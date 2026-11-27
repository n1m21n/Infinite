// Scene 2D paint: paint-layer surfaces and their tiles, pixel masks, the
// document selection, bucket, gradient and pick. Plan: internal/plans/2d-view (Block B).
#include "Scene2DNode.h"
#include "ColorSpace.h"
#include "ArtHistory.h"
#include "GLUtil.h"

#include "gl3.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <set>

#include "TileStore.h"
#include "core/ml/AiSelect.h"
#include "platform/Platform.h"
#include <chrono>

namespace
{
   constexpr int kT = TileStore::kTile;

   int Cols(int w) { return (w + kT - 1) / kT; }

   // Maps each texel of a surface to the document and either paints a fill
   // (solid or gradient, through a coverage and the selection) over the
   // surface (uOp 0), or writes the selection there as grey (uOp 1).
   const char* kFillFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSurf;\n"
      "uniform sampler2D uCov;\n"
      "uniform sampler2D uSel;\n"
      "uniform int uHasCov;\n"
      "uniform int uHasSel;\n"
      "uniform int uOp;\n"
      "uniform vec2 uSurfSize;\n"
      "uniform vec2 uBox;\n"
      "uniform vec2 uDoc;\n"
      "uniform vec3 uMA;\n"
      "uniform vec3 uMB;\n"
      "uniform vec4 uColA;\n"
      "uniform vec4 uColB;\n"
      "uniform int uKind;\n"
      "uniform vec2 uP0;\n"
      "uniform vec2 uP1;\n"
      "uniform float uOpacity;\n"
      "uniform int uLock;\n"
      "uniform int uExtend;\n"
      "uniform int uDither;\n"
      "uniform int uMethod;\n"
      // Smooth: Photoshop's measured Classic black-to-white curve every tenth, and Foreground-to-Transparent's opacity
      // every quarter (PhotoCraft paint_gradient_sample_tests / gradient_alpha_method_tests, Apache 2.0).
      "const float kSmoothC[11] = float[11](0.0, 2.0, 17.0, 42.0, 70.0, 99.0, 129.0, 162.0, 196.0, 228.0, 255.0);\n"
      "const float kSmoothA[5] = float[5](0.0, 76.0, 155.0, 220.0, 255.0);\n"
      "float SmoothC(float t) { float f = clamp(t, 0.0, 1.0) * 10.0; int i = min(int(f), 9); return mix(kSmoothC[i], kSmoothC[i + 1], f - float(i)) / 255.0; }\n"
      "float SmoothA(float t) { float f = clamp(t, 0.0, 1.0) * 4.0; int i = min(int(f), 3); return mix(kSmoothA[i], kSmoothA[i + 1], f - float(i)) / 255.0; }\n"
      "void main() {\n"
      "   vec2 sp = vUv * uSurfSize;\n"
      "   vec2 bp = vec2(sp.x * uBox.x / uSurfSize.x, (uSurfSize.y - sp.y) * uBox.y / uSurfSize.y);\n"
      "   vec2 dp = vec2(dot(uMA, vec3(bp, 1.0)), dot(uMB, vec3(bp, 1.0)));\n"
      "   vec2 duv = vec2(dp.x / uDoc.x, 1.0 - dp.y / uDoc.y);\n"
      "   bool inDoc = duv.x >= 0.0 && duv.y >= 0.0 && duv.x <= 1.0 && duv.y <= 1.0;\n"
      "   float sel = uHasSel == 1 ? (inDoc ? texture(uSel, duv).r : 0.0) : 1.0;\n"
      "   if (uOp == 1) { fragColor = vec4(vec3(sel), 1.0); return; }\n"
      "   if (uOp == 2) { fragColor = inDoc ? texture(uSel, duv) : vec4(0.0); return; }\n"   // the picture, onto this surface
      "   vec4 d = texture(uSurf, vUv);\n"
      "   float cov = uHasCov == 1 ? (inDoc ? texture(uCov, duv).r : 0.0) : 1.0;\n"
      "   vec4 c = uColA;\n"
      "   float t = -1.0;\n"
      "   if (uKind == 1) {\n"
      "      vec2 ab = uP1 - uP0;\n"
      "      t = dot(dp - uP0, ab) / max(dot(ab, ab), 1e-6);\n"
      "   } else if (uKind == 2) {\n"
      "      t = length(dp - uP0) / max(length(uP1 - uP0), 1e-3);\n"
      "   } else if (uKind == 3) {\n"   // angle: once round from the drag's direction
      "      vec2 ab = uP1 - uP0, q = dp - uP0;\n"
      "      t = fract((atan(q.y, q.x) - atan(ab.y, ab.x)) / 6.2831853 + 1.0);\n"
      "   } else if (uKind == 4) {\n"   // reflected: the linear ramp mirrored back across the start
      "      vec2 ab = uP1 - uP0;\n"
      "      t = abs(dot(dp - uP0, ab)) / max(dot(ab, ab), 1e-6);\n"
      "   } else if (uKind == 5) {\n"   // diamond: a square turned to the drag, its corner at the end
      "      vec2 ab = uP1 - uP0, e = vec2(-ab.y, ab.x), q = dp - uP0;\n"
      "      t = (abs(dot(q, ab)) + abs(dot(q, e))) / max(dot(ab, ab), 1e-6);\n"
      "   }\n"
      "   if (uKind >= 1 && uKind <= 5) {\n"
      "      if (uExtend == 1) t = fract(t);\n"                       // repeat
      "      else if (uExtend == 2) t = 1.0 - abs(mod(t, 2.0) - 1.0);\n"   // reflect
      "      c = mix(uColA, uColB, clamp(t, 0.0, 1.0));\n"
      "      if (uMethod == 1) c = vec4(mix(uColA.rgb, uColB.rgb, SmoothC(t)), mix(uColA.a, uColB.a, SmoothA(t)));\n"
      "      if (uDither == 1) c.rgb += (fract(sin(dot(sp, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;\n"
      "   }\n"
      "   float a = clamp(c.a * cov * sel * uOpacity, 0.0, 1.0);\n"
      "   if (uOp == 3) { fragColor = vec4(d.rgb, d.a * (1.0 - clamp(cov * sel * uOpacity, 0.0, 1.0))); return; }\n"   // clear
      "   if (uLock == 1) { fragColor = vec4(mix(d.rgb, c.rgb, a), d.a); return; }\n"
      "   float oa = a + d.a * (1.0 - a);\n"
      "   fragColor = vec4((c.rgb * a + d.rgb * d.a * (1.0 - a)) / max(oa, 1e-4), oa);\n"
      "}\n";

   // Marching ants on the selection's edge, or the quick mask's red tint.
   const char* kAntsFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSel;\n"
      "uniform vec2 uPx;\n"
      "uniform float uTime;\n"
      "uniform int uQuick;\n"
      "void main() {\n"
      "   float v = texture(uSel, vUv).r;\n"
      "   if (uQuick == 1) { fragColor = vec4(1.0, 0.0, 0.0, (1.0 - v) * 0.5); return; }\n"
      "   bool inside = v >= 0.5;\n"
      "   bool edge = false;\n"
      "   for (int i = 0; i < 4; i++) {\n"
      "      vec2 o = i == 0 ? vec2(uPx.x, 0.0) : i == 1 ? vec2(-uPx.x, 0.0) : i == 2 ? vec2(0.0, uPx.y) : vec2(0.0, -uPx.y);\n"
      "      edge = edge || ((texture(uSel, vUv + o).r >= 0.5) != inside);\n"
      "   }\n"
      "   if (!edge) { fragColor = vec4(0.0); return; }\n"
      "   float s = mod(floor((gl_FragCoord.x + gl_FragCoord.y) / 4.0 + uTime * 8.0), 2.0);\n"
      "   fragColor = vec4(vec3(s), 1.0);\n"
      "}\n";

   void ReadRect(const GLUtil::Fbo& f, int x, int y, int w, int h, std::vector<uint8_t>& out)
   {
      out.assign((size_t)w * (size_t)h * 4, 0);
      GLint prev = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, f.fbo);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);
   }

   void WriteRect(const GLUtil::Fbo& f, int x, int y, int w, int h, const uint8_t* px)
   {
      glBindTexture(GL_TEXTURE_2D, f.tex);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      glBindTexture(GL_TEXTURE_2D, 0);
   }

   // Two-pass running box blur of an n-channel float image (w x h), radius r.
   void BoxBlurF(std::vector<float>& a, int w, int h, int ch, int r)
   {
      if (r <= 0)
         return;
      std::vector<float> t(a.size());
      for (int pass = 0; pass < 2; pass++)
      {
         for (int y = 0; y < h; y++)
            for (int c = 0; c < ch; c++)
            {
               float sum = 0;
               for (int k = -r; k <= r; k++)
                  sum += a[((size_t)y * w + std::clamp(k, 0, w - 1)) * ch + c];
               for (int x = 0; x < w; x++)
               {
                  t[((size_t)y * w + x) * ch + c] = sum / (2 * r + 1);
                  sum += a[((size_t)y * w + std::min(w - 1, x + r + 1)) * ch + c] - a[((size_t)y * w + std::max(0, x - r)) * ch + c];
               }
            }
         for (int x = 0; x < w; x++)
            for (int c = 0; c < ch; c++)
            {
               float sum = 0;
               for (int k = -r; k <= r; k++)
                  sum += t[((size_t)std::clamp(k, 0, h - 1) * w + x) * ch + c];
               for (int y = 0; y < h; y++)
               {
                  a[((size_t)y * w + x) * ch + c] = sum / (2 * r + 1);
                  sum += t[((size_t)std::min(h - 1, y + r + 1) * w + x) * ch + c] - t[((size_t)std::max(0, y - r) * w + x) * ch + c];
               }
            }
      }
   }

   // Fills the holes of `img` (RGBA floats, w x h; hole[i] true) from the known pixels around: a push-pull pyramid.
   void PushPull(std::vector<float>& img, const std::vector<uint8_t>& hole, int w, int h)
   {
      struct Lv { int w, h; std::vector<float> c; };   // premultiplied by weight, weight in [4]
      std::vector<Lv> lv(1);
      lv[0].w = w;
      lv[0].h = h;
      lv[0].c.assign((size_t)w * h * 5, 0.0f);
      for (size_t i = 0; i < (size_t)w * h; i++)
         if (!hole[i])
         {
            for (int c = 0; c < 4; c++)
               lv[0].c[i * 5 + c] = img[i * 4 + c];
            lv[0].c[i * 5 + 4] = 1.0f;
         }
      while (lv.back().w > 1 || lv.back().h > 1)
      {
         const Lv& a = lv.back();
         Lv b;
         b.w = std::max(1, (a.w + 1) / 2);
         b.h = std::max(1, (a.h + 1) / 2);
         b.c.assign((size_t)b.w * b.h * 5, 0.0f);
         for (int y = 0; y < a.h; y++)
            for (int x = 0; x < a.w; x++)
            {
               const float* s = &a.c[((size_t)y * a.w + x) * 5];
               float* d = &b.c[((size_t)(y / 2) * b.w + x / 2) * 5];
               for (int c = 0; c < 5; c++)
                  d[c] += s[c];
            }
         for (size_t i = 0; i < (size_t)b.w * b.h; i++)   // keep weight at most 1: a full cell counts as one known pixel
         {
            float* d = &b.c[i * 5];
            if (d[4] > 1.0f)
            {
               for (int c = 0; c < 4; c++)
                  d[c] /= d[4];
               d[4] = 1.0f;
            }
         }
         lv.push_back(std::move(b));
      }
      for (int k = (int)lv.size() - 2; k >= 0; k--)
      {
         Lv& a = lv[k];
         const Lv& b = lv[k + 1];
         for (int y = 0; y < a.h; y++)
            for (int x = 0; x < a.w; x++)
            {
               float* d = &a.c[((size_t)y * a.w + x) * 5];
               if (d[4] >= 1.0f)
                  continue;
               // Bilinear from the coarser level (all its cells are full by now).
               const float fx = std::clamp((x + 0.5f) / 2.0f - 0.5f, 0.0f, (float)b.w - 1), fy = std::clamp((y + 0.5f) / 2.0f - 0.5f, 0.0f, (float)b.h - 1);
               const int ix = (int)fx, iy = (int)fy, jx = std::min(b.w - 1, ix + 1), jy = std::min(b.h - 1, iy + 1);
               const float tx = fx - ix, ty = fy - iy;
               float up[4];
               for (int c = 0; c < 4; c++)
               {
                  auto at = [&](int xx, int yy) { const float* q = &b.c[((size_t)yy * b.w + xx) * 5]; return q[4] > 0 ? q[c] / q[4] : 0.0f; };
                  up[c] = (at(ix, iy) * (1 - tx) + at(jx, iy) * tx) * (1 - ty) + (at(ix, jy) * (1 - tx) + at(jx, jy) * tx) * ty;
               }
               const float wk = d[4];
               for (int c = 0; c < 4; c++)
                  d[c] = d[c] + up[c] * (1.0f - wk);
               d[4] = 1.0f;
            }
      }
      for (size_t i = 0; i < (size_t)w * h; i++)
         if (hole[i])
            for (int c = 0; c < 4; c++)
               img[i * 4 + c] = lv[0].c[i * 5 + c];
   }


   // Automatic heal (spot heal, inpaint, blemish): the hole is cut into tiles and each tile takes texture from its
   // own best place nearby, as patch-based fills do; one offset for a long stroke always lands somewhere wrong.
   // Base colour = the push-pull fill from the edge; texture = the source's detail above its own local mean, so a
   // dark or bright source lends grain but never its tone. Tiles cross-fade bilinearly, so there are no seams.
   bool HealTiled(std::vector<uint8_t>& px, int w, int h, const std::vector<float>& img, const std::vector<float>& orig,
                  const std::vector<uint8_t>& hole, const std::vector<uint8_t>& m8, int cx0, int cy0, int cw, int chh,
                  int hx0, int hy0, int bw, int bh, bool fixed, int fox, int foy)
   {
      (void)h;
      const int big = std::max(bw, bh);
      const int T = std::clamp(std::min(bw, bh), 12, 40);
      const int lr = std::max(2, T / 3);
      std::vector<float> low = img;
      BoxBlurF(low, cw, chh, 4, lr);
      auto inHole = [&](int x, int y) { return x < 0 || y < 0 || x >= cw || y >= chh || hole[(size_t)y * cw + x]; };
      auto luma = [&](const std::vector<float>& a, int x, int y) { const float* c = &a[((size_t)y * cw + x) * 4]; return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
      const int nx = (bw + T - 1) / T, ny = (bh + T - 1) / T;
      std::vector<int> tox((size_t)nx * ny, 0), toy((size_t)nx * ny, 0);
      std::vector<uint8_t> tok((size_t)nx * ny, 0);
      std::vector<float> tgain((size_t)nx * ny, 1.0f), twant((size_t)nx * ny, -1.0f);
      const int st = std::max(1, T / 10), ringW = std::max(2, T / 4);
      // Grain: mean luma step right and down over the points moved by (tx, ty).
      auto grain = [&](const std::vector<std::pair<int, int>>& pts, int tx, int ty)
      {
         float g = 0;
         int n = 0;
         for (const auto& q : pts)
         {
            const int x = q.first + tx, y = q.second + ty;
            if (x < 0 || y < 0 || x + 1 >= cw || y + 1 >= chh)
               continue;
            const float l0 = luma(orig, x, y);
            g += std::fabs(luma(orig, x + 1, y) - l0) + std::fabs(luma(orig, x, y + 1) - l0);
            n++;
         }
         return n > 0 ? g / n : 0.0f;
      };
      for (int j = 0; j < ny; j++)
         for (int i = 0; i < nx; i++)
         {
            const int x0 = hx0 + i * T, y0 = hy0 + j * T;
            std::vector<std::pair<int, int>> inner, ring;
            for (int y = std::max(0, y0 - ringW); y < std::min(chh, y0 + T + ringW); y += st)
               for (int x = std::max(0, x0 - ringW); x < std::min(cw, x0 + T + ringW); x += st)
               {
                  const bool in = x >= x0 && x < x0 + T && y >= y0 && y < y0 + T;
                  if (hole[(size_t)y * cw + x])
                  {
                     if (in)
                        inner.push_back({ x, y });
                     continue;
                  }
                  bool near = false;
                  for (int k = -ringW; k <= ringW && !near; k += std::max(1, ringW / 2))
                     near = (x + k >= 0 && x + k < cw && hole[(size_t)y * cw + x + k]) || (y + k >= 0 && y + k < chh && hole[(size_t)(y + k) * cw + x]);
                  if (near)
                     ring.push_back({ x, y });
               }
            if (inner.empty())
               continue;
            const float want = ring.empty() ? -1.0f : grain(ring, 0, 0);
            twant[(size_t)j * nx + i] = want;
            if (fixed)   // the heal brush's or patch's own source: every tile takes it
            {
               tox[(size_t)j * nx + i] = fox;
               toy[(size_t)j * nx + i] = foy;
               tok[(size_t)j * nx + i] = 1;
               tgain[(size_t)j * nx + i] = grain(inner, fox, foy);   // source grain for now, a gain below
               continue;
            }
            float best = 1e30f;
            for (int a = 0; a < 24; a++)
            {
               const float dx = std::cos(a * 0.2617994f), dy = std::sin(a * 0.2617994f);
               for (float d = T * 0.75f; d <= big * 1.5f + T; d += std::max(2.0f, T / 3.0f))
               {
                  const int tx = (int)std::lround(dx * d), ty = (int)std::lround(dy * d);
                  bool clear = true;
                  for (const auto& q : inner)
                     if (inHole(q.first + tx, q.second + ty))
                     {
                        clear = false;
                        break;
                     }
                  if (!clear)
                     continue;
                  // Colour: the source against the edge-predicted colour here; seam: the ring against the moved ring.
                  float e = 0;
                  int n = 0;
                  for (const auto& q : inner)
                  {
                     const int sx = q.first + tx, sy = q.second + ty;
                     for (int c = 0; c < 3; c++)
                     {
                        const float dv = orig[((size_t)sy * cw + sx) * 4 + c] - img[((size_t)q.second * cw + q.first) * 4 + c];
                        e += dv * dv;
                     }
                     n++;
                  }
                  float score = e / std::max(1, n);
                  int rn = 0;
                  float re = 0;
                  for (const auto& q : ring)
                  {
                     const int sx = q.first + tx, sy = q.second + ty;
                     if (inHole(sx, sy))
                        continue;
                     for (int c = 0; c < 3; c++)
                     {
                        const float dv = orig[((size_t)q.second * cw + q.first) * 4 + c] - orig[((size_t)sy * cw + sx) * 4 + c];
                        re += dv * dv;
                     }
                     rn++;
                  }
                  if (rn > 0)
                     score += re / rn;
                  if (want >= 0.0f)
                  {
                     const float gd = grain(inner, tx, ty) - want;
                     score += 4.0f * gd * gd;
                  }
                  score += 1e-3f * d / (float)big;
                  if (score < best)
                  {
                     best = score;
                     tox[(size_t)j * nx + i] = tx;
                     toy[(size_t)j * nx + i] = ty;
                     tok[(size_t)j * nx + i] = 1;
                  }
                  break;   // the nearest clear place in this direction only, farther ones are another texture
               }
            }
            if (tok[(size_t)j * nx + i])
               tgain[(size_t)j * nx + i] = grain(inner, tox[(size_t)j * nx + i], toy[(size_t)j * nx + i]);
         }
      // Gain: a source busier than the edge round the hole (a shadow line, a ridge) is turned down to the edge's
      // grain, so it lends texture, not its structure. Deep tiles use the mean edge grain.
      float wantSum = 0;
      int wantN = 0;
      for (float v : twant)
         if (v >= 0.0f)
         {
            wantSum += v;
            wantN++;
         }
      const float wantAll = wantN > 0 ? wantSum / wantN : 0.02f;
      for (size_t t = 0; t < tgain.size(); t++)
      {
         const float want = twant[t] >= 0.0f ? twant[t] : wantAll;
         tgain[t] = std::min(1.0f, (want * 1.25f + 0.004f) / std::max(1e-4f, tgain[t]));
      }
      for (int y = 0; y < chh; y++)
         for (int x = 0; x < cw; x++)
         {
            const size_t di = (size_t)y * cw + x;
            const float k = m8[di] / 255.0f;
            if (k <= 0.0f)
               continue;
            // Bilinear weights over the four nearest tile centres.
            const float fx = (x - hx0 + 0.5f) / T - 0.5f, fy = (y - hy0 + 0.5f) / T - 0.5f;
            const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
            const float ax = fx - ix, ay = fy - iy;
            float det[4] = { 0, 0, 0, 0 }, wsum = 0;
            for (int q = 0; q < 4; q++)
            {
               const int ti = std::clamp(ix + (q & 1), 0, nx - 1), tj = std::clamp(iy + (q >> 1), 0, ny - 1);
               const size_t t = (size_t)tj * nx + ti;
               if (!tok[t])
                  continue;
               // A source inside the hole steps on by the same offset (a clone chain) until it is clear.
               int sx = x + tox[t], sy = y + toy[t];
               for (int hop = 0; hop < 4 && sx >= 0 && sy >= 0 && sx < cw && sy < chh && hole[(size_t)sy * cw + sx]; hop++)
               {
                  sx += tox[t];
                  sy += toy[t];
               }
               if (inHole(sx, sy))
                  continue;
               const float wq = ((q & 1) ? ax : 1 - ax) * ((q >> 1) ? ay : 1 - ay) + 1e-4f;
               const size_t si = (size_t)sy * cw + sx;
               for (int c = 0; c < 4; c++)
                  det[c] += wq * tgain[t] * (orig[si * 4 + c] - low[si * 4 + c]);
               wsum += wq;
            }
            for (int c = 0; c < 4; c++)
            {
               const float v = img[di * 4 + c] + (wsum > 0 ? det[c] / wsum : 0.0f);
               const float o = orig[di * 4 + c] * (1.0f - k) + std::clamp(v, 0.0f, 1.0f) * k;
               px[((size_t)(y + cy0) * w + (x + cx0)) * 4 + c] = (uint8_t)std::lround(std::clamp(o, 0.0f, 1.0f) * 255.0f);
            }
         }
      return true;
   }

}   // namespace

// The masked pixels (mask 0..255, the surface's size) rebuilt from around them: the colour from a push-pull fill of
// the edge, the texture from a clean place `off` away (per tile, the best-matching place nearby when autoOff).
bool Scene2DNode::HealRegion(std::vector<uint8_t>& px, int w, int h, const std::vector<uint8_t>& mask, int ox, int oy, bool autoOff)
{
   int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
         if (mask[(size_t)y * w + x] > 8)
         {
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x);
            by1 = std::max(by1, y);
         }
   if (bx1 < 0)
      return false;
   const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1, big = std::max(bw, bh);
   const int R = std::clamp(big / 8 + 2, 3, 24);
   const int reach = autoOff ? big * 2 + R + 4 : std::max(std::abs(ox), std::abs(oy)) * 5 + R + 4;   // room for the clone chain
   const int cx0 = std::max(0, bx0 - reach), cy0 = std::max(0, by0 - reach);
   const int cx1 = std::min(w, bx1 + 1 + reach), cy1 = std::min(h, by1 + 1 + reach);
   const int cw = cx1 - cx0, chh = cy1 - cy0;
   std::vector<float> img((size_t)cw * chh * 4);
   std::vector<uint8_t> hole((size_t)cw * chh), m8((size_t)cw * chh);
   for (int y = 0; y < chh; y++)
      for (int x = 0; x < cw; x++)
      {
         const size_t si = (size_t)(y + cy0) * w + (x + cx0), di = (size_t)y * cw + x;
         for (int c = 0; c < 4; c++)
            img[di * 4 + c] = px[si * 4 + c] / 255.0f;
         m8[di] = mask[si];
         hole[di] = mask[si] > 8;
      }
   const std::vector<float> orig = img;
   PushPull(img, hole, cw, chh);
   return HealTiled(px, w, h, img, orig, hole, m8, cx0, cy0, cw, chh, bx0 - cx0, by0 - cy0, bw, bh, !autoOff, ox, oy);
}

// Crop Fill: Content-Aware, after PhotoCraft engine/src/crop_fill.rs (Apache 2.0, (c) the PhotoCraft authors): coverage
// by the crop's map, uncovered pieces labelled 8-connected, each filled on its own.
double Scene2DNode::CropMargin(const double m[6], bool rotated)
{
   return 0.5 * (std::fabs(m[0]) + std::fabs(m[1])) + (rotated ? 1.0 : 0.0);
}

bool Scene2DNode::CropCovered(const double m[6], double ow, double oh, int u, int v, double margin)
{
   const double cu = u + 0.5, cv = v + 0.5;
   const double x = m[0] * cu + m[1] * cv + m[4], y = m[2] * cu + m[3] * cv + m[5];
   return x >= margin && y >= margin && x <= ow - margin && y <= oh - margin;
}

std::vector<std::array<int, 4>> Scene2DNode::LabelComponents(std::vector<uint32_t>& l, int w, int h)
{
   std::vector<std::array<int, 4>> pieces;
   if (w <= 0 || h <= 0 || l.size() < (size_t)w * h)
      return pieces;
   std::vector<int> stack;
   for (int start = 0; start < w * h; start++)
   {
      if (l[start] != kUnlabelled)
         continue;
      const uint32_t k = (uint32_t)pieces.size() + 1;
      int x0 = w, y0 = h, x1 = -1, y1 = -1;
      l[start] = k;
      stack.push_back(start);
      while (!stack.empty())
      {
         const int i = stack.back();
         stack.pop_back();
         const int x = i % w, y = i / w;
         x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
         for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
               const int nx = x + dx, ny = y + dy;
               if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h)
                  continue;
               const int j = ny * w + nx;
               if (l[j] == kUnlabelled)
               {
                  l[j] = k;
                  stack.push_back(j);
               }
            }
      }
      pieces.push_back({ x0, y0, x1 + 1, y1 + 1 });
   }
   return pieces;
}

// Crop and Straighten Photos, after PhotoCraft algo/src/scancrop.rs (Apache 2.0, (c) the PhotoCraft authors): background
// = border median, foreground by Otsu on the colour distance, closed and opened, 8-connected pieces, then the
// minimum-area rectangle of each piece's hull (Freeman and Shapira 1975), shrunk by a px.
namespace
{
   float OtsuThreshold(const std::vector<float>& v)
   {
      constexpr int B = 256;
      double hist[B] = {};
      for (float x : v)
         hist[(int)(std::clamp(x, 0.0f, 1.0f) * (B - 1))] += 1.0;
      double total = 0, sum = 0;
      for (int i = 0; i < B; i++)
         total += hist[i], sum += i * hist[i];
      double wb = 0, sb = 0, best = 0;
      int thr = 0;
      for (int i = 0; i < B; i++)
      {
         wb += hist[i];
         if (wb == 0)
            continue;
         const double wf = total - wb;
         if (wf == 0)
            break;
         sb += i * hist[i];
         const double mb = sb / wb, mf = (sum - sb) / wf, between = wb * wf * (mb - mf) * (mb - mf);
         if (between > best)
            best = between, thr = i;
      }
      return (thr + 0.5f) / (B - 1);
   }

   std::vector<uint8_t> Morph(int w, int h, const std::vector<uint8_t>& m, int r, bool dilate)
   {
      auto pass = [&](const std::vector<uint8_t>& src, bool horizontal) {
         std::vector<uint8_t> out((size_t)w * h, 0);
         for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
               bool acc = !dilate;
               for (int d = -r; d <= r; d++)
               {
                  const int xx = horizontal ? x + d : x, yy = horizontal ? y : y + d;
                  const bool v = xx >= 0 && yy >= 0 && xx < w && yy < h && src[(size_t)yy * w + xx] != 0;
                  acc = dilate ? (acc || v) : (acc && v);
               }
               out[(size_t)y * w + x] = acc ? 1 : 0;
            }
         return out;
      };
      return pass(pass(m, true), false);
   }

   std::vector<std::array<double, 2>> ConvexHull(std::vector<std::array<double, 2>> pts)
   {
      std::sort(pts.begin(), pts.end());
      pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
      if (pts.size() < 3)
         return pts;
      auto cross = [](const std::array<double, 2>& o, const std::array<double, 2>& a, const std::array<double, 2>& b) {
         return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
      };
      std::vector<std::array<double, 2>> lo, up;
      for (const auto& p : pts)
      {
         while (lo.size() >= 2 && cross(lo[lo.size() - 2], lo.back(), p) <= 0)
            lo.pop_back();
         lo.push_back(p);
      }
      for (auto it = pts.rbegin(); it != pts.rend(); ++it)
      {
         while (up.size() >= 2 && cross(up[up.size() - 2], up.back(), *it) <= 0)
            up.pop_back();
         up.push_back(*it);
      }
      lo.pop_back();
      up.pop_back();
      lo.insert(lo.end(), up.begin(), up.end());
      return lo;
   }
}

std::vector<Scene2DNode::FoundPhoto> Scene2DNode::FindPhotos(int w, int h, const std::vector<uint8_t>& rgba)
{
   std::vector<FoundPhoto> out;
   if (w < 8 || h < 8 || rgba.size() < (size_t)w * h * 4)
      return out;
   const int k = std::max(1, (std::max(w, h) + 799) / 800);
   const int sw = (w + k - 1) / k, sh = (h + k - 1) / k;
   std::vector<std::array<float, 3>> small((size_t)sw * sh, { 0, 0, 0 });
   std::vector<int> cnt((size_t)sw * sh, 0);
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
         const size_t i = (size_t)(y / k) * sw + x / k;
         const uint8_t* p = &rgba[((size_t)y * w + x) * 4];
         for (int c = 0; c < 3; c++)
            small[i][c] += p[c] / 255.0f;
         cnt[i]++;
      }
   for (size_t i = 0; i < small.size(); i++)
      for (float& v : small[i])
         v /= (float)std::max(1, cnt[i]);
   std::vector<std::array<float, 3>> border;
   for (int x = 0; x < sw; x++)
      border.push_back(small[x]), border.push_back(small[(size_t)(sh - 1) * sw + x]);
   for (int y = 0; y < sh; y++)
      border.push_back(small[(size_t)y * sw]), border.push_back(small[(size_t)y * sw + sw - 1]);
   float bg[3];
   for (int c = 0; c < 3; c++)
   {
      std::vector<float> v;
      for (const auto& p : border)
         v.push_back(p[c]);
      std::sort(v.begin(), v.end());
      bg[c] = v[v.size() / 2];
   }
   std::vector<float> dist(small.size());
   for (size_t i = 0; i < small.size(); i++)
   {
      const float a = small[i][0] - bg[0], b = small[i][1] - bg[1], c = small[i][2] - bg[2];
      dist[i] = std::sqrt(a * a + b * b + c * c) / std::sqrt(3.0f);
   }
   const float thr = std::max(OtsuThreshold(dist), 0.04f);
   std::vector<uint8_t> mask(small.size());
   for (size_t i = 0; i < mask.size(); i++)
      mask[i] = dist[i] > thr ? 1 : 0;
   const int r = std::max(1, std::max(sw, sh) / 200);
   mask = Morph(sw, sh, Morph(sw, sh, mask, r, true), r, false);
   mask = Morph(sw, sh, Morph(sw, sh, mask, 1, false), 1, true);
   std::vector<uint32_t> label(mask.size());
   for (size_t i = 0; i < mask.size(); i++)
      label[i] = mask[i] ? kUnlabelled : 0;
   LabelComponents(label, sw, sh);
   uint32_t n = 0;
   for (uint32_t v : label)
      n = std::max(n, v);
   std::vector<std::vector<int>> comps(n);
   for (size_t i = 0; i < label.size(); i++)
      if (label[i] != 0)
         comps[label[i] - 1].push_back((int)i);
   const double total = (double)sw * sh;
   for (const auto& c : comps)
   {
      if ((double)c.size() <= total * 0.01)
         continue;
      std::vector<std::array<double, 2>> pts;
      for (int i : c)
      {
         const int x = i % sw, y = i / sw;
         const bool edge = x == 0 || y == 0 || x + 1 == sw || y + 1 == sh || !mask[i - 1] || !mask[i + 1] || !mask[i - sw] || !mask[i + sw];
         if (!edge)
            continue;
         for (int dy = 0; dy <= 1; dy++)
            for (int dx = 0; dx <= 1; dx++)
               pts.push_back({ (double)(x + dx) * k, (double)(y + dy) * k });
      }
      const auto hull = ConvexHull(pts);
      if (hull.size() < 3)
         continue;
      double best = 1e300, bcx = 0, bcy = 0, bw = 0, bh = 0, ang = 0;
      for (size_t i = 0; i < hull.size(); i++)
      {
         const auto& a = hull[i];
         const auto& b = hull[(i + 1) % hull.size()];
         const double th = std::atan2(b[1] - a[1], b[0] - a[0]), sn = std::sin(th), cs = std::cos(th);
         double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
         for (const auto& p : hull)
         {
            const double u = cs * p[0] + sn * p[1], v = -sn * p[0] + cs * p[1];
            x0 = std::min(x0, u), x1 = std::max(x1, u), y0 = std::min(y0, v), y1 = std::max(y1, v);
         }
         const double area = (x1 - x0) * (y1 - y0);
         if (area < best)
         {
            const double cu = (x0 + x1) * 0.5, cv = (y0 + y1) * 0.5;
            best = area, bcx = cs * cu - sn * cv, bcy = sn * cu + cs * cv, bw = x1 - x0, bh = y1 - y0, ang = th * 180.0 / 3.14159265358979;
         }
      }
      while (ang > 45.0)
         ang -= 90.0, std::swap(bw, bh);
      while (ang < -45.0)
         ang += 90.0, std::swap(bw, bh);
      FoundPhoto f;
      f.cx = bcx, f.cy = bcy, f.w = std::max(1.0, bw - 2.0 * k), f.h = std::max(1.0, bh - 2.0 * k), f.angle = ang;
      f.area = (double)c.size() / total;
      out.push_back(f);
   }
   // Reading order: rows by centre y (bands of a third of the smallest photo), then x.
   double band = 1e300;
   for (const auto& f : out)
      band = std::min(band, std::min(f.w, f.h));
   band = std::max(band, 1.0) / 3.0;
   std::sort(out.begin(), out.end(), [band](const FoundPhoto& a, const FoundPhoto& b) {
      const double ra = std::floor(a.cy / band), rb = std::floor(b.cy / band);
      return ra != rb ? ra < rb : a.cx < b.cx;
   });
   return out;
}

int Scene2DNode::StraightenPhotos(int id)
{
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> px;
   static int sScanFrame = 1 << 26;
   if (FindLayer(id) == nullptr || !RenderSet({ id }, ++sScanFrame, px) || px.size() < (size_t)W * H * 4)
      return 0;
   // Look only inside the scan's own box: the empty canvas around a smaller scan is not its background.
   float blo[2] = { 0, 0 }, bhi[2] = { (float)W, (float)H };
   Bounds(id, blo, bhi);
   const int bx0 = std::clamp((int)std::floor(blo[0]), 0, W), by0 = std::clamp((int)std::floor(blo[1]), 0, H);
   const int bx1 = std::clamp((int)std::ceil(bhi[0]), bx0, W), by1 = std::clamp((int)std::ceil(bhi[1]), by0, H);
   std::vector<FoundPhoto> found;
   if (bx1 - bx0 >= 2 && by1 - by0 >= 2)
   {
      const int bw = bx1 - bx0, bh = by1 - by0;
      std::vector<uint8_t> sub((size_t)bw * bh * 4);
      for (int y = 0; y < bh; y++)
         std::memcpy(&sub[(size_t)y * bw * 4], &px[((size_t)(by0 + y) * W + bx0) * 4], (size_t)bw * 4);
      found = FindPhotos(bw, bh, sub);
      for (FoundPhoto& f : found)
      {
         f.cx += bx0;
         f.cy += by0;
      }
   }
   int made = 0;
   for (const FoundPhoto& f : found)
   {
      const int pw = std::max(1, (int)std::lround(f.w)), ph = std::max(1, (int)std::lround(f.h));
      const double r = f.angle * 3.14159265358979 / 180.0, cs = std::cos(r), sn = std::sin(r);
      std::vector<uint8_t> up((size_t)pw * ph * 4, 0);
      for (int y = 0; y < ph; y++)
         for (int x = 0; x < pw; x++)
         {
            // The upright photo's pixel in the scan: along the photo's own axes from its centre, bilinear.
            const double u = x + 0.5 - pw * 0.5, v = y + 0.5 - ph * 0.5;
            const double sx = f.cx + cs * u - sn * v - 0.5, sy = f.cy + sn * u + cs * v - 0.5;
            const int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
            const double fx = sx - x0, fy = sy - y0;
            for (int c = 0; c < 4; c++)
            {
               double acc = 0;
               for (int j = 0; j <= 1; j++)
                  for (int i = 0; i <= 1; i++)
                  {
                     const int xx = std::clamp(x0 + i, 0, W - 1), yy = std::clamp(y0 + j, 0, H - 1);
                     acc += px[((size_t)yy * W + xx) * 4 + c] * (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                  }
               up[((size_t)y * pw + x) * 4 + c] = (uint8_t)std::lround(std::clamp(acc, 0.0, 255.0));
            }
         }
      const int nid = AddFileLayer(0, "Photo " + std::to_string(made + 1), (int)std::lround(f.cx - pw * 0.5),
                                   (int)std::lround(f.cy - ph * 0.5), pw, ph, up, id);
      if (nid > 0)
         made++;
   }
   std::fprintf(stderr, "PROBE straighten photos: found %zu made %d\n", found.size(), made);
   if (made > 0)
      Commit();
   return made;
}

void Scene2DNode::SetCloneExternal(const std::vector<uint8_t>& rgba, int w, int h)
{
   if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4)
   {
      mCloneExt.clear();
      mCloneExtW = mCloneExtH = 0;
      return;
   }
   mCloneExt.assign(rgba.begin(), rgba.begin() + (size_t)w * h * 4);
   mCloneExtW = w;
   mCloneExtH = h;
}

