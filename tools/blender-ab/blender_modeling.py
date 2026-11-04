# Blender side of modeling.py. Two steps, each run by modeling.py:
#   blender -b --factory-startup --python blender_modeling.py -- run <dir>
#       reads <dir>/cases.json, runs each case's operations on its base mesh (an OBJ Infinite wrote) or builds the
#       primitive/generator, and writes <dir>/<case>.blender.obj (triangulated, Z-up)
#   blender -b --factory-startup --python blender_modeling.py -- render <dir> <case>
#       <case>.blender.obj and <case>.inf.obj side by side from one camera in Workbench, as <case>.a.png / .b.png
import bpy, bmesh, json, math, os, sys, traceback
from mathutils import Vector, Matrix

args = sys.argv[sys.argv.index("--") + 1:]
step, d = args[0], args[1]


def read_obj(path):
    vs, fs = [], []
    for l in open(path):
        p = l.split()
        if p and p[0] == "v":
            vs.append(tuple(map(float, p[1:4])))
        elif p and p[0] == "f":
            fs.append([int(x.split("/")[0]) - 1 for x in p[1:]])
    return vs, fs


def write_obj(bm, path):
    bm = bm.copy()
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.verts.ensure_lookup_table()
    with open(path, "w") as f:
        for v in bm.verts:
            f.write("v %.6f %.6f %.6f\n" % tuple(v.co))
        for fc in bm.faces:
            f.write("f %s\n" % " ".join(str(v.index + 1) for v in fc.verts))
    bm.free()


def from_obj(path):
    vs, fs = read_obj(path)
    bm = bmesh.new()
    for v in vs:
        bm.verts.new(v)
    bm.verts.ensure_lookup_table()
    for f in fs:
        try:
            bm.faces.new([bm.verts[i] for i in f])
        except ValueError:
            pass
    # Infinite splits points along UV seams and flat edges: weld them as Blender would on import.
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    return bm


def ok(p, sel):
    """A (centre, normal) pick: normal within `min` dot of `normal`, and `where` = [axis, '<'|'>', value] tests on the centre."""
    c, n = p
    if "normal" in sel and Vector(n).dot(Vector(sel["normal"])) < sel.get("min", 0.9):
        return False
    for ax, op, val in sel.get("where", []):
        if (op == ">" and not c[ax] > val) or (op == "<" and not c[ax] < val):
            return False
    return True


def pick_faces(bm, sel):
    return [f for f in bm.faces if ok((f.calc_center_median(), f.normal), sel)]


def pick_verts(bm, sel):
    return [v for v in bm.verts if ok((v.co, None), {k: x for k, x in sel.items() if k != "normal"})]


def verts_of(faces):
    s = set()
    for f in faces:
        s.update(f.verts)
    return list(s)


def median(vs):
    return sum((v.co for v in vs), Vector()) / len(vs)


def mod_apply(bm, kind, **kw):
    me = bpy.data.meshes.new("t")
    bm.to_mesh(me)
    ob = bpy.data.objects.new("t", me)
    bpy.context.scene.collection.objects.link(ob)
    m = ob.modifiers.new("m", kind)
    for k, v in kw.items():
        setattr(m, k, v)
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg).to_mesh()
    out = bmesh.new()
    out.from_mesh(ev)
    ob.evaluated_get(dg).to_mesh_clear()
    bpy.data.objects.remove(ob)
    bpy.data.meshes.remove(me)
    return out


