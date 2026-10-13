// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
//
// Float port of punch.c from SLOOP / Felucca (GPL-3.0-only). The firmware
// runs in fixed point at 44.1 kHz on the whole mix; here every time constant
// is scaled to the engine's sample rate and the node processes whatever is
// cabled into it. Each effect runs in its own engine (own ring, own state)
// and the engines are chained in table order, so any number can be held at
// once. FREEZE is new here (the firmware's held a reverb it does not have):
// a granular hold of the last quarter second.

#include "PunchFxNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"

namespace
{
   enum Fx
   {
      PX_LOOP4, PX_LOOP8, PX_LOOP16, PX_LOOP32, PX_STUT, PX_REV, PX_STOP, PX_HALF,
      PX_LPF, PX_HPF, PX_TEL, PX_CRUSH, PX_DOWN, PX_GATE, PX_ECHO, PX_WOBBLE,
      PX_KLOW, PX_KMID, PX_KHIGH, PX_RWD, PX_OCT, PX_FLANGE, PX_PAN, PX_LOOP8T, PX_MONO, PX_FADE,
      PX_FREEZE,
      PX_COUNT
   };
   static_assert(PX_COUNT == PunchFxNode::kNumEffects, "effect table");

   const char* const kNames[PX_COUNT] = {
      "Loop 1/4", "Loop 1/8", "Loop 1/16", "Loop 1/32", "Stutter", "Reverse", "Tape stop", "Half speed",
      "Low pass", "High pass", "Phone", "Crush", "Alias", "Gate", "Echo", "Wobble",
      "Kill low", "Kill mid", "Kill high", "Rewind", "Octave up", "Flanger", "Auto-pan", "Loop 1/8T", "Mono",
      "Fade out", "Freeze" };

   constexpr uint32_t kRing = 65536; // power of two, 1.36 s at 48 kHz
   constexpr uint32_t kMask = kRing - 1;
   constexpr double kTwoPi = 6.283185307179586;

   bool IsLoop(int fx) { return fx <= PX_STUT || fx == PX_LOOP8T; }
   bool Replays(int fx) { return fx <= PX_HALF || fx == PX_LOOP8T; }
   float NoteHz(float note) { return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); }
}

// One effect, its own ring and state. want = its index while held, -1 when not.
class PunchEngine
{
public:
   PunchEngine() : mRing(kRing, 0.0f) {}

   void Prepare(double sampleRate)
   {
      mSr = sampleRate > 0.0 ? sampleRate : 48000.0;
      mScale = (float)(mSr / 44100.0);
      for (auto& f : mF) f.SetSampleRate(mSr);
      for (auto& k : mKill) { k.SetSampleRate(mSr); k.Reset(); }
      std::fill(mRing.begin(), mRing.end(), 0.0f);
      mCur = -1;
      mG = 0.0f;
      mFrozen.assign((size_t)(0.25 * mSr), 0.0f);
   }

   bool Busy() const { return mCur >= 0; }

   void Process(float* L, float* R, int n, int fx, bool held)
   {
      const int want = held ? fx : -1;
      if (mCur < 0 && want < 0)
      {
         for (int i = 0; i < n; ++i)
            mRing[mW++ & kMask] = 0.5f * (L[i] + R[i]);
         return;
      }
      if (mCur < 0)
      {
         Start(fx);
         if (fx == PX_FREEZE)
            CaptureFreeze();
      }
      if (fx == PX_FREEZE)
         ProcessFreeze(L, R, n, want);
      else
         ProcessFx(L, R, n, want);
   }

private:
   double BeatSamples() const
   {
      const double bpm = std::max(20.0, (double)Transport::Instance().Tempo());
      return mSr * 60.0 / bpm;
   }

   // Samples since the start of the current beat (transport running).
   uint32_t PhaseInBeat(double beat) const
   {
      const double beats = Transport::Instance().Beats();
      const double frac = beats - std::floor(beats);
      return (uint32_t)(frac * beat);
   }

   float Ring(uint32_t i) const { return mRing[i & kMask]; }
   float RingAt(double pos) const
   {
      const double f = std::floor(pos);
      const uint32_t i = (uint32_t)(int64_t)f;
      const float t = (float)(pos - f);
      const float a = Ring(i), b = Ring(i + 1u);
      return a + (b - a) * t;
   }

