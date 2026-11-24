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
    "stroke_scale": [0.5, 1, 1.5, 2, 2.5, 3, 4],
    "opacity": [0.04, 0.06, 0.08, 0.1, 0.12, 0.16, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.85, 0.9, 0.95, 1],
}
PRIMITIVE_GROUPS = ("space", "radius", "stroke_scale", "opacity", "opacity8")
INT_PRIMITIVE_GROUPS = ("opacity8",)
INT_ALIAS_GROUPS = ("opacity8", "alpha8")
JOB_GROUPS = ("stroke", "radius", "gap", "alpha", "alpha8", "hit")

# Tier 2 shared job tokens: primitive -> canonical job alias (zero visual change; Step B).
JOB_TOKENS = {
    "stroke": {
        "_doc": "Line weights in pt (Tier 2 semantic jobs aliasing stroke_scale)",
        "fine": "{stroke_scale.0_5}",
        "rule": "{stroke_scale.1}",
        "medium": "{stroke_scale.1_5}",
        "emphasis": "{stroke_scale.2}",
        "bold": "{stroke_scale.2_5}",
        "heavy": "{stroke_scale.3}",
        "wide": "{stroke_scale.4}",
    },
    "radius": {
        "xs": "{radius.1}",
        "micro": "{radius.1_5}",
        "cell": "{radius.2}",
        "sm": "{radius.2_5}",
        "chip": "{radius.3}",
        "card": "{radius.4}",
        "pill": "{radius.5}",
        "control": "{radius.6}",
        "container": "{radius.8}",
        "dialog": "{radius.12}",
    },
    "gap": {
        "_doc": "Spacing jobs (Tier 2 semantic jobs aliasing space)",
        "px": "{space.0_25}",
        "hair": "{space.0_5}",
        "tight": "{space.0_75}",
        "item": "{space.1}",
        "gutter": "{space.1_25}",
        "group": "{space.1_5}",
        "panel": "{space.2}",
        "label": "{space.2_5}",
        "section": "{space.3}",
        "inset": "{space.3_5}",
        "block": "{space.4}",
        "page": "{space.5}",
    },
}

# Primitive alias -> Tier 2 job alias mapping (exact value match, semantic scale-tiered families only).
PRIM_TO_JOB = {}
for _jg, _jkv in JOB_TOKENS.items():
    for _jk, _jv in _jkv.items():
        if not _jk.startswith("_"):
            PRIM_TO_JOB[_jv] = "{%s.%s}" % (_jg, _jk)

SYSTEM_TO_JOB = {
    "row_gap": "{gap.item}",
    "section_gap": "{gap.section}",
    "pad_panel": "{gap.panel}",
    "switch_inset": "{gap.tight}",
    "empty_plate_pad": "{gap.item}",
    "radius_control": "{radius.control}",
    "radius_container": "{radius.container}",
    "radius_dialog": "{radius.dialog}",
}

# Step C: off-scale spacing snap table (100 tokens -> Tier-2 gap.* jobs; 18->16, 20->24 keeps 4/8pt rhythm pure).
SPACE_SNAP = {
    1.25: "{gap.hair}",    # 2
    1.4:  "{gap.hair}",    # 2
    1.5:  "{gap.hair}",    # 2
    2.5:  "{gap.tight}",   # 3
    3.5:  "{gap.item}",    # 4
    4.5:  "{gap.item}",    # 4
    5.5:  "{gap.group}",   # 6
    6.5:  "{gap.group}",   # 6
    7.0:  "{gap.panel}",   # 8
    9.0:  "{gap.panel}",   # 8
    11.0: "{gap.section}", # 12
    13.0: "{gap.section}", # 12
    15.0: "{gap.block}",   # 16
    18.0: "{gap.block}",   # 16
    20.0: "{gap.page}",    # 24
    22.0: "{gap.page}",    # 24
}

