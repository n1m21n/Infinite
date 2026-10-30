#include "StringNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"

namespace
{
   enum SmoothedParam
   {
      kParamPosition = 0,
      kParamTone,
      kParamDecay,
      kParamStiffness,
      kParamBody,
      kParamVolume,
      kParamPressure,
      kParamNoise,
   };

   // One loop per voice, long enough for 16 Hz at 192 kHz.
   constexpr int kBufSize = 16384;
   constexpr int kBufMask = kBufSize - 1;
   constexpr int kDispersionStages = 4;
   // Tuning, loss and dispersion are re-solved this often (0.7 ms at 48 kHz).
   constexpr int kChunk = 32;

   // Phase delay in samples of the first-order allpass (a + z^-1) / (1 + a z^-1)
   // at w radians per sample.
   double AllpassDelay(double a, double w)
   {
      const double s = std::sin(w), c = std::cos(w);
      const double num = std::atan2(-s, a + c);
      const double den = std::atan2(-a * s, 1.0 + a * c);
      return -(num - den) / w;
   }

   // The coefficient whose phase delay at w is `delay` (delay falls as a rises).
   double SolveAllpass(double delay, double w)
   {
      double lo = -0.95, hi = 0.95;
      for (int it = 0; it < 40; it++)
      {
         const double mid = 0.5 * (lo + hi);
         if (AllpassDelay(mid, w) > delay)
            lo = mid;
         else
            hi = mid;
      }
      return 0.5 * (lo + hi);
   }

   // The body: a bank of constant-peak bandpass modes, one table per body
   // type. Box is the low air and plate modes of a small acoustic box
   // (Helmholtz resonance, then plate modes). Guitar is fitted from 101
   // notes of a classical guitar (string_body_fit.py: the dB average of every
   // note's spectrum keeps what the notes share, the body; r 0.68 with that
   // curve, box 0.42). Harp from 43 concert-harp notes (r 0.80), violin and
   // cello from 21 and 19 section pizzicato notes (r 0.86, 0.75; box about 0).
   struct BodyMode
   {
      float hz, q, gain;
   };
   constexpr int kBodyModeCount = 10;
   constexpr BodyMode kBodyModes[StringNode::kBodyTypeCount][kBodyModeCount] = {
      {
         { 98.0f, 14.0f, 1.00f },  { 204.0f, 22.0f, 0.85f }, { 283.0f, 26.0f, 0.60f }, { 399.0f, 28.0f, 0.55f },
         { 552.0f, 30.0f, 0.42f }, { 728.0f, 30.0f, 0.35f }, { 1020.0f, 26.0f, 0.30f }, { 1430.0f, 22.0f, 0.26f },
         { 2200.0f, 14.0f, 0.22f }, { 3300.0f, 8.0f, 0.18f },
      },
      {
         { 99.3f, 13.9f, 1.00f },  { 167.1f, 2.0f, 0.11f },  { 198.7f, 7.7f, 0.47f },  { 254.0f, 2.2f, 0.13f },
         { 391.7f, 7.5f, 0.17f },  { 493.5f, 4.3f, 0.11f },  { 772.2f, 6.9f, 0.10f },  { 1261.6f, 2.0f, 0.10f },
         { 1544.3f, 2.0f, 0.10f }, { 3368.2f, 2.0f, 0.15f },
      },
      {
         { 157.7f, 7.5f, 0.57f },  { 210.5f, 7.7f, 1.00f },  { 265.2f, 5.7f, 0.64f },  { 324.7f, 5.8f, 0.90f },
         { 397.4f, 6.8f, 0.62f },  { 493.5f, 6.2f, 0.61f },  { 604.1f, 2.0f, 0.23f },  { 728.8f, 2.0f, 0.48f },
         { 1173.8f, 2.0f, 0.33f }, { 1566.8f, 2.0f, 0.23f },
      },
      {
         { 195.8f, 2.0f, 0.07f },  { 232.9f, 4.4f, 0.21f },  { 281.0f, 7.7f, 1.00f },  { 334.2f, 4.6f, 0.28f },
         { 421.0f, 4.6f, 0.55f },  { 530.5f, 6.2f, 0.39f },  { 630.8f, 3.1f, 0.12f },  { 905.1f, 2.0f, 0.23f },
         { 1478.9f, 2.0f, 0.11f }, { 1917.8f, 2.0f, 0.09f },
      },
      {
         { 167.1f, 3.8f, 0.41f },  { 198.7f, 4.8f, 1.00f },  { 239.7f, 2.0f, 0.28f },  { 320.0f, 2.7f, 0.86f },
         { 433.4f, 3.8f, 0.49f },  { 668.3f, 2.0f, 0.33f },  { 1001.4f, 2.0f, 0.47f },  { 1522.2f, 2.0f, 0.29f },
         { 1810.2f, 2.0f, 0.31f }, { 2215.8f, 2.0f, 0.36f },
      },
   };
   // Makes the body path roughly as loud as the dry string on a plucked note;
   // per table, so every body type sits at the box's level (mean power over
   // 80 Hz - 5 kHz).
   constexpr float kBodyGain[StringNode::kBodyTypeCount] = { 5.5f, 5.5f * 0.917f, 5.5f * 0.336f, 5.5f * 0.536f, 5.5f * 0.281f };

