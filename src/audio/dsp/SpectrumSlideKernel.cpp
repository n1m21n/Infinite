#include "SpectrumSlideKernel.h"

#include <algorithm>
#include <cstring>

#include "nodes/AudioEffectNode.h"

namespace
{
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr float kSilence = 1e-8f; // total spectral energy below this counts as silence

   inline float WrapPi(float x)
   {
      while (x > 3.14159265f) x -= kTwoPi;
      while (x <= -3.14159265f) x += kTwoPi;
      return x;
   }
}

void SpectrumSlideKernel::Side::Init()
{
   inputFifo.assign(kFftSize, 0.0f);
   phasePrev.assign(kNumBins + 1, 0.0f);
   fifoSamples = 0;
}

void SpectrumSlideKernel::Side::Reset()
{
   std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
   std::fill(phasePrev.begin(), phasePrev.end(), 0.0f);
   fifoSamples = 0;
}

void SpectrumSlideKernel::ChannelState::Init()
{
   a.Init();
   b.Init();
   outputOla.assign(kOlaSize, 0.0f);
   synthPhase.assign(kNumBins + 1, 0.0f);
   olaRead = 0;
}

void SpectrumSlideKernel::ChannelState::Reset()
{
   a.Reset();
   b.Reset();
   std::fill(outputOla.begin(), outputOla.end(), 0.0f);
   std::fill(synthPhase.begin(), synthPhase.end(), 0.0f);
   olaRead = 0;
}

void SpectrumSlideKernel::Spectrum::Init()
{
   re.assign(kNumBins, 0.0f);
   im.assign(kNumBins, 0.0f);
   mag.assign(kNumBins + 1, 0.0f);
   phase.assign(kNumBins + 1, 0.0f);
   inst.assign(kNumBins + 1, 0.0f);
   cdf.assign(kNumBins + 1, 0.0f);
   energy = 0.0f;
}

void SpectrumSlideKernel::PrepareToPlay(double sampleRate, int /*maxBlockSize*/)
{
   mSampleRate = sampleRate;
   mFft.Prepare(kLog2Fft);
   mWindow.resize(kFftSize);
   for (int n = 0; n < kFftSize; n++)
      mWindow[n] = 0.5f * (1.0f - cosf((float)(2.0 * M_PI * (double)n / (double)kFftSize)));
   mTime.assign(kFftSize, 0.0f);
   mOutRe.assign(kNumBins, 0.0f);
   mOutIm.assign(kNumBins, 0.0f);
   mA.Init();
   mB.Init();
   mHist.assign(kNumBins + 2, 0.0f);
   mInstNum.assign(kNumBins + 2, 0.0f);
   mSrcA.assign(kNumBins + 2, 0.0f);
   mSrcB.assign(kNumBins + 2, 0.0f);
   mOutMag.assign(kNumBins + 2, 0.0f);
   mOutInst.assign(kNumBins + 2, 0.0f);
   for (int ch = 0; ch < kMaxChannels; ch++)
      mChannels[ch].Init();
}

void SpectrumSlideKernel::Reset()
{
   for (int ch = 0; ch < kMaxChannels; ch++)
      mChannels[ch].Reset();
}

void SpectrumSlideKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mSlide.store(std::clamp(node.Param("slide"), 0.0f, 1.0f), std::memory_order_relaxed);
}

// Forward FFT of one input's window, then magnitude / phase / instantaneous
// frequency (in bins) per bin and the normalised cumulative energy curve.
void SpectrumSlideKernel::Analyse(Side& side, const float* fifo, Spectrum& sp)
{
   for (int n = 0; n < kFftSize; n++)
      mTime[n] = fifo[n] * mWindow[n];
   mFft.Forward(mTime.data(), kLog2Fft, sp.re.data(), sp.im.data());
   for (int k = 0; k < kNumBins; k++)
   {
      sp.re[k] *= 0.5f;
      sp.im[k] *= 0.5f;
   }

   const float hopAdv = kTwoPi * (float)kHopSize / (float)kFftSize;
   float total = 0.0f;
   sp.cdf[0] = 0.0f;
   for (int k = 1; k < kNumBins; k++)
   {
      const float re = sp.re[k];
      const float im = sp.im[k];
      const float mag = sqrtf(re * re + im * im);
      const float ph = atan2f(im, re);
      const float dev = WrapPi(ph - side.phasePrev[k] - (float)k * hopAdv);
      side.phasePrev[k] = ph;
      sp.mag[k] = mag;
      sp.phase[k] = ph;
      sp.inst[k] = (float)k + dev / hopAdv;
      total += mag * mag;
      sp.cdf[k] = total; // unnormalised for now
   }
   sp.energy = total;
   if (total > kSilence)
   {
      const float inv = 1.0f / total;
      for (int k = 1; k < kNumBins; k++)
         sp.cdf[k] *= inv;
      sp.cdf[kNumBins - 1] = 1.0f;
   }
}

void SpectrumSlideKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out)
{
   const int numChannels = std::min({ in.numChannels, out.numChannels, kMaxChannels });
   const float normScale = 1.0f / ((float)kFftSize * 1.5f);
   const int olaMask = kOlaSize - 1;

   for (int ch = 0; ch < numChannels; ch++)
   {
      ChannelState& st = mChannels[ch];
      const float* inA = in.channels[ch];
      // No cable on B: slide has nothing to move toward, so B mirrors A.
      const float* inB = (sidechain && sidechain->numChannels > 0)
                            ? sidechain->channels[std::min(ch, sidechain->numChannels - 1)]
                            : inA;
      float* outData = out.channels[ch];

      for (int i = 0; i < out.numFrames; i++)
      {
         outData[i] = DspMath::FlushDenormal(st.outputOla[st.olaRead]);
         st.outputOla[st.olaRead] = 0.0f;
         st.olaRead = (st.olaRead + 1) & olaMask;

         st.a.inputFifo[st.a.fifoSamples++] = inA[i];
         st.b.inputFifo[st.b.fifoSamples++] = inB[i];
         if (st.a.fifoSamples >= kFftSize)
         {
            Analyse(st.a, st.a.inputFifo.data(), mA);
            Analyse(st.b, st.b.inputFifo.data(), mB);

            ProcessFrame(st);

            mFft.Inverse(mOutRe.data(), mOutIm.data(), kLog2Fft, mTime.data());
            for (int n = 0; n < kFftSize; n++)
               st.outputOla[(st.olaRead + n) & olaMask] += mTime[n] * mWindow[n] * normScale;

            const size_t keep = (size_t)(kFftSize - kHopSize) * sizeof(float);
            std::memmove(st.a.inputFifo.data(), st.a.inputFifo.data() + kHopSize, keep);
            std::memmove(st.b.inputFifo.data(), st.b.inputFifo.data() + kHopSize, keep);
            st.a.fifoSamples = st.b.fifoSamples = kFftSize - kHopSize;
         }
      }
   }

   for (int ch = numChannels; ch < out.numChannels; ch++)
      for (int i = 0; i < out.numFrames; i++)
         out.channels[ch][i] = 0.0f;
}

