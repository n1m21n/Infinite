#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "audio/DspMath.h"

// Turbo 0.50: the Super Mixer's master bus, run on the summed stereo signal.
//
//   sum -> EQ -> glue comp -> width -> saturation -> fader -> balance
//       -> lookahead limiter -> mute -> meters
//
// Turbo 0.50: the limiter is a true-peak lookahead design (see LimiterChunk).
// It adds latency (its lookahead) only while it is switched in, reported to
// the engine's delay compensation through AudioSuperMixerNode::LatencySamples.
//
// Every FX stage has its own on/off switch with a short crossfade, and all
// default to off, so a patch from before 0.50 is processed exactly as before
// (fader then nothing). Header-only and allocation-free: every buffer is a
// fixed-size member, params arrive through relaxed atomics (main thread
// writes, audio thread reads once per block), meters go back the same way.
// Transcendentals (exp, pow, log) run per block, per 64-frame chunk or per
// 16-frame control step, never per sample.
namespace MasterDsp
{
   enum Param : int
   {
      kFaderGain = 0, // linear, computed by the node exactly as before 0.50
      kBalance,       // -1..1
      kMute,          // 0/1
      kEqOn, kEqLowHz, kEqLowDb, kEqMidHz, kEqMidDb, kEqMidQ, kEqHighHz, kEqHighDb,
      kCompOn, kCompThreshDb, kCompRatio, kCompAttackMs, kCompReleaseMs, kCompMakeupDb,
      kWidthOn, kWidth,
      kSatOn, kSatDriveDb, kSatMix, kSatOutDb,
      kLimOn, kLimCeilingDb, kLimReleaseMs,
      // Turbo 0.50: limiter v2, appended (old indices unchanged).
      kLimLookaheadMs, kLimLink, kLimTruePeak, kLimAutoRelease, kLimStyle,
      kParamCount
   };

   class MasterChain
   {
   public:
      static constexpr int kChunk = 64;          // smoothing / coefficient update period
      static constexpr int kCompCtl = 16;        // compressor gain-computer period
      static constexpr int kRing = 2048;         // limiter delay + window capacity (power of 2)
      static constexpr uint32_t kMask = kRing - 1;
      static constexpr float kKneeDb = 6.0f;

      // Limiter v2 geometry. The true-peak interpolator is a 4x polyphase
      // FIR, 48 taps per phase (reads at most 0.05 dB low up to 0.9 x Nyquist), so a segment's inter-sample peaks are known
      // kTpHalf samples after its left sample arrives; one more sample
      // spreads each segment peak onto both of its neighbours.
      static constexpr int kTpTaps = 48;
      static constexpr int kTpHalf = kTpTaps / 2;
      static constexpr int kSideDelay = kTpHalf + 1;
      static constexpr int kMaxLatency = kRing - 64;
      // Shortest attack ramp, in samples. A faster gain ramp spreads the
      // gain modulation itself into new inter-sample peaks (measured: up to
      // +0.3 dBTP over the ceiling on +18 dB drum hits with a 3-sample ramp,
      // none from about 18 samples up), so the lookahead never goes below
      // kSideDelay + kMinHold - 1 samples (0.92 ms at 48 kHz, 0.46 at 96).
      static constexpr int kMinHold = 20;
      static constexpr float kLimMinMs = 0.5f, kLimMaxMs = 5.0f;
      static constexpr int kLimStyleCount = 3;

      // Lookahead in samples: the delay the limiter adds, and the latency it
      // reports. Shared by the node (main thread, PDC) and the audio thread
      // so both always agree.
      static int LimiterLatencySamples(float ms, double sampleRate)
      {
         const double m = std::clamp((double)ms, (double)kLimMinMs, (double)kLimMaxMs);
         return std::clamp((int)std::lround(m * 0.001 * sampleRate), kSideDelay + kMinHold - 1, kMaxLatency);
      }

      MasterChain()
      {
         static const float kDefaults[kParamCount] = {
            1.0f, 0.0f, 0.0f,
            0.0f, 100.0f, 0.0f, 1000.0f, 0.0f, 0.7f, 10000.0f, 0.0f,
            0.0f, -12.0f, 2.0f, 10.0f, 200.0f, 0.0f,
            0.0f, 1.0f,
            0.0f, 6.0f, 1.0f, 0.0f,
            0.0f, -0.3f, 100.0f,
            1.5f, 1.0f, 1.0f, 1.0f, 0.0f,
         };
         for (int i = 0; i < kParamCount; i++)
         {
            mParam[i].store(kDefaults[i], std::memory_order_relaxed);
            mT[i] = kDefaults[i];
         }
         mFaderCur = 1.0f;
         BuildTruePeakFir();
         ApplySampleRate(48000.0);
      }

      // Main thread.
      void SetParam(int i, float v) { mParam[i].store(v, std::memory_order_relaxed); }

      // Main thread (RebuildAudioTopology), possibly while the audio thread is
      // inside Process: only hand the rate over, Process applies it.
      void Prepare(double sampleRate)
      {
         if (sampleRate > 0.0)
            mPendingRate.store(sampleRate, std::memory_order_relaxed);
      }

