#include "MovementStats.h"
#include "../platform/AppPaths.h"
#include "NodeFactory.h"

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

namespace
{
// README §5, the one edit point. The raw log keeps every tag, so any of these can change later.
enum WeightSlot { kWHand, kWPerf, kWCorrection, kWGesture, kWModulator, kWExpression, kWPrediction,
                  kWPredictionUntouched, kWOther, kWSlots };
constexpr float kSourceWeights[kWSlots] = {
   1.0f,  // hand
   1.0f,  // perf matrix
   3.0f,  // hand or perf + correction (grabbed a green param): the expert corrected the learner
   0.2f,  // gesture playback: a replay of a take already logged at 1.0
   0.1f,  // modulator
   0.1f,  // expression
   0.1f,  // prediction that moved (own output, owner's call: keep the data rich)
   0.0f,  // prediction left untouched: "acceptance" is mostly inattention, and weighting it closes the loop
   0.0f,  // other (preset, randomize, reset: a jump with no widget)
};
} // namespace

float SourceWeight(Source src, uint8_t flags, bool changedThisTick)
{
   switch (src)
   {
   case Source::Hand:
   case Source::Perf:
      return kSourceWeights[(flags & MovementLog::kCorrection) ? kWCorrection : (src == Source::Hand ? kWHand : kWPerf)];
   case Source::Gesture: return kSourceWeights[kWGesture];
   case Source::Modulator: return kSourceWeights[kWModulator];
   case Source::Expression: return kSourceWeights[kWExpression];
   case Source::Prediction: return kSourceWeights[changedThisTick ? kWPrediction : kWPredictionUntouched];
   case Source::Other: return kSourceWeights[kWOther];
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

void SmoothBins(const float* h, float out[kBins], bool wrap)
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
         if (wrap)
            j = (j % kBins + kBins) % kBins; // hue: the ends are neighbours
         else if (j < 0)
            j = -j - 1; // reflect at the walls: knobs are often parked at 0 or 1, and truncating would thin them
         else if (j >= kBins)
            j = 2 * kBins - j - 1;
         acc += kernel[k + kRadius] * h[j];
      }
      tmp[i] = acc;
      total += acc;
   }
   for (int i = 0; i < kBins; i++)
      out[i] = total > 0.0 ? static_cast<float>(tmp[i] / total) : 0.0f;
}

void SmoothedHist(const ParamStats& s, float out[kBins])
{
   SmoothBins(s.hist, out, false);
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
   mRoles.clear();
   mFamilies.clear();
   mYou = You();
   mEnergyAll = Energy();
   mEnergyRole.clear();
   mLastEnergyT = -1e18;
   mGridStarted = false;
   mNextGrid = 0;
   mTime = 0;
   mActiveClock = 0;
   mLastHandMoveT = -1e18;
   mPlaying = false;
   mWasActive = false;
}

