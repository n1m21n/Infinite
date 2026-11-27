// PhotoCraft ledger parity checks (rows re-opened by tools/parity/na_recheck.py, 2026-11-27).
// Run alone with INFINITE_VIEW2DTEST_ONLY=parity (or a narrower tag, e.g. parity.tip); the full VIEW2DTEST runs them too.
// Expected numbers are PhotoCraft's Photoshop measurements (MIT/Apache-2.0), cited per check.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "app/AppShared.h"
#include "nodes/Scene2DNode.h"
#include "core/BrushEngine.h"
#include "core/GLUtil.h"
#include "gl3.h"

namespace app
{
namespace
{
   struct Section
   {
      const char* only = "";
      int fails = 0;
      bool Want(const char* tag) const { return only[0] == '\0' || std::strncmp(tag, only, std::strlen(only)) == 0; }
      void Check(const std::string& label, bool pass)
      {
         printf("  [%s] %s\n", pass ? "pass" : "FAIL", label.c_str());
         fails += pass ? 0 : 1;
      }
   };

   std::vector<uint8_t> ReadSurface(const GLUtil::Fbo& f)
   {
      std::vector<uint8_t> px((size_t)f.w * f.h * 4);
      GLint prev = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, f.fbo);
      glReadPixels(0, 0, f.w, f.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev);
      return px;
   }

   void Paint(GLUtil::Fbo& surf, const BrushEngine::Brush& b, const std::vector<std::pair<float, float>>& pts)
   {
      BrushEngine::Stroke st;
      int dirty[4];
      st.Begin(surf, b, pts[0].first, pts[0].second, 1.0f);
      for (size_t i = 1; i < pts.size(); i++)
         st.MoveTo(pts[i].first, pts[i].second, 1.0f);
      st.Flush();
      st.End(surf, dirty);
   }

