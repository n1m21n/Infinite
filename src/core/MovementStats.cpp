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

// How much a sample is still worth after the hold has sat unchanged for `holdTicks` ticks.
// See kDwellFullTicks in the header for why this exists. Prediction and other machine sources
// are budgeted too: an LFO parked at a rail is the same failure mode with a different author.
double DwellWeight(int holdTicks)
{
   if (holdTicks <= kDwellFullTicks)
      return 1.0;
   return std::exp(-(double)(holdTicks - kDwellFullTicks) / kDwellDecayTicks);
}

bool PoolIsHuman(Pool p) { return p == kPoolDeliberate || p == kPoolExploratory; }

Pool ClassifyPool(Source src, uint8_t flags, bool changed)
{
   switch (src)
   {
   case Source::Hand:
   case Source::Perf:
      // A correction is the strongest statement the system ever receives - the human watched the
      // model be wrong and said where it should have been - so it is deliberate regardless of
      // whether the hand happened to be moving on this particular tick. Otherwise the split is
      // transit vs settled: still moving is a search, not yet a decision.
      if (flags & MovementLog::kCorrection)
         return kPoolDeliberate;
      return changed ? kPoolExploratory : kPoolDeliberate;
   case Source::Gesture: return kPoolReplay;
   case Source::Modulator:
   case Source::Expression:
   case Source::Prediction:
   case Source::Other:
      break;
   }
   return kPoolAuto;
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
   mBarSummaries.clear();
   mCurrentSectionId = 0;
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
      r.holdTicks = 0;
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
   r.holdTicks = 0;
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
// Normalised, smoothed landscape of one pool. Returns false when the pool is empty.
bool PoolDensity(const ParamStats& s, float out[kBins])
{
   if (s.nEff <= 0.0)
      return false;
   SmoothedHist(s, out);
   double sum = 0;
   for (int i = 0; i < kBins; i++)
      sum += out[i];
   if (sum <= 0.0)
      return false;
   for (int i = 0; i < kBins; i++)
      out[i] = static_cast<float>(out[i] / sum);
   return true;
}
} // namespace

void Engine::ScorePools(Runtime& r, int bin)
{
   // Prequential: every pool is graded on this sample using only what it already holds, then the
   // sample is absorbed by its own pool afterwards. Without that ordering a pool would be scored
   // on data it had just been fitted to and would always appear to be the best expert.
   const double f = std::exp2(-(mActiveClock - r.scoreClock) / kScoreHalfLifeSec);
   if (f < 1.0 && r.scoreW > 0.0)
   {
      for (int i = 0; i < kNumPools; i++)
         r.scoreSum[i] *= f;
      r.scoreW *= f;
   }
   r.scoreClock = mActiveClock;

   constexpr double kEps = 1e-4;   // ~1/10 of a uniform bin: bounds the loss of a total miss
   float dens[kBins];
   for (int i = 0; i < kNumPools; i++)
   {
      // An empty pool scores the uniform distribution rather than nothing, so it is neither
      // rewarded nor destroyed by its own emptiness - kPoolShares' maturity term is what holds
      // it back until it has data.
      const double p = PoolDensity(r.pools[i], dens) ? dens[std::clamp(bin, 0, kBins - 1)]
                                                     : 1.0 / kBins;
      r.scoreSum[i] += -std::log(p + kEps);
   }
   r.scoreW += 1.0;
}

namespace
{
// share_i is proportional to prior x maturity x exp(-mean log-loss / tau). Nothing in it is a
// function of elapsed time, which is the whole point: a pool earns its share by being right about
// where the hand goes, never by having run longer than the others.
void SharesFrom(const ParamStats* pools, const double* scoreSum, double scoreW, double now,
                PoolShares& out)
{
   double raw[kNumPools] = {};
   double total = 0;
   for (int i = 0; i < kNumPools; i++)
   {
      // Decayed, not raw: pools are forgotten lazily (only a write to that pool applies the
      // factor), so a pool nobody has fed for a month would otherwise keep its full vote.
      const double n = EffectiveN(pools[i], now);
      out.n[i] = n;
      out.maturity[i] = static_cast<float>(n / (n + kN0));
      const double meanL = scoreW > 0.0 ? scoreSum[i] / scoreW : 0.0;
      raw[i] = kPoolPrior[i] * out.maturity[i] * std::exp(-meanL / kPoolTau);
      total += raw[i];
   }
   if (total <= 0.0)
   {
      // Nothing learned anywhere: fall back to the prior so a brand-new key still has a mix.
      for (int i = 0; i < kNumPools; i++)
         out.share[i] = static_cast<float>(kPoolPrior[i]);
      out.valid = false;
      return;
   }
   for (int i = 0; i < kNumPools; i++)
      out.share[i] = static_cast<float>(raw[i] / total);
   out.valid = true;
}
} // namespace