void Engine::RegisterKey(const KeyId& id, const std::string& nodeType, const std::string& name, bool continuous,
                         const KeyMeta& metaIn)
{
   Runtime& r = mKeys[id];
   r.continuous = continuous;
   r.hasProfile = continuous;
   r.profile.nodeType = nodeType;
   r.profile.paramIndex = id.paramIndex;
   r.profile.name = name;
   r.profStats = nullptr;

   // A replay re-registers keys the live session may already know with their curve functions: keep them.
   KeyMeta meta = metaIn;
   if (meta.posToValue == nullptr && meta.valueToPos == nullptr)
   {
      meta.posToValue = r.meta.posToValue;
      meta.valueToPos = r.meta.valueToPos;
   }
   if (meta.category.empty())
      meta.category = !r.meta.category.empty() ? r.meta.category : NodeFactory::Instance().CategoryOf(nodeType);
   r.meta = meta;

   r.role = continuous ? ParamRoles::RoleFor(meta.category, name) : ParamRoles::Role();
   r.roleStats = nullptr;
   r.famStats = nullptr;
   r.centsPerUnit = 1.0f;
   if (r.role.any())
   {
      r.famUnit = ParamRoles::FamilyUnit(r.role.family, r.famShared);
      const std::string n = ParamRoles::Normalise(name);
      if (r.role.unit == ParamRoles::Unit::Cents && (n == "coarse" || n == "pitch" || n == "transpose"))
         r.centsPerUnit = 100.0f; // a semitone knob
   }
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
      r.lastObsHandT = -1e18;
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
      const float w = SourceWeight(src, flags, true);
      // Hand energy and your move speed: a position change between two writes of one grab. Only Hand
      // and Perf get here, so a prediction can never raise it.
      if (t > r.lastObsHandT && t - r.lastObsHandT <= 0.25)
      {
         const float d = std::abs(pos - r.lastObsHandPos);
         mEnergyAll.acc += d;
         if (r.role.any())
            mEnergyRole[r.role.role].acc += d;
         if (d > 1e-4f)
         {
            mYou.speedSum += w * std::min(d / std::max(t - r.lastObsHandT, 1e-3), 5.0);
            mYou.speedW += w;
         }
      }
      r.lastObsHandPos = pos;
      r.lastObsHandT = t;
      // Pause between hand writes anywhere, log-spaced from 0.15 s.
      const double gap = t - mLastHandMoveT;
      if (gap >= 0.15 && gap < 300.0)
         mYou.pauses[std::clamp(static_cast<int>(std::log2(gap / 0.15)), 0, 11)] += w;
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
      s.nPred *= f;
      const float ff = static_cast<float>(f);
      for (float& h : s.hist)
         h *= ff;
   }
   s.activeSeconds = mActiveClock;
}

void Engine::AddSample(ParamStats& s, float x, float y, bool hasPair, double w, int bin, double wPred)
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
   s.hist[std::clamp(bin, 0, kBins - 1)] += static_cast<float>(w);
   s.nEff += w;
   s.nPred += wPred;
}

namespace
{
int PosBin(float pos) { return std::clamp(static_cast<int>(pos * kBins), 0, kBins - 1); }
int UnitBin(ParamRoles::Unit u, float value)
{
   const ParamRoles::Range rg = ParamRoles::UnitRange(u);
   return std::clamp(static_cast<int>((value - rg.lo) / (rg.hi - rg.lo) * kBins), 0, kBins - 1);
}
bool IsLogUnit(ParamRoles::Unit u)
{
   return u == ParamRoles::Unit::Log2Hz || u == ParamRoles::Unit::LogSec || u == ParamRoles::Unit::Log2Val;
}
double Entropy(const float* p)
{
   double h = 0.0;
   for (int i = 0; i < kBins; i++)
      if (p[i] > 1e-9f)
         h -= p[i] * std::log(static_cast<double>(p[i]));
   return h;
}
} // namespace

float Engine::ToUnit(const Runtime& r, float pos, ParamRoles::Unit u) const
{
   if (u == ParamRoles::Unit::Pos || u == ParamRoles::Unit::Hue)
      return pos;
   const KeyMeta& m = r.meta;
   float v;
   if (m.posToValue != nullptr)
      v = m.posToValue(pos, m.minValue, m.maxValue);
   else if (m.hasCurve && m.minValue > 0.0f && IsLogUnit(u))
      v = m.minValue * std::pow(m.maxValue / m.minValue, pos); // curve function unknown (replay): assume a log taper
   else
      v = m.minValue + pos * (m.maxValue - m.minValue);
   switch (u)
   {
   case ParamRoles::Unit::Log2Hz: return std::log2(std::max(v, 1e-3f));
   case ParamRoles::Unit::Db: return m.minValue < 0.0f ? v : 20.0f * std::log10(std::max(v, 1e-4f));
   case ParamRoles::Unit::Cents:
   case ParamRoles::Unit::CentsFine: return v * r.centsPerUnit;
   case ParamRoles::Unit::LogSec: return std::log2(std::max(v, 1e-4f));
   case ParamRoles::Unit::Log2Val: return std::log2(std::max(std::abs(v), 1e-4f));
   default: return pos;
   }
}

