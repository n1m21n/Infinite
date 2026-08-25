#include "RemoveBgNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

#include "Platform.h"

namespace
{
#if defined(_WIN32)
   const std::vector<std::string> kModeNames = { "Subject - U2Net", "Person - U2Net Human" };
   const std::vector<std::string> kBackendNames = { "Auto GPU (DX12)", "DirectML only", "CPU" };
#else
   const std::vector<std::string> kModeNames = { "Subject (macOS 14+)", "Person (macOS 12+)" };
   const std::vector<std::string> kBackendNames = { "On-device" };
#endif
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
   if (mPairedSourceTex != 0)
      glDeleteTextures(1, &mPairedSourceTex);
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

void RemoveBgNode::QueueMask(unsigned int srcTex, int w, int h)
{
   {
      std::lock_guard<std::mutex> lock(mWorkerMutex);
      if (mRequests.size() >= (size_t)std::clamp(frameCache, 1, 16))
         return;
   }
   // Only the GL readback remains on the render thread. Model inference runs
   // on a latest-frame worker below, so a slow mask can never stall the UI,
   // projector or audio graph and queued video frames never build a backlog.
   std::vector<unsigned char> pixels((size_t)w * h * 4);
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   GLuint fbo = 0;
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glDeleteFramebuffers(1, &fbo);

   {
      std::lock_guard<std::mutex> lock(mWorkerMutex);
      FrameRequest request;
      request.pixels = std::move(pixels);
      request.width = w; request.height = h;
      request.mode = mode; request.backend = backend;
      request.serial = ++mNextSerial;
      const size_t limit = (size_t)std::clamp(frameCache, 1, 16);
      while (mRequests.size() >= limit)
         mRequests.pop_front();
      mRequests.push_back(std::move(request));
      mStatus = mProcessing ? "processing - frames cached" : "processing mask";
   }
   if (!mWorker.joinable())
      mWorker = std::thread(&RemoveBgNode::WorkerLoop, this);
   mWorkerWake.notify_one();
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
            mCompletedSource = pixels;
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
            mCompletedSource.clear();
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
   std::vector<unsigned char> source;
   int w = 0, h = 0;
   std::string status;
   {
      std::lock_guard<std::mutex> lock(mWorkerMutex);
      if (mCompletedSerial == 0 || mCompletedSerial == mUploadedSerial)
         return;
      mUploadedSerial = mCompletedSerial;
      mask = mCompletedMask;
      source = mCompletedSource;
      w = mCompletedWidth;
      h = mCompletedHeight;
      status = mCompletedStatus;
      if (mProcessing || !mRequests.empty())
         status += " - updating";
   }

   mStatus = status;
   if (mask.empty() || w <= 0 || h <= 0)
      return;

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

   if (mPairedSourceTex == 0)
      glGenTextures(1, &mPairedSourceTex);
   glBindTexture(GL_TEXTURE_2D, mPairedSourceTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, source.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

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
