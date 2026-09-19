#include "nodes/PredictionNodes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
   constexpr double kColdNEff = 30.0;     // below this the landscape is a peak at the anchor (README §6 rung 4)
   constexpr float kColdSigma = 0.10f;    // cold-start OU sigma and theta: a slow, small reflected walk
   constexpr float kColdTheta = 0.5f;
   constexpr float kColdWidth = 0.10f;    // sd of the anchor peak
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
         m.nEff = 1e9; m.cold = false;
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

   const MovementStats::ParamStats* st =
      mStats != nullptr ? mStats->Find(MovementStats::KeyId{ k.uid, k.paramIndex }) : nullptr;
   const double nEff = st != nullptr && mStats != nullptr ? MovementStats::EffectiveN(*st, mStats->ActiveClock()) : 0.0;
   m.nEff = nEff;
   if (st != nullptr && nEff >= kColdNEff)
   {
      float p[kBins];
      MovementStats::SmoothedHist(*st, p);
      float sum = 0.0f;
      for (float v : p) sum += v;
      if (sum > 0.0f)
      {
         for (int i = 0; i < kBins; i++) m.p[i] = p[i] / sum;
         const MovementStats::Derived d = MovementStats::Derive(*st);
         if (d.valid)
         {
            m.theta = (float)d.theta;
            m.sigma = (float)std::max(d.sigma, 0.005);
         }
         m.lo = (float)d.rangeLo; m.hi = (float)d.rangeHi;
         if (m.hi - m.lo < 0.05f)
         {
            const float c = 0.5f * (m.lo + m.hi);
            m.lo = std::max(0.0f, c - 0.025f); m.hi = std::min(1.0f, c + 0.025f);
         }
         m.cold = false;
         s.model = m;
         s.modelFrame = mFrame;
         return;
      }
   }
   // Cold start: a narrow peak at the anchor, slow and small, over the whole fader.
   const float a = AnchorFor(k);
   float sum = 0.0f;
   for (int i = 0; i < kBins; i++)
   {
      const float d = ((i + 0.5f) / kBins - a) / kColdWidth;
      m.p[i] = std::exp(-0.5f * d * d) + 1e-4f;
      sum += m.p[i];
   }
   for (float& v : m.p) v /= sum;
   m.theta = kColdTheta; m.sigma = kColdSigma; m.lo = 0.0f; m.hi = 1.0f; m.cold = true;
   s.model = m;
   s.modelFrame = mFrame;
}

// ---- IPredictor -----------------------------------------------------------------------------

namespace
{
   // The energy link (README §6): how actively the hand is moving right now. MovementStats does not
   // track it yet, so Link is wired and inert until step 5 supplies the live value.
   float HandEnergy(const ParamKey&) { return 0.0f; }
}

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
         Step(s.x, s.v, s.rng, s.model, pr, HandEnergy(it->first), dt);
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
      return 3;
   auto it = mSlots.find(k);
   if (it == mSlots.end() || it->second.model.cold)
      return 0;
   const double n = it->second.model.nEff;
   return n < 100.0 ? 1 : (n < 1000.0 ? 2 : 3);
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
         Step(x, v, rng, s.model, pr, HandEnergy(k), kGhostDt);
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
