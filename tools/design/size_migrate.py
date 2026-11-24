#!/usr/bin/env python3
"""Move the design numbers ui_audit.py lists (size, layout, const_def, draw and the geometry numerics) onto tok:: tokens.

Owner rule A1 (2026-11-24): a gap, padding or inset uses the 4 px spacing scale (fractional steps space_0_25 .. space_5)
and a half-pixel snap uses tok::px_align; a thing's own size (width, height, radius, stroke, segment count, column
width, ratio, threshold) gets a semantic per-surface token named for what it is, `tok::<surface>_<name>`, even when
it lands on the grid. An off-scale gap gets a semantic token too. Same surface + name + value share one token.

Values stay exact and keep their C++ type: an integer literal becomes an int token (surface_i) unless it sits directly
in a float argument (ImVec2, SameLine, AddLine thickness ...), a double literal a double token (surface_d), a float
literal a float token (surface). The tokens land in surfaces.json (surface / surface_i / surface_d); run
build_tokens.py afterwards.

    python3 tools/design/size_migrate.py [--dry] [--cats size,layout,...] [--file src/...]
"""
import json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_audit  # noqa: E402

ROOT = ui_audit.ROOT
SURF = ROOT / "src/app/ui/design/surfaces.json"
SURF_DOC = ("Per-surface semantic sizes (owner rule A1, 2026-11-24): a thing's own width, height, radius, stroke, count, "
            "ratio or limit, named for what it is; tok::<surface>_<name>. surface = float, surface_i = int, surface_d = "
            "double (the literal's own C++ type). Written by tools/design/size_migrate.py; edit values here.")
GEN = ROOT / "src/app/ui/design/Tokens.gen.h"
INC = '#include "app/ui/design/Tokens.gen.h"'
DEFAULT_CATS = ["size", "layout", "const_def", "draw", "arith", "assign", "clamp", "compare", "maths", "call:*"]

# call:* findings that belong to the colour step (rule B: role / alpha tokens) or to the per-node param tables
COLOUR_CALLS = {"Base", "Role", "RoleV4", "AccentU32", "withAlpha", "shade", "NodeColor", "DotColor", "BrandColor", "rgba",
                "PackRgb", "AxisColor", "cardCol", "SliceColour", "SliceColours", "CurveChannelColor", "DriftLineColor",
                "ArrangeGroupColor", "KindCol", "tone", "Ansi", "ColorDot", "NoteInk", "ClearOrFill"}
PARAM_CALLS = {"REGISTER_NODE", "SetParamValue", "FixedParamOrdinal", "SetTempo", "SetTimeSignature", "SecondsToTicks",
               "SetGain", "ConnectNodes", "MotionDefaults", "Number", "field", "StyleSliderInt", "StyleSlider",
               "FixedModSlider", "AudioSliderFloat", "AudioSliderInt", "ArrangeSliderFloat", "ArrangeDragFloat", "DragFloat4",
               "FloatPairRow", "FloatMultiRow", "VFaderFloat", "KnobRow", "BarCombo", "MoreCombo", "drawRangeField",
               "ClipField", "Field", "AddSyncedRateCell", "AddRateModeCells", "DrawModRateControls",
               "Generate", "PrepareToPlay", "SetScrollHereY", "Predict", "euclid", "SelectRandom"}
SCALE = {1: "0_25", 2: "0_5", 3: "0_75", 4: "1", 5: "1_25", 6: "1_5", 8: "2", 10: "2_5", 12: "3", 14: "3_5", 16: "4", 24: "5"}

FLOAT_CALLS = {"ImVec2", "ImVec4", "ImRect", "SetNextItemWidth", "PushItemWidth", "SameLine", "Indent", "Unindent",
               "SetCursorPosX", "SetCursorPosY", "SetCursorPos", "SetCursorScreenPos", "Dummy", "TableSetupColumn",
               "AddLine", "AddRect", "AddRectFilled", "AddCircle", "AddCircleFilled", "PathStroke", "PathArcTo",
               "PathArcToFast", "AddPolyline", "AddTriangle", "AddTriangleFilled", "AddBezierCubic", "PathLineTo",
               "PathRect", "AddNgon", "AddNgonFilled", "AddQuad", "AddQuadFilled", "AddRectFilledMultiColor",
               "SetNextWindowSize", "SetNextWindowPos", "BeginChild", "InvisibleButton", "Button", "Selectable",
               "PushStyleVar", "SetWindowFontScale", "SetColumnWidth", "AddImage", "AddImageRounded",
               "SetNextWindowSizeConstraints", "SetNextWindowContentSize", "ItemSize", "AddEllipse", "AddEllipseFilled"}