int Scene2DNode::CropContentAware(int id, float x0, float y0, float x1, float y1, float deg, bool deletePixels)
{
   if (x1 < x0) std::swap(x0, x1);
   if (y1 < y0) std::swap(y0, y1);
   const double ow = mDoc.w, oh = mDoc.h;
   const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
   const bool rotated = std::fabs(std::remainder((double)deg, 360.0)) > 1e-4;
   if (rotated)
      CropRotated((float)cx, (float)cy, x1 - x0, y1 - y0, deg, deletePixels);
   else
      CropTo(x0, y0, x1, y1, deletePixels);
   const int W = mDoc.w, H = mDoc.h;
   // The new canvas's (u, v) in the old document: the box's centre plus the box offset turned back by deg.
   const double r = deg * 3.14159265358979 / 180.0, cs = rotated ? std::cos(r) : 1.0, sn = rotated ? std::sin(r) : 0.0;
   const double hw = W * 0.5, hh = H * 0.5;
   const double m[6] = { cs, -sn, sn, cs, cx - (cs * hw - sn * hh), cy - (sn * hw + cs * hh) };
   const double margin = CropMargin(m, rotated);
   std::vector<uint32_t> labels((size_t)W * H, 0);
   bool any = false;
   for (int v = 0; v < H; v++)
      for (int u = 0; u < W; u++)
         if (!CropCovered(m, ow, oh, u, v, margin))
            labels[(size_t)v * W + u] = kUnlabelled, any = true;
   if (!any || FindLayer(id) == nullptr)
      return 0;
   const std::vector<std::array<int, 4>> pieces = LabelComponents(labels, W, H);
   std::vector<uint8_t> px;
   static int sCropFillFrame = 1 << 25;
   if (!RenderSet({ id }, ++sCropFillFrame, px) || px.size() < (size_t)W * H * 4)
      return 0;
   std::vector<uint8_t> hole((size_t)W * H);
   for (size_t k = 0; k < pieces.size(); k++)
   {
      for (size_t i = 0; i < hole.size(); i++)
         hole[i] = labels[i] == k + 1 ? 255 : 0;
      RemoveRegion(px, W, H, hole);
   }
   Layer& nl = AddLayer(Kind::Paint, 0, id);
   nl.name = "Crop fill";
   const int nid = nl.id;
   SyncSurfaces();
   // Only the rebuilt pixels, bottom row first as the surface holds them.
   std::vector<uint8_t> up((size_t)W * H * 4, 0);
   for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
         if (labels[(size_t)y * W + x] != 0)
            std::copy(&px[((size_t)y * W + x) * 4], &px[((size_t)y * W + x) * 4] + 4, &up[((size_t)(H - 1 - y) * W + x) * 4]);
   auto dt = mSurfaces.find(nid);
   Layer* dl = FindLayer(nid);
   if (dt == mSurfaces.end() || dl == nullptr)
      return 0;
   WriteRect(dt->second.fbo, 0, 0, W, H, up.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(dt->second, dl->tiles, all);
   std::fprintf(stderr, "PROBE crop fill: %zu pieces into layer %d\n", pieces.size(), nid);
   Commit();
   return nid;
}

// The Remove tool's fill: as HealRegion with automatic sources, but the base colour is a membrane fill (exact on
// smooth gradients, thin scratches take each side's colour) where the heal brushes use push-pull.
bool Scene2DNode::RemoveRegion(std::vector<uint8_t>& px, int w, int h, const std::vector<uint8_t>& mask)
{
   int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
         if (mask[(size_t)y * w + x] > 8)
         {
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x);
            by1 = std::max(by1, y);
         }
   if (bx1 < 0)
      return false;
   const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1, big = std::max(bw, bh);
   const int R = std::clamp(big / 8 + 2, 3, 24), reach = big * 2 + R + 4;
   const int cx0 = std::max(0, bx0 - reach), cy0 = std::max(0, by0 - reach);
   const int cx1 = std::min(w, bx1 + 1 + reach), cy1 = std::min(h, by1 + 1 + reach);
   const int cw = cx1 - cx0, chh = cy1 - cy0;
   std::vector<float> img((size_t)cw * chh * 4);
   std::vector<uint8_t> hole((size_t)cw * chh), m8((size_t)cw * chh);
   for (int y = 0; y < chh; y++)
      for (int x = 0; x < cw; x++)
      {
         const size_t si = (size_t)(y + cy0) * w + (x + cx0), di = (size_t)y * cw + x;
         for (int c = 0; c < 4; c++)
            img[di * 4 + c] = px[si * 4 + c] / 255.0f;
         m8[di] = mask[si];
         hole[di] = mask[si] > 8;
      }
   const std::vector<float> orig = img;
   SelectMaths::HarmonicFill(img, 4, hole, cw, chh);
   return HealTiled(px, w, h, img, orig, hole, m8, cx0, cy0, cw, chh, bx0 - cx0, by0 - cy0, bw, bh, false, 0, 0);
}

std::string Scene2DNode::RemoveLayerName() const
{
   const std::string base = "Remove Tool Edits";
   for (int n = 0;; n++)
   {
      const std::string name = n == 0 ? base : base + " " + std::to_string(n);
      bool taken = false;
      for (const Layer& l : mLayers)
         taken = taken || l.name == name;
      if (!taken)
         return name;
   }
}

// The MI-GAN pass over the classic fill (rows bottom first, like the surfaces): a crop round the hole, flipped
// upright, filled by the model, its colour written into the hole only. False (px untouched) when the pixels round the
// hole are not all opaque (the model knows no alpha) or the model fails.
bool Scene2DNode::RemoveWithAi(std::vector<uint8_t>& px, int w, int h, const std::vector<uint8_t>& hole)
{
   int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
         if (hole[(size_t)y * w + x] > 8)
         {
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x);
            by1 = std::max(by1, y);
         }
   if (bx1 < 0)
      return false;
   const int pad = std::max(64, std::max(bx1 - bx0, by1 - by0));   // context for the model, as much as the hole
   const int cx0 = std::max(0, bx0 - pad), cy0 = std::max(0, by0 - pad);
   const int cx1 = std::min(w, bx1 + 1 + pad), cy1 = std::min(h, by1 + 1 + pad);
   const int cw = cx1 - cx0, ch = cy1 - cy0;
   std::vector<uint8_t> crop((size_t)cw * ch * 4), m((size_t)cw * ch);
   for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++)
      {
         const size_t si = (size_t)(cy0 + ch - 1 - y) * w + (cx0 + x), di = (size_t)y * cw + x;   // flipped upright
         std::copy(&px[si * 4], &px[si * 4] + 4, &crop[di * 4]);
         m[di] = hole[si] > 8 ? 255 : 0;
         if (m[di] == 0 && crop[di * 4 + 3] != 255)
            return false;
      }
   std::vector<uint8_t> out;
   std::string err;
   if (!AiSelect::InpaintNow(crop, m, cw, ch, out, err))
      return false;
   for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++)
      {
         const size_t si = (size_t)(cy0 + ch - 1 - y) * w + (cx0 + x), di = (size_t)y * cw + x;
         if (m[di] != 0)
         {
            std::copy(&out[di * 4], &out[di * 4] + 3, &px[si * 4]);
            px[si * 4 + 3] = 255;
         }
      }
   return true;
}

int Scene2DNode::Remove(int id, const std::vector<RemoveStroke>& strokes, bool sampleAll, bool newLayer, bool ai)
{
   mRemoveUsedAi = false;
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || strokes.empty() || !EnsurePaintShaders())
      return 0;
   if (!newLayer && (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return 0;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return 0;
   const int W = mDoc.w, H = mDoc.h;
   float m[6];
   LayerMatrix(*l, m);
   const bool aligned = std::fabs(m[0] - 1) < 1e-4f && std::fabs(m[4] - 1) < 1e-4f && std::fabs(m[1]) < 1e-4f &&
                        std::fabs(m[3]) < 1e-4f && std::fabs(m[2]) < 1e-3f && std::fabs(m[5]) < 1e-3f && l->pw == W && l->ph == H;
   if (!aligned)
      return 0;
   // The hole, rows bottom first like the surfaces: each stroke with its own radius, as capsules.
   std::vector<uint8_t> hole((size_t)W * H, 0);
   for (const RemoveStroke& st : strokes)
   {
      const float r = st.Radius();
      const size_t n = st.xy.size() / 2;
      for (size_t k = 0; k < n; k++)
      {
         const float ax = st.xy[k * 2], ay = st.xy[k * 2 + 1];
         const float bx = st.xy[std::min(n - 1, k + 1) * 2], by = st.xy[std::min(n - 1, k + 1) * 2 + 1];
         const int x0 = std::max(0, (int)std::floor(std::min(ax, bx) - r)), x1 = std::min(W - 1, (int)std::ceil(std::max(ax, bx) + r));
         const int y0 = std::max(0, (int)std::floor(std::min(ay, by) - r)), y1 = std::min(H - 1, (int)std::ceil(std::max(ay, by) + r));
         const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
         for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
            {
               const float px = x + 0.5f, py = y + 0.5f;
               const float t = len2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f) : 0.0f;
               const float ex = px - (ax + t * dx), ey = py - (ay + t * dy);
               if (ex * ex + ey * ey <= r * r)
                  hole[(size_t)(H - 1 - y) * W + x] = 255;
            }
      }
   }
   SelectMaths::CloseRings(hole, W, H);
   SelectMaths::Dilate(hole, W, H, 2.0f);
   if (mSelAny && mSel.size() == hole.size())
      for (size_t i = 0; i < hole.size(); i++)
         if (mSel[i] == 0)
            hole[i] = 0;
   if (std::none_of(hole.begin(), hole.end(), [](uint8_t v) { return v != 0; }))
      return 0;
   std::vector<uint8_t> px;
   if (sampleAll)
   {
      if (mOut.fbo == 0)
         return 0;   // nothing composed yet to sample
      ReadRect(mOut, 0, 0, W, H, px);
   }
   else
      ReadRect(it->second.fbo, 0, 0, W, H, px);
   // An empty source has nothing to remove: no fill, no step.
   bool any = false;
   for (size_t i = 0; i < hole.size() && !any; i++)
      any = px[i * 4 + 3] != 0;
   if (!any)
      return 0;
   if (!RemoveRegion(px, W, H, hole))
      return 0;
   if (ai && AiSelect::InpaintAvailable())
      mRemoveUsedAi = RemoveWithAi(px, W, H, hole);
   int dest = id;
   if (newLayer)
   {
      const std::string name = RemoveLayerName();
      Layer& nl = AddLayer(Kind::Paint, 0, id);
      nl.name = name;
      dest = nl.id;
      SyncSurfaces();
      // Only the rebuilt pixels: the rest of the new layer stays clear.
      for (size_t i = 0; i < hole.size(); i++)
         if (hole[i] == 0)
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = px[i * 4 + 3] = 0;
   }
   else if (sampleAll)
   {
      // The picture's fill lands in the layer only in the hole; the layer keeps its own pixels elsewhere.
      std::vector<uint8_t> own;
      ReadRect(it->second.fbo, 0, 0, W, H, own);
      for (size_t i = 0; i < hole.size(); i++)
         if (hole[i] == 0)
            std::copy(&own[i * 4], &own[i * 4] + 4, &px[i * 4]);
   }
   auto dt = mSurfaces.find(dest);
   Layer* dl = FindLayer(dest);
   if (dt == mSurfaces.end() || dl == nullptr)
      return 0;
   WriteRect(dt->second.fbo, 0, 0, W, H, px.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(dt->second, dl->tiles, all);
   Commit();
   return dest;
}

namespace
{

   // Box blur, three times: close to a gaussian with sigma = radius (Photoshop's feather; PhotoCraft
   // feather_sigma). Three passes of half-width r give sigma^2 = r (r + 1).
   void Feather(std::vector<uint8_t>& a, int w, int h, float radius)
   {
      const int r = (int)std::lround((std::sqrt(1.0f + 4.0f * radius * radius) - 1.0f) * 0.5f);
      if (r < 1)
         return;
      std::vector<float> buf(a.begin(), a.end()), tmp(buf.size());
      for (int pass = 0; pass < 3; pass++)
      {
         for (int y = 0; y < h; y++)
         {
            float sum = 0;
            const float* row = &buf[(size_t)y * w];
            for (int x = -r; x <= r; x++)
               sum += row[std::clamp(x, 0, w - 1)];
            for (int x = 0; x < w; x++)
            {
               tmp[(size_t)y * w + x] = sum / (2 * r + 1);
               sum += row[std::min(w - 1, x + r + 1)] - row[std::max(0, x - r)];
            }
         }
         for (int x = 0; x < w; x++)
         {
            float sum = 0;
            for (int y = -r; y <= r; y++)
               sum += tmp[(size_t)std::clamp(y, 0, h - 1) * w + x];
            for (int y = 0; y < h; y++)
            {
               buf[(size_t)y * w + x] = sum / (2 * r + 1);
               sum += tmp[(size_t)std::min(h - 1, y + r + 1) * w + x] - tmp[(size_t)std::max(0, y - r) * w + x];
            }
         }
      }
      for (size_t i = 0; i < a.size(); i++)
         a[i] = (uint8_t)std::clamp((int)std::lround(buf[i]), 0, 255);
   }

   // Distance from the seed in RGBA, against tolerance as a share of the RGB cube's diagonal (0..255 = 0..100 %):
   // the rule Affinity's wand and flood fill follow (measured, tools/re-survey/affinity_probe.py), so a gradient
   // sky goes in one click at 20 % as it does there. The per-channel test it replaces stopped far short.
   bool Near(const uint8_t* a, const uint8_t* b, int tol)
   {
      if (a[3] == 0 && b[3] == 0)
         return true;   // two clear pixels are the same, whatever RGB they hide (Photoshop, PhotoCraft)
      int d2 = 0;
      for (int c = 0; c < 4; c++)
      {
         const int d = (int)a[c] - (int)b[c];
         d2 += d * d;
      }
      const float r = (float)tol * 1.7320508f;   // tol/255 of the diagonal, 255 * sqrt(3)
      return (float)d2 <= r * r;
   }

   // Squared Euclidean distance to the nearest seed (Felzenszwalb & Huttenlocher), in place over a w x h grid:
   // seeds hold 0, the rest a large value.
   void Edt1(const float* f, float* d, int* v, float* z, int n)
   {
      int k = 0;
      v[0] = 0;
      z[0] = -1e20f;
      z[1] = 1e20f;
      for (int q = 1; q < n; q++)
      {
         float s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
         while (s <= z[k])
         {
            k--;
            s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
         }
         k++;
         v[k] = q;
         z[k] = s;
         z[k + 1] = 1e20f;
      }
      k = 0;
      for (int q = 0; q < n; q++)
      {
         while (z[k + 1] < q)
            k++;
         d[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]];
      }
   }
   std::vector<float> Edt2(const std::vector<uint8_t>& seed, int w, int h)
   {
      const float kInf = 1e18f;
      std::vector<float> g((size_t)w * h);
      for (size_t i = 0; i < g.size(); i++)
         g[i] = seed[i] ? 0.0f : kInf;
      const int n = std::max(w, h);
      std::vector<float> f(n), d(n), z(n + 1);
      std::vector<int> v(n);
      for (int x = 0; x < w; x++)
      {
         for (int y = 0; y < h; y++) f[y] = g[(size_t)y * w + x];
         Edt1(f.data(), d.data(), v.data(), z.data(), h);
         for (int y = 0; y < h; y++) g[(size_t)y * w + x] = d[y];
      }
      for (int y = 0; y < h; y++)
      {
         std::copy(&g[(size_t)y * w], &g[(size_t)y * w] + w, f.begin());
         Edt1(f.data(), d.data(), v.data(), z.data(), w);
         std::copy(d.begin(), d.begin() + w, &g[(size_t)y * w]);
      }
      return g;
   }

   float SeedDist(const uint8_t* a, const uint8_t* b)
   {
      if (a[3] == 0 && b[3] == 0)
         return 0.0f;
      int d2 = 0;
      for (int c = 0; c < 4; c++)
      {
         const int d = (int)a[c] - (int)b[c];
         d2 += d * d;
      }
      return std::sqrt((float)d2);
   }

   // The antialiased edge Affinity gives its wand and flood fill (measured to 0.001): each pixel just outside the
   // region is covered by how far along the step from an in-region neighbour q the tolerance is crossed,
   // (tol - d_q) / (d_p - d_q), the largest over its 8 neighbours. Inside stays full, so a region never shrinks.
   bool sMatchSoft = true;   // the soft edge on (Scene2DNode::wandAntialias, set before each match)

   void SoftenEdge(const std::vector<uint8_t>& px, int w, int h, const uint8_t* ref, int tol, std::vector<uint8_t>& out)
   {
      if (!sMatchSoft)
         return;
      const float r = (float)tol * 1.7320508f;
      std::vector<uint8_t> edge(out.size(), 0);
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            const size_t i = (size_t)y * w + x;
            if (out[i] != 0)
               continue;
            float best = 0.0f, dp = -1.0f;
            for (int dy = -1; dy <= 1; dy++)
               for (int dx = -1; dx <= 1; dx++)
               {
                  const int nx = x + dx, ny = y + dy;
                  if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h)
                     continue;
                  const size_t n = (size_t)ny * w + nx;
                  if (out[n] != 255)
                     continue;
                  if (dp < 0.0f)
                     dp = SeedDist(&px[i * 4], ref);
                  const float dq = SeedDist(&px[n * 4], ref);
                  if (dp > dq)
                     best = std::max(best, std::clamp((r - dq) / (dp - dq), 0.0f, 1.0f));
               }
            edge[i] = (uint8_t)std::lround(best * 255.0f);
         }
      for (size_t i = 0; i < out.size(); i++)
         out[i] = std::max(out[i], edge[i]);
   }

   // Which pixels (document size, bottom row first) match the seed: flood or everywhere, with a soft 1 px edge.
   void MatchRegion(const std::vector<uint8_t>& px, int w, int h, int sx, int sy, int tol, bool contiguous,
                    std::vector<uint8_t>& out)
   {
      out.assign((size_t)w * h, 0);
      if (sx < 0 || sy < 0 || sx >= w || sy >= h)
         return;
      const uint8_t* seed = &px[((size_t)sy * w + sx) * 4];
      uint8_t ref[4];
      std::memcpy(ref, seed, 4);
      if (!contiguous)
      {
         for (size_t i = 0; i < out.size(); i++)
            out[i] = Near(&px[i * 4], ref, tol) ? 255 : 0;
         SoftenEdge(px, w, h, ref, tol, out);
         return;
      }
      std::vector<int> stack;
      stack.push_back(sy * w + sx);
      out[(size_t)sy * w + sx] = 255;
      while (!stack.empty())
      {
         const int i = stack.back();
         stack.pop_back();
         const int x = i % w, y = i / w;
         const int nb[4] = { x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1 };
         for (int n : nb)
            if (n >= 0 && out[n] == 0 && Near(&px[(size_t)n * 4], ref, tol))
            {
               out[n] = 255;
               stack.push_back(n);
            }
      }
      SoftenEdge(px, w, h, ref, tol, out);
   }
}

// ---- layers --------------------------------------------------------------

int Scene2DNode::AddPaintLayer(int above)
{
   return AddLayer(Kind::Paint, 0, above).id;
}

bool Scene2DNode::AddPixelMask(int id)
{
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind == Kind::Group)
      return false;
   if (!l->HasPixelMask())
   {
      float bw = 1, bh = 1;
      LayerBox(*l, bw, bh);
      l->mw = std::clamp((int)std::lround(bw), 1, GLUtil::kMaxTargetSide);
      l->mh = std::clamp((int)std::lround(bh), 1, GLUtil::kMaxTargetSide);
      l->mtiles.assign((size_t)Cols(l->mw) * Cols(l->mh), "-");
      l->pmaskOn = true;
   }
   Commit();
   return true;
}

void Scene2DNode::RemovePixelMask(int id)
{
   if (Layer* l = FindLayer(id))
   {
      l->mw = l->mh = 0;
      l->mtiles.clear();
      Commit();
   }
}

bool Scene2DNode::AddFilterMask(int id)
{
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind == Kind::Group || l->kind == Kind::Adjust || l->effects.empty())
      return false;
   if (!l->HasFilterMask())
   {
      l->fmw = std::clamp(mDoc.w, 1, GLUtil::kMaxTargetSide);
      l->fmh = std::clamp(mDoc.h, 1, GLUtil::kMaxTargetSide);
      l->fmtiles.assign((size_t)Cols(l->fmw) * Cols(l->fmh), "-");   // "-" loads white: the effects everywhere
   }
   Commit();
   return true;
}

void Scene2DNode::RemoveFilterMask(int id)
{
   if (Layer* l = FindLayer(id))
   {
      l->fmw = l->fmh = 0;
      l->fmtiles.clear();
      Commit();
   }
}

bool Scene2DNode::FilterMaskPixels(int id, std::vector<uint8_t>& out)
{
   SyncSurfaces();
   const Layer* l = FindConst(id);
   auto it = mFxMasks.find(id);
   if (l == nullptr || !l->HasFilterMask() || it == mFxMasks.end() || it->second.fbo.fbo == 0)
      return false;
   const int w = l->fmw, h = l->fmh;
   std::vector<uint8_t> px;
   ReadRect(it->second.fbo, 0, 0, w, h, px);
   if (px.size() != (size_t)w * h * 4)
      return false;
   out.resize((size_t)w * h);
   for (size_t i = 0; i < out.size(); i++)
      out[i] = px[i * 4];
   return true;
}

// The filter mask's new grey, `next` (w x h), blended in through the selection (soft edges kept) when there is one.
bool Scene2DNode::StoreFilterMask(int id, const std::vector<uint8_t>& was, std::vector<uint8_t> next)
{
   Layer* l = FindLayer(id);
   auto it = mFxMasks.find(id);
   if (l == nullptr || it == mFxMasks.end() || next.size() != was.size())
      return false;
   const int w = l->fmw, h = l->fmh;
   if (mSelAny && mSel.size() == next.size())
      for (size_t i = 0; i < next.size(); i++)
         next[i] = (uint8_t)((next[i] * mSel[i] + was[i] * (255 - mSel[i]) + 127) / 255);
   std::vector<uint8_t> px((size_t)w * h * 4);
   for (size_t i = 0; i < next.size(); i++)
   {
      px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = next[i];
      px[i * 4 + 3] = 255;
   }
   WriteRect(it->second.fbo, 0, 0, w, h, px.data());
   const int all[4] = { 0, 0, w, h };
   StoreTiles(it->second, l->fmtiles, all);
   Commit();
   return true;
}

bool Scene2DNode::FillFilterMask(int id, float grey)
{
   std::vector<uint8_t> m;
   if (!FilterMaskPixels(id, m))
      return false;
   const uint8_t g = (uint8_t)std::lround(std::clamp(grey, 0.0f, 1.0f) * 255.0f);
   return StoreFilterMask(id, m, std::vector<uint8_t>(m.size(), g));
}

bool Scene2DNode::BlurFilterMask(int id, float sigma)
{
   std::vector<uint8_t> m;
   const Layer* l = FindConst(id);
   if (sigma <= 0.0f || !FilterMaskPixels(id, m) || l == nullptr)
      return false;
   std::vector<uint8_t> b = m;
   SelectMaths::AdjustEdge(b, l->fmw, l->fmh, 0.0f, sigma, 0.0f, 0.0f);
   return StoreFilterMask(id, m, std::move(b));
}

int Scene2DNode::FilterMaskToLayer(int id, int above)
{
   std::vector<uint8_t> m;
   const Layer* l = FindConst(id);
   if (l == nullptr || !FilterMaskPixels(id, m))
      return 0;
   const int w = l->fmw, h = l->fmh;
   const bool cut = mSelAny && mSel.size() == m.size();
   std::vector<uint8_t> rgba((size_t)w * h * 4);
   for (int y = 0; y < h; y++)   // bottom-first mask to the top-down picture a file layer takes
      for (int x = 0; x < w; x++)
      {
         const size_t s = (size_t)(h - 1 - y) * w + x, d = ((size_t)y * w + x) * 4;
         rgba[d] = rgba[d + 1] = rgba[d + 2] = m[s];
         rgba[d + 3] = cut ? mSel[s] : 255;
      }
   const int nid = AddFileLayer(0, "Filter Mask", 0, 0, w, h, rgba, above);
   Commit();
   return nid;
}

bool Scene2DNode::MaskFromSelection(int id)
{
   if (!AddPixelMask(id) || !EnsurePaintShaders())
      return false;
   SyncSurfaces();
   Layer* l = FindLayer(id);
   auto it = mMasks.find(id);
   if (l == nullptr || it == mMasks.end())
      return false;
   if (mSelAny)
   {
      UploadSelection();
      ApplyFill(it->second, l->mw, l->mh, *l, 0, nullptr, nullptr, -1, nullptr, nullptr, 1.0f);
   }
   const int all[4] = { 0, 0, l->mw, l->mh };
   StoreTiles(it->second, l->mtiles, all);
   l = FindLayer(id);
   Commit();
   return true;
}

bool Scene2DNode::ApplyPixelMask(int id)
{
   SyncSurfaces();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || !l->HasPixelMask())
      return false;
   if (!l->pmaskOn)
   {
      RemovePixelMask(id);   // a disabled mask hides nothing, so applying it keeps every pixel
      return true;
   }
   auto mt = mMasks.find(id);
   auto pt = mSurfaces.find(id);
   if (mt == mMasks.end() || pt == mSurfaces.end() || mt->second.fbo.fbo == 0 || l->mw != l->pw || l->mh != l->ph)
      return false;
   const int w = l->pw, h = l->ph;
   std::vector<uint8_t> m, px;
   ReadRect(mt->second.fbo, 0, 0, w, h, m);
   ReadRect(pt->second.fbo, 0, 0, w, h, px);
   if (m.size() != (size_t)w * h * 4 || px.size() != m.size())
      return false;
   for (size_t i = 0; i < (size_t)w * h; i++)   // straight alpha: only alpha scales
      px[i * 4 + 3] = (uint8_t)((px[i * 4 + 3] * m[i * 4] + 127) / 255);
   WriteRect(pt->second.fbo, 0, 0, w, h, px.data());
   const int all[4] = { 0, 0, w, h };
   StoreTiles(pt->second, l->tiles, all);
   RemovePixelMask(id);
   return true;
}

bool Scene2DNode::InvertPixelMask(int id)
{
   SyncSurfaces();
   Layer* l = FindLayer(id);
   auto it = mMasks.find(id);
   if (l == nullptr || !l->HasPixelMask() || it == mMasks.end() || it->second.fbo.fbo == 0)
      return false;
   const int w = l->mw, h = l->mh;
   std::vector<uint8_t> m;
   ReadRect(it->second.fbo, 0, 0, w, h, m);
   if (m.size() != (size_t)w * h * 4)
      return false;
   for (size_t i = 0; i < (size_t)w * h; i++)
      for (int c = 0; c < 3; c++)
         m[i * 4 + c] = (uint8_t)(255 - m[i * 4 + c]);
   WriteRect(it->second.fbo, 0, 0, w, h, m.data());
   const int all[4] = { 0, 0, w, h };
   StoreTiles(it->second, l->mtiles, all);
   Commit();
   return true;
}

// Layer > Layer Mask > From Transparency: the layer's alpha moves into its mask (times a mask already there) over the
// whole layer box, past the canvas edge too, and the pixels become opaque.
bool Scene2DNode::MaskFromTransparency(int id)
{
   SyncSurfaces();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return false;
   auto pt = mSurfaces.find(id);
   if (pt == mSurfaces.end() || pt->second.fbo.fbo == 0)
      return false;
   const int w = l->pw, h = l->ph;
   if (l->HasPixelMask() && (l->mw != w || l->mh != h))
      return false;
   const bool had = l->HasPixelMask();
   if (!had)
   {
      l->mw = w;
      l->mh = h;
      l->mtiles.assign((size_t)Cols(w) * Cols(h), "-");
      l->pmaskOn = true;
      Commit();
      SyncSurfaces();
      l = FindLayer(id);
   }
   auto mt = mMasks.find(id);
   pt = mSurfaces.find(id);
   if (l == nullptr || mt == mMasks.end() || mt->second.fbo.fbo == 0 || pt == mSurfaces.end())
      return false;
   std::vector<uint8_t> m, px;
   ReadRect(mt->second.fbo, 0, 0, w, h, m);
   ReadRect(pt->second.fbo, 0, 0, w, h, px);
   if (m.size() != (size_t)w * h * 4 || px.size() != m.size())
      return false;
   for (size_t i = 0; i < (size_t)w * h; i++)
   {
      const uint8_t v = (uint8_t)(had ? (px[i * 4 + 3] * m[i * 4] + 127) / 255 : px[i * 4 + 3]);
      m[i * 4] = m[i * 4 + 1] = m[i * 4 + 2] = v;
      m[i * 4 + 3] = 255;
      px[i * 4 + 3] = 255;
   }
   WriteRect(mt->second.fbo, 0, 0, w, h, m.data());
   WriteRect(pt->second.fbo, 0, 0, w, h, px.data());
   const int all[4] = { 0, 0, w, h };
   StoreTiles(mt->second, l->mtiles, all);
   StoreTiles(pt->second, l->tiles, all);
   Commit();
   return true;
}

// Layer > Layer Mask > Hide Selection: a mask that hides the selected pixels and shows the rest.
bool Scene2DNode::MaskHideSelection(int id)
{
   const Layer* l = FindConst(id);
   if (!mSelAny || l == nullptr || l->HasPixelMask())
      return false;
   // The selection as a mask, then the mask inverted: past the canvas the selection is empty, so that part of the
   // layer stays shown (inverting the selection first would hide it).
   return MaskFromSelection(id) && InvertPixelMask(id);
}

// The layer's pixel mask through its box, at document size (bottom row first, 0..255; 0 outside the box).
bool Scene2DNode::MaskInDoc(int id, std::vector<uint8_t>& out)
{
   SyncSurfaces();
   const Layer* l = FindConst(id);
   auto it = mMasks.find(id);
   if (l == nullptr || !l->HasPixelMask() || it == mMasks.end() || it->second.fbo.fbo == 0)
      return false;
   const int mw = l->mw, mh = l->mh;
   std::vector<uint8_t> m;
   ReadRect(it->second.fbo, 0, 0, mw, mh, m);
   float bw = 1, bh = 1;
   if (m.size() != (size_t)mw * mh * 4 || !LayerBox(*l, bw, bh))
      return false;
   const Layer box = *l;
   const int w = mDoc.w, h = mDoc.h;
   out.assign((size_t)w * h, 0);
   for (int y = 0; y < h; y++)   // document rows top-down; the output keeps the bottom row first
      for (int x = 0; x < w; x++)
      {
         float bx = 0, by = 0;
         DocToBox(box, x + 0.5f, y + 0.5f, bx, by);
         const int ix = (int)std::floor(bx * mw / bw), iy = (int)std::floor(mh - by * mh / bh);
         out[(size_t)(h - 1 - y) * w + x] = ix >= 0 && iy >= 0 && ix < mw && iy < mh ? m[((size_t)iy * mw + ix) * 4] : 0;
      }
   return true;
}

// Cmd-click on a mask thumbnail: the mask's grey, through the layer's box, as the selection (Photoshop's load mask).
bool Scene2DNode::SelectMaskPixels(int id, int op)
{
   std::vector<uint8_t> m;
   if (!MaskInDoc(id, m))
      return false;
   const int w = mDoc.w, h = mDoc.h;
   if (mSel.size() != (size_t)w * h)
      mSel.assign((size_t)w * h, 0);
   for (size_t i = 0; i < m.size(); i++)
   {
      const int a = m[i];
      uint8_t& v = mSel[i];
      v = (uint8_t)(op == kSelAdd ? std::max<int>(v, a) : op == kSelSub ? std::max(0, v - a)
                                : op == kSelIntersect ? std::min<int>(v, a) : a);
   }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
   StoreSelection();
   return true;
}

unsigned Scene2DNode::MaskViewTexture(int id)
{
   const Layer* l = FindConst(id);
   if (l == nullptr || !l->HasPixelMask())
      return 0;
   // Redrawn when the document changes (a finished stroke, a moved layer), not every frame.
   const size_t key = std::hash<std::string>()(mText) ^ (size_t)id * 0x9E3779B97F4A7C15ull;
   if (mMaskView.tex != 0 && mMaskViewKey == key && mMaskView.w == mDoc.w && mMaskView.h == mDoc.h)
      return mMaskView.tex;
   std::vector<uint8_t> m;
   if (!MaskInDoc(id, m) || !GLUtil::EnsureFbo(mMaskView, mDoc.w, mDoc.h, GL_RGBA8, "Scene2DMaskView"))
      return 0;
   std::vector<uint8_t> rgba(m.size() * 4);
   for (size_t i = 0; i < m.size(); i++)   // Photoshop's rubylith: half red where the mask hides
   {
      rgba[i * 4] = 255;
      rgba[i * 4 + 1] = rgba[i * 4 + 2] = 0;
      rgba[i * 4 + 3] = (uint8_t)((255 - m[i]) / 2);
   }
   WriteRect(mMaskView, 0, 0, mDoc.w, mDoc.h, rgba.data());
   mMaskViewKey = key;
   return mMaskView.tex;
}

// ---- surfaces ------------------------------------------------------------

Scene2DNode::Surface* Scene2DNode::SyncSurface(std::map<int, Surface>& in, int id, int w, int h,
                                               const std::vector<std::string>& keys)
{
   if (w <= 0 || h <= 0)
      return nullptr;
   const bool mask = &in == &mMasks || &in == &mFxMasks || &in == &mWarpMaskFrom;
   Surface& s = in[id];
   if (s.fbo.w != w || s.fbo.h != h || s.fbo.tex == 0)
   {
      if (!GLUtil::EnsureFbo(s.fbo, w, h, GL_RGBA8, mask ? "Scene2DMask" : "Scene2DPaint"))
         return nullptr;
      s.loaded.assign(keys.size(), "?");
   }
   if (s.loaded.size() != keys.size())
      s.loaded.assign(keys.size(), "?");
   const int cols = Cols(w);
   std::vector<uint8_t> px;
   for (size_t i = 0; i < keys.size(); i++)
   {
      if (keys[i] == s.loaded[i])
         continue;
      const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
      const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
      if (tw <= 0 || th <= 0)
         continue;
      int gw = 0, gh = 0;
      if (keys[i] == "-" || !TileStore::Get(keys[i], px, gw, gh) || gw != tw || gh != th)
         px.assign((size_t)tw * th * 4, mask ? 255 : 0);
      WriteRect(s.fbo, x0, y0, tw, th, px.data());
      s.loaded[i] = keys[i];
      mPaintEdits++;
   }
   return &s;
}

void Scene2DNode::SyncSurfaces()
{
   std::vector<int> paintIds, maskIds, fxIds;
   for (const Layer& l : mLayers)
   {
      if ((l.kind == Kind::Paint || l.HasSkin()) && SyncSurface(mSurfaces, l.id, l.pw, l.ph, l.tiles) != nullptr)
      {
         paintIds.push_back(l.id);
         if (l.kind == Kind::Paint && mUndoTiles.find(l.id) == mUndoTiles.end())
            mUndoTiles[l.id] = l.tiles;   // the Undo brush paints back to the layer as first shown
      }
      if (l.HasPixelMask() && SyncSurface(mMasks, l.id, l.mw, l.mh, l.mtiles) != nullptr)
         maskIds.push_back(l.id);
      if (l.HasFilterMask() && SyncSurface(mFxMasks, l.id, l.fmw, l.fmh, l.fmtiles) != nullptr)
         fxIds.push_back(l.id);
   }
   for (auto* m : { &mSurfaces, &mMasks, &mFxMasks })
   {
      const std::vector<int>& keep = m == &mSurfaces ? paintIds : m == &mMasks ? maskIds : fxIds;
      for (auto it = m->begin(); it != m->end();)
      {
         if (std::find(keep.begin(), keep.end(), it->first) == keep.end() && !((mStroke.Active() || mArt.on) && it->first == mPaintId))
         {
            GLUtil::RetireFbo(it->second.fbo); // its thumbnail may be queued this frame
            it = m->erase(it);
         }
         else
            ++it;
      }
   }
}

