#!/usr/bin/env python3
"""Spectrasonics STEAM (.db / .db2 / .omnisphere) deep extractor:
    steam_reader.py [ROOT ...] [--out-dir ~/Infinite/training-data/steam] [--wavs-per-db 12]
Reads (read only, zero audio leaves the drive):
  1. Wavetables (Wavetables/~BundleArchives.db & .db2):
     1,892 raw IEEE-754 float32 wavetables (.stmwf) and .wfb morph bundles -> 32-band spectral fingerprint,
     centroid, roll-off, flatness, crest, peak and RMS.
  2. Arpeggiator Grooves, Modulation Matrices & .omnisphere Expansion Packs:
     Walks all .db and .omnisphere containers for .prt_omn / .mlt_omn XML patches:
       - Extracts active <ARPSEQ2> / <SLICESEQSTEP> step sequences (tempo, meter, swing, step begin/end, gate,
         velocity, slice/pitch index) and active <MOD_MATRIX> routings (source -> target, lo, hi, depth).
       - Extracts full preset knob rows from third-party .omnisphere packages (which preset_reader.py skips).
  3. Soundsources (Soundsources/**/*.db & .omnisphere):
     - Parses all .zmap multisample instruments (Timbre, Tonality, Type, Complexity, Specs, Keywords, zones) and
       Default Layer.xml velocity / round-robin / BaseNote mappings.
     - Decodes embedded Soundsource .wav streams in-memory (standard 16/24-bit PCM with `data` XORed by the 4-byte
       constant `7d b5 7e 21` and `nChannels` offset by +20 in `fmt `) and runs audio_features.py's 32-band spectrum,
       timbre and 10 ms attack/decay envelope analysis.
Licence: "sample library, local only"."""
import argparse, collections, glob, html, json, math, os, re, struct, subprocess, sys, time
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import audio_features as AF
import preset_reader as PR

XOR4_WAV = np.array([0x7d, 0xb5, 0x7e, 0x21], dtype=np.uint8)
XOR4_SPCA = np.array([0xbc, 0xef, 0x42, 0x12], dtype=np.uint8)
FS = 48000
FFT, HOP = 4096, 1024
_win = AF._win
_scale = AF._scale
_freqs = AF._freqs
_BM = AF._BM


def iter_fs_header(f):
    """Reads only the <FileSystem> XML header from an open file `f` and yields (stack, name, off, sz, base)."""
    f.seek(0)
    head = f.read(64)
    if not head.startswith(b"<FileSystem>"):
        return
    f.seek(0)
    chunks = []
    found = -1
    while found < 0:
        c = f.read(1024 * 1024)
        if not c:
            return
        chunks.append(c)
        if b"</FileSystem>" in c or (len(chunks) > 1 and b"</FileSystem>" in chunks[-2][-32:] + c[:32]):
            b = b"".join(chunks)
            found = b.find(b"</FileSystem>")
            break
        if len(chunks) > 32:
            return
    base = found + len(b"</FileSystem>")
    if base < len(b) and b[base:base + 1] == b"\n":
        base += 1
    stack = []
    for m in re.finditer(
        r"<DIR name=\"([^\"]*)\">|</DIR>|<FILE name=\"([^\"]*)\" offset=\"(\d+)\" size=\"(\d+)\"/>",
        b[:found].decode("latin-1", errors="ignore"),
    ):
        if m[0].startswith("<DIR"):
            stack.append(html.unescape(m[1]))
        elif m[0] == "</DIR>":
            if stack:
                stack.pop()
        else:
            yield tuple(stack), html.unescape(m[2]), int(m[3]), int(m[4]), base


