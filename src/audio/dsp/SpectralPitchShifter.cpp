#include "SpectralPitchShifter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/DspMath.h"

namespace
{
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr float kPeakRelFloor = 1.0e-4f; // -80 dB re the frame's strongest bin
   constexpr float kPeakAbsFloor = 1.0e-7f;
   // Spectral flux (summed magnitude rise / summed magnitude) above which a
   // frame counts as an onset and its phases are reset. A steady chord sits
   // near 0; a drum hit or plucked attack lands well above 0.3.
   constexpr float kOnsetFlux = 0.3f;

   inline float WrapPi(float x)
   {
      while (x > 3.14159265f)
         x -= kTwoPi;
      while (x <= -3.14159265f)
         x += kTwoPi;
      return x;
   }
}

void SpectralPitchShifter::Prepare(double sampleRate)
{
   mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
   mFft.Prepare(kLog2Fft);
   mWindow.resize(kFftSize);
   for (int n = 0; n < kFftSize; n++)
      mWindow[(size_t)n] = 0.5f * (1.0f - cosf((float)(2.0 * M_PI * (double)n / (double)kFftSize)));
   mTime.assign(kFftSize, 0.0f);
   mRe.assign(kBins, 0.0f);
   mIm.assign(kBins, 0.0f);
   mOutRe.assign(kBins, 0.0f);
   mOutIm.assign(kBins, 0.0f);
   mMag.assign(kBins + 1, 0.0f);
   mPhase.assign(kBins + 1, 0.0f);
   mInst.assign(kBins + 1, 0.0f);
   for (Channel& c : mCh)
   {
      c.fifo.assign(kFftSize, 0.0f);
      c.ola.assign(kOlaSize, 0.0f);
      c.phasePrev.assign(kBins + 1, 0.0f);
      c.magPrev.assign(kBins + 1, 0.0f);
      c.synthPhase.assign(kBins + 1, 0.0f);
      c.synthPhaseNext.assign(kBins + 1, 0.0f);
      c.hasSynth.assign(kBins + 1, 0);
      c.hasSynthNext.assign(kBins + 1, 0);
   }
   Reset();
}

void SpectralPitchShifter::Reset()
{
   for (Channel& c : mCh)
   {
      std::fill(c.fifo.begin(), c.fifo.end(), 0.0f);
      std::fill(c.ola.begin(), c.ola.end(), 0.0f);
      std::fill(c.phasePrev.begin(), c.phasePrev.end(), 0.0f);
      std::fill(c.magPrev.begin(), c.magPrev.end(), 0.0f);
      std::fill(c.hasSynth.begin(), c.hasSynth.end(), (uint8_t)0);
      std::fill(c.hasSynthNext.begin(), c.hasSynthNext.end(), (uint8_t)0);
      c.fifoCount = 0;
      c.olaRead = 0;
   }
}

float SpectralPitchShifter::Process(int ch, float x, double ratio)
{
   Channel& c = mCh[ch];
   constexpr int olaMask = kOlaSize - 1;
   // Pop first so the latency is exactly kFftSize.
   const float y = DspMath::FlushDenormal(c.ola[(size_t)c.olaRead]);
   c.ola[(size_t)c.olaRead] = 0.0f;
   c.olaRead = (c.olaRead + 1) & olaMask;

   c.fifo[(size_t)c.fifoCount++] = x;
   if (c.fifoCount >= kFftSize)
   {
      // Zero-phase framing: the windowed frame is rotated by N/2 so the
      // window's centre sits at sample 0. A sinusoid's main-lobe bins then
      // share one phase instead of alternating sign bin to bin, which is
      // what lets a region be resampled at a fractional bin offset (an
      // interpolation between alternating-sign bins cancels itself).
      for (int n = 0; n < kFftSize; n++)
         mTime[(size_t)((n + kFftSize / 2) & (kFftSize - 1))] = c.fifo[(size_t)n] * mWindow[(size_t)n];
      mFft.Forward(mTime.data(), kLog2Fft, mRe.data(), mIm.data());
      for (int k = 0; k < kBins; k++)
      {
         mRe[(size_t)k] *= 0.5f; // PortableFft's bins are x2 (vDSP zrip convention)
         mIm[(size_t)k] *= 0.5f;
      }

      Frame(c, ratio);

      // Hann analysis x Hann synthesis at 75% overlap sums to 1.5.
      const float normScale = 1.0f / ((float)kFftSize * 1.5f);
      mFft.Inverse(mOutRe.data(), mOutIm.data(), kLog2Fft, mTime.data());
      for (int n = 0; n < kFftSize; n++)
         c.ola[(size_t)((c.olaRead + n) & olaMask)] +=
            mTime[(size_t)((n + kFftSize / 2) & (kFftSize - 1))] * mWindow[(size_t)n] * normScale;

      std::memmove(c.fifo.data(), c.fifo.data() + kHop, (size_t)(kFftSize - kHop) * sizeof(float));
      c.fifoCount = kFftSize - kHop;
   }
   return y;
}