void Scene2DNode::StoreTiles(Surface& s, std::vector<std::string>& keys, const int dirty[4])
{
   const int w = s.fbo.w, h = s.fbo.h, cols = Cols(w);
   const bool mask = !mMasks.empty() && std::any_of(mMasks.begin(), mMasks.end(), [&](const auto& kv) { return &kv.second == &s; });
   std::vector<uint8_t> px;
   for (size_t i = 0; i < keys.size(); i++)
   {
      const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
      const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
      if (tw <= 0 || th <= 0 || x0 >= dirty[2] || y0 >= dirty[3] || x0 + tw <= dirty[0] || y0 + th <= dirty[1])
         continue;
      ReadRect(s.fbo, x0, y0, tw, th, px);
      bool white = mask;
      for (size_t k = 0; white && k < px.size(); k++)
         white = px[k] == 255;
      keys[i] = white ? "-" : TileStore::Put(px.data(), tw, th);
      s.loaded[i] = keys[i];
   }
   mPaintEdits++;
}

bool Scene2DNode::DocToSurface(const Layer& l, int target, float x, float y, float& sx, float& sy, float& scale)
{
   if (target == 2)
   {
      sx = x;
      sy = (float)mDoc.h - y;
      scale = 1.0f;
      return true;
   }
   if (target == 3)
   {
      // The filter mask lies over the document, stretched if the canvas changed size since it was made.
      if (l.fmw <= 0 || l.fmh <= 0 || mDoc.w <= 0 || mDoc.h <= 0)
         return false;
      const float kx = (float)l.fmw / mDoc.w, ky = (float)l.fmh / mDoc.h;
      sx = x * kx;
      sy = (float)l.fmh - y * ky;
      scale = std::sqrt(kx * ky);
      return true;
   }
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   const float sw = (float)(target == 0 ? l.pw : l.mw), sh = (float)(target == 0 ? l.ph : l.mh);
   if (sw <= 0 || sh <= 0)
      return false;
   // Through the card's homography; the brush scale is the local one (a 3D card shrinks with distance).
   float bx = 0, by = 0, ux = 0, uy = 0, vx = 0, vy = 0;
   DocToBox(l, x, y, bx, by);
   DocToBox(l, x + 1.0f, y, ux, uy);
   DocToBox(l, x, y + 1.0f, vx, vy);
   sx = bx * sw / bw;
   sy = sh - by * sh / bh;
   scale = std::sqrt(std::fabs((ux - bx) * (vy - by) - (uy - by) * (vx - bx))) * sw / bw;
   return true;
}

bool Scene2DNode::EnsurePaintShaders()
{
   if (!mPaintTried)
   {
      mPaintTried = true;
      mFillProgram = GLUtil::CompileProgram(kFillFrag);
      mAntsProgram = GLUtil::CompileProgram(kAntsFrag);
   }
   return mFillProgram != 0 && mAntsProgram != 0;
}

void Scene2DNode::ApplyFill(Surface& s, int sw, int sh, const Layer& l, unsigned coverage, const float colA[4],
                            const float colB[4], int kind, const float p0[2], const float p1[2], float opacity)
{
   // kind -1: write the selection as grey (mask from selection).
   GLUtil::Fbo out;
   if (!GLUtil::EnsureFbo(out, sw, sh, GL_RGBA8, "Scene2DFill"))
      return;
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   float m[6];
   LayerMatrix(l, m);
   const unsigned prog = mFillProgram;
   // kind -2 samples the picture (mOut) through the same mapping: clone from all layers.
   const unsigned sel = kind == -2 ? mOut.tex : mSelAny ? mSelTex.tex : 0;
   const float zero[4] = { 0, 0, 0, 0 };
   GLUtil::RunShaderPass(out, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, s.fbo.tex);
      glUniform1i(GLUtil::UniformLoc(prog, "uSurf"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, coverage);
      glUniform1i(GLUtil::UniformLoc(prog, "uCov"), 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, sel);
      glUniform1i(GLUtil::UniformLoc(prog, "uSel"), 2);
      glActiveTexture(GL_TEXTURE0);
      glUniform1i(GLUtil::UniformLoc(prog, "uHasCov"), coverage != 0 ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uHasSel"), sel != 0 ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uOp"), kind == -3 ? 3 : kind == -2 ? 2 : kind < 0 ? 1 : 0);
      glUniform2f(GLUtil::UniformLoc(prog, "uSurfSize"), (float)sw, (float)sh);
      glUniform2f(GLUtil::UniformLoc(prog, "uBox"), bw, bh);
      glUniform2f(GLUtil::UniformLoc(prog, "uDoc"), (float)mDoc.w, (float)mDoc.h);
      glUniform3f(GLUtil::UniformLoc(prog, "uMA"), m[0], m[1], m[2]);
      glUniform3f(GLUtil::UniformLoc(prog, "uMB"), m[3], m[4], m[5]);
      glUniform4fv(GLUtil::UniformLoc(prog, "uColA"), 1, colA != nullptr ? colA : zero);
      glUniform4fv(GLUtil::UniformLoc(prog, "uColB"), 1, colB != nullptr ? colB : zero);
      glUniform1i(GLUtil::UniformLoc(prog, "uKind"), std::max(0, kind));
      glUniform2f(GLUtil::UniformLoc(prog, "uP0"), p0 != nullptr ? p0[0] : 0.0f, p0 != nullptr ? p0[1] : 0.0f);
      glUniform2f(GLUtil::UniformLoc(prog, "uP1"), p1 != nullptr ? p1[0] : 1.0f, p1 != nullptr ? p1[1] : 1.0f);
      glUniform1f(GLUtil::UniformLoc(prog, "uOpacity"), std::clamp(opacity, 0.0f, 1.0f));
      glUniform1i(GLUtil::UniformLoc(prog, "uLock"), (EffectiveLock(l) & kLockAlpha) != 0 ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uExtend"), mGradExtend);
      glUniform1i(GLUtil::UniformLoc(prog, "uDither"), mGradDither ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uMethod"), mGradMethod);
   });
   GLint prevRead = 0;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, out.fbo);
   glBindTexture(GL_TEXTURE_2D, s.fbo.tex);
   glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, sw, sh);
   glBindTexture(GL_TEXTURE_2D, 0);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
   GLUtil::DestroyFbo(out);
}

// ---- painting --------------------------------------------------------------

namespace
{
   bool sHealFixed = false;   // the stroke in progress is a healing brush with a source (one stroke at a time)
   int sHealOff[2] = { 0, 0 };   // its source offset, surface px
}

bool Scene2DNode::BeginPaint(int id, int target, const BrushEngine::Brush& b, float x, float y, float pressure)
{
   Layers();
   if (mStroke.Active() || mArt.on)
      EndPaint();
   mPaintBrushIn = b;
   BrushEngine::Brush brush = b;
   GLUtil::Fbo* surf = nullptr;
   const Layer* l = FindConst(id);
   if (target == 2)
   {
      if (mSel.size() != (size_t)mDoc.w * mDoc.h)
      {
         mSel.assign((size_t)mDoc.w * mDoc.h, 0);
         mSelDirty = true;
      }
      UploadSelection();
      surf = &mSelTex;
   }
   else
   {
      if (l == nullptr || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
         return false;
      if (target == 0 && l->kind != Kind::Paint)
         return false;
      if (target == 1 && !l->HasPixelMask())
         return false;
      if (target == 3 && !l->HasFilterMask())
         return false;
      SyncSurfaces();
      auto& m = target == 0 ? mSurfaces : target == 3 ? mFxMasks : mMasks;
      auto it = m.find(id);
      if (it == m.end())
         return false;
      surf = &it->second.fbo;
      if (target == 0)
         brush.lockAlpha = brush.lockAlpha || ((EffectiveLock(*l) & kLockAlpha) != 0 && brush.mode != BrushEngine::kBgErase);   // the background eraser ignores it (Photoshop)
   }
   if (target != 0)
   {
      // Masks hold grey: the eraser shows (or, on the quick mask, takes away).
      if (brush.mode == BrushEngine::kErase)
      {
         const float v = target == 2 ? 0.0f : 1.0f;
         brush.color[0] = brush.color[1] = brush.color[2] = v;
      }
      brush.mode = BrushEngine::kNormal;
      brush.lockAlpha = false;
      const float g = (brush.color[0] + brush.color[1] + brush.color[2]) / 3.0f;
      brush.color[0] = brush.color[1] = brush.color[2] = g;
   }
   float sx = 0, sy = 0, scale = 1;
   if (!DocToSurface(target == 2 ? Layer() : *l, target, x, y, sx, sy, scale))
      return false;
   brush.size *= scale;
   if (brush.mode == BrushEngine::kClone || brush.mode == BrushEngine::kHeal)
   {
      // The source offset arrives in document pixels; the shader wants it on this surface.
      float ox = 0, oy = 0, s2 = 1;
      if (DocToSurface(target == 2 ? Layer() : *l, target, x + b.sourceOffset[0], y + b.sourceOffset[1], ox, oy, s2))
      {
         brush.sourceOffset[0] = ox - sx;
         brush.sourceOffset[1] = oy - sy;
      }
      // Rotation, scale and flip turn the source about where the aligned run began (else the stroke's start).
      float ax = sx, ay = sy, s3 = 1;
      if (b.sourceAnchor[0] != 0.0f || b.sourceAnchor[1] != 0.0f)
         DocToSurface(target == 2 ? Layer() : *l, target, b.sourceAnchor[0], b.sourceAnchor[1], ax, ay, s3);
      brush.sourceAnchor[0] = ax;
      brush.sourceAnchor[1] = ay;
   }
   // Healing brush with a source: the stroke marks where to mend (the inpaint preview) and on release the marked
   // pixels are rebuilt from the source with the membrane blend (HealRegion), as Affinity heals: texture from the
   // source, tone from round the stroke. Dab-by-dab mixing washed the texture out.
   sHealFixed = false;
   if (target == 0 && brush.mode == BrushEngine::kHeal && !b.sampleAll)
   {
      sHealFixed = true;
      sHealOff[0] = (int)std::lround(brush.sourceOffset[0]);
      sHealOff[1] = (int)std::lround(brush.sourceOffset[1]);
      brush.mode = BrushEngine::kInpaint;
   }
   // Paint lands only inside the selection, resampled to the surface when it is placed differently.
   unsigned sel = 0;
   if (target == 3 && mSelAny && surf->w == mDoc.w && surf->h == mDoc.h)
   {
      UploadSelection();   // the filter mask is document-sized: the selection lies on it as it is
      sel = mSelTex.tex;
   }
   else if (target != 2 && mSelAny && EnsurePaintShaders())
   {
      UploadSelection();
      if (GLUtil::EnsureFbo(mSelScratch, surf->w, surf->h, GL_RGBA8, "Scene2DSelResample"))
      {
         Surface tmp;
         tmp.fbo = mSelScratch;
         ApplyFill(tmp, surf->w, surf->h, *l, 0, nullptr, nullptr, -1, nullptr, nullptr, 1.0f);
         sel = mSelScratch.tex;
      }
   }
   mPaintId = id;
   mPaintTarget = target;
   brush.sourceTex = 0;
   const bool readsAll = brush.mode == BrushEngine::kClone || brush.mode == BrushEngine::kHeal || brush.mode == BrushEngine::kBlur ||
                         brush.mode == BrushEngine::kSharpen || brush.mode == BrushEngine::kSmudge;
   if (target == 0 && b.sampleAll && readsAll && mOut.tex != 0 &&
       EnsurePaintShaders() && GLUtil::EnsureFbo(mCloneSrc, surf->w, surf->h, GL_RGBA8, "Scene2DCloneSrc"))
   {
      // All layers: the picture as it stands, laid onto this surface's pixels, is what the clone copies from.
      Surface tmp;
      tmp.fbo = mCloneSrc;
      ApplyFill(tmp, surf->w, surf->h, *l, 0, nullptr, nullptr, -2, nullptr, nullptr, 1.0f);
      brush.sourceTex = mCloneSrc.tex;
   }
   if (target == 0 && brush.mode == BrushEngine::kClone && !mCloneExt.empty() && mCloneExtW > 0 && mCloneExtH > 0 &&
       GLUtil::EnsureFbo(mCloneSrc, surf->w, surf->h, GL_RGBA8, "Scene2DCloneSrc"))
   {
      // Another document's pixels laid onto this surface's px through the layer's placing, repeated at the source's own
      // edges under Wrap, clear past them otherwise.
      const int sw = surf->w, sh = surf->h, ew = mCloneExtW, eh = mCloneExtH;
      float p0x = 0, p0y = 0, p1x = 1, p1y = 0, p2x = 0, p2y = 1, sc = 1;
      DocToSurface(*l, 0, 0.0f, 0.0f, p0x, p0y, sc);
      DocToSurface(*l, 0, 1.0f, 0.0f, p1x, p1y, sc);
      DocToSurface(*l, 0, 0.0f, 1.0f, p2x, p2y, sc);
      const double ax = p1x - p0x, ay = p1y - p0y, bx = p2x - p0x, by = p2y - p0y, det = ax * by - bx * ay;
      std::vector<uint8_t> up((size_t)sw * sh * 4, 0);
      if (std::fabs(det) > 1e-9)
         for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++)
            {
               const double qx = x + 0.5 - p0x, qy = y + 0.5 - p0y;
               int dx = (int)std::floor((by * qx - bx * qy) / det), dy = (int)std::floor((-ay * qx + ax * qy) / det);
               if (brush.wrap)
                  dx = ((dx % ew) + ew) % ew, dy = ((dy % eh) + eh) % eh;
               if (dx < 0 || dy < 0 || dx >= ew || dy >= eh)
                  continue;
               const uint8_t* e = &mCloneExt[((size_t)dy * ew + dx) * 4];
               std::copy(e, e + 4, &up[((size_t)y * sw + x) * 4]);
            }
      WriteRect(mCloneSrc, 0, 0, sw, sh, up.data());
      brush.sourceTex = mCloneSrc.tex;
   }
   if (target == 0 && brush.mode == BrushEngine::kUndo)
   {
      const GLUtil::Fbo* src = HistorySourceSurface(*l);
      if (src == nullptr)
         return false;   // no state to paint back to (gone, or it never held this layer)
      if (mArtOpt.on)
         return BeginArt(id, brush, sx, sy, *src);
      brush.sourceTex = src->tex;
   }
   if (brush.mode == BrushEngine::kPatternStamp)
   {
      brush.sourceTex = mPattern.tex;
      if (!brush.patternAligned)
      {
         // Not aligned: the tile starts at this stroke's first dab; a new stroke starts it again. An explicit phase
         // still shifts it, in both modes.
         // Not aligned: each axis starts from half its tile plus one pixel, mirrored about the stroke's first point, so
         // one phase holds for the whole stroke whatever the tip size (ported from PhotoCraft's stamp_phase, Apache 2.0).
         // An explicit phase still shifts it, in both modes.
         auto origin = [](float first, int edge) {
            const float e = (float)std::max(1, edge);
            const float o = std::fmod((float)(edge / 2 + 1) - first, e);
            return o < 0.0f ? o + e : o;
         };
         brush.patternPhase[0] -= origin(sx, mPattern.w);
         brush.patternPhase[1] -= origin(sy, mPattern.h);
      }
      if (brush.impressionist && !mPatternCpu.empty() && mPattern.w > 0 && mPattern.h > 0)
      {
         // Impressionist paints each dab in the tile's colour under it, jittered by the stroke's seed; it paints as in
         // Normal, so Behind and the other modes are ignored (as PhotoCraft does).
         brush.patternRgba = mPatternCpu.data();
         brush.patternW = mPattern.w;
         brush.patternH = mPattern.h;
         brush.mode = BrushEngine::kNormal;
         brush.sourceBlend = 0;
         brush.sourceTex = 0;
      }
   }
   ApplyBrushSources(brush, true);
   mStroke.SetMirrors(SurfaceMirrors(target == 2 ? Layer() : *l, target));
   mBgSnap.clear();
   mBgPts.clear();
   if (target == 0 && brush.mode == BrushEngine::kBgErase && brush.bgContiguous)
   {
      ReadRect(*surf, 0, 0, surf->w, surf->h, mBgSnap);
      mBgPts = { sx, sy };
   }
   mStroke.Begin(*surf, brush, sx, sy, pressure, sel);
   return mStroke.Active();
}

void Scene2DNode::ApplyBrushSources(BrushEngine::Brush& brush, bool snapshot)
{
   unsigned* texOf[3] = { &brush.tipTex, &brush.grainTex, &brush.colorTex };
   for (int k = 0; k < 3; k++)
   {
      *texOf[k] = 0;
      INode* n = BrushSource(k);
      if (n == nullptr)
         continue;
      n->CookIfNeeded(std::max(0, mLastCookFrame));
      unsigned tex = n->GetOutputTexture();
      const int w = n->GetOutputWidth(), h = n->GetOutputHeight();
      // Frozen: the stroke keeps the picture the node had when it started.
      if (tex != 0 && snapshot && !mDoc.brushLive)
         tex = BrushEngine::Snapshot(tex, w, h, mBrushFrozen[k]) ? mBrushFrozen[k].tex : 0;
      *texOf[k] = tex;
      if (k == 1)
      {
         brush.grainTexW = w;
         brush.grainTexH = h;
      }
   }
}

void Scene2DNode::PaintTo(float x, float y, float pressure)
{
   if (mArt.on)
   {
      const Layer* al = FindConst(mArt.id);
      float sx = 0, sy = 0, scale = 1;
      if (al != nullptr && DocToSurface(*al, 0, x, y, sx, sy, scale))
      {
         mArt.pts.push_back(sx);
         mArt.pts.push_back(sy);
         ArtStamp(false);
      }
      return;
   }
   if (!mStroke.Active())
      return;
   const Layer* l = FindConst(mPaintId);
   if (mPaintTarget != 2 && l == nullptr)
      return;
   float sx = 0, sy = 0, scale = 1;
   if (DocToSurface(mPaintTarget == 2 ? Layer() : *l, mPaintTarget, x, y, sx, sy, scale))
   {
      mStroke.MoveTo(sx, sy, pressure);
      if (!mBgSnap.empty())
      {
         mBgPts.push_back(sx);
         mBgPts.push_back(sy);
      }
   }
}

// Contiguous background erase: the erasable colour is flooded from the stroke's path over the picture as it was before
// the stroke, and every pixel the stroke changed that the flood did not reach is put back (PhotoCraft background eraser
// Limits: Contiguous).
void Scene2DNode::BgContiguousRestore(GLUtil::Fbo& fbo, int dirty[4])
{
   const int w = fbo.w, h = fbo.h;
   if (mBgSnap.size() != (size_t)w * h * 4 || mBgPts.size() < 2)
      return;
   const BrushEngine::Brush& b = mStroke.CurrentBrush();
   float tol = std::max(0.005f, b.keyTolerance * 1.7320508f);   // as the merge shader
   auto dist = [](const float* a, const float* c) {
      return std::sqrt((a[0] - c[0]) * (a[0] - c[0]) + (a[1] - c[1]) * (a[1] - c[1]) + (a[2] - c[2]) * (a[2] - c[2]));
   };
   if (b.protectFg)
      tol = std::min(tol, dist(b.key, b.color));
   auto erasable = [&](size_t i) {
      const float c[3] = { mBgSnap[i * 4] / 255.0f, mBgSnap[i * 4 + 1] / 255.0f, mBgSnap[i * 4 + 2] / 255.0f };
      return mBgSnap[i * 4 + 3] > 0 && dist(c, b.key) < tol;
   };
   std::vector<uint8_t> reached((size_t)w * h, 0);
   std::vector<int> stack;
   auto seed = [&](float fx, float fy) {
      const int x = (int)std::floor(fx), y = (int)std::floor(fy);
      if (x < 0 || y < 0 || x >= w || y >= h)
         return;
      const size_t i = (size_t)y * w + x;
      if (!reached[i] && erasable(i))
      {
         reached[i] = 1;
         stack.push_back((int)i);
      }
   };
   seed(mBgPts[0], mBgPts[1]);
   for (size_t k = 2; k + 1 < mBgPts.size(); k += 2)
   {
      const float ax = mBgPts[k - 2], ay = mBgPts[k - 1], bx = mBgPts[k], by = mBgPts[k + 1];
      const int n = std::max(1, (int)std::ceil(std::max(std::fabs(bx - ax), std::fabs(by - ay))));
      for (int j = 1; j <= n; j++)
         seed(ax + (bx - ax) * j / n, ay + (by - ay) * j / n);
   }
   while (!stack.empty())
   {
      const int i = stack.back();
      stack.pop_back();
      const int x = i % w, y = i / w;
      const int nx[4] = { x - 1, x + 1, x, x }, ny[4] = { y, y, y - 1, y + 1 };
      for (int q = 0; q < 4; q++)
         if (nx[q] >= 0 && ny[q] >= 0 && nx[q] < w && ny[q] < h)
         {
            const size_t j = (size_t)ny[q] * w + nx[q];
            if (!reached[j] && erasable(j))
            {
               reached[j] = 1;
               stack.push_back((int)j);
            }
         }
   }
   std::vector<uint8_t> px;
   ReadRect(fbo, 0, 0, w, h, px);
   bool put = false;
   for (int y = std::max(0, dirty[1]); y < std::min(h, dirty[3]); y++)
      for (int x = std::max(0, dirty[0]); x < std::min(w, dirty[2]); x++)
      {
         const size_t i = (size_t)y * w + x;
         if (!reached[i] && std::memcmp(&px[i * 4], &mBgSnap[i * 4], 4) != 0)
         {
            std::memcpy(&px[i * 4], &mBgSnap[i * 4], 4);
            put = true;
         }
      }
   if (put)
      WriteRect(fbo, 0, 0, w, h, px.data());
}

void Scene2DNode::PaintFollow(const BrushEngine::Brush& b)
{
   if (!mStroke.Active() || mPaintTarget == 2)
      return;
   const Layer* l = FindConst(mPaintId);
   const BrushEngine::Brush& cur = mStroke.CurrentBrush();
   float sx = 0, sy = 0, scale = 1;
   if (l == nullptr || !DocToSurface(*l, mPaintTarget, 0.0f, 0.0f, sx, sy, scale))
      return;
   if (cur.size != b.size * scale || cur.opacity != b.opacity || cur.flow != b.flow)
      mStroke.Follow(b.size * scale, b.opacity, b.flow);
}

bool Scene2DNode::RepaintStroke(const std::vector<float>& xy)
{
   // The stroke lives in its own coverage until EndPaint merges it, so dropping it leaves the layer untouched.
   if (!mStroke.Active() || xy.size() < 4 || mPaintTarget == 2)
      return false;
   const int id = mPaintId, target = mPaintTarget;
   const BrushEngine::Brush b = mPaintBrushIn;
   mStroke.Cancel();
   mSkinStroke.on = false;
   mPaintId = 0;
   if (!BeginPaint(id, target, b, xy[0], xy[1], 1.0f))
      return false;
   for (size_t i = 2; i + 1 < xy.size(); i += 2)
      PaintTo(xy[i], xy[i + 1], 1.0f);
   return true;
}

void Scene2DNode::EndPaint()
{
   mSkinStroke.on = false;
   if (mArt.on)
   {
      EndArt();
      return;
   }
   if (!mStroke.Active())
      return;
   Layers();
   int dirty[4];
   if (mPaintTarget == 2)
   {
      mStroke.End(mSelTex, dirty);
      std::vector<uint8_t> px;
      ReadRect(mSelTex, 0, 0, mDoc.w, mDoc.h, px);
      mSel.resize((size_t)mDoc.w * mDoc.h);
      mSelAny = false;
      for (size_t i = 0; i < mSel.size(); i++)
      {
         mSel[i] = px[i * 4];
         mSelAny = mSelAny || mSel[i] != 0;
      }
      mPaintId = 0;
      mPaintEdits++;
      mSelDirty = true;
      StoreSelection();
      return;
   }
   Layer* l = FindLayer(mPaintId);
   auto& m = mPaintTarget == 0 ? mSurfaces : mPaintTarget == 3 ? mFxMasks : mMasks;
   auto it = m.find(mPaintId);
   if (l == nullptr || it == m.end())
   {
      mStroke.Cancel();
      mPaintId = 0;
      return;
   }
   const bool inpaint = mStroke.CurrentBrush().mode == BrushEngine::kInpaint && mPaintTarget == 0;
   mStroke.End(it->second.fbo, dirty);
   if (!mBgSnap.empty() && mPaintTarget == 0)
      BgContiguousRestore(it->second.fbo, dirty);
   mBgSnap.clear();
   mBgPts.clear();
   if (inpaint && dirty[2] > dirty[0] && dirty[3] > dirty[1])
   {
      // Inpainting: where the stroke went is rebuilt from around it.
      const GLUtil::Fbo& cov = mStroke.Coverage();
      const int w = it->second.fbo.w, h = it->second.fbo.h, cw = std::min(w, cov.w), ch = std::min(h, cov.h);
      std::vector<uint8_t> px, cv, mask((size_t)w * h, 0);
      ReadRect(it->second.fbo, 0, 0, w, h, px);
      ReadRect(cov, 0, 0, cw, ch, cv);
      for (int y = 0; y < ch; y++)
         for (int x = 0; x < cw; x++)
            mask[(size_t)y * w + x] = cv[((size_t)y * cw + x) * 4 + 3] > 20 ? 255 : 0;
      const bool fixedSrc = sHealFixed && (sHealOff[0] != 0 || sHealOff[1] != 0);
      if (HealRegion(px, w, h, mask, fixedSrc ? sHealOff[0] : 0, fixedSrc ? sHealOff[1] : 0, !fixedSrc))
      {
         WriteRect(it->second.fbo, 0, 0, w, h, px.data());
         dirty[0] = dirty[1] = 0;
         dirty[2] = w;
         dirty[3] = h;
      }
   }
   if (dirty[2] > dirty[0] && dirty[3] > dirty[1])
   {
      StoreTiles(it->second, mPaintTarget == 0 ? l->tiles : mPaintTarget == 3 ? l->fmtiles : l->mtiles, dirty);
      Commit();
   }
   mPaintId = 0;
}

// The bucket's region in the document, bottom row first as the selection; false when the click fills nothing.
bool Scene2DNode::BucketRegion(int id, float x, float y, int tolerance, bool contiguous, bool sampleAll, std::vector<uint8_t>& region)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0 || !EnsurePaintShaders())
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   const int W = mDoc.w, H = mDoc.h;
   // The region is found in the document: from the picture, or from the layer drawn alone.
   std::vector<uint8_t> px;
   float m[6];
   LayerMatrix(*l, m);
   const bool aligned = std::fabs(m[0] - 1) < 1e-4f && std::fabs(m[4] - 1) < 1e-4f && std::fabs(m[1]) < 1e-4f &&
                        std::fabs(m[3]) < 1e-4f && std::fabs(m[2]) < 1e-3f && std::fabs(m[5]) < 1e-3f && l->pw == W && l->ph == H;
   if (sampleAll || !aligned)
   {
      if (mOut.fbo == 0)
         return false;   // nothing composed yet to sample
      ReadRect(mOut, 0, 0, W, H, px);
   }
   else
      ReadRect(it->second.fbo, 0, 0, W, H, px);
   region.clear();
   sMatchSoft = wandAntialias;
   MatchRegion(px, W, H, (int)std::floor(x), H - 1 - (int)std::floor(y), tolerance, contiguous, region);
   if (std::none_of(region.begin(), region.end(), [](uint8_t v) { return v != 0; }))
      return false;
   // A click on a pixel the selection leaves out fills nothing, even when its region reaches the selection
   // (Photoshop; PhotoCraft a_click_outside_the_selection_adds_no_state_even_when_its_region_reaches_it).
   {
      const int cx = (int)std::floor(x), cr = H - 1 - (int)std::floor(y);
      if (mSelAny && mSel.size() == region.size() && cx >= 0 && cx < W && cr >= 0 && cr < H && mSel[(size_t)cr * W + cx] == 0)
         return false;
   }
   // A region the selection hides wholly changes nothing (and so is no undo step).
   if (mSelAny && mSel.size() == region.size())
   {
      bool meets = false;
      for (size_t i = 0; i < region.size() && !meets; i++)
         meets = region[i] != 0 && mSel[i] != 0;
      if (!meets)
         return false;
   }
   return true;
}

bool Scene2DNode::Bucket(int id, float x, float y, const float rgb[3], float opacity, int tolerance, bool contiguous, bool sampleAll)
{
   std::vector<uint8_t> region;
   if (!BucketRegion(id, x, y, tolerance, contiguous, sampleAll, region))
      return false;
   Layer* l = FindLayer(id);
   auto it = mSurfaces.find(id);
   const int W = mDoc.w, H = mDoc.h;
   GLUtil::Fbo cov;
   if (!GLUtil::EnsureFbo(cov, W, H, GL_RGBA8, "Scene2DBucket"))
      return false;
   std::vector<uint8_t> rgba((size_t)W * H * 4);
   for (size_t i = 0; i < region.size(); i++)
      rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = region[i];
   WriteRect(cov, 0, 0, W, H, rgba.data());
   UploadSelection();
   const float col[4] = { rgb[0], rgb[1], rgb[2], 1.0f };
   ApplyFill(it->second, l->pw, l->ph, *l, cov.tex, col, col, 0, nullptr, nullptr, opacity);
   GLUtil::DestroyFbo(cov);
   const int all[4] = { 0, 0, l->pw, l->ph };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

// Bucket with a pattern (Photoshop's Fill: Pattern): the region, inside the selection, becomes a stroke's selection
// for one hard Pattern Stamp dab over the whole layer, aligned to the document so neighbouring fills meet.
bool Scene2DNode::BucketPattern(int id, float x, float y, float opacity, int tolerance, bool contiguous, bool sampleAll, float scale)
{
   if (mStroke.Active() || mPattern.tex == 0)
      return false;
   std::vector<uint8_t> region;
   if (!BucketRegion(id, x, y, tolerance, contiguous, sampleAll, region))
      return false;
   const Layer* l = FindConst(id);
   if (mSelAny && mSel.size() == region.size())
      for (size_t i = 0; i < region.size(); i++)
         region[i] = (uint8_t)(region[i] * mSel[i] / 255);
   std::vector<uint8_t> keepSel = mSel;
   const bool keepAny = mSelAny;
   mSel.swap(region);
   mSelAny = true;
   mSelDirty = true;
   BrushEngine::Brush b;
   b.mode = BrushEngine::kPatternStamp;
   b.hardness = 1.0f;
   b.opacity = b.flow = std::clamp(opacity, 0.0f, 1.0f);
   b.patternAligned = true;
   b.patternScale = std::max(scale, kPatternScaleMin);
   b.patternPhase[0] = b.patternPhase[1] = 0.0f;
   const float w = (float)mDoc.w, h = (float)mDoc.h;
   b.size = 4.0f * std::sqrt(w * w + h * h) + 4.0f * std::sqrt((float)l->pw * l->pw + (float)l->ph * l->ph);
   const bool ok = BeginPaint(id, 0, b, w * 0.5f, h * 0.5f, 1.0f);
   if (ok)
      EndPaint();
   mSel.swap(keepSel);
   mSelAny = keepAny;
   mSelDirty = true;
   return ok;
}

bool Scene2DNode::Gradient(int id, float ax, float ay, float bx, float by, const float a[4], const float b[4], int kind, float opacity,
                           int extend, bool dither, int method, float smoothness)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0 || !EnsurePaintShaders())
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   UploadSelection();
   const float p0[2] = { ax, ay }, p1[2] = { bx, by };
   mGradExtend = std::clamp(extend, 0, 2);
   mGradDither = dither;
   mGradMethod = method == 1 && smoothness >= 0.999f ? 1 : 0;
   if (method != 0)
      std::fprintf(stderr, "PROBE gradient method %d smoothness %.2f: %s\n", method, smoothness, mGradMethod == 1 ? "smooth curves" : "straight");
   ApplyFill(it->second, l->pw, l->ph, *l, 0, a, b, std::clamp(kind, 1, 5), p0, p1, opacity);
   mGradExtend = 0;
   mGradDither = false;
   mGradMethod = 0;
   const int all[4] = { 0, 0, l->pw, l->ph };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

bool Scene2DNode::ClearPixels(int id)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll | kLockAlpha)) != 0 || !EnsurePaintShaders())
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   UploadSelection();
   ApplyFill(it->second, l->pw, l->ph, *l, 0, nullptr, nullptr, -3, nullptr, nullptr, 1.0f);
   const int all[4] = { 0, 0, l->pw, l->ph };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

bool Scene2DNode::FillPixels(int id, const float rgb[3], float opacity)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0 || !EnsurePaintShaders())
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   UploadSelection();
   const float col[4] = { rgb[0], rgb[1], rgb[2], 1.0f };
   ApplyFill(it->second, l->pw, l->ph, *l, 0, col, col, 0, nullptr, nullptr, opacity);
   const int all[4] = { 0, 0, l->pw, l->ph };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

bool Scene2DNode::OffsetPixels(int id, int dx, int dy, bool wrap)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end() || l->pw <= 0 || l->ph <= 0 || (dx == 0 && dy == 0))
      return false;
   const int W = l->pw, H = l->ph;
   std::vector<uint8_t> px;
   ReadRect(it->second.fbo, 0, 0, W, H, px);
   // The tile that moves, in surface px (rows bottom first): the whole surface, or for a wrap of a plain layer that
   // runs past the canvas, the part on the canvas (PhotoCraft's contained wrap): what lies outside stays where it is.
   int tx0 = 0, ty0 = 0, tw = W, th = H;
   if (wrap && l->sx == 1.0f && l->sy == 1.0f && l->angle == 0.0f && l->shear == 0.0f && !l->flipX && !l->flipY && !l->is3d)
   {
      const float fx = l->x - l->ax * W, fy = l->y - l->ay * H;
      const int ox = (int)std::lround(fx), oy = (int)std::lround(fy);
      if (std::fabs(fx - ox) < 1e-3f && std::fabs(fy - oy) < 1e-3f)
      {
         // Canvas columns [-ox, W-ox); canvas rows top-down [-oy, H-oy) are surface rows (H-1-y).
         const int cx0 = std::max(0, -ox), cx1 = std::min(W, mDoc.w - ox);
         const int cy0 = std::max(0, -oy), cy1 = std::min(H, mDoc.h - oy);
         if (cx1 > cx0 && cy1 > cy0)
         {
            tx0 = cx0;
            tw = cx1 - cx0;
            ty0 = H - cy1;
            th = cy1 - cy0;
         }
      }
   }
   std::vector<uint8_t> out = px;
   for (int y = ty0; y < ty0 + th; y++)
      std::memset(&out[((size_t)y * W + tx0) * 4], 0, (size_t)tw * 4);
   // Surface rows run bottom first, so a move down is a move to a lower row.
   for (int y = 0; y < th; y++)
   {
      int sy = y + dy;   // destination row y takes source row y + dy (bottom-first rows)
      if (wrap)
         sy = ((sy % th) + th) % th;
      else if (sy < 0 || sy >= th)
         continue;
      for (int x = 0; x < tw; x++)
      {
         int sx = x - dx;
         if (wrap)
            sx = ((sx % tw) + tw) % tw;
         else if (sx < 0 || sx >= tw)
            continue;
         std::memcpy(&out[((size_t)(ty0 + y) * W + tx0 + x) * 4], &px[((size_t)(ty0 + sy) * W + tx0 + sx) * 4], 4);
      }
   }
   WriteRect(it->second.fbo, 0, 0, W, H, out.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(it->second, l->tiles, all);
   // The vector mask goes with its pixels (PhotoCraft: a linked vector mask wraps with the raster): a shape wholly
   // inside the moving tile moves by the same step and, under wrap, a shape pushed wholly past the tile comes back
   // on the other side. One outside the tile stays where it is, as its pixels do.
   if (l->hasVMask && !l->vmask.empty())
   {
      float bw = 1, bh = 1;
      LayerBox(*l, bw, bh);
      const float kx = bw / (float)W, ky = bh / (float)H;
      const float t0x = tx0 * kx, t1x = (tx0 + tw) * kx, t0y = (H - ty0 - th) * ky, t1y = (H - ty0) * ky;
      const float mx = dx * kx, my = dy * ky, e = 1e-3f;
      for (Shape& sh : l->vmask)
      {
         float lo[2], hi[2];
         if (!ShapeBounds(sh, lo, hi) || lo[0] < t0x - e || hi[0] > t1x + e || lo[1] < t0y - e || hi[1] > t1y + e)
            continue;
         float sx = mx, sy = my;
         if (wrap)
         {
            const float wx = t1x - t0x, wy = t1y - t0y;
            if (lo[0] + sx >= t1x - e)
               sx -= wx;
            else if (hi[0] + sx <= t0x + e)
               sx += wx;
            if (lo[1] + sy >= t1y - e)
               sy -= wy;
            else if (hi[1] + sy <= t0y + e)
               sy += wy;
         }
         sh.x0 += sx;
         sh.x1 += sx;
         sh.y0 += sy;
         sh.y1 += sy;
         for (size_t i = 0; i < sh.pts.size(); i++)
            sh.pts[i] += (i % 2) == 0 ? sx : sy;
      }
   }
   Commit();
   return true;
}

