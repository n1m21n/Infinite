// Art History Brush dabs. Ported from PhotoCraft's crates/algo/src/art_history.rs (Apache License 2.0, Copyright the
// PhotoCraft authors); the numbers below are its, kept as they are so strokes match it.
#include "ArtHistory.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace ArtHistory
{
namespace
{
   struct StyleRow
   {
      const char* id;
      const char* name;
      float spacing, scatter, curlDeg, size;
   };
   // Per style: spacing, scatter and size multipliers, and the curl per dab.
   constexpr StyleRow kStyles[kStyleCount] = {
      { "tightShort", "Tight Short", 0.35f, 0.00f, 0.0f, 0.7f },
      { "tightMedium", "Tight Medium", 0.55f, 0.00f, 0.0f, 0.9f },
      { "tightLong", "Tight Long", 0.85f, 0.05f, 0.0f, 1.1f },
      { "looseMedium", "Loose Medium", 0.70f, 0.45f, 0.0f, 1.0f },
      { "looseLong", "Loose Long", 1.10f, 0.70f, 0.0f, 1.2f },
      { "dab", "Dab", 1.60f, 0.10f, 0.0f, 1.0f },
      { "tightCurl", "Tight Curl", 0.50f, 0.05f, 18.0f, 0.9f },
      { "looseCurl", "Loose Curl", 0.80f, 0.40f, 28.0f, 1.1f },
   };
   const StyleRow& Row(Style s) { return kStyles[(int)s]; }

   constexpr float kPi = 3.14159265358979323846f;
   constexpr float kLimit = 1000000.0f;   // a halo or sample side past this is refused

   bool Finite(float v) { return std::isfinite(v); }

   uint32_t Mix32(uint32_t seed, uint32_t dab, uint32_t lane)
   {
      uint32_t x = seed ^ (dab * 0x9E3779B9u) ^ (lane * 0x85EBCA6Bu);
      x += 0x9E3779B9u;
      x ^= x >> 16;
      x *= 0x7FEB352Du;
      x ^= x >> 15;
      x *= 0x846CA68Bu;
      x ^= x >> 16;
      return x;
   }

   float Signed01(uint32_t seed, uint32_t dab, uint32_t lane)
   {
      const uint32_t u = Mix32(seed, dab, lane) >> 8;
      return (float)u * (2.0f / 16777215.0f) - 1.0f;
   }

   bool DabRadius(float size, Style style, float& r)
   {
      r = size * Row(style).size * 0.5f;
      return Finite(r) && r > 0.0f;
   }

   bool DabBounds(const Dab& d, int& x0, int& y0, int& x1, int& y1)
   {
      const float r = d.radius + 1.0f;
      if (!Finite(r))
         return false;
      const float fx0 = std::floor(d.x - r), fy0 = std::floor(d.y - r), fx1 = std::ceil(d.x + r), fy1 = std::ceil(d.y + r);
      if (!(Finite(fx0) && Finite(fy0) && Finite(fx1) && Finite(fy1)))
         return false;
      auto clampI = [](float v) { return (int)std::clamp(v, (float)INT_MIN, (float)INT_MAX); };
      x0 = clampI(fx0);
      y0 = clampI(fy0);
      x1 = clampI(fx1);
      y1 = clampI(fy1);
      return true;
   }

   float CoverageAt(const Dab& d, int x, int y, float hardness)
   {
      const float dx = (float)x + 0.5f - d.x, dy = (float)y + 0.5f - d.y;
      const float dist = std::hypot(dx, dy);
      if (!Finite(dist) || dist >= d.radius + 0.5f)
         return 0.0f;
      const float edge = std::clamp(d.radius + 0.5f - dist, 0.0f, 1.0f);
      const float h = std::clamp(hardness, 0.0f, 1.0f);
      const float inner = d.radius * h;
      float falloff = 1.0f;
      if (!(dist <= inner || d.radius - inner < 1.0e-3f))
      {
         const float t = std::clamp((dist - inner) / (d.radius - inner), 0.0f, 1.0f);
         falloff = 1.0f - t * t * (3.0f - 2.0f * t);
      }
      return edge * falloff;
   }

   // Index of pixel (x, y) in a buffer whose first pixel sits at (ox, oy); -1 outside it.
   long long Index(int ox, int oy, int x, int y, int w, int h, int ch)
   {
      const long long lx = (long long)x - ox, ly = (long long)y - oy;
      if (lx < 0 || ly < 0 || lx >= w || ly >= h || ch <= 0)
         return -1;
      return (ly * w + lx) * ch;
   }

   int FloorI(float v) { return (int)std::floor(std::clamp(v, -2147483648.0f, 2147483647.0f)); }

   // Bilinear read at continuous (sx, sy); pixel i has its centre at i + 0.5. Colour is weighted by alpha, so a
   // transparent neighbour does not darken the edge.
   bool Sample(const StampParams& p, float sx, float sy, float* out)
   {
      const int ch = p.ch;
      const int alpha = p.alpha >= 0 && p.alpha < ch ? p.alpha : -1;
      std::fill(out, out + ch, 0.0f);
      if (!(Finite(sx) && Finite(sy)))
         return false;
      const float lx = sx - (float)p.srcX - 0.5f, ly = sy - (float)p.srcY - 0.5f;
      if (!(Finite(lx) && Finite(ly)))
         return false;
      const int x0 = FloorI(lx), y0 = FloorI(ly);
      const float fx = std::clamp(lx - (float)x0, 0.0f, 1.0f), fy = std::clamp(ly - (float)y0, 0.0f, 1.0f);
      bool any = false;
      for (int iy = 0; iy < 2; iy++)
         for (int ix = 0; ix < 2; ix++)
         {
            const float wt = (ix == 0 ? 1.0f - fx : fx) * (iy == 0 ? 1.0f - fy : fy);
            if (wt <= 0.0f)
               continue;
            const long long px = (long long)x0 + ix, py = (long long)y0 + iy;
            if (px < 0 || py < 0 || px >= p.srcW || py >= p.srcH)
               continue;
            const float* s = p.src + (py * p.srcW + px) * ch;
            const float cov = alpha >= 0 ? s[alpha] : 1.0f;
            for (int c = 0; c < ch; c++)
               out[c] += s[c] * wt * (alpha >= 0 && c < alpha ? cov : 1.0f);
            any = true;
         }
      if (alpha >= 0)
      {
         const float cov = out[alpha];
         for (int c = 0; c < alpha; c++)
            out[c] = cov > 0.0f ? out[c] / cov : 0.0f;
      }
      return any;
   }

   void RotatedSource(const Dab& d, int x, int y, float& sx, float& sy)
   {
      const float dx = (float)x + 0.5f - d.x, dy = (float)y + 0.5f - d.y;
      const float c = std::cos(d.theta), s = std::sin(d.theta);
      sx = d.x + c * dx + s * dy;
      sy = d.y - s * dx + c * dy;
   }

   void Over(float* dst, const float* src, float k, int ch, int alpha, bool lock)
   {
      if (k <= 0.0f)
         return;
      if (alpha >= 0)
      {
         const float da = dst[alpha], sa = src[alpha] * k;
         const float oa = sa + da * (1.0f - sa);
         if (oa > 0.0f)
            for (int c = 0; c < alpha; c++)
               dst[c] = (src[c] * sa + dst[c] * da * (1.0f - sa)) / oa;
         if (!lock)
            dst[alpha] = oa;
         return;
      }
      for (int c = 0; c < ch; c++)
         dst[c] += (src[c] - dst[c]) * k;
   }

   struct Rect
   {
      int x0, y0, x1, y1;
   };

   std::vector<Rect> TileGrid(const StampParams& p)
   {
      const int x0 = p.destX, y0 = p.destY;
      const int x1 = (int)std::min<long long>((long long)x0 + p.destW, INT_MAX), y1 = (int)std::min<long long>((long long)y0 + p.destH, INT_MAX);
      if (p.tile <= 0)
         return { { x0, y0, x1, y1 } };
      std::vector<Rect> tiles;
      for (int y = y0; y < y1;)
      {
         const int ny = (int)std::min<long long>((long long)y + p.tile, y1);
         for (int x = x0; x < x1;)
         {
            const int nx = (int)std::min<long long>((long long)x + p.tile, x1);
            tiles.push_back({ x, y, nx, ny });
            x = nx;
         }
         y = ny;
      }
      return tiles;
   }

   Error StampRect(const StampParams& p, const Dab& d, Rect r, uint32_t& written)
   {
      if (r.x1 <= r.x0 || r.y1 <= r.y0)
         return Error::None;
      const long long tw = (long long)r.x1 - r.x0, th = (long long)r.y1 - r.y0;
      if ((uint64_t)tw * (uint64_t)th > kMaxCoverage)
         return Error::TooLarge;
      // Reads always come from the history state, never from pixels this dab already wrote, so the rectangle is
      // stamped in place: the tile split changes nothing.
      std::vector<float> sampled((size_t)p.ch);
      const float kStroke = std::clamp(p.opacity * p.flow * d.pressure, 0.0f, 1.0f);
      for (int y = r.y0; y < r.y1; y++)
         for (int x = r.x0; x < r.x1; x++)
         {
            const float c = CoverageAt(d, x, y, p.hardness);
            if (c <= 0.0f)
               continue;
            float k = c * kStroke;
            if (p.selection != nullptr)
            {
               const long long si = Index(p.destX, p.destY, x, y, p.destW, p.destH, 1);
               if (si < 0)
                  continue;
               k *= std::clamp(p.selection[si], 0.0f, 1.0f);
            }
            if (k <= 0.0f)
               continue;
            float sx = 0, sy = 0;
            RotatedSource(d, x, y, sx, sy);
            if (!Sample(p, sx, sy, sampled.data()))
               continue;
            if (p.alpha >= 0 && sampled[(size_t)p.alpha] == 0.0f)
               continue;   // a fully transparent sample leaves the pixel as it is
            const long long di = Index(p.destX, p.destY, x, y, p.destW, p.destH, p.ch);
            if (di < 0)
               continue;
            float* dst = p.dest + di;
            if (p.lockTransparent && p.alpha >= 0 && dst[p.alpha] <= 0.0f)
               continue;
            Over(dst, sampled.data(), k, p.ch, p.alpha, p.lockTransparent);
            written++;
         }
      return Error::None;
   }

   // Mean change between the history state and the destination under the dab: per pixel the larger of the mean
   // colour change and the alpha change, so an unchanged alpha does not water a colour change down and an erased
   // patch (colour kept, alpha lowered) counts. Transparent source samples are left out. False: nothing covered.
   bool MeanDiff(const StampParams& p, const Dab& d, float& mean)
   {
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      if (!DabBounds(d, x0, y0, x1, y1))
         return false;
      std::vector<float> sampled((size_t)p.ch);
      float sum = 0.0f;
      unsigned count = 0, transparent = 0;
      for (int y = y0; y < y1; y++)
         for (int x = x0; x < x1; x++)
         {
            if (CoverageAt(d, x, y, p.hardness) <= 0.0f)
               continue;
            const long long di = Index(p.destX, p.destY, x, y, p.destW, p.destH, p.ch);
            if (di < 0)
               continue;
            const float* dst = p.dest + di;
            float sx = 0, sy = 0;
            RotatedSource(d, x, y, sx, sy);
            if (!Sample(p, sx, sy, sampled.data()))
               continue;
            if (p.alpha >= 0 && sampled[(size_t)p.alpha] == 0.0f)
            {
               transparent++;
               continue;
            }
            float colour = 0.0f, a = 0.0f;
            int colours = 0;
            for (int i = 0; i < p.ch; i++)
            {
               const float diff = std::fabs(sampled[(size_t)i] - dst[i]);
               if (i == p.alpha)
                  a = diff;
               else
               {
                  colour += diff;
                  colours++;
               }
            }
            sum += std::max(colours == 0 ? 0.0f : colour / (float)colours, a);
            count++;
         }
      if (count == 0)
      {
         mean = 0.0f;
         return transparent > 0;
      }
      mean = sum / (float)count;
      return Finite(mean);
   }

   bool Intersects(const Dab& d, const Rect& t)
   {
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      return DabBounds(d, x0, y0, x1, y1) && x0 < t.x1 && x1 > t.x0 && y0 < t.y1 && y1 > t.y0;
   }
}

const char* StyleId(Style s) { return Row(s).id; }
const char* StyleName(Style s) { return Row(s).name; }

bool ParseStyle(const std::string& id, Style& out)
{
   for (int i = 0; i < kStyleCount; i++)
      if (id == kStyles[i].id)
      {
         out = (Style)i;
         return true;
      }
   return false;
}

bool SpacingPx(float size, float area, Style style, float& out)
{
   if (!(Finite(size) && Finite(area)) || size <= 0.0f)
      return false;
   out = size * Row(style).spacing * (0.25f + 0.02f * area);
   return Finite(out) && out > 0.0f;
}

bool HaloRadius(float size, Style style, int& out)
{
   if (!Finite(size) || size <= 0.0f)
      return false;
   const float v = std::ceil(size * Row(style).size / 2.0f + Row(style).scatter * size + 2.0f);
   if (!Finite(v) || v > kLimit)
      return false;
   out = (int)v;
   return true;
}

bool SampleSide(float size, Style style, int& out)
{
   if (!Finite(size) || size <= 0.0f)
      return false;
   const float v = std::ceil(size * Row(style).size) + 2.0f;
   if (!Finite(v) || v > kLimit)
      return false;
   out = (int)v;
   return true;
}

Error PlaceDabs(const std::vector<float>& pts, float size, float area, Style style, uint32_t seed, std::vector<Dab>& out)
{
   out.clear();
   const size_t n = pts.size() / 2;
   if (n == 0)
      return Error::Empty;
   float spacing = 0, radius = 0;
   if (!SpacingPx(size, area, style, spacing))
      return Error::Empty;
   if (!DabRadius(size, style, radius))
      return Error::TooLarge;
   const float curl = Row(style).curlDeg * kPi / 180.0f;
   const float scatter = Row(style).scatter;
   auto emit = [&](uint32_t i, float x, float y, float tx, float ty, float pressure) -> Error {
      if (!(Finite(x) && Finite(y) && Finite(tx) && Finite(ty)))
         return Error::Empty;
      const float u = Signed01(seed, i, 1), v = Signed01(seed, i, 2), w = Signed01(seed, i, 3);
      const float cx = x + scatter * size * u, cy = y + scatter * size * v;
      if (!(Finite(cx) && Finite(cy)))
         return Error::TooLarge;
      const float theta = std::atan2(ty, tx) + (float)i * curl + scatter * w;
      if (!Finite(theta))
         return Error::Empty;
      if (out.size() >= kMaxDabs)
         return Error::TooLarge;
      out.push_back({ cx, cy, theta, radius, std::clamp(pressure, 0.0f, 1.0f) });
      return Error::None;
   };
   if (!Finite(pts[0]) || !Finite(pts[1]))
      return Error::Empty;
   float px = pts[0], py = pts[1];
   float tx = 1.0f, ty = 0.0f;
   if (n > 1)
   {
      tx = pts[2] - px;
      ty = pts[3] - py;
      if (tx == 0.0f && ty == 0.0f)
         tx = 1.0f;
   }
   if (Error e = emit(0, px, py, tx, ty, 1.0f); e != Error::None)
      return e;
   float carry = 0.0f;
   uint32_t i = 1;
   for (size_t k = 0; k + 1 < n; k++)
   {
      const float ax = pts[k * 2], ay = pts[k * 2 + 1], bx = pts[k * 2 + 2], by = pts[k * 2 + 3];
      if (!(Finite(ax) && Finite(ay) && Finite(bx) && Finite(by)))
         return Error::Empty;
      const float dx = bx - ax, dy = by - ay;
      const float len = std::hypot(dx, dy);
      if (!Finite(len))
         return Error::Empty;
      if (len < 1.0e-6f)
         continue;
      tx = dx;
      ty = dy;
      float consumed = 0.0f;
      while (carry + (len - consumed) >= spacing)
      {
         const float need = spacing - carry;
         if (!Finite(need) || need < 0.0f)
            return Error::Empty;
         consumed += need;
         if (consumed > len)
            break;
         const float t = consumed / len;
         px = ax + dx * t;
         py = ay + dy * t;
         if (Error e = emit(i, px, py, tx, ty, 1.0f); e != Error::None)
            return e;
         i = i == UINT32_MAX ? i : i + 1;
         carry = 0.0f;
      }
      carry += len - consumed;
      if (!Finite(carry))
         return Error::Empty;
   }
   return out.empty() ? Error::Empty : Error::None;
}

Error Stamp(const StampParams& p, uint32_t& written)
{
   written = 0;
   if (p.ch <= 0 || p.destW <= 0 || p.destH <= 0 || p.dest == nullptr)
      return Error::Empty;
   if (p.srcW <= 0 || p.srcH <= 0 || p.src == nullptr)
      return Error::Empty;
   if (p.dabs == nullptr || p.dabCount == 0)
      return Error::Empty;
   if ((uint64_t)p.destW * (uint64_t)p.destH > kMaxCoverage)
      return Error::TooLarge;
   const std::vector<Rect> tiles = TileGrid(p);
   for (size_t n = 0; n < p.dabCount; n++)
   {
      const Dab& d = p.dabs[n];
      int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
      if (!DabBounds(d, bx0, by0, bx1, by1))
         continue;
      float mean = 0.0f;
      if (p.tolerance > 0.0f && MeanDiff(p, d, mean) && mean < p.tolerance)
         continue;
      for (const Rect& t : tiles)
      {
         if (!Intersects(d, t))
            continue;
         const Rect r = { std::max(t.x0, bx0), std::max(t.y0, by0), std::min(t.x1, bx1), std::min(t.y1, by1) };
         if (Error e = StampRect(p, d, r, written); e != Error::None)
            return e;
      }
   }
   return Error::None;
}
}

