#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioSlicerNode;
namespace Platform
{
   struct SampleBuffer;
}

// Grid-mode divisions. The names are the dropdown's entries and their index
// is the saved `division` value, so entries may only ever be appended - a
// reorder silently rewrites every saved patch's division (pillar P7's rule,
// applied to a non-filter list).
constexpr int kSlicerNumDivisions = 6;
extern const char* const kSlicerDivisionNames[kSlicerNumDivisions];
extern const float kSlicerDivisionDenoms[kSlicerNumDivisions];
extern const bool kSlicerDivisionTriplet[kSlicerNumDivisions];

// Visual snapshot of the slicer's active voices, for the waveform's
// playheads. Published by the audio thread into a triple buffer and read
// once per CookIfNeeded, the same shape SamplerVoiceSnapshot uses.
struct SlicerVoiceSnapshot
{
   static constexpr int kMaxVisualVoices = 8;
   struct Item
   {
      float position = 0.0f; // 0..1 normalized position in the sample buffer
      float amp = 0.0f;      // 0..1 current amplitude, for the fade-out trail
      int slice = 0;         // which slice this voice is playing
   };
   Item voices[kMaxVisualVoices];
   int count = 0;
};

// A slicer: load or record a sample, chop it into slices - either at detected
// transients or on a tempo grid - and play each slice from a MIDI keyboard,
// chromatically ascending from C1 (note 36 = slice 0).
//
// The two modes differ only in where the slice boundaries come from:
//
//   onsets - a background worker runs SlicerDsp::Detect over the sample
//            (log-magnitude spectral flux with a SuperFlux max-filtered
//            reference frame, Dixon-style adaptive peak picking). The
//            `sensitivity` param is the detection threshold and is the ONLY
//            control that relaunches that worker; `onsets` is a pure
//            main-thread top-N-by-strength prune of the cached candidate
//            list, and `slice by`/`division` recompute boundaries with no
//            analysis at all.
//
//   grid   - boundaries are arithmetic from the global transport tempo
//            (Transport::Instance().Tempo()) and the chosen division. There
//            is deliberately no per-node bpm param.
//
// A note at or above 36 + sliceCount is silent: it does not wrap, clamp, or
// fall back to slice 0.
//
// Two separate controls decide how long a slice lasts, and they must not be
// conflated (an earlier build did, in a single Voice::infinite flag):
//
//   crossthrough (bool) owns the BOUNDARY - whether playback is allowed to
//                       run past the slice's own next onset. Off by default:
//                       each slice stops where the next one begins.
//   decay (float ms)    owns the ENVELOPE only - how fast the amplitude
//                       falls after the attack ramp. At the top of its throw
//                       it is a no-decay detent (the slice holds at full
//                       level); that says nothing about where it stops.
//
// The four combinations:
//   1. cross off + no decay   - attack ramp, full level, a 3 ms raised-cosine
//                               fade completing exactly at the next onset.
//                               The classic tight chop; the shipped default.
//   2. cross off + decay      - ends at whichever comes first: the envelope
//                               falling under 1e-4, or the next onset.
//   3. cross on + no decay    - full level through every later slice to the
//                               end of the sample. The only case a note-off
//                               can shorten.
//   4. cross on + decay       - a one-shot with a tail over the rest of the
//                               break.
//
// Confinement is expressed in read-head POSITION, not wall-clock, so it
// stretches correctly with `speed`: at speed 0.5 the boundary arrives in
// twice the wall-clock time, and no audio past the boundary is ever read.
class SlicerNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new SlicerNode(); }
   SlicerNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~SlicerNode() override;

   static constexpr int kMaxSlices = 64;
   static constexpr int kBaseNote = 36; // C1 plays slice 0

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   // Slot 1, not 0: audio and note pins share one slot index space, so the
   // record input lands one past the note pin (see SamplerNode.h).
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

   // While recording, ProcessBlock must run every block to capture
   // audioInput even with no downstream cable and no note input.
   bool RequiresAudioProcessing() const override { return IsRecording(); }

   // Re-runs onset detection from scratch with the current settings - the
   // "re-slice" button. A no-op with nothing loaded.
   void ReSlice();

   // Auditions slice `index` immediately, independent of any note cable and
   // the transport. index < 0 auditions the whole buffer.
   void TriggerSlicePreview(int index);
   void StopPreview();
   bool IsPlaying() const { return mIsPlaying; }
   bool IsAnalyzing() const { return mWorking.load(std::memory_order_relaxed); }

   const std::string& FilePath() const { return mFilePath; }
   const std::string& FileName() const { return mFileName; }
   const std::string& Status() const { return mStatus; }

   // Effective slice start fractions (0..1), ascending, always beginning at
   // 0. Main thread only - this is what the waveform draws and what the
   // audio thread was last handed.
   const std::vector<float>& Slices() const { return mSliceFrac; }
   int SliceCount() const { return (int)mSliceFrac.size(); }

   // Moves slice marker `index` (>0; marker 0 is pinned at the sample's
   // start) to `frac`, keeping the list ascending. Onsets mode only - grid
   // boundaries are derived, so a drag there would be silently undone on the
   // next recompute.
   void MoveSliceMarker(int index, float frac);
   bool MarkersAreEditable() const { return sliceBy == 0; }

   static constexpr int kWaveformCacheSize = 256;
   float waveformMin[kWaveformCacheSize] = {};
   float waveformMax[kWaveformCacheSize] = {};
   int waveformCacheCount = 0;

   const SlicerVoiceSnapshot& VisualSnapshot() const { return mLatestVisualSnapshot; }

   // --- params, in DrawSlicerBody's draw order (= modulation pin order) ----
   int sliceBy = 0;         // 0 = onsets, 1 = grid
   int onsets = 16;         // 1..64, top-N cap on detected candidates
   int division = 3;        // index into kSlicerDivisions (1/4,1/8,1/8T,1/16,1/16T,1/32)
   float sensitivity = 65.0f; // 0..100, onset-detection threshold
   float pitch = 0.0f;      // semitones, +/-24
   float finetune = 0.0f;   // cents, +/-100
   float attack = 0.0f;     // ms, 0..500; per-slice attack ramp
   float decay = 5000.0f;   // ms; the top of the range is the no-decay detent
   float speed = 1.0f;      // 0.25..4 playback rate multiplier
   float volume = 0.8f;     // 0..1
   bool crossthrough = false; // let a slice run past its own next onset

   // At or above this the decay slider is at its no-decay detent: after its
   // attack the slice holds at full level instead of decaying. It says
   // NOTHING about where the slice stops - that is `crossthrough`'s job.
   static constexpr float kDecayInfinite = 4999.0f;

   NoteCable noteInput;
   AudioCable audioInput;

