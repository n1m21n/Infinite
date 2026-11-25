// Scene 2D paint: paint-layer surfaces and their tiles, pixel masks, the
// document selection, bucket, gradient and pick. Plan: internal/plans/2d-view (Block B).
#include "Scene2DNode.h"
#include "GLUtil.h"

#include "gl3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

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

int Scene2DNode::Remove(int id, const std::vector<RemoveStroke>& strokes, bool sampleAll, bool newLayer)
{
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
   const bool mask = &in == &mMasks;
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
   std::vector<int> paintIds, maskIds;
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
   }
   for (auto* m : { &mSurfaces, &mMasks })
   {
      const std::vector<int>& keep = m == &mSurfaces ? paintIds : maskIds;
      for (auto it = m->begin(); it != m->end();)
      {
         if (std::find(keep.begin(), keep.end(), it->first) == keep.end() && !(mStroke.Active() && it->first == mPaintId))
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
   if (mStroke.Active())
      EndPaint();
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
      SyncSurfaces();
      auto& m = target == 0 ? mSurfaces : mMasks;
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
   if (target != 2 && mSelAny && EnsurePaintShaders())
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
   if (target == 0 && b.sampleAll && (brush.mode == BrushEngine::kClone || brush.mode == BrushEngine::kHeal) && mOut.tex != 0 &&
       EnsurePaintShaders() && GLUtil::EnsureFbo(mCloneSrc, surf->w, surf->h, GL_RGBA8, "Scene2DCloneSrc"))
   {
      // All layers: the picture as it stands, laid onto this surface's pixels, is what the clone copies from.
      Surface tmp;
      tmp.fbo = mCloneSrc;
      ApplyFill(tmp, surf->w, surf->h, *l, 0, nullptr, nullptr, -2, nullptr, nullptr, 1.0f);
      brush.sourceTex = mCloneSrc.tex;
   }
   if (target == 0 && brush.mode == BrushEngine::kUndo)
   {
      auto u = mUndoTiles.find(id);
      if (u == mUndoTiles.end() || u->second.size() != l->tiles.size())
         mUndoTiles[id] = l->tiles;
      if (Surface* us = SyncSurface(mUndoSurf, id, l->pw, l->ph, mUndoTiles[id]))
         brush.sourceTex = us->fbo.tex;
   }
   ApplyBrushSources(brush, true);
   mStroke.SetMirrors(SurfaceMirrors(target == 2 ? Layer() : *l, target));
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
   if (!mStroke.Active())
      return;
   const Layer* l = FindConst(mPaintId);
   if (mPaintTarget != 2 && l == nullptr)
      return;
   float sx = 0, sy = 0, scale = 1;
   if (DocToSurface(mPaintTarget == 2 ? Layer() : *l, mPaintTarget, x, y, sx, sy, scale))
      mStroke.MoveTo(sx, sy, pressure);
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

void Scene2DNode::EndPaint()
{
   mSkinStroke.on = false;
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
   auto& m = mPaintTarget == 0 ? mSurfaces : mMasks;
   auto it = m.find(mPaintId);
   if (l == nullptr || it == m.end())
   {
      mStroke.Cancel();
      mPaintId = 0;
      return;
   }
   const bool inpaint = mStroke.CurrentBrush().mode == BrushEngine::kInpaint && mPaintTarget == 0;
   mStroke.End(it->second.fbo, dirty);
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
      StoreTiles(it->second, mPaintTarget == 0 ? l->tiles : l->mtiles, dirty);
      Commit();
   }
   mPaintId = 0;
}

bool Scene2DNode::Bucket(int id, float x, float y, const float rgb[3], float opacity, int tolerance, bool contiguous, bool sampleAll)
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
   std::vector<uint8_t> region;
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

bool Scene2DNode::Gradient(int id, float ax, float ay, float bx, float by, const float a[4], const float b[4], int kind, float opacity,
                           int extend, bool dither)
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
   ApplyFill(it->second, l->pw, l->ph, *l, 0, a, b, std::clamp(kind, 1, 5), p0, p1, opacity);
   mGradExtend = 0;
   mGradDither = false;
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
   Commit();
}