void Scene2DNode::ClearPaintOffCanvas()
{
   // Delete Cropped Pixels (PhotoCraft image_cmds: crop with delete): only pixel data goes. A surface pixel whose
   // footprint lies wholly outside the canvas is cleared; one that touches it stays, so what shows is unchanged.
   SyncSurfaces();
   const float W = (float)mDoc.w, H = (float)mDoc.h;
   for (Layer& l : mLayers)
   {
      if (l.kind != Kind::Paint || l.pw <= 0 || l.ph <= 0 || l.warpOn || l.is3d)
         continue;
      auto it = mSurfaces.find(l.id);
      if (it == mSurfaces.end())
         continue;
      float bw = 1, bh = 1, m[6];
      LayerBox(l, bw, bh);
      LayerMatrix(l, m);
      const int sw = l.pw, sh = l.ph;
      const float kx = bw / (float)sw, ky = bh / (float)sh;
      // A pixel's footprint reaches at most half its turned diagonal from its centre.
      const float reach = 0.5f * (std::fabs(m[0]) * kx + std::fabs(m[1]) * ky + std::fabs(m[3]) * kx + std::fabs(m[4]) * ky) + 1.0f;
      std::vector<uint8_t> px;
      ReadRect(it->second.fbo, 0, 0, sw, sh, px);
      bool any = false;
      for (int row = 0; row < sh; row++)
      {
         const float by = ((float)(sh - 1 - row) + 0.5f) * ky;   // rows bottom first; the box runs y down
         for (int x = 0; x < sw; x++)
         {
            uint8_t* p = &px[((size_t)row * sw + x) * 4];
            if (p[3] == 0)
               continue;
            const float bx = ((float)x + 0.5f) * kx;
            const float dx = m[0] * bx + m[1] * by + m[2], dy = m[3] * bx + m[4] * by + m[5];
            if (dx < -reach || dy < -reach || dx > W + reach || dy > H + reach)
            {
               std::memset(p, 0, 4);
               any = true;
            }
         }
      }
      if (!any)
         continue;
      WriteRect(it->second.fbo, 0, 0, sw, sh, px.data());
      const int all[4] = { 0, 0, sw, sh };
      StoreTiles(it->second, l.tiles, all);
      std::fprintf(stderr, "PROBE crop delete layer %d cleared off-canvas pixels\n", l.id);
   }
}

bool Scene2DNode::BucketSelection(float x, float y, float value, int tolerance, bool contiguous)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   if (mSel.size() != (size_t)W * H)
      mSel.assign((size_t)W * H, 0);
   std::vector<uint8_t> px((size_t)W * H * 4);
   for (size_t i = 0; i < mSel.size(); i++)
   {
      px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = mSel[i];
      px[i * 4 + 3] = 255;
   }
   std::vector<uint8_t> region;
   sMatchSoft = wandAntialias;
   MatchRegion(px, W, H, (int)std::floor(x), H - 1 - (int)std::floor(y), tolerance, contiguous, region);
   if (std::none_of(region.begin(), region.end(), [](uint8_t v) { return v != 0; }))
      return false;
   const int v = (int)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f);
   for (size_t i = 0; i < mSel.size(); i++)
      mSel[i] = (uint8_t)((mSel[i] * (255 - region[i]) + v * region[i]) / 255);
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t s) { return s != 0; });
   mSelDirty = true;
   StoreSelection();
   return true;
}

bool Scene2DNode::GradientSelection(float ax, float ay, float bx, float by, float va, float vb, int kind)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   const float dx = bx - ax, dy = by - ay, len2 = std::max(dx * dx + dy * dy, 1e-6f), len = std::sqrt(len2);
   mSel.assign((size_t)W * H, 0);
   for (int y = 0; y < H; y++)
   {
      uint8_t* row = &mSel[(size_t)(H - 1 - y) * W];
      for (int x = 0; x < W; x++)
      {
         const float qx = x + 0.5f - ax, qy = y + 0.5f - ay;
         float t = 0.0f;
         if (kind == 2)
            t = std::sqrt(qx * qx + qy * qy) / std::max(len, 1e-3f);
         else if (kind == 3)
         {
            t = (std::atan2(qy, qx) - std::atan2(dy, dx)) / 6.2831853f + 1.0f;
            t -= std::floor(t);
         }
         else if (kind == 4)
            t = std::fabs(qx * dx + qy * dy) / len2;
         else if (kind == 5)
            t = (std::fabs(qx * dx + qy * dy) + std::fabs(qy * dx - qx * dy)) / len2;
         else
            t = (qx * dx + qy * dy) / len2;
         t = std::clamp(t, 0.0f, 1.0f);
         row[x] = (uint8_t)std::lround(std::clamp(va + (vb - va) * t, 0.0f, 1.0f) * 255.0f);
      }
   }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t s) { return s != 0; });
   mSelDirty = true;
   StoreSelection();
   return true;
}

// ---- the selection in the doc text ---------------------------------------

namespace
{
   // One key per 256 px tile: "-" nothing selected, "+" all of it, else a TileStore key (grey in RGB).
   std::vector<std::string> SelEncode(const std::vector<uint8_t>& sel, int w, int h)
   {
      std::vector<std::string> keys;
      std::vector<uint8_t> px;
      for (int y0 = 0; y0 < h; y0 += kT)
         for (int x0 = 0; x0 < w; x0 += kT)
         {
            const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
            bool none = true, all = true;
            for (int y = 0; y < th && (none || all); y++)
            {
               const uint8_t* row = &sel[(size_t)(y0 + y) * w + x0];
               for (int x = 0; x < tw; x++)
               {
                  none = none && row[x] == 0;
                  all = all && row[x] == 255;
               }
            }
            if (none || all)
            {
               keys.push_back(none ? "-" : "+");
               continue;
            }
            px.resize((size_t)tw * th * 4);
            for (int y = 0; y < th; y++)
               for (int x = 0; x < tw; x++)
               {
                  uint8_t* o = &px[((size_t)y * tw + x) * 4];
                  o[0] = o[1] = o[2] = sel[(size_t)(y0 + y) * w + x0 + x];
                  o[3] = 255;
               }
            keys.push_back(TileStore::Put(px.data(), tw, th));
         }
      return keys;
   }

   bool SelDecode(const std::vector<std::string>& keys, int w, int h, std::vector<uint8_t>& sel)
   {
      const int cols = (w + kT - 1) / kT, rows = (h + kT - 1) / kT;
      if (w <= 0 || h <= 0 || keys.size() != (size_t)cols * rows)
         return false;
      sel.assign((size_t)w * h, 0);
      std::vector<uint8_t> px;
      for (size_t i = 0; i < keys.size(); i++)
      {
         const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
         const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
         if (keys[i] == "-")
            continue;
         int gw = 0, gh = 0;
         const bool full = keys[i] == "+";
         if (!full && (!TileStore::Get(keys[i], px, gw, gh) || gw != tw || gh != th))
            continue;
         for (int y = 0; y < th; y++)
            for (int x = 0; x < tw; x++)
               sel[(size_t)(y0 + y) * w + x0 + x] = full ? 255 : px[((size_t)y * tw + x) * 4];
      }
      return true;
   }
}

void Scene2DNode::StoreSelection()
{
   mSelStore = SelStore();
   if (mSelAny && mSel.size() == (size_t)mDoc.w * mDoc.h)
   {
      mSelStore.w = mDoc.w;
      mSelStore.h = mDoc.h;
      mSelStore.keys = SelEncode(mSel, mDoc.w, mDoc.h);
   }
   if (!mSelApron.a.empty() && !ApronMatches())
      mSelApron = SelApron();
   mSelApronStore = ApronStore();
   if (!mSelApron.a.empty())
   {
      mSelApronStore.x0 = mSelApron.x0;
      mSelApronStore.y0 = mSelApron.y0;
      mSelApronStore.w = mSelApron.w;
      mSelApronStore.h = mSelApron.h;
      mSelApronStore.keys = SelEncode(mSelApron.a, mSelApron.w, mSelApron.h);
   }
   Commit();
}

// Called by Parse: the selection the text names (none when it names none, or the document changed size).
void Scene2DNode::RestoreSelection()
{
   std::vector<uint8_t> sel;
   const bool had = mSelAny;
   if (mSelStore.w == mDoc.w && mSelStore.h == mDoc.h && SelDecode(mSelStore.keys, mDoc.w, mDoc.h, sel))
   {
      mSel.swap(sel);
      mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   }
   else
   {
      mSelStore = SelStore();
      mSel.clear();
      mSelAny = false;
   }
   mSelApron = SelApron();
   std::vector<uint8_t> a;
   const ApronStore& as = mSelApronStore;
   if (as.w > 0 && as.h > 0 && SelDecode(as.keys, as.w, as.h, a))
   {
      mSelApron = SelApron{ as.x0, as.y0, as.w, as.h, std::move(a) };
      if (!ApronMatches())
         mSelApron = SelApron();
   }
   if (mSelApron.a.empty())
      mSelApronStore = ApronStore();
   if (had || mSelAny)
      mSelDirty = true;
}

std::vector<std::string> Scene2DNode::SavedSelections() const
{
   std::vector<std::string> names;
   for (const SelStore& s : mSelSaved)
      names.push_back(s.name);
   return names;
}

void Scene2DNode::SaveSelection(const std::string& name)
{
   Layers();
   if (!mSelAny || name.empty() || mSel.size() != (size_t)mDoc.w * mDoc.h)
      return;
   SelStore s;
   s.name = name;
   s.w = mDoc.w;
   s.h = mDoc.h;
   s.keys = SelEncode(mSel, mDoc.w, mDoc.h);
   auto it = std::find_if(mSelSaved.begin(), mSelSaved.end(), [&](const SelStore& o) { return o.name == name; });
   if (it != mSelSaved.end())
      *it = s;
   else
      mSelSaved.push_back(s);
   Commit();
}

bool Scene2DNode::LoadSelection(const std::string& name, int op)
{
   Layers();
   auto it = std::find_if(mSelSaved.begin(), mSelSaved.end(), [&](const SelStore& o) { return o.name == name; });
   std::vector<uint8_t> t;
   if (it == mSelSaved.end() || it->w != mDoc.w || it->h != mDoc.h || !SelDecode(it->keys, it->w, it->h, t))
      return false;
   CombineSelection(t, op);
   return true;
}

void Scene2DNode::DeleteSavedSelection(const std::string& name)
{
   Layers();
   mSelSaved.erase(std::remove_if(mSelSaved.begin(), mSelSaved.end(), [&](const SelStore& o) { return o.name == name; }), mSelSaved.end());
   Commit();
}

bool Scene2DNode::PickBox(int px, int py, int size, float zoom, int w, int h, int& x0, int& y0, int& x1, int& y1)
{
   size = std::clamp(size, 1, kPickSizeMax);
   if (size == 1 || !(zoom > 0.0f) || zoom > 1.0f)
   {
      const int half = size / 2;
      x0 = px - half;
      y0 = py - half;
      x1 = px + half + 1;
      y1 = py + half + 1;
   }
   else
   {
      // (N+1) screen pixels in document pixels, at least N+1; the pixel sits just left of the middle.
      const int side = (int)std::clamp(std::round((size + 1) / (double)zoom), (double)(size + 1), (double)kPickBoxMax);
      const int before = (side + 1) / 2 - 1;
      x0 = px - before;
      y0 = py - before;
      x1 = x0 + side;
      y1 = y0 + side;
   }
   x0 = std::max(0, x0);
   y0 = std::max(0, y0);
   x1 = std::min(w, x1);
   y1 = std::min(h, y1);
   return x1 > x0 && y1 > y0;
}

bool Scene2DNode::Pick(float x, float y, float rgb[3], int size, int layer, float zoom)
{
   // Where to read: the picture (y flipped to texture rows) or one paint layer's own pixels.
   const GLUtil::Fbo* src = &mOut;
   int px = (int)std::floor(x), py = mDoc.h - 1 - (int)std::floor(y);
   if (layer != 0)
   {
      Layers();
      SyncSurfaces();   // a document just re-read from its text (undo, a snapshot) reads its new pixels, not the last frame's
      const Layer* l = FindConst(layer);
      auto it = mSurfaces.find(layer);
      float sx = 0, sy = 0, scale = 1;
      if (l != nullptr && l->kind == Kind::Paint && it != mSurfaces.end() && DocToSurface(*l, 0, x, y, sx, sy, scale))
      {
         src = &it->second.fbo;
         // Mid-stroke the layer shows the stroke merged over it: read what is on screen, as the commit will keep it.
         if (mStroke.Active() && mPaintId == layer && mPaintTarget == 0)
            src = &mStroke.DisplayFbo(it->second.fbo);
         px = (int)std::floor(sx);
         py = (int)std::floor(sy);
      }
   }
   if (src->fbo == 0 || px < 0 || py < 0 || px >= src->w || py >= src->h)
      return false;
   // The box in document orientation; the picture's rows run bottom up, so flip it back.
   const bool flip = src == &mOut;
   int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
   if (!PickBox(px, flip ? src->h - 1 - py : py, size, zoom, src->w, src->h, x0, y0, x1, y1))
      return false;
   if (flip)
   {
      const int r0 = src->h - y1;
      y1 = src->h - y0;
      y0 = r0;
   }
   std::vector<float> v((size_t)(x1 - x0) * (y1 - y0) * 4, 0.0f);
   GLint prev = 0;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, src->fbo);
   glReadPixels(x0, y0, x1 - x0, y1 - y0, GL_RGBA, GL_FLOAT, v.data());
   glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);
   // Averaged by alpha, so a transparent neighbour does not grey the colour.
   double sum[3] = { 0, 0, 0 }, wsum = 0;
   for (size_t i = 0; i < v.size(); i += 4)
   {
      const double w = src == &mOut ? 1.0 : std::clamp(v[i + 3], 0.0f, 1.0f);
      for (int k = 0; k < 3; k++)
         sum[k] += std::clamp(v[i + k], 0.0f, 1.0f) * w;
      wsum += w;
   }
   if (wsum <= 1e-6)
      return false;   // an empty spot on the layer: keep the colour
   for (int k = 0; k < 3; k++)
      rgb[k] = (float)(sum[k] / wsum);
   return true;
}

bool Scene2DNode::ReadOutput(std::vector<uint8_t>& rgba, int& w, int& h)
{
   if (mOut.fbo == 0)
      return false;
   w = mOut.w;
   h = mOut.h;
   ReadRect(mOut, 0, 0, w, h, rgba);
   return true;
}

bool Scene2DNode::ReadPaint(int id, std::vector<uint8_t>& rgba, int& w, int& h)
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind != Kind::Paint)
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   w = it->second.fbo.w;
   h = it->second.fbo.h;
   ReadRect(it->second.fbo, 0, 0, w, h, rgba);
   return true;
}

// ---- selection -------------------------------------------------------------

namespace
{
   // Even-odd scanline fill of a polygon given in document px (y down), into a bottom-row-first buffer.
   // aa: four sub-scanlines per row, each span weighed by how much of a pixel it covers, so the edge is one pixel of coverage.
   void FillPolygon(const std::vector<float>& xy, int w, int h, std::vector<uint8_t>& out, bool aa = false)
   {
      out.assign((size_t)w * h, 0);
      const size_t n = xy.size() / 2;
      if (n < 3)
         return;
      std::vector<float> xs, acc(aa ? (size_t)w : 0);
      const int subs = aa ? 4 : 1;
      for (int y = 0; y < h; y++)
      {
         uint8_t* row = &out[(size_t)(h - 1 - y) * w];
         if (aa)
            std::fill(acc.begin(), acc.end(), 0.0f);
         for (int s = 0; s < subs; s++)
         {
            const float cy = (float)y + ((float)s + 0.5f) / (float)subs;
            xs.clear();
            for (size_t i = 0; i < n; i++)
            {
               const float x0 = xy[i * 2], y0 = xy[i * 2 + 1];
               const float x1 = xy[((i + 1) % n) * 2], y1 = xy[((i + 1) % n) * 2 + 1];
               if ((y0 <= cy && y1 > cy) || (y1 <= cy && y0 > cy))
                  xs.push_back(x0 + (cy - y0) / (y1 - y0) * (x1 - x0));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
            {
               if (!aa)
               {
                  const int a = std::clamp((int)std::lround(xs[k]), 0, w), b = std::clamp((int)std::lround(xs[k + 1]), 0, w);
                  if (b > a)
                     std::memset(row + a, 255, (size_t)(b - a));
                  continue;
               }
               const float xa = std::clamp(xs[k], 0.0f, (float)w), xb = std::clamp(xs[k + 1], 0.0f, (float)w);
               if (xb <= xa)
                  continue;
               const int ia = (int)xa, ib = std::min((int)xb, w - 1);
               const float wt = 1.0f / (float)subs;
               if (ia == ib)
               {
                  acc[ia] += (xb - xa) * wt;
                  continue;
               }
               acc[ia] += ((float)(ia + 1) - xa) * wt;
               for (int x = ia + 1; x < ib; x++)
                  acc[x] += wt;
               if (ib < w)
                  acc[ib] += (xb - (float)ib) * wt;
            }
         }
         if (aa)
            for (int x = 0; x < w; x++)
               row[x] = (uint8_t)std::lround(std::clamp(acc[x], 0.0f, 1.0f) * 255.0f);
      }
   }
}

void Scene2DNode::PolygonMask(const std::vector<float>& xy, int w, int h, std::vector<uint8_t>& out, bool aa)
{
   FillPolygon(xy, w, h, out, aa);
}

void Scene2DNode::FeatherMask(std::vector<uint8_t>& a, int w, int h, float radius)
{
   Feather(a, w, h, radius);
}

void Scene2DNode::SelectPolygon(const std::vector<float>& xy, int op, float feather, bool aa)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> t;
   FillPolygon(xy, W, H, t, aa);
   if (feather > 0.5f)
      Feather(t, W, H, feather);
   CombineSelection(t, op);
}

void Scene2DNode::SelectRect(float x0, float y0, float x1, float y1, int op, float feather, bool ellipse, bool aa)
{
   if (x1 < x0) std::swap(x0, x1);
   if (y1 < y0) std::swap(y0, y1);
   if (ellipse)
   {
      SelectEllipse(x0, y0, x1, y1, op, feather, aa);
      return;
   }
   const std::vector<float> xy = { x0, y0, x1, y0, x1, y1, x0, y1 };
   SelectPolygon(xy, op, feather, false);   // Photoshop's Anti-alias only softens the ellipse
}

// The Elliptical Marquee as Photoshop draws it (PhotoCraft algo/selection/marquee.rs Ellipse, MIT/Apache-2.0):
// hard, a pixel is in when its centre is; anti-aliased, with F the normalised radius at the centre the coverage ramps
// linearly over the pixel's own width across the edge, (0.5 - (F - 1) F / (|dx|/rx^2 + |dy|/ry^2)). An ellipse at
// most 3 px across keeps 4 x 4 sub-samples.
void Scene2DNode::SelectEllipse(float x0, float y0, float x1, float y1, int op, float feather, bool aa)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> t((size_t)W * H, 0);
   const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5, rx = (x1 - x0) * 0.5, ry = (y1 - y0) * 0.5;
   if (rx > 0.0 && ry > 0.0)
   {
      const bool small = std::min(x1 - x0, y1 - y0) <= 3.0f;
      auto inside = [&](double px, double py) { const double u = (px - cx) / rx, v = (py - cy) / ry; return u * u + v * v <= 1.0; };
      const int ya = std::max(0, (int)std::floor(y0)), yb = std::min(H, (int)std::ceil(y1));
      const int xa = std::max(0, (int)std::floor(x0)), xb = std::min(W, (int)std::ceil(x1));
      for (int y = ya; y < yb; y++)
      {
         uint8_t* row = &t[(size_t)(H - 1 - y) * W];
         for (int x = xa; x < xb; x++)
         {
            double c;
            if (!aa)
               c = inside(x + 0.5, y + 0.5) ? 1.0 : 0.0;
            else if (small)
            {
               int n = 0;
               for (int i = 0; i < 16; i++)
                  n += inside(x + ((i % 4) + 0.5) / 4.0, y + ((i / 4) + 0.5) / 4.0) ? 1 : 0;
               c = n / 16.0;
            }
            else
            {
               const double dx = x + 0.5 - cx, dy = y + 0.5 - cy, u = dx / rx, v = dy / ry;
               const double f = std::sqrt(u * u + v * v), across = std::fabs(dx) / (rx * rx) + std::fabs(dy) / (ry * ry);
               c = across <= 0.0 ? 1.0 : std::clamp(0.5 - (f - 1.0) * f / across, 0.0, 1.0);
            }
            row[x] = (uint8_t)(c * 255.0 + 0.5);
         }
      }
   }
   if (feather > 0.5f)
      Feather(t, W, H, feather);
   CombineSelection(t, op);
}

void Scene2DNode::SelectWand(float x, float y, int tolerance, bool contiguous, int op, bool sampleAll, int layerId)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> px;
   const Layer* l = FindConst(layerId);
   auto it = l != nullptr && l->kind == Kind::Paint ? mSurfaces.find(layerId) : mSurfaces.end();
   if (!sampleAll && it != mSurfaces.end() && l->pw == W && l->ph == H)
      ReadRect(it->second.fbo, 0, 0, W, H, px);
   else if (mOut.fbo != 0 && mOut.w == W && mOut.h == H)
      ReadRect(mOut, 0, 0, W, H, px);
   else
      return;
   std::vector<uint8_t> region;
   sMatchSoft = wandAntialias;
   MatchRegion(px, W, H, (int)std::floor(x), H - 1 - (int)std::floor(y), tolerance, contiguous, region);
   CombineSelection(region, op);
}

void Scene2DNode::SelectAll()
{
   Layers();
   mSel.assign((size_t)mDoc.w * mDoc.h, 255);
   mSelAny = true;
   mSelDirty = true;
   StoreSelection();
}

void Scene2DNode::Deselect()
{
   Layers();   // a node Undo just rebuilt has not parsed yet; committing unparsed would write an empty document
   mSelApron = SelApron();
   if (mSelAny && !mSel.empty())
      mSelLast = mSel;
   mSel.clear();
   mSelAny = false;
   mSelDirty = true;
   StoreSelection();
}

bool Scene2DNode::Reselect()
{
   Layers();
   if (mSelLast.size() != (size_t)std::max(0, mDoc.w) * std::max(0, mDoc.h))
      return false;
   mSelApron = SelApron();
   mSel = mSelLast;
   mSelAny = true;
   mSelDirty = true;
   StoreSelection();
   return true;
}

void Scene2DNode::ReframeSelection(int w, int h, int dx, int dy)
{
   mSelApron = SelApron();
   if (!mSelAny || mSel.size() != (size_t)mDoc.w * mDoc.h)
   {
      mSel.clear();
      mSelAny = false;
      mSelDirty = true;
      return;
   }
   std::vector<uint8_t> out((size_t)w * h, 0);
   for (int r = 0; r < h; r++)   // rows bottom first, like the selection
   {
      const int sy = (h - 1 - r) - dy;   // the document row it came from, top down
      if (sy < 0 || sy >= mDoc.h)
         continue;
      const int sr = mDoc.h - 1 - sy;
      for (int x = 0; x < w; x++)
      {
         const int sx = x - dx;
         if (sx >= 0 && sx < mDoc.w)
            out[(size_t)r * w + x] = mSel[(size_t)sr * mDoc.w + sx];
      }
   }
   mSel = std::move(out);
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   if (!mSelAny)
      mSel.clear();
   mSelDirty = true;
}

bool Scene2DNode::ApronMatches() const
{
   const int W = mDoc.w, H = mDoc.h;
   const SelApron& p = mSelApron;
   if (p.a.empty() || p.a.size() != (size_t)p.w * p.h)
      return false;
   const bool have = mSelAny && mSel.size() == (size_t)W * H;
   for (int y = 0; y < H; y++)
   {
      const int ay = y - p.y0;
      const uint8_t* row = have ? &mSel[(size_t)(H - 1 - y) * W] : nullptr;
      for (int x = 0; x < W; x++)
      {
         const int ax = x - p.x0;
         const uint8_t a = ax >= 0 && ay >= 0 && ax < p.w && ay < p.h ? p.a[(size_t)ay * p.w + ax] : 0;
         if (a != (row != nullptr ? row[x] : 0))
            return false;
      }
   }
   return true;
}

Scene2DNode::SelApron Scene2DNode::ApronOrCanvas() const
{
   if (ApronMatches())
      return mSelApron;
   SelApron ap;
   const int W = mDoc.w, H = mDoc.h;
   if (!mSelAny || mSel.size() != (size_t)W * H)
      return ap;
   ap.w = W;
   ap.h = H;
   ap.a.resize((size_t)W * H);
   for (int y = 0; y < H; y++)
      std::copy_n(&mSel[(size_t)(H - 1 - y) * W], W, &ap.a[(size_t)y * W]);
   return ap;
}

void Scene2DNode::SetSelectionFromApron(SelApron ap)
{
   const int W = mDoc.w, H = mDoc.h;
   // Trim to what is selected, no further than one canvas past each edge.
   int x0 = std::max(ap.x0, -W), y0 = std::max(ap.y0, -H);
   int x1 = std::min(ap.x0 + ap.w, 2 * W), y1 = std::min(ap.y0 + ap.h, 2 * H);
   int bx0 = x1, by0 = y1, bx1 = x0, by1 = y0;
   for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++)
         if (ap.a[(size_t)(y - ap.y0) * ap.w + (x - ap.x0)] != 0)
         {
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x + 1);
            by1 = std::max(by1, y + 1);
         }
   mSel.assign((size_t)W * H, 0);
   mSelApron = SelApron();
   if (bx1 > bx0 && by1 > by0)
   {
      for (int y = std::max(by0, 0); y < std::min(by1, H); y++)
         for (int x = std::max(bx0, 0); x < std::min(bx1, W); x++)
            mSel[(size_t)(H - 1 - y) * W + x] = ap.a[(size_t)(y - ap.y0) * ap.w + (x - ap.x0)];
      if (bx0 < 0 || by0 < 0 || bx1 > W || by1 > H)
      {
         SelApron t;
         t.x0 = bx0;
         t.y0 = by0;
         t.w = bx1 - bx0;
         t.h = by1 - by0;
         t.a.resize((size_t)t.w * t.h);
         for (int y = 0; y < t.h; y++)
            std::copy_n(&ap.a[(size_t)(by0 + y - ap.y0) * ap.w + (bx0 - ap.x0)], t.w, &t.a[(size_t)y * t.w]);
         mSelApron = std::move(t);
      }
   }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   if (!mSelAny)
      mSel.clear();
   mSelDirty = true;
}

// The marquee moves by whole pixels; what passes the edge is kept (see SelApron).
void Scene2DNode::ShiftSelection(float dx, float dy)
{
   Layers();
   SelApron ap = ApronOrCanvas();
   if (ap.a.empty())
   {
      StoreSelection();
      return;
   }
   ap.x0 += (int)std::lround(dx);
   ap.y0 += (int)std::lround(dy);
   SetSelectionFromApron(std::move(ap));
   StoreSelection();
}

// ---- Type Mask -----------------------------------------------------------------------------------------------
// The text's own bitmap, read through the layer's box (bilinear, as it draws), over every document px its box covers:
// the canvas does not cut it. The text colour's alpha is taken out, so the glyphs select fully.
bool Scene2DNode::TextCoverage(int id, SelApron& out)
{
   Layers();
   out = SelApron();
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind != Kind::Text)
      return false;
   int tw = 0, th = 0;
   if (!TextSize(*l, tw, th))
      return true;   // no text: no glyphs
   auto it = mTextBoxes.find(id);
   float bw = 1, bh = 1;
   if (it == mTextBoxes.end() || it->second.rgba.size() != (size_t)tw * th * 4 || !LayerBox(*l, bw, bh))
      return false;
   const std::vector<uint8_t>& px = it->second.rgba;
   const Layer box = *l;
   float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
   for (int c = 0; c < 4; c++)
   {
      float x = 0, y = 0;
      BoxToDoc(box, (c == 1 || c == 2) ? bw : 0.0f, c >= 2 ? bh : 0.0f, x, y);
      lo[0] = std::min(lo[0], x);
      lo[1] = std::min(lo[1], y);
      hi[0] = std::max(hi[0], x);
      hi[1] = std::max(hi[1], y);
   }
   const int W = mDoc.w, H = mDoc.h;
   // No further than SetSelectionFromApron keeps (one canvas past each edge).
   const int x0 = std::max(-W, (int)std::floor(lo[0]) - 1), y0 = std::max(-H, (int)std::floor(lo[1]) - 1);
   const int x1 = std::min(2 * W, (int)std::ceil(hi[0]) + 1), y1 = std::min(2 * H, (int)std::ceil(hi[1]) + 1);
   if (x1 <= x0 || y1 <= y0)
      return true;
   const float alpha = std::clamp(l->textC[3], 1.0f / 255.0f, 1.0f);
   auto at = [&](int x, int y) -> float {
      return x < 0 || y < 0 || x >= tw || y >= th ? 0.0f : px[((size_t)y * tw + x) * 4 + 3];
   };
   out.x0 = x0;
   out.y0 = y0;
   out.w = x1 - x0;
   out.h = y1 - y0;
   out.a.assign((size_t)out.w * out.h, 0);
   for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++)
      {
         float bx = 0, by = 0;
         DocToBox(box, x + 0.5f, y + 0.5f, bx, by);
         const float u = bx * tw / bw - 0.5f, v = by * th / bh - 0.5f;
         const int iu = (int)std::floor(u), iv = (int)std::floor(v);
         const float fu = u - iu, fv = v - iv;
         const float a = (at(iu, iv) * (1 - fu) + at(iu + 1, iv) * fu) * (1 - fv) + (at(iu, iv + 1) * (1 - fu) + at(iu + 1, iv + 1) * fu) * fv;
         out.a[(size_t)(y - y0) * out.w + (x - x0)] = (uint8_t)std::clamp((int)std::lround(a / alpha), 0, 255);
      }
   return true;
}

bool Scene2DNode::TypeMaskSelect(int textLayer, int op, std::string* err)
{
   SelApron cov;
   if (!TextCoverage(textLayer, cov))
   {
      if (err != nullptr)
         *err = "not a text layer";
      return false;
   }
   // The earlier selection with what lies past the canvas, and the glyphs, on one frame that holds the canvas too.
   const SelApron prev = ApronOrCanvas();
   const int W = mDoc.w, H = mDoc.h;
   int x0 = 0, y0 = 0, x1 = W, y1 = H;
   for (const SelApron* p : { &prev, static_cast<const SelApron*>(&cov) })
      if (!p->a.empty())
      {
         x0 = std::min(x0, p->x0);
         y0 = std::min(y0, p->y0);
         x1 = std::max(x1, p->x0 + p->w);
         y1 = std::max(y1, p->y0 + p->h);
      }
   SelApron u;
   u.x0 = x0;
   u.y0 = y0;
   u.w = x1 - x0;
   u.h = y1 - y0;
   u.a.assign((size_t)u.w * u.h, 0);
   auto read = [](const SelApron& p, int x, int y) -> int {
      const int ax = x - p.x0, ay = y - p.y0;
      return p.a.empty() || ax < 0 || ay < 0 || ax >= p.w || ay >= p.h ? 0 : p.a[(size_t)ay * p.w + ax];
   };
   for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++)
      {
         const int p = read(prev, x, y), c = read(cov, x, y);
         const int v = op == kSelAdd ? std::max(p, c) : op == kSelSub ? p * (255 - c) / 255 : op == kSelIntersect ? std::min(p, c) : c;
         u.a[(size_t)(y - y0) * u.w + (x - x0)] = (uint8_t)v;
      }
   RemoveLayer(textLayer);
   SetSelectionFromApron(std::move(u));
   StoreSelection();
   Commit();
   return true;
}

bool Scene2DNode::SelectionExtent(int& x0, int& y0, int& x1, int& y1) const
{
   if (ApronMatches())
   {
      x0 = mSelApron.x0;
      y0 = mSelApron.y0;
      x1 = x0 + mSelApron.w;
      y1 = y0 + mSelApron.h;
      return true;
   }
   float lo[2], hi[2];
   if (!SelectionBounds(lo, hi))
      return false;
   x0 = (int)lo[0];
   y0 = (int)lo[1];
   x1 = (int)hi[0];
   y1 = (int)hi[1];
   return true;
}

void Scene2DNode::InvertSelection()
{
   Layers();
   if (mSel.size() != (size_t)mDoc.w * mDoc.h)
      mSel.assign((size_t)mDoc.w * mDoc.h, 0);
   for (uint8_t& v : mSel)
      v = (uint8_t)(255 - v);
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
   StoreSelection();
}

void Scene2DNode::ModifySelection(int op, int px)
{
   const int w = mDoc.w, h = mDoc.h, r = std::clamp(px, 1, 100);
   if (!mSelAny || mSel.size() != (size_t)w * h)
      return;
   // Expand / Contract by true distance (PhotoCraft morphology on an EDT): a square grows round corners and
   // diagonals reach r, not r * sqrt(2). Canvas edges are not seeds.
   if (op == 0 || op == 1)
   {
      std::vector<uint8_t> seed(mSel.size());
      for (size_t i = 0; i < mSel.size(); i++)
         seed[i] = op == 0 ? mSel[i] >= 128 : mSel[i] < 128;
      const std::vector<float> d2 = Edt2(seed, w, h);
      const float rr = (float)r * r;
      for (size_t i = 0; i < mSel.size(); i++)
         mSel[i] = op == 0 ? (d2[i] <= rr ? 255 : 0) : (d2[i] > rr ? 255 : 0);
   }
   else if (op == 5)
      Feather(mSel, w, h, (float)r);   // Select > Modify > Feather: a Gaussian with sigma = radius (Photoshop)
   else if (op == 2 || op == 4)
   {
      // Box blur (running sums, a pass per axis) then a hard cut at half: corners round, specks under r go.
      // Op 4 (soft edges) keeps the blur: a feathered edge.
      std::vector<int> tmp((size_t)w * h);
      for (int y = 0; y < h; y++)
      {
         int sum = 0;
         for (int x = -r; x <= r; x++)
            sum += mSel[(size_t)y * w + std::clamp(x, 0, w - 1)];
         for (int x = 0; x < w; x++)
         {
            tmp[(size_t)y * w + x] = sum;
            sum += mSel[(size_t)y * w + std::min(w - 1, x + r + 1)] - mSel[(size_t)y * w + std::max(0, x - r)];
         }
      }
      const long long n = (long long)(2 * r + 1) * (2 * r + 1);
      for (int x = 0; x < w; x++)
      {
         long long sum = 0;
         for (int y = -r; y <= r; y++)
            sum += tmp[(size_t)std::clamp(y, 0, h - 1) * w + x];
         for (int y = 0; y < h; y++)
         {
            mSel[(size_t)y * w + x] = op == 4 ? (uint8_t)std::min<long long>(255, (sum + n / 2) / n) : sum * 2 >= n * 255 ? 255 : 0;
            sum += tmp[(size_t)std::min(h - 1, y + r + 1) * w + x] - tmp[(size_t)std::max(0, y - r) * w + x];
         }
      }
   }
   else
   {
      // A ring r wide straddling the edge, by true distance (round, as Expand / Contract): pixels within half
      // of the edge on either side.
      const float half = std::max(1.0f, (float)r * 0.5f), hh = half * half;
      std::vector<uint8_t> inSeed(mSel.size()), outSeed(mSel.size());
      for (size_t i = 0; i < mSel.size(); i++)
      {
         inSeed[i] = mSel[i] >= 128;
         outSeed[i] = mSel[i] < 128;
      }
      const std::vector<float> dIn = Edt2(inSeed, w, h), dOut = Edt2(outSeed, w, h);
      for (size_t i = 0; i < mSel.size(); i++)
         mSel[i] = (inSeed[i] ? dOut[i] <= hh : dIn[i] <= hh) ? 255 : 0;
   }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
   StoreSelection();
}