   void Start(int fx)
   {
      static const uint8_t kDiv[8] = { 1, 2, 4, 8, 6, 1, 1, 2 };
      const double beat = BeatSamples();
      const bool playing = Transport::Instance().IsPlaying();
      const uint32_t ph = playing ? PhaseInBeat(beat) : 0u;
      mCur = fx;
      mT = 0;
      double len = fx <= PX_HALF ? beat / kDiv[fx] : fx == PX_LOOP8T ? beat / 3.0 : beat;
      mLen = (uint32_t)std::clamp(len, 64.0, (double)(kRing - 2048));
      mPre = 0;
      mStart = mW;
      if (IsLoop(fx) && playing)
      {
         const uint32_t back = ph % mLen;
         if (mLen - back <= (uint32_t)(1536 * mScale))
            mPre = mLen - back; // just before a division: start on it
         else
         {
            mStart = mW - back; // else from the division that just went by
            mT = back;
         }
      }
      mRp = (double)mW;
      mSpd = 1.0;
      mDspd = 1.0 / (double)mLen;
      mSub = 0;
      mK = mT % mLen;
      mLfo = 0.0;
      mHold = 0;
      mCut = 127.0f;
      for (auto& f : mF) f.Reset();
      for (auto& k : mKill) k.Reset();
      mV = 1.0;
      mFade = 1.0f;
      mGp = 0;
      if (fx == PX_PAN && playing)
         mLfo = (double)ph / beat;
      if (fx == PX_FLANGE)
         mLfo = 0.75;
      if (fx == PX_OCT)
         mK = 0;
   }

   // Mono ring effects (loops, half, reverse, tape stop, wobble).
   float RingFx(int fx)
   {
      const uint32_t len = mLen, k = mK;
      float y;
      switch (fx)
      {
         case PX_REV:
            y = Ring(mStart - k);
            break;
         case PX_STOP:
         {
            y = RingAt(mRp);
            mRp += mSpd;
            mSpd = mSpd > mDspd ? mSpd - mDspd : 0.0;
            return y * (float)mSpd;
         }
         case PX_WOBBLE:
         {
            const double d = (700.0 + std::sin(kTwoPi * mLfo) * 180.0) * mScale;
            mLfo += 4.0 / mSr;
            if (mLfo >= 1.0) mLfo -= 1.0;
            return RingAt((double)mW - d);
         }
         default:
            y = Ring(mStart + k);
            break;
      }
      if (fx != PX_HALF || (mSub ^= 1u) == 0u)
         mK = k + 1u >= len ? 0u : k + 1u;
      if (k < 64u) return y * (float)k / 64.0f;               // declick at the seams
      if (len - k < 64u) return y * (float)(len - k) / 64.0f;
      return y;
   }

   bool Owns(int fx, uint32_t w) const
   {
      if (fx < 0 || !Replays(fx) || fx == PX_STOP || mPre)
         return false;
      const uint32_t d = w - mStart, len = mLen;
      if (fx == PX_REV)
         return d >= 1u && ((d + len - 1u) & kMask) < len;
      return d >= len && (d & kMask) < len;
   }

