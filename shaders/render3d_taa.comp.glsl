#version 430
// Turbo 0.70 (R4): Render 3D's temporal anti-aliasing, on the linear HDR image
// before the tonemap. Each frame is drawn with a different sub-pixel jitter;
// this pass blends it into the history (last frame's result, fetched where
// the motion vectors say the surface was) so edges and thin points
// accumulate many samples. Ghosting is held back by clipping the history to
// the spread of this frame's 3x3 neighbourhood (variance clipping in YCoCg,
// Salvi / Playdead); fireflies by blending in a luminance-weighted space
// (Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014).
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uCurrent; // this frame, HDR, jittered
layout(binding = 1) uniform sampler2D uHistory; // last result, HDR, linear filtering
layout(binding = 2) uniform sampler2D uMotion;  // render3d_motion.comp.glsl
layout(binding = 3) uniform sampler2D uDepth;
layout(binding = 4) uniform sampler2D uCoverageHistory;
layout(rgba16f, binding = 0) uniform writeonly image2D uOut;
// How much of the pixel the geometry covers (depth < 1), averaged like the
// colour: the HDR finish uses it to keep the plain background colour exact
// while the edge against it stays anti-aliased.
layout(r16f, binding = 1) uniform writeonly image2D uCoverageOut;

uniform ivec2 uSize;
uniform float uBlend;       // weight of this frame (1 = ignore the history)
uniform int uHistoryValid;

float Luma(vec3 c)
{
   return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Bounded "tonemapped" space the neighbourhood statistics are taken in.
vec3 Compress(vec3 c)
{
   return c / (1.0 + Luma(c));
}

vec3 Expand(vec3 t)
{
   return t / max(1.0 - Luma(t), 1e-3);
}

vec3 ToYCoCg(vec3 c)
{
   return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 FromYCoCg(vec3 c)
{
   return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Catmull-Rom history fetch in five bilinear taps (Jimenez / MJP): sharper
// than one bilinear tap, which would blur a little more every frame.
vec4 SampleCatmullRom(sampler2D tex, vec2 uv, vec2 size)
{
   vec2 pos = uv * size;
   vec2 t1 = floor(pos - 0.5) + 0.5;
   vec2 f = pos - t1;
   vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
   vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
   vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
   vec2 w3 = f * f * (-0.5 + 0.5 * f);
   vec2 w12 = w1 + w2;
   vec2 t0 = (t1 - 1.0) / size;
   vec2 t3 = (t1 + 2.0) / size;
   vec2 t12 = (t1 + w2 / w12) / size;
   vec4 r = textureLod(tex, vec2(t12.x, t0.y), 0.0) * (w12.x * w0.y) +
            textureLod(tex, vec2(t0.x, t12.y), 0.0) * (w0.x * w12.y) +
            textureLod(tex, t12, 0.0) * (w12.x * w12.y) +
            textureLod(tex, vec2(t3.x, t12.y), 0.0) * (w3.x * w12.y) +
            textureLod(tex, vec2(t12.x, t3.y), 0.0) * (w12.x * w3.y);
   float ws = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
   return r / ws;
}

// Moves q toward the box centre until it is inside the box.
vec3 ClipToBox(vec3 lo, vec3 hi, vec3 centre, vec3 q)
{
   vec3 extent = 0.5 * (hi - lo) + 1e-5;
   vec3 v = q - centre;
   vec3 u = abs(v / extent);
   float m = max(u.x, max(u.y, u.z));
   return m > 1.0 ? centre + v / m : q;
}

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   vec4 current = texelFetch(uCurrent, p, 0);
   float coverage = texelFetch(uDepth, p, 0).r < 0.99999 ? 1.0 : 0.0;

   // Neighbourhood statistics, and the motion of its nearest surface (so an
   // edge in front of the background moves with the edge).
   vec3 m1 = vec3(0.0), m2 = vec3(0.0);
   float nearest = 2.0;
   ivec2 nearestP = p;
   for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
      {
         ivec2 q = clamp(p + ivec2(dx, dy), ivec2(0), uSize - 1);
         vec3 c = ToYCoCg(Compress(max(texelFetch(uCurrent, q, 0).rgb, vec3(0.0))));
         m1 += c;
         m2 += c * c;
         float z = texelFetch(uDepth, q, 0).r;
         if (z < nearest)
         {
            nearest = z;
            nearestP = q;
         }
      }
   vec3 mean = m1 / 9.0;
   vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));

   vec2 uv = (vec2(p) + 0.5) / vec2(uSize);
   vec2 historyUv = uv - texelFetch(uMotion, nearestP, 0).xy;
   if (uHistoryValid == 0 || any(lessThan(historyUv, vec2(0.0))) || any(greaterThan(historyUv, vec2(1.0))))
   {
      imageStore(uOut, p, current);
      imageStore(uCoverageOut, p, vec4(coverage));
      return;
   }
   vec4 history = max(SampleCatmullRom(uHistory, historyUv, vec2(uSize)), vec4(0.0));
   const float gamma = 1.0;
   vec3 h = ClipToBox(mean - gamma * sigma, mean + gamma * sigma, mean, ToYCoCg(Compress(history.rgb)));
   vec3 historyRgb = Expand(FromYCoCg(h));

   vec3 c = max(current.rgb, vec3(0.0));
   float wc = uBlend / (1.0 + Luma(c));
   float wh = (1.0 - uBlend) / (1.0 + Luma(historyRgb));
   vec3 result = (c * wc + historyRgb * wh) / max(wc + wh, 1e-6);
   imageStore(uOut, p, vec4(result, mix(history.a, current.a, uBlend)));
   float coverageHistory = textureLod(uCoverageHistory, historyUv, 0.0).r;
   imageStore(uCoverageOut, p, vec4(mix(coverageHistory, coverage, uBlend)));
}
