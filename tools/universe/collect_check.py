#!/usr/bin/env python3
"""Proof that a sample drive has been collected: every top-level folder, every music file type in it, which tool
reads that type, how many rows the tool wrote from that folder, and a status.
    collect_check.py [--inventory drive_inventory.json] [--root /Volumes/Wd-Black] [--out collect_check.md]
Inputs (all already written by the tools, nothing on the drive is read again):
  drive_inventory.py  files per extension per top-level folder
  audio_features.py   training-data/audio/features.jsonl
  preset_reader.py    training-data/presets/presets.jsonl, alchemy.jsonl
  exs_reader.py       training-data/samplers/exs.jsonl
  als_reader.py       training-data/arcs/als.jsonl and inbox/als_*
  wt_reader.py        training-data/wavetables/wt.jsonl
  link_drive_midi.py  inbox/wd_* -> midi_ingest.py rows in store/inbox.jsonl ("inbox:wd_<pack>/...")
Status per extension: captured (a tool wrote rows), sampled (audio: up to --per-folder files a folder), locked
(encrypted / proprietary sampler formats), incomplete (unfinished downloads), not parsed (known, no reader yet),
not music. A folder is 'done' when nothing in it is 'missed' (a readable type with no rows)."""
import argparse, collections, json, os, re, subprocess

