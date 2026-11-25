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
