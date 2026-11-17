#!/usr/bin/env python3
"""Context-menu order lint: every right-click menu lists the same actions in the same order.

Static source scan (no app build). The canonical order and the full inventory live in
docs/quality/context-menu-order.md. Within one menu, items run in this order:

  1. Rename / Edit                 (rank 10)
  2. Cut, Copy, Paste, Duplicate   (rank 20..23)
  3. State toggles                 (rank 30: Mute, Solo, Active, Hide, Bypass, Enable, Turn Group On/Off)
  4. everything else               (submenus, view, actions: unranked, free order)
  5. Delete*                       (rank 90)
  6. Settings                      (rank 95: the very last row, under Delete - the way on to more settings)

Rules checked per menu (branches that exclude each other, such as the group / note / node variants of the
node menu, are checked separately):
  order     two ranked items out of order, or any item after a Delete*
  wording   a synonym where the canonical word exists (bare "Remove" for Delete, "Clone" for Duplicate ...)
  hint      a shortcut hint that differs from the canonical one for that action (literal Cmd+/Ctrl+ instead of
            MODKEY, "Del" or "Delete" instead of "Backspace" ...)
  registry  a popup whose id says ctx/context that the registry below does not know (add it to the registry and to
            the doc), or a registry entry whose menu no longer exists

  context_menu_order.py            check against context_menu_baseline.json (exit 1 on a new violation)
  context_menu_order.py --update   rewrite the baseline to the current violations (never adds silently in CI)
  context_menu_order.py --table    print the inventory as a markdown table (for the doc)
"""
import json, os, pathlib, re, sys

ROOT = pathlib.Path(os.environ.get("CMO_ROOT") or pathlib.Path(__file__).resolve().parents[2])
BASELINE = pathlib.Path(__file__).resolve().parent / "context_menu_baseline.json"

# ---------------------------------------------------------------- registry
# popup id -> short name. Every menu here is checked. A popup whose id contains "ctx" or "context" must be here.
MENUS = {
    "##nodecontext": "Node (group / note / node variants)",
    "##arrangeclipctx": "Arrange clip (timeline + session slot)",
    "##arrangemarkerctx": "Arrange marker",
    "##arrangelanectx": "Arrange track header",
    "##arrangegroupctx": "Arrange track-group header",
    "##arrangeemptyspacectx": "Arrange empty track area",
    "##arrangeviewportctx": "Arrange viewport dock",
    "##sessiontrackctx": "Session track",
    "##sessionslotctx": "Session empty slot",
    "##sessionscenectx": "Session scene",
    "##sessiondockctx": "Session dock",
    "##studioWaveMenu": "Sample studio waveform / slice",
    "##curveMenu": "Curve editor",
    "##v3dcontext": "3D view (object / empty space)",
    "##v3dselmode": "3D view select tool",
    "##viewportpanelctx": "Viewport panel dock",
    "##modmatrixctx": "Mod matrix dock",
    "##curve_ctx": "Mod matrix curve",
    "##elemcontext": "Performance control",
    "##perfcanvascontext": "Performance canvas",
    "##entry_ctx": "Library sample / media entry",
    "##plugin_ctx": "Library plugin entry",
    "##field_ctx": "Library field preset",
    "##mod_ctx": "Side panel module (search)",
    "##mod_cat_ctx": "Side panel module (category)",
    "##terminalDockMenu": "Terminal dock",
    "##mixerpanelctx": "Mixer panel",
    "##stripctx": "Mixer strip",
    "##insctx": "Mixer insert",
    "##drumDockMenu": "Drum dock",
    "##fpDockMenu": "Field patcher dock",
    "##fpNodeMenu": "Field patcher node",
    "##fpLinkMenu": "Field patcher link",
    "##fpStripMenu": "Field patcher strip",
    "##fpFooterMenu": "Field patcher footer",
    "##fpEmptyMenu": "Field patcher empty space",
    "##v2dcanvasmenu": "2D view canvas",
    "##v2ddockmenu": "2D view dock",
    "##rowmenu": "2D view layer row",
}
# Menus that are a function body, not a popup block: id -> (file, function name).
FRAGMENTS = {
    "pianoFx": ("src/app/panels/PianoRollPanel.cpp", "DrawContextMenu", "Piano roll note / clip / empty stretch"),
    "v3dAddItems": ("src/app/panels/View3DPanel.cpp", "DrawAddItems", "3D view Add (+ scene) items"),
}
BEGINS = ("MenuParts::BeginPopup", "BeginPickerMenu", "MenuParts::BeginContextItem", "MenuParts::BeginContextWindow")

