# Blender side of sculpt.py. Three steps, each run by sculpt.py:
#   blender -b --factory-startup --python blender_sculpt.py -- prep <case dir>
#       builds the mesh from case.json, exports base.glb (what Infinite sculpts) and base.blend
#   blender base.blend --python blender_sculpt.py -- sculpt <case dir>
#       opens a window (brush strokes need a 3D view), runs each dab, writes truth.txt
#   blender -b base.blend --python blender_sculpt.py -- render <case dir> <points.txt> <out.png>
#       the mesh with those point positions, from the case's camera, in Workbench
# Positions are Blender's Z-up; sculpt.py turns them into Infinite's Y-up (x, z, -y).
import bpy, json, math, os, sys, traceback
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
step, d = args[0], args[1]
case = json.load(open(os.path.join(d, "case.json")))

# Infinite brush index -> Blender's Essentials brush (Sculpt::Brush order). A dab with "to" is a drag.
BRUSHES = ["Draw", "Smooth", "Grab", "Inflate/Deflate", "Pinch/Magnify", "Flatten/Contrast", "Crease Polish"]
LIB = "brushes/essentials_brushes-mesh_sculpt.blend/Brush/"
LEAD_IN = (4, 5)


def mesh_object():
    return next(o for o in bpy.data.objects if o.type == 'MESH')


def write_points(ob, path, tag, mode="w"):
    with open(path, mode) as f:
        f.write("%s %d\n" % (tag, len(ob.data.vertices)))
        for v in ob.data.vertices:
            f.write("%g %g %g\n" % tuple(v.co))


def prep():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    m = case["mesh"]
    if m["shape"] == "sphere":
        bpy.ops.mesh.primitive_uv_sphere_add(segments=m.get("segments", 96), ring_count=m.get("rings", 48), radius=1)
    else:
        bpy.ops.import_scene.gltf(filepath=os.path.expanduser(m["asset"]))
        for o in list(bpy.data.objects):
            if o.type != 'MESH':
                bpy.data.objects.remove(o)
        for o in bpy.data.objects:
            mw = o.matrix_world.copy()
            o.parent = None
            o.matrix_world = mw
        bpy.ops.object.select_all(action='SELECT')
        bpy.context.view_layer.objects.active = mesh_object()
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        if len(bpy.data.objects) > 1:
            bpy.ops.object.join()
    ob = mesh_object()
    ob.name = "sculpt"
    # Bake whatever transform is left (an apply skips shared mesh data) so points are world positions.
    from mathutils import Matrix
    if ob.data.users > 1:
        ob.data = ob.data.copy()
    ob.data.transform(ob.matrix_world)
    ob.matrix_world = Matrix.Identity(4)
    ob.modifiers.clear()
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    # One point per position, as Infinite welds them, so both sculpt the same surface.
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=1e-6); bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.object.mode_set(mode='OBJECT')
    ob.data.materials.clear()
    if m.get("bump"):   # the same starting bump for both sides: along the normal, smoothstep falloff
        c, r, h = Vector(m["bump"]["at"]), m["bump"]["radius"], m["bump"]["height"]
        for v in ob.data.vertices:
            t = max(0.0, 1.0 - (v.co - c).length / r)
            v.co += v.co.normalized() * h * t * t * (3 - 2 * t)   # radial: the sphere's normal
        ob.data.update()
    bpy.ops.export_scene.gltf(filepath=os.path.join(d, "base.glb"), use_selection=True, export_materials='NONE',
                              export_normals=True, export_texcoords=False)
    # Dabs land on the surface: each target snaps to the nearest point, its normal is where the view looks from.
    dabs = []
    for k in case["dabs"]:
        if "to" in k:   # a drag: surface points every 2% of the radius from at to to
            a, b = Vector(k["at"]), Vector(k["to"])
            n = max(1, int((b - a).length / (k["radius"] * 0.02)))
            k = dict(k, path=[list(ob.closest_point_on_mesh(a.lerp(b, i / n))[1]) for i in range(n + 1)])
            k["at"] = list(a.lerp(b, 0.5))
        ok, loc, nrm, _ = ob.closest_point_on_mesh(Vector(k["at"]))
        dabs.append(dict(k, at=list(loc), normal=list(nrm.normalized())))
    json.dump(dabs, open(os.path.join(d, "dabs.json"), "w"), indent=1)
    write_points(ob, os.path.join(d, "base.txt"), "before")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(d, "base.blend"))
    print("SCULPTPREP", len(ob.data.vertices))


