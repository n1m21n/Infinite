# Material-domain Field programs for the judge table (judge_table.py). Each starts from a recipe in
# src/nodes/FieldMaterialRecipes.h (its outputs renamed to locals), adds the flaws, and ends in one tail that writes
# the outputs from the locals oB oR oM oH. P is Infinite's Y-up world in metres (Blender x, z, -y), table top y = 0.
# tail("rm") writes roughness and metallic as the base colour instead, for bake_maps.py.
import math
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def recipe(name, subs=()):
    src = open(os.path.join(REPO, "src", "nodes", "FieldMaterialRecipes.h")).read()
    code = dict(re.findall(r'FLD_RECIPE\("([^"]+)", R"FLD\((.*?)\)FLD"\)', src, re.S))[name]
    code = "\n".join(l for l in code.split("\n") if not l.startswith("#"))
    for out, loc in (("base", "b0"), ("roughness", "r0"), ("metallic", "m0"), ("height", "h0")):
        code = re.sub(r"(?m)^%s = " % out, loc + " = ", code)
    for a, b in subs:
        assert a in code, a
        code = code.replace(a, b)
    return code + "\n"


TAIL = "base = oB;\nroughness = oR;\nmetallic = oM;\nheight = oH;"
TAIL_RM = "base = vec3(oR, oM, 0.0);\nroughness = 1.0;\nmetallic = 0.0;\nheight = oH;"


def table(flaws):
    c = recipe("Painted Wood", [("vec3(0.05, 0.17, 0.11)", "vec3(0.085, 0.19, 0.185)")])
    if not flaws:
        return c + "oB = b0;\noR = r0;\noM = 0.0;\noH = h0;\n"
    return c + """q = vec2(P.x * 0.8 + P.z * 0.6, P.z * 0.8 - P.x * 0.6);
s1 = smoothstep(0.50, 0.58, noise3(vec3(P.x * 6.0, 3.1, P.z * 1300.0)));
s2 = smoothstep(0.54, 0.60, noise3(vec3(q.x * 4.0, 7.7, q.y * 1700.0)));
scr = clamp(s1 + s2, 0.0, 1.0) * smoothstep(-0.2, 0.3, fbm(P * 5.0, 3.0, 2.0, 0.5));
wn = fbm(P * 70.0, 4.0, 2.0, 0.5);
wear = smoothstep(0.35, 0.65, (curvature - 22.0) / 30.0 + wn * 0.9);
wood = vec3(0.40, 0.28, 0.17) * (0.75 + 0.35 * grain);
dust = clamp((1.0 - grain) * 0.5 + scr, 0.0, 1.0) * smoothstep(0.0, 0.5, fbm(P * 9.0, 4.0, 2.0, 0.5) + 0.2) * 0.35;
b1 = mix(b0, b0 * 1.5 + 0.04, scr * 0.7);
b1 = mix(b1, vec3(0.55, 0.52, 0.47), dust);
oB = mix(b1, wood, wear);
oR = mix(r0 + scr * 0.3 + dust * 0.4, 0.72, wear);
oM = 0.0;
oH = h0 - scr * 0.00012 - wear * 0.00018;
"""


def table_dust():
    return ("n = fbm(P * 40.0, 4.0, 2.0, 0.5);\noB = vec3(0.58, 0.55, 0.50) * (0.9 + 0.1 * n);\noR = 0.95;\noM = 0.0;\n"
            "oH = 0.0;\n")


# Image-domain masks for the Material mask pin (a picture: its red is the layer's cover).
TABLE_DUST_MASK = ("n = fbm(vec3(uv.x * 14.0, uv.y * 14.0, 0.5), 5.0, 2.0, 0.5) * 0.5 + 0.5;\n"
                   "col = vec3(smoothstep(0.5, 0.85, n) * 0.3);")
