// Turbo 0.71 (S4): Render 3D HQ shadows - the shadow map of one light (the
// first directional / sun, or the built-in light) and screen-space contact
// shadows. Techniques as Filament describes them (shaders/src/
// surface_shadowing.fs @ 13be3649, docs/filament-notes.md 7.2; Apache License
// 2.0, Copyright Google LLC): the default PCF "low" filter is hardware PCF
// plus a 3x3 Gaussian in 4 bilinear taps (Castano 2013, "Shadow Mapping
// Summary Part 1"); contact shadows march the depth buffer towards the light
// (8 steps, interleaved gradient noise, faded at the screen edges).
// Changes: one orthographic map instead of cascades, no reversed Z; the soft
// mode is a PCSS-lite on the same depth map (blocker search + penumbra from
// the blocker distance, 16-tap Poisson filter), not Filament's EVSM-based
// PCSS. Include after hq_common.glsl, before hq_lighting.glsl.
// S9: the soft mode's blocker search uses the disc as is (not rotated per
// pixel: a rotated search found no blocker at some penumbra pixels, which
// came out fully lit - the dotted, streaky edge seen without TAA); only the
// filter is rotated, by a noise that also turns every frame under TAA. The
// soft and contact terms are computed once per pixel into a screen-space
// mask (hq_shadow_mask.comp, which defines HQ_COMPUTE) and smoothed with the
// GTAO's bilateral blur; the shading passes read the mask (uShadowMaskOn).

uniform mat4 uViewProj;
uniform int uShadowLight;          // index of the shadowed light, -1 = none
uniform sampler2DShadow uShadowMap;
uniform sampler2D uShadowDepth;    // the same map without comparison (blocker search)
uniform mat4 uLightViewProj;
uniform vec2 uShadowTexel;         // 1 / map size
uniform float uShadowNormalBias;   // world units (normal bias x one texel)
uniform float uShadowConstBias;    // light depth units
uniform float uShadowDepthRange;   // world metres the map's 0..1 depth spans
uniform float uShadowSoftness;     // 0 = PCF low; > 0 = PCSS-lite (light size)
uniform int uContactShadows;
uniform sampler2D uSceneDepth;     // copy of this frame's depth (after the prepass)
uniform vec4 uProjParams;          // proj m[10], m[14], ortho flag, contact distance
// S9: the noise's frame under TAA (0 = the same every frame), and the
// screen-space mask of the soft / contact terms (1 = read it).
uniform int uShadowNoiseFrame;
uniform int uShadowMaskOn;
uniform sampler2D uShadowMask;     // (visibility, view z), frame size

float interleavedGradientNoise(vec2 w)
{
   const vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
   return fract(m.z * fract(dot(w, m.xy)));
}

// S9: Jimenez's interleaved gradient noise moved by 5.588238 pixels a frame
// (its temporal form), so the TAA sees a new rotation every frame.
float shadowNoise(vec2 pixel, float offset)
{
   return interleavedGradientNoise(pixel + offset + 5.588238 * float(uShadowNoiseFrame & 63));
}

// Castano 2013: 3x3 Gaussian over hardware 2x2 PCF in 4 taps.
float shadowPcfLow(vec3 position)
{
   vec2 size = 1.0 / uShadowTexel;
   vec2 uv = position.xy * size + 0.5;
   vec2 base = (floor(uv) - 0.5) * uShadowTexel;
   vec2 st = fract(uv);
   vec2 uw = vec2(3.0 - 2.0 * st.x, 1.0 + 2.0 * st.x);
   vec2 vw = vec2(3.0 - 2.0 * st.y, 1.0 + 2.0 * st.y);
   vec2 u = vec2((2.0 - st.x) / uw.x - 1.0, st.x / uw.y + 1.0) * uShadowTexel.x;
   vec2 v = vec2((2.0 - st.y) / vw.x - 1.0, st.y / vw.y + 1.0) * uShadowTexel.y;
   float sum = 0.0;
   sum += uw.x * vw.x * textureLod(uShadowMap, vec3(base + vec2(u.x, v.x), position.z), 0.0);
   sum += uw.y * vw.x * textureLod(uShadowMap, vec3(base + vec2(u.y, v.x), position.z), 0.0);
   sum += uw.x * vw.y * textureLod(uShadowMap, vec3(base + vec2(u.x, v.y), position.z), 0.0);
   sum += uw.y * vw.y * textureLod(uShadowMap, vec3(base + vec2(u.y, v.y), position.z), 0.0);
   return sum * (1.0 / 16.0);
}

const vec2 kPoisson[16] = vec2[](
   vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
   vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
   vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
   vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
   vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
   vec2(0.14383161, -0.14100790));

