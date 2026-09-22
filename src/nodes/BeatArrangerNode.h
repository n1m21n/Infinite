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

// Visual snapshot of the arranger's active playhead and sounding voices,
// for the main arrangement view and strip waveforms. Published by the
// audio thread into a triple buffer and read once per CookIfNeeded.
struct BeatArrangerVisualSnapshot
{
   static constexpr int kMaxVisualVoices = 16;
   struct Voice
   {
      int sample = 0;        // 0..7 strip index
      int slice = 0;         // slice index
      float position = 0.0f; // 0..1 position in sample buffer
      float amp = 0.0f;      // 0..1 current amplitude
   };
   Voice voices[kMaxVisualVoices];
   int voiceCount = 0;
   float playheadStep = 0.0f; // fractional step in current pattern
   int totalSteps = 32;       // 16 * bars
};

// Beat Arranger node:
// Drop up to 8 samples, the node chops them at transients using SlicerDsp,
// classifies every slice (kick / bass / snare / clap / closed hat / open hat / perc)
// using pure-DSP DrumClassifier, and lays the slices out as a groove.
// `arrange` builds it, `re-arrange` rolls a new one (seed + 1).
class BeatArrangerNode : public INode, public IAudioSource
{
public:
   static constexpr int kNumStrips = 8;
   static constexpr int kWaveCache = 128;

   static INode* Create() { return new BeatArrangerNode(); }
   BeatArrangerNode();
   ~BeatArrangerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   AudioNode* GetAudioNode() override;

   // 1 main stereo mix + 8 per-sample individual stereo outs (matching DrumSequencer)
   int OutputCount() const override { return 1 + kNumStrips; }
   const char* OutputLabel(int index) const override
   {
      static const char* kLabels[1 + kNumStrips] = {
         "out", "1", "2", "3", "4", "5", "6", "7", "8"
      };
      return (index >= 0 && index < 1 + kNumStrips) ? kLabels[index] : nullptr;
   }
   int AudioOutputSlotForPin(int pinIndex) const override { return pinIndex; }

   // No note or audio inputs - free-running from Transport
   NoteCable* NoteInputSlot(int) override { return nullptr; }
   AudioCable* AudioInputSlot(int) override { return nullptr; }
   const char* InputLabel(int) const override { return nullptr; }

   bool LoadFileToStrip(int strip, const std::string& path);
   void ReloadFromPaths();
   void ClearStrip(int strip);

   void Arrange();
   void ReArrange();
   void ClearGroove();

   bool HasGroove() const { return !mArrangedHits.empty(); }
   int LoadedStripCount() const;
   const std::string& FilePath(int strip) const { return stripFilePath[ClampStrip(strip)]; }
   const std::string& FileName(int strip) const { return stripFileName[ClampStrip(strip)]; }
   const std::string& StripStatus(int strip) const { return stripStatus[ClampStrip(strip)]; }

   // Main thread visual snapshot accessor
   const BeatArrangerVisualSnapshot& VisualSnapshot() const { return mVisualSnapshot; }

   // ---- Global parameters -----------------------------------------------
   int seed = 42;             // 0..9999
   float randPitch = 0.0f;    // 0..1
   float globalTransient = 0.0f; // -1..1
   float globalDecay = 0.0f;     // -1..1
   float globalSpeed = 1.0f;     // 0.25..4
   int bars = 2;              // 1, 2, or 4
   int rate = 12;             // MusicTime::kSixteenth (12)
   float swing = 0.0f;        // 0..1
   float volume = 0.8f;       // 0..1

   // ---- Per-strip parameters (8 strips) ---------------------------------
   float stripVolume[kNumStrips];
   float stripPan[kNumStrips];
   float stripPitch[kNumStrips];
   float stripFineTune[kNumStrips]; // cents +/-50
   float stripSpeed[kNumStrips];    // 0.25..4
   float stripTransient[kNumStrips]; // -1..1
   float stripDecay[kNumStrips];     // -1..1
   bool stripMute[kNumStrips];
   bool stripSolo[kNumStrips];
   int stripClassOverride[kNumStrips]; // 0 = Auto, 1 = Kick, 2 = Bass, ...

   // Waveform thumbnail cache per strip
   float stripWaveMin[kNumStrips][kWaveCache] = {};
   float stripWaveMax[kNumStrips][kWaveCache] = {};
   int stripWaveCount[kNumStrips] = {};

   // Slice information per strip
   std::vector<int> stripOnsets[kNumStrips];
   std::vector<DrumClassifier::Result> stripSlices[kNumStrips];
   double stripSampleLenSec[kNumStrips] = {};

   // Arranged hits and serialized blob
   std::vector<BeatArranger::ArrangedHit> mArrangedHits;
   std::string mHitBlob;

   // Cached canvas-space bounding boxes for drag & drop hit testing
   float stripCardCanvasX0[kNumStrips] = {};
   float stripCardCanvasY0[kNumStrips] = {};
   float stripCardCanvasX1[kNumStrips] = {};
   float stripCardCanvasY1[kNumStrips] = {};

   float timelineCanvasX0 = 0.0f;
   float timelineCanvasY0 = 0.0f;
   float timelineCanvasX1 = 0.0f;
   float timelineCanvasY1 = 0.0f;

   // Thread management for background transient/classifier analysis
   void JoinWorkerIfDone(int strip);

private:
   static int ClampStrip(int strip) { return std::clamp(strip, 0, kNumStrips - 1); }
   void FinishStripBuffer(int strip, Platform::SampleBuffer* buf);

   std::unique_ptr<AudioBeatArrangerNode> mAudioNode;

   std::string stripFilePath[kNumStrips];
   std::string stripFileName[kNumStrips];
   std::string stripStatus[kNumStrips];
   Platform::SampleBuffer* mLoadedBuffers[kNumStrips] = {};

   // Worker thread state per strip
   std::unique_ptr<std::thread> mWorkerThreads[kNumStrips];
   std::atomic<bool> mWorkerAbort[kNumStrips] { false, false, false, false, false, false, false, false };
   std::atomic<bool> mWorkerDone[kNumStrips] { false, false, false, false, false, false, false, false };

   struct WorkerResult
   {
      std::vector<int> onsets;
      std::vector<DrumClassifier::Result> slices;
   };
   WorkerResult mWorkerResults[kNumStrips];

   // Dirty tracking shadows
   float mLastStripVolume[kNumStrips] = {};
   float mLastStripPan[kNumStrips] = {};
   float mLastStripPitch[kNumStrips] = {};
   float mLastStripFineTune[kNumStrips] = {};
   float mLastStripSpeed[kNumStrips] = {};
   float mLastStripTransient[kNumStrips] = {};
   float mLastStripDecay[kNumStrips] = {};
   float mLastGlobalTransient = 0.0f;
   float mLastGlobalDecay = 0.0f;
   float mLastGlobalSpeed = 1.0f;
   float mLastVolume = 0.8f;
   int mLastRate = 12;
   int mLastBars = 2;
   float mLastSwing = 0.0f;
   bool mFirstCook = true;

   BeatArrangerVisualSnapshot mVisualSnapshot;
};
