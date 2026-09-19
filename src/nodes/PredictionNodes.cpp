#include "nodes/PredictionNodes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
   constexpr float kDriftCap = 0.05f;     // max |eta U' dt| per step
   constexpr float kEps = 1e-3f;          // log floor, in density units
   constexpr int kModelRefreshFrames = 30;
   constexpr int kSlotDropFrames = 300;

   // ---- RNG: splitmix64 seeds an xorshift64*, ~free and trivially copyable so the ghost can fork it.
   uint64_t SplitMix(uint64_t& z)
   {
      z += 0x9E3779B97F4A7C15ull;
      uint64_t r = z;
      r = (r ^ (r >> 30)) * 0xBF58476D1CE4E5B9ull;
      r = (r ^ (r >> 27)) * 0x94D049BB133111EBull;
      return r ^ (r >> 31);
   }
   uint64_t NextRaw(uint64_t& s)
   {
      s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
      return s * 0x2545F4914F6CDD1Dull;
   }
   float Uniform01(uint64_t& s) // (0,1)
   {
      return ((float)(NextRaw(s) >> 40) + 0.5f) * (1.0f / 16777216.0f);
   }
   float Gauss(uint64_t& s)
   {
      const float u1 = Uniform01(s), u2 = Uniform01(s);
      return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
   }
   uint64_t SeedFor(int seed, const ParamKey& k)
   {
      uint64_t z = (uint64_t)(uint32_t)seed * 0x100000001B3ull ^ (k.uid * 0x9E3779B97F4A7C15ull) ^
                   ((uint64_t)(uint32_t)k.paramIndex << 32);
      uint64_t s = SplitMix(z);
      return s != 0 ? s : 1;
   }

   // Density (bins sum to 1, so a uniform landscape reads 1.0) at position x, linear between bin centres.
   float Density(const float* p, float x)
   {
      const float f = std::clamp(x * DriftNode::kBins - 0.5f, 0.0f, (float)(DriftNode::kBins - 1));
      const int i = std::min((int)f, DriftNode::kBins - 2);
      const float a = f - (float)i;
      return (p[i] * (1.0f - a) + p[i + 1] * a) * (float)DriftNode::kBins;
   }
   float Potential(const float* p, float x) { return -std::log(Density(p, x) + kEps); }

   // ---- base64 (frozen profile text) ----
   const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   std::string B64Encode(const uint8_t* d, size_t n)
   {
      std::string out;
      for (size_t i = 0; i < n; i += 3)
      {
         const uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
         out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63];
         out += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
         out += i + 2 < n ? kB64[v & 63] : '=';
      }
      return out;
   }
   bool B64Decode(const std::string& s, std::vector<uint8_t>& out)
   {
      out.clear();
      uint32_t acc = 0; int bits = 0;
      for (char c : s)
      {
         if (c == '=') break;
         const char* p = std::strchr(kB64, c);
         if (p == nullptr || c == '\0') return false;
         acc = acc << 6 | (uint32_t)(p - kB64); bits += 6;
         if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(acc >> bits)); }
      }
      return true;
   }

   std::vector<std::string> Split(const std::string& s, char sep)
   {
      std::vector<std::string> out;
      size_t a = 0;
      while (a <= s.size())
      {
         size_t b = s.find(sep, a);
         if (b == std::string::npos) b = s.size();
         if (b > a) out.push_back(s.substr(a, b - a));
         a = b + 1;
      }
      return out;
   }
   bool ParseKey(const std::string& s, ParamKey& k)
   {
      unsigned long long uid = 0; int idx = 0;
      if (std::sscanf(s.c_str(), "%llu:%d", &uid, &idx) != 2) return false;
      k.uid = uid; k.paramIndex = idx;
      return true;
   }
}

DriftNode::DriftNode()
{
   // Two Drifts dropped on the canvas must not walk in lockstep (RandomNode::NextSeed's lesson).
   static int sSpawn = 0;
   seed = 1000 + (++sSpawn) * 7919;
   mStats = &MovementStats::Live();
}

// ---- the dynamics --------------------------------------------------------------------------

void DriftNode::Step(float& x, float& v, uint64_t& rng, const Model& m, const Params& pr, float handEnergy, double dt)
{
   const float fdt = (float)dt;
   v = pr.momentum > 1e-4f ? v * std::exp(-fdt / pr.momentum) : 0.0f;

   const float T = pr.stray * (1.0f + pr.link * std::clamp(handEnergy, 0.0f, 1.0f));
   const float k = std::clamp(m.sigma * m.sigma / (2.0f * std::max(m.theta, 1e-3f)), 0.001f, 0.1f);
   const float eta = pr.speed * m.theta * k;
   const float sig = std::sqrt(2.0f * eta * T);

   const float h = 1.0f / kBins;
   const float dU = (Potential(m.p, x + h) - Potential(m.p, x - h)) / (2.0f * h);
   const float drift = std::clamp(-eta * dU * fdt, -kDriftCap, kDriftCap);

   // The reflecting range always contains where we started, so a release outside the observed
   // range walks back in rather than teleporting.
   const float lo = std::min(m.lo, x), hi = std::max(m.hi, x);
   x += v * fdt + drift + sig * std::sqrt(fdt) * Gauss(rng);
   for (int i = 0; i < 4 && (x < lo || x > hi); i++)
   {
      if (x < lo) { x = 2.0f * lo - x; v = -v; }
      if (x > hi) { x = 2.0f * hi - x; v = -v; }
   }
   x = std::clamp(x, lo, hi);
}

// ---- slots ---------------------------------------------------------------------------------

DriftNode::Slot& DriftNode::SlotFor(const ParamKey& k, float curPos)
{
   auto it = mSlots.find(k);
   if (it == mSlots.end())
   {
      Slot s;
      s.x = std::clamp(curPos, 0.0f, 1.0f);
      s.rng = SeedFor(seed, k);
      it = mSlots.emplace(k, s).first;
      if (mAnchors.find(k) == mAnchors.end())
         SetAnchor(k, s.x); // "else its value at bind time"
   }
   it->second.lastSeen = mFrame;
   return it->second;
}

float DriftNode::AnchorFor(const ParamKey& k) const
{
   auto it = mAnchors.find(k);
   return it != mAnchors.end() ? it->second : 0.5f;
}

void DriftNode::SetAnchor(const ParamKey& k, float pos)
{
   mAnchors[k] = std::clamp(pos, 0.0f, 1.0f);
   RebuildAnchorText();
}

void DriftNode::RebuildAnchorText()
{
   std::string t;
   char buf[96];
   for (const auto& [k, pos] : mAnchors)
   {
      std::snprintf(buf, sizeof(buf), "%llu:%d=%.4f;", (unsigned long long)k.uid, k.paramIndex, pos);
      t += buf;
   }
   anchors = t;
   mParsedAnchors = t;
}

