#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPredictiveVelocityNode;

// Predictive Velocity (docs/plans/prediction/step-10-predictive-velocity). Wire a note chain in
// and press Learn: it captures incoming note-on velocities, passing them through untouched while
// it listens, and learns the shape of the player's own dynamic range - not one average, but an
// 8-bin (NoteModel::kVelBins) profile of what "loud" and "soft" actually mean for this player.
// Stop learning and it remaps future note-on velocities from the nominal 0..127 range onto that
// learned range - the loudest nominal note plays at the player's own real loudest, not a
// theoretical 127. Same relationship to VelocityCurveNode (NoteNodes.h) that Predictive Quantize
// has to QuantizerNode: one fixed, hand-picked shape vs. one learned from what was actually played.
//
// Two objects, matching PredictiveQuantizeNode's split: this INode owns Learn state and the
// histogram fit (synchronous - 8 bins is far too small to need a worker thread); the audio object
// captures velocities and applies the remap. The learned curve crosses to the audio thread by
// atomic pointer swap, freed on the main thread once the audio thread has moved past it - same
// shape as PredictiveQuantizeNode's ModeSet, chosen over 8 independent atomics so a Render call
// can never observe a curve that is half old bins and half new ones.
class PredictiveVelocityNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PredictiveVelocityNode(); }
   PredictiveVelocityNode();
   ~PredictiveVelocityNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   void SweepPrepare() override;
   bool SweepNeedsClock() const override { return false; } // remap is per-note-on, not time-based

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   // Saved params. Names are patch keys.
   float mix = 0.75f;        // 0 = untouched velocity, 1 = fully remapped onto the learned range
   std::string velCurve;     // encoded 8-bin remap: "binMean0;binMean1;...;binMean7" (0..127 each)

   NoteCable noteInput;

   // --- main-thread UI / test helpers ---
   bool IsLearning() const { return mLearning; }
   void SetLearning(bool on);
   int NotesCaptured() const { return mNotesCaptured; }
   bool HasCurve() const;
   bool LastLearnTooShort() const { return mLastLearnTooShort; }
   float Confidence01() const;
   int Dropped() const;

   AudioPredictiveVelocityNode* Audio() { return mAudioNode.get(); }
   // Tests: install a curve through the same swap path a finished Learn uses.
   void TestSwapCurve(const std::string& encoded);

private:
   void DrainCaptures();
   void FinishLearn();
   void FitCurve();

   std::unique_ptr<AudioPredictiveVelocityNode> mAudioNode;
   int mLastCookFrame = -1;

   bool mLearning = false;
   std::vector<int> mCapturedVel127;
   int mNotesCaptured = 0;
   int mBinsCovered = 0; // how many of the 8 velocity bins the last fit actually saw data for
   bool mLastLearnTooShort = false;
   std::string mAppliedCurve;
};

namespace PredictiveVelocity
{
   // INFINITE_PREDVELOCITYTEST, headless: identity at mix=0, exact remap to a learned curve at
   // mix=1, too-little-data falls back to identity rather than overfitting, note-off/bend events
   // pass through untouched, save/load round-trips the encoded curve.
   bool RunPredVelocityTest();
}
