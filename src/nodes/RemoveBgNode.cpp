#include "RemoveBgNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "Platform.h"

namespace
{
   const std::vector<std::string> kModeNames = { "Subject - U2Net", "Person - U2Net Human" };
   const std::vector<std::string> kBackendNames = { "Auto GPU (DX12)", "DirectML only", "CPU" };
   const std::vector<std::string> kOutputModeNames = { "Cutout", "Mask only", "Background only" };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uMask;\n"
      "uniform int uHasMask;\n"
      "uniform vec2 uTexel;\n"
      "uniform int uOutputMode;\n"
      "uniform float uFeather;\n"
      "uniform float uThreshold;\n"
      "uniform float uContrast;\n"
      "uniform vec3 uBgColor;\n"
      "uniform float uBgOpacity;\n"
      "void main() {\n"
      "   vec4 c = texture(uSrc, vUv);\n"
      "   if (uHasMask == 0) { fragColor = c; return; }\n"
      "\n"
      "   float m;\n"
      "   if (uFeather > 0.0) {\n"
      "      // small blur of the mask only, to soften a hard segmentation edge\n"
      "      float sum = 0.0, total = 0.0;\n"
      "      for (int x = -2; x <= 2; x++) for (int y = -2; y <= 2; y++) {\n"
      "         vec2 off = vec2(float(x), float(y)) * uTexel * uFeather * 4.0;\n"
      "         float w = exp(-float(x*x + y*y) / 4.0);\n"
      "         sum += texture(uMask, vUv + off).r * w; total += w;\n"
      "      }\n"
      "      m = sum / max(total, 1e-4);\n"
      "   } else {\n"
      "      m = texture(uMask, vUv).r;\n"
      "   }\n"
      "\n"
      "   m = clamp((m - uThreshold) * uContrast + 0.5, 0.0, 1.0);\n"
      "\n"
      "   if (uOutputMode == 1) { fragColor = vec4(vec3(m), 1.0); return; }\n"
      "   if (uOutputMode == 2) m = 1.0 - m;\n"
      "\n"
      "   vec3 bg = uBgColor;\n"
      "   fragColor = vec4(mix(bg, c.rgb, m), max(c.a * m, uBgOpacity * (1.0 - m)));\n"
      "}\n";
}

const std::vector<std::string>& RemoveBgNode::ModeNames() { return kModeNames; }
const std::vector<std::string>& RemoveBgNode::BackendNames() { return kBackendNames; }
const std::vector<std::string>& RemoveBgNode::OutputModeNames() { return kOutputModeNames; }

RemoveBgNode::~RemoveBgNode()
{
   {
      std::lock_guard<std::mutex> lock(mWorkerMutex);
      mWorkerStop = true;
   }
   mWorkerWake.notify_one();
   if (mWorker.joinable())
      mWorker.join();

   GLUtil::DestroyFbo(mOut);
   if (mMaskTex != 0)
      glDeleteTextures(1, &mMaskTex);
   for (GpuFrame& f : mGpuFrames) GLUtil::DestroyFbo(f.fbo);
   for (GpuFrame& f : mFreeGpuFrames) GLUtil::DestroyFbo(f.fbo);
   for (PendingReadback& rb : mPendingReadbacks) { glDeleteSync((GLsync)rb.fence); mFreePbos.push_back(rb.pbo); }
   if (!mFreePbos.empty()) glDeleteBuffers((GLsizei)mFreePbos.size(), mFreePbos.data());
   GLUtil::DestroyFbo(mSmall);
   if (mReadFbo != 0)
      glDeleteFramebuffers(1, &mReadFbo);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool RemoveBgNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

// Turbo: the mask pipeline never stalls the render thread.
//  1. The input is copied on the GPU (blit) into a full-resolution slot that
//     stays paired with its mask, so cutout and mask always line up.
//  2. A downscaled copy (longest side kMaskReadbackMaxSide - the models run at
//     320 px anyway) is read back through a PBO + fence, collected on a later
//     frame instead of blocking on glReadPixels.
//  3. The worker only ever sees the newest frame; older waiting requests are
//     dropped, so the mask never lags behind by a queue of stale frames.
namespace
{
   constexpr int kMaskReadbackMaxSide = 512;
}

void RemoveBgNode::QueueMask(unsigned int srcTex, int w, int h)
{
   PollReadbacks();

   const size_t maxInFlight = (size_t)std::clamp(frameCache, 1, 4);
   if (mPendingReadbacks.size() >= maxInFlight)
      return;
   {
      std::lock_guard<std::mutex> lock(mWorkerMutex);
      // A fresh frame is already waiting for the worker: don't pay for another
      // copy until it has been picked up.
      if (!mRequests.empty())
         return;
   }

   const float scale = std::min(1.0f, (float)kMaskReadbackMaxSide / (float)std::max(w, h));
   const int sw = std::max(1, (int)std::lround(w * scale));
   const int sh = std::max(1, (int)std::lround(h * scale));
   if (!GLUtil::EnsureFbo(mSmall, sw, sh))
      return;

   GpuFrame frame;
   for (size_t i = 0; i < mFreeGpuFrames.size(); ++i)
   {
      if (mFreeGpuFrames[i].fbo.w == w && mFreeGpuFrames[i].fbo.h == h)
      {
         frame = mFreeGpuFrames[i];
         mFreeGpuFrames.erase(mFreeGpuFrames.begin() + (long)i);
         break;
      }
   }
   if (!GLUtil::EnsureFbo(frame.fbo, w, h))
      return;

   GLint prevRead = 0, prevDraw = 0, prevPbo = 0, prevPack = 4;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
   glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
   glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPbo);
   glGetIntegerv(GL_PACK_ALIGNMENT, &prevPack);

   if (mReadFbo == 0)
      glGenFramebuffers(1, &mReadFbo);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, mReadFbo);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frame.fbo.fbo);
   glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mSmall.fbo);
   glBlitFramebuffer(0, 0, w, h, 0, 0, sw, sh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

   PendingReadback rb;
   rb.w = sw;
   rb.h = sh;
   rb.serial = ++mNextSerial;
   if (!mFreePbos.empty())
   {
      rb.pbo = mFreePbos.back();
      mFreePbos.pop_back();
   }
   else
      glGenBuffers(1, &rb.pbo);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo);
   glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)sw * sh * 4, nullptr, GL_STREAM_READ);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, mSmall.fbo);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   rb.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

   glPixelStorei(GL_PACK_ALIGNMENT, prevPack);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPbo);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);

   frame.serial = rb.serial;
   mGpuFrames.push_back(frame);
   if (rb.fence == nullptr)
   {
      mFreePbos.push_back(rb.pbo);
      return;
   }
   mPendingReadbacks.push_back(rb);
}

