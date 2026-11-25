#include "SelectMaths.h"

#include <algorithm>
#include <cmath>

namespace SelectMaths
{
   void BoxMean(const std::vector<float>& in, std::vector<float>& out, int w, int h, int r)
   {
      out.resize((size_t)w * h);
      if (r < 1 || w < 1 || h < 1)
      {
         out = in;
         return;
      }
      std::vector<float> tmp((size_t)w * h);
      const float n = (float)(2 * r + 1);
      for (int y = 0; y < h; y++)
      {
         const float* row = &in[(size_t)y * w];
         double sum = 0;
         for (int x = -r; x <= r; x++)
            sum += row[std::clamp(x, 0, w - 1)];
         for (int x = 0; x < w; x++)
         {
            tmp[(size_t)y * w + x] = (float)(sum / n);
            sum += row[std::min(w - 1, x + r + 1)] - row[std::max(0, x - r)];
         }
      }
      for (int x = 0; x < w; x++)
      {
         double sum = 0;
         for (int y = -r; y <= r; y++)
            sum += tmp[(size_t)std::clamp(y, 0, h - 1) * w + x];
         for (int y = 0; y < h; y++)
         {
            out[(size_t)y * w + x] = (float)(sum / n);
            sum += tmp[(size_t)std::min(h - 1, y + r + 1) * w + x] - tmp[(size_t)std::max(0, y - r) * w + x];
         }
      }
   }

   namespace
   {
      float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
      uint8_t To8(float v) { return (uint8_t)std::lround(Clamp01(v) * 255.0f); }

      std::vector<float> Mul(const std::vector<float>& a, const std::vector<float>& b)
      {
         std::vector<float> o(a.size());
         for (size_t i = 0; i < a.size(); i++)
            o[i] = a[i] * b[i];
         return o;
      }

      // Three box passes: close to a Gaussian with sigma (sigma^2 = r (r + 1) for three passes of half-width r).
      void Gauss(std::vector<float>& v, int w, int h, float sigma)
      {
         const int r = (int)std::lround((std::sqrt(1.0f + 4.0f * sigma * sigma) - 1.0f) * 0.5f);
         if (r < 1)
            return;
         std::vector<float> t;
         for (int pass = 0; pass < 3; pass++)
         {
            BoxMean(v, t, w, h, r);
            v.swap(t);
         }
      }

      float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
   }

   namespace
   {
      // One extended box along a line of n samples `stride` apart: half-width r plus a tap of weight alpha at +-(r + 1).
      void ExtBoxLine(const float* in, float* out, int n, size_t stride, int r, float alpha)
      {
         const float norm = 1.0f / (2.0f * (float)r + 1.0f + 2.0f * alpha);
         auto at = [&](int i) { return in[(size_t)std::clamp(i, 0, n - 1) * stride]; };
         double sum = 0.0;
         for (int k = -r; k <= r; k++)
            sum += at(k);
         for (int i = 0; i < n; i++)
         {
            out[(size_t)i * stride] = (float)((sum + alpha * (at(i - r - 1) + at(i + r + 1))) * norm);
            sum += at(i + r + 1) - at(i - r);
         }
      }
   }

   void GaussianBlur(std::vector<float>& v, int w, int h, float sigma)
   {
      if (!(sigma > 0.0f) || w < 1 || h < 1 || v.size() != (size_t)w * h)
         return;
      constexpr int kPasses = 3;
      const float s2 = sigma * sigma / (float)kPasses;   // variance each pass adds
      // Largest whole box not over it (a box of half-width r has variance r (r + 1) / 3), then the end taps' weight.
      const int r = std::max(0, (int)std::floor((std::sqrt(1.0f + 12.0f * s2) - 1.0f) * 0.5f));
      const float rr = (float)r, r1 = rr + 1.0f;
      const float alpha = (2.0f * rr + 1.0f) * (rr * r1 - 3.0f * s2) / (6.0f * (s2 - r1 * r1));
      std::vector<float> t(v.size());
      for (int pass = 0; pass < kPasses; pass++)
      {
         for (int y = 0; y < h; y++)
            ExtBoxLine(&v[(size_t)y * w], &t[(size_t)y * w], w, 1, r, alpha);
         for (int x = 0; x < w; x++)
            ExtBoxLine(&t[x], &v[x], h, (size_t)w, r, alpha);
      }
   }

