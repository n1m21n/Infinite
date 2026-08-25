#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "Platform.h"

class VideoAudioNode;

// Video file source with a second, typed audio output. The decoded soundtrack
// is played by the real-time audio graph and publishes its playhead back to the
// image side, keeping picture and sound on the same clock.
class VideoSourceNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new VideoSourceNode(); }
   VideoSourceNode();
   ~VideoSourceNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return 2; }
   const char* OutputLabel(int index) const override { return index == 1 ? "audio" : "video"; }
   AudioNode* GetAudioNode() override;
   bool RequiresAudioProcessing() const override { return mVideo != nullptr; }

   int ModulatorInputCount() const override { return kCueCount; }
   IModulator** ModulatorInputSlot(int slot) override;
   const char* InputLabel(int slot) const override;

   bool OpenViaDialog();
   bool Open(const std::string& path);

   const std::string& LastError() const { return mLastError; }
   const std::string& LoadedPath() const { return mLoadedPath; }
   const std::string& AudioError() const { return mAudioError; }
   bool HasAudio() const { return mAudioLoaded; }
   double Duration() const { return mDuration; }
   double Position() const { return mPosition; }

   void Restart();
   void SeekNormalized(float normalized);
   void SetCue(int index, float normalized);
   void SetNextCue(float normalized);
   void ClearCue(int index);
   void TriggerCue(int index);
   float CueNormalized(int index) const;
   double CueSeconds(int index) const;

   bool loop = true;
   bool reverse = false;
   bool audioEnabled = true;
   float volume = 1.0f;
   float speed = 1.0f;
   float trimStart = 0.0f;
   float trimEnd = 1.0f;

   static constexpr int kCueCount = 4;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mLoadedPath);
      v.Bool("loop", loop);
      v.Bool("reverse", reverse);
      v.Bool("audioEnabled", audioEnabled);
      v.Float("volume", volume);
      v.Float("speed", speed);
      v.Float("trimStart", trimStart);
      v.Float("trimEnd", trimEnd);
      for (int i = 0; i < kCueCount; ++i)
      {
         const std::string key = "cue" + std::to_string(i + 1);
         v.Float(key.c_str(), mCueNormalized[i]);
      }
   }

   // Reloads from whatever path a patch restored. Called after loading.
   void ReloadFromPath()
   {
      if (!mLoadedPath.empty())
      {
         const std::string p = mLoadedPath;
         Open(p);
      }
   }

private:
   float EffectiveRate() const { return (reverse ? -1.0f : 1.0f) * std::fabs(speed); }
   void EnsurePlaceholder();
   void PushAudioParams();

   Platform::VideoHandle* mVideo = nullptr;
   std::unique_ptr<VideoAudioNode> mAudioNode;
   unsigned int mTex = 0;
   int mWidth = 0;
   int mHeight = 0;
   bool mHasPlaceholder = false;
   double mDuration = 0.0;
   double mPosition = 0.0;
   double mLastTransportSeconds = 0.0;
   std::vector<unsigned char> mFrame;
   std::string mLoadedPath;
   std::string mLastError;
   std::string mAudioError;
   bool mAudioLoaded = false;
   bool mWasTransportPlaying = false;
   IModulator* mCueInputs[kCueCount] = {};
   float mCueInputPrevious[kCueCount] = {};
   float mCueNormalized[kCueCount] = { -1.0f, -1.0f, -1.0f, -1.0f };
   int mLastCookFrame = -1;
};
