# The procedural table: one judge scene for the real-time engine (docs/plans/realtime-engine). A table top, a mug, a
# glass with water, a spoon and a folded cloth, all generated here (no assets): lathes from one profile each, a tube
# for the mug handle, a draped grid for the cloth. Blender Z-up metres, the table top at z = 0.
# stage: "clay" (one grey material), "base" (clean materials), "flaws" (imperfection layers, the final scene).
import math
import os

SEG = 96


def _round(profile, radius, n=6):
    """Fillet every inner corner of an (r, z) polyline with a quadratic arc of about `radius`."""
    out = [profile[0]]
    for a, b, c in zip(profile, profile[1:], profile[2:]):
        la, lc = math.dist(a, b), math.dist(b, c)
        r = min(radius, la * 0.45, lc * 0.45)
        p0 = (b[0] + (a[0] - b[0]) * r / la, b[1] + (a[1] - b[1]) * r / la)
        p1 = (b[0] + (c[0] - b[0]) * r / lc, b[1] + (c[1] - b[1]) * r / lc)
        for k in range(n + 1):
            t = k / n
            out.append(tuple((1 - t) ** 2 * p0[i] + 2 * t * (1 - t) * b[i] + t * t * p1[i] for i in (0, 1)))
    out.append(profile[-1])
    return out


def lathe(profile, seg=SEG):
    """Spin an (r, z) profile about Z. r = 0 points become poles. Outward faces when the profile runs bottom-axis,
    out, up, and back in. Returns verts, faces, per-corner uvs."""
    verts, rings, faces, uvs = [], [], [], []
    length = [0.0]
    for a, b in zip(profile, profile[1:]):
        length.append(length[-1] + math.dist(a, b))
    for r, z in profile:
        if r < 1e-7:
            rings.append([len(verts)]); verts.append([0.0, 0.0, z])
        else:
            rings.append(list(range(len(verts), len(verts) + seg)))
            verts += [[r * math.cos(2 * math.pi * j / seg), r * math.sin(2 * math.pi * j / seg), z] for j in range(seg)]
    for i in range(len(profile) - 1):
        a, b = rings[i], rings[i + 1]
        v0, v1 = length[i] / length[-1], length[i + 1] / length[-1]
        for j in range(seg):
            k = (j + 1) % seg
            u0, u1 = j / seg, (j + 1) / seg
            if len(a) == 1 and len(b) == 1:
                continue
            if len(a) == 1:
                faces.append([a[0], b[k], b[j]]); uvs += [[u0, v0], [u1, v1], [u0, v1]]
            elif len(b) == 1:
                faces.append([a[j], a[k], b[0]]); uvs += [[u0, v0], [u1, v0], [u0, v1]]
            else:
                faces.append([a[j], a[k], b[k], b[j]]); uvs += [[u0, v0], [u1, v0], [u1, v1], [u0, v1]]
    return verts, faces, uvs


def tube(path, rx, ry, seg=24):
    """A tube along a polyline in the XZ plane, cross-section an ellipse (rx in the plane, ry along Y), open ends."""
    verts, faces, uvs = [], [], []
    n = len(path)
    for i, (x, z) in enumerate(path):
        ax, az = path[min(i + 1, n - 1)][0] - path[max(i - 1, 0)][0], path[min(i + 1, n - 1)][1] - path[max(i - 1, 0)][1]
        l = math.hypot(ax, az)
        nx, nz = -az / l, ax / l   # in-plane normal
        for j in range(seg):
            a = 2 * math.pi * j / seg
            verts.append([x + nx * rx * math.cos(a), ry * math.sin(a), z + nz * rx * math.cos(a)])
    for i in range(n - 1):
        for j in range(seg):
            k = (j + 1) % seg
            faces.append([i * seg + j, i * seg + k, (i + 1) * seg + k, (i + 1) * seg + j])
            uvs += [[j / seg, i / (n - 1)], [(j + 1) / seg, i / (n - 1)], [(j + 1) / seg, (i + 1) / (n - 1)], [j / seg, (i + 1) / (n - 1)]]
    return verts, faces, uvs


