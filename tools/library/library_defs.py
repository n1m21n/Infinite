"""The Infinite Library: Field Pixel devices shipped as downloadable .field files.

Each entry is one device. build_library.py turns this table into
website/assets/library/pixel/<slug>.field, previews/<slug>.png and index.json.

Field rules (field-pixel-presets skill): bare names, `;` ends a statement, aspect-correct p, no `//`, no
GLSL keywords as local names. Every program here ends each statement with `;` because the patch format
(used for previews) is one line per code string.
"""

NAVY = "0.082, 0.098, 0.188"
CREAM = "0.953, 0.929, 0.875"

DITHER = """
input pixel image img;
param float cell = 3.0 [1.0, 16.0];
param float contrast = 1.2 [0.2, 4.0];
param float bias = 0.0 [-0.5, 0.5];
param float inkR = 0.953 [0.0, 1.0];
param float inkG = 0.929 [0.0, 1.0];
param float inkB = 0.875 [0.0, 1.0];
param float paperR = 0.082 [0.0, 1.0];
param float paperG = 0.098 [0.0, 1.0];
param float paperB = 0.188 [0.0, 1.0];
g = floor(uv * res / cell);
c = img((g + 0.5) * cell / res);
gray = clamp(dot(c.rgb, vec3(0.299, 0.587, 0.114)) * contrast + bias, 0.0, 1.0);
x0 = fmod(g.x, 2.0);
x1 = floor(fmod(g.x, 4.0) / 2.0);
y0 = fmod(g.y, 2.0);
y1 = floor(fmod(g.y, 4.0) / 2.0);
thr = (8.0 * abs(y0 - x0) + 4.0 * y0 + 2.0 * abs(y1 - x1) + y1) / 16.0;
on = step(thr, gray);
col = mix(vec3(paperR, paperG, paperB), vec3(inkR, inkG, inkB), on);
alpha = c.a;
"""

GLASS = """
input pixel image img;
param float cx = 0.5 [0.0, 1.0];
param float cy = 0.5 [0.0, 1.0];
param float halfW = 0.42 [0.05, 1.2];
param float halfH = 0.2 [0.05, 0.6];
param float radius = 0.12 [0.0, 0.6];
param float bezel = 0.08 [0.01, 0.4];
param float bend = 0.07 [0.0, 0.3];
param float frost = 0.85 [0.0, 1.0];
param float blurSize = 0.02 [0.0, 0.08];
param float chroma = 0.1 [0.0, 0.4];
param float tint = 0.3 [0.0, 1.0];
param float tintR = 0.09 [0.0, 1.0];
param float tintG = 0.105 [0.0, 1.0];
param float tintB = 0.2 [0.0, 1.0];
param float shine = 0.035 [0.0, 0.3];
param float rimWidth = 0.004 [0.001, 0.02];
param float shadow = 0.4 [0.0, 1.0];
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
ctr = vec2((cx - 0.5) * aspect, cy - 0.5);
rr = min(radius, min(halfW, halfH));
pc = p - ctr;
q = abs(pc) - (vec2(halfW, halfH) - vec2(rr));
mq = max(q, vec2(0.0));
lq = length(mq);
d = lq + min(max(q.x, q.y), 0.0) - rr;
axis = vec2(step(q.y, q.x), 1.0 - step(q.y, q.x));
nrm = sign(pc) * mix(axis, mq / (lq + 0.00001), step(0.00001, lq));
di = max(-d, 0.0);
k = 1.0 - clamp(di / bezel, 0.0, 1.0);
k = k * k;
off = nrm * bend * k;
offuv = vec2(off.x / aspect, off.y);
bc = uv + offuv;
sr = img(uv + offuv * (1.0 + chroma)).r;
sg = img(bc).g;
sb = img(uv + offuv * (1.0 - chroma)).b;
sharp = vec3(sr, sg, sb);
bx = vec2(blurSize / aspect, 0.0);
by = vec2(0.0, blurSize);
bd = vec2(blurSize / aspect, blurSize) * 0.7;
be = vec2(blurSize / aspect, -blurSize) * 0.7;
soft = img(bc).rgb;
soft = soft + img(bc + bx).rgb + img(bc - bx).rgb + img(bc + by).rgb + img(bc - by).rgb;
soft = soft + img(bc + bd).rgb + img(bc - bd).rgb + img(bc + be).rgb + img(bc - be).rgb;
soft = soft / 9.0;
rgb = mix(sharp, soft, frost);
rgb = mix(rgb, vec3(tintR, tintG, tintB), tint) + vec3(shine);
ldir = normalize(vec2(-0.55, 0.85));
lit = clamp(dot(nrm, ldir), 0.0, 1.0);
dark = clamp(dot(-nrm, ldir), 0.0, 1.0);
rim = 1.0 - smoothstep(0.0, rimWidth, di);
glow = 1.0 - smoothstep(0.0, bezel * 0.9, di);
rgb = rgb + vec3(1.0, 0.97, 0.95) * (rim * (0.18 + 0.72 * lit * lit) + glow * 0.07 * lit);
rgb = rgb + vec3(0.75, 0.82, 1.0) * rim * 0.22 * dark * dark;
aa = 1.2 / res.y;
cover = 1.0 - smoothstep(-aa, aa, d);
back = img(uv);
under = 1.0 - shadow * (1.0 - smoothstep(0.0, 0.09, max(d, 0.0))) * step(0.0, d);
col = mix(back.rgb * under, rgb, cover);
alpha = back.a;
"""