// ---- self-tests (PhotoCraft crates/algo/src/art_history/tests.rs) ----
namespace ArtHistory
{
namespace
{
   template <class F> std::vector<float> Fill(int w, int h, int ch, F f)
   {
      std::vector<float> v((size_t)w * h * ch);
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
            for (int c = 0; c < ch; c++)
               v[((size_t)y * w + x) * ch + c] = f(x, y, c);
      return v;
   }

   StampParams Params(int w, int h, int ch, const std::vector<float>& src, std::vector<float>& dest, const Dab* dabs, size_t n, int tile)
   {
      StampParams p;
      p.dest = dest.data();
      p.destW = w;
      p.destH = h;
      p.src = src.data();
      p.srcW = w;
      p.srcH = h;
      p.ch = ch;
      p.dabs = dabs;
      p.dabCount = n;
      p.tile = tile;
      p.alpha = ch > 1 ? ch - 1 : -1;
      return p;
   }

   uint32_t StampDabs(int w, int h, int ch, const std::vector<float>& src, std::vector<float>& dest, const std::vector<Dab>& dabs, int tile)
   {
      uint32_t n = 0;
      StampParams p = Params(w, h, ch, src, dest, dabs.data(), dabs.size(), tile);
      Stamp(p, n);
      return n;
   }

