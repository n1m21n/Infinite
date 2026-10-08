#!/usr/bin/env python3
"""Design ratchet: per-file counts of literal colours may only go down.

  ratchet.py            check against ratchet.json (exit 1 if any file went up or a new file has any)
  ratchet.py --update   lower the stored counts to current (never raises them)
Also verifies Tokens.gen.h matches tokens.json.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
STORE = ROOT / "tools/design/ratchet.json"
RX = re.compile(r"IM_COL32\(\s*\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\)|ImVec4\(\s*[0-9.]+f\s*,\s*[0-9.]+f\s*,\s*[0-9.]+f\s*,\s*[0-9.]+f\s*\)")
ALLOW = {"src/app/ui/design/"}  # generated/bridge files

def counts():
    out = {}
    for base in ("src/app", "src/arrange"):
        for f in sorted((ROOT / base).rglob("*")):
            if f.suffix not in (".cpp", ".h", ".mm"): continue
            rel = str(f.relative_to(ROOT))
            if any(rel.startswith(a) for a in ALLOW): continue
            n = len(RX.findall(f.read_text(errors="ignore")))
            if n: out[rel] = n
    return out

def main():
    cur = counts()
    old = json.loads(STORE.read_text()) if STORE.exists() else {}
    if "--update" in sys.argv:
        new = {k: min(v, old.get(k, v)) for k, v in cur.items()}
        STORE.write_text(json.dumps(dict(sorted(new.items())), indent=1) + "\n")
        print(f"ratchet: {sum(new.values())} literal colours in {len(new)} files"); return 0
    bad = [(k, v, old.get(k, 0)) for k, v in cur.items() if v > old.get(k, 0)]
    for k, v, o in bad: print(f"RATCHET: {k} has {v} literal colours (allowed {o}): use a role token")
    r = subprocess.run([sys.executable, str(ROOT / "tools/design/build_tokens.py"), "--check"])
    print(f"ratchet: total {sum(cur.values())} (stored {sum(old.values())})")
    return 1 if bad or r.returncode else 0

if __name__ == "__main__":
    sys.exit(main())