# Step D1: off-scale stroke & radius snap tables + pruned sub-pixel stroke aliases (1.25, 1.75).
STROKE_SNAP = {
    0.97: "{stroke.rule}",     # 1.0
    1.1:  "{stroke.rule}",     # 1.0
    1.2:  "{stroke.rule}",     # 1.0
    1.25: "{stroke.rule}",     # 1.0 (pruned sub-pixel stop)
    1.4:  "{stroke.medium}",   # 1.5
    1.6:  "{stroke.medium}",   # 1.5
    1.75: "{stroke.emphasis}", # 2.0 (pruned sub-pixel stop)
    1.8:  "{stroke.emphasis}", # 2.0
    2.2:  "{stroke.emphasis}", # 2.0
    3.5:  "{stroke.wide}",     # 4.0
    4.5:  "{stroke.wide}",     # 4.0
    5.0:  "{stroke.wide}",     # 4.0
    6.0:  "{stroke.wide}",     # 4.0
}
STROKE_ALIAS_PRUNE = {
    "{stroke_scale.1_25}": "{stroke.rule}",
    "{stroke_scale.1_75}": "{stroke.emphasis}",
}
RADIUS_SNAP = {
    0.5: "{radius.xs}",   # 1.0
    3.2: "{radius.chip}", # 3.0
    4.8: "{radius.pill}", # 5.0
}
HIT_RECT_RX = re.compile(r"(?:^|_)hit_(?:x|y|w|h)$")

# Step G: canonical fixed colour roles for deduplicating own_colour (surface_rgba8 / surface_rgbaf).
FIXED_COL_8 = {
    (255, 255, 255, 255): "{fixed.white}",
    (0, 0, 0, 255):       "{fixed.black}",
}
FIXED_COL_F = {
    (1.0, 1.0, 1.0, 1.0): "{fixed_f.white}",
    (0.0, 0.0, 0.0, 1.0): "{fixed_f.black}",
    (1.0, 0.4, 0.4, 1.0): "{fixed_f.error}",
}


def col_role_alias(group, v):
    if group == "surface_rgba8" and isinstance(v, list) and len(v) == 4:
        return FIXED_COL_8.get(tuple(int(x) for x in v))
    if group == "surface_rgbaf" and isinstance(v, list) and len(v) == 4:
        return FIXED_COL_F.get(tuple(round(float(x), 4) for x in v))
    return None


def step_key(v, scale=1.0):
    v = round(v * scale, 6)
    s = ("%g" % v).replace(".", "_")
    return s


def ladder_keys(group, values):
    if group == "opacity8":
        return {step_key(v, 100.0): int(round(v * 255)) for v in values}
    scale = 100.0 if group == "opacity" else 1.0
    return {step_key(v, scale): float(v) for v in values}


def primitives(tok):
    """{group: {key: value}} for the primitive ladders (numeric keys only)."""
    b = tok["base"]
    out = {}
    for g in PRIMITIVE_GROUPS:
        src = b.get(g, {})
        cast = int if g in INT_PRIMITIVE_GROUPS else float
        out[g] = {k: cast(v) for k, v in src.items() if not k.startswith("_") and isinstance(v, (int, float))
                  and re.fullmatch(r"[0-9_]+", k)}
    return out


# ---------------------------------------------------------------- what kind of value is this?
LAST_SKIP = {"sm", "lg", "md", "xs", "xl", "xxs", "xxl", "lo", "hi", "u8", "n", "alt", "2", "3", "4"}
SPACE_WORDS = {"gap", "pad", "inset", "spacing", "gutter", "margin", "indent", "padding", "spacer"}
DIR_SUFFIX = {"x", "y", "l", "r", "t", "b", "w", "h", "i"}


def parts(key):
    p = key.split("_")
    while len(p) > 1 and p[-1] in LAST_SKIP:
        p.pop()
    if len(p) > 1 and p[-1] in DIR_SUFFIX and p[-2] in SPACE_WORDS:
        p.pop()
    return p