   struct StringVoice
   {
      bool active = false;
      bool held = false;
      int note = -1;
      int voiceId = 0;
      int exciter = StringNode::kPluck;
      float velocity = 0.0f;
      float bend = 0.0f;
      uint64_t age = 0;
      int chunkLeft = 0;
      // A bowed voice runs its loop `os` times per output sample: the
      // stick-slip corner lands on a sample boundary, so a short loop locks
      // toward whole-sample periods (+30 c at C8) unless the grid is finer.
      int os = 1;
      float prevTicks[8] = {};

      // The loop: M samples of delay, then at the bridge the 3-tap
      // linear-phase loss filter (exactly 1 sample), the dispersion
      // allpasses and the tuning allpass.
      float buf[kBufSize];
      int w = 0;
      int M = 100;
      int tap = 10; // exciter's distance from the bridge, in samples of one rail
      float b = 0.125f;
      float g = 0.999f;
      float z1 = 0.0f, z2 = 0.0f;
      float ad = 0.0f;
      float dx[kDispersionStages] = {}, dy[kDispersionStages] = {};
      float at = 0.0f, tx = 0.0f, ty = 0.0f;

      // Pluck/strike: a Hann pulse of `pulseLen` samples, `pulseAmp` tall.
      int pulseLen = 0, pulsePos = 0;
      float pulseAmp = 0.0f;
      // Bow: speed and how far the bow is down (ramps on and off).
      float bowSpeed = 0.0f;
      float bowEnv = 0.0f;

      float level = 0.0f; // decaying peak of the output, to free the voice

      void Clear()
      {
         std::memset(buf, 0, sizeof(buf));
         w = 0;
         z1 = z2 = 0.0f;
         for (int d = 0; d < kDispersionStages; d++)
            dx[d] = dy[d] = 0.0f;
         tx = ty = 0.0f;
         level = 0.0f;
         bowEnv = 0.0f;
      }
   };
}

const std::vector<std::string>& StringNode::ExciterNames()
{
   static const std::vector<std::string> kNames = { "pluck", "bow", "strike" };
   return kNames;
}

const std::vector<std::string>& StringNode::BodyTypeNames()
{
   static const std::vector<std::string> kNames = { "box", "guitar", "harp", "violin", "cello" };
   return kNames;
}

