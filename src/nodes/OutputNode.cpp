#include "OutputNode.h"

#include "gl3.h"

#include "AnalyzeNodes.h"

#include "audio/AudioEngine.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTex;\n"
      "void main() { fragColor = texture(uTex, vUv); }\n";
}

OutputNode::~OutputNode()
{
   // Deleting the node mid-recording must not lose the frames already queued
   // in the PBO pipeline, and must not leak the PBOs themselves.
   if (mRecorder != nullptr)
      StopRecording();
   // A StopRecordingAsync() finalize may still be running in the background;
   // its thread captures `this` by pointer, so teardown must not proceed
   // (and free the object out from under it) until that thread is done.
   WaitForFinalize();
   if (mOfflineActive)
      RequestFinishOfflineRender(true);
   WaitForOfflineFinalize();
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool OutputNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

bool OutputNode::StartRecording(const std::string& path)
{
   if (mRecorder != nullptr)
      return false;
   if (mOut.w <= 1 || mOut.h <= 1)
   {
      mRecordStatus = "nothing connected to record";
      return false;
   }

   // H.264 needs even dimensions; lock the size for the whole take so a
   // mid-recording resolution change can't corrupt the stream.
   mRecordW = mOut.w & ~1;
   mRecordH = mOut.h & ~1;
   mRecordFps = recordFps > 0 ? recordFps : 30;

   std::string audioPath;
   bool audioLoop = true;
   double liveAudioSampleRate = 0.0;

   if (includeAudio && mAudioInput.IsConnected())
   {
      if (auto* file = dynamic_cast<AudioFileNode*>(mAudioInput.GetSource()))
      {
         if (file->IsLoaded())
         {
            audioPath = file->FilePath();
            audioLoop = file->loop;
         }
      }
      else
      {
         liveAudioSampleRate = AudioEngine::Instance().SampleRate();
         if (liveAudioSampleRate <= 0.0)
            liveAudioSampleRate = 44100.0;
         mCaptureRing.overflowCount.store(0, std::memory_order_relaxed);
         mCaptureRing.enabled.store(true, std::memory_order_relaxed);
         mPaceToAudio = true;
         mAudioSampleRate = liveAudioSampleRate;
      }
   }

   mAudioFramesAppended = 0;
   mFramesEmitted = 0;
   mReadbackFormatDecided = false;
   mReadbackIsBgra = true;

   std::string error;
   mRecorder = Platform::RecorderStart(path, mRecordW, mRecordH, mRecordFps, error,
                                       audioPath, audioLoop, liveAudioSampleRate, 2);
   if (mRecorder == nullptr)
   {
      mCaptureRing.enabled.store(false, std::memory_order_relaxed);
      mPaceToAudio = false;
      mRecordStatus = error.empty() ? "could not start recording" : error;
      return false;
   }

   AllocateReadbackBuffers();

   mRecordStatus = (includeAudio && (!audioPath.empty() || liveAudioSampleRate > 0.0))
                      ? "recording with audio..."
                      : "recording...";
   return true;
}

void OutputNode::StopRecording()
{
   WaitForFinalize(); // see its doc comment - guards mFinalizeThread reuse below
   mStopRequested = false;
   if (mRecorder == nullptr)
      return;

   mCaptureRing.enabled.store(false, std::memory_order_relaxed);
   DrainAudioCapture();

   // Blocking drain: the last couple of PBO readbacks in flight must reach
   // the recorder's own queue before it's told to stop, or the movie comes
   // out short by exactly the number of buffers in the pipeline.
   FlushReadbacks();

   Platform::RecorderHandle* handle = mRecorder;
   mRecorder = nullptr;
   ReleaseReadbackBuffers();

   // Audio the ring had to throw away is a sync problem, not just a glitch:
   // the samples are gone, so everything after the gap sits earlier in the
   // file than it was played. Surface it rather than shipping a quietly
   // shortened audio track.
   const uint64_t audioDropped =
      mPaceToAudio ? mCaptureRing.overflowCount.exchange(0, std::memory_order_relaxed) : 0;
   mPaceToAudio = false;

   ApplyFinalizeResult(FinishRecorder(handle, audioDropped));
}

void OutputNode::StopRecordingAsync()
{
   WaitForFinalize(); // must not reassign a still-joinable mFinalizeThread below
   mStopRequested = false;
   if (mRecorder == nullptr)
      return;

   // Same GL-bound work as StopRecording() - this has to run on the render
   // thread (the only thread with the GL context) and is normally fast: the
   // PBOs it's draining were issued a frame or two ago, so glClientWaitSync
   // returns almost immediately. Only Platform::RecorderStop below - the
   // encoder join and AVAssetWriter's finishWriting - is the part that can
   // take tens of seconds under a big backlog, so that's the part that moves
   // to a background thread.
   mCaptureRing.enabled.store(false, std::memory_order_relaxed);
   DrainAudioCapture();
   FlushReadbacks();

   Platform::RecorderHandle* handle = mRecorder;
   mRecorder = nullptr;
   ReleaseReadbackBuffers();

   const uint64_t audioDropped =
      mPaceToAudio ? mCaptureRing.overflowCount.exchange(0, std::memory_order_relaxed) : 0;
   mPaceToAudio = false;

   mRecordStatus = "finalizing...";
   mFinalizeDone.store(false, std::memory_order_relaxed);
   // Ownership of `handle` moves entirely into the thread: nothing else on
   // OutputNode touches it again (PendingFrames/DroppedFrames both check
   // IsFinalizing() first - see their doc comments in OutputNode.h).
   mFinalizeThread = std::thread([this, handle, audioDropped]()
   {
      FinalizeResult r = FinishRecorder(handle, audioDropped);
      mFinalizeResult = std::move(r);
      mFinalizeDone.store(true, std::memory_order_release);
   });
}

OutputNode::FinalizeResult OutputNode::FinishRecorder(Platform::RecorderHandle* handle, uint64_t audioDropped)
{
   FinalizeResult r;
   r.audioDropped = audioDropped;

   // Final counts come out of RecorderStop itself, after it has joined the
   // encoder worker - reading them beforehand can race the worker's last
   // few writes and under-report what actually landed in the file.
   r.ok = Platform::RecorderStop(handle, r.error, &r.frames, &r.dropped);
   return r;
}

void OutputNode::ApplyFinalizeResult(const FinalizeResult& r)
{
   mLastFrames = r.frames;
   mLastDropped = r.dropped;
   if (r.ok)
   {
      mRecordStatus = "saved " + std::to_string(r.frames) + " frames";
      if (r.dropped > 0)
         mRecordStatus += " (" + std::to_string(r.dropped) + " dropped)";
      if (r.audioDropped > 0)
         mRecordStatus += " (audio gap: " + std::to_string(r.audioDropped / 2) + " frames lost)";
   }
   else
      mRecordStatus = r.error;
}

void OutputNode::PollFinalize()
{
   if (!mFinalizeThread.joinable() || !mFinalizeDone.load(std::memory_order_acquire))
      return;
   mFinalizeThread.join();
   ApplyFinalizeResult(mFinalizeResult);
}

void OutputNode::WaitForFinalize()
{
   if (mFinalizeThread.joinable())
   {
      mFinalizeThread.join();
      ApplyFinalizeResult(mFinalizeResult);
   }
}

bool OutputNode::StartOfflineRender(const std::string& path, double audioSampleRate)
{
   if (mRecorder != nullptr || IsFinalizing() || mOfflineActive || IsOfflineFinalizing())
      return false;
   if (mOut.w <= 1 || mOut.h <= 1)
   {
      mRecordStatus = "nothing connected to record";
      return false;
   }

   mOfflineRecordW = mOut.w & ~1;
   mOfflineRecordH = mOut.h & ~1;
   mOfflineRecordFps = offlineFps > 0 ? offlineFps : 30;
   mOfflineTotalFrames = mOfflineRecordFps * (offlineDurationSeconds > 0 ? offlineDurationSeconds : 1);
   mOfflinePrerollRemaining = offlinePrerollFrames > 0 ? offlinePrerollFrames : 0;
   mOfflineFramesDone = 0;
   mOfflineAudioSampleRate = 0.0;
   mOfflineAudioFramesGenerated = 0;
   mOfflineAudioFramesAppended = 0;

   std::string audioPath;
   bool audioLoop = true;
   double liveAudioSampleRate = 0.0;
   mOfflineIncludeAudio = includeAudio && mAudioInput.IsConnected();

   if (mOfflineIncludeAudio)
   {
      if (auto* file = dynamic_cast<AudioFileNode*>(mAudioInput.GetSource()))
      {
         if (file->IsLoaded())
         {
            audioPath = file->FilePath();
            audioLoop = file->loop;
         }
      }
      else
      {
         // Non-file audio (synthesized in the graph) is rendered block by
         // block via AudioEngine::ProcessOffline, one block per video frame -
         // see main.cpp's RunOfflineRenderStep. That makes the mCaptureRing
         // the audio path for this whole take, same as a live take's, just
         // fed synchronously instead of from the real device callback.
         // The graph's AudioNodes were PrepareToPlay'd at the live device's
         // rate and keep generating as if it still applies, even after
         // main.cpp detaches the device for the take - so the take must be
         // budgeted and muxed at THAT rate. Hardcoding 44100 here against a
         // 48kHz device is exactly what makes rendered audio play back at the
         // wrong speed.
         liveAudioSampleRate = audioSampleRate > 0.0 ? audioSampleRate : 44100.0;
         mOfflineAudioSampleRate = liveAudioSampleRate;

         // Drop anything already sitting in the ring before arming it. The
         // device is detached by the time this runs, but its last callbacks
         // may have landed samples in here on the way out - and those belong
         // to real time, not to this take. Left in, they ride at the head of
         // the take's audio track and push everything after them out of sync
         // with the picture.
         {
            float discard[4096];
            while (mCaptureRing.Read(discard, 4096) > 0)
               ;
         }
         mCaptureRing.overflowCount.store(0, std::memory_order_relaxed);
         mCaptureRing.enabled.store(true, std::memory_order_relaxed);
      }
   }

   std::string error;
   mOfflineRecorder = Platform::RecorderStart(path, mOfflineRecordW, mOfflineRecordH, mOfflineRecordFps, error,
                                              audioPath, audioLoop, liveAudioSampleRate, 2);
   if (mOfflineRecorder == nullptr)
   {
      mCaptureRing.enabled.store(false, std::memory_order_relaxed);
      mOfflineIncludeAudio = false;
      mOfflineAudioSampleRate = 0.0;
      mRecordStatus = error.empty() ? "could not start offline render" : error;
      return false;
   }

   Platform::RecorderSetInputIsBgra(mOfflineRecorder, false);
   mOfflineActive = true;
   mRecordStatus = "rendering...";
   return true;
}

void OutputNode::DrainOfflineAudioCapture()
{
   if (mOfflineRecorder == nullptr)
      return;

   float scratch[4096];
   int n;
   while ((n = mCaptureRing.Read(scratch, 4096)) > 0)
   {
      Platform::RecorderAppendAudio(mOfflineRecorder, scratch, n / 2);
      mOfflineAudioFramesAppended += n / 2;
   }
}

int OutputNode::OfflineAudioFramesOwed() const
{
   if (!OfflineNeedsGraphAudio())
      return 0;
   const int fps = mOfflineRecordFps > 0 ? mOfflineRecordFps : 30;
   // (framesDone + 1): the budget for the video frame that is about to be
   // captured, not the one just finished.
   const long long target = (long long)std::llround(
      (double)(mOfflineFramesDone + 1) * mOfflineAudioSampleRate / (double)fps);
   const long long owed = target - mOfflineAudioFramesGenerated;
   return owed > 0 ? (int)owed : 0;
}

void OutputNode::CaptureOfflineFrame()
{
   if (mOfflineRecorder == nullptr)
      return;

   const int w = mOfflineRecordW;
   const int h = mOfflineRecordH;

   std::vector<unsigned char> buf = Platform::RecorderAcquireFrameBuffer(mOfflineRecorder);
   buf.resize((size_t)w * h * 4);

   if (mOut.w >= w && mOut.h >= h)
   {
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, mOut.fbo);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
   }
   else
   {
      // The graph resolution shrank mid-take: feed a black frame of the
      // take's locked size rather than reading out of bounds.
      memset(buf.data(), 0, buf.size());
   }

   // Always counted, even if the append itself is dropped by the encoder -
   // this is a fixed frame budget (mOfflineTotalFrames), not a live pace, so
   // the only way the take can ever end is by counting down regardless.
   Platform::RecorderAppend(mOfflineRecorder, std::move(buf), 1);
   mOfflineFramesDone++;

   if (mOfflineIncludeAudio)
      DrainOfflineAudioCapture();
}