def kind(key, v, group="surface"):
    """space / radius / stroke / opacity / opacity8 (design kinds, must sit on a ladder) or a why word."""
    p = parts(key)
    last = p[-1]
    words = set(p)
    if abs(v) in (3.14159265, 3.141592653589793, 1.5707963, 0.017453292, 0.0174532925, 57.2957795) or (0 < abs(v) <= 1e-3):
        return "maths"
    if last in ("ratio", "scale", "mul", "frac", "t", "phase", "u", "em", "k", "factor", "fade", "amt", "amount", "mix"):
        return "ratio"
    if last == "shade" and 0 < v <= 4:
        return "ratio"
    if last in ("frames", "count", "rows", "cols", "steps", "limit", "res", "max", "min", "cap", "floor", "threshold",
                "lines", "chars", "items", "decimals", "digits", "segments", "octaves", "taps", "ms", "s", "sec", "hz",
                "rate", "period", "hold", "delay", "speed", "deg", "angle"):
        return "limit"
    if last == "layout" and 1 <= v <= 12 and float(v).is_integer():
        return "limit"
    if last in ("alpha", "opacity", "a") or (group == "surface_i" and "alpha" in words):
        if group != "surface_i" and 0 < v <= 1:
            return "opacity"
        if group != "surface_i" and 1 < v <= 4:
            return "ratio"
        if 1 <= v <= 255 and float(v).is_integer():
            return "opacity8"
    if last in ("stroke", "hairline", "thick", "thickness", "weight", "line_w") and 0 < v <= 6:
        return "stroke"
    if last in ("radius", "rounding", "round", "corner") or "rounding" in words:
        return "radius" if 0 < v <= 16 else "own_size"
    if last in SPACE_WORDS and 1 <= v <= 24:
        return "space"
    if 0 < abs(v) < 1:
        return "ratio"
    return "own_size"


DESIGN_TO_LADDER = {"space": "space", "radius": "radius", "stroke": "stroke_scale", "opacity": "opacity", "opacity8": "opacity8"}


def place(key, v, prim, group="surface"):
    """('alias', '{g.k}') or (why, value)."""
    k = kind(key, float(v), group)
    if k in DESIGN_TO_LADDER:
        g = DESIGN_TO_LADDER[k]
        target = int(round(v)) if g in INT_PRIMITIVE_GROUPS else float(v)
        for pk, pv in prim.get(g, {}).items():
            if pv == target:
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


def jobs(tok):
    """{group: {key: value}} for Tier 2 shared job tokens in tokens.json."""
    b = tok["base"]
    out = {}
    for g in JOB_GROUPS:
        src = b.get(g, {})
        out[g] = {}
        for k, v in src.items():
            if k.startswith("_") or is_bucket(k, v):
                continue
            if g == "radius" and re.fullmatch(r"[0-9_]+", k):
                continue
            out[g][k] = v
    return out


def col_roles(tok):
    r = tok.get("roles", {})
    out = {"fixed": {}, "fixed_f": {}}
    for k, v in r.get("fixed", {}).items():
        if k.startswith("fixed."):
            out["fixed"][k[6:]] = v
    for k, v in r.get("fixed_f", {}).items():
        out["fixed_f"][k] = v
    return out


def resolve(value, tok):
    """Follow alias chain (Tier 3 -> Tier 2 -> Tier 1, or colour role) to its value; anything else unchanged."""
    seen = set()
    cur = value
    while isinstance(cur, str):
        m = ALIAS.match(cur)
        if not m:
            return cur
        key = (m.group(1), m.group(2))
        if key in seen:
            raise ValueError(f"alias cycle at {cur}")
        seen.add(key)
        if key[0] in ("fixed", "fixed_f"):
            cr = col_roles(tok)
            if key[1] in cr[key[0]]:
                return cr[key[0]][key[1]]
            raise KeyError(f"broken colour role alias {cur}")
        grp = tok["base"].get(key[0], {})
        if key[1] in grp:
            cur = grp[key[1]]
        elif "off_scale" in grp and key[1] in grp["off_scale"]:
            cur = grp["off_scale"][key[1]]
        else:
            raise KeyError(f"broken alias {cur}")
        if isinstance(cur, dict) and "value" in cur:
            cur = cur["value"]
    return float(cur) if isinstance(cur, (int, float)) else cur


