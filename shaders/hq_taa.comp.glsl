#version 430
// Turbo 0.71 (S6): Render 3D HQ - temporal anti-aliasing. Ported from Filament
// filament/src/materials/antiAliasing/taa/taa.mat @ 13be3649 (Apache License
// 2.0, Copyright Google LLC) with its defaults: history read with a 5-tap
// Catmull-Rom filter, this frame reconstructed at the pixel centre with a
// Lanczos-2 kernel over its 3x3 jittered samples (de-ringed), the history
// clipped towards the reconstruction by a ray against the 5/9-tap AABB
// (ACCURATE clipping), all in the bounded "tonemapped" space c / (1 + max(c)),
// feedback 0.12.
// Changes: compute instead of a fragment pass; the history is fetched where
// the motion vectors say the pixel was (Render3DTemporal::MotionVectors: the
// camera from depth, plus the prepass's per-object velocity of meshes and
// particles - Filament reprojects the camera only), taken at the front-most
// pixel of the 3x3 so silhouettes move with their object; and the geometry's
// coverage rides along with the colour for the plain-background finish
// (Render3DPost::FinishTaa).
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uColor;           // this frame, HDR, jittered
layout(binding = 1) uniform sampler2D uHistory;         // last result, HDR (linear filtering)
layout(binding = 2) uniform sampler2D uMotion;          // uv now - uv last frame
layout(binding = 3) uniform sampler2D uDepth;           // hardware depth 0..1
layout(binding = 4) uniform sampler2D uCoverageHistory; // last coverage (linear filtering)
layout(rgba16f, binding = 0) uniform writeonly image2D uOut;
layout(r16f, binding = 1) uniform writeonly image2D uCoverageOut;

uniform ivec2 uSize;
uniform vec2 uJitter;     // this frame's jitter in pixels (what the projection was moved by)
uniform float uAlpha;     // feedback: weight of this frame
uniform int uHistoryValid;

const float kMinBoxHalfWidth = 0.002;

float max3(vec3 v)
{
   return max(v.x, max(v.y, v.z));
}

vec3 tonemap(vec3 c)
{
   return c / (1.0 + max3(c));
}

vec3 untonemap(vec3 c)
{
   const float epsilon = 1.0 / 65504.0;
   vec3 t = clamp(c, 0.0, 1.0 - epsilon);
   return t / max(epsilon, 1.0 - max3(t));
}

vec3 lanczos2(vec3 v)
{
   vec3 x2 = v * v;
   vec3 wA = x2 - 4.0;
   return (wA * wA) * ((x2 - 1.0) * (-0.354377 * x2 + 1.0)) * (-1.0 / 16.0);
}

vec3 clipToBox(vec3 boxmin, vec3 boxmax, vec3 c, vec3 h)
{
   const float epsilon = 0.0001;
   if (any(lessThan(h, boxmin)) || any(greaterThan(h, boxmax)))
   {
      vec3 cClamped = clamp(c, boxmin, boxmax);
      vec3 r = cClamped - h;
      vec3 rSign = step(0.0, r) * 2.0 - 1.0;
      vec3 ir = 1.0 / (r + rSign * epsilon);
      vec3 rmax = (boxmax - h) * ir;
      vec3 rmin = (boxmin - h) * ir;
      vec3 imin = min(rmax, rmin);
      return h + r * clamp(max3(imin), 0.0, 1.0);
   }
   return h;
}

