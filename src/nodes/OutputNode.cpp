#include "OutputNode.h"

#include "platform/OpenGLHeaders.h"
#include "core/RuntimeLog.h"

#include "AnalyzeNodes.h"

#include "audio/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>

namespace
{
   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTex;\n"
      "void main() { fragColor = texture(uTex, vUv); }\n";

   double MonotonicSeconds()
   {
      using Clock = std::chrono::steady_clock;
      return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
   }
}

OutputNode::~OutputNode()
{
   StopRecording();
   ReleaseReadbackBuffers();
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
   if (path.empty())
   {
      mRecordStatus = "CHOOSE WHERE TO SAVE THE VIDEO FIRST";
      return false;
   }
   if (mOut.w <= 1 || mOut.h <= 1)
   {
      mRecordStatus = "NOTHING CONNECTED TO RECORD";
      return false;
   }

   // H.264 needs even dimensions; lock the size for the whole take so a
   // mid-recording resolution change can't corrupt the stream.
   mRecordW = mOut.w & ~1;
   mRecordH = mOut.h & ~1;
   mActiveRecordFps = std::max(1, recordFps);
   ReleaseReadbackBuffers();
   if (!EnsureReadbackBuffers())
   {
      mRecordStatus = "COULD NOT ALLOCATE ASYNC GPU READBACK";
      return false;
   }

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
      }
   }

   std::string error;
   mRecorder = Platform::RecorderStart(path, mRecordW, mRecordH, mActiveRecordFps, error,
                                       audioPath, audioLoop, liveAudioSampleRate, 2);
   if (mRecorder == nullptr)
   {
      mCaptureRing.enabled.store(false, std::memory_order_relaxed);
      ReleaseReadbackBuffers();
      mRecordStatus = error.empty() ? "COULD NOT START RECORDING" : error;
      return false;
   }

   mReadbackWrite = 0;
   mReadbackRead = 0;
   mReadbackPending = 0;
   mScheduledFrames = 0;
   mReadbacksCompleted = 0;
   mReadbackWaitTotalMs = 0.0;
   mReadbackWaitMaxMs = 0.0;
   mRecordStartedSeconds = MonotonicSeconds();
   RuntimeLog::Write("OUTPUT recording started: path='%s' size=%dx%d fps=%d readback=PBOx3 encoder=worker",
                     path.c_str(), mRecordW, mRecordH, mActiveRecordFps);

   mRecordStatus = (includeAudio && (!audioPath.empty() || liveAudioSampleRate > 0.0))
                      ? "RECORDING WITH AUDIO..."
                      : "RECORDING...";
   return true;
}

void OutputNode::StopRecording()
{
   if (mRecorder == nullptr)
      return;

   mCaptureRing.enabled.store(false, std::memory_order_relaxed);
   DrainAudioCapture();
   FlushReadbacks();

   const int frames = Platform::RecorderFrameCount(mRecorder);
   const int dropped = Platform::RecorderDroppedFrameCount(mRecorder);
   std::string error;
   const bool ok = Platform::RecorderStop(mRecorder, error);
   mRecorder = nullptr;
   RuntimeLog::Write("OUTPUT recording stopped: submitted=%d dropped=%d readbacks=%d gpuWaitAvg=%.3fms gpuWaitMax=%.3fms ok=%d",
                     frames, dropped, mReadbacksCompleted,
                     mReadbacksCompleted > 0 ? mReadbackWaitTotalMs / mReadbacksCompleted : 0.0,
                     mReadbackWaitMaxMs, ok ? 1 : 0);
   ReleaseReadbackBuffers();

   if (ok)
      mRecordStatus = "SAVED " + std::to_string(frames) + " FRAMES";
   else
      mRecordStatus = error;
}

void OutputNode::DrainAudioCapture()
{
   if (mRecorder == nullptr)
      return;

   float scratch[4096];
   int n;
   while ((n = mCaptureRing.Read(scratch, 4096)) > 0)
   {
      Platform::RecorderAppendAudio(mRecorder, scratch, n / 2);
   }
}

