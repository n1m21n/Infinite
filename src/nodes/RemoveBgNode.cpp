#include "RemoveBgNode.h"
#include "GLUtil.h"

#include "gl3.h"
#include <algorithm>
#include <cstring>
#include <mutex>

#include "Platform.h"

namespace
{
   const std::vector<std::string> kOutputModeNames = { "Cutout", "Mask only", "Background only" };

   // Small process-wide cache of recent masks keyed by exact RGBA pixels, size,
   // and detection mode. Every global Undo/Redo rebuilds the node graph from
   // scratch via ApplyPatchData(), so hitting this cache lets ReloadDerived()
   // re-upload the mask on the very first cook without re-running segmentation
   // or flickering unmasked frames.
   struct CachedMaskEntry
   {
      uint64_t hash = 0;
      int width = 0, height = 0;
      Platform::MattingMode mode = Platform::MattingMode::Subject;
      std::vector<unsigned char> mask;
   };
   constexpr size_t kMaxMaskCacheEntries = 8;
   std::mutex gMaskCacheMutex;
   std::vector<CachedMaskEntry> gMaskCache;

   uint64_t HashMaskInputs(const std::vector<unsigned char>& pixels, int w, int h, Platform::MattingMode mode)
   {
      uint64_t hash = 14695981039346656037ull;
      auto mix = [&](uint64_t v)
      {
         hash ^= v;
         hash *= 1099511628211ull;
      };
      mix((uint64_t)w);
      mix((uint64_t)h);
      mix((uint64_t)mode);
      const size_t n = pixels.size();
      const unsigned char* p = pixels.data();
      size_t i = 0;
      for (; i + 8 <= n; i += 8)
      {
         uint64_t chunk = 0;
         std::memcpy(&chunk, p + i, 8);
         mix(chunk);
      }
      for (; i < n; i++)
         mix(p[i]);
      return hash;
   }

   bool LookupCachedMask(uint64_t hash, int w, int h, Platform::MattingMode mode, std::vector<unsigned char>& outMask)
   {
      std::lock_guard<std::mutex> lock(gMaskCacheMutex);
      for (size_t i = gMaskCache.size(); i > 0; i--)
      {
         const CachedMaskEntry& e = gMaskCache[i - 1];
         if (e.hash == hash && e.width == w && e.height == h && e.mode == mode &&
             e.mask.size() == (size_t)w * (size_t)h)
         {
            outMask = e.mask;
            return true;
         }
      }
      return false;
   }

   void StoreCachedMask(uint64_t hash, int w, int h, Platform::MattingMode mode, const std::vector<unsigned char>& mask)
   {
      if (w <= 0 || h <= 0 || mask.size() != (size_t)w * (size_t)h)
         return;
      std::lock_guard<std::mutex> lock(gMaskCacheMutex);
      for (size_t i = 0; i < gMaskCache.size(); i++)
      {
         if (gMaskCache[i].hash == hash && gMaskCache[i].width == w &&
             gMaskCache[i].height == h && gMaskCache[i].mode == mode)
         {
            gMaskCache[i].mask = mask;
            return;
         }
      }
      if (gMaskCache.size() >= kMaxMaskCacheEntries)
         gMaskCache.erase(gMaskCache.begin());
      gMaskCache.push_back({ hash, w, h, mode, mask });
   }

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

const std::vector<std::string>& RemoveBgNode::ModeNames() { return Platform::MattingModeNames(); }
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
      result.mode = req.mode;
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

void RemoveBgNode::UploadMaskAndPaired(const unsigned char* mask, const unsigned char* pixels, int w, int h)
{
   if (mMaskTex == 0)
      glGenTextures(1, &mMaskTex);
   glBindTexture(GL_TEXTURE_2D, mMaskTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, mask);
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
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mStatus = "mask ready";
}

void RemoveBgNode::SubmitMaskRequest(unsigned int srcTex, int w, int h)
{
   // Read the source back off the GPU so the worker can see it - this part
   // must stay on the render thread, since only it may touch GL objects.
   std::vector<unsigned char> pixels((size_t)w * h * 4);
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   GLint internalFmt = GL_RGBA8;
   glBindTexture(GL_TEXTURE_2D, srcTex);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFmt);
   glBindTexture(GL_TEXTURE_2D, 0);
   GLuint fbo = 0;
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   if (internalFmt == GL_RGBA16F || internalFmt == GL_RGBA32F)
   {
      // Float working FBOs hold linear light in 2D View; encode to sRGB so Vision sees true display contrast.
      std::vector<float> fp((size_t)w * h * 4);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, fp.data());
      auto toSrgb8 = [](float v) -> unsigned char {
         v = std::clamp(v, 0.0f, 1.0f);
         const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
         return (unsigned char)std::clamp((int)std::lround(s * 255.0f), 0, 255);
      };
      const size_t npix = (size_t)w * h;
      for (size_t i = 0; i < npix; i++)
      {
         pixels[i * 4 + 0] = toSrgb8(fp[i * 4 + 0]);
         pixels[i * 4 + 1] = toSrgb8(fp[i * 4 + 1]);
         pixels[i * 4 + 2] = toSrgb8(fp[i * 4 + 2]);
         pixels[i * 4 + 3] = (unsigned char)std::clamp((int)std::lround(std::clamp(fp[i * 4 + 3], 0.0f, 1.0f) * 255.0f), 0, 255);
      }
   }
   else
   {
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   }
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glDeleteFramebuffers(1, &fbo);

   const Platform::MattingMode reqMode = (mode == 1) ? Platform::MattingMode::Person : Platform::MattingMode::Subject;
   const uint64_t cacheHash = HashMaskInputs(pixels, w, h, reqMode);
   std::vector<unsigned char> cachedMask;
   if (LookupCachedMask(cacheHash, w, h, reqMode, cachedMask))
   {
      UploadMaskAndPaired(cachedMask.data(), pixels.data(), w, h);
      mMaskInFlight = false;
      return;
   }

   EnsureWorkerStarted();

   FrameRequest req;
   req.pixels = std::move(pixels);
   req.width = w;
   req.height = h;
   req.mode = reqMode;
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

   StoreCachedMask(HashMaskInputs(result.pixels, result.width, result.height, result.mode),
                   result.width, result.height, result.mode, result.mask);
   UploadMaskAndPaired(result.mask.data(), result.pixels.data(), result.width, result.height);
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
      glUniform1i(GLUtil::UniformLoc(mProgram, "uSrc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mMaskTex != 0 ? mMaskTex : srcTex);
      glUniform1i(GLUtil::UniformLoc(mProgram, "uMask"), 1);
      glUniform1i(GLUtil::UniformLoc(mProgram, "uHasMask"), mMaskTex != 0 ? 1 : 0);
      glUniform2f(GLUtil::UniformLoc(mProgram, "uTexel"), 1.0f / w, 1.0f / h);
      glUniform1i(GLUtil::UniformLoc(mProgram, "uOutputMode"), outputMode);
      glUniform1f(GLUtil::UniformLoc(mProgram, "uFeather"), feather);
      glUniform1f(GLUtil::UniformLoc(mProgram, "uThreshold"), threshold);
      glUniform1f(GLUtil::UniformLoc(mProgram, "uContrast"), contrast);
      glUniform3f(GLUtil::UniformLoc(mProgram, "uBgColor"), bgColor[0], bgColor[1], bgColor[2]);
      glUniform1f(GLUtil::UniformLoc(mProgram, "uBgOpacity"), bgOpacity);
   });
}