void SpectralPitchShifter::Frame(Channel& c, double ratioD)
{
   const float ratio = (float)ratioD;
   const float hopAdv = kTwoPi * (float)kHop / (float)kFftSize;

   // 1. Magnitude, phase, instantaneous frequency (bins); onset flux.
   float maxMag = 0.0f;
   double flux = 0.0, total = 0.0;
   for (int k = 1; k < kBins; k++)
   {
      const float re = mRe[(size_t)k], im = mIm[(size_t)k];
      const float mag = sqrtf(re * re + im * im);
      const float phase = atan2f(im, re);
      const float dev = WrapPi(phase - c.phasePrev[(size_t)k] - (float)k * hopAdv);
      c.phasePrev[(size_t)k] = phase;
      mMag[(size_t)k] = mag;
      mPhase[(size_t)k] = phase;
      mInst[(size_t)k] = (float)k + dev / hopAdv;
      maxMag = std::max(maxMag, mag);
      flux += std::max(0.0f, mag - c.magPrev[(size_t)k]);
      total += mag;
      c.magPrev[(size_t)k] = mag;
   }
   const bool onset = total > 1.0e-9 && flux / total > kOnsetFlux;

   mOutRe[0] = mRe[0];
   mOutIm[0] = mIm[0];

   // At ratio 1 the frame passes through: 0 st is an exact reconstruction.
   if (std::fabs(ratio - 1.0f) < 1.0e-6f)
   {
      for (int k = 1; k < kBins; k++)
      {
         mOutRe[(size_t)k] = mRe[(size_t)k];
         mOutIm[(size_t)k] = mIm[(size_t)k];
      }
      std::fill(c.hasSynth.begin(), c.hasSynth.end(), (uint8_t)0);
      return;
   }
   std::fill(mOutRe.begin() + 1, mOutRe.end(), 0.0f);
   std::fill(mOutIm.begin() + 1, mOutIm.end(), 0.0f);

   // 2. Peaks.
   int peaks[kMaxPeaks];
   int numPeaks = 0;
   const float thresh = std::max(maxMag * kPeakRelFloor, kPeakAbsFloor);
   for (int k = 2; k < kBins - 2 && numPeaks < kMaxPeaks; k++)
   {
      const float m = mMag[(size_t)k];
      // A local maximum is a sinusoid only if its measured frequency is its
      // own bin's (within a bin): a window sidelobe ripple of a stronger
      // partial measures that partial's frequency instead, and given a
      // region of its own it would pile onto the partial's target bin.
      if (m > thresh && m > mMag[(size_t)k - 1] && m >= mMag[(size_t)k + 1] && m > mMag[(size_t)k - 2] &&
          m >= mMag[(size_t)k + 2] && std::fabs(mInst[(size_t)k] - (float)k) <= 1.0f)
         peaks[numPeaks++] = k;
   }
   std::fill(c.hasSynthNext.begin(), c.hasSynthNext.end(), (uint8_t)0);
   if (numPeaks == 0)
   {
      c.hasSynth.swap(c.hasSynthNext);
      return;
   }

   // 3. Move each peak's region to the shifted frequency, phase-locked.
   for (int pi = 0; pi < numPeaks; pi++)
   {
      const int p = peaks[pi];
      int lo = 1, hi = kBins - 1;
      if (pi > 0)
      {
         int v = peaks[pi - 1];
         for (int k = peaks[pi - 1]; k <= p; k++)
            if (mMag[(size_t)k] < mMag[(size_t)v])
               v = k;
         lo = std::min(v + 1, p); // the valley bin belongs to the region below
      }
      if (pi + 1 < numPeaks)
      {
         int v = p;
         for (int k = p; k <= peaks[pi + 1]; k++)
            if (mMag[(size_t)k] < mMag[(size_t)v])
               v = k;
         hi = v;
      }

      // The region moves by the exact (fractional) number of bins that puts
      // the peak's true frequency on its target. A whole-bin move leaves the
      // content up to a bin off the frequency its phase advances at, and
      // overlapping frames then disagree - amplitude modulation at the frame
      // rate (measured: -16 dB of sidebands an octave down at N = 4096).
      const float inst = mInst[(size_t)p];
      const float shift = inst * (ratio - 1.0f);
      const int pTarget = (int)std::lround(inst * ratio);
      if (pTarget < 1 || pTarget >= kBins)
         continue; // shifted past Nyquist (or below DC): dropped

      // Continue last frame's synthesis phase at (or next to) the target
      // bin; at an onset, or with no history, take the analysis phase.
      float newPhase = mPhase[(size_t)p];
      if (!onset)
      {
         for (int d = 0; d <= 1; d++)
         {
            bool found = false;
            for (int s = -d; s <= d; s += (d == 0 ? 1 : 2))
            {
               const int b = pTarget + s;
               if (b >= 1 && b < kBins && c.hasSynth[(size_t)b])
               {
                  newPhase = WrapPi(c.synthPhase[(size_t)b] + hopAdv * inst * ratio);
                  found = true;
                  break;
               }
            }
            if (found)
               break;
         }
      }

      // Rotate so the peak's phase lands on newPhase (identity phase
      // locking: the whole region turns by the same angle), then resample
      // the region onto the target bins at the fractional offset.
      const float theta = newPhase - mPhase[(size_t)p];
      const float rc = cosf(theta), rs = sinf(theta);
      const int ktLo = std::max(1, (int)std::ceil((float)lo + shift));
      const int ktHi = std::min(kBins - 1, (int)std::floor((float)hi + shift));
      for (int kt = ktLo; kt <= ktHi; kt++)
      {
         const float src = (float)kt - shift;
         int k0 = (int)std::floor(src);
         float f = src - (float)k0;
         if (k0 < lo)
         {
            k0 = lo;
            f = 0.0f;
         }
         const int k1 = std::min(k0 + 1, hi);
         const float re = mRe[(size_t)k0] + (mRe[(size_t)k1] - mRe[(size_t)k0]) * f;
         const float im = mIm[(size_t)k0] + (mIm[(size_t)k1] - mIm[(size_t)k0]) * f;
         mOutRe[(size_t)kt] += re * rc - im * rs;
         mOutIm[(size_t)kt] += re * rs + im * rc;
      }
      c.synthPhaseNext[(size_t)pTarget] = newPhase;
      c.hasSynthNext[(size_t)pTarget] = 1;
   }
   c.synthPhase.swap(c.synthPhaseNext);
   c.hasSynth.swap(c.hasSynthNext);
}
