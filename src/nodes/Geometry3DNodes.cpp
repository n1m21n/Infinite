#include "Geometry3DNodes.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cstdio>

#include "Transport.h"
#include "SceneNodes.h"
#include "GeometryOpNodes.h"

namespace
{
   const std::vector<std::string> kShapeNames = {
      "Plane", "Cube", "Sphere", "Icosphere", "Torus", "Cylinder", "Cone", "Torus Knot"
   };
   const std::vector<std::string> kShadingNames = { "Lit", "Normals", "UV", "Flat" };
   const std::vector<std::string> kProjectionNames = { "Perspective", "Orthographic" };
   const std::vector<std::string> kSampleNames = { "Off", "2x", "4x", "8x" };
   const std::vector<std::string> kTonemapNames = { "None", "ACES", "Reinhard" };
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
      "   gl_Position = uViewProj * world;\n"
      "}\n";

   const char* kFragSrc =
      "#version 150\n"
      "in vec3 vWorldPos;\n"
      "in vec3 vNormal;\n"
      "in vec2 vUv;\n"
      "in vec3 vInstanceColor;\n"
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
      "uniform float uExposure;\n"
      "uniform int uTonemap;\n"
      "uniform vec3 uEmissionColor;\n"
      "uniform float uEmission;\n"
      "uniform vec3 uEnvSky;\n"
      "uniform vec3 uEnvHorizon;\n"
      "uniform vec3 uEnvGround;\n"
      "uniform float uEnvIntensity;\n"
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
      "   float rough = clamp(uRoughness, 0.045, 1.0);\n"
      "   float metal = clamp(uMetallic, 0.0, 1.0);\n"
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
      "      col += (kD * base / 3.14159265 + specular) * radiance * nDotL;\n"
      "   }\n"
      "\n"
      // Ambient, from the environment rather than a flat constant: the diffuse
      // half samples straight up the normal, the specular half up the mirror
      // direction. uAmbient stays as a tint so old patches still respond to it.
      "   vec3 reflectDir = reflect(-viewDir, n);\n"
      "   vec3 irradiance = sampleEnv(n, 1.0) * toLinear(uAmbient) * 3.0;\n"
      "   vec3 reflection = sampleEnv(reflectDir, rough);\n"
      "   vec3 fAmbient = fresnelSchlickRoughness(nDotV, f0, rough);\n"
      "   col += (vec3(1.0) - fAmbient) * (1.0 - metal) * base * irradiance;\n"
      "   col += reflection * fAmbient;\n"
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
       bevel == mBuiltBevel && bevelSegments == mBuiltBevelSegments && !mMesh.Empty())
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
      default: mMesh = Primitives::TorusKnot(detail * 8, sides, tubeRadius, knotP, knotQ); break;
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
   for (int i = 0; i < kSlots; i++)
      ReleaseGpuMesh(mGpu[i]);
   if (mProgram != 0) glDeleteProgram(mProgram);
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
