# The lily lying on a glazed tile in a dusty room, one morning sun shaft through a window slot, dew on the petals
# and the tile (the shoe test's room, set up for a flower). Writes art/lily/tile.inf; edit this script, not the patch.
#   python3 art/lily/gen_tile.py [--probe] [--view az,el,dist] [--size WxH] [--spp N] [--out path]
# The flower is gen_lily's, every part turned by one tilt (rotX) so it rests on its front tepals and cut stem.
import argparse, math, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_lily as G

TILT = 32.0
BIG = 3.0           # tepal sheets are built 3x size so Solidify (min 1 mm) and dew density reach true scale         # degrees the flower's face leans toward +Z (the camera side)

# Petal relief (normal input, grayscale height): lengthwise veins that gather to the base, raised papillae where
# the spots are and a thicker field of small ones in the throat, and a fine cell grain over all of it.
TEPAL_BUMP = "; ".join([
    "a = abs(uv.x - 0.5) * 2.0",
    "s = uv.y",
    "vx = (uv.x - 0.5) / (0.25 + 0.75 * sqrt(max(s, 0.02)))",
    "vein = pow(abs(sin(vx * 70.0 + 0.8 * sin(s * 11.0))), 6.0) * 0.5 + pow(abs(sin(vx * 18.0)), 12.0) * 0.5",
    "mid = exp(-pow(a / 0.06, 2.0)) * (1.0 - smoothstep(0.5, 0.95, s))",
    "g = vec2(uv.x * 17.0, s * 26.0)",
    "cell = floor(g)",
    "h1 = fract(sin(cell.x * 127.1 + cell.y * 311.7) * 43758.5453)",
    "h2 = fract(sin(cell.x * 269.5 + cell.y * 183.3) * 43758.5453)",
    "h3 = fract(sin(cell.x * 419.2 + cell.y * 371.9) * 43758.5453)",
    "d = length(g - cell - vec2(0.2 + 0.6 * h1, 0.2 + 0.6 * h2))",
    "keep = step(h3, 0.75 - 0.75 * smoothstep(0.1, 0.85, s)) * (1.0 - smoothstep(0.55, 0.72, a)) * smoothstep(0.08, 0.15, s)",
    "pap = (1.0 - smoothstep(0.0, 0.22 + 0.14 * h1, d)) * keep",
    "g2 = vec2(uv.x * 60.0, s * 90.0)",
    "c2 = floor(g2)",
    "k1 = fract(sin(c2.x * 91.3 + c2.y * 47.9) * 43758.5453)",
    "d2 = length(g2 - c2 - vec2(0.5, 0.5))",
    "pap2 = (1.0 - smoothstep(0.0, 0.45, d2)) * step(k1, 0.6) * (1.0 - smoothstep(0.08, 0.3, s)) * (1.0 - smoothstep(0.4, 0.7, a))",
    "g3 = vec2(uv.x * 420.0, s * 700.0)",
    "c3 = floor(g3)",
    "grain = fract(sin(c3.x * 12.9898 + c3.y * 78.233) * 43758.5453)",
    "hgt = 0.5 + 0.1 * vein + 0.1 * mid + 0.35 * pap + 0.22 * pap2 + 0.02 * (grain - 0.5)",
    "col = vec3(hgt, hgt, hgt)",
])

