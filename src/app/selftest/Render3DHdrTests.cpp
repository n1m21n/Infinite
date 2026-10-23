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

namespace app
{
// ===================================================== INFINITE_R3DPANOTEST
// 360 / dome output: a bright cube in front of the camera lands where each
// layout says forward is, the back stays empty, a fisheye's corners are
// black, and an equirect of the sky gradient has no seam where cube faces meet.
bool RunRender3DPanoTest()
{
   printf("[R3D PANO TEST] equirect, cubemap 3x2, fisheye dome\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };
   GeometryNode geo; // cube
   geo.emission = 4.0f;
   Render3DNode r;
   r.geometry[0] = &geo;
   r.samples = 0;
   r.envAsBackground = false;
   r.camElevation = 0.0f; // level with the cube: a panorama keeps the horizon level, so elevation only lifts the eye
   int frame = 7000;
   unsigned int scratch = 0;
   std::vector<float> px;
   int w = 0, h = 0;
   auto render = [&]() -> bool
   {
      for (int i = 0; i < 2; i++)
         r.CookIfNeeded(++frame);
      w = r.GetOutputWidth();
      h = r.GetOutputHeight();
      return w > 0 && h > 0 && GLUtil::ReadTexturePixels(scratch, r.GetOutputTexture(), w, h, px);
   };
   auto lumAt = [&](float u, float v) {
      const int x = std::clamp((int)(u * w), 0, w - 1), y = std::clamp((int)(v * h), 0, h - 1);
      const size_t i = ((size_t)y * w + x) * 4;
      return 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
   };

   r.projection = Render3DNode::kEquirect;
   r.width = 512.0f; r.height = 256.0f;
   if (!render())
   {
      printf("  [FAIL] no picture\nR3DPANOTEST FAIL\n");
      return false;
   }
   check(w == 512 && h == 256, "equirect output is the asked size", w, h);
   check(lumAt(0.5f, 0.5f) > 0.5 && lumAt(0.02f, 0.5f) < 0.2, "equirect: the cube is in the middle, behind is empty",
         lumAt(0.5f, 0.5f), lumAt(0.02f, 0.5f));

   r.projection = Render3DNode::kCubemap3x2;
   r.width = 768.0f; r.height = 512.0f;
   render();
   check(lumAt(0.5f, 0.25f) > 0.5 && lumAt(0.833f, 0.25f) < 0.2, "cubemap: the cube is in the front cell, the back cell is empty",
         lumAt(0.5f, 0.25f), lumAt(0.833f, 0.25f));

   r.projection = Render3DNode::kFisheye;
   r.width = 512.0f; r.height = 512.0f;
   r.domeTilt = 90.0f;
   render();
   check(lumAt(0.5f, 0.5f) > 0.5, "fisheye tilt 90: the cube is in the centre", lumAt(0.5f, 0.5f), 0.0);
   check(lumAt(0.01f, 0.01f) < 0.01, "fisheye: outside the dome is black", lumAt(0.01f, 0.01f), 0.0);
   r.domeTilt = 0.0f;
   r.domeAngle = 220.0f; // the horizon sits at 180/220 of the radius, inside the circle
   render();
   check(lumAt(0.5f, 0.5f) < 0.2 && lumAt(0.5f, 0.091f) > 0.5, "fisheye tilt 0, 220: centre is up, the cube on the horizon below",
         lumAt(0.5f, 0.5f), lumAt(0.5f, 0.091f));

   // Seams: two identical glowing spheres the same distance from the eye, one
   // straight ahead (the middle of a cube face), one 45 degrees right (exactly
   // on the edge between two faces). A wrong face orientation or a gap at the
   // edge would make the second one a different size or lopsided.
   GeometryNode ahead, onSeam;
   ahead.shape = onSeam.shape = 2; // sphere
   ahead.emission = onSeam.emission = 4.0f;
   {
      const float az = r.camAzimuth * 3.14159265f / 180.0f;
      const float eye[3] = { r.camDistance * std::cos(az), 0.0f, r.camDistance * std::sin(az) };
      const float fwd[3] = { -std::cos(az), 0.0f, -std::sin(az) };
      const float right[3] = { -fwd[2], 0.0f, fwd[0] };
      const float k = std::sqrt(0.5f);
      onSeam.posX = eye[0] + r.camDistance * k * (right[0] + fwd[0]);
      onSeam.posZ = eye[2] + r.camDistance * k * (right[2] + fwd[2]);
   }
   r.geometry[0] = &ahead;
   r.geometry[1] = &onSeam;
   r.projection = Render3DNode::kEquirect;
   r.width = 1024.0f; r.height = 512.0f;
   render();
   auto blob = [&](int cx, int& count, int& left, int& right, int& rows) {
      count = left = right = rows = 0;
      for (int y = 0; y < h; y++)
      {
         bool rowHit = false;
         for (int x = cx - 48; x <= cx + 48; x++)
         {
            const size_t i = ((size_t)y * w + ((x + w) % w)) * 4;
            if (0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2] > 0.5)
            {
               count++;
               rowHit = true;
               (x < cx ? left : right)++;
            }
         }
         rows += rowHit ? 1 : 0;
      }
   };
   int c0, l0, r0, h0, c1, l1, r1, h1;
   blob(w / 2, c0, l0, r0, h0);
   blob(w / 2 + w / 8, c1, l1, r1, h1);
   check(c0 > 50 && std::abs(c1 - c0) <= c0 / 10 && std::abs(h1 - h0) <= 2,
         "a sphere on a cube-face edge matches one in a face's middle (area)", c0, c1);
   check(std::abs(l1 - r1) <= c1 / 8, "the sphere on the edge is not lopsided", l1, r1);
   r.geometry[1] = nullptr;
   r.geometry[0] = &geo;

   r.projection = Render3DNode::kPerspective;
   r.width = 320.0f; r.height = 200.0f;
   render();
   check(w == 320 && h == 200, "back to perspective: the plain output again", w, h);

   const GLenum err = glGetError();
   check(err == GL_NO_ERROR, "no GL error", (double)err, 0.0);
   r.geometry[0] = nullptr;
   if (scratch != 0)
      glDeleteFramebuffers(1, &scratch);
   printf(failures == 0 ? "R3DPANOTEST OK\n" : "R3DPANOTEST FAIL (%d)\n", failures);
   return failures == 0;
}
}
