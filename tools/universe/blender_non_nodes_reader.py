#!/usr/bin/env python3
"""Exhaustive extractor for Blender's NON-NODE subsystems:
  - 312 Sculpt, Texture Paint, Vertex Paint, Weight, Grease Pencil & Curve Brushes
  - Animation Systems: F-Curves, F-Modifiers, Armatures, B-Bones, NLA, Shape Keys, Drivers
  - Physics Systems: Rigid Body, Cloth, Soft Body, Mantaflow Fluid, Dynamic Paint, 14 Force Fields, Particles
  - Physical Optics & Color Management: Cameras, OCIO AgX/Filmic, Frame Rates, Output formats
  - View Layers & Multi-Pass AOVs: Cryptomatte, Z, Diffuse/Glossy/Transmission Direct/Indirect
  - Video Sequence Editor (VSE): Strips, Retiming, Blends
Outputs to:
  - ~/Infinite/training-data/blender/blender_non_nodes_catalog.json
  - ~/Infinite/training-data/blender/blender_all_devices_complete.jsonl
"""

import bpy
import glob
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
        skip_keys = {'rna_type', 'name', 'type'}
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

print("1. Extracting 312 Native Brushes across all libraries...")
brush_files = glob.glob("/Applications/Blender.app/Contents/Resources/5.1/datafiles/assets/brushes/*.blend")
brush_catalog = {}

for bf in sorted(brush_files):
    cat_name = os.path.basename(bf).replace("essentials_brushes-", "").replace(".blend", "")
    brush_catalog[cat_name] = []
    with bpy.data.libraries.load(bf) as (data_from, data_to):
        data_to.brushes = data_from.brushes
    
    for b in data_to.brushes:
        if not b:
            continue
        b_info = {
            "name": b.name,
            "category": cat_name,
            "mode": getattr(b, 'mode', 'SCULPT'),
            "size": getattr(b, 'size', None),
            "strength": getattr(b, 'strength', None),
            "blend": getattr(b, 'blend', None),
            "use_airbrush": getattr(b, 'use_airbrush', None),
            "stroke_method": getattr(b, 'stroke_method', None),
            "curve_preset": getattr(b, 'curve_preset', None),
            "sculpt_tool": getattr(b, 'sculpt_tool', None),
        }
        brush_catalog[cat_name].append(b_info)
    
    # Clean up imported brushes to keep memory low
    for b in data_to.brushes:
        if b:
            bpy.data.brushes.remove(b)

total_brushes_extracted = sum(len(v) for v in brush_catalog.values())
print(f"   -> Extracted {total_brushes_extracted} Brushes")

print("2. Extracting Animation & Curve System (F-Modifiers, Interpolation, NLA, Drivers)...")
fmodifier_catalog = {}
fmod_enum = bpy.types.FModifier.bl_rna.properties['type'].enum_items
for item in fmod_enum:
    if item.identifier == 'NULL':
        continue
    cls_name = "FModifier" + item.name.replace(" ", "").replace("-", "").replace("(", "").replace(")", "")
    cls = getattr(bpy.types, cls_name, None) or getattr(bpy.types, "FModifier" + item.identifier.title(), None)
    fmodifier_catalog[item.identifier] = {
        "name": item.name,
        "description": item.description,
        "properties": dump_rna_properties(cls) if cls else {}
    }

animation_system = {
    "fcurve_modifiers": fmodifier_catalog,
    "interpolation_modes": [item.identifier for item in bpy.types.Keyframe.bl_rna.properties['interpolation'].enum_items],
    "easing_modes": [item.identifier for item in bpy.types.Keyframe.bl_rna.properties['easing'].enum_items],
    "nla_blend_modes": [item.identifier for item in bpy.types.NlaStrip.bl_rna.properties['blend_type'].enum_items],
    "nla_extrapolation": [item.identifier for item in bpy.types.NlaStrip.bl_rna.properties['extrapolation'].enum_items],
    "driver_types": [item.identifier for item in bpy.types.Driver.bl_rna.properties['type'].enum_items],
    "driver_variable_types": [item.identifier for item in bpy.types.DriverVariable.bl_rna.properties['type'].enum_items],
    "armature_bbone_settings": dump_rna_properties(bpy.types.Bone),
}

