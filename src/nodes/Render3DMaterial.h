#pragma once

// Render 3D's material record on the Metal engine: one struct for every path (raster InstU, the rays' RInst for
// GI, reflections and settle), named fields instead of spare float4 lanes. The field list is written once below and
// expands to the MSL struct (R3D_MAT_MSL, spliced into the shader) and the C++ mirror (R3dMatH): every field is a
// float or three, so both sides pack them the same way (4-byte alignment, no padding; packed_float3 is 12 bytes).
// Adding a field: one line here, one line in the fill (MetalDevice.mm FillMat), keep the size a multiple of 16.
//
// Encoded so all zeros is the plain material (instance batches leave most of it empty):
//   map*         - texture index + 1, 0 = none
//   clear        - 1 - opacity
//   f0Extra      - dielectric F0 less 0.04 (from ior and specular)
//   hairRadialNeg- minus the radial roughness: < 0 marks a hair material, from primitive hairFrom on
//   wrapBump     - map wrap (0 clamp, 1 repeat, 2 mirror) + 4 when the normal map is a bump map
//   fimg0..3     - row 2.4 field images (img(slot, uv) in a material program): texture index + 1, 0 = none
//   p0..p15      - row 3.4 live material-program knobs (FldMat prm0..prm3)
//   sheenDir     - row 5.1: the sheen lobe's lean toward the uv tangent, -1..1 (0 isotropic); spare0..2 pad to 16 bytes
//   emTri        - rays: the instance's first emissive triangle for next-event estimation, -1 none
#define R3D_MAT_FIELDS(F3, F1) \
   F3(base) F1(metallic) F1(roughness) F1(transmission) F1(ior) \
   F3(emission) F1(emTri) \
   F1(mapAlbedo) F1(mapRoughness) F1(mapMetallic) F1(mapNormal) \
   F1(mapAo) F1(mapEmission) F1(mapSheen) F1(mapOpacity) F1(mapCoat) F1(mapTransmission) \
   F1(sheen) F1(sheenRoughness) F3(sheenColor) F1(normalStrength) F1(wrapBump) \
   F1(mapSpace) F1(triplanar) F1(alphaCutoff) F1(clear) \
   F3(mapScale) F3(mapRotate) F3(mapTranslate) \
   F1(coat) F1(coatRoughness) F1(f0Extra) \
   F3(boxLo) F3(boxHi) F1(breakup) F1(detailScale) F1(detailStrength) \
   F1(hairFrom) F1(hairRadialNeg) F1(hairShift) F3(hairSigma) \
   F3(sssRadius) F1(subsurface) F1(translucency) \
   F1(iridescence) F1(iridIor) F1(iridThickness) F1(program) \
   F1(fimg0) F1(fimg1) F1(fimg2) F1(fimg3) \
   F1(p0) F1(p1) F1(p2) F1(p3) F1(p4) F1(p5) F1(p6) F1(p7) \
   F1(p8) F1(p9) F1(p10) F1(p11) F1(p12) F1(p13) F1(p14) F1(p15) \
   F1(sheenDir) F1(spare0) F1(spare1) F1(spare2)

#define R3D_MAT_MSL_F3(n) "packed_float3 " #n "; "
#define R3D_MAT_MSL_F1(n) "float " #n "; "
#define R3D_MAT_MSL "struct MatRec { " R3D_MAT_FIELDS(R3D_MAT_MSL_F3, R3D_MAT_MSL_F1) "};\n"

#define R3D_MAT_C_F3(n) float n[3];
#define R3D_MAT_C_F1(n) float n;
struct R3dMatH { R3D_MAT_FIELDS(R3D_MAT_C_F3, R3D_MAT_C_F1) };
static_assert(sizeof(R3dMatH) == 368, "MatRec: 92 floats (keep a multiple of 16 bytes)");