MUG_STAIN_MASK = ("n = noise3(vec3(uv.x * 260.0, uv.y * 180.0, 1.7)) * 0.5 + 0.5;\n"
                  "col = vec3(smoothstep(0.83, 0.88, n));")


def mug(flaws):
    # No ceramic recipe exists: a short glaze program in the recipes' style, then Edge Wear's and Dirt's maths.
    c = """f = fbm(P * 14.0, 4.0, 2.0, 0.5);
b0 = vec3(0.76, 0.71, 0.62) * (0.97 + 0.05 * f);
r0 = 0.26;
h0 = fbm(P * 90.0, 3.0, 2.0, 0.5) * 0.00002;
"""
    if not flaws:
        return c + "oB = b0;\noR = r0;\noM = 0.0;\noH = h0;\n"
    return c + """ax = vec2(P.x + 0.075, P.z + 0.045);
rad = length(ax);
pool = fbm(vec3(P.x * 22.0, P.y * 7.0, P.z * 22.0), 4.0, 2.0, 0.5) * 0.5 + 0.5;
b1 = b0 * mix(vec3(1.0), vec3(0.86, 0.80, 0.68), pool * 0.8);
gate = smoothstep(0.05, 0.3, fbm(P * 45.0, 3.0, 2.0, 0.5));
e = (curvature - 7.0) / 6.0 + fbm(P * 260.0, 4.0, 2.0, 0.5) * 1.3;
chip = smoothstep(0.35, 0.65, e) * gate * step(0.08, P.y);
inside = step(rad, 0.0385) * step(0.0085, P.y);
wob = fbm(P * 60.0, 3.0, 2.0, 0.5) * 0.0015;
dy = (P.y - 0.056 + wob) / 0.0011;
ring = exp(-dy * dy) * inside;
film = inside * (1.0 - smoothstep(0.050, 0.057, P.y + wob)) * 0.35;
tea = clamp(ring * 0.85 + film, 0.0, 1.0);
b1 = mix(b1, vec3(0.30, 0.16, 0.06), tea);
oB = mix(b1, vec3(0.50, 0.44, 0.36), chip);
oR = mix(r0 + pool * 0.10 + tea * 0.25, 0.85, chip);
oM = 0.0;
oH = h0 + pool * 0.00006 - chip * 0.0004;
"""


def mug_stain():
    return "oB = vec3(0.16, 0.09, 0.04);\noR = 0.45;\noM = 0.0;\noH = 0.0;\n"


def glass(flaws):
    if not flaws:
        return "oB = vec3(1.0);\noR = 0.0;\noM = 0.0;\noH = 0.0;\n"
    return """ax = vec2(P.x - 0.105, P.z + 0.095);
ang = atan2(ax.y, ax.x);
c1 = vec2((ang - 1.75) * 0.034, P.y - 0.074);
c2 = vec2((ang - 2.45) * 0.034, P.y - 0.050);
c3 = vec2((ang - 1.2) * 0.034, P.y - 0.104);
d1 = length(vec2(c1.x, c1.y * 0.7));
d2 = length(vec2(c2.x * 0.8, c2.y));
d3 = length(vec2(c3.x, c3.y * 0.75));
w = fbm(P * 90.0, 3.0, 2.0, 0.5) * 0.0012;
p1 = (1.0 - smoothstep(0.006, 0.0085, d1)) * (0.5 + 0.5 * sin((d1 + w) * 5200.0));
p2 = (1.0 - smoothstep(0.006, 0.008, d2)) * (0.5 + 0.5 * sin((d2 + w) * 5600.0));
p3 = (1.0 - smoothstep(0.005, 0.0075, d3)) * (0.5 + 0.5 * sin((d3 + w) * 5000.0));
smudge = smoothstep(0.1, 0.6, fbm(vec3(P.x * 30.0, P.y * 55.0, P.z * 30.0), 4.0, 2.0, 0.5)) * smoothstep(0.02, 0.05, P.y);
ly = (P.y - 0.0845) / 0.0007;
line = exp(-ly * ly);
scale = smoothstep(0.084, 0.086, P.y) * (1.0 - smoothstep(0.088, 0.094, P.y)) * (fbm(P * 300.0, 3.0, 2.0, 0.5) * 0.5 + 0.5);
oB = vec3(1.0) * (1.0 - 0.06 * line);
oR = clamp((p1 + p2 + p3) * 0.30 + smudge * 0.10 + line * 0.30 + scale * 0.12, 0.0, 0.6);
oM = 0.0;
oH = 0.0;
"""


