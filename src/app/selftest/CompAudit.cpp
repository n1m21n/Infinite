// INFINITE_COMPAUDIT: a measurement audit of the compositing nodes.
//
// The image twin of FXAUDIT / SYNTHAUDIT / NOTEAUDIT / IOAUDIT / GEOAUDIT
// (docs/plans/node-audits/README.md, block 3). Needs the live GL context, so it
// is dispatched after the window exists (like SPOUTLOOPTEST); a hidden window
// is used. Real nodes run on real textures and are read back with
// GLUtil::ReadTexturePixels.
//
//   BLEND     Blend: every mode against the W3C Compositing and Blending
//             Level 1 formulas (separable and non-separable; Add, Linear Burn,
//             Linear Light, Subtract and Divide against their usual
//             definitions) on 4096 random 8-bit pixel pairs, opaque, mix 1
//             (flag > 1/255)
//   ALPHA     Blend Normal and Multiply with random source and backdrop alpha
//             and a mix of 0.7 against the W3C source-over formula, straight
//             alpha out (flag > 1/255)
//   DET       the same inputs twice give bit-identical pixels
//   FEEDBACK  Feedback hands back exactly the previous frame's input (in a plain
//             chain; the delay is documented as always one frame)
//   TRAILS    Trails decay after the input goes black: level at 10 frames
//             against decay^10, and whether the trail ever reaches black
//             (flag > 2/255 off, or a residual that never clears)
//   FLOW      Optical Flow on a smooth pattern moved by a known amount (0.5 to 7 px a
//             frame, sub-pixel): mean endpoint error over the interior (flag > 0.5 px),
//             a still pattern reads zero, and ms per cook at 1080p (report)
//   FLUID     Fluid: largest divergence left after projection (orbiting stirrer, with and
//             without a disc obstacle, at 20 / 60 / 120 pressure iterations; flag > 1e-3 at
//             120 iterations), a still picture with no stirring stays the picture (flag > 2/255),
//             the output is finite after 60 frames, and ms per cook at 1080p (report, bar 3 ms)
//   TIMEMACHINE  Time Machine fed flat frames whose grey is 3 x the frame number, a
//             left-to-right ramp as the delay map: every pixel must read the frame
//             the ramp asks for (mean error flag > 1 level, worst > 3), with and without
//             a map, inverted, and at length 8; the first frame fills the ring; ms per cook
//   PROXIMITY Proximity Lines: with caps that do not bind, the link set equals a brute
//             force all-pairs search exactly (2000 random points); with caps it keeps the
//             degree and total limits, every pair once; cook is stable on a second cook;
//             ms to find links and to cook the whole node at 100k points (reported, not flagged,
//             like the 1080p costs of FLOW)
//   HAP       HAP movies: snappy decode, a hand-built .mov read back frame by frame (raw, snappy and
//             chunked frames all give the same texture), a cut-off file is refused, and Video Source
//             shows a DXT1 clip (red) and a HAP Q clip (grey) through the real upload path
//   HAPGEN    writes a 4096x2160 Hap1 test clip (random DXT1 blocks, snappy-wrapped) for the 3.12 bench;
//             only when named; INFINITE_HAPGEN_OUT = path, INFINITE_HAPGEN_FRAMES = count (default 90)
//   STACK     Layer Stack with two layers against Blend with the same mode and
//             opacity, random alpha (flag > 1/255)
//   CURVES    default curve is identity; an inverted RGB curve gives 1-x; a Red
//             curve leaves green and blue alone; mix 0 returns the input
//             (flag > 1/255)
//   RAMP      Color Ramp red to blue over a 64-step grey ramp against the
//             analytic gradient (Linear), and a Constant ramp against its step
//             (flag > 1/255)
//   FIT       output size follows width/height and matchInput; Fit leaves
//             transparent bars, Fill covers the frame, Stretch and Fit keep the
//             centre pixel
//   SWITCH    Switcher in manual mode shows exactly the chosen slot
//
//   BLUR      Gaussian (Classic, Smooth) and Box blur at radius 1/2/4 on a hard
//             edge: output against the shader's own kernel in double (flag > 1/255),
//             the effective sigma of the edge response against the nominal sigma the
//             slider implies (report; flag > 10 % off), energy kept on a flat image,
//             and ms per cook against radius at 1024^2
//   COLOUR    which colour space blur, blend and ramp work in: a 0/1 checker blurred,
//             black over white at 50 %, and a ramp midpoint, as encoded values against
//             the linear-light answer (flag when blur and blend disagree)
//   RES       Gaussian and Blend at 1 / 4 / 16 Mpx: ms per cook, output bytes
//   BYPASS    a bypassed node's pull-through is the input's texture; toggling bypass
//             on then off gives the same pixels as the first cook
//   SOURCES   every Source node and Reaction Diffusion cooks: output exists, finite,
//             range, and (Reaction Diffusion) stays inside [0, 1] over 120 frames
//
// Not covered: Remove Background (needs the ONNX model; checked by REMOVEBGTEST).
//
// Only hard invariants fail the run (a node that produces no output at all).
// Everything else is reported, with FLAG lines.
#include "app/AppShared.h"
#include "core/FilterDefs.h"
#include "nodes/BlendNode.h"
#include "nodes/FilterNode.h"
#include "nodes/ColorRampNode.h"
#include "nodes/CurvesNode.h"
#include "nodes/FeedbackNodes.h"
#include "nodes/FitNode.h"
#include "nodes/LayerStackNode.h"
#include "nodes/OpticalFlowNode.h"
#include "nodes/FluidNode.h"
#include "nodes/TimeMachineNode.h"
#include "nodes/ProximityLinesNode.h"
#include <set>
#include "nodes/SwitcherNode.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "core/ShareBundle.h"
#include "core/HapReader.h"
#include "nodes/VideoSourceNode.h"
#include <thread>
#include <cstring>
#include <functional>

namespace app
{
namespace
{
   constexpr int kW = 64, kH = 64;

   // One measured value for the quality system (docs/quality/README.md): tools/quality/quality.py keeps
   // the worst per metric and subject and owns the target and the release gate.
   void Quality(const char* id, const std::string& node, double v)
   {
      printf("QUALITY id=%s subject=\"node:%s\" value=%.4f\n", id, node.c_str(), v);
   }

   struct Flags
   {
      int n = 0;
      void Flag(const char* sec, const std::string& what, double v)
      {
         printf("COMPAUDIT FLAG %-9s %s %.5g\n", sec, what.c_str(), v);
         n++;
      }
   };

   // A node that hands out a texture the audit fills itself.
   class TexSource : public INode
   {
   public:
      explicit TexSource(int w = kW, int h = kH) : mW(w), mH(h)
      {
         glGenTextures(1, &mTex);
         glBindTexture(GL_TEXTURE_2D, mTex);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glBindTexture(GL_TEXTURE_2D, 0);
      }
      ~TexSource() override { glDeleteTextures(1, &mTex); }
      void Set(const std::vector<unsigned char>& rgba)
      {
         glBindTexture(GL_TEXTURE_2D, mTex);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mW, mH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         mRevision = NextTextureRevision();
      }
      unsigned int GetOutputTexture() override { return mTex; }
      int GetOutputWidth() const override { return mW; }
      int GetOutputHeight() const override { return mH; }
      void CookIfNeeded(int) override {}
      unsigned long long TextureRevision() const override { return mRevision; }

   private:
      int mW = kW, mH = kH;
      unsigned int mTex = 0;
      unsigned long long mRevision = 0;
   };

   struct Rng
   {
      unsigned s;
      explicit Rng(unsigned seed) : s(seed) {}
      unsigned Next()
      {
         s = s * 1664525u + 1013904223u;
         return s >> 8;
      }
   };

   std::vector<unsigned char> RandomImage(unsigned seed, bool randomAlpha)
   {
      Rng r(seed);
      std::vector<unsigned char> px((size_t)kW * kH * 4);
      for (size_t i = 0; i < (size_t)kW * kH; i++)
      {
         for (int c = 0; c < 3; c++)
            px[i * 4 + c] = (unsigned char)(r.Next() & 255);
         px[i * 4 + 3] = randomAlpha ? (unsigned char)(r.Next() & 255) : 255;
      }
      // Pin the corners so the extremes 0 and 255 are always present.
      for (int c = 0; c < 4; c++)
      {
         px[(size_t)c] = 0;
         px[(size_t)(kW - 1) * 4 + (size_t)c] = 255;
      }
      return px;
   }

   std::vector<unsigned char> Flat(unsigned char v)
   {
      std::vector<unsigned char> px((size_t)kW * kH * 4, v);
      for (size_t i = 0; i < (size_t)kW * kH; i++)
         px[i * 4 + 3] = 255;
      return px;
   }

   bool Read(unsigned int tex, std::vector<float>& out)
   {
      unsigned int fbo = 0;
      const bool ok = GLUtil::ReadTexturePixels(fbo, tex, kW, kH, out);
      if (fbo != 0)
         glDeleteFramebuffers(1, &fbo);
      return ok;
   }

   // ---- W3C Compositing and Blending Level 1 reference (double) ----
   using V3 = std::array<double, 3>;
   double Lum(const V3& c) { return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; }
   V3 ClipColor(V3 c)
   {
      const double l = Lum(c);
      const double n = std::min(c[0], std::min(c[1], c[2]));
      const double x = std::max(c[0], std::max(c[1], c[2]));
      if (n < 0.0)
         for (double& v : c)
            v = l + (v - l) * l / (l - n);
      if (x > 1.0)
         for (double& v : c)
            v = l + (v - l) * (1.0 - l) / (x - l);
      return c;
   }
   V3 SetLum(const V3& c, double l)
   {
      const double d = l - Lum(c);
      return ClipColor({ c[0] + d, c[1] + d, c[2] + d });
   }
   double Sat(const V3& c) { return std::max(c[0], std::max(c[1], c[2])) - std::min(c[0], std::min(c[1], c[2])); }
   V3 SetSat(V3 c, double s)
   {
      int mx = 0, mn = 0;
      for (int i = 1; i < 3; i++)
      {
         if (c[i] > c[mx])
            mx = i;
         if (c[i] < c[mn])
            mn = i;
      }
      if (mx == mn)
         return { 0, 0, 0 };
      const int mid = 3 - mx - mn;
      V3 r { 0, 0, 0 };
      if (c[mx] > c[mn])
      {
         r[mid] = (c[mid] - c[mn]) * s / (c[mx] - c[mn]);
         r[mx] = s;
      }
      return r;
   }

   // Separable modes, cb = backdrop, cs = source. nullptr-style "unknown" is reported by the caller.
   bool Separable(const std::string& n, double cb, double cs, double& out)
   {
      auto screen = [](double a, double b) { return a + b - a * b; };
      if (n == "Normal") out = cs;
      else if (n == "Darken") out = std::min(cb, cs);
      else if (n == "Lighten") out = std::max(cb, cs);
      else if (n == "Multiply") out = cb * cs;
      else if (n == "Screen") out = screen(cb, cs);
      else if (n == "Overlay") out = cb <= 0.5 ? cs * 2.0 * cb : screen(cs, 2.0 * cb - 1.0);
      else if (n == "Hard Light") out = cs <= 0.5 ? cb * 2.0 * cs : screen(cb, 2.0 * cs - 1.0);
      else if (n == "Soft Light")
      {
         if (cs <= 0.5)
            out = cb - (1.0 - 2.0 * cs) * cb * (1.0 - cb);
         else
         {
            const double d = cb <= 0.25 ? ((16.0 * cb - 12.0) * cb + 4.0) * cb : std::sqrt(cb);
            out = cb + (2.0 * cs - 1.0) * (d - cb);
         }
      }
      else if (n == "Color Dodge") out = cb == 0.0 ? 0.0 : (cs >= 1.0 ? 1.0 : std::min(1.0, cb / (1.0 - cs)));
      else if (n == "Color Burn") out = cb >= 1.0 ? 1.0 : (cs <= 0.0 ? 0.0 : 1.0 - std::min(1.0, (1.0 - cb) / cs));
      else if (n == "Difference") out = std::fabs(cb - cs);
      else if (n == "Exclusion") out = cb + cs - 2.0 * cb * cs;
      // Not in the W3C list, but with one agreed definition.
      else if (n == "Linear Burn") out = std::max(0.0, cb + cs - 1.0);
      else if (n == "Add") out = std::min(1.0, cb + cs);
      else if (n == "Linear Light") out = std::min(1.0, std::max(0.0, cb + 2.0 * cs - 1.0));
      else if (n == "Subtract") out = std::max(0.0, cb - cs);
      else if (n == "Divide") out = cs <= 0.0 ? 1.0 : std::min(1.0, cb / cs);
      else return false;
      return true;
   }

   bool NonSeparable(const std::string& n, const V3& cb, const V3& cs, V3& out)
   {
      if (n == "Hue") out = SetLum(SetSat(cs, Sat(cb)), Lum(cb));
      else if (n == "Saturation") out = SetLum(SetSat(cb, Sat(cs)), Lum(cb));
      else if (n == "Color") out = SetLum(cs, Lum(cb));
      else if (n == "Luminosity") out = SetLum(cb, Lum(cs));
      else return false;
      return true;
   }

