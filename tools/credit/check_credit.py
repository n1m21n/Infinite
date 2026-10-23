#!/usr/bin/env python3
"""Check that Infinite's credit travels: in exported files, and in every place that states it.

  check_credit.py FILE [FILE ...]   does each file carry "Made with Infinite" in its metadata?
  check_credit.py --docs            do README, AGENT.md, CITATION.cff, llms.txt, Provenance.h,
                                    both MCP servers and the C++ JSON agree on the credit?

Exit code 0 when everything asked for passes, 1 otherwise. Needs only the standard library.
The wording lives in src/core/Provenance.h; this tool is how we notice drift.
"""
import os
import re
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MARK = "Made with Infinite"
CREDIT_LINE = "Infinite, by Naman Soni (n1m21n), https://infinitedevices.org"


def _has(data, text=MARK):
    """UTF-8 or UTF-16 (Windows Media Foundation writes wide strings)."""
    return text.encode("utf-8") in data or text.encode("utf-16-le") in data or text.encode("utf-16-be") in data


def check_wav(data):
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return False, "not a RIFF/WAVE file"
    pos = 12
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"LIST" and body[:4] == b"INFO" and MARK.encode() in body:
            return True, "LIST/INFO chunk"
        pos += 8 + size + (size & 1)
    return False, "no LIST/INFO credit chunk"


def check_flac(data):
    if data[:4] != b"fLaC":
        return False, "not a FLAC file"
    pos = 4
    while pos + 4 <= len(data):
        hdr = data[pos]
        last, btype = hdr & 0x80, hdr & 0x7F
        size = int.from_bytes(data[pos + 1:pos + 4], "big")
        body = data[pos + 4:pos + 4 + size]
        if btype == 4 and MARK.encode() in body:
            return True, "Vorbis comment block"
        pos += 4 + size
        if last:
            break
    return False, "no Vorbis credit comment"


def check_mp3(data):
    if data[:3] == b"ID3" and MARK.encode() in data[:2048]:
        return True, "ID3v2 tag"
    return False, "no ID3v2 credit tag"


def check_png(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return False, "not a PNG"
    pos = 8
    while pos + 8 <= len(data):
        size = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        if ctype == b"tEXt" and MARK.encode() in data[pos + 8:pos + 8 + size]:
            return True, "tEXt chunk"
        if ctype == b"IDAT":
            break
        pos += 12 + size
    return False, "no tEXt credit chunk"


def check_jpeg(data):
    if data[:2] != b"\xff\xd8":
        return False, "not a JPEG"
    pos = 2
    while pos + 4 <= len(data) and data[pos] == 0xFF:
        marker = data[pos + 1]
        if marker == 0xDA:
            break
        size = struct.unpack(">H", data[pos + 2:pos + 4])[0]
        if marker == 0xFE and MARK.encode() in data[pos + 4:pos + 2 + size]:
            return True, "COM segment"
        pos += 2 + size
    return False, "no COM credit segment"


def check_inf(data):
    head = data[:600].decode("utf-8", "replace")
    return (MARK in head), ("header comment" if MARK in head else "no header comment")


def check_movie(data):
    # MP4/MOV keep metadata in moov/udta or meta atoms; the note is stored as text inside them.
    return (_has(data), "metadata atom" if _has(data) else "no credit text in the file")


CHECKS = {
    ".wav": check_wav, ".flac": check_flac, ".mp3": check_mp3, ".png": check_png,
    ".jpg": check_jpeg, ".jpeg": check_jpeg, ".inf": check_inf, ".infinite": check_inf,
    ".mp4": check_movie, ".mov": check_movie, ".m4v": check_movie,
}


def check_files(paths):
    ok_all = True
    for p in paths:
        ext = os.path.splitext(p)[1].lower()
        fn = CHECKS.get(ext)
        if fn is None:
            print(f"?    {p}: no check for '{ext}'")
            ok_all = False
            continue
        with open(p, "rb") as f:
            data = f.read()
        ok, why = fn(data)
        print(("ok   " if ok else "MISS ") + f"{p}: {why}")
        ok_all &= ok
    return ok_all


# ---- the wording must agree everywhere ---------------------------------------------------------

DOC_PLACES = [
    ("README.md", CREDIT_LINE),
    ("AGENT.md", "Infinite, by Naman Soni (GitHub: n1m21n), https://infinitedevices.org"),
    ("website/llms.txt", CREDIT_LINE),
    ("tools/npm/infinite-engine/lib/engine.js", CREDIT_LINE),
    ("tools/mcp/README.md", CREDIT_LINE),
]


def check_docs():
    ok_all = True
    for rel, needle in DOC_PLACES:
        path = os.path.join(ROOT, rel)
        text = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
        ok = needle in text
        print(("ok   " if ok else "MISS ") + f"{rel}: credit line")
        ok_all &= ok

    prov = open(os.path.join(ROOT, "src/core/Provenance.h"), encoding="utf-8").read()
    pieces = ['"Naman Soni (n1m21n)"', '"https://infinitedevices.org"']
    for piece in pieces:
        ok = piece in prov
        print(("ok   " if ok else "MISS ") + f"src/core/Provenance.h: {piece}")
        ok_all &= ok

    cff = open(os.path.join(ROOT, "CITATION.cff"), encoding="utf-8").read()
    ok = "Soni" in cff and "Naman" in cff and "infinitedevices.org" in cff
    print(("ok   " if ok else "MISS ") + "CITATION.cff: author and url")
    ok_all &= ok

    # Every place that writes a file or answers an agent must route through Provenance.
    users = {
        "src/audio/AudioFileWriter.cpp": "Provenance::",
        "src/audio/AudioFileWriter.mm": "Provenance::",
        "src/app/graph/ExportImage.cpp": "Provenance::",
        "src/app/AppShared.h": "Provenance::",
        "src/platform/Platform.mm": "Provenance::",
        "src/platform/linux/MediaLinux.cpp": "Provenance::",
        "src/platform/win/MediaWin.cpp": "Provenance::",
        "src/core/Patch.cpp": "Provenance::",
        "src/core/HeadlessJob.cpp": "Provenance::",
    }
    for rel, needle in users.items():
        text = open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace").read()
        ok = needle in text
        print(("ok   " if ok else "MISS ") + f"{rel}: uses Provenance")
        ok_all &= ok
    return ok_all


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    if argv[1] == "--docs":
        return 0 if check_docs() else 1
    return 0 if check_files(argv[1:]) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
