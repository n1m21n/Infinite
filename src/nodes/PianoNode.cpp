#include "PianoNode.h"

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
#include "audio/dsp/DenseBody.h"
#include "audio/dsp/ElectricPiano.h"

namespace
{
   enum SmoothedParam
   {
      kParamBody = 0,
      kParamVolume,
      kParamTremolo,
   };

   // One string's loop, long enough for A0 at 192 kHz.
   constexpr int kStrBuf = 8192;
   constexpr int kStrMask = kStrBuf - 1;
   // The hammer's force history, for the reflection off the near end.
   constexpr int kForceBuf = 1024;
   constexpr int kForceMask = kForceBuf - 1;
   // Up to three unison strings and the second polarisation.
   constexpr int kStrings = 4;
   constexpr int kDispersion = 4; // first-order dispersion allpasses per string
   constexpr int kChunk = 32;     // loss, tuning and detune re-solved this often
   constexpr int kUndamped = 89;  // F6 and up have no dampers

   // Hammer and string, in SI units (Chaigne & Askenfelt 1994's ranges);
   // trimmed by tools/universe/piano_proto.py, which is this voice in Python.
   constexpr double kHammerK = 8e9;      // felt stiffness at C4, hardness 0.5
   constexpr double kHammerP = 2.5;      // felt exponent at A0, rising 0.012 per key
   constexpr double kHammerMass = 0.005; // kg at C4
   constexpr double kHammerSpeed = 2.5;  // m/s at full velocity
   constexpr double kSlope = 18.0;       // keys per doubling of K and halving of mass
   constexpr double kTreble = 8.0;       // above C5, both again: contact well under a period
   constexpr double kStringZ = 4.0;      // string impedance, kg/s
   constexpr double kStrikePos = 0.125;  // hammer at 1/8 of the string
   constexpr double kOutGain = 0.7;
   constexpr double kTilt = 0.45; // dB per key: long bass strings put out far more velocity

   // Soundboard: a dense modal bank (DenseBody.h). A ribbed spruce plate has a
   // nearly constant modal density and a loss factor around 0.02 (Suzuki 1986;
   // Ege, Boutillon & Rebillat 2013), so T60 falls as 1/f. No signature modes:
   // a board has no air mode to be known by.
   DenseBody::Spec BoardSpec()
   {
      DenseBody::Spec s;
      s.loHz = 45.0f; s.hiHz = 6000.0f; s.modes = 224; s.density = 0.0f; s.eta = 0.02f;
      s.rollHz = 1000.0f; s.rollDbPerOct = 4.0f; s.lowCutHz = 60.0f; s.stereo = 0.7f; s.seed = 88;
      return s;
   }
   // A board's response is a smooth mean (its characteristic admittance) with
   // the modes on top. Low down a mode is ~2 Hz wide and ~27 Hz from the next,
   // so modes alone miss most sustained partials: the direct share is that mean.
   constexpr float kBoardDirect = 0.65f;
   constexpr float kBoardGain = 0.45f; // the modes' level against dry

   // Phase delays in samples at w radians per sample: the first-order allpass
   // (a + z^-1) / (1 + a z^-1) and the one-pole lowpass (1 - a) / (1 - a z^-1).
   double AllpassDelay(double a, double w)
   {
      return (std::atan2(std::sin(w), a + std::cos(w)) - std::atan2(a * std::sin(w), 1.0 + a * std::cos(w))) / w;
   }

   double OnePoleDelay(double a, double w)
   {
      return std::atan2(a * std::sin(w), 1.0 - a * std::cos(w)) / w;
   }

   // Inharmonicity coefficient B: rises about 1.2x per octave up the keyboard
   // (Fletcher's measurements on grands), scaled by stiffness (0.5 = a grand).
   double Inharmonicity(int note, double stiffness)
   {
      return std::clamp(3e-4 * std::exp2(1.2 * (note - 60) / 12.0), 1.2e-4, 2e-2) * 2.0 * stiffness;
   }

   // T60 of the fundamental while held: 25 s at A0, halving every 18 keys;
   // decay 0..1 scales it by 1/4..4.
   double SustainT60(int note, double decay)
   {
      return 25.0 * std::exp2(-(note - 21) / 18.0) * std::exp2(4.0 * (decay - 0.5));
   }

