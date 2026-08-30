#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
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
   // Fully synchronous: blocks until the encoder is joined and the movie is
   // finalized on disk. Used by the destructor (teardown must not outlive a
   // thread touching `this`, see ~OutputNode) and by the dev/test harnesses
   // in main.cpp, which need the file to be complete the instant this
   // returns. The interactive Stop button does NOT call this directly - see
   // StopRecordingAsync below - because on a long or backlogged take this can
   // block for tens of seconds (audio backlog flush, PBO drain, encoder join,
   // AVAssetWriter's finishWriting spin), which would freeze the whole app
   // with no repaint.
   void StopRecording();
   // The interactive path: does the GL-bound work (audio drain, PBO flush)
   // on this thread since it needs the render thread's GL context, then hands
   // the recorder handle to a background thread for the part that can take a
   // long time (Platform::RecorderStop - encoder join + finishWriting) and
   // returns immediately. IsRecording() is already false by the time this
   // returns; IsFinalizing() is true until the background thread completes.
   // Call PollFinalize() once a frame to pick up completion.
   void StopRecordingAsync();
   // Marks a stop as wanted without blocking: the caller (the ImGui button)
   // sets this and returns, so its "finalizing" state gets one frame to
   // reach the screen before the driver's per-frame pump actually calls
   // StopRecordingAsync() - see the pump next to glfwPollEvents() in
   // main.cpp. Recording is still considered active (IsRecording() is still
   // true) until that call runs.
   void RequestStopRecording() { mStopRequested = true; }
   bool StopRequested() const { return mStopRequested; }
   bool IsRecording() const { return mRecorder != nullptr; }
   // True from the moment StopRecordingAsync() hands the handle to the
   // background thread until PollFinalize() has picked up its result and
   // joined it. Mutually exclusive with IsRecording().
   bool IsFinalizing() const { return mFinalizeThread.joinable(); }
   // Call once a frame (see the pump next to glfwPollEvents() in main.cpp).
   // No-op unless a StopRecordingAsync() finalize has completed, in which
   // case it joins the background thread and copies its results into
   // RecordStatus()/LastRecordedFrames()/LastDroppedFrames() - the same
   // fields StopRecording() itself populates synchronously.
   void PollFinalize();
   int RecordedFrames() const;
   const std::string& RecordStatus() const { return mRecordStatus; }
   // For callers outside this class that need to surface a status string
   // before/without ever calling Start(Offline)Recording - e.g. main.cpp's
   // up-front hardware-driven-node refusal, which has to reject a take
   // before OutputNode itself is involved at all.
   void SetRecordStatus(const std::string& s) { mRecordStatus = s; }
   // Both read the live handle, so both are 0 while IsFinalizing() - the
   // handle has already been handed off to the background thread by then,
   // and touching it from this thread while RecorderStop() may be mid-delete
   // on the other would be a data race.
   int PendingFrames() const { return IsFinalizing() ? 0 : Platform::RecorderPendingFrameCount(mRecorder); }
   int DroppedFrames() const { return IsFinalizing() ? 0 : Platform::RecorderDroppedFrameCount(mRecorder); }
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

   // --- offline render (non-realtime export) ---
   // A second, fully separate capture path from the live recorder above: the
   // caller (main.cpp) drives the graph and AudioEngine::ProcessOffline in
   // lockstep, one fixed-size step per frame, completely divorced from wall
   // time - so a patch that only manages 10fps live can still export a
   // perfectly smooth, perfectly A/V-synced 60fps file, the same model as
   // TouchDesigner/After Effects/Final Cut's non-realtime export. Because
   // both video and audio for a frame are produced by the same synchronous
   // step (see CaptureOfflineFrame), there is no drift to correct for and
   // this path needs none of the live path's PacedRepeat pacing.
   // audioSampleRate: the live device's rate, read by the caller BEFORE it
   // detaches the device (this call happens after the detach, so it can't
   // read AudioEngine::SampleRate() itself - and must not, since the graph's
   // AudioNodes stay prepared at the detached device's rate). 0 falls back to
   // 44100. Everything downstream - the per-frame sample budget and the
   // muxer's declared audio rate - is derived from this one number; a
   // mismatch here is heard directly as the take playing back off-speed.
   bool StartOfflineRender(const std::string& path, double audioSampleRate);
   bool IsOfflineRendering() const { return mOfflineActive; }
   // True from the moment CancelOfflineRender()/the frame-count target hands
   // the handle to the background finalize thread until PollOfflineFinalize()
   // has picked up its result - mirrors IsFinalizing() above. Checked by the
   // Cancel button/main.cpp's progress dialog so clicking Cancel never blocks
   // the main thread waiting on Platform::RecorderStop.
   bool IsOfflineFinalizing() const { return mOfflineFinalizeThread.joinable(); }
   int OfflineFramesDone() const { return mOfflineFramesDone; }
   int OfflineFramesTotal() const { return mOfflineTotalFrames; }
   bool IsPrerolling() const { return mOfflinePrerollRemaining > 0; }
   int PrerollFramesRemaining() const { return mOfflinePrerollRemaining; }
   void DecrementPreroll() { if (mOfflinePrerollRemaining > 0) mOfflinePrerollRemaining--; }
   // Called once per offline step, after the graph has cooked this frame and
   // (if includeAudio) AudioEngine::ProcessOffline has just written this
   // frame's audio into mCaptureRing. Synchronous glReadPixels straight into
   // client memory - no PBO pipeline, since nothing downstream is waiting on
   // this frame's readback to hide latency behind; the whole point of this
   // path is that it's allowed to take as long as it takes.
   void CaptureOfflineFrame();
   // Async, mirrors StopRecordingAsync(): hands Platform::RecorderStop to a
   // background thread and returns immediately, so a mid-render Cancel click
   // never blocks the UI thread. Also the path taken when OfflineFramesDone()
   // reaches OfflineFramesTotal() - see main.cpp's RunOfflineRenderStep.
   void RequestFinishOfflineRender(bool cancelled);
   // Call once a frame (see the pump next to glfwPollEvents() in main.cpp).
   // No-op unless a RequestFinishOfflineRender() finalize has completed, in
   // which case it joins the background thread, clears IsOfflineRendering(),
   // and copies its results into RecordStatus().
   void PollOfflineFinalize();

   // True only for the graph-synthesized-audio case (a synth/sampler chain,
   // not an AudioFileNode - that one is baked in muxer-side and needs no
   // per-frame work). Gates the caller's AudioEngine::ProcessOffline pump.
   bool OfflineNeedsGraphAudio() const { return mOfflineIncludeAudio && mOfflineAudioSampleRate > 0.0; }
   // The device sample rate this take was started at - captured before
   // main.cpp detaches the audio device, since the graph's AudioNodes were
   // PrepareToPlay'd at that rate and will keep generating as if it still
   // applies. Everything downstream (per-frame sample budget, the muxer's
   // declared audio rate) must use THIS, never a hardcoded 44100: a 48kHz
   // device rendered as 44.1kHz plays back at the wrong speed.
   double OfflineAudioSampleRate() const { return mOfflineAudioSampleRate; }
   // How many audio frames the take still owes for the video frame about to
   // be captured, from a running cumulative target rather than a fixed
   // per-frame quota - so an fps that doesn't divide the sample rate evenly
   // (24fps @ 44100 = 1837.5) can't drift over a long take. May exceed
   // kAudioMaxBlockFrames; the caller generates it in chunks.
   int OfflineAudioFramesOwed() const;
   void NoteOfflineAudioGenerated(int frames) { mOfflineAudioFramesGenerated += frames; }
   // Audio frames actually handed to the muxer this take (self-test hook:
   // this should equal round(totalFrames * sampleRate / fps)).
   long long OfflineAudioFramesAppended() const { return mOfflineAudioFramesAppended; }

   int offlineFps = 30;
   int offlineDurationSeconds = 10;
   int offlinePrerollFrames = 0;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("recordFps", recordFps);
      v.Bool("includeAudio", includeAudio);
      v.Int("imageFormat", imageFormat);
      v.Int("videoFormat", videoFormat);
      v.Text("exportImagePath", exportImagePath);
      v.Text("recordVideoPath", recordVideoPath);
      v.Int("offlineFps", offlineFps);
      v.Int("offlineDurationSeconds", offlineDurationSeconds);
      v.Int("offlinePrerollFrames", offlinePrerollFrames);
   }

