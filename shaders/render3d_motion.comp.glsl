#version 430
// Turbo 0.70 (MV): Render 3D's motion vectors - for every pixel, how far the
// surface it shows moved on screen since the last frame, in uv units:
// (this frame's unjittered uv) - (last frame's uv). From the depth buffer and
// the two cameras, so it covers camera motion; a surface that moves on its
// own (animated points, a moving model) reads as still and is left to the
// TAA's history clipping. Shared by the TAA (R4) and, later, the RTX
// denoiser's temporal pass (X1).
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uDepth;              // hardware depth, 0..1
layout(binding = 1) uniform sampler2D uVelocity;           // Turbo 0.70: Render3DMotion's, when uHasVelocity
layout(rg16f, binding = 0) uniform writeonly image2D uMotion;

uniform mat4 uInvViewProj;  // this frame's camera, jittered (what the depth was drawn with)
uniform mat4 uPrevViewProj; // last frame's camera, unjittered
uniform vec2 uJitterUv;     // this frame's jitter, uv units
uniform ivec2 uSize;
uniform int uHasVelocity;

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   vec2 uv = (vec2(p) + 0.5) / vec2(uSize);
   // Turbo 0.70: a surface that moves on its own (copies, particles) - its
   // velocity pass motion (pixels, unjittered, now - last frame) wins.
   if (uHasVelocity == 1)
   {
      vec4 v = texelFetch(uVelocity, p, 0);
      if (v.z > 0.5)
      {
         imageStore(uMotion, p, vec4(v.xy / vec2(uSize), 0.0, 0.0));
         return;
      }
   }
   float d = texelFetch(uDepth, p, 0).r;
   vec4 world = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
   world /= world.w;
   vec4 prev = uPrevViewProj * world;
   vec2 prevUv = prev.xy / prev.w * 0.5 + 0.5;
   imageStore(uMotion, p, vec4((uv - uJitterUv) - prevUv, 0.0, 0.0));
}
