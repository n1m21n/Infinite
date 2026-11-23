#!/usr/bin/env python3
"""UI audit: every hardcoded number and visible string in the UI, by allowlist (not by known-bad patterns).

Reads every UI file under src/ (a file that calls ImGui or ImDrawList; tests, self-tests, third_party and generated
headers excluded) with a small C++ tokenizer: comments and string insides are skipped, numbers are read whole
(ints, decimals, exponents, hex, f/u/l suffixes, unary minus).

Each numeric literal and each user-visible string literal is tagged with the call it sits in (innermost
`name(`), the file, the line and the enclosing function, then given a category:
  - a theme from its statement first (timing, hit, font, key, cursor), because those read the same in any call;
  - else the family of the call it sits in (size, colour, layout, draw, clamp, format, label ...);
  - else what the statement does (const_def, compare, assign, arith).
Unknown calls become `call:<name>`, so a new kind of hardcoding shows up as a new category by itself.

Allowed (not findings): token reads (tok::, a `k*` constant name), 0, 1, -1, 0.5 next to * or /, array indices,
`for (...)` headers, case labels, enum values, preprocessor lines, text inside L()/T()/TC()/LC()/I18N_KEY(),
IDs ("##..."), strings with no letters. Every allowed literal is counted by reason so nothing disappears quietly.

Also counts every raw `ImGui::` call outside src/app/ui/design, by widget family.

  tools/design/ui_audit.py                 print the category table, write internal/plans/ui-audit.md
  tools/design/ui_audit.py --out PATH      write the report somewhere else
  tools/design/ui_audit.py --file F...     print every finding in these files (hand checks)
  tools/design/ui_audit.py --ratchet       per-category counts vs ratchet.json "audit" (may only go down); exit 1 on a rise
  tools/design/ui_audit.py --ratchet --update | --reset   lower / rewrite the stored counts
"""
import collections, json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
STORE = ROOT / "tools/design/ratchet.json"
KIT = "src/app/ui/design/"
SKIP = re.compile(r"(^src/app/selftest/|/third_party/|^src/third_party|\.gen\.h$|Tests?\w*\.(cpp|h)$|/tests?/|BenchHarness)")
UI_FILE = re.compile(r"ImGui::|ImDrawList|ImVec2|ImU32|IM_COL32")

# ---------------------------------------------------------------- tokenizer
NUM = re.compile(r"0[xX][0-9a-fA-F']+[uUlL]*|(?:\d[\d']*\.?\d*|\.\d+)(?:[eE][+-]?\d+)?[fFuUlL]*")
IDENT = re.compile(r"[A-Za-z_]\w*")
PUNCT = ("::", "->", "<<=", ">>=", "<=", ">=", "==", "!=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=", "%=",
         "&=", "|=", "^=", "<<", ">>")

class Tok:
    __slots__ = ("kind", "text", "line", "pos")
    def __init__(self, kind, text, line, pos): self.kind, self.text, self.line, self.pos = kind, text, line, pos

def tokenize(src):
    """kinds: num, str, chr, id, op, pp (a whole preprocessor line)."""
    out, i, n, line = [], 0, len(src), 1
    bol = True
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1; i += 1; bol = True; continue
        if c in " \t\r\f\v":
            i += 1; continue
        if c == "\\" and i + 1 < n and src[i + 1] == "\n":
            i += 2; line += 1; continue
        if src.startswith("//", i):
            j = src.find("\n", i); i = n if j < 0 else j; continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2); j = n if j < 0 else j + 2
            line += src.count("\n", i, j); i = j; continue
        if bol and c == "#":
            j = i
            while True:
                k = src.find("\n", j)
                if k < 0: k = n; break
                if src[k - 1] == "\\": j = k + 1; continue
                break
            out.append(Tok("pp", src[i:k], line, i)); line += src.count("\n", i, k); i = k; continue
        bol = False
        m = re.match(r'(?:u8|u|U|L)?R"([^()\\ ]{0,16})\(', src[i:i + 24])
        if m:
            end = src.find(")" + m.group(1) + '"', i + m.end())
            end = n if end < 0 else end + len(m.group(1)) + 2
            out.append(Tok("str", src[i + m.end() - 1:end], line, i)); line += src.count("\n", i, end); i = end; continue
        if c == '"' or (c in "uUL" and i + 1 < n and src[i + 1] == '"') or (src.startswith('u8"', i)):
            q = src.index('"', i); j = q + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            out.append(Tok("str", src[q:j + 1], line, i)); i = j + 1; continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            out.append(Tok("chr", src[i:j + 1], line, i)); i = j + 1; continue
        if c.isdigit() or (c == "." and i + 1 < n and src[i + 1].isdigit()):
            m = NUM.match(src, i)
            out.append(Tok("num", m.group(0), line, i)); i = m.end(); continue
        if c.isalpha() or c == "_":
            m = IDENT.match(src, i)
            out.append(Tok("id", m.group(0), line, i)); i = m.end(); continue
        for p in PUNCT:
            if src.startswith(p, i):
                out.append(Tok("op", p, line, i)); i += len(p); break
        else:
            out.append(Tok("op", c, line, i)); i += 1
    # adjacent string literals are one literal
    merged = []
    for t in out:
        if t.kind == "str" and merged and merged[-1].kind == "str":
            merged[-1] = Tok("str", merged[-1].text[:-1] + t.text[t.text.index('"') + 1:], merged[-1].line, merged[-1].pos)
        else:
            merged.append(t)
    return merged

