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
  tools/design/ui_audit.py --cat C...      print every finding in these categories outside the kit
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
# families whose numbers are not design values (owner 2026-11-24): a node's canvas spot is patch data and stays with
# the patch; an ImGui id or a style-stack pop count is bookkeeping, never drawn
DATA_FAMILY = {"canvas_pos": "patch_data", "id_stack": "id_stack"}
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
TEST_FUNC = re.compile(r"(SelfCheck|SelfTest|Test|AffinityAb|Audit\w*State|Bench|Probe|Census|Fuzz)", re.I)
HARNESS = re.compile(r'getenv\s*\(\s*"[^"]*(TEST|BENCH|PROBE|AUDIT|CENSUS)|\bargv\b|\bargc\b')
I18N_WRAP = {"L", "T", "TC", "LC", "TList", "I18N_KEY", "HelpT"}
SHORTCUT = re.compile(r"^(?:\+?(?:Shift|Alt|Ctrl|Cmd|Opt)\+)*\+?(?:[A-Z0-9]|F\d{1,2}|Backspace|Delete|Tab|Esc|Enter|Home|End|PgUp|PgDn|"
                      r"Space|Click|Double-click(?: top)?)$")
# the same English-by-design scope as tools/i18n/lint.py (decision D2): node bodies' param names and values
I18N_ENGLISH = ("src/app/bodies/", "src/app/ui/ParamWidgets.cpp")   # node body, English by owner decision 2026-11-24 (docs/DECISIONS.md)
ID_CALLS = {"OpenPopup", "BeginPopup", "BeginPopupModal", "BeginPopupContextItem", "BeginPopupContextWindow", "IsPopupOpen",
            "PushID", "GetID", "BeginChild", "BeginTable", "BeginTabBar", "BeginListBox", "BeginContextItem"}   # an id, never shown
# a theme is read from what the statement calls, or from the name of the variable it sets (never from any word in it)
THEME = [
    ("timing", re.compile(r"\b(GetTime|DeltaTime|steady_clock|system_clock|milliseconds|microseconds|sleep_for|wait_for)\b"),
               re.compile(r"(time|timer|hold|delay|duration|dur$|secs?$|seconds$|ms$|Ms$|period|interval|timeout|blink|flash|pulse|"
                          r"elapsed|debounce|anim|glide|tween|cooldown|grace|age$)", re.I)),
    # matched on whole camelCase words, so whiteCount or pickedUnit are not hit sizes
    ("hit", None, re.compile(r"(?:^|_)(?:hit|grab|tol|tolerance|slop|reach|catch)(?![a-z])|(?:Hit|Grab|Tol|Tolerance|Slop|Reach|Catch)"
                             r"(?![a-z])|snapDist|snapPx|SnapPx|handleR\b|hoverR|nearPx|thresholdPx")),
    ("font", re.compile(r"\b(AddFontFrom\w+|SizePixels|PushFont|SetWindowFontScale)\b"), re.compile(r"(font|glyph)(Size|Px|Scale)", re.I)),
]
FMT = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfgGaAcsp]")

PLAIN_SPEC = re.compile(r"%(?:hh|h|ll|l|z|j|t)?[sdiuc]")   # no width, precision or sign: the number's look is not chosen here

def fmt_category(s):
    if s.startswith("##"): return []   # an id ("##row%p"), never shown
    # "%%" is a percent sign; "% o" / "% to" is prose ("10% to 100%"), not a space-flag spec
    return [x for x in FMT.findall(s.replace("%%", "")) if not x.startswith("% ")]