void Engine::MixPools(const Runtime& r, ParamStats& out) const
{
   PoolShares sh;
   SharesFrom(r.pools, r.scoreSum, r.scoreW, mActiveClock, sh);

   out = ParamStats();
   out.activeSeconds = mActiveClock;

   // Landscape: each pool contributes its own *shape*, scaled by its share. Because every pool is
   // normalised to itself first, eight hours of LFO and three seconds of hand arrive at the same
   // size and only the share decides which is louder.
   double totalN = 0;
   for (int i = 0; i < kNumPools; i++)
      totalN += sh.n[i];
   if (totalN <= 0.0)
      return;

   float dens[kBins];
   for (int i = 0; i < kNumPools; i++)
   {
      if (!PoolDensity(r.pools[i], dens))
         continue;
      const float k = static_cast<float>(sh.share[i] * totalN);
      for (int b = 0; b < kBins; b++)
         out.hist[b] += dens[b] * k;
   }
   out.nEff = totalN;

   // Cadence (phi/theta/sigma) comes from the human pools only, at full strength. A sine LFO's
   // dwell is the arcsine law - it peaks at both rails - and its AR(1) fit is the LFO's rate, not
   // the hand's, so letting automation set theta/sigma would teach the model to move at the speed
   // of whatever was left running. Ranges from automation are fair; timing from it is not.
   for (int i = 0; i < kNumPools; i++)
   {
      if (!PoolIsHuman(static_cast<Pool>(i)))
         continue;
      const ParamStats& p = r.pools[i];
      const double f = DecayFactor(p.activeSeconds, mActiveClock);
      out.W += p.W * f; out.Sx += p.Sx * f; out.Sy += p.Sy * f;
      out.Sxx += p.Sxx * f; out.Sxy += p.Sxy * f; out.Syy += p.Syy * f;
   }
   out.releaseVel = r.pools[kPoolDeliberate].releaseVel != 0.0f
                       ? r.pools[kPoolDeliberate].releaseVel
                       : r.pools[kPoolExploratory].releaseVel;
   out.nPred = r.stats.nPred;
}

bool Engine::GetPoolShares(const KeyId& id, PoolShares& out) const
{
   const Runtime* r = FindRuntime(id);
   if (r == nullptr)
      return false;
   SharesFrom(r->pools, r->scoreSum, r->scoreW, mActiveClock, out);
   return true;
}

