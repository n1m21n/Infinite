#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Oversampler.h"
#include "audio/DspMath.h"

// Rhodes and Wurlitzer: a hammer on a steel bar, read by a nonlinear pickup.
// Both bars move as near-sinusoids; the pickup makes the timbre (Pfeifle &
// Muenster 2017: "the primary mechanical exciters are secondary"). So the
// model is small on the mechanical side and exact on the transducer:
//
//   hammer  Hunt-Crossley contact, F = k u^1.5 (1 + lambda du/dt), coupled to
//           the bar: a harder blow is shorter, so it reaches higher modes.
//   bar     four modes of the bar's tip (complex one-poles).
//           Rhodes (an asymmetric tuning fork, Falaize & Helie 2017): the tine,
//           the tone bar just beside it, the tine's first overtone (cantilever
//           6.27, raised to 7 by the tuning spring's mass at the tip) and the
//           tone bar's own (free bar, 2.76), louder up the keyboard. Overtones
//           die fast; the fundamental's Q is Shear 2011's measured table.
//           Wurlitzer: one mode dominates (Pfeifle 2017), so the overtones are
//           the cantilever's own and small.
//   pickup  Rhodes: Faraday, V = -d/dt flux. The flux at the tine's tip is a
//           bell in position, B ~ dz / |r|^3 (Pfeifle 2017), gap 1.5 mm and
//           0.5 mm off axis (Falaize & Helie's "ideal" voicing). Centred, it
//           doubles the pitch; off centre, soft notes are near sine and loud
//           ones bark (Shear 2011, Fig. 2.4).
//           Wurlitzer: i = u0 dC/dt. The capacitance peaks with the reed at
//           rest and falls differently on each side, the reed being
//           non-symmetric (Pfeifle & Muenster 2017, Fig. 9).
//           Differencing the flux or charge per sample is the exact integral
//           of dF/dx dx/dt over that sample, so the pickup is its own
//           first-order antiderivative antialiasing.
//   amp     shared by every note: a high-pass, an asymmetric soft clip (the
//           Wurlitzer's preamp barks; a Rhodes preamp barely does), a speaker
//           (body), and the tremolo. Voiced, not measured.
//
// Everything before the speaker runs at 2x. Up to 16 notes at four modes each
// cost less than one voice of the waveguide piano.
namespace ElectricPiano
{
   enum Model
   {
      kRhodes = 1,
      kWurlitzer = 2,
   };

   struct Knobs
   {
      float hardness = 0.5f, tone = 0.5f, decay = 0.5f, detune = 0.3f, bell = 0.5f, release = 250.0f;
      float clipSemis = 0.0f;
   };

   class Engine
   {
   public:
      static constexpr int kMaxVoices = 16;

      void Prepare(double sampleRate)
      {
         mSr = sampleRate;
         mOs = 2.0 * sampleRate;
         mDown.Prepare(2, 32);
         SetModel(mModel);
         Reset();
      }

      void Reset()
      {
         for (auto& v : mVoices)
            v.active = false;
         mDown.Reset();
         mHp = mHpIn = 0.0f;
         for (auto& b : mCab)
            b.x1 = b.x2 = b.y1 = b.y2 = 0.0f;
         mLfo = 0.0;
         mNextAge = 1;
      }

      void SetModel(int model)
      {
         mModel = model == kWurlitzer ? kWurlitzer : kRhodes;
         const bool w = mModel == kWurlitzer;
         mHpR = (float)std::exp(-2.0 * M_PI * (w ? 90.0 : 40.0) / mSr);
         mPreDrive = w ? 1.6f : 0.5f;
         mPreBias = w ? 0.25f : 0.08f;
         mPreRest = std::tanh(mPreBias);
         mOutGain = w ? 1.6f : 0.65f;
         mLfoInc = 2.0 * M_PI * (w ? 5.63 : 4.8) / mSr;
         mCab[0] = Biquad::HighPass(w ? 100.0 : 75.0, 0.8, mSr);
         mCab[1] = Biquad::Peak(w ? 1200.0 : 2600.0, 1.0, w ? 5.0 : 2.5, mSr);
         mCab[2] = Biquad::LowPass(w ? 4500.0 : 6000.0, 0.8, mSr);
      }

      void SetKnobs(const Knobs& k) { mK = k; }

