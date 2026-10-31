#!/usr/bin/env python3
"""Exhaustive Blender device, node, modifier, shader, and setting extractor.
Extracts every parameter, socket, default, range, and hardware setting into:
  - ~/Infinite/training-data/blender/blender_environment.json
  - ~/Infinite/training-data/blender/blender_device_catalog.json
  - ~/Infinite/training-data/blender/blender_nodes_devices.jsonl
  - ~/Infinite/training-data/blender/blender_projects_scanned.json
"""

import bpy
import json
import os
import sys

OUTPUT_DIR = os.path.expanduser("~/Infinite/training-data/blender")
os.makedirs(OUTPUT_DIR, exist_ok=True)

def serialize_val(v):
    if v is None or isinstance(v, (int, float, str, bool)):
        return v
    if hasattr(v, '__len__') and not isinstance(v, (str, bytes, dict)):
        return [serialize_val(x) for x in v]
    return str(v)

def dump_rna_properties(rna_obj, skip_keys=None):
    if skip_keys is None:
        skip_keys = {'rna_type', 'name', 'type', 'inputs', 'outputs', 'internal_links', 'parent'}
    res = {}
    if not hasattr(rna_obj, 'bl_rna'):
        return res
    for prop in rna_obj.bl_rna.properties:
        if prop.identifier in skip_keys:
            continue
        try:
            val = getattr(rna_obj, prop.identifier)
            p_info = {
                "name": prop.name,
                "description": prop.description,
                "type": prop.type,
                "value": serialize_val(val)
            }
            if prop.type in ('FLOAT', 'INT'):
                p_info["hard_min"] = getattr(prop, 'hard_min', None)
                p_info["hard_max"] = getattr(prop, 'hard_max', None)
                p_info["soft_min"] = getattr(prop, 'soft_min', None)
                p_info["soft_max"] = getattr(prop, 'soft_max', None)
                p_info["default"] = getattr(prop, 'default', None)
            elif prop.type == 'ENUM':
                p_info["items"] = [item.identifier for item in prop.enum_items]
                p_info["default"] = getattr(prop, 'default', None)
            elif prop.type == 'BOOLEAN':
                p_info["default"] = getattr(prop, 'default', None)
            res[prop.identifier] = p_info
        except Exception:
            pass
    return res

def extract_node_sockets_and_props(node):
    inputs = {}
    for inp in node.inputs:
        val = getattr(inp, 'default_value', None)
        inputs[inp.name] = {
            "identifier": inp.identifier,
            "type": inp.type,
            "default": serialize_val(val)
        }
    outputs = {}
    for out in node.outputs:
        outputs[out.name] = {
            "identifier": out.identifier,
            "type": out.type
        }
    node_props = dump_rna_properties(node, skip_keys={'rna_type', 'name', 'type', 'inputs', 'outputs', 'internal_links', 'parent', 'location', 'location_absolute', 'dimensions', 'width', 'height'})
    return {
        "inputs": inputs,
        "outputs": outputs,
        "properties": node_props
    }

print("1. Extracting System & Environment Preferences...")
prefs = bpy.context.preferences
cprefs = prefs.addons['cycles'].preferences if 'cycles' in prefs.addons else None
if cprefs and hasattr(cprefs, 'get_devices'):
    try:
        cprefs.get_devices()
    except Exception:
        pass

cycles_devices = []
if cprefs:
    for d in cprefs.devices:
        cycles_devices.append({
            "name": d.name,
            "type": d.type,
            "use": d.use,
            "id": getattr(d, 'id', '')
        })

