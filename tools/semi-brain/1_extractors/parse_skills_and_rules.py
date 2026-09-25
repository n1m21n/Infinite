#!/usr/bin/env python3
"""
parse_skills_and_rules.py
Ingests all current skills from .claude/skills and AGENTS.md, extracting their
invariants, verification rules, bug-hunting questions, and domain constraints.
"""

import json
import os
import re
from pathlib import Path

REPO_PATH = Path(__file__).resolve().parents[3]
OUTPUT_DIR = Path(__file__).resolve().parent / "output"
OUTPUT_FILE = OUTPUT_DIR / "skills_and_invariants_corpus.json"

def collect_records():
    """AGENTS.md and every .claude/skills/*/SKILL.md, as corpus records."""
    skills_dir = REPO_PATH / ".claude" / "skills"
    agents_md = REPO_PATH / "AGENTS.md"
    
    records = []
    
    # Ingest AGENTS.md
    if agents_md.exists():
        records.append({
            "type": "agents_rule",
            "name": "AGENTS.md",
            "path": "AGENTS.md",
            "content": agents_md.read_text(encoding="utf-8")
        })
        
    # Ingest all skills
    if skills_dir.exists():
        for skill_path in sorted(skills_dir.glob("*/SKILL.md")):
            skill_name = skill_path.parent.name
            content = skill_path.read_text(encoding="utf-8")
            
            # Extract frontmatter description if present
            desc_match = re.search(r"description:\s*(.*?)\n(?:user-facing|version|category|---)", content, re.DOTALL)
            description = desc_match.group(1).strip() if desc_match else ""
            
            records.append({
                "type": "skill",
                "name": skill_name,
                "path": str(skill_path.relative_to(REPO_PATH)),
                "description": description,
                "content": content
            })
    return records


def parse_skills():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    records = collect_records()
    print(f"Parsed {len(records)} skills and rules.")
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        json.dump(records, f, indent=2)
    print(f"Saved skills and invariants corpus to {OUTPUT_FILE}")

if __name__ == "__main__":
    parse_skills()