void OutputNode::RequestFinishOfflineRender(bool cancelled)
{
   if (!mOfflineActive || IsOfflineFinalizing())
      return;

   mOfflineActive = false;
   mCaptureRing.enabled.store(false, std::memory_order_relaxed);
   if (mOfflineIncludeAudio)
      DrainOfflineAudioCapture();
   mOfflineIncludeAudio = false;
   mOfflineAudioSampleRate = 0.0;

   const uint64_t audioDropped = mCaptureRing.overflowCount.exchange(0, std::memory_order_relaxed);

   Platform::RecorderHandle* handle = mOfflineRecorder;
   mOfflineRecorder = nullptr;

   mRecordStatus = cancelled ? "cancelling..." : "finalizing...";
   mOfflineFinalizeDone.store(false, std::memory_order_relaxed);
   mOfflineFinalizeThread = std::thread([this, handle, audioDropped, cancelled]()
   {
      FinalizeResult r = FinishRecorder(handle, audioDropped);
      if (cancelled && r.ok)
         r.error = "cancelled";
      mOfflineFinalizeResult = std::move(r);
      mOfflineFinalizeDone.store(true, std::memory_order_release);
   });
}

void OutputNode::PollOfflineFinalize()
{
   if (!mOfflineFinalizeThread.joinable() || !mOfflineFinalizeDone.load(std::memory_order_acquire))
      return;
   mOfflineFinalizeThread.join();
   const FinalizeResult& r = mOfflineFinalizeResult;
   mLastFrames = r.frames;
   mLastDropped = r.dropped;
   if (r.ok && r.error != "cancelled")
      mRecordStatus = "rendered " + std::to_string(r.frames) + " frames";
   else if (r.error == "cancelled")
      mRecordStatus = "cancelled (" + std::to_string(r.frames) + " frames kept)";
   else
      mRecordStatus = r.error;
}

