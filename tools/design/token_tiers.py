#!/usr/bin/env python3
"""Token tiers (ui-formalisation.md Block A): primitives, then semantic tokens that alias them or say why not.

Tiers
  primitive   tokens.json base ladders: space, radius, stroke_scale, opacity (and type, icon, motion as they are).
              Value-named on purpose (space.3_5 = 14): nothing outside the kit should need them by name.
  semantic    tokens.json base semantic groups (alpha, stroke, radius.cell ...) and every surfaces.json token.
              Each one either ALIASES a primitive ("{space.3_5}") or sits in a WHY bucket that says why it has a
              raw value of its own:
                own_size    a thing's own width / height / size (owner rule A1: a semantic token in surfaces.json)
                ratio       a proportion of another token-sized thing (unitless)
                limit       a logic cap, floor, count, frame count or resolution, not a look
                maths       a maths constant (pi, degrees, epsilon)
                off_scale   a gap, radius, stroke or opacity that is NOT on its ladder: design debt, ratcheted down
                own_colour  a surface colour no role holds (Rgba8 / Rgbaf / hex / pair)
  colour roles (tokens.json roles.col / pairs / fixed) are the colour primitives: one named dark + light pair each.

Shape in surfaces.json:  "<surface>": { "top": "{space.3_5}", "own_size": {"glyph_box": 14.0}, "ratio": {...} }
A raw number directly under a surface (not in a why bucket) fails build_tokens.py.
The emitted C++ name never changes: tok::<surface>_<key>, whatever bucket the key sits in.

  token_tiers.py            report: aliases, buckets, off-scale list
  token_tiers.py --apply    rewrite surfaces.json / tokens.json into tiers (idempotent; new flat raw values get placed)
  token_tiers.py --lint     exit 1 when a raw value has no alias and no bucket (build_tokens.py runs this too)
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOK = ROOT / "src/app/ui/design/tokens.json"
SURF = ROOT / "src/app/ui/design/surfaces.json"

WHY = ("own_size", "ratio", "limit", "maths", "off_scale", "own_colour")
NUM_GROUPS = ("surface", "surface_i", "surface_d")
COL_GROUPS = ("surface_hex", "surface_rgba8", "surface_rgbaf", "surface_pair8", "surface_pairf")
ALIAS = re.compile(r"^\{([a-z_0-9]+)\.([a-z_0-9]+)\}$")

# The primitive ladders. space already exists in tokens.json; the others are added by --apply.
LADDERS = {
    "radius": [1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 12],
    "stroke_scale": [0.5, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4],
    "opacity": [0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.1, 0.12, 0.14, 0.15, 0.16, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5,
                0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1],
}
PRIMITIVE_GROUPS = ("space", "radius", "stroke_scale", "opacity")


def step_key(v, scale=1.0):
    v = round(v * scale, 6)
    s = ("%g" % v).replace(".", "_")
    return s


def ladder_keys(group, values):
    scale = 100.0 if group == "opacity" else 1.0
    return {step_key(v, scale): float(v) for v in values}


def primitives(tok):
    """{group: {key: value}} for the primitive ladders (numeric keys only)."""
    b = tok["base"]
    out = {}
    for g in PRIMITIVE_GROUPS:
        src = b.get(g, {})
        out[g] = {k: float(v) for k, v in src.items() if not k.startswith("_") and isinstance(v, (int, float))
                  and re.fullmatch(r"[0-9_]+", k)}
    return out


# ---------------------------------------------------------------- what kind of value is this?
LAST_SKIP = {"sm", "lg", "md", "xs", "xl", "xxs", "xxl", "lo", "hi", "u8", "n", "alt", "2", "3", "4"}


def parts(key):
    p = key.split("_")
    while len(p) > 1 and p[-1] in LAST_SKIP:
        p.pop()
    return p


def kind(key, v):
    """space / radius / stroke / opacity (design kinds, must sit on a ladder) or a why word."""
    p = parts(key)
    last = p[-1]
    words = set(p)
    if abs(v) in (3.14159265, 3.141592653589793, 1.5707963, 0.017453292, 0.0174532925, 57.2957795) or (0 < abs(v) <= 1e-3):
        return "maths"
    if last in ("ratio", "scale", "mul", "frac", "t", "phase", "u", "em", "k", "factor", "fade", "amt", "amount", "mix"):
        return "ratio"
    if last in ("frames", "count", "rows", "cols", "steps", "limit", "res", "max", "min", "cap", "floor", "threshold",
                "lines", "chars", "items", "decimals", "digits", "segments", "octaves", "taps", "ms", "s", "sec", "hz",
                "rate", "period", "hold", "delay", "speed", "deg", "angle"):
        return "limit"
    if last in ("alpha", "opacity", "a") and 0 < v <= 1:
        return "opacity"
    if last in ("stroke", "hairline", "thick", "thickness", "weight", "line_w") and 0 < v <= 6:
        return "stroke"
    if last in ("radius", "rounding", "round", "corner") or "rounding" in words:
        return "radius" if 0 < v <= 16 else "own_size"
    if last in ("gap", "pad", "inset", "spacing", "gutter", "margin", "indent", "padding") and 1 <= v <= 24:
        return "space"
    if 0 < abs(v) < 1:
        return "ratio"
    return "own_size"


DESIGN_TO_LADDER = {"space": "space", "radius": "radius", "stroke": "stroke_scale", "opacity": "opacity"}


def place(key, v, prim):
    """('alias', '{g.k}') or (why, value)."""
    k = kind(key, float(v))
    if k in DESIGN_TO_LADDER:
        g = DESIGN_TO_LADDER[k]
        for pk, pv in prim.get(g, {}).items():
            if pv == float(v):
                return "alias", "{%s.%s}" % (g, pk)
        return "off_scale", v
    return k, v


def is_bucket(k, v):
    return k in WHY and isinstance(v, dict)


def flat(entries):
    """{key: value-or-alias} for one surface, buckets flattened."""
    out = {}
    for k, v in entries.items():
        if k.startswith("_"):
            continue
        if is_bucket(k, v):
            for kk, vv in v.items():
                if not kk.startswith("_"):
                    out[kk] = vv
        else:
            out[k] = v
    return out


def flatten_surfaces(sj):
    """surfaces.json with buckets flattened: what the older codemods (size/colour migrate, token_names) expect."""
    out = {}
    for g, surfs in sj.items():
        if g.startswith("_") or not isinstance(surfs, dict):
            out[g] = surfs; continue
        out[g] = {s: flat(kv) for s, kv in surfs.items()}
    return out


def resolve(value, tok):
    """An alias string -> its number; anything else unchanged."""
    if isinstance(value, str):
        m = ALIAS.match(value)
        if m:
            return float(tok["base"][m.group(1)][m.group(2)])
    return value


def tier_surface(entries, prim, colour):
    """Rewrite one surface's entries into aliases + buckets (keeps existing aliases and buckets)."""
    out, buckets = {}, {}
    for k, v in entries.items():
        if k.startswith("_"):
            out[k] = v; continue
        if is_bucket(k, v):
            for kk, vv in v.items():
                if k == "off_scale":        # re-placed every run: a ladder step added later picks it up
                    where, val = place(kk, vv, prim)
                    if where == "alias":
                        out[kk] = val; continue
                buckets.setdefault(k, {})[kk] = vv
            continue
        if isinstance(v, str) and ALIAS.match(v):
            out[k] = v; continue
        if colour:
            buckets.setdefault("own_colour", {})[k] = v; continue
        if isinstance(v, str):          # hex words in a numeric group (never expected)
            buckets.setdefault("own_colour", {})[k] = v; continue
        where, val = place(k, v, prim)
        if where == "alias":
            out[k] = val
        else:
            buckets.setdefault(where, {})[k] = val
    for w in WHY:
        if w in buckets:
            out[w] = dict(sorted(buckets[w].items()))
    return out


