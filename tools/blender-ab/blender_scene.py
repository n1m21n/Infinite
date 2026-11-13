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

# The truth cache (harness.py) keys on this number, not on this file's text: bump it only when a change alters the
# picture of a case that already exists. Adding a new feature for a new case needs no bump, so the old truths stay.
TRUTH_VERSION = 1
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

def detail_normal(nt, ds, dk):
    """Render 3D's detail normal on an imported glTF material: the normal map re-read at uv * ds, its tilt times dk,
    whiteout-blended (x, y add; z multiplies) in tangent space before the Normal Map node."""
    nm = next((n for n in nt.nodes if n.type == 'NORMAL_MAP'), None)
    if nm is None or not nm.inputs["Color"].is_linked:
        return
    src = nm.inputs["Color"].links[0].from_node
    if src.type != 'TEX_IMAGE':
        return
    vec = src.inputs["Vector"].links[0].from_socket if src.inputs["Vector"].is_linked else None
    if vec is None:
        uvn = nt.nodes.new("ShaderNodeUVMap"); vec = uvn.outputs["UV"]
    mp = nt.nodes.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value = (ds, ds, ds)
    nt.links.new(vec, mp.inputs["Vector"])
    t2 = nt.nodes.new("ShaderNodeTexImage"); t2.image = src.image; t2.extension = 'REPEAT'
    t2.interpolation = src.interpolation
    nt.links.new(mp.outputs["Vector"], t2.inputs["Vector"])
    def tang(c):
        t = nt.nodes.new("ShaderNodeVectorMath"); t.operation = 'MULTIPLY_ADD'
        t.inputs[1].default_value = (2, 2, 2); t.inputs[2].default_value = (-1, -1, -1)
        nt.links.new(c, t.inputs[0])
        sx = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(t.outputs[0], sx.inputs[0]); return sx
    a, b = tang(src.outputs["Color"]), tang(t2.outputs["Color"])
    cx = nt.nodes.new("ShaderNodeCombineXYZ")
    for ax, key in ((0, "X"), (1, "Y")):
        bm = nt.nodes.new("ShaderNodeMath"); bm.operation = 'MULTIPLY_ADD'; bm.inputs[1].default_value = dk
        nt.links.new(b.outputs[key], bm.inputs[0]); nt.links.new(a.outputs[key], bm.inputs[2])
        nt.links.new(bm.outputs[0], cx.inputs[ax])
    zm = nt.nodes.new("ShaderNodeMath"); zm.operation = 'MULTIPLY'
    nt.links.new(a.outputs["Z"], zm.inputs[0]); nt.links.new(b.outputs["Z"], zm.inputs[1]); nt.links.new(zm.outputs[0], cx.inputs[2])
    nz = nt.nodes.new("ShaderNodeVectorMath"); nz.operation = 'NORMALIZE'; nt.links.new(cx.outputs[0], nz.inputs[0])
    enc = nt.nodes.new("ShaderNodeVectorMath"); enc.operation = 'MULTIPLY_ADD'
    enc.inputs[1].default_value = (0.5, 0.5, 0.5); enc.inputs[2].default_value = (0.5, 0.5, 0.5)
    nt.links.new(nz.outputs[0], enc.inputs[0]); nt.links.new(enc.outputs[0], nm.inputs["Color"])


