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
      void ComputeBlend(const KeyId& id, float anchor, Blend& out) const;
      // How actively the hand is moving right now, in [0,1] (README §6): 0.7 role + 0.3 everything.
      // O(1); only Hand and Perf writes count, so predictions can never excite themselves.
      float HandEnergy(const KeyId& id) const;
      // Collapse monitors. Returns false for an unknown key.
      bool GetMonitor(const KeyId& id, Monitor& out) const;
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
      double ActiveClock() const { return mActiveClock; }
      size_t KeyCount() const { return mKeys.size(); }
      size_t ProfileCount() const { return mProfiles.size(); }

      // Persistence. Load returns false (leaving the engine empty) on any corruption.
      std::vector<uint8_t> Serialize(const std::string& lastConsumed) const;
      bool Deserialize(const uint8_t* data, size_t size, std::string& lastConsumedOut);

      // Replay a session file (.mlog or .mlog.z) into this engine as if it had run live.
      bool ReplayFile(const std::string& path);

   private:
      struct Runtime
      {
         ParamStats stats;
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
         // AR chain.
         bool prevValid = false;
         float prevPos = 0;
         // Hand-move memory for release velocity and the 30 s window.
         double lastHandT = -1e18;
         double handT[8] = {};
         float handPos[8] = {};
         int handN = 0;
      };

      void Tick(double tickTime);
      void BreakChains();
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
      bool mPlaying = false;
      bool mWasActive = false;
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
