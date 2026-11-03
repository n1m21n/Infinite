# Blender half of a physics case. Run by physics.py inside headless Blender:
#   Blender -b --factory-startup --python blender_physics.py -- <case dir>
# Reads <case>/scene.json (format in README.md, "Physics cases"), builds it, steps
# the simulation frame by frame and writes <case>/truth.jsonl in the same shape
# as Infinite's --geometry dump: one line per frame, world-space positions in
# Infinite's Y-up frame (Blender's (x, y, z) is (x, z, -y)). Rigid bodies also
# get their per-body position and rotation ("bodies"), and the objects go out as
# <case>/geo.glb (every body joined into one mesh, transforms baked) for Infinite.
import bpy, sys, os, json, math
from mathutils import Vector, Matrix

case = sys.argv[sys.argv.index("--") + 1]
scene = json.load(open(os.path.join(case, "scene.json")))
kind = scene["kind"]
fps = scene.get("fps", 30)
frames = int(round(scene["duration"] * fps)) + 1

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.fps = fps
sc.frame_start = 1
sc.frame_end = frames
g = scene.get("gravity", (0, 0, -9.8))
sc.gravity = g
sc.use_gravity = True


def yup(p):
    return (p[0], p[2], -p[1])


def add_shape(o, name):
    shape = o["shape"]
    s = o.get("size", 1.0)
    if shape == "cube":
        bpy.ops.mesh.primitive_cube_add(size=s)
    elif shape == "sphere":
        bpy.ops.mesh.primitive_uv_sphere_add(radius=s / 2, segments=32, ring_count=16)
    elif shape == "grid":
        n = o.get("subdivisions", 20)
        bpy.ops.mesh.primitive_grid_add(x_subdivisions=n, y_subdivisions=n, size=s)
    ob = bpy.context.object
    ob.name = name
    ob.location = o.get("location", (0, 0, 0))
    ob.rotation_euler = [math.radians(a) for a in o.get("rotation", (0, 0, 0))]
    if shape == "sphere":
        bpy.ops.object.shade_smooth()
    return ob


def ground_plane():
    gz = scene["ground"]
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, gz))
    return bpy.context.object


objs = [add_shape(o, "obj%d" % i) for i, o in enumerate(scene.get("objects", []))]

# Infinite gets the rest pose: every object, transforms baked, joined into one mesh.
def export_rest(path, sources):
    bpy.ops.object.select_all(action="DESELECT")
    copies = []
    for ob in sources:
        c = ob.copy()
        c.data = ob.data.copy()
        sc.collection.objects.link(c)
        copies.append(c)
    for c in copies:
        c.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if len(copies) > 1:
        bpy.ops.object.join()
    joined = bpy.context.object
    bpy.ops.object.select_all(action="DESELECT")
    joined.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                              export_apply=True, export_materials="NONE", export_animations=False)
    bpy.data.objects.remove(joined, do_unlink=True)


if objs:
    export_rest(os.path.join(case, "geo.glb"), objs)

# Cloth colliders: still objects the sheet drapes over, out as <case>/collider.glb.
cols = [add_shape(o, "col%d" % i) for i, o in enumerate(scene.get("colliders", []))]
if cols:
    export_rest(os.path.join(case, "collider.glb"), cols)

lines = []

if kind == "rigid":
    bpy.ops.rigidbody.world_add()
    w = sc.rigidbody_world
    w.substeps_per_frame = scene.get("substeps", 10)
    w.solver_iterations = scene.get("solver_iterations", 10)
    w.point_cache.frame_start = 1
    w.point_cache.frame_end = frames
    body = scene["body"]
    for ob, o in zip(objs, scene["objects"]):
        bpy.context.view_layer.objects.active = ob
        ob.select_set(True)
        bpy.ops.rigidbody.object_add(type="ACTIVE")
        rb = ob.rigid_body
        rb.collision_shape = {"cube": "BOX", "sphere": "SPHERE"}[o["shape"]] if body.get("collider") != "hull" else "CONVEX_HULL"
        rb.mass = body.get("mass", 1.0)
        rb.friction = body["friction"]
        rb.restitution = body["bounce"]
        rb.linear_damping = body["linear_damping"]
        rb.angular_damping = body["angular_damping"]
        rb.use_margin = True
        rb.collision_margin = 0.0
        ob.select_set(False)
    if scene.get("ground") is not None:
        gp = ground_plane()
        bpy.context.view_layer.objects.active = gp
        gp.select_set(True)
        bpy.ops.rigidbody.object_add(type="PASSIVE")
        # Bullet multiplies the two bodies' friction and restitution; a ground of 1
        # leaves the body's own values as the contact's.
        gp.rigid_body.friction = 1.0
        gp.rigid_body.restitution = 1.0
        gp.rigid_body.collision_shape = "BOX"
        gp.rigid_body.use_margin = True
        gp.rigid_body.collision_margin = 0.0
        gp.select_set(False)
    for f in range(1, frames + 1):
        sc.frame_set(f)
        bodies, verts = [], []
        for ob in objs:
            m = ob.matrix_world
            loc, rot, _ = m.decompose()
            # Rotation in the Y-up frame: conjugate by the axis change.
            R = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0)))
            ry = (R @ rot.to_matrix() @ R.transposed()).to_quaternion()
            bodies.append({"p": list(yup(loc)), "q": [ry.w, ry.x, ry.y, ry.z], "n": len(ob.data.vertices)})
            for v in ob.data.vertices:
                verts.extend(yup(m @ v.co))
        lines.append({"time": (f - 1) / fps, "frame": f - 1, "bodies": bodies, "v": verts})

