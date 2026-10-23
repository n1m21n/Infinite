// GPU points (docs/plans/gpu-points): the GPU path must draw the picture the
// CPU path draws. Renders one Image to Points -> Transform -> Render 3D chain
// with GPU points off, then on, and compares the two frames.
#include "app/AppShared.h"

#include "GpuPoints.h"

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

   Render3DNode render;
   render.geometry[0] = &xf;

   int frame = 41001;
   auto renderOnce = [&](bool gpu)
   {
      GpuPointsGL::SetEnabledForTest(gpu);
      render.CookIfNeeded(frame++);
      return ReadRgba(render.GetOutputTexture(), render.GetOutputWidth(), render.GetOutputHeight());
   };

   const std::vector<unsigned char> cpuFrame = renderOnce(false);
   const bool cpuHadNoGpu = i2p.GetGpuPoints() == nullptr && xf.GetGpuPoints() == nullptr;
   const size_t cpuCount = i2p.GetPoints().size();

   const std::vector<unsigned char> gpuFrame = renderOnce(true);
   const bool gpuOffered = i2p.GetGpuPoints() != nullptr && xf.GetGpuPoints() != nullptr;

   // The occlusion count lands a cook later; a still image must report the
   // same number of kept points as the CPU build did.
   for (int i = 0; i < 4; i++)
      render.CookIfNeeded(frame++);
   const size_t gpuCount = i2p.PointCount();

   // The same scene with no points, to prove the points are what is drawn.
   i2p.bypassed = true;
   const std::vector<unsigned char> emptyFrame = renderOnce(true);
   i2p.bypassed = false;

   const int w = render.GetOutputWidth(), h = render.GetOutputHeight();
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
   printf("%s\n", allOk ? "GPU POINTS TEST OK" : "GPU POINTS TEST FAIL");
}

}
