#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "audio/NoteModel.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPredictiveBasslineNode;

// Predictive Bassline (docs/plans/prediction/step-11). Wire a harmony source into "harmony" and an
// example bass part into "learn from" (the same wire in the simple case), press Learn: it captures
// the example, and builds a bass *feel* from it - rhythm, duration and velocity via NoteModel's
// existing kIoi/kDur/kVel viewpoints, reused wholesale (Predictive Notes' §6.1 flow), plus a new
// kPitchRel viewpoint (NoteModel.h/.cpp) that learns pitch as a signed interval from whatever root
// was sounding at that moment, not as absolute MIDI. At play time the bass plays on its own learned
// rhythm (deliberately decoupled from the harmony input's onsets - see the design doc §2.3), and at
// each of its own onsets samples a relative-pitch symbol from the learned model and resolves it
// against the *live* root (lowest currently-held note on the harmony input), so the same learned
// habit re-expresses correctly against harmony it was never trained on.
//
// Two objects, same split as PredictiveNotesNode: this INode owns Learn state and the worker-thread
// table build; the audio object tracks the live root from harmonyInput's held notes every block
// (small fixed-capacity voiceId->note map, same shape as AudioQuantizerNode::mOutNote,
// NoteNodes.cpp:1465, not the same code) and samples/resolves/emits from learnInput's tables. Two
// NoteCable inputs on one node - unusual in this codebase (NoteMergeNode is the only other multi-
// input note node, and even it treats all its slots identically) - so both the pin labels below and
// both node-help tables must describe "harmony" and "learn from" distinctly; see
// .claude/skills/audio-node-sweep/SKILL.md's "only one note inbox is ever wired" blind spot, which
// applies here too: the generic sweep only ever connects slot 0 (harmony).
class PredictiveBasslineNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PredictiveBasslineNode(); }
   PredictiveBasslineNode();
   ~PredictiveBasslineNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   void SweepPrepare() override;
   bool SweepNeedsClock() const override { return true; }

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &harmonyInput : slot == 1 ? &learnInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "harmony" : slot == 1 ? "learn from" : nullptr; }
   INode* BypassSource() override { return harmonyInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   // Saved params. Names are patch keys.
   float stray = 0.5f;   // 0 replay .. 0.5 faithful .. 1 uniform
   int memory = 4;        // max context order 0..8
   float mix = 1.0f;      // 0 = silent (no generated bass), 1 = full generated bass
   std::string model;     // NoteModel::EncodeEvents of the learned example, relPitch-populated

   NoteCable harmonyInput; // read live every block, never captured
   NoteCable learnInput;   // captured only while Learn is on

   // --- main-thread UI / test helpers ---
   struct Cap // one captured note event from learnInput: absolute beat, voiceId, note, velocity, root
   {
      double beat;
      int voiceId;
      uint8_t note, vel;
      int8_t rootAtCapture; // lowest held harmonyInput note at capture time, or -1 if none held
      bool on;
   };
   bool IsLearning() const { return mLearning; }
   void SetLearning(bool on);
   int NotesCaptured() const { return mNotesCaptured; }
   int LearnedNotes() const { return mLearned; }
   bool Building() const { return mBuild.valid(); }
   bool LastLearnTooShort() const { return mLastLearnTooShort; }
   int LiveRoot() const; // -1 if harmonyInput has never held a note

   AudioPredictiveBasslineNode* Audio() { return mAudioNode.get(); }
   void WaitForBuild();
   void TestSwapTables(NoteModel::Tables* t) { SwapIn(t); }

private:
   void DrainCaptures();
   void FinishLearn();
   void StartBuild(const std::vector<NoteModel::Event>& events);
   void PollBuild();
   void SwapIn(NoteModel::Tables* t);

   std::unique_ptr<AudioPredictiveBasslineNode> mAudioNode;
   int mLastCookFrame = -1;

   bool mLearning = false;
   std::vector<Cap> mCaps;
   int mNotesCaptured = 0, mLearned = 0;
   bool mLastLearnTooShort = false;
   double mBeatsPerBar = 4.0;

   std::future<NoteModel::Tables*> mBuild;
   bool mQueued = false;
   std::vector<NoteModel::Event> mQueuedEvents;
   std::string mAppliedModel;
};

namespace PredictiveBassline
{
   // INFINITE_PREDBASSLINETEST, headless: same-harmony playback matches root, transposed-harmony
   // playback proves relative-pitch resolution, chord change mid-note proves decoupled timing,
   // harmony-input-disconnected holds the last known root, dual-NoteCable spawn/wire/save-load/
   // delete-mid-playback safety.
   bool RunPredBasslineTest();
}
