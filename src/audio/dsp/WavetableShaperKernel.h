#pragma once

#include <algorithm>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"
#include "audio/Wavetable.h"
#include "audio/dsp/DriveKernel.h" // DcBlocker

class AudioEffectNode;

// Wavetable Shaper's kernel - a wavetable frame used as a waveshaping
// transfer function, in the spirit of KHS Audio's wavetable shaper module:
// the input sample's amplitude selects a phase into the table, and the
// table's value at that phase is the output. Drive pushes the phase past the
// table's ends, where Wavetable::Sample's own wrap (`phase01 -= floor(phase01)`)
// folds it back rather than clipping it - true wavefolding, seamless because
// every bank table is synthesised from a harmonic series and is therefore
// periodic and continuous end-to-end (see Wavetable.h's class comment).
// `position` morphs the curve across the table's 8 frames.
//
// `smooth` selects (a continuous crossfade of) the bank's mip level. A
// band-limited frame has strictly less high-order harmonic content in its
// output than the full-bandwidth one, which makes `smooth` a real, principled
// brightness/anti-alias control that costs one extra table read instead of
// an oversampler - see Wavetable.h lines 1-33. Be honest about its limit:
// waveshaping aliases by construction, and Wavetable::MipForPhaseInc does not
// apply here at all (there is no oscillator phase increment to key off - the
// "harmonic order" this transfer curve produces is set by the input signal's
// own slew rate, not by a pitch). `smooth` is a user-facing tone control, not
// an anti-alias guarantee. Oversampling is out of scope here - see
// .claude/skills/new-audio-node/SKILL.md's minimalism rule.
//
// Zero-reference and DC follow DriveKernel.h's construction exactly: Shape()
// subtracts RawShape(0) so bias warps the curve's symmetry without sliding
// its DC point, and the DcBlocker DriveKernel.h already declares runs always
// (bias and an asymmetric table both leak a constant offset otherwise) - see
// that header for the DcBlocker itself, not re-declared here.
namespace WavetableShaperDsp
{
   // Pure function shared by the audio thread and the main-thread visualizer,
   // so both compute the byte-identical curve - the same reason DriveDsp::
   // Shape and DynamicsDsp::GainComputerDb exist. `table` is a resolved
   // integer index (the mailbox smooths the raw param and rounds it before
   // calling this, since a table crossfade isn't worth it - the dropdown is a
   // deliberate discrete change); `position` and `smooth` are continuous
   // 0..1, `driveDb` is 0..24, `bias` is -1..1.
   inline float RawShape(float x, int table, float position, float driveDb, float bias, float smooth)
   {
      const double phase01 = 0.5 + 0.5 * (double)(x + bias) * (double)DspMath::DbToLinear(driveDb);

      const float posClamped = std::clamp(position, 0.0f, 1.0f);
      const float framePos = posClamped * (float)(Wavetable::kFrames - 1);
      const int frameLo = (int)framePos;
      const int frameHi = std::min(frameLo + 1, Wavetable::kFrames - 1);
      const float frameFrac = framePos - (float)frameLo;

      // Both frames read at any one moment must come from the same mip level
      // (Wavetable::Sample's own requirement) - so the mip crossfade below is
      // two full frame-interpolated reads blended together, not a per-sample
      // mip pick.
      const float smoothClamped = std::clamp(smooth, 0.0f, 1.0f);
      const float mipPos = smoothClamped * (float)(Wavetable::kMipLevels - 1);
      const int mipLo = (int)mipPos;
      const int mipHi = std::min(mipLo + 1, Wavetable::kMipLevels - 1);
      const float mipFrac = mipPos - (float)mipLo;

      const float sampleLo = Wavetable::Sample(Wavetable::Frame(table, frameLo, mipLo),
                                                Wavetable::Frame(table, frameHi, mipLo), frameFrac, phase01);
      if (mipFrac <= 0.0f)
         return sampleLo;

      const float sampleHi = Wavetable::Sample(Wavetable::Frame(table, frameLo, mipHi),
                                                Wavetable::Frame(table, frameHi, mipHi), frameFrac, phase01);
      return sampleLo + (sampleHi - sampleLo) * mipFrac;
   }

   inline float Shape(float x, int table, float position, float driveDb, float bias, float smooth)
   {
      const float zeroRef = RawShape(0.0f, table, position, driveDb, bias, smooth);
      return RawShape(x, table, position, driveDb, bias, smooth) - zeroRef;
   }
}

class WavetableShaperKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;

   enum ParamSlot
   {
      kTable = 0,
      kPosition,
      kDrive,
      kBias,
      kSmooth,
      kOutputDb,
      kNumSlots
   };

   // Builds the wavetable bank (main thread, ~100ms the first time,
   // idempotent afterwards) before this kernel's ProcessBlock can ever run on
   // the audio thread: def.makeKernel is invoked from AudioEffectRuntime's
   // constructor, which is itself only ever constructed from
   // AudioEffectNode::CookIfNeeded or ::GetAudioNode - both main thread. See
   // Wavetable.h's class comment and AudioEffectNode.cpp.
   WavetableShaperKernel() { Wavetable::EnsureBuilt(); }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      Reset();
   }

   void Reset() override
   {
      for (int ch = 0; ch < kMaxChannels; ch++)
         mDcBlockers[ch].Reset();
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

   // The block's last raw input sample (channel 0, pre-shape) - the
   // visualizer's live operating-point dot on the transfer curve, the same
   // role DynamicsKernel's ExtraMeter plays for its GR dot. A meter, not a
   // control, so it doesn't count against the node's control budget.
   MeterRing* ExtraMeter() override { return &mLevelMeter; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   DcBlocker mDcBlockers[kMaxChannels];
   MeterRing mLevelMeter;
};
