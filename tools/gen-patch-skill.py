#!/usr/bin/env python3
"""Regenerate the node facts in the infinite-patch-authoring skill.

Runs `Infinite --describe --json`, rewrites the block between the generated markers in
.claude/skills/infinite-patch-authoring/SKILL.md, and derives the two copies that ship:
docs/ai-skills/infinite-patch-authoring.md and the kPatchAuthoringMarkdown constant in
src/core/AISkillContent.h. Only the loop, rules and troubleshooting prose are hand-written.

  tools/gen-patch-skill.py [--bin path/to/Infinite]   rewrite the three files
  tools/gen-patch-skill.py --check [--bin ...]        exit 1 if any of them is stale (CI)
"""
import argparse, json, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKILL = os.path.join(ROOT, ".claude/skills/infinite-patch-authoring/SKILL.md")
DOCS = os.path.join(ROOT, "docs/ai-skills/infinite-patch-authoring.md")
HEADER = os.path.join(ROOT, "src/core/AISkillContent.h")
BEGIN, END = "<!-- generated:begin -->", "<!-- generated:end -->"
CONST_RE = re.compile(r'   inline const char\* kPatchAuthoringMarkdown =\n.*?\)AISKILL";\n', re.S)
CHUNK = 12000  # MSVC rejects a single string literal over ~16 KB; adjacent raw literals concatenate
DEFAULT_BIN = os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")


def describe(binary):
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "all.json")
        subprocess.run([binary, "--describe", "--json", out], check=True, stdout=subprocess.DEVNULL)
        with open(out) as f:
            return json.load(f)


def sig(slots, key):
    if not slots:
        return "none"
    if key == "in":
        return ", ".join(f"{s.get('name') or s['kind']}:{s['kind']}" for s in slots)
    return ", ".join(f"{s.get('label') or 'out'}:{s['kind']}" for s in slots)


def generate(desc):
    types = sorted(desc["types"], key=lambda t: (t["category"], t["type"]))
    lines = [f"{len(types)} node types. `node <index> <category> <type>`; inputs are `slot name:kind`, "
             "outputs `label:kind`; `p` is the number of saved parameters; `bypass` marks single-input "
             "nodes that can be bypassed.", ""]
    cat = None
    for t in types:
        if t["category"] != cat:
            cat = t["category"]
            lines += ["", f"### {cat}", "", "| Type | Inputs | Outputs | p | Notes |", "|---|---|---|---|---|"]
        notes = []
        if t.get("hardware_driven"):
            notes.append("hardware: refused headless")
        if t.get("can_bypass"):
            notes.append("bypass")
        lines.append(f"| `{t['type']}` | {sig(t['inputs'], 'in')} | {sig(t['outputs'], 'out')} | "
                     f"{len(t['params'])} | {', '.join(notes)} |")
    return "\n".join(lines).strip("\n")


def cpp_constant(text):
    """The text as adjacent raw literals, each cut at a line boundary under CHUNK bytes."""
    chunks, cur = [], ""
    for line in text.splitlines(keepends=True):
        if cur and len((cur + line).encode()) > CHUNK:
            chunks.append(cur)
            cur = ""
        cur += line
    chunks.append(cur)
    body = "\n".join(f'R"AISKILL({c})AISKILL"' for c in chunks)
    return "   inline const char* kPatchAuthoringMarkdown =\n" + body + ";\n"


def splice(text, block):
    i, j = text.index(BEGIN), text.index(END)
    return text[: i + len(BEGIN)] + "\n" + block + "\n" + text[j:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    with open(SKILL) as f:
        skill = splice(f.read(), generate(describe(a.bin)))
    assert ")AISKILL" not in skill
    with open(HEADER) as f:
        header = f.read()
    const_text = cpp_constant(skill)
    if CONST_RE.search(header):
        new_header = CONST_RE.sub(lambda m: const_text, header, count=1)
    else:
        tail = header.rindex("}")
        new_header = header[:tail] + "\n" + const_text + header[tail:]
    want = {SKILL: skill, DOCS: skill, HEADER: new_header}

    stale = []
    for path, content in want.items():
        cur = open(path).read() if os.path.exists(path) else None
        if cur != content:
            stale.append(os.path.relpath(path, ROOT))
            if not a.check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as f:
                    f.write(content)
    if a.check and stale:
        print("skill is stale, run tools/gen-patch-skill.py:", ", ".join(stale))
        return 1
    print("up to date" if not stale else "wrote " + ", ".join(stale))
    return 0


if __name__ == "__main__":
    sys.exit(main())
