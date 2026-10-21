#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "IEffectKernel.h"
#include "AnalogPrimitives.h"
#include "audio/ParamMailbox.h"

// Reverb's kernel - algorithmic FDN built for Valhalla Vintage Verb-grade
// lushness: a 16-line Hadamard FDN fed by two independent 4-stage Schroeder
// diffusion chains (one per input channel, for real stereo decorrelation
// instead of a mono-in phase-flip trick), always-on delay-length modulation
// on every line (a static FDN rings metallically - this used to only run in
// "analog" mode, which is why non-"analog" patches sounded metallic), and
// hall-scale delay lengths so the tank doesn't top out at room size. "analog"
// now only toggles the vintage saturation/dynamic-air character on top of
// that shared foundation - the modulation and diffusion underneath it are no
// longer gated behind it.
class AudioEffectNode;

namespace ReverbDsp
{
   constexpr int kNumLines = 16;
   constexpr int kNumDiffusionStages = 4;

   // Distinct primes spanning ~20-77ms @ 44.1kHz - hall-scale, not the old
   // ~24-42ms room-scale range - and mutually coprime so no two lines ever
   // share a resonant comb.
   constexpr int kBaseLengths44k[kNumLines] = {
      907,  983,  1087, 1181, 1289, 1409, 1543, 1693,
      1831, 1999, 2203, 2389, 2609, 2851, 3109, 3407
   };

   // Dattorro/Griesinger-style input diffusion: 4 series Schroeder allpasses
   // densify the impulse response before it ever reaches the tank, so the
   // FDN doesn't ring metallically on a near-impulsive input the way 1-2
   // diffusion stages leave it to. The R chain reuses the same lengths
   // offset by a fixed prime so the two channels decorrelate instead of
   // mirroring each other.
   constexpr int kDiffusionLengths44k[kNumDiffusionStages] = { 142, 107, 379, 277 };
   constexpr float kDiffusionGains[kNumDiffusionStages] = { 0.75f, 0.75f, 0.625f, 0.625f };
   constexpr int kDiffusionROffset44k = 23;

   // The damping one-pole in the feedback path also loses a little level on
   // every pass below its corner, which made the 1 kHz RT60 run short of the
   // decay knob (about 10% at 4 s). This is 1 / |H| of y += a (x - y) at the
   // reference frequency - 1 kHz, or two octaves under the damping corner
   // when that is lower (heavy damping is meant to shorten 1 kHz) - and the
   // line gains are scaled by it, so the knob is the mid-band RT60 and
   // damping only shortens what is above it.
   inline float DampCompensation(float cutoffHz, float sampleRate)
   {
      const float a = 1.0f - std::exp(-2.0f * 3.14159265f * cutoffHz / sampleRate);
      const float w = 2.0f * 3.14159265f * std::min(1000.0f, 0.25f * cutoffHz) / sampleRate;
      const float re = 1.0f - (1.0f - a) * std::cos(w), im = (1.0f - a) * std::sin(w);
      return std::sqrt(re * re + im * im) / a;
   }

   // Compensated loop gain. The compensation lifts DC above the knob too;
   // capped at g^0.8 (DC RT60 at most 25% over the knob), which also keeps
   // the loop under unity however long the decay.
   inline float CompensatedDecayGain(float decayGain, float comp)
   {
      return std::max(decayGain, std::min(decayGain * comp, std::pow(decayGain, 0.8f)));
   }

   // Prime-detuned LFO rates, one per line, so no two lines' modulation ever
   // phase-locks - shared by both the digital and analog character modes.
   constexpr float kLfoRates[kNumLines] = {
      0.29f, 0.41f, 0.53f, 0.67f, 0.37f, 0.47f, 0.73f, 0.83f,
      0.31f, 0.43f, 0.59f, 0.71f, 0.39f, 0.51f, 0.61f, 0.79f
   };

   // In-place fast Walsh-Hadamard transform, normalized to unit gain -
   // generalizes the old fixed 8-line Hadamard mix to any power-of-two line
   // count. Orthogonal (energy-preserving), so the tank's total energy can
   // only ever fall, never build up, no matter how many lines feed it.
   inline void HadamardMixN(float* buf, int n)
   {
      for (int len = 1; len < n; len <<= 1)
      {
         for (int i = 0; i < n; i += (len << 1))
         {
            for (int j = i; j < i + len; j++)
            {
               const float a = buf[j];
               const float b = buf[j + len];
               buf[j] = a + b;
               buf[j + len] = a - b;
            }
         }
      }
      const float norm = 1.0f / std::sqrt((float)n);
      for (int i = 0; i < n; i++)
         buf[i] *= norm;
   }