TD = os.path.expanduser("~/Infinite/training-data")
AUDIO = {".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".m4a", ".caf"}
TOOL = {**{e: "audio_features" for e in AUDIO}, ".mid": "midi_ingest", ".midi": "midi_ingest", ".als": "als_reader",
        ".exs": "exs_reader", ".wt": "wt_reader", ".h2p": "preset_reader", ".adv": "preset_reader", ".adg": "preset_reader",
        ".pst": "preset_reader", ".acp": "preset_reader", ".db": "preset_reader", ".prt_omn": "preset_reader",
        ".mlt_omn": "preset_reader", ".mlt_key": "preset_reader", ".prt_key": "preset_reader", ".prt_trl": "preset_reader",
        ".mlt_trl": "preset_reader"}
LOCKED = {".spitfire": "Spitfire LABS, encrypted", ".zmulti": "Spitfire LABS", ".zpreset": "Spitfire LABS",
          ".zconfig": "Spitfire LABS", ".ch1": "NI Play Series, encrypted", ".nki": "Kontakt", ".nkx": "Kontakt, encrypted",
          ".nkc": "Kontakt", ".nkr": "Kontakt", ".ncw": "Kontakt, compressed",
          ".nicnt": "Kontakt", ".xpak": "XLN Addictive Drums, encrypted", ".obw": "Toontrack EZkeys",
          ".db2": "Spectrasonics soundsource", ".rx2": "Propellerhead REX, compressed", ".logicx": "Logic project, binary",
          ".aaz": "Alchemy resynthesis data", ".fxp_rmx": "Stylus RMX", ".fxr_rmx": "Stylus RMX"}
NOT_PARSED = {".bwpreset": "Bitwig preset (binary)",
              ".spf": "Spire preset", ".mxgrp": "Maschine group", ".multisample": "Bitwig multisample (zip)",
              ".omnisphere": "Omnisphere preset bundle", ".tun": "tuning tables", ".env": "Omnisphere envelopes",
              ".pbpack": "plugin preset bank", ".sbpack": "plugin preset bank", ".nbkt": "Battery kit",
              ".xmp": "Ableton metadata", ".crate": "Serato crate", ".sfk": "Sound Forge peaks"}
READ_FORMATS = {".sfz", ".fxp", ".asd", ".cst", ".bwproject", ".nksn", ".nka", ".amxd"}
TOOL.update({e: "read_formats" for e in READ_FORMATS})
WT_DUPES = {"Bitwig Studio": "KRC wavetables (same audio, written once)"}
DECODED_BY_SPITFIRE = {".spitfire", ".zmulti", ".zpreset", ".zconfig", ".db_app", ".nkc"}
DECODED_BY_STEAM = {".db2", ".omnisphere", ".db_app"}
INCOMPLETE = {".fdmdownload"}
NOT_MUSIC_TOP = {"Pictures", "blender", "Design", "Books", "spark ar", "library.fcpbundle", "Final Cut Original Media",
                 "High Life (2018) [BluRay] [1080p] [YTS.AM]", "Silver Linings Playbook (2012) [1080p]", "PIONEER"}
SPECTRA_TOPS = {"Spectrasonics", "Omisphere Patch and Soundsource Library Update", "Keyscape_Patch_Library_Update_1_6_0c",
                "Trilian Patch Library Update v1.4.9c", "Sonic Extensions - Nylon Sky"}
# a type means something else in some folders: .db is a Spectrasonics patch list only in Spectrasonics folders (LABS and
# others keep their own sample database in a .db); PitchLoop89's .adv are Max for Live devices with no device params
RECAST = {".db": lambda top: None if top in SPECTRA_TOPS else ".db_app", ".adv": lambda top: ".adv_max" if top == "PitchLoop89" else None}
LOCKED[".db_app"] = "library sample database (LABS etc.)"
NOT_PARSED[".adv_max"] = "Max for Live device preset (no device params)"
MUSICAL = set(TOOL) | set(LOCKED) | set(NOT_PARSED) | INCOMPLETE


def rows_by_top(path, root, key="src"):
    """{top folder: rows} for a jsonl whose rows carry a drive path"""
    c = collections.Counter()
    if not os.path.exists(path):
        return c
    for ln in open(path):
        m = re.search(r'"%s":"%s/([^/"#]+)' % (key, re.escape(root)), ln, re.I)  # one reader writes /Volumes/WD-Black
        if m:
            c[m[1]] += 1
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inventory", required=True)
    ap.add_argument("--root", default="/Volumes/Wd-Black")
    ap.add_argument("--out", default="collect_check.md")
    a = ap.parse_args()
    inv = json.load(open(a.inventory))["top_ext"]
    out = {
        "audio_features": rows_by_top(os.path.join(TD, "audio/features.jsonl"), a.root),
        "preset_reader": rows_by_top(os.path.join(TD, "presets/presets.jsonl"), a.root)
                         + rows_by_top(os.path.join(TD, "presets/alchemy.jsonl"), a.root)
                         + rows_by_top(os.path.join(TD, "presets/ableton.jsonl"), a.root),
        "exs_reader": rows_by_top(os.path.join(TD, "samplers/exs.jsonl"), a.root),
        "als_reader": rows_by_top(os.path.join(TD, "arcs/als.jsonl"), a.root),
        "wt_reader": rows_by_top(os.path.join(TD, "wavetables/wt.jsonl"), a.root),
        "spitfire_reader": sum((rows_by_top(os.path.join(TD, "spitfire", f), a.root) for f in ("labs_audio.jsonl", "nkc_catalog.jsonl")),
                               collections.Counter()),
        "steam_reader": sum((rows_by_top(os.path.join(TD, "steam", f), a.root) for f in ("arp_and_mods.jsonl", "omnisphere_packs.jsonl",
                            "soundsource_audio.jsonl", "soundsources.jsonl", "wavetables.jsonl")), collections.Counter()),
        "read_formats": rows_by_top(os.path.join(TD, "store/formats_meta.jsonl"), a.root),
    }
    # MIDI: inbox folder wd_<slug> came from top folder <top>; count ingest rows per inbox folder
    slug = lambda top: "wd_" + re.sub(r"[^a-z0-9]+", "_", top.lower()).strip("_")[:60]
    ing = collections.Counter()
    store = os.path.join(TD, "store/inbox.jsonl")
    if os.path.exists(store):
        r = subprocess.run(["grep", "-o", r'"src":"inbox:\(wd\|als\)_[a-z0-9_]*', store], capture_output=True, text=True)
        for ln in r.stdout.splitlines():
            ing[ln.split("inbox:")[1]] += 1
    copied = {d: len([f for f in os.listdir(os.path.join(TD, "inbox", d)) if f.endswith(".mid")])
              for d in os.listdir(os.path.join(TD, "inbox")) if d.startswith(("wd_", "als_"))}
    lines = ["# Drive collection check: %s" % a.root, "",
             "| folder | music files | captured by | rows | locked | incomplete | not parsed | status |",
             "|---|---:|---|---:|---:|---:|---:|---|"]
    tally = collections.Counter()
    missed_all = []
    for top in sorted(inv, key=str.lower):
        ext = collections.Counter()
        for e, n in inv[top].items():
            ext[(RECAST[e](top) if e in RECAST else None) or e] += n
        if top in NOT_MUSIC_TOP:
            lines.append("| %s | %d | not music (skipped) | | | | | skipped |" % (top, sum(ext.values())))
            tally["not music files"] += sum(ext.values())
            continue
        music = {e: n for e, n in ext.items() if e in MUSICAL}
        if not music:
            lines.append("| %s | 0 | (%s) | | | | | nothing musical |" % (top, ", ".join("%s %d" % kv for kv in sorted(ext.items(), key=lambda kv: -kv[1])[:3])))
            continue
        tools, rows, missed = collections.Counter(), 0, []
        for e, n in music.items():
            t = TOOL.get(e)
            if t:
                tools[t] += n
        for t in tools:
            got = out.get(t, collections.Counter()).get(top, 0)
            if t == "midi_ingest":
                got = ing.get(slug(top), 0)
                if top == "Discovery Series Cuba":
                    got = "held (Kontakt key map)"
                elif not got and copied.get(slug(top)):
                    got = "copied, ingest filtered it out (too short)"
            if t == "wt_reader" and not got and top in WT_DUPES:
                got = "duplicates of " + WT_DUPES[top]
            if got == 0:
                missed.append(t)
            rows += got if isinstance(got, int) else 0
        if "als_reader" in tools and top == "my music":
            rows += ing.get("als_owner", 0)
        # LABS containers, Kontakt .nkc maps and STEAM databases: decoded by spitfire_reader / steam_reader where they wrote rows
        for t, exts in (("spitfire_reader", DECODED_BY_SPITFIRE), ("steam_reader", DECODED_BY_STEAM)):
            got = out[t].get(top, 0)
            if got:
                tools[t] += sum(n for e, n in music.items() if e in exts)
                rows += got
                dec = sum(n for e, n in music.items() if e in exts)
                music = {e: n for e, n in music.items() if e not in exts}
                music["decoded"] = music.get("decoded", 0) + dec
        locked = sum(n for e, n in music.items() if e in LOCKED)
        inc = sum(n for e, n in music.items() if e in INCOMPLETE)
        np_ = sum(n for e, n in music.items() if e in NOT_PARSED)
        status = "missed: " + ", ".join(missed) if missed else ("captured" if tools else
                 "locked" if locked else "incomplete download" if inc else "not parsed")
        if missed:
            missed_all.append((top, missed))
        tally[status.split(":")[0]] += 1
        lines.append("| %s | %d | %s | %d | %d | %d | %d | %s |" % (top, sum(music.values()),
                     ", ".join("%s (%d files)" % kv for kv in tools.most_common()) or "-", rows, locked, inc, np_, status))
    lines += ["", "## Totals", "", "| status | folders |", "|---|---:|"] + ["| %s | %s |" % kv for kv in tally.most_common()]
    lines += ["", "## Rows written per tool", "", "| tool | rows | from folders |", "|---|---:|---:|"]
    for t, c in out.items():
        lines.append("| %s | %d | %d |" % (t, sum(c.values()), len(c)))
    lines.append("| midi_ingest (drive MIDI + Ableton clips) | %d | %d inbox folders, %d .mid copied |" % (
        sum(ing.values()), len(ing), sum(copied.values())))
    open(a.out, "w").write("\n".join(lines) + "\n")
    print("\n".join(lines[-12:]))
    print("missed:", missed_all or "none")
    print("wrote", a.out)


if __name__ == "__main__":
    main()
