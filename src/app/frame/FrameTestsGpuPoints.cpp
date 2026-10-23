// GPU points (docs/plans/gpu-points): the GPU path must draw the picture the
// CPU path draws. Renders one Image to Points -> Transform -> Render 3D chain
// with GPU points off, then on, and compares the two frames.
#include "app/AppShared.h"

#include "GpuPoints.h"
#include "PlyPoints.h"

#include <filesystem>
#include <fstream>

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

   // The frame compare every step uses: a pixel differs when a channel moves
   // by more than 2, and the frames match when at most 0.05% of them differ
   // (float rounding on a sprite edge can flip a sample). `drawn` counts the
   // pixels the CPU frame changed against a frame with nothing in it.
   struct FrameDiff
   {
      size_t differ = 0, drawn = 0, total = 1;
      int maxDiff = 0;
      bool Match() const { return differ * 2000 <= total; }
      bool Drawn() const { return drawn > total / 200; }
   };

   FrameDiff CompareFrames(const std::vector<unsigned char>& cpu, const std::vector<unsigned char>& gpu,
                           const std::vector<unsigned char>& empty)
   {
      FrameDiff r;
      r.total = std::max<size_t>(1, cpu.size() / 4);
      if (cpu.size() != gpu.size())
      {
         r.differ = r.total;
         return r;
      }
      for (size_t i = 0; i + 3 < cpu.size(); i += 4)
      {
         int d = 0, e = 0;
         for (int c = 0; c < 3; c++)
         {
            d = std::max(d, std::abs((int)cpu[i + c] - (int)gpu[i + c]));
            if (i + 3 < empty.size())
               e = std::max(e, std::abs((int)cpu[i + c] - (int)empty[i + c]));
         }
         if (d > 2)
            r.differ++;
         if (e > 8)
            r.drawn++;
         r.maxDiff = std::max(r.maxDiff, d);
      }
      return r;
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

   // Step 8: Mesh to Points uploads its samples as GPU points once per
   // rebuild. A sphere's vertices through a Transform, CPU vs GPU.
   {
      GeometryNode sphere;
      sphere.shape = 2;
      MeshToPointsNode m2p;
      m2p.input = &sphere;
      m2p.mode = 0; // vertices
      m2p.pointSize = 0.06f;
      GeometryOpNode m2pXf;
      m2pXf.op = GeometryOpNode::kTransform;
      m2pXf.input = &m2p;
      m2pXf.rotX = 30.0f;
      m2pXf.offsetY = 0.1f;

      auto renderM2p = [&](bool gpu)
      {
         GpuPointsGL::SetEnabledForTest(gpu);
         Render3DNode r; // fresh, for the same reason as renderOnce
         r.geometry[0] = &m2pXf;
         r.CookIfNeeded(frame++);
         return ReadRgba(r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight());
      };
      const std::vector<unsigned char> cpuM2p = renderM2p(false);
      const bool cpuNone = m2p.GetGpuPoints() == nullptr;
      const std::vector<unsigned char> gpuM2p = renderM2p(true);
      const GpuPoints* gp = m2p.GetGpuPoints();
      const bool offered = gp != nullptr && m2pXf.GetGpuPoints() != nullptr;
      const size_t cpuN = m2p.GetPoints().size();
      const bool sameCount = gp != nullptr && (size_t)gp->count == cpuN && (size_t)gp->liveCount == cpuN;
      const bool sameRevision = gp != nullptr && gp->revision == m2p.PointCloudRevision();
      // A second ask with nothing changed re-uploads nothing.
      const unsigned long long rev0 = gp ? gp->revision : 0;
      const GpuPoints* again = m2p.GetGpuPoints();
      const bool stable = again != nullptr && again->revision == rev0;
      m2p.bypassed = true;
      const std::vector<unsigned char> emptyM2p = renderM2p(true);
      m2p.bypassed = false;

      const FrameDiff d = CompareFrames(cpuM2p, gpuM2p, emptyM2p);
      printf("mesh to points: %zu points, drawn %zu, differ %zu (max %d)\n", cpuN, d.drawn, d.differ, d.maxDiff);
      report("Mesh to Points: no GPU points when the path is off", cpuNone);
      report("Mesh to Points and its Transform offer GPU points", offered);
      report("Mesh to Points: GPU count and revision match the CPU cloud", sameCount && sameRevision && stable);
      report("Mesh to Points: the points are drawn", d.Drawn());
      report("Mesh to Points: GPU frame matches the CPU frame", d.Match());
      GpuPointsGL::SetEnabledForTest(wasEnabled);
   }

   // Step 9: Twist and Displacement act on points, on both paths. Each runs
   // over Image to Points behind the Transform (so the pass bakes an
   // upstream pointMatrix) and must draw the CPU path's frame; Twist also
   // runs over Mesh to Points (uploaded points), and the displaced CPU
   // cloud, read back on demand after a GPU frame, must hold the CPU run's
   // positions.
   {
      GeometryNode ball;
      ball.shape = 2;
      MeshToPointsNode ballPts;
      ballPts.input = &ball;
      ballPts.pointSize = 0.05f;

      GeometryOpNode twist;
      twist.op = GeometryOpNode::kTwist;
      twist.input = &xf;
      twist.amount = 25.0f;
      twist.axis = 1;
      GeometryOpNode twistM2p;
      twistM2p.op = GeometryOpNode::kTwist;
      twistM2p.input = &ballPts;
      twistM2p.amount = 40.0f;
      twistM2p.axis = 0;

      DisplacementNode dispScalar;
      dispScalar.input = &xf;
      dispScalar.TextureInput().Connect(&circle);
      dispScalar.strength = 0.6f;
      DisplacementNode dispVector;
      dispVector.input = &i2p;
      dispVector.TextureInput().Connect(&circle);
      dispVector.mode = DisplacementNode::kVector;
      dispVector.strength = 0.3f;

      struct Case { const char* name; IGeometrySource* node; INode* producer; };
      const Case cases[] = {
         { "Twist over Image to Points", &twist, &i2p },
         { "Twist over Mesh to Points", &twistM2p, &ballPts },
         { "Displacement (scalar) over Image to Points", &dispScalar, &i2p },
         { "Displacement (vector) over Image to Points", &dispVector, &i2p },
      };
      for (const Case& c : cases)
      {
         auto renderCase = [&](bool gpu)
         {
            GpuPointsGL::SetEnabledForTest(gpu);
            Render3DNode r; // fresh, for the same reason as renderOnce
            r.geometry[0] = c.node;
            r.CookIfNeeded(frame++);
            return ReadRgba(r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight());
         };
         const std::vector<unsigned char> cpuF = renderCase(false);
         const bool cpuNone = c.node->GetGpuPoints() == nullptr;
         std::vector<Particle> cpuCloud;
         if (const std::vector<Particle>* cc = c.node->GetPointCloud())
            cpuCloud = *cc;
         const std::vector<unsigned char> gpuF = renderCase(true);
         const bool offered = c.node->GetGpuPoints() != nullptr;
         c.producer->bypassed = true;
         const std::vector<unsigned char> emptyF = renderCase(true);
         c.producer->bypassed = false;
         renderCase(true); // the GPU path again, so the cloud below is asked for after a GPU frame

         // The same points as the CPU run, through the lazy CPU path.
         const std::vector<Particle>* lazy = c.node->GetPointCloud();
         float maxErr = 0.0f;
         bool sameSize = lazy != nullptr && lazy->size() == cpuCloud.size() && !cpuCloud.empty();
         if (sameSize)
            for (size_t i = 0; i < cpuCloud.size(); i++)
            {
               maxErr = std::max(maxErr, std::fabs((*lazy)[i].px - cpuCloud[i].px));
               maxErr = std::max(maxErr, std::fabs((*lazy)[i].py - cpuCloud[i].py));
               maxErr = std::max(maxErr, std::fabs((*lazy)[i].pz - cpuCloud[i].pz));
            }
         // The points must have moved: the op is not a pass-through.
         const std::vector<Particle>* before = (c.node == &twistM2p) ? &ballPts.GetPoints()
                                               : (c.node == &dispVector) ? &i2p.GetPoints()
                                               : xf.GetPointCloud();
         float moved = 0.0f;
         if (before != nullptr && before->size() == cpuCloud.size())
            for (size_t i = 0; i < cpuCloud.size(); i++)
               moved = std::max(moved, std::fabs((*before)[i].px - cpuCloud[i].px) +
                                       std::fabs((*before)[i].py - cpuCloud[i].py) +
                                       std::fabs((*before)[i].pz - cpuCloud[i].pz));

         const FrameDiff d = CompareFrames(cpuF, gpuF, emptyF);
         printf("%s: %zu points, moved up to %.3f, lazy cloud err %.6f, drawn %zu, differ %zu (max %d)\n",
                c.name, cpuCloud.size(), moved, maxErr, d.drawn, d.differ, d.maxDiff);
         char label[160];
         snprintf(label, sizeof(label), "%s: CPU path offers no GPU points, GPU path does", c.name);
         report(label, cpuNone && offered);
         snprintf(label, sizeof(label), "%s: moves the points", c.name);
         report(label, moved > 0.01f);
         snprintf(label, sizeof(label), "%s: drawn, GPU frame matches the CPU frame", c.name);
         report(label, d.Drawn() && d.Match());
         snprintf(label, sizeof(label), "%s: CPU cloud after a GPU frame matches the CPU run", c.name);
         report(label, sameSize && maxErr < 1e-4f);
      }
      GpuPointsGL::SetEnabledForTest(wasEnabled);
   }

   // Step 10: Field Modifier compiled to GLSL. Each kernel runs over Mesh to
   // Points (a CPU-made cloud, every point alive) on both paths, and the GPU
   // texels, read back, must hold the CPU cloud's position, sprite size and
   // colour point for point. One kernel also renders behind the Transform
   // (an upstream pointMatrix) and must draw the CPU frame. A kernel the
   // backend cannot copy must offer no GPU points and still act on the CPU
   // cloud.
   {
      GeometryNode ball;
      ball.shape = 2;
      MeshToPointsNode ballPts;
      ballPts.input = &ball;
      ballPts.pointSize = 0.05f;

      struct Kernel { const char* name; const char* code; bool gpu; };
      const Kernel kernels[] = {
         { "wave over t", "P.y += sin(P.x * 4.0 + t * 3.0) * 0.2\n", true },
         { "colour from index and P", "Cd = vec3(fract(i * 0.37), 0.5, clamp(1.0 - P.y, 0.0, 1.0))\n", true },
         { "random size", "attrib float size = 1\nsize = rand(0.25, 2.0, 1.0, i)\n", true },
         { "params, prologue, if/else",
           "param float amp = 0.3 [0.0, 1.0]\nk = amp * 2.0\nd = length(P) * 0.0 + P.y\n"
           "if (d > 0.13) { P *= 1.0 + k } else { P.x -= k }\n", true },
         { "for loop, swizzle, smoothstep",
           "s = 0.0\nfor (j = 0; j < 4; j += 1) { s += P.x * 0.1 }\nP.z += s\n"
           "Cd.xy = vec2(smoothstep(-1.0, 1.0, P.y), step(0.0, P.x))\n", true },
         { "divide by zero, pow, mod, sh",
           "z = P.y - P.y\nP.x = P.x / z + pow(abs(P.y), 1.5)\nP /= z\n"
           "Cd = vec3(mod(i, 7.0) / 7.0, sh(0.0, 1.0, 4.0, i), noise(2.0))\n", true },
         { "normal read (CPU only)", "P += N * 0.2\n", false },
      };

      auto readTex = [](unsigned int tex, int w, int h)
      {
         std::vector<float> px((size_t)w * (size_t)h * 4, 0.0f);
         glBindTexture(GL_TEXTURE_2D, tex);
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         return px;
      };

      for (const Kernel& k : kernels)
      {
         FieldElementNode fe;
         fe.input = &ballPts;
         fe.code = k.code;
         const bool compiled = fe.Apply();
         GpuPointsGL::SetEnabledForTest(false);
         ball.CookIfNeeded(frame);
         ballPts.CookIfNeeded(frame);
         fe.CookIfNeeded(frame++);
         const bool cpuNone = fe.GetGpuPoints() == nullptr;
         std::vector<Particle> cpuCloud;
         if (const std::vector<Particle>* cc = fe.GetPointCloud())
            cpuCloud = *cc;

         GpuPointsGL::SetEnabledForTest(true);
         fe.CookIfNeeded(frame - 1 + 1000000); // a new cook at the same time
         const GpuPoints* g = fe.GetGpuPoints();
         const std::vector<Particle>& before = ballPts.GetPoints();
         float moved = 0.0f;
         if (before.size() == cpuCloud.size())
            for (size_t i = 0; i < cpuCloud.size(); i++)
               moved = std::max(moved, std::fabs(before[i].px - cpuCloud[i].px) + std::fabs(before[i].py - cpuCloud[i].py) +
                                       std::fabs(before[i].pz - cpuCloud[i].pz) + std::fabs(before[i].r - cpuCloud[i].r) +
                                       std::fabs(before[i].g - cpuCloud[i].g) + std::fabs(before[i].b - cpuCloud[i].b) +
                                       std::fabs(before[i].scale - cpuCloud[i].scale));
         char label[200];
         if (!k.gpu)
         {
            printf("Field Modifier, %s: fallback \"%s\", moved %.3f\n", k.name, fe.PointGpuFallback().c_str(), moved);
            snprintf(label, sizeof(label), "Field Modifier, %s: no GPU points, CPU cloud still modified", k.name);
            report(label, compiled && g == nullptr && !fe.PointGpuFallback().empty() && moved > 0.01f);
            continue;
         }

         float errP = 0.0f, errW = 0.0f, errC = 0.0f;
         bool same = g != nullptr && !cpuCloud.empty() && g->count == (int)cpuCloud.size();
         if (same)
         {
            const std::vector<float> pos = readTex(g->posTex, g->texW, g->texH);
            const std::vector<float> col = readTex(g->colorTex, g->texW, g->texH);
            const float base = fe.PointBaseSize();
            for (size_t i = 0; i < cpuCloud.size(); i++)
            {
               const Particle& p = cpuCloud[i];
               const float* q = &pos[i * 4];
               const float* c = &col[i * 4];
               errP = std::max(errP, std::fabs(q[0] - p.px) + std::fabs(q[1] - p.py) + std::fabs(q[2] - p.pz));
               errW = std::max(errW, std::fabs(q[3] - (p.alive ? p.scale * base : 0.0f)));
               errC = std::max(errC, std::fabs(c[0] - p.r) + std::fabs(c[1] - p.g) + std::fabs(c[2] - p.b));
            }
         }
         printf("Field Modifier, %s: %zu points, moved %.3f, err pos %.6f size %.6f colour %.6f%s%s\n", k.name,
                cpuCloud.size(), moved, errP, errW, errC, fe.PointGpuFallback().empty() ? "" : ", fallback: ",
                fe.PointGpuFallback().c_str());
         snprintf(label, sizeof(label), "Field Modifier, %s: CPU path offers no GPU points, GPU path does", k.name);
         report(label, compiled && cpuNone && g != nullptr);
         snprintf(label, sizeof(label), "Field Modifier, %s: changes the points", k.name);
         report(label, moved > 0.01f);
         snprintf(label, sizeof(label), "Field Modifier, %s: GPU texels match the CPU cloud", k.name);
         report(label, same && errP < 1e-3f && errW < 1e-4f && errC < 1e-3f);
      }

      // The wave kernel behind the Transform, drawn.
      FieldElementNode wave;
      wave.input = &xf;
      wave.code = kernels[0].code;
      wave.Apply();
      auto renderWave = [&](bool gpu)
      {
         GpuPointsGL::SetEnabledForTest(gpu);
         Render3DNode r; // fresh, for the same reason as renderOnce
         r.geometry[0] = &wave;
         r.CookIfNeeded(frame++);
         return ReadRgba(r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight());
      };
      const std::vector<unsigned char> cpuF = renderWave(false);
      const std::vector<unsigned char> gpuF = renderWave(true);
      const bool offered = wave.GetGpuPoints() != nullptr;
      i2p.bypassed = true;
      const std::vector<unsigned char> emptyF = renderWave(true);
      i2p.bypassed = false;
      const FrameDiff d = CompareFrames(cpuF, gpuF, emptyF);
      printf("Field Modifier over Image to Points + Transform: drawn %zu, differ %zu (max %d)\n", d.drawn, d.differ,
             d.maxDiff);
      report("Field Modifier over Image to Points + Transform: GPU points, frame matches the CPU frame",
             offered && d.Drawn() && d.Match());
      GpuPointsGL::SetEnabledForTest(wasEnabled);
   }

   // Step 11: Model 3D reads a face-less .ply as points. The same 300 points
   // are written as ascii (uchar colour), binary little-endian (float colour,
   // an extra double to skip) and binary big-endian (ushort colour); all three
   // must parse to the same points. A splat export takes its colour from
   // f_dc, and a .ply with faces still loads as a mesh. Model 3D's points are
   // fitted like a mesh, uploaded as GPU points equal to the cloud, and drawn
   // behind a Transform the same on both paths.
   {
      const int n = 300;
      std::vector<float> xyz, rgb;
      for (int i = 0; i < n; i++)
      {
         const float a = (float)i * 0.21f;
         xyz.push_back(std::cos(a) * (2.0f + (float)i * 0.01f));
         xyz.push_back((float)i * 0.02f - 3.0f);
         xyz.push_back(std::sin(a) * 2.0f);
         // Multiples of 1/255, so uchar, ushort (x 257) and float hold them exactly.
         rgb.push_back((float)((i * 7) % 256) / 255.0f);
         rgb.push_back((float)((i * 13 + 40) % 256) / 255.0f);
         rgb.push_back((float)(255 - i % 256) / 255.0f);
      }
      auto put = [](std::string& out, const void* v, int width, bool big)
      {
         const unsigned char* b = (const unsigned char*)v;
         for (int k = 0; k < width; k++)
            out.push_back((char)b[big ? width - 1 - k : k]); // the test host is little-endian
      };

      std::string ascii = "ply\nformat ascii 1.0\ncomment made by GPUPOINTSTEST\nelement vertex 300\n"
                          "property float x\nproperty float y\nproperty float z\nproperty float nx\n"
                          "property float ny\nproperty float nz\nproperty uchar red\nproperty uchar green\n"
                          "property uchar blue\nend_header\n";
      std::string little = "ply\r\nformat binary_little_endian 1.0\r\nelement vertex 300\r\nproperty float x\r\n"
                           "property float y\r\nproperty float z\r\nproperty double intensity\r\nproperty float red\r\n"
                           "property float green\r\nproperty float blue\r\nend_header\r\n";
      std::string big = "ply\nformat binary_big_endian 1.0\nelement vertex 300\nproperty float x\nproperty float y\n"
                        "property float z\nproperty ushort red\nproperty ushort green\nproperty ushort blue\n"
                        "element face 0\nproperty list uchar int vertex_indices\nend_header\n";
      std::string splat = "ply\nformat binary_little_endian 1.0\nelement vertex 300\nproperty float x\nproperty float y\n"
                          "property float z\nproperty float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
                          "property float opacity\nend_header\n";
      for (int i = 0; i < n; i++)
      {
         char row[256];
         snprintf(row, sizeof(row), "%.9g %.9g %.9g 0 1 0 %d %d %d\n", xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2],
                  (int)std::lround(rgb[i * 3] * 255.0f), (int)std::lround(rgb[i * 3 + 1] * 255.0f),
                  (int)std::lround(rgb[i * 3 + 2] * 255.0f));
         ascii += row;
         const double intensity = i * 0.5;
         for (int k = 0; k < 3; k++)
            put(little, &xyz[i * 3 + k], 4, false);
         put(little, &intensity, 8, false);
         for (int k = 0; k < 3; k++)
            put(little, &rgb[i * 3 + k], 4, false);
         for (int k = 0; k < 3; k++)
            put(big, &xyz[i * 3 + k], 4, true);
         for (int k = 0; k < 3; k++)
         {
            const unsigned short c = (unsigned short)(std::lround(rgb[i * 3 + k] * 255.0f) * 257);
            put(big, &c, 2, true);
         }
         for (int k = 0; k < 3; k++)
            put(splat, &xyz[i * 3 + k], 4, false);
         for (int k = 0; k < 3; k++)
         {
            const float dc = (rgb[i * 3 + k] - 0.5f) / 0.28209479f;
            put(splat, &dc, 4, false);
         }
         const float opacity = 1.0f;
         put(splat, &opacity, 4, false);
      }
      const std::string mesh = "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\n"
                               "property float z\nelement face 2\nproperty list uchar int vertex_indices\nend_header\n"
                               "0 0 0\n1 0 0\n1 1 0\n0 1 0\n3 0 1 2\n3 0 2 3\n";

      auto parse = [](const std::string& bytes, PlyPoints::Cloud& c, std::string& err)
      {
         return PlyPoints::Parse((const unsigned char*)bytes.data(), bytes.size(), c, err);
      };
      auto cloudErr = [&](const PlyPoints::Cloud& c, float& errP, float& errC)
      {
         errP = errC = 1e9f;
         if ((int)c.points.size() != n)
            return false;
         errP = errC = 0.0f;
         for (int i = 0; i < n; i++)
         {
            const Particle& p = c.points[i];
            errP = std::max(errP, std::fabs(p.px - xyz[i * 3]) + std::fabs(p.py - xyz[i * 3 + 1]) +
                                  std::fabs(p.pz - xyz[i * 3 + 2]));
            errC = std::max(errC, std::fabs(p.r - rgb[i * 3]) + std::fabs(p.g - rgb[i * 3 + 1]) +
                                  std::fabs(p.b - rgb[i * 3 + 2]));
         }
         return true;
      };
      struct Case { const char* name; const std::string* bytes; float colourTol; };
      const Case cases[] = {
         { "ascii, uchar colour, normals", &ascii, 1e-6f },
         { "binary little-endian, float colour, a double to skip", &little, 1e-6f },
         { "binary big-endian, ushort colour, an empty face element", &big, 1e-6f },
         { "Gaussian splat, colour from f_dc", &splat, 1e-5f },
      };
      for (const Case& c : cases)
      {
         PlyPoints::Cloud cloud;
         std::string err;
         const PlyPoints::Kind kind = parse(*c.bytes, cloud, err);
         float errP = 0.0f, errC = 0.0f;
         const bool sized = kind == PlyPoints::Kind::kPoints && cloudErr(cloud, errP, errC);
         printf(".ply %s: kind %d, %zu points, err pos %.7f colour %.7f %s\n", c.name, (int)kind, cloud.points.size(),
                errP, errC, err.c_str());
         char label[200];
         snprintf(label, sizeof(label), ".ply %s: reads as the same points and colours", c.name);
         report(label, sized && cloud.hasColor && errP < 1e-5f && errC < c.colourTol);
      }
      {
         PlyPoints::Cloud cloud;
         std::string err;
         const bool meshKind = parse(mesh, cloud, err) == PlyPoints::Kind::kMesh;
         const std::string cut = little.substr(0, little.size() - 10);
         const bool cutErr = parse(cut, cloud, err) == PlyPoints::Kind::kError;
         const bool junkErr = parse(std::string("solid cube\n"), cloud, err) == PlyPoints::Kind::kError;
         report(".ply with faces reads as a mesh; truncated or non-PLY bytes are refused", meshKind && cutErr && junkErr);
      }

      // Through files and Model 3D.
      std::error_code ec;
      const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "infinite_gpupointstest_ply";
      std::filesystem::create_directories(dir, ec);
      auto write = [&](const char* name, const std::string& bytes)
      {
         const std::filesystem::path path = dir / name;
         std::ofstream f(path, std::ios::binary | std::ios::trunc);
         f.write(bytes.data(), (std::streamsize)bytes.size());
         return path.string();
      };
      const std::string asciiPath = write("cloud_ascii.ply", ascii);
      const std::string littlePath = write("cloud_le.ply", little);
      const std::string meshPath = write("quad.ply", mesh);

      ModelSourceNode meshModel;
      const bool meshLoaded = meshModel.Load(meshPath);
      printf("Model 3D quad.ply: %s\n", meshModel.Status().c_str());
      report("Model 3D: a .ply with faces still loads as a mesh",
             meshLoaded && !meshModel.IsPointCloud() && meshModel.TriangleCount() == 2 &&
             meshModel.GetPointCloud() == nullptr && meshModel.GetGpuPoints() == nullptr);

      ModelSourceNode model;
      model.pointSize = 0.03f;
      const bool loaded = model.Load(asciiPath);
      printf("Model 3D cloud_ascii.ply: %s\n", model.Status().c_str());
      const std::vector<Particle>* pts = model.GetPointCloud();
      bool fitted = false, coloured = false;
      if (pts != nullptr && (int)pts->size() == n)
      {
         float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
         float errC = 0.0f, errS = 0.0f;
         for (int i = 0; i < n; i++)
         {
            const Particle& p = (*pts)[i];
            const float q[3] = { p.px, p.py, p.pz };
            for (int k = 0; k < 3; k++)
            {
               lo[k] = std::min(lo[k], q[k]);
               hi[k] = std::max(hi[k], q[k]);
            }
            errC = std::max(errC, std::fabs(p.r - rgb[i * 3]) + std::fabs(p.g - rgb[i * 3 + 1]) +
                                  std::fabs(p.b - rgb[i * 3 + 2]));
            errS = std::max(errS, std::fabs(p.scale - 0.03f));
         }
         const float extent = std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2]));
         float centre = 0.0f;
         for (int k = 0; k < 3; k++)
            centre = std::max(centre, std::fabs(hi[k] + lo[k]) * 0.5f);
         fitted = std::fabs(extent - 1.0f) < 1e-4f && centre < 1e-4f;
         coloured = errC < 1e-6f && errS < 1e-7f && (*pts)[0].hasColor;
      }
      report("Model 3D: a face-less .ply loads as points, fitted and recentred, with the file's colours",
             loaded && model.IsPointCloud() && model.PointCount() == (size_t)n && model.GetMesh().vertices.empty() &&
             fitted && coloured);

      const unsigned long long rev0 = model.PointCloudRevision();
      const bool steady = model.PointCloudRevision() == rev0;
      model.pointSize = 0.02f;
      const bool moves = model.PointCloudRevision() != rev0 && (*model.GetPointCloud())[0].scale == 0.02f;
      ModelSourceNode littleModel;
      const bool sameAcross = littleModel.Load(littlePath) && littleModel.PointCount() == (size_t)n;
      float acrossErr = 0.0f;
      if (sameAcross)
         for (int i = 0; i < n; i++)
         {
            const Particle& a = (*model.GetPointCloud())[i];
            const Particle& b = (*littleModel.GetPointCloud())[i];
            acrossErr = std::max(acrossErr, std::fabs(a.px - b.px) + std::fabs(a.py - b.py) + std::fabs(a.pz - b.pz));
         }
      report("Model 3D: point size moves the revision only when it changes; ascii and binary files load alike",
             steady && moves && sameAcross && acrossErr < 1e-5f);

      GpuPointsGL::SetEnabledForTest(false);
      const bool cpuNone = model.GetGpuPoints() == nullptr;
      GpuPointsGL::SetEnabledForTest(true);
      const GpuPoints* g = model.GetGpuPoints();
      float errP = 1e9f, errW = 1e9f, errC = 1e9f;
      if (g != nullptr && g->count == n)
      {
         std::vector<float> pos((size_t)g->texW * (size_t)g->texH * 4), col(pos.size());
         glBindTexture(GL_TEXTURE_2D, g->posTex);
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pos.data());
         glBindTexture(GL_TEXTURE_2D, g->colorTex);
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, col.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         errP = errW = errC = 0.0f;
         for (int i = 0; i < n; i++)
         {
            const Particle& p = (*model.GetPointCloud())[i];
            errP = std::max(errP, std::fabs(pos[i * 4] - p.px) + std::fabs(pos[i * 4 + 1] - p.py) +
                                  std::fabs(pos[i * 4 + 2] - p.pz));
            errW = std::max(errW, std::fabs(pos[i * 4 + 3] - p.scale * model.PointBaseSize()));
            errC = std::max(errC, std::fabs(col[i * 4] - p.r) + std::fabs(col[i * 4 + 1] - p.g) +
                                  std::fabs(col[i * 4 + 2] - p.b));
         }
      }
      printf("Model 3D GPU points: err pos %.7f size %.7f colour %.7f\n", errP, errW, errC);
      report("Model 3D: GPU points only on the GPU path, texels equal the cloud, revision follows it",
             cpuNone && g != nullptr && g->revision == model.PointCloudRevision() && errP < 1e-6f && errW < 1e-6f &&
             errC < 1e-6f);

      GeometryOpNode modelXf;
      modelXf.op = GeometryOpNode::kTransform;
      modelXf.input = &model;
      modelXf.rotY = 25.0f;
      modelXf.offsetX = 0.0f;
      modelXf.scaleX = modelXf.scaleY = modelXf.scaleZ = 1.6f;
      auto renderModel = [&](bool gpu)
      {
         GpuPointsGL::SetEnabledForTest(gpu);
         Render3DNode r; // fresh, for the same reason as renderOnce
         r.geometry[0] = &modelXf;
         r.CookIfNeeded(frame++);
         return ReadRgba(r.GetOutputTexture(), r.GetOutputWidth(), r.GetOutputHeight());
      };
      const std::vector<unsigned char> cpuF = renderModel(false);
      const std::vector<unsigned char> gpuF = renderModel(true);
      const bool offered = modelXf.GetGpuPoints() != nullptr;
      model.bypassed = true;
      const std::vector<unsigned char> emptyF = renderModel(true);
      model.bypassed = false;
      const FrameDiff d = CompareFrames(cpuF, gpuF, emptyF);
      printf("Model 3D .ply behind a Transform: drawn %zu, differ %zu (max %d)\n", d.drawn, d.differ, d.maxDiff);
      report("Model 3D .ply behind a Transform: GPU points, frame matches the CPU frame",
             offered && d.Drawn() && d.Match());
      GpuPointsGL::SetEnabledForTest(wasEnabled);
      std::filesystem::remove_all(dir, ec);
   }

   printf("%s\n", allOk ? "GPU POINTS TEST OK" : "GPU POINTS TEST FAIL");
}

}