def edit_ops(bm, ops):
    cur = []   # the selection the next op acts on, as Infinite carries it between ops
    for op in ops:
        k = op["op"]
        if "faces" in op:
            cur = pick_faces(bm, op["faces"])
        elif "verts" in op:
            cur = pick_verts(bm, op["verts"])
        elif "edge" in op:
            a, b = (min(bm.verts, key=lambda v: (v.co - Vector(p)).length) for p in op["edge"])
            e = next(e for e in bm.edges if set(e.verts) == {a, b})
        if k == "extrude":
            fs = [f for f in cur if f.is_valid]
            nrm = sum((f.normal * f.calc_area() for f in fs), Vector()).normalized() * op["d"]
            r = bmesh.ops.extrude_face_region(bm, geom=fs)
            nv = [g for g in r["geom"] if isinstance(g, bmesh.types.BMVert)]
            bmesh.ops.translate(bm, verts=nv, vec=nrm)
            bmesh.ops.delete(bm, geom=fs, context='FACES_ONLY')   # Blender's Extrude Region replaces the region
            cur = [g for g in r["geom"] if isinstance(g, bmesh.types.BMFace)]
        elif k == "inset":
            bmesh.ops.inset_region(bm, faces=cur, thickness=op["w"], depth=0.0, use_even_offset=True, use_boundary=True)
        elif k == "delete_faces":
            bmesh.ops.delete(bm, geom=cur, context='FACES'); cur = []
        elif k == "delete_edge":
            bmesh.ops.delete(bm, geom=[e], context='EDGES'); cur = []
        elif k == "delete_verts":
            bmesh.ops.delete(bm, geom=cur, context='VERTS'); cur = []
        elif k in ("move", "rotate", "scale"):
            vs = cur if cur and isinstance(cur[0], bmesh.types.BMVert) else verts_of(cur)
            c = median(vs)
            if k == "move":
                bmesh.ops.translate(bm, verts=vs, vec=Vector(op["v"]))
            elif k == "rotate":
                bmesh.ops.rotate(bm, verts=vs, cent=c, matrix=Matrix.Rotation(math.radians(op["deg"]), 3, Vector(op["axis"])))
            else:
                bmesh.ops.scale(bm, verts=vs, vec=Vector(op["v"]), space=Matrix.Translation(-c))
    return bm


def run_case(name, c):
    kind = c["kind"]
    if kind == "edit":
        bm = from_obj(os.path.join(d, name + ".base.obj"))
        if c.get("quads") == "dissolve":
            bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(1), verts=bm.verts[:], edges=bm.edges[:])
        elif c.get("quads") == "join":
            bmesh.ops.join_triangles(bm, faces=bm.faces[:], angle_face_threshold=math.radians(60), angle_shape_threshold=math.radians(60))
        bm.normal_update()
        for g in c.get("geo", []):
            g_op = g["op"]
            if g_op == "subdivide":
                bm = mod_apply(bm, 'SUBSURF', levels=g["levels"], render_levels=g["levels"], subdivision_type='CATMULL_CLARK')
            elif g_op == "smooth":
                bm = mod_apply(bm, 'SMOOTH', factor=g["strength"], iterations=g["iters"])
            elif g_op == "twist":
                bm = mod_apply(bm, 'SIMPLE_DEFORM', deform_method='TWIST', angle=math.radians(g["deg"]), deform_axis='XYZ'[g["axis"]])
            elif g_op == "bevel":
                bm = mod_apply(bm, 'BEVEL', width=g["w"], segments=g["segs"], limit_method='ANGLE', angle_limit=math.radians(g["angle"]))
        if c.get("ops"):
            bm.normal_update()
            edit_ops(bm, c["ops"])
        return bm
    bm = bmesh.new()
    if kind == "prim":
        s = c["shape"]
        mk = {"cube": lambda: bmesh.ops.create_cube(bm, size=1.0),
              "sphere": lambda: bmesh.ops.create_uvsphere(bm, u_segments=c["sectors"], v_segments=c["rings"], radius=0.5),
              "cylinder": lambda: bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=c["sides"], radius1=0.5, radius2=0.5 * c.get("top", 1.0), depth=1.0),
              "torus": None}[s]
        if s == "torus":
            major, minor = 0.5 - c["tube"] * 0.5, c["tube"] * 0.5
            bpy.ops.mesh.primitive_torus_add(major_segments=c["rings"], minor_segments=c["sides"], major_radius=major, minor_radius=minor)
            bm.from_mesh(bpy.context.object.data)
        else:
            mk()
        if s == "cube" and c.get("seg", 1) > 1:   # Infinite's segments = quads per side
            bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=c["seg"] - 1, use_grid_fill=True)
    elif kind == "revolve":
        pts = c["profile"]
        vs = [bm.verts.new(p) for p in pts]
        for a, b in zip(vs, vs[1:]):
            bm.edges.new((a, b))
        bm = mod_apply(bm, 'SCREW', angle=math.radians(c["deg"]), steps=c["steps"], render_steps=c["steps"], axis='XYZ'[c["axis"]],
                       use_merge_vertices=True, merge_threshold=1e-5, screw_offset=0.0, iterations=1)
    elif kind == "sweep":
        cu = bpy.data.curves.new("path", 'CURVE')
        cu.dimensions = '3D'
        sp = cu.splines.new('POLY')
        sp.points.add(len(c["path"]) - 1)
        for p, q in zip(sp.points, c["path"]):
            p.co = (*q, 1.0)
        sp.use_cyclic_u = c.get("closed_path", False)
        prof = bpy.data.curves.new("prof", 'CURVE')
        prof.dimensions = '2D'
        ps = prof.splines.new('POLY')
        ps.points.add(len(c["profile"]) - 1)
        for p, q in zip(ps.points, c["profile"]):
            p.co = (q[0], q[1], 0.0, 1.0)
        ps.use_cyclic_u = True
        po = bpy.data.objects.new("prof", prof)
        bpy.context.scene.collection.objects.link(po)
        cu.bevel_mode = 'OBJECT'
        cu.bevel_object = po
        cu.use_fill_caps = c.get("caps", True)
        cu.twist_mode = 'MINIMUM'
        cu.resolution_u = 1
        ob = bpy.data.objects.new("sweep", cu)
        bpy.context.scene.collection.objects.link(ob)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        bm.from_mesh(me)
        ev.to_mesh_clear()
    return bm