   // One-pole loss g (1 - a) / (1 - a z^-1) per pass of a loop at f: T60 is T
   // at f and T * 10^(-1.4 (1 - tone)) at fh. Two-point design in closed form
   // (the ratio of |H|^2 at the two frequencies is a quadratic in a).
   void LossFilter(double f, double T, double tone, double sr, float& g, float& a)
   {
      const double P = sr / f;
      const double fh = std::min(0.45 * sr, std::max(6000.0, 2.5 * f));
      const double Th = T * std::pow(10.0, -1.4 * (1.0 - tone));
      const double g0 = std::pow(10.0, -3.0 * P / (T * sr));
      const double gh = std::pow(10.0, -3.0 * P / (Th * sr));
      const double R = (gh / g0) * (gh / g0);
      const double c0 = std::cos(2.0 * M_PI * f / sr), ch = std::cos(2.0 * M_PI * fh / sr);
      const double b = (R * ch - c0) / (1.0 - R);
      double pole = b < -1.0 ? -b - std::sqrt(b * b - 1.0) : 0.0;
      pole = std::clamp(pole, 0.0, 0.95);
      const double h0 = (1.0 - pole) / std::sqrt(1.0 - 2.0 * pole * c0 + pole * pole);
      g = (float)std::min(0.9999, g0 / h0);
      a = (float)pole;
   }

   // The dispersion allpass coefficient that puts the loop's partials on the
   // stiff string's k f0 sqrt(1 + B k^2) (golden-section search on the phase
   // error of partials 2..8; f0 here is the string's ideal, B-free one).
   float FitDispersion(double f0, double B, double lossPole, double sr)
   {
      const int K = std::max(2, std::min(8, (int)(0.45 * sr / f0 / std::sqrt(1.0 + B * 64.0))));
      const double w1 = 2.0 * M_PI * f0 * std::sqrt(1.0 + B) / sr;
      auto err = [&](double a) {
         const double L = 2.0 * M_PI / w1 - kDispersion * AllpassDelay(a, w1) - OnePoleDelay(lossPole, w1);
         if (L < 4.0)
            return 1e9 + (4.0 - L);
         double e = 0.0;
         for (int k = 2; k <= K; k++)
         {
            const double fk = k * f0 * std::sqrt(1.0 + B * k * k);
            const double wk = 2.0 * M_PI * fk / sr;
            if (wk > 3.0)
               break;
            const double tau = L + kDispersion * AllpassDelay(a, wk) + OnePoleDelay(lossPole, wk);
            const double cyc = tau * fk / sr - k;
            e += cyc * cyc;
         }
         return e;
      };
      double lo = -0.95, hi = 0.0;
      const double gr = 0.6180339887;
      for (int it = 0; it < 24; it++)
      {
         const double m1 = hi - gr * (hi - lo), m2 = lo + gr * (hi - lo);
         if (err(m1) < err(m2))
            hi = m2;
         else
            lo = m1;
      }
      return (float)(0.5 * (lo + hi));
   }

   struct PianoString
   {
      float buf[kStrBuf];
      int M = 100;      // integer part of the loop delay
      float eta = 0.0f; // Thiran allpass for the fraction
      float th1 = 0.0f, thy = 0.0f;
      float g = 0.999f, la = 0.2f, lp = 0.0f; // loss
      float apA = -0.2f;                      // dispersion coefficient
      float ap[kDispersion] = {};
      float mix = 0.0f;
   };

   struct PianoVoice
   {
      bool active = false;
      bool held = false;
      int note = -1;
      int voiceId = 0;
      float velocity = 0.0f;
      float bend = 0.0f;
      uint64_t age = 0;
      int chunkLeft = 0;
      int strings = 1; // unison strings; the polarisation is strings[strings]
      int w = 0;
      PianoString str[kStrings];
      float fitStiffness = -1.0f;
      float level = 0.0f;
      float gain = 0.0f;

      // Hammer.
      double yh = 0.0, vh = 0.0, ys = 0.0;
      double K = 0.0, p = 2.5, mass = 0.005;
      bool contact = false, struck = false;
      int sinceStrike = 0, tail = 0, D = 1;
      float force[kForceBuf];
      int fw = 0;

      void Clear()
      {
         for (auto& s : str)
         {
            std::memset(s.buf, 0, sizeof(s.buf));
            s.th1 = s.thy = s.lp = 0.0f;
            std::fill_n(s.ap, kDispersion, 0.0f);
         }
         w = 0;
         level = 0.0f;
      }
   };
}