void OutputNode::WaitForOfflineFinalize()
{
   if (mOfflineFinalizeThread.joinable())
   {
      mOfflineFinalizeThread.join();
      mLastFrames = mOfflineFinalizeResult.frames;
      mLastDropped = mOfflineFinalizeResult.dropped;
   }
}

void OutputNode::DrainAudioCapture()
{
   if (mRecorder == nullptr)
      return;

   float scratch[4096];
   int n;
   while ((n = mCaptureRing.Read(scratch, 4096)) > 0)
   {
      const int frames = n / 2;
      if (Platform::RecorderAppendAudio(mRecorder, scratch, frames))
         mAudioFramesAppended += frames;
   }
}

int OutputNode::RecordedFrames() const
{
   return Platform::RecorderFrameCount(mRecorder);
}

void OutputNode::AllocateReadbackBuffers()
{
   GLint prevPbo = 0;
   glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPbo);

   const GLsizeiptr bytes = (GLsizeiptr)mRecordW * mRecordH * 4;
   for (int i = 0; i < kPboCount; i++)
   {
      glGenBuffers(1, &mPbo[i].pbo);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[i].pbo);
      glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
      mPbo[i].fence = nullptr;
      mPbo[i].pending = false;
   }
   mPboWriteIndex = 0;
   mPboReadIndex = 0;

   glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPbo);
}