LENS = """
input pixel image img;
param float cx = 0.5 [0.0, 1.0];
param float cy = 0.5 [0.0, 1.0];
param float size = 0.22 [0.05, 0.5];
param float spread = 0.12 [0.0, 0.4];
param float speed = 1.0 [0.0, 4.0];
param float roam = 0.12 [0.0, 0.4];
param float bend = 0.06 [0.0, 0.3];
param float rimGlow = 0.8 [0.0, 2.0];
param float tint = 0.25 [0.0, 1.0];
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
c0 = vec2((cx - 0.5) * aspect, cy - 0.5) + roam * vec2(1.4 * sin(t * speed * 0.5), 0.7 * cos(t * speed * 0.37));
ph = t * speed * 1.8;
a0 = ph;
a1 = ph + 2.094395;
a2 = ph + 4.18879;
d0 = spread * (1.0 + 0.27 * sin(2.2 * t * speed));
d1 = spread * (1.0 + 0.27 * sin(2.2 * t * speed + 1.0));
d2 = spread * (1.0 + 0.27 * sin(2.2 * t * speed + 2.0));
l0 = c0 + vec2(cos(a0), 0.8 * sin(a0)) * d0;
l1 = c0 + vec2(cos(a1), 0.8 * sin(a1)) * d1;
l2 = c0 + vec2(cos(a2), 0.8 * sin(a2)) * d2;
r0 = size * (0.6 + 0.1 * sin(3.1 * t * speed));
r1 = size * (0.6 + 0.1 * sin(3.1 * t * speed + 2.0));
r2 = size * (0.6 + 0.1 * sin(3.1 * t * speed + 4.0));
va = p - l0;
vb = p - l1;
vc = p - l2;
da = dot(va, va) + 0.00002;
db = dot(vb, vb) + 0.00002;
dc = dot(vc, vc) + 0.00002;
f = r0 * r0 / da + r1 * r1 / db + r2 * r2 / dc;
g = -2.0 * (r0 * r0 * va / (da * da) + r1 * r1 * vb / (db * db) + r2 * r2 * vc / (dc * dc));
glen = length(g) + 0.0001;
sd = (f - 1.0) / glen;
n = g / glen;
depth = (1.0 - smoothstep(0.0, 0.1, sd)) * (1.0 - smoothstep(1.15, 1.9, f));
sh = n * bend * depth;
inside = img(uv + vec2(sh.x / aspect, sh.y)).rgb;
rgb = mix(inside, vec3(0.12, 0.145, 0.27), tint);
rim = (1.0 - smoothstep(0.0, 0.0075, sd)) * (1.0 - smoothstep(1.08, 1.5, f));
lit = clamp(dot(-n, normalize(vec2(-0.6, 0.8))) * 0.5 + 0.5, 0.0, 1.0);
rgb = rgb + vec3(1.0, 0.8, 0.72) * rim * (0.25 + 0.75 * lit) * rimGlow;
aa = 1.5 / res.y;
cover = smoothstep(-aa, aa, sd) * step(0.5, f);
back = img(uv);
col = mix(back.rgb, rgb, cover);
alpha = back.a;
"""