double Engine::RebinToTarget(const float* hist, ParamRoles::Unit u, const Runtime& target, float* out) const
{
   double total = 0.0;
   for (int i = 0; i < kBins; i++)
      total += hist[i];
   if (total <= 0.0)
   {
      std::fill(out, out + kBins, 0.0f);
      return 0.0;
   }
   if (u == ParamRoles::Unit::Pos || u == ParamRoles::Unit::Hue)
   {
      std::copy(hist, hist + kBins, out);
      return 1.0;
   }

   const KeyMeta& m = target.meta;
   const ParamRoles::Range rg = ParamRoles::UnitRange(u);
   float acc[kBins] = {};
   double kept = 0.0;
   constexpr int kSub = 4; // sub-points per source bin, so a stretched axis leaves no gaps
   for (int i = 0; i < kBins; i++)
   {
      if (hist[i] <= 0.0f)
         continue;
      for (int s = 0; s < kSub; s++)
      {
         const float un = rg.lo + (i + (s + 0.5f) / kSub) / kBins * (rg.hi - rg.lo);
         float v;
         switch (u)
         {
         case ParamRoles::Unit::Db: v = m.minValue < 0.0f ? un : std::pow(10.0f, un / 20.0f); break;
         case ParamRoles::Unit::Cents:
         case ParamRoles::Unit::CentsFine: v = un / target.centsPerUnit; break;
         default: v = std::exp2(un); break; // Log2Hz, LogSec, Log2Val
         }
         float pos;
         if (m.valueToPos != nullptr)
            pos = m.valueToPos(v, m.minValue, m.maxValue);
         else if (m.hasCurve && m.minValue > 0.0f && IsLogUnit(u))
            pos = std::log(v / m.minValue) / std::log(m.maxValue / m.minValue);
         else
            pos = m.maxValue > m.minValue ? (v - m.minValue) / (m.maxValue - m.minValue) : 0.0f;
         if (!std::isfinite(pos) || pos < -0.001f || pos > 1.001f)
            continue; // outside the target's range: dropped, and renormalised away below
         acc[PosBin(std::clamp(pos, 0.0f, 1.0f))] += hist[i] / kSub;
         kept += hist[i] / kSub;
      }
   }
   std::copy(acc, acc + kBins, out);
   return kept / total;
}

void Engine::UpdateEnergy(double t)
{
   if (mLastEnergyT < -1e17)
   {
      mLastEnergyT = t;
      return;
   }
   const double dt = std::clamp(t - mLastEnergyT, 1e-3, 0.5);
   mLastEnergyT = t;
   const double alpha = 1.0 - std::exp(-dt / 1.0); // tau ~ 1 s
   auto step = [&](Energy& e) {
      e.ema += alpha * (e.acc / dt - e.ema);
      e.acc = 0.0;
   };
   step(mEnergyAll);
   for (auto& [role, e] : mEnergyRole)
      step(e);
}

float Engine::HandEnergy(const KeyId& id) const
{
   const Runtime* r = FindRuntime(id);
   const double typical = mYou.speedW >= 1.0 ? std::max(mYou.speedSum / mYou.speedW, 0.05) : 0.3;
   const double eAll = std::clamp(mEnergyAll.ema / typical, 0.0, 1.0);
   double eRole = 0.0;
   if (r != nullptr && r->role.any())
   {
      auto it = mEnergyRole.find(r->role.role);
      if (it != mEnergyRole.end())
         eRole = std::clamp(it->second.ema / typical, 0.0, 1.0);
   }
   return static_cast<float>(0.7 * eRole + 0.3 * eAll);
}

