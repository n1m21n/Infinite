#include "ModulatorNodes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "Transport.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"

constexpr int PatternNode::kSteps;

namespace
{
   const std::vector<std::string> kLfoShapes = {
      "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold"
   };

   float Frac(double v) { return (float)(v - std::floor(v)); }

   // Deterministic hash so a given step always yields the same value; the pattern
   // repeats identically after a rewind instead of drifting.
   float Hash01(long long n)
   {
      unsigned long long x = (unsigned long long)(n * 2654435761u + 1013904223u);
      x ^= x >> 33;
      x *= 0xff51afd7ed558ccdULL;
      x ^= x >> 33;
      return (float)((x >> 11) & 0xFFFFFF) / (float)0xFFFFFF;
   }

   float Remap(float v01, float low, float high)
   {
      // Value01() is contractually 0..1 regardless of what low/high were
      // typed as, so the mapped result is clamped back into that range too -
      // low > high (an inverted swing) stays legitimate, but low/high outside
      // 0..1 must not leak out of contract.
      float mapped = low + (high - low) * std::min(1.0f, std::max(0.0f, v01));
      return std::min(1.0f, std::max(0.0f, mapped));
   }
}

// ---------------------------------------------------------------- LFO

const std::vector<std::string>& LFONode::ShapeNames()
{
   return kLfoShapes;
}

float LFONode::Value01()
{
   const double beats = Transport::Instance().Beats();
   const float rate = std::max(0.01f, rateBeats);
   const double cycles = beats / rate;
   const float t = Frac(cycles + phase);

   float raw = 0.0f;
   switch (shape)
   {
      case 0: raw = 0.5f + 0.5f * std::sin(t * 6.28318530718f); break;      // Sine
      case 1: raw = t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f; break;           // Triangle
      case 2: raw = t; break;                                               // Saw Up
      case 3: raw = 1.0f - t; break;                                        // Saw Down
      case 4: raw = t < 0.5f ? 0.0f : 1.0f; break;                          // Square
      default: raw = Hash01((long long)std::floor(cycles + phase)); break;  // S&H
   }

   return Remap(raw, low, high);
}

// ---------------------------------------------------------------- Random

float RandomNode::ValueForStep(long long step) const
{
   // The seed is mixed into the hash input rather than added to the result, so
   // two seeds give genuinely different sequences instead of the same sequence
   // offset by a constant.
   const long long salt = (long long)(seed * 1013.0f);
   return Hash01(step * 6364136223LL + salt * 1442695040888963407LL);
}

float RandomNode::Value01()
{
   const double beats = Transport::Instance().Beats();
   const float rate = std::max(0.01f, rateBeats);
   const double pos = beats / rate;
   const long long step = (long long)std::floor(pos);
   const float t = Frac(pos);

   float current = ValueForStep(step);
   float next = ValueForStep(step + 1);

   float raw = current;
   if (smooth > 0.0f)
   {
      // ease only across the tail of the step, scaled by the smooth amount
      float k = std::min(1.0f, t / std::max(0.0001f, smooth));
      float eased = k * k * (3.0f - 2.0f * k);
      raw = current + (next - current) * eased;
   }

   return Remap(raw, low, high);
}

// ---------------------------------------------------------------- Pattern

int PatternNode::EffectiveLength() const
{
   if (fitToBar)
   {
      const int stepsPerBar = Transport::Instance().TimeSigNumerator() * std::max(1, fitDivision);
      return std::max(1, std::min(stepsPerBar, kSteps));
   }
   return std::max(1, std::min(length, kSteps));
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
   const float v = input ? input->Value01() : constantIn;
   const float span = inHigh - inLow;
   const float t = std::fabs(span) > 1e-6f ? (v - inLow) / span : 0.0f;
   const float r = outLow + (outHigh - outLow) * t;
   if (!clampOutput) return r;
   return std::min(std::max(outLow, outHigh), std::max(std::min(outLow, outHigh), r));
}

// ---------------------------------------------------------------- Smooth

