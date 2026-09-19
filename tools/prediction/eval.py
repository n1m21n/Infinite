#!/usr/bin/env python3
"""Offline evaluation gate for the prediction modulator (docs/plans/prediction README §4.3, step 2).

For every param key with >= 5 min of active hand time it fits the same online statistics the app keeps
(src/core/MovementStats.cpp, reimplemented here in numpy) on the first 80% of the key's active time,
then scores the negative log-likelihood of the value 1 s ahead over the last 20% under three models:

  B0 hold    Gaussian at the current value, variance = the training set's empirical 1 s change variance
  B1 range   uniform over the observed range (2nd..98th percentile of the smoothed dwell landscape)
  Drift      64 simulated paths of the README §4.1 SDE for 1 s -> Gaussian KDE -> density at the truth

Usage:
  eval.py [LOG_DIR_OR_FILES ...]     default: the app's movement-log folder
  eval.py --selftest                 synthetic Ornstein-Uhlenbeck knob, checks the whole pipeline

Lower NLL is better. The gate (step 2 exit criterion): Drift should beat both baselines on most
hand-moved keys, otherwise revisit README §4 before building any node.
"""
import argparse
import glob
import math
import os
import struct
import sys
import tempfile
import zlib
from collections import defaultdict

import numpy as np

# ----------------------------------------------------------------------------- constants
# Mirrors MovementStats.h.
BINS = 64
GRID_DT = 0.1
HALF_LIFE_ACTIVE_SEC = 14.0 * 24.0 * 3600.0
HAND_ACTIVE_WINDOW = 30.0
PHI_MIN, PHI_MAX = 0.5, 0.999
MAX_CATCH_UP = 60.0

SRC_HAND, SRC_PERF, SRC_MOD, SRC_EXPR, SRC_GESTURE, SRC_PRED, SRC_OTHER = range(7)
FLAG_CORRECTION = 1
MARK_SESSION_START, MARK_SESSION_END, MARK_PATCH_LOADED, MARK_PATCH_NEW, MARK_UNDO, MARK_REDO = range(6)
JUMP_MARKS = (MARK_PATCH_LOADED, MARK_PATCH_NEW, MARK_UNDO, MARK_REDO)

REC_KEY, REC_VAL, REC_BIND, REC_TRANSPORT, REC_MARK = 1, 2, 3, 4, 5

# Evaluation settings.
HORIZON_TICKS = 10           # 1 s at 10 Hz
MIN_HAND_SECONDS = 300.0     # a key needs 5 min of active hand time
TRAIN_FRACTION = 0.8
N_PATHS = 64
SIM_DT = 0.05
DENSITY_FLOOR = 1e-3         # applied to all three models so none is rewarded for a hard zero
KDE_MIN_BW = 0.005
LANDSCAPE_EPS = 1e-2
DRIFT_STEP_CAP = 0.05


# ----------------------------------------------------------------------------- log reading
def _varint(buf, i):
    val = shift = 0
    while True:
        b = buf[i]
        i += 1
        val |= (b & 0x7F) << shift
        if not b & 0x80:
            return val, i
        shift += 7


