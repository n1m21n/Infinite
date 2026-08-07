#include "Geometry3DNodes.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

#include "Transport.h"
#include "SceneNodes.h"
#include "GeometryOpNodes.h"
#include "UtilityNodes.h"

namespace
{
   const std::vector<std::string> kShapeNames = {
      "Plane", "Cube", "Sphere", "Icosphere", "Torus", "Cylinder", "Cone", "Torus Knot",
      "Capsule", "Tube", "Pyramid", "Prism", "Helix", "Supershape"
   };
   const std::vector<std::string> kShadingNames = { "Lit", "Normals", "UV", "Flat" };
   const std::vector<std::string> kProjectionNames = { "Perspective", "Orthographic" };
   const std::vector<std::string> kSampleNames = { "Off", "2x", "4x", "8x" };
   const std::vector<std::string> kTonemapNames = { "None", "ACES", "Reinhard" };
   const std::vector<std::string> kShadowQualityNames = { "1024", "2048", "4096" };
   const int kShadowSizes[] = { 1024, 2048, 4096 };

   // Depth-only pass from the light's point of view. Shares the instancing
   // attribute layout with the main shader so an instanced source casts shadows
   // from all its copies rather than just the base mesh.
   const char* kShadowVertSrc =
      "#version 150\n"
      "in vec3 aPos;\n"
      "in mat4 aInstance;\n"
      "uniform mat4 uModel;\n"
      "uniform int uInstanced;\n"
      "uniform mat4 uLightViewProj;\n"
      "void main() {\n"
      "   mat4 model = (uInstanced == 1) ? aInstance : uModel;\n"
      "   gl_Position = uLightViewProj * model * vec4(aPos, 1.0);\n"
      "}\n";

   const char* kShadowFragSrc =
      "#version 150\n"
      "void main() { }\n";
   const int kSampleCounts[] = { 0, 2, 4, 8 };

   // Not in gl3.h - the anisotropic filter extension predates core profiles and
   // is still exposed only under its EXT names on macOS.
   const GLenum kTextureMaxAnisotropy = 0x84FE;
   const GLenum kMaxTextureMaxAnisotropy = 0x84FF;

