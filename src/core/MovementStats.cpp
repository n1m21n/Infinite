#include "MovementStats.h"
#include "../platform/AppPaths.h"

#include <miniz.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

namespace MovementStats
{

using MovementLog::Source;

// -----------------------------------------------------------------------------
// Pure functions
// -----------------------------------------------------------------------------

float SourceWeight(Source src, uint8_t flags, bool changedThisTick)
{
   switch (src)
   {
   case Source::Hand:
   case Source::Perf:
      return (flags & MovementLog::kCorrection) ? 3.0f : 1.0f;
   case Source::Gesture:
      return 0.2f;
   case Source::Modulator:
   case Source::Expression:
      return 0.1f;
   case Source::Prediction:
      // Own output counts a little when it moved; a prediction left sitting is "acceptance", which
      // is mostly absent attention and would close the feedback loop (README §5).
      return changedThisTick ? 0.1f : 0.0f;
   case Source::Other:
      return 0.0f;
   }
   return 0.0f;
}

double DecayFactor(double stampActive, double nowActive)
{
   if (nowActive <= stampActive)
      return 1.0;
   return std::exp2(-(nowActive - stampActive) / kHalfLifeActiveSec);
}

double EffectiveN(const ParamStats& s, double nowActive)
{
   return s.nEff * DecayFactor(s.activeSeconds, nowActive);
}

void SmoothedHist(const ParamStats& s, float out[kBins])
{
   constexpr int kRadius = 6;
   constexpr double kSigma = 2.0;
   double kernel[2 * kRadius + 1];
   for (int k = -kRadius; k <= kRadius; k++)
      kernel[k + kRadius] = std::exp(-0.5 * k * k / (kSigma * kSigma));

   double tmp[kBins];
   double total = 0.0;
   for (int i = 0; i < kBins; i++)
   {
      double acc = 0.0;
      for (int k = -kRadius; k <= kRadius; k++)
      {
         int j = i + k;
         // Reflect at the walls: knobs are often parked at 0 or 1, and truncating would thin them.
         if (j < 0)
            j = -j - 1;
         else if (j >= kBins)
            j = 2 * kBins - j - 1;
         acc += kernel[k + kRadius] * s.hist[j];
      }
      tmp[i] = acc;
      total += acc;
   }
   for (int i = 0; i < kBins; i++)
      out[i] = total > 0.0 ? static_cast<float>(tmp[i] / total) : 0.0f;
}

Derived Derive(const ParamStats& s, double dt)
{
   Derived d;

   // Observed range: 2nd/98th percentile of the smoothed landscape, interpolated inside a bin.
   float p[kBins];
   SmoothedHist(s, p);
   double total = 0.0;
   for (int i = 0; i < kBins; i++)
      total += p[i];
   if (total > 0.0)
   {
      auto percentile = [&](double q) {
         double cum = 0.0;
         for (int i = 0; i < kBins; i++)
         {
            const double next = cum + p[i];
            if (next >= q && p[i] > 0.0)
               return (i + (q - cum) / p[i]) / kBins;
            cum = next;
         }
         return 1.0;
      };
      d.rangeLo = percentile(0.02);
      d.rangeHi = percentile(0.98);
   }

   if (s.W <= 0.0)
      return d;

   const double mx = s.Sx / s.W;
   const double my = s.Sy / s.W;
   const double vx = std::max(0.0, s.Sxx / s.W - mx * mx);
   const double vy = std::max(0.0, s.Syy / s.W - my * my);
   const double cov = s.Sxy / s.W - mx * my;

   constexpr double kMinVar = 1e-8;
   constexpr double kMinPairWeight = 30.0;
   if (vx < kMinVar)
   {
      // A knob that never moved: nothing to regress. Report the ill-conditioned limit, flagged invalid.
      d.phi = kPhiMax;
      d.theta = -std::log(d.phi) / dt;
      d.mu = my;
      d.sigma = 0.0;
      return d;
   }

   const double phi = std::clamp(cov / vx, kPhiMin, kPhiMax);
   const double c = my - phi * mx;
   const double residVar = std::max(0.0, vy - 2.0 * phi * cov + phi * phi * vx);
   d.phi = phi;
   d.theta = -std::log(phi) / dt;
   d.mu = c / (1.0 - phi);
   d.sigma = std::sqrt(residVar * 2.0 * d.theta / (1.0 - phi * phi));
   d.valid = s.W >= kMinPairWeight;
   return d;
}

// -----------------------------------------------------------------------------
// Engine
// -----------------------------------------------------------------------------

void Engine::Reset()
{
   mKeys.clear();
   mProfiles.clear();
   mGridStarted = false;
   mNextGrid = 0;
   mTime = 0;
   mActiveClock = 0;
   mLastHandMoveT = -1e18;
   mPlaying = false;
   mWasActive = false;
}

void Engine::RegisterKey(const KeyId& id, const std::string& nodeType, const std::string& name, bool continuous)
{
   Runtime& r = mKeys[id];
   r.continuous = continuous;
   r.hasProfile = continuous;
   r.profile.nodeType = nodeType;
   r.profile.paramIndex = id.paramIndex;
   r.profile.name = name;
   r.profStats = nullptr;
}

void Engine::BeginSession()
{
   mGridStarted = false;
   mLastHandMoveT = -1e18;
   mPlaying = false;
   mWasActive = false;
   for (auto& [id, r] : mKeys)
   {
      r.holdValid = false;
      r.prevValid = false;
      r.changedSinceTick = false;
      r.lastHandT = -1e18;
      r.handN = 0;
   }
}

void Engine::BreakChains()
{
   for (auto& [id, r] : mKeys)
      r.prevValid = false;
}

void Engine::NoteJump()
{
   for (auto& [id, r] : mKeys)
   {
      r.holdValid = false;
      r.prevValid = false;
   }
}

void Engine::Baseline(const KeyId& id, double /*t*/, float pos)
{
   Runtime& r = mKeys[id];
   if (!r.continuous)
      return;
   r.holdValid = true;
   r.holdPos = std::clamp(pos, 0.0f, 1.0f);
   // A baseline is a value nobody chose (first sighting, patch load, undo): zero weight until moved.
   r.holdSrc = Source::Other;
   r.holdFlags = 0;
}

void Engine::Observe(const KeyId& id, double t, float pos, Source src, uint8_t flags)
{
   Runtime& r = mKeys[id];
   if (!r.continuous)
      return;
   pos = std::clamp(pos, 0.0f, 1.0f);

   if (src == Source::Hand || src == Source::Perf)
   {
      mLastHandMoveT = t;
      // A gap over 100 ms means a fresh grab: velocity starts from zero.
      if (t - r.lastHandT > 0.1)
         r.handN = 0;
      if (r.handN == 8)
      {
         for (int i = 1; i < 8; i++)
         {
            r.handT[i - 1] = r.handT[i];
            r.handPos[i - 1] = r.handPos[i];
         }
         r.handN = 7;
      }
      r.handT[r.handN] = t;
      r.handPos[r.handN] = pos;
      r.handN++;
      r.lastHandT = t;

      int i0 = r.handN - 1;
      while (i0 > 0 && t - r.handT[i0 - 1] <= 0.1)
         i0--;
      const double span = t - r.handT[i0];
      r.stats.releaseVel = (i0 < r.handN - 1 && span > 1e-3)
                              ? static_cast<float>((pos - r.handPos[i0]) / span)
                              : 0.0f;
   }

   r.holdValid = true;
   r.holdPos = pos;
   r.holdSrc = src;
   r.holdFlags = flags;
   r.changedSinceTick = true;
}

void Engine::Decay(ParamStats& s) const
{
   const double f = DecayFactor(s.activeSeconds, mActiveClock);
   if (f < 1.0)
   {
      s.W *= f;
      s.Sx *= f;
      s.Sy *= f;
      s.Sxx *= f;
      s.Sxy *= f;
      s.Syy *= f;
      s.nEff *= f;
      const float ff = static_cast<float>(f);
      for (float& h : s.hist)
         h *= ff;
   }
   s.activeSeconds = mActiveClock;
}

void Engine::AddSample(ParamStats& s, float x, float y, bool hasPair, double w)
{
   Decay(s);
   if (hasPair)
   {
      s.W += w;
      s.Sx += w * x;
      s.Sy += w * y;
      s.Sxx += w * x * x;
      s.Sxy += w * x * y;
      s.Syy += w * y * y;
   }
   int bin = static_cast<int>(y * kBins);
   bin = std::clamp(bin, 0, kBins - 1);
   s.hist[bin] += static_cast<float>(w);
   s.nEff += w;
}

void Engine::Tick(double tickTime)
{
   const bool active = mPlaying || (tickTime - mLastHandMoveT) <= kHandActiveWindowSec;
   if (!active)
   {
      // Idle gap: consecutive samples on either side are not neighbours.
      if (mWasActive)
         BreakChains();
      mWasActive = false;
      return;
   }
   mWasActive = true;
   mActiveClock += kGridDt;

   for (auto& [id, r] : mKeys)
   {
      if (!r.continuous || !r.holdValid)
         continue;

      const double w = SourceWeight(r.holdSrc, r.holdFlags, r.changedSinceTick);
      r.changedSinceTick = false;
      if (w > 0.0)
      {
         AddSample(r.stats, r.prevPos, r.holdPos, r.prevValid, w);
         if (r.hasProfile)
         {
            if (r.profStats == nullptr)
               r.profStats = &mProfiles[r.profile];
            AddSample(*r.profStats, r.prevPos, r.holdPos, r.prevValid, w);
         }
      }
      r.prevPos = r.holdPos;
      r.prevValid = true;
   }
}

void Engine::Advance(double t)
{
   mTime = t;
   if (!mGridStarted)
   {
      mGridStarted = true;
      mNextGrid = t;
   }
   if (t - mNextGrid > mMaxCatchUp)
   {
      // A stalled frame or a long idle gap: skip the ticks rather than replaying them.
      mNextGrid = t;
      BreakChains();
      mWasActive = false;
   }
   while (mNextGrid <= t + 1e-9)
   {
      Tick(mNextGrid);
      mNextGrid += kGridDt;
   }
}

const ParamStats* Engine::Find(const KeyId& id) const
{
   auto it = mKeys.find(id);
   return (it != mKeys.end() && it->second.stats.nEff > 0.0) ? &it->second.stats : nullptr;
}

const ParamStats* Engine::FindProfile(const ProfileKey& k) const
{
   auto it = mProfiles.find(k);
   return it != mProfiles.end() ? &it->second : nullptr;
}

// -----------------------------------------------------------------------------
// Persistence
// -----------------------------------------------------------------------------

namespace
{
constexpr char kMagic[6] = {'I', 'M', 'S', 'T', 'A', 'T'};
constexpr uint16_t kVersion = 1;
constexpr uint32_t kMaxEntries = 4u * 1024u * 1024u;
constexpr uint32_t kMaxString = 4096;

struct Writer
{
   std::vector<uint8_t> buf;
   template <class T> void put(const T& v)
   {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
      buf.insert(buf.end(), p, p + sizeof(T));
   }
   void putString(const std::string& s)
   {
      put(static_cast<uint32_t>(s.size()));
      buf.insert(buf.end(), s.begin(), s.end());
   }
   void putStats(const ParamStats& s)
   {
      put(s.W); put(s.Sx); put(s.Sy); put(s.Sxx); put(s.Sxy); put(s.Syy);
      buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(s.hist),
                 reinterpret_cast<const uint8_t*>(s.hist) + sizeof(s.hist));
      put(s.releaseVel); put(s.nEff); put(s.activeSeconds);
   }
};

struct Reader
{
   const uint8_t* p;
   const uint8_t* end;
   bool ok = true;
   template <class T> T get()
   {
      T v{};
      if (!ok || static_cast<size_t>(end - p) < sizeof(T))
      {
         ok = false;
         return v;
      }
      std::memcpy(&v, p, sizeof(T));
      p += sizeof(T);
      return v;
   }
   std::string getString()
   {
      const uint32_t n = get<uint32_t>();
      if (!ok || n > kMaxString || static_cast<size_t>(end - p) < n)
      {
         ok = false;
         return {};
      }
      std::string s(reinterpret_cast<const char*>(p), n);
      p += n;
      return s;
   }
   ParamStats getStats()
   {
      ParamStats s;
      s.W = get<double>(); s.Sx = get<double>(); s.Sy = get<double>();
      s.Sxx = get<double>(); s.Sxy = get<double>(); s.Syy = get<double>();
      if (!ok || static_cast<size_t>(end - p) < sizeof(s.hist))
      {
         ok = false;
         return s;
      }
      std::memcpy(s.hist, p, sizeof(s.hist));
      p += sizeof(s.hist);
      s.releaseVel = get<float>(); s.nEff = get<double>(); s.activeSeconds = get<double>();
      return s;
   }
};

bool StatsSane(const ParamStats& s)
{
   const double v[] = {s.W, s.Sx, s.Sy, s.Sxx, s.Sxy, s.Syy, s.nEff, s.activeSeconds, s.releaseVel};
   for (double x : v)
      if (!std::isfinite(x))
         return false;
   if (s.W < 0.0 || s.nEff < 0.0 || s.activeSeconds < 0.0)
      return false;
   for (float h : s.hist)
      if (!std::isfinite(h) || h < 0.0f)
         return false;
   return true;
}

// Verifies magic, version and the trailing CRC; positions `r` just after the header.
bool OpenBlob(const uint8_t* data, size_t size, Reader& r)
{
   if (size < sizeof(kMagic) + 2 + 4)
      return false;
   if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0)
      return false;
   uint32_t crc = 0;
   std::memcpy(&crc, data + size - 4, 4);
   if (static_cast<uint32_t>(mz_crc32(0, data, size - 4)) != crc)
      return false;
   r.p = data + sizeof(kMagic);
   r.end = data + size - 4;
   r.ok = true;
   return r.get<uint16_t>() == kVersion && r.ok;
}
} // namespace

