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

   struct MsegPoint
   {
      float x = 0.0f;     // 0..1 along the shape
      float y = 0.0f;     // 0..1
      float curve = 0.0f; // bend of the segment that leaves this point: -1..1, 0 = straight
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

   // Shape value at x (0..1) for points sorted by x.
   inline float MsegEvaluate(const MsegPoint* points, int count, float x)
   {
      if (count < 2)
         return count <= 0 ? 0.0f : points[0].y;
      x = std::clamp(x, 0.0f, 1.0f);
      int i = 0;
      while (i < count - 2 && x >= points[i + 1].x)
         i++;
      const MsegPoint& a = points[i];
      const MsegPoint& b = points[i + 1];
      const float span = b.x - a.x;
      if (span <= 1.0e-6f)
         return x >= b.x ? b.y : a.y; // two points on one x: a step
      return MsegSegment(a.y, b.y, a.curve, (x - a.x) / span);
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
