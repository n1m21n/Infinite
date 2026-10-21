#pragma once

#include <algorithm>
#include <vector>

#include "audio/DspMath.h"
#include "audio/SynthModes.h"
#include "audio/dsp/Oversampler.h"
#include "audio/dsp/SimdF4.h"

// Four synth voices' drive-and-filter chains in one set of registers (2.16).
// Each type here is the lane-wise twin of a scalar primitive, doing the same
// operations in the same order, so lane n holds exactly what voice n's
// scalar state would. The scalar twins opt out of multiply-add fusion
// (INFINITE_NO_FMA, audio/FpContract.h) so both round alike:
//
//   DcBlocker4    DspMath::DcBlocker
//   Oversampler4  Oversampler (factor and taps copied from a prepared one)
//   Svf4          DspMath::TptSvf
//
// The filter models (FilterModels.h) are templates that serve both widths.
//
// State is stored as plain float[4] per variable, so one lane can be reset or
// copied on its own when its voice starts a note. Every lane always runs: a
// lane whose voice is idle filters silence, and the voice resets its lane
// when it is next given a note.
namespace VoiceSimd
{
   struct DcBlocker4
   {
      alignas(16) float prevIn[4] = {};
      alignas(16) float prevOut[4] = {};
      float r = 0.0f;

      void SetSampleRate(double sampleRate)
      {
         DspMath::DcBlocker d;
         d.SetSampleRate(sampleRate);
         r = d.r;
      }
      void ResetLane(int l) { prevIn[l] = prevOut[l] = 0.0f; }
      void CopyLane(const DcBlocker4& from, int l)
      {
         prevIn[l] = from.prevIn[l];
         prevOut[l] = from.prevOut[l];
      }

      F4 Process(F4 x)
      {
         const F4 y = x - F4::Load(prevIn) + F4(r) * F4::Load(prevOut);
         x.Store(prevIn);
         y.Store(prevOut);
         return y;
      }
   };

   struct Svf4
   {
      alignas(16) float s1[4] = {};
      alignas(16) float s2[4] = {};

      void ResetLane(int l) { s1[l] = s2[l] = 0.0f; }

      // `shape` is a SynthModes::kShape*; anything else reads the low output.
      F4 Process(F4 in, F4 g, F4 k, int shape)
      {
         const F4 S1 = F4::Load(s1), S2 = F4::Load(s2);
         const F4 a1 = F4(1.0f) / (F4(1.0f) + g * (g + k));
         const F4 a2 = g * a1;
         const F4 bandOut = a1 * S1 + a2 * (in - S2);
         const F4 low = S2 + g * bandOut;
         (F4(2.0f) * bandOut - S1).Store(s1);
         (F4(2.0f) * low - S2).Store(s2);
         if (shape == SynthModes::kShapeHigh)
            return in - low - k * bandOut;
         if (shape == SynthModes::kShapeBand)
            return bandOut;
         if (shape == SynthModes::kShapeNotch)
            return in - k * bandOut;
         return low;
      }
   };

   // Oversampler's half-band cascade, four lanes wide. Histories interleave
   // the lanes ([sample][lane]) and stay mirrored like the scalar stage's, so
   // every tap reads one contiguous F4.
   class Oversampler4
   {
   public:
      // Main thread (allocates). `proto` must already be prepared.
      void Prepare(const Oversampler& proto)
      {
         mFactor = proto.Factor();
         mStages = proto.Stages();
         for (int s = 0; s < mStages; s++)
         {
            Stage& st = mStage[s];
            const Oversampler::HalfbandStage& p = proto.Stage(s);
            st.gd = p.GroupDelay();
            st.centre = p.Centre();
            st.odd = p.OddTaps();
            st.oddIndex = p.OddIndex();
            st.upTaps = p.UpTaps();
            st.parity = p.OddParity();
            st.upSize = p.UpSize();
            st.downSize = p.DownSize();
            st.upHist.assign((size_t)st.upSize * 2 * 4, 0.0f);
            st.downHist.assign((size_t)st.downSize * 2 * 4, 0.0f);
            st.upPos = st.downPos = 0;
         }
      }

      void ResetLane(int l)
      {
         for (int s = 0; s < mStages; s++)
         {
            Stage& st = mStage[s];
            for (size_t i = (size_t)l; i < st.upHist.size(); i += 4)
               st.upHist[i] = 0.0f;
            for (size_t i = (size_t)l; i < st.downHist.size(); i += 4)
               st.downHist[i] = 0.0f;
         }
      }

      int Factor() const { return mFactor; }

      void Up(F4 x, F4* os)
      {
         F4 a[Oversampler::kMaxFactor], b[Oversampler::kMaxFactor];
         a[0] = x;
         int n = 1;
         for (int s = 0; s < mStages; s++)
         {
            for (int i = 0; i < n; i++)
               mStage[s].Up(a[i], &b[2 * i]);
            n *= 2;
            std::copy(b, b + n, a);
         }
         std::copy(a, a + n, os);
      }

      F4 Down(const F4* os)
      {
         F4 a[Oversampler::kMaxFactor];
         std::copy(os, os + mFactor, a);
         int n = mFactor;
         for (int s = mStages - 1; s >= 0; s--)
         {
            n /= 2;
            for (int i = 0; i < n; i++)
               a[i] = mStage[s].Down(&a[2 * i]);
         }
         return a[0];
      }

   private:
      struct Stage
      {
         int gd = 0;
         float centre = 0.5f;
         std::vector<float> odd, upTaps;
         std::vector<int> oddIndex;
         int parity = 1;
         std::vector<float> upHist, downHist;
         int upSize = 0, downSize = 0, upPos = 0, downPos = 0;

         void Up(F4 x, F4* out)
         {
            x.Store(&upHist[(size_t)upPos * 4]);
            x.Store(&upHist[(size_t)(upPos + upSize) * 4]);
            const float* w = &upHist[(size_t)(upPos + upSize) * 4];
            F4 acc(0.0f);
            const int span = (int)upTaps.size();
            for (int j = 0; j < span; j++)
               acc += F4(upTaps[(size_t)j]) * F4::Load(w - 4 * j);
            const F4 delayed = F4(2.0f * centre) * F4::Load(w - 4 * (gd / 2));
            out[parity] = acc;
            out[parity ^ 1] = delayed;
            if (++upPos >= upSize)
               upPos = 0;
         }

         F4 Down(const F4* in)
         {
            Push(in[0]);
            Push(in[1]);
            const float* v0 = &downHist[(size_t)(downPos + downSize - 2) * 4];
            F4 acc = F4(centre) * F4::Load(v0 - 4 * gd);
            for (size_t k = 0; k < odd.size(); k++)
               acc += F4(odd[k]) * F4::Load(v0 - 4 * oddIndex[k]);
            return acc;
         }

         void Push(F4 v)
         {
            v.Store(&downHist[(size_t)downPos * 4]);
            v.Store(&downHist[(size_t)(downPos + downSize) * 4]);
            if (++downPos >= downSize)
               downPos = 0;
         }
      };

      int mFactor = 4;
      int mStages = 2;
      Stage mStage[3];
   };
}