void OutputNode::ReleaseReadbackBuffers()
{
   for (int i = 0; i < kPboCount; i++)
   {
      if (mPbo[i].fence != nullptr)
      {
         glDeleteSync(mPbo[i].fence);
         mPbo[i].fence = nullptr;
      }
      if (mPbo[i].pbo != 0)
      {
         glDeleteBuffers(1, &mPbo[i].pbo);
         mPbo[i].pbo = 0;
      }
      mPbo[i].pending = false;
   }
}

int OutputNode::PacedRepeat(long long audioFrames, double rate, int fps,
                            long long emitted, bool finalDrain)
{
   // No live audio has reached the recorder yet - the engine may be stopped
   // or silent for this whole take. Pass frames straight through rather than
   // stalling the video on an audio stream that may never arrive.
   if (rate <= 0.0 || audioFrames <= 0)
      return 1;
   if (fps <= 0)
      fps = 30;

   const double audioSeconds = (double)audioFrames / rate;
   const long long want = (long long)(audioSeconds * (double)fps + 0.5);
   long long n = want - emitted;
   if (n <= 0)
      return 0; // rendering faster than recordFps: decimate

   // A long stall (a modal dialog, a device change) shouldn't turn into one
   // enormous burst of duplicated frames handed to the encoder in a single
   // call - that just overflows its queue and gets counted as drops. Cap the
   // catch-up per frame and let the following frames finish it; `want` keeps
   // growing off the audio clock either way, so the cap spreads the catch-up
   // out instead of losing it. The final drain has no following frames, so it
   // takes the whole remainder and pads the tail out to the audio's length.
   if (!finalDrain && n > fps)
      n = fps;
   return (int)n;
}