def analyse_pcm_array(x, sr, src, pack, part_hint=None):
    """Run audio_features.py's exact 32-band / timbre / envelope analysis on an in-memory (2, N) float64 array."""
    if x is None or x.shape[1] < 2048:
        return None
    L, R = x
    n = x.shape[1]
    peak = float(np.abs(x).max())
    rms = float(np.sqrt((x ** 2).mean()))
    if peak < 1e-5:
        return None
    blk = max(1, sr // 10)
    nb = max(1, n // blk)
    sub = (
        np.maximum((L[:nb * blk].reshape(nb, -1) ** 2).mean(1), (R[:nb * blk].reshape(nb, -1) ** 2).mean(1))
        if n >= blk
        else np.array([max((L ** 2).mean(), (R ** 2).mean())])
    )
    silence = float((sub < 1e-6).mean())
    den = math.sqrt(float((L * L).sum()) * float((R * R).sum()))
    corr = float((L * R).sum()) / den if den > 0 else float("nan")

    pad = x if n >= FFT else np.pad(x, ((0, 0), (0, FFT - n)))
    starts = np.arange(0, pad.shape[1] - FFT + 1, HOP)
    if len(starts) > 256:
        starts = starts[np.linspace(0, len(starts) - 1, 256, dtype=int)]
    fr = np.stack([pad[:, s:s + FFT] for s in starts], 1) * _win
    spec = np.fft.rfft(fr, axis=2)
    pw = (0.5 * (spec.real ** 2 + spec.imag ** 2)).sum(0) * _scale
    bands = _BM @ pw.mean(0)
    tot = float(bands.sum()) or 1e-30
    mean_pw = pw.mean(0)
    s = float(mean_pw.sum()) or 1e-30
    cum = np.cumsum(mean_pw)
    lin = np.sqrt(mean_pw[1:] + 1e-20)

    words = f"{pack} {src}".replace("_", " ")
    part = part_hint or next((p for p, rx in AF.PARTS if rx.search(words)), "synth")
    row = {
        "src": src,
        "seconds": round(n / sr, 2),
        "part": part,
        "bpm": None,
        "key": None,
        "loop": False,
        "pack": pack,
        "bands": [float("%.4g" % b) for b in bands],
        "peak": round(peak, 4),
        "rms": float("%.4g" % rms),
        "silence": round(silence, 3),
        "corr": round(corr, 3) if corr == corr else None,
        "onsets": 1,
        "crest": round(peak / max(1e-12, rms), 2),
        "low": round(float(bands[:11].sum()) / tot, 4),
        "mid": round(float(bands[11:22].sum()) / tot, 4),
        "high": round(float(bands[22:].sum()) / tot, 4),
        "centroid": round(float((_freqs * mean_pw).sum()) / s),
        "rolloff": round(float(_freqs[np.searchsorted(cum, 0.85 * s)])),
        "flatness": round(float(np.exp(np.log(lin).mean()) / lin.mean()), 4),
        "license": "sample library, local only",
    }
    step_10ms = max(1, sr // 100)
    if n >= step_10ms:
        e = np.sqrt(
            np.maximum(L ** 2, R ** 2)[:n // step_10ms * step_10ms].reshape(-1, step_10ms).mean(1) + 1e-20
        )
        if e.size:
            pk = int(e.argmax())
            after = np.nonzero(e[pk:] < e[pk] * 0.1)[0]
            row.update(
                attack_ms=10 * pk,
                decay_ms=10 * int(after[0]) if after.size else None,
                env=[round(float(v / e[pk]), 3) for v in e[:40]],
            )
    return row


def decode_steam_wav(w_bytes, max_seconds=4.0):
    if len(w_bytes) < 64:
        return None, None
    if w_bytes[:4] == b"SpCA":
        pos = 4
        while pos + 4 <= len(w_bytes):
            hdr = w_bytes[pos]
            last = hdr >> 7
            blen = int.from_bytes(w_bytes[pos + 1:pos + 4], "big")
            pos += 4 + blen
            if last:
                break
        if pos + 64 > len(w_bytes):
            return None, None
        rem = (len(w_bytes) - (pos + 4)) // 4 * 4
        if rem < 64:
            return None, None
        tail = np.frombuffer(w_bytes, dtype=np.uint8, count=rem, offset=pos + 4) ^ np.tile(XOR4_SPCA, rem // 4)
        flac = b"fLaC" + w_bytes[4:pos + 4] + tail.tobytes()
        p = subprocess.run(
            ["ffmpeg", "-v", "error", "-nostdin", "-f", "flac", "-i", "pipe:0", "-t", str(max_seconds), "-f", "f32le", "-ac", "2", "-ar", str(FS), "pipe:1"],
            input=flac,
            capture_output=True,
            timeout=15,
        )
        x = np.frombuffer(p.stdout, dtype=np.float32).astype(np.float64)
        if x.size < 4096:
            return None, None
        return x[:x.size // 2 * 2].reshape(-1, 2).T, FS
    if w_bytes[:4] != b"RIFF":
        return None, None
    pos = 12
    fmt = None
    data_off = data_sz = None
    while pos + 8 <= len(w_bytes):
        tag = w_bytes[pos:pos + 4]
        sz = struct.unpack("<I", w_bytes[pos + 4:pos + 8])[0]
        if tag == b"fmt " and sz >= 16:
            fmt = struct.unpack("<HHIIHH", w_bytes[pos + 8:pos + 24])
        elif tag == b"data":
            data_off = pos + 8
            data_sz = min(sz, len(w_bytes) - data_off)
            break
        pos += 8 + (sz + (sz & 1))
    if not fmt or data_off is None or data_sz <= 0:
        return None, None
    _, _, sr, _, block_align, bits = fmt
    bps = bits // 8
    ch = block_align // bps if bps else 2
    if ch not in (1, 2) or bps not in (2, 3, 4) or sr < 8000 or sr > 192000:
        return None, None
    max_bytes = int(max_seconds * sr) * block_align
    n_bytes = min(data_sz, max_bytes) // (block_align * 4) * (block_align * 4)
    if n_bytes < block_align * 64:
        return None, None
    raw = np.frombuffer(w_bytes, dtype=np.uint8, count=n_bytes, offset=data_off)
    dec = raw ^ np.tile(XOR4_WAV, n_bytes // 4)
    if bps == 2:
        pcm = dec.view(np.int16).astype(np.float64) / 32768.0
    elif bps == 3:
        tri = dec.reshape(-1, 3)
        val = (
            tri[:, 0].astype(np.int32)
            | (tri[:, 1].astype(np.int32) << 8)
            | (tri[:, 2].astype(np.int8).astype(np.int32) << 16)
        )
        pcm = val.astype(np.float64) / 8388608.0
    else:
        pcm = dec.view(np.int32).astype(np.float64) / 2147483648.0
    pcm = pcm.reshape(-1, ch).T
    if ch == 1:
        pcm = np.vstack([pcm, pcm])
    return pcm, sr


def extract_wavetables(roots, out_path):
    t0 = time.time()
    n_wf = 0
    with open(out_path, "w") as fo:
        for root in roots:
            for d, subs, files in os.walk(root):
                subs[:] = sorted(s for s in subs if s not in PR.SKIP and not s.startswith("."))
                for f in sorted(files):
                    if f.startswith("._") or not f.lower().startswith("~bundlearchives.db"):
                        continue
                    p = os.path.join(d, f)
                    try:
                        with open(p, "rb") as fh:
                            entries = list(iter_fs_header(fh))
                            for stack, name, off, sz, base in entries:
                                if not name.lower().endswith(".stmwf") or sz < 16384:
                                    continue
                                n_floats = min(sz // 4, 16384)
                                fh.seek(base + off)
                                raw_wf = fh.read(n_floats * 4)
                                if len(raw_wf) < 16384:
                                    continue
                                wf = np.frombuffer(raw_wf, dtype=np.float32).astype(np.float64)
                                if not np.all(np.isfinite(wf)):
                                    continue
                                row = analyse_pcm_array(
                                    np.vstack([wf, wf]),
                                    FS,
                                    f"{p}#{'/'.join(stack)}/{name}",
                                    f"Omnisphere/Wavetables/{'/'.join(stack[:2])}",
                                    part_hint="synth",
                                )
                                if row:
                                    row["wavetable"] = os.path.splitext(name)[0]
                                    row["category"] = "/".join(stack)
                                    fo.write(json.dumps(row, separators=(",", ":")) + "\n")
                                    n_wf += 1
                    except OSError:
                        continue
    print(f"Wavetables: {n_wf} .stmwf tables analysed in {time.time() - t0:.1f}s -> {out_path}")
    return n_wf


def parse_arp_and_mod(text):
    """Extract active <ARPSEQ2>/<SLICESEQSTEP> pattern and active <MOD_MATRIX> routings from a .prt_omn XML."""
    arp = None
    m_arp = re.search(r"<ARP\b([^>]*)>", text)
    m_seq = re.search(r"<ARPSEQ2\b([^>]*)>(.*?)</ARPSEQ2>", text, re.S)
    if m_seq:
        arp_a = dict(PR.ATTR.findall(m_arp[1])) if m_arp else {}
        seq_a = dict(PR.ATTR.findall(m_seq[1]))
        tpq = int(seq_a.get("TICKSPERQUARTER", "600") or 600)
        step_ticks = max(1, tpq // 4)
        steps = []
        for sm in re.finditer(r"<SLICESEQSTEP\b([^>]*)>", m_seq[2]):
            sa = dict(PR.ATTR.findall(sm[1]))
            beg = int(sa.get("BEGIN", "0") or 0)
            end = int(sa.get("END", "0") or 0)
            vel = int(sa.get("VEL", "0") or 0)
            slc = int(sa.get("SLICEINDEX", "60") or 60)
            if vel > 0 and end > beg:
                steps.append({
                    "step": round(beg / step_ticks, 3),
                    "gate": round((end - beg) / step_ticks, 3),
                    "vel": round(vel / 127.0, 3),
                    "slice": slc,
                    "transpose": int(sa.get("TRANSPOSE", "0") or 0),
                })
        if steps:
            arp = {
                "bpm": PR.hexf(seq_a.get("TEMPO", "")) or 120.0,
                "meter": f"{seq_a.get('TIMESIGNUM', '4')}/{seq_a.get('TIMESIGDENOM', '4')}",
                "swing": PR.hexf(arp_a.get("ArpSwing", "0")) or 0.0,
                "mode": int(arp_a.get("ArpMode", "0") or 0),
                "clock": int(arp_a.get("ArpClock", "0") or 0),
                "length_val": PR.hexf(arp_a.get("ArpLen", "0")),
                "steps": steps[:64],
            }

    mods = []
    for mm in re.finditer(r"<MOD_MATRIX\b([^>]*)>", text):
        ma = dict(PR.ATTR.findall(mm[1]))
        for idx in range(24):
            src = ma.get(f"source{idx}", "off")
            tgt = ma.get(f"target{idx}", "off")
            mute = ma.get(f"mute{idx}", "0")
            if src != "off" and tgt != "off" and mute == "0":
                mods.append({
                    "src": src,
                    "target": tgt,
                    "lo": PR.hexf(ma.get(f"lo{idx}", "0")) or 0.0,
                    "hi": PR.hexf(ma.get(f"hi{idx}", "0")) or 0.0,
                    "val": PR.hexf(ma.get(f"defV{idx}", "0")) or 0.0,
                })
    return arp, mods


def extract_arps_and_omnisphere_packs(roots, arp_out, pack_presets_out):
    t0 = time.time()
    n_arps, n_mods, n_pack_presets = 0, 0, 0
    seen = set()
    with open(arp_out, "w") as fa, open(pack_presets_out, "w") as fp:
        for root in roots:
            for d, subs, files in os.walk(root):
                subs[:] = sorted(s for s in subs if s not in PR.SKIP and not s.startswith("."))
                for f in sorted(files):
                    if f.startswith("._"):
                        continue
                    low = f.lower()
                    is_omn_pkg = low.endswith(".omnisphere")
                    is_patch_db = low.endswith(".db") and ("/Patches/" in os.path.join(d, f) or "/Multis/" in os.path.join(d, f))
                    if not (is_omn_pkg or is_patch_db):
                        continue
                    p = os.path.join(d, f)
                    engine = "keyscape" if "keyscape" in p.lower() else "trilian" if "trilian" in p.lower() else "omnisphere"
                    try:
                        with open(p, "rb") as fh:
                            entries = [
                                (stack, name, off, sz, base)
                                for stack, name, off, sz, base in iter_fs_header(fh)
                                if name.lower().endswith(PR.SPECTRA)
                            ]
                            if not entries:
                                continue
                            # Read contiguous patch span in one sequential read if <= 250 MB
                            end_byte = max(base + off + sz for _, _, off, sz, base in entries)
                            if end_byte <= 260 * 1024 * 1024:
                                fh.seek(0)
                                buf = fh.read(end_byte)
                            else:
                                buf = None
                            for stack, name, off, sz, base in entries:
                                if buf is not None:
                                    text = buf[base + off:base + off + sz].decode("latin-1", errors="ignore") if len(buf) >= base + off + sz else ""
                                else:
                                    fh.seek(base + off)
                                    text = fh.read(sz).decode("latin-1", errors="ignore")
                                patch_name = os.path.splitext(name)[0]
                                cat = "/".join(stack)
                                lib_name = os.path.splitext(f)[0]
                                key = (engine, lib_name, patch_name)
                                if key in seen:
                                    continue
                                seen.add(key)
                                src_id = f"{p}#{cat}/{patch_name}"
                                patch_row = PR.spectra_patch(text, src_id, engine, lib_name, cat, patch_name)
                                if is_omn_pkg and patch_row.get("modules"):
                                    fp.write(json.dumps(patch_row, separators=(",", ":")) + "\n")
                                    n_pack_presets += 1
                                arp, mods = parse_arp_and_mod(text)
                                if arp or mods:
                                    row = {
                                        "src": src_id,
                                        "engine": engine,
                                        "library": patch_row.get("library", lib_name),
                                        "category": cat,
                                        "name": patch_row.get("name", patch_name),
                                        "tags": patch_row.get("tags", {}),
                                        "arp": arp,
                                        "mod_matrix": mods,
                                        "license": "preset library, local only",
                                    }
                                    fa.write(json.dumps(row, separators=(",", ":")) + "\n")
                                    n_arps += bool(arp)
                                    n_mods += bool(mods)
                    except OSError:
                        continue
    print(
        f"Patches & .omnisphere packs: {n_arps} ARP grooves, {n_mods} mod-matrices, "
        f"{n_pack_presets} .omnisphere pack presets in {time.time() - t0:.1f}s"
    )
    return n_arps, n_mods, n_pack_presets


def extract_soundsources(roots, zmap_out, audio_out, wavs_per_db):
    t0 = time.time()
    n_zmap, n_audio = 0, 0
    with open(zmap_out, "w") as fz, open(audio_out, "w") as fa:
        for root in roots:
            for d, subs, files in os.walk(root):
                subs[:] = sorted(s for s in subs if s not in PR.SKIP and not s.startswith("."))
                for f in sorted(files):
                    if f.startswith("._"):
                        continue
                    low = f.lower()
                    p = os.path.join(d, f)
                    if low == "zmap.index":
                        try:
                            xml_t = open(p, encoding="latin-1", errors="ignore").read()
                        except OSError:
                            continue
                        for em in re.finditer(r"<ENTRY\s+NAME=\"([^\"]*)\"\s+PATH=\"([^\"]*)\"\s*>(.*?)</ENTRY>", xml_t, re.S):
                            ename, epath, ebody = html.unescape(em[1]), html.unescape(em[2]), em[3]
                            tags = collections.defaultdict(list)
                            for am in re.finditer(r"<ATTR\s+NAME=\"([^\"]*)\"\s+VALUE=\"([^\"]*)\"", ebody):
                                k = am[1].strip().lower().replace(" ", "_")
                                tags[k].append(html.unescape(am[2].strip()))
                            tags = {k: (v if len(v) > 1 or k in ("mood", "timbre", "type", "genre") else v[0]) for k, v in tags.items()}
                            row = {
                                "src": f"{p}#{epath}/{ename}",
                                "library": os.path.basename(d),
                                "path": epath,
                                "name": ename,
                                "tags": tags,
                                "license": "sample library, local only",
                            }
                            fz.write(json.dumps(row, separators=(",", ":")) + "\n")
                            n_zmap += 1
                    elif (low.endswith(".db") and "/Soundsources/" in p) or low.endswith(".omnisphere"):
                        if wavs_per_db <= 0:
                            continue
                        try:
                            with open(p, "rb") as fh:
                                entries = list(iter_fs_header(fh))
                                wav_entries = [
                                    (stack, name, off, sz, base)
                                    for stack, name, off, sz, base in entries
                                    if name.lower().endswith(".wav") and sz >= 8192
                                ]
                                if len(wav_entries) > wavs_per_db:
                                    wav_entries = [wav_entries[i * len(wav_entries) // wavs_per_db] for i in range(wavs_per_db)]
                                pack = f"Spectrasonics/{os.path.basename(os.path.dirname(p))}"
                                for stack, name, off, sz, base in wav_entries:
                                    fh.seek(base + off)
                                    w_bytes = fh.read(min(sz, 768 * 1024))
                                    pcm, sr = decode_steam_wav(w_bytes, max_seconds=4.0)
                                    if pcm is None:
                                        continue
                                    row = analyse_pcm_array(pcm, sr, f"{p}#{'/'.join(stack)}/{name}", pack)
                                    if row:
                                        fa.write(json.dumps(row, separators=(",", ":")) + "\n")
                                        n_audio += 1
                        except OSError:
                            continue
    print(f"Soundsources: {n_zmap} zmap.index instruments, {n_audio} decoded audio feature rows in {time.time() - t0:.1f}s")
    return n_zmap, n_audio


def expand_steam_roots(roots):
    out = []
    for r in roots:
        if os.path.basename(r.rstrip("/")).lower() == "wd-black":
            for entry in sorted(os.listdir(r)):
                if entry.startswith("._"):
                    continue
                low = entry.lower()
                if any(w in low for w in ("spectrasonics", "omnisphere", "omisphere", "keyscape", "trilian", "sonic extensions")):
                    out.append(os.path.join(r, entry))
        else:
            out.append(r)
    return out or roots


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/WD-Black"])
    ap.add_argument("--out-dir", default="~/Infinite/training-data/steam")
    ap.add_argument("--wavs-per-db", type=int, default=8, help="0 = every embedded .wav in every Soundsource .db")
    ap.add_argument("--only-soundsources", action="store_true")
    a = ap.parse_args()
    roots = expand_steam_roots(a.roots)
    out_dir = os.path.expanduser(a.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    if not a.only_soundsources:
        extract_wavetables(roots, os.path.join(out_dir, "wavetables.jsonl"))
        extract_arps_and_omnisphere_packs(
            roots,
            os.path.join(out_dir, "arp_and_mods.jsonl"),
            os.path.join(out_dir, "omnisphere_packs.jsonl"),
        )
    extract_soundsources(
        roots,
        os.path.join(out_dir, "soundsources.jsonl"),
        os.path.join(out_dir, "soundsource_audio.jsonl"),
        a.wavs_per_db,
    )


if __name__ == "__main__":
    main()