int OutputNode::RecordedFrames() const
{
   return Platform::RecorderFrameCount(mRecorder);
}

int OutputNode::PendingFrames() const
{
   return Platform::RecorderPendingFrameCount(mRecorder);
}

int OutputNode::DroppedFrames() const
{
   return Platform::RecorderDroppedFrameCount(mRecorder);
}

void OutputNode::CaptureFrame()
{
   if (mRecorder == nullptr)
      return;

   DrainAudioCapture();

   // Retire a frame from two or three renders ago only when its GPU fence is
   // ready. The normal path never waits for the current render to finish.
   DrainOneReadback(false);

   const int fps = mActiveRecordFps;
   const double elapsed = std::max(0.0, MonotonicSeconds() - mRecordStartedSeconds);
   const long long desiredFrames = (long long)std::floor(elapsed * fps) + 1;
   int repeatCount = (int)std::clamp<long long>(desiredFrames - mScheduledFrames, 0, fps * 10LL);
   if (repeatCount <= 0) return;
   mScheduledFrames = desiredFrames;

   // A resolution change during a take cannot be read safely. Keep CFR and
   // duration intact with a black frame instead of touching out-of-range GPU
   // memory; the next take picks up the new output dimensions.
   if (mOut.w < mRecordW || mOut.h < mRecordH)
   {
      std::vector<unsigned char> black = Platform::RecorderAcquireFrameBuffer(mRecorder);
      std::fill(black.begin(), black.end(), 0);
      Platform::RecorderAppend(mRecorder, std::move(black), repeatCount);
      return;
   }
   if (!SubmitReadback(repeatCount))
   {
      std::vector<unsigned char> black = Platform::RecorderAcquireFrameBuffer(mRecorder);
      std::fill(black.begin(), black.end(), 0);
      Platform::RecorderAppend(mRecorder, std::move(black), repeatCount);
   }
}

bool OutputNode::EnsureReadbackBuffers()
{
   const size_t requestedBytes = (size_t)mRecordW * mRecordH * 4;
   if (requestedBytes == 0) return false;
   if (mCaptureFbo != 0 && mReadbackBytes == requestedBytes &&
       mReadbackPbos[0] != 0 && mReadbackPbos[1] != 0 && mReadbackPbos[2] != 0)
      return true;

   ReleaseReadbackBuffers();
   while (glGetError() != GL_NO_ERROR) {}
   glGenFramebuffers(1, &mCaptureFbo);
   glGenBuffers(3, mReadbackPbos);
   GLint previousPbo = 0;
   glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPbo);
   for (unsigned int pbo : mReadbackPbos)
   {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
      glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)requestedBytes, nullptr, GL_STREAM_READ);
   }
   glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)previousPbo);
   mReadbackBytes = requestedBytes;
   const GLenum error = glGetError();
   if (error != GL_NO_ERROR || mCaptureFbo == 0 || mReadbackPbos[0] == 0 ||
       mReadbackPbos[1] == 0 || mReadbackPbos[2] == 0)
   {
      RuntimeLog::Write("OUTPUT PBO allocation failed: error=0x%04X bytes=%zu",
                        (unsigned)error, requestedBytes);
      ReleaseReadbackBuffers();
      return false;
   }
   return true;
}

void OutputNode::ReleaseReadbackBuffers()
{
   for (GLsync& fence : mReadbackFences)
   {
      if (fence != nullptr) glDeleteSync(fence);
      fence = nullptr;
   }
   if (mReadbackPbos[0] != 0 || mReadbackPbos[1] != 0 || mReadbackPbos[2] != 0)
      glDeleteBuffers(3, mReadbackPbos);
   std::fill(std::begin(mReadbackPbos), std::end(mReadbackPbos), 0u);
   std::fill(std::begin(mReadbackRepeats), std::end(mReadbackRepeats), 0);
   if (mCaptureFbo != 0) glDeleteFramebuffers(1, &mCaptureFbo);
   mCaptureFbo = 0;
   mReadbackBytes = 0;
   mReadbackWrite = 0;
   mReadbackRead = 0;
   mReadbackPending = 0;
}

