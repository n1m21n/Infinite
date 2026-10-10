"""Infinite brand 3D kit: the node as an instrument object, its controls, the logo and the brand ball.

    python3 tools/brand/build_3d.py            # every asset, every render (about 2 min on an M-series laptop)
    python3 tools/brand/build_3d.py --quick    # low samples, for layout checks
    python3 tools/brand/build_3d.py --only node_hero_cutout,logo_3d_cutout   # just these renders, no save/export
    python3 tools/brand/build_3d.py --no-render  # geometry, animation, .blend and .glb only (seconds, not minutes)

Cutouts (*_cutout.png) are transparent PNGs rendered in Cycles with the floor as a shadow catcher, so the object and
its contact shadow sit on any ground (channels.py, slides, the website).

Nodes: Audio Filter (hero), LFO, Field Pixel, and the patch LFO -> Field Pixel; each laid out from its body code.
The .glb files carry baked loops on the brand springs (knob turn, keycap press, LFO playhead, cable plug-in).
studio.blend is the stage alone (world, lights, shadow-catcher floor, cameras) for new scenes.

Run with system Python: it re-launches itself inside Blender (-b). Every number comes from docs/brand/brand.json
(node3d, logo, colour.modes.midnight); geometry is built in app pixels (1 unit = 1 px at 100% UI scale) and exported
to glTF in metres (1 px = 1 mm). Outputs go to art/brand/3d/: *.glb, kit.blend, renders/*.png.
"""
import json
import math
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BLENDER = os.environ.get("BLENDER", "/Applications/Blender.app/Contents/MacOS/Blender")

try:
    import bpy
    import bmesh
except ImportError:
    if __name__ == "__main__":
        args = [a for a in sys.argv[1:]]
        sys.exit(subprocess.call([BLENDER, "-b", "--factory-startup", "-P", os.path.abspath(__file__), "--"] + args))
    raise

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
QUICK = "--quick" in ARGS
NORENDER = "--no-render" in ARGS
ONLY = set(next((a.split("=", 1)[1] for a in ARGS if a.startswith("--only=")), "").split(",")) - {""}
if "--only" in ARGS:
    ONLY = set(ARGS[ARGS.index("--only") + 1].split(","))
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))
M = B["colour"]["modes"]["midnight"]
N3 = B["node3d"]
OUT = os.path.join(ROOT, "art", "brand", "3d")
REN = os.path.join(OUT, "renders")
FONTS = os.path.join(ROOT, "art", "brand", "fonts")
os.makedirs(REN, exist_ok=True)

CAT = {c["name"]: c["hex"] for c in B["colour"]["category"]}
ROLE = {r["name"]: r["dark"] for r in B["colour"]["role"]}
EMBER = B["colour"]["anchors"]["ember"]["400"]
SIGNAL = M["signal"]
MOON = M["ink"]
MOD = ROLE["Modulation"]


# ---------------------------------------------------------------- colour + materials
def lin(h):
    h = h.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple((x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4) for x in c) + (1.0,)


def mix_hex(a, b, t):
    ca, cb = [int(a[i:i + 2], 16) for i in (1, 3, 5)], [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02X}" for x, y in zip(ca, cb))


def setin(node, name, val):
    if name in node.inputs:
        node.inputs[name].default_value = val


_MATS = {}


def mat(name, hexc, rough=0.5, metal=0.0, emit=0.0, alpha=1.0, coat=0.0, film=0.0, sheen=0.0):
    if name in _MATS:
        return _MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    setin(p, "Base Color", lin(hexc))
    setin(p, "Roughness", rough)
    setin(p, "Metallic", metal)
    setin(p, "Coat Weight", coat)
    setin(p, "Sheen Weight", sheen)
    if emit:
        setin(p, "Emission Color", lin(hexc))
        setin(p, "Emission Strength", emit)
    if film:
        setin(p, "Thin Film Thickness", film)
        setin(p, "Thin Film IOR", 1.45)
    if alpha < 1:
        setin(p, "Alpha", alpha)
        try:
            m.surface_render_method = "BLENDED"
        except (AttributeError, TypeError):
            pass
    m.diffuse_color = lin(hexc)
    _MATS[name] = m
    return m


def gradient_mat(name, stops, positions, span):
    """Brand gradient across object X from -span to +span (the logo's own horizontal gradient)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    setin(p, "Roughness", 0.32)
    setin(p, "Coat Weight", 0.35)
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sx = nt.nodes.new("ShaderNodeSeparateXYZ")
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.inputs["From Min"].default_value = -span
    mr.inputs["From Max"].default_value = span
    cr = nt.nodes.new("ShaderNodeValToRGB")
    els = cr.color_ramp.elements
    while len(els) < len(stops):
        els.new(0.5)
    for e, h, t in zip(els, stops, positions):
        e.position, e.color = t, lin(h)
    nt.links.new(tc.outputs["Object"], sx.inputs[0])
    nt.links.new(sx.outputs["X"], mr.inputs["Value"])
    nt.links.new(mr.outputs["Result"], cr.inputs["Fac"])
    nt.links.new(cr.outputs["Color"], p.inputs["Base Color"])
    m.diffuse_color = lin(stops[0])
    return m


# ---------------------------------------------------------------- geometry helpers
def rrect(w, h, r, seg=10, cx=0.0, cy=0.0):
    """Rounded rectangle outline centred on (cx, cy), counter-clockwise."""
    r = min(r, w / 2, h / 2)
    pts = []
    for qx, qy, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        ox, oy = cx + qx * (w / 2 - r), cy + qy * (h / 2 - r)
        for i in range(seg + 1):
            a = math.radians(a0 + 90 * i / seg)
            pts.append((ox + r * math.cos(a), oy + r * math.sin(a)))
    return pts


def circle(r, n=64, cx=0.0, cy=0.0):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def sector(r0, r1, a0, a1, cx, cy, n=48):
    """Annular sector from angle a0 to a1 (degrees, either direction)."""
    steps = max(2, int(n * abs(a1 - a0) / 360) + 2)
    outer = [(cx + r1 * math.cos(math.radians(a0 + (a1 - a0) * i / (steps - 1))),
              cy + r1 * math.sin(math.radians(a0 + (a1 - a0) * i / (steps - 1)))) for i in range(steps)]
    inner = [(cx + r0 * math.cos(math.radians(a0 + (a1 - a0) * i / (steps - 1))),
              cy + r0 * math.sin(math.radians(a0 + (a1 - a0) * i / (steps - 1)))) for i in reversed(range(steps))]
    pts = outer + inner
    area = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]))
    return pts if area > 0 else pts[::-1]


def link(obj, coll, layer):
    coll.objects.link(obj)
    obj["layer"] = layer
    return obj


def prism(name, pts, z0, z1, material, coll, layer="body", bevel=0.0, segs=3, smooth=True):
    """Extrude a 2D outline between z0 and z1, optional edge bevel, baked to a plain mesh."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bot = [bm.verts.new((x, y, z0)) for x, y in pts]
    top = [bm.verts.new((x, y, z1)) for x, y in pts]
    bm.faces.new(top)
    bm.faces.new(bot[::-1])
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bot[i], bot[j], top[j], top[i]))
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    link(ob, coll, layer)
    me.materials.append(material)
    if bevel > 0:
        md = ob.modifiers.new("bevel", "BEVEL")
        md.width, md.segments, md.limit_method = bevel, segs, "ANGLE"
        md.angle_limit = math.radians(50)
    bake(ob, smooth)
    return ob


def bake(ob, smooth=True, sharp=34):
    if ob.modifiers:
        dg = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
        old = ob.data
        ob.modifiers.clear()
        ob.data = me
        bpy.data.meshes.remove(old)
    if smooth and ob.type == "MESH":
        for p in ob.data.polygons:
            p.use_smooth = True
        try:
            ob.data.set_sharp_from_angle(angle=math.radians(sharp))
        except AttributeError:
            pass


def carve(target, cutters):
    """Boolean-subtract cutter prisms (recesses) from target, keeping the target's own material on the walls."""
    for c in cutters:
        md = target.modifiers.new("cut", "BOOLEAN")
        md.operation, md.object = "DIFFERENCE", c
        try:
            md.solver = "EXACT"
        except TypeError:
            pass
    bake(target)
    for c in cutters:
        bpy.data.objects.remove(c)


def text(body, size, font, material, coll, x, y, z, align="LEFT", layer="print", rot=0.0):
    cu = bpy.data.curves.new("t_" + body[:12], "FONT")
    cu.body = body
    cu.size = size
    cu.font = bpy.data.fonts.load(os.path.join(FONTS, font), check_existing=True)
    cu.align_x = align
    cu.align_y = "BOTTOM_BASELINE"
    ob = bpy.data.objects.new("text_" + body[:12], cu)
    link(ob, coll, layer)
    ob.location = (x, y, z)
    ob.rotation_euler = (0, 0, rot)
    cu.materials.append(material)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    mo = bpy.data.objects.new(ob.name, me)
    link(mo, coll, layer)
    mo.location, mo.rotation_euler = ob.location, ob.rotation_euler
    me.materials.clear()
    me.materials.append(material)
    bpy.data.objects.remove(ob)
    bpy.data.curves.remove(cu)
    return mo


def tube(name, pts, radius, material, coll, layer="cable", cyclic=False, res=12):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius
    cu.bevel_resolution = res // 2
    cu.use_fill_caps = True
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for p, q in zip(sp.points, pts):
        p.co = (q[0], q[1], q[2], 1.0)
    sp.use_cyclic_u = cyclic
    ob = bpy.data.objects.new(name, cu)
    link(ob, coll, layer)
    cu.materials.append(material)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    mo = bpy.data.objects.new(name, me)
    link(mo, coll, layer)
    me.materials.clear()
    me.materials.append(material)
    bpy.data.objects.remove(ob)
    bpy.data.curves.remove(cu)
    mo.name = name
    bake(mo, sharp=60)
    return mo


def cylinder_x(name, r, x0, x1, y, z, material, coll, layer, n=48, bevel=0.0):
    """Cylinder along the X axis (jacks and plugs in the side walls)."""
    ob = prism(name, circle(r, n), 0, x1 - x0, material, coll, layer, bevel=bevel)
    ob.rotation_euler = (0, math.radians(90), 0)
    ob.location = (x0, y, z)
    return ob


def bezier3(p0, p1, p2, p3, n=48):
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append(tuple(u ** 3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t ** 3 * d for a, b, c, d in zip(p0, p1, p2, p3)))
    return out


def new_coll(name):
    c = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(c)
    return c


# ---------------------------------------------------------------- shared materials
def materials():
    mm = N3["materials"]
    return {
        "body": mat("body", M["surface-1"], mm["body"]["roughness"]),
        "title": mat("title", mix_hex(M["surface-1"], CAT["Audio Effects"], 0.18), 0.5),
        "deep": mat("deep", M["deep"], 0.6),
        "well": mat("well", "#141830", 0.1, coat=1.0),
        "knob": mat("knob", mm["knob"]["base"], mm["knob"]["roughness"], sheen=mm["knob"].get("sheen", 0)),
        "cap": mat("cap", mm["cap_top"]["base"], mm["cap_top"]["roughness"]),
        "key": mat("key", mm["keycap"]["base"], mm["keycap"]["roughness"]),
        "metal": mat("metal", mm["metal"]["base"], mm["metal"]["roughness"], metal=1.0),
        "ink": mat("ink", M["ink"], 0.6, emit=0.15),
        "ink2": mat("ink2", M["ink-2"], 0.6),
        "ink3": mat("ink3", M["ink-3"], 0.6),
        "line": mat("line", M["line"], 0.6),
        "signal": mat("signal", SIGNAL, 0.4, emit=0.6),
        "signal_fill": mat("signal_fill", mix_hex("#141830", SIGNAL, 0.2), 0.3, emit=0.1),
        "mod": mat("mod", MOD, 0.4, emit=0.6),
        "ember": mat("ember", EMBER, 0.38),
        "ember_print": mat("ember_print", EMBER, 0.4, emit=0.4),
        "cat_fx": mat("cat_fx", CAT["Audio Effects"], 0.38),
        "cat_fx_ring": mat("cat_fx_ring", CAT["Audio Effects"], 0.35, emit=0.25),
        "hole": mat("hole", "#05060C", 0.9),
        "led_on": mat("led_on", SIGNAL, 0.2, emit=6.0),
        "led_off": mat("led_off", "#2C3256", 0.3),
        "key_on": mat("key_on", mix_hex(M["surface-2"], SIGNAL, 0.22), 0.4, emit=0.15),
        "floor": mat("floor", mix_hex(M["deep"], M["ground"], 0.5), 0.85),
    }


# ---------------------------------------------------------------- controls (shared by the node and the kit sheet)
TOP = N3["card"]["thickness"]
Z = N3["z"]
KR = N3["knob"]["diameter"] / 2


def knob(c, MT, cx, cy, v, label=None, value=None, bipolar=False, mod=0.0, z=TOP, name="knob"):
    cap_r = KR * N3["knob"]["cap_ratio"]
    ang = 225 - 270 * v
    a_track = sector(KR * 1.12, KR * 1.12 + 2.4, 225, -45, cx, cy)
    prism(name + "_track", a_track, z, z + 0.15, MT["line"], c, "print", smooth=False)
    a0 = 90 if bipolar else 225
    if abs(ang - a0) > 0.5:
        prism(name + "_arc", sector(KR * 1.12, KR * 1.12 + 2.4, a0, ang, cx, cy), z, z + 0.22, MT["signal"], c, "print", smooth=False)
    if mod:
        prism(name + "_mod", sector(KR * 1.12 + 4, KR * 1.12 + 6.2, ang, ang - 270 * mod, cx, cy), z, z + 0.22, MT["mod"], c, "print", smooth=False)
    prism(name + "_skirt", circle(KR, 96, cx, cy), z, z + Z["knob_skirt"] + 1, MT["knob"], c, "controls", bevel=1.4)
    n = N3["knob"]["ridges"]
    star = [(cx + (cap_r - (N3["knob"]["ridge_depth"] if i % 2 else 0)) * math.cos(math.pi * i / n),
             cy + (cap_r - (N3["knob"]["ridge_depth"] if i % 2 else 0)) * math.sin(math.pi * i / n)) for i in range(2 * n)]
    prism(name + "_cap", star, z + Z["knob_skirt"], z + Z["knob_cap"], MT["knob"], c, "caps", bevel=0.8, segs=2)
    prism(name + "_top", circle(cap_r - 2.2, 96, cx, cy), z + Z["knob_cap"] - 0.2, z + Z["knob_cap"] + 0.35, MT["cap"], c, "caps", bevel=0.3, segs=2)
    ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    r0, r1, hw = cap_r * 0.25, cap_r * 0.86, 1.15
    ptr = [(cx + r0 * ca - hw * sa, cy + r0 * sa + hw * ca), (cx + r0 * ca + hw * sa, cy + r0 * sa - hw * ca),
           (cx + r1 * ca + hw * sa, cy + r1 * sa - hw * ca), (cx + r1 * ca - hw * sa, cy + r1 * sa + hw * ca)]
    prism(name + "_ptr", ptr, z + Z["knob_cap"] + 0.2, z + Z["knob_cap"] + 0.45, MT["ink"], c, "caps", smooth=False)
    if label:
        text(label, 13, "Geist-Medium.ttf", MT["ink2"], c, cx, cy - KR - N3["knob"]["caption_gap"] - 12, z + 0.05, "CENTER")
    if value:
        text(value, 10.5, "GeistMono-Medium.ttf", MT["ink3"], c, cx, cy - KR - N3["knob"]["caption_gap"] - 26, z + 0.05, "CENTER")


def keycap(c, MT, cx, cy, w, h, on, label, z=TOP, name="key"):
    zt = z + (Z["button_on"] if on else Z["button_rest"])
    prism(name + "_well", rrect(w + 6, h + 6, (h + 6) / 2, 10, cx, cy), z - 0.2, z + 0.12, MT["deep"], c, "print", smooth=False)
    prism(name, rrect(w, h, h / 2, 12, cx, cy), z, zt, MT["key_on"] if on else MT["key"], c, "controls", bevel=1.6)
    text(label, 11.5, "Geist-Medium.ttf", MT["ink"] if on else MT["ink2"], c, cx + 5, cy - 4, zt + 0.05, "CENTER")
    prism(name + "_led", circle(2.2, 24, cx - w / 2 + 11, cy), zt, zt + 0.5, MT["led_on"] if on else MT["led_off"], c, "caps", bevel=0.3, segs=2)


def checkbox(c, MT, cx, cy, on, label=None, z=TOP, name="check"):
    s = N3["controls"]["checkbox"]
    zt = z + Z["switch"]
    prism(name, rrect(s, s, 4, 6, cx, cy), z, zt, MT["key_on"] if on else MT["key"], c, "controls", bevel=1.0)
    prism(name + "_led", circle(2.4, 24, cx, cy), zt, zt + 0.5, MT["led_on"] if on else MT["led_off"], c, "caps", bevel=0.3, segs=2)
    if label:
        text(label, 13, "Geist-Medium.ttf", MT["ink2"], c, cx, cy - KR - N3["knob"]["caption_gap"] - 12, z + 0.05, "CENTER")


def switch(c, MT, cx, cy, on, z=TOP, name="switch"):
    w, h = N3["controls"]["switch"]
    prism(name + "_track", rrect(w, h, h / 2, 12, cx, cy), z - 0.2, z + 0.15, MT["signal"] if on else MT["deep"], c, "print", smooth=False)
    tx = cx + (w / 2 - h / 2) * (1 if on else -1)
    prism(name + "_thumb", circle(h / 2 - 2, 48, tx, cy), z, z + Z["switch"], MT["cap"], c, "controls", bevel=1.2)


def dropdown(c, MT, cx, cy, w, value, label=None, z=TOP, name="dropdown"):
    h = N3["controls"]["dropdown"][1]
    prism(name, rrect(w, h, 4, 6, cx, cy), z - 0.2, z + 0.12, MT["deep"], c, "print", smooth=False)
    text(value, 12.5, "Geist-Medium.ttf", MT["ink"], c, cx - w / 2 + 8, cy - 4.5, z + 0.18)
    if label:
        text(label, 13, "Geist-Medium.ttf", MT["ink2"], c, cx, cy - KR - N3["knob"]["caption_gap"] - 12, z + 0.05, "CENTER")


def fader(c, MT, cx, cy, v, z=TOP, name="fader"):
    W, H = N3["controls"]["fader"]
    hw, hh = N3["controls"]["fader_handle"]
    prism(name + "_rail", rrect(6, H - hh, 3, 6, cx, cy), z - 0.2, z + 0.12, MT["deep"], c, "print", smooth=False)
    hy = cy - (H - hh) / 2 + v * (H - hh)
    prism(name + "_fill", rrect(2.4, hy - (cy - (H - hh) / 2), 1.2, 4, cx, (hy + cy - (H - hh) / 2) / 2), z + 0.12, z + 0.3, MT["signal"], c, "print", smooth=False)
    prism(name + "_handle", rrect(hw, hh, 3, 6, cx, hy), z, z + Z["slider_thumb"], MT["key"], c, "controls", bevel=1.4)
    prism(name + "_line", rrect(hw - 6, 1.4, 0.7, 2, cx, hy), z + Z["slider_thumb"], z + Z["slider_thumb"] + 0.3, MT["ink"], c, "caps", smooth=False)


def plug_and_cable(c, MT, jack_x, y, zc, direction, colour_mat, far):
    """A plug seated in a side-wall jack plus a cable that sags to the floor (catenary-like cubic) and runs away."""
    d = direction
    L = N3["cable"]["plug_length"]
    pr = N3["cable"]["plug"] / 2
    cylinder_x("plug_sleeve", pr * 0.62, 0, 5, 0, 0, MT["metal"], c, "cable", bevel=0.4).location = (jack_x + (0 if d > 0 else -5), y, zc)
    body = cylinder_x("plug_body", pr, 0, L, 0, 0, colour_mat, c, "cable", bevel=1.8)
    body.location = (jack_x + (5 if d > 0 else -5 - L), y, zc)
    cr = N3["cable"]["diameter"] / 2
    x0 = jack_x + d * (5 + L)
    pts = bezier3((x0 - d * 1, y, zc), (x0 + d * 60, y, zc), (x0 + d * 70, y - 20, cr), (x0 + d * 150, y - 40, cr))
    pts += bezier3((x0 + d * 150, y - 40, cr), (x0 + d * 220, y - 60, cr), far[0], far[1])[1:]
    tube("cable", pts, cr, colour_mat, c)


# ---------------------------------------------------------------- the node: Audio Filter as an instrument object
def build_node(MT):
    c = new_coll("node_audio_filter")
    cd = N3["card"]
    W, P, R, F = cd["width"], cd["padding"], cd["radius"], cd["fillet"]
    ctrl = N3["controls"]
    ty = cd["title_h"]
    ro_y0 = ty + 10
    vw_y0 = ro_y0 + ctrl["readout_h"] + 8
    r1 = vw_y0 + ctrl["viewer_h"] + 14
    k1 = r1 + KR + 6
    sec = k1 + KR + 44
    k2 = sec + 18 + KR + 6
    H = k2 + KR + 44
    ox, oy = W / 2, H / 2

    def P2(x, y):
        return (x - ox, oy - y)

    # body, with the readout strip, the display well and the dropdown recessed into it
    card = prism("card", rrect(W, H, R, 14), 0, TOP, MT["body"], c, "body", bevel=F)
    cx0, cy0 = P2(W / 2, vw_y0 + ctrl["viewer_h"] / 2)
    cutters = [prism("cut_view", rrect(W - 2 * P, ctrl["viewer_h"], 12, 10, cx0, cy0), TOP + Z["display_well"], TOP + 5, MT["deep"], c, "body", smooth=False)]
    rx, ry = P2(W / 2, ro_y0 + ctrl["readout_h"] / 2)
    cutters.append(prism("cut_ro", rrect(W - 2 * P, ctrl["readout_h"], 6, 8, rx, ry), TOP + Z["dropdown_well"], TOP + 5, MT["deep"], c, "body", smooth=False))
    carve(card, cutters)
    prism("view_floor", rrect(W - 2 * P - 0.2, ctrl["viewer_h"] - 0.2, 12, 10, cx0, cy0), TOP + Z["display_well"] - 0.5, TOP + Z["display_well"] + 0.05, MT["well"], c, "body", smooth=False)
    prism("readout_floor", rrect(W - 2 * P - 0.2, ctrl["readout_h"] - 0.2, 6, 8, rx, ry), TOP + Z["dropdown_well"] - 0.5, TOP + Z["dropdown_well"] + 0.05, MT["deep"], c, "body", smooth=False)

    # title strip: flush print in the category tint, rounded only where the card is
    ins, rr = F, R - F
    xr, yt, yb = W / 2 - ins, oy - ins, oy - ins - ty
    tl = [(xr, yb)]
    for cxs, a0 in ((xr - rr, 0), (-(xr - rr), 90)):
        for i in range(15):
            a = math.radians(a0 + 90 * i / 14)
            tl.append((cxs + rr * math.cos(a), yt - rr + rr * math.sin(a)))
    tl.append((-xr, yb))
    prism("title_strip", tl, TOP - 0.05, TOP + 0.12, MT["title"], c, "print", smooth=False)
    text("Audio Filter", 15, "Geist-SemiBold.ttf", MT["ink"], c, *P2(P + 6, 20), TOP + 0.15)
    text("AUDIO EFFECTS", 9.5, "GeistMono-Medium.ttf", MT["ink3"], c, *P2(W - P - 6, 19.5), TOP + 0.15, "RIGHT")

    # readout strip and the response viewer (graticule, curve, fill)
    text("lowpass   1.20 kHz   Q 2.00   0.0 dB", 10.5, "GeistMono-Medium.ttf", MT["ink2"], c, *P2(P + 10, ro_y0 + 14.5), TOP + Z["dropdown_well"] + 0.1)
    vx0, vx1 = P + 10, W - P - 10
    vy0, vy1 = vw_y0 + 12, vw_y0 + ctrl["viewer_h"] - 12
    zf = TOP + Z["display_well"] + 0.1

    def fx(f):
        return vx0 + (math.log10(f) - math.log10(20)) / 3 * (vx1 - vx0)

    def dby(db):
        return vy0 + (18 - db) / 54 * (vy1 - vy0)

    for f in (100, 1000, 10000):
        prism(f"grid_{f}", rrect(0.8, vy1 - vy0, 0.2, 1, *P2(fx(f), (vy0 + vy1) / 2)), zf, zf + 0.06, MT["line"], c, "print", smooth=False)
        text(f"{f // 1000}k" if f >= 1000 else str(f), 9, "GeistMono-Medium.ttf", MT["ink3"], c, *P2(fx(f) + 4, vy1 - 3), zf + 0.06)
    for db in (12, 0, -12, -24):
        prism(f"grid_db{db}", rrect(vx1 - vx0, 0.8, 0.2, 1, *P2((vx0 + vx1) / 2, dby(db))), zf, zf + 0.06, MT["line"], c, "print", smooth=False)
    f0, q = 1200.0, 2.0
    curve = []
    for i in range(161):
        f = 20 * 10 ** (3 * i / 160)
        x = f / f0
        mag = 1 / math.sqrt((1 - x * x) ** 2 + (x / q) ** 2)
        db = max(-36, 20 * math.log10(mag))
        curve.append((fx(f), dby(db)))
    fill = [P2(x, min(y, vy1)) for x, y in curve] + [P2(vx1, vy1), P2(vx0, vy1)]
    prism("response_fill", fill[::-1] if sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(fill, fill[1:] + fill[:1])) < 0 else fill, zf + 0.06, zf + 0.1, MT["signal_fill"], c, "print", smooth=False)
    tube("response", [(*P2(x, min(y, vy1)), zf + 1.1) for x, y in curve], 1.1, MT["signal"], c, "print", res=6)

    # row 1: type, freq, Q, gain  (four cells of 110)
    cell = (W - 2 * P) / 4

    def cx_(i):
        return P + cell * (i + 0.5)

    dropdown(c, MT, *P2(cx_(0), k1), 92, "lowpass", "type")
    knob(c, MT, *P2(cx_(1), k1), 0.62, "freq", "1.20 kHz", name="k_freq")
    knob(c, MT, *P2(cx_(2), k1), 0.11, "Q", "2.00", name="k_q")
    knob(c, MT, *P2(cx_(3), k1), 0.5, "gain", "0.0 dB", bipolar=True, name="k_gain")
    # section header and row 2: mix, env, sync, rate (rate is modulated)
    text("OUTPUT", 9.5, "GeistMono-Medium.ttf", MT["ink3"], c, *P2(P + 6, sec + 4), TOP + 0.05)
    prism("sec_rule", rrect(W - 2 * P - 70, 0.8, 0.2, 1, *P2(P + 64 + (W - 2 * P - 70) / 2, sec)), TOP, TOP + 0.08, MT["line"], c, "print", smooth=False)
    knob(c, MT, *P2(cx_(0), k2), 1.0, "mix", "1.00", name="k_mix")
    knob(c, MT, *P2(cx_(1), k2), 0.68, "env", "+0.36", bipolar=True, name="k_env")
    checkbox(c, MT, *P2(cx_(2), k2), True, "sync", name="c_sync")
    knob(c, MT, *P2(cx_(3), k2), 0.42, "rate", "1/8", mod=0.22, name="k_rate")

    # jacks in the side walls at the pin row, the Ember key cable in, the category cable out
    jy = P2(0, ro_y0 + ctrl["readout_h"] / 2)[1]
    zc = TOP / 2
    jo, ji = N3["jack"]["outer"] / 2, N3["jack"]["inner"] / 2
    for side, sx in (("in", -W / 2), ("out", W / 2)):
        d = -1 if side == "in" else 1
        ring = cylinder_x(f"jack_{side}", jo, 0, 1.2, 0, 0, MT["cat_fx_ring"], c, "jack", bevel=0.4)
        ring.location = (sx + (0 if d > 0 else -1.2), jy, zc)
        hole = cylinder_x(f"hole_{side}", ji, 0, 1.3, 0, 0, MT["hole"], c, "jack")
        hole.location = (sx + (0.05 if d > 0 else -1.35), jy, zc)
    plug_and_cable(c, MT, -W / 2 - 1.2, jy, zc, -1, MT["ember"], ((-W / 2 - 330, -120, 2.5), (-W / 2 - 520, -260, 2.5)))
    plug_and_cable(c, MT, W / 2 + 1.2, jy, zc, 1, MT["cat_fx"], ((W / 2 + 330, 40, 2.5), (W / 2 + 560, 120, 2.5)))
    return c, (W, H)


# ---------------------------------------------------------------- more nodes: one slab, rows from each node's body code
def hslider(c, MT, cx, cy, w, v, label, value, mod=0.0, z=TOP, name="slider"):
    """ModSlider since v0.5: a full-width well, the name inside on the left, the value on the right, Signal fill."""
    h = 24
    prism(name + "_well", rrect(w, h, 6, 8, cx, cy), z - 0.2, z + 0.12, MT["deep"], c, "print", smooth=False)
    fw = max(2.0, (w - 4) * v)
    prism(name + "_fill", rrect(fw, h - 4, 4, 6, cx - w / 2 + 2 + fw / 2, cy), z + 0.12, z + 0.24, MT["signal_fill"], c, "print", smooth=False)
    prism(name + "_edge", rrect(2, h - 4, 1, 2, cx - w / 2 + 2 + fw - 1, cy), z + 0.12, z + 0.5, MT["signal"], c, "print", smooth=False)
    if mod:
        x0 = cx - w / 2 + 2 + fw
        prism(name + "_mod", rrect((w - 4) * mod, 2.2, 1.1, 2, x0 + (w - 4) * mod / 2, cy - h / 2 + 2.5), z + 0.24, z + 0.5, MT["mod"], c, "print", smooth=False)
    text(label, 11.5, "Geist-Medium.ttf", MT["ink2"], c, cx - w / 2 + 9, cy - 4, z + 0.3)
    text(value, 10.5, "GeistMono-Medium.ttf", MT["ink"], c, cx + w / 2 - 9, cy - 4, z + 0.3, "RIGHT")


def slab(MT, coll, W, H, title, cat, cat_hex, wells):
    """Body with recessed wells (list of (x, y, w, h, depth, radius) in app coords, y down), category title strip, jacks."""
    c = new_coll(coll)
    cd = N3["card"]
    P, R, F, ty = cd["padding"], cd["radius"], cd["fillet"], cd["title_h"]
    ox, oy = W / 2, H / 2

    def P2(x, y):
        return (x - ox, oy - y)

    tmat = mat("title_" + coll, mix_hex(M["surface-1"], cat_hex, 0.18), 0.5)
    card = prism("card", rrect(W, H, R, 14), 0, TOP, MT["body"], c, "body", bevel=F)
    cutters = []
    for i, (x, y, w, h, d, r) in enumerate(wells):
        cx0, cy0 = P2(x + w / 2, y + h / 2)
        cutters.append(prism(f"cut_{i}", rrect(w, h, r, 10, cx0, cy0), TOP + d, TOP + 5, MT["deep"], c, "body", smooth=False))
        prism(f"well_{i}", rrect(w - 0.2, h - 0.2, r, 10, cx0, cy0), TOP + d - 0.5, TOP + d + 0.05, MT["well"], c, "body", smooth=False)
    if cutters:
        carve(card, cutters)
    ins, rr = F, R - F
    xr, yt, yb = W / 2 - ins, oy - ins, oy - ins - ty
    tl = [(xr, yb)]
    for cxs, a0 in ((xr - rr, 0), (-(xr - rr), 90)):
        for i in range(15):
            a = math.radians(a0 + 90 * i / 14)
            tl.append((cxs + rr * math.cos(a), yt - rr + rr * math.sin(a)))
    tl.append((-xr, yb))
    prism("title_strip", tl, TOP - 0.05, TOP + 0.12, tmat, c, "print", smooth=False)
    text(title, 15, "Geist-SemiBold.ttf", MT["ink"], c, *P2(P + 6, 20), TOP + 0.15)
    text(cat.upper(), 9.5, "GeistMono-Medium.ttf", MT["ink3"], c, *P2(W - P - 6, 19.5), TOP + 0.15, "RIGHT")
    ring = mat("ring_" + coll, cat_hex, 0.35, emit=0.25)
    jy = P2(0, ty + 22)[1]
    jo, ji = N3["jack"]["outer"] / 2, N3["jack"]["inner"] / 2
    for side, sx in (("in", -W / 2), ("out", W / 2)):
        d = -1 if side == "in" else 1
        cylinder_x(f"jack_{side}", jo, 0, 1.2, 0, 0, ring, c, "jack", bevel=0.4).location = (sx + (0 if d > 0 else -1.2), jy, TOP / 2)
        cylinder_x(f"hole_{side}", ji, 0, 1.3, 0, 0, MT["hole"], c, "jack").location = (sx + (0.05 if d > 0 else -1.35), jy, TOP / 2)
    return c, P2, jy


ANIM = {}  # objects and paths the builders hand to animate()


def build_lfo(MT, origin):
    """LFO (Modulators): wave viewer, shape dropdown, rate / phase / low / high sliders (bodies/ParamBodies1.cpp DrawLFOParams)."""
    W = 250                                        # PatchLayout.cpp default width
    P, ty = N3["card"]["padding"], N3["card"]["title_h"]
    cw = W - 2 * P
    vy, vh = ty + 10, 92
    dy = vy + vh + 12 + 13
    rows = [dy + 34 + 30 * i for i in range(4)]
    H = rows[-1] + 12 + 18
    c, P2, jy = slab(MT, "node_lfo", W, H, "LFO", "Modulators", CAT["Modulators"], [(P, vy, cw, vh, Z["display_well"], 10)])
    zf = TOP + Z["display_well"] + 0.1
    x0, x1, ya, yb = P + 10, W - P - 10, vy + 14, vy + vh - 14
    for i in range(3):
        prism(f"grid_{i}", rrect(x1 - x0, 0.8, 0.2, 1, *P2((x0 + x1) / 2, ya + (yb - ya) * i / 2)), zf, zf + 0.06, MT["line"], c, "print", smooth=False)
    wave = [(x0 + (x1 - x0) * i / 160, (ya + yb) / 2 - (yb - ya) / 2 * math.sin(2 * math.pi * 1.5 * i / 160)) for i in range(161)]
    tube("wave", [(*P2(x, y), zf + 1.1) for x, y in wave], 1.1, MT["mod"], c, "print", res=6)
    k = 0.37                                       # playhead
    px, py = wave[int(k * 160)]
    ph = prism("playhead", circle(3.4, 32, 0, 0), zf + 0.5, zf + 2.6, MT["ink"], c, "caps", bevel=0.4, segs=2)
    ph.location.xy = P2(px, py)
    ANIM["wave"] = [P2(x, y) for x, y in wave]
    dropdown(c, MT, *P2(W / 2, dy), cw, "Sine", name="d_shape")
    for i, (lab, v, val, mod) in enumerate((("rate", 0.12, "4.00 beats", 0.0), ("phase", 0.0, "0.00", 0.0),
                                            ("low", 0.0, "0.00", 0.0), ("high", 1.0, "1.00", 0.0))):
        hslider(c, MT, *P2(W / 2, rows[i]), cw, v, lab, val, mod, name=f"s_{lab}")
    for ob in c.objects:
        ob.location.x += origin[0]
        ob.location.y += origin[1]
    return c, (W, H), jy


def build_field_pixel(MT, origin):
    """Field Pixel (Source): live preview, preset dropdown, Save / Export / Import, Edit Field..., the preset's params
    (bodies/FieldParams.cpp DrawFieldPixelParams with the Organic Liquid Warp preset)."""
    W = 340
    P, ty = N3["card"]["padding"], N3["card"]["title_h"]
    cw = W - 2 * P
    vy, vh = ty + 10, round(cw * 9 / 16)
    dy = vy + vh + 12 + 13
    by = dy + 26 + 4
    ey = by + 30
    rows = [ey + 32 + 30 * i for i in range(4)]
    H = rows[-1] + 12 + 18
    c, P2, jy = slab(MT, "node_field_pixel", W, H, "Field Pixel", "Source", CAT["Source"], [(P, vy, cw, vh, Z["display_well"], 10)])
    # the preview: a Field-style warped cosine palette as an emissive gradient on the well floor
    cx0, cy0 = P2(W / 2, vy + vh / 2)
    pm = gradient_mat("field_preview", [M["deep"], mix_hex(M["deep"], SIGNAL, 0.6), mix_hex(M["deep"], MOD, 0.5), mix_hex(M["deep"], EMBER, 0.7), M["deep"]], [0.0, 0.3, 0.55, 0.8, 1.0], cw / 2)
    bsdf = next(n for n in pm.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    ramp = next(n for n in pm.node_tree.nodes if n.type == "VALTORGB")
    pm.node_tree.links.new(ramp.outputs["Color"], bsdf.inputs["Emission Color"])
    setin(bsdf, "Emission Strength", 0.45)
    prism("preview", rrect(cw - 6, vh - 6, 8, 10, cx0, cy0), TOP + Z["display_well"] + 0.05, TOP + Z["display_well"] + 0.15, pm, c, "print", smooth=False)
    dropdown(c, MT, *P2(W / 2, dy), cw, "Organic Liquid Warp", name="d_preset")
    bw = (cw - 8) / 3
    for i, lab in enumerate(("Save", "Export", "Import")):
        keycap(c, MT, *P2(P + bw / 2 + i * (bw + 4), by), bw - 4, 22, False, lab, name=f"b_{lab.lower()}")
    before = set(c.objects)
    keycap(c, MT, *P2(W / 2, ey), cw - 4, 24, True, "Edit Field...", name="b_edit")
    ANIM["edit_key"] = [o.name for o in c.objects if o not in before and not o.name.startswith("b_edit_well")]
    for i, (lab, v, val, mod) in enumerate((("scale", 0.33, "3.00", 0.0), ("speed", 0.23, "0.50", 0.0),
                                            ("warp", 0.34, "1.50", 0.18), ("hue", 0.0, "0.00", 0.0))):
        hslider(c, MT, *P2(W / 2, rows[i]), cw, v, lab, val, mod, name=f"s_{lab}")
    for ob in c.objects:
        ob.location.x += origin[0]
        ob.location.y += origin[1]
    return c, (W, H), jy


def patch_cable(c, MT, a, b, colour_mat, name="patch"):
    """Plug seated in jack a (output, cable leaves +x) and jack b (input, leaves -x), the cable sagging to the floor
    between them (cable.sag of the span). a and b are (x, y, z) at the jack mouths. Returns the input plug objects."""
    L, pr, cr = N3["cable"]["plug_length"], N3["cable"]["plug"] / 2, N3["cable"]["diameter"] / 2
    plugs = []
    for (x, y, z), d in ((a, 1), (b, -1)):
        sl = cylinder_x(name + "_sleeve", pr * 0.62, 0, 5, 0, 0, MT["metal"], c, "cable", bevel=0.4)
        sl.location = (x + (0 if d > 0 else -5), y, z)
        bd = cylinder_x(name + "_plug", pr, 0, L, 0, 0, colour_mat, c, "cable", bevel=1.8)
        bd.location = (x + (5 if d > 0 else -5 - L), y, z)
        plugs.append([sl, bd])
    xa, xb = a[0] + 5 + L, b[0] - 5 - L
    span = abs(xb - xa)
    low = max(cr, a[2] - N3["cable"]["sag"] * span * 2)
    pts = bezier3((xa - 1, a[1], a[2]), (xa + span * 0.3, a[1], a[2]), (xa + span * 0.25, (a[1] + b[1]) / 2, low),
                  ((xa + xb) / 2, (a[1] + b[1]) / 2, low))
    pts += bezier3(((xa + xb) / 2, (a[1] + b[1]) / 2, low), (xb - span * 0.25, (a[1] + b[1]) / 2, low),
                   (xb - span * 0.3, b[1], b[2]), (xb + 1, b[1], b[2]))[1:]
    tube(name + "_cable", pts, cr, colour_mat, c)
    return plugs[1]


# ---------------------------------------------------------------- baked animation (motion.py laws, 60 fps)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import motion as MO  # noqa: E402

FPS = 60
spring = MO.spring
SPR = {sp["name"]: (sp["zeta"], sp["omega"]) for sp in B["motion"]["springs"]}
BEAT = MO.note_ms("1/4") / 1000                                  # L1: loops are one bar at 120 BPM
BAR = MO.note_ms("bar") / 1000


def on_beat(beat, kind):
    """L3: launch time (s) so the spring's overshoot peak lands on `beat` (0-based quarter notes)."""
    pk = MO.peak_ms(*SPR[kind])
    return beat * BEAT - (pk / 1000 if pk else 0)


def pivot(coll, obs, at, name):
    """An empty at `at` that the objects hang from, so a turn or press happens about the control's own centre."""
    e = bpy.data.objects.new(name, None)
    link(e, coll, "anim")
    e.location = at
    bpy.context.view_layer.update()
    for o in obs:
        mw = o.matrix_world.copy()
        o.parent = e
        o.matrix_world = mw
    return e


def key_spring(ob, path, index, v0, v1, kind, t0, dur):
    """Bake v0 -> v1 on the named spring as one key per frame from t0 (s) for dur (s)."""
    z, w = SPR[kind]
    for f in range(int(dur * FPS) + 1):
        t = f / FPS
        val = v0 + (v1 - v0) * spring(z, w, t)
        getattr(ob, path)[index] = val
        ob.keyframe_insert(path, index=index, frame=1 + int(t0 * FPS) + f)


def animate(node, lfo, fp, patch_plug):
    """Loops for the .glb files: knob turn (slab), keycap press (press), LFO playhead (linear), cable plug-in (slab)."""
    sc = bpy.context.scene
    sc.render.fps = FPS
    # Audio Filter: freq knob sweeps 0.30 -> 0.62 and back (the arc on the card shows the end value)
    obs = [o for o in node.objects if o.name.startswith(("k_freq_cap", "k_freq_top", "k_freq_ptr"))]
    top = next(o for o in obs if o.name.startswith("k_freq_top"))  # a circle: its vertex mean is the knob centre
    ctr = [sum((top.matrix_world @ v.co)[i] for v in top.data.vertices) / len(top.data.vertices) for i in range(2)]
    e = pivot(node, obs, (ctr[0], ctr[1], 0), "k_freq_turn")
    a = math.radians(270 * (0.62 - 0.30))
    t1, t3 = on_beat(1, "slab"), on_beat(3, "slab")             # peaks on beats 2 and 4
    key_spring(e, "rotation_euler", 2, a, 0.0, "slab", t1, t3 - t1)
    key_spring(e, "rotation_euler", 2, 0.0, a, "slab", t3, BAR - t3)
    # Field Pixel: "Edit Field..." keycap goes down on beat 2 for a 1/8, comes back
    ek = [bpy.data.objects[n] for n in ANIM["edit_key"]]
    bpy.context.view_layer.update()
    ctr = [sum(o.matrix_world.translation[i] for o in ek) / len(ek) for i in range(2)]
    e = pivot(fp, ek, (ctr[0], ctr[1], 0), "edit_press")
    drop = Z["button_rest"] - Z["button_on"]
    eighth = MO.note_ms("1/8") / 1000
    key_spring(e, "location", 2, 0.0, -drop, "press", BEAT, eighth)
    key_spring(e, "location", 2, -drop, 0.0, "press", BEAT + eighth, BAR - BEAT - eighth)
    # LFO: playhead rides the wave, one cycle per bar
    ph = next(o for o in lfo.objects if o.name.startswith("playhead"))
    wave = ANIM["wave"]
    ox, oy = ph.location.x - wave[int(0.37 * 160)][0], ph.location.y - wave[int(0.37 * 160)][1]  # the build origin
    n = int(round(BAR * FPS))
    for f in range(n + 1):
        i = int(160 * f / n) % 161
        ph.location.x, ph.location.y = wave[i][0] + ox, wave[i][1] + oy
        ph.keyframe_insert("location", frame=1 + f)
    # Patch: the input plug slides 40 px out and snaps home on the slab spring, overshoot peak on beat 2
    for o in patch_plug:
        x = o.location.x
        key_spring(o, "location", 0, x - 40, x, "slab", on_beat(1, "slab"), BAR - on_beat(1, "slab"))
    sc.frame_start, sc.frame_end = 1, int(round(BAR * FPS)) + 1
    for ob in bpy.data.objects:
        if ob.animation_data and ob.animation_data.action:
            for fc in getattr(ob.animation_data.action, "fcurves", []):
                for kp in fc.keyframe_points:
                    kp.interpolation = "LINEAR"


# ---------------------------------------------------------------- component sheet
def build_kit(MT, origin):
    c = new_coll("kit_components")
    prism("kit_plate", rrect(660, 470, 24, 14), 0, TOP, MT["body"], c, "body", bevel=3)
    items = [
        ("knob", lambda x, y: knob(c, MT, x, y, 0.62, name="kk1")),
        ("knob, modulated", lambda x, y: knob(c, MT, x, y, 0.4, mod=0.25, name="kk2")),
        ("keycap", lambda x, y: keycap(c, MT, x, y, 72, 26, False, "learn", name="kb1")),
        ("keycap, on", lambda x, y: keycap(c, MT, x, y, 72, 26, True, "learn", name="kb2")),
        ("switch", lambda x, y: switch(c, MT, x, y, True, name="ks")),
        ("checkbox", lambda x, y: checkbox(c, MT, x, y, True, name="kc")),
        ("dropdown", lambda x, y: dropdown(c, MT, x, y, 92, "lowpass", name="kd")),
        ("fader", lambda x, y: fader(c, MT, x, y + 6, 0.7, name="kf")),
    ]
    for i, (lab, fn) in enumerate(items):
        x, y = -232 + 155 * (i % 4), 105 - 215 * (i // 4)
        fn(x, y)
        text(lab.upper(), 10, "GeistMono-Medium.ttf", MT["ink3"], c, x, y - 92, TOP + 0.05, "CENTER")
    for ob in c.objects:
        ob.location.x += origin[0]
        ob.location.y += origin[1]
    return c


# ---------------------------------------------------------------- logo
def lemniscate(A, n=480, lift=0.0):
    pts = []
    for i in range(n):
        p = 2 * math.pi * i / n
        d = 1 + math.cos(p) ** 2
        pts.append((A * math.sin(p) / d, A * math.sin(p) * math.cos(p) / d, lift * math.cos(p)))
    return pts


def build_logo(origin):
    c = new_coll("logo")
    L = B["logo"]
    A = 100.0
    s = L["stroke"] * A
    g = B["colour"]["gradient"]
    gm = gradient_mat("brand_gradient", g["stops"], g["positions"], A + s / 2)
    # the 3D mark: one tube; the two passes through the crossing separate by one stroke so it reads over/under
    pts = [(x, y, z + s / 2 + s * 0.62) for x, y, z in lemniscate(A, 480, s * 0.62)]
    ob = tube("lemniscate", pts, s / 2, gm, c, "logo", cyclic=True, res=24)
    ob.location = origin
    # the tile: Midnight rounded square, corner and mark width from brand.json logo.tile, sitting flush (flat mark extruded 2)
    T = (2 * A + s) / B["logo"]["tile"]["mark_width"]
    tile = prism("tile", rrect(T, T, T * B["logo"]["tile"]["corner"], 16), 0, 24, mat("tile", M["ground"], 0.5), c, "logo", bevel=5)
    flat = tube("lemniscate_flat", [(x, y, 24 + s / 2 - 1) for x, y, _ in lemniscate(A, 480)], s / 2, gm, c, "logo", cyclic=True, res=24)
    for o in (tile, flat):
        o.location = (origin[0], origin[1] - 520, origin[2])
    tile.location.z = 0
    flat.location.z = 0
    return c


def build_ball(origin):
    c = new_coll("ball")
    m = bpy.data.materials.new("iridescent")
    m.use_nodes = True
    nt = m.node_tree
    p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    setin(p, "Roughness", 0.22)
    setin(p, "Coat Weight", 1.0)
    setin(p, "Thin Film Thickness", 380.0)
    setin(p, "Thin Film IOR", 1.4)
    lw = nt.nodes.new("ShaderNodeLayerWeight")
    lw.inputs["Blend"].default_value = 0.42
    cr = nt.nodes.new("ShaderNodeValToRGB")
    ir = B["colour"]["iridescence"]["stops"]
    els = cr.color_ramp.elements
    while len(els) < len(ir):
        els.new(0.5)
    for i, (e, h) in enumerate(zip(els, ir)):
        e.position, e.color = i / (len(ir) - 1), lin(h)
    nt.links.new(lw.outputs["Facing"], cr.inputs["Fac"])
    nt.links.new(cr.outputs["Color"], p.inputs["Base Color"])
    m.diffuse_color = lin(ir[0])
    me = bpy.data.meshes.new("ball")
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=96, v_segments=48, radius=60)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("brand_ball", me)
    link(ob, c, "ball")
    me.materials.append(m)
    for p in me.polygons:
        p.use_smooth = True
    ob.location = (origin[0], origin[1], 60)
    return c


# ---------------------------------------------------------------- scene, light, cameras, render
def floor(MT, size=20000):
    me = bpy.data.meshes.new("floor")
    h = size / 2
    me.from_pydata([(-h, -h, 0), (h, -h, 0), (h, h, 0), (-h, h, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new("floor", me)
    bpy.context.scene.collection.objects.link(ob)
    me.materials.append(MT["floor"])
    return ob


def sun(name, az, el, strength, angle, colour="#FFFFFF"):
    ld = bpy.data.lights.new(name, "SUN")
    ld.energy = strength
    ld.angle = math.radians(angle)
    ld.color = lin(colour)[:3]
    ob = bpy.data.objects.new(name, ld)
    bpy.context.scene.collection.objects.link(ob)
    # a sun points down its -Z; aim it from (az, el) toward the origin
    ob.rotation_euler = (math.radians(90 - el), 0, math.radians(az + 90))
    return ob


def camera(name, target, yaw, pitch, dist, lens=50, ortho=None):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    cd.clip_start, cd.clip_end = 1, 50000
    if ortho:
        cd.type = "ORTHO"
        cd.ortho_scale = ortho
    ob = bpy.data.objects.new(name, cd)
    bpy.context.scene.collection.objects.link(ob)
    y, p = math.radians(yaw), math.radians(pitch)
    ob.location = (target[0] + dist * math.cos(p) * math.sin(y), target[1] - dist * math.cos(p) * math.cos(y), target[2] + dist * math.sin(p))
    ob.rotation_euler = (math.radians(90) - p, 0, y)
    return ob


def setup_render():
    sc = bpy.context.scene
    for eng in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
        try:
            sc.render.engine = eng
            break
        except TypeError:
            continue
    ee = sc.eevee
    ee.taa_render_samples = 16 if QUICK else 96
    for attr, val in (("use_shadows", True), ("use_raytracing", True), ("shadow_ray_count", 2), ("shadow_step_count", 8),
                      ("use_gtao", True), ("gtao_distance", 40.0), ("fast_gi_distance", 40.0)):
        if hasattr(ee, attr):
            try:
                setattr(ee, attr, val)
            except (TypeError, AttributeError):
                pass
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.render.film_transparent = False
    sc.render.image_settings.file_format = "PNG"
    w = sc.world or bpy.data.worlds.new("world")
    sc.world = w
    w.use_nodes = True
    bg = next(n for n in w.node_tree.nodes if n.type == "BACKGROUND")
    bg.inputs["Color"].default_value = lin(M["ground"])
    bg.inputs["Strength"].default_value = 0.35


def render(cam, name, res, colls_on):
    if NORENDER or (ONLY and name not in ONLY):
        return
    sc = bpy.context.scene
    for c in bpy.data.collections:
        c.hide_render = c.name not in colls_on
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 50 if QUICK else 100
    sc.render.filepath = os.path.join(REN, name + ".png")
    bpy.ops.render.render(write_still=True)
    print("render", name)


def render_cutout(cam, name, res, colls_on, floor_ob):
    """Transparent PNG with a real contact shadow: Cycles, film transparent, floor as shadow catcher."""
    if NORENDER or (ONLY and name not in ONLY):
        return
    sc = bpy.context.scene
    eng = sc.render.engine
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 24 if QUICK else 128
    sc.cycles.use_denoising = True
    sc.render.film_transparent = True
    sc.render.image_settings.color_mode = "RGBA"
    floor_ob.is_shadow_catcher = True
    try:
        render(cam, name, res, colls_on)
    finally:
        floor_ob.is_shadow_catcher = False
        sc.render.film_transparent = False
        sc.render.image_settings.color_mode = "RGB"
        sc.render.engine = eng


def explode(coll, offsets):
    moved = []
    for ob in coll.objects:
        dz = offsets.get(ob.get("layer"), None)
        if dz is None:
            ob.hide_render = True
        else:
            ob.location.z += dz
        moved.append((ob, dz))
    return moved


def unexplode(moved):
    for ob, dz in moved:
        if dz is None:
            ob.hide_render = False
        else:
            ob.location.z -= dz


def export(colls, name, root_origin):
    """glTF in metres. A scaled root carries the offset, so baked location keys stay valid; undone afterwards."""
    colls = colls if isinstance(colls, (list, tuple)) else [colls]
    obs = [o for c in colls for o in c.objects]
    root = bpy.data.objects.new(name + "_root", None)
    bpy.context.scene.collection.objects.link(root)
    root.scale = (0.001, 0.001, 0.001)
    root.location = (-root_origin[0] * 0.001, -root_origin[1] * 0.001, 0)
    tops = [o for o in obs if o.parent is None]
    for ob in tops:
        ob.parent = root
    bpy.context.scene.frame_set(1)
    bpy.ops.object.select_all(action="DESELECT")
    for ob in obs:
        ob.select_set(True)
    root.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, name + ".glb"), export_format="GLB", use_selection=True, export_apply=True)
    for ob in tops:
        ob.parent = None
    bpy.data.objects.remove(root)
    print("export", name)


def main():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob)
    MT = materials()
    setup_render()
    lt = N3["light"]
    KEY = 4.2
    sun("key", lt["key"]["azimuth"], lt["key"]["elevation"], KEY, 5)
    sun("rim", lt["rim"]["azimuth"], lt["rim"]["elevation"], KEY * lt["rim"]["ratio"], 3, "#DCE2FF")
    fl = floor(MT)

    node, (W, H) = build_node(MT)
    KIT, LOGO, BALL = (0, -1400), (0, 1600), (1400, 1600)
    kit = build_kit(MT, KIT)
    logo = build_logo((LOGO[0], LOGO[1], 0))
    ball = build_ball(BALL)

    yaw = sum(N3["camera"]["yaw"]) / 2
    pitch = sum(N3["camera"]["pitch"]) / 2
    hero = camera("cam_hero", (-20, -10, 20), -yaw, pitch, 1380, 50)
    sun("fill", 270 - yaw, pitch, KEY * lt["fill"]["ratio"], 12)
    render(hero, "node_hero", (1920, 1200), {"node_audio_filter"})
    render_cutout(hero, "node_hero_cutout", (1920, 1200), {"node_audio_filter"}, fl)

    top = camera("cam_top", (0, 0, 0), 0, 90, 3000, ortho=W * 1.14)
    render(top, "node_top", (1200, int(1200 * (H * 1.14) / (W * 1.14) + 0.5) // 2 * 2), {"node_audio_filter"})

    moved = explode(node, {"body": 0, "print": 80, "controls": 160, "caps": 240})
    fl.hide_render = True
    iso = camera("cam_iso", (0, -10, 120), -30, 35.264, 4000, ortho=W * 1.75)
    render(iso, "node_exploded", (1600, 1400), {"node_audio_filter"})
    unexplode(moved)
    fl.hide_render = False

    macro = camera("cam_knob", (-W / 2 + 12 + (W - 24) / 4 * 1.5, -20, 20), -24, 38, 420, 85)
    render(macro, "knob_macro", (1600, 1000), {"node_audio_filter"})

    kc = camera("cam_kit", (KIT[0], KIT[1] - 10, 0), -10, 46, 1080, 60)
    render(kc, "kit_components", (1600, 1100), {"kit_components"})

    lc = camera("cam_logo", (LOGO[0], LOGO[1], 30), -14, 30, 900, 70)
    render(lc, "logo_3d", (1600, 900), {"logo"})
    tile_obs = [o for o in bpy.data.collections["logo"].objects if o.name.startswith(("tile", "lemniscate_flat"))]
    for o in tile_obs:
        o.hide_render = True
    render_cutout(lc, "logo_3d_cutout", (1600, 900), {"logo"}, fl)
    for o in tile_obs:
        o.hide_render = False
    tc = camera("cam_tile", (LOGO[0], LOGO[1] - 520, 10), -18, 48, 1150, 70)
    mark3d = bpy.data.objects["lemniscate"]
    mark3d.hide_render = True
    render(tc, "logo_tile", (1200, 1200), {"logo"})
    mark3d.hide_render = False

    # the patch: LFO out -> Field Pixel in, a Modulation-colour cable between the side-wall jacks
    LX, FX = -1900, -1900 + 125 + 230 + 170
    lfo, (LW, LH), ljy = build_lfo(MT, (LX, 0))
    fp, (FW, FH), fjy = build_field_pixel(MT, (FX, 0))
    pc = new_coll("patch")
    plug = patch_cable(pc, MT, (LX + LW / 2 + 1.2, ljy, TOP / 2), (FX - FW / 2 - 1.2, fjy, TOP / 2), MT["mod"])
    for nm, cl, (cx, w, h) in (("node_lfo", lfo, (LX, LW, LH)), ("node_field_pixel", fp, (FX, FW, FH))):
        cam = camera("cam_" + nm, (cx - 10, -10, 20), -yaw, pitch, 1380 * max(w, h) / max(W, H) * 1.05, 50)
        render_cutout(cam, nm + "_cutout", (1600, 1200), {nm}, fl)
        tcam = camera("cam_top_" + nm, (cx, 0, 0), 0, 90, 3000, ortho=max(w, h) * 1.14)
        render(tcam, nm + "_top", (1000, 1000), {nm})
    pcam = camera("cam_patch", ((LX + FX) / 2, -20, 20), -yaw * 0.6, pitch, 1700, 50)
    render(pcam, "patch_hero", (1920, 1080), {"node_lfo", "node_field_pixel", "patch"})
    render_cutout(pcam, "patch_cutout", (1920, 1080), {"node_lfo", "node_field_pixel", "patch"}, fl)

    bc = camera("cam_ball", (BALL[0], BALL[1], 60), -20, 18, 700, 85)
    render(bc, "brand_ball", (1000, 1000), {"ball"})
    render_cutout(bc, "brand_ball_cutout", (1000, 1000), {"ball"}, fl)
    if ONLY:
        return

    for c in bpy.data.collections:
        c.hide_render = False
    animate(node, lfo, fp, plug)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "kit.blend"), compress=True)
    # glTF last: exporting re-parents into a metre-scaled root
    export(node, "node_audio_filter", (0, 0))
    export(lfo, "node_lfo", (LX, 0))
    export(fp, "node_field_pixel", (FX, 0))
    export([lfo, fp, pc], "patch_lfo_field_pixel", ((LX + FX) / 2, 0))
    export(kit, "controls_kit", KIT)
    export(ball, "brand_ball", BALL)
    logo_flat = [o for o in logo.objects if o.name.startswith(("tile", "lemniscate_flat"))]
    tile_c = bpy.data.collections.new("logo_tile")
    bpy.context.scene.collection.children.link(tile_c)
    for o in logo_flat:
        logo.objects.unlink(o)
        tile_c.objects.link(o)
    export(logo, "logo_3d", LOGO)
    export(tile_c, "logo_tile", (LOGO[0], LOGO[1] - 520))
    # studio.blend: the stage alone (world, key/rim/fill suns, shadow-catcher floor, every camera) to drop .glb files into
    for c in list(bpy.data.collections):
        for o in list(c.objects):
            bpy.data.objects.remove(o)
        bpy.data.collections.remove(c)
    bpy.ops.outliner.orphans_purge(do_recursive=True)
    fl.is_shadow_catcher = True
    bpy.context.scene.camera = hero
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "studio.blend"), compress=True)


main()
