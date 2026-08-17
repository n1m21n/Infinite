#pragma once

#include <memory>
#include <vector>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "audio/EffectDefs.h"

class AudioEffectRuntime; // AudioNode subclass, defined in AudioEffectNode.cpp

// One class serves every entry in the EffectDefs table (Audio Filter today;
// Dynamics/Delay/Reverb/Drive/Stereo/Pitch Time later) - exactly the way
// FilterNode serves every FilterDef (core/FilterDefs.h/FilterNode.h). See
// docs/plans/audio/P3c-P3a2-design.md §0.4.
//
// What is genuinely table-driven here: a param's declaration, its
// VisitParams save/load key, and its push to the runtime all walk the *same*
// EffectDef::params list (VisitParams below, and AudioEffectRuntime::
// PushParams in the .cpp), so the "declared it for the UI but forgot to wire
// it into the mailbox" bug class - the one AUDIOPARAMSWEEPTEST exists to
// catch - cannot happen: there is exactly one list.
//
// What is deliberately NOT table-driven: which controls land in Tier 1 vs
// Tier 2, and how a dynamic selection (Audio Filter's Tier 1 follows
// whichever band is selected) maps onto them. The design doc gives the
// visualizers this same carve-out for the same reason - heterogeneity, not
// oversight, since a frequency-response curve, a goniometer and a transfer
// curve share nothing. This node's body stays a per-effect Draw*Body
// function, exactly like every other audio node in this tree (GainNode,
// MixerNode and WavetableNode all hand-write their own body despite sharing
// the BeginAudioBody/AudioKnobRow/BeginAudioSection grammar) - see
// audio-node-ui-system.md.
class AudioEffectNode : public INode, public IAudioSource
{
public:
   static INode* CreateFor(const EffectDef& def) { return new AudioEffectNode(def); }

   explicit AudioEffectNode(const EffectDef& def);
   ~AudioEffectNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   // Slot 1 (sidechain) only exists for an EffectDef with hasSidechain set
   // (Dynamics) - every other effect's contiguous-slot count stops at 1, the
   // same generic mechanism CollectAudioChain (main.cpp) already walks for
   // every node's input pins.
   AudioCable* AudioInputSlot(int slot) override
   {
      if (slot == 0)
         return &input;
      if (slot == 1 && mDef.hasSidechain)
         return &sidechainInput;
      return nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      if (slot == 0)
         return "audio";
      if (slot == 1 && mDef.hasSidechain)
         return mDef.sidechainLabel;
      return nullptr;
   }

   const EffectDef& Def() const { return mDef; }

   // Index-based, mirroring FilterNode::ParamPtr - the table (mDef.params) is
   // shared across every AudioEffectNode of this kind, so an index into it is
   // the stable handle UI code binds ModKnob/AudioSlider to directly.
   float* ParamPtr(size_t index) { return &mParamValues[index]; }
   float Param(size_t index) const { return mParamValues[index]; }
   // -1 if `name` isn't in this effect's table. A linear search over a
   // couple dozen entries, fine at UI/CookIfNeeded rates; not called from the
   // audio thread.
   int ParamIndex(const char* name) const;
   float* ParamPtr(const char* name)
   {
      const int i = ParamIndex(name);
      return i >= 0 ? &mParamValues[i] : nullptr;
   }
   float Param(const char* name) const
   {
      const int i = ParamIndex(name);
      return i >= 0 ? mParamValues[i] : 0.0f;
   }

   // Universal per §0.5: every effect has a dry/wet mix. A dedicated field
   // rather than a table row, so the rule is enforced by construction (every
   // AudioEffectNode has one) rather than left to each EffectDef to remember
   // to declare - the same reasoning kAudioNarrowWidth exists for.
   float mix = 1.0f;

   float Level() const { return mLevel; }
   int LatencySamples() const;
   // Kernel-published extra meter values beyond AudioEffectRuntime's own
   // generic post-mix peak - Dynamics publishes {instantaneous input dB,
   // gain-reduction dB} for its transfer-curve visualizer's operating-point
   // dot and GR bar. 0 for any index a kernel doesn't publish. See
   // IEffectKernel::ExtraMeter.
   static constexpr int kMaxExtraMeterValues = 4;
   float ExtraMeterValue(int index) const
   {
      return (index >= 0 && index < kMaxExtraMeterValues) ? mExtraMeterValues[index] : 0.0f;
   }

   AudioCable input;
   AudioCable sidechainInput; // only wired to a pin when mDef.hasSidechain

private:
   const EffectDef& mDef;
   std::vector<float> mParamValues;
   std::unique_ptr<AudioEffectRuntime> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
   float mExtraMeterValues[kMaxExtraMeterValues] = {};
};