int OutputNode::PacedRepeatCount(bool finalDrain)
{
   if (!mPaceToAudio)
      return 1;
   return PacedRepeat(mAudioFramesAppended, mAudioSampleRate, mRecordFps, mFramesEmitted, finalDrain);
}

void OutputNode::FlushReadbacks()
{
   if (mRecorder == nullptr)
      return;

   GLint prevPbo = 0;
   glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPbo);

   const size_t bytes = (size_t)mRecordW * mRecordH * 4;
   // A generous but finite bound rather than a literal indefinite wait - if
   // the driver never signals, StopRecording should still return.
   const GLuint64 kFlushTimeoutNs = 5000000000ull; // 5s

   // Which iteration will append the last frame of the take - the slot that
   // has to pad the tail out to the audio track's full length. Not simply the
   // last iteration: slots that were never filled are skipped below.
   int lastPending = -1;
   for (int i = 0; i < kPboCount; i++)
   {
      if (mPbo[(mPboReadIndex + i) % kPboCount].pending)
         lastPending = i;
   }

   for (int i = 0; i < kPboCount; i++)
   {
      PboSlot& slot = mPbo[mPboReadIndex];
      mPboReadIndex = (mPboReadIndex + 1) % kPboCount;
      if (!slot.pending)
         continue;

      const GLenum waitResult = glClientWaitSync(slot.fence, GL_SYNC_FLUSH_COMMANDS_BIT, kFlushTimeoutNs);
      if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED)
      {
         glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
         const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_READ_BIT);
         if (mapped != nullptr)
         {
            // Last slot in the pipeline pads the tail out to the audio track's
            // full length; earlier ones pace like any other frame.
            const int repeat = PacedRepeatCount(i == lastPending);
            if (repeat > 0)
            {
               std::vector<unsigned char> buf = Platform::RecorderAcquireFrameBuffer(mRecorder);
               buf.resize(bytes);
               memcpy(buf.data(), mapped, bytes);
               if (Platform::RecorderAppend(mRecorder, std::move(buf), repeat))
                  mFramesEmitted += repeat;
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
         }
      }

      glDeleteSync(slot.fence);
      slot.fence = nullptr;
      slot.pending = false;
   }

   glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPbo);
}