      void NoteOn(int note, float velocity, int voiceId, float bend)
      {
         int slot = -1;
         for (int k = 0; k < kMaxVoices && slot < 0; k++)
            if (mVoices[k].active && mVoices[k].note == note)
               slot = k;
         const bool sameNote = slot >= 0;
         for (int k = 0; k < kMaxVoices && slot < 0; k++)
            if (!mVoices[k].active)
               slot = k;
         if (slot < 0)
         {
            float quietest = 1e30f;
            uint64_t oldest = UINT64_MAX;
            for (int k = 0; k < kMaxVoices; k++)
               if (mVoices[k].held ? false : mVoices[k].level < quietest)
               {
                  quietest = mVoices[k].level;
                  slot = k;
               }
            for (int k = 0; k < kMaxVoices && slot < 0; k++)
               if (mVoices[k].age < oldest)
                  oldest = mVoices[k].age;
            for (int k = 0; k < kMaxVoices && slot < 0; k++)
               if (mVoices[k].age == oldest)
                  slot = k;
         }
         Voice& v = mVoices[slot];
         if (!sameNote)
         {
            for (auto& m : v.modes)
               m.re = m.im = 0.0;
            v.y = 0.0;
            v.level = 0.0f;
         }
         v.active = v.held = true;
         v.note = std::clamp(note, 0, 127);
         v.voiceId = voiceId;
         v.velocity = std::clamp(velocity, 0.0f, 1.0f);
         v.bend = bend;
         v.age = mNextAge++;
         Retune(v);
         v.yPrev = v.y;
         Strike(v);
      }

      void NoteOff(int note, int voiceId)
      {
         for (auto& v : mVoices)
            if (v.active && v.held && v.note == note && (v.voiceId == voiceId || voiceId == 0))
            {
               v.held = false;
               v.chunkLeft = 0;
            }
      }

      void Bend(int voiceId, float semitones)
      {
         for (auto& v : mVoices)
            if (v.active && v.voiceId == voiceId)
               v.bend = semitones;
      }

      void ReleaseAll()
      {
         for (auto& v : mVoices)
            v.held = false;
      }

      void RestrikeAll()
      {
         for (auto& v : mVoices)
            if (v.active)
               Strike(v);
      }

      // One base-rate sample. body 0 the amp's direct out .. 1 its speaker.
      void Tick(float body, float tremolo, float& outL, float& outR)
      {
         float os[2];
         os[0] = Preamp(VoicesTick());
         os[1] = Preamp(VoicesTick());
         const float x = mDown.Down(0, os);
         mHp = DspMath::FlushDenormal(x - mHpIn + mHpR * mHp);
         mHpIn = x;
         float cab = mHp;
         for (auto& b : mCab)
            cab = b.Process(cab);
         const float o = ((1.0f - body) * mHp + body * cab) * mOutGain;

         mLfo += mLfoInc;
         if (mLfo > 2.0 * M_PI)
            mLfo -= 2.0 * M_PI;
         const float s = (float)std::sin(mLfo);
         if (mModel == kWurlitzer)
         {
            // Amplitude only: the 200A's LDR pulls the preamp's gain down.
            const float g = 1.0f - tremolo * 0.5f * (1.0f + s);
            outL = outR = o * g;
         }
         else
         {
            // The Suitcase's stereo vibrato is a pan: as one side falls the other rises.
            outL = o * (1.0f - tremolo * 0.5f * (1.0f + s));
            outR = o * (1.0f - tremolo * 0.5f * (1.0f - s));
         }
      }

      int ActiveVoices() const
      {
         int count = 0;
         for (const auto& v : mVoices)
            count += v.active ? 1 : 0;
         return count;
      }

   private:
      static constexpr int kChunk = 64;
      static constexpr int kModes = 4;

      struct Biquad
      {
         float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

         float Process(float x)
         {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = DspMath::FlushDenormal(y);
            return y;
         }

