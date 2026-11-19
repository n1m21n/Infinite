#pragma once

#include "INode.h"
#include "Modulation.h"

#include <array>
#include <map>
#include <string>
#include <vector>

// --- Brain Sculpt (Breakthrough 1: Shift-Grab Co-Adaptive Neural Sculpting) -
// Implements Infinite's IPredictor interface so an artist wearing a headset
// (or running SyntheticCortex) can Shift-grab any wired knob across the canvas
// in different mental states. Each OnRelease() captures an anchor (z_brain, pos)
// in 6D neural latent space [Arousal, Valence, Entropy, Coherence, Brow, Jaw],
// and Gaussian RBF kernel interpolation smoothly sculpts every bound parameter
// simultaneously as the user's mind moves between those states.
class BrainSculptNode : public INode, public IModulator, public IPredictor
{
public:
   struct Anchor
   {
      std::array<float, 6> z = { 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f };
      float targetPos01 = 0.5f;
   };

   static INode* Create() { return new BrainSculptNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   const char* InputLabel(int) const override { return "brain"; }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float Value01() override { return mSummaryValue; }

   // IPredictor interface
   void Tick(int frameId, double dt) override;
   float ValuePos01For(const ParamKey& k, float curPos) override;
   void OnGrab(const ParamKey& k) override;
   void OnRelease(const ParamKey& k, float pos, float velPerSec) override;
   float Confidence01(const ParamKey& k) const override;

   void ClearAnchors();
   int TotalAnchorCount() const;
   int BoundParamCount() const { return (int)mAnchors.size(); }

   // Serializable parameters
   float sigma = 0.35f;      // RBF kernel bandwidth in normalized 6D latent space
   float smoothing = 0.35f;  // Output smoothing time constant (0 = fast, 1 = sluggish)
   float regularization = 0.05f; // Ridge pull toward resting curPos when far from all anchors

   void VisitParams(ParamVisitor& v) override;

private:
   std::array<float, 6> CurrentLatent() const;
   float PredictRaw(const ParamKey& k, float fallbackPos) const;

   std::map<ParamKey, std::vector<Anchor>> mAnchors;
   std::map<ParamKey, float> mSmoothedPos;
   int mLastTickFrame = -1;
   float mSummaryValue = 0.5f;
};