// PCSS-lite: average blocker depth in a search disc, penumbra width from the
// blocker-receiver distance (a directional light whose half-angle tangent is
// softness x 0.03), then a rotated 16-tap Poisson PCF of that width. S9: the
// search disc is not rotated (neighbouring pixels agree on the blockers).
float shadowPcss(vec3 position, float noise)
{
   float angle = noise * 6.2831853;
   mat2 rot = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
   float lightSize = uShadowSoftness * 0.03;
   float texelWorld = uShadowTexel.x * uShadowDepthRange; // ~ one texel in metres
   // Search wide enough for a blocker half a metre up.
   vec2 search = uShadowTexel * clamp(0.5 * lightSize / texelWorld, 2.0, 48.0);
   float blockers = 0.0, sum = 0.0;
   for (int i = 0; i < 16; i++)
   {
      float d = textureLod(uShadowDepth, position.xy + kPoisson[i] * search, 0.0).r;
      if (d < position.z)
      {
         sum += d;
         blockers += 1.0;
      }
   }
   if (blockers == 0.0)
      return 1.0;
   float blockerDist = (position.z - sum / blockers) * uShadowDepthRange; // metres
   vec2 radius = uShadowTexel * clamp(blockerDist * lightSize / texelWorld, 1.0, 48.0);
   float lit = 0.0;
   for (int i = 0; i < 16; i++)
      lit += textureLod(uShadowMap, vec3(position.xy + rot * kPoisson[i] * radius, position.z), 0.0);
   return lit / 16.0;
}

// Visibility of the shadowed light at a world point (1 = lit); `pixel` seeds
// the soft filter's rotation.
float shadowVisibilityAt(vec3 worldPos, vec3 geoN, vec3 l, vec2 pixel)
{
   // Normal bias: along the normal, a light-space texel scaled up as the light
   // grazes the surface; then the constant bias in depth.
   float NoL = clamp(dot(geoN, l), 0.0, 1.0);
   vec3 p = worldPos + geoN * (uShadowNormalBias * (0.25 + sqrt(1.0 - NoL * NoL)));
   vec4 lp = uLightViewProj * vec4(p, 1.0);
   vec3 position = lp.xyz / lp.w * 0.5 + 0.5;
   if (any(lessThan(position, vec3(0.0))) || any(greaterThan(position, vec3(1.0))))
      return 1.0;
   position.z -= uShadowConstBias;
   if (uShadowSoftness > 0.0)
      return shadowPcss(position, shadowNoise(pixel, 0.0));
   return shadowPcfLow(position);
}

// View-space depth (positive, metres) from a 0..1 window depth.
float linearDepth(float windowZ)
{
   float ndc = windowZ * 2.0 - 1.0;
   if (uProjParams.z > 0.5)
      return -(ndc - uProjParams.y) / uProjParams.x;
   return uProjParams.y / (ndc + uProjParams.x);
}

// Screen-space contact shadow: march towards the light in the depth buffer.
float contactShadowAt(vec3 worldPos, vec3 l, vec2 pixel)
{
   const int kSteps = 8;
   float dist = uProjParams.w;
   float dither = shadowNoise(pixel, 17.0);
   for (int i = 0; i < kSteps; i++)
   {
      float t = (float(i) + dither) / float(kSteps);
      vec3 p = worldPos + l * (dist * t * t + 0.005); // denser near the surface
      vec4 clip = uViewProj * vec4(p, 1.0);
      vec3 ndc = clip.xyz / clip.w;
      vec2 uv = ndc.xy * 0.5 + 0.5;
      if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
         break;
      float rayZ = linearDepth(ndc.z * 0.5 + 0.5);
      float sceneZ = linearDepth(textureLod(uSceneDepth, uv, 0.0).r);
      float delta = rayZ - sceneZ;
      // Occluded when the depth buffer is in front of the ray, but not by so
      // much that it is a separate object far in front (thickness).
      if (delta > 0.002 * rayZ && delta < dist)
      {
         vec2 edge = min(uv, 1.0 - uv);
         float fade = smoothstep(0.0, 0.05, min(edge.x, edge.y));
         return 1.0 - fade;
      }
   }
   return 1.0;
}

#ifndef HQ_COMPUTE
// The shadowed light's visibility at this fragment. S9: from the mask when
// the soft or contact term is on (computed and smoothed per pixel already),
// else the plain PCF here.
float shadowVisibility(vec3 worldPos, vec3 geoN, vec3 l)
{
   if (uShadowMaskOn == 1)
      return texelFetch(uShadowMask, ivec2(gl_FragCoord.xy), 0).r;
   float visibility = shadowVisibilityAt(worldPos, geoN, l, gl_FragCoord.xy);
   if (uContactShadows == 1 && visibility > 0.0)
      visibility *= contactShadowAt(worldPos, l, gl_FragCoord.xy);
   return visibility;
}
#endif