def nearest_ladder_alias(val, ladder_dict, ladder_name):
    """Snap a numeric value to the nearest step in a primitive ladder."""
    best_k = min(ladder_dict, key=lambda k: (abs(float(ladder_dict[k]) - float(val)), float(ladder_dict[k])))
    return "{%s.%s}" % (ladder_name, best_k)


def tier_surface(entries, prim, colour, group="surface", refile=False, use_jobs=False, snap_space=False, snap_d1=False, snap_d2=False, snap_cols=False, old_prim=None, hit_min=20.0):
    """Rewrite one surface's entries into aliases + buckets (refile re-checks own_size; use_jobs lifts exact primitive aliases to Tier 2 jobs)."""
    out, buckets = {}, {}
    for k, v in entries.items():
        if k.startswith("_"):
            out[k] = v; continue
        if is_bucket(k, v):
            for kk, vv in v.items():
                if kk.startswith("_"):
                    buckets.setdefault(k, {})[kk] = vv; continue
                if snap_cols and k == "own_colour":
                    ca = col_role_alias(group, vv)
                    if ca:
                        out[kk] = ca; continue
                if snap_d1 and k == "own_size" and isinstance(vv, (int, float)) and HIT_RECT_RX.search(kk) and float(vv) < hit_min:
                    out[kk] = "{hit.target}"; continue
                if k == "off_scale" or (refile and k == "own_size" and isinstance(vv, (int, float))):
                    where, val = place(kk, vv, prim, group)
                    if where == "alias":
                        out[kk] = PRIM_TO_JOB.get(val, val) if use_jobs else val; continue
                    if snap_space and k == "off_scale" and isinstance(vv, (int, float)) and kind(kk, float(vv), group) == "space":
                        snapped = SPACE_SNAP.get(float(vv))
                        if snapped:
                            out[kk] = snapped; continue
                    if snap_d1 and k == "off_scale" and isinstance(vv, (int, float)):
                        kd = kind(kk, float(vv), group)
                        snapped = STROKE_SNAP.get(float(vv)) if kd == "stroke" else (RADIUS_SNAP.get(float(vv)) if kd == "radius" else None)
                        if snapped:
                            out[kk] = snapped; continue
                    if snap_d2 and k == "off_scale" and isinstance(vv, (int, float)):
                        kd = kind(kk, float(vv), group)
                        if kd in ("opacity", "opacity8"):
                            out[kk] = nearest_ladder_alias(vv, prim[kd], kd); continue
                    if refile and k == "own_size" and where != "own_size":
                        buckets.setdefault(where, {})[kk] = val; continue
                buckets.setdefault(k, {})[kk] = vv
            continue
        if isinstance(v, str) and ALIAS.match(v):
            if snap_d1 and v in STROKE_ALIAS_PRUNE:
                v = STROKE_ALIAS_PRUNE[v]
            if snap_d2 and old_prim is not None:
                m = ALIAS.match(v)
                ag, ak = m.group(1), m.group(2)
                if ag in ("opacity", "opacity8") and ak not in prim[ag] and ak in old_prim.get(ag, {}):
                    v = nearest_ladder_alias(old_prim[ag][ak], prim[ag], ag)
            out[k] = PRIM_TO_JOB.get(v, v) if use_jobs else v; continue
        if colour:
            buckets.setdefault("own_colour", {})[k] = v; continue
        if isinstance(v, str):          # hex words in a numeric group (never expected)
            buckets.setdefault("own_colour", {})[k] = v; continue
        where, val = place(k, v, prim, group)
        if where == "alias":
            out[k] = PRIM_TO_JOB.get(val, val) if use_jobs else val
        else:
            buckets.setdefault(where, {})[k] = val
    for w in WHY:
        if w in buckets:
            out[w] = dict(sorted(buckets[w].items()))
    return out


