#version 430
// Turbo 0.70: Render 3D FX - depth of field: each pixel's blur circle grows
// with its distance from the focus band (`uFocus` +- `uRange` along the view,
// world units) up to `uMaxBlur` pixels; gathered over a golden-angle disc,
// where a sample counts only when its own circle reaches this pixel (a sharp
// subject does not bleed into the blurred background behind it).
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uColor;
layout(binding = 1) uniform sampler2D uDepth;
layout(rgba16f, binding = 0) uniform writeonly image2D uOut;

uniform ivec2 uSize;
uniform mat4 uInvProj;
uniform float uFocus;
uniform float uRange;
uniform float uMaxBlur;

float Coc(vec2 uv)
{
   float d = texture(uDepth, uv).r;
   vec4 v = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
   float z = d >= 0.99999 ? 1e6 : -v.z / v.w;
   float off = max(abs(z - uFocus) - uRange, 0.0) / max(uFocus, 0.1);
   return clamp(off, 0.0, 1.0) * uMaxBlur;
}

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (p.x >= uSize.x || p.y >= uSize.y)
      return;
   vec2 texel = 1.0 / vec2(uSize);
   vec2 uv = (vec2(p) + 0.5) * texel;
   float coc = Coc(uv);
   vec4 centre = texture(uColor, uv);
   if (coc < 0.5)
   {
      imageStore(uOut, p, centre);
      return;
   }
   const int kSamples = 32;
   vec4 sum = centre;
   float wsum = 1.0;
   for (int i = 0; i < kSamples; i++)
   {
      float t = sqrt((float(i) + 0.5) / float(kSamples));
      float a = float(i) * 2.39996323;
      vec2 off = vec2(cos(a), sin(a)) * (t * coc);
      vec2 suv = uv + off * texel;
      float sc = Coc(suv);
      float reach = smoothstep(length(off) - 1.0, length(off) + 1.0, sc);
      sum += texture(uColor, suv) * reach;
      wsum += reach;
   }
   imageStore(uOut, p, sum / wsum);
}