std::vector<uint8_t> Engine::Serialize(const std::string& lastConsumed) const
{
   Writer w;
   w.buf.insert(w.buf.end(), kMagic, kMagic + sizeof(kMagic));
   w.put(kVersion);
   w.putString(lastConsumed);
   w.put(mActiveClock);

   uint32_t nKeys = 0;
   for (const auto& [id, r] : mKeys)
      if (r.stats.nEff > 0.0)
         nKeys++;
   w.put(nKeys);
   for (const auto& [id, r] : mKeys)
   {
      if (r.stats.nEff <= 0.0)
         continue;
      w.put(id.uid);
      w.put(id.paramIndex);
      w.putStats(r.stats);
   }

   w.put(static_cast<uint32_t>(mProfiles.size()));
   for (const auto& [k, s] : mProfiles)
   {
      w.putString(k.nodeType);
      w.put(k.paramIndex);
      w.putString(k.name);
      w.putStats(s);
   }

   const uint32_t crc = static_cast<uint32_t>(mz_crc32(0, w.buf.data(), w.buf.size()));
   w.put(crc);
   return w.buf;
}

bool Engine::Deserialize(const uint8_t* data, size_t size, std::string& lastConsumedOut)
{
   Reset();
   Reader r{nullptr, nullptr};
   if (!OpenBlob(data, size, r))
      return false;

   std::string lastConsumed = r.getString();
   const double clock = r.get<double>();
   const uint32_t nKeys = r.get<uint32_t>();
   if (!r.ok || nKeys > kMaxEntries || !std::isfinite(clock) || clock < 0.0)
      return false;

   std::unordered_map<KeyId, Runtime, KeyIdHash> keys;
   for (uint32_t i = 0; i < nKeys && r.ok; i++)
   {
      KeyId id;
      id.uid = r.get<uint64_t>();
      id.paramIndex = r.get<int32_t>();
      ParamStats s = r.getStats();
      if (!r.ok || !StatsSane(s) || s.activeSeconds > clock + 1e-6)
         return false;
      keys[id].stats = s;
   }

   const uint32_t nProf = r.get<uint32_t>();
   if (!r.ok || nProf > kMaxEntries)
      return false;
   std::unordered_map<ProfileKey, ParamStats, ProfileKeyHash> profiles;
   for (uint32_t i = 0; i < nProf && r.ok; i++)
   {
      ProfileKey k;
      k.nodeType = r.getString();
      k.paramIndex = r.get<int32_t>();
      k.name = r.getString();
      ParamStats s = r.getStats();
      if (!r.ok || !StatsSane(s) || s.activeSeconds > clock + 1e-6)
         return false;
      profiles[k] = s;
   }
   if (!r.ok || r.p != r.end)
      return false;

   mKeys = std::move(keys);
   mProfiles = std::move(profiles);
   mActiveClock = clock;
   lastConsumedOut = std::move(lastConsumed);
   return true;
}

