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
}
