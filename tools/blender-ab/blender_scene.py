# Blender half of a picture case. Run by harness.py inside headless Blender:
#   Blender -b --factory-startup --python blender_scene.py -- <case dir>
# Reads <case>/scene.json (format in README.md), builds it, renders truth.png
# with Cycles and writes each object as <case>/obj_<i>.glb for Infinite. The
# objects' transforms are baked into their meshes first, so both apps get the
# same world-space vertices and the glTF needs no node transforms.
# Hero additions: shape "model" (a glTF/glb asset kept with its own material, optionally skinned
# and posed per frame), "instances" (one shape at many points; the points go out as a vertex-only
# glb), an HDRI world, per-region object indices, and a mask_<k>.png per frame (1 spp, no filter,
# no depth of field: the value in each pixel is the object index, 0 = sky).
import bpy, sys, os, json, math, struct
from mathutils import Vector, Matrix

case = sys.argv[sys.argv.index("--") + 1]
scene = json.load(open(os.path.join(case, "scene.json")))
HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
REGIONS = scene.get("regions", [])
FRAMES = scene.get("frames")


def region_index(o):
    return REGIONS.index(o["region"]) + 1 if o.get("region") in REGIONS else 0


def asset_path(o):
    # The Fox's path comes from the environment, as in the 3D bench; assets/ holds a copy.
    env = os.environ.get(o.get("env", ""), "")
    return env if env and os.path.exists(env) else os.path.join(ASSETS, o["asset"])


def glb_json(path):
    f = open(path, "rb").read()
    return json.loads(f[20:20 + struct.unpack("<I", f[12:16])[0]])


def spherical(az, el, dist, target=(0, 0, 0)):
    # The harness's one convention (Infinite's, in Blender's Z-up frame):
    # azimuth from +X toward -Y, elevation up from the ground.
    a, e = math.radians(az), math.radians(el)
    return Vector(target) + Vector((math.cos(e) * math.cos(a), -math.cos(e) * math.sin(a), math.sin(e))) * dist


def aim(ob, at):
    ob.rotation_euler = (Vector(at) - ob.location).to_track_quat('-Z', 'Y').to_euler()


bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene

def import_model(o, i):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=asset_path(o))
    new = [x for x in bpy.data.objects if x not in before]
    root = bpy.data.objects.new("model%d" % i, None)
    sc.collection.objects.link(root)
    s = o.get("scale", 1.0)
    root.scale = (s, s, s)
    root.location = o.get("location", (0, 0, 0))
    for x in new:
        x.pass_index = region_index(o)
        if x.parent is None:
            x.parent = root
    if o.get("clip"):
        name = glb_json(asset_path(o))["animations"][o["clip"] - 1]["name"]
        arm = next(x for x in new if x.type == 'ARMATURE')
        act = bpy.data.actions[name]
        ad = arm.animation_data_create()
        for t in list(ad.nla_tracks):
            ad.nla_tracks.remove(t)
        ad.action = act
        if hasattr(ad, "action_slot") and len(act.slots):
            ad.action_slot = act.slots[0]
        posed.append((arm, act, o))


posed = []   # (armature, action, object) set to each frame's phase before it renders

