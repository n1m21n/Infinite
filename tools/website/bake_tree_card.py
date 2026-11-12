"""Blender (headless): render a scanned tree as a flat-lit RGBA card.

    Blender -b --factory-startup -P bake_tree_card.py -- tree.gltf out.png

Orthographic front view, transparent film, even white world light and no lamps,
so the card holds colour only and the page lights it.
"""
import math
import sys

import bpy
from mathutils import Vector

src, out = sys.argv[sys.argv.index('--') + 1:][:2]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

lo = Vector((1e9, 1e9, 1e9))
hi = Vector((-1e9, -1e9, -1e9))
for ob in bpy.context.scene.objects:
    if ob.type != 'MESH':
        continue
    for c in ob.bound_box:
        w = ob.matrix_world @ Vector(c)
        lo = Vector(map(min, lo, w))
        hi = Vector(map(max, hi, w))

width = max(hi.x - lo.x, hi.y - lo.y) * 1.04
height = (hi.z - lo.z) * 1.02
cx, cy = (lo.x + hi.x) / 2, (lo.y + hi.y) / 2

scene = bpy.context.scene
cam_data = bpy.data.cameras.new('cam')
cam_data.type = 'ORTHO'
cam_data.ortho_scale = max(width, height)
cam_data.clip_end = 1000
cam = bpy.data.objects.new('cam', cam_data)
scene.collection.objects.link(cam)
cam.location = (cx, cy - 50, lo.z + height / 2)
cam.rotation_euler = (math.radians(90), 0, 0)
scene.camera = cam

world = bpy.data.worlds.new('w')
world.use_nodes = True
bg = next(n for n in world.node_tree.nodes if n.type == 'BACKGROUND')
bg.inputs[0].default_value = (1, 1, 1, 1)
bg.inputs[1].default_value = 1.0
scene.world = world

engines = [i.identifier for i in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
scene.render.engine = next(e for e in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT', 'CYCLES') if e in engines)
if scene.render.engine == 'CYCLES':
    scene.cycles.samples = 48
scene.render.film_transparent = True
scene.view_settings.view_transform = 'Standard'
res_y = 2048
scene.render.resolution_y = res_y
scene.render.resolution_x = max(256, int(round(res_y * width / height / 4)) * 4)
cam_data.sensor_fit = 'VERTICAL'
cam_data.ortho_scale = height
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print('CARD', scene.render.resolution_x, res_y, 'aspect', round(width / height, 4))
