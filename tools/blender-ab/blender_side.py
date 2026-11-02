# Blender half of the AO A/B. Run by ab.py inside headless Blender:
#   Blender -b --factory-startup --python blender_side.py -- gen <outdir>
#   Blender -b --factory-startup --python blender_side.py -- ao|bake <in.glb> <outdir> <res> <samples> <distance>
# gen writes the small test objects as .glb. ao joins every mesh of a file into
# one (so both apps see the same surface), writes it as joined.glb for
# Infinite, then renders two pictures from the same camera as the Infinite
# patch: truth.png (AO computed per pixel, no UVs); bake renders bake.png
# (Smart UV, islands packed, AO baked to res x res, shown by emission). bake
# runs in its own process: Blender 5.1 aborts on some meshes leaving edit
# mode after a pack, and the truth must survive that. Standard view,
# transparent film, so the alpha is the object mask.
import bpy, sys, math, os
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def export(path):
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', export_materials='NONE',
                              export_animations=False, export_skins=False)


def gen(out):
    os.makedirs(out, exist_ok=True)
    made = {}

    def keep(name):
        bpy.ops.object.select_all(action='SELECT')
        bpy.ops.object.join() if len(bpy.context.selected_objects) > 1 else None
        made[name] = os.path.join(out, name + ".glb")
        export(made[name])

    clear(); bpy.ops.mesh.primitive_monkey_add(); bpy.ops.object.shade_smooth()
    bpy.ops.object.modifier_add(type='SUBSURF'); bpy.ops.object.modifier_apply(modifier="Subdivision")
    keep("suzanne")
    # Contact crevice: a torus lying on a slab.
    clear(); bpy.ops.mesh.primitive_cube_add(size=1, scale=(1.4, 1.4, 0.08), location=(0, 0, -0.04))
    bpy.ops.mesh.primitive_torus_add(major_radius=0.45, minor_radius=0.15, location=(0, 0, 0.15))
    bpy.ops.object.shade_smooth(); keep("torus_on_slab")
    # Inside corner: three walls and a floor.
    clear()
    for loc, sc in [((0, 0, 0), (1, 1, 0.05)), ((0, 0.5, 0.5), (1, 0.05, 1)), ((-0.5, 0, 0.5), (0.05, 1, 1))]:
        bpy.ops.mesh.primitive_cube_add(size=1, location=loc, scale=sc)
    bpy.ops.mesh.primitive_cube_add(size=0.3, location=(0.1, 0.1, 0.175))
    keep("corner_box")
    # Concave: a cup (open cylinder made solid).
    clear(); bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=0.5, depth=0.8)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='DESELECT')
    bpy.ops.object.mode_set(mode='OBJECT')
    ob = bpy.context.active_object
    top = max(ob.data.polygons, key=lambda p: p.center.z); top.select = True
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.delete(type='FACE'); bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.modifier_add(type='SOLIDIFY'); ob.modifiers[-1].thickness = 0.06
    bpy.ops.object.modifier_apply(modifier=ob.modifiers[-1].name); bpy.ops.object.shade_smooth()
    keep("cup")
    # Stairs: many parallel steps, hard edges.
    clear()
    for i in range(5):
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, i * 0.2, i * 0.1 + 0.05), scale=(1, 0.2, 0.1 + i * 0.2))
    keep("stairs")
    print("GEN", " ".join(made.values()))


def ao(glb, out, res, samples, distance, bake):
    os.makedirs(out, exist_ok=True)
    clear()
    bpy.ops.import_scene.gltf(filepath=glb)
    for o in list(bpy.context.scene.objects):
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)
    meshes = [o for o in bpy.context.scene.objects]
    for o in meshes:
        o.modifiers.clear()
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.active_object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    ob.data.materials.clear()
    if not bake:
        export(os.path.join(out, "joined.glb"))
    size = max(ob.dimensions)
    centre = sum((Vector(b) for b in ob.bound_box), Vector()) / 8

    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'
    sc.world = bpy.data.worlds.new("w"); sc.world.use_nodes = False; sc.world.color = (0, 0, 0)
    sc.world.light_settings.distance = distance * size
    sc.view_settings.view_transform = "Standard"
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = 512
    camd = bpy.data.cameras.new("c")  # default 50 mm on 36 mm: 39.6 deg
    co = bpy.data.objects.new("c", camd); sc.collection.objects.link(co); sc.camera = co
    # Same view as the Infinite Camera node: azimuth 50.2, elevation 17.75, 2.2 sizes away.
    d = Vector((1.0, -1.2, 0.5)).normalized() * size * 2.2
    co.location = centre + d
    co.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()

    mat = bpy.data.materials.new("m"); mat.use_nodes = True; ob.data.materials.append(mat)
    nt = mat.node_tree

    def emit(src):
        for n in [n for n in nt.nodes if n.type not in ('TEX_IMAGE', 'AMBIENT_OCCLUSION')]:
            nt.nodes.remove(n)
        em = nt.nodes.new("ShaderNodeEmission"); o = nt.nodes.new("ShaderNodeOutputMaterial")
        nt.links.new(src, em.inputs[0]); nt.links.new(em.outputs[0], o.inputs[0])

    for n in list(nt.nodes):
        nt.nodes.remove(n)
    a = nt.nodes.new("ShaderNodeAmbientOcclusion"); a.samples = samples
    a.inputs["Distance"].default_value = distance * size
    emit(a.outputs["AO"])
    if not bake:
        sc.cycles.samples = 64
        sc.render.filepath = os.path.join(out, "truth.png"); bpy.ops.render.render(write_still=True)
        return

    nt.nodes.remove(a)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.02)
    bpy.ops.uv.pack_islands(margin=0.004)
    bpy.ops.object.mode_set(mode='OBJECT')
    img = bpy.data.images.new("ao", res, res, float_buffer=True)
    tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = img; nt.nodes.active = tex
    sc.cycles.samples = samples; sc.render.bake.margin = 4
    bpy.ops.object.bake(type='AO')
    emit(tex.outputs[0])
    sc.cycles.samples = 16
    sc.render.filepath = os.path.join(out, "bake.png"); bpy.ops.render.render(write_still=True)


if args[0] == "gen":
    gen(args[1])
else:
    ao(args[1], args[2], int(args[3]), int(args[4]), float(args[5]), args[0] == "bake")