   // W3C source-over with a blend function: straight-alpha result.
   bool Reference(const std::string& mode, const unsigned char* b8, const unsigned char* s8, double mix, double out[4])
   {
      const V3 cb { b8[0] / 255.0, b8[1] / 255.0, b8[2] / 255.0 };
      const V3 cs { s8[0] / 255.0, s8[1] / 255.0, s8[2] / 255.0 };
      const double ab = b8[3] / 255.0, as = s8[3] / 255.0 * mix;
      V3 B;
      if (!NonSeparable(mode, cb, cs, B))
         for (int c = 0; c < 3; c++)
            if (!Separable(mode, cb[(size_t)c], cs[(size_t)c], B[(size_t)c]))
               return false;
      const double ar = as + ab * (1.0 - as);
      for (int c = 0; c < 3; c++)
      {
         const double mixed = (1.0 - ab) * cs[(size_t)c] + ab * B[(size_t)c];
         out[c] = ar > 1e-6 ? (as * mixed + ab * (1.0 - as) * cb[(size_t)c]) / ar : 0.0;
      }
      out[3] = ar;
      return true;
   }

   int gFrame = 5000;

   struct BlendRig
   {
      TexSource a, b;
      BlendNode node;
      // The formula checks are on encoded values: run the node in encoded mode
      // (linear light and headroom off, the fixed modes on). COLOUR keeps the defaults.
      explicit BlendRig(bool encoded = true)
      {
         if (encoded)
         {
            node.Linear() = false;
            node.Headroom() = false;
         }
         node.InputA().Connect(&a);
         node.InputB().Connect(&b);
      }
      bool Run(int mode, float mix, std::vector<float>& px)
      {
         node.ModeIndex() = mode;
         node.Mix() = mix;
         node.CookIfNeeded(gFrame++);
         return node.GetOutputTexture() != 0 && Read(node.GetOutputTexture(), px);
      }
   };

   double WorstError(const std::string& mode, BlendRig& rig, const std::vector<unsigned char>& A,
                     const std::vector<unsigned char>& B, double mix, const std::vector<float>& px, bool& known)
   {
      double worst = 0.0;
      known = true;
      for (size_t i = 0; i < (size_t)kW * kH; i++)
      {
         double ref[4];
         if (!Reference(mode, &A[i * 4], &B[i * 4], mix, ref))
         {
            known = false;
            return 0.0;
         }
         // Where the result is fully transparent its colour is undefined.
         for (int c = 0; c < 4; c++)
         {
            if (c < 3 && ref[3] < 1e-4)
               continue;
            worst = std::max(worst, std::fabs(ref[c] - (double)px[i * 4 + (size_t)c]));
         }
      }
      (void)rig;
      return worst;
   }

   void AuditBlend(Flags& f, bool& ok)
   {
      BlendRig rig;
      const auto A = RandomImage(11, false), B = RandomImage(23, false);
      rig.a.Set(A);
      rig.b.Set(B);
      const auto& names = BlendNode::ModeNames();
      for (size_t m = 0; m < names.size(); m++)
      {
         std::vector<float> px;
         if (!rig.Run((int)m, 1.0f, px))
         {
            f.Flag("BLEND", names[m] + " produced no output", 1);
            ok = false;
            continue;
         }
         bool known = true;
         const double worst = WorstError(names[m], rig, A, B, 1.0, px, known);
         if (!known)
         {
            printf("COMPAUDIT BLEND  %-14s no published reference, skipped\n", names[m].c_str());
            continue;
         }
         printf("COMPAUDIT BLEND  %-14s worst error %.2f / 255\n", names[m].c_str(), worst * 255.0);
         Quality("comp.blend_x255", "Blend", worst * 255.0);
         if (worst > 1.0 / 255.0 + 1e-6)
            f.Flag("BLEND", names[m] + " differs from the W3C formula by (x/255)", worst * 255.0);
      }
   }

   void AuditAlpha(Flags& f, bool& ok)
   {
      BlendRig rig;
      const auto A = RandomImage(31, true), B = RandomImage(47, true);
      rig.a.Set(A);
      rig.b.Set(B);
      const auto& names = BlendNode::ModeNames();
      for (const char* mode : { "Normal", "Multiply", "Screen" })
      {
         size_t idx = 0;
         for (size_t m = 0; m < names.size(); m++)
            if (names[m] == mode)
               idx = m;
         std::vector<float> px;
         if (!rig.Run((int)idx, 0.7f, px))
         {
            f.Flag("ALPHA", std::string(mode) + " produced no output", 1);
            ok = false;
            continue;
         }
         bool known = true;
         const double worst = WorstError(mode, rig, A, B, 0.7, px, known);
         printf("COMPAUDIT ALPHA  %-14s worst error %.2f / 255 (alpha random, mix 0.7)\n", mode, worst * 255.0);
         if (worst > 1.0 / 255.0 + 1e-6)
            f.Flag("ALPHA", std::string(mode) + " differs from W3C source-over by (x/255)", worst * 255.0);
      }
   }

   void AuditDet(Flags& f)
   {
      BlendRig rig;
      rig.a.Set(RandomImage(5, true));
      rig.b.Set(RandomImage(6, true));
      for (int mode : { 0, 2, 12, 22 })
      {
         std::vector<float> p1, p2;
         rig.Run(mode, 0.8f, p1);
         rig.Run(mode, 0.8f, p2);
         if (p1.empty() || p1 != p2)
            f.Flag("DET", "Blend mode " + std::to_string(mode) + " two cooks differ", 1);
      }
      printf("COMPAUDIT DET    Blend: 4 modes cooked twice\n");
   }

   void AuditFeedback(Flags& f)
   {
      // By design (FeedbackNodes.h): the delay comes from the cycle, so in a plain chain the capture is the
      // current frame and the node is a pass-through copy. Saved loops depend on exactly this timing.
      TexSource src;
      FeedbackNode fb;
      fb.Input().Connect(&src);
      double worst = 0.0;
      for (int k = 0; k < 6; k++)
      {
         const unsigned char v = (unsigned char)(40 + 35 * k);
         src.Set(Flat(v));
         fb.CookIfNeeded(gFrame++);
         std::vector<float> px;
         if (!Read(fb.GetOutputTexture(), px) || px.empty())
            continue;
         worst = std::max(worst, (double)std::fabs(px[0] - v / 255.0f));
      }
      printf("COMPAUDIT FEEDBACK plain chain is a pass-through of the current frame: worst difference %.2f / 255\n", worst * 255.0);
      if (worst > 1.0 / 255.0 + 1e-6)
         f.Flag("FEEDBACK", "in a plain chain the output is not the current frame, off by (x/255)", worst * 255.0);
   }


   void AuditTrails(Flags& f)
   {
      for (float decay : { 0.5f, 0.92f, 0.99f })
      {
         TexSource src;
         TrailsNode tr;
         tr.decay = decay;
         tr.Input().Connect(&src);
         src.Set(Flat(255));
         tr.CookIfNeeded(gFrame++);
         src.Set(Flat(0));
         double at10 = -1.0, residual = 0.0;
         int cleared = -1;
         const int frames = 1200;
         for (int k = 1; k <= frames; k++)
         {
            tr.CookIfNeeded(gFrame++);
            std::vector<float> px;
            if (!Read(tr.GetOutputTexture(), px) || px.empty())
               break;
            if (k == 10)
               at10 = px[0];
            residual = px[0];
            if (cleared < 0 && px[0] < 0.5f / 255.0f)
               cleared = k;
         }
         const double want = std::pow((double)decay, 10.0);
         printf("COMPAUDIT TRAILS decay %.2f: level at 10 frames %.4f (decay^10 = %.4f), cleared at frame %d, residual after %d frames %.1f / 255\n",
                decay, at10, want, cleared, frames, residual * 255.0);
         if (std::fabs(at10 - want) > 2.0 / 255.0)
            f.Flag("TRAILS", "decay " + std::to_string(decay) + " level at 10 frames off by (x/255)",
                   std::fabs(at10 - want) * 255.0);
         if (cleared < 0)
            f.Flag("TRAILS", "decay " + std::to_string(decay) + " trail never reaches black, residual (x/255)",
                   residual * 255.0);
      }
   }

   // ---------------------------------------------------------------- STACK
   void AuditStack(Flags& f, bool& ok)
   {
      const auto A = RandomImage(31, true), B = RandomImage(37, true);
      TexSource a, b;
      a.Set(A);
      b.Set(B);
      LayerStackNode stack;
      stack.Input(0).Connect(&a);
      stack.Input(1).Connect(&b);
      stack.fit = LayerStackNode::kStretch;
      stack.linear = stack.headroom = false;
      BlendNode blend;
      blend.Linear() = blend.Headroom() = false;
      blend.InputA().Connect(&a);
      blend.InputB().Connect(&b);
      const auto& names = BlendNode::ModeNames();
      for (int mode : { 0, 1, 2, 7, 9, 11, 12, 13, 18, 22, 25 })
         for (float op : { 1.0f, 0.6f })
         {
            stack.modes[1] = mode;
            stack.opacities[1] = op;
            stack.CookIfNeeded(gFrame++);
            blend.ModeIndex() = mode;
            blend.Mix() = op;
            blend.CookIfNeeded(gFrame++);
            std::vector<float> ps, pb;
            if (!Read(stack.GetOutputTexture(), ps) || !Read(blend.GetOutputTexture(), pb))
            {
               printf("COMPAUDIT FAIL STACK no output\n");
               ok = false;
               return;
            }
            double worst = 0.0;
            for (size_t i = 0; i < ps.size(); i++)
            {
               // Colour is undefined where the result is fully transparent.
               if (i % 4 != 3 && pb[i / 4 * 4 + 3] < 1e-4f)
                  continue;
               worst = std::max(worst, (double)std::fabs(ps[i] - pb[i]));
            }
            printf("COMPAUDIT STACK %-12s opacity %.1f vs Blend: worst %.2f / 255\n", names[(size_t)mode].c_str(), op,
                   worst * 255.0);
            if (worst > 1.0 / 255.0)
               f.Flag("STACK", std::string(names[(size_t)mode]) + " opacity " + std::to_string(op) + " differs from Blend by (x/255)",
                      worst * 255.0);
         }
   }