   // brush_tests.rs the_tip_matches_photoshops_measured_dabs: single 40 px dabs, mean alpha over rings 0.1 R wide.
   void TipRings(Section& s)
   {
      const int S = 96;
      GLUtil::Fbo surf;
      GLUtil::EnsureFbo(surf, S, S, GL_RGBA8, "ParityTipRings");
      struct Ring { float lo, want; };
      const struct { float h; Ring r[4]; } measured[3] = {
         { 0.0f, { { 0.2f, 0.861f }, { 0.5f, 0.509f }, { 0.9f, 0.129f }, { 1.2f, 0.027f } } },
         { 0.5f, { { 0.6f, 0.97f }, { 0.8f, 0.561f }, { 1.0f, 0.135f }, { 1.2f, 0.012f } } },
         { 1.0f, { { 0.8f, 1.0f }, { 0.9f, 0.821f }, { 1.2f, 0.0f }, { 1.3f, 0.0f } } },
      };
      for (const auto& m : measured)
      {
         GLUtil::ClearFbo(surf);
         BrushEngine::Brush b;
         b.size = 40.0f;
         b.hardness = m.h;
         b.opacity = b.flow = 1.0f;
         b.pressureOpacity = b.pressureSize = false;
         Paint(surf, b, { { 48.0f, 48.0f } });
         const std::vector<uint8_t> px = ReadSurface(surf);
         // The centre from the alpha itself, so the check does not depend on the pixel-centre convention.
         double sx = 0, sy = 0, sw = 0;
         for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++)
            {
               const double a = px[((size_t)y * S + x) * 4 + 3];
               sx += a * (x + 0.5), sy += a * (y + 0.5), sw += a;
            }
         const double cx = sw > 0 ? sx / sw : 48, cy = sw > 0 ? sy / sw : 48;
         std::string got;
         bool pass = sw > 0;
         for (const Ring& r : m.r)
         {
            double sum = 0, n = 0;
            for (int y = 0; y < S; y++)
               for (int x = 0; x < S; x++)
               {
                  const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy) / 20.0;
                  if (d >= r.lo && d < r.lo + 0.1)
                     sum += px[((size_t)y * S + x) * 4 + 3] / 255.0, n++;
               }
            const double v = n > 0 ? sum / n : -1;
            pass = pass && std::fabs(v - r.want) < 0.05;
            char buf[48];
            std::snprintf(buf, sizeof buf, " %.1fR %.3f/%.3f", r.lo, v, r.want);
            got += buf;
         }
         printf("  parity tip h%.1f:%s\n", m.h, got.c_str());
         char label[96];
         std::snprintf(label, sizeof label, "Parity: a hardness %.1f round tip matches Photoshop's measured dab", m.h);
         s.Check(label, pass);
      }
      GLUtil::DestroyFbo(surf);
   }

   // bg_erase_decontamination_tests.rs: eight blocks (200, 40 + 20 i, 40), a hard drag sampled from block 0, Tolerance 50.
   // Half size here (40 px blocks, 20 px tip): one hard pass covers each block centre fully either way.
   void BgErase(Section& s)
   {
      const int B = 40, W = B * 8, H = 60;
      GLUtil::Fbo surf;
      GLUtil::EnsureFbo(surf, W, H, GL_RGBA8, "ParityBgErase");
      int odd = 0;   // 1: block 1 is (220, 60, 40), both up; block 2 (220, 20, 40), one up one down
      auto fill = [&]() {
         std::vector<uint8_t> px((size_t)W * H * 4);
         for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
               uint8_t* p = &px[((size_t)y * W + x) * 4];
               p[0] = 200, p[1] = (uint8_t)(40 + 20 * (x / B)), p[2] = 40, p[3] = 255;
               if (odd == 1 && (x / B == 1 || x / B == 2))
                  p[0] = 220, p[1] = x / B == 1 ? 60 : 20;
            }
         glBindTexture(GL_TEXTURE_2D, surf.tex);
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindTexture(GL_TEXTURE_2D, 0);
      };
      auto drag = [&](float tol) {
         fill();
         BrushEngine::Brush b;
         b.mode = BrushEngine::kBgErase;
         b.size = 20.0f;
         b.hardness = 1.0f;
         b.opacity = b.flow = 1.0f;
         b.pressureOpacity = b.pressureSize = false;
         b.key[0] = 200.0f / 255.0f, b.key[1] = 40.0f / 255.0f, b.key[2] = 40.0f / 255.0f;
         b.keyTolerance = tol;
         std::vector<std::pair<float, float>> pts;
         for (float x = B * 0.5f; x <= B * 7.5f; x += 2.0f)
            pts.push_back({ x, H * 0.5f });
         Paint(surf, b, pts);
         return ReadSurface(surf);
      };
      auto at = [&](const std::vector<uint8_t>& px, int i, int c) { return (int)px[((size_t)(H / 2) * W + B * i + B / 2) * 4 + c]; };
      {
         const int want[8][4] = { { 200, 40, 40, 0 }, { 200, 216, 40, 29 }, { 200, 184, 40, 71 }, { 200, 175, 40, 113 },
                                  { 200, 172, 40, 155 }, { 200, 170, 40, 196 }, { 200, 169, 40, 238 }, { 200, 180, 40, 255 } };
         const std::vector<uint8_t> px = drag(0.5f);
         bool pass = true;
         std::string got;
         for (int i = 0; i < 8; i++)
         {
            for (int c = want[i][3] == 0 ? 3 : 0; c < 4; c++)
               pass = pass && std::abs(at(px, i, c) - want[i][c]) <= 8;
            got += " " + std::to_string(at(px, i, 1)) + "/" + std::to_string(at(px, i, 3));
         }
         printf("  parity bg erase T50 g/a:%s\n", got.c_str());
         s.Check("Parity: Background Eraser at Tolerance 50 gives Photoshop's alpha and decontaminated colour per block", pass);
         // Composited over the sampled colour, each partly erased block gives back its original colour (2 levels).
         bool back = true;
         for (int i = 1; i < 7; i++)
         {
            const int a = at(px, i, 3);
            const float g = (at(px, i, 1) * a + 40 * (255 - a)) / 255.0f;
            back = back && a > 0 && a < 255 && std::fabs(g - (40 + 20 * i)) <= 2.0f;
         }
         s.Check("Parity: a partly erased pixel over the sampled colour gives back its original colour", back);
      }
      {
         const struct { float tol; int a[8]; } rows[4] = {
            { 0.1f, { 0, 194, 255, 255, 255, 255, 255, 255 } }, { 0.2f, { 0, 90, 197, 255, 255, 255, 255, 255 } },
            { 0.3f, { 0, 56, 126, 196, 255, 255, 255, 255 } }, { 0.7f, { 0, 18, 48, 78, 107, 137, 167, 197 } } };
         bool pass = true;
         for (const auto& r : rows)
         {
            const std::vector<uint8_t> px = drag(r.tol);
            std::string got;
            for (int i = 0; i < 8; i++)
            {
               pass = pass && std::abs(at(px, i, 3) - r.a[i]) <= 8;
               got += " " + std::to_string(at(px, i, 3));
            }
            printf("  parity bg erase T%.0f a:%s\n", r.tol * 100, got.c_str());
         }
         s.Check("Parity: Background Eraser alpha follows Photoshop's drag table at Tolerance 10 to 70", pass);
      }
      {
         // bg_erase_graded_alpha_tests.rs same_sign_differences_count_once_and_opposite_signs_add_up: +20 +20 is as far as
         // +20 alone (block 1 of the table, alpha 29); +20 -20 adds up to 40 (block 2, alpha 71).
         odd = 1;
         const std::vector<uint8_t> px = drag(0.5f);
         odd = 0;
         printf("  parity bg erase signs a: %d %d\n", at(px, 1, 3), at(px, 2, 3));
         s.Check("Parity: Background Eraser counts same-sign channel changes once and adds opposite ones",
                 std::abs(at(px, 1, 3) - 29) <= 8 && std::abs(at(px, 2, 3) - 71) <= 8);
      }
      {
         // Protect Foreground: the foreground's own colour is kept, the sampled colour still goes.
         fill();
         BrushEngine::Brush b;
         b.mode = BrushEngine::kBgErase;
         b.size = 20.0f;
         b.hardness = 1.0f;
         b.opacity = b.flow = 1.0f;
         b.pressureOpacity = b.pressureSize = false;
         b.key[0] = 200.0f / 255.0f, b.key[1] = 40.0f / 255.0f, b.key[2] = 40.0f / 255.0f;
         b.color[0] = 200.0f / 255.0f, b.color[1] = 100.0f / 255.0f, b.color[2] = 40.0f / 255.0f;
         b.keyTolerance = 1.0f;
         b.protectFg = true;
         std::vector<std::pair<float, float>> pts;
         for (float x = B * 0.5f; x <= B * 7.5f; x += 2.0f)
            pts.push_back({ x, H * 0.5f });
         Paint(surf, b, pts);
         const std::vector<uint8_t> px = ReadSurface(surf);
         s.Check("Parity: Protect Foreground keeps the foreground colour and what lies past it",
                 at(px, 0, 3) == 0 && at(px, 3, 3) == 255 && at(px, 5, 3) == 255 && at(px, 1, 3) < 255);
      }
      GLUtil::DestroyFbo(surf);
   }
   // tile_damage_tests.rs: an effect reaches past the pixels that changed. A dab on a layer with a drop shadow shows
   // its shadow outside the dab and the layer's old content, the rest of the shadow stays; a dab inside a group with a
   // drop shadow moves the group's shadow too. Infinite recomposites the stack every cook, so the check is the picture.
   void FxBounds(Section& s)
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 400;
      pic.Doc().h = 300;
      pic.Doc().background = 1;   // black, so a white shadow reads directly
      pic.Commit();
      auto Px = [&](int x, int y) {
         const int w = pic.GetOutputWidth(), h = pic.GetOutputHeight();
         std::vector<float> px((size_t)w * h * 4);
         glBindTexture(GL_TEXTURE_2D, pic.GetOutputTexture());
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         const size_t at = ((size_t)(h - 1 - y) * w + x) * 4;
         return std::vector<float>{ px[at], px[at + 1], px[at + 2], px[at + 3] };
      };
      static int frame = 900000;
      auto Cook = [&]() { pic.CookIfNeeded(++frame); };
      BrushEngine::Brush b = BrushEngine::Presets()[5].brush;   // Hard round
      b.size = 20.0f;
      b.pressureSize = b.pressureOpacity = false;
      b.color[0] = b.color[1] = 0.0f;
      b.color[2] = 1.0f;
      auto Dab = [&](int id, float x, float y) {
         pic.BeginPaint(id, 0, b, x, y, 1.0f);
         pic.EndPaint();
      };
      auto Shadow = [](Scene2DNode::Fx& f) {
         f.on = true;
         f.angle = 90.0f;   // light from the top: the shadow falls 20 px down
         f.dist = 20.0f;
         f.size = 0.0f;
         f.c[0] = f.c[1] = f.c[2] = 1.0f;
         f.c[3] = 1.0f;
      };
      const int a = pic.AddPaintLayer();
      Shadow(pic.FindLayer(a)->fx[Scene2DNode::kFxShadow]);
      pic.Commit();
      Dab(a, 100.0f, 100.0f);
      Cook();
      const std::vector<float> first = Px(100, 125);
      Dab(a, 300.0f, 60.0f);
      Cook();
      const std::vector<float> dab = Px(300, 60), shadow = Px(300, 85), old = Px(100, 125), clear = Px(200, 200);
      printf("  parity fx: dab b %.2f, its shadow %.2f (outside the dab), first shadow %.2f -> %.2f, clear %.2f\n", dab[2],
             shadow[0], first[0], old[0], clear[0]);
      s.Check("parity: a dab on a layer with a drop shadow draws the shadow past the dab, the old shadow stays",
              dab[2] > 0.9f && dab[0] < 0.1f && shadow[0] > 0.9f && first[0] > 0.9f && old[0] > 0.9f && clear[0] < 0.1f);

      // The group's shadow of a child dab: the child has no fx of its own.
      const int c = pic.AddPaintLayer();
      const int g = pic.GroupLayers({ c });
      Scene2DNode::Layer* gl = pic.FindLayer(g);
      bool grouped = gl != nullptr;
      if (grouped)
      {
         Shadow(gl->fx[Scene2DNode::kFxShadow]);
         pic.Commit();
         Dab(c, 200.0f, 230.0f);
         Cook();
      }
      const std::vector<float> gd = Px(200, 230), gs = Px(200, 255);
      printf("  parity fx: group child dab b %.2f, group shadow %.2f\n", gd[2], gs[0]);
      s.Check("parity: a dab inside a group with a drop shadow draws the group's shadow", grouped && gd[2] > 0.9f && gs[0] > 0.9f);
   }

   // compose/src/fill_layout.rs: Gradient Fill end points snap to whole pixels, Scale and Offset move them, and the
   // other styles and bad input pass through. GradT mirrors the fill shader's Linear / Reflected t at a pixel centre.
   void GradientFill(Section& s)
   {
      using SN = Scene2DNode;
      auto Lay = [](bool lin, bool refl, float a, float sc, float ox, float oy, int w, int h) {
         std::array<float, 4> r { a, sc, ox, oy };
         SN::GradientLayout(lin, refl, r[0], r[1], r[2], r[3], w, h);
         return r;
      };
      auto GradT = [](bool refl, const std::array<float, 4>& L, int w, int h, int x, int y) {
         const float a = L[0] * 3.14159265f / 180.0f, c = std::cos(a), sn = std::sin(a);
         const float pdx = (float)x + 0.5f - (float)w * (0.5f + L[2]), pdy = (float)y + 0.5f - (float)h * (0.5f + L[3]);
         const float along = pdx * c - pdy * sn - 0.5f * (c - sn);
         const float chord = std::max(std::min((float)w / std::max(std::fabs(c), 1e-6f), (float)h / std::max(std::fabs(sn), 1e-6f)), 1.0f) *
                             std::max(L[1], 1e-3f);
         return std::clamp(refl ? std::fabs(along / (chord * 0.5f)) : along / chord + 0.5f, 0.0f, 1.0f);
      };
      bool corner = true;
      const auto R = Lay(false, true, 30.0f, 1.0f, 0.0f, 0.0f, 4, 4);
      for (int y = 0; y < 4; ++y)
         for (int x = 0; x < 4; ++x)
            corner = corner && std::fabs(GradT(true, R, 4, 4, x, y) - std::fabs((float)(x - y)) / 4.0f) < 1e-4f;
      s.Check("parity: a small 30 degree Reflected fill snaps to the corner (t = |x - y| / 4 on 4 x 4)", corner);
      const auto a0 = Lay(true, false, 30.0f, 0.5f, 0.0f, 0.0f, 40, 20), a1 = Lay(true, false, 30.0f, 0.5f, 0.25f, -0.1f, 40, 20);
      s.Check("parity: a Gradient Fill offset moves the snapped layout by the same share",
              std::fabs(a0[0] - a1[0]) < 1e-4f && std::fabs(a0[1] - a1[1]) < 1e-4f && std::fabs(a1[2] - a0[2] - 0.25f) < 1e-4f &&
                 std::fabs(a1[3] - a0[3] + 0.1f) < 1e-4f);
      const auto big = Lay(true, false, 30.0f, 1.0f, 0.0f, 0.0f, 3000, 2000);
      const auto rad = Lay(false, false, 30.0f, 1.0f, 0.2f, 0.1f, 40, 20);
      const auto bad = Lay(true, false, 30.0f, 1.0f, std::nanf(""), 0.0f, 40, 20);
      s.Check("parity: large frames barely move, Radial and a bad offset pass through",
              std::fabs(big[0] - 30.0f) < 0.05f && std::fabs(big[1] - 1.0f) < 1e-3f && rad == std::array<float, 4> { 30.0f, 1.0f, 0.2f, 0.1f } &&
                 std::isnan(bad[2]));
      // Save and load keep Scale and Offset.
      SN d;
      d.Layers();
      SN::Layer& l0 = d.AddLayer(SN::Kind::Fill, 0);
      l0.fillType = 1;
      const int id = l0.id;
      bool saved = false;
      if (SN::Layer* l = d.FindLayer(id))
      {
         l->fillScale = 0.5f;
         l->fillOff[0] = 0.25f;
         l->fillOff[1] = -0.1f;
         d.Commit();
         SN e;
         e.SetDocText(d.DocText());
         const SN::Layer* m = e.FindLayer(id);
         saved = m != nullptr && std::fabs(m->fillScale - 0.5f) < 1e-4f && std::fabs(m->fillOff[0] - 0.25f) < 1e-4f &&
                 std::fabs(m->fillOff[1] + 0.1f) < 1e-4f;
      }
      s.Check("parity: Gradient Fill Scale and Offset save and load", saved);
   }

   // layer_copy_cmds/tests.rs + layers_panel_parity_tests.rs + smartselect_cmds/tests.rs: copying layers between open
   // documents, Duplicate To a new document, and Select and Mask's new-document outputs.
   void CrossDoc(Section& s)
   {
      using SN = Scene2DNode;
      auto Doc = [](SN& d, int w, int h, float dpi) {
         d.Layers();
         d.Doc().w = w;
         d.Doc().h = h;
         d.Doc().dpi = dpi;
         d.Commit();
      };
      auto Count = [](SN& d) { return (int)d.Layers().size(); };
      auto Top = [](SN& d) {
         int n = 0;
         for (const SN::Layer& l : d.Layers())
            n += l.parent == 0 ? 1 : 0;
         return n;
      };
      std::vector<uint8_t> red((size_t)8 * 8 * 4);
      for (size_t i = 0; i < red.size(); i += 4)
         red[i] = 255, red[i + 3] = 255;

      // Above the destination's active layer, one copy per selected layer, same place; the source is untouched.
      {
         SN a, b;
         Doc(a, 64, 48, 72.0f);
         Doc(b, 64, 48, 72.0f);
         const int a1 = a.AddFileLayer(0, "one", 4, 4, 8, 8, red), a2 = a.AddFileLayer(0, "two", 20, 4, 8, 8, red);
         const int b1 = b.AddPaintLayer(), b2 = b.AddPaintLayer();
         const std::string before = a.DocText();
         const std::vector<int> made = b.CopyLayersFrom(a, { a1, a2 }, b1);
         const auto& L = b.Layers();
         int at1 = -1, atB2 = -1, atM0 = -1, atM1 = -1;
         for (int i = 0; i < (int)L.size(); i++)
         {
            at1 = L[i].id == b1 ? i : at1;
            atB2 = L[i].id == b2 ? i : atB2;
            atM0 = made.size() == 2 && L[i].id == made[0] ? i : atM0;
            atM1 = made.size() == 2 && L[i].id == made[1] ? i : atM1;
         }
         const SN::Layer* m0 = made.size() == 2 ? b.FindLayer(made[0]) : nullptr;
         const bool order = at1 >= 0 && atM0 == at1 + 1 && atM1 == at1 + 2 && atB2 == at1 + 3;
         const bool place = m0 != nullptr && m0->name == "one" && m0->x == a.FindLayer(a1)->x && m0->y == a.FindLayer(a1)->y;
         s.Check("parity: copying layers to another document puts them above its active layer, in order, at the same place",
                 order && place && a.DocText() == before);
      }

      // Link groups: two source groups stay two distinct groups, and neither joins the destination's own group.
      {
         SN a, b;
         Doc(a, 16, 16, 72.0f);
         Doc(b, 16, 16, 72.0f);
         const int a1 = a.AddPaintLayer(), a2 = a.AddPaintLayer();
         a.FindLayer(a1)->link = 1;
         a.FindLayer(a2)->link = 2;
         const int b1 = b.AddPaintLayer(), b2 = b.AddPaintLayer();
         b.FindLayer(b1)->link = 1;
         b.FindLayer(b2)->link = 1;
         b.Commit();
         const std::vector<int> made = b.CopyLayersFrom(a, { a1, a2 }, 0);
         const uint64_t l0 = made.size() == 2 ? b.FindLayer(made[0])->link : 0, l1 = made.size() == 2 ? b.FindLayer(made[1])->link : 0;
         s.Check("parity: copied link groups stay distinct from each other and from the destination's groups",
                 l0 != 0 && l1 != 0 && l0 != l1 && l0 != 1 && l1 != 1 && b.LinkedWith({ b1 }).size() == 2);
         const std::vector<int> again = b.CopyLayersFrom(a, { a1 }, 0);
         s.Check("parity: copying a linked layer does not join an unrelated destination group",
                 again.size() == 1 && b.FindLayer(again[0])->link != 1 && b.FindLayer(again[0])->link != l0 &&
                     b.LinkedWith({ b1 }).size() == 2);
      }

      // Group depth: a copied group that would nest past the cap is refused, and nothing is copied.
      {
         SN a, b;
         Doc(a, 16, 16, 72.0f);
         Doc(b, 16, 16, 72.0f);
         const int g = a.GroupLayers({ a.AddPaintLayer() });
         int inner = b.AddPaintLayer();
         const int deep = inner;
         for (int i = 0; i < SN::kMaxGroupDepth && inner != 0; i++)
            inner = b.GroupLayers({ inner });
         const std::string before = b.DocText();
         std::string err;
         const std::vector<int> made = b.CopyLayersFrom(a, { g }, deep, &err);
         s.Check("parity: a copied group that would nest the destination past the group depth limit is refused, nothing copied",
                 made.empty() && err.find("deeper than") != std::string::npos && b.DocText() == before);
      }

      // Type: pixel size kept whatever the two resolutions (Infinite stores px; points follow the dpi), styles by name.
      {
         SN a, b;
         Doc(a, 200, 100, 72.0f);
         Doc(b, 200, 100, 300.0f);
         SN::TextStyles& as = a.Doc().textStyles;
         as.character.push_back({ 1, "Character Style 1", { { "size", "24" }, { "underline", "1" } } });
         as.paragraph.push_back({ 1, "Paragraph Style 1", { { "leading", "1.5" } }, {} });
         as.basic.chr["tracking"] = "40";
         const int t = a.AddTextLayer("Hi", 10, 10, 0);
         a.ApplyParaStyle(t, 1, true);
         a.ApplyCharStyle(t, 0, 2, 1, true);
         a.Commit();
         const float px = a.FindLayer(t)->runs.empty() ? 0.0f : a.FindLayer(t)->runs[0].st.size;
         // The destination already has a "Character Style 1" (a different look) behind another style.
         SN::TextStyles& bs = b.Doc().textStyles;
         bs.character.push_back({ 1, "Filler", {} });
         bs.character.push_back({ 2, "Character Style 1", { { "size", "12" } } });
         const std::vector<int> made = b.CopyLayersFrom(a, { t }, 0);
         const SN::Layer* c = made.size() == 1 ? b.FindLayer(made[0]) : nullptr;
         const bool pxKept = c != nullptr && !c->runs.empty() && std::fabs(c->runs[0].st.size - px) < 1e-3f && px > 0.0f;
         s.Check("parity: copied type keeps its pixel size between documents of different resolutions", pxKept);
         s.Check("parity: copied type uses the destination's same-named character style, not a second one",
                 c != nullptr && !c->runs.empty() && c->runs[0].st.charStyle == 2 && bs.character.size() == 2);
         s.Check("parity: a paragraph style the destination lacks comes along under its name",
                 c != nullptr && b.Doc().textStyles.Para(c->paraStyle) != nullptr &&
                     b.Doc().textStyles.Para(c->paraStyle)->name == "Paragraph Style 1");
         // Pasted (copy, then paste in the other document) keeps the same pixels too.
         SN d;
         Doc(d, 200, 100, 150.0f);
         const std::vector<int> pasted = d.PasteLayers(a.CopyLayers({ t }), 0, 0.0f, 0.0f);
         s.Check("parity: pasted type keeps its pixel size in a document of another resolution",
                 pasted.size() == 1 && !d.FindLayer(pasted[0])->runs.empty() && std::fabs(d.FindLayer(pasted[0])->runs[0].st.size - px) < 1e-3f);

         // Duplicate To a new document: the styles and the source's Basic Paragraph come along, runs unchanged.
         SN n;
         n.Layers();
         const std::vector<int> nd = n.TakeLayersAsNewDocument(a, { t }, "", "");
         const SN::Layer* nl = nd.size() == 1 ? n.FindLayer(nd[0]) : nullptr;
         const SN::TextStyles& ns = n.Doc().textStyles;
         s.Check("parity: type duplicated to a new document keeps its styles and runs",
                 nl != nullptr && nl->runs == a.FindLayer(t)->runs && ns.Char(nl->runs[0].st.charStyle) != nullptr &&
                     ns.Char(nl->runs[0].st.charStyle)->name == "Character Style 1" && ns.Para(nl->paraStyle) != nullptr);
         s.Check("parity: type duplicated to a new document keeps the source's Basic Paragraph",
                 ns.basic == as.basic && n.Doc().dpi == 72.0f && n.Doc().w == 200);
      }

      // Duplicate To a new document: every selected layer in order, with the copy's and the document's names.
      {
         SN a;
         Doc(a, 32, 32, 96.0f);
         const int a1 = a.AddFileLayer(0, "one", 0, 0, 8, 8, red), a2 = a.AddFileLayer(0, "two", 8, 8, 8, 8, red);
         const int a3 = a.AddFileLayer(0, "three", 16, 16, 8, 8, red);
         SN n;
         n.Layers();
         n.AddPaintLayer();   // a new document's own starting layer goes
         const std::vector<int> made = n.TakeLayersAsNewDocument(a, { a3, a1, a2 }, "", "");
         const auto& L = n.Layers();
         const bool order = Count(n) == 3 && L[0].name == "one" && L[1].name == "two" && L[2].name == "three" && made.size() == 3;
         s.Check("parity: Duplicate To a new document carries every selected layer, in stack order",
                 order && n.Doc().w == 32 && n.Doc().dpi == 96.0f);
         SN m;
         m.Layers();
         const std::vector<int> one = m.TakeLayersAsNewDocument(a, { a1 }, "Ink", "Card");
         SN r;
         r.SetDocText(m.DocText());
         s.Check("parity: Duplicate To a new document names the copy and the document (and the name saves)",
                 one.size() == 1 && m.FindLayer(one[0])->name == "Ink" && m.Doc().name == "Card" && r.Doc().name == "Card" &&
                     Top(m) == 1);
      }

      // Select and Mask > New Document / New Document with Layer Mask.
      {
         SN a;
         Doc(a, 40, 30, 300.0f);
         std::vector<uint8_t> full((size_t)40 * 30 * 4);
         for (size_t i = 0; i < full.size(); i += 4)
            full[i] = 200, full[i + 3] = 255;
         const int src = a.AddFileLayer(0, "photo", 0, 0, 40, 30, full);
         a.SelectRect(0, 0, 20, 30, SN::kSelNew, 0.0f, false);
         const std::string before = a.DocText();
         const std::vector<uint8_t> sel = a.SelectionPixels();
         SN n;
         n.Layers();
         const int id = n.RefineIntoNewDocument(a, src, true);
         const SN::Layer* l = id != 0 ? n.FindLayer(id) : nullptr;
         const bool masked = l != nullptr && l->HasPixelMask() && !n.HasSelection() && Count(n) == 1 && n.Doc().w == 40 &&
                             n.Doc().h == 30 && n.Doc().dpi == 300.0f;
         SN m;
         m.Layers();
         const int id2 = m.RefineIntoNewDocument(a, src, false);
         const SN::Layer* l2 = id2 != 0 ? m.FindLayer(id2) : nullptr;
         const bool baked = l2 != nullptr && !l2->HasPixelMask() && !m.HasSelection() && Count(m) == 1;
         printf("  parity crossdoc refine: masked %d baked %d\n", (int)masked, (int)baked);
         s.Check("parity: Select and Mask outputs to a new document (with a layer mask, or baked), the source untouched",
                 masked && baked && a.DocText() == before && a.SelectionPixels() == sel);
      }
   }

}

int View2DPanelParityTests(Scene2DNode* doc, const char* only);   // View2DPanel.cpp

// Runs the parity checks whose tag starts with `only` ("" = all); returns the failures.
int RunView2DParitySection(Scene2DNode* doc, const char* only)
{
   Section s;
   s.only = only != nullptr ? only : "";
   if (s.Want("parity.tip"))
      TipRings(s);
   if (s.Want("parity.fx"))
      FxBounds(s);
   if (s.Want("parity.bgerase"))
      BgErase(s);
   if (s.Want("parity.crossdoc"))
      CrossDoc(s);
   if (s.Want("parity.gradient"))
      GradientFill(s);
   return s.fails + View2DPanelParityTests(doc, s.only);
}
}
