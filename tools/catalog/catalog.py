#!/usr/bin/env python3
"""Shared catalog layer: one item schema, one renderer, one publish step for generated patches.

    catalog.py samples --lib <samples dir> [--max-mb 150]    -> website/samples/ data.json + media (wav / mp4 / poster / m4a for playback)
    catalog.py library --lib <samples dir> [--n 60]          -> website/assets/universe/ index.json + .inf + preview (the Library's curated slice)
    catalog.py tag                                           -> part / style / key on website/samples/data.json from each patch (the page's filters)

Samples are listening and downloading only: audio downloads as .wav, visual and audio+visual as .mp4. The patch is never offered there.
Patches live in the Library; only a stricter slice of the samples goes in, and each carries a link back to its sample.
render_media is the one renderer; tools/universe/library.py calls it. Existing authoring tables (tools/library, templates, embeds) stay as they are."""
import argparse, hashlib, json, os, re, shutil, subprocess, wave
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
KINDS = ("A", "V", "AV")


def codename(sid, taken=()):
    """Sixteen binary digits in four groups of four, fixed by the id; a clash re-hashes."""
    n = 0
    while True:
        h = hashlib.sha1((sid if not n else "%s#%d" % (sid, n)).encode()).digest()
        bits = "".join("01"[(h[i // 8] >> (i % 8)) & 1] for i in range(16))
        name = " ".join(bits[i:i + 4] for i in range(0, 16, 4))
        if name not in taken:
            return name
        n += 1


def run(cmd, timeout):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).returncode == 0
    except subprocess.TimeoutExpired:
        return False


FRAME = (1080, 1080)   # 1:1 square at 1080x1080
CLIP_SECS = 6


def render_clip(binary, inf, d, kind):
    """1:1 1080x1080 h264 clip: the Output node tapped at full size, re-encoded small, with the sample's sound muxed in for AV."""
    raw, mp4 = os.path.join(d, "raw.mp4"), os.path.join(d, "preview.mp4")
    if not run([binary, "--render", inf, raw, "--duration", str(CLIP_SECS), "--fps", "30"], 300):
        return False
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", raw]
    wav = os.path.join(d, "sample.wav")
    if kind == "AV" and os.path.exists(wav):
        cmd += ["-i", wav, "-map", "0:v", "-map", "1:a", "-c:a", "aac", "-b:a", "160k", "-t", str(CLIP_SECS)]
    cmd += ["-c:v", "libx264", "-preset", "slow", "-crf", "24", "-maxrate", "4M", "-bufsize", "8M", "-pix_fmt", "yuv420p", "-movflags", "+faststart", mp4]
    ok = run(cmd, 300)
    os.remove(raw)
    return ok and os.path.exists(mp4)


def render_media(binary, inf, kind, d, secs=8):
    """The one renderer: sample.wav for A/AV, poster.png + preview.mp4 for V/AV. Returns {"wav","png","mp4"} -> ok."""
    made = {}
    if kind in ("A", "AV"):
        made["wav"] = run([binary, "--audio-summary", inf, os.path.join(d, "audio.json"), "--duration", str(secs), "--wav", os.path.join(d, "sample.wav")], 120)
    if kind in ("V", "AV"):
        made["png"] = run([binary, "--frame", inf, "0", d + "/"], 180)
        for f in os.listdir(d):  # the engine names frames itself
            if f.endswith(".png") and f != "poster.png":
                os.replace(os.path.join(d, f), os.path.join(d, "poster.png"))
        made["mp4"] = render_clip(binary, inf, d, kind)
    aj = os.path.join(d, "audio.json")
    if os.path.exists(aj):
        try:
            made["metrics"] = json.load(open(aj))
        except ValueError:
            pass
        os.remove(aj)
    return made


def peaks(wav, n=120):
    w = wave.open(wav)
    d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
    if w.getnchannels() > 1:
        d = d.reshape(-1, w.getnchannels()).mean(axis=1)
    if not len(d):
        return [0.0] * n
    p = np.array([s.max() if len(s) else 0 for s in np.array_split(np.abs(d), n)])
    return [round(float(x), 3) for x in p / max(p.max(), 1e-6)]


def read_lib(lib):
    """Index rows whose folder is still there, ids made URL-safe."""
    out = []
    for l in open(os.path.join(lib, "index.jsonl")):
        m = json.loads(l)
        if os.path.isdir(os.path.join(lib, m["id"])) and m.get("kind") in KINDS:
            m["sid"] = m["id"].replace("/", "-")
            out.append(m)
    return out


def passes(m):
    """Samples gate (free zone, but not broken): no silent audio, no flat or blank picture."""
    a = m.get("audio") or {}
    if m["kind"] in ("A", "AV"):
        if a.get("silence_ratio", 0) > 0.6 or a.get("integrated_lufs", -99) < -45:
            return False
    if m["kind"] in ("V", "AV") and (m.get("interest") or 0) < 0.2:
        return False
    if m["kind"] in ("V", "AV") and not 14 <= (m.get("lum") or 90) <= 228:
        return False  # blown-out white or near-black picture (paper-white poster styles are legitimate, so the ceiling is high)
    return True


def curated(m):
    """Library gate: stricter. Audible, balanced, moving; pictures with real variety."""
    if not passes(m):
        return False
    a = m.get("audio") or {}
    if m["kind"] in ("A", "AV"):
        if not (-26 <= a.get("integrated_lufs", -99) <= -8) or a.get("silence_ratio", 0) > 0.25 or a.get("sample_peak_dbfs", 0) > -0.5:
            return False
    if m["kind"] in ("V", "AV") and (m.get("interest") or 0) < 0.45:
        return False
    return True


SAMPLE_KINDS = ("A",)   # the Samples page is sound only; pictures live in the Library


def publish_samples(lib, site, max_mb=150):
    media = os.path.join(site, "media")
    shutil.rmtree(media, ignore_errors=True)
    out, total, used = [], 0.0, set()
    for m in read_lib(lib):
        if not passes(m) or m["kind"] not in SAMPLE_KINDS:
            continue
        src = os.path.join(lib, m["id"])
        d = os.path.join(media, m["sid"])
        os.makedirs(d, exist_ok=True)
        s = {"id": m["sid"], "name": codename(m["sid"], used), "kind": m["kind"], "tags": m.get("tags", []), "nodes": len(m.get("nodes", [])),
             "family": (m.get("family") or "").replace("/", "-") or None, "peaks": [0.0] * 120, "audio": None, "video": None, "poster": None, "download": None}
        wav, mp4, png, inf = (os.path.join(src, f) for f in ("sample.wav", "preview.mp4", "poster.png", "patch.inf"))
        if m["kind"] == "A" and os.path.exists(wav):
            s["peaks"] = peaks(wav)
            shutil.copy(wav, os.path.join(d, "sample.wav"))
            subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "128000", wav, os.path.join(d, "sample.m4a")], check=True)
            s["audio"] = "media/%s/sample.m4a" % m["sid"]
            s["download"] = "media/%s/sample.wav" % m["sid"]
            if os.path.exists(inf):
                shutil.copy(inf, os.path.join(d, "patch.inf"))
                s["patch"] = "media/%s/patch.inf" % m["sid"]
        elif m["kind"] in ("V", "AV") and os.path.exists(mp4):
            shutil.copy(mp4, os.path.join(d, "sample.mp4"))
            s["video"] = s["download"] = "media/%s/sample.mp4" % m["sid"]
            if os.path.exists(png):
                subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", png, "-vf", "scale=720:-2", "-q:v", "4", os.path.join(d, "poster.jpg")], check=True)
                s["poster"] = "media/%s/poster.jpg" % m["sid"]
        else:
            shutil.rmtree(d); continue
        used.add(s["name"])
        s["curated"] = curated(m)
        out.append(s)
    # a 'cap' that keeps the page's media under max_mb, dropping from the end of the (stable) order
    for s in list(out):
        size = sum(os.path.getsize(os.path.join(site, p)) for p in (s["audio"], s["video"], s["poster"], s["download"], s.get("patch")) if p and os.path.exists(os.path.join(site, p))) / 1e6
        total += size
    while total > max_mb and out:
        s = out.pop()
        total -= sum(os.path.getsize(os.path.join(site, p)) for p in (s["audio"], s["video"], s["poster"], s["download"], s.get("patch")) if p and os.path.exists(os.path.join(site, p))) / 1e6
        shutil.rmtree(os.path.join(media, s["id"]), ignore_errors=True)
    json.dump({"samples": out}, open(os.path.join(site, "data.json"), "w"), separators=(",", ":"))
    print("samples: %d (%.0f MB) -> %s" % (len(out), total, site))
    tag_samples(site)
    return out


ROOTS = ("c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b")
# MusicTime::ScaleTable order (src/audio/MusicTime.h)
SCALES = ("major", "natural minor", "harmonic minor", "melodic minor", "dorian", "phrygian", "lydian", "mixolydian", "locrian",
          "major pentatonic", "minor pentatonic", "blues", "whole tone", "chromatic")
PIANO_VOICES = {"FM", "Partials"}
SYNTH_TYPES = {"Oscillator", "Wavetable", "Analog", "FM", "Partials", "String", "Wave Terrain", "Equation Synth", "Spectral Synth", "Metallic",
               "Sampler", "Slicer", "PaulStretch", "Molder", "Grain Molder", "Granular", "Field Synth"}


def describe(inf):
    """What a sample is, read from its patch: part (the Generate Samples node's parts: full, drums, percussion, synths, piano, fx),
    style (display name) and key ("a natural minor"; None for drums). The header's '# style:' line names the card."""
    text = open(inf, encoding="utf-8", errors="replace").read()
    types = set(re.findall(r"^node \d+ \S+ (.+)$", text, re.M))
    m = re.search(r"^# style: (\w+)(?:.*\bpart (\w+))?", text, re.M)
    style, part = (m.group(1), m.group(2)) if m else (None, None)
    fam = None
    if style:
        for st in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "styles_music.json")))["styles"]:
            if st["id"] == style:
                fam = st["family"]
    voices = types & SYNTH_TYPES
    if not part:
        drums = "Drum Sequencer" in types
        part = ("drums" if drums and not voices else "piano" if (fam == "piano" or (voices and voices <= PIANO_VOICES)) and not drums
                else "synths" if not drums else "full")
    t = re.search(r"^transport \S+ \d+ \d+ (\d+) (\d+)", text, re.M)
    key = None
    if t and part not in ("drums", "percussion"):
        key = "%s %s" % (ROOTS[int(t.group(1)) % 12], SCALES[int(t.group(2))] if int(t.group(2)) < len(SCALES) else "major")
    return {"part": part, "style": style.replace("_", " ") if style else None, "key": key}