// ------------------------------------------------------------- audio thread
class AudioPianoNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = PianoNode::kMaxVoices;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      mDcR = (float)std::exp(-2.0 * M_PI * 20.0 / mSampleRate);
      mBoard.Design(BoardSpec(), mSampleRate);
      mElectric.Prepare(mSampleRate);
      Reset();
   }

   void Reset() override
   {
      for (auto& v : mVoices)
         v.active = false;
      mBoard.Reset();
      mElectric.Reset();
      mDcIn[0] = mDcIn[1] = mDcOut[0] = mDcOut[1] = 0.0f;
      mNextAge = 1;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void SetClipPitchOverride(float semitones) override { mClipPitch.store(semitones, std::memory_order_relaxed); }

   int ActiveVoices() const { return mActiveVoiceCount.load(std::memory_order_relaxed); }

   // The param sweep holds one note: these let it hear a release, and hear a
   // control that only acts at the strike, on the next block that has one.
   void ReleaseAll() { mReleaseAll.store(true, std::memory_order_relaxed); }
   void RestrikeAll() { mRestrikeAll.store(true, std::memory_order_relaxed); }

   void PushParams(const PianoNode& n)
   {
      mMailbox.Push(kParamBody, n.body);
      mMailbox.Push(kParamVolume, n.volume);
      mMailbox.Push(kParamTremolo, n.tremolo);
      mModel.store(n.model, std::memory_order_relaxed);
      mHardness.store(n.hardness, std::memory_order_relaxed);
      mTone.store(n.tone, std::memory_order_relaxed);
      mDecay.store(n.decay, std::memory_order_relaxed);
      mDetune.store(n.detune, std::memory_order_relaxed);
      mStiffness.store(n.stiffness, std::memory_order_relaxed);
      mRelease.store(n.release, std::memory_order_relaxed);
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

      // The model is chosen between notes: a switch stops what is ringing.
      const int model = mModel.load(std::memory_order_relaxed);
      if (model != mPlayingModel)
      {
         mPlayingModel = model;
         Reset();
         if (model != PianoNode::kGrand)
            mElectric.SetModel(model);
      }
      if (model != PianoNode::kGrand)
      {
         ProcessElectric(outL, outR, output.numFrames);
         return;
      }

      bool any = false;
      for (const auto& v : mVoices)
         any = any || v.active;
      if (any && mReleaseAll.exchange(false, std::memory_order_relaxed))
         for (auto& v : mVoices)
            v.held = false;
      if (any && mRestrikeAll.exchange(false, std::memory_order_relaxed))
         for (auto& v : mVoices)
            if (v.active)
               Strike(v);

      NoteEvent evts[64];
      const int numEvts = mNoteInbox ? mNoteInbox->Pop(mNoteCursor, evts, 64) : 0;
      int nextEvt = 0;

      // A stiffness change re-fits the dispersion of one voice per block, so
      // a sweeping knob never costs more than a single fit.
      const float stiffness = mStiffness.load(std::memory_order_relaxed);
      for (int k = 0; k < kMaxVoices; k++)
      {
         PianoVoice& v = mVoices[(mFitCursor + k) % kMaxVoices];
         if (v.active && std::fabs(v.fitStiffness - stiffness) > 0.01f)
         {
            Fit(v, stiffness);
            mFitCursor = (mFitCursor + k + 1) % kMaxVoices;
            break;
         }
      }

      const double dt = 1.0 / mSampleRate;
      for (int i = 0; i < output.numFrames; i++)
      {
         while (nextEvt < numEvts && evts[nextEvt].frameOffset <= i)
            HandleEvent(evts[nextEvt++]);

         const float body = std::clamp(mMailbox.SmoothedValue(kParamBody), 0.0f, 1.0f);
         const float volume = mMailbox.SmoothedValue(kParamVolume);

         float dry = 0.0f;
         for (auto& v : mVoices)
         {
            if (!v.active)
               continue;
            if (v.chunkLeft <= 0)
            {
               Retune(v);
               v.chunkLeft = kChunk;
            }
            v.chunkLeft--;

            // Hammer against the string: felt force K c^p on the compression,
            // the string answering with impedance Z and the reflection off
            // the near end D samples later (the comb that notches the
            // harmonics the strike point is a node of).
            double exc = 0.0;
            if (!v.struck || v.tail > 0)
            {
               double F = 0.0;
               const double fd = v.force[(v.fw - v.D) & kForceMask];
               if (!v.struck)
               {
                  const double c = v.yh - v.ys;
                  F = c > 0.0 ? v.K * std::pow(c, v.p) : 0.0;
                  v.vh -= F / v.mass * dt;
                  v.yh += v.vh * dt;
                  v.ys += (F - fd) / (2.0 * kStringZ) * dt;
                  if (F > 0.0)
                     v.contact = true;
                  // Off the string, on its way back: the strike is over.
                  if (++v.sinceStrike > 32 && F == 0.0 && v.vh < 0.0 && v.contact)
                  {
                     v.struck = true;
                     v.tail = v.D;
                  }
               }
               else
                  v.tail--;
               v.force[v.fw] = (float)F;
               v.fw = (v.fw + 1) & kForceMask;
               exc = (F - fd) / (2.0 * kStringZ);
            }

            float sum = 0.0f;
            const int n = v.strings + 1;
            for (int k = 0; k < n; k++)
            {
               PianoString& s = v.str[k];
               const float xi = s.buf[(v.w - s.M) & kStrMask];
               const float x = s.eta * xi + s.th1 - s.eta * s.thy;
               s.th1 = xi;
               s.thy = DspMath::FlushDenormal(x);
               s.lp = DspMath::FlushDenormal(s.g * ((1.0f - s.la) * x + s.la * s.lp));
               float y = s.lp;
               for (int m = 0; m < kDispersion; m++)
               {
                  const float yo = s.apA * y + s.ap[m];
                  s.ap[m] = DspMath::FlushDenormal(y - s.apA * yo);
                  y = yo;
               }
               s.buf[v.w] = DspMath::FlushDenormal(y + (float)exc * s.mix);
               sum += x * s.mix;
            }
            v.w = (v.w + 1) & kStrMask;

            const float out = sum * v.gain;
            v.level = std::max(std::fabs(out), v.level * 0.9995f);
            if (v.struck && v.tail <= 0 && v.level < 1e-6f)
               v.active = false;
            dry += out;
         }

         float board[2];
         mBoard.Process(dry, board[0], board[1]);
         float ch[2];
         for (int c = 0; c < 2; c++)
         {
            const float o = (1.0f - body * (1.0f - kBoardDirect)) * dry + body * kBoardGain * board[c];
            mDcOut[c] = DspMath::FlushDenormal(o - mDcIn[c] + mDcR * mDcOut[c]);
            mDcIn[c] = o;
            ch[c] = mDcOut[c] * volume;
         }
         if (!std::isfinite(ch[0]) || !std::isfinite(ch[1]))
         {
            ch[0] = ch[1] = 0.0f;
            Reset();
         }
         outL[i] = std::clamp(ch[0], -4.0f, 4.0f);
         if (outR)
            outR[i] = std::clamp(ch[1], -4.0f, 4.0f);
      }
      while (nextEvt < numEvts)
         HandleEvent(evts[nextEvt++]);

      int count = 0;
      for (const auto& v : mVoices)
         count += v.active ? 1 : 0;
      mActiveVoiceCount.store(count, std::memory_order_relaxed);
   }

private:
   void ProcessElectric(float* outL, float* outR, int numFrames)
   {
      ElectricPiano::Knobs k;
      k.hardness = mHardness.load(std::memory_order_relaxed);
      k.tone = mTone.load(std::memory_order_relaxed);
      k.decay = mDecay.load(std::memory_order_relaxed);
      k.detune = mDetune.load(std::memory_order_relaxed);
      k.bell = mStiffness.load(std::memory_order_relaxed);
      k.release = mRelease.load(std::memory_order_relaxed);
      k.clipSemis = mClipPitch.load(std::memory_order_relaxed);
      mElectric.SetKnobs(k);
      if (mReleaseAll.exchange(false, std::memory_order_relaxed))
         mElectric.ReleaseAll();
      if (mRestrikeAll.exchange(false, std::memory_order_relaxed))
         mElectric.RestrikeAll();

      NoteEvent evts[64];
      const int numEvts = mNoteInbox ? mNoteInbox->Pop(mNoteCursor, evts, 64) : 0;
      int nextEvt = 0;
      auto handle = [&](const NoteEvent& e) {
         if (e.bendUpdate)
            mElectric.Bend(e.voiceId, e.bendSemitones);
         else if (e.isNoteOn && e.velocity > 0.0f)
            mElectric.NoteOn(e.note, e.velocity, e.voiceId, e.bendSemitones);
         else
            mElectric.NoteOff(e.note, e.voiceId);
      };
      for (int i = 0; i < numFrames; i++)
      {
         while (nextEvt < numEvts && evts[nextEvt].frameOffset <= i)
            handle(evts[nextEvt++]);
         const float body = std::clamp(mMailbox.SmoothedValue(kParamBody), 0.0f, 1.0f);
         const float volume = mMailbox.SmoothedValue(kParamVolume);
         const float tremolo = std::clamp(mMailbox.SmoothedValue(kParamTremolo), 0.0f, 1.0f);
         float l, r;
         mElectric.Tick(body, tremolo, l, r);
         l *= volume;
         r *= volume;
         if (!std::isfinite(l) || !std::isfinite(r))
         {
            l = r = 0.0f;
            mElectric.Reset();
         }
         outL[i] = std::clamp(l, -4.0f, 4.0f);
         if (outR)
            outR[i] = std::clamp(r, -4.0f, 4.0f);
      }
      while (nextEvt < numEvts)
         handle(evts[nextEvt++]);
      mActiveVoiceCount.store(mElectric.ActiveVoices(), std::memory_order_relaxed);
   }

   double NoteHz(const PianoVoice& v) const
   {
      const double clip = mClipPitch.load(std::memory_order_relaxed);
      const double f = 440.0 * std::exp2(((double)v.note - 69.0 + (double)v.bend + clip) / 12.0);
      return std::clamp(f, mSampleRate / (double)(kStrBuf - 64), mSampleRate / 8.0);
   }

   double StringCents(const PianoVoice& v, int k) const
   {
      if (k == v.strings)
         return 0.4; // the second polarisation, a hair sharp
      const double det = 6.0 * mDetune.load(std::memory_order_relaxed);
      if (v.strings == 1)
         return 0.0;
      if (v.strings == 2)
         return k == 0 ? -0.5 * det : 0.5 * det;
      return (k - 1) * det;
   }

   // Loss, then the loop delay that tunes the fundamental exactly given the
   // dispersion and loss filters' phase delays, split into integer + Thiran.
   void Retune(PianoVoice& v)
   {
      const double sr = mSampleRate;
      const double f0 = NoteHz(v);
      const double tone = std::clamp(mTone.load(std::memory_order_relaxed), 0.0f, 1.0f);
      double T = SustainT60(v.note, std::clamp(mDecay.load(std::memory_order_relaxed), 0.0f, 1.0f));
      for (int k = 0; k <= v.strings; k++)
      {
         PianoString& s = v.str[k];
         const double f = f0 * std::exp2(StringCents(v, k) / 1200.0);
         double Tk = k == v.strings ? 3.0 * T : T;
         if (!v.held && v.note < kUndamped)
            Tk = std::min(Tk, 0.001 * std::max(10.0f, mRelease.load(std::memory_order_relaxed)));
         LossFilter(f, Tk, tone, sr, s.g, s.la);
         const double w = 2.0 * M_PI * f / sr;
         const double L = std::max(4.0, sr / f - kDispersion * AllpassDelay(s.apA, w) - OnePoleDelay(s.la, w));
         // First-order Thiran allpass for what is left after the integer
         // delay; its phase delay drifts from d at higher w, so correct it at
         // the fundamental.
         const int M = (int)std::floor(L - 0.5);
         double d = L - M;
         for (int it = 0; it < 3; it++)
            d += (L - M) - AllpassDelay((1.0 - d) / (1.0 + d), w);
         s.M = std::clamp(M, 1, kStrBuf - 2);
         s.eta = (float)((1.0 - d) / (1.0 + d));
      }
   }

   void Fit(PianoVoice& v, float stiffness)
   {
      const double B = Inharmonicity(v.note, std::clamp(stiffness, 0.0f, 1.0f));
      const double f0 = NoteHz(v);
      for (int k = 0; k <= v.strings; k++)
         v.str[k].apA = FitDispersion(f0 / std::sqrt(1.0 + B), B, v.str[k].la, mSampleRate);
      v.fitStiffness = stiffness;
   }

   void Strike(PianoVoice& v)
   {
      const double hard = std::clamp(mHardness.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const double up = std::max(0, v.note - 72) / kTreble;
      v.K = kHammerK * std::exp2(4.0 * (hard - 0.5) + (v.note - 60) / kSlope + up);
      v.p = kHammerP + 0.012 * (v.note - 21);
      v.mass = kHammerMass * std::exp2(-(v.note - 60) / kSlope - up);
      v.yh = v.ys = 0.0;
      v.vh = kHammerSpeed * (0.2 + 1.8 * std::pow((double)v.velocity, 1.5));
      v.contact = v.struck = false;
      v.sinceStrike = v.tail = 0;
      v.D = std::clamp((int)std::lround(kStrikePos * mSampleRate / NoteHz(v)), 1, kForceBuf - 1);
      std::fill_n(v.force, kForceBuf, 0.0f);
      v.fw = 0;
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
            v.chunkLeft = 0; // dampers down now
         }
   }

   void NoteOn(const NoteEvent& e)
   {
      // The same key strikes its own still-ringing strings again; else a free
      // voice; else the quietest released one; else the oldest.
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
         float quietest = 1e30f;
         for (int k = 0; k < kMaxVoices; k++)
            if (!mVoices[k].held && mVoices[k].level < quietest)
            {
               quietest = mVoices[k].level;
               slot = k;
            }
      }
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
      PianoVoice& v = mVoices[slot];
      if (!sameNote)
         v.Clear();
      v.active = true;
      v.held = true;
      v.note = std::clamp(e.note, 0, 127);
      v.voiceId = e.voiceId;
      v.velocity = std::clamp(e.velocity, 0.0f, 1.0f);
      v.bend = e.bendSemitones;
      v.age = mNextAge++;
      v.strings = v.note < 33 ? 1 : (v.note < 50 ? 2 : 3);
      for (int k = 0; k <= v.strings; k++)
         v.str[k].mix = k == v.strings ? 0.08f : 1.0f / (float)v.strings;
      for (int k = v.strings + 1; k < kStrings; k++)
         v.str[k].mix = 0.0f;
      v.gain = (float)(kOutGain * std::pow(10.0, kTilt * (v.note - 60) / 20.0));
      v.level = std::max(v.level, 1e-3f);
      Retune(v); // loss first: the fit reads its pole
      Fit(v, mStiffness.load(std::memory_order_relaxed));
      Retune(v);
      v.chunkLeft = kChunk;
      Strike(v);
   }

   double mSampleRate = 44100.0;
   PianoVoice mVoices[kMaxVoices];
   uint64_t mNextAge = 1;
   int mFitCursor = 0;
   float mDcR = 0.997f, mDcIn[2] = {}, mDcOut[2] = {};
   DenseBody::Bank mBoard;
   ElectricPiano::Engine mElectric;
   int mPlayingModel = PianoNode::kGrand;

   ParamMailbox mMailbox;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   std::atomic<float> mHardness{ 0.5f };
   std::atomic<float> mTone{ 0.5f };
   std::atomic<float> mDecay{ 0.5f };
   std::atomic<float> mDetune{ 0.3f };
   std::atomic<float> mStiffness{ 0.5f };
   std::atomic<float> mRelease{ 250.0f };
   std::atomic<int> mModel{ PianoNode::kGrand };
   std::atomic<float> mClipPitch{ 0.0f };
   std::atomic<int> mActiveVoiceCount{ 0 };
   std::atomic<bool> mReleaseAll{ false };
   std::atomic<bool> mRestrikeAll{ false };
};

// -------------------------------------------------------------- main thread
PianoNode::PianoNode() = default;
PianoNode::~PianoNode() = default;

AudioNode* PianoNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPianoNode>();
   return mAudioNode.get();
}

int PianoNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void PianoNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPianoNode>();
   mAudioNode->PushParams(*this);
}

void PianoNode::SweepPostAlter(const std::string& paramName, int& /*frameId*/)
{
   if (!mAudioNode)
      return;
   if (paramName == "release")
      mAudioNode->ReleaseAll();
   else if (paramName == "hardness")
      mAudioNode->RestrikeAll();
}

const std::vector<std::string>& PianoNode::ModelNames()
{
   static const std::vector<std::string> kNames = { "grand", "rhodes", "wurlitzer" };
   return kNames;
}

void PianoNode::VisitParams(ParamVisitor& v)
{
   v.Int("model", model);
   v.Float("hardness", hardness);
   v.Float("tone", tone);
   v.Float("decay", decay);
   v.Float("detune", detune);
   v.Float("stiffness", stiffness);
   v.Float("body", body);
   v.Float("release", release);
   v.Float("tremolo", tremolo);
   v.Float("volume", volume);
}
