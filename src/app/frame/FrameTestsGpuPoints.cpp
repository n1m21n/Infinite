// GPU points (docs/plans/gpu-points): the GPU path must draw the picture the
// CPU path draws. Renders one Image to Points -> Transform -> Render 3D chain
// with GPU points off, then on, and compares the two frames.
#include "app/AppShared.h"

#include "DisplaceNoise.h"
#include "GpuPoints.h"
#include "PlyPoints.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <tuple>

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

   // Step 12: the Field Modifier point presets. Each runs over a cloud of
   // mortal particles (ages 0..life, a few immortal) on both paths: the
   // preset must compile to GLSL, change the points, and the GPU texels -
   // life fraction in colour alpha included - must equal the CPU cloud. Then
   // each preset's own promise: sizes spread, colours differ, the stagger
   // follows the index, and age runs young colour -> old colour while the
   // sprite shrinks.
   {
      struct LifeCloud : IGeometrySource
      {
         std::vector<Particle> points;
         unsigned long long revision = NextMeshRevision();
         GpuPointsGL::Target target;
         GpuPoints gpu;
         unsigned long long uploaded = 0;
         bool valid = false;
         Mesh empty;
         ~LifeCloud() { GpuPointsGL::DestroyTarget(target); }
         const Mesh& GetMesh() override { return empty; }
         unsigned long long MeshRevision() override { return revision; }
         Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
         Material GetMaterial() const override { return Material(); }
         const std::vector<Particle>* GetPointCloud() override { return &points; }
         unsigned long long PointCloudRevision() override { return revision; }
         const GpuPoints* GetGpuPoints() override
         {
            if (!GpuPointsGL::Enabled())
               return nullptr;
            if (uploaded != revision)
            {
               uploaded = revision;
               valid = GpuPointsGL::Upload(target, gpu, points, 1.0f, "GpuPointsTest_LifeCloud");
               gpu.revision = revision;
            }
            return valid ? &gpu : nullptr;
         }
      };
      LifeCloud cloud;
      const int n = 600;
      for (int k = 0; k < n; k++)
      {
         Particle p;
         p.px = (float)(k % 30) / 15.0f - 1.0f;
         p.py = (float)(k / 30) / 10.0f - 1.0f;
         p.pz = std::sin((float)k * 0.37f) * 0.2f;
         p.scale = 0.04f;
         p.r = 0.6f; p.g = 0.8f; p.b = 1.0f;
         p.hasColor = true;
         p.life = (k % 50 == 7) ? 0.0f : 2.0f; // every 50th from 7 is immortal
         p.age = 2.0f * (float)k / (float)(n - 1);
         cloud.points.push_back(p);
      }

      auto readTex = [](unsigned int tex, int w, int h)
      {
         std::vector<float> px((size_t)w * (size_t)h * 4, 0.0f);
         glBindTexture(GL_TEXTURE_2D, tex);
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         return px;
      };

      GpuPointsGL::SetEnabledForTest(true);
      {
         const GpuPoints* g = cloud.GetGpuPoints();
         float errL = 1e9f;
         if (g != nullptr)
         {
            const std::vector<float> col = readTex(g->colorTex, g->texW, g->texH);
            errL = 0.0f;
            for (int k = 0; k < n; k++)
               errL = std::max(errL, std::fabs(col[(size_t)k * 4 + 3] - PointLifeFraction(cloud.points[k])));
         }
         report("Upload carries each point's life fraction in colour alpha", g != nullptr && errL < 1e-6f);
      }

      const char* const names[] = { "Points: Random Size", "Points: Random Colour", "Points: Stagger over Index",
                                    "Points: Age to Colour", "Points: Limit to Box", "Points: Project onto Shape" };
      for (const char* name : names)
      {
         int index = -1;
         for (size_t k = 0; k < FieldElementNode::Presets().size(); k++)
            if (FieldElementNode::Presets()[k].name == std::string(name))
               index = (int)k;
         char label[200];
         if (index < 0)
         {
            snprintf(label, sizeof(label), "%s: preset exists", name);
            report(label, false);
            continue;
         }

         FieldElementNode fe;
         fe.input = &cloud;
         fe.LoadPreset(index);
         GpuPointsGL::SetEnabledForTest(false);
         fe.CookIfNeeded(frame++);
         const bool cpuNone = fe.GetGpuPoints() == nullptr;
         std::vector<Particle> cpuCloud;
         if (const std::vector<Particle>* cc = fe.GetPointCloud())
            cpuCloud = *cc;
         GpuPointsGL::SetEnabledForTest(true);
         fe.CookIfNeeded(frame - 1 + 1000000); // a new cook at the same time
         const GpuPoints* g = fe.GetGpuPoints();

         float moved = 0.0f;
         if ((int)cpuCloud.size() == n)
            for (int k = 0; k < n; k++)
            {
               const Particle& a = cloud.points[k];
               const Particle& b = cpuCloud[k];
               moved = std::max(moved, std::fabs(a.py - b.py) + std::fabs(a.r - b.r) + std::fabs(a.g - b.g) +
                                       std::fabs(a.b - b.b) + std::fabs(a.scale - b.scale));
            }
         float errP = 1e9f, errW = 1e9f, errC = 1e9f, errL = 1e9f;
         if (g != nullptr && g->count == n && (int)cpuCloud.size() == n)
         {
            const std::vector<float> pos = readTex(g->posTex, g->texW, g->texH);
            const std::vector<float> col = readTex(g->colorTex, g->texW, g->texH);
            errP = errW = errC = errL = 0.0f;
            for (int k = 0; k < n; k++)
            {
               const Particle& p = cpuCloud[k];
               const float* q = &pos[(size_t)k * 4];
               const float* c = &col[(size_t)k * 4];
               errP = std::max(errP, std::fabs(q[0] - p.px) + std::fabs(q[1] - p.py) + std::fabs(q[2] - p.pz));
               errW = std::max(errW, std::fabs(q[3] - p.scale * fe.PointBaseSize()));
               errC = std::max(errC, std::fabs(c[0] - p.r) + std::fabs(c[1] - p.g) + std::fabs(c[2] - p.b));
               errL = std::max(errL, std::fabs(c[3] - PointLifeFraction(p)));
            }
         }

         // The preset's own promise, on the CPU cloud.
         bool promise = (int)cpuCloud.size() == n;
         std::string what;
         if (promise && index >= 0)
         {
            const std::string nm = name;
            if (nm == "Points: Random Size")
            {
               float lo = 1e9f, hi = 0.0f;
               for (const Particle& p : cpuCloud)
               {
                  lo = std::min(lo, p.scale);
                  hi = std::max(hi, p.scale);
               }
               promise = lo > 0.0f && hi / lo > 3.0f && hi <= 0.04f * 1.8f + 1e-5f;
               what = "sizes " + std::to_string(lo) + " .. " + std::to_string(hi);
            }
            else if (nm == "Points: Random Colour")
            {
               float spread = 0.0f;
               for (int k = 1; k < n; k++)
                  spread = std::max(spread, std::fabs(cpuCloud[k].r - cpuCloud[0].r) +
                                            std::fabs(cpuCloud[k].g - cpuCloud[0].g) +
                                            std::fabs(cpuCloud[k].b - cpuCloud[0].b));
               promise = spread > 0.8f;
               what = "colour spread " + std::to_string(spread);
            }
            else if (nm == "Points: Stagger over Index")
            {
               // Neighbours in index move nearly together; far ones apart.
               float near = 0.0f, far = 0.0f;
               for (int k = 0; k + 1 < n; k++)
               {
                  const float d0 = cpuCloud[k].py - cloud.points[k].py;
                  near = std::max(near, std::fabs(d0 - (cpuCloud[k + 1].py - cloud.points[k + 1].py)));
                  far = std::max(far, std::fabs(d0 - (cpuCloud[(k + n / 6) % n].py - cloud.points[(k + n / 6) % n].py)));
               }
               promise = near < 0.01f && far > 0.1f;
               what = "neighbour diff " + std::to_string(near) + ", far diff " + std::to_string(far);
            }
            else if (nm == "Points: Limit to Box")
            {
               // Clamp (the default) to a 0.6 box: everything inside, inside points untouched.
               float outside = 0.0f, insideMoved = 0.0f;
               for (int k = 0; k < n; k++)
               {
                  const Particle& a = cloud.points[k];
                  const Particle& b = cpuCloud[k];
                  outside = std::max({ outside, std::fabs(b.px) - 0.6f, std::fabs(b.py) - 0.6f, std::fabs(b.pz) - 0.6f });
                  if (std::fabs(a.px) < 0.6f && std::fabs(a.py) < 0.6f && std::fabs(a.pz) < 0.6f)
                     insideMoved = std::max(insideMoved, std::fabs(a.px - b.px) + std::fabs(a.py - b.py) + std::fabs(a.pz - b.pz));
               }
               promise = outside < 1e-5f && insideMoved < 1e-5f;
               what = "outside the box by " + std::to_string(outside) + ", inside points moved " + std::to_string(insideMoved);
            }
            else if (nm == "Points: Project onto Shape")
            {
               // Sphere of radius 1 (the default) at amount 1: every point lands on it.
               float off = 0.0f;
               for (const Particle& p : cpuCloud)
                  off = std::max(off, std::fabs(std::sqrt(p.px * p.px + p.py * p.py + p.pz * p.pz) - 1.0f));
               promise = off < 1e-4f;
               what = "off the sphere by " + std::to_string(off);
            }
            else
            {
               const Particle& young = cpuCloud[0];
               const Particle& old = cpuCloud[n - 1];
               const Particle& immortal = cpuCloud[7];
               const bool colours = std::fabs(young.r - 1.0f) + std::fabs(young.g - 0.92f) + std::fabs(young.b - 0.55f) < 1e-4f &&
                                    std::fabs(old.r - 0.35f) + std::fabs(old.g - 0.08f) + std::fabs(old.b - 0.45f) < 1e-4f &&
                                    std::fabs(immortal.g - 0.92f) < 1e-4f;
               const bool shrinks = std::fabs(young.scale - 0.04f) < 1e-6f && std::fabs(old.scale - 0.04f * 0.3f) < 1e-6f;
               promise = colours && shrinks;
               what = "young (" + std::to_string(young.r) + ", " + std::to_string(young.g) + ", " +
                      std::to_string(young.b) + ") size " + std::to_string(young.scale) + ", old (" +
                      std::to_string(old.r) + ", " + std::to_string(old.g) + ", " + std::to_string(old.b) +
                      ") size " + std::to_string(old.scale);
            }
         }

         printf("%s: moved %.3f, err pos %.6f size %.6f colour %.6f life %.6f; %s%s%s\n", name, moved, errP, errW,
                errC, errL, what.c_str(), fe.PointGpuFallback().empty() ? "" : "; fallback: ",
                fe.PointGpuFallback().c_str());
         snprintf(label, sizeof(label), "%s: compiles to GLSL, GPU points only on the GPU path", name);
         report(label, fe.PointGpuFallback().empty() && cpuNone && g != nullptr);
         snprintf(label, sizeof(label), "%s: GPU texels (life alpha too) equal the CPU cloud", name);
         report(label, moved > 0.01f && errP < 1e-3f && errW < 1e-4f && errC < 1e-3f && errL < 1e-6f);
         snprintf(label, sizeof(label), "%s: does what its name says", name);
         report(label, promise);
      }
      GpuPointsGL::SetEnabledForTest(wasEnabled);
   }

   // Step 5a: Displacement's Noise source. The C++ and GLSL noise are one
   // definition in two languages (DisplaceNoise.h), so every type, moving the
   // point (scalar and vector), tinting it or resizing it, must land the GPU
   // texels on the CPU cloud. Then the noise itself: each type spans roughly
   // -1..1, Curl is divergence free, a mesh keeps its seams closed, speed
   // animates with the transport and speed 0 holds still, and a default
   // Displacement still reads its texture.
   {
      struct NoiseCloud : IGeometrySource
      {
         std::vector<Particle> points;
         unsigned long long revision = NextMeshRevision();
         GpuPointsGL::Target target;
         GpuPoints gpu;
         unsigned long long uploaded = 0;
         bool valid = false;
         Mesh empty;
         ~NoiseCloud() { GpuPointsGL::DestroyTarget(target); }
         const Mesh& GetMesh() override { return empty; }
         unsigned long long MeshRevision() override { return revision; }
         Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
         Material GetMaterial() const override { return Material(); }
         const std::vector<Particle>* GetPointCloud() override { return &points; }
         unsigned long long PointCloudRevision() override { return revision; }
         const GpuPoints* GetGpuPoints() override
         {
            if (!GpuPointsGL::Enabled())
               return nullptr;
            if (uploaded != revision)
            {
               uploaded = revision;
               valid = GpuPointsGL::Upload(target, gpu, points, 1.0f, "GpuPointsTest_NoiseCloud");
               gpu.revision = revision;
            }
            return valid ? &gpu : nullptr;
         }
      };
      NoiseCloud cloud;
      const int n = 900;
      for (int k = 0; k < n; k++)
      {
         Particle p;
         p.px = (float)(k % 30) / 10.0f - 1.5f;
         p.py = (float)(k / 30) / 10.0f - 1.5f;
         p.pz = std::sin((float)k * 0.37f) * 0.7f;
         p.scale = 0.05f;
         p.r = 0.5f; p.g = 0.6f; p.b = 0.7f;
         p.hasColor = true;
         cloud.points.push_back(p);
      }

      auto readTex = [](unsigned int tex, int w, int h)
      {
         std::vector<float> px((size_t)w * (size_t)h * 4, 0.0f);
         glBindTexture(GL_TEXTURE_2D, tex);
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         return px;
      };

      const char* const typeNames[] = { "Smooth", "Fractal", "Curl", "Ridged", "Cells" };
      struct Use { const char* name; int mode; int affects; };
      const Use uses[] = {
         { "scalar position", DisplacementNode::kScalar, DisplaceNoise::kPosition },
         { "vector position", DisplacementNode::kVector, DisplaceNoise::kPosition },
         { "scalar colour", DisplacementNode::kScalar, DisplaceNoise::kColour },
         { "vector colour", DisplacementNode::kVector, DisplaceNoise::kColour },
         { "size", DisplacementNode::kScalar, DisplaceNoise::kSize },
      };
      for (int type = 0; type < 5; type++)
      {
         float worstP = 0.0f, worstW = 0.0f, worstC = 0.0f, leastMoved = 1e9f;
         bool allOffered = true, allCpuNone = true;
         for (const Use& u : uses)
         {
            DisplacementNode d;
            d.input = &cloud;
            d.source = DisplacementNode::kNoise;
            d.noiseType = type;
            d.mode = u.mode;
            d.affects = u.affects;
            d.strength = 0.4f;
            d.frequency = 1.3f;
            d.speed = 0.7f;
            d.seed = 17 + type;
            GpuPointsGL::SetEnabledForTest(false);
            d.CookIfNeeded(frame++);
            allCpuNone = allCpuNone && d.GetGpuPoints() == nullptr;
            std::vector<Particle> cpuCloud;
            if (const std::vector<Particle>* cc = d.GetPointCloud())
               cpuCloud = *cc;
            GpuPointsGL::SetEnabledForTest(true);
            const GpuPoints* g = d.GetGpuPoints();
            allOffered = allOffered && g != nullptr;
            float moved = 0.0f, errP = 1e9f, errW = 1e9f, errC = 1e9f;
            if ((int)cpuCloud.size() == n)
               for (int k = 0; k < n; k++)
               {
                  const Particle& a = cloud.points[k];
                  const Particle& b = cpuCloud[k];
                  moved = std::max(moved, std::fabs(a.px - b.px) + std::fabs(a.py - b.py) + std::fabs(a.pz - b.pz) +
                                          std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b) +
                                          std::fabs(a.scale - b.scale) * 10.0f);
               }
            if (g != nullptr && g->count == n && (int)cpuCloud.size() == n)
            {
               const std::vector<float> pos = readTex(g->posTex, g->texW, g->texH);
               const std::vector<float> col = readTex(g->colorTex, g->texW, g->texH);
               errP = errW = errC = 0.0f;
               for (int k = 0; k < n; k++)
               {
                  const Particle& p = cpuCloud[k];
                  const float* q = &pos[(size_t)k * 4];
                  const float* c = &col[(size_t)k * 4];
                  errP = std::max(errP, std::fabs(q[0] - p.px) + std::fabs(q[1] - p.py) + std::fabs(q[2] - p.pz));
                  errW = std::max(errW, std::fabs(q[3] - p.scale));
                  errC = std::max(errC, std::fabs(c[0] - p.r) + std::fabs(c[1] - p.g) + std::fabs(c[2] - p.b));
               }
            }
            printf("noise %s %s: moved %.3f, err pos %.6f size %.6f colour %.6f\n", typeNames[type], u.name, moved,
                   errP, errW, errC);
            worstP = std::max(worstP, errP);
            worstW = std::max(worstW, errW);
            worstC = std::max(worstC, errC);
            leastMoved = std::min(leastMoved, moved);
         }
         char label[200];
         snprintf(label, sizeof(label), "Noise %s: GPU points only on the GPU path", typeNames[type]);
         report(label, allOffered && allCpuNone);
         snprintf(label, sizeof(label), "Noise %s: position, colour and size all change the cloud", typeNames[type]);
         report(label, leastMoved > 0.02f);
         snprintf(label, sizeof(label), "Noise %s: GPU texels equal the CPU cloud", typeNames[type]);
         report(label, worstP < 2e-3f && worstW < 1e-4f && worstC < 2e-3f);
      }

      // The noise's own range, over a 3D grid: spans most of -1..1 and stays
      // inside it (a little headroom for the gain).
      for (int type = 0; type < 5; type++)
      {
         DisplaceNoise::Params np;
         np.type = type;
         np.frequency = 1.0f;
         np.w = 0.3f;
         float lo = 1e9f, hi = -1e9f, sum = 0.0f;
         int count = 0;
         for (int x = 0; x < 24; x++)
            for (int y = 0; y < 24; y++)
               for (int z = 0; z < 8; z++)
               {
                  float v[3];
                  DisplaceNoise::Vector(np, x * 0.29f - 3.4f, y * 0.31f - 3.6f, z * 0.43f - 1.7f, v);
                  for (float c : v)
                  {
                     lo = std::min(lo, c);
                     hi = std::max(hi, c);
                     sum += c;
                     count++;
                  }
               }
         printf("noise %s range: %.3f .. %.3f, mean %.3f\n", typeNames[type], lo, hi, sum / (float)count);
         char label[200];
         snprintf(label, sizeof(label), "Noise %s spans roughly -1..1", typeNames[type]);
         report(label, lo > -1.3f && hi < 1.3f && hi - lo > 0.9f);
      }

      // Curl is divergence free: its divergence is small next to the size of
      // its own derivatives.
      {
         DisplaceNoise::Params np;
         np.type = DisplaceNoise::kCurl;
         const float h = 0.002f;
         double divSum = 0.0, derivSum = 0.0;
         for (int k = 0; k < 400; k++)
         {
            const float x = std::sin(k * 1.7f) * 2.0f, y = std::cos(k * 0.9f) * 2.0f, z = std::sin(k * 0.31f) * 2.0f;
            float a[3], b[3];
            float div = 0.0f;
            for (int axis = 0; axis < 3; axis++)
            {
               const float dx = axis == 0 ? h : 0.0f, dy = axis == 1 ? h : 0.0f, dz = axis == 2 ? h : 0.0f;
               DisplaceNoise::Vector(np, x + dx, y + dy, z + dz, a);
               DisplaceNoise::Vector(np, x - dx, y - dy, z - dz, b);
               div += (a[axis] - b[axis]) / (2.0f * h);
               for (int c = 0; c < 3; c++)
                  derivSum += std::fabs((a[c] - b[c]) / (2.0f * h));
            }
            divSum += std::fabs(div);
         }
         const double ratio = divSum / std::max(1e-9, derivSum / 9.0);
         printf("curl: mean |div| / mean |dV| = %.4f\n", ratio);
         report("Noise Curl is divergence free", ratio < 0.1);
      }

      // A mesh: vertices move, and vertices that shared a position before
      // still share one after (the sphere's UV seam stays closed).
      {
         GeometryNode sphere;
         sphere.shape = 2;
         DisplacementNode d;
         d.input = &sphere;
         d.source = DisplacementNode::kNoise;
         d.noiseType = DisplaceNoise::kFractal;
         d.strength = 0.2f;
         d.speed = 0.0f;
         d.CookIfNeeded(frame++);
         const Mesh& in = sphere.GetMesh();
         const Mesh out = d.GetMesh();
         float moved = 0.0f, seam = 0.0f;
         int shared = 0;
         if (in.vertices.size() == out.vertices.size())
         {
            std::map<std::tuple<float, float, float>, size_t> first;
            for (size_t k = 0; k < in.vertices.size(); k++)
            {
               const Vertex& a = in.vertices[k];
               const Vertex& b = out.vertices[k];
               moved = std::max(moved, std::fabs(a.px - b.px) + std::fabs(a.py - b.py) + std::fabs(a.pz - b.pz));
               auto key = std::make_tuple(a.px, a.py, a.pz);
               auto it = first.find(key);
               if (it == first.end())
                  first[key] = k;
               else
               {
                  const Vertex& c = out.vertices[it->second];
                  seam = std::max(seam, std::fabs(c.px - b.px) + std::fabs(c.py - b.py) + std::fabs(c.pz - b.pz));
                  shared++;
               }
            }
         }
         printf("noise mesh: %zu vertices, moved %.3f, %d shared, seam gap %.6f\n", out.vertices.size(), moved, shared,
                seam);
         report("Noise on a mesh moves its vertices", moved > 0.02f);
         report("Noise on a mesh keeps its seams closed", shared > 0 && seam < 1e-5f);

         // Speed 0 holds still as the transport moves; speed > 0 animates.
         const unsigned long long rev0 = d.MeshRevision();
         Transport::Instance().Seek(Transport::Instance().Seconds() + 1.25);
         d.CookIfNeeded(frame++);
         d.GetMesh();
         const bool still = d.MeshRevision() == rev0;
         d.speed = 0.8f;
         d.CookIfNeeded(frame++);
         d.GetMesh();
         const unsigned long long rev1 = d.MeshRevision();
         Transport::Instance().Seek(Transport::Instance().Seconds() + 1.25);
         d.CookIfNeeded(frame++);
         const Mesh later = d.GetMesh();
         float animated = 0.0f;
         if (later.vertices.size() == out.vertices.size())
            for (size_t k = 0; k < later.vertices.size(); k++)
               animated = std::max(animated, std::fabs(later.vertices[k].px - out.vertices[k].px) +
                                             std::fabs(later.vertices[k].py - out.vertices[k].py) +
                                             std::fabs(later.vertices[k].pz - out.vertices[k].pz));
         printf("noise mesh over time: speed 0 still %d, speed 0.8 moved %.3f\n", still ? 1 : 0, animated);
         report("Noise at speed 0 holds still as the transport moves", still);
         report("Noise at speed > 0 animates with the transport", d.MeshRevision() != rev1 && animated > 0.01f);
      }

      // A new Displacement reads its texture, as every saved patch expects.
      {
         DisplacementNode d;
         report("Displacement's default source is Texture", d.source == DisplacementNode::kTexture);
      }
      GpuPointsGL::SetEnabledForTest(wasEnabled);
   }

   // Step 5b: Feedback 3D. A Transform inside the loop moves the cloud a
   // step further every cook, on the GPU exactly as on the CPU; Reset and a
   // missing loop both fall back to the seed; a mesh loop accumulates the
   // same way; and the wiring refuses a geometry loop that does not close
   // through the loop pin, while a cycle through it cooks without recursing.
   {
      std::vector<Particle> seedPts;
      for (int k = 0; k < 64; k++)
      {
         Particle p;
         p.px = (float)(k % 8) * 0.1f; p.py = (float)(k / 8) * 0.1f; p.pz = 0.0f;
         p.scale = 1.0f; p.r = 0.2f; p.g = 0.4f; p.b = 0.8f; p.hasColor = true;
         seedPts.push_back(p);
      }
      struct SeedCloud : IGeometrySource
      {
         std::vector<Particle> points;
         unsigned long long revision = NextMeshRevision();
         Mesh empty;
         const Mesh& GetMesh() override { return empty; }
         unsigned long long MeshRevision() override { return 0; }
         Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
         Material GetMaterial() const override { return Material(); }
         const std::vector<Particle>* GetPointCloud() override { return &points; }
         unsigned long long PointCloudRevision() override { return revision; }
      };

      // The loop follows the transport (it holds while the clock is still), so
      // every cook moves the clock a beat; the test must not depend on what an
      // earlier step left in the transport.
      double feedbackBeat = 1.0;
      auto NextFeedbackBeat = [&]() { Transport::Instance().SeekBeats(feedbackBeat += 1.0); };
      auto runLoop = [&](bool gpu, int cooks, float& meanX, size_t& count)
      {
         GpuPointsGL::SetEnabledForTest(gpu);
         SeedCloud seed;
         seed.points = seedPts;
         GeometryFeedbackNode fb;
         GeometryOpNode step;
         step.op = GeometryOpNode::kTransform;
         step.offsetX = 0.25f;
         step.input = &fb;
         fb.seed = &seed;
         fb.loop = &step;
         for (int i = 0; i < cooks; i++)
         {
            NextFeedbackBeat();
            step.CookIfNeeded(frame++);
            fb.GetGpuPoints(); // what a GPU Render 3D would pull
         }
         const std::vector<Particle>* c = fb.GetPointCloud();
         count = c ? c->size() : 0;
         meanX = 0.0f;
         if (c)
            for (const Particle& p : *c)
               meanX += p.px;
         meanX = count ? meanX / (float)count : 0.0f;
         return fb.Generation();
      };
      float cpuX = 0.0f, gpuX = 0.0f;
      size_t cpuN = 0, gpuN = 0;
      const int cpuGen = runLoop(false, 5, cpuX, cpuN);
      const int gpuGen = runLoop(true, 5, gpuX, gpuN);
      // Seed mean x is 0.35; cook 1 seeds, cooks 2..5 each add one 0.25 step.
      printf("feedback 3D: cpu mean x %.4f (%zu pts, gen %d), gpu mean x %.4f (%zu pts, gen %d)\n", cpuX, cpuN,
             cpuGen, gpuX, gpuN, gpuGen);
      report("Feedback 3D accumulates a Transform each cook (CPU)",
             cpuN == seedPts.size() && std::fabs(cpuX - (0.35f + 4 * 0.25f)) < 1e-4f && cpuGen == 4);
      report("Feedback 3D on the GPU matches the CPU", gpuN == cpuN && std::fabs(gpuX - cpuX) < 1e-4f);

      // Reset and pass-through.
      {
         GpuPointsGL::SetEnabledForTest(false);
         SeedCloud seed;
         seed.points = seedPts;
         GeometryFeedbackNode fb;
         GeometryOpNode step;
         step.op = GeometryOpNode::kTransform;
         step.offsetX = 0.25f;
         step.input = &fb;
         fb.seed = &seed;
         fb.loop = &step;
         for (int i = 0; i < 3; i++)
         {
            NextFeedbackBeat();
            step.CookIfNeeded(frame++);
         }
         fb.Reset();
         fb.CookIfNeeded(frame++);
         const std::vector<Particle>* c = fb.GetPointCloud();
         const bool reseeded = c && c->size() == seedPts.size() && std::fabs((*c)[9].px - seedPts[9].px) < 1e-6f &&
                               fb.Generation() == 0;
         report("Feedback 3D Reset starts over from the seed", reseeded);
         fb.loop = nullptr;
         fb.CookIfNeeded(frame++);
         c = fb.GetPointCloud();
         report("Feedback 3D with no loop passes the seed through",
                !fb.Looping() && c && std::fabs((*c)[9].px - seedPts[9].px) < 1e-6f);
      }

      // A mesh loop: a box nudged up each cook.
      {
         GeometryNode box;
         GeometryFeedbackNode fb;
         GeometryOpNode step;
         step.op = GeometryOpNode::kTransform;
         step.offsetY = 0.5f;
         step.input = &fb;
         fb.seed = &box;
         fb.loop = &step;
         box.CookIfNeeded(frame);
         const Mesh seedMesh = box.GetMesh();
         for (int i = 0; i < 4; i++)
         {
            NextFeedbackBeat();
            step.CookIfNeeded(frame++);
         }
         const Mesh& out = fb.GetMesh();
         float dy = 0.0f;
         if (!out.vertices.empty() && out.vertices.size() == seedMesh.vertices.size())
         {
            const Mat4 bm = box.GetModelMatrix();
            float sy = bm.m[1] * seedMesh.vertices[0].px + bm.m[5] * seedMesh.vertices[0].py +
                       bm.m[9] * seedMesh.vertices[0].pz + bm.m[13];
            dy = out.vertices[0].py - sy;
         }
         printf("feedback 3D mesh: %zu vertices, moved up %.3f after 3 loops\n", out.vertices.size(), dy);
         report("Feedback 3D accumulates a mesh loop", std::fabs(dy - 1.5f) < 1e-4f);
      }

      // Wiring: a geometry loop is refused unless it closes through the
      // loop pin; the legal cycle cooks once per frame and does not recurse.
      {
         const size_t before = gNodes.size();
         // Read each index at once: a later spawn can grow gNodes and move the earlier ones.
         auto spawn = [](const char* type, float x)
         {
            GraphNode* gn = SpawnNode(type, "3D", x, 0.0f);
            return gn ? gn->index : -1;
         };
         const int ai = spawn("Feedback 3D", 0.0f);
         const int bi = spawn("Transform", 200.0f);
         const int ci = spawn("Twist", 400.0f);
         std::string err;
         bool ok = ai >= 0 && bi >= 0 && ci >= 0;
         ok = ok && ConnectNodes(ai, 0, bi, 0, err);  // fb -> transform
         ok = ok && ConnectNodes(bi, 0, ci, 0, err);  // transform -> twist
         const bool refused = ok && !ConnectNodes(ci, 0, bi, 0, err) && !ConnectNodes(ci, 0, ai, 0, err);
         const bool loopOk = ok && ConnectNodes(ci, 0, ai, GeometryFeedbackNode::kLoopSlot, err);
         printf("feedback 3D wiring: chain %d, plain cycle refused %d, loop pin accepted %d (%s)\n", ok, refused,
                loopOk, err.c_str());
         report("A geometry cycle is refused outside Feedback 3D's loop pin", refused);
         report("A cycle through Feedback 3D's loop pin is accepted", loopOk);
         if (loopOk)
         {
            auto* fbn = dynamic_cast<GeometryFeedbackNode*>(FindNodeByIndex(ai)->node.get());
            FindNodeByIndex(ci)->node->CookIfNeeded(frame++);
            FindNodeByIndex(ci)->node->CookIfNeeded(frame++);
            report("The legal geometry cycle cooks without recursing", fbn != nullptr);
         }
         // Leave the graph as it was.
         for (int idx : { ci, bi, ai })
            for (size_t k = before; k < gNodes.size(); k++)
               if (gNodes[k].index == idx)
               {
                  gNodes.erase(gNodes.begin() + (long)k);
                  break;
               }
      }
      GpuPointsGL::SetEnabledForTest(wasEnabled);
   }

   // Step 5c: Point Trails. A cloud moved a step each cook leaves tails of
   // `length` samples behind it, in world space, tapered and faded; a paused
   // input freezes them; sample every N thins the samples; a reborn particle
   // starts a new tail by ID but not by index; a dead point draws none; the
   // sample budget caps tracked points; and a Particle System on the GPU path
   // gives tails through its readback.
   {
      struct MovingCloud : IGeometrySource
      {
         std::vector<Particle> points;
         unsigned long long revision = NextMeshRevision();
         Mat4 model = Mat4::Identity();
         Mesh empty;
         const Mesh& GetMesh() override { return empty; }
         unsigned long long MeshRevision() override { return 0; }
         Mat4 GetModelMatrix() const override { return model; }
         Material GetMaterial() const override { return Material(); }
         const std::vector<Particle>* GetPointCloud() override { return &points; }
         unsigned long long PointCloudRevision() override { return revision; }
         void Step(float dx)
         {
            for (Particle& p : points)
            {
               p.px += dx;
               p.age += 0.1f;
            }
            revision = NextMeshRevision();
         }
      };
      auto makeCloud = [](MovingCloud& c, int n)
      {
         c.points.clear();
         for (int k = 0; k < n; k++)
         {
            Particle p;
            p.py = (float)k * 0.5f;
            p.r = 0.5f; p.g = 0.25f; p.b = 1.0f; p.hasColor = true;
            p.life = 100.0f;
            c.points.push_back(p);
         }
      };
      // Max |offset from the ring centre| over one ring's three vertices: the radius.
      auto ringRadius = [](const Mesh& m, size_t ring, const float* centre)
      {
         float r = 0.0f;
         for (size_t j = 0; j < 3; j++)
         {
            const Vertex& v = m.vertices[ring * 3 + j];
            const float dx = v.px - centre[0], dy = v.py - centre[1], dz = v.pz - centre[2];
            r = std::max(r, std::sqrt(dx * dx + dy * dy + dz * dz));
         }
         return r;
      };

      MovingCloud cloud;
      makeCloud(cloud, 8);
      cloud.model.m[14] = 2.0f; // world z = 2
      PointTrailsNode trails;
      trails.input = &cloud;
      trails.length = 5;
      trails.width = 0.02f;
      trails.taper = 0.5f;
      trails.fade = 1.0f;
      trails.CookIfNeeded(frame++);
      for (int i = 0; i < 9; i++)
      {
         cloud.Step(0.1f);
         trails.CookIfNeeded(frame++);
      }
      const Mesh& tm = trails.GetMesh();
      const bool shape = trails.TrailCount() == 8 && tm.vertices.size() == 8 * 5 * 3 && tm.indices.size() == 8 * 4 * 18 &&
                         tm.vertexColor.size() == tm.vertices.size() * 3;
      // Point 0's head ring is at its live position (x 0.9, z 2), its tail 4 steps back.
      const float head[3] = { 0.9f, 0.0f, 2.0f }, tail[3] = { 0.5f, 0.0f, 2.0f };
      const float headR = shape ? ringRadius(tm, 0, head) : 0.0f, tailR = shape ? ringRadius(tm, 4, tail) : 0.0f;
      const float headC = shape ? tm.vertexColor[2] : 0.0f, tailC = shape ? tm.vertexColor[4 * 9 + 2] : 1.0f;
      printf("point trails: %zu tails, %zu vertices, head r %.4f tail r %.4f, head blue %.3f tail blue %.3f\n",
             trails.TrailCount(), tm.vertices.size(), headR, tailR, headC, tailC);
      report("Point Trails: one tail of `length` samples per point", shape);
      report("Point Trails: tails sit in world space, head at the point, tail behind it",
             shape && std::fabs(headR - 0.02f) < 1e-4f && std::fabs(tailR - 0.01f) < 1e-4f);
      report("Point Trails: taper and fade over age, colour from the points",
             shape && std::fabs(headC - 1.0f) < 1e-5f && std::fabs(tailC) < 1e-5f);

      const unsigned long long rev = trails.MeshRevision();
      const int samples = trails.SamplesTaken();
      trails.CookIfNeeded(frame++);
      trails.CookIfNeeded(frame++);
      report("Point Trails: a paused input freezes the tails",
             trails.MeshRevision() == rev && trails.SamplesTaken() == samples);
      trails.width = 0.04f;
      trails.colorMode = PointTrailsNode::kColorGradient;
      trails.fade = 0.0f;
      trails.CookIfNeeded(frame++);
      const Mesh& gm = trails.GetMesh();
      const bool gradient = gm.vertexColor.size() >= 4 * 9 + 3 &&
                            std::fabs(gm.vertexColor[0] - trails.headColor[0]) < 1e-5f &&
                            std::fabs(gm.vertexColor[4 * 9] - trails.tailColor[0]) < 1e-5f;
      report("Point Trails: a look change rebuilds without sampling (gradient over age)",
             trails.MeshRevision() != rev && trails.SamplesTaken() == samples && gradient &&
             std::fabs(ringRadius(gm, 0, head) - 0.04f) < 1e-4f);

      // Sample every 2: the first cook samples, then every second change.
      {
         MovingCloud c2;
         makeCloud(c2, 4);
         PointTrailsNode t2;
         t2.input = &c2;
         t2.sampleEvery = 2;
         t2.CookIfNeeded(frame++);
         for (int i = 0; i < 9; i++)
         {
            c2.Step(0.1f);
            t2.CookIfNeeded(frame++);
         }
         report("Point Trails: sample every 2 takes every other frame", t2.SamplesTaken() == 5);
      }

      // Rebirth and death.
      {
         auto rebirth = [&](int match)
         {
            MovingCloud c3;
            makeCloud(c3, 3);
            PointTrailsNode t3;
            t3.input = &c3;
            t3.length = 10;
            t3.matchMode = match;
            t3.CookIfNeeded(frame++);
            for (int i = 0; i < 4; i++)
            {
               c3.Step(0.1f);
               t3.CookIfNeeded(frame++);
            }
            c3.points[1].age = 0.0f;   // slot 1 is a new particle
            c3.points[2].alive = false; // slot 2 died
            c3.Step(0.1f);
            t3.CookIfNeeded(frame++);
            return t3.TrailCount();
         };
         const size_t byId = rebirth(PointTrailsNode::kMatchParticleId);
         const size_t byIndex = rebirth(PointTrailsNode::kMatchIndex);
         printf("point trails: rebirth/death leave %zu tails by ID, %zu by index\n", byId, byIndex);
         report("Point Trails: a reborn particle starts a new tail by ID, keeps it by index; a dead one has none",
                byId == 1 && byIndex == 2);
      }

      // The sample budget: 240 x 100k is more than kMaxSamples.
      {
         MovingCloud c4;
         makeCloud(c4, 2);
         PointTrailsNode t4;
         t4.input = &c4;
         t4.length = 240;
         t4.pointLimit = 100000;
         t4.CookIfNeeded(frame++);
         report("Point Trails: the sample budget caps tracked points",
                t4.TrackedPoints() == PointTrailsNode::kMaxSamples / 240);
      }

      // Particle System on the GPU path -> Point Trails.
      {
         Transport& tr = Transport::Instance();
         const bool wasPlaying = tr.IsPlaying();
         const float wasTempo = tr.Tempo();
         tr.SetPlaying(false);
         tr.SetTempo(120.0f);
         GpuPointsGL::SetEnabledForTest(true);
         ParticleSystemNode ps;
         ps.maxParticles = 2000;
         ps.emitRate = 600.0f;
         ps.lifetime = 1.0f;
         PointTrailsNode t5;
         t5.input = &ps;
         t5.length = 20;
         double beat = 0.0;
         tr.SeekBeats(beat);
         t5.CookIfNeeded(frame++);
         for (int i = 0; i < 40; i++)
         {
            beat += 1.0 / 30.0;
            tr.SeekBeats(beat);
            t5.CookIfNeeded(frame++);
         }
         bool finite = true;
         for (const Vertex& v : t5.GetMesh().vertices)
            finite = finite && std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz);
         printf("point trails over GPU particles: %zu tails, %zu vertices, gpu points %d\n", t5.TrailCount(),
                t5.GetMesh().vertices.size(), ps.GetGpuPoints() != nullptr);
         report("Point Trails: GPU particles give tails", ps.GetGpuPoints() != nullptr && t5.TrailCount() > 50 && finite);
         tr.SeekBeats(0.0);
         tr.SetTempo(wasTempo);
         tr.SetPlaying(wasPlaying);
      }
      GpuPointsGL::SetEnabledForTest(wasEnabled);
   }

   printf("%s\n", allOk ? "GPU POINTS TEST OK" : "GPU POINTS TEST FAIL");
}

}
