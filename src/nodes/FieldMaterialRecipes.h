// Material-domain Field Pixel recipes (rows 3.5 and 4.5). Included by FieldPixelNode::MaterialPresets() with
// FLD_RECIPE(name, code) defined; tools/blender-ab/harness.py reads this file to put a recipe in a case.
// Each recipe stays under 40 lines. Inputs: P N UV W V curvature cavity backfacing objrand.
// Outputs: base roughness metallic normal height emission opacity mask (height is a bump in metres; mask under 0.5
// cuts the surface away).
// No include guard: the file is meant to be expanded once per FLD_RECIPE definition.

// ---- row 4.5: wear ----

FLD_RECIPE("Edge Wear", R"FLD(# Edge wear: paint chips off where the surface bends outward (curvature from the
# per-vertex bake), broken by fbm so the edge is ragged, revealing bare metal under the paint.
param float threshold = 5.0 [0.0, 20.0];
param float width = 3.0 [0.1, 10.0];
param float breakup = 0.6 [0.0, 1.0];
param float scale = 8.0 [1.0, 40.0];
n = fbm(P * scale, 4.0, 2.0, 0.5);
e = (curvature - threshold) / width + n * breakup * 2.0;
wear = smoothstep(0.35, 0.65, e);
paint = vec3(0.50, 0.07, 0.05);
metal = vec3(0.80, 0.78, 0.74);
base = mix(paint, metal, wear);
metallic = wear;
roughness = mix(0.55, 0.25, wear);)FLD")

FLD_RECIPE("Dirt", R"FLD(# Dirt: grime settles where the surface is occluded (cavity from the bake), patchy by fbm;
# it darkens and roughens.
param float amount = 1.0 [0.0, 1.0];
param float spread = 6.0 [0.5, 16.0];
param float scale = 6.0 [1.0, 40.0];
n = fbm(P * scale, 5.0, 2.0, 0.5) * 0.5 + 0.5;
d = clamp(cavity * spread * (0.3 + n), 0.0, 1.0) * amount;
clean = vec3(0.70, 0.69, 0.66);
grime = vec3(0.10, 0.08, 0.05);
base = mix(clean, grime, d);
roughness = mix(0.4, 0.9, d);)FLD")

FLD_RECIPE("Dust", R"FLD(# Dust: a pale matte film on up-facing surfaces, thinned by fbm; it greys and flattens.
param float amount = 0.85 [0.0, 1.0];
param float sharpness = 3.0 [0.5, 12.0];
param float scale = 20.0 [1.0, 80.0];
up = clamp(N.y, 0.0, 1.0);
n = fbm(P * scale, 4.0, 2.0, 0.5) * 0.5 + 0.5;
d = clamp(pow(up, sharpness) * (0.5 + n), 0.0, 1.0) * amount;
surface = vec3(0.04, 0.10, 0.30);
dust = vec3(0.60, 0.57, 0.52);
base = mix(surface, dust, d);
roughness = mix(0.3, 1.0, d);)FLD")

// ---- row 3.5: surfaces. Each REALISM line is the measured range the defaults sit in. P is object space, metres. ----

FLD_RECIPE("Leather", R"FLD(# Leather: pebble grain = voronoi crackle (creases between cells) + fbm (hide variation).
# REALISM: dyed leather albedo 0.05-0.25, roughness 0.45-0.65 (cells waxier than creases), grain 1-3 mm.
param float scale = 60.0 [5.0, 300.0];
param float depth = 0.6 [0.0, 1.0];
param float tone = 0.5 [0.0, 1.0];
v = voronoi3(P * scale);
cell = smoothstep(0.0, 0.15, v.y - v.x);
f = fbm(P * scale * 0.1, 4.0, 2.0, 0.5);
height = (cell * 0.7 + f * 0.3) * depth * 0.0006;
hide = vec3(0.22, 0.10, 0.045) * (0.6 + 0.8 * tone) * (0.9 + 0.3 * f + 0.1 * v.z);
base = hide * mix(0.55, 1.0, cell);
roughness = mix(0.68, 0.45, cell);)FLD")

FLD_RECIPE("Suede", R"FLD(# Suede: a napped surface, no gloss; fine fibre noise and soft brushed patches lighter where the nap lies flat.
# REALISM: albedo 0.1-0.4 (nap lifts it), roughness 0.9-1.0, no specular sheen highlight below 0.04.
param float scale = 400.0 [50.0, 2000.0];
param float patches = 4.0 [0.5, 20.0];
param float tone = 0.5 [0.0, 1.0];
fibre = noise3(P * scale);
nap = fbm(P * patches, 3.0, 2.0, 0.5) * 0.5 + 0.5;
c = vec3(0.30, 0.20, 0.13) * (0.6 + 0.8 * tone);
base = c * (0.8 + 0.35 * nap + 0.1 * fibre);
roughness = 0.95 - 0.08 * nap;
height = fibre * 0.00005;)FLD")

FLD_RECIPE("Rubber", R"FLD(# Rubber: carbon-black, satin, a fine stippled moulding texture and faint bloom (grey film) in patches.
# REALISM: black rubber albedo 0.02-0.05, roughness 0.55-0.8, stipple 0.2-0.5 mm.
param float scale = 300.0 [50.0, 2000.0];
param float bloom = 0.3 [0.0, 1.0];
s = voronoi3(P * scale);
stip = 1.0 - smoothstep(0.15, 0.35, s.x);
b = clamp(fbm(P * 3.0, 3.0, 2.0, 0.5) + 0.5, 0.0, 1.0) * bloom;
base = mix(vec3(0.025, 0.025, 0.028), vec3(0.09, 0.09, 0.09), b);
roughness = mix(0.6, 0.8, b) + stip * 0.08;
height = stip * 0.0002;)FLD")

