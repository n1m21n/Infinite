#include "ModulatorNodes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "Transport.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "platform/Platform.h"

constexpr int PatternNode::kSteps;

namespace
{
   const std::vector<std::string> kLfoShapes = {
      "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold"
   };

   // Shared with VoiceMod's per-voice kernel - see core/ModShapes.h.
   using ModShapes::Frac;
   using ModShapes::Hash01;
   using ModShapes::HashSeeded;
   using ModShapes::Mix64;
   using ModShapes::SeedSalt;

   double PeriodBeatsFor(int rateMode, float rateBeats, SyncedPeriodCache& cache)
   {
      // Synced: a rate on a MusicTime division runs at the division's exact
      // period (a float triplet is ~3e-8 off it). A rate off the menu (a cable
      // on the hidden rate slot, a Field kernel's set(osc, "rateBeats", t))
      // stays continuous, so modulating rate never steps.
      if (rateMode == LFONode::kRateSynced)
      {
         // Only the bar divisions follow the time signature, so only they
         // read it per call.
         const Transport& tr = Transport::Instance();
         if (cache.Hit(rateBeats, tr))
            return cache.period;
         const MusicTime::RateDivision d = MusicTime::NearestRateDivision((double)rateBeats);
         const double div = MusicTime::BeatsFor(d);
         const bool onDivision = std::fabs(div - (double)rateBeats) <= 1.0e-5 * div;
         const bool barBased = d == MusicTime::k4Bars || d == MusicTime::k2Bars || d == MusicTime::k1Bar;
         cache.rate = rateBeats;
         cache.sigNum = onDivision && barBased ? tr.TimeSigNumerator() : -1;
         cache.sigDen = onDivision && barBased ? tr.TimeSigDenominator() : -1;
         cache.period = onDivision ? div : (double)std::max(0.01f, rateBeats);
         return cache.period;
      }
      return std::max(0.01f, rateBeats);
   }
   double RatePosition(int rateMode, float rateBeats, float rateHz, SyncedPeriodCache& cache)
   {
      Transport& tr = Transport::Instance();
      if (rateMode == LFONode::kRateHz)
         return tr.Seconds() * (double)std::max(0.001f, rateHz);
      return tr.Beats() / PeriodBeatsFor(rateMode, rateBeats, cache);
   }

   using ModShapes::Remap;
}

// ---------------------------------------------------------------- LFO

INode* LFONode::Create()
{
   auto* node = new LFONode();
   node->seed = NextSeed();
   return node;
}

const std::vector<std::string>& LFONode::ShapeNames()
{
   return kLfoShapes;
}

const std::vector<std::string>& LFONode::RateModeNames()
{
   static const std::vector<std::string> kNames = { "beats", "synced", "Hz" };
   return kNames;
}

const std::vector<std::string>& LFONode::PhaseModeNames()
{
   static const std::vector<std::string> kNames = { "transport", "free", "retrigger" };
   return kNames;
}

double LFONode::PeriodBeats() const
{
   return PeriodBeatsFor(rateMode, rateBeats, mPeriodCache);
}

// Free and Retrigger: cycles accumulated over FreeSeconds, at the current
// rate, so a tempo or rate change bends the speed without jumping the phase.
// Memoised on the clock like every stateful modulator. A render starts from
// phase 0 so it repeats; a retrigger edge is placed where the trig input
// crosses 0.5 on the straight line between calls.
double LFONode::FreeCycles()
{
   Transport& tr = Transport::Instance();
   const double now = tr.FreeSeconds();
   if (now == mFreeLast)
      return mFreeCycles;
   const bool offline = tr.IsOfflineMode();
   double dt = mFreeLast < 0.0 ? 0.0 : now - mFreeLast;
   if (offline != mFreeOffline)
   {
      mFreeOffline = offline;
      mFreeCycles = 0.0;
      dt = 0.0;
   }
   if (dt < 0.0 || dt > 1.0)
      dt = 0.0; // the clock changed domain (engine restart): hold, don't jump
   mFreeLast = now;

   const double perSecond = rateMode == kRateHz ? (double)std::max(0.001f, rateHz)
                                                : (tr.Tempo() / 60.0) / PeriodBeats();
   const float trig = trigger != nullptr ? trigger->Value01() : 0.0f;
   if (phaseMode == kPhaseRetrigger && trig >= 0.5f && mTrigLast < 0.5f)
   {
      const float f = trig > mTrigLast ? std::clamp((0.5f - mTrigLast) / (trig - mTrigLast), 0.0f, 1.0f) : 1.0f;
      mFreeCycles = (1.0 - f) * dt * perSecond;
   }
   else
      mFreeCycles += dt * perSecond;
   mTrigLast = trig;
   return mFreeCycles;
}

float LFONode::Value01()
{
   double cycles;
   if (phaseMode == kPhaseFree || phaseMode == kPhaseRetrigger)
      cycles = FreeCycles();
   else if (rateMode == kRateBeats)
   {
      // Exactly the expression saved patches were made with.
      const double beats = Transport::Instance().Beats();
      const float rate = std::max(0.01f, rateBeats);
      cycles = beats / rate;
   }
   else
   {
      const Transport& tr = Transport::Instance();
      cycles = rateMode == kRateSynced && mPeriodCache.Hit(rateBeats, tr) ? tr.Beats() / mPeriodCache.period // memo hit
                                                                          : RatePosition(rateMode, rateBeats, rateHz, mPeriodCache);
   }
   const float raw = ModShapes::Lfo(shape, cycles + phase, maths, maths >= 1 ? SeedSalt(seed, 1) : 0);
   return Remap(raw, low, high);
}

