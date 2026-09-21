#pragma once

#include "MovementLog.h"
#include "ParamRoles.h"

#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Online per-param statistics for the prediction modulator (docs/plans/prediction README §3.3).
// Everything is O(1) per key per 100 ms grid tick and runs on the main thread. Forgetting is lazy and
// measured in *active* time (transport playing, or a hand move in the last 30 s), never wall-clock.
namespace MovementStats
{
   constexpr int kBins = 64;
   constexpr double kGridDt = 0.1;           // AR(1) grid, seconds
   constexpr double kHalfLifeActiveSec = 14.0 * 24.0 * 3600.0; // "H": 2 weeks of active time
   constexpr double kHandActiveWindowSec = 30.0;
   constexpr double kPhiMin = 0.5;
   constexpr double kPhiMax = 0.999;

   // --- Source pools (user profile) ---------------------------------------------------------
   // The four kinds of evidence a parameter's landscape can be built from, partitioned by *who
   // authored the motion and when*, not by which code path wrote the value:
   //
   //   DELIBERATE  a human, now, making a decision   - release points, held positions, typed
   //               numbers, Shift-grab corrections, a hand on a MIDI controller
   //   EXPLORATORY a human, now, still searching     - transit motion inside a drag
   //   AUTO        a rule the human set earlier      - LFO, expression, envelope, arpeggiator
   //   REPLAY      a recording of the human's past   - gesture playback, clip automation
   //
   // The split exists because a pool's influence must not be a function of how long it ran: a
   // patch left running overnight accumulates ~100x the samples of a real performance, so a
   // single summed pool is authored by whichever source had the most wall time. Each pool
   // normalises to itself and the *mix* between them is decided by predictive skill - see
   // PoolShares.
   enum Pool
   {
      kPoolDeliberate = 0,
      kPoolExploratory,
      kPoolAuto,
      kPoolReplay,
      kNumPools
   };

   // Where a fresh user starts, before any evidence exists to move the shares (README §6's
   // cold-start ladder applied to the profile itself). HUMAN 60 / AUTO 25 / REPLAY 15, with the
   // human half split 2:1 toward decisions over searching.
   constexpr double kPoolPrior[kNumPools] = { 0.40, 0.20, 0.25, 0.15 };
   // Softmax temperature over mean log-loss. 1.0 in nats is exact Bayesian model averaging;
   // larger flattens the mix, smaller makes it winner-take-all.
   constexpr double kPoolTau = 1.0;
   // Prequential score forgetting, in active seconds. Much shorter than kHalfLifeActiveSec: the
   // shares are meant to track how you are playing lately, not average over a fortnight.
   constexpr double kScoreHalfLifeSec = 4.0 * 3600.0;
   // A machine source may only accumulate while a human is demonstrably present. Transport
   // running is not presence - that is exactly the "left it going overnight" case. Human
   // sources are never gated (they cannot happen without a human by definition).
   constexpr double kPresenceWindowSec = 30.0 * 60.0;

   // Which pool a logged sample belongs to. `changed` is Runtime::changedSinceTick: a hand
   // sample that did NOT move this tick is a held position, i.e. a decision to leave it there,
   // which is the single most informative thing the landscape ever sees.
   Pool ClassifyPool(MovementLog::Source src, uint8_t flags, bool changed);
   bool PoolIsHuman(Pool p);

   // The emergent mix for one key: how much of its model each pool currently authors.
   struct PoolShares
   {
      float share[kNumPools] = {};   // sums to 1
      float maturity[kNumPools] = {};// n/(n+kN0) per pool, what gates a thin pool's claim
      double n[kNumPools] = {};      // raw n_eff per pool
      bool valid = false;            // false when the key has no data in any pool
   };