float Scene2DNode::SelectionAt(int x, int y) const
{
   if (!mSelAny || x < 0 || y < 0 || x >= mDoc.w || y >= mDoc.h || mSel.size() != (size_t)mDoc.w * mDoc.h)
      return 0.0f;
   return mSel[(size_t)(mDoc.h - 1 - y) * mDoc.w + x] / 255.0f;
}

bool Scene2DNode::SelectionBounds(float lo[2], float hi[2]) const
{
   if (!mSelAny || mSel.size() != (size_t)mDoc.w * mDoc.h)
      return false;
   int x0 = mDoc.w, y0 = mDoc.h, x1 = -1, y1 = -1;
   for (int r = 0; r < mDoc.h; r++)
   {
      const uint8_t* row = &mSel[(size_t)r * mDoc.w];
      for (int x = 0; x < mDoc.w; x++)
         if (row[x] >= 128)
         {
            const int y = mDoc.h - 1 - r;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
         }
   }
   if (x1 < 0)
      return false;
   lo[0] = (float)x0;
   lo[1] = (float)y0;
   hi[0] = (float)x1 + 1;
   hi[1] = (float)y1 + 1;
   return true;
}

void Scene2DNode::UploadSelection()
{
   const int W = mDoc.w, H = mDoc.h;
   if (mSelTex.w != W || mSelTex.h != H)
      mSelDirty = true;
   if (!GLUtil::EnsureFbo(mSelTex, W, H, GL_RGBA8, "Scene2DSelection"))
      return;
   if (!mSelDirty)
      return;
   mSelDirty = false;
   std::vector<uint8_t> rgba((size_t)W * H * 4, 0);
   if (mSel.size() == (size_t)W * H)
      for (size_t i = 0; i < mSel.size(); i++)
      {
         rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = mSel[i];
         rgba[i * 4 + 3] = 255;
      }
   WriteRect(mSelTex, 0, 0, W, H, rgba.data());
   mSelTexGen++;
}

unsigned Scene2DNode::SelectionTexture()
{
   if (!mSelAny && !quickMask)
      return 0;
   UploadSelection();
   return mSelTex.tex;
}

unsigned Scene2DNode::SelectionOverlay(float time, bool quick)
{
   if ((!mSelAny && !quick) || !EnsurePaintShaders())
      return 0;
   if (mSel.size() != (size_t)mDoc.w * mDoc.h)
   {
      mSel.assign((size_t)mDoc.w * mDoc.h, 0);
      mSelDirty = true;
   }
   UploadSelection();
   if (!GLUtil::EnsureFbo(mSelOverlay, mDoc.w, mDoc.h, GL_RGBA8, "Scene2DAnts"))
      return 0;
   const bool live = mStroke.Active() && mPaintTarget == 2;
   const unsigned sel = live ? mStroke.DisplayTexture(mSelTex) : mSelTex.tex;
   if (quick && !live && mSelOverlayQuick && mSelOverlaySrc == sel && mSelOverlayGen == mSelTexGen && mSelOverlay.tex != 0)
      return mSelOverlay.tex;
   const unsigned prog = mAntsProgram;
   GLUtil::RunShaderPass(mSelOverlay, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, sel);
      glUniform1i(GLUtil::UniformLoc(prog, "uSel"), 0);
      glUniform2f(GLUtil::UniformLoc(prog, "uPx"), 1.0f / (float)mDoc.w, 1.0f / (float)mDoc.h);
      glUniform1f(GLUtil::UniformLoc(prog, "uTime"), time);
      glUniform1i(GLUtil::UniformLoc(prog, "uQuick"), quick ? 1 : 0);
   });
   mSelOverlayQuick = quick && !live;
   mSelOverlaySrc = sel;
   mSelOverlayGen = mSelTexGen;
   return mSelOverlay.tex;
}

unsigned Scene2DNode::PaintTexture(int id)
{
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   return it == mSurfaces.end() ? 0u : it->second.fbo.tex;
}

unsigned Scene2DNode::PixelMaskTexture(int id)
{
   SyncSurfaces();
   auto it = mMasks.find(id);
   return it == mMasks.end() ? 0u : it->second.fbo.tex;
}

// ---- retouch tools ---------------------------------------------------------

namespace
{
   const char* kMeshFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform vec2 uD[144];\n"   // each grid point's move, in uv, row by row from the top (12 x 12 at most)
      "uniform float uUs[12];\n"  // the grid's column lines, 0 .. 1 across its box
      "uniform float uVs[12];\n"  // its row lines, 0 .. 1 down
      "uniform int uNx;\n"
      "uniform int uNy;\n"
      "uniform vec4 uR;\n"       // the grid's box in uv, y from the top (x0, y0, x1, y1)
      "uniform sampler2D uSel;\n"  // the selection on this surface (red); outside it the pixels stay as they began
      "uniform int uUseSel;\n"
      "uniform int uIsMask;\n"   // the surface is a mask (grey, white shows)
      // A shrinking warp is prefiltered: an area average over the pixel's footprint in the source (PhotoCraft
      // a_shrinking_mesh_warp_is_prefiltered), as the compositor does for a layer shrunk by its transform.
      "vec4 areaTex(sampler2D t, vec2 uv, vec2 du, vec2 dv) {\n"
      "   vec2 ts = vec2(textureSize(t, 0));\n"
      "   float nx = clamp(ceil(length(du * ts) - 0.25), 1.0, 8.0), ny = clamp(ceil(length(dv * ts) - 0.25), 1.0, 8.0);\n"
      "   if (nx * ny < 2.0) return texture(t, uv);\n"
      "   vec4 s = vec4(0.0);\n"
      "   for (int j = 0; j < 8; j++) { if (float(j) >= ny) break;\n"
      "      for (int i = 0; i < 8; i++) { if (float(i) >= nx) break;\n"
      "         vec2 o = (vec2(float(i), float(j)) + 0.5) / vec2(nx, ny) - 0.5;\n"
      "         s += texture(t, uv + du * o.x + dv * o.y); } }\n"
      "   return s / (nx * ny);\n"
      "}\n"
      "vec2 disp(vec2 p) {\n"
      "   vec2 g = clamp((vec2(p.x, 1.0 - p.y) - uR.xy) / max(uR.zw - uR.xy, vec2(1e-6)), 0.0, 1.0);\n"
      "   int i = 0, j = 0;\n"
      "   for (int k = 1; k < 11; k++) { if (k < uNx - 1 && g.x >= uUs[k]) i = k; if (k < uNy - 1 && g.y >= uVs[k]) j = k; }\n"
      "   vec2 f = vec2((g.x - uUs[i]) / max(uUs[i + 1] - uUs[i], 1e-6), (g.y - uVs[j]) / max(uVs[j + 1] - uVs[j], 1e-6));\n"
      "   int k = j * uNx + i;\n"
      "   return mix(mix(uD[k], uD[k + 1], f.x), mix(uD[k + uNx], uD[k + uNx + 1], f.x), f.y);\n"
      "}\n"
      "void main() {\n"
      "   vec2 p = vUv;\n"
      "   for (int it = 0; it < 12; it++) p = vUv - disp(p);\n"   // where this pixel came from
      "   vec2 du = dFdx(p), dv = dFdy(p);\n"
      "   bool off = p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0;\n"
      "   vec4 w = off ? vec4(0.0) : areaTex(uSrc, p, du, dv);\n"
      "   if (uUseSel != 1) { fragColor = w; return; }\n"
      // A selection floats what it holds: the pulled part lands over the rest, and where it left reads empty
      // (pixels clear, a mask white: revealed), as Photoshop and PhotoCraft #4175 leave it.
      "   float here = texture(uSel, vUv).r, moved = off ? 0.0 : texture(uSel, p).r;\n"
      "   vec4 b = texture(uSrc, vUv);\n"
      "   if (uIsMask == 1) { fragColor = mix(mix(b, vec4(1.0), here), w, moved); return; }\n"
      "   b.a *= 1.0 - here;\n"
      "   float a = w.a * moved, oa = a + b.a * (1.0 - a);\n"
      "   fragColor = vec4(oa > 0.0 ? (w.rgb * a + b.rgb * b.a * (1.0 - a)) / oa : vec3(0.0), oa);\n"
      "}\n";
}

// The selection (document px) laid onto a layer's surface, rows as the surface reads back (bottom first).
static void SelOnSurface(const std::vector<uint8_t>& sel, int W, int H, int sw, int sh, const float inv[6], std::vector<uint8_t>& out)
{
   // inv: surface px -> document px (x = a*sx + b*sy + c, y = d*sx + e*sy + f), sy counted from the bottom.
   out.assign((size_t)sw * sh, 0);
   for (int y = 0; y < sh; y++)
      for (int x = 0; x < sw; x++)
      {
         const float sx = x + 0.5f, sy = y + 0.5f;
         const int dx = (int)std::floor(inv[0] * sx + inv[1] * sy + inv[2]), dy = (int)std::floor(inv[3] * sx + inv[4] * sy + inv[5]);
         if (dx >= 0 && dy >= 0 && dx < W && dy < H)
            out[(size_t)y * sw + x] = sel[(size_t)(H - 1 - dy) * W + dx];
      }
}

void Scene2DNode::SetUndoSource(int id)
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind != Kind::Paint)
      return;
   mUndoTiles[id] = l->tiles;
}

// ---- Art History brush ---------------------------------------------------
// The History Brush with an Art style: dabs posed along the stroke (seeded scatter, curl) each paint a rotated copy
// of the History source under them (ArtHistory.cpp, ported from PhotoCraft). Runs on the CPU over the rectangle the
// new dabs touch; EndPaint stores the tiles and commits.
bool Scene2DNode::BeginArt(int id, const BrushEngine::Brush& b, float sx, float sy, const GLUtil::Fbo& src)
{
   const Layer* l = FindConst(id);
   auto it = mSurfaces.find(id);
   if (l == nullptr || it == mSurfaces.end() || src.w != it->second.fbo.w || src.h != it->second.fbo.h)
      return false;
   mArt = ArtStroke();
   mArt.id = id;
   mArt.src = src;
   mArt.w = it->second.fbo.w;
   mArt.h = it->second.fbo.h;
   mArt.style = std::clamp(mArtOpt.style, 0, ArtHistory::kStyleCount - 1);
   mArt.area = std::clamp(mArtOpt.area, 0.0f, kArtAreaMax);
   mArt.tolerance = std::clamp(mArtOpt.tolerance, 0.0f, 1.0f);
   mArt.size = b.size;
   mArt.hardness = b.hardness;
   mArt.opacity = b.opacity;
   mArt.flow = b.flow;
   mArt.lockTransparent = b.lockAlpha;
   const long long state = mHistorySrc == kSourceState ? mHistorySrcDepth : mHistorySrc;
   mArt.seed = ArtHistory::kSeed ^ (uint32_t)state;
   if (mSelAny)
   {
      float o[2], ux[2], uy[2], s = 1;
      if (DocToSurface(*l, 0, 0, 0, o[0], o[1], s) && DocToSurface(*l, 0, 1, 0, ux[0], ux[1], s) && DocToSurface(*l, 0, 0, 1, uy[0], uy[1], s))
      {
         const float a = ux[0] - o[0], c = ux[1] - o[1], bb = uy[0] - o[0], d = uy[1] - o[1], det = a * d - bb * c;
         if (std::fabs(det) >= 1e-9f)
         {
            const float inv[6] = { d / det, -bb / det, -(d * o[0] - bb * o[1]) / det, -c / det, a / det, -(-c * o[0] + a * o[1]) / det };
            SelOnSurface(mSel, mDoc.w, mDoc.h, mArt.w, mArt.h, inv, mArt.sel);
         }
      }
   }
   mArt.pts = { sx, sy };
   mArt.dirty[0] = mArt.w;
   mArt.dirty[1] = mArt.h;
   mArt.on = true;
   mPaintId = id;
   mPaintTarget = 0;
   return true;
}

void Scene2DNode::ArtStamp(bool final)
{
   // The first dab faces the stroke's first move, so nothing is stamped until there is one (or the stroke ends).
   if (!mArt.on || (!final && mArt.pts.size() < 4))
      return;
   std::vector<ArtHistory::Dab> dabs;
   ArtHistory::PlaceDabs(mArt.pts, mArt.size, mArt.area, (ArtHistory::Style)mArt.style, mArt.seed, dabs);
   const size_t end = dabs.size();
   auto st = mSurfaces.find(mArt.id);
   if (mArt.stamped >= end || st == mSurfaces.end())
      return;
   int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
   for (size_t i = mArt.stamped; i < end; i++)
   {
      const ArtHistory::Dab& d = dabs[i];
      const float r = d.radius + 2.0f;
      x0 = std::min(x0, (int)std::floor(d.x - r));
      y0 = std::min(y0, (int)std::floor(d.y - r));
      x1 = std::max(x1, (int)std::ceil(d.x + r));
      y1 = std::max(y1, (int)std::ceil(d.y + r));
   }
   x0 = std::max(x0, 0);
   y0 = std::max(y0, 0);
   x1 = std::min(x1, mArt.w);
   y1 = std::min(y1, mArt.h);
   const int rw = x1 - x0, rh = y1 - y0;
   if (rw <= 0 || rh <= 0 || (uint64_t)rw * (uint64_t)rh > ArtHistory::kMaxCoverage)
   {
      mArt.stamped = end;
      return;
   }
   std::vector<uint8_t> dst8, src8;
   ReadRect(st->second.fbo, x0, y0, rw, rh, dst8);
   ReadRect(mArt.src, x0, y0, rw, rh, src8);
   std::vector<float> dst(dst8.size()), src(src8.size()), sel;
   for (size_t i = 0; i < dst8.size(); i++)
   {
      dst[i] = dst8[i] / 255.0f;
      src[i] = src8[i] / 255.0f;
   }
   if (!mArt.sel.empty())
   {
      sel.resize((size_t)rw * rh);
      for (int y = 0; y < rh; y++)
         for (int x = 0; x < rw; x++)
            sel[(size_t)y * rw + x] = mArt.sel[(size_t)(y0 + y) * mArt.w + x0 + x] / 255.0f;
   }
   ArtHistory::StampParams p;
   p.dest = dst.data();
   p.destW = rw;
   p.destH = rh;
   p.destX = x0;
   p.destY = y0;
   p.src = src.data();
   p.srcW = rw;
   p.srcH = rh;
   p.srcX = x0;
   p.srcY = y0;
   p.ch = 4;
   p.alpha = 3;
   p.dabs = dabs.data() + mArt.stamped;
   p.dabCount = end - mArt.stamped;
   p.hardness = mArt.hardness;
   p.opacity = mArt.opacity;
   p.flow = mArt.flow;
   p.tolerance = mArt.tolerance;
   p.selection = sel.empty() ? nullptr : sel.data();
   p.lockTransparent = mArt.lockTransparent;
   uint32_t written = 0;
   ArtHistory::Stamp(p, written);
   mArt.stamped = end;
   if (written == 0)
      return;
   for (size_t i = 0; i < dst.size(); i++)
      dst8[i] = (uint8_t)std::lround(std::clamp(dst[i], 0.0f, 1.0f) * 255.0f);
   WriteRect(st->second.fbo, x0, y0, rw, rh, dst8.data());
   mPaintEdits++;   // the picture changed before the text does
   mArt.dirty[0] = std::min(mArt.dirty[0], x0);
   mArt.dirty[1] = std::min(mArt.dirty[1], y0);
   mArt.dirty[2] = std::max(mArt.dirty[2], x1);
   mArt.dirty[3] = std::max(mArt.dirty[3], y1);
}

void Scene2DNode::EndArt()
{
   if (mArt.stamped == 0)
      ArtStamp(true);   // a click: the one dab, facing right
   mArt.on = false;
   mPaintId = 0;
   Layer* l = FindLayer(mArt.id);
   auto st = mSurfaces.find(mArt.id);
   if (l != nullptr && st != mSurfaces.end() && mArt.dirty[2] > mArt.dirty[0] && mArt.dirty[3] > mArt.dirty[1])
   {
      StoreTiles(st->second, l->tiles, mArt.dirty);
      Commit();
   }
   mArt.pts.clear();
   mArt.sel.clear();
   mArtOpt.on = false;   // each stroke asks again (the panel sets it per stroke)
}

// ---- history snapshots ---------------------------------------------------

namespace { constexpr size_t kSnapshotsMax = 64; }

int Scene2DNode::AddSnapshot(const std::string& name, bool replace)
{
   Layers();
   Commit();   // the text must hold the layers as they are now, edits not yet committed included
   if (replace && !name.empty())
      for (int i = (int)mSnapshots.size() - 1; i >= 0; i--)
         if (mSnapshots[(size_t)i].name == name)
            DeleteSnapshot(i);
   if (mSnapshots.size() >= kSnapshotsMax)
   {
      mSnapshots.erase(mSnapshots.begin());   // the oldest goes, as Photoshop drops the oldest state
      mHistorySrc = mHistorySrc > 0 ? mHistorySrc - 1 : mHistorySrc == 0 ? -1 : mHistorySrc;   // a state source stays
   }
   return PushSnapshot(name, mText);
}

int Scene2DNode::PushSnapshot(const std::string& name, const std::string& text)
{
   const int id = mNextSnapId++;
   mSnapshots.push_back({ name.empty() ? "Snapshot " + std::to_string(id) : name, text, id });
   return (int)mSnapshots.size() - 1;
}

int Scene2DNode::AddSnapshotFrom(const std::string& name, SnapFrom from, int layerId, int frameId)
{
   if (from == SnapFrom::Document)
      return AddSnapshot(name);
   Layers();
   Commit();
   if (from == SnapFrom::Layer && FindLayer(layerId) == nullptr)
      return -1;
   // Change the document, take its text, then put the document back from its own text: the same round trip
   // RestoreSnapshot makes. The History source tiles are kept aside, since removing a layer drops its own.
   const std::string now = mText;
   const auto undoTiles = mUndoTiles;
   if (mSnapshots.size() >= kSnapshotsMax)
   {
      mSnapshots.erase(mSnapshots.begin());
      mHistorySrc = mHistorySrc > 0 ? mHistorySrc - 1 : mHistorySrc == 0 ? -1 : mHistorySrc;   // a state source stays
   }
   if (from == SnapFrom::Merged)
   {
      std::vector<int> ids;
      for (int c : ChildrenOf(0))
         if (const Layer* l = FindLayer(c))
            if (Shown(*l))
               ids.push_back(c);
      if (!ids.empty())
         MergeLayers(ids, frameId);
   }
   else
   {
      std::set<int> keep;
      for (int id = layerId; id != 0;)
      {
         const Layer* l = FindLayer(id);
         if (l == nullptr)
            break;
         keep.insert(id);
         id = l->parent;
      }
      for (bool gone = true; gone;)   // the layer, its groups and what the layer holds stay; the rest goes
      {
         gone = false;
         for (const Layer& l : Layers())
         {
            bool inside = false;
            for (int p = l.parent; p != 0 && !inside;)
            {
               inside = p == layerId;
               const Layer* pl = FindLayer(p);
               p = pl != nullptr ? pl->parent : 0;
            }
            if (keep.count(l.id) == 0 && !inside)
            {
               RemoveLayer(l.id);
               gone = true;
               break;
            }
         }
      }
   }
   Layers();
   Commit();
   const std::string text = mText;
   SetDocText(now);
   Layers();
   Commit();
   mUndoTiles = undoTiles;
   return PushSnapshot(name, text);
}

int Scene2DNode::SnapshotOnSave(const std::string& name)
{
   Layers();
   Commit();
   for (const Snapshot& sn : mSnapshots)
      if (sn.id == mSaveSnapId && sn.text == mText)
         return -1;   // nothing changed since the last save's snapshot
   const int i = AddSnapshot(name);
   mSaveSnapId = mSnapshots[(size_t)i].id;
   return i;
}

Scene2DNode::Session Scene2DNode::TakeSession() const
{
   Session s;
   s.snapshots = mSnapshots;
   s.nextSnapId = mNextSnapId;
   s.historySrc = mHistorySrc;
   s.srcDepth = mHistorySrcDepth;
   s.saveSnapId = mSaveSnapId;
   s.firstSnapDone = mFirstSnapDone;
   s.undoTiles = mUndoTiles;
   s.sourceAll = mSourceAll;
   s.sourceIds = mSourceIds;
   s.sourceLack = mSourceLack;
   return s;
}

void Scene2DNode::PutSession(const Session& s)
{
   mSnapshots = s.snapshots;
   mNextSnapId = s.nextSnapId;
   mHistorySrc = s.historySrc;
   mHistorySrcDepth = s.srcDepth;
   mSaveSnapId = s.saveSnapId;
   mFirstSnapDone = s.firstSnapDone;
   mUndoTiles = s.undoTiles;
   mSourceAll = s.sourceAll;
   mSourceIds = s.sourceIds;
   mSourceLack = s.sourceLack;
}

bool Scene2DNode::RenameSnapshot(int i, const std::string& name)
{
   if (i < 0 || i >= (int)mSnapshots.size() || name.empty())
      return false;
   mSnapshots[(size_t)i].name = name;
   return true;
}

bool Scene2DNode::DeleteSnapshot(int i)
{
   if (i < 0 || i >= (int)mSnapshots.size())
      return false;
   mSnapshots.erase(mSnapshots.begin() + i);
   if (mHistorySrc == i)
      SetHistorySource(-1);
   else if (mHistorySrc > i)
      mHistorySrc--;
   return true;
}

bool Scene2DNode::RestoreSnapshot(int i)
{
   if (i < 0 || i >= (int)mSnapshots.size())
      return false;
   Layers();
   Commit();
   if (mSnapshots[(size_t)i].text == mText)
      return true;
   SetDocText(mSnapshots[(size_t)i].text);
   Layers();
   Commit();
   return true;
}

bool Scene2DNode::SetHistorySource(int i)
{
   if (i >= (int)mSnapshots.size())
      return false;
   Layers();
   if (i < 0)
   {
      for (const Layer& l : mLayers)
         if (l.kind == Kind::Paint)
            mUndoTiles[l.id] = l.tiles;
      mSourceAll = true;
      mHistorySrc = -1;
      mHistorySrcDepth = -1;
      return true;
   }
   SourceFromText(mSnapshots[(size_t)i].text);
   mHistorySrc = i;
   mHistorySrcDepth = -1;
   return true;
}

bool Scene2DNode::SetHistorySourceText(const std::string& text, long long depth)
{
   if (text.empty())
      return false;
   Layers();
   SourceFromText(text);
   mHistorySrc = kSourceState;
   mHistorySrcDepth = depth;
   return true;
}

void Scene2DNode::DropHistorySource()
{
   mHistorySrc = kSourceNone;
   mHistorySrcDepth = -1;
}

void Scene2DNode::SourceFromText(const std::string& text)
{
   Commit();   // parsing the source below re-reads the document from its text afterwards
   // Read the source's layers by parsing its text, then parse the document back. A layer the source lacks, or
   // one whose pixel size changed since, keeps its current source (the Undo brush then paints it back to itself).
   std::map<int, std::vector<std::string>> then;
   const std::string now = mText;
   mText = text;
   Layers();
   mSourceAll = false;
   mSourceIds.clear();
   mSourceLack = SourceLack();
   for (const Layer& l : mLayers)
   {
      mSourceIds.insert(l.id);
      if (l.kind == Kind::Paint)
         then[l.id] = l.tiles;
   }
   if (mLayers.size() == 1)
   {
      const Layer& lone = mLayers[0];
      if (lone.kind == Kind::Paint)
      {
         mSourceLack.kind = SourceLack::kTiles;
         mSourceLack.tiles = lone.tiles;
         mSourceLack.pw = lone.pw;
         mSourceLack.ph = lone.ph;
      }
      else
      {
         // A lone shape gives its solid colour, past its path too; any other fill, or none, gives white.
         mSourceLack.kind = SourceLack::kColour;
         if (lone.kind == Kind::Vector && lone.shapes.size() == 1 && lone.shapes[0].fillOn && lone.shapes[0].fillType == 0)
            for (int c = 0; c < 3; c++)
               mSourceLack.rgba[c] = lone.shapes[0].fillC[c];
      }
   }
   mText = now;
   Layers();
   for (const Layer& l : mLayers)
   {
      if (l.kind != Kind::Paint)
         continue;
      auto t = then.find(l.id);
      mUndoTiles[l.id] = t != then.end() && t->second.size() == l.tiles.size() ? t->second : l.tiles;
   }
}

const GLUtil::Fbo* Scene2DNode::HistorySourceSurface(const Layer& l)
{
   mHistoryRefusal.clear();
   if (mHistorySrc == kSourceNone)
   {
      mHistoryRefusal = "The History source state is gone: set one in History";
      return nullptr;
   }
   if (!mSourceAll && mSourceIds.count(l.id) == 0)
   {
      if (mSourceLack.kind == SourceLack::kTiles && mSourceLack.pw == l.pw && mSourceLack.ph == l.ph)
      {
         Surface* us = SyncSurface(mUndoSurf, l.id, l.pw, l.ph, mSourceLack.tiles);
         return us != nullptr ? &us->fbo : nullptr;
      }
      if (mSourceLack.kind == SourceLack::kRefuse)
      {
         mHistoryRefusal = (l.name.empty() ? std::string("This layer") : "\"" + l.name + "\"") + " did not exist in the History source state";
         return nullptr;
      }
      if (!GLUtil::EnsureFbo(mLackSurf, l.pw, l.ph, GL_RGBA8, "Scene2DHistoryLack"))
         return nullptr;
      const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
      const float* c = mSourceLack.kind == SourceLack::kColour ? mSourceLack.rgba : white;
      GLint prev = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
      const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
      glDisable(GL_SCISSOR_TEST);
      glBindFramebuffer(GL_FRAMEBUFFER, mLackSurf.fbo);
      glClearColor(c[0], c[1], c[2], c[3]);
      glClear(GL_COLOR_BUFFER_BIT);
      if (scissor)
         glEnable(GL_SCISSOR_TEST);
      glBindFramebuffer(GL_FRAMEBUFFER, prev);
      return &mLackSurf;
   }
   auto u = mUndoTiles.find(l.id);
   if (u == mUndoTiles.end() || u->second.size() != l.tiles.size())
      mUndoTiles[l.id] = l.tiles;
   Surface* us = SyncSurface(mUndoSurf, l.id, l.pw, l.ph, mUndoTiles[l.id]);
   return us != nullptr ? &us->fbo : nullptr;
}

bool Scene2DNode::SetPattern(const std::vector<uint8_t>& rgba, int w, int h, uint64_t key)
{
   if (w <= 0 || h <= 0 || rgba.size() != (size_t)w * h * 4)
      return false;
   if (key != 0 && key == mPatternKey && mPattern.tex != 0 && mPattern.w == w && mPattern.h == h)
      return true;
   if (!GLUtil::EnsureFbo(mPattern, w, h, GL_RGBA8, "Scene2DPattern"))
      return false;
   // Surfaces are bottom row first: flip so the tile reads upright on the canvas.
   std::vector<uint8_t> up(rgba.size());
   const size_t row = (size_t)w * 4;
   for (int y = 0; y < h; y++)
      std::copy(rgba.begin() + (size_t)(h - 1 - y) * row, rgba.begin() + (size_t)(h - y) * row, up.begin() + (size_t)y * row);
   WriteRect(mPattern, 0, 0, w, h, up.data());
   mPatternCpu = std::move(up);
   mPatternKey = key;
   return true;
}

const std::vector<std::string>& Scene2DNode::PatternNames()
{
   static const std::vector<std::string> names = { "Checker", "Stripes", "Dots", "Grid", "Bricks" };
   return names;
}

bool Scene2DNode::BuiltinPattern(int which, std::vector<uint8_t>& rgba, int& w, int& h)
{
   if (which < 0 || which >= (int)PatternNames().size())
      return false;
   constexpr int kSide = 16;
   w = h = kSide;
   rgba.assign((size_t)w * h * 4, 255);
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
         bool ink = false;
         switch (which)
         {
         case 0: ink = (x < kSide / 2) != (y < kSide / 2); break;
         case 1: ink = x < kSide / 4 || (x >= kSide / 2 && x < 3 * kSide / 4); break;
         case 2: { const int dx = x - kSide / 2, dy = y - kSide / 2; ink = dx * dx + dy * dy <= (kSide / 4) * (kSide / 4); break; }
         case 3: ink = x == 0 || y == 0; break;
         default: ink = y == 0 || y == kSide / 2 || (y < kSide / 2 ? x == 0 : x == kSide / 2); break;
         }
         uint8_t* px = &rgba[((size_t)y * w + x) * 4];
         px[0] = px[1] = px[2] = ink ? 0 : 255;
      }
   return true;
}

bool Scene2DNode::FillFromHistory(int id, float opacity)
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind != Kind::Paint || mStroke.Active())
      return false;
   // One hard Undo-brush dab over the whole layer: the stroke already keeps to the selection and the locks.
   BrushEngine::Brush b;
   b.mode = BrushEngine::kUndo;
   b.hardness = 1.0f;
   b.opacity = b.flow = std::clamp(opacity, 0.0f, 1.0f);
   const float w = (float)mDoc.w, h = (float)mDoc.h;
   b.size = 4.0f * std::sqrt(w * w + h * h) + 4.0f * std::sqrt((float)l->pw * l->pw + (float)l->ph * l->ph);
   if (!BeginPaint(id, 0, b, w * 0.5f, h * 0.5f, 1.0f))
      return false;
   EndPaint();
   return true;
}

bool Scene2DNode::PatchSelection(int id, float dx, float dy, bool selIsSource, bool transparent)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0 || !mSelAny)
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   const int sw = it->second.fbo.w, sh = it->second.fbo.h;
   // The surface's inverse place, from three points (a flat layer is affine on its surface).
   float o[2], ux[2], uy[2], s = 1;
   if (!DocToSurface(*l, 0, 0, 0, o[0], o[1], s) || !DocToSurface(*l, 0, 1, 0, ux[0], ux[1], s) || !DocToSurface(*l, 0, 0, 1, uy[0], uy[1], s))
      return false;
   const float a = ux[0] - o[0], c = ux[1] - o[1], b = uy[0] - o[0], d = uy[1] - o[1], det = a * d - b * c;
   if (std::fabs(det) < 1e-9f)
      return false;
   const float inv[6] = { d / det, -b / det, -(d * o[0] - b * o[1]) / det, -c / det, a / det, -(-c * o[0] + a * o[1]) / det };
   std::vector<uint8_t> mask, px;
   SelOnSurface(mSel, mDoc.w, mDoc.h, sw, sh, inv, mask);
   ReadRect(it->second.fbo, 0, 0, sw, sh, px);
   int ox = (int)std::lround(a * dx + b * dy), oy = (int)std::lround(c * dx + d * dy);
   if (selIsSource)
   {
      // The selection's pixels go to where it was dragged: the hole moves there, its source is the selection.
      std::vector<uint8_t> moved((size_t)sw * sh, 0);
      for (int y = 0; y < sh; y++)
         for (int x = 0; x < sw; x++)
         {
            const int sx = x - ox, sy = y - oy;
            if (sx >= 0 && sy >= 0 && sx < sw && sy < sh)
               moved[(size_t)y * sw + x] = mask[(size_t)sy * sw + sx];
         }
      mask.swap(moved);
      ox = -ox;
      oy = -oy;
   }
   const std::vector<uint8_t> before = transparent ? px : std::vector<uint8_t>();
   if (!HealRegion(px, sw, sh, mask, ox, oy, false))
      return false;
   if (transparent)
      for (size_t i = 0; i < px.size(); i++)
         px[i] = (uint8_t)((px[i] + before[i] + 1) / 2);
   WriteRect(it->second.fbo, 0, 0, sw, sh, px.data());
   const int all[4] = { 0, 0, sw, sh };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

// ---- content-aware move --------------------------------------------------------
// Follows PhotoCraft's content_aware_move rules (read, not copied): the selection's content is placed (scaled and
// turned about its centre, then offset), a band at its edge is rebuilt as a membrane between the content and what
// is there, and a move fills the old place with the Remove fill first so nothing ghosts.

namespace
{
   // Chamfer (3-4) distance, in px, from every pixel to the nearest seed (seed[i] != 0).
   void ChamferDist(const std::vector<uint8_t>& seed, int w, int h, std::vector<float>& d)
   {
      const float inf = 1e9f;
      d.assign((size_t)w * h, inf);
      for (size_t i = 0; i < d.size(); i++)
         if (seed[i] != 0)
            d[i] = 0;
      auto at = [&](int x, int y) -> float& { return d[(size_t)y * w + x]; };
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            float v = at(x, y);
            if (x > 0) v = std::min(v, at(x - 1, y) + 3);
            if (y > 0)
            {
               v = std::min(v, at(x, y - 1) + 3);
               if (x > 0) v = std::min(v, at(x - 1, y - 1) + 4);
               if (x + 1 < w) v = std::min(v, at(x + 1, y - 1) + 4);
            }
            at(x, y) = v;
         }
      for (int y = h - 1; y >= 0; y--)
         for (int x = w - 1; x >= 0; x--)
         {
            float v = at(x, y);
            if (x + 1 < w) v = std::min(v, at(x + 1, y) + 3);
            if (y + 1 < h)
            {
               v = std::min(v, at(x, y + 1) + 3);
               if (x + 1 < w) v = std::min(v, at(x + 1, y + 1) + 4);
               if (x > 0) v = std::min(v, at(x - 1, y + 1) + 4);
            }
            at(x, y) = v;
         }
      for (float& v : d)
         v = v >= inf ? inf : v / 3.0f;
   }

   // Bilinear RGBA (0..1) at a continuous point (pixel centres at +0.5), clamped to the picture.
   void Bilerp(const std::vector<uint8_t>& px, int W, int H, float x, float y, float o[4])
   {
      x = std::clamp(x - 0.5f, 0.0f, (float)(W - 1));
      y = std::clamp(y - 0.5f, 0.0f, (float)(H - 1));
      const int x0 = (int)x, y0 = (int)y, x1 = std::min(W - 1, x0 + 1), y1 = std::min(H - 1, y0 + 1);
      const float fx = x - x0, fy = y - y0;
      for (int c = 0; c < 4; c++)
      {
         const float a = px[((size_t)y0 * W + x0) * 4 + c], b = px[((size_t)y0 * W + x1) * 4 + c];
         const float e = px[((size_t)y1 * W + x0) * 4 + c], f = px[((size_t)y1 * W + x1) * 4 + c];
         o[c] = ((a + (b - a) * fx) * (1 - fy) + (e + (f - e) * fx) * fy) / 255.0f;
      }
   }

   std::vector<uint8_t> FlipRows(const std::vector<uint8_t>& in, int w, int h, int ch)
   {
      std::vector<uint8_t> out(in.size());
      const size_t row = (size_t)w * ch;
      for (int y = 0; y < h; y++)
         std::memcpy(&out[(size_t)y * row], &in[(size_t)(h - 1 - y) * row], row);
      return out;
   }
}

