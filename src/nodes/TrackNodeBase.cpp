#include "TrackNodeBase.h"

#include "gl3.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#include "GLUtil.h"
#include "core/Extensions.h"
#include "core/tracking/OrtRuntime.h"

namespace
{
   const char* kFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";

   double NowSec()
   {
      return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   }

   float Clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }

   // One-Euro filter (Casiez et al.): smooth when still, responsive when fast.
   float EuroStep(float x, float& prev, float& dprev, double& tprev, double t, float minCutoff, float beta)
   {
      if (tprev < 0) { prev = x; dprev = 0; tprev = t; return x; }
      const float dt = (float)std::max(1e-3, t - tprev);
      tprev = t;
      auto alpha = [dt](float cutoff)
      {
         const float tau = 1.f / (2.f * 3.14159265f * cutoff);
         return 1.f / (1.f + tau / dt);
      };
      const float dx = (x - prev) / dt;
      dprev += alpha(1.0f) * (dx - dprev);
      const float cutoff = minCutoff + beta * std::fabs(dprev);
      prev += alpha(cutoff) * (x - prev);
      return prev;
   }
}

TrackNodeBase::TrackNodeBase()
{
   for (int i = 0; i < kMaxOutputs; i++)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
   }
}

void TrackNodeBase::StopWorker()
{
   {
      std::lock_guard<std::mutex> lk(mMu);
      mStop = true;
   }
   mCv.notify_all();
   if (mThread.joinable()) mThread.join();
}

TrackNodeBase::~TrackNodeBase()
{
   StopWorker();
   if (mTex) glDeleteTextures(1, &mTex);
   if (mFbo) glDeleteFramebuffers(1, &mFbo);
   if (mProgram) glDeleteProgram(mProgram);
}

IModulator* TrackNodeBase::ModulatorOutput(int index)
{
   return (index < 0 || index >= std::min(kMaxOutputs, OutputCount())) ? nullptr : &mTaps[index];
}

float TrackNodeBase::Value(int index) const
{
   if (index < 0 || index >= std::min(kMaxOutputs, OutputCount())) return 0.f;
   std::lock_guard<std::mutex> lk(mOutMu);
   const bool live = mFound || (NowSec() - mFoundAt) * 1000.0 < std::max(0.f, holdMs);
   if (index == 0) return mFound ? 1.f : 0.f;
   return live ? Clamp01(mVals[index]) : 0.f;
}

std::string TrackNodeBase::Status() const
{
   std::lock_guard<std::mutex> lk(mStatusMu);
   return mStatus;
}

void TrackNodeBase::EnsureWorker()
{
   if (!mThread.joinable())
      mThread = std::thread([this] { WorkerLoop(); });
}

void TrackNodeBase::SubmitFrame(const unsigned char* rgb, int w, int h)
{
   if (w <= 0 || h <= 0) return;
   EnsureWorker();
   {
      std::lock_guard<std::mutex> lk(mMu);
      mPending.rgb.assign(rgb, rgb + (size_t)w * h * 3);
      mPending.w = w;
      mPending.h = h;
      mPending.t = NowSec();
      mHavePending = true;   // an older unconsumed frame is simply replaced
   }
   mCv.notify_one();
}

void TrackNodeBase::WaitIdle()
{
   std::unique_lock<std::mutex> lk(mMu);
   mIdleCv.wait(lk, [this] { return !mHavePending && !mBusy; });
}

void TrackNodeBase::Publish(const float* vals, bool found, double t, const std::vector<float>* overlay)
{
   std::lock_guard<std::mutex> lk(mOutMu);
   if (vals) for (int i = 0; i < kMaxOutputs; i++) mVals[i] = vals[i];
   mFound = found;
   if (found) mFoundAt = t;
   if (overlay) mOverlay = *overlay; else mOverlay.clear();
}

void TrackNodeBase::GetOverlay(std::vector<float>& xy) const
{
   std::lock_guard<std::mutex> lk(mOutMu);
   xy = mOverlay;
}

const std::vector<std::pair<int, int>>& TrackNodeBase::OverlayLines() const
{
   static const std::vector<std::pair<int, int>> none;
   return none;
}