float SmoothNode::Value01()
{
   const float target = input ? input->Value01() : constantIn;
   const double beats = Transport::Instance().Beats();
   if (mLast >= 0.0f && beats == mLastBeats)
      return mLast; // already advanced this tick - don't double-apply the filter

   const float k = std::min(1.0f, std::max(0.0f, amount));
   mLast = (mLast < 0.0f) ? target : mLast + (target - mLast) * (1.0f - k);
   mLastBeats = beats;
   return mLast;
}

// ---------------------------------------------------------------- Mod Depth

float ModDepthNode::Value01()
{
   const float v = input ? input->Value01() : constantIn;
   return std::clamp(0.5f + (v - 0.5f) * depth, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- Envelope

float EnvelopeNode::Value01()
{
   const float in01 = input ? input->Value01() : constantIn;
   const double seconds = Transport::Instance().Seconds();

   const bool above = in01 >= threshold;
   if (above != mGateOpen)
   {
      mGateOpen = above;
      mStage = above ? Stage::Attack : Stage::Release;
      mStageStartLevel = mLevel;
      mStageStartSeconds = seconds;
   }

   const double elapsedMs = mStageStartSeconds < 0.0 ? 0.0 : (seconds - mStageStartSeconds) * 1000.0;

   switch (mStage)
   {
      case Stage::Idle:
         mLevel = 0.0f;
         break;

      case Stage::Attack:
      {
         const float t = (float)(elapsedMs / std::max(0.001f, attackMs));
         if (t >= 1.0f)
         {
            mLevel = 1.0f;
            mStage = Stage::Decay;
            mStageStartLevel = 1.0f;
            mStageStartSeconds = seconds;
         }
         else
         {
            mLevel = mStageStartLevel + (1.0f - mStageStartLevel) * t;
         }
         break;
      }

      case Stage::Decay:
      {
         const float sustain = std::clamp(sustainLevel, 0.0f, 1.0f);
         const float t = (float)(elapsedMs / std::max(0.001f, decayMs));
         if (t >= 1.0f)
         {
            mLevel = sustain;
            mStage = Stage::Sustain;
         }
         else
         {
            mLevel = 1.0f + (sustain - 1.0f) * t;
         }
         break;
      }

      case Stage::Sustain:
         mLevel = std::clamp(sustainLevel, 0.0f, 1.0f);
         break;

      case Stage::Release:
      {
         const float t = (float)(elapsedMs / std::max(0.001f, releaseMs));
         if (t >= 1.0f)
         {
            mLevel = 0.0f;
            mStage = Stage::Idle;
         }
         else
         {
            mLevel = mStageStartLevel * (1.0f - t);
         }
         break;
      }
   }

   return std::clamp(0.5f + (in01 - 0.5f) * mLevel, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- CV to Pitch

float CVToPitchNode::Value01()
{
   const float v01 = std::clamp(input ? input->Value01() : constantIn, 0.0f, 1.0f);

   const int low = std::min(rangeLow, rangeHigh);
   const int high = std::max(rangeLow, rangeHigh);
   const int semitoneCount = std::max(1, high - low + 1);
   const int idx = std::clamp((int)std::lround(v01 * (float)(semitoneCount - 1)), 0, semitoneCount - 1);
   int semitone = low + idx;

   if (scale != MusicTime::kChromatic)
      semitone = MusicTime::SnapToScale(semitone, root, scale, MusicTime::kSnapNearest);
   mLastSemitone = semitone;

   const int snappedIdx = std::clamp(semitone - low, 0, semitoneCount - 1);
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
   const float v = input ? input->Value01() : constantIn;
   return low + high - v;
}

// ---------------------------------------------------------------- Mod Curve

float ModCurveNode::Value01()
{
   const float in = input ? input->Value01() : constantIn;
   const float x = std::clamp(in, 0.0f, 1.0f); // clamp for lookup only; the curve is only defined on 0..1
   const float y = curve.Evaluate(x);
   return in + (y - in) * mix;
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
