#include "RemoveBgNode.h"

#include "gl3.h"
#include <algorithm>
#include <mutex>

#include "Platform.h"
#include "Transport.h"

namespace
{
   // The macOS Vision request types (subject lifting vs. person segmentation)
   // don't exist on Windows, where a single salient-object model (u2netp) runs
   // through ONNX Runtime + DirectML and the mode is ignored entirely - so
   // showing the macOS-only labels there was misleading. Each platform gets the
   // options it can actually honour.
#if defined(_WIN32)
   const std::vector<std::string> kModeNames = { "Salient subject (GPU)" };
#else
   const std::vector<std::string> kModeNames = { "Subject (macOS 14+)", "Person (macOS 12+)" };
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
const std::vector<std::string>& RemoveBgNode::OutputModeNames() { return kOutputModeNames; }

RemoveBgNode::~RemoveBgNode()
{
   if (mWorkerStarted)
   {
      {
         std::lock_guard<std::mutex> lock(mRequestMutex);
         mStopWorker = true;
      }
      mRequestCv.notify_one();
      mWorkerThread.join();
   }

   GLUtil::DestroyFbo(mOut);
   if (mMaskTex != 0)
      glDeleteTextures(1, &mMaskTex);
   if (mPairedSrcTex != 0)
      glDeleteTextures(1, &mPairedSrcTex);
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

void RemoveBgNode::EnsureWorkerStarted()
{
   if (mWorkerStarted)
      return;
   mWorkerStarted = true;
   mWorkerThread = std::thread(&RemoveBgNode::WorkerThreadMain, this);
}

// Runs entirely on the worker thread. Reads only its own request copy off
// the stack and never touches a GL object - inference happens on CPU
// pixels in, CPU pixels out.
void RemoveBgNode::WorkerThreadMain()
{
   for (;;)
   {
      FrameRequest req;
      {
         std::unique_lock<std::mutex> lock(mRequestMutex);
         mRequestCv.wait(lock, [this] { return mRequestPending || mStopWorker; });
         if (mStopWorker && !mRequestPending)
            return;
         req = std::move(mPendingRequest);
         mRequestPending = false;
      }

      FrameResult result;
      result.width = req.width;
      result.height = req.height;
      result.serial = req.serial;
      result.pixels = req.pixels; // paired with the mask below, regardless of outcome

      result.ok = Platform::SubjectMask(req.pixels, req.width, req.height, req.mode, result.mask, result.error);

      {
         std::lock_guard<std::mutex> lock(mResultMutex);
         mPendingResult = std::move(result);
      }
      mResultReady.store(true, std::memory_order_release);
   }
}

void RemoveBgNode::SubmitMaskRequest(unsigned int srcTex, int w, int h)
{
   EnsureWorkerStarted();

   // Read the source back off the GPU so the worker can see it - this part
   // must stay on the render thread, since only it may touch GL objects.
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

   FrameRequest req;
   req.pixels = std::move(pixels);
   req.width = w;
   req.height = h;
   req.mode = (mode == 1) ? Platform::MattingMode::Person : Platform::MattingMode::Subject;
   req.serial = mNextSerial++;

   {
      std::lock_guard<std::mutex> lock(mRequestMutex);
      mPendingRequest = std::move(req); // replaces whatever was queued but not yet picked up
      mRequestPending = true;
   }
   mRequestCv.notify_one();

   mMaskInFlight = true;
   mStatus = "computing...";
}

// Main thread only, called once per CookIfNeeded: cheap (try_lock), picks up
// a finished result without ever blocking on the worker thread.
void RemoveBgNode::PollMaskResult()
{
   if (!mResultReady.load(std::memory_order_acquire))
      return;

   std::unique_lock<std::mutex> lock(mResultMutex, std::try_to_lock);
   if (!lock.owns_lock())
      return; // worker thread mid-write to mPendingResult; try again next frame

   FrameResult result = std::move(mPendingResult);
   mResultReady.store(false, std::memory_order_relaxed);
   lock.unlock();

   if (result.serial <= mLastAppliedSerial)
      return; // stale/duplicate; a newer result already applied
   mLastAppliedSerial = result.serial;
   mMaskInFlight = false;

   if (!result.ok)
   {
      mStatus = result.error;
      return;
   }

   if (mMaskTex == 0)
      glGenTextures(1, &mMaskTex);
   glBindTexture(GL_TEXTURE_2D, mMaskTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, result.width, result.height, 0, GL_RED, GL_UNSIGNED_BYTE, result.mask.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   // The frame the mask was computed from, uploaded the same raw way as the
   // mask itself so the two stay in the same UV convention - composited
   // together below instead of the (possibly newer) live source frame.
   if (mPairedSrcTex == 0)
      glGenTextures(1, &mPairedSrcTex);
   glBindTexture(GL_TEXTURE_2D, mPairedSrcTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, result.width, result.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, result.pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mStatus = "mask ready";
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

   PollMaskResult();

   // Auto-refresh is rate-limited by the render frame counter, not the musical
   // transport: segmentation is far too expensive to run every frame, and for
   // video the natural cadence is "every N frames" (tied to fps), not "every N
   // beats" (tied to BPM). With the worker in place, a request that arrives
   // while nothing is in flight is honoured immediately.
   if (autoRefresh)
   {
      const int interval = std::max(1, (int)(refreshFrames + 0.5f));
      if (frameId < mLastMaskFrame || frameId - mLastMaskFrame >= interval)
      {
         mNeedsMask = true;
         mLastMaskFrame = frameId;
      }
   }

   if (mNeedsMask && !mMaskInFlight)
   {
      mNeedsMask = false;
      SubmitMaskRequest(srcTex, w, h);
   }

   // Once a mask exists, always composite it with the exact frame it was
   // computed from (mPairedSrcTex), never the live srcTex - otherwise a
   // moving subject would tear against a mask that lags behind it.
   unsigned int colorTex = (mMaskTex != 0 && mPairedSrcTex != 0) ? mPairedSrcTex : srcTex;

   GLUtil::RunShaderPass(mOut, mProgram, [this, colorTex, srcTex, w, h]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, colorTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mMaskTex != 0 ? mMaskTex : srcTex);
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