bool LFONode::VoiceSpec(VoiceMod::Spec& out)
{
   if (!voice)
      return false;
   out = VoiceMod::Spec();
   out.kind = VoiceMod::kLfo;
   out.hz = rateMode == kRateHz;
   out.rateHz = rateHz;
   out.periodBeats = PeriodBeats();
   out.low = low;
   out.high = high;
   out.salt = SeedSalt(seed, 1);
   out.shape = shape;
   out.phase = phase;
   return true;
}

// ---------------------------------------------------------------- Random

float RandomNode::ValueForStep(long long step, uint64_t salt) const
{
   if (maths >= 1)
      return HashSeeded(step, salt);
   // The seed is mixed into the hash input rather than added to the result, so
   // two seeds give genuinely different sequences instead of the same sequence
   // offset by a constant.
   const long long legacySalt = (long long)(seed * 1013.0f);
   // Unsigned so the mix wraps by definition; the signed form overflowed (UB) for large seeds.
   return Hash01((long long)((unsigned long long)step * 6364136223ULL + (unsigned long long)legacySalt * 1442695040888963407ULL));
}

float RandomNode::Value01()
{
   double pos;
   if (rateMode == LFONode::kRateBeats)
   {
      const double beats = Transport::Instance().Beats();
      const float rate = std::max(0.01f, rateBeats);
      pos = beats / rate;
   }
   else
   {
      const Transport& tr = Transport::Instance();
      pos = rateMode == LFONode::kRateSynced && mPeriodCache.Hit(rateBeats, tr) ? tr.Beats() / mPeriodCache.period // memo hit
                                                                                : RatePosition(rateMode, rateBeats, rateHz, mPeriodCache);
   }
   const long long step = (long long)std::floor(pos);
   const float t = Frac(pos);

   const uint64_t salt = maths >= 1 ? SeedSalt(seed, 2) : 0;
   const float current = ValueForStep(step, salt);

   const float raw = ModShapes::RandomGlide(current, smooth > 0.0f ? ValueForStep(step + 1, salt) : current, t, smooth);

   return Remap(raw, low, high);
}

bool RandomNode::VoiceSpec(VoiceMod::Spec& out)
{
   if (!voice)
      return false;
   out = VoiceMod::Spec();
   out.kind = VoiceMod::kRandom;
   out.hz = rateMode == LFONode::kRateHz;
   out.rateHz = rateHz;
   out.periodBeats = PeriodBeatsFor(rateMode, rateBeats, mPeriodCache);
   out.low = low;
   out.high = high;
   out.salt = SeedSalt(seed, 2);
   out.smooth = smooth;
   return true;
}

// ---------------------------------------------------------------- Pattern

int PatternNode::EffectiveLength() const
{
   return std::clamp(length, 1, kSteps);
}

float PatternNode::Value01()
{
   const int count = EffectiveLength();
   const double beats = Transport::Instance().Beats();
   const float rate = std::max(0.01f, stepBeats);
   const double pos = beats / rate;
   const long long stepIndex = (long long)std::floor(pos);

   const int i = (int)(((stepIndex % count) + count) % count);
   mCurrentStep = i;

   float raw = steps[i];
   if (smoothSteps)
   {
      const int nextI = (i + 1) % count;
      const float t = Frac(pos);
      const float eased = t * t * (3.0f - 2.0f * t);
      raw = steps[i] + (steps[nextI] - steps[i]) * eased;
   }

   return Remap(raw, low, high);
}

// ---------------------------------------------------------------- Math

namespace
{
   const std::vector<std::string> kMathOps = {
      "A + B", "A - B", "A * B", "A / B", "min(A,B)", "max(A,B)",
      "average", "difference", "A only", "B only"
   };
}

const std::vector<std::string>& MathNode::OpNames()
{
   return kMathOps;
}

float MathNode::Value01()
{
   const float a = inputA ? inputA->Value01() : constantA;
   const float b = inputB ? inputB->Value01() : constantB;

   float r;
   switch (op)
   {
      case 0: r = a + b; break;
      case 1: r = a - b; break;
      case 2: r = a * b; break;
      case 3: r = a / std::max(b, 1e-4f); break;
      case 4: r = std::min(a, b); break;
      case 5: r = std::max(a, b); break;
      case 6: r = (a + b) * 0.5f; break;
      case 7: r = std::fabs(a - b); break;
      case 8: r = a; break;
      default: r = b; break;
   }

   r = r * gain + offset;
   return clampOutput ? std::min(1.0f, std::max(0.0f, r)) : r;
}

// ---------------------------------------------------------------- Compare

namespace
{
   const std::vector<std::string> kCompareOps = {
      "A > B", "A >= B", "A < B", "A <= B", "A == B", "A != B"
   };
}

const std::vector<std::string>& CompareNode::OpNames()
{
   return kCompareOps;
}

float CompareNode::Value01()
{
   const float a = inputA ? inputA->Value01() : constantA;
   const float b = inputB ? inputB->Value01() : constantB;
   const float tol = std::max(0.0f, tolerance);

   bool result;
   switch (op)
   {
      case 0: result = a > b; break;
      case 1: result = a >= b - tol; break;
      case 2: result = a < b; break;
      case 3: result = a <= b + tol; break;
      case 4: result = std::fabs(a - b) <= tol; break;
      default: result = std::fabs(a - b) > tol; break; // A != B
   }

   return result ? 1.0f : 0.0f;
}