void DriftNode::SyncAnchors()
{
   if (anchors == mParsedAnchors)
      return;
   mAnchors.clear();
   for (const std::string& e : Split(anchors, ';'))
   {
      const size_t eq = e.find('=');
      ParamKey k;
      if (eq == std::string::npos || !ParseKey(e.substr(0, eq), k))
         continue;
      mAnchors[k] = std::clamp((float)std::atof(e.c_str() + eq + 1), 0.0f, 1.0f);
   }
   mParsedAnchors = anchors;
}

// ---- freeze ---------------------------------------------------------------------------------

void DriftNode::RebuildFrozenText()
{
   std::string t;
   for (const auto& [k, f] : mFrozen)
   {
      uint8_t buf[kBins + 16];
      std::memcpy(buf, f.q, kBins);
      const float tail[4] = { f.theta, f.sigma, f.lo, f.hi };
      std::memcpy(buf + kBins, tail, 16);
      char head[48];
      std::snprintf(head, sizeof(head), "%llu:%d:", (unsigned long long)k.uid, k.paramIndex);
      t += head + B64Encode(buf, sizeof(buf)) + ";";
   }
   frozenProfile = t;
   mParsedFrozen = t;
}

void DriftNode::SyncFrozen(int)
{
   if (!frozen)
   {
      if (mFrozenApplied)
      {
         mFrozen.clear();
         frozenProfile.clear();
         mParsedFrozen.clear();
         mFrozenApplied = false;
         for (auto& kv : mSlots) kv.second.modelFrame = -1000;
      }
      return;
   }
   if (!mFrozenApplied && frozenProfile.empty())
   {
      // Snapshot the learned profile of every bound key, exactly as the live model would build it.
      for (auto& [k, s] : mSlots)
      {
         RefreshModel(k, s);
         Frozen f;
         float mx = 0.0f;
         for (int i = 0; i < kBins; i++) mx = std::max(mx, s.model.p[i]);
         for (int i = 0; i < kBins; i++)
            f.q[i] = mx > 0.0f ? (uint8_t)std::lround(std::clamp(s.model.p[i] / mx, 0.0f, 1.0f) * 255.0f) : 0;
         f.theta = s.model.theta; f.sigma = s.model.sigma; f.lo = s.model.lo; f.hi = s.model.hi;
         mFrozen[k] = f;
      }
      RebuildFrozenText();
      mFrozenApplied = true;
      for (auto& kv : mSlots) kv.second.modelFrame = -1000;
      return;
   }
   if (frozenProfile != mParsedFrozen || !mFrozenApplied)
   {
      mFrozen.clear();
      for (const std::string& e : Split(frozenProfile, ';'))
      {
         const size_t c2 = e.find(':', e.find(':') + 1);
         ParamKey k;
         std::vector<uint8_t> raw;
         if (c2 == std::string::npos || !ParseKey(e.substr(0, c2), k) || !B64Decode(e.substr(c2 + 1), raw) ||
             raw.size() < (size_t)kBins + 16)
            continue;
         Frozen f;
         std::memcpy(f.q, raw.data(), kBins);
         float tail[4];
         std::memcpy(tail, raw.data() + kBins, 16);
         f.theta = tail[0]; f.sigma = tail[1]; f.lo = tail[2]; f.hi = tail[3];
         mFrozen[k] = f;
      }
      mParsedFrozen = frozenProfile;
      mFrozenApplied = true;
      for (auto& kv : mSlots) kv.second.modelFrame = -1000;
   }
}

// ---- model ----------------------------------------------------------------------------------

void DriftNode::RefreshModel(const ParamKey& k, Slot& s)
{
   Model m;
   if (frozen)
   {
      auto it = mFrozen.find(k);
      if (it != mFrozen.end())
      {
         float sum = 0.0f;
         for (int i = 0; i < kBins; i++) sum += it->second.q[i];
         for (int i = 0; i < kBins; i++) m.p[i] = sum > 0.0f ? it->second.q[i] / sum : 1.0f / kBins;
         m.theta = std::clamp(it->second.theta, 0.05f, 20.0f);
         m.sigma = std::clamp(it->second.sigma, 0.005f, 1.0f);
         m.lo = it->second.lo; m.hi = it->second.hi;
         m.nEff = 1e9; m.w1 = 1.0f; m.cold = false;
         s.model = m;
         s.modelFrame = mFrame;
         return;
      }
      // A key bound after the freeze has no profile: a flat, deterministic walk.
      for (int i = 0; i < kBins; i++) m.p[i] = 1.0f / kBins;
      s.model = m;
      s.modelFrame = mFrame;
      return;
   }

   // The fallback ladder, blended (README §6): this key, its profile, its role, its family, your style,
   // and the anchor. No rung ever switches on or off; each carries a weight that grows with its data.
   MovementStats::Blend b;
   const MovementStats::KeyId id{ k.uid, k.paramIndex };
   if (mStats != nullptr)
      mStats->ComputeBlend(id, AnchorFor(k), b);
   else
   {
      // No stats at all: a peak at the anchor and the default speed.
      static const MovementStats::Engine sNone;
      sNone.ComputeBlend(id, AnchorFor(k), b);
   }
   std::memcpy(m.p, b.p, sizeof(m.p));
   m.theta = std::clamp(b.theta, 0.05f, 20.0f);
   m.sigma = std::clamp(b.sigma, 0.005f, 1.0f);
   m.lo = b.lo; m.hi = b.hi;
   m.nEff = b.nKey;
   m.w1 = (float)b.w1;
   m.cold = b.w1 < 0.2;
   s.model = m;
   s.modelFrame = mFrame;
}

// ---- IPredictor -----------------------------------------------------------------------------

void DriftNode::Tick(int frameId, double dt)
{
   mFrame = frameId;
   SyncAnchors();
   SyncFrozen(frameId);

   const Params pr{ std::clamp(speed, 0.1f, 4.0f), std::clamp(stray, 0.25f, 4.0f), std::clamp(momentum, 0.0f, 4.0f),
                    std::clamp(link, 0.0f, 2.0f) };
   for (auto it = mSlots.begin(); it != mSlots.end();)
   {
      Slot& s = it->second;
      if (frameId - s.lastSeen > kSlotDropFrames)
      {
         it = mSlots.erase(it);
         continue;
      }
      if (frameId - s.modelFrame >= kModelRefreshFrames || s.modelFrame < 0)
         RefreshModel(it->first, s);
      if (!s.paused && dt > 0.0)
         Step(s.x, s.v, s.rng, s.model, pr, EnergyFor(it->first), dt);
      ++it;
   }
}

