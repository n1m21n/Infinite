// Engine RHI self-test (INFINITE_RHITEST): Metal draws into an IOSurface-backed image and GL reads the same
// pixels back with no copy. This is the interop every later engine stage stands on.
#include "app/AppShared.h"
#include "engine/rhi/Rhi.h"

namespace app
{
bool RunRhiTest()
{
   printf("[RHI TEST] Metal -> IOSurface -> GL\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what)
   {
      printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
      if (!ok)
         failures++;
   };
   auto dev = rhi::CreateDevice();
#if defined(__APPLE__)
   check(dev != nullptr, "a Metal device exists");
   if (!dev)
      return false;
   printf("  %s\n", dev->Name().c_str());
   for (rhi::Format fmt : { rhi::Format::RGBA8, rhi::Format::RGBA16F })
   {
      const bool half = fmt == rhi::Format::RGBA16F;
      const int W = 64, H = 64;
      auto img = dev->CreateSharedImage(W, H, fmt);
      check(img && img->GlTexture() != 0, half ? "RGBA16F image binds in GL" : "RGBA8 image binds in GL");
      if (!img)
         continue;
      const float clear[4] = { 0.0f, 0.25f, 0.5f, 1.0f }, tri[3] = { 1.0f, 0.5f, 0.0f };
      check(img->DrawTestTriangle(clear, tri), "Metal draw completes");
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, img->GlTexture(), 0);
      check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "GL can render-read the shared image");
      std::vector<float> px((size_t)W * H * 4);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, px.data());
      auto at = [&](int x, int y, int c) { return px[((size_t)y * W + x) * 4 + c]; };
      const double tol = half ? 0.002 : 0.01;
      // Corner is the clear colour; the centre is inside the triangle. Metal and GL disagree on which row is up,
      // so the centre test is symmetric about it.
      check(std::fabs(at(2, 2, 0) - clear[0]) < tol && std::fabs(at(2, 2, 1) - clear[1]) < tol &&
               std::fabs(at(2, 2, 2) - clear[2]) < tol,
            half ? "RGBA16F corner = clear colour" : "RGBA8 corner = clear colour");
      check(std::fabs(at(32, 32, 0) - tri[0]) < tol && std::fabs(at(32, 32, 1) - tri[1]) < tol &&
               std::fabs(at(32, 32, 2) - tri[2]) < tol,
            half ? "RGBA16F centre = triangle colour" : "RGBA8 centre = triangle colour");
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &fbo);
   }
   check(glGetError() == GL_NO_ERROR, "no GL error");
#else
   check(dev == nullptr, "no engine backend yet on this platform (GL renderer stays)");
#endif
   printf(failures == 0 ? "RHITEST OK\n" : "RHITEST FAIL (%d)\n", failures);
   return failures == 0;
}
}
