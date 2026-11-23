"""Phase 3 scorer (to be locked at the start of phase 4 and run unchanged on B): H1 (sound) and H3 (covers),
plus the Float32 re-check of every certificate on the real interpreter.

Per kernel:
  1. Oracle (D1): instability witness over the declared knob box (u-space, log axes for cutoff-like
     knobs), exhaustive length 1-2, greedy length 3-8, Nelder-Mead on near misses.
  2. Certifier (T1, certify.py): common P over the whole box, with the cover. Field `if(p > c)` switches
     split the cover at c.
  3. Float32 re-check: the certificate's check points (every LMI point plus the 5000 highest-P-norm cover
     points) are dumped from the Field interpreter (INFINITE_FIELDSTABDUMP, the phase 2 binary, no
     rebuild) and ||A_field||_P is measured. A certificate holds in Float32 if every point stays < 1 and
     the largest rise over float64 is below the cover's slack (1 - largest cell bound). Kernels Field
     can't compile (the 75 G kernels over the register cap, owner decision 2026-11-23) use the bound
     sqrt(kappa) * n * 10 * (largest Field-vs-Python error measured in phase 2 for that set).

    python phase3_score.py <set,...> [--jobs N] [--only name,...]     sets: L, G (B in phase 4)
Writes data/phase3-<sets>.json.
"""
import argparse, json, math, os, pathlib, shutil, subprocess, sys, tempfile, time
from multiprocessing import Pool
import numpy as np
from scipy.optimize import minimize

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from emit_field import kernels, fmt  # noqa: E402
from certify import Box, certify, pnorm_factory  # noqa: E402
from stabmod import find_witness, radius  # noqa: E402

TOL = 1e-6
SEED = 20261123
# Largest Field-vs-Python error per set from phase 2 (data/phase2-compare.json), times 10 for the bound.
PHASE2_ERR = {"L": 1.67e-6, "G": 1.9e-7}

def box_of(params, spec=None):
    lo = [p[2] for p in params]
    hi = [p[3] for p in params]
    log = [j for j, p in enumerate(params) if p[2] > 0 and p[3] / p[2] >= 10]
    breaks = {}
    for (_, _, _, terms) in (spec or {}).get("entries", []):
        for (_, kind, p, thr) in terms:
            if kind == "select":
                breaks.setdefault(p, set()).add(thr)
    return Box(lo, hi, log=log, breaks={j: sorted(v) for j, v in breaks.items()})

DENSE = {1: 129, 2: 33, 3: 13}

def oracle(Afun, box, n, rng, per):
    """Witness search. Phase 3 fix (rig bug, found before the verdict, recorded in the README): the phase 1
    oracle only looked at a 4-9 point grid and tuned only near misses above 0.97, so a kernel that is
    unstable at one fixed knob setting between grid points was called "no witness". Now:
      (a) a dense frozen scan (129 / 33 / 13 points per axis for 1 / 2 / 3 knobs) plus Nelder-Mead on
          rho(A) from the 5 worst points: a frozen-unstable setting is a length-1 witness;
      (b) the switching search over the coarse grid, as before;
      (c) Nelder-Mead on the top 3 switching sequences, always (not only on near misses)."""
    d = len(box.lo)
    ulo, uhi = box.ulo, box.uhi
    clipu = lambda u: np.clip(u, ulo, uhi)  # noqa: E731
    best_rate, best = 0.0, None
    # (a) frozen
    dense = box.grid([DENSE[d]] * d)
    rs = np.array([radius(Afun(th)) for th in dense])
    for i in np.argsort(-rs)[:5]:
        if rs[i] > best_rate:
            best_rate, best = float(rs[i]), [list(dense[i])]
        res = minimize(lambda u: -radius(Afun(box.from_u(clipu(u)))), box.to_u(dense[i]), method="Nelder-Mead",
                       options={"maxiter": 400, "xatol": 1e-9, "fatol": 1e-13})
        if -res.fun > best_rate:
            best_rate, best = float(-res.fun), [list(box.from_u(clipu(res.x)))]
    # (b) switching
    cands = box.grid([per] * d)
    seqs = find_witness(lambda th: Afun(th), cands, rng, max_len=8, starts=20, top=4)
    for r, seq in seqs:
        rate = r ** (1 / len(seq))
        if rate > best_rate:
            best_rate, best = rate, [list(cands[i]) for i in seq]
    # (c) tune
    for r, seq in sorted(seqs, key=lambda s: -s[0] ** (1 / len(s[1])))[:3]:
        L = len(seq)
        def neg(v):
            P = np.eye(n)
            for u in clipu(v.reshape(L, d)):
                P = Afun(box.from_u(u)) @ P
            return -radius(P) ** (1 / L)
        x0 = np.array([box.to_u(cands[i]) for i in seq], float).ravel()
        res = minimize(neg, x0, method="Nelder-Mead", options={"maxiter": 3000, "xatol": 1e-7, "fatol": 1e-12})
        if -res.fun > best_rate:
            best_rate = -res.fun
            best = [list(box.from_u(u)) for u in clipu(res.x.reshape(L, d))]
    return best_rate, best

