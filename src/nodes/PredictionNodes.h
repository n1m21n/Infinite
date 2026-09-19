#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "INode.h"
#include "Modulation.h"
#include "core/MovementStats.h"

// The green (prediction) node, docs/plans/prediction README §4.1 and step-04. One node drives many
// params; it keeps one small simulation slot per bound ParamKey. It is an IModulator only so the
// matrix and the meter see it: the apply loop reaches it through IPredictor and writes each
// destination in its own fader space.
//
// Per slot, per frame (Euler-Maruyama, all in fader position):
//    v  *= exp(-dt / momentum)                                  Continue
//    T   = stray * (1 + link * handEnergy)
//    eta = speed * theta * k          k = sigma^2 / (2 theta): the fitted OU stationary variance,
//                                     so a Gaussian well relaxes at rate eta/k = speed * theta
//    x  += v dt - clamp(eta U'(x) dt, +-0.05) + sqrt(2 eta T dt) N(0,1)      Home + Wander
//    reflect x into the learned range                           U = -log(p_hat + eps)
// Stationary law is proportional to p_hat^(1/T): Stray is an exact temperature.
class DriftNode : public INode, public IModulator, public IPredictor
{
public:
   static INode* Create() { return new DriftNode(); }
   DriftNode();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   // IPredictor
   void Tick(int frameId, double dt) override;
   float ValuePos01For(const ParamKey& k, float curPos) override;
   void OnGrab(const ParamKey& k) override;
   void OnRelease(const ParamKey& k, float pos, float velPerSec) override;

   // Saved params. Names are patch keys.
   float speed = 1.0f;     // 0.1..4
   float stray = 1.0f;     // 0.25..4, log
   float momentum = 1.0f;  // seconds, 0..4
   float link = 0.0f;      // 0..2
   int seed = 1;
   bool frozen = false;
   std::string frozenProfile; // one entry per key: uid:paramIndex:base64(64 x u8, theta, sigma, lo, hi)
   std::string anchors;       // uid:paramIndex=pos;...   the last hand-set place of each key

   // --- v2 Step 7 Params ---
   bool follow = false;       // 7a Follow leader mode
   int leaderNodeIndex = -1;
   int leaderParamIndex = 0;
   bool sectionConditioned = false; // 7e Section-conditioned dwell landscape
   int recallBar = -1;        // 7b Recall jump trigger

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("speed", speed); v.Float("stray", stray); v.Float("momentum", momentum);
      v.Float("link", link); v.Int("seed", seed); v.Bool("frozen", frozen);
      v.Text("frozenProfile", frozenProfile); v.Text("anchors", anchors);
      v.Bool("follow", follow); v.Int("leaderNodeIndex", leaderNodeIndex);
      v.Int("leaderParamIndex", leaderParamIndex);
      v.Bool("sectionConditioned", sectionConditioned); v.Int("recallBar", recallBar);
   }

   // --- UI / test helpers (main thread) ---
   // 0 = grey (defaults, w1 < 0.2), 1 = dim green (your style from other patches, w1 < 0.6), 2 = full
   // green (this knob). Frozen reads 2.
   int ConfidenceRung(const ParamKey& k) const;
   float Confidence01(const ParamKey& k) const override;
   // The likely next 2 s (kGhostPoints samples) from a COPY of the slot's x, v and RNG state, at a
   // fixed dt. Never advances or reseeds the real slot. Cached per slot per frame. Returns the
   // number of points written (0 when the slot is absent or paused under a hand grab).
   static constexpr int kGhostPoints = 32;
   static constexpr double kGhostDt = 1.0 / 16.0;
   int Ghost(const ParamKey& k, float* out, int maxPoints);
   // The slot's current position, or -1.
   float SlotPos(const ParamKey& k) const;
   // Point the node at another stats engine (tests); default is MovementStats::Live().
   void SetStatsSource(const MovementStats::Engine* e) { mStats = e; }
   int SlotCount() const { return (int)mSlots.size(); }

   // Deterministic simulation entry points shared by Tick and the ghost.
   static constexpr int kBins = MovementStats::kBins;
   struct Model
   {
      float p[kBins] = {}; // normalised so the bins sum to 1
      float theta = 0.5f, sigma = 0.1f, lo = 0.0f, hi = 1.0f;
      double nEff = 0.0;   // the key's own n_eff
      float w1 = 0.0f;     // weight of the key's own data in the blend (drives the confidence dot)
      float w2 = 0.0f, w2b = 0.0f, w2d = 0.0f, w3 = 0.0f;
      bool cold = true;
   };
   struct Slot
   {
      float x = 0.5f, v = 0.0f;
      uint64_t rng = 1;
      bool paused = false;
      int lastSeen = 0;
      int modelFrame = -1000;
      Model model;
      // ghost cache (never feeds back into the dynamics)
      int ghostFrame = -1;
      int ghostN = 0;
      float ghost[kGhostPoints] = {};
   };
   struct Params { float speed, stray, momentum, link; };
   // One step of the dynamics on (x, v, rng). handEnergy in [0,1].
   static void Step(float& x, float& v, uint64_t& rng, const Model& m, const Params& p, float handEnergy, double dt);