// 5 bilinear taps, de-ringed (Filament's sampleTextureCatmullRom).
vec4 sampleTextureCatmullRom(sampler2D tex, vec2 uv, vec2 texSize)
{
   vec2 samplePos = uv * texSize;
   vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
   vec2 f = samplePos - texPos1;
   vec2 f2 = f * f;
   vec2 f3 = f2 * f;
   vec2 w0 = f2 - 0.5 * (f3 + f);
   vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
   vec2 w3 = 0.5 * (f3 - f2);
   vec2 w2 = 1.0 - w0 - w1 - w3;
   vec2 w12 = w1 + w2;
   vec2 tc0 = (texPos1 - 1.0) / texSize;
   vec2 tc3 = (texPos1 + 2.0) / texSize;
   vec2 tc12 = (texPos1 + w2 / w12) / texSize;
   float k0 = w12.x * w0.y;
   float k1 = w0.x * w12.y;
   float k2 = w12.x * w12.y;
   float k3 = w3.x * w12.y;
   float k4 = w12.x * w3.y;
   vec4 s0 = textureLod(tex, vec2(tc12.x, tc0.y), 0.0);
   vec4 s1 = textureLod(tex, vec2(tc0.x, tc12.y), 0.0);
   vec4 s2 = textureLod(tex, tc12, 0.0);
   vec4 s3 = textureLod(tex, vec2(tc3.x, tc12.y), 0.0);
   vec4 s4 = textureLod(tex, vec2(tc12.x, tc3.y), 0.0);
   vec4 result = (s0 * k0 + s1 * k1 + s2 * k2 + s3 * k3 + s4 * k4) / (k0 + k1 + k2 + k3 + k4);
   vec4 mn = min(s0, min(s1, min(s2, min(s3, s4))));
   vec4 mx = max(s0, max(s1, max(s2, max(s3, s4))));
   return clamp(result, mn, mx);
}

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   vec2 size = vec2(uSize);
   vec2 uv = (vec2(p) + 0.5) / size;

   // The 3x3 neighbourhood, tonemapped (Filament's order: centre, cross, corners).
   const ivec2 kOffsets[9] = ivec2[9](ivec2(0, 0), ivec2(0, -1), ivec2(-1, 0), ivec2(1, 0), ivec2(0, 1),
                                      ivec2(-1, -1), ivec2(1, -1), ivec2(-1, 1), ivec2(1, 1));
   vec3 s[9];
   float closestDepth = 2.0;
   ivec2 closest = p;
   for (int i = 0; i < 9; i++)
   {
      ivec2 q = clamp(p + kOffsets[i], ivec2(0), uSize - 1);
      s[i] = tonemap(max(texelFetch(uColor, q, 0).rgb, vec3(0.0)));
      float d = texelFetch(uDepth, q, 0).r;
      if (d < closestDepth)
      {
         closestDepth = d;
         closest = q;
      }
   }
   vec4 rawColor = texelFetch(uColor, p, 0);

   // This frame at the pixel centre: each sample sits at its pixel centre
   // minus the jitter in the unjittered image.
   vec2 dcenter = -uJitter;
   vec3 vx = dcenter.x + vec3(-1.0, 0.0, 1.0);
   vec3 vy = dcenter.y + vec3(-1.0, 0.0, 1.0);
   vec3 wx3 = lanczos2(vx);
   vec3 wy3 = lanczos2(vy);
   float wx = wx3.x + wx3.y + wx3.z;
   float wy = wy3.x + wy3.y + wy3.z;
   vec3 filtered = (s[5] * wx3.x + s[1] * wx3.y + s[6] * wx3.z) * wy3.x
                 + (s[2] * wx3.x + s[0] * wx3.y + s[3] * wx3.z) * wy3.y
                 + (s[7] * wx3.x + s[4] * wx3.y + s[8] * wx3.z) * wy3.z;
   vec3 box9min = s[0], box9max = s[0];
   for (int i = 1; i < 9; i++)
   {
      box9min = min(box9min, s[i]);
      box9max = max(box9max, s[i]);
   }
   float sum = wx * wy;
   filtered = abs(sum) > 0.1 ? filtered / sum : s[0];
   filtered = clamp(filtered, box9min, box9max);

   float coverage = texelFetch(uDepth, p, 0).r < 1.0 ? 1.0 : 0.0;
   vec2 historyUv = uv - texelFetch(uMotion, closest, 0).xy;
   if (uHistoryValid == 0 || any(lessThan(historyUv, vec2(0.0))) || any(greaterThan(historyUv, vec2(1.0))))
   {
      imageStore(uOut, p, vec4(untonemap(filtered), rawColor.a));
      imageStore(uCoverageOut, p, vec4(coverage));
      return;
   }

   vec3 history = tonemap(max(sampleTextureCatmullRom(uHistory, historyUv, size).rgb, vec3(0.0)));

   // The box: half way between the 5-tap cross and the 9-tap square.
   vec3 box5min = s[0], box5max = s[0];
   for (int i = 1; i < 5; i++)
   {
      box5min = min(box5min, s[i]);
      box5max = max(box5max, s[i]);
   }
   vec3 boxmin = mix(box5min, box9min, 0.5);
   vec3 boxmax = mix(box5max, box9max, 0.5);
   vec3 boxCenter = (boxmin + boxmax) * 0.5;
   boxmin = min(boxmin, boxCenter - kMinBoxHalfWidth);
   boxmax = max(boxmax, boxCenter + kMinBoxHalfWidth);
   history = clipToBox(boxmin, boxmax, filtered, history);

   vec3 result = mix(history, filtered, uAlpha);
   float coverageHistory = textureLod(uCoverageHistory, historyUv, 0.0).r;
   imageStore(uOut, p, vec4(untonemap(result), rawColor.a));
   imageStore(uCoverageOut, p, vec4(mix(coverageHistory, coverage, uAlpha)));
}