for i, o in enumerate(scene["objects"]):
    shape = o["shape"]
    if shape == "model":
        import_model(o, i)
        continue
    if shape == "sphere":
        bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=1)
        bpy.ops.object.shade_smooth()
    elif shape == "plane":
        bpy.ops.mesh.primitive_plane_add(size=2)
    elif shape == "cube":
        bpy.ops.mesh.primitive_cube_add(size=2)
    elif shape == "monkey":
        bpy.ops.mesh.primitive_monkey_add(size=2)
        bpy.ops.object.modifier_add(type='SUBSURF'); bpy.ops.object.modifier_apply(modifier="Subdivision")
        bpy.ops.object.shade_smooth()
    else:
        sys.exit("unknown shape " + shape)
    ob = bpy.context.active_object
    s = o.get("scale", 1.0)
    ob.scale = s if isinstance(s, list) else (s, s, s)
    ob.location = o.get("location", (0, 0, 0))
    bpy.context.view_layer.update()
    ob.data.transform(ob.matrix_world)
    ob.matrix_world = Matrix.Identity(4)

    m = o.get("material", {})
    mat = bpy.data.materials.new("m%d" % i); mat.use_nodes = True
    p = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (*m.get("base_color", (0.8, 0.8, 0.8)), 1)
    p.inputs["Metallic"].default_value = m.get("metallic", 0.0)
    p.inputs["Roughness"].default_value = m.get("roughness", 0.5)
    p.inputs["IOR"].default_value = m.get("ior", 1.5)
    p.inputs["Transmission Weight"].default_value = m.get("transmission", 0.0)
    p.inputs["Coat Weight"].default_value = m.get("coat", 0.0)
    p.inputs["Coat Roughness"].default_value = m.get("coat_roughness", 0.03)
    p.inputs["Sheen Weight"].default_value = m.get("sheen", 0.0)
    p.inputs["Emission Color"].default_value = (*m.get("emission", (0, 0, 0)), 1)
    p.inputs["Emission Strength"].default_value = m.get("emission_strength", 0.0)
    ob.data.materials.append(mat)
    ob.pass_index = region_index(o)

    for x in bpy.context.scene.objects:
        x.select_set(x == ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(case, "obj_%d.glb" % i), export_format='GLB',
                              use_selection=True, export_materials='NONE', export_animations=False)

    if o.get("instances"):
        # Many copies of this shape. The shape stays at the origin (Infinite stamps it on the points);
        # the points leave as a mesh whose vertices are the positions (triangles only keep them all used).
        pts = o["instances"]
        ob.hide_render = True
        for k, pt in enumerate(pts):
            c = bpy.data.objects.new("inst%d_%d" % (i, k), ob.data)
            c.location = pt
            c.pass_index = region_index(o)
            sc.collection.objects.link(c)
        me = bpy.data.meshes.new("pts%d" % i)
        me.from_pydata(pts, [], [(j, j + 1, j + 2) for j in range(len(pts) - 2)])
        po = bpy.data.objects.new("pts%d" % i, me)
        sc.collection.objects.link(po)
        po.hide_render = True
        for x in bpy.context.scene.objects:
            x.select_set(x == po)
        bpy.context.view_layer.objects.active = po
        bpy.ops.export_scene.gltf(filepath=os.path.join(case, "pts_%d.glb" % i), export_format='GLB',
                                  use_selection=True, export_materials='NONE', export_animations=False,
                                  export_normals=False)

cam = scene["camera"]
camd = bpy.data.cameras.new("c")
# fov is the VERTICAL angle on both sides (Render 3D's Mat4::Perspective); a 36 mm span keeps harness.dof_aperture's
# sensor span the same as before. Square frames are unchanged.
camd.sensor_fit = 'VERTICAL'; camd.sensor_height = 36
camd.angle = math.radians(cam["fov"])
co = bpy.data.objects.new("c", camd); sc.collection.objects.link(co); sc.camera = co
target = cam.get("target", (0, 0, 0))
co.location = spherical(cam["azimuth"], cam["elevation"], cam["distance"], target)
aim(co, target)
if cam.get("fstop"):
    camd.dof.use_dof = True
    camd.dof.focus_distance = cam.get("focus", cam["distance"])
    camd.dof.aperture_fstop = cam["fstop"]

for i, l in enumerate(scene.get("lights", [])):
    kind = {"point": "POINT", "spot": "SPOT", "area": "AREA", "sun": "SUN"}[l["type"]]
    ld = bpy.data.lights.new("l%d" % i, kind)
    ld.color = l.get("color", (1, 1, 1))
    ld.energy = l["power"]  # W (W/m^2 for a sun)
    ld.use_shadow = True
    if kind == "SUN":
        ld.angle = math.radians(l.get("angle", 0.5))
    elif kind == "AREA":
        ld.shape = 'RECTANGLE'; ld.size, ld.size_y = l.get("size", (1, 1))
    else:
        ld.shadow_soft_size = l.get("radius", 0.1)
    if kind == "SPOT":
        ld.spot_size = math.radians(2 * l["spot_angle"])  # Blender: full cone angle
        ld.spot_blend = l.get("blend", 0.2)
    lo = bpy.data.objects.new("l%d" % i, ld); sc.collection.objects.link(lo)
    lo.location = spherical(l["azimuth"], l["elevation"], l.get("distance", 10))
    aim(lo, (0, 0, 0))