   // --------------------------------------------------------------- CURVES
   std::vector<unsigned char> GreyRamp()
   {
      std::vector<unsigned char> px((size_t)kW * kH * 4);
      for (int y = 0; y < kH; y++)
         for (int x = 0; x < kW; x++)
         {
            const unsigned char v = (unsigned char)std::lround(255.0 * x / (kW - 1));
            unsigned char* p = &px[((size_t)y * kW + (size_t)x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
         }
      return px;
   }

   double MaxDiffRow(const std::vector<float>& px, const std::function<void(int, double*)>& want)
   {
      double worst = 0.0;
      if (getenv("INFINITE_COMPAUDIT_ROW"))
         for (int x = 0; x < kW; x += 8)
         {
            double w[3];
            want(x, w);
            const float* p = &px[((size_t)(kH / 2) * kW + (size_t)x) * 4];
            printf("   x=%d want %.3f %.3f %.3f got %.3f %.3f %.3f\n", x, w[0], w[1], w[2], p[0], p[1], p[2]);
         }
      const int y = kH / 2;
      for (int x = 0; x < kW; x++)
      {
         double w[3];
         want(x, w);
         for (int c = 0; c < 3; c++)
            worst = std::max(worst, std::fabs(w[c] - (double)px[((size_t)y * kW + (size_t)x) * 4 + (size_t)c]));
      }
      return worst;
   }

   void AuditCurves(Flags& f, bool& ok)
   {
      TexSource src;
      src.Set(GreyRamp());
      CurvesNode n;
      n.Input().Connect(&src);
      auto run = [&](std::vector<float>& px) {
         n.MarkDirty();
         n.CookIfNeeded(gFrame++);
         return n.GetOutputTexture() != 0 && Read(n.GetOutputTexture(), px);
      };
      std::vector<float> px;
      if (!run(px))
      {
         printf("COMPAUDIT FAIL CURVES no output\n");
         ok = false;
         return;
      }
      auto ramp = [](int x, double* w) { w[0] = w[1] = w[2] = std::lround(255.0 * x / (kW - 1)) / 255.0; };
      double e = MaxDiffRow(px, ramp);
      printf("COMPAUDIT CURVES identity: worst %.2f / 255\n", e * 255.0);
      if (e > 1.5 / 255.0)
         f.Flag("CURVES", "default curve is not identity, worst (x/255)", e * 255.0);

      n.MovePoint(CurvesNode::kRGB, 0, 0.0f, 1.0f);
      n.MovePoint(CurvesNode::kRGB, 1, 1.0f, 0.0f);
      run(px);
      e = MaxDiffRow(px, [&](int x, double* w) { ramp(x, w); for (int c = 0; c < 3; c++) w[c] = 1.0 - w[c]; });
      printf("COMPAUDIT CURVES inverted RGB: worst %.2f / 255\n", e * 255.0);
      if (e > 1.5 / 255.0)
         f.Flag("CURVES", "inverted curve is not 1-x, worst (x/255)", e * 255.0);

      n.ResetChannel(CurvesNode::kRGB);
      n.MovePoint(CurvesNode::kRed, 0, 0.0f, 1.0f);
      n.MovePoint(CurvesNode::kRed, 1, 1.0f, 0.0f);
      run(px);
      e = MaxDiffRow(px, [&](int x, double* w) { ramp(x, w); w[0] = 1.0 - w[0]; });
      printf("COMPAUDIT CURVES Red only inverted: worst %.2f / 255\n", e * 255.0);
      if (e > 1.5 / 255.0)
         f.Flag("CURVES", "Red curve leaks into G/B or is wrong, worst (x/255)", e * 255.0);

      n.mix = 0.0f;
      run(px);
      e = MaxDiffRow(px, ramp);
      printf("COMPAUDIT CURVES mix 0: worst %.2f / 255\n", e * 255.0);
      if (e > 1.0 / 255.0)
         f.Flag("CURVES", "mix 0 does not return the input, worst (x/255)", e * 255.0);
   }

   // ------------------------------------------------------------------ RAMP
   void AuditRamp(Flags& f, bool& ok)
   {
      TexSource src;
      src.Set(GreyRamp());
      ColorRampNode n;
      n.Input().Connect(&src);
      n.stopCount = 2;
      n.stopPos[0] = 0.0f; n.stopPos[1] = 1.0f;
      n.stopColor[0][0] = 1; n.stopColor[0][1] = 0; n.stopColor[0][2] = 0;
      n.stopColor[1][0] = 0; n.stopColor[1][1] = 0; n.stopColor[1][2] = 1;
      n.MarkDirty();
      n.interpMode = ColorRampNode::kLinear;
      n.CookIfNeeded(gFrame++);
      std::vector<float> px;
      if (n.GetOutputTexture() == 0 || !Read(n.GetOutputTexture(), px))
      {
         printf("COMPAUDIT FAIL RAMP no output\n");
         ok = false;
         return;
      }
      auto grey = [](int x) { return std::lround(255.0 * x / (kW - 1)) / 255.0; };
      double e = MaxDiffRow(px, [&](int x, double* w) { const double t = grey(x); w[0] = 1.0 - t; w[1] = 0.0; w[2] = t; });
      printf("COMPAUDIT RAMP linear red->blue: worst %.2f / 255\n", e * 255.0);
      if (e > 1.5 / 255.0)
         f.Flag("RAMP", "linear gradient differs from analytic, worst (x/255)", e * 255.0);

      n.interpMode = ColorRampNode::kConstant;
      n.MarkDirty();
      n.CookIfNeeded(gFrame++);
      Read(n.GetOutputTexture(), px);
      // Constant holds the colour of the last stop at or below t: red until t reaches 1.
      e = MaxDiffRow(px, [&](int x, double* w) {
         const double t = grey(x);
         const bool blue = t >= 1.0;
         w[0] = blue ? 0.0 : 1.0; w[1] = 0.0; w[2] = blue ? 1.0 : 0.0;
      });
      printf("COMPAUDIT RAMP constant: worst %.2f / 255\n", e * 255.0);
      if (e > 1.5 / 255.0)
         f.Flag("RAMP", "constant ramp is not a step, worst (x/255)", e * 255.0);
   }

   // ------------------------------------------------------------------- FIT
   void AuditFit(Flags& f, bool& ok)
   {
      TexSource src;
      src.Set(RandomImage(53, false));
      FitNode n;
      n.Input().Connect(&src);
      n.width = 128.0f;
      n.height = 64.0f;
      n.bgOpacity = 0.0f;
      auto cook = [&]() { n.CookIfNeeded(gFrame++); return n.GetOutputTexture() != 0; };
      auto readAt = [&](float out[4], int x, int y) {
         std::vector<float> px;
         unsigned int fbo = 0;
         const bool r = GLUtil::ReadTexturePixels(fbo, n.GetOutputTexture(), n.GetOutputWidth(), n.GetOutputHeight(), px);
         if (fbo != 0)
            glDeleteFramebuffers(1, &fbo);
         if (!r)
            return false;
         for (int c = 0; c < 4; c++)
            out[c] = px[((size_t)y * (size_t)n.GetOutputWidth() + (size_t)x) * 4 + (size_t)c];
         return true;
      };
      const std::vector<unsigned char> S = RandomImage(53, false);
      for (int mode : { 0, 1, 2 })
      {
         n.mode = mode;
         if (!cook())
         {
            printf("COMPAUDIT FAIL FIT no output\n");
            ok = false;
            return;
         }
         const char* nm = FitNode::ModeNames()[(size_t)mode].c_str();
         const int w = n.GetOutputWidth(), h = n.GetOutputHeight();
         float left[4] = {}, mid[4] = {};
         readAt(left, 4, h / 2);
         readAt(mid, w / 2, h / 2);
         printf("COMPAUDIT FIT %-8s out %dx%d | left-edge alpha %.2f | centre alpha %.2f\n", nm, w, h, left[3], mid[3]);
         if (w != 128 || h != 64)
            f.Flag("FIT", std::string(nm) + " output size is not width x height, width", w);
         if (mid[3] < 0.99f)
            f.Flag("FIT", std::string(nm) + " centre is transparent, alpha", mid[3]);
         if (mode == 0 && left[3] > 0.01f)
            f.Flag("FIT", "Fit leaves no transparent bar at the side, alpha", left[3]);
         if (mode != 0 && left[3] < 0.99f)
            f.Flag("FIT", std::string(nm) + " does not cover the frame at the left edge, alpha", left[3]);
      }
      n.mode = 2;
      n.matchInput = true;
      if (cook() && (n.GetOutputWidth() != kW || n.GetOutputHeight() != kH))
         f.Flag("FIT", "matchInput does not adopt the source size, width", n.GetOutputWidth());
      printf("COMPAUDIT FIT matchInput -> %dx%d\n", n.GetOutputWidth(), n.GetOutputHeight());
      (void)S;
   }

   // ---------------------------------------------------------------- SWITCH
   void AuditSwitch(Flags& f, bool& ok)
   {
      TexSource s[4];
      SwitcherNode n;
      const unsigned char vals[4] = { 40, 100, 160, 220 };
      for (int i = 0; i < 4; i++)
      {
         s[i].Set(Flat(vals[i]));
         n.Input(i).Connect(&s[i]);
      }
      n.manual = true;
      n.crossfade = 0.0f;
      for (int slot = 0; slot < 4; slot++)
      {
         n.manualSlot = slot;
         n.CookIfNeeded(gFrame++);
         std::vector<float> px;
         if (n.GetOutputTexture() == 0 || !Read(n.GetOutputTexture(), px))
         {
            printf("COMPAUDIT FAIL SWITCH no output\n");
            ok = false;
            return;
         }
         const double e = std::fabs((double)px[0] - vals[slot] / 255.0);
         printf("COMPAUDIT SWITCH manual slot %d: %.4f (want %.4f)\n", slot, px[0], vals[slot] / 255.0);
         if (e > 1.0 / 255.0)
            f.Flag("SWITCH", "manual slot " + std::to_string(slot) + " shows the wrong input, off by (x/255)", e * 255.0);
      }
   }

   // ---------------------------------------------------------------- helpers
   const FilterDef* FindDef(const char* name)
   {
      for (const FilterDef& d : GetFilterDefs())
         if (d.name == name)
            return &d;
      return nullptr;
   }

   bool ReadSized(unsigned int tex, int w, int h, std::vector<float>& out)
   {
      unsigned int fbo = 0;
      const bool ok = GLUtil::ReadTexturePixels(fbo, tex, w, h, out);
      if (fbo != 0)
         glDeleteFramebuffers(1, &fbo);
      return ok;
   }

   std::vector<unsigned char> Solid(int w, int h, unsigned char v)
   {
      std::vector<unsigned char> px((size_t)w * h * 4, v);
      for (size_t i = 0; i < (size_t)w * h; i++)
         px[i * 4 + 3] = 255;
      return px;
   }

   // Left half 0, right half 255: the edge sits between columns kW/2-1 and kW/2.
   std::vector<unsigned char> EdgeImage()
   {
      std::vector<unsigned char> px((size_t)kW * kH * 4, 0);
      for (int y = 0; y < kH; y++)
         for (int x = 0; x < kW; x++)
         {
            const size_t i = ((size_t)y * kW + x) * 4;
            px[i] = px[i + 1] = px[i + 2] = x >= kW / 2 ? 255 : 0;
            px[i + 3] = 255;
         }
      return px;
   }

   // The shader's own 1D kernel (taps and weights), normalised.
   struct Tap { double off, w; };
   std::vector<Tap> KernelOf(int kind, double r) // 0 classic, 1 smooth, 2 box
   {
      std::vector<Tap> k;
      double total = 0.0;
      if (kind == 1)
      {
         const double rr = std::max(r, 0.001);
         const int n = std::min((int)std::ceil(6.0 * rr), 60);
         for (int i = -n; i <= n; i++)
         {
            const double d = i / rr;
            k.push_back({ (double)i, std::exp(-d * d / 8.0) });
         }
      }
      else if (kind == 0)
         for (int i = -4; i <= 4; i++)
            k.push_back({ i * r, std::exp(-(double)(i * i) / 8.0) });
      else
         for (int i = -4; i <= 4; i++)
            k.push_back({ i * r, 1.0 });
      for (const Tap& t : k)
         total += t.w;
      for (Tap& t : k)
         t.w /= total;
      return k;
   }

   double KernelSigma(const std::vector<Tap>& k)
   {
      double v = 0.0;
      for (const Tap& t : k)
         v += t.w * t.off * t.off;
      return std::sqrt(v);
   }

   // Effective sigma of an edge response row: the pixel differences are the kernel.
   double EdgeSigma(const std::vector<float>& px, int y)
   {
      double sum = 0.0, mean = 0.0, var = 0.0;
      std::vector<double> d((size_t)kW - 1);
      for (int x = 0; x < kW - 1; x++)
      {
         d[(size_t)x] = (double)px[((size_t)y * kW + x + 1) * 4] - px[((size_t)y * kW + x) * 4];
         sum += d[(size_t)x];
      }
      if (sum < 1e-6)
         return 0.0;
      for (int x = 0; x < kW - 1; x++)
         mean += d[(size_t)x] / sum * (x + 0.5);
      for (int x = 0; x < kW - 1; x++)
         var += d[(size_t)x] / sum * (x + 0.5 - mean) * (x + 0.5 - mean);
      return std::sqrt(std::max(var, 0.0));
   }

   // ------------------------------------------------------------------ BLUR
   void AuditBlur(Flags& f, bool& ok)
   {
      struct Kind { const char* def; int kind; const char* label; };
      const Kind kinds[3] = { { "gaussianblur", 0, "Gaussian Classic" }, { "gaussianblur", 1, "Gaussian Smooth" }, { "boxblur", 2, "Box" } };
      for (const Kind& k : kinds)
      {
         const FilterDef* def = FindDef(k.def);
         if (def == nullptr)
         {
            printf("COMPAUDIT FAIL BLUR no def %s\n", k.def);
            ok = false;
            continue;
         }
         TexSource src;
         src.Set(EdgeImage());
         for (double r : { 1.0, 2.0, 4.0 })
         {
            FilterNode n(*def);
            n.BeforeLoadParams(); // encoded mode: the kernel reference is on encoded values
            n.Input().Connect(&src);
            n.SetParamValue(0, 0, (float)r);
            if (k.kind != 2)
               n.SetParamValue(1, 0, (float)k.kind);
            n.CookIfNeeded(gFrame++);
            std::vector<float> px;
            if (n.GetOutputTexture() == 0 || !ReadSized(n.GetOutputTexture(), kW, kH, px))
            {
               printf("COMPAUDIT FAIL BLUR %s no output\n", k.label);
               ok = false;
               return;
            }
            const std::vector<Tap> ker = KernelOf(k.kind, r);
            // Reference: horizontal pass of the shader kernel on the edge, clamped at the border (the vertical pass of a
            // vertically constant image changes nothing).
            double worst = 0.0;
            const int y = kH / 2;
            for (int x = 0; x < kW; x++)
            {
               double v = 0.0;
               for (const Tap& t : ker)
               {
                  // GL_LINEAR sampling at texel centres: an integer tap hits one texel, a fractional tap blends two.
                  const double pos = x + t.off;
                  const int x0 = (int)std::floor(pos);
                  const double fr = pos - x0;
                  auto at = [&](int xi) { return std::clamp(xi, 0, kW - 1) >= kW / 2 ? 1.0 : 0.0; };
                  v += t.w * (at(x0) * (1.0 - fr) + at(x0 + 1) * fr);
               }
               worst = std::max(worst, std::fabs(v - (double)px[((size_t)y * kW + x) * 4]));
            }
            const double sigNom = KernelSigma(ker);
            const double sigEff = EdgeSigma(px, y);
            // The slider's nominal sigma: exp(-d^2/8) is a gaussian with sigma 2 r (box: the 9-tap variance).
            const double sigWanted = k.kind == 2 ? sigNom : 2.0 * r;
            printf("COMPAUDIT BLUR %-16s r %.0f: vs shader kernel %.2f/255 | sigma measured %.2f, kernel %.2f, slider implies %.2f px\n",
                   k.label, r, worst * 255.0, sigEff, sigNom, sigWanted);
            if (worst > 1.5 / 255.0)
               f.Flag("BLUR", std::string(k.label) + " differs from its own kernel, worst (x/255)", worst * 255.0);
            if (k.kind != 2 && std::fabs(sigEff / sigWanted - 1.0) > 0.10)
               f.Flag("BLUR", std::string(k.label) + " r " + std::to_string((int)r) + " sigma off the gaussian the radius implies, %",
                      (sigEff / sigWanted - 1.0) * 100.0);
         }
         // Flat image: nothing may be gained or lost.
         TexSource flat;
         flat.Set(Solid(kW, kH, 128));
         FilterNode n(*def);
         n.BeforeLoadParams();
         n.Input().Connect(&flat);
         n.SetParamValue(0, 0, 4.0f);
         if (k.kind != 2)
            n.SetParamValue(1, 0, (float)k.kind);
         n.CookIfNeeded(gFrame++);
         std::vector<float> px;
         if (n.GetOutputTexture() != 0 && ReadSized(n.GetOutputTexture(), kW, kH, px))
         {
            double e = 0.0;
            for (size_t i = 0; i < (size_t)kW * kH; i++)
               e = std::max(e, std::fabs((double)px[i * 4] - 128.0 / 255.0));
            printf("COMPAUDIT BLUR %-16s flat 128 stays: worst %.2f / 255\n", k.label, e * 255.0);
            if (e > 1.5 / 255.0)
               f.Flag("BLUR", std::string(k.label) + " changes a flat image, worst (x/255)", e * 255.0);
         }
      }

      // Cost against radius at 1024^2, Gaussian Smooth.
      if (const FilterDef* def = FindDef("gaussianblur"))
      {
         TexSource big(1024, 1024);
         big.Set(Solid(1024, 1024, 100));
         printf("COMPAUDIT BLUR cost at 1024^2 (Gaussian Smooth, ms per cook):");
         for (float r : { 1.0f, 2.0f, 5.0f, 10.0f })
         {
            FilterNode n(*def);
            n.Input().Connect(&big);
            n.SetParamValue(0, 0, r);
            n.SetParamValue(1, 0, 1.0f);
            n.CookIfNeeded(gFrame++);
            glFinish();
            const auto t0 = std::chrono::steady_clock::now();
            const int reps = 10;
            for (int i = 0; i < reps; i++)
            {
               n.SetParamValue(0, 0, r + 0.001f * (float)(i + 1)); // changes the signature, forces a recook
               n.CookIfNeeded(gFrame++);
            }
            glFinish();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
            printf("  r%.0f %.2f", r, ms);
         }
         printf("\n");
      }
   }

   // ---------------------------------------------------------------- COLOUR
   void AuditColour(Flags& f, bool& ok)
   {
      auto toLinear = [](double e) { return e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4); };
      auto toEnc = [](double l) { return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055; };
      (void)toLinear;
      const double linearAnswer = toEnc(0.5); // 0.735: the mean of black and white, in linear light

      // Blur: fine checker, large radius, the centre pixel.
      double blurMid = -1.0;
      {
         std::vector<unsigned char> px((size_t)kW * kH * 4, 255);
         for (int y = 0; y < kH; y++)
            for (int x = 0; x < kW; x++)
               px[((size_t)y * kW + x) * 4] = px[((size_t)y * kW + x) * 4 + 1] = px[((size_t)y * kW + x) * 4 + 2] =
                  ((x + y) & 1) ? 255 : 0;
         TexSource src;
         src.Set(px);
         if (const FilterDef* def = FindDef("gaussianblur"))
         {
            FilterNode n(*def);
            n.Input().Connect(&src);
            n.SetParamValue(0, 0, 4.0f);
            n.SetParamValue(1, 0, 1.0f);
            n.CookIfNeeded(gFrame++);
            std::vector<float> out;
            if (n.GetOutputTexture() != 0 && ReadSized(n.GetOutputTexture(), kW, kH, out))
               blurMid = out[((size_t)(kH / 2) * kW + kW / 2) * 4];
         }
      }
      // Blend: black over white at 50 %.
      double blendMid = -1.0;
      {
         BlendRig rig(false);
         rig.a.Set(Solid(kW, kH, 255));
         rig.b.Set(Solid(kW, kH, 0));
         std::vector<float> out;
         if (rig.Run(0, 0.5f, out))
            blendMid = out[((size_t)(kH / 2) * kW + kW / 2) * 4];
      }
      // Ramp: the midpoint of black to white.
      double rampMid = -1.0;
      {
         TexSource src;
         src.Set(Solid(kW, kH, 128));
         ColorRampNode n;
         n.Input().Connect(&src);
         n.stopCount = 2;
         n.stopPos[0] = 0.0f; n.stopPos[1] = 1.0f;
         for (int c = 0; c < 3; c++) { n.stopColor[0][c] = 0.0f; n.stopColor[1][c] = 1.0f; }
         n.interpMode = ColorRampNode::kLinear;
         n.MarkDirty();
         n.CookIfNeeded(gFrame++);
         std::vector<float> out;
         if (n.GetOutputTexture() != 0 && ReadSized(n.GetOutputTexture(), kW, kH, out))
            rampMid = out[0] - 128.0 / 255.0; // ramp(0.502) against the grey itself: ~0 when the ramp is the identity
      }
      auto space = [&](double v) { return std::fabs(v - 0.5) < 0.02 ? "encoded (gamma) values" : std::fabs(v - linearAnswer) < 0.02 ? "linear light" : "neither"; };
      printf("COMPAUDIT COLOUR blur of a 0/1 checker: %.3f -> %s (0.500 encoded, %.3f linear)\n", blurMid, space(blurMid), linearAnswer);
      printf("COMPAUDIT COLOUR Blend black over white at 50 %%: %.3f -> %s\n", blendMid, space(blendMid));
      printf("COMPAUDIT COLOUR Color Ramp black->white at t = 128/255: offset from the grey itself %.4f (encoded interpolation is exact)\n", rampMid);
      if (blurMid < 0 || blendMid < 0)
      {
         printf("COMPAUDIT FAIL COLOUR no output\n");
         ok = false;
         return;
      }
      if (std::fabs(blurMid - blendMid) > 0.02)
         f.Flag("COLOUR", "blur and blend work in different colour spaces, difference", std::fabs(blurMid - blendMid));
      if (std::fabs(rampMid) > 1.5 / 255.0)
         f.Flag("COLOUR", "ramp interpolation is not in encoded space, offset (x/255)", rampMid * 255.0);
   }

   // ----------------------------------------------------------------- FLOW
   // A band-limited pattern (wavelengths 24 to 61 px) evaluated at (x - sx, y - sy), so the
   // shift is exact at any sub-pixel amount; row index grows with v, so a positive sy is "up".
   std::vector<unsigned char> FlowPattern(int w, int h, double sx, double sy)
   {
      struct Wave { double kx, ky, ph, a; };
      static const Wave waves[5] = {
         { 0.2618, 0.0914, 0.4, 0.22 }, { -0.1103, 0.1675, 1.9, 0.20 }, { 0.0731, -0.2205, 3.1, 0.18 },
         { 0.1349, 0.1871, 5.2, 0.15 }, { -0.1917, -0.0427, 2.3, 0.14 } };
      std::vector<unsigned char> px((size_t)w * h * 4);
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            double v = 0.5;
            for (const Wave& wv : waves)
               v += wv.a * std::sin(wv.kx * (x - sx) + wv.ky * (y - sy) + wv.ph);
            const unsigned char c = (unsigned char)std::lround(std::clamp(v, 0.0, 1.0) * 255.0);
            unsigned char* o = &px[((size_t)y * w + x) * 4];
            o[0] = o[1] = o[2] = c;
            o[3] = 255;
         }
      return px;
   }