      // UI thread meter reads. Peaks and GR are "max since last read".
      float TakePeak(int ch) { return mPeakOut[ch & 1].exchange(0.0f, std::memory_order_relaxed); }
      float Rms(int ch) const { return mRmsOut[ch & 1].load(std::memory_order_relaxed); }
      bool TakeClip(int ch) { return mClipOut[ch & 1].exchange(false, std::memory_order_relaxed); }
      float TakeCompGrDb() { return mCompGrOut.exchange(0.0f, std::memory_order_relaxed); }
      float TakeLimGrDb() { return mLimGrOut.exchange(0.0f, std::memory_order_relaxed); }
      float BlockPeak() const { return mBlockPeak.load(std::memory_order_relaxed); }
      // Limiter safety-clamp engagements (samples) since the last read.
      uint32_t TakeLimClamps() { return mLimClampOut.exchange(0u, std::memory_order_relaxed); }

      // Audio thread. `r` may be null (mono output): the chain then runs on a
      // copy of `l` and writes back the left side only.
      void Process(float* l, float* r, int frames)
      {
         for (int i = 0; i < kParamCount; i++)
            mT[i] = mParam[i].load(std::memory_order_relaxed);
         const double pending = mPendingRate.load(std::memory_order_relaxed);
         if (pending > 0.0 && pending != mSampleRate)
            ApplySampleRate(pending);

         // Fader + balance targets, ramped across the whole block.
         const float faderTarget = mT[kFaderGain];
         float balL = 1.0f, balR = 1.0f;
         BalanceGains(mT[kBalance], balL, balR);
         mFaderStart = mFaderCur;
         mFaderDelta = frames > 0 ? (faderTarget - mFaderCur) / (float)frames : 0.0f;
         mBalLStart = mBalLCur;
         mBalRStart = mBalRCur;
         mBalLDelta = frames > 0 ? (balL - mBalLCur) / (float)frames : 0.0f;
         mBalRDelta = frames > 0 ? (balR - mBalRCur) / (float)frames : 0.0f;
         mBlockFrame = 0;

         mBlockPeakL = mBlockPeakR = 0.0f;
         mBlockClipL = mBlockClipR = false;
         mBlockCompGr = 0.0f;
         mBlockLimMin = 1.0f;
         mBlockClamps = 0;

         for (int off = 0; off < frames; off += kChunk)
         {
            const int n = std::min(kChunk, frames - off);
            if (r != nullptr)
               ProcessChunk(l + off, r + off, n);
            else
            {
               std::copy(l + off, l + off + n, mMonoR);
               ProcessChunk(l + off, mMonoR, n);
            }
         }

         mFaderCur = faderTarget;
         mBalLCur = balL;
         mBalRCur = balR;

         AtomicMax(mPeakOut[0], mBlockPeakL);
         AtomicMax(mPeakOut[1], r != nullptr ? mBlockPeakR : mBlockPeakL);
         mRmsOut[0].store(std::sqrt(std::max(0.0f, mMsL)), std::memory_order_relaxed);
         mRmsOut[1].store(std::sqrt(std::max(0.0f, r != nullptr ? mMsR : mMsL)), std::memory_order_relaxed);
         if (mBlockClipL)
            mClipOut[0].store(true, std::memory_order_relaxed);
         if (r != nullptr ? mBlockClipR : mBlockClipL)
            mClipOut[1].store(true, std::memory_order_relaxed);
         mBlockPeak.store(std::max(mBlockPeakL, r != nullptr ? mBlockPeakR : 0.0f), std::memory_order_relaxed);
         AtomicMax(mCompGrOut, mBlockCompGr);
         if (mBlockLimMin < 1.0f)
            AtomicMax(mLimGrOut, -DspMath::LinearToDb(mBlockLimMin));
         if (mBlockClamps > 0)
            mLimClampOut.fetch_add(mBlockClamps, std::memory_order_relaxed);
      }

   private:
      enum Stage { kStEq = 0, kStComp, kStWidth, kStSat, kStLim, kStageCount };