private:
   struct Frozen
   {
      uint8_t q[kBins] = {};
      float theta = 0.5f, sigma = 0.1f, lo = 0.0f, hi = 1.0f;
   };

   float EnergyFor(const ParamKey& k) const;
   Slot& SlotFor(const ParamKey& k, float curPos);
   void RefreshModel(const ParamKey& k, Slot& s);
   float AnchorFor(const ParamKey& k) const;
   void SetAnchor(const ParamKey& k, float pos);
   void SyncAnchors();
   void SyncFrozen(int frameId);
   void RebuildAnchorText();
   void RebuildFrozenText();

   std::map<ParamKey, Slot> mSlots;
   std::map<ParamKey, float> mAnchors;
   std::map<ParamKey, Frozen> mFrozen;
   std::string mParsedAnchors, mParsedFrozen;
   bool mFrozenApplied = false;
   int mFrame = 0;
   const MovementStats::Engine* mStats = nullptr;
};

// 7d Your Moves Faders node (Prediction Step 7d)
// Macro faders driving multi-parameter offsets via PCA on deltas (p = p_now + W * delta_h)
class MovesNode : public INode, public IModulator, public IPredictor
{
public:
   static INode* Create() { return new MovesNode(); }
   MovesNode();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   // IPredictor
   void Tick(int frameId, double dt) override;
   float ValuePos01For(const ParamKey& k, float curPos) override;
   void OnGrab(const ParamKey& k) override;
   void OnRelease(const ParamKey& k, float pos, float velPerSec) override;

   // Saved params. Names are patch keys.
   float gesture = 0.0f; // -1 .. +1 (primary unified gesture macro)
   float amount = 1.0f;  // 0 .. 2 (depth scale)
   float fader1 = 0.0f;  // legacy compatibility
   float fader2 = 0.0f;
   float fader3 = 0.0f;
   float fader4 = 0.0f;
   std::string weightsData;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("gesture", gesture);
      v.Float("amount", amount);
      v.Float("fader1", fader1);
      v.Float("fader2", fader2);
      v.Float("fader3", fader3);
      v.Float("fader4", fader4);
      v.Text("weightsData", weightsData);
   }

   void RefreshPCA(const MovementStats::Engine* engine = nullptr);
   int SlotCount() const { return (int)mBasePos.size(); }
   float ExplainedVariance(int comp) const
   {
      return (comp >= 0 && comp < (int)mPCA.explainedVarianceRatio.size()) ? mPCA.explainedVarianceRatio[comp] : 0.0f;
   }
   void SetStatsSource(const MovementStats::Engine* e) { mStats = e; }
   float Confidence01(const ParamKey& k) const override;

private:
   std::map<ParamKey, float> mBasePos;
   std::map<ParamKey, float> mOutputPos;
   MovementStats::MovesPCA mPCA;
   const MovementStats::Engine* mStats = nullptr;
};

namespace PredictionNodes
{
   // INFINITE_DRIFTTEST, headless: dynamics, release carry, determinism, save/load, ghost isolation.
   bool RunDriftTest();
   // INFINITE_PREDFEEDBACKTEST, headless: cold start, blending, role pooling, your style, energy link,
   // anchors and the collapse monitor (docs/plans/prediction/step-05).
   bool RunPredFeedbackTest();
   // INFINITE_PREDV2TEST, headless: Follow, Recall, Session Map, Your Moves, Section-Conditioned Drift,
   // and Play Like Me (DMD).
   bool RunPredV2Test();
}
