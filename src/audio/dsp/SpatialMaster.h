#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Master-section DSP for the Spatial Mixer (docs/plans/spatial, Block A):
// room (early reflections + diffuse tail), bass mono, linked true-peak limiter,
// and a BS.1770 loudness meter. Audio-thread safe after Prepare(): fixed buffers,
// no allocation, no locks. Written from the public references (Schroeder/Moorer
// reverb structure, ITU-R BS.1770-4 K-weighting, textbook look-ahead limiting).
namespace SpatialMaster
{
   // ---- Room ---------------------------------------------------------------
   // Mono send in, stereo out. Six early-reflection taps alternate ears (a
   // fixed asymmetric pattern, scaled by size), feeding four damped combs per
   // ear with different lengths for L and R (decorrelated) and two allpasses.
   class Room
   {
   public:
      void Prepare(double sr)
      {
         mSr = sr;
         const int erMax = (int)(0.08 * sr) + 8;
         mEr.assign((size_t)erMax, 0.0f);
         static const int kComb[2][4] = { { 1557, 1617, 1491, 1422 }, { 1580, 1640, 1514, 1445 } };
         static const int kAp[2][2] = { { 556, 441 }, { 579, 464 } };
         for (int e = 0; e < 2; e++)
         {
            for (int c = 0; c < 4; c++)
            {
               mComb[e][c].assign((size_t)std::lround(kComb[e][c] * sr / 44100.0), 0.0f);
               mCombPos[e][c] = 0;
               mCombLp[e][c] = 0.0f;
            }
            for (int a = 0; a < 2; a++)
            {
               mAp[e][a].assign((size_t)std::lround(kAp[e][a] * sr / 44100.0), 0.0f);
               mApPos[e][a] = 0;
            }
         }
         mErPos = 0;
         Reset();
      }

      void Reset()
      {
         std::fill(mEr.begin(), mEr.end(), 0.0f);
         for (int e = 0; e < 2; e++)
         {
            for (int c = 0; c < 4; c++)
            {
               std::fill(mComb[e][c].begin(), mComb[e][c].end(), 0.0f);
               mCombLp[e][c] = 0.0f;
            }
            for (int a = 0; a < 2; a++)
               std::fill(mAp[e][a].begin(), mAp[e][a].end(), 0.0f);
         }
      }

      // room: 0..1 (size and level together); once per block.
      void SetRoom(float room)
      {
         room = std::clamp(room, 0.0f, 1.0f);
         mSize = 0.35f + 0.65f * room;              // scales the early taps
         mFeedback = 0.70f + 0.20f * room;          // ~0.4 s .. ~1.4 s tail
         mWet = 0.5f * room;                        // level of the whole room
      }

      inline void Process(float send, float& outL, float& outR)
      {
         if (mWet <= 0.0f)
            return;
         const int erLen = (int)mEr.size();
         mEr[mErPos] = send;
         // Early reflections (ms): left/right alternating, decaying.
         static const float kTapMs[6] = { 7.0f, 11.0f, 17.0f, 23.0f, 31.0f, 41.0f };
         static const float kTapG[6] = { 0.55f, 0.50f, 0.40f, 0.34f, 0.26f, 0.20f };
         float er[2] = { 0.0f, 0.0f };
         for (int t = 0; t < 6; t++)
         {
            const int d = std::min((int)(kTapMs[t] * 0.001 * mSr * mSize), erLen - 1);
            er[t & 1] += kTapG[t] * mEr[(mErPos - d + erLen) % erLen];
         }
         mErPos = (mErPos + 1) % erLen;

         const float damp = 0.35f;
         float tail[2];
         for (int e = 0; e < 2; e++)
         {
            float acc = 0.0f;
            for (int c = 0; c < 4; c++)
            {
               std::vector<float>& b = mComb[e][c];
               int& p = mCombPos[e][c];
               const float y = b[p];
               mCombLp[e][c] += (1.0f - damp) * (y - mCombLp[e][c]);
               b[p] = send * 0.25f + mFeedback * mCombLp[e][c];
               p = (p + 1) % (int)b.size();
               acc += y;
            }
            for (int a = 0; a < 2; a++)
            {
               std::vector<float>& b = mAp[e][a];
               int& p = mApPos[e][a];
               const float z = b[p];
               const float y = -acc + z;
               b[p] = acc + 0.5f * z;
               p = (p + 1) % (int)b.size();
               acc = y;
            }
            tail[e] = acc;
         }
         outL += mWet * (er[0] + 0.6f * tail[0]);
         outR += mWet * (er[1] + 0.6f * tail[1]);
      }

