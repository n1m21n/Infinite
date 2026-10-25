#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Vendor M-VAVE's open firmware sources (SMK37 OPEN, FM1 OPEN) into Synth REF (full build).

Usage:  python scripts/synthref_vendor_mvave.py <smk37_open|fm1_open> <path to a clone of github.com/m-vave/<repo>>

Copies, byte for byte, only what the host build compiles: app/ (without the
hardware drivers in app/board/), web/, third_party/synth/, the SDK's public
headers (sdk/include/*.h, no libc) and the licence files. Never copies the
proprietary SDK binaries (sdk/lib, sdk/firmware) or any tool. The firmware is
not modified; Synth REF only adds its own adapter next to it. Writes
UPSTREAM.md with the commit and a SHA-256 per file so an update is a reviewed
diff of this folder.
"""
import hashlib
import shutil
import subprocess
import sys
from datetime import date
from pathlib import Path

CORES = Path(__file__).resolve().parent.parent / "src" / "gpl" / "SynthRef" / "cores"
REPOS = {"smk37_open": "SMK37 OPEN", "fm1_open": "FM1 OPEN"}
TREES = ["app", "web", "third_party/synth"]
FILES = ["LICENSE.md", "THIRD_PARTY.md", "README.md"]
SOURCE_EXT = {".c", ".h", ".inc"}


def wanted(rel: Path) -> bool:
    parts = rel.parts
    if parts[0] == "app" and len(parts) > 1 and parts[1] == "board":
        return False
    return rel.suffix in SOURCE_EXT


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in REPOS:
        print(__doc__)
        return 2
    repo = sys.argv[1]
    DEST = CORES / repo / "upstream"
    src = Path(sys.argv[2]).resolve()
    if not (src / "web" / "engine" / "Simulator.c").is_file():
        print(f"not an {repo} checkout: {src}")
        return 1
    commit = subprocess.run(["git", "-C", str(src), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    picked = []
    for tree in TREES:
        for f in sorted((src / tree).rglob("*")):
            if f.is_file() and wanted(f.relative_to(src)):
                picked.append(f.relative_to(src))
    for f in sorted((src / "sdk" / "include").glob("*.h")):
        picked.append(f.relative_to(src))
    picked += [Path(f) for f in FILES if (src / f).is_file()]

    if DEST.exists():
        shutil.rmtree(DEST)
    lines = []
    for rel in picked:
        out = DEST / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src / rel, out)
        lines.append(f"| `{rel.as_posix()}` | `{hashlib.sha256(out.read_bytes()).hexdigest()[:16]}` |")

    (DEST.parent / "UPSTREAM.md").write_text(
        f"# {REPOS[repo]} upstream\n\n"
        "Vendored by `scripts/synthref_vendor_mvave.py`; do not edit files under `upstream/`.\n\n"
        f"- Source: https://github.com/m-vave/{repo}\n"
        f"- Commit: `{commit}`\n"
        f"- Vendored: {date.today().isoformat()}\n"
        "- Licences: GPL-3.0-only WITH GPL-3.0-linking-exception (app, web, sdk/include),\n"
        "  Apache-2.0 (web/synth DX7 port of msfa, app/factory_dx7*.inc / fm_factory.inc), MIT (third_party/synth,\n"
        "  web/synth sea_*). See the SPDX line of each file.\n"
        "  See upstream/LICENSE.md and upstream/THIRD_PARTY.md.\n"
        "- Not vendored: sdk/lib (proprietary JieLi engine library), sdk/firmware (stock images),\n"
        "  sdk/include/libc, app/board (hardware drivers), tools.\n\n"
        f"{len(picked)} files (SHA-256 prefix):\n\n| File | SHA-256 |\n|---|---|\n" + "\n".join(lines) + "\n",
        encoding="utf-8", newline="\n")
    print(f"vendored {len(picked)} files from {commit} into {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
