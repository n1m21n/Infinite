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
#include "core/MeshWear.h"
#include "core/Mesh.h"
#include <chrono>
#include "core/Expression.h"
#include <functional>
#include "nodes/FieldPixelNode.h"
#include "nodes/SketchNode.h"
#include "Transport.h"
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

   // Row 2.1: mean, variance and power-spectrum slope of one channel image (n x n). The slope is the least-squares
   // fit of log power against log frequency, k = 1..n/2, of the row DFTs averaged over all rows.
   struct NoiseStats { double mean = 0, var = 0, slope = 0; };
   NoiseStats StatsOf(const std::vector<double>& img, int n)
   {
      NoiseStats s;
      for (double v : img) s.mean += v;
      s.mean /= (double)img.size();
      for (double v : img) s.var += (v - s.mean) * (v - s.mean);
      s.var /= (double)img.size();
      std::vector<double> pw((size_t)n / 2 + 1, 0.0);
      for (int y = 0; y < n; y++)
         for (int k = 1; k <= n / 2; k++)
         {
            double re = 0, im = 0;
            for (int x = 0; x < n; x++)
            {
               const double a = -2.0 * M_PI * k * x / n, v = img[(size_t)y * n + x] - s.mean;
               re += v * std::cos(a);
               im += v * std::sin(a);
            }
            pw[k] += re * re + im * im;
         }
      double sx = 0, sy = 0, sxx = 0, sxy = 0;
      int m = 0;
      for (int k = 1; k <= n / 2; k++)
      {
         const double lx = std::log((double)k), ly = std::log(pw[k] / n + 1e-12);
         sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly; m++;
      }
      s.slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
      return s;
   }

   // Within 2 %: relative to the reference, with a floor so values near 0 (a white spectrum's slope, a centred mean)
   // are judged on an absolute scale instead of blowing up.
   bool Within2(double ref, double v, double floor) { return std::fabs(v - ref) <= 0.02 * std::max(std::fabs(ref), floor); }

   void NoiseRows(rhi::Device& dev, const std::function<void(bool, const char*, double, double)>& check)
   {
      struct Case { const char* name; const char* expr; };
      // expr is over u, v (pixel centre uv); the kernel and the CPU VM evaluate the same text.
      const Case cases[] = {
         { "noise3", "noise3(vec3(u * 16.0, v * 16.0, 0.37)) * 0.5 + 0.5" },
         { "noise4", "noise4(vec3(u * 16.0, v * 16.0, 0.37), 1.7) * 0.5 + 0.5" },
         { "fbm", "fbm(vec3(u * 8.0, v * 8.0, 0.37), 5.0, 2.0, 0.5) * 0.5 + 0.5" },
         { "voronoi3 F1", "voronoi3(vec3(u * 12.0, v * 12.0, 0.37)).x * 0.5" },
         { "voronoi3 F2", "voronoi3(vec3(u * 12.0, v * 12.0, 0.37)).y * 0.5" },
         { "voronoi3 id", "voronoi3(vec3(u * 12.0, v * 12.0, 0.37)).z" },
         { "wave", "wave(vec3(u * 6.3, v * 6.3, 0.0))" },
         { "brick", "brick(vec3(u * 7.3, v * 7.3, 0.0), 0.1)" },
         { "checker", "checker(vec3(u * 9.3, v * 9.3, 0.0))" },
         { "gradient", "gradient(vec3(u * 1.4 - 0.2, v, 0.0))" },
         { "white", "white(vec3(u * 32.0, v * 32.0, 0.0))" },
      };
      const int n = 128;
      int passed = 0, total = 0;
      double worstStat = 0.0;
      for (const Case& c : cases)
      {
         total++;
         std::string code = "float u = uv.x;\nfloat v = uv.y;\ncol = vec3(" + std::string(c.expr) + ");";
         Field::PixelIRProgram ir;
         std::string err;
         std::vector<float> gl, mt;
         if (!LowerPreset(code.c_str(), ir, err) || !GlFieldPixel(ir, n, gl, err) || !MetalFieldPixel(dev, ir, n, mt, err))
         {
            printf("  noise %-12s GPU failed: %s\n", c.name, err.c_str());
            continue;
         }
         std::vector<double> cpu((size_t)n * n), g((size_t)n * n), m((size_t)n * n);
         bool vmOk = true;
         std::map<std::string, float> sib;
         for (int y = 0; y < n && vmOk; y++)
            for (int x = 0; x < n; x++)
            {
               sib["u"] = (float)((x + 0.5) / n);
               sib["v"] = (float)((y + 0.5) / n);
               float out = 0.0f;
               if (!Expression::Evaluate(c.expr, 0.0, &sib, nullptr, out, err))
               {
                  printf("  noise %-12s VM failed: %s\n", c.name, err.c_str());
                  vmOk = false;
                  break;
               }
               const size_t i = (size_t)y * n + x;
               cpu[i] = out;
               g[i] = gl[i * 4];
               m[i] = mt[i * 4];
            }
         if (!vmOk)
            continue;
         const NoiseStats a = StatsOf(cpu, n), b = StatsOf(g, n), d = StatsOf(m, n);
         double px = 0.0;
         for (size_t i = 0; i < cpu.size(); i++)
            px = std::max({ px, std::fabs(cpu[i] - g[i]), std::fabs(cpu[i] - m[i]) });
         const bool ok = Within2(a.mean, b.mean, 0.05) && Within2(a.mean, d.mean, 0.05) && Within2(a.var, b.var, 0.01) &&
                         Within2(a.var, d.var, 0.01) && Within2(a.slope, b.slope, 1.0) && Within2(a.slope, d.slope, 1.0);
         auto rel = [](double r, double v, double f) { return std::fabs(v - r) / std::max(std::fabs(r), f); };
         worstStat = std::max({ worstStat, rel(a.mean, b.mean, 0.05), rel(a.mean, d.mean, 0.05), rel(a.var, b.var, 0.01),
                                rel(a.var, d.var, 0.01), rel(a.slope, b.slope, 1.0), rel(a.slope, d.slope, 1.0) });
         printf("  noise %-12s VM mean %.4f var %.4f slope %+.3f | GLSL %.4f %.4f %+.3f | MSL %.4f %.4f %+.3f | max px %.5f %s\n",
                c.name, a.mean, a.var, a.slope, b.mean, b.var, b.slope, d.mean, d.var, d.slope, px, ok ? "" : "<-- off");
         if (ok)
            passed++;
      }
      check(passed == total, "2.1: 9 noise built-ins (11 channels), mean/variance/spectrum slope VM vs GLSL vs MSL within 2 %",
            worstStat * 100.0, 2.0);
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
   NoiseRows(*dev, check);
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

   // ---- 2.3 (ShadeRows): Shade Coord (Object) -> Shade Mapping (rotate Z 30, scale 3) -> Shade Checker -> Material
   // `field` composes one Material program; on L the sphere shows both checker colours.
   {
      ShadeCoordNode coord;
      coord.output = ShadeCoordNode::kObject;
      ShadeMappingNode mapping;
      mapping.rotate[2] = 30.0f;
      mapping.scale[0] = mapping.scale[1] = mapping.scale[2] = 3.0f;
      mapping.ShadeInput(0)->Connect(&coord);
      ShadeCheckerNode checker;
      checker.color1[0] = 1.0f; checker.color1[1] = 0.0f; checker.color1[2] = 0.0f;
      checker.color2[0] = 0.0f; checker.color2[1] = 0.0f; checker.color2[2] = 1.0f;
      checker.scale = 1.0f;
      checker.ShadeInput(0)->Connect(&mapping);
      MaterialNode smat;
      smat.input = &sphereG;
      smat.FieldInput().Connect(&checker);
      const int sid = smat.GetMaterial().fieldProgram;
      const float lo[3] = { 0.0f, 0.0f, 0.0f }, hi[3] = { 1.0f, 1.0f, 1.0f };
      std::string serr;
      printf("    shade program:\n%s", ComposeShadeProgram(&checker, lo, hi, serr).c_str());
      check(sid > 0 && serr.empty(), "2.3: Shade chain on Material field registers a program", (double)sid, 0);
      check(smat.GetMaterial().fieldProgram == sid, "2.3: same chain, same id", (double)smat.GetMaterial().fieldProgram, (double)sid);
      mapping.rotate[2] = 45.0f;
      const int sid2 = smat.GetMaterial().fieldProgram;
      check(sid2 > 0 && sid2 != sid, "2.3: a knob change gives a new program", (double)sid2, (double)sid);
      mapping.rotate[2] = 30.0f;
      engine::Frame fs = frame;
      fs.instances[2].fieldProgram = sid;
      fs.fieldMsl = Field::BuildMaterialLibrary({ sid }, Field::MaterialLang::Msl, fs.fieldMslKey);
      std::vector<float> sp;
      const bool sok = sid > 0 && raster->Render(*img, fs, nullptr) && raster->Render(*img, fs, nullptr) && ReadRect(img->GlTexture(), W, H, sp);
      size_t red = 0, blue = 0;
      for (size_t i = 0; sok && i + 3 < sp.size() && i + 3 < mt.size(); i += 4)
      {
         const float d = std::max({ std::fabs(sp[i] - mt[i]), std::fabs(sp[i + 1] - mt[i + 1]), std::fabs(sp[i + 2] - mt[i + 2]) });
         if (d < 0.05f) continue;
         if (sp[i] > sp[i + 2] + 0.1f) red++;
         else if (sp[i + 2] > sp[i] + 0.1f) blue++;
      }
      const std::string perr = sid > 0 ? Field::MaterialProgramError(sid) : std::string("no program");
      if (!perr.empty()) printf("    shade program error: %s\n", perr.c_str());
      check(perr.empty(), "2.3 L: shade program compiles", (double)perr.size(), 0);
      const size_t need = (size_t)W * H / 400;
      check(sok && red > need && blue > need, "2.3 L: sphere shows both checker colours", (double)red, (double)blue);
   }

   // ---- 2.4: every mode of Shade Noise, Voronoi, Gradient and Brick composes, compiles on L and draws a pattern
   // (the sphere's changed pixels are not one flat value).
   {
      ShadeCoordNode coord;
      coord.output = ShadeCoordNode::kObject;
      ShadeNoiseNode noise;
      noise.distortion = 0.5f;
      noise.ShadeInput(0)->Connect(&coord);
      ShadeVoronoiNode voronoi;
      voronoi.ShadeInput(0)->Connect(&coord);
      ShadeGradientNode gradient;
      gradient.scale = 2.0f;
      gradient.ShadeInput(0)->Connect(&coord);
      ShadeBrickNode brick;
      brick.mortarColor[0] = brick.mortarColor[1] = brick.mortarColor[2] = 1.0f;
      brick.ShadeInput(0)->Connect(&coord);
      struct Mode { const ShadeNode* node; int* knob; int count; const char* name; };
      int one = 0;
      const Mode modes[] = {
         { &noise, &noise.output, (int)ShadeNoiseNode::OutputNames().size(), "noise" },
         { &voronoi, &voronoi.output, (int)ShadeVoronoiNode::OutputNames().size(), "voronoi" },
         { &gradient, &gradient.kind, (int)ShadeGradientNode::KindNames().size(), "gradient" },
         { &brick, &one, 1, "brick" },
      };
      const float lo[3] = { -1.0f, -1.0f, -1.0f }, hi[3] = { 1.0f, 1.0f, 1.0f };
      int total = 0, composed = 0, compiled = 0, patterned = 0;
      for (const Mode& m : modes)
         for (int k = 0; k < m.count; k++)
         {
            total++;
            *m.knob = k;
            std::string err;
            const std::string src = ComposeShadeProgram(m.node, lo, hi, err);
            const int id = src.empty() ? 0 : Field::RegisterMaterialProgram(src, {}, err);
            if (id <= 0)
            {
               printf("    2.4 %s mode %d: %s\n%s", m.name, k, err.c_str(), src.c_str());
               continue;
            }
            composed++;
            engine::Frame fs = frame;
            fs.instances[2].fieldProgram = id;
            fs.fieldMsl = Field::BuildMaterialLibrary({ id }, Field::MaterialLang::Msl, fs.fieldMslKey);
            std::vector<float> sp;
            const bool ok = raster->Render(*img, fs, nullptr) && raster->Render(*img, fs, nullptr) && ReadRect(img->GlTexture(), W, H, sp);
            const std::string perr = Field::MaterialProgramError(id);
            if (!perr.empty())
            {
               printf("    2.4 %s mode %d: %s\n%s", m.name, k, perr.c_str(), src.c_str());
               continue;
            }
            compiled++;
            double sum = 0.0, sum2 = 0.0;
            size_t n = 0;
            for (size_t i = 0; ok && i + 3 < sp.size() && i + 3 < mt.size(); i += 4)
            {
               const float d = std::max({ std::fabs(sp[i] - mt[i]), std::fabs(sp[i + 1] - mt[i + 1]), std::fabs(sp[i + 2] - mt[i + 2]) });
               if (d < 0.02f) continue;
               const double l = (sp[i] + sp[i + 1] + sp[i + 2]) / 3.0;
               sum += l; sum2 += l * l; n++;
            }
            const double var = n ? sum2 / (double)n - (sum / (double)n) * (sum / (double)n) : 0.0;
            if (n > (size_t)W * H / 400 && var > 1e-4) patterned++;
            else printf("    2.4 %s mode %d: flat (%zu px, variance %.6f)\n", m.name, k, n, var);
         }
      check(composed == total, "2.4: every Noise / Voronoi / Gradient / Brick mode composes a program", (double)composed, (double)total);
      check(compiled == total, "2.4 L: every mode compiles", (double)compiled, (double)total);
      check(patterned == total, "2.4 L: every mode draws a pattern on the sphere", (double)patterned, (double)total);

      // Shade Image: a two-colour picture through img(slot, uv), flat and as a box (triplanar), colour and data.
      struct FakeImage : INode
      {
         unsigned int GetOutputTexture() override { return 7; }
         int GetOutputWidth() const override { return 64; }
         int GetOutputHeight() const override { return 64; }
         void CookIfNeeded(int) override {}
      } fakeImage;
      std::vector<unsigned char> stripes(64 * 64 * 4, 255);
      for (int y = 0; y < 64; y++)
         for (int x = 0; x < 64; x++)
         {
            unsigned char* p = &stripes[((size_t)y * 64 + x) * 4];
            const bool on = ((x / 8) + (y / 8)) % 2 == 0;
            p[0] = on ? 230 : 20; p[1] = on ? 60 : 90; p[2] = on ? 30 : 220;
         }
      ShadeImageNode image;
      image.ShadeInput(0)->Connect(&coord);
      image.ShadeInput(1)->Connect(&fakeImage);
      int imgTotal = 0, imgOk = 0;
      double meanColour = 0.0, meanData = 0.0;
      for (int proj = 0; proj < 2; proj++)
         for (int space = 0; space < 2; space++)
         {
            imgTotal++;
            image.projection = proj; image.space = space;
            std::string err;
            std::vector<const ShadeNode*> images;
            const std::string src = ComposeShadeProgram(&image, lo, hi, err, &images);
            const int id = src.empty() ? 0 : Field::RegisterMaterialProgram(src, {}, err);
            if (id <= 0 || images.size() != 1)
            {
               printf("    2.4 image %d/%d: %s (%zu images)\n%s", proj, space, err.c_str(), images.size(), src.c_str());
               continue;
            }
            engine::Frame fs = frame;
            engine::TextureData td;
            td.rgba = stripes.data(); td.w = 64; td.h = 64; td.key = 0x24a0001ull;
            fs.textures.push_back(td);
            fs.instances[2].fieldProgram = id;
            fs.instances[2].fieldImage[0] = (int)fs.textures.size() - 1;
            fs.fieldMsl = Field::BuildMaterialLibrary({ id }, Field::MaterialLang::Msl, fs.fieldMslKey);
            std::vector<float> sp;
            const bool ok = raster->Render(*img, fs, nullptr) && raster->Render(*img, fs, nullptr) && ReadRect(img->GlTexture(), W, H, sp);
            const std::string perr = Field::MaterialProgramError(id);
            if (!perr.empty())
            {
               printf("    2.4 image %d/%d: %s\n%s", proj, space, perr.c_str(), src.c_str());
               continue;
            }
            double sum = 0.0, sum2 = 0.0;
            size_t n = 0;
            for (size_t i = 0; ok && i + 3 < sp.size() && i + 3 < mt.size(); i += 4)
            {
               const float d = std::max({ std::fabs(sp[i] - mt[i]), std::fabs(sp[i + 1] - mt[i + 1]), std::fabs(sp[i + 2] - mt[i + 2]) });
               if (d < 0.02f) continue;
               const double l = (sp[i] + sp[i + 1] + sp[i + 2]) / 3.0;
               sum += l; sum2 += l * l; n++;
            }
            const double mean = n ? sum / (double)n : 0.0, var = n ? sum2 / (double)n - mean * mean : 0.0;
            if (proj == 0) (space == 0 ? meanColour : meanData) = mean;
            if (n > (size_t)W * H / 400 && var > 1e-4) imgOk++;
            else printf("    2.4 image %d/%d: flat (%zu px, variance %.6f)\n", proj, space, n, var);
         }
      check(imgOk == imgTotal, "2.4 L: Shade Image draws its picture, flat and box, colour and data", (double)imgOk, (double)imgTotal);
      check(meanData > meanColour + 0.01, "2.4 L: Data reads raw values, Colour decodes sRGB (darker)", meanData - meanColour, 0.01);
      {
         std::string err;
         const int bad = Field::RegisterMaterialProgram("c = img(0.0, P)\nbase = vec3(c.x, c.y, c.z)\n", {}, err);
         check(bad == 0 && !err.empty(), "2.4: img() refuses a uv that is not a vec2", (double)bad, 0.0);
      }
   }

   // ---- 2.5: value nodes against hand formulas. Each node is fed probe values that vary over a 5x5 picture
   // (25 inputs), its program runs through both emitters, and every pixel is compared with the formula in C++.
   {
      struct Probe : ShadeNode
      {
         std::string e;
         explicit Probe(const char* s) : e(s) {}
         std::string Emit(ShadeBuild& b) const override { return b.NewVar(this, e); }
      };
      struct V { double x, y, z; };
      const int n = 5;
      auto U = [&](int x) { return (x + 0.5) / n; };
      int bad = 0;
      double worst = 0.0;
      // runs `node`, compares with ref(x, y) on GLSL and MSL; values travel as v / 20 + 0.5
      auto run = [&](const char* name, const ShadeNode& node, const std::function<V(int, int)>& ref, const std::string& pre = std::string()) {
         ShadeBuild sb;
         const std::string v = node.Emit(sb);
         const std::string code = pre + sb.body + "col = " + v + " * 0.05 + vec3(0.5)\n";
         Field::PixelIRProgram ir;
         std::string err;
         std::vector<float> g, m;
         if (!LowerPreset(code.c_str(), ir, err) || !GlFieldPixel(ir, n, g, err) || (dev && !MetalFieldPixel(*dev, ir, n, m, err)))
         {
            printf("    2.5 %s: %s\n%s", name, err.c_str(), code.c_str());
            bad++;
            return false;
         }
         double e = 0.0;
         for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++)
            {
               const V r = ref(x, y);
               const double want[3] = { r.x, r.y, r.z };
               for (int c = 0; c < 3; c++)
               {
                  const double w = std::clamp(want[c], -9.5, 9.5);
                  const size_t i = ((size_t)y * n + x) * 4 + c;
                  const double tol = 2e-3 + 1e-3 * std::fabs(w);
                  e = std::max(e, std::fabs(std::clamp((g[i] - 0.5) * 20.0, -9.5, 9.5) - w) / tol);
                  if (!m.empty()) e = std::max(e, std::fabs(std::clamp((m[i] - 0.5) * 20.0, -9.5, 9.5) - w) / tol);
               }
            }
         worst = std::max(worst, e);
         if (!(e <= 1.0))
         {
            printf("    2.5 %s: off by %.2f x tolerance\n", name, e);
            bad++;
            return false;
         }
         return true;
      };
      auto A = [&](int x) { return -2.0 + 4.5 * U(x); };
      auto B = [&](int y) { return -1.5 + 4.0 * U(y); };
      const double C = 0.3;
      auto fr = [](double x) { return x - std::floor(x); };

      // Shade Math
      {
         Probe pa("vec3(-2.0 + 4.5 * uv.x)"), pb("vec3(-1.5 + 4.0 * uv.y)"), pc("vec3(0.3)");
         using M = ShadeMathNode;
         const std::function<double(double, double, double)> f[M::kOpCount] = {
            [](double a, double b, double) { return a + b; },
            [](double a, double b, double) { return a - b; },
            [](double a, double b, double) { return a * b; },
            [](double a, double b, double) { return b != 0.0 ? a / b : 0.0; },
            [](double a, double b, double c) { return a * b + c; },
            [](double a, double b, double) { return a >= 0.0 ? std::pow(a, b) : (b == std::floor(b) ? std::pow(a, b) : 0.0); },
            [](double a, double b, double) { return a > 0.0 && b > 0.0 ? std::log(a) / std::log(b) : 0.0; },
            [](double a, double, double) { return a > 0.0 ? std::sqrt(a) : 0.0; },
            [](double a, double, double) { return a > 0.0 ? 1.0 / std::sqrt(a) : 0.0; },
            [](double a, double, double) { return std::fabs(a); },
            [](double a, double, double) { return std::exp(a); },
            [](double a, double b, double) { return std::min(a, b); },
            [](double a, double b, double) { return std::max(a, b); },
            [](double a, double b, double) { return a < b ? 1.0 : 0.0; },
            [](double a, double b, double) { return a > b ? 1.0 : 0.0; },
            [](double a, double, double) { return a > 0.0 ? 1.0 : a < 0.0 ? -1.0 : 0.0; },
            [](double a, double b, double c) { return std::fabs(a - b) <= std::max(c, 1e-5) ? 1.0 : 0.0; },
            [](double a, double b, double c) { const double h = std::max(c - std::fabs(a - b), 0.0) / c; return std::min(a, b) - h * h * h * c / 6.0; },
            [](double a, double b, double c) { const double h = std::max(c - std::fabs(a - b), 0.0) / c; return std::max(a, b) + h * h * h * c / 6.0; },
            [](double a, double, double) { return std::floor(a + 0.5); },
            [](double a, double, double) { return std::floor(a); },
            [](double a, double, double) { return std::ceil(a); },
            [](double a, double, double) { return std::trunc(a); },
            [](double a, double, double) { return a - std::floor(a); },
            [](double a, double b, double) { return std::fmod(a, b); },
            [](double a, double b, double) { return a - std::floor(a / b) * b; },
            [](double a, double b, double c) { return a - (b - c) * std::floor((a - c) / (b - c)); },
            [](double a, double b, double) { return std::floor(a / b) * b; },
            [](double a, double b, double) { const double q = (a - b) / (b * 2.0); return std::fabs((q - std::floor(q)) * b * 2.0 - b); },
            [](double a, double, double) { return std::sin(a); },
            [](double a, double, double) { return std::cos(a); },
            [](double a, double, double) { return std::tan(a); },
            [](double a, double, double) { return std::asin(std::clamp(a, -1.0, 1.0)); },
            [](double a, double, double) { return std::acos(std::clamp(a, -1.0, 1.0)); },
            [](double a, double, double) { return std::atan(a); },
            [](double a, double b, double) { return std::atan2(a, b); },
            [](double a, double, double) { return std::sinh(a); },
            [](double a, double, double) { return std::cosh(a); },
            [](double a, double, double) { return std::tanh(a); },
            [](double a, double, double) { return a * 3.14159265358979 / 180.0; },
            [](double a, double, double) { return a * 180.0 / 3.14159265358979; },
         };
         int ok = 0;
         for (int op = 0; op < M::kOpCount; op++)
         {
            M node;
            node.op = op;
            node.ShadeInput(0)->Connect(&pa);
            node.ShadeInput(1)->Connect(&pb);
            node.ShadeInput(2)->Connect(&pc);
            if (run(("Math " + M::OpNames()[op]).c_str(), node, [&](int x, int y) { const double r = f[op](A(x), B(y), C); return V{ r, r, r }; }))
               ok++;
         }
         M cl; // clamp, and unwired pins reading the node's own numbers
         cl.op = M::kMultiply; cl.value[0] = 3.0f; cl.value[1] = 0.25f; cl.clampResult = true;
         if (run("Math own values", cl, [](int, int) { return V{ 0.75, 0.75, 0.75 }; })) ok++;
         cl.value[1] = 0.5f;
         if (run("Math clamp", cl, [](int, int) { return V{ 1.0, 1.0, 1.0 }; })) ok++;
         check(ok == M::kOpCount + 2, "2.5 R/L: Shade Math, 41 ops x 25 inputs match the hand formula", (double)ok, (double)M::kOpCount + 2);
      }

      // Shade Vector Math
      {
         Probe pa("vec3(-2.0 + 4.5 * uv.x, -1.5 + 4.0 * uv.y, 0.4)"), pb("vec3(-1.5 + 4.0 * uv.y, 0.75, -2.0 + 4.5 * uv.x)"), pc("vec3(0.3, -0.55, 0.15)");
         using M = ShadeVectorMathNode;
         auto per = [](V a, V b, V c, const std::function<double(double, double, double)>& f) { return V{ f(a.x, b.x, c.x), f(a.y, b.y, c.y), f(a.z, b.z, c.z) }; };
         auto dot = [](V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
         auto mul = [](V a, double s) { return V{ a.x * s, a.y * s, a.z * s }; };
         auto sub = [](V a, V b) { return V{ a.x - b.x, a.y - b.y, a.z - b.z }; };
         auto unit = [&](V a) { const double l = std::sqrt(dot(a, a)); return l > 0.0 ? mul(a, 1.0 / l) : V{ 0, 0, 0 }; };
         auto grey = [](double v) { return V{ v, v, v }; };
         const double eta = 0.8;
         const std::function<V(V, V, V)> f[M::kOpCount] = {
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double) { return x + y; }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double) { return x - y; }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double) { return x * y; }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double) { return y != 0.0 ? x / y : 0.0; }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double z) { return x * y + z; }); },
            [&](V a, V b, V) { return V{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; },
            [&](V a, V b, V) { return mul(b, dot(a, b) / dot(b, b)); },
            [&](V a, V b, V) { const V nn = unit(b); return sub(a, mul(nn, 2.0 * dot(a, nn))); },
            [&](V a, V b, V) {
               const V nn = unit(b);
               const double d = dot(nn, a), k = 1.0 - eta * eta * (1.0 - d * d);
               return k < 0.0 ? V{ 0, 0, 0 } : sub(mul(a, eta), mul(nn, eta * d + std::sqrt(k)));
            },
            [&](V a, V b, V c) { return dot(c, b) < 0.0 ? a : mul(a, -1.0); },
            [&](V a, V b, V) { return grey(dot(a, b)); },
            [&](V a, V b, V) { const V d = sub(a, b); return grey(std::sqrt(dot(d, d))); },
            [&](V a, V, V) { return grey(std::sqrt(dot(a, a))); },
            [&](V a, V, V) { return mul(a, eta); },
            [&](V a, V, V) { return unit(a); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double, double) { return std::fabs(x); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double) { return std::min(x, y); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double) { return std::max(x, y); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double, double) { return std::floor(x); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double, double) { return std::ceil(x); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double, double) { return x - std::floor(x); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double) { return std::fmod(x, y); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double z) { return x - (y - z) * std::floor((x - z) / (y - z)); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double y, double) { return std::floor(x / y) * y; }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double, double) { return std::sin(x); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double, double) { return std::cos(x); }); },
            [&](V a, V b, V c) { return per(a, b, c, [](double x, double, double) { return std::tan(x); }); },
         };
         int ok = 0;
         for (int op = 0; op < M::kOpCount; op++)
         {
            M node;
            node.op = op;
            node.scale = (float)eta;
            node.ShadeInput(0)->Connect(&pa);
            node.ShadeInput(1)->Connect(&pb);
            node.ShadeInput(2)->Connect(&pc);
            if (run(("Vector Math " + M::OpNames()[op]).c_str(), node, [&](int x, int y) {
                   return f[op](V{ A(x), B(y), 0.4 }, V{ B(y), 0.75, A(x) }, V{ 0.3, -0.55, 0.15 });
                }))
               ok++;
         }
         check(ok == M::kOpCount, "2.5 R/L: Shade Vector Math, 27 ops x 25 inputs match the hand formula", (double)ok, (double)M::kOpCount);
      }

      // Shade Mix: float, vector, and the 30 colour modes against core/BlendModes.cpp's formulas
      {
         Probe pa("vec3(0.03 + 0.9 * uv.x, 0.07 + 0.8 * uv.y, 0.35)"), pb("vec3(0.07 + 0.8 * uv.y, 0.6, 0.03 + 0.9 * uv.x)");
         auto ch = [](V a, V b, const std::function<double(double, double)>& f) { return V{ f(a.x, b.x), f(a.y, b.y), f(a.z, b.z) }; };
         auto lum = [](V c) { return 0.3 * c.x + 0.59 * c.y + 0.11 * c.z; };
         auto mn = [](V c) { return std::min({ c.x, c.y, c.z }); };
         auto mx = [](V c) { return std::max({ c.x, c.y, c.z }); };
         auto sat01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
         auto setSat = [&](V c, double s) {
            const double lo = mn(c), hi = mx(c);
            return hi > lo ? V{ (c.x - lo) * s / (hi - lo), (c.y - lo) * s / (hi - lo), (c.z - lo) * s / (hi - lo) } : V{ 0, 0, 0 };
         };
         auto setLum = [&](V c, double l) {
            const double d = l - lum(c);
            c = V{ c.x + d, c.y + d, c.z + d };
            const double L = lum(c), lo = mn(c), hi = mx(c);
            auto sc = [&](V q, double k) { return V{ L + (q.x - L) * k, L + (q.y - L) * k, L + (q.z - L) * k }; };
            if (lo < 0.0) c = sc(c, L / (L - lo + 1e-6));
            if (hi > 1.0) c = sc(c, (1.0 - L) / (hi - L + 1e-6));
            return c;
         };
         auto hard = [](double a, double b, bool top) { return top ? 1.0 - 2.0 * (1.0 - a) * (1.0 - b) : 2.0 * a * b; };
         auto blend = [&](int mode, V a, V b) -> V {
            switch (mode)
            {
            case 0: return b;
            case 1: return ch(a, b, [](double x, double y) { return std::min(x, y); });
            case 2: return ch(a, b, [](double x, double y) { return x * y; });
            case 3: return ch(a, b, [](double x, double y) { return 1.0 - std::min((1.0 - x) / std::max(y, 1e-4), 1.0); });
            case 4: return ch(a, b, [&](double x, double y) { return sat01(x + y - 1.0); });
            case 5: return lum(a) <= lum(b) ? a : b;
            case 6: return ch(a, b, [](double x, double y) { return std::max(x, y); });
            case 7: return ch(a, b, [](double x, double y) { return 1.0 - (1.0 - x) * (1.0 - y); });
            case 8: return ch(a, b, [](double x, double y) { return std::min(x / std::max(1.0 - y, 1e-4), 1.0); });
            case 9: return ch(a, b, [&](double x, double y) { return sat01(x + y); });
            case 10: return lum(a) >= lum(b) ? a : b;
            case 11: return ch(a, b, [&](double x, double y) { return hard(x, y, x >= 0.5); });
            case 12: return ch(a, b, [](double x, double y) {
               const double d = x >= 0.25 ? std::sqrt(x) : ((16.0 * x - 12.0) * x + 4.0) * x;
               return y >= 0.5 ? x + (2.0 * y - 1.0) * (d - x) : x - (1.0 - 2.0 * y) * x * (1.0 - x);
            });
            case 13: return ch(a, b, [&](double x, double y) { return hard(x, y, y >= 0.5); });
            case 14: return ch(a, b, [](double x, double y) {
               return y >= 0.5 ? std::min(x / std::max(2.0 * (1.0 - y), 1e-4), 1.0) : 1.0 - std::min((1.0 - x) / std::max(2.0 * y, 1e-4), 1.0);
            });
            case 15: return ch(a, b, [&](double x, double y) { return sat01(x + 2.0 * y - 1.0); });
            case 16: return ch(a, b, [](double x, double y) { return y >= 0.5 ? std::max(x, 2.0 * y - 1.0) : std::min(x, 2.0 * y); });
            case 17: return ch(a, b, [&](double x, double y) { return sat01(x + 2.0 * y - 1.0) >= 0.5 ? 1.0 : 0.0; });
            case 18: return ch(a, b, [](double x, double y) { return std::fabs(x - y); });
            case 19: return ch(a, b, [](double x, double y) { return x + y - 2.0 * x * y; });
            case 20: return ch(a, b, [&](double x, double y) { return sat01(x - y); });
            case 21: return ch(a, b, [](double x, double y) { return std::min(x / std::max(y, 1e-4), 1.0); });
            case 22: return setLum(setSat(b, mx(a) - mn(a)), lum(a));
            case 23: return setLum(setSat(a, mx(b) - mn(b)), lum(a));
            case 24: return setLum(b, lum(a));
            case 25: return setLum(a, lum(b));
            case 26: return ch(a, b, [](double x, double y) { return (x + y) * 0.5; });
            case 27: return ch(a, b, [](double x, double y) { return 1.0 - std::fabs(1.0 - x - y); });
            case 28: return ch(a, b, [](double x, double y) { return std::min(x * x / std::max(1.0 - y, 1e-4), 1.0); });
            default: return ch(a, b, [](double x, double y) { return std::min(y * y / std::max(1.0 - x, 1e-4), 1.0); });
            }
         };
         auto pA = [&](int x, int y) { return V{ 0.03 + 0.9 * U(x), 0.07 + 0.8 * U(y), 0.35 }; };
         auto pB = [&](int x, int y) { return V{ 0.07 + 0.8 * U(y), 0.6, 0.03 + 0.9 * U(x) }; };
         const double fac = 0.7;
         auto lerp = [&](V a, V b) { return V{ a.x + (b.x - a.x) * fac, a.y + (b.y - a.y) * fac, a.z + (b.z - a.z) * fac }; };
         int ok = 0, total = 0;
         for (int mode = 0; mode < (int)ShadeMixNode::ModeNames().size(); mode++)
         {
            ShadeMixNode node;
            node.mode = mode;
            node.factor = (float)fac;
            node.ShadeInput(1)->Connect(&pa);
            node.ShadeInput(2)->Connect(&pb);
            total++;
            if (run(("Mix " + ShadeMixNode::ModeNames()[mode]).c_str(), node, [&](int x, int y) { return lerp(pA(x, y), blend(mode, pA(x, y), pB(x, y))); }))
               ok++;
         }
         ShadeMixNode vec;
         vec.dataType = ShadeMixNode::kVector;
         vec.factor = (float)fac;
         vec.ShadeInput(1)->Connect(&pa);
         vec.ShadeInput(2)->Connect(&pb);
         total++;
         if (run("Mix vector", vec, [&](int x, int y) { return lerp(pA(x, y), pB(x, y)); })) ok++;
         ShadeMixNode fl; // float reads x; the factor pin is clamped to 0..1 (probe a runs 0.03 to 0.84, times 2)
         Probe pf("vec3(2.0 * (0.03 + 0.9 * uv.x))");
         fl.dataType = ShadeMixNode::kFloat;
         fl.ShadeInput(0)->Connect(&pf);
         fl.ShadeInput(1)->Connect(&pa);
         fl.ShadeInput(2)->Connect(&pb);
         total++;
         if (run("Mix float", fl, [&](int x, int y) {
                const double f = std::clamp(2.0 * (0.03 + 0.9 * U(x)), 0.0, 1.0), a = pA(x, y).x, b = pB(x, y).x, r = a + (b - a) * f;
                return V{ r, r, r };
             }))
            ok++;
         check(ok == total, "2.5 R/L: Shade Mix, float, vector and 30 colour modes match BlendModes", (double)ok, (double)total);
      }

      // Shade Ramp: 4 known stops, 25 factors from -0.076 to 1.076
      {
         Probe pt("vec3(-0.1 + 1.2 * (uv.x + floor(uv.y * 5.0)) / 5.0)");
         auto T = [&](int x, int y) { return -0.1 + 1.2 * (U(x) + y) / 5.0; };
         const double p[4] = { 0.1, 0.4, 0.6, 0.9 };
         const V col[4] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 1 } };
         auto mixv = [](V a, V b, double w) { return V{ a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w }; };
         auto ref = [&](int interp, double t) -> V {
            if (interp == ShadeRampNode::kBSpline)
            {
               t = std::clamp(t, p[0], p[3]);
               int i = t >= p[2] ? 2 : t >= p[1] ? 1 : 0;
               const double s = (t - p[i]) / (p[i + 1] - p[i]);
               const double w[4] = { (1 - s) * (1 - s) * (1 - s) / 6.0, (3 * s * s * s - 6 * s * s + 4) / 6.0, (-3 * s * s * s + 3 * s * s + 3 * s + 1) / 6.0, s * s * s / 6.0 };
               V r{ 0, 0, 0 };
               for (int k = 0; k < 4; k++)
               {
                  const V c = col[std::clamp(i - 1 + k, 0, 3)];
                  r = V{ r.x + c.x * w[k], r.y + c.y * w[k], r.z + c.z * w[k] };
               }
               return r;
            }
            if (t <= p[0]) return col[0];
            if (t >= p[3]) return col[3];
            const int i = t >= p[2] ? 2 : t >= p[1] ? 1 : 0;
            double w = (t - p[i]) / (p[i + 1] - p[i]);
            if (interp == ShadeRampNode::kConstant) return col[i];
            if (interp == ShadeRampNode::kEase) w = w * w * (3.0 - 2.0 * w);
            return mixv(col[i], col[i + 1], w);
         };
         int ok = 0;
         for (int interp = 0; interp < 4; interp++)
         {
            ShadeRampNode node;
            node.interp = interp;
            node.count = 4;
            // stored out of order: the node sorts by position
            const int slot[4] = { 2, 0, 3, 1 };
            for (int i = 0; i < 4; i++)
            {
               node.pos[slot[i]] = (float)p[i];
               node.color[slot[i]][0] = (float)col[i].x; node.color[slot[i]][1] = (float)col[i].y; node.color[slot[i]][2] = (float)col[i].z;
            }
            node.ShadeInput(0)->Connect(&pt);
            if (run(("Ramp " + ShadeRampNode::InterpNames()[interp]).c_str(), node, [&](int x, int y) { return ref(interp, T(x, y)); })) ok++;
         }
         check(ok == 4, "2.5 R/L: Shade Ramp, 4 known stops, linear / ease / constant / b-spline", (double)ok, 4.0);
      }

      // Shade Map Range, Separate / Combine, Curves
      {
         Probe pa("vec3(-2.0 + 4.5 * uv.x)");
         int ok = 0, total = 0;
         const double f0 = -1.0, f1 = 1.5, t0 = 2.0, t1 = -1.0, steps = 4.0;
         auto range = [&](int interp, bool clampIt, double v) {
            double f = (v - f0) / (f1 - f0), r;
            if (interp == ShadeMapRangeNode::kStepped) f = std::floor(f * (steps + 1.0)) / steps;
            if (interp >= ShadeMapRangeNode::kSmoothstep)
            {
               f = std::clamp(f, 0.0, 1.0);
               f = interp == ShadeMapRangeNode::kSmoothstep ? f * f * (3.0 - 2.0 * f) : f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
               return t0 + f * (t1 - t0);
            }
            r = t0 + f * (t1 - t0);
            return clampIt ? std::clamp(r, std::min(t0, t1), std::max(t0, t1)) : r;
         };
         for (int k = 0; k < 5; k++)
         {
            const int interp = k < 4 ? k : 0;
            const bool cl = k < 4;
            ShadeMapRangeNode node;
            node.interp = interp;
            node.fromMin = (float)f0; node.fromMax = (float)f1; node.toMin = (float)t0; node.toMax = (float)t1; node.steps = (float)steps;
            node.clampResult = cl;
            node.ShadeInput(0)->Connect(&pa);
            total++;
            if (run(("Map Range " + ShadeMapRangeNode::InterpNames()[interp] + (cl ? "" : " unclamped")).c_str(), node,
                    [&](int x, int) { const double r = range(interp, cl, A(x)); return V{ r, r, r }; }))
               ok++;
         }
         check(ok == total, "2.5 R/L: Shade Map Range, 4 interpolations and unclamped", (double)ok, (double)total);

         Probe pcol("vec3(0.03 + 0.9 * uv.x, 0.07 + 0.8 * uv.y, 0.35)");
         auto colAt = [&](int x, int y) { return V{ 0.03 + 0.9 * U(x), 0.07 + 0.8 * U(y), 0.35 }; };
         auto hsv = [](V c) {
            const double hi = std::max({ c.x, c.y, c.z }), lo = std::min({ c.x, c.y, c.z }), d = hi - lo;
            double h = d == 0.0 ? 0.0 : (c.x == hi ? (c.y - c.z) / d : c.y == hi ? 2.0 + (c.z - c.x) / d : 4.0 + (c.x - c.y) / d) / 6.0;
            if (h < 0.0) h += 1.0;
            return V{ h, hi > 0.0 ? d / hi : 0.0, hi };
         };
         auto rgb = [&](V q) {
            auto chn = [&](double k) { return q.z * (1.0 + q.y * (std::clamp(std::fabs(std::fmod(q.x * 6.0 + k, 6.0) - 3.0) - 1.0, 0.0, 1.0) - 1.0)); };
            return V{ chn(0.0), chn(4.0), chn(2.0) };
         };
         ok = total = 0;
         ShadeSeparateNode sep[6];
         for (int k = 0; k < 6; k++)
         {
            sep[k].mode = k / 3;
            sep[k].channel = k % 3;
            sep[k].ShadeInput(0)->Connect(&pcol);
            total++;
            if (run(k < 3 ? "Separate RGB" : "Separate HSV", sep[k], [&](int x, int y) {
                   const V c = k < 3 ? colAt(x, y) : hsv(colAt(x, y));
                   const double r = k % 3 == 0 ? c.x : k % 3 == 1 ? c.y : c.z;
                   return V{ r, r, r };
                }))
               ok++;
         }
         ShadeCombineNode comb; // x, y, z straight
         for (int k = 0; k < 3; k++) comb.ShadeInput(k)->Connect(&sep[k]);
         total++;
         if (run("Combine RGB", comb, [&](int x, int y) { return colAt(x, y); })) ok++;
         ShadeCombineNode back; // separate to HSV and back gives the colour again
         back.mode = 1;
         for (int k = 0; k < 3; k++) back.ShadeInput(k)->Connect(&sep[3 + k]);
         total++;
         if (run("Combine HSV round trip", back, [&](int x, int y) { return colAt(x, y); })) ok++;
         ShadeCombineNode hc; // hue from the picture, own saturation and value
         hc.mode = 1;
         hc.value[1] = 0.6f; hc.value[2] = 0.8f;
         hc.ShadeInput(0)->Connect(&sep[0]);
         total++;
         if (run("Combine HSV", hc, [&](int x, int y) { return rgb(V{ colAt(x, y).x, 0.6, 0.8 }); })) ok++;
         check(ok == total, "2.5 R/L: Shade Separate / Combine, RGB and HSV, round trip", (double)ok, (double)total);

         Probe pt("vec3(-0.1 + 1.2 * (uv.x + floor(uv.y * 5.0)) / 5.0)");
         auto T = [&](int x, int y) { return -0.1 + 1.2 * (U(x) + y) / 5.0; };
         const double cx[4] = { 0.0, 0.3, 0.7, 1.0 }, cy[4] = { 0.0, 0.5, 0.6, 1.0 };
         auto curve = [&](double t) {
            double m[4];
            for (int i = 0; i < 4; i++)
            {
               const int lo = std::max(i - 1, 0), hi = std::min(i + 1, 3);
               m[i] = (cy[hi] - cy[lo]) / (cx[hi] - cx[lo]);
            }
            if (t < cx[0]) return cy[0] + m[0] * (t - cx[0]);
            if (t > cx[3]) return cy[3] + m[3] * (t - cx[3]);
            const int i = t >= cx[2] ? 2 : t >= cx[1] ? 1 : 0;
            const double h = cx[i + 1] - cx[i], s = (t - cx[i]) / h, s2 = s * s, s3 = s2 * s;
            return cy[i] * (2 * s3 - 3 * s2 + 1) + h * m[i] * (s3 - 2 * s2 + s) + cy[i + 1] * (3 * s2 - 2 * s3) + h * m[i + 1] * (s3 - s2);
         };
         ok = total = 0;
         ShadeCurvesNode cv;
         cv.count = 4;
         for (int i = 0; i < 4; i++) { cv.px[i] = (float)cx[i]; cv.py[i] = (float)cy[i]; }
         cv.ShadeInput(0)->Connect(&pt);
         total++;
         if (run("Curves 4 points", cv, [&](int x, int y) { const double r = curve(T(x, y)); return V{ r, r, r }; })) ok++;
         cv.factor = 0.25f;
         total++;
         if (run("Curves factor", cv, [&](int x, int y) { const double t = T(x, y), r = t + (curve(t) - t) * 0.25; return V{ r, r, r }; })) ok++;
         ShadeCurvesNode id; // the default curve changes nothing, also outside 0..1
         id.ShadeInput(0)->Connect(&pa);
         total++;
         if (run("Curves identity", id, [&](int x, int) { return V{ A(x), A(x), A(x) }; })) ok++;
         check(ok == total, "2.5 R/L: Shade Curves, 4 points, factor, identity default", (double)ok, (double)total);
      }
      printf("    2.5 worst error %.3f x tolerance (tolerance 0.002 + 0.1%%), %d node programs off\n", worst, bad);

      // ---- 2.6: surface helpers against hand formulas. The Material inputs they read (N, V, P, W, backfacing,
      // curvature, cavity, objrand) are plain locals here, varying over the picture.
      {
         const std::string pre =
            "N = normalize(vec3(uv.x * 1.6 - 0.8, uv.y * 1.2 - 0.6, 0.5))\n"
            "V = normalize(vec3(0.2, 0.1 + uv.x * 0.0, 1.0))\n"
            "P = vec3(uv.x, 0.3, uv.y)\n"
            "W = vec3(uv.x * 2.0, uv.y, 0.2)\n"
            "backfacing = step(0.55, uv.y)\n"
            "curvature = uv.x * 0.3\n"
            "cavity = uv.y * 0.6\n"
            "objrand = uv.x * uv.y\n";
         struct D3 { double x, y, z; };
         auto nrm = [](D3 a) { const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return D3{ a.x / l, a.y / l, a.z / l }; };
         auto crs = [](D3 a, D3 b) { return D3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; };
         auto Nn = [&](int x, int y) { return nrm({ U(x) * 1.6 - 0.8, U(y) * 1.2 - 0.6, 0.5 }); };
         const D3 Vv = nrm({ 0.2, 0.1, 1.0 });
         auto cosNV = [&](int x, int y) { const D3 a = Nn(x, y); return std::fabs(a.x * Vv.x + a.y * Vv.y + a.z * Vv.z); };
         auto back = [&](int y) { return U(y) >= 0.55; };
         auto fresnel = [](double c, double eta) {
            const double g2 = eta * eta - 1.0 + c * c;
            if (g2 <= 0.0) return 1.0;
            const double g = std::sqrt(g2), a = (g - c) / (g + c), b = (c * (g + c) - 1.0) / (c * (g - c) + 1.0);
            return 0.5 * a * a * (1.0 + b * b);
         };
         auto S = [](double r) { return V{ r, r, r }; };
         int ok = 0, total = 0;
         ShadeFresnelNode fn; // IOR 1.45
         total++;
         if (run("Fresnel 1.45", fn, [&](int x, int y) { return S(fresnel(cosNV(x, y), back(y) ? 1.0 / 1.45 : 1.45)); }, pre)) ok++;
         Probe pi("vec3(1.0 + 1.5 * uv.x)");
         ShadeFresnelNode fw;
         fw.ShadeInput(0)->Connect(&pi);
         total++;
         if (run("Fresnel wired", fw, [&](int x, int y) { const double i = 1.0 + 1.5 * U(x); return S(fresnel(cosNV(x, y), back(y) ? 1.0 / i : i)); }, pre)) ok++;
         ShadeLayerWeightNode lw;
         lw.blend = 0.3f;
         total++;
         if (run("Layer Weight fresnel", lw, [&](int x, int y) { return S(fresnel(cosNV(x, y), back(y) ? 0.7 : 1.0 / 0.7)); }, pre)) ok++;
         lw.output = ShadeLayerWeightNode::kFacing;
         for (float bl : { 0.0f, 0.3f, 0.5f, 0.8f, 1.0f })
         {
            lw.blend = bl;
            total++;
            if (run("Layer Weight facing", lw, [&](int x, int y) {
                   const double k = std::clamp((double)bl, 0.0, 0.99999), e = k < 0.5 ? 2.0 * k : 0.5 / (1.0 - k);
                   return S(1.0 - std::pow(std::max(cosNV(x, y), 1e-6), e));
                }, pre)) ok++;
         }
         check(ok == total, "2.6 R/L: Shade Fresnel and Layer Weight match the hand formula", (double)ok, (double)total);

         ok = total = 0;
         Probe pcol("vec3(uv.x, uv.y, 0.9)");
         ShadeNormalMapNode nm;
         nm.strength = 0.7f;
         nm.ShadeInput(0)->Connect(&pcol);
         total++;
         if (run("Normal Map", nm, [&](int x, int y) {
                const D3 t{ U(x) * 2.0 - 1.0, U(y) * 2.0 - 1.0, 0.8 }, r = nrm({ t.x * 0.7, t.y * 0.7, 1.0 + (t.z - 1.0) * 0.7 });
                return V{ r.x, r.y, r.z };
             }, pre)) ok++;
         ShadeNormalMapNode nflat; // nothing wired: the flat normal
         total++;
         if (run("Normal Map unwired", nflat, [&](int, int) { return V{ 0.0, 0.0, 1.0 }; }, pre)) ok++;
         check(ok == total, "2.6 R/L: Shade Normal Map, strength 0.7 and unwired", (double)ok, (double)total);

         ok = total = 0;
         using G = ShadeGeometryNode;
         const std::function<V(int, int)> gref[] = {
            [&](int x, int y) { return V{ U(x) * 2.0, U(y), 0.2 }; },
            [&](int x, int y) { const D3 a = Nn(x, y); return V{ a.x, a.y, a.z }; },
            [&](int x, int y) {
               const D3 a = Nn(x, y), d{ U(x) - 0.5, -0.2, U(y) - 0.5 }, r{ d.z, 0.0, -d.x };
               D3 c = crs(r, a); c.x += 1e-7;
               const D3 t = crs(a, nrm(c));
               return V{ t.x, t.y, t.z };
            },
            [&](int, int) { return V{ Vv.x, Vv.y, Vv.z }; },
            [&](int, int y) { return S(back(y) ? 1.0 : 0.0); },
            [&](int x, int) { return S(U(x) * 0.3); },
            [&](int, int y) { return S(U(y) * 0.6); },
            [&](int x, int y) { return S(U(x) * U(y)); },
         };
         for (int o = 0; o < 8; o++)
         {
            G gnode;
            gnode.output = o;
            total++;
            if (run(G::OutputNames()[o].c_str(), gnode, gref[o], pre)) ok++;
         }
         check(ok == total, "2.6 R/L: Shade Geometry, 8 outputs", (double)ok, (double)total);
         printf("    2.6 worst error so far %.3f x tolerance, %d node programs off in all\n", worst, bad);
      }
   }

   // ---- 3.2: a Sketch as a material map. An animated sketch whose picture does not change uploads once and keeps
   // its revision (the engines read a map back only on a new revision); one that moves uploads every frame.
   {
      SketchNode sk;
      sk.width = 64.0f; sk.height = 64.0f; sk.animate = true;
      sk.code = "function draw(t) {\n  background(25 / 255);\n  noStroke();\n  fill(230 / 255);\n"
                "  for (let k = 1; k < 8; k += 2) rect(0, k * 8, width, 8);\n}\n";
      const double t0 = Transport::Instance().Seconds();
      unsigned long long rev0 = 0;
      for (int i = 0; i < 100; i++)
      {
         Transport::Instance().Seek(t0 + i / 60.0);
         sk.CookIfNeeded(900000 + i);
         if (i == 0) rev0 = sk.TextureRevision();
      }
      check(sk.LastError().empty() && sk.GetOutputTexture() != 0 && sk.Uploads() == 1 && sk.TextureRevision() == rev0,
            "3.2: an unchanged animated sketch uploads once in 100 frames", (double)sk.Uploads(), 1);
      sk.code = "function draw(t) {\n  background(25 / 255);\n  fill(230 / 255);\n  rect(t * 60 % 64, 0, 8, 64);\n}\n";
      const int before = sk.Uploads();
      for (int i = 0; i < 10; i++)
      {
         Transport::Instance().Seek(t0 + 2.0 + i / 60.0);
         sk.CookIfNeeded(900200 + i);
      }
      check(sk.Uploads() - before == 10, "3.2: a moving sketch uploads every frame (10 / 10)", (double)(sk.Uploads() - before), 10);
      Transport::Instance().Seek(t0);
   }

   // ---- 2.6: Shade Bump into Material's normal pin writes `height`; the sphere's shading changes on L, and the
   // base still comes from the chain on the field pin.
   {
      struct Probe : ShadeNode
      {
         std::string e;
         explicit Probe(const char* s) : e(s) {}
         std::string Emit(ShadeBuild& b) const override { return b.NewVar(this, e); }
      };
      Probe ph("vec3(sin(P.x * 40.0) * sin(P.y * 40.0))"), pbase("vec3(0.8, 0.8, 0.8)");
      ShadeBumpNode bump;
      bump.strength = 0.5f; bump.distance = 0.2f; bump.invert = true;
      bump.ShadeInput(0)->Connect(&ph);
      MaterialNode bmat;
      bmat.input = &sphereG;
      bmat.FieldInput().Connect(&pbase);
      const int flat = bmat.GetMaterial().fieldProgram;
      bmat.MapInput(kMapNormal).Connect(&bump);
      const float lo[3] = { 0.0f, 0.0f, 0.0f }, hi[3] = { 1.0f, 1.0f, 1.0f };
      std::string berr;
      const std::string bsrc = ComposeShadeProgram(std::vector<ShadeOutput>{ { "base", &pbase }, { "normal", &bump } }, lo, hi, berr);
      printf("    bump program:\n%s", bsrc.c_str());
      check(bsrc.find("height = sh") != std::string::npos && bsrc.find(".x * (-0.1)") != std::string::npos && bsrc.find("base = sh") != std::string::npos,
            "2.6: Bump on the normal pin writes height = h x strength x distance, inverted", (double)bsrc.size(), 0);
      const int bid = bmat.GetMaterial().fieldProgram;
      const Field::MaterialProgramEntry* be = bid > 0 ? Field::FindMaterialProgram(bid) : nullptr;
      check(bid > 0 && bid != flat && be && (be->written & (1u << Field::MaterialOutputBit("height"))) != 0,
            "2.6: the Material registers a program that writes height", (double)bid, (double)flat);
      std::vector<float> pf, pb2;
      engine::Frame ff = frame, fb = frame;
      ff.instances[2].fieldProgram = flat;
      ff.fieldMsl = Field::BuildMaterialLibrary({ flat }, Field::MaterialLang::Msl, ff.fieldMslKey);
      fb.instances[2].fieldProgram = bid;
      fb.fieldMsl = Field::BuildMaterialLibrary({ bid }, Field::MaterialLang::Msl, fb.fieldMslKey);
      const bool okf = flat > 0 && raster->Render(*img, ff, nullptr) && raster->Render(*img, ff, nullptr) && ReadRect(img->GlTexture(), W, H, pf);
      const bool okb = bid > 0 && raster->Render(*img, fb, nullptr) && raster->Render(*img, fb, nullptr) && ReadRect(img->GlTexture(), W, H, pb2);
      const std::string perr = bid > 0 ? Field::MaterialProgramError(bid) : std::string("no program");
      if (!perr.empty()) printf("    bump program error: %s\n", perr.c_str());
      check(perr.empty(), "2.6 L: bump program compiles", (double)perr.size(), 0);
      size_t changed = 0;
      for (size_t i = 0; okf && okb && i + 3 < pf.size() && i + 3 < pb2.size(); i += 4)
         if (std::fabs(pf[i] - pb2[i]) > 0.03f) changed++;
      check(okf && okb && changed > (size_t)W * H / 400, "2.6 L: the bump changes the sphere's shading", (double)changed, (double)((size_t)W * H / 400));
   }

   // ---- 3.4: a material-program knob is live data. 100 moves of k keep one program id and one library key (no
   // pipeline rebuild), the Material carries the value, and the picture on L follows it.
   {
      FieldPixelNode kfp;
      kfp.code = "param float k = 0.5 [0.0, 1.0];\nbase = vec3(k)\nroughness = 0.6";
      kfp.domain = 1;
      const bool kap = kfp.Apply();
      MaterialNode kmat;
      kmat.input = &sphereG;
      kmat.FieldInput().Connect(&kfp);
      const int kid = kmat.GetMaterial().fieldProgram;
      unsigned long long key0 = 0;
      Field::BuildMaterialLibrary({ kid }, Field::MaterialLang::Msl, key0);
      int rebuilds = 0, follow = 0;
      for (int i = 0; i < 100; i++)
      {
         const float v = (float)i / 99.0f;
         if (Field::ParamEntry* e = kfp.GetParamTable().Find("k")) e->value = v;
         kfp.CookIfNeeded(910000 + i);
         const Material& km = kmat.GetMaterial();
         unsigned long long key = 0;
         Field::BuildMaterialLibrary({ km.fieldProgram }, Field::MaterialLang::Msl, key);
         if (km.fieldProgram != kid || key != key0) rebuilds++;
         if (std::fabs(km.fieldParams[0] - v) < 1e-6f) follow++;
      }
      check(kap && kid > 0 && rebuilds == 0 && follow == 100, "3.4: 100 knob moves, 0 program rebuilds, value carried 100 / 100",
            (double)rebuilds, (double)follow);
      std::vector<float> plo, phi;
      engine::Frame fl = frame, fh = frame;
      for (engine::Frame* fr : { &fl, &fh })
      {
         fr->instances[2].fieldProgram = kid;
         fr->fieldMsl = Field::BuildMaterialLibrary({ kid }, Field::MaterialLang::Msl, fr->fieldMslKey);
      }
      fl.instances[2].fieldParams[0] = 0.2f; fh.instances[2].fieldParams[0] = 0.8f;
      const bool okl = kid > 0 && raster->Render(*img, fl, nullptr) && raster->Render(*img, fl, nullptr) && ReadRect(img->GlTexture(), W, H, plo);
      const bool okh = kid > 0 && raster->Render(*img, fh, nullptr) && raster->Render(*img, fh, nullptr) && ReadRect(img->GlTexture(), W, H, phi);
      size_t brighter = 0;
      for (size_t i = 0; okl && okh && i + 3 < plo.size() && i + 3 < phi.size(); i += 4)
         if (phi[i] - plo[i] > 0.05f) brighter++;
      check(okl && okh && fl.fieldMslKey == fh.fieldMslKey && brighter > (size_t)W * H / 100,
            "3.4 L: k 0.2 -> 0.8 brightens the sphere with the same library", (double)brighter, (double)((size_t)W * H / 100));
   }

   // ---- 4.1: Material layers. Red gloss paint laid over grey rough metal: one program, the layer's knobs live (100
   // moves of layerMix, 0 rebuilds), mix 1 reads as the paint and mix 0 as the metal on L; a Shade mask splits the
   // sphere; a 4-deep chain compiles.
   {
      struct MaskProbe : ShadeNode
      {
         std::string e;
         explicit MaskProbe(const char* s) : e(s) {}
         std::string Emit(ShadeBuild& b) const override { return b.NewVar(this, e); }
      };
      MaterialNode metal, paint, dirt, dust;
      metal.input = &sphereG;
      metal.color[0] = metal.color[1] = metal.color[2] = 0.5f; metal.metallic = 1.0f; metal.roughness = 0.7f;
      paint.color[0] = 0.8f; paint.color[1] = 0.08f; paint.color[2] = 0.08f; paint.metallic = 0.0f; paint.roughness = 0.2f;
      metal.layer = &paint;
      const Material lm = metal.GetMaterial();
      const int lid = lm.fieldProgram;
      const std::string lerr = lid > 0 ? Field::MaterialProgramError(lid) : std::string("no program");
      if (!lerr.empty()) printf("    layer program error: %s\n", lerr.c_str());
      int lre = 0;
      for (int i = 0; i < 100; i++)
      {
         metal.layerMix = (float)i / 99.0f;
         paint.roughness = 0.2f + 0.001f * (float)i;
         if (metal.GetMaterial().fieldProgram != lid) lre++;
      }
      check(lid > 0 && lerr.empty() && lre == 0, "4.1: a layer is one program; 100 knob moves, 0 rebuilds", (double)lre, 0);
      auto shot = [&](const Material& mm, std::vector<float>& px)
      {
         engine::Frame fr = frame;
         fr.instances[2].fieldProgram = mm.fieldProgram;
         std::memcpy(fr.instances[2].fieldParams, mm.fieldParams, sizeof(mm.fieldParams));
         fr.fieldMsl = Field::BuildMaterialLibrary({ mm.fieldProgram }, Field::MaterialLang::Msl, fr.fieldMslKey);
         return mm.fieldProgram > 0 && raster->Render(*img, fr, nullptr) && raster->Render(*img, fr, nullptr) && ReadRect(img->GlTexture(), W, H, px);
      };
      auto red = [&](const std::vector<float>& px, size_t& n)
      {
         n = 0;
         for (size_t i = 0; i + 3 < px.size(); i += 4)
            if (px[i] > px[i + 1] + 0.15f) n++;
      };
      // Counted against mix 0: the rest of the scene has red of its own.
      std::vector<float> p1, p0, ph;
      metal.layerMix = 1.0f;
      const bool o1 = shot(metal.GetMaterial(), p1);
      metal.layerMix = 0.0f;
      const bool o0 = shot(metal.GetMaterial(), p0);
      size_t r1 = 0, r0 = 0, rh = 0;
      red(p1, r1); red(p0, r0);
      const double paintPx = (double)r1 - (double)r0;
      check(o1 && o0 && paintPx > (double)W * H / 200, "4.1 L: mix 1 paints the sphere red, mix 0 leaves the metal",
            paintPx, (double)W * H / 200);
      MaskProbe half("vec3(step(0.0, P.x))");   // the test sphere has no UVs: split it in object space
      metal.layerMix = 1.0f;
      metal.LayerMaskInput().Connect(&half);
      const Material hm = metal.GetMaterial();
      const bool oh = shot(hm, ph);
      red(ph, rh);
      const double halfPx = (double)rh - (double)r0;
      check(oh && halfPx > paintPx / 5 && halfPx < paintPx * 4 / 5, "4.1 L: a Shade mask paints part of the sphere", halfPx, paintPx);
      paint.layer = &dirt; dirt.layer = &dust;
      dirt.color[0] = 0.3f; dust.roughness = 0.9f; dust.layerMix = 0.5f;
      const int deep = metal.GetMaterial().fieldProgram;
      const std::string derr = deep > 0 ? Field::MaterialProgramError(deep) : std::string("no program");
      if (!derr.empty()) printf("    4-deep program error: %s\n", derr.c_str());
      check(deep > 0 && derr.empty(), "4.1: a 4-deep layer chain compiles", (double)deep, 0);
      metal.LayerMaskInput().Disconnect();
   }

   // ---- 4.4: curvature and cavity per vertex. A ~100 k vertex sphere reads curvature 1 (radius-normalised) and
   // cavity 0, baked under 200 ms; only a program that reads them asks for the bake.
   {
      Mesh sp = Primitives::Sphere(224, 448);
      for (Vertex& v : sp.vertices) { v.px *= 3.0f; v.py *= 3.0f; v.pz *= 3.0f; } // any size reads the same
      const auto t0 = std::chrono::steady_clock::now();
      const std::vector<float> wr = MeshWear::Bake(&sp.vertices[0].px, sizeof(Vertex) / sizeof(float), sp.vertices.size(), sp.indices.data(), sp.indices.size());
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      std::vector<float> cs;
      double cavSum = 0.0;
      for (size_t i = 0; i < sp.vertices.size() && wr.size() == sp.vertices.size() * 2; i++)
      {
         if (std::fabs(sp.vertices[i].ny) < 0.95f) cs.push_back(wr[i * 2]); // poles of a uv sphere are fans
         cavSum += wr[i * 2 + 1];
      }
      std::sort(cs.begin(), cs.end());
      const double med = cs.empty() ? 0.0 : cs[cs.size() / 2], cav = cavSum / std::max<size_t>(sp.vertices.size(), 1);
      printf("    4.4 sphere %zu vertices: bake %.0f ms, curvature median %.3f, cavity mean %.4f\n", sp.vertices.size(), ms, med, cav);
      check(std::fabs(med - 1.0) < 0.03 && cav < 0.01, "4.4: sphere curvature 1, cavity 0", med, 1.0);
      check(sp.vertices.size() >= 100000 && ms < 200.0, "4.4: wear bake of 100 k vertices under 200 ms", ms, 200.0);
      std::string e1, e2;
      const int pw = Field::RegisterMaterialProgram("emission = vec3(curvature + cavity)", {}, e1);
      const int pn = Field::RegisterMaterialProgram("base = vec3(0.25, 0.5, 0.75)", {}, e2);
      const Field::MaterialProgramEntry* ew = Field::FindMaterialProgram(pw);
      const Field::MaterialProgramEntry* en = Field::FindMaterialProgram(pn);
      check(ew && en && ew->usesWear && !en->usesWear, "4.4: only a program reading curvature or cavity asks for the bake", ew && ew->usesWear, 1);
   }

   // ---- 3.3: attr reaches the stream: a program reading it asks for it (not the bake), and Stream carries the mask
   {
      std::string e1;
      const int pa = Field::RegisterMaterialProgram("base = vec3(attr)", {}, e1);
      const Field::MaterialProgramEntry* ea = Field::FindMaterialProgram(pa);
      const float tri[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
      const unsigned int ix[3] = { 0, 1, 2 };
      const std::vector<float> st = MeshWear::Stream(tri, 3, 3, ix, 3, false, { 0.0f, 1.0f, 0.5f });
      check(ea && ea->usesAttr && !ea->usesWear && st.size() == 9 && st[5] == 1.0f && st[8] == 0.5f,
            "3.3: attr program asks for the stream, mask rides in it", ea && ea->usesAttr, 1);
   }

   // ---- 3.5 / 4.5: every Material recipe lowers and registers; the wear ones ask for the bake
   {
      int ok = 0, wear = 0;
      for (const auto& r : FieldPixelNode::MaterialPresets())
      {
         std::string err;
         const int id = Field::RegisterMaterialProgram(r.code, {}, err);
         const Field::MaterialProgramEntry* e = id ? Field::FindMaterialProgram(id) : nullptr;
         if (e && err.empty()) ok++;
         else printf("    recipe %s: %s\n", r.name, err.c_str());
         if (e && e->usesWear) wear++;
      }
      const int n = (int)FieldPixelNode::MaterialPresets().size();
      check(ok == n && wear >= 2, "3.5/4.5: every Material recipe compiles", ok, n);
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
   if (const char* l64 = getenv("INFINITE_ENGINETEST_LIGHTS64"))
   {
      // Row 5.6, case lights_64: the same objects under 64 small point lights (an 8 x 8 grid, each with a cutoff),
      // drawn on R (GL), L (live) and T (settle). The PPMs go to the named folder; FLIP is measured outside
      // (tools/blender-ab/lights64.py). Timing: L's frame with 64 lights against the same scene with 8.
      const int S = 256;
      Render3DNode::Rig rig64 = rig;
      rig64.proj = Mat4::Perspective(45.0f * 3.14159265f / 180.0f, 1.0f, 0.05f, 100.0f);
      rig64.lights.clear();
      for (int i = 0; i < 64; i++)
      {
         Render3DNode::RigLight l;
         l.type = 1;
         l.vec[0] = -3.5f + (float)(i % 8); l.vec[1] = -0.6f + 0.25f * (float)((i / 3) % 3); l.vec[2] = -3.5f + (float)(i / 8);
         const float h = 6.2831853f * (float)i / 64.0f;
         l.color[0] = 0.55f + 0.45f * std::cos(h); l.color[1] = 0.55f + 0.45f * std::cos(h - 2.094f); l.color[2] = 0.55f + 0.45f * std::cos(h + 2.094f);
         l.intensity = 0.6f; l.falloff = -1.0f; l.cutoff = 1.6f; l.castShadow = false;
         rig64.lights.push_back(l);
      }
      // The 8-light frame: 8 of them along the diagonal, no cutoff, so every pixel shades all 8 (the old path's work);
      // the 64-light frame shades the ~8 within each pixel's reach, so the two frames do the same lighting per pixel.
      Render3DNode::Rig rig8 = rig64;
      rig8.lights.clear();
      for (int i = 0; i < 8; i++) { rig8.lights.push_back(rig64.lights[(size_t)(i * 9)]); rig8.lights.back().cutoff = 0.0f; }
      Render3DNode q;
      for (int i = 0; i < 4; i++) q.geometry[i] = nodes[i];
      q.width = (float)S; q.height = (float)S; q.samples = 0; q.hdr = false; q.tonemap = 1;
      q.rig = &rig64;
      std::vector<float> pR, pL, pT;
      auto grab = [&](std::vector<float>& px) { return GLUtil::ReadTexturePixels(scratch, q.GetOutputTexture(), q.GetOutputWidth(), q.GetOutputHeight(), px); };
      auto ppm = [&](const char* name, const std::vector<float>& px)
      {
         FILE* fp = fopen((std::string(l64) + "/" + name).c_str(), "wb");
         if (!fp) return;
         fprintf(fp, "P6\n%d %d\n255\n", S, S);
         for (int y = S - 1; y >= 0; y--)
            for (int x = 0; x < S; x++)
               for (int c = 0; c < 3; c++)
                  fputc((int)std::lround(std::clamp(px[((size_t)y * S + x) * 4 + c], 0.0f, 1.0f) * 255.0f), fp);
         fclose(fp);
      };
      auto timeL = [&](int frames)
      {
         std::vector<double> t;
         for (int i = 0; i < frames + 5; i++)
         {
            q.exposure = 1.0f + 0.0001f * (float)(i % 2);
            const double t0 = NowMs();
            q.CookIfNeeded(++fnum);
            glFinish();
            if (i >= 5) t.push_back(NowMs() - t0);
         }
         std::sort(t.begin(), t.end());
         return t[t.size() / 2];
      };
      // One set per rig: R, then L (timed), then T, written as R64/L64/T64 and R8/L8/T8.
      auto shoot = [&](Render3DNode::Rig* rg, const char* tag)
      {
         q.rig = rg; q.liveEngine = false;
         q.exposure = 1.001f; q.CookIfNeeded(++fnum); q.exposure = 1.0f; q.CookIfNeeded(++fnum);
         check(grab(pR), "lights_64 R picture", 0, 0); ppm((std::string("R") + tag + ".ppm").c_str(), pR);
         q.liveEngine = true; q.liveEngineFeatures = 7;
         const double ms = timeL(40);
         q.exposure = 1.0f; q.CookIfNeeded(++fnum);
         check(grab(pL), "lights_64 L picture", 0, 0); ppm((std::string("L") + tag + ".ppm").c_str(), pL);
         q.liveEngineFeatures = 15; q.ptSamples = 512; q.ptBounces = 4;
         for (int i = 0; i < 60; i++) q.CookIfNeeded(++fnum);
         check(grab(pT), "lights_64 T picture", 0, 0); ppm((std::string("T") + tag + ".ppm").c_str(), pT);
         q.liveEngine = false;
         return ms;
      };
      q.CookIfNeeded(++fnum);
      const double ms64 = shoot(&rig64, "64");
      const double ms8 = shoot(&rig8, "8");
      printf("  lights_64 L frame %.2f ms, 8 lights %.2f ms, ratio %.2f (%dx%d)\n", ms64, ms8, ms64 / std::max(ms8, 1e-3), S, S);
      check(ms64 / std::max(ms8, 1e-3) < 1.3, "lights_64 L frame under 1.3x the 8-light frame", ms64 / std::max(ms8, 1e-3), 1.3);
      q.liveEngine = false;
   }
   if (const char* glb = getenv("INFINITE_ENGINETEST_GLTF"))
   {
      // Row 7.1: a glTF on disk through the drop's own ApplyGltfMaterial, drawn on R, L and T at 256 px. PPMs go to
      // INFINITE_ENGINETEST_OUT; the KHR factors the file has must reach the Material.
      const char* outDir = getenv("INFINITE_ENGINETEST_OUT");
      const int S = 256;
      std::string err;
      const GltfImport::GltfDecodePackage* file = GltfImport::DecodeCached(glb, err);
      check(file != nullptr, "glTF decodes", 0, 0);
      if (file != nullptr)
      {
         const GltfImport::GltfDecodePackage& pk = *file->Part(0);
         printf("  gltf part 0 '%s': clearcoat %.3f/%.3f sheen %.3f emissive x%.2f ior %.2f transmission %.2f irid %.2f aniso %.2f uvT %d\n",
                pk.materialName.c_str(), pk.clearcoat, pk.clearcoatRoughness, pk.sheen, pk.emissiveStrength, pk.ior,
                pk.transmission, pk.iridescence, pk.anisotropy, (int)pk.hasUvTransform);
         ModelSourceNode model;
         model.Load(glb);
         MaterialNode mat;
         mat.input = &model;
         ApplyGltfMaterial(&mat, pk);
         check(std::fabs(mat.clearcoat - pk.clearcoat) < 1e-6f && std::fabs(mat.sheen - pk.sheen) < 1e-6f &&
               std::fabs(mat.ior - pk.ior) < 1e-6f, "glTF KHR factors reach the Material", mat.clearcoat, pk.clearcoat);
         Render3DNode::Rig rg = rig;
         const float e2[3] = { 0.0f, 0.7f, 2.6f }, t2[3] = { 0.0f, 0.0f, 0.0f };
         rg.view = Mat4::LookAt(e2, t2, up); rg.eye[0] = e2[0]; rg.eye[1] = e2[1]; rg.eye[2] = e2[2];
         rg.proj = Mat4::Perspective(40.0f * 3.14159265f / 180.0f, 1.0f, 0.05f, 100.0f);
         rg.lights.resize(2);
         Render3DNode q;
         q.geometry[0] = &mat;
         q.width = (float)S; q.height = (float)S; q.samples = 0; q.hdr = false; q.tonemap = 1; q.rig = &rg;
         std::vector<float> pR, pL, pT;
         auto grab = [&](std::vector<float>& px) { return GLUtil::ReadTexturePixels(scratch, q.GetOutputTexture(), q.GetOutputWidth(), q.GetOutputHeight(), px); };
         auto ppm = [&](const char* name, const std::vector<float>& px)
         {
            if (!outDir) return;
            FILE* fp = fopen((std::string(outDir) + "/" + name).c_str(), "wb");
            if (!fp) return;
            fprintf(fp, "P6\n%d %d\n255\n", S, S);
            for (int y = S - 1; y >= 0; y--)
               for (int x = 0; x < S; x++)
                  for (int c = 0; c < 3; c++)
                     fputc((int)std::lround(std::clamp(px[((size_t)y * S + x) * 4 + c], 0.0f, 1.0f) * 255.0f), fp);
            fclose(fp);
         };
         auto mae = [](const std::vector<float>& a, const std::vector<float>& b)
         {
            double d = 0.0;
            for (size_t i = 0; i < a.size() && i < b.size(); i++) d += std::fabs(a[i] - b[i]);
            return a.empty() ? 1.0 : d / (double)a.size();
         };
         q.CookIfNeeded(++fnum);
         check(grab(pR), "glTF R picture", 0, 0); ppm("gR.ppm", pR);
         q.liveEngine = true; q.liveEngineFeatures = 7;
         for (int i = 0; i < 3; i++) q.CookIfNeeded(++fnum);
         check(grab(pL), "glTF L picture", 0, 0); ppm("gL.ppm", pL);
         q.liveEngineFeatures = 15; q.ptSamples = 256; q.ptBounces = 4;
         for (int i = 0; i < 40; i++) q.CookIfNeeded(++fnum);
         check(grab(pT), "glTF T picture", 0, 0); ppm("gT.ppm", pT);
         q.liveEngine = false;
         printf("  glTF MAE L-R %.4f T-R %.4f\n", mae(pL, pR), mae(pT, pR));
         check(mae(pL, pR) < 0.08 && mae(pT, pR) < 0.08, "glTF on L and T stays close to R", std::max(mae(pL, pR), mae(pT, pR)), 0.08);
      }
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
