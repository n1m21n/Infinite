// Render 3D HDR self-test: a real cube through the real renderer, picture read back.
#include "app/AppShared.h"

namespace app
{
// ===================================================== INFINITE_R3DHDRTEST
// Asserts: old patches load with HDR off and ACES; HDR with no bloom and no
// tonemap matches the plain render; the background keeps its exact colour
// under a tonemap; bloom spills light past a bright object's edge; AgX and
// Neutral stay in range and differ from ACES.
bool RunRender3DHdrTest()
{
   printf("[R3D HDR TEST] HDR finish, bloom, tonemaps\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };

   {
      Render3DNode fresh;
      check(fresh.hdr && fresh.tonemap == 3, "a new node starts in HDR with AgX", fresh.hdr, fresh.tonemap);
      Render3DNode old;
      old.BeforeLoadParams();
      check(!old.hdr && old.tonemap == 1, "an old patch loads with HDR off and ACES", old.hdr, old.tonemap);
   }

   GeometryNode geo; // cube
   Render3DNode r;
   r.geometry[0] = &geo;
   r.width = 256.0f;
   r.height = 256.0f;
   r.samples = 0;
   int frame = 5000;
   unsigned int scratch = 0;
   auto render = [&](std::vector<float>& px) -> bool
   {
      for (int i = 0; i < 2; i++)
         r.CookIfNeeded(++frame);
      const int w = r.GetOutputWidth(), h = r.GetOutputHeight();
      return w == 256 && h == 256 && GLUtil::ReadTexturePixels(scratch, r.GetOutputTexture(), w, h, px);
   };
   auto lum = [](const std::vector<float>& px, size_t i) { return 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]; };

   std::vector<float> ldr, hdr, aces, agx, neutral, noBloom, withBloom;
   r.hdr = false; r.tonemap = 0;
   const bool okLdr = render(ldr);
   r.hdr = true; r.tonemap = 0; r.bloom = 0.0f;
   const bool okHdr = render(hdr);
   if (!okLdr || !okHdr)
   {
      printf("  [FAIL] no picture from Render 3D\nR3DHDRTEST FAIL\n");
      return false;
   }
   double diff = 0.0, lit = 0.0;
   for (size_t i = 0; i < ldr.size(); i += 4)
   {
      diff += std::fabs(lum(ldr, i) - lum(hdr, i));
      lit += lum(ldr, i) > 0.1 ? 1.0 : 0.0;
   }
   diff /= (double)(ldr.size() / 4);
   check(lit > 256.0 * 256.0 * 0.03, "the cube is on screen", lit, 0.0);
   check(diff < 0.01, "HDR with no tonemap or bloom matches the plain render", diff, 0.01);

   // Bright object: the tonemaps must keep it in range and tell themselves apart.
   geo.emission = 6.0f;
   r.tonemap = 1; render(aces);
   r.tonemap = 3; render(agx);
   r.tonemap = 4; render(neutral);
   double maxAgx = 0.0, maxNeutral = 0.0, dAgx = 0.0, dNeutral = 0.0;
   for (size_t i = 0; i < agx.size(); i += 4)
      for (int c = 0; c < 3; c++)
      {
         maxAgx = std::max(maxAgx, (double)agx[i + c]);
         maxNeutral = std::max(maxNeutral, (double)neutral[i + c]);
         dAgx += std::fabs(agx[i + c] - aces[i + c]);
         dNeutral += std::fabs(neutral[i + c] - aces[i + c]);
      }
   dAgx /= (double)agx.size();
   dNeutral /= (double)agx.size();
   check(maxAgx <= 1.001 && maxNeutral <= 1.001, "AgX and Neutral stay within 0..1", maxAgx, maxNeutral);
   check(dAgx > 0.002 && dNeutral > 0.002, "AgX and Neutral differ from ACES", dAgx, dNeutral);

   // Background exactness under a tonemap: the corner is the clear colour.
   const size_t corner = 0;
   const double bgExpect = r.bgColor[1]; // the clear colour is already encoded
   check(std::fabs(agx[corner + 1] - bgExpect) < 1.5 / 255.0, "the background keeps its exact colour under AgX",
         agx[corner + 1], bgExpect);

   // Bloom: pixels that were background spill light from the bright cube.
   r.tonemap = 3;
   r.bloom = 0.0f; render(noBloom);
   r.bloom = 1.0f; r.bloomThreshold = 1.0f; r.bloomRadius = 0.5f; render(withBloom);
   double bgBefore = 0.0, bgAfter = 0.0;
   int bgCount = 0;
   for (size_t i = 0; i < noBloom.size(); i += 4)
      if (lum(noBloom, i) < 0.08)
      {
         bgBefore += lum(noBloom, i);
         bgAfter += lum(withBloom, i);
         bgCount++;
      }
   if (bgCount > 0) { bgBefore /= bgCount; bgAfter /= bgCount; }
   check(bgCount > 0 && bgAfter > bgBefore + 0.01, "bloom spills light past the bright cube's edge", bgBefore, bgAfter);

   const GLenum err = glGetError();
   check(err == GL_NO_ERROR, "no GL error", (double)err, 0.0);
   r.geometry[0] = nullptr;
   if (scratch != 0)
      glDeleteFramebuffers(1, &scratch);

   printf(failures == 0 ? "R3DHDRTEST OK\n" : "R3DHDRTEST FAIL (%d)\n", failures);
   return failures == 0;
}
}