def read_records(path):
    """Yield dicts from a .mlog or .mlog.z file (port of MovementLog::ReadFile). A corrupt tail ends the
    stream instead of raising, like the C++ reader."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:6] != b"IMLOG1":
        size = struct.unpack_from("<I", raw, 0)[0]
        raw = zlib.decompress(raw[4:])
        if len(raw) != size:
            return
    if len(raw) < 8 or raw[:6] != b"IMLOG1" or struct.unpack_from("<H", raw, 6)[0] != 1:
        return
    i, n = 8, len(raw)
    try:
        while i < n:
            tag = raw[i]
            i += 1
            if tag == REC_KEY:
                dt, i = _varint(raw, i)
                kid, i = _varint(raw, i)
                uid, i = _varint(raw, i)
                pidx, i = _varint(raw, i)
                ln, i = _varint(raw, i)
                tname = raw[i:i + ln].decode("utf-8", "replace")
                i += ln
                ln, i = _varint(raw, i)
                name = raw[i:i + ln].decode("utf-8", "replace")
                i += ln
                mn, mx, step = struct.unpack_from("<fff", raw, i)
                i += 12
                flags = raw[i]
                i += 1
                yield dict(type=REC_KEY, dt=dt, id=kid, uid=uid, pidx=pidx, tname=tname, name=name,
                           min=mn, max=mx, is_enum=bool(flags & 1), is_bool=bool(flags & 2))
            elif tag == REC_VAL:
                kid, i = _varint(raw, i)
                dt, i = _varint(raw, i)
                q = raw[i] | (raw[i + 1] << 8)
                src, flags = raw[i + 2], raw[i + 3]
                i += 4
                yield dict(type=REC_VAL, dt=dt, id=kid, q=q, src=src, flags=flags)
            elif tag == REC_BIND:
                dt, i = _varint(raw, i)
                kid, i = _varint(raw, i)
                i += 1
                _, i = _varint(raw, i)
                _, i = _varint(raw, i)
                i += 8
                yield dict(type=REC_BIND, dt=dt, id=kid)
            elif tag == REC_TRANSPORT:
                dt, i = _varint(raw, i)
                playing = raw[i] != 0
                i += 1 + 4 + 8
                _, i = _varint(raw, i)
                yield dict(type=REC_TRANSPORT, dt=dt, playing=playing)
            elif tag == REC_MARK:
                mark = raw[i]
                i += 1
                dt, i = _varint(raw, i)
                if mark == MARK_SESSION_END:
                    _, i = _varint(raw, i)
                yield dict(type=REC_MARK, dt=dt, mark=mark)
            else:
                return
    except (IndexError, struct.error):
        return


# ----------------------------------------------------------------------------- replay
def source_weight(src, flags, changed):
    """README §5. Vectorised over arrays."""
    src = np.asarray(src)
    flags = np.asarray(flags)
    w = np.zeros(src.shape, dtype=np.float64)
    hand = (src == SRC_HAND) | (src == SRC_PERF)
    w[hand] = np.where(flags[hand] & FLAG_CORRECTION, 3.0, 1.0)
    w[src == SRC_GESTURE] = 0.2
    w[(src == SRC_MOD) | (src == SRC_EXPR)] = 0.1
    w[(src == SRC_PRED) & changed] = 0.1
    return w


class KeySeries:
    """The active-tick series of one param key, concatenated over session files in time order."""

    def __init__(self, uid, pidx, tname, name):
        self.uid, self.pidx, self.tname, self.name = uid, pidx, tname, name
        self.parts = defaultdict(list)

    def finish(self):
        self.pos = np.concatenate(self.parts["pos"]) if self.parts["pos"] else np.zeros(0)
        self.w = np.concatenate(self.parts["w"]) if self.parts["w"] else np.zeros(0)
        self.pair = np.concatenate(self.parts["pair"]) if self.parts["pair"] else np.zeros(0, bool)
        self.hand = np.concatenate(self.parts["hand"]) if self.parts["hand"] else np.zeros(0, bool)
        self.clock = np.concatenate(self.parts["clock"]) if self.parts["clock"] else np.zeros(0)
        self.hand_seconds = float(self.hand.sum()) * GRID_DT


def replay_session(path, series, clock0):
    """Turn one session file into per-key active-tick samples (mirrors Engine::ReplayFile + Tick).
    Returns the active clock after the file."""
    keys = {}          # file key id -> (uid, pidx)
    events = defaultdict(lambda: ([], [], [], []))   # (uid,pidx) -> t, pos, src, flags
    hand_times = []
    play_edges = []    # (t, playing)
    jumps = []
    rec_times = []
    t = 0.0
    for rec in read_records(path):
        t += rec["dt"] / 1000.0
        rec_times.append(t)
        typ = rec["type"]
        if typ == REC_KEY:
            k = (rec["uid"], rec["pidx"])
            keys[rec["id"]] = k
            if not (rec["is_enum"] or rec["is_bool"]) and k not in series:
                series[k] = KeySeries(rec["uid"], rec["pidx"], rec["tname"], rec["name"])
            if rec["is_enum"] or rec["is_bool"]:
                keys[rec["id"]] = None
        elif typ == REC_VAL:
            k = keys.get(rec["id"])
            if k is None:
                continue
            ev = events[k]
            ev[0].append(t)
            ev[1].append(rec["q"] / 65535.0)
            ev[2].append(rec["src"])
            ev[3].append(rec["flags"])
            if rec["src"] in (SRC_HAND, SRC_PERF):
                hand_times.append(t)
        elif typ == REC_TRANSPORT:
            play_edges.append((t, rec["playing"]))
        elif typ == REC_MARK:
            if rec["mark"] in JUMP_MARKS:
                jumps.append(t)
            elif rec["mark"] == MARK_SESSION_END:
                play_edges.append((t, False))
    if not rec_times:
        return clock0
    t_end = t
    n_ticks = int(math.floor(t_end / GRID_DT + 1e-9)) + 1
    ticks = np.arange(n_ticks) * GRID_DT

    # Active mask: transport playing, or within 30 s after any hand move.
    diff = np.zeros(n_ticks + 1, dtype=np.int64)
    playing_since = None
    for et, pl in play_edges + [(t_end + GRID_DT, False)]:
        if pl and playing_since is None:
            playing_since = et
        elif not pl and playing_since is not None:
            a = int(math.ceil(playing_since / GRID_DT - 1e-9))
            b = min(n_ticks, int(math.ceil(et / GRID_DT - 1e-9)))
            if b > a:
                diff[a] += 1
                diff[b] -= 1
            playing_since = None
    for ht in hand_times:
        a = int(math.ceil(ht / GRID_DT - 1e-9))
        b = min(n_ticks, int(math.floor((ht + HAND_ACTIVE_WINDOW) / GRID_DT + 1e-9)) + 1)
        if b > a:
            diff[a] += 1
            diff[b] -= 1
    active = np.cumsum(diff[:n_ticks]) > 0

    # Gaps of more than a minute between records are skipped, not replayed (Engine::Advance).
    rt = np.asarray(rec_times)
    for a, b in zip(rt[:-1], rt[1:]):
        if b - a > MAX_CATCH_UP + GRID_DT:
            lo = int(math.floor(a / GRID_DT + 1e-9)) + 1
            hi = int(math.ceil(b / GRID_DT - 1e-9))
            active[lo:hi] = False

    clock = clock0 + np.cumsum(active) * GRID_DT
    act_idx = np.nonzero(active)[0]
    if act_idx.size == 0:
        return clock0
    act_t = ticks[act_idx]
    jump_arr = np.asarray(jumps)
    # Chain continuity between consecutive active ticks: adjacent and no skipped gap.
    adjacent = np.zeros(act_idx.size, dtype=bool)
    adjacent[1:] = np.diff(act_idx) == 1
    if jump_arr.size:
        jidx = np.searchsorted(jump_arr, act_t, side="right") - 1
        last_jump = np.where(jidx >= 0, jump_arr[np.maximum(jidx, 0)], -np.inf)
    else:
        jidx = np.full(act_t.shape, -1)
        last_jump = np.full(act_t.shape, -np.inf)
    same_jump_epoch = np.zeros(act_idx.size, dtype=bool)
    same_jump_epoch[1:] = jidx[1:] == jidx[:-1]

    hand_arr = np.asarray(hand_times)
    for k, (et, ep, es, ef) in events.items():
        et = np.asarray(et)
        ep = np.asarray(ep)
        es = np.asarray(es)
        ef = np.asarray(ef)
        idx = np.searchsorted(et, act_t, side="right") - 1
        has = idx >= 0
        safe = np.maximum(idx, 0)
        valid = has & (et[safe] >= last_jump)          # a jump invalidates the hold until the next VAL
        changed = np.zeros(act_t.shape, dtype=bool)
        changed[1:] = idx[1:] != idx[:-1]
        changed[0] = has[0]
        w = np.where(valid, source_weight(es[safe], ef[safe], changed), 0.0)
        pos = np.where(valid, ep[safe], np.nan)
        pair = np.zeros(act_t.shape, dtype=bool)
        pair[1:] = valid[1:] & valid[:-1] & adjacent[1:] & same_jump_epoch[1:]
        # Hand time of THIS key: within 30 s after one of its own hand moves.
        own_hand = et[(es == SRC_HAND) | (es == SRC_PERF)]
        if own_hand.size:
            j = np.searchsorted(own_hand, act_t, side="right") - 1
            hand = (j >= 0) & (act_t - own_hand[np.maximum(j, 0)] <= HAND_ACTIVE_WINDOW)
        else:
            hand = np.zeros(act_t.shape, dtype=bool)
        s = series[k]
        s.parts["pos"].append(pos)
        s.parts["w"].append(w)
        s.parts["pair"].append(pair)
        s.parts["hand"].append(hand)
        s.parts["clock"].append(clock[act_idx])
    return float(clock[-1])


def load_series(paths):
    series = {}
    clock = 0.0
    for p in sorted(paths, key=lambda x: os.path.basename(x)):
        clock = replay_session(p, series, clock)
    for s in series.values():
        s.finish()
    return series


# ----------------------------------------------------------------------------- statistics
def decayed_weights(s, upto, clock_now):
    f = np.exp2(-(clock_now - s.clock[:upto]) / HALF_LIFE_ACTIVE_SEC)
    return s.w[:upto] * f


def fit(pos, w, pair, dw):
    """The engine's sums over a training slice: returns dict with W, S*, hist."""
    m = ~np.isnan(pos)
    x = np.where(pair, np.concatenate([[np.nan], pos[:-1]]), np.nan)
    pm = pair & (dw > 0)
    wp = dw[pm]
    xp, yp = x[pm], pos[pm]
    st = dict(W=wp.sum(), Sx=(wp * xp).sum(), Sy=(wp * yp).sum(), Sxx=(wp * xp * xp).sum(),
              Sxy=(wp * xp * yp).sum(), Syy=(wp * yp * yp).sum())
    hm = m & (dw > 0)
    bins = np.clip((pos[hm] * BINS).astype(int), 0, BINS - 1)
    st["hist"] = np.bincount(bins, weights=dw[hm], minlength=BINS)[:BINS]
    return st