   struct ParamStats
   {
      // AR(1) sufficient statistics on the 10 Hz grid (x = x_t, y = x_{t+1}), exponentially forgotten.
      double W = 0, Sx = 0, Sy = 0, Sxx = 0, Sxy = 0, Syy = 0;
      float hist[kBins] = {};    // dwell landscape, same forgetting
      float releaseVel = 0;      // last hand velocity (pos/s) over the final <=100 ms of a move
      double nEff = 0;           // sum of weights = confidence
      double nPred = 0;          // the part of nEff that came from prediction rows (collapse monitor)
      // Global active-clock reading at the last update: the sums above are "as of" this instant.
      double activeSeconds = 0;
   };

   struct Derived
   {
      bool valid = false;        // enough pairs to trust phi/theta/mu/sigma
      double phi = 0, theta = 0, mu = 0, sigma = 0;
      double rangeLo = 0, rangeHi = 1;
   };

   // Training weight of one grid sample (README §5). `changedThisTick` is false for a held value.
   float SourceWeight(MovementLog::Source src, uint8_t flags, bool changedThisTick);

   // Pure functions of a ParamStats.
   Derived Derive(const ParamStats& s, double dt = kGridDt);
   // Gaussian-smoothed (sigma = 2 bins), normalised so the bins sum to 1. All zero when hist is empty.
   void SmoothedHist(const ParamStats& s, float out[kBins]);
   // The same smoothing on a bare histogram; `wrap` makes the ends neighbours (hue).
   void SmoothBins(const float* h, float out[kBins], bool wrap);
   // Value of the decay factor 2^(-(now - stamp)/H) to apply to a ParamStats read at `nowActive`.
   double DecayFactor(double stampActive, double nowActive);
   // n_eff as seen at `nowActive` (decayed).
   double EffectiveN(const ParamStats& s, double nowActive);

   // Rung weights of the fallback ladder (README §6). Everything that is a tunable lives here.
   constexpr double kN0 = 30.0;              // independent samples for a level to carry half the weight
   constexpr double kRoleN0Mult = 3.0;       // role prior is weaker: larger N0
   constexpr double kFamilyN0Mult = 6.0;
   constexpr float kAnchorWidth = 0.04f;     // sd of the anchor peak, fader space
   // Dwell budget. A hold is not cleared when the hand lets go (there is no release event at the
   // stats layer), so Tick keeps re-absorbing the *same* resting position at full hand weight for
   // as long as the transport runs. Unbounded, that makes "where I parked it and walked away" the
   // dominant fact about every knob and buries the gesture you actually performed. Full weight for
   // the first kDwellFullTicks, then an exponential fade, so settling still counts - it just cannot
   // count forever.
   constexpr int kDwellFullTicks = 40;        // 4 s at kGridDt
   constexpr double kDwellDecayTicks = 100.0; // e-fold of the fade after that
   constexpr double kAutoCutWindowSec = 2.0 * 3600.0;   // active time
   constexpr double kAutoCutDrop = 0.25;                // relative entropy loss that trips the cut
   constexpr double kMonitorPeriodSec = 10.0;           // active time between monitor checks

   // What the stats layer needs to know about a key beyond its name: the category it lives in (for its
   // role) and its declared range and fader curve (to convert a fader position to a physical value).
   struct KeyMeta
   {
      std::string category;
      float minValue = 0.0f, maxValue = 1.0f;
      float (*posToValue)(float, float, float) = nullptr;
      float (*valueToPos)(float, float, float) = nullptr;
      bool hasCurve = false;   // a curve exists but its function is not known (replaying a file)
   };

   // The fallback ladder blended for one key: rung 1 (this key), 2 (profile), 2b (role), 2d (family),
   // 2c (you, speed only), 3 (community, not built) and 4 (the anchor).
   struct Blend
   {
      float p[kBins] = {};       // dwell landscape, bins sum to 1
      float theta = 0.5f, sigma = 0.1f;
      float lo = 0.0f, hi = 1.0f;
      double w1 = 0, w2 = 0, w2b = 0, w2d = 0, w3 = 0, wYou = 0, w4 = 1;
      double nKey = 0;           // raw n_eff of the key itself
      // The same count after the AR(1) independence deflation - i.e. how many *independent*
      // moves this key really contributes, which is what w1 (and therefore every confidence
      // readout) is actually computed from. nKey alone is dwell-inflated: a knob left parked
      // while the transport runs accrues thousands of identical samples that carry almost no
      // information, and showing that number next to a confidence percentage was the headline
      // dishonesty in the Drift node's readout.
      double nKeyIndependent = 0;
   };

