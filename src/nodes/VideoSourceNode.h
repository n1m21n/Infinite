#pragma once

#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "Platform.h"

class VideoAudioNode;

// Video file source. Playback position comes from the global Transport, so the
// play/pause button freezes video alongside every modulator, and the same patch
// re-renders identically when recording.
//
// Also an IAudioSource: output 0 is the picture, output 1 is the clip's own
// audio track (if it has one). See VideoAudioNode in VideoSourceNode.cpp for
// how the audio half derives its read position from the same Transport-driven
// timeline as the picture, rather than running an independent clock.
class VideoSourceNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new VideoSourceNode(); }
   VideoSourceNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~VideoSourceNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return 2; }
   const char* OutputLabel(int i) const override { return i == 1 ? "audio" : "video"; }

   AudioNode* GetAudioNode() override;
   // Output 0 (video) is not an audio source; only output 1 is. VideoSourceNode
   // is the first node with both an image output and an audio output, so the
   // default (every output is the audio one) is wrong here - see INode.h's
   // IAudioSource::IsAudioOutputIndex.
   bool IsAudioOutputIndex(int index) const override { return index == 1; }

   bool OpenViaDialog();
   bool Open(const std::string& path);

   const std::string& LastError() const { return mLastError; }
   const std::string& LoadedPath() const { return mLoadedPath; }
   double Duration() const { return mDuration; }
   double Position() const { return mPosition; }
   // Test-only instrumentation: counts successful Platform::VideoFrameAt
   // calls, so a self-test can tell "position advanced but the displayed
   // frame didn't" (a real freeze) apart from "position legitimately didn't
   // move" (e.g. paused).
   int FrameUpdateCount() const { return mFrameUpdateCount; }

   bool HasAudio() const { return mAudioLoaded; }
   const std::string& AudioError() const { return mAudioError; }

   bool loop = true;
   float speed = 1.0f;
   bool audioEnabled = true;
   float volume = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mLoadedPath);
      v.Bool("loop", loop); v.Float("speed", speed);
      v.Bool("audioEnabled", audioEnabled); v.Float("volume", volume);
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
   void EnsurePlaceholder();
   void LoadAudioTrack(const std::string& path);

   Platform::VideoHandle* mVideo = nullptr;
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
   int mLastCookFrame = -1;
   int mFrameUpdateCount = 0;

   std::unique_ptr<VideoAudioNode> mAudioNode;
   bool mAudioLoaded = false;
   std::string mAudioError;
};
