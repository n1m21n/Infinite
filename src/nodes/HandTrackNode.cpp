#include "HandTrackNode.h"

#include "gl3.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#include "GLUtil.h"
#include "core/Extensions.h"
#include "core/tracking/HandTracker.h"

namespace
{
   const char* kFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";

   const char* kLabels[] = {
      "present", "palm x", "palm y", "index x", "index y", "thumb x", "thumb y",
      "pinch", "open", "roll", "size"
   };

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

HandTrackNode::HandTrackNode()
{
   for (int i = 0; i < kOutputCount; i++)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
   }
}

HandTrackNode::~HandTrackNode()
{
   {
      std::lock_guard<std::mutex> lk(mMu);
      mStop = true;
   }
   mCv.notify_all();
   if (mThread.joinable()) mThread.join();
   if (mTex) glDeleteTextures(1, &mTex);
   if (mFbo) glDeleteFramebuffers(1, &mFbo);
   if (mProgram) glDeleteProgram(mProgram);
}

const char* HandTrackNode::OutputLabel(int index) const
{
   return (index < 0 || index >= kOutputCount) ? "out" : kLabels[index];
}

IModulator* HandTrackNode::ModulatorOutput(int index)
{
   return (index < 0 || index >= kOutputCount) ? nullptr : &mTaps[index];
}

float HandTrackNode::Value(int index) const
{
   if (index < 0 || index >= kOutputCount) return 0.f;
   std::lock_guard<std::mutex> lk(mOutMu);
   const bool live = mFound || (NowSec() - mFoundAt) * 1000.0 < std::max(0.f, holdMs);
   if (index == kPresent) return mFound ? 1.f : 0.f;
   return live ? Clamp01(mVals[index]) : 0.f;
}

std::string HandTrackNode::Status() const
{
   std::lock_guard<std::mutex> lk(mStatusMu);
   return mStatus;
}

void HandTrackNode::EnsureWorker()
{
   if (!mThread.joinable())
      mThread = std::thread([this] { WorkerLoop(); });
}

void HandTrackNode::SubmitFrame(const unsigned char* rgb, int w, int h)
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

void HandTrackNode::WaitIdle()
{
   std::unique_lock<std::mutex> lk(mMu);
   mIdleCv.wait(lk, [this] { return !mHavePending && !mBusy; });
}

void HandTrackNode::Publish(const float* vals, bool found, double t)
{
   std::lock_guard<std::mutex> lk(mOutMu);
   if (vals) for (int i = 0; i < kOutputCount; i++) mVals[i] = vals[i];
   mFound = found;
   if (found) mFoundAt = t;
}

void HandTrackNode::WorkerLoop()
{
   Tracking::OrtRuntime rt;
   Tracking::HandTracker ht;
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
         if (mStop) return;
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
            Publish(nullptr, false, 0);
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
            if (rt.Load(lib, err) && ht.Open(rt, dir, true, err))
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
         Tracking::HandResult r;
         std::string err;
         if (!ht.Process(f.rgb.data(), f.w, f.h, r, err))
         {
            setStatus("tracking error: " + err);
         }
         else
         {
            mLastMs = (float)((NowSec() - t0) * 1000.0);
            if (!r.found)
            {
               Publish(nullptr, false, f.t);
            }
            else
            {
               float v[kOutputCount] = {};
               const float W = (float)f.w, H = (float)f.h;
               auto X = [&](int i) { const float x = r.lm[i][0] / W; return mirror ? 1.f - x : x; };
               auto Y = [&](int i) { return 1.f - r.lm[i][1] / H; };
               const float palmX = (X(0) + X(5) + X(9) + X(13) + X(17)) / 5.f;
               const float palmY = (Y(0) + Y(5) + Y(9) + Y(13) + Y(17)) / 5.f;
               auto dist = [&](int a, int b)
               {
                  return std::hypot(r.lm[a][0] - r.lm[b][0], r.lm[a][1] - r.lm[b][1]);
               };
               const float s = std::max(1e-3f, dist(0, 9));  // wrist to middle knuckle
               float tips = 0;
               for (int i : {8, 12, 16, 20}) tips += dist(0, i);
               tips /= 4.f * s;
               const float vx = r.lm[9][0] - r.lm[0][0], vy = r.lm[9][1] - r.lm[0][1];
               float roll = std::atan2(vx, -vy);          // 0 = fingers up, + = clockwise
               if (mirror) roll = -roll;
               v[kPalmX] = palmX; v[kPalmY] = palmY;
               v[kIndexX] = X(8); v[kIndexY] = Y(8);
               v[kThumbX] = X(4); v[kThumbY] = Y(4);
               v[kPinch] = 1.f - Clamp01((dist(4, 8) / s - 0.15f) / 0.85f);
               v[kOpen] = Clamp01((tips - 1.0f) / 1.0f);
               v[kRoll] = Clamp01(0.5f + roll / 3.14159265f);
               v[kSize] = Clamp01(s / (0.4f * H));
               // speed-adaptive smoothing; 0 = raw
               const float sm = Clamp01(smoothing);
               if (sm > 0.f)
               {
                  const float minCut = 8.f * std::pow(0.5f / 8.f, sm);
                  for (int i = kPalmX; i < kOutputCount; i++)
                     v[i] = EuroStep(v[i], mEuro[i].x, mEuro[i].dx, mEuro[i].t, f.t, minCut, 1.5f);
               }
               else
               {
                  for (auto& e : mEuro) e.t = -1;
               }
               Publish(v, true, f.t);
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

void HandTrackNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId) return;
   mLastCookFrame = frameId;
   if (!mInput.IsConnected()) return;
   mInput.Pull(frameId);

   const unsigned int src = mInput.Texture();
   if (src == 0) return;

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