   void AuditFlow(Flags& f, bool& ok)
   {
      const int w = 256, h = 192, margin = 24;
      struct Case { double sx, sy; };
      const Case cases[] = { { 0.0, 0.0 }, { 0.5, 0.0 }, { -0.7, 0.4 }, { 1.5, -1.0 }, { 3.2, 2.1 }, { 7.0, -4.5 } };
      for (int res : { 0, 1, 2 })
      {
         double worst = 0.0;
         for (const Case& c : cases)
         {
            TexSource src(w, h);
            OpticalFlowNode fl;
            fl.resolution = res;
            fl.Input().Connect(&src);
            src.Set(FlowPattern(w, h, 0.0, 0.0));
            fl.CookIfNeeded(gFrame++);
            src.Set(FlowPattern(w, h, c.sx, c.sy));
            fl.CookIfNeeded(gFrame++);
            std::vector<float> px;
            if (fl.GetOutputTexture() == 0 || !ReadSized(fl.GetOutputTexture(), fl.GetOutputWidth(), fl.GetOutputHeight(), px))
            {
               ok = false;
               printf("COMPAUDIT FLOW no output texture\n");
               return;
            }
            const int ow = fl.GetOutputWidth(), oh = fl.GetOutputHeight();
            const double to = (double)w / ow; // output pixels to input pixels, for the margin
            double sum = 0.0, mx = 0.0, mean0 = 0.0, mean1 = 0.0;
            long n = 0;
            for (int y = (int)(margin / to); y < oh - (int)(margin / to); y++)
               for (int x = (int)(margin / to); x < ow - (int)(margin / to); x++)
               {
                  const float* p = &px[((size_t)y * ow + x) * 4];
                  const double ex = p[0] - c.sx, ey = p[1] - c.sy;
                  const double e = std::sqrt(ex * ex + ey * ey);
                  sum += e;
                  mx = std::max(mx, e);
                  mean0 += p[0];
                  mean1 += p[1];
                  n++;
              }
            const double epe = n > 0 ? sum / n : 1e9;
            worst = std::max(worst, epe);
            printf("COMPAUDIT FLOW %s move (%+.1f, %+.1f) px: mean (%+.3f, %+.3f), mean endpoint error %.3f px, worst pixel %.2f px\n",
                   res == 0 ? "full " : res == 1 ? "half " : "quart", c.sx, c.sy, n ? mean0 / n : 0.0, n ? mean1 / n : 0.0, epe, mx);
            if (epe > 0.5)
               f.Flag("FLOW", std::string(res == 0 ? "full" : res == 1 ? "half" : "quarter") + " resolution, move " + std::to_string(c.sx) + "," + std::to_string(c.sy)
                      + ": mean endpoint error (px)", epe);
         }
         printf("COMPAUDIT FLOW %s worst mean endpoint error %.3f px (bar 0.5)\n", res == 0 ? "full " : res == 1 ? "half " : "quart", worst);
      }
      for (int win : { 3, 4, 5 })
         for (int it : { 2, 3 })
            for (int lv : { 3, 4 })
            {
               TexSource src(w, h);
               OpticalFlowNode fl;
               fl.resolution = 0;
               fl.window = win;
               fl.iterations = it;
               fl.levels = lv;
               fl.Input().Connect(&src);
               src.Set(FlowPattern(w, h, 0.0, 0.0));
               fl.CookIfNeeded(gFrame++);
               src.Set(FlowPattern(w, h, 3.2, 2.1));
               fl.CookIfNeeded(gFrame++);
               std::vector<float> px;
               if (!ReadSized(fl.GetOutputTexture(), fl.GetOutputWidth(), fl.GetOutputHeight(), px))
                  continue;
               double sum = 0.0;
               long n = 0;
               for (int y = margin; y < h - margin; y++)
                  for (int x = margin; x < w - margin; x++, n++)
                     sum += std::hypot(px[((size_t)y * w + x) * 4] - 3.2, px[((size_t)y * w + x) * 4 + 1] - 2.1);
               printf("COMPAUDIT FLOW sweep window %d iterations %d levels %d: error %.3f px\n", win, it, lv, sum / n);
            }
      // Cost at 1080p, one frame of motion per cook.
      for (int res : { 0, 1, 2 })
      {
         const int W = 1920, H = 1080;
         TexSource src(W, H);
         OpticalFlowNode fl;
         fl.resolution = res;
         fl.Input().Connect(&src);
         src.Set(FlowPattern(W, H, 0.0, 0.0));
         fl.CookIfNeeded(gFrame++);
         src.Set(FlowPattern(W, H, 2.0, 1.0));
         fl.CookIfNeeded(gFrame++);
         glFinish();
         const int reps = 20;
         const auto t0 = std::chrono::steady_clock::now();
         for (int i = 0; i < reps; i++)
            fl.CookIfNeeded(gFrame++);
         glFinish();
         const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
         printf("COMPAUDIT FLOW 1080p %s: %.2f ms per cook (bar 2 ms)\n", res == 0 ? "full   " : res == 1 ? "half   " : "quarter", ms);
      }
   }

