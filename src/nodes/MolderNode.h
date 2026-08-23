#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio/dsp/MolderDsp.h"
#include "core/AudioCable.h"
#include "core/INode.h"

class AudioMolderNode;
namespace Platform
{
   struct SampleBuffer;
}

// Analysis / genome / additive-resynthesis sample-mangling synth. A source
// sample is decomposed once (worker thread: YIN pitch, STFT, per-frame
// partial tracking, voiced/unvoiced gate, sinusoids-plus-residual split)
// into an Analysis; "roll" mutates a compact Genome (deterministic from a
// seed + generation count, see 4f in the design doc) and re-renders a new
// playable sample from analysis x genome. Rolling again mutates further
// from the current genome, so repeated rolls walk a space of
// related-but-different sounds.
//
// Three threads, not two, unlike every other audio node here: analysis and
// full-buffer rendering are both too slow for CookIfNeeded's <5us budget
// (Analyze is a full STFT + partial tracking over the whole sample; Render
// is a per-sample additive synthesis over the whole sample). Both run on a
// dedicated worker std::thread, handed off to the main thread through a
// single mResultReady atomic flip (SampleScanner.cpp is the template), and
// the finished audio buffer reaches the audio thread through the existing
// SampleSlot. CookIfNeeded itself does none of that work - only draining a
// finished worker result and pushing already-computed param values.
class MolderNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new MolderNode(); }
   MolderNode();
   ~MolderNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &audioInput : nullptr; }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "record in" : nullptr;
   }

   bool LoadFile(const std::string& path);
   void ReloadFromPath();

   void StartRecording();
   void StopRecording();
   bool IsRecording() const { return mRecording; }
   bool RequiresAudioProcessing() const override { return IsRecording(); }

   // Feeds the current rendered buffer back in as the new analysis source
   // (the Lucier "I Am Sitting In A Room" move). The genome (seed +
   // generation) is untouched - only the re-analysis changes, so it's
   // deterministic given the same source and knobs.
   void Iterate();

   // Mutates the genome one generation further and re-renders - the main
   // "roll" action.
   void Roll();

   // Generation 0 (the analysed source, unmutated).
   void Reset();

   // Steps generation back by one and re-renders from the same seed -
   // trivial because the genome for any generation is always recomputed
   // from scratch by replaying (seed, generation) mutations, never stored.
   void RollBack();

   void TriggerPreview(float frac);
   void StopPreview();
   bool IsPlaying() const { return mIsPlaying; }

   const std::string& FilePath() const { return mFilePath; }
   const std::string& FileName() const { return mFileName; }
   const std::string& Status() const { return mStatus; }
   bool IsAnalyzing() const { return mWorking && mCurrentJob == Job::AnalyzeThenRender; }
   bool IsRendering() const { return mWorking; }

   static constexpr int kWaveformCacheSize = 256;
   float waveformMin[kWaveformCacheSize] = {};
   float waveformMax[kWaveformCacheSize] = {};
   int waveformCacheCount = 0;

   static constexpr int kPartialBarCount = 128;
   float partialBars[kPartialBarCount] = {};
   int partialBarCount = 0;

   float Playhead() const { return mPlayhead; }
   float AnalyzedF0() const { return mAnalysis.valid ? mAnalysis.globalF0 : 0.0f; }
   // 0..100, how "in tune" the measured partials are to a pure series, or
   // -1.0 when the analysis found no partial with any measurable energy
   // (an unpitched source) - the UI shows "--" rather than a fabricated
   // "100% harmonic" for "nothing to measure".
   float HarmonicityPercent() const;

   uint32_t Seed() const { return mSeed; }
   int Generation() const { return mGeneration; }

   // ---- the eight exposed controls (Tier 1 - see SKILL.md) --------------
   float chaos = 0.4f;              // 0..1, mutation strength for the next roll
   float pitch = 0.0f;              // -24..24 st, offset on top of the genome's own walk
   float tone = 0.5f;                // 0..1, tonal-vs-residual balance
   float air = 0.5f;                 // 0..1, steady-residual ("hiss") level
   float snap = 0.5f;                // 0..1, transient-residual ("attack") level
   float stretch = 0.5f;             // 0..1, inharmonicity + harmonic stretch, one knob
   float time = 0.5f;                // 0..1, attack/decay warp, 0.5 = unwarped
   float level = 0.8f;               // 0..2, output

   // ---- playback range/transport, mirroring SamplerNode verbatim --------
   // Pure playback params: they change nothing about the rendered buffer,
   // so they deliberately do NOT participate in DispatchSnapshot below -
   // adding them there would kick off a full STFT-and-additive-render every
   // time someone drags a marker.
   float start = 0.0f;    // 0..1, left edge of the playback/loop range
   float end = 1.0f;      // 0..1, right edge of the playback/loop range
   bool loop = false;
   bool reverse = false;  // plays start<-end instead of start->end
   bool pingpong = false; // with loop on, bounces direction at each edge instead of wrapping

   AudioCable audioInput; // record source