INT_ARGS = {("AddCircle", 3), ("AddCircleFilled", 3), ("PathArcTo", 4), ("AddNgon", 3), ("AddNgonFilled", 3),
            ("AddPolyline", 3), ("AddRect", 4), ("AddRectFilled", 4), ("PathStroke", 1), ("AddBezierCubic", 6),
            ("TableSetupColumn", 1), ("Selectable", 2), ("BeginChild", 3), ("InvisibleButton", 2), ("PushStyleVar", 0),
            ("AddImageRounded", 7), ("BeginChild", 2), ("AddPolyline", 1),
            ("PathArcToFast", 2), ("PathArcToFast", 3), ("AddEllipse", 4), ("AddEllipseFilled", 4)}
INT_TYPES = {"int", "unsigned", "short", "long", "size_t", "int32_t", "uint32_t", "int64_t", "uint64_t", "int16_t",
             "uint16_t", "int8_t", "uint8_t", "char", "bool", "ImU32", "ImGuiID", "ptrdiff_t", "ImS32", "ImU8"}
FLOAT_TYPES = {"float", "double", "ImVec2", "ImVec4", "ImRect"}
OWN_SIZE_CALLS = {"Dummy", "InvisibleButton", "Button", "BeginChild", "Selectable", "SetNextWindowSize", "ItemSize",
                  "SetNextWindowContentSize", "ImageButton", "ColorButton", "ArrowButton", "SmallButton", "Image"}
GAP_WORD = re.compile(r"(?i)(gap|pad|margin|inset|spacing|space|indent|offset|nudge)")
DRAW_ARG = {  # (call, arg index) -> kind
    ("AddLine", 3): "stroke", ("AddRect", 3): "radius", ("AddRect", 5): "stroke", ("AddRectFilled", 3): "radius",
    ("AddCircle", 1): "r", ("AddCircle", 3): "segments", ("AddCircle", 4): "stroke", ("AddCircleFilled", 1): "r",
    ("AddCircleFilled", 3): "segments", ("PathStroke", 2): "stroke", ("PathArcTo", 1): "r", ("PathArcTo", 4): "segments",
    ("PathArcToFast", 1): "r", ("AddPolyline", 4): "stroke", ("AddTriangle", 4): "stroke", ("AddBezierCubic", 5): "stroke",
    ("AddBezierCubic", 6): "segments", ("AddNgon", 1): "r", ("AddNgon", 3): "segments", ("AddNgon", 4): "stroke",
    ("AddNgonFilled", 1): "r", ("AddNgonFilled", 3): "segments", ("PathRect", 2): "radius", ("AddQuad", 5): "stroke",
    ("AddImageRounded", 6): "radius", ("TableSetupColumn", 2): "col_w", ("SetNextItemWidth", 0): "w",
    ("PushItemWidth", 0): "w", ("SetColumnWidth", 1): "col_w", ("SetWindowFontScale", 0): "font_scale",
    ("AddEllipse", 4): "segments", ("AddEllipse", 5): "stroke", ("AddEllipseFilled", 4): "segments",
    ("SameLine", 0): "x", ("SameLine", 1): "gap", ("Indent", 0): "gap", ("Unindent", 0): "gap",
}
SURFACE_DROP = re.compile(r"(Panel|Panels|Bodies\d*|Body|Window|Widgets|Parts|Kit|Ui|UI|Render|View)$")


def snake(name):
    name = re.sub(r"^(k|g|s|m_)(?=[A-Z_])", "", name).lstrip("_")
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", name).lower()
    s = re.sub(r"[^a-z0-9_]", "_", s)
    return re.sub(r"_+", "_", s).strip("_")


def surface_of(rel):
    stem = os.path.splitext(os.path.basename(rel))[0]
    s = SURFACE_DROP.sub("", stem) or stem
    s = SURFACE_DROP.sub("", s) or s
    s = snake(s).replace("view2_d", "view2d").replace("view3_d", "view3d")
    if s[:1].isdigit(): s = "s" + s
    return s or "misc"


