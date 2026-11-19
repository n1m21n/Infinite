#pragma once

#include "BciTypes.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Bci
{
   // Biophysical 4-channel EEG + EOG + sEMG + PPG/IMU simulator (256 Hz) and CSV replay source.
   // Generates:
   //   - 1/f^chi aperiodic background per channel (AF7, AF8, TP9, TP10)
   //   - Coupled Kuramoto-style cortical oscillators (delta, theta, alpha, beta, gamma)
   //   - Symmetric corneo-retinal blink dipoles (vegetative vs. intentional)
   //   - Anti-phase horizontal gaze saccade / unilateral wink dipoles
   //   - Frontalis/Corrugator brow (30-75 Hz) and Temporalis/Masseter jaw (40-110 Hz) MUAP bursts
   class SyntheticCortex
   {
   public:
      SyntheticCortex();

      void Reset(uint32_t seed = 0x494E4642u);
      void SetPreset(SyntheticPreset preset);
      SyntheticPreset Preset() const { return mPreset; }

      // Injects a deliberate micro-muscular or cortical event (thread-safe).
      void TriggerGesture(GestureKind kind);
      // Injects a brief spontaneous vegetative blink (125 ms, ~130 uV) for Gap 1 testing.
      void TriggerVegetativeBlink();
      // Enables or disables automatic periodic micro-gestures in MicroGesturePerf preset.
      void SetAutoDemoGestures(bool enabled) { mAutoDemo = enabled; }
      // Allows deterministic unit tests to disable natural random vegetative blinks.
      void SetSpontaneousBlinks(bool enabled) { mSpontaneousBlinks = enabled; }

      // CSV replay support (BrainFlow TSV/CSV or 4-column microvolt CSV).
      bool LoadCsv(const std::string& path, std::string& outErr);
      void ClearCsv();
      bool HasCsv() const { return !mCsvFrames.empty(); }

      // Generates `count` frames at `sampleRateHz` into `outFrames`.
      void Generate(int count, double sampleRateHz, std::vector<SampleFrame>& outFrames);

   private:
      struct PinkState
      {
         float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
         float Step(float white, float slopeChi);
      };

      struct ActiveGesture
      {
         GestureKind kind = GestureKind::None;
         bool vegetative = false;
         float elapsedSec = 0.0f;
         float durationSec = 0.25f;
      };

      float NextUniform();
      float NextGaussian();

      uint32_t mRng = 0x494E4642u;
      double mTimeSec = 0.0;
      double mAutoTimerSec = 0.0;
      double mSpontBlinkTimerSec = 3.2;
      int mAutoStep = 0;

      SyntheticPreset mPreset = SyntheticPreset::CalmAlpha;
      bool mAutoDemo = true;
      bool mSpontaneousBlinks = true;

      PinkState mPink[kChannelCount];
      double mPhase[kBandCount][kChannelCount] = {};
      float mFocusBoost = 0.0f;
      float mCalmBoost = 0.0f;

      std::mutex mGestureMu;
      std::vector<ActiveGesture> mGestures;

      // CSV replay buffer
      std::vector<SampleFrame> mCsvFrames;
      size_t mCsvCursor = 0;
   };
}