def join(*parts):
    verts, faces, uvs = [], [], []
    for v, f, u in parts:
        faces += [[i + len(verts) for i in face] for face in f]
        verts += v; uvs += u
    return verts, faces, uvs


def place(mesh, loc=(0, 0, 0), rot_z=0.0, scale=(1, 1, 1), rot_y=0.0):
    """scale, then rotate about Y, then about Z (degrees), then translate."""
    v, f, u = mesh
    cy, sy, cz, sz = (math.cos(math.radians(rot_y)), math.sin(math.radians(rot_y)),
                      math.cos(math.radians(rot_z)), math.sin(math.radians(rot_z)))
    out = []
    for x, y, z in v:
        x, y, z = x * scale[0], y * scale[1], z * scale[2]
        x, z = x * cy + z * sy, -x * sy + z * cy
        x, y = x * cz - y * sz, x * sz + y * cz
        out.append([round(x + loc[0], 6), round(y + loc[1], 6), round(z + loc[2], 6)])
    return out, f, u


def _obj(mesh, material):
    v, f, u = mesh
    return {"shape": "mesh", "verts": v, "faces": f, "uvs": u, "material": material}


# ---- shapes ----------------------------------------------------------------------------------------------------------
MUG_AT, MUG_R, MUG_H = (-0.075, 0.045), 0.042, 0.094
GLASS_AT = (0.105, 0.095)
SPOON_AT, SPOON_ROT = (0.02, -0.15), 14.0
CLOTH_AT, CLOTH_ROT = (-0.235, -0.12), 9.0


def mug():
    body = lathe(_round([(0, 0.0015), (0.034, 0.0015), (0.036, 0.0), (0.039, 0.0), (MUG_R, 0.006), (MUG_R, MUG_H),
                         (MUG_R - 0.0042, MUG_H), (MUG_R - 0.0042, 0.009), (0, 0.007)], 0.0021))
    arc = [(MUG_R - 0.002 + 0.027 * math.cos(math.radians(a)) * 0.82, 0.050 + 0.027 * math.sin(math.radians(a)))
           for a in range(-104, 105, 8)]
    handle = tube(arc, 0.0052, 0.0075)
    return place(join(body, handle), (MUG_AT[0], MUG_AT[1], 0), rot_z=-48)


def _glass_inner(z):
    return 0.0282 + (0.0338 - 0.0282) * (z - 0.012) / 0.118


def glass():
    return place(lathe(_round([(0, 0.0), (0.0295, 0.0), (0.0355, 0.13), (0.0338, 0.13), (0.0282, 0.012), (0, 0.011)], 0.0018)),
                 (GLASS_AT[0], GLASS_AT[1], 0))


def water():
    g, top = 0.0002, 0.084
    prof = [(0, 0.0115), (_glass_inner(0.0125) - g - 0.0006, 0.0122), (_glass_inner(top) - g, top - 0.0012),
            (_glass_inner(top) - g - 0.0006, top), (0, top - 0.0004)]
    return place(lathe(_round(prof, 0.0008)), (GLASS_AT[0], GLASS_AT[1], 0))


def spoon():
    R, d, t = 0.019, 0.0085, 0.0011
    outer = [(R * math.sin(math.radians(a)), -d * math.cos(math.radians(a))) for a in range(0, 91, 6)]
    inner = [((R - t) * math.sin(math.radians(a)), -(d - t) * math.cos(math.radians(a))) for a in range(84, -1, -6)]
    rim = [(R - t * 0.15, t * 0.45), (R - t * 0.85, t * 0.45)]
    bowl = place(lathe(outer + rim + inner), (0, 0, d), scale=(1.42, 1.0, 1.0))
    L = 0.125   # handle: a lathe along its own axis, flattened, wider toward the end
    prof = [(0, 0)] + [(0.0036 + 0.0042 * (k / 14) ** 1.6, L * k / 14) for k in range(1, 14)] + [(0.0062, L * 0.985), (0.0045, L), (0, L)]
    handle = place(lathe(_round(prof, 0.002), 32), (R * 1.42 - 0.004, 0, d - 0.0006), scale=(0.30, 1.0, 1.0), rot_y=92.4)
    return place(join(bowl, handle), (SPOON_AT[0], SPOON_AT[1], 0), rot_z=SPOON_ROT)