def func_part(func, surf):
    if not func or func == "-": return ""
    f = snake(re.sub(r"^(Draw|Render|Show|Build|Do|Handle|Update)(?=[A-Z])", "", func))
    for w in surf.split("_"):
        f = re.sub(rf"(^|_){w}(_|$)", r"\1", f)
    return re.sub(r"_+", "_", f).strip("_")


UI_SCOPE = re.compile(r"^src/(app/(panels|bodies|ui|frame)/|app/AppShared\.h$|app/AppState\.cpp$|core/SplashScreen\.h$|core/LauncherCard\.h$)")
BYTE_TYPES = {"size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int64_t", "ptrdiff_t"}


def words_of(T, i):
    """type words of the statement holding token i: `const size_t row = ...` -> {const, size_t}"""
    k = i - 1
    while k >= 0 and not (T[k].kind == "op" and T[k].text in (";", "{", "}")): k -= 1
    return {t.text for t in T[k + 1:i] if t.kind == "id"} | {T[j].text for j in range(k + 1, i) if T[j].kind == "id" and j + 1 < len(T) and T[j + 1].text == "("}


ROUNDERS = {"floor", "floorf", "round", "roundf", "lround", "lroundf", "ceil", "ceilf", "IM_FLOOR", "IM_ROUND", "ImFloor", "trunc"}


def operand_names(T, i):
    """the names on either side of the + / - next to token i: `h - 4` -> [h]; `GetFrameHeight() - 12` -> [GetFrameHeight]"""
    out = []
    for k, step in ((i - 2, -1), (i + 2, 1)):
        if not (0 <= k < len(T)): continue
        if step == -1 and T[k].text in (")", "]"):
            d, m = 0, k
            while m >= 0:
                if T[m].text in ")]": d += 1
                elif T[m].text in "([":
                    d -= 1
                    if d == 0: break
                m -= 1
            k = m - 1
        while 0 <= k < len(T) and T[k].text in (".", "->", "::"):
            k += step
        if 0 <= k < len(T) and T[k].kind == "id": out.append(T[k].text)
        if step == -1 and k >= 2 and T[k - 1].text in (".", "->"): out.append(T[k - 2].text)
    return out


def group_scaled(T, i):
    """is the + / - sub-expression holding token i scaled, or rounded, as a whole? `(a + 0.5f) * w`, `floor(a + 0.5f)`"""
    d, k = 0, i - 1
    while k >= 0:
        t = T[k]
        if t.kind == "op" and t.text in ")]": d += 1
        elif t.kind == "op" and t.text in "([":
            if d == 0: break
            d -= 1
        elif d == 0 and (t.kind == "op" and t.text in (",", ";", "{", "}", "=", "?", ":", "+=", "-=", "<", ">", "<=", ">=", "&&", "||") or t.text == "return"):
            return False
        k -= 1
    if k < 0 or T[k].text != "(": return False
    d, j = 0, i + 1
    while j < len(T):
        t = T[j]
        if t.kind == "op" and t.text in "([": d += 1
        elif t.kind == "op" and t.text in ")]":
            if d == 0: break
            d -= 1
        elif d == 0 and t.kind == "op" and t.text in (",", ";", "?", ":", "<", ">", "<=", ">=", "&&", "||"):
            return False
        j += 1
    pre = T[k - 1] if k else None
    post = T[j + 1] if j + 1 < len(T) else None
    if pre is not None and pre.kind == "id" and pre.text in ROUNDERS: return True
    return (post is not None and post.text in ("*", "/")) or (pre is not None and pre.text in ("*", "/"))


def strip_surf(name, surf):
    for w in surf.split("_"):
        name = re.sub(rf"^{w}_", "", name)
    return name or "value"


def num_kind(text):
    t = text.replace("'", "")
    if t[:2].lower() == "0x": return "hex"
    if any(c in t for c in ".eE") or t[-1:] in "fF":
        return "float" if t[-1:] in "fF" else "double"
    return "int"


def fmt_value(v, kind):
    if kind == "int": return int(v)
    return float(v)