   // Give the currently bound texture mipmaps and anisotropic filtering, then
   // put its filter back the way the 2D pipeline left it.
   //
   // Surface textures belong to other nodes: they are ordinary FBO textures
   // built with GL_LINEAR and no mip chain, and a UV-mapped object minifying
   // one of those aliases hard - it is the shimmer you see on a textured cube
   // at 4K. Mipmaps have to be regenerated per frame because the owning node
   // may have re-rendered into the texture since we last looked.
   //
   // The filter is restored afterwards rather than left on: the owner keeps
   // drawing into level 0 without telling us, and leaving a mip filter on a
   // chain that is about to go stale would show up as blur in its own preview.
   void PrepareSurfaceTexture()
   {
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

      static float sMaxAniso = -1.0f;
      if (sMaxAniso < 0.0f)
      {
         sMaxAniso = 1.0f;
         glGetFloatv(kMaxTextureMaxAnisotropy, &sMaxAniso);
         // Querying an unsupported enum leaves the value untouched and raises
         // GL_INVALID_ENUM; clear it so the next real error is not misread.
         if (glGetError() != GL_NO_ERROR || sMaxAniso < 1.0f)
            sMaxAniso = 1.0f;
      }
      if (sMaxAniso > 1.0f)
         glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropy, std::min(8.0f, sMaxAniso));
   }

   void RestoreSurfaceTexture()
   {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   }

   // Bound in place of a real surface texture when a geometry has none. Binding
   // texture 0 instead leaves the sampler pointing at an incomplete texture,
   // which the macOS driver reports as "unloadable ... using zero texture" -
   // harmless, since uHasTexture gates the sample, but it is a real warning that
   // would mask a real one later.
   unsigned int WhiteTexture()
   {
      static unsigned int sTex = 0;
      if (sTex == 0)
      {
         const unsigned char white[4] = { 255, 255, 255, 255 };
         glGenTextures(1, &sTex);
         glBindTexture(GL_TEXTURE_2D, sTex);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      }
      return sTex;
   }

   // Bound to the shadow sampler when shadows are off. A depth sampler pointed
   // at texture 0 is incomplete, and the driver warns about it - the same issue
   // the white texture solves for the colour sampler.
   unsigned int DummyShadowTexture()
   {
      static unsigned int sTex = 0;
      if (sTex == 0)
      {
         const float one = 1.0f; // fully lit
         glGenTextures(1, &sTex);
         glBindTexture(GL_TEXTURE_2D, sTex);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0,
                      GL_DEPTH_COMPONENT, GL_FLOAT, &one);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
      }
      return sTex;
   }

   const char* kVertSrc =
      "#version 150\n"
      "in vec3 aPos;\n"
      "in vec3 aNormal;\n"
      "in vec2 aUv;\n"
      "in mat4 aInstance;\n"      // per-instance transform, divisor 1
      "in vec3 aInstanceColor;\n" // per-instance tint, divisor 1
      "uniform mat4 uModel;\n"
      "uniform int uInstanced;\n"
      "uniform int uInstanceColored;\n"
      "uniform mat4 uViewProj;\n"
      "uniform mat3 uNormalMatrix;\n"
      "uniform mat4 uLightViewProj;\n"
      "out vec4 vLightSpacePos;\n"
      "out vec3 vWorldPos;\n"
      "out vec3 vNormal;\n"
      "out vec2 vUv;\n"
      "out vec3 vInstanceColor;\n"
      "void main() {\n"
      "   mat4 model = (uInstanced == 1) ? aInstance : uModel;\n"
      "   vec4 world = model * vec4(aPos, 1.0);\n"
      "   vWorldPos = world.xyz;\n"
      "   vNormal = (uInstanced == 1)\n"
      "      ? normalize(mat3(model) * aNormal)\n"
      "      : normalize(uNormalMatrix * aNormal);\n"
      "   vUv = aUv;\n"
      // White when there is no per-instance colour, so the fragment shader can
      // multiply unconditionally rather than branching on it.
      "   vInstanceColor = (uInstanceColored == 1) ? aInstanceColor : vec3(1.0);\n"
      "   vLightSpacePos = uLightViewProj * world;\n"
      "   gl_Position = uViewProj * world;\n"
      "}\n";

   const char* kFragSrc =
      "#version 150\n"
      "in vec3 vWorldPos;\n"
      "in vec3 vNormal;\n"
      "in vec2 vUv;\n"
      "in vec3 vInstanceColor;\n"
      "in vec4 vLightSpacePos;\n"
      "out vec4 fragColor;\n"
      "uniform vec3 uBaseColor;\n"
      "uniform float uMetallic;\n"
      "uniform float uRoughness;\n"
      "uniform float uOpacity;\n"
      "uniform int uShading;\n"
      "uniform vec3 uLightDir[3];\n"
      "uniform vec3 uLightColor[3];\n"
      "uniform float uLightIntensity[3];\n"
      "uniform int uLightType[3];\n"
      "uniform int uLightCount;\n"
      "uniform vec3 uAmbient;\n"
      "uniform float uRim;\n"
      "uniform vec3 uCamPos;\n"
      "uniform sampler2D uTexture;\n"
      "uniform int uHasTexture;\n"
      "uniform sampler2D uRoughnessMap;\n"
      "uniform sampler2D uMetallicMap;\n"
      "uniform sampler2D uNormalMap;\n"
      "uniform sampler2D uAoMap;\n"
      "uniform int uHasRoughnessMap;\n"
      "uniform int uHasMetallicMap;\n"
      "uniform int uHasNormalMap;\n"
      "uniform int uHasAoMap;\n"
      "uniform float uNormalStrength;\n"
      "uniform float uExposure;\n"
      "uniform int uTonemap;\n"
      "uniform vec3 uEmissionColor;\n"
      "uniform float uEmission;\n"
      "uniform vec3 uEnvSky;\n"
      "uniform vec3 uEnvHorizon;\n"
      "uniform vec3 uEnvGround;\n"
      "uniform float uEnvIntensity;\n"
      "uniform sampler2DShadow uShadowMap;\n"
      "uniform int uShadowsOn;\n"
      "uniform float uShadowBias;\n"
      "uniform float uShadowSoftness;\n"
      "uniform float uShadowStrength;\n"
      "uniform float uShadowTexel;\n"
      "\n"
      // 3x3 percentage-closer filter. A single depth comparison gives a hard,
      // stair-stepped edge at any shadow map resolution; averaging several
      // comparisons around the sample turns that into a gradient.
      //
      // sampler2DShadow does the compare in hardware, so each tap is one
      // instruction rather than a fetch plus a branch.
      "float shadowFactor(vec3 normal, vec3 lightDir) {\n"
      "   if (uShadowsOn == 0) return 1.0;\n"
      "   vec3 proj = vLightSpacePos.xyz / max(vLightSpacePos.w, 1e-6);\n"
      "   proj = proj * 0.5 + 0.5;\n"
      // Outside the shadow volume there is no information, so treat it as lit
      // rather than shadowing everything beyond the fitted box.
      "   if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)\n"
      "      return 1.0;\n"
      // Slope-scaled bias: a surface at a grazing angle to the light spans more
      // depth per texel, and a constant bias either acnes those or peters the
      // face-on ones off their own contact shadow.
      "   float slope = 1.0 - max(dot(normal, lightDir), 0.0);\n"
      "   float bias = uShadowBias * (1.0 + slope * 4.0);\n"
      "   float lit = 0.0;\n"
      "   for (int x = -1; x <= 1; x++) {\n"
      "      for (int y = -1; y <= 1; y++) {\n"
      "         vec2 offset = vec2(float(x), float(y)) * uShadowTexel * uShadowSoftness;\n"
      "         lit += texture(uShadowMap, vec3(proj.xy + offset, proj.z - bias));\n"
      "      }\n"
      "   }\n"
      "   lit /= 9.0;\n"
      "   return mix(1.0, lit, uShadowStrength);\n"
      "}\n"
      "\n"
      // Colours arrive from the UI in sRGB. Lighting has to happen in linear
      // space or every sum and product is being done on gamma-encoded numbers,
      // which is what makes falloff read as too dark in the mids and blow out
      // abruptly at the top.
      "vec3 toLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }\n"
      "vec3 toSrgb(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }\n"
      "\n"
      // Narkowicz's ACES fit: cheap, and it rolls highlights off instead of
      // clipping them flat the way a bare clamp does.
      "vec3 acesFilm(vec3 x) {\n"
      "   return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);\n"
      "}\n"
      "\n"
      // Ordered dither, one 8-bit step wide, applied just before the frame is
      // quantised. Smooth shading across a large surface otherwise lands in
      // visible bands, and this scatters the rounding instead.
      "float ditherOffset() {\n"
      "   vec2 p = floor(gl_FragCoord.xy);\n"
      "   float v = fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);\n"
      "   return (v - 0.5) / 255.0;\n"
      "}\n"
      "\n"
      // A three-stop vertical gradient standing in for a sky. Metal with nothing
      // to reflect reads as flat grey plastic no matter how good the BRDF is,
      // and this is the cheapest thing that gives it something. Roughness fades
      // the sample toward the horizon colour, which approximates a blurred
      // reflection without any mip chain or IBL precompute.
      "vec3 sampleEnv(vec3 dir, float rough) {\n"
      "   float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);\n"
      "   vec3 lower = mix(toLinear(uEnvGround), toLinear(uEnvHorizon), smoothstep(0.0, 0.5, t));\n"
      "   vec3 env = mix(lower, toLinear(uEnvSky), smoothstep(0.5, 1.0, t));\n"
      "   return mix(env, toLinear(uEnvHorizon), rough * rough) * uEnvIntensity;\n"
      "}\n"
      "\n"
      // Cook-Torrance: GGX/Trowbridge-Reitz distribution, Smith height-
      // correlated geometry via Schlick-GGX, Schlick Fresnel.
      "float distributionGGX(float nDotH, float rough) {\n"
      "   float a = rough * rough;\n"
      "   float a2 = a * a;\n"
      "   float d = nDotH * nDotH * (a2 - 1.0) + 1.0;\n"
      "   return a2 / max(3.14159265 * d * d, 1e-7);\n"
      "}\n"
      "float geometrySchlickGGX(float nDotV, float rough) {\n"
      // Direct-lighting remap of k; image-based lighting uses a different one.
      "   float r = rough + 1.0;\n"
      "   float k = (r * r) / 8.0;\n"
      "   return nDotV / (nDotV * (1.0 - k) + k);\n"
      "}\n"
      "vec3 fresnelSchlick(float cosTheta, vec3 f0) {\n"
      "   return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
      "}\n"
      // Tangent frame derived from screen-space derivatives rather than from
      // vertex tangents. The Vertex struct carries only position, normal and UV,
      // and adding tangents would mean regenerating them in every primitive and
      // every mesh operator; this needs nothing but the UVs already present.
      "vec3 applyNormalMap(vec3 normal, vec3 viewDir, vec2 uv) {\n"
      "   vec3 dp1 = dFdx(vWorldPos);\n"
      "   vec3 dp2 = dFdy(vWorldPos);\n"
      "   vec2 duv1 = dFdx(uv);\n"
      "   vec2 duv2 = dFdy(uv);\n"
      "   vec3 dp2perp = cross(dp2, normal);\n"
      "   vec3 dp1perp = cross(normal, dp1);\n"
      "   vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;\n"
      "   vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;\n"
      // A face with degenerate UVs gives a zero-length frame; falling back to
      // the geometric normal is better than normalising a zero vector.
      "   float maxLen = max(dot(tangent, tangent), dot(bitangent, bitangent));\n"
      "   if (maxLen < 1e-12) return normal;\n"
      "   float invMax = inversesqrt(maxLen);\n"
      "   mat3 tbn = mat3(tangent * invMax, bitangent * invMax, normal);\n"
      "   vec3 sampled = texture(uNormalMap, uv).rgb * 2.0 - 1.0;\n"
      "   sampled.xy *= uNormalStrength;\n"
      "   return normalize(tbn * sampled);\n"
      "}\n"
      "\n"
      "vec3 fresnelSchlickRoughness(float cosTheta, vec3 f0, float rough) {\n"
      // Rough surfaces should not develop a razor-thin mirror rim at grazing
      // angles the way the plain Schlick term would give them.
      "   vec3 ceiling = max(vec3(1.0 - rough), f0);\n"
      "   return f0 + (ceiling - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
      "}\n"
      "\n"
      "void main() {\n"
      "   vec3 n = normalize(vNormal);\n"
      // Normals and UVs are data being visualised, not light - they must not be
      // tonemapped or gamma-encoded on the way out.
      "   if (uShading == 1) { fragColor = vec4(n * 0.5 + 0.5, uOpacity); return; }\n"
      "   if (uShading == 2) { fragColor = vec4(vUv, 0.0, uOpacity); return; }\n"
      "\n"
      "   vec3 base = toLinear(uBaseColor) * vInstanceColor;\n"
      "   if (uHasTexture == 1) base *= toLinear(texture(uTexture, vUv).rgb);\n"
      "   if (uShading == 3) { fragColor = vec4(toSrgb(base), uOpacity); return; }\n"
      "\n"
      "   vec3 viewDir = normalize(uCamPos - vWorldPos);\n"
      // Maps multiply the slider rather than replacing it, so the slider stays
      // the overall level and the map is the variation across the surface.
      "   float rough = uRoughness;\n"
      "   if (uHasRoughnessMap == 1) rough *= texture(uRoughnessMap, vUv).r;\n"
      "   rough = clamp(rough, 0.045, 1.0);\n"
      "   float metal = uMetallic;\n"
      "   if (uHasMetallicMap == 1) metal *= texture(uMetallicMap, vUv).r;\n"
      "   metal = clamp(metal, 0.0, 1.0);\n"
      "   if (uHasNormalMap == 1) n = applyNormalMap(n, viewDir, vUv);\n"
      "   float ao = (uHasAoMap == 1) ? texture(uAoMap, vUv).r : 1.0;\n"
      "   float nDotV = max(dot(n, viewDir), 1e-4);\n"
      // Dielectrics reflect ~4% head-on; metals reflect their own albedo and
      // have no diffuse lobe at all.
      "   vec3 f0 = mix(vec3(0.04), base, metal);\n"
      "   vec3 col = vec3(0.0);\n"
      "\n"
      "   for (int i = 0; i < 3; i++) {\n"
      "      if (i >= uLightCount) break;\n"
      // Type 3 is an ambient fill: it has no direction at all, so it skips the
      // whole BRDF and simply lifts the surface. Handled before anything that
      // needs a light vector.
      "      if (uLightType[i] == 3) {\n"
      "         col += base * (1.0 - metal) * toLinear(uLightColor[i]) * uLightIntensity[i];\n"
      "         continue;\n"
      "      }\n"
      "      vec3 lightDir;\n"
      "      float attenuation = 1.0;\n"
      "      if (uLightType[i] == 1) {\n"
      "         vec3 toLight = uLightDir[i] - vWorldPos;\n"
      "         float dist = length(toLight);\n"
      "         lightDir = toLight / max(dist, 1e-4);\n"
      "         attenuation = 1.0 / (1.0 + dist * dist * 0.25);\n"
      "      } else {\n"
      "         lightDir = normalize(uLightDir[i]);\n"
      "      }\n"
      "      vec3 halfway = normalize(lightDir + viewDir);\n"
      // A sun is a large angular source, not a point at infinity, so its
      // terminator is soft. Wrapping the diffuse term is the cheap stand-in for
      // that; without it a "sun" is indistinguishable from a directional light.
      "      float nDotL;\n"
      "      if (uLightType[i] == 2) {\n"
      "         const float wrap = 0.35;\n"
      "         nDotL = max((dot(n, lightDir) + wrap) / (1.0 + wrap), 0.0);\n"
      "      } else {\n"
      "         nDotL = max(dot(n, lightDir), 0.0);\n"
      "      }\n"
      "      if (nDotL <= 0.0) continue;\n"
      "      float nDotH = max(dot(n, halfway), 0.0);\n"
      "\n"
      "      float d = distributionGGX(nDotH, rough);\n"
      "      float g = geometrySchlickGGX(nDotV, rough) * geometrySchlickGGX(nDotL, rough);\n"
      "      vec3 f = fresnelSchlick(max(dot(halfway, viewDir), 0.0), f0);\n"
      "      vec3 specular = (d * g * f) / max(4.0 * nDotV * nDotL, 1e-7);\n"
      // Whatever is not reflected is refracted and available to scatter, so the
      // two lobes together never return more energy than arrived.
      "      vec3 kD = (vec3(1.0) - f) * (1.0 - metal);\n"
      "      vec3 radiance = toLinear(uLightColor[i]) * uLightIntensity[i] * attenuation;\n"
      // Only light 0 casts: one depth map is rendered, from that light.
      "      float shadow = (i == 0) ? shadowFactor(n, lightDir) : 1.0;\n"
      "      col += (kD * base / 3.14159265 + specular) * radiance * nDotL * shadow;\n"
      "   }\n"
      "\n"
      // Ambient, from the environment rather than a flat constant: the diffuse
      // half samples straight up the normal, the specular half up the mirror
      // direction. uAmbient stays as a tint so old patches still respond to it.
      "   vec3 reflectDir = reflect(-viewDir, n);\n"
      "   vec3 irradiance = sampleEnv(n, 1.0) * toLinear(uAmbient) * 3.0;\n"
      "   vec3 reflection = sampleEnv(reflectDir, rough);\n"
      "   vec3 fAmbient = fresnelSchlickRoughness(nDotV, f0, rough);\n"
      // Ambient occlusion darkens only the ambient and environment terms - it
      // describes light that cannot reach a crevice from the surroundings, not
      // light blocked from a specific lamp, which is what shadows are for.
      "   col += (vec3(1.0) - fAmbient) * (1.0 - metal) * base * irradiance * ao;\n"
      "   col += reflection * fAmbient * ao;\n"
      "\n"
      "   col += base * pow(1.0 - nDotV, 3.0) * uRim;\n"
      "   col += toLinear(uEmissionColor) * uEmission;\n"
      "\n"
      "   col *= uExposure;\n"
      "   if (uTonemap == 1) col = acesFilm(col);\n"
      "   else if (uTonemap == 2) col = col / (col + vec3(1.0));\n"
      "   col = toSrgb(col) + ditherOffset();\n"
      "   fragColor = vec4(col, uOpacity);\n"
      "}\n";
}

