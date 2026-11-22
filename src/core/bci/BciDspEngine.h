#pragma once

#include "BciTypes.h"

#include <array>
#include <deque>
#include <string>
#include <vector>

namespace Bci
{
   // Multi-rate biophysical signal separation, 1/f^chi detrended cortical PSD,
   // instantaneous IAF phase-locking, and 15-second robust MAD calibration.
   class BciDspEngine
   {
   public:
      BciDspEngine();

      void Reset(double sampleRateHz = kSampleRateHz);
      void SetSampleRate(double sampleRateHz);

      // Configuration parameters (updated from BrainSourceNode)
      void SetSmoothing(float smoothing01) { mSmoothing = smoothing01; }
      void SetArtifactBlanking(bool enabled) { mArtifactBlanking = enabled; }
      void SetStartleGuard(bool enabled) { mStartleGuard = enabled; }
      void SetBlinkMode(BlinkMode mode) { mBlinkMode = mode; }
      void SetStateTauSec(double tauSec) { mStateTauSec = std::max(0.25, tauSec); }

      // Gap 6: Calibration & profile persistence
      void StartCalibration();
      void CancelCalibration();
      // The last calibration phase on its own (also run by StartCalibration after eyes closed).
      void StartBlinkCalibration(double seconds = kBlinkPhaseSec);
      bool IsCalibrating() const
      {
         return mCalibPhase == CalibrationPhase::EyesOpen || mCalibPhase == CalibrationPhase::EyesClosed ||
                mCalibPhase == CalibrationPhase::Blinks;
      }
      static constexpr double kBlinkPhaseSec = 12.0;
      const CalibrationProfile& Profile() const { return mProfile; }
      void SetProfile(const CalibrationProfile& prof) { mProfile = prof; }
      void SetPersistProfile(bool persist) { mPersistProfile = persist; } // false: calibration never writes disk (tests)
      static std::string DefaultProfilePath();
      bool SaveProfile(const std::string& path = "") const;
      bool LoadProfile(const std::string& path = "");

      // Processes a batch of raw samples and updates `outPins24` (0..1 clamped) and `outTelemetry`.
      void Process(const SampleFrame* frames, int count, float outPins24[kOutputPinCount], TelemetrySnapshot& outTelemetry);
      // Audified frames produced by the last Process call (one per input sample).
      const std::vector<AudifyFrame>& Audify() const { return mAudify; }

   private:
      struct Biquad
      {
         double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
         double z1 = 0, z2 = 0;
         void Reset() { z1 = z2 = 0; }
         double Step(double x)
         {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
         }
         void SetHighPass(double fs, double fc, double q = 0.7071);
         void SetLowPass(double fs, double fc, double q = 0.7071);
         void SetBandPass(double fs, double f0, double q);
         void SetNotch(double fs, double f0, double q = 20.0);
      };

      struct EuroState
      {
         float x = 0.0f;
         float dx = 0.0f;
         double t = -1.0;
         float Step(float val, double timeSec, float minCutoff, float beta);
      };

      void RebuildFilters();
      void UpdateContactQuality();
      void ComputeWelchCorticalPsd();
      void FinishCalibration();
      void FinishBlinkCalibration();
      void UpdateStateFeatures(const double (&chanPsd)[kChannelCount][48], double binHz);
      bool FitStateWeights();
      void UpdateImageColumn(const double (&chanPsd)[kChannelCount][48], double binHz);
      void StepAudify(const double (&clean)[kChannelCount]);
      void FinishDirectBaselines();

      double mSampleRate = kSampleRateHz;
      double mStreamTimeSec = 0.0;
      float mSmoothing = 0.45f;
      bool mArtifactBlanking = true;
      bool mStartleGuard = true;
      bool mPersistProfile = true;
      BlinkMode mBlinkMode = BlinkMode::IntentionalOrDouble;

      // Per-channel preprocessing filters
      Biquad mHpDc[kChannelCount];
      Biquad mNotch50[kChannelCount];
      Biquad mNotch60[kChannelCount];

      // Somatic spatial-frequency filters
      Biquad mBlinkLp;
      Biquad mBlinkHp;
      Biquad mBlinkDiffLp;
      Biquad mSaccadeLp;
      Biquad mSaccadeHp;
      Biquad mJawHp[2];
      Biquad mJawLp[2];
      Biquad mBrowHp;
      Biquad mBrowLp;

      // 1st-order 0.35 Hz DC blocker for blink common/diff (zero ringing overshoot on double blinks)
      bool mBlinkDcInit = false;
      double mBlinkDcPrevIn = 0.0, mBlinkDcPrevOut = 0.0;
      double mBlinkDiffDcPrevIn = 0.0, mBlinkDiffDcPrevOut = 0.0;

      // Instantaneous IAF complex resonator state (AF7, AF8, TP9, TP10)
      double mResRe[kChannelCount] = {};
      double mResIm[kChannelCount] = {};
      double mPlvRe = 0.5;
      double mPlvIm = 0.0;

      // Fast somatic envelope & blink state machine
      double mJawRmsSq[2] = { 0.0, 0.0 };
      double mBrowRmsSq = 0.0;
      double mImuVelSmooth = 0.0;
      double mPpgLp = 0.0;
      double mPpgBaseline = 0.0;

