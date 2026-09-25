#!/usr/bin/env python3
"""
build_main_cpp_regions.py
Virtual split of src/main.cpp into contiguous, non-overlapping line-range regions covering the
whole file, so a brief can say `main.cpp@region L100-2300` instead of just `main.cpp` and the
brain (or a person) reads a few thousand lines instead of grepping the whole 95k-line file.

A region is a run of file lines, not a semantic cluster: v1 grouped symbols by community
(co-edit + call + name-family + light proximity) and took the min/max line of each community's
members as its "range". Communities aren't contiguous - a handful of far-apart symbols pulled
into the same cluster stretched that envelope across other clusters' ranges, so most regions
overlapped (68/80) and the last one stopped 12k lines short of EOF. This version never looks at
cluster membership for the range: it walks the file top to bottom and only ever cuts *between*
two adjacent symbols, so every region is one real slice of the file and the slices tile it
exactly.

Cut points, strongest rule first:
  banner   a `// ====` banner between two adjacent symbols is always a cut - it is the file's
           own section marker (self-test blocks, mostly).
  weakest link   inside any run still over the cap, repeatedly cut at the adjacent pair with the
           lowest affinity (co-edit + call-graph + name-family), same signals v1 used to cluster,
           now used to score adjacency instead of communities.
  cap      no region may exceed MAX_REGION_SPAN lines; a run that has no more affinity signal
           left to cut on is split at its midpoint so the cap still holds.

Output: output/main_cpp_regions.json - {built_at, source_commit, region_count, regions: [...],
symbol_to_region: {symbol: region_id}}. Static (built off HEAD, not embargo-aware per commit):
regions are keyed by symbol name, which is stable enough for the replay's region score to use
against any past commit's target symbols.

    python3 build_main_cpp_regions.py
"""

import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

REPO_PATH = Path(__file__).resolve().parents[3]
SEMI_BRAIN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SEMI_BRAIN_DIR))

MAIN_CPP = "src/main.cpp"
AST_GRAPH_FILE = Path(__file__).resolve().parent / "output" / "ast_symbol_graph.json"
OUTPUT_FILE = Path(__file__).resolve().parent / "output" / "main_cpp_regions.json"

MAX_COMMIT_SYMBOLS = 40
COEDIT_W = 1.0
CALL_W = 0.6
NAME_W = 0.4
# A region should be a chunk someone can actually read instead of grepping the whole file.
MAX_REGION_SPAN = 3000
# A callee shared by many main.cpp callers is a generic helper (color/layout/undo utilities),
# not a feature: an edge through it would pull unrelated neighbours together the same way a
# shared helper collapsed clusters into one blob in the old community version.
HUB_CALLEE_MAX_CALLERS = 12
MAX_FAMILY_GROUP = 30
STOPWORDS = {
    "the", "and", "for", "with", "into", "from", "this", "that", "add", "adds", "fix", "fixes",
    "fixed", "chore", "feat", "docs", "test", "tests", "wip", "semi", "brain", "node", "nodes",
    "make", "makes", "when", "now", "not", "its", "was", "were", "onto", "off", "out", "off",
    "use", "used", "using", "via", "per", "one", "two", "main", "cpp", "region", "regions",
}

HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
BANNER_RE = re.compile(r"^// ={5,}\s*(.*)$")
WORD_RE = re.compile(r"[A-Z][a-z0-9]*|[A-Z]+(?![a-z])|[a-z0-9]+")


def load_main_symbols():
    graph = json.loads(AST_GRAPH_FILE.read_text())
    syms = {name: meta for name, meta in graph["symbols"].items() if meta.get("file") == MAIN_CPP}
    return syms, graph["forward_call_graph"]


def build_roots(ordered):
    """Collapses nested/overlapping symbols (a class and its own member functions, both listed
    as separate main.cpp symbols with the member's range inside the class's) into one top-level
    interval per group, extended to cover every member. Regions are cut between these roots: a
    walk that instead visited every nested symbol individually would see end lines jump backward
    (the class's end line, then a member's much earlier one) and produce backwards/negative-span
    boundaries. Every symbol - root or nested - is still mapped to a region afterwards, by line,
    in main()."""
    roots = []
    for start, end, name in ordered:
        if roots and start <= roots[-1][1]:
            if end > roots[-1][1]:
                roots[-1] = (roots[-1][0], end, roots[-1][2])
        else:
            roots.append([start, end, name])
    return [tuple(r) for r in roots]


