#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// The shape maths of LFO, Random, MSEG, Envelope and Voice Spread as pure functions, shared by the
// nodes' Value01() (main thread, one shared value) and VoiceMod's per-voice
// kernel (audio thread, one instance per voice) so the two cannot drift.
// Nothing here allocates or touches the transport.
namespace ModShapes
{
   inline float Frac(double v) { return (float)(v - std::floor(v)); }

   inline float Hash01(long long n)
   {
      unsigned long long x = (unsigned long long)n * 2654435761ULL + 1013904223ULL;
      x ^= x >> 33;
      x *= 0xff51afd7ed558ccdULL;
      x ^= x >> 33;
      return (float)((x >> 11) & 0xFFFFFF) / (float)0xFFFFFF;
   }

   inline uint64_t Mix64(uint64_t x)
   {
      x += 0x9E3779B97F4A7C15ULL;
      x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
      x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
      return x ^ (x >> 31);
   }

   inline uint64_t SeedSalt(float seed, uint64_t stream)
   {
      uint32_t bits = 0;
      if (seed != 0.0f) // -0 and +0 are the same seed
         std::memcpy(&bits, &seed, sizeof bits);
      return Mix64((uint64_t)bits ^ (stream << 32));
   }

   inline float HashSeeded(long long step, uint64_t salt)
   {
      return (float)(Mix64((uint64_t)step ^ salt) >> 40) / (float)0xFFFFFF;
   }

   inline float Remap(float v01, float low, float high)
   {
      const float mapped = low + (high - low) * std::min(1.0f, std::max(0.0f, v01));
      return std::min(1.0f, std::max(0.0f, mapped));
   }

   // LFO shape at `pos` = cycles + phase. `salt` is SeedSalt(seed, 1) for
   // maths >= 1 (S&H only); maths 0 is the unseeded hash of older patches.
   inline float Lfo(int shape, double pos, int maths, uint64_t salt)
   {
      const float t = Frac(pos);
      switch (shape)
      {
         case 0: return 0.5f + 0.5f * std::sin(t * 6.28318530718f); // Sine
         case 1: return t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f;      // Triangle
         case 2: return t;                                          // Saw Up
         case 3: return 1.0f - t;                                   // Saw Down
         case 4: return t < 0.5f ? 0.0f : 1.0f;                     // Square
         default:                                                   // S&H
         {
            const long long step = (long long)std::floor(pos);
            return maths >= 1 ? HashSeeded(step, salt) : Hash01(step);
         }
      }
   }

   // Random's glide from one step's value to the next across the tail of
   // the step, scaled by `smooth` (0 = stepped).
   inline float RandomGlide(float current, float next, float t, float smooth)
   {
      if (!(smooth > 0.0f))
         return current;
      const float k = std::min(1.0f, t / std::max(0.0001f, smooth));
      const float eased = k * k * (3.0f - 2.0f * k);
      return current + (next - current) * eased;
   }

   // Voice Spread: where the `index`th note of `count` sits, 0..1. Linear
   // steps evenly; golden ratio scatters (each new note lands in the
   // largest gap); primes steps unevenly, so no two spacings repeat;
   // alternate goes out from the centre, one side then the other.
   enum SpreadMode
   {
      kSpreadLinear = 0,
      kSpreadGolden,
      kSpreadPrimes,
      kSpreadAlternate
   };
   constexpr int kSpreadMaxCount = 16;

