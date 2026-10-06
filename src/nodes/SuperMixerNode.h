#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/AudioCable.h"
#include "core/INode.h"

class AudioSuperMixerNode;

// Infinite-Turbo: a 16-channel mixer - the regular Mixer's gain/pan/mute per
// strip, plus solo and a 3-band EQ (low shelf, sweepable mid peak, high
// shelf, each +/-15 dB) and a master fader. Every control is modulatable, so
// each one has a CV pin for a MIDI controller.
class SuperMixerNode : public INode, public IAudioSource
{
public:
   static constexpr int kChannels = 16;

   static INode* Create() { return new SuperMixerNode(); }
   SuperMixerNode();
   ~SuperMixerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kChannels) ? &inputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      static const char* kLabels[kChannels] = { "1", "2", "3", "4", "5", "6", "7", "8",
                                                "9", "10", "11", "12", "13", "14", "15", "16" };
      return (slot >= 0 && slot < kChannels) ? kLabels[slot] : nullptr;
   }

   // The mid frequency is inaudible while the mid gain is 0 dB (the sweep's
   // default), so the param sweep sets a mid boost first.
   std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& paramName) const override
   {
      if (paramName.rfind("eqMidFreq", 0) == 0)
         return { { "eqMid" + paramName.substr(9), 12.0f } };
      // Turbo 0.50: limiter settings only act with the limiter in and
      // working, so switch it in with a low ceiling first.
      if (paramName.rfind("fxLim", 0) == 0 && paramName != "fxLimOn")
      {
         if (paramName == "fxLimCeilingDb")
            return { { "fxLimOn", 1.0f } };
         return { { "fxLimOn", 1.0f }, { "fxLimCeilingDb", -24.0f } };
      }
      return {};
   }

   float Level() const { return mLevel; }
   float ChannelLevel(int ch) const { return (ch >= 0 && ch < kChannels) ? mChannelLevel[ch] : 0.0f; }

   float trimDb[kChannels];    // input gain (pre-EQ), -24..+24 dB
   float gainDb[kChannels];
   float pan[kChannels];
   bool mute[kChannels];
   bool solo[kChannels];
   float eqLow[kChannels];     // dB, low shelf at 120 Hz
   float eqMid[kChannels];     // dB, peak at eqMidFreq
   float eqMidFreq[kChannels]; // Hz, 200..8000
   float eqHigh[kChannels];    // dB, high shelf at 8 kHz
   float masterDb = 0.0f;
   AudioCable inputs[kChannels];

   // Turbo 0.50: master bus. Defaults reproduce the pre-0.50 output exactly
   // (balance centred, unmuted, every FX stage off). Saved after `master`.
   float masterPan = 0.0f;  // balance, -1..1 (equal-power, unity at centre)
   bool masterMute = false; // 10 ms ramp, click-free
   bool fxOpen = false;     // UI only: master FX area expanded
   bool fxEqOn = false;
   float fxEqLowHz = 100.0f, fxEqLowDb = 0.0f;
   float fxEqMidHz = 1000.0f, fxEqMidDb = 0.0f, fxEqMidQ = 0.7f;
   float fxEqHighHz = 10000.0f, fxEqHighDb = 0.0f;
   bool fxCompOn = false;
   float fxCompThreshDb = -12.0f, fxCompRatio = 2.0f, fxCompAttackMs = 10.0f, fxCompReleaseMs = 200.0f,
         fxCompMakeupDb = 0.0f;
   bool fxWidthOn = false;
   float fxWidth = 1.0f;
   bool fxSatOn = false;
   float fxSatDriveDb = 6.0f, fxSatMix = 1.0f, fxSatOutDb = 0.0f;
   bool fxLimOn = false;
   float fxLimCeilingDb = -0.3f, fxLimReleaseMs = 100.0f;
   // Turbo 0.50: limiter v2 (saved after fxLimReleaseMs). Lookahead applies
   // once the knob has rested 0.25 s (it changes the reported latency).
   float fxLimLookaheadMs = 1.5f; // 0.5..5
   float fxLimLink = 100.0f;      // stereo link, %
   bool fxLimTruePeak = true;     // 4x oversampled inter-sample peak detection
   bool fxLimAutoRelease = true;  // program-dependent dual-stage release
   int fxLimStyle = 0;            // 0 transparent, 1 punchy, 2 loud

   // Turbo 0.50: master meter state for the UI, refreshed in CookIfNeeded.
   // Ballistics live here (main thread): the audio thread only reports
   // "max since last read" peaks, a 300 ms RMS and clip flags.
   struct MasterMeter
   {
      float peakDb[2] = { -120.0f, -120.0f }; // falls at 24 dB/s
      float holdDb[2] = { -120.0f, -120.0f }; // held 1.5 s, then falls
      float holdAge[2] = {};
      float rmsDb[2] = { -120.0f, -120.0f };
      bool clip[2] = {};                      // latched until clicked
      float compGrDb = 0.0f, limGrDb = 0.0f;  // positive dB, fast attack / 20 dB/s fall
      // Turbo 0.50: limiter GR history (max per ~33 ms step, oldest first
      // from grHistPos) and safety-clamp engagements since the last reset.
      static constexpr int kGrHist = 96;
      float grHist[kGrHist] = {};
      int grHistPos = 0;
      float grHistAcc = 0.0f, grHistTime = 0.0f;
      unsigned limClamps = 0;
   };
   const MasterMeter& Meter() const { return mMeter; }
   void ClearClip() { mMeter.clip[0] = mMeter.clip[1] = false; }
   void ClearLimClamps() { mMeter.limClamps = 0; }

   // Turbo 0.50: the lookahead the audio thread runs with (quantised to
   // 0.1 ms, applied after the knob rests) and the limiter's latency in ms
   // at the current rate (what it adds while switched in).
   float LimLookaheadApplied() const;
   float LimLatencyMs() const;

private:
   std::unique_ptr<AudioSuperMixerNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
   float mChannelLevel[kChannels] = {};
   MasterMeter mMeter;
   double mLastMeterTime = -1.0;
   float mLookApplied = -1.0f, mLookPending = -1.0f;
   double mLookPendingSince = 0.0, mLookAppliedAt = -1.0e9;
};
