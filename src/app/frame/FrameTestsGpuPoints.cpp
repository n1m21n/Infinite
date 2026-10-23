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

   printf("%s\n", allOk ? "GPU POINTS TEST OK" : "GPU POINTS TEST FAIL");
}

}
