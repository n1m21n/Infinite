# Blender side of the boat-on-ocean A/B: rebuilds the frame that BenchBoatDump wrote (meshes already posed, in
# Blender's Z-up axes) and renders it with Cycles.
#   Blender -b -P tools/blender-ab/boat_scene.py -- <dump dir> <out.png> [samples]
# Units follow harness.light_lines: sun W/m^2 = intensity; point (hard falloff 1) P = 16 pi * intensity.
# Node colours are sRGB-encoded, so they are linearised here. Exposure is a multiplier in Infinite, stops here.
import bpy, json, math, os, sys
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
dump, out = a[0], a[1]
samples = int(a[2]) if len(a) > 2 else 256
sc_json = json.load(open(os.path.join(dump, "scene.json")))
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene

def lin(c):
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]

for o in sc_json["objects"]:
    bpy.ops.wm.obj_import(filepath=os.path.join(dump, o["name"] + ".obj"), forward_axis='Y', up_axis='Z')
    ob = bpy.context.selected_objects[0]
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(40))
    m = o["material"]
    mat = bpy.data.materials.new(o["name"]); mat.use_nodes = True
    p = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (*lin(m["color"]), 1)
    p.inputs["Metallic"].default_value = m["metallic"]
    p.inputs["Roughness"].default_value = m["roughness"]
    p.inputs["IOR"].default_value = m["ior"]
    p.inputs["Specular IOR Level"].default_value = m["specular"]
    p.inputs["Coat Weight"].default_value = m["clearcoat"]
    p.inputs["Coat Roughness"].default_value = m["clearcoat_roughness"]
    if m["emission"] > 0:
        p.inputs["Emission Color"].default_value = (*lin(m["emission_color"]), 1)
        p.inputs["Emission Strength"].default_value = m["emission"]
    ob.data.materials.clear(); ob.data.materials.append(mat)

def aim(obj, target):
    d = Vector(target) - obj.location
    obj.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()

c = sc_json["camera"]
cd = bpy.data.cameras.new("c"); cd.sensor_fit = 'VERTICAL'; cd.sensor_height = 36
cd.angle = math.radians(c["fov"]); cd.clip_start = 0.05; cd.clip_end = 5000
co = bpy.data.objects.new("c", cd); sc.collection.objects.link(co); sc.camera = co
co.location = c["eye"]; aim(co, c["target"])

for i, l in enumerate(sc_json["lights"]):
    v = Vector(l["vec"]); col = lin(l["color"])
    if l["type"] in (0, 2):
        ld = bpy.data.lights.new("l%d" % i, 'SUN'); ld.energy = l["intensity"]
        ld.angle = math.radians(l.get("angle", 0.526))  # the engine's disc: 0.526 deg x the light's shadow softness
        lo = bpy.data.objects.new("l%d" % i, ld); sc.collection.objects.link(lo)
        lo.location = v.normalized() * 10; aim(lo, (0, 0, 0))
    elif l["type"] == 1:
        ld = bpy.data.lights.new("l%d" % i, 'POINT'); ld.energy = l["intensity"] * 16 * math.pi
        ld.shadow_soft_size = 0.0  # a true point, as the engine's; a radius would poke out of a lamp globe
        lo = bpy.data.objects.new("l%d" % i, ld); sc.collection.objects.link(lo); lo.location = v
    else:
        if l["intensity"] > 0:
            print("BOAT_AB: light type %d has no Cycles twin, skipped" % l["type"])
        continue
    ld.color = col

sc.world = bpy.data.worlds.new("w"); sc.world.use_nodes = True
wt = sc.world.node_tree
bg = next(n for n in wt.nodes if n.type == "BACKGROUND")
tex = wt.nodes.new("ShaderNodeTexEnvironment"); tex.image = bpy.data.images.load(sc_json["env"])
mp = wt.nodes.new("ShaderNodeMapping")
wt.links.new(wt.nodes.new("ShaderNodeTexCoord").outputs["Generated"], mp.inputs["Vector"])
wt.links.new(mp.outputs["Vector"], tex.inputs["Vector"])
wt.links.new(tex.outputs["Color"], bg.inputs["Color"])

sc.render.engine = 'CYCLES'
prefs = bpy.context.preferences.addons["cycles"].preferences
try:
    prefs.compute_device_type = 'METAL'; prefs.get_devices()
    for d in prefs.devices: d.use = True
    sc.cycles.device = 'GPU'
except Exception as e:
    print("BOAT_AB: GPU off,", e); sc.cycles.device = 'CPU'
sc.cycles.samples = samples; sc.cycles.use_denoising = True
sc.cycles.max_bounces = 12
sc.view_settings.view_transform = 'AgX' if sc_json["tonemap"] == 3 else 'Standard'
sc.view_settings.look = 'None'
sc.view_settings.exposure = math.log2(max(sc_json["exposure"], 1e-4))
sc.render.resolution_x, sc.render.resolution_y = sc_json["size"]
sc.render.resolution_percentage = 100
sc.render.image_settings.file_format = 'PNG'
sc.render.filepath = out
bpy.ops.render.render(write_still=True)
# Linear scene-referred copy (no view transform, no exposure) so lighting can be scored apart from the tonemap.
img = bpy.data.images["Render Result"]
sc.render.image_settings.file_format = 'OPEN_EXR'; sc.render.image_settings.color_depth = '32'
img.save_render(os.path.splitext(out)[0] + ".exr", scene=sc)
print("BOAT_AB_OK", out)