env_info = {
    "blender_version": bpy.app.version_string,
    "build_branch": str(bpy.app.build_branch),
    "build_hash": str(bpy.app.build_hash),
    "build_commit_date": str(bpy.app.build_commit_date),
    "binary_path": bpy.app.binary_path,
    "python_version": sys.version,
    "python_executable": sys.executable,
    "paths": {
        "user_config": bpy.utils.user_resource('CONFIG'),
        "user_scripts": bpy.utils.user_resource('SCRIPTS'),
        "user_datafiles": bpy.utils.user_resource('DATAFILES'),
    },
    "system_preferences": dump_rna_properties(prefs.system),
    "view_preferences": dump_rna_properties(prefs.view),
    "edit_preferences": dump_rna_properties(prefs.edit),
    "filepath_preferences": dump_rna_properties(prefs.filepaths),
    "input_preferences": dump_rna_properties(prefs.inputs),
    "cycles_compute_device_type": getattr(cprefs, 'compute_device_type', None),
    "cycles_devices": cycles_devices,
    "enabled_addons": list(prefs.addons.keys()),
    "asset_libraries": [{"name": lib.name, "path": lib.path} for lib in prefs.filepaths.asset_libraries]
}

with open(os.path.join(OUTPUT_DIR, "blender_environment.json"), "w") as f:
    json.dump(env_info, f, indent=2)

print("2. Extracting Shader Nodes (Material Synths)...")
shader_catalog = {}
mat = bpy.data.materials.new("Catalog_Shader_Mat")
mat_tree = mat.node_tree
for name in dir(bpy.types):
    if name.startswith("ShaderNode") and name not in ("ShaderNode", "ShaderNodeTree", "ShaderNodeGroup"):
        try:
            node = mat_tree.nodes.new(name)
            shader_catalog[name] = {
                "bl_idname": node.bl_idname,
                "label": node.label or node.name,
                **extract_node_sockets_and_props(node)
            }
            mat_tree.nodes.remove(node)
        except Exception as e:
            pass
bpy.data.materials.remove(mat)

print(f"   -> Extracted {len(shader_catalog)} Shader Nodes")

print("3. Extracting Geometry Nodes (Procedural / Modular Engine)...")
geo_catalog = {}
geo_group = bpy.data.node_groups.new("Catalog_Geo_Tree", 'GeometryNodeTree')
for name in dir(bpy.types):
    if name.startswith("GeometryNode") and name not in ("GeometryNode", "GeometryNodeTree", "GeometryNodeGroup"):
        try:
            node = geo_group.nodes.new(name)
            geo_catalog[name] = {
                "bl_idname": node.bl_idname,
                "label": node.label or node.name,
                **extract_node_sockets_and_props(node)
            }
            geo_group.nodes.remove(node)
        except Exception as e:
            pass
bpy.data.node_groups.remove(geo_group)

print(f"   -> Extracted {len(geo_catalog)} Geometry Nodes")

print("4. Extracting Compositor Nodes (Mastering / FX)...")
cmp_catalog = {}
cmp_group = bpy.data.node_groups.new("Catalog_Cmp_Tree", 'CompositorNodeTree')
for name in dir(bpy.types):
    if name.startswith("CompositorNode") and name not in ("CompositorNode", "CompositorNodeTree", "CompositorNodeGroup"):
        try:
            node = cmp_group.nodes.new(name)
            cmp_catalog[name] = {
                "bl_idname": node.bl_idname,
                "label": node.label or node.name,
                **extract_node_sockets_and_props(node)
            }
            cmp_group.nodes.remove(node)
        except Exception as e:
            pass
bpy.data.node_groups.remove(cmp_group)

print(f"   -> Extracted {len(cmp_catalog)} Compositor Nodes")

print("5. Extracting Modifiers (Geometry FX & Generators)...")
modifier_catalog = {}
mesh = bpy.data.meshes.new("Catalog_Mesh")
obj = bpy.data.objects.new("Catalog_Obj", mesh)
bpy.context.scene.collection.objects.link(obj)

mod_enum = bpy.types.Modifier.bl_rna.properties['type'].enum_items
for item in mod_enum:
    mod_type = item.identifier
    try:
        mod = obj.modifiers.new(name=item.name, type=mod_type)
        modifier_catalog[mod_type] = {
            "name": item.name,
            "description": item.description,
            "properties": dump_rna_properties(mod, skip_keys={'rna_type', 'name', 'type'})
        }
        obj.modifiers.remove(mod)
    except Exception as e:
        cls_name = mod_type.title().replace("_", "") + "Modifier"
        cls = getattr(bpy.types, cls_name, None)
        if cls:
            modifier_catalog[mod_type] = {
                "name": item.name,
                "description": item.description,
                "properties": dump_rna_properties(cls, skip_keys={'rna_type', 'name', 'type'})
            }

