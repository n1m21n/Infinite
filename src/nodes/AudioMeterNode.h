#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "core/AudioCable.h"
#include "core/INode.h"

class AudioMeterAudioNode;

// AudioMeterNode: a stereo (L/R) peak + RMS display meter with no parameters.
// Follows the two-object rule: INode on the main thread (UI, ballistics,
// pins) owning AudioMeterAudioNode on the audio thread (ProcessBlock passthrough
// and level extraction only).
class AudioMeterNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new AudioMeterNode(); }
   AudioMeterNode();
   ~AudioMeterNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& /*v*/) override {}

   INode* BypassSource() override { return input.GetSource(); }
   // A meter is a tap: it is usually wired off a source that already feeds
   // something else, with its own output left dangling. The topology builder
   // only walks back from Audio Outs, so without this an unterminated meter
   // never gets an AudioTopologyEntry, ProcessBlock never runs, and it reads
   // silence. Running whenever its input is patched makes it measure no
   // matter where its output goes (or doesn't).
   bool RequiresAudioProcessing() const override { return input.IsConnected(); }
   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }

   // Telemetry for UI visualization
   float PeakL() const { return mDisplayPeakL; }
   float PeakR() const { return mDisplayPeakR; }
   float RmsL() const { return mDisplayRmsL; }
   float RmsR() const { return mDisplayRmsR; }
   float PeakHoldL() const { return mPeakHoldL; }
   float PeakHoldR() const { return mPeakHoldR; }
   float MaxPeakL() const { return mMaxPeakL; }
   float MaxPeakR() const { return mMaxPeakR; }
   bool HasInput() const { return input.IsConnected(); }
   bool ClipL() const { return mClipL; }
   bool ClipR() const { return mClipR; }
   void ResetPeaks();

   AudioCable input;

private:
   std::unique_ptr<AudioMeterAudioNode> mAudioNode;
   int mLastCookFrame = -1;

   // UI ballistics and hold state
   float mDisplayPeakL = 0.0f;
   float mDisplayPeakR = 0.0f;
   float mDisplayRmsL = 0.0f;
   float mDisplayRmsR = 0.0f;
   float mPeakHoldL = 0.0f;
   float mPeakHoldR = 0.0f;
   float mPeakHoldTimerL = 0.0f;
   float mPeakHoldTimerR = 0.0f;
   // Highest peak since the last reset, per channel - the numeric readout.
   // Unlike the hold markers these never decay; a click clears them.
   float mMaxPeakL = 0.0f;
   float mMaxPeakR = 0.0f;
   bool mClipL = false;
   bool mClipR = false;
   std::chrono::steady_clock::time_point mLastTime{};
   bool mHasLastTime = false;
};