// ---------------------------------------------------------------- Range to Range

float RangeToRangeNode::Value01()
{
   if (bypassed)
      return input ? input->Value01() : constantIn;
   const float v = input ? input->Value01() : constantIn;
   const float span = inHigh - inLow;
   const float t = std::fabs(span) > 1e-6f ? (v - inLow) / span : 0.0f;
   const float r = outLow + (outHigh - outLow) * t;
   if (!clampOutput) return r;
   return std::min(std::max(outLow, outHigh), std::max(std::min(outLow, outHigh), r));
}

// ---------------------------------------------------------------- Smooth

double SmoothNode::AmountTau(float amount)
{
   const double k = std::clamp((double)amount, 0.0, 0.99);
   return k <= 0.0 ? 0.0 : (1.0 / 60.0) / -std::log(k);
}

// maths 1: every mode steps by transport seconds and treats the input as a
// straight line from the previous call's value, so the curve is the same
// at any frame rate (Modulation::SmoothStep / SpringStep, both exact).
float SmoothNode::ValueExact(float target)
{
   const double now = Transport::Instance().Seconds();
   if (mInit && now == mLastSeconds)
      return mLast; // fan-out: already advanced for this instant
   const double dt = now - mLastSeconds;
   mLastSeconds = now;
   if (!mInit || dt <= 0.0 || dt > 1.0)
   {
      // First call, a seek or a long gap: snap rather than glide across it.
      mInit = true;
      mLast = mPrevIn = target;
      mVel = 0.0f;
      return mLast;
   }

   if (mode == kSpring)
   {
      double y = mLast, v = mVel;
      Modulation::SpringStep(y, v, mPrevIn, target, dt, 6.283185307179586 * std::max(0.05f, frequency),
                             std::max(0.0f, damping));
      mLast = (float)y;
      mVel = (float)v;
      if (!std::isfinite(mLast) || !std::isfinite(mVel))
      {
         mLast = target;
         mVel = 0.0f;
      }
   }
   else
   {
      if (mode != kTime && amount != mTauAmount)
      {
         mTauAmount = amount; // the log runs when amount moves, not per call
         mTau = AmountTau(amount);
      }
      const double tau = mode == kTime ? (double)std::max(0.001f, timeConstant) : mTau;
      mLast = tau > 0.0 ? (float)Modulation::SmoothStep(mLast, mPrevIn, target, dt, tau) : target;
   }
   mPrevIn = target;
   return mLast;
}

float SmoothNode::Value01()
{
   if (bypassed)
      return input ? input->Value01() : constantIn;
   const float target = input ? input->Value01() : constantIn;
   if (maths >= 1)
      return ValueExact(target);
   if (mode == kPerFrame)
   {
      const double beats = Transport::Instance().Beats();
      if (mInit && beats == mLastBeats)
         return mLast; // already advanced this tick - don't double-apply the filter

      const float k = std::min(1.0f, std::max(0.0f, amount));
      mLast = !mInit ? target : mLast + (target - mLast) * (1.0f - k);
      mInit = true;
      mLastBeats = beats;
      return mLast;
   }

   const double now = Transport::Instance().Seconds();
   if (mInit && now == mLastSeconds)
      return mLast; // fan-out: already advanced for this instant

   const double dt = now - mLastSeconds;
   mLastSeconds = now;
   // First call, a seek backwards or a long gap: snap rather than glide across it.
   if (!mInit || dt < 0.0 || dt > 1.0)
   {
      mInit = true;
      mLast = target;
      mVel = 0.0f;
      return mLast;
   }

   if (mode == kTime)
   {
      const float tc = std::max(0.001f, timeConstant);
      mLast += (target - mLast) * (1.0f - std::exp(-(float)dt / tc));
      return mLast;
   }

   // Spring: x'' = w^2 (target - x) - 2 z w v, semi-implicit Euler in substeps
   // small enough to stay stable at any stiffness the knob allows.
   const float w = 6.2831853f * std::max(0.05f, frequency);
   const float z = std::max(0.0f, damping);
   // dt is capped (a hitch just takes a slower glide) and the step is bounded by
   // both stiffness and damping, so h*w*(1+z) stays well under 1 at every knob value.
   const float sdt = (float)std::min(dt, 0.1);
   const int steps = std::min(256, std::max(1, (int)std::ceil(sdt * w * (1.0f + z) * 4.0f)));
   const float h = sdt / (float)steps;
   for (int i = 0; i < steps; ++i)
   {
      mVel += (w * w * (target - mLast) - 2.0f * z * w * mVel) * h;
      mLast += mVel * h;
   }
   if (!std::isfinite(mLast) || !std::isfinite(mVel))
   {
      mLast = target;
      mVel = 0.0f;
   }
   return mLast;
}

// ---------------------------------------------------------------- CV Recorder

namespace
{
   const char* kHex = "0123456789abcdef";
   int HexVal(char c)
   {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
   }
   unsigned char Quantize(float v) { return (unsigned char)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); }
}

