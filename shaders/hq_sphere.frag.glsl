#version 450 core
// Turbo 0.71 (S3): Render 3D HQ sphere impostors (hq_sphere.vert). The view
// ray meets the sphere analytically: exact silhouette, exact depth
// (gl_FragDepth, never nearer than the quad on the tangent plane in front of
// it, so depth_greater keeps early depth rejection) and the sphere's own
// normal, shaded
// by the same Filament model as triangles (hqShade). One program for both
// passes: the prepass writes depth + view normal (uPrepass = 1), the shading
// pass recomputes the identical depth and compares LEQUAL against it; S4: the
// shadow map uses hq_sphere_shadow.frag instead (no exact depth needed).
in vec3 vWorldPos;
flat in vec3 vCentre;
flat in float vRadius;
flat in vec4 vColor;
flat in vec4 vCurClip;
flat in vec4 vPrevClip;

uniform mat4 uView;
uniform vec2 uFrameSize;
uniform int uOrtho;
uniform vec3 uViewDir;
uniform int uPrepass;
uniform int uEarlyDepth; // shading pass: the prepass depth is in uSceneDepth

layout(depth_greater) out float gl_FragDepth;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outVelocity; // S6: prepass, as hq_prepass.frag (the centre's motion)
// S7: prepass - the point's colour and its slot (1-based) for the deferred
// shading pass (hq_sphere_deferred.frag), which shades each pixel once.
layout(location = 3) out vec4 outAlbedo;
uniform float uSlotId;

//@include hq_common.glsl
//@include hq_brdf.glsl
//@include hq_shadows.glsl
//@include hq_lighting.glsl

void main()
{
   vec3 rd = (uOrtho == 1) ? uViewDir : normalize(vWorldPos - uCamPos);
   vec3 ro = (uOrtho == 1) ? vWorldPos - uViewDir * vRadius : uCamPos;
   vec3 oc = ro - vCentre;
   float b = dot(oc, rd);
   float disc = b * b - (dot(oc, oc) - vRadius * vRadius);
   bool hit = disc >= 0.0;
   // Missing pixels still get a continuous normal (the silhouette's) so the
   // derivatives specular AA takes are sane; taken here, before any discard.
   float t = -b - sqrt(max(disc, 0.0));
   vec3 p = ro + rd * t;
   vec3 n = normalize(p - vCentre);
   float kernelRoughness = computeKernelRoughness(n, 0.15, 0.2);
   vec4 clip = uViewProj * vec4(p, 1.0);
   float depth = clamp((clip.z / clip.w) * 0.5 + 0.5, 0.0, 1.0);
   gl_FragDepth = depth;

   if (uPrepass == 1)
   {
      if (!hit)
         discard;
      outNormal = vec4(normalize(mat3(uView) * n), uRoughness);
      vec2 v = vPrevClip.w > 1e-6 ? (vCurClip.xy / vCurClip.w - vPrevClip.xy / vPrevClip.w) * 0.5 * uFrameSize
                                  : vec2(0.0);
      outVelocity = vec4(v, 1.0, 0.0);
      outAlbedo = vec4(vColor.rgb, uSlotId / 255.0); // RGBA8 target
      return;
   }

   if (!hit)
      discard;
   // Dense clouds: a neighbour's quad passes the early depth test (it sits in
   // front of its sphere) but its sphere is hidden - leave before shading.
   // A few 24-bit steps of slack: the visible fragment computed the same depth.
   if (uEarlyDepth == 1 && depth > texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r + 4e-7)
      discard;

   HqSurface s;
   s.pos = p;
   s.geoN = n;
   s.n = n;
   s.base = toLinear(uBaseColor) * vColor.rgb; // Color multiplies, as in Render 3D
   s.perceptualRoughness = uRoughness;
   s.metallic = uMetallic;
   s.ao = 1.0;
   s.hasAo = false;
   s.clearCoat = uClearcoat;
   s.sheen = uSheen;
   s.emissive = toLinear(uEmissionColor) * uEmission;
   s.normalMapped = false;
   s.kernelRoughness = kernelRoughness;
   vec3 col = hqShade(s);
   outColor = vec4(col * uExposure, 1.0);
}
