#version 430
// Turbo 0.70: Render 3D FX's motion blur, `uShutter` frames long and centred
// on each pixel (several frames read as streaks). The render's velocity target
// holds the geometry's motion in its own pixels (B = 1 where geometry was
// drawn; `uVelScale` brings it to this image's size); the background moves
// with the camera alone.
//
// Three passes (McGuire et al. 2012, "A reconstruction filter for plausible
// motion blur"): uPass 0 writes each 16x16 tile's longest motion; uPass 1 the
// longest one that can reach each tile (neighbour max); uPass 2 gathers every
// pixel along that motion, weighting each sample by depth - a surface in
// front smears over what is behind it, never the other way round. Averaging
// only along a pixel's own motion blurred a moving object inside its own
// silhouette - its colour never streaked over the background, so the blur
// hardly showed.
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uScene;
layout(binding = 1) uniform sampler2D uVelocity;
layout(binding = 2) uniform sampler2D uTileMax;
layout(binding = 3) uniform sampler2D uDepth;
layout(rgba16f, binding = 0) uniform writeonly image2D uOut;
layout(rg16f, binding = 1) uniform writeonly image2D uTileOut;

uniform int uPass;
uniform ivec2 uSize;
uniform vec2 uVelScale;
uniform float uShutter;
uniform mat4 uInvViewProj;
uniform mat4 uPrevViewProj;
uniform mat4 uInvProj;

const int kTile = 16;
const float kMaxLen = 192.0;

// This pixel's motion over the shutter, in pixels of this image.
vec2 VelocityAt(ivec2 p)
{
   vec4 v = texture(uVelocity, (vec2(p) + 0.5) / vec2(uSize));
   vec2 vel = v.xy * uVelScale;
   if (v.z < 0.5)
   {
      // Background: the far point under this pixel, through last frame's camera.
      vec2 ndc = (vec2(p) + 0.5) / vec2(uSize) * 2.0 - 1.0;
      vec4 farP = uInvViewProj * vec4(ndc, 1.0, 1.0);
      farP /= farP.w;
      vec4 prev = uPrevViewProj * vec4(farP.xyz, 1.0);
      vel = prev.w > 1e-5 ? (ndc - prev.xy / prev.w) * 0.5 * vec2(uSize) : vec2(0.0);
   }
   vel *= uShutter;
   float len = length(vel);
   if (len > kMaxLen)
      vel *= kMaxLen / len;
   return vel;
}

// Distance from the camera of the surface under p (view space).
float LinearDepth(ivec2 p)
{
   float d = texture(uDepth, (vec2(p) + 0.5) / vec2(uSize)).r;
   vec4 v = uInvProj * vec4(0.0, 0.0, d * 2.0 - 1.0, 1.0);
   return -v.z / v.w;
}

// 1 when a is in front of b (by a soft margin), 0 when behind.
float SoftDepthCompare(float a, float b)
{
   return clamp(1.0 - (a - b) / max(0.01, 0.02 * b), 0.0, 1.0);
}

float Cone(float d, float len)
{
   return clamp(1.0 - d / max(len, 1e-3), 0.0, 1.0);
}

float Cylinder(float d, float len)
{
   return 1.0 - smoothstep(0.95 * len, 1.05 * len + 1e-3, d);
}

void main()
{
   ivec2 g = ivec2(gl_GlobalInvocationID.xy);
   if (uPass == 0)
   {
      ivec2 tiles = (uSize + kTile - 1) / kTile;
      if (g.x >= tiles.x || g.y >= tiles.y)
         return;
      vec2 best = vec2(0.0);
      float bestLen = 0.0;
      for (int y = 0; y < kTile; y++)
         for (int x = 0; x < kTile; x++)
         {
            ivec2 p = g * kTile + ivec2(x, y);
            if (p.x >= uSize.x || p.y >= uSize.y)
               continue;
            vec2 v = VelocityAt(p);
            float l = dot(v, v);
            if (l > bestLen)
            {
               bestLen = l;
               best = v;
            }
         }
      imageStore(uTileOut, g, vec4(best, 0.0, 0.0));
      return;
   }
   if (uPass == 1)
   {
      // Neighbour max: the longest tile motion within reach of this tile -
      // a streak reaches half its length (kMaxLen / 2) to either side, so
      // the search spans that many tiles, not just the adjacent ones.
      ivec2 tiles = textureSize(uTileMax, 0);
      if (g.x >= tiles.x || g.y >= tiles.y)
         return;
      const int kReach = (int(kMaxLen) / 2 + kTile - 1) / kTile;
      vec2 best = vec2(0.0);
      float bestLen = 0.0;
      for (int dy = -kReach; dy <= kReach; dy++)
         for (int dx = -kReach; dx <= kReach; dx++)
         {
            ivec2 t = g + ivec2(dx, dy);
            if (t.x < 0 || t.y < 0 || t.x >= tiles.x || t.y >= tiles.y)
               continue;
            vec2 v = texelFetch(uTileMax, t, 0).xy;
            float l = dot(v, v);
            // Only a motion long enough to reach this tile counts.
            float reach = 0.5 * sqrt(l) + float(kTile);
            vec2 gap = vec2(max(abs(dx) - 1, 0), max(abs(dy) - 1, 0)) * float(kTile);
            if (l > bestLen && dot(gap, gap) <= reach * reach)
            {
               bestLen = l;
               best = v;
            }
         }
      imageStore(uTileOut, g, vec4(best, 0.0, 0.0));
      return;
   }

   ivec2 p = g;
   if (p.x >= uSize.x || p.y >= uSize.y)
      return;
   vec4 c0 = texelFetch(uScene, p, 0);
   vec2 vn = texelFetch(uTileMax, p / kTile, 0).xy; // the neighbour max (uPass 1)
   float ln = length(vn);
   if (ln < 0.5)
   {
      imageStore(uOut, p, c0);
      return;
   }
   float lp = length(VelocityAt(p));
   float zp = LinearDepth(p);
   // A pixel smeared over L pixels keeps 1/L of itself.
   float w0 = 1.0 / max(lp, 1.0);
   vec4 sum = c0 * w0;
   float wsum = w0;
   int taps = clamp(int(ceil(ln)), 2, 32);
   // A different offset per pixel along the samples (interleaved gradient
   // noise), so the 16-pixel tiles leave no steps.
   float jitter = fract(52.9829189 * fract(dot(vec2(p), vec2(0.06711056, 0.00583715)))) - 0.5;
   for (int k = 0; k < taps; k++)
   {
      float s = (float(k) + 0.5 + jitter) / float(taps) - 0.5;
      ivec2 q = clamp(ivec2(vec2(p) + vn * s + 0.5), ivec2(0), uSize - 1);
      if (q == p)
         continue;
      float d = length(vec2(q - p));
      float lq = length(VelocityAt(q));
      float zq = LinearDepth(q);
      // A sample in front smears over p by its own motion; one behind is
      // seen only through p's own motion; both moving alike blend.
      float front = SoftDepthCompare(zq, zp), back = SoftDepthCompare(zp, zq);
      float w = front * Cone(d, lq) + back * Cone(d, lp) + Cylinder(d, lq) * Cylinder(d, lp) * 2.0;
      sum += texelFetch(uScene, q, 0) * w;
      wsum += w;
   }
   imageStore(uOut, p, sum / wsum);
}
