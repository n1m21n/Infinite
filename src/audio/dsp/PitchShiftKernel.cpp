#include "PitchShiftKernel.h"

#include "nodes/AudioEffectNode.h"

void PitchShiftKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kSemitones, node.Param("pitch"));
   mMailbox.Push(kGrainMs, node.Param("grain"));
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mSpectral.store(node.Param("spectral") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

double PitchShiftKernel::FindSplice(double from, int jMin, int jMax, int sign, int window) const
{
   // Integer part of each candidate's delay; the fractional part of `from`
   // carries over, so the comparison is on the sample grid.
   const int base = (int)std::floor(from);
   auto mono = [&](int k) { return mLineL.SampleAtDelay(k) + mLineR.SampleAtDelay(k); };

   double refEnergy = 0.0;
   for (int m = 0; m < window; m += 2)
   {
      const double r = mono(base + m);
      refEnergy += r * r;
   }
   const int nominal = jMax;
   if (refEnergy < 1.0e-12)
      return from + sign * nominal;

   auto score = [&](int j, int step) {
      const int cand = base + sign * j;
      double dot = 0.0, e = 0.0;
      for (int m = 0; m < window; m += step)
      {
         const double c = mono(cand + m);
         dot += c * mono(base + m);
         e += c * c;
      }
      return (e < 1.0e-12) ? -1.0 : dot / std::sqrt(e);
   };

   // Coarse pass on every other lag and sample, then refine around the best.
   int best = nominal;
   double bestScore = -1.0e30;
   for (int j = jMin; j <= jMax; j += 2)
   {
      const double sc = score(j, 2);
      if (sc > bestScore)
      {
         bestScore = sc;
         best = j;
      }
   }
   const int coarse = best;
   bestScore = -1.0e30;
   for (int j = std::max(jMin, coarse - 2); j <= std::min(jMax, coarse + 2); j++)
   {
      const double sc = score(j, 1);
      if (sc > bestScore)
      {
         bestScore = sc;
         best = j;
      }
   }
   return from + sign * best;
}

void PitchShiftKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;
   const double maxDelay = (double)mLineL.size - 4.0;

   // A mode switch starts the newly active engine from silence rather than
   // from whatever it held the last time it ran.
   const int spectral = mSpectral.load(std::memory_order_relaxed);
   if (spectral != mActiveSpectral)
   {
      if (spectral)
         mSpectralEngine.Reset();
      else
      {
         mLineL.Reset();
         mLineR.Reset();
         mDelay = mNextDelay = 1.0;
         mFadeLen = 0;
      }
      mActiveSpectral = spectral;
   }

   const float aFast = 1.0f - std::exp(-1.0f / (0.0005f * (float)mSampleRate));
   const float aSlow = 1.0f - std::exp(-1.0f / (0.050f * (float)mSampleRate));
   for (int i = 0; i < out.numFrames; i++)
   {
      const float semitones = mMailbox.SmoothedValue(kSemitones);
      const float grainMs = std::clamp(mMailbox.SmoothedValue(kGrainMs), 10.0f, kMaxGrainMs);
      const double grain = grainMs * 0.001 * mSampleRate;
      const double ratio = std::pow(2.0, semitones / 12.0);
      const double drift = 1.0 - ratio; // delay change per sample: < 0 shifting up

      const float inL = in.channels[0][i];
      const float inR = numChannels >= 2 ? in.channels[1][i] : inL;

      if (spectral)
      {
         float outL = mSpectralEngine.Process(0, inL, ratio);
         float outR = numChannels >= 2 ? mSpectralEngine.Process(1, inR, ratio) : outL;
         if (analog)
         {
            outL = AnalogDsp::AsymTanh(mFilterL[1].Process(mFilterL[0].Process(outL).low).low, 0.12f);
            outR = AnalogDsp::AsymTanh(mFilterR[1].Process(mFilterR[0].Process(outR).low).low, 0.12f);
         }
         out.channels[0][i] = outL;
         if (numChannels >= 2)
            out.channels[1][i] = outR;
         for (int ch = 2; ch < out.numChannels; ch++)
            out.channels[ch][i] = 0.0f;
         continue;
      }

      mLineL.Write(inL);
      mLineR.Write(inR);

      // Onset: a fast (0.5 ms) envelope jumping well above a slow (50 ms)
      // one; re-armed once it settles back near it.
      {
         const float x = std::fabs(inL) + std::fabs(inR);
         mEnvFast += (x - mEnvFast) * aFast;
         mEnvSlow += (x - mEnvSlow) * aSlow;
         if (mOnsetDelay >= 0.0 && ++mOnsetDelay > maxDelay)
            mOnsetDelay = -1.0;
         if (mOnsetArmed && mEnvFast > 4.0f * mEnvSlow + 1.0e-3f)
         {
            mOnsetDelay = 1.0;
            mOnsetArmed = false;
         }
         else if (!mOnsetArmed && mEnvFast < 1.5f * mEnvSlow)
            mOnsetArmed = true;
      }

      // Splice geometry, from the grain: jump by 0.5..1 grain (the search
      // range), crossfade over 0.3 grain (3..30 ms), compare 15 ms of signal.
      const int jMax = std::max(4, (int)grain);
      const int jMin = std::max(2, jMax / 2);
      const int fadeLen = (int)std::clamp(0.3 * grain, 0.003 * mSampleRate, 0.030 * mSampleRate);
      const int window = (int)std::min((double)fadeLen, 0.015 * mSampleRate);

      // A splice's jump, shortened so it does not cross the last onset:
      // `room` is the distance from the tap to the onset in the jump's
      // direction, less a 1 ms pre-roll so the attack's front stays intact.
      // Only when the room left still fits a crossfade of at least 3 ms;
      // otherwise the hit cannot be avoided and the splice is left as is.
      auto steer = [&](int sign, int& lo, int& hi, int& fade) {
         if (mOnsetDelay < 0.0)
            return;
         const double room = sign > 0 ? mOnsetDelay - mDelay : mDelay - mOnsetDelay;
         const int limit = (int)(room - 0.001 * mSampleRate);
         if (room <= 0.0 || limit >= hi)
            return;
         const int minFade = (int)(0.003 * mSampleRate);
         // Shifting up, the new tap must not reach the newest sample before
         // its fade ends; shifting down, the jump only has to be a jump.
         const int need = sign > 0 ? (int)std::ceil(-drift * minFade) + 4 : 4;
         if (limit < need)
            return;
         hi = limit;
         lo = std::min(lo, limit);
         if (sign > 0)
            fade = std::clamp((int)((limit - 4) / -drift), minFade, fade);
      };

      if (mFadeLen == 0)
      {
         if (drift < -1.0e-9 && mDelay + drift * fadeLen <= 2.0)
         {
            // Shifting up: about to run into the newest sample - jump back.
            int lo = jMin, hi = jMax, fade = fadeLen;
            steer(+1, lo, hi, fade);
            const double target = std::min(FindSplice(mDelay, lo, hi, +1, std::min(window, fade)), maxDelay);
            mNextDelay = target;
            mFadeLen = fade;
            mFadePos = 0;
         }
         else if (drift > 1.0e-9 && mDelay + drift * fadeLen >= std::min(maxDelay, (double)jMax + fadeLen + 2.0))
         {
            // Shifting down: about to fall off the end - jump forward.
            int lo = jMin, hi = jMax, fade = fadeLen;
            steer(-1, lo, hi, fade);
            const double target = std::max(FindSplice(mDelay, lo, hi, -1, window), 1.0);
            mNextDelay = target;
            mFadeLen = fade;
            mFadePos = 0;
         }
         else if (std::fabs(drift) <= 1.0e-9 && mDelay != 1.0)
         {
            // Back at 0 st after a shift: return to transparent.
            mNextDelay = 1.0;
            mFadeLen = fadeLen;
            mFadePos = 0;
         }
      }

      float jitter = 0.0f;
      if (analog)
      {
         mJitterPhase += 17.3f / (float)mSampleRate;
         if (mJitterPhase >= 1.0f)
            mJitterPhase -= 1.0f;
         jitter = sinf(mJitterPhase * 2.0f * (float)M_PI) * 0.35f;
      }

      const float dA = (float)std::clamp(mDelay + jitter, 1.0, maxDelay);
      float outL = mLineL.Read(dA);
      float outR = mLineR.Read(dA);
      if (mFadeLen > 0)
      {
         const float t = (float)(mFadePos + 1) / (float)(mFadeLen + 1);
         const float wNew = 0.5f - 0.5f * cosf((float)M_PI * t);
         const float dB = (float)std::clamp(mNextDelay + jitter, 1.0, maxDelay);
         outL += (mLineL.Read(dB) - outL) * wNew;
         outR += (mLineR.Read(dB) - outR) * wNew;
      }

      // Both taps move at the shifted rate; a finished splice hands over.
      mDelay = std::clamp(mDelay + drift, 1.0, maxDelay);
      if (mFadeLen > 0)
      {
         mNextDelay = std::clamp(mNextDelay + drift, 1.0, maxDelay);
         if (++mFadePos >= mFadeLen)
         {
            mDelay = mNextDelay;
            mFadeLen = 0;
         }
      }

      if (analog)
      {
         // 4th-order 9kHz lowpass reconstruction filter
         outL = mFilterL[0].Process(outL).low;
         outL = mFilterL[1].Process(outL).low;
         outR = mFilterR[0].Process(outR).low;
         outR = mFilterR[1].Process(outR).low;

         // Harmonic grain saturation
         outL = AnalogDsp::AsymTanh(outL, 0.12f);
         outR = AnalogDsp::AsymTanh(outR, 0.12f);
      }

      out.channels[0][i] = outL;
      if (numChannels >= 2)
         out.channels[1][i] = outR;

      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}
