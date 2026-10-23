// GPU points (docs/plans/gpu-points): the GPU path must draw the picture the
// CPU path draws. Renders one Image to Points -> Transform -> Render 3D chain
// with GPU points off, then on, and compares the two frames.
#include "app/AppShared.h"

#include "GpuPoints.h"

// Geometry3DNodes.cpp: route opaque mesh draws through the no-discard program.
void Render3DSetOpaqueVariantForTest(bool enabled);
int Render3DOpaqueVariantDrawsForTest();
int Render3DGpuInstanceDrawsForTest();

namespace app
{

namespace
{
   std::vector<unsigned char> ReadRgba(unsigned int tex, int w, int h)
   {
      std::vector<unsigned char> px((size_t)w * (size_t)h * 4, 0);
      if (tex == 0 || w <= 0 || h <= 0)
         return px;
      glBindTexture(GL_TEXTURE_2D, tex);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      return px;
   }
}

void FrameTest_GPUPOINTSTEST(int frameId, GLFWwindow* window)
{
   (void)window;
   if (getenv("INFINITE_GPUPOINTSTEST") == nullptr || frameId != 6)
      return;

   bool allOk = true;
   auto report = [&](const char* name, bool ok)
   {
      printf("  [%s] %s\n", ok ? "pass" : "FAIL", name);
      allOk = allOk && ok;
   };

   const bool wasEnabled = GpuPointsGL::Enabled();

   // Off-centre circle: a thresholded shape with a real edge, so a dropped or
   // misplaced cell shows up as changed pixels.
   ShapeNode circle;
   circle.sizeX = 0.25f;
   circle.sizeY = 0.25f;
   circle.posY = 0.7f;
   circle.CookIfNeeded(41000);

   ImageToPointsNode i2p;
   i2p.Input().Connect(&circle);
   i2p.density = 96;
   i2p.threshold = 0.3f;
   i2p.depthScale = 0.4f;
   i2p.sizeFromLuma = 0.5f;
   i2p.tint[0] = 1.0f; i2p.tint[1] = 0.55f; i2p.tint[2] = 0.2f;

   GeometryOpNode xf;
   xf.op = GeometryOpNode::kTransform;
   xf.input = &i2p;
   xf.rotY = 35.0f;
   xf.offsetX = 0.3f;

   // A fresh Render 3D per frame: one node would hand back its cached image
   // whenever the scene signature matches, and the signature does not (and
   // should not) know which path drew the points.
   int frame = 41001;
   int w = 0, h = 0;
   auto renderOnce = [&](bool gpu)
   {
      GpuPointsGL::SetEnabledForTest(gpu);
      Render3DNode render;
      render.geometry[0] = &xf;
      render.CookIfNeeded(frame++);
      w = render.GetOutputWidth();
      h = render.GetOutputHeight();
      return ReadRgba(render.GetOutputTexture(), w, h);
   };

   const std::vector<unsigned char> cpuFrame = renderOnce(false);
   const bool cpuHadNoGpu = i2p.GetGpuPoints() == nullptr && xf.GetGpuPoints() == nullptr;
   const size_t cpuCount = i2p.GetPoints().size();

   const std::vector<unsigned char> gpuFrame = renderOnce(true);
   const bool gpuOffered = i2p.GetGpuPoints() != nullptr && xf.GetGpuPoints() != nullptr;

   // The occlusion count lands a cook later; a still image must report the
   // same number of kept points as the CPU build did.
   for (int i = 0; i < 4; i++)
      renderOnce(true);
   const size_t gpuCount = i2p.PointCount();

   // The same scene with no points, to prove the points are what is drawn.
   i2p.bypassed = true;
   const std::vector<unsigned char> emptyFrame = renderOnce(true);
   i2p.bypassed = false;

   size_t drawn = 0, differ = 0;
   int maxDiff = 0;
   for (size_t i = 0; i + 3 < cpuFrame.size() && i + 3 < gpuFrame.size(); i += 4)
   {
      int d = 0;
      for (int c = 0; c < 3; c++)
         d = std::max(d, std::abs((int)cpuFrame[i + c] - (int)gpuFrame[i + c]));
      if (i + 3 < emptyFrame.size())
      {
         int e = 0;
         for (int c = 0; c < 3; c++)
            e = std::max(e, std::abs((int)cpuFrame[i + c] - (int)emptyFrame[i + c]));
         if (e > 8)
            drawn++;
      }
      if (d > 2)
         differ++;
      maxDiff = std::max(maxDiff, d);
   }
   const size_t total = (size_t)std::max(1, w * h);
   // Float rounding on a sprite edge can flip a sample; anything beyond a
   // hairline (0.05% of the frame) is a real mismatch.
   const bool framesMatch = differ * 2000 <= total;

   printf("gpu points: %dx%d, cpu points %zu, gpu count %zu, drawn %zu, differ %zu (max %d)\n",
          w, h, cpuCount, gpuCount, drawn, differ, maxDiff);
   report("INFINITE_GPU_POINTS off offers no GPU points", cpuHadNoGpu);
   report("Image to Points and Transform offer GPU points", gpuOffered);
   report("the points are drawn (frame differs from no points)", drawn > total / 200);
   report("GPU frame matches the CPU frame", framesMatch);
   report("occlusion count matches the CPU cloud", gpuCount == cpuCount);

   // Lazy CPU cloud: still there, still the same points, for CPU consumers.
   const std::vector<Particle>* lazy = xf.GetPointCloud();
   report("CPU cloud still built on demand", lazy != nullptr && lazy->size() == cpuCount);

   GpuPointsGL::SetEnabledForTest(wasEnabled);

   // Step 5: opaque mesh draws use a program with discard compiled out. It
   // must draw the same pixels as the full one: overlapping instanced cubes
   // (opaque variant), a blended sphere (full program, switched to and back
   // in the same frame) and a cutout sphere (must still discard).
   {
      GeometryNode points;
      points.shape = 2; // sphere
      GeometryNode cube;
      cube.shape = 1;
      InstanceOnPointsNode inst;
      inst.pointSource = &points;
      inst.instanceShape = &cube;
      inst.pointMode = 0; // vertices
      inst.maxPoints = 400;
      inst.instanceScale = 0.3f;

      GeometryNode glass;
      glass.shape = 2;
      glass.posX = 0.6f;
      glass.opacity = 0.5f;
      GeometryNode cutout;
      cutout.shape = 2;
      cutout.posX = -0.6f;
      cutout.opacity = 0.8f;
      cutout.alphaCutoff = 0.5f;

      auto renderMesh = [&](bool variant)
      {
         Render3DSetOpaqueVariantForTest(variant);
         Render3DNode meshRender; // fresh, for the same reason as renderOnce
         meshRender.geometry[0] = &inst;
         meshRender.geometry[1] = &glass;
         meshRender.geometry[2] = &cutout;
         meshRender.CookIfNeeded(frame++);
         return ReadRgba(meshRender.GetOutputTexture(), meshRender.GetOutputWidth(), meshRender.GetOutputHeight());
      };
      const std::vector<unsigned char> fullFrame = renderMesh(false);
      const int variantDrawsBefore = Render3DOpaqueVariantDrawsForTest();
      const std::vector<unsigned char> variantFrame = renderMesh(true);
      const int variantDraws = Render3DOpaqueVariantDrawsForTest() - variantDrawsBefore;
      Render3DSetOpaqueVariantForTest(true);

      size_t meshDiffer = 0, lit = 0;
      for (size_t i = 0; i + 3 < fullFrame.size() && i + 3 < variantFrame.size(); i += 4)
      {
         if (fullFrame[i] != variantFrame[i] || fullFrame[i + 1] != variantFrame[i + 1] ||
             fullFrame[i + 2] != variantFrame[i + 2] || fullFrame[i + 3] != variantFrame[i + 3])
            meshDiffer++;
         if (fullFrame[i] != fullFrame[0] || fullFrame[i + 1] != fullFrame[1] || fullFrame[i + 2] != fullFrame[2])
            lit++;
      }
      printf("opaque variant: %zu instances, %d variant draws, %zu pixels differ from background, %zu differ between programs\n",
             inst.InstanceCount(), variantDraws, lit, meshDiffer);
      report("mesh scene draws something", inst.InstanceCount() > 0 && lit > fullFrame.size() / 4 / 100);
      // Instances take it; the blended and cutout spheres must not.
      report("the opaque variant ran for the instances only", variantDraws == 1);
      report("opaque variant draws the same pixels as the full program", !fullFrame.empty() && meshDiffer == 0);
   }

   // Step 6: Instance on Points over GPU points builds each copy in the
   // vertex shader. Same chain as above into cubes, wrapped in a Transform
   // (the group matrix), shadows on (the shadow pass builds them too); the
   // frame must match the CPU matrices' frame, and the CPU list must still
   // come out right when something asks for it.
   {
      GeometryNode cube;
      cube.shape = 1;
      cube.rotY = 20.0f; // the stamp's own model matrix
      InstanceOnPointsNode inst;
      inst.cloudSource = &xf;
      inst.instanceShape = &cube;
      inst.instanceScale = 0.6f;
      GeometryOpNode group;
      group.op = GeometryOpNode::kTransform;
      group.input = &inst;
      group.rotX = 25.0f;
      group.offsetY = -0.2f;

      auto renderInst = [&](bool gpu)
      {
         GpuPointsGL::SetEnabledForTest(gpu);
         Render3DNode instRender; // fresh, for the same reason as renderOnce
         instRender.geometry[0] = &group;
         instRender.shadowsEnabled = true;
         instRender.CookIfNeeded(frame++);
         return ReadRgba(instRender.GetOutputTexture(), instRender.GetOutputWidth(), instRender.GetOutputHeight());
      };
      const std::vector<unsigned char> cpuInst = renderInst(false);
      const size_t cpuInstances = inst.InstanceCount();
      const int gpuDrawsBefore = Render3DGpuInstanceDrawsForTest();
      std::vector<unsigned char> gpuInst = renderInst(true);
      for (int i = 0; i < 3; i++) // the occlusion count lands a cook later
         gpuInst = renderInst(true);
      const int gpuDraws = Render3DGpuInstanceDrawsForTest() - gpuDrawsBefore;
      const bool offered = inst.GpuInstancePoints() != nullptr;
      const size_t gpuInstances = inst.InstanceCount(); // the GPU count, nothing built
      const size_t lazyInstances = inst.InstanceTransforms().size();

      size_t instDiffer = 0, instLit = 0;
      int instMax = 0;
      for (size_t i = 0; i + 3 < cpuInst.size() && i + 3 < gpuInst.size(); i += 4)
      {
         int d = 0;
         for (int c = 0; c < 3; c++)
            d = std::max(d, std::abs((int)cpuInst[i + c] - (int)gpuInst[i + c]));
         if (d > 2)
            instDiffer++;
         instMax = std::max(instMax, d);
         if (cpuInst[i] != cpuInst[0] || cpuInst[i + 1] != cpuInst[1] || cpuInst[i + 2] != cpuInst[2])
            instLit++;
      }
      const size_t instTotal = std::max<size_t>(1, cpuInst.size() / 4);
      printf("gpu instances: cpu %zu, gpu count %zu, lazy list %zu, %d gpu draws, lit %zu, differ %zu (max %d)\n",
             cpuInstances, gpuInstances, lazyInstances, gpuDraws, instLit, instDiffer, instMax);
      report("instances drawn", cpuInstances > 0 && instLit > instTotal / 100);
      report("Instance on Points offers its cloud's GPU points", offered);
      report("Render 3D built the copies on the GPU", gpuDraws > 0);
      report("GPU instance frame matches the CPU frame", instDiffer * 2000 <= instTotal);
      report("instance count without the CPU list matches", gpuInstances == cpuInstances);
      report("CPU list still built on demand", lazyInstances == cpuInstances);
      GpuPointsGL::SetEnabledForTest(wasEnabled);
   }

   // Step 7: Particle System on the GPU. The two paths draw their random
   // numbers differently, so the runs are compared by what must hold for any
   // seed: the same alive count when nothing is random about a life, the same
   // flight when nothing is random about a launch, a rerun that repeats
   // exactly, bounds that hold every particle, births on the emit pin's
   // geometry, and a cap past the CPU's 200k.
   {
      Transport& tr = Transport::Instance();
      const bool wasPlaying = tr.IsPlaying();
      const float wasTempo = tr.Tempo();
      tr.SetPlaying(false);
      tr.SetTempo(120.0f); // 1/30 beat is 1/60 s

      struct Run
      {
         size_t alive = 0;       // AliveCount() once the count has landed
         size_t readAlive = 0;   // alive in the CPU readback
         bool offered = false;
         bool finite = true;
         bool inBounds = true;
         double meanY = 0.0;
         double checksum = 0.0;
         float lo[3] = { 1e30f, 1e30f, 1e30f };
         float hi[3] = { -1e30f, -1e30f, -1e30f };
         float maxColorErr = 0.0f;
      };
      // Steps `frames` 60 Hz frames from a rewind, then lets the count land.
      auto simulate = [&](bool gpu, ParticleSystemNode& p, int frames, const float* wantColor)
      {
         GpuPointsGL::SetEnabledForTest(gpu);
         double beat = 0.0;
         tr.SeekBeats(beat);
         p.CookIfNeeded(frame++);
         for (int i = 0; i < frames; i++)
         {
            beat += 1.0 / 30.0;
            tr.SeekBeats(beat);
            p.CookIfNeeded(frame++);
         }
         // Same beat, no step: the first reads the count in flight and
         // recounts if it was taken before the last step, the second reads that.
         for (int settle = 0; settle < 2; settle++)
         {
            glFinish();
            p.CookIfNeeded(frame++);
         }
         Run r;
         r.alive = p.AliveCount();
         const GpuPoints* gp = p.GetGpuPoints();
         r.offered = gp != nullptr;
         for (const Particle& q : p.GetPoints())
         {
            if (!q.alive)
               continue;
            r.readAlive++;
            const float v[3] = { q.px, q.py, q.pz };
            for (int k = 0; k < 3; k++)
            {
               if (!std::isfinite(v[k]))
                  r.finite = false;
               r.lo[k] = std::min(r.lo[k], v[k]);
               r.hi[k] = std::max(r.hi[k], v[k]);
               if (gp != nullptr && gp->hasBounds && (v[k] < gp->lo[k] - 1e-4f || v[k] > gp->hi[k] + 1e-4f))
                  r.inBounds = false;
            }
            if (gp != nullptr && !gp->hasBounds)
               r.inBounds = false;
            r.meanY += q.py;
            r.checksum += (double)q.px + 2.0 * q.py + 3.0 * q.pz;
            if (wantColor != nullptr)
               r.maxColorErr = std::max({ r.maxColorErr, std::fabs(q.r - wantColor[0]),
                                          std::fabs(q.g - wantColor[1]), std::fabs(q.b - wantColor[2]) });
         }
         if (r.readAlive > 0)
            r.meanY /= (double)r.readAlive;
         return r;
      };
      auto beam = [](ParticleSystemNode& p)
      {
         p.maxParticles = 20000;
         p.emitRate = 3000.0f;
         p.emitRadius = 0.0f;
         p.lifetime = 1.0f;
         p.lifetimeRandom = 0.0f;
         p.speedRandom = 0.0f;
         p.spread = 0.0f;
      };

      // Nothing random: the same particles on both paths, so the same count
      // and the same flight.
      Run cpuBeam, gpuBeam;
      {
         ParticleSystemNode p;
         beam(p);
         cpuBeam = simulate(false, p, 150, nullptr);
      }
      {
         ParticleSystemNode p;
         beam(p);
         gpuBeam = simulate(true, p, 150, nullptr);
      }
      printf("gpu particles, beam: cpu %zu alive (mean y %.4f), gpu %zu alive / %zu read back (mean y %.4f)\n",
             cpuBeam.alive, cpuBeam.meanY, gpuBeam.alive, gpuBeam.readAlive, gpuBeam.meanY);
      report("GPU particles: off offers no GPU points, on offers them", !cpuBeam.offered && gpuBeam.offered);
      report("GPU particles: alive count matches the CPU run",
             cpuBeam.alive > 1000 && std::llabs((long long)gpuBeam.alive - (long long)cpuBeam.alive) * 100 <= (long long)cpuBeam.alive);
      report("GPU particles: readback agrees with the count", gpuBeam.readAlive == gpuBeam.alive);
      report("GPU particles: same flight as the CPU run", std::fabs(gpuBeam.meanY - cpuBeam.meanY) < 2e-3);

      // Random launches: statistically alike, and a rerun repeats exactly.
      auto spray = [](ParticleSystemNode& p)
      {
         p.maxParticles = 50000;
         p.emitRate = 8000.0f;
         p.turbulence = 2.0f;
         p.seed = 7.0f;
      };
      Run cpuSpray, gpuSpray, gpuSpray2;
      {
         ParticleSystemNode p;
         spray(p);
         cpuSpray = simulate(false, p, 120, nullptr);
      }
      {
         ParticleSystemNode p;
         spray(p);
         gpuSpray = simulate(true, p, 120, nullptr);
         gpuSpray2 = simulate(true, p, 120, nullptr); // the same node, rewound
      }
      const double sprayRange = std::max(1e-3, (double)(cpuSpray.hi[1] - cpuSpray.lo[1]));
      printf("gpu particles, spray: cpu %zu alive (mean y %.4f, y %.3f..%.3f), gpu %zu (mean y %.4f), rerun checksum %.6f vs %.6f\n",
             cpuSpray.alive, cpuSpray.meanY, cpuSpray.lo[1], cpuSpray.hi[1], gpuSpray.alive, gpuSpray.meanY,
             gpuSpray.checksum, gpuSpray2.checksum);
      report("GPU particles: random run alike in count and spread",
             std::llabs((long long)gpuSpray.alive - (long long)cpuSpray.alive) * 20 <= (long long)cpuSpray.alive &&
             std::fabs(gpuSpray.meanY - cpuSpray.meanY) < 0.1 * sprayRange);
      report("GPU particles: a rewind replays the same run", gpuSpray.alive > 0 && gpuSpray.checksum == gpuSpray2.checksum);
      report("GPU particles: all finite, all inside the offered bounds", gpuSpray.finite && gpuSpray.inBounds);

      // Born on a cube's surface, in its colour, on both paths.
      {
         GeometryNode cube;
         cube.shape = 1;
         cube.posX = 0.7f;
         cube.color[0] = 0.2f; cube.color[1] = 0.8f; cube.color[2] = 0.4f;
         cube.CookIfNeeded(frame++);
         const Mat4 m = cube.GetModelMatrix();
         float clo[3] = { 1e30f, 1e30f, 1e30f }, chi[3] = { -1e30f, -1e30f, -1e30f };
         for (const Vertex& v : cube.GetMesh().vertices)
            for (int k = 0; k < 3; k++)
            {
               const float w = m.m[k] * v.px + m.m[4 + k] * v.py + m.m[8 + k] * v.pz + m.m[12 + k];
               clo[k] = std::min(clo[k], w);
               chi[k] = std::max(chi[k], w);
            }
         const Material mat = cube.GetMaterial();
         auto onCube = [&](ParticleSystemNode& p)
         {
            p.emitSource = &cube;
            p.emitFromInput = true;
            p.initialSpeed = 0.0f;
            p.gravityY = 0.0f;
            p.lifetime = 5.0f;
         };
         Run cpuCube, gpuCube;
         {
            ParticleSystemNode p;
            onCube(p);
            cpuCube = simulate(false, p, 30, mat.color);
         }
         {
            ParticleSystemNode p;
            onCube(p);
            gpuCube = simulate(true, p, 30, mat.color);
         }
         auto inside = [&](const Run& r)
         {
            for (int k = 0; k < 3; k++)
               if (r.lo[k] < clo[k] - 1e-3f || r.hi[k] > chi[k] + 1e-3f)
                  return false;
            return r.readAlive > 0;
         };
         printf("gpu particles, emit on cube: cpu %zu alive (colour err %.4f), gpu %zu (colour err %.4f)\n",
                cpuCube.readAlive, cpuCube.maxColorErr, gpuCube.readAlive, gpuCube.maxColorErr);
         report("emit from input: born on the cube, both paths", inside(cpuCube) && inside(gpuCube));
         report("emit from input: source colour copied, both paths", cpuCube.maxColorErr < 1e-3f && gpuCube.maxColorErr < 2e-3f);
      }

      // Born on GPU points (the Image to Points chain above): sampled in the
      // shader, never read back.
      {
         GpuPointsGL::SetEnabledForTest(true);
         i2p.CookIfNeeded(frame++);
         xf.CookIfNeeded(frame++);
         const std::vector<Particle>* cloud = xf.GetPointCloud();
         const Mat4 m = xf.GetModelMatrix();
         float clo[3] = { 1e30f, 1e30f, 1e30f }, chi[3] = { -1e30f, -1e30f, -1e30f };
         if (cloud != nullptr)
            for (const Particle& q : *cloud)
               for (int k = 0; k < 3; k++)
               {
                  const float w = m.m[k] * q.px + m.m[4 + k] * q.py + m.m[8 + k] * q.pz + m.m[12 + k];
                  clo[k] = std::min(clo[k], w);
                  chi[k] = std::max(chi[k], w);
               }
         ParticleSystemNode p;
         p.emitSource = &xf;
         p.emitFromInput = true;
         p.initialSpeed = 0.0f;
         p.gravityY = 0.0f;
         const Run r = simulate(true, p, 30, nullptr);
         bool inside = r.readAlive > 0;
         for (int k = 0; k < 3; k++)
            if (r.lo[k] < clo[k] - 1e-3f || r.hi[k] > chi[k] + 1e-3f)
               inside = false;
         printf("gpu particles, emit on GPU points: %zu alive\n", r.readAlive);
         report("emit from input: born on GPU points", xf.GetGpuPoints() != nullptr && inside);
      }

      // Past the CPU cap.
      {
         ParticleSystemNode p;
         p.maxParticles = ParticleSystemNode::kGpuMaxParticles;
         p.emitRate = 600000.0f;
         p.lifetime = 3.0f;
         const Run r = simulate(true, p, 60, nullptr);
         printf("gpu particles, 1M cap: %zu alive after 1 s at 600k/s\n", r.alive);
         report("GPU particles: past the CPU's 200k", r.alive > 400000 && r.finite);
      }

      GpuPointsGL::SetEnabledForTest(wasEnabled);
      tr.SetTempo(wasTempo);
      tr.SetPlaying(wasPlaying);
   }

   printf("%s\n", allOk ? "GPU POINTS TEST OK" : "GPU POINTS TEST FAIL");
}

}