   inline float Spread(int mode, int index, int count)
   {
      const int n = std::clamp(count, 1, kSpreadMaxCount);
      if (n == 1)
         return 0.5f;
      const int k = ((index % n) + n) % n;
      switch (mode)
      {
         case kSpreadGolden: return Frac((double)k * 0.6180339887498949);
         case kSpreadPrimes:
         {
            static constexpr int kPrimes[kSpreadMaxCount] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53 };
            return (float)(kPrimes[k] - 2) / (float)(kPrimes[n - 1] - 2);
         }
         case kSpreadAlternate:
         {
            const int pairs = (n + 1) / 2;
            const float offset = 0.5f * (float)(k / 2 + 1) / (float)pairs;
            return (k % 2 == 0) ? 0.5f - offset : 0.5f + offset;
         }
         default: return (float)k / (float)(n - 1);
      }
   }

   // How the segment that leaves a point is drawn (Blender's keyframe
   // interpolation: the left point of a segment decides it).
   enum MsegInterp : uint8_t
   {
      kInterpConstant = 0, // hold this point's value until the next point
      kInterpLinear,
      kInterpBend,         // exponential bend set by `curve` (every older patch)
      kInterpBezier,       // cubic through this point's right and the next point's left handle
      kInterpEasing,
      kInterpCount
   };
   // Blender's handle types. The auto kinds are recomputed from the
   // neighbours after every edit (MsegAutoHandles); Aligned and Free keep
   // what was dragged.
   enum MsegHandle : uint8_t
   {
      kHandleAutoClamped = 0, // smooth, flat at peaks and dips, never overshoots
      kHandleAuto,
      kHandleVector,          // points a third of the way at the neighbour
      kHandleAligned,         // dragged, the two sides stay in one line
      kHandleFree,            // dragged, each side on its own
      kHandleCount
   };

   struct MsegPoint
   {
      float x = 0.0f;     // 0..1 along the shape
      float y = 0.0f;     // 0..1
      float curve = 0.0f; // bend of the segment that leaves this point: -1..1, 0 = straight
      uint8_t interp = kInterpBend;
      uint8_t handle = kHandleAutoClamped;
      uint8_t easing = 0;
      // Bezier handles as offsets from the point, in shape units: left
      // (lx <= 0) and right (rx >= 0).
      float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
   };

   // Exponential bend: (e^(k t) - 1) / (e^k - 1), k = -6 * curve. Positive curve
   // moves fast first and settles (the usual attack and decay shape).
   inline float MsegSegment(float y0, float y1, float curve, float t)
   {
      t = std::clamp(t, 0.0f, 1.0f);
      const float k = -6.0f * std::clamp(curve, -1.0f, 1.0f);
      const float shaped = std::fabs(k) < 1.0e-3f ? t : std::expm1(k * t) / std::expm1(k);
      return y0 + (y1 - y0) * shaped;
   }

   // A bezier segment's y at x. Handles longer than the segment are scaled
   // down together (Blender's correct_bezpart), so x only ever rises and
   // there is one y per x; x(t) = x is then solved by Newton, with bisection
   // as the safety net.
   inline float MsegBezier(const MsegPoint& a, const MsegPoint& b, float x)
   {
      const double span = (double)b.x - (double)a.x;
      if (span <= 1.0e-9)
         return x >= b.x ? b.y : a.y;
      double h1x = std::max(0.0, (double)a.rx), h1y = a.ry;
      double h2x = std::max(0.0, -(double)b.lx), h2y = -(double)b.ly;
      const double len = h1x + h2x;
      if (len > span)
      {
         const double f = span / len;
         h1x *= f; h1y *= f; h2x *= f; h2y *= f;
      }
      const double x0 = a.x, x1 = a.x + h1x, x2 = b.x - h2x, x3 = b.x;
      const double y0 = a.y, y1 = a.y + h1y, y2 = b.y - h2y, y3 = b.y;
      // x(t) = c3 t^3 + c2 t^2 + c1 t + x0
      const double c1 = 3.0 * (x1 - x0), c2 = 3.0 * (x0 - 2.0 * x1 + x2), c3 = x3 - x0 + 3.0 * (x1 - x2);
      const double target = std::clamp((double)x, x0, x3);
      double t = (target - x0) / span, lo = 0.0, hi = 1.0;
      for (int i = 0; i < 24; i++)
      {
         const double fx = ((c3 * t + c2) * t + c1) * t + x0 - target;
         if (std::fabs(fx) < 1.0e-9)
            break;
         if (fx > 0.0)
            hi = t;
         else
            lo = t;
         const double d = (3.0 * c3 * t + 2.0 * c2) * t + c1;
         double next = d > 1.0e-12 ? t - fx / d : 0.5 * (lo + hi);
         if (next <= lo || next >= hi)
            next = 0.5 * (lo + hi);
         t = next;
      }
      const double u = 1.0 - t;
      return (float)(u * u * u * y0 + 3.0 * u * u * t * y1 + 3.0 * u * t * t * y2 + t * t * t * y3);
   }

   constexpr int kEaseCount = 38;
   inline const char* MsegEaseName(int id)
   {
      static const char* names[kEaseCount] = {
         "Sine In", "Sine Out", "Sine InOut", "Quad In", "Quad Out", "Quad InOut",
         "Cubic In", "Cubic Out", "Cubic InOut", "Quart In", "Quart Out", "Quart InOut",
         "Quint In", "Quint Out", "Quint InOut", "Expo In", "Expo Out", "Expo InOut",
         "Circ In", "Circ Out", "Circ InOut", "Back In", "Back Out", "Back InOut",
         "Elastic In", "Elastic Out", "Elastic InOut", "Bounce In", "Bounce Out", "Bounce InOut",
         "CSS ease", "CSS ease-in", "CSS ease-out", "CSS ease-in-out", "CSS linear",
         "Easy Ease", "Material standard", "Material emphasized"
      };
      return names[std::clamp(id, 0, kEaseCount - 1)];
   }
   // The easing that plays this one backwards in time: In and Out swap, InOut and the symmetric ones keep.
   inline uint8_t MsegEaseReversed(uint8_t id)
   {
      if (id < 30 && id % 3 != 2)
         return (uint8_t)(id % 3 == 0 ? id + 1 : id - 1);
      if (id == 31 || id == 32)
         return (uint8_t)(id == 31 ? 32 : 31);
      return id;
   }
   inline double MsegBounceOut(double t)
   {
      constexpr double n = 7.5625, d = 2.75;
      if (t < 1.0 / d) return n * t * t;
      if (t < 2.0 / d) { t -= 1.5 / d; return n * t * t + 0.75; }
      if (t < 2.5 / d) { t -= 2.25 / d; return n * t * t + 0.9375; }
      t -= 2.625 / d; return n * t * t + 0.984375;
   }
   // CSS x handles may cross; do not shorten them like keyframe handles.
   inline float MsegCubicEase(float t, double x1, double y1, double x2, double y2)
   {
      double lo = 0.0, hi = 1.0;
      for (int i = 0; i < 32; ++i)
      {
         const double u = (lo + hi) * .5, v = 1.0 - u;
         const double x = 3*v*v*u*x1 + 3*v*u*u*x2 + u*u*u;
         if (x < t) lo = u; else hi = u;
      }
      const double u = (lo + hi) * .5, v = 1.0 - u;
      return (float)(3*v*v*u*y1 + 3*v*u*u*y2 + u*u*u);
   }
   inline float MsegEase(int id, float time)
   {
      const double t = std::clamp((double)time, 0.0, 1.0);
      if (t == 0.0 || t == 1.0) return (float)t;
      id = std::clamp(id, 0, kEaseCount - 1);
      if (id >= 30)
      {
         switch (id)
         {
            case 30: return MsegCubicEase((float)t, .25, .1, .25, 1);
            case 31: return MsegCubicEase((float)t, .42, 0, 1, 1);
            case 32: return MsegCubicEase((float)t, 0, 0, .58, 1);
            case 33: return MsegCubicEase((float)t, .42, 0, .58, 1);
            case 34: return (float)t;
            case 35: return MsegCubicEase((float)t, 1.0/3, 0, 2.0/3, 1);
            case 36: return MsegCubicEase((float)t, .4, 0, .2, 1);
            default: return MsegCubicEase((float)t, .2, 0, 0, 1);
         }
      }
      constexpr double pi = 3.14159265358979323846;
      const int family = id / 3, mode = id % 3;
      auto in = [family](double u) {
         constexpr double pi = 3.14159265358979323846;
         switch (family)
         {
            case 0: return 1.0 - std::cos(u*pi*.5);
            case 1: return u*u;
            case 2: return u*u*u;
            case 3: return u*u*u*u;
            case 4: return u*u*u*u*u;
            case 5: return u == 0 ? 0.0 : std::pow(2.0, 10*u-10);
            case 6: return 1.0 - std::sqrt(std::max(0.0, 1-u*u));
            case 7: return 2.70158*u*u*u - 1.70158*u*u;
            case 8: return u == 0 || u == 1 ? u : -std::pow(2.0, 10*u-10)*std::sin((10*u-10.75)*(2*pi/3));
            default: return 1.0-MsegBounceOut(1-u);
         }
      };
      if (mode == 0) return (float)in(t);
      if (mode == 1) return (float)(1-in(1-t));
      if (family == 7)
      {
         const double c = 1.70158*1.525, u = t < .5 ? 2*t : 2*t-2;
         return (float)(t < .5 ? u*u*((c+1)*u-c)/2 : (u*u*((c+1)*u+c)+2)/2);
      }
      if (family == 8)
      {
         const double wave = std::sin((20*t-11.125)*(2*pi/4.5));
         return (float)(t < .5 ? -std::pow(2.0,20*t-10)*wave/2 : std::pow(2.0,-20*t+10)*wave/2+1);
      }
      return (float)(t < .5 ? in(2*t)*.5 : 1-in(2-2*t)*.5);
   }

   // The segment holding x: the last point at or before x among
   // 0..count-2 (a run of points on one x resolves to its last, so a step
   // lands on the new value). `hint` is the segment found last time: a curve
   // read left to right finds x in it or the next one, so the per-voice
   // kernel pays a compare or two, not a search. count >= 2.
   inline int MsegFindSegment(const MsegPoint* points, int count, float x, int hint)
   {
      if (hint >= 0 && hint <= count - 2 && points[hint].x <= x)
      {
         if (hint == count - 2 || x < points[hint + 1].x)
            return hint;
         if (hint + 1 == count - 2 || x < points[hint + 2].x)
            return hint + 1;
      }
      int lo = 1, hi = count - 1; // first index in 1..count-1 whose x > target
      while (lo < hi)
      {
         const int mid = (lo + hi) >> 1;
         if (points[mid].x <= x)
            lo = mid + 1;
         else
            hi = mid;
      }
      return lo - 1;
   }

   // Shape value at x (0..1) for points sorted by x; `hint` as above, updated.
   inline float MsegEvaluate(const MsegPoint* points, int count, float x, int& hint)
   {
      if (count < 2)
         return count <= 0 ? 0.0f : points[0].y;
      x = std::clamp(x, 0.0f, 1.0f);
      const int i = hint = MsegFindSegment(points, count, x, hint);
      const MsegPoint& a = points[i];
      const MsegPoint& b = points[i + 1];
      const float span = b.x - a.x;
      if (span <= 1.0e-6f)
         return x >= b.x ? b.y : a.y; // two points on one x: a step
      const float t = (x - a.x) / span;
      switch (a.interp)
      {
         case kInterpConstant: return x >= b.x ? b.y : a.y;
         case kInterpLinear: return a.y + (b.y - a.y) * std::clamp(t, 0.0f, 1.0f);
         case kInterpBezier: return MsegBezier(a, b, x);
         case kInterpEasing: return a.y + (b.y - a.y) * MsegEase(a.easing, t);
         default: return MsegSegment(a.y, b.y, a.curve, t);
      }
   }

   inline float MsegEvaluate(const MsegPoint* points, int count, float x)
   {
      int hint = -1;
      return MsegEvaluate(points, count, x, hint);
   }

   // Recompute the handles of every auto-kind point from its neighbours,
   // a third of the way along each side. Auto Clamped lies flat at a peak,
   // a dip or an end and never lets a handle pass a neighbour's value, so a
   // modulator never overshoots 0..1. Main thread, after an edit.
   inline void MsegAutoHandles(MsegPoint* p, int count)
   {
      for (int i = 0; i < count; i++)
      {
         MsegPoint& c = p[i];
         if (c.handle == kHandleAligned || c.handle == kHandleFree)
            continue;
         const float dl = i > 0 ? c.x - p[i - 1].x : 0.0f;
         const float dr = i + 1 < count ? p[i + 1].x - c.x : 0.0f;
         if (c.handle == kHandleVector)
         {
            c.lx = -dl / 3.0f; c.ly = i > 0 ? (p[i - 1].y - c.y) / 3.0f : 0.0f;
            c.rx = dr / 3.0f;  c.ry = i + 1 < count ? (p[i + 1].y - c.y) / 3.0f : 0.0f;
            continue;
         }
         float slope = 0.0f;
         if (i > 0 && i + 1 < count && p[i + 1].x - p[i - 1].x > 1.0e-6f)
            slope = (p[i + 1].y - p[i - 1].y) / (p[i + 1].x - p[i - 1].x);
         else if (c.handle == kHandleAuto && i + 1 < count && dr > 1.0e-6f)
            slope = (p[i + 1].y - c.y) / dr;
         else if (c.handle == kHandleAuto && i > 0 && dl > 1.0e-6f)
            slope = (c.y - p[i - 1].y) / dl;
         c.lx = -dl / 3.0f; c.ly = -slope * dl / 3.0f;
         c.rx = dr / 3.0f;  c.ry = slope * dr / 3.0f;
         if (c.handle == kHandleAutoClamped)
         {
            if (i == 0 || i + 1 == count)
            {
               c.ly = c.ry = 0.0f;
               continue;
            }
            const float yl = p[i - 1].y, yr = p[i + 1].y;
            if ((c.y >= yl && c.y >= yr) || (c.y <= yl && c.y <= yr))
            {
               c.ly = c.ry = 0.0f; // peak or dip
               continue;
            }
            // Keep each handle's value between this point and its neighbour.
            const float kl = std::fabs(c.ly) > 1.0e-9f ? std::min(1.0f, std::fabs(yl - c.y) / std::fabs(c.ly)) : 1.0f;
            const float kr = std::fabs(c.ry) > 1.0e-9f ? std::min(1.0f, std::fabs(yr - c.y) / std::fabs(c.ry)) : 1.0f;
            const float k = std::min(kl, kr); // one factor keeps the two sides in line
            c.ly *= k; c.ry *= k;
         }
      }
   }

   // Envelope: attack, hold, decay, sustain, release (times in ms), each
   // moving stage bent by its own curve (MsegSegment's: 0 straight,
   // positive fast first, so a positive decay or release falls like a
   // capacitor discharging).
   struct Ahdsr
   {
      float attackMs = 10.0f;
      float holdMs = 0.0f;
      float decayMs = 200.0f;
      float sustain = 0.6f;
      float releaseMs = 400.0f;
      float attackCurve = 0.0f;
      float decayCurve = 0.0f;
      float releaseCurve = 0.0f;
   };

   enum EnvStage
   {
      kEnvIdle = 0,
      kEnvAttack,
      kEnvHold,
      kEnvDecay,
      kEnvSustain,
      kEnvRelease
   };

   // The level at `seconds`, walking stage edges at their exact times (an
   // attack that ends between calls hands over at attackMs, not at the call
   // that noticed). `stage`, `stageStart` (seconds) and `startLevel` (the
   // level the stage began from) advance in place. Attack and release start
   // from `startLevel`, so a retrigger or an early release never jumps.
   inline float AhdsrLevel(const Ahdsr& e, int& stage, double& stageStart, float& startLevel, double seconds)
   {
      const float sustain = std::clamp(e.sustain, 0.0f, 1.0f);
      for (int edges = 0; edges < 6; edges++)
      {
         const double ms = std::max(0.0, (seconds - stageStart) * 1000.0);
         switch (stage)
         {
            case kEnvAttack:
            {
               const double a = std::max(0.001f, e.attackMs);
               if (ms < a)
                  return MsegSegment(startLevel, 1.0f, e.attackCurve, (float)(ms / a));
               stage = kEnvHold;
               startLevel = 1.0f;
               stageStart += a * 0.001;
               break;
            }
            case kEnvHold:
            {
               const double hd = std::max(0.0f, e.holdMs);
               if (ms < hd)
                  return 1.0f;
               stage = kEnvDecay;
               stageStart += hd * 0.001;
               break;
            }
            case kEnvDecay:
            {
               const double d = std::max(0.001f, e.decayMs);
               if (ms < d)
                  return MsegSegment(1.0f, sustain, e.decayCurve, (float)(ms / d));
               stage = kEnvSustain;
               stageStart += d * 0.001;
               break;
            }
            case kEnvSustain:
               return sustain;
            case kEnvRelease:
            {
               const double r = std::max(0.001f, e.releaseMs);
               if (ms < r)
                  return MsegSegment(startLevel, 0.0f, e.releaseCurve, (float)(ms / r));
               stage = kEnvIdle;
               return 0.0f;
            }
            default:
               return 0.0f;
         }
      }
      return stage == kEnvSustain ? sustain : 0.0f;
   }

   // The loop as ordered bounds a <= b, clamped to the shape.
   inline void MsegLoopBounds(float loopStart, float loopEnd, double& a, double& b)
   {
      a = std::clamp((double)std::min(loopStart, loopEnd), 0.0, 1.0);
      b = std::clamp((double)std::max(loopStart, loopEnd), 0.0, 1.0);
   }

   // Where along the shape a run that started `elapsed` shape-lengths ago
   // is while its gate is held (or, `patched` false, while nothing gates it).
   inline float MsegPositionHeld(double elapsed, bool patched, bool loop, float loopStart, float loopEnd)
   {
      if (elapsed <= 0.0)
         return 0.0f;
      if (!loop)
         return patched ? (float)std::min(1.0, elapsed) : Frac(elapsed);
      double a, b;
      MsegLoopBounds(loopStart, loopEnd, a, b);
      if (elapsed < b)
         return (float)elapsed;
      if (b - a < 1.0e-4)
         return (float)b; // sustain point
      return (float)(a + std::fmod(elapsed - a, b - a));
   }

   // How a shape nobody triggers runs along the transport (or its own clock).
   enum MsegPlay
   {
      kPlayRepeat = 0, // over and over; with loop on, plays in then repeats the loop part
      kPlayOnce,       // once from the start, then holds the last value
      kPlayPingPong,   // forward, then back, over and over
      kPlayCount
   };

   // Where along the shape a free run is, `elapsed` shape-lengths in.
   inline float MsegPositionFree(double elapsed, int play, bool loop, float loopStart, float loopEnd)
   {
      if (elapsed <= 0.0)
         return 0.0f;
      switch (play)
      {
         case kPlayOnce: return (float)std::min(1.0, elapsed);
         case kPlayPingPong:
         {
            const double f = std::fmod(elapsed, 2.0);
            return (float)(f <= 1.0 ? f : 2.0 - f);
         }
         default: return MsegPositionHeld(elapsed, false, loop, loopStart, loopEnd);
      }
   }

   // One step of a gated run already under way: `held` keeps it in the
   // loop (or at the end, loop off); released, it plays out to the end and
   // rests there. `released` latches.
   inline double MsegStep(double x, double dx, bool held, bool& released, bool loop, float loopStart, float loopEnd)
   {
      if (!released && held)
      {
         double a, b;
         MsegLoopBounds(loopStart, loopEnd, a, b);
         x += dx;
         if (!loop)
            return std::min(1.0, x);
         if (x >= b)
            x = b - a < 1.0e-4 ? b : a + std::fmod(x - a, b - a);
         return x;
      }
      released = true;
      return std::min(1.0, x + dx);
   }
}