   struct Monitor
   {
      double entropy = 0;        // nats, of the key's own smoothed landscape
      double sigma = 0;          // fitted OU sigma
      double predShare = 0;      // share of n_eff that came from prediction rows
      bool cut = false;          // prediction weight forced to 0 until the next hand move
   };

   struct ProfileKey
   {
      std::string nodeType;
      int32_t paramIndex = 0;
      std::string name;
      bool operator==(const ProfileKey& o) const
      {
         return paramIndex == o.paramIndex && nodeType == o.nodeType && name == o.name;
      }
   };

   struct KeyId
   {
      uint64_t uid = 0;
      int32_t paramIndex = 0;
      bool operator==(const KeyId& o) const { return uid == o.uid && paramIndex == o.paramIndex; }
   };

   struct KeyIdHash
   {
      size_t operator()(const KeyId& k) const
      {
         return std::hash<uint64_t>()(k.uid) ^ (std::hash<int32_t>()(k.paramIndex) * 0x9E3779B1u);
      }
   };

   struct ProfileKeyHash
   {
      size_t operator()(const ProfileKey& k) const
      {
         return std::hash<std::string>()(k.nodeType) ^ (std::hash<std::string>()(k.name) << 1) ^
                (std::hash<int32_t>()(k.paramIndex) * 0x9E3779B1u);
      }
   };

   // --- Step 7: v2 Prediction Modes & Modeling ---

   // 7a Follow: Lagged ridge regression over velocities (Delta x).
   struct FollowFit
   {
      bool valid = false;
      int tauTicks = 0;          // lag in 100 ms grid ticks (tau >= 0)
      float tauSec = 0.0f;       // tau in seconds
      float beta = 0.0f;         // regression coefficient
      float c = 0.0f;            // offset
      float r2 = 0.0f;           // held-out R^2 / goodness of fit
      float corrVel = 0.0f;      // velocity cross-correlation at best lag
   };

   // 7b Recall: Per-bar summary index and nearest segment retrieval.
   struct BarSummary
   {
      int barIndex = 0;
      double beat = 0.0;
      struct KeySummary
      {
         KeyId id;
         float meanPos = 0.5f;
         float slopePos = 0.0f;
      };
      std::vector<KeySummary> keys;
   };

   struct RecallMatch
   {
      bool found = false;
      int matchedBar = -1;
      float similarity = 0.0f;
   };

   // 7c Session Map: Cosine similarity and Foote novelty segmentation.
   struct SessionSection
   {
      int startBar = 0;
      int endBar = 0;
      std::string label;         // "A", "B", "A'", "C", etc.
      int sectionId = 0;
   };

   struct SessionMap
   {
      static constexpr size_t kMaxColumns = 2048;
      int numBars = 0;
      int downsampleFactor = 1;
      std::vector<std::vector<float>> columns; // L2-normalized parameter vectors
      std::vector<float> similarityMatrix;     // flattened numBars x numBars cosine similarity
      std::vector<float> noveltyCurve;         // Foote novelty curve
      std::vector<SessionSection> sections;
   };

   // 7d Your Moves: PCA on signed deltas (Delta M).
   struct MovesPCA
   {
      bool valid = false;
      int numComponents = 0;
      std::vector<KeyId> keys;
      std::vector<std::vector<float>> W;        // [component][key]
      std::vector<float> explainedVarianceRatio;
      float totalVariance = 0.0f;
   };