def lint(tok, sj):
    bad = []
    prim = primitives(tok)
    job = jobs(tok)
    cr = col_roles(tok)

    def check_alias(where, v):
        m = ALIAS.match(v)
        g, k = m.group(1), m.group(2)
        in_prim = g in prim and k in prim[g]
        in_job = g in job and k in job[g]
        in_col = g in cr and k in cr[g]
        if not (in_prim or in_job or in_col):
            bad.append(f"{where}: alias {v} is neither a primitive, job token, nor colour role")
            return
        try:
            resolve(v, tok)
        except (ValueError, KeyError) as e:
            bad.append(f"{where}: {e}")

    hit_min = float(tok["base"].get("hit_min", 20))
    for g in NUM_GROUPS + COL_GROUPS:
        for s, kv in sj.get(g, {}).items():
            for k, v in kv.items():
                if k.startswith("_"):
                    continue
                if is_bucket(k, v):
                    if k == "own_colour" and g in ("surface_rgba8", "surface_rgbaf"):
                        for kk, vv in v.items():
                            if kk.startswith("_"): continue
                            ca = col_role_alias(g, vv)
                            if ca:
                                bad.append(f"{g}.{s}.own_colour.{kk}: duplicate raw colour (use {ca})")
                    if k == "own_size" and g in NUM_GROUPS:
                        for kk, vv in v.items():
                            if kk.startswith("_") or not isinstance(vv, (int, float)):
                                continue
                            if HIT_RECT_RX.search(kk) and float(vv) < hit_min:
                                bad.append(f"{g}.{s}.own_size.{kk} = {vv}: hit target below hit_min ({hit_min})")
                            if (kk == "gap" or kk.endswith("_gap")) and float(vv) > 24:
                                bad.append(f"{g}.{s}.own_size.{kk} = {vv}: large size/count (>24) misnamed _gap")
                            where, _ = place(kk, vv, prim, g)
                            if where != "own_size":
                                bad.append(f"{g}.{s}.own_size.{kk} = {vv}: misfiled (belongs in {where})")
                    continue
                if isinstance(v, str) and ALIAS.match(v):
                    check_alias(f"{g}.{s}.{k}", v)
                    if HIT_RECT_RX.search(k):
                        try:
                            if float(resolve(v, tok)) < hit_min:
                                bad.append(f"{g}.{s}.{k}: hit target below hit_min ({hit_min})")
                        except (ValueError, KeyError):
                            pass
                    continue
                bad.append(f"{g}.{s}.{k} = {v}: raw value with no alias and no why bucket")
    for g in ("alpha", "alpha8", "stroke", "gap"):
        for k, v in tok["base"].get(g, {}).items():
            if k.startswith("_") or is_bucket(k, v):
                continue
            if isinstance(v, str) and ALIAS.match(v):
                check_alias(f"base.{g}.{k}", v)
                continue
            if isinstance(v, dict) and "value" in v:
                continue
            bad.append(f"base.{g}.{k} = {v}: raw value with no alias and no why bucket")
    return bad


def count(sj, tok):
    prim = primitives(tok)
    job = jobs(tok)
    c = {"alias": 0, "alias_job": 0, "alias_prim": 0}
    for g in NUM_GROUPS + COL_GROUPS:
        for s, kv in sj.get(g, {}).items():
            for k, v in kv.items():
                if k.startswith("_"):
                    continue
                if is_bucket(k, v):
                    c[k] = c.get(k, 0) + len([x for x in v if not x.startswith("_")])
                elif isinstance(v, str) and ALIAS.match(v):
                    c["alias"] += 1
                    m = ALIAS.match(v)
                    if m.group(1) in job and m.group(2) in job[m.group(1)]:
                        c["alias_job"] += 1
                    else:
                        c["alias_prim"] += 1
                else:
                    c["raw"] = c.get("raw", 0) + 1
    return c


