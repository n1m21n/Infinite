# Blender A/B for the lily's tepal process: the same Edit Mesh cage, smoothed by Catmull-Clark 2 levels,
# lit and framed the same, rendered by Blender Cycles and by Infinite's settle. Writes a side-by-side board.
#   python3 art/lily/ab_tepal.py OUTDIR
import json, math, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_lily as G

REPO = os.path.dirname(os.path.dirname(HERE))
INF = os.path.join(REPO, "build/Infinite.app/Contents/MacOS/Infinite")
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
VIEW = (90, 25, 0.35)          # azimuth, elevation, distance: a side 3/4 view shows the arch and ruffle
TARGET = (0, 0.03, 0.04)
SUN = (-120, 25, 4.0)          # azimuth, elevation, intensity
W, H, FOV = 640, 480, 20


def infinite_patch():
    p = G.Patch()
    p.node("3D", "Plane", "cage", "i detail %d" % G.D)
    p.node("3D", "Edit Mesh", "edit", "s ops " + G.cage_ops(G.INNER))
    p.node("3D", "Subdivide", "sub", "i levels 2", "f smooth 1")
    p.node("3D", "Material", "mat", "c color 0.55 0.6 0.95", "f metallic 0", "f roughness 0.45", "f specular 0.5")
    p.wire("geo edit 0 cage"); p.wire("geo sub 0 edit"); p.wire("geo mat 0 sub")
    p.node("3D", "Camera", "cam", "f fov %g" % FOV, "f distance %g" % VIEW[2], "f azimuth %g" % VIEW[0],
           "f elevation %g" % VIEW[1], "f targetX %g" % TARGET[0], "f targetY %g" % TARGET[1], "f targetZ %g" % TARGET[2])
    p.node("3D", "Light", "sun", "i type Directional", "f azimuth %g" % SUN[0], "f elevation %g" % SUN[1],
           "c color 1 1 1", "f intensity %g" % SUN[2], "i shadowRes 1")
    p.node("3D", "Render 3D", "render", "f width %d" % W, "f height %d" % H, "i tonemap AgX", "f exposure 1",
           "b shadows 1", "f ao 0", "c envSky 0.05 0.05 0.05", "c envHorizon 0.05 0.05 0.05",
           "c envGround 0.05 0.05 0.05", "c bg 0.05 0.05 0.05", "f rim 0", "f lightIntensity 0", "c ambient 0 0 0",
           "i ptSamples 128", "i ptBounces 8", "i supersample 1")
    p.node("Utility", "Output", "out")
    p.wire("geo render geo_a mat"); p.wire("geo render camera cam"); p.wire("geo render light_1 sun")
    p.wire("cable out 0 render")
    return p.text()


def cage_points():
    pts = []
    for j in range(G.D + 1):
        for i in range(G.D + 1):
            pts.append(G.tepal_point(j / G.D, 2.0 * i / G.D - 1.0, G.INNER))
    return pts


BLENDER_SCRIPT = r'''
import bpy, json, math, sys
a = json.load(open(sys.argv[sys.argv.index("--") + 1]))
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
def zup(p): return (p[0], -p[2], p[1])      # Infinite Y-up -> Blender Z-up
D = a["D"]
verts = [zup(p) for p in a["pts"]]
faces = [(j*(D+1)+i, j*(D+1)+i+1, (j+1)*(D+1)+i+1, (j+1)*(D+1)+i) for j in range(D) for i in range(D)]
me = bpy.data.meshes.new("tepal"); me.from_pydata(verts, [], faces); me.update()
ob = bpy.data.objects.new("tepal", me); sc.collection.objects.link(ob)
for poly in me.polygons: poly.use_smooth = True
m = ob.modifiers.new("sub", "SUBSURF"); m.levels = 2; m.render_levels = 2
mat = bpy.data.materials.new("m"); mat.use_nodes = True
bs = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
bs.inputs["Base Color"].default_value = (0.55, 0.6, 0.95, 1); bs.inputs["Roughness"].default_value = 0.45
bs.inputs["Metallic"].default_value = 0.0
me.materials.append(mat)
az, el, d = [math.radians(a["view"][0]), math.radians(a["view"][1]), a["view"][2]]
t = a["target"]
pos = (t[0] + d*math.cos(el)*math.cos(az), t[1] + d*math.sin(el), t[2] + d*math.cos(el)*math.sin(az))
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam)
cam.location = zup(pos); cam.data.sensor_fit = "HORIZONTAL"; cam.data.angle = math.radians(a["fov"])
tg = bpy.data.objects.new("tg", None); tg.location = zup(t); sc.collection.objects.link(tg)
c = cam.constraints.new("TRACK_TO"); c.target = tg; c.track_axis = "TRACK_NEGATIVE_Z"; c.up_axis = "UP_Y"
sc.camera = cam
saz, sel = math.radians(a["sun"][0]), math.radians(a["sun"][1])
sd = (math.cos(sel)*math.cos(saz), math.sin(sel), math.cos(sel)*math.sin(saz))   # toward the light, Y-up
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sc.collection.objects.link(sun)
sun.data.energy = a["sun"][2]; sun.data.angle = math.radians(1)
v = zup(sd)
import mathutils
sun.rotation_euler = mathutils.Vector(v).to_track_quat("Z", "Y").to_euler()
w = bpy.data.worlds.new("w"); w.use_nodes = True
bg = next(n for n in w.node_tree.nodes if n.type == "BACKGROUND"); bg.inputs[0].default_value = (0.05, 0.05, 0.05, 1)
sc.world = w
sc.render.engine = "CYCLES"; sc.cycles.samples = 128; sc.cycles.device = "CPU"
sc.render.resolution_x, sc.render.resolution_y = a["w"], a["h"]
try: sc.view_settings.view_transform = "AgX"
except TypeError: pass
sc.render.filepath = a["out"]; bpy.ops.render.render(write_still=True)
'''


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    pf = os.path.join(out, "tepal.inf")
    open(pf, "w").write(infinite_patch())
    env = dict(os.environ, INFINITE_LIVE_ENGINE="15")
    subprocess.run([INF, "--frame", pf, "0", os.path.join(out, "inf") + "/"], env=env, check=True, capture_output=True)
    args = {"D": G.D, "pts": cage_points(), "view": VIEW, "target": TARGET, "fov": FOV, "sun": SUN, "w": W, "h": H,
            "out": os.path.join(out, "blender.png")}
    aj, bs = os.path.join(out, "args.json"), os.path.join(out, "scene.py")
    json.dump(args, open(aj, "w")); open(bs, "w").write(BLENDER_SCRIPT)
    subprocess.run([BLENDER, "-b", "--python", bs, "--", aj], check=True, capture_output=True)
    from PIL import Image, ImageDraw
    a, b = Image.open(args["out"]).convert("RGB"), Image.open(os.path.join(out, "inf/frame_00000.png")).convert("RGB")
    board = Image.new("RGB", (W * 2, H + 24), (20, 20, 20))
    board.paste(a, (0, 24)); board.paste(b, (W, 24))
    dr = ImageDraw.Draw(board); dr.text((8, 6), "Blender Cycles: Subsurf 2", fill=(230, 230, 230))
    dr.text((W + 8, 6), "Infinite settle: Edit Mesh + Subdivide 2", fill=(230, 230, 230))
    board.save(os.path.join(out, "board.png"))
    print(os.path.join(out, "board.png"))


if __name__ == "__main__":
    main()