class Ctx:
    """per-file token pass: enclosing call stack with argument index for every number token"""

    def __init__(self, src):
        self.toks = ui_audit.tokenize(src)
        self.info = {}
        stack = []   # [name, kind('(' or '{' or '['), argidx, open token index]
        T = self.toks
        for i, t in enumerate(T):
            if t.kind == "op" and t.text in "([{":
                name = ""
                if t.text == "(" and i and T[i - 1].kind == "id":
                    name = T[i - 1].text
                    if i >= 2 and T[i - 2].kind == "id" and T[i - 2].text[:1].isupper() and T[i - 2].text not in ui_audit.KEYWORDS:
                        name = T[i - 2].text
                if t.text == "{" and i and T[i - 1].kind == "id" and T[i - 1].text[:1].isupper():
                    name = T[i - 1].text   # ImVec2{...}
                stack.append([name, t.text, 0, i])
            elif t.kind == "op" and t.text in ")]}":
                if stack: stack.pop()
            elif t.kind == "op" and t.text == "," and stack:
                stack[-1][2] += 1
            elif t.kind == "num":
                self.info[i] = [list(x) for x in stack[-3:]]

    def operand_before(self, i):
        T = self.toks
        return i >= 1 and (T[i - 1].kind in ("id", "num") or T[i - 1].text in (")", "]"))


def decl_type(T, i):
    """type words of the declaration whose initializer holds token i, plus the declared name"""
    depth = 0
    k = i - 1
    while k >= 0:
        t = T[k]
        if t.kind == "op" and t.text in ")]}": depth += 1
        elif t.kind == "op" and t.text in "([{":
            if depth == 0:
                if t.text == "{" and k >= 1 and T[k - 1].kind == "id":   # `int a{4}` / `float x{..}`
                    j = k - 1; name = T[j].text
                    words = []
                    j -= 1
                    while j >= 0 and T[j].kind == "id": words.append(T[j].text); j -= 1
                    return set(words), name
                if t.text == "(":
                    return set(), ""
                return set(), ""
            depth -= 1
        elif depth == 0 and t.kind == "op" and t.text in (";", "{", "}"):
            return set(), ""
        elif depth == 0 and t.kind == "op" and t.text in ("=", "+=", "-=", "*=", "/="):
            j = k - 1
            while j >= 0 and T[j].kind == "op" and T[j].text in ("]", ")"):
                # skip a subscript
                d = 0
                while j >= 0:
                    if T[j].text in ")]": d += 1
                    elif T[j].text in "([":
                        d -= 1
                        if d == 0: break
                    j -= 1
                j -= 1
            name = T[j].text if j >= 0 and T[j].kind == "id" else ""
            # walk back over `Type a = .., b` to the type words
            words, m, d = [], j - 1, 0
            while m >= 0:
                x = T[m]
                if x.kind == "op" and x.text in ")]}":
                    d += 1
                elif x.kind == "op" and x.text in "([{":
                    if d == 0: break
                    d -= 1
                elif d == 0 and x.kind == "op" and x.text in (";",): break
                elif d == 0 and x.kind == "id": words.append(x.text)
                elif d == 0 and x.kind == "op" and x.text in ("::", "*", "&", ","): pass
                elif d == 0 and x.kind == "op" and x.text in ("=",): pass
                elif d == 0 and x.kind == "op": break
                m -= 1
            return set(words), name
        k -= 1
    return set(), ""


