// Turbo 0.71 (S2, S3): Render 3D HQ - Filament's standard model as one
// function, hqShade(), shared by the triangle pass (hq_surface.frag) and the
// sphere impostors (hq_sphere.frag). Assembly of Filament's
// shaders/src/surface_shading_model_standard.fs and surface_light_indirect.fs
// @ 13be3649 (Apache License 2.0, Copyright Google LLC); the terms are in
// hq_brdf.glsl. Include after hq_common.glsl, hq_brdf.glsl and (S4)
// hq_shadows.glsl.
// Light types and their falloff are Render 3D's (sun = directional here).

uniform vec3 uCamPos;
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissionColor;
uniform float uEmission;
uniform float uIor;
uniform float uSpecular;
uniform float uClearcoat;
uniform float uClearcoatRoughness;
uniform float uSheen;
uniform vec3 uSheenColor;
uniform float uSheenRoughness;

uniform int uLightCount;
uniform vec3 uLightVec[3];
uniform vec3 uLightColor[3];
uniform float uLightIntensity[3];
uniform int uLightType[3];
uniform float uLightSpotAngle[3];
uniform float uLightSpotPenumbra[3];

uniform int uHasIbl;
uniform sampler2D uIrrEnv;
uniform sampler2D uPrefEnv;
uniform sampler2D uDfg;
uniform float uPrefLevelOne; // prefiltered levels - 1 (the roughness-1 mip)
uniform float uEnvRotation;
uniform float uEnvIntensity;
uniform vec3 uEnvSky;
uniform vec3 uEnvHorizon;
uniform vec3 uEnvGround;
uniform float uExposure;
// S5: screen-space AO (GTAO, hq_gtao.comp) - 0 off, 1 on, 2 show it alone.
uniform int uSsao;
uniform sampler2D uSsaoMap;  // (visibility, view z) at the AO size
uniform vec2 uFrameTexel;    // 1 / frame size: gl_FragCoord -> uv

struct HqSurface
{
   vec3 pos;                  // world
   vec3 geoN;                 // geometric normal, facing the viewer
   vec3 n;                    // shading normal (normal-mapped or geoN)
   vec3 base;                 // linear base colour
   float perceptualRoughness; // before specular AA and the clear coat
   float metallic;
   float ao;                  // material occlusion (1 = none)
   bool hasAo;                // an AO map: micro-shadowing on direct light
   float clearCoat;
   float sheen;
   vec3 emissive;             // linear
   bool normalMapped;
   // Specular AA kernel (computeKernelRoughness of the geometric normal),
   // taken by the caller in uniform control flow, before any discard.
   float kernelRoughness;
};

vec3 prefilteredRadiance(vec3 r, float perceptualRoughness)
{
   if (uHasIbl == 1)
      return textureLod(uPrefEnv, dirToEquirect(r, uEnvRotation),
                        perceptualRoughnessToLod(perceptualRoughness, uPrefLevelOne)).rgb * uEnvIntensity;
   return gradientEnv(r, uEnvSky, uEnvHorizon, uEnvGround) * uEnvIntensity;
}

vec3 diffuseIrradiance(vec3 n)
{
   if (uHasIbl == 1)
      return textureLod(uIrrEnv, dirToEquirect(n, uEnvRotation), 0.0).rgb * uEnvIntensity;
   return gradientEnv(n, uEnvSky, uEnvHorizon, uEnvGround) * uEnvIntensity;
}

// S5: the half-size GTAO at this pixel, depth-aware: the 4 bilinear taps
// weighted by how close their view z is to this fragment's, so a silhouette
// does not pick up the occlusion of what is behind it (Filament's upsampling
// at HIGH quality, surface_ambient_occlusion.fs; edge distance = the blur's
// 0.05 m bilateral threshold).
float ssaoVisibility()
{
   vec2 size = vec2(textureSize(uSsaoMap, 0));
   vec2 st = gl_FragCoord.xy * uFrameTexel * size - 0.5;
   vec2 base = floor(st);
   vec2 f = st - base;
   float z = -linearDepth(gl_FragCoord.z);
   float sum = 0.0, weights = 0.0;
   for (int k = 0; k < 4; k++)
   {
      ivec2 o = ivec2(k & 1, k >> 1);
      vec2 s = texelFetch(uSsaoMap, clamp(ivec2(base) + o, ivec2(0), ivec2(size) - 1), 0).rg;
      float bilinear = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
      float d = (s.y - z) * 20.0;
      float w = bilinear * max(1e-4, 1.0 - d * d);
      sum += s.x * w;
      weights += w;
   }
   return sum / max(weights, 1e-6);
}