def num_value(text):
    t = text.replace("'", "").rstrip("fFuUlL")
    try:
        return float(int(t, 16)) if t[:2] in ("0x", "0X") else float(t)
    except ValueError:
        return None

# ---------------------------------------------------------------- categories
FAMILY = {
    "size": ("ImVec2", "CalcItemWidth", "SetNextItemWidth", "PushItemWidth", "SetColumnWidth", "TableSetupColumn", "BeginChild",
             "InvisibleButton", "Button", "SmallButton", "Selectable", "Rect", "Fixed", "Dummy"),
    "colour": ("ImVec4", "IM_COL32", "GetColorU32", "PushStyleColor", "ColorConvertFloat4ToU32", "TextColored", "U32", "Ink", "WithAlpha",
               "Hex", "Tint", "TextTint", "TextCol", "ImColor", "wellCol", "vec", "Lo", "Hi", "Mix", "Shade", "Lighten", "Darken",
               "ColorConvertHSVtoRGB", "Alpha", "Fade"),
    "layout": ("SameLine", "Indent", "Unindent", "SetCursorPos", "SetCursorPosX", "SetCursorPosY", "SetCursorScreenPos", "Spacing",
               "PushTextWrapPos", "SetScrollY", "SetScrollX", "Inline", "AudioKnobRow", "Columns", "BeginTable"),
    "window": ("SetNextWindowSize", "SetNextWindowSizeConstraints", "SetNextWindowPos", "SetNextWindowBgAlpha", "SetWindowSize", "Begin"),
    "style_var": ("PushStyleVar",),
    "draw": ("AddLine", "AddRect", "AddRectFilled", "AddRectFilledMultiColor", "AddCircle", "AddCircleFilled", "AddTriangle",
             "AddTriangleFilled", "AddQuad", "AddQuadFilled", "AddBezierCubic", "AddBezierQuadratic", "AddPolyline",
             "AddConvexPolyFilled", "AddText", "AddImage", "AddImageRounded", "PathArcTo", "PathArcToFast", "PathLineTo",
             "PathStroke", "PathFillConvex", "PathRect", "PathBezierCubicCurveTo", "PathBezierQuadraticCurveTo", "AddNgon",
             "AddNgonFilled", "Draw", "DrawStar", "DrawIcon"),
    "clamp": ("clamp", "min", "max", "fmin", "fmax", "Clamp", "ImClamp", "ImMin", "ImMax", "saturate"),
    "slider": ("SliderFloat", "SliderInt", "DragFloat", "DragInt", "DragFloat2", "DragFloat3", "VSliderFloat", "InputFloat",
               "InputInt", "SliderAngle", "DragFloatRange2"),
    # a node param's range/default in a body helper: model data, listed so ranges can move to the param table
    "param_range": ("ModSlider", "ModSliderInt", "FloatRow", "AudioSlider", "Slider", "SliderRow", "Knob", "KnobInt", "BarSlider",
                    "DragRow", "MoreSlider", "MorePct", "DropdownKnob", "NoteField", "DragMulti", "AudioBareDropdown", "IntRow",
                    "AudioKnob", "AudioKnobInt", "PctRow", "Pot"),
    "canvas_pos": ("SpawnNode", "AddPerfElementToCurrentPage"),
    "value_format": ("snprintf", "ImFormatString", "Format", "StrFormat",
                     # UiFmt (src/app/ui/design/UiFmt.h): the scaling into its unit, e.g. Pct(mix * 100.0f)
                     "Hz", "KHz", "Db", "DbOffset", "Ms", "Sec", "Pct", "Semi", "Cents", "Ratio", "Px", "Deg", "Bpm", "Lufs",
                     "Beats", "Bytes", "KB", "MB", "Timecode"),   # unit maths inside a formatter call: the number formatter's job
    "id_stack": ("PushID", "PopID", "GetID", "PopStyleColor", "PopStyleVar"),
    "key": ("IsKeyPressed", "IsKeyDown", "IsKeyReleased", "IsKeyChordPressed", "Shortcut", "digit", "KeyPressed"),
    "cursor": ("SetMouseCursor",),
    "mouse": ("IsMouseDragging", "IsMouseClicked", "IsMouseReleased", "IsMouseDoubleClicked", "IsMouseDown", "GetMouseDragDelta",
              "IsMouseHoveringRect", "IsItemClicked", "ResetMouseDragDelta"),
    "font": ("AddFontFromFileTTF", "AddFontFromMemoryTTF", "AddFontFromMemoryCompressedTTF", "PushFont", "SetWindowFontScale",
             "GetFontSize", "CalcTextSize"),
    "anim": ("Value", "Hover", "Ease", "Spring", "Lerp", "ImLerp", "lerp", "mix", "smoothstep"),
    "maths": ("sin", "cos", "tan", "atan2", "pow", "powf", "exp", "log", "log10", "log2", "sqrt", "fmod", "floor", "ceil", "round",
              "lround", "abs", "fabs", "sinf", "cosf", "hypot", "exp2", "copysign", "trunc"),
    "format": (),   # set from the string, not the call
}
CALL_FAMILY = {name: fam for fam, names in FAMILY.items() for name in names}
TEXT_CALLS = {"Text", "TextDisabled", "TextWrapped", "TextUnformatted", "BulletText", "TextColored", "LabelText", "SeparatorText",
              "MenuItem", "BeginMenu", "Selectable", "Button", "SmallButton", "Checkbox", "RadioButton", "CollapsingHeader",
              "TreeNode", "TreeNodeEx", "SetTooltip", "SetItemTooltip", "BeginCombo", "Combo", "BeginTabItem", "OpenPopup",
              "InputText", "InputTextWithHint", "SliderFloat", "SliderInt", "DragFloat", "DragInt", "ColorEdit3", "ColorEdit4",
              "BeginPopup", "BeginPopupModal", "Begin", "BeginChild", "Draw", "Item", "Choice", "Heading", "AddText", "HelpTip",
              "Label", "Row", "Card", "Caption", "Section", "Notice", "Show"}
