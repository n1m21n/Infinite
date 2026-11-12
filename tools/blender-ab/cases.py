"""Picture cases for harness.py: one scene each, read the same way by Blender
(blender_scene.py) and Infinite (harness.to_patch). Format in README.md.
Colours are linear, positions are Blender's Z-up metres, lights and the
camera sit on azimuth / elevation / distance around their target."""

GREY_FLOOR = {"shape": "plane", "scale": 6, "material": {"base_color": [0.5, 0.5, 0.5], "roughness": 0.6}}
CAMERA = {"fov": 40, "azimuth": 50, "elevation": 25, "distance": 4.5, "target": [0, 0, 0.4]}


def _ball(x=0.0, y=0.0, r=0.5, **mat):
    return {"shape": "sphere", "scale": r, "location": [x, y, r], "material": mat}


def _lit(light, world=None, view="Standard"):
    return {"camera": CAMERA, "lights": [light], "world": world or {"color": [0, 0, 0]}, "view": view,
            "objects": [GREY_FLOOR, _ball(base_color=[0.8, 0.8, 0.8], roughness=0.4)]}


def _material_rows(field=False):
    """field=True: each ball's values come from a Field material program (OPUS_PLAN row 1.4); the harness sets
    the Material knobs to a decoy, so the picture matches `materials` only if the program overrides them."""
    objs = [GREY_FLOOR]
    f = {"field": True} if field else {}
    for i, rough in enumerate([0.05, 0.25, 0.5, 0.75, 1.0]):
        x = (i - 2) * 0.9
        objs.append(_ball(x, -0.5, 0.35, base_color=[0.8, 0.1, 0.08], roughness=rough, **f))
        objs.append(_ball(x, 0.5, 0.35, base_color=[1.0, 0.77, 0.34], metallic=1.0, roughness=rough, **f))
    return {"camera": {"fov": 40, "azimuth": 90, "elevation": 30, "distance": 6.0, "target": [0, 0, 0.3]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 60, "elevation": 50}],
            "world": {"color": [0.3, 0.3, 0.3]}, "objects": objs}


def _furnace(metallic):
    objs = [_ball((i - 2) * 0.9, 0.0, 0.35, base_color=[1.0, 1.0, 1.0], metallic=metallic, roughness=r)
            for i, r in enumerate([0.0, 0.25, 0.5, 0.75, 1.0])]
    return {"camera": {"fov": 40, "azimuth": 90, "elevation": 0, "distance": 6.0, "target": [0, 0, 0.35]},
            "lights": [], "world": {"color": [0.5, 0.5, 0.5]}, "view": "Standard", "objects": objs}  # 0.5, not 1.0: a white world clips at 255 in the PNG and hides energy gain


def _lineup():
    specs = [
        {"base_color": [1.0, 0.77, 0.34], "metallic": 1.0, "roughness": 0.3},
        {"base_color": [0.8, 0.1, 0.08], "roughness": 0.7},
        {"base_color": [0.8, 0.1, 0.08], "roughness": 0.3, "coat": 1.0, "coat_roughness": 0.03},
        {"base_color": [0.35, 0.06, 0.25], "roughness": 0.8, "sheen": 1.0},
        {"base_color": [1.0, 1.0, 1.0], "transmission": 1.0, "ior": 1.5, "roughness": 0.3},
    ]
    objs = [GREY_FLOOR] + [_ball((i - 2) * 0.9, 0.0, 0.35, **m) for i, m in enumerate(specs)]
    return {"camera": {"fov": 40, "azimuth": 90, "elevation": 25, "distance": 6.0, "target": [0, 0, 0.35]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 60, "elevation": 50}],
            "world": {"color": [0.3, 0.3, 0.3]}, "objects": objs}


def _cornell():
    white = {"base_color": [0.8, 0.8, 0.8], "roughness": 0.8}
    red = {"base_color": [0.8, 0.08, 0.08], "roughness": 0.8}
    green = {"base_color": [0.08, 0.8, 0.08], "roughness": 0.8}
    objs = [
        {"shape": "cube", "scale": [1.0, 1.0, 0.02], "location": [0.0, 0.0, -0.02], "material": white},
        {"shape": "cube", "scale": [1.0, 1.0, 0.02], "location": [0.0, 0.0, 2.02], "material": white},
        {"shape": "cube", "scale": [1.0, 0.02, 1.0], "location": [0.0, 1.02, 1.0], "material": white},
        {"shape": "cube", "scale": [0.02, 1.0, 1.0], "location": [-1.02, 0.0, 1.0], "material": red},
        {"shape": "cube", "scale": [0.02, 1.0, 1.0], "location": [1.02, 0.0, 1.0], "material": green},
        _ball(-0.42, 0.2, 0.35, base_color=[0.9, 0.9, 0.9], metallic=1.0, roughness=0.15),
        _ball(0.42, -0.15, 0.35, base_color=[0.85, 0.85, 0.85], roughness=0.7),
    ]
    return {"camera": {"fov": 40, "azimuth": 90, "elevation": 0, "distance": 3.8, "target": [0, 0, 1.0]},
            "lights": [{"type": "area", "power": 40, "azimuth": 90, "elevation": 90, "distance": 1.95, "size": [0.5, 0.5]}],
            "world": {"color": [0.0, 0.0, 0.0]}, "objects": objs}


# Region names in mask order: index 1 is "room", 2 "models" and so on; index 0 (no object hit) is "sky".
REGIONS = ["room", "models", "glass", "coat", "velvet", "fox", "instances", "emissive"]
# The skinned Fox: the clip is Run (3) and the three frames sit at these shares of its length.
FOX_CLIP, FOX_PHASES = 3, [0.10, 0.45, 0.80]


