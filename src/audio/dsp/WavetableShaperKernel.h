#pragma once

#include <algorithm>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"
#include "audio/Wavetable.h"
#include "audio/dsp/DriveKernel.h" // DcBlocker
#include "audio/dsp/MinPhaseOversampler.h"

class AudioEffectNode;

// Wavetable Shaper's kernel - a wavetable frame used as a waveshaping
// transfer function, in the spirit of KHS Audio's wavetable shaper module:
// the input sample's amplitude selects a phase into the table, and the
// table's value at that phase is the output. Drive pushes the phase past the
// table's ends, where Wavetable::Sample's own wrap (`phase01 -= floor(phase01)`)
// folds it back rather than clipping it - true wavefolding, seamless because
// every bank table is synthesised from a harmonic series and is therefore
// periodic and continuous end-to-end (see Wavetable.h's class comment).
// `position` morphs the curve across the table's 8 frames. `stereo` offsets
// `position` in opposite directions for L and R (L: position - stereo*0.5,
// R: position + stereo*0.5, each fed through Shape()'s own position clamp -
// no separate policy), so the two channels are shaped by different frames.
// At stereo=0 both channels land back on the same frame - bit-identical to
// the pre-stereo output, which is what keeps existing patches sounding the
// same after load.
//
// `smooth` selects (a continuous crossfade of) the bank's mip level. A
// band-limited frame has strictly less high-order harmonic content in its
// output than the full-bandwidth one, which makes `smooth` a real, principled
// brightness control - see Wavetable.h lines 1-33.
//
// Anti-aliasing is built in, with no control. The table read is a
// phase-modulated oscillator: the input moves the read phase, and harmonic h
// of the table comes out at h times the phase's instantaneous speed. So the
// curve runs at 8x the sample rate (MinPhaseOversampler.h: 5 or 6 samples
// of delay where the linear-phase one cost 73), and every oversampled
// sample reads the finest mip level whose harmonics all stay under the
// oversampled Nyquist at the current phase speed (crossfaded continuously
// between levels, so there is no zipper). What remains above the base
// Nyquist the decimator removes. `smooth` sets the floor of that choice -
// the curve is never brighter than it asks for, and only darker where it
// would otherwise alias. ALIASTEST gates the result. The dry half of the
// mix runs through a second, identical oversampler (ProcessDry), as in
// DriveKernel.h.
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
   // Where the input sample lands in the table, in cycles.
   inline double ReadPhase(float x, float driveDb, float bias)
   {
      return 0.5 + 0.5 * (double)(x + bias) * (double)DspMath::DbToLinear(driveDb);
   }

   // The table value at `phase01`, reading a continuous mip position
   // (0 = full bandwidth .. kMipLevels-1 = fundamental only).
   inline float ReadAtMip(double phase01, int table, float position, float mipPos)
   {
      // NaN/inf input reaches here as a NaN phase (and slew mip) through the
      // oversampler; std::clamp passes NaN on and the (int) casts below are
      // then undefined. Read the table's zero point instead - AudioEffectNode
      // already resets the kernel and drops the block (B-K-023).
      if (!std::isfinite(phase01))
         phase01 = 0.0;
      if (!(mipPos >= 0.0f))
         mipPos = 0.0f;
      if (!(position >= 0.0f))
         position = 0.0f;
      const float posClamped = std::clamp(position, 0.0f, 1.0f);
      const float framePos = posClamped * (float)(Wavetable::kFrames - 1);
      const int frameLo = (int)framePos;
      const int frameHi = std::min(frameLo + 1, Wavetable::kFrames - 1);
      const float frameFrac = framePos - (float)frameLo;

      // Both frames read at any one moment must come from the same mip level
      // (Wavetable::Sample's own requirement) - so the mip crossfade below is
      // two full frame-interpolated reads blended together, not a per-sample
      // mip pick.
      mipPos = std::clamp(mipPos, 0.0f, (float)(Wavetable::kMipLevels - 1));
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

   inline float SmoothToMip(float smooth) { return std::clamp(smooth, 0.0f, 1.0f) * (float)(Wavetable::kMipLevels - 1); }

   // The curve is the table read negated: x = 0 sits at phase 0.5, where a
   // sine frame falls, so the raw read is -sin(pi x) - polarity-inverting.
   // Under Wavetable Shaper's linear mix that inverted wet cancelled the dry
   // fundamental (a null near mix 0.24); negated, the spawn default is
   // sin(pi x), in phase with its input. The kernel applies the same sign
   // (zeroRef - read).
   inline float RawShape(float x, int table, float position, float driveDb, float bias, float smooth)
   {
      return -ReadAtMip(ReadPhase(x, driveDb, bias), table, position, SmoothToMip(smooth));
   }

   // The coarsest-needed mip for a read phase moving `phaseInc` cycles per
   // (oversampled) sample: level L holds harmonics up to 512 >> L, and the
   // top one must stay under Nyquist, so L >= log2(1024 * phaseInc).
   inline float MipForPhaseSpeed(double phaseInc)
   {
      if (phaseInc <= 1.0e-9)
         return 0.0f;
      return (float)std::log2(2.0 * Wavetable::kMaxHarmonic * phaseInc);
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
      kStereo,
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
      for (int ch = 0; ch < kMaxChannels; ch++)
         mDcBlockers[ch].SetSampleRate(sampleRate);
      mOversampler.Prepare(kOversample, sampleRate);
      mDryOversampler.Prepare(kOversample, sampleRate);
      Reset();
   }

   static constexpr int kOversample = 8;

   void Reset() override
   {
      mOversampler.Reset();
      mDryOversampler.Reset();
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         mDcBlockers[ch].Reset();
         mPrevPhase[ch] = 0.5;
      }
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;
   bool ProcessDry(const AudioBuffer& in, AudioBuffer& dry) override;

   int LatencySamples() const override { return mOversampler.Latency(); }
   int MaxLatencySamples() const override { return DriveKernel::kMaxLatency; }

   // The block's last raw input sample (channel 0, pre-shape) - the
   // visualizer's live operating-point dot on the transfer curve, the same
   // role DynamicsKernel's ExtraMeter plays for its GR dot. A meter, not a
   // control, so it doesn't count against the node's control budget.
   MeterRing* ExtraMeter() override { return &mLevelMeter; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0; // written by PrepareToPlay only (audio-thread owned)

   MinPhaseOversampler mOversampler;
   MinPhaseOversampler mDryOversampler; // the dry half of the mix, see ProcessDry
   double mPrevPhase[kMaxChannels] = {};
   DcBlocker mDcBlockers[kMaxChannels];
   MeterRing mLevelMeter;
};