PRINTF_CALLS = {"snprintf", "sprintf", "ImFormatString", "Format", "StrFormat", "vsnprintf"}   # buffers that reach the screen; console printf is not UI
PIXEL_ONE = {"SameLine", "Dummy", "Indent", "Unindent", "SetCursorPosX", "SetCursorPosY", "PushStyleVar", "AddLine", "AddRect"}  # a 1 here is a pixel
KEYWORDS = {"if", "while", "switch", "return", "sizeof", "decltype", "alignof", "catch", "static_cast", "reinterpret_cast", "const_cast",
            "noexcept", "defined"}
STRUCTURE_CALLS = {"static_assert", "alignas", "sizeof", "reserve", "resize", "copy", "copy_n", "fill", "fill_n", "memcpy", "memset",
                   "substr", "at", "erase", "assign", "compare", "find", "rfind", "bitset", "array", "get", "make_index_sequence"}
TEST_FUNC = re.compile(r"(SelfCheck|SelfTest|Test|AffinityAb|AuditState|Bench|Probe|Census|Fuzz)", re.I)
HARNESS = re.compile(r'getenv\s*\(\s*"[^"]*(TEST|BENCH|PROBE|AUDIT|CENSUS)|\bargv\b|\bargc\b')
I18N_WRAP = {"L", "T", "TC", "LC", "TList", "I18N_KEY", "HelpT"}
# a theme is read from what the statement calls, or from the name of the variable it sets (never from any word in it)
THEME = [
    ("timing", re.compile(r"\b(GetTime|DeltaTime|steady_clock|system_clock|milliseconds|microseconds|sleep_for|wait_for)\b"),
               re.compile(r"(time|timer|hold|delay|duration|dur$|secs?$|seconds$|ms$|Ms$|period|interval|timeout|blink|flash|pulse|"
                          r"elapsed|debounce|anim|glide|tween|cooldown|grace|age$)", re.I)),
    ("hit", None, re.compile(r"(hit|grab|tol$|tolerance|pick|slop|reach|catch|snapDist|snapPx|handleR|hoverR|nearPx|thresholdPx)", re.I)),
    ("font", re.compile(r"\b(AddFontFrom\w+|SizePixels|PushFont|SetWindowFontScale)\b"), re.compile(r"(font|glyph)(Size|Px|Scale)", re.I)),
]
FMT = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfgGaAcsp]")