def _scatter(n=50, seed=7):
    """n floor positions for the small spheres: deterministic, clear of the big objects and each other."""
    import random
    rng, keep_out = random.Random(seed), [(-1.5, 0.6, 0.75), (1.9, 0.9, 0.55), (0.2, 0.7, 1.1),
                                          (-0.9, -0.8, 0.45), (0.0, -0.8, 0.45), (0.9, -0.8, 0.45)]
    pts = []
    while len(pts) < n:
        x, y = rng.uniform(-2.5, 2.5), rng.uniform(-1.7, 1.5)
        if all((x - a) ** 2 + (y - b) ** 2 > r * r for a, b, r in keep_out) and \
                all((x - q[0]) ** 2 + (y - q[1]) ** 2 > 0.14 ** 2 for q in pts):
            pts.append([round(x, 3), round(y, 3), 0.05])
    return pts


def _hero():
    white = {"base_color": [0.75, 0.75, 0.75], "roughness": 0.7}
    objs = [
        {"region": "room", "shape": "plane", "scale": 5, "material": white},
        {"region": "room", "shape": "cube", "scale": [5.0, 0.05, 1.7], "location": [0, 1.85, 1.7], "material": white},
        {"region": "room", "shape": "cube", "scale": [0.05, 5.0, 1.7], "location": [-3.0, 0, 1.7],
         "material": {"base_color": [0.75, 0.08, 0.06], "roughness": 0.7}},
        {"region": "emissive", "shape": "cube", "scale": [0.8, 0.02, 0.25], "location": [0.4, 1.78, 1.9],
         "material": {"base_color": [0.05, 0.05, 0.05], "emission": [1.0, 0.9, 0.75], "emission_strength": 8.0}},
        # Poly Haven models keep their own glTF material (albedo, normal, metal-rough maps).
        {"region": "models", "shape": "model", "asset": "Ottoman_01.glb", "location": [-1.5, 0.6, 0]},
        {"region": "models", "shape": "model", "asset": "ClassicNightstand_01.glb", "location": [1.9, 0.9, 0]},
        {"region": "glass", "shape": "sphere", "scale": 0.35, "location": [-0.9, -0.8, 0.35],
         "material": {"base_color": [1, 1, 1], "transmission": 1.0, "roughness": 0.0, "ior": 1.5}},
        {"region": "coat", "shape": "sphere", "scale": 0.35, "location": [0.0, -0.8, 0.35],
         "material": {"base_color": [0.8, 0.1, 0.08], "roughness": 0.3, "coat": 1.0, "coat_roughness": 0.03}},
        {"region": "velvet", "shape": "sphere", "scale": 0.35, "location": [0.9, -0.8, 0.35],
         "material": {"base_color": [0.35, 0.06, 0.25], "roughness": 0.8, "sheen": 1.0}},
        # Skinned, glTF material; animation frame set per frame below. Faces +Z of the file = toward the camera.
        {"region": "fox", "shape": "model", "asset": "Fox.glb", "env": "INFINITE_3DBENCH_FOX", "scale": 0.0095,
         "location": [0.2, 0.7, 0], "clip": FOX_CLIP},
        {"region": "instances", "shape": "sphere", "scale": 0.05, "location": [0, 0, 0], "instances": _scatter(),
         "material": {"base_color": [0.9, 0.55, 0.1], "metallic": 0.0, "roughness": 0.35}},
    ]
    return {
        "size": [768, 512], "samples": 192, "view": "AgX", "frames": [{"phase": p} for p in FOX_PHASES],
        "camera": {"fov": 45, "azimuth": 90, "elevation": 20, "distance": 6.0, "target": [0, 0.2, 0.5],
                   "fstop": 2.0, "focus": 6.2},
        # Render 3D has three light pins; the HDRI's sun is the fourth (Environment extracts it).
        "lights": [
            {"type": "point", "power": 250, "azimuth": 200, "elevation": 40, "distance": 3.0, "radius": 0.1},
            {"type": "spot", "power": 500, "azimuth": 330, "elevation": 55, "distance": 3.5, "spot_angle": 30,
             "blend": 0.3, "radius": 0.1},
            {"type": "area", "power": 300, "azimuth": 90, "elevation": 80, "distance": 3.0, "size": [1.0, 1.0]},
        ],
        "world": {"hdri": "kloofendal_43d_clear_puresky_1k.hdr", "strength": 1.0, "rotation": 0.0, "sun_threshold": 10.0},
        "objects": objs,
    }


def _deer():
    # A real scanned-style asset (the owner's ~/Downloads/Assets/deer.glb, not in git; DEER_GLB overrides):
    # 0.51 m tall in the file, spec-gloss material with diffuse + glossiness maps. Scaled to 1.2 m, feet on the floor.
    deer = {"region": "models", "shape": "model", "asset": "deer.glb", "env": "DEER_GLB", "scale": 2.344,
            "location": [0.0, 0.6, 0.6]}
    return {"size": [768, 512], "view": "AgX",
            "camera": {"fov": 40, "azimuth": 35, "elevation": 15, "distance": 3.6, "target": [0, 0, 0.6]},
            "lights": [{"type": "sun", "power": 4, "azimuth": 40, "elevation": 50},
                       {"type": "area", "power": 150, "azimuth": 220, "elevation": 30, "distance": 3.0, "size": [1.5, 1.5]}],
            "world": {"color": [0.25, 0.27, 0.3]}, "objects": [GREY_FLOOR, deer]}


