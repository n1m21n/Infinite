#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio/dsp/BeatArranger.h"
#include "audio/dsp/DrumClassifier.h"
#include "core/AudioCable.h"
#include "core/INode.h"

class AudioBeatArrangerNode;
namespace Platform
{
   struct SampleBuffer;
}

// Visual snapshot of the arranger's active playhead and sounding voices, for
// the waveform view. Published by the audio thread into a triple buffer and
// read once per CookIfNeeded.
struct BeatArrangerVisualSnapshot
{
   static constexpr int kMaxVisualVoices = 8;
   struct Voice
   {
      int slice = 0;
      float position = 0.0f; // 0..1 position in the source buffer
      float amp = 0.0f;      // 0..1 current amplitude
   };
   Voice voices[kMaxVisualVoices];
   int voiceCount = 0;
   float playheadStep = 0.0f; // fractional step in the current pattern
   int totalSteps = 32;
};

// Beat Arranger v2: single-source-file model. Drop ONE sample, the node
// chops it at transients (SlicerDsp), classifies every slice into one of
// DrumClassifier's 10 classes, and "Generate" lays the slices out as a
// seeded groove (BeatArranger namespace) that plays free-running from
// Transport. Dropping a second file replaces the first; a multi-file drop
// loads only the first ("1 of N loaded").
class BeatArrangerNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new BeatArrangerNode(); }
   BeatArrangerNode();
   ~BeatArrangerNode() override;

   static constexpr int kMaxSlices = 64;
   static constexpr int kWaveCache = 256;

   // timeSig dropdown: index 0 = "transport" (Transport's own numerator/
   // denominator), 1..5 = fixed 4/4, 3/4, 6/8, 5/4, 7/8. Append-only - the
   // index is a saved param.
   static constexpr int kNumTimeSigs = 6;
   static const char* const kTimeSigNames[kNumTimeSigs];

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   AudioNode* GetAudioNode() override;

   int OutputCount() const override { return 1; }
   const char* OutputLabel(int index) const override { return index == 0 ? "out" : nullptr; }

   // Free-running from Transport - no note or audio inputs.
   NoteCable* NoteInputSlot(int) override { return nullptr; }
   AudioCable* AudioInputSlot(int) override { return nullptr; }
   const char* InputLabel(int) const override { return nullptr; }

   bool LoadFile(const std::string& path);
   void ReloadFromPath();

   // Re-runs the seeded groove generator ("Generate" button). A no-op with
   // nothing loaded or nothing classified yet.
   void Generate();
   bool HasBeat() const { return !mArrangedHits.empty(); }
   int HitCount() const { return (int)mArrangedHits.size(); }
   const std::vector<BeatArranger::ArrangedHit>& ArrangedHits() const { return mArrangedHits; }
   bool IsAnalyzing() const { return mWorking.load(std::memory_order_relaxed); }

   const std::string& FilePath() const { return mFilePath; }
   const std::string& FileName() const { return mFileName; }
   const std::string& Status() const { return mStatus; }

   const std::vector<BeatArranger::SliceInfo>& Slices() const { return mSlices; }
   int SliceCount() const { return (int)mSlices.size(); }
   // Right-click override of a slice's detected class (main thread only,
   // pushed to the audio thread and included in the next Generate()).
   void SetSliceClassOverride(int slice, DrumClassifier::DrumClass cls);

   const BeatArrangerVisualSnapshot& VisualSnapshot() const { return mVisualSnapshot; }

   static constexpr int kWaveformCacheSize = 256;
   float waveformMin[kWaveformCacheSize] = {};
   float waveformMax[kWaveformCacheSize] = {};
   int waveformCacheCount = 0;

   // ---- Params (7 sliders, KHS-simple; matches DrawBeatArrangerBody's draw
   // order = modulation pin order) ----
   int timeSig = 1;          // index into kTimeSigNames, default "4/4"
   float swing = 0.0f;       // 0..1, audio-thread scheduling param
   float randPitch = 0.0f;   // 0..1
   float speed = 1.0f;       // 0.25..4
   float transient = 0.5f;   // 0..1
   float decay = 0.5f;       // 0..1
   float output = 0.8f;      // 0..1

   uint32_t seed = 42;

   void JoinWorkerIfDone();

private:
   void FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                     const std::string& filePath, const std::string& status);
   void LaunchAnalysis();
   void PushSlicesToAudio();
   void PushHitsToAudio();
   std::string SerializeSlices() const;
   void DeserializeSlices(const std::string& blob);

   std::unique_ptr<AudioBeatArrangerNode> mAudioNode;
   int mLastCookFrame = -1;

   std::string mFilePath;
   std::string mFileName;
   std::string mStatus = "no sample loaded";
   std::string mSliceBlob;  // persisted per-slice class/bounds - see VisitParams
   std::string mHitBlob;    // persisted arranged hits

   std::vector<BeatArranger::SliceInfo> mSlices;
   std::vector<BeatArranger::ArrangedHit> mArrangedHits;

   // Main-thread-only mono copy for (re-)analysis, kept separate from the
   // audio thread's own buffer copy so a re-classify never touches it.
   std::vector<float> mSourceMono;
   double mSourceSR = 44100.0;
   int mSourceFrames = 0;

   std::thread mWorkerThread;
   std::atomic<bool> mWorking { false };
   std::atomic<bool> mAbort { false };
   std::atomic<bool> mResultReady { false };

   struct PendingResult
   {
      std::vector<int> onsetFrames;
      std::vector<DrumClassifier::Result> classified;
   };
   PendingResult mPendingResult;

   // Dirty-tracking shadows: compared BEFORE being updated, every cook (fix
   // Section7#1 - a shadow updated before the comparison silences the param
   // forever).
   float mLastSwing = -1.0f;
   float mLastRandPitch = -1.0f;
   float mLastSpeed = -1.0f;
   float mLastTransient = -1.0f;
   float mLastDecay = -1.0f;
   float mLastOutput = -1.0f;
   int mLastTimeSig = -1;
   bool mFirstCook = true;

   BeatArrangerVisualSnapshot mVisualSnapshot;
};
