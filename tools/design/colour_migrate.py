#!/usr/bin/env python3
"""Move every colour literal ui_audit.py lists (`colour`, hex `draw`, `palette`, the colour call:* args) onto tokens
(owner rule B, 2026-11-24): a literal reuses an existing role or alpha token only when the value is identical AND the
job is the same; otherwise it gets a token of its own, named for its job on its surface, in surfaces.json.

  IM_COL32(40, 45, 55, 255)                      -> tok::U32(tok::<surface>_<name>)            (Rgba8)
  IM_COL32(40, 45, 55, alpha)                    -> tok::U32(tok::<surface>_<name>, alpha)     (Rgba8, a = 255)
  ImVec4(0.2f, 0.3f, 0.4f, 1.0f)                 -> tok::V4(tok::<surface>_<name>)             (Rgbaf, exact floats)
  isLight ? IM_COL32(..) : IM_COL32(..)          -> tok::U32(tok::<surface>_<name>, isLight)   (Pair8: dark, light)
  isLight ? ImVec4(..) : ImVec4(..)              -> tok::V4(tok::<surface>_<name>, isLight)    (Pairf)
  ImColor(r, g, b, a)                            -> ImColor(tok::U32(..)) / ImColor(tok::V4(..))
  any other number in a colour call (an alpha, a shade factor, a hex rgb) -> tok::<surface>_<name>_<job> in the
  literal's own C++ type (hex: unsigned)

Every value stays exact. A literal nothing here can place is printed and left.

    python3 tools/design/colour_migrate.py [--dry] [--file=src/...]
"""
import json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_audit  # noqa: E402
import size_migrate as sm  # noqa: E402

ROOT = ui_audit.ROOT
CTORS = {"IM_COL32", "ImVec4", "ImColor"}
BIT_OPS = {"&", "|", "^", "<<", ">>", "&=", "|=", ">>=", "<<="}
THEMED_ID = re.compile(r"^(is)?[Ll]ight(Theme)?$|^lt$|^light\w*$|^isLight\w*$|^themeLight$")
# (call, arg index) -> job of a lone number there
ARG_JOB = {("IM_COL32", 0): "r", ("IM_COL32", 1): "g", ("IM_COL32", 2): "b", ("IM_COL32", 3): "alpha",
           ("ImVec4", 0): "r", ("ImVec4", 1): "g", ("ImVec4", 2): "b", ("ImVec4", 3): "alpha",
           ("ImColor", 0): "r", ("ImColor", 1): "g", ("ImColor", 2): "b", ("ImColor", 3): "alpha",
           ("TextTint", 0): "alpha", ("Hex", 1): "alpha", ("WithAlpha", 1): "alpha", ("withAlpha", 1): "alpha",
           ("ScaleAlpha", 1): "fade", ("GetColorU32", 1): "alpha", ("Base", 1): "alpha", ("Role", 1): "alpha",
           ("RoleV4", 1): "alpha", ("BaseV4", 1): "alpha", ("U32", 1): "alpha", ("U32", 2): "alpha",
           ("Ink", 0): "alpha", ("Lo", 0): "alpha", ("Hi", 0): "alpha", ("Tint", 0): "alpha", ("Tint", 1): "shade",
           ("Tint", 2): "alpha", ("TextCol", 0): "alpha", ("wellCol", 0): "alpha", ("vec", 1): "alpha",
           ("AccentU32", 0): "alpha", ("ColorConvertHSVtoRGB", 0): "hue", ("ColorConvertHSVtoRGB", 1): "sat",
           ("ColorConvertHSVtoRGB", 2): "val", ("DotColor", 1): "alpha", ("BrandColor", 1): "alpha",
           ("cardCol", 0): "alpha", ("tone", 0): "alpha", ("SliceColour", 1): "alpha", ("AxisColor", 1): "alpha"}


