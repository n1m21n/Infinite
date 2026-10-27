#!/usr/bin/env python3
"""What a drive holds, by kind of data, so nothing usable is missed.
    drive_inventory.py [ROOT] [--out drive_inventory.json] [--budget 900]
Walks the drive once (read only) and adds up files and GB by kind and by library (two folders under the root):
  audio      one-shots, loops, multisamples (.wav .aif .flac ...)
  midi       .mid files (grooves, songs, chord packs)
  projects   DAW sessions with arrangements and knob automation (.als .logicx .flp .cpr .ptx .rpp .song .bwproject)
  presets    synth and effect presets: knob values made by sound designers (.fxp .vstpreset .h2p .adv .adg .aupreset ...)
  sampler    instrument maps: which sample plays which note (.exs .sfz .sf2 .nki .multisample)
  grooves    groove templates (.agr .grv)
  video, docs, archives, apps, other
Archives (.zip .rar .7z) are listed because packs often sit unpacked inside them."""
import argparse, collections, json, os, sys, time

KIND = {}
for k, exts in {
        "audio": ".wav .aif .aiff .flac .ogg .mp3 .m4a .caf .rx2 .rex",
        "midi": ".mid .midi .smf .kar",
        "projects": ".als .flp .cpr .ptx .ptf .rpp .song .bwproject .logic .band .reason .npr",
        "presets": ".fxp .fxb .vstpreset .h2p .adv .adg .aupreset .nmsv .ksd .spf .serum .vital .fst .xpf .preset .patch "
                   ".syx .tfx .nksf .ableton .phaseplant .omnisphere .prt_omn .mse .mpr .fxpreset .ksp",
        "sampler": ".exs .sfz .sf2 .sf3 .nki .nkm .nkc .nkx .nkr .ncw .multisample .dspreset .xpm",
        "grooves": ".agr .grv .mxgrp",
        "video": ".mp4 .mov .mkv .avi .m4v .webm",
        "docs": ".pdf .txt .rtf .doc .docx .html .md .nfo",
        "archives": ".zip .rar .7z .tar .gz .dmg .iso .pkg",
        "apps": ".app .exe .vst .vst3 .component .dll .aaxplugin"}.items():
    for e in exts.split():
        KIND[e] = k
SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
BUNDLES = {".logicx", ".app", ".vst", ".vst3", ".component", ".aaxplugin", ".band", ".bundle"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default="/Volumes/Wd-Black")
    ap.add_argument("--out", default="drive_inventory.json")
    ap.add_argument("--budget", type=float, default=900)
    a = ap.parse_args()
    t0 = time.time()
    by_kind = collections.defaultdict(lambda: [0, 0])
    by_ext = collections.defaultdict(lambda: [0, 0])
    by_lib = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0]))
    top_ext = collections.defaultdict(collections.Counter)  # top-level folder -> extension -> files
    for d, subs, files in os.walk(a.root):
        if time.time() - t0 > a.budget:
            print("budget hit", file=sys.stderr)
            break
        rel = os.path.relpath(d, a.root).split(os.sep)
        lib = os.sep.join(rel[:2]) if rel[0] != "." else "(root)"
        keep = []
        for s in subs:
            ext = os.path.splitext(s)[1].lower()
            if s in SKIP or s.startswith("."):
                continue
            if ext in BUNDLES:  # a Logic project or plugin is a folder: count it as one file, don't walk inside
                k = "projects" if ext in (".logicx", ".band") else "apps"
                for c in (by_kind[k], by_ext[ext], by_lib[lib][k]):
                    c[0] += 1
                continue
            keep.append(s)
        subs[:] = keep
        for f in files:
            if f.startswith("._"):
                continue
            ext = os.path.splitext(f)[1].lower()
            k = KIND.get(ext, "other")
            try:
                sz = os.path.getsize(os.path.join(d, f))
            except OSError:
                sz = 0
            for c in (by_kind[k], by_ext[ext], by_lib[lib][k]):
                c[0] += 1
                c[1] += sz
            top_ext[rel[0] if rel[0] != "." else "(root)"][ext or "(none)"] += 1
    gb = lambda b: b / 1e9
    print("%s in %.0f s\n\n%-10s %9s %8s" % (a.root, time.time() - t0, "kind", "files", "GB"))
    for k, (n, b) in sorted(by_kind.items(), key=lambda kv: -kv[1][1]):
        print("%-10s %9d %8.1f" % (k, n, gb(b)))
    print("\ntop extensions:", ", ".join("%s %d (%.1f GB)" % (e or "(none)", n, gb(b))
                                         for e, (n, b) in sorted(by_ext.items(), key=lambda kv: -kv[1][0])[:30]))
    print("\n%-58s %7s %7s %6s %6s %6s %6s %6s" % ("library", "GB", "audio", "midi", "proj", "preset", "smplr", "arch"))
    tot = lambda L: sum(v[1] for v in L.values())
    for lib, L in sorted(by_lib.items(), key=lambda kv: -tot(kv[1]))[:70]:
        print("%-58s %7.1f %7d %6d %6d %6d %6d %6d" % (lib[-58:], gb(tot(L)), L["audio"][0], L["midi"][0], L["projects"][0],
                                                       L["presets"][0], L["sampler"][0], L["archives"][0]))
    print("\nevery top-level folder: files and its main extensions")
    for top, c in sorted(top_ext.items(), key=lambda kv: kv[0].lower()):
        print("  %-60s %7d  %s" % (top[:60], sum(c.values()), " ".join("%s %d" % kv for kv in c.most_common(6))))
    json.dump({"kind": by_kind, "ext": by_ext, "lib": by_lib, "top_ext": top_ext}, open(a.out, "w"), indent=1)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
