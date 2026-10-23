#version 450 core
// Turbo 0.71 (S8): Render 3D HQ - bloom downsampling. Ported from Filament
// filament/src/materials/bloom/bloomDownsample2x.mat (mode 0),
// bloomDownsample9.mat (mode 1) and bloomDownsample.mat (mode 2)
// @ 13be3649 (Apache License 2.0, Copyright Google LLC).
// Changes: GL 4.6 full-screen pass over its own target per level (lod 0 of
// a texture per level instead of mips of one); the threshold subtracts the
// node's `threshold` (Filament: a fixed 1.0, on or off).
in vec2 vNdc;

uniform sampler2D uSource;
uniform int uMode;            // 0 2x (+ threshold, fireflies), 1 9-tap, 2 13-tap (Jimenez)
uniform float uThreshold;     // 0 = none
uniform float uInvHighlight;  // 1 / highlight (BloomOptions.highlight 1000)
uniform int uFireflies;       // mode 0: Karis average (off under TAA, as in Filament)

layout(location = 0) out vec4 outColor;

float max3(vec3 v)
{
   return max(v.x, max(v.y, v.z));
}

void threshold(inout vec3 c)
{
   c = max(vec3(0.0), c - uThreshold);
   float f = max3(c);
   c *= 1.0 / (1.0 + f * uInvHighlight);
}

vec3 tap(vec2 uv)
{
   return textureLod(uSource, uv, 0.0).rgb;
}

vec3 box4x4(vec3 s0, vec3 s1, vec3 s2, vec3 s3)
{
   return (s0 + s1 + s2 + s3) * 0.25;
}

void main()
{
   vec2 uvOut = vNdc * 0.5 + 0.5;
   vec2 size = vec2(textureSize(uSource, 0));
   vec2 texelSize = 1.0 / size;
   vec3 c;
   if (uMode == 0)
   {
      // 4 bilinear taps = a 3x3 Gaussian (Castano 2013).
      vec2 offset = vec2(0.5);
      vec2 uv = (uvOut * size) + offset;
      vec2 base = (floor(uv) - offset) * texelSize;
      vec2 st = fract(uv);
      vec2 uw = vec2(3.0 - 2.0 * st.x, 1.0 + 2.0 * st.x);
      vec2 vw = vec2(3.0 - 2.0 * st.y, 1.0 + 2.0 * st.y);
      vec2 u = vec2((2.0 - st.x) / uw.x - 1.0, st.x / uw.y + 1.0) * texelSize.x;
      vec2 v = vec2((2.0 - st.y) / vw.x - 1.0, st.y / vw.y + 1.0) * texelSize.y;
      vec3 c0 = tap(base + vec2(u.x, v.x));
      vec3 c1 = tap(base + vec2(u.y, v.x));
      vec3 c2 = tap(base + vec2(u.x, v.y));
      vec3 c3 = tap(base + vec2(u.y, v.y));
      float w0 = uw.x * vw.x * (1.0 / 16.0);
      float w1 = uw.y * vw.x * (1.0 / 16.0);
      float w2 = uw.x * vw.y * (1.0 / 16.0);
      float w3 = uw.y * vw.y * (1.0 / 16.0);
      if (uFireflies != 0)
      {
         w0 /= (1.0 + max3(c0));
         w1 /= (1.0 + max3(c1));
         w2 /= (1.0 + max3(c2));
         w3 /= (1.0 + max3(c3));
         float w = 1.0 / (w0 + w1 + w2 + w3);
         w0 *= w;
         w1 *= w;
         w2 *= w;
         w3 *= w;
      }
      c = c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3;
      if (uThreshold > 0.0)
         threshold(c);
   }
   else if (uMode == 1)
   {
      // 9 bilinear taps of a 6x6 kernel (even source size).
      const float o = 1.5 + 0.261629;
      const float wa = 7.46602 / 32.0;
      const float wb = 1.0 - wa * 2.0;
      const float wab = wa * wb;
      const float waa = wa * wa;
      const float wbb = wb * wb;
      vec2 d = texelSize * o;
      c = tap(uvOut) * wbb;
      c += (tap(uvOut + vec2(-d.x, 0.0)) + tap(uvOut + vec2(d.x, 0.0)) + tap(uvOut + vec2(0.0, -d.y)) +
            tap(uvOut + vec2(0.0, d.y))) * wab;
      c += (tap(uvOut + vec2(-d.x, -d.y)) + tap(uvOut + vec2(d.x, -d.y)) + tap(uvOut + vec2(-d.x, d.y)) +
            tap(uvOut + d)) * waa;
   }
   else
   {
      // 13 taps, 5 overlapping boxes (odd source size).
      vec2 t = texelSize;
      vec3 cc = tap(uvOut);
      vec3 lt = tap(uvOut + vec2(-1.0, -1.0) * t), rt = tap(uvOut + vec2(1.0, -1.0) * t);
      vec3 rb = tap(uvOut + vec2(1.0, 1.0) * t), lb = tap(uvOut + vec2(-1.0, 1.0) * t);
      vec3 lt2 = tap(uvOut + vec2(-2.0, -2.0) * t), rt2 = tap(uvOut + vec2(2.0, -2.0) * t);
      vec3 rb2 = tap(uvOut + vec2(2.0, 2.0) * t), lb2 = tap(uvOut + vec2(-2.0, 2.0) * t);
      vec3 l = tap(uvOut + vec2(-2.0, 0.0) * t), r = tap(uvOut + vec2(2.0, 0.0) * t);
      vec3 tt = tap(uvOut + vec2(0.0, -2.0) * t), b = tap(uvOut + vec2(0.0, 2.0) * t);
      vec3 c0 = box4x4(lt, rt, rb, lb);
      vec3 c1 = box4x4(cc, l, tt, lt2);
      c1 += box4x4(cc, r, tt, rt2);
      c1 += box4x4(cc, r, b, rb2);
      c1 += box4x4(cc, l, b, lb2);
      c = c0 * 0.5 + c1 * 0.125;
   }
   outColor = vec4(c, 1.0);
}
