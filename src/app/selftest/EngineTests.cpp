// Engine vs Render 3D (INFINITE_ENGINETEST): the same scene through the GL raster path and the Metal engine, compared
// pixel by pixel and timed. R1's gate: FLIP-class error <= 0.02, >= 1.5x faster, >= 8 lights (docs/plans/realtime-engine).
#include "app/AppShared.h"
#include "engine/rhi/Rhi.h"
#include "engine/MusicRig.h"
#include "core/field/FieldLex.h"
#include "core/field/FieldParse.h"
#include "core/field/GlslBackend.h"
#include "core/field/MslBackend.h"
#include "core/field/MaterialRegistry.h"
#include "nodes/FieldPixelNode.h"
#include "nodes/Render3DPathTrace.h"

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace app
{
namespace
{
   struct Probe
   {
      GeometryNode geo;
   };

   engine::MeshData ToMeshData(const Mesh& m)
   {
      engine::MeshData d;
      d.posNormal.reserve(m.vertices.size() * 6);
      for (const Vertex& v : m.vertices)
      {
         d.posNormal.insert(d.posNormal.end(), { v.px, v.py, v.pz, v.nx, v.ny, v.nz });
      }
      d.indices.assign(m.indices.begin(), m.indices.end());
      return d;
   }

   int ShapeIndex(const char* name)
   {
      const auto& names = GeometryNode::ShapeNames();
      for (size_t i = 0; i < names.size(); i++)
         if (names[i] == name)
            return (int)i;
      return 1;
   }

   bool ReadRect(unsigned int tex, int w, int h, std::vector<float>& px)
   {
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, tex, 0);
      const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
      px.resize((size_t)w * h * 4);
      if (ok)
         glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, px.data());
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &fbo);
      return ok;
   }


   // ---- Field Pixel through both emitters (OPUS_PLAN row 1.1) ----
   bool LowerPreset(const char* code, Field::PixelIRProgram& ir, std::string& err)
   {
      std::vector<Field::Token> tokens;
      Field::FieldError fe;
      Field::AstNodePtr ast;
      if (!Field::Lex(code, tokens, fe) || !Field::ParseProgram(tokens, ast, fe) || !Field::LowerPixelProgramToIR(ast, ir, fe))
      {
         err = "line " + std::to_string(fe.span.line) + ": " + fe.message;
         return false;
      }
      return true;
   }

   // The fixed test inputs both sides get: 256x256, t 1.37, dt 1/60, frame 42, age 0, unconnected source,
   // display pass, state bank at its initial values, hoisted value i = 0.25 + 0.125 i, params at their defaults.
   constexpr float kFpT = 1.37f, kFpDt = 1.0f / 60.0f, kFpFrame = 42.0f;
   float HoistValue(int i) { return 0.25f + 0.125f * (float)i; }

   void BankInit(const Field::PixelIRProgram& ir, float init[4])
   {
      for (int i = 0; i < 4; i++)
         init[i] = 0.0f;
      for (size_t i = 0; i < ir.declaredStates.size() && i < 4; i++)
         if (!ir.declaredStates[i].initialValues.empty())
            init[i] = ir.declaredStates[i].initialValues[0];
   }

   bool GlFieldPixel(const Field::PixelIRProgram& ir, int n, std::vector<float>& px, std::string& err)
   {
      Field::GlslEmitResult ge = Field::EmitGlsl(ir);
      if (!ge.error.empty()) { err = ge.error; return false; }
      unsigned int prog = GLUtil::CompileProgram(ge.source.c_str(), &err);
      if (prog == 0) return false;
      GLUtil::Fbo out, bank;
      GLUtil::EnsureFbo(out, n, n, GL_RGBA32F, "FieldMslTest");
      GLUtil::EnsureFbo(bank, n, n, GL_RGBA32F, "FieldMslTest");
      float init[4];
      BankInit(ir, init);
      GLint oldFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, bank.fbo);
      glClearColor(init[0], init[1], init[2], init[3]);
      glClear(GL_COLOR_BUFFER_BIT);
      GLuint black = 0;
      glGenTextures(1, &black);
      glBindTexture(GL_TEXTURE_2D, black);
      const unsigned char zero[4] = { 0, 0, 0, 0 };
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      GLUtil::RunShaderPass(out, prog, [&]()
      {
         auto loc = [&](const char* nm) { return glGetUniformLocation(prog, nm); };
         glUniform2f(loc("fld_res"), (float)n, (float)n);
         glUniform1f(loc("fld_t"), kFpT);
         glUniform1f(loc("fld_dt"), kFpDt);
         glUniform1i(loc("fld_frame"), (int)kFpFrame);
         glUniform1f(loc("fld_age"), 0.0f);
         glUniform1f(loc("fld_srcAlpha"), 0.0f);
         glUniform1i(loc("fld_outMode"), 1);
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, bank.tex);
         glUniform1i(loc("fld_s_bank0"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, black);
         glUniform1i(loc("fld_srcTex"), 1);
         glActiveTexture(GL_TEXTURE0);
         for (const auto& u : ge.uniforms)
         {
            const GLint l = loc(u.name.c_str());
            const float v = u.hoistedIndex >= 0 ? HoistValue(u.hoistedIndex) : (float)ir.declaredParams[u.paramIndex].defaultValue;
            if (u.type.lanes == 2) glUniform2f(l, v, v);
            else if (u.type.lanes == 3) glUniform3f(l, v, v, v);
            else if (u.type.lanes == 4) glUniform4f(l, v, v, v, v);
            else if (u.type.kind == Field::DataType::Int) glUniform1i(l, (int)v);
            else glUniform1f(l, v);
         }
      });
      px.resize((size_t)n * n * 4);
      glBindFramebuffer(GL_FRAMEBUFFER, out.fbo);
      glReadPixels(0, 0, n, n, GL_RGBA, GL_FLOAT, px.data());
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
      glDeleteTextures(1, &black);
      glDeleteProgram(prog);
      GLUtil::DestroyFbo(out);
      GLUtil::DestroyFbo(bank);
      return true;
   }

   bool MetalFieldPixel(rhi::Device& dev, const Field::PixelIRProgram& ir, int n, std::vector<float>& px, std::string& err)
   {
      Field::MslEmitResult me = Field::EmitMsl(ir);
      if (!me.error.empty()) { err = me.error; return false; }
      std::vector<float> slots((size_t)me.uniformSlotCount * 4, 0.0f);
      const float fixed[8] = { (float)n, (float)n, kFpT, kFpDt, kFpFrame, 0.0f, 0.0f, 1.0f };
      std::copy(fixed, fixed + 8, slots.begin());
      for (const auto& u : me.uniforms)
      {
         const float v = u.hoistedIndex >= 0 ? HoistValue(u.hoistedIndex) : (float)ir.declaredParams[u.paramIndex].defaultValue;
         for (int k = 0; k < 4; k++)
            slots[(size_t)u.location * 4 + k] = v;
      }
      float init[4];
      BankInit(ir, init);
      if (!dev.RunFieldPixel(me.source, n, n, slots, init, px, err))
      {
         static int dumped = 0;
         if (dumped++ == 0)
            printf("%s\n", me.source.c_str());
         return false;
      }
      return true;
   }

   void FieldMslRows(rhi::Device& dev, const std::function<void(bool, const char*, double, double)>& check)
   {
      const auto& presets = FieldPixelNode::Presets();
      int compiled = 0;
      for (const auto& p : presets)
      {
         Field::PixelIRProgram ir;
         std::string err;
         bool ok = LowerPreset(p.code, ir, err);
         if (ok)
         {
            Field::MslEmitResult me = Field::EmitMsl(ir);
            ok = me.error.empty();
            err = me.error;
            std::vector<float> none;
            if (ok)
               ok = dev.RunFieldPixel(me.source, 0, 0, {}, nullptr, none, err);
         }
         if (ok)
            compiled++;
         else
            printf("  field msl: %s: %s\n", p.name, err.c_str());
      }
      check(compiled == (int)presets.size(), "1.1: every Field Pixel preset compiles with the Metal compiler",
            (double)compiled, (double)presets.size());

      const char* picks[8] = { "Default (UV Gradient)", "Noise Texture", "Organic Blobs / Metaballs", "Chladni Nodal Pattern",
                               "Quasicrystal Kaleidoscope", "Voronoi Cells", "Nebula / Galaxy", "Reaction Diffusion (Gray-Scott)" };
      const int n = 256;
      double worstAll = 0.0;
      int rendered = 0;
      for (const char* name : picks)
      {
         const FieldPixelNode::Preset* pre = nullptr;
         for (const auto& p : presets)
            if (std::strcmp(p.name, name) == 0)
               pre = &p;
         Field::PixelIRProgram ir;
         std::string err;
         std::vector<float> a, b;
         if (!pre || !LowerPreset(pre->code, ir, err) || !GlFieldPixel(ir, n, a, err) || !MetalFieldPixel(dev, ir, n, b, err))
         {
            printf("  field msl: %s: not rendered (%s)\n", name, pre ? err.c_str() : "no such preset");
            worstAll = 1.0;
            continue;
         }
         double worst = 0.0;
         for (size_t i = 0; i < a.size(); i++)
         {
            const bool na = std::isnan(a[i]), nb = std::isnan(b[i]);
            const double d = (na || nb) ? (na == nb ? 0.0 : 1.0)
                                        : std::fabs((double)std::clamp(a[i], 0.0f, 1.0f) - (double)std::clamp(b[i], 0.0f, 1.0f));
            worst = std::max(worst, d);
         }
         printf("  field msl: %-34s max diff %.6f (%.2f/255)\n", name, worst, worst * 255.0);
         worstAll = std::max(worstAll, worst);
         rendered++;
      }
      check(rendered == 8 && worstAll <= 1.0 / 255.0, "1.1: 8 presets, GLSL vs MSL at 256x256, max diff <= 1/255",
            worstAll * 255.0, 1.0);
   }

   double NowMs()
   {
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
   }
}