   // ----------------------------------------------------------- FLUID
   void AuditVolume(Flags& f, bool& ok)
   {
      const int w = 320, h = 180;
      std::vector<unsigned char> pic((size_t)w * h * 4, 255);
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            unsigned char* p = &pic[((size_t)y * w + x) * 4];
            p[0] = (unsigned char)(x * 255 / (w - 1));
            p[1] = (unsigned char)(y * 255 / (h - 1));
            p[2] = 128;
         }
      TexSource src(w, h);
      src.Set(pic);
      auto cook = [&](VolumeNode& v, std::vector<float>& px)
      {
         v.Input().Connect(&src);
         v.CookIfNeeded(gFrame++);
         return ReadSized(v.GetOutputTexture(), v.GetOutputWidth(), v.GetOutputHeight(), px);
      };
      auto mean = [](const std::vector<float>& px)
      {
         double s = 0.0;
         size_t n = 0;
         for (size_t i = 0; i + 3 < px.size(); i += 4, n++)
            s += (px[i] + px[i + 1] + px[i + 2]) / 3.0;
         return n ? s / (double)n : 0.0;
      };
      for (int shape = 0; shape < VolumeNode::kShapeCount; shape++)
      {
         std::vector<float> a, b;
         VolumeNode v1, v2;
         v1.shape = v2.shape = shape;
         v1.centerY = v2.centerY = shape == 0 ? -0.5f : 0.0f;
         const bool okA = cook(v1, a), okB = cook(v2, b);
         bool finite = okA && okB;
         for (float x : a)
            if (!std::isfinite(x))
               finite = false;
         if (!finite)
            f.Flag("VOLUME", "non-finite output, shape " + std::to_string(shape), 1.0);
         double diff = 0.0;
         for (size_t i = 0; i < a.size() && i < b.size(); i++)
            diff = std::max(diff, (double)std::fabs(a[i] - b[i]));
         printf("COMPAUDIT VOLUME shape %d: mean %.4f, same-settings difference %.6f\n", shape, mean(a), diff);
         if (diff > 1e-6)
            f.Flag("VOLUME", "same settings differ, shape " + std::to_string(shape), diff);

         VolumeNode zero;
         zero.shape = shape;
         zero.density = 0.0f;
         std::vector<float> z;
         cook(zero, z);
         double zd = 0.0;
         for (size_t i = 0; i + 3 < z.size(); i += 4)
         {
            const size_t px = i / 4;
            const double r = pic[px * 4] / 255.0, g = pic[px * 4 + 1] / 255.0, bl = pic[px * 4 + 2] / 255.0;
            zd = std::max(zd, std::max(std::fabs(z[i] - r), std::max(std::fabs(z[i + 1] - g), std::fabs(z[i + 2] - bl))));
         }
         printf("COMPAUDIT VOLUME shape %d: zero density vs backdrop, worst %.5f\n", shape, zd);
         if (zd > 2e-3)
            f.Flag("VOLUME", "zero density is not the backdrop, shape " + std::to_string(shape), zd);

         // A dark backdrop shows transmittance: more density means less of it comes through.
         double prev = 2.0;
         for (float d : { 0.5f, 2.0f, 8.0f })
         {
            VolumeNode v;
            v.shape = shape;
            v.centerY = shape == 0 ? -0.5f : 0.0f;
            v.density = d;
            v.color[0] = v.color[1] = v.color[2] = 0.0f;
            v.ambient = 0.0f;
            std::vector<float> px;
            cook(v, px);
            const double m = mean(px);
            printf("COMPAUDIT VOLUME shape %d: density %.1f gives mean %.4f (all-black haze)\n", shape, d, m);
            if (m > prev + 1e-4)
               f.Flag("VOLUME", "more density let more through, shape " + std::to_string(shape), m - prev);
            prev = m;
         }
      }
      for (int steps : { 24, 96 })
      {
         const int tw = 1920, th = 1080;
         std::vector<unsigned char> big((size_t)tw * th * 4, 128);
         TexSource bsrc(tw, th);
         bsrc.Set(big);
         VolumeNode v;
         v.shape = VolumeNode::kCloud;
         v.centerY = 0.0f;
         v.steps = steps;
         v.Input().Connect(&bsrc);
         v.CookIfNeeded(gFrame++);
         glFinish();
         const auto t0 = std::chrono::steady_clock::now();
         const int reps = 5;
         for (int i = 0; i < reps; i++)
            v.CookIfNeeded(gFrame++);
         glFinish();
         const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
         printf("COMPAUDIT VOLUME 1080p, %d steps: %.2f ms per frame\n", steps, ms);
      }
      (void)ok;
   }


   // --- HAP: the reader and decoder on hand-built QuickTime files, then the node showing them. ---
   std::string HapAtom(const char* type, const std::string& body)
   {
      std::string s;
      const uint32_t n = (uint32_t)(body.size() + 8);
      for (int i = 3; i >= 0; i--)
         s.push_back((char)((n >> (8 * i)) & 0xFF));
      s.append(type, 4);
      return s + body;
   }
   std::string HapU32(uint32_t v)
   {
      std::string s;
      for (int i = 3; i >= 0; i--)
         s.push_back((char)((v >> (8 * i)) & 0xFF));
      return s;
   }
   std::string HapSection(uint8_t type, const std::string& body)
   {
      std::string s;
      s.push_back((char)(body.size() & 0xFF));
      s.push_back((char)((body.size() >> 8) & 0xFF));
      s.push_back((char)((body.size() >> 16) & 0xFF));
      s.push_back((char)type);
      return s + body;
   }
   // The moov atom of one HAP track whose samples follow each other in one chunk at offset 8.
   // Each sample lasts `delta` ticks of a `timescale` clock (default 0.5 s).
   std::string HapMoov(const char* codec, int w, int h, const std::vector<uint32_t>& frameSizes,
                       uint32_t timescale = 1000, uint32_t delta = 500)
   {
      std::string sizes;
      for (uint32_t sz : frameSizes)
         sizes += HapU32(sz);
      const uint32_t n = (uint32_t)frameSizes.size();
      std::string entry = HapU32(86) + std::string(codec, 4) + std::string(6, '\0') + std::string("\0\1", 2) +
                          std::string(16, '\0') + std::string({ (char)(w >> 8), (char)(w & 255), (char)(h >> 8), (char)(h & 255) }) +
                          std::string(50, '\0');
      std::string stbl = HapAtom("stsd", HapU32(0) + HapU32(1) + entry) +
                         HapAtom("stts", HapU32(0) + HapU32(1) + HapU32(n) + HapU32(delta)) +
                         HapAtom("stsz", HapU32(0) + HapU32(0) + HapU32(n) + sizes) +
                         HapAtom("stsc", HapU32(0) + HapU32(1) + HapU32(1) + HapU32(n) + HapU32(1)) +
                         HapAtom("stco", HapU32(0) + HapU32(1) + HapU32(8));
      std::string mdhd = HapU32(0) + HapU32(0) + HapU32(0) + HapU32(timescale) + HapU32(n * delta) + std::string(4, '\0');
      std::string hdlr = HapU32(0) + HapU32(0) + "vide" + std::string(12, '\0') + std::string(1, '\0');
      return HapAtom("moov", HapAtom("trak", HapAtom("mdia", HapAtom("mdhd", mdhd) + HapAtom("hdlr", hdlr) +
                                                                  HapAtom("minf", HapAtom("stbl", stbl)))));
   }
   // A QuickTime file of one HAP track: samples at 0.5 s apart, one chunk.
   bool WriteHapMov(const std::string& path, const char* codec, int w, int h, const std::vector<std::string>& frames)
   {
      std::string mdat;
      std::vector<uint32_t> sizes;
      for (const auto& fr : frames)
      {
         mdat += fr;
         sizes.push_back((uint32_t)fr.size());
      }
      std::ofstream out(path, std::ios::binary);
      const std::string file = HapAtom("mdat", mdat) + HapMoov(codec, w, h, sizes);
      out.write(file.data(), (std::streamsize)file.size());
      return (bool)out;
   }

   // --- HAPGEN: writes a 4096x2160 Hap1 test clip for the 3.12 bench (named explicitly, never in a full run). ---
   // Frames are random DXT1 blocks wrapped in a literal-only snappy stream: the largest frames a Hap1 clip of
   // this size can have, so decode and upload cost is the worst case. It tests throughput, not an encoder's output.
   // INFINITE_HAPGEN_OUT = path (default hap4k.mov in the temp folder), INFINITE_HAPGEN_FRAMES = count (default 90).
   void AuditHapGen(Flags& f)
   {
      const int w = 4096, h = 2160;
      const char* outEnv = getenv("INFINITE_HAPGEN_OUT");
      const std::string path = (outEnv && *outEnv) ? outEnv : (std::filesystem::temp_directory_path() / "hap4k.mov").string();
      const char* nEnv = getenv("INFINITE_HAPGEN_FRAMES");
      const int count = (nEnv && atoi(nEnv) > 0) ? atoi(nEnv) : 90;
      const size_t texBytes = Hap::TextureBytes(Hap::Format::Dxt1, w, h);

      // Snappy header: the length as a varint, then literal runs of up to 64 KB.
      std::string head;
      for (size_t v = texBytes;;)
      {
         uint8_t b = (uint8_t)(v & 0x7F);
         v >>= 7;
         if (v) b |= 0x80;
         head.push_back((char)b);
         if (!v) break;
      }
      const size_t run = 65536;
      const size_t runs = (texBytes + run - 1) / run;
      const size_t snapSize = head.size() + texBytes + runs * 3;
      std::vector<uint8_t> tex(texBytes);
      std::string frame;
      frame.reserve(snapSize + 8);
      std::vector<uint32_t> sizes;
      const auto t0 = std::chrono::steady_clock::now();
      std::ofstream out(path, std::ios::binary);
      const uint64_t mdatBytes = (uint64_t)count * (snapSize + 4) + 8;
      if (!out || mdatBytes > 0xFFFFFFFFull)
      {
         printf("COMPAUDIT HAPGEN cannot write %s (or the clip would pass 4 GB)\n", path.c_str());
         f.Flag("HAPGEN", "cannot write the clip", 1.0);
         return;
      }
      out.write(HapU32((uint32_t)mdatBytes).data(), 4);
      out.write("mdat", 4);
      uint64_t seed = 0x9E3779B97F4A7C15ull;
      for (int i = 0; i < count; i++)
      {
         for (size_t p = 0; p + 8 <= texBytes; p += 8)
         {
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
            memcpy(&tex[p], &seed, 8);
         }
         frame = head;
         for (size_t p = 0; p < texBytes; p += run)
         {
            const size_t n = std::min(run, texBytes - p);
            frame.push_back((char)0xF4); // literal, length in the next two bytes
            frame.push_back((char)((n - 1) & 0xFF));
            frame.push_back((char)(((n - 1) >> 8) & 0xFF));
            frame.append((const char*)&tex[p], n);
         }
         const std::string sec = HapSection(0xBB, frame);
         // Frames here are over 16 MB only if the picture is far larger than 4K; the 3-byte length holds 4K.
         out.write(sec.data(), (std::streamsize)sec.size());
         sizes.push_back((uint32_t)sec.size());
      }
      const std::string moov = HapMoov("Hap1", w, h, sizes, 30000, 1000);
      out.write(moov.data(), (std::streamsize)moov.size());
      out.close();
      const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

      // Read it back: the first and last frame must decode to the size the clip says.
      Hap::Reader r;
      std::string err;
      std::vector<uint8_t> back;
      const bool readable = r.Open(path, err) && r.Width() == w && r.Height() == h && r.FrameCount() == count &&
                            r.ReadFrame(0, back, err) && back.size() == texBytes &&
                            r.ReadFrame(count - 1, back, err) && back.size() == texBytes && memcmp(back.data(), tex.data(), texBytes) == 0;
      printf("COMPAUDIT HAPGEN wrote %s: %d frames of %dx%d, %.1f MB, %.1f s; reads back %d (%s)\n", path.c_str(), count, w, h,
             (double)(mdatBytes) / 1048576.0, sec, readable ? 1 : 0, err.c_str());
      if (!readable)
         f.Flag("HAPGEN", "clip does not read back", 1.0);
   }