def add_fur(x, f):
    """Geometry Op Fur rebuilt as Geometry Nodes on an imported mesh: roots spread at random over the faces at the same
    density, straight strands along the surface normal of length L (1 - jitter * rand), drooped by gravity * L * t^2,
    radius width/2 tapering to 0.2x, in a copy of the mesh's material that reads the root's UVs."""
    # Infinite bakes the glTF node transform into the vertices before Fur runs; do the same so strands match.
    x.data.transform(x.matrix_basis)
    x.matrix_basis.identity()
    ng = bpy.data.node_groups.new("fur", 'GeometryNodeTree')
    ng.interface.new_socket("Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    ng.interface.new_socket("Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')
    N = ng.nodes.new
    L = lambda a, b: ng.links.new(a, b)
    gi, go = N("NodeGroupInput"), N("NodeGroupOutput")
    dist = N("GeometryNodeDistributePointsOnFaces"); dist.distribute_method = 'RANDOM'
    dist.inputs["Density"].default_value = f["density"] * 1e4
    dist.inputs["Seed"].default_value = int(f.get("seed", 0))
    # Cycles skips curves that a mesh object's modifier outputs, so the strands live on their own hair object
    # that reads the mesh through Object Info.
    oi = N("GeometryNodeObjectInfo"); oi.transform_space = 'ORIGINAL'; oi.inputs["Object"].default_value = x
    L(oi.outputs["Geometry"], dist.inputs["Mesh"])
    rs = N("GeometryNodeResampleCurve"); rs.inputs["Count"].default_value = f.get("segments", 3) + 1
    g = f.get("guide")
    if g:
        # Infinite's Curve arc preset (linear): x = (t - 0.5) 2 spread, y = sin(pi t) height in its Y-up space, here
        # (x, 0, y). Fur keeps the guide's shape per unit length, turned by the shortest rotation that takes its first
        # segment onto the root normal: so here the shape is normalised and pre-turned by the inverse of Z -> first
        # segment (R0), and each instance gets R0 aligned (Z onto the normal, shortest arc).
        from mathutils import Vector
        n = g["points"]
        pts = [Vector(((k / (n - 1) - 0.5) * 2 * g["spread"], 0.0, math.sin(math.pi * k / (n - 1)) * g["height"]))
               for k in range(n)]
        arc = sum((pts[k + 1] - pts[k]).length for k in range(n - 1))
        pts = [(p - pts[0]) / arc for p in pts]
        r0 = Vector((0, 0, 1)).rotation_difference((pts[1] - pts[0]).normalized())
        cd = bpy.data.curves.new(x.name + "_guide", 'CURVE'); cd.dimensions = '3D'
        sp = cd.splines.new('POLY'); sp.points.add(n - 1)
        inv = r0.inverted()
        for k, p in enumerate(pts):
            q = inv @ p
            sp.points[k].co = (q.x, q.y, q.z, 1.0)
        gob = bpy.data.objects.new(x.name + "_guide", cd)   # not linked to the scene: read through Object Info only
        gi2 = N("GeometryNodeObjectInfo"); gi2.transform_space = 'ORIGINAL'; gi2.inputs["Object"].default_value = gob
        L(gi2.outputs["Geometry"], rs.inputs["Curve"])
        al = N("FunctionNodeAlignRotationToVector"); al.axis = 'Z'; al.pivot_axis = 'AUTO'
        al.inputs["Rotation"].default_value = r0.to_euler()
        L(dist.outputs["Normal"], al.inputs["Vector"])
        rot_out = al.outputs["Rotation"]
    else:
        line = N("GeometryNodeCurvePrimitiveLine")
        line.inputs["Start"].default_value = (0, 0, 0); line.inputs["End"].default_value = (0, 0, 1)
        L(line.outputs[0], rs.inputs["Curve"])
        rot_out = dist.outputs["Rotation"]
    rnd = N("FunctionNodeRandomValue"); rnd.data_type = 'FLOAT'
    lo = next(i for i in rnd.inputs if i.name == "Min" and i.type == 'VALUE')
    hi = next(i for i in rnd.inputs if i.name == "Max" and i.type == 'VALUE')
    lo.default_value = f["length"] * (1 - f.get("jitter", 0.3)); hi.default_value = f["length"]
    inst = N("GeometryNodeInstanceOnPoints")
    L(dist.outputs["Points"], inst.inputs["Points"]); L(rs.outputs[0], inst.inputs["Instance"])
    L(rot_out, inst.inputs["Rotation"])
    L(next(o for o in rnd.outputs if o.type == 'VALUE'), inst.inputs["Scale"])
    real = N("GeometryNodeRealizeInstances"); L(inst.outputs[0], real.inputs[0])
    sp, sl = N("GeometryNodeSplineParameter"), N("GeometryNodeSplineLength")
    t2 = N("ShaderNodeMath"); t2.operation = 'MULTIPLY'; L(sp.outputs["Factor"], t2.inputs[0]); L(sp.outputs["Factor"], t2.inputs[1])
    gl = N("ShaderNodeMath"); gl.operation = 'MULTIPLY'; L(t2.outputs[0], gl.inputs[0]); L(sl.outputs["Length"], gl.inputs[1])
    dz = N("ShaderNodeMath"); dz.operation = 'MULTIPLY'; L(gl.outputs[0], dz.inputs[0]); dz.inputs[1].default_value = -f.get("gravity", 0.0)
    cx = N("ShaderNodeCombineXYZ"); L(dz.outputs[0], cx.inputs["Z"])
    setp = N("GeometryNodeSetPosition"); L(real.outputs[0], setp.inputs["Geometry"]); L(cx.outputs[0], setp.inputs["Offset"])
    rad = N("ShaderNodeMapRange"); L(sp.outputs["Factor"], rad.inputs["Value"])
    rad.inputs["To Min"].default_value = f["width"] * 0.5; rad.inputs["To Max"].default_value = f["width"] * 0.1
    setr = N("GeometryNodeSetCurveRadius"); L(setp.outputs[0], setr.inputs["Curve"]); L(rad.outputs["Result"], setr.inputs["Radius"])
    src = x.material_slots[0].material if x.material_slots and x.material_slots[0].material else None
    setm = N("GeometryNodeSetMaterial"); L(setr.outputs[0], setm.inputs["Geometry"])
    if src:
        hm = src.copy(); hm.name = src.name + "_fur"
        nt = hm.node_tree
        uvname = x.data.uv_layers.active.name if x.data.uv_layers else "UVMap"
        at = nt.nodes.new("ShaderNodeAttribute"); at.attribute_name = uvname
        for lk in list(nt.links):
            if lk.from_node.type == 'UVMAP':
                nt.links.new(at.outputs["Vector"], lk.to_socket)
        for nd in nt.nodes:
            if nd.type == 'TEX_IMAGE' and not nd.inputs["Vector"].is_linked:
                nt.links.new(at.outputs["Vector"], nd.inputs["Vector"])
        bs = next((nd for nd in nt.nodes if nd.type == 'BSDF_PRINCIPLED'), None)
        if bs and bs.inputs["Normal"].is_linked:   # a surface normal map has no meaning across a strand
            nt.links.remove(bs.inputs["Normal"].links[0])
        h = f.get("hair")
        if h:   # Material model Hair: the strands take Principled Hair (Chiang, melanin); the skin keeps its own
            for nd in list(nt.nodes):
                nt.nodes.remove(nd)
            ph = nt.nodes.new("ShaderNodeBsdfHairPrincipled"); ph.model = 'CHIANG'; ph.parametrization = 'MELANIN'
            ph.inputs["Melanin"].default_value = h["melanin"]
            ph.inputs["Melanin Redness"].default_value = h.get("redness", 1.0)
            ph.inputs["Tint"].default_value = (1, 1, 1, 1)
            ph.inputs["Roughness"].default_value = h["roughness"]
            ph.inputs["Radial Roughness"].default_value = h.get("radial", 0.3)
            ph.inputs["Coat"].default_value = 0.0
            ph.inputs["IOR"].default_value = h.get("ior", 1.55)
            ph.inputs["Offset"].default_value = math.radians(h.get("shift", 2.0))
            for k in ("Random Color", "Random Roughness"):
                ph.inputs[k].default_value = 0.0
            out = nt.nodes.new("ShaderNodeOutputMaterial")
            nt.links.new(ph.outputs[0], out.inputs["Surface"])
        setm.inputs["Material"].default_value = hm
    L(setm.outputs[0], go.inputs[0])
    hc = bpy.data.objects.new(x.name + "_fur", bpy.data.hair_curves.new(x.name + "_fur"))
    for c in x.users_collection:
        c.objects.link(hc)
    hc.parent = x.parent; hc.matrix_parent_inverse = x.matrix_parent_inverse.copy(); hc.pass_index = x.pass_index
    if src:
        hc.data.materials.append(hm)
    mod = hc.modifiers.new("fur", 'NODES'); mod.node_group = ng


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
    if o.get("detail"):
        for x in new:
            for ms in getattr(x, "material_slots", []):
                if ms.material and ms.material.use_nodes:
                    detail_normal(ms.material.node_tree, *o["detail"])
    if o.get("fur"):
        for x in new:
            if x.type == 'MESH':
                add_fur(x, o["fur"])
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


def add_recipe(mat, p, ob, name):
    """The FieldMaterialRecipes.h recipe `name` as Cycles nodes. curvature = (pointiness - 0.5) * 125 (the row 4.4
    calibration), cavity = 1 - local AO at 5 % of the radius, N.y = world Z, fbm = Noise Texture (its own pattern)."""
    if name not in ("Edge Wear", "Dirt", "Dust"):   # row 3.5 surfaces have no Cycles rebuild: owner review only
        return
    import re
    hdr = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "nodes", "FieldMaterialRecipes.h")).read()
    code = dict(re.findall(r'FLD_RECIPE\("([^"]+)", R"FLD\((.*?)\)FLD"\)', hdr, re.S))[name]
    k = {a: float(b) for a, b in re.findall(r"param float (\w+) = ([-\d.]+)", code)}   # the recipe's defaults
    nt = mat.node_tree
    def math(op, a, b=None, clamp=False):
        n = nt.nodes.new("ShaderNodeMath"); n.operation = op; n.use_clamp = clamp
        for i, v in enumerate((a, b)):
            if v is None: continue
            if isinstance(v, (int, float)): n.inputs[i].default_value = v
            else: nt.links.new(v, n.inputs[i])
        return n.outputs[0]
    def noise(scale, octaves):
        tc = nt.nodes.new("ShaderNodeTexCoord"); nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = scale; nz.inputs["Detail"].default_value = octaves - 1
        nz.inputs["Roughness"].default_value = 0.5
        nt.links.new(tc.outputs["Object"], nz.inputs["Vector"]); return nz.outputs["Fac"]
    def mixc(a, b, f, sock):
        mx = nt.nodes.new("ShaderNodeMix"); mx.data_type = 'RGBA'
        mx.inputs[6].default_value = (*a, 1); mx.inputs[7].default_value = (*b, 1)
        nt.links.new(f, mx.inputs["Factor"]); nt.links.new(mx.outputs[2], sock)
    def mixf(a, b, f, sock):
        nt.links.new(math('ADD', a, math('MULTIPLY', f, b - a)), sock)
    ge = nt.nodes.new("ShaderNodeNewGeometry")
    if name == "Edge Wear":
        curv = math('MULTIPLY', math('SUBTRACT', ge.outputs["Pointiness"], 0.5), 125.0)
        n = math('SUBTRACT', noise(k["scale"], 4), 0.5)
        e = math('ADD', math('DIVIDE', math('SUBTRACT', curv, k["threshold"]), k["width"]), math('MULTIPLY', n, 2 * k["breakup"]))
        mr = nt.nodes.new("ShaderNodeMapRange"); mr.interpolation_type = 'SMOOTHSTEP'
        mr.inputs[1].default_value, mr.inputs[2].default_value = 0.35, 0.65
        nt.links.new(e, mr.inputs[0]); w = mr.outputs[0]
        mixc((0.50, 0.07, 0.05), (0.80, 0.78, 0.74), w, p.inputs["Base Color"])
        nt.links.new(w, p.inputs["Metallic"]); mixf(0.55, 0.25, w, p.inputs["Roughness"])
        return
    nn = noise(k["scale"], 5 if name == "Dirt" else 4)
    if name == "Dirt":
        vs = [v.co for v in ob.data.vertices]
        lo = [min(v[k] for v in vs) for k in range(3)]; hi = [max(v[k] for v in vs) for k in range(3)]
        mid = [(lo[k] + hi[k]) / 2 for k in range(3)]
        rad = max(sum((v[k] - mid[k]) ** 2 for k in range(3)) for v in vs) ** 0.5
        ao = nt.nodes.new("ShaderNodeAmbientOcclusion"); ao.samples = 16; ao.only_local = True
        ao.inputs["Distance"].default_value = 0.05 * rad
        cav = math('SUBTRACT', 1.0, ao.outputs["AO"])
        d = math('MULTIPLY', math('MULTIPLY', math('MULTIPLY', cav, k["spread"]), math('ADD', nn, 0.3), clamp=True), k["amount"])
        mixc((0.70, 0.69, 0.66), (0.10, 0.08, 0.05), d, p.inputs["Base Color"]); mixf(0.4, 0.9, d, p.inputs["Roughness"])
    elif name == "Dust":
        sep = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(ge.outputs["Normal"], sep.inputs[0])
        up = math('MAXIMUM', sep.outputs["Z"], 0.0)
        d = math('MULTIPLY', math('MULTIPLY', math('POWER', up, k["sharpness"]), math('ADD', nn, 0.5), clamp=True), k["amount"])
        mixc((0.04, 0.10, 0.30), (0.60, 0.57, 0.52), d, p.inputs["Base Color"]); mixf(0.3, 1.0, d, p.inputs["Roughness"])


