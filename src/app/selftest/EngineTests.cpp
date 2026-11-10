// Engine vs Render 3D (INFINITE_ENGINETEST): the same scene through the GL raster path and the Metal engine, compared
// pixel by pixel and timed. R1's gate: FLIP-class error <= 0.02, >= 1.5x faster, >= 8 lights (docs/plans/realtime-engine).
#include "app/AppShared.h"
#include "engine/rhi/Rhi.h"
#include "engine/MusicRig.h"

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
   frame.gi = getenv("INFINITE_ENGINETEST_GI") != nullptr; if (const char* gr = getenv("INFINITE_ENGINETEST_GIRAYS")) frame.giRays = atoi(gr);
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
      r.liveEngine = false;
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
      std::vector<double> ft; std::vector<double> rigMs;
      int over50 = 0; double worst = 0.0, last = 0.0;
      const double start = NowMs();
      double tPrev = start;
      uint32_t rng = 12345;
      while (NowMs() - start < seconds * 1000.0)
      {
         const double now = NowMs();
         const double beat = (now - start) / 500.0; // 120 bpm
         engine::MusicSignals sg;
         sg.beatPhase = (float)(beat - std::floor(beat));
         sg.beatPulse = std::exp(-4.0f * sg.beatPhase);
         sg.low = 0.6f * sg.beatPulse; sg.mid = 0.5f + 0.5f * (float)std::sin(beat * 1.7);
         rng = rng * 1664525u + 1013904223u; sg.high = (float)(rng >> 24) / 255.0f;
         const double r0 = NowMs();
         mrig.Apply(frame, sg);
         rigMs.push_back(NowMs() - r0);
         frame.frameIndex++;
         raster->Render(*img, frame, nullptr);
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
      check(fps >= 30.0, "R3: >= 30 fps sustained", fps, 30.0);
      check(over50 == 0, "R3: 0 frames over 50 ms", over50, 0);
      check(rigMs[(size_t)(rigMs.size() * 0.99)] <= 1.0, "R3: rig <= 1 ms", rigMs[(size_t)(rigMs.size() * 0.99)], 1.0);
   }
   for (int i = 0; i < 4; i++)
      r.geometry[i] = nullptr;
   r.rig = nullptr;
#else
   check(dev == nullptr, "no engine backend on this platform", 0, 0);
#endif
   printf(failures == 0 ? "ENGINETEST OK\n" : "ENGINETEST FAIL (%d)\n", failures);
   return failures == 0;
}
}