      bool mBlinkInPulse = false;
      int mBlinkPulseSamples = 0;
      float mBlinkPeakUv = 0.0f;
      float mBlinkDiffPeakUv = 0.0f;
      double mBlinkStartTimeSec = -10.0;
      double mBlinkPeakTimeSec = -10.0;
      double mBlinkValleyUv = 0.0;
      double mPrevValidBlinkPeakSec = -10.0;
      double mPrevValidBlinkEndSec = -10.0;
      float mPrevValidBlinkPeakUv = 0.0f;
      double mLastBlinkEndSec = -10.0;
      double mBlinkLatchUntilSec = -10.0;
      double mBlinkRefractoryUntilSec = -10.0;
      double mBlinkVotePos = 0.0, mBlinkVoteNeg = 0.0; // decayed seconds past threshold per side
      double mBlinkSign = 1.0;               // learned blink polarity
      double mArtifactHoldUntilSec = -10.0;

      // Flashes for UI micro-scope
      float mBlinkFlash = 0.0f;
      float mWinkFlash = 0.0f;
      float mJawLFlash = 0.0f;
      float mJawRFlash = 0.0f;

      // 256-sample sliding cortical ring buffer per channel
      static constexpr int kFftSize = 256;
      std::array<std::array<float, kFftSize>, kChannelCount> mRing{};
      int mRingHead = 0;
      int mSamplesSincePsd = 0;

      // Held cortical outputs (preserved during artifact blanking)
      float mBands[kBandCount] = { 0.2f, 0.25f, 0.45f, 0.25f, 0.15f };
      float mFaaRaw = 0.0f;
      float mSpectralEntropy = 0.5f;
      float mAperiodicSlope = 1.5f;
      float mAperiodicOffset = 1.0f;
      float mContactQuality[kChannelCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
      float mPrevArousal = 0.5f;
      float mDriftState = 0.35f;
      float mSurgeState = 0.0f;

      // Trace buffer for UI (128 samples)
      float mTraceFrontal[kTraceSamples] = {};
      float mTraceTemporal[kTraceSamples] = {};
      int mTraceDecim = 0;

      // Per-person eyes-closed state (per-channel relative band powers, EMA tau 1.5 s; see tools/bci/calib_ab.py)
      static constexpr double kStateTauSec = 1.5;
      double mStateTauSec = kStateTauSec;
      double mFeatEma[kStateFeatureCount] = {};
      bool mFeatEmaInit = false;
      float mRegionAlpha[kChannelCount] = {};
      float mStateScore = 0.0f;
      bool mStateCalibFailed = false;
      std::vector<std::array<float, kStateFeatureCount>> mCalibFeatOpen;
      std::vector<std::array<float, kStateFeatureCount>> mCalibFeatClosed;
      double mCalibChanQualitySum[kChannelCount] = {};
      int mCalibChanQualityCount = 0;

      // Direct Image pin: log power EMA (tau 0.5 s), running baseline until calibrated
      static constexpr double kImageTauSec = 0.5;
      static constexpr double kBaselineTauSec = 30.0;
      double mImgLog[kImageColumnSize] = {};
      double mImgRunMean[kImageColumnSize] = {};
      double mImgRunDev[kImageColumnSize] = {};
      bool mImgInit = false;
      float mImgCol[kImageColumnSize] = {};
      unsigned mImgSeq = 0;
      std::vector<std::array<float, kImageColumnSize>> mCalibImgOpen;

      // Direct Audio pin: 2-40 Hz band-pass, 65-tap windowed FIR Hilbert (32 samples delay)
      static constexpr int kHilbertTaps = 65;
      double mHilbertH[kHilbertTaps] = {};
      Biquad mAudHp[kChannelCount][2];
      Biquad mAudLp[kChannelCount][2];
      double mAudLine[kChannelCount][kHilbertTaps] = {};
      int mAudHead = 0;
      double mAudPrevPhase[kChannelCount] = {};
      double mAudRunAmp[kChannelCount] = { 1.0, 1.0, 1.0, 1.0 };
      std::vector<float> mCalibAudAmp[kChannelCount];
      std::vector<AudifyFrame> mAudify;

      // Calibration accumulator (30 s open + 30 s closed; first 4 s of each phase not used for
      // the state weights while the eyes settle and the EMA catches up)
      static constexpr double kCalibPhaseSec = 30.0;
      static constexpr double kCalibSettleSec = 4.0;
      CalibrationPhase mCalibPhase = CalibrationPhase::Idle;
      double mCalibElapsedSec = 0.0;
      double mBlinkPhaseLen = kBlinkPhaseSec;
      std::vector<float> mCalibBlinkSig; // blink-band signal (signed) during the Blinks phase
      CalibrationProfile mProfile;
      std::vector<float> mCalibChanSamples[kChannelCount];
      std::vector<float> mCalibClosedAlphaBins;
      std::vector<float> mCalibOpenAlpha;
      std::vector<float> mCalibOpenBeta;
      int mCalibAlphaBinsAccumCount = 0;
      std::array<double, 32> mCalibClosedPsdAccum{};

      // Slow 180s homeostatic baseline tracker for Arousal
      double mHomeoArousalMean = 0.0;
      double mHomeoArousalVar = 0.25;

      // Latest raw pins before One-Euro smoother
      float mRawPins[kOutputPinCount] = {};
      EuroState mEuro[kOutputPinCount];
   };
}