   void ProcessFx(float* L, float* R, int n, int want)
   {
      const double beat = BeatSamples();
      const bool playing = Transport::Instance().IsPlaying();
      const uint32_t step = std::max<uint32_t>(1u, (uint32_t)(beat / 4.0));
      const uint32_t echoD = std::min<uint32_t>((uint32_t)(beat * 0.75), kRing - 64u);
      const double turnPerSample = 1.0 / beat;
      const float fadeStep = (float)(1.0 / (2.0 * beat));
      const float gStep = 1.0f / (64.0f * mScale);
      const uint32_t holdN = std::max<uint32_t>(1u, (uint32_t)std::lround(8.0 * mScale));
      const uint32_t octW = std::max<uint32_t>(256u, (uint32_t)(2048.0 * mScale)) & ~1u;
      const double rwdAcc = 4.0 / (0.35 * mSr);
      const int fx0 = mCur;
      if (fx0 == PX_GATE)
         mGp = (playing ? PhaseInBeat(beat) : mT) % step;
      if (fx0 == PX_KLOW || fx0 == PX_KMID || fx0 == PX_KHIGH)
      {
         for (int i = 0; i < 4; ++i) { mKill[i].SetCutoff(251.0f, 0.7071f); mKill[i + 8].SetCutoff(251.0f, 0.7071f); }
         for (int i = 4; i < 8; ++i) { mKill[i].SetCutoff(2440.0f, 0.7071f); mKill[i + 8].SetCutoff(2440.0f, 0.7071f); }
      }
      if (fx0 == PX_TEL)
      {
         mF[0].SetCutoff(2500.0f, 0.7071f);
         mF[2].SetCutoff(500.0f, 0.7071f);
      }

      for (int i = 0; i < n; ++i)
      {
         const float x = L[i], y = R[i], m = 0.5f * (x + y);
         float wl = x, wr = y;
         const int fx = mCur;
         const float target = want == fx ? 1.0f : 0.0f;
         if (!Owns(fx, mW))
            mRing[mW & kMask] = m;
         switch (fx)
         {
            case PX_LPF:
            case PX_HPF:
            {
               if (mG > 0.0f)
                  mCut = fx == PX_LPF ? std::max(34.0f, mCut - 93.0f / (float)mSr)
                                      : std::max(88.0f, mCut - 62.0f / (float)mSr);
               if ((i & 15) == 0)
               {
                  const float note = fx == PX_LPF ? mCut : 127.0f - mCut + 40.0f;
                  const float hz = std::min(NoteHz(note), 0.45f * (float)mSr);
                  mF[0].SetCutoff(hz, 1.2f);
                  mF[1].SetCutoff(hz, 1.2f);
               }
               const float lowL = mF[0].Process(x).low, lowR = mF[1].Process(y).low;
               wl = fx == PX_LPF ? lowL : x - lowL;
               wr = fx == PX_LPF ? lowR : y - lowR;
               break;
            }
            case PX_TEL:
            {
               const float b = mF[0].Process(m).low;
               const float band = mF[2].Process(b).high;
               wl = wr = 0.5f * std::tanh(4.0f * band);
               break;
            }
            case PX_CRUSH:
               wl = std::trunc(x * 32.0f) / 32.0f;
               wr = std::trunc(y * 32.0f) / 32.0f;
               break;
            case PX_DOWN:
               if (mHold == 0)
               {
                  mHold = holdN;
                  mHeldL = x;
                  mHeldR = y;
               }
               --mHold;
               wl = mHeldL;
               wr = mHeldR;
               break;
            case PX_GATE:
            {
               const uint32_t p = mGp;
               float gg = p < step / 2u ? 1.0f : 0.0f;
               mGp = p + 1u >= step ? 0u : p + 1u;
               if (p < 64u) gg = (float)p / 64.0f;
               else if (p >= step / 2u && p < step / 2u + 64u) gg = 1.0f - (float)(p - step / 2u) / 64.0f;
               wl = x * gg;
               wr = y * gg;
               break;
            }
            case PX_KLOW:
            case PX_KMID:
            case PX_KHIGH:
            {
               auto kill = [&](float s, DspMath::TptSvf* k) -> float
               {
                  if (fx == PX_KLOW) // keep above ~250 Hz
                     return k[1].Process(k[0].Process(s).high).high;
                  if (fx == PX_KHIGH) // keep below ~2.5 kHz
                     return k[5].Process(k[4].Process(s).low).low;
                  const float lo = k[1].Process(k[0].Process(s).low).low;
                  const float hi = k[5].Process(k[4].Process(s).high).high;
                  return lo + hi;
               };
               wl = kill(x, &mKill[0]);
               wr = kill(y, &mKill[8]);
               break;
            }
            case PX_RWD:
            {
               const double back = (double)mW - mRp;
               const double room = (double)kRing - 512.0 - back;
               if (room <= 0.0) { wl = wr = 0.0f; break; }
               const float e = room < 1024.0 ? (float)(room / 1024.0) : 1.0f;
               wl = wr = RingAt(mRp) * e;
               mRp += mV;
               if (mV > -3.0) mV -= rwdAcc;
               break;
            }
            case PX_OCT:
            {
               const uint32_t half = octW / 2u;
               const uint32_t d1 = octW - mK, d2 = d1 > half ? d1 - half : d1 + half;
               const float g1 = (float)(d1 <= half ? d1 : octW - d1) / (float)half;
               mK = mK + 1u >= octW ? 0u : mK + 1u;
               wl = wr = Ring(mW - d1) * g1 + Ring(mW - d2) * (1.0f - g1);
               break;
            }
            case PX_FLANGE:
            {
               const double dl = (26.0 + (std::sin(kTwoPi * mLfo) + 1.0) * 120.0) * mScale;
               const double dr = (26.0 + (std::sin(kTwoPi * (mLfo + 0.25)) + 1.0) * 120.0) * mScale;
               const float el = RingAt((double)mW - dl), er = RingAt((double)mW - dr);
               mLfo += turnPerSample / 4.0; // one sweep a bar
               if (mLfo >= 1.0) mLfo -= 1.0;
               mRing[mW & kMask] = m + (el + er) * 0.2747f; // feedback through the ring
               wl = (x + el) * 0.625f;
               wr = (y + er) * 0.625f;
               break;
            }
            case PX_PAN:
            {
               const double a = (std::sin(kTwoPi * mLfo) * 0.854 + 1.0) * 0.125; // 0..a quarter turn
               mLfo += turnPerSample;
               if (mLfo >= 1.0) mLfo -= 1.0;
               wl = m * (float)(std::cos(kTwoPi * a) * 1.41421356);
               wr = m * (float)(std::sin(kTwoPi * a) * 1.41421356);
               break;
            }
            case PX_MONO:
               wl = wr = m;
               break;
            case PX_FADE:
            {
               const float gq = mFade * mFade;
               mFade = mFade > fadeStep ? mFade - fadeStep : 0.0f;
               wl = x * gq;
               wr = y * gq;
               break;
            }
            case PX_ECHO:
            {
               const float e = Ring(mW - echoD) * 0.549f;
               mRing[mW & kMask] = m + e; // feedback
               wl = x + e;
               wr = y + e;
               break;
            }
            default:
               if (fx < 0)
                  break;
               if (mPre)
               {
                  if (--mPre == 0)
                  {
                     mStart = mW + 1u;
                     mK = 0;
                  }
                  break;
               }
               wl = wr = RingFx(fx);
               break;
         }
         if (mG < target) mG = std::min(1.0f, mG + gStep);
         else if (mG > target) mG = std::max(0.0f, mG - gStep);
         L[i] = DspMath::FlushDenormal(x + (wl - x) * mG);
         if (R != L) R[i] = DspMath::FlushDenormal(y + (wr - y) * mG);
         ++mW;
         ++mT;
         if (mG <= 0.0f && want != fx)
            mCur = -1; // faded out: the next block starts the next effect
      }
   }