CASES = {
    # One light each on a grey floor and a white ball: units, falloff, cone, shadow.
    "light_sun": _lit({"type": "sun", "power": 3, "azimuth": 30, "elevation": 45}),
    "light_point": _lit({"type": "point", "power": 300, "azimuth": 30, "elevation": 50, "distance": 3, "radius": 0.1}),
    "light_spot": _lit({"type": "spot", "power": 600, "azimuth": 30, "elevation": 60, "distance": 3,
                        "spot_angle": 25, "blend": 0.3, "radius": 0.1}),
    "light_area": _lit({"type": "area", "power": 300, "azimuth": 30, "elevation": 50, "distance": 3, "size": [1, 1]}),
    # No lamp: the world colour alone lights everything.
    "world_grey": {"camera": CAMERA, "lights": [], "world": {"color": [0.5, 0.5, 0.5]},
                   "objects": [GREY_FLOOR, _ball(-0.6, 0, 0.45, base_color=[0.8, 0.8, 0.8], roughness=0.5),
                               _ball(0.6, 0, 0.45, base_color=[0.95, 0.95, 0.95], metallic=1, roughness=0.2)]},
    # Principled: dielectric row and metal row, roughness rising left to right.
    "materials": _material_rows(),
    # The same scene, every ball's base / metallic / roughness written by a Field material program over decoy knobs.
    # Gate (row 1.4): FLIP vs `materials` on the same engine under 0.01 (`sweep.py pair materials mat_field_knobs`).
    "mat_field_knobs": _material_rows(field=True),
    # The same sun scene through AgX on both sides.
    "view_agx": _lit({"type": "sun", "power": 6, "azimuth": 30, "elevation": 45}, view="AgX"),
    # White furnace energy conservation: 5 roughness steps on metal and dielectric in a (1,1,1) world.
    "furnace_metal": _furnace(1.0),
    "furnace_dielectric": _furnace(0.0),
    # Spec lobes side by side: gold, rough red plastic, clearcoat car paint, velvet sheen, frosted glass.
    "lineup": _lineup(),
    # 2x2x2 m Cornell box with red/green walls, ceiling area light, metal and white spheres.
    "cornell": _cornell(),
    # Every 3D aspect in one picture, 3 Fox frames, scored per region (harness.py hero_*).
    "hero": _hero(),
    # Owner-supplied deer: textured spec-gloss material, two lights, AgX. Skipped when the file is missing.
    "deer": _deer(),
}


# ---- Coverage cases: one feature each, the terms a 3D user expects to just work (README "Coverage"). ----
SMALL = [384, 384]
ROW_CAM = {"fov": 40, "azimuth": 90, "elevation": 20, "distance": 6.0, "target": [0, 0, 0.35]}
ROW_SUN = {"type": "sun", "power": 3, "azimuth": 60, "elevation": 45}


def _row(mats, cam=None, lights=None, world=None, extra=(), **kw):
    objs = [GREY_FLOOR] + [_ball((i - (len(mats) - 1) / 2) * 0.9, 0.0, 0.35, **m) for i, m in enumerate(mats)] + list(extra)
    sc = {"size": SMALL, "camera": cam or ROW_CAM, "lights": lights if lights is not None else [ROW_SUN],
          "world": world or {"color": [0.3, 0.3, 0.3]}, "objects": objs}
    sc.update(kw)
    return sc


def _stripes():
    """A wall of coloured bars behind the row, so refraction, blur and see-through have something to bend."""
    cols = [[0.8, 0.1, 0.08], [0.9, 0.8, 0.1], [0.1, 0.6, 0.15], [0.1, 0.25, 0.8], [0.9, 0.9, 0.9], [0.05, 0.05, 0.05]]
    return [{"shape": "cube", "scale": [0.2, 0.05, 1.2], "location": [-2.5 + k * 0.4, 1.2, 1.2],
             "material": {"base_color": cols[k % len(cols)], "roughness": 0.6}} for k in range(13)]


def _textured():
    # Maps are drawn by harness.make_texture into the case folder; the same PNG goes to Blender and Infinite.
    return {"size": SMALL, "camera": {"fov": 40, "azimuth": 90, "elevation": 30, "distance": 5.0, "target": [0, 0, 0.35]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 40, "elevation": 40}], "world": {"color": [0.3, 0.3, 0.3]},
            "objects": [
                {"shape": "plane", "scale": 3, "material": {"base_color": [1, 1, 1], "roughness": 0.6,
                                                            "maps": {"albedo": "checker"}}},
                _ball(-1.0, 0, 0.45, base_color=[1, 1, 1], roughness=0.5, maps={"albedo": "checker_fine"}),
                _ball(0.0, 0, 0.45, base_color=[0.9, 0.9, 0.9], metallic=1.0, roughness=1.0, maps={"roughness": "rough_bands"}),
                _ball(1.0, 0, 0.45, base_color=[0.8, 0.5, 0.3], roughness=0.4, maps={"normal": "bumps"}),
            ]}


def _shading(**plane):
    """Shading-gap cases: a plane seen from high up (tiling, breakup), a ball and a generated-triplanar cube."""
    return {"size": SMALL, "camera": {"fov": 40, "azimuth": 90, "elevation": 55, "distance": 5.0, "target": [0, 0, 0.3]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 40, "elevation": 35}], "world": {"color": [0.3, 0.3, 0.3]},
            "objects": [{"shape": "plane", "scale": 3, "material": {"base_color": [1, 1, 1], "roughness": 0.6} | plane}]}


def _detail():
    case = _shading(maps={"normal": "bumps"}, roughness=0.35, detail=[6.0, 1.0])
    case["objects"] += [
        _ball(-0.8, 0, 0.45, base_color=[0.8, 0.5, 0.3], roughness=0.35, maps={"normal": "bumps"}, detail=[6.0, 1.0]),
        {"shape": "cube", "scale": 0.4, "location": [0.8, 0, 0.4],
         "material": {"base_color": [1, 1, 1], "roughness": 0.5, "maps": {"albedo": "checker"},
                      "mapping": "generated", "triplanar": 0.3}}]
    return case


