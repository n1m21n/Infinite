#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "INode.h"
#include "Modulation.h"
#include "core/bci/BciTypes.h"

namespace Bci { class BciDspEngine; class BciRuntime; }
class BrainAudioNode;

// --- Brain Source (BCI & Micro-Muscular Neural Interface) -----------------
// Acquires 4-channel EEG/EOG/sEMG (AF7, AF8, TP9, TP10) + PPG/IMU via BrainFlow
// ("bci" extension pack) or the built-in biophysical SyntheticCortex, separates
// fast somatic micro-movements (<40 ms) from 1/f^chi-detrended cortical waves,
// and publishes 24 lock-free 0..1 modulator outputs plus `brain.*` Field symbols.
// Direct pins (Block B, docs/plans/bci/STRATEGY.md): output 24 "image" = calibrated eyes-closed strip
// over a 4-row 1-40 Hz spectrogram; output 25 "audio" = the 4 channels audified (frequency x 32).
class BrainSourceNode : public INode, public IAudioSource
{
public:
   enum Output
   {
      // --- 0. Master Safety & Contact Confidence ---
      kPresence = 0,

      // --- 1. 4-Axis Cortical Latent Manifold ---
      kArousal,
      kValence,
      kEntropy,
      kCoherence,

      // --- 2. Perceptual & Brain-Native Verbs ---
      kDensity,
      kSmoothness,
      kMotion,
      kSpace,
      kTension,
      kDrift,
      kSurge,
      kStillness,

      // --- 3. 1/f-Detrended Cortical Bands ---
      kDelta,
      kTheta,
      kAlpha,
      kBeta,
      kGamma,

      // --- 4. Micro-Muscular (sEMG/EOG) & Autonomic Actuators ---
      kBlink,
      kGazeLR,
      kBrowTension,
      kJawLeft,
      kJawRight,
      kHeartPulse,

      kOutputCount, // 24 modulator pins (indices are saved in patches: never reorder)

      kImagePin = kOutputCount,
      kAudioPin,
      kTotalOutputs
   };

   // Image pin layout: 256 columns (12 s at 21.3 columns/s), state strip + 4 rows of 40 bins.
   static constexpr int kImgW = 256;
   static constexpr int kImgStrip = 12;
   static constexpr int kImgGap = 2;
   static constexpr int kImgH = kImgStrip + 4 * (kImgGap + Bci::kImageBins);

   enum SourceDevice
   {
      kDeviceSynthetic = 0,
      kDeviceCsvReplay,
      kDeviceMuse2,
      kDeviceMuseS,
      kDeviceOpenBCICyton,
      kDeviceOpenBCIGanglion,
      kDeviceNeurosityCrown
   };

   static const std::vector<std::string>& DeviceNames();
   static const std::vector<std::string>& PresetNames();
   static const std::vector<std::string>& BlinkModeNames();

   static INode* Create() { return new BrainSourceNode(); }
   BrainSourceNode();
   ~BrainSourceNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   unsigned int GetOutputTexture(int index) override { return index == kImagePin ? mTexture : 0; }
   int GetOutputWidth() const override { return kImgW; }
   int GetOutputHeight() const override { return kImgH; }
   unsigned long long TextureRevision() const override { return mTexRevision; }
   void CookIfNeeded(int frameId) override;

   AudioNode* GetAudioNode() override;
   bool IsAudioOutputIndex(int index) const override { return index == kAudioPin; }

   int OutputCount() const override { return kTotalOutputs; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;
   float Value(int index) const;

   // UI & Test Actions
   void StartWorker();
   void StopWorker();
   void StartCalibration();
   void CancelCalibration();
   void TriggerSyntheticGesture(Bci::GestureKind kind);
   void TriggerSyntheticVegetativeBlink();

   // Gap 4: Transport-Locked Neural Take Baking (16 samples/beat, 24 channels)
   void StartRecordingTake();
   void StopRecordingTake();
   void ClearTake();
   bool IsRecordingTake() const { return mRecordingTake; }
   bool HasTake() const { return !mTakeFrames.empty(); }
   int TakeFrameCount() const { return (int)mTakeFrames.size(); }

   void GetTelemetry(Bci::TelemetrySnapshot& out) const;
   // Copy of the Image pin's RGBA pixels (kImgW x kImgH, row 0 = top); for tests and tools.
   void CopyImagePixels(std::vector<uint8_t>& out) const;
   // What the worker does with each processed batch; public so the selftest can drive the pins.
   void FeedDirect(const Bci::TelemetrySnapshot& snap, unsigned& lastImgSeq, const std::vector<Bci::AudifyFrame>& frames,
                   double eegRateHz);
   std::string Status() const;
   bool PackMissing() const { return mPackMissing.load(std::memory_order_relaxed); }

   // Serializable parameters
   int device = kDeviceSynthetic;
   int syntheticPreset = 0; // 0=Calm Alpha, 1=Deep Focus, 2=Theta Dream, 3=Micro-Gesture Perf
   int blinkMode = 0;       // 0=Intentional/Double, 1=All Blinks
   float smoothing = 0.45f; // 0..1 One-Euro adaptive cutoff
   float holdMs = 300.0f;   // Contact drop hold before decaying
   bool artifactBlanking = true;
   bool startleGuard = true;
   bool playTake = false;
   std::string serialPort;
   std::string csvPath;

   void VisitParams(ParamVisitor& v) override;

private:
   struct Tap : public IModulator
   {
      BrainSourceNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };

   void WorkerLoop();
   void RecordTakeStep(double beats);
   float SampleTake(int index, double beats) const;

   Tap mTaps[kOutputCount];
   std::atomic<float> mPublished[kOutputCount]{};
   std::atomic<bool> mPackMissing{ false };

   // Worker thread & config handoff
   std::thread mThread;
   std::mutex mMu;
   std::condition_variable mCv;
   bool mStop = false;
   bool mReopenRequested = false;
   bool mCalibStartRequested = false;
   bool mCalibCancelRequested = false;

   int mActiveDevice = -1;
   int mActivePreset = -1;
   std::string mActiveSerial;
   std::string mActiveCsv;

   std::vector<std::pair<Bci::GestureKind, bool>> mPendingGestures; // bool = vegetative

   mutable std::mutex mTelemetryMu;
   Bci::TelemetrySnapshot mTelemetry;
   std::string mStatus = "synthetic cortex ready";

   int mLastCookFrame = -1;

   // Image pin: worker writes columns into mPixels under mPixMu, the GL thread uploads on change.
   void AppendImageColumn(const Bci::TelemetrySnapshot& snap);
   mutable std::mutex mPixMu;
   std::vector<uint8_t> mPixels;       // kImgW * kImgH * 4, newest column on the right
   unsigned long long mPixRevision = 0; // guarded by mPixMu
   unsigned long long mUploadedRevision = 0;
   unsigned long long mTexRevision = 1;
   unsigned int mTexture = 0;

   // Audio pin: worker pushes audified EEG frames, the audio thread plays them.
   std::unique_ptr<BrainAudioNode> mAudio;

   // Transport-locked take buffer: 24 bytes per sample (quantized 0..255), 16 samples/beat
   static constexpr int kTakeSamplesPerBeat = 16;
   static constexpr int kTakeMaxSteps = 4096;
   bool mRecordingTake = false;
   double mTakeRecStartBeats = 0.0;
   std::vector<uint8_t> mTakeFrames; // size = N * kOutputCount
};