// ------------------------------------------------------------- audio thread
class AudioStringNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = StringNode::kMaxVoices;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      mBodyTypeSet = -1;
      SetBodyType(mBodyType.load(std::memory_order_relaxed));
      mDc.SetSampleRate(mSampleRate);
      Reset();
   }

   void Reset() override
   {
      for (auto& v : mVoices)
         v.active = false;
      for (auto& bq : mBody)
         bq.Reset();
      mDc.Reset();
      mNextAge = 1;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void SetClipPitchOverride(float semitones) override { mClipPitch.store(semitones, std::memory_order_relaxed); }

   int ActiveVoices() const { return mActiveVoiceCount.load(std::memory_order_relaxed); }

   void PushParams(const StringNode& n)
   {
      mMailbox.Push(kParamPosition, n.position);
      mMailbox.Push(kParamTone, n.tone);
      mMailbox.Push(kParamDecay, n.decay);
      mMailbox.Push(kParamStiffness, n.stiffness);
      mMailbox.Push(kParamBody, n.body);
      mMailbox.Push(kParamVolume, n.volume);
      mMailbox.Push(kParamPressure, n.pressure);
      mMailbox.Push(kParamNoise, n.noise);
      mHardness.store(std::clamp(n.hardness, 0.0f, 1.0f), std::memory_order_relaxed);
      mRelease.store(n.release, std::memory_order_relaxed);
      mExciter.store(std::clamp(n.exciter, 0, (int)StringNode::kExciterCount - 1), std::memory_order_relaxed);
      mBodyType.store(std::clamp(n.bodyType, 0, (int)StringNode::kBodyTypeCount - 1), std::memory_order_relaxed);
   }

   // Audio thread: retune the body bank when the type changes. The filters
   // keep their state, so a switch while ringing does not click to silence.
   void SetBodyType(int type)
   {
      if (type == mBodyTypeSet)
         return;
      mBodyTypeSet = type;
      for (int m = 0; m < kBodyModeCount; m++)
         mBody[m].SetBandpass(std::min((double)kBodyModes[type][m].hz, 0.45 * mSampleRate), kBodyModes[type][m].q, mSampleRate);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      if (output.numChannels <= 0 || output.numFrames <= 0)
         return;
      float* outL = output.channels[0];
      float* outR = (output.numChannels > 1 && output.channels[1]) ? output.channels[1] : nullptr;
      for (int ch = 2; ch < output.numChannels; ch++)
         if (output.channels[ch])
            std::fill_n(output.channels[ch], output.numFrames, 0.0f);

      SetBodyType(mBodyType.load(std::memory_order_relaxed));
      const BodyMode* modes = kBodyModes[mBodyTypeSet];
      const float bodyGain = kBodyGain[mBodyTypeSet];

      NoteEvent evts[64];
      const int numEvts = mNoteInbox ? mNoteInbox->Pop(mNoteCursor, evts, 64) : 0;
      int nextEvt = 0;

      const float release = std::max(1.0f, mRelease.load(std::memory_order_relaxed));
      const float clipPitch = mClipPitch.load(std::memory_order_relaxed);
      const float bowStep = 1.0f / (0.04f * (float)mSampleRate);   // 40 ms on
      const float bowLift = 1.0f / (0.015f * (float)mSampleRate);  // 15 ms off

      for (int i = 0; i < output.numFrames; i++)
      {
         while (nextEvt < numEvts && evts[nextEvt].frameOffset <= i)
            HandleEvent(evts[nextEvt++]);

         const float position = std::clamp(mMailbox.SmoothedValue(kParamPosition), 0.02f, 0.5f);
         const float tone = std::clamp(mMailbox.SmoothedValue(kParamTone), 0.0f, 1.0f);
         const float decay = std::clamp(mMailbox.SmoothedValue(kParamDecay), 0.05f, 20.0f);
         const float stiffness = std::clamp(mMailbox.SmoothedValue(kParamStiffness), 0.0f, 1.0f);
         const float body = std::clamp(mMailbox.SmoothedValue(kParamBody), 0.0f, 1.0f);
         const float volume = mMailbox.SmoothedValue(kParamVolume);
         // Bow force sets the friction curve's slope: 3.5 at 0.5 (Smith's
         // value), x2.8 steeper at 0 (slips sooner), /2.8 at 1 (sticks longer).
         mBowSlope = 3.5f * std::exp2(1.5f * (1.0f - 2.0f * std::clamp(mMailbox.SmoothedValue(kParamPressure), 0.0f, 1.0f)));
         mBowNoise = std::clamp(mMailbox.SmoothedValue(kParamNoise), 0.0f, 1.0f);

         float dry = 0.0f;
         for (auto& v : mVoices)
         {
            if (!v.active)
               continue;
            if (v.chunkLeft <= 0)
            {
               Retune(v, clipPitch, position, tone, decay, stiffness, release);
               v.chunkLeft = kChunk;
            }
            v.chunkLeft--;
            dry += Render(v, bowStep, bowLift);
         }

         float wet = 0.0f;
         for (int m = 0; m < kBodyModeCount; m++)
            wet += modes[m].gain * mBody[m].Process(dry);
         float y = dry + (wet * bodyGain - dry) * body;
         y = mDc.Process(y) * volume;
         if (!std::isfinite(y))
         {
            y = 0.0f;
            for (auto& bq : mBody)
               bq.Reset();
            mDc.Reset();
         }
         y = std::clamp(y, -4.0f, 4.0f);
         outL[i] = y;
         if (outR)
            outR[i] = y;
      }
      while (nextEvt < numEvts)
         HandleEvent(evts[nextEvt++]);

      int count = 0;
      for (const auto& v : mVoices)
         count += v.active ? 1 : 0;
      mActiveVoiceCount.store(count, std::memory_order_relaxed);
   }

private:
   // Solves the loop for the voice's current pitch: total delay M + 1 +
   // dispersion + tuning allpass equals sr / f exactly at the fundamental,
   // and the loop gain gives the fundamental a T60 of `decay` (the damper's
   // `release` once the note is let go).
   void Retune(StringVoice& v, float clipPitch, float position, float tone, float decay, float stiffness,
               float releaseMs)
   {
      const double sr = mSampleRate * (double)v.os;
      double f = 440.0 * std::exp2(((double)v.note - 69.0 + (double)v.bend + (double)clipPitch) / 12.0);
      f = std::clamp(f, sr / (double)(kBufSize - 64), sr / 8.0);
      const double P = sr / f;
      const double w = 2.0 * M_PI * f / sr;

      const double t60 = v.held ? (double)decay : std::min((double)decay, (double)releaseMs * 0.001);
      const double g0 = std::pow(10.0, -3.0 / (f * t60));
      // Tone's loss never outruns decay at the fundamental: high notes
      // keep the decay they were asked for and only lose upper partials.
      double b = 0.25 * (1.0 - (double)tone);
      b = std::min(b, std::max(0.0, (1.0 - g0 / 0.99999) / (2.0 * (1.0 - std::cos(w)))));
      const double lossAtF0 = std::fabs(1.0 - 2.0 * b + 2.0 * b * std::cos(w));

      // Stiffness: four allpasses with a < 0 delay the low end more than the
      // top, so upper partials run sharp. Capped so they fit in the loop.
      double k = 0.9 * (double)stiffness;
      double disp = kDispersionStages * AllpassDelay(-k, w);
      while (k > 1e-4 && disp > 0.35 * P)
      {
         k *= 0.8;
         disp = kDispersionStages * AllpassDelay(-k, w);
      }
      if (k <= 1e-4)
      {
         k = 0.0; // the stages are bypassed, not four plain delays
         disp = 0.0;
      }

      const double rem = P - 1.0 - disp;
      int M = std::max(4, (int)std::floor(rem - 0.5));
      const double tune = rem - (double)M;

      const double g = g0 / std::max(1e-6, lossAtF0);

      v.M = M;
      v.b = (float)b;
      v.g = (float)std::min(g, 0.99999);
      v.ad = (float)-k;
      v.at = (float)SolveAllpass(tune, w);
      v.tap = std::clamp((int)std::lround((double)position * (double)M * 0.5), 1, std::max(1, M / 2));
   }

   // One output sample: `os` loop ticks, decimated with triangle weights
   // over this sample's ticks and the last one's (sinc^2: nulls at every
   // multiple of the output rate).
   float Render(StringVoice& v, float bowStep, float bowLift)
   {
      float out;
      if (v.os == 1)
         out = Tick(v, bowStep, bowLift);
      else
      {
         const float st = bowStep / (float)v.os, lt = bowLift / (float)v.os;
         float acc = 0.0f;
         for (int j = 0; j < v.os; j++)
         {
            const float t = Tick(v, st, lt);
            acc += (float)(j + 1) * v.prevTicks[j] + (float)(v.os - j) * t;
            v.prevTicks[j] = t;
         }
         out = acc / (float)(v.os * (v.os + 1));
      }
      v.level = std::max(std::fabs(out), v.level * 0.9995f);
      const bool exciting = v.pulsePos < v.pulseLen || v.bowEnv > 0.0f || (v.held && v.exciter == StringNode::kBow);
      if (!exciting && v.level < 1e-5f)
         v.active = false;
      return out;
   }

   float Tick(StringVoice& v, float bowStep, float bowLift)
   {
      const float old = v.buf[(v.w - v.M) & kBufMask];
      float x = v.b * old + (1.0f - 2.0f * v.b) * v.z1 + v.b * v.z2;
      v.z2 = v.z1;
      v.z1 = old;
      for (int d = 0; d < kDispersionStages && v.ad != 0.0f; d++)
      {
         const float y = v.ad * x + v.dx[d] - v.ad * v.dy[d];
         v.dx[d] = x;
         v.dy[d] = DspMath::FlushDenormal(y);
         x = v.dy[d];
      }
      const float y = v.at * x + v.tx - v.at * v.ty;
      v.tx = x;
      v.ty = DspMath::FlushDenormal(y);
      v.buf[v.w] = v.g * v.ty;

      // The exciter's point: the left-going wave passed it `tap` samples ago,
      // the right-going one (stored inverted, past the nut) M - tap ago.
      // Adding d to the string's velocity there adds d to both waves.
      float* left = &v.buf[(v.w - v.tap) & kBufMask];
      float* right = &v.buf[(v.w - (v.M - v.tap)) & kBufMask];
      if (v.pulsePos < v.pulseLen)
      {
         const float ph = ((float)v.pulsePos + 0.5f) / (float)v.pulseLen;
         const float d = v.pulseAmp * (0.5f - 0.5f * std::cos(2.0f * (float)M_PI * ph));
         *left += d;
         *right -= d;
         v.pulsePos++;
      }
      if (v.exciter == StringNode::kBow)
      {
         v.bowEnv = v.held ? std::min(1.0f, v.bowEnv + bowStep) : std::max(0.0f, v.bowEnv - bowLift);
         if (v.bowEnv > 0.0f)
         {
            // Smith's friction junction: the bow sticks while the string
            // moves with it and slips once they part (reflection falls off
            // as the fourth power of the velocity difference).
            // Rosin noise jitters the bow's speed (up to +-50%).
            float speed = v.bowSpeed * v.bowEnv;
            if (mBowNoise > 0.0f)
            {
               mRng = mRng * 1664525u + 1013904223u;
               speed *= 1.0f + mBowNoise * ((float)(mRng >> 8) * (1.0f / 8388608.0f) - 1.0f) * 0.5f;
            }
            const float vs = *left - *right;
            const float dv = speed - vs;
            const float r = std::min(1.0f, std::pow(std::fabs(dv * mBowSlope) + 0.75f, -4.0f));
            const float d = r * dv * v.bowEnv;
            *left += d;
            *right -= d;
         }
      }
      v.w = (v.w + 1) & kBufMask;

      return -old;
   }

   void HandleEvent(const NoteEvent& e)
   {
      if (e.bendUpdate)
      {
         for (auto& v : mVoices)
            if (v.active && v.voiceId == e.voiceId)
               v.bend = e.bendSemitones;
         return;
      }
      if (e.isNoteOn && e.velocity > 0.0f)
      {
         NoteOn(e);
         return;
      }
      for (auto& v : mVoices)
         if (v.active && v.held && v.note == e.note && (v.voiceId == e.voiceId || e.voiceId == 0))
         {
            v.held = false;
            v.chunkLeft = 0; // the damper lands now, not a chunk later
         }
   }

   void NoteOn(const NoteEvent& e)
   {
      // Same note re-excites its ringing string; else a free voice; else
      // the oldest, cleared.
      int slot = -1;
      for (int k = 0; k < kMaxVoices && slot < 0; k++)
         if (mVoices[k].active && mVoices[k].note == e.note)
            slot = k;
      const bool sameNote = slot >= 0;
      for (int k = 0; k < kMaxVoices && slot < 0; k++)
         if (!mVoices[k].active)
            slot = k;
      if (slot < 0)
      {
         uint64_t oldest = UINT64_MAX;
         for (int k = 0; k < kMaxVoices; k++)
            if (mVoices[k].age < oldest)
            {
               oldest = mVoices[k].age;
               slot = k;
            }
      }
      StringVoice& v = mVoices[slot];
      const int exciter = mExciter.load(std::memory_order_relaxed);
      // A new exciter on a ringing string changes its loop rate; start clean.
      const bool same = sameNote && v.exciter == exciter;
      if (!same)
         v.Clear();
      v.active = true;
      v.held = true;
      v.note = e.note;
      v.voiceId = e.voiceId;
      v.velocity = std::clamp(e.velocity, 0.0f, 1.0f);
      v.bend = e.bendSemitones;
      v.age = mNextAge++;
      v.exciter = exciter;
      v.chunkLeft = 0;
      v.level = std::max(v.level, 1e-3f);

      // A pluck is a sharp pulse (a soft pick or a finger widens it up to
      // 10x, which dulls the top); a strike is a felt hammer, longer and
      // softer the gentler the hit. Both put the same total velocity in.
      const double sr = mSampleRate;
      if (!same)
      {
         v.os = 1;
         if (v.exciter == StringNode::kBow)
         {
            const double period = sr / (440.0 * std::exp2(((double)e.note - 69.0) / 12.0));
            while (v.os < 8 && period * (double)v.os < 96.0)
               v.os *= 2;
         }
         std::fill_n(v.prevTicks, 8, 0.0f);
      }
      v.pulsePos = 0;
      if (v.exciter == StringNode::kPluck)
         v.pulseLen = std::max(2, (int)std::lround(sr * 0.0003 * std::pow(10.0, 1.0 - (double)mHardness.load(std::memory_order_relaxed))));
      else if (v.exciter == StringNode::kStrike)
         v.pulseLen = std::max(2, (int)std::lround(sr * (0.004 - 0.003 * (double)v.velocity)));
      else
         v.pulseLen = 0;
      v.pulseAmp = v.pulseLen > 0 ? 2.0f * 3.0f * v.velocity / (float)v.pulseLen : 0.0f;
      v.bowSpeed = 0.07f * (0.4f + 0.6f * v.velocity);
   }

   double mSampleRate = 44100.0;
   StringVoice mVoices[kMaxVoices];
   uint64_t mNextAge = 1;
   DspMath::Biquad mBody[kBodyModeCount];
   DspMath::DcBlocker mDc;

   ParamMailbox mMailbox;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   std::atomic<float> mRelease{ 150.0f };
   std::atomic<int> mExciter{ StringNode::kPluck };
   std::atomic<int> mBodyType{ StringNode::kBodyBox };
   std::atomic<float> mHardness{ 1.0f };
   float mBowSlope = 3.5f, mBowNoise = 0.0f; // this sample's bow force and noise (audio thread)
   uint32_t mRng = 0x2545F491u;
   int mBodyTypeSet = -1; // the table the body bank is tuned to (audio thread)
   std::atomic<float> mClipPitch{ 0.0f };
   std::atomic<int> mActiveVoiceCount{ 0 };
};

// -------------------------------------------------------------- main thread
StringNode::StringNode() = default;
StringNode::~StringNode() = default;

AudioNode* StringNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioStringNode>();
   return mAudioNode.get();
}

int StringNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void StringNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioStringNode>();
   mAudioNode->PushParams(*this);
}

void StringNode::VisitParams(ParamVisitor& v)
{
   v.Int("exciter", exciter);
   v.Float("position", position);
   v.Float("tone", tone);
   v.Float("decay", decay);
   v.Float("stiffness", stiffness);
   v.Float("body", body);
   v.Int("bodyType", bodyType);
   v.Float("release", release);
   v.Float("hardness", hardness);
   v.Float("pressure", pressure);
   v.Float("noise", noise);
   v.Float("volume", volume);
}