float DriftNode::ValuePos01For(const ParamKey& k, float curPos)
{
   return std::clamp(SlotFor(k, curPos).x, 0.0f, 1.0f);
}

void DriftNode::OnGrab(const ParamKey& k)
{
   auto it = mSlots.find(k);
   if (it != mSlots.end())
      it->second.paused = true;
}

void DriftNode::OnRelease(const ParamKey& k, float pos, float velPerSec)
{
   Slot& s = SlotFor(k, pos);
   s.x = std::clamp(pos, 0.0f, 1.0f);
   s.v = velPerSec;
   s.paused = false;
   SetAnchor(k, s.x); // only a hand write ever moves an anchor, never Drift's own output
}

float DriftNode::Value01()
{
   return mSlots.empty() ? 0.5f : std::clamp(mSlots.begin()->second.x, 0.0f, 1.0f);
}

float DriftNode::SlotPos(const ParamKey& k) const
{
   auto it = mSlots.find(k);
   return it != mSlots.end() ? it->second.x : -1.0f;
}

int DriftNode::ConfidenceRung(const ParamKey& k) const
{
   if (frozen)
      return 2;
   auto it = mSlots.find(k);
   if (it == mSlots.end())
      return 0;
   // README §6.5: how much of the model is this knob's own data. Grey = defaults, dim green = your style
   // carried over from other patches and knobs, full green = this knob.
   const float w1 = it->second.model.w1;
   return w1 < 0.2f ? 0 : (w1 < 0.6f ? 1 : 2);
}

float DriftNode::EnergyFor(const ParamKey& k) const
{
   return mStats != nullptr ? mStats->HandEnergy(MovementStats::KeyId{ k.uid, k.paramIndex }) : 0.0f;
}

int DriftNode::Ghost(const ParamKey& k, float* out, int maxPoints)
{
   auto it = mSlots.find(k);
   if (it == mSlots.end() || it->second.paused || out == nullptr)
      return 0;
   Slot& s = it->second;
   if (s.ghostFrame != mFrame)
   {
      // Fork x, v and the RNG state. Reseeding from `seed` would draw the path from the start of the
      // take, not from now; touching the real slot would change the take.
      float x = s.x, v = s.v;
      uint64_t rng = s.rng;
      const Params pr{ std::clamp(speed, 0.1f, 4.0f), std::clamp(stray, 0.25f, 4.0f), std::clamp(momentum, 0.0f, 4.0f),
                       std::clamp(link, 0.0f, 2.0f) };
      for (int i = 0; i < kGhostPoints; i++)
      {
         Step(x, v, rng, s.model, pr, EnergyFor(k), kGhostDt);
         s.ghost[i] = x;
      }
      s.ghostN = kGhostPoints;
      s.ghostFrame = mFrame;
   }
   const int n = std::min(maxPoints, s.ghostN);
   std::memcpy(out, s.ghost, sizeof(float) * (size_t)n);
   return n;
}

// ---- self test (INFINITE_DRIFTTEST) --------------------------------------------------------------

