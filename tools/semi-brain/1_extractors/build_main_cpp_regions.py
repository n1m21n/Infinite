#!/usr/bin/env python3
"""
build_main_cpp_regions.py
Virtual split of src/main.cpp into ~40-80 regions: groups of top-level symbols that belong
together, so a brief can say `main.cpp@region L100-200` instead of just `main.cpp` and the
brain (or a person) reads a few thousand lines instead of grepping the whole 95k-line file.

Signals feeding one weighted symbol graph, strongest first (see l4/louvain.py for the cluster
step, shared with l4/clusters.py's file-level areas):
  co-edit  two top-level symbols touched by the same commit's diff hunks to main.cpp (same
           logic as clusters.py's file co-change: a commit of n symbols adds 1/(n-1) per pair;
           commits touching more than MAX_COMMIT_SYMBOLS symbols are sweeping and skipped)
  calls    the AST forward call graph, restricted to edges where both ends are main.cpp
           top-level symbols
  family   symbols sharing a name prefix (DrawArrange*, Field*Test*): first two camelCase words
  banner   symbols between the same pair of `// ====` banner lines, chained lightly

Output: output/main_cpp_regions.json - {built_at, source_commit, regions: [...],
symbol_to_region: {symbol: region_id}}. Static (built off HEAD, not embargo-aware per commit):
regions are keyed by symbol name, which is stable enough for the replay's region score to use
against any past commit's target symbols.

    python3 build_main_cpp_regions.py
"""

import json
import re
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

REPO_PATH = Path(__file__).resolve().parents[3]
SEMI_BRAIN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SEMI_BRAIN_DIR))

from l4.louvain import louvain  # noqa: E402

MAIN_CPP = "src/main.cpp"
AST_GRAPH_FILE = Path(__file__).resolve().parent / "output" / "ast_symbol_graph.json"
OUTPUT_FILE = Path(__file__).resolve().parent / "output" / "main_cpp_regions.json"

MAX_COMMIT_SYMBOLS = 40
CALL_W = 0.3
NAME_W = 0.2
BANNER_W = 0.15
# The point of a region is "read these 2-3k lines instead of the whole file": semantic ties
# alone (co-edit/call/name) scatter across the file as a feature grows over years, so a region
# built from them alone can span nearly the whole file - no more readable than main.cpp itself.
# A strong chain edge between each symbol and its immediate file neighbour keeps clustering
# anchored to physical proximity; semantic ties still pull the occasional distant symbol in,
# but can't out-compete a run of local chain edges for a whole contiguous block.
PROXIMITY_W = 1.5
MAX_FAMILY_GROUP = 30
# A region should be a chunk someone can actually read instead of grepping the whole file.
MAX_REGION_SPAN = 6000
# A callee shared by many main.cpp callers is a generic helper (color/layout/undo utilities),
# not a feature: using it to cluster would glue unrelated Draw*Body functions into one region,
# same collapse-to-one-blob failure clusters.py notes for plain label propagation on files.
HUB_CALLEE_MAX_CALLERS = 12
# A single global resolution can't hit the target region count without a trade-off: high
# enough to keep semantic clusters apart (no giant blob), the raw community count is a few
# hundred (mostly singletons); low enough to merge those singletons away, main.cpp's own call
# graph percolates into one region spanning nearly the whole file. So: cluster at a resolution
# that stays split (no blob), then merge the smallest communities into their best-connected (or
# line-nearest) neighbour until the count is in range - shrinking from a good clustering, not
# coarsening a global parameter into one.
RESOLUTION = 3.0
TARGET_RANGE = (40, 80)

HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
BANNER_RE = re.compile(r"^// ={5,}\s*(.*)$")


def load_main_symbols():
    graph = json.loads(AST_GRAPH_FILE.read_text())
    syms = {name: meta for name, meta in graph["symbols"].items() if meta.get("file") == MAIN_CPP}
    return syms, graph["forward_call_graph"]