def _alpha():
    leaf = {"shape": "cube", "scale": [0.6, 0.01, 0.6], "location": [1.1, 0.0, 0.6],
            "material": {"base_color": [0.2, 0.6, 0.15], "roughness": 0.6, "alpha_cutoff": 0.5, "maps": {"opacity": "holes"}}}
    return _row([{"base_color": [0.8, 0.1, 0.08], "roughness": 0.4, "alpha": 0.5},
                 {"base_color": [0.1, 0.3, 0.8], "roughness": 0.4, "alpha": 0.2}], extra=[leaf] + _stripes())


def _emission_room():
    dark = {"base_color": [0.6, 0.6, 0.6], "roughness": 0.8}
    return {"size": SMALL, "camera": {"fov": 45, "azimuth": 90, "elevation": 15, "distance": 4.0, "target": [0, 0, 0.6]},
            "lights": [], "world": {"color": [0.0, 0.0, 0.0]},
            "objects": [GREY_FLOOR,
                        {"shape": "cube", "scale": [3.0, 0.05, 1.5], "location": [0, 1.5, 1.5], "material": dark},
                        {"shape": "cube", "scale": [0.6, 0.02, 0.2], "location": [-0.6, 1.4, 1.4],
                         "material": {"base_color": [0, 0, 0], "emission": [1.0, 0.85, 0.6], "emission_strength": 12.0}},
                        _ball(0.7, 0.2, 0.25, base_color=[0, 0, 0], emission=[0.2, 0.5, 1.0], emission_strength=10.0),
                        _ball(-0.3, -0.3, 0.4, base_color=[0.8, 0.8, 0.8], roughness=0.5),
                        _ball(0.6, -0.8, 0.3, base_color=[0.9, 0.9, 0.9], metallic=1.0, roughness=0.15)]}


def _interior():
    # A 4 x 4 x 2.6 m room, sun through a window in the left wall: almost every lit pixel is bounce light.
    wall = {"base_color": [0.75, 0.73, 0.7], "roughness": 0.8}
    floor = {"base_color": [0.45, 0.3, 0.2], "roughness": 0.5}
    W = 0.05
    objs = [{"shape": "cube", "scale": [2, 2, W], "location": [0, 0, -W], "material": floor},
            {"shape": "cube", "scale": [2, 2, W], "location": [0, 0, 2.6 + W], "material": wall},
            {"shape": "cube", "scale": [2, W, 1.3], "location": [0, 2 + W, 1.3], "material": wall},
            {"shape": "cube", "scale": [W, 2, 1.3], "location": [2 + W, 0, 1.3], "material": {"base_color": [0.2, 0.35, 0.6], "roughness": 0.8}},
            {"shape": "cube", "scale": [2, W, 1.3], "location": [0, -2 - W, 1.3], "material": wall},
            # left wall (x = -2) with a 1.2 x 1.2 m window: below, above, either side
            {"shape": "cube", "scale": [W, 2, 0.4], "location": [-2 - W, 0, 0.4], "material": wall},
            {"shape": "cube", "scale": [W, 2, 0.5], "location": [-2 - W, 0, 2.1], "material": wall},
            {"shape": "cube", "scale": [W, 0.7, 0.6], "location": [-2 - W, -1.3, 1.2], "material": wall},
            {"shape": "cube", "scale": [W, 0.7, 0.6], "location": [-2 - W, 1.3, 1.2], "material": wall},
            {"shape": "cube", "scale": [0.4, 0.4, 0.4], "location": [0.5, 0.6, 0.4], "material": {"base_color": [0.8, 0.8, 0.8], "roughness": 0.7}},
            _ball(-0.3, -0.4, 0.35, base_color=[0.8, 0.1, 0.08], roughness=0.4)]
    return {"size": SMALL, "samples": 256,
            "camera": {"fov": 60, "azimuth": 45, "elevation": 10, "distance": 2.4, "target": [0, 0.3, 0.8]},
            "lights": [{"type": "sun", "power": 6, "azimuth": 180, "elevation": 35}],
            "world": {"color": [0.25, 0.3, 0.4]}, "view": "AgX", "objects": objs}


def _monkey():
    return {"size": SMALL, "camera": {"fov": 35, "azimuth": 70, "elevation": 20, "distance": 4.5, "target": [0, 0, 0.6]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 20, "elevation": 40},
                       {"type": "area", "power": 100, "azimuth": 200, "elevation": 30, "distance": 3, "size": [1, 1]}],
            "world": {"color": [0.2, 0.2, 0.22]},
            "objects": [GREY_FLOOR,
                        {"shape": "monkey", "scale": 0.5, "location": [-0.4, 0, 0.6], "material": {"base_color": [0.8, 0.6, 0.4], "roughness": 0.35}},
                        {"shape": "cube", "scale": 0.3, "location": [0.8, 0.3, 0.3], "material": {"base_color": [0.3, 0.5, 0.8], "roughness": 0.5}}]}


def _wear_masks():
    # Row 4.4: curvature (left) and cavity (right) of the monkey head shown as emission; truth is Cycles' pointiness
    # and local AO at 5 % of the radius. Gate: pixel correlation over each head above 0.8 (wear_corr.py).
    blk = {"base_color": [0, 0, 0], "roughness": 1.0}
    return {"size": SMALL, "camera": {"fov": 35, "azimuth": 90, "elevation": 10, "distance": 4.2, "target": [0, 0, 0.6]},
            "lights": [], "world": {"color": [0, 0, 0]},
            "objects": [{"shape": "monkey", "scale": 0.6, "location": [0.65, 0, 0.6], "material": dict(blk, wear="curvature", gain=0.04)},
                        {"shape": "monkey", "scale": 0.6, "location": [-0.65, 0, 0.6], "material": dict(blk, wear="cavity")}]}


