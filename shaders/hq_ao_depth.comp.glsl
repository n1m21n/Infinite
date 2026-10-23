#version 430
// Turbo 0.71 (S5): Render 3D HQ - the depth chain GTAO samples. Mode 0 makes
// level 0 at the AO size (half the frame): the view-space z of one full-size
// depth sample per 2x2 block; mode 1 makes the next level from the one
// before. After Filament filament/src/materials/ssao/mipmapDepth.mat
// @ 13be3649 (Apache License 2.0, Copyright Google LLC). Changes: compute
// instead of a fragment pass; level 0 linearises the hardware depth itself
// (Filament draws a structure pass at the AO size instead); and every level
// keeps the same corner of each 2x2 block, not Filament's rotated grid, so
// hq_gtao rebuilds each sample's exact pixel position - with the rotated grid
// a floor seen at a grazing angle came out in alternating rows (stripes).
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uDepth;            // mode 0: hardware depth, frame size
layout(r32f, binding = 0) uniform readonly image2D uSrc;  // mode 1: the level before
layout(r32f, binding = 1) uniform writeonly image2D uDst;

uniform int uMode;
uniform ivec2 uSrcSize;
uniform ivec2 uDstSize;
uniform vec3 uProjParams; // proj m[10], m[14], ortho flag
uniform int uLevel0Shift; // S8: frame -> AO size, log2 (1 half resolution, 0 full)

// View-space z (negative in front of the camera) of a hardware depth.
float viewZ(float windowZ)
{
   float ndc = windowZ * 2.0 - 1.0;
   if (uProjParams.z > 0.5)
      return (ndc - uProjParams.y) / uProjParams.x;
   return -uProjParams.y / (ndc + uProjParams.x);
}

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uDstSize)))
      return;
   ivec2 s = min(uMode == 0 ? (p << uLevel0Shift) : 2 * p, uSrcSize - 1);
   float z = uMode == 0 ? viewZ(texelFetch(uDepth, s, 0).r) : imageLoad(uSrc, s).r;
   imageStore(uDst, p, vec4(z));
}