private:
   bool EnsureShader();
   void CaptureFrame();
   void DrainAudioCapture();
   // Offline-render counterpart of DrainAudioCapture(): drains mCaptureRing
   // into mOfflineRecorder instead of mRecorder. A separate function rather
   // than a parameterized one so the live path's mAudioFramesAppended pacing
   // counter (meaningless for the offline path, which is never paced) can't
   // accidentally get mixed up with it.
   void DrainOfflineAudioCapture();
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

   // Common tail of StopRecording()/StopRecordingAsync(): everything up to
   // and including handing the drained handle off to Platform::RecorderStop.
   // Runs synchronously on whichever thread calls it - the render thread for
   // StopRecording(), the background finalize thread for StopRecordingAsync().
   struct FinalizeResult
   {
      bool ok = false;
      int frames = 0;
      int dropped = 0;
      uint64_t audioDropped = 0;
      std::string error;
   };
   FinalizeResult FinishRecorder(Platform::RecorderHandle* handle, uint64_t audioDropped);
   void ApplyFinalizeResult(const FinalizeResult& r);
   // Blocks until any in-flight StopRecordingAsync() finalize has actually
   // completed and joins the thread. A no-op if none is in flight. Called by
   // the destructor (a dangling `this` in the background thread's lambda
   // would otherwise outlive the node) and defensively at the top of
   // StartRecording()/StopRecordingAsync(), so mFinalizeThread is never
   // reassigned while still joinable (std::thread::operator= on a joinable
   // thread calls std::terminate).
   void WaitForFinalize();

   // Blocks until any in-flight offline finalize completes and joins the
   // thread; a no-op if none is in flight. Called by the destructor for the
   // same reason WaitForFinalize() is - the background thread's lambda
   // captures `this`.
   void WaitForOfflineFinalize();

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

   // --- async finalize (StopRecordingAsync) ---
   std::thread mFinalizeThread;
   // Set by the finalize thread right before it returns; PollFinalize() polls
   // this rather than blocking on the thread, so the render thread never
   // waits on it. Only meaningful while mFinalizeThread.joinable().
   std::atomic<bool> mFinalizeDone{ false };
   // Staged result, written once by the finalize thread before it sets
   // mFinalizeDone (release) and read once by PollFinalize() after it
   // observes mFinalizeDone (acquire) - that pair is the only synchronization
   // these need, since nothing else touches them while a finalize is in
   // flight.
   FinalizeResult mFinalizeResult;

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

   // --- offline render (non-realtime export) ---
   Platform::RecorderHandle* mOfflineRecorder = nullptr;
   bool mOfflineActive = false;
   int mOfflineTotalFrames = 0;
   int mOfflineFramesDone = 0;
   int mOfflinePrerollRemaining = 0;
   int mOfflineRecordW = 0;
   int mOfflineRecordH = 0;
   int mOfflineRecordFps = 30;
   bool mOfflineIncludeAudio = false;
   double mOfflineAudioSampleRate = 0.0;
   long long mOfflineAudioFramesGenerated = 0;
   long long mOfflineAudioFramesAppended = 0;

   std::thread mOfflineFinalizeThread;
   std::atomic<bool> mOfflineFinalizeDone{ false };
   FinalizeResult mOfflineFinalizeResult;
};