print(f"   -> Extracted {len(modifier_catalog)} Modifiers")

print("6. Extracting Constraints (Modulation / Tracking / Relationship)...")
constraint_catalog = {}
const_enum = bpy.types.Constraint.bl_rna.properties['type'].enum_items
for item in const_enum:
    c_type = item.identifier
    try:
        c = obj.constraints.new(type=c_type)
        constraint_catalog[c_type] = {
            "name": item.name,
            "description": item.description,
            "properties": dump_rna_properties(c, skip_keys={'rna_type', 'name', 'type'})
        }
        obj.constraints.remove(c)
    except Exception as e:
        pass

bpy.data.objects.remove(obj)
bpy.data.meshes.remove(mesh)
print(f"   -> Extracted {len(constraint_catalog)} Constraints")

print("7. Extracting Render Engines, Scene, Camera, Light, and World Settings...")
scene = bpy.context.scene
render_engines = {
    "cycles": dump_rna_properties(scene.cycles) if hasattr(scene, 'cycles') else {},
    "eevee": dump_rna_properties(scene.eevee) if hasattr(scene, 'eevee') else {},
    "scene_render": dump_rna_properties(scene.render),
    "view_settings": dump_rna_properties(scene.view_settings),
}

# Lights
lights_catalog = {}
for light_type in ('POINT', 'SUN', 'SPOT', 'AREA'):
    l_data = bpy.data.lights.new(name=f"Catalog_{light_type}", type=light_type)
    lights_catalog[light_type] = dump_rna_properties(l_data, skip_keys={'rna_type', 'name', 'type'})
    bpy.data.lights.remove(l_data)

# Camera
cam_data = bpy.data.cameras.new("Catalog_Camera")
camera_catalog = dump_rna_properties(cam_data, skip_keys={'rna_type', 'name', 'type'})
bpy.data.cameras.remove(cam_data)

# World
world_data = bpy.data.worlds.new("Catalog_World")
world_catalog = dump_rna_properties(world_data, skip_keys={'rna_type', 'name', 'type'})
bpy.data.worlds.remove(world_data)

master_catalog = {
    "render_engines": render_engines,
    "lights": lights_catalog,
    "camera": camera_catalog,
    "world": world_catalog,
    "shader_nodes": shader_catalog,
    "geometry_nodes": geo_catalog,
    "compositor_nodes": cmp_catalog,
    "modifiers": modifier_catalog,
    "constraints": constraint_catalog
}

with open(os.path.join(OUTPUT_DIR, "blender_device_catalog.json"), "w") as f:
    json.dump(master_catalog, f, indent=2)