void OutputNode::CaptureFrame()
{
   if (mRecorder == nullptr)
      return;

   DrainAudioCapture();

   const int w = mRecordW;
   const int h = mRecordH;

   GLint prevPbo = 0;
   glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPbo);

   if (mOut.w >= w && mOut.h >= h)
   {
      // Issue an async readback into the write slot, if it isn't still
      // waiting to be consumed by the read side below - glReadPixels into a
      // bound PBO returns immediately (a GPU-to-GPU copy), so this never
      // stalls the render thread.
      // With vsync off (or a light patch on a fast GPU) the render loop can
      // run many times the target rate, and every one of those frames would
      // otherwise cost a full-resolution glReadPixels whose result the pacing
      // below just discards. Count what the pipeline already holds and skip
      // issuing a readback we are certain to decimate - if that guess leaves
      // us short later, the padding path covers it, so this can only cost a
      // slightly staler frame, never sync.
      int inFlight = 0;
      for (int i = 0; i < kPboCount; i++)
      {
         if (mPbo[i].pending)
            inFlight++;
      }
      const bool alreadyAhead =
         mPaceToAudio && PacedRepeat(mAudioFramesAppended, mAudioSampleRate, mRecordFps,
                                     mFramesEmitted + inFlight, false) == 0;

      PboSlot& writeSlot = mPbo[mPboWriteIndex];
      if (!writeSlot.pending && !alreadyAhead)
      {
         GLint prevFbo = 0;
         glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GLUtil::FboTexture(mOut), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);

         glBindBuffer(GL_PIXEL_PACK_BUFFER, writeSlot.pbo);
         if (!mReadbackFormatDecided)
         {
            // GL_BGRA/GL_UNSIGNED_INT_8_8_8_8_REV is the implementation-preferred
            // readback format on Apple GL and on essentially all desktop GPUs -
            // glReadPixels becomes a straight blit instead of a driver-side
            // conversion. Both tokens are legal GL 3.3 core arguments, but confirm
            // the driver actually accepted them before committing the whole take
            // to that assumption: glGetError reports an enum-validation failure
            // synchronously even though the readback itself completes async.
            glGetError();
            glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
            const GLenum err = glGetError();
            mReadbackIsBgra = (err != GL_INVALID_ENUM && err != GL_INVALID_OPERATION);
            if (!mReadbackIsBgra)
               glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            Platform::RecorderSetInputIsBgra(mRecorder, mReadbackIsBgra);
            mReadbackFormatDecided = true;
         }
         else if (mReadbackIsBgra)
            glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
         else
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
         writeSlot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
         writeSlot.pending = true;
         mPboWriteIndex = (mPboWriteIndex + 1) % kPboCount;

         glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
         glDeleteFramebuffers(1, &fbo);
      }
   }
   else
   {
      // The graph resolution shrank mid-take: the locked-size region no
      // longer fits, so feed the encoder a black frame of the take's size
      // rather than reading out of bounds or leaving the movie short.
      const int repeat = PacedRepeatCount(false);
      if (repeat > 0)
      {
         std::vector<unsigned char> blank = Platform::RecorderAcquireFrameBuffer(mRecorder);
         blank.assign((size_t)w * h * 4, 0);
         if (Platform::RecorderAppend(mRecorder, std::move(blank), repeat))
            mFramesEmitted += repeat;
      }
   }

   // Separately, check whether the read slot - up to kPboCount-1 frames
   // behind the write above - has become available. Zero-timeout: if it
   // isn't ready, do nothing and try again next frame.
   PboSlot& readSlot = mPbo[mPboReadIndex];
   if (readSlot.pending)
   {
      const GLenum waitResult = glClientWaitSync(readSlot.fence, 0, 0);
      if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED)
      {
         // Decide how many video frames this readback is worth *before*
         // paying for the map and the copy: at repeat 0 the frame is being
         // decimated away and none of that work is needed. The slot still has
         // to be retired below either way, or the readback pipeline stalls.
         const int repeat = PacedRepeatCount(false);
         if (repeat > 0)
         {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, readSlot.pbo);
            const size_t bytes = (size_t)w * h * 4;
            const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_READ_BIT);
            if (mapped != nullptr)
            {
               std::vector<unsigned char> buf = Platform::RecorderAcquireFrameBuffer(mRecorder);
               buf.resize(bytes);
               memcpy(buf.data(), mapped, bytes);
               glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
               if (Platform::RecorderAppend(mRecorder, std::move(buf), repeat))
                  mFramesEmitted += repeat;
            }
         }
         glDeleteSync(readSlot.fence);
         readSlot.fence = nullptr;
         readSlot.pending = false;
         mPboReadIndex = (mPboReadIndex + 1) % kPboCount;
      }
   }

   glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPbo);
}

void OutputNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int tex = mInput.Pull(frameId);
   if (tex == 0)
      return;

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, mInput.Width(), mInput.Height()))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, tex]()
   {
      GLint loc = glGetUniformLocation(mProgram, "uTex");
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(loc, 0);
   });

   CaptureFrame();
}