def lint(tok, sj):
    bad = []
    prim = primitives(tok)
    for g in NUM_GROUPS + COL_GROUPS:
        for s, kv in sj.get(g, {}).items():
            for k, v in kv.items():
                if k.startswith("_") or is_bucket(k, v):
                    continue
                if isinstance(v, str) and ALIAS.match(v):
                    m = ALIAS.match(v)
                    if m.group(1) not in prim or m.group(2) not in prim[m.group(1)]:
                        bad.append(f"{g}.{s}.{k}: alias {v} is not a primitive")
                    continue
                bad.append(f"{g}.{s}.{k} = {v}: raw value with no alias and no why bucket")
    for g in ("alpha", "stroke"):
        for k, v in tok["base"].get(g, {}).items():
            if k.startswith("_") or is_bucket(k, v):
                continue
            if isinstance(v, str) and ALIAS.match(v):
                continue
            if isinstance(v, dict) and "value" in v:
                continue
            bad.append(f"base.{g}.{k} = {v}: raw value with no alias and no why bucket")
    return bad


def count(sj, tok):
    c = {"alias": 0}
    for g in NUM_GROUPS + COL_GROUPS:
        for s, kv in sj.get(g, {}).items():
            for k, v in kv.items():
                if k.startswith("_"):
                    continue
                if is_bucket(k, v):
                    c[k] = c.get(k, 0) + len([x for x in v if not x.startswith("_")])
                elif isinstance(v, str) and ALIAS.match(v):
                    c["alias"] += 1
                else:
                    c["raw"] = c.get("raw", 0) + 1
    return c