      static void AtomicMax(std::atomic<float>& a, float v)
      {
         float cur = a.load(std::memory_order_relaxed);
         while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed))
         {
         }
      }

      // Equal-power balance normalised to unity at centre: the far side
      // follows the sine law down to silence, the near side stays at 1, so a
      // centred master is bit-identical to the pre-0.50 output.
      static void BalanceGains(float bal, float& gl, float& gr)
      {
         if (std::fabs(bal) < 1e-4f)
         {
            gl = gr = 1.0f; // exact, not sqrt2 * cos(pi/4) rounding
            return;
         }
         DspMath::EqualPowerPan(bal, gl, gr);
         gl = std::min(1.0f, gl * (float)M_SQRT2);
         gr = std::min(1.0f, gr * (float)M_SQRT2);
      }

      void ApplySampleRate(double sr)
      {
         mSampleRate = sr;
         mXfStep = (float)(1.0 / (0.015 * sr));   // 15 ms bypass crossfade
         mMuteStep = (float)(1.0 / (0.010 * sr)); // 10 ms mute ramp
         mChunkAlpha = (float)(1.0 - std::exp(-(double)kChunk / (0.030 * sr))); // ~30 ms param glide
         mRmsCoef = (float)(1.0 - std::exp(-1.0 / (0.300 * sr)));            // 300 ms RMS window
         mDuckStep = (float)(1.0 / (0.002 * sr));  // 2 ms each way when the delay changes
         mEqDirty = true;
         mCompLastAtt = mCompLastRel = -1.0f;
         mLimCoefKey[0] = -1.0f;
         // The rings hold samples of the old rate: clear them, and let the
         // limiter re-apply its config (fresh state) on the next sample.
         for (int c = 0; c < 2; c++)
            std::fill(mRing[c], mRing[c] + kRing, 0.0f);
         mLimCfg = LimCfg {};
         mDuck = 1.0f;
         mLimRateReset = true;
      }

      // ---- stage crossfade -------------------------------------------------
      // Runs `wet` in place on the chunk and blends it against the dry copy
      // while the stage's mix ramps. Fully off: skipped entirely.
      template <class Reset, class Wet>
      void RunStage(int s, bool on, float* l, float* r, int n, Reset&& reset, Wet&& wet)
      {
         const float target = on ? 1.0f : 0.0f;
         float m = mMix[s];
         if (m == 0.0f && target == 0.0f)
            return;
         if (m == 0.0f)
            reset();
         if (m == 1.0f && target == 1.0f)
         {
            wet(l, r, n);
            return;
         }
         std::copy(l, l + n, mDryL);
         std::copy(r, r + n, mDryR);
         wet(l, r, n);
         const float step = target > m ? mXfStep : -mXfStep;
         for (int i = 0; i < n; i++)
         {
            m = std::clamp(m + step, 0.0f, 1.0f);
            l[i] = mDryL[i] + (l[i] - mDryL[i]) * m;
            r[i] = mDryR[i] + (r[i] - mDryR[i]) * m;
         }
         mMix[s] = m;
      }

      static float Glide(float cur, float target, float alpha, float snap)
      {
         const float next = cur + (target - cur) * alpha;
         return std::fabs(target - next) < snap ? target : next;
      }

      void ProcessChunk(float* l, float* r, int n)
      {
         // 1. EQ: params glide per chunk, coefficients only rebuilt when the
         //    glided values actually moved.
         RunStage(kStEq, mT[kEqOn] >= 0.5f, l, r, n,
                  [this] {
                     for (int c = 0; c < 2; c++)
                     {
                        mEqLow[c].Reset();
                        mEqMid[c].Reset();
                        mEqHigh[c].Reset();
                     }
                     for (int k = 0; k < 7; k++)
                        mEqS[k] = mT[kEqLowHz + k];
                     mEqDirty = true;
                  },
                  [this](float* L, float* R, int N) {
                     static const float kSnap[7] = { 0.05f, 0.005f, 0.5f, 0.005f, 0.0005f, 5.0f, 0.005f };
                     bool moved = mEqDirty;
                     for (int k = 0; k < 7; k++)
                     {
                        const float g = Glide(mEqS[k], mT[kEqLowHz + k], mChunkAlpha, kSnap[k]);
                        if (g != mEqS[k])
                           moved = true;
                        mEqS[k] = g;
                     }
                     if (moved)
                        UpdateEqCoefficients();
                     for (int i = 0; i < N; i++)
                     {
                        L[i] = mEqHigh[0].Process(mEqMid[0].Process(mEqLow[0].Process(L[i])));
                        R[i] = mEqHigh[1].Process(mEqMid[1].Process(mEqLow[1].Process(R[i])));
                     }
                  });

         // 2. Glue compressor: stereo-linked peak detector, soft knee, gain
         //    computed every kCompCtl frames and interpolated in between.
         RunStage(kStComp, mT[kCompOn] >= 0.5f, l, r, n,
                  [this] {
                     mCompEnv = 0.0f;
                     mCompGain = 1.0f;
                     mCompStep = 0.0f;
                     mCompCtr = 0;
                  },
                  [this](float* L, float* R, int N) { CompWet(L, R, N); });

         // 3. Stereo width (mid/side). 1 = unchanged, 0 = mono, 2 = double side.
         RunStage(kStWidth, mT[kWidthOn] >= 0.5f, l, r, n,
                  [this] { mWidthCur = mT[kWidth]; },
                  [this](float* L, float* R, int N) {
                     const float target = std::clamp(mT[kWidth], 0.0f, 2.0f);
                     const float w0 = mWidthCur;
                     const float next = Glide(w0, target, mChunkAlpha, 0.0005f);
                     const float dw = (next - w0) / (float)N;
                     for (int i = 0; i < N; i++)
                     {
                        const float w = w0 + dw * (float)(i + 1);
                        const float mid = 0.5f * (L[i] + R[i]);
                        const float side = 0.5f * (L[i] - R[i]) * w;
                        L[i] = mid + side;
                        R[i] = mid - side;
                     }
                     mWidthCur = next;
                  });

         // 4. Saturation: odd-symmetric soft clip (rational tanh), dry/wet,
         //    output trim. No oversampling: keep drive moderate on bright mixes.
         RunStage(kStSat, mT[kSatOn] >= 0.5f, l, r, n,
                  [this] {
                     mSatDrive = DspMath::DbToLinear(std::clamp(mT[kSatDriveDb], 0.0f, 24.0f));
                     mSatMix = std::clamp(mT[kSatMix], 0.0f, 1.0f);
                     mSatOut = DspMath::DbToLinear(std::clamp(mT[kSatOutDb], -12.0f, 6.0f));
                  },
                  [this](float* L, float* R, int N) {
                     const float dTarget = DspMath::DbToLinear(std::clamp(mT[kSatDriveDb], 0.0f, 24.0f));
                     const float mTarget = std::clamp(mT[kSatMix], 0.0f, 1.0f);
                     const float oTarget = DspMath::DbToLinear(std::clamp(mT[kSatOutDb], -12.0f, 6.0f));
                     const float inv = 1.0f / (float)N;
                     const float dd = (dTarget - mSatDrive) * inv, dm = (mTarget - mSatMix) * inv,
                                 dout = (oTarget - mSatOut) * inv;
                     for (int i = 0; i < N; i++)
                     {
                        const float g = mSatDrive + dd * (float)(i + 1);
                        const float mx = mSatMix + dm * (float)(i + 1);
                        const float o = mSatOut + dout * (float)(i + 1);
                        L[i] = (L[i] + (DspMath::FastTanh(L[i] * g) - L[i]) * mx) * o;
                        R[i] = (R[i] + (DspMath::FastTanh(R[i] * g) - R[i]) * mx) * o;
                     }
                     mSatDrive = dTarget;
                     mSatMix = mTarget;
                     mSatOut = oTarget;
                  });

         // 5. Master fader + balance (always on; ramped across the block, so
         //    an unchanged fader multiplies by exactly the old constant).
         for (int i = 0; i < n; i++)
         {
            const float t = (float)(++mBlockFrame);
            const float g = mFaderStart + mFaderDelta * t;
            l[i] *= g * (mBalLStart + mBalLDelta * t);
            r[i] *= g * (mBalRStart + mBalRDelta * t);
         }

         // 6. Brickwall limiter. The delay line is fed even while off, so
         //    switching it on reads real history.
         LimiterChunk(l, r, n);

         // 7. Mute: 10 ms linear ramp, so a live mute never clicks.
         const float muteTarget = mT[kMute] >= 0.5f ? 0.0f : 1.0f;
         if (mMuteGain != muteTarget || muteTarget != 1.0f)
         {
            for (int i = 0; i < n; i++)
            {
               if (mMuteGain < muteTarget)
                  mMuteGain = std::min(muteTarget, mMuteGain + mMuteStep);
               else if (mMuteGain > muteTarget)
                  mMuteGain = std::max(muteTarget, mMuteGain - mMuteStep);
               l[i] *= mMuteGain;
               r[i] *= mMuteGain;
            }
         }

         // 8. Meters (post everything: what actually leaves the mixer).
         float pl = mBlockPeakL, pr = mBlockPeakR, msl = mMsL, msr = mMsR;
         for (int i = 0; i < n; i++)
         {
            const float al = std::fabs(l[i]), ar = std::fabs(r[i]);
            pl = std::max(pl, al);
            pr = std::max(pr, ar);
            msl += (l[i] * l[i] - msl) * mRmsCoef;
            msr += (r[i] * r[i] - msr) * mRmsCoef;
         }
         mBlockClipL = mBlockClipL || pl >= 1.0f;
         mBlockClipR = mBlockClipR || pr >= 1.0f;
         mBlockPeakL = pl;
         mBlockPeakR = pr;
         mMsL = DspMath::FlushDenormal(msl);
         mMsR = DspMath::FlushDenormal(msr);
      }

      void UpdateEqCoefficients()
      {
         const double nyq = mSampleRate * 0.45;
         const double lowHz = std::clamp((double)mEqS[0], 20.0, std::min(1000.0, nyq));
         const double midHz = std::clamp((double)mEqS[2], 40.0, std::min(16000.0, nyq));
         const double highHz = std::clamp((double)mEqS[5], 1000.0, std::min(20000.0, nyq));
         const double midQ = std::clamp((double)mEqS[4], 0.2, 10.0);
         mEqLow[0].SetLowShelf(lowHz, 0.707, std::clamp(mEqS[1], -15.0f, 15.0f), mSampleRate);
         mEqMid[0].SetPeaking(midHz, midQ, std::clamp(mEqS[3], -15.0f, 15.0f), mSampleRate);
         mEqHigh[0].SetHighShelf(highHz, 0.707, std::clamp(mEqS[6], -15.0f, 15.0f), mSampleRate);
         CopyCoefficients(mEqLow[0], mEqLow[1]);
         CopyCoefficients(mEqMid[0], mEqMid[1]);
         CopyCoefficients(mEqHigh[0], mEqHigh[1]);
         mEqDirty = false;
      }

      static void CopyCoefficients(const DspMath::Biquad& from, DspMath::Biquad& to)
      {
         to.b0 = from.b0;
         to.b1 = from.b1;
         to.b2 = from.b2;
         to.a1 = from.a1;
         to.a2 = from.a2;
      }

      float TimeCoef(float ms) const
      {
         return (float)std::exp(-1.0 / (std::max(0.01, (double)ms * 0.001) * mSampleRate));
      }

      void CompWet(float* L, float* R, int N)
      {
         const float attMs = std::clamp(mT[kCompAttackMs], 0.1f, 100.0f);
         const float relMs = std::clamp(mT[kCompReleaseMs], 20.0f, 2000.0f);
         if (attMs != mCompLastAtt)
         {
            mCompAtt = TimeCoef(attMs);
            mCompLastAtt = attMs;
         }
         if (relMs != mCompLastRel)
         {
            mCompRel = TimeCoef(relMs);
            mCompLastRel = relMs;
         }
         const float thr = std::clamp(mT[kCompThreshDb], -48.0f, 0.0f);
         const float slope = 1.0f / std::clamp(mT[kCompRatio], 1.0f, 20.0f) - 1.0f; // <= 0
         const float makeup = std::clamp(mT[kCompMakeupDb], 0.0f, 24.0f);

         for (int i = 0; i < N; i++)
         {
            if (mCompCtr == 0)
            {
               // Soft-knee static curve (Giannoulis, Massberg, Reiss 2012).
               const float over = DspMath::LinearToDb(mCompEnv) - thr;
               float grDb = 0.0f;
               if (2.0f * over >= kKneeDb)
                  grDb = slope * over;
               else if (2.0f * over > -kKneeDb)
               {
                  const float k = over + 0.5f * kKneeDb;
                  grDb = slope * k * k / (2.0f * kKneeDb);
               }
               mBlockCompGr = std::max(mBlockCompGr, -grDb);
               const float target = DspMath::DbToLinear(grDb + makeup);
               mCompStep = (target - mCompGain) / (float)kCompCtl;
               mCompCtr = kCompCtl;
            }
            mCompCtr--;
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            mCompEnv = x + (mCompEnv - x) * (x > mCompEnv ? mCompAtt : mCompRel);
            mCompGain += mCompStep;
            L[i] *= mCompGain;
            R[i] *= mCompGain;
         }
         mCompEnv = DspMath::FlushDenormal(mCompEnv);
      }

      // ---- limiter v2 --------------------------------------------------------
      // Turbo 0.50: mastering-grade lookahead limiter.
      //
      //   in -> delay line (L samples) ----------------------------> x gain -> safety clamp
      //    \-> 4x true-peak estimate -> link -> required gain -> pre-delay
      //        -> min-hold over A' -> release (manual, or fast+slow auto)
      //        -> two cascaded box filters (support A') -----------/
      //
      // The hold keeps every required gain for A' samples and the two boxes
      // (lengths B1 + B2 - 1 = A') only average values that are already at or
      // below it, so the smoothed gain reaches each peak's target exactly
      // when that peak leaves the delay line: no overshoot, and an S-shaped
      // (C1) attack instead of a corner. L = kSideDelay + A - 1, A' = A times
      // the style's attack fraction, the rest of A is a pre-delay on the
      // sidechain so a shorter attack still lands on time.
      struct LimCfg
      {
         int lat = 0;      // samples the output is delayed by; 0 = idle (no delay)
         int hold = 2;     // A'
         int pre = 0;      // A - A'
         int box1 = 1, box2 = 2;
         bool tp = true;
         bool operator==(const LimCfg& o) const
         {
            return lat == o.lat && hold == o.hold && pre == o.pre && tp == o.tp;
         }
      };

      struct LimStyleDef
      {
         float attackFrac, fastScale, slowAttackMs, slowReleaseMs;
      };
      // transparent / punchy / loud: only internal time constants.
      static const LimStyleDef& Style(int i)
      {
         static const LimStyleDef kStyles[kLimStyleCount] = {
            { 1.00f, 1.0f, 100.0f, 1200.0f },
            { 0.60f, 0.7f, 220.0f, 700.0f },
            { 0.35f, 0.5f, 450.0f, 350.0f },
         };
         return kStyles[std::clamp(i, 0, kLimStyleCount - 1)];
      }

      static double BesselI0(double x)
      {
         double sum = 1.0, term = 1.0;
         for (int k = 1; k < 40; k++)
         {
            const double h = x / (2.0 * k);
            term *= h * h;
            sum += term;
         }
         return sum;
      }

      // Kaiser-windowed sinc, one 48-tap kernel per fractional phase 1/4,
      // 2/4, 3/4 (phase 0 is the sample itself). Each phase is normalised to
      // unity DC gain. Built once, at construction (main thread).
      void BuildTruePeakFir()
      {
         const double beta = 4.0;
         const double i0b = BesselI0(beta);
         for (int p = 0; p < 3; p++)
         {
            const double frac = (double)(p + 1) * 0.25;
            double sum = 0.0;
            double h[kTpTaps];
            for (int k = 0; k < kTpTaps; k++)
            {
               const double d = (double)(k - (kTpHalf - 1)) - frac;
               const double sinc = std::sin(M_PI * d) / (M_PI * d);
               const double u = d / (double)kTpHalf;
               const double w = BesselI0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0b;
               h[k] = sinc * w;
               sum += h[k];
            }
            for (int k = 0; k < kTpTaps; k++)
               mTpFir[p][k] = (float)(h[k] / sum);
         }
      }

      // Peak of channel c over the segment [s, s+1): the sample itself and,
      // with true peak on, the three interpolated points after it.
      float SegmentPeak(int c, uint32_t s, bool tp) const
      {
         const float* x = mRing[c];
         const float x0 = std::fabs(x[s & kMask]);
         if (!tp)
            return x0;
         const uint32_t base = s - (uint32_t)(kTpHalf - 1);
         float a = 0.0f, b = 0.0f, d = 0.0f;
         for (int k = 0; k < kTpTaps; k++)
         {
            const float v = x[(base + (uint32_t)k) & kMask];
            a += mTpFir[0][k] * v;
            b += mTpFir[1][k] * v;
            d += mTpFir[2][k] * v;
         }
         return std::max(std::max(x0, std::fabs(a)), std::max(std::fabs(b), std::fabs(d)));
      }

      // Required gain for input sample j (segments j-1..j+1 must be stored).
      void RequiredGain(uint32_t j, bool tp, float ceil, float link, float& gl, float& gr) const
      {
         float lv[2];
         for (int c = 0; c < 2; c++)
         {
            const float* sg = mSeg[c];
            lv[c] = tp ? std::max(sg[j & kMask], std::max(sg[(j - 1u) & kMask], sg[(j + 1u) & kMask]))
                       : sg[j & kMask];
         }
         // Link: each side follows its own level plus `link` of the gap to
         // the louder side, so 100% is fully linked and 0% fully dual-mono;
         // never below the side's own level, so the ceiling always holds.
         const float mx = std::max(lv[0], lv[1]);
         const float el = lv[0] + (mx - lv[0]) * link;
         const float er = lv[1] + (mx - lv[1]) * link;
         gl = el > ceil ? ceil / el : 1.0f;
         gr = er > ceil ? ceil / er : 1.0f;
      }

      LimCfg TargetCfg() const
      {
         LimCfg c;
         c.lat = LimiterLatencySamples(mT[kLimLookaheadMs], mSampleRate);
         const int a = c.lat - kSideDelay + 1;
         const int style = (int)std::lround(mT[kLimStyle]);
         c.hold = std::clamp((int)std::lround((double)a * Style(style).attackFrac), kMinHold, a);
         c.pre = a - c.hold;
         c.box1 = (c.hold + 1) / 2;
         c.box2 = c.hold + 1 - c.box1;
         c.tp = mT[kLimTruePeak] >= 0.5f;
         return c;
      }

      // Switch the limiter to `cfg` at time t (x[t] is the newest sample in
      // the ring). Only ever called at the bottom of a duck (output silent)
      // or after a rate reset, so the jump in delay is inaudible. The state
      // is rebuilt from the ring: every sample still in flight gets its
      // required gain recomputed, and the release/box state starts at the
      // minimum of them, so nothing in flight can pass above the ceiling.
      void ApplyLimCfg(const LimCfg& cfg, uint32_t t, float ceil, float link)
      {
         mLimCfg = cfg;
         if (cfg.lat <= 0)
            return;
         const uint32_t L = (uint32_t)cfg.lat;
         for (uint32_t s = t - L - 1u; s != t - (uint32_t)kTpHalf; s++)
            for (int c = 0; c < 2; c++)
               mSeg[c][s & kMask] = SegmentPeak(c, s, cfg.tp);
         float seed[2] = { 1.0f, 1.0f };
         const uint32_t jEnd = t - (uint32_t)kSideDelay;
         for (uint32_t j = t - L; j != jEnd; j++)
         {
            float g[2];
            RequiredGain(j, cfg.tp, ceil, link, g[0], g[1]);
            for (int c = 0; c < 2; c++)
            {
               mReq[c][j & kMask] = g[c];
               seed[c] = std::min(seed[c], g[c]);
            }
         }
         const uint32_t qEnd = jEnd - (uint32_t)cfg.pre;
         for (int c = 0; c < 2; c++)
         {
            mDqHead[c] = mDqTail[c] = 0;
            for (uint32_t q = t - L; q != qEnd; q++)
               DequePush(c, mReq[c][q & kMask], q);
            mFast[c] = mSlow[c] = seed[c];
            std::fill(mBox1[c], mBox1[c] + cfg.box1, seed[c]);
            std::fill(mBox2[c], mBox2[c] + cfg.box2, seed[c]);
            mBoxSum1[c] = (double)seed[c] * cfg.box1;
            mBoxSum2[c] = (double)seed[c] * cfg.box2;
            mBoxPos1[c] = mBoxPos2[c] = 0;
         }
      }

      void DequePush(int c, float v, uint32_t idx)
      {
         int& head = mDqHead[c];
         int& tail = mDqTail[c];
         while (tail != head && mDqVal[c][(tail - 1) & (int)kMask] >= v)
            tail = (tail - 1) & (int)kMask;
         mDqVal[c][tail] = v;
         mDqIdx[c][tail] = idx;
         tail = (tail + 1) & (int)kMask;
      }

      static float SmoothStep(float x) { return x * x * (3.0f - 2.0f * x); }

      void LimiterChunk(float* l, float* r, int n)
      {
         const bool on = mT[kLimOn] >= 0.5f;
         float m = mMix[kStLim];

         // Fully bypassed: only keep the ring fed (so switching in reads real
         // history); the signal is not touched at all (bit-exact, no delay).
         if (!on && m == 0.0f && mLimCfg.lat == 0 && mDuck >= 1.0f)
         {
            mLimRateReset = false;
            for (int i = 0; i < n; i++)
            {
               const uint32_t w = mClock++ & kMask;
               mRing[0][w] = l[i];
               mRing[1][w] = r[i];
            }
            return;
         }

         // Per-chunk coefficients (exp only when a time constant changed).
         const float relMs = std::clamp(mT[kLimReleaseMs], 1.0f, 2000.0f);
         const bool autoRel = mT[kLimAutoRelease] >= 0.5f;
         const int styleIdx = std::clamp((int)std::lround(mT[kLimStyle]), 0, kLimStyleCount - 1);
         const LimStyleDef& style = Style(styleIdx);
         if (relMs != mLimCoefKey[0] || (autoRel ? 1.0f : 0.0f) != mLimCoefKey[1] || (float)styleIdx != mLimCoefKey[2])
         {
            mLimCoefKey[0] = relMs;
            mLimCoefKey[1] = autoRel ? 1.0f : 0.0f;
            mLimCoefKey[2] = (float)styleIdx;
            mFastCoef = TimeCoef(autoRel ? relMs * style.fastScale : relMs);
            mSlowAttCoef = TimeCoef(style.slowAttackMs);
            mSlowRelCoef = TimeCoef(style.slowReleaseMs);
         }
         const float ceilTarget = DspMath::DbToLinear(std::clamp(mT[kLimCeilingDb], -24.0f, 0.0f));
         if (mCeil <= 0.0f)
            mCeil = ceilTarget;
         const float c0 = mCeil;
         const float dc = (ceilTarget - c0) / (float)n;
         const float link = std::clamp(mT[kLimLink], 0.0f, 1.0f);

         // The safety clamp follows a falling ceiling only once the samples
         // limited against the old one have left the delay line.
         if (ceilTarget >= mClampCeil)
         {
            mClampCeil = ceilTarget;
            mClampHold = 0;
         }
         else if ((mClampHold += n) > mLimCfg.lat + kChunk)
            mClampCeil = ceilTarget;
         const float clampCeil = std::max(mClampCeil, c0);

         const LimCfg want = TargetCfg();
         bool forceApply = false;
         if (mLimRateReset)
         {
            mLimRateReset = false;
            forceApply = on || m > 0.0f;
         }
         auto pendingSwitch = [&]() {
            return on ? !(mLimCfg == want) : (m == 0.0f && mLimCfg.lat != 0);
         };
         bool switching = pendingSwitch();
         float gMin = mBlockLimMin;
         uint32_t clamps = 0;

         for (int i = 0; i < n; i++)
         {
            const float ceil = c0 + dc * (float)(i + 1);
            const float inL = l[i], inR = r[i];
            const uint32_t t = mClock++;
            mRing[0][t & kMask] = inL;
            mRing[1][t & kMask] = inR;

            if (forceApply)
            {
               ApplyLimCfg(want, t, ceil, link);
               forceApply = false;
               switching = pendingSwitch();
            }

            // Delay change: duck to silence, switch, come back (2 ms each
            // way). Mix ramps only once the delay is where it should be, so
            // the 15 ms crossfade always blends equally delayed dry and wet.
            if (switching)
            {
               mDuck -= mDuckStep;
               if (mDuck <= 0.0f)
               {
                  mDuck = 0.0f;
                  ApplyLimCfg(on ? want : LimCfg {}, t, ceil, link);
                  switching = false;
               }
            }
            else if (mDuck < 1.0f)
               mDuck = std::min(1.0f, mDuck + mDuckStep);
            else if (on ? m < 1.0f : m > 0.0f)
            {
               m = std::clamp(m + (on ? mXfStep : -mXfStep), 0.0f, 1.0f);
               if (m == 0.0f)
                  switching = pendingSwitch();
            }

            float outL = inL, outR = inR;
            const LimCfg& cfg = mLimCfg;
            if (cfg.lat > 0)
            {
               // Sidechain: newest complete segment, then the required gain
               // of the sample kSideDelay back, then the pre-delayed stream
               // into hold / release / boxes.
               const uint32_t s = t - (uint32_t)kTpHalf;
               for (int c = 0; c < 2; c++)
                  mSeg[c][s & kMask] = SegmentPeak(c, s, cfg.tp);
               const uint32_t j = t - (uint32_t)kSideDelay;
               float req[2];
               RequiredGain(j, cfg.tp, ceil, link, req[0], req[1]);
               mReq[0][j & kMask] = req[0];
               mReq[1][j & kMask] = req[1];
               const uint32_t q = j - (uint32_t)cfg.pre;
               float g[2];
               for (int c = 0; c < 2; c++)
               {
                  DequePush(c, mReq[c][q & kMask], q);
                  while (q - mDqIdx[c][mDqHead[c]] >= (uint32_t)cfg.hold)
                     mDqHead[c] = (mDqHead[c] + 1) & (int)kMask;
                  const float h = mDqVal[c][mDqHead[c]];

                  // Release. Fast stage: instant down, release knob up. Auto
                  // adds a slow stage that only sinks while reduction is
                  // sustained (its attack is the style's integration time);
                  // the gain is the lower of the two, so a lone transient
                  // recovers fast and dense material recovers slowly. Both
                  // stay <= h, so release never lets a peak through.
                  float rv = mFast[c] = h < mFast[c] ? h : h + (mFast[c] - h) * mFastCoef;
                  if (autoRel)
                  {
                     mSlow[c] = h + (mSlow[c] - h) * (h < mSlow[c] ? mSlowAttCoef : mSlowRelCoef);
                     rv = std::min(rv, mSlow[c]);
                  }
                  else
                     mSlow[c] = rv;

                  mBoxSum1[c] += (double)(rv - mBox1[c][mBoxPos1[c]]);
                  mBox1[c][mBoxPos1[c]] = rv;
                  if (++mBoxPos1[c] >= cfg.box1)
                     mBoxPos1[c] = 0;
                  const float b1 = (float)(mBoxSum1[c] / (double)cfg.box1);
                  mBoxSum2[c] += (double)(b1 - mBox2[c][mBoxPos2[c]]);
                  mBox2[c][mBoxPos2[c]] = b1;
                  if (++mBoxPos2[c] >= cfg.box2)
                     mBoxPos2[c] = 0;
                  g[c] = std::min(1.0f, (float)(mBoxSum2[c] / (double)cfg.box2));
               }

               const uint32_t rd = (t - (uint32_t)cfg.lat) & kMask;
               const float dl = mRing[0][rd], dr = mRing[1][rd];
               float wl = dl * g[0], wr = dr * g[1];
               // Last resort; practically never engages (counted for the UI).
               // Rounding (1 ulp) is clamped silently; only a real over
               // (> 0.001 dB) counts.
               if (std::fabs(wl) > clampCeil)
               {
                  clamps += std::fabs(wl) > clampCeil * 1.000115f ? 1u : 0u;
                  wl = std::clamp(wl, -clampCeil, clampCeil);
               }
               if (std::fabs(wr) > clampCeil)
               {
                  clamps += std::fabs(wr) > clampCeil * 1.000115f ? 1u : 0u;
                  wr = std::clamp(wr, -clampCeil, clampCeil);
               }
               if (m >= 1.0f)
               {
                  outL = wl;
                  outR = wr;
               }
               else
               {
                  outL = dl + (wl - dl) * m;
                  outR = dr + (wr - dr) * m;
               }
               if (m > 0.0f)
                  gMin = std::min(gMin, std::min(g[0], g[1]));
            }
            if (mDuck < 1.0f)
            {
               const float d = SmoothStep(mDuck);
               outL *= d;
               outR *= d;
            }
            l[i] = outL;
            r[i] = outR;
         }
         mCeil = ceilTarget;
         mMix[kStLim] = m;
         mBlockLimMin = gMin;
         mBlockClamps += clamps;

         // Re-sum the boxes now and then so float error in the running sums
         // never accumulates over a long set.
         if (mLimCfg.lat > 0 && (mBoxResum += n) >= 8192)
         {
            mBoxResum = 0;
            for (int c = 0; c < 2; c++)
            {
               double s1 = 0.0, s2 = 0.0;
               for (int i = 0; i < mLimCfg.box1; i++)
                  s1 += (double)mBox1[c][i];
               for (int i = 0; i < mLimCfg.box2; i++)
                  s2 += (double)mBox2[c][i];
               mBoxSum1[c] = s1;
               mBoxSum2[c] = s2;
            }
         }
      }

      // Params: written by the main thread, snapshotted into mT per block.
      std::atomic<float> mParam[kParamCount];
      float mT[kParamCount] = {};
      std::atomic<double> mPendingRate { 0.0 };

      double mSampleRate = 48000.0;
      float mXfStep = 0.0f, mMuteStep = 0.0f, mChunkAlpha = 1.0f, mRmsCoef = 0.0f;
      float mMix[kStageCount] = {};
      float mDryL[kChunk] = {}, mDryR[kChunk] = {}, mMonoR[kChunk] = {};

      // Fader / balance / mute
      float mFaderCur = 1.0f, mFaderStart = 1.0f, mFaderDelta = 0.0f;
      float mBalLCur = 1.0f, mBalRCur = 1.0f, mBalLStart = 1.0f, mBalRStart = 1.0f;
      float mBalLDelta = 0.0f, mBalRDelta = 0.0f;
      int mBlockFrame = 0;
      float mMuteGain = 1.0f;

      // EQ
      DspMath::Biquad mEqLow[2], mEqMid[2], mEqHigh[2];
      float mEqS[7] = { 100.0f, 0.0f, 1000.0f, 0.0f, 0.7f, 10000.0f, 0.0f };
      bool mEqDirty = true;

      // Compressor
      float mCompEnv = 0.0f, mCompGain = 1.0f, mCompStep = 0.0f;
      float mCompAtt = 0.0f, mCompRel = 0.0f, mCompLastAtt = -1.0f, mCompLastRel = -1.0f;
      int mCompCtr = 0;

      // Width / saturation
      float mWidthCur = 1.0f;
      float mSatDrive = 1.0f, mSatMix = 1.0f, mSatOut = 1.0f;

      // Limiter (Turbo 0.50 v2). Fixed-size rings, indexed by the absolute
      // sample clock masked to kRing.
      float mRing[2][kRing] = {};
      float mSeg[2][kRing] = {};
      float mReq[2][kRing] = {};
      float mDqVal[2][kRing] = {};
      uint32_t mDqIdx[2][kRing] = {};
      int mDqHead[2] = {}, mDqTail[2] = {};
      float mBox1[2][kRing] = {}, mBox2[2][kRing] = {};
      double mBoxSum1[2] = {}, mBoxSum2[2] = {};
      int mBoxPos1[2] = {}, mBoxPos2[2] = {};
      int mBoxResum = 0;
      float mFast[2] = { 1.0f, 1.0f }, mSlow[2] = { 1.0f, 1.0f };
      float mFastCoef = 0.0f, mSlowAttCoef = 0.0f, mSlowRelCoef = 0.0f;
      float mLimCoefKey[3] = { -1.0f, -1.0f, -1.0f };
      float mTpFir[3][kTpTaps] = {};
      uint32_t mClock = 0;
      LimCfg mLimCfg;
      float mDuck = 1.0f, mDuckStep = 0.0f;
      bool mLimRateReset = false;
      float mCeil = 0.0f, mClampCeil = 1.0f;
      int mClampHold = 0;
      uint32_t mBlockClamps = 0;
      std::atomic<uint32_t> mLimClampOut { 0u };

      // Meters (audio-thread accumulators, then the published atomics)
      float mMsL = 0.0f, mMsR = 0.0f;
      float mBlockPeakL = 0.0f, mBlockPeakR = 0.0f, mBlockCompGr = 0.0f, mBlockLimMin = 1.0f;
      bool mBlockClipL = false, mBlockClipR = false;
      std::atomic<float> mPeakOut[2] { 0.0f, 0.0f };
      std::atomic<float> mRmsOut[2] { 0.0f, 0.0f };
      std::atomic<bool> mClipOut[2] { false, false };
      std::atomic<float> mCompGrOut { 0.0f };
      std::atomic<float> mLimGrOut { 0.0f };
      std::atomic<float> mBlockPeak { 0.0f };
   };
}
