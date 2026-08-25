#include "OutputNode.h"

#include "gl3.h"

#include "AnalyzeNodes.h"

#include "audio/AudioEngine.h"

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
   mRecorder = Platform::RecorderStart(path, mRecordW, mRecordH, recordFps, error,
                                       audioPath, audioLoop, liveAudioSampleRate, 2);
   if (mRecorder == nullptr)
   {
      mCaptureRing.enabled.store(false, std::memory_order_relaxed);
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
   if (mRecorder == nullptr)
      return;

   mCaptureRing.enabled.store(false, std::memory_order_relaxed);
   DrainAudioCapture();

   // Blocking drain: the last couple of PBO readbacks in flight must reach
   // the recorder's own queue before it's told to stop, or the movie comes
   // out short by exactly the number of buffers in the pipeline.
   FlushReadbacks();

   // Final counts come out of RecorderStop itself, after it has joined the
   // encoder worker - reading them beforehand can race the worker's last
   // few writes and under-report what actually landed in the file.
   int frames = 0;
   int dropped = 0;
   std::string error;
   const bool ok = Platform::RecorderStop(mRecorder, error, &frames, &dropped);
   mRecorder = nullptr;
   ReleaseReadbackBuffers();
   mLastFrames = frames;
   mLastDropped = dropped;

   if (ok)
   {
      mRecordStatus = "saved " + std::to_string(frames) + " frames";
      if (dropped > 0)
         mRecordStatus += " (" + std::to_string(dropped) + " dropped)";
   }
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
            std::vector<unsigned char> buf = Platform::RecorderAcquireFrameBuffer(mRecorder);
            buf.resize(bytes);
            memcpy(buf.data(), mapped, bytes);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            Platform::RecorderAppend(mRecorder, std::move(buf));
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
      PboSlot& writeSlot = mPbo[mPboWriteIndex];
      if (!writeSlot.pending)
      {
         GLint prevFbo = 0;
         glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GLUtil::FboTexture(mOut), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);

         glBindBuffer(GL_PIXEL_PACK_BUFFER, writeSlot.pbo);
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
      std::vector<unsigned char> blank = Platform::RecorderAcquireFrameBuffer(mRecorder);
      blank.assign((size_t)w * h * 4, 0);
      Platform::RecorderAppend(mRecorder, std::move(blank));
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
         glBindBuffer(GL_PIXEL_PACK_BUFFER, readSlot.pbo);
         const size_t bytes = (size_t)w * h * 4;
         const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_READ_BIT);
         if (mapped != nullptr)
         {
            std::vector<unsigned char> buf = Platform::RecorderAcquireFrameBuffer(mRecorder);
            buf.resize(bytes);
            memcpy(buf.data(), mapped, bytes);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            Platform::RecorderAppend(mRecorder, std::move(buf));
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