print("8. Writing Flat JSONL (Compatible with ableton.jsonl format)...")
jsonl_path = os.path.join(OUTPUT_DIR, "blender_nodes_devices.jsonl")
with open(jsonl_path, "w") as f:
    # Shader Nodes
    for name, data in shader_catalog.items():
        row = {
            "src": "builtin::shader_node",
            "engine": "blender",
            "library": "Blender 5.1 Builtin",
            "category": "Shader Node",
            "name": name,
            "tags": {"subsystem": "cycles/eevee/shading"},
            "modules": {
                name: {
                    **{f"in_{k}": v["default"] for k, v in data["inputs"].items() if v["default"] is not None},
                    **{f"prop_{k}": v["value"] for k, v in data["properties"].items() if v.get("value") is not None}
                }
            },
            "license": "GPL/Blender Foundation"
        }
        f.write(json.dumps(row) + "\n")

    # Geometry Nodes
    for name, data in geo_catalog.items():
        row = {
            "src": "builtin::geometry_node",
            "engine": "blender",
            "library": "Blender 5.1 Builtin",
            "category": "Geometry Node",
            "name": name,
            "tags": {"subsystem": "geometry_nodes/procedural"},
            "modules": {
                name: {
                    **{f"in_{k}": v["default"] for k, v in data["inputs"].items() if v["default"] is not None},
                    **{f"prop_{k}": v["value"] for k, v in data["properties"].items() if v.get("value") is not None}
                }
            },
            "license": "GPL/Blender Foundation"
        }
        f.write(json.dumps(row) + "\n")

    # Compositor Nodes
    for name, data in cmp_catalog.items():
        row = {
            "src": "builtin::compositor_node",
            "engine": "blender",
            "library": "Blender 5.1 Builtin",
            "category": "Compositor Node",
            "name": name,
            "tags": {"subsystem": "compositor/grading"},
            "modules": {
                name: {
                    **{f"in_{k}": v["default"] for k, v in data["inputs"].items() if v["default"] is not None},
                    **{f"prop_{k}": v["value"] for k, v in data["properties"].items() if v.get("value") is not None}
                }
            },
            "license": "GPL/Blender Foundation"
        }
        f.write(json.dumps(row) + "\n")

    # Modifiers
    for name, data in modifier_catalog.items():
        row = {
            "src": "builtin::modifier",
            "engine": "blender",
            "library": "Blender 5.1 Builtin",
            "category": "Modifier",
            "name": name,
            "tags": {"subsystem": "modifiers/mesh_deform_generate"},
            "modules": {
                name: {k: v["value"] for k, v in data["properties"].items() if v.get("value") is not None}
            },
            "license": "GPL/Blender Foundation"
        }
        f.write(json.dumps(row) + "\n")

    # Constraints
    for name, data in constraint_catalog.items():
        row = {
            "src": "builtin::constraint",
            "engine": "blender",
            "library": "Blender 5.1 Builtin",
            "category": "Constraint",
            "name": name,
            "tags": {"subsystem": "constraints/animation_rigging"},
            "modules": {
                name: {k: v["value"] for k, v in data["properties"].items() if v.get("value") is not None}
            },
            "license": "GPL/Blender Foundation"
        }
        f.write(json.dumps(row) + "\n")

print("9. Scanning local .blend files for actual scene trees & parameter usage...")
user_blend_files = [
    os.path.expanduser("~/Downloads/Assets/blue_metal_plate_4k.blend/blue_metal_plate_4k.blend"),
    os.path.expanduser("~/Downloads/Assets/gravelly_sand_4k.blend/gravelly_sand_4k.blend"),
    os.path.expanduser("~/Downloads/Assets/Plaster001_1K-JPG/Plaster001_1K-JPG.blend")
]

scanned_projects = []
for blend_path in user_blend_files:
    if not os.path.exists(blend_path):
        continue
    try:
        # Load blend file in headless mode
        bpy.ops.wm.open_mainfile(filepath=blend_path)
        proj_summary = {
            "file": blend_path,
            "name": os.path.basename(blend_path),
            "scenes": [s.name for s in bpy.data.scenes],
            "objects": [],
            "materials": [],
            "textures": [img.name for img in bpy.data.images],
        }
        for o in bpy.data.objects:
            obj_data = {
                "name": o.name,
                "type": o.type,
                "location": list(o.location),
                "rotation": list(o.rotation_euler),
                "scale": list(o.scale),
                "modifiers": [m.name + f" ({m.type})" for m in o.modifiers],
                "constraints": [c.name + f" ({c.type})" for c in o.constraints]
            }
            proj_summary["objects"].append(obj_data)
        for m in bpy.data.materials:
            mat_info = {
                "name": m.name,
                "nodes": [n.name + f" ({n.type})" for n in m.node_tree.nodes] if m.node_tree else []
            }
            proj_summary["materials"].append(mat_info)
        scanned_projects.append(proj_summary)
    except Exception as e:
        print(f"Error scanning {blend_path}: {e}")

with open(os.path.join(OUTPUT_DIR, "blender_projects_scanned.json"), "w") as f:
    json.dump(scanned_projects, f, indent=2)

print("\n=== BLENDER EXTRACTION COMPLETE ===")
print(f"Artifacts successfully stored in: {OUTPUT_DIR}")