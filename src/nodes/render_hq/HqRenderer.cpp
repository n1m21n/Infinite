// Turbo 0.71 (S1): the Render 3D HQ renderer (module built with
// INFINITE_ENABLE_RENDER_HQ). A forward renderer with a depth + normal
// prepass: the prepass lays down depth (and the view-space normal and
// roughness the screen-space passes of S4/S5 read), the HDRI goes behind it
// where nothing was drawn, then each pixel is shaded once with depth test
// LEQUAL and no depth write (S2: Filament's standard model, hq_surface.frag;
// its own image-based lighting built here, hq_ibl.frag). The HDR result goes
// through Render3DPost (bloom, tonemap, sRGB) like Render 3D's. Its own
// targets, IBL maps and GPU copies of the geometry: Render 3D is untouched.
//
// Draws triangle meshes (Geometry, Group 3D, mesh operators), PointZ triangles
// straight from their GPU buffers, and PointZ points as sphere impostors (S3:
// hq_sphere.*, one instanced strip per source, no CPU copy). PointZ line strips
// and Instance PointZ are counted in LastSkipped() (instancing: S7).

#include "HqRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "core/GLUtil.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/Render3DPost.h"
#include "nodes/Render3DTemporal.h"
#include "platform/OpenGLHeaders.h"
#include "pop/PopData.h"
#include "pop/PopGpu.h"

namespace
{
   unsigned int CompileStage(GLenum type, const char* name)
   {
      const std::string src = Render3DTemporal::LoadShaderSource(name);
      if (src.empty())
      {
         fprintf(stderr, "Render 3D HQ: shader %s is not in this build\n", name);
         return 0;
      }
      const char* p = src.c_str();
      const GLuint s = glCreateShader(type);
      glShaderSource(s, 1, &p, nullptr);
      glCompileShader(s);
      GLint ok = 0;
      glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
      if (!ok)
      {
         char log[2048];
         glGetShaderInfoLog(s, sizeof(log), nullptr, log);
         fprintf(stderr, "Render 3D HQ: %s did not compile: %s\n", name, log);
         glDeleteShader(s);
         return 0;
      }
      return s;
   }

   unsigned int BuildProgram(const char* vertName, const char* fragName)
   {
      const unsigned int vs = CompileStage(GL_VERTEX_SHADER, vertName);
      const unsigned int fs = CompileStage(GL_FRAGMENT_SHADER, fragName);
      if (vs == 0 || fs == 0)
      {
         if (vs != 0) glDeleteShader(vs);
         if (fs != 0) glDeleteShader(fs);
         return 0;
      }
      const GLuint prog = glCreateProgram();
      glAttachShader(prog, vs);
      glAttachShader(prog, fs);
      glLinkProgram(prog);
      glDeleteShader(vs);
      glDeleteShader(fs);
      GLint ok = 0;
      glGetProgramiv(prog, GL_LINK_STATUS, &ok);
      if (!ok)
      {
         char log[2048];
         glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
         fprintf(stderr, "Render 3D HQ: %s + %s did not link: %s\n", vertName, fragName, log);
         glDeleteProgram(prog);
         return 0;
      }
      GLUtil::LabelObject(GL_PROGRAM, prog, fragName);
      return prog;
   }

   // Prefiltered radiance levels: 512 x 256 down to 16 x 8, level i at
   // perceptual roughness 1 - sqrt(1 - i / (levels - 1)) (cmgen's mapping,
   // the inverse of the shader's lod = L * pr * (2 - pr)).
   const int kPrefLevels = 6;
   const int kPrefW = 512, kPrefH = 256;
   const int kIrrW = 64, kIrrH = 32;
   const int kDfgSize = 128;
   // Texture units of the shading passes (set once at link).
   const int kUnitIrr = 0, kUnitPref = 1, kUnitDfg = 2, kUnitShadow = 3, kUnitMap0 = 4;
   const int kUnitShadowRaw = 12, kUnitSceneDepth = 13, kUnitLast = 13;
   // S4: Filament's ShadowOptions defaults (polygonOffsetConstant / Slope,
   // normalBias in texels) and contact shadows' maxShadowDistance.
   const float kPolygonOffsetConstant = 0.5f, kPolygonOffsetSlope = 2.0f;
   const float kNormalBiasTexels = 1.0f;
   const float kContactDistance = 0.3f;

   // What the shading passes need to sample the shadow map (S4).
   struct ShadowState
   {
      int light = -1; // index into HqFrame::lights, -1 = no shadows
      Mat4 viewProj;
      float texel = 0.0f, normalBias = 0.0f, constBias = 0.0f, depthRange = 1.0f, softness = 0.0f;
      int contact = 0;
      float proj[4] = { 0, 0, 0, 0 }; // proj m[10], m[14], ortho, contact distance
   };

   unsigned int MakeTexture(int w, int h, int levels, GLenum internal, bool wrapU, const char* label)
   {
      unsigned int tex = 0;
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexStorage2D(GL_TEXTURE_2D, levels, internal, w, h);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
      GLUtil::LabelObject(GL_TEXTURE, tex, label);
      return tex;
   }

   // Points a VAO slot at a PointZ attribute's GPU buffer (or a shared buffer
   // of its default when the PointZ lacks it), keeping the buffer alive in
   // `keep` while the VAO points at it.
   void BindPopAttrib(unsigned int loc, const pop::PopData& d, const char* name, int size, bool perInstance,
                      std::vector<pop::BufferPtr>& keep)
   {
      pop::BufferPtr buf;
      if (const pop::PopAttrib* a = d.Find(name))
         if (a->buffer->Count() >= d.pointCount)
            buf = a->buffer;
      if (!buf)
      {
         static std::unordered_map<std::string, pop::BufferPtr> sDefaults;
         pop::BufferPtr& cached = sDefaults[std::string(name) + "#" + std::to_string(size)];
         const int need = std::max(1, d.pointCount);
         if (!cached || cached->Count() < need)
         {
            float def[4];
            pop::DefaultValue(name, def);
            cached = pop::MakeFilled(std::max(need, cached ? cached->Count() * 2 : 0), std::max(1, size), def);
         }
         buf = cached;
      }
      keep.push_back(buf);
      glBindBuffer(GL_ARRAY_BUFFER, buf->Gpu());
      glEnableVertexAttribArray(loc);
      glVertexAttribPointer(loc, std::min(size, buf->Comps()), GL_FLOAT, GL_FALSE, buf->Comps() * (GLsizei)sizeof(float),
                            (void*)0);
      glVertexAttribDivisor(loc, perInstance ? 1 : 0);
   }

   // Locations of the lighting uniforms (hq_lighting.glsl), shared by the
   // triangle and the sphere programs.
   struct LitLocs
   {
      int camPos = -1, baseColor = -1, metallic = -1, roughness = -1, emissionColor = -1, emission = -1;
      int ior = -1, specular = -1, clearcoat = -1, clearcoatRoughness = -1;
      int sheen = -1, sheenColor = -1, sheenRoughness = -1;
      int lightCount = -1, lightVec = -1, lightColor = -1, lightIntensity = -1, lightType = -1;
      int lightSpotAngle = -1, lightSpotPenumbra = -1;
      int hasIbl = -1, prefLevelOne = -1;
      int envRotation = -1, envIntensity = -1, envSky = -1, envHorizon = -1, envGround = -1, exposure = -1;
      int shadowLight = -1, lightViewProj = -1, shadowTexel = -1, shadowNormalBias = -1, shadowConstBias = -1;
      int shadowDepthRange = -1, shadowSoftness = -1, contactShadows = -1, projParams = -1;