def fmt_category(s):
    specs = FMT.findall(s)
    return specs

# ---------------------------------------------------------------- scan
def ui_files():
    for base in ("src",):
        for f in sorted((ROOT / base).rglob("*")):
            if f.suffix not in (".cpp", ".h", ".mm", ".hpp"): continue
            rel = str(f.relative_to(ROOT))
            if SKIP.search(rel): continue
            t = f.read_text(errors="ignore")
            if UI_FILE.search(t): yield rel, t

STMT_END = {";", "{", "}"}
UNARY_PREV = {"(", ",", "=", "return", "?", ":", "[", "{", "+", "-", "*", "/", "<", ">", "<=", ">=", "==", "!=", "&&", "||",
              "+=", "-=", "*=", "/=", "case"}

TRACE = []   # (rel, line, reason) of allowed literals, filled only when --why is used

class _Allowed(collections.Counter):
    def __init__(self, trace, rel): super().__init__(); self._t, self._rel, self.line = trace, rel, 0
    def __missing__(self, k): return 0
    def bump(self, k, line):
        self[k] += 1
        if WHY: self._t.append((self._rel, line, k))

WHY = False

def scan_file(rel, src):
    toks = tokenize(src)
    findings, allowed, rawcalls = [], _Allowed(TRACE, rel), collections.Counter()
    # matching parens / brackets
    match = {}
    stack = []
    for i, t in enumerate(toks):
        if t.kind == "op" and t.text in "([{":
            stack.append(i)
        elif t.kind == "op" and t.text in ")]}" and stack:
            j = stack.pop(); match[i] = j; match[j] = i
    # context walk
    paren = []          # (call name, open index, is_for, qual)
    brack = 0
    funcs = []          # (brace index, name)
    brace_kind = []     # "func"/"enum"/"class"/"other"
    harness_stack = []  # inside a self-test function or a getenv("..TEST")/argv block
    stmt_start = 0
    in_kit = rel.startswith(KIT)

    def func_name():
        for _, nm in reversed(funcs):
            if nm: return nm
        return "-"

    def stmt_lhs(a, i):
        """name of the variable the statement (or the init this literal is in) sets: the id right before the last `=` before i"""
        for k in range(i - 1, a - 1, -1):
            if toks[k].kind == "op" and toks[k].text in ("=", "+=", "-=", "*=", "/="):
                j = k - 1
                while j >= a and toks[j].kind == "op" and toks[j].text in ("]", ")"): j = match.get(j, j) - 1
                return toks[j].text if j >= a and toks[j].kind == "id" else ""
        return ""

    def stmt_text(i):
        a = stmt_start; b = i
        while b < len(toks) and not (toks[b].kind == "op" and toks[b].text in STMT_END): b += 1
        return " ".join(t.text for t in toks[a:b] if t.kind in ("id", "op"))

    for i, t in enumerate(toks):
        if t.kind == "pp":
            continue
        if t.kind == "op":
            if t.text == "(":
                p = toks[i - 1] if i else None
                name = p.text if p is not None and p.kind == "id" and p.text not in KEYWORDS else ""
                if name and i >= 2 and toks[i - 2].kind == "id" and toks[i - 2].text[:1].isupper() and toks[i - 2].text not in KEYWORDS:
                    name = toks[i - 2].text   # `ImVec2 c(...)`: the type is the call
                qual = ""
                if name and i >= 3 and toks[i - 2].text in ("::", "->", "."):
                    qual = toks[i - 3].text
                is_for = name in ("for",)
                paren.append((name, i, is_for, qual))
                if name and qual == "ImGui" and not in_kit:
                    rawcalls[name] += 1
            elif t.text == ")":
                if paren: paren.pop()
            elif t.text == "[":
                brack += 1
            elif t.text == "]":
                brack = max(0, brack - 1)
            elif t.text == "{":
                # function body? ... ) [const|noexcept|override|final|-> type]* {
                k = i - 1
                while k >= 0 and toks[k].kind == "id" and toks[k].text in ("const", "noexcept", "override", "final", "mutable"): k -= 1
                kind, nm = "other", ""
                if k >= 0 and toks[k].text == ")" and k in match:
                    o = match[k]
                    if o >= 1 and toks[o - 1].kind == "id" and toks[o - 1].text not in ("if", "for", "while", "switch", "catch", "return"):
                        kind, nm = "func", toks[o - 1].text
                    elif o >= 1 and toks[o - 1].text == "]":
                        kind, nm = "func", ""
                back = " ".join(x.text for x in toks[max(0, i - 4):i])
                if re.search(r"\benum\b", back): kind = "enum"
                elif kind == "other" and re.search(r"\b(struct|class|union)\b", " ".join(x.text for x in toks[max(0, i - 8):i])): kind = "class"
                head = src[toks[stmt_start].pos if stmt_start < i else t.pos:t.pos]
                harness = ((bool(harness_stack) and harness_stack[-1]) or (kind == "func" and bool(TEST_FUNC.search(nm)))
                           or (kind == "other" and bool(HARNESS.search(head))))
                funcs.append((i, nm if kind == "func" else None)); brace_kind.append(kind); harness_stack.append(harness)
                stmt_start = i + 1
            elif t.text == "}":
                if funcs: funcs.pop(); brace_kind.pop(); harness_stack.pop()
                stmt_start = i + 1
            elif t.text == ";":
                stmt_start = i + 1
            continue

        if t.kind == "id" and t.text in ("pal", "palf") and i >= 2 and toks[i - 1].text == "::" and toks[i - 2].text == "tok":
            if not (harness_stack and harness_stack[-1]) and i + 2 < len(toks):
                findings.append(("palette", paren[-1][0] if paren else "-", "tok::" + t.text + "::" + toks[i + 2].text, "role-less colour")
                                + (rel, t.line, func_name()))
            continue
        if t.kind not in ("num", "str"):
            continue
        if harness_stack and harness_stack[-1]:
            allowed.bump("test", t.line); continue
        call = paren[-1][0] if paren else ""
        qual = paren[-1][3] if paren else ""
        in_for = any(p[2] for p in paren)
        in_enum = bool(brace_kind) and brace_kind[-1] == "enum"
        prev = toks[i - 1] if i else None
        nxt = toks[i + 1] if i + 1 < len(toks) else None
        where = (rel, t.line, func_name())

        if t.kind == "str":
            s = t.text[t.text.index('"') + 1:-1] if '"' in t.text else t.text
            wrapped = any(p[0] in I18N_WRAP for p in paren)
            specs = fmt_category(s)
            if specs and (call in TEXT_CALLS or call in PRINTF_CALLS or call in I18N_WRAP or any(p[0] in TEXT_CALLS | PRINTF_CALLS for p in paren)):
                findings.append(("format", call or "-", " ".join(specs), s[:80]) + where)
                for um in ([] if ("=%" in s or s.startswith("[")) else UNIT_SPEC.finditer(s)):   # a number printed with its unit: UiFmt's job (owner 2026-11-24)
                    findings.append(("unit", call or "-", um.group(0), s[:80]) + where)
            visible = call in TEXT_CALLS and re.search(r"[A-Za-z]", FMT.sub("", s.split("##")[0]))
            if not visible:
                continue
            if s.startswith("##") or ("###" in s and s.split("###")[0] == ""):
                allowed.bump("id", t.line); continue
            if wrapped or (prev is not None and prev.text in I18N_WRAP):
                allowed.bump("i18n", t.line); continue
            findings.append(("label", call, "", s[:80]) + where)
            continue

        # number
        v = num_value(t.text)
        neg = prev is not None and prev.text == "-" and (i < 2 or toks[i - 2].text in UNARY_PREV or toks[i - 2].kind == "pp")
        if v is not None and neg: v = -v
        if in_for: allowed.bump("loop", t.line); continue
        if brack: allowed.bump("index", t.line); continue
        if in_enum: allowed.bump("enum", t.line); continue
        if prev is not None and prev.text == "case": allowed.bump("case", t.line); continue
        if v == 0.0 or (v in (1.0, -1.0) and not any(p[0] in PIXEL_ONE for p in paren) and not THEME[0][1].search(stmt_text(i))):
            allowed.bump("0/1/-1", t.line); continue
        if v == 0.5 and ((prev is not None and prev.text in ("*", "/")) or (nxt is not None and nxt.text in ("*", "/"))):
            allowed.bump("half", t.line); continue
        if call in STRUCTURE_CALLS:
            allowed.bump("structure", t.line); continue
        st = stmt_text(i)
        cat = None
        if call in CALL_FAMILY:
            fam = CALL_FAMILY[call]
            if fam in ("key", "cursor", "font"): cat = fam
        if cat is None:
            lhs = stmt_lhs(stmt_start, i)
            for name, call_rx, name_rx in THEME:
                if (call_rx and call_rx.search(st)) or (lhs and name_rx.search(lhs)): cat = name; break
        if cat is None:
            if call in CALL_FAMILY: cat = CALL_FAMILY[call]
            elif call: cat = "call:" + call
            elif brace_kind and brace_kind[-1] == "class": cat = "member_default"
            else:
                if re.search(r"\b(constexpr|const|static)\b.*=", st) and (prev is not None and prev.text == "="): cat = "const_def"
                elif prev is not None and prev.text in ("<", ">", "<=", ">=", "==", "!="): cat = "compare"
                elif prev is not None and prev.text in ("=", "+=", "-=", "*=", "/=", "return", "?", ":"): cat = "assign"
                else: cat = "arith"
        findings.append((cat, call or "-", t.text if not neg else "-" + t.text, st[:110]) + where)
    return findings, allowed, rawcalls