   // FREEZE: the last quarter second, replayed as overlapping Hann grains
   // from random points (left and right apart), the dry signal fading under.
   void CaptureFreeze()
   {
      const size_t len = mFrozen.size();
      for (size_t i = 0; i < len; ++i)
         mFrozen[i] = Ring(mW - (uint32_t)len + (uint32_t)i);
      mGrainLen = std::max<uint32_t>(256u, (uint32_t)(0.12 * mSr));
      for (int c = 0; c < 2; ++c)
         for (int g = 0; g < 2; ++g)
         {
            mGrainPos[c][g] = NextGrainStart();
            mGrainT[c][g] = g * (mGrainLen / 2u);
         }
   }

   uint32_t NextGrainStart()
   {
      mRand = mRand * 1664525u + 1013904223u;
      const uint32_t span = (uint32_t)mFrozen.size() > mGrainLen ? (uint32_t)mFrozen.size() - mGrainLen : 1u;
      return (mRand >> 8) % span;
   }

   float GrainSample(int c)
   {
      float s = 0.0f;
      for (int g = 0; g < 2; ++g)
      {
         const uint32_t t = mGrainT[c][g];
         const float w = 0.5f - 0.5f * std::cos((float)kTwoPi * (float)t / (float)mGrainLen);
         s += mFrozen[(mGrainPos[c][g] + t) % mFrozen.size()] * w;
         if (++mGrainT[c][g] >= mGrainLen)
         {
            mGrainT[c][g] = 0;
            mGrainPos[c][g] = NextGrainStart();
         }
      }
      return s;
   }

