// Ported from upstream Infinite (github.com/n1m21n/Infinite) into Infinite-Turbo.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPredictiveQuantizeNode;

// Predictive Quantize (docs/plans/prediction/step-09, revised to a global adaptive profile - see
// PredictiveQuantizeProfile below). Wire a note chain in: it always captures incoming note onsets,
// passing them through while also correcting them in the same pass, and continuously refits a small
// histogram of inter-onset spacings. There is no Learn/Stop - the correction is live from the first
// onset it has enough data to trust, and keeps adapting as you keep playing. A groove/template
// quantizer, not the fixed-grid QuantizerNode (NoteNodes.h) sitting next to it in the palette.
//
// Two objects, matching PredictiveNotesNode's split: this INode drains captures and feeds/refits the
// shared global profile (synchronous - small enough it needs no worker thread); the audio object
// captures onsets and applies the correction every block. The learned mode set crosses to the audio
// thread by atomic pointer swap, freed on the main thread once the audio thread has moved past it.
class PredictiveQuantizeNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PredictiveQuantizeNode(); }
   PredictiveQuantizeNode();
   ~PredictiveQuantizeNode() override;

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

   // Saved params. Names are patch keys. The learned spacing profile itself is NOT a patch param -
   // it lives in the global PredictiveQuantizeProfile, shared by every instance of this node in
   // every patch (see PredictiveQuantizeProfile below).
   float mix = 0.75f; // 0 = untouched pass-through, 1 = fully snapped to the nearest learned spacing

   NoteCable noteInput;

   // --- main-thread UI / test helpers ---
   // One captured note-on: absolute beat and its onset's IOI-viewpoint tick (NoteModel::kTicksPerBeat).
   struct Cap
   {
      double beat;
   };
   int ModeCount() const;
   float Confidence01() const;
   int Dropped() const;
   // Total onsets ever folded into the global profile's current rolling window (across all patches
   // and sessions) - what the status line reports instead of a per-Learn-take count.
   int TotalCaptured() const;

   AudioPredictiveQuantizeNode* Audio() { return mAudioNode.get(); }

private:
   void DrainCaptures();
   void RefitIfDirty();

   std::unique_ptr<AudioPredictiveQuantizeNode> mAudioNode;
   int mLastCookFrame = -1;
   bool mHaveLastBeat = false;
   double mLastCapturedBeat = 0.0;
   uint64_t mAppliedProfileVersion = 0;
   int mCachedModeCount = 0;
   float mCachedWeightSum = 0.0f;
};

// The global, cross-patch, cross-session learned spacing profile. Every Predictive Quantize
// instance, in every patch, feeds and reads this same rolling window - same shape as
// ColorStats::Engine (src/core/ColorStats.h), minus the time-decay (a bounded sample count instead:
// the profile tracks the player's *current* feel by forgetting the oldest captures once the window
// is full, rather than decaying old evidence by active time).
namespace PredictiveQuantizeProfile
{
   bool Load(const std::string& directory);
   bool Save(const std::string& directory);
   bool HasLearnedData();
}

namespace PredictiveQuantize
{
   // INFINITE_PREDQUANTIZETEST, headless: identity at mix=0, exact snap to a learned mode at
   // mix=1, chord grouping doesn't arpeggiate, cold start passes input through unchanged, the global
   // profile's rolling window and refit behave correctly.
   bool RunPredQuantizeTest();
}