void Engine::RunMonitor(const KeyId& id, Runtime& r)
{
   if (mActiveClock < r.nextCheck)
      return;
   r.nextCheck = mActiveClock + kMonitorPeriodSec;
   if (r.stats.nEff <= 0.0)
      return;
   float p[kBins];
   SmoothedHist(r.stats, p);
   const double h = Entropy(p);
   if (!r.refValid || r.handSinceRef)
   {
      // A hand move re-anchors the reference: the model just learned something from you.
      r.refEntropy = h;
      r.refClock = mActiveClock;
      r.refValid = true;
      r.handSinceRef = false;
      return;
   }
   if (!r.cut && mActiveClock - r.refClock >= kAutoCutWindowSec && h < (1.0 - kAutoCutDrop) * r.refEntropy)
   {
      r.cut = true;
      if (mOnAutoCut)
         mOnAutoCut(id);
   }
}

bool Engine::GetMonitor(const KeyId& id, Monitor& out) const
{
   const Runtime* r = FindRuntime(id);
   if (r == nullptr || r->stats.nEff <= 0.0)
      return false;
   float p[kBins];
   SmoothedHist(r->stats, p);
   out.entropy = Entropy(p);
   out.sigma = Derive(r->stats).sigma;
   out.predShare = r->stats.nEff > 0.0 ? r->stats.nPred / r->stats.nEff : 0.0;
   out.cut = r->cut;
   return true;
}

const Engine::Runtime* Engine::FindRuntime(const KeyId& id) const
{
   auto it = mKeys.find(id);
   return it != mKeys.end() ? &it->second : nullptr;
}

const ParamStats* Engine::FindRole(const std::string& role) const
{
   auto it = mRoles.find(role);
   return it != mRoles.end() ? &it->second : nullptr;
}

const ParamStats* Engine::FindFamily(const std::string& family) const
{
   auto it = mFamilies.find(family);
   return it != mFamilies.end() ? &it->second : nullptr;
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

      const bool isPred = r.holdSrc == Source::Prediction;
      const bool isHand = r.holdSrc == Source::Hand || r.holdSrc == Source::Perf;
      double w = SourceWeight(r.holdSrc, r.holdFlags, r.changedSinceTick);
      if (isPred)
         w = (r.changedSinceTick && !r.cut) ? mPredWeight : 0.0; // an auto-cut key stops learning from itself
      r.changedSinceTick = false;
      if (w > 0.0)
      {
         const double wPred = isPred ? w : 0.0;
         const int bin = PosBin(r.holdPos);
         AddSample(r.stats, r.prevPos, r.holdPos, r.prevValid, w, bin, wPred);
         if (r.hasProfile)
         {
            if (r.profStats == nullptr)
               r.profStats = &mProfiles[r.profile];
            AddSample(*r.profStats, r.prevPos, r.holdPos, r.prevValid, w, bin, wPred);
         }
         if (r.role.any())
         {
            if (r.roleStats == nullptr)
               r.roleStats = &mRoles[r.role.role];
            AddSample(*r.roleStats, r.prevPos, r.holdPos, r.prevValid, w, UnitBin(r.role.unit, ToUnit(r, r.holdPos, r.role.unit)), wPred);
            if (r.famStats == nullptr)
               r.famStats = &mFamilies[r.role.family];
            const ParamRoles::Unit fu = r.famShared ? r.famUnit : ParamRoles::Unit::Pos;
            AddSample(*r.famStats, r.prevPos, r.holdPos, r.prevValid, w, UnitBin(fu, ToUnit(r, r.holdPos, fu)), wPred);
         }
         if (isHand)
         {
            AddSample(mYou.ar, r.prevPos, r.holdPos, r.prevValid, w, PosBin(r.holdPos)); // how, never where
            r.handSinceRef = true;
            r.cut = false; // the next hand move lifts an auto-cut
         }
      }
      r.prevPos = r.holdPos;
      r.prevValid = true;
      RunMonitor(id, r);
   }
}