WIDGET_FAMILY = {
    "button": ("Button", "SmallButton", "ArrowButton", "ImageButton", "InvisibleButton"),
    "toggle": ("Checkbox", "RadioButton"),
    "slider": ("SliderFloat", "SliderInt", "DragFloat", "DragInt", "VSliderFloat", "DragFloat2", "DragFloat3", "SliderAngle"),
    "input": ("InputText", "InputTextWithHint", "InputTextMultiline", "InputFloat", "InputInt"),
    "dropdown": ("BeginCombo", "Combo", "EndCombo"),
    "colour": ("ColorButton", "ColorEdit3", "ColorEdit4", "ColorPicker3", "ColorPicker4"),
    "popup_menu": ("BeginPopup", "BeginPopupModal", "BeginPopupContextItem", "BeginPopupContextWindow", "BeginPopupContextVoid",
                   "OpenPopup", "CloseCurrentPopup", "BeginMenu", "MenuItem", "EndMenu", "EndPopup", "IsPopupOpen"),
    "tooltip": ("SetTooltip", "SetItemTooltip", "BeginTooltip", "BeginItemTooltip", "EndTooltip"),
    "text": ("Text", "TextDisabled", "TextWrapped", "TextUnformatted", "TextColored", "BulletText", "LabelText", "SeparatorText"),
    "list_table": ("BeginTable", "EndTable", "TableSetupColumn", "TableNextColumn", "TableNextRow", "TableHeadersRow", "Selectable",
                   "BeginListBox", "EndListBox"),
    "tree_tab": ("CollapsingHeader", "TreeNode", "TreeNodeEx", "TreePop", "BeginTabBar", "BeginTabItem", "EndTabBar", "EndTabItem"),
    "window_child": ("Begin", "End", "BeginChild", "EndChild", "SetNextWindowPos", "SetNextWindowSize", "SetNextWindowSizeConstraints",
                     "SetNextWindowBgAlpha", "SetNextWindowFocus"),
    "layout": ("SameLine", "Dummy", "Spacing", "Separator", "Indent", "Unindent", "NewLine", "BeginGroup", "EndGroup",
               "SetCursorPos", "SetCursorPosX", "SetCursorPosY", "SetCursorScreenPos", "AlignTextToFramePadding", "SetNextItemWidth",
               "PushItemWidth", "PopItemWidth", "PushTextWrapPos", "PopTextWrapPos"),
    "style": ("PushStyleColor", "PopStyleColor", "PushStyleVar", "PopStyleVar", "PushFont", "PopFont", "SetWindowFontScale",
              "BeginDisabled", "EndDisabled"),
    "input_state": ("IsItemHovered", "IsItemActive", "IsItemClicked", "IsItemActivated", "IsItemDeactivated", "IsItemDeactivatedAfterEdit",
                    "IsMouseClicked", "IsMouseDown", "IsMouseReleased", "IsMouseDragging", "IsMouseDoubleClicked", "IsMouseHoveringRect",
                    "IsWindowHovered", "IsWindowFocused", "IsAnyItemActive", "IsAnyItemHovered", "GetMouseDragDelta",
                    "ResetMouseDragDelta", "SetKeyboardFocusHere", "SetItemDefaultFocus", "ClearActiveID"),
    "keys": ("IsKeyPressed", "IsKeyDown", "IsKeyReleased", "IsKeyChordPressed", "Shortcut"),
    "cursor": ("SetMouseCursor",),
    "image": ("Image", "ImageWithBg"),
    "progress": ("ProgressBar",),
    "drag_drop": ("BeginDragDropSource", "BeginDragDropTarget", "SetDragDropPayload", "AcceptDragDropPayload",
                  "EndDragDropSource", "EndDragDropTarget"),
    "id": ("PushID", "PopID", "GetID"),
}
WIDGET_OF = {n: f for f, ns in WIDGET_FAMILY.items() for n in ns}