def cloth(nx=150, ny=120, w=0.25, h=0.19):
    """A napkin folded once: the far half lies doubled, soft wrinkles, the border rolls down to the table."""
    def s(a, b, x):
        t = min(max((x - a) / (b - a), 0.0), 1.0)
        return t * t * (3 - 2 * t)
    verts, faces, uvs = [], [], []
    for j in range(ny + 1):
        for i in range(nx + 1):
            u, v = i / nx, j / ny
            edge = min(s(0, 0.035, u), s(0, 0.035, 1 - u), s(0, 0.045, v), s(0, 0.045, 1 - v))
            fold = s(0.43, 0.50, v + 0.03 * math.sin(u * 5.0))
            z = 0.0035 + 0.0045 * fold
            z += 0.0013 * math.sin(u * 13.0 + 2.5 * v) * math.sin(v * 9.0 + 1.3) * (0.4 + 0.6 * fold)
            z += 0.0012 * math.sin(u * 31.0 - v * 17.0) * math.cos(v * 23.0 + u * 5.0)
            z += 0.0040 * math.exp(-((u - 0.62) ** 2 * 60 + (v - 0.3) ** 2 * 25))
            verts.append([(u - 0.5) * w, (v - 0.5) * h, 0.0006 + z * edge])
    for j in range(ny):
        for i in range(nx):
            a = j * (nx + 1) + i
            faces.append([a, a + 1, a + nx + 2, a + nx + 1])
            uvs += [[i / nx, j / ny], [(i + 1) / nx, j / ny], [(i + 1) / nx, (j + 1) / ny], [i / nx, (j + 1) / ny]]
    return place((verts, faces, uvs), (CLOTH_AT[0], CLOTH_AT[1], 0), rot_z=CLOTH_ROT)


DROPS = [(0.150, 0.045, 0.0042), (0.163, 0.062, 0.0026), (0.139, 0.030, 0.0019), (0.171, 0.041, 0.0031),
         (0.128, 0.048, 0.0015), (0.157, 0.026, 0.0022)]


def droplets():
    parts = []
    for x, y, r in DROPS:
        prof = [(r * math.sin(math.radians(a)), r * 0.42 * math.cos(math.radians(a))) for a in range(90, -1, -10)]
        parts.append(place(lathe([(0, 0.0)] + prof, 24), (x, y, 0.00003)))
    return join(*parts)


# ---- materials -------------------------------------------------------------------------------------------------------
CLAY = {"base_color": [0.55, 0.55, 0.55], "roughness": 0.6}