std::string Scene2DNode::ContentAwareSolve(const std::vector<uint8_t>& src, int W, int H, const std::vector<uint8_t>& sel,
                                           const CamParams& p, std::vector<uint8_t>& out, std::vector<uint8_t>& write,
                                           std::vector<uint8_t>& selOut)
{
   if (W <= 0 || H <= 0 || src.size() != (size_t)W * H * 4 || sel.size() != (size_t)W * H)
      return "No picture to move";
   if (p.structure < 1 || p.structure > 7 || p.color < 0 || p.color > 10 || !std::isfinite(p.dx) || !std::isfinite(p.dy) ||
       !std::isfinite(p.scaleX) || !std::isfinite(p.scaleY) || !std::isfinite(p.rotate) || std::fabs(p.scaleX) < 1 ||
       std::fabs(p.scaleY) < 1 || std::fabs(p.scaleX) > 10000 || std::fabs(p.scaleY) > 10000 || std::fabs(p.rotate) > 36000)
      return "Content-aware move settings out of range";
   int bx0 = W, by0 = H, bx1 = -1, by1 = -1;
   for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
         if (sel[(size_t)y * W + x] != 0)
         {
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x);
            by1 = std::max(by1, y);
         }
   if (bx1 < 0)
      return "Make a selection first";
   if ((double)(bx1 - bx0 + 1) * (by1 - by0 + 1) > 16.0 * 1024 * 1024)
      return "The selection is too large to move";
   const int dx = (int)std::lround(p.dx), dy = (int)std::lround(p.dy);
   const bool transform = std::fabs(p.scaleX - 100) > 1e-3f || std::fabs(p.scaleY - 100) > 1e-3f || std::fmod(std::fabs(p.rotate), 360.0f) > 1e-3f;
   if (!transform && dx == 0 && dy == 0)
      return "Drag the selection to where the content should go";
   if (p.patch)
   {
      // Patch: the content (dx, dy) away is placed into the selection, which stays where it is.
      if (transform)
         return "Patch takes no scale or turn";
      if (bx0 + dx < 0 || by0 + dy < 0 || bx1 + dx >= W || by1 + dy >= H)
         return "The patch source must stay inside the canvas";
      std::vector<uint8_t> moved((size_t)W * H, 0);
      for (int y = by0; y <= by1; y++)
         for (int x = bx0; x <= bx1; x++)
            moved[(size_t)(y + dy) * W + x + dx] = sel[(size_t)y * W + x];
      CamParams q = p;
      q.patch = false;
      q.extend = true;
      q.dx = (float)-dx;
      q.dy = (float)-dy;
      std::string e = ContentAwareSolve(src, W, H, moved, q, out, write, selOut);
      if (e.empty())
         selOut = sel;
      return e;
   }
   // Placement: about the selection's centre, scale, then turn (clockwise on screen), then offset.
   const float cx = (bx0 + bx1 + 1) * 0.5f, cy = (by0 + by1 + 1) * 0.5f;
   const float th = p.rotate * 3.14159265358979f / 180.0f, cs = std::cos(th), sn = std::sin(th);
   const float sx = p.scaleX / 100.0f, sy = p.scaleY / 100.0f;
   auto fwd = [&](float x, float y, float& ox, float& oy) {
      const float u = (x - cx) * sx, v = (y - cy) * sy;
      ox = cx + dx + u * cs - v * sn;
      oy = cy + dy + u * sn + v * cs;
   };
   auto inv = [&](float x, float y, float& ox, float& oy) {
      const float u = x - cx - dx, v = y - cy - dy;
      ox = cx + (u * cs + v * sn) / sx;
      oy = cy + (-u * sn + v * cs) / sy;
   };
   int lx0, ly0, lx1, ly1;
   if (!transform)
   {
      lx0 = bx0 + dx;
      ly0 = by0 + dy;
      lx1 = bx1 + dx;
      ly1 = by1 + dy;
      if (lx0 < 0 || ly0 < 0 || lx1 >= W || ly1 >= H)
         return "The moved selection must stay inside the canvas";
   }
   else
   {
      float fx0 = 1e30f, fy0 = 1e30f, fx1 = -1e30f, fy1 = -1e30f;
      const float cxs[4] = { (float)bx0, (float)bx1 + 1, (float)bx0, (float)bx1 + 1 }, cys[4] = { (float)by0, (float)by0, (float)by1 + 1, (float)by1 + 1 };
      for (int k = 0; k < 4; k++)
      {
         float ox, oy;
         fwd(cxs[k], cys[k], ox, oy);
         fx0 = std::min(fx0, ox);
         fy0 = std::min(fy0, oy);
         fx1 = std::max(fx1, ox);
         fy1 = std::max(fy1, oy);
      }
      if (fx1 <= 0 || fy1 <= 0 || fx0 >= W || fy0 >= H)
         return "The transformed selection must land on the canvas";
      if ((double)(fx1 - fx0) * (fy1 - fy0) > 16.0 * 1024 * 1024)
         return "The transformed selection is too large";
      lx0 = std::max(0, (int)std::floor(fx0));
      ly0 = std::max(0, (int)std::floor(fy0));
      lx1 = std::min(W - 1, (int)std::ceil(fx1));
      ly1 = std::min(H - 1, (int)std::ceil(fy1));
   }
   // The base: the picture, with the old place filled first when moving (the paste overwrites what lands on it).
   std::vector<uint8_t> base = src;
   if (!p.extend)
      RemoveRegion(base, W, H, sel);
   // The landed shape and the selection that follows it.
   const int side = std::max(1, std::min(lx1 - lx0 + 1, ly1 - ly0 + 1));
   int bw = p.structure <= 1 ? std::max((int)std::lround(0.3f * side), 12) : p.structure == 2 ? std::max((int)std::lround(0.18f * side), 9) : 6;
   bw = std::min(bw, 128);
   const int ring = 6, mg = std::max(bw, ring) + 2;
   const int X0 = std::max(0, lx0 - mg), Y0 = std::max(0, ly0 - mg), X1 = std::min(W - 1, lx1 + mg), Y1 = std::min(H - 1, ly1 + mg);
   const int cw = X1 - X0 + 1, ch = Y1 - Y0 + 1;
   std::vector<uint8_t> land((size_t)cw * ch, 0);
   selOut.assign((size_t)W * H, 0);
   for (int y = ly0; y <= ly1; y++)
      for (int x = lx0; x <= lx1; x++)
      {
         float qx, qy;
         inv(x + 0.5f, y + 0.5f, qx, qy);
         const int ix = (int)std::floor(qx), iy = (int)std::floor(qy);
         if (ix < 0 || iy < 0 || ix >= W || iy >= H)
            continue;
         const uint8_t v = sel[(size_t)iy * W + ix];
         selOut[(size_t)y * W + x] = v;
         land[(size_t)(y - Y0) * cw + x - X0] = v != 0 ? 255 : 0;
      }
   // Inner distance (to the nearest pixel not landed, on the canvas) and outer distance (to the landed shape).
   std::vector<uint8_t> outside(land.size());
   for (size_t i = 0; i < land.size(); i++)
      outside[i] = land[i] == 0 ? 255 : 0;
   std::vector<float> din, dout;
   ChamferDist(outside, cw, ch, din);
   ChamferDist(land, cw, ch, dout);
   // The content at every pixel of the crop, from the original picture through the inverse placement.
   std::vector<float> cont((size_t)cw * ch * 4);
   for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++)
      {
         float qx, qy;
         inv(X0 + x + 0.5f, Y0 + y + 0.5f, qx, qy);
         Bilerp(src, W, H, qx, qy, &cont[((size_t)y * cw + x) * 4]);
      }
   // Colour: above 5 the content takes part of the shift between its old surround and its new one, when that
   // shift is the same in every channel (a light change, not a different object).
   float bias[3] = { 0, 0, 0 };
   if (p.color > 5)
   {
      double sc[3] = { 0, 0, 0 }, sb[3] = { 0, 0, 0 };
      size_t n = 0;
      for (int y = 0; y < ch; y++)
         for (int x = 0; x < cw; x++)
         {
            const size_t i = (size_t)y * cw + x;
            if (land[i] != 0 || dout[i] > ring)
               continue;
            const size_t gi = ((size_t)(Y0 + y) * W + X0 + x) * 4;
            for (int c = 0; c < 3; c++)
            {
               sc[c] += cont[i * 4 + c];
               sb[c] += base[gi + c] / 255.0;
            }
            n++;
         }
      if (n > 0)
      {
         float d[3];
         for (int c = 0; c < 3; c++)
            d[c] = (float)((sb[c] - sc[c]) / n);
         const float lo = std::min({ d[0], d[1], d[2] }), hi = std::max({ d[0], d[1], d[2] });
         if (hi - lo <= 8.0f / 255.0f)
            for (int c = 0; c < 3; c++)
               bias[c] = (p.color - 5) / 5.0f * d[c];
      }
   }
   // The membrane: the base outside, the fitted content in the core, the band between them rebuilt.
   std::vector<float> img((size_t)cw * ch * 4);
   std::vector<uint8_t> band((size_t)cw * ch, 0);
   for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++)
      {
         const size_t i = (size_t)y * cw + x, gi = ((size_t)(Y0 + y) * W + X0 + x) * 4;
         for (int c = 0; c < 3; c++)
            cont[i * 4 + c] = std::clamp(cont[i * 4 + c] + bias[c], 0.0f, 1.0f);
         if (land[i] == 0)
            for (int c = 0; c < 4; c++)
               img[i * 4 + c] = base[gi + c] / 255.0f;
         else if (din[i] > bw)
            std::copy(&cont[i * 4], &cont[i * 4] + 4, &img[i * 4]);
         else
            band[i] = 255;
      }
   SelectMaths::HarmonicFill(img, 4, band, cw, ch);
   out = base;
   write.assign((size_t)W * H, 0);
   if (!p.extend)
      for (size_t i = 0; i < sel.size(); i++)
         if (sel[i] != 0)
            write[i] = 255;
   for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++)
      {
         const size_t i = (size_t)y * cw + x, g = (size_t)(Y0 + y) * W + X0 + x;
         if (land[i] == 0)
            continue;
         float t = 1;
         if (band[i] != 0)
         {
            const float r = std::clamp(din[i] / bw, 0.0f, 1.0f);
            t = p.structure >= 3 ? r : p.structure == 2 ? r * r : 0.0f;
         }
         for (int c = 0; c < 4; c++)
         {
            const float v = img[i * 4 + c] + (cont[i * 4 + c] - img[i * 4 + c]) * t;
            out[g * 4 + c] = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
         }
         write[g] = 255;
      }
   return "";
}

// The picture a content-aware move reads and the target it writes, top-down; false (with why) when it cannot.
bool Scene2DNode::CamSource(int id, const CamParams& p, std::vector<uint8_t>& src, std::vector<uint8_t>& own, std::string* err)
{
   auto fail = [&](const char* why) {
      if (err != nullptr)
         *err = why;
      return false;
   };
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr)
      return fail("No layer to move on");
   if (!mSelAny || mSel.size() != (size_t)mDoc.w * mDoc.h)
      return fail("Make a selection first");
   SyncSurfaces();
   const int W = mDoc.w, H = mDoc.h;
   GLUtil::Fbo* fbo = nullptr;
   if (p.target == 0)
   {
      if (l->kind != Kind::Paint)
         return fail("Content-aware move works on a pixel layer");
      if ((EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
         return fail("The layer is locked");
      float m[6];
      LayerMatrix(*l, m);
      const bool aligned = std::fabs(m[0] - 1) < 1e-4f && std::fabs(m[4] - 1) < 1e-4f && std::fabs(m[1]) < 1e-4f &&
                           std::fabs(m[3]) < 1e-4f && std::fabs(m[2]) < 1e-3f && std::fabs(m[5]) < 1e-3f && l->pw == W && l->ph == H;
      if (!aligned)
         return fail("Content-aware move needs a layer the size of the document, not moved or scaled");
      auto it = mSurfaces.find(id);
      if (it == mSurfaces.end())
         return fail("No pixels on this layer");
      fbo = &it->second.fbo;
   }
   else if (p.target == 1 || p.target == 3)
   {
      const bool fx = p.target == 3;
      auto& surfaces = fx ? mFxMasks : mMasks;
      const int mw = fx ? l->fmw : l->mw, mh = fx ? l->fmh : l->mh;
      if ((fx ? !l->HasFilterMask() : !l->HasPixelMask()) || (EffectiveLock(*l) & kLockAll) != 0)
         return fail("No mask to move on");
      if (mw != W || mh != H)
         return fail("Content-aware move needs a mask the size of the document");
      auto it = surfaces.find(id);
      if (it == surfaces.end())
         return fail("No mask to move on");
      fbo = &it->second.fbo;
   }
   else
      return fail("Content-aware move works on pixels or a mask");
   std::vector<uint8_t> px;
   ReadRect(*fbo, 0, 0, W, H, px);
   if (px.size() != (size_t)W * H * 4)
      return fail("No pixels on this layer");
   own = FlipRows(px, W, H, 4);
   if (p.sampleAll && p.target == 0)
   {
      if (mOut.fbo == 0 || mOut.w != W || mOut.h != H)
         return fail("Nothing composed yet to sample");
      ReadRect(mOut, 0, 0, W, H, px);
      src = FlipRows(px, W, H, 4);
   }
   else
      src = own;
   return true;
}

bool Scene2DNode::ContentAwareMove(int id, const CamParams& p, std::string* err)
{
   std::vector<uint8_t> src, own, out, write, selOut;
   if (!CamSource(id, p, src, own, err))
      return false;
   const int W = mDoc.w, H = mDoc.h;
   const std::vector<uint8_t> sel = FlipRows(mSel, W, H, 1);
   const std::string e = ContentAwareSolve(src, W, H, sel, p, out, write, selOut);
   if (!e.empty())
   {
      if (err != nullptr)
         *err = e;
      return false;
   }
   Layer* l = FindLayer(id);
   const bool lockAlpha = p.target == 0 && (EffectiveLock(*l) & kLockAlpha) != 0;
   for (size_t i = 0; i < write.size(); i++)
      if (write[i] != 0)
      {
         if (p.target == 0)
         {
            const uint8_t a = own[i * 4 + 3];
            std::copy(&out[i * 4], &out[i * 4] + 4, &own[i * 4]);
            if (lockAlpha)
               own[i * 4 + 3] = a;
         }
         else
         {
            own[i * 4] = own[i * 4 + 1] = own[i * 4 + 2] = out[i * 4];
            own[i * 4 + 3] = 255;
         }
      }
   const std::vector<uint8_t> px = FlipRows(own, W, H, 4);
   auto& surfaces = p.target == 0 ? mSurfaces : p.target == 1 ? mMasks : mFxMasks;
   auto& tiles = p.target == 0 ? l->tiles : p.target == 1 ? l->mtiles : l->fmtiles;
   Surface& s = surfaces.find(id)->second;
   WriteRect(s.fbo, 0, 0, W, H, px.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(s, tiles, all);
   mSel = FlipRows(selOut, W, H, 1);
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
   StoreSelection();
   return true;
}

bool Scene2DNode::ContentAwarePreview(int id, const CamParams& p, int maxArea, std::vector<uint8_t>& rgba, int& pw, int& ph,
                                      float r[4], std::string* err)
{
   std::vector<uint8_t> src, own, out, write, selOut;
   if (!CamSource(id, p, src, own, err))
      return false;
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> sel = FlipRows(mSel, W, H, 1);
   // Coarse: the picture and the selection box-averaged down (the larger of the selection and its landing at most
   // maxArea px), the offset scaled with them.
   int w = W, h = H;
   float f = 1;
   CamParams q = p;
   int bx0 = W, by0 = H, bx1 = -1, by1 = -1;
   for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
         if (sel[(size_t)y * W + x] != 0)
         {
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x);
            by1 = std::max(by1, y);
         }
   const double area = bx1 < 0 ? 0.0 : (double)(bx1 - bx0 + 1) * (by1 - by0 + 1);
   const double big = std::max(area, area * std::fabs(p.scaleX * p.scaleY) / 1e4);
   if (maxArea > 0 && big > maxArea)
   {
      f = (float)std::sqrt(maxArea / big);
      w = std::max(1, (int)std::lround(W * f));
      h = std::max(1, (int)std::lround(H * f));
      std::vector<uint8_t> s2((size_t)w * h * 4), m2((size_t)w * h), faint((size_t)w * h);
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            const int x0 = x * W / w, x1 = std::max(x0 + 1, (x + 1) * W / w), y0 = y * H / h, y1 = std::max(y0 + 1, (y + 1) * H / h);
            uint32_t acc[5] = { 0, 0, 0, 0, 0 }, n = 0;
            for (int yy = y0; yy < y1; yy++)
               for (int xx = x0; xx < x1; xx++, n++)
               {
                  const size_t i = (size_t)yy * W + xx;
                  for (int c = 0; c < 4; c++)
                     acc[c] += src[i * 4 + c];
                  acc[4] += sel[i];
               }
            for (int c = 0; c < 4; c++)
               s2[((size_t)y * w + x) * 4 + c] = (uint8_t)(acc[c] / n);
            m2[(size_t)y * w + x] = (uint8_t)(acc[4] / n);
            faint[(size_t)y * w + x] = acc[4] != 0;
         }
      // A sparse faint selection that averages away entirely keeps every block it touched.
      if (std::all_of(m2.begin(), m2.end(), [](uint8_t v) { return v == 0; }))
         for (size_t i = 0; i < m2.size(); i++)
            m2[i] = faint[i] ? 1 : 0;
      src.swap(s2);
      sel.swap(m2);
      q.dx = p.dx * f;
      q.dy = p.dy * f;
   }
   const std::string e = ContentAwareSolve(src, w, h, sel, q, out, write, selOut);
   if (!e.empty())
   {
      if (err != nullptr)
         *err = e;
      return false;
   }
   int x0 = w, y0 = h, x1 = -1, y1 = -1;
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
         if (write[(size_t)y * w + x] != 0)
         {
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
         }
   if (x1 < 0)
      return false;
   pw = x1 - x0 + 1;
   ph = y1 - y0 + 1;
   rgba.assign((size_t)pw * ph * 4, 0);
   for (int y = 0; y < ph; y++)
      for (int x = 0; x < pw; x++)
      {
         const size_t g = (size_t)(y0 + y) * w + x0 + x;
         if (write[g] != 0)
            std::copy(&out[g * 4], &out[g * 4] + 4, &rgba[((size_t)y * pw + x) * 4]);
      }
   r[0] = x0 / f;
   r[1] = y0 / f;
   r[2] = (x1 + 1) / f;
   r[3] = (y1 + 1) / f;
   return true;
}

bool Scene2DNode::Blemish(int id, float x, float y, float radius)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   float sx = 0, sy = 0, s = 1;
   if (!DocToSurface(*l, 0, x, y, sx, sy, s))
      return false;
   const int sw = it->second.fbo.w, sh = it->second.fbo.h;
   const float r = std::max(1.0f, radius * s);
   std::vector<uint8_t> mask((size_t)sw * sh, 0), px;
   for (int yy = std::max(0, (int)(sy - r - 1)); yy < std::min(sh, (int)(sy + r + 2)); yy++)
      for (int xx = std::max(0, (int)(sx - r - 1)); xx < std::min(sw, (int)(sx + r + 2)); xx++)
      {
         const float dd = std::hypot(xx + 0.5f - sx, yy + 0.5f - sy);
         mask[(size_t)yy * sw + xx] = (uint8_t)std::lround(std::clamp(r + 0.5f - dd, 0.0f, 1.0f) * 255.0f);
      }
   ReadRect(it->second.fbo, 0, 0, sw, sh, px);
   if (!HealRegion(px, sw, sh, mask, 0, 0, true))
      return false;
   WriteRect(it->second.fbo, 0, 0, sw, sh, px.data());
   const int all[4] = { 0, 0, sw, sh };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

bool Scene2DNode::FloodErase(int id, float x, float y, int tolerance, bool contiguous, bool sampleAll)
{
   Layers();
   const std::vector<uint8_t> keep = mSel;
   const bool keepAny = mSelAny;
   const SelStore keepStore = mSelStore;
   const SelApron keepApron = mSelApron;
   const ApronStore keepApronStore = mSelApronStore;
   SelectWand(x, y, tolerance, contiguous, kSelNew, sampleAll, id);
   const bool ok = mSelAny && ClearPixels(id);
   mSel = keep;
   mSelAny = keepAny;
   mSelStore = keepStore;
   mSelApron = keepApron;
   mSelApronStore = keepApronStore;
   mSelDirty = true;
   Commit();
   return ok;
}

void Scene2DNode::SelectObject(float x0, float y0, float x1, float y1, int op, bool sampleAll, int layerId)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> px;
   const Layer* l = FindConst(layerId);
   auto it = l != nullptr && l->kind == Kind::Paint ? mSurfaces.find(layerId) : mSurfaces.end();
   if (!sampleAll && it != mSurfaces.end() && l->pw == W && l->ph == H)
      ReadRect(it->second.fbo, 0, 0, W, H, px);
   else if (mOut.fbo != 0 && mOut.w == W && mOut.h == H)
      ReadRect(mOut, 0, 0, W, H, px);
   else
      return;
   if (x1 < x0) std::swap(x0, x1);
   if (y1 < y0) std::swap(y0, y1);
   const int bx0 = std::clamp((int)x0, 0, W - 1), bx1 = std::clamp((int)x1, bx0 + 1, W);
   const int by0 = std::clamp((int)y0, 0, H - 1), by1 = std::clamp((int)y1, by0 + 1, H);
   const int bw = bx1 - bx0, bh = by1 - by0;
   if (bw < 4 || bh < 4)
      return;
   auto colAt = [&](int x, int y, float c[3]) {
      const uint8_t* p = &px[((size_t)(H - 1 - y) * W + x) * 4];
      c[0] = p[0] / 255.0f; c[1] = p[1] / 255.0f; c[2] = p[2] / 255.0f;
   };
   // Colour groups for what is round the box's edge (background) and what is in its middle (the object).
   const int K = 6, band = std::max(2, std::min(bw, bh) / 20);
   std::vector<std::array<float, 3>> bgS, fgS;
   const int step = std::max(1, (int)std::sqrt((double)bw * bh / 40000.0));
   for (int y = by0; y < by1; y += step)
      for (int x = bx0; x < bx1; x += step)
      {
         std::array<float, 3> c;
         colAt(x, y, c.data());
         const bool edge = x < bx0 + band || x >= bx1 - band || y < by0 + band || y >= by1 - band;
         const bool mid = std::abs(x - (bx0 + bx1) / 2) < bw / 4 && std::abs(y - (by0 + by1) / 2) < bh / 4;
         if (edge) bgS.push_back(c);
         else if (mid) fgS.push_back(c);
      }
   auto kmeans = [&](const std::vector<std::array<float, 3>>& s, std::vector<std::array<float, 3>>& cent) {
      cent.clear();
      if (s.empty())
         return;
      for (int k = 0; k < K; k++)
         cent.push_back(s[(size_t)k * s.size() / K]);
      std::vector<std::array<float, 4>> acc(K);
      for (int iter = 0; iter < 8; iter++)
      {
         for (auto& a : acc) a = { 0, 0, 0, 0 };
         for (const auto& c : s)
         {
            int best = 0;
            float bd = 1e9f;
            for (int k = 0; k < K; k++)
            {
               const float d = (c[0] - cent[k][0]) * (c[0] - cent[k][0]) + (c[1] - cent[k][1]) * (c[1] - cent[k][1]) + (c[2] - cent[k][2]) * (c[2] - cent[k][2]);
               if (d < bd) { bd = d; best = k; }
            }
            for (int q = 0; q < 3; q++) acc[best][q] += c[q];
            acc[best][3] += 1;
         }
         for (int k = 0; k < K; k++)
            if (acc[k][3] > 0)
               for (int q = 0; q < 3; q++) cent[k][q] = acc[k][q] / acc[k][3];
      }
   };
   auto nearest = [](const std::vector<std::array<float, 3>>& cent, const float c[3]) {
      float bd = 1e9f;
      for (const auto& m : cent)
         bd = std::min(bd, (c[0] - m[0]) * (c[0] - m[0]) + (c[1] - m[1]) * (c[1] - m[1]) + (c[2] - m[2]) * (c[2] - m[2]));
      return bd;
   };
   std::vector<std::array<float, 3>> bgC, fgC;
   std::vector<uint8_t> fg((size_t)bw * bh, 0);
   for (int round = 0; round < 3; round++)
   {
      kmeans(bgS, bgC);
      kmeans(fgS, fgC);
      if (bgC.empty() || fgC.empty())
         return;
      std::vector<std::array<float, 3>> nbg, nfg;
      for (int y = 0; y < bh; y++)
         for (int x = 0; x < bw; x++)
         {
            float c[3];
            colAt(bx0 + x, by0 + y, c);
            const bool edge = x < band || x >= bw - band || y < band || y >= bh - band;
            const bool f = !edge && nearest(fgC, c) < nearest(bgC, c);
            fg[(size_t)y * bw + x] = f ? 1 : 0;
            if (((x | y) % step) == 0)
               (f ? nfg : nbg).push_back({ c[0], c[1], c[2] });
         }
      bgS.swap(nbg);
      fgS.swap(nfg);
   }
   // Majority smoothing, then the largest piece with its holes filled.
   const int rr = std::max(1, std::min(bw, bh) / 100);
   std::vector<int> sat((size_t)(bw + 1) * (bh + 1), 0);
   for (int y = 0; y < bh; y++)
      for (int x = 0; x < bw; x++)
         sat[(size_t)(y + 1) * (bw + 1) + x + 1] = fg[(size_t)y * bw + x] + sat[(size_t)y * (bw + 1) + x + 1] + sat[(size_t)(y + 1) * (bw + 1) + x] - sat[(size_t)y * (bw + 1) + x];
   std::vector<uint8_t> sm(fg.size());
   for (int y = 0; y < bh; y++)
      for (int x = 0; x < bw; x++)
      {
         const int ax = std::max(0, x - rr), ay = std::max(0, y - rr), cx = std::min(bw, x + rr + 1), cy = std::min(bh, y + rr + 1);
         const int sum = sat[(size_t)cy * (bw + 1) + cx] - sat[(size_t)ay * (bw + 1) + cx] - sat[(size_t)cy * (bw + 1) + ax] + sat[(size_t)ay * (bw + 1) + ax];
         sm[(size_t)y * bw + x] = sum * 2 > (cx - ax) * (cy - ay) ? 1 : 0;
      }
   std::vector<int> lab(sm.size(), 0), stack;
   int bestLab = 0, bestN = 0, next = 0;
   for (size_t i = 0; i < sm.size(); i++)
   {
      if (!sm[i] || lab[i])
         continue;
      int n = 0;
      lab[i] = ++next;
      stack.assign(1, (int)i);
      while (!stack.empty())
      {
         const int p = stack.back();
         stack.pop_back();
         n++;
         const int x = p % bw, y = p / bw;
         const int nb[4] = { x > 0 ? p - 1 : -1, x < bw - 1 ? p + 1 : -1, y > 0 ? p - bw : -1, y < bh - 1 ? p + bw : -1 };
         for (int q : nb)
            if (q >= 0 && sm[q] && !lab[q]) { lab[q] = next; stack.push_back(q); }
      }
      if (n > bestN) { bestN = n; bestLab = next; }
   }
   if (bestN == 0)
      return;
   // Outside: reached from the box's edge without crossing the piece; everything else is the object.
   std::vector<uint8_t> outside(sm.size(), 0);
   stack.clear();
   for (int x = 0; x < bw; x++) { stack.push_back(x); stack.push_back((bh - 1) * bw + x); }
   for (int y = 0; y < bh; y++) { stack.push_back(y * bw); stack.push_back(y * bw + bw - 1); }
   while (!stack.empty())
   {
      const int p = stack.back();
      stack.pop_back();
      if (outside[p] || lab[p] == bestLab)
         continue;
      outside[p] = 1;
      const int x = p % bw, y = p / bw;
      if (x > 0) stack.push_back(p - 1);
      if (x < bw - 1) stack.push_back(p + 1);
      if (y > 0) stack.push_back(p - bw);
      if (y < bh - 1) stack.push_back(p + bw);
   }
   std::vector<uint8_t> region((size_t)W * H, 0);
   for (int y = 0; y < bh; y++)
      for (int x = 0; x < bw; x++)
         if (!outside[(size_t)y * bw + x])
            region[(size_t)(H - 1 - (by0 + y)) * W + bx0 + x] = 255;
   CombineSelection(region, op);
}

void Scene2DNode::CombineSelection(const std::vector<uint8_t>& region, int op)
{
   if (mLiveRefineOn && mLiveSel.size() == region.size())
   {
      // Inside Select and Mask: Object Selection and Select Subject edit the selection the dialog refines.
      std::swap(mSel, mLiveSel);
      std::swap(mSelAny, mLiveSelAny);
      mLiveRefineOn = false;
      CombineSelection(region, op == kSelNew ? kSelAdd : op);
      mLiveRefineOn = true;
      std::swap(mSel, mLiveSel);
      std::swap(mSelAny, mLiveSelAny);
      mLiveBaseChanged = true;
      return;
   }
   // Add and Subtract act on the canvas and keep what lies past its edge; New and Intersect drop it.
   SelApron keep;
   if ((op == kSelAdd || op == kSelSub) && ApronMatches())
      keep = mSelApron;
   if (mSel.size() != region.size())
      mSel.assign(region.size(), 0);
   if (op == kSelNew || !mSelAny)
      mSel = op == kSelSub || op == kSelIntersect ? std::vector<uint8_t>(region.size(), 0) : region;
   else
      for (size_t i = 0; i < region.size(); i++)
      {
         if (op == kSelAdd) mSel[i] = std::max(mSel[i], region[i]);
         else if (op == kSelSub) mSel[i] = (uint8_t)(mSel[i] * (255 - region[i]) / 255);
         else mSel[i] = std::min(mSel[i], region[i]);
      }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
   if (!keep.a.empty() && mSel.size() == (size_t)mDoc.w * mDoc.h)
   {
      // The apron grown to hold the canvas, the canvas part replaced by the new selection.
      const int W = mDoc.w, H = mDoc.h;
      SelApron u;
      u.x0 = std::min(keep.x0, 0);
      u.y0 = std::min(keep.y0, 0);
      u.w = std::max(keep.x0 + keep.w, W) - u.x0;
      u.h = std::max(keep.y0 + keep.h, H) - u.y0;
      u.a.assign((size_t)u.w * u.h, 0);
      for (int y = 0; y < keep.h; y++)
         std::copy_n(&keep.a[(size_t)y * keep.w], keep.w, &u.a[(size_t)(keep.y0 - u.y0 + y) * u.w + (keep.x0 - u.x0)]);
      for (int y = 0; y < H; y++)
         std::copy_n(&mSel[(size_t)(H - 1 - y) * W], W, &u.a[(size_t)(y - u.y0) * u.w - u.x0]);
      SetSelectionFromApron(std::move(u));
   }
   StoreSelection();
}

// ---- live selection dialogs (Select and Mask, Color Range) ----------------------------------------------------
bool Scene2DNode::ReadSelectPicture(bool sampleAll, int layerId, std::vector<uint8_t>& px)
{
   Layers();
   SyncSurfaces();   // after an undo the surfaces still hold the undone pixels until synced
   const int W = mDoc.w, H = mDoc.h;
   px.clear();
   const Layer* l = FindConst(layerId);
   auto it = l != nullptr && l->kind == Kind::Paint ? mSurfaces.find(layerId) : mSurfaces.end();
   if (!sampleAll && it != mSurfaces.end() && l->pw == W && l->ph == H)
      ReadRect(it->second.fbo, 0, 0, W, H, px);
   else if (!sampleAll && l != nullptr && l->kind == Kind::Paint && it == mSurfaces.end())
      px.assign((size_t)W * H * 4, 0);   // a pixel layer nothing was painted on yet is clear, not the picture under it
   else if (mOut.fbo != 0 && mOut.w == W && mOut.h == H)
      ReadRect(mOut, 0, 0, W, H, px);
   return px.size() == (size_t)W * H * 4;
}

bool Scene2DNode::GrowSelection(bool similar, int tolerance, bool sampleAll, int layerId)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> px;
   if (!mSelAny || mSel.size() != (size_t)W * H || !ReadSelectPicture(sampleAll, layerId, px))
      return false;
   std::vector<uint8_t> out;
   SelectMaths::GrowMatch(px.data(), W, H, mSel, tolerance, !similar, out);
   mSel.swap(out);
   mSelAny = true;
   mSelDirty = true;
   StoreSelection();
   return true;
}

void Scene2DNode::FocusPreview(float range, float noise, bool invert, int op)
{
   const int W = mDoc.w, H = mDoc.h;
   if (mLivePic.size() != (size_t)W * H * 4)
      return;
   if (mLiveFocus.size() != (size_t)W * H || mLiveFocusNoise != noise)
   {
      SelectMaths::FocusMap(mLivePic.data(), W, H, noise, mLiveFocus);
      mLiveFocusNoise = noise;
   }
   std::vector<uint8_t> region;
   SelectMaths::FocusSelect(mLiveFocus, W, H, range, region);
   LiveCombine(region, invert, op);
}

bool Scene2DNode::LiveSelBegin(bool sampleAll, int layerId)
{
   std::vector<uint8_t> px;
   if (!ReadSelectPicture(sampleAll, layerId, px))
      return false;
   const int W = mDoc.w, H = mDoc.h;
   mLivePic.swap(px);
   mLiveDepth.clear();
   mLiveDepthKey = 0;
   mLiveFocus.clear();
   mLiveFocusNoise = -1.0f;
   mLiveSelAny = mSelAny && mSel.size() == (size_t)W * H;
   mLiveSel = mLiveSelAny ? mSel : std::vector<uint8_t>((size_t)W * H, 0);
   mLiveZone.clear();
   mLiveZoneR = 0.0f;
   mLiveRefineOn = mLiveBaseChanged = false;
   return true;
}

bool Scene2DNode::LiveSelFromMask(int id)
{
   std::vector<uint8_t> m;
   if (mLivePic.empty() || !MaskInDoc(id, m) || m.size() != mLiveSel.size())
      return false;
   mLiveSel.swap(m);
   mLiveSelAny = std::any_of(mLiveSel.begin(), mLiveSel.end(), [](uint8_t v) { return v != 0; });
   return true;
}

void Scene2DNode::LiveRefineDab(float x, float y, float r, bool erase)
{
   const int W = mDoc.w, H = mDoc.h;
   if (mLivePic.empty() || r <= 0.0f)
      return;
   if (mLiveZone.size() != (size_t)W * H)
      mLiveZone.assign((size_t)W * H, 0);
   if (!erase)
      mLiveZoneR = std::max(mLiveZoneR, r);
   for (int yy = std::max(0, (int)(y - r - 1)); yy < std::min(H, (int)(y + r + 2)); yy++)
      for (int xx = std::max(0, (int)(x - r - 1)); xx < std::min(W, (int)(x + r + 2)); xx++)
      {
         const float c = std::clamp(r + 0.5f - std::hypot(xx + 0.5f - x, yy + 0.5f - y), 0.0f, 1.0f);
         uint8_t& z = mLiveZone[(size_t)(H - 1 - yy) * W + xx];
         const uint8_t v = (uint8_t)std::lround(c * 255.0f);
         z = erase ? (uint8_t)(z * (255 - v) / 255) : std::max(z, v);
      }
}

bool Scene2DNode::LiveBaseChanged()
{
   const bool c = mLiveBaseChanged;
   mLiveBaseChanged = false;
   return c;
}

bool Scene2DNode::LivePixel(float x, float y, float rgb[3]) const
{
   const int W = mDoc.w, H = mDoc.h, px = (int)std::floor(x), py = H - 1 - (int)std::floor(y);
   if (mLivePic.size() != (size_t)W * H * 4 || px < 0 || py < 0 || px >= W || py >= H)
      return false;
   const uint8_t* c = &mLivePic[((size_t)py * W + px) * 4];
   for (int k = 0; k < 3; k++)
      rgb[k] = c[k] / 255.0f;
   return true;
}

void Scene2DNode::LiveSelEnd(bool keep)
{
   if (mLivePic.empty())
      return;
   if (!keep)
   {
      mSel = mLiveSel;
      mSelAny = mLiveSelAny;
   }
   mLivePic.clear();
   mLivePic.shrink_to_fit();
   mLiveSel.clear();
   mLiveSel.shrink_to_fit();
   mLiveDepth.clear();
   mLiveDepth.shrink_to_fit();
   mLiveDepthKey = 0;
   mLiveFocus.clear();
   mLiveFocus.shrink_to_fit();
   mLiveFocusNoise = -1.0f;
   mLiveZone.clear();
   mLiveZone.shrink_to_fit();
   mLiveZoneR = 0.0f;
   mLiveRefineOn = mLiveBaseChanged = false;
   mSelDirty = true;
   StoreSelection();
}

void Scene2DNode::RefinePreview(const SelectMaths::RefineParams& p)
{
   const int W = mDoc.w, H = mDoc.h;
   if (mLivePic.size() != (size_t)W * H * 4 || !mLiveSelAny)
      return;
   mLiveRefineOn = true;   // object and subject edits now go to the selection under the dialog
   mSel = mLiveSel;
   SelectMaths::Refine(mLivePic.data(), mSel, W, H, p);
   if (mLiveZone.size() == mSel.size() && mLiveZoneR > 0.0f)
   {
      // Refine Edge Brush: where it painted, the edge is redrawn from the picture over the brush's width.
      std::vector<uint8_t> g = mSel;
      SelectMaths::GuidedEdge(mLivePic.data(), g, W, H, mLiveZoneR, false);
      for (size_t i = 0; i < mSel.size(); i++)
         if (mLiveZone[i] != 0)
            mSel[i] = (uint8_t)((mSel[i] * (255 - mLiveZone[i]) + g[i] * mLiveZone[i] + 127) / 255);
   }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
}