# ---------------------------------------------------------------- scan
def ui_files():
    # every source file under src/app and src/core (owner 2026-11-24): a file with no screen values comes back clean
    for base in ("src/app", "src/core"):
        for f in sorted((ROOT / base).rglob("*")):
            if f.suffix not in (".cpp", ".h", ".mm", ".hpp", ".inl"): continue
            rel = str(f.relative_to(ROOT))
            if SKIP.search(rel): continue
            yield rel, f.read_text(errors="ignore")

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
    ok_lines, skipping = set(), False
    for n, l in enumerate(src.split("\n"), 1):   # // i18n-ok lines and i18n-skip-begin/end regions, as tools/i18n/lint.py
        skipping = (skipping or "i18n-skip-begin" in l) and "i18n-skip-end" not in l
        if skipping or "i18n-ok" in l: ok_lines.add(n)
    # `// ui-audit-ok: <reason>` on a line: not on screen (a file name, a log line); the reason is required
    # (owner 2026-11-24) the reason's first word must be one of OK_WORDS; any other reason is a `bad_ok` finding and
    # excuses nothing. A valid tag never excuses a literal that reaches a colour, alpha, position or size (design_sink).
    off_lines, bad_ok = {}, []
    for n, l in enumerate(src.split("\n"), 1):
        m = OK_TAG.search(l)
        if not m: continue
        if m.group(1) in OK_WORDS: off_lines[n] = m.group(1)
        else: bad_ok.append(("bad_ok", "-", m.group(1) or "(none)", "ui-audit-ok reason must be one of " + ", ".join(sorted(OK_WORDS)), rel, n, "-"))

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
        if t.line in off_lines and not (t.kind == "num" and (alpha_mask(t.text) or (not selector(toks, match, i) and
                design_sink(paren, stmt_text(i), [] if off_lines[t.line] in DATA_WORDS else
                            _neigh(toks, match, i) + [stmt_lhs(stmt_start, i)], toks, i, off_lines[t.line] in DATA_WORDS)))):
            allowed.bump("off_screen", t.line); continue
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
                if all(PLAIN_SPEC.fullmatch(x) for x in specs):   # a %s / %d slot in a sentence: no design value in it
                    allowed.bump("placeholder", t.line)
                elif call in I18N_WRAP and any(p[0] in FAMILY["slider"] for p in paren):
                    # a slider's own display format with a translated unit word ("%.0f ticks"): ImGui prints the value
                    allowed.bump("widget_format", t.line)
                else:
                    findings.append(("format", call or "-", " ".join(specs), s[:80]) + where)
                for um in ([] if ("=%" in s or s.startswith("[")) else UNIT_SPEC.finditer(s)):   # a number printed with its unit: UiFmt's job (owner 2026-11-24)
                    findings.append(("unit", call or "-", um.group(0), s[:80]) + where)
            visible = call in TEXT_CALLS and re.search(r"[A-Za-z]", re.sub(r"\\x[0-9A-Fa-f]{2}|\\[nt]", "", FMT.sub("", s.split("##")[0])))
            if not visible:
                continue
            if s.startswith("##") or ("###" in s and s.split("###")[0] == ""):
                allowed.bump("id", t.line); continue
            if wrapped or (prev is not None and prev.text in I18N_WRAP):
                allowed.bump("i18n", t.line); continue
            if call in ID_CALLS or (re.fullmatch(r"[a-z][a-z0-9_]*(?:\.[a-z0-9_]+)*", s) and call not in ("Text", "TextUnformatted", "TextWrapped")):
                # a dotted lower-case name ("lib.tabs", "row") is a widget id, not words
                allowed.bump("id", t.line); continue
            if call == "Begin":   # a window title is also the window's saved id (imgui.ini): translating it loses the layout
                allowed.bump("window_id", t.line); continue
            if call in ("Item", "MenuItem") and SHORTCUT.match(s):   # a menu row's key hint: key names, never translated
                allowed.bump("shortcut", t.line); continue
            if rel.startswith(I18N_ENGLISH) or t.line in ok_lines:
                allowed.bump("english", t.line); continue
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
        if v == 255.0 and ((prev is not None and prev.text in ("*", "/")) or (nxt is not None and nxt.text in ("*", "/"))):
            allowed.bump("unit_conv", t.line); continue   # a colour channel between 0-1 and 0-255
        if call in STRUCTURE_CALLS:
            allowed.bump("structure", t.line); continue
        if CALL_FAMILY.get(call) in DATA_FAMILY:
            allowed.bump(DATA_FAMILY[CALL_FAMILY[call]], t.line); continue
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
        if cat in LOGIC_CATS or cat in ("timing", "hit", "value_format", "const_def", "member_default") or cat.startswith("call:"):
            r = logic_reason(toks, match, i, t, v, prev, nxt, paren, st, stmt_lhs(stmt_start, i), cat, rel)
            if r: allowed.bump(r, t.line); continue
        findings.append((cat, call or "-", t.text if not neg else "-" + t.text, st[:110]) + where)
    if not UI_FILE.search(src):
        # no screen API in the file (engine maths, file formats, CLI, the param tables, the theme source): nothing it holds
        # is drawn from here, so every literal is allowed and counted, and the file comes back clean (owner 2026-11-24)
        for f in findings: allowed.bump("no_screen", f[5])
        findings = []
    return findings + bad_ok, allowed, rawcalls

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

