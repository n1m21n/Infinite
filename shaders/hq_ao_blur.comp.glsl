#version 430
// Turbo 0.71 (S5): Render 3D HQ - separable bilateral blur of the GTAO
// output, one axis per dispatch. Weights after Filament
// filament/src/materials/ssao/bilateralBlur.mat @ 13be3649 (Apache License
// 2.0, Copyright Google LLC): a Gaussian (11 taps, sigma 4, Filament's MEDIUM
// low-pass) times max(0, 1 - (dz / bilateralThreshold)^2), so occlusion does
// not bleed across depth edges. Pixels where nothing was drawn are skipped.
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uSrc; // (visibility, view z)
layout(rg16f, binding = 0) uniform writeonly image2D uDst;

uniform ivec2 uSize;
uniform ivec2 uAxis;
uniform float uFarZ;
uniform float uEdgeInv; // 1 / bilateralThreshold (metres)

const int kRadius = 5;
const float kSigma = 4.0;

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   vec2 c = texelFetch(uSrc, p, 0).rg;
   if (c.y <= uFarZ * 0.9999)
   {
      imageStore(uDst, p, vec4(c, 0.0, 0.0));
      return;
   }
   float sum = c.x;
   float weights = 1.0;
   for (int i = 1; i <= kRadius; i++)
   {
      float g = exp(-float(i * i) / (2.0 * kSigma * kSigma));
      for (int side = -1; side <= 1; side += 2)
      {
         vec2 s = texelFetch(uSrc, clamp(p + uAxis * (i * side), ivec2(0), uSize - 1), 0).rg;
         if (s.y <= uFarZ * 0.9999)
            continue;
         float d = (s.y - c.y) * uEdgeInv;
         float w = g * max(0.0, 1.0 - d * d);
         sum += s.x * w;
         weights += w;
      }
   }
   imageStore(uDst, p, vec4(sum / weights, c.y, 0.0, 0.0));
}
