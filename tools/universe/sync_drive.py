#!/usr/bin/env python3
"""Mirror ~/Infinite/training-data rows and tools/universe code to Google Drive for the Colab trainer.

Rows only (no raw audio, no chat corpora). JSONL over 20 MB is gzipped on Drive; every copy is verified by
size (plain) or md5 of the gunzipped bytes. Safe to re-run: unchanged files are skipped. Nothing local is
deleted.

    python3 -I tools/universe/sync_drive.py [--drive DIR]
"""
import argparse, glob, gzip, hashlib, os, shutil

DATA = os.path.expanduser("~/Infinite/training-data")
CODE = os.path.join(os.path.dirname(os.path.abspath(__file__)))
SKIP = {"inbox"}  # raw MIDI corpus folder (store/ IS synced: Colab trains on store/inbox.jsonl)
BIG = 20_000_000


def md5(f):
    h = hashlib.md5()
    for b in iter(lambda: f.read(1 << 20), b""):
        h.update(b)
    return h.hexdigest()


def sync(src, dst, bad):
    if src.endswith(".jsonl") and os.path.getsize(src) > BIG:
        dst += ".gz"
        if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(src, "rb") as a, gzip.open(dst, "wb", 6) as b:
                shutil.copyfileobj(a, b)
        with open(src, "rb") as a, gzip.open(dst, "rb") as b:
            ok = md5(a) == md5(b)
    else:
        if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src) or os.path.getmtime(dst) < os.path.getmtime(src):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
        ok = os.path.getsize(dst) == os.path.getsize(src)
    if not ok:
        bad.append(src)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--drive", default=(glob.glob(os.path.expanduser("~/Library/CloudStorage/GoogleDrive-*/My Drive/Infinite-Cloud-Training")) or [""])[0])
    a = ap.parse_args()
    if not a.drive or not os.path.isdir(a.drive):
        raise SystemExit("Drive folder not found; is Google Drive for desktop running?")
    bad, n = [], 0
    for root, dirs, files in os.walk(DATA):
        rel = os.path.relpath(root, DATA)
        if rel.split(os.sep)[0] in SKIP or any(x.endswith(".bak") for x in [rel]):
            dirs[:] = []
            continue
        for f in files:
            if f.endswith(".bak") or f.startswith("."):
                continue
            sync(os.path.join(root, f), os.path.join(a.drive, "training-data", rel, f), bad)
            n += 1
    for f in glob.glob(os.path.join(CODE, "*.py")) + glob.glob(os.path.join(CODE, "*.json")):
        sync(f, os.path.join(a.drive, "universe", os.path.basename(f)), bad)
        n += 1
    print("synced and verified %d files, %d failed" % (n, len(bad)))
    for b in bad:
        print("  FAILED", b)


if __name__ == "__main__":
    main()