# ---------------------------------------------------------------- canonical order
def L(*a): return re.compile("^(?:" + "|".join(a) + ")$")
RANKS = [
    (10, L(r"Rename", r"Rename (?:Track|Group|Page|Marker)", r"Edit Note")),
    (20, L(r"Cut")),
    (21, L(r"Copy", r"Copy scene")),
    (22, L(r"Paste", r"Paste at [Pp]layhead", r"Paste scene")),
    (23, L(r"Duplicate", r"Duplicate (?:Page|Group|Track|Scene|scene)")),
    (30, L(r"Mute", r"Solo", r"Active", r"Hide", r"Bypass(?: Node| selection)?", r"Enable Node", r"Toggle Enabled",
           r"Turn Group (?:On|Off)")),
    (95, L(r"Settings\.*", r"Clip Settings")),
    (90, re.compile(r"^(?:Delete\b(?!.*Mask)|Remove$)")),   # the 3D view keeps Blender's "Remove" (X)
]
# Cut / Copy / Paste are only edit actions inside a context menu; a tool picker's "Cut" (blade tool) is not one.
WORDING = [  # (label regex, what to use)
    (re.compile(r"^(?:Clone|Dupe|Copy Here)$"), 'use "Duplicate"'),
    (re.compile(r"^(?:Erase|Trash|Clear Selection)$"), 'use "Delete"'),
    (re.compile(r"^Preferences\.*$"), 'use "Settings"'),
    (re.compile(r"^Rename\.\.\.$"), 'use "Rename"'),
]
# Canonical hint per action (None = no hint is fine too). A set means any of them.
HINTS = [
    (re.compile(r"^(?:Delete\b(?!.*Mask)|Remove$)"), {"Backspace", "X", "Delete"}, "Backspace (3D view: X)"),
    (re.compile(r"^Duplicate(?: (?:Page|Group|Track|Scene|scene))?$"), {"MODKEY+D", "Shift+D"}, "MODKEY+D (3D view and Piano Roll: Shift+D)"),
    (re.compile(r"^Cut$"), {"MODKEY+X"}, "MODKEY+X"),
    (re.compile(r"^Copy(?: scene)?$"), {"MODKEY+C"}, "MODKEY+C"),
    (re.compile(r"^(?:Paste|Paste at [Pp]layhead|Paste scene)$"), {"MODKEY+V"}, "MODKEY+V"),
    (re.compile(r"^Rename(?: (?:Track|Group|Page|Marker))?$"), {"MODKEY+R", "F2"}, "MODKEY+R"),
    (re.compile(r"^Mute$"), {"0", "M"}, "0 (note / clip) or M (slice)"),
]

# ---------------------------------------------------------------- source parsing
def mask(src):
    """Same length as src; comments and the inside of string / char literals blanked so braces and parens count."""
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j): out[k] = " "
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if src[k] != "\n": out[k] = " "
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                if src[j] == "\\": j += 1
                j += 1
            for k in range(i + 1, min(j, n)): out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)

class Block:
    __slots__ = ("id", "parent", "start", "end", "chain", "last_child", "submenu")
    def __init__(self, id, parent, start, chain):
        self.id, self.parent, self.start, self.end, self.chain = id, parent, start, None, chain
        self.last_child, self.submenu = None, False