# ---------------------------------------------------------------- logic vs design (arith/compare/assign/clamp/maths/call:*)
# A number is a design value when it sits next to a screen quantity (a position, width, padding, font height, a tok::
# value, an ImVec2); otherwise it is the program's own logic and is allowed with the reason it has:
#   bits (masks, shifts, byte maths), mode (== a kind/enum/count), epsilon (a guard below 0.01), domain (dB, Hz, BPM,
#   notes, samples, params: the model's units), unit_conv (100 %, 1000 ms, 60 s, 12 semitones, pi).
LOGIC_CATS = {"arith", "compare", "assign", "clamp", "maths"}
GEOM_NAME = re.compile(
    r"^(?:x|y|w|h|[xy][0-9]|cx|cy|px|py|dx|dy|r|rad|radius|width|height|size|pad|gap|top|bot|bottom|left|right|avail|origin|pos|"
    r"mouse|bb|tl|br|area|rect|cell|row|lane|line|indent|inset|margin|spacing|thick|stroke|thickness|scroll\w*|zoom|"
    r"\w*(?:W|H|X|Y|Pad|Width|Height|Size|Gap|Inset|Margin|Radius|Rad|Px|Top|Left|Right|Bottom|Rect|Pos|Spacing|Indent|Thick|Stroke)|"
    r"Get(?:FontSize|FrameHeight\w*|TextLineHeight\w*|ContentRegion\w*|Cursor\w*|Window\w*|ItemRect\w*|MousePos\w*|Scroll\w*)|"
    r"CalcTextSize|ItemSpacing|FramePadding|WindowPadding|ItemInnerSpacing|FontSize|MousePos|DisplaySize|ImVec2|ImRect|"
    r"(?:space|radius|hit|pin|icon|bar|stroke|row|page|dock|cell|ruler|fader|topbar|fold|compact|desk|arrange|chat|drag|type)_\w+)$")
GEOM_CTX = {"ImVec2", "ImRect", "SetNextItemWidth", "PushItemWidth", "Dummy", "InvisibleButton", "BeginChild", "SetCursorPos",
            "SetCursorPosX", "SetCursorPosY", "SetCursorScreenPos", "SameLine", "IsMouseHoveringRect", "ItemSize", "ItemAdd"}
# A literal that flows into a colour / alpha or into a pixel position / size is a design value whatever other reason
# matches (owner 2026-11-24): the bits / domain / unit_conv / no_geometry reasons and a ui-audit-ok tag do not excuse it.
COLOUR_SINK = {"IM_COL32", "Base", "Role", "BaseV4", "RoleV4", "U32", "WithAlpha", "GetColorU32", "ColorConvertFloat4ToU32",
               "ColorConvertHSVtoRGB", "ImColor", "Tint", "TextTint", "PushStyleColor", "TextColored", "MulAlpha", "Alpha",
               "LerpCol", "LerpColor", "Mix", "Fade", "ScaleAlpha", "SetNextWindowBgAlpha"}