void CVRecorderNode::VisitParams(ParamVisitor& v)
{
   v.Float("constantIn", constantIn);
   v.Float("speed", speed);
   v.Float("low", low);
   v.Float("high", high);
   v.Bool("playing", playing);

   std::string enc;
   enc.reserve(mData.size() * 2);
   for (unsigned char b : mData)
   {
      enc.push_back(kHex[b >> 4]);
      enc.push_back(kHex[b & 15]);
   }
   const std::string before = enc;
   v.Text("data", enc);
   if (enc != before && !mRecording) // a load / undo restore replaced the take
   {
      mData.clear();
      for (size_t i = 0; i + 1 < enc.size() && (int)mData.size() < kMaxSamples; i += 2)
      {
         const int hi = HexVal(enc[i]), lo = HexVal(enc[i + 1]);
         if (hi < 0 || lo < 0) break;
         mData.push_back((unsigned char)(hi * 16 + lo));
      }
      mAnchorSet = false;
   }
}

void CVRecorderNode::StartRecording()
{
   mRecording = true;
   mData.clear();
   mRecStart = Transport::Instance().Beats();
}

void CVRecorderNode::StopRecording()
{
   if (!mRecording)
      return;
   mRecording = false;
   playing = !mData.empty();
   mAnchorSet = false; // playback restarts from the top of the take
   mLastBeats = -1.0;
   mPhase = 0.0;
}

void CVRecorderNode::StartPlayback()
{
   playing = true;
   mAnchorSet = false;
   mLastBeats = -1.0;
   mPhase = 0.0;
}

void CVRecorderNode::StopPlayback()
{
   playing = false;
}

void CVRecorderNode::Clear()
{
   mRecording = false;
   playing = false;
   mData.clear();
}

float CVRecorderNode::Sample(double pos) const
{
   const int n = (int)mData.size();
   if (n == 0)
      return 0.0f;
   if (pos < 0.0)
      pos = 0.0;
   int i0 = (int)pos;
   const float f = (float)(pos - i0);
   i0 %= n;
   const int i1 = (i0 + 1) % n;
   return (mData[i0] + (mData[i1] - mData[i0]) * f) * (1.0f / 255.0f);
}

float CVRecorderNode::Value01()
{
   const float in = std::clamp(input ? input->Value01() : constantIn, 0.0f, 1.0f);
   if (bypassed)
      return in;
   const double beats = Transport::Instance().Beats();

   if (mRecording)
   {
      double rel = beats - mRecStart;
      if (rel < 0.0) // transport rewound: start the take over from here
      {
         mRecStart = beats;
         mData.clear();
         rel = 0.0;
      }
      const int idx = (int)(rel * kSamplesPerBeat);
      if (idx >= kMaxSamples)
      {
         StopRecording(); // take is full; fall through to playback
      }
      else
      {
         // Fill any skipped slots with the previous value so a stalled frame
         // leaves a hold, not a gap; the current slot tracks the latest input.
         const unsigned char q = Quantize(in);
         while ((int)mData.size() <= idx)
            mData.push_back(mData.empty() ? q : mData.back());
         mData[idx] = q;
         mLastOut = in;
         return in;
      }
   }

   if (!mPlaying())
      return mData.empty() ? in : mLastOut;
   if (beats == mLastBeats)
      return mLastOut; // already resolved this tick

   // Integrate the playhead instead of deriving it from (beats - anchor) *
   // speed: that form jumps the loop position every time `speed` is dragged.
   // A rewind or first tick only re-seeds mLastBeats, keeping the phase.
   if (!mAnchorSet || mLastBeats < 0.0 || beats < mLastBeats)
   {
      mAnchorSet = true;
      if (mLastBeats < 0.0)
         mPhase = 0.0; // fresh pass starts from the top of the take
   }
   else
      mPhase += (beats - mLastBeats) * std::max(0.0f, speed) * kSamplesPerBeat;
   mLastBeats = beats;

   const double n = (double)mData.size();
   mPhase = std::fmod(mPhase, n);
   const double pos = mPhase;
   mPlayNorm = (float)(pos / n);
   mLastOut = std::clamp(low + (high - low) * Sample(pos), 0.0f, 1.0f);
   return mLastOut;
}

// ---------------------------------------------------------------- Mod Depth

