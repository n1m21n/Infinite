#!/usr/bin/env python3
"""Starter profile for new installs (PLAN-2026-10-30 item 22): the owner's learned profiles, features only.
    starter_profile.py [--src DIR] [--out resources/starter-profile]
Copies the derived profile files from this machine's AppSupport/Infinite/prediction (note, rhythm, velocity and quantize
styles, tempo, colour stats, Generate Samples taste): summaries of how the owner plays, never the raw movement or MIDI
logs. Writes manifest.json beside them. The app copies the folder into a fresh install's prediction folder once
(LearningLog::Start); Forget me deletes it and it is not seeded again."""
import argparse, datetime, hashlib, json, os, shutil

FILES = ["notes_style.bin", "rhythm_style.bin", "velocity_profile.bin", "quantize_profile.bin", "tempo_profile.txt",
         "color_stats.bin", "generate_taste.txt", "generate_taste_vec.txt"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.expanduser("~/Library/Application Support/Infinite/prediction"))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "resources", "starter-profile"))
    a = ap.parse_args()
    out = os.path.normpath(a.out); os.makedirs(out, exist_ok=True)
    man = {"built": datetime.date.today().isoformat(), "source": "owner's prediction profiles (features only)", "files": {}}
    for f in FILES:
        p = os.path.join(a.src, f)
        if not os.path.exists(p):
            print("missing", f); continue
        if f == "tempo_profile.txt":  # the bins only: its "file" lines name the owner's movement logs, done on this machine
            open(os.path.join(out, f), "w").write("".join(l for l in open(p) if l.startswith("bin ")))
        else:
            shutil.copyfile(p, os.path.join(out, f))
        p = os.path.join(out, f)
        man["files"][f] = {"bytes": os.path.getsize(p), "sha1": hashlib.sha1(open(p, "rb").read()).hexdigest()[:12]}
        print("%-24s %6d bytes" % (f, os.path.getsize(p)))
    json.dump(man, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("->", out)


if __name__ == "__main__":
    main()