COLOUR_NAME = re.compile(r"^(?:a|alpha|fade|opacity|tint|shade|col|colour|color|rgba|\w*(?:Alpha|Fade|Opacity|Tint|Shade|Col|Colour|Color))$")
PIXEL_NAME = re.compile(r"^(?:tile|tiles|tileW|tileH|\w*Tile)$")


# The only reasons a `// ui-audit-ok: <word> [note]` tag may give (owner 2026-11-24). Each names a value that is data,
# never a look; the optional note after the word says which.
OK_WORDS = {
    "patch_data",   # values a patch saves or the startup/demo patch builds (node params, canvas spots)
    "model",        # a model value or default: counts, ranges, modes, settings defaults
    "index",        # an index, id, stride or slot layout
    "mask",         # a channel mask or bit packing
    "hash",         # hash salt / mixing / offset for an id
    "unit",         # unit maths: dB, degrees, percent, 0-255 <-> 0-1
    "bytes",        # byte / buffer / string-length arithmetic
    "sentinel",     # a bounds or "never" sentinel
    "timing",       # polling, pacing, smoothing, budgets, frame counters
    "key",          # a key code or key parsing
    "rpc",          # RPC parsing, defaults and ranges
    "log",          # a log format, file or folder name
    "core",         # src/core cannot include the app tokens
    "world",        # a 3D world-space quantity, not screen pixels
    "count",        # how many of something: lines, segments, cells, voices
    "setting",      # a default the user's settings file saves (render size, OS window restore box)
}
# Words that name data by what the value is: for these a variable's name ending in Col / Alpha / Tile (a guess) does not
# make the literal a design value; only a colour / geometry / draw call it is an argument of does.
DATA_WORDS = {"count", "setting", "patch_data", "index", "mask", "hash", "unit", "bytes", "sentinel", "key", "rpc", "log", "core"}
SEL_OPS = {"&", "|", "^", "<<", ">>", "%", "==", "!=", "<", ">", "<=", ">=", "&=", "|=", "^=", "<<=", ">>="}


def selector(toks, match, i):
    """the literal picks or extracts (an operand of a bit / modulo / compare op, or inside [ ]): it is never itself the
    colour or pixel value, so a colour or draw call around it does not make it one"""
    k = i - 1
    if k >= 0 and toks[k].text == "-": k -= 1
    if k >= 0 and toks[k].text in SEL_OPS: return True
    if i + 1 < len(toks) and toks[i + 1].text in SEL_OPS: return True
    d = 0
    for j in range(i - 1, max(-1, i - 200), -1):
        t = toks[j].text
        if t in (")", "]", "}"): d += 1
        elif t in ("(", "[", "{"):
            if d == 0: return t == "["
            d -= 1
        elif t in (";",) and d == 0: return False
    return False


OK_TAG = re.compile(r"//\s*ui-audit-ok:?\s*([A-Za-z_]*)")
DATA_REASON = (r"(?:patch data|byte|buffer|binary|log format|RPC|parsing|saved setting|settings default|off screen|bytes per pixel|"
               r"scrollback|param id|string length|model value|\bdB\b|sentinel|core cannot|bit packing|channel mask|hash|protocol|file)")
ANGLE = re.compile(r"\b(?:kPi|kTau|cos|sin|cosf|sinf|atan2?f?|angle|ang|theta|deg|rad)\b")


def kit_ok(rel):
    """the migration tools also move the kit's own literals with --kit (owner 2026-11-24); the token plumbing stays"""
    return "--kit" in sys.argv and not rel.endswith(("Tokens.gen.h", "TokenColors.h", "Glyphs.gen.h"))


def alpha_mask(text):
    """an ImU32 mask / or-in that sets the alpha byte to a chosen level (col & 0x80FFFFFF, | 0x40000000u)"""
    m = re.fullmatch(r"0[xX]([0-9A-Fa-f]{8})[uUlL]*", text)
    if not m: return False
    h = m.group(1).upper()
    return h[:2] not in ("00", "FF") and h[2:] in ("FFFFFF", "000000") and h != "7FFFFFFF"   # 0x7fffffff: an int-max id mask