print("3. Extracting Physics & Simulation Systems...")
physics_system = {
    "force_fields": {
        item.identifier: {
            "name": item.name,
            "description": item.description
        } for item in bpy.types.FieldSettings.bl_rna.properties['type'].enum_items if item.identifier != 'NONE'
    },
    "field_properties": dump_rna_properties(bpy.types.FieldSettings),
    "rigid_body_world": dump_rna_properties(bpy.types.RigidBodyWorld),
    "rigid_body_object": dump_rna_properties(bpy.types.RigidBodyObject),
    "cloth_settings": dump_rna_properties(bpy.types.ClothSettings),
    "cloth_collision_settings": dump_rna_properties(bpy.types.ClothCollisionSettings),
    "soft_body_settings": dump_rna_properties(bpy.types.SoftBodySettings),
    "particle_settings": dump_rna_properties(bpy.types.ParticleSettings),
    "fluid_domain_settings": dump_rna_properties(bpy.types.FluidDomainSettings),
    "dynamic_paint_canvas": dump_rna_properties(bpy.types.DynamicPaintCanvasSettings),
    "dynamic_paint_brush": dump_rna_properties(bpy.types.DynamicPaintBrushSettings),
}

print("4. Extracting Optics, Color Management & Render Output...")
scene = bpy.context.scene
optics_and_output = {
    "color_management_view_transforms": [item.identifier for item in scene.view_settings.bl_rna.properties['view_transform'].enum_items],
    "color_management_looks": [item.identifier for item in scene.view_settings.bl_rna.properties['look'].enum_items],
    "color_management_displays": [item.identifier for item in scene.display_settings.bl_rna.properties['display_device'].enum_items],
    "view_settings": dump_rna_properties(scene.view_settings),
    "render_resolution_and_framerate": {
        "resolution_x": scene.render.resolution_x,
        "resolution_y": scene.render.resolution_y,
        "resolution_percentage": scene.render.resolution_percentage,
        "fps": scene.render.fps,
        "fps_base": scene.render.fps_base,
        "pixel_aspect_x": scene.render.pixel_aspect_x,
        "pixel_aspect_y": scene.render.pixel_aspect_y,
    },
    "image_format_settings": dump_rna_properties(scene.render.image_settings),
    "ffmpeg_video_settings": dump_rna_properties(scene.render.ffmpeg),
    "camera_sensor_and_optics": dump_rna_properties(bpy.types.Camera),
}

print("5. Extracting View Layer Multi-Pass AOVs & Render Passes...")
view_layer = scene.view_layers[0]
passes = {}
for p in view_layer.bl_rna.properties:
    if p.identifier.startswith("use_pass_"):
        passes[p.identifier] = {
            "name": p.name,
            "description": p.description,
            "default": getattr(view_layer, p.identifier, False)
        }
optics_and_output["view_layer_passes"] = passes

print("6. Extracting Video Sequence Editor (VSE) Engine...")
vse_engine = {
    "strip_base": dump_rna_properties(bpy.types.Strip),
    "strip_transform": dump_rna_properties(bpy.types.StripTransform),
    "strip_color_balance": dump_rna_properties(bpy.types.StripColorBalance),
    "strip_crop": dump_rna_properties(bpy.types.StripCrop),
    "sound_strip": dump_rna_properties(bpy.types.SoundStrip),
    "image_strip": dump_rna_properties(bpy.types.ImageStrip),
    "movie_strip": dump_rna_properties(bpy.types.MovieStrip),
    "text_strip": dump_rna_properties(bpy.types.TextStrip),
    "adjustment_strip": dump_rna_properties(bpy.types.AdjustmentStrip),
}

