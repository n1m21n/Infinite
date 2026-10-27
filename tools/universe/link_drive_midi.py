#!/usr/bin/env python3
"""Copy every MIDI file on a sample drive into the ingest inbox, one inbox folder per pack, with a SOURCE.txt.
    link_drive_midi.py [ROOT] [--inbox ~/Infinite/training-data/inbox] [--dry]
Read only on the drive. Pack = the first folder under ROOT; its name also gives midi_ingest its family guess
(house, garage, dnb, rave, ambient ...). The owner's own folder ('my music') is licence "owner"; packs are
"sample pack, local only". Held back (listed, not copied):
  Discovery Series Cuba  its MIDI drives Kontakt ensemble patterns: notes under 24 are pattern keys and the rest
                         a library key map shown only as pictures in the manual, so its notes are not GM drums.
Files are copied (a few MB), not linked, so ingest does not need the drive mounted."""
import argparse, collections, os, re, shutil

SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
HELD = {"Discovery Series Cuba": "Kontakt pattern keys + picture-only key map, not GM drums"}
MIDI = (".mid", ".midi", ".smf")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default="/Volumes/Wd-Black")
    ap.add_argument("--inbox", default="~/Infinite/training-data/inbox")
    ap.add_argument("--dry", action="store_true")
    a = ap.parse_args()
    inbox = os.path.expanduser(a.inbox)
    n, held = collections.Counter(), collections.Counter()
    for d, subs, fs in os.walk(a.root):
        subs[:] = sorted(s for s in subs if s not in SKIP and not s.startswith("."))
        for f in sorted(fs):
            if not f.lower().endswith(MIDI) or f.startswith("._"):
                continue
            pack = os.path.relpath(d, a.root).split(os.sep)[0]
            if pack in HELD:
                held[pack] += 1
                continue
            owner = pack.lower() == "my music"
            slug = "wd_" + re.sub(r"[^a-z0-9]+", "_", pack.lower()).strip("_")[:60]
            dst_dir = os.path.join(inbox, slug)
            rel = re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.relpath(os.path.join(d, f), os.path.join(a.root, pack)))[-150:]
            if not a.dry:
                os.makedirs(dst_dir, exist_ok=True)
                src_txt = os.path.join(dst_dir, "SOURCE.txt")
                if not os.path.exists(src_txt):
                    with open(src_txt, "w") as fo:
                        fo.write("license: %s\norigin: %s/%s\n" % ("owner" if owner else "sample pack, local only", a.root, pack))
                shutil.copyfile(os.path.join(d, f), os.path.join(dst_dir, rel))
            n[slug] += 1
    print("copied %d MIDI files into %d inbox folders" % (sum(n.values()), len(n)))
    for k, v in n.most_common():
        print("  %5d  %s" % (v, k))
    for k, v in held.items():
        print("  held %d from %s: %s" % (v, k, HELD[k]))


if __name__ == "__main__":
    main()