namespace PredictionNodes
{
namespace
{
   int gFail = 0;
#define DRIFT_CHECK(cond, ...) \
   do { if (!(cond)) { std::printf("DRIFTTEST FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); gFail++; } } while (0)

   const ParamKey kKey{ 4242, 3 };

   // A visitor that writes a node's params to a map, or reads them back: a stand-in for Patch save/load.
   struct MapVisitor : ParamVisitor
   {
      std::map<std::string, std::string> m;
      bool writing = true;
      void Float(const char* n, float& v) override { if (writing) m[n] = std::to_string(v); else v = std::stof(m[n]); }
      void Int(const char* n, int& v) override { if (writing) m[n] = std::to_string(v); else v = std::stoi(m[n]); }
      void Bool(const char* n, bool& v) override { if (writing) m[n] = v ? "1" : "0"; else v = m[n] == "1"; }
      void Text(const char* n, std::string& v) override { if (writing) m[n] = v; else v = m[n]; }
      void Color(const char*, float*) override {}
   };

   // Two broad dwell peaks, trained through the real stats engine.
   void TrainTwoPeak(MovementStats::Engine& e)
   {
      using namespace MovementStats;
      e.RegisterKey(KeyId{ kKey.uid, kKey.paramIndex }, "TestType", "cutoff", true);
      uint64_t r = 99;
      for (int i = 0; i < 6000; i++)
      {
         const double t = i * kGridDt;
         const bool second = (i / 600) % 2 == 1;
         const float pos = std::clamp((second ? 0.75f : 0.25f) + 0.07f * Gauss(r), 0.0f, 1.0f);
         e.Observe(KeyId{ kKey.uid, kKey.paramIndex }, t, pos, MovementLog::Source::Hand, 0);
         e.Advance(t);
      }
   }

   std::vector<float> Trace(DriftNode& n, int frames, double dt, int firstFrame = 1)
   {
      std::vector<float> out;
      for (int f = 0; f < frames; f++)
      {
         n.Tick(firstFrame + f, dt);
         out.push_back(n.ValuePos01For(kKey, 0.5f));
      }
      return out;
   }

   double Entropy(const std::vector<float>& v)
   {
      double h[DriftNode::kBins] = {};
      for (float x : v) h[std::min((int)(x * DriftNode::kBins), DriftNode::kBins - 1)] += 1.0;
      double e = 0.0;
      for (double c : h) if (c > 0.0) { const double p = c / v.size(); e -= p * std::log(p); }
      return e;
   }
}

bool RunDriftTest()
{
   gFail = 0;
   const double dt = 1.0 / 60.0;

   // 1. Stationary law: matches p_hat at T = 1, sharper at 0.25, flatter at 4.
   {
      MovementStats::Engine e;
      TrainTwoPeak(e);
      const MovementStats::ParamStats* st = e.Find(MovementStats::KeyId{ kKey.uid, kKey.paramIndex });
      DRIFT_CHECK(st != nullptr, "no stats");
      float p[MovementStats::kBins];
      if (st) MovementStats::SmoothedHist(*st, p);
      double H[3] = {};
      const float strays[3] = { 1.0f, 0.25f, 4.0f };
      for (int c = 0; c < 3; c++)
      {
         DriftNode n;
         n.SetStatsSource(&e);
         n.stray = strays[c]; n.momentum = 0.0f; n.seed = 5;
         n.Tick(1, dt);
         n.ValuePos01For(kKey, 0.25f);
         const std::vector<float> tr = Trace(n, 36000, dt, 2); // 10 simulated minutes
         H[c] = Entropy(tr);
         if (c == 0)
         {
            double h[MovementStats::kBins] = {};
            for (float x : tr) h[std::min((int)(x * MovementStats::kBins), MovementStats::kBins - 1)] += 1.0;
            double kl = 0.0;
            for (int i = 0; i < MovementStats::kBins; i++)
            {
               const double q = h[i] / tr.size();
               if (q > 0.0) kl += q * std::log(q / std::max((double)p[i], 1e-6));
            }
            std::printf("[DRIFT] T=1 KL=%.3f\n", kl);
            DRIFT_CHECK(kl < 0.1, "KL %.3f >= 0.1", kl);
         }
      }
      std::printf("[DRIFT] entropy T=1 %.3f  T=0.25 %.3f  T=4 %.3f\n", H[0], H[1], H[2]);
      DRIFT_CHECK(H[1] < H[0], "T=0.25 not sharper");
      DRIFT_CHECK(H[2] > H[0], "T=4 not flatter");
   }

   // 2. Release momentum: v = 0.5/s with tau = 1 s travels about 0.5.
   {
      DriftNode n;
      n.speed = 0.1f; n.stray = 0.25f; n.momentum = 1.0f;
      n.Tick(1, dt);
      n.ValuePos01For(kKey, 0.2f);
      n.OnRelease(kKey, 0.2f, 0.5f);
      float x = 0.2f;
      for (int f = 0; f < 600; f++) { n.Tick(2 + f, dt); x = n.ValuePos01For(kKey, 0.2f); }
      std::printf("[DRIFT] release travel %.3f (0.5)\n", x - 0.2f);
      DRIFT_CHECK(std::abs((x - 0.2f) - 0.5f) < 0.12f, "release travel %.3f", x - 0.2f);
   }

   // 3. Frozen + seed is deterministic, and survives a save/load round trip.
   {
      MovementStats::Engine e;
      TrainTwoPeak(e);
      DriftNode a;
      a.SetStatsSource(&e);
      a.seed = 77; a.stray = 1.5f;
      a.Tick(1, dt);
      a.ValuePos01For(kKey, 0.3f);
      a.Tick(2, dt);
      a.frozen = true;
      a.Tick(3, dt);
      DRIFT_CHECK(!a.frozenProfile.empty(), "freeze produced no profile");
      std::printf("[DRIFT] frozen profile %zu chars for 1 key\n", a.frozenProfile.size());
      MapVisitor mv; mv.writing = true; a.VisitParams(mv);

      DriftNode b, c;
      b.SetStatsSource(nullptr); c.SetStatsSource(nullptr); // frozen must not read live stats
      mv.writing = false; b.VisitParams(mv); c.VisitParams(mv);
      auto run = [&](DriftNode& n) {
         n.Tick(1, dt); n.ValuePos01For(kKey, 0.3f);
         return Trace(n, 600, dt, 2);
      };
      const std::vector<float> t1 = run(b), t2 = run(c);
      DRIFT_CHECK(t1 == t2, "frozen traces differ across two runs");
      double moved = 0.0;
      for (size_t i = 1; i < t1.size(); i++) moved += std::abs(t1[i] - t1[i - 1]);
      DRIFT_CHECK(moved > 0.05, "frozen trace does not move (%.4f)", moved);
      // Unfreezing clears the profile so the next freeze re-snapshots.
      b.frozen = false; b.Tick(2000, dt);
      DRIFT_CHECK(b.frozenProfile.empty(), "unfreeze kept the profile");
   }

   // 4. Ghost isolation: drawing the ghost every frame must not change the take.
   {
      MovementStats::Engine e;
      TrainTwoPeak(e);
      DriftNode a, b;
      a.SetStatsSource(&e); b.SetStatsSource(&e);
      a.seed = b.seed = 31;
      a.Tick(1, dt); b.Tick(1, dt);
      a.ValuePos01For(kKey, 0.4f); b.ValuePos01For(kKey, 0.4f);
      std::vector<float> ta, tb;
      float g[DriftNode::kGhostPoints];
      int ghosts = 0;
      for (int f = 0; f < 600; f++)
      {
         a.Tick(2 + f, dt); b.Tick(2 + f, dt);
         ghosts += a.Ghost(kKey, g, DriftNode::kGhostPoints) > 0;
         ta.push_back(a.ValuePos01For(kKey, 0.4f));
         tb.push_back(b.ValuePos01For(kKey, 0.4f));
      }
      DRIFT_CHECK(ghosts == 600, "ghost drawn on %d/600 frames", ghosts);
      DRIFT_CHECK(ta == tb, "ghost changed the real trace");
      // The ghost starts from now: its first point is one preview step from the current x.
      a.Tick(700, dt);
      a.Ghost(kKey, g, DriftNode::kGhostPoints);
      DRIFT_CHECK(std::abs(g[0] - a.SlotPos(kKey)) < 0.1f, "ghost does not start from the current position");
   }

   // 5. Idempotency + stale slots + anchors: many reads per frame do not advance; a slot unseen 300 frames drops.
   {
      DriftNode a, b;
      a.seed = b.seed = 9;
      a.Tick(1, dt); b.Tick(1, dt);
      a.ValuePos01For(kKey, 0.5f); b.ValuePos01For(kKey, 0.5f);
      for (int f = 0; f < 120; f++)
      {
         a.Tick(2 + f, dt); b.Tick(2 + f, dt);
         for (int r = 0; r < 8; r++) a.ValuePos01For(kKey, 0.5f);
         b.ValuePos01For(kKey, 0.5f);
      }
      DRIFT_CHECK(a.SlotPos(kKey) == b.SlotPos(kKey), "8 reads per frame differ from 1");
      DRIFT_CHECK(a.anchors.find("4242:3=") != std::string::npos, "anchor missing: '%s'", a.anchors.c_str());
      for (int f = 0; f < 320; f++) a.Tick(200 + f, dt);
      DRIFT_CHECK(a.SlotCount() == 0, "stale slot not dropped");
   }

   std::printf("%s\n", gFail == 0 ? "DRIFTTEST OK" : "DRIFTTEST FAIL");
   return gFail == 0;
}
}

// ---- self test (INFINITE_PREDFEEDBACKTEST) -----------------------------------------------------
// Step 5: cold start, the blended ladder, role pooling, your style, the energy link, anchors and the
// collapse monitor. Everything runs headless against private MovementStats::Engine instances.

namespace PredictionNodes
{
namespace
{
   using MovementStats::Blend;
   using MovementStats::Engine;
   using MovementStats::KeyId;
   using MovementStats::KeyMeta;
   using MovementLog::Source;

   int gFbFail = 0;
#define FB_CHECK(cond, ...) \
   do { if (!(cond)) { std::printf("PREDFEEDBACKTEST FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); gFbFail++; } } while (0)

   float LogP2V(float p, float mn, float mx) { return mn * std::pow(mx / mn, p); }
   float LogV2P(float v, float mn, float mx) { return std::log(v / mn) / std::log(mx / mn); }

   KeyMeta Meta(const char* cat, float mn, float mx, bool logCurve)
   {
      KeyMeta m;
      m.category = cat; m.minValue = mn; m.maxValue = mx;
      if (logCurve) { m.posToValue = LogP2V; m.valueToPos = LogV2P; m.hasCurve = true; }
      return m;
   }
   void Reg(Engine& e, KeyId id, const char* type, const char* name, const KeyMeta& m) { e.RegisterKey(id, type, name, true, m); }
   ParamKey PK(KeyId k) { return ParamKey{ k.uid, k.paramIndex }; }

   // n grid rows of a hand dwelling at `center`.
   void Dwell(Engine& e, KeyId id, double& t, int rows, float center, float jitter, uint64_t& rng, Source src = Source::Hand)
   {
      for (int i = 0; i < rows; i++)
      {
         e.Observe(id, t, std::clamp(center + jitter * Gauss(rng), 0.0f, 1.0f), src, 0);
         e.Advance(t);
         t += MovementStats::kGridDt;
      }
   }

   int Argmax(const float* p)
   {
      int b = 0;
      for (int i = 1; i < MovementStats::kBins; i++) if (p[i] > p[b]) b = i;
      return b;
   }
   float Mass(const float* p, float lo, float hi)
   {
      float m = 0.0f;
      for (int i = 0; i < MovementStats::kBins; i++)
      {
         const float c = (i + 0.5f) / MovementStats::kBins;
         if (c >= lo && c <= hi) m += p[i];
      }
      return m;
   }
   int BinOf(float pos) { return std::clamp((int)(pos * MovementStats::kBins), 0, MovementStats::kBins - 1); }

   // An Ornstein-Uhlenbeck hand, exact step, on the 10 Hz grid.
   void OuHand(Engine& e, KeyId id, double& t, double seconds, double theta, double mu, double sigma, uint64_t& rng, double& x)
   {
      const double dt = MovementStats::kGridDt, a = std::exp(-theta * dt);
      const double sd = sigma * std::sqrt((1.0 - a * a) / (2.0 * theta));
      const int rows = (int)(seconds / dt);
      for (int i = 0; i < rows; i++)
      {
         x = mu + (x - mu) * a + sd * Gauss(rng);
         e.Observe(id, t, std::clamp((float)x, 0.0f, 1.0f), Source::Hand, 0);
         e.Advance(t);
         t += dt;
      }
   }

   struct CollapseResult { double h0 = 0, hEnd = 0, predShare = 0; int cuts = 0; bool cutEnd = false; };

   // A Drift node feeding only itself: its output is logged as Prediction and nothing else ever arrives.
   CollapseResult RunCollapse(float predWeight, float stray, double hours)
   {
      CollapseResult r;
      Engine e;
      e.SetPredictionWeight(predWeight);
      int cuts = 0;
      e.SetAutoCutCallback([&](const KeyId&) { cuts++; });
      const KeyId id{ kKey.uid, kKey.paramIndex };
      TrainTwoPeak(e);
      MovementStats::Monitor mon;
      e.GetMonitor(id, mon);
      r.h0 = mon.entropy;
      e.SetPlaying(true);

      DriftNode n;
      n.SetStatsSource(&e);
      n.seed = 21; n.stray = stray; n.momentum = 0.0f;
      const double dt = 1.0 / 30.0;
      double t = 600.0;
      n.Tick(1, dt);
      float x = n.ValuePos01For(kKey, 0.25f);
      uint16_t lastQ = 0xFFFF;
      const long frames = (long)(hours * 3600.0 / dt);
      for (long f = 0; f < frames; f++, t += dt)
      {
         n.Tick((int)(f + 2), dt);
         x = n.ValuePos01For(kKey, 0.25f);
         const uint16_t q = (uint16_t)std::lround(std::clamp(x, 0.0f, 1.0f) * 65535.0f);
         if (q != lastQ) { e.Observe(id, t, q / 65535.0f, Source::Prediction, 0); lastQ = q; }
         e.Advance(t);
      }
      e.GetMonitor(id, mon);
      r.hEnd = mon.entropy; r.predShare = mon.predShare; r.cutEnd = mon.cut; r.cuts = cuts;
      return r;
   }
}

bool RunPredFeedbackTest()
{
   gFbFail = 0;
   const double dt = 1.0 / 60.0;
   const int kB = MovementStats::kBins;

   // 1. Cold: a fresh key with no profile, role or family data is a narrow peak at its anchor (flat only
   //    with no anchor), and the value never leaves the declared range.
   {
      Engine e;
      Reg(e, KeyId{ 1, 0 }, "Cold", "thing", Meta("", 0.0f, 1.0f, false));
      Blend b;
      e.ComputeBlend(KeyId{ 1, 0 }, 0.3f, b);
      const int pk = Argmax(b.p);
      FB_CHECK(std::abs(pk - BinOf(0.3f)) <= 1, "cold peak at bin %d, anchor bin %d", pk, BinOf(0.3f));
      FB_CHECK(Mass(b.p, 0.2f, 0.4f) > 0.6f, "cold mass near the anchor %.2f", Mass(b.p, 0.2f, 0.4f));
      FB_CHECK(b.w4 > 0.999 && b.w1 == 0.0 && b.w2 == 0.0 && b.w2b == 0.0 && b.w2d == 0.0, "cold weights not all on the anchor");
      FB_CHECK(b.lo == 0.0f && b.hi == 1.0f, "cold range [%.2f, %.2f] is not the declared one", b.lo, b.hi);
      e.ComputeBlend(KeyId{ 1, 0 }, -1.0f, b);
      float mn = 1.0f, mx = 0.0f;
      for (float v : b.p) { mn = std::min(mn, v); mx = std::max(mx, v); }
      FB_CHECK(mx - mn < 1e-5f, "no anchor should be flat (%.6f .. %.6f)", mn, mx);

      DriftNode n;
      n.SetStatsSource(&e);
      n.seed = 3;
      n.Tick(1, dt);
      n.ValuePos01For(PK(KeyId{ 1, 0 }), 0.3f);
      float lo = 1.0f, hi = 0.0f;
      for (int f = 0; f < 3600; f++)
      {
         n.Tick(2 + f, dt);
         const float x = n.ValuePos01For(PK(KeyId{ 1, 0 }), 0.3f);
         lo = std::min(lo, x); hi = std::max(hi, x);
      }
      FB_CHECK(lo >= 0.0f && hi <= 1.0f, "cold value left the range [%.3f, %.3f]", lo, hi);
      std::printf("[FB] cold: peak bin %d, walked %.3f..%.3f\n", pk, lo, hi);
   }

   // 2. Warm profile: node type A trained in "patch 1"; a fresh A node starts warm.
   {
      Engine e;
      const KeyMeta m = Meta("", 0.0f, 1.0f, false);
      Reg(e, KeyId{ 10, 3 }, "TypeA", "foo", m);
      Reg(e, KeyId{ 11, 3 }, "TypeA", "foo", m);
      uint64_t rng = 5;
      double t = 0.0;
      for (int rep = 0; rep < 10; rep++)
      {
         Dwell(e, KeyId{ 10, 3 }, t, 200, 0.25f, 0.04f, rng);
         Dwell(e, KeyId{ 10, 3 }, t, 200, 0.75f, 0.04f, rng);
      }
      Blend b;
      e.ComputeBlend(KeyId{ 11, 3 }, -1.0f, b);
      std::printf("[FB] warm profile: w1 %.2f  w2 %.2f  w4 %.2f\n", b.w1, b.w2, b.w4);
      FB_CHECK(b.w1 == 0.0 && b.w2 > 0.5, "profile weight %.2f (want > 0.5)", b.w2);
      FB_CHECK(b.p[BinOf(0.25f)] > 3.0f * b.p[BinOf(0.5f)] && b.p[BinOf(0.75f)] > 3.0f * b.p[BinOf(0.5f)],
               "fresh node does not carry the profile's two peaks");
   }

   // 3. Role pooling: "cutoff is cutoff", in physical units; family pooling; unknown names get no role.
   {
      Engine e;
      Reg(e, KeyId{ 20, 0 }, "Filter", "cutoff", Meta("AudioEffects", 20.0f, 20000.0f, true));
      Reg(e, KeyId{ 21, 0 }, "Wavetable", "cutoff", Meta("Synths", 50.0f, 5000.0f, true));
      Reg(e, KeyId{ 22, 0 }, "Wavetable", "wobble", Meta("Synths", 50.0f, 5000.0f, true));
      uint64_t rng = 8;
      double t = 0.0;
      const float pos1kFilter = LogV2P(1000.0f, 20.0f, 20000.0f), pos1kWave = LogV2P(1000.0f, 50.0f, 5000.0f);
      Dwell(e, KeyId{ 20, 0 }, t, 6000, pos1kFilter, 0.01f, rng);
      Blend b, bu;
      e.ComputeBlend(KeyId{ 21, 0 }, pos1kWave, b);
      // Compare without the anchor's help: anchor deliberately somewhere else.
      e.ComputeBlend(KeyId{ 21, 0 }, 0.05f, b);
      const int pk = Argmax(b.p);
      std::printf("[FB] role: w2b %.2f  peak bin %d (own 1 kHz = %d, filter's = %d)\n", b.w2b, pk, BinOf(pos1kWave), BinOf(pos1kFilter));
      FB_CHECK(b.w2b > 0.5, "role weight %.2f", b.w2b);
      FB_CHECK(std::abs(pk - BinOf(pos1kWave)) <= 2, "peak bin %d, expected the wavetable's own 1 kHz at %d", pk, BinOf(pos1kWave));
      FB_CHECK(std::abs(pk - BinOf(pos1kFilter)) > 3, "peak bin %d sits at the filter's fader position", pk);
      e.ComputeBlend(KeyId{ 22, 0 }, 0.5f, bu);
      FB_CHECK(bu.w2b == 0.0 && bu.w2d == 0.0, "an unknown name got a role weight (%.2f / %.2f)", bu.w2b, bu.w2d);

      // Family: `width` trains size.extent; `radius` is size.radius, same family, same unit: no role, some family weight.
      Reg(e, KeyId{ 23, 0 }, "Shape", "width", Meta("Source", 0.0f, 1.0f, false));
      Reg(e, KeyId{ 24, 0 }, "Circle", "radius", Meta("Source", 0.0f, 1.0f, false));
      double t2 = 0.0;
      uint64_t r2 = 4;
      OuHand(e, KeyId{ 23, 0 }, t2, 1800.0, 1.0, 0.6, 0.15, r2, *std::make_unique<double>(0.6));
      Blend bf;
      e.ComputeBlend(KeyId{ 24, 0 }, 0.5f, bf);
      std::printf("[FB] family: w2b %.2f  w2d %.2f\n", bf.w2b, bf.w2d);
      FB_CHECK(bf.w2b == 0.0 && bf.w2d > 0.2, "family weight %.2f (role %.2f)", bf.w2d, bf.w2b);
   }

   // 4. Your style: one busy knob for a simulated hour warms every other knob's speed, never its place.
   {
      Engine e;
      const KeyId busy{ 30, 0 }, quiet{ 31, 0 };
      Reg(e, busy, "Busy", "wobble", Meta("", 0.0f, 1.0f, false));
      Reg(e, quiet, "Quiet", "still", Meta("", 0.0f, 1.0f, false));
      uint64_t rng = 12;
      double t = 0.0, x = 0.5;
      OuHand(e, busy, t, 3600.0, 1.0, 0.5, 0.15, rng, x);
      const MovementStats::ParamStats* bs = e.Find(busy);
      FB_CHECK(bs != nullptr, "busy knob has no stats");
      const double busyTheta = bs != nullptr ? MovementStats::Derive(*bs).theta : 1.0;
      Blend b;
      e.ComputeBlend(quiet, 0.8f, b);
      const int pk = Argmax(b.p);
      std::printf("[FB] your style: quiet theta %.3f vs busy %.3f  wYou %.2f  peak bin %d (anchor %d)\n", b.theta, busyTheta, b.wYou, pk, BinOf(0.8f));
      FB_CHECK(std::abs(b.theta - busyTheta) <= 0.3 * busyTheta, "theta %.3f not within 30%% of %.3f", b.theta, busyTheta);
      FB_CHECK(std::abs(pk - BinOf(0.8f)) <= 1, "quiet knob's peak at bin %d, anchor at %d", pk, BinOf(0.8f));

      DriftNode n;
      n.SetStatsSource(&e);
      n.seed = 17; n.stray = 1.0f; n.momentum = 0.0f;
      n.Tick(1, dt);
      n.ValuePos01For(PK(quiet), 0.8f);
      int inside = 0;
      const int frames = 36000; // 10 simulated minutes
      for (int f = 0; f < frames; f++)
      {
         n.Tick(2 + f, dt);
         if (std::abs(n.ValuePos01For(PK(quiet), 0.8f) - 0.8f) <= 0.1f) inside++;
      }
      const double frac = (double)inside / frames;
      std::printf("[FB] your style: quiet knob within +-0.1 of its anchor %.1f%% of the time\n", 100.0 * frac);
      FB_CHECK(frac >= 0.95, "only %.1f%% within +-0.1 of the anchor", 100.0 * frac);
   }

   // 5. Energy link. Same role for the busy and the quiet knobs, a narrow landscape so the only thing
   //    that can widen the walk is T_eff = Stray (1 + Link E).
   {
      Engine e;
      const KeyId busy{ 40, 0 };
      const int kQ = 8;
      Reg(e, busy, "Comp", "opacity", Meta("Compositing", 0.0f, 1.0f, false));
      for (int i = 0; i < kQ; i++) Reg(e, KeyId{ (uint64_t)(50 + i), 0 }, "Comp", "opacity", Meta("Compositing", 0.0f, 1.0f, false));
      double t = 0.0;
      auto handAt = [&](double tt) { return 0.5f + 0.02f * (float)std::sin(6.2831853 * 1.5 * tt); };
      for (; t < 120.0; t += dt) { e.Observe(busy, t, handAt(t), Source::Hand, 0); e.Advance(t); }

      DriftNode n;
      n.SetStatsSource(&e);
      n.seed = 4; n.stray = 1.0f; n.momentum = 0.0f; n.link = 1.0f;
      int frame = 1;
      n.Tick(frame++, dt);
      for (int i = 0; i < kQ; i++) n.ValuePos01For(PK(KeyId{ (uint64_t)(50 + i), 0 }), 0.5f);

      auto run = [&](double seconds, bool hand, bool measure) {
         double acc = 0.0; long cnt = 0;
         const long frames = (long)(seconds / dt);
         for (long f = 0; f < frames; f++, t += dt)
         {
            if (hand) e.Observe(busy, t, handAt(t), Source::Hand, 0);
            e.Advance(t);
            n.Tick(frame++, dt);
            for (int i = 0; i < kQ; i++)
            {
               const float x = n.ValuePos01For(PK(KeyId{ (uint64_t)(50 + i), 0 }), 0.5f);
               if (measure) { acc += (x - 0.5f) * (x - 0.5f); cnt++; }
            }
         }
         return cnt > 0 ? acc / cnt : 0.0;
      };
      run(5.0, true, false);
      const double eHand = e.HandEnergy(KeyId{ 50, 0 });
      const double varHand = run(10.0, true, true);
      run(8.0, false, false);
      const double eQuiet = e.HandEnergy(KeyId{ 50, 0 });
      const double varQuiet = run(10.0, false, true);
      std::printf("[FB] energy: E %.2f -> %.3f   variance %.5f -> %.5f  (x%.2f)\n", eHand, eQuiet, varQuiet, varHand, varHand / std::max(varQuiet, 1e-12));
      FB_CHECK(eHand > 0.9 && eQuiet < 0.05, "energy %.2f while playing, %.3f after", eHand, eQuiet);
      // E is clamped to [0,1] and Link = 1, so T_eff can at most double: the ratio has 2 as its ceiling.
      FB_CHECK(varHand >= 1.8 * varQuiet, "variance x%.2f (want >= 1.8, ceiling 2)", varHand / std::max(varQuiet, 1e-12));
   }

   // 5b. No self-excitation: Drift driving five knobs with no hand input leaves HandEnergy at zero.
   {
      Engine e;
      DriftNode n;
      n.SetStatsSource(&e);
      n.seed = 6; n.stray = 4.0f; n.link = 2.0f;
      KeyId ids[5];
      for (int i = 0; i < 5; i++)
      {
         ids[i] = KeyId{ (uint64_t)(70 + i), 0 };
         Reg(e, ids[i], "Comp", "opacity", Meta("Compositing", 0.0f, 1.0f, false));
      }
      e.SetPlaying(true);
      uint16_t last[5] = {};
      float maxE = 0.0f;
      double t = 0.0;
      n.Tick(1, dt);
      for (int i = 0; i < 5; i++) n.ValuePos01For(PK(ids[i]), 0.5f);
      for (int f = 0; f < 3600; f++, t += dt)
      {
         n.Tick(2 + f, dt);
         for (int i = 0; i < 5; i++)
         {
            const uint16_t q = (uint16_t)std::lround(n.ValuePos01For(PK(ids[i]), 0.5f) * 65535.0f);
            if (q != last[i]) { e.Observe(ids[i], t, q / 65535.0f, Source::Prediction, 0); last[i] = q; }
         }
         e.Advance(t);
         for (int i = 0; i < 5; i++) maxE = std::max(maxE, e.HandEnergy(ids[i]));
      }
      FB_CHECK(maxE == 0.0f, "Drift excited itself: energy reached %.4f", maxE);
   }

   // 6. Role energy: an opacity moved by hand wakes the other opacities far more than an unrelated gain.
   {
      Engine e;
      const KeyId o1{ 80, 0 }, o2{ 81, 0 }, g{ 82, 0 };
      Reg(e, o1, "Comp", "opacity", Meta("Compositing", 0.0f, 1.0f, false));
      Reg(e, o2, "Comp", "opacity", Meta("Compositing", 0.0f, 1.0f, false));
      Reg(e, g, "Osc", "gain", Meta("Synths", 0.0f, 1.0f, false));
      double t = 0.0;
      for (; t < 5.0; t += dt)
      {
         e.Observe(o1, t, 0.5f + 0.2f * (float)std::sin(6.2831853 * t), Source::Hand, 0);
         e.Advance(t);
      }
      const float e2 = e.HandEnergy(o2), eg = e.HandEnergy(g);
      std::printf("[FB] role energy: other opacity %.3f  unrelated gain %.3f\n", e2, eg);
      FB_CHECK(e2 >= 2.0f * eg && eg > 0.0f, "opacity %.3f vs gain %.3f", e2, eg);
   }

   // 7. Role keying, and a fine tune pooled in cents across synths with different ranges.
   {
      FB_CHECK(std::string(ParamRoles::RoleFor("AudioEffects", "freq").role ? ParamRoles::RoleFor("AudioEffects", "freq").role : "") == "filter.cutoff", "freq on an audio effect is not filter.cutoff");
      FB_CHECK(std::string(ParamRoles::RoleFor("Synths", "Freq").role ? ParamRoles::RoleFor("Synths", "Freq").role : "") == "pitch.freq", "freq on a synth is not pitch.freq");
      FB_CHECK(!ParamRoles::RoleFor("Source", "amount").any() && !ParamRoles::RoleFor("3D", "amount").any(), "amount on a 2D/3D node has a role");
      FB_CHECK(ParamRoles::RoleFor("AudioEffects", "amount").any(), "amount on an audio effect has no role");
      FB_CHECK(!ParamRoles::RoleFor("", "cutoff").any() && !ParamRoles::RoleFor("Nonsense", "cutoff").any(), "unknown category got a role");

      Engine e;
      Reg(e, KeyId{ 90, 0 }, "SynthA", "fine", Meta("Synths", -50.0f, 50.0f, false));
      Reg(e, KeyId{ 91, 0 }, "SynthB", "fine tune", Meta("Synths", -10.0f, 10.0f, false));
      uint64_t rng = 2;
      double t = 0.0;
      Dwell(e, KeyId{ 90, 0 }, t, 6000, (3.0f + 50.0f) / 100.0f, 0.004f, rng);
      Blend b;
      e.ComputeBlend(KeyId{ 91, 0 }, 0.05f, b);
      const int want = BinOf((3.0f + 10.0f) / 20.0f), pk = Argmax(b.p);
      std::printf("[FB] fine tune: peak bin %d, +3 cents on the new range is bin %d  (w2b %.2f w2d %.2f w4 %.2f)\n", pk, want, b.w2b, b.w2d, b.w4);
      // The source landscape has 3.1 cents per bin, so its 3-cent dwell is only known to +-1.6 cents (5 target bins).
      FB_CHECK(std::abs(pk - want) <= 5 && pk > 34, "peak bin %d, +3 cents is bin %d", pk, want);
   }

   // 8. Anchors do not creep: predictions never move them, and they survive save and load.
   {
      MapVisitor mv;
      DriftNode a;
      a.seed = 12;
      a.Tick(1, 1.0 / 60.0);
      const ParamKey k1{ 100, 0 }, k2{ 101, 2 };
      a.ValuePos01For(k1, 0.31f);
      a.ValuePos01For(k2, 0.77f);
      const std::string before = a.anchors;
      for (int f = 0; f < 36000; f++) // 10 minutes of Drift
      {
         a.Tick(2 + f, 1.0 / 60.0);
         a.ValuePos01For(k1, 0.31f);
         a.ValuePos01For(k2, 0.77f);
      }
      FB_CHECK(a.anchors == before, "anchors crept: '%s' -> '%s'", before.c_str(), a.anchors.c_str());
      mv.writing = true; a.VisitParams(mv);
      DriftNode b;
      mv.writing = false; b.VisitParams(mv);
      for (int f = 0; f < 36000; f++)
      {
         b.Tick(1 + f, 1.0 / 60.0);
         b.ValuePos01For(k1, 0.5f);
         b.ValuePos01For(k2, 0.5f);
      }
      FB_CHECK(b.anchors == before, "anchors changed across save/load + 10 min: '%s'", b.anchors.c_str());
      b.OnRelease(k1, 0.9f, 0.0f);
      FB_CHECK(b.anchors != before, "a hand release did not move the anchor");
   }

   // 9. Collapse. Drift feeding only itself for 20 simulated hours.
   {
      struct Case { float weight, stray; };
      const Case cases[3] = { { 0.1f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.5f } };
      for (const Case& c : cases)
      {
         const CollapseResult r = RunCollapse(c.weight, c.stray, 20.0);
         std::printf("[FB] collapse w=%.1f stray=%.2f: entropy %.3f -> %.3f (%.0f%%)  pred share %.2f  cuts %d\n",
                     c.weight, c.stray, r.h0, r.hEnd, 100.0 * r.hEnd / std::max(r.h0, 1e-9), r.predShare, r.cuts);
         FB_CHECK(r.hEnd >= 0.75 * r.h0 || r.cuts > 0, "w=%.1f stray=%.2f: entropy fell to %.0f%% and nothing cut", c.weight, c.stray,
                  100.0 * r.hEnd / std::max(r.h0, 1e-9));
      }
   }

   // 9b. The cut itself: prediction rows that sharpen the landscape trip it after 2 h of active time, it
   //     stops the key learning from itself, and the next hand move lifts it.
   {
      Engine e;
      e.SetPredictionWeight(1.0f);
      int cuts = 0;
      e.SetAutoCutCallback([&](const KeyId&) { cuts++; });
      const KeyId id{ kKey.uid, kKey.paramIndex };
      TrainTwoPeak(e);
      e.SetPlaying(true);
      double t = 600.0;
      uint64_t rng = 3;
      // Hold the key on one spot as a prediction: the landscape collapses onto it.
      double cutAt = -1.0;
      double predAtCut = 0.0;
      for (int i = 0; i < 36000 * 6; i++, t += MovementStats::kGridDt)
      {
         e.Observe(id, t, std::clamp(0.25f + 0.005f * Gauss(rng), 0.0f, 1.0f), Source::Prediction, 0);
         e.Advance(t);
         MovementStats::Monitor m;
         if (cutAt < 0.0 && e.GetMonitor(id, m) && m.cut)
         {
            cutAt = t - 600.0;
            const MovementStats::ParamStats* s = e.Find(id);
            predAtCut = s != nullptr ? s->nPred : 0.0;
         }
      }
      const MovementStats::ParamStats* s = e.Find(id);
      std::printf("[FB] cut: fired at %.0f s of active time, cuts %d\n", cutAt, cuts);
      FB_CHECK(cuts == 1 && cutAt >= 7200.0, "cut fired %d times, first at %.0f s (want once, >= 7200 s)", cuts, cutAt);
      FB_CHECK(s != nullptr && s->nPred <= predAtCut * 1.001 + 1e-6 + 1.0, "key kept learning from itself after the cut (%.1f -> %.1f)", predAtCut, s ? s->nPred : 0.0);
      e.Observe(id, t, 0.6f, Source::Hand, 0);
      e.Advance(t + 0.2);
      MovementStats::Monitor m;
      FB_CHECK(e.GetMonitor(id, m) && !m.cut, "a hand move did not lift the cut");
   }

   // 10. Persistence keeps the new levels.
   {
      Engine e;
      Reg(e, KeyId{ 200, 0 }, "Filter", "cutoff", Meta("AudioEffects", 20.0f, 20000.0f, true));
      uint64_t rng = 1;
      double t = 0.0;
      Dwell(e, KeyId{ 200, 0 }, t, 3000, 0.5f, 0.02f, rng);
      const std::vector<uint8_t> blob = e.Serialize("x");
      Engine back;
      std::string last;
      FB_CHECK(back.Deserialize(blob.data(), blob.size(), last), "round trip failed");
      FB_CHECK(back.FindRole("filter.cutoff") != nullptr && back.FindFamily("filter") != nullptr && back.YouN() > 0.0,
               "role, family or you stats lost in the round trip");
      const std::vector<uint8_t> again = back.Serialize("x");
      FB_CHECK(again.size() == blob.size(), "re-serialised blob differs in size (%zu vs %zu)", again.size(), blob.size());
   }

   std::printf("%s\n", gFbFail == 0 ? "PREDFEEDBACKTEST OK" : "PREDFEEDBACKTEST FAIL");
   return gFbFail == 0;
}
}