def blocks_of(m):
    blocks, stack = [], []
    root = Block(0, None, 0, 0)
    root.end = len(m)
    blocks.append(root)
    for i, c in enumerate(m):
        if c == "{":
            parent = stack[-1] if stack else root
            k = max(m.rfind(";", 0, i), m.rfind("{", 0, i), m.rfind("}", 0, i))
            header = m[k + 1:i].strip()
            if header.startswith("else") and parent.last_child is not None:
                chain = parent.last_child.chain
            else:
                chain = len(blocks) + 100000
            b = Block(len(blocks), parent, i, chain)
            # A lambda body is a helper defined ahead of its call site, not menu order: skipped like a submenu.
            if re.search(r"\]\s*\([^()]*\)\s*(?:mutable\s*)?$", header):
                b.submenu = True
            blocks.append(b)
            parent.last_child = b
            stack.append(b)
        elif c == "}" and stack:
            stack.pop().end = i
    return blocks

def match_paren(m, i):
    """Index of the ')' matching the '(' at i."""
    d = 0
    for j in range(i, len(m)):
        if m[j] == "(": d += 1
        elif m[j] == ")":
            d -= 1
            if d == 0: return j
    return len(m) - 1

def split_args(src, m, open_i, close_i):
    args, d, cur = [], 0, open_i + 1
    for j in range(open_i + 1, close_i):
        if m[j] in "([{": d += 1
        elif m[j] in ")]}": d -= 1
        elif m[j] == "," and d == 0:
            args.append(src[cur:j].strip()); cur = j + 1
    args.append(src[cur:close_i].strip())
    return args

LIT = re.compile(r'^(?:[LT]\()?\s*"((?:[^"\\]|\\.)*)"\s*\)?$')
TERN = re.compile(r'^[^?"]*\?\s*(?:[LT]\()?\s*"((?:[^"\\]|\\.)*)"\s*\)?\s*:\s*(?:[LT]\()?\s*"(?:[^"\\]|\\.)*"\s*\)?$')
def label_of(arg):
    mm = LIT.match(arg) or TERN.match(arg)   # `cond ? L("On") : L("Off")` is ranked by its first label
    return mm.group(1) if mm else None

def hint_of(arg):
    if arg is None or arg == "nullptr": return None
    arg = arg.strip()
    if arg.startswith("MODKEY"):
        lit = re.findall(r'"([^"]*)"', arg)
        return "MODKEY" + "".join(lit)
    mm = re.match(r'^"([^"]*)"$', arg)
    return mm.group(1) if mm else None   # computed hint: not checked

ITEM_RX = re.compile(r"MenuParts::(Item|Check|SubMenu)\(")
class Item:
    def __init__(self, kind, label, hint, block, line):
        self.kind, self.label, self.hint, self.block, self.line = kind, label, hint, block, line

def rank_of(label):
    if label is None: return None
    for r, rx in RANKS:
        if rx.match(label): return r
    return None

def path_of(b):
    p = []
    while b is not None:
        p.append(b)
        b = b.parent
    return p[::-1]

def exclusive(a, b):
    pa, pb = path_of(a), path_of(b)
    for x, y in zip(pa, pb):
        if x is not y:
            return x.chain == y.chain
    return False