void TrackNodeBase::WorkerLoop()
{
   Tracking::OrtRuntime rt;
   bool opened = false;
   auto setStatus = [this](const std::string& s)
   {
      std::lock_guard<std::mutex> lk(mStatusMu);
      mStatus = s;
   };

   for (;;)
   {
      Frame f;
      {
         std::unique_lock<std::mutex> lk(mMu);
         mCv.wait(lk, [this] { return mStop || mHavePending; });
         if (mStop) { lk.unlock(); CloseTracker(); return; }
         f = std::move(mPending);
         mHavePending = false;
         mBusy = true;
      }

      if (!opened)
      {
         const std::string dir = Extensions::PackDir("tracking");
         if (dir.empty())
         {
            mPackMissing = true;
            setStatus("Tracking pack not installed - Settings > Extensions");
            Publish(nullptr, false, 0, nullptr);
         }
         else
         {
            std::string err;
#if defined(__APPLE__)
            const std::string lib = dir + "/lib/libonnxruntime.dylib";
#elif defined(_WIN32)
            const std::string lib = dir + "/lib/onnxruntime.dll";
#else
            const std::string lib = dir + "/lib/libonnxruntime.so";
#endif
            if (rt.Load(lib, err) && OpenTracker(rt, dir, err))
            {
               opened = true;
               mPackMissing = false;
               setStatus("tracking");
            }
            else
            {
               mPackMissing = true;
               setStatus("Tracking pack could not load: " + err);
            }
         }
      }

      if (opened)
      {
         const double t0 = NowSec();
         float v[kMaxOutputs] = {};
         std::vector<float> overlay;
         bool found = false;
         std::string err;
         if (!Infer(f, v, overlay, found, err))
         {
            setStatus("tracking error: " + err);
         }
         else
         {
            mLastMs = (float)((NowSec() - t0) * 1000.0);
            if (!found)
            {
               Publish(nullptr, false, f.t, nullptr);
            }
            else
            {
               // speed-adaptive smoothing; 0 = raw
               const float sm = Clamp01(smoothing);
               const int n = std::min(kMaxOutputs, OutputCount());
               if (sm > 0.f)
               {
                  const float minCut = 8.f * std::pow(0.5f / 8.f, sm);
                  for (int i = 0; i < n; i++)
                     if (SmoothOutput(i))
                        v[i] = EuroStep(v[i], mEuro[i].x, mEuro[i].dx, mEuro[i].t, f.t, minCut, 1.5f);
               }
               else
               {
                  for (auto& e : mEuro) e.t = -1;
               }
               Publish(v, true, f.t, &overlay);
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "tracking, %.1f ms", mLastMs.load());
            setStatus(buf);
         }
      }

      {
         std::lock_guard<std::mutex> lk(mMu);
         mBusy = false;
      }
      mIdleCv.notify_all();
   }
}

void TrackNodeBase::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId) return;
   mLastCookFrame = frameId;
   if (!mInput.IsConnected())
   {
      Publish(nullptr, false, NowSec(), nullptr); // unplugged: report "not found", don't freeze the last pose
      return;
   }
   mInput.Pull(frameId);

   const unsigned int src = mInput.Texture();
   if (src == 0)
   {
      Publish(nullptr, false, NowSec(), nullptr);
      return;
   }

   // Skip while the worker is still busy: the newest frame wins, and reading
   // back a frame nobody will use only stalls the GPU.
   {
      std::lock_guard<std::mutex> lk(mMu);
      if (mBusy || mHavePending) return;
   }

   if (!mShaderTried)
   {
      mShaderTried = true;
      mProgram = GLUtil::CompileProgram(kFrag);
   }
   if (mProgram == 0) return;

   const int sw = std::max(1, mInput.Width()), sh = std::max(1, mInput.Height());
   const float k = 256.f / (float)std::max(sw, sh);
   const int w = std::max(8, (int)std::lround(sw * std::min(1.f, k)));
   const int h = std::max(8, (int)std::lround(sh * std::min(1.f, k)));

   if (mTexW != w || mTexH != h)
   {
      if (mTex) glDeleteTextures(1, &mTex);
      if (mFbo) glDeleteFramebuffers(1, &mFbo);
      glGenTextures(1, &mTex);
      glBindTexture(GL_TEXTURE_2D, mTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glGenFramebuffers(1, &mFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTex, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindTexture(GL_TEXTURE_2D, 0);
      mTexW = w;
      mTexH = h;
   }

   GLUtil::Fbo wrapper;
   wrapper.fbo = mFbo;
   wrapper.tex = mTex;
   wrapper.w = w;
   wrapper.h = h;
   GLUtil::RunShaderPass(wrapper, mProgram, [this, src]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, src);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
   });

   mGl.assign((size_t)w * h * 4, 0);
   GLint prev = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, mGl.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prev);

   // GL rows run bottom-up; the tracker wants top-down RGB.
   std::vector<unsigned char> rgb((size_t)w * h * 3);
   for (int y = 0; y < h; ++y)
   {
      const unsigned char* s = &mGl[(size_t)(h - 1 - y) * w * 4];
      unsigned char* d = &rgb[(size_t)y * w * 3];
      for (int x = 0; x < w; ++x) { d[x * 3] = s[x * 4]; d[x * 3 + 1] = s[x * 4 + 1]; d[x * 3 + 2] = s[x * 4 + 2]; }
   }
   SubmitFrame(rgb.data(), w, h);
}