int Scene2DNode::RefineOutput(int output, int layerId, float decon)
{
   if (output == 1)
      return MaskFromSelection(layerId) ? layerId : 0;
   if (output != 2)
      return 0;
   const int id = Duplicate(layerId);
   if (id == 0)
      return 0;
   Layers();
   Layer* l = FindLayer(id);
   if (l != nullptr && l->kind == Kind::Paint && decon > 0.0f && mSelAny)
   {
      SyncSurfaces();
      auto it = mSurfaces.find(id);
      float o[2], ux[2], uy[2], s = 1;
      if (it != mSurfaces.end() && DocToSurface(*l, 0, 0, 0, o[0], o[1], s) && DocToSurface(*l, 0, 1, 0, ux[0], ux[1], s) &&
          DocToSurface(*l, 0, 0, 1, uy[0], uy[1], s))
      {
         const float a = ux[0] - o[0], c = ux[1] - o[1], b = uy[0] - o[0], d = uy[1] - o[1], det = a * d - b * c;
         if (std::fabs(det) >= 1e-9f)
         {
            const float inv[6] = { d / det, -b / det, -(d * o[0] - b * o[1]) / det, -c / det, a / det, -(-c * o[0] + a * o[1]) / det };
            const int sw = it->second.fbo.w, sh = it->second.fbo.h;
            std::vector<uint8_t> mask, px;
            SelOnSurface(mSel, mDoc.w, mDoc.h, sw, sh, inv, mask);
            ReadRect(it->second.fbo, 0, 0, sw, sh, px);
            SelectMaths::Decontaminate(px.data(), mask, sw, sh, std::clamp(decon, 0.0f, 1.0f));
            WriteRect(it->second.fbo, 0, 0, sw, sh, px.data());
            const int all[4] = { 0, 0, sw, sh };
            StoreTiles(it->second, l->tiles, all);
         }
      }
   }
   return MaskFromSelection(id) ? id : 0;
}

void Scene2DNode::ColorRangePreview(const std::vector<SelectMaths::Sample>& s, const SelectMaths::RangeParams& p, bool invert, int op)
{
   const int W = mDoc.w, H = mDoc.h;
   if (mLivePic.size() != (size_t)W * H * 4)
      return;
   std::vector<SelectMaths::Sample> flip = s;   // samples top row first; the picture bottom row first
   for (SelectMaths::Sample& k : flip)
      k.y = (float)H - k.y;
   std::vector<uint8_t> region;
   SelectMaths::ColorRange(mLivePic.data(), W, H, flip, p, region);
   LiveCombine(region, invert, op);
}

void Scene2DNode::LiveCombine(std::vector<uint8_t>& region, bool invert, int op)
{
   if (invert)
      for (uint8_t& v : region)
         v = (uint8_t)(255 - v);
   if (op == kSelNew && mLiveSelAny)   // a range inside a selection looks only there
      for (size_t i = 0; i < region.size(); i++)
         region[i] = std::min(region[i], mLiveSel[i]);
   mSel = mLiveSel;
   mSelAny = mLiveSelAny;
   if (op == kSelNew || !mSelAny)
      mSel = op == kSelSub || op == kSelIntersect ? std::vector<uint8_t>(region.size(), 0) : region;
   else
      for (size_t i = 0; i < region.size(); i++)
      {
         if (op == kSelAdd) mSel[i] = std::max(mSel[i], region[i]);
         else if (op == kSelSub) mSel[i] = (uint8_t)(mSel[i] * (255 - region[i]) / 255);
         else mSel[i] = std::min(mSel[i], region[i]);
      }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
}

bool Scene2DNode::QuickSelectBegin(bool sampleAll, int layerId)
{
   Layers();
   const Layer* l = FindConst(layerId);
   mQuickDisc = !sampleAll && l != nullptr && l->kind == Kind::Group;
   if (!LiveSelBegin(sampleAll, layerId))
      return false;
   mSel = mLiveSel;
   return true;
}

void Scene2DNode::QuickSelectDab(float x, float y, float radius, bool subtract)
{
   const int W = mDoc.w, H = mDoc.h;
   if (mLivePic.size() != (size_t)W * H * 4 || mSel.size() != (size_t)W * H)
      return;
   std::vector<uint8_t> region;
   int box[4] = {};
   SelectMaths::QuickRegion(mQuickDisc ? nullptr : mLivePic.data(), W, H, x, (float)H - y, radius, 2.5f, region, box);
   for (int yy = 0; yy < box[3]; yy++)
      for (int xx = 0; xx < box[2]; xx++)
      {
         const uint8_t v = region[(size_t)yy * box[2] + xx];
         if (v == 0)
            continue;
         uint8_t& s = mSel[(size_t)(box[1] + yy) * W + box[0] + xx];
         s = subtract ? (uint8_t)std::min<int>(s, 255 - v) : std::max(s, v);
      }
   if (!subtract && !region.empty())
      mSelAny = true;
   mSelDirty = true;
}

