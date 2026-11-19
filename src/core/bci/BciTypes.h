#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Bci
{
   constexpr int kChannelCount = 4; // AF7 (0), AF8 (1), TP9 (2), TP10 (3)
   constexpr int kBandCount = 5;    // Delta, Theta, Alpha, Beta, Gamma
   constexpr int kTraceSamples = 128;
   constexpr int kOutputPinCount = 24;
   constexpr double kSampleRateHz = 256.0;
   // Per-person eyes-closed state: 4 bands (4-8, 8-10, 10-12, 12-30 Hz) x 4 channels of
   // log10(band / 2-40 Hz total), index band * 4 + channel. Validated in tools/bci/calib_ab.py.
   constexpr int kStateFeatureCount = 16;
   // Direct Image pin: 4 rows (AF7 AF8 TP9 TP10) x 40 bins (1..40 Hz), one column per PSD update.
   // Mirrored in tools/bci/direct_pins.py.
   constexpr int kImageBins = 40;
   constexpr int kImageColumnSize = kChannelCount * kImageBins;
   // Direct Audio pin: each channel's analytic signal, frequency x kAudifyMult (10 Hz alpha -> 320 Hz).
   constexpr double kAudifyMult = 32.0;

   // One EEG sample of the audified stream: amplitude / baseline amplitude, phase step (rad).
   struct AudifyFrame
   {
      float amp[kChannelCount] = {};
      float dphase[kChannelCount] = {};
   };

   enum class BoardKind : int
   {
      Synthetic = 0,
      CsvReplay,
      Muse2,
      MuseS,
      OpenBCICyton,
      OpenBCIGanglion,
      NeurosityCrown
   };

   enum class SyntheticPreset : int
   {
      CalmAlpha = 0,
      DeepFocus,
      ThetaDream,
      MicroGesturePerf
   };

   enum class GestureKind : int
   {
      None = 0,
      Blink,
      WinkLeft,
      WinkRight,
      JawLeft,
      JawRight,
      TonguePress,
      FocusSurge,
      CalmDrop
   };

   enum class BlinkMode : int
   {
      IntentionalOrDouble = 0,
      AllBlinks = 1
   };

   enum class CalibrationPhase : int
   {
      Idle = 0,
      EyesOpen,
      EyesClosed,
      Blinks, // "blink 5 times": sets the person's blink size, width and polarity
      Complete
   };

   struct SampleFrame
   {
      float ch[kChannelCount] = { 0.0f, 0.0f, 0.0f, 0.0f }; // uV: AF7, AF8, TP9, TP10
      float ppg = 0.0f;
      float imu[3] = { 0.0f, 0.0f, 0.0f }; // gyro rad/s
      double timestampSec = 0.0;
   };

   struct CalibrationProfile
   {
      bool valid = false;
      float iafHz = 10.0f;
      float chanMean[kChannelCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
      float chanMad[kChannelCount] = { 12.0f, 12.0f, 12.0f, 12.0f };
      float medianBlinkUv = 135.0f;
      // Prompted-blink calibration (CalibrationPhase::Blinks). When valid, medianBlinkUv is the median
      // cued blink and the blink polarity is fixed instead of voted.
      bool blinkValid = false;
      float blinkSign = 1.0f;
      float blinkWidthMs = 150.0f;
      float medianJawUv = 42.0f;
      float baselineArousal = 0.0f;
      float baselineAlpha = 0.35f;
      float baselineTheta = 0.25f;
      float baselineBeta = 0.25f;
      // Shrinkage-LDA weights fitted at calibration (eyes closed vs open), see FitStateWeights.
      bool stateValid = false;
      float stateW[kStateFeatureCount] = {};
      float stateBias = 0.0f;
      float stateScale = 1.0f;
      // Eyes-open baselines for the direct pins (median / MAD of the open phase).
      bool directValid = false;
      float imgMed[kImageColumnSize] = {};
      float imgMad[kImageColumnSize] = {};
      float audAmp[kChannelCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
   };

   struct TelemetrySnapshot
   {
      float contactQuality[kChannelCount] = { 1.0f, 1.0f, 1.0f, 1.0f }; // TP9, AF7, AF8, TP10 order in UI or AF7,AF8,TP9,TP10
      float traceFrontal[kTraceSamples] = {};
      float traceTemporal[kTraceSamples] = {};
      float bandPower[kBandCount] = {}; // 1/f-detrended normalized 0..1 (delta, theta, alpha, beta, gamma)
      float aperiodicSlope = 1.5f;
      float aperiodicOffset = 1.0f;
      float iafHz = 10.0f;
      float regionAlpha[kChannelCount] = {}; // 8-12 Hz share of 2-40 Hz power, AF7 AF8 TP9 TP10
      float eyesClosedScore = 0.0f;          // calibrated 0..1 (0.5 = boundary); 0 until calibrated
      bool stateCalibrated = false;
      bool stateCalibFailed = false;         // last calibration ended without a fit (too few clean updates)
      float imageCol[kImageColumnSize] = {}; // latest Image pin column, 0..1 (z -3..+3), bin 0 = 1 Hz
      float imageState = -1.0f;              // eyes-closed score for the strip, -1 until calibrated
      unsigned imageSeq = 0;                 // +1 per column (every 12 samples)
      float alphaPhaseRad = 0.0f;
      float latent[4] = { 0.5f, 0.5f, 0.5f, 0.5f }; // Arousal, Valence, Entropy, Coherence
      float blinkFlash = 0.0f;
      float winkFlash = 0.0f;
      float jawLFlash = 0.0f;
      float jawRFlash = 0.0f;
      bool artifactBlanked = false;
      bool startleMasked = false;
      bool calibrating = false;
      float calibProgress = 0.0f;
      CalibrationPhase calibPhase = CalibrationPhase::Idle;
      double sampleRateHz = kSampleRateHz;
      bool packMissing = false;
      char statusText[96] = "synthetic cortex ready";
   };

   // Lock-free global `brain.*` registry shared across BrainSourceNode, Expression, and Field backends.
   int BrainGlobalCount();
   const char* BrainGlobalName(int id);
   int FindBrainGlobalId(const std::string& field);
   bool IsBrainGlobalField(const std::string& field);

   // Updates the lock-free `brain.*` values from a 24-pin BrainSourceNode output vector.
   void PublishBrainGlobals(const float* vals24, bool active);
   float ReadBrainGlobal(int id);
   float ReadBrainGlobal(const std::string& fieldOrPrefixed);
   void PopulateBrainGlobalsMap(std::map<std::string, float>& out);

   // Reads the 6D neural sculpting latent vector [Arousal, Valence, Entropy, Coherence, BrowTension, JawMax].
   void ReadBrainLatent6(float out6[6]);

   // Gap 3: Efference-Copy Startle Mask notification (main/audio transient).
   void NotifyOutputTransient(float audioRmsDelta, float lumaDelta);
   double LastOutputTransientSec();
}
