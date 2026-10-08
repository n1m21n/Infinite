#!/usr/bin/env python3
"""Design ratchet: per-file counts of literal colours and of raw widget calls may only go down.

  ratchet.py            check against ratchet.json (exit 1 if any file went up or a new file has any)
  ratchet.py --update   lower the stored counts to current (never raises them)
Also verifies Tokens.gen.h matches tokens.json.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
STORE = ROOT / "tools/design/ratchet.json"
RX = re.compile(r"IM_COL32\(\s*\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\)|ImVec4\(\s*[0-9.]+f\s*,\s*[0-9.]+f\s*,\s*[0-9.]+f\s*,\s*[0-9.]+f\s*\)")
# Plan section 5: a surface never calls these; it uses a component. Existing calls are the baseline, new ones fail.
RAW = re.compile(r"ImGui::(Button|SmallButton|Separator|SameLine|PushStyleColor|PushStyleVar)\(")
ALLOW = {"src/app/ui/design/"}  # generated/bridge files and the components themselves

def counts(rx=RX):
    out = {}
    for base in ("src/app", "src/arrange"):
        for f in sorted((ROOT / base).rglob("*")):
            if f.suffix not in (".cpp", ".h", ".mm"): continue
            rel = str(f.relative_to(ROOT))
            if any(rel.startswith(a) for a in ALLOW): continue
            n = len(rx.findall(f.read_text(errors="ignore")))
            if n: out[rel] = n
    return out

def load():
    raw = json.loads(STORE.read_text()) if STORE.exists() else {}
    if "colours" not in raw and "widgets" not in raw:   # old flat format
        raw = {"colours": raw, "widgets": {}}
    return raw.get("colours", {}), raw.get("widgets", {})

def main():
    cur_c, cur_w = counts(RX), counts(RAW)
    old_c, old_w = load()
    if "--update" in sys.argv:
        new_c = {k: min(v, old_c.get(k, v)) for k, v in cur_c.items()}
        new_w = {k: min(v, old_w.get(k, v)) for k, v in cur_w.items()}
        STORE.write_text(json.dumps({"colours": dict(sorted(new_c.items())), "widgets": dict(sorted(new_w.items()))}, indent=1) + "\n")
        print(f"ratchet: {sum(new_c.values())} literal colours in {len(new_c)} files; {sum(new_w.values())} raw widget calls in {len(new_w)} files"); return 0
    bad = [(k, v, old_c.get(k, 0)) for k, v in cur_c.items() if v > old_c.get(k, 0)]
    for k, v, o in bad: print(f"RATCHET: {k} has {v} literal colours (allowed {o}): use a role token")
    badw = [(k, v, old_w.get(k, 0)) for k, v in cur_w.items() if v > old_w.get(k, 0)]
    for k, v, o in badw: print(f"RATCHET: {k} has {v} raw widget calls (allowed {o}): use a component from ui/design/components")
    r = subprocess.run([sys.executable, str(ROOT / "tools/design/build_tokens.py"), "--check"])
    print(f"ratchet: colours {sum(cur_c.values())} (stored {sum(old_c.values())}), raw widgets {sum(cur_w.values())} (stored {sum(old_w.values())})")
    return 1 if bad or badw or r.returncode else 0

if __name__ == "__main__":
    sys.exit(main())
