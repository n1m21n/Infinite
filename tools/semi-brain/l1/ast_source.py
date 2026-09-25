#!/usr/bin/env python3
"""
ast_source.py
ast_symbol_graph.json as part of the sync. Each src/ code file carries a watermark
(size, mtime, git blob sha); a file is re-read only when its size or mtime moved, and
re-parsed only when its content is new to the parse cache. Assembling the graph from cached
per-file results is cheap, so only the changed files cost tree-sitter time.
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import REPO_PATH  # noqa: E402
from l1.ast_cache import CODE_EXTS, BytesReader, ParseCache, blob_sha, graph_order  # noqa: E402
from l1.sources import OUT, write_if_changed  # noqa: E402

SRC = REPO_PATH / "src"
GRAPH = OUT / "ast_symbol_graph.json"


def sync_ast(store):
    wms = store.wm_prefix("ast:")
    files = sorted(p for p in SRC.rglob("*") if p.suffix in CODE_EXTS and p.is_file())
    cache = ParseCache()
    seen, moved = set(), False
    entries = []
    for p in files:
        rel = str(p.relative_to(REPO_PATH))
        seen.add("ast:" + rel)
        st = p.stat()
        wm = wms.get("ast:" + rel)
        if wm and wm["size"] == st.st_size and wm["mtime"] == st.st_mtime:
            entries.append((rel, wm["blob"], None))
            continue
        data = p.read_bytes()
        blob = blob_sha(data)
        if not wm or wm["blob"] != blob:
            moved = True
        store.set_wm("ast:" + rel, {"size": st.st_size, "mtime": st.st_mtime, "blob": blob})
        entries.append((rel, blob, data))
    gone = [k for k in wms if k not in seen]
    if gone:
        moved = True
        store.conn.executemany("DELETE FROM watermarks WHERE source=?", [(k,) for k in gone])
        store.conn.commit()
    if not moved and GRAPH.exists() and store.get_wm("astgraph:written"):
        return False
    store.set_wm("astgraph:written", False)  # an interrupted write is redone next sync

    results = []
    for rel, blob, data in graph_order(entries):
        if data is None and (rel, blob) not in cache.mem and not cache.has(rel, blob):
            data = (REPO_PATH / rel).read_bytes()  # cache was cleared; re-read
        results.append(cache.get(rel, blob, BytesReader(data)))
    from build_ast_graph import assemble_graph
    changed = write_if_changed(GRAPH, assemble_graph(results))
    store.set_wm("astgraph:written", True)
    return changed
