#!/usr/bin/env python3
"""
regions.py
L4: reads the virtual split of src/main.cpp built by
1_extractors/build_main_cpp_regions.py (output/main_cpp_regions.json) and answers, for a
symbol or a file result, which region it's in - so a brief can say
`src/main.cpp@drawoscillator L13399-14991` instead of just `src/main.cpp`, and a person (or the
brain) can read that one region's lines instead of the whole 95k-line file.

Static: built off HEAD, not rebuilt per query. Regenerate with:
    python3 1_extractors/build_main_cpp_regions.py
"""

import json
from pathlib import Path

MAIN_CPP = "src/main.cpp"
REGIONS_FILE = (Path(__file__).resolve().parents[1] / "1_extractors" / "output"
                / "main_cpp_regions.json")
TOC_BUDGET = 2000  # ~ tokens the plan wants the table of contents to cost, in chars * 4


class Regions:
    def __init__(self, path=REGIONS_FILE):
        self._by_id = {}
        self.symbol_to_region = {}
        self.ordered = []
        if not path.exists():
            return
        data = json.loads(path.read_text())
        self.symbol_to_region = data.get("symbol_to_region", {})
        for r in data.get("regions", []):
            self._by_id[r["id"]] = r
        self.ordered = sorted(self._by_id.values(), key=lambda r: r["line_start"])

    def region_for_symbol(self, sym):
        rid = self.symbol_to_region.get(sym)
        if rid is None:
            short = sym.split("::")[-1]
            rid = self.symbol_to_region.get(short)
        return self._by_id.get(rid) if rid else None

    def region_for_line(self, line):
        for r in self.ordered:
            if r["line_start"] <= line <= r["line_end"]:
                return r
        return None

    def label(self, sym):
        """`main.cpp@region-id L{start}-{end}`, or "" if the symbol isn't in a known region."""
        r = self.region_for_symbol(sym)
        if not r:
            return ""
        return f"{MAIN_CPP}@{r['id']} L{r['line_start']}-{r['line_end']}"

    def toc(self, budget=TOC_BUDGET):
        """The table of contents: one line per region, region id, line range, size and its
        key functions, cut to `budget` characters."""
        lines = [f"{MAIN_CPP} - {len(self.ordered)} regions:"]
        used = len(lines[0]) + 1
        for r in self.ordered:
            key = ", ".join(k.split(" ", 1)[0] for k in r["key_functions"][:3])
            line = f"@{r['id']} L{r['line_start']}-{r['line_end']} ({r['member_count']} syms): {key}"
            if used + len(line) + 1 > budget:
                break
            lines.append(line)
            used += len(line) + 1
        return "\n".join(lines)