posed = []   # (armature, action, object) set to each frame's phase before it renders
MOTION = scene.get("motion", {})
moving = []  # (object, velocity m/s) keyed linearly over the motion's frames


def add_maps(mat, p, maps, m):
    """Image maps from the case folder (harness.make_texture): albedo and opacity multiply their factor,
    roughness multiplies the roughness, normal is a tangent-space OpenGL map; an alpha cutoff thresholds alpha."""
    nt = mat.node_tree
    tc = nt.nodes.new("ShaderNodeTexCoord")
    gen = m.get("mapping") == "generated"
    uv = tc.outputs["Generated" if gen else "UV"]
    def scaled(vec, k):
        mp = nt.nodes.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value = (k, k, k)
        nt.links.new(vec, mp.inputs["Vector"]); return mp.outputs["Vector"]
    if m.get("uv_scale", 1.0) != 1.0:
        uv = scaled(uv, m["uv_scale"])
    def img(name, data, vec=None):
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(os.path.join(case, name + ".png"))
        if data:
            t.image.colorspace_settings.is_data = True
        if gen and m.get("triplanar", 0.0) > 0:
            t.projection = 'BOX'; t.projection_blend = m["triplanar"]
        if m.get("uv_scale") or gen or vec is not None:
            t.extension = 'REPEAT'
        nt.links.new(vec if vec is not None else uv, t.inputs["Vector"])
        return t
    def times(sock, out, factor):
        mul = nt.nodes.new("ShaderNodeMix"); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'
        mul.inputs["Factor"].default_value = 1.0
        mul.inputs[6].default_value = (*factor, 1) if len(factor) == 3 else factor
        nt.links.new(out, mul.inputs[7]); nt.links.new(mul.outputs[2], sock)
    if "albedo" in maps:
        times(p.inputs["Base Color"], img(maps["albedo"], False).outputs["Color"], m.get("base_color", (1, 1, 1)))
    if "roughness" in maps:
        mm = nt.nodes.new("ShaderNodeMath"); mm.operation = 'MULTIPLY'; mm.inputs[1].default_value = m.get("roughness", 1.0)
        nt.links.new(img(maps["roughness"], True).outputs["Color"], mm.inputs[0]); nt.links.new(mm.outputs[0], p.inputs["Roughness"])
    if "normal" in maps and "displace" not in m:   # row 4.2: a displaced case's truth is the real relief, not the bump
        nm = nt.nodes.new("ShaderNodeNormalMap")
        col = img(maps["normal"], True).outputs["Color"]
        if "detail" in m:
            # Render 3D's detail normal: the same map at uv * scale, tilt * strength, whiteout-blended in tangent space.
            ds, dk = m["detail"]
            def tang(c, k):
                t = nt.nodes.new("ShaderNodeVectorMath"); t.operation = 'MULTIPLY_ADD'
                t.inputs[1].default_value = (2, 2, 2); t.inputs[2].default_value = (-1, -1, -1)
                nt.links.new(c, t.inputs[0])
                sx = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(t.outputs[0], sx.inputs[0])
                return sx, k
            a, _ = tang(col, 1.0)
            b, _ = tang(img(maps["normal"], True, scaled(uv, ds)).outputs["Color"], dk)
            cx = nt.nodes.new("ShaderNodeCombineXYZ")
            for ax, key in ((0, "X"), (1, "Y")):
                bm = nt.nodes.new("ShaderNodeMath"); bm.operation = 'MULTIPLY_ADD'; bm.inputs[1].default_value = dk
                nt.links.new(b.outputs[key], bm.inputs[0]); nt.links.new(a.outputs[key], bm.inputs[2])
                nt.links.new(bm.outputs[0], cx.inputs[ax])
            zm = nt.nodes.new("ShaderNodeMath"); zm.operation = 'MULTIPLY'
            nt.links.new(a.outputs["Z"], zm.inputs[0]); nt.links.new(b.outputs["Z"], zm.inputs[1]); nt.links.new(zm.outputs[0], cx.inputs[2])
            nz = nt.nodes.new("ShaderNodeVectorMath"); nz.operation = 'NORMALIZE'; nt.links.new(cx.outputs[0], nz.inputs[0])
            enc = nt.nodes.new("ShaderNodeVectorMath"); enc.operation = 'MULTIPLY_ADD'
            enc.inputs[1].default_value = (0.5, 0.5, 0.5); enc.inputs[2].default_value = (0.5, 0.5, 0.5)
            nt.links.new(nz.outputs[0], enc.inputs[0]); col = enc.outputs[0]
        nt.links.new(col, nm.inputs["Color"]); nt.links.new(nm.outputs["Normal"], p.inputs["Normal"])
    if "opacity" in maps or m.get("alpha_cutoff", 0.0) > 0:
        a = img(maps["opacity"], True).outputs["Color"] if "opacity" in maps else None
        mm = nt.nodes.new("ShaderNodeMath"); mm.operation = 'MULTIPLY'; mm.inputs[1].default_value = m.get("alpha", 1.0)
        if a is not None:
            nt.links.new(a, mm.inputs[0])
        else:
            mm.inputs[0].default_value = 1.0
        out = mm.outputs[0]
        if m.get("alpha_cutoff", 0.0) > 0:
            gt = nt.nodes.new("ShaderNodeMath"); gt.operation = 'GREATER_THAN'; gt.inputs[1].default_value = m["alpha_cutoff"]
            nt.links.new(out, gt.inputs[0]); out = gt.outputs[0]
        nt.links.new(out, p.inputs["Alpha"])

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
    elif shape == "mesh":   # judge_table: verts, faces and per-corner uvs straight from the case, smooth shaded
        me = bpy.data.meshes.new("mesh%d" % i)
        me.from_pydata(o["verts"], [], o["faces"])
        if o.get("uvs"):
            uvl = me.uv_layers.new(name="UVMap")
            for lp, uv in zip(uvl.data, o["uvs"]):
                lp.uv = uv
        for poly in me.polygons:
            poly.use_smooth = True
        me.update()
        mo = bpy.data.objects.new("mesh%d" % i, me)
        sc.collection.objects.link(mo)
        bpy.context.view_layer.objects.active = mo
    else:
        sys.exit("unknown shape " + shape)
    ob = bpy.context.active_object
    s = o.get("scale", 1.0)
    ob.scale = s if isinstance(s, list) else (s, s, s)
    ob.location = o.get("location", (0, 0, 0))
    bpy.context.view_layer.update()
    ob.data.transform(ob.matrix_world)
    ob.matrix_world = Matrix.Identity(4)
    if o.get("bevel"):   # round every edge by this width (metres), flat faces kept flat by weighted normals
        bv = ob.modifiers.new("bev", 'BEVEL'); bv.width = o["bevel"]; bv.segments = 4; bv.limit_method = 'ANGLE'
        wn = ob.modifiers.new("wn", 'WEIGHTED_NORMAL'); wn.keep_sharp = False
        for x in bpy.context.scene.objects:
            x.select_set(x == ob)
        bpy.ops.object.shade_smooth()
        for md in list(ob.modifiers):
            bpy.ops.object.modifier_apply(modifier=md.name)
    dz = o.get("material", {}).get("displace")
    if dz:   # row 4.2: the bump map's grey as real geometry, 1 = the surface, 0 = dz below it
        sub = ob.modifiers.new("sub", 'SUBSURF'); sub.subdivision_type = 'SIMPLE'; sub.levels = sub.render_levels = 9
        tx = bpy.data.textures.new("disp%d" % i, 'IMAGE')
        tx.image = bpy.data.images.load(os.path.join(case, o["material"]["maps"]["normal"] + ".png"))
        tx.image.colorspace_settings.is_data = True
        tx.extension = 'EXTEND'; tx.use_interpolation = True
        dm = ob.modifiers.new("disp", 'DISPLACE'); dm.texture = tx; dm.texture_coords = 'UV'
        dm.strength = dz; dm.mid_level = 1.0

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
    p.inputs["Sheen Roughness"].default_value = m.get("sheen_roughness", 0.5)
    if m.get("sheen_direction"):   # row 5.1 truth: a separate Sheen BSDF whose normal leans toward the uv tangent (+X on a
        # Blender plane), added over the Principled with its own sheen off; the base dims by Infinite's 1 - sheen E, kept dark
        nt = mat.node_tree; dd = m["sheen_direction"]
        p.inputs["Sheen Weight"].default_value = 0.0
        sh = nt.nodes.new("ShaderNodeBsdfSheen"); sh.distribution = 'MICROFIBER'
        sh.inputs["Color"].default_value = (m.get("sheen", 0.0),) * 3 + (1.0,)
        sh.inputs["Roughness"].default_value = m.get("sheen_roughness", 0.5)
        k = (1 + dd * dd) ** -0.5
        cx = nt.nodes.new("ShaderNodeCombineXYZ")   # an unlinked Normal socket's value is ignored, so feed it
        cx.inputs[0].default_value, cx.inputs[1].default_value, cx.inputs[2].default_value = dd * k, 0.0, k
        nt.links.new(cx.outputs[0], sh.inputs["Normal"])
        ad = nt.nodes.new("ShaderNodeAddShader"); mo = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        nt.links.new(p.outputs[0], ad.inputs[0]); nt.links.new(sh.outputs[0], ad.inputs[1]); nt.links.new(ad.outputs[0], mo.inputs[0])
    p.inputs["Emission Color"].default_value = (*m.get("emission", (0, 0, 0)), 1)
    p.inputs["Emission Strength"].default_value = m.get("emission_strength", 0.0)
    p.inputs["Alpha"].default_value = m.get("alpha", 1.0)
    if m.get("wear"):   # row 4.4 truth: pointiness (curvature) or local AO at 5 % of the bounding radius, as emission
        nt = mat.node_tree
        out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        em = nt.nodes.new("ShaderNodeEmission")
        nt.links.new(em.outputs[0], out.inputs["Surface"])
        if m["wear"] == "curvature":
            ge = nt.nodes.new("ShaderNodeNewGeometry")
            mr = nt.nodes.new("ShaderNodeMapRange")
            mr.inputs[1].default_value, mr.inputs[2].default_value = 0.4, 0.6
            nt.links.new(ge.outputs["Pointiness"], mr.inputs[0])
            nt.links.new(mr.outputs[0], em.inputs["Color"])
        else:
            vs = [v.co for v in ob.data.vertices]
            lo = [min(v[k] for v in vs) for k in range(3)]; hi = [max(v[k] for v in vs) for k in range(3)]
            mid = [(lo[k] + hi[k]) / 2 for k in range(3)]
            rad = max(sum((v[k] - mid[k]) ** 2 for k in range(3)) for v in vs) ** 0.5
            ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
            ao.samples = 16; ao.only_local = True
            ao.inputs["Distance"].default_value = 0.05 * rad
            nt.links.new(ao.outputs["AO"], em.inputs["Color"])
    if m.get("split"):   # row 3.3 truth: the Sketch 3D mask() split as a world-height step (Blender Z-up)
        c0, c1, z = m["split"]
        nt = mat.node_tree
        geo = nt.nodes.new("ShaderNodeNewGeometry"); sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        gt = nt.nodes.new("ShaderNodeMath"); gt.operation = 'GREATER_THAN'; gt.inputs[1].default_value = z
        mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'
        mix.inputs[6].default_value = (*c0, 1.0); mix.inputs[7].default_value = (*c1, 1.0)
        nt.links.new(geo.outputs["Position"], sep.inputs[0]); nt.links.new(sep.outputs["Z"], gt.inputs[0])
        nt.links.new(gt.outputs[0], mix.inputs[0]); nt.links.new(mix.outputs[2], p.inputs["Base Color"])
        p.inputs["Roughness"].default_value = 0.5
    if m.get("recipe"):   # rows 3.5 / 4.5 truth: the recipe rebuilt from Cycles' own masks (pointiness, local AO, normal)
        add_recipe(mat, p, ob, m["recipe"])
    # Coverage-case lobes. Render 3D's subsurface colour is Cycles' per-channel radius; its 0..1 radius maps to 0.2 m.
    p.inputs["Subsurface Weight"].default_value = m.get("subsurface", 0.0)
    p.inputs["Subsurface Radius"].default_value = m.get("subsurface_color", (1.0, 0.2, 0.1))
    p.inputs["Subsurface Scale"].default_value = 0.2 * m.get("subsurface_radius", 0.5)
    p.inputs["Anisotropic"].default_value = m.get("anisotropy", 0.0)
    p.inputs["Anisotropic Rotation"].default_value = m.get("anisotropy_rotation", 0.0)
    if m.get("iridescence", 0.0) > 0:
        p.inputs["Thin Film Thickness"].default_value = m["iridescence"] * m.get("iridescence_thickness", 400.0)
        p.inputs["Thin Film IOR"].default_value = m.get("iridescence_ior", 1.33)
    add_maps(mat, p, m.get("maps", {}), m)
    if m.get("layer"):   # row 4.1: mix(base, mix(layer, its layer, its mask), mask) through Mix Shaders on UV masks
        nt = mat.node_tree
        def principled(lm):
            q = nt.nodes.new("ShaderNodeBsdfPrincipled")
            q.inputs["Base Color"].default_value = (*lm.get("base_color", (0.8, 0.8, 0.8)), 1)
            q.inputs["Metallic"].default_value = lm.get("metallic", 0.0)
            q.inputs["Roughness"].default_value = lm.get("roughness", 0.5)
            q.inputs["Emission Color"].default_value = (*lm.get("emission", (0, 0, 0)), 1)
            q.inputs["Emission Strength"].default_value = lm.get("emission_strength", 0.0)
            return q.outputs["BSDF"]
        def over(under, lm):
            top = principled(lm)
            if lm.get("layer"):
                top = over(top, lm["layer"])
            mx = nt.nodes.new("ShaderNodeMixShader")
            mx.inputs["Fac"].default_value = lm.get("mix", 1.0)
            if lm.get("mask"):
                tc = nt.nodes.new("ShaderNodeTexCoord")
                t = nt.nodes.new("ShaderNodeTexImage")
                t.image = bpy.data.images.load(os.path.join(case, lm["mask"] + ".png"))
                t.image.colorspace_settings.is_data = True
                nt.links.new(tc.outputs["UV"], t.inputs["Vector"])
                fac = t.outputs["Color"]
                if lm.get("mix", 1.0) != 1.0:
                    k = nt.nodes.new("ShaderNodeMath"); k.operation = 'MULTIPLY'
                    k.inputs[1].default_value = lm["mix"]
                    nt.links.new(t.outputs["Color"], k.inputs[0]); fac = k.outputs[0]
                nt.links.new(fac, mx.inputs["Fac"])
            nt.links.new(under, mx.inputs[1]); nt.links.new(top, mx.inputs[2])
            return mx.outputs["Shader"]
        out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        nt.links.new(over(p.outputs["BSDF"], m["layer"]), out.inputs["Surface"])
    sh = m.get("shade")
    if sh and sh.get("tex") == "image":   # row 2.4: Texture Coordinate (UV) -> Image Texture -> Base Color
        nt = mat.node_tree
        tc = nt.nodes.new("ShaderNodeTexCoord")
        it = nt.nodes.new("ShaderNodeTexImage")
        it.image = bpy.data.images.load(os.path.join(case, sh.get("image", "blotches") + ".png"))
        it.extension = 'REPEAT'
        nt.links.new(tc.outputs["UV"], it.inputs["Vector"])
        nt.links.new(it.outputs["Color"], p.inputs["Base Color"])
    elif sh:   # row 2.3: Texture Coordinate (Object) -> Mapping (Point) -> Checker (scale 1) -> Base Color
        nt = mat.node_tree
        tc = nt.nodes.new("ShaderNodeTexCoord")
        mp = nt.nodes.new("ShaderNodeMapping"); mp.vector_type = 'POINT'
        s3 = sh.get("scale", 1.0)
        mp.inputs["Location"].default_value = sh.get("translate", (0, 0, 0))
        mp.inputs["Rotation"].default_value = [math.radians(a) for a in sh.get("rotate", (0, 0, 0))]
        mp.inputs["Scale"].default_value = s3 if isinstance(s3, list) else (s3, s3, s3)
        tex = sh.get("tex", "checker")
        if tex == "bump":       # row 2.6: sin(x) sin(y) -> Bump -> Normal
            sp = nt.nodes.new("ShaderNodeSeparateXYZ")
            ma = nt.nodes.new("ShaderNodeMath"); ma.operation = 'SINE'
            mb = nt.nodes.new("ShaderNodeMath"); mb.operation = 'SINE'
            mm = nt.nodes.new("ShaderNodeMath"); mm.operation = 'MULTIPLY'
            bp = nt.nodes.new("ShaderNodeBump")
            bp.inputs["Strength"].default_value = sh.get("strength", 1.0)
            bp.inputs["Distance"].default_value = sh.get("distance", 0.1)
            nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
            nt.links.new(mp.outputs["Vector"], sp.inputs[0])
            nt.links.new(sp.outputs["X"], ma.inputs[0]); nt.links.new(sp.outputs["Y"], mb.inputs[0])
            nt.links.new(ma.outputs[0], mm.inputs[0]); nt.links.new(mb.outputs[0], mm.inputs[1])
            nt.links.new(mm.outputs[0], bp.inputs["Height"])
            nt.links.new(bp.outputs["Normal"], p.inputs["Normal"])
        elif tex == "brick":      # row 2.4: both brick colours the same, since the pick per brick is random
            ck = nt.nodes.new("ShaderNodeTexBrick")
            ck.inputs["Color1"].default_value = (*sh.get("color1", (0.8, 0.8, 0.8)), 1)
            ck.inputs["Color2"].default_value = (*sh.get("color1", (0.8, 0.8, 0.8)), 1)
            ck.inputs["Mortar"].default_value = (*sh.get("mortarColor", (0, 0, 0)), 1)
            ck.inputs["Scale"].default_value = 1.0
            ck.inputs["Mortar Size"].default_value = sh.get("mortar", 0.02)
            ck.inputs["Mortar Smooth"].default_value = 0.0
            ck.inputs["Brick Width"].default_value = sh.get("width", 0.5)
            ck.inputs["Row Height"].default_value = sh.get("height", 0.25)
            ck.offset = sh.get("offset", 0.5); ck.offset_frequency = 2; ck.squash = 1.0
        elif tex == "gradient":
            ck = nt.nodes.new("ShaderNodeTexGradient")
            ck.gradient_type = sh.get("kind", "linear").upper()
        else:
            ck = nt.nodes.new("ShaderNodeTexChecker")
            ck.inputs["Color1"].default_value = (*sh.get("color1", (0.8, 0.8, 0.8)), 1)
            ck.inputs["Color2"].default_value = (*sh.get("color2", (0.2, 0.2, 0.2)), 1)
            ck.inputs["Scale"].default_value = 1.0
        if tex != "bump":
            nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
            nt.links.new(mp.outputs["Vector"], ck.inputs["Vector"])
            nt.links.new(ck.outputs["Color"], p.inputs["Base Color"])
    tl = m.get("translucency", 0.0)
    if tl > 0:  # Render 3D's translucency: that share of the diffuse leaves the far side (Translucent BSDF)
        nt = mat.node_tree
        bc = m.get("base_color", (0.8, 0.8, 0.8))
        p.inputs["Base Color"].default_value = (*[c * (1.0 - tl) for c in bc], 1)
        tr = nt.nodes.new("ShaderNodeBsdfTranslucent"); tr.inputs["Color"].default_value = (*[c * tl for c in bc], 1)
        ad = nt.nodes.new("ShaderNodeAddShader")
        mo = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        nt.links.new(p.outputs[0], ad.inputs[0]); nt.links.new(tr.outputs[0], ad.inputs[1])
        nt.links.new(ad.outputs[0], mo.inputs["Surface"])
    ob.data.materials.append(mat)
    ob.pass_index = region_index(o)

    for x in bpy.context.scene.objects:
        x.select_set(x == ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(case, "obj_%d.glb" % i), export_format='GLB',
                              use_selection=True, export_materials='NONE', export_animations=False,
                              export_tangents=bool(m.get("maps")))
    if o.get("fur"):
        add_fur(ob, o["fur"])
    if str(i) in MOTION.get("velocity", {}):
        moving.append((ob, MOTION["velocity"][str(i)]))

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
    if cam.get("blades"):
        camd.dof.aperture_blades = cam["blades"]
        camd.dof.aperture_rotation = math.radians(cam.get("blade_rotation", 0.0))

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

