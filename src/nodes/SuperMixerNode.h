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

private:
   std::unique_ptr<AudioSuperMixerNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
   float mChannelLevel[kChannels] = {};
};