master_non_nodes = {
    "brushes": brush_catalog,
    "animation_and_rigging": animation_system,
    "physics_and_simulation": physics_system,
    "optics_color_and_output": optics_and_output,
    "video_sequence_editor": vse_engine
}

with open(os.path.join(OUTPUT_DIR, "blender_non_nodes_catalog.json"), "w") as f:
    json.dump(master_non_nodes, f, indent=2)

print("7. Generating Combined All-Inclusive JSONL (blender_all_devices_complete.jsonl)...")
combined_jsonl = os.path.join(OUTPUT_DIR, "blender_all_devices_complete.jsonl")

# Read existing 539 rows from nodes & modifiers
rows = []
existing_nodes_path = os.path.join(OUTPUT_DIR, "blender_nodes_devices.jsonl")
if os.path.exists(existing_nodes_path):
    with open(existing_nodes_path, "r") as f:
        for line in f:
            if line.strip():
                rows.append(json.loads(line))

# Add 312 Brushes
for cat, brushes in brush_catalog.items():
    for b in brushes:
        rows.append({
            "src": f"builtin::brush::{cat}",
            "engine": "blender",
            "library": "Blender 5.1 Essentials",
            "category": f"Brush ({cat})",
            "name": b["name"],
            "tags": {"subsystem": "sculpt_paint", "mode": b["mode"]},
            "modules": {
                b["name"]: {
                    "size": b["size"],
                    "strength": b["strength"],
                    "blend": b["blend"],
                    "stroke_method": b["stroke_method"],
                    "curve_preset": b["curve_preset"],
                    "sculpt_tool": b["sculpt_tool"]
                }
            },
            "license": "GPL/Blender Foundation"
        })

# Add F-Curve Modifiers
for f_id, f_data in fmodifier_catalog.items():
    rows.append({
        "src": "builtin::fmodifier",
        "engine": "blender",
        "library": "Blender 5.1 Builtin",
        "category": "Animation F-Curve Modifier",
        "name": f_data["name"],
        "tags": {"subsystem": "animation_graph_editor"},
        "modules": {
            f_id: {k: v.get("value") for k, v in f_data["properties"].items() if v.get("value") is not None}
        },
        "license": "GPL/Blender Foundation"
    })

# Add Force Fields
for ff_id, ff_data in physics_system["force_fields"].items():
    rows.append({
        "src": "builtin::force_field",
        "engine": "blender",
        "library": "Blender 5.1 Builtin",
        "category": "Physics Force Field",
        "name": ff_data["name"],
        "tags": {"subsystem": "physics_simulation"},
        "modules": {
            ff_id: {"type": ff_id, "name": ff_data["name"]}
        },
        "license": "GPL/Blender Foundation"
    })

# Add Physics Engines
for phys_name, phys_props in [
    ("RigidBodyWorld", physics_system["rigid_body_world"]),
    ("ClothSimulation", physics_system["cloth_settings"]),
    ("SoftBodySimulation", physics_system["soft_body_settings"]),
    ("ParticleSystem", physics_system["particle_settings"]),
    ("MantaflowFluid", physics_system["fluid_domain_settings"])
]:
    rows.append({
        "src": "builtin::physics_engine",
        "engine": "blender",
        "library": "Blender 5.1 Builtin",
        "category": "Physics Engine",
        "name": phys_name,
        "tags": {"subsystem": "physics_simulation"},
        "modules": {
            phys_name: {k: v.get("value") for k, v in phys_props.items() if v.get("value") is not None}
        },
        "license": "GPL/Blender Foundation"
    })

with open(combined_jsonl, "w") as f:
    for r in rows:
        f.write(json.dumps(r) + "\n")

print(f"\n=== NON-NODE EXTRACTION COMPLETE ===")
print(f"Total Devices/Nodes/Brushes in Master JSONL: {len(rows)}")
print(f"Saved catalog: {os.path.join(OUTPUT_DIR, 'blender_non_nodes_catalog.json')}")
print(f"Saved master dataset: {combined_jsonl}")