# The lily of docs/plans/flower-film: writes art/lily/lily.inf (edit this script, not the patch).
#   python3 art/lily/gen_lily.py [--probe]
# Modelled the Blender way with Infinite's own nodes: each tepal is a Plane cage whose points Edit Mesh moves
# into the petal shape (edit mode, point by point), then Subdivide (Catmull-Clark) smooths it, Material shades it.
# Infinite is Y up; the flower's axis is +Y, a tepal at rest points along +Z, its inner face up/in.
import argparse, math, os

HERE = os.path.dirname(os.path.abspath(__file__))


class Patch:
    def __init__(self):
        self.nodes, self.wires, self.n = [], [], 0

    def node(self, cat, typ, id_, *lines):
        self.n += 1
        body = ["node %d %s %s" % (self.n, cat, typ), "  id %s" % id_] + ["  " + l for l in lines] + ["end"]
        self.nodes.append("\n".join(body))
        return id_

    def wire(self, line):
        self.wires.append(line)

    def text(self):
        return "infinite-patch 1\n" + "\n".join(self.nodes) + "\n" + "\n".join(self.wires) + "\n"


def smooth(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def tepal_point(s, u, k):
    """Where the cage point at (s along 0..1, u across -1..1) of tepal kind k sits, in metres."""
    L, W = k["length"], k["width"]
    # Centreline: leaves the base steep (the cup), opens past flat, the tip recurves down.
    th0, th1 = math.radians(k["base_deg"]), math.radians(k["tip_deg"])
    n = 40
    cy, cz, ty, tz = 0.0, k["ring"], 1.0, 0.0
    for j in range(n):
        sj = (j + 0.5) / n * s
        th = th0 + (th1 - th0) * sj ** k["open_pow"]
        cy += math.cos(th) * L * s / n
        cz += math.sin(th) * L * s / n
    th = th0 + (th1 - th0) * s ** k["open_pow"]
    ty, tz = math.cos(th), math.sin(th)
    ny, nz = tz, -ty                                   # inner normal: T x X
    # Width: a narrow claw at the base, widest a little before the middle, a pointed tip.
    half = W * max(math.sin(math.pi * s ** 0.72), 0.0) ** 0.85 + 0.0025 * (1 - s) ** 3
    x = u * half
    # Cross-section along the inner normal: cupped low down, edges rolled back further out,
    # the nectar furrow along the midrib, and the ruffled rim of the inner tepals.
    cup = k["cup"] * (0.55 - s) * u * u * half / W
    furrow = -k["furrow"] * math.exp(-(u / 0.13) ** 2) * (1 - smooth(0.5, 0.95, s))
    ruffle = k["ruffle"] * math.sin(2 * math.pi * k["waves"] * s + k["phase"]) * abs(u) ** 3 * smooth(0.2, 0.45, s) * (1 - 0.6 * s)
    h = cup + furrow + ruffle
    # Each tepal twists a little along its length and drifts to one side: no two are mirror images.
    a = math.radians(k.get("twist", 0.0)) * s ** 1.5
    x, h = x * math.cos(a) - h * math.sin(a), x * math.sin(a) + h * math.cos(a)
    x += k.get("bend", 0.0) * s * s
    return (x, cy + ny * h, cz + nz * h)


INNER = {"length": 0.095, "width": 0.028, "ring": 0.003, "base_deg": 18, "tip_deg": 138, "open_pow": 1.25,
         "cup": 0.010, "furrow": 0.0016, "ruffle": 0.0036, "waves": 5.5, "phase": 0.4}
OUTER = dict(INNER, length=0.1, width=0.02, ruffle=0.0006, waves=4.0, base_deg=20, tip_deg=142, cup=0.008)

D = 20   # cage: Plane detail, (D+1) x (D+1) points


def cage_ops(k, scale=1.0):
    """Edit Mesh ops: lift the whole grid out of the way, then move every point to its place on the tepal."""
    ops = ["sel all", "move 0 0 10"]
    for j in range(D + 1):
        for i in range(D + 1):
            gx, gy = -0.5 + i / D, -0.5 + j / D
            x, y, z = (v * scale for v in tepal_point(j / D, 2.0 * i / D - 1.0, k))
            ops.append("sel v %.5f %.5f 10" % (gx, gy))
            ops.append("move %.6f %.6f %.6f" % (x - gx, y - gy, z - 10.0))
    return "\\n".join(ops)


def rot_to(t):
    """Edit Mesh rotate args turning +Y onto unit vector t: axis (Y x t), angle."""
    ax = (t[2], 0.0, -t[0])
    l = math.sqrt(ax[0] ** 2 + ax[2] ** 2)
    ang = math.degrees(math.acos(max(-1.0, min(1.0, t[1]))))
    if l < 1e-6:
        return None
    return "rotate %.5f 0 %.5f %.4f" % (ax[0] / l, ax[2] / l, ang)


def tube_ops(path, radius, sides, rings):
    """Edit Mesh ops bending a Cylinder cage (sides, rings) along path(s)->(P,T) with radius(s) in metres.
    Ring by ring: select it (lifted out of the way), scale to the radius, turn onto the tangent, move into place."""
    ops = ["sel all", "move 0 0 10"]
    for i in range(rings + 1):
        y = 0.5 - i / rings
        pts = []
        for j in range(sides):
            th = 2 * math.pi * j / sides
            pts += [0.5 * math.cos(th), y, 0.5 * math.sin(th) + 10]
        if i in (0, rings):
            pts += [0, y, 10]
        P, T = path(i / rings)
        r = radius(i / rings)
        ops.append("sel v " + " ".join("%.5f" % v for v in pts))
        ops.append("scale %.6f 1 %.6f" % (2 * r, 2 * r))
        ops.append("move 0 %.6f 0" % -y)          # ring centre to y=0 so the turn is about the origin side
        rt = rot_to(T)
        if rt:
            ops.append(rt)
        ops.append("move %.6f %.6f %.6f" % (P[0], P[1], P[2] - 10))
    return "\\n".join(ops)


def bent_path(base, length, a0, a1, az, power=1.0, n=40):
    """Centreline leaving base, its angle from +Y going a0 -> a1 (degrees), leaning toward azimuth az (deg about Y, 0 = +Z)."""
    ca, sa = math.cos(math.radians(az)), math.sin(math.radians(az))

    def f(s):
        y, r = base[1], 0.0
        for j in range(n):
            sj = (j + 0.5) / n * s
            th = math.radians(a0 + (a1 - a0) * sj ** power)
            y += math.cos(th) * length * s / n
            r += math.sin(th) * length * s / n
        th = math.radians(a0 + (a1 - a0) * s ** power)
        P = (base[0] + r * sa, y, base[2] + r * ca)
        T = (math.sin(th) * sa, math.cos(th), math.sin(th) * ca)
        return P, T
    return f


def blob_ops(scale, tangent, at):
    """A Sphere (radius 0.5) scaled to an ellipsoid, its long Y axis turned onto tangent, moved to at."""
    ops = ["sel all", "scale %.6f %.6f %.6f" % scale]
    rt = rot_to(tangent)
    if rt:
        ops.append(rt)
    ops.append("move %.6f %.6f %.6f" % at)
    return "\\n".join(ops)


def norm(v):
    l = math.sqrt(sum(x * x for x in v))
    return tuple(x / l for x in v)


# The tepal's painted colour map, by the cage's UV: x across (0.5 = midrib), y along (0 = base).
# Deep blue at the midrib, lighter blue out, a wavy white rim, a green-white throat, dark speckles thick at
# the base and sparse at the tip, fine lengthwise veins.
TEPAL_MAP = "; ".join([
    "a = abs(uv.x - 0.5) * 2.0",
    "s = uv.y",
    "w = 0.035 * sin(s * 19.0) + 0.02 * sin(s * 37.0 + 1.3)",
    "streak = 0.09 * sin(uv.x * 95.0 + 2.0 * sin(s * 7.0)) * sin(uv.x * 23.0 + 1.7)", "rim = smoothstep(0.52 + w, 0.86 + w, a + streak + 0.3 * smoothstep(0.7, 1.0, s))",
    "blue = mix(vec3(0.13, 0.16, 0.72), vec3(0.40, 0.46, 0.95), smoothstep(0.0, 0.75, a))",
    "blue = mix(blue, vec3(0.30, 0.34, 0.86), smoothstep(0.6, 1.0, s) * 0.5)",
    "c = mix(blue, vec3(0.94, 0.95, 1.0), rim)",
    "c = mix(vec3(0.82, 0.92, 0.80), c, smoothstep(0.03, 0.16, s))",
    "vein = 0.95 + 0.05 * sin(uv.x * 160.0 + 3.0 * sin(s * 9.0))",
    "c = c * mix(1.0, vein, 1.0 - rim)",
    "g = vec2(uv.x * 17.0, s * 26.0)",
    "cell = floor(g)",
    "h1 = fract(sin(cell.x * 127.1 + cell.y * 311.7) * 43758.5453)",
    "h2 = fract(sin(cell.x * 269.5 + cell.y * 183.3) * 43758.5453)",
    "h3 = fract(sin(cell.x * 419.2 + cell.y * 371.9) * 43758.5453)",
    "d = length(g - cell - vec2(0.2 + 0.6 * h1, 0.2 + 0.6 * h2))",
    "keep = step(h3, 0.75 - 0.75 * smoothstep(0.1, 0.85, s)) * (1.0 - smoothstep(0.55, 0.72, a)) * smoothstep(0.08, 0.15, s)",
    "dot1 = (1.0 - smoothstep(0.11 + 0.14 * h1, 0.19 + 0.14 * h1, d)) * keep",
    "c = mix(c, vec3(0.04, 0.04, 0.22), dot1 * 0.9)",
    "col = c",
])

TEPAL_MAT = ("c color 1 1 1", "f metallic 0", "f roughness 0.45", "f specular 0.4", "f translucency 0.6")


def build(probe, view=(90, 70, 0.72)):
    import random
    rnd = random.Random(7)
    p = Patch()

    def mat(id_, src, *lines):
        p.node("3D", "Material", id_, *lines)
        p.wire("geo %s 0 %s" % (id_, src))
        return id_

    def group(id_, srcs):
        p.node("3D", "Group 3D", id_)
        for i, s_ in enumerate(srcs):
            p.wire("geo %s geo_%s %s" % (id_, "abcdefgh"[i], s_))
        return id_

    p.node("Source", "FieldPixel", "tepalMap", "f width 1024", "f height 2048", "b animate 0", "s code " + TEPAL_MAP)
    # Six tepals: inner three at 0/120/240 deg, outer three between them, a little lower.
    tepals = []
    for n_ in range(6):
        outer = n_ % 2 == 1
        k = dict(OUTER if outer else INNER)
        k["phase"] = rnd.uniform(0, 6.28)
        k["width"] *= rnd.uniform(0.93, 1.07)
        k["length"] *= rnd.uniform(0.95, 1.04)
        k["tip_deg"] += rnd.uniform(-8, 8)
        k["base_deg"] += rnd.uniform(-3, 3)
        az = n_ * 60 + rnd.uniform(-4, 4)
        c, e, sb, t = "cage%d" % n_, "edit%d" % n_, "sub%d" % n_, "tepal%d" % n_
        p.node("3D", "Plane", c, "i detail %d" % D)
        p.node("3D", "Edit Mesh", e, "s ops " + cage_ops(k))
        p.node("3D", "Subdivide", sb, "i levels 2", "f smooth 1")
        p.node("3D", "Transform", t, "f rotY %.3f" % ((az + 180) % 360 - 180), "f offsetX 0", "f offsetY %g" % (-0.003 if outer else 0.0))
        p.wire("geo %s 0 %s" % (e, c)); p.wire("geo %s 0 %s" % (sb, e)); p.wire("geo %s 0 %s" % (t, sb))
        tepals.append(mat("tepalMat%d" % n_, t, *TEPAL_MAT))
        p.wire("cable tepalMat%d albedo tepalMap" % n_)
    tep_mat = group("tepals", tepals)

    # Six stamens: a filament tube curving out and up, the anther balanced across its tip (T shape).
    TS, TR = 10, 12
    fils, anths = [], []
    for n_ in range(6):
        az = n_ * 60 + 30 + rnd.uniform(-10, 10)
        path = bent_path((0, 0.004, 0), 0.072 * rnd.uniform(0.95, 1.05), 38 + rnd.uniform(-5, 5),
                         12 + rnd.uniform(-6, 6), az, 1.4)
        c, e = "filCyl%d" % n_, "fil%d" % n_
        p.node("3D", "Cylinder", c, "i detail %d" % (TR * 8), "i sides %d" % TS)
        p.node("3D", "Edit Mesh", e, "s ops " + tube_ops(path, lambda s: 0.00075 - 0.00035 * s, TS, TR))
        p.wire("geo %s 0 %s" % (e, c))
        fils.append(e)
        P, T = path(1.0)
        tang = norm((math.cos(math.radians(az + rnd.uniform(60, 120))), rnd.uniform(-0.25, 0.25),
                     -math.sin(math.radians(az + rnd.uniform(60, 120)))))
        sc, ae = "anthSph%d" % n_, "anth%d" % n_
        p.node("3D", "Sphere", sc, "i detail 16", "i sides 8")
        p.node("3D", "Edit Mesh", ae, "s ops " + blob_ops((0.0038, 0.013, 0.0026), tang, (P[0], P[1] + 0.001, P[2])))
        p.wire("geo %s 0 %s" % (ae, sc))
        anths.append(ae)
    fil_mat = mat("filMat", group("filaments", fils), "c color 0.78 0.84 0.92", "f metallic 0", "f roughness 0.35",
                  "f specular 0.45", "f translucency 0.4")
    anth_mat = mat("anthMat", group("anthers", anths), "c color 0.06 0.07 0.22", "f metallic 0", "f roughness 0.9",
                   "f specular 0.2", "f sheen 0.6", "c sheenColor 0.35 0.4 0.8")

    # Pistil: green ovary, a longer style leaning slightly, the three-lobed wet stigma.
    p.node("3D", "Sphere", "ovSph", "i detail 24", "i sides 12")
    p.node("3D", "Edit Mesh", "ovary", "s ops " + blob_ops((0.0065, 0.022, 0.0065), (0, 1, 0), (0, 0.011, 0)))
    p.wire("geo ovary 0 ovSph")
    spath = bent_path((0, 0.02, 0), 0.085, 6, 24, 90, 1.6)
    p.node("3D", "Cylinder", "styCyl", "i detail %d" % (TR * 8), "i sides %d" % TS)
    p.node("3D", "Edit Mesh", "style", "s ops " + tube_ops(spath, lambda s: 0.0011 + 0.0004 * s, TS, TR))
    p.wire("geo style 0 styCyl")
    P, T = spath(1.0)
    lobes = []
    for n_ in range(3):
        a = math.radians(n_ * 120 + 20)
        off = (0.0016 * math.cos(a), 0.0, 0.0016 * math.sin(a))
        sc, le = "stigSph%d" % n_, "stig%d" % n_
        p.node("3D", "Sphere", sc, "i detail 12", "i sides 8")
        p.node("3D", "Edit Mesh", le, "s ops " + blob_ops((0.0032, 0.0042, 0.0032), T,
                                                           (P[0] + off[0], P[1] + off[1] + 0.0012, P[2] + off[2])))
        p.wire("geo %s 0 %s" % (le, sc))
        lobes.append(le)
    stig_mat = mat("stigMat", group("stigma", lobes), "c color 0.12 0.14 0.32", "f metallic 0", "f roughness 0.08",
                   "f specular 0.5", "f clearcoat 1", "f clearcoatRoughness 0.03")
    sty_mat = mat("styMat", "style", "c color 0.72 0.8 0.86", "f metallic 0", "f roughness 0.2", "f specular 0.5",
                  "f translucency 0.35")
    ov_mat = mat("ovMat", "ovary", "c color 0.35 0.5 0.25", "f metallic 0", "f roughness 0.4", "f specular 0.4",
                 "f subsurface 0.3", "c subsurfaceColor 0.4 0.7 0.2")
    # Stem: down and back from the flower.
    stpath = bent_path((0, 0.002, 0), 0.25, 180, 165, 90, 1.0)
    p.node("3D", "Cylinder", "stemCyl", "i detail %d" % (12 * 8), "i sides 12")
    p.node("3D", "Edit Mesh", "stem", "s ops " + tube_ops(stpath, lambda s: 0.0032 + 0.0006 * s, 12, 12))
    p.wire("geo stem 0 stemCyl")
    stem_mat = mat("stemMat", "stem", "c color 0.22 0.36 0.14", "f metallic 0", "f roughness 0.35", "f specular 0.45",
                   "f clearcoat 0.2", "f subsurface 0.3", "c subsurfaceColor 0.4 0.7 0.2")
    centre = group("centre", [fil_mat, anth_mat, sty_mat, stig_mat, ov_mat, stem_mat])

    p.node("3D", "Camera", "cam", "f fov 13.7", "f distance %g" % view[2], "f azimuth %g" % view[0],
           "f elevation %g" % view[1], "f targetX 0", "f targetY 0.035", "f targetZ 0.01")
    p.node("3D", "Light", "sun", "i type Directional", "f azimuth -120", "f elevation 25", "c color 1 0.93 0.82",
           "f intensity 5", "i shadowRes 1")
    p.node("3D", "Light", "fill", "i type Area", "f azimuth 60", "f elevation 55", "f distance 1.2",
           "c color 0.9 0.95 1", "f intensity 0.6", "f areaWidth 1", "f areaHeight 1", "b normalize 1",
           "b softFalloff 0", "f falloff 1", "f shadowRadius 0.5", "i shadowRes 1")
    p.node("3D", "Light", "kick", "i type Area", "f azimuth 160", "f elevation 40", "f distance 0.8",
           "c color 0.9 0.95 1", "f intensity 2", "f areaWidth 0.15", "f areaHeight 0.15", "b normalize 1",
           "b softFalloff 0", "f falloff 1", "f shadowRadius 0.05", "i shadowRes 1")
    w, h = (640, 480) if probe else (1920, 1440)
    fov, fstop, focus = 13.7, 5.6, view[2] - 0.03
    mm = 36.0 / (2.0 * math.tan(math.radians(fov) * 0.5))
    dof = mm * mm / (fstop * (focus * 1000.0 - mm)) / 36.0 * 45.0
    p.node("3D", "Render 3D", "render", "f width %d" % w, "f height %d" % h, "i tonemap AgX", "f exposure 1",
           "b shadows 1", "f ao 0", "c envSky 0.004 0.004 0.005", "c envHorizon 0.004 0.004 0.005",
           "c envGround 0.004 0.004 0.005", "c bg 0 0 0", "f rim 0", "f lightIntensity 0", "c ambient 0 0 0",
           "i ptSamples %d" % (64 if probe else 256), "i ptBounces 8", "i supersample 1",
           "f dofAperture %.4f" % min(dof, 4.0), "f focusDistance %.3f" % focus, "i dofBlades 6")
    p.node("Utility", "Output", "out")
    p.wire("geo render geo_a %s" % tep_mat)
    p.wire("geo render geo_b %s" % centre)
    p.wire("geo render camera cam")
    p.wire("geo render light_1 sun")
    p.wire("geo render light_2 fill")
    p.wire("geo render light_3 kick")
    p.wire("cable out 0 render")
    return p.text()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", action="store_true")
    ap.add_argument("--view", default="90,70,0.72")
    ap.add_argument("--out", default=os.path.join(HERE, "lily.inf"))
    a = ap.parse_args()
    open(a.out, "w").write(build(a.probe, tuple(float(x) for x in a.view.split(","))))
    print(a.out)
