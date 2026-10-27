#!/usr/bin/env python3
"""Find multisampled instruments on a drive: folders of audio files named by note (Piano_C3.wav, Cello A#2.aif, 060.wav).
    sample_scan.py [ROOT ...] [--out report.json] [--min-notes 8]
Walks every folder once (read only), reads each audio file's name for a note (C#3, Db4, A-1) or a MIDI number (21..108),
plus a velocity layer (v1, pp, ff, soft, hard) and a round-robin index (rr2), and prints the folders that cover enough
distinct notes to rebuild an instrument: notes covered, range, layers, formats, size, and the instrument name guessed
from the folder. Folders with sample-library formats (.nki, .sfz, .exs, .sf2) are listed too, since their samples
may sit in a sibling folder."""
import argparse, collections, json, os, re, sys, time

AUDIO = {".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".m4a", ".caf"}
LIBS = {".sfz", ".sf2", ".sf3", ".nki", ".nkm", ".exs", ".dspreset", ".adv", ".aupreset", ".multisample", ".zpr", ".ens"}
PC = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
# a note name not glued to other letters: C3, C#3, Db-1, A 4 ("A 4" only after a separator)
NOTE = re.compile(r"(?<![a-z])([a-g])(#|b|s|sharp)?[ _]?(-1|-2|[0-8])(?![0-9a-z])", re.I)
NUM = re.compile(r"(?<![0-9])(0?[2-9][0-9]|10[0-8])(?![0-9])")
VEL = re.compile(r"(?<![a-z])(v[0-9]{1,3}|vel[0-9]{1,3}|ppp|pp|p|mp|mf|f|ff|fff|soft|med|medium|hard|loud)(?![a-z])", re.I)
RR = re.compile(r"(?<![a-z])(rr|r)[ _-]?([0-9]{1,2})(?![0-9])", re.I)
DRUM = re.compile(r"kick|bassdrum|bd|snare|clap|hat|hh|cymbal|crash|ride|tom|perc|rim|shaker|conga|bongo|808|909", re.I)
SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}


def note_of(stem):
    """(MIDI note, named) of a file name (C3 = 60, the Ableton / Logic / Kontakt convention), or None;
    named is False for a bare number, which may be a note or just the file's index in a pack"""
    hits = list(NOTE.finditer(stem))
    if hits:
        m = hits[-1]  # the last match: names often start with a word like 'Bass' or 'Grand'
        acc = m[2] or ""
        off = 1 if acc.lower() in ("#", "s", "sharp") else -1 if acc == "b" else 0
        return 12 * (int(m[3]) + 2) + PC[m[1].lower()] + off, True
    nums = re.findall(r"(?<![0-9])[0-9]{1,3}(?![0-9])", stem)
    return (int(nums[-1]), False) if len(nums) == 1 and NUM.fullmatch(nums[-1]) else None


def is_index(audio):
    """True when a folder's bare numbers count files (Kick 001 ... Kick 120) instead of naming notes"""
    nums = sorted({int(x[-1]) for x in (re.findall(r"(?<![0-9])[0-9]{1,3}(?![0-9])", os.path.splitext(f)[0]) for f in audio) if x})
    return bool(nums) and nums[0] < 20


def scan(root, min_notes, budget):
    t0 = time.time()
    found, libs, kits, n_audio, n_dirs = [], [], [], 0, 0
    for d, subs, files in os.walk(root):
        subs[:] = [s for s in subs if s not in SKIP and not s.startswith(".")]
        n_dirs += 1
        if time.time() - t0 > budget:
            print("budget hit after %d folders" % n_dirs, file=sys.stderr)
            break
        audio = [f for f in files if os.path.splitext(f)[1].lower() in AUDIO and not f.startswith("._")]
        lib = sorted({os.path.splitext(f)[1].lower() for f in files if os.path.splitext(f)[1].lower() in LIBS})
        if lib:
            libs.append({"dir": d, "formats": lib, "n": sum(os.path.splitext(f)[1].lower() in LIBS for f in files)})
        n_audio += len(audio)
        if audio and DRUM.search(os.path.basename(d)) and not re.search(r"loop|fill|beat|bpm", os.path.basename(d), re.I):
            kits.append({"dir": d, "files": len(audio), "kind": DRUM.search(os.path.basename(d))[0].lower()})
        if len(audio) < min_notes:
            continue
        notes, vel, rr, fmt, size = collections.Counter(), set(), set(), collections.Counter(), 0
        index = is_index(audio)
        for f in audio:
            stem, ext = os.path.splitext(f)
            n = note_of(stem)
            if n is not None and (n[1] or not index):
                notes[n[0]] += 1
            v = VEL.findall(stem)
            if v:
                vel.add(v[-1].lower())
            r = RR.findall(stem)
            if r:
                rr.add(r[-1][1])
            fmt[ext.lower()] += 1
            try:
                size += os.path.getsize(os.path.join(d, f))
            except OSError:
                pass
        if len(notes) >= min_notes and sum(notes.values()) >= 0.6 * len(audio):
            found.append({"dir": d, "instrument": os.path.basename(d) or d, "files": len(audio),
                          "notes": len(notes), "lo": min(notes), "hi": max(notes), "layers": len(vel) or 1,
                          "rr": len(rr) or 1, "formats": dict(fmt), "mb": round(size / 1e6, 1)})
    return found, libs, kits, n_audio, n_dirs, time.time() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--out", default="sample_scan.json")
    ap.add_argument("--min-notes", type=int, default=8)
    ap.add_argument("--budget", type=float, default=900, help="seconds")
    a = ap.parse_args()
    allf, alll, allk = [], [], []
    for r in a.roots:
        f, l, k, na, nd, s = scan(r, a.min_notes, a.budget)
        allk += k
        print("%s: %d folders, %d audio files, %.0f s" % (r, nd, na, s))
        allf += f
        alll += l
    json.dump({"instruments": allf, "libraries": alll, "drum_one_shots": allk}, open(a.out, "w"), indent=1)
    name = lambda n: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12] + str(n // 12 - 2)
    print("\n%d multisampled folders (>= %d notes)" % (len(allf), a.min_notes))
    print("%-44s %5s %5s %-9s %3s %3s %8s  %s" % ("instrument (folder)", "files", "notes", "range", "vel", "rr", "MB", "path"))
    for x in sorted(allf, key=lambda x: -x["notes"])[:80]:
        print("%-44s %5d %5d %-9s %3d %3d %8.1f  %s" % (x["instrument"][:44], x["files"], x["notes"],
              "%s-%s" % (name(x["lo"]), name(x["hi"])), x["layers"], x["rr"], x["mb"], os.path.dirname(x["dir"])[-60:]))
    fm = collections.Counter(f for l in alll for f in l["formats"])
    print("\nsample-library files: %s in %d folders" % (dict(fm), len(alll)))
    dk = collections.Counter()
    for k in allk:
        dk[k["kind"]] += k["files"]
    print("drum one-shot folders: %d, files by kind %s" % (len(allk), dict(dk.most_common(12))))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