def materials(stage):
    if stage == "clay":
        return {k: CLAY for k in ("table", "wall", "floor", "card", "mug", "glass", "water", "spoon", "cloth", "drops")}
    import judge_table_fields as F
    flaws = stage == "flaws"
    m = {
        "wall": {"base_color": [0.62, 0.60, 0.56], "roughness": 0.85},
        "floor": {"base_color": [0.16, 0.12, 0.09], "roughness": 0.6},
        "card": {"base_color": [0.85, 0.62, 0.42], "roughness": 0.9},
        "table": {"roughness": 0.4, "code": F.table(flaws) + F.TAIL},
        "mug": {"roughness": 0.3, "coat": 1.0, "coat_roughness": 0.04 if not flaws else 0.07, "code": F.mug(flaws) + F.TAIL},
        "glass": {"base_color": [1, 1, 1], "roughness": 0.0, "transmission": 1.0, "ior": 1.5, "code": F.glass(flaws) + F.TAIL},
        "water": {"base_color": [1, 1, 1], "roughness": 0.0, "transmission": 1.0, "ior": 1.33},
        # refractive beads resting on the table render black in settle (AUTORUN defect): wet-paint beads under a clear coat
        "drops": {"base_color": [0.045, 0.10, 0.10], "roughness": 0.03, "coat": 1.0, "coat_roughness": 0.0, "ior": 1.33},
        "spoon": {"metallic": 1.0, "roughness": 0.1, "code": F.spoon(flaws) + F.TAIL},
        "cloth": {"roughness": 0.92, "sheen": 0.6, "sheen_roughness": 0.45, "code": F.cloth(flaws) + F.TAIL},
    }
    if flaws:
        m["table"]["layer"] = {"code": F.table_dust() + F.TAIL, "mask_code": F.TABLE_DUST_MASK}
        m["mug"]["layer"] = {"code": F.mug_stain() + F.TAIL, "mask_code": F.MUG_STAIN_MASK}
    return m


# Camera-projected maps of the Field materials (judge_render.py bake), so Cycles renders the same surfaces.
PROJ = os.path.expanduser("~/.cache/infinite-sweep/judge/proj")
BAKED = {"table": (0, True), "mug": (4, False), "glass": (5, False), "spoon": (7, False), "cloth": (8, True)}   # object, bump


def judge_table(stage="flaws", size=(640, 480), samples=256, proj=True):
    m = materials(stage)
    if proj and stage == "flaws" and all(os.path.exists(os.path.join(PROJ, k + "_rmh.png")) for k in BAKED):
        import judge_table_fields as F
        for k, (_, bump) in BAKED.items():
            m[k]["proj"] = {"albedo": os.path.join(PROJ, k + "_albedo.png"), "rmh": os.path.join(PROJ, k + "_rmh.png"),
                            "bump": F.HEIGHT_K if bump else 0}
    objs = [
        {"shape": "cube", "scale": [0.62, 0.41, 0.016], "location": [0, 0.17, -0.016], "bevel": 0.0035, "material": m["table"]},
        {"shape": "cube", "scale": [2.5, 0.02, 1.4], "location": [0, 1.25, 0.2], "material": m["wall"]},
        {"shape": "plane", "scale": 3, "location": [0, 0, -0.74], "material": m["floor"]},
        # the warm bounce card: a matt board standing right of the table, facing the window
        {"shape": "cube", "scale": [0.008, 0.36, 0.26], "location": [0.56, 0.02, 0.27], "bevel": 0.002, "material": m["card"]},
        _obj(mug(), m["mug"]), _obj(glass(), m["glass"]), _obj(water(), m["water"]),
        _obj(spoon(), m["spoon"]), _obj(cloth(), m["cloth"]), _obj(droplets(), m["drops"]),
    ]
    # 50 mm on a 36 x 24 frame: 27 degrees vertical. The harness measures f-stop on a 36 mm vertical span (a 75 mm
    # lens at this angle), so f/4.2 there is the 17.9 mm aperture of a real 50 mm at f/2.8. Seated eye: 0.42 m above
    # the table top. Focus on the mug's near rim.
    cam = {"fov": 27.0, "azimuth": 94, "elevation": 29, "distance": 0.88, "target": [-0.07, -0.01, 0.045],
           "fstop": 4.2, "focus": 0.87, "blades": 7}
    return {"size": list(size), "samples": samples, "view": "AgX", "camera": cam,
            # C1: one large area light as the window (left, behind the camera line), the physical sky, the card above.
            "lights": [{"type": "area", "power": 20, "azimuth": 205, "elevation": 34, "distance": 1.25, "size": [0.9, 1.1],
                        "color": [1.0, 0.97, 0.92]}],
            "world": {"sky": {"elevation": 38, "azimuth": 200, "turbidity": 3.0}, "strength": 0.08},
            "objects": objs}