def score_one(job):
    set_, name, params, spec, idx = job
    ref = REFS[(set_, name)]
    Afun = lambda th: np.asarray(ref(tuple(th)), float)  # noqa: E731
    box = box_of(params, spec)
    n = Afun(tuple(box.lo)).shape[0]
    rng = np.random.default_rng(SEED + idx)
    t0 = time.time()
    rate, seq = oracle(Afun, box, n, rng, per={1: 9, 2: 5, 3: 4}[len(params)] if set_ != "L" else {1: 17, 2: 9}[len(params)])
    t1 = time.time()
    witness = bool(rate ** len(seq) > 1 + TOL)
    cert = certify(lambda *th: Afun(th), box)
    t2 = time.time()
    row = {"set": set_, "name": name, "n": n, "d": len(params), "witness": witness, "growth_per_step": rate,
           "witness_seq": seq, "status": cert["status"], "reason": cert.get("reason", ""),
           "t": cert.get("t"), "kappa": cert.get("kappa"), "worst_bound": cert.get("worst_bound"),
           "cells": cert.get("cells"), "rounds": cert.get("rounds"), "lmi_points": cert.get("lmi_points"),
           "oracle_s": t1 - t0, "certify_s": t2 - t1, "breaks": {str(k): v for k, v in box.breaks.items()}}
    if "P" in cert:
        row["P"] = cert["P"]
        row["check_points"] = cert["check_points"]
    return row

REFS = {}

def find_bin():
    for p in [os.environ.get("INFINITE_BIN", ""), ROOT.parents[2] / "build-dev/Infinite.app/Contents/MacOS/Infinite",
              ROOT.parents[2] / "build/Infinite.app/Contents/MacOS/Infinite"]:
        if p and pathlib.Path(p).exists():
            return str(p)
    raise SystemExit("no Infinite binary; set INFINITE_BIN")

NEAR_BREAK = 1e-6  # of the knob range; the cover's 1e-9 "just inside" offset is below Float32 resolution

def f32_points(r, spec, params):
    """The cover evaluates A 1e-9 inside an `if` threshold. Float32 can't resolve that, so the interpreter
    rounds the knob back across the threshold and takes the other branch: a check-point artifact, not a
    Float32 error (both branches are covered up to the threshold). Move such points to NEAR_BREAK of the
    range, on the side the cover used, where Float32 and float64 pick the same branch."""
    thr = {}
    for (_, _, _, terms) in (spec or {}).get("entries", []):
        for (_, kind, j, c) in terms:
            if kind == "select":
                thr.setdefault(j, set()).add(c)
    out, moved = [], 0
    for th in r["check_points"]:
        th = list(th)
        for j, cs in thr.items():
            w = NEAR_BREAK * (params[j][3] - params[j][2])
            for c in cs:
                if abs(th[j] - c) < w:
                    th[j] = c + w if th[j] > c else c - w
                    moved += 1
        out.append(th)
    return out, moved

