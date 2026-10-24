"""Writes the Render 3D HQ example scenes to examples/hq (shipped with the app,
FILE > Examples), one renderer per patch: Render 3D HQ only (0.71.1; the same
scenes through Render 3D, for BEFORE / AFTER comparisons, only with --raster,
into build/hq/raster_scenes - not shipped). Scenes A-I are authored for Render
3D and converted (to_filament); J-L are HQ-only. Nodes are laid out in columns
by graph depth (layout), each column as wide as its widest node and each node
below the one above by its real height (scripts/hq_node_sizes.tsv, measured in
the app with --measure), a Comment right above the node it explains. 1920 x
1080. Missing keys take the node defaults, so only what matters is written.
The HDRI is examples/hq/hq_media/studio.hdr (scripts/make_hq_studio_hdr.py).

  A  material_studio     six PBR spheres (polished / rough metal, dielectric,
                         glossy plastic, emissive, normal-mapped) on a floor;
                         an LFO turns the HDRI (one turn every 32 beats)
  B  particle_studio     ~90k static points, side light, orbiting camera
  C  generative_cloud    GPU particles in curl turbulence (~200k alive)
  D  emissive_particles  glowing particles on black, bloom
  E  geometry_particles  a metal torus knot inside a particle cloud
  F  points_1m           1M points displaced by animated noise (benchmark)
  G  shadow_contact      shadows right under objects resting on / just above
                         the floor (gold sphere touching, clear-coat sphere
                         20 cm up, rough cube 2 cm up, copper cylinder), low
                         camera - the case behind the 0.71 Render 3D fix
  H  ao_contact          ambient occlusion at contacts and creases: a wall
                         corner, matte / plastic / metal / rough objects on the
                         floor (HQ: tick "show AO only" to see it alone)
  I  instances_100k      ~100k metal boxes instanced on a noisy grid
                         (Instance PointZ), orbiting camera
  J  sand_1m             (0.71.1, HQ only) a million grains of sand: GPU
                         particles drawn as dots, pulled onto drifting dunes
                         (Force Target; the dunes take only P.y from a noisy
                         grid with Lookup Attribute), parted by a wandering
                         repulsor (Force Radial), on the floor (Collide)
  K  emissive_gi         (0.71.1, HQ only) glowing objects lighting a dim room
                         through screen-space GI
  L  tutorial_attribute  (0.71.1, HQ only) how to use Attribute PointZ, in
                         Comments: its settings, what an expression can read,
                         four boxes-on-a-grid examples (size by height, stretch,
                         random turn, colour by position and time)

Usage:
  python scripts/make_hq_scenes.py                    examples/hq/*.inf
  python scripts/make_hq_scenes.py --raster           + build/hq/raster_scenes
  python scripts/make_hq_scenes.py --measure <exe>    measure node sizes in the
                                                      app, then lay out again
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
OUT_DIR = os.path.join(ROOT, "examples", "hq")
RASTER_DIR = os.path.join(ROOT, "build", "hq", "raster_scenes")
SIZES_FILE = os.path.join(os.path.dirname(__file__), "hq_node_sizes.tsv")
# Relative to the patch: examples/hq/<scene>.inf next to examples/hq/hq_media.
HDRI = "hq_media/studio.hdr"


class Patch:
    def __init__(self):
        self.nodes = []   # (category, type, params, pos)
        self.links = []   # text lines
        self.attach = {}  # Comment index -> the node it explains (layout: right above it)

    def node(self, category, type_name, x, y, **params):
        self.nodes.append((category, type_name, params, (x, y)))
        return len(self.nodes)

    def note(self, text, attach=None, width=380, height=170, font=1, color=(0.95, 0.85, 0.45)):
        # A Comment on the canvas (text with real line breaks; the patch file
        # stores them as \\n). attach: the node it sits right above.
        escaped = text.replace("\\", "\\\\").replace("\n", "\\n")
        i = self.node("Compositing", "Comment", 0, 0, s_text=escaped, f_width=width, f_height=height,
                      i_fontSize=font, c_color=color)
        if attach is not None:
            self.attach[i] = attach
        return i

    def geo(self, dst, slot, src):
        self.links.append("geo %d %d %d" % (dst, slot, src))

    def cable(self, dst, slot, src):
        self.links.append("cable %d %d %d" % (dst, slot, src))

    def text(self):
        out = ["infinite-patch 1", "# turbo-mod-range"]
        for i, (cat, typ, params, (x, y)) in enumerate(self.nodes, 1):
            out.append("node %d %s %s" % (i, cat, typ))
            out.append("  uid %d" % i)
            out.append("  pos %d %d" % (x, y))
            out.append("  flags 1 0 0 0 1 0")
            for key, val in params.items():
                out.append("  " + fmt(key, val))
            out.append("end")
        out += self.links
        out.append("transport 120 4 4 0 0")
        return "\n".join(out) + "\n"


def fmt(key, val):
    # key is "<prefix>_<name>": f float, i int, b bool, c colour, s text
    prefix, name = key.split("_", 1)
    if prefix == "c":
        return "c %s %g %g %g" % ((name,) + tuple(val))
    if prefix == "b":
        return "b %s %d" % (name, 1 if val else 0)
    if prefix == "i":
        return "i %s %d" % (name, val)
    if prefix == "s":
        return "s %s %s" % (name, val)
    return "f %s %g" % (name, val)


def render(p, x=1600, y=600, **extra):
    params = dict(f_width=1920, f_height=1080, i_renderMode=0, i_samples=2, b_shadows=True,
                  i_shadowQuality=1, i_tonemap=1, f_exposure=1.0, f_envIntensity=1.0,
                  b_envAsBackground=True)
    params.update(extra)
    return p.node("3DScene", "Render 3D", x, y, **params)


def camera(p, dist, az, el, ty=0.0, orbit=0.0):
    return p.node("3DScene", "Camera", 1200, 200, i_projection=0, f_fov=40.0, f_distance=dist,
                  f_azimuth=az, f_elevation=el, f_targetX=0.0, f_targetY=ty, f_targetZ=0.0,
                  f_orbitPerBeat=orbit)


def hdri(p, intensity=1.0):
    return p.node("3DScene", "HDRI", 1200, 0, s_path=HDRI, f_intensity=intensity, f_rotation=0.0)


def sun(p, az, el, intensity):
    return p.node("3DScene", "Light", 1200, 400, i_type=0, f_azimuth=az, f_elevation=el,
                  c_color=(1.0, 0.97, 0.92), f_intensity=intensity)


def floor(p, x=200, y=900):
    # A flattened cube: a floor whatever way a Plane faces.
    return p.node("3DScene", "Geometry", x, y, i_shape=1, f_scaleX=30.0, f_scaleY=0.1, f_scaleZ=30.0,
                  f_posY=-0.05,
                  c_color=(0.5, 0.5, 0.52), f_metallic=0.0, f_roughness=0.8)


def output(p, src):
    o = p.node("Output", "Output", 2000, 600)
    p.cable(o, 0, src)
    return o


def wire(p, r, cam, light=None, env=None):
    p.geo(r, 4, cam)
    if light:
        p.geo(r, 5, light)
    if env:
        p.cable(r, 8, env)


# What Render 3D HQ reads (RenderHqNode::VisitParams); the rest stays default.
HQ_KEYS = {"width", "height", "projection", "fov", "orthoHeight", "camDistance", "camAzimuth",
           "camElevation", "targetX", "targetY", "targetZ", "near", "far", "lightAzimuth",
           "lightElevation", "lightColor", "lightIntensity", "envSky", "envHorizon", "envGround",
           "envIntensity", "envAsBackground", "bg", "tonemap", "exposure", "bloom",
           "bloomThreshold", "bloomRadius", "shadows", "shadowQuality", "shadowSoftness",
           "contactShadows", "ao", "aoRadius", "aoIntensity", "aoShow"}


def to_filament(p):
    # The same scene through Render 3D HQ: each Render 3D becomes one, in
    # place (same index, so every cable stays), keeping the params it reads.
    for i, (cat, typ, params, pos) in enumerate(p.nodes):
        if typ == "Render 3D":
            hq_params = {k: v for k, v in params.items() if k.split("_", 1)[1] in HQ_KEYS}
            p.nodes[i] = (cat, "Render 3D HQ", hq_params, pos)
    return p


SCENE_TYPES = {"Camera", "Light", "HDRI", "LFO"}
RENDER_TYPES = {"Render 3D", "Render 3D HQ"}
# 0.71.1: canvas units between columns and between nodes in a column, and the
# size assumed for a node type not measured yet (--measure fills the table).
GAP_X, GAP_Y = 140, 90
DEFAULT_SIZE = (340, 560)


def load_sizes():
    # scripts/hq_node_sizes.tsv: "type <tab> width <tab> height", the largest
    # each type measured in the app over all example patches.
    sizes = {}
    if os.path.exists(SIZES_FILE):
        with open(SIZES_FILE, encoding="utf-8") as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if len(parts) == 3 and not line.startswith("#"):
                    sizes[parts[0]] = (float(parts[1]), float(parts[2]))
    return sizes


NODE_SIZES = load_sizes()


def node_size(node):
    _cat, typ, params, _pos = node
    if typ == "Comment":
        # A Comment is as large as its own width / height plus its title bar,
        # frame and controls (measured in the app: + 16 wide, + 183 tall).
        return params.get("f_width", 260.0) + 16.0, params.get("f_height", 140.0) + 183.0
    return NODE_SIZES.get(typ, DEFAULT_SIZE)


def layout(p):
    # Columns by depth in the graph (a source at 0, each consumer one right of
    # its deepest input). Camera / light / HDRI / LFO go in the column just
    # left of the renderer. 0.71.1: each column as wide as its widest node and
    # each node right below the one above by that one's real height (no more
    # overlapping bodies); a Comment sits right above the node it explains,
    # unattached Comments at the top of the first column.
    inputs = {i: set() for i in range(1, len(p.nodes) + 1)}
    for line in p.links:
        kind, dst, _slot, src = line.split()[:4]
        if kind in ("geo", "cable"):
            inputs[int(dst)].add(int(src))
    depth = {}

    def d(i):
        if i not in depth:
            typ = p.nodes[i - 1][1]
            srcs = [s for s in inputs[i] if p.nodes[s - 1][1] not in SCENE_TYPES]
            depth[i] = 0 if typ in SCENE_TYPES or not srcs else 1 + max(d(s) for s in srcs)
        return depth[i]

    for i in inputs:
        if i not in p.attach:
            d(i)
    render_col = max([depth[i] for i in depth if p.nodes[i - 1][1] in RENDER_TYPES] or [1])
    for i in depth:
        if p.nodes[i - 1][1] in SCENE_TYPES:
            depth[i] = max(render_col - 1, 0)
    # Column contents in order: free Comments first, then each node preceded
    # by the Comments attached to it.
    notes_of = {}
    for note, target in p.attach.items():
        notes_of.setdefault(target, []).append(note)
    columns = {}
    for i in sorted(inputs):
        if i in p.attach:
            continue
        if p.nodes[i - 1][1] == "Comment":
            columns.setdefault(0, []).insert(0, i)
            continue
        col = depth[i]
        columns.setdefault(col, []).extend(notes_of.get(i, []) + [i])
    x = 0.0
    for col in sorted(columns):
        y = 0.0
        width = 0.0
        for i in columns[col]:
            cat, typ, params, _pos = p.nodes[i - 1]
            w, h = node_size(p.nodes[i - 1])
            p.nodes[i - 1] = (cat, typ, params, (int(x), int(y)))
            y += h + GAP_Y
            width = max(width, w)
        x += width + GAP_X
    return p


def scene_a():
    p = Patch()
    r = render(p)
    cam = camera(p, 12.0, 90.0, 10.0, ty=0.9)
    env = hdri(p)
    light = sun(p, -40.0, 35.0, 2.5)
    group = p.node("3DScene", "Group 3D", 1000, 600)
    spheres = [
        dict(c_color=(1.0, 0.78, 0.34), f_metallic=1.0, f_roughness=0.05),            # polished gold
        dict(c_color=(0.9, 0.9, 0.92), f_metallic=1.0, f_roughness=0.55),             # rough steel
        dict(c_color=(0.75, 0.12, 0.10), f_metallic=0.0, f_roughness=0.35),           # dielectric
        dict(c_color=(0.10, 0.35, 0.85), f_metallic=0.0, f_roughness=0.08, f_clearcoat=1.0),  # glossy plastic
        dict(c_color=(0.05, 0.05, 0.05), f_metallic=0.0, f_roughness=0.5,
             c_emissionColor=(1.0, 0.55, 0.15), f_emission=4.0),                     # emissive
        dict(c_color=(0.8, 0.8, 0.8), f_metallic=0.0, f_roughness=0.4),               # normal-mapped
    ]
    ids = []
    for i, m in enumerate(spheres):
        ids.append(p.node("3DScene", "Geometry", 200, 100 + i * 120, i_shape=2,
                          f_posX=-5.0 + i * 2.0, f_posY=0.85, f_scale=1.6, **m))
    # The last sphere through a Material with a procedural normal (bump) map.
    tex = p.node("Source", "Texture", 0, 800, i_textureType=0, f_width=1024, f_height=1024, f_scale=6.0)
    mat = p.node("3DScene", "Material", 600, 800, c_color=(0.8, 0.8, 0.8), f_metallic=0.0,
                 f_roughness=0.4, b_normalBump=True, f_normalStrength=1.5)
    p.geo(mat, 0, ids[5])
    p.cable(mat, 4, tex)
    fl = floor(p)
    for slot, src in enumerate(ids[:5] + [mat, fl]):
        p.geo(group, slot, src)
    p.geo(r, 0, group)
    wire(p, r, cam, light, env)
    output(p, r)
    # The studio turns: an LFO saw sweeps the HDRI's rotation over its whole
    # range (-180..180, so the wrap is seamless), one turn every 32 beats.
    lfo = p.node("Modulators", "LFO", 0, 0, i_shape=2, i_rateMode=0, f_rateBeats=32.0, f_low=0.0, f_high=1.0)
    p.links.append("mod %d rotation %d 0 0 1 0" % (env, lfo))
    return p


def points(p, rows, size, x=0, y=1200):
    return p.node("PointZ", "Grid PointZ", x, y, f_sizeX=size, f_sizeY=size, f_sizeZ=0.0,
                  i_rows=rows, i_cols=rows, i_orient=1, i_kind=2)


def noise(p, amp, speed, typ=2, x=300, y=1200, freq=1.2):
    return p.node("PointZ", "Noise PointZ", x, y, s_attrib="P", i_type=typ, i_mode=0, f_amp=amp,
                  f_freq=freq, b_use4d=True, f_speed=speed)


def particles(p, rate, life, mx, x=600, y=1200, **extra):
    params = dict(i_birth=0, f_rate=rate, f_life=life, f_lifeVar=0.1, f_velX=0.0, f_velY=0.3,
                  f_velZ=0.0, f_velRand=0.2, f_drag=2.0, f_damp=0.6, i_max=mx, b_play=True,
                  f_sizeBirth=0.015, f_sizeDeath=0.004, f_turb=6.0, f_turbScale=0.7, f_turbSpeed=0.05,
                  i_engine=1)
    params.update(extra)
    return p.node("PointZ", "Particle PointZ", x, y, **params)


def scene_b():
    p = Patch()
    r = render(p)
    cam = camera(p, 9.0, -30.0, 18.0, orbit=0.03)
    light = sun(p, 90.0, 12.0, 3.0)   # from the side
    env = hdri(p, 0.6)
    g = points(p, 300, 6.0)
    n = noise(p, 0.6, 0.0)
    p.geo(n, 0, g)
    p.geo(r, 0, n)
    p.geo(r, 1, floor(p))
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def cloud(p):
    g = points(p, 100, 3.0)
    n = noise(p, 0.3, 0.0)
    p.geo(n, 0, g)
    part = particles(p, 60000.0, 3.0, 400000)
    p.geo(part, 0, n)
    return part


def scene_c():
    p = Patch()
    r = render(p)
    cam = camera(p, 8.0, -25.0, 15.0, ty=1.0, orbit=0.02)
    light, env = sun(p, -40.0, 35.0, 2.0), hdri(p)
    p.geo(r, 0, cloud(p))
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def scene_d():
    p = Patch()
    r = render(p, b_envAsBackground=False, c_bg=(0.0, 0.0, 0.0), f_envIntensity=0.15,
               f_bloom=0.8, f_bloomThreshold=0.6, f_bloomRadius=0.6, b_shadows=False)
    cam = camera(p, 8.0, -25.0, 15.0, ty=1.0, orbit=0.02)
    env = hdri(p, 0.15)
    mat = p.node("3DScene", "Material", 900, 1200, c_color=(1.0, 0.45, 0.1), f_metallic=0.0,
                 f_roughness=0.5, c_emissionColor=(1.0, 0.5, 0.15), f_emission=6.0)
    p.geo(mat, 0, cloud(p))
    p.geo(r, 0, mat)
    wire(p, r, cam, None, env)
    output(p, r)
    return p


def scene_e():
    p = Patch()
    r = render(p)
    cam = camera(p, 9.0, -25.0, 15.0, ty=1.2, orbit=0.02)
    light, env = sun(p, -40.0, 35.0, 2.5), hdri(p)
    knot = p.node("3DScene", "Geometry", 200, 100, i_shape=7, f_posY=1.4, f_scale=1.1,
                  c_color=(0.95, 0.64, 0.54), f_metallic=1.0, f_roughness=0.2)
    p.geo(r, 0, knot)
    p.geo(r, 1, cloud(p))
    p.geo(r, 2, floor(p))
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def scene_f():
    p = Patch()
    r = render(p, b_shadows=False)
    cam = camera(p, 10.0, -30.0, 25.0)
    light, env = sun(p, 90.0, 20.0, 3.0), hdri(p, 0.6)
    g = points(p, 1000, 8.0)
    n = noise(p, 0.8, 0.3)
    p.geo(n, 0, g)
    p.geo(r, 0, n)
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def group_of(p, sources, x=1000, y=600):
    g = p.node("3DScene", "Group 3D", x, y)
    for slot, src in enumerate(sources):
        p.geo(g, slot, src)
    return g


def scene_g():
    # Shadows under objects on / just above the floor, seen from low down.
    # Unit sphere radius 0.5 (scale 1.6 -> 0.8); cube and cylinder 1 tall.
    p = Patch()
    r = render(p)
    cam = camera(p, 7.5, 90.0, 12.0, ty=0.5)
    env = hdri(p, 0.8)
    light = sun(p, -40.0, 50.0, 2.5)
    objs = [
        p.node("3DScene", "Geometry", 200, 100, i_shape=2, f_posX=-3.3, f_posY=0.80, f_scale=1.6,
               c_color=(1.0, 0.78, 0.34), f_metallic=1.0, f_roughness=0.15),               # gold, touching
        p.node("3DScene", "Geometry", 200, 220, i_shape=2, f_posX=-1.1, f_posY=1.00, f_scale=1.6,
               c_color=(0.75, 0.12, 0.10), f_metallic=0.0, f_roughness=0.35, f_clearcoat=1.0),  # 20 cm up
        p.node("3DScene", "Geometry", 200, 340, i_shape=1, f_posX=1.1, f_posY=0.52, f_scale=1.0,
               c_color=(0.20, 0.40, 0.80), f_metallic=0.0, f_roughness=0.7),                # 2 cm up
        p.node("3DScene", "Geometry", 200, 460, i_shape=5, f_posX=3.1, f_posY=0.5, f_scale=1.0,
               c_color=(0.95, 0.64, 0.54), f_metallic=1.0, f_roughness=0.3),                # copper, touching
    ]
    p.geo(r, 0, group_of(p, objs + [floor(p)]))
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def scene_h():
    # Ambient occlusion at contacts and creases: a floor-wall corner and
    # objects resting on the floor, from 35 degrees up. A third render (HQ,
    # aoShow) shows the occlusion alone.
    p = Patch()
    r = render(p)
    cam = camera(p, 8.0, 70.0, 35.0, ty=0.6)
    env = hdri(p)
    light = sun(p, 30.0, 45.0, 2.0)   # in front of the wall, from the right
    wall = p.node("3DScene", "Geometry", 200, 600, i_shape=1, f_posY=1.5, f_posZ=-2.0,
                  f_scaleX=10.0, f_scaleY=3.0, f_scaleZ=0.2, c_color=(0.75, 0.75, 0.76), f_roughness=0.9)
    objs = [
        p.node("3DScene", "Geometry", 200, 100, i_shape=1, f_posX=-2.4, f_posY=0.5, f_posZ=-1.4,
               c_color=(0.85, 0.85, 0.85), f_metallic=0.0, f_roughness=0.9),                # matte, against the wall
        p.node("3DScene", "Geometry", 200, 220, i_shape=2, f_posX=-0.6, f_posY=0.8, f_scale=1.6,
               c_color=(0.10, 0.35, 0.85), f_metallic=0.0, f_roughness=0.15),               # glossy plastic
        p.node("3DScene", "Geometry", 200, 340, i_shape=5, f_posX=1.4, f_posY=0.5, f_scale=1.0,
               c_color=(0.9, 0.9, 0.92), f_metallic=1.0, f_roughness=0.1),                  # polished metal
        p.node("3DScene", "Geometry", 200, 460, i_shape=7, f_posX=3.0, f_posY=0.75, f_scale=0.9,
               c_color=(0.55, 0.55, 0.58), f_metallic=1.0, f_roughness=0.6),                # rough steel knot
    ]
    group = group_of(p, objs + [wall, floor(p)])
    p.geo(r, 0, group)
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def scene_i():
    # ~100k boxes instanced on a noisy grid (Instance PointZ), the shapes of
    # the "1M cubes" patches; metal, lit from the side.
    p = Patch()
    r = render(p)
    cam = camera(p, 9.0, -30.0, 28.0, ty=0.3, orbit=0.02)
    light, env = sun(p, 60.0, 30.0, 2.5), hdri(p, 0.8)
    g = points(p, 316, 6.0)
    n = noise(p, 0.5, 0.2)
    p.geo(n, 0, g)
    box = p.node("PointZ", "Box PointZ", 0, 0)
    inst = p.node("PointZ", "Instance PointZ", 0, 0, f_scale=0.016)
    p.geo(inst, 0, n)
    p.geo(inst, 1, box)
    mat = p.node("3DScene", "Material", 0, 0, c_color=(0.9, 0.62, 0.45), f_metallic=1.0, f_roughness=0.3)
    p.geo(mat, 0, inst)
    p.geo(r, 0, mat)
    p.geo(r, 1, floor(p))
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def scene_j():
    # 0.71.1, Render 3D HQ only: a million grains of sand (GPU particles drawn
    # as dots) settling on dunes and parted by a wandering repulsor.
    #   - the dunes: a flat grid takes only P.y from a grid run through noise
    #     (Lookup Attribute "P.y", TouchDesigner-style), slowly drifting;
    #   - the sand: 1000 x 1000 points born once (per point, life 600 s), each
    #     pulled to its own dune point (Force Target), pushed and swirled by a
    #     repulsor that wanders over them (Force Radial, centre by expression),
    #     resting on the floor (Collide), looped through the Null tagged "sand".
    p = Patch()
    res, size = 1000, 6.0
    dunes_flat = p.node("PointZ", "Grid PointZ", 0, 0, f_sizeX=size, f_sizeY=size, f_sizeZ=0.0,
                        i_rows=res, i_cols=res, i_orient=1, i_kind=2, f_centerY=0.6)
    dunes_noise = p.node("PointZ", "Grid PointZ", 0, 0, f_sizeX=size, f_sizeY=size, f_sizeZ=0.0,
                         i_rows=res, i_cols=res, i_orient=1, i_kind=2, f_centerY=0.6)
    n = noise(p, 0.45, 0.06, freq=0.55)
    p.geo(n, 0, dunes_noise)
    # Each grain's resting place moved by up to one grid step in x and z: a
    # regular lattice of grains shows moire stripes, sand does not.
    step = size / res
    scatter = p.node("PointZ", "Random PointZ", 0, 0, s_attrib="P", i_mode=1, f_loX=-step, f_loY=0.0,
                     f_loZ=-step, f_hiX=step, f_hiY=0.0, f_hiZ=step, f_seed=7.0)
    p.geo(scatter, 0, dunes_flat)
    dunes = p.node("PointZ", "Lookup Attribute PointZ", 0, 0, s_attribs="P.y")
    p.geo(dunes, 0, scatter)
    p.geo(dunes, 1, n)
    source = p.node("PointZ", "Grid PointZ", 0, 0, f_sizeX=size, f_sizeY=size, f_sizeZ=0.0,
                    i_rows=res, i_cols=res, i_orient=1, i_kind=2, f_centerY=4.0)
    sand = p.node("PointZ", "Particle PointZ", 0, 0, i_birth=1, i_max=res * res, f_life=600.0, f_lifeVar=0.0,
                  f_velX=0.0, f_velY=0.0, f_velZ=0.0, f_velRand=0.05, f_gravityY=-3.0, f_drag=0.3, f_damp=0.0,
                  f_sizeBirth=1.0, f_sizeDeath=1.0, f_turb=0.4, f_turbScale=1.5, f_turbSpeed=0.2,
                  s_target="sand", i_engine=1, b_play=True)
    p.geo(sand, 0, source)
    attract = p.node("PointZ", "Force Target PointZ", 0, 0, f_strength=8.0, f_damping=3.0, i_match=0,
                     f_limit=40.0)
    p.geo(attract, 0, sand)
    p.geo(attract, 1, dunes)
    repel = p.node("PointZ", "Force Radial PointZ", 0, 0, f_centerX=0.0, f_centerY=0.6, f_centerZ=0.0,
                   f_strength=-45.0, f_radius=1.1, i_falloff=1, f_swirl=10.0)
    p.geo(repel, 0, attract)
    p.links.append("expr %d centerX sin(t * 0.35) * 2.4" % repel)
    p.links.append("expr %d centerZ cos(t * 0.27) * 2.0" % repel)
    floor_hit = p.node("PointZ", "Collide PointZ", 0, 0, f_bounce=0.1, f_friction=0.6)
    p.geo(floor_hit, 0, repel)
    loop = p.node("PointZ", "Null PointZ", 0, 0, s_tag="sand")
    p.geo(loop, 0, floor_hit)
    mat = p.node("3DScene", "Material", 0, 0, c_color=(0.86, 0.68, 0.45), f_metallic=0.0, f_roughness=0.85)
    p.geo(mat, 0, loop)
    r = p.node("3DScene", "Render 3D HQ", 1600, 600, f_width=1920, f_height=1080, i_tonemap=1, f_exposure=1.0,
               f_envIntensity=0.7, b_envAsBackground=False, c_bg=(0.05, 0.05, 0.07), b_shadows=True,
               i_shadowQuality=2, f_shadowSoftness=1.0, b_ao=True, f_aoRadius=0.4, f_aoIntensity=1.0,
               b_taa=True, i_pointStyle=1, f_dotSize=1.6)
    cam = camera(p, 7.5, -35.0, 28.0, ty=0.5, orbit=0.01)
    light, env = sun(p, -40.0, 22.0, 3.2), hdri(p, 0.7)
    p.geo(r, 0, mat)
    p.geo(r, 1, floor(p))
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def scene_k():
    # 0.71.1, Render 3D HQ only: emissive objects lighting the scene through
    # screen-space GI - three glowing spheres and a row of instanced glowing
    # cubes in a dim room (a light floor and wall); the HDRI barely lights it.
    p = Patch()
    wall = p.node("3DScene", "Geometry", 0, 0, i_shape=1, f_posY=1.5, f_posZ=-2.2, f_scaleX=12.0, f_scaleY=3.0,
                  f_scaleZ=0.2, c_color=(0.8, 0.8, 0.8), f_roughness=0.8)
    glow = [
        dict(f_posX=-2.2, c_emissionColor=(1.0, 0.45, 0.1)),
        dict(f_posX=0.0, c_emissionColor=(0.1, 0.7, 1.0)),
        dict(f_posX=2.2, c_emissionColor=(1.0, 0.2, 0.7)),
    ]
    spheres = [p.node("3DScene", "Geometry", 0, 0, i_shape=2, f_posY=0.6, f_posZ=-0.6, f_scale=1.2,
                      c_color=(0.05, 0.05, 0.05), f_roughness=0.5, f_emission=6.0, **g) for g in glow]
    row = p.node("PointZ", "Grid PointZ", 0, 0, f_sizeX=6.0, f_sizeY=0.0, f_sizeZ=0.0, i_rows=1, i_cols=12,
                 i_orient=1, i_kind=2, f_centerY=0.15, f_centerZ=1.2)
    cube = p.node("PointZ", "Box PointZ", 0, 0)
    inst = p.node("PointZ", "Instance PointZ", 0, 0, f_scale=0.3)
    p.geo(inst, 0, row)
    p.geo(inst, 1, cube)
    cubes = p.node("3DScene", "Material", 0, 0, c_color=(0.05, 0.05, 0.05), f_roughness=0.5,
                   c_emissionColor=(1.0, 0.85, 0.5), f_emission=4.0)
    p.geo(cubes, 0, inst)
    r = p.node("3DScene", "Render 3D HQ", 1600, 600, f_width=1920, f_height=1080, i_tonemap=1, f_exposure=1.0,
               f_envIntensity=0.15, b_envAsBackground=False, c_bg=(0.02, 0.02, 0.03), b_shadows=True,
               i_shadowQuality=1, b_ao=True, f_aoRadius=0.6, b_taa=True, f_bloom=0.3, f_bloomThreshold=1.0,
               b_gi=True, f_giIntensity=1.5, f_giRadius=2.0)
    cam = camera(p, 8.0, 90.0, 18.0, ty=0.6)
    env = hdri(p, 0.15)
    light = sun(p, -40.0, 50.0, 0.2)   # barely there: the glow does the lighting
    p.geo(r, 0, group_of(p, spheres + [wall, floor(p)]))
    p.geo(r, 1, cubes)
    wire(p, r, cam, light, env)
    output(p, r)
    return p


def scene_l():
    # 0.71.1: a tutorial for Attribute PointZ, in Comments on the canvas - what
    # it does and every setting, what an expression can read, then four of them
    # in a row shaping boxes on a noisy grid (Instance PointZ reads PScale,
    # Scale, Rotate and Color per point).
    p = Patch()
    p.note(
        "ATTRIBUTE POINTZ - tutorial\n"
        "\n"
        "Writes one attribute on every point: a number, a vector or a colour. Add it from the node menu: "
        "PointZ > Attribute PointZ. Plug any PointZ in, and what comes out is the same points with that "
        "attribute set.\n"
        "\n"
        "Settings\n"
        "- attribute: the name to write. P (position), N (normal), Color, PScale (size), Scale (size per "
        "axis), Rotate (degrees), Tex (uv), PartVel... or a new name of your own (Height, Mask).\n"
        "- components: 1 = one number, 2 / 3 / 4 = a vector (x y z w = r g b a).\n"
        "- value: where it comes from -\n"
        "   constant: 'value' (and 'value (4th)' for the 4th component)\n"
        "   copy attribute: another attribute, 'source attribute'\n"
        "   index: the point number 0, 1, 2...\n"
        "   index 0..1: the same, spread from 0 to 1\n"
        "   expression: one GLSL expression, 'expression' (below)\n"
        "\n"
        "In an expression you can read\n"
        "- every attribute the points already have, by name: P, N, Color, Tex, PScale, PartAge... "
        "(P.x, P.y, P.z or Color.r for one component)\n"
        "- id (this point's number), uCount (how many points), uTime (seconds since play), uBeat, "
        "uFrame, uDelta (seconds per frame), uSeed\n"
        "- GLSL: sin cos abs fract floor mod mix clamp smoothstep length distance dot normalize pow "
        "min max, vec2 / vec3 / vec4(...)\n"
        "- noise and random: gnoise(vec3) (-1..1), fbm(vec3, octaves, roughness), curl(vec3), "
        "hashi(id, seed) and hash3i(id, seed) (0..1 per point), hash11(x), hash31(vec3); PI, TAU\n"
        "One expression, no ';' or '{'. Its size must match 'components': vec3(...) for 3.\n"
        "\n"
        "The chain below: a 40 x 40 grid, Noise moves it, four Attribute PointZ shape the boxes that "
        "Instance PointZ puts on every point. Change the expressions and watch the render.",
        width=760, height=900, font=1, color=(0.55, 0.8, 0.95))
    g = p.node("PointZ", "Grid PointZ", 0, 0, f_sizeX=6.0, f_sizeY=6.0, f_sizeZ=0.0, i_rows=40, i_cols=40,
               i_orient=1, i_kind=2)
    p.note("A grid of 40 x 40 points (output: points), lying flat (orientation XZ).", attach=g, height=90)
    n = noise(p, 0.5, 0.25, freq=0.6)
    p.geo(n, 0, g)
    p.note("Noise PointZ moves P up and down over time: now every point has its own P.y for the "
           "expressions after it to use.", attach=n, height=110)
    steps = [
        ("PScale", 1, "0.4 + abs(P.y) * 3.0",
         "1  attribute PScale, 1 component, expression:\n0.4 + abs(P.y) * 3.0\n\nThe higher (or lower) the "
         "point, the bigger its box. Try length(P.xz) * 0.6: bigger towards the edges."),
        ("Scale", 3, "vec3(1.0, 1.0 + abs(P.y) * 4.0, 1.0)",
         "2  attribute Scale, 3 components:\nvec3(1.0, 1.0 + abs(P.y) * 4.0, 1.0)\n\nSize per axis: only Y "
         "stretches - columns. It multiplies PScale."),
        ("Rotate", 3, "vec3(0.0, hashi(id, 1.0) * 360.0, 0.0)",
         "3  attribute Rotate, 3 components (degrees X Y Z):\nvec3(0.0, hashi(id, 1.0) * 360.0, 0.0)\n\n"
         "A random turn around Y per box. hashi(id, seed) gives each point its own number, the same "
         "every frame; change the seed for another pattern."),
        ("Color", 4, "vec4(0.5 + 0.5 * sin(P.x * 2.0 + uTime), 0.35, 0.5 + 0.5 * cos(P.z * 2.0), 1.0)",
         "4  attribute Color, 4 components (r g b a):\nvec4(0.5 + 0.5 * sin(P.x * 2.0 + uTime), 0.35,\n"
         "     0.5 + 0.5 * cos(P.z * 2.0), 1.0)\n\nColour by position, moving with uTime (seconds)."),
    ]
    last = n
    for name, comps, expr, text in steps:
        a = p.node("PointZ", "Attribute PointZ", 0, 0, s_name=name, i_comps=comps, i_from=4, s_expr=expr)
        p.geo(a, 0, last)
        p.note(text, attach=a, width=420, height=200)
        last = a
    box = p.node("PointZ", "Box PointZ", 0, 0)
    inst = p.node("PointZ", "Instance PointZ", 0, 0, f_scale=0.07)
    p.geo(inst, 0, last)
    p.geo(inst, 1, box)
    p.note("Instance PointZ draws the shape on input 2 (a box) at every point of input 1, and per point "
           "reads P (where), PScale (size), Scale (size per axis), Rotate (degrees), Color, Active (0 hides "
           "it) and N (aim, with 'aim along N'). 'scale' here multiplies them all.", attach=inst, height=170)
    mat = p.node("3DScene", "Material", 0, 0, c_color=(1.0, 1.0, 1.0), f_metallic=0.0, f_roughness=0.35)
    p.geo(mat, 0, inst)
    p.note("Material: white, so the boxes show their own Color.", attach=mat, height=80)
    r = p.node("3DScene", "Render 3D HQ", 0, 0, f_width=1920, f_height=1080, i_tonemap=1, f_exposure=1.0,
               f_envIntensity=0.8, b_envAsBackground=True, b_shadows=True, i_shadowQuality=1, b_ao=True,
               f_aoRadius=0.5, b_taa=True)
    cam = camera(p, 9.0, -35.0, 30.0, ty=0.3, orbit=0.02)
    light, env = sun(p, -40.0, 40.0, 2.5), hdri(p, 0.8)
    p.geo(r, 0, mat)
    p.geo(r, 1, floor(p))
    wire(p, r, cam, light, env)
    output(p, r)
    return p


# 0.71.1: scenes for Render 3D HQ only (they use what only it has).
HQ_ONLY_SCENES = {
    "J_sand_1m": scene_j,
    "K_emissive_gi": scene_k,
    "L_tutorial_attribute_pointz": scene_l,
}


SCENES = {
    "A_material_studio": scene_a,
    "B_particle_studio": scene_b,
    "C_generative_cloud": scene_c,
    "D_emissive_particles": scene_d,
    "E_geometry_particles": scene_e,
    "F_points_1m": scene_f,
    "G_shadow_contact": scene_g,
    "H_ao_contact": scene_h,
    "I_instances_100k": scene_i,
}


def write_if_changed(path, text):
    # 0.71.1: only scenes whose content changed are written (line endings do
    # not count), so a run leaves the other patches - and git - alone.
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            if f.read().replace("\r\n", "\n") == text:
                return
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", os.path.normpath(path))


def hq_scenes():
    # name -> Patch through Render 3D HQ (A-I converted, J-L HQ-only).
    scenes = {name: (lambda make=make: to_filament(make())) for name, make in SCENES.items()}
    scenes.update(HQ_ONLY_SCENES)
    return scenes


def write_hq():
    paths = []
    for name, make in hq_scenes().items():
        path = os.path.join(OUT_DIR, name + ".inf")
        write_if_changed(path, layout(make()).text())
        paths.append(path)
    return paths


def write_raster():
    # BEFORE / AFTER comparisons only: the A-I scenes through Render 3D, in
    # build/ (not shipped), the HDRI path pointing back at examples/hq.
    os.makedirs(RASTER_DIR, exist_ok=True)
    media = os.path.relpath(os.path.join(OUT_DIR, HDRI), RASTER_DIR).replace(os.sep, "/")
    for name, make in SCENES.items():
        text = layout(make()).text().replace(" " + HDRI + "\n", " " + media + "\n")
        write_if_changed(os.path.join(RASTER_DIR, name + ".inf"), text)


def measure(exe, paths):
    # Opens each patch in the app (INFINITE_LOADPATCH), which writes every
    # node's canvas size (INFINITE_NODESIZES, frame 20) and quits after a
    # screenshot (frame 30); keeps each type's largest size.
    sizes = {}
    with tempfile.TemporaryDirectory() as tmp:
        for path in paths:
            out = os.path.join(tmp, "sizes.tsv")
            env = dict(os.environ, INFINITE_LOADPATCH=os.path.abspath(path), INFINITE_NODESIZES=out,
                       IMAGERESYNTH_SCREENSHOT=os.path.join(tmp, "shot.png"), INFINITE_SCREENSHOT_FRAME="30")
            subprocess.run([exe], env=env, timeout=120)
            if not os.path.exists(out):
                print("no sizes from", path)
                continue
            with open(out, encoding="utf-8", errors="replace") as f:
                for line in f:
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) == 3 and parts[0] != "Comment":
                        w, h = float(parts[1]), float(parts[2])
                        ow, oh = sizes.get(parts[0], (0.0, 0.0))
                        sizes[parts[0]] = (max(ow, w), max(oh, h))
            os.remove(out)
    with open(SIZES_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Canvas size of each node type in the example patches (largest seen), measured by\n"
                "# python scripts/make_hq_scenes.py --measure <Infinite-Turbo.exe>. Used by layout().\n")
        for typ in sorted(sizes):
            f.write("%s\t%.0f\t%.0f\n" % (typ, sizes[typ][0], sizes[typ][1]))
    print("measured", len(sizes), "node types into", os.path.normpath(SIZES_FILE))


def main(argv):
    global NODE_SIZES
    paths = write_hq()
    if "--measure" in argv:
        measure(argv[argv.index("--measure") + 1], paths)
        NODE_SIZES = load_sizes()
        paths = write_hq()
    if "--raster" in argv:
        write_raster()


if __name__ == "__main__":
    main(sys.argv[1:])
