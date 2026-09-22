#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "audio/NoteModel.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPredictiveRhythmNode;

// Predictive Rhythm (docs/plans/prediction/step-11). Wire any note source in and press Learn, just
// like Predictive Notes: it captures the notes (passing them through so you still hear them) and
// learns them as a multiple-viewpoint variable-order Markov model (audio/NoteModel.h). The
// difference from Predictive Notes is what it learns pitch as - not the absolute MIDI note, but a
// signed interval from a selected `root` note (NoteModel's kPitchRel viewpoint), so what comes back
// out reads as a repeating rhythmic pattern anchored on one note with occasional deviation (e.g.
// C2 C2 C2 D3 C2 C2 C2) rather than a free melody. `root` is auto-detected from the learned material
// (the most common captured note) when Learn finishes, and can be changed afterward - since pitch is
// stored as an interval from the root, changing `root` transposes the whole learned pattern live,
// without relearning.
//
// Two objects, same split as PredictiveNotesNode: this INode owns Learn state and the worker-thread
// table build; the audio object samples and emits NoteEvents from the live tables.
class PredictiveRhythmNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PredictiveRhythmNode(); }
   PredictiveRhythmNode();
   ~PredictiveRhythmNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   void SweepPrepare() override;
   bool SweepNeedsClock() const override { return true; }

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   // Saved params. Names are patch keys.
   float stray = 0.5f;   // 0 replay .. 0.5 faithful .. 1 uniform
   int memory = 4;        // max context order 0..8
   float mix = 1.0f;      // 0 = silent (no generated output), 1 = full generated pattern
   int root = 60;          // MIDI note pitch is measured relative to; auto-set when Learn finishes
   std::string model;     // NoteModel::EncodeEvents of the learned example, relPitch-populated

   NoteCable noteInput;

   // --- main-thread UI / test helpers ---
   struct Cap // one captured note event: absolute beat, the note-on's voiceId, note, velocity 0..127
   {
      double beat;
      int voiceId;
      uint8_t note, vel;
      bool on;
   };
   bool IsLearning() const { return mLearning; }
   void SetLearning(bool on);
   // Clears this instance's own learned material back to "press Learn" - used when a node is
   // duplicated/copy-pasted so the copy starts fresh instead of inheriting the source's exact
   // learned pattern. Does not touch the cross-session shared pool - the source instance's
   // contribution to house rhythm style stays.
   void ResetLearnedState();
   int NotesCaptured() const { return mNotesCaptured; }
   int LearnedNotes() const { return mLearned; }
   bool Building() const { return mBuild.valid(); }
   bool LastLearnTooShort() const { return mLastLearnTooShort; }
   // Bits per event that the full blend beats the order-0 baseline by, one point per captured bar.
   const std::vector<float>& Curve() const { return mCurve; }
   float Confidence01() const;
   int Dropped() const;

   AudioPredictiveRhythmNode* Audio() { return mAudioNode.get(); }
   void WaitForBuild();
   void TestSwapTables(NoteModel::Tables* t) { SwapIn(t); }

private:
   void DrainCaptures();
   void UpdateMeter(bool force);
   void FinishLearn();
   void StartBuild(const std::vector<NoteModel::Event>& events);
   void PollBuild();
   void SwapIn(NoteModel::Tables* t);

   std::unique_ptr<AudioPredictiveRhythmNode> mAudioNode;
   int mLastCookFrame = -1;

   bool mLearning = false;
   std::vector<Cap> mCaps;
   int mNotesCaptured = 0, mLearned = 0, mMeterBars = 0;
   bool mLastLearnTooShort = false;
   double mBeatsPerBar = 4.0;
   std::vector<float> mCurve; // held-out cross-entropy gain, one point per captured bar (meter only)

   std::future<NoteModel::Tables*> mBuild;
   bool mQueued = false;
   std::vector<NoteModel::Event> mQueuedEvents;
   std::string mAppliedModel;
};

// The cross-session "house rhythm style" pool. Every Predictive Rhythm instance, in every patch,
// feeds and reads this same rolling window of captured events - same shape as
// PredictiveNotesStyle (PredictiveNotesNode.h) and ColorStats::Engine (src/core/ColorStats.h).
namespace PredictiveRhythmStyle
{
   bool Load(const std::string& directory);
   bool Save(const std::string& directory);
   bool HasLearnedData();
}

namespace PredictiveRhythm
{
   // INFINITE_PREDRHYTHMTEST, headless: learn captures a mostly-one-note pattern with a couple of
   // deviations, root auto-detects to the most common note, relPitch resolves correctly on replay;
   // changing root after learning transposes playback; mix=0 is silent; save/load round trip;
   // single-NoteCable spawn/wire/delete-mid-playback safety.
   bool RunPredRhythmTest();
}