# A printf number followed by a unit that UiFmt (src/app/ui/design/UiFmt.h) owns. Machine-readable log lines
# ("key=%d", "[tag] ...") are not on screen and are not counted.
UNIT_SPEC = re.compile(r"(?<![=%])%[-+ #0]*\d*(?:\.\d+)?(?:l|ll|z)?[dfgiu](?: ?(?:kHz|Hz|dB|LUFS|ms|BPM|bpm|px|MB|KB)(?![A-Za-z])"
                       r"| ?s(?![A-Za-z0-9])|%%| ?st(?![A-Za-z])|\\xC2\\xB0|\u00b0)")

def run(files=None):
    all_f, allowed, raw = [], collections.Counter(), collections.Counter()
    for rel, src in ui_files():
        if files and not any(rel.endswith(x) for x in files): continue
        f, a, r = scan_file(rel, src)
        all_f += f; allowed.update(dict(a)); raw += r
    return all_f, allowed, raw

def counts(findings):
    by = collections.Counter(f[0] for f in findings)
    return dict(sorted(by.items()))

def report(findings, allowed, raw, out):
    by_cat = collections.defaultdict(list)
    for f in findings: by_cat[f[0]].append(f)
    L = ["# UI audit (generated by tools/design/ui_audit.py)\n",
         "Every hardcoded number and visible string in UI files, by allowlist. Regenerate with `python3 tools/design/ui_audit.py`.",
         "Kit files (`src/app/ui/design/`) are included and marked; self-tests, tests and generated headers are not.\n",
         f"Total findings: {len(findings)} (kit {sum(1 for f in findings if f[4].startswith(KIT))}). "
         f"Allowed: " + ", ".join(f"{k} {v}" for k, v in allowed.most_common()) + ".\n",
         "## Categories\n", "| category | findings | outside kit | files | top files |", "|---|---|---|---|---|"]
    for cat, fs in sorted(by_cat.items(), key=lambda kv: -len(kv[1])):
        files = collections.Counter(f[4] for f in fs)
        top = ", ".join(f"{pathlib.Path(k).name} {v}" for k, v in files.most_common(3))
        L.append(f"| {cat} | {len(fs)} | {sum(1 for f in fs if not f[4].startswith(KIT))} | {len(files)} | {top} |")
    L += ["\n## Raw ImGui calls outside the kit, by widget family\n", "| family | calls | top calls |", "|---|---|---|"]
    fam = collections.defaultdict(collections.Counter)
    for name, n in raw.items():
        f = WIDGET_OF.get(name) or ("query" if re.match(r"(Get|ColorConvert|Calc|IsWindow|IsAny|IsRect)", name) else "other:" + name)
        fam[f][name] += n
    for f, c in sorted(fam.items(), key=lambda kv: -sum(kv[1].values())):
        L.append(f"| {f} | {sum(c.values())} | " + ", ".join(f"{k} {v}" for k, v in c.most_common(4)) + " |")
    L.append("\n## Findings by category, then file\n")
    for cat, fs in sorted(by_cat.items(), key=lambda kv: -len(kv[1])):
        L.append(f"\n### {cat} ({len(fs)})\n")
        byfile = collections.defaultdict(list)
        for f in fs: byfile[f[4]].append(f)
        for file, items in sorted(byfile.items(), key=lambda kv: -len(kv[1])):
            L.append(f"- **{file}** ({len(items)}): " + "; ".join(f"{x[5]} `{x[2]}`" + (f" in {x[1]}" if x[1] != "-" else "")
                                                               for x in sorted(items, key=lambda x: x[5])[:60])
                     + (" ..." if len(items) > 60 else ""))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(L) + "\n")