   void ProcessFreeze(float* L, float* R, int n, int want)
   {
      const float gStep = 1.0f / (0.05f * (float)mSr); // 50 ms in and out
      for (int i = 0; i < n; ++i)
      {
         const float x = L[i], y = R[i];
         mRing[mW & kMask] = 0.5f * (x + y);
         const float target = want == PX_FREEZE ? 1.0f : 0.0f;
         if (mG < target) mG = std::min(1.0f, mG + gStep);
         else if (mG > target) mG = std::max(0.0f, mG - gStep);
         const float wl = GrainSample(0), wr = GrainSample(1);
         L[i] = DspMath::FlushDenormal(x + (wl - x) * mG);
         if (R != L) R[i] = DspMath::FlushDenormal(y + (wr - y) * mG);
         ++mW;
         ++mT;
         if (mG <= 0.0f && want != PX_FREEZE)
            mCur = -1;
      }
   }

   std::vector<float> mRing;
   double mSr = 48000.0;
   float mScale = 1.0f;
   int mCur = -1;
   float mG = 0.0f;
   uint32_t mW = 0, mT = 0, mStart = 0, mLen = 64, mPre = 0, mK = 0, mSub = 0, mGp = 0, mHold = 0;
   double mRp = 0.0, mSpd = 1.0, mDspd = 0.0, mLfo = 0.0, mV = 1.0;
   float mCut = 127.0f, mFade = 1.0f, mHeldL = 0.0f, mHeldR = 0.0f;
   DspMath::TptSvf mF[4];
   DspMath::TptSvf mKill[16];
   std::vector<float> mFrozen;
   uint32_t mGrainLen = 4096, mRand = 22222u;
   uint32_t mGrainPos[2][2] = {}, mGrainT[2][2] = {};
};

const char* PunchFxNode::EffectName(int i)
{
   return (i >= 0 && i < kNumEffects) ? kNames[i] : "-";
}

class AudioPunchFxNode : public AudioNode
{
public:
   AudioPunchFxNode() : mEngines(PX_COUNT)
   {
      for (int p = 0; p < PunchFxNode::kNumPads; ++p)
      {
         mPadNote[p].store(PunchFxNode::kBaseNote + p, std::memory_order_relaxed);
         mPadFx[p].store(p, std::memory_order_relaxed);
      }
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      for (PunchEngine& e : mEngines)
         e.Prepare(sampleRate);
      std::fill(std::begin(mNoteHeld), std::end(mNoteHeld), 0);
      mLatchPads = 0;
   }

   void Reset() override
   {
      std::fill(std::begin(mNoteHeld), std::end(mNoteHeld), 0);
      mLatchPads = 0;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mInbox = inbox;
      mCursor = cursor;
   }