def float32_check(rows, field_dir, specs=None, params_of=None):
    """Dump A at each certificate's check points from the interpreter and re-measure ||A||_P."""
    todo = [r for r in rows if "P" in r]
    for r in todo:
        r["f32_points"], r["f32_moved"] = f32_points(r, (specs or {}).get(r["name"]), params_of[(r["set"], r["name"])])
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="stab3-"))
    try:
        for r in todo:
            src = field_dir / r["set"] / f"{r['name']}.field"
            d = tmp / "in" / r["set"]
            d.mkdir(parents=True, exist_ok=True)
            shutil.copy(src, d / src.name)
            (d / f"{r['name']}.grid").write_text("".join(" ".join(fmt(v) for v in p) + "\n" for p in r["f32_points"]))
        env = dict(os.environ, INFINITE_FIELDSTABDUMP=str(tmp / "in"), INFINITE_FIELDSTABDUMP_OUT=str(tmp / "out"))
        subprocess.run([find_bin()], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)
        for r in todo:
            P = np.array(r["P"])
            pn = pnorm_factory(P)
            ref = REFS[(r["set"], r["name"])]
            slack = 1.0 - (r["worst_bound"] or 0.0) if r["status"] == "certified" else None
            p = tmp / "out" / r["set"] / f"{r['name']}.amat"
            head = p.read_text().split("\n", 1)[0].split() if p.exists() else ["status", "missing"]
            if head[1] == "ok":
                n, npar = int(head[3]), int(head[5])
                worst_f, rise = 0.0, -np.inf
                for ln in p.read_text().splitlines()[1:]:
                    v = [float(x) for x in ln.split()]
                    th, Af = v[:npar], np.array(v[npar:]).reshape(n, n)
                    nf = pn(Af)
                    worst_f = max(worst_f, nf)
                    # compare at the knob values the interpreter actually used (Float32), so a point just
                    # past an `if` threshold that rounds back across it is compared with the same side
                    th32 = tuple(float(np.float32(v)) for v in th)
                    rise = max(rise, nf - pn(np.asarray(ref(th32), float)))
                r["f32"] = {"source": "field", "points": len(r["f32_points"]), "moved_off_break": r["f32_moved"], "max_norm": worst_f,
                            "max_rise": rise, "slack": slack,
                            "holds": bool(worst_f < 1.0 and (slack is None or rise < slack))}
            else:
                bound = math.sqrt(r["kappa"]) * r["n"] * 10 * PHASE2_ERR[r["set"]]
                r["f32"] = {"source": "bound (not compiled: " + " ".join(head[1:2] + head[8:]) + ")",
                            "max_rise": bound, "slack": slack,
                            "holds": bool(slack is None or bound < slack)}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

def init_refs():
    for set_, name, _, params, _, ref in kernels():
        REFS[(set_, name)] = ref

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("sets")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    sets, only = a.sets.split(","), set(filter(None, a.only.split(",")))
    init_refs()
    G = {s["id"]: s for s in json.loads((ROOT / "corpus" / "G.json").read_text())}
    jobs = [(s, nm, params, G.get(nm) if s == "G" else None, i)
            for i, (s, nm, _, params, _, _) in enumerate(kernels()) if s in sets and (not only or nm in only)]
    t0 = time.time()
    with Pool(a.jobs, initializer=init_refs) as pool:
        rows = []
        for r in pool.imap(score_one, jobs):
            rows.append(r)
            print(f"{r['set']} {r['name']:<22} n {r['n']} witness {str(r['witness']):<5} {r['status']:<14}"
                  f" {r['certify_s']:6.1f}s {r['reason']}", flush=True)
    float32_check(rows, ROOT / "corpus" / "field", G, {(s, nm): pr for s, nm, _, pr, _, _ in kernels()})
    for r in rows:
        r.pop("f32_points", None)
    out = ROOT / "data" / f"phase3-{'-'.join(sets)}{'-partial' if only else ''}.json"
    out.write_text(json.dumps({"seconds": time.time() - t0, "rows": rows}, indent=0))
    print("wrote", out, f"{time.time() - t0:.0f}s")