def sculpt():
    from bpy_extras import view3d_utils
    win = bpy.context.window_manager.windows[0]
    area = next(a for a in win.screen.areas if a.type == 'VIEW_3D')
    region = next(r for r in area.regions if r.type == 'WINDOW')
    space = area.spaces.active
    rv3d = space.region_3d
    ob = mesh_object()
    out = os.path.join(d, "truth.txt")
    write_points(ob, out, "before")
    dabs = json.load(open(os.path.join(d, "dabs.json")))
    with bpy.context.temp_override(window=win, area=area, region=region):
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.mode_set(mode='SCULPT')
        sc = bpy.context.tool_settings.sculpt
        sc.use_symmetry_x = sc.use_symmetry_y = sc.use_symmetry_z = False
        # The shared size and strength would override each brush's own (radius 0.58 by default).
        ups = sc.unified_paint_settings
        ups.use_unified_size = ups.use_unified_strength = False
        rv3d.view_perspective = 'ORTHO'
        for k in dabs:
            # Activated per dab so each starts from the asset's own settings.
            bpy.ops.brush.asset_activate(asset_library_type='ESSENTIALS', relative_asset_identifier=LIB + BRUSHES[k["brush"]])
            br = sc.brush
            br.use_locked_size = 'SCENE'
            br.unprojected_size = 2 * k["radius"]   # a diameter in Blender 5
            br.strength = k["strength"]
            br.use_pressure_strength = False
            assert not k.get("invert"), "inverted dabs are not mapped to Blender's brush direction yet"
            # Look at the dab straight down its normal so the mouse ray lands on it.
            at, n = Vector(k["at"]), Vector(k["normal"])
            rv3d.view_location = at
            rv3d.view_rotation = n.to_track_quat('Z', 'Y')
            path = [Vector(p) for p in k.get("path", [])]
            span = max(((p - at).length for p in path), default=0)
            rv3d.view_distance = max(k["radius"] * 8, span * 4)
            rv3d.update()   # the view matrices only follow on a redraw otherwise
            mxy = view3d_utils.location_3d_to_region_2d(region, rv3d, at)
            if path:   # a drag: Blender spaces its own dabs along the samples
                pts = [(p, view3d_utils.location_3d_to_region_2d(region, rv3d, p)) for p in path]
            else:      # Pinch and Flatten skip a stroke's first sample (the rest apply each one): give them a lead-in.
                pts = [(at, mxy + Vector((i * 0.5, 0))) for i in range(2 if k["brush"] in LEAD_IN else 1)]
            bpy.ops.sculpt.brush_stroke(stroke=[{"name": "", "location": p, "mouse": m, "mouse_event": m,
                                                 "is_start": i == 0, "pressure": 1.0, "size": 50, "time": i * 0.01,
                                                 "x_tilt": 0, "y_tilt": 0} for i, (p, m) in enumerate(pts)])
        bpy.ops.object.mode_set(mode='OBJECT')
    write_points(ob, out, "after", "a")
    print("SCULPTDONE", len(dabs))


def render(points, png):
    ob = mesh_object()
    lines = open(points).read().split("\n")
    i = next(k for k, l in enumerate(lines) if l.startswith("after"))
    vals = [float(x) for l in lines[i + 1:i + 1 + len(ob.data.vertices)] for x in l.split()]
    ob.data.vertices.foreach_set("co", vals)
    ob.data.update()
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.shade_smooth()
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'SINGLE'
    sc.render.resolution_x, sc.render.resolution_y = 640, 640
    c = case["camera"]
    a, e = math.radians(c["azimuth"]), math.radians(c["elevation"])
    tgt = Vector(c["target"])
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    cam.location = tgt + c["distance"] * Vector((math.cos(e) * math.cos(a), -math.cos(e) * math.sin(a), math.sin(e)))
    cam.rotation_euler = (tgt - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam.data.angle = math.radians(c.get("fov", 35))
    sc.camera = cam
    sc.render.filepath = png
    bpy.ops.render.render(write_still=True)
    print("SCULPTRENDER", png)


def main():
    try:
        if step == "prep":
            prep()
        elif step == "sculpt":
            sculpt()
        elif step == "render":
            render(args[2], args[3])
    except Exception:
        traceback.print_exc()
    if step == "sculpt":
        bpy.ops.wm.quit_blender()


if step == "sculpt":
    bpy.app.timers.register(lambda: main() and None, first_interval=1.0)
else:
    main()
