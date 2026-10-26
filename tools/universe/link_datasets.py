#!/usr/bin/env python3
"""Link the big MIDI corpus (~/Infinite/datasets/midi/data) into the training inbox without copying it.
    link_datasets.py
Writes a SOURCE.txt (licence + family) next to each chosen dataset and a symlink in the inbox.
The owner has cleared the licensing of the whole corpus, so every set is tagged `cleared-by-owner` and trains both scopes."""
import os

DATA = os.path.expanduser("~/Infinite/datasets/midi/data")
INBOX = os.path.expanduser("~/Infinite/training-data/inbox")
CLEARED = "cleared-by-owner"  # owner confirmed 2026-10-26 that the licensing of this corpus is sorted
# name -> (licence, family or None)
SETS = {
    "lakh_midi_clean": (CLEARED, None),
    "lakh_midi_full": (CLEARED, None),
    "tegridy_symbolic_corpus": (CLEARED, None),
    "rock_piano_drums": (CLEARED, None),
    "drum_machine_patterns": (CLEARED, None),
    "nes_chiptune_mdb": (CLEARED, "retro"),
    "pop909_songs": (CLEARED, None),
    "nottingham_folk": (CLEARED, None),
    "adl_piano_genres": (CLEARED, None),
    "bach_chorales": (CLEARED, None),
    "chord_progressions": (CLEARED, None),
    "chords_and_scales": (CLEARED, None),
    "modal_chord_progressions": (CLEARED, None),
}


def main():
    os.makedirs(INBOX, exist_ok=True)
    for name, (lic, fam) in SETS.items():
        src = os.path.join(DATA, name)
        if not os.path.isdir(src):
            print("missing", name)
            continue
        with open(os.path.join(src, "SOURCE.txt"), "w") as f:
            f.write("license: %s\n" % lic)
            if fam:
                f.write("family: %s\n" % fam)
        dst = os.path.join(INBOX, name)
        if not os.path.islink(dst):
            os.symlink(src, dst)
        print("linked", name, "->", lic)


if __name__ == "__main__":
    main()