// ================================================================ Geometry

const std::vector<std::string>& GeometryNode::ShapeNames() { return kShapeNames; }
const std::vector<std::string>& GeometryNode::ShadingNames() { return kShadingNames; }

GeometryNode::~GeometryNode()
{
   GLUtil::DestroyFbo(mPreview);
}

void GeometryNode::RebuildIfNeeded()
{
   if (shape == mBuiltShape && detail == mBuiltDetail && sides == mBuiltSides &&
       knotP == mBuiltP && knotQ == mBuiltQ && tubeRadius == mBuiltTube &&
       bevel == mBuiltBevel && bevelSegments == mBuiltBevelSegments &&
       superN2 == mBuiltN2 && superN3 == mBuiltN3 &&
       superP2 == mBuiltP2 && superP3 == mBuiltP3 && !mMesh.Empty())
      return;

   switch (shape)
   {
      case 0: mMesh = Primitives::Plane(detail); break;
      case 1: mMesh = Primitives::Cube(std::max(1, detail / 8)); break;
      case 2: mMesh = Primitives::Sphere(detail, sides * 2); break;
      case 3: mMesh = Primitives::Icosphere(std::max(0, std::min(4, detail / 8))); break;
      case 4: mMesh = Primitives::Torus(detail, sides, tubeRadius); break;
      case 5: mMesh = Primitives::Cylinder(sides, std::max(1, detail / 8), 1.0f); break;
      case 6: mMesh = Primitives::Cone(sides, std::max(1, detail / 8)); break;
      case 7: mMesh = Primitives::TorusKnot(detail * 8, sides, tubeRadius, knotP, knotQ); break;
      case 8: mMesh = Primitives::Capsule(detail, sides * 2, tubeRadius); break;
      case 9: mMesh = Primitives::Tube(sides, std::max(1, detail / 8), tubeRadius); break;
      case 10: mMesh = Primitives::Pyramid(sides); break;
      case 11: mMesh = Primitives::Prism(sides, std::max(1, detail / 8)); break;
      case 12: mMesh = Primitives::Helix(detail * 8, sides, tubeRadius * 0.4f,
                                         (float)knotP, (float)knotQ * 0.5f); break;
      default: mMesh = Primitives::Supershape(detail, sides * 2,
                                              (float)knotP, 1.0f, superN2, superN3,
                                              (float)knotQ, 1.0f, superP2, superP3); break;
   }

   mBuiltShape = shape;
   mBuiltDetail = detail;
   mBuiltSides = sides;
   mBuiltP = knotP;
   mBuiltQ = knotQ;
   if (bevel > 0.0f)
      mMesh = MeshOps::Bevel(mMesh, bevel, bevelSegments);

   mBuiltTube = tubeRadius;
   mBuiltBevel = bevel;
   mBuiltBevelSegments = bevelSegments;
   mBuiltN2 = superN2; mBuiltN3 = superN3;
   mBuiltP2 = superP2; mBuiltP3 = superP3;
   mMeshRevision = NextMeshRevision();
}