void Engine::Advance(double t)
{
   mTime = t;
   UpdateEnergy(t);
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
// The fallback ladder, blended (README §5.2 / §6)
// -----------------------------------------------------------------------------

namespace
{
constexpr double kDefaultTheta = 0.5, kDefaultSigma = 0.1;

// Independent samples in a level: n_eff rows on a 10 Hz grid are strongly autocorrelated, and one row is
// far from one sample. n(1-phi)/(1+phi) (README §6).
double IndependentN(const ParamStats& s, double nowActive)
{
   const double n = EffectiveN(s, nowActive);
   if (n <= 0.0)
      return 0.0;
   double phi = 0.9;
   if (s.W > 0.0)
   {
      const double mx = s.Sx / s.W;
      const double vx = std::max(0.0, s.Sxx / s.W - mx * mx);
      phi = vx < 1e-8 ? kPhiMax : std::clamp((s.Sxy / s.W - mx * (s.Sy / s.W)) / vx, kPhiMin, kPhiMax);
   }
   return n * (1.0 - phi) / (1.0 + phi);
}

// Levels are (key, profile, role, family); rung 2c "you" joins the theta chain only. A level with
// n = 0 gets no weight and leaves its share to the levels below it.
struct Chain
{
   double w[4] = {};
   double wYou = 0, rest = 1;
};

Chain MakeChain(const double n[4], double nYou)
{
   using namespace MovementStats;
   const double n0[4] = { kN0, kN0, kN0 * kRoleN0Mult, kN0 * kFamilyN0Mult };
   Chain c;
   double rem = 1.0;
   for (int i = 0; i < 4; i++)
   {
      c.w[i] = n[i] > 0.0 ? n[i] / (n[i] + n0[i]) * rem : 0.0;
      rem -= c.w[i];
   }
   c.wYou = nYou > 0.0 ? nYou / (nYou + kN0) * rem : 0.0;
   c.rest = rem - c.wYou;
   return c;
}
} // namespace

void Engine::ComputeBlend(const KeyId& id, float anchor, Blend& out) const
{
   const Runtime* r = FindRuntime(id);
   const double now = mActiveClock;

   const ParamStats* lv[4] = {};
   ParamRoles::Unit roleUnit = ParamRoles::Unit::Pos;
   ParamRoles::Unit famUnit = ParamRoles::Unit::Pos;
   bool famLandscape = false;
   if (r != nullptr && r->continuous)
   {
      if (r->stats.nEff > 0.0)
         lv[0] = &r->stats;
      auto pit = mProfiles.find(r->profile);
      if (r->hasProfile && pit != mProfiles.end())
         lv[1] = &pit->second;
      if (r->role.any())
      {
         auto rit = mRoles.find(r->role.role);
         if (rit != mRoles.end())
            lv[2] = &rit->second;
         auto fit = mFamilies.find(r->role.family);
         if (fit != mFamilies.end())
            lv[3] = &fit->second;
         roleUnit = r->role.unit;
         famUnit = r->famShared ? r->famUnit : ParamRoles::Unit::Pos;
         famLandscape = r->famShared;
      }
   }

   // Each level's landscape in the target's fader space, and its OU fit.
   float lp[4][kBins] = {};
   bool usableP[4] = {};
   Derived d[4];
   bool usableT[4] = {};
   double nInd[4] = {};
   for (int i = 0; i < 4; i++)
   {
      if (lv[i] == nullptr)
         continue;
      nInd[i] = IndependentN(*lv[i], now);
      d[i] = Derive(*lv[i]);
      usableT[i] = d[i].valid && d[i].sigma > 0.0;
      if (i == 3 && !famLandscape)
         continue; // a mixed-unit family shares speed, never a landscape
      float raw[kBins];
      double kept;
      if (i < 2)
      {
         std::copy(lv[i]->hist, lv[i]->hist + kBins, raw);
         kept = 1.0;
      }
      else
         kept = RebinToTarget(lv[i]->hist, i == 2 ? roleUnit : famUnit, *r, raw);
      if (kept >= 0.1)
      {
         SmoothBins(raw, lp[i], r != nullptr && r->role.unit == ParamRoles::Unit::Hue && i >= 2);
         usableP[i] = true;
      }
   }
   const double nYou = mYou.ar.nEff > 0.0 ? IndependentN(mYou.ar, now) : 0.0;
   const Derived dYou = mYou.ar.nEff > 0.0 ? Derive(mYou.ar) : Derived();

   // Landscape chain.
   double nP[4];
   for (int i = 0; i < 4; i++)
      nP[i] = usableP[i] ? nInd[i] : 0.0;
   const Chain cp = MakeChain(nP, 0.0);

   float fb[kBins];
   double fbSum = 0.0;
   for (int i = 0; i < kBins; i++)
   {
      if (anchor >= 0.0f)
      {
         const float z = ((i + 0.5f) / kBins - anchor) / kAnchorWidth;
         fb[i] = std::exp(-0.5f * z * z) + 1e-4f;
      }
      else
         fb[i] = 1.0f;
      fbSum += fb[i];
   }
   double sum = 0.0;
   for (int i = 0; i < kBins; i++)
   {
      double v = cp.rest * fb[i] / fbSum; // rung 3 (community prior) would take a slot here: w3 = 0
      for (int k = 0; k < 4; k++)
         v += cp.w[k] * lp[k][i];
      out.p[i] = static_cast<float>(v);
      sum += v;
   }
   for (float& v : out.p)
      v = sum > 0.0 ? static_cast<float>(v / sum) : 1.0f / kBins;

   // Speed chain (theta, sigma): the same weights, with rung 2c before the default.
   double nT[4];
   for (int i = 0; i < 4; i++)
      nT[i] = usableT[i] ? nInd[i] : 0.0;
   const bool youOk = dYou.valid && dYou.sigma > 0.0;
   const Chain ct = MakeChain(nT, youOk ? nYou : 0.0);
   double lnTheta = ct.rest * std::log(kDefaultTheta), lnSigma = ct.rest * std::log(kDefaultSigma);
   for (int k = 0; k < 4; k++)
      if (ct.w[k] > 0.0)
      {
         lnTheta += ct.w[k] * std::log(d[k].theta);
         lnSigma += ct.w[k] * std::log(std::max(d[k].sigma, 1e-3));
      }
   if (ct.wYou > 0.0)
   {
      lnTheta += ct.wYou * std::log(dYou.theta);
      lnSigma += ct.wYou * std::log(std::max(dYou.sigma, 1e-3));
   }
   out.theta = static_cast<float>(std::exp(lnTheta));
   out.sigma = static_cast<float>(std::exp(lnSigma));

   out.w1 = cp.w[0]; out.w2 = cp.w[1]; out.w2b = cp.w[2]; out.w2d = cp.w[3];
   out.w3 = 0.0; out.wYou = ct.wYou; out.w4 = cp.rest;
   out.nKey = lv[0] != nullptr ? EffectiveN(*lv[0], now) : 0.0;

   // Range: declared min/max (the whole fader) until the key's own data carries real weight.
   out.lo = 0.0f;
   out.hi = 1.0f;
   if (out.w1 > 0.2 && lv[0] != nullptr)
   {
      out.lo = static_cast<float>(d[0].rangeLo);
      out.hi = static_cast<float>(d[0].rangeHi);
      if (out.hi - out.lo < 0.05f)
      {
         const float c = 0.5f * (out.lo + out.hi);
         out.lo = std::max(0.0f, c - 0.025f);
         out.hi = std::min(1.0f, c + 0.025f);
      }
   }
}

// -----------------------------------------------------------------------------
// Persistence
// -----------------------------------------------------------------------------

namespace
{
constexpr char kMagic[6] = {'I', 'M', 'S', 'T', 'A', 'T'};
constexpr uint16_t kVersion = 2;
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
      put(s.releaseVel); put(s.nEff); put(s.nPred); put(s.activeSeconds);
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
      s.releaseVel = get<float>(); s.nEff = get<double>(); s.nPred = get<double>(); s.activeSeconds = get<double>();
      return s;
   }
};

bool StatsSane(const ParamStats& s)
{
   const double v[] = {s.W, s.Sx, s.Sy, s.Sxx, s.Sxy, s.Syy, s.nEff, s.nPred, s.activeSeconds, s.releaseVel};
   for (double x : v)
      if (!std::isfinite(x))
         return false;
   if (s.W < 0.0 || s.nEff < 0.0 || s.nPred < 0.0 || s.activeSeconds < 0.0)
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
      w.put(static_cast<uint8_t>(r.cut ? 1 : 0));
      w.put(static_cast<uint8_t>(r.refValid ? 1 : 0));
      w.put(r.refEntropy);
      w.put(r.refClock);
   }

   w.put(static_cast<uint32_t>(mProfiles.size()));
   for (const auto& [k, s] : mProfiles)
   {
      w.putString(k.nodeType);
      w.put(k.paramIndex);
      w.putString(k.name);
      w.putStats(s);
   }

   auto putMap = [&](const std::unordered_map<std::string, ParamStats>& m) {
      uint32_t n = 0;
      for (const auto& [name, st] : m)
         if (st.nEff > 0.0)
            n++;
      w.put(n);
      for (const auto& [name, st] : m)
      {
         if (st.nEff <= 0.0)
            continue;
         w.putString(name);
         w.putStats(st);
      }
   };
   putMap(mRoles);
   putMap(mFamilies);
   w.putStats(mYou.ar);
   w.put(mYou.speedSum);
   w.put(mYou.speedW);
   for (float p : mYou.pauses)
      w.put(p);

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
      Runtime& rt = keys[id];
      rt.cut = r.get<uint8_t>() != 0;
      rt.refValid = r.get<uint8_t>() != 0;
      rt.refEntropy = r.get<double>();
      rt.refClock = r.get<double>();
      if (!r.ok || !StatsSane(s) || s.activeSeconds > clock + 1e-6 || !std::isfinite(rt.refEntropy) ||
          !std::isfinite(rt.refClock))
         return false;
      rt.stats = s;
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
   auto getMap = [&](std::unordered_map<std::string, ParamStats>& m) {
      const uint32_t n = r.get<uint32_t>();
      if (!r.ok || n > kMaxEntries)
         return false;
      for (uint32_t i = 0; i < n && r.ok; i++)
      {
         std::string name = r.getString();
         ParamStats st = r.getStats();
         if (!r.ok || !StatsSane(st) || st.activeSeconds > clock + 1e-6)
            return false;
         m[std::move(name)] = st;
      }
      return r.ok;
   };
   std::unordered_map<std::string, ParamStats> roles, families;
   if (!getMap(roles) || !getMap(families))
      return false;
   You you;
   you.ar = r.getStats();
   you.speedSum = r.get<double>();
   you.speedW = r.get<double>();
   for (float& p : you.pauses)
      p = r.get<float>();
   if (!r.ok || !StatsSane(you.ar) || you.ar.activeSeconds > clock + 1e-6 || !std::isfinite(you.speedSum) ||
       !std::isfinite(you.speedW) || you.speedSum < 0.0 || you.speedW < 0.0)
      return false;
   for (float p : you.pauses)
      if (!std::isfinite(p) || p < 0.0f)
         return false;
   if (r.p != r.end)
      return false;

   mKeys = std::move(keys);
   mProfiles = std::move(profiles);
   mRoles = std::move(roles);
   mFamilies = std::move(families);
   mYou = you;
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
         {
            KeyMeta meta;
            meta.minValue = rec.key.minValue;
            meta.maxValue = rec.key.maxValue;
            meta.hasCurve = rec.key.hasCurve;
            RegisterKey(id, rec.key.typeName, rec.key.name, !(rec.key.isEnum || rec.key.isBool), meta);
         }
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
         case MovementLog::Mark::AutoCut:
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
       a.Syy != b.Syy || a.nEff != b.nEff || a.nPred != b.nPred || a.activeSeconds != b.activeSeconds ||
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
