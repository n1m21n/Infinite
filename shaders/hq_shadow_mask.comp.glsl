#version 450 core
// Turbo 0.71 (S9): Render 3D HQ - the shadowed light's soft and contact
// terms once per pixel, from this frame's prepass (depth copy, view normals),
// into (visibility, view z) for the GTAO's bilateral blur (hq_ao_blur.comp).
// The soft filter and the contact march use per-pixel noise; computed in the
// shading passes it showed as dots and streaks without TAA. Smoothed here
// along the surfaces (the blur stops at depth edges), the shading passes read
// the result at their pixel (hq_shadows.glsl, uShadowMaskOn).
#define HQ_COMPUTE
layout(local_size_x = 8, local_size_y = 8) in;

layout(rg16f, binding = 0) uniform writeonly image2D uDst;
uniform sampler2D uSceneNormalMap; // xyz: view-space normal (prepass)
uniform mat4 uInvViewProj;         // the jittered camera the depth was drawn with
uniform mat4 uViewMatrix;
uniform vec3 uLightDir;            // towards the light, normalised
uniform ivec2 uSize;

//@include hq_common.glsl
//@include hq_shadows.glsl

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   float depth = texelFetch(uSceneDepth, p, 0).r;
   if (depth >= 1.0)
   {
      imageStore(uDst, p, vec4(1.0, -65000.0, 0.0, 0.0)); // nothing drawn: the blur skips it
      return;
   }
   vec2 ndc = (vec2(p) + 0.5) / vec2(uSize) * 2.0 - 1.0;
   vec4 world = uInvViewProj * vec4(ndc, depth * 2.0 - 1.0, 1.0);
   vec3 pos = world.xyz / world.w;
   vec3 geoN = normalize(transpose(mat3(uViewMatrix)) * texelFetch(uSceneNormalMap, p, 0).xyz);
   vec2 pixel = vec2(p) + 0.5;
   float visibility = shadowVisibilityAt(pos, geoN, uLightDir, pixel);
   if (uContactShadows == 1 && visibility > 0.0)
      visibility *= contactShadowAt(pos, uLightDir, pixel);
   float viewZ = (uViewMatrix * vec4(pos, 1.0)).z;
   imageStore(uDst, p, vec4(visibility, viewZ, 0.0, 0.0));
}