PLATE = """
param float cells = 36.0 [8.0, 120.0];
param float grow = 1.0 [0.0, 1.0];
param float seed = 2.0 [0.0, 50.0];
param float gap = 0.06 [0.0, 0.4];
param float margin = 4.0 [0.0, 20.0];
param float inkR = 0.953 [0.0, 1.0];
param float inkG = 0.929 [0.0, 1.0];
param float inkB = 0.875 [0.0, 1.0];
param float paperR = 0.082 [0.0, 1.0];
param float paperG = 0.098 [0.0, 1.0];
param float paperB = 0.188 [0.0, 1.0];
cs = vec2(cells * aspect, cells);
g = floor(uv * cs);
cols = floor(cs.x);
rows = floor(cs.y);
hl = floor(3.0 * fract(sin(g.y * 12.9898 + seed * 78.233) * 43758.5453));
hr = floor(3.0 * fract(sin(g.y * 39.346 + seed * 11.135 + 5.0) * 43758.5453));
ht = floor(3.0 * fract(sin(g.x * 27.619 + seed * 53.211 + 9.0) * 43758.5453));
hb = floor(3.0 * fract(sin(g.x * 63.725 + seed * 31.337 + 3.0) * 43758.5453));
okx = step(hl + margin, g.x) * step(g.x + 1.0, cols - hr - margin);
oky = step(ht + margin, g.y) * step(g.y + 1.0, rows - hb - margin);
dist = length(vec2((g.x - cols * 0.5) / cols, (g.y - rows * 0.5) / rows));
inside = 1.0 - step(grow * 0.75, dist);
fc = fract(uv * cs);
tile = step(gap, fc.x) * step(gap, fc.y);
on = okx * oky * inside * tile;
col = mix(vec3(paperR, paperG, paperB), vec3(inkR, inkG, inkB), on);
alpha = 1.0;
"""

ORB = """
param float size = 0.3 [0.05, 0.6];
param float spin = 40.0 [-360.0, 360.0];
param float wobble = 0.04 [0.0, 0.15];
param float sheen = 0.75 [0.0, 1.0];
param float gloss = 0.9 [0.0, 1.0];
param float paperR = 0.082 [0.0, 1.0];
param float paperG = 0.098 [0.0, 1.0];
param float paperB = 0.188 [0.0, 1.0];
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
r = length(p);
ang = atan2(p.y, p.x);
rad = size * (1.0 + wobble * sin(3.0 * ang + t * 1.1) + wobble * 0.5 * sin(5.0 * ang - t * 1.3));
aa = 1.5 / res.y;
edge = 1.0 - smoothstep(rad - aa, rad, r);
u = fract(ang / 6.283185 + spin * t / 360.0);
s = u * 5.0;
w0 = max(0.0, 1.0 - abs(s)) + max(0.0, 1.0 - abs(s - 5.0));
w1 = max(0.0, 1.0 - abs(s - 1.0));
w2 = max(0.0, 1.0 - abs(s - 2.0));
w3 = max(0.0, 1.0 - abs(s - 3.0));
w4 = max(0.0, 1.0 - abs(s - 4.0));
body = vec3(1.0, 0.702, 0.851) * w0 + vec3(0.78, 0.722, 1.0) * w1 + vec3(0.659, 0.894, 1.0) * w2;
body = body + vec3(0.71, 0.961, 0.863) * w3 + vec3(1.0, 0.89, 0.69) * w4;
sp = vec2(-0.3, 0.35) * size;
sh = clamp(1.0 - length(p - sp) / (size * 1.2), 0.0, 1.0) * 0.75 * sheen;
body = mix(body, vec3(1.0), sh);
rim = smoothstep(0.7, 1.0, r / (size * 1.05)) * 0.22;
body = mix(body, vec3(0.416, 0.361, 0.91), rim);
gp = vec2(-0.34, 0.4) * size;
spot = (1.0 - smoothstep(0.0, size * 0.14, length(p - gp))) * gloss;
body = mix(body, vec3(1.0), spot);
col = mix(vec3(paperR, paperG, paperB), body, edge);
alpha = 1.0;
"""


