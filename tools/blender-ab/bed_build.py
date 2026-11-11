# Builds the bed close-up's meshes once, headless, and writes one glb per object into assets/bed/ (git-ignored):
#   Blender -b --factory-startup -P tools/blender-ab/bed_build.py
# sheet.glb: a cotton sheet cloth-simulated onto a mattress over a few hidden lumps (real folds, not a bump map),
# cotton_jersey scan maps; plush.glb: a soft toy from rounded parts, curly_teddy scan maps + sheen;
# mattress.glb, lamp.glb: plain. Blender Z-up metres; the harness case "bed" (cases.py) places nothing, all
# geometry is already where it belongs.
import bpy, bmesh, math, os
from mathutils import Vector

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets", "bed")
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene


def principled(name, maps=None, tint=(1, 1, 1), rough=0.8, sheen=0.0, sheen_rough=0.5, normal_strength=1.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (*tint, 1)
    p.inputs["Roughness"].default_value = rough
    p.inputs["Sheen Weight"].default_value = sheen
    p.inputs["Sheen Roughness"].default_value = sheen_rough
    for key, path in (maps or {}).items():
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(path)
        if key != "albedo":
            tex.image.colorspace_settings.name = "Non-Color"
        if key == "albedo":
            if tint != (1, 1, 1):   # glTF carries the tint as baseColorFactor x texture
                mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'
                mix.inputs[0].default_value = 1.0
                nt.links.new(tex.outputs["Color"], mix.inputs[6])
                mix.inputs[7].default_value = (*tint, 1)
                nt.links.new(mix.outputs[2], p.inputs["Base Color"])
            else:
                nt.links.new(tex.outputs["Color"], p.inputs["Base Color"])
        elif key == "roughness":
            nt.links.new(tex.outputs["Color"], p.inputs["Roughness"])
        elif key == "normal":
            nm = nt.nodes.new("ShaderNodeNormalMap"); nm.inputs["Strength"].default_value = normal_strength
            nt.links.new(tex.outputs["Color"], nm.inputs["Color"])
            nt.links.new(nm.outputs["Normal"], p.inputs["Normal"])
    return mat


def scale_uv(ob, s):
    for loop in ob.data.uv_layers.active.data:
        loop.uv = loop.uv * s


def export(ob, name):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(HERE, name + ".glb"), export_format='GLB', use_selection=True,
                              export_apply=True, export_tangents=True, export_image_format='JPEG',
                              export_jpeg_quality=92)
    print("BED_GLB", name)


def tex(role):
    return {k: os.path.join(HERE, "%s_%s.jpg" % (role, k)) for k in ("albedo", "normal", "roughness")}


# Mattress: 2.0 x 1.6 m, top at z = 0.45, rounded edges.
bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0.3))
mattress = bpy.context.object; mattress.name = "mattress"
mattress.scale = (2.0, 1.6, 0.3)
bpy.ops.object.transform_apply(scale=True)
bev = mattress.modifiers.new("b", 'BEVEL'); bev.width = 0.06; bev.segments = 6
bpy.ops.object.modifier_apply(modifier="b")
mattress.data.materials.append(principled("mattress", tint=(0.55, 0.55, 0.56), rough=0.9))

# Hidden lumps under the sheet: a crumpled duvet edge and a few ridges, like a slept-in bed.
lumps = []
for loc, sc_ in [((0.35, 0.25, 0.47), (0.45, 0.22, 0.07)), ((-0.15, -0.2, 0.47), (0.35, 0.12, 0.05)),
                 ((0.6, -0.35, 0.47), (0.18, 0.4, 0.06)), ((-0.5, 0.45, 0.47), (0.3, 0.3, 0.09))]:
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1, location=loc, segments=24, ring_count=12)
    l = bpy.context.object; l.scale = sc_; lumps.append(l)
for c in [mattress] + lumps:
    c.modifiers.new("col", 'COLLISION').settings.thickness_outer = 0.004

# Sheet: 2.6 x 2.2 m dropped from above, slightly gathered at two corners so it folds instead of lying flat.
bpy.ops.mesh.primitive_grid_add(x_subdivisions=150, y_subdivisions=130, size=1, location=(0, 0, 0.62))
sheet = bpy.context.object; sheet.name = "sheet"
sheet.scale = (2.6, 2.2, 1); bpy.ops.object.transform_apply(scale=True)
bm = bmesh.new(); bm.from_mesh(sheet.data)
for v in bm.verts:   # gathered in toward the middle: extra cloth that has to buckle
    v.co.x *= 0.92 + 0.08 * abs(v.co.y) / 1.1
    v.co.z += 0.015 * math.sin(v.co.x * 7.0) * math.cos(v.co.y * 5.0)