def dumps(obj):
    s = json.dumps(obj, indent=1)
    return re.sub(r"\[\s*([-0-9.eE]+),\s*([-0-9.eE]+),\s*([-0-9.eE]+),\s*([-0-9.eE]+)\s*\]", r"[\1, \2, \3, \4]", s) + "\n"


def apply(tok, sj):
    b = tok["base"]
    # 1. primitive ladders (space exists; radius gains numeric steps beside its semantic 'cell')
    for g, vals in LADDERS.items():
        grp = b.setdefault(g, {})
        for k, v in ladder_keys(g, vals).items():
            grp.setdefault(k, v)
    grp_doc = {"radius": "Primitive radius ladder (numeric keys) + semantic radii that alias it",
               "stroke_scale": "Primitive stroke-weight ladder; semantic weights live in 'stroke' and alias it",
               "opacity": "Primitive opacity ladder in percent (opacity.12 = 0.12); semantic alphas live in 'alpha' and alias it"}
    for g, d in grp_doc.items():
        b[g].setdefault("_doc", d)
        b[g] = {"_doc": b[g]["_doc"], **{k: v for k, v in b[g].items() if k != "_doc"}}
    prim = primitives(tok)
    # 2. tokens.json semantic groups alias the ladders where they can
    for g, ladder in (("alpha", "opacity"), ("stroke", "stroke_scale"), ("radius", "radius")):
        for k, v in list(b[g].items()):
            if k.startswith("_") or not isinstance(v, (int, float)) or (g == "radius" and re.fullmatch(r"[0-9_]+", k)):
                continue
            hit = next((pk for pk, pv in prim[ladder].items() if pv == float(v)), None)
            if hit is not None:
                b[g][k] = "{%s.%s}" % (ladder, hit)
            elif g in ("alpha", "stroke"):
                b[g].setdefault("off_scale", {})[k] = v
                del b[g][k]
        for k, v in list(b[g].get("off_scale", {}).items()):
            hit = next((pk for pk, pv in prim[ladder].items() if pv == float(v)), None)
            if hit is not None:
                b[g][k] = "{%s.%s}" % (ladder, hit); del b[g]["off_scale"][k]
        if g in b and b[g].get("off_scale") == {}:
            del b[g]["off_scale"]
    # 3. surfaces
    for g in NUM_GROUPS + COL_GROUPS:
        for s, kv in sj.get(g, {}).items():
            sj[g][s] = tier_surface(kv, prim, g in COL_GROUPS)
    return tok, sj


def report(tok, sj):
    c = count(sj, tok)
    print("surface tokens by tier:", ", ".join(f"{k} {v}" for k, v in sorted(c.items(), key=lambda kv: -kv[1])))
    off = []
    for g in NUM_GROUPS:
        for s, kv in sj.get(g, {}).items():
            for k, v in kv.get("off_scale", {}).items():
                off.append((kind(k, float(v)), f"{s}_{k}", v))
    for g in ("alpha", "stroke"):
        for k, v in tok["base"][g].get("off_scale", {}).items():
            off.append((g, f"{g}_{k}", v))
    by = {}
    for kd, n, v in off:
        by.setdefault(kd, []).append((n, v))
    for kd, rows in sorted(by.items()):
        vals = {}
        for n, v in rows:
            vals[v] = vals.get(v, 0) + 1
        print(f"off_scale {kd}: {len(rows)}  values " + ", ".join(f"{v} x{n}" for v, n in sorted(vals.items())))
    return c


def main():
    tok = json.loads(TOK.read_text())
    sj = json.loads(SURF.read_text())
    if "--apply" in sys.argv:
        tok, sj = apply(tok, sj)
        TOK.write_text(json.dumps(tok, indent=1, ensure_ascii=False) + "\n")
        SURF.write_text(dumps(sj))
        print("wrote tokens.json, surfaces.json")
    if "--lint" in sys.argv:
        bad = lint(tok, sj)
        for b in bad[:40]:
            print(b)
        if bad:
            print(f"{len(bad)} token(s) with a raw value and no alias or why: run tools/design/token_tiers.py --apply")
            return 1
        print("token tiers: every semantic token aliases a primitive or says why")
        return 0
    report(tok, sj)
    return 0


if __name__ == "__main__":
    sys.exit(main())