      void Load(unsigned int p)
      {
         auto loc = [p](const char* n) { return glGetUniformLocation(p, n); };
         camPos = loc("uCamPos");
         baseColor = loc("uBaseColor");
         metallic = loc("uMetallic");
         roughness = loc("uRoughness");
         emissionColor = loc("uEmissionColor");
         emission = loc("uEmission");
         ior = loc("uIor");
         specular = loc("uSpecular");
         clearcoat = loc("uClearcoat");
         clearcoatRoughness = loc("uClearcoatRoughness");
         sheen = loc("uSheen");
         sheenColor = loc("uSheenColor");
         sheenRoughness = loc("uSheenRoughness");
         lightCount = loc("uLightCount");
         lightVec = loc("uLightVec");
         lightColor = loc("uLightColor");
         lightIntensity = loc("uLightIntensity");
         lightType = loc("uLightType");
         lightSpotAngle = loc("uLightSpotAngle");
         lightSpotPenumbra = loc("uLightSpotPenumbra");
         hasIbl = loc("uHasIbl");
         prefLevelOne = loc("uPrefLevelOne");
         envRotation = loc("uEnvRotation");
         envIntensity = loc("uEnvIntensity");
         envSky = loc("uEnvSky");
         envHorizon = loc("uEnvHorizon");
         envGround = loc("uEnvGround");
         exposure = loc("uExposure");
         shadowLight = loc("uShadowLight");
         lightViewProj = loc("uLightViewProj");
         shadowTexel = loc("uShadowTexel");
         shadowNormalBias = loc("uShadowNormalBias");
         shadowConstBias = loc("uShadowConstBias");
         shadowDepthRange = loc("uShadowDepthRange");
         shadowSoftness = loc("uShadowSoftness");
         contactShadows = loc("uContactShadows");
         projParams = loc("uProjParams");
         // Samplers sit on fixed units (the program is bound by the caller).
         glUniform1i(loc("uIrrEnv"), kUnitIrr);
         glUniform1i(loc("uPrefEnv"), kUnitPref);
         glUniform1i(loc("uDfg"), kUnitDfg);
         glUniform1i(loc("uShadowMap"), kUnitShadow);
         glUniform1i(loc("uShadowDepth"), kUnitShadowRaw);
         glUniform1i(loc("uSceneDepth"), kUnitSceneDepth);
      }

      void SetShadow(const ShadowState& st) const
      {
         glUniform1i(shadowLight, st.light);
         glUniformMatrix4fv(lightViewProj, 1, GL_FALSE, st.viewProj.m);
         glUniform2f(shadowTexel, st.texel, st.texel);
         glUniform1f(shadowNormalBias, st.normalBias);
         glUniform1f(shadowConstBias, st.constBias);
         glUniform1f(shadowDepthRange, st.depthRange);
         glUniform1f(shadowSoftness, st.softness);
         glUniform1i(contactShadows, st.contact);
         glUniform4fv(projParams, 1, st.proj);
      }

      // Camera, lights and environment: once per pass (program bound).
      void SetFrame(const HqFrame& f, bool ibl) const
      {
         glUniform3fv(camPos, 1, f.eye);
         float vec[HqFrame::kMaxLights * 3] = {}, color[HqFrame::kMaxLights * 3] = {};
         float intensity[HqFrame::kMaxLights] = {}, spotAngle[HqFrame::kMaxLights] = {};
         float spotPenumbra[HqFrame::kMaxLights] = {};
         int type[HqFrame::kMaxLights] = {};
         for (int l = 0; l < f.lightCount; l++)
         {
            std::copy(f.lights[l].vec, f.lights[l].vec + 3, vec + l * 3);
            std::copy(f.lights[l].color, f.lights[l].color + 3, color + l * 3);
            intensity[l] = f.lights[l].intensity;
            type[l] = f.lights[l].type;
            spotAngle[l] = f.lights[l].spotAngle;
            spotPenumbra[l] = f.lights[l].spotPenumbra;
         }
         glUniform1i(lightCount, f.lightCount);
         glUniform3fv(lightVec, HqFrame::kMaxLights, vec);
         glUniform3fv(lightColor, HqFrame::kMaxLights, color);
         glUniform1fv(lightIntensity, HqFrame::kMaxLights, intensity);
         glUniform1iv(lightType, HqFrame::kMaxLights, type);
         glUniform1fv(lightSpotAngle, HqFrame::kMaxLights, spotAngle);
         glUniform1fv(lightSpotPenumbra, HqFrame::kMaxLights, spotPenumbra);
         glUniform1i(hasIbl, ibl ? 1 : 0);
         glUniform1f(prefLevelOne, (float)(kPrefLevels - 1));
         glUniform1f(envRotation, f.envRotation);
         glUniform1f(envIntensity, f.envIntensity);
         glUniform3fv(envSky, 1, f.envSky);
         glUniform3fv(envHorizon, 1, f.envHorizon);
         glUniform3fv(envGround, 1, f.envGround);
         glUniform1f(exposure, f.exposure);
      }

      void SetMaterial(const Material& m) const
      {
         glUniform3fv(baseColor, 1, m.color);
         glUniform1f(metallic, m.metallic);
         glUniform1f(roughness, m.roughness);
         glUniform3fv(emissionColor, 1, m.emissionColor);
         glUniform1f(emission, m.emission);
         glUniform1f(ior, m.ior);
         glUniform1f(specular, m.specular);
         glUniform1f(clearcoat, m.clearcoat);
         glUniform1f(clearcoatRoughness, m.clearcoatRoughness);
         glUniform1f(sheen, m.sheen);
         glUniform3fv(sheenColor, 1, m.sheenColor);
         glUniform1f(sheenRoughness, m.sheenRoughness);
      }
   };

   class HqRendererGl : public HqRenderer
   {
   public:
      ~HqRendererGl() override { Release(); }

      unsigned int Render(const HqFrame& frame) override;
      unsigned int DepthTexture() const override { return mDepth; }
      size_t LastTriangles() const override { return mTriangles; }
      size_t LastDrawCalls() const override { return mDrawCalls; }
      size_t LastSpheres() const override { return mSpheres; }
      size_t LastSkipped() const override { return mSkipped; }
      const std::string& Error() const override { return mError; }