def _motion(kind):
    sc = _lineup()
    sc["size"] = SMALL
    if kind == "orbit":   # the camera swings 60 deg/s; scored at 0.5 s and 1 s, after the history has had to follow
        sc["motion"] = {"fps": 30, "camera_azimuth_rate": 60.0, "checks": [0.5, 1.0]}
    elif kind == "blur":  # one ball crosses at 9 m/s, a full-frame shutter: a 0.3 m smear at 5/30 s, mid-frame
        sc["objects"] = [GREY_FLOOR, _ball(-1.5, -0.6, 0.3, base_color=[0.9, 0.5, 0.1], roughness=0.4)] + sc["objects"][1:]
        sc["motion"] = {"fps": 30, "velocity": {"1": [9.0, 0.0, 0.0]}, "blur": 1.0, "checks": [5 / 30]}
    else:                 # nothing moves: any change between frames is flicker or noise
        sc["motion"] = {"fps": 30, "checks": [1.0], "flicker": True}
    return sc


CASES.update({
    # Material lobes, one per row, rising left to right.
    "mat_coat": _row([{"base_color": [0.8, 0.1, 0.08], "roughness": 0.6, "coat": 1.0, "coat_roughness": r} for r in (0.03, 0.15, 0.3, 0.5)]),
    "mat_sheen": _row([{"base_color": [0.35, 0.06, 0.25], "roughness": 0.9, "sheen": s} for s in (0.0, 0.5, 1.0)],
                      lights=[{"type": "sun", "power": 3, "azimuth": 250, "elevation": 25}]),
    "mat_subsurface": _row([{"base_color": [0.9, 0.7, 0.6], "roughness": 0.5, "subsurface": 1.0, "subsurface_radius": r}
                            for r in (0.0, 0.25, 0.5, 1.0)],
                           lights=[{"type": "sun", "power": 3, "azimuth": 250, "elevation": 30}]),
    # Thin translucency: four leaves seen from below against a sun above them, none to full.
    "mat_translucent": {"size": SMALL, "camera": {"fov": 40, "azimuth": 90, "elevation": -25, "distance": 5.0, "target": [0, 0, 1.0]},
                        "lights": [{"type": "sun", "power": 3, "azimuth": 60, "elevation": 60}], "world": {"color": [0.3, 0.3, 0.3]},
                        "objects": [{"shape": "plane", "scale": 0.4, "location": [(k - 1.5) * 0.9, 0, 1.0],
                                     "material": {"base_color": [0.5, 0.8, 0.3], "roughness": 0.6, "translucency": t}}
                                    for k, t in enumerate((0.0, 0.33, 0.66, 1.0))]},
    "mat_anisotropy": _row([{"base_color": [0.95, 0.93, 0.88], "metallic": 1.0, "roughness": 0.4, "anisotropy": a} for a in (0.0, 0.33, 0.66, 1.0)],
                           world={"hdri": "kloofendal_43d_clear_puresky_1k.hdr", "strength": 1.0, "sun_threshold": 10.0}, lights=[]),
    "mat_thinfilm": _row([{"base_color": [0.05, 0.05, 0.05], "roughness": 0.15, "iridescence": 1.0, "iridescence_thickness": t}
                          for t in (250, 400, 600, 900)],
                         world={"hdri": "kloofendal_43d_clear_puresky_1k.hdr", "strength": 1.0, "sun_threshold": 10.0}, lights=[]),
    # Glass in front of coloured bars: clear, frosted, tinted. (Cycles has no dispersion input, so no dispersive ball.)
    "mat_glass": _row([{"base_color": [1, 1, 1], "transmission": 1.0, "roughness": 0.0},
                       {"base_color": [1, 1, 1], "transmission": 1.0, "roughness": 0.35},
                       {"base_color": [0.3, 0.8, 0.4], "transmission": 1.0, "roughness": 0.0}], extra=_stripes()),
    "mat_alpha": _alpha(),
    # Image maps: albedo on a plane and a ball, a roughness map, a normal map.
    "tex_maps": _textured(),
    # Shading gaps: a tiled blotch map (uv x6), the same with breakup (Cycles has none: its truth stays the plain
    # tile, so tex_breakup is judged by tile_repeat.py, not FLIP), and detail normals plus a triplanar cube.
    "tex_tile": _shading(maps={"albedo": "blotches"}, uv_scale=6.0),
    "tex_breakup": _shading(maps={"albedo": "blotches"}, uv_scale=6.0, breakup=1.0),
    "tex_detail": _detail(),
    # Filtered normals (2.1): a fine weave normal map tiled x40 on a glossy plane seen from 8 m at a low angle, so many
    # texels fall under one pixel and the highlight must widen instead of sparkling or vanishing.
    "tex_fabric_far": {"size": SMALL, "camera": {"fov": 40, "azimuth": 90, "elevation": 12, "distance": 8.0, "target": [0, 0, 0.0]},
                       "lights": [{"type": "sun", "power": 3, "azimuth": 270, "elevation": 14}], "world": {"color": [0.3, 0.3, 0.3]},
                       "objects": [{"shape": "plane", "scale": 6, "material": {"base_color": [0.25, 0.3, 0.5], "roughness": 0.25,
                                                                                "maps": {"normal": "weave"}, "uv_scale": 40.0}}]},
    # Row 4.2: parallax occlusion. A 1.2 m plate with a 6 x 6 grid of 5 cm deep round holes as a bump map (strength
    # 5.33 = 0.05 m / 1.2 m x 512 px / 4), seen at 35 degrees under a raking sun; Cycles renders the real relief.
    "tex_perforated": {"size": SMALL, "camera": {"fov": 40, "azimuth": 90, "elevation": 35, "distance": 2.2, "target": [0, 0, 0.0]},
                       "lights": [{"type": "sun", "power": 3, "azimuth": 200, "elevation": 30}], "world": {"color": [0.3, 0.3, 0.3]},
                       "objects": [{"shape": "plane", "scale": 0.6, "material": {"base_color": [0.7, 0.62, 0.5], "roughness": 0.5,
                                                                                  "maps": {"normal": "holes"}, "bump": 5.33,
                                                                                  "displace": 0.05}}]},
    # Row 0.4 elimination: tex_fabric_far without its normal map (is the darkness the map, or the lobe?).
    "tex_fabric_far_flat": {"size": SMALL, "camera": {"fov": 40, "azimuth": 90, "elevation": 12, "distance": 8.0, "target": [0, 0, 0.0]},
                            "lights": [{"type": "sun", "power": 3, "azimuth": 270, "elevation": 14}], "world": {"color": [0.3, 0.3, 0.3]},
                            "objects": [{"shape": "plane", "scale": 6, "material": {"base_color": [0.25, 0.3, 0.5], "roughness": 0.25}}]},
    # Emissive meshes as the only light.
    "emission_light": _emission_room(),
    # Three coloured point lights: coloured, overlapping shadows.
    "lights_multi": _lit({"type": "point", "power": 150, "azimuth": 0, "elevation": 40, "distance": 3, "radius": 0.1, "color": [1, 0.2, 0.2]})
    | {"lights": [{"type": "point", "power": 150, "azimuth": a, "elevation": 40, "distance": 3, "radius": 0.1, "color": c}
                  for a, c in ((0, [1, 0.2, 0.2]), (120, [0.2, 1, 0.2]), (240, [0.2, 0.3, 1]))]},
    # Image-based light only: the HDRI lights the material row.
    "world_hdri": _row([{"base_color": [0.8, 0.8, 0.8], "roughness": 0.6},
                        {"base_color": [0.8, 0.1, 0.08], "roughness": 0.2},
                        {"base_color": [1.0, 0.77, 0.34], "metallic": 1.0, "roughness": 0.25},
                        {"base_color": [0.95, 0.95, 0.95], "metallic": 1.0, "roughness": 0.02}],
                       world={"hdri": "kloofendal_43d_clear_puresky_1k.hdr", "strength": 1.0, "sun_threshold": 10.0}, lights=[], view="AgX"),
    # The same, with the sun left in the sky map (no sun extraction): a tiny, very bright patch the path tracer
    # must find by sampling the map as a light. Row 2.4's case.
    "world_hdri_sun": _row([{"base_color": [0.8, 0.8, 0.8], "roughness": 0.6},
                            {"base_color": [0.8, 0.1, 0.08], "roughness": 0.2},
                            {"base_color": [1.0, 0.77, 0.34], "metallic": 1.0, "roughness": 0.25},
                            {"base_color": [0.95, 0.95, 0.95], "metallic": 1.0, "roughness": 0.02}],
                           world={"hdri": "kloofendal_43d_clear_puresky_1k.hdr", "strength": 1.0, "sun_threshold": 0.0}, lights=[], view="AgX"),
    # Interior lit by sun through a window: GI, and the noise it leaves.
    "interior": _interior(),
    # Depth of field: f/1.4 on the middle ball, the rest blur by distance.
    "dof": _row([{"base_color": c, "roughness": 0.4} for c in ([0.8, 0.1, 0.08], [0.9, 0.8, 0.1], [0.1, 0.6, 0.15], [0.1, 0.25, 0.8], [0.9, 0.9, 0.9])],
                cam={"fov": 30, "azimuth": 60, "elevation": 15, "distance": 5.0, "target": [0, 0, 0.35], "fstop": 1.4, "focus": 5.0},
                extra=_stripes()),
    # The Neutral view (Khronos PBR Neutral) on the sun scene.
    "view_neutral": _lit({"type": "sun", "power": 6, "azimuth": 30, "elevation": 45}, view="Khronos PBR Neutral"),
    # A smooth subdivided head and a hard cube: smooth normals, self-shadow, crevices.
    "geom_monkey": _monkey(),
    "wear_masks": _wear_masks(),
    # Motion: a camera orbit, a fast object with motion blur, and a still shot scored for flicker.
    "motion_orbit": _motion("orbit"),
    "motion_blur": _motion("blur"),
    "motion_still": _motion("still"),
})