void RemoveBgNode::PollReadbacks()
{
   while (!mPendingReadbacks.empty())
   {
      PendingReadback rb = mPendingReadbacks.front();
      const GLenum state = glClientWaitSync((GLsync)rb.fence, 0, 0);
      if (state == GL_TIMEOUT_EXPIRED)
         break; // GPU not there yet - collect it on a later frame
      mPendingReadbacks.pop_front();
      glDeleteSync((GLsync)rb.fence);

      std::vector<unsigned char> pixels;
      if (state != GL_WAIT_FAILED)
      {
         GLint prevPbo = 0;
         glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPbo);
         glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo);
         const size_t bytes = (size_t)rb.w * rb.h * 4;
         const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_READ_BIT);
         if (mapped != nullptr)
         {
            pixels.assign((const unsigned char*)mapped, (const unsigned char*)mapped + bytes);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
         }
         glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPbo);
      }
      mFreePbos.push_back(rb.pbo);
      if (pixels.empty())
         continue;

      {
         std::lock_guard<std::mutex> lock(mWorkerMutex);
         FrameRequest request;
         request.pixels = std::move(pixels);
         request.width = rb.w;
         request.height = rb.h;
         request.mode = mode;
         request.backend = backend;
         request.serial = rb.serial;
         mRequests.clear(); // newest frame wins
         mRequests.push_back(std::move(request));
         mStatus = mProcessing ? "processing - newest frame queued" : "processing mask";
      }
      if (!mWorker.joinable())
         mWorker = std::thread(&RemoveBgNode::WorkerLoop, this);
      mWorkerWake.notify_one();
   }
}

// Recycles every GPU source copy with serial <= upTo, except `keep` (the one
// currently paired with the displayed mask).
void RemoveBgNode::ReleaseGpuFrames(uint64_t upTo, uint64_t keep)
{
   for (auto it = mGpuFrames.begin(); it != mGpuFrames.end();)
   {
      if (it->serial > upTo || it->serial == keep)
      {
         ++it;
         continue;
      }
      if (mFreeGpuFrames.size() < 4)
         mFreeGpuFrames.push_back(*it);
      else
         GLUtil::DestroyFbo(it->fbo);
      it = mGpuFrames.erase(it);
   }
}

void RemoveBgNode::WorkerLoop()
{
   for (;;)
   {
      std::vector<unsigned char> pixels;
      int w = 0, h = 0, requestedMode = 0, requestedBackend = 0;
      uint64_t serial = 0;
      {
         std::unique_lock<std::mutex> lock(mWorkerMutex);
         mWorkerWake.wait(lock, [this] { return mWorkerStop || !mRequests.empty(); });
         if (mWorkerStop)
            return;
         FrameRequest request = std::move(mRequests.front());
         mRequests.pop_front();
         pixels = std::move(request.pixels);
         w = request.width; h = request.height;
         requestedMode = request.mode; requestedBackend = request.backend;
         serial = request.serial;
         mProcessing = true;
      }

      std::vector<unsigned char> mask;
      std::string error;
      const auto started = std::chrono::steady_clock::now();
      const Platform::MattingMode mattingMode = requestedMode == 1
         ? Platform::MattingMode::Person : Platform::MattingMode::Subject;
      Platform::MattingBackend mattingBackend = Platform::MattingBackend::Auto;
      if (requestedBackend == 1) mattingBackend = Platform::MattingBackend::DirectML;
      else if (requestedBackend == 2) mattingBackend = Platform::MattingBackend::Cpu;
      std::string usedBackend;
      const bool ok = Platform::SubjectMask(pixels, w, h, mattingMode, mattingBackend,
                                            mask, error, &usedBackend);
      const double elapsedMs = std::chrono::duration<double, std::milli>(
         std::chrono::steady_clock::now() - started).count();

      {
         std::lock_guard<std::mutex> lock(mWorkerMutex);
         if (ok)
         {
            mCompletedMask = std::move(mask);
            mCompletedWidth = w;
            mCompletedHeight = h;
            mCompletedSerial = serial;
            char status[96];
            std::snprintf(status, sizeof(status), "mask ready - %s - %.0f ms",
                          usedBackend.empty() ? "on-device" : usedBackend.c_str(), elapsedMs);
            mCompletedStatus = status;
         }
         else
         {
            mCompletedMask.clear();
            mCompletedWidth = 0;
            mCompletedHeight = 0;
            mCompletedSerial = serial;
            mCompletedStatus = error;
         }
         mProcessing = false;
      }
   }
}

void RemoveBgNode::ConsumeCompletedMask()
{
   std::vector<unsigned char> mask;
   int w = 0, h = 0;
   uint64_t serial = 0;
   std::string status;
   {
      std::lock_guard<std::mutex> lock(mWorkerMutex);
      if (mCompletedSerial == 0 || mCompletedSerial == mUploadedSerial)
         return;
      mUploadedSerial = mCompletedSerial;
      serial = mCompletedSerial;
      mask.swap(mCompletedMask);
      w = mCompletedWidth;
      h = mCompletedHeight;
      status = mCompletedStatus;
      if (mProcessing || !mRequests.empty())
         status += " - updating";
   }

   mStatus = status;
   if (mask.empty() || w <= 0 || h <= 0)
   {
      // Failed pass: drop its source copy (and anything older), but keep the
      // frame the visible mask is still paired with.
      ReleaseGpuFrames(serial, mPairedSerial);
      return;
   }

   if (mMaskTex == 0)
      glGenTextures(1, &mMaskTex);
   glBindTexture(GL_TEXTURE_2D, mMaskTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, mask.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   // Pair the mask with the full-resolution GPU copy of the exact frame it
   // was computed from; everything older than it can be recycled.
   ReleaseGpuFrames(serial, serial);
   mPairedSourceTex = 0;
   mPairedSerial = 0;
   for (const GpuFrame& f : mGpuFrames)
   {
      if (f.serial == serial)
      {
         mPairedSourceTex = GLUtil::FboTexture(f.fbo);
         mPairedSerial = serial;
         break;
      }
   }
}

void RemoveBgNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0)
   {
      GLUtil::DestroyFbo(mOut);
      return;
   }
   if (!EnsureShader())
      return;

   const int w = std::max(1, mInput.Width());
   const int h = std::max(1, mInput.Height());
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   ConsumeCompletedMask();

   autoRefresh = true;
   mNeedsMask = false;
   QueueMask(srcTex, w, h);
   const unsigned int pairedSrc = (mMaskTex != 0 && mPairedSourceTex != 0) ? mPairedSourceTex : srcTex;

   GLUtil::RunShaderPass(mOut, mProgram, [this, pairedSrc, w, h]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, pairedSrc);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mMaskTex != 0 ? mMaskTex : pairedSrc);
      glUniform1i(glGetUniformLocation(mProgram, "uMask"), 1);
      glUniform1i(glGetUniformLocation(mProgram, "uHasMask"), mMaskTex != 0 ? 1 : 0);
      glUniform2f(glGetUniformLocation(mProgram, "uTexel"), 1.0f / w, 1.0f / h);
      glUniform1i(glGetUniformLocation(mProgram, "uOutputMode"), outputMode);
      glUniform1f(glGetUniformLocation(mProgram, "uFeather"), feather);
      glUniform1f(glGetUniformLocation(mProgram, "uThreshold"), threshold);
      glUniform1f(glGetUniformLocation(mProgram, "uContrast"), contrast);
      glUniform3f(glGetUniformLocation(mProgram, "uBgColor"), bgColor[0], bgColor[1], bgColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uBgOpacity"), bgOpacity);
   });
}