// Called by Parse: the selection the text names (none when it names none, or the document changed size).
void Scene2DNode::RestoreSelection()
{
   std::vector<uint8_t> sel;
   if (mSelStore.w == mDoc.w && mSelStore.h == mDoc.h && SelDecode(mSelStore.keys, mDoc.w, mDoc.h, sel))
   {
      mSel.swap(sel);
      mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   }
   else
   {
      mSelStore = SelStore();
      if (!mSelAny)
         return;
      mSel.clear();
      mSelAny = false;
   }
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
   if (mSel.size() != t.size())
      mSel.assign(t.size(), 0);
   if (op == kSelNew || !mSelAny)
      mSel = op == kSelSub || op == kSelIntersect ? std::vector<uint8_t>(t.size(), 0) : t;
   else
      for (size_t i = 0; i < t.size(); i++)
      {
         if (op == kSelAdd) mSel[i] = std::max(mSel[i], t[i]);
         else if (op == kSelSub) mSel[i] = (uint8_t)(mSel[i] * (255 - t[i]) / 255);
         else mSel[i] = std::min(mSel[i], t[i]);
      }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
   StoreSelection();
   return true;
}

void Scene2DNode::DeleteSavedSelection(const std::string& name)
{
   Layers();
   mSelSaved.erase(std::remove_if(mSelSaved.begin(), mSelSaved.end(), [&](const SelStore& o) { return o.name == name; }), mSelSaved.end());
   Commit();
}

bool Scene2DNode::Pick(float x, float y, float rgb[3], int size, int layer)
{
   // Where to read: the picture (y flipped to texture rows) or one paint layer's own pixels.
   const GLUtil::Fbo* src = &mOut;
   int px = (int)std::floor(x), py = mDoc.h - 1 - (int)std::floor(y);
   if (layer != 0)
   {
      Layers();
      const Layer* l = FindConst(layer);
      auto it = mSurfaces.find(layer);
      float sx = 0, sy = 0, scale = 1;
      if (l != nullptr && l->kind == Kind::Paint && it != mSurfaces.end() && DocToSurface(*l, 0, x, y, sx, sy, scale))
      {
         src = &it->second.fbo;
         px = (int)std::floor(sx);
         py = (int)std::floor(sy);
      }
   }
   if (src->fbo == 0 || px < 0 || py < 0 || px >= src->w || py >= src->h)
      return false;
   const int half = std::clamp(size, 1, 5) / 2;
   const int x0 = std::max(0, px - half), y0 = std::max(0, py - half);
   const int x1 = std::min(src->w, px + half + 1), y1 = std::min(src->h, py + half + 1);
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

void Scene2DNode::SelectPolygon(const std::vector<float>& xy, int op, float feather, bool aa)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> t;
   FillPolygon(xy, W, H, t, aa);
   if (feather > 0.5f)
      Feather(t, W, H, feather);
   if (mSel.size() != t.size())
      mSel.assign(t.size(), 0);
   if (op == kSelNew || !mSelAny)
      mSel = op == kSelSub || op == kSelIntersect ? std::vector<uint8_t>(t.size(), 0) : t;
   else
      for (size_t i = 0; i < t.size(); i++)
      {
         if (op == kSelAdd) mSel[i] = std::max(mSel[i], t[i]);
         else if (op == kSelSub) mSel[i] = (uint8_t)(mSel[i] * (255 - t[i]) / 255);
         else mSel[i] = std::min(mSel[i], t[i]);
      }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
   StoreSelection();
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
   if (mSel.size() != t.size())
      mSel.assign(t.size(), 0);
   if (op == kSelNew || !mSelAny)
      mSel = op == kSelSub || op == kSelIntersect ? std::vector<uint8_t>(t.size(), 0) : t;
   else
      for (size_t i = 0; i < t.size(); i++)
      {
         if (op == kSelAdd) mSel[i] = std::max(mSel[i], t[i]);
         else if (op == kSelSub) mSel[i] = (uint8_t)(mSel[i] * (255 - t[i]) / 255);
         else mSel[i] = std::min(mSel[i], t[i]);
      }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
   StoreSelection();
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
   StoreSelection();
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
   mSel.clear();
   mSelAny = false;
   mSelDirty = true;
   StoreSelection();
}