// Linear HDR radiance towards the camera (exposure not applied).
vec3 hqShade(HqSurface s)
{
   vec3 geoN = s.geoN, n = s.n;
   float metallic = clamp(s.metallic, 0.0, 1.0);
   float clearCoat = clamp(s.clearCoat, 0.0, 1.0);
   float ao = s.ao;
   // S5: indirect light sees min(material AO, SSAO) (Filament's diffuseAO);
   // direct light keeps only the material's micro-shadowing.
   float indirectAO = ao;
   if (uSsao != 0)
      indirectAO = min(ao, ssaoVisibility());

   // Pixel parameters (surface_shading_lit.fs order): specular AA on the
   // geometric normal, clear coat roughens the base, clamp, square.
   float perceptualRoughness = normalFiltering(s.kernelRoughness,
                                               clamp(s.perceptualRoughness, 0.0, 1.0));
   float clearCoatPerceptualRoughness = clamp(uClearcoatRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
   perceptualRoughness = mix(perceptualRoughness, max(perceptualRoughness, clearCoatPerceptualRoughness), clearCoat);
   perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
   float roughness = perceptualRoughness * perceptualRoughness;
   float clearCoatRoughness = clearCoatPerceptualRoughness * clearCoatPerceptualRoughness;

   vec3 v = normalize(uCamPos - s.pos);
   float NoV = max(dot(n, v), MIN_N_DOT_V);
   // Material adapter: Render 3D's dielectric f0 (IOR, scaled by specular:
   // 1.5 and 0.5 give Filament's default 0.04).
   float iorF0 = pow((uIor - 1.0) / (uIor + 1.0), 2.0);
   vec3 dielectricF0 = vec3(clamp(iorF0 * 2.0 * uSpecular, 0.0, 1.0));
   vec3 f0 = s.base * metallic + dielectricF0 * (1.0 - metallic);
   vec3 diffuseColor = (1.0 - metallic) * s.base;

   vec3 dfg = textureLod(uDfg, vec2(NoV, perceptualRoughness), 0.0).rgb;
   vec3 energyCompensation = 1.0 + f0 * (1.0 / max(dfg.y, 1e-4) - 1.0);

   float sheenPerceptualRoughness = clamp(uSheenRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
   float sheenRoughness = sheenPerceptualRoughness * sheenPerceptualRoughness;
   vec3 sheenColor = toLinear(uSheenColor) * max(s.sheen, 0.0);
   float sheenDFG = textureLod(uDfg, vec2(NoV, sheenPerceptualRoughness), 0.0).b;
   float sheenScaling = 1.0 - max3(sheenColor) * sheenDFG;
   bool hasSheen = max3(sheenColor) > 0.0;

   // --- direct lights ---------------------------------------------------------
   vec3 col = vec3(0.0);
   for (int i = 0; i < uLightCount; i++)
   {
      vec3 radiance = toLinear(uLightColor[i]) * uLightIntensity[i];
      if (uLightType[i] == 3) // ambient (Render 3D's): flat diffuse
      {
         col += diffuseColor * radiance * indirectAO;
         continue;
      }
      vec3 l;
      float attenuation = 1.0;
      if (uLightType[i] == 1 || uLightType[i] == 4) // point, spot: uLightVec is the position
      {
         vec3 toLight = uLightVec[i] - s.pos;
         float dist = length(toLight);
         l = toLight / max(dist, 1e-4);
         attenuation = 1.0 / (1.0 + dist * dist * 0.25);
         if (uLightType[i] == 4)
         {
            vec3 spotDir = normalize(-uLightVec[i]);
            float cosOuter = cos(radians(uLightSpotAngle[i]));
            float cosInner = cos(radians(uLightSpotAngle[i] * (1.0 - uLightSpotPenumbra[i])));
            float spot = clamp((dot(-l, spotDir) - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
            attenuation *= spot * spot;
         }
      }
      else
         l = normalize(uLightVec[i]);

      float NoL = clamp(dot(n, l), 0.0, 1.0);
      if (NoL <= 0.0 && !(clearCoat > 0.0 && s.normalMapped))
         continue;
      vec3 h = normalize(v + l);
      float NoH = clamp(dot(n, h), 0.0, 1.0);
      float LoH = clamp(dot(l, h), 0.0, 1.0);

      vec3 Fr = D_GGX(roughness, NoH) * V_SmithGGXCorrelated(roughness, NoV, NoL) * fresnel(f0, LoH);
      Fr *= energyCompensation;
      vec3 Fd = diffuseColor * Fd_Lambert();
      vec3 color = Fd + Fr;
      if (hasSheen)
      {
         color *= sheenScaling;
         color += D_Charlie(sheenRoughness, NoH) * V_Neubelt(NoV, NoL) * sheenColor;
      }
      // The material AO map shadows direct light too (micro-shadowing).
      float occlusion = s.hasAo ? computeMicroShadowing(NoL, ao) : 1.0;
      // S4: the shadowed light - its map, then contact shadows where it is lit.
      if (i == uShadowLight)
      {
         float visibility = shadowVisibility(s.pos, geoN, l);
         if (uContactShadows == 1 && visibility > 0.0)
            visibility *= contactShadow(s.pos, l);
         occlusion *= visibility;
      }
      if (clearCoat > 0.0)
      {
         float clearCoatNoH = s.normalMapped ? clamp(dot(geoN, h), 0.0, 1.0) : NoH;
         float Fcc = F_Schlick(0.04, 1.0, LoH) * clearCoat;
         float clearCoatLobe = D_GGX(clearCoatRoughness, clearCoatNoH) * V_Kelemen(LoH) * Fcc;
         if (s.normalMapped)
         {
            color *= (1.0 - Fcc) * NoL;
            color += clearCoatLobe * clamp(dot(geoN, l), 0.0, 1.0);
            col += color * radiance * (attenuation * occlusion);
            continue;
         }
         color *= 1.0 - Fcc;
         color += clearCoatLobe;
      }
      col += color * radiance * (attenuation * NoL * occlusion);
   }

   // --- image-based light (prefiltered path) ------------------------------------
   vec3 E = mix(dfg.xxx, dfg.yyy, f0);
   vec3 r = reflect(-v, n);
   vec3 Fr = E * prefilteredRadiance(getSpecularDominantDirection(n, r, roughness), perceptualRoughness);
   Fr *= energyCompensation;
   float specularAO = SpecularAO_Lagarde(NoV, indirectAO, roughness);
   vec3 Fd = diffuseColor * diffuseIrradiance(n) * (1.0 - E);
   // Multi-bounce AO is on by default on the desktop (singleBounceAO = 1).
   Fd *= gtaoMultiBounce(indirectAO, diffuseColor);
   Fr *= gtaoMultiBounce(specularAO, f0);
   if (hasSheen)
   {
      Fd *= sheenScaling;
      Fr *= sheenScaling;
      Fr += sheenDFG * sheenColor * SpecularAO_Lagarde(NoV, indirectAO, sheenRoughness) *
            prefilteredRadiance(r, sheenPerceptualRoughness);
   }
   if (clearCoat > 0.0)
   {
      float clearCoatNoV = max(dot(geoN, v), MIN_N_DOT_V);
      float Fc = F_Schlick(0.04, 1.0, clearCoatNoV) * clearCoat;
      Fd *= 1.0 - Fc;
      Fr *= 1.0 - Fc;
      Fr += prefilteredRadiance(reflect(-v, geoN), clearCoatPerceptualRoughness) *
            (SpecularAO_Lagarde(clearCoatNoV, indirectAO, clearCoatRoughness) * Fc);
   }
   col += Fd + Fr;
   if (uSsao == 2)
      return vec3(indirectAO); // S5 debug: the occlusion alone
   return col + s.emissive;
}
