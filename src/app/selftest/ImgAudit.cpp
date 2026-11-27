// INFINITE_IMGAUDIT: a measurement audit of every 2D image effect.
//
// The image-side twin of FXAUDIT. Every FilterDef runs through the real
// FilterNode (and Curves / Blend through their real nodes) on synthetic float
// inputs with known answers, so "is this industry grade" is a table, not an
// opinion. Reference standards are named next to each check:
//
//   fmt     internal format of the node's output (Nuke/Resolve work in float)
//   hdr     largest value out for a flat 2.0 input (float headroom survives?)
//   dark    distinct output levels over a 0..0.1 float ramp, 1024 px
//           (8-bit gives ~26; a float chain keeps hundreds) - banding
//   bleed   visible (premultiplied) change in output when only the colour
//           hidden under alpha 0 changes - should be 0 (Porter-Duff 1984)
//   id      max |out - in| at the node's own defaults (informative only)
//   gpu     ms per cook at 1920x1080 (A/B the cost of linear light in one
//           binary: INFINITE_IMGAUDIT_LEGACY=1 INFINITE_IMAGE_RGBA8=1 is the
//           old 8-bit gamma-space chain)
//
// plus SPEC lines: per-node truths against the published formula - sRGB
// transfer (IEC 61966-2-1), exposure in stops of linear light, Rec.709 luma,
// blur sigma / ripple / energy / linear-light edge, LUT and palette
// identity, premultiply round trip, blend modes against the W3C Compositing
// and Blending Level 1 formulas, and a 10-node grading chain.
//
// Only hard invariants fail the run (a node that renders nothing, or any
// non-finite output). Everything else is reported, with FLAG lines past the
// thresholds below, for a person to triage - and so a fixed baseline can be
// recorded once and compared against (docs/plans/image-fx/README.md).
#include "app/AppShared.h"
#include "nodes/FilterNode.h"
#include "nodes/BlendNode.h"
#include "nodes/CurvesNode.h"
#include "core/BlendModes.h"
#include "core/FilterDefs.h"
#include "core/GLUtil.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <set>

namespace app
{
namespace
{
   // Thresholds for FLAG lines (reported, not failed).
   constexpr float kOneCode = 1.0f / 255.0f;
   constexpr int kFlagDarkLevels = 100;   // a float chain keeps >> 100 here
   constexpr float kFlagBleed = 0.02f;
   constexpr float kInputTol = 1.0f / 4096.0f; // relative input error a GPU decode may carry
   constexpr float kFlagGpuMs = 2.0f;
   constexpr float kFlagSpecErr = 2.0f * kOneCode;

   struct Flags
   {
      int count = 0;
      void Add(const std::string& who, const char* what, const char* fmt, double v)
      {
         char buf[96];
         std::snprintf(buf, sizeof(buf), fmt, v);
         printf("IMGAUDIT FLAG %-18s %-10s %s\n", who.c_str(), what, buf);
         count++;
      }
   };