   // ---- main thread ----
   void SetMouseMask(uint32_t fxMask) { mMouseMask.store(fxMask, std::memory_order_relaxed); }
   void SetPad(int p, int note, int fx)
   {
      mPadNote[p].store(note, std::memory_order_relaxed);
      mPadFx[p].store(fx, std::memory_order_relaxed);
   }
   void SetLatch(bool on) { mLatch.store(on, std::memory_order_relaxed); }
   uint32_t ActiveMask() const { return mActive.load(std::memory_order_relaxed); }
   // Last note-on seen (for note learn): serial bumps on every note-on.
   void LastNote(int& note, uint32_t& serial) const
   {
      serial = mNoteSerial.load(std::memory_order_acquire);
      note = mLastNote.load(std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& out) override
   {
      const AudioBuffer* in = numInputs > 0 ? inputs[0] : nullptr;
      const int n = out.numFrames;
      const int chans = out.numChannels;
      float* L = chans > 0 ? out.channels[0] : nullptr;
      float* R = chans > 1 ? out.channels[1] : L;
      if (L == nullptr)
         return;
      for (int i = 0; i < n; ++i)
      {
         const float x = in != nullptr && in->numChannels > 0 ? in->channels[0][i] : 0.0f;
         const float y = in != nullptr ? (in->numChannels > 1 ? in->channels[1][i] : x) : 0.0f;
         L[i] = x;
         if (R != L) R[i] = y;
      }

      DrainNotes();
      uint32_t padMask = mLatchPads;
      for (int p = 0; p < PunchFxNode::kNumPads; ++p)
      {
         const int note = mPadNote[p].load(std::memory_order_relaxed);
         if (note >= 0 && note < 128 && mNoteHeld[note] > 0 && !mLatch.load(std::memory_order_relaxed))
            padMask |= 1u << p;
      }
      uint32_t fxMask = mMouseMask.load(std::memory_order_relaxed);
      for (int p = 0; p < PunchFxNode::kNumPads; ++p)
         if (padMask & (1u << p))
         {
            const int fx = mPadFx[p].load(std::memory_order_relaxed);
            if (fx >= 0 && fx < PX_COUNT)
               fxMask |= 1u << fx;
         }

      uint32_t active = 0;
      for (int fx = 0; fx < PX_COUNT; ++fx)
      {
         const bool held = (fxMask >> fx) & 1u;
         mEngines[fx].Process(L, R, n, fx, held);
         if (mEngines[fx].Busy())
            active |= 1u << fx;
      }
      mActive.store(active, std::memory_order_relaxed);
      for (int ch = 2; ch < chans; ++ch)
         std::memcpy(out.channels[ch], L, sizeof(float) * n);
   }

private:
   void DrainNotes()
   {
      if (mInbox == nullptr || mCursor < 0)
         return;
      const bool latch = mLatch.load(std::memory_order_relaxed);
      NoteEvent ev[64];
      int got = 0;
      while ((got = mInbox->Pop(mCursor, ev, 64)) > 0)
      {
         for (int i = 0; i < got; ++i)
         {
            const NoteEvent& e = ev[i];
            if (e.bendUpdate || e.note < 0 || e.note >= 128)
               continue;
            const bool on = e.isNoteOn && e.velocity > 0.0f;
            if (on)
            {
               mLastNote.store(e.note, std::memory_order_relaxed);
               mNoteSerial.fetch_add(1, std::memory_order_release);
               ++mNoteHeld[e.note];
               if (latch)
                  for (int p = 0; p < PunchFxNode::kNumPads; ++p)
                     if (mPadNote[p].load(std::memory_order_relaxed) == e.note)
                        mLatchPads ^= 1u << p;
            }
            else if (mNoteHeld[e.note] > 0)
               --mNoteHeld[e.note];
         }
      }
      if (!latch)
         mLatchPads = 0;
   }

   std::vector<PunchEngine> mEngines;
   NoteEventQueue* mInbox = nullptr;
   int mCursor = -1;
   int mNoteHeld[128] = {};
   uint32_t mLatchPads = 0;
   std::atomic<int> mPadNote[PunchFxNode::kNumPads];
   std::atomic<int> mPadFx[PunchFxNode::kNumPads];
   std::atomic<uint32_t> mMouseMask { 0 };
   std::atomic<bool> mLatch { false };
   std::atomic<uint32_t> mActive { 0 };
   std::atomic<int> mLastNote { -1 };
   std::atomic<uint32_t> mNoteSerial { 0 };
};

PunchFxNode::PunchFxNode()
{
   for (int p = 0; p < kNumPads; ++p)
   {
      padNote[p] = kBaseNote + p;
      padFx[p] = p;
   }
}
PunchFxNode::~PunchFxNode() = default;

AudioNode* PunchFxNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPunchFxNode>();
   return mAudioNode.get();
}

void PunchFxNode::TogglePadLatch(int pad)
{
   if (pad >= 0 && pad < kNumPads)
      mMouseLatchPads ^= 1u << pad;
}

void PunchFxNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPunchFxNode>();
   effect = std::clamp(effect, 0, kNumEffects - 1);
   if (!latch)
      mMouseLatchPads = 0;
   uint32_t mask = engage ? (1u << effect) : 0u;
   for (int p = 0; p < kNumPads; ++p)
   {
      padFx[p] = std::clamp(padFx[p], 0, kNumEffects - 1);
      padNote[p] = std::clamp(padNote[p], 0, 127);
      mAudioNode->SetPad(p, padNote[p], padFx[p]);
      const bool mouse = latch ? ((mMouseLatchPads >> p) & 1u) != 0 : ((heldPads >> p) & 1u) != 0;
      if (mouse)
         mask |= 1u << padFx[p];
   }
   mAudioNode->SetMouseMask(mask);
   mAudioNode->SetLatch(latch);
   mActiveMask = mAudioNode->ActiveMask();

}

void PunchFxNode::VisitParams(ParamVisitor& v)
{
   v.Int("effect", effect);
   v.Bool("engage", engage);
   v.Bool("latch", latch);
   char key[24];
   for (int p = 0; p < kNumPads; ++p)
   {
      snprintf(key, sizeof(key), "pad%dfx", p);
      v.Int(key, padFx[p]);
      snprintf(key, sizeof(key), "pad%dnote", p);
      v.Int(key, padNote[p]);
   }
}