def ratchet(mode):
    findings, _, _ = run()
    cur = collections.Counter()
    for f in findings:
        if not f[4].startswith(KIT): cur["call" if f[0].startswith("call:") else f[0]] += 1   # one bucket for unnamed calls
    cur = dict(sorted(cur.items()))
    raw = json.loads(STORE.read_text()) if STORE.exists() else {}
    old = raw.get("audit")
    if mode == "reset" or (mode == "update" and old is None):
        raw["audit"] = cur
    elif mode == "update":
        raw["audit"] = {k: min(v, old.get(k, v)) for k, v in cur.items()}
    if mode in ("reset", "update"):
        STORE.write_text(json.dumps(raw, indent=1) + "\n"); return 0
    if old is None:
        print("ui_audit: no 'audit' baseline yet: run ui_audit.py --ratchet --update"); return 0
    bad = [(k, v, old.get(k, 0)) for k, v in cur.items() if v > old.get(k, 0)]
    print(f"ui_audit: {sum(cur.values())} findings outside the kit (stored {sum(old.values())}), {len(cur)} categories")
    for k, v, a in bad: print(f"RATCHET: ui_audit category {k} has {v} (allowed {a}): use a token or a kit part")
    return 1 if bad else 0

def main():
    a = sys.argv[1:]
    if "--ratchet" in a:
        return ratchet("reset" if "--reset" in a else "update" if "--update" in a else "check")
    if "--why" in a:   # --why FILE LINE: why each literal on that line was allowed
        global WHY; WHY = True
        f, ln = a[a.index("--why") + 1], int(a[a.index("--why") + 2])
        findings, _, _ = run([f])
        for x in findings:
            if x[5] == ln: print("finding", x[0], repr(x[2]), "in", x[1], x[6])
        for r in TRACE:
            if r[1] == ln: print("allowed", r[2])
        return 0
    if "--file" in a:
        files = a[a.index("--file") + 1:]
        findings, allowed, _ = run(files)
        for f in sorted(findings, key=lambda x: (x[4], x[5])):
            print(f"{f[4]}:{f[5]} [{f[0]}] {f[2]!r} in {f[1]} ({f[6]}) | {f[3]}")
        print("allowed:", dict(allowed)); return 0
    out = pathlib.Path(a[a.index("--out") + 1]) if "--out" in a else ROOT / "internal/plans/ui-audit.md"
    findings, allowed, raw = run()
    cats = collections.Counter(f[0] for f in findings)
    outside = collections.Counter(f[0] for f in findings if not f[4].startswith(KIT))
    print(f"{'category':<26} {'all':>7} {'outside kit':>12}")
    for k, v in cats.most_common(): print(f"{k:<26} {v:>7} {outside.get(k, 0):>12}")
    print(f"{'TOTAL':<26} {len(findings):>7} {sum(outside.values()):>12}")
    print("allowed:", ", ".join(f"{k} {v}" for k, v in allowed.most_common()))
    print("raw ImGui calls outside the kit:", sum(raw.values()))
    report(findings, allowed, raw, out)
    print("wrote", out)
    return 0

if __name__ == "__main__":
    sys.exit(main())
