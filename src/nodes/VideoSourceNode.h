#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "BenchMediaIo.h"
#include "INode.h"
#include "Platform.h"
#include "Transport.h"

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

   // Adopts an already-open handle and pre-decoded audio track instead of
   // opening/decoding `path` itself - for the Arrange panel's async
   // media-drop import, where Platform::VideoOpen and the audio-track
   // decode already ran on a worker thread (see ArrangeMediaImport.h).
   // Takes ownership of `handle`; `audioTrack` may be null (no audio track
   // in the file - a normal case, not an error).
   bool OpenFromHandle(const std::string& path, Platform::VideoHandle* handle,
                       Platform::SampleBuffer* audioTrack);

   const std::string& LastError() const { return mLastError; }
   const std::string& LoadedPath() const { return mLoadedPath; }
   double Duration() const { return mDuration; }
   double Position() const { return mPosition; }

   // Arrangement Timeline exact seek (Video Sample only - see
   // ArrangeSeekVideoSampleSources in main.cpp): jumps straight to `seconds`
   // into the source instead of the free-running wall-clock-delta advance
   // CookIfNeeded normally does. Also resets mLastTransportSeconds to "now"
   // so THIS frame's CookIfNeeded (which always runs after this call - see
   // the caller) computes a near-zero delta and doesn't immediately nudge
   // the position away from what was just set. Main thread only, called
   // once per frame from the same loop that drives CookIfNeeded - never
   // from the audio thread (unlike AudioNode::SetClipSamplePosition, which is).
   void SeekTo(double seconds)
   {
      mPosition = seconds;
      mLastTransportSeconds = Transport::Instance().Seconds();
   }

   // Arrangement Timeline sync for a Video Sample (see
   // ArrangeSeekVideoSampleSources in main.cpp), called every frame while its
   // clip is under the playhead - playing OR paused, so dragging the playhead
   // while stopped seeks the picture too. Unlike SeekTo, this does NOT reset
   // the position unconditionally: it wraps `targetSeconds` into the same
   // trim/loop window CookIfNeeded computes (WrapPosition) and only calls
   // SeekTo when that differs from the current position by more than one
   // frame's worth (kSyncEpsilonSeconds). During ordinary continuous playback
   // the two already agree every frame (CookIfNeeded's own wall-clock delta
   // already tracks it exactly), so this is a no-op then - forcing an
   // unconditional absolute reset every frame instead (an earlier version of
   // this code did) fights Platform::VideoFrameAt's forward-resume fast path
   // with sub-frame floating-point jitter, which was making Video Sample
   // playback stutter/hang. A real discontinuity - a scrub, a loop wrap the
   // delta hasn't caught up to yet, or a fresh Play landing mid-clip - is
   // exactly when the wrapped target and the current position diverge, which
   // is what actually needs a hard seek.
   void SyncToArrangement(double targetSeconds)
   {
      const double wrapped = WrapPosition(targetSeconds);
      constexpr double kSyncEpsilonSeconds = 1.0 / 24.0; // ~ one frame at a typical rate
      if (std::abs(wrapped - mPosition) > kSyncEpsilonSeconds)
         SeekTo(wrapped);
   }
   // Test-only instrumentation: counts successful Platform::VideoFrameAt
   // calls, so a self-test can tell "position advanced but the displayed
   // frame didn't" (a real freeze) apart from "position legitimately didn't
   // move" (e.g. paused).
   int FrameUpdateCount() const { return mFrameUpdateCount; }
   // B8 bench only: nullptr unless Bench::MediaIoEnabled() was on when this
   // node first cooked with a clip loaded.
   const Bench::MediaClipCounters* BenchCounters() const { return mBench.get(); }
   Platform::VideoHandle* BenchVideoHandle() const { return mVideo; }

   bool HasAudio() const { return mAudioLoaded; }
   const std::string& AudioError() const { return mAudioError; }

   bool loop = true;
   float speed = 1.0f;
   bool audioEnabled = true;
   float volume = 1.0f;
   float trimStart = 0.0f;
   float trimEnd = 0.0f;   // <= 0.0 means "unset - use full duration", same sentinel convention as mDuration

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mLoadedPath);
      v.Bool("loop", loop); v.Float("speed", speed);
      v.Bool("audioEnabled", audioEnabled); v.Float("volume", volume);
      v.Float("trimStart", trimStart); v.Float("trimEnd", trimEnd);
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
   double WrapPosition(double raw) const;

   Platform::VideoHandle* mVideo = nullptr;
   unsigned int mTex = 0;
   int mWidth = 0;
   int mHeight = 0;
   int mTexWidth = 0;
   int mTexHeight = 0;
   bool mHasPlaceholder = false;
   double mDuration = 0.0;
   double mPosition = 0.0;
   double mLastTransportSeconds = 0.0;
   std::vector<unsigned char> mFrame;
   std::string mLoadedPath;
   std::string mLastError;
   int mLastCookFrame = -1;
   int mFrameUpdateCount = 0;
   std::unique_ptr<Bench::MediaClipCounters> mBench;

   std::unique_ptr<VideoAudioNode> mAudioNode;
   bool mAudioLoaded = false;
   std::string mAudioError;
};
