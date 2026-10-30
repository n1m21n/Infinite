#include "WindNode.h"

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
      kParamBreath = 0,
      kParamTone,
      kParamNoise,
      kParamVibrato,
      kParamVolume,
   };

   // Two delay lines per voice (bore, and the flute's jet or the sax's
   // second bore section), long enough for a brass C0 at 192 kHz.
   constexpr int kBufSize = 32768;
   constexpr int kBufMask = kBufSize - 1;
   // Delays and the lip resonator are re-solved this often (0.7 ms at 48 kHz).
   constexpr int kChunk = 32;

   // Per instrument. Pressure: blowing pressure at breath 0 and its range up
   // to breath 1 (each excitation has its own threshold of oscillation).
   // Loop: the bore's round trip in periods of the note (a reed on a
   // cylinder sounds at twice its round trip; the flute's jet and brass lips
   // lock to the second loop mode). Gain: output level, matched so the four
   // sit together. Trimmed by tools/universe/wind_proto.py and wind_tune.py.
   struct InstrumentSpec
   {
      float pressureMin, pressureRange;
      float loop;
      float gain;
   };
   constexpr InstrumentSpec kSpecs[WindNode::kInstrumentCount] = {
      { 0.60f, 0.40f, 0.5f, 0.85f },    // clarinet
      { 0.68f, 0.22f, 1.0f, 0.6f },     // sax: speaks from p ~0.6
      { 0.88f, 0.32f, 1.9965f, 1.3f },  // flute: the jet table clips above ~1.3
      { 0.40f, 0.45f, 2.0f, 1.9f },     // brass
   };
   constexpr float kSaxReedPos = 0.4f;   // reed's share of the conical bore
   constexpr float kJetRatio = 0.32f;    // flute: jet delay over the bore delay
   constexpr float kTongueAmount = 0.4f; // onset overshoot of the breath pressure
   constexpr double kTongueMs = 40.0;    // its decay time constant
   // Brass lip resonator: a constant Q (so its bandwidth follows the note) and
   // a fixed DC gain, so the valve neither saturates open on low notes nor
   // stays shut on high ones; tuned a little under the note, since the lip
   // pulls the pitch towards itself (further under on low notes, which it
   // pulls harder).
   constexpr double kLipQ = 25.0;
   constexpr double kLipDcGain = 3.0;
   constexpr double kLipTune = 0.975;

   // Phase delay in samples of the one-pole lowpass (1 - a) / (1 - a z^-1)
   // at w radians per sample.
   double OnePoleDelay(double a, double w)
   {
      return std::atan2(a * std::sin(w), 1.0 - a * std::cos(w)) / w;
   }

   struct WindVoice
   {
      bool active = false;
      bool held = false;
      int note = -1;
      int voiceId = 0;
      int instrument = WindNode::kClarinet;
      float velocity = 0.0f;
      float bend = 0.0f;
      uint64_t age = 0;
      int chunkLeft = 0;

      float bore[kBufSize];
      float aux[kBufSize]; // flute: the jet; sax: the bore past the reed
      int w = 0;
      float d0 = 100.0f, d1 = 30.0f; // bore and aux delays, samples (fractional)

      float env = 0.0f;   // breath envelope 0..1
      float lp = 0.0f;    // bell loss state
      float dcIn = 0.0f, dcOut = 0.0f;
      // Brass lip: two-pole resonator at the note's frequency.
      float lipA1 = 0.0f, lipA2 = 0.0f, lipG = 0.0f, lipY1 = 0.0f, lipY2 = 0.0f;
      float level = 0.0f; // decaying peak of the output, to free the voice
      float tongue = 0.0f; // onset pressure overshoot, 1 at note-on, decays

      void Clear()
      {
         std::memset(bore, 0, sizeof(bore));
         std::memset(aux, 0, sizeof(aux));
         w = 0;
         env = lp = dcIn = dcOut = 0.0f;
         lipY1 = lipY2 = 0.0f;
         level = 0.0f;
      }
   };

   // Linear-interpolated read `d` samples behind the write head.
   inline float ReadDelay(const float* buf, int w, float d)
   {
      const float pos = (float)w - d;
      const float fl = std::floor(pos);
      const int i = (int)fl;
      const float frac = pos - fl;
      return buf[i & kBufMask] + frac * (buf[(i + 1) & kBufMask] - buf[i & kBufMask]);
   }
}

const std::vector<std::string>& WindNode::InstrumentNames()
{
   static const std::vector<std::string> kNames = { "clarinet", "sax", "flute", "brass" };
   return kNames;
}

