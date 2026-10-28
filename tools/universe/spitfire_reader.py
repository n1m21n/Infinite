#!/usr/bin/env python3
"""Spitfire Audio LABS (.spitfire + SQLite .db) & Kontakt .nkc catalogs -> audio features and multisample maps.
    spitfire_reader.py [ROOT ...] [--out-dir ~/Infinite/training-data/spitfire] [--per-container 24]
Reads (read only, zero audio leaves the drive):
  1. LABS .spitfire containers (21 libraries, 148 containers, 11,450 samples):
     Each entry's 40-byte metadata block at `moff` is unencrypted (format=1, bytes_per_sample, channels, sample_rate,
     total_frames). The audio stream at `aoff` starts with 'SFLC\\x00\\x00\\x01\\x00' + <HHI(bits, ch, sr) XORed with a
     16-byte repeating key, which immediately gives the exact 16-byte key per sample (`raw[:16] ^ expected_header`).
     Walking the decoded ABLK / DATA block headers (1024 frames = 21.3 ms per block at 48 kHz) yields 4 uncompressed
     24-bit signed PCM warm-up samples per channel and the 4th-order fixed-point LPC filter (a1..a4 / 256) per block,
     producing the 32-band spectrum, centroid, roll-off, flatness, peak, RMS, crest, stereo correlation, silence ratio
     and 10 ms attack/decay envelope in the same schema as audio_features.py.
  2. Spitfire / Kontakt .nkc catalogs (e.g. Spitfire Audio Olafur Arnalds Stratus):
     Reads UTF-16LE sample names embedded in .nkc index files to recover multisample note coverage, velocity layers,
     round-robin counts and articulations.
Licence: "sample library, local only"."""
import argparse, collections, glob, json, math, os, re, sqlite3, struct, sys, time
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import audio_features as AF
import sample_scan as SS