def symbol_at_line(ordered, line):
    """ordered: [(start, end, name)] sorted by start. The symbol containing `line`, or the
    nearest preceding one (a diff inside a body between two recorded starts)."""
    lo, hi, best = 0, len(ordered) - 1, None
    while lo <= hi:
        mid = (lo + hi) // 2
        if ordered[mid][0] <= line:
            best = ordered[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    if best is None:
        return None
    start, end, name = best
    return name


def commit_hashes():
    out = subprocess.run(["git", "log", "--format=%H", "--follow", "--", MAIN_CPP],
                          cwd=REPO_PATH, capture_output=True, text=True, check=True)
    return [h for h in out.stdout.splitlines() if h]


def touched_lines(commit_hash):
    """New-file line numbers this commit's diff to main.cpp added or changed."""
    out = subprocess.run(
        ["git", "show", "--unified=0", "--format=", commit_hash, "--", MAIN_CPP],
        cwd=REPO_PATH, capture_output=True, text=True)
    lines = []
    for l in out.stdout.splitlines():
        m = HUNK_RE.match(l)
        if not m:
            continue
        start = int(m.group(1))
        count = int(m.group(2)) if m.group(2) is not None else 1
        if count == 0:
            continue  # pure deletion, no new-file lines to attribute
        lines.extend(range(start, start + count))
    return lines


def build_coedit_adj(ordered):
    adj = defaultdict(lambda: defaultdict(float))
    for h in commit_hashes():
        lines = touched_lines(h)
        if not lines:
            continue
        syms = sorted({symbol_at_line(ordered, l) for l in lines} - {None})
        if not 2 <= len(syms) <= MAX_COMMIT_SYMBOLS:
            continue
        w = 1.0 / (len(syms) - 1)
        for i, a in enumerate(syms):
            for b in syms[i + 1:]:
                adj[a][b] += w
                adj[b][a] += w
    return adj


def resolve_callee(name, main_syms):
    if name in main_syms:
        return name
    for k in main_syms:
        if k.endswith("::" + name) or k.split("::")[-1] == name:
            return k
    return None


def add_call_edges(adj, main_syms, fwd_graph):
    edges = []
    callers_of = defaultdict(set)
    for caller, callees in fwd_graph.items():
        if caller not in main_syms:
            continue
        for callee in callees:
            dst = resolve_callee(callee, main_syms)
            if dst and dst != caller:
                edges.append((caller, dst))
                callers_of[dst].add(caller)
    for src, dst in edges:
        if len(callers_of[dst]) > HUB_CALLEE_MAX_CALLERS:
            continue
        adj[src][dst] += CALL_W
        adj[dst][src] += CALL_W


WORD_RE = re.compile(r"[A-Z][a-z0-9]*|[A-Z]+(?![a-z])|[a-z0-9]+")


def family_key(sym_name):
    short = sym_name.split("::")[-1]
    words = WORD_RE.findall(short)
    return "".join(words[:2]) if words else short


def add_name_edges(adj, main_syms):
    groups = defaultdict(list)
    for name in main_syms:
        groups[family_key(name)].append(name)
    for key, members in groups.items():
        if not 2 <= len(members) <= MAX_FAMILY_GROUP:
            continue
        for i, a in enumerate(members):
            for b in members[i + 1:]:
                adj[a][b] += NAME_W
                adj[b][a] += NAME_W


def banner_map():
    """line -> banner label, for each `// ====` banner in main.cpp."""
    banners = []
    for i, line in enumerate((REPO_PATH / MAIN_CPP).read_text(errors="replace").splitlines(), 1):
        m = BANNER_RE.match(line)
        if m and m.group(1).strip():
            banners.append((i, m.group(1).strip()))
    return banners


def add_banner_edges(adj, ordered, banners):
    """Symbols between the same pair of consecutive banners get a light chain edge, and each
    symbol's nearest preceding banner label is returned for naming."""
    label_at = {}
    if not banners:
        return label_at
    for start, end, name in ordered:
        lbl = None
        for bline, label in banners:
            if bline <= start:
                lbl = label
            else:
                break
        if lbl:
            label_at[name] = lbl
    by_label = defaultdict(list)
    for name, lbl in label_at.items():
        by_label[lbl].append(name)
    for lbl, members in by_label.items():
        if len(members) < 2:
            continue
        for a, b in zip(members, members[1:]):
            adj[a][b] += BANNER_W
            adj[b][a] += BANNER_W
    return label_at


def add_proximity_edges(adj, ordered):
    for (_, _, a), (_, _, b) in zip(ordered, ordered[1:]):
        adj[a][b] += PROXIMITY_W
        adj[b][a] += PROXIMITY_W


def cluster(adj, main_syms, ordered):
    comm = louvain(adj, resolution=RESOLUTION)
    next_id = max(comm.values(), default=-1) + 1
    for name in main_syms:
        if name not in comm:
            comm[name] = next_id  # isolated symbol, no edges at all: its own community
            next_id += 1
    comm = merge_small(comm, adj, ordered, TARGET_RANGE[1])
    return comm, RESOLUTION


def merge_small(comm, adj, ordered, max_regions, max_span=MAX_REGION_SPAN):
    """Repeatedly folds the smallest community into whichever other community it's most
    connected to (or, failing any connection, the line-nearest one) until at most max_regions
    remain. A merge that would stretch the target past max_span lines is skipped in favour of
    the next-best tie, so a region stays a readable chunk instead of a whole-file thematic
    grab-bag reached only through a chain of semantic ties."""
    lo, hi = {}, {}
    for start, end, name in ordered:
        lo[name] = start
        hi[name] = end
    centroid = {name: (lo[name] + hi[name]) / 2 for name in lo}
    members = defaultdict(set)
    for name, c in comm.items():
        members[c].add(name)

    def span_after_merge(a, b):
        lines = [lo[n] for n in members[a]] + [lo[n] for n in members[b]] + \
                [hi[n] for n in members[a]] + [hi[n] for n in members[b]]
        return max(lines) - min(lines)

    def nearest_by_line(c, avoid=()):
        c_mid = sum(centroid[n] for n in members[c]) / len(members[c])
        best, best_dist = None, None
        for other in members:
            if other == c or other in avoid or not members[other]:
                continue
            o_mid = sum(centroid[n] for n in members[other]) / len(members[other])
            dist = abs(o_mid - c_mid)
            if best_dist is None or dist < best_dist:
                best, best_dist = other, dist
        return best

    while len([c for c in members if members[c]]) > max_regions:
        smallest = min((c for c in members if members[c]), key=lambda c: len(members[c]))
        neighbours = {}
        for n in members[smallest]:
            for m, w in adj.get(n, {}).items():
                other = comm[m]
                if other != smallest:
                    neighbours[other] = neighbours.get(other, 0.0) + w
        target = next((cand for cand in sorted(neighbours, key=neighbours.get, reverse=True)
                       if span_after_merge(smallest, cand) <= max_span), None)
        if target is None:
            # no semantic tie fits under the span cap (or none at all): the physically
            # nearest community, even if that too exceeds the cap - it must merge somewhere.
            target = nearest_by_line(smallest)
        if target is None:
            break
        for n in members[smallest]:
            comm[n] = target
        members[target] |= members[smallest]
        members[smallest] = set()
    return comm


def slugify(text):
    return re.sub(r"-+", "-", re.sub(r"[^a-z0-9]+", "-", text.lower())).strip("-")[:40]


def main():
    main_syms, fwd_graph = load_main_symbols()
    ordered = sorted(((m["line"], m.get("end_line", m["line"]), name)
                      for name, m in main_syms.items()))

    adj = build_coedit_adj(ordered)
    add_call_edges(adj, main_syms, fwd_graph)
    add_name_edges(adj, main_syms)
    banners = banner_map()
    label_at = add_banner_edges(adj, ordered, banners)
    add_proximity_edges(adj, ordered)

    for name in main_syms:
        adj.setdefault(name, defaultdict(float))
    comm, resolution = cluster(adj, main_syms, ordered)

    by_region = defaultdict(list)
    for name, c in comm.items():
        by_region[c].append(name)

    regions = []
    symbol_to_region = {}
    for c, members in by_region.items():
        members = [m for m in members if m in main_syms]
        if not members:
            continue
        labels = [label_at[m] for m in members if m in label_at]
        families = [family_key(m) for m in members]
        if labels and labels.count(max(set(labels), key=labels.count)) >= len(members) / 2:
            name = slugify(max(set(labels), key=labels.count))
        else:
            name = slugify(max(set(families), key=families.count))
        rid = name or f"region-{c}"
        base_rid, dupe = rid, 1
        existing_ids = {r["id"] for r in regions}
        while rid in existing_ids:
            dupe += 1
            rid = f"{base_rid}-{dupe}"
        regions.append(_region_record(rid, members, main_syms, adj, label_at))
        for m in members:
            symbol_to_region[m] = rid

    regions.sort(key=lambda r: r["line_start"])

    head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=REPO_PATH,
                          capture_output=True, text=True).stdout.strip()
    out = {
        "built_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "source_commit": head,
        "resolution": resolution,
        "region_count": len(regions),
        "regions": regions,
        "symbol_to_region": symbol_to_region,
    }
    OUTPUT_FILE.write_text(json.dumps(out, indent=1))
    print(f"{len(regions)} regions (resolution={resolution}) -> {OUTPUT_FILE}")


def _region_record(rid, members, main_syms, adj, label_at):
    metas = [(m, main_syms[m]) for m in members]
    line_start = min(meta["line"] for _, meta in metas)
    line_end = max(meta.get("end_line", meta["line"]) for _, meta in metas)
    degree = {m: sum(adj.get(m, {}).values()) for m in members}
    key = sorted(members, key=lambda m: -degree[m])[:5]
    labels = [label_at[m] for m in members if m in label_at]
    banner = max(set(labels), key=labels.count) if labels else ""
    return {
        "id": rid,
        "line_start": line_start,
        "line_end": line_end,
        "member_count": len(members),
        "banner": banner,
        "key_functions": [f"{m} L{main_syms[m]['line']}" for m in key],
    }


if __name__ == "__main__":
    main()
