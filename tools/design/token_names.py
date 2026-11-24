#!/usr/bin/env python3
"""Rename the surface tokens whose names say where a literal came from (a file, a function, an argument position or a
running number) to names that say what the value does (owner 2026-11-24).

    tok::arrange_panel_content_draw_pencil_arg_2  ->  tok::arrange_pencil_cursor_size
    tok::chat_row_h_3                             ->  tok::chat_row_h_lg

The job comes from where the token is used, strongest first: the parameter it fills (read from the callee's own
declaration: imgui.h, imgui_node_editor.h, every header and source in src/), the component of an ImVec2 that fills a
size or position parameter (_w / _h, _x / _y), the variable it initialises, the gap / inset words around it. The subject
comes from the control's label, the helper being called (DrawPencil -> pencil) or the variable it initialises.
Tokens that end up with the same name and the same value become one token; the same name with different values gets
named steps in value order (xs, sm, md, lg, xl), never a number.

    python3 tools/design/token_names.py [--dry] [--show]     (--show prints old -> new)
    python3 tools/design/token_names.py --lint               (exit 1 if a provenance name is left; ratchet.py runs it)
"""
import json, os, re, sys, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_audit, token_tiers, subprocess  # noqa: E402
import size_migrate as sm  # noqa: E402

ROOT = ui_audit.ROOT
SURF = ROOT / "src/app/ui/design/surfaces.json"
GROUPS = ("surface", "surface_i", "surface_d", "surface_hex", "surface_rgba8", "surface_rgbaf", "surface_pair8", "surface_pairf")
# a provenance name: a running number, an argument position, a draw-call kind, or a statement kind left from the
# first cut (bound / assign / limit / bias / draw / dummy / arith / maths / if_arith)
BAD = re.compile(r"(_\d+$|(^|_)arg(_|$)|(^|_)draw(_x|_y)?(_|$)|(^|_)(bound|assign|bias|arith|if_arith|maths|pow_maths)(_|$)|"
                 r"(^|_)(dummy_[wh]|set_next_\w+|im_col32|im_color|u32)(_|$)|"
                 r"(^|_)for(_|$)|(^|_)(can_group|add_file_to|make_clip_source|estimate_sample|render_text_clipped)(_|$))")
BAD_SURF = re.compile(r"support\d+")
STEPS = {1: [""], 2: ["sm", "lg"], 3: ["sm", "md", "lg"], 4: ["xs", "sm", "md", "lg"], 5: ["xs", "sm", "md", "lg", "xl"],
         6: ["xxs", "xs", "sm", "md", "lg", "xl"], 7: ["xxs", "xs", "sm", "md", "lg", "xl", "xxl"],
         8: ["xxs", "xs", "sm", "md", "lg", "xl", "xxl", "xxxl"], 9: ["xxxs", "xxs", "xs", "sm", "md", "lg", "xl", "xxl", "xxxl"]}
PARAM_JOB = {"thickness": "stroke", "rounding": "radius", "num_segments": "segments", "radius": "r", "spacing": "gap",
             "offset_from_start_x": "x", "spacing_w": "gap", "item_width": "w", "width": "w", "height": "h",
             "size_arg": "size", "font_size": "font_size", "indent_w": "indent", "alpha": "alpha", "a": "alpha",
             "local_x": "x", "local_y": "y", "wrap_pos_x": "wrap_x", "wrap_width": "wrap_w", "col_w": "col_w",
             "init_width_or_weight": "col_w", "v_speed": "speed", "v_min": "lo", "v_max": "hi", "pad": "pad",
             "padding": "pad", "inset": "inset", "gap": "gap", "r": "r", "size": "size", "w": "w", "h": "h"}
SKIP_PARAM = {"col", "color", "colour", "c", "label", "str_id", "id", "fmt", "flags", "text", "draw_list", "dl", "p", "pos",
              "p1", "p2", "p3", "p4", "p_min", "p_max", "a_min", "a_max", "center", "centre", "min", "max", "v", "value"}