   // ---------------------------------------------------------------- colour maths
   float SrgbToLinear(float v)
   {
      return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
   }
   float LinearToSrgb(float v)
   {
      return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
   }
   float Luma709(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

   // ---------------------------------------------------------------- image + source
   struct Image
   {
      int w = 0, h = 0;
      std::vector<float> px; // RGBA, row 0 = bottom (GL order)
      Image() = default;
      Image(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0.0f) {}
      float* At(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
      const float* At(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
      void Fill(float r, float g, float b, float a)
      {
         for (size_t i = 0; i < px.size(); i += 4)
         {
            px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a;
         }
      }
   };

   // A float texture standing in for an upstream node. Each Set() bumps the
   // revision so a downstream FilterNode's cache signature always re-renders.
   class TexSource : public INode
   {
   public:
      ~TexSource() override
      {
         if (mTex != 0)
            glDeleteTextures(1, &mTex);
      }
      void Set(const Image& img)
      {
         if (mTex == 0)
            glGenTextures(1, &mTex);
         glBindTexture(GL_TEXTURE_2D, mTex);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, img.w, img.h, 0, GL_RGBA, GL_FLOAT, img.px.data());
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glBindTexture(GL_TEXTURE_2D, 0);
         mW = img.w;
         mH = img.h;
         mRev = NextTextureRevision();
      }
      void Touch() { mRev = NextTextureRevision(); }
      unsigned int GetOutputTexture() override { return mTex; }
      int GetOutputWidth() const override { return mW; }
      int GetOutputHeight() const override { return mH; }
      void CookIfNeeded(int) override {}
      unsigned long long TextureRevision() const override { return mRev; }

   private:
      unsigned int mTex = 0;
      int mW = 0, mH = 0;
      unsigned long long mRev = 0;
   };

   int gFrame = 1000;
   unsigned int gReadFbo = 0;

   bool Read(INode& n, Image& out)
   {
      n.CookIfNeeded(++gFrame);
      const unsigned int tex = n.GetOutputTexture();
      const int w = n.GetOutputWidth(), h = n.GetOutputHeight();
      if (tex == 0 || w <= 0 || h <= 0)
         return false;
      out = Image(w, h);
      return GLUtil::ReadTexturePixels(gReadFbo, tex, w, h, out.px);
   }

   GLint OutputFormat(INode& n)
   {
      GLint fmt = 0;
      glBindTexture(GL_TEXTURE_2D, n.GetOutputTexture());
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
      glBindTexture(GL_TEXTURE_2D, 0);
      return fmt;
   }

   const char* FormatName(GLint f)
   {
      switch (f)
      {
         case GL_RGBA8: return "RGBA8";
         case GL_RGBA16F: return "RGBA16F";
         case GL_RGBA32F: return "RGBA32F";
         default: return "other";
      }
   }

   // INFINITE_IMGAUDIT_LEGACY=1: every node as an old patch loads it
   // (BeforeLoadParams), for same-binary A/B of the legacy look and timing.
   bool LegacyLook()
   {
      static const bool legacy = std::getenv("INFINITE_IMGAUDIT_LEGACY") != nullptr;
      return legacy;
   }

   // ---------------------------------------------------------------- filter rig
   struct FilterRig
   {
      std::unique_ptr<FilterNode> node;
      TexSource src, src2;

      explicit FilterRig(const FilterDef& def) : node(std::make_unique<FilterNode>(def))
      {
         node->Input().Connect(&src);
         if (def.inputs > 1)
            node->Input2().Connect(&src2);
         if (LegacyLook())
            node->BeforeLoadParams();
      }
      bool Set(const char* uniform, float v, int comp = 0)
      {
         const auto& ps = node->Def().params;
         for (size_t i = 0; i < ps.size(); i++)
            if (ps[i].uniformName == uniform)
            {
               node->SetParamValue(i, comp, v);
               return true;
            }
         return false;
      }
      bool Run(const Image& in, Image& out, const Image* in2 = nullptr)
      {
         src.Set(in);
         src2.Set(in2 ? *in2 : in);
         return Read(*node, out);
      }
   };

   const FilterDef* FindDef(const std::string& name)
   {
      for (const FilterDef& d : GetFilterDefs())
         if (d.name == name)
            return &d;
      return nullptr;
   }

   // ---------------------------------------------------------------- test images
   Image DarkRamp() // 0..0.1 float ramp, 1024 x 4
   {
      Image img(1024, 4);
      for (int y = 0; y < img.h; y++)
         for (int x = 0; x < img.w; x++)
         {
            float v = 0.1f * (float)x / (float)(img.w - 1);
            float* p = img.At(x, y);
            p[0] = p[1] = p[2] = v; p[3] = 1.0f;
         }
      return img;
   }

   Image FullRamp(int w = 1024)
   {
      Image img(w, 4);
      for (int y = 0; y < img.h; y++)
         for (int x = 0; x < img.w; x++)
         {
            float v = (float)x / (float)(img.w - 1);
            float* p = img.At(x, y);
            p[0] = p[1] = p[2] = v; p[3] = 1.0f;
         }
      return img;
   }

   Image RandomColours(int w, int h, unsigned seed, float aLo = 1.0f, float aHi = 1.0f)
   {
      std::mt19937 rng(seed);
      std::uniform_real_distribution<float> u(0.0f, 1.0f), ua(aLo, aHi);
      Image img(w, h);
      for (size_t i = 0; i < img.px.size(); i += 4)
      {
         img.px[i] = u(rng); img.px[i + 1] = u(rng); img.px[i + 2] = u(rng);
         img.px[i + 3] = ua(rng);
      }
      return img;
   }

   int DistinctLevels(const Image& img, int row, int x0, int x1)
   {
      std::set<long> seen;
      for (int x = x0; x < x1; x++)
         seen.insert(std::lround(img.At(x, row)[0] * 1.0e6));
      return (int)seen.size();
   }

   float MaxAbsDiffRgb(const Image& a, const Image& b)
   {
      if (a.w != b.w || a.h != b.h)
         return 1.0e9f;
      float m = 0.0f;
      for (size_t i = 0; i < a.px.size(); i++)
         if (i % 4 != 3)
            m = std::max(m, std::fabs(a.px[i] - b.px[i]));
      return m;
   }

   long NonFinite(const Image& img)
   {
      long n = 0;
      for (float v : img.px)
         if (!std::isfinite(v))
            n++;
      return n;
   }

   // ---------------------------------------------------------------- blur profile
   struct Profile
   {
      float sigma = 0.0f, energy = 0.0f;
      int ripples = 0;
   };

   // Horizontal profile through a 1 px vertical white line at x = w/2.
   Profile MeasureLine(FilterRig& rig, int w = 257)
   {
      Image in(w, 9), out;
      for (int y = 0; y < in.h; y++)
         for (int x = 0; x < in.w; x++)
         {
            float v = x == w / 2 ? 1.0f : 0.0f;
            float* p = in.At(x, y);
            p[0] = p[1] = p[2] = v; p[3] = 1.0f;
         }
      Profile pr;
      if (!rig.Run(in, out))
         return pr;
      double sum = 0, m2 = 0;
      std::vector<float> v(w);
      for (int x = 0; x < w; x++)
      {
         v[x] = SrgbToLinear(out.At(x, 4)[0]); // energy is light: measure it linear
         sum += v[x];
         m2 += (double)v[x] * (x - w / 2) * (x - w / 2);
      }
      pr.energy = (float)sum;
      pr.sigma = sum > 0 ? (float)std::sqrt(m2 / sum) : 0.0f;
      for (int x = 1; x + 1 < w; x++)
         if (v[x] < v[x - 1] && v[x] < v[x + 1])
            pr.ripples++;
      return pr;
   }

   // Red | green hard edge, blurred. A blur in linear light never dips below
   // the darker side's luminance; one done on encoded values goes darker at
   // the seam (the "dark halo" of gamma-space compositing). Returns the
   // minimum linear luminance across the seam / red's luminance.
   float EdgeDip(FilterRig& rig)
   {
      Image in(256, 8), out;
      for (int y = 0; y < in.h; y++)
         for (int x = 0; x < in.w; x++)
         {
            float* p = in.At(x, y);
            bool left = x < in.w / 2;
            p[0] = left ? 1.0f : 0.0f; p[1] = left ? 0.0f : 1.0f; p[2] = 0.0f; p[3] = 1.0f;
         }
      if (!rig.Run(in, out))
         return 0.0f;
      float minY = 1.0e9f;
      for (int x = 0; x < out.w; x++)
      {
         const float* p = out.At(x, 4);
         minY = std::min(minY, Luma709(SrgbToLinear(p[0]), SrgbToLinear(p[1]), SrgbToLinear(p[2])));
      }
      return minY / 0.2126f;
   }

   // ---------------------------------------------------------------- blend reference
   // W3C Compositing and Blending Level 1 (separable + non-separable modes),
   // Photoshop's documented formulas for the modes W3C doesn't define.
   // a = backdrop, b = source.
   float RefSep(int mode, float a, float b)
   {
      auto burn = [](float cb, float cs) {
         if (cb >= 1.0f) return 1.0f;
         if (cs <= 0.0f) return 0.0f;
         return 1.0f - std::min(1.0f, (1.0f - cb) / cs);
      };
      auto dodge = [](float cb, float cs) {
         if (cb <= 0.0f) return 0.0f;
         if (cs >= 1.0f) return 1.0f;
         return std::min(1.0f, cb / (1.0f - cs));
      };
      auto hard = [](float cb, float cs) {
         return cs <= 0.5f ? cb * 2.0f * cs : 1.0f - (1.0f - cb) * (1.0f - (2.0f * cs - 1.0f));
      };
      switch (mode)
      {
         case 1: return std::min(a, b);
         case 2: return a * b;
         case 3: return burn(a, b);
         case 4: return std::max(0.0f, a + b - 1.0f);
         case 6: return std::max(a, b);
         case 7: return a + b - a * b;
         case 8: return dodge(a, b);
         case 9: return std::min(1.0f, a + b);
         case 11: return hard(b, a); // overlay = hard light with layers swapped
         case 12:
         {
            float d = a <= 0.25f ? ((16.0f * a - 12.0f) * a + 4.0f) * a : std::sqrt(a);
            return b <= 0.5f ? a - (1.0f - 2.0f * b) * a * (1.0f - a) : a + (2.0f * b - 1.0f) * (d - a);
         }
         case 13: return hard(a, b);
         case 14: return b <= 0.5f ? burn(a, 2.0f * b) : dodge(a, 2.0f * (b - 0.5f));
         case 15: return std::clamp(a + 2.0f * b - 1.0f, 0.0f, 1.0f);
         case 16: return b <= 0.5f ? std::min(a, 2.0f * b) : std::max(a, 2.0f * b - 1.0f);
         case 17: return a + b >= 1.0f ? 1.0f : 0.0f;
         case 18: return std::fabs(a - b);
         case 19: return a + b - 2.0f * a * b;
         case 20: return std::max(0.0f, a - b);
         case 21: return b <= 0.0f ? (a > 0.0f ? 1.0f : 0.0f) : std::min(1.0f, a / b);
         case 26: return 0.5f * (a + b);
         case 27: return 1.0f - std::fabs(1.0f - a - b);
         case 28: return b >= 1.0f ? 1.0f : std::min(1.0f, a * a / (1.0f - b));
         case 29: return a >= 1.0f ? 1.0f : std::min(1.0f, b * b / (1.0f - a));
         default: return b;
      }
   }

   struct V3 { float r, g, b; };
   float Lum(V3 c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }
   V3 ClipColor(V3 c)
   {
      float l = Lum(c), n = std::min({ c.r, c.g, c.b }), x = std::max({ c.r, c.g, c.b });
      if (n < 0.0f) c = { l + (c.r - l) * l / (l - n), l + (c.g - l) * l / (l - n), l + (c.b - l) * l / (l - n) };
      if (x > 1.0f) c = { l + (c.r - l) * (1 - l) / (x - l), l + (c.g - l) * (1 - l) / (x - l), l + (c.b - l) * (1 - l) / (x - l) };
      return c;
   }
   V3 SetLum(V3 c, float l)
   {
      float d = l - Lum(c);
      return ClipColor({ c.r + d, c.g + d, c.b + d });
   }
   float Sat(V3 c) { return std::max({ c.r, c.g, c.b }) - std::min({ c.r, c.g, c.b }); }
   V3 SetSat(V3 c, float s)
   {
      float mn = std::min({ c.r, c.g, c.b }), mx = std::max({ c.r, c.g, c.b });
      if (mx <= mn) return { 0, 0, 0 };
      return { (c.r - mn) * s / (mx - mn), (c.g - mn) * s / (mx - mn), (c.b - mn) * s / (mx - mn) };
   }

   V3 RefBlend(int mode, V3 a, V3 b)
   {
      switch (mode)
      {
         case 5: return Lum(a) <= Lum(b) ? a : b;
         case 10: return Lum(a) >= Lum(b) ? a : b;
         case 22: return SetLum(SetSat(b, Sat(a)), Lum(a));
         case 23: return SetLum(SetSat(a, Sat(b)), Lum(a));
         case 24: return SetLum(b, Lum(a));
         case 25: return SetLum(a, Lum(b));
         default: return { RefSep(mode, a.r, b.r), RefSep(mode, a.g, b.g), RefSep(mode, a.b, b.b) };
      }
   }

   // ---------------------------------------------------------------- the audit
   void AuditFilters(Flags& flag, bool& ok, int& audited)
   {
      const Image dark = DarkRamp();
      Image hdr(64, 64);
      hdr.Fill(2.0f, 2.0f, 2.0f, 1.0f);
      // Left half opaque red, right half transparent with green under alpha 0.
      Image bleedIn(128, 32);
      for (int y = 0; y < bleedIn.h; y++)
         for (int x = 0; x < bleedIn.w; x++)
         {
            float* p = bleedIn.At(x, y);
            if (x < bleedIn.w / 2) { p[0] = 1; p[1] = 0; p[2] = 0; p[3] = 1; }
            else { p[0] = 0; p[1] = 1; p[2] = 0; p[3] = 0; }
         }
      Image bleedCtrl = bleedIn;
      for (int y = 0; y < bleedCtrl.h; y++)
         for (int x = bleedCtrl.w / 2; x < bleedCtrl.w; x++)
            bleedCtrl.At(x, y)[1] = 0.0f;
      const Image photo = RandomColours(256, 256, 7);
      Image stress(64, 64);
      {
         const float vals[] = { -1.0f, 0.0f, 1.0f, 4.0f, 1.0e4f, 0.5f };
         for (int y = 0; y < stress.h; y++)
            for (int x = 0; x < stress.w; x++)
            {
               float* p = stress.At(x, y);
               p[0] = vals[(x + y) % 6]; p[1] = vals[(x * 3 + y) % 6]; p[2] = vals[(x + 5 * y) % 6];
               p[3] = (x % 4 == 0) ? 0.0f : 1.0f;
            }
      }
      Image hd = RandomColours(1920, 1080, 11);

      for (const FilterDef& def : GetFilterDefs())
      {
         FilterRig rig(def);
         Image out;
         if (!rig.Run(photo, out))
         {
            printf("IMGAUDIT %-18s FAIL (no output - shader or FBO failed)\n", def.name.c_str());
            ok = false;
            continue;
         }
         audited++;
         const char* fmt = FormatName(OutputFormat(*rig.node));
         const float idErr = MaxAbsDiffRgb(out, photo);

         Image o2;
         rig.Run(hdr, o2);
         float hdrMax = 0.0f;
         for (size_t i = 0; i < o2.px.size(); i++)
            if (i % 4 != 3)
               hdrMax = std::max(hdrMax, o2.px[i]);

         rig.Run(dark, o2);
         const int darkLv = DistinctLevels(o2, 2, 0, o2.w);

         // Run twice, changing only the colour hidden under alpha 0. Any
         // difference in covered pixels is colour leaking from where it
         // should not exist - independent of what the filter does to colour.
         Image ctrl;
         rig.Run(bleedIn, o2);
         rig.Run(bleedCtrl, ctrl);
         float bleed = 0.0f;
         for (int y = 0; y < o2.h; y++)
            for (int x = 0; x < o2.w; x++)
            {
               const float* p = o2.At(x, y);
               const float* q = ctrl.At(x, y);
               // premultiplied: what is actually visible once composited
               for (int k = 0; k < 3; k++)
                  bleed = std::max(bleed, std::fabs(p[k] * p[3] - q[k] * q[3]));
            }

         long nonFinite = 0;
         rig.Run(stress, o2);
         nonFinite += NonFinite(o2);

         // GPU: 20 cooks at 1080p, each forced by a new upstream revision.
         rig.src.Set(hd);
         rig.src2.Set(hd);
         Read(*rig.node, o2);
         glFinish();
         auto t0 = std::chrono::steady_clock::now();
         for (int i = 0; i < 20; i++)
         {
            rig.src.Touch();
            rig.node->CookIfNeeded(++gFrame);
         }
         glFinish();
         const float gpuMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20.0f;

         printf("IMGAUDIT %-18s fmt %-7s | hdr %5.2f | dark %4d | bleed %4.2f | id %5.3f | gpu %5.2f ms\n",
                def.name.c_str(), fmt, hdrMax, darkLv, bleed, idErr, gpuMs);
         if (nonFinite > 0)
         {
            printf("IMGAUDIT %-18s FAIL (non-finite %ld)\n", def.name.c_str(), nonFinite);
            ok = false;
         }
         if (darkLv < kFlagDarkLevels && idErr < 0.5f)
            flag.Add(def.name, "dark", "%.0f levels in 0..0.1 (banding)", darkLv);
         // Expected bleed: the alpha filters make hidden colour visible by definition.
         static const char* kBleedByDesign[] = { "set alpha", "alpha invert", "alpha from luma" };
         bool bleedByDesign = false;
         for (const char* n : kBleedByDesign)
            bleedByDesign = bleedByDesign || def.name == n;
         if (bleed > kFlagBleed && !bleedByDesign)
            flag.Add(def.name, "bleed", "%.2f colour leaks from alpha-0 pixels", bleed);
         if (gpuMs > kFlagGpuMs)
            flag.Add(def.name, "gpu", "%.2f ms at 1080p", gpuMs);
      }
   }

   void Spec(Flags& flag, const char* who, const char* what, float err, float limit, const char* note)
   {
      printf("IMGAUDIT SPEC %-18s %-28s err %.4f (%s)\n", who, what, err, note);
      if (err > limit)
         flag.Add(who, "spec", "%.4f", err);
   }

   // PhotoCraft parity (2026-11-27). Shape Blur's binary kernel (crates/algo/src/blur2/shape_tests.rs) and Unsharp Mask's
   // Gaussian, halo and threshold rule (crates/algo/src/sharpen/tests.rs), against a CPU port of PhotoCraft's reference.
   void AuditParity(Flags& flag)
   {
      if (const FilterDef* d = FindDef("shape blur"))
      {
         struct Want { const char* name; int shape; float radius; int count; };
         static const Want kWant[] = { { "circle r3", 0, 3.0f, 29 }, { "ring r3", 5, 3.0f, 20 }, { "square r3", 4, 3.0f, 49 },
                                       { "square r2.5", 4, 2.5f, 49 }, { "diamond r3", 6, 3.0f, 25 },
                                       { "triangle r3", 7, 3.0f, 18 }, { "cross r3", 8, 3.0f, 13 } };
         Image dot(15, 15);
         float* c = dot.At(7, 7);
         c[0] = c[1] = c[2] = c[3] = 1.0f;
         for (const Want& w : kWant)
         {
            FilterRig r(*d);
            r.Set("uLinear", 0.0f);
            r.Set("uShape", (float)w.shape);
            r.Set("uRadius", w.radius);
            Image out;
            int n = 0;
            float peak = 0.0f;
            if (r.Run(dot, out))
               for (int y = 0; y < out.h; y++)
                  for (int x = 0; x < out.w; x++)
                     if (out.At(x, y)[3] > 1e-4f)
                     {
                        n++;
                        peak = std::max(peak, out.At(x, y)[3]);
                     }
            const std::string what = std::string("kernel ") + w.name;
            printf("IMGAUDIT PARITY shape blur %-12s covers %d px (want %d), weight %.4f\n", w.name, n, w.count, peak);
            Spec(flag, "shape blur", what.c_str(), (float)std::abs(n - w.count) + std::abs(peak - 1.0f / (float)w.count),
                 0.002f, "pixel count and 1/count weight");
         }
      }
      if (const FilterDef* d = FindDef("unsharpmask"))
      {
         // A soft step 0.25 | 0.75 at x = 30, rows identical, so only the horizontal kernel matters.
         const int W = 64, H = 5;
         auto Edge = [&](float alpha) {
            Image im(W, H);
            for (int y = 0; y < H; y++)
               for (int x = 0; x < W; x++)
               {
                  float* p = im.At(x, y);
                  p[0] = p[1] = p[2] = x >= 30 ? 0.75f : 0.25f;
                  p[3] = alpha;
               }
            return im;
         };
         auto Blur = [&](const Image& im, int x, float sigma) {
            if (sigma < 0.05f)
               return im.At(x, 0)[0];
            const int r = (int)std::ceil(sigma * 3.0f);
            double s = 0.0, t = 0.0;
            for (int i = -r; i <= r; i++)
            {
               const double w = std::exp(-(double)(i * i) / (2.0 * sigma * sigma));
               s += w * im.At(std::clamp(x + i, 0, W - 1), 0)[0];
               t += w;
            }
            return (float)(s / t);
         };
         for (float alpha : { 1.0f, 0.7f })
            for (float radius : { 0.6f, 3.25f })
            {
               const Image in = Edge(alpha);
               FilterRig r(*d);
               r.Set("uAmount", 3.0f);
               r.Set("uRadius", radius);
               Image out;
               float err = 1.0f, halo = 0.0f;
               if (r.Run(in, out))
               {
                  err = 0.0f;
                  for (int x = 12; x < W - 12; x++)
                  {
                     // Every shader's colour is floored at 0 (GLUtil's output wrap keeps headroom above 1, never below 0).
                     const float o = in.At(x, 0)[0], want = std::max(0.0f, o + 3.0f * (o - Blur(in, x, radius)));
                     err = std::max(err, std::abs(out.At(x, 2)[0] - want));
                     err = std::max(err, std::abs(out.At(x, 2)[3] - alpha));
                     halo = std::max(halo, out.At(x, 2)[0]);
                  }
               }
               char what[64];
               std::snprintf(what, sizeof(what), "halo a%.1f r%.2f", alpha, radius);
               printf("IMGAUDIT PARITY unsharp %s err %.5f peak %.3f\n", what, err, halo);
               // The blur pass is half float (a step of 1/2048 at 0.5..1), and the amount of 3 multiplies its rounding by 4.
               Spec(flag, "unsharpmask", what, err, 4e-3f, "PhotoCraft o + k(o - gauss), floored at 0");
            }
         {
            // Threshold: a channel sharpens only where |o - blur| reaches threshold / 255. Columns within 0.004 of the
            // boundary are skipped (the pass texture is half float).
            const Image in = Edge(1.0f);
            const float radius = 3.25f, k = 2.0f;
            const float level = std::abs(0.25f - Blur(in, 26, radius)) * 255.0f;
            FilterRig r(*d);
            r.Set("uAmount", k);
            r.Set("uRadius", radius);
            r.Set("uThreshold", level);
            Image out;
            float err = 1.0f;
            int on = 0, off = 0;
            if (r.Run(in, out))
            {
               err = 0.0f;
               for (int x = 12; x < W - 12; x++)
               {
                  const float o = in.At(x, 0)[0], dd = o - Blur(in, x, radius);
                  if (std::abs(std::abs(dd) * 255.0f - level) < 0.004f * 255.0f)
                     continue;
                  const bool sharp = std::abs(dd) + 1e-6f >= level / 255.0f;
                  (sharp ? on : off)++;
                  err = std::max(err, std::abs(out.At(x, 2)[0] - (sharp ? std::max(0.0f, o + k * dd) : o)));
               }
            }
            printf("IMGAUDIT PARITY unsharp threshold %.2f levels: %d sharpened, %d kept, err %.5f\n", level, on, off, err);
            Spec(flag, "unsharpmask", "threshold boundary", err + (on > 0 && off > 0 ? 0.0f : 1.0f), 2e-3f,
                 "sharpen at or above, keep below");
         }
         {
            // A flat field has no halo, and a transparent pixel keeps its hidden colour.
            Image in(16, 16);
            in.Fill(0.4f, 0.4f, 0.4f, 1.0f);
            float* hole = in.At(8, 8);
            hole[0] = 0.9f; hole[1] = 0.1f; hole[2] = 0.3f; hole[3] = 0.0f;
            FilterRig r(*d);
            r.Set("uAmount", 5.0f);
            r.Set("uRadius", 2.0f);
            Image out;
            float err = 1.0f;
            if (r.Run(in, out))
            {
               err = std::abs(out.At(8, 8)[0] - 0.9f) + std::abs(out.At(8, 8)[1] - 0.1f) + std::abs(out.At(8, 8)[3]);
               for (int y = 0; y < 16; y++)
                  for (int x = 0; x < 16; x++)
                     if (std::abs(x - 8) > 7 || std::abs(y - 8) > 7)
                        err = std::max(err, std::abs(out.At(x, y)[0] - 0.4f));
            }
            Spec(flag, "unsharpmask", "flat field and transparent pixel", err, 2e-3f, "unchanged");
         }
      }
   }

   void AuditSpecs(Flags& flag)
   {
      const Image ramp = FullRamp();

      // PhotoCraft Filter menu (2026-11-27): at its defaults each one visibly changes a picture.
      {
         static const char* kPhotoCraft[] = { "crystallize", "facet", "fragment", "mezzotint", "pointillize", "diffuse",
                                              "extrude", "oil paint", "tiles", "trace contour", "wind streaks",
                                              "tilt shift", "iris blur", "spin blur", "field blur", "path blur",
                                              "smart blur", "shape blur", "zigzag", "reduce noise", "hsb hsl", "fibers",
                                              "lens flare", "lighting effects" };
         const Image pic = RandomColours(128, 96, 5);
         for (const char* n : kPhotoCraft)
         {
            const FilterDef* d = FindDef(n);
            float changed = 0.0f;
            if (d != nullptr)
            {
               FilterRig r(*d);
               Image out;
               if (r.Run(pic, out))
                  changed = MaxAbsDiffRgb(out, pic);
            }
            Spec(flag, n, "changes the picture at defaults", std::max(0.0f, 0.05f - changed), 0.0f, "shortfall under a 0.05 change");
         }
      }

      // invert: 1 - x exactly.
      if (const FilterDef* d = FindDef("invert"))
      {
         FilterRig r(*d);
         Image out, want = ramp;
         r.Run(ramp, out);
         for (size_t i = 0; i < want.px.size(); i++)
            if (i % 4 != 3) want.px[i] = 1.0f - want.px[i];
         Spec(flag, "invert", "1 - x on a float ramp", MaxAbsDiffRgb(out, want), kOneCode * 0.51f, "exact");
      }

      // exposure: +1 EV doubles linear light. Mid grey 18% in, 36% out.
      if (const FilterDef* d = FindDef("exposure"))
      {
         FilterRig r(*d);
         r.Set("uExposure", 1.0f);
         Image in(16, 16), out;
         in.Fill(LinearToSrgb(0.18f), LinearToSrgb(0.18f), LinearToSrgb(0.18f), 1.0f);
         r.Run(in, out);
         float lin = SrgbToLinear(out.At(8, 8)[0]);
         printf("IMGAUDIT SPEC exposure +1 EV on 18%% grey -> %.3f linear (want 0.360)\n", lin);
         Spec(flag, "exposure", "+1 EV, linear light", std::fabs(lin - 0.36f), 0.01f, "stops of light");
         in.Fill(LinearToSrgb(0.8f), LinearToSrgb(0.8f), LinearToSrgb(0.8f), 1.0f);
         r.Run(in, out);
         printf("IMGAUDIT SPEC exposure +1 EV on 80%% -> %.3f encoded (want > 1.0, headroom)\n", out.At(8, 8)[0]);
      }

      // headroom: +1 EV on 80% is 1.6 linear. It must survive Curves, Blend
      // Add (over black) and the grade at their defaults, so -1 EV gives the
      // 80% back. The legacy look (patches saved before headroom) clips at
      // white, so it reads 0.5 there by design.
      const FilterDef* ex = FindDef("exposure");
      const FilterDef* gr = FindDef("color adjustments");
      const auto& modes = BlendModes::Names();
      const int addMode = (int)(std::find(modes.begin(), modes.end(), "Add") - modes.begin());
      if (ex != nullptr && gr != nullptr && addMode < (int)modes.size())
      {
         TexSource src, black;
         FilterRig up(*ex), grade(*gr), down(*ex);
         CurvesNode cv;
         BlendNode bl;
         if (LegacyLook())
         {
            cv.BeforeLoadParams();
            bl.BeforeLoadParams();
         }
         up.Set("uExposure", 1.0f);
         down.Set("uExposure", -1.0f);
         up.node->Input().Connect(&src);
         cv.Input().Connect(up.node.get());
         bl.InputA().Connect(&cv);
         bl.InputB().Connect(&black);
         bl.ModeIndex() = addMode;
         grade.node->Input().Connect(&bl);
         down.node->Input().Connect(grade.node.get());
         Image in(16, 16), blk(16, 16), mid, out;
         in.Fill(LinearToSrgb(0.8f), LinearToSrgb(0.8f), LinearToSrgb(0.8f), 1.0f);
         blk.Fill(0.0f, 0.0f, 0.0f, 1.0f);
         src.Set(in);
         black.Set(blk);
         Read(*down.node, out);
         const float lin = SrgbToLinear(out.At(8, 8)[0]);
         float stage[4];
         INode* stages[4] = { up.node.get(), &cv, &bl, grade.node.get() };
         for (int i = 0; i < 4; i++)
         {
            Read(*stages[i], mid);
            stage[i] = SrgbToLinear(mid.At(8, 8)[0]);
         }
         printf("IMGAUDIT SPEC headroom 80%% +1 EV %.3f -> Curves %.3f -> Blend Add %.3f -> grade %.3f -> -1 EV %.3f linear (want 1.6 ... 0.800)\n",
                stage[0], stage[1], stage[2], stage[3], lin);
         Spec(flag, "headroom", "over-white survives the chain", std::fabs(lin - 0.8f), 0.005f, "linear");
      }

      // color adjustments: defaults are identity; saturation 0 gives Rec.709 luminance.
      if (const FilterDef* d = FindDef("color adjustments"))
      {
         FilterRig r(*d);
         Image out;
         r.Run(ramp, out);
         Spec(flag, "color adjustments", "defaults are identity", MaxAbsDiffRgb(out, ramp), kOneCode * 0.51f, "");
         r.Set("uSaturation", 0.0f);
         const float prim[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
         const float want[3] = { 0.2126f, 0.7152f, 0.0722f };
         float worst = 0.0f;
         for (int c = 0; c < 3; c++)
         {
            Image in(16, 16);
            in.Fill(prim[c][0], prim[c][1], prim[c][2], 1.0f);
            r.Run(in, out);
            float lin = SrgbToLinear(out.At(8, 8)[0]);
            printf("IMGAUDIT SPEC color adjustments sat 0 on primary %d -> %.4f linear (Rec.709 %.4f)\n", c, lin, want[c]);
            worst = std::max(worst, std::fabs(lin - want[c]));
         }
         Spec(flag, "color adjustments", "sat 0 = Rec.709 luminance", worst, 0.01f, "linear");

         // ASC CDL v1.2 against a CPU reference: clamp(in*slope+offset)^power, then sat about Rec.709 luma.
         {
            r.Set("uSaturation", 1.0f);
            const float slope[3] = { 1.2f, 0.9f, 1.1f }, offs[3] = { 0.02f, -0.01f, 0.0f }, pw[3] = { 1.1f, 0.95f, 1.0f };
            const float sat = 0.8f;
            const char* sN[3] = { "uSlopeR", "uSlopeG", "uSlopeB" };
            const char* oN[3] = { "uOffsetR", "uOffsetG", "uOffsetB" };
            const char* pN[3] = { "uPowerR", "uPowerG", "uPowerB" };
            for (int c = 0; c < 3; c++) { r.Set(sN[c], slope[c]); r.Set(oN[c], offs[c]); r.Set(pN[c], pw[c]); }
            r.Set("uCdlSat", sat);
            Image in = RandomColours(64, 64, 3), want = in;
            for (int y = 0; y < in.h; y++)
               for (int x = 0; x < in.w; x++)
               {
                  float* o = want.At(x, y);
                  float v[3];
                  for (int c = 0; c < 3; c++)
                     v[c] = std::pow(std::min(1.0f, std::max(0.0f, o[c] * slope[c] + offs[c])), pw[c]);
                  const float l = 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2];
                  for (int c = 0; c < 3; c++)
                     o[c] = std::min(1.0f, std::max(0.0f, l + (v[c] - l) * sat));
               }
            r.Run(in, out);
            Spec(flag, "color adjustments", "ASC CDL v1.2 reference", MaxAbsDiffRgb(out, want), 1e-3f, "");
            for (int c = 0; c < 3; c++) { r.Set(sN[c], 1.0f); r.Set(oN[c], 0.0f); r.Set(pN[c], 1.0f); }
            r.Set("uCdlSat", 1.0f);
         }

         // 10-node chain: gamma 1.25 then 0.8, five times - ideally identity.
         std::vector<std::unique_ptr<FilterRig>> chain;
         TexSource head;
         Image in = FullRamp();
         head.Set(in);
         INode* prev = &head;
         std::vector<std::unique_ptr<FilterNode>> nodes;
         for (int i = 0; i < 10; i++)
         {
            auto n = std::make_unique<FilterNode>(*d);
            n->SetHeadroom(!LegacyLook());
            const auto& ps = d->params;
            for (size_t k = 0; k < ps.size(); k++)
               if (ps[k].uniformName == "uGamma")
                  n->SetParamValue(k, 0, (i % 2 == 0) ? 1.25f : 0.8f);
            n->Input().Connect(prev);
            prev = n.get();
            nodes.push_back(std::move(n));
         }
         Read(*prev, out);
         const int lv = DistinctLevels(out, 2, 0, out.w);
         const int lvDark = DistinctLevels(out, 2, 0, out.w / 10);
         printf("IMGAUDIT SPEC chain 10x color adjustments (gamma 1.25/0.8): max err %.4f, levels %d/1024, dark %d/103\n",
                MaxAbsDiffRgb(out, in), lv, lvDark);
         if (lvDark < 60)
            flag.Add("chain x10", "banding", "%.0f of 103 dark levels survive", lvDark);
      }

      // Blur family: sigma, ripples (holes between taps), energy, and the
      // linear-light edge. Gaussian: sigma should track the radius param.
      struct BlurCase { const char* name; const char* quality; float qv; float radius; };
      const BlurCase blurs[] = {
         { "gaussianblur", "uQuality", 0, 2 }, { "gaussianblur", "uQuality", 0, 10 },
         { "gaussianblur", "uQuality", 1, 2 }, { "gaussianblur", "uQuality", 1, 10 },
         { "boxblur", nullptr, 0, 2 }, { "boxblur", nullptr, 0, 10 },
      };
      for (const BlurCase& bc : blurs)
      {
         const FilterDef* d = FindDef(bc.name);
         if (!d) continue;
         FilterRig r(*d);
         if (bc.quality) r.Set(bc.quality, bc.qv);
         r.Set("uRadius", bc.radius);
         Profile p = MeasureLine(r);
         float dip = EdgeDip(r);
         printf("IMGAUDIT SPEC %-12s q%.0f radius %4.1f -> sigma %5.2f px, ripples %2d, energy %.3f, edge dip %.3f\n",
                bc.name, bc.qv, bc.radius, p.sigma, p.ripples, p.energy, dip);
         std::string who = std::string(bc.name) + (bc.quality ? (bc.qv > 0 ? " smooth" : " classic") : "");
         if (p.ripples > 0)
            flag.Add(who, "ripples", "%.0f holes between taps", p.ripples);
         if (std::fabs(p.energy - 1.0f) > 0.02f)
            flag.Add(who, "energy", "%.3f of input (want 1.000)", p.energy);
         if (dip < 0.98f)
            flag.Add(who, "edge", "%.3f dark seam (linear light = 1.000)", dip);
      }
      if (const FilterDef* d = FindDef("bloom"))
      {
         FilterRig r(*d);
         r.Set("uThreshold", 0.0f);
         r.Set("uIntensity", 1.0f);
         r.Set("uRadius", 12.0f);
         Profile p = MeasureLine(r);
         printf("IMGAUDIT SPEC bloom radius 12 -> sigma %5.2f px, ripples %2d, energy %.3f (input 1 + glow)\n",
                p.sigma, p.ripples, p.energy);
         if (p.ripples > 0)
            flag.Add("bloom", "ripples", "%.0f holes between taps", p.ripples);
      }

      // lut: an identity strip LUT (N*N x N, standard layout) must change nothing.
      if (const FilterDef* d = FindDef("lut"))
      {
         const int n = 16;
         Image lut(n * n, n);
         for (int g = 0; g < n; g++)
            for (int b = 0; b < n; b++)
               for (int rr = 0; rr < n; rr++)
               {
                  // GL row 0 is the bottom; the strip's green axis runs top to bottom in the file
                  // convention but texture v increases upward, which is what the shader indexes.
                  float* p = lut.At(b * n + rr, g);
                  p[0] = (float)rr / (n - 1); p[1] = (float)g / (n - 1); p[2] = (float)b / (n - 1); p[3] = 1;
               }
         FilterRig r(*d);
         r.Set("uSize", (float)n);
         Image in = RandomColours(64, 64, 3), out;
         r.Run(in, out, &lut);
         Spec(flag, "lut", "identity 16^3 strip LUT", MaxAbsDiffRgb(out, in), kOneCode, "");
      }
      // lookup: an identity palette (ramp) indexed by red must give red back.
      if (const FilterDef* d = FindDef("lookup"))
      {
         Image pal(256, 1);
         for (int i = 0; i < 256; i++)
         {
            float* p = pal.At(i, 0);
            p[0] = p[1] = p[2] = (float)i / 255.0f; p[3] = 1;
         }
         FilterRig r(*d);
         r.Set("uChannel", 1.0f);
         Image out;
         r.Run(ramp, out, &pal);
         Spec(flag, "lookup", "identity palette by red", MaxAbsDiffRgb(out, ramp), kOneCode, "");
      }
      // posterize 6: six evenly spaced output levels.
      if (const FilterDef* d = FindDef("posterize"))
      {
         FilterRig r(*d);
         r.Set("uLevels", 6.0f);
         Image out;
         r.Run(ramp, out);
         printf("IMGAUDIT SPEC posterize 6 levels -> %d distinct\n", DistinctLevels(out, 2, 0, out.w));
      }
      // dither: a flat 30% patch keeps its mean tone.
      if (const FilterDef* d = FindDef("dither"))
      {
         FilterRig r(*d);
         Image in(64, 64), out;
         in.Fill(0.3f, 0.3f, 0.3f, 1.0f);
         r.Run(in, out);
         double m = 0;
         for (int y = 0; y < out.h; y++)
            for (int x = 0; x < out.w; x++)
               m += out.At(x, y)[0];
         m /= (double)out.w * out.h;
         Spec(flag, "dither", "flat 0.30 keeps its mean", (float)std::fabs(m - 0.3), 0.01f, "tone");
      }
      // transform: defaults are pixel-exact; 90 degrees is a lossless rotation.
      if (const FilterDef* d = FindDef("transform"))
      {
         FilterRig r(*d);
         Image in = RandomColours(128, 128, 5), out;
         r.Run(in, out);
         Spec(flag, "transform", "defaults are identity", MaxAbsDiffRgb(out, in), kOneCode * 0.51f, "");
         r.Set("uRotation", 90.0f);
         r.Run(in, out);
         Image want(128, 128);
         for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++)
            {
               // Output (x,y) samples input rotated by -90 about the centre.
               const float* s = in.At(y, 127 - x);
               float* p = want.At(x, y);
               p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; p[3] = s[3];
            }
         Image want2(128, 128);
         for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++)
            {
               const float* s = in.At(127 - y, x);
               float* p = want2.At(x, y);
               p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; p[3] = s[3];
            }
         Spec(flag, "transform", "rotate 90 is lossless",
              std::min(MaxAbsDiffRgb(out, want), MaxAbsDiffRgb(out, want2)), kOneCode * 0.51f, "");
      }
      // premultiply -> unpremultiply round trip.
      if (const FilterDef* d = FindDef("premultiply"))
      {
         TexSource head;
         Image in = RandomColours(64, 64, 9, 0.02f, 1.0f), out;
         head.Set(in);
         FilterNode a(*d), b(*d);
         b.SetParamValue(0, 0, 1.0f);
         a.Input().Connect(&head);
         b.Input().Connect(&a);
         Read(b, out);
         Spec(flag, "premultiply", "premult -> unpremult", MaxAbsDiffRgb(out, in), kOneCode, "alpha 0.02..1");
      }
   }

