#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "AudioCable.h"
#include "GLUtil.h"
#include "Platform.h"
#include "gl3.h" // GLsync, for the recording readback fences below
#include "audio/AudioCaptureRing.h"

// Terminal node. Passes its input through into its own FBO (identity pass) so it
// has a real cook/output-texture lifecycle like any other node, and can also
// record the cooked result to an H.264 movie a frame at a time.
class OutputNode : public INode
{
public:
   static INode* Create() { return new OutputNode(); }

   ~OutputNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   AudioCable& AudioInput() { return mAudioInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int slot) const override { return slot == 0 ? "in" : (slot == 1 ? "audio" : nullptr); }
   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &mAudioInput : nullptr; }

   AudioCaptureRing& CaptureRing() { return mCaptureRing; }

   // --- recording ---
   bool StartRecording(const std::string& path);
   void StopRecording();
   // Marks a stop as wanted without blocking: the caller (the ImGui button)
   // sets this and returns, so its "finalizing" state gets one frame to
   // reach the screen before the driver's per-frame pump actually calls the
   // blocking StopRecording() - see the pump next to glfwPollEvents() in
   // main.cpp. Recording is still considered active (IsRecording() is still
   // true) until that call completes.
   void RequestStopRecording() { mStopRequested = true; }
   bool StopRequested() const { return mStopRequested; }
   bool IsRecording() const { return mRecorder != nullptr; }
   int RecordedFrames() const;
   const std::string& RecordStatus() const { return mRecordStatus; }
   int PendingFrames() const { return Platform::RecorderPendingFrameCount(mRecorder); }
   int DroppedFrames() const { return Platform::RecorderDroppedFrameCount(mRecorder); }
   // Final totals from the take that just ended, once StopRecording has
   // drained the queue and joined the encoder - unlike DroppedFrames()
   // above, still valid after mRecorder is gone.
   int LastRecordedFrames() const { return mLastFrames; }
   int LastDroppedFrames() const { return mLastDropped; }

   // Pure arithmetic half of the A/V pacing below, exposed so it can be
   // asserted directly (INFINITE_RECSYNCTEST) without a GL context, a device
   // or a movie file - which is what makes it checkable on Windows CI too.
   // Returns how many video frames to write for the frame just captured so
   // that emitted/fps tracks audioFrames/rate.
   static int PacedRepeat(long long audioFrames, double rate, int fps,
                          long long emitted, bool finalDrain);

   int recordFps = 30;
   bool includeAudio = false;
   int imageFormat = 0; // 0 = .png, 1 = .jpg
   int videoFormat = 0; // 0 = .mp4, 1 = .mov
   std::string exportImagePath;
   std::string recordVideoPath;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("recordFps", recordFps);
      v.Bool("includeAudio", includeAudio);
      v.Int("imageFormat", imageFormat);
      v.Int("videoFormat", videoFormat);
      v.Text("exportImagePath", exportImagePath);
      v.Text("recordVideoPath", recordVideoPath);
   }

private:
   bool EnsureShader();
   void CaptureFrame();
   void DrainAudioCapture();
   // How many video frames to emit for the frame just rendered, so the video
   // track lands on the audio track's timeline: 0 when the app is rendering
   // faster than recordFps (decimate), >1 when it is rendering slower (pad
   // with a repeat). Always 1 when this take isn't audio-paced.
   int PacedRepeatCount(bool finalDrain);


   // Triple-buffered PBO readback: CaptureFrame issues an async glReadPixels
   // into the write slot and fences it, then separately checks whether the
   // read slot (two frames behind) has become available. Never blocks the
   // render thread - see local-prompts/13-async-video-readback.md.
   void AllocateReadbackBuffers();
   void ReleaseReadbackBuffers();
   void FlushReadbacks(); // blocking drain, used on StopRecording/teardown

   ImageCable mInput;
   AudioCable mAudioInput;
   AudioCaptureRing mCaptureRing;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   Platform::RecorderHandle* mRecorder = nullptr;
   bool mStopRequested = false;
   int mRecordW = 0;
   int mRecordH = 0;
   // Latched for the whole take alongside the dimensions above, and for the
   // same reason. The recorder fixes its video PTS denominator at
   // RecorderStart and never re-reads it, so pacing against a `recordFps` the
   // user can still drag mid-take would have the pacer and the muxer working
   // off two different frame rates - dragging 30 -> 60 would emit frames twice
   // as fast as the encoder stamps them, stretching the video to double length
   // against real audio. The UI disables the slider while recording too; this
   // is the half that doesn't depend on the UI getting it right.
   int mRecordFps = 30;
   std::string mRecordStatus;
   int mLastFrames = 0;
   int mLastDropped = 0;

   // --- A/V sync ---
   // A live-audio take has two independent clocks. The video track's PTS is a
   // plain frame counter over recordFps (frameIndex/fps on macOS,
   // FrameNumberToHns(frameCount, fps) on Windows), while live audio is
   // stamped from the real sample count actually captured. Emitting one video
   // frame per *rendered* frame therefore gives the movie a video duration of
   // renderedFrames/recordFps against an audio duration of real elapsed time -
   // so unless the render loop happens to hold exactly recordFps, the two
   // drift linearly apart across the take. That is invisible while monitoring
   // (playback is real-time either way) and shows up only in the written file.
   //
   // Pacing the video against the same sample count the muxer stamps the audio
   // with locks them together by construction, and does it without trusting a
   // wall clock - the audio device clock drifts from the system clock, which
   // over a long take is its own source of desync.
   //
   // Only live audio needs this. With an audio *file* source the platform
   // recorders slave the audio to the video's synthetic clock instead
   // (AppendAudioUpTo / WriteFileAudioTrack), so that path is already in sync
   // by construction and must keep every rendered frame.
   bool mPaceToAudio = false;
   double mAudioSampleRate = 0.0;
   long long mAudioFramesAppended = 0; // audio frames handed to the recorder
   long long mFramesEmitted = 0;       // video frames handed to the recorder

   static constexpr int kPboCount = 3;
   struct PboSlot
   {
      unsigned int pbo = 0;
      GLsync fence = nullptr;
      bool pending = false; // readback issued into this slot, not yet consumed
   };
   PboSlot mPbo[kPboCount];
   int mPboWriteIndex = 0;
   int mPboReadIndex = 0;

   // Decided once, on the first readback of a take: prefer the GPU's native
   // GL_BGRA/GL_UNSIGNED_INT_8_8_8_8_REV format so glReadPixels is a straight
   // blit instead of a driver-side conversion, falling back to GL_RGBA if the
   // driver rejects it (checked via glGetError right after the first call,
   // which reports enum-validation errors synchronously even though the
   // readback itself is async). Platform::RecorderSetInputIsBgra is told the
   // result so the encoder converts the bytes correctly either way.
   bool mReadbackFormatDecided = false;
   bool mReadbackIsBgra = true;
};