   inline float FlushDenormal(float x) { return DspMath::FlushDenormal(x); }

   struct FdnLine
   {
      std::vector<float> buf;
      int capacity = 1;
      int writePos = 0;
      float dampState = 0.0f;

      void Prepare(int cap)
      {
         capacity = std::max(8, cap);
         buf.assign((size_t)capacity, 0.0f);
         writePos = 0;
         dampState = 0.0f;
      }

      void Reset()
      {
         std::fill(buf.begin(), buf.end(), 0.0f);
         writePos = 0;
         dampState = 0.0f;
      }

      float SampleAtDelay(int k) const
      {
         int p = writePos - 1 - k;
         p %= capacity;
         if (p < 0)
            p += capacity;
         return buf[(size_t)p];
      }

      // 4-point Hermite cubic fractional read - every line is continuously
      // modulated now (see class comment), so this is the only read path.
      float Read(float delaySamples) const
      {
         const int iDelay = (int)delaySamples;
         const float frac = delaySamples - (float)iDelay;
         const float xm1 = SampleAtDelay(iDelay - 1);
         const float x0  = SampleAtDelay(iDelay);
         const float x1  = SampleAtDelay(iDelay + 1);
         const float x2  = SampleAtDelay(iDelay + 2);
         const float c0 = x0;
         const float c1 = 0.5f * (x1 - xm1);
         const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
         const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
         return ((c3 * frac + c2) * frac + c1) * frac + c0;
      }

      void Write(float v)
      {
         buf[(size_t)writePos] = v;
         writePos++;
         if (writePos >= capacity)
            writePos = 0;
      }
   };

   struct AllpassDiffuser
   {
      std::vector<float> buf;
      int size = 1;
      int pos = 0;
      float g = 0.7f;

      void Prepare(int samples, float gain)
      {
         size = std::max(4, samples);
         buf.assign((size_t)size, 0.0f);
         pos = 0;
         g = gain;
      }

      void Reset()
      {
         std::fill(buf.begin(), buf.end(), 0.0f);
         pos = 0;
      }

      float Process(float x)
      {
         const float delayed = buf[(size_t)pos];
         const float y = -g * x + delayed;
         buf[(size_t)pos] = FlushDenormal(x + g * y);
         pos++;
         if (pos >= size)
            pos = 0;
         return y;
      }
   };
}

class ReverbKernel : public IEffectKernel
{
public:
   enum ParamSlot
   {
      kSize = 0,
      kDecaySeconds,
      kDamping,
      kPredelayMs,
      kWidth,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mPushed.store(false, std::memory_order_relaxed);
      const float rateScale = (float)(sampleRate / 44100.0);

      for (int i = 0; i < ReverbDsp::kNumLines; i++)
      {
         const int cap = (int)std::ceil(ReverbDsp::kBaseLengths44k[i] * rateScale) + 64;
         mLines[i].Prepare(cap);
      }

      for (int i = 0; i < ReverbDsp::kNumDiffusionStages; i++)
      {
         const int lenL = (int)std::ceil(ReverbDsp::kDiffusionLengths44k[i] * rateScale);
         const int lenR =
            (int)std::ceil((ReverbDsp::kDiffusionLengths44k[i] + ReverbDsp::kDiffusionROffset44k) * rateScale);
         mDiffuserL[i].Prepare(lenL, ReverbDsp::kDiffusionGains[i]);
         mDiffuserR[i].Prepare(lenR, ReverbDsp::kDiffusionGains[i]);
      }

      // Fixed bandwidth limit ahead of the tank - softens the injected
      // transient so it can't ping the FDN into a harsh/metallic onset.
      // Independent of the `damping` knob, which shapes the feedback tail.
      mInputLpfL.SetCutoff(13000.0f, sampleRate);
      mInputLpfR.SetCutoff(13000.0f, sampleRate);

      const int maxPredelaySamples = (int)std::ceil(0.5 * sampleRate) + 8;
      mPredelayL.assign((size_t)std::max(8, maxPredelaySamples), 0.0f);
      mPredelayR.assign((size_t)std::max(8, maxPredelaySamples), 0.0f);
      mPredelayCapacity = (int)mPredelayL.size();

