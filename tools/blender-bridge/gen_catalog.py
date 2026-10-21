# Run: Blender -b -P gen_catalog.py -- OUT.json
# Asks Blender itself (never its source) what it can do: every operator with its typed properties.
import bpy, sys, json
out = sys.argv[sys.argv.index("--") + 1]
ops = []
for mod in dir(bpy.ops):
    if mod.startswith("_"): continue
    for name in dir(getattr(bpy.ops, mod)):
        if name.startswith("_"): continue
        try:
            rna = getattr(getattr(bpy.ops, mod), name).get_rna_type()
        except Exception:
            continue
        props = [{"key": p.identifier, "type": p.type, "default": getattr(p, "default", None) if p.type in ("BOOLEAN","INT","FLOAT","STRING","ENUM") and not getattr(p, "is_array", False) else None}
                 for p in rna.properties if p.identifier != "rna_type"]
        ops.append({"id": f"{mod}.{name}", "doc": rna.description, "props": props})
json.dump({"blender": bpy.app.version_string, "count": len(ops), "ops": ops}, open(out, "w"), default=str)
print(json.dumps({"ok": True, "blender": bpy.app.version_string, "operators": len(ops)}))