def smoothed_hist(hist):
    radius, sigma = 6, 2.0
    k = np.arange(-radius, radius + 1)
    kern = np.exp(-0.5 * k * k / (sigma * sigma))
    padded = np.concatenate([hist[radius - 1::-1], hist, hist[:-radius - 1:-1]])   # reflect at the walls
    out = np.convolve(padded, kern, mode="valid")
    tot = out.sum()
    return out / tot if tot > 0 else out


def derive(st, dt=GRID_DT):
    p = smoothed_hist(st["hist"])
    d = dict(valid=False, phi=PHI_MAX, theta=0.0, mu=0.0, sigma=0.0, lo=0.0, hi=1.0, p=p)
    if p.sum() > 0:
        cdf = np.cumsum(p)

        def pct(q):
            i = int(np.searchsorted(cdf, q))
            i = min(i, BINS - 1)
            prev = cdf[i - 1] if i else 0.0
            return (i + (q - prev) / p[i]) / BINS if p[i] > 0 else (i + 0.5) / BINS
        d["lo"], d["hi"] = pct(0.02), pct(0.98)
    W = st["W"]
    if W <= 0:
        return d
    mx, my = st["Sx"] / W, st["Sy"] / W
    vx = max(0.0, st["Sxx"] / W - mx * mx)
    vy = max(0.0, st["Syy"] / W - my * my)
    cov = st["Sxy"] / W - mx * my
    if vx < 1e-8:
        d["mu"] = my
        return d
    phi = min(max(cov / vx, PHI_MIN), PHI_MAX)
    resid = max(0.0, vy - 2 * phi * cov + phi * phi * vx)
    theta = -math.log(phi) / dt
    d.update(phi=phi, theta=theta, mu=(my - phi * mx) / (1 - phi),
             sigma=math.sqrt(resid * 2 * theta / (1 - phi * phi)), valid=W >= 30.0)
    return d


# ----------------------------------------------------------------------------- models
def gauss_pdf(x, mean, var):
    return np.exp(-0.5 * (x - mean) ** 2 / var) / np.sqrt(2 * np.pi * var)


def simulate_drift(x0, v0, d, tau_m, rng, stray=1.0, speed=1.0):
    """README §4.1 for 1 s from each start value. x0, v0: (N,). Returns (N, N_PATHS)."""
    lo, hi = d["lo"], d["hi"]
    centers = (np.arange(BINS) + 0.5) / BINS
    landscape = -np.log(d["p"] * BINS + LANDSCAPE_EPS)
    grad = np.gradient(landscape, 1.0 / BINS)          # U'(x) on the bin centres
    sigma = d["sigma"] * math.sqrt(speed)
    eta = sigma * sigma / (2.0 * stray)                # T = sigma^2 / 2 eta; T = 1 reproduces p-hat
    n = x0.shape[0]
    x = np.repeat(x0[:, None], N_PATHS, axis=1)
    v = np.repeat(v0[:, None], N_PATHS, axis=1)
    decay = math.exp(-SIM_DT / tau_m) if tau_m > 0 else 0.0
    steps = int(round(1.0 / SIM_DT))
    for _ in range(steps):
        v *= decay
        drift = np.clip(-eta * np.interp(x, centers, grad) * SIM_DT, -DRIFT_STEP_CAP, DRIFT_STEP_CAP)
        x = x + v * SIM_DT + drift + sigma * math.sqrt(SIM_DT) * rng.standard_normal((n, N_PATHS))
        x = np.where(x < lo, 2 * lo - x, x)
        x = np.where(x > hi, 2 * hi - x, x)
        x = np.clip(x, lo, hi)
    return x