const Mesh& GeometryNode::GetMesh()
{
   RebuildIfNeeded();
   return mMesh;
}

unsigned long long GeometryNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Mat4 GeometryNode::GetModelMatrix() const
{
   const float spin = spinY * (float)Transport::Instance().Beats();
   Mat4 m = Mat4::Scale(scaleX * uniformScale, scaleY * uniformScale, scaleZ * uniformScale);
   m = Mat4::Multiply(Mat4::RotationZ(rotZ), m);
   m = Mat4::Multiply(Mat4::RotationY(rotY + spin), m);
   m = Mat4::Multiply(Mat4::RotationX(rotX), m);
   m = Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
   return m;
}

Material GeometryNode::GetMaterial() const
{
   Material m;
   m.color[0] = color[0]; m.color[1] = color[1]; m.color[2] = color[2];
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   m.shading = shading;
   m.emissionColor[0] = emissionColor[0];
   m.emissionColor[1] = emissionColor[1];
   m.emissionColor[2] = emissionColor[2];
   m.emission = emission;
   return m;
}

unsigned int GeometryNode::GetSurfaceTexture()
{
   return mTextureInput.IsConnected() && mTextureInput.GetSource()
             ? mTextureInput.GetSource()->GetOutputTexture()
             : 0;
}

unsigned int GeometryNode::GetOutputTexture()
{
   return GLUtil::FboTexture(mPreview);
}

void GeometryNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   RebuildIfNeeded();
   if (mTextureInput.IsConnected())
      mTextureInput.Pull(frameId);
   // The node's own preview is drawn by the editor using a shared solo renderer;
   // geometry itself produces no image.
}

// ================================================================= Render

const std::vector<std::string>& Render3DNode::ProjectionNames() { return kProjectionNames; }
const std::vector<std::string>& Render3DNode::SampleNames() { return kSampleNames; }
const std::vector<std::string>& Render3DNode::TonemapNames() { return kTonemapNames; }
const std::vector<std::string>& Render3DNode::ShadowQualityNames() { return kShadowQualityNames; }

void Render3DNode::ReleaseShadowTargets()
{
   if (mShadowTex != 0) { glDeleteTextures(1, &mShadowTex); mShadowTex = 0; }
   if (mShadowFbo != 0) { glDeleteFramebuffers(1, &mShadowFbo); mShadowFbo = 0; }
   mShadowSize = 0;
}

bool Render3DNode::EnsureShadowShader()
{
   if (mShadowShaderTried)
      return mShadowProgram != 0;
   mShadowShaderTried = true;

   auto compile = [](GLenum type, const char* src) -> unsigned int {
      unsigned int shader = glCreateShader(type);
      glShaderSource(shader, 1, &src, nullptr);
      glCompileShader(shader);
      GLint ok = 0;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
      if (!ok)
      {
         char log[1024];
         glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
         fprintf(stderr, "Shadow shader error: %s\n", log);
         glDeleteShader(shader);
         return 0u;
      }
      return shader;
   };

   const unsigned int vert = compile(GL_VERTEX_SHADER, kShadowVertSrc);
   const unsigned int frag = compile(GL_FRAGMENT_SHADER, kShadowFragSrc);
   if (vert == 0 || frag == 0)
      return false;

   mShadowProgram = glCreateProgram();
   // Same attribute slots as the main program, so one VAO serves both passes.
   glBindAttribLocation(mShadowProgram, 0, "aPos");
   glBindAttribLocation(mShadowProgram, 3, "aInstance");
   glAttachShader(mShadowProgram, vert);
   glAttachShader(mShadowProgram, frag);
   glLinkProgram(mShadowProgram);
   glDeleteShader(vert);
   glDeleteShader(frag);

   GLint linked = 0;
   glGetProgramiv(mShadowProgram, GL_LINK_STATUS, &linked);
   if (!linked)
   {
      char log[1024];
      glGetProgramInfoLog(mShadowProgram, sizeof(log), nullptr, log);
      fprintf(stderr, "Shadow link error: %s\n", log);
      glDeleteProgram(mShadowProgram);
      mShadowProgram = 0;
      return false;
   }
   return true;
}

bool Render3DNode::EnsureShadowResources(int size)
{
   if (mShadowFbo != 0 && mShadowSize == size)
      return true;
   ReleaseShadowTargets();

   glGenTextures(1, &mShadowTex);
   glBindTexture(GL_TEXTURE_2D, mShadowTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
                GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   // Clamped to a border of 1.0 (fully lit) so geometry outside the shadow
   // volume is not shadowed by the edge texel repeating.
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
   const float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
   glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
   // Hardware depth comparison, which is what makes each PCF tap a single
   // instruction instead of a fetch and a branch.
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

   glGenFramebuffers(1, &mShadowFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mShadowFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mShadowTex, 0);
   // Depth only: no colour attachment at all.
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);

   const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);
   if (status != GL_FRAMEBUFFER_COMPLETE)
   {
      fprintf(stderr, "Shadow framebuffer incomplete: 0x%x\n", status);
      ReleaseShadowTargets();
      return false;
   }

   mShadowSize = size;
   return true;
}