   void AuditHap(Flags& f, bool& ok)
   {
      // Snappy: a literal then two kinds of back-reference, and a corrupt stream.
      {
         const uint8_t a[] = { 12, 0x0C, 'a', 'b', 'c', 'd', 0x1E, 4, 0 };
         uint8_t out[12] = {};
         const bool good = Hap::SnappyDecompress(a, sizeof(a), out, 12) && memcmp(out, "abcdabcdabcd", 12) == 0;
         const uint8_t b[] = { 9, 0x08, 'x', 'y', 'z', 0x09, 3 };
         uint8_t out2[9] = {};
         const bool good2 = Hap::SnappyDecompress(b, sizeof(b), out2, 9) && memcmp(out2, "xyzxyzxyz", 9) == 0;
         const uint8_t c[] = { 12, 0x0C, 'a', 'b', 'c', 'd', 0x1E, 9, 0 };
         const bool refused = !Hap::SnappyDecompress(c, sizeof(c), out, 12);
         printf("COMPAUDIT HAP snappy: two-byte offset %d, one-byte offset %d, bad offset refused %d\n", good ? 1 : 0, good2 ? 1 : 0,
                refused ? 1 : 0);
         if (!good || !good2 || !refused)
            f.Flag("HAP", "snappy decode wrong", 1.0);
      }

      const std::string dir = AppPaths::AppSupportDir() + "/HapAudit";
      AppPaths::EnsureDir(dir);

      // DXT1, 16 x 8, solid red: raw, snappy and chunked frames must all give the same 64 bytes.
      std::string red;
      for (int b = 0; b < 8; b++)
         red += std::string({ (char)0x00, (char)0xF8, (char)0x00, (char)0xF8, 0, 0, 0, 0 });
      const std::string rawF = HapSection(0xAB, red);
      const std::string snapF = HapSection(0xBB, std::string({ 0x40, (char)0xF0, 63 }) + red);
      const std::string instr = HapSection(0x02, std::string({ 0x0A, 0x0A })) + HapSection(0x03, std::string({ 32, 0, 0, 0, 32, 0, 0, 0 }));
      const std::string chunkF = HapSection(0xCB, HapSection(0x01, instr) + red);
      const std::string red16 = dir + "/red.mov";
      WriteHapMov(red16, "Hap1", 16, 8, { rawF, snapF, chunkF });

      Hap::Reader r;
      std::string error;
      const bool probed = Hap::Reader::Probe(red16);
      const bool opened = r.Open(red16, error);
      printf("COMPAUDIT HAP file: probe %d, open %d, %dx%d, %d frames, %.2f s, format %d\n", probed ? 1 : 0, opened ? 1 : 0, r.Width(),
             r.Height(), r.FrameCount(), r.Duration(), (int)r.TextureFormat());
      if (!probed || !opened || r.Width() != 16 || r.Height() != 8 || r.FrameCount() != 3 || std::fabs(r.Duration() - 1.5) > 1e-6 ||
          r.TextureFormat() != Hap::Format::Dxt1)
         f.Flag("HAP", "the container was read wrong", 1.0);
      else
      {
         const int i0 = r.FrameIndexAt(0.1), i1 = r.FrameIndexAt(0.6), i2 = r.FrameIndexAt(99.0), ineg = r.FrameIndexAt(-1.0);
         printf("COMPAUDIT HAP index at 0.1 / 0.6 / 99 / -1 s: %d %d %d %d\n", i0, i1, i2, ineg);
         if (i0 != 0 || i1 != 1 || i2 != 2 || ineg != 0)
            f.Flag("HAP", "frame lookup by time is wrong", 1.0);
         for (int i = 0; i < 3; i++)
         {
            std::vector<uint8_t> px;
            const bool read = r.ReadFrame(i, px, error);
            const bool same = read && px.size() == red.size() && memcmp(px.data(), red.data(), red.size()) == 0;
            printf("COMPAUDIT HAP frame %d (%s): decoded %d, bytes match %d\n", i, i == 0 ? "raw" : i == 1 ? "snappy" : "chunked", read ? 1 : 0,
                   same ? 1 : 0);
            if (!same)
               f.Flag("HAP", "frame " + std::to_string(i) + " does not decode to the texture", 1.0);
         }
      }

      // A cut-off file must be refused, not crash.
      {
         std::ifstream in(red16, std::ios::binary);
         std::ostringstream ss;
         ss << in.rdbuf();
         const std::string whole = ss.str();
         std::ofstream cut(dir + "/cut.mov", std::ios::binary);
         cut.write(whole.data(), (std::streamsize)(whole.size() - 40));
         cut.close();
         const bool refused = !Hap::Reader::Probe(dir + "/cut.mov");
         printf("COMPAUDIT HAP cut-off file refused: %d\n", refused ? 1 : 0);
         if (!refused)
            f.Flag("HAP", "a cut-off file was accepted", 1.0);
      }

      // The node: show the DXT1 file, and a HAP Q file (grey), and read both back through a copy pass.
      const char* copyFrag = "#version 150\nin vec2 vUv;\nout vec4 fragColor;\nuniform sampler2D uSrc;\nvoid main(){ fragColor = texture(uSrc, vUv); }\n";
      const unsigned int copy = GLUtil::CompileProgram(copyFrag);
      auto show = [&](const std::string& path, std::vector<float>& px, int& w) -> bool
      {
         VideoSourceNode n;
         if (!n.Open(path))
            return false;
         bool got = false;
         for (int i = 0; i < 600 && !got; i++)
         {
            n.SeekTo(0.0);
            static int frameId = 1000;
            n.CookIfNeeded(++frameId);
            got = n.FrameUpdateCount() > 0;
            if (!got)
               std::this_thread::sleep_for(std::chrono::milliseconds(5));
         }
         if (!got)
            return false;
         w = n.GetOutputWidth();
         GLUtil::Fbo out;
         GLUtil::EnsureFbo(out, n.GetOutputWidth(), n.GetOutputHeight());
         const unsigned int tex = n.GetOutputTexture();
         GLUtil::RunShaderPass(out, copy, [&]()
         {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniform1i(glGetUniformLocation(copy, "uSrc"), 0);
         });
         unsigned int scratch = 0;
         const bool read = GLUtil::ReadTexturePixels(scratch, out.tex, out.w, out.h, px);
         GLUtil::DestroyFbo(out);
         if (scratch != 0)
            glDeleteFramebuffers(1, &scratch);
         return read;
      };
      {
         std::vector<float> px;
         int w = 0;
         const bool shown = copy != 0 && show(red16, px, w);
         const float rr = shown ? px[0] : -1.0f, gg = shown ? px[1] : -1.0f, bb = shown ? px[2] : -1.0f;
         printf("COMPAUDIT HAP node, DXT1 red: shown %d, width %d, pixel %.3f %.3f %.3f\n", shown ? 1 : 0, w, rr, gg, bb);
         if (!shown || w != 16 || rr < 0.95f || gg > 0.05f || bb > 0.05f)
            f.Flag("HAP", "the node does not show the DXT1 clip", 1.0);
      }
      {
         // HAP Q: one 4x4 block, Y = 128 in alpha, Co and Cg near the offset: a mid grey.
         std::string blk = std::string({ (char)0x80, (char)0x80, 0, 0, 0, 0, 0, 0 }) + std::string({ (char)0x00, (char)0x84, (char)0x00, (char)0x84, 0, 0, 0, 0 });
         const std::string q = dir + "/q.mov";
         WriteHapMov(q, "HapY", 4, 4, { HapSection(0xAF, blk) });
         std::vector<float> px;
         int w = 0;
         const bool shown = copy != 0 && show(q, px, w);
         const float rr = shown ? px[0] : -1.0f, gg = shown ? px[1] : -1.0f, bb = shown ? px[2] : -1.0f;
         printf("COMPAUDIT HAP node, HAP Q grey: shown %d, pixel %.3f %.3f %.3f\n", shown ? 1 : 0, rr, gg, bb);
         if (!shown || std::fabs(rr - 0.5f) > 0.06f || std::fabs(gg - 0.5f) > 0.06f || std::fabs(bb - 0.5f) > 0.06f)
            f.Flag("HAP", "the node does not show the HAP Q clip", 1.0);
      }
      if (copy != 0)
         glDeleteProgram(copy);
      std::filesystem::remove_all(dir);
      if (f.n > 0)
         ok = false;
   }

   // Share bundle: cleaning, the zip round trip, unsafe names, and a real bundle of a shipped template opening again.
   void AuditShare(Flags& f)
   {
      const std::string text =
         "node 1 Image Source\n  s path /Users/jane/Pictures/cat.png\n  f speed 1\n"
         "node 2 Sound Player\n  s sampleFile C:\\\\Users\\\\bob\\\\kick.wav\n  s label from /Users/jane/x\n";
      std::vector<std::string> blanked;
      const std::string cleaned = ShareBundle::CleanPatchText(text, &blanked);
      const bool noUser = cleaned.find("jane") == std::string::npos && cleaned.find("bob") == std::string::npos;
      printf("COMPAUDIT SHARE clean: %zu media names, user names gone %d, keeps speed %d\n", blanked.size(), noUser ? 1 : 0,
             cleaned.find("f speed 1") != std::string::npos ? 1 : 0);
      if (!noUser)
         f.Flag("SHARE", "a user name is still in the cleaned patch", 1.0);
      if (blanked.size() != 2)
         f.Flag("SHARE", "wrong number of blanked media names", (double)blanked.size());

      const std::string dir = AppPaths::AppSupportDir() + "/ShareAudit";
      AppPaths::EnsureDir(dir);
      const std::string zip = dir + "/t.infshare";
      std::string error;
      if (!ShareBundle::WriteZip(zip, { { "patch.inf", cleaned }, { "share.json", "{}" } }, error))
         f.Flag("SHARE", "zip write failed: " + error, 1.0);
      const std::string out = dir + "/out";
      std::filesystem::remove_all(out);
      const bool extracted = ShareBundle::ExtractZip(zip, out, error);
      std::ifstream back(out + "/patch.inf", std::ios::binary);
      std::ostringstream ss;
      ss << back.rdbuf();
      printf("COMPAUDIT SHARE zip round trip: extracted %d, identical %d\n", extracted ? 1 : 0, ss.str() == cleaned ? 1 : 0);
      if (!extracted || ss.str() != cleaned)
         f.Flag("SHARE", "zip round trip changed the patch", 1.0);

      const std::string evil = dir + "/evil.infshare";
      ShareBundle::WriteZip(evil, { { "../escape.txt", "x" } }, error);
      const bool refused = !ShareBundle::ExtractZip(evil, dir + "/evilout", error);
      printf("COMPAUDIT SHARE unsafe path refused: %d\n", refused ? 1 : 0);
      if (!refused)
         f.Flag("SHARE", "an entry climbing out of the folder was extracted", 1.0);
      std::filesystem::remove_all(dir);
   }

   // A real bundle of a shipped template, built through the app's own headless modes, unpacked, then validated.
   void AuditBundle(Flags& f)
   {
      const std::string dir = AppPaths::AppSupportDir() + "/BundleAudit";
      std::filesystem::remove_all(dir);
      AppPaths::EnsureDir(dir);
      const std::string src = BundledResourcePath("templates/compositing.inf");
      std::string message;
      const auto t0 = std::chrono::steady_clock::now();
      const bool built = BuildShareBundleBlocking(src, dir + "/b.infshare", "Audit", dir + "/work", message);
      const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      printf("COMPAUDIT BUNDLE built %d in %.1f s: %s\n", built ? 1 : 0, sec, message.c_str());
      std::string error;
      const bool extracted = built && ShareBundle::ExtractZip(dir + "/b.infshare", dir + "/open", error);
      auto size = [&](const char* n) { std::error_code ec; return extracted ? (long long)std::filesystem::file_size(dir + "/open/" + n, ec) : -1LL; };
      printf("COMPAUDIT BUNDLE parts: patch %lld bytes, thumbnail %lld, preview %lld, meta %lld\n", size("patch.inf"),
             size("thumbnail.png"), size("preview.mp4"), size("share.json"));
      // The child keeps configuration and drops every self-test switch (B-A-002: Windows kept them all,
      // macOS/Linux dropped four by name). This audit runs with INFINITE_COMPAUDIT set, so the build
      // above is the end-to-end proof; these pin the rule itself.
      const bool envRule = !SelfChildKeepsEnv("INFINITE_COMPAUDIT") && !SelfChildKeepsEnv("INFINITE_FIELDTEST") &&
                           !SelfChildKeepsEnv("INFINITE_EXITAFTER") && !SelfChildKeepsEnv("INFINITE_OSC_PORT") &&
                           SelfChildKeepsEnv("INFINITE_EXTENSIONS_DIR") && SelfChildKeepsEnv("PATH") &&
                           SelfChildKeepsEnv("HOME");
      printf("COMPAUDIT BUNDLE child env rule: %d\n", envRule ? 1 : 0);
      if (!envRule)
         f.Flag("BUNDLE", "self-relaunch keeps a test switch or drops configuration", 1.0);
      if (!built || size("patch.inf") <= 0 || size("thumbnail.png") <= 0 || size("share.json") <= 0)
         f.Flag("BUNDLE", "bundle is missing a part", 1.0);
      if (extracted)
      {
         const int rc = std::system(SelfCommand({ "--validate", dir + "/open/patch.inf" }).c_str());
         printf("COMPAUDIT BUNDLE unpacked patch validates: %d (exit %d)\n", rc == 0 ? 1 : 0, rc);
         if (rc != 0)
            f.Flag("BUNDLE", "unpacked patch does not validate", (double)rc);
      }
      std::filesystem::remove_all(dir);
   }