def collect(src, m, blocks, region_start, region_end, region_block):
    starts = [b.start for b in blocks]
    def innermost(pos):
        best = blocks[0]
        for b in blocks[1:]:
            if b.start < pos < (b.end or 0) and b.start >= best.start:
                best = b
        return best
    # submenu bodies: items inside them belong to the submenu, not this menu
    for mm in re.finditer(r"MenuParts::SubMenu\(|ImGui::BeginMenu\(", m):
        close = match_paren(m, mm.end() - 1)
        k = close + 1
        while k < len(m) and m[k] in " \t\r\n)": k += 1
        if k < len(m) and m[k] == "{":
            for b in blocks:
                if b.start == k: b.submenu = True
    items = []
    for mm in ITEM_RX.finditer(m, region_start, region_end):
        kind = mm.group(1)
        op = mm.end() - 1
        cl = match_paren(m, op)
        b = innermost(mm.start())
        skip, p = False, b
        while p is not None:
            if p.submenu: skip = True
            p = p.parent
        if skip: continue
        if region_block is not None and region_block not in path_of(b): continue
        args = split_args(src, m, op, cl)
        label = label_of(args[0]) if args else None
        hint = hint_of(args[1]) if kind == "Item" and len(args) > 1 else None
        items.append(Item(kind, label, hint, b, src.count("\n", 0, mm.start()) + 1))
    return items

def enclosing(blocks, pos):
    best = blocks[0]
    for b in blocks[1:]:
        if b.start < pos < (b.end or 0) and b.start >= best.start:
            best = b
    return best

def popup_regions(src, m, blocks):
    """(id, items) for each registered popup call found in this file."""
    found = []
    for fn in BEGINS:
        for mm in re.finditer(re.escape(fn) + r'\(\s*"([^"]+)"', src):
            pid = mm.group(1)
            if pid not in MENUS and not re.search(r"ctx|context", pid, re.I):
                continue
            if mm.start() >= len(m) or m[mm.start()] == " ": continue  # inside a comment
            op = src.index("(", mm.start() + len(fn) - 1)
            cl = match_paren(m, op)
            k = cl + 1
            while k < len(m) and m[k] in " \t\r\n": k += 1
            scope = enclosing(blocks, mm.start())
            # `if (Begin...)` then `{`: the body is that block. `if (!Begin...) return;`: rest of the enclosing scope.
            if k < len(m) and m[k] == ")":
                k += 1
                while k < len(m) and m[k] in " \t\r\n": k += 1
            if k < len(m) and m[k] == "{":
                body = next((b for b in blocks if b.start == k), None)
                items = collect(src, m, blocks, body.start, body.end, body)
            else:
                items = collect(src, m, blocks, cl, scope.end, scope if scope.id else None)
            found.append((pid, mm.start(), items))
    return found

def function_items(src, m, blocks, fname):
    mm = re.search(r"\b" + re.escape(fname) + r"\s*\([^;{]*\)\s*(?:const\s*)?\{", m)
    if not mm: return None
    k = mm.end() - 1
    body = next(b for b in blocks if b.start == k)
    return collect(src, m, blocks, body.start, body.end, body)

# ---------------------------------------------------------------- rules
def check_menu(name, items):
    out = []
    n = len(items)
    for i, a in enumerate(items):
        ra = rank_of(a.label)
        if a.label is not None:
            for rx, advice in WORDING:
                if rx.match(a.label):
                    out.append(f"{name}: wording: \"{a.label}\" ({advice})")
            for rx, ok, canon in HINTS:
                if a.kind == "Item" and rx.match(a.label) and a.hint is not None and a.hint not in ok:
                    out.append(f"{name}: hint: \"{a.label}\" shows {a.hint}, canonical is {canon}")
        if a.kind == "Item" and a.hint and re.match(r"^(Cmd|Ctrl)\+", a.hint):
            out.append(f"{name}: hint: \"{a.label}\" has literal {a.hint}: write MODKEY \"+...\"")
        for b in range(i + 1, n):
            c = items[b]
            if exclusive(a.block, c.block): continue
            rc = rank_of(c.label)
            if ra == 95 and a.label is not None:
                out.append(f"order: {name}: \"{c.label or '?'}\" comes after \"{a.label}\" (Settings is last)")
            elif ra == 90 and a.label is not None and rc != 95:
                out.append(f"order: {name}: \"{c.label or '?'}\" comes after \"{a.label}\" (Delete is last, only Settings may follow)")
            elif ra is not None and rc is not None and ra > rc and ra not in (90, 95):
                out.append(f"order: {name}: \"{c.label}\" should come before \"{a.label}\"")
    # dedupe, keep order
    seen, res = set(), []
    for o in out:
        if o not in seen:
            seen.add(o); res.append(o)
    return res