COLOUR_ARG = {"TextColored": 0, "PushStyleColor": 1}   # calls where only one argument is the colour


def _arg_index(toks, open_i, i):
    """which argument (0-based) of the call opened at toks[open_i] the token i sits in"""
    d, n = 0, 0
    for j in range(open_i + 1, i):
        t = toks[j].text
        if t in "([{": d += 1
        elif t in ")]}": d -= 1
        elif t == "," and d == 0: n += 1
    return n


DATA_PIXEL_SKIP = {"ImVec2", "ImRect"}   # a vector type is not a screen sink by itself: canvas spots, sentinels


def design_sink(paren, st, names, toks=None, i=None, data=False):
    """'colour' or 'pixel' when the literal's value ends up on screen as a colour or a position / size, else ''.
    data=True (a ui-audit-ok line with a data word): ImVec2 / ImRect alone and a kit Draw() argument are not sinks; an
    ImDrawList Add* call, a layout / window / style call and every colour sink still are."""
    for p in paren:
        if p[0] in COLOUR_ARG and toks is not None and _arg_index(toks, p[1], i) != COLOUR_ARG[p[0]]: continue   # TextColored's text
        if p[0] in COLOUR_SINK or (p[0] == "ImVec4" and re.search(r"Col|col|colour|color", st)): return "colour"
    if any(COLOUR_NAME.match(n) for n in names if n and not (n == "a" and ANGLE.search(st))): return "colour"
    for p in paren:
        fam = CALL_FAMILY.get(p[0])
        if fam in ("layout", "window", "style_var") and p[3][:1].islower(): continue   # st.Begin: a member call, not ImGui::Begin
        if data and (p[0] in DATA_PIXEL_SKIP or (fam == "draw" and not p[0].startswith("Add"))): continue
        if p[0] in GEOM_CTX or fam in ("draw", "layout", "window", "style_var"): return "pixel"
    if any(GEOM_NAME.match(n) or PIXEL_NAME.match(n) for n in names if n): return "pixel"
    return ""


DSP_WORDS = {"db", "hz", "khz", "freq", "frequency", "cutoff", "reso", "resonance", "gain", "bpm", "tempo", "beat", "beats", "bar",
             "bars", "ppq", "tick", "ticks", "sample", "samples", "sr", "semi", "semis", "semitone", "semitones", "note", "notes",
             "midi", "pitch", "cents", "octave", "octaves", "phase", "vel", "velocity", "param", "ratio", "lufs", "rms", "peak",
             "amp", "amplitude", "level", "lvl", "mag", "bin", "bins", "fft", "depth", "rate", "decay", "attack", "release",
             "sustain", "feedback", "drive", "wet", "dry", "detune", "pan", "voice", "voices", "chan", "channel", "channels", "bus",
             "fps", "chord", "q", "hardness", "humanize", "lifetime", "emission", "weight", "exposure", "gamma"}
MODEL_WORDS = {"count", "num", "idx", "index", "kind", "mode", "type", "op", "state", "dock", "ref", "tab", "version", "seed",
               "hash", "rgb", "hue", "sat", "lum", "luma", "step", "steps", "bank", "preset", "frame", "frames", "mix", "port",
               "joint", "band", "lane", "page", "unit", "units", "orient", "sel", "handle", "pc", "val", "value", "raw"}
LONE = {"t", "u", "v", "k", "i", "j", "n", "s", "f", "e", "nt", "m", "c", "d"}


