#!/usr/bin/env python3
"""Rule audit: the input to the monthly rule loop (infinite-lifecycle section 10).

Inventories every standing instruction (AGENTS.md, CLAUDE.md, .claude/skills/*/SKILL.md, agents,
and optionally the memory directory) and reports what to bring to the owner: size, staleness,
catalogue drift, memories with no reason, broken links and supersede chains. It never edits or
deletes anything; the agent retires superseded or duplicated rules, the owner the rest.

    python3 tools/lifecycle/rule_audit.py --memory ~/.claude/projects/<project>/memory
    python3 tools/lifecycle/rule_audit.py --out docs/lifecycle/rule-audit.md
"""
from __future__ import annotations

import argparse
import re
import subprocess
from datetime import date, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SKILLS = ROOT / ".claude" / "skills"
AGENTS_DIR = ROOT / ".claude" / "agents"


def words(p: Path) -> int:
    return len(p.read_text(errors="ignore").split())


def git_last(p: Path) -> str:
    out = subprocess.run(["git", "log", "-1", "--format=%ad", "--date=short", "--", str(p)],
                         cwd=ROOT, capture_output=True, text=True).stdout.strip()
    return out or "uncommitted"


def frontmatter(text: str) -> dict:
    m = re.match(r"^---\n(.*?)\n---", text, re.S)
    out = {}
    for line in (m.group(1).splitlines() if m else []):
        k, _, v = line.partition(":")
        out[k.strip()] = v.strip().strip('"')
    return out


def age_days(d: str) -> int | None:
    try:
        return (date.today() - datetime.strptime(d[:10], "%Y-%m-%d").date()).days
    except ValueError:
        return None


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--memory", type=Path, help="memory directory (MEMORY.md + one file per memory)")
    ap.add_argument("--top", type=int, default=10)
    ap.add_argument("--out", type=Path, help="write the report here instead of stdout")
    args = ap.parse_args()

    out: list[str] = [f"# Rule audit ({date.today()})", ""]

    # Always-loaded context: what every session pays for before doing anything.
    always = [p for p in (ROOT / "AGENTS.md", ROOT / "CLAUDE.md") if p.exists()]
    if args.memory and (args.memory / "MEMORY.md").exists():
        always.append(args.memory / "MEMORY.md")
    out += ["## Always loaded", "", "| File | Words |", "|---|---|"]
    out += [f"| {p.name} | {words(p)} |" for p in always]
    out += [f"| **total** | **{sum(words(p) for p in always)}** |", ""]

    # Skills: size, last change, and drift against the AGENTS.md catalogue.
    skills = sorted(p for p in SKILLS.glob("*/SKILL.md"))
    agents_md = (ROOT / "AGENTS.md").read_text() if (ROOT / "AGENTS.md").exists() else ""
    listed = set(re.findall(r"^\| `([a-z0-9-]+)` \|", agents_md, re.M))
    names = {p.parent.name for p in skills}
    rows = sorted(((words(p), p.parent.name, git_last(p)) for p in skills), reverse=True)
    total = sum(w for w, _, _ in rows)
    out += [f"## Skills ({len(rows)}, {total} words, loaded on demand)", "",
            f"Largest {args.top}:", "", "| Skill | Words | Last changed |", "|---|---|---|"]
    out += [f"| {n} | {w} | {d} |" for w, n, d in rows[: args.top]]
    out.append("")
    no_desc = [p.parent.name for p in skills if not frontmatter(p.read_text(errors="ignore")).get("description")]
    not_listed = sorted(names - listed)
    agent_names = {p.stem for p in AGENTS_DIR.glob("*.md")} if AGENTS_DIR.exists() else set()
    dangling = sorted(listed - names - agent_names)
    out += ["### Catalogue drift", "",
            f"- Skills with no `description` (never auto-trigger): {', '.join(no_desc) or 'none'}",
            f"- Skills missing from the AGENTS.md table: {', '.join(not_listed) or 'none'}",
            f"- AGENTS.md rows with no skill directory: {', '.join(dangling) or 'none'}", ""]

    agent_files = sorted(AGENTS_DIR.glob("*.md")) if AGENTS_DIR.exists() else []
    if agent_files:
        out += [f"## Agents ({len(agent_files)})", ""]
        out += [f"- {p.stem}: {words(p)} words, last changed {git_last(p)}" for p in agent_files]
        out.append("")

    # Memories: the rules most likely to collide, because they are written one incident at a time.
    if args.memory and args.memory.exists():
        mems = sorted(p for p in args.memory.glob("*.md") if p.name != "MEMORY.md")
        index = (args.memory / "MEMORY.md").read_text() if (args.memory / "MEMORY.md").exists() else ""
        slugs, no_why, supers, broken, unindexed, ages = {}, [], [], [], [], []
        texts = {p: p.read_text(errors="ignore") for p in mems}
        def norm(x: str) -> str:
            x = x.lower().replace("_", "-")
            return re.sub(r"^(feedback|project|reference|user)-", "", x)

        for p, t in texts.items():
            for key in (frontmatter(t).get("name", ""), p.stem):
                if key:
                    slugs[norm(key)] = p
        for p, t in texts.items():
            fm = frontmatter(t)
            if fm.get("type") in ("feedback", "project") and "**Why:**" not in t:
                no_why.append(p.stem)
            if re.search(r"\bsupersed", t, re.I):
                supers.append(p.stem)
            broken += [f"{p.stem} → [[{l}]]" for l in re.findall(r"\[\[([^\]]+)\]\]", t) if norm(l) not in slugs]
            if p.name not in index:
                unindexed.append(p.stem)
            m = re.search(r"modified:\s*(\S+)", t)
            ages.append((age_days(m.group(1)) if m else None, p.stem, words(p)))
        out += [f"## Memories ({len(mems)}, {sum(len(t.split()) for t in texts.values())} words)", "",
                f"- feedback/project memories with no **Why:** (can't be re-tested): {', '.join(no_why) or 'none'}",
                f"- mention superseding another rule (check the old one is gone): {', '.join(supers) or 'none'}",
                f"- broken `[[links]]`: {', '.join(broken) or 'none'}",
                f"- files not in MEMORY.md: {', '.join(unindexed) or 'none'}", ""]
        dated = sorted((a, n, w) for a, n, w in ages if a is not None)[::-1]
        out += [f"Oldest {args.top} (re-test these first):", "", "| Memory | Days since modified | Words |", "|---|---|---|"]
        out += [f"| {n} | {a} | {w} |" for a, n, w in dated[: args.top]]
        out.append("")

    out += ["## Next step", "",
            "Bring the top five items to the owner one at a time: keep, merge into the owning skill, or retire.",
            "Record each answer that matters later in docs/DECISIONS.md."]
    text = "\n".join(out) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text)
        print(f"wrote {args.out}")
    else:
        print(text)


if __name__ == "__main__":
    main()