# One glazed tile (repeated by Mapping): warm off-white glaze pooling slightly darker toward the edges, a faint
# wave in the glaze, a fine craze network, recessed grout.
# Ref 1's colour: deep cobalt that fades a little toward the margin, a narrow crisp white rim, navy spots.
TEPAL_MAP = (G.TEPAL_MAP
             # The rim is not an outline: thin near the base, wider and wobbling toward the tip, feathered into
             # the blue by streaks that run along the veins (ref 1).
             .replace("w = 0.035 * sin(s * 19.0) + 0.02 * sin(s * 37.0 + 1.3)",
                      "w = 0.04 * sin(s * 6.0 + 1.1) + 0.02 * sin(s * 17.0 + 0.4) + 0.08 * (1.0 - smoothstep(0.1, 0.5, s))")
             .replace("streak = 0.09 * sin(uv.x * 95.0 + 2.0 * sin(s * 7.0)) * sin(uv.x * 23.0 + 1.7)",
                      "streak = 0.12 * pow(abs(sin(uv.x * 70.0 + 3.0 * sin(s * 11.0))), 3.0) * sin(uv.x * 19.0 + s * 5.0)")
             .replace("smoothstep(0.52 + w, 0.86 + w, a + streak + 0.3 * smoothstep(0.7, 1.0, s))",
                      "smoothstep(0.84 + w, 0.93 + w, a + streak + 0.3 * smoothstep(0.65, 1.0, s))")
             .replace("vec3(0.94, 0.95, 1.0), rim", "vec3(0.97, 0.98, 1.0), rim")
             .replace("vec3(0.13, 0.16, 0.72), vec3(0.40, 0.46, 0.95)", "vec3(0.05, 0.07, 0.62), vec3(0.20, 0.26, 0.92)")
             .replace("vec3(0.30, 0.34, 0.86), smoothstep(0.6, 1.0, s) * 0.5", "vec3(0.16, 0.20, 0.80), smoothstep(0.6, 1.0, s) * 0.4")
             .replace("vec3(0.04, 0.04, 0.22), dot1 * 0.9", "vec3(0.02, 0.02, 0.16), dot1 * 0.95")
             .replace("vein = 0.95 + 0.05 * sin(uv.x * 160.0 + 3.0 * sin(s * 9.0))", "vein = 0.975 + 0.025 * sin(uv.x * (130.0 + 40.0 * s) + 5.0 * sin(s * 9.0 + uv.x * 7.0))"))
# Shape: more recurve at the tip, a deeper midrib furrow, a stronger ruffle on the margin.
SHAPE = {"tip_deg": 10.0, "furrow": 1.8, "ruffle": 1.4}
OUTER_RUFFLE = 0.0018

TILE_A = "; ".join([
    "e = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y))",
    "grout = 1.0 - smoothstep(0.008, 0.014, e)",
    "pool = smoothstep(0.012, 0.09, e)",
    "n = 0.5 + 0.25 * sin(uv.x * 9.0 + 2.0 * sin(uv.y * 7.0)) * sin(uv.y * 8.0 + 1.3) + 0.25 * sin(uv.x * 23.0 + uv.y * 17.0)",
    "glaze = mix(vec3(0.80, 0.79, 0.76), vec3(0.93, 0.92, 0.89), pool * (0.85 + 0.15 * n))",
    "q = vec2(uv.x * 9.0 + 0.3 * sin(uv.y * 13.0), uv.y * 9.0 + 0.3 * sin(uv.x * 11.0))",
    "fq = fract(q)",
    "craze = 1.0 - smoothstep(0.0, 0.02, min(min(fq.x, 1.0 - fq.x), min(fq.y, 1.0 - fq.y)))",
    "glaze = glaze * (1.0 - 0.06 * craze)",
    "col = mix(glaze, vec3(0.30, 0.29, 0.27), grout)",
])
TILE_H = "; ".join([
    "e = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y))",
    "edge = smoothstep(0.008, 0.03, e)",
    "wave = 0.04 * sin(uv.x * 6.0 + 1.1) * sin(uv.y * 5.0 + 0.4) + 0.02 * sin(uv.x * 19.0 + uv.y * 13.0)",
    "q = vec2(uv.x * 9.0 + 0.3 * sin(uv.y * 13.0), uv.y * 9.0 + 0.3 * sin(uv.x * 11.0))",
    "fq = fract(q)",
    "craze = 1.0 - smoothstep(0.0, 0.015, min(min(fq.x, 1.0 - fq.x), min(fq.y, 1.0 - fq.y)))",
    "h = 0.3 + 0.55 * edge + wave * edge - 0.03 * craze",
    "col = vec3(h, h, h)",
])