void Engine::GetGlobalPoolShares(PoolShares& out) const
{
   // Pool the per-key evidence, then run the same formula once. Summing n and the scores (rather
   // than averaging finished shares) keeps a knob you have played for an hour from being outvoted
   // by fifty knobs you have never touched.
   ParamStats agg[kNumPools];
   double scoreSum[kNumPools] = {};
   double scoreW = 0;
   for (const auto& [id, r] : mKeys)
   {
      for (int i = 0; i < kNumPools; i++)
      {
         agg[i].nEff += EffectiveN(r.pools[i], mActiveClock);
         scoreSum[i] += r.scoreSum[i];
      }
      scoreW += r.scoreW;
   }
   SharesFrom(agg, scoreSum, scoreW, mActiveClock, out);
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
      const Pool pool = ClassifyPool(r.holdSrc, r.holdFlags, r.changedSinceTick);
      double w = SourceWeight(r.holdSrc, r.holdFlags, r.changedSinceTick);
      if (isPred)
         w = (r.changedSinceTick && !r.cut) ? mPredWeight : 0.0; // an auto-cut key stops learning from itself
      // The hold only ever changes through Observe, which is only called when the value changes,
      // so an untouched knob re-absorbs its resting position on every tick for the rest of the
      // session. Charge that against the dwell budget.
      r.holdTicks = r.changedSinceTick ? 0 : (r.holdTicks + 1);
      w *= DwellWeight(r.holdTicks);
      r.changedSinceTick = false;
      if (w > 0.0)
      {
         const double wPred = isPred ? w : 0.0;
         const int bin = PosBin(r.holdPos);
         if (isHand && r.holdTicks == 0)
         {
            // Grade the pools on where the hand actually went BEFORE this sample is absorbed -
            // and only on a sample the hand actually produced. A stale hold re-scores the same
            // bin on every tick, which hands a runaway win to whichever pool already peaks
            // there: the same unbounded-repetition problem the dwell budget fixes for the
            // landscape, one layer up in the grader that decides which pool is trusted.
            ScorePools(r, bin);
            // Presence is "a human is in the room", and the only evidence of that is a value
            // that actually moved - which is why this sits inside the holdTicks == 0 branch. A
            // stale Hand hold is re-read forever, so crediting presence for it left the gate
            // permanently open after the first knob move of the session, which is exactly the
            // overnight-automation flood the gate exists to stop.
            mLastPresenceActive = mActiveClock;
         }
         // A machine source may only accumulate while a human was recently here. Transport
         // running is not presence: an arpeggiator or an LFO left going overnight would
         // otherwise out-sample a real performance by two orders of magnitude. Prediction rows
         // never enter a pool at all - their share would be derived from pools they also feed.
         const bool present = (mActiveClock - mLastPresenceActive) <= kPresenceWindowSec;
         if (!isPred && (PoolIsHuman(pool) || present))
            AddSample(r.pools[pool], r.prevPos, r.holdPos, r.prevValid, w, bin, 0.0);
         // The same gate has to hold at every rung, not just at the key's own pools. The
         // profile/role/family tables below are what the ladder falls back to when a key is
         // young, so letting an unattended overnight run flood them would reintroduce exactly
         // the time skew the pools were built to remove - one rung higher, where it is harder
         // to see. Prediction rows are exempt: they carry wPred, which is what predShare and
         // the anti-collapse auto-cut read, and they are never pooled in the first place.
         const bool absorb = isPred || PoolIsHuman(pool) || present;
         if (absorb)
         {
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
      }
      float delta = r.prevValid ? (r.holdPos - r.prevPos) : 0.0f;
      r.posHistory.push_back(r.holdPos);
      r.deltaHistory.push_back(delta);
      if (r.posHistory.size() > Runtime::kHistoryCap)
      {
         r.posHistory.erase(r.posHistory.begin());
         r.deltaHistory.erase(r.deltaHistory.begin());
      }
      if (mCurrentSectionId >= 0 && w > 0.0)
      {
         auto& sHist = r.sectionHist[mCurrentSectionId];
         if (sHist.size() < kBins)
            sHist.resize(kBins, 0.0f);
         const int bin = PosBin(r.holdPos);
         sHist[bin] += (float)w;
         r.sectionNEff[mCurrentSectionId] += w;
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

void Engine::ComputeBlend(const KeyId& id, float anchor, Blend& out, int sectionId) const
{
   const Runtime* r = FindRuntime(id);
   const double now = mActiveClock;

   const ParamStats* lv[4] = {};
   ParamRoles::Unit roleUnit = ParamRoles::Unit::Pos;
   ParamRoles::Unit famUnit = ParamRoles::Unit::Pos;
   bool famLandscape = false;
   // Rung 1 is no longer the raw sum of everything that ever moved this key: it is the pools
   // mixed at their emergent shares (landscape from all of them, cadence from the human ones).
   ParamStats mixed;
   if (r != nullptr && r->continuous)
   {
      if (r->stats.nEff > 0.0)
      {
         MixPools(*r, mixed);
         lv[0] = mixed.nEff > 0.0 ? &mixed : &r->stats;
      }
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

   // 7e Section-conditioned landscape blending: if sectionId >= 0, blend section histogram by its nEff
   if (r != nullptr && sectionId >= 0)
   {
      auto itNEff = r->sectionNEff.find(sectionId);
      auto itHist = r->sectionHist.find(sectionId);
      if (itNEff != r->sectionNEff.end() && itHist != r->sectionHist.end() && itNEff->second > 0.0)
      {
         float sSmoothed[kBins] = {};
         SmoothBins(itHist->second.data(), sSmoothed, false);
         const double nSec = itNEff->second;
         const double wSec = nSec / (nSec + kN0);
         double secSum = 0.0;
         for (int i = 0; i < kBins; i++)
         {
            out.p[i] = static_cast<float>(wSec * sSmoothed[i] + (1.0 - wSec) * out.p[i]);
            secSum += out.p[i];
         }
         if (secSum > 0.0)
         {
            for (int i = 0; i < kBins; i++)
               out.p[i] = static_cast<float>(out.p[i] / secSum);
         }
      }
   }

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
   out.nKeyIndependent = nP[0] > 0.0 ? nP[0] : nInd[0];

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
// v3 adds the per-key source pools and their prequential scores. v2 files still load: everything
// they hold was written before the split existed, so it is credited to DELIBERATE - the pool a
// hand-moved knob's settled position would have landed in anyway.
constexpr uint16_t kVersion = 3;
constexpr uint16_t kVersionPooled = 3;
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
bool OpenBlob(const uint8_t* data, size_t size, Reader& r, uint16_t& version)
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
   version = r.get<uint16_t>();
   return (version == kVersion || version == 2) && r.ok;
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
      for (int i = 0; i < kNumPools; i++)
      {
         w.putStats(r.pools[i]);
         w.put(r.scoreSum[i]);
      }
      w.put(r.scoreW);
      w.put(r.scoreClock);
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
   uint16_t version = 0;
   if (!OpenBlob(data, size, r, version))
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
      if (version >= kVersionPooled)
      {
         for (int pi = 0; pi < kNumPools; pi++)
         {
            rt.pools[pi] = r.getStats();
            rt.scoreSum[pi] = r.get<double>();
            if (!r.ok || !StatsSane(rt.pools[pi]) || rt.pools[pi].activeSeconds > clock + 1e-6 ||
                !std::isfinite(rt.scoreSum[pi]) || rt.scoreSum[pi] < 0.0)
               return false;
         }
         rt.scoreW = r.get<double>();
         rt.scoreClock = r.get<double>();
         if (!r.ok || !std::isfinite(rt.scoreW) || rt.scoreW < 0.0 || !std::isfinite(rt.scoreClock) ||
             rt.scoreClock < 0.0 || rt.scoreClock > clock + 1e-6)
            return false;
      }
      else
      {
         // v2 migration: one undifferentiated pile becomes DELIBERATE. Crediting it to the
         // strongest human pool keeps every existing user's learned landscape intact and errs
         // toward trusting them; the shares re-learn from the next hand move either way.
         rt.pools[kPoolDeliberate] = s;
      }
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

// -----------------------------------------------------------------------------
// Step 7: v2 Prediction Modes Implementations
// -----------------------------------------------------------------------------

FollowFit Engine::FitFollow(const KeyId& follower, const KeyId& leader, int maxLagTicks, float ridgeLambda) const
{
   FollowFit res;
   const Runtime* rFollow = FindRuntime(follower);
   const Runtime* rLead = FindRuntime(leader);
   if (!rFollow || !rLead)
      return res;

   const size_t nF = rFollow->deltaHistory.size();
   const size_t nL = rLead->deltaHistory.size();
   const size_t n = std::min(nF, nL);
   if (n < 20)
      return res;

   // 1. Cross-correlate VELOCITIES (Delta x) across lag tau >= 0 (up to maxLagTicks)
   float bestCorr = -1e9f;
   int bestLag = 0;

   for (int tau = 0; tau <= maxLagTicks && (size_t)tau + 10 < n; tau++)
   {
      double sumF = 0, sumL = 0, sumF2 = 0, sumL2 = 0, sumFL = 0;
      size_t count = 0;
      for (size_t i = tau; i < n; i++)
      {
         float dF = rFollow->deltaHistory[i];
         float dL = rLead->deltaHistory[i - tau];
         sumF += dF; sumL += dL;
         sumF2 += dF * dF; sumL2 += dL * dL;
         sumFL += dF * dL;
         count++;
      }
      if (count < 10)
         continue;
      double mF = sumF / count;
      double mL = sumL / count;
      double varF = std::max(0.0, sumF2 / count - mF * mF);
      double varL = std::max(0.0, sumL2 / count - mL * mL);
      if (varF > 1e-9 && varL > 1e-9)
      {
         double cov = sumFL / count - mF * mL;
         double corr = std::abs(cov) / std::sqrt(varF * varL);
         if (corr > bestCorr)
         {
            bestCorr = (float)corr;
            bestLag = tau;
         }
      }
   }

   if (bestCorr < 0.0f)
      return res;

   // 2. Ridge regression on levels with lag bestLag: x_B(t) = beta * x_A(t - bestLag) + c
   // Split 80% train / 20% test for holdout validation
   const size_t nPos = std::min(rFollow->posHistory.size(), rLead->posHistory.size());
   if (nPos <= (size_t)bestLag + 10)
      return res;

   const size_t totalPairs = nPos - bestLag;
   const size_t trainPairs = (totalPairs * 4) / 5;
   const size_t testPairs = totalPairs - trainPairs;
   if (trainPairs < 10 || testPairs < 4)
      return res;

   double sL = 0, sF = 0, sL2 = 0, sLF = 0;
   for (size_t i = 0; i < trainPairs; i++)
   {
      float pF = rFollow->posHistory[bestLag + i];
      float pL = rLead->posHistory[i];
      sL += pL; sF += pF;
      sL2 += pL * pL; sLF += pL * pF;
   }
   double mL = sL / trainPairs;
   double mF = sF / trainPairs;
   double varL = sL2 / trainPairs - mL * mL;
   double covLF = sLF / trainPairs - mL * mF;

   // Ridge beta = covLF / (varL + lambda)
   float beta = (float)(covLF / (varL + (double)ridgeLambda));
   float c = (float)(mF - beta * mL);

   // Evaluate R^2 on test split
   double testSSE = 0, testSST = 0;
   double testMF = 0;
   for (size_t i = trainPairs; i < totalPairs; i++)
      testMF += rFollow->posHistory[bestLag + i];
   testMF /= testPairs;

   for (size_t i = trainPairs; i < totalPairs; i++)
   {
      float actual = rFollow->posHistory[bestLag + i];
      float pred = beta * rLead->posHistory[i] + c;
      testSSE += (actual - pred) * (actual - pred);
      testSST += (actual - testMF) * (actual - testMF);
   }

   float r2 = testSST > 1e-6 ? (float)(1.0 - testSSE / testSST) : 0.0f;

   res.valid = true;
   res.tauTicks = bestLag;
   res.tauSec = bestLag * (float)kGridDt;
   res.beta = beta;
   res.c = c;
   res.r2 = r2;
   res.corrVel = bestCorr;
   return res;
}

void Engine::RecordBarSummary(int barIndex, double beat)
{
   BarSummary bs;
   bs.barIndex = barIndex;
   bs.beat = beat;

   for (const auto& [id, r] : mKeys)
   {
      if (!r.continuous || r.posHistory.empty())
         continue;

      size_t samples = std::min((size_t)10, r.posHistory.size());
      float mean = 0.0f;
      for (size_t i = r.posHistory.size() - samples; i < r.posHistory.size(); i++)
         mean += r.posHistory[i];
      mean /= samples;

      float slope = (r.posHistory.back() - r.posHistory[r.posHistory.size() - samples]) / (float)samples;

      BarSummary::KeySummary ks;
      ks.id = id;
      ks.meanPos = mean;
      ks.slopePos = slope;
      bs.keys.push_back(ks);
   }

   mBarSummaries.push_back(bs);
   if (mBarSummaries.size() > SessionMap::kMaxColumns)
      mBarSummaries.erase(mBarSummaries.begin());
}

RecallMatch Engine::SearchRecallIndex(const std::vector<BarSummary::KeySummary>& query, int queryBars) const
{
   RecallMatch match;
   if (mBarSummaries.size() < 2 || query.empty())
      return match;

   float bestDist = 1e9f;
   int bestBar = -1;

   int searchLimit = std::max(0, (int)mBarSummaries.size() - queryBars - 1);
   for (int b = 0; b < searchLimit; b++)
   {
      const auto& candidate = mBarSummaries[b];
      float totalDist = 0.0f;
      int matchedKeys = 0;

      for (const auto& qk : query)
      {
         for (const auto& ck : candidate.keys)
         {
            if (qk.id == ck.id)
            {
               float dm = qk.meanPos - ck.meanPos;
               float ds = qk.slopePos - ck.slopePos;
               totalDist += dm * dm + 0.5f * ds * ds;
               matchedKeys++;
               break;
            }
         }
      }

      if (matchedKeys > 0)
      {
         float avgDist = totalDist / matchedKeys;
         if (avgDist < bestDist)
         {
            bestDist = avgDist;
            bestBar = candidate.barIndex;
         }
      }
   }

   if (bestBar >= 0)
   {
      match.found = true;
      match.matchedBar = bestBar;
      match.similarity = 1.0f / (1.0f + bestDist);
   }
   return match;
}

void Engine::ComputeSessionMap(SessionMap& out, int noveltyKernelHalfSize) const
{
   out.numBars = (int)mBarSummaries.size();
   if (out.numBars == 0)
      return;

   // 1. L2-normalized column vector per bar
   out.columns.resize(out.numBars);
   for (int b = 0; b < out.numBars; b++)
   {
      const auto& bs = mBarSummaries[b];
      std::vector<float> col;
      col.reserve(bs.keys.size());
      double normSq = 0.0;
      for (const auto& k : bs.keys)
      {
         col.push_back(k.meanPos);
         normSq += k.meanPos * k.meanPos;
      }
      double norm = std::sqrt(normSq);
      if (norm > 1e-6)
      {
         for (auto& v : col)
            v = (float)(v / norm);
      }
      out.columns[b] = std::move(col);
   }

   // 2. Pairwise cosine similarity matrix S (numBars x numBars)
   out.similarityMatrix.resize(out.numBars * out.numBars, 0.0f);
   for (int i = 0; i < out.numBars; i++)
   {
      out.similarityMatrix[i * out.numBars + i] = 1.0f;
      for (int j = i + 1; j < out.numBars; j++)
      {
         const auto& ci = out.columns[i];
         const auto& cj = out.columns[j];
         size_t len = std::min(ci.size(), cj.size());
         float dot = 0.0f;
         for (size_t k = 0; k < len; k++)
            dot += ci[k] * cj[k];
         dot = std::clamp(dot, -1.0f, 1.0f);
         out.similarityMatrix[i * out.numBars + j] = dot;
         out.similarityMatrix[j * out.numBars + i] = dot;
      }
   }

   // 3. Foote novelty curve along diagonal
   out.noveltyCurve.resize(out.numBars, 0.0f);
   const int L = std::max(1, noveltyKernelHalfSize);
   for (int i = L; i < out.numBars - L; i++)
   {
      float score = 0.0f;
      for (int a = -L; a < L; a++)
      {
         for (int b = -L; b < L; b++)
         {
            float sign = ((a < 0 && b < 0) || (a >= 0 && b >= 0)) ? 1.0f : -1.0f;
            int r = i + a;
            int c = i + b;
            score += sign * out.similarityMatrix[r * out.numBars + c];
         }
      }
      out.noveltyCurve[i] = std::max(0.0f, score / (2.0f * L * L));
   }

   // 4. Section segmentation from peaks in novelty curve
   float maxNov = 0.0f;
   for (float v : out.noveltyCurve)
      maxNov = std::max(maxNov, v);
   float threshold = std::max(0.02f, 0.35f * maxNov);

   std::vector<int> boundaries = { 0 };
   for (int i = L; i < out.numBars - L; i++)
   {
      if (out.noveltyCurve[i] >= threshold &&
          (i == 0 || out.noveltyCurve[i] >= out.noveltyCurve[i - 1]) &&
          (i + 1 >= out.numBars || out.noveltyCurve[i] >= out.noveltyCurve[i + 1]))
      {
         boundaries.push_back(i);
         i += L;
      }
   }
   boundaries.push_back(out.numBars);

   out.sections.clear();
   const char labels[] = "ABCDEFGH";
   for (size_t s = 0; s + 1 < boundaries.size(); s++)
   {
      SessionSection sec;
      sec.startBar = boundaries[s];
      sec.endBar = boundaries[s + 1];
      sec.sectionId = (int)s;
      char base = labels[std::min((size_t)s, sizeof(labels) - 2)];
      sec.label = std::string(1, base);
      out.sections.push_back(sec);
   }
}

MovesPCA Engine::ComputeDeltaPCA(int maxComponents) const
{
   MovesPCA res;
   std::vector<KeyId> validKeys;
   for (const auto& [id, r] : mKeys)
   {
      if (r.continuous && r.deltaHistory.size() >= 20)
         validKeys.push_back(id);
   }

   if (validKeys.size() < 2)
      return res;

   const size_t P = validKeys.size();
   size_t N = 10000;
   for (const auto& k : validKeys)
   {
      const auto* r = FindRuntime(k);
      if (r)
         N = std::min(N, r->deltaHistory.size());
   }

   if (N < 20)
      return res;

   std::vector<double> means(P, 0.0);
   for (size_t j = 0; j < P; j++)
   {
      const auto* r = FindRuntime(validKeys[j]);
      double sum = 0;
      for (size_t i = 0; i < N; i++)
         sum += r->deltaHistory[i];
      means[j] = sum / N;
   }

   std::vector<double> C(P * P, 0.0);
   for (size_t j1 = 0; j1 < P; j1++)
   {
      const auto* r1 = FindRuntime(validKeys[j1]);
      for (size_t j2 = j1; j2 < P; j2++)
      {
         const auto* r2 = FindRuntime(validKeys[j2]);
         double cov = 0;
         for (size_t i = 0; i < N; i++)
            cov += (r1->deltaHistory[i] - means[j1]) * (r2->deltaHistory[i] - means[j2]);
         cov /= (N - 1);
         C[j1 * P + j2] = cov;
         C[j2 * P + j1] = cov;
      }
   }

   int kMax = std::min((int)P, maxComponents);
   res.numComponents = kMax;
   res.keys = validKeys;
   res.W.resize(kMax, std::vector<float>(P, 0.0f));
   res.explainedVarianceRatio.resize(kMax, 0.0f);

   double totalVar = 0.0;
   for (size_t j = 0; j < P; j++)
      totalVar += C[j * P + j];
   res.totalVariance = (float)totalVar;

   std::vector<double> C_def = C;
   for (int comp = 0; comp < kMax; comp++)
   {
      std::vector<double> v(P, 1.0 / std::sqrt((double)P));
      double lambda = 0.0;
      for (int iter = 0; iter < 100; iter++)
      {
         std::vector<double> v_next(P, 0.0);
         for (size_t r = 0; r < P; r++)
            for (size_t c = 0; c < P; c++)
               v_next[r] += C_def[r * P + c] * v[c];

         double norm = 0.0;
         for (size_t r = 0; r < P; r++)
            norm += v_next[r] * v_next[r];
         norm = std::sqrt(norm);
         if (norm < 1e-12)
            break;
         for (size_t r = 0; r < P; r++)
            v[r] = v_next[r] / norm;
         lambda = norm;
      }

      for (size_t j = 0; j < P; j++)
         res.W[comp][j] = (float)v[j];

      res.explainedVarianceRatio[comp] = totalVar > 1e-9 ? (float)(lambda / totalVar) : 0.0f;

      for (size_t r = 0; r < P; r++)
         for (size_t c = 0; c < P; c++)
            C_def[r * P + c] -= lambda * v[r] * v[c];
   }

   res.valid = true;
   return res;
}

DMDFit Engine::FitDMD(int maxRank) const
{
   DMDFit res;
   std::vector<KeyId> validKeys;
   for (const auto& [id, r] : mKeys)
   {
      if (r.continuous && r.posHistory.size() >= 20)
         validKeys.push_back(id);
   }
   if (validKeys.size() < 2)
      return res;

   const size_t P = validKeys.size();
   size_t N = 10000;
   for (const auto& k : validKeys)
   {
      const auto* r = FindRuntime(k);
      if (r)
         N = std::min(N, r->posHistory.size());
   }
   if (N < 20)
      return res;

   int rank = std::min((int)P, maxRank);
   const int D = rank + 1; // augmented with one constant feature, for the affine fit below
   res.rank = rank;
   res.keys = validKeys;
   res.A.resize((size_t)rank * D, 0.0f);
   res.noiseStd.assign(rank, 0.0f);

   // Affine fit: x2 = Asub*x1 + bias, not the purely homogeneous x2 = A*x1 this used to solve.
   // A homogeneous map can only ever settle to the fixed point at the origin, so any real,
   // off-centre signal (almost everything patched into Predictive Modulator) free-ran straight
   // to 0 and sat there - "learned it, then went static" - regardless of how good the fit was.
   // The constant feature (x1_aug[rank] == 1 below) lets the model land on the actual observed
   // operating point instead.
   std::vector<double> H1H1T((size_t)D * D, 0.0);
   std::vector<double> H2H1T((size_t)rank * D, 0.0);

   std::vector<double> x1(D);
   for (size_t t = 0; t + 1 < N; t++)
   {
      for (int i = 0; i < rank; i++)
      {
         const auto* ri = FindRuntime(validKeys[i]);
         x1[i] = ri ? ri->posHistory[t] : 0.0;
      }
      x1[rank] = 1.0;
      for (int i = 0; i < D; i++)
         for (int j = 0; j < D; j++)
            H1H1T[i * D + j] += x1[i] * x1[j];
      for (int i = 0; i < rank; i++)
      {
         const auto* ri = FindRuntime(validKeys[i]);
         double x2_i = ri ? ri->posHistory[t + 1] : 0.0;
         for (int j = 0; j < D; j++)
            H2H1T[i * D + j] += x2_i * x1[j];
      }
   }

   for (int i = 0; i < D; i++)
      H1H1T[i * D + i] += 1e-4;

   std::vector<double> invH1H1T((size_t)D * D, 0.0);
   for (int i = 0; i < D; i++)
      invH1H1T[i * D + i] = 1.0;

   std::vector<double> M = H1H1T;
   for (int i = 0; i < D; i++)
   {
      double pivot = M[i * D + i];
      if (std::abs(pivot) < 1e-9)
         pivot = 1e-9;
      for (int j = 0; j < D; j++)
      {
         M[i * D + j] /= pivot;
         invH1H1T[i * D + j] /= pivot;
      }
      for (int r = 0; r < D; r++)
      {
         if (r == i)
            continue;
         double factor = M[r * D + i];
         for (int j = 0; j < D; j++)
         {
            M[r * D + j] -= factor * M[i * D + j];
            invH1H1T[r * D + j] -= factor * invH1H1T[i * D + j];
         }
      }
   }

   std::vector<double> A_aug((size_t)rank * D, 0.0);
   for (int i = 0; i < rank; i++)
   {
      for (int j = 0; j < D; j++)
      {
         double sum = 0.0;
         for (int k = 0; k < D; k++)
            sum += H2H1T[i * D + k] * invH1H1T[k * D + j];
         A_aug[i * D + j] = sum;
      }
   }

   // Stability (spectral radius) is a property of the linear part alone - the bias column just
   // translates the fixed point, it can't make the homogeneous part unstable or not.
   std::vector<double> v(rank, 1.0 / std::sqrt((double)rank));
   double rho = 1.0;
   for (int iter = 0; iter < 50; iter++)
   {
      std::vector<double> v_next(rank, 0.0);
      for (int r = 0; r < rank; r++)
         for (int c = 0; c < rank; c++)
            v_next[r] += A_aug[r * D + c] * v[c];
      double norm = 0.0;
      for (int r = 0; r < rank; r++)
         norm += v_next[r] * v_next[r];
      norm = std::sqrt(norm);
      if (norm < 1e-12)
         break;
      for (int r = 0; r < rank; r++)
         v[r] = v_next[r] / norm;
      rho = norm;
   }

   // Only the linear columns get the stability scale-down; the bias column is left alone so the
   // model still settles near the signal's real operating point rather than shrinking toward 0.
   const double scale = rho > 1.0 ? 1.0 / rho : 1.0;
   for (int i = 0; i < rank; i++)
   {
      for (int j = 0; j < rank; j++)
         res.A[i * D + j] = (float)(A_aug[i * D + j] * scale);
      res.A[i * D + rank] = (float)A_aug[i * D + rank];
   }
   res.spectralRadius = (float)(rho * scale);

   // Residual std of the (scaled) one-step fit, per output dimension - the honest measure of
   // how much the training data actually wandered around what the affine map predicts. Free-run
   // adds noise at this scale so a finished model keeps generating a sequence instead of
   // decaying onto a single, silent fixed point once Learn stops.
   std::vector<double> sqErr(rank, 0.0);
   std::vector<double> x1raw(rank);
   for (size_t t = 0; t + 1 < N; t++)
   {
      for (int i = 0; i < rank; i++)
      {
         const auto* ri = FindRuntime(validKeys[i]);
         x1raw[i] = ri ? ri->posHistory[t] : 0.0;
      }
      for (int i = 0; i < rank; i++)
      {
         const auto* ri = FindRuntime(validKeys[i]);
         double x2_i = ri ? ri->posHistory[t + 1] : 0.0;
         double pred = res.A[i * D + rank];
         for (int j = 0; j < rank; j++)
            pred += res.A[i * D + j] * x1raw[j];
         const double e = x2_i - pred;
         sqErr[i] += e * e;
      }
   }
   for (int i = 0; i < rank; i++)
      res.noiseStd[i] = (float)std::sqrt(sqErr[i] / (double)std::max((size_t)1, N - 1));

   res.valid = true;
   return res;
}

namespace
{
   // xorshift64* - local to this Step(), only needs to keep a free-run playhead wandering, not
   // match any other RNG's statistics.
   uint64_t DMDNextRaw(uint64_t& s)
   {
      s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
      return s * 0x2545F4914F6CDD1Dull;
   }
   float DMDGauss(uint64_t& s)
   {
      const float u1 = ((float)(DMDNextRaw(s) >> 40) + 0.5f) * (1.0f / 16777216.0f);
      const float u2 = ((float)(DMDNextRaw(s) >> 40) + 0.5f) * (1.0f / 16777216.0f);
      return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
   }
}

void DMDFit::Step(std::vector<float>& x, uint64_t* rngState) const
{
   const int D = rank + 1;
   if (!valid || (int)x.size() < rank || (int)A.size() < rank * D)
      return;
   std::vector<float> x_next(rank, 0.0f);
   for (int i = 0; i < rank; i++)
   {
      x_next[i] = A[i * D + rank]; // bias/equilibrium term
      for (int j = 0; j < rank; j++)
         x_next[i] += A[i * D + j] * x[j];
      if (rngState != nullptr && i < (int)noiseStd.size() && noiseStd[i] > 0.0f)
         x_next[i] += DMDGauss(*rngState) * noiseStd[i];
      x_next[i] = std::clamp(x_next[i], 0.0f, 1.0f);
   }
   for (int i = 0; i < rank; i++)
      x[i] = x_next[i];
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
   uint16_t probeVersion = 0;
   if (!OpenBlob(bytes.data(), bytes.size(), r, probeVersion))
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

   // 12. Source pools and the user profile.
   {
      // 12a. Classification: transit vs settled inside a hand move, corrections always deliberate.
      STATS_CHECK(ClassifyPool(Source::Hand, 0, true) == kPoolExploratory, "moving hand is not exploratory");
      STATS_CHECK(ClassifyPool(Source::Hand, 0, false) == kPoolDeliberate, "settled hand is not deliberate");
      STATS_CHECK(ClassifyPool(Source::Perf, MovementLog::kCorrection, true) == kPoolDeliberate,
                  "correction is not deliberate");
      STATS_CHECK(ClassifyPool(Source::Modulator, 0, true) == kPoolAuto, "modulator is not auto");
      STATS_CHECK(ClassifyPool(Source::Expression, 0, true) == kPoolAuto, "expression is not auto");
      STATS_CHECK(ClassifyPool(Source::Gesture, 0, true) == kPoolReplay, "gesture is not replay");
      STATS_CHECK(PoolIsHuman(kPoolDeliberate) && PoolIsHuman(kPoolExploratory), "human pools");
      STATS_CHECK(!PoolIsHuman(kPoolAuto) && !PoolIsHuman(kPoolReplay), "machine pools");

      // 12b. The skew this whole split exists to kill: a short hand performance against an
      // overnight LFO. The modulator logs ~960x more samples, so under one summed pool it
      // authored 99% of the landscape. The hand must still win the mix.
      const KeyId pk{9100, 0};
      Engine e;
      e.RegisterKey(pk, "Test", "cutoff", true);
      e.BeginSession();
      e.SetPlaying(true);
      double t = 0;
      // 30 s of hand, parked at 0.25 (the settled position is what the landscape should keep).
      for (int i = 0; i < 300; i++)
      {
         e.Observe(pk, t, 0.25f, Source::Hand, 0);
         t += kGridDt;
         e.Advance(t);
      }
      const double handN = e.Find(pk)->nEff;
      // 8 hours of a sine LFO sweeping the full range: the arcsine law, piling mass on both rails.
      // A hand sample every 10 minutes keeps the presence gate open, so this test measures the
      // *share* mechanism on its own - the gate gets its own test below. This is also the harder
      // and more realistic case: someone in the room all evening with a patch running.
      const int kEightHoursTicks = 8 * 3600 * 10;
      for (int i = 0; i < kEightHoursTicks; i++)
      {
         if (i % 6000 == 0)
         {
            e.Observe(pk, t, 0.25f, Source::Hand, 0);
            t += kGridDt;
            e.Advance(t);
            continue;
         }
         const float x = 0.5f + 0.5f * (float)std::sin(t * 0.7);
         e.Observe(pk, t, std::clamp(x, 0.0f, 1.0f), Source::Modulator, 0);
         t += kGridDt;
         e.Advance(t);
      }
      PoolShares sh;
      STATS_CHECK(e.GetPoolShares(pk, sh) && sh.valid, "no pool shares after a mixed session");
      const double human = sh.share[kPoolDeliberate] + sh.share[kPoolExploratory];
      const double autoN = sh.n[kPoolAuto];
      std::printf("[POOLS] hand n=%.0f auto n=%.0f -> human share %.3f auto share %.3f\n",
                  handN, autoN, human, sh.share[kPoolAuto]);
      STATS_CHECK(autoN > 10.0 * handN, "the overnight run did not actually out-sample the hand");
      STATS_CHECK(human >= 0.5, "8 h of LFO outvoted 30 s of hand: share %.3f", human);

      // The blended landscape must still point at where the hand parked, not at the rails the
      // sine spent most of its time on.
      Blend b;
      e.ComputeBlend(pk, 0.25f, b);
      int peak = 0;
      for (int i = 1; i < kBins; i++)
         if (b.p[i] > b.p[peak])
            peak = i;
      STATS_CHECK(std::abs(peak - PosBin(0.25f)) <= 4, "landscape peak moved to bin %d, hand parked at %d",
                  peak, PosBin(0.25f));

      // 12c. Cadence is human-only: an LFO's rate must never become "your speed".
      const ParamStats* hp = e.Find(pk);
      STATS_CHECK(hp != nullptr && hp->nEff > 0.0, "key lost its legacy stats");

      // 12d. Round trip, including the pools and their scores.
      std::string lc;
      const std::vector<uint8_t> blob = e.Serialize("x");
      Engine e2;
      STATS_CHECK(e2.Deserialize(blob.data(), blob.size(), lc), "pooled stats.bin did not load");
      PoolShares sh2;
      STATS_CHECK(e2.GetPoolShares(pk, sh2), "pools lost across save/load");
      for (int i = 0; i < kNumPools; i++)
         STATS_CHECK(std::abs(sh2.n[i] - sh.n[i]) < 1e-6, "pool %d n changed across save/load", i);
   }

   // 13. Presence gate: a machine left running with no human in the room stops accumulating.
   {
      const KeyId pk{9200, 0};
      Engine e;
      e.RegisterKey(pk, "Test", "cutoff", true);
      e.BeginSession();
      e.SetPlaying(true);
      double t = 0;
      e.Observe(pk, t, 0.5f, Source::Hand, 0);
      t += kGridDt;
      e.Advance(t);
      // Well past kPresenceWindowSec of transport-only time.
      const int ticks = (int)((kPresenceWindowSec * 3.0) / kGridDt);
      for (int i = 0; i < ticks; i++)
      {
         e.Observe(pk, t, 0.9f, Source::Modulator, 0);
         t += kGridDt;
         e.Advance(t);
      }
      PoolShares sh;
      STATS_CHECK(e.GetPoolShares(pk, sh), "no shares for the presence key");
      const double cap = (kPresenceWindowSec / kGridDt) * 0.1 * 1.05;
      std::printf("[POOLS] presence gate: auto n=%.0f (cap %.0f of %d ticks)\n", sh.n[kPoolAuto], cap, ticks);
      STATS_CHECK(sh.n[kPoolAuto] <= cap, "machine kept accumulating with nobody present: n=%.1f", sh.n[kPoolAuto]);
   }

   // 14. The invariant this whole pooled design exists to hold: what the model learns is never
   // a function of how long a machine was left running. The presence gate enforces that on a
   // key's own pools; it has to enforce it at the profile/role/family rungs too, which are
   // what a young key actually falls back to. So: run the identical fixture for 1.5 and for 8
   // presence windows of unattended automation, and read an untouched sibling of the same
   // role. The two must agree. Before the gate reached the aggregate tables they diverged,
   // because the longer run kept flooding the family table after everyone had gone home.
   {
      const KeyId a{9300, 0}, b{9301, 0};
      auto run = [&](double windows, Blend& out) {
         Engine e;
         e.RegisterKey(a, "Test", "cutoff", true);
         e.RegisterKey(b, "Test", "cutoff", true);
         e.BeginSession();
         e.SetPlaying(true);
         double t = 0;
         e.Observe(a, t, 0.5f, Source::Hand, 0);   // one human touch, then the room empties
         t += kGridDt;
         e.Advance(t);
         const int ticks = (int)((kPresenceWindowSec * windows) / kGridDt);
         for (int i = 0; i < ticks; i++)
         {
            e.Observe(a, t, 0.9f, Source::Modulator, 0);
            t += kGridDt;
            e.Advance(t);
         }
         e.ComputeBlend(b, 0.5f, out);
      };
      Blend shortRun, longRun;
      run(1.5, shortRun);
      run(8.0, longRun);
      double l1 = 0.0;
      for (int i = 0; i < kBins; i++)
         l1 += std::abs(shortRun.p[i] - longRun.p[i]);
      const double dw = std::abs(shortRun.w2b - longRun.w2b) + std::abs(shortRun.w2d - longRun.w2d) +
                        std::abs(shortRun.w2 - longRun.w2);
      std::printf("[POOLS] ladder gate: 1.5 vs 8 windows unattended -> landscape L1=%.4f rung dw=%.4f\n", l1, dw);
      STATS_CHECK(l1 < 0.02, "unattended runtime changed what a sibling learned: L1=%.4f", l1);
      STATS_CHECK(dw < 0.02, "unattended runtime changed the ladder weights: dw=%.4f", dw);
   }

   std::filesystem::remove_all(testDir, ec);
   std::printf("[MOVEMENT STATS TEST] PASS\n");
   std::printf("INFINITE_MOVESTATSTEST: OK\n");
   return true;
}

} // namespace MovementStats