   // 7g Play Like Me: Dynamic Mode Decomposition (DMD) with spectral radius <= 1.
   struct DMDFit
   {
      bool valid = false;
      int rank = 0;
      std::vector<KeyId> keys;
      // Affine transition: x2 = A[:, 0:rank] * x1 + A[:, rank] (row-major, rank rows x (rank+1)
      // columns - the last column is the learned bias/equilibrium term, not part of the linear
      // map, so it isn't touched by the stability scale-down FitDMD applies to the rest).
      std::vector<float> A;
      float spectralRadius = 1.0f;
      // Per-dimension one-step residual std from the training fit - how much real data wandered
      // around what the affine map alone predicts. Step() uses this to keep free-run alive
      // instead of settling onto a silent fixed point once the deterministic part decays.
      std::vector<float> noiseStd;
      // rngState: pass a persistent seed (e.g. one field owned by the caller) to keep adding
      // residual-scale noise across calls; pass nullptr for the old deterministic-only behavior.
      void Step(std::vector<float>& x, uint64_t* rngState = nullptr) const;
   };

   // The statistics engine. Instantiable so tests and offline replay can run one in isolation;
   // the live app uses Live().
   class Engine
   {
   public:
      // maxCatchUpSec: grid ticks older than this behind `t` are skipped, not replayed. Live keeps it
      // small (a stalled frame must not spin); offline replay raises it to span idle event gaps.
      explicit Engine(double maxCatchUpSec = 1.0) : mMaxCatchUp(maxCatchUpSec) {}

      void Reset();

      // Declare a key. Enum/bool keys (continuous == false) are skipped by every later call.
      void RegisterKey(const KeyId& id, const std::string& nodeType, const std::string& name, bool continuous,
                       const KeyMeta& meta = KeyMeta());

      // The value of a key at time t without treating it as a move (first sighting, after a mark).
      void Baseline(const KeyId& id, double t, float pos);
      // A real change (a logged VAL row).
      void Observe(const KeyId& id, double t, float pos, MovementLog::Source src, uint8_t flags);

      // Start a fresh time base (live session start, or each replayed file): holds and AR chains reset,
      // learned statistics and the active clock carry over.
      void BeginSession();
      // Marks that jump values (patch load/new, undo, redo) break the AR chain and invalidate holds.
      void NoteJump();
      void SetPlaying(bool playing) { mPlaying = playing; }

      // Run every 100 ms grid tick up to time t.
      void Advance(double t);

      // The blended model of a key. `anchor` < 0 means the key has none (a flat fallback).
      // If `sectionId` >= 0, incorporates section-conditioned dwell landscape (7e).
      void ComputeBlend(const KeyId& id, float anchor, Blend& out, int sectionId = -1) const;
      // How actively the hand is moving right now, in [0,1] (README §6): 0.7 role + 0.3 everything.
      // O(1); only Hand and Perf writes count, so predictions can never excite themselves.
      float HandEnergy(const KeyId& id) const;
      // Collapse monitors. Returns false for an unknown key.
      bool GetMonitor(const KeyId& id, Monitor& out) const;
      // The emergent source mix for a key (the "user profile" at that knob). Returns false for an
      // unknown key; `out.valid` is false when the key exists but no pool has data yet.
      bool GetPoolShares(const KeyId& id, PoolShares& out) const;
      // The same mix pooled over every key - the profile readout the node shows. Always succeeds;
      // falls back to kPoolPrior when nothing has been learned.
      void GetGlobalPoolShares(PoolShares& out) const;
      // Called (main thread) when a key's prediction weight is auto-cut.
      void SetAutoCutCallback(std::function<void(const KeyId&)> cb) { mOnAutoCut = std::move(cb); }
      // Training weight of a prediction row that moved (README §5: 0.1 by default; tests raise it).
      void SetPredictionWeight(float w) { mPredWeight = w; }
      // Number of rungs feeding the you-record, for tests.
      double YouN() const { return mYou.ar.nEff; }
      const ParamStats* FindRole(const std::string& role) const;
      const ParamStats* FindFamily(const std::string& family) const;