bool Render3DNode::SceneBounds(float outLo[3], float outHi[3])
{
   outLo[0] = outLo[1] = outLo[2] = 1e30f;
   outHi[0] = outHi[1] = outHi[2] = -1e30f;
   bool any = false;

   for (int i = 0; i < kSlots; i++)
   {
      if (geometry[i] == nullptr || !mGpu[i].hasBounds)
         continue;
      const Mat4 model = geometry[i]->GetModelMatrix();
      // The eight corners through the matrix: a rotated box's true extent
      // cannot be had from transforming the min and max alone.
      for (int corner = 0; corner < 8; corner++)
      {
         const float c[3] = {
            (corner & 1) ? mGpu[i].hi[0] : mGpu[i].lo[0],
            (corner & 2) ? mGpu[i].hi[1] : mGpu[i].lo[1],
            (corner & 4) ? mGpu[i].hi[2] : mGpu[i].lo[2]
         };
         for (int k = 0; k < 3; k++)
         {
            const float w = model.m[k] * c[0] + model.m[4 + k] * c[1] +
                            model.m[8 + k] * c[2] + model.m[12 + k];
            outLo[k] = std::min(outLo[k], w);
            outHi[k] = std::max(outHi[k], w);
         }
      }
      any = true;
   }
   return any;
}

void Render3DNode::ReleaseTargets()
{
   if (mColorTex != 0) { glDeleteTextures(1, &mColorTex); mColorTex = 0; }
   if (mDepthBuffer != 0) { glDeleteRenderbuffers(1, &mDepthBuffer); mDepthBuffer = 0; }
   if (mFbo != 0) { glDeleteFramebuffers(1, &mFbo); mFbo = 0; }
   if (mMsColor != 0) { glDeleteRenderbuffers(1, &mMsColor); mMsColor = 0; }
   if (mMsDepth != 0) { glDeleteRenderbuffers(1, &mMsDepth); mMsDepth = 0; }
   if (mMsFbo != 0) { glDeleteFramebuffers(1, &mMsFbo); mMsFbo = 0; }
}

Render3DNode::~Render3DNode()
{
   ReleaseTargets();
   ReleaseShadowTargets();
   for (int i = 0; i < kSlots; i++)
      ReleaseGpuMesh(mGpu[i]);
   if (mProgram != 0) glDeleteProgram(mProgram);
   if (mShadowProgram != 0) glDeleteProgram(mShadowProgram);
}

void Render3DNode::ReleaseGpuMesh(GpuMesh& gpu)
{
   if (gpu.vbo != 0) glDeleteBuffers(1, &gpu.vbo);
   if (gpu.ibo != 0) glDeleteBuffers(1, &gpu.ibo);
   if (gpu.instanceVbo != 0) glDeleteBuffers(1, &gpu.instanceVbo);
   if (gpu.instanceColorVbo != 0) glDeleteBuffers(1, &gpu.instanceColorVbo);
   if (gpu.vao != 0) glDeleteVertexArrays(1, &gpu.vao);
   gpu = GpuMesh();
}

bool Render3DNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;

   auto compile = [](GLenum type, const char* src) -> unsigned int {
      unsigned int shader = glCreateShader(type);
      glShaderSource(shader, 1, &src, nullptr);
      glCompileShader(shader);
      GLint ok = 0;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
      if (!ok)
      {
         char log[1024];
         glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
         fprintf(stderr, "Render3D shader error: %s\n", log);
         glDeleteShader(shader);
         return 0u;
      }
      return shader;
   };

   const unsigned int vert = compile(GL_VERTEX_SHADER, kVertSrc);
   const unsigned int frag = compile(GL_FRAGMENT_SHADER, kFragSrc);
   if (vert == 0 || frag == 0)
      return false;

   mProgram = glCreateProgram();
   glBindAttribLocation(mProgram, 0, "aPos");
   glBindAttribLocation(mProgram, 1, "aNormal");
   glBindAttribLocation(mProgram, 2, "aUv");
   glBindAttribLocation(mProgram, 3, "aInstance"); // occupies locations 3..6
   glBindAttribLocation(mProgram, 7, "aInstanceColor");
   glAttachShader(mProgram, vert);
   glAttachShader(mProgram, frag);
   glLinkProgram(mProgram);
   glDeleteShader(vert);
   glDeleteShader(frag);

   GLint linked = 0;
   glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
   if (!linked)
   {
      char log[1024];
      glGetProgramInfoLog(mProgram, sizeof(log), nullptr, log);
      fprintf(stderr, "Render3D link error: %s\n", log);
      glDeleteProgram(mProgram);
      mProgram = 0;
      return false;
   }

   return true;
}

bool Render3DNode::EnsureResources(int w, int h, int sampleCount)
{
   if (mFbo != 0 && mWidth == w && mHeight == h && mActiveSamples == sampleCount)
      return true;

   ReleaseTargets();

   glGenTextures(1, &mColorTex);
   glBindTexture(GL_TEXTURE_2D, mColorTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

   // A depth renderbuffer is the piece the 2D pipeline never needed: without it
   // triangles composite in draw order instead of by distance.
   glGenRenderbuffers(1, &mDepthBuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, mDepthBuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

   glGenFramebuffers(1, &mFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mColorTex, 0);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthBuffer);

   GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);

   if (status != GL_FRAMEBUFFER_COMPLETE)
   {
      fprintf(stderr, "Render3D framebuffer incomplete: 0x%x\n", status);
      return false;
   }

   // Multisampled twin of the above. The scene is drawn here and then resolved
   // into mColorTex, which is what GetOutputTexture() hands downstream - so the
   // 2D graph is unaware any of this happened.
   int resolved = 0;
   if (sampleCount > 1)
   {
      GLint maxSamples = 0;
      glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
      resolved = std::min(sampleCount, (int)maxSamples);

      // Multisampled storage is colour + depth at every sample, so it grows with
      // both area and sample count: 4096 square at 8x is over a gigabyte of
      // renderbuffer. Halve the sample count until it fits a budget rather than
      // letting a big export try to allocate more than the GPU will give.
      const double bytesPerSample = (double)w * (double)h * 8.0;
      const double budget = 512.0 * 1024.0 * 1024.0;
      while (resolved > 1 && bytesPerSample * resolved > budget)
         resolved /= 2;
      if (resolved < 2)
         resolved = 0;
   }

   if (resolved > 1)
   {
      glGenRenderbuffers(1, &mMsColor);
      glBindRenderbuffer(GL_RENDERBUFFER, mMsColor);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, resolved, GL_RGBA8, w, h);

      glGenRenderbuffers(1, &mMsDepth);
      glBindRenderbuffer(GL_RENDERBUFFER, mMsDepth);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, resolved, GL_DEPTH_COMPONENT24, w, h);

      glGenFramebuffers(1, &mMsFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, mMsFbo);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, mMsColor);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mMsDepth);

      status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindRenderbuffer(GL_RENDERBUFFER, 0);

      if (status != GL_FRAMEBUFFER_COMPLETE)
      {
         // Fall back to drawing straight into the resolve target rather than
         // failing the whole render: aliased output beats no output.
         fprintf(stderr, "Render3D multisample framebuffer incomplete: 0x%x, falling back\n", status);
         if (mMsColor != 0) { glDeleteRenderbuffers(1, &mMsColor); mMsColor = 0; }
         if (mMsDepth != 0) { glDeleteRenderbuffers(1, &mMsDepth); mMsDepth = 0; }
         if (mMsFbo != 0) { glDeleteFramebuffers(1, &mMsFbo); mMsFbo = 0; }
         resolved = 0;
      }
   }

   mActiveSamples = resolved;
   mWidth = w;
   mHeight = h;
   return true;
}