def kde_density(samples, truth):
    sd = samples.std(axis=1)
    bw = np.maximum(1.06 * sd * samples.shape[1] ** -0.2, KDE_MIN_BW)
    z = (truth[:, None] - samples) / bw[:, None]
    return np.exp(-0.5 * z * z).mean(axis=1) / (bw * math.sqrt(2 * math.pi))


def evaluate_key(s, tau_m, seed=0):
    """Returns a result dict, or None with a 'skip' reason."""
    n = s.pos.size
    split = int(n * TRAIN_FRACTION)
    if split < 100 or n - split < 100:
        return dict(skip="too little data")
    clock_now = s.clock[split - 1]
    st = fit(s.pos[:split], s.w[:split], s.pair[:split], decayed_weights(s, split, clock_now))
    d = derive(st)
    if not d["valid"] or d["hi"] - d["lo"] < 1.0 / BINS or d["sigma"] <= 0:
        return dict(skip="no usable fit (knob barely moved)")

    pos, pair = s.pos, s.pair
    # Test origins: every tick with a full 1 s of continuous samples after it.
    idx = np.arange(split, n - HORIZON_TICKS)
    ok = ~np.isnan(pos[idx])
    for j in range(1, HORIZON_TICKS + 1):
        ok &= pair[idx + j]
    idx = idx[ok]
    if idx.size < 50:
        return dict(skip="too few 1 s windows in the test part")
    x0 = pos[idx]
    truth = pos[idx + HORIZON_TICKS]

    # B0: empirical 1 s change variance on the training part.
    tr = np.arange(0, split - HORIZON_TICKS)
    tok = ~np.isnan(pos[tr])
    for j in range(1, HORIZON_TICKS + 1):
        tok &= pair[tr + j]
    tr = tr[tok]
    if tr.size < 50:
        return dict(skip="too few 1 s windows in the training part")
    var0 = max(float(np.mean((pos[tr + HORIZON_TICKS] - pos[tr]) ** 2)), 1e-6)
    b0 = np.maximum(gauss_pdf(truth, x0, var0), DENSITY_FLOOR)

    # B1: uniform over the observed range.
    width = d["hi"] - d["lo"]
    b1 = np.where((truth >= d["lo"]) & (truth <= d["hi"]), 1.0 / width, 0.0)
    b1 = np.maximum(b1, DENSITY_FLOOR)

    # Drift, seeded with the hand's velocity over the last 0.3 s (the release velocity).
    back = np.maximum(idx - 3, 0)
    v_ok = pair[idx] & pair[idx - 1] & pair[idx - 2] & pair[np.maximum(idx - 2, 0) + 0] & ~np.isnan(pos[back])
    v0 = np.where(v_ok, (pos[idx] - pos[back]) / (3 * GRID_DT), 0.0)
    rng = np.random.default_rng(seed)
    drift_pdf = np.empty(idx.size)
    for a in range(0, idx.size, 20000):
        sl = slice(a, a + 20000)
        sims = simulate_drift(x0[sl], v0[sl], d, tau_m, rng)
        drift_pdf[sl] = kde_density(sims, truth[sl])
    dr = np.maximum(drift_pdf, DENSITY_FLOOR)

    nll = lambda p: float(-np.log(p).mean())
    return dict(n_test=int(idx.size), b0=nll(b0), b1=nll(b1), drift=nll(dr),
                theta=d["theta"], sigma=d["sigma"], lo=d["lo"], hi=d["hi"])


