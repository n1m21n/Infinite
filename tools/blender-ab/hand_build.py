# Row 5.6: builds the photo_hand case's meshes once, headless, into assets/hand/ (git-ignored):
#   Blender -b --factory-startup -P tools/blender-ab/hand_build.py
# hand.glb: a relaxed adult hand resting palm down on a table, fingers toward +Y, wrist and a bit of forearm
# toward -Y, from metaball capsules (palm, three bones a finger, thumb) remeshed and smoothed, with knuckle and
# joint creases pressed in; nails.glb: the five nails as thin shells on the fingertips. Blender Z-up metres,
# placed where the case wants them (the case moves nothing). Geometry only: the case gives the materials.
import bpy, bmesh, math, os
from mathutils import Vector, Quaternion

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets", "hand")
os.makedirs(HERE, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene

mb = bpy.data.metaballs.new("hand")
mb.resolution = 0.0018
mb.render_resolution = 0.0018
mb.threshold = 0.6
ob = bpy.data.objects.new("hand", mb)
sc.collection.objects.link(ob)


# A metaball's field reaches zero at its radius and the surface sits where it falls to the threshold, about half
# way in; FIELD scales every radius below (the thickness wanted) to the element radius that gives it.
FIELD = 1.9


def capsule(a, b, r, stiff=2.0):
    """A metaball capsule from point a to point b, about r thick (radius)."""
    a, b = Vector(a), Vector(b)
    e = mb.elements.new(type='CAPSULE')
    e.co = (a + b) / 2
    e.radius = r * FIELD
    e.size_x = (b - a).length / 2
    e.rotation = Vector((1, 0, 0)).rotation_difference((b - a).normalized())
    e.stiffness = stiff


def ellipsoid(c, size, r=None, rot=(1, 0, 0, 0), stiff=2.0):
    """An ellipsoid of half-axes `size` (Blender scales the radius by each size, so radius 1 x half-axis)."""
    e = mb.elements.new(type='ELLIPSOID')
    e.co = c
    e.radius = FIELD
    e.size_x, e.size_y, e.size_z = size
    e.rotation = Quaternion(rot)
    e.stiffness = stiff


# Palm: a flattened block, thicker at the heel and under the thumb; the wrist narrows into the forearm.
ellipsoid((0.0, 0.005, 0.016), (0.042, 0.052, 0.013))
ellipsoid((-0.024, -0.028, 0.014), (0.022, 0.028, 0.013))   # thenar (thumb ball)
ellipsoid((0.022, -0.030, 0.013), (0.019, 0.026, 0.012))    # hypothenar
capsule((0.0, -0.055, 0.018), (0.0, -0.200, 0.030), 0.022, 1.5)    # wrist into forearm
capsule((-0.012, -0.060, 0.017), (-0.014, -0.200, 0.028), 0.017, 1.5)
capsule((0.012, -0.060, 0.017), (0.014, -0.200, 0.028), 0.017, 1.5)

# Fingers: knuckle (x, y), bone lengths, radius at the base; each bone bends down a little more, so the
# tips just touch the table and the knuckles stand up as the highest points.
FINGERS = [  # (knuckle x, knuckle y, splay deg, bones, radius)
    (-0.030, 0.050, -8, (0.044, 0.026, 0.021), 0.0095),   # index
    (-0.010, 0.054, -2, (0.048, 0.030, 0.023), 0.0098),   # middle
    (0.010, 0.051, 4, (0.045, 0.028, 0.022), 0.0092),     # ring
    (0.028, 0.044, 11, (0.035, 0.021, 0.019), 0.0082),    # little
]
TIPS = []
for kx, ky, splay, bones, r in FINGERS:
    p = Vector((kx, ky, 0.027))
    heading = math.radians(splay)
    pitch = [math.radians(d) for d in (-6, -18, -16)]
    for k, (L, pt) in enumerate(zip(bones, pitch)):
        d = Vector((math.sin(heading) * math.cos(pt), math.cos(heading) * math.cos(pt), math.sin(pt)))
        q = p + d * L
        rk = r * (1.0, 0.9, 0.82)[k]
        capsule(p, q, rk)
        p = q
    TIPS.append((p.copy(), d.copy(), r * 0.82))
# Thumb: from the thenar out toward the index side, lying lower and rolled.
p = Vector((-0.032, -0.022, 0.022))
for L, d, rk in ((0.040, Vector((-0.65, 0.70, -0.05)), 0.0125), (0.032, Vector((-0.40, 0.90, -0.12)), 0.0108),
                 (0.026, Vector((-0.25, 0.95, -0.22)), 0.0098)):
    q = p + d.normalized() * L
    capsule(p, q, rk)
    p = q
TIPS.append((p.copy(), d.normalized(), 0.0098))

# Metaballs -> mesh, remeshed even, smoothed.
bpy.context.view_layer.objects.active = ob
ob.select_set(True)
bpy.ops.object.convert(target='MESH')
hand = bpy.context.active_object
rm = hand.modifiers.new("rm", 'REMESH'); rm.mode = 'VOXEL'; rm.voxel_size = 0.0012
sm = hand.modifiers.new("sm", 'SMOOTH'); sm.iterations = 6; sm.factor = 0.6
for m in list(hand.modifiers):
    bpy.ops.object.modifier_apply(modifier=m.name)

# Creases: across each finger joint on the back of the hand, a narrow trough a fraction of a millimetre deep,
# so the grazing light the photo case uses catches folds rather than a rubber glove.
bm = bmesh.new(); bm.from_mesh(hand.data)
bm.normal_update()
joints = []
for kx, ky, splay, bones, r in FINGERS:
    p = Vector((kx, ky, 0.027)); heading = math.radians(splay)
    for k, (L, pt) in enumerate(zip(bones[:2], (-6, -18))):
        pt = math.radians(pt)
        d = Vector((math.sin(heading) * math.cos(pt), math.cos(heading) * math.cos(pt), math.sin(pt)))
        p = p + d * L
        joints.append((p.copy(), d.copy()))
for v in bm.verts:
    for c, d in joints:
        rel = v.co - c
        along = rel.dot(d)
        if abs(along) < 0.004 and v.normal.z > 0.2 and rel.length < 0.014:
            for s in (-0.0016, 0.0, 0.0016):   # three fine lines a joint
                w = math.exp(-((along - s) / 0.0004) ** 2)
                v.co -= v.normal * 0.00025 * w
bm.to_mesh(hand.data); bm.free()
for poly in hand.data.polygons:
    poly.use_smooth = True
dec = hand.modifiers.new("dec", 'DECIMATE'); dec.ratio = 0.35
bpy.ops.object.modifier_apply(modifier="dec")


def export(obj, name):
    for x in sc.objects:
        x.select_set(x == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=os.path.join(HERE, name), export_format='GLB', use_selection=True,
                              export_materials='NONE', export_animations=False)


export(hand, "hand.glb")

# Nails: a thin curved plate on top of each tip, set slightly into the skin.
bm = bmesh.new()
for tip, d, r in TIPS:
    side = d.cross(Vector((0, 0, 1))).normalized()
    up = side.cross(d).normalized()
    nl, nw = r * 1.15, r * 0.85
    grid = []
    for i in range(7):
        row = []
        for j in range(5):
            u = i / 6 - 0.75            # along the finger, ending just past the tip
            s = (j / 4 - 0.5) * 2       # across
            bend = 1 - s * s
            pos = tip + d * (u * nl) + side * (s * nw) + up * (r * (0.70 + 0.40 * bend))
            row.append(bm.verts.new(pos))
        grid.append(row)
    for i in range(6):
        for j in range(4):
            bm.faces.new((grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]))
me = bpy.data.meshes.new("nails"); bm.to_mesh(me); bm.free()
nails = bpy.data.objects.new("nails", me); sc.collection.objects.link(nails)
for poly in me.polygons:
    poly.use_smooth = True
so = nails.modifiers.new("so", 'SOLIDIFY'); so.thickness = 0.0006
bpy.context.view_layer.objects.active = nails
bpy.ops.object.modifier_apply(modifier="so")
export(nails, "nails.glb")
print("HANDDONE", len(hand.data.polygons), "faces", [round(v, 3) for v in hand.dimensions])
