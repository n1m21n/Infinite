#version 450 core
// Turbo 0.71.1: Render 3D HQ - temporal accumulation of the screen-space GI
// (hq_ssgi.comp) before its blur. A few rays per pixel give a noisy, blotchy
// estimate; the TAA's neighbourhood clamp keeps blotches that large, so the
// GI keeps its own history: each pixel's surface is reprojected into last
// frame's GI (last frame's camera), and the new estimate is blended in by
// uAlpha where the history saw the same surface (its view z agrees), else
// the history is dropped (disocclusion, first frame).
layout(local_size_x = 8, local_size_y = 8) in;

layout(rgba16f, binding = 0) uniform writeonly image2D uDst;
uniform sampler2D uCur;        // this frame's raw GI (rgb, view z)
uniform sampler2D uHist;       // last frame's accumulated GI (rgb, view z), linear
uniform sampler2D uDepth;      // this frame's depth (copy), full size
uniform mat4 uInvViewProj;     // this frame's camera (as the depth)
uniform mat4 uPrevViewProj;    // last frame's camera
uniform mat4 uPrevView;        // last frame's view (its view z)
uniform ivec2 uSize;           // GI size
uniform ivec2 uFullSize;
uniform float uAlpha;          // weight of the new estimate
uniform int uHistValid;

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   vec4 cur = texelFetch(uCur, p, 0);
   if (cur.w <= -60000.0 || uHistValid == 0)
   {
      imageStore(uDst, p, cur);
      return;
   }
   ivec2 fp = min(p * uFullSize / uSize, uFullSize - 1);
   vec2 uv = (vec2(fp) + 0.5) / vec2(uFullSize);
   float depth = texelFetch(uDepth, fp, 0).r;
   vec4 w = uInvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
   vec3 P = w.xyz / w.w;
   vec4 prev = uPrevViewProj * vec4(P, 1.0);
   vec2 puv = prev.xy / prev.w * 0.5 + 0.5;
   vec4 result = cur;
   if (prev.w > 0.0 && all(greaterThanEqual(puv, vec2(0.0))) && all(lessThanEqual(puv, vec2(1.0))))
   {
      vec4 hist = textureLod(uHist, puv, 0.0);
      float prevZ = (uPrevView * vec4(P, 1.0)).z;
      // The same surface: its view z then, within 5% (and a centimetre).
      if (abs(hist.w - prevZ) < 0.05 * abs(prevZ) + 0.01)
         result = vec4(mix(hist.rgb, cur.rgb, uAlpha), cur.w);
   }
   imageStore(uDst, p, result);
}
