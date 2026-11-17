#!/usr/bin/env python3
"""Design ratchet: per-file counts of literal colours, raw widget calls and size literals may only go down.

  ratchet.py            check against ratchet.json (exit 1 if any file went up or a new file has any)
  ratchet.py --update   lower the stored counts to current (never raises them)
  ratchet.py            also prints the 10 worst files per count, so each pass picks those first
  ratchet.py --reset    store the current counts as the baseline (once, when a UI system sheet step lands)
Also verifies Tokens.gen.h matches tokens.json.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
STORE = ROOT / "tools/design/ratchet.json"
RX = re.compile(r"IM_COL32\(\s*\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\)|ImVec4\(\s*[0-9.]+f\s*,\s*[0-9.]+f\s*,\s*[0-9.]+f\s*,\s*[0-9.]+f\s*\)")
# Plan section 5: a surface never calls these; it uses a component. Existing calls are the baseline, new ones fail.
RAW = re.compile(r"ImGui::(Button|SmallButton|Separator|SameLine|PushStyleColor|PushStyleVar)\(")
# UI system sheet section 5: a height, icon size or radius is a token, never a number. Counts named size
# constants set to a literal and rect corners given as a literal. Components count too.
SIZE = re.compile(r"(?:constexpr|const)\s+float\s+k\w*(?:H|Height|Row\w*|Radius|Rounding|Icon\w*|Chip\w*|Btn\w*)\s*=\s*[0-9][0-9.]*f?\s*;"
                  r"|AddRect(?:Filled)?\([^;]*?,\s*[0-9][0-9.]*f\s*\)\s*;")
FONT_SCALE = re.compile(r"ImGui::SetWindowFontScale\(|ImGui::GetFontSize\(\)\s*\*\s*0\.(?!5f|74f)[0-9]+f?")
ROUNDING = re.compile(
    r"ImGui::PushStyleVar\(ImGuiStyleVar_(?:Frame|Window|Child|Popup|Tab|Grab)Rounding,\s*[^;]*?[1-9][0-9.]*f"
    r"|style\.(?:Frame|Window|Child|Popup|Tab|Grab)Rounding\s*=\s*[1-9][0-9.]*f"
    r"|ed::PushStyleVar\(ed::StyleVar_NodeRounding,\s*[0-9.]+f"
    r"|float\s+rounding\s*=\s*[0-9.]+f"
)
ALLOW = {"src/app/ui/design/", "src/app/selftest/UiAudit.cpp"}  # components themselves; UiAudit draws raw widgets on purpose as canaries

def counts(rx=RX, allow=None):
    allow = ALLOW if allow is None else allow
    out = {}
    for base in ("src/app", "src/arrange"):
        for f in sorted((ROOT / base).rglob("*")):
            if f.suffix not in (".cpp", ".h", ".mm"): continue
            rel = str(f.relative_to(ROOT))
            if any(rel.startswith(a) for a in allow): continue
            n = len(rx.findall(f.read_text(errors="ignore")))
            if n: out[rel] = n
    return out

def load():
    raw = json.loads(STORE.read_text()) if STORE.exists() else {}
    if "colours" not in raw and "widgets" not in raw:   # old flat format
        raw = {"colours": raw, "widgets": {}}
    return raw.get("colours", {}), raw.get("widgets", {}), raw.get("sizes", {}), raw.get("font_scales", {}), raw.get("roundings", {})

def store(c, w, z, f, rn):
    STORE.write_text(json.dumps({"colours": dict(sorted(c.items())), "widgets": dict(sorted(w.items())),
                                 "sizes": dict(sorted(z.items())), "font_scales": dict(sorted(f.items())),
                                 "roundings": dict(sorted(rn.items()))}, indent=1) + "\n")
    print(f"ratchet: {sum(c.values())} literal colours, {sum(w.values())} raw widget calls, {sum(z.values())} size literals, {sum(f.values())} font scales, {sum(rn.values())} rounding literals")

def main():
    cur_c, cur_w, cur_z, cur_f, cur_r = counts(RX), counts(RAW), counts(SIZE, set()), counts(FONT_SCALE), counts(ROUNDING, set())
    old_c, old_w, old_z, old_f, old_r = load()
    if "--reset" in sys.argv:
        store(cur_c, cur_w, cur_z, cur_f, cur_r); return 0
    if "--update" in sys.argv:
        low = lambda cur, old: {k: min(v, old.get(k, v)) for k, v in cur.items()}
        store(low(cur_c, old_c), low(cur_w, old_w), low(cur_z, old_z), low(cur_f, old_f), low(cur_r, old_r)); return 0
    bad = [(k, v, old_c.get(k, 0)) for k, v in cur_c.items() if v > old_c.get(k, 0)]
    for k, v, o in bad: print(f"RATCHET: {k} has {v} literal colours (allowed {o}): use a role token")
    badw = [(k, v, old_w.get(k, 0)) for k, v in cur_w.items() if v > old_w.get(k, 0)]
    for k, v, o in badw: print(f"RATCHET: {k} has {v} raw widget calls (allowed {o}): use a component from ui/design/components")
    badz = [(k, v, old_z.get(k, 0)) for k, v in cur_z.items() if v > old_z.get(k, 0)]
    for k, v, o in badz: print(f"RATCHET: {k} has {v} size literals (allowed {o}): use control_h / row_h / radius_* / icon_control")
    badf = [(k, v, old_f.get(k, 0)) for k, v in cur_f.items() if v > old_f.get(k, 0)]
    for k, v, o in badf: print(f"RATCHET: {k} has {v} font scales (allowed {o}): use UiType::Push / UiType::Px")
    badr = [(k, v, old_r.get(k, 0)) for k, v in cur_r.items() if v > old_r.get(k, 0)]
    for k, v, o in badr: print(f"RATCHET: {k} has {v} rounding literals (allowed {o}): use tok::radius_control / tok::radius_container")
    for name, cur in (("literal colours", cur_c), ("raw widget calls", cur_w), ("size literals", cur_z), ("font scales", cur_f), ("rounding literals", cur_r)):
        top = sorted(cur.items(), key=lambda kv: -kv[1])[:10]
        print(f"top {name}: " + ", ".join(f"{pathlib.Path(k).name} {v}" for k, v in top))
    r = subprocess.run([sys.executable, str(ROOT / "tools/design/build_tokens.py"), "--check"])
    print(f"ratchet: colours {sum(cur_c.values())} (stored {sum(old_c.values())}), raw widgets {sum(cur_w.values())} (stored {sum(old_w.values())}), "
          f"sizes {sum(cur_z.values())} (stored {sum(old_z.values())}), font scales {sum(cur_f.values())} (stored {sum(old_f.values())}), "
          f"roundings {sum(cur_r.values())} (stored {sum(old_r.values())})")
    return 1 if bad or badw or badz or badf or badr or r.returncode else 0

if __name__ == "__main__":
    sys.exit(main())