      // Lookup for readers. nullptr when the key has no stats.
      const ParamStats* Find(const KeyId& id) const;
      const ParamStats* FindProfile(const ProfileKey& k) const;
      // How many grid samples this key has actually accumulated - what FitDMD's own N < 20
      // rejection (see FitDMD) checks. A private, per-node Engine (Predictive Modulator) uses
      // this to report real learning progress instead of a counter that isn't the number
      // FitDMD actually looks at.
      size_t HistoryLengthFor(const KeyId& id) const
      {
         const Runtime* r = FindRuntime(id);
         return r != nullptr ? r->posHistory.size() : 0;
      }
      double ActiveClock() const { return mActiveClock; }
      size_t KeyCount() const { return mKeys.size(); }
      size_t ProfileCount() const { return mProfiles.size(); }

      // --- v2 Mode Fits & Queries ---
      // 7a Follow fit between leader and follower
      FollowFit FitFollow(const KeyId& follower, const KeyId& leader, int maxLagTicks = 20, float ridgeLambda = 1e-4f) const;
      // 7b Recall index lookup
      void RecordBarSummary(int barIndex, double beat);
      RecallMatch SearchRecallIndex(const std::vector<BarSummary::KeySummary>& query, int queryBars = 1) const;
      const std::vector<BarSummary>& GetBarSummaries() const { return mBarSummaries; }
      // 7c Session Map computation
      void ComputeSessionMap(SessionMap& out, int noveltyKernelHalfSize = 4) const;
      int CurrentSectionId() const { return mCurrentSectionId; }
      void SetCurrentSectionId(int secId) { mCurrentSectionId = secId; }
      // 7d Delta PCA
      MovesPCA ComputeDeltaPCA(int maxComponents = 4) const;
      // 7g Dynamic Mode Decomposition
      DMDFit FitDMD(int maxRank = 4) const;

      // Persistence. Load returns false (leaving the engine empty) on any corruption.
      std::vector<uint8_t> Serialize(const std::string& lastConsumed) const;
      bool Deserialize(const uint8_t* data, size_t size, std::string& lastConsumedOut);

      // Replay a session file (.mlog or .mlog.z) into this engine as if it had run live.
      bool ReplayFile(const std::string& path);

   private:
      struct Runtime
      {
         ParamStats stats;         // legacy combined pool: still what rungs 2/2b/2d and the
                                   // collapse monitor read, and what n_eff/confidence reports
         ParamStats pools[kNumPools];  // the same evidence, partitioned by author (see Pool)
         // Prequential log-loss per pool, scored against hand samples *before* they are absorbed,
         // so a pool is never graded on data it has already seen. Exponentially forgotten at
         // kScoreHalfLifeSec; scoreW is the matching weight so the mean stays well-defined.
         double scoreSum[kNumPools] = {};
         double scoreW = 0;
         double scoreClock = 0;    // active-clock stamp for lazy forgetting of the scores
         ProfileKey profile;
         bool hasProfile = false;
         ParamStats* profStats = nullptr; // cached slot in mProfiles (node-based, so stable)
         // Role and family levels (README §6 rungs 2b/2d), resolved once at RegisterKey.
         KeyMeta meta;
         ParamRoles::Role role;
         ParamRoles::Unit famUnit = ParamRoles::Unit::Pos;
         bool famShared = false;
         float centsPerUnit = 1.0f;   // 100 for a semitone knob, 1 for cents
         ParamStats* roleStats = nullptr;
         ParamStats* famStats = nullptr;
         bool continuous = true;
         // Hand energy: last hand write, to turn a position change into a speed.
         float lastObsHandPos = 0;
         double lastObsHandT = -1e18;
         // Collapse monitor (all in active time).
         bool cut = false;
         bool refValid = false;
         bool handSinceRef = false;
         double refEntropy = 0, refClock = 0, nextCheck = 0;
         // Hold state (not persisted).
         bool holdValid = false;
         float holdPos = 0;
         MovementLog::Source holdSrc = MovementLog::Source::Hand;
         uint8_t holdFlags = 0;
         bool changedSinceTick = false;
         // Ticks this hold has been re-absorbed without the value changing - what the dwell
         // budget above is spent against. Reset on every real Observe, so each fresh visit to a
         // resting position gets its own full budget.
         int holdTicks = 0;
         // AR chain.
         bool prevValid = false;
         float prevPos = 0;
         // Hand-move memory for release velocity and the 30 s window.
         double lastHandT = -1e18;
         double handT[8] = {};
         float handPos[8] = {};
         int handN = 0;