# ----------------------------------------------------------------------------- driver
def default_log_dir():
    home = os.path.expanduser("~")
    if sys.platform == "darwin":
        return os.path.join(home, "Library/Application Support/Infinite/movement-log")
    if sys.platform.startswith("win"):
        return os.path.join(os.environ.get("APPDATA", home), "Infinite", "movement-log")
    return os.path.join(os.environ.get("XDG_DATA_HOME", os.path.join(home, ".local/share")), "Infinite", "movement-log")


def gather(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += glob.glob(os.path.join(p, "*.mlog")) + glob.glob(os.path.join(p, "*.mlog.z"))
        else:
            files.append(p)
    return sorted(set(files), key=os.path.basename)


def run(files, tau_m, min_hand=MIN_HAND_SECONDS, quiet=False):
    series = load_series(files)
    total_hand = sum(s.hand_seconds for s in series.values())
    if not quiet:
        print(f"{len(files)} session file(s), {len(series)} continuous key(s), "
              f"{total_hand / 3600.0:.2f} h of per-key hand time")
    rows, skipped = [], []
    for k, s in sorted(series.items(), key=lambda kv: -kv[1].hand_seconds):
        label = f"{s.tname}.{s.name} [{s.uid}:{s.pidx}]"
        if s.hand_seconds < min_hand:
            skipped.append((label, f"only {s.hand_seconds / 60.0:.1f} min hand time"))
            continue
        res = evaluate_key(s, tau_m)
        if "skip" in res:
            skipped.append((label, res["skip"]))
            continue
        res["label"], res["hand_min"] = label, s.hand_seconds / 60.0
        rows.append(res)

    if not quiet:
        if rows:
            print(f"\n{'key':46s} {'hand':>6s} {'tests':>7s} {'B0 hold':>9s} {'B1 range':>9s} {'Drift':>9s}  verdict")
            for r in rows:
                best = min(r["b0"], r["b1"])
                verdict = "DRIFT" if r["drift"] < best else ("hold" if r["b0"] <= r["b1"] else "range")
                print(f"{r['label'][:46]:46s} {r['hand_min']:5.1f}m {r['n_test']:7d} "
                      f"{r['b0']:9.3f} {r['b1']:9.3f} {r['drift']:9.3f}  {verdict}")
        print(f"\nskipped {len(skipped)} key(s):")
        for label, why in skipped[:12]:
            print(f"  {label[:60]:60s} {why}")
        if len(skipped) > 12:
            print(f"  ... and {len(skipped) - 12} more")
    wins = sum(1 for r in rows if r["drift"] < min(r["b0"], r["b1"]))
    return rows, wins, series


def summary(rows, wins):
    if not rows:
        print("\nSUMMARY: no key had enough data to evaluate. The gate cannot be decided yet.")
        return None
    share = wins / len(rows)
    print(f"\nSUMMARY: Drift beats BOTH baselines on {wins}/{len(rows)} keys ({share:.0%}).")
    print("  mean NLL  B0 %.3f   B1 %.3f   Drift %.3f" % (
        np.mean([r["b0"] for r in rows]), np.mean([r["b1"] for r in rows]), np.mean([r["drift"] for r in rows])))
    print("  GATE:", "PASS - continue to step 3" if share > 0.5 else
          "FAIL - revisit README §4 before writing any node")
    return share


# ----------------------------------------------------------------------------- self test
def _put_varint(buf, v):
    while v >= 0x80:
        buf.append((v & 0x7F) | 0x80)
        v >>= 7
    buf.append(v & 0x7F)


def write_synthetic_log(path, keys, minutes, seed):
    """keys: list of (uid, pidx, name, theta, mu, sigma). One hand-moved OU knob per entry, 10 Hz."""
    rng = np.random.default_rng(seed)
    buf = bytearray(b"IMLOG1" + struct.pack("<H", 1))
    last_ms = 0

    def dt_for(ms):
        nonlocal last_ms
        d = max(0, ms - last_ms)
        last_ms = max(last_ms, ms)
        return d

    buf.append(REC_MARK); buf.append(MARK_SESSION_START); _put_varint(buf, 0)
    for i, (uid, pidx, name, *_rest) in enumerate(keys):
        buf.append(REC_KEY)
        _put_varint(buf, 0)
        _put_varint(buf, i + 1); _put_varint(buf, uid); _put_varint(buf, pidx)
        for text in ("SynthType", name):
            enc = text.encode()
            _put_varint(buf, len(enc)); buf += enc
        buf += struct.pack("<fff", 0.0, 1.0, 0.0)
        buf.append(0)
    steps = int(minutes * 60 / GRID_DT)
    x = [k[4] for k in keys]
    for n in range(steps):
        ms = int(round(n * GRID_DT * 1000))
        for i, (uid, pidx, name, theta, mu, sigma) in enumerate(keys):
            a = math.exp(-theta * GRID_DT)
            sd = sigma * math.sqrt((1 - a * a) / (2 * theta))
            x[i] = mu + (x[i] - mu) * a + sd * rng.standard_normal()
            q = int(round(min(max(x[i], 0.0), 1.0) * 65535))
            buf.append(REC_VAL)
            _put_varint(buf, i + 1)
            _put_varint(buf, dt_for(ms))
            buf += bytes([q & 0xFF, q >> 8, SRC_HAND, 0])
    buf.append(REC_MARK); buf.append(MARK_SESSION_END); _put_varint(buf, dt_for(last_ms)); _put_varint(buf, 0)
    with open(path, "wb") as f:
        f.write(buf)


def selftest():
    print("selftest: synthetic Ornstein-Uhlenbeck knobs, 40 min of hand time each")
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, "20260101-000000.mlog")
        # A persistent knob (theta 0.5) and a jittery one (theta 3), both well inside [0,1].
        write_synthetic_log(p, [(1, 0, "slow", 0.5, 0.3, 0.1), (2, 0, "fast", 3.0, 0.6, 0.15)], 40, seed=1)
        recs = list(read_records(p))
        assert recs and recs[-1]["type"] == REC_MARK, "reader failed on the synthetic file"
        rows, wins, series = run([p], tau_m=0.0, quiet=True)
        assert len(rows) == 2, f"expected 2 evaluated keys, got {len(rows)}"
        for r in rows:
            print(f"  {r['label']:32s} B0 {r['b0']:.3f}  B1 {r['b1']:.3f}  Drift {r['drift']:.3f}  "
                  f"(theta {r['theta']:.2f}, sigma {r['sigma']:.3f})")
            assert r["drift"] < r["b1"], "Drift must beat the uniform-range baseline on a mean-reverting knob"
        slow = next(r for r in rows if "slow" in r["label"])
        assert slow["drift"] < slow["b0"] + 0.05, "Drift should be at least as good as hold on the persistent knob"
        assert abs(slow["theta"] - 0.5) < 0.15, f"theta recovered {slow['theta']:.3f}, expected ~0.5"
        # Reader edge cases: a truncated tail yields the prefix, an unknown tag ends the stream.
        with open(p, "rb") as f:
            raw = f.read()
        trunc = os.path.join(tmp, "20260101-000001.mlog")
        with open(trunc, "wb") as f:
            f.write(raw[:len(raw) // 2])
        assert 0 < len(list(read_records(trunc))) < len(recs)
        # The .z container: 4-byte raw size + zlib stream.
        zpath = p + ".z"
        with open(zpath, "wb") as f:
            f.write(struct.pack("<I", len(raw)) + zlib.compress(raw))
        assert len(list(read_records(zpath))) == len(recs), ".mlog.z read differs from .mlog"
    print("selftest: PASS")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="session files or folders (default: the app's movement-log folder)")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--tau-m", type=float, default=0.5, help="Drift momentum time constant, seconds (0 = off)")
    ap.add_argument("--min-hand-minutes", type=float, default=MIN_HAND_SECONDS / 60.0)
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return 0
    files = gather(args.paths or [default_log_dir()])
    if not files:
        print("No .mlog/.mlog.z files found. Logging starts with the step-1 build; nothing to evaluate yet.")
        return 2
    rows, wins, _ = run(files, args.tau_m, args.min_hand_minutes * 60.0)
    summary(rows, wins)
    return 0


if __name__ == "__main__":
    sys.exit(main())
