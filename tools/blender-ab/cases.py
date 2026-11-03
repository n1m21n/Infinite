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


def _material_rows():
    objs = [GREY_FLOOR]
    for i, rough in enumerate([0.05, 0.25, 0.5, 0.75, 1.0]):
        x = (i - 2) * 0.9
        objs.append(_ball(x, -0.5, 0.35, base_color=[0.8, 0.1, 0.08], roughness=rough))
        objs.append(_ball(x, 0.5, 0.35, base_color=[1.0, 0.77, 0.34], metallic=1.0, roughness=rough))
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
    rng, keep_out = random.Random(seed), [(-1.5, 0.6, 0.75), (1.9, 0.9, 0.55), (0.2, 0.4, 0.95),
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
        {"region": "fox", "shape": "model", "asset": "Fox.glb", "env": "INFINITE_3DBENCH_FOX", "scale": 0.0075,
         "location": [0.2, 0.4, 0], "clip": FOX_CLIP},
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
}