bm.to_mesh(sheet.data); bm.free()
cl = sheet.modifiers.new("cloth", 'CLOTH')
s = cl.settings
s.quality = 8; s.mass = 0.15; s.air_damping = 1.0
s.tension_stiffness = 15; s.compression_stiffness = 15; s.shear_stiffness = 5; s.bending_stiffness = 0.05
cl.collision_settings.use_self_collision = True; cl.collision_settings.self_distance_min = 0.004
cl.collision_settings.distance_min = 0.004
cl.point_cache.frame_start = 1; cl.point_cache.frame_end = 70
sc.frame_start, sc.frame_end = 1, 70
for f in range(1, 71):
    sc.frame_set(f)
bpy.context.view_layer.objects.active = sheet
bpy.ops.object.modifier_apply(modifier="cloth")
sm = sheet.modifiers.new("sm", 'CORRECTIVE_SMOOTH'); sm.iterations = 3
bpy.ops.object.modifier_apply(modifier="sm")
sub = sheet.modifiers.new("sub", 'SUBSURF'); sub.levels = 1
bpy.ops.object.modifier_apply(modifier="sub")
bpy.ops.object.shade_smooth()
scale_uv(sheet, 5.0)   # the jersey scan covers ~0.5 m: 5 repeats over the 2.6 m sheet
# A grey cotton sheet like the photo: the beige jersey scan tinted cool grey.
sheet.data.materials.append(principled("sheet", tex("sheet"), tint=(0.62, 0.66, 0.72), rough=1.0, sheen=0.3,
                                       sheen_rough=0.4))
for l in lumps:
    bpy.data.objects.remove(l)

# Plush toy lying on its side at the left, a leg toward the camera (as the photo's toy).
parts = []
def blob(loc, r, sc_=(1, 1, 1)):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=loc, segments=48, ring_count=24)
    o = bpy.context.object; o.scale = sc_; parts.append(o)
blob((-0.55, -0.35, 0.62), 0.14, (1.25, 1.0, 0.85))          # body
blob((-0.34, -0.33, 0.66), 0.1, (1.0, 1.0, 0.95))            # head
blob((-0.30, -0.27, 0.75), 0.035, (0.5, 1, 1))               # ear
blob((-0.30, -0.42, 0.74), 0.035, (0.5, 1, 1))               # ear
blob((-0.24, -0.34, 0.64), 0.035)                            # snout
blob((-0.62, -0.52, 0.56), 0.05, (1.0, 2.0, 0.9))            # leg toward the camera
blob((-0.74, -0.47, 0.57), 0.05, (1.0, 1.8, 0.9))            # leg
blob((-0.48, -0.5, 0.6), 0.045, (1.0, 1.7, 0.9))             # arm
bpy.ops.object.select_all(action='DESELECT')
for o in parts: o.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
plush = bpy.context.object; plush.name = "plush"
rm = plush.modifiers.new("r", 'REMESH'); rm.mode = 'VOXEL'; rm.voxel_size = 0.006
bpy.ops.object.modifier_apply(modifier="r")
sm = plush.modifiers.new("sm", 'SMOOTH'); sm.iterations = 6
bpy.ops.object.modifier_apply(modifier="sm")
bpy.ops.object.shade_smooth()
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(island_margin=0.01); bpy.ops.object.mode_set(mode='OBJECT')
scale_uv(plush, 2.0)
plush.data.materials.append(principled("plush", tex("plush"), rough=1.0, sheen=1.0, sheen_rough=0.35,
                                       normal_strength=1.5))

# Teal ceramic lamp base behind the bed, out of focus.
bpy.ops.mesh.primitive_cylinder_add(radius=0.11, depth=0.32, location=(1.15, 0.95, 0.62), vertices=64)
lamp = bpy.context.object; lamp.name = "lamp"
bv = lamp.modifiers.new("b", 'BEVEL'); bv.width = 0.03; bv.segments = 5
bpy.ops.object.modifier_apply(modifier="b"); bpy.ops.object.shade_smooth()
lamp.data.materials.append(principled("lamp", tint=(0.05, 0.38, 0.38), rough=0.25))

os.makedirs(HERE, exist_ok=True)
for ob in (mattress, sheet, plush, lamp):
    export(ob, ob.name)
print("BED_BUILD_OK")