GENERIC_SUBJ = {"draw", "add", "path", "text", "button", "item", "row", "value", "v", "x", "y", "w", "h", "i", "j", "k", "n", "t",
                "a", "b", "c", "d", "r", "p", "s", "u", "sz", "dl", "fg", "bg", "col", "tok", "im_vec2", "im_vec4", "im_color",
                "role", "base", "with_alpha", "tint", "u32", "v4", "hex"}
GENERIC_CALLS = {"ImVec2", "ImVec4", "ImRect", "ImColor", "IM_COL32", "U32", "V4", "Role", "Base", "WithAlpha", "Tint",
                 "Draw", "max", "min", "clamp", "std", "floor", "floorf", "round", "roundf", "Hex", "TextTint", "RoleV4"}


def parse_decls():
    """function name -> parameter names, from the declarations Infinite and its ImGui copies write"""
    out = {}
    files = [ROOT / "external/imgui/imgui.h", ROOT / "external/imgui-node-editor/imgui_node_editor.h"]
    for d, _, fs in os.walk(ROOT / "src"):
        for f in fs:
            if f.endswith((".h", ".cpp")): files.append(os.path.join(d, f))
    rx = re.compile(r"\b([A-Za-z_]\w*)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*(?:const\s*)?(?:IM_FMT\w+\([^)]*\)\s*)?(?:;|\{|override)")
    for f in files:
        try: s = open(f, encoding="utf-8", errors="replace").read()
        except OSError: continue
        for m in rx.finditer(s):
            name, plist = m.group(1), m.group(2)
            if name in ui_audit.KEYWORDS or name in ("if", "for", "while", "switch", "return"): continue
            ps = []
            d, cur = 0, ""
            for ch in plist:
                if ch in "(<[{": d += 1
                elif ch in ")>]}": d -= 1
                if ch == "," and d == 0: ps.append(cur); cur = ""
                else: cur += ch
            if cur.strip(): ps.append(cur)
            names = []
            ok = True
            for p in ps:
                p = p.split("=")[0].strip()
                mm = re.search(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?$", p)
                words = re.findall(r"[A-Za-z_]\w*", p)
                if not mm or len(words) < 2 or not re.search(r"[\s*&]\s*" + re.escape(mm.group(1)) + r"\s*(?:\[[^\]]*\])?$", p) \
                        or re.search(r"(::|\.|->)\s*" + re.escape(mm.group(1)) + r"\s*(?:\[[^\]]*\])?$", p) or re.search(r"[-+/%\"']|\d\.", p):
                    ok = False; break   # a call, not a declaration
                names.append(mm.group(1))
            if ok and names and len(names) >= len(out.get(name, [])):
                out[name] = names
    return out


def load_tokens():
    d = token_tiers.flatten_surfaces(json.loads(SURF.read_text()))
    toks = {}   # full name -> (group, surf, key)
    for g in GROUPS:
        for surf, kv in d.get(g, {}).items():
            if surf.startswith("_") or not isinstance(kv, dict): continue
            for k in kv:
                toks[f"{surf}_{k}"] = (g, surf, k)
    return d, toks


PRIM = {"AddLine": "line", "AddRect": "outline", "AddRectFilled": "fill", "AddRectFilledMultiColor": "fill",
        "AddCircle": "ring", "AddCircleFilled": "dot", "AddText": "text", "AddTriangle": "arrow", "AddTriangleFilled": "arrow",
        "AddPolyline": "line", "PathStroke": "path", "AddBezierCubic": "curve", "AddBezierQuadratic": "curve",
        "AddConvexPolyFilled": "fill", "AddQuadFilled": "fill", "AddQuad": "outline", "AddNgon": "ring", "AddNgonFilled": "dot",
        "PathArcTo": "arc", "PathArcToFast": "arc", "PathRect": "rect", "AddImage": "image", "AddImageRounded": "image",
        "Dummy": "spacer", "SameLine": "", "SetNextItemWidth": "field", "PushItemWidth": "field", "SetCursorPosX": "",
        "SetCursorPosY": "", "SetCursorScreenPos": "", "SetCursorPos": "", "BeginChild": "child", "Button": "button",
        "InvisibleButton": "hit", "Selectable": "row", "TableSetupColumn": "column", "SetNextWindowSize": "window",
        "SetNextWindowPos": "window", "SetNextWindowSizeConstraints": "window", "ItemSize": "item", "Indent": "",
        "SetWindowFontScale": "text", "CalcTextSize": "text", "PushTextWrapPos": "text"}
PROV = {"body", "bodies", "panel", "panels", "content", "draw", "arg", "bound", "assign", "bias", "arith", "maths", "set",
        "next", "get", "im", "gui", "u32", "col32", "kind", "value", "values", "item", "if", "pow", "the", "do", "render",
        "show", "build", "handle", "update", "section", "begin", "end", "push", "add", "tok", "params", "param_ref"}
COLOUR_CALLS = {"Role", "Base", "WithAlpha", "Tint", "U32", "IM_COL32", "TextTint", "ImColor", "RoleV4", "BaseV4", "TextCol",
                "LabelOn", "Hex", "ColorConvertHSVtoRGB", "V4", "Col", "Ink", "Accent"}
FUNC_RX = re.compile(r"^(?:[ ]{0,6})(?:static |inline |constexpr |virtual )*[A-Za-z_][\w:<>\*&, ]*?[\s\*&]([A-Z]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?\{?\s*$", re.M)


def words(s):
    return [w for w in re.split(r"_+", s) if w]


def drop_prov(s):
    return "_".join(w for w in words(s) if w not in PROV)


TOKNAMES = set()
# names the clues cannot find (full old name -> new key)
HAND = {"terminal_draw_braille_assign": "braille_dot_col_left", "terminal_draw_braille_assign_2": "braille_dot_col_right",
        "terminal_draw_braille_bias": "braille_dot_row_first", "terminal_draw_braille_ratio_2": "braille_dot_row_step",
        "terminal_draw_box_path_bezier_quadratic_curve_to_draw": "box_corner_segments"}
TOKRX = None


def contexts(src, wanted):
    """every use of a wanted token, as a dict of naming clues"""
    T = ui_audit.tokenize(src)
    starts = [(m.start(), m.group(1)) for m in FUNC_RX.finditer(src)]
    line_pos = [0]
    for m in re.finditer("\n", src): line_pos.append(m.end())
    stack = []
    out = []
    for i, t in enumerate(T):
        if t.kind == "op" and t.text in "([{":
            name, qual, first = "", "", ""
            if i and T[i - 1].kind == "id":
                name = T[i - 1].text
                if i >= 3 and T[i - 2].text == "::" and T[i - 3].kind == "id": qual = T[i - 3].text
            if i + 1 < len(T) and T[i + 1].kind == "id":
                first = T[i + 1].text
                if i + 3 < len(T) and T[i + 2].text == "::" and T[i + 3].kind == "id":
                    first = "" if first == "tok" else T[i + 3].text
            stack.append([name, t.text, 0, i, qual, first])
        elif t.kind == "op" and t.text in ")]}":
            if stack: stack.pop()
        elif t.kind == "op" and t.text == "," and stack:
            stack[-1][2] += 1
        elif t.kind == "id" and t.text in wanted and i >= 2 and T[i - 1].text == "::" and T[i - 2].text == "tok":
            k = i - 1
            while k >= 0 and not (T[k].kind == "op" and T[k].text in (";", "{", "}")): k -= 1
            lhs = ""
            for j in range(k + 1, i):
                if T[j].kind == "op" and T[j].text in ("=", "+=", "-=", "*=", "/="):
                    ids = [T[x].text for x in range(k + 1, j) if T[x].kind == "id" and not (x >= 1 and T[x - 1].text in ("::", ".", "->"))
                           and T[x].text not in ("const", "float", "int", "auto", "constexpr", "static", "double", "unsigned",
                                                 "ImU32", "ImVec2", "ImVec4", "bool", "size_t", "uint32_t", "tok")]
                    if ids: lhs = ids[-1]
                    if j >= 2 and T[j - 1].kind == "id" and T[j - 2].text in (".", "->"): lhs = T[j - 1].text
                    break
            label = ""
            for fr in reversed(stack):
                if fr[0] in COLOUR_CALLS or fr[0] in ("ImVec2", "ImVec4", "ImRect"): continue
                for j in range(fr[3], min(len(T), fr[3] + 30)):
                    if T[j].kind == "str": label = T[j].text.strip('"'); break
                break
            pos = T[i].pos
            func = ""
            for st_, nm in starts:
                if st_ > pos: break
                func = nm
            prev = T[i - 3].text if i >= 3 else ""
            nxt = T[i + 1].text if i + 1 < len(T) else ""
            near = sm.operand_names(T, i) if prev in ("+", "-") or nxt in ("+", "-") else []
            near = [x for x in near if x not in TOKNAMES]
            if lhs in TOKNAMES: lhs = ""
            out.append(dict(name=t.text, st=[list(x) for x in stack[-4:]], lhs=lhs, label=label, func=func,
                            ops=(prev, nxt), near=near))
    return out


def clues(u, decls, surf):
    """(element, property, area) for one use"""
    st, lhs, ops = u["st"], u["lhs"], u["ops"]
    elem, prop = "", ""
    frames = [f for f in st]
    colour = any(f[0] in COLOUR_CALLS for f in frames)
    role = ""
    # property: the parameter this value fills
    if frames:
        top = frames[-1]
        call, ai = top[0], top[2]
        if call in ("ImVec2", "ImVec4") and len(frames) >= 2:
            parent = frames[-2]
            pn = decls.get(parent[0], [])
            pname = pn[parent[2]] if parent[2] < len(pn) else ""
            pj = PARAM_JOB.get(pname, pname)
            sizey = pname in ("size", "size_arg", "sz", "min", "max", "size_min", "size_max") or pj in ("size", "w", "h") or parent[0] in ("Dummy", "SetNextWindowSize", "InvisibleButton", "BeginChild", "ItemSize", "Button")
            prop = ("w" if ai == 0 else "h") if sizey else ("x" if ai == 0 else "y")
            if parent[0] in ("PushStyleVar",): elem = sm.snake(parent[5].replace("ImGuiStyleVar_", "").replace("StyleVar_", ""))
            elif parent[0] in PRIM: elem = PRIM[parent[0]]
            elif parent[0] and parent[0] not in GENERIC_CALLS: elem = sm.snake(parent[4] or parent[0])
        elif colour:
            prop = "alpha" if ai >= 1 or call in ("WithAlpha",) else "shade"
            if call in ("ColorConvertHSVtoRGB",): prop = ["hue", "sat", "val"][min(ai, 2)]
            cf = next((f for f in reversed(frames) if f[0] in COLOUR_CALLS), None)
            role = sm.snake(cf[5]) if cf and cf[5] and cf[5] not in ("col", "c", "tok") else ""
            outer = next((f for f in reversed(frames) if f[0] not in COLOUR_CALLS and f[0] not in ("ImVec2", "ImVec4")), None)
            if outer:
                if outer[0] in ("PushStyleColor",): elem = sm.snake(outer[5].replace("ImGuiCol_", "").replace("StyleColor_", ""))
                elif outer[0] in PRIM: elem = PRIM[outer[0]]
                elif outer[0] not in GENERIC_CALLS: elem = sm.snake(outer[4] or outer[0])
        elif call:
            pn = decls.get(call, [])
            if call in ("PushStyleVar",): elem = sm.snake(top[5].replace("ImGuiStyleVar_", "").replace("StyleVar_", "")); prop = ""
            elif ai < len(pn) and pn[ai] not in SKIP_PARAM: prop = sm.snake(PARAM_JOB.get(pn[ai], pn[ai]))
            if call in PRIM: elem = elem or PRIM[call]
            elif call not in GENERIC_CALLS and not elem: elem = sm.snake(top[4] if top[4] and call == "Draw" else call)
            if call in ("SameLine", "Indent", "Unindent"): prop = "gap" if call != "SameLine" or ai == 1 else "x"
            if call in ("clamp", "min", "max", "ImClamp", "ImMin", "ImMax", "fmin", "fmax"):
                prop = {"min": "max", "ImMin": "max", "fmin": "max"}.get(call, "min" if call in ("max", "ImMax", "fmax") else ["v", "lo", "hi"][min(ai, 2)])
    if ops[0] in ("%",) or ops[1] in ("%",): prop = "period"
    elif ops[0] in ("==", "!=", "<", ">", "<=", ">="): prop = "limit"
    l = sm.snake(lhs) if lhs and len(lhs) > 1 else ""
    if l in GENERIC_SUBJ: l = ""
    if not prop:
        if l and (re.search(r"(^|_)(w|h|x|y|r|gap|pad|size|width|height|radius|stroke|thick|inset|alpha|scale|offset|len|step|speed)$", l)):
            prop, l = l, ""
        elif ops[0] in ("*", "/") or ops[1] in ("*", "/"): prop = "scale"
        elif ops[0] in ("+", "-") or ops[1] in ("+", "-"):
            nm = [sm.snake(x) for x in u["near"] if len(x) > 1 and sm.snake(x) not in GENERIC_SUBJ]
            prop = "inset"
            if nm and not elem: elem = nm[0]
    if l and not elem: elem = l
    elif l and l not in elem: elem = l + "_" + elem
    if not elem and colour and role: elem = role
    if not elem and u["label"]:
        lab = sm.snake(re.sub(r"##.*", "", u["label"]).strip() or u["label"].split("##")[-1])
        if lab and len(lab) <= 20 and not lab[0].isdigit(): elem = lab
    area = drop_prov(sm.func_part(u["func"], surf)) if u["func"] else ""
    global TOKRX
    if TOKRX is None:
        TOKRX = re.compile(r"(?<![a-z0-9])(?:color_|col_)?(?:" + "|".join(sorted(map(re.escape, (t for t in TOKNAMES if len(t) >= 8 and "_" in t)), key=len, reverse=True)) + r")(?![a-z0-9])")
    elem = re.sub(r"_+", "_", TOKRX.sub("", elem)).strip("_")
    elem = "_".join(w for w in elem.split("_") if not w.isdigit())
    return drop_prov(elem), drop_prov(prop), area


def clean(parts, surf):
    ws = []
    for p in parts:
        for w in words(p):
            if w not in PROV and (not ws or ws[-1] != w): ws.append(w)
    s = sm.strip_surf("_".join(ws), surf)
    # no word twice
    seen, out = set(), []
    for w in words(s):
        if w in seen and len(w) > 1: continue
        seen.add(w); out.append(w)
    return "_".join(out)


def old_core(key):
    """what the old name already says about the job, minus its provenance"""
    k = re.sub(r"_\d+$", "", key)
    k = re.sub(r"(^|_)(arg|draw_x|draw_y|draw|bound|assign|bias|arith|if_arith|maths|pow_maths|dummy|set_next_\w+|im_col32|im_color|u32)(?=_|$)", "", k)
    k = re.sub(r"_\d+(?=_)", "", k)
    return drop_prov(re.sub(r"_+", "_", k).strip("_"))


def main():
    dry, show, lint = "--dry" in sys.argv, "--show" in sys.argv, "--lint" in sys.argv
    d, toks = load_tokens()
    TOKNAMES.update(toks); TOKNAMES.update(k for (_, _, k) in toks.values())
    base_scale = {f"space_{x}" for x in ("0_25", "0_5", "0_75", "1_5", "2_5")}
    bad = {n for n, (g, s, k) in toks.items() if BAD.search(k) or BAD_SURF.search(s)}
    if lint:
        # a surface token named for where it sits (file, function, argument position, step number), and a numbered
        # colour role (accent_2): both must be 0 (ratchet.py runs this)
        tj = json.loads((ROOT / "src/app/ui/design/tokens.json").read_text())
        cols = [k for k in tj.get("roles", {}).get("col", {}) if not k.startswith("_") and re.search(r"_\d+$", k)]
        left = sorted(bad) + [f"col::{k}" for k in sorted(cols)]
        print(f"token_names: {len(left)} provenance names")
        for n in left[:20]: print("  " + n)
        return 1 if left else 0
    decls = parse_decls()
    uses = collections.defaultdict(list)
    files = []
    for dd, _, fs in os.walk(ROOT / "src"):
        for f in fs:
            if f.endswith((".h", ".cpp", ".mm")) and not f.endswith(".gen.h"):
                files.append(os.path.join(dd, f))
    for f in files:
        s = open(f, encoding="utf-8", errors="replace").read()
        if "tok::" not in s: continue
        names = set(re.findall(r"tok::(\w+)", s)) & bad
        if not names: continue
        for u in contexts(s, names): uses[u["name"]].append(u)
    # proposed name per bad token: <element>_<property>, the area (the node / part it is drawn for) kept apart
    value = lambda g, surf, key: d[g][surf][key]
    keep = {n: (g, s, k) for n, (g, s, k) in toks.items() if n not in bad}
    info = {}
    for n in bad:
        g, surf, key = toks[n]
        ev, pv, av = collections.Counter(), collections.Counter(), collections.Counter()
        for u in uses.get(n, []):
            e, pr, ar = clues(u, decls, surf)
            if e: ev[e] += 1
            if pr: pv[pr] += 1
            if ar: av[ar] += 1
        core = old_core(key)
        e = ev.most_common(1)[0][0] if ev else ""
        pr = pv.most_common(1)[0][0] if pv else ""
        ar = av.most_common(1)[0][0] if av else ""
        if not pr:
            cw = words(core)
            pr = cw[-1] if cw else "value"
            if not e and len(cw) > 1: e = "_".join(cw[:-1])
        lv = collections.Counter()
        for u in uses.get(n, []):
            lab = sm.snake(re.sub(r"##.*", "", u["label"]).strip() or u["label"].split("##")[-1]) if u["label"] else ""
            if lab and len(lab) <= 20 and not lab[0].isdigit(): lv[lab] += 1
        info[n] = (g, surf, e, pr, ar, core, lv.most_common(1)[0][0] if lv else "")

    def base(n, level):
        g, surf, e, pr, ar, core, lab = info[n]
        parts = [e or ar, pr] if level == 0 else [ar, e, pr] if level == 1 else [ar, e, info[n][6], pr]
        nm = clean(parts, surf) or clean([core], surf) or "value"
        if nm[0].isdigit(): nm = "n" + nm
        return nm

    level = {n: 0 for n in bad}
    final = {}
    for _round in range(3):
        buckets = collections.defaultdict(list)
        for n in bad:
            if n in final: continue
            buckets[(toks[n][0], toks[n][1], base(n, level[n]))].append(n)
        for (g, surf, nm), ns in buckets.items():
            vals = []
            for n in ns:
                v = value(*toks[n])
                if v not in vals: vals.append(v)
            exist = keep.get(f"{surf}_{nm}")
            if exist and exist[0] == g and vals == [value(*exist)]:
                for n in ns: final[n] = nm          # merges into the existing good token
                continue
            if len(vals) == 1 and not exist:
                for n in ns: final[n] = nm
                continue
            if len(vals) > (4 if _round == 0 else 9) and _round < 2:
                for n in ns: level[n] += 1          # try a more specific name first
                continue
            if exist and value(*exist) not in vals: vals.append(value(*exist))
            def keyf(v): return (0, v) if isinstance(v, (int, float)) else (1, sum(v) if isinstance(v, list) and all(isinstance(x, (int, float)) for x in v) else str(v))
            order = sorted(vals, key=keyf)
            steps = STEPS.get(len(order)) or [f"v{i}" for i in range(len(order))]
            for n in ns:
                stp = steps[order.index(value(*toks[n]))]
                final[n] = f"{nm}_{stp}" if stp else nm
    # one name in two groups (a float alpha and an int 0-255 alpha): the non-float one says its unit
    UNIT = {"surface_i": "_u8", "surface_d": "_d", "surface_hex": "_hex", "surface_rgba8": "_rgba8", "surface_rgbaf": "_rgbaf",
            "surface_pair8": "_pair8", "surface_pairf": "_pairf"}
    seen = collections.defaultdict(set)
    for n, nm in final.items(): seen[(toks[n][1], nm)].add(toks[n][0])
    for n, (g, s_, k) in keep.items(): seen[(s_, k)].add(g)
    for n, nm in list(final.items()):
        g, surf, _ = toks[n]
        if len(seen[(surf, nm)]) > 1 and g != "surface":
            u = UNIT.get(g, "_" + g)
            if g == "surface_i" and not re.search(r"alpha|shade", nm): u = "_n" if re.search(r"layout|segments|count|step", nm) else "_i"
            final[n] = nm + u
    # collisions with kept names of a different value or group
    for n, nm in list(final.items()):
        g, surf, _ = toks[n]
        e = keep.get(f"{surf}_{nm}")
        if e and (e[0] != g or value(*e) != value(*toks[n])):
            final[n] = nm + "_alt"
    for n, nm in HAND.items():
        if n in final: final[n] = nm
    rename = {n: f"{toks[n][1]}_{nm}" for n, nm in final.items()}
    still = [f"{toks[n][1]}_{nm}" for n, nm in final.items() if BAD.search(nm) or re.search(r"_v\d+$", nm)]
    if show:
        for o in sorted(rename): print(f"{o} -> {rename[o]}")
    print(f"token_names: {len(bad)} provenance names -> {len(set(rename.values()))} job names; {len(still)} still need a hand name")
    for r in sorted(set(still))[:40]: print("  hand: " + r)
    if dry: return 0
    # rewrite surfaces.json
    for n, new in rename.items():
        g, surf, key = toks[n]
        v = d[g][surf].pop(key)
        nk = new[len(surf) + 1:]
        if nk in d[g][surf] and d[g][surf][nk] != v:
            raise SystemExit(f"collision {new}")
        d[g][surf][nk] = v
    for g in GROUPS:
        for surf in d.get(g, {}):
            if isinstance(d[g][surf], dict): d[g][surf] = dict(sorted(d[g][surf].items()))
    import colour_migrate
    SURF.write_text(colour_migrate.compact_json(d).rstrip("\n") + "\n")
    subprocess.run([sys.executable, str(ROOT / "tools/design/token_tiers.py"), "--apply"], check=True)   # re-tier: buckets + aliases
    rx = re.compile(r"\btok::(" + "|".join(sorted(map(re.escape, rename), key=len, reverse=True)) + r")\b")
    for f in files:
        s = open(f, encoding="utf-8").read()
        s2 = rx.sub(lambda m: "tok::" + rename[m.group(1)], s)
        if s2 != s: open(f, "w", encoding="utf-8").write(s2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
