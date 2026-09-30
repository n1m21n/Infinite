#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio/dsp/GrainMolderDsp.h"
#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioGrainMolderNode;
namespace Platform
{
   struct SampleBuffer;
}

// Visual snapshot of active voices for multi-note polyphonic playhead rendering.
struct GrainMolderVoiceSnapshot
{
   static constexpr int kMaxVisualVoices = 16;
   struct Item
   {
      float position = 0.0f; // 0..1 normalized position in sample buffer
      float amp = 1.0f;      // 0..1 current envelope amplitude for visual decay
      int note = 60;         // MIDI note number
   };
   Item voices[kMaxVisualVoices];
   int count = 0;
   float selfPos = -1.0f;    // Position of self/audition voice (0..1) if active, else -1.0f
   float selfAmp = 0.0f;     // Self voice amplitude (0..1)
   bool selfActive = false;
};

// Grain Molder: time-domain grain sorting and continuous scrambling synth.
// Slices audio into overlapping grains, evaluates per-grain metric (Level,
// Brightness, Random), and rearranges them based on a continuous blend between
// original temporal position and metric rank.
class GrainMolderNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new GrainMolderNode(); }
   GrainMolderNode();
   ~GrainMolderNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &audioInput : nullptr; }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "notes" : slot == 1 ? "record in" : nullptr;
   }

   bool LoadFile(const std::string& path);
   void ReloadFromPath();

   void StartRecording();
   void StopRecording();
   bool IsRecording() const { return mRecording; }
   bool RequiresAudioProcessing() const override { return IsRecording(); }

   void TriggerPreview(float frac);
   void StopPreview();
   bool IsPlaying() const { return mIsPlaying; }
   bool NotesSounding() const { return mNotesSounding; }
   int ActiveNoteCount() const { return mActiveNoteCount; }
   bool IsAuditioning() const { return mIsPlaying && mSelfOwnedByUser; }

   const std::string& FilePath() const { return mFilePath; }
   const std::string& FileName() const { return mFileName; }
   const std::string& Status() const { return mStatus; }
   bool IsRendering() const { return mWorking; }
   float Playhead() const { return mPlayhead; }
   const GrainMolderVoiceSnapshot& VisualSnapshot() const { return mLatestVisualSnapshot; }

   static constexpr int kWaveformCacheSize = 256;
   float waveformMin[kWaveformCacheSize] = {};
   float waveformMax[kWaveformCacheSize] = {};
   int waveformCacheCount = 0;

   // ---- Exposed controls ------------------------------------------------
   float grain = 100.0f;    // 10..500 ms
   float amount = 0.0f;     // 0..1 (0 = original, 1 = fully sorted)
   int key = 0;             // 0 = Level, 1 = Bright, 2 = Random
   bool descending = false; // sort direction (asc/desc)
   int seed = 1;            // random seed
   float level = 0.8f;      // 0..2 output level
   float pitch = 0.0f;      // -24..24 semitones
   float decay = 2.0f;      // 0.05..10 s envelope release

   // ---- Playback / Transport --------------------------------------------
   float start = 0.0f;      // 0..1
   float end = 1.0f;        // 0..1
   float position = 0.0f;   // 0..1 playback start anchor in [start, end]
   bool loop = false;
   bool reverse = false;
   bool pingpong = false;

   NoteCable noteInput;
   AudioCable audioInput;

private:
   enum class Job
   {
      None,
      NewSource,  // LoadFile / StopRecording
      ReProcess,  // Param changes
   };

   void LaunchJob(Job job, std::vector<float> sourceOverride = {}, double sourceOverrideSR = 0.0);
   void JoinWorkerIfDone();
   void RebuildWaveformCache(const std::vector<float>& mono);

   std::unique_ptr<AudioGrainMolderNode> mAudioNode;
   int mLastCookFrame = -1;

   std::string mFilePath;
   std::string mFileName;
   std::string mStatus = "no sample loaded";
   bool mRecording = false;
   bool mIsPlaying = false;
   bool mNotesSounding = false;
   int mActiveNoteCount = 0;
   bool mSelfOwnedByUser = false;
   float mPlayhead = 0.0f;
   GrainMolderVoiceSnapshot mLatestVisualSnapshot;

   // Source audio data
   std::vector<float> mSourceMono;
   std::vector<float> mSourceRight;
   double mSourceSR = 44100.0;
   bool mSourceIsStereo = false;

   // Worker thread management
   std::thread mWorkerThread;
   std::atomic<bool> mWorking { false };
   std::atomic<bool> mAbort { false };
   std::atomic<bool> mResultReady { false };
   Job mCurrentJob = Job::None;

   struct PendingResult
   {
      std::vector<float> renderedL;
      std::vector<float> renderedR;
      double renderedSR = 44100.0;
      bool isStereo = false;
      bool isNewSource = false;
   };
   PendingResult mPendingResult;

   // Debounced dirty-check snapshot
   struct DispatchSnapshot
   {
      float grain = -1.0f;
      float amount = -1.0f;
      int key = -1;
      bool descending = false;
      int seed = -1;
      bool operator==(const DispatchSnapshot& o) const
      {
         return grain == o.grain && amount == o.amount && key == o.key &&
                descending == o.descending && seed == o.seed;
      }
   };
   DispatchSnapshot mLastDispatched;
   int mCooldownFrames = 0;
};