def compact_json(obj):
    s = json.dumps(obj, indent=1)
    # a colour value on one line: [r, g, b, a]
    return re.sub(r"\[\s*([-0-9.eE]+),\s*([-0-9.eE]+),\s*([-0-9.eE]+),\s*([-0-9.eE]+)\s*\]",
                  r"[\1, \2, \3, \4]", s)


def main():
    dry = "--dry" in sys.argv
    only_file = None
    for a in sys.argv[1:]:
        if a.startswith("--file="): only_file = a.split("=", 1)[1]
    findings, _, _ = ui_audit.run()
    by_file = {}
    for f in findings:
        cat = f[0]
        is_col = cat in ("colour", "palette") or (cat == "draw" and sm.num_kind(f[2]) == "hex") or \
            (cat.startswith("call:") and (cat[5:] in sm.COLOUR_CALLS or re.search(r"(?i)col|tint|tone|accent|ink|rgb|^V4$|^U32$|^Hex$", cat[5:])))
        if not is_col or f[4].startswith(ui_audit.KIT): continue
        if only_file is not None and f[4] != only_file: continue
        by_file.setdefault(f[4], []).append(f)

    sj = json.loads(sm.SURF.read_text())
    kinds = {"float": "surface", "int": "surface_i", "double": "surface_d", "hex": "surface_hex",
             "rgba8": "surface_rgba8", "rgbaf": "surface_rgbaf", "pair8": "surface_pair8", "pairf": "surface_pairf"}
    groups = {k: sj.setdefault(g, {}) for k, g in kinds.items()}
    existing = set(re.findall(r"constexpr \w+ (\w+) =", sm.GEN.read_text()))
    taken = {}
    for kind, g in groups.items():
        for s, kv in g.items():
            for k, v in kv.items(): taken[f"{s}_{k}"] = (kind, json.dumps(v))

    def token_for(surf, name, kind, value):
        name = re.sub(r"_+", "_", name).strip("_") or "col"
        n = 1
        while True:
            nm = name if n == 1 else f"{name}_{n}"
            ident = f"{surf}_{nm}"
            key = (kind, json.dumps(value))
            if ident in taken:
                if taken[ident] == key: return ident
            elif ident not in existing:
                groups[kind].setdefault(surf, {})[nm] = value
                taken[ident] = key
                return ident
            n += 1

    base = json.loads((ROOT / "src/app/ui/design/tokens.json").read_text())["base"]
    shared = [("alpha", k, v, "float") for k, v in base["alpha"].items() if not k.startswith("_")] + \
             [("alpha8", k, v, "int") for k, v in base["alpha8"].items() if not k.startswith("_")]

    def reuse(name, kind, value):
        # rule B: an existing alpha token, only when the value is identical and its job is named here
        segs = set(name.split("_"))
        for grp, k, v, ty in shared:
            if ty == kind and v == value and set(k.split("_")) <= segs:
                return f"{grp}_{k}"
        return None

    done = skipped = deferred = 0
    stats = {}
    for rel, fs in sorted(by_file.items()):
        path = ROOT / rel
        src = path.read_text(encoding="utf-8")
        ctx = sm.Ctx(src)
        T = ctx.toks
        surf = sm.surface_of(rel)
        want = {}   # line -> list of literal texts still to place
        for f in fs:
            want.setdefault(f[5], []).append(f[2].lstrip("-").strip())
        lines_with = set(want)
        tag = set()
        consumed = set()   # num token indices placed by a whole-colour edit
        edits = []

        def close_of(k):   # index of the ')' matching T[k] == '('
            d = 0
            for j in range(k, len(T)):
                if T[j].text == "(": d += 1
                elif T[j].text == ")":
                    d -= 1
                    if d == 0: return j
            return None

        def args_of(k, e):  # [(a, b)] token spans, exclusive b
            out, d, a = [], 0, k + 1
            for j in range(k + 1, e):
                t = T[j].text
                if t in "([{": d += 1
                elif t in ")]}": d -= 1
                elif t == "," and d == 0: out.append((a, j)); a = j + 1
            out.append((a, e))
            return out

        def name_of(i, func):
            words, lhs = sm.decl_type(T, i)
            if lhs:
                return sm.strip_surf(sm.snake(lhs), surf)
            st = ctx.info.get(i, [])
            outer = ""
            for fr in reversed(st[:-1] if st else []):
                if fr[0] and fr[0] not in CTORS: outer = fr[0]; break
            fp = sm.func_part(func, surf)
            return "_".join(p for p in (fp, sm.snake(outer) if outer else "", "col") if p)

        def channel_conv(i):   # `x * 255`: ui_audit allows it (unit_conv), so it is never the finding
            return ui_audit.num_value(T[i].text) == 255.0 and (T[i - 1].text in ("*", "/") or T[i + 1].text in ("*", "/"))

        def subject(fr):
            # the colour an alpha / shade applies to: Base(tok::col::ink_black, 12) -> ink_black
            if fr[2] < 1 or fr[3] < 0 or fr[0] in CTORS: return ""
            ids, j = [], fr[3] + 1
            while j < len(T) and T[j].text != ",":
                if T[j].kind == "id": ids.append(T[j].text)
                elif T[j].text not in ("::", "."): return ""
                j += 1
            if not ids or len(ids) > 4: return ""
            if ids[0] != "tok" and not re.search(r"(?i)(col|ink|accent|tint|fill|bg|edge|text|line)", ids[-1]): return ""
            last = sm.strip_surf(sm.snake(ids[-1]), surf)
            return last if last not in ("c", "col", "color", "colour") else ""

        # whole colours
        calls = {}   # ctor '(' index -> (kind, value, alpha_text|None, span (a, b))
        for k in range(1, len(T)):
            if T[k].text != "(" or T[k - 1].kind != "id" or T[k - 1].text not in CTORS: continue
            if T[k].line not in lines_with: continue
            e = close_of(k)
            if e is None: continue
            ctor = T[k - 1].text
            spans = args_of(k, e)
            if len(spans) not in (3, 4): continue
            # `30.0f / 255.0f` is a 0-255 channel written as a float: an Rgba8 read through tok::V4 (c.r / 255.0f)
            over = ctor == "ImVec4" and all(b - a == 3 and T[a].kind == "num" and T[a + 1].text == "/" and
                                            T[a + 2].kind == "num" and ui_audit.num_value(T[a + 2].text) == 255.0 and
                                            float(ui_audit.num_value(T[a].text)).is_integer() for a, b in spans[:3])
            if over:
                last = spans[3] if len(spans) == 4 else None
                if last is None or (last[1] - last[0] == 1 and T[last[0]].kind == "num" and ui_audit.num_value(T[last[0]].text) == 1.0) or \
                   (last[1] - last[0] == 3 and T[last[0]].kind == "num" and T[last[0] + 1].text == "/" and ui_audit.num_value(T[last[0] + 2].text) == 255.0):
                    v = [int(ui_audit.num_value(T[a].text)) for a, b in spans[:3]]
                    v.append(255 if last is None or last[1] - last[0] == 1 else int(ui_audit.num_value(T[last[0]].text)))
                    calls[k] = (ctor, "rgba8_f", v, None, (k - 1, e))
                    continue
            lit = [b - a == 1 and T[a].kind == "num" and sm.num_kind(T[a].text) != "hex" for a, b in spans]
            if not all(lit[:3]): continue
            alpha = None
            if len(spans) == 4 and not lit[3]:
                alpha = src[T[spans[3][0]].pos:T[spans[3][1] - 1].pos + len(T[spans[3][1] - 1].text)]
            vals = [ui_audit.num_value(T[a].text) for a, b in spans if b - a == 1 and T[a].kind == "num"]
            nks = {sm.num_kind(T[a].text) for a, b in spans if b - a == 1 and T[a].kind == "num"}
            if ctor == "IM_COL32" or (ctor == "ImColor" and nks == {"int"}):
                if any(v != int(v) for v in vals): continue
                v = [int(x) for x in vals] + ([255] if len(vals) == 3 else [])
                kind = "rgba8"
            else:
                v = [float(x) for x in vals] + ([1.0] if len(vals) == 3 else [])
                kind = "rgbaf"
            calls[k] = (ctor, kind, v, alpha, (k - 1, e))

        def text_of(a, b):  # tokens a..b inclusive
            return src[T[a].pos:T[b].pos + len(T[b].text)]

        claimed = set()
        spans = []   # source spans a whole-colour edit rewrites: a number left inside waits for the next run

        def consume(k, a, e):
            for (x, y) in args_of(k, e):
                if y - x == 1 and T[x].kind == "num": consumed.add(x)
        # theme pairs: cond ? C1 : C2
        for k, (ctor, kind, v, alpha, (a, e)) in sorted(calls.items()):
            if k in claimed: continue
            q = a - 1
            if q < 1 or T[q].text != "?": continue
            # condition: ident, or [ns::]IsThemeLight()
            neg = False
            if T[q - 1].kind == "id" and THEMED_ID.match(T[q - 1].text):
                c0 = q - 1
            elif T[q - 1].text == ")" and q >= 3 and T[q - 2].text == "(" and T[q - 3].text == "IsThemeLight":
                c0 = q - 3
                while c0 >= 2 and T[c0 - 1].text == "::" and T[c0 - 2].kind == "id": c0 -= 2
            else:
                continue
            if c0 >= 1 and T[c0 - 1].text == "!":
                neg = True; c0 -= 1
            if c0 >= 1 and T[c0 - 1].text not in ("(", ",", "=", "return", "?", ":", "{"): continue
            if e + 1 >= len(T) or T[e + 1].text != ":": continue
            k2 = e + 3
            if k2 not in calls or k2 in claimed or T[e + 2].text != ctor: continue
            ctor2, kind2, v2, alpha2, (a2, e2) = calls[k2]
            if kind2 != kind or alpha2 != alpha: continue
            if e2 + 1 < len(T) and T[e2 + 1].text not in (")", ",", ";", "}"): continue
            cond = text_of(c0 + (1 if neg else 0), q - 1)
            light, dark = (v2, v) if neg else (v, v2)
            pk = "pair8" if kind in ("rgba8", "rgba8_f") else "pairf"
            func = next((f[6] for f in fs if f[5] == T[k].line), "-")
            ident = token_for(surf, name_of(k, func), pk, {"dark": dark, "light": light})
            fn = "U32" if ctor == "IM_COL32" else "V4"
            rep = f"tok::{fn}(tok::{ident}, {cond}" + (f", {alpha}" if alpha else "") + ")"
            if ctor == "ImColor": rep = f"ImColor({rep})"
            edits.append((T[c0].pos, T[e2].pos + 1, rep))
            claimed.update((k, k2))
            for (sa, se) in ((a, e), (a2, e2)):
                consume(k if sa == a else k2, sa, se)
            spans.append((T[c0].pos, T[e2].pos + 1))
            stats["pair"] = stats.get("pair", 0) + 1
            done += 1
        for k, (ctor, kind, v, alpha, (a, e)) in sorted(calls.items()):
            if k in claimed: continue
            func = next((f[6] for f in fs if f[5] == T[k].line), "-")
            ident = token_for(surf, name_of(k, func), "rgba8" if kind == "rgba8_f" else kind, v)
            fn = "U32" if kind == "rgba8" and ctor != "ImVec4" else "V4"
            rep = f"tok::{fn}(tok::{ident}" + (f", {alpha}" if alpha else "") + ")"
            if ctor == "ImColor": rep = f"ImColor({rep})"
            edits.append((T[a].pos, T[e].pos + 1, rep))
            consume(k, a, e)
            spans.append((T[a].pos, T[e].pos + 1))
            stats["colour"] = stats.get("colour", 0) + 1
            done += 1

        # the remaining numbers, one by one
        by_line = {}
        for i, t in enumerate(T):
            if t.kind == "num" and t.line in lines_with: by_line.setdefault(t.line, []).append(i)
        used = set(consumed)
        for f in fs:
            cat, call, text, stmt, _, ln, func = f
            text = text.lstrip("-").strip()
            cand = [i for i in by_line.get(ln, []) if T[i].text == text and i not in used and not channel_conv(i)]
            if not cand:
                # already placed by a whole colour (or never on this line)
                if any(T[i].text == text for i in by_line.get(ln, []) if i in consumed): continue
                print(f"skip {rel}:{ln} {text} not found"); skipped += 1; continue
            i = cand[0]; used.add(i)
            if any(x <= T[i].pos < y for x, y in spans):
                deferred += 1; continue
            if T[i - 1].text in BIT_OPS or (i + 1 < len(T) and T[i + 1].text in BIT_OPS):
                tag.add(ln); continue
            nk = sm.num_kind(text)
            st = ctx.info.get(i, [])
            top = st[-1] if st else ["", "", 0, -1]
            prev = T[i - 1].text if i else ""
            nxt = T[i + 1].text if i + 1 < len(T) else ""
            if prev in ("*", "/") or nxt in ("*", "/"): job = "scale"
            elif prev in ("+", "-") or nxt in ("+", "-"): job = "offset"
            elif nk == "hex": job = "rgb"
            else:
                job = ARG_JOB.get((top[0], top[2]))
                if job is None and cat.startswith("call:"): job = ARG_JOB.get((cat[5:], top[2]), "alpha" if top[2] else "level")
                if job is None: job = "level"
            words, lhs = sm.decl_type(T, i)
            if lhs:
                name = sm.strip_surf(sm.snake(lhs), surf)
                if not name.endswith(job): name += "_" + job
            else:
                c = top[0] if top[0] else (cat[5:] if cat.startswith("call:") else "")
                subj = subject(top)
                if subj: c = subj
                fp = sm.func_part(func, surf)
                if fp and c.startswith(fp + "_"): c = c[len(fp) + 1:]
                name = "_".join(p for p in (sm.func_part(func, surf), sm.snake(c), job) if p)
            if nk == "hex":
                value = "0x" + text[2:].rstrip("uUlL").upper()
                kind = "hex"
            else:
                kind = nk
                value = sm.fmt_value(ui_audit.num_value(text), kind)
            ident = reuse(name, kind, value) if job == "alpha" else None
            if ident: stats["reused"] = stats.get("reused", 0) + 1
            ident = ident or token_for(surf, name, kind, value)
            edits.append((T[i].pos, T[i].pos + len(T[i].text), f"tok::{ident}"))
            stats[job] = stats.get(job, 0) + 1
            done += 1
        if not edits and not tag: continue
        for a, b, t in sorted(edits, reverse=True):
            src = src[:a] + t + src[b:]
        if tag:
            ls = src.split("\n")
            for ln in tag:
                if "ui-audit-ok" not in ls[ln - 1]:
                    ls[ln - 1] += "  // ui-audit-ok: channel mask / bit packing, not a colour value"
            src = "\n".join(ls)
        if "TokenColors.h" not in src and "AppShared.h" not in src:
            inc = '#include "app/ui/design/TokenColors.h"'
            m = re.search(r"^#include .*$", src, re.M) or re.search(r"^#pragma once.*$", src, re.M)
            src = (src[:m.end()] + "\n" + inc + src[m.end():]) if m else inc + "\n" + src
        if not dry:
            path.write_text(src, encoding="utf-8")
    if not dry:
        for kind, g in groups.items():
            sj[kinds[kind]] = {s: dict(sorted(kv.items())) for s, kv in sorted(g.items())}
        sm.SURF.write_text(compact_json(sj) + "\n")
    for k in sorted(stats): print(f"  {k:<10} {stats[k]}")
    print(f"colour_migrate: {done} placed, {skipped} skipped, {deferred} wait for the next run (inside a rewritten colour)")


if __name__ == "__main__":
    main()