def scan():
    """-> (menus: {key: (file, line, name, [labels])}, violations: [str], registry problems: [str])"""
    menus, viol, prob = {}, [], []
    seen_ids = set()
    for f in sorted((ROOT / "src").rglob("*")):
        if f.suffix not in (".cpp", ".h", ".mm"): continue
        rel = str(f.relative_to(ROOT))
        src = f.read_text(errors="ignore")
        if "MenuParts::" not in src and "BeginPickerMenu" not in src: continue
        m = mask(src)
        blocks = blocks_of(m)
        for pid, pos, items in popup_regions(src, m, blocks):
            line = src.count("\n", 0, pos) + 1
            if pid not in MENUS:
                prob.append(f"registry: {rel}:{line}: popup \"{pid}\" looks like a context menu but is not in MENUS (context_menu_order.py)")
                continue
            seen_ids.add(pid)
            key = f"{rel}::{pid}"
            # the same id opened twice in one file (two call sites) is one menu: keep the larger
            if key in menus and len(menus[key][3]) >= len(items): continue
            menus[key] = (rel, line, MENUS[pid], items)
        for fid, (ffile, fname, desc) in FRAGMENTS.items():
            if rel != ffile: continue
            items = function_items(src, m, blocks, fname)
            if items is None:
                prob.append(f"registry: {rel}: function {fname} is gone (update FRAGMENTS)")
                continue
            seen_ids.add(fid)
            ln = re.search(r"\b" + fname + r"\s*\(", src).start()
            menus[f"{rel}::{fname}"] = (rel, src.count("\n", 0, ln) + 1, desc, items)
    for pid in MENUS:
        if pid not in seen_ids:
            prob.append(f"registry: popup \"{pid}\" is in MENUS but no longer found in src (remove it)")
    for fid in FRAGMENTS:
        if fid not in seen_ids:
            prob.append(f"registry: fragment {fid} not found")
    for key, (rel, line, name, items) in sorted(menus.items()):
        for v in check_menu(f"{name} [{rel}]", items):
            viol.append(v)
    return menus, viol, prob

def table(menus):
    rows = ["| Menu | Where | Items, top to bottom |", "|---|---|---|"]
    for key, (rel, line, name, items) in sorted(menus.items(), key=lambda kv: (kv[1][0], kv[1][1])):
        parts = []
        for it in items:
            lab = it.label if it.label is not None else "(computed)"
            if it.kind == "SubMenu": lab += " >"
            elif it.kind == "Check": lab += " [x]"
            if it.hint: lab += f" `{it.hint}`"
            parts.append(lab)
        rows.append(f"| {name} | `{rel}:{line}` | {' / '.join(parts) if parts else '(items drawn by helper)'} |")
    return "\n".join(rows)

def main():
    menus, viol, prob = scan()
    if "--table" in sys.argv:
        print(table(menus)); return 0
    base = set(json.loads(BASELINE.read_text())) if BASELINE.exists() else set()
    if "--update" in sys.argv:
        BASELINE.write_text(json.dumps(sorted(viol), indent=1) + "\n")
        print(f"context-menu order: baseline now {len(viol)} violations in {len(menus)} menus"); return 0 if not prob else 1
    new = [v for v in viol if v not in base]
    stale = sorted(base - set(viol))
    for p in prob: print("CONTEXT-MENU " + p)
    for v in new: print("CONTEXT-MENU " + v)
    for s in stale: print("context-menu note: baseline entry no longer needed (run --update): " + s)
    print(f"context-menu order: {len(menus)} menus, {len(viol)} violations ({len(base)} baselined)")
    return 1 if new or prob else 0

if __name__ == "__main__":
    sys.exit(main())