elif kind == "cloth":
    ob = objs[0]
    c = scene["cloth"]
    pin = c.get("pin", "none")
    vg = ob.vertex_groups.new(name="pin")
    co = [ob.matrix_world @ v.co for v in ob.data.vertices]
    lo = [min(p[k] for p in co) for k in range(3)]
    hi = [max(p[k] for p in co) for k in range(3)]
    eps = [max(1e-4, (hi[k] - lo[k]) * 0.02) for k in range(3)]
    # The same pin rules as Infinite's Cloth (SimulationNodes.cpp), in Blender's axes:
    # Infinite's y is Blender's z, its x and z are Blender's x and y.
    pinned = []
    for i, p in enumerate(co):
        top = p.z >= hi[2] - eps[2]
        ex = p.x <= lo[0] + eps[0] or p.x >= hi[0] - eps[0]
        ey = p.y <= lo[1] + eps[1] or p.y >= hi[1] - eps[1]
        if (pin == "top" and top) or (pin == "corners" and ex and ey and (top or hi[2] - lo[2] < eps[2])) or \
           (pin == "edges" and (ex or ey)):
            pinned.append(i)
    if pinned:
        vg.add(pinned, 1.0, "REPLACE")
    mod = ob.modifiers.new("cloth", "CLOTH")
    s = mod.settings
    s.quality = c.get("quality", 5)
    s.mass = c.get("vertex_mass", 0.3)
    s.air_damping = c.get("air_damping", 1.0)
    s.tension_stiffness = s.compression_stiffness = c.get("tension", 15.0)
    s.shear_stiffness = c.get("shear", 5.0)
    s.bending_stiffness = c.get("bending", 0.5)
    s.effector_weights.gravity = 1.0
    if pinned:
        s.vertex_group_mass = "pin"
    mod.collision_settings.use_self_collision = False
    mod.point_cache.frame_start = 1
    mod.point_cache.frame_end = frames
    if scene.get("ground") is not None:
        gp = ground_plane()
        gp.modifiers.new("collision", "COLLISION")
        gp.collision.cloth_friction = c.get("friction", 5.0)
    for col in cols:
        col.modifiers.new("collision", "COLLISION")
        col.collision.cloth_friction = c.get("friction", 5.0)
    for f in range(1, frames + 1):
        sc.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = ob.evaluated_get(dg)
        m = ev.matrix_world
        verts = []
        for v in ev.data.vertices:
            verts.extend(yup(m @ v.co))
        lines.append({"time": (f - 1) / fps, "frame": f - 1, "v": verts})

elif kind == "particles":
    p = scene["particles"]
    bpy.ops.mesh.primitive_plane_add(size=0.001, location=p.get("location", (0, 0, 0)))
    em = bpy.context.object
    bpy.ops.object.particle_system_add()
    ps = em.particle_systems[0].settings
    rate = p["rate"]
    ps.count = int(round(rate * scene["duration"]))
    ps.frame_start = 1
    ps.frame_end = 1 + scene["duration"] * fps
    ps.lifetime = 10000
    ps.emit_from = "FACE"
    ps.use_emit_random = False
    ps.normal_factor = 0.0
    v = p["velocity"]
    ps.object_align_factor = v
    ps.factor_random = 0.0
    ps.physics_type = "NEWTON"
    ps.mass = 1.0
    ps.drag_factor = 0.0
    ps.brownian_factor = 0.0
    ps.timestep = 1.0 / fps
    ps.subframes = 4
    ps.effector_weights.gravity = 1.0
    em.particle_systems[0].point_cache.frame_start = 1
    em.particle_systems[0].point_cache.frame_end = frames
    for f in range(1, frames + 1):
        sc.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = em.evaluated_get(dg)
        pts = []
        for q in ev.particle_systems[0].particles:
            if q.alive_state == "ALIVE":
                pts.extend(yup(q.location))
        lines.append({"time": (f - 1) / fps, "frame": f - 1, "v": [], "p": pts})

with open(os.path.join(case, "truth.jsonl"), "w") as f:
    for l in lines:
        f.write(json.dumps(l) + "\n")
print("BLENDER_PHYSICS_DONE", case, len(lines))
