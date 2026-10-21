#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "DelayKernel.h" // reuses DelayLine's Hermite-interpolated fractional read
#include "AnalogPrimitives.h"
#include "SpectralPitchShifter.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Pitch Shifter's kernel - a time-domain (delay-line) pitch shifter with
// correlation-chosen splices, plus an analog mode.
//
// One read tap moves through a delay line at `ratio` times real time, so it
// plays the input at the shifted pitch. Moving at that rate it slowly runs
// out of room - toward zero delay when shifting up, toward the line's end
// when shifting down - and has to jump back by about one `grain`. Where it
// jumps to is chosen, not fixed: the candidate positions within the grain
// are compared against where the tap is now by normalised cross-correlation
// (on the L+R sum, so both channels splice together and the image holds),
// and the best-matching one is taken, so the two signals being crossfaded
// are in phase. The crossfade is raised-cosine and linear-sum, right for two
// aligned copies of the same waveform. This is the "deglitched" splice of
// classic digital harmonizers (cf. WSOLA, Verhelst & Roelands, ICASSP 1993,
// for the correlation search).
//
// Splices also steer around transients. A jump back (shifting up) replays
// the material it jumps over and a jump forward (shifting down) drops it, so
// a drum hit inside that span came out doubled, or not at all. The last
// onset in the line is tracked (fast/slow envelope on the input), and a
// splice whose span would cross it is shortened to land just short of it,
// with a shorter crossfade when the room left needs one.
//
// At 0 semitones the tap sits still one sample behind the input, so the
// effect is transparent (latency 1 sample, reported).
//
// `spectral` switches to SpectralPitchShifter (a phase-locked phase vocoder)
// for chords and full mixes, where one splice cannot align every partial.
// It costs 4096 samples of latency (~85 ms at 48 kHz), so it is a choice,
// not the default; the reported latency follows the mode and
// PollAudioLatencyChanges() re-runs PDC when it flips.
//
// The previous kernel summed two taps half a grain apart with permanent
// triangular weights - a comb filter that never went away, even at
// 0 semitones (PITCHTEST: no null at all), and blind splices that left
// grain-rate amplitude modulation (-9 dB of artifacts at +12 st).
class AudioEffectNode;

class PitchShiftKernel : public IEffectKernel
{
public:
   static constexpr float kMaxGrainMs = 250.0f;

   enum ParamSlot
   {
      kSemitones = 0,
      kGrainMs,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      const int maxSamples = (int)std::ceil(kMaxGrainMs * 0.001 * sampleRate) * 2 + 16;
      mLineL.Prepare(maxSamples);
      mLineR.Prepare(maxSamples);
      mSpectralEngine.Prepare(sampleRate);
      for (int i = 0; i < 2; i++)
      {
         mFilterL[i].SetSampleRate(sampleRate);
         mFilterL[i].SetCutoff(9000.0f, 0.707f);
         mFilterR[i].SetSampleRate(sampleRate);
         mFilterR[i].SetCutoff(9000.0f, 0.707f);
      }
      Reset();
   }

   void Reset() override
   {
      mLineL.Reset();
      mLineR.Reset();
      mDelay = 1.0;
      mNextDelay = 1.0;
      mFadePos = 0;
      mFadeLen = 0;
      mJitterPhase = 0.0f;
      mEnvFast = mEnvSlow = 0.0f;
      mOnsetArmed = true;
      mOnsetDelay = -1.0;
      mSpectralEngine.Reset();
      mActiveSpectral = mSpectral.load(std::memory_order_relaxed);
      for (int i = 0; i < 2; i++)
      {
         mFilterL[i].Reset();
         mFilterR[i].Reset();
      }
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override
   {
      return mSpectral.load(std::memory_order_relaxed) ? SpectralPitchShifter::kFftSize : 1;
   }
   int MaxLatencySamples() const override { return SpectralPitchShifter::kFftSize; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;
   std::atomic<int> mAnalog { 0 };
   std::atomic<int> mSpectral { 0 };
   int mActiveSpectral = 0; // audio thread: the mode last processed

   // Picks where a splice lands: the integer offset in [jMin, jMax] (added to
   // `from`, sign included) whose recent past best matches the tap's own.
   double FindSplice(double from, int jMin, int jMax, int sign, int window) const;

   SpectralPitchShifter mSpectralEngine;

   DelayLine mLineL, mLineR;
   double mDelay = 1.0;     // active tap, samples behind the newest input
   double mNextDelay = 1.0; // the tap being faded in during a splice
   int mFadePos = 0, mFadeLen = 0; // splice in progress while mFadeLen > 0

   // Onset tracking for transient-safe splices (see the header comment).
   float mEnvFast = 0.0f, mEnvSlow = 0.0f;
   bool mOnsetArmed = true;
   double mOnsetDelay = -1.0; // delay of the last onset; < 0 = none in the line

   // Analog mode components
   float mJitterPhase = 0.0f;
   DspMath::TptSvf mFilterL[2];
   DspMath::TptSvf mFilterR[2];
};