def _bed():
    # The owner's reference photo, rebuilt: a close-up of a slept-in grey cotton sheet (cloth-simulated folds, a
    # jersey scan), a plush toy (fleece scan + sheen), low warm sun through a window cutting hard patches across the
    # folds, a dim cool sky fill. Meshes from bed_build.py (assets/bed/, fetch_bed.py for the scans).
    wall = {"base_color": [0.7, 0.68, 0.65], "roughness": 0.9}
    W = 0.05
    # Wall at x = 2 (the sun's side, out of frame) with a window y -0.9..0.3, z 0.75..1.6, a mullion and a transom.
    objs = [{"shape": "plane", "scale": 4, "material": {"base_color": [0.3, 0.22, 0.16], "roughness": 0.6}},
            {"shape": "cube", "scale": [W, 2.0, 0.375], "location": [2 + W, 0, 0.375], "material": wall},
            {"shape": "cube", "scale": [W, 2.0, 0.5], "location": [2 + W, 0, 2.1], "material": wall},
            {"shape": "cube", "scale": [W, 0.55, 0.425], "location": [2 + W, -1.45, 1.175], "material": wall},
            {"shape": "cube", "scale": [W, 0.85, 0.425], "location": [2 + W, 1.15, 1.175], "material": wall},
            {"shape": "cube", "scale": [W, 0.03, 0.425], "location": [2 + W, -0.3, 1.175], "material": wall},
            {"shape": "cube", "scale": [W, 0.6, 0.025], "location": [2 + W, -0.3, 1.2], "material": wall},
            # back wall behind the headboard end
            {"shape": "cube", "scale": [2.0, W, 1.3], "location": [0, 1.6 + W, 1.3], "material": wall}]
    objs += [{"region": r, "shape": "model", "asset": "bed/%s.glb" % r}
             for r in ("mattress", "sheet", "plush", "lamp")]
    objs[-3]["detail"] = [8.0, 0.6]   # the jersey scan again at 8x: the thread-level weave a close look needs
    return {"size": [768, 512], "samples": 384, "view": "AgX",
            "camera": {"fov": 38, "azimuth": 100, "elevation": 22, "distance": 1.25, "target": [-0.25, -0.1, 0.5],
                       "fstop": 2.8, "focus": 1.2},
            "lights": [{"type": "sun", "power": 8, "azimuth": 0, "elevation": 16, "color": [1.0, 0.68, 0.42]}],
            "world": {"color": [0.06, 0.075, 0.1]}, "objects": objs}