def _words(name):
    return {w.lower() for w in re.findall(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+", name)}


def is_domain(name, dsp_only=False):
    ws = _words(name.lstrip("gsk") if re.match(r"^[gsk][A-Z]", name) else name)
    if ws & DSP_WORDS: return True
    if dsp_only: return False
    return bool(ws & MODEL_WORDS) or name in LONE


UNIT_CONV = {100.0, 1000.0, 60.0, 12.0, 3600.0, 1e3, 1e6, 1e9, 1024.0, 255.0, 256.0, 127.0, 128.0, 440.0, 69.0, 24.0, 48.0, 96.0,
             44100.0, 48000.0, 360.0, 180.0, 90.0, 86400.0, 1048576.0, 1900.0, 20.0}
PI = (3.14159, 6.28318, 1.5707)


def _neigh(toks, match, i):
    """identifiers the number is combined with: the operand just before and just after it (a call or cast counts by name)"""
    out = []
    k = i - 1
    if k >= 0 and toks[k].text == "-" and (k == 0 or toks[k - 1].text in UNARY_PREV): k -= 1
    if k >= 0 and toks[k].kind == "op" and toks[k].text in ("+", "-", "*", "/", "<", ">", "<=", ">=", "==", "!=", "=", "+=", "-=",
                                                          "*=", "/=", "?", ":", "&", "|", "<<", ">>", "%", "^"):
        k -= 1
        if k >= 0 and toks[k].text == ")" and k in match:
            o = match[k]
            if o >= 1 and toks[o - 1].kind == "id": out.append(toks[o - 1].text)
            j = k - 1
            while j > o and toks[j].kind != "id": j -= 1
            if j > o: out.append(toks[j].text)
        elif k >= 0 and toks[k].kind == "id": out.append(toks[k].text)
    k = i + 1
    if k < len(toks) and toks[k].kind == "id" and toks[k].text in ("f", "F"): k += 1
    if k < len(toks) and toks[k].kind == "op" and toks[k].text in ("+", "-", "*", "/", "<", ">", "<=", ">=", "&", "|", "<<", ">>", "%"):
        k += 1
        while k < len(toks) and toks[k].text in ("(", "-", "!"): k += 1
        j = k
        while j + 2 < len(toks) and toks[j].kind == "id" and toks[j + 1].text in (".", "->", "::") and toks[j + 2].kind == "id": j += 2
        if j < len(toks) and toks[j].kind == "id": out.append(toks[j].text)
    return out


# C library, STL, image/compression and GL/GLFW calls: their numbers are API arguments (a base, a channel count, a
# buffer size, a window hint), never a screen quantity.
API_CALLS = {"strtol", "strtoul", "strtoull", "strtod", "strtof", "atoi", "atof", "strncmp", "strcmp", "strchr", "strcspn",
             "strncpy", "memcmp", "memcpy", "memset", "push_back", "emplace_back", "emplace", "insert", "reserve", "resize",
             "replace", "substr", "erase", "assign", "string", "setprecision", "setw", "wait_for", "sleep_for", "llround",
             "lround", "expf", "exp", "log10f", "log10", "atan", "atan2", "powf", "pow", "write", "read", "open",
             "mz_compress", "mz_uncompress", "glPixelStorei", "glfwWindowHint", "glfwSetWindowPos", "equal", "num",
             "stbi_write_png", "stbi_write_png_to_func", "stbi_load_from_memory", "stbi_load_gif_from_memory"}
API_PREFIX = re.compile(r"^(stbi_|glfw|mz_|gl[A-Z]|std_)")


UI_DIRS = re.compile(r"^src/app/(panels|bodies|ui|frame)/")


def logic_reason(toks, match, i, t, v, prev, nxt, paren, st, lhs, cat, rel=""):
    if cat.startswith("call:") and (cat[5:] in API_CALLS or API_PREFIX.match(cat[5:])):
        return "api_arg"
    names = _neigh(toks, match, i) + ([lhs] if lhs else [])
    bitop = {"<<", ">>", "&", "|", "^", "%", "~", "&=", "|="}
    if v is not None and float(v).is_integer() and prev is not None and prev.text in ("==", "!=") and cat != "timing":
        return "mode"   # an integer equality is a mode / enum / index test even inside a draw call
    if alpha_mask(t.text):
        return None   # sets the alpha byte: a design value
    unpack = (prev is not None and prev.text in (">>", "<<", ">>=", "<<=")) or (t.text.upper().rstrip("UL") in ("0XFF", "0XFF00", "0XFF0000", "0XFF000000", "0XFFFFFF", "0X00FFFFFF")) \
        or (v in (8, 16, 24, 32) and nxt is not None and nxt.text in (")",) and "SHIFT" in st)
    if unpack:
        return "bits"   # a channel shift / byte mask: unpacking a colour, not choosing one
    if design_sink(paren, st, names, toks, i):
        return None
    if t.text.lower().startswith("0x") or (prev is not None and prev.text in bitop) or (nxt is not None and nxt.text in bitop):
        return "bits"
    if v is not None and float(v).is_integer() and prev is not None and prev.text in ("==", "!=") and cat != "timing":
        return "mode"   # an integer equality is a mode / enum / index test even inside a draw call
    if any(p[0] in GEOM_CTX or CALL_FAMILY.get(p[0]) in ("draw", "layout", "window", "style_var") for p in paren):
        return None
    if any(GEOM_NAME.match(n) for n in names) or "tok ::" in st and not names:
        return None
    if v is not None and float(v).is_integer() and prev is not None and prev.text in ("==", "!=") and cat != "timing":
        return "mode"
    if cat == "timing" and v is not None and abs(v) == 1.0 and not (prev is not None and prev.text in ("<", ">", "<=", ">=")):
        return "0/1/-1"   # a full-scale 1 in an animation statement (clamp to 1, the reduce-motion value), not a 1 s hold
    if v is not None and 0 < abs(v) < 0.01:
        return "epsilon"
    if any(is_domain(n, cat == "timing") for n in names) or ("Param (" in st and cat != "timing"):
        return "domain"
    if v is not None and (abs(v) in UNIT_CONV or any(abs(abs(v) - p) < 1e-3 for p in PI)):
        return "unit_conv"
    # no screen quantity beside it (no geometry call, no position/size name, no tok:: value): an offset into a byte
    # buffer, a count, an argc, a threshold on a model value. A design number always sits next to one.
    if cat not in ("const_def", "member_default") or not UI_DIRS.match(rel):
        return "no_geometry"
    return None


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
    per, canvas = layout_calls()
    tot = collections.Counter()
    for d in per.values(): tot.update(d)
    L += [f"\n## Raw layout calls outside the kit (layout_call ratchet)\n",
          f"{sum(tot.values())} calls in {len(per)} files (" + ", ".join(f"{k} {v}" for k, v in tot.most_common()) +
          f"); canvas-tagged {canvas}. Target 0: panels lay out through UiScope.h (Block B).\n",
          "| file | calls |", "|---|---|"]
    for f, d in sorted(per.items(), key=lambda kv: -sum(kv[1].values()))[:25]:
        L.append(f"| {f} | {sum(d.values())} |")
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
    oks = ok_count()   # every `// ui-audit-ok:` tag in the scanned files, kit included: may only go down (owner 2026-11-24)
    raw = json.loads(STORE.read_text()) if STORE.exists() else {}
    old = raw.get("audit")
    old_ok = raw.get("audit_ok")
    if mode == "reset" or (mode == "update" and old is None):
        raw["audit"] = cur
    elif mode == "update":
        raw["audit"] = {k: min(v, old.get(k, v)) for k, v in cur.items()}
    if mode == "reset" or old_ok is None: raw["audit_ok"] = oks
    elif mode == "update": raw["audit_ok"] = min(oks, old_ok)
    seeding = "layout_call" not in raw   # a new ratchet seeds from the current tree on its first run
    lbad = layout_ratchet(raw, mode)
    if mode in ("reset", "update") or seeding:
        STORE.write_text(json.dumps(raw, indent=1) + "\n")
        if mode in ("reset", "update"): return 0
    if old is None:
        print("ui_audit: no 'audit' baseline yet: run ui_audit.py --ratchet --update"); return 0
    bad = [(k, v, old.get(k, 0)) for k, v in cur.items() if v > old.get(k, 0)]
    print(f"ui_audit: {sum(cur.values())} findings outside the kit (stored {sum(old.values())}), {len(cur)} categories; "
          f"ui-audit-ok tags {oks} (stored {old_ok})")
    for k, v, a in bad: print(f"RATCHET: ui_audit category {k} has {v} (allowed {a}): use a token or a kit part")
    for b in lbad: print(b)
    if old_ok is not None and oks > old_ok:
        print(f"RATCHET: {oks} ui-audit-ok tags (allowed {old_ok}): a new literal gets a token or a kit part, not a tag")
        return 1
    return 1 if (bad or lbad) else 0


# ---------------------------------------------------------------- layout calls (ui-formalisation Block A)
# Hand layout outside the kit: the panel places its own cursor instead of a layout scope (Block B) doing it.
# SetCursorScreenPos is not counted: custom canvas drawing places itself (node canvas, timeline, viewports).
# A call that is canvas placement on purpose carries `// ui-layout: canvas` (counted apart, may only go down).
LAYOUT_CALLS = ("SameLine", "SetCursorPos", "SetCursorPosX", "SetCursorPosY", "Dummy", "GetContentRegionAvail")
LAYOUT_TAG = re.compile(r"//\s*ui-layout:\s*canvas\b")
LAYOUT_RX = re.compile(r"\bImGui::(" + "|".join(LAYOUT_CALLS) + r")\s*\(")


def layout_calls():
    """{file: {call: n}} for raw layout calls outside the kit, plus {'canvas': n} tagged ones."""
    per, canvas = {}, 0
    for rel, src in ui_files():
        if rel.startswith(KIT): continue
        code = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
        for line in code.split("\n"):
            hits = LAYOUT_RX.findall(line.split("//")[0])
            if not hits: continue
            if LAYOUT_TAG.search(line): canvas += len(hits); continue
            d = per.setdefault(rel, {})
            for h in hits: d[h] = d.get(h, 0) + 1
    return per, canvas


def layout_ratchet(raw, mode):
    per, canvas = layout_calls()
    cur = {f: sum(d.values()) for f, d in sorted(per.items())}
    old = raw.get("layout_call")
    if mode == "reset" or old is None:
        raw["layout_call"] = cur; raw["layout_canvas"] = canvas; return []
    if mode == "update":
        raw["layout_call"] = {f: min(v, old.get(f, v)) for f, v in cur.items()}
        raw["layout_canvas"] = min(canvas, raw.get("layout_canvas", canvas)); return []
    bad = [f"RATCHET: {f} has {v} raw layout calls (allowed {old.get(f, 0)}): use a layout scope (UiScope.h)"
           for f, v in cur.items() if v > old.get(f, 0)]
    if canvas > raw.get("layout_canvas", 0):
        bad.append(f"RATCHET: {canvas} `ui-layout: canvas` tags (allowed {raw.get('layout_canvas', 0)})")
    print(f"ui_audit: {sum(cur.values())} raw layout calls outside the kit (stored {sum(old.values())}) in {len(cur)} files; "
          f"canvas-tagged {canvas}")
    return bad


def ok_count():
    return sum(1 for _, src in ui_files() for l in src.split("\n") if OK_TAG.search(l))

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
    if "--cat" in a:   # --cat CAT [CAT...]: every finding in these categories outside the kit, by file and line
        cats = set(x for x in a[a.index("--cat") + 1:] if not x.startswith("--"))
        findings, _, _ = run()
        for f in sorted(findings, key=lambda x: (x[4], x[5])):
            if f[0] in cats and not f[4].startswith(KIT):
                print(f"{f[4]}:{f[5]} [{f[0]}] {f[2]!r} in {f[1]} ({f[6]}) | {f[3]}")
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