      Reset();
   }

   void Reset() override
   {
      mSnapPending = true;
      for (auto& line : mLines)
         line.Reset();
      for (auto& lfo : mLfo)
         lfo.Reset();
      for (int l = 0; l < ReverbDsp::kNumLines; l++)
         mLfoPhase[l] = mLfoDriftPhase[l] = 0.0f;
      for (auto& d : mDiffuserL)
         d.Reset();
      for (auto& d : mDiffuserR)
         d.Reset();
      mInputLpfL.Reset();
      mInputLpfR.Reset();
      std::fill(mPredelayL.begin(), mPredelayL.end(), 0.0f);
      std::fill(mPredelayR.begin(), mPredelayR.end(), 0.0f);
      mPredelayWrite = 0;
      mPredelayTap = -1;
      mPredelayFade = 1.0f;
      for (float& len : mLineLen)
         len = -1.0f;
      mInputEnv = 0.0f;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;
   void ProcessBlockScalar(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out);
   void ProcessBlockSimd(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out);
   // Test-only frozen reference (ReverbKernelLegacy.cpp): the kernel before
   // the Block 1 perf work, for DSPTEST's old-vs-new comparison.
   void ProcessBlockLegacy(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out);

   // Audio thread only. A freshly prepared or reset reverb starts at the
   // settings it was given, not ramping up to them from 0: the param
   // smoothers jump to their pushed targets on the first block after a
   // push. (From 0, `size` swept the lines from their shortest length,
   // which the slewed line lengths below would have carried for seconds.)
   // ProcessBlockSimd/Scalar call it; DSPTEST calls it for the legacy path.
   void SnapParamsIfPending()
   {
      if (!mSnapPending || !mPushed.load(std::memory_order_acquire))
         return;
      for (int id = 0; id < kNumSlots; id++)
         mMailbox.SnapToTarget(id);
      mSnapPending = false;
   }

   int LatencySamples() const override { return 0; }
   MeterRing* ExtraMeter() override { return &mLevelMeter; }

private:
   ParamMailbox mMailbox;
   std::atomic<bool> mPushed { false }; // a PushParams has landed since PrepareToPlay
   bool mSnapPending = true;            // audio thread: snap on the next block
   double mSampleRate = 44100.0;
   std::atomic<int> mAnalog { 0 };

   ReverbDsp::FdnLine mLines[ReverbDsp::kNumLines];
   ReverbDsp::AllpassDiffuser mDiffuserL[ReverbDsp::kNumDiffusionStages];
   ReverbDsp::AllpassDiffuser mDiffuserR[ReverbDsp::kNumDiffusionStages];
   AnalogDsp::OnePoleLP mInputLpfL, mInputLpfR;
   AnalogDsp::DriftLfo mLfo[ReverbDsp::kNumLines];
   // Live-path LFO state (ProcessBlockSimd), one array per field so the
   // 16-line update vectorises. mLfo above stays for ProcessBlockScalar
   // and ProcessBlockLegacy.
   float mLfoPhase[ReverbDsp::kNumLines] = {};
   float mLfoDriftPhase[ReverbDsp::kNumLines] = {};
   float mInputEnv = 0.0f;

   std::vector<float> mPredelayL, mPredelayR;
   int mPredelayCapacity = 1;
   int mPredelayWrite = 0;
   // Live path only: the predelay tap being read, and a crossfade toward a
   // new one. Moving the tap a sample at a time as the knob smooths read a
   // jagged, discontinuous signal (a +21 dB click on a 30->70% step);
   // instead the old and new taps are crossfaded over kPredelayFadeMs, then
   // the next change starts. -1 = not primed: the first sample snaps.
   int mPredelayTap = -1;
   int mPredelayNextTap = 0;
   float mPredelayFade = 1.0f;
   // Live path only: each FDN line's read length, slewed toward the length
   // `size` asks for at kLineSlew samples per sample. Jumping it to a new
   // integer as the knob smoothed read the line at a fresh position every
   // sample (a +20 dB click on a size step); slewed, a size move is a short,
   // gentle pitch glide. At rest it equals the integer length exactly, so
   // static output is unchanged. -1 = not primed: the first sample snaps.
   float mLineLen[ReverbDsp::kNumLines] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
                                            -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };

   MeterRing mLevelMeter;
};