         // --- v2 Runtime History & Section Accumulators ---
         static constexpr size_t kHistoryCap = 512; // ~51.2 s of 100 ms ticks
         std::vector<float> posHistory;
         std::vector<float> deltaHistory;
         std::unordered_map<int, std::vector<float>> sectionHist; // 64 bins per section
         std::unordered_map<int, double> sectionNEff;
      };

      void Tick(double tickTime);
      void BreakChains();
      // Grade every pool on `bin` using only what it already holds, then forget a little.
      void ScorePools(Runtime& r, int bin);
      // Rung 1 of the ladder, assembled from the pools at their emergent shares: the landscape is
      // the share-weighted mix, the AR cadence comes from the human pools alone.
      void MixPools(const Runtime& r, ParamStats& out) const;
      void Decay(ParamStats& s) const;
      void AddSample(ParamStats& s, float x, float y, bool hasPair, double w, int bin, double wPred = 0.0);
      void UpdateEnergy(double t);
      void RunMonitor(const KeyId& id, Runtime& r);
      float ToUnit(const Runtime& r, float pos, ParamRoles::Unit u) const;
      // Rebin a histogram kept in unit `u` into a target key's fader space. Returns the fraction of the
      // mass that landed inside the target's declared range (the rest is dropped and renormalised away).
      double RebinToTarget(const float* hist, ParamRoles::Unit u, const Runtime& target, float* out) const;
      const Runtime* FindRuntime(const KeyId& id) const;

      std::unordered_map<KeyId, Runtime, KeyIdHash> mKeys;
      std::unordered_map<ProfileKey, ParamStats, ProfileKeyHash> mProfiles;
      std::unordered_map<std::string, ParamStats> mRoles;     // by role name, in the role's unit
      std::unordered_map<std::string, ParamStats> mFamilies;  // by family name
      // "You" (rung 2c): how you move, pooled over every hand-moved key in fader space. Never a landscape.
      struct You
      {
         ParamStats ar;                 // AR sums only; hist unused
         double speedSum = 0, speedW = 0;   // weighted mean of |dpos|/dt while moving
         float pauses[12] = {};         // log-spaced pause lengths between hand writes (0.15 s .. 300 s)
      } mYou;
      // Live hand energy (not persisted).
      struct Energy { double ema = 0, acc = 0; };
      Energy mEnergyAll;
      std::unordered_map<std::string, Energy> mEnergyRole;
      double mLastEnergyT = -1e18;
      float mPredWeight = 0.1f;
      std::function<void(const KeyId&)> mOnAutoCut;
      double mMaxCatchUp;
      bool mGridStarted = false;
      double mNextGrid = 0;
      double mTime = 0;
      double mActiveClock = 0;     // seconds of active time absorbed so far
      double mLastHandMoveT = -1e18;
      // Wall of the last *human* sample, in active time: what kPresenceWindowSec is measured from.
      double mLastPresenceActive = -1e18;
      bool mPlaying = false;
      bool mWasActive = false;

      // --- v2 Storage ---
      std::vector<BarSummary> mBarSummaries;
      int mCurrentSectionId = 0;
   };

   Engine& Live();

   // Path of stats.bin inside a log folder.
   std::string StatsPath(const std::string& logDir);
   // Just the lastConsumed field of a stats.bin ("" when missing or unreadable). Cheap: header only.
   std::string ReadLastConsumed(const std::string& path);
   // Read stats.bin; returns false (engine left empty) on absence/corruption.
   bool LoadStatsFile(const std::string& path, Engine& out, std::string& lastConsumedOut);

   bool RunMovementStatsTest();
}