         // RBJ cookbook.
         static Biquad Make(double b0, double b1, double b2, double a0, double a1, double a2)
         {
            Biquad q;
            q.b0 = (float)(b0 / a0);
            q.b1 = (float)(b1 / a0);
            q.b2 = (float)(b2 / a0);
            q.a1 = (float)(a1 / a0);
            q.a2 = (float)(a2 / a0);
            return q;
         }
         static Biquad LowPass(double f, double Q, double sr)
         {
            const double w = 2.0 * M_PI * std::min(f, 0.45 * sr) / sr, c = std::cos(w), al = std::sin(w) / (2.0 * Q);
            return Make((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
         }
         static Biquad HighPass(double f, double Q, double sr)
         {
            const double w = 2.0 * M_PI * f / sr, c = std::cos(w), al = std::sin(w) / (2.0 * Q);
            return Make((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
         }
         static Biquad Peak(double f, double Q, double db, double sr)
         {
            const double A = std::pow(10.0, db / 40.0), w = 2.0 * M_PI * f / sr, c = std::cos(w), al = std::sin(w) / (2.0 * Q);
            return Make(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
         }
      };

      struct Mode
      {
         double re = 0.0, im = 0.0;      // im: the mode's displacement in mm
         double c = 1.0, s = 0.0;        // pole
         double w = 1.0;                 // rad/s
         double shape = 0.0;             // the mode's share at the tip
      };

      struct Voice
      {
         bool active = false, held = false;
         int note = -1, voiceId = 0;
         float velocity = 0.0f, bend = 0.0f, level = 0.0f;
         uint64_t age = 0;
         int chunkLeft = 0;
         Mode modes[kModes];
         double y = 0.0, yPrev = 0.0; // yPrev: tip position one sample ago, for the pickup's difference
         // Hammer.
         bool contact = false, struck = true;
         double yh = 0.0, vh = 0.0, k = 0.0, lambda = 0.0, mass = 1.5;
      };

      static double Lerp(double a, double b, double t) { return a + (b - a) * t; }

      double NoteHz(const Voice& v) const
      {
         const double f = 440.0 * std::exp2(((double)v.note - 69.0 + (double)v.bend + (double)mK.clipSemis) / 12.0);
         return std::clamp(f, 16.0, mSr / 8.0);
      }

      // T60 of the fundamental in seconds. Rhodes: Shear 2011's Q of a Mark I
      // (Eb2 949, Eb3 731, Eb4 1520, Eb5 2175, Eb6 1761), tau = Q / (pi f).
      // Wurlitzer: a reed loses more, voiced by ear; held to 8 s at the bottom.
      double FundamentalT60(const Voice& v, double f) const
      {
         double q;
         if (mModel == kWurlitzer)
            q = 350.0 * std::sqrt(f / 261.6);
         else
         {
            static const double kQ[] = { 949.0, 731.0, 1520.0, 2175.0, 1761.0 };
            const double oct = std::clamp((v.note - 39) / 12.0, 0.0, 3.999);
            const int i = (int)oct;
            q = Lerp(kQ[i], kQ[i + 1], oct - i);
         }
         double t = 6.9078 * q / (M_PI * f);
         if (mModel == kWurlitzer)
            t = std::min(t, 8.0);
         return t * std::exp2(4.0 * ((double)mK.decay - 0.5));
      }

      void Retune(Voice& v)
      {
         const double f0 = NoteHz(v);
         const double t0 = FundamentalT60(v, f0);
         const bool w = mModel == kWurlitzer;
         const double up = std::clamp((v.note - 48) / 48.0, 0.0, 1.0);
         // Mistuning the fork's two prongs loses Q (Shear 2011): detune trades sustain for beating.
         const double det = (double)mK.detune;
         const double bell = (double)mK.bell;
         const double hz[kModes] = { f0, f0 * (1.0 + 0.006 * det), f0 * (w ? 6.27 : 7.0), f0 * (w ? 17.55 : 2.756) };
         const double t60[kModes] = { t0 / (1.0 + 2.0 * det), t0 / (1.0 + 2.0 * det),
                                      (w ? 0.15 : 0.4) * std::sqrt(261.6 / f0), (w ? 0.06 : 0.25) * std::sqrt(261.6 / f0) };
         const double shape[kModes] = { 1.0, w ? 0.2 : 0.3, bell * (w ? 0.8 : 2.0), bell * (w ? 0.5 : Lerp(0.2, 0.8, up)) };
         const double damper = 0.001 * std::max(10.0, (double)mK.release);
         for (int m = 0; m < kModes; m++)
         {
            Mode& d = v.modes[m];
            double t = t60[m];
            if (!v.held)
               t = std::min(t, damper);
            const double r = std::exp(-6.9078 / (t * mOs));
            const double th = 2.0 * M_PI * hz[m] / mOs;
            d.c = r * std::cos(th);
            d.s = r * std::sin(th);
            d.w = 2.0 * M_PI * hz[m];
            d.shape = hz[m] < 0.45 * mOs ? shape[m] : 0.0;
         }
         v.chunkLeft = kChunk;
      }

      // Contact time from the note: short against the period, as the tip is
      // graded harder up the keyboard; hardness halves or doubles it. The
      // stiffness that gives it at a mezzo-forte blow; a harder blow is shorter.
      void Strike(Voice& v)
      {
         const double f0 = NoteHz(v);
         const bool w = mModel == kWurlitzer;
         const double tc = std::clamp((w ? 0.35 : 0.26) / f0, w ? 0.0006 : 0.0004, w ? 0.005 : 0.003)
                           * std::exp2(1.0 - 2.0 * (double)mK.hardness);
         // Displacement in mm: a tine or reed takes the blow's momentum, so it swings v / w,
         // falling with pitch, to a limit in the bass.
         const double swing = (w ? 2.2 : 2.0) * std::pow(261.6 / f0, 0.9);
         const double Y = std::min(2.4, swing) * (0.1 + 0.9 * std::pow((double)v.velocity, 1.5));
         const double v0 = 2.0 * M_PI * f0 * Y;
         const double vRef = 2.0 * M_PI * f0 * std::min(2.4, swing) * 0.65;
         const double mu = v.mass / (1.0 + v.mass);
         const double kLin = mu * (M_PI / tc) * (M_PI / tc);
         v.k = kLin / std::sqrt(std::max(1e-6, vRef * tc / M_PI));
         v.lambda = 0.6 / std::max(1e-6, vRef);
         v.yh = v.y;
         v.vh = v0;
         v.contact = false;
         v.struck = false;
      }

      // Position in mm -> the pickup's flux (Rhodes) or capacitance (Wurlitzer).
      double Pickup(const Voice&, double y) const
      {
         if (mModel == kWurlitzer)
         {
            const double w = 1.2 * std::exp2(1.0 - 2.0 * (double)mK.tone);
            const double s = (y + 0.45 * w) / w;
            const double e = s * (1.0 + 0.35 * s / std::sqrt(1.0 + s * s));
            return w / (1.0 + e * e);
         }
         const double d = 1.5 * std::exp2(1.0 - 2.0 * (double)mK.tone);
         const double s = (y + d / 3.0) / d;
         const double r2 = 1.0 + s * s;
         return d / (r2 * std::sqrt(r2));
      }

      float VoicesTick()
      {
         const double dt = 1.0 / mOs;
         float sum = 0.0f;
         for (auto& v : mVoices)
         {
            if (!v.active)
               continue;
            if (v.chunkLeft-- <= 0)
               Retune(v);

            double force = 0.0;
            if (!v.struck)
            {
               const double u = v.yh - v.y;
               if (u > 0.0)
               {
                  v.contact = true;
                  double dv = v.vh;
                  for (const auto& m : v.modes)
                     dv -= m.shape * m.re * m.w;
                  force = std::max(0.0, v.k * u * std::sqrt(u) * (1.0 + v.lambda * dv));
               }
               else if (v.contact)
                  v.struck = true; // off the bar: it never comes back
               v.vh -= force / v.mass * dt;
               v.yh += v.vh * dt;
            }

            double y = 0.0;
            for (auto& m : v.modes)
            {
               if (m.shape == 0.0)
                  continue;
               const double re = m.c * m.re - m.s * m.im + force * m.shape * dt / m.w;
               m.im = m.s * m.re + m.c * m.im;
               m.re = re;
               y += m.shape * m.im;
            }
            v.y = y;
            // Both flux values use the pickup's current geometry: differencing against a value taken under the old
            // `pickup` knob setting put a step of (new - old) flux into every sample where the knob moved, a click.
            const double p = Pickup(v, y);
            // dF/dt, in per-octave-free units: 1 mm at 261.6 Hz through unit slope is 1.
            const float out = (float)((p - Pickup(v, v.yPrev)) * mOs / (2.0 * M_PI * 261.6));
            v.yPrev = y;
            v.level = std::max(std::fabs(out), v.level * 0.9998f);
            if (v.struck && v.level < 1e-6f)
               v.active = false;
            sum += out;
         }
         return sum;
      }

      float Preamp(float x) const
      {
         const float d = x * mPreDrive + mPreBias;
         return (std::tanh(d) - mPreRest) / mPreDrive;
      }

      double mSr = 48000.0, mOs = 96000.0;
      int mModel = kRhodes;
      Knobs mK;
      Voice mVoices[kMaxVoices];
      uint64_t mNextAge = 1;
      Oversampler mDown;
      Biquad mCab[3];
      float mHp = 0.0f, mHpIn = 0.0f, mHpR = 0.99f;
      float mPreDrive = 1.0f, mPreBias = 0.0f, mPreRest = 0.0f, mOutGain = 1.0f;
      double mLfo = 0.0, mLfoInc = 0.0;
   };
}
