"""The labelled chord-progression files key_eval.py tests on, and a disjoint tuning set taken from the same shuffle after them."""
import glob, os, random
ROOT = os.path.expanduser("~/Infinite/datasets/midi/data/chord_progressions")
PC = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}

def files(n):
    """(test, tune): lists of (path, tonic pitch class, mode). test = key_eval.py's first n labelled files; tune = the next n."""
    fs = glob.glob(ROOT + "/Major/**/*.mid", recursive=True) + glob.glob(ROOT + "/Minor/**/*.mid", recursive=True)
    random.Random(0).shuffle(fs); out = []
    for f in fs:
        key = os.path.basename(f).split(" - ")[0].strip()
        if key in PC: out.append((f, PC[key], "major" if "/Major/" in f else "minor"))
    return out[:n], out[n:2 * n]