float ModDepthNode::Value01()
{
   if (bypassed)
      return input ? input->Value01() : constantIn;
   const float v = input ? input->Value01() : constantIn;
   return std::clamp(0.5f + (v - 0.5f) * depth, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- Envelope

ModShapes::Ahdsr EnvelopeNode::Shape() const
{
   ModShapes::Ahdsr e;
   e.attackMs = attackMs;
   e.holdMs = holdMs;
   e.decayMs = decayMs;
   e.sustain = sustainLevel;
   e.releaseMs = releaseMs;
   e.attackCurve = attackCurve;
   e.decayCurve = decayCurve;
   e.releaseCurve = releaseCurve;
   return e;
}

// The level at `seconds`; ModShapes::AhdsrLevel walks the stage edges at
// their exact times. A time before the stage start reads as the start.
float EnvelopeNode::LevelAt(double seconds)
{
   return ModShapes::AhdsrLevel(Shape(), mStage, mStageStartSeconds, mStageStartLevel, seconds);
}

bool EnvelopeNode::VoiceSpec(VoiceMod::Spec& out)
{
   if (!voice || bypassed || output != kOutAdsr)
      return false;
   out = VoiceMod::Spec();
   out.kind = VoiceMod::kEnvelope;
   out.hz = true; // the kernel's position then counts seconds since note-on
   out.rateHz = 1.0f;
   out.env = Shape();
   return true;
}

// maths 1: the gate edge sits where the input, as a straight line from the
// previous call, crosses the threshold, and the stage edges after it at
// their exact times, so stage lengths match the knobs at any frame rate.
float EnvelopeNode::LevelExact(double seconds, float in01)
{
   const bool above = in01 >= threshold;
   if (above != mGateOpen)
   {
      double edge = seconds;
      if (mPrevSeconds >= 0.0 && seconds > mPrevSeconds && in01 != mPrevIn)
      {
         const double f = std::clamp((double)(threshold - mPrevIn) / (double)(in01 - mPrevIn), 0.0, 1.0);
         edge = mPrevSeconds + f * (seconds - mPrevSeconds);
      }
      const float levelAtEdge = mStageStartSeconds < 0.0 ? 0.0f : LevelAt(edge);
      mGateOpen = above;
      mStage = above ? ModShapes::kEnvAttack : ModShapes::kEnvRelease;
      mStageStartLevel = levelAtEdge;
      mStageStartSeconds = edge;
   }
   mPrevIn = in01;
   mPrevSeconds = seconds;
   mLevel = mStageStartSeconds < 0.0 ? 0.0f : LevelAt(seconds);
   return mLevel;
}

float EnvelopeNode::Value01()
{
   if (bypassed)
      return input ? input->Value01() : constantIn;
   const float in01 = input ? input->Value01() : constantIn;
   const double seconds = Transport::Instance().Seconds();

   if (maths >= 1)
   {
      if (seconds != mPrevSeconds)
         LevelExact(seconds, in01);
      return output == kOutAdsr ? std::clamp(mLevel, 0.0f, 1.0f)
                                : std::clamp(0.5f + (in01 - 0.5f) * mLevel, 0.0f, 1.0f);
   }

   const bool above = in01 >= threshold;
   if (above != mGateOpen)
   {
      mGateOpen = above;
      mStage = above ? ModShapes::kEnvAttack : ModShapes::kEnvRelease;
      mStageStartLevel = mLevel;
      mStageStartSeconds = seconds;
   }

   const double elapsedMs = mStageStartSeconds < 0.0 ? 0.0 : (seconds - mStageStartSeconds) * 1000.0;

   switch (mStage)
   {
      case ModShapes::kEnvIdle:
         mLevel = 0.0f;
         break;

      case ModShapes::kEnvAttack:
      {
         const float t = (float)(elapsedMs / std::max(0.001f, attackMs));
         if (t >= 1.0f)
         {
            mLevel = 1.0f;
            mStage = ModShapes::kEnvDecay;
            mStageStartLevel = 1.0f;
            mStageStartSeconds = seconds;
         }
         else
         {
            mLevel = mStageStartLevel + (1.0f - mStageStartLevel) * t;
         }
         break;
      }

      case ModShapes::kEnvDecay:
      {
         const float sustain = std::clamp(sustainLevel, 0.0f, 1.0f);
         const float t = (float)(elapsedMs / std::max(0.001f, decayMs));
         if (t >= 1.0f)
         {
            mLevel = sustain;
            mStage = ModShapes::kEnvSustain;
         }
         else
         {
            mLevel = 1.0f + (sustain - 1.0f) * t;
         }
         break;
      }

      case ModShapes::kEnvSustain:
         mLevel = std::clamp(sustainLevel, 0.0f, 1.0f);
         break;

      case ModShapes::kEnvRelease:
      {
         const float t = (float)(elapsedMs / std::max(0.001f, releaseMs));
         if (t >= 1.0f)
         {
            mLevel = 0.0f;
            mStage = ModShapes::kEnvIdle;
         }
         else
         {
            mLevel = mStageStartLevel * (1.0f - t);
         }
         break;
      }
   }

   if (output == kOutAdsr)
      return std::clamp(mLevel, 0.0f, 1.0f);
   return std::clamp(0.5f + (in01 - 0.5f) * mLevel, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- CV to Pitch

float CVToPitchNode::TargetFor(float v01, int* semitoneOut) const
{
   const int low = std::min(rangeLow, rangeHigh);
   const int high = std::max(rangeLow, rangeHigh);
   const int semitoneCount = std::max(1, high - low + 1);
   const int idx = std::clamp((int)std::lround(v01 * (float)(semitoneCount - 1)), 0, semitoneCount - 1);
   int semitone = low + idx;
   if (scale != MusicTime::kChromatic)
      semitone = MusicTime::SnapToScaleInRange(semitone, root, scale, low, high);
   if (semitoneOut != nullptr)
      *semitoneOut = semitone;
   return (float)(semitone - low) / (float)std::max(1, semitoneCount - 1);
}

float CVToPitchNode::Value01()
{
   if (bypassed)
      return input ? input->Value01() : constantIn;

   const float v01 = std::clamp(input ? input->Value01() : constantIn, 0.0f, 1.0f);

   if (maths >= 1)
   {
      int semitone = 0;
      const float target01 = TargetFor(v01, &semitone);
      mLastSemitone = semitone;
      const double seconds = Transport::Instance().Seconds();
      if (mGlided >= 0.0f && seconds == mLastSeconds)
         return mGlided; // already advanced this tick - don't double-apply the filter
      const double dt = (mGlided < 0.0f || mLastSeconds < 0.0) ? 0.0 : seconds - mLastSeconds;
      mLastSeconds = seconds;
      if (glideMs <= 0.0f || mGlided < 0.0f || dt <= 0.0 || dt > 1.0)
         mGlided = target01;
      else
      {
         // The input runs in a straight line from the last call's value; the
         // glide chases each semitone from the moment the line enters it.
         // Boundaries between input steps sit at (k + 0.5) / (count - 1).
         const double tau = glideMs * 0.001;
         const int count = std::max(1, std::abs(rangeHigh - rangeLow) + 1);
         const double x0 = mPrevIn < 0.0f ? v01 : mPrevIn, x1 = v01;
         double y = mGlided, from = 0.0;
         if (count > 1 && x1 != x0)
         {
            const double n = count - 1;
            const double dir = x1 > x0 ? 1.0 : -1.0;
            // First boundary past x0 in the direction of travel.
            double k = dir > 0 ? std::floor(x0 * n - 0.5) + 1.0 : std::ceil(x0 * n - 0.5) - 1.0;
            for (int guard = 0; guard < 256; guard++, k += dir)
            {
               const double b = (k + 0.5) / n;
               const double f = (b - x0) / (x1 - x0);
               if (!(f < 1.0))
                  break; // past this call's end of the line (or not finite)
               if (f <= from)
                  continue; // x0 sits on this boundary: already in the next step
               const float seg = TargetFor((float)(x0 + (x1 - x0) * (from + f) * 0.5), nullptr);
               y = seg + (y - seg) * std::exp(-(f - from) * dt / tau);
               from = f;
            }
         }
         // The last stretch chases the step the line runs through, which is
         // target01 unless the line ends exactly on a boundary.
         const float last = x1 != x0 ? TargetFor((float)(x0 + (x1 - x0) * (from + 1.0) * 0.5), nullptr) : target01;
         y = last + (y - last) * std::exp(-(1.0 - from) * dt / tau);
         mGlided = (float)y;
      }
      mPrevIn = v01;
      return std::clamp(mGlided, 0.0f, 1.0f);
   }

   const int low = std::min(rangeLow, rangeHigh);
   const int high = std::max(rangeLow, rangeHigh);
   const int semitoneCount = std::max(1, high - low + 1);
   const int idx = std::clamp((int)std::lround(v01 * (float)(semitoneCount - 1)), 0, semitoneCount - 1);
   int semitone = low + idx;

   if (scale != MusicTime::kChromatic)
      semitone = MusicTime::SnapToScaleInRange(semitone, root, scale, low, high);
   mLastSemitone = semitone;

   const int snappedIdx = semitone - low;
   const float target01 = (float)snappedIdx / (float)std::max(1, semitoneCount - 1);

   // Glide smooths the quantized output itself (portamento) - see the class
   // comment. Transport::Seconds() is real playing time, frozen while
   // paused, so glide simply holds rather than jumping while stopped.
   const double seconds = Transport::Instance().Seconds();
   if (mGlided >= 0.0f && seconds == mLastSeconds)
      return mGlided; // already advanced this tick - don't double-apply the filter

   const double dt = (mGlided < 0.0f || mLastSeconds < 0.0) ? 0.0 : std::max(0.0, seconds - mLastSeconds);
   mLastSeconds = seconds;

   if (glideMs <= 0.0f || mGlided < 0.0f)
      mGlided = target01;
   else
   {
      const float coef = std::exp(-(float)dt / (glideMs * 0.001f));
      mGlided = target01 + (mGlided - target01) * coef;
   }
   return std::clamp(mGlided, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- Invert

float InvertNode::Value01()
{
   if (bypassed)
      return input ? input->Value01() : constantIn;
   const float v = input ? input->Value01() : constantIn;
   return low + high - v;
}

// ---------------------------------------------------------------- Mod Curve

float ModCurveNode::Value01()
{
   if (bypassed)
      return input ? input->Value01() : constantIn;
   const float in = input ? input->Value01() : constantIn;
   const float x = std::clamp(in, 0.0f, 1.0f); // clamp for lookup only; the curve is only defined on 0..1
   const float y = curve.Evaluate(x);
   return in + (y - in) * mix;
}

// ---------------------------------------------------------------- MSEG

void MSEGNode::Reset()
{
   points = { { 0.0f, 0.0f, 0.0f }, { 0.1f, 1.0f, 0.6f }, { 0.5f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
}

int MSEGNode::AddPoint(float x, float y)
{
   if ((int)points.size() >= kMaxPoints)
      return -1;
   x = std::clamp(x, 0.0f, 1.0f);
   y = std::clamp(y, 0.0f, 1.0f);
   int at = 1;
   while (at < (int)points.size() - 1 && points[at].x < x)
      at++;
   points.insert(points.begin() + at, Point { x, y, 0.0f });
   return at;
}

void MSEGNode::MovePoint(int index, float x, float y)
{
   const int last = (int)points.size() - 1;
   if (index < 0 || index > last)
      return;
   Point& p = points[index];
   p.y = std::clamp(y, 0.0f, 1.0f);
   if (index == 0)
      p.x = 0.0f;
   else if (index == last)
      p.x = 1.0f;
   else
      p.x = std::clamp(x, points[index - 1].x, points[index + 1].x); // a point never passes its neighbours
}

void MSEGNode::RemovePoint(int index)
{
   if (index <= 0 || index >= (int)points.size() - 1)
      return;
   points.erase(points.begin() + index);
}

float MSEGNode::Segment(float y0, float y1, float curve, float t)
{
   return ModShapes::MsegSegment(y0, y1, curve, t);
}

float MSEGNode::Evaluate(float x) const
{
   return ModShapes::MsegEvaluate(points.data(), (int)points.size(), x);
}

std::string MSEGNode::Encode() const
{
   std::string out;
   for (size_t i = 0; i < points.size(); i++)
   {
      if (i > 0)
         out += ";";
      char buf[64];
      snprintf(buf, sizeof(buf), "%.6g,%.6g,%.6g", (double)points[i].x, (double)points[i].y, (double)points[i].curve);
      out += buf;
   }
   return out;
}

void MSEGNode::Decode(const std::string& s)
{
   std::vector<Point> decoded;
   size_t start = 0;
   while (start < s.size() && (int)decoded.size() < kMaxPoints)
   {
      const size_t sep = s.find(';', start);
      const std::string term = s.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
      const size_t c1 = term.find(',');
      const size_t c2 = c1 == std::string::npos ? std::string::npos : term.find(',', c1 + 1);
      if (c1 != std::string::npos)
      {
         Point p;
         p.x = std::clamp((float)atof(term.substr(0, c1).c_str()), 0.0f, 1.0f);
         p.y = std::clamp((float)atof(term.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1).c_str()), 0.0f, 1.0f);
         p.curve = c2 == std::string::npos ? 0.0f : std::clamp((float)atof(term.substr(c2 + 1).c_str()), -1.0f, 1.0f);
         decoded.push_back(p);
      }
      if (sep == std::string::npos)
         break;
      start = sep + 1;
   }
   if (decoded.size() < 2)
      return;
   std::stable_sort(decoded.begin(), decoded.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
   decoded.front().x = 0.0f;
   decoded.back().x = 1.0f;
   points = decoded;
}

float MSEGNode::PositionHeld(double elapsed, bool patched) const
{
   return ModShapes::MsegPositionHeld(elapsed, patched, loop, loopStart, loopEnd);
}

// trig patched: the run moves by the clock's step since the last call, at
// the current rate, so a tempo or rate change bends the speed without a jump
// and a paused transport holds it. The trig edge sits where the input, as a
// straight line from the previous call, crosses 0.5.
float MSEGNode::Advance()
{
   Transport& tr = Transport::Instance();
   const bool hz = rateMode == LFONode::kRateHz;
   const double clock = hz ? tr.Seconds() : tr.Beats();
   if (clock == mClockLast)
      return (float)mX;
   double step = mClockLast < 0.0 ? 0.0 : clock - mClockLast;
   if (step < 0.0)
      step = 0.0; // seek or loop wrap: hold, don't run backwards
   mClockLast = clock;
   const double dx = hz ? step * (double)std::max(0.001f, rateHz) : step / PeriodBeatsFor(rateMode, rateBeats, mPeriodCache);

   const float trig = trigger->Value01();
   const bool held = trig >= 0.5f;
   if (held && mTrigLast < 0.5f)
   {
      const float f = trig > mTrigLast ? std::clamp((0.5f - mTrigLast) / (trig - mTrigLast), 0.0f, 1.0f) : 1.0f;
      mX = PositionHeld((1.0 - (double)f) * dx, true);
      mReleased = false;
   }
   else
      mX = ModShapes::MsegStep(mX, dx, held, mReleased, loop, loopStart, loopEnd);
   mTrigLast = trig;
   return (float)mX;
}

bool MSEGNode::VoiceSpec(VoiceMod::Spec& out)
{
   if (!voice)
      return false;
   out = VoiceMod::Spec();
   out.kind = VoiceMod::kMseg;
   out.hz = rateMode == LFONode::kRateHz;
   out.rateHz = rateHz;
   out.periodBeats = PeriodBeatsFor(rateMode, rateBeats, mPeriodCache);
   out.loop = loop;
   out.loopStart = loopStart;
   out.loopEnd = loopEnd;
   out.numPoints = std::min((int)points.size(), VoiceMod::kMaxPoints);
   for (int i = 0; i < out.numPoints; i++)
      out.points[i] = points[i];
   return true;
}

// ---------------------------------------------------------------- Voice Spread

const std::vector<std::string>& VoiceSpreadNode::ModeNames()
{
   static const std::vector<std::string> kNames = { "linear", "golden ratio", "primes", "alternate" };
   return kNames;
}

float VoiceSpreadNode::Value01()
{
   return Remap(ModShapes::Spread(mode, 0, count), low, high);
}

bool VoiceSpreadNode::VoiceSpec(VoiceMod::Spec& out)
{
   out = VoiceMod::Spec();
   out.kind = VoiceMod::kSpread;
   out.shape = mode;
   out.count = std::clamp(count, 1, ModShapes::kSpreadMaxCount);
   out.low = low;
   out.high = high;
   return true;
}

// ------------------------------------------------------------- Note Expression

const std::vector<std::string>& NoteExpressionNode::SourceNames()
{
   static const std::vector<std::string> kNames = { "pressure", "slide" };
   return kNames;
}

float NoteExpressionNode::Value01()
{
   const int ch = Platform::MidiLastExpressionChannel().load(std::memory_order_relaxed);
   const float v = source == VoiceMod::kExprSlide ? Platform::MidiChannelSlide(ch) : Platform::MidiChannelPressure(ch);
   return Remap(v, low, high);
}

bool NoteExpressionNode::VoiceSpec(VoiceMod::Spec& out)
{
   out = VoiceMod::Spec();
   out.kind = VoiceMod::kExpression;
   out.shape = std::clamp(source, 0, 1);
   out.hz = true;
   out.rateHz = kSmoothHz;
   out.low = low;
   out.high = high;
   return true;
}

float MSEGNode::Value01()
{
   float x;
   if (trigger != nullptr)
      x = Advance();
   else
   {
      mClockLast = -1.0; // a cable patched later starts from rest
      mX = 1.0;
      mReleased = true;
      mTrigLast = 0.0f;
      x = PositionHeld(RatePosition(rateMode, rateBeats, rateHz, mPeriodCache), false);
   }
   mShownX = x;
   return std::clamp(Evaluate(x), 0.0f, 1.0f);
}

// ---------------------------------------------------------------- Audio to CV
class AudioAudioToCVNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const AudioBuffer* inBuf = (numInputs > 0) ? inputs[0] : nullptr;

      const float gain = mGain.load(std::memory_order_relaxed);
      const float attackMs = std::max(0.1f, mAttackMs.load(std::memory_order_relaxed));
      const float releaseMs = std::max(1.0f, mReleaseMs.load(std::memory_order_relaxed));
      const int mode = mMode.load(std::memory_order_relaxed);
      const float minVal = mMinVal.load(std::memory_order_relaxed);
      const float maxVal = mMaxVal.load(std::memory_order_relaxed);

      const float attCoef = std::exp(-1.0f / (float)(mSampleRate * 0.001 * attackMs));
      const float relCoef = std::exp(-1.0f / (float)(mSampleRate * 0.001 * releaseMs));

      float env = mEnv.load(std::memory_order_relaxed);

      if (inBuf != nullptr && inBuf->numChannels > 0 && inBuf->channels != nullptr)
      {
         const int channels = inBuf->numChannels;
         for (int i = 0; i < numFrames; i++)
         {
            float instant = 0.0f;
            if (mode == 0) // Peak
            {
               for (int c = 0; c < channels; c++)
                  if (inBuf->channels[c] != nullptr)
                     instant = std::max(instant, std::fabs(inBuf->channels[c][i]));
            }
            else // RMS
            {
               float sumSq = 0.0f;
               int validCh = 0;
               for (int c = 0; c < channels; c++)
               {
                  if (inBuf->channels[c] != nullptr)
                  {
                     float s = inBuf->channels[c][i];
                     sumSq += s * s;
                     validCh++;
                  }
               }
               instant = validCh > 0 ? std::sqrt(sumSq / (float)validCh) : 0.0f;
            }
            instant *= gain;

            const float coef = (instant > env) ? attCoef : relCoef;
            env = instant + coef * (env - instant);
         }
      }
      else
      {
         for (int i = 0; i < numFrames; i++)
            env *= relCoef;
      }

      mEnv.store(env, std::memory_order_relaxed);

      float mapped = minVal + (maxVal - minVal) * std::clamp(env, 0.0f, 1.0f);
      mLevel.store(std::clamp(mapped, 0.0f, 1.0f), std::memory_order_relaxed);
   }

   void PushParams(const AudioToCVNode& n)
   {
      mGain.store(n.gain, std::memory_order_relaxed);
      mAttackMs.store(n.attackMs, std::memory_order_relaxed);
      mReleaseMs.store(n.releaseMs, std::memory_order_relaxed);
      mMode.store(n.mode, std::memory_order_relaxed);
      mMinVal.store(n.minVal, std::memory_order_relaxed);
      mMaxVal.store(n.maxVal, std::memory_order_relaxed);
   }

   float Level() const { return mLevel.load(std::memory_order_relaxed); }
   float RawEnvelope() const { return mEnv.load(std::memory_order_relaxed); }

private:
   double mSampleRate = 44100.0;
   std::atomic<float> mEnv { 0.0f };
   std::atomic<float> mLevel { 0.0f };
   std::atomic<float> mGain { 1.0f };
   std::atomic<float> mAttackMs { 10.0f };
   std::atomic<float> mReleaseMs { 100.0f };
   std::atomic<int> mMode { 0 };
   std::atomic<float> mMinVal { 0.0f };
   std::atomic<float> mMaxVal { 1.0f };
};

AudioToCVNode::AudioToCVNode() = default;
AudioToCVNode::~AudioToCVNode() = default;

void AudioToCVNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioAudioToCVNode>();
   mAudioNode->PushParams(*this);
}

void AudioToCVNode::VisitParams(ParamVisitor& v)
{
   v.Float("gain", gain);
   v.Float("attackMs", attackMs);
   v.Float("releaseMs", releaseMs);
   v.Int("mode", mode);
   v.Float("minVal", minVal);
   v.Float("maxVal", maxVal);
}

AudioNode* AudioToCVNode::AudioNodeForNotePorts()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioAudioToCVNode>();
   return mAudioNode.get();
}

float AudioToCVNode::Value01()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioAudioToCVNode>();
   return mAudioNode->Level();
}

float AudioToCVNode::CurrentLevel() const
{
   return mAudioNode ? mAudioNode->Level() : 0.0f;
}

float AudioToCVNode::RawEnvelope() const
{
   return mAudioNode ? mAudioNode->RawEnvelope() : 0.0f;
}