def main():
    dry = "--dry" in sys.argv
    cats = DEFAULT_CATS
    only_file = None
    for a in sys.argv[1:]:
        if a.startswith("--cats="): cats = a.split("=", 1)[1].split(",")
        if a.startswith("--file="): only_file = a.split("=", 1)[1]
    def want(cat):
        return cat in cats or ("call:*" in cats and cat.startswith("call:"))
    findings, _, _ = ui_audit.run()
    by_file = {}
    for f in findings:
        if not UI_SCOPE.match(f[4]) and only_file is None: continue   # graph/startup code holds patch data (node defaults, canvas positions)
        if f[0].startswith("call:") and (f[0][5:] in COLOUR_CALLS or f[0][5:] in PARAM_CALLS): continue
        if want(f[0]) and not f[4].startswith(ui_audit.KIT) and (only_file is None or f[4] == only_file):
            by_file.setdefault(f[4], []).append(f)

    sj = json.loads(SURF.read_text()) if SURF.exists() else {"_doc": SURF_DOC}
    groups = {"float": sj.setdefault("surface", {}), "int": sj.setdefault("surface_i", {}), "double": sj.setdefault("surface_d", {})}
    existing = set(re.findall(r"constexpr \w+ (\w+) =", GEN.read_text()))
    taken = {}   # ident -> (kind, value)
    for kind, g in groups.items():
        for s, kv in g.items():
            for k, v in kv.items(): taken[f"{s}_{k}"] = (kind, v)

    def token_for(surf, name, kind, value):
        name = re.sub(r"_+", "_", name).strip("_") or "value"
        n = 1
        while True:
            nm = name if n == 1 else f"{name}_{n}"
            ident = f"{surf}_{nm}"
            if ident in taken:
                if taken[ident] == (kind, value): return ident
            elif ident not in existing:
                groups[kind].setdefault(surf, {})[nm] = value
                taken[ident] = (kind, value)
                return ident
            n += 1

    done = skipped = 0
    stats = {}
    for rel, fs in sorted(by_file.items()):
        path = ROOT / rel
        src = path.read_text(encoding="utf-8")
        ctx = Ctx(src)
        T = ctx.toks
        by_line = {}
        for i, t in enumerate(T):
            if t.kind == "num": by_line.setdefault(t.line, []).append(i)
        used = set()
        tag = set()   # lines whose number is byte / buffer arithmetic: tagged ui-audit-ok, not tokenised
        surf = surface_of(rel)
        edits = []
        for f in fs:
            cat, call, text, stmt, _, ln, func = f
            neg = text.startswith("-")
            if neg: text = text[1:].strip()
            cand = [i for i in by_line.get(ln, []) if T[i].text == text and i not in used]
            if not cand:
                print(f"skip {rel}:{ln} {text} not found"); skipped += 1; continue
            i = cand[0]; used.add(i)
            if words_of(T, i) & BYTE_TYPES:
                tag.add(ln); continue
            nk = num_kind(text)
            if nk == "hex":
                print(f"skip {rel}:{ln} hex {text}"); skipped += 1; continue
            v = ui_audit.num_value(text)
            prev = T[i - 1] if i else None
            nxt = T[i + 1] if i + 1 < len(T) else None
            pv = prev.text if prev is not None and prev.kind == "op" else ""
            nv = nxt.text if nxt is not None and nxt.kind == "op" else ""
            unary = pv == "-" and not ctx.operand_before(i - 1)
            binop_prev = pv if pv in ("+", "-", "*", "/", "%") and not unary else ""
            binop_next = nv if nv in ("+", "-", "*", "/", "%") else ""
            stack = ctx.info.get(i, [])
            top = stack[-1] if stack else ["", "", 0, -1]
            outer = stack[-2] if len(stack) >= 2 else ["", "", 0, -1]
            words, lhs = decl_type(T, i)
            # the type this literal must keep
            in_float_arg = top[0] in FLOAT_CALLS and (top[0], top[2]) not in INT_ARGS
            int_ops = {binop_prev, binop_next} & {"/", "%"} or pv in ("<<", ">>", "&", "|", "^") or nv in ("<<", ">>", "&", "|", "^")
            if nk == "int":
                kind = "float" if (in_float_arg and not int_ops) or (words & FLOAT_TYPES and not words & INT_TYPES and not int_ops and lhs) else "int"
                if words & INT_TYPES or "auto" in words: kind = "int"
            else:
                kind = nk
            # the job
            dk = DRAW_ARG.get((top[0], top[2]))
            job = None
            if binop_prev in ("*", "/") or binop_next in ("*", "/"):
                job = "ratio"   # `2.0f * pad`: a factor binds tighter than the + / - around it
            elif binop_prev in ("+", "-") or (binop_next in ("+", "-") and not binop_prev):
                job = "gap"
            elif dk:
                job = dk
            elif top[0] in OWN_SIZE_CALLS or (top[0] == "ImVec2" and outer[0] in OWN_SIZE_CALLS):
                job = "w" if top[2] == 0 else "h"
            elif top[0] in ("ImVec2", "ImVec4", "ImRect"):
                job = "gap" if lhs and GAP_WORD.search(lhs) or outer[0] in ("PushStyleVar",) else ("x" if top[2] % 2 == 0 else "y")
            elif cat in ("compare", "clamp"):
                job = {"compare": "limit", "clamp": "bound"}[cat]
            if job == "gap" and group_scaled(T, i) and (v != int(v) or not any(ui_audit.GEOM_NAME.match(n) for n in operand_names(T, i))):
                job = "bias"   # `(0.5f + 0.5f * x) * w`, `(slot + 0.5f) * cellW`, `floor(v + 0.5f)`: a centre / rounding term
            if lhs and GAP_WORD.search(lhs) and job not in ("ratio", "bias") and cat in ("const_def", "assign", "member_default"):
                job = "gap"
            if job == "gap" and lhs and not GAP_WORD.search(lhs) and cat in ("const_def",) and not binop_prev and not binop_next:
                job = "own"
            # replacement
            av = abs(v)
            if job == "gap" and kind == "float" and av in SCALE and av == int(av):
                rep = f"tok::space_{SCALE[int(av)]}"
            elif job == "gap" and kind == "float" and av == 0.5:
                rep = "tok::px_align"
            else:
                if lhs:
                    name = strip_surf(snake(lhs), surf)
                    if top[0] in ("ImVec2", "ImVec4") and top[3] > i - 40:
                        name += "_" + ("xy"[top[2] % 2] if top[0] == "ImVec2" else "xyzw"[top[2] % 4])
                    if job in ("gap", "ratio", "limit", "bound") and not name.endswith(job):
                        name += "_" + job if job not in ("gap",) or not GAP_WORD.search(lhs) else ""
                else:
                    fp = func_part(func, surf)
                    base_job = job or ("own" if cat in ("size", "const_def") else ("arg" if cat.startswith("call:") else cat))
                    base_job = base_job.lower()
                    callp = ""
                    if cat.startswith("call:") and not job:
                        callp = snake(cat[5:])
                    elif not job or job in ("x", "y", "w", "h"):
                        callp = snake(top[0]) if top[0] and top[0] not in ("ImVec2", "ImVec4") else ""
                        if top[0] in ("ImVec2", "ImVec4") and outer[0]:
                            callp = snake(outer[0])
                        if callp == base_job: callp = ""
                    name = "_".join(p for p in (fp, callp, base_job) if p)
                ident = token_for(surf, name, kind, fmt_value(v, kind))
                rep = f"tok::{ident}"
            # a negative literal `-4.0f`: the token replaces only the number, the unary minus stays
            edits.append((T[i].pos, T[i].pos + len(text), rep))
            stats[(cat, "scale" if "space_" in rep or "px_align" in rep else "semantic")] = stats.get((cat, "scale" if "space_" in rep or "px_align" in rep else "semantic"), 0) + 1
            done += 1
        if not edits and not tag: continue
        for a, b, t in sorted(edits, reverse=True):
            src = src[:a] + t + src[b:]
        if tag:
            lines = src.split("\n")
            for ln in tag:
                if "ui-audit-ok" not in lines[ln - 1]:
                    lines[ln - 1] += "  // ui-audit-ok: byte / buffer arithmetic, not a screen quantity"
            src = "\n".join(lines)
        if "Tokens.gen.h" not in src and "AppShared.h" not in src and "TokenColors.h" not in src:
            m = None
            for m in re.finditer(r"^#include .*$", src, re.M): break
            if m is None:
                m = re.search(r"^#pragma once.*$", src, re.M)
            if m is None:
                src = INC + "\n" + src
            else:
                src = src[:m.end()] + "\n" + INC + src[m.end():]
        if not dry:
            path.write_text(src, encoding="utf-8")
    if not dry:
        for g in groups.values():
            for s in list(g): g[s] = dict(sorted(g[s].items()))
        for k, g in (("surface", "float"), ("surface_i", "int"), ("surface_d", "double")):
            sj[k] = dict(sorted(groups[g].items()))
        SURF.write_text(json.dumps(sj, indent=1) + "\n")
    for k in sorted(stats): print(f"  {k[0]:<14} {k[1]:<9} {stats[k]}")
    ntok = sum(len(kv) for g in groups.values() for kv in g.values())
    print(f"size_migrate: {done} literals moved, {skipped} skipped, {ntok} semantic tokens")


if __name__ == "__main__":
    main()
