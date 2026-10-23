"""Writes the Render 3D HQ reference scenes (0.71) to examples/hq.

Every scene twice, one renderer per patch: examples/hq/raster/<scene>.inf
through Render 3D and examples/hq/filament/<scene>.inf through Render 3D HQ
(same sources, camera, lights, HDRI; to_filament). Nodes are laid out in
columns by graph depth (layout). 1920 x 1080. Missing keys take the node
defaults, so only what matters is written. The HDRI is
examples/hq/hq_media/studio.hdr (scripts/make_hq_studio_hdr.py).

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

Usage: python scripts/make_hq_scenes.py
"""
import os

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "examples", "hq")
# The patches sit in examples/hq/raster and examples/hq/filament; the HDRI
# they share is examples/hq/hq_media (relative paths resolve next to the .inf).
HDRI = "../hq_media/studio.hdr"


class Patch:
    def __init__(self):
        self.nodes = []   # (category, type, params, pos)
        self.links = []   # text lines

    def node(self, category, type_name, x, y, **params):
        self.nodes.append((category, type_name, params, (x, y)))
        return len(self.nodes)

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
COL_W, ROW_H, RENDER_W = 520, 560, 980


def layout(p):
    # Columns by depth in the graph (a source at 0, each consumer one right of
    # its deepest input), nodes stacked in each column with room for their
    # bodies. Camera / light / HDRI / LFO go in the column just left of the
    # renderer; the output (and anything after a renderer) clears its wide body.
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
        d(i)
    render_col = max([depth[i] for i in inputs if p.nodes[i - 1][1] in RENDER_TYPES] or [1])
    for i in inputs:
        if p.nodes[i - 1][1] in SCENE_TYPES:
            depth[i] = max(render_col - 1, 0)
    rows = {}
    for i in sorted(inputs):
        col = depth[i]
        row = rows.get(col, 0)
        rows[col] = row + 1
        x = col * COL_W + (RENDER_W - COL_W if col > render_col else 0)
        cat, typ, params, _pos = p.nodes[i - 1]
        p.nodes[i - 1] = (cat, typ, params, (x, row * ROW_H))
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


SCENES = {
    "A_material_studio": scene_a,
    "B_particle_studio": scene_b,
    "C_generative_cloud": scene_c,
    "D_emissive_particles": scene_d,
    "E_geometry_particles": scene_e,
    "F_points_1m": scene_f,
    "G_shadow_contact": scene_g,
    "H_ao_contact": scene_h,
}


def main():
    # One renderer per patch: raster/ (Render 3D) and filament/ (Render 3D HQ).
    for variant, convert in (("raster", lambda p: p), ("filament", to_filament)):
        out_dir = os.path.join(OUT_DIR, variant)
        os.makedirs(out_dir, exist_ok=True)
        for name, make in SCENES.items():
            path = os.path.join(out_dir, name + ".inf")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(layout(convert(make())).text())
            print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