   void AuditCurvesBlend(Flags& flag)
   {
      // Curves at its default (straight line) must be identity and keep precision.
      {
         TexSource src;
         CurvesNode c;
         c.SetExactLine(!LegacyLook()); // legacy: the old spline that bends a straight line
         c.Input().Connect(&src);
         Image ramp = FullRamp(), out;
         src.Set(ramp);
         Read(c, out);
         Image dark = DarkRamp(), outD;
         src.Set(dark);
         Read(c, outD);
         int worstX = 0;
         float worst = 0.0f;
         for (int x = 0; x < out.w; x++)
            if (std::fabs(out.At(x, 2)[0] - ramp.At(x, 2)[0]) > worst)
            {
               worst = std::fabs(out.At(x, 2)[0] - ramp.At(x, 2)[0]);
               worstX = x;
            }
         printf("IMGAUDIT %-18s fmt %-7s | dark %4d | id %5.3f at input %.3f\n", "Curves", FormatName(OutputFormat(c)),
                DistinctLevels(outD, 2, 0, outD.w), worst, ramp.At(worstX, 2)[0]);
         if (DistinctLevels(outD, 2, 0, outD.w) < kFlagDarkLevels)
            flag.Add("Curves", "dark", "%.0f levels in 0..0.1 (banding)", DistinctLevels(outD, 2, 0, outD.w));
      }

      // Blend: every colour mode, opaque over opaque, against the reference.
      TexSource sa, sb;
      BlendNode bl;
      bl.InputA().Connect(&sa);
      bl.InputB().Connect(&sb);
      bl.Mix() = 1.0f;
      bl.Linear() = false; // the W3C formulas are written for encoded values
      bl.Headroom() = false; // and clip at white (display-referred)
      bl.BlendFix() = LegacyLook() ? 0 : 1; // legacy: the pre-W3C Overlay/Soft/Hard Light/Hard Mix
      Image a = RandomColours(32, 32, 21), b = RandomColours(32, 32, 22), out;
      sa.Set(a);
      sb.Set(b);
      const auto& names = BlendModes::Names();
      int bad = 0;
      for (int m = 0; m < (int)names.size(); m++)
      {
         if (m == BlendModes::kEraseMode || m == BlendModes::kAntiEraseMode)
            continue;
         bl.ModeIndex() = m;
         sa.Touch();
         Read(bl, out);
         float worst = 0.0f;
         for (int y = 0; y < a.h; y++)
            for (int x = 0; x < a.w; x++)
            {
               const float* pa = a.At(x, y);
               const float* pb = b.At(x, y);
               V3 ref = RefBlend(m, { pa[0], pa[1], pa[2] }, { pb[0], pb[1], pb[2] });
               const float* po = out.At(x, y);
               worst = std::max({ worst, std::fabs(po[0] - ref.r), std::fabs(po[1] - ref.g), std::fabs(po[2] - ref.b) });
            }
         if (worst > kFlagSpecErr)
         {
            bad++;
            flag.Add("Blend " + names[m], "w3c", "%.4f max err vs reference", worst);
         }
      }
      printf("IMGAUDIT SPEC Blend %d colour modes vs W3C/Photoshop reference: %d off by > 2 codes\n",
             (int)names.size() - 2, bad);

      // Linear light: the same formulas applied to decoded light, re-encoded.
      bl.Linear() = true;
      int badLin = 0;
      std::string badLinNames;
      for (int m = 0; m < (int)names.size(); m++)
      {
         if (m == BlendModes::kEraseMode || m == BlendModes::kAntiEraseMode)
            continue;
         bl.ModeIndex() = m;
         sa.Touch();
         Read(bl, out);
         float worst = 0.0f;
         for (int y = 0; y < a.h; y++)
            for (int x = 0; x < a.w; x++)
            {
               const float* pa = a.At(x, y);
               const float* pb = b.At(x, y);
               const V3 la = { SrgbToLinear(pa[0]), SrgbToLinear(pa[1]), SrgbToLinear(pa[2]) };
               const V3 lb = { SrgbToLinear(pb[0]), SrgbToLinear(pb[1]), SrgbToLinear(pb[2]) };
               // Darker/Lighter Color pick a whole pixel by luminance: on a
               // near-tie either pick is right, so don't score it
               if ((m == 5 || m == 10) && std::fabs(Lum(la) - Lum(lb)) < 2e-3f)
                  continue;
               // Score against the reference over inputs within 2^-12 of the
               // nominal: the GPU decodes with a fast pow, and steep modes
               // (Vivid Light's burn divides by 2b) magnify that input error
               // without the formula being wrong. Distance to the range.
               const float* po = out.At(x, y);
               float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
               for (int k = 0; k < 5; k++)
               {
                  const float sa = k == 1 ? 1.0f + kInputTol : k == 2 ? 1.0f - kInputTol : 1.0f;
                  const float sb = k == 3 ? 1.0f + kInputTol : k == 4 ? 1.0f - kInputTol : 1.0f;
                  const V3 r = RefBlend(m, { la.r * sa, la.g * sa, la.b * sa }, { lb.r * sb, lb.g * sb, lb.b * sb });
                  const float e[3] = { LinearToSrgb(std::clamp(r.r, 0.0f, 1.0f)), LinearToSrgb(std::clamp(r.g, 0.0f, 1.0f)),
                                       LinearToSrgb(std::clamp(r.b, 0.0f, 1.0f)) };
                  for (int c = 0; c < 3; c++)
                  {
                     lo[c] = std::min(lo[c], e[c]);
                     hi[c] = std::max(hi[c], e[c]);
                  }
               }
               for (int c = 0; c < 3; c++)
                  worst = std::max(worst, std::max({ 0.0f, po[c] - hi[c], lo[c] - po[c] }));
            }
         if (worst > kFlagSpecErr)
         {
            badLin++;
            badLinNames += " " + names[m] + "(" + std::to_string((int)std::lround(worst * 255.0f)) + ")";
         }
      }
      printf("IMGAUDIT SPEC Blend %d colour modes in linear light vs reference: %d off by > 2 codes:%s\n",
             (int)names.size() - 2, badLin, badLinNames.c_str());

      // 50% white over black is half the light: 0.5 linear, 0.735 encoded.
      Image white(8, 8), black(8, 8);
      white.Fill(1.0f, 1.0f, 1.0f, 1.0f);
      black.Fill(0.0f, 0.0f, 0.0f, 1.0f);
      sa.Set(black);
      sb.Set(white);
      bl.ModeIndex() = 0;
      bl.Mix() = 0.5f;
      Read(bl, out);
      Spec(flag, "Blend", "50% white over black, linear", std::fabs(out.At(4, 4)[0] - LinearToSrgb(0.5f)), kOneCode, "half the light");
      bl.Mix() = 1.0f;
      bl.Linear() = false;

      // Porter-Duff: two 50% layers cover 75%.
      Image ha(8, 8), hb(8, 8);
      ha.Fill(0.2f, 0.4f, 0.6f, 0.5f);
      hb.Fill(0.8f, 0.2f, 0.1f, 0.5f);
      sa.Set(ha);
      sb.Set(hb);
      bl.ModeIndex() = 0;
      Read(bl, out);
      Spec(flag, "Blend", "50% over 50% = 75% alpha", std::fabs(out.At(4, 4)[3] - 0.75f), kOneCode, "Porter-Duff");
   }
}

bool RunImgAudit()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   Flags flag;
   bool ok = true;
   int audited = 0;
   // INFINITE_IMGAUDIT_ONLY=parity runs only the PhotoCraft parity checks.
   if (const char* only = std::getenv("INFINITE_IMGAUDIT_ONLY"); only != nullptr && std::string(only).rfind("parity", 0) == 0)
   {
      AuditParity(flag);
      if (gReadFbo != 0)
         glDeleteFramebuffers(1, &gReadFbo);
      printf("IMGAUDIT parity, %d flag(s) %s\n", flag.count, flag.count == 0 ? "OK" : "FAIL");
      return flag.count == 0;
   }
   AuditFilters(flag, ok, audited);
   AuditParity(flag);
   AuditCurvesBlend(flag);
   AuditSpecs(flag);
   if (gReadFbo != 0)
      glDeleteFramebuffers(1, &gReadFbo);
   printf("IMGAUDIT %d filter(s) + Curves + Blend, %d flag(s) %s\n", audited, flag.count, ok ? "OK" : "FAIL");
   return ok;
}
}