   private:
      // How a draw-list entry is drawn.
      enum Kind { kNone, kMesh, kPopMesh, kSpheres };
      // One per entry of the draw list. A CPU mesh is re-uploaded only when its
      // source or mesh revision moves; a PointZ is never copied - its VAO points
      // at the PointZ's own GPU buffers, re-pointed when its revision moves.
      struct Slot
      {
         Kind kind = kNone;
         const void* source = nullptr;
         unsigned long long revision = 0;
         unsigned int vao = 0, vbo = 0, ibo = 0;
         int indexCount = 0;  // triangles x 3 (kMesh, kPopMesh)
         int pointCount = 0;  // spheres (kSpheres)
         float radiusScale = 1.0f; // PointBaseSize (x 0.02 without PScale)
         float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 }; // object-space bounds (Mapping: generated)
         std::vector<pop::BufferPtr> keep; // the PointZ buffers the VAO points at
         std::shared_ptr<pop::AsyncBounds> bounds;
         bool hasBounds = false; // lo / hi are known (shadow fit, Mapping)
         bool drawable = false;
      };
      // Uniform locations, looked up once at link (none per frame).
      struct PrepassProgram
      {
         unsigned int id = 0;
         int viewProj = -1, model = -1, normalMatrix = -1, view = -1, roughness = -1;
      };
      struct SurfaceProgram
      {
         unsigned int id = 0;
         int viewProj = -1, model = -1, normalMatrix = -1;
         int shading = -1, maps = -1, normalBump = -1, normalStrength = -1;
         int mapSpace = -1, mapTranslate = -1, mapRotate = -1, mapScale = -1, triplanarBlend = -1;
         int objLo = -1, objHi = -1;
         LitLocs lit;
      };
      struct ShadowProgram
      {
         unsigned int id = 0;
         int viewProj = -1, model = -1, normalMatrix = -1;
      };
      // Spheres in the shadow map: one point sprite each, a disc at the centre's depth.
      struct SphereShadowProgram
      {
         unsigned int id = 0;
         int viewProj = -1, model = -1, radiusScale = -1, texelsPerUnit = -1;
      };
      struct SphereProgram
      {
         unsigned int id = 0;
         int viewProj = -1, view = -1, model = -1, radiusScale = -1, ortho = -1, viewDir = -1, prepass = -1;
         int earlyDepth = -1;
         LitLocs lit;
      };
      struct BackgroundProgram
      {
         unsigned int id = 0;
         int invViewProj = -1, camPos = -1, envRotation = -1, envIntensity = -1, exposure = -1;
      };
      struct IblProgram
      {
         unsigned int id = 0;
         int mode = -1, perceptualRoughness = -1, srcTexels = -1, srcMaxLod = -1;
      };

      bool EnsurePrograms();
      bool EnsureTargets(int w, int h);
      void EnsureDfg();
      bool EnsureIbl(const HqFrame& f);
      void DrawFullscreenInto(unsigned int tex, int level, int w, int h);
      void Upload(const std::vector<IGeometrySource*>& draw);
      void ResetSlot(Slot& slot, Kind kind);
      ShadowState UpdateShadows(const HqFrame& f, const std::vector<IGeometrySource*>& draw);
      void Release();