void Scene2DNode::QuickSelectEnd(float enhanceRadius, float enhanceSmooth)
{
   const int W = mDoc.w, H = mDoc.h;
   if (enhanceRadius > 0.0f && !mQuickDisc && mLivePic.size() == (size_t)W * H * 4 && mSel.size() == (size_t)W * H &&
       mLiveSel.size() == mSel.size())
   {
      // Enhance Edge: the stroke's new edge redrawn from the picture and smoothed; the rest of the selection is left.
      int x0 = W, y0 = H, x1 = -1, y1 = -1;
      for (int y = 0; y < H; y++)
         for (int x = 0; x < W; x++)
            if (mSel[(size_t)y * W + x] != mLiveSel[(size_t)y * W + x])
            {
               x0 = std::min(x0, x); x1 = std::max(x1, x);
               y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
      if (x1 >= 0)
      {
         const int pad = (int)std::ceil(enhanceRadius + enhanceSmooth) + 2;
         x0 = std::max(0, x0 - pad); y0 = std::max(0, y0 - pad);
         x1 = std::min(W - 1, x1 + pad); y1 = std::min(H - 1, y1 + pad);
         std::vector<uint8_t> r = mSel;
         SelectMaths::RefineParams p;
         p.radius = enhanceRadius;
         p.smooth = enhanceSmooth;
         SelectMaths::Refine(mLivePic.data(), r, W, H, p);
         for (int y = y0; y <= y1; y++)
            std::copy(&r[(size_t)y * W + x0], &r[(size_t)y * W + x1] + 1, &mSel[(size_t)y * W + x0]);
      }
   }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   LiveSelEnd(true);
   mQuickDisc = false;
}

int Scene2DNode::LiveDepthPoll()
{
   const int W = mDoc.w, H = mDoc.h;
   if (!mLiveDepth.empty())
      return 1;
   if (mLivePic.size() != (size_t)W * H * 4 || !AiSelect::DepthAvailable())
      return -1;
   if (mLiveDepthKey == 0)
   {
      auto pic = std::make_shared<AiSelect::Picture>();   // the model takes the picture top row first
      pic->w = W;
      pic->h = H;
      pic->rgba.resize(mLivePic.size());
      for (int y = 0; y < H; y++)
         std::memcpy(&pic->rgba[(size_t)y * W * 4], &mLivePic[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
      mLiveDepthKey = AiSelect::KeyOf(*pic) | 1;
      if (!AiSelect::DepthReady(mLiveDepthKey))
         AiSelect::RequestDepth(pic, mLiveDepthKey);
   }
   if (!AiSelect::DepthReady(mLiveDepthKey))
      return AiSelect::Busy() ? 0 : -1;
   std::vector<float> d;
   int dw = 0, dh = 0;
   if (!AiSelect::Depth(mLiveDepthKey, d, dw, dh) || dw != W || dh != H)
      return -1;
   SetLiveDepth(d);
   return 1;
}

void Scene2DNode::SetLiveDepth(const std::vector<float>& topDown)
{
   const int W = mDoc.w, H = mDoc.h;
   if (topDown.size() != (size_t)W * H)
      return;
   mLiveDepth.resize(topDown.size());
   for (int y = 0; y < H; y++)
      std::memcpy(&mLiveDepth[(size_t)y * W], &topDown[(size_t)(H - 1 - y) * W], (size_t)W * sizeof(float));
}

float Scene2DNode::LiveDepthAt(float x, float y) const
{
   const int W = mDoc.w, H = mDoc.h, px = (int)std::floor(x), py = H - 1 - (int)std::floor(y);
   if (mLiveDepth.size() != (size_t)W * H || px < 0 || py < 0 || px >= W || py >= H)
      return -1.0f;
   return mLiveDepth[(size_t)py * W + px];
}

void Scene2DNode::DepthPreview(float lo, float hi, float soft, bool invert, int op)
{
   const int W = mDoc.w, H = mDoc.h;
   if (mLiveDepth.size() != (size_t)W * H || mLiveSel.size() != (size_t)W * H)
      return;
   std::vector<uint8_t> region;
   SelectMaths::DepthSelect(mLiveDepth, W, H, lo, hi, soft, region);
   LiveCombine(region, invert, op);
}

bool Scene2DNode::SelectColorRange(const std::vector<SelectMaths::Sample>& s, const SelectMaths::RangeParams& p, bool invert,
                                   int op, bool sampleAll, int layerId)
{
   if (!LiveSelBegin(sampleAll, layerId))
      return false;
   ColorRangePreview(s, p, invert, op);
   LiveSelEnd(true);
   return true;
}

namespace
{
   // Replace Color's shift on one colour, 0..1: hue turns, saturation and lightness move toward 1 (or 0) by their share.
   void ReplaceShift(float rgb[3], float hue, float sat, float light)
   {
      const float r = rgb[0], g = rgb[1], b = rgb[2];
      const float mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
      float h = 0.0f, l = (mx + mn) * 0.5f, s = 0.0f;
      if (d > 1e-6f)
      {
         s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
         if (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
         else if (mx == g) h = (b - r) / d + 2.0f;
         else h = (r - g) / d + 4.0f;
         h /= 6.0f;
      }
      h = std::fmod(h + hue / 360.0f + 1.0f, 1.0f);
      const float ks = std::clamp(sat / 100.0f, -1.0f, 1.0f), kl = std::clamp(light / 100.0f, -1.0f, 1.0f);
      s = ks >= 0 ? s + (1.0f - s) * ks : s * (1.0f + ks);
      l = kl >= 0 ? l + (1.0f - l) * kl : l * (1.0f + kl);
      auto ch = [](float p, float q, float t) {
         t = t < 0 ? t + 1 : (t > 1 ? t - 1 : t);
         if (t < 1.0f / 6) return p + (q - p) * 6 * t;
         if (t < 0.5f) return q;
         if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
         return p;
      };
      if (s <= 1e-6f)
      {
         rgb[0] = rgb[1] = rgb[2] = l;
         return;
      }
      const float q = l < 0.5f ? l * (1 + s) : l + s - l * s, pp = 2 * l - q;
      rgb[0] = ch(pp, q, h + 1.0f / 3);
      rgb[1] = ch(pp, q, h);
      rgb[2] = ch(pp, q, h - 1.0f / 3);
   }
}

bool Scene2DNode::ReplaceColor(int layerId, const std::vector<SelectMaths::Sample>& s, const SelectMaths::RangeParams& p,
                               float hue, float sat, float light, std::string* err, const float* fromRgb)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   auto fail = [&](const char* why) {
      if (err != nullptr)
         *err = std::string("replaceColor: ") + why;
      return false;
   };
   Layer* l = FindLayer(layerId);
   if (l == nullptr || l->kind != Kind::Paint)
      return fail("not a paint layer");
   if ((EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return fail("the layer's pixels are locked");
   if (fromRgb != nullptr)
      for (int j = 0; j < 3; j++)
         if (!std::isfinite(fromRgb[j]) || fromRgb[j] < 0.0f || fromRgb[j] > 1.0f)
            return fail("bad colour");
   if (s.empty() && fromRgb == nullptr)
      return fail("no samples");
   for (const SelectMaths::Sample& k : s)
      if (!(k.x >= 0 && k.y >= 0 && k.x < (float)W && k.y < (float)H))
         return fail("a sample is outside the document");
   std::vector<SelectMaths::Sample> clicks = s;
   if (fromRgb != nullptr && std::none_of(s.begin(), s.end(), [](const SelectMaths::Sample& k) { return !k.subtract; }))
   {
      SelectMaths::Sample f;
      std::copy(fromRgb, fromRgb + 3, f.rgb);
      clicks.insert(clicks.begin(), f);   // the From colour, then the clicks that take colours out of it
      std::fprintf(stderr, "PROBE replace colour from %.3f %.3f %.3f before %zu subtracts\n", fromRgb[0], fromRgb[1], fromRgb[2], s.size());
   }
   SyncSurfaces();
   auto it = mSurfaces.find(layerId);
   const bool plain = l->sx == 1.0f && l->sy == 1.0f && l->angle == 0.0f && l->shear == 0.0f && !l->flipX && !l->flipY &&
                      !l->is3d && !l->warpOn && std::fabs(l->x - l->ax * W) < 1e-3f && std::fabs(l->y - l->ay * H) < 1e-3f;
   if (it == mSurfaces.end() || l->pw != W || l->ph != H || !plain)
      return fail("the layer must fill the document, unmoved");
   std::vector<uint8_t> px;
   ReadRect(it->second.fbo, 0, 0, W, H, px);
   std::vector<SelectMaths::Sample> flip = clicks;   // the surface is bottom row first
   for (SelectMaths::Sample& k : flip)
      k.y = (float)H - k.y;
   std::vector<uint8_t> region;
   SelectMaths::ColorRange(px.data(), W, H, flip, p, region);
   const bool limit = mSelAny && mSel.size() == region.size();
   size_t n = 0;
   for (size_t i = 0; i < region.size(); i++)
   {
      const float k = (limit ? std::min(region[i], mSel[i]) : region[i]) / 255.0f;
      uint8_t* q = &px[i * 4];
      if (k <= 0.0f || q[3] == 0)
         continue;
      float c[3] = { q[0] / 255.0f, q[1] / 255.0f, q[2] / 255.0f };
      ReplaceShift(c, hue, sat, light);
      for (int j = 0; j < 3; j++)
         q[j] = (uint8_t)std::lround(std::clamp(q[j] + (c[j] * 255.0f - q[j]) * k, 0.0f, 255.0f));
      n++;
   }
   WriteRect(it->second.fbo, 0, 0, W, H, px.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(it->second, l->tiles, all);
   std::fprintf(stderr, "PROBE replace colour layer %d changed %zu px\n", layerId, n);
   Commit();
   return true;
}

bool Scene2DNode::ApplyAdjustment(int layerId, const Layer& adj, std::string* err)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   auto fail = [&](const char* why) {
      if (err != nullptr)
         *err = std::string("applyAdjustment: ") + why;
      std::fprintf(stderr, "PROBE apply adjustment refused: %s\n", why);
      return false;
   };
   if (adj.adjType < 0 || adj.adjType >= kAdjCount || adj.adjType == kAdjLensBlur)
      return fail("not an adjustment that can be applied");
   Layer* l = FindLayer(layerId);
   if (l == nullptr || l->kind != Kind::Paint)
      return fail("not a paint layer");
   if ((EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return fail("the layer's pixels are locked");
   SyncSurfaces();
   auto it = mSurfaces.find(layerId);
   const bool plain = l->sx == 1.0f && l->sy == 1.0f && l->angle == 0.0f && l->shear == 0.0f && !l->flipX && !l->flipY &&
                      !l->is3d && !l->warpOn && std::fabs(l->x - l->ax * W) < 1e-3f && std::fabs(l->y - l->ay * H) < 1e-3f;
   if (it == mSurfaces.end() || l->pw != W || l->ph != H || !plain)
      return fail("the layer must fill the document, unmoved");
   Layer a = adj;
   a.kind = Kind::Adjust;
   a.adjDrive = 0;   // a baked edit takes the amount as set, never a drive's
   // A layer's pixels are display-encoded; the composite feeds the shader linear light. Run it on the encoded
   // values as a non-linear document would, so the baked pixels match what the live layer showed.
   const bool wasLinear = mDoc.linear;
   mDoc.linear = false;
   const bool ran = RunAdjust(a, it->second.fbo.tex);
   mDoc.linear = wasLinear;
   if (!ran)
      return fail("the adjustment did not run");
   std::vector<uint8_t> px, done;
   ReadRect(it->second.fbo, 0, 0, W, H, px);
   ReadRect(mAdjFbo, 0, 0, W, H, done);
   const bool limit = mSelAny && mSel.size() == (size_t)W * H;
   size_t n = 0;
   for (size_t i = 0; i < (size_t)W * H; i++)
   {
      const float k = limit ? mSel[i] / 255.0f : 1.0f;
      if (k <= 0.0f)
         continue;
      uint8_t* q = &px[i * 4];
      const uint8_t* d = &done[i * 4];
      bool any = false;
      for (int j = 0; j < 4; j++)
      {
         const uint8_t v = (uint8_t)std::lround(q[j] + (d[j] - q[j]) * k);
         any = any || v != q[j];
         q[j] = v;
      }
      n += any ? 1 : 0;
   }
   WriteRect(it->second.fbo, 0, 0, W, H, px.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(it->second, l->tiles, all);
   std::fprintf(stderr, "PROBE apply adjustment %d to layer %d: %zu px changed, selection %d\n", adj.adjType, layerId, n, limit ? 1 : 0);
   Commit();
   return true;
}

namespace
{
   // CIELab (D65) from display sRGB and back, for Match Color.
   void SrgbToLab(const float c[3], float lab[3])
   {
      float l[3];
      for (int i = 0; i < 3; i++)
         l[i] = ColorSpace::SrgbToLinear(std::clamp(c[i], 0.0f, 1.0f));
      const float X = (0.4124564f * l[0] + 0.3575761f * l[1] + 0.1804375f * l[2]) / 0.95047f;
      const float Y = 0.2126729f * l[0] + 0.7151522f * l[1] + 0.0721750f * l[2];
      const float Z = (0.0193339f * l[0] + 0.1191920f * l[1] + 0.9503041f * l[2]) / 1.08883f;
      auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
      const float fx = f(X), fy = f(Y), fz = f(Z);
      lab[0] = 116.0f * fy - 16.0f;
      lab[1] = 500.0f * (fx - fy);
      lab[2] = 200.0f * (fy - fz);
   }
   void LabToSrgb(const float lab[3], float c[3])
   {
      const float fy = (lab[0] + 16.0f) / 116.0f, fx = fy + lab[1] / 500.0f, fz = fy - lab[2] / 200.0f;
      auto g = [](float t) { return t * t * t > 0.008856f ? t * t * t : (t - 16.0f / 116.0f) / 7.787f; };
      const float X = g(fx) * 0.95047f, Y = g(fy), Z = g(fz) * 1.08883f;
      const float l[3] = { 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z, -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z,
                           0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z };
      for (int i = 0; i < 3; i++)
         c[i] = ColorSpace::LinearToSrgb(std::clamp(l[i], 0.0f, 1.0f));
   }
   struct LabStats
   {
      double mean[3] = { 0, 0, 0 }, sd[3] = { 0, 0, 0 };
      size_t n = 0;
   };
   LabStats StatsOf(const uint8_t* px, size_t count, bool linear)
   {
      LabStats s;
      double sum[3] = { 0, 0, 0 }, sq[3] = { 0, 0, 0 };
      for (size_t i = 0; i < count; i++)
      {
         const uint8_t* q = &px[i * 4];
         if (q[3] == 0)
            continue;
         float c[3] = { q[0] / 255.0f, q[1] / 255.0f, q[2] / 255.0f }, lab[3];
         if (linear)
            for (float& v : c)
               v = ColorSpace::LinearToSrgb(v);
         SrgbToLab(c, lab);
         for (int k = 0; k < 3; k++)
         {
            sum[k] += lab[k];
            sq[k] += (double)lab[k] * lab[k];
         }
         s.n++;
      }
      for (int k = 0; k < 3 && s.n > 0; k++)
      {
         s.mean[k] = sum[k] / (double)s.n;
         s.sd[k] = std::sqrt(std::max(0.0, sq[k] / (double)s.n - s.mean[k] * s.mean[k]));
      }
      return s;
   }
}

// Ported from PhotoCraft algo/src/tone.rs match_color (Apache 2.0).
bool Scene2DNode::MatchColor(int layerId, const uint8_t* srcRgba, size_t srcPixels, float luminance, float intensity, float fade,
                             bool neutralize, std::string* err)
{
   Layers();
   auto fail = [&](const char* why) {
      if (err != nullptr)
         *err = std::string("matchColor: ") + why;
      std::fprintf(stderr, "PROBE match color refused: %s\n", why);
      return false;
   };
   if (!std::isfinite(luminance) || !std::isfinite(intensity) || !std::isfinite(fade))
      return fail("bad option");
   Layer* l = FindLayer(layerId);
   if (l == nullptr || l->kind != Kind::Paint)
      return fail("not a paint layer");
   if ((EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return fail("the layer's pixels are locked");
   SyncSurfaces();
   auto it = mSurfaces.find(layerId);
   if (it == mSurfaces.end() || it->second.fbo.fbo == 0)
      return fail("no pixels");
   const int W = l->pw, H = l->ph;
   std::vector<uint8_t> px;
   ReadRect(it->second.fbo, 0, 0, W, H, px);
   if (px.size() != (size_t)W * H * 4)
      return fail("no pixels");
   const bool lin = false;   // 8-bit surfaces hold display values, as Pick and ApplyAdjustment read them
   const LabStats tgt = StatsOf(px.data(), (size_t)W * H, lin);
   if (tgt.n == 0)
      return fail("the layer is empty");
   LabStats src = tgt;
   if (srcRgba != nullptr)
   {
      src = StatsOf(srcRgba, srcPixels, false);
      if (src.n == 0)
         return fail("the source is empty");
   }
   const bool plain = l->sx == 1.0f && l->sy == 1.0f && l->angle == 0.0f && l->shear == 0.0f && !l->flipX && !l->flipY && !l->is3d &&
                      !l->warpOn && W == mDoc.w && H == mDoc.h && std::fabs(l->x - l->ax * W) < 1e-3f && std::fabs(l->y - l->ay * H) < 1e-3f;
   const bool limit = mSelAny && mSel.size() == (size_t)mDoc.w * mDoc.h;
   if (limit && !plain)
      return fail("a selection needs the layer to fill the document, unmoved");
   const float lum = luminance / 100.0f, inten = intensity / 100.0f, fd = std::clamp(fade / 100.0f, 0.0f, 1.0f);
   double scale[3], dst[3] = { src.mean[0], src.mean[1], src.mean[2] };
   for (int k = 0; k < 3; k++)
      scale[k] = tgt.sd[k] > 1e-4 ? src.sd[k] / tgt.sd[k] : 1.0;
   if (neutralize)
      dst[1] = dst[2] = 0.0;
   size_t n = 0;
   for (size_t i = 0; i < (size_t)W * H; i++)
   {
      uint8_t* q = &px[i * 4];
      const float k = limit ? mSel[i] / 255.0f : 1.0f;
      if (q[3] == 0 || k <= 0.0f)
         continue;
      float c[3] = { q[0] / 255.0f, q[1] / 255.0f, q[2] / 255.0f }, lab[3], o[3];
      if (lin)
         for (float& v : c)
            v = ColorSpace::LinearToSrgb(v);
      SrgbToLab(c, lab);
      const float out[3] = { std::clamp((float)(((lab[0] - tgt.mean[0]) * scale[0] + dst[0]) * lum), 0.0f, 100.0f),
                             (float)(((lab[1] - tgt.mean[1]) * scale[1] + dst[1]) * inten),
                             (float)(((lab[2] - tgt.mean[2]) * scale[2] + dst[2]) * inten) };
      LabToSrgb(out, o);
      bool any = false;
      for (int j = 0; j < 3; j++)
      {
         float v = std::clamp(o[j] + (c[j] - o[j]) * fd, 0.0f, 1.0f);
         if (lin)
            v = ColorSpace::SrgbToLinear(v);
         const uint8_t b = (uint8_t)std::lround(q[j] + (v * 255.0f - q[j]) * k);
         any = any || b != q[j];
         q[j] = b;
      }
      n += any ? 1 : 0;
   }
   WriteRect(it->second.fbo, 0, 0, W, H, px.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(it->second, l->tiles, all);
   std::fprintf(stderr, "PROBE match color layer %d: %zu px changed (source %s, fade %.0f, neutralize %d)\n", layerId, n,
                srcRgba != nullptr ? "picture" : "none", fade, (int)neutralize);
   Commit();
   return true;
}

bool Scene2DNode::SelectObjectPoint(float x, float y, int op, bool sampleAll, int layerId)
{
   std::vector<uint8_t> px;
   if (!ReadSelectPicture(sampleAll, layerId, px))
      return false;
   const int W = mDoc.w, H = mDoc.h, cx = (int)std::floor(x), cy = H - 1 - (int)std::floor(y);   // read is bottom row first
   std::vector<uint8_t> region((size_t)W * H, 0);
   const bool inside = cx >= 0 && cy >= 0 && cx < W && cy < H;
   const size_t seed = inside ? (size_t)cy * W + cx : 0;
   if (!inside || px[seed * 4 + 3] == 0)
   {
      CombineSelection(region, op);
      return false;
   }
   // Grows the 4-connected region from the click through the pixels `take` accepts; returns how many it took.
   std::vector<int> stack;
   auto flood = [&](auto take) {
      std::fill(region.begin(), region.end(), 0);
      stack.assign(1, (int)seed);
      region[seed] = 1;
      size_t n = 0;
      while (!stack.empty())
      {
         const int i = stack.back();
         stack.pop_back();
         n++;
         const int ix = i % W, iy = i / W;
         const int nb[4] = { ix > 0 ? i - 1 : -1, ix < W - 1 ? i + 1 : -1, iy > 0 ? i - W : -1, iy < H - 1 ? i + W : -1 };
         for (int j : nb)
            if (j >= 0 && region[j] == 0 && take((size_t)i, (size_t)j))
            {
               region[j] = 1;
               stack.push_back(j);
            }
      }
      return n;
   };
   const size_t shape = flood([&](size_t, size_t j) { return px[j * 4 + 3] != 0; });
   const bool nearlyAll = shape * 10 >= (size_t)W * H * 9;
   if (!nearlyAll)
      for (size_t i = 0; i < region.size(); i++)
         region[i] = region[i] != 0 ? px[i * 4 + 3] : 0;
   else
   {
      // The shape is (nearly) everything: grow through pixels close to their neighbour (alpha counted), so a smooth
      // ramp is one object and an opaque shape stands out from a faint layer of the same colour.
      const int step = 12;   // per channel, between neighbours
      flood([&](size_t i, size_t j) {
         const uint8_t *a = &px[i * 4], *c = &px[j * 4];
         return std::abs(c[0] - a[0]) <= step && std::abs(c[1] - a[1]) <= step && std::abs(c[2] - a[2]) <= step &&
                std::abs(c[3] - a[3]) <= step;
      });
      for (uint8_t& v : region)
         v = v != 0 ? 255 : 0;
   }
   CombineSelection(region, op);
   return true;
}

int Scene2DNode::SelectObjectAi(float x0, float y0, float x1, float y1, int op, bool sampleAll, int layerId)
{
   if (!AiSelect::Available())
      return 0;
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> px;
   const Layer* l = FindConst(layerId);
   auto it = l != nullptr && l->kind == Kind::Paint ? mSurfaces.find(layerId) : mSurfaces.end();
   if (!sampleAll && it != mSurfaces.end() && l->pw == W && l->ph == H)
      ReadRect(it->second.fbo, 0, 0, W, H, px);
   else if (mOut.fbo != 0 && mOut.w == W && mOut.h == H)
      ReadRect(mOut, 0, 0, W, H, px);
   if (px.size() != (size_t)W * H * 4)
      return 0;
   // The encoder takes the picture top row first; the read is bottom row first.
   auto pic = std::make_shared<AiSelect::Picture>();
   pic->w = W;
   pic->h = H;
   pic->rgba.resize(px.size());
   for (int y = 0; y < H; y++)
      std::memcpy(&pic->rgba[(size_t)y * W * 4], &px[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
   const uint64_t key = AiSelect::KeyOf(*pic);
   const bool box = std::fabs(x1 - x0) >= 4.0f && std::fabs(y1 - y0) >= 4.0f;
   const float p[4] = { std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1) };
   if (AiSelect::Ready(key))
   {
      mAiWait.on = false;
      return AiDecodeApply(key, W, H, p, box, op) ? 1 : 0;
   }
   AiSelect::Request(pic, key);
   mAiWait.on = true;
   mAiWait.key = key;
   mAiWait.w = W;
   mAiWait.h = H;
   mAiWait.op = op;
   mAiWait.box = box;
   std::copy(p, p + 4, mAiWait.p);
   return 2;
}

bool Scene2DNode::SelectSubject(int op, bool sampleAll, int layerId)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   auto px = std::make_shared<std::vector<uint8_t>>();
   const Layer* l = FindConst(layerId);
   auto it = l != nullptr && l->kind == Kind::Paint ? mSurfaces.find(layerId) : mSurfaces.end();
   if (!sampleAll && it != mSurfaces.end() && l->pw == W && l->ph == H)
      ReadRect(it->second.fbo, 0, 0, W, H, *px);
   else if (mOut.fbo != 0 && mOut.w == W && mOut.h == H)
      ReadRect(mOut, 0, 0, W, H, *px);
   if (px->size() != (size_t)W * H * 4 || mSubjectJob.valid())
      return false;
   mAiWait.on = false;
   mSubjectW = W;
   mSubjectH = H;
   mSubjectOp = op;
   mSubjectPx = px;
   // Matting takes a second or so: never on the UI thread.
   mSubjectJob = std::async(std::launch::async, [px, W, H]() {
      SubjectResult r;
      std::string err;
      r.ok = Platform::SubjectMask(*px, W, H, Platform::MattingMode::Subject, r.mask, err) && r.mask.size() == (size_t)W * H;
      return r;
   });
   return true;
}

bool Scene2DNode::SubjectLanded()
{
   if (!mSubjectJob.valid() || mSubjectJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return false;
   SubjectResult r = mSubjectJob.get();
   std::shared_ptr<const std::vector<uint8_t>> px = std::move(mSubjectPx);
   const int W = mSubjectW, H = mSubjectH;
   if (!r.ok || W != mDoc.w || H != mDoc.h || px == nullptr)
      return false;
   // The subject's box (document px, top row first).
   int x0 = W, y0 = H, x1 = -1, y1 = -1;
   for (int row = 0; row < H; row++)
      for (int x = 0; x < W; x++)
         if (r.mask[(size_t)row * W + x] > 127)
         {
            const int y = H - 1 - row;
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            y0 = std::min(y0, y); y1 = std::max(y1, y);
         }
   if (x1 < x0)
      return false;   // nothing stood out
   if (!AiSelect::Available() || x1 - x0 < 4 || y1 - y0 < 4)
   {
      CombineSelection(r.mask, mSubjectOp);
      return true;
   }
   auto pic = std::make_shared<AiSelect::Picture>();
   pic->w = W;
   pic->h = H;
   pic->rgba.resize(px->size());
   for (int y = 0; y < H; y++)
      std::memcpy(&pic->rgba[(size_t)y * W * 4], &(*px)[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
   const uint64_t key = AiSelect::KeyOf(*pic);
   const float pad = 0.02f * std::max(W, H);
   const float p[4] = { std::max(0.0f, x0 - pad), std::max(0.0f, y0 - pad), std::min((float)W, x1 + 1 + pad), std::min((float)H, y1 + 1 + pad) };
   if (AiSelect::Ready(key))
      return AiDecodeApply(key, W, H, p, true, mSubjectOp, &r.mask);
   AiSelect::Request(pic, key);
   mAiWait.on = true;
   mAiWait.key = key;
   mAiWait.w = W;
   mAiWait.h = H;
   mAiWait.op = mSubjectOp;
   mAiWait.box = true;
   std::copy(p, p + 4, mAiWait.p);
   mAiWait.guide = std::move(r.mask);
   return false;
}

// Layer > Mask All Objects: Select Subject's matting written into the layer's pixel mask; the selection stays as it was.
bool Scene2DNode::MaskAllObjects(int id)
{
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind == Kind::Group || AiSelectWaiting())
      return false;
   mMaskTo = { id, mSel, mSelAny, mSelApron };
   if (!SelectSubject(kSelNew, false, id))
   {
      mMaskTo.id = 0;
      return false;
   }
   return true;
}

bool Scene2DNode::AiSelectPoll()
{
   const bool landed = AiSelectStep();
   if (mMaskTo.id == 0 || AiSelectWaiting())
      return landed;
   // A Mask All Objects run ended: what it found becomes the mask, and the selection before it comes back.
   const int id = mMaskTo.id;
   mMaskTo.id = 0;
   bool ok = false;
   if (landed && mSelAny)
   {
      RemovePixelMask(id);
      ok = MaskFromSelection(id);
   }
   mSel = std::move(mMaskTo.sel);
   mSelAny = mMaskTo.any;
   mSelApron = std::move(mMaskTo.apron);
   mSelDirty = true;
   StoreSelection();
   return ok;
}

bool Scene2DNode::AiSelectStep()
{
   if (SubjectLanded())
      return true;
   if (!mAiWait.on)
      return false;
   if (mAiWait.w != mDoc.w || mAiWait.h != mDoc.h)
   {
      mAiWait.on = false;   // the document changed size: the prompt no longer points at the same place
      return false;
   }
   if (!AiSelect::Ready(mAiWait.key))
   {
      if (!AiSelect::Busy())
         mAiWait.on = false;   // the analysis failed (LastError says why): drop the prompt
      return false;
   }
   mAiWait.on = false;
   const std::vector<uint8_t> guide = std::move(mAiWait.guide);
   mAiWait.guide.clear();
   return AiDecodeApply(mAiWait.key, mAiWait.w, mAiWait.h, mAiWait.p, mAiWait.box, mAiWait.op, guide.empty() ? nullptr : &guide);
}

bool Scene2DNode::AiDecodeApply(uint64_t key, int w, int h, const float p[4], bool box, int op, const std::vector<uint8_t>* guide)
{
   AiSelect::Prompt pr;
   if (box)
   {
      pr.hasBox = true;
      std::copy(p, p + 4, pr.box);
   }
   else
   {
      pr.xy = { p[0], p[1] };
      pr.labels = { 1 };
   }
   std::vector<uint8_t> cov;
   std::string err;
   if (!AiSelect::Decode(key, pr, cov, err) || cov.size() != (size_t)w * h)
      return false;
   // Coverage is top row first; the selection is bottom row first.
   std::vector<uint8_t> region((size_t)w * h);
   for (int y = 0; y < h; y++)
      std::memcpy(&region[(size_t)(h - 1 - y) * w], &cov[(size_t)y * w], (size_t)w);
   if (guide != nullptr && guide->size() == region.size())
   {
      // SAM drew something else from the subject's box (a part, or the background): keep the matting's mask.
      size_t inter = 0, uni = 0;
      for (size_t i = 0; i < region.size(); i++)
      {
         const bool a = region[i] > 127, b = (*guide)[i] > 127;
         inter += a && b;
         uni += a || b;
      }
      if (uni == 0 || inter * 2 < uni)
         region = *guide;
   }
   CombineSelection(region, op);
   return true;
}

void Scene2DNode::WarpGridRest(WarpGrid& g, const float c[8])
{
   g.pts.assign((size_t)g.Count() * 2, 0.0f);
   for (int j = 0; j < g.Ny(); j++)
      for (int i = 0; i < g.Nx(); i++)
      {
         const float u = g.us[i], v = g.vs[j];
         const size_t k = ((size_t)j * g.Nx() + i) * 2;
         // TL TR BR BL
         g.pts[k] = (1 - v) * ((1 - u) * c[0] + u * c[2]) + v * ((1 - u) * c[6] + u * c[4]);
         g.pts[k + 1] = (1 - v) * ((1 - u) * c[1] + u * c[3]) + v * ((1 - u) * c[7] + u * c[5]);
      }
}

bool Scene2DNode::WarpGridParam(const WarpGrid& g, float x, float y, float& s, float& t)
{
   if (g.Nx() < 2 || g.Ny() < 2 || g.pts.size() != (size_t)g.Count() * 2)
      return false;
   auto P = [&](int i, int j, int c) { return g.pts[((size_t)j * g.Nx() + i) * 2 + c]; };
   // Each cell's bilinear map inverted by Newton; the first cell that holds the point wins, else the nearest.
   float bestErr = 1e30f;
   for (int j = 0; j + 1 < g.Ny(); j++)
      for (int i = 0; i + 1 < g.Nx(); i++)
      {
         float fu = 0.5f, fv = 0.5f;
         for (int it = 0; it < 16; it++)
         {
            float q[2], du[2], dv[2];
            for (int c = 0; c < 2; c++)
            {
               const float a = P(i, j, c), b = P(i + 1, j, c), d = P(i, j + 1, c), e = P(i + 1, j + 1, c);
               q[c] = (1 - fv) * ((1 - fu) * a + fu * b) + fv * ((1 - fu) * d + fu * e) - (c == 0 ? x : y);
               du[c] = (1 - fv) * (b - a) + fv * (e - d);
               dv[c] = (1 - fu) * (d - a) + fu * (e - b);
            }
            const float det = du[0] * dv[1] - du[1] * dv[0];
            if (std::fabs(det) < 1e-9f)
               break;
            fu -= (q[0] * dv[1] - q[1] * dv[0]) / det;
            fv -= (du[0] * q[1] - du[1] * q[0]) / det;
         }
         const float cu = std::clamp(fu, 0.0f, 1.0f), cv = std::clamp(fv, 0.0f, 1.0f);
         const float err = std::fabs(fu - cu) + std::fabs(fv - cv);
         if (err < bestErr)
         {
            bestErr = err;
            s = g.us[i] + cu * (g.us[i + 1] - g.us[i]);
            t = g.vs[j] + cv * (g.vs[j + 1] - g.vs[j]);
         }
      }
   return bestErr < 1e30f;
}

namespace
{
   using WarpGrid = Scene2DNode::WarpGrid;
   const float kThirds[4] = { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
   const float kLineEps = 1e-4f;    // two grid lines closer than this (in box fractions) are one
   const float kLossPx = 1e-3f;     // a point this close to its rest place carries no edit

   bool LinesAre(const std::vector<float>& k, const float* want, int n)
   {
      if ((int)k.size() != n)
         return false;
      for (int i = 0; i < n; i++)
         if (std::fabs(k[i] - want[i]) > kLineEps)
            return false;
      return true;
   }

   // Every line of k is one of want's, the first and last among them (a Default grid with some thirds gone).
   bool LinesWithin(const std::vector<float>& k, const float* want, int n)
   {
      if (k.size() < 2 || std::fabs(k.front() - want[0]) > kLineEps || std::fabs(k.back() - want[n - 1]) > kLineEps)
         return false;
      for (float v : k)
         if (std::none_of(want, want + n, [&](float w) { return std::fabs(v - w) <= kLineEps; }))
            return false;
      return true;
   }

   // A new line at fraction f: column (axis 0) or row (axis 1), its points on the bilinear pieces either side.
   bool InsertLine(WarpGrid& g, int axis, float f)
   {
      std::vector<float>& k = axis == 0 ? g.us : g.vs;
      if ((int)k.size() >= Scene2DNode::kWarpLinesMax || f <= kLineEps || f >= 1.0f - kLineEps)
         return false;
      const int at = (int)(std::upper_bound(k.begin(), k.end(), f) - k.begin());
      if (std::fabs(k[at - 1] - f) < kLineEps || std::fabs(k[at] - f) < kLineEps)
         return false;
      const float w = (f - k[at - 1]) / (k[at] - k[at - 1]);
      const int nx = g.Nx(), ny = g.Ny();
      std::vector<float> out;
      if (axis == 0)
      {
         out.reserve((size_t)(nx + 1) * ny * 2);
         for (int j = 0; j < ny; j++)
            for (int i = 0; i <= nx; i++)
               for (int c = 0; c < 2; c++)
               {
                  if (i < at)
                     out.push_back(g.pts[((size_t)j * nx + i) * 2 + c]);
                  else if (i > at)
                     out.push_back(g.pts[((size_t)j * nx + i - 1) * 2 + c]);
                  else
                     out.push_back((1 - w) * g.pts[((size_t)j * nx + at - 1) * 2 + c] + w * g.pts[((size_t)j * nx + at) * 2 + c]);
               }
      }
      else
      {
         out.reserve((size_t)nx * (ny + 1) * 2);
         for (int j = 0; j <= ny; j++)
            for (int i = 0; i < nx; i++)
               for (int c = 0; c < 2; c++)
               {
                  if (j < at)
                     out.push_back(g.pts[((size_t)j * nx + i) * 2 + c]);
                  else if (j > at)
                     out.push_back(g.pts[((size_t)(j - 1) * nx + i) * 2 + c]);
                  else
                     out.push_back((1 - w) * g.pts[((size_t)(at - 1) * nx + i) * 2 + c] + w * g.pts[((size_t)at * nx + i) * 2 + c]);
               }
      }
      k.insert(k.begin() + at, f);
      g.pts.swap(out);
      return true;
   }

   void RemoveLine(WarpGrid& g, int axis, int at)
   {
      const int nx = g.Nx(), ny = g.Ny();
      std::vector<float> out;
      for (int j = 0; j < ny; j++)
         for (int i = 0; i < nx; i++)
            if ((axis == 0 ? i : j) != at)
               for (int c = 0; c < 2; c++)
                  out.push_back(g.pts[((size_t)j * nx + i) * 2 + c]);
      std::vector<float>& k = axis == 0 ? g.us : g.vs;
      k.erase(k.begin() + at);
      g.pts.swap(out);
   }

}

namespace
{
   // Whether line `at` of the edited grid still sits where the rest grid has it (no point on it was moved).
   bool LineUnmoved(const Scene2DNode::WarpGrid& moved, const Scene2DNode::WarpGrid& rest, int axis, int at)
   {
      const int nx = moved.Nx(), n = axis == 0 ? moved.Ny() : nx;
      for (int m = 0; m < n; m++)
      {
         const size_t i = axis == 0 ? (size_t)m * nx + at : (size_t)at * nx + m;
         for (int c = 0; c < 2; c++)
            if (std::fabs(moved.pts[i * 2 + c] - rest.pts[i * 2 + c]) > kLossPx)
               return false;
      }
      return true;
   }
}

bool Scene2DNode::SplitWarpGrid(WarpGrid& moved, WarpGrid& rest, float s, float t, int how)
{
   if (moved.us != rest.us || moved.vs != rest.vs || moved.pts.size() != (size_t)moved.Count() * 2 || rest.pts.size() != moved.pts.size())
      return false;
   s = std::clamp(s, 0.0f, 1.0f);
   t = std::clamp(t, 0.0f, 1.0f);
   bool ok = false;
   if (how == kWarpSplitRemove)
   {
      // The interior line nearest the click, columns winning a tie.
      auto nearLine = [](const std::vector<float>& k, float v, int& at) {
         float best = 1e30f;
         for (int i = 1; i + 1 < (int)k.size(); i++)
            if (std::fabs(k[i] - v) < best) { best = std::fabs(k[i] - v); at = i; }
         return best;
      };
      int iu = -1, iv = -1;
      const float du = nearLine(moved.us, s, iu), dv = nearLine(moved.vs, t, iv);
      const int axis = iu >= 0 && du <= dv ? 0 : iv >= 0 ? 1 : -1;
      if (axis < 0)
         return false;
      RemoveLine(moved, axis, axis == 0 ? iu : iv);
      RemoveLine(rest, axis, axis == 0 ? iu : iv);
      // Thirds one way and only thirds the other: the Default grid again, its missing thirds put back unbent (where
      // they rest), so a grid bent before a split comes back as it was bent.
      const bool cols = LinesAre(moved.us, kThirds, 4) && LinesWithin(moved.vs, kThirds, 4);
      const bool rows = LinesAre(moved.vs, kThirds, 4) && LinesWithin(moved.us, kThirds, 4);
      if (cols || rows)
      {
         const int ax = cols ? 1 : 0;
         for (int third = 1; third <= 2; third++)
         {
            if (!InsertLine(rest, ax, kThirds[third]) || !InsertLine(moved, ax, kThirds[third]))
               continue;
            const std::vector<float>& k = ax == 0 ? moved.us : moved.vs;
            const int at = (int)(std::find_if(k.begin(), k.end(), [&](float v) { return std::fabs(v - kThirds[third]) <= kLineEps; }) - k.begin());
            const int nx = moved.Nx(), n = ax == 0 ? moved.Ny() : nx;
            for (int m = 0; m < n; m++)
            {
               const size_t i = ax == 0 ? (size_t)m * nx + at : (size_t)at * nx + m;
               moved.pts[i * 2] = rest.pts[i * 2];
               moved.pts[i * 2 + 1] = rest.pts[i * 2 + 1];
            }
         }
         moved.deflt = rest.deflt = true;
      }
      return true;
   }
   // A one-way split of the Default grid: the thirds it replaces go, when no point on them was moved.
   if (moved.deflt && how != kWarpSplitCross && LinesAre(moved.us, kThirds, 4) && LinesAre(moved.vs, kThirds, 4))
   {
      const int axis = how == kWarpSplitH ? 1 : 0;
      for (int at = 2; at >= 1; at--)
         if (LineUnmoved(moved, rest, axis, at))
         {
            RemoveLine(moved, axis, at);
            RemoveLine(rest, axis, at);
         }
   }
   // A horizontal split line divides rows; a vertical one divides columns.
   if (how != kWarpSplitH && InsertLine(moved, 0, s))
   {
      InsertLine(rest, 0, s);
      ok = true;
   }
   if (how != kWarpSplitV && InsertLine(moved, 1, t))
   {
      InsertLine(rest, 1, t);
      ok = true;
   }
   moved.deflt = rest.deflt = false;
   return ok;
}

bool Scene2DNode::MeshRest(int id, WarpGrid& out)
{
   Layers();
   const Layer* l = FindConst(id);
   float c[8];
   if (l == nullptr)
      return false;
   if (l->kind == Kind::Node && !l->HasSkin())
   {
      // An image / node layer is made into pixels the size of the page on the first pull, so its grid is the page's.
      const float w = (float)mDoc.w, h = (float)mDoc.h;
      const float pg[8] = { 0, 0, w, 0, w, h, 0, h };
      std::copy(pg, pg + 8, c);
   }
   else if (l->kind != Kind::Paint || !Corners(*l, c))
      return false;
   out = WarpGrid();
   WarpGridRest(out, c);
   return true;
}

bool Scene2DNode::MeshWarp(int id, const std::vector<std::string>& from, const WarpGrid& moved, bool store, bool nearest,
                           const WarpGrid* rest, const std::vector<std::string>* maskFrom)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0 || from.size() != l->tiles.size())
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   if (mWarpProgram == 0)
      mWarpProgram = GLUtil::CompileProgram(kMeshFrag);
   // The pixels as the warp began, kept beside the undo brush's (a negative id is never a layer's).
   Surface* src = SyncSurface(mUndoSurf, -id, l->pw, l->ph, from);
   if (mWarpProgram == 0 || src == nullptr)
      return false;
   const int nx = moved.Nx(), ny = moved.Ny();
   if (nx < 2 || ny < 2 || nx > kWarpLinesMax || ny > kWarpLinesMax || moved.pts.size() != (size_t)moved.Count() * 2 ||
       (rest != nullptr && (rest->us != moved.us || rest->vs != moved.vs || rest->pts.size() != moved.pts.size())))
      return false;
   // Each point's move in surface uv (y up), from where it rests: the grid fitted to the content when `rest` is
   // given, else the whole surface.
   std::vector<float> d((size_t)nx * ny * 2);
   float box[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
   if (rest != nullptr)
   {
      box[0] = box[1] = 1e30f;
      box[2] = box[3] = -1e30f;
   }
   for (int j = 0; j < ny; j++)
      for (int i = 0; i < nx; i++)
      {
         const size_t k = ((size_t)j * nx + i) * 2;
         float sx = 0, sy = 0, s = 1;
         DocToSurface(*l, 0, moved.pts[k], moved.pts[k + 1], sx, sy, s);
         float ru = moved.us[i], rv = 1.0f - moved.vs[j];
         if (rest != nullptr)
         {
            float rx = 0, ry = 0, rs = 1;
            DocToSurface(*l, 0, rest->pts[k], rest->pts[k + 1], rx, ry, rs);
            ru = rx / (float)l->pw;
            rv = ry / (float)l->ph;
            box[0] = std::min(box[0], ru);
            box[2] = std::max(box[2], ru);
            box[1] = std::min(box[1], 1.0f - rv);
            box[3] = std::max(box[3], 1.0f - rv);
         }
         d[k] = sx / (float)l->pw - ru;
         d[k + 1] = sy / (float)l->ph - rv;
      }
   // A selection limits the warp, of the pixels and of the mask alike (PhotoCraft #4175): laid onto the surface once
   // per selection and layer box, then read by the pass.
   bool useSel = false;
   if (mSelAny && mSel.size() == (size_t)mDoc.w * mDoc.h)
   {
      float o[2], ux[2], uy[2], sc = 1;
      if (DocToSurface(*l, 0, 0, 0, o[0], o[1], sc) && DocToSurface(*l, 0, 1, 0, ux[0], ux[1], sc) &&
          DocToSurface(*l, 0, 0, 1, uy[0], uy[1], sc))
      {
         const float a = ux[0] - o[0], c = ux[1] - o[1], b = uy[0] - o[0], dd = uy[1] - o[1], det = a * dd - b * c;
         if (std::fabs(det) >= 1e-9f)
         {
            UploadSelection();   // bumps mSelTexGen when the selection changed
            const unsigned long long k2 = mSelTexGen * 1000003ull + (unsigned long long)id * 131ull + (unsigned long long)l->pw * 7ull + l->ph;
            if (k2 != mWarpSelKey || mWarpSel.w != l->pw || mWarpSel.h != l->ph || mWarpSel.tex == 0)
            {
               const float inv[6] = { dd / det, -b / det, -(dd * o[0] - b * o[1]) / det, -c / det, a / det, -(-c * o[0] + a * o[1]) / det };
               std::vector<uint8_t> m, rgba;
               SelOnSurface(mSel, mDoc.w, mDoc.h, l->pw, l->ph, inv, m);
               rgba.assign(m.size() * 4, 255);
               for (size_t i = 0; i < m.size(); i++)
                  rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = m[i];
               if (GLUtil::EnsureFbo(mWarpSel, l->pw, l->ph, GL_RGBA8, "Scene2DWarpSel"))
                  WriteRect(mWarpSel, 0, 0, l->pw, l->ph, rgba.data());
               mWarpSelKey = k2;
            }
            useSel = mWarpSel.tex != 0;
         }
      }
   }
   const unsigned prog = mWarpProgram, tex = src->fbo.tex, selTex = useSel ? mWarpSel.tex : 0;
   auto bindSel = [&](bool isMask) {
      glUniform1i(GLUtil::UniformLoc(prog, "uIsMask"), isMask ? 1 : 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, selTex);
      glUniform1i(GLUtil::UniformLoc(prog, "uSel"), 1);
      glUniform1i(GLUtil::UniformLoc(prog, "uUseSel"), useSel ? 1 : 0);
      glActiveTexture(GL_TEXTURE0);
   };
   GLUtil::DrawPass(it->second.fbo, prog, [&]() {
      bindSel(false);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      const GLint filt = nearest ? GL_NEAREST : GL_LINEAR;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
      glUniform1i(GLUtil::UniformLoc(prog, "uSrc"), 0);
      glUniform2fv(GLUtil::UniformLoc(prog, "uD"), nx * ny, d.data());
      glUniform1fv(GLUtil::UniformLoc(prog, "uUs"), nx, moved.us.data());
      glUniform1fv(GLUtil::UniformLoc(prog, "uVs"), ny, moved.vs.data());
      glUniform1i(GLUtil::UniformLoc(prog, "uNx"), nx);
      glUniform1i(GLUtil::UniformLoc(prog, "uNy"), ny);
      glUniform4fv(GLUtil::UniformLoc(prog, "uR"), 1, box);
   });
   if (nearest)
   {
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   // A pixel mask on the same box moves with its pixels (a mask is linked to its layer), through the same grid.
   Surface* maskSurf = nullptr;
   if (maskFrom != nullptr && l->HasPixelMask() && l->mw == l->pw && l->mh == l->ph && maskFrom->size() == l->mtiles.size())
   {
      Surface* msrc = SyncSurface(mWarpMaskFrom, id, l->mw, l->mh, *maskFrom);
      auto mt = mMasks.find(id);
      if (msrc != nullptr && mt != mMasks.end() && mt->second.fbo.fbo != 0)
      {
         maskSurf = &mt->second;
         const unsigned mtex = msrc->fbo.tex;
         GLUtil::DrawPass(mt->second.fbo, prog, [&]() {
            bindSel(true);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mtex);
            glUniform1i(GLUtil::UniformLoc(prog, "uSrc"), 0);
            glUniform2fv(GLUtil::UniformLoc(prog, "uD"), nx * ny, d.data());
            glUniform1fv(GLUtil::UniformLoc(prog, "uUs"), nx, moved.us.data());
            glUniform1fv(GLUtil::UniformLoc(prog, "uVs"), ny, moved.vs.data());
            glUniform1i(GLUtil::UniformLoc(prog, "uNx"), nx);
            glUniform1i(GLUtil::UniformLoc(prog, "uNy"), ny);
            glUniform4fv(GLUtil::UniformLoc(prog, "uR"), 1, box);
         });
      }
   }
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, 0);
   glActiveTexture(GL_TEXTURE0);
   // Shown now; the tiles follow when the drag ends.
   mPaintEdits++;
   if (store)
   {
      const int all[4] = { 0, 0, l->pw, l->ph };
      StoreTiles(it->second, l->tiles, all);
      if (maskSurf != nullptr)
      {
         StoreTiles(*maskSurf, l->mtiles, all);
         auto mf = mWarpMaskFrom.find(id);
         if (mf != mWarpMaskFrom.end())
         {
            GLUtil::DestroyFbo(mf->second.fbo);
            mWarpMaskFrom.erase(mf);
         }
      }
      auto u = mUndoSurf.find(-id);
      if (u != mUndoSurf.end())
      {
         GLUtil::DestroyFbo(u->second.fbo);
         mUndoSurf.erase(u);
      }
      Commit();
   }
   return true;
}

bool Scene2DNode::CanTransferEffects(int from, int to, int effect, bool copy)
{
   Layers();
   const Layer* a = FindConst(from);
   const Layer* b = FindConst(to);
   if (a == nullptr || b == nullptr || a == b || (b->lock & kLockAll) != 0 || (!copy && (a->lock & kLockAll) != 0))
      return false;
   const int extra = effect - kFxCount;
   if (effect == -1)
      return a->AnyFx();
   if (effect >= 0 && effect < kFxCount)
      return a->fx[effect].on;
   return extra >= 0 && extra < (int)a->fxMore.size() && a->fxMore[(size_t)extra].second.on;
}

bool Scene2DNode::TransferEffects(int from, int to, int effect, bool copy)
{
   if (!CanTransferEffects(from, to, effect, copy))
   {
      std::fprintf(stderr, "PROBE transfer effects %d->%d: effect %d refused\n", from, to, effect);
      return false;
   }
   Layer* a = FindLayer(from);
   Layer* b = FindLayer(to);
   const int extra = effect - kFxCount;
   const bool whole = effect == -1;
   const bool kind = effect >= 0 && effect < kFxCount;
   if (whole)
   {
      for (int k = 0; k < kFxCount; k++)
         b->fx[k] = a->fx[k];
      b->fxMore = a->fxMore;
      if (!copy)
      {
         for (int k = 0; k < kFxCount; k++)
            a->fx[k] = FxDefault(k);
         a->fxMore.clear();
      }
   }
   else
   {
      const int type = kind ? effect : a->fxMore[(size_t)extra].first;
      const Fx f = kind ? a->fx[effect] : a->fxMore[(size_t)extra].second;
      if (b->fx[type].on)
         b->fxMore.push_back({ type, f });
      else
         b->fx[type] = f;
      if (!copy)
      {
         if (kind)
            a->fx[effect] = FxDefault(effect);
         else
            a->fxMore.erase(a->fxMore.begin() + extra);
      }
   }
   std::fprintf(stderr, "PROBE transfer effects %d->%d: effect %d %s\n", from, to, effect, copy ? "copied" : "moved");
   Commit();
   return true;
}

bool Scene2DNode::CopyLayerStyle(int from, int to, int parts)
{
   Layers();
   const Layer* a = FindConst(from);
   Layer* b = FindLayer(to);
   if (a == nullptr || b == nullptr || a == b || (b->lock & kLockAll) != 0)
      return false;
   if (parts & kStOpacity)
   {
      b->blend = a->blend;
      b->opacity = a->opacity;
      b->fill = a->fill;
   }
   if (parts & kStEffects)
   {
      for (int k = 0; k < kFxCount; k++)
         b->fx[k] = a->fx[k];
      b->fxMore = a->fxMore;
   }
   // Vector shapes: the first shape's fill, stroke and object settings go to every shape.
   if (!a->shapes.empty() && (parts & (kStStroke | kStFill | kStObject)))
   {
      const Shape& f = a->shapes.front();
      for (Shape& t : b->shapes)
      {
         if (parts & kStFill)
         {
            t.fillOn = f.fillOn;
            std::copy(f.fillC, f.fillC + 4, t.fillC);
            t.fillType = f.fillType;
            t.fillAngle = f.fillAngle;
            t.fillStops = f.fillStops;
            t.fillImage = f.fillImage;
            t.imageMode = f.imageMode;
            std::copy(&f.meshC[0][0], &f.meshC[0][0] + 16, &t.meshC[0][0]);
            t.noise = f.noise;
         }
         if (parts & kStStroke)
         {
            t.strokeOn = f.strokeOn;
            std::copy(f.strokeC, f.strokeC + 4, t.strokeC);
            t.strokeType = f.strokeType;
            t.strokeAngle = f.strokeAngle;
            t.strokeStops = f.strokeStops;
            t.width = f.width;
            t.cap = f.cap;
            t.join = f.join;
            t.align = f.align;
            t.dash = f.dash;
            t.gap = f.gap;
            t.miter = f.miter;
            t.dashPhase = f.dashPhase;
            t.balanced = f.balanced;
            t.arrowS = f.arrowS;
            t.arrowE = f.arrowE;
            t.arrowSize = f.arrowSize;
            t.arrowConcave = f.arrowConcave;
            t.taperS = f.taperS;
            t.taperE = f.taperE;
            t.pressMin = f.pressMin;
            t.behind = f.behind;
            t.scaleStroke = f.scaleStroke;
         }
         if ((parts & kStObject) && t.type == f.type)
         {
            t.radius = f.radius;
            t.sides = f.sides;
            t.inner = f.inner;
            std::copy(f.cornerR, f.cornerR + 4, t.cornerR);
            std::copy(f.cornerType, f.cornerType + 4, t.cornerType);
         }
      }
   }
   if (a->kind == Kind::Text && b->kind == Kind::Text)
   {
      if (parts & kStCharacter)
      {
         b->font = a->font;
         b->fontSize = a->fontSize;
         b->tracking = a->tracking;
         b->bold = a->bold;
         b->italic = a->italic;
         b->underline = a->underline;
         b->caps = a->caps;
         std::copy(a->textC, a->textC + 4, b->textC);
         b->strike = a->strike;
         b->highlight = a->highlight;
         b->kerning = a->kerning;
         std::copy(a->highlightC, a->highlightC + 4, b->highlightC);
         b->script = a->script;
         b->baseShift = a->baseShift;
         b->textSX = a->textSX;
         b->textSY = a->textSY;
         b->ulOwn = a->ulOwn;
         b->stOwn = a->stOwn;
         std::copy(a->ulC, a->ulC + 4, b->ulC);
         std::copy(a->stC, a->stC + 4, b->stC);
         b->noBreak = a->noBreak;
         b->optical = a->optical;
         b->outline = a->outline;
         b->outlineW = a->outlineW;
         b->typeAA = a->typeAA;
         std::copy(a->outlineC, a->outlineC + 4, b->outlineC);
         b->offsetX = a->offsetX;
         b->lang = a->lang;
      }
      if (parts & kStParagraph)
      {
         b->align = a->align;
         b->leading = a->leading;
         b->indentL = a->indentL;
         b->indentR = a->indentR;
         b->indentFirst = a->indentFirst;
         b->spaceBefore = a->spaceBefore;
         b->spaceAfter = a->spaceAfter;
      }
   }
   Commit();
   return true;
}

// ---- Layer > Matting, Image > Auto / Equalize ----------------------------------------------------------------------

namespace
{
   // The 0.1% clip points of a 256-bin histogram (Photoshop's Auto default): lo, hi as 0..255 values.
   void ClipRange(const double hist[256], double total, int& lo, int& hi)
   {
      const double cut = total * 0.001;
      double acc = 0.0;
      for (lo = 0; lo < 255 && acc + hist[lo] <= cut; lo++)
         acc += hist[lo];
      acc = 0.0;
      for (hi = 255; hi > 0 && acc + hist[hi] <= cut; hi--)
         acc += hist[hi];
      if (hi <= lo)
      {
         lo = 0;
         hi = 255;
      }
   }
}

bool Scene2DNode::Matting(int layerId, int mode, int width, std::string* err)
{
   Layers();
   auto fail = [&](const char* why) {
      if (err != nullptr)
         *err = std::string("matting: ") + why;
      std::fprintf(stderr, "PROBE matting refused: %s\n", why);
      return false;
   };
   Layer* l = FindLayer(layerId);
   if (l == nullptr || l->kind != Kind::Paint)
      return fail("not a paint layer");
   if ((EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return fail("the layer's pixels are locked");
   if (mode < 0 || mode > 3)
      return fail("bad mode");
   SyncSurfaces();
   auto it = mSurfaces.find(layerId);
   if (it == mSurfaces.end() || it->second.fbo.fbo == 0)
      return fail("no pixels");
   const int W = l->pw, H = l->ph;
   std::vector<uint8_t> px;
   ReadRect(it->second.fbo, 0, 0, W, H, px);
   if (px.size() != (size_t)W * H * 4)
      return fail("no pixels");
   const std::vector<uint8_t> was = px;
   if (mode == 0)
   {
      // Defringe: each soft edge pixel takes the nearest solid pixel's colour, nearest by distance within width.
      const int r = std::clamp(width, 1, 64);
      for (int y = 0; y < H; y++)
         for (int x = 0; x < W; x++)
         {
            uint8_t* q = &px[((size_t)y * W + x) * 4];
            if (q[3] == 0 || q[3] == 255)
               continue;
            int best = INT_MAX;
            const uint8_t* src = nullptr;
            for (int dy = -r; dy <= r; dy++)
               for (int dx = -r; dx <= r; dx++)
               {
                  const int sx = x + dx, sy = y + dy, d2 = dx * dx + dy * dy;
                  if (sx < 0 || sy < 0 || sx >= W || sy >= H || d2 >= best || d2 > r * r)
                     continue;
                  const uint8_t* s = &was[((size_t)sy * W + sx) * 4];
                  if (s[3] == 255)
                  {
                     best = d2;
                     src = s;
                  }
               }
            if (src != nullptr)
               for (int j = 0; j < 3; j++)
                  q[j] = src[j];
         }
   }
   else if (mode == 1 || mode == 2)
   {
      // The colour was mixed with black (or white) by its alpha: c = a * f + (1 - a) * m, so f = (c - (1 - a) m) / a.
      const float m = mode == 2 ? 1.0f : 0.0f;
      for (size_t i = 0; i < (size_t)W * H; i++)
      {
         uint8_t* q = &px[i * 4];
         if (q[3] == 0 || q[3] == 255)
            continue;
         const float a = q[3] / 255.0f;
         for (int j = 0; j < 3; j++)
            q[j] = (uint8_t)std::lround(std::clamp((q[j] / 255.0f - (1.0f - a) * m) / a, 0.0f, 1.0f) * 255.0f);
      }
   }
   else
   {
      // Colour Decontaminate: the layer's own alpha is the mask (rows as read; the maths is the same either way up).
      std::vector<uint8_t> mask((size_t)W * H);
      for (size_t i = 0; i < mask.size(); i++)
         mask[i] = px[i * 4 + 3];
      SelectMaths::Decontaminate(px.data(), mask, W, H, std::clamp(width / 100.0f, 0.0f, 1.0f));
      for (size_t i = 0; i < mask.size(); i++)
         px[i * 4 + 3] = mask[i];   // decontamination moves colour, never coverage
   }
   size_t n = 0;
   for (size_t i = 0; i < px.size(); i += 4)
      n += std::memcmp(&px[i], &was[i], 4) != 0 ? 1 : 0;
   std::fprintf(stderr, "PROBE matting layer %d mode %d: %zu px changed\n", layerId, mode, n);
   if (n == 0)
      return fail("nothing to change");
   WriteRect(it->second.fbo, 0, 0, W, H, px.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

bool Scene2DNode::AutoTone(int layerId, int mode, std::string* err)
{
   Layers();
   auto fail = [&](const char* why) {
      if (err != nullptr)
         *err = std::string("autoTone: ") + why;
      std::fprintf(stderr, "PROBE auto tone refused: %s\n", why);
      return false;
   };
   Layer* l = FindLayer(layerId);
   if (l == nullptr || l->kind != Kind::Paint)
      return fail("not a paint layer");
   if ((EffectiveLock(*l) & (kLockPixels | kLockAll)) != 0)
      return fail("the layer's pixels are locked");
   if (mode < 0 || mode > 3)
      return fail("bad mode");
   SyncSurfaces();
   auto it = mSurfaces.find(layerId);
   if (it == mSurfaces.end() || it->second.fbo.fbo == 0)
      return fail("no pixels");
   const int W = l->pw, H = l->ph;
   std::vector<uint8_t> px;
   ReadRect(it->second.fbo, 0, 0, W, H, px);
   if (px.size() != (size_t)W * H * 4)
      return fail("no pixels");
   const bool plain = l->sx == 1.0f && l->sy == 1.0f && l->angle == 0.0f && l->shear == 0.0f && !l->flipX && !l->flipY && !l->is3d &&
                      !l->warpOn && W == mDoc.w && H == mDoc.h && std::fabs(l->x - l->ax * W) < 1e-3f && std::fabs(l->y - l->ay * H) < 1e-3f;
   const bool limit = mSelAny && mSel.size() == (size_t)mDoc.w * mDoc.h;
   if (limit && !plain)
      return fail("a selection needs the layer to fill the document, unmoved");
   // Histograms of the pixels the command works on (the selection's, when there is one).
   double hist[3][256] = {}, total = 0.0;
   for (size_t i = 0; i < (size_t)W * H; i++)
   {
      const uint8_t* q = &px[i * 4];
      const double k = limit ? mSel[i] / 255.0 : 1.0;
      if (q[3] == 0 || k <= 0.0)
         continue;
      for (int j = 0; j < 3; j++)
         hist[j][q[j]] += k;
      total += k;
   }
   if (total <= 0.0)
      return fail("the layer is empty");
   uint8_t lut[3][256];
   if (mode == 0)
   {
      // Equalize: one cumulative table from all three channels, so the brightness steps are used evenly.
      double all[256], acc = 0.0;
      for (int v = 0; v < 256; v++)
         all[v] = hist[0][v] + hist[1][v] + hist[2][v];
      const double sum = total * 3.0;
      double first = 0.0;
      for (int v = 0; v < 256 && first == 0.0; v++)
         first = all[v];
      for (int v = 0; v < 256; v++)
      {
         acc += all[v];
         const double t = sum - first > 0.0 ? (acc - first) / (sum - first) : 0.0;
         lut[0][v] = lut[1][v] = lut[2][v] = (uint8_t)std::lround(std::clamp(t, 0.0, 1.0) * 255.0);
      }
   }
   else
   {
      int lo[3], hi[3];
      for (int j = 0; j < 3; j++)
         ClipRange(hist[j], total, lo[j], hi[j]);
      if (mode == 2)
      {
         // Auto Contrast: one range for all channels, so a colour cast stays.
         double all[256];
         for (int v = 0; v < 256; v++)
            all[v] = hist[0][v] + hist[1][v] + hist[2][v];
         int a = 0, b = 255;
         ClipRange(all, total * 3.0, a, b);
         for (int j = 0; j < 3; j++)
         {
            lo[j] = a;
            hi[j] = b;
         }
      }
      for (int j = 0; j < 3; j++)
         for (int v = 0; v < 256; v++)
            lut[j][v] = (uint8_t)std::lround(std::clamp((v - lo[j]) / (double)std::max(1, hi[j] - lo[j]), 0.0, 1.0) * 255.0);
      if (mode == 3)
      {
         // Auto Color: after the stretch, each channel's midtones (pixels near mid grey) meet at neutral grey, by a gamma.
         double mean[3] = {}, wsum = 0.0;
         for (size_t i = 0; i < (size_t)W * H; i++)
         {
            const uint8_t* q = &px[i * 4];
            const double k = limit ? mSel[i] / 255.0 : 1.0;
            if (q[3] == 0 || k <= 0.0)
               continue;
            const double luma = (lut[0][q[0]] + lut[1][q[1]] + lut[2][q[2]]) / (3.0 * 255.0);
            if (luma < 0.25 || luma > 0.75)
               continue;
            for (int j = 0; j < 3; j++)
               mean[j] += lut[j][q[j]] / 255.0 * k;
            wsum += k;
         }
         if (wsum > 0.0)
         {
            const double grey = (mean[0] + mean[1] + mean[2]) / (3.0 * wsum);
            for (int j = 0; j < 3; j++)
            {
               const double m = std::clamp(mean[j] / wsum, 0.02, 0.98);
               const double g = std::clamp(std::log(std::clamp(grey, 0.02, 0.98)) / std::log(m), 0.25, 4.0);
               for (int v = 0; v < 256; v++)
                  lut[j][v] = (uint8_t)std::lround(std::pow(lut[j][v] / 255.0, g) * 255.0);
            }
         }
      }
   }
   size_t n = 0;
   for (size_t i = 0; i < (size_t)W * H; i++)
   {
      uint8_t* q = &px[i * 4];
      const float k = limit ? mSel[i] / 255.0f : 1.0f;
      if (q[3] == 0 || k <= 0.0f)
         continue;
      bool any = false;
      for (int j = 0; j < 3; j++)
      {
         const uint8_t b = (uint8_t)std::lround(q[j] + (lut[j][q[j]] - q[j]) * k);
         any = any || b != q[j];
         q[j] = b;
      }
      n += any ? 1 : 0;
   }
   std::fprintf(stderr, "PROBE auto tone layer %d mode %d: %zu px changed\n", layerId, mode, n);
   WriteRect(it->second.fbo, 0, 0, W, H, px.data());
   const int all[4] = { 0, 0, W, H };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

std::vector<int> Scene2DNode::FxToLayers(int id, int frameId)
{
   Layers();
   std::vector<int> out;
   const Layer* l0 = FindConst(id);
   if (l0 == nullptr || !l0->AnyFx())
      return out;
   const Layer keep = *l0;
   struct One { bool more; int idx; int type; };
   std::vector<One> list;
   for (int k = 0; k < kFxCount; k++)
      if (keep.fx[k].on)
         list.push_back({ false, k, k });
   for (int j = 0; j < (int)keep.fxMore.size(); j++)
      if (keep.fxMore[(size_t)j].second.on)
         list.push_back({ true, j, keep.fxMore[(size_t)j].first });
   for (const One& one : list)
   {
      Layer* l = FindLayer(id);
      if (l == nullptr)
         break;
      // The layer with this one fx and no pixels of its own (Fill 0 keeps fx), drawn alone.
      *l = keep;
      for (Fx& f : l->fx)
         f.on = false;
      for (auto& m : l->fxMore)
         m.second.on = false;
      (one.more ? l->fxMore[(size_t)one.idx].second : l->fx[one.idx]).on = true;
      l->fill = 0.0f;
      l->effects.clear();
      Commit();
      std::vector<uint8_t> rgba;
      if (!RenderLayerAlone(id, frameId, rgba) || rgba.size() != (size_t)mDoc.w * mDoc.h * 4)
         continue;
      const std::string name = keep.name + "'s " + FxName(one.type);
      const int nid = AddFileLayer(0, name, 0, 0, mDoc.w, mDoc.h, rgba, id);   // x, y: the top left
      if (nid == 0)
         continue;
      // What is drawn under the pixels (drop shadow, outer glow) goes under the layer.
      if (one.type == kFxShadow || one.type == kFxGlow)
         MoveLayer(nid, id, true);
      if (Layer* nl = FindLayer(nid))
      {
         nl->clip = keep.clip;
         nl->opacity = keep.opacity;
      }
      out.push_back(nid);
   }
   if (Layer* l = FindLayer(id))
   {
      *l = keep;
      for (Fx& f : l->fx)
         f.on = false;
      for (auto& m : l->fxMore)
         m.second.on = false;
   }
   Commit();
   std::fprintf(stderr, "PROBE fx to layers %d: %zu layers\n", id, out.size());
   return out;
}

int Scene2DNode::RasterizeFillLayer(int id, int frameId)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Fill)
      return 0;
   const Layer keep = *l;
   l->opacity = 1.0f;
   l->blend = 0;
   l->clip = false;
   l->visible = true;
   Commit();
   std::vector<uint8_t> rgba;
   const bool drawn = RenderLayerAlone(id, frameId, rgba) && rgba.size() == (size_t)mDoc.w * mDoc.h * 4;
   if (Layer* back = FindLayer(id))
      *back = keep;
   if (!drawn)
   {
      Commit();
      return 0;
   }
   const int nid = AddFileLayer(keep.parent, keep.name.empty() ? std::string("Fill") : keep.name, 0, 0, mDoc.w, mDoc.h, rgba, id);
   if (Layer* nl = FindLayer(nid))
   {
      nl->opacity = keep.opacity;
      nl->blend = keep.blend;
      nl->clip = keep.clip;
      nl->visible = keep.visible;
      nl->label = keep.label;
   }
   if (nid != 0)
      RemoveLayer(id);
   std::fprintf(stderr, "PROBE rasterize fill %d -> %d\n", id, nid);
   Commit();
   return nid;
}