void Scene2DNode::ReframeSelection(int w, int h, int dx, int dy)
{
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

void Scene2DNode::ShiftSelection(float dx, float dy)
{
   Layers();
   ReframeSelection(mDoc.w, mDoc.h, (int)std::lround(dx), (int)std::lround(dy));
   StoreSelection();
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
      "uniform vec2 uD[16];\n"   // each grid point's move, in uv, row by row from the top
      "uniform vec4 uR;\n"       // the grid's box in uv, y from the top (x0, y0, x1, y1)
      "vec2 disp(vec2 p) {\n"
      "   vec2 g = clamp((vec2(p.x, 1.0 - p.y) - uR.xy) / max(uR.zw - uR.xy, vec2(1e-6)), 0.0, 1.0) * 3.0;\n"
      "   ivec2 i = ivec2(min(floor(g), vec2(2.0)));\n"
      "   vec2 f = g - vec2(i);\n"
      "   int k = i.y * 4 + i.x;\n"
      "   return mix(mix(uD[k], uD[k + 1], f.x), mix(uD[k + 4], uD[k + 5], f.x), f.y);\n"
      "}\n"
      "void main() {\n"
      "   vec2 p = vUv;\n"
      "   for (int it = 0; it < 12; it++) p = vUv - disp(p);\n"   // where this pixel came from
      "   if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0) { fragColor = vec4(0.0); return; }\n"
      "   fragColor = texture(uSrc, p);\n"
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
   SelectWand(x, y, tolerance, contiguous, kSelNew, sampleAll, id);
   const bool ok = mSelAny && ClearPixels(id);
   mSel = keep;
   mSelAny = keepAny;
   mSelStore = keepStore;
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
   StoreSelection();
}

// ---- live selection dialogs (Select and Mask, Color Range) ----------------------------------------------------
bool Scene2DNode::ReadSelectPicture(bool sampleAll, int layerId, std::vector<uint8_t>& px)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   px.clear();
   const Layer* l = FindConst(layerId);
   auto it = l != nullptr && l->kind == Kind::Paint ? mSurfaces.find(layerId) : mSurfaces.end();
   if (!sampleAll && it != mSurfaces.end() && l->pw == W && l->ph == H)
      ReadRect(it->second.fbo, 0, 0, W, H, px);
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
   return true;
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
   mSelDirty = true;
   StoreSelection();
}

void Scene2DNode::RefinePreview(const SelectMaths::RefineParams& p)
{
   const int W = mDoc.w, H = mDoc.h;
   if (mLivePic.size() != (size_t)W * H * 4 || !mLiveSelAny)
      return;
   mSel = mLiveSel;
   SelectMaths::Refine(mLivePic.data(), mSel, W, H, p);
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

void Scene2DNode::QuickSelectEnd()
{
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

bool Scene2DNode::AiSelectPoll()
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

bool Scene2DNode::MeshRest(int id, float out[32])
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
   for (int j = 0; j < 4; j++)
      for (int i = 0; i < 4; i++)
      {
         const float u = i / 3.0f, v = j / 3.0f;
         // TL TR BR BL
         out[(j * 4 + i) * 2] = (1 - v) * ((1 - u) * c[0] + u * c[2]) + v * ((1 - u) * c[6] + u * c[4]);
         out[(j * 4 + i) * 2 + 1] = (1 - v) * ((1 - u) * c[1] + u * c[3]) + v * ((1 - u) * c[7] + u * c[5]);
      }
   return true;
}

bool Scene2DNode::MeshWarp(int id, const std::vector<std::string>& from, const float moved[32], bool store, bool nearest,
                           const float* rest)
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
   // Each point's move in surface uv (y up), from where it rests: the grid fitted to the content when `rest` is
   // given, else the whole surface.
   float d[32], box[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
   if (rest != nullptr)
   {
      box[0] = box[1] = 1e30f;
      box[2] = box[3] = -1e30f;
   }
   for (int j = 0; j < 4; j++)
      for (int i = 0; i < 4; i++)
      {
         const int k = (j * 4 + i) * 2;
         float sx = 0, sy = 0, s = 1;
         DocToSurface(*l, 0, moved[k], moved[k + 1], sx, sy, s);
         float ru = i / 3.0f, rv = 1.0f - j / 3.0f;
         if (rest != nullptr)
         {
            float rx = 0, ry = 0, rs = 1;
            DocToSurface(*l, 0, rest[k], rest[k + 1], rx, ry, rs);
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
   const unsigned prog = mWarpProgram, tex = src->fbo.tex;
   GLUtil::DrawPass(it->second.fbo, prog, [&]() {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      const GLint filt = nearest ? GL_NEAREST : GL_LINEAR;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
      glUniform1i(GLUtil::UniformLoc(prog, "uSrc"), 0);
      glUniform2fv(GLUtil::UniformLoc(prog, "uD"), 16, d);
      glUniform4fv(GLUtil::UniformLoc(prog, "uR"), 1, box);
   });
   if (nearest)
   {
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   // Shown now; the tiles follow when the drag ends.
   mPaintEdits++;
   if (store)
   {
      const int all[4] = { 0, 0, l->pw, l->ph };
      StoreTiles(it->second, l->tiles, all);
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
