#pragma once

// Render 3D's shared microfacet terms, one source for the GL forward shader (Geometry3DNodes.cpp) and Metal
// (MetalDevice.mm). Written in syntax GLSL and MSL share.
//   ggxG2(nV, nL, rough) - Smith masking-shadowing for GGX, height-correlated (Heitz 2014), alpha = rough^2:
//                          the form both path tracers use. Replaces the Schlick-k (k = (r+1)^2/8) product in
//                          the raster and live direct light, measured against Cycles in tools/blender-ab.
//   ggxMs(f0, rough, nV)   - multiple-scattering gain for the single-scatter GGX lobe (Kulla and Conty 2017, in
//                            Turquin's 2019 form 1 + f0 * (1/E - 1)); E is the lobe's directional albedo from
//                            Karis' 2014 analytic env-BRDF fit. A rough metal loses up to ~40% of its energy
//                            without it and reads dark next to Cycles' multiscatter GGX.
#define R3D_BSDF_SRC \
   "float ggxG2(float nV, float nL, float rough) {\n" \
   "   float a = rough * rough, a2 = a * a;\n" \
   "   float gV = nL * sqrt(a2 + (1.0 - a2) * nV * nV), gL = nV * sqrt(a2 + (1.0 - a2) * nL * nL);\n" \
   "   return 2.0 * nV * nL / max(gV + gL, 1e-7);\n" \
   "}\n" \
   "vec3 ggxMs(vec3 f0, float rough, float nV) {\n" \
   "   float rx = 1.0 - rough, ry = 0.0425 - 0.0275 * rough, rz = 1.04 - 0.572 * rough, rw = 0.022 * rough - 0.04;\n" \
   "   float a4 = min(rx * rx, exp2(-9.28 * nV)) * rx + ry;\n" \
   "   float e = clamp((-1.04 * a4 + rz) + (1.04 * a4 + rw), 0.05, 1.0);\n" \
   "   return vec3(1.0) + f0 * (1.0 / e - 1.0);\n" \
   "}\n"