def split_big_roots(ordered, max_span, main_syms):
    """A handful of top-level functions (RegisterNodes: 42k lines of REGISTER_NODE macro calls,
    no nested symbols the AST extractor can see) are themselves bigger than the cap, with no
    sub-boundary to cut at - a semantic split can't happen because there's no semantic structure
    inside them. Chunked into max_span-line pieces (`Name#2`, `Name#3`, ...) and added to
    main_syms so the rest of the pipeline (adjacency, naming, key_functions) treats each chunk
    like any other symbol; this is the same "no signal left, cut on line count alone" fallback
    fill_gaps uses for stretches with no symbols at all."""
    out = []
    for start, end, name in ordered:
        span = end - start + 1
        if span <= max_span:
            out.append((start, end, name))
            continue
        n_chunks = -(-span // max_span)
        size = -(-span // n_chunks)
        cur = start
        i = 1
        while cur <= end:
            chunk_end = min(cur + size - 1, end)
            chunk_name = name if i == 1 else f"{name}#{i}"
            out.append((cur, chunk_end, chunk_name))
            main_syms[chunk_name] = {"line": cur, "end_line": chunk_end}
            cur = chunk_end + 1
            i += 1
    return out


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
    return best[2]


def commit_log():
    """[(hash, subject)] for every commit that has ever touched main.cpp, oldest last."""
    out = subprocess.run(["git", "log", "--format=%H%x01%s", "--follow", "--", MAIN_CPP],
                          cwd=REPO_PATH, capture_output=True, text=True, check=True)
    log = []
    for line in out.stdout.splitlines():
        if "\x01" not in line:
            continue
        h, subject = line.split("\x01", 1)
        log.append((h, subject))
    return log


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


def build_symbol_adj_and_commit_symbols(ordered, log):
    """Returns (adj, commits_by_symbol): adj is the co-edit weighted graph over main.cpp
    symbols; commits_by_symbol maps a symbol name to the list of (hash, subject) that touched
    it, for naming."""
    adj = defaultdict(lambda: defaultdict(float))
    commits_by_symbol = defaultdict(list)
    for h, subject in log:
        lines = touched_lines(h)
        if not lines:
            continue
        syms = sorted({symbol_at_line(ordered, l) for l in lines} - {None})
        for s in syms:
            commits_by_symbol[s].append((h, subject))
        if not 2 <= len(syms) <= MAX_COMMIT_SYMBOLS:
            continue
        w = 1.0 / (len(syms) - 1)
        for i, a in enumerate(syms):
            for b in syms[i + 1:]:
                adj[a][b] += w
                adj[b][a] += w
    return adj, commits_by_symbol


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


def family_key(sym_name):
    short = sym_name.split("::")[-1]
    words = WORD_RE.findall(short)
    return "".join(words[:2]) if words else short


def banner_map():
    """line -> banner label, for each `// ====` banner in main.cpp."""
    banners = []
    for i, line in enumerate((REPO_PATH / MAIN_CPP).read_text(errors="replace").splitlines(), 1):
        m = BANNER_RE.match(line)
        if m and m.group(1).strip():
            banners.append((i, m.group(1).strip()))
    return banners


def banner_label_at(ordered, banners):
    """The nearest-preceding banner label for each symbol, and per adjacent-pair, whether a
    banner falls strictly between them (a hard cut)."""
    label_at = {}
    hard_cut_after = [False] * (len(ordered) - 1)
    if not banners:
        return label_at, hard_cut_after
    bidx = 0
    cur_label = None
    for i, (start, end, name) in enumerate(ordered):
        while bidx < len(banners) and banners[bidx][0] <= start:
            cur_label = banners[bidx][1]
            bidx += 1
        if cur_label:
            label_at[name] = cur_label
    # a cut is "hard" wherever the label changes between i and i+1 (a banner sits in between)
    for i in range(len(ordered) - 1):
        a, b = ordered[i][2], ordered[i + 1][2]
        if label_at.get(a) != label_at.get(b) and label_at.get(b) is not None:
            hard_cut_after[i] = True
    return label_at, hard_cut_after


def boundary_weights(ordered, adj):
    """affinity[i] = tie strength between ordered[i] and ordered[i+1] - the "weakest link" cut
    signal. Only looks at declared edges (co-edit/call/name), not physical adjacency, so a run
    with no signal at all is genuinely weak everywhere and falls back to a midpoint split."""
    weights = []
    for i in range(len(ordered) - 1):
        a, b = ordered[i][2], ordered[i + 1][2]
        weights.append(adj.get(a, {}).get(b, 0.0))
    return weights


def split_segment(seg, weights, max_span, seg_bounds):
    """seg: (i0, i1) inclusive symbol-index range. Splits recursively on the weakest boundary
    until every piece's real rendered span (per seg_bounds - the same boundaries the final
    regions are built from) is under max_span. Hard cuts are applied first, upstream, so this
    only ever makes size-driven cuts."""
    i0, i1 = seg
    line_start, line_end = seg_bounds(i0, i1)
    if line_end - line_start + 1 <= max_span:
        return [seg]
    if i0 >= i1:
        return [seg]  # a single symbol (plus its share of surrounding dead space) over the cap
    candidates = list(range(i0, i1))  # cut "after" index j, for j in [i0, i1)
    weak = min(candidates, key=lambda j: (weights[j], abs(j - (i0 + i1) // 2)))
    left, right = (i0, weak), (weak + 1, i1)
    return split_segment(left, weights, max_span, seg_bounds) + \
        split_segment(right, weights, max_span, seg_bounds)


def commit_keyword(members, commits_by_symbol, exclude_words):
    """The most common non-stopword token across commit subjects that touched this region's
    symbols, excluding words already spent on the family/banner part of the name."""
    counts = Counter()
    seen_hashes = set()
    for m in members:
        for h, subject in commits_by_symbol.get(m, []):
            if (h, m) in seen_hashes:
                continue
            seen_hashes.add((h, m))
            for w in WORD_RE.findall(subject):
                lw = w.lower()
                if len(lw) < 3 or lw in STOPWORDS or lw in exclude_words:
                    continue
                counts[lw] += 1
    if not counts:
        return None
    return counts.most_common(1)[0][0]


def slugify(text):
    return re.sub(r"-+", "-", re.sub(r"[^a-z0-9]+", "-", text.lower())).strip("-")[:40]


def is_gap(name):
    return name.startswith("__gap")


def name_region(members, label_at, commits_by_symbol):
    real = [m for m in members if not is_gap(m)]
    if not real:
        # a stretch of file the AST extractor found no top-level symbols in (e.g. macro-heavy
        # self-test bodies): name it from the enclosing banner alone, there's nothing else to go on.
        lbl = next((label_at[m] for m in members if m in label_at), None)
        return slugify(lbl) + "-block" if lbl else "unparsed-block"
    labels = [label_at[m] for m in real if m in label_at]
    families = [family_key(m) for m in real]
    if labels and labels.count(max(set(labels), key=labels.count)) >= len(real) / 2:
        base = max(set(labels), key=labels.count)
    else:
        base = max(set(families), key=families.count)
    base_slug = slugify(base)
    kw = commit_keyword(real, commits_by_symbol, exclude_words=set(WORD_RE.findall(base.lower())))
    if kw and kw not in base_slug:
        return slugify(f"{base}-{kw}")
    return base_slug or "region"


def fill_gaps(ordered, max_span, file_lines):
    """Inserts synthetic `__gap_<start>` placeholder entries for any stretch between two real
    symbols (or before the first / after the last, to EOF) over half the cap, so the segmenter
    always has a cut point inside code the AST extractor didn't parse into top-level symbols -
    otherwise that stretch is one unsplittable, over-cap region. A gap doesn't need to be over
    the cap by itself to cause trouble: a run of several sub-cap gaps between sparse symbols can
    add up to an over-cap segment with no single cut point big enough to trigger a split on."""
    GAP_TRIGGER = max_span // 2

    def chunk_gap(filled, start, end):
        length = end - start + 1
        if length <= 0:
            return
        if length <= max_span:
            filled.append((start, end, f"__gap_{start}"))
            return
        n_chunks = -(-length // max_span)  # ceil
        size = -(-length // n_chunks)      # roughly equal pieces, each <= max_span
        cur = start
        while cur <= end:
            chunk_end = min(cur + size - 1, end)
            filled.append((cur, chunk_end, f"__gap_{cur}"))
            cur = chunk_end + 1

    filled = []
    prev_end = ordered[0][0] - 1
    for start, end, name in ordered:
        gap = start - 1 - prev_end
        if gap > GAP_TRIGGER:
            chunk_gap(filled, prev_end + 1, start - 1)
        filled.append((start, end, name))
        prev_end = end
    if file_lines - prev_end > GAP_TRIGGER:
        chunk_gap(filled, prev_end + 1, file_lines)
    return filled


def main():
    main_syms, fwd_graph = load_main_symbols()
    all_syms_ordered = sorted(((m["line"], m.get("end_line", m["line"]), name)
                               for name, m in main_syms.items()))
    ordered = build_roots(all_syms_ordered)
    ordered = split_big_roots(ordered, MAX_REGION_SPAN, main_syms)
    file_text_lines = (REPO_PATH / MAIN_CPP).read_text(errors="replace").splitlines()
    file_lines = len(file_text_lines)

    log = commit_log()
    adj, commits_by_symbol = build_symbol_adj_and_commit_symbols(ordered, log)
    ordered = fill_gaps(ordered, MAX_REGION_SPAN, file_lines)
    # scale co-edit weight, then add call and name-family ties on the same graph
    for a in list(adj):
        for b in list(adj[a]):
            adj[a][b] *= COEDIT_W
    add_call_edges(adj, main_syms, fwd_graph)
    groups = defaultdict(list)
    for name in main_syms:
        groups[family_key(name)].append(name)
    for members in groups.values():
        if not 2 <= len(members) <= MAX_FAMILY_GROUP:
            continue
        for i, a in enumerate(members):
            for b in members[i + 1:]:
                adj[a][b] += NAME_W
                adj[b][a] += NAME_W

    banners = banner_map()
    label_at, hard_cut_after = banner_label_at(ordered, banners)
    weights = boundary_weights(ordered, adj)

    # The line a region actually gets assigned (line_start/line_end below) has to agree with
    # what split_segment measures while deciding whether to cut - otherwise a segment can pass
    # its own span check using bare symbol start/end, then grow past the cap once the dead space
    # on either side (not itself a full/half-cap gap, so fill_gaps left it alone) is tiled onto
    # it. `bnd[i]` is the fixed boundary line between symbol i and i+1 (their gap's midpoint);
    # every region's real start/end is derived from these same boundaries, both here and in
    # split_segment, so there is never a second, disagreeing notion of a region's size.
    n = len(ordered)
    bnd = [(ordered[i][1] + ordered[i + 1][0]) // 2 for i in range(n - 1)]

    def seg_bounds(i0, i1):
        line_start = 1 if i0 == 0 else bnd[i0 - 1] + 1
        line_end = file_lines if i1 == n - 1 else bnd[i1]
        return line_start, line_end

    # 1. split at hard (banner) cuts
    hard_segments = []
    start = 0
    for i in range(n - 1):
        if hard_cut_after[i]:
            hard_segments.append((start, i))
            start = i + 1
    hard_segments.append((start, n - 1))

    # 2. split anything still over the cap at its weakest boundary
    segments = []
    for seg in hard_segments:
        segments.extend(split_segment(seg, weights, MAX_REGION_SPAN, seg_bounds))
    segments.sort()

    regions = []
    symbol_to_region = {}
    for k, (i0, i1) in enumerate(segments):
        members = [ordered[i][2] for i in range(i0, i1 + 1)]
        line_start, line_end = seg_bounds(i0, i1)
        real_members = [m for m in members if not is_gap(m)]
        degree = {m: sum(adj.get(m, {}).values()) for m in real_members}
        key = sorted(real_members, key=lambda m: -degree[m])[:5]
        rid = name_region(members, label_at, commits_by_symbol)
        base_rid, dupe = rid, 1
        existing_ids = {r["id"] for r in regions}
        while rid in existing_ids:
            dupe += 1
            rid = f"{base_rid}-{dupe}"
        regions.append({
            "id": rid,
            "line_start": line_start,
            "line_end": line_end,
            "member_count": len(real_members),
            "banner": label_at.get(members[0], ""),
            "key_functions": [f"{m} L{main_syms[m]['line']}" for m in key],
        })
        for m in real_members:
            symbol_to_region[m] = rid

    # Roots drove the cuts, but every symbol - including the nested ones build_roots folded
    # away (a class's own member functions) - still needs a region so a brief naming one of
    # them resolves. Regions are contiguous and sorted by line_start, so this is one linear scan.
    region_bounds = [(r["line_start"], r["line_end"], r["id"]) for r in regions]
    ri = 0
    for start, _end, name in all_syms_ordered:
        if name in symbol_to_region:
            continue
        while ri + 1 < len(region_bounds) and start > region_bounds[ri][1]:
            ri += 1
        if region_bounds[ri][0] <= start <= region_bounds[ri][1]:
            symbol_to_region[name] = region_bounds[ri][2]

    head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=REPO_PATH,
                          capture_output=True, text=True).stdout.strip()
    out = {
        "built_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "source_commit": head,
        "region_count": len(regions),
        "regions": regions,
        "symbol_to_region": symbol_to_region,
    }
    OUTPUT_FILE.write_text(json.dumps(out, indent=1))

    spans = sorted(r["line_end"] - r["line_start"] + 1 for r in regions)
    overlaps = sum(1 for a, b in zip(regions, regions[1:]) if a["line_end"] >= b["line_start"])
    gap = (regions[0]["line_start"] != 1) or (regions[-1]["line_end"] != file_lines)
    print(f"{len(regions)} regions -> {OUTPUT_FILE}")
    print(f"median span {spans[len(spans)//2]}, max span {spans[-1]}, overlaps {overlaps}, "
          f"covers whole file: {not gap}")


if __name__ == "__main__":
    main()