   void AuditFluid(Flags& f, bool& ok)
   {
      const int w = 256, h = 192;
      std::vector<unsigned char> pic((size_t)w * h * 4, 255), disc((size_t)w * h * 4, 255);
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            unsigned char* p = &pic[((size_t)y * w + x) * 4];
            p[0] = (unsigned char)(x * 255 / (w - 1));
            p[1] = (unsigned char)(y * 255 / (h - 1));
            p[2] = ((x / 16 + y / 16) & 1) ? 220 : 40;
            const double dx = x - 150.0, dy = y - 90.0;
            const unsigned char v = dx * dx + dy * dy < 20.0 * 20.0 ? 255 : 0;
            disc[((size_t)y * w + x) * 4 + 0] = disc[((size_t)y * w + x) * 4 + 1] = disc[((size_t)y * w + x) * 4 + 2] = v;
         }
      for (int withObstacle = 0; withObstacle < 2; withObstacle++)
         for (int iters : { 20, 60, 120 })
         {
            TexSource src(w, h), obs(w, h);
            src.Set(pic);
            obs.Set(disc);
            FluidNode fl;
            fl.resolution = 0;
            fl.stirrer = 1;
            fl.pressureIterations = iters;
            fl.Input().Connect(&src);
            if (withObstacle)
               fl.Obstacles().Connect(&obs);
            for (int i = 0; i < 30; i++)
               fl.CookIfNeeded(gFrame++);
            const float div = fl.MeasureDivergence();
            printf("COMPAUDIT FLUID divergence after projection, %s, %d iterations: %.5f\n",
                   withObstacle ? "disc obstacle" : "open", iters, div);
            if (iters == 120 && div > 1e-3f)
               f.Flag("FLUID", std::string("divergence after projection, ") + (withObstacle ? "obstacle" : "open") + ", 120 iterations", div);
         }
      {
         TexSource src(w, h);
         src.Set(pic);
         FluidNode fl;
         fl.resolution = 0;
         fl.stirrer = 0;
         fl.Input().Connect(&src);
         for (int i = 0; i < 10; i++)
            fl.CookIfNeeded(gFrame++);
         std::vector<float> px;
         double worst = 1e9;
         if (ReadSized(fl.GetOutputTexture(), fl.GetOutputWidth(), fl.GetOutputHeight(), px))
         {
            worst = 0.0;
            for (int y = 0; y < h; y++)
               for (int x = 0; x < w; x++)
                  for (int c = 0; c < 3; c++)
                     worst = std::max(worst, std::fabs((double)px[((size_t)y * w + x) * 4 + c] - pic[((size_t)y * w + x) * 4 + c] / 255.0));
         }
         printf("COMPAUDIT FLUID still picture, no stirring: worst difference %.4f\n", worst);
         if (worst > 2.0 / 255.0)
            f.Flag("FLUID", "still picture drifts from the input", worst);
      }
      {
         TexSource src(w, h);
         src.Set(pic);
         FluidNode fl;
         fl.resolution = 0;
         fl.stirrer = 1;
         fl.refresh = 0.0f;
         fl.Input().Connect(&src);
         for (int i = 0; i < 60; i++)
            fl.CookIfNeeded(gFrame++);
         std::vector<float> px;
         bool finite = ReadSized(fl.GetOutputTexture(), fl.GetOutputWidth(), fl.GetOutputHeight(), px);
         double mean = 0.0;
         for (float v : px)
         {
            if (!std::isfinite(v))
               finite = false;
            mean += v;
         }
         printf("COMPAUDIT FLUID 60 frames with no refresh: finite %s, mean level %.3f\n", finite ? "yes" : "NO", px.empty() ? 0.0 : mean / px.size());
         if (!finite)
            f.Flag("FLUID", "output has non-finite values after 60 frames", 1.0);
      }
      for (int res : { 0, 1, 2 })
      {
         const int W = 1920, H = 1080;
         TexSource src(W, H);
         src.Set(std::vector<unsigned char>((size_t)W * H * 4, 128));
         FluidNode fl;
         fl.resolution = res;
         fl.Input().Connect(&src);
         for (int i = 0; i < 3; i++)
            fl.CookIfNeeded(gFrame++);
         glFinish();
         const int reps = 20;
         const auto t0 = std::chrono::steady_clock::now();
         for (int i = 0; i < reps; i++)
            fl.CookIfNeeded(gFrame++);
         glFinish();
         const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
         printf("COMPAUDIT FLUID 1080p %s, 20 iterations: %.2f ms per cook (bar 3 ms)\n",
                res == 0 ? "full   " : res == 1 ? "half   " : "quarter", ms);
      }
   }

   // ----------------------------------------------------------- TIMEMACHINE
   std::vector<unsigned char> FlatGrey(int w, int h, int v)
   {
      std::vector<unsigned char> px((size_t)w * h * 4, (unsigned char)v);
      for (size_t i = 3; i < px.size(); i += 4)
         px[i] = 255;
      return px;
   }

   void AuditTimeMachine(Flags& f, bool& ok)
   {
      const int w = 128, h = 64;
      struct Case { const char* name; int length; bool invert; bool useMap; };
      const Case cases[] = { { "no map, length 60", 60, false, false }, { "ramp map, length 60", 60, false, true },
                             { "ramp map, inverted", 60, true, true }, { "ramp map, length 8", 8, false, true } };
      std::vector<unsigned char> ramp((size_t)w * h * 4, 255);
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++)
               ramp[((size_t)y * w + x) * 4 + c] = (unsigned char)std::lround(255.0 * (x + 0.5) / w);
      for (const Case& c : cases)
      {
         TexSource src(w, h), map(w, h);
         map.Set(ramp);
         TimeMachineNode tm;
         tm.resolution = 0;
         tm.length = c.length;
         tm.invert = c.invert;
         tm.Input().Connect(&src);
         if (c.useMap)
            tm.DelayMap().Connect(&map);
         const int frames = 70;
         for (int k = 0; k < frames; k++)
         {
            src.Set(FlatGrey(w, h, 3 * k));
            tm.CookIfNeeded(gFrame++);
            if (k == 0)
            {
               std::vector<float> first;
               if (tm.GetOutputTexture() != 0 && ReadSized(tm.GetOutputTexture(), tm.GetOutputWidth(), tm.GetOutputHeight(), first))
               {
                  double worst = 0.0;
                  for (size_t i = 0; i < first.size(); i += 4)
                     worst = std::max(worst, std::fabs(first[i] * 255.0));
                  if (worst > 1.0)
                     f.Flag("TIMEMACHINE", std::string(c.name) + ": first frame does not fill the ring, worst level", worst);
               }
            }
         }
         std::vector<float> px;
         if (tm.GetOutputTexture() == 0 || !ReadSized(tm.GetOutputTexture(), tm.GetOutputWidth(), tm.GetOutputHeight(), px))
         {
            ok = false;
            printf("COMPAUDIT TIMEMACHINE no output texture\n");
            return;
         }
         const int ow = tm.GetOutputWidth(), oh = tm.GetOutputHeight();
         double sum = 0.0, mx = 0.0;
         long n = 0;
         for (int y = 0; y < oh; y++)
            for (int x = 0; x < ow; x++, n++)
            {
               double g = (x + 0.5) / ow;
               if (c.invert)
                  g = 1.0 - g;
               const double expect = 3.0 * ((frames - 1) - g * (c.length - 1));
               const double e = std::fabs(px[((size_t)y * ow + x) * 4] * 255.0 - expect);
               sum += e;
               mx = std::max(mx, e);
            }
         printf("COMPAUDIT TIMEMACHINE %-22s mean error %.2f levels, worst %.2f (bar 1 / 3)\n", c.name, sum / n, mx);
         if (sum / n > 1.0 || mx > 3.0)
            f.Flag("TIMEMACHINE", std::string(c.name) + ": mean error (levels of 255)", sum / n);
      }
      for (int res : { 0, 1, 2 })
      {
         const int W = 1920, H = 1080;
         TexSource src(W, H);
         TimeMachineNode tm;
         tm.resolution = res;
         tm.Input().Connect(&src);
         src.Set(FlatGrey(W, H, 40));
         for (int i = 0; i < 4; i++)
            tm.CookIfNeeded(gFrame++);
         glFinish();
         const int reps = 20;
         const auto t0 = std::chrono::steady_clock::now();
         for (int i = 0; i < reps; i++)
            tm.CookIfNeeded(gFrame++);
         glFinish();
         const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
         printf("COMPAUDIT TIMEMACHINE 1080p %s: %.2f ms per cook, ring %.0f MB\n", res == 0 ? "full   " : res == 1 ? "half   " : "quarter", ms,
                60.0 * (W / (res == 0 ? 1 : res == 1 ? 2 : 4)) * (H / (res == 0 ? 1 : res == 1 ? 2 : 4)) * 4 / 1048576.0);
      }
   }

   // ------------------------------------------------------------- PROXIMITY
   class CloudSource : public INode, public IGeometrySource
   {
   public:
      std::vector<Particle> pts;
      unsigned long long rev = 1;
      unsigned int GetOutputTexture() override { return 0; }
      int GetOutputWidth() const override { return 0; }
      int GetOutputHeight() const override { return 0; }
      void CookIfNeeded(int) override {}
      const Mesh& GetMesh() override { return mEmpty; }
      unsigned long long MeshRevision() override { return 0; }
      Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
      Material GetMaterial() const override { return Material(); }
      const std::vector<Particle>* GetPointCloud() override { return &pts; }
      unsigned long long PointCloudRevision() override { return rev; }
   private:
      Mesh mEmpty;
   };

   void AuditProximity(Flags& f)
   {
      auto fill = [](size_t n, float extent, unsigned seed, std::vector<float>& xyz)
      {
         xyz.resize(n * 3);
         unsigned s = seed;
         for (float& v : xyz)
         {
            s = s * 1664525u + 1013904223u;
            v = ((s >> 8) & 0xFFFFFF) / 16777216.0f * extent;
         }
      };
      {
         std::vector<float> xyz;
         fill(2000, 10.0f, 7u, xyz);
         const float r = 0.5f;
         std::vector<std::pair<unsigned int, unsigned int>> links;
         ProximityLinesNode::FindLinks(xyz.data(), 2000, r, 16, 1000000, links);
         std::set<std::pair<unsigned int, unsigned int>> got(links.begin(), links.end()), want;
         std::vector<int> deg(2000, 0);
         for (unsigned int i = 0; i < 2000; i++)
            for (unsigned int j = i + 1; j < 2000; j++)
            {
               const float dx = xyz[i * 3] - xyz[j * 3], dy = xyz[i * 3 + 1] - xyz[j * 3 + 1], dz = xyz[i * 3 + 2] - xyz[j * 3 + 2];
               if (dx * dx + dy * dy + dz * dz <= r * r)
               {
                  want.insert({ i, j });
                  deg[i]++;
                  deg[j]++;
               }
            }
         const int maxDeg = *std::max_element(deg.begin(), deg.end());
         printf("COMPAUDIT PROXIMITY exact search: %zu links, brute force %zu, duplicates %zu, max degree %d (cap 16)\n",
                links.size(), want.size(), links.size() - got.size(), maxDeg);
         if (maxDeg > 16)
            f.Flag("PROXIMITY", "test cloud is too dense for the exactness check, max degree", maxDeg);
         else if (got != want || got.size() != links.size())
            f.Flag("PROXIMITY", "link set differs from brute force, links found", (double)links.size());
         // Reach: dense cloud, every point's 3 nearest inside the radius must be linked to it.
         fill(2000, 3.0f, 11u, xyz);
         ProximityLinesNode::FindLinks(xyz.data(), 2000, r, 3, 1000000, links);
         std::set<std::pair<unsigned int, unsigned int>> uniq(links.begin(), links.end());
         int missing = 0, outside = 0;
         for (const auto& l : links)
         {
            const float dx = xyz[l.first * 3] - xyz[l.second * 3], dy = xyz[l.first * 3 + 1] - xyz[l.second * 3 + 1],
                        dz = xyz[l.first * 3 + 2] - xyz[l.second * 3 + 2];
            if (l.first >= l.second || dx * dx + dy * dy + dz * dz > r * r)
               outside++;
         }
         for (unsigned int i = 0; i < 2000; i++)
         {
            std::vector<std::pair<float, unsigned int>> near;
            for (unsigned int j = 0; j < 2000; j++)
            {
               const float dx = xyz[i * 3] - xyz[j * 3], dy = xyz[i * 3 + 1] - xyz[j * 3 + 1], dz = xyz[i * 3 + 2] - xyz[j * 3 + 2];
               const float d = dx * dx + dy * dy + dz * dz;
               if (j != i && d <= r * r)
                  near.push_back({ d, j });
            }
            std::sort(near.begin(), near.end());
            for (size_t m = 0; m < near.size() && m < 3; m++)
               if (!uniq.count({ std::min(i, near[m].second), std::max(i, near[m].second) }))
                  missing++;
         }
         printf("COMPAUDIT PROXIMITY reach: %zu links, unique %zu, outside radius or unordered %d, nearest-3 links missing %d\n",
                links.size(), uniq.size(), outside, missing);
         if (uniq.size() != links.size() || outside > 0 || missing > 0)
            f.Flag("PROXIMITY", "reach, uniqueness or radius broken, missing nearest links", missing + outside);
         ProximityLinesNode::FindLinks(xyz.data(), 2000, r, 3, 500, links);
         if (links.size() > 500)
            f.Flag("PROXIMITY", "total cap broken, links", (double)links.size());
      }
      // Cost at 100k points, roughly six neighbours inside the radius.
      {
         const size_t n = 100000;
         const float extent = 40.0f, r = 1.0f;
         std::vector<float> xyz;
         fill(n, extent, 3u, xyz);
         std::vector<std::pair<unsigned int, unsigned int>> links;
         ProximityLinesNode::FindLinks(xyz.data(), n, r, 4, 200000, links);
         // Median of 21 timed calls: one call is short enough that a background process
         // waking up on a core moves it by a large fraction.
         std::vector<double> times;
         for (int i = 0; i < 21; i++)
         {
            const auto t0 = std::chrono::steady_clock::now();
            ProximityLinesNode::FindLinks(xyz.data(), n, r, 4, 200000, links);
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
         }
         std::sort(times.begin(), times.end());
         const double msFind = times[times.size() / 2];
         const double msFindBest = times.front();
         CloudSource src;
         src.pts.resize(n);
         for (size_t i = 0; i < n; i++)
         {
            src.pts[i].px = xyz[i * 3]; src.pts[i].py = xyz[i * 3 + 1]; src.pts[i].pz = xyz[i * 3 + 2];
         }
         ProximityLinesNode node;
         node.input = &src;
         node.radius = r;
         node.maxPerPoint = 4;
         node.maxLinks = 200000;
         auto t0 = std::chrono::steady_clock::now();
         node.CookIfNeeded(gFrame++);
         const double msCook = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
         const unsigned long long rev = node.MeshRevision();
         node.CookIfNeeded(gFrame++);
         const bool stable = node.MeshRevision() == rev;
         printf("COMPAUDIT PROXIMITY 100k points: find links median %.2f ms, best %.2f ms (bar 4 ms), %zu links; whole node incl. mesh build %.1f ms; revision stable on a second cook: %s\n",
                msFind, msFindBest, links.size(), msCook, stable ? "yes" : "NO");
         if (!stable)
            f.Flag("PROXIMITY", "revision changes on a second cook with nothing changed", 1.0);
      }
   }

   // ------------------------------------------------------------------- RES
   void AuditRes(Flags& f, bool& ok)
   {
      (void)f;
      struct Res { int w, h; const char* label; };
      const Res res[3] = { { 1024, 1024, "1 Mpx" }, { 2048, 2048, "4 Mpx" }, { 4096, 4096, "16 Mpx" } };
      for (const Res& r : res)
      {
         TexSource a(r.w, r.h), b(r.w, r.h);
         a.Set(Solid(r.w, r.h, 90));
         b.Set(Solid(r.w, r.h, 200));
         const FilterDef* def = FindDef("gaussianblur");
         if (def == nullptr)
         {
            ok = false;
            return;
         }
         FilterNode g(*def);
         g.Input().Connect(&a);
         g.SetParamValue(0, 0, 4.0f);
         g.SetParamValue(1, 0, 1.0f);
         BlendNode bl;
         bl.InputA().Connect(&a);
         bl.InputB().Connect(&b);
         bl.ModeIndex() = 0;
         bl.Mix() = 0.5f;
         g.CookIfNeeded(gFrame++);
         bl.CookIfNeeded(gFrame++);
         glFinish();
         const int reps = 5;
         auto timeIt = [&](std::function<void(int)> bump, INode& n) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < reps; i++)
            {
               bump(i);
               n.CookIfNeeded(gFrame++);
            }
            glFinish();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
         };
         const double gMs = timeIt([&](int i) { g.SetParamValue(0, 0, 4.0f + 0.001f * (float)(i + 1)); }, g);
         const double bMs = timeIt([&](int i) { bl.Mix() = 0.5f + 0.001f * (float)(i + 1); }, bl);
         printf("COMPAUDIT RES %-6s Gaussian Smooth r4 %.2f ms | Blend %.2f ms | output %d x %d (%.0f MB at 8 bit)\n", r.label, gMs, bMs,
                g.GetOutputWidth(), g.GetOutputHeight(), (double)g.GetOutputWidth() * g.GetOutputHeight() * 4.0 / 1048576.0);
         if (g.GetOutputWidth() != r.w || g.GetOutputHeight() != r.h)
            f.Flag("RES", std::string("Gaussian output is not the input size at ") + r.label + ", width", g.GetOutputWidth());
      }
   }

   // each pixel of two reads compared exactly
   double MaxPixelDiff(const std::vector<float>& a, const std::vector<float>& b)
   {
      if (a.size() != b.size())
         return 1.0;
      double m = 0.0;
      for (size_t i = 0; i < a.size(); i++)
         m = std::max(m, (double)std::fabs(a[i] - b[i]));
      return m;
   }

   // ---------------------------------------------------------------- BYPASS
   void AuditBypass(Flags& f, bool& ok)
   {
      TexSource src;
      src.Set(RandomImage(77, false));
      std::vector<float> srcPx;
      ReadSized(src.GetOutputTexture(), kW, kH, srcPx);

      struct Item { const char* name; std::function<INode*(TexSource&)> make; };
      std::vector<Item> items = {
         { "Gaussian Blur", [](TexSource& s) { FilterNode* n = new FilterNode(*FindDef("gaussianblur")); n->Input().Connect(&s); return (INode*)n; } },
         { "Hue/Saturation or first Color def", [](TexSource& s) { FilterNode* n = new FilterNode(*FindDef("boxblur")); n->Input().Connect(&s); return (INode*)n; } },
         { "Curves", [](TexSource& s) { auto* n = new CurvesNode(); n->Input().Connect(&s); return (INode*)n; } },
         { "Color Ramp", [](TexSource& s) { auto* n = new ColorRampNode(); n->Input().Connect(&s); return (INode*)n; } },
         { "Fit", [](TexSource& s) { auto* n = new FitNode(); n->Input().Connect(&s); return (INode*)n; } },
         { "Trails", [](TexSource& s) { auto* n = new TrailsNode(); n->Input().Connect(&s); return (INode*)n; } },
         { "Optical Flow", [](TexSource& s) { auto* n = new OpticalFlowNode(); n->Input().Connect(&s); return (INode*)n; } },
         { "Time Machine", [](TexSource& s) { auto* n = new TimeMachineNode(); n->Input().Connect(&s); return (INode*)n; } },
         { "Fluid", [](TexSource& s) { auto* n = new FluidNode(); n->Input().Connect(&s); return (INode*)n; } },
         { "Reaction Diffusion", [](TexSource& s) { auto* n = new ReactionDiffusionNode(); n->Input().Connect(&s); return (INode*)n; } },
      };
      items[1].name = "Box Blur";
      for (Item& it : items)
      {
         std::unique_ptr<INode> n(it.make(src));
         INode* pass = n->BypassSource();
         const bool hasPass = pass == &src;
         // Cooked twice, bypass toggled in between, same pixels.
         n->CookIfNeeded(gFrame++);
         std::vector<float> first, again;
         const bool got1 = n->GetOutputTexture() != 0 && ReadSized(n->GetOutputTexture(), n->GetOutputWidth(), n->GetOutputHeight(), first);
         n->bypassed = true;
         n->CookIfNeeded(gFrame++);
         n->bypassed = false;
         n->CookIfNeeded(gFrame++);
         const bool got2 = n->GetOutputTexture() != 0 && ReadSized(n->GetOutputTexture(), n->GetOutputWidth(), n->GetOutputHeight(), again);
         const double d = (got1 && got2) ? MaxPixelDiff(first, again) : -1.0;
         // Stateful nodes (Trails, Reaction Diffusion) integrate over frames, so only the pass-through is asserted.
         const bool stateful = strcmp(it.name, "Trails") == 0 || strcmp(it.name, "Optical Flow") == 0 || strcmp(it.name, "Time Machine") == 0 || strcmp(it.name, "Reaction Diffusion") == 0 || strcmp(it.name, "Fluid") == 0;
         printf("COMPAUDIT BYPASS %-18s pass-through is the input: %s | cook, bypass, un-bypass, cook: %s%s\n", it.name,
                hasPass ? "yes" : "NO", d < 0 ? "no output" : d == 0.0 ? "identical" : "differs",
                stateful ? " (stateful, not asserted)" : "");
         if (!hasPass)
            f.Flag("BYPASS", std::string(it.name) + " BypassSource is not its input", 1.0);
         else if (d > 0.0 && !stateful)
            f.Flag("BYPASS", std::string(it.name) + " pixels change after a bypass toggle, worst", d);
      }
      (void)ok;
   }

   // --------------------------------------------------------------- SOURCES
   void AuditSources(Flags& f, bool& ok)
   {
      if (NodeFactory::Instance().GetNodesInCategory("Source").empty())
         RegisterNodes(); // headless dispatch runs before the editor registers its node types
      const std::vector<std::string> names = NodeFactory::Instance().GetNodesInCategory("Source");
      for (const std::string& name : names)
      {
         // File, device, text-entry and script sources have nothing to show without their own input.
         if (name == "Image Source" || name == "Slideshow" || name == "Video" || name == "Video In" || name == "Audio Texture" || name == "Sketch" || name == "Text")
         {
            printf("COMPAUDIT SOURCE %-16s skipped (needs a file, device, audio input or script)\n", name.c_str());
            continue;
         }
         std::unique_ptr<INode> n(NodeFactory::Instance().MakeNode(name));
         if (!n)
            continue;
         for (int i = 0; i < 3; i++)
            n->CookIfNeeded(gFrame++);
         if (n->GetOutputTexture() == 0)
         {
            printf("COMPAUDIT SOURCE %-16s no output texture\n", name.c_str());
            f.Flag("SOURCE", name + " produces no output", 1.0);
            continue;
         }
         std::vector<float> px;
         if (!ReadSized(n->GetOutputTexture(), n->GetOutputWidth(), n->GetOutputHeight(), px))
         {
            printf("COMPAUDIT SOURCE %-16s unreadable\n", name.c_str());
            continue;
         }
         double lo = 1e9, hi = -1e9;
         size_t bad = 0;
         for (float v : px)
         {
            if (!std::isfinite(v))
               bad++;
            else
            {
               lo = std::min(lo, (double)v);
               hi = std::max(hi, (double)v);
            }
         }
         printf("COMPAUDIT SOURCE %-16s %dx%d range %.3f..%.3f non-finite %zu\n", name.c_str(), n->GetOutputWidth(), n->GetOutputHeight(), lo,
                hi, bad);
         if (bad > 0)
            f.Flag("SOURCE", name + " has non-finite pixels", (double)bad);
      }

      // Reaction Diffusion: stays bounded and alive over 120 frames.
      ReactionDiffusionNode rd;
      rd.width = 128.0f;
      rd.height = 128.0f;
      double lo = 1e9, hi = -1e9;
      size_t bad = 0;
      for (int i = 0; i < 120; i++)
         rd.CookIfNeeded(gFrame++);
      std::vector<float> px;
      if (rd.GetOutputTexture() != 0 && ReadSized(rd.GetOutputTexture(), rd.GetOutputWidth(), rd.GetOutputHeight(), px))
      {
         for (float v : px)
         {
            if (!std::isfinite(v))
               bad++;
            else
            {
               lo = std::min(lo, (double)v);
               hi = std::max(hi, (double)v);
            }
         }
         printf("COMPAUDIT SOURCE Reaction Diffusion 120 frames: range %.3f..%.3f non-finite %zu\n", lo, hi, bad);
         if (bad > 0 || lo < -0.01 || hi > 1.01)
            f.Flag("SOURCE", "Reaction Diffusion leaves [0, 1] or goes non-finite, range", std::max(std::fabs(lo), hi));
         if (hi - lo < 0.05)
            f.Flag("SOURCE", "Reaction Diffusion is flat after 120 frames, range", hi - lo);
      }
      else
      {
         printf("COMPAUDIT FAIL SOURCE Reaction Diffusion no output\n");
         ok = false;
      }
   }
} // namespace