   private:
      double mSr = 48000.0;
      float mSize = 0.5f, mFeedback = 0.8f, mWet = 0.0f;
      std::vector<float> mEr;
      int mErPos = 0;
      std::vector<float> mComb[2][4], mAp[2][2];
      int mCombPos[2][4] = {}, mApPos[2][2] = {};
      float mCombLp[2][4] = {};
   };

   // ---- Bass mono ----------------------------------------------------------
   // Below `hz` the two ears are summed to one signal (steep 12 dB/oct split).
   class BassMono
   {
   public:
      void Prepare(double sr) { mSr = sr; Reset(); }
      void Reset() { for (int c = 0; c < 2; c++) mLp[c][0] = mLp[c][1] = 0.0f; }
      void SetHz(float hz)
      {
         mOn = hz >= 20.0f;
         if (mOn)
            mA = 1.0f - std::exp(-6.283185307f * std::min(hz, 500.0f) / (float)mSr);
      }
      inline void Process(float& l, float& r)
      {
         if (!mOn)
            return;
         float lo[2];
         const float in[2] = { l, r };
         for (int c = 0; c < 2; c++)
         {
            mLp[c][0] += mA * (in[c] - mLp[c][0]);
            mLp[c][1] += mA * (mLp[c][0] - mLp[c][1]);
            lo[c] = mLp[c][1];
         }
         const float mid = 0.5f * (lo[0] + lo[1]);
         l += mid - lo[0];
         r += mid - lo[1];
      }
   private:
      double mSr = 48000.0;
      bool mOn = false;
      float mA = 0.01f;
      float mLp[2][2] = {};
   };

   // ---- True-peak look-ahead limiter ---------------------------------------
   // One gain for both ears so direction is kept. The true peak between samples
   // is estimated with a 4-point cubic midpoint. Latency: kLook - 1 samples.
   class Limiter
   {
   public:
      static constexpr int kMaxLook = 128;
      static constexpr int kRing = 256;

      void Prepare(double sr)
      {
         mLook = std::clamp((int)std::lround(0.0015 * sr), 8, kMaxLook);
         mRel = 1.0f - std::exp(-1.0f / (0.08f * (float)sr));
         Reset();
      }
      void Reset()
      {
         for (int i = 0; i < kRing; i++)
            mDel[0][i] = mDel[1][i] = 0.0f, mGt[i] = 1.0f, mEnv[i] = 1.0f;
         mPos = 0;
         mEnvCur = 1.0f;
         mEnvSum = (float)mLook;
         mX[0][0] = mX[0][1] = mX[0][2] = mX[1][0] = mX[1][1] = mX[1][2] = 0.0f;
      }
      void SetCeilingDb(float db) { mCeil = std::pow(10.0f, db / 20.0f); }
      int Latency() const { return mLook - 1; }
      float LastReductionDb() const { return mLastRedDb; }
      float BlockTruePeak() const { return mBlockTp; }
      void BeginBlock() { mBlockTp = 0.0f; mMinGain = 1.0f; }
      float BlockMinGainDb() const { return 20.0f * std::log10(std::max(mMinGain, 1e-6f)); }

      // enabled=false keeps the same latency, gain 1.
      inline void Process(float& l, float& r, bool enabled)
      {
         const unsigned m = kRing - 1;
         const float in[2] = { l, r };
         float peak = 0.0f;
         for (int c = 0; c < 2; c++)
         {
            // x0 = oldest of the 4 (n-3), x1 = n-2, x2 = n-1, x3 = n (cubic midpoint between x1,x2)
            const float x0 = mX[c][0], x1 = mX[c][1], x2 = mX[c][2], x3 = in[c];
            const float mid = 0.5625f * (x1 + x2) - 0.0625f * (x0 + x3);
            peak = std::max(peak, std::max(std::fabs(x3), std::fabs(mid)));
            mX[c][0] = x1;
            mX[c][1] = x2;
            mX[c][2] = x3;
         }
         mBlockTp = std::max(mBlockTp, peak);
         const float gt = (enabled && peak > mCeil) ? mCeil / peak : 1.0f;
         mDel[0][mPos & m] = l;
         mDel[1][mPos & m] = r;
         mGt[mPos & m] = gt;

         float wmin = 1.0f;
         for (int i = 0; i < mLook; i++)
            wmin = std::min(wmin, mGt[(mPos - i) & m]);
         if (wmin < mEnvCur)
            mEnvCur = wmin;
         else
            mEnvCur += mRel * (wmin - mEnvCur);
         // Running sum of the last mLook env values.
         const float old = mEnv[(mPos - mLook) & m];
         mEnv[mPos & m] = mEnvCur;
         mEnvSum += mEnvCur - old;
         const float g = mEnvSum / (float)mLook;

         const unsigned dp = (mPos - (unsigned)(mLook - 1)) & m;
         l = mDel[0][dp] * g;
         r = mDel[1][dp] * g;
         mMinGain = std::min(mMinGain, g);
         mPos++;
      }

