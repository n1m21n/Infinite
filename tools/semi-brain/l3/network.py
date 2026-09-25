#!/usr/bin/env python3
"""
network.py
The L3 network: typed edges between what the brain retrieves and the code it is about.

  touches          commit   -> file    files the commit changed (merges: vs first parent)
  mentions_file    session / explainer / skill / plan / research -> file
                                       src/... paths, or a bare file name that names exactly
                                       one file in the tree
  mentions_symbol  same sources -> symbol   Qualified names (Foo::Bar) that resolve in the AST
  defines          file -> symbol      from the AST graph
  calls            symbol -> symbol    from the AST graph
  includes         file -> file        from the AST graph (bare include names resolved)

Doc edges are keyed by the same doc_ids index_codebase.prepare_documents gives the docs, so a
retrieved hit is a node in the network as-is. Everything is derived from the corpora by a pure
function (build_doc_edges), so the replay benchmark builds the network as it stood at each
past commit, and the sync rebuilds it in well under a second.

activate() is the query-time use: spreading activation from the retrieved hits (seeds) along
doc edges to files, then from a file's score to the symbols it defines.
"""

import json
import math
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import STATE_DIR  # noqa: E402

NETWORK_FILE = STATE_DIR / "network.json"  # holds chat-derived edges: local only, like l1/state

CODE_EXT = r"(?:cpp|h|hpp|mm|m|c|glsl|metal|frag|vert)"
PATH_RE = re.compile(r"\b((?:src|tests)/[\w/.+-]+?\." + CODE_EXT + r")\b")
BASENAME_RE = re.compile(r"\b([A-Za-z_][\w+-]*\." + CODE_EXT + r")\b")
QUALIFIED_RE = re.compile(r"\b([A-Z]\w*(?:::\w+)+)\b")

# Seed weight per compartment: how much a hit there says about which code is involved.
SEED_WEIGHT = {"history": 1.0, "code": 1.0, "conversations": 0.6, "research": 0.6,
               "plans": 0.4, "skills": 0.25, "external": 0.0}


class CodeIndex:
    """File and symbol lookups over one AST graph."""

    def __init__(self, ast_graph):
        symbols = (ast_graph or {}).get("symbols", {})
        self.sym_file = {name: m.get("file", "") for name, m in symbols.items()}
        self.file_syms = defaultdict(list)
        for name, f in self.sym_file.items():
            if f:
                self.file_syms[f].append(name)
        self.files = set(self.file_syms) | set((ast_graph or {}).get("file_includes", {}))
        self.by_base = defaultdict(list)
        for f in self.files:
            self.by_base[f.rsplit("/", 1)[-1]].append(f)
        self.includes = {}
        for f, incs in ((ast_graph or {}).get("file_includes", {}) or {}).items():
            self.includes[f] = [self.by_base[i.rsplit("/", 1)[-1]][0] for i in incs
                                if len(self.by_base.get(i.rsplit("/", 1)[-1], ())) == 1]

    def resolve_file(self, name):
        if name in self.files:
            return name
        hits = self.by_base.get(name.rsplit("/", 1)[-1], ())
        return hits[0] if len(hits) == 1 else None

    def mentions(self, text):
        files, syms = set(), set()
        for p in PATH_RE.findall(text):
            f = self.resolve_file(p)
            if f:
                files.add(f)
        for b in BASENAME_RE.findall(text):
            f = self.resolve_file(b)
            if f:
                files.add(f)
        for q in QUALIFIED_RE.findall(text):
            if q in self.sym_file:
                syms.add(q)
        return files, syms


def _add(edges, src, kind, dsts):
    if dsts:
        w = 1.0 / math.sqrt(len(dsts))
        edges.setdefault(src, []).extend((kind, d, w) for d in sorted(dsts))


def build_doc_edges(corpora, code=None):
    """{doc_id: [(edge_type, dst, weight)]} for every doc that points at code. A doc's edges of
    one type share weight 1/sqrt(n), so a 60-file merge says less about each file than a
    one-file fix."""
    code = code or CodeIndex(corpora.get("ast_data"))
    from l1.research import is_explainer

    edges = {}
    for c in corpora.get("commits") or []:
        files = {f for f in (c.get("files") or []) if f in code.files}
        _add(edges, f"commit::{c.get('hash', '')[:8]}", "touches", files)

    def text_edges(doc_id, text):
        files, syms = code.mentions(text or "")
        _add(edges, doc_id, "mentions_file", files)
        _add(edges, doc_id, "mentions_symbol", syms)

    ordinal = {}
    for turn in corpora.get("sessions") or []:
        key = (turn.get("source_tool", "claude_code"), turn.get("session_id", ""))
        n = ordinal[key] = ordinal.get(key, -1) + 1
        if not (turn.get("user_text") or "").strip():
            continue
        prefix = "explainer" if is_explainer(turn) else "session"
        text_edges(f"{prefix}::{key[0]}::{key[1]}::{n}",
                   f"{turn.get('user_text', '')}\n{turn.get('assistant_text', '')}")
    for s in corpora.get("skills") or []:
        text_edges(f"skill::{s.get('name', '')}", s.get("content", ""))
    for d in corpora.get("docs") or []:
        text_edges(f"plan::{d.get('path', '')}", d.get("content", ""))
    for d in corpora.get("research_docs") or []:
        text_edges(f"research::{d['path']}", d.get("content", ""))
    return edges


class Network:
    def __init__(self, doc_edges, ast_graph):
        self.doc_edges = doc_edges or {}
        self.code = CodeIndex(ast_graph)

    @classmethod
    def load(cls, ast_graph, path=NETWORK_FILE):
        try:
            edges = json.loads(Path(path).read_text())
        except (OSError, ValueError):
            edges = {}
        return cls({k: [tuple(e) for e in v] for k, v in edges.items()}, ast_graph)

    def activate(self, seeds):
        """seeds: [(hit, weight)], hit a retriever result dict. Returns (file_scores,
        symbol_scores): activation that reached each file and symbol, with the doc ids that
        carried it (why[file] = [doc_id, ...])."""
        fscore, sscore, why = defaultdict(float), defaultdict(float), defaultdict(list)
        for hit, w in seeds:
            if w <= 0:
                continue
            did = hit["doc_id"]
            if hit.get("category") == "ast_symbol":
                sym = did.split("::", 1)[1]
                f = self.code.sym_file.get(sym)
                sscore[sym] += w
                if f:
                    fscore[f] += w
                    why[f].append(did)
                continue
            for kind, dst, ew in self.doc_edges.get(did, ()):
                if kind == "mentions_symbol":
                    sscore[dst] += w * ew
                    f = self.code.sym_file.get(dst)
                    if f:
                        fscore[f] += w * ew
                        why[f].append(did)
                else:
                    fscore[dst] += w * ew
                    why[dst].append(did)
        return fscore, sscore, why


def write_network(corpora, path=NETWORK_FILE):
    """Rebuild the doc edges from the corpora; rewrite the file only if they changed."""
    data = json.dumps(build_doc_edges(corpora), sort_keys=True)
    path = Path(path)
    if path.exists() and path.read_text() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    tmp.write_text(data)
    tmp.replace(path)
    return True