def _shade_mapping():
    # Row 2.3: a 3D checker through Object coordinates and a Mapping (Point: translate, rotate Z 30, scale 3) on a
    # plane, a sphere and a cube. Cycles: Texture Coordinate Object -> Mapping -> Checker (scale 1) -> Base Color.
    # Infinite: Shade Coord -> Shade Mapping -> Shade Checker -> Material field. The translate keeps every flat face
    # off a checker boundary (the plane sits at 0.25, not 0).
    sh = {"translate": [0.1, 0.2, 0.25], "rotate": [0, 0, 30], "scale": 3,
          "color1": [0.8, 0.8, 0.8], "color2": [0.2, 0.2, 0.2]}
    mat = {"base_color": [0.8, 0.8, 0.8], "roughness": 0.5, "shade": sh}
    return {"camera": {"fov": 40, "azimuth": 60, "elevation": 30, "distance": 5.0, "target": [0, 0, 0.3]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 30, "elevation": 50}],
            "world": {"color": [0.3, 0.3, 0.3]},
            "objects": [{"shape": "plane", "scale": 3, "material": mat},
                        {"shape": "sphere", "scale": 0.5, "location": [-0.7, 0.0, 0.5], "material": mat},
                        {"shape": "cube", "scale": 0.4, "location": [0.7, 0.0, 0.4], "material": mat}]}




def _shade_textures():
    # Row 2.4: one texture source per object, each on Object coordinates through a Mapping. Brick and gradient live in
    # the coordinate's x and y (the floor plane in the case's Z-up frame), so the camera looks down. Noise and voronoi
    # are not pixel-comparable to Cycles and are gated in ENGINETEST instead.
    def mat(**sh):
        return {"base_color": [0.8, 0.8, 0.8], "roughness": 0.5, "shade": sh}
    return {"camera": {"fov": 40, "azimuth": 70, "elevation": 55, "distance": 5.0, "target": [0, 0, 0.3]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 30, "elevation": 60}],
            "world": {"color": [0.3, 0.3, 0.3]},
            "objects": [{"shape": "plane", "scale": 3,
                         "material": mat(tex="brick", translate=[0.1, 0.07, 0.25], rotate=[0, 0, 20], scale=1.5,
                                         color1=[0.7, 0.3, 0.2], mortarColor=[0.8, 0.8, 0.8])},
                        {"shape": "sphere", "scale": 0.5, "location": [-1.2, 0.0, 0.5],
                         "material": mat(tex="checker", translate=[0.1, 0.2, 0.25], scale=4)},
                        {"shape": "sphere", "scale": 0.5, "location": [0.0, 0.0, 0.5],
                         "material": mat(tex="gradient", kind="linear", translate=[0.5, 0.5, 0.5])},
                        {"shape": "sphere", "scale": 0.5, "location": [1.2, 0.0, 0.5],
                         "material": mat(tex="gradient", kind="radial")},
                        {"shape": "cube", "scale": 0.3, "location": [0.0, -1.2, 0.3],
                         "material": mat(tex="gradient", kind="quadratic_sphere", scale=2)},
                        {"shape": "sphere", "scale": 0.5, "location": [1.2, -1.2, 0.5],
                         "material": mat(tex="image", image="blotches")}]}


def _shade_bump():
    # Row 2.6: a height bends the normal. Height = sin(10 x) sin(10 y) on Object coordinates (noise is not
    # pixel-comparable to Cycles), strength 1, distance 0.05, on a plane under a raking sun.
    # Cycles: Texture Coordinate Object -> Mapping -> Separate XYZ -> Math Sine x2 -> Math Multiply -> Bump -> Normal.
    # Infinite: Shade Coord -> Shade Mapping -> Shade Separate x2 -> Shade Math x3 -> Shade Bump -> Material normal.
    mat = {"base_color": [0.8, 0.8, 0.8], "roughness": 0.5,
           "shade": {"tex": "bump", "scale": 10, "distance": 0.05, "strength": 1.0}}
    return {"camera": {"fov": 40, "azimuth": 60, "elevation": 40, "distance": 4.0, "target": [0, 0, 0]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 20, "elevation": 20}],
            "world": {"color": [0.3, 0.3, 0.3]},
            "objects": [{"shape": "plane", "scale": 1, "material": mat}]}


