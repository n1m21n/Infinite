// Ported from upstream Infinite (github.com/n1m21n/Infinite) into Infinite-Turbo.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPredictiveVelocityNode;

// Predictive Velocity (docs/plans/prediction/step-10-predictive-velocity, revised to a global
// adaptive profile - see PredictiveVelocityProfile below). Wire a note chain in: it always captures
// incoming note-on velocities, passing them through while also remapping them in the same pass, and
// continuously refits an 8-bin (NoteModel::kVelBins) profile of what "loud" and "soft" actually mean
// for this player. There is no Learn/Stop - the remap is live from the first note it has enough data
// to trust, and keeps adapting as you keep playing. Same relationship to VelocityCurveNode
// (NoteNodes.h) that Predictive Quantize has to QuantizerNode: one fixed, hand-picked shape vs. one
// learned from what was actually played.
//
// Two objects, matching PredictiveQuantizeNode's split: this INode drains captures and feeds/refits
// the shared global profile (synchronous - 8 bins is far too small to need a worker thread); the
// audio object captures velocities and applies the remap every note-on. The learned curve crosses to
// the audio thread by atomic pointer swap, freed on the main thread once the audio thread has moved
// past it - same shape as PredictiveQuantizeNode's ModeSet, chosen over 8 independent atomics so a
// Render call can never observe a curve that is half old bins and half new ones.
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

   // Saved params. Names are patch keys. The learned curve itself is NOT a patch param - it lives in
   // the global PredictiveVelocityProfile, shared by every instance of this node in every patch.
   float mix = 0.75f; // 0 = untouched velocity, 1 = fully remapped onto the learned range

   NoteCable noteInput;

   // --- main-thread UI / test helpers ---
   bool HasCurve() const;
   float Confidence01() const;
   int Dropped() const;
   // Total notes ever folded into the global profile's current rolling window (across all patches
   // and sessions) - what the status line reports instead of a per-Learn-take count.
   int TotalCaptured() const;

   AudioPredictiveVelocityNode* Audio() { return mAudioNode.get(); }

private:
   void DrainCaptures();
   void RefitIfDirty();

   std::unique_ptr<AudioPredictiveVelocityNode> mAudioNode;
   int mLastCookFrame = -1;
   uint64_t mAppliedProfileVersion = 0;
   bool mCachedHasCurve = false;
   int mCachedBinsCovered = 0;
};

// The global, cross-patch, cross-session learned dynamics profile. Every Predictive Velocity
// instance, in every patch, feeds and reads this same rolling window - same shape as
// PredictiveQuantizeProfile (PredictiveQuantizeNode.h) and ColorStats::Engine
// (src/core/ColorStats.h).
namespace PredictiveVelocityProfile
{
   bool Load(const std::string& directory);
   bool Save(const std::string& directory);
   bool HasLearnedData();
}

namespace PredictiveVelocity
{
   // INFINITE_PREDVELOCITYTEST, headless: identity at mix=0, exact remap to a learned curve at
   // mix=1, too-little-data falls back to identity rather than overfitting, note-off/bend events
   // pass through untouched, the global profile's rolling window and refit behave correctly.
   bool RunPredVelocityTest();
}