w = scene.get("world", {})
sc.world = bpy.data.worlds.new("w"); sc.world.use_nodes = True
wt = sc.world.node_tree
bg = next(n for n in wt.nodes if n.type == "BACKGROUND")
bg.inputs["Color"].default_value = (*w.get("color", (0, 0, 0)), 1)
bg.inputs["Strength"].default_value = w.get("strength", 1.0)
if w.get("hdri"):
    # Equirect HDRI; its sun lights the scene here, and Infinite's Environment lifts it out as a light.
    tex = wt.nodes.new("ShaderNodeTexEnvironment")
    tex.image = bpy.data.images.load(os.path.join(ASSETS, w["hdri"]))
    mp = wt.nodes.new("ShaderNodeMapping")
    mp.inputs["Rotation"].default_value[2] = math.radians(w.get("rotation", 0.0))
    wt.links.new(wt.nodes.new("ShaderNodeTexCoord").outputs["Generated"], mp.inputs["Vector"])
    wt.links.new(mp.outputs["Vector"], tex.inputs["Vector"])
    wt.links.new(tex.outputs["Color"], bg.inputs["Color"])

sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'
sc.cycles.samples = scene.get("samples", 256)
sc.cycles.use_denoising = True
sc.view_settings.view_transform = scene.get("view", "Standard")
sc.view_settings.look = 'None'
sc.render.resolution_x, sc.render.resolution_y = scene.get("size", (512, 512))
sc.render.resolution_percentage = 100
sc.render.image_settings.file_format = 'PNG'


def pose(frame):
    for arm, act, o in posed:
        a, b = act.frame_range
        f = a + frame["phase"] * (b - a)
        sc.frame_set(int(f), subframe=f - int(f))


def render_mask(path):
    """Object index per pixel as an 8-bit PNG: one sample, no filter, no depth of field."""
    sc.view_layers[0].use_pass_object_index = True
    sc.render.dither_intensity = 0.0   # the 8-bit PNG would otherwise dither the index by +-1
    tree = bpy.data.node_groups.new("mask", 'CompositorNodeTree')
    rl = tree.nodes.new('CompositorNodeRLayers')
    div = tree.nodes.new('ShaderNodeMath'); div.operation = 'DIVIDE'; div.inputs[1].default_value = 255.0
    out = tree.nodes.new('NodeGroupOutput')
    tree.interface.new_socket("Image", in_out='OUTPUT', socket_type='NodeSocketColor')
    tree.links.new(rl.outputs['Object Index'], div.inputs[0])
    tree.links.new(div.outputs[0], out.inputs[0])
    keep = (sc.cycles.samples, sc.cycles.use_denoising, sc.cycles.filter_width, sc.view_settings.view_transform,
            sc.render.image_settings.color_mode, sc.render.image_settings.color_depth, camd.dof.use_dof)
    sc.cycles.samples, sc.cycles.use_denoising, sc.cycles.filter_width = 1, False, 0.01
    sc.view_settings.view_transform = 'Raw'
    sc.render.image_settings.color_mode, sc.render.image_settings.color_depth = 'BW', '8'
    camd.dof.use_dof = False
    sc.compositing_node_group = tree
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    sc.compositing_node_group = None
    sc.render.dither_intensity = 1.0
    (sc.cycles.samples, sc.cycles.use_denoising, sc.cycles.filter_width, sc.view_settings.view_transform,
     sc.render.image_settings.color_mode, sc.render.image_settings.color_depth, camd.dof.use_dof) = keep


for k, fr in enumerate(FRAMES or [None]):
    tag = "" if FRAMES is None else "_%d" % k
    if fr is not None:
        pose(fr)
    sc.render.filepath = os.path.join(case, "truth%s.png" % tag)
    bpy.ops.render.render(write_still=True)
    if REGIONS:
        render_mask(os.path.join(case, "mask%s.png" % tag))
print("SCENEDONE", len(scene["objects"]))