bool RunCompAudit()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   const char* only = getenv("INFINITE_COMPAUDIT_ONLY");
   auto want = [&](const char* sec) { return only == nullptr || *only == 0 || strstr(only, sec) != nullptr; };
   Flags flags;
   bool ok = true;
   if (want("BLEND"))
      AuditBlend(flags, ok);
   if (want("ALPHA"))
      AuditAlpha(flags, ok);
   if (want("DET"))
      AuditDet(flags);
   if (want("FEEDBACK"))
      AuditFeedback(flags);
   if (want("TRAILS"))
      AuditTrails(flags);
   if (want("FLOW"))
      AuditFlow(flags, ok);
   if (want("FLUID"))
      AuditFluid(flags, ok);
   if (want("HAP"))
      AuditHap(flags, ok);
   if (only != nullptr && strstr(only, "HAPGEN") != nullptr)
      AuditHapGen(flags);
   if (want("SHARE"))
      AuditShare(flags);
   if (only != nullptr && strstr(only, "BUNDLE") != nullptr)
      AuditBundle(flags);
   if (want("VOLUME"))
      AuditVolume(flags, ok);
   if (want("TIMEMACHINE"))
      AuditTimeMachine(flags, ok);
   if (want("PROXIMITY"))
      AuditProximity(flags);
   if (want("STACK"))
      AuditStack(flags, ok);
   if (want("CURVES"))
      AuditCurves(flags, ok);
   if (want("RAMP"))
      AuditRamp(flags, ok);
   if (want("FIT"))
      AuditFit(flags, ok);
   if (want("SWITCH"))
      AuditSwitch(flags, ok);
   if (want("BLUR"))
      AuditBlur(flags, ok);
   if (want("COLOUR"))
      AuditColour(flags, ok);
   if (want("RES"))
      AuditRes(flags, ok);
   if (want("BYPASS"))
      AuditBypass(flags, ok);
   if (want("SOURCES"))
      AuditSources(flags, ok);
   printf("COMPAUDIT %s (%d flags)\n", ok ? "PASS" : "FAIL", flags.n);
   return ok;
}

} // namespace app
