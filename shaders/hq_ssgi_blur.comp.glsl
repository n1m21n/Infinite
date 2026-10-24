#version 430
// Turbo 0.71.1: Render 3D HQ - separable bilateral blur of the SSGI
// (hq_ssgi.comp), one axis per dispatch: the GTAO's blur (hq_ao_blur.comp,
// after Filament's bilateralBlur.mat) on RGB, view z in alpha. Light does not
// bleed across depth edges; pixels where nothing was drawn are skipped.
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uSrc; // (rgb, view z)
layout(rgba16f, binding = 0) uniform writeonly image2D uDst;

uniform ivec2 uSize;
uniform ivec2 uAxis;
uniform float uFarZ;
uniform float uEdgeInv; // 1 / bilateral threshold (metres)

const int kRadius = 6;
const float kSigma = 4.0;

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   vec4 c = texelFetch(uSrc, p, 0);
   if (c.w <= uFarZ * 0.9999)
   {
      imageStore(uDst, p, c);
      return;
   }
   vec3 sum = c.rgb;
   float weights = 1.0;
   for (int i = 1; i <= kRadius; i++)
   {
      float g = exp(-float(i * i) / (2.0 * kSigma * kSigma));
      for (int side = -1; side <= 1; side += 2)
      {
         vec4 s = texelFetch(uSrc, clamp(p + uAxis * (i * side), ivec2(0), uSize - 1), 0);
         if (s.w <= uFarZ * 0.9999)
            continue;
         float d = (s.w - c.w) * uEdgeInv;
         float w = g * max(0.0, 1.0 - d * d);
         sum += s.rgb * w;
         weights += w;
      }
   }
   imageStore(uDst, p, vec4(sum / weights, c.w));
}