FS = 48000
FFT = 4096
_df = FS / FFT
_freqs = np.arange(FFT // 2 + 1) * _df
_w = 2 * np.pi * _freqs / FS
_E = np.exp(-1j * np.outer(np.arange(1, 5), _w))
_DAMP = np.array([0.985, 0.985 ** 2, 0.985 ** 3, 0.985 ** 4])


def labs_part(lib_name, stem):
    text = f"{lib_name} {stem}".replace("_", " ")
    for p, rx in AF.PARTS:
        if rx.search(text):
            return p
    if re.search(r"cello|strings|violin|viola", text, re.I):
        return "strings"
    if re.search(r"atmos|wandering|opiate|siren|foghorn", text, re.I):
        return "pad"
    if re.search(r"antique|obsolete|cassette", text, re.I):
        return "synth"
    return "keys"


def analyse_spitfire_entry(b, aoff, blen, moff, mlen, src, lib_name, articulation, part):
    if moff + 20 > len(b) or aoff + 64 > len(b) or blen < 64:
        return None
    fmt_id, bps, ch, sr, tot_frames = struct.unpack("<IIIII", b[moff:moff + 20])
    if bps not in (2, 3, 4) or ch not in (1, 2) or sr < 8000 or sr > 192000:
        return None
    exp16 = struct.pack("<4sIHHI", b"SFLC", 0x00010000, bps * 8, ch, sr)
    key16 = np.frombuffer(bytes(x ^ y for x, y in zip(b[aoff:aoff + 16], exp16)), dtype=np.uint8)
    raw = np.frombuffer(b, dtype=np.uint8, count=min(blen, len(b) - aoff), offset=aoff)
    hdr24 = bytes(raw[:24] ^ np.tile(key16, 2)[:24])
    if hdr24[:4] != b"SFLC":
        return None
    _, _, bits, ch_h, sr_h, blk_frames, n_frames = struct.unpack("<4sIHHIII", hdr24)
    blk_frames = blk_frames or 1024
    scale = float(1 << (bits - 1)) if bits in (16, 24, 32) else 8388608.0

    pos = 24
    l_blk, r_blk, lpc_rows, lpc_pwr = [], [], [], []
    max_blocks = int(30.0 * sr / blk_frames)
    n_blk = 0
    while pos + 56 <= len(raw) and n_blk < max_blocks:
        ab = bytes(raw[pos:pos + 12] ^ key16[np.arange(pos, pos + 12) % 16])
        if ab[:4] != b"ABLK":
            break
        ablk_sz, _ = struct.unpack("<II", ab[4:12])
        if ablk_sz < 16 or pos + 8 + ablk_sz > len(raw):
            break
        cp = pos + 12
        ch_samps = []
        for c in range(ch):
            if cp + 44 > len(raw):
                break
            dh = bytes(raw[cp:cp + 44] ^ key16[np.arange(cp, cp + 44) % 16])
            if dh[:4] != b"DATA":
                break
            dsz, _ = struct.unpack("<II", dh[4:12])
            vals = struct.unpack("<8i", dh[12:44])
            s4 = [v / scale for v in vals[4:]]
            ch_samps.append(s4)
            if c == 0 and any(vals[:4]):
                pw_b = sum(v * v for v in s4) * 0.25
                if pw_b > 1e-10:
                    lpc_rows.append(vals[:4])
                    lpc_pwr.append(pw_b)
            cp += 8 + dsz
        if ch_samps:
            l_blk.append(ch_samps[0])
            r_blk.append(ch_samps[1] if len(ch_samps) > 1 else ch_samps[0])
        pos += 8 + ablk_sz
        n_blk += 1

    if len(l_blk) < 4:
        return None
    L = np.array(l_blk, dtype=np.float64).reshape(-1)
    R = np.array(r_blk, dtype=np.float64).reshape(-1)
    peak = float(max(np.abs(L).max(), np.abs(R).max()))
    rms = float(np.sqrt(0.5 * ((L ** 2).mean() + (R ** 2).mean())))
    if peak < 1e-5:
        return None

    # Block RMS envelope (~21.3 ms per block, resampled to 10 ms steps for the first 40 points)
    blk_rms = np.sqrt(np.maximum((np.array(l_blk) ** 2).mean(1), (np.array(r_blk) ** 2).mean(1)) + 1e-20)
    silence = float((blk_rms ** 2 < 1e-6).mean())
    den = math.sqrt(float((L * L).sum()) * float((R * R).sum()))
    corr = float((L * R).sum()) / den if den > 0 else None

    if lpc_rows:
        A = np.array(lpc_rows, dtype=np.float64) / 256.0
        H2 = 1.0 / (np.abs(1.0 - (A * _DAMP) @ _E) ** 2 + 1e-6)
        H2[:, :2] = 0.0
        H2 /= H2.sum(1, keepdims=True) + 1e-30
        mean_pw = (H2 * np.array(lpc_pwr, dtype=np.float64)[:, None]).mean(0)
    else:
        return None

    bands = AF._BM @ mean_pw
    tot = float(bands.sum()) or 1e-30
    s = float(mean_pw.sum()) or 1e-30
    cum = np.cumsum(mean_pw)
    lin = np.sqrt(mean_pw[2:] + 1e-20)

    # Interpolate block_rms (blk_frames / sr sec per step) onto 10 ms grid
    dt_ms = 1000.0 * blk_frames / sr
    pk_idx = int(blk_rms.argmax())
    after = np.nonzero(blk_rms[pk_idx:] < blk_rms[pk_idx] * 0.1)[0]
    attack_ms = round(dt_ms * pk_idx)
    decay_ms = round(dt_ms * int(after[0])) if after.size else None
    t_10ms = np.arange(40) * 10.0 / dt_ms
    env40 = np.interp(t_10ms, np.arange(len(blk_rms)), blk_rms / max(1e-12, float(blk_rms[pk_idx])))

    return {
        "src": src,
        "seconds": round(tot_frames / sr, 2),
        "part": part,
        "bpm": None,
        "key": None,
        "loop": False,
        "pack": f"{lib_name}/{articulation}",
        "bands": [float("%.4g" % v) for v in bands],
        "peak": round(peak, 4),
        "rms": float("%.4g" % rms),
        "silence": round(silence, 3),
        "corr": round(corr, 3) if corr is not None and corr == corr else None,
        "onsets": 1,
        "crest": round(peak / max(1e-12, rms), 2),
        "low": round(float(bands[:11].sum()) / tot, 4),
        "mid": round(float(bands[11:22].sum()) / tot, 4),
        "high": round(float(bands[22:].sum()) / tot, 4),
        "centroid": round(float((_freqs * mean_pw).sum()) / s),
        "rolloff": round(float(_freqs[np.searchsorted(cum, 0.85 * s)])),
        "flatness": round(float(np.exp(np.log(lin).mean()) / lin.mean()), 4),
        "attack_ms": attack_ms,
        "decay_ms": decay_ms,
        "env": [round(float(v), 3) for v in env40],
        "license": "sample library, local only",
    }


def scan_labs(roots, out_path, per_container):
    t0 = time.time()
    n_containers, n_rows, by_part = 0, 0, collections.Counter()
    with open(out_path, "w") as fo:
        for root in roots:
            for lib_dir in sorted(glob.glob(os.path.join(root, "LABS*"))):
                if not os.path.isdir(lib_dir):
                    continue
                lib_name = os.path.basename(lib_dir)
                sdir = os.path.join(lib_dir, "Samples")
                if not os.path.isdir(sdir):
                    continue
                for sf in sorted(glob.glob(os.path.join(sdir, "*.spitfire"))):
                    if os.path.basename(sf).startswith("._"):
                        continue
                    stem = os.path.splitext(os.path.basename(sf))[0]
                    if stem.endswith("_IR_01") or "_IR_" in stem:
                        continue
                    try:
                        b = open(sf, "rb").read()
                    except OSError:
                        continue
                    if len(b) < 48 or b[:8] != b"Spitfire":
                        continue
                    _, _, count = struct.unpack("<8sII", b[:16])
                    if count <= 0:
                        continue
                    n_containers += 1
                    part = labs_part(lib_name, stem)
                    idxs = list(range(count))
                    if per_container and count > per_container:
                        idxs = [idxs[i * count // per_container] for i in range(per_container)]
                    for idx in idxs:
                        if 48 + idx * 32 > len(b):
                            break
                        fid, flg, blen, mlen, aoff, _, moff, _ = struct.unpack("<8I", b[16 + idx * 32:48 + idx * 32])
                        row = analyse_spitfire_entry(
                            b, aoff, blen, moff, mlen,
                            f"{sf}#id={fid}", lib_name, stem, part
                        )
                        if row:
                            fo.write(json.dumps(row, separators=(",", ":")) + "\n")
                            n_rows += 1
                            by_part[part] += 1
    print(f"LABS: {n_containers} .spitfire containers -> {n_rows} audio feature rows in {time.time() - t0:.1f}s")
    print("  by part:", dict(by_part.most_common()))
    return n_rows


def scan_nkc(roots, out_path):
    t0 = time.time()
    n_files, n_inst = 0, 0
    target_dirs = []
    for r in roots:
        if os.path.basename(r.rstrip("/")).lower() == "wd-black":
            for entry in sorted(os.listdir(r)):
                if entry.startswith("._"):
                    continue
                low = entry.lower()
                if any(w in low for w in ("spitfire", "kontakt", "noire", "exhale", "omnia", "cuba", "saffron", "sequis", "sunburst")):
                    target_dirs.append((r, os.path.join(r, entry)))
        else:
            target_dirs.append((r, r))
    with open(out_path, "w") as fo:
        for root, tdir in target_dirs:
            for d, subs, files in os.walk(tdir):
                subs[:] = sorted(s for s in subs if s not in SS.SKIP and not s.startswith("."))
                for f in sorted(files):
                    if f.startswith("._") or not f.lower().endswith(".nkc"):
                        continue
                    p = os.path.join(d, f)
                    n_files += 1
                    try:
                        raw = open(p, "rb").read()
                    except OSError:
                        continue
                    names = [
                        s.decode("utf-16le", errors="ignore")
                        for s in re.findall(b"(?:[ -~]\\x00){5,}", raw)
                    ]
                    audio = [x for x in names if os.path.splitext(x)[1].lower() in (".ncw", ".wav", ".aif", ".aiff")]
                    if len(audio) < 4:
                        continue
                    notes, vel, rr = collections.Counter(), set(), set()
                    index = SS.is_index(audio)
                    for a in audio:
                        stem = os.path.splitext(a)[0]
                        nt = SS.note_of(stem)
                        if nt is not None and (nt[1] or not index):
                            notes[nt[0]] += 1
                        v = SS.VEL.findall(stem)
                        if v:
                            vel.add(v[-1].lower())
                        r = SS.RR.findall(stem)
                        if r:
                            rr.add(r[-1][1])
                    if not notes:
                        continue
                    rel = os.path.relpath(p, root).split(os.sep)
                    row = {
                        "src": p,
                        "library": rel[0],
                        "instrument": os.path.splitext(f)[0],
                        "files": len(audio),
                        "notes": len(notes),
                        "lo": min(notes),
                        "hi": max(notes),
                        "layers": len(vel) or 1,
                        "rr": len(rr) or 1,
                        "sample_names": audio[:12],
                        "license": "sample library, local only",
                    }
                    fo.write(json.dumps(row, separators=(",", ":")) + "\n")
                    n_inst += 1
    print(f"NKC catalogs: {n_files} .nkc files scanned -> {n_inst} multisample instruments in {time.time() - t0:.1f}s")
    return n_inst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/WD-Black"])
    ap.add_argument("--out-dir", default="~/Infinite/training-data/spitfire")
    ap.add_argument("--per-container", type=int, default=24, help="0 = every sample in each .spitfire container")
    ap.add_argument("--skip-nkc", action="store_true")
    a = ap.parse_args()
    out_dir = os.path.expanduser(a.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    labs_out = os.path.join(out_dir, "labs_audio.jsonl")
    scan_labs(a.roots, labs_out, a.per_container)
    if not a.skip_nkc:
        nkc_out = os.path.join(out_dir, "nkc_catalog.jsonl")
        scan_nkc(a.roots, nkc_out)


if __name__ == "__main__":
    main()