   uint32_t StampRun(int w, int h, int ch, const std::vector<float>& src, std::vector<float>& dest, const Dab& d, int tile)
   {
      return StampDabs(w, h, ch, src, dest, { d }, tile);
   }

   float MaxAbs(const std::vector<float>& a, const std::vector<float>& b)
   {
      float m = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++)
         m = std::max(m, std::fabs(a[i] - b[i]));
      return m;
   }

   int MaxU8(const std::vector<float>& a, const std::vector<float>& b)
   {
      auto q = [](float v) { return (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
      int m = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++)
         m = std::max(m, std::abs(q(a[i]) - q(b[i])));
      return m;
   }

   std::vector<float> Horizontal(float len) { return { 10.0f, 32.0f, 10.0f + len, 32.0f }; }

   int Rem(int a, int m) { return ((a % m) + m) % m; }

   std::vector<float> Pattern(int w, int h, int ch)
   {
      return Fill(w, h, ch, [ch](int x, int y, int c) {
         return c + 1 == ch && ch > 1 ? 1.0f : (float)Rem(x * 13 + y * 7 + c * 3, 251) / 255.0f;
      });
   }
}

std::vector<std::pair<std::string, bool>> SelfTests()
{
   std::vector<std::pair<std::string, bool>> out;
   auto add = [&](const char* name, bool ok) { out.emplace_back(name, ok); };

   {
      bool ok = true;
      for (int ch : { 2, 4, 5 })
         for (float hidden : { 1.0f, 0.0f })
         {
            const auto src = Fill(2, 1, ch, [&](int x, int, int c) { return c + 1 == ch ? (x == 0 ? 1.0f : 0.0f) : x == 0 ? 0.4f : hidden; });
            const auto before = Fill(2, 1, ch, [&](int x, int, int c) { return c + 1 == ch ? (x == 0 ? 1.0f : 0.0f) : x == 0 ? 0.0f : hidden; });
            const Dab d = { 1.0f, 0.5f, 0.0f, 2.0f, 1.0f };
            for (float tol : { 0.6f, 0.3f })
            {
               auto dest = before;
               StampParams p = Params(2, 1, ch, src, dest, &d, 1, 0);
               p.tolerance = tol;
               uint32_t n = 0;
               Stamp(p, n);
               if (tol == 0.6f)
                  ok = ok && n == 0 && dest == before;
               else
                  ok = ok && n > 0 && std::fabs(dest[0] - 0.4f) < 1e-6f && std::equal(dest.begin() + ch, dest.end(), before.begin() + ch);
            }
         }
      add("tolerance_ignores_fully_transparent_samples", ok);
   }
   {
      bool ok = true;
      for (int ch : { 2, 4, 5 })
         for (float opaque : { 1.0f, 0.0f })
         {
            const std::vector<float> src((size_t)16 * ch, 0.0f);
            const auto before = Fill(4, 4, ch, [&](int, int, int c) { return c + 1 == ch ? opaque : 0.5f; });
            auto dest = before;
            const Dab d = { 2.0f, 2.0f, 0.0f, 2.0f, 1.0f };
            StampParams p = Params(4, 4, ch, src, dest, &d, 1, 0);
            p.tolerance = 0.1f;
            uint32_t n = 0;
            Stamp(p, n);
            ok = ok && n == 0 && dest == before;
         }
      add("tolerance_skips_a_dab_whose_source_is_fully_transparent", ok);
   }
   {
      bool ok = true;
      for (int ch : { 2, 4, 5 })
         for (float opaque : { 1.0f, 0.0f })
         {
            const std::vector<float> src((size_t)16 * ch, 0.0f);
            const auto before = Fill(4, 4, ch, [&](int, int, int c) { return c + 1 == ch ? opaque : 0.5f; });
            auto dest = before;
            const uint32_t n = StampRun(4, 4, ch, src, dest, { 2.0f, 2.0f, 0.0f, 2.0f, 1.0f }, 0);
            ok = ok && n == 0 && dest == before;
         }
      add("zero_tolerance_does_not_write_fully_transparent_samples", ok);
   }
   {
      bool ok = true;
      for (int ch : { 2, 4, 5 })
         for (float hidden : { 0.0f, 1.0f })
         {
            const auto src = Fill(4, 4, ch, [&](int x, int, int c) {
               return c + 1 == ch ? (x < 2 ? 1.0f : 0.0f) : x < 2 ? (c == 0 ? 0.8f : 0.25f) : hidden;
            });
            std::vector<float> dest((size_t)16 * ch, 0.0f);
            StampRun(4, 4, ch, src, dest, { 2.0f, 2.0f, kPi / 4.0f, 4.0f, 1.0f }, 0);
            int partial = 0;
            for (size_t i = 0; i < dest.size(); i += ch)
            {
               const float a = dest[i + ch - 1];
               if (a > 0.0f && a < 1.0f)
               {
                  partial++;
                  for (int c = 0; c < ch - 1; c++)
                     ok = ok && std::fabs(dest[i + c] - (c == 0 ? 0.8f : 0.25f)) < 1e-6f;
               }
            }
            ok = ok && partial > 0;
         }
      add("rotated_dabs_preserve_color_at_transparent_edges", ok);
   }
   {
      const auto pts = Horizontal(100.0f);
      std::vector<Dab> s, l;
      float ss = 0, ls = 0;
      const bool ok = PlaceDabs(pts, 10.0f, 50.0f, Style::TightShort, kSeed, s) == Error::None &&
                      PlaceDabs(pts, 10.0f, 50.0f, Style::TightLong, kSeed, l) == Error::None && s.size() > l.size() &&
                      SpacingPx(10.0f, 50.0f, Style::TightShort, ss) && SpacingPx(10.0f, 50.0f, Style::TightLong, ls) && ss < ls;
      add("tight_short_emits_more_dabs_than_tight_long", ok);
   }
   {
      const auto pts = Horizontal(80.0f);
      std::vector<Dab> t, l;
      bool ok = PlaceDabs(pts, 12.0f, 50.0f, Style::TightCurl, kSeed, t) == Error::None &&
                PlaceDabs(pts, 12.0f, 50.0f, Style::LooseCurl, kSeed, l) == Error::None && t.size() >= 3 && l.size() >= 3;
      if (ok)
      {
         float mt = 0, ml = 0, st = 0, sl = 0;
         for (size_t i = 1; i < t.size(); i++)
         {
            ok = ok && t[i].theta - t[i - 1].theta > 0.05f;
            mt += t[i].theta - t[i - 1].theta;
         }
         for (size_t i = 1; i < l.size(); i++)
            ml += l[i].theta - l[i - 1].theta;
         mt /= (float)(t.size() - 1);
         ml /= (float)(l.size() - 1);
         for (const Dab& d : t)
            st += std::fabs(d.y - 32.0f);
         for (const Dab& d : l)
            sl += std::fabs(d.y - 32.0f);
         ok = ok && ml > mt && sl > st;
      }
      add("tight_curl_angles_increase_and_loose_curl_scatters", ok);
   }
   {
      const auto pts = Horizontal(60.0f);
      std::vector<Dab> a, b, c;
      PlaceDabs(pts, 16.0f, 40.0f, Style::LooseMedium, kSeed ^ 3u, a);
      PlaceDabs(pts, 16.0f, 40.0f, Style::LooseMedium, kSeed ^ 3u, b);
      PlaceDabs(pts, 16.0f, 40.0f, Style::LooseMedium, kSeed ^ 4u, c);
      add("seeded_scatter_is_replayable", !a.empty() && a == b && !(a == c));
   }
   {
      const int w = 128, h = 96;
      const Dab d = { 63.0f, 40.0f, 0.4f, 18.0f, 1.0f };
      const auto src4 = Pattern(w, h, 4);
      const auto d4 = Fill(w, h, 4, [](int, int, int c) { return c == 3 ? 1.0f : 0.1f; });
      auto tiled = d4, flat = d4;
      const uint32_t nt = StampRun(w, h, 4, src4, tiled, d, 64), nf = StampRun(w, h, 4, src4, flat, d, 0);
      bool ok = nt > 0 && nf > 0 && MaxU8(tiled, flat) == 0 && MaxAbs(tiled, flat) <= 1.0f / 32767.0f && MaxAbs(tiled, flat) <= 1.0e-5f;
      const auto src1 = Pattern(w, h, 1);
      const auto d1 = Fill(w, h, 1, [](int, int, int) { return 0.1f; });
      auto t1 = d1, f1 = d1;
      StampRun(w, h, 1, src1, t1, d, 64);
      StampRun(w, h, 1, src1, f1, d, 0);
      ok = ok && MaxU8(t1, f1) == 0;
      add("tiled_stamp_matches_untiled_at_a_tile_seam", ok);
   }
   {
      const int w = 128, h = 96, ch = 4;
      const auto src = Pattern(w, h, ch);
      const auto d0 = Fill(w, h, ch, [](int, int, int c) { return c == 3 ? 1.0f : 0.1f; });
      bool ok = true;
      for (Style s : { Style::TightMedium, Style::TightCurl, Style::LooseCurl })
      {
         std::vector<Dab> dabs;
         ok = ok && PlaceDabs({ 110.0f, 40.0f, 20.0f, 40.0f }, 40.0f, 50.0f, s, kSeed, dabs) == Error::None;
         auto tiled = d0, flat = d0;
         StampDabs(w, h, ch, src, tiled, dabs, 64);
         StampDabs(w, h, ch, src, flat, dabs, 0);
         ok = ok && MaxAbs(tiled, flat) == 0.0f;
      }
      add("leftward_and_curl_strokes_match_untiled_across_a_tile_edge", ok);
   }
   {
      const int w = 48, h = 48, ch = 4;
      const auto src = Fill(w, h, ch, [](int x, int y, int c) { return c == 3 ? 1.0f : (float)Rem(x * 37 + y * 11 + c * 5, 97) / 96.0f; });
      auto dest = Fill(w, h, ch, [](int, int, int c) { return c == 3 ? 1.0f : 0.5f; });
      StampRun(w, h, ch, src, dest, { 24.0f, 24.0f, 0.0f, 8.0f, 1.0f }, 0);
      bool ok = true;
      for (auto [x, y] : { std::pair<int, int>{ 24, 24 }, { 20, 22 }, { 27, 26 } })
      {
         const size_t i = ((size_t)y * w + x) * ch;
         ok = ok && std::equal(dest.begin() + i, dest.begin() + i + ch, src.begin() + i);
      }
      add("unrotated_dab_restores_the_source_pixel", ok);
   }
   {
      bool ok = true;
      for (int s = 0; s < kStyleCount; s++)
      {
         int halo = 0, side = 0;
         ok = ok && HaloRadius(40.0f, (Style)s, halo) && SampleSide(40.0f, (Style)s, side) && halo >= (side + 1) / 2;
      }
      add("halo_covers_rotated_read_reach", ok);
   }
   {
      const int w = 48, h = 48, ch = 4;
      const auto src = Fill(w, h, ch, [](int x, int y, int c) { return c == 3 ? 1.0f : (float)(x + y) / 100.0f; });
      auto dest = src;
      const Dab d = { 24.0f, 24.0f, 0.0f, 8.0f, 1.0f };
      StampParams p = Params(w, h, ch, src, dest, &d, 1, 0);
      p.tolerance = 1.0f;
      uint32_t n = 0;
      Stamp(p, n);
      add("tolerance_skips_an_unchanged_layer", n == 0 && dest == src);
   }
   {
      const int w = 48, h = 48, ch = 4;
      const auto src = Fill(w, h, ch, [](int, int, int c) { return c == 3 ? 1.0f : 0.9f; });
      auto dest = Fill(w, h, ch, [](int, int, int c) { return c == 3 ? 1.0f : 0.1f; });
      const uint32_t n = StampRun(w, h, ch, src, dest, { 24.0f, 24.0f, 0.0f, 8.0f, 1.0f }, 0);
      add("tolerance_zero_writes_where_history_differs", n > 0 && std::fabs(dest[((size_t)24 * w + 24) * ch] - 0.9f) < 1.0e-5f);
   }
   {
      const int w = 48, h = 48, ch = 4;
      const auto src = Fill(w, h, ch, [](int, int, int c) { return c == 3 ? 1.0f : 0.6f; });
      const auto d0 = Fill(w, h, ch, [](int, int, int c) { return c == 3 ? 1.0f : 0.3f; });
      const Dab d = { 24.0f, 24.0f, 0.0f, 8.0f, 1.0f };
      bool ok = true;
      for (auto [tol, paints] : { std::pair<float, bool>{ 0.25f, true }, { 0.35f, false } })
      {
         auto dest = d0;
         StampParams p = Params(w, h, ch, src, dest, &d, 1, 0);
         p.tolerance = tol;
         uint32_t n = 0;
         Stamp(p, n);
         ok = ok && (n > 0) == paints;
      }
      add("tolerance_ignores_unchanged_alpha", ok);
   }
   {
      const int w = 48, h = 48, ch = 4;
      const auto src = Fill(w, h, ch, [](int, int, int c) { return c == 0 || c == 3 ? 1.0f : 0.0f; });
      auto dest = Fill(w, h, ch, [](int, int, int c) { return c == 0 ? 1.0f : c == 3 ? 0.6f : 0.0f; });
      const Dab d = { 24.0f, 24.0f, 0.0f, 8.0f, 1.0f };
      StampParams p = Params(w, h, ch, src, dest, &d, 1, 0);
      p.tolerance = 0.25f;
      uint32_t n = 0;
      Stamp(p, n);
      add("tolerance_sees_an_erased_patch", n > 0 && std::fabs(dest[((size_t)24 * w + 24) * ch + 3] - 1.0f) < 1.0e-5f);
   }
   {
      std::vector<Dab> dabs;
      PlaceDabs(Horizontal(50.0f), 12.0f, 50.0f, Style::LooseCurl, kSeed, dabs);
      const int w = 80, h = 64, ch = 4;
      const auto src = Fill(w, h, ch, [](int x, int y, int c) { return c == 3 ? 1.0f : (float)(x + 3 * y) / 200.0f; });
      const auto d0 = Fill(w, h, ch, [](int, int, int c) { return c == 3 ? 1.0f : 0.2f; });
      auto run = [&](std::vector<float>& dest) {
         StampParams p = Params(w, h, ch, src, dest, dabs.data(), dabs.size(), 64);
         p.hardness = 0.8f;
         uint32_t n = 0;
         Stamp(p, n);
         return n;
      };
      auto a = d0, b = d0;
      const uint32_t na = run(a), nb = run(b);
      add("two_stamps_with_the_same_seed_match", !dabs.empty() && na == nb && a == b);
   }
   return out;
}
}
