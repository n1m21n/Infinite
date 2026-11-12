// Material-domain Field Pixel recipes (rows 3.5 and 4.5). Included by FieldPixelNode::MaterialPresets() with
// FLD_RECIPE(name, code) defined; tools/blender-ab/harness.py reads this file to put a recipe in a case.
// Each recipe stays under 40 lines. Inputs: P N UV W V curvature cavity backfacing objrand.
// Outputs: base roughness metallic normal height emission opacity mask (mask under 0.5 cuts the surface away).
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
