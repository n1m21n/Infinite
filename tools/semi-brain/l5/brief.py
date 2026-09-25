#!/usr/bin/env python3
"""
brief.py
L5: turn an analyze_problem() frame into a short brief (about 200 tokens) for a person or for
the prompt hook, or into the same content as JSON.

  Files     top files, each with the evidence that put it there (a past commit, a chat, a
            symbol hit)
  Symbols   top symbols with file:line
  Area      the L4 area the top file sits in, named by its most connected files
  Past      the best past commits for this problem
  Skills    skills worth loading: skills compartment hits, promoted when the skill's text
            names one of the top files (L3 edge)
  Before    the best research-compartment hit (research doc or mined explainer)

Every line is cut to fit; the whole brief stays under BUDGET characters (~4 chars per token).
"""

import re
import sqlite3

BUDGET = 900
N_FILES, N_SYMBOLS, N_PAST, N_SKILLS = 4, 4, 2, 2


def _short(path):
    return path[4:] if path.startswith("src/") else path


def _title_of(frame, doc_id):
    for hits in frame.compartments.values():
        for h in hits:
            if h["doc_id"] == doc_id:
                return h.get("title", "")
    return ""


def _why(frame, doc_ids):
    """The strongest non-symbol evidence for a file, as a few words."""
    for d in doc_ids:
        kind = d.split("::", 1)[0]
        if kind == "commit":
            title = _title_of(frame, d).split("]: ", 1)[-1]
            return f"{d.split('::')[1][:7]} {title[:48]}"
        if kind in ("session", "explainer"):
            return "chat: " + _title_of(frame, d).split(": ", 1)[-1][:40]
        if kind in ("skill", "plan", "research"):
            return f"{kind} {d.split('::', 1)[1].rsplit('/', 1)[-1][:40]}"
    return ""


HEADING_RE = re.compile(r"(?m)^(?:#{1,4} +(.+)|\*\*([^*\n]{8,80})\*\*\s*$)")


def research_line(engine, frame):
    """The best research hit, named by what it says: a research doc by its title, a mined
    explainer by the first heading of its answer (its title is the user's question, often
    just "can you explain...")."""
    for h in frame.compartments.get("research", [])[:3]:
        if h["category"] == "research_doc":
            return h["title"].replace("Research: ", "")
        content = _content(engine, h["doc_id"])
        answer = content.split("\n\n", 1)[-1]
        m = HEADING_RE.search(answer)
        if m:
            return "explainer: " + (m.group(1) or m.group(2)).strip(" #*")
    return ""


def _content(engine, doc_id):
    for db in engine.retriever.db_paths:
        try:
            conn = sqlite3.connect(str(db))
            row = conn.execute("SELECT content FROM fts_documents WHERE doc_id=?", (doc_id,)).fetchone()
            conn.close()
        except sqlite3.Error:
            row = None
        if row:
            return row[0]
    return ""


def rank_skills(engine, frame):
    """Skills compartment hits, promoted when the skill's own text points at the top files."""
    network = getattr(engine, "network", None)
    top = set(frame.ranked_files[:N_FILES])
    scored = []
    for rank, h in enumerate(frame.compartments.get("skills", [])[:10]):
        score = 1.0 / (rank + 1)
        if network is not None:
            score += sum(1.0 for kind, dst, _ in network.doc_edges.get(h["doc_id"], ())
                         if kind == "mentions_file" and dst in top)
        scored.append((score, h["title"].replace("Skill: ", "")))
    return [name for _, name in sorted(scored, key=lambda x: -x[0])]


def area_name(engine, path):
    areas = engine._areas() if hasattr(engine, "_areas") else None
    if areas is None or path not in areas.area:
        return ""
    members = areas.members[areas.area[path]]
    hubs = sorted(members, key=lambda f: -sum(areas.adj[f].values()))[:3]
    return f"{len(members)} files around " + ", ".join(_short(h).rsplit("/", 1)[-1] for h in hubs)


def brief_data(engine, frame):
    files = []
    for f in frame.ranked_files[:N_FILES]:
        files.append({"file": f, "why": _why(frame, frame.file_evidence.get(f, []))})
    symbols = []
    for s in frame.ast_impacted_symbols[:N_SYMBOLS]:
        m = engine._get_symbol_meta(s)
        symbols.append({"symbol": s, "at": f"{_short(m['file'])}:{m['line']}" if m["file"] else ""})
    past = [h["title"].replace("Commit [", "").replace("]:", "") for h in frame.compartments.get("history", [])[:N_PAST]]
    skills = rank_skills(engine, frame)[:N_SKILLS]
    before = research_line(engine, frame)
    return {"query": frame.raw_query, "files": files, "symbols": symbols,
            "area": area_name(engine, frame.ranked_files[0]) if frame.ranked_files else "",
            "past": past, "skills": skills, "before": before}


def render(data, budget=BUDGET):
    lines = []
    if data["files"]:
        lines.append("Files: " + "; ".join(
            _short(f["file"]) + (f" ({f['why']})" if f["why"] else "") for f in data["files"]))
    if data["symbols"]:
        lines.append("Symbols: " + ", ".join(
            s["symbol"] + (f" {s['at']}" if s["at"] else "") for s in data["symbols"]))
    if data["area"]:
        lines.append("Area: " + data["area"])
    if data["past"]:
        lines.append("Past fixes: " + " | ".join(p[:70] for p in data["past"]))
    if data["skills"]:
        lines.append("Load skills: " + ", ".join(data["skills"]))
    if data["before"]:
        lines.append("Discussed before: " + data["before"][:80])
    out, used = [], 0
    for line in lines:
        line = line if len(line) <= 260 else line[:257] + "..."
        if used + len(line) + 1 > budget:
            break
        out.append(line)
        used += len(line) + 1
    return "\n".join(out)


def brief(engine, query, budget=BUDGET):
    return render(brief_data(engine, engine.analyze_problem(query)), budget)