private:
   enum class Job
   {
      None,
      AnalyzeThenRender, // LoadFile / StopRecording / Iterate
      RenderOnly,        // Roll / knob change
   };

   // isOriginalSource marks a LoadFile()/StopRecording() dispatch (a
   // genuine new source) as opposed to Iterate()'s re-analysis of the last
   // render - only the former updates mOriginalAnalysis/mOriginalMono/SR,
   // which is what Reset() restores from. See PendingResult::isOriginalSource.
   void LaunchJob(Job job, std::vector<float> sourceOverride = {}, double sourceOverrideSR = 0.0,
                  bool isOriginalSource = false);
   void JoinWorkerIfDone();
   MolderDsp::Genome BuildDesiredGenome() const;
   void RebuildWaveformCache(const std::vector<float>& mono);
   void RebuildPartialCache();

   std::unique_ptr<AudioMolderNode> mAudioNode;
   int mLastCookFrame = -1;

   std::string mFilePath;
   std::string mFileName;
   std::string mStatus = "no sample loaded";
   bool mRecording = false;
   bool mIsPlaying = false;
   bool mSelfOwnedByUser = false;
   float mPlayhead = 0.0f;

   // The last analysis this node has ever produced, main-thread-owned,
   // handed to worker jobs by value (Analysis is a plain copyable struct of
   // vectors - a full copy per job launch, not a shared-mutable reference).
   MolderDsp::Analysis mAnalysis;

   // The genuine source as loaded/recorded - set once by LoadFile()/
   // StopRecording() and never touched by a render completing, so Reset()
   // always has something real to go back to regardless of how many Rolls
   // or Iterates happened since. (Previously this doubled as "whatever
   // Iterate() should re-analyze" and got clobbered by every render - see
   // the transport-and-range prompt's bug #3c.)
   MolderDsp::Analysis mOriginalAnalysis;
   std::vector<float> mOriginalMono;
   double mOriginalSR = 44100.0;

   // Whatever the worker most recently rendered - what Iterate() feeds back
   // in as its new analysis source ("I Am Sitting In A Room"). Updated on
   // every completed render regardless of job type; never read by Reset().
   std::vector<float> mLastRenderedMono;
   double mLastRenderedSR = 44100.0;

   uint32_t mSeed = 1;
   int mGeneration = 0;

   // Worker thread lifecycle: one job at a time, joined before the next one
   // starts (mirrors SampleScanner::StartScan). mAbort is set by the
   // destructor so a job in flight at teardown notices and returns early
   // rather than racing a freed node.
   std::thread mWorkerThread;
   std::atomic<bool> mWorking { false };
   std::atomic<bool> mAbort { false };
   std::atomic<bool> mResultReady { false };
   Job mCurrentJob = Job::None;

   struct PendingResult
   {
      bool analysisValid = false;
      MolderDsp::Analysis analysis;
      std::vector<float> rendered;
      // 4g stereo width: the right channel companion to `rendered`, filled
      // alongside it by MolderDsp::Render. Always the same length as
      // `rendered` when analysisValid is true.
      std::vector<float> renderedR;
      double renderedSR = 44100.0;
      MolderDsp::Genome genomeUsed;
      bool didAnalyze = false;
      bool isOriginalSource = false; // see LaunchJob's comment
   };
   PendingResult mPendingResult; // worker-owned until mResultReady flips, then main-thread-owned

   // Dirty-check snapshot: what was last dispatched to a RenderOnly job, so
   // CookIfNeeded's per-frame check is a handful of scalar compares, never
   // a genome recompute (that only happens inside the worker job itself).
   struct DispatchSnapshot
   {
      uint32_t seed = 0xFFFFFFFFu;
      int generation = -1;
      float chaos = -1.0f, pitch = 1e9f, tone = -1.0f, air = -1.0f, snap = -1.0f, stretchP = -1.0f, timeP = -1.0f;
      bool operator==(const DispatchSnapshot& o) const
      {
         return seed == o.seed && generation == o.generation && chaos == o.chaos && pitch == o.pitch &&
                tone == o.tone && air == o.air && snap == o.snap && stretchP == o.stretchP && timeP == o.timeP;
      }
   };
   DispatchSnapshot mLastDispatched;
   int mCooldownFrames = 0; // simple debounce while a slider is being dragged
};