std::string StatsPath(const std::string& logDir)
{
   return logDir + "/stats.bin";
}

static bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out)
{
   std::ifstream f(path, std::ios::binary);
   if (!f)
      return false;
   f.seekg(0, std::ios::end);
   const std::streamoff n = f.tellg();
   if (n <= 0 || n > (256LL << 20))
      return false;
   f.seekg(0, std::ios::beg);
   out.resize(static_cast<size_t>(n));
   f.read(reinterpret_cast<char*>(out.data()), n);
   return static_cast<bool>(f);
}

std::string ReadLastConsumed(const std::string& path)
{
   std::vector<uint8_t> bytes;
   if (!ReadWholeFile(path, bytes))
      return {};
   Reader r{nullptr, nullptr};
   if (!OpenBlob(bytes.data(), bytes.size(), r))
      return {};
   std::string s = r.getString();
   return r.ok ? s : std::string();
}

bool LoadStatsFile(const std::string& path, Engine& out, std::string& lastConsumedOut)
{
   std::vector<uint8_t> bytes;
   if (!ReadWholeFile(path, bytes))
   {
      out.Reset();
      return false;
   }
   return out.Deserialize(bytes.data(), bytes.size(), lastConsumedOut);
}

// -----------------------------------------------------------------------------
// Replay
// -----------------------------------------------------------------------------

