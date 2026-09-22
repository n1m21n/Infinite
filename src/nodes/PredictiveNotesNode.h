#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "audio/NoteModel.h"
#include "core/INode.h"
#include "core/Modulation.h"
#include "core/NoteCable.h"

class AudioPredictiveNotesNode;

// Predictive Notes (docs/plans/prediction/step-06). Wire a note chain in and press Learn: it
// captures the notes (passing them through so you still hear them), learns them as a
// multiple-viewpoint variable-order Markov model (audio/NoteModel.h, README §8.2) and then plays
// standalone in that style. The learned model is saved in the patch as the captured notes; tables
// are rebuilt from them on load, so a patch plays without re-learning.
//
// Two objects: this INode owns Learn state, the meter, and table building (worker thread); the
// audio object samples and emits NoteEvents. Tables cross to the audio thread by atomic pointer
// swap and are freed on the main thread after the audio thread has moved past them.
class PredictiveNotesNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PredictiveNotesNode(); }
   PredictiveNotesNode();
   ~PredictiveNotesNode() override;

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
   int sourceMode = 0;        // 0 = Notes (Source A), 1 = Movement (Source B)
   float stray = 0.5f;        // 0 replay .. 0.5 faithful .. 1 uniform in range
   int memory = 4;            // max context order 0..8
   float lengthSpread = 0.25f;
   float velocitySpread = 0.25f;
   int rangeLow = 36;
   int rangeHigh = 96;
   int seed = 1;
   bool useGlobalScale = false; // snap generated notes to Transport's key/scale
   std::string model;         // NoteModel::EncodeEvents of the learned notes

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
   // learned model (CopyParams round-trips `model` like any other param; this undoes that for the
   // learned field specifically). Does not touch the cross-session shared pool - the source
   // instance's contribution to house style stays.
   void ResetLearnedState();
   int NotesCaptured() const { return mNotesCaptured; }
   int LearnedNotes() const { return mLearned; }
   int BarsCaptured() const { return mBars; }
   bool Building() const { return mBuild.valid(); }
   // True right after a Stop that captured fewer than 2 notes: the old model (if any) is still
   // playing untouched, and nothing in the UI said so before this flag existed.
   bool LastLearnTooShort() const { return mLastLearnTooShort; }
   // Bits per event that the full blend beats the order-0 baseline by, one point per captured bar.
   const std::vector<float>& Curve() const { return mCurve; }
   int LastNote() const;
   float Confidence01() const;
   int Dropped() const;

   AudioPredictiveNotesNode* Audio() { return mAudioNode.get(); }
   // Blocks until any table build has finished and been swapped in (tests, save).
   void WaitForBuild();
   // Tests: put tables in play through the same swap path a finished build uses.
   void TestSwapTables(NoteModel::Tables* t) { SwapIn(t); }

private:
   void DrainCaptures();
   void UpdateMeter(bool force);
   void FinishLearn();
   void StartBuild(const std::vector<NoteModel::Event>& events);
   void PollBuild();
   void SwapIn(NoteModel::Tables* t);

   std::unique_ptr<AudioPredictiveNotesNode> mAudioNode;
   int mLastCookFrame = -1;

   bool mLearning = false;
   std::vector<Cap> mCaps;
   int mNotesCaptured = 0, mBars = 0, mLearned = 0;
   bool mLastLearnTooShort = false;
   double mBeatsPerBar = 4.0;
   std::vector<float> mCurve;

   std::future<NoteModel::Tables*> mBuild;
   bool mQueued = false;
   std::vector<NoteModel::Event> mQueuedEvents;
   std::string mAppliedModel;
};

// The cross-session "house style" pool. Every Predictive Notes instance, in every patch, feeds
// and reads this same rolling window of captured events - same shape as PredictiveVelocityProfile
// (PredictiveVelocityNode.h) and ColorStats::Engine (src/core/ColorStats.h).
namespace PredictiveNotesStyle
{
   bool Load(const std::string& directory);
   bool Save(const std::string& directory);
   bool HasLearnedData();
}

namespace PredictiveNotes
{
   // INFINITE_PREDMIDITEST, headless: exact replay, in-key sampling, stray extremes, table swap on the
   // audio thread, save/load determinism, the Learn meter and the improved Random/Chorder theory.
   bool RunPredMidiTest();
}