// ------------------------------------------------------------- audio thread
class AudioWindNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = WindNode::kMaxVoices;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      mDc.SetSampleRate(mSampleRate);
      mHighpass.SetHighpass(60.0, 0.707, mSampleRate);
      mLoopDcR = (float)std::exp(-2.0 * M_PI * 20.0 / mSampleRate);
      mTongueDecay = (float)std::exp(-1.0 / (kTongueMs * 0.001 * mSampleRate));
      Reset();
   }

   void Reset() override
   {
      for (auto& v : mVoices)
         v.active = false;
      mDc.Reset();
      mHighpass.Reset();
      mNextAge = 1;
      mVibPhase = 0.0;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void SetClipPitchOverride(float semitones) override { mClipPitch.store(semitones, std::memory_order_relaxed); }

   int ActiveVoices() const { return mActiveVoiceCount.load(std::memory_order_relaxed); }

   // Lets go of every held voice at the next block that has one sounding
   // (the param sweep's way to hear release while it holds its note).
   void ReleaseAll() { mReleaseAll.store(true, std::memory_order_relaxed); }

   void PushParams(const WindNode& n)
   {
      mMailbox.Push(kParamBreath, n.breath);
      mMailbox.Push(kParamTone, n.tone);
      mMailbox.Push(kParamNoise, n.noise);
      mMailbox.Push(kParamVibrato, n.vibrato);
      mMailbox.Push(kParamVolume, n.volume);
      mAttack.store(n.attack, std::memory_order_relaxed);
      mRelease.store(n.release, std::memory_order_relaxed);
      mInstrument.store(std::clamp(n.instrument, 0, (int)WindNode::kInstrumentCount - 1), std::memory_order_relaxed);
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

      if (mReleaseAll.load(std::memory_order_relaxed))
      {
         bool any = false;
         for (auto& v : mVoices)
         {
            any = any || v.active;
            v.held = false;
         }
         if (any)
            mReleaseAll.store(false, std::memory_order_relaxed);
      }

      NoteEvent evts[64];
      const int numEvts = mNoteInbox ? mNoteInbox->Pop(mNoteCursor, evts, 64) : 0;
      int nextEvt = 0;

      const float sr = (float)mSampleRate;
      const float attackStep = 1.0f / (std::max(1.0f, mAttack.load(std::memory_order_relaxed)) * 0.001f * sr);
      const float releaseStep = 1.0f / (std::max(1.0f, mRelease.load(std::memory_order_relaxed)) * 0.001f * sr);
      const float clipPitch = mClipPitch.load(std::memory_order_relaxed);
      const int inst = mInstrument.load(std::memory_order_relaxed);
      const InstrumentSpec& spec = kSpecs[inst];
      const double vibInc = 2.0 * M_PI * 5.5 / mSampleRate;

      for (int i = 0; i < output.numFrames; i++)
      {
         while (nextEvt < numEvts && evts[nextEvt].frameOffset <= i)
            HandleEvent(evts[nextEvt++], inst);

         const float breath = std::clamp(mMailbox.SmoothedValue(kParamBreath), 0.0f, 1.0f);
         const float tone = std::clamp(mMailbox.SmoothedValue(kParamTone), 0.0f, 1.0f);
         const float noiseAmt = std::clamp(mMailbox.SmoothedValue(kParamNoise), 0.0f, 1.0f);
         const float vib = std::clamp(mMailbox.SmoothedValue(kParamVibrato), 0.0f, 1.0f);
         const float volume = mMailbox.SmoothedValue(kParamVolume);
         // Bell loss: a one-pole lowpass in the loop, darker as tone falls;
         // the pole is set for 44.1 kHz and scaled so it sounds the same at
         // every rate.
         const float pole = std::pow(0.75f - 0.6f * tone, 44100.0f / sr);
         mVibPhase += vibInc;
         if (mVibPhase > 2.0 * M_PI)
            mVibPhase -= 2.0 * M_PI;
         const float vibMod = 0.12f * vib * (float)std::sin(mVibPhase);

         float dry = 0.0f;
         for (auto& v : mVoices)
         {
            if (!v.active)
               continue;
            if (v.chunkLeft <= 0)
            {
               Retune(v, inst, clipPitch, pole);
               v.chunkLeft = kChunk;
            }
            v.chunkLeft--;
            v.env = v.held ? std::min(1.0f, v.env + attackStep) : std::max(0.0f, v.env - releaseStep);
            // The tongue's release puts a short pressure overshoot on the
            // onset, which is where a played note's attack is heard.
            v.tongue *= mTongueDecay;
            float p = v.env * (spec.pressureMin + spec.pressureRange * breath) * (0.7f + 0.3f * v.velocity)
                      * (1.0f + kTongueAmount * v.tongue);
            p += p * (0.2f * noiseAmt * mNoise.Next() + vibMod);
            const float out = Tick(v, inst, p, pole) * spec.gain;
            v.level = std::max(std::fabs(out), v.level * 0.9995f);
            if (!v.held && v.env <= 0.0f && v.level < 1e-5f)
               v.active = false;
            dry += out;
         }

         // Radiation from the bell passes the highs and not the lowest
         // rumble of the breath; then the usual DC guard.
         float y = mDc.Process(mHighpass.Process(dry)) * volume;
         if (!std::isfinite(y))
         {
            y = 0.0f;
            for (auto& v : mVoices)
               v.active = false;
            mHighpass.Reset();
            mDc.Reset();
         }
         y = std::clamp(y, -4.0f, 4.0f);
         outL[i] = y;
         if (outR)
            outR[i] = y;
      }
      while (nextEvt < numEvts)
         HandleEvent(evts[nextEvt++], inst);

      int count = 0;
      for (const auto& v : mVoices)
         count += v.active ? 1 : 0;
      mActiveVoiceCount.store(count, std::memory_order_relaxed);
   }

private:
   // Solves the delays for the voice's current pitch: the loop (bore delay +
   // the bell loss's phase delay at the fundamental) equals `loop` periods.
   void Retune(WindVoice& v, int inst, float clipPitch, float pole)
   {
      const double sr = mSampleRate;
      double f = 440.0 * std::exp2(((double)v.note - 69.0 + (double)v.bend + (double)clipPitch) / 12.0);
      const double loopPeriods = kSpecs[inst].loop;
      f = std::clamp(f, sr * loopPeriods / (double)(kBufSize - 64), sr / 6.0);
      const double P = sr / f;
      const double w = 2.0 * M_PI * f / sr;
      const double lossDelay = inst == WindNode::kBrass ? 0.0 : OnePoleDelay(pole, w);
      const double loop = std::max(4.0, loopPeriods * P - lossDelay);
      if (inst == WindNode::kSax)
      {
         v.d1 = (float)std::max(2.0, loop * kSaxReedPos);
         v.d0 = (float)std::max(2.0, loop - (double)v.d1);
      }
      else if (inst == WindNode::kFlute)
      {
         v.d1 = (float)std::max(2.0, loop * kJetRatio / (1.0 + kJetRatio));
         v.d0 = (float)std::max(2.0, loop - (double)v.d1);
      }
      else
         v.d0 = (float)loop;
      if (inst == WindNode::kBrass)
      {
         const double wl = w * (kLipTune - 0.01 * std::clamp((64.0 - (double)v.note) / 24.0, 0.0, 1.0));
         const double r = std::exp(-wl / (2.0 * kLipQ));
         v.lipA1 = (float)(-2.0 * r * std::cos(wl));
         v.lipA2 = (float)(r * r);
         v.lipG = (float)(kLipDcGain * (1.0 + v.lipA1 + v.lipA2));
      }
   }

   float Tick(WindVoice& v, int inst, float p, float pole)
   {
      const float boreOut = ReadDelay(v.bore, v.w, v.d0);
      float out = 0.0f;
      switch (inst)
      {
      case WindNode::kClarinet:
      {
         // Reed (Smith's reed table): the reflection at the mouthpiece
         // opens as the pressure across the reed falls; the bell inverts.
         v.lp = (1.0f - pole) * boreOut + pole * v.lp;
         const float diff = -0.95f * v.lp - p;
         const float r = std::clamp(0.7f - 0.3f * diff, -1.0f, 1.0f);
         out = p + diff * r;
         v.bore[v.w] = out;
         v.aux[v.w] = 0.0f;
         break;
      }
      case WindNode::kSax:
      {
         // A conical bore as two sections either side of the reed, both
         // ends inverting, so every harmonic sounds (Scavone 1997).
         const float auxOut = ReadDelay(v.aux, v.w, v.d1);
         v.lp = (1.0f - pole) * boreOut + pole * v.lp;
         const float bell = -0.95f * v.lp;
         out = bell - auxOut;
         const float diff = p - out;
         const float r = std::clamp(0.7f + 0.3f * diff, -1.0f, 1.0f);
         v.aux[v.w] = bell;
         v.bore[v.w] = p - diff * r - bell;
         break;
      }
      case WindNode::kFlute:
      {
         // Air jet: the breath, less part of the bore's return, travels the
         // jet delay and is split by the edge (a cubic jet table).
         v.lp = (1.0f - pole) * boreOut + pole * v.lp;
         float back = -v.lp;
         const float dc = back - v.dcIn + mLoopDcR * v.dcOut;
         v.dcIn = back;
         v.dcOut = DspMath::FlushDenormal(dc);
         back = v.dcOut;
         const float jetOut = ReadDelay(v.aux, v.w, v.d1);
         v.aux[v.w] = p - 0.5f * back;
         const float jet = std::clamp(jetOut * (jetOut * jetOut - 1.0f), -1.0f, 1.0f);
         v.bore[v.w] = jet + 0.5f * back;
         out = 0.3f * boreOut;
         break;
      }
      default:
      {
         // Lips: a valve whose opening follows a resonator at the note,
         // driven by the pressure across it (Cook's swinging door).
         const float mouth = 0.3f * p;
         const float boreP = 0.85f * boreOut;
         float lip = v.lipG * (mouth - boreP) - v.lipA1 * v.lipY1 - v.lipA2 * v.lipY2;
         lip = DspMath::FlushDenormal(lip);
         v.lipY2 = v.lipY1;
         v.lipY1 = lip;
         const float open = std::min(1.0f, lip * lip);
         const float x = open * mouth + (1.0f - open) * boreP;
         const float dc = x - v.dcIn + mLoopDcR * v.dcOut;
         v.dcIn = x;
         v.dcOut = DspMath::FlushDenormal(dc);
         v.bore[v.w] = v.dcOut;
         out = boreOut;
         break;
      }
      }
      v.bore[v.w] = DspMath::FlushDenormal(v.bore[v.w]);
      v.aux[v.w] = DspMath::FlushDenormal(v.aux[v.w]);
      v.w = (v.w + 1) & kBufMask;
      return out;
   }

   void HandleEvent(const NoteEvent& e, int inst)
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
         NoteOn(e, inst);
         return;
      }
      for (auto& v : mVoices)
         if (v.active && v.held && v.note == e.note && (v.voiceId == e.voiceId || e.voiceId == 0))
            v.held = false;
   }

   void NoteOn(const NoteEvent& e, int inst)
   {
      // Same note keeps its sounding bore (a re-tongue); else a free voice;
      // else the oldest, cleared.
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
      WindVoice& v = mVoices[slot];
      if (!sameNote || v.instrument != inst)
         v.Clear();
      v.instrument = inst;
      v.active = true;
      v.held = true;
      v.note = e.note;
      v.voiceId = e.voiceId;
      v.velocity = std::clamp(e.velocity, 0.0f, 1.0f);
      v.bend = e.bendSemitones;
      v.age = mNextAge++;
      v.chunkLeft = 0;
      v.level = std::max(v.level, 1e-3f);
      v.tongue = 1.0f;
   }

   double mSampleRate = 44100.0;
   WindVoice mVoices[kMaxVoices];
   uint64_t mNextAge = 1;
   DspMath::Biquad mHighpass;
   DspMath::DcBlocker mDc;
   DspMath::WhiteNoise mNoise;
   double mVibPhase = 0.0;
   float mLoopDcR = 0.997f;
   float mTongueDecay = 0.9995f;

   ParamMailbox mMailbox;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   std::atomic<float> mAttack{ 30.0f };
   std::atomic<float> mRelease{ 80.0f };
   std::atomic<int> mInstrument{ WindNode::kClarinet };
   std::atomic<float> mClipPitch{ 0.0f };
   std::atomic<int> mActiveVoiceCount{ 0 };
   std::atomic<bool> mReleaseAll{ false };
};

// -------------------------------------------------------------- main thread
WindNode::WindNode() = default;
WindNode::~WindNode() = default;

AudioNode* WindNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWindNode>();
   return mAudioNode.get();
}

int WindNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void WindNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWindNode>();
   mAudioNode->PushParams(*this);
}

void WindNode::SweepPostAlter(const std::string& paramName, int& /*frameId*/)
{
   if (paramName == "release" && mAudioNode)
      mAudioNode->ReleaseAll();
}

void WindNode::VisitParams(ParamVisitor& v)
{
   v.Int("instrument", instrument);
   v.Float("breath", breath);
   v.Float("tone", tone);
   v.Float("noise", noise);
   v.Float("vibrato", vibrato);
   v.Float("attack", attack);
   v.Float("release", release);
   v.Float("volume", volume);
}