fog = scene.get("fog")
if fog:
    # Row 5.3 truth: a homogeneous medium in a box, extinction = density, scattered share = colour, HG g:
    # Volume Scatter (colour x density) plus Volume Absorption ((1 - colour) x density).
    (x0, y0, z0), (x1, y1, z1) = fog["box"]
    bpy.ops.mesh.primitive_cube_add(size=2, location=((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    fo = bpy.context.active_object; fo.scale = ((x1 - x0) / 2, (y1 - y0) / 2, (z1 - z0) / 2)
    fm = bpy.data.materials.new("fog"); fm.use_nodes = True; fnt = fm.node_tree
    for nd in list(fnt.nodes):
        if nd.type != "OUTPUT_MATERIAL": fnt.nodes.remove(nd)
    fout = next(nd for nd in fnt.nodes if nd.type == "OUTPUT_MATERIAL")
    vs = fnt.nodes.new("ShaderNodeVolumeScatter"); va = fnt.nodes.new("ShaderNodeVolumeAbsorption")
    vs.inputs["Color"].default_value = (*fog["color"], 1); vs.inputs["Density"].default_value = fog["density"]
    vs.inputs["Anisotropy"].default_value = fog.get("g", 0.0)
    va.inputs["Color"].default_value = (*fog["color"], 1); va.inputs["Density"].default_value = fog["density"]
    add = fnt.nodes.new("ShaderNodeAddShader")
    fnt.links.new(vs.outputs[0], add.inputs[0]); fnt.links.new(va.outputs[0], add.inputs[1])
    fnt.links.new(add.outputs[0], fout.inputs["Volume"])
    try: fm.cycles.homogeneous_volume = True
    except Exception: pass
    fo.data.materials.append(fm)
    sc.cycles.volume_bounces = 8

w = scene.get("world", {})
sc.world = bpy.data.worlds.new("w"); sc.world.use_nodes = True
wt = sc.world.node_tree
bg = next(n for n in wt.nodes if n.type == "BACKGROUND")
bg.inputs["Color"].default_value = (*w.get("color", (0, 0, 0)), 1)
bg.inputs["Strength"].default_value = w.get("strength", 1.0)
if w.get("sky"):
    # Row 5.5: Blender's Preetham sky; sun_direction is Z up, from the case's elevation / azimuth (0 = +X, -90 = +Y).
    k = w["sky"]; e, a = math.radians(k["elevation"]), math.radians(k["azimuth"])
    st = wt.nodes.new("ShaderNodeTexSky"); st.sky_type = 'PREETHAM'; st.turbidity = k.get("turbidity", 3.0)
    st.sun_direction = (math.cos(e) * math.cos(a), -math.cos(e) * math.sin(a), math.sin(e))
    wt.links.new(st.outputs["Color"], bg.inputs["Color"])
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
try:  # Metal when there is one: the truth is the same picture, several times sooner
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = 'METAL'; prefs.get_devices()
    for d in prefs.devices: d.use = True
    if any(d.type == 'METAL' for d in prefs.devices): sc.cycles.device = 'GPU'
except Exception as e:
    print("SCENE: GPU off,", e)
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


if MOTION:
    # Frame f is time f / fps, as Infinite's --frames-dir; the camera orbits and objects move linearly.
    fps = MOTION.get("fps", 30); sc.render.fps = fps
    last = int(round(max(MOTION["checks"]) * fps)) + 1
    rate = MOTION.get("camera_azimuth_rate", 0.0)
    for f in range(0, last + 1):
        tt = f / fps
        if rate:
            co.location = spherical(cam["azimuth"] + rate * tt, cam["elevation"], cam["distance"], target); aim(co, target)
            co.keyframe_insert("location", frame=f); co.keyframe_insert("rotation_euler", frame=f)
        for ob, v in moving:
            ob.location = Vector(v) * tt; ob.keyframe_insert("location", frame=f)
    for x in [co] + [ob for ob, _ in moving]:
        if x.animation_data and x.animation_data.action:
            for fc in getattr(x.animation_data.action, "fcurves", []):
                for kp in fc.keyframe_points: kp.interpolation = 'LINEAR'
    if MOTION.get("blur"):
        sc.render.use_motion_blur = True; sc.render.motion_blur_shutter = MOTION["blur"]
        for host in (sc.render, sc.cycles):
            if hasattr(host, "motion_blur_position"): host.motion_blur_position = 'CENTER'
    for k, tc in enumerate(MOTION["checks"]):
        sc.frame_set(int(round(tc * fps)))
        sc.render.filepath = os.path.join(case, "truth_%d.png" % k)
        bpy.ops.render.render(write_still=True)
    print("SCENEDONE", len(scene["objects"]))
    sys.exit(0)

if os.environ.get("AB_PASSES"):
    # Row 7.3 check (tools/blender-ab/passes_check.py): world normal, diffuse colour and world position as float
    # arrays, <case>/pass_<Name>.npy, rows top to bottom. 4 samples, no denoise, no depth of field.
    import numpy as np
    vl = sc.view_layers[0]
    vl.use_pass_normal = vl.use_pass_diffuse_color = vl.use_pass_position = True
    sc.cycles.samples, sc.cycles.use_denoising = 4, False
    sc.view_settings.view_transform = 'Raw'
    camd.dof.use_dof = False
    # Diffuse Color is the base colour scaled by the specular layer (and 0 on metal); the albedo pass is the plain
    # base colour, so for this render no surface has a specular layer or metal.
    for mat in bpy.data.materials:
        for nd in (mat.node_tree.nodes if mat.node_tree else []):
            if nd.type == 'BSDF_PRINCIPLED':
                for k in ("Specular IOR Level", "Metallic", "Coat Weight", "Sheen Weight"):
                    if k in nd.inputs and not nd.inputs[k].is_linked:
                        nd.inputs[k].default_value = 0.0
    sc.render.image_settings.file_format = 'OPEN_EXR'
    sc.render.image_settings.color_mode, sc.render.image_settings.color_depth = 'RGB', '32'
    for name in ("Normal", "DiffCol", "Position"):
        tree = bpy.data.node_groups.new("pass_" + name, 'CompositorNodeTree')
        rl = tree.nodes.new('CompositorNodeRLayers')
        out = tree.nodes.new('NodeGroupOutput')
        tree.interface.new_socket("Image", in_out='OUTPUT', socket_type='NodeSocketColor')
        names = {"DiffCol": ("DiffCol", "Diffuse Color")}.get(name, (name,))
        sock = next((rl.outputs[n] for n in names if n in rl.outputs), None)
        if sock is None:
            print("PASSES outputs:", [o.name for o in rl.outputs]); sys.exit(1)
        tree.links.new(sock, out.inputs[0])
        sc.compositing_node_group = tree
        path = os.path.join(case, "pass_%s.exr" % name)
        sc.render.filepath = path
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(path)
        w, h = img.size
        np.save(path[:-4] + ".npy", np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1, :, :3])
    np.save(os.path.join(case, "pass_cam.npy"), np.array(co.location[:], dtype=np.float32))
    print("PASSESDONE")
    sys.exit(0)

for k, fr in enumerate(FRAMES or [None]):
    tag = "" if FRAMES is None else "_%d" % k
    if fr is not None:
        pose(fr)
    sc.render.filepath = os.path.join(case, "truth%s.png" % tag)
    bpy.ops.render.render(write_still=True)
    if REGIONS:
        render_mask(os.path.join(case, "mask%s.png" % tag))
print("SCENEDONE", len(scene["objects"]))