def dumps(obj):
    s = json.dumps(obj, indent=1)
    return re.sub(r"\[\s*([-0-9.eE]+),\s*([-0-9.eE]+),\s*([-0-9.eE]+),\s*([-0-9.eE]+)\s*\]", r"[\1, \2, \3, \4]", s) + "\n"


def apply(tok, sj, refile=False, use_jobs=False, snap_space=False, snap_d1=False, snap_d2=False, snap_cols=False):
    b = tok["base"]
    if snap_cols:
        r = tok.setdefault("roles", {})
        r.setdefault("fixed", {}).setdefault("fixed.white", [255, 255, 255, 255])
        r.setdefault("fixed", {}).setdefault("fixed.black", [0, 0, 0, 255])
        r.setdefault("fixed_f", {}).setdefault("white", [1.0, 1.0, 1.0, 1.0])
        r.setdefault("fixed_f", {}).setdefault("black", [0.0, 0.0, 0.0, 1.0])
        r.setdefault("fixed_f", {}).setdefault("error", [1.0, 0.4, 0.4, 1.0])
    old_prim = primitives(tok)
    # 1. primitive ladders (space exists; radius gains numeric steps beside its semantic 'cell'; opacity8 mirrors opacity)
    all_ladders = {**LADDERS, "opacity8": LADDERS["opacity"]}
    for g, vals in all_ladders.items():
        if snap_d2 and g in ("opacity", "opacity8"):
            doc = b.get(g, {}).get("_doc")
            b[g] = ({**({"_doc": doc} if doc else {}), **ladder_keys(g, vals)})
            continue
        grp = b.setdefault(g, {})
        for k, v in ladder_keys(g, vals).items():
            grp.setdefault(k, v)
    if snap_d1:
        for obsolete in ("1_25", "1_75"):
            b.get("stroke_scale", {}).pop(obsolete, None)
        b.setdefault("hit", {}).setdefault("target", float(b.get("hit_min", 20)))
        for k, v in list(b.get("stroke", {}).get("off_scale", {}).items()):
            if float(v) in STROKE_SNAP:
                b["stroke"][k] = STROKE_SNAP[float(v)]
                del b["stroke"]["off_scale"][k]
        if b.get("stroke", {}).get("off_scale") == {}:
            del b["stroke"]["off_scale"]
    grp_doc = {"radius": "Primitive radius ladder (numeric keys) + semantic radii that alias it",
               "stroke_scale": "Primitive stroke-weight ladder; semantic weights live in 'stroke' and alias it",
               "opacity": "Primitive opacity ladder in percent (opacity.12 = 0.12); semantic alphas live in 'alpha' and alias it",
               "opacity8": "Primitive 8-bit opacity ladder (round(opacity * 255)) for IM_COL32; semantic 8-bit alphas live in 'alpha8'"}
    for g, d in grp_doc.items():
        b[g].setdefault("_doc", d)
        b[g] = {"_doc": b[g]["_doc"], **{k: v for k, v in b[g].items() if k != "_doc"}}
    prim = primitives(tok)
    # 2. Tier 2 shared job tokens (Step B): seed canonical stroke, radius, gap jobs and wire system.* to them
    if use_jobs:
        for g, kv in JOB_TOKENS.items():
            grp = b.setdefault(g, {})
            for k, v in kv.items():
                if k == "_doc":
                    grp.setdefault("_doc", v)
                else:
                    grp.setdefault(k, v)
        sys_grp = b.get("system", {})
        for k, alias in SYSTEM_TO_JOB.items():
            if k in sys_grp and isinstance(sys_grp[k], (int, float)):
                if float(sys_grp[k]) == float(resolve(alias, tok)):
                    sys_grp[k] = alias
    # 3. tokens.json semantic groups alias the ladders where they can
    for g, ladder in (("alpha", "opacity"), ("alpha8", "opacity8"), ("stroke", "stroke_scale"), ("radius", "radius")):
        if g not in b:
            continue
        cast = int if ladder in INT_PRIMITIVE_GROUPS else float
        for k, v in list(b[g].items()):
            if k.startswith("_") or (g == "radius" and re.fullmatch(r"[0-9_]+", k)):
                continue
            if snap_d2 and ladder in ("opacity", "opacity8") and isinstance(v, str) and ALIAS.match(v):
                m = ALIAS.match(v)
                if m.group(1) == ladder and m.group(2) not in prim[ladder] and m.group(2) in old_prim.get(ladder, {}):
                    b[g][k] = nearest_ladder_alias(old_prim[ladder][m.group(2)], prim[ladder], ladder)
                continue
            if not isinstance(v, (int, float)):
                continue
            hit = next((pk for pk, pv in prim[ladder].items() if pv == cast(v)), None)
            if hit is not None:
                b[g][k] = "{%s.%s}" % (ladder, hit)
            elif g in ("alpha", "alpha8", "stroke"):
                b[g].setdefault("off_scale", {})[k] = v
                del b[g][k]
        for k, v in list(b[g].get("off_scale", {}).items()):
            if snap_d2 and ladder in ("opacity", "opacity8"):
                b[g][k] = nearest_ladder_alias(v, prim[ladder], ladder); del b[g]["off_scale"][k]
                continue
            hit = next((pk for pk, pv in prim[ladder].items() if pv == cast(v)), None)
            if hit is not None:
                b[g][k] = "{%s.%s}" % (ladder, hit); del b[g]["off_scale"][k]
        if g in b and b[g].get("off_scale") == {}:
            del b[g]["off_scale"]
    # 4. surfaces
    hit_min = float(b.get("hit_min", 20))
    for g in NUM_GROUPS + COL_GROUPS:
        for s, kv in sj.get(g, {}).items():
            sj[g][s] = tier_surface(kv, prim, g in COL_GROUPS, group=g, refile=refile, use_jobs=use_jobs,
                                    snap_space=snap_space, snap_d1=snap_d1, snap_d2=snap_d2, snap_cols=snap_cols,
                                    old_prim=old_prim, hit_min=hit_min)
    return tok, sj


