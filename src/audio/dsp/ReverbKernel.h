#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "IEffectKernel.h"
#include "audio/ParamMailbox.h"

// Reverb's kernel - algorithmic only, per
// .claude/skills/new-audio-node/SKILL.md's minimalism rule. The design doc
// (docs/plans/audio/P3c-P3a2-design.md §1.4) specifies an `engine` dropdown
// (algorithmic / convolution) plus a Tier 2 table (diffusion, mod rate/depth,
// early/late balance, low/high cut, low/high decay multipliers, width,
// freeze, and an entire convolution IR-file sub-panel); none of that ships
// here. Convolution reverb is a second engine with its own file-loading,
// worker-thread IR swap and FFT-partition kernel - the kind of "more capable,
// closer to the spec doc" addition §0.5 explicitly calls out as not a reason
// to build it. Tier 1 only: size, decay, damping, predelay, mix - 5 controls,
// one processing mode, matching the KHS Audio Delay/Compressor control-
// surface bar.
//
// Kernel: 8-line FDN with a Hadamard mixing matrix (lossless, so the overall
// output RT60 tracks the per-line target gain formula below), mutually-
// incommensurate delay lengths, per-line one-pole damping in the feedback
// path, and a short Schroeder allpass diffusion chain ahead of the network.
// Primary reference: Jot & Chaigne, "Digital Delay Networks for Designing
// Artificial Reverberators" (AES 1991) - the per-line decay-gain formula
// g_i = 10^(-3 * L_i / (fs * RT60)) so every line decays to -60dB at the same
// RT60 comes straight from that paper's feedback-matrix-gain derivation.
class AudioEffectNode;

namespace ReverbDsp
{
   constexpr int kNumLines = 8;

   // Delay lengths in samples at a 44.1kHz reference rate, chosen distinct
   // and free of small common factors so the 8 lines don't beat together
   // into audible resonances - scaled by sampleRate/44100 and then by the
   // `size` param at PrepareToPlay/ProcessBlock time.
   constexpr int kBaseLengths44k[kNumLines] = { 1051, 1163, 1279, 1381, 1499, 1607, 1733, 1867 };

   // Sylvester-construction Hadamard 8x8, entries +-1, normalized by
   // 1/sqrt(8) so the matrix is orthogonal (energy-preserving) - required
   // for the per-line RT60 gain formula above to hold for the network as a
   // whole, not just for one isolated line.
   inline void HadamardMix8(const float in[kNumLines], float out[kNumLines])
   {
      // H4 blocks, built from H2 = [[1,1],[1,-1]] via the standard
      // recursive doubling, then folded into H8 = [[H4,H4],[H4,-H4]].
      float h4a[4], h4b[4];
      {
         const float a = in[0], b = in[1], c = in[2], d = in[3];
         h4a[0] = a + b + c + d;
         h4a[1] = a - b + c - d;
         h4a[2] = a + b - c - d;
         h4a[3] = a - b - c + d;
      }
      {
         const float a = in[4], b = in[5], c = in[6], d = in[7];
         h4b[0] = a + b + c + d;
         h4b[1] = a - b + c - d;
         h4b[2] = a + b - c - d;
         h4b[3] = a - b - c + d;
      }
      static constexpr float kNorm = 0.35355339059f; // 1/sqrt(8)
      for (int i = 0; i < 4; i++)
      {
         out[i] = (h4a[i] + h4b[i]) * kNorm;
         out[4 + i] = (h4a[i] - h4b[i]) * kNorm;
      }
   }

   inline float FlushDenormal(float x) { return std::fabs(x) < 1.0e-20f ? 0.0f : x; }

   // One FDN line: a plain circular buffer sized for the largest delay this
   // sample rate will ever need (size=1), read at an exact integer offset
   // that may be smaller than the buffer's capacity - shrinking `activeLen`
   // just means the unread tail of the physical buffer sits idle, which is
   // harmless (see ReverbKernel.cpp's ProcessBlock comment).
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

      float ReadAtDelay(int activeLen) const
      {
         const int len = std::clamp(activeLen, 1, capacity);
         int p = writePos - len;
         p %= capacity;
         if (p < 0)
            p += capacity;
         return buf[(size_t)p];
      }

      void Write(float v)
      {
         buf[(size_t)writePos] = v;
         writePos++;
         if (writePos >= capacity)
            writePos = 0;
      }
   };

   // Fixed-length Schroeder allpass: y[n] = -g*x[n] + w[n-D]; w[n] = x[n] +
   // g*y[n]. Standard diffusion building block, series-chained ahead of the
   // FDN to smear the input transient before it hits the network.
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
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      const float rateScale = (float)(sampleRate / 44100.0);

      for (int i = 0; i < ReverbDsp::kNumLines; i++)
      {
         const int cap = (int)std::ceil(ReverbDsp::kBaseLengths44k[i] * rateScale) + 8;
         mLines[i].Prepare(cap);
      }

      // Two short allpasses, primes well below the FDN's own line lengths so
      // they diffuse the transient without themselves ringing audibly.
      mDiffuser[0].Prepare((int)std::ceil(347 * rateScale), 0.7f);
      mDiffuser[1].Prepare((int)std::ceil(113 * rateScale), 0.7f);

      const int maxPredelaySamples = (int)std::ceil(0.5 * sampleRate) + 8; // 500ms Tier 1 cap
      mPredelay.assign((size_t)std::max(8, maxPredelaySamples), 0.0f);
      mPredelayCapacity = (int)mPredelay.size();

      Reset();
   }

   void Reset() override
   {
      for (auto& line : mLines)
         line.Reset();
      mDiffuser[0].Reset();
      mDiffuser[1].Reset();
      std::fill(mPredelay.begin(), mPredelay.end(), 0.0f);
      mPredelayWrite = 0;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }
   MeterRing* ExtraMeter() override { return &mLevelMeter; } // {dry peak, wet peak} for the visualizer's level pair

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   ReverbDsp::FdnLine mLines[ReverbDsp::kNumLines];
   ReverbDsp::AllpassDiffuser mDiffuser[2];

   std::vector<float> mPredelay;
   int mPredelayCapacity = 1;
   int mPredelayWrite = 0;

   MeterRing mLevelMeter;
};