bool Engine::ReplayFile(const std::string& path)
{
   const double savedCatchUp = mMaxCatchUp;
   mMaxCatchUp = 60.0; // event gaps while playing can be a bar long
   BeginSession();

   using MovementLog::Record;
   std::unordered_map<uint32_t, KeyId> ids;
   double t = 0.0;

   const bool ok = MovementLog::ReadFile(path, [&](const Record& rec) {
      // Advance to just before this record's time, then apply it: ticks up to that point see the
      // holds as they were, the next tick sees the change.
      auto step = [&](uint32_t dt_ms) {
         t += dt_ms / 1000.0;
         Advance(t - 1e-6);
      };
      switch (rec.type)
      {
      case Record::Type::Key:
      {
         step(rec.key.dt_ms);
         const KeyId id{rec.key.uid, rec.key.paramIndex};
         ids[rec.key.id] = id;
         RegisterKey(id, rec.key.typeName, rec.key.name, !(rec.key.isEnum || rec.key.isBool));
         break;
      }
      case Record::Type::Val:
      {
         step(rec.val.dt_ms);
         auto it = ids.find(rec.val.id);
         if (it != ids.end())
            Observe(it->second, t, rec.val.q / 65535.0f, rec.val.source, rec.val.flags);
         break;
      }
      case Record::Type::Bind:
         step(rec.bind.dt_ms);
         break;
      case Record::Type::Transport:
         step(rec.transport.dt_ms);
         SetPlaying(rec.transport.isPlaying);
         break;
      case Record::Type::Mark:
         step(rec.mark.dt_ms);
         switch (rec.mark.mark)
         {
         case MovementLog::Mark::PatchLoaded:
         case MovementLog::Mark::PatchNew:
         case MovementLog::Mark::Undo:
         case MovementLog::Mark::Redo:
            NoteJump();
            break;
         case MovementLog::Mark::SessionEnd:
            SetPlaying(false);
            break;
         case MovementLog::Mark::SessionStart:
            break;
         }
         break;
      }
      return true;
   });

   Advance(t);
   mMaxCatchUp = savedCatchUp;
   BeginSession();
   return ok;
}