CASES["shade_bump"] = _shade_bump()
CASES["shade_mapping"] = _shade_mapping()
CASES["shade_textures"] = _shade_textures()

def _mat_field_code():
    # Row 3.1: shade_textures with the brick, checker and gradient textures written by hand as Field code on a
    # Material-domain Field Pixel instead of Shade nodes. Gate: `sweep.py pair shade_textures mat_field_code` FLIP 0.
    # The image sphere stays a Shade Image chain: a Field Pixel has no picture input in the Material domain.
    c = _shade_textures()
    for o in c["objects"]:
        sh = o["material"]["shade"]
        if sh.get("tex") != "image":
            sh["code"] = True
    return c


CASES["mat_field_code"] = _mat_field_code()


def _mat_sketch_mask(name):
    # Row 3.2: a JS-drawn stripe mask on the roughness pin of a metal ball (sketch_stripes), and the same mask as a
    # PNG (stripes). Gate: `sweep.py pair mat_sketch_mask_png mat_sketch_mask` FLIP under 0.005.
    return {"size": SMALL, "camera": {"fov": 35, "azimuth": 90, "elevation": 20, "distance": 4.0, "target": [0, 0, 0.45]},
            "lights": [{"type": "sun", "power": 3, "azimuth": 40, "elevation": 35}], "world": {"color": [0.3, 0.3, 0.3]},
            "objects": [{"shape": "plane", "scale": 3, "material": {"base_color": [0.5, 0.5, 0.5], "roughness": 0.6}},
                        _ball(0.0, 0, 0.45, base_color=[0.9, 0.9, 0.9], metallic=1.0, roughness=1.0, maps={"roughness": name})]}


CASES["mat_sketch_mask"] = _mat_sketch_mask("sketch_stripes")
CASES["mat_sketch_mask_png"] = _mat_sketch_mask("stripes")
CASES["bed"] = _bed()
# A close look at the sunlit folds right of the plush, where the weave has to read.
CASES["bed_close"] = dict(_bed(), camera={"fov": 30, "azimuth": 100, "elevation": 30, "distance": 0.45,
                                          "target": [-0.3, 0.3, 0.45], "fstop": 0, "focus": 0.45})


def _bed_fur(base):
    # The plush with Geometry Op Fur on it (docs/plans/fur, block 1): a short fleece pile.
    c = dict(base, objects=[dict(o) for o in base["objects"]])
    plush = next(o for o in c["objects"] if o.get("region") == "plush")
    plush["fur"] = {"density": 40, "length": 0.006, "jitter": 0.3, "width": 0.0003, "segments": 3, "gravity": 0.1}
    return c


CASES["bed_fur"] = _bed_fur(CASES["bed"])
CASES["bed_fur_close"] = _bed_fur(dict(CASES["bed"], camera={"fov": 28, "azimuth": 100, "elevation": 22, "distance": 0.5,
                                                            "target": [-0.49, -0.41, 0.62], "fstop": 0, "focus": 0.5},
                                        supersample=16))   # pixel-thin strands need sub-pixel samples, as Cycles' 256 give them


# Styled long hair: a head-sized ball, strands combed by one arc guide (Geometry Op Fur's guide input), drooped by
# gravity. Blender rebuilds the same guide in Geometry Nodes (blender_scene.add_fur).
CASES["hair_long"] = {
    "camera": {"fov": 35, "azimuth": 70, "elevation": 12, "distance": 1.1, "target": [0, 0, 0.32]},
    "lights": [{"type": "sun", "power": 3, "azimuth": 40, "elevation": 45}],
    "world": {"color": [0.25, 0.25, 0.25]}, "supersample": 16,
    "objects": [GREY_FLOOR,
                {"shape": "sphere", "scale": 0.1, "location": [0, 0, 0.4],
                 "material": {"base_color": [0.25, 0.12, 0.06], "roughness": 0.4},
                 "fur": {"density": 20, "length": 0.15, "jitter": 0.2, "width": 0.0003, "segments": 16, "gravity": 0.3,
                         "guide": {"points": 9, "spread": 0.5, "height": 1.0}}}],
}
# Block 3 gate: the same hair with Material model Hair (Chiang, melanin) against Cycles' Principled Hair.
for _mel in (0.2, 0.5, 0.8):
    _o = dict(CASES["hair_long"]["objects"][1])
    _o["material"] = dict(_o["material"], roughness=0.3)   # skin and fibre share one Material roughness
    _o["fur"] = dict(_o["fur"], hair={"melanin": _mel, "redness": 1.0, "roughness": 0.3, "radial": 0.3, "shift": 2.0, "ior": 1.55})
    CASES["hair_melanin_%g" % _mel] = dict(CASES["hair_long"], objects=[GREY_FLOOR, _o])


# Row 4.1: layered materials. A gloss red paint over rough metal through a soft noise mask, and a 4-deep stack
# (metal, paint, dirt, dust) where each layer's own mask sits inside its parent's. Blender: Mix Shaders on the masks.
_METAL = {"base_color": [0.7, 0.7, 0.72], "metallic": 1.0, "roughness": 0.55}
_PAINT = {"base_color": [0.6, 0.05, 0.04], "roughness": 0.15, "mask": "mask_noise"}
CASES["layer_two"] = _row([_METAL, dict(_METAL, layer=_PAINT), _PAINT])
CASES["layer_four"] = _row([dict(_METAL, layer=dict(_PAINT, layer={
    "base_color": [0.18, 0.12, 0.06], "roughness": 0.9, "mask": "mask_dirt",
    "layer": {"base_color": [0.75, 0.72, 0.65], "roughness": 0.8, "mask": "mask_dust"}}))])