FLD_RECIPE("Knit Fabric", R"FLD(# Knit fabric: stockinette, columns of V loops along UV; yarn fuzz from fine noise, each loop a little different.
# REALISM: wool / cotton yarn albedo 0.2-0.6, roughness 0.85-1.0, about 5 loops per cm on a fine knit.
param float scale = 40.0 [4.0, 200.0];
param float depth = 0.8 [0.0, 1.0];
param float tone = 0.5 [0.0, 1.0];
q = vec2(UV.x * scale, UV.y * scale * 1.4);
cx = fract(q.x) - 0.5;
row = fract(q.y + abs(cx) * 0.9);
loop = sin(row * 3.14159) * (1.0 - abs(cx) * 1.6);
yarn = clamp(loop, 0.0, 1.0);
fuzz = noise3(vec3(q * 9.0, 0.0));
id = white(vec3(floor(q.x), floor(q.y + abs(cx) * 0.9), 0.0));
height = (yarn + fuzz * 0.15) * depth * 0.0015;
c = vec3(0.55, 0.12, 0.10) * (0.6 + 0.8 * tone) * (0.92 + 0.16 * id);
base = c * mix(0.45, 1.0, yarn);
roughness = 0.92;)FLD")

FLD_RECIPE("Carpet", R"FLD(# Carpet: cut pile, tufts as tiny cells with tips lighter than the gaps, shading patches where the pile leans.
# REALISM: nylon / wool pile albedo 0.1-0.5, roughness 1.0, tufts 2-4 mm.
param float scale = 300.0 [50.0, 2000.0];
param float lean = 0.5 [0.0, 1.0];
param float tone = 0.5 [0.0, 1.0];
v = voronoi3(P * scale);
tuft = 1.0 - smoothstep(0.1, 0.55, v.x);
shade = fbm(P * 2.0, 3.0, 2.0, 0.5) * lean;
c = vec3(0.32, 0.26, 0.18) * (0.6 + 0.8 * tone) * (0.9 + 0.2 * v.z);
base = c * (0.55 + 0.45 * tuft) * (1.0 + shade);
roughness = 1.0;
height = tuft * 0.002;)FLD")

FLD_RECIPE("Brushed Metal", R"FLD(# Brushed metal: fine parallel scratches along one axis (noise stretched 300:1), a few deeper strokes.
# REALISM: steel / aluminium F0 0.56-0.91 (here as base), roughness 0.25-0.4 across the brushing.
param float scale = 6.0 [1.0, 50.0];
param float stretch = 300.0 [20.0, 2000.0];
param float rough = 0.32 [0.05, 0.8];
s = vec3(P.x * scale, P.y * scale * stretch, P.z * scale * stretch);
fine = noise3(s) * 0.5 + 0.5;
deep = smoothstep(0.75, 0.95, noise3(s * 0.2) * 0.5 + 0.5);
metallic = 1.0;
base = vec3(0.91, 0.92, 0.92) * (0.92 + 0.08 * fine) * (1.0 - 0.1 * deep);
roughness = rough + 0.16 * (fine - 0.5) + 0.12 * deep;
height = fine * 0.00004 + deep * 0.00008;)FLD")

FLD_RECIPE("Concrete", R"FLD(# Concrete: cast grey with large cement blotches, fine aggregate grit and air pits (voronoi holes).
# REALISM: albedo 0.25-0.4, roughness 0.85-0.95, pits 1-4 mm, blotches 10-50 cm.
param float scale = 1.0 [0.1, 10.0];
param float pits = 0.5 [0.0, 1.0];
param float tone = 0.5 [0.0, 1.0];
blot = fbm(P * scale * 2.0, 5.0, 2.0, 0.5);
grit = noise3(P * scale * 400.0);
v = voronoi3(P * scale * 80.0);
pit = (1.0 - smoothstep(0.08, 0.16, v.x)) * step(1.0 - pits * 0.6, v.z);
g = (0.28 + 0.12 * tone) * (1.0 + 0.25 * blot + 0.08 * grit) * (1.0 - 0.5 * pit);
base = vec3(g, g * 0.98, g * 0.95);
roughness = 0.9 - 0.05 * blot;
height = grit * 0.0002 - pit * 0.002;)FLD")

FLD_RECIPE("Painted Wood", R"FLD(# Painted wood: enamel over planed wood; the growth rings (warped by fbm) print through the paint as relief.
# REALISM: gloss enamel roughness 0.25-0.45 (duller in the grain lines), paint albedo per colour, rings 2-6 mm apart.
param float rings = 120.0 [10.0, 600.0];
param float warp = 0.4 [0.0, 2.0];
param float relief = 0.5 [0.0, 1.0];
w = fbm(vec3(P.x * 2.0, P.y * 30.0, P.z * 2.0), 4.0, 2.0, 0.5);
r = fract(length(vec2(P.x, P.z) + vec2(0.31, 0.17)) * rings + w * warp * 4.0);
grain = smoothstep(0.0, 0.2, r) * (1.0 - smoothstep(0.6, 1.0, r));
brush = noise3(vec3(P.x * 80.0, P.y * 4.0, P.z * 80.0));
base = vec3(0.05, 0.17, 0.11) * (0.93 + 0.07 * grain);
roughness = 0.32 + 0.12 * (1.0 - grain) + 0.03 * brush;
height = (grain * 0.5 + brush * 0.1) * relief * 0.0004;)FLD")