Engine& Live()
{
   static Engine sLive;
   return sLive;
}

// -----------------------------------------------------------------------------
// Self test
// -----------------------------------------------------------------------------

namespace
{
#define STATS_CHECK(cond, ...)                                       \
   do                                                                \
   {                                                                 \
      if (!(cond))                                                   \
      {                                                              \
         std::printf("[FAIL] %s:%d ", __FILE__, __LINE__);           \
         std::printf(__VA_ARGS__);                                   \
         std::printf("\n");                                          \
         return false;                                               \
      }                                                              \
   } while (0)

bool NearlyEqual(const ParamStats& a, const ParamStats& b)
{
   if (a.W != b.W || a.Sx != b.Sx || a.Sy != b.Sy || a.Sxx != b.Sxx || a.Sxy != b.Sxy ||
       a.Syy != b.Syy || a.nEff != b.nEff || a.activeSeconds != b.activeSeconds ||
       a.releaseVel != b.releaseVel)
      return false;
   return std::memcmp(a.hist, b.hist, sizeof(a.hist)) == 0;
}

void WriteBytes(const std::string& path, const std::vector<uint8_t>& bytes)
{
   std::ofstream f(path, std::ios::binary | std::ios::trunc);
   f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
} // namespace

bool RunMovementStatsTest()
{
   std::printf("[MOVEMENT STATS TEST] Starting...\n");
   const KeyId kKey{42, 3};

   // 1. Source weights (README §5).
   STATS_CHECK(SourceWeight(Source::Hand, 0, true) == 1.0f, "hand weight");
   STATS_CHECK(SourceWeight(Source::Perf, 0, false) == 1.0f, "perf weight");
   STATS_CHECK(SourceWeight(Source::Hand, MovementLog::kCorrection, true) == 3.0f, "correction weight");
   STATS_CHECK(SourceWeight(Source::Gesture, 0, true) == 0.2f, "gesture weight");
   STATS_CHECK(SourceWeight(Source::Modulator, 0, true) == 0.1f, "modulator weight");
   STATS_CHECK(SourceWeight(Source::Expression, 0, true) == 0.1f, "expression weight");
   STATS_CHECK(SourceWeight(Source::Prediction, 0, true) == 0.1f, "prediction weight when it moved");
   STATS_CHECK(SourceWeight(Source::Prediction, 0, false) == 0.0f, "untouched prediction weight");
   STATS_CHECK(SourceWeight(Source::Other, 0, true) == 0.0f, "other weight");

   // 2. Ornstein-Uhlenbeck recovery: theta 0.5/s, mu 0.3, sigma 0.1, 20 simulated minutes.
   {
      constexpr double kTheta = 0.5, kMu = 0.3, kSigma = 0.1;
      Engine e;
      e.RegisterKey(kKey, "TestType", "cutoff", true);
      std::mt19937_64 rng(12345);
      std::normal_distribution<double> nd(0.0, 1.0);
      const double a = std::exp(-kTheta * kGridDt);
      const double sd = kSigma * std::sqrt((1.0 - a * a) / (2.0 * kTheta));
      double x = kMu;
      const int steps = static_cast<int>(20 * 60 / kGridDt);
      for (int i = 0; i < steps; i++)
      {
         const double t = i * kGridDt;
         x = kMu + (x - kMu) * a + sd * nd(rng);
         e.Observe(kKey, t, static_cast<float>(x), Source::Hand, 0);
         e.Advance(t);
      }
      const ParamStats* s = e.Find(kKey);
      STATS_CHECK(s != nullptr, "OU key has no stats");
      const Derived d = Derive(*s);
      std::printf("[OU] phi=%.4f theta=%.3f (0.5) mu=%.3f (0.3) sigma=%.3f (0.1) nEff=%.0f\n",
                  d.phi, d.theta, d.mu, d.sigma, s->nEff);
      STATS_CHECK(d.valid, "OU fit not valid");
      STATS_CHECK(std::abs(d.theta - kTheta) <= 0.2 * kTheta, "theta %.3f off by > 20%%", d.theta);
      STATS_CHECK(std::abs(d.mu - kMu) <= 0.05, "mu %.3f off by > 0.05", d.mu);
      STATS_CHECK(std::abs(d.sigma - kSigma) <= 0.25 * kSigma, "sigma %.3f off by > 25%%", d.sigma);
      // The profile slot got the same data.
      const ParamStats* prof = e.FindProfile({"TestType", 3, "cutoff"});
      STATS_CHECK(prof != nullptr && prof->nEff == s->nEff, "profile stats missing or different");
   }

   // 3. Two-spot dwell: a hand parking at 0.25, then at 0.75, leaves two peaks in p-hat.
   {
      Engine e;
      e.RegisterKey(kKey, "TestType", "cutoff", true);
      std::mt19937_64 rng(7);
      std::uniform_real_distribution<double> jit(-0.01, 0.01);
      const int perSpot = static_cast<int>(120 / kGridDt);
      for (int i = 0; i < 2 * perSpot; i++)
      {
         const double t = i * kGridDt;
         const double spot = i < perSpot ? 0.25 : 0.75;
         e.Observe(kKey, t, static_cast<float>(spot + jit(rng)), Source::Hand, 0);
         e.Advance(t);
      }
      const ParamStats* s = e.Find(kKey);
      STATS_CHECK(s != nullptr, "dwell key has no stats");
      float p[kBins];
      SmoothedHist(*s, p);
      float sum = 0;
      for (float v : p)
         sum += v;
      STATS_CHECK(std::abs(sum - 1.0f) < 1e-4f, "p-hat not normalised (%.5f)", sum);
      std::vector<int> peaks;
      for (int i = 0; i < kBins; i++)
      {
         const float l = i > 0 ? p[i - 1] : 0.0f;
         const float r = i + 1 < kBins ? p[i + 1] : 0.0f;
         if (p[i] > l && p[i] >= r && p[i] > 0.05f)
            peaks.push_back(i);
      }
      STATS_CHECK(peaks.size() == 2, "expected 2 dwell peaks, found %zu", peaks.size());
      STATS_CHECK(std::abs(peaks[0] - 16) <= 1 && std::abs(peaks[1] - 48) <= 1,
                  "peaks at bins %d,%d (expected 16,48)", peaks[0], peaks[1]);
      const Derived d = Derive(*s);
      STATS_CHECK(d.rangeLo > 0.15 && d.rangeLo < 0.35 && d.rangeHi > 0.65 && d.rangeHi < 0.85,
                  "range [%.3f, %.3f] not around the two spots", d.rangeLo, d.rangeHi);
   }

   // 4. Active-time gating and lazy forgetting.
   {
      Engine e;
      e.RegisterKey(kKey, "T", "p", true);
      e.Baseline(kKey, 0.0, 0.5f);
      e.Observe(kKey, 0.0, 0.5f, Source::Hand, 0);
      for (double t = 0.0; t <= 200.0; t += kGridDt)
         e.Advance(t);
      // Hand moved once at t = 0: active for 30 s, then idle.
      STATS_CHECK(std::abs(e.ActiveClock() - 30.0) < 0.3, "idle time counted (clock %.2f)", e.ActiveClock());
      e.SetPlaying(true);
      for (double t = 200.1; t <= 260.0; t += kGridDt)
         e.Advance(t);
      STATS_CHECK(std::abs(e.ActiveClock() - 90.0) < 0.5, "playing time not counted (clock %.2f)", e.ActiveClock());

      ParamStats s;
      s.nEff = 10.0;
      s.activeSeconds = 0.0;
      STATS_CHECK(std::abs(EffectiveN(s, kHalfLifeActiveSec) - 5.0) < 1e-9, "half-life is not 2 weeks of active time");
      STATS_CHECK(EffectiveN(s, 0.0) == 10.0, "no forgetting at zero elapsed active time");
   }

   // 5. phi clamp on a nearly-still knob, and skipping enum/bool keys.
   {
      Engine e;
      e.RegisterKey(kKey, "T", "p", true);
      const KeyId enumKey{43, 0};
      e.RegisterKey(enumKey, "T", "mode", false);
      std::mt19937_64 rng(3);
      std::uniform_real_distribution<double> jit(-0.0005, 0.0005);
      for (int i = 0; i < 3000; i++)
      {
         const double t = i * kGridDt;
         e.Observe(kKey, t, static_cast<float>(0.5 + jit(rng)), Source::Hand, 0);
         e.Observe(enumKey, t, 1.0f, Source::Hand, 0);
         e.Advance(t);
      }
      STATS_CHECK(e.Find(enumKey) == nullptr, "enum key was accumulated");
      const ParamStats* s = e.Find(kKey);
      STATS_CHECK(s != nullptr, "still knob has no stats");
      const Derived d = Derive(*s);
      STATS_CHECK(d.phi <= kPhiMax && d.phi >= kPhiMin, "phi %.5f outside clamp", d.phi);
      STATS_CHECK(std::isfinite(d.mu) && std::isfinite(d.sigma), "mu/sigma not finite for a still knob");
   }

   // 6. Untouched baseline and prediction hold add nothing; a prediction that moves adds 0.1.
   {
      Engine e;
      e.RegisterKey(kKey, "T", "p", true);
      e.SetPlaying(true);
      e.Baseline(kKey, 0.0, 0.5f);
      for (double t = 0.0; t < 20.0; t += kGridDt)
         e.Advance(t);
      STATS_CHECK(e.Find(kKey) == nullptr, "an untouched baseline was learned");
      e.Observe(kKey, 20.0, 0.6f, Source::Prediction, 0);
      e.Advance(20.0);
      for (double t = 20.1; t < 30.0; t += kGridDt)
         e.Advance(t);
      const ParamStats* s = e.Find(kKey);
      STATS_CHECK(s != nullptr, "prediction move left no stats");
      STATS_CHECK(std::abs(s->nEff - 0.1) < 1e-6, "prediction accumulated %.4f (expected only the one move, 0.1)", s->nEff);
   }

   // 7. Release velocity from the last 100 ms of a hand move.
   {
      Engine e;
      e.RegisterKey(kKey, "T", "p", true);
      for (int i = 0; i <= 10; i++)
         e.Observe(kKey, i * 0.016, 0.2f + 0.008f * i, Source::Hand, 0); // 0.5 pos/s
      e.Advance(0.2);
      // Force a stats slot to exist so Find() returns it.
      e.Advance(1.0);
      const ParamStats* s = e.Find(kKey);
      STATS_CHECK(s != nullptr, "moving knob has no stats");
      STATS_CHECK(std::abs(s->releaseVel - 0.5f) < 0.02f, "release velocity %.3f (expected 0.5)", s->releaseVel);
   }

   // 8. Persistence round trip and corruption handling.
   const std::string testDir = AppPaths::AppSupportDir() + "/movestats_test_" +
      std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
   std::error_code ec;
   std::filesystem::create_directories(testDir, ec);
   {
      Engine e;
      e.RegisterKey(kKey, "Filter", "cutoff", true);
      e.RegisterKey({44, 1}, "Filter", "cutoff", true);
      std::mt19937_64 rng(99);
      std::uniform_real_distribution<double> u(0.0, 1.0);
      for (int i = 0; i < 2000; i++)
      {
         const double t = i * kGridDt;
         e.Observe(kKey, t, static_cast<float>(u(rng)), Source::Hand, 0);
         e.Observe({44, 1}, t, static_cast<float>(u(rng)), Source::Modulator, 0);
         e.Advance(t);
      }
      const std::vector<uint8_t> blob = e.Serialize("20260101-000002");
      Engine back;
      std::string last;
      STATS_CHECK(back.Deserialize(blob.data(), blob.size(), last), "round trip failed");
      STATS_CHECK(last == "20260101-000002", "lastConsumed lost ('%s')", last.c_str());
      STATS_CHECK(back.KeyCount() == 2 && back.ProfileCount() == 2, "keys %zu profiles %zu", back.KeyCount(), back.ProfileCount());
      STATS_CHECK(NearlyEqual(*back.Find(kKey), *e.Find(kKey)), "key stats changed in round trip");
      STATS_CHECK(back.ActiveClock() == e.ActiveClock(), "active clock lost");
      STATS_CHECK(NearlyEqual(*back.FindProfile({"Filter", 1, "cutoff"}), *e.FindProfile({"Filter", 1, "cutoff"})),
                  "profile changed in round trip");

      // Corruption: every failure must leave an empty engine and never crash.
      auto expectRejected = [&](std::vector<uint8_t> bad, const char* what) {
         Engine x;
         std::string l;
         const bool ok = x.Deserialize(bad.data(), bad.size(), l);
         STATS_CHECK(!ok && x.KeyCount() == 0 && x.ProfileCount() == 0, "%s was accepted", what);
         return true;
      };
      std::vector<uint8_t> bad = blob;
      bad[bad.size() / 2] ^= 0x5A;
      if (!expectRejected(bad, "flipped byte")) return false;
      bad = blob;
      bad.resize(bad.size() / 2);
      if (!expectRejected(bad, "truncated file")) return false;
      if (!expectRejected({}, "empty file")) return false;
      bad = blob;
      bad[0] = 'X';
      if (!expectRejected(bad, "bad magic")) return false;
      // A structurally valid file whose count field lies must still be rejected (CRC recomputed).
      bad = blob;
      const size_t countAt = sizeof(kMagic) + 2 + 4 + 15 + 8; // header, lastConsumed string, clock
      bad[countAt + 3] = 0x7F;
      const uint32_t crc = static_cast<uint32_t>(mz_crc32(0, bad.data(), bad.size() - 4));
      std::memcpy(bad.data() + bad.size() - 4, &crc, 4);
      if (!expectRejected(bad, "inflated key count")) return false;

      // Through the file API.
      WriteBytes(StatsPath(testDir), blob);
      Engine fromFile;
      STATS_CHECK(LoadStatsFile(StatsPath(testDir), fromFile, last) && fromFile.KeyCount() == 2, "LoadStatsFile failed");
      STATS_CHECK(ReadLastConsumed(StatsPath(testDir)) == "20260101-000002", "ReadLastConsumed mismatch");
      WriteBytes(StatsPath(testDir), std::vector<uint8_t>(bad.begin(), bad.begin() + 20));
      STATS_CHECK(ReadLastConsumed(StatsPath(testDir)).empty(), "corrupt stats.bin yielded a lastConsumed");
      std::filesystem::remove(StatsPath(testDir), ec);
      Engine missing;
      STATS_CHECK(!LoadStatsFile(StatsPath(testDir), missing, last), "missing stats.bin reported loaded");
   }

   // 9. Retention gate: a session file whose rows are not in stats.bin must survive the cap.
   {
      const uint64_t savedCap = MovementLog::GetRetentionCapBytes();
      MovementLog::SetRetentionCapBytes(1024 * 1024); // 1 MB
      const char* names[3] = {"20260101-000000.mlog.z", "20260101-000001.mlog.z", "20260101-000002.mlog.z"};
      auto writeFiles = [&]() {
         for (const char* n : names)
            WriteBytes(testDir + "/" + n, std::vector<uint8_t>(600 * 1024, 0x55));
      };
      auto exists = [&](int i) { return std::filesystem::exists(testDir + "/" + names[i]); };
      auto writeStats = [&](const std::string& lastConsumed) {
         Engine e;
         const std::vector<uint8_t> blob = e.Serialize(lastConsumed);
         WriteBytes(StatsPath(testDir), blob);
      };

      // No stats.bin at all: nothing has been consumed, nothing may go.
      writeFiles();
      MovementLog::EnforceRetention(testDir);
      STATS_CHECK(exists(0) && exists(1) && exists(2), "files deleted although no stats.bin exists");

      // File 0 consumed only: 0 goes, 1 and 2 stay even though the folder is still over the cap.
      writeStats("20260101-000000");
      MovementLog::EnforceRetention(testDir);
      STATS_CHECK(!exists(0), "consumed file not deleted");
      STATS_CHECK(exists(1) && exists(2), "unconsumed file deleted under the cap");

      // The same file goes once lastConsumed has passed it; the newest, still unconsumed, survives.
      writeFiles();
      writeStats("20260101-000001");
      MovementLog::EnforceRetention(testDir);
      STATS_CHECK(!exists(0) && !exists(1), "files at or older than lastConsumed not pruned");
      STATS_CHECK(exists(2), "newest unconsumed file was pruned");
      STATS_CHECK(std::filesystem::exists(StatsPath(testDir)), "stats.bin was pruned");

      MovementLog::SetRetentionCapBytes(savedCap);
   }

   std::filesystem::remove_all(testDir, ec);
   std::printf("[MOVEMENT STATS TEST] PASS\n");
   std::printf("INFINITE_MOVESTATSTEST: OK\n");
   return true;
}

} // namespace MovementStats