def _bloom():
    head = """
param float seeds = 420.0 [60.0, 1500.0];
param float dotSize = 0.8 [0.2, 1.0];
param float turn = 8.0 [-90.0, 90.0];
param float hueSpan = 0.6 [0.0, 1.5];
param float paperR = 0.082 [0.0, 1.0];
param float paperG = 0.098 [0.0, 1.0];
param float paperB = 0.188 [0.0, 1.0];
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
sc = 0.46 / sqrt(seeds);
rot = turn * 0.0174533 * t;
r = length(p);
phi = fract((atan2(p.y, p.x) - rot) / 6.283185);
s = r * r / (sc * sc);
fa = 34.0 * phi + s * 0.01315562;
fb = 0.021286236 * s - 21.0 * phi;
ra = floor(fa + 0.5);
rb = floor(fb + 0.5);
"""
    body = ""
    for i, (da, db) in enumerate([(a, b) for a in (-1, 0, 1) for b in (-1, 0, 1)]):
        body += (
            f"kk{i} = 21.0 * (ra + ({da}.0)) + 34.0 * (rb + ({db}.0));\n"
            f"ok{i} = step(0.0, kk{i}) * step(kk{i}, seeds - 1.0);\n"
            f"sa{i} = kk{i} * 2.399963 + rot;\n"
            f"sp{i} = sc * sqrt(max(kk{i}, 0.0)) * vec2(cos(sa{i}), sin(sa{i}));\n"
            f"dk{i} = length(p - sp{i}) + (1.0 - ok{i}) * 10.0;\n"
        )
        if i == 0:
            body += "dm0 = dk0;\nkb0 = kk0;\n"
        else:
            body += (
                f"dm{i} = min(dm{i - 1}, dk{i});\n"
                f"kb{i} = mix(kb{i - 1}, kk{i}, step(dk{i}, dm{i - 1}));\n"
            )
    tail = """
rad = dotSize * sc * 0.95;
aa = 1.5 / res.y;
cover = 1.0 - smoothstep(rad - aa, rad, dm8);
pc = 0.5 + 0.5 * cos(6.283185 * (vec3(0.0, 0.33, 0.67) + hueSpan * kb8 / seeds + t * 0.04));
shade = 1.0 - 0.35 * smoothstep(0.0, rad, dm8);
col = mix(vec3(paperR, paperG, paperB), pc * shade, cover);
alpha = 1.0;
"""
    return head + body + tail


BLOOM = _bloom()

# id, name, kind, tags, description, code, preview frame time
ENTRIES = [
    dict(
        id="ordered-dither", name="Ordered Dither", kind="Filter",
        tags=["Retro", "Print", "Colour"],
        blurb="A 4x4 Bayer dither: any picture becomes two inks on paper. The series look from the films.",
        code=DITHER, t=0.0,
    ),
    dict(
        id="liquid-glass", name="Liquid Glass", kind="Filter",
        tags=["Glass", "Lens", "UI"],
        blurb="A frosted, refracting glass pane over whatever is behind it: bent edges, a colour split and a lit rim.",
        code=GLASS, t=0.0,
    ),
    dict(
        id="metaball-lens", name="Metaball Lens", kind="Filter",
        tags=["Glass", "Lens", "Motion"],
        blurb="Three liquid lobes drift over the picture, merging into one blob that refracts and magnifies what is under it.",
        code=LENS, t=1.4,
    ),
    dict(
        id="pixel-plate", name="Pixel Plate", kind="Generator",
        tags=["Retro", "Pattern", "Print"],
        blurb="A plate of square cells with ragged, notched edges that grows outward cell by cell.",
        code=PLATE, t=0.0,
    ),
    dict(
        id="iridescent-orb", name="Iridescent Orb", kind="Generator",
        tags=["Glass", "Colour", "Motion"],
        blurb="A soft holographic pastel ball that slowly spins its colours, with a white sheen and a gloss spot.",
        code=ORB, t=1.0,
    ),
    dict(
        id="phyllotaxis-bloom", name="Phyllotaxis Bloom", kind="Generator",
        tags=["Nature", "Pattern", "Motion"],
        blurb="A sunflower of golden-angle seeds, each dot coloured along the spiral. Seed count and turn are live.",
        code=BLOOM, t=1.0,
    ),
]
