#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"

// Delay's kernel - cut down to KHS Audio's Delay control surface (Time,
// Tone, Feedback, Pan, Duck, a Bounce on/off switch, Mix - 7 controls, one
// processing mode) per .claude/skills/new-audio-node/SKILL.md's minimalism
// rule. An earlier version of this file had 4 selectable modes (simple/
// ping-pong/multiband/multitap) and 39 params; multiband and multitap are
// gone entirely, not hidden - Bounce is a plain bool, not a mode selector.
//
// Primary reference: Valimaki et al., "Fractional Delay Filters - Design and
// Applications", for the interpolated-read requirement; the 4-point
// Hermite/Catmull-Rom coefficients are the standard closed-form cubic used
// throughout that literature for a 4-tap window (see DelayLine::Read below).
class AudioEffectNode;

// A single mono delay line: a plain circular buffer, written once per sample
// and read at an arbitrary fractional delay via 4-point Hermite
// interpolation. Allocation happens once in Prepare() (main thread, via
// PrepareToPlay) - Write/Read below touch no memory beyond the pre-sized
// buffer, so they're safe on the audio thread.
struct DelayLine
{
   std::vector<float> buf;
   int size = 1;
   int writePos = 0;

   void Prepare(int maxSamples)
   {
      size = std::max(8, maxSamples);
      buf.assign((size_t)size, 0.0f);
      writePos = 0;
   }

   void Reset()
   {
      std::fill(buf.begin(), buf.end(), 0.0f);
      writePos = 0;
   }

   void Write(float v)
   {
      buf[writePos] = v;
      writePos++;
      if (writePos >= size)
         writePos = 0;
   }

   // Sample `k` samples older than the one just written (k=0 is the most
   // recent write).
   float SampleAtDelay(int k) const
   {
      int p = writePos - 1 - k;
      p %= size;
      if (p < 0)
         p += size;
      return buf[(size_t)p];
   }

   // 4-point (3rd-order) Hermite/Catmull-Rom interpolated read, per
   // Valimaki's fractional-delay-filter literature. Clamped to
   // [1, size - 3] so the 4-tap window (idx-1 .. idx+2) always stays
   // in-bounds. At an exact integer delaySamples (frac == 0) this returns
   // SampleAtDelay(idx) exactly - c1/c2/c3's frac terms all vanish - which is
   // what makes a steady, unswept delay time land on the exact expected
   // sample.
   float Read(float delaySamples) const
   {
      const float d = std::clamp(delaySamples, 1.0f, (float)size - 3.0f);
      const int idx = (int)d;
      const float frac = d - (float)idx;

      const float y0 = SampleAtDelay(idx - 1);
      const float y1 = SampleAtDelay(idx);
      const float y2 = SampleAtDelay(idx + 1);
      const float y3 = SampleAtDelay(idx + 2);

      const float c0 = y1;
      const float c1 = 0.5f * (y2 - y0);
      const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
      const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
      return ((c3 * frac + c2) * frac + c1) * frac + c0;
   }
};

// Single-knob "tone" shaping on the feedback path: a bipolar tilt around a
// fixed 1 kHz pivot (negative = darker repeats, positive = brighter). At
// tone=0 the SVF's low/high outputs cancel and y=x exactly.
struct DelayToneFilter
{
   DspMath::TptSvf tiltSvf;

   void Reset() { tiltSvf.Reset(); }

   float Process(float x, float tone, float sampleRate)
   {
      if (tone != 0.0f)
      {
         tiltSvf.SetSampleRate(sampleRate);
         tiltSvf.SetCutoff(1000.0f, 0.707f);
         const DspMath::TptSvf::Outputs o = tiltSvf.Process(x);
         x = x + tone * (o.high - o.low) * 0.5f;
      }
      return x;
   }
};

// AudioEffectNode's kernel for the Delay node.
class DelayKernel : public IEffectKernel
{
public:
   static constexpr float kMaxDelaySeconds = 4.0f;

   enum ParamSlot
   {
      kFeedbackPct = 0,
      kTone,
      kFreeMs,
      kPan,
      kDucking,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      const int maxSamples = (int)std::ceil(kMaxDelaySeconds * sampleRate) + 8;
      mLine.Prepare(maxSamples);
      mLineA.Prepare(maxSamples);
      mLineB.Prepare(maxSamples);
      mDelaySmoother.SetTimeConstant(0.02f, sampleRate); // slower than the mailbox's ~5ms so a time change sweeps, not zippers
      mDelaySmootherInit = false;
      Reset();
   }

   void Reset() override
   {
      mLine.Reset();
      mLineFilter.Reset();
      mLineA.Reset();
      mLineB.Reset();
      mFilterA.Reset();
      mFilterB.Reset();
      mDryEnv = 0.0f;
      mDelaySmootherInit = false;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }
   MeterRing* ExtraMeter() override { return &mLevelMeter; } // {dry peak, wet peak} for the visualizer's level pair

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mBounce { 0 };
   std::atomic<int> mSync { 1 };
   std::atomic<int> mRateDiv { MusicTime::kEighth };

   // Bounce off: one mono tap, panned to the stereo field by `pan`.
   DelayLine mLine;
   DelayToneFilter mLineFilter;

   // Bounce on: two cross-feeding lines, one per side.
   DelayLine mLineA, mLineB;
   DelayToneFilter mFilterA, mFilterB;

   DspMath::OnePole mDelaySmoother;
   bool mDelaySmootherInit = false;

   float mDryEnv = 0.0f; // mono duck envelope, off the downmixed dry input

   MeterRing mLevelMeter;
};