bool RunEngineTest()
{
   printf("[ENGINE TEST] Metal raster vs Render 3D\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };
   auto dev = rhi::CreateDevice();
#if defined(__APPLE__)
   if (!dev)
   {
      printf("  [FAIL] no Metal device\nENGINETEST FAIL\n");
      return false;
   }
   FieldMslRows(*dev, check);
   const int W = 1920, H = 1080;
   const int nLights = getenv("INFINITE_ENGINETEST_LIGHTS") ? atoi(getenv("INFINITE_ENGINETEST_LIGHTS")) : 8;

   // ---- the scene, once, for both renderers
   GeometryNode floorG, cubeG, sphereG, torusG;
   floorG.shape = ShapeIndex("Cube");  floorG.posY = -1.3f; floorG.scaleX = 8.0f; floorG.scaleY = 0.1f; floorG.scaleZ = 8.0f;
   floorG.color[0] = 0.6f; floorG.color[1] = 0.6f; floorG.color[2] = 0.62f; floorG.metallic = 0.0f; floorG.roughness = 0.7f;
   cubeG.shape = ShapeIndex("Cube");   cubeG.posX = -1.8f; cubeG.rotY = 0.5f; cubeG.color[0] = 0.9f; cubeG.color[1] = 0.3f; cubeG.color[2] = 0.2f;
   cubeG.metallic = 0.0f; cubeG.roughness = 0.4f;
   sphereG.shape = ShapeIndex("Sphere"); sphereG.posX = 0.2f; sphereG.color[0] = 0.95f; sphereG.color[1] = 0.8f; sphereG.color[2] = 0.3f;
   sphereG.metallic = 1.0f; sphereG.roughness = 0.25f;
   torusG.shape = ShapeIndex("Torus"); torusG.posX = 2.2f; torusG.rotX = 0.9f; torusG.color[0] = 0.3f; torusG.color[1] = 0.6f; torusG.color[2] = 0.95f;
   torusG.metallic = 0.3f; torusG.roughness = 0.5f;
   GeometryNode* nodes[4] = { &floorG, &cubeG, &sphereG, &torusG };

   const float eye[3] = { 3.5f, 2.2f, 6.0f }, target[3] = { 0.2f, -0.2f, 0.0f }, up[3] = { 0, 1, 0 };
   const Mat4 view = Mat4::LookAt(eye, target, up);
   const Mat4 proj = Mat4::Perspective(45.0f * 3.14159265f / 180.0f, (float)W / (float)H, 0.05f, 100.0f);

   Render3DNode::Rig rig;
   rig.hasView = true; rig.view = view; rig.eye[0] = eye[0]; rig.eye[1] = eye[1]; rig.eye[2] = eye[2];
   rig.hasProj = true; rig.proj = proj;
   rig.hasLights = true;
   const float cols[8][3] = { { 1, 0.95f, 0.9f }, { 1, 0.3f, 0.2f }, { 0.2f, 0.4f, 1 }, { 0.3f, 1, 0.4f },
                              { 1, 0.8f, 0.2f }, { 0.8f, 0.3f, 1 }, { 0.2f, 1, 0.9f }, { 1, 0.5f, 0.7f } };
   engine::Frame frame;
   for (int i = 0; i < nLights; i++)
   {
      Render3DNode::RigLight l;
      engine::Light e;
      if (i == 0)
      {
         l.type = 2; l.vec[0] = 0.5f; l.vec[1] = 1.0f; l.vec[2] = 0.4f; l.intensity = 2.2f;
      }
      else
      {
         l.type = i % 3 == 2 ? 4 : 1;
         const float a = 6.2831853f * (float)i / (float)nLights;
         l.vec[0] = 3.2f * std::cos(a); l.vec[1] = 1.2f + 0.3f * (float)(i % 3); l.vec[2] = 3.2f * std::sin(a);
         l.intensity = 14.0f; l.falloff = -1.0f;
         l.aim[0] = -l.vec[0]; l.aim[1] = -l.vec[1]; l.aim[2] = -l.vec[2];
         l.spotAngle = 40.0f; l.spotPenumbra = 0.3f;
      }
      for (int k = 0; k < 3; k++) l.color[k] = cols[i][k];
      rig.lights.push_back(l);
      e.type = (engine::LightType)l.type;
      std::memcpy(e.vec, l.vec, 12); std::memcpy(e.aim, l.aim, 12); std::memcpy(e.colorSrgb, l.color, 12);
      e.intensity = l.intensity; e.falloff = l.falloff; e.spotAngleDeg = l.spotAngle; e.spotPenumbra = l.spotPenumbra;
      e.diffuse = l.diffuse; e.specular = l.specular; e.cutoff = l.cutoff;
      frame.lights.push_back(e);
   }

   Render3DNode r;
   for (int i = 0; i < 4; i++)
      r.geometry[i] = nodes[i];
   r.width = (float)W; r.height = (float)H; r.samples = 0; r.hdr = false; r.tonemap = 1; r.shadowsEnabled = getenv("INFINITE_ENGINETEST_SHADOWS") != nullptr;
   r.rig = &rig;

   std::vector<engine::MeshData> meshes;
   for (int i = 0; i < 4; i++)
   {
      meshes.push_back(ToMeshData(nodes[i]->GetMesh()));
      engine::Instance in;
      in.mesh = i;
      const Mat4 m = nodes[i]->GetModelMatrix();
      std::memcpy(in.model, m.m, 64);
      float n3[9];
      m.NormalMatrix(n3);
      float* n4 = in.normalMatrix;
      for (int c = 0; c < 3; c++)
         for (int rr = 0; rr < 3; rr++)
            n4[c * 4 + rr] = n3[c * 3 + rr];
      const Material mat = nodes[i]->GetMaterial();
      for (int k = 0; k < 3; k++) in.baseSrgb[k] = mat.color[k];
      in.metallic = mat.metallic; in.roughness = mat.roughness;
      frame.instances.push_back(in);
   }
   const Mat4 vp = Mat4::Multiply(proj, view);
   std::memcpy(frame.viewProj, vp.m, 64);
   std::memcpy(frame.eye, eye, 12);
   frame.exposure = r.exposure; frame.tonemap = 1; frame.rim = r.rimIntensity;
   std::memcpy(frame.ambientSrgb, r.ambientColor, 12);
   std::memcpy(frame.envSkySrgb, r.envSky, 12); std::memcpy(frame.envHorizonSrgb, r.envHorizon, 12);
   std::memcpy(frame.envGroundSrgb, r.envGround, 12);
   frame.envIntensity = r.envIntensity;
   std::memcpy(frame.clearSrgb, r.bgColor, 12);
   frame.shadow.enabled = r.shadowsEnabled;
   frame.rtShadows = getenv("INFINITE_ENGINETEST_RT") != nullptr;
   frame.gi = getenv("INFINITE_ENGINETEST_GI") != nullptr; if (const char* dn = getenv("INFINITE_ENGINETEST_DENOISE")) frame.giDenoise = atoi(dn); if (const char* gr = getenv("INFINITE_ENGINETEST_GIRAYS")) frame.giRays = atoi(gr);
   frame.rtReflections = getenv("INFINITE_ENGINETEST_REFL") != nullptr;
   frame.shadow.size = 2048; frame.shadow.bias = r.shadowBias; frame.shadow.strength = r.shadowStrength; frame.shadow.softness = r.shadowSoftness;

   auto raster = dev->CreateRasterRenderer();
   check(raster != nullptr, "Metal raster renderer builds", 0, 0);
   auto img = dev->CreateSharedImage(W, H, rhi::Format::RGBA8);
   check(img != nullptr && raster && raster->UploadMeshes(meshes), "meshes upload", (double)meshes.size(), 0);
   if (!raster || !img)
   {
      printf("ENGINETEST FAIL\n");
      return false;
   }

   // ---- pictures
   int fnum = 9000;
   unsigned int scratch = 0;
   std::vector<float> gl, mt;
   if (getenv("INFINITE_ENGINETEST_PASS")) r.renderPass = atoi(getenv("INFINITE_ENGINETEST_PASS"));
   // First cook uploads the meshes (no shadow bounds yet); nudge exposure so the next one draws again.
   r.CookIfNeeded(++fnum);
   r.exposure = 1.001f;
   r.CookIfNeeded(++fnum);
   r.exposure = 1.0f;
   r.CookIfNeeded(++fnum);
   printf("  GL shadow size %d, shadows enabled %d\n", r.ActiveShadowSize(), (int)r.shadowsEnabled);
   check(GLUtil::ReadTexturePixels(scratch, r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight(), gl), "GL picture", (double)r.GetOutputWidth(), (double)r.GetOutputHeight());
   double gpuMs = 0.0;
   for (int i = 0; i < (frame.gi ? 59 : 0); i++) { frame.frameIndex++; raster->Render(*img, frame, nullptr); }
   frame.frameIndex++;
   check(raster->Render(*img, frame, &gpuMs), "Metal picture", gpuMs, 0);
   check(ReadRect(img->GlTexture(), W, H, mt), "Metal picture reads in GL", 0, 0);
   if (gl.size() != mt.size())
   {
      printf("  [FAIL] picture sizes differ\nENGINETEST FAIL\n");
      return false;
   }
   double sum = 0.0, worst = 0.0;
   size_t big = 0, lit = 0;
   std::vector<float> errs;
   errs.reserve(gl.size() / 4);
   for (size_t i = 0; i < gl.size(); i += 4)
   {
      double e = 0.0;
      for (int c = 0; c < 3; c++)
         e = std::max(e, (double)std::fabs(gl[i + c] - mt[i + c]));
      errs.push_back((float)e);
      sum += e;
      worst = std::max(worst, e);
      if (e > 0.04) big++;
      if (gl[i] + gl[i + 1] + gl[i + 2] > 0.3f) lit++;
   }
   std::sort(errs.begin(), errs.end());
   const double mae = sum / (double)errs.size(), p99 = errs[(size_t)((double)errs.size() * 0.99)];
   printf("  MAE %.5f  p99 %.5f  max %.4f  >0.04: %.3f%% of pixels\n", mae, p99, worst, 100.0 * (double)big / (double)errs.size());
   check(lit > (size_t)W * H / 20, "scene is on screen", (double)lit, 0);
   check(mae <= 0.02, "mean error vs GL <= 0.02 (R1 gate)", mae, 0.02);

   // ---- 1.3: material programs run after the maps in R (GL), L (Metal raster) and T (Metal tracer)
   {
      const char* kRed = "base = vec3(1.0, 0.0, 0.0)\nmetallic = 0.0\nroughness = 1.0";
      // changed pixels between a and b, and whether b reads red there
      auto redWhereChanged = [](const std::vector<float>& a, const std::vector<float>& b, size_t& changed) -> bool
      {
         changed = 0;
         double sr = 0, sg = 0, sb = 0;
         for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
         {
            const float d = std::max({ std::fabs(a[i] - b[i]), std::fabs(a[i + 1] - b[i + 1]), std::fabs(a[i + 2] - b[i + 2]) });
            if (d < 0.1f) continue;
            changed++;
            sr += b[i]; sg += b[i + 1]; sb += b[i + 2];
         }
         if (changed == 0) return false;
         sr /= (double)changed; sg /= (double)changed; sb /= (double)changed;
         printf("    changed %zu px, mean rgb %.3f %.3f %.3f\n", changed, sr, sg, sb);
         return sr > sg + 0.1 && sr > sb + 0.1;
      };
      std::string ferr;
      const int id = Field::RegisterMaterialProgram(kRed, {}, ferr);
      check(id > 0, "1.3: material program registers", (double)id, 0);
      check(Field::RegisterMaterialProgram(kRed, {}, ferr) == id, "1.3: same source, same id", 0, 0);

      // L: the sphere (instance 2) gets the program
      engine::Frame f2 = frame;
      f2.instances[2].fieldProgram = id;
      f2.fieldMsl = Field::BuildMaterialLibrary({ id }, Field::MaterialLang::Msl, f2.fieldMslKey);
      const int builds0 = raster->FieldLibraryBuilds();
      std::vector<float> lp;
      const bool lok = raster->Render(*img, f2, nullptr) && raster->Render(*img, f2, nullptr) && ReadRect(img->GlTexture(), W, H, lp);
      size_t ch = 0;
      check(lok && redWhereChanged(mt, lp, ch) && ch > (size_t)W * H / 100, "1.3 L: program turns the sphere red", (double)ch, 0);
      check(raster->FieldLibraryBuilds() - builds0 == 1, "1.3 L: one library build for one program set", raster->FieldLibraryBuilds() - builds0, 1);
      // and back: no program is the picture it was
      std::vector<float> back;
      bool bok = raster->Render(*img, frame, nullptr) && ReadRect(img->GlTexture(), W, H, back) && back.size() == mt.size();
      double bworst = 0.0;
      for (size_t i = 0; bok && i < back.size(); i++) bworst = std::max(bworst, (double)std::fabs(back[i] - mt[i]));
      check(bok && bworst <= 1.0 / 255.0, "1.3 L: program removed, picture unchanged", bworst * 255.0, 1.0);

      // T: the tracer reads the same program
      std::vector<float> t0, t1;
      const bool tok = raster->PathTrace(*img, frame, 4, 3, true, nullptr) && ReadRect(img->GlTexture(), W, H, t0) &&
                       raster->PathTrace(*img, f2, 4, 3, true, nullptr) && ReadRect(img->GlTexture(), W, H, t1);
      check(tok && redWhereChanged(t0, t1, ch) && ch > (size_t)W * H / 100, "1.3 T: program turns the sphere red", (double)ch, 0);

      // R: Field Pixel (Material domain) -> Material `field` pin -> Render 3D (GL)
      FieldPixelNode fp;
      fp.code = kRed;
      fp.domain = 1;
      const bool applied = fp.Apply();
      check(applied && fp.MaterialProgramId() == id, "1.3: Field Pixel in Material domain registers its program", fp.MaterialProgramId(), id);
      MaterialNode mat;
      mat.input = &sphereG;
      Render3DNode r2;
      r2.geometry[0] = &floorG; r2.geometry[1] = &cubeG; r2.geometry[2] = &mat; r2.geometry[3] = &torusG;
      r2.width = (float)W; r2.height = (float)H; r2.samples = 0; r2.hdr = false; r2.tonemap = 1; r2.shadowsEnabled = r.shadowsEnabled;
      r2.rig = &rig;
      std::vector<float> ra, rb;
      for (int k = 0; k < 3; k++) { r2.exposure = 1.0f + 0.001f * (float)k; r2.CookIfNeeded(++fnum); }
      const bool aok = GLUtil::ReadTexturePixels(scratch, r2.GetOutputTexture(), r2.GetOutputWidth(), r2.GetOutputHeight(), ra);
      mat.FieldInput().Connect(&fp);
      check(mat.GetMaterial().fieldProgram == id, "1.3: Material node carries the program id", mat.GetMaterial().fieldProgram, id);
      for (int k = 0; k < 3; k++) { r2.exposure = 1.01f + 0.001f * (float)k; r2.CookIfNeeded(++fnum); }
      const bool rok = aok && GLUtil::ReadTexturePixels(scratch, r2.GetOutputTexture(), r2.GetOutputWidth(), r2.GetOutputHeight(), rb);
      check(rok && redWhereChanged(ra, rb, ch) && ch > (size_t)W * H / 100, "1.3 R: program turns the sphere red", (double)ch, 0);

      // 1.4: a one-frame R render (fresh Render 3D, Field Pixel never applied) has the program on its first cook
      FieldPixelNode fp4;
      fp4.code = "base = vec3(1.0, 0.0, 0.0)\nmetallic = 0.0\nroughness = 0.75";
      fp4.domain = 1;
      MaterialNode mat4;
      mat4.input = &sphereG;
      mat4.FieldInput().Connect(&fp4);
      Render3DNode r4;
      r4.geometry[0] = &floorG; r4.geometry[1] = &cubeG; r4.geometry[2] = &mat4; r4.geometry[3] = &torusG;
      r4.width = (float)W; r4.height = (float)H; r4.samples = 0; r4.hdr = false; r4.tonemap = 1; r4.shadowsEnabled = r.shadowsEnabled;
      r4.rig = &rig;
      r4.exposure = 1.0f;
      r4.CookIfNeeded(++fnum);
      std::vector<float> r4px;
      const bool r4ok = GLUtil::ReadTexturePixels(scratch, r4.GetOutputTexture(), r4.GetOutputWidth(), r4.GetOutputHeight(), r4px);
      check(r4ok && fp4.MaterialProgramId() != 0 && redWhereChanged(ra, r4px, ch) && ch > (size_t)W * H / 100,
            "1.4 R: program is in the first frame", (double)ch, 0);

      // 1.4: the GL path tracer (Render 3D's GI probes and its pt engine) runs the program at its hits
      {
         Render3DPathTrace gpt;
         Bvh::Instance gi;
         gi.blasIndex = 0; gi.materialId = 0; gi.transform = Mat4::Identity();
         auto grey = Render3DPathTrace::PtMaterial::FromLinear(0.8f, 0.8f, 0.8f, 0.75f, 0.0f);
         auto prog = grey;
         prog.fieldProgram = id;
         const float eye[3] = { 0.0f, 0.0f, 4.0f }, at[3] = { 0.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 1.0f, 0.0f };
         auto shot = [&](const Render3DPathTrace::PtMaterial& m, std::vector<unsigned char>& px) -> bool
         {
            int w = 0, h = 0;
            if (!gpt.UploadScene({ sphereG.GetMesh() }, { gi }, { m })) return false;
            gpt.SetCameraLookAt(eye, at, up, 30.0f, 1.0f);
            gpt.SetLights({});
            gpt.SetWorldColorLinear(1.0f, 1.0f, 1.0f);
            gpt.SetTonemap(0);
            gpt.SetExposure(1.0f);
            return gpt.RenderToSpp(64, 64, 4) && gpt.ReadDisplayRGBA8(px, w, h) && w == 64 && h == 64;
         };
         std::vector<unsigned char> pg, pr;
         const bool gok = shot(grey, pg) && shot(prog, pr);
         const size_t c = (32 * 64 + 32) * 4;
         const int dr = gok ? (int)pr[c] - (int)pr[c + 1] : 0, dg = gok ? (int)pg[c] - (int)pg[c + 1] : 99;
         check(gok && dr > 60 && std::abs(dg) < 10, "1.4 GL tracer: program turns the sphere red", (double)dr, 60.0);
      }

      // 1.5: a program the backend cannot compile shows its error on the node and its material draws the knobs; the
      // others keep running. One edit of one program costs one library build.
      {
         const char* kBad = "base = vec3(1.0, 0.0, 0.0)\nmetallic = 0.0\nroughness = 0.9";
         const int bad = Field::RegisterMaterialProgram(kBad, {}, ferr);
         Field::BreakMaterialProgramForTest(bad);
         engine::Frame fb = frame, fref = frame;
         fb.instances[1].fieldProgram = id;  // cube: a good program
         fb.instances[2].fieldProgram = bad; // sphere: the broken one
         fb.fieldMsl = Field::BuildMaterialLibrary({ id, bad }, Field::MaterialLang::Msl, fb.fieldMslKey);
         fref.instances[1].fieldProgram = id;
         fref.fieldMsl = Field::BuildMaterialLibrary({ id }, Field::MaterialLang::Msl, fref.fieldMslKey);
         std::vector<float> pb, pref;
         const int b0 = raster->FieldLibraryBuilds();
         const bool okb = raster->Render(*img, fb, nullptr) && ReadRect(img->GlTexture(), W, H, pb);
         const bool okr = raster->Render(*img, fref, nullptr) && ReadRect(img->GlTexture(), W, H, pref);
         double worst = (okb && okr && pb.size() == pref.size()) ? 0.0 : 1.0;
         for (size_t i = 0; worst < 1.0 && i < pb.size(); i++) worst = std::max(worst, (double)std::fabs(pb[i] - pref[i]));
         const std::string berr = Field::MaterialProgramError(bad);
         printf("    bad program error: %s\n", berr.c_str());
         check(!berr.empty(), "1.5 L: bad program -> error text non-empty", (double)berr.size(), 0);
         check(worst <= 1.0 / 255.0, "1.5 L: bad program draws the knob picture, the good one still runs", worst * 255.0, 1.0);
         check(raster->FieldLibraryBuilds() - b0 == 1, "1.5 L: failure costs one library build, the next frame none",
               raster->FieldLibraryBuilds() - b0, 1);
         // the node shows it and hands the material no program
         FieldPixelNode fpb;
         fpb.code = kBad;
         fpb.domain = 1;
         fpb.Apply();
         check(!fpb.CodeError().empty() && fpb.MaterialProgramId() == 0, "1.5: Field Pixel shows the backend error, no program",
               (double)fpb.CodeError().size(), 0);
         // edit of one program: the cube's program changes, one library build; the same frame again, none
         const int edited = Field::RegisterMaterialProgram("base = vec3(0.0, 1.0, 0.0)\nmetallic = 0.0\nroughness = 1.0", {}, ferr);
         engine::Frame fe = fref;
         fe.instances[1].fieldProgram = edited;
         fe.fieldMsl = Field::BuildMaterialLibrary({ edited }, Field::MaterialLang::Msl, fe.fieldMslKey);
         const int b1 = raster->FieldLibraryBuilds();
         const bool oke = raster->Render(*img, fe, nullptr) && raster->Render(*img, fe, nullptr);
         check(oke && raster->FieldLibraryBuilds() - b1 == 1, "1.5 L: edit of one program recompiles 1 library",
               raster->FieldLibraryBuilds() - b1, 1);

         // R (GL): a fresh broken program on the sphere draws the knob picture (ra) and gets its error
         FieldPixelNode fpg;
         fpg.code = "base = vec3(1.0, 0.0, 0.0)\nmetallic = 0.0\nroughness = 0.8";
         fpg.domain = 1;
         fpg.Apply();
         const int gbad = fpg.MaterialProgramId();
         Field::BreakMaterialProgramForTest(gbad);
         MaterialNode mat5;
         mat5.input = &sphereG;
         mat5.FieldInput().Connect(&fpg);
         Render3DNode r5;
         r5.geometry[0] = &floorG; r5.geometry[1] = &cubeG; r5.geometry[2] = &mat5; r5.geometry[3] = &torusG;
         r5.width = (float)W; r5.height = (float)H; r5.samples = 0; r5.hdr = false; r5.tonemap = 1; r5.shadowsEnabled = r.shadowsEnabled;
         r5.rig = &rig;
         for (int k = 0; k < 3; k++) { r5.exposure = 1.0f + 0.001f * (float)k; r5.CookIfNeeded(++fnum); }
         std::vector<float> r5px;
         const bool r5ok = gbad != 0 && GLUtil::ReadTexturePixels(scratch, r5.GetOutputTexture(), r5.GetOutputWidth(), r5.GetOutputHeight(), r5px);
         double rworst = (r5ok && r5px.size() == ra.size()) ? 0.0 : 1.0;
         for (size_t i = 0; rworst < 1.0 && i < r5px.size(); i++) rworst = std::max(rworst, (double)std::fabs(r5px[i] - ra[i]));
         printf("    R bad program error: %s\n", Field::MaterialProgramError(gbad).c_str());
         check(!Field::MaterialProgramError(gbad).empty() && !fpg.CodeError().empty(), "1.5 R: bad program -> error text non-empty",
               (double)Field::MaterialProgramError(gbad).size(), 0);
         check(rworst <= 1.0 / 255.0, "1.5 R: bad program draws the knob picture", rworst * 255.0, 1.0);
      }
   }

   // dump for eyes
   if (const char* dir = getenv("INFINITE_ENGINETEST_DUMP"))
   {
      auto writePpm = [&](const std::string& path, const std::vector<float>& px)
      {
         FILE* f = fopen(path.c_str(), "wb");
         if (!f) return;
         fprintf(f, "P6\n%d %d\n255\n", W, H);
         for (int y = H - 1; y >= 0; y--)
            for (int x = 0; x < W; x++)
               for (int c = 0; c < 3; c++)
                  fputc((int)std::lround(std::clamp(px[((size_t)y * W + x) * 4 + c], 0.0f, 1.0f) * 255.0f), f);
         fclose(f);
      };
      writePpm(std::string(dir) + "/gl.ppm", gl);
      writePpm(std::string(dir) + "/metal.ppm", mt);
   }

   // ---- timing: end to end per frame (CPU encode + GPU + wait), exposure nudged so nothing is cached
   const int N = 120;
   std::vector<double> tgl, tmt;
   for (int i = 0; i < N + 10; i++)
   {
      r.exposure = 1.0f + 0.0001f * (float)i;
      const double t0 = NowMs();
      r.CookIfNeeded(++fnum);
      glFinish();
      if (i >= 10) tgl.push_back(NowMs() - t0);
   }
   for (int i = 0; i < N + 10; i++)
   {
      frame.exposure = 1.0f + 0.0001f * (float)i;
      frame.frameIndex++;
      const double t0 = NowMs();
      raster->Render(*img, frame, nullptr);
      if (i >= 10) tmt.push_back(NowMs() - t0);
   }
   auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
   const double mGl = med(tgl), mMt = med(tmt);
   printf("  GL %.2f ms   Metal %.2f ms   speedup %.2fx   (%d lights, %dx%d)\n", mGl, mMt, mGl / mMt, nLights, W, H);
   check(nLights >= 8, "8 lights drawn", (double)nLights, 8);
   check(mGl / mMt >= 1.5, "at least 1.5x faster than Render 3D (R1 gate)", mGl / mMt, 1.5);
   check(glGetError() == GL_NO_ERROR, "no GL error", 0, 0);
   {
      // The bridge: the same Render 3D node, drawn through the engine, must match its own GL picture.
      r.exposure = 1.0f;
      r.liveEngineFeatures = 0;
      r.liveEngine = true;
      r.CookIfNeeded(++fnum);
      std::vector<float> viaNode;
      check(GLUtil::ReadTexturePixels(scratch, r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight(), viaNode), "node engine picture reads", 0, 0);
      double sum = 0.0;
      const size_t n = std::min(gl.size(), viaNode.size());
      for (size_t i = 0; i < n; i++)
         sum += std::fabs(gl[i] - viaNode[i]);
      const double mae = n ? sum / (double)n : 1.0;
      check(mae <= 0.02, "Render 3D engine mode matches its GL render (MAE)", mae, 0.02);
      r.liveEngineFeatures = 7;
      r.CookIfNeeded(++fnum);
      std::vector<float> full;
      check(GLUtil::ReadTexturePixels(scratch, r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight(), full) && !full.empty(), "node engine picture with RT + GI reads", 0, 0);
      // Settle: the same still scene, cooked repeatedly, switches to the path tracer and refines.
      r.liveEngineFeatures = 15; r.ptSamples = 32; r.ptBounces = 6;
      for (int i = 0; i < 12; i++) r.CookIfNeeded(++fnum);
      std::vector<float> settled;
      check(GLUtil::ReadTexturePixels(scratch, r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight(), settled) && settled.size() == gl.size(), "node engine settles to a path-traced still", 0, 0);
      double ssum = 0.0, lum = 0.0;
      for (size_t i = 0; i < settled.size(); i++) { ssum += std::fabs(settled[i] - gl[i]); lum += settled[i]; }
      printf("  settled vs raster MAE %.4f, mean %.4f\n", ssum / (double)settled.size(), lum / (double)settled.size());
      check(ssum / (double)settled.size() < 0.08, "settled picture stays close to the raster", ssum / (double)settled.size(), 0.08);
      r.liveEngine = false;
   }
   if (const char* instEnv = getenv("INFINITE_ENGINETEST_INSTANCES"))
   {
      // R5: one mesh, N copies in one instanced draw. Gates: >= 30 fps at 1M, <= 1.5 GB resident, regenerate <= 100 ms.
      const size_t N = (size_t)atoll(instEnv);
      engine::Frame bf = frame;
      bf.rtShadows = false; bf.rtReflections = false; bf.gi = false; bf.shadow.enabled = false;
      bf.instances.clear();
      double regen = 0.0;
      auto build = [&](float phase)
      {
         const double g0 = NowMs();
         engine::InstanceBatch b;
         b.mesh = 1; // the cube
         b.models.resize(N * 16);
         const size_t side = (size_t)std::ceil(std::cbrt((double)N));
         const float step = 0.09f, sc = 0.03f;
         size_t i = 0;
         for (size_t z = 0; z < side && i < N; z++)
            for (size_t y = 0; y < side && i < N; y++)
               for (size_t x = 0; x < side && i < N; x++, i++)
               {
                  float* m = &b.models[i * 16];
                  const float wob = 0.02f * std::sin(phase + 0.1f * (float)x + 0.13f * (float)z);
                  m[0] = sc; m[5] = sc; m[10] = sc; m[15] = 1.0f;
                  m[12] = ((float)x - 0.5f * (float)side) * step;
                  m[13] = ((float)y - 0.5f * (float)side) * step + wob;
                  m[14] = ((float)z - 0.5f * (float)side) * step;
               }
         b.baseSrgb[0] = 0.8f; b.baseSrgb[1] = 0.5f; b.baseSrgb[2] = 0.3f; b.roughness = 0.45f;
         bf.batches.clear();
         bf.batches.push_back(std::move(b));
         regen = NowMs() - g0;
      };
      build(0.0f);
      // camera outside the cloud looking at its centre
      const float half = 0.5f * std::ceil(std::cbrt((double)N)) * 0.09f;
      const float ceye[3] = { half * 1.3f, half * 0.9f, half * 2.2f }, ctgt[3] = { 0, 0, 0 };
      const Mat4 cv = Mat4::LookAt(ceye, ctgt, up);
      const Mat4 cp = Mat4::Perspective(45.0f * 3.14159265f / 180.0f, (float)W / (float)H, 0.1f, 400.0f);
      const Mat4 cvp = Mat4::Multiply(cp, cv);
      std::memcpy(bf.viewProj, cvp.m, 64); std::memcpy(bf.eye, ceye, 12);
      std::vector<double> ti;
      for (int i = 0; i < 70; i++)
      {
         bf.frameIndex++;
         const double t0 = NowMs();
         double gm = 0.0;
         raster->Render(*img, bf, &gm);
         if (i == 20) printf("  instanced frame GPU %.1f ms\n", gm);
         if (i >= 10) ti.push_back(NowMs() - t0);
      }
      std::sort(ti.begin(), ti.end());
      build(1.0f); // the regenerate time (positions rebuilt each frame in a live use)
      size_t rss = 0;
#if defined(__APPLE__)
      mach_task_basic_info_data_t info; mach_msg_type_number_t cnt = MACH_TASK_BASIC_INFO_COUNT;
      if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &cnt) == KERN_SUCCESS) rss = info.resident_size;
#endif
      printf("  INSTANCES %zu: frame p50 %.1f ms p99 %.1f ms (%.1f fps), regenerate %.1f ms, resident %.0f MB\n",
             N, ti[ti.size() / 2], ti[(size_t)(ti.size() * 0.99)], 1000.0 / ti[ti.size() / 2], regen, (double)rss / 1048576.0);
      check(1000.0 / ti[ti.size() / 2] >= 30.0, "R5: >= 30 fps with the instanced batch", 1000.0 / ti[ti.size() / 2], 30.0);
#ifdef NDEBUG
      check(regen <= 100.0, "R5: regenerate <= 100 ms", regen, 100.0);
#else
      printf("  (regenerate gate is checked in release builds only; this is a debug build: %.1f ms)\n", regen);
#endif
      check((double)rss <= 1.5 * 1073741824.0, "R5: resident <= 1.5 GB", (double)rss / 1048576.0, 1536.0);
      if (const char* dir = getenv("INFINITE_ENGINETEST_DUMP"))
      {
         std::vector<float> px; ReadRect(img->GlTexture(), W, H, px);
         FILE* f = fopen((std::string(dir) + "/inst.ppm").c_str(), "wb");
         if (f) { fprintf(f, "P6\n%d %d\n255\n", W, H); for (int y = H - 1; y >= 0; y--) for (int x = 0; x < W; x++) for (int c = 0; c < 3; c++) fputc((int)std::lround(std::clamp(px[((size_t)y * W + x) * 4 + c], 0.0f, 1.0f) * 255.0f), f); fclose(f); }
      }
   }
   if (const char* ptEnv = getenv("INFINITE_ENGINETEST_PT"))
   {
      // R4: the Metal path tracer against the GL path tracer on the same scene. H5: >= 4x faster per sample.
      const int spp = std::max(1, atoi(ptEnv));
      const int bounces = 6;
      std::vector<float> glPt, mtPt;
      // Metal: warm-up, then timed converge.
      raster->PathTrace(*img, frame, 1, bounces, true, nullptr);
      double mtMs = 0.0;
      const double t0 = NowMs();
      bool ok = raster->PathTrace(*img, frame, spp, bounces, true, &mtMs);
      const double mtWall = NowMs() - t0;
      check(ok, "Metal path trace runs", mtMs, 0);
      printf("  Metal PT: %d spp  GPU %.1f ms  wall %.1f ms  = %.3f ms/sample (%dx%d)\n", spp, mtMs, mtWall, mtMs / spp, W, H);
      check(ReadRect(img->GlTexture(), W, H, mtPt), "Metal PT picture reads", 0, 0);
      // GL: the node's own tracer, offline so every sample is traced in this cook.
      r.engine = 1; r.ptSamples = spp; r.ptBounces = bounces;
      Transport::Instance().SetOfflineMode(true);
      r.exposure = 1.0f;
      r.CookIfNeeded(++fnum);
      const double g0 = NowMs();
      r.exposure = 1.0001f;
      r.CookIfNeeded(++fnum);
      glFinish();
      const double glWall = NowMs() - g0;
      Transport::Instance().SetOfflineMode(false);
      check(GLUtil::ReadTexturePixels(scratch, r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight(), glPt), "GL PT picture reads", 0, 0);
      r.engine = 0;
      printf("  GL PT: %d spp  wall %.1f ms  = %.3f ms/sample\n", spp, glWall, glWall / spp);
      check((glWall / spp) / (mtMs / spp) >= 4.0, "R4/H5: Metal PT >= 4x faster per sample", (glWall / spp) / (mtMs / spp), 4.0);
      if (glPt.size() == mtPt.size())
      {
         double s2 = 0.0, lm = 0.0, lg = 0.0;
         for (size_t i = 0; i < glPt.size(); i += 4)
            for (int c = 0; c < 3; c++) { s2 += std::fabs(glPt[i + c] - mtPt[i + c]); lm += mtPt[i + c]; lg += glPt[i + c]; }
         const double n = (double)(glPt.size() / 4) * 3.0;
         printf("  PT MAE %.4f   mean luminance GL %.4f  Metal %.4f\n", s2 / n, lg / n, lm / n);
         check(s2 / n <= 0.06, "R4: Metal PT within 0.06 MAE of GL PT", s2 / n, 0.06);
      }
      if (const char* dir = getenv("INFINITE_ENGINETEST_DUMP"))
      {
         auto wp = [&](const std::string& path, const std::vector<float>& px)
         {
            FILE* f = fopen(path.c_str(), "wb");
            if (!f) return;
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (int y = H - 1; y >= 0; y--)
               for (int x = 0; x < W; x++)
                  for (int c = 0; c < 3; c++)
                     fputc((int)std::lround(std::clamp(px[((size_t)y * W + x) * 4 + c], 0.0f, 1.0f) * 255.0f), f);
            fclose(f);
         };
         wp(std::string(dir) + "/gl_pt.ppm", glPt);
         wp(std::string(dir) + "/metal_pt.ppm", mtPt);
      }
   }
   if (const char* soakEnv = getenv("INFINITE_ENGINETEST_SOAK"))
   {
      // R3 soak: GI + RT shadows + RT reflections + music rig, unpaced, for N seconds. Reports fps and hitches.
      const double seconds = atof(soakEnv);
      engine::MusicRig mrig;
      mrig.SetBindings({
         { engine::RigSource::BeatPulse, engine::RigTarget::ScaleUniform, 1, 0.25f, 0.8f, 0.12f },
         { engine::RigSource::Low, engine::RigTarget::OffsetY, 2, 0.4f, 0.6f, 0.1f },
         { engine::RigSource::Mid, engine::RigTarget::ScaleY, 3, 0.3f, 0.5f, 0.15f },
         { engine::RigSource::High, engine::RigTarget::LightIntensity, 1, 0.6f, 0.7f, 0.2f },
         { engine::RigSource::BeatPulse, engine::RigTarget::Exposure, 0, 0.08f, 0.8f, 0.1f },
      });
      frame.gi = true; frame.rtShadows = true; frame.rtReflections = true;
      // Live audio: the real engine at a 128-frame buffer with the transport playing, so the beat comes from the
      // transport and the callbacks compete with the GPU soak. Reported as xruns.
      const bool liveAudio = getenv("INFINITE_ENGINETEST_AUDIO") != nullptr;
      std::string audioErr;
      if (liveAudio)
      {
         AudioEngine::Instance().Stop();
         AudioEngine::Instance().SetRequestedBufferFrames(128);
         const bool started = StartAudioEngine(audioErr);
         check(started, "audio engine starts at 128 frames", 0, 0);
         if (started) Transport::Instance().SetPlaying(true);
      }
      std::vector<double> ft; std::vector<double> rigMs;
      int over50 = 0; double worst = 0.0, last = 0.0;
      const double start = NowMs();
      double tPrev = start;
      uint32_t rng = 12345;
      while (NowMs() - start < seconds * 1000.0)
      {
         const double now = NowMs();
         const double beat = liveAudio ? Transport::Instance().Beats() : (now - start) / 500.0; // 120 bpm when synthetic
         engine::MusicSignals sg;
         sg.beatPhase = (float)(beat - std::floor(beat));
         sg.beatPulse = std::exp(-4.0f * sg.beatPhase);
         sg.low = 0.6f * sg.beatPulse; sg.mid = 0.5f + 0.5f * (float)std::sin(beat * 1.7);
         rng = rng * 1664525u + 1013904223u; sg.high = (float)(rng >> 24) / 255.0f;
         const double r0 = NowMs();
         mrig.Apply(frame, sg);
         rigMs.push_back(NowMs() - r0);
         frame.frameIndex++;
         if (getenv("INFINITE_ENGINETEST_NOGPU")) std::this_thread::sleep_for(std::chrono::milliseconds(20)); else raster->Render(*img, frame, nullptr);
         const double t = NowMs();
         const double dt = t - tPrev; tPrev = t;
         ft.push_back(dt); worst = std::max(worst, dt); if (dt > 50.0) over50++;
         if (t - last > 30000.0) { last = t; printf("  soak %.0fs: %zu frames, last %.1f ms, worst %.1f ms, >50ms %d\n", (t - start) / 1000.0, ft.size(), dt, worst, over50); fflush(stdout); }
      }
      std::sort(rigMs.begin(), rigMs.end());
      std::vector<double> sorted = ft; std::sort(sorted.begin(), sorted.end());
      const double total = (NowMs() - start) / 1000.0;
      const double fps = (double)ft.size() / total;
      const double p99 = sorted[(size_t)(sorted.size() * 0.99)];
      printf("  SOAK %.0fs: %zu frames, %.1f fps avg, frame p50 %.1f p99 %.1f max %.1f ms, >50ms %d, rig p99 %.4f ms\n",
             total, ft.size(), fps, sorted[sorted.size() / 2], p99, worst, over50, rigMs[(size_t)(rigMs.size() * 0.99)]);
      if (liveAudio)
      {
         const AudioEngine::XrunCounts xr = AudioEngine::Instance().Xruns();
         printf("  AUDIO xruns during soak: deadline %llu, os %llu, callback gaps %llu (buffer %d frames)\n",
                (unsigned long long)xr.deadline, (unsigned long long)xr.os, (unsigned long long)xr.gaps, 128);
         check(xr.Total() == 0, "R3: 0 audio xruns beside the GPU soak (128-frame buffer)", (double)xr.Total(), 0);
         Transport::Instance().SetPlaying(false);
         AudioEngine::Instance().Stop();
      }
      check(fps >= 30.0, "R3: >= 30 fps sustained", fps, 30.0);
      check((double)over50 <= 0.001 * (double)ft.size(), "R3: <= 0.1% of frames over 50 ms", over50, 0.001 * (double)ft.size());
      check(rigMs[(size_t)(rigMs.size() * 0.99)] <= 1.0, "R3: rig <= 1 ms", rigMs[(size_t)(rigMs.size() * 0.99)], 1.0);
   }
   for (int i = 0; i < 4; i++)
      r.geometry[i] = nullptr;
   r.rig = nullptr;
   // Every pipeline the frames above built (raster, GI, path tracer, denoise) compiled: a broken shared header
   // fails here instead of quietly drawing the raster picture (DESIGN.md L2).
   if (!rhi::LastError().empty())
      printf("  engine error: %s\n", rhi::LastError().c_str());
   check(rhi::LastError().empty(), "no shader or pipeline failed to build", 0, 0);
#else
   check(dev == nullptr, "no engine backend on this platform", 0, 0);
#endif
   printf(failures == 0 ? "ENGINETEST OK\n" : "ENGINETEST FAIL (%d)\n", failures);
   return failures == 0;
}
}