bool OutputNode::SubmitReadback(int repeatCount)
{
   if (mRecorder == nullptr || !EnsureReadbackBuffers()) return false;
   if (mReadbackPending == 3 && !DrainOneReadback(true)) return false;

   const int slot = mReadbackWrite;
   GLint previousFbo = 0;
   GLint previousPbo = 0;
   GLint previousPackAlignment = 4;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
   glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPbo);
   glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

   glBindFramebuffer(GL_FRAMEBUFFER, mCaptureFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          GLUtil::FboTexture(mOut), 0);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
   {
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFbo);
      return false;
   }
   glBindBuffer(GL_PIXEL_PACK_BUFFER, mReadbackPbos[slot]);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, mRecordW, mRecordH, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   mReadbackFences[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

   glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)previousPbo);
   glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFbo);
   if (mReadbackFences[slot] == nullptr)
   {
      glFinish();
      RuntimeLog::Write("OUTPUT could not create an OpenGL readback fence");
      return false;
   }
   mReadbackRepeats[slot] = std::max(1, repeatCount);
   mReadbackWrite = (mReadbackWrite + 1) % 3;
   ++mReadbackPending;
   return true;
}

bool OutputNode::DrainOneReadback(bool waitForGpu)
{
   if (mReadbackPending <= 0 || mRecorder == nullptr) return false;
   const int slot = mReadbackRead;
   GLsync fence = mReadbackFences[slot];
   if (fence == nullptr) return false;

   const auto waitStarted = std::chrono::steady_clock::now();
   GLenum wait = glClientWaitSync(fence,
                                  waitForGpu ? GL_SYNC_FLUSH_COMMANDS_BIT : 0,
                                  waitForGpu ? 1000000000ULL : 0ULL);
   if (!waitForGpu && (wait == GL_TIMEOUT_EXPIRED || wait == GL_WAIT_FAILED)) return false;
   if (wait == GL_TIMEOUT_EXPIRED)
   {
      glFinish();
      wait = GL_ALREADY_SIGNALED;
   }

   const double waitedMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - waitStarted).count();
   mReadbackWaitTotalMs += waitedMs;
   mReadbackWaitMaxMs = std::max(mReadbackWaitMaxMs, waitedMs);

   GLint previousPbo = 0;
   glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPbo);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, mReadbackPbos[slot]);
   const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                                         (GLsizeiptr)mReadbackBytes, GL_MAP_READ_BIT);
   std::vector<unsigned char> pixels = Platform::RecorderAcquireFrameBuffer(mRecorder);
   if (pixels.size() != mReadbackBytes) pixels.resize(mReadbackBytes);
   if (mapped != nullptr)
   {
      std::memcpy(pixels.data(), mapped, mReadbackBytes);
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
   }
   else
      std::fill(pixels.begin(), pixels.end(), 0);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)previousPbo);
   glDeleteSync(fence);
   mReadbackFences[slot] = nullptr;

   const int repeats = std::max(1, mReadbackRepeats[slot]);
   mReadbackRepeats[slot] = 0;
   mReadbackRead = (mReadbackRead + 1) % 3;
   --mReadbackPending;
   ++mReadbacksCompleted;
   Platform::RecorderAppend(mRecorder, std::move(pixels), repeats);
   return true;
}

void OutputNode::FlushReadbacks()
{
   while (mReadbackPending > 0)
   {
      if (!DrainOneReadback(true))
      {
         RuntimeLog::Write("OUTPUT could not flush an asynchronous GPU readback");
         break;
      }
   }
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