def spoon(flaws):
    ca, sa = math.cos(math.radians(14.0)), math.sin(math.radians(14.0))
    c = ("d = vec2(P.x - 0.02, P.z - 0.15);\nsl = d.x * %r - d.y * %r;\nsx = d.x * %r + d.y * %r;\n" % (ca, sa, sa, ca)
         + "pb = vec3(sl, P.y, sx);\n"
         + re.sub(r"\bP\b", "pb", recipe("Brushed Metal", [("= 0.32 [", "= 0.26 ["), ("= 6.0 [", "= 9.0 [")]))
         + "hb = smoothstep(0.020, 0.030, sl);\n"
           "oB = mix(vec3(0.93, 0.93, 0.92), b0, hb);\noR = mix(0.045, r0, hb);\noM = 1.0;\noH = h0 * hb;\n")
    if not flaws:
        return c
    return c + """a1 = smoothstep(0.55, 0.62, noise3(vec3(pb.x * 2500.0, 1.3, pb.z * 60.0)));
a2 = smoothstep(0.58, 0.64, noise3(vec3((pb.x + pb.z) * 45.0, 4.1, (pb.x - pb.z) * 2800.0)));
a3 = smoothstep(0.60, 0.66, noise3(vec3((pb.x * 0.5 - pb.z) * 3000.0, 9.3, (pb.x + pb.z * 0.5) * 50.0)));
fine = clamp(a1 + a2 + a3, 0.0, 1.0) * (1.0 - hb * 0.6);
tn = fbm(P * 55.0, 4.0, 2.0, 0.5) * 0.5 + 0.5;
neck = (1.0 - smoothstep(0.004, 0.012, abs(sl - 0.027))) * 0.5;
tar = clamp((cavity * 5.0 + neck) * (0.3 + tn), 0.0, 1.0);
oB = mix(oB * (1.0 - 0.10 * fine), vec3(0.34, 0.25, 0.14), tar * 0.8);
oR = oR + fine * 0.16 + tar * 0.35;
oH = oH - fine * 0.00003;
"""


def cloth(flaws):
    c = recipe("Knit Fabric", [("= 40.0 [", "= 105.0 ["), ("vec3(0.55, 0.12, 0.10)", "vec3(0.47, 0.34, 0.20)"),
                               ("* 0.0015", "* 0.0006")])
    if not flaws:
        return c + "oB = b0;\noR = r0;\noM = 0.0;\noH = h0;\n"
    return c + """var = fbm(P * 16.0, 4.0, 2.0, 0.5);
fz = smoothstep(1.5, 9.0, curvature);
lint = smoothstep(0.80, 0.86, noise3(P * 1400.0)) * 0.5;
stain = smoothstep(0.012, 0.004, length(vec2(P.x + 0.19, P.z - 0.145))) * 0.35;
b1 = b0 * (1.0 + 0.16 * var) * mix(vec3(1.0), vec3(1.06, 0.98, 0.9), var * 0.5 + 0.5);
b1 = mix(b1, b1 * 1.35 + 0.03, clamp(fz * 0.5 + lint, 0.0, 1.0));
oB = mix(b1, b1 * vec3(0.55, 0.42, 0.30), stain);
oR = 0.92 + fz * 0.08;
oM = 0.0;
oH = h0 + fz * noise3(P * 3000.0) * 0.0002;
"""