      PrepassProgram mPre;
      SurfaceProgram mSurf;
      SphereProgram mSph;
      ShadowProgram mShadowProg;
      SphereShadowProgram mSphShadow;
      BackgroundProgram mBg;
      IblProgram mIbl;
      bool mProgramsTried = false;
      unsigned int mFbo = 0, mColor = 0, mNormal = 0, mDepth = 0;
      unsigned int mEmptyVao = 0, mIblFbo = 0;
      unsigned int mPref = 0, mIrr = 0, mDfg = 0;
      unsigned int mShadowTex = 0, mShadowFbo = 0, mCmpSampler = 0, mRawSampler = 0;
      int mShadowSize = 0;
      std::vector<unsigned long long> mShadowSig; // what the map was last drawn from
      unsigned int mDepthCopy = 0, mDepthCopyFbo = 0;
      unsigned int mIblSource = 0;
      unsigned long long mIblRevision = 0;
      int mW = 0, mH = 0;
      std::vector<Slot> mSlots;
      Render3DPost mPost;
      size_t mTriangles = 0, mDrawCalls = 0, mSpheres = 0, mSkipped = 0;
      std::string mError;
   };

   bool HqRendererGl::EnsurePrograms()
   {
      if (mProgramsTried)
         return mPre.id != 0 && mSurf.id != 0 && mSph.id != 0 && mShadowProg.id != 0 && mSphShadow.id != 0 && mBg.id != 0 && mIbl.id != 0;
      mProgramsTried = true;
      mPre.id = BuildProgram("hq_surface.vert.glsl", "hq_prepass.frag.glsl");
      mSurf.id = BuildProgram("hq_surface.vert.glsl", "hq_surface.frag.glsl");
      mSph.id = BuildProgram("hq_sphere.vert.glsl", "hq_sphere.frag.glsl");
      mShadowProg.id = BuildProgram("hq_surface.vert.glsl", "hq_shadow.frag.glsl");
      mSphShadow.id = BuildProgram("hq_sphere_shadow.vert.glsl", "hq_sphere_shadow.frag.glsl");
      mBg.id = BuildProgram("hq_background.vert.glsl", "hq_background.frag.glsl");
      mIbl.id = BuildProgram("hq_background.vert.glsl", "hq_ibl.frag.glsl");
      if (mPre.id == 0 || mSurf.id == 0 || mSph.id == 0 || mShadowProg.id == 0 || mSphShadow.id == 0 || mBg.id == 0 || mIbl.id == 0)
      {
         mError = "Render 3D HQ: its shaders did not build (see the log)";
         return false;
      }
      auto loc = [](unsigned int p, const char* n) { return glGetUniformLocation(p, n); };
      mPre.viewProj = loc(mPre.id, "uViewProj");
      mPre.model = loc(mPre.id, "uModel");
      mPre.normalMatrix = loc(mPre.id, "uNormalMatrix");
      mPre.view = loc(mPre.id, "uView");
      mPre.roughness = loc(mPre.id, "uRoughness");

      const unsigned int s = mSurf.id;
      glUseProgram(s);
      mSurf.viewProj = loc(s, "uViewProj");
      mSurf.model = loc(s, "uModel");
      mSurf.normalMatrix = loc(s, "uNormalMatrix");
      mSurf.shading = loc(s, "uShading");
      mSurf.maps = loc(s, "uMaps");
      mSurf.normalBump = loc(s, "uNormalBump");
      mSurf.normalStrength = loc(s, "uNormalStrength");
      mSurf.mapSpace = loc(s, "uMapSpace");
      mSurf.mapTranslate = loc(s, "uMapTranslate");
      mSurf.mapRotate = loc(s, "uMapRotate");
      mSurf.mapScale = loc(s, "uMapScale");
      mSurf.triplanarBlend = loc(s, "uTriplanarBlend");
      mSurf.objLo = loc(s, "uObjLo");
      mSurf.objHi = loc(s, "uObjHi");
      mSurf.lit.Load(s);
      const char* mapNames[kMapCount] = { "uAlbedoMap", "uRoughnessMap", "uMetallicMap", "uNormalMap",
                                          "uAoMap", "uEmissionMap", "uClearcoatMap", "uSheenMap" };
      for (int m = 0; m < kMapCount; m++)
         glUniform1i(loc(s, mapNames[m]), kUnitMap0 + m);

      glUseProgram(mSph.id);
      mSph.viewProj = loc(mSph.id, "uViewProj");
      mSph.view = loc(mSph.id, "uView");
      mSph.model = loc(mSph.id, "uModel");
      mSph.radiusScale = loc(mSph.id, "uRadiusScale");
      mSph.ortho = loc(mSph.id, "uOrtho");
      mSph.viewDir = loc(mSph.id, "uViewDir");
      mSph.prepass = loc(mSph.id, "uPrepass");
      mSph.earlyDepth = loc(mSph.id, "uEarlyDepth");
      mSph.lit.Load(mSph.id);

      mShadowProg.viewProj = loc(mShadowProg.id, "uViewProj");
      mShadowProg.model = loc(mShadowProg.id, "uModel");
      mShadowProg.normalMatrix = loc(mShadowProg.id, "uNormalMatrix");
      mSphShadow.viewProj = loc(mSphShadow.id, "uViewProj");
      mSphShadow.model = loc(mSphShadow.id, "uModel");
      mSphShadow.radiusScale = loc(mSphShadow.id, "uRadiusScale");
      mSphShadow.texelsPerUnit = loc(mSphShadow.id, "uTexelsPerUnit");

      // The shadow map read two ways: compared (PCF) and raw (blocker search).
      glGenSamplers(1, &mCmpSampler);
      glSamplerParameteri(mCmpSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glSamplerParameteri(mCmpSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glSamplerParameteri(mCmpSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glSamplerParameteri(mCmpSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glSamplerParameteri(mCmpSampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
      glSamplerParameteri(mCmpSampler, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
      glGenSamplers(1, &mRawSampler);
      glSamplerParameteri(mRawSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glSamplerParameteri(mRawSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glSamplerParameteri(mRawSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glSamplerParameteri(mRawSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glSamplerParameteri(mRawSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);

      mBg.invViewProj = loc(mBg.id, "uInvViewProj");
      mBg.camPos = loc(mBg.id, "uCamPos");
      mBg.envRotation = loc(mBg.id, "uEnvRotation");
      mBg.envIntensity = loc(mBg.id, "uEnvIntensity");
      mBg.exposure = loc(mBg.id, "uExposure");

      mIbl.mode = loc(mIbl.id, "uMode");
      mIbl.perceptualRoughness = loc(mIbl.id, "uPerceptualRoughness");
      mIbl.srcTexels = loc(mIbl.id, "uSrcTexels");
      mIbl.srcMaxLod = loc(mIbl.id, "uSrcMaxLod");

      glUseProgram(mBg.id);
      glUniform1i(loc(mBg.id, "uEnvMap"), 0);
      glUseProgram(mIbl.id);
      glUniform1i(loc(mIbl.id, "uEnv"), 0);
      glUseProgram(0);
      glGenVertexArrays(1, &mEmptyVao);
      glGenFramebuffers(1, &mIblFbo);
      return true;
   }

   bool HqRendererGl::EnsureTargets(int w, int h)
   {
      if (mFbo != 0 && w == mW && h == mH)
         return true;
      auto makeTex = [w, h](unsigned int& tex, GLenum internal, const char* label) {
         if (tex != 0)
            glDeleteTextures(1, &tex);
         glGenTextures(1, &tex);
         glBindTexture(GL_TEXTURE_2D, tex);
         glTexStorage2D(GL_TEXTURE_2D, 1, internal, w, h);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         GLUtil::LabelObject(GL_TEXTURE, tex, label);
      };
      makeTex(mColor, GL_RGBA16F, "Render 3D HQ: colour");
      makeTex(mNormal, GL_RGBA16F, "Render 3D HQ: normal + roughness");
      makeTex(mDepth, GL_DEPTH_COMPONENT24, "Render 3D HQ: depth");
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
      // S4: a copy of the prepass depth that contact shadows sample while the
      // real one is attached (no feedback loop).
      makeTex(mDepthCopy, GL_DEPTH_COMPONENT24, "Render 3D HQ: depth copy");
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
      glBindTexture(GL_TEXTURE_2D, 0);

      if (mFbo == 0)
         glGenFramebuffers(1, &mFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mColor, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mNormal, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepth, 0);
      // Only when the targets are (re)made, never per frame.
      const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
      if (!complete)
      {
         mError = "Render 3D HQ: its render targets could not be made at this size";
         return false;
      }
      GLUtil::LabelObject(GL_FRAMEBUFFER, mFbo, "Render 3D HQ");
      if (mDepthCopyFbo == 0)
         glGenFramebuffers(1, &mDepthCopyFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, mDepthCopyFbo);
      glDrawBuffer(GL_NONE);
      glReadBuffer(GL_NONE);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthCopy, 0);
      mW = w;
      mH = h;
      return true;
   }

   // One full-screen pass of the IBL program into a level of `tex` (the
   // caller has the program bound and its uniforms set).
   void HqRendererGl::DrawFullscreenInto(unsigned int tex, int level, int w, int h)
   {
      glBindFramebuffer(GL_FRAMEBUFFER, mIblFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, level);
      glViewport(0, 0, w, h);
      glBindVertexArray(mEmptyVao);
      glDrawArrays(GL_TRIANGLES, 0, 3);
   }

   // The DFG table depends on nothing in the scene: built once.
   void HqRendererGl::EnsureDfg()
   {
      if (mDfg != 0)
         return;
      GLUtil::DebugScope group("Render 3D HQ: DFG table");
      mDfg = MakeTexture(kDfgSize, kDfgSize, 1, GL_RGBA16F, false, "Render 3D HQ: DFG");
      glUseProgram(mIbl.id);
      glUniform1i(mIbl.mode, 2);
      DrawFullscreenInto(mDfg, 0, kDfgSize, kDfgSize);
   }

   // Prefiltered radiance + irradiance for the patched HDRI, rebuilt only when
   // it (or its pixels) change.
   bool HqRendererGl::EnsureIbl(const HqFrame& f)
   {
      if (f.envTex == 0)
         return false;
      if (f.envTex == mIblSource && f.envRevision == mIblRevision && mPref != 0)
         return true;
      GLUtil::DebugScope group("Render 3D HQ: IBL maps");
      if (mPref != 0) glDeleteTextures(1, &mPref);
      if (mIrr != 0) glDeleteTextures(1, &mIrr);
      mPref = MakeTexture(kPrefW, kPrefH, kPrefLevels, GL_RGBA16F, true, "Render 3D HQ: prefiltered radiance");
      mIrr = MakeTexture(kIrrW, kIrrH, 1, GL_RGBA16F, true, "Render 3D HQ: irradiance");

      // The equirect base is 2^maxLod wide and half as tall (EnvironmentNode).
      const float envW = std::exp2(f.envMaxLod);
      glUseProgram(mIbl.id);
      glUniform1f(mIbl.srcTexels, envW * std::max(1.0f, envW * 0.5f));
      glUniform1f(mIbl.srcMaxLod, f.envMaxLod);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, f.envTex);
      glUniform1i(mIbl.mode, 0);
      for (int level = 0; level < kPrefLevels; level++)
      {
         const float lod01 = (float)level / (float)(kPrefLevels - 1);
         glUniform1f(mIbl.perceptualRoughness, 1.0f - std::sqrt(std::max(0.0f, 1.0f - lod01)));
         DrawFullscreenInto(mPref, level, std::max(1, kPrefW >> level), std::max(1, kPrefH >> level));
      }
      glUniform1i(mIbl.mode, 1);
      DrawFullscreenInto(mIrr, 0, kIrrW, kIrrH);
      mIblSource = f.envTex;
      mIblRevision = f.envRevision;
      return true;
   }

   // A fresh VAO when a slot changes what it draws (no stale attribute state).
   void HqRendererGl::ResetSlot(Slot& slot, Kind kind)
   {
      if (slot.vao != 0) glDeleteVertexArrays(1, &slot.vao);
      if (slot.vbo != 0) glDeleteBuffers(1, &slot.vbo);
      if (slot.ibo != 0) glDeleteBuffers(1, &slot.ibo);
      slot.vao = slot.vbo = slot.ibo = 0;
      slot.keep.clear();
      slot.bounds.reset();
      slot.hasBounds = false;
      slot.source = nullptr;
      slot.revision = 0;
      slot.kind = kind;
      glGenVertexArrays(1, &slot.vao);
   }

   void HqRendererGl::Upload(const std::vector<IGeometrySource*>& draw)
   {
      if (mSlots.size() < draw.size())
         mSlots.resize(draw.size());
      for (size_t i = 0; i < draw.size(); i++)
      {
         Slot& slot = mSlots[i];
         IGeometrySource* src = draw[i];
         slot.drawable = false;
         if (src == nullptr)
            continue;
         // Instance PointZ: not drawn yet (S7).
         if (src->GetPopInstances() != nullptr)
         {
            mSkipped++;
            continue;
         }
         // PointZ: drawn from its GPU buffers. Asking it for GetMesh() would
         // copy it back to the CPU.
         if (const pop::PopData* pd = src->GetPop())
         {
            if (pd->pointCount <= 0)
               continue;
            const pop::PopTopology& topo = pd->Topo();
            const Kind kind = topo.HasFaces() ? kPopMesh : (!topo.HasLines() ? kSpheres : kNone);
            if (kind == kNone) // line strips: not drawn by the HQ
            {
               mSkipped++;
               continue;
            }
            if (slot.kind != kind)
               ResetSlot(slot, kind);
            if (slot.source != (const void*)src || slot.revision != pd->revision || slot.keep.empty())
            {
               glBindVertexArray(slot.vao);
               slot.keep.clear();
               if (kind == kSpheres)
               {
                  BindPopAttrib(0, *pd, pop::attr::P, 3, true, slot.keep);
                  BindPopAttrib(1, *pd, pop::attr::PScale, 1, true, slot.keep);
                  BindPopAttrib(2, *pd, pop::attr::Color, 4, true, slot.keep);
                  BindPopAttrib(3, *pd, pop::attr::Active, 1, true, slot.keep);
                  slot.pointCount = pd->pointCount;
                  // Render 3D's sprite size: PScale x PointBaseSize, 0.02 without PScale.
                  slot.radiusScale = src->PointBaseSize() * (pd->Has(pop::attr::PScale) ? 1.0f : 0.02f);
                  if (!slot.bounds)
                     slot.bounds = std::make_shared<pop::AsyncBounds>();
               }
               else
               {
                  BindPopAttrib(0, *pd, pop::attr::P, 3, false, slot.keep);
                  BindPopAttrib(1, *pd, pop::attr::N, 3, false, slot.keep);
                  BindPopAttrib(2, *pd, pop::attr::Tex, 2, false, slot.keep);
                  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, topo.TriIbo());
                  slot.indexCount = (int)topo.tris.size();
                  if (!slot.bounds)
                     slot.bounds = std::make_shared<pop::AsyncBounds>();
               }
               slot.source = src;
               slot.revision = pd->revision;
            }
            // The box (Mapping, the shadow fit), measured without waiting on the GPU.
            if (slot.bounds && pd->BoundsAsync(*slot.bounds, slot.lo, slot.hi))
               slot.hasBounds = true;
            slot.drawable = true;
            continue;
         }
         const Mesh& mesh = src->GetMesh();
         if (mesh.indices.empty() || mesh.vertices.empty())
         {
            if (src->GetPointCloud() != nullptr)
               mSkipped++;
            continue;
         }
         if (slot.kind != kMesh)
         {
            ResetSlot(slot, kMesh);
            glGenBuffers(1, &slot.vbo);
            glGenBuffers(1, &slot.ibo);
            glBindVertexArray(slot.vao);
            glBindBuffer(GL_ARRAY_BUFFER, slot.vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nx));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot.ibo);
         }
         const unsigned long long revision = src->MeshRevision();
         if (slot.source != (const void*)src || slot.revision != revision)
         {
            glBindVertexArray(slot.vao);
            glBindBuffer(GL_ARRAY_BUFFER, slot.vbo);
            glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), mesh.vertices.data(), GL_STATIC_DRAW);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(),
                         GL_STATIC_DRAW);
            slot.indexCount = (int)mesh.indices.size();
            slot.source = src;
            slot.revision = revision;
            for (int k = 0; k < 3; k++)
            {
               slot.lo[k] = 1e30f;
               slot.hi[k] = -1e30f;
            }
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  slot.lo[k] = std::min(slot.lo[k], p[k]);
                  slot.hi[k] = std::max(slot.hi[k], p[k]);
               }
            }
            slot.hasBounds = true;
         }
         slot.drawable = true;
      }
      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
   }

   // S4: the shadow map of the first directional / sun light. Fitted to the
   // casters' world bounds, not to the camera, so moving the camera never
   // moves it; the size steps in 10% increments and the origin is snapped to
   // a texel, so moving objects don't make it shimmer either. Flat, wide
   // pieces (a floor) receive but do not widen the fit. Redrawn only when a
   // caster, the light or the fit changed.
   ShadowState HqRendererGl::UpdateShadows(const HqFrame& f, const std::vector<IGeometrySource*>& draw)
   {
      ShadowState st;
      if (!f.shadows)
         return st;
      int light = -1;
      for (int l = 0; l < f.lightCount && light < 0; l++)
         if (f.lights[l].type == 0 || f.lights[l].type == 2)
            light = l;
      if (light < 0)
         return st;
      float L[3] = { f.lights[light].vec[0], f.lights[light].vec[1], f.lights[light].vec[2] };
      const float len = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
      if (len < 1e-6f)
         return st;
      for (float& c : L)
         c /= len;

      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      bool any = false;
      for (size_t i = 0; i < draw.size(); i++)
      {
         const Slot& slot = mSlots[i];
         if (!slot.drawable || !slot.hasBounds)
            continue;
         float olo[3], ohi[3];
         const float pad = slot.kind == kSpheres ? slot.radiusScale : 0.0f;
         for (int k = 0; k < 3; k++)
         {
            olo[k] = slot.lo[k] - pad;
            ohi[k] = slot.hi[k] + pad;
         }
         const float ext[3] = { ohi[0] - olo[0], ohi[1] - olo[1], ohi[2] - olo[2] };
         const float big = std::max(ext[0], std::max(ext[1], ext[2]));
         const float small = std::min(ext[0], std::min(ext[1], ext[2]));
         if (big > 0.0f && small < 0.01f * big && draw.size() > 1)
            continue; // a floor or a wall: receives only
         const Mat4 model = draw[i]->GetModelMatrix();
         for (int corner = 0; corner < 8; corner++)
         {
            const float c[3] = { (corner & 1) ? ohi[0] : olo[0], (corner & 2) ? ohi[1] : olo[1],
                                 (corner & 4) ? ohi[2] : olo[2] };
            for (int k = 0; k < 3; k++)
            {
               const float w = model.m[k] * c[0] + model.m[4 + k] * c[1] + model.m[8 + k] * c[2] + model.m[12 + k];
               lo[k] = std::min(lo[k], w);
               hi[k] = std::max(hi[k], w);
            }
         }
         any = true;
      }
      if (!any)
         return st;
      const float centre[3] = { 0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2]) };
      const float d[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] };
      const float radius = std::max(1e-3f, 0.5f * std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
      const float R = std::pow(1.1f, std::ceil(std::log(radius) / std::log(1.1f)));

      const float eye[3] = { centre[0] + L[0] * 2.0f * R, centre[1] + L[1] * 2.0f * R, centre[2] + L[2] * 2.0f * R };
      const float up[3] = { std::fabs(L[1]) > 0.99f ? 1.0f : 0.0f, std::fabs(L[1]) > 0.99f ? 0.0f : 1.0f, 0.0f };
      const Mat4 view = Mat4::LookAt(eye, centre, up);
      // Depth: from in front of the casters to well past them (receivers below).
      const float nearZ = 0.5f * R, farZ = 7.0f * R;
      Mat4 proj = Mat4::Orthographic(R, 1.0f, nearZ, farZ);
      const int size = std::clamp(f.shadowSize, 512, 4096);
      {
         // Snap the world origin to a texel: the grid stays put as the fit moves.
         const Mat4 vp = Mat4::Multiply(proj, view);
         const float half = (float)size * 0.5f;
         const float ox = vp.m[12] * half, oy = vp.m[13] * half;
         proj.m[12] += (std::round(ox) - ox) / half;
         proj.m[13] += (std::round(oy) - oy) / half;
      }
      st.light = light;
      st.viewProj = Mat4::Multiply(proj, view);
      st.texel = 1.0f / (float)size;
      const float texelWorld = 2.0f * R / (float)size;
      st.normalBias = texelWorld * kNormalBiasTexels;
      st.depthRange = farZ - nearZ;
      st.constBias = 0.5f * texelWorld / st.depthRange;
      st.softness = f.shadowSoftness;

      // What the map would be drawn from; the same as last time = keep it.
      std::vector<unsigned long long> sig;
      auto pushF = [&sig](float v) {
         unsigned int bits;
         std::memcpy(&bits, &v, sizeof(bits));
         sig.push_back(bits);
      };
      sig.push_back((unsigned long long)size);
      for (float v : st.viewProj.m)
         pushF(v);
      for (size_t i = 0; i < draw.size(); i++)
      {
         const Slot& slot = mSlots[i];
         if (!slot.drawable)
            continue;
         sig.push_back((unsigned long long)slot.kind);
         sig.push_back((unsigned long long)(size_t)slot.source);
         sig.push_back(slot.revision);
         sig.push_back((unsigned long long)slot.pointCount);
         pushF(slot.radiusScale);
         for (float v : draw[i]->GetModelMatrix().m)
            pushF(v);
      }
      if (sig == mShadowSig && mShadowTex != 0)
         return st;
      mShadowSig.swap(sig);

      if (mShadowTex == 0 || mShadowSize != size)
      {
         if (mShadowTex != 0)
            glDeleteTextures(1, &mShadowTex);
         glGenTextures(1, &mShadowTex);
         glBindTexture(GL_TEXTURE_2D, mShadowTex);
         glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, size, size);
         GLUtil::LabelObject(GL_TEXTURE, mShadowTex, "Render 3D HQ: shadow map");
         if (mShadowFbo == 0)
            glGenFramebuffers(1, &mShadowFbo);
         glBindFramebuffer(GL_FRAMEBUFFER, mShadowFbo);
         glDrawBuffer(GL_NONE);
         glReadBuffer(GL_NONE);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mShadowTex, 0);
         mShadowSize = size;
      }

      GLUtil::DebugScope group("Render 3D HQ: shadow map");
      glBindFramebuffer(GL_FRAMEBUFFER, mShadowFbo);
      glViewport(0, 0, size, size);
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LESS);
      glDepthMask(GL_TRUE);
      glDisable(GL_CULL_FACE); // both faces cast: no light leaking through open meshes
      const float farDepth = 1.0f;
      glClearBufferfv(GL_DEPTH, 0, &farDepth);
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(kPolygonOffsetSlope, kPolygonOffsetConstant);
      glUseProgram(mShadowProg.id);
      glUniformMatrix4fv(mShadowProg.viewProj, 1, GL_FALSE, st.viewProj.m);
      for (size_t i = 0; i < draw.size(); i++)
      {
         if (!mSlots[i].drawable || mSlots[i].kind == kSpheres)
            continue;
         const Mat4 model = draw[i]->GetModelMatrix();
         float normalMatrix[9];
         model.NormalMatrix(normalMatrix);
         glUniformMatrix4fv(mShadowProg.model, 1, GL_FALSE, model.m);
         glUniformMatrix3fv(mShadowProg.normalMatrix, 1, GL_FALSE, normalMatrix);
         glBindVertexArray(mSlots[i].vao);
         glDrawElements(GL_TRIANGLES, mSlots[i].indexCount, GL_UNSIGNED_INT, nullptr);
      }
      glDisable(GL_POLYGON_OFFSET_FILL); // spheres: the disc through the centre is the bias
      bool sphereSet = false;
      for (size_t i = 0; i < draw.size(); i++)
      {
         if (!mSlots[i].drawable || mSlots[i].kind != kSpheres)
            continue;
         if (!sphereSet)
         {
            glUseProgram(mSphShadow.id);
            glUniformMatrix4fv(mSphShadow.viewProj, 1, GL_FALSE, st.viewProj.m);
            glUniform1f(mSphShadow.texelsPerUnit, 1.0f / texelWorld);
            glEnable(GL_PROGRAM_POINT_SIZE);
            sphereSet = true;
         }
         const Mat4 model = draw[i]->GetModelMatrix();
         const float scale = std::sqrt(model.m[0] * model.m[0] + model.m[1] * model.m[1] + model.m[2] * model.m[2]);
         glUniformMatrix4fv(mSphShadow.model, 1, GL_FALSE, model.m);
         glUniform1f(mSphShadow.radiusScale, mSlots[i].radiusScale * scale);
         // The slot's VAO feeds the camera passes per instance; here one vertex
         // per point, so its divisors go to 0 for this draw and back after.
         glBindVertexArray(mSlots[i].vao);
         for (unsigned int a = 0; a < 4; a++)
            glVertexAttribDivisor(a, 0);
         glDrawArrays(GL_POINTS, 0, mSlots[i].pointCount);
         for (unsigned int a = 0; a < 4; a++)
            glVertexAttribDivisor(a, 1);
      }
      if (sphereSet)
         glDisable(GL_PROGRAM_POINT_SIZE);
      glBindVertexArray(0);
      return st;
   }

   unsigned int HqRendererGl::Render(const HqFrame& f)
   {
      mTriangles = mDrawCalls = mSpheres = mSkipped = 0;
      if (f.draw == nullptr || !EnsurePrograms())
         return 0;

      // The state the 2D graph relies on, restored at the end (F4's cache will
      // replace these queries).
      GLint prevFbo = 0, prevViewport[4];
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      glGetIntegerv(GL_VIEWPORT, prevViewport);
      const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
      const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
      const GLboolean prevBlend = glIsEnabled(GL_BLEND);
      const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
      auto restore = [&]() {
         glBindVertexArray(0);
         glUseProgram(0);
         glDepthMask(GL_TRUE);
         glDepthFunc(GL_LESS);
         glBindSampler(kUnitShadow, 0);
         glBindSampler(kUnitShadowRaw, 0);
         for (int unit = kUnitLast; unit >= 0; unit--)
         {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, 0);
         }
         glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
         glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
         if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
         if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
         if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
         if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
      };
      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_BLEND);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_CULL_FACE);
      glDisable(GL_STENCIL_TEST);
      glDisable(GL_RASTERIZER_DISCARD);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

      // Tables and environment maps first: their own passes, own targets.
      EnsureDfg();
      const bool ibl = EnsureIbl(f);
      const bool envBackground = f.envTex != 0 && f.envAsBackground;
      if (!EnsureTargets(f.width, f.height))
      {
         restore();
         return 0;
      }
      Upload(*f.draw);
      const std::vector<IGeometrySource*>& draw = *f.draw;
      // S4: the shadow map first (its own target), then the frame.
      ShadowState shadow = UpdateShadows(f, draw);
      shadow.contact = (shadow.light >= 0 && f.contactShadows) ? 1 : 0;
      shadow.proj[0] = f.proj.m[10];
      shadow.proj[1] = f.proj.m[14];
      shadow.proj[2] = f.proj.m[15] == 1.0f ? 1.0f : 0.0f;
      shadow.proj[3] = kContactDistance;
      bool anySpheres = false;
      for (size_t i = 0; i < draw.size(); i++)
         anySpheres = anySpheres || (mSlots[i].drawable && mSlots[i].kind == kSpheres);
      const bool depthCopy = shadow.contact || anySpheres;

      glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
      glViewport(0, 0, f.width, f.height);
      glDepthMask(GL_TRUE);
      const GLenum both[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
      glDrawBuffers(2, both);
      const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      const float farDepth = 1.0f;
      glClearBufferfv(GL_COLOR, 0, zero);
      glClearBufferfv(GL_COLOR, 1, zero);
      glClearBufferfv(GL_DEPTH, 0, &farDepth);

      const Mat4 viewProj = Mat4::Multiply(f.proj, f.view);
      // Sphere rays: orthographic when the projection has no perspective row;
      // the camera looks down -Z of its view matrix.
      const int ortho = f.proj.m[15] == 1.0f ? 1 : 0;
      const float viewDir[3] = { -f.view.m[2], -f.view.m[6], -f.view.m[10] };
      auto sphereSetup = [&](int prepass) {
         glUseProgram(mSph.id);
         glUniformMatrix4fv(mSph.viewProj, 1, GL_FALSE, viewProj.m);
         glUniformMatrix4fv(mSph.view, 1, GL_FALSE, f.view.m);
         glUniform1i(mSph.ortho, ortho);
         glUniform3fv(mSph.viewDir, 1, viewDir);
         glUniform1i(mSph.prepass, prepass);
         glUniform1i(mSph.earlyDepth, prepass == 0 && depthCopy ? 1 : 0);
         mSph.lit.SetFrame(f, ibl);
         mSph.lit.SetShadow(shadow);
      };
      auto drawSpheres = [&](size_t i, bool shade) {
         const Mat4 model = draw[i]->GetModelMatrix();
         // The model's scale on the radius (its first column's length).
         const float scale = std::sqrt(model.m[0] * model.m[0] + model.m[1] * model.m[1] + model.m[2] * model.m[2]);
         glUniformMatrix4fv(mSph.model, 1, GL_FALSE, model.m);
         glUniform1f(mSph.radiusScale, mSlots[i].radiusScale * scale);
         if (shade)
            mSph.lit.SetMaterial(draw[i]->GetMaterial());
         else
            glUniform1f(mSph.lit.roughness, draw[i]->GetMaterial().roughness);
         glBindVertexArray(mSlots[i].vao);
         glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, mSlots[i].pointCount);
      };

      glEnable(GL_DEPTH_TEST);
      glCullFace(GL_BACK);
      glFrontFace(GL_CCW);

      // --- prepass: depth, view normal + roughness ---------------------
      {
         GLUtil::DebugScope group("Render 3D HQ: prepass");
         const GLenum normalOnly[2] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
         glDrawBuffers(2, normalOnly);
         glDepthFunc(GL_LESS);
         glEnable(GL_CULL_FACE);
         glUseProgram(mPre.id);
         glUniformMatrix4fv(mPre.viewProj, 1, GL_FALSE, viewProj.m);
         glUniformMatrix4fv(mPre.view, 1, GL_FALSE, f.view.m);
         for (size_t i = 0; i < draw.size(); i++)
         {
            if (!mSlots[i].drawable || mSlots[i].kind == kSpheres)
               continue;
            const Mat4 model = draw[i]->GetModelMatrix();
            float normalMatrix[9];
            model.NormalMatrix(normalMatrix);
            glUniformMatrix4fv(mPre.model, 1, GL_FALSE, model.m);
            glUniformMatrix3fv(mPre.normalMatrix, 1, GL_FALSE, normalMatrix);
            glUniform1f(mPre.roughness, draw[i]->GetMaterial().roughness);
            glBindVertexArray(mSlots[i].vao);
            glDrawElements(GL_TRIANGLES, mSlots[i].indexCount, GL_UNSIGNED_INT, nullptr);
         }
         // Impostor quads face the camera whatever their winding.
         glDisable(GL_CULL_FACE);
         bool setUp = false;
         for (size_t i = 0; i < draw.size(); i++)
         {
            if (!mSlots[i].drawable || mSlots[i].kind != kSpheres)
               continue;
            if (!setUp)
               sphereSetup(1);
            setUp = true;
            drawSpheres(i, false);
         }
      }

      // S4: contact shadows and the spheres' early-out read this frame's
      // depth from a copy.
      if (depthCopy)
      {
         glBindFramebuffer(GL_READ_FRAMEBUFFER, mFbo);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mDepthCopyFbo);
         glBlitFramebuffer(0, 0, f.width, f.height, 0, 0, f.width, f.height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
         glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
      }

      const GLenum colourOnly[2] = { GL_COLOR_ATTACHMENT0, GL_NONE };
      glDrawBuffers(2, colourOnly);
      glDepthMask(GL_FALSE);

      // --- HDRI background, where the prepass left the far plane --------
      if (envBackground)
      {
         GLUtil::DebugScope group("Render 3D HQ: background");
         glDepthFunc(GL_LEQUAL);
         glUseProgram(mBg.id);
         const Mat4 invViewProj = Mat4::Inverse(viewProj);
         glUniformMatrix4fv(mBg.invViewProj, 1, GL_FALSE, invViewProj.m);
         glUniform3fv(mBg.camPos, 1, f.eye);
         glUniform1f(mBg.envRotation, f.envRotation);
         glUniform1f(mBg.envIntensity, f.envIntensity);
         glUniform1f(mBg.exposure, f.exposure);
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, f.envTex);
         glBindVertexArray(mEmptyVao);
         glDrawArrays(GL_TRIANGLES, 0, 3);
      }

      // --- shading: one fragment per pixel (depth equal to the prepass) --
      {
         GLUtil::DebugScope group("Render 3D HQ: shading");
         glDepthFunc(GL_LEQUAL);
         glActiveTexture(GL_TEXTURE0 + kUnitIrr);
         glBindTexture(GL_TEXTURE_2D, ibl ? mIrr : 0);
         glActiveTexture(GL_TEXTURE0 + kUnitPref);
         glBindTexture(GL_TEXTURE_2D, ibl ? mPref : 0);
         glActiveTexture(GL_TEXTURE0 + kUnitDfg);
         glBindTexture(GL_TEXTURE_2D, mDfg);
         // S4: the shadow map on two units (compared / raw), the depth copy.
         const unsigned int shadowTex = shadow.light >= 0 ? mShadowTex : 0;
         glActiveTexture(GL_TEXTURE0 + kUnitShadow);
         glBindTexture(GL_TEXTURE_2D, shadowTex);
         glBindSampler(kUnitShadow, mCmpSampler);
         glActiveTexture(GL_TEXTURE0 + kUnitShadowRaw);
         glBindTexture(GL_TEXTURE_2D, shadowTex);
         glBindSampler(kUnitShadowRaw, mRawSampler);
         glActiveTexture(GL_TEXTURE0 + kUnitSceneDepth);
         glBindTexture(GL_TEXTURE_2D, depthCopy ? mDepthCopy : 0);

         glEnable(GL_CULL_FACE);
         glUseProgram(mSurf.id);
         glUniformMatrix4fv(mSurf.viewProj, 1, GL_FALSE, viewProj.m);
         mSurf.lit.SetFrame(f, ibl);
         mSurf.lit.SetShadow(shadow);
         for (size_t i = 0; i < draw.size(); i++)
         {
            if (!mSlots[i].drawable || mSlots[i].kind == kSpheres)
               continue;
            IGeometrySource* src = draw[i];
            const Mat4 model = src->GetModelMatrix();
            float normalMatrix[9];
            model.NormalMatrix(normalMatrix);
            const Material m = src->GetMaterial();
            glUniformMatrix4fv(mSurf.model, 1, GL_FALSE, model.m);
            glUniformMatrix3fv(mSurf.normalMatrix, 1, GL_FALSE, normalMatrix);
            glUniform1i(mSurf.shading, m.shading);
            mSurf.lit.SetMaterial(m);
            int maps = 0;
            for (int map = 0; map < kMapCount; map++)
            {
               const unsigned int tex = src->GetMaterialTexture(map);
               glActiveTexture(GL_TEXTURE0 + kUnitMap0 + map);
               glBindTexture(GL_TEXTURE_2D, tex);
               if (tex != 0)
                  maps |= 1 << map;
            }
            glUniform1i(mSurf.maps, maps);
            glUniform1i(mSurf.normalBump, m.normalBump);
            glUniform1f(mSurf.normalStrength, m.normalStrength);
            const MappingTransform mt = src->GetMappingTransform();
            glUniform1i(mSurf.mapSpace, mt.space);
            glUniform3fv(mSurf.mapTranslate, 1, mt.translate);
            glUniform3fv(mSurf.mapRotate, 1, mt.rotate);
            glUniform3fv(mSurf.mapScale, 1, mt.scale);
            glUniform1f(mSurf.triplanarBlend, mt.triplanarBlend);
            glUniform3fv(mSurf.objLo, 1, mSlots[i].lo);
            glUniform3fv(mSurf.objHi, 1, mSlots[i].hi);
            glBindVertexArray(mSlots[i].vao);
            glDrawElements(GL_TRIANGLES, mSlots[i].indexCount, GL_UNSIGNED_INT, nullptr);
            mTriangles += (size_t)mSlots[i].indexCount / 3;
            mDrawCalls++;
         }

         glDisable(GL_CULL_FACE);
         bool setUp = false;
         for (size_t i = 0; i < draw.size(); i++)
         {
            if (!mSlots[i].drawable || mSlots[i].kind != kSpheres)
               continue;
            if (!setUp)
               sphereSetup(0);
            setUp = true;
            drawSpheres(i, true);
            mSpheres += (size_t)mSlots[i].pointCount;
            mDrawCalls++;
         }
      }

      glDrawBuffers(1, both);
      restore();

      // HDR -> RGBA8: bloom, tonemap, sRGB. With the HDRI behind, everything
      // is tonemapped; otherwise the plain background shows where nothing was
      // drawn (depth 1), as in Render 3D.
      return mPost.Finish(mColor, f.width, f.height, f.tonemap, f.bloom, f.bloomThreshold, f.bloomRadius, false,
                          envBackground ? 0u : mDepth, f.bg);
   }

   void HqRendererGl::Release()
   {
      for (Slot& s : mSlots)
      {
         if (s.vao != 0) glDeleteVertexArrays(1, &s.vao);
         if (s.vbo != 0) glDeleteBuffers(1, &s.vbo);
         if (s.ibo != 0) glDeleteBuffers(1, &s.ibo);
      }
      mSlots.clear();
      unsigned int* textures[] = { &mColor, &mNormal, &mDepth, &mPref, &mIrr, &mDfg, &mShadowTex, &mDepthCopy };
      for (unsigned int* t : textures)
      {
         if (*t != 0)
            glDeleteTextures(1, t);
         *t = 0;
      }
      if (mFbo != 0) glDeleteFramebuffers(1, &mFbo);
      if (mIblFbo != 0) glDeleteFramebuffers(1, &mIblFbo);
      if (mShadowFbo != 0) glDeleteFramebuffers(1, &mShadowFbo);
      if (mDepthCopyFbo != 0) glDeleteFramebuffers(1, &mDepthCopyFbo);
      if (mCmpSampler != 0) glDeleteSamplers(1, &mCmpSampler);
      if (mRawSampler != 0) glDeleteSamplers(1, &mRawSampler);
      mShadowFbo = mDepthCopyFbo = mCmpSampler = mRawSampler = 0;
      if (mEmptyVao != 0) glDeleteVertexArrays(1, &mEmptyVao);
      unsigned int* programs[] = { &mPre.id, &mSurf.id, &mSph.id, &mShadowProg.id, &mSphShadow.id, &mBg.id, &mIbl.id };
      for (unsigned int* p : programs)
      {
         if (*p != 0)
            glDeleteProgram(*p);
         *p = 0;
      }
      mFbo = mIblFbo = mEmptyVao = 0;
   }
}

std::unique_ptr<HqRenderer> CreateHqRenderer(std::string& why)
{
   why.clear();
   return std::make_unique<HqRendererGl>();
}

bool HqModuleBuilt()
{
   return true;
}