def tag_samples(site):
    """Adds part / style / key to every audio item of <site>/data.json that has its patch (no media rewritten)."""
    path = os.path.join(site, "data.json")
    d = json.load(open(path))
    for s in d["samples"]:
        if s.get("patch") and os.path.exists(os.path.join(site, s["patch"])):
            s.update(describe(os.path.join(site, s["patch"])))
    json.dump(d, open(path, "w"), separators=(",", ":"))
    from collections import Counter
    print("tagged %d: %s" % (len(d["samples"]), dict(Counter(s.get("part") for s in d["samples"]))))


def _title(nodes):
    seen = []
    for n in nodes:
        b = n.split(":")[0]
        if b not in seen and b not in ("Audio Out", "Switcher"):
            seen.append(b)
    return " + ".join(seen[:3]) or "Generated patch"


def _wave_png(wav, dest):
    from PIL import Image, ImageDraw
    p = peaks(wav, 96)
    im = Image.new("RGB", (640, 360), (14, 20, 36))
    dr = ImageDraw.Draw(im)
    for i, v in enumerate(p):
        h = max(3, int(v * 240)); x = 20 + i * 6
        dr.rectangle([x, 180 - h // 2, x + 3, 180 + h // 2], fill=(255, 107, 90))
    im.save(dest, "JPEG", quality=85)


def publish_library(lib, site_assets, n=60, samples=None):
    """The Library's slice: curated samples as patches under assets/universe/, each linking to its sample page row."""
    shutil.rmtree(site_assets, ignore_errors=True)
    os.makedirs(site_assets)
    rows = [m for m in read_lib(lib) if curated(m) and not m.get("family")]  # one per family: the base patch
    live = {s["id"] for s in (samples or [])}
    if samples is not None:
        rows = [m for m in rows if m["kind"] not in SAMPLE_KINDS or m["sid"] in live]  # a sound patch only links to a sample that is actually published
    items = []
    for m in rows[:n]:
        src = os.path.join(lib, m["id"])
        gid = "gen-" + m["sid"].replace("-s", "-")
        body = open(os.path.join(src, "patch.inf")).read().replace(os.path.join(ROOT, "assets", "drumkits"), "/Applications/Infinite.app/Contents/Resources/drumkits")  # the engine re-resolves stale kit paths against the installed kit
        open(os.path.join(site_assets, gid + ".inf"), "w").write(body)
        prev = os.path.join(site_assets, gid + ".jpg")
        if os.path.exists(os.path.join(src, "poster.png")):
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", os.path.join(src, "poster.png"), "-vf", "scale=640:-2", "-q:v", "5", prev], check=True)
        else:
            _wave_png(os.path.join(src, "sample.wav"), prev)
        kind_word = {"A": "sound", "V": "picture", "AV": "sound and picture"}[m["kind"]]
        items.append({"id": gid, "name": _title(m["nodes"]), "category": "Generated", "kind": "Patch", "tags": [t for t in m.get("tags", []) if t not in ("code", "small", "dense")][:4],
                      "description": "A %s Infinite made by itself from %d nodes. Open it, then change anything." % (kind_word, len(m["nodes"])),
                      "price": 0, "preview": "assets/universe/%s.jpg" % gid, "file": "assets/universe/%s.inf" % gid,
                      "sample": ("samples/?s=%s" % m["sid"]) if m["kind"] == "A" else None, "nodes": m["nodes"][:12]})
    json.dump({"version": 1, "items": items}, open(os.path.join(site_assets, "index.json"), "w"), indent=1)
    print("library: %d patches -> %s" % (len(items), site_assets))
    return items


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["samples", "library", "all", "tag"])
    ap.add_argument("--lib", default=os.path.expanduser("~/Infinite/patch-universe-library/samples"))
    ap.add_argument("--n", type=int, default=60)
    ap.add_argument("--max-mb", type=float, default=150)
    a = ap.parse_args()
    W = os.path.join(ROOT, "website")
    s = None
    if a.cmd in ("samples", "all"):
        s = publish_samples(a.lib, os.path.join(W, "samples"), a.max_mb)
    if a.cmd in ("library", "all"):
        its = publish_library(a.lib, os.path.join(W, "assets", "universe"), a.n, s)
        if s is not None:  # the sample row links to its patch in the Library
            by = {i["sample"].split("=")[1]: i["id"] for i in its if i["sample"]}
            for x in s:
                x["library"] = by.get(x["id"])
                if not x.get("patch") and x.get("library"):
                    lib_inf = os.path.join(W, "assets", "universe", "%s.inf" % x["library"])
                    if os.path.exists(lib_inf):
                        dest_inf = os.path.join(W, "samples", "media", x["id"], "patch.inf")
                        shutil.copy(lib_inf, dest_inf)
                        x["patch"] = "media/%s/patch.inf" % x["id"]
            json.dump({"samples": s}, open(os.path.join(W, "samples", "data.json"), "w"), separators=(",", ":"))
    if a.cmd in ("all", "tag"):
        tag_samples(os.path.join(W, "samples"))


if __name__ == "__main__":
    main()