TILES_ACROSS = 4      # the floor is 0.8 m: 0.2 m tiles
FLOOR = 0.8
H = 0.5               # room height above the tile
WIN_Y = (0.25, 0.41)  # window slot in the +X wall, heights above the tile
WIN_Z = (-0.12, 0.16)
SUN, SKY, FOG = 12.0, 0.6, 0.25   # sun past the slider cap of 5 (values in a file are not clamped)


def rot_x(p, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return (p[0], c * p[1] - s * p[2], s * p[1] + c * p[2])


def rot_y(p, deg):
    # Infinite's RotationY: x' = c x + s z, z' = -s x + c z (column-major m[0]=c, m[2]=-s)
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return (c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2])


def tiled(k):   # Mapping hides FieldPixel textures in settle, so the kernel repeats itself
    return "u = fract(uv * %.1f); " % TILES_ACROSS + k.replace("uv.", "u.")


WATER = ("c color 1 1 1", "f metallic 0", "f roughness 0.02", "f specular 0.5", "f ior 1.33", "f transmission 1",
         "f transmissionRoughness 0")


def build(probe, view, size, spp):
    rnd = random.Random(7)
    p = G.Patch()

    def mat(id_, src, *lines):
        p.node("3D", "Material", id_, *lines)
        p.wire("geo %s 0 %s" % (id_, src))
        return id_

    def group(id_, srcs):
        p.node("3D", "Group 3D", id_)
        for i, s_ in enumerate(srcs):
            p.wire("geo %s geo_%s %s" % (id_, "abcdefgh"[i], s_))
        return id_

    def tilt(id_, src):
        p.node("3D", "Transform", id_, "f rotX %g" % TILT, "f offsetX 0")
        p.wire("geo %s 0 %s" % (id_, src))
        return id_

    low = []   # world points of the tilted flower, to sit the tile under it

    # Tepals: Plane cage -> Edit Mesh -> Subdivide -> Solidify (0.35 mm) -> Transform (place + tilt) -> Material.
    p.node("Source", "FieldPixel", "tepalMap", "f width 1024", "f height 2048", "b animate 0", "s code " + (TEPAL_MAP if not os.environ.get("LILY_RED") else os.environ.get("LILY_RED")))
    p.node("Source", "FieldPixel", "tepalBump", "f width 1024", "f height 2048", "b animate 0", "s code " + TEPAL_BUMP)
    tepals, dew_src = [], []
    for n_ in range(6):
        outer = n_ % 2 == 1
        k = dict(G.OUTER if outer else G.INNER)
        k["phase"] = rnd.uniform(0, 6.28)
        if outer:
            k["ruffle"] = OUTER_RUFFLE
        k["tip_deg"] += SHAPE["tip_deg"]; k["furrow"] *= SHAPE["furrow"]; k["ruffle"] *= SHAPE["ruffle"]
        k["width"] *= rnd.uniform(0.95, 1.12)
        k["length"] *= rnd.uniform(0.95, 1.05)
        k["tip_deg"] += rnd.uniform(-10, 14)
        k["base_deg"] += rnd.uniform(-3, 4)
        k["twist"] = rnd.uniform(-22, 22)
        k["bend"] = rnd.uniform(-0.008, 0.008)
        az = n_ * 60 + rnd.uniform(-6, 6)
        ry = (az + 180) % 360 - 180
        oy = -0.003 if outer else 0.0
        c, e, sb, so, t = "cage%d" % n_, "edit%d" % n_, "sub%d" % n_, "solid%d" % n_, "tepal%d" % n_
        p.node("3D", "Plane", c, "i detail %d" % G.D)
        p.node("3D", "Edit Mesh", e, "s ops " + G.cage_ops(k, BIG))
        p.node("3D", "Subdivide", sb, "i levels 2", "f smooth 1")
        p.node("3D", "Solidify", so, "f thickness 0.001")
        place = ("f scaleX %g" % (1 / BIG), "f scaleY %g" % (1 / BIG), "f scaleZ %g" % (1 / BIG), "f rotY %.3f" % ry, "f rotX %g" % TILT, "f offsetX 0",
                 "f offsetY %g" % (oy * math.cos(math.radians(TILT))), "f offsetZ %g" % (oy * math.sin(math.radians(TILT))))
        p.node("3D", "Transform", t, *place)
        p.wire("geo %s 0 %s" % (e, c)); p.wire("geo %s 0 %s" % (sb, e)); p.wire("geo %s 0 %s" % (so, sb))
        p.wire("geo %s 0 %s" % (t, sb if os.environ.get("LILY_NO_SOLID") else so))
        tepals.append(mat("tepalMat%d" % n_, t, "c color 1 1 1", "f metallic 0", "f roughness 0.28", "f specular 0.6",
                          "f translucency 0.3", "f sheen 0.15", "c sheenColor 1 1 1", "f sheenRoughness 0.25",
                          "b normalBump 1", "f normalStrength 1.0"))
        p.wire("cable tepalMat%d albedo tepalMap" % n_)
        p.wire("cable tepalMat%d normal tepalBump" % n_)
        # The dew: points scattered on the unsolidified sheet while it is still big (more per petal than the
        # density control's 500 / m^2 allows at true size), then placed the same way.
        dp, ds = "dewPts%d" % n_, "dewSrc%d" % n_
        p.node("3D", "Distribute Points on Faces", dp, "f density 500", "i method 1", "f minDistance %g" % (0.007 * BIG),
               "f seed %d" % (3 + n_))
        p.node("3D", "Transform", ds, *place)
        pv = "dewVerts%d" % n_
        p.node("3D", "Points to Vertices", pv)        # Distribute's cloud as vertices, so a Transform can place it
        p.wire("geo %s 0 %s" % (dp, sb)); p.wire("geo %s 0 %s" % (pv, dp)); p.wire("geo %s 0 %s" % (ds, pv))
        dew_src.append(ds)
        for j in range(0, 41, 2):
            for u in (-1.0, -0.5, 0.0, 0.5, 1.0):
                q = G.tepal_point(j / 40, u, k)
                q = (q[0], q[1] + oy, q[2])
                low.append(rot_x(rot_y(q, ry), TILT))
    tep = group("tepals", tepals)

    # Stamens and pistil, as gen_lily, each tilted before its material.
    TS, TR = 10, 12
    fils, anths = [], []
    for n_ in range(6):
        az = n_ * 60 + 30 + rnd.uniform(-10, 10)
        path = G.bent_path((0, 0.004, 0), 0.072 * rnd.uniform(0.95, 1.05), 38 + rnd.uniform(-5, 5),
                           12 + rnd.uniform(-6, 6), az, 1.4)
        c, e = "filCyl%d" % n_, "fil%d" % n_
        p.node("3D", "Cylinder", c, "i detail %d" % (TR * 8), "i sides %d" % TS)
        p.node("3D", "Edit Mesh", e, "s ops " + G.tube_ops(path, lambda s: 0.00075 - 0.00035 * s, TS, TR))
        p.wire("geo %s 0 %s" % (e, c))
        fils.append(e)
        P, T = path(1.0)
        a1 = math.radians(az + rnd.uniform(60, 120))
        tang = G.norm((math.cos(a1), rnd.uniform(-0.25, 0.25), -math.sin(a1)))
        sc, ae = "anthSph%d" % n_, "anth%d" % n_
        p.node("3D", "Sphere", sc, "i detail 16", "i sides 8")
        p.node("3D", "Edit Mesh", ae, "s ops " + G.blob_ops((0.0046, 0.0155, 0.0032), tang, (P[0], P[1] + 0.001, P[2])))
        p.wire("geo %s 0 %s" % (ae, sc))
        anths.append(ae)
    fil_mat = mat("filMat", tilt("filTilt", group("filaments", fils)), "c color 0.78 0.93 0.95", "f metallic 0",
                  "f roughness 0.22", "f specular 0.55", "f translucency 0.65", "f clearcoat 0.4")
    anth_mat = mat("anthMat", tilt("anthTilt", group("anthers", anths)), "c color 0.10 0.11 0.40", "f metallic 0",
                   "f roughness 0.95", "f specular 0.15", "f sheen 1", "c sheenColor 0.55 0.55 1", "f sheenRoughness 0.6")
    p.node("3D", "Sphere", "ovSph", "i detail 24", "i sides 12")
    p.node("3D", "Edit Mesh", "ovary", "s ops " + G.blob_ops((0.0065, 0.022, 0.0065), (0, 1, 0), (0, 0.011, 0)))
    p.wire("geo ovary 0 ovSph")
    spath = G.bent_path((0, 0.02, 0), 0.085, 6, 24, 90, 1.6)
    p.node("3D", "Cylinder", "styCyl", "i detail %d" % (TR * 8), "i sides %d" % TS)
    p.node("3D", "Edit Mesh", "style", "s ops " + G.tube_ops(spath, lambda s: 0.0008 + 0.0003 * s, TS, TR))
    p.wire("geo style 0 styCyl")
    P, T = spath(1.0)
    lobes = []
    for n_ in range(3):
        a = math.radians(n_ * 120 + 20)
        sc, le = "stigSph%d" % n_, "stig%d" % n_
        p.node("3D", "Sphere", sc, "i detail 12", "i sides 8")
        p.node("3D", "Edit Mesh", le, "s ops " + G.blob_ops((0.0032, 0.0042, 0.0032), T,
                                                             (P[0] + 0.0016 * math.cos(a), P[1] + 0.0012, P[2] + 0.0016 * math.sin(a))))
        p.wire("geo %s 0 %s" % (le, sc))
        lobes.append(le)
    stig_mat = mat("stigMat", tilt("stigTilt", group("stigma", lobes)), "c color 0.14 0.16 0.34", "f metallic 0",
                   "f roughness 0.08", "f specular 0.5", "f clearcoat 1", "f clearcoatRoughness 0.03")
    sty_mat = mat("styMat", tilt("styTilt", "style"), "c color 0.76 0.92 0.94", "f metallic 0", "f roughness 0.2",
                  "f specular 0.5", "f translucency 0.35")
    ov_mat = mat("ovMat", tilt("ovTilt", "ovary"), "c color 0.35 0.5 0.25", "f metallic 0", "f roughness 0.4",
                 "f specular 0.4", "f subsurface 0.3", "c subsurfaceColor 0.4 0.7 0.2")

    # The tile sits under the lowest tepal point; the cut stem leaves the flower's base backwards (-Z) and settles
    # onto the tile, built straight in world space.
    ty = min(q[1] for q in low) - 0.0004
    b0 = rot_x((0, -0.004, 0), TILT)
    r0, r1 = 0.0034, 0.0036
    L = 0.2

    def stem_path(s):
        def at(s_):
            z = b0[2] - L * s_
            y = b0[1] + (ty + r1 - b0[1]) * G.smooth(0.0, 0.5, s_)
            return (0.004 * math.sin(s_ * 3.0), y, z)
        P0, P1 = at(max(s - 0.01, 0)), at(min(s + 0.01, 1))
        return at(s), G.norm(tuple(b - a for a, b in zip(P0, P1)))
    p.node("3D", "Cylinder", "stemCyl", "i detail %d" % (12 * 8), "i sides 14")
    p.node("3D", "Edit Mesh", "stem", "s ops " + G.tube_ops(stem_path, lambda s: r0 + (r1 - r0) * s, 14, 12))
    p.wire("geo stem 0 stemCyl")
    stem_mat = mat("stemMat", "stem", "c color 0.24 0.38 0.15", "f metallic 0", "f roughness 0.32", "f specular 0.45",
                   "f clearcoat 0.25", "f subsurface 0.3", "c subsurfaceColor 0.4 0.7 0.2")
    centre = group("centre", [fil_mat, anth_mat, sty_mat, stig_mat, ov_mat, stem_mat])

    # The room: tiled floor, plaster walls and ceiling, a window slot in the +X wall.
    p.node("Source", "FieldPixel", "tileMap", "f width 2048", "f height 2048", "b animate 0", "s code " + tiled(TILE_A))
    p.node("Source", "FieldPixel", "tileBump", "f width 2048", "f height 2048", "b animate 0", "s code " + tiled(TILE_H))
    p.node("3D", "Plane", "floor", "i detail 2", "f scale %g" % FLOOR, "f rotX -1.5708", "f posY %g" % ty)   # Plane is XY; its rot is radians
    floor_mat = mat("floorMat", "floor", "c color 1 1 1", "f metallic 0", "f roughness 0.5", "f specular 0.5",
                    "f clearcoat 1", "f clearcoatRoughness 0.03", "b normalBump 1", "f normalStrength 1.0")
    p.wire("cable floorMat albedo tileMap")
    p.wire("cable floorMat normal tileBump")
    hf, t_ = FLOOR / 2, 0.02
    boxes = [  # (centre, size)
        ((0, ty + H + t_ / 2, 0), (FLOOR, t_, FLOOR)),                                   # ceiling
        ((-hf - t_ / 2, ty + H / 2, 0), (t_, H, FLOOR)),                                 # -X wall
        ((0, ty + H / 2, -hf - t_ / 2), (FLOOR, H, t_)),                                 # -Z wall
        ((0, ty + H / 2, hf + t_ / 2), (FLOOR, H, t_)),                                  # +Z wall
        ((hf + t_ / 2, ty + WIN_Y[0] / 2, 0), (t_, WIN_Y[0], FLOOR + 2 * t_)),           # +X below the slot
        ((hf + t_ / 2, ty + (WIN_Y[1] + H) / 2, 0), (t_, H - WIN_Y[1], FLOOR + 2 * t_)),  # +X above it
        ((hf + t_ / 2, ty + sum(WIN_Y) / 2, (-hf + WIN_Z[0]) / 2), (t_, WIN_Y[1] - WIN_Y[0], WIN_Z[0] + hf)),  # beside
        ((hf + t_ / 2, ty + sum(WIN_Y) / 2, (hf + WIN_Z[1]) / 2), (t_, WIN_Y[1] - WIN_Y[0], hf - WIN_Z[1])),
    ]
    walls = []
    for i, (c, s_) in enumerate(boxes):
        walls.append(p.node("3D", "Cube", "wall%d" % i, "f posX %g" % c[0], "f posY %g" % c[1], "f posZ %g" % c[2],
                            "f scaleX %g" % s_[0], "f scaleY %g" % s_[1], "f scaleZ %g" % s_[2]))
    wall_mat = mat("wallMat", group("walls", walls), "c color 0.82 0.81 0.78", "f metallic 0", "f roughness 0.92",
                   "f specular 0.3")
    room = group("room", [floor_mat, wall_mat])

    # Dew: beads on the petals (near spheres, the petal is waxy), flatter drops on the glaze around the flower.
    p.node("3D", "Group 3D", "dewPts")
    for i, s_ in enumerate(dew_src):
        p.wire("geo dewPts geo_%s %s" % ("abcdefgh"[i], s_))
    p.node("3D", "Sphere", "bead", "i detail 24", "i sides 24", "f scale 0.3", "f scaleY 0.85")
    p.node("3D", "Instance on Points", "petalDew", "i pointMode 0", "i maxPoints 4000", "f instanceScale 0.01",
           "f scaleRandom 0.8", "f rotationRandom 0", "b alignToNormal 1", "f normalOffset 0.0012", "f seed 5",
           "b inheritMaterial 0", *WATER)
    p.wire("geo petalDew points dewPts"); p.wire("geo petalDew shape bead")
    p.node("3D", "Plane", "dewPatch", "i detail 2", "f scale 0.34", "f rotX -1.5708", "f posY %g" % ty)
    p.node("3D", "Distribute Points on Faces", "tileScatter", "f density 300", "i method 1", "f minDistance 0.008",
           "f seed 9")
    p.node("3D", "Points to Vertices", "tilePts")
    p.wire("geo tileScatter 0 dewPatch"); p.wire("geo tilePts 0 tileScatter")
    p.node("3D", "Sphere", "drop", "i detail 24", "i sides 24", "f scale 0.5", "f posY -0.16")
    p.node("3D", "Instance on Points", "tileDew", "i pointMode 0", "i maxPoints 4000", "f instanceScale 0.01",
           "f scaleRandom 0.75", "f rotationRandom 0", "b alignToNormal 1", "f normalOffset 0", "f seed 11",
           "b inheritMaterial 0", *WATER)
    p.wire("geo tileDew points tilePts"); p.wire("geo tileDew shape drop")
    dew = group("dew", ["petalDew", "tileDew"])

    # Camera: low macro look across the flower toward the window, the shaft crossing the frame.
    tgt = rot_x((0, 0.03, 0.012), TILT)
    p.node("3D", "Camera", "cam", "f fov %g" % view[3], "f distance %g" % view[2], "f azimuth %g" % view[0],
           "f elevation %g" % view[1], "f targetX %g" % tgt[0], "f targetY %g" % tgt[1], "f targetZ %g" % tgt[2])
    p.node("3D", "Light", "sun", "i type Directional", "f azimuth 0", "f elevation 31", "c color 1 0.92 0.8",
           "f intensity %g" % SUN, "i shadowRes 1")
    w, h = size
    fstop, focus = 8.0, view[2]
    mm = 36.0 / (2.0 * math.tan(math.radians(view[3]) * 0.5))
    dof = mm * mm / (fstop * (focus * 1000.0 - mm)) / 36.0 * 45.0
    p.node("3D", "Render 3D", "render", "f width %d" % w, "f height %d" % h, "i tonemap AgX", "f exposure %g" % view[4],
           "b shadows 1", "i shadowQuality 1", "f shadowStrength 1", "f ao 0",
           "c envSky 0.55 0.68 0.95", "c envHorizon 0.75 0.78 0.82", "c envGround 0.3 0.28 0.25", "f envIntensity %g" % SKY,
           "c bg 0.03 0.03 0.035", "f rim 0", "f lightIntensity 0", "c ambient 0 0 0",
           "f fogDensity %g" % FOG, "c fogColor 1 1 1", "f fogAnisotropy 0.75",
           "f fogMinX %g" % -hf, "f fogMinY %g" % ty, "f fogMinZ %g" % -hf,
           "f fogMaxX %g" % hf, "f fogMaxY %g" % (ty + H), "f fogMaxZ %g" % hf,
           "i ptSamples %d" % spp, "i ptBounces 8", "i supersample 1",
           "f dofAperture %.4f" % (dof if not os.environ.get("LILY_NO_DOF") else 0), "f focusDistance %.3f" % focus, "i dofBlades 6")
    p.node("Utility", "Output", "out")
    p.wire("geo render geo_a %s" % tep)
    p.wire("geo render geo_b %s" % centre)
    p.wire("geo render geo_c %s" % room)
    p.wire("geo render geo_d %s" % dew)
    p.wire("geo render camera cam")
    p.wire("geo render light_1 sun")
    p.wire("cable out 0 render")
    return p.text()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", action="store_true")
    ap.add_argument("--view", default="125,24,0.40,26,1")   # azimuth, elevation, distance, fov, exposure
    ap.add_argument("--size", default="")
    ap.add_argument("--spp", type=int, default=0)
    ap.add_argument("--out", default=os.path.join(HERE, "tile.inf"))
    a = ap.parse_args()
    size = tuple(int(x) for x in a.size.split("x")) if a.size else ((640, 480) if a.probe else (1920, 1440))
    spp = a.spp or (64 if a.probe else 256)
    open(a.out, "w").write(build(a.probe, tuple(float(x) for x in a.view.split(",")), size, spp))
    import subprocess   # lay the nodes out left to right so the patch opens tidy in the app
    subprocess.run([sys.executable, os.path.join(HERE, "..", "..", "tools", "patch-layout.py"), a.out],
                   check=True, stdout=subprocess.DEVNULL)
    print(a.out)