void Render3DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!EnsureShader())
      return;

   const int w = std::max(16, (int)width);
   const int h = std::max(16, (int)height);
   const int wantSamples =
      kSampleCounts[std::max(0, std::min(samples, (int)(sizeof(kSampleCounts) / sizeof(int)) - 1))];
   if (!EnsureResources(w, h, wantSamples))
      return;

   // Pull any textures the geometry wants before we start drawing.
   for (int i = 0; i < kSlots; i++)
   {
      if (auto* node = dynamic_cast<INode*>(geometry[i]))
         node->CookIfNeeded(frameId);
   }

   const float aspect = (float)w / (float)h;

   float eye[3];
   Mat4 view, proj;
   if (camera != nullptr)
   {
      camera->ComputeEye(eye);
      view = camera->ViewMatrix();
      proj = camera->ProjectionMatrix(aspect);
   }
   else
   {
      const float ce = std::cos(camElevation);
      eye[0] = targetX + camDistance * ce * std::cos(camAzimuth);
      eye[1] = targetY + camDistance * std::sin(camElevation);
      eye[2] = targetZ + camDistance * ce * std::sin(camAzimuth);
      const float target[3] = { targetX, targetY, targetZ };
      const float up[3] = { 0.0f, 1.0f, 0.0f };
      view = Mat4::LookAt(eye, target, up);
      proj = (projection == 1)
         ? Mat4::Orthographic(orthoHeight, aspect, nearPlane, farPlane)
         : Mat4::Perspective(fov * 3.14159265f / 180.0f, aspect, nearPlane, farPlane);
   }
   const Mat4 viewProj = Mat4::Multiply(proj, view);

   // Gather lights: patched Light nodes win, otherwise the built-in one.
   float lightDirs[kLightSlots * 3] = { 0 };
   float lightCols[kLightSlots * 3] = { 0 };
   float lightPower[kLightSlots] = { 0 };
   int lightTypes[kLightSlots] = { 0 };
   int lightCount = 0;
   for (int i = 0; i < kLightSlots; i++)
   {
      if (lights[i] == nullptr)
         continue;
      float vec[3];
      lights[i]->ComputeVector(vec);
      lightDirs[lightCount * 3 + 0] = vec[0];
      lightDirs[lightCount * 3 + 1] = vec[1];
      lightDirs[lightCount * 3 + 2] = vec[2];
      lightCols[lightCount * 3 + 0] = lights[i]->color[0];
      lightCols[lightCount * 3 + 1] = lights[i]->color[1];
      lightCols[lightCount * 3 + 2] = lights[i]->color[2];
      lightPower[lightCount] = lights[i]->intensity;
      lightTypes[lightCount] = lights[i]->type;
      lightCount++;
   }
   if (lightCount == 0)
   {
      const float le = std::cos(lightElevation);
      lightDirs[0] = le * std::cos(lightAzimuth);
      lightDirs[1] = std::sin(lightElevation);
      lightDirs[2] = le * std::sin(lightAzimuth);
      lightCols[0] = lightColor[0]; lightCols[1] = lightColor[1]; lightCols[2] = lightColor[2];
      lightPower[0] = lightIntensity;
      lightTypes[0] = 0;
      lightCount = 1;
   }

   // --- shadow pass ---------------------------------------------------
   // Depth from the light's point of view, rendered before the main pass and
   // sampled by it. Only the first light casts, and only when it has a
   // direction: a point light would need a depth cube map and six passes.
   bool shadowsActive = false;
   if (shadowsEnabled && lightTypes[0] != 1 && EnsureShadowShader())
   {
      const int wantSize = kShadowSizes[std::max(0, std::min(shadowQuality, 2))];
      float sceneLo[3], sceneHi[3];
      if (EnsureShadowResources(wantSize) && SceneBounds(sceneLo, sceneHi))
      {
         const float centre[3] = { (sceneLo[0]+sceneHi[0])*0.5f,
                                   (sceneLo[1]+sceneHi[1])*0.5f,
                                   (sceneLo[2]+sceneHi[2])*0.5f };
         const float radius = std::max(0.01f,
            0.5f * std::sqrt((sceneHi[0]-sceneLo[0])*(sceneHi[0]-sceneLo[0]) +
                             (sceneHi[1]-sceneLo[1])*(sceneHi[1]-sceneLo[1]) +
                             (sceneHi[2]-sceneLo[2])*(sceneHi[2]-sceneLo[2])));

         // The light sits outside the bounding sphere looking at its centre, and
         // the ortho box is fitted to that sphere. Fitting to the scene rather
         // than using a fixed volume is what keeps texel density usable whether
         // the scene is a centimetre or a hundred units across.
         float dir[3] = { lightDirs[0], lightDirs[1], lightDirs[2] };
         const float dirLen = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
         if (dirLen > 1e-5f) { dir[0] /= dirLen; dir[1] /= dirLen; dir[2] /= dirLen; }
         else { dir[0] = 0.0f; dir[1] = 1.0f; dir[2] = 0.0f; }

         const float eye[3] = { centre[0] + dir[0] * radius * 2.5f,
                                centre[1] + dir[1] * radius * 2.5f,
                                centre[2] + dir[2] * radius * 2.5f };
         // A light pointing straight down would make the view matrix's up
         // vector parallel to its direction, which degenerates.
         // A light pointing straight down would make the view matrix's up
         // vector parallel to its direction, which degenerates.
         const float upY[3] = { 0.0f, 1.0f, 0.0f };
         const float upZ[3] = { 0.0f, 0.0f, 1.0f };
         const Mat4 lightView =
            Mat4::LookAt(eye, centre, (std::fabs(dir[1]) > 0.99f) ? upZ : upY);
         const Mat4 lightProj = Mat4::Orthographic(radius * 1.2f, 1.0f,
                                                   0.05f, radius * 5.0f);
         mLightViewProj = Mat4::Multiply(lightProj, lightView);

         GLint prevFboShadow = 0, prevViewportShadow[4];
         glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFboShadow);
         glGetIntegerv(GL_VIEWPORT, prevViewportShadow);
         const GLboolean prevScissorShadow = glIsEnabled(GL_SCISSOR_TEST);
         glDisable(GL_SCISSOR_TEST);

         glBindFramebuffer(GL_FRAMEBUFFER, mShadowFbo);
         glViewport(0, 0, mShadowSize, mShadowSize);
         glClearDepth(1.0);
         glClear(GL_DEPTH_BUFFER_BIT);
         glEnable(GL_DEPTH_TEST);
         glDepthFunc(GL_LESS);
         glDepthMask(GL_TRUE);
         // Front faces culled so the depth written is the back of each object.
         // Peter-panning is easier to bias away than the acne you get otherwise.
         glEnable(GL_CULL_FACE);
         glCullFace(GL_FRONT);

         glUseProgram(mShadowProgram);
         glUniformMatrix4fv(glGetUniformLocation(mShadowProgram, "uLightViewProj"),
                            1, GL_FALSE, mLightViewProj.m);

         for (int i = 0; i < kSlots; i++)
         {
            IGeometrySource* source = geometry[i];
            if (source == nullptr || mGpu[i].vao == 0 || mGpu[i].indexCount == 0)
               continue;
            glBindVertexArray(mGpu[i].vao);
            const Mat4 model = source->GetModelMatrix();
            auto* instancer = dynamic_cast<InstanceOnPointsNode*>(source);
            const bool instanced = instancer != nullptr && mGpu[i].instanceCount > 0 &&
                                   mGpu[i].instanceAttribsOn;
            glUniformMatrix4fv(glGetUniformLocation(mShadowProgram, "uModel"), 1, GL_FALSE, model.m);
            glUniform1i(glGetUniformLocation(mShadowProgram, "uInstanced"), instanced ? 1 : 0);
            if (instanced)
               glDrawElementsInstanced(GL_TRIANGLES, mGpu[i].indexCount, GL_UNSIGNED_INT,
                                       nullptr, (GLsizei)mGpu[i].instanceCount);
            else
               glDrawElements(GL_TRIANGLES, mGpu[i].indexCount, GL_UNSIGNED_INT, nullptr);
         }

         glBindVertexArray(0);
         glCullFace(GL_BACK);
         glBindFramebuffer(GL_FRAMEBUFFER, prevFboShadow);
         glViewport(prevViewportShadow[0], prevViewportShadow[1],
                    prevViewportShadow[2], prevViewportShadow[3]);
         if (prevScissorShadow)
            glEnable(GL_SCISSOR_TEST);
         shadowsActive = true;
      }
   }

   // --- save the GL state the 2D pipeline relies on -------------------
   GLint prevFbo = 0, prevViewport[4];
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glGetIntegerv(GL_VIEWPORT, prevViewport);
   const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
   const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
   const GLboolean prevBlend = glIsEnabled(GL_BLEND);

   // The 2D pipeline and ImGui both leave state behind. Scissor in particular
   // is left enabled with the UI's clip rect, which silently discards every
   // fragment we draw into our own framebuffer.
   const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
   GLint prevScissorBox[4];
   glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_RASTERIZER_DISCARD);
   glDisable(GL_STENCIL_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDepthMask(GL_TRUE);

   const bool multisampling = mActiveSamples > 1 && mMsFbo != 0;
   glBindFramebuffer(GL_FRAMEBUFFER, multisampling ? mMsFbo : mFbo);
   glViewport(0, 0, w, h);
   glClearColor(bgColor[0], bgColor[1], bgColor[2], bgOpacity);
   glClearDepth(1.0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   if (depthTest)
   {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LESS);
   }
   else
   {
      glDisable(GL_DEPTH_TEST);
   }

   if (backfaceCull)
   {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glFrontFace(GL_CCW);
   }
   else
   {
      glDisable(GL_CULL_FACE);
   }

   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   glUseProgram(mProgram);
   mLastTriangles = 0;
   mLastDrawCalls = 0;
   mLastUploads = 0;

   glUniformMatrix4fv(glGetUniformLocation(mProgram, "uViewProj"), 1, GL_FALSE, viewProj.m);
   glUniform3fv(glGetUniformLocation(mProgram, "uLightDir"), kLightSlots, lightDirs);
   glUniform3fv(glGetUniformLocation(mProgram, "uLightColor"), kLightSlots, lightCols);
   glUniform1fv(glGetUniformLocation(mProgram, "uLightIntensity"), kLightSlots, lightPower);
   glUniform1iv(glGetUniformLocation(mProgram, "uLightType"), kLightSlots, lightTypes);
   glUniform1i(glGetUniformLocation(mProgram, "uLightCount"), lightCount);
   glUniform3fv(glGetUniformLocation(mProgram, "uAmbient"), 1, ambientColor);
   glUniform1f(glGetUniformLocation(mProgram, "uRim"), rimIntensity);
   glUniform3fv(glGetUniformLocation(mProgram, "uCamPos"), 1, eye);
   glUniform1f(glGetUniformLocation(mProgram, "uExposure"), exposure);
   glUniform1i(glGetUniformLocation(mProgram, "uTonemap"), tonemap);
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, shadowsActive ? mShadowTex : DummyShadowTexture());
   glUniform1i(glGetUniformLocation(mProgram, "uShadowMap"), 1);
   glUniform1i(glGetUniformLocation(mProgram, "uShadowsOn"), shadowsActive ? 1 : 0);
   glUniform1f(glGetUniformLocation(mProgram, "uShadowBias"), shadowBias);
   glUniform1f(glGetUniformLocation(mProgram, "uShadowSoftness"), shadowSoftness);
   glUniform1f(glGetUniformLocation(mProgram, "uShadowStrength"), shadowStrength);
   glUniform1f(glGetUniformLocation(mProgram, "uShadowTexel"),
               mShadowSize > 0 ? 1.0f / (float)mShadowSize : 0.0f);
   glUniformMatrix4fv(glGetUniformLocation(mProgram, "uLightViewProj"), 1, GL_FALSE,
                      mLightViewProj.m);
   glActiveTexture(GL_TEXTURE0);

   glUniform3fv(glGetUniformLocation(mProgram, "uEnvSky"), 1, envSky);
   glUniform3fv(glGetUniformLocation(mProgram, "uEnvHorizon"), 1, envHorizon);
   glUniform3fv(glGetUniformLocation(mProgram, "uEnvGround"), 1, envGround);
   glUniform1f(glGetUniformLocation(mProgram, "uEnvIntensity"), envIntensity);

   for (int i = 0; i < kSlots; i++)
   {
      IGeometrySource* source = geometry[i];
      if (source == nullptr)
         continue;
      const Mesh& mesh = source->GetMesh();
      if (mesh.Empty())
         continue;

      GpuMesh& gpu = mGpu[i];
      if (gpu.vao == 0)
      {
         glGenVertexArrays(1, &gpu.vao);
         glGenBuffers(1, &gpu.vbo);
         glGenBuffers(1, &gpu.ibo);
      }
      glBindVertexArray(gpu.vao);

      // A slot that swapped to a different node re-uploads even if the stamps
      // happen to line up, and drops any instancing state the old source left.
      const bool sourceChanged = gpu.source != (const void*)source;
      if (sourceChanged)
      {
         gpu.meshRevision = 0;
         gpu.instanceRevision = 0;
         gpu.source = source;
      }

      const unsigned long long revision = source->MeshRevision();
      if (gpu.meshRevision != revision)
      {
         glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
         glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex),
                      mesh.vertices.data(), GL_STATIC_DRAW);
         // The element binding is captured by the VAO, so it survives the frame.
         glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
         glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                      mesh.indices.data(), GL_STATIC_DRAW);

         glEnableVertexAttribArray(0);
         glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
         glEnableVertexAttribArray(1);
         glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
         glEnableVertexAttribArray(2);
         glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));

         // Bounds computed here rather than per frame: this is the one place
         // the vertices are already being walked.
         gpu.lo[0] = gpu.lo[1] = gpu.lo[2] = 1e30f;
         gpu.hi[0] = gpu.hi[1] = gpu.hi[2] = -1e30f;
         for (const Vertex& v : mesh.vertices)
         {
            const float p[3] = { v.px, v.py, v.pz };
            for (int k = 0; k < 3; k++)
            {
               if (!std::isfinite(p[k]))
                  continue;
               gpu.lo[k] = std::min(gpu.lo[k], p[k]);
               gpu.hi[k] = std::max(gpu.hi[k], p[k]);
            }
         }
         gpu.hasBounds = gpu.lo[0] <= gpu.hi[0];

         gpu.meshRevision = revision;
         gpu.indexCount = (int)mesh.indices.size();
         mLastUploads++;
      }

      const Mat4 model = source->GetModelMatrix();
      float normalMatrix[9];
      model.NormalMatrix(normalMatrix);

      const Material material = source->GetMaterial();

      const unsigned int surface = source->GetSurfaceTexture();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, surface != 0 ? surface : WhiteTexture());
      if (surface != 0)
         PrepareSurfaceTexture();
      glUniform1i(glGetUniformLocation(mProgram, "uTexture"), 0);
      glUniform1i(glGetUniformLocation(mProgram, "uHasTexture"), surface != 0 ? 1 : 0);

      // The remaining material channels. Units 0 and 1 are taken by albedo and
      // the shadow map, so these start at 2.
      {
         static const char* kMapUniform[] = { "", "uRoughnessMap", "uMetallicMap",
                                              "uNormalMap", "uAoMap" };
         static const char* kHasUniform[] = { "", "uHasRoughnessMap", "uHasMetallicMap",
                                              "uHasNormalMap", "uHasAoMap" };
         for (int map = kMapRoughness; map < kMapCount; map++)
         {
            const unsigned int tex = source->GetMaterialTexture(map);
            const int unit = 2 + (map - kMapRoughness);
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, tex != 0 ? tex : WhiteTexture());
            glUniform1i(glGetUniformLocation(mProgram, kMapUniform[map]), unit);
            glUniform1i(glGetUniformLocation(mProgram, kHasUniform[map]), tex != 0 ? 1 : 0);
         }
         glActiveTexture(GL_TEXTURE0);
      }
      {
         auto* asMaterial = dynamic_cast<MaterialNode*>(source);
         glUniform1f(glGetUniformLocation(mProgram, "uNormalStrength"),
                     asMaterial ? asMaterial->normalStrength : 1.0f);
      }

      // Instanced sources upload a transform per copy and draw them all at
      // once; ten thousand instances stay a single draw call.
      auto* instancer = dynamic_cast<InstanceOnPointsNode*>(source);
      const bool instanced = instancer != nullptr && instancer->InstanceCount() > 0;
      glUniform1i(glGetUniformLocation(mProgram, "uInstanced"), instanced ? 1 : 0);
      if (instanced)
      {
         if (gpu.instanceVbo == 0)
            glGenBuffers(1, &gpu.instanceVbo);

         const unsigned long long instanceRevision = instancer->InstanceRevision();
         if (gpu.instanceRevision != instanceRevision || !gpu.instanceAttribsOn)
         {
            const std::vector<Mat4>& xforms = instancer->InstanceTransforms();
            glBindBuffer(GL_ARRAY_BUFFER, gpu.instanceVbo);
            glBufferData(GL_ARRAY_BUFFER, xforms.size() * sizeof(Mat4), xforms.data(),
                         GL_STATIC_DRAW);
            // a mat4 attribute is four consecutive vec4 slots
            for (int col = 0; col < 4; col++)
            {
               const unsigned int loc = 3 + col;
               glEnableVertexAttribArray(loc);
               glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                     (void*)(size_t)(col * 4 * sizeof(float)));
               glVertexAttribDivisor(loc, 1);
            }
            // Per-instance colours travel in their own buffer rather than being
            // packed into the transform, so the common case - instances sharing
            // one material - uploads nothing extra at all.
            const std::vector<float>& colors = instancer->InstanceColors();
            if (!colors.empty())
            {
               if (gpu.instanceColorVbo == 0)
                  glGenBuffers(1, &gpu.instanceColorVbo);
               glBindBuffer(GL_ARRAY_BUFFER, gpu.instanceColorVbo);
               glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(),
                            GL_STATIC_DRAW);
               glEnableVertexAttribArray(7);
               glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
               glVertexAttribDivisor(7, 1);
            }
            else
            {
               glDisableVertexAttribArray(7);
               glVertexAttribDivisor(7, 0);
            }
            gpu.instanceColored = !colors.empty();

            gpu.instanceRevision = instanceRevision;
            gpu.instanceCount = (int)xforms.size();
            gpu.instanceAttribsOn = true;
            mLastUploads++;
         }
      }
      else if (gpu.instanceAttribsOn)
      {
         // Only on the transition; these are VAO state and persist otherwise.
         for (int col = 0; col < 4; col++)
         {
            glDisableVertexAttribArray(3 + col);
            glVertexAttribDivisor(3 + col, 0);
         }
         glDisableVertexAttribArray(7);
         glVertexAttribDivisor(7, 0);
         gpu.instanceAttribsOn = false;
         gpu.instanceColored = false;
      }
      glUniform1i(glGetUniformLocation(mProgram, "uInstanceColored"),
                  (instanced && gpu.instanceColored) ? 1 : 0);

      glUniformMatrix4fv(glGetUniformLocation(mProgram, "uModel"), 1, GL_FALSE, model.m);
      glUniformMatrix3fv(glGetUniformLocation(mProgram, "uNormalMatrix"), 1, GL_FALSE, normalMatrix);
      glUniform3fv(glGetUniformLocation(mProgram, "uBaseColor"), 1, material.color);
      glUniform1f(glGetUniformLocation(mProgram, "uMetallic"), material.metallic);
      glUniform1f(glGetUniformLocation(mProgram, "uRoughness"), material.roughness);
      glUniform1f(glGetUniformLocation(mProgram, "uOpacity"), material.opacity);
      glUniform1i(glGetUniformLocation(mProgram, "uShading"), material.shading);
      glUniform3fv(glGetUniformLocation(mProgram, "uEmissionColor"), 1, material.emissionColor);
      glUniform1f(glGetUniformLocation(mProgram, "uEmission"), material.emission);

      if (instanced)
      {
         glDrawElementsInstanced(GL_TRIANGLES, gpu.indexCount, GL_UNSIGNED_INT,
                                 nullptr, (GLsizei)gpu.instanceCount);
         mLastTriangles += (size_t)(gpu.indexCount / 3) * (size_t)gpu.instanceCount;
      }
      else
      {
         glDrawElements(GL_TRIANGLES, gpu.indexCount, GL_UNSIGNED_INT, nullptr);
         mLastTriangles += gpu.indexCount / 3;
      }
      mLastDrawCalls++;

      if (surface != 0)
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, surface);
         RestoreSurfaceTexture();
      }
   }

   glBindVertexArray(0);
   glUseProgram(0);

   // Resolve the multisampled buffer down into the texture the graph reads.
   // Scissor is already off, which matters here: a blit is clipped by it too.
   if (multisampling)
   {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, mMsFbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
      glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
   }

   // --- restore, or every 2D node after this draws wrongly ------------
   if (!prevDepth) glDisable(GL_DEPTH_TEST);
   if (!prevCull) glDisable(GL_CULL_FACE);
   if (!prevBlend) glDisable(GL_BLEND);
   if (prevScissor)
   {
      glEnable(GL_SCISSOR_TEST);
      glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);
   }
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}
