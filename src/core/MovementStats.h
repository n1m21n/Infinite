#pragma once

#include "MovementLog.h"

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
   // Value of the decay factor 2^(-(now - stamp)/H) to apply to a ParamStats read at `nowActive`.
   double DecayFactor(double stampActive, double nowActive);
   // n_eff as seen at `nowActive` (decayed).
   double EffectiveN(const ParamStats& s, double nowActive);

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
      void RegisterKey(const KeyId& id, const std::string& nodeType, const std::string& name, bool continuous);

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
         bool continuous = true;
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
      void AddSample(ParamStats& s, float x, float y, bool hasPair, double w);

      std::unordered_map<KeyId, Runtime, KeyIdHash> mKeys;
      std::unordered_map<ProfileKey, ParamStats, ProfileKeyHash> mProfiles;
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