private:
   enum class Job
   {
      None,
      NewSource,  // load / record-stop / re-slice
      ReProcess,  // a sensitivity change
   };

   void FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                     const std::string& filePath, const std::string& status);
   void LaunchJob(Job job);
   void JoinWorkerIfDone();
   // Cheap main-thread recompute of mSliceFrac from either the cached onset
   // candidates (top-N by strength, re-sorted by time) or the tempo grid. No
   // analysis, no worker.
   void RebuildSlices();
   void PushSlicesToAudio();
   std::string SerializeSlices() const;
   void DeserializeSlices(const std::string& blob);

   std::unique_ptr<AudioSlicerNode> mAudioNode;
   int mLastCookFrame = -1;

   std::string mFilePath;
   std::string mFileName;
   std::string mStatus = "no sample loaded";
   std::string mSliceBlob; // persisted markers - see VisitParams
   bool mRecording = false;
   bool mIsPlaying = false;
   SlicerVoiceSnapshot mLatestVisualSnapshot;

   // Source audio kept main-thread-side so a re-analysis never has to touch
   // the audio thread's copy.
   std::vector<float> mSourceMono;
   double mSourceSR = 44100.0;
   int mSourceFrames = 0;

   // Raw detector output: every candidate above the threshold, with its peak
   // strength. `onsets` prunes this; it never re-runs the detector.
   std::vector<float> mCandidateFrac;
   std::vector<float> mCandidateStrength;
   std::vector<float> mSliceFrac;

   std::thread mWorkerThread;
   std::atomic<bool> mWorking { false };
   std::atomic<bool> mAbort { false };
   std::atomic<bool> mResultReady { false };
   Job mCurrentJob = Job::None;

   struct PendingResult
   {
      std::vector<float> frac;
      std::vector<float> strength;
   };
   PendingResult mPendingResult;

   struct DispatchSnapshot
   {
      float sensitivity = -1.0f;
      bool operator==(const DispatchSnapshot& o) const { return sensitivity == o.sensitivity; }
   };
   DispatchSnapshot mLastDispatched;
   int mCooldownFrames = 0;

   // Recompute triggers for RebuildSlices - all main-thread, all cheap.
   int mLastRebuiltSliceBy = -1;
   int mLastRebuiltOnsets = -1;
   int mLastRebuiltDivision = -1;
   float mLastRebuiltTempo = -1.0f;
   int mLastRebuiltSourceFrames = -1;
   bool mSlicesDirty = true;
};