def main_run():
    cases = json.load(open(os.path.join(d, "cases.json")))
    for name, c in cases.items():
        try:
            bpy.ops.wm.read_factory_settings(use_empty=True)
            write_obj(run_case(name, c), os.path.join(d, name + ".blender.obj"))
            print("MODELCASE", name)
        except Exception:
            traceback.print_exc()
            print("MODELFAIL", name)
    print("MODELDONE")


def main_render(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'OBJECT'
    sc.render.resolution_x = sc.render.resolution_y = 640
    sc.display.shading.show_object_outline = False
    pts = []
    for tag in ("blender", "inf"):
        path = os.path.join(d, "%s.%s.obj" % (name, tag))
        bm = from_obj(path) if os.path.exists(path) else bmesh.new()
        pts += [v.co.copy() for v in bm.verts]
    lo = Vector((min(p[k] for p in pts) for k in range(3))) if pts else Vector()
    hi = Vector((max(p[k] for p in pts) for k in range(3))) if pts else Vector()
    ctr, size = (lo + hi) / 2, max((hi - lo).length, 1e-3)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = size * 1.1
    az, el = math.radians(35), math.radians(30)
    cam.location = ctr + 4 * size * Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
    cam.rotation_euler = (ctr - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = cam
    for tag, png in (("blender", "a"), ("inf", "b")):
        path = os.path.join(d, "%s.%s.obj" % (name, tag))
        for o in list(bpy.data.objects):
            if o.type == 'MESH':
                bpy.data.objects.remove(o)
        if os.path.exists(path):
            bm = from_obj(path)
            me = bpy.data.meshes.new(tag)
            bm.to_mesh(me)
            for wire in (False, True):
                ob = bpy.data.objects.new(tag, me)
                sc.collection.objects.link(ob)
                ob.color = (0.12, 0.12, 0.12, 1) if wire else (0.75, 0.78, 0.85, 1)
                if wire:
                    m = ob.modifiers.new("w", 'WIREFRAME')
                    m.thickness = size * 0.004
                    m.use_even_offset = True
        sc.render.filepath = os.path.join(d, "%s.%s.png" % (name, png))
        bpy.ops.render.render(write_still=True)
    print("MODELRENDER", name)


try:
    if step == "run":
        main_run()
    else:
        main_render(args[2])
except Exception:
    traceback.print_exc()