   private:
      int mLook = 64;
      float mRel = 0.0003f, mCeil = 0.891f;
      float mDel[2][kRing] = {};
      float mGt[kRing] = {}, mEnv[kRing] = {};
      float mX[2][3] = {};
      float mEnvCur = 1.0f, mEnvSum = 64.0f, mBlockTp = 0.0f, mMinGain = 1.0f, mLastRedDb = 0.0f;
      unsigned mPos = 0;
   };

   // ---- BS.1770 loudness meter ---------------------------------------------
   // K-weighted mean square over 100 ms blocks: momentary (400 ms) and
   // short-term (3 s). Values are LUFS (-70 floor).
   class Loudness
   {
   public:
      void Prepare(double sr)
      {
         mBlock = (int)std::lround(0.1 * sr);
         {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(M_PI * f0 / sr), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.499666774155);
            const double a0 = 1.0 + K / Q + K * K;
            mS1[0] = (float)((Vh + Vb * K / Q + K * K) / a0);
            mS1[1] = (float)(2.0 * (K * K - Vh) / a0);
            mS1[2] = (float)((Vh - Vb * K / Q + K * K) / a0);
            mS1[3] = (float)(2.0 * (K * K - 1.0) / a0);
            mS1[4] = (float)((1.0 - K / Q + K * K) / a0);
         }
         {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(M_PI * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            mS2[0] = 1.0f;
            mS2[1] = -2.0f;
            mS2[2] = 1.0f;
            mS2[3] = (float)(2.0 * (K * K - 1.0) / a0);
            mS2[4] = (float)((1.0 - K / Q + K * K) / a0);
         }
         Reset();
      }
      void Reset()
      {
         for (int c = 0; c < 2; c++)
            for (int i = 0; i < 4; i++)
               mZ1[c][i] = mZ2[c][i] = 0.0f;
         for (int i = 0; i < 30; i++)
            mMs[i] = 0.0f;
         mFill = 0;
         mAcc = 0.0;
         mCount = 0;
         mMomentary = mShort = -70.0f;
      }
      inline void Process(float l, float r)
      {
         const float in[2] = { l, r };
         double ms = 0.0;
         for (int c = 0; c < 2; c++)
         {
            // Direct form II transposed, two biquads.
            float x = in[c];
            float y = mS1[0] * x + mZ1[c][0];
            mZ1[c][0] = mS1[1] * x - mS1[3] * y + mZ1[c][1];
            mZ1[c][1] = mS1[2] * x - mS1[4] * y;
            x = y;
            y = mS2[0] * x + mZ2[c][0];
            mZ2[c][0] = mS2[1] * x - mS2[3] * y + mZ2[c][1];
            mZ2[c][1] = mS2[2] * x - mS2[4] * y;
            ms += (double)y * (double)y;
         }
         mAcc += ms;
         if (++mCount >= mBlock)
         {
            mMs[mHead] = (float)(mAcc / (double)mCount);
            mHead = (mHead + 1) % 30;
            mFill = std::min(mFill + 1, 30);
            mAcc = 0.0;
            mCount = 0;
            auto lufs = [&](int n) {
               if (mFill < n)
                  n = mFill;
               if (n == 0)
                  return -70.0f;
               double s = 0.0;
               for (int i = 0; i < n; i++)
                  s += mMs[(mHead - 1 - i + 60) % 30];
               s /= n;
               return s <= 1e-10 ? -70.0f : std::max(-70.0f, (float)(-0.691 + 10.0 * std::log10(s)));
            };
            mMomentary = lufs(4);
            mShort = lufs(30);
         }
      }
      float Momentary() const { return mMomentary; }
      float ShortTerm() const { return mShort; }
   private:
      int mBlock = 4800, mFill = 0, mHead = 0, mCount = 0;
      double mAcc = 0.0;
      float mS1[5] = {}, mS2[5] = {};
      float mZ1[2][4] = {}, mZ2[2][4] = {};
      float mMs[30] = {};
      float mMomentary = -70.0f, mShort = -70.0f;
   };
}