   // ---- Select and Mask ----------------------------------------------------------------------------------------
   void GuidedEdge(const uint8_t* rgba, std::vector<uint8_t>& mask, int w, int h, float radius, bool smart)
   {
      if (rgba == nullptr || w < 2 || h < 2 || mask.size() != (size_t)w * h || radius < 0.5f)
         return;
      // Working size: the fast guided filter (He and Sun 2015) for big pictures.
      const int s = std::max(1, (std::max(w, h) + 1023) / 1024);
      const int ww = (w + s - 1) / s, hh = (h + s - 1) / s;
      const size_t n = (size_t)ww * hh;
      std::vector<float> I[3], p(n, 0.0f);
      for (auto& c : I)
         c.assign(n, 0.0f);
      for (int y = 0; y < hh; y++)
         for (int x = 0; x < ww; x++)
         {
            float acc[4] = { 0, 0, 0, 0 };
            int cnt = 0;
            for (int yy = y * s; yy < std::min(h, y * s + s); yy++)
               for (int xx = x * s; xx < std::min(w, x * s + s); xx++)
               {
                  const size_t i = (size_t)yy * w + xx;
                  for (int k = 0; k < 3; k++)
                     acc[k] += rgba[i * 4 + k];
                  acc[3] += mask[i];
                  cnt++;
               }
            const size_t o = (size_t)y * ww + x;
            for (int k = 0; k < 3; k++)
               I[k][o] = acc[k] / (255.0f * cnt);
            p[o] = acc[3] / (255.0f * cnt);
         }

      // The band: pixels whose window of the band radius holds both inside and outside.
      auto bandOf = [&](float rad) {
         std::vector<float> bin(n), m;
         for (size_t i = 0; i < n; i++)
            bin[i] = p[i] >= 0.5f ? 1.0f : 0.0f;
         BoxMean(bin, m, ww, hh, std::max(1, (int)std::ceil(rad / s)));
         std::vector<uint8_t> b(n);
         for (size_t i = 0; i < n; i++)
            b[i] = m[i] > 1e-5f && m[i] < 1.0f - 1e-5f;
         return b;
      };
      std::vector<uint8_t> band = bandOf(radius);
      if (smart)
      {
         // Smart Radius: a third of the band on clean edges; the whole band where the picture is busy (hair, fur).
         const std::vector<uint8_t> thin = bandOf(std::max(1.0f, radius / 3.0f));
         std::vector<float> lum(n), m1, m2;
         for (size_t i = 0; i < n; i++)
            lum[i] = 0.299f * I[0][i] + 0.587f * I[1][i] + 0.114f * I[2][i];
         BoxMean(lum, m1, ww, hh, 2);
         BoxMean(Mul(lum, lum), m2, ww, hh, 2);
         for (size_t i = 0; i < n; i++)
            band[i] = thin[i] || (band[i] && m2[i] - m1[i] * m1[i] > 0.0015f);
      }

      // Inside the band the edge is redrawn from colour: each pixel's alpha is where its colour sits between the
      // mean of the solid inside and the solid outside nearby (searched wider until both are found). Where the two
      // means are close the colour says little, and the old mask is kept in proportion.
      {
         std::vector<float> fg(n), bg(n), cf[3], cb[3];
         for (size_t i = 0; i < n; i++)
         {
            fg[i] = !band[i] && p[i] >= 0.5f ? 1.0f : 0.0f;
            bg[i] = !band[i] && p[i] < 0.5f ? 1.0f : 0.0f;
         }
         for (int k = 0; k < 3; k++)
         {
            cf[k] = Mul(I[k], fg);
            cb[k] = Mul(I[k], bg);
         }
         std::vector<uint8_t> done(n, 0);
         std::vector<float> np = p;
         const int r0 = std::max(2, (int)std::ceil(radius / s));
         for (int reach = r0; reach <= r0 * 8; reach *= 2)
         {
            std::vector<float> mf, mb, mcf[3], mcb[3];
            BoxMean(fg, mf, ww, hh, reach);
            BoxMean(bg, mb, ww, hh, reach);
            for (int k = 0; k < 3; k++)
            {
               BoxMean(cf[k], mcf[k], ww, hh, reach);
               BoxMean(cb[k], mcb[k], ww, hh, reach);
            }
            for (size_t i = 0; i < n; i++)
            {
               if (!band[i] || done[i] || mf[i] < 1e-4f || mb[i] < 1e-4f)
                  continue;
               done[i] = 1;
               float d[3], c[3], dd = 0.0f, dc = 0.0f;
               for (int k = 0; k < 3; k++)
               {
                  const float F = mcf[k][i] / mf[i], B = mcb[k][i] / mb[i];
                  d[k] = F - B;
                  c[k] = I[k][i] - B;
                  dd += d[k] * d[k];
                  dc += d[k] * c[k];
               }
               if (dd < 1e-6f)
                  continue;
               const float alpha = Clamp01(dc / dd), conf = Clamp01(std::sqrt(dd) / 0.15f);
               np[i] = p[i] + (alpha - p[i]) * conf;
            }
         }
         p.swap(np);
      }

      // Colour guided filter: q = a . I + b, with a and b fitted per window and then averaged.
      const int r = std::max(1, (int)std::lround(std::clamp(radius * 0.25f, 2.0f, 8.0f) / s));
      const float eps = 1e-4f;
      std::vector<float> mI[3], mp, mIp[3], ab[4];
      for (int k = 0; k < 3; k++)
      {
         BoxMean(I[k], mI[k], ww, hh, r);
         BoxMean(Mul(I[k], p), mIp[k], ww, hh, r);
      }
      BoxMean(p, mp, ww, hh, r);
      std::vector<float> v[6];   // rr rg rb gg gb bb
      {
         static const int kPair[6][2] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 1 }, { 1, 2 }, { 2, 2 } };
         for (int k = 0; k < 6; k++)
            BoxMean(Mul(I[kPair[k][0]], I[kPair[k][1]]), v[k], ww, hh, r);
      }
      for (auto& c : ab)
         c.assign(n, 0.0f);
      for (size_t i = 0; i < n; i++)
      {
         const float rr = v[0][i] - mI[0][i] * mI[0][i] + eps, rg = v[1][i] - mI[0][i] * mI[1][i], rb = v[2][i] - mI[0][i] * mI[2][i];
         const float gg = v[3][i] - mI[1][i] * mI[1][i] + eps, gb = v[4][i] - mI[1][i] * mI[2][i], bb = v[5][i] - mI[2][i] * mI[2][i] + eps;
         const float cr = mIp[0][i] - mI[0][i] * mp[i], cg = mIp[1][i] - mI[1][i] * mp[i], cb = mIp[2][i] - mI[2][i] * mp[i];
         const float i00 = gg * bb - gb * gb, i01 = gb * rb - rg * bb, i02 = rg * gb - gg * rb;
         const float i11 = rr * bb - rb * rb, i12 = rb * rg - rr * gb, i22 = rr * gg - rg * rg;
         const float det = rr * i00 + rg * i01 + rb * i02;
         float a0 = 0, a1 = 0, a2 = 0;
         if (std::fabs(det) > 1e-20f)
         {
            a0 = (i00 * cr + i01 * cg + i02 * cb) / det;
            a1 = (i01 * cr + i11 * cg + i12 * cb) / det;
            a2 = (i02 * cr + i12 * cg + i22 * cb) / det;
         }
         ab[0][i] = a0;
         ab[1][i] = a1;
         ab[2][i] = a2;
         ab[3][i] = mp[i] - a0 * mI[0][i] - a1 * mI[1][i] - a2 * mI[2][i];
      }
      for (auto& c : v)
         std::vector<float>().swap(c);
      for (int k = 0; k < 4; k++)
      {
         std::vector<float> t;
         BoxMean(ab[k], t, ww, hh, r);
         ab[k].swap(t);
      }

      // Full size: the averaged coefficients (bilinear when the work ran smaller) applied to the full picture.
      for (int y = 0; y < h; y++)
      {
         const float fy = std::clamp((y + 0.5f) / s - 0.5f, 0.0f, (float)(hh - 1));
         const int y0 = (int)fy, y1 = std::min(y0 + 1, hh - 1);
         const float ty = fy - y0;
         for (int x = 0; x < w; x++)
         {
            if (!band[(size_t)std::min(hh - 1, y / s) * ww + std::min(ww - 1, x / s)])
               continue;
            const float fx = std::clamp((x + 0.5f) / s - 0.5f, 0.0f, (float)(ww - 1));
            const int x0 = (int)fx, x1 = std::min(x0 + 1, ww - 1);
            const float tx = fx - x0;
            float c[4];
            for (int k = 0; k < 4; k++)
            {
               const std::vector<float>& a = ab[k];
               const float top = a[(size_t)y0 * ww + x0] + (a[(size_t)y0 * ww + x1] - a[(size_t)y0 * ww + x0]) * tx;
               const float bot = a[(size_t)y1 * ww + x0] + (a[(size_t)y1 * ww + x1] - a[(size_t)y1 * ww + x0]) * tx;
               c[k] = top + (bot - top) * ty;
            }
            const size_t i = (size_t)y * w + x;
            const float q = c[0] * rgba[i * 4] / 255.0f + c[1] * rgba[i * 4 + 1] / 255.0f + c[2] * rgba[i * 4 + 2] / 255.0f + c[3];
            mask[i] = To8(q);
         }
      }
   }

   void AdjustEdge(std::vector<uint8_t>& mask, int w, int h, float smooth, float feather, float contrast, float shift)
   {
      if (mask.size() != (size_t)w * h || w < 1 || h < 1)
         return;
      const bool any = smooth >= 1.0f || feather > 0.25f || contrast > 0.0f || std::fabs(shift) > 0.0f;
      if (!any)
         return;
      std::vector<float> v(mask.size());
      for (size_t i = 0; i < v.size(); i++)
         v[i] = mask[i] / 255.0f;
      // Smooth: a box blur cut back to a narrow edge, so bumps and notches under the radius go.
      const int sr = std::min(10, (int)std::lround(smooth / 10.0f));
      if (sr >= 1)
      {
         std::vector<float> t;
         BoxMean(v, t, w, h, sr);
         const float k = (float)(sr + 1);
         for (size_t i = 0; i < v.size(); i++)
            v[i] = Clamp01((t[i] - 0.5f) * k + 0.5f);
      }
      if (feather > 0.25f)
         Gauss(v, w, h, feather);
      if (contrast > 0.0f)
      {
         const float k = 1.0f / std::max(1.0f - std::min(contrast, 100.0f) / 100.0f, 0.01f);
         for (float& x : v)
            x = Clamp01((x - 0.5f) * k + 0.5f);
      }
      if (shift != 0.0f)
      {
         const float s = 0.98f * std::clamp(shift, -100.0f, 100.0f) / 100.0f;
         for (float& x : v)
            x = s > 0.0f ? Clamp01(x / (1.0f - s)) : Clamp01((x + s) / (1.0f + s));
      }
      for (size_t i = 0; i < v.size(); i++)
         mask[i] = To8(v[i]);
   }

   void Refine(const uint8_t* rgba, std::vector<uint8_t>& mask, int w, int h, const RefineParams& p)
   {
      if (p.radius >= 0.5f)
         GuidedEdge(rgba, mask, w, h, p.radius, p.smart);
      AdjustEdge(mask, w, h, p.smooth, p.feather, p.contrast, p.shift);
   }

   void Decontaminate(uint8_t* rgba, const std::vector<uint8_t>& mask, int w, int h, float amount)
   {
      if (rgba == nullptr || mask.size() != (size_t)w * h || amount <= 0.0f)
         return;
      // Only the soft edge's box (plus the widest reach) is worked on.
      constexpr int kReach[3] = { 4, 16, 64 };
      int x0 = w, y0 = h, x1 = -1, y1 = -1;
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            const uint8_t m = mask[(size_t)y * w + x];
            if (m > 0 && m < 250)
               x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
         }
      if (x1 < x0)
         return;
      x0 = std::max(0, x0 - kReach[2]), y0 = std::max(0, y0 - kReach[2]);
      x1 = std::min(w - 1, x1 + kReach[2]), y1 = std::min(h - 1, y1 + kReach[2]);
      const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
      const size_t n = (size_t)bw * bh;
      std::vector<float> solid(n), col[3];
      for (auto& c : col)
         c.resize(n);
      for (int y = 0; y < bh; y++)
         for (int x = 0; x < bw; x++)
         {
            const size_t g = (size_t)(y + y0) * w + (x + x0), o = (size_t)y * bw + x;
            const float sd = mask[g] >= 250 ? 1.0f : 0.0f;
            solid[o] = sd;
            for (int k = 0; k < 3; k++)
               col[k][o] = sd * rgba[g * 4 + k];
         }
      std::vector<uint8_t> done(n, 0);
      const float amt = std::min(amount, 1.0f);
      for (int reach : kReach)
      {
         std::vector<float> ms, mc[3];
         BoxMean(solid, ms, bw, bh, reach);
         for (int k = 0; k < 3; k++)
            BoxMean(col[k], mc[k], bw, bh, reach);
         for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++)
            {
               const size_t o = (size_t)y * bw + x, g = (size_t)(y + y0) * w + (x + x0);
               const uint8_t m = mask[g];
               if (done[o] || m == 0 || m >= 250 || ms[o] < 1e-4f)
                  continue;
               done[o] = 1;
               // At half selected or less the colour is mostly background: replaced in full. Nearer solid, less.
               const float t = amt * std::min(1.0f, 2.0f * (1.0f - m / 255.0f));
               for (int k = 0; k < 3; k++)
               {
                  const float f = mc[k][o] / ms[o];
                  rgba[g * 4 + k] = (uint8_t)std::clamp((int)std::lround(rgba[g * 4 + k] + (f - rgba[g * 4 + k]) * t), 0, 255);
               }
            }
      }
   }

   // ---- Color Range --------------------------------------------------------------------------------------------
   void Lab(const float rgb[3], float lab[3])
   {
      const float r = SrgbToLinear(Clamp01(rgb[0])), g = SrgbToLinear(Clamp01(rgb[1])), b = SrgbToLinear(Clamp01(rgb[2]));
      const float X = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f;
      const float Y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      const float Z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
      auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
      const float fx = f(X), fy = f(Y), fz = f(Z);
      lab[0] = 116.0f * fy - 16.0f;
      lab[1] = 500.0f * (fx - fy);
      lab[2] = 200.0f * (fy - fz);
   }

   float RangeWeight(Range r, const float rgb[3], const RangeParams& p)
   {
      const float R = Clamp01(rgb[0]), G = Clamp01(rgb[1]), B = Clamp01(rgb[2]);
      switch (r)
      {
         case Range::Reds: case Range::Yellows: case Range::Greens: case Range::Cyans: case Range::Blues: case Range::Magentas:
         {
            const float mx = std::max({ R, G, B }), mn = std::min({ R, G, B }), c = mx - mn;
            if (c <= 1e-6f)
               return 0.0f;
            float hue;
            if (mx == R) hue = 60.0f * std::fmod((G - B) / c + 6.0f, 6.0f);
            else if (mx == G) hue = 60.0f * ((B - R) / c + 2.0f);
            else hue = 60.0f * ((R - G) / c + 4.0f);
            const float centre = 60.0f * ((int)r - (int)Range::Reds);
            float d = std::fabs(hue - centre);
            d = std::min(d, 360.0f - d);
            // Full within 15 degrees, none past 45: neighbouring families share the colours between them.
            const float wh = Clamp01((45.0f - d) / 30.0f);
            // Chroma, not saturation: a dark or dull colour has little of any hue.
            return wh * Clamp01(c / 0.2f);
         }
         case Range::Highlights: case Range::Midtones: case Range::Shadows:
         {
            const float L = 255.0f * (0.299f * R + 0.587f * G + 0.114f * B);
            float lo = p.lo, hi = p.hi;
            if (lo < 0.0f || hi < 0.0f)
            {
               if (r == Range::Shadows) lo = 0.0f, hi = 65.0f;
               else if (r == Range::Midtones) lo = 105.0f, hi = 150.0f;
               else lo = 190.0f, hi = 255.0f;
            }
            if (lo > hi)
               std::swap(lo, hi);
            const float d = L < lo ? lo - L : L > hi ? L - hi : 0.0f;
            const float fall = std::max(0.5f, std::clamp(p.tonalFuzz, 0.0f, 100.0f) * 2.55f);
            return Clamp01(1.0f - d / fall);
         }
         case Range::Skin:
         {
            // A box on the YCbCr skin locus (Chai and Ngan 1999), soft inside its edge so cream and white stay out.
            const float r8 = R * 255.0f, g8 = G * 255.0f, b8 = B * 255.0f;
            const float y = 0.299f * r8 + 0.587f * g8 + 0.114f * b8;
            const float cb = 128.0f - 0.168736f * r8 - 0.331264f * g8 + 0.5f * b8;
            const float cr = 128.0f + 0.5f * r8 - 0.418688f * g8 - 0.081312f * b8;
            auto inBox = [](float v, float lo, float hi) { return Clamp01(std::min(v - lo, hi - v) / 4.0f); };
            const float wy = Clamp01((y - 30.0f) / 15.0f);
            return inBox(cb, 77.0f, 127.0f) * inBox(cr, 133.0f, 173.0f) * wy;
         }
         case Range::Sampled:
            break;
      }
      return 0.0f;
   }

   void ColorRange(const uint8_t* rgba, int w, int h, const std::vector<Sample>& samples, const RangeParams& p,
                   std::vector<uint8_t>& out)
   {
      out.assign((size_t)w * h, 0);
      if (rgba == nullptr || w < 1 || h < 1)
         return;
      if (p.range != Range::Sampled)
      {
         for (size_t i = 0; i < out.size(); i++)
         {
            const float c[3] = { rgba[i * 4] / 255.0f, rgba[i * 4 + 1] / 255.0f, rgba[i * 4 + 2] / 255.0f };
            out[i] = To8(RangeWeight(p.range, c, p));
         }
         return;
      }
      if (samples.empty())
         return;
      struct S { float lab[3]; uint8_t c8[3]; float x, y; bool sub; };
      std::vector<S> ss;
      for (const Sample& s : samples)
      {
         S t;
         Lab(s.rgb, t.lab);
         for (int k = 0; k < 3; k++)
            t.c8[k] = (uint8_t)std::lround(Clamp01(s.rgb[k]) * 255.0f);
         t.x = s.x, t.y = s.y, t.sub = s.subtract;
         ss.push_back(t);
      }
      const float fuzz = std::clamp(p.fuzziness, 0.0f, 200.0f);
      const float reach = std::max(1.0f, std::clamp(p.localRange, 1.0f, 100.0f) / 100.0f * std::max(w, h));
      // Equal colours give equal answers: a small cache of the last colour seen saves the Lab maths on flat areas.
      uint32_t lastKey = 0xffffffffu;
      float lastLab[3] = { 0, 0, 0 };
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            const size_t i = (size_t)y * w + x;
            const uint8_t* px = &rgba[i * 4];
            const uint32_t key = (uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2];
            if (key != lastKey)
            {
               const float c[3] = { px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f };
               Lab(c, lastLab);
               lastKey = key;
            }
            float v = 0.0f;
            for (const S& s : ss)
            {
               float sv;
               if (fuzz <= 0.0f)
                  sv = px[0] == s.c8[0] && px[1] == s.c8[1] && px[2] == s.c8[2] ? 1.0f : 0.0f;
               else
               {
                  const float d0 = lastLab[0] - s.lab[0], d1 = lastLab[1] - s.lab[1], d2 = lastLab[2] - s.lab[2];
                  sv = Clamp01(1.0f - std::sqrt(d0 * d0 + d1 * d1 + d2 * d2) / fuzz);
               }
               if (p.localized && sv > 0.0f)
                  sv *= Clamp01(1.0f - std::hypot(x + 0.5f - s.x, y + 0.5f - s.y) / reach);
               v = s.sub ? std::min(v, 1.0f - sv) : std::max(v, sv);
            }
            out[i] = To8(v);
         }
   }

   // ---- Quick Select ---------------------------------------------------------------------------------------------
   void QuickRegion(const uint8_t* rgba, int w, int h, float cx, float cy, float r, float reach, std::vector<uint8_t>& out,
                    int box[4])
   {
      r = std::max(r, 0.5f);
      const float R = rgba != nullptr ? r * std::max(reach, 1.0f) : r;
      const int x0 = std::max(0, (int)std::floor(cx - R)), y0 = std::max(0, (int)std::floor(cy - R));
      const int x1 = std::min(w, (int)std::ceil(cx + R) + 1), y1 = std::min(h, (int)std::ceil(cy + R) + 1);
      box[0] = x0;
      box[1] = y0;
      box[2] = std::max(0, x1 - x0);
      box[3] = std::max(0, y1 - y0);
      const int bw = box[2], bh = box[3];
      out.assign((size_t)bw * bh, 0);
      if (bw == 0 || bh == 0)
         return;
      auto inDisc = [&](int x, int y) { return std::hypot(x + 0.5f - cx, y + 0.5f - cy) <= r; };
      std::vector<int> queue;
      for (int y = y0; y < y1; y++)
         for (int x = x0; x < x1; x++)
            if (inDisc(x, y))
            {
               out[(size_t)(y - y0) * bw + (x - x0)] = 255;
               queue.push_back((y - y0) * bw + (x - x0));
            }
      if (rgba == nullptr || queue.empty())
         return;
      std::vector<float> lab((size_t)bw * bh * 3);
      for (int y = y0; y < y1; y++)
         for (int x = x0; x < x1; x++)
         {
            const uint8_t* c = rgba + ((size_t)y * w + x) * 4;
            const float rgb[3] = { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f };
            Lab(rgb, &lab[((size_t)(y - y0) * bw + (x - x0)) * 3]);
         }
      double mean[3] = { 0, 0, 0 };
      for (int i : queue)
         for (int k = 0; k < 3; k++)
            mean[k] += lab[(size_t)i * 3 + k];
      for (double& m : mean)
         m /= (double)queue.size();
      double spread = 0.0;
      for (int i : queue)
      {
         const float* l = &lab[(size_t)i * 3];
         spread += std::sqrt((l[0] - mean[0]) * (l[0] - mean[0]) + (l[1] - mean[1]) * (l[1] - mean[1]) + (l[2] - mean[2]) * (l[2] - mean[2]));
      }
      spread /= (double)queue.size();
      const float tol = (float)std::clamp(10.0 + 2.5 * spread, 10.0, 60.0);   // Delta E from the disc's mean
      const float step = std::max(6.0f, tol * 0.6f);                         // the most a neighbour may jump
      auto dist = [&](const float* a, const float* b) {
         return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
      };
      const float m[3] = { (float)mean[0], (float)mean[1], (float)mean[2] };
      const float R2 = R * R;
      for (size_t qi = 0; qi < queue.size(); qi++)
      {
         const int i = queue[qi], x = i % bw, y = i / bw;
         const float* li = &lab[(size_t)i * 3];
         const int nx[4] = { x - 1, x + 1, x, x }, ny[4] = { y, y, y - 1, y + 1 };
         for (int k = 0; k < 4; k++)
         {
            if (nx[k] < 0 || ny[k] < 0 || nx[k] >= bw || ny[k] >= bh)
               continue;
            const int j = ny[k] * bw + nx[k];
            if (out[(size_t)j] != 0)
               continue;
            const float dx = x0 + nx[k] + 0.5f - cx, dy = y0 + ny[k] + 0.5f - cy;
            if (dx * dx + dy * dy > R2)
               continue;
            const float* lj = &lab[(size_t)j * 3];
            if (dist(lj, m) > tol || dist(lj, li) > step)
               continue;
            out[(size_t)j] = 255;
            queue.push_back(j);
         }
      }
   }

   // ---- Remove tool's hole ---------------------------------------------------------------------------------------
   void CloseRings(std::vector<uint8_t>& mask, int w, int h)
   {
      if (w <= 0 || h <= 0 || mask.size() != (size_t)w * h)
         return;
      std::vector<uint8_t> out((size_t)w * h, 0);   // 1 = background reached from the border
      std::vector<int> q;
      auto push = [&](int x, int y) {
         const size_t i = (size_t)y * w + x;
         if (mask[i] == 0 && out[i] == 0)
         {
            out[i] = 1;
            q.push_back((int)i);
         }
      };
      for (int x = 0; x < w; x++)
      {
         push(x, 0);
         push(x, h - 1);
      }
      for (int y = 0; y < h; y++)
      {
         push(0, y);
         push(w - 1, y);
      }
      for (size_t k = 0; k < q.size(); k++)
      {
         const int x = q[k] % w, y = q[k] / w;
         if (x > 0) push(x - 1, y);
         if (x + 1 < w) push(x + 1, y);
         if (y > 0) push(x, y - 1);
         if (y + 1 < h) push(x, y + 1);
      }
      for (size_t i = 0; i < mask.size(); i++)
         if (mask[i] == 0 && out[i] == 0)
            mask[i] = 255;
   }

   namespace
   {
      // The lower envelope of parabolas (Felzenszwalb and Huttenlocher 2012): dd[q] = min_p (q - p)^2 + f[p].
      // v and z are scratch of len and len + 1.
      void Envelope(const float* f, float* dd, int len, int* v, float* z)
      {
         const float inf = 1e20f;
         int k = 0;
         v[0] = 0;
         z[0] = -inf;
         z[1] = inf;
         for (int q = 1; q < len; q++)
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
            z[k + 1] = inf;
         }
         k = 0;
         for (int q = 0; q < len; q++)
         {
            while (z[k + 1] < q)
               k++;
            dd[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]];
         }
      }

      // Squared Euclidean distance to the nearest cell where `seed` is true, on a grid of up to three axes
      // (n[0] fastest). One envelope pass per axis.
      void DistanceSqN(const std::vector<uint8_t>& seed, const int n[3], std::vector<float>& d)
      {
         d.assign(seed.size(), 1e20f);
         for (size_t i = 0; i < d.size(); i++)
            if (seed[i])
               d[i] = 0.0f;
         const int len = std::max(n[0], std::max(n[1], n[2]));
         std::vector<float> f(len), dd(len), z(len + 1);
         std::vector<int> v(len);
         const size_t stride[3] = { 1, (size_t)n[0], (size_t)n[0] * n[1] };
         for (int axis = 0; axis < 3; axis++)
         {
            if (n[axis] < 2)
               continue;
            const size_t lines = d.size() / n[axis], st = stride[axis];
            for (size_t l = 0; l < lines; l++)
            {
               // The line's first cell: l counts the other two axes, fastest first.
               const size_t lo = l % st, hi = l / st, base = hi * st * n[axis] + lo;
               for (int q = 0; q < n[axis]; q++)
                  f[q] = d[base + q * st];
               Envelope(f.data(), dd.data(), n[axis], v.data(), z.data());
               for (int q = 0; q < n[axis]; q++)
                  d[base + q * st] = dd[q];
            }
         }
      }

      // Squared Euclidean distance to the nearest pixel where `seed` is true.
      void DistanceSq(const std::vector<uint8_t>& seed, int w, int h, std::vector<float>& d)
      {
         const int n[3] = { w, h, 1 };
         DistanceSqN(seed, n, d);
      }
   }

   float HoleRadius(const std::vector<uint8_t>& mask, int w, int h)
   {
      if (w <= 0 || h <= 0 || mask.size() != (size_t)w * h)
         return 0.0f;
      std::vector<uint8_t> outside((size_t)w * h);
      bool any = false;
      for (size_t i = 0; i < mask.size(); i++)
      {
         outside[i] = mask[i] == 0;
         any = any || mask[i] != 0;
      }
      if (!any)
         return 0.0f;
      std::vector<float> d;
      DistanceSq(outside, w, h, d);
      float best = 0.0f;
      for (size_t i = 0; i < mask.size(); i++)
         if (mask[i] != 0 && d[i] < 1e19f)
            best = std::max(best, d[i]);
      return best > 0.0f ? std::sqrt(best) : (float)std::max(w, h);   // no outside at all: the whole picture
   }

   void Dilate(std::vector<uint8_t>& mask, int w, int h, float r)
   {
      if (w <= 0 || h <= 0 || mask.size() != (size_t)w * h || !(r > 0.0f))
         return;
      std::vector<uint8_t> in((size_t)w * h);
      for (size_t i = 0; i < mask.size(); i++)
         in[i] = mask[i] != 0;
      std::vector<float> d;
      DistanceSq(in, w, h, d);
      const float r2 = r * r;
      for (size_t i = 0; i < mask.size(); i++)
         if (mask[i] == 0 && d[i] <= r2)
            mask[i] = 255;
   }

   void HarmonicFill(std::vector<float>& img, int ch, const std::vector<uint8_t>& hole, int w, int h)
   {
      if (w <= 0 || h <= 0 || ch <= 0 || hole.size() != (size_t)w * h || img.size() != (size_t)w * h * ch)
         return;
      size_t holes = 0;
      for (uint8_t v : hole)
         holes += v != 0;
      if (holes == 0 || holes == hole.size())
         return;   // nothing to fill, or nothing to fill it from
      if (w > 8 && h > 8)
      {
         // The level below: a cell is known when any of its pixels is, with their mean.
         const int cw = (w + 1) / 2, chh = (h + 1) / 2;
         std::vector<float> cimg((size_t)cw * chh * ch, 0.0f);
         std::vector<uint8_t> chole((size_t)cw * chh, 1);
         std::vector<int> cn((size_t)cw * chh, 0);
         for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
               const size_t i = (size_t)y * w + x;
               if (hole[i])
                  continue;
               const size_t c = (size_t)(y / 2) * cw + x / 2;
               for (int k = 0; k < ch; k++)
                  cimg[c * ch + k] += img[i * ch + k];
               cn[c]++;
            }
         for (size_t c = 0; c < cn.size(); c++)
            if (cn[c] > 0)
            {
               chole[c] = 0;
               for (int k = 0; k < ch; k++)
                  cimg[c * ch + k] /= cn[c];
            }
         HarmonicFill(cimg, ch, chole, cw, chh);
         for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
               const size_t i = (size_t)y * w + x;
               if (!hole[i])
                  continue;
               const float fx = std::clamp((x + 0.5f) / 2.0f - 0.5f, 0.0f, (float)cw - 1), fy = std::clamp((y + 0.5f) / 2.0f - 0.5f, 0.0f, (float)chh - 1);
               const int ix = (int)fx, iy = (int)fy, jx = std::min(cw - 1, ix + 1), jy = std::min(chh - 1, iy + 1);
               const float tx = fx - ix, ty = fy - iy;
               for (int k = 0; k < ch; k++)
               {
                  auto at = [&](int xx, int yy) { return cimg[((size_t)yy * cw + xx) * ch + k]; };
                  img[i * ch + k] = (at(ix, iy) * (1 - tx) + at(jx, iy) * tx) * (1 - ty) + (at(ix, jy) * (1 - tx) + at(jx, jy) * tx) * ty;
               }
            }
      }
      else
      {
         std::vector<double> mean(ch, 0.0);
         for (size_t i = 0; i < hole.size(); i++)
            if (!hole[i])
               for (int k = 0; k < ch; k++)
                  mean[k] += img[i * ch + k];
         for (size_t i = 0; i < hole.size(); i++)
            if (hole[i])
               for (int k = 0; k < ch; k++)
                  img[i * ch + k] = (float)(mean[k] / (double)(hole.size() - holes));
      }
      // Red-black over-relaxation on the hole only; image edges are mirror (no flow out).
      std::vector<int> idx;
      idx.reserve(holes);
      for (size_t i = 0; i < hole.size(); i++)
         if (hole[i])
            idx.push_back((int)i);
      const float omega = 1.8f;
      for (int it = 0; it < 120; it++)
      {
         float worst = 0.0f;
         for (int colour = 0; colour < 2; colour++)
            for (int i : idx)
            {
               const int x = i % w, y = i / w;
               if (((x + y) & 1) != colour)
                  continue;
               int nb[4], n = 0;
               if (x > 0) nb[n++] = i - 1;
               if (x + 1 < w) nb[n++] = i + 1;
               if (y > 0) nb[n++] = i - w;
               if (y + 1 < h) nb[n++] = i + w;
               for (int k = 0; k < ch; k++)
               {
                  float s = 0.0f;
                  for (int q = 0; q < n; q++)
                     s += img[(size_t)nb[q] * ch + k];
                  float& v = img[(size_t)i * ch + k];
                  const float d = omega * (s / n - v);
                  v += d;
                  worst = std::max(worst, std::fabs(d));
               }
            }
         if (worst < 1e-5f)
            break;
      }
   }

   // ---- Grow / Similar ------------------------------------------------------------------------------------------
   void GrowMatch(const uint8_t* rgba, int w, int h, const std::vector<uint8_t>& sel, int tol, bool contiguous,
                  std::vector<uint8_t>& out)
   {
      out = sel;
      if (rgba == nullptr || w <= 0 || h <= 0 || sel.size() != (size_t)w * h)
         return;
      const size_t n = sel.size();
      auto pack = [&](size_t i) {
         const uint8_t* c = &rgba[i * 4];
         return (uint32_t)c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
      };
      // The selected colours, each once, binned on a 64^3 RGB grid (4 levels a cell) by a counting sort.
      constexpr int kN = 64, kCell = 256 / kN;
      auto bin = [](uint32_t c) {
         return (int)((c & 0xFF) / kCell) + kN * ((int)((c >> 8 & 0xFF) / kCell) + kN * (int)((c >> 16 & 0xFF) / kCell));
      };
      std::vector<uint32_t> seeds;
      for (size_t i = 0; i < n; i++)
         if (sel[i] >= 128)
            seeds.push_back(pack(i));
      if (seeds.empty())
         return;
      std::sort(seeds.begin(), seeds.end());
      seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
      bool clearSeed = false, allOpaque = true;
      std::vector<int> start(kN * kN * kN + 1, 0);
      for (uint32_t c : seeds)
      {
         clearSeed = clearSeed || (c >> 24) == 0;
         allOpaque = allOpaque && (c >> 24) == 255;
         start[bin(c) + 1]++;
      }
      for (size_t b = 1; b < start.size(); b++)
         start[b] += start[b - 1];
      std::vector<uint32_t> flat(seeds.size());
      {
         std::vector<int> at(start.begin(), start.end() - 1);
         for (uint32_t c : seeds)
            flat[at[bin(c)]++] = c;
      }
      std::vector<uint8_t> occ(kN * kN * kN, 0);
      std::vector<int> used;
      for (int b = 0; b < kN * kN * kN; b++)
         if (start[b + 1] > start[b])
         {
            occ[b] = 1;
            used.push_back(b);
         }
      std::vector<float> d2;
      const int dims[3] = { kN, kN, kN };
      DistanceSqN(occ, dims, d2);
      // A cell k cells away holds colours at least kCell k - (kCell - 1) sqrt 3 and at most kCell k + (kCell - 1) sqrt 3 off.
      const float r = (float)std::clamp(tol, 0, 255) * 1.7320508f, r2 = r * r, slack = (kCell - 1) * 1.7320508f;
      auto exact = [&](uint32_t q) {
         const int qb = bin(q), qx = qb % kN, qy = qb / kN % kN, qz = qb / (kN * kN);
         for (int b : used)
         {
            const int dx = b % kN - qx, dy = b / kN % kN - qy, dz = b / (kN * kN) - qz;
            const float k = std::sqrt((float)(dx * dx + dy * dy + dz * dz));
            if (kCell * k - slack > r)
               continue;
            for (int j = start[b]; j < start[b + 1]; j++)
            {
               const uint32_t c = flat[j];
               int s = 0;
               for (int ch = 0; ch < 4; ch++)
               {
                  const int dd = (int)(q >> (8 * ch) & 0xFF) - (int)(c >> (8 * ch) & 0xFF);
                  s += dd * dd;
               }
               if ((float)s <= r2)
                  return true;
            }
         }
         return false;
      };
      auto match = [&](uint32_t q) {
         if ((q >> 24) == 0 && clearSeed)
            return true;   // two clear pixels are alike, whatever RGB they hide (the Magic Wand's rule)
         const float k = std::sqrt(d2[bin(q)]);
         if (kCell * k - slack > r)
            return false;
         if (allOpaque && (q >> 24) == 255 && kCell * k + slack <= r)
            return true;
         return exact(q);
      };
      // Each colour is tested once.
      std::vector<uint32_t> keys;
      std::vector<uint8_t> verdict;
      std::vector<uint8_t> ok(n, 0);
      {
         std::vector<uint32_t> all(n);
         for (size_t i = 0; i < n; i++)
            all[i] = pack(i);
         keys = all;
         std::sort(keys.begin(), keys.end());
         keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
         verdict.resize(keys.size());
         for (size_t k = 0; k < keys.size(); k++)
            verdict[k] = match(keys[k]) ? 1 : 0;
         for (size_t i = 0; i < n; i++)
            ok[i] = verdict[(size_t)(std::lower_bound(keys.begin(), keys.end(), all[i]) - keys.begin())];
      }
      if (!contiguous)
      {
         for (size_t i = 0; i < n; i++)
            if (ok[i])
               out[i] = 255;
         return;
      }
      std::vector<int> q;
      for (size_t i = 0; i < n; i++)
         if (sel[i] >= 128)
         {
            out[i] = 255;
            q.push_back((int)i);
         }
      auto push = [&](int j) {
         if (ok[j] && out[j] != 255)
         {
            out[j] = 255;
            q.push_back(j);
         }
      };
      for (size_t k = 0; k < q.size(); k++)
      {
         const int x = q[k] % w, y = q[k] / w;
         if (x > 0) push(q[k] - 1);
         if (x + 1 < w) push(q[k] + 1);
         if (y > 0) push(q[k] - w);
         if (y + 1 < h) push(q[k] + w);
      }
   }

   // ---- Focus Area -----------------------------------------------------------------------------------------------
   void FocusMap(const uint8_t* rgba, int w, int h, float noise, std::vector<float>& out)
   {
      out.assign((size_t)std::max(0, w) * std::max(0, h), 0.0f);
      if (rgba == nullptr || w < 3 || h < 3)
         return;
      const size_t n = (size_t)w * h;
      std::vector<float> y(n), lap(n, 0.0f);
      for (size_t i = 0; i < n; i++)
         y[i] = 0.2126f * rgba[i * 4] + 0.7152f * rgba[i * 4 + 1] + 0.0722f * rgba[i * 4 + 2];
      for (int r = 1; r + 1 < h; r++)
         for (int x = 1; x + 1 < w; x++)
         {
            const size_t i = (size_t)r * w + x;
            lap[i] = std::fabs(4.0f * y[i] - y[i - 1] - y[i + 1] - y[i - w] - y[i + w]);
         }
      // The local mean over a window about 1/100 of the long side: a region reads as sharp by the detail in it.
      const int win = std::max(2, std::max(w, h) / 100);
      BoxMean(lap, out, w, h, win);
      std::vector<float> sorted(out);
      const size_t at = (size_t)((sorted.size() - 1) * 0.99);
      std::nth_element(sorted.begin(), sorted.begin() + at, sorted.end());
      const float top = sorted[at];
      if (!(top > 1e-6f))
      {
         std::fill(out.begin(), out.end(), 0.0f);
         return;
      }
      const float floor = Clamp01(noise) * top;
      const float span = std::max(top - floor, 1e-6f);
      for (float& v : out)
         v = Clamp01((v - floor) / span);
   }

   void FocusSelect(const std::vector<float>& map, int w, int h, float range, std::vector<uint8_t>& out)
   {
      out.assign((size_t)std::max(0, w) * std::max(0, h), 0);
      if (map.size() != out.size())
         return;
      // Range 0 keeps only the sharpest detail, 1 everything with any; the edge falls off over 0.1.
      const float t = 1.0f - Clamp01(range), soft = 0.1f;
      for (size_t i = 0; i < out.size(); i++)
         out[i] = range >= 1.0f ? 255 : To8((map[i] - t) / soft + 0.5f);
   }

   // ---- Depth --------------------------------------------------------------------------------------------------
   void DepthSelect(const std::vector<float>& depth, int w, int h, float lo, float hi, float soft, std::vector<uint8_t>& out)
   {
      out.assign((size_t)w * h, 0);
      if (depth.size() != out.size())
         return;
      if (lo > hi)
         std::swap(lo, hi);
      const float f = std::max(soft, 1e-4f);
      for (size_t i = 0; i < out.size(); i++)
      {
         const float d = depth[i];
         const float off = d < lo ? lo - d : d > hi ? d - hi : 0.0f;
         out[i] = To8(1.0f - off / f);
      }
   }
}