def report(tok, sj):
    c = count(sj, tok)
    top = {k: v for k, v in c.items() if k not in ("alias_job", "alias_prim")}
    print("surface tokens by tier:", ", ".join(f"{k} {v}" for k, v in sorted(top.items(), key=lambda kv: -kv[1])),
          f"(aliases: {c['alias_job']} job, {c['alias_prim']} prim)")
    off = []
    for g in NUM_GROUPS:
        for s, kv in sj.get(g, {}).items():
            for k, v in kv.get("off_scale", {}).items():
                off.append((kind(k, float(v), g), f"{s}_{k}", v))
    for g in ("alpha", "alpha8", "stroke"):
        for k, v in tok["base"].get(g, {}).get("off_scale", {}).items():
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
    refile = "--refile" in sys.argv
    use_jobs = "--jobs" in sys.argv
    snap_space = "--snap-space" in sys.argv
    snap_d1 = "--snap-d1" in sys.argv
    snap_d2 = "--snap-d2" in sys.argv
    snap_cols = "--colours" in sys.argv
    if "--apply" in sys.argv or refile or use_jobs or snap_space or snap_d1 or snap_d2 or snap_cols:
        tok, sj = apply(tok, sj, refile=refile, use_jobs=use_jobs or snap_space or snap_d1 or snap_d2,
                        snap_space=snap_space, snap_d1=snap_d1, snap_d2=snap_d2, snap_cols=snap_cols)
        TOK.write_text(json.dumps(tok, indent=1, ensure_ascii=False) + "\n")
        SURF.write_text(dumps(sj))
        print("wrote tokens.json, surfaces.json")
    if "--lint" in sys.argv:
        bad = lint(tok, sj)
        for b in bad[:40]:
            print(b)
        if bad:
            print(f"{len(bad)} token(s) failed tier lint: run tools/design/token_tiers.py --refile")
            return 1
        print("token tiers: every semantic token aliases a primitive or job token, or says why")
        return 0
    report(tok, sj)
    return 0


if __name__ == "__main__":
    sys.exit(main())