// mA / mB hold the two analysed spectra. Write the morph to mOutRe/mOutIm.
void SpectrumSlideKernel::ProcessFrame(ChannelState& st)
{
   const float s = mSlide.load(std::memory_order_relaxed);
   const float hopAdv = kTwoPi * (float)kHopSize / (float)kFftSize;

   std::fill(mOutRe.begin(), mOutRe.end(), 0.0f);
   std::fill(mOutIm.begin(), mOutIm.end(), 0.0f);
   mOutRe[0] = (1.0f - s) * mA.re[0] + s * mB.re[0];
   mOutIm[0] = (1.0f - s) * mA.im[0] + s * mB.im[0];

   const bool haveA = mA.energy > kSilence;
   const bool haveB = mB.energy > kSilence;
   if (!haveA && !haveB)
   {
      std::fill(st.synthPhase.begin(), st.synthPhase.end(), 0.0f);
      return;
   }
   // A silent side has no distribution to move: borrow the other's shape so
   // the morph reduces to a level fade instead of dragging energy to a ghost.
   const Spectrum& dA = haveA ? mA : mB;
   const Spectrum& dB = haveB ? mB : mA;
   const float energy = (1.0f - s) * mA.energy + s * mB.energy;
   const Spectrum& phaseSrc = s < 0.5f ? dA : dB;

   // 1. Transport equal-energy quantile levels. For each level u the bin
   //    position in A and in B is found by walking their cumulative curves;
   //    the output position is the s-weighted mean, and the level's energy is
   //    deposited there (linearly shared between the two nearest bins).
   std::fill(mHist.begin(), mHist.end(), 0.0f);
   std::fill(mInstNum.begin(), mInstNum.end(), 0.0f);
   std::fill(mSrcA.begin(), mSrcA.end(), 0.0f);
   std::fill(mSrcB.begin(), mSrcB.end(), 0.0f);

   const float w = 1.0f / (float)kQuantiles;
   int ia = 1, ib = 1;
   for (int q = 0; q < kQuantiles; q++)
   {
      const float u = ((float)q + 0.5f) * w;
      while (ia < kNumBins - 1 && dA.cdf[ia] < u) ia++;
      while (ib < kNumBins - 1 && dB.cdf[ib] < u) ib++;
      const float mA_ = std::max(dA.cdf[ia] - dA.cdf[ia - 1], 1e-12f);
      const float mB_ = std::max(dB.cdf[ib] - dB.cdf[ib - 1], 1e-12f);
      // Energy is uniform across a bin, which spans [k - 0.5, k + 0.5].
      const float qa = std::clamp((float)ia - 0.5f + (u - dA.cdf[ia - 1]) / mA_, 1.0f, (float)(kNumBins - 1));
      const float qb = std::clamp((float)ib - 0.5f + (u - dB.cdf[ib - 1]) / mB_, 1.0f, (float)(kNumBins - 1));
      const float x = (1.0f - s) * qa + s * qb;

      const int ra = std::clamp((int)lroundf(qa), 1, kNumBins - 1);
      const int rb = std::clamp((int)lroundf(qb), 1, kNumBins - 1);
      // The component's true frequency: its position plus the sub-bin offset
      // each source measured, morphed the same way.
      const float inst = x + (1.0f - s) * (dA.inst[ra] - (float)ra) + s * (dB.inst[rb] - (float)rb);

      // Whole-bin deposit: the sub-bin position lives in `inst` (the phase
      // advance), and keeping each peak's lobe compact keeps its bins
      // mutually consistent for overlap-add.
      const int j = std::clamp((int)lroundf(x), 1, kNumBins - 1);
      mHist[j] += w;
      mInstNum[j] += w * inst;
      mSrcA[j] += w * (float)ra;
      mSrcB[j] += w * (float)rb;
   }

   // 2. Output magnitude and instantaneous frequency per bin.
   float maxMag = 0.0f;
   for (int j = 1; j < kNumBins; j++)
   {
      const float h = mHist[j];
      if (h > 1e-9f)
      {
         mOutMag[j] = sqrtf(h * energy);
         mOutInst[j] = mInstNum[j] / h;
         maxMag = std::max(maxMag, mOutMag[j]);
      }
      else
      {
         mOutMag[j] = 0.0f;
         mOutInst[j] = (float)j;
      }
   }

   // 3. Peaks of the output, regions valley to valley, one phase accumulator
   //    per peak; the rest of a region keeps the source phase relationships.
   int peaks[256];
   int numPeaks = 0;
   const float thresh = std::max(maxMag * 0.003f, 1e-6f);
   for (int k = 2; k < kNumBins - 2 && numPeaks < 256; k++)
   {
      const float m = mOutMag[k];
      if (m > thresh && m > mOutMag[k - 1] && m >= mOutMag[k + 1] && m > mOutMag[k - 2] && m >= mOutMag[k + 2])
         peaks[numPeaks++] = k;
   }

   static thread_local float newPhase[kNumBins + 1];
   for (int k = 0; k <= kNumBins; k++)
      newPhase[k] = st.synthPhase[k];

   auto srcBin = [&](int j) {
      const float h = std::max(mHist[j], 1e-9f);
      const float v = (s < 0.5f ? mSrcA[j] : mSrcB[j]) / h;
      return std::clamp((int)lroundf(v), 1, kNumBins - 1);
   };

   for (int pi = 0; pi < numPeaks; pi++)
   {
      const int p = peaks[pi];
      int lo = 1;
      if (pi > 0)
      {
         int v = peaks[pi - 1];
         for (int k = peaks[pi - 1]; k <= p; k++)
            if (mOutMag[k] < mOutMag[v]) v = k;
         lo = v + (v < p ? 1 : 0);
      }
      int hi = kNumBins - 1;
      if (pi + 1 < numPeaks)
      {
         int v = p;
         for (int k = p; k <= peaks[pi + 1]; k++)
            if (mOutMag[k] < mOutMag[v]) v = k;
         hi = v;
      }
      lo = std::min(lo, p);
      hi = std::max(hi, p);

      const float pPhase = WrapPi(st.synthPhase[p] + hopAdv * mOutInst[p]);
      const float srcP = phaseSrc.phase[srcBin(p)];
      for (int k = lo; k <= hi; k++)
         newPhase[k] = WrapPi(pPhase + (phaseSrc.phase[srcBin(k)] - srcP));
   }

   // Bins outside any region (no peaks at all) run free at their own frequency.
   if (numPeaks == 0)
      for (int k = 1; k < kNumBins; k++)
         newPhase[k] = WrapPi(st.synthPhase[k] + hopAdv * mOutInst[k]);

   for (int k = 1; k < kNumBins; k++)
   {
      mOutRe[k] = mOutMag[k] * cosf(newPhase[k]);
      mOutIm[k] = mOutMag[k] * sinf(newPhase[k]);
      st.synthPhase[k] = newPhase[k];
   }
}
