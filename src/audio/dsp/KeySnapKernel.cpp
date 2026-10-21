#include "KeySnapKernel.h"

#include <algorithm>
#include <cstring>

#include "audio/MusicTime.h"
#include "core/Transport.h"
#include "nodes/AudioEffectNode.h"

namespace
{
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr float kMinHz = 40.0f;
   constexpr float kMaxHz = 8000.0f;
   // A peak must be within 40 dB of the frame's strongest, and above an
   // absolute floor, to be corrected. Quieter content passes through.
   constexpr float kPeakRelFloor = 0.01f;
   constexpr float kPeakAbsFloor = 1e-3f;

   inline float WrapPi(float x)
   {
      while (x > 3.14159265f) x -= kTwoPi;
      while (x <= -3.14159265f) x += kTwoPi;
      return x;
   }
}

void KeySnapKernel::ChannelState::Init(int fftSize, int olaSize)
{
   const int numBins = fftSize / 2;
   inputFifo.assign(fftSize, 0.0f);
   outputOla.assign(olaSize, 0.0f);
   phasePrev.assign(numBins + 1, 0.0f);
   synthPhase.assign(numBins + 1, 0.0f);
   synthPhaseNext.assign(numBins + 1, 0.0f);
   glideSt.assign(numBins + 1, 0.0f);
   glideStNext.assign(numBins + 1, 0.0f);
   hasSynth.assign(numBins + 1, 0);
   hasSynthNext.assign(numBins + 1, 0);
   fifoSamples = 0;
   olaRead = 0;
}

void KeySnapKernel::ChannelState::Reset()
{
   std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
   std::fill(outputOla.begin(), outputOla.end(), 0.0f);
   std::fill(phasePrev.begin(), phasePrev.end(), 0.0f);
   std::fill(hasSynth.begin(), hasSynth.end(), (uint8_t)0);
   std::fill(hasSynthNext.begin(), hasSynthNext.end(), (uint8_t)0);
   fifoSamples = 0;
   olaRead = 0;
}

uint32_t KeySnapKernel::ScaleMask(int scale, int root)
{
   const MusicTime::ScaleDef& def = MusicTime::ScaleTable(scale);
   uint32_t mask = 0;
   for (int i = 0; i < def.count; i++)
      mask |= 1u << (((def.intervals[i] + root) % 12 + 12) % 12);
   return mask;
}

float KeySnapKernel::NearestScaleNote(float midi, uint32_t mask)
{
   if ((mask & 0xFFFu) == 0)
      return midi;
   const int base = (int)std::lround(midi);
   float best = midi;
   float bestDist = 1e9f;
   for (int n = base - 6; n <= base + 6; n++)
   {
      const int pc = ((n % 12) + 12) % 12;
      if (!(mask & (1u << pc)))
         continue;
      const float d = std::fabs((float)n - midi);
      if (d < bestDist)
      {
         bestDist = d;
         best = (float)n;
      }
   }
   return best;
}

void KeySnapKernel::PrepareToPlay(double sampleRate, int /*maxBlockSize*/)
{
   mSampleRate = sampleRate;
   // Same frame length and bin width in seconds/Hz at every rate: 2048 at
   // 44.1/48 kHz, 4096 at 88.2/96, 8192 above. A fixed 2048 halved both at
   // 96 kHz, so peaks split differently and 6 kHz read 3.3 dB apart.
   const int rateFactor = sampleRate > 132000.0 ? 4 : sampleRate > 66000.0 ? 2 : 1;
   mLog2Fft = kBaseLog2Fft + (rateFactor == 4 ? 2 : rateFactor == 2 ? 1 : 0);
   mFftSize = 1 << mLog2Fft;
   mHopSize = mFftSize / 4;
   mNumBins = mFftSize / 2;
   mOlaSize = mFftSize * 2;
   mFft.Prepare(mLog2Fft);

   mWindow.resize(mFftSize);
   for (int n = 0; n < mFftSize; n++)
      mWindow[n] = 0.5f * (1.0f - cosf((float)(2.0 * M_PI * (double)n / (double)mFftSize)));

   mRe.assign(mNumBins, 0.0f);
   mIm.assign(mNumBins, 0.0f);
   mOutRe.assign(mNumBins, 0.0f);
   mOutIm.assign(mNumBins, 0.0f);
   mTime.assign(mFftSize, 0.0f);
   mMag.assign(mNumBins + 1, 0.0f);
   mPhaseA.assign(mNumBins + 1, 0.0f);
   mInstBin.assign(mNumBins + 1, 0.0f);

   for (int ch = 0; ch < kMaxChannels; ch++)
   {
      mChannels[ch].Init(mFftSize, mOlaSize);
      mChRe[ch].assign(mNumBins, 0.0f);
      mChIm[ch].assign(mNumBins, 0.0f);
   }
   mMid.Init(mFftSize, mOlaSize);
   mMapTarget.assign(mNumBins, -1);
   mMapRc.assign(mNumBins, 1.0f);
   mMapRs.assign(mNumBins, 0.0f);
}

void KeySnapKernel::Reset()
{
   for (int ch = 0; ch < kMaxChannels; ch++)
      mChannels[ch].Reset();
   mMid.Reset();
}

void KeySnapKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   int scale = (int)std::lround(node.Param("scale"));
   int root = (int)std::lround(node.Param("root"));
   if (node.Param("globalKey") > 0.5f)
   {
      scale = Transport::Instance().Scale();
      root = Transport::Instance().Key();
   }
   scale = std::clamp(scale, 0, (int)MusicTime::kNumScaleTypes - 1);
   root = ((root % 12) + 12) % 12;

   mSnap.store(std::clamp(node.Param("snap"), 0.0f, 1.0f), std::memory_order_relaxed);
   mGlideSec.store(std::clamp(node.Param("glide"), 0.0f, 1000.0f) * 0.001f, std::memory_order_relaxed);
   mMask.store(ScaleMask(scale, root), std::memory_order_relaxed);
}

void KeySnapKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min({ in.numChannels, out.numChannels, kMaxChannels });
   const float normScale = 1.0f / ((float)mFftSize * 1.5f);
   const int olaMask = mOlaSize - 1;

   for (int i = 0; i < out.numFrames; i++)
   {
      // Pop first so the latency is exactly mFftSize. Every channel's FIFO
      // fills on the same sample, so their frames are processed together.
      for (int ch = 0; ch < numChannels; ch++)
      {
         ChannelState& st = mChannels[ch];
         out.channels[ch][i] = DspMath::FlushDenormal(st.outputOla[st.olaRead]);
         st.outputOla[st.olaRead] = 0.0f;
         st.olaRead = (st.olaRead + 1) & olaMask;
         st.inputFifo[st.fifoSamples++] = in.channels[ch][i];
      }
      if (numChannels == 0 || mChannels[0].fifoSamples < mFftSize)
         continue;

      // Every channel's spectrum, and their average (the mid).
      std::fill(mRe.begin(), mRe.end(), 0.0f);
      std::fill(mIm.begin(), mIm.end(), 0.0f);
      const float avg = 1.0f / (float)numChannels;
      for (int ch = 0; ch < numChannels; ch++)
      {
         ChannelState& st = mChannels[ch];
         for (int n = 0; n < mFftSize; n++)
            mTime[n] = st.inputFifo[n] * mWindow[n];
         mFft.Forward(mTime.data(), mLog2Fft, mChRe[ch].data(), mChIm[ch].data());
         for (int k = 0; k < mNumBins; k++)
         {
            mChRe[ch][k] *= 0.5f;
            mChIm[ch][k] *= 0.5f;
            mRe[k] += mChRe[ch][k] * avg;
            mIm[k] += mChIm[ch][k] * avg;
         }
      }

      ProcessFrame(mMid); // decides the mapping on the mid

      for (int ch = 0; ch < numChannels; ch++)
      {
         ChannelState& st = mChannels[ch];
         ApplyMapping(mChRe[ch].data(), mChIm[ch].data());
         mFft.Inverse(mOutRe.data(), mOutIm.data(), mLog2Fft, mTime.data());
         for (int n = 0; n < mFftSize; n++)
            st.outputOla[(st.olaRead + n) & olaMask] += mTime[n] * mWindow[n] * normScale;
         std::memmove(st.inputFifo.data(), st.inputFifo.data() + mHopSize, (mFftSize - mHopSize) * sizeof(float));
         st.fifoSamples = mFftSize - mHopSize;
      }
   }

   for (int ch = numChannels; ch < out.numChannels; ch++)
      for (int i = 0; i < out.numFrames; i++)
         out.channels[ch][i] = 0.0f;
}

void KeySnapKernel::ApplyMapping(const float* re, const float* im)
{
   // DC and Nyquist pass through untouched.
   mOutRe[0] = re[0];
   mOutIm[0] = im[0];
   if (mMapPassthrough)
   {
      for (int k = 1; k < mNumBins; k++)
      {
         mOutRe[k] = re[k];
         mOutIm[k] = im[k];
      }
      return;
   }
   std::fill(mOutRe.begin() + 1, mOutRe.end(), 0.0f);
   std::fill(mOutIm.begin() + 1, mOutIm.end(), 0.0f);
   for (int k = 1; k < mNumBins; k++)
   {
      const int kt = mMapTarget[k];
      if (kt < 1)
         continue;
      mOutRe[kt] += re[k] * mMapRc[k] - im[k] * mMapRs[k];
      mOutIm[kt] += re[k] * mMapRs[k] + im[k] * mMapRc[k];
   }
}

// Analyse mRe/mIm (the mid), correct every peak, and record the result as a
// per-source-bin mapping (mMapTarget/mMapRc/mMapRs) for ApplyMapping.
void KeySnapKernel::ProcessFrame(ChannelState& st)
{
   const float snap = mSnap.load(std::memory_order_relaxed);
   const float glideSec = mGlideSec.load(std::memory_order_relaxed);
   const uint32_t mask = mMask.load(std::memory_order_relaxed);
   const float sr = (float)mSampleRate;
   const float hopPhaseAdv = kTwoPi * (float)mHopSize / (float)mFftSize;
   const float glideA = glideSec <= 0.0f ? 1.0f : 1.0f - expf(-(float)mHopSize / (sr * glideSec));

   // 1. Magnitude, phase and instantaneous frequency (in bins) per bin.
   float maxMag = 0.0f;
   for (int k = 1; k < mNumBins; k++)
   {
      const float re = mRe[k];
      const float im = mIm[k];
      const float mag = sqrtf(re * re + im * im);
      const float phase = atan2f(im, re);
      const float dev = WrapPi(phase - st.phasePrev[k] - (float)k * hopPhaseAdv);
      st.phasePrev[k] = phase;
      mMag[k] = mag;
      mPhaseA[k] = phase;
      mInstBin[k] = (float)k + dev / hopPhaseAdv;
      maxMag = std::max(maxMag, mag);
   }

   std::fill(mMapTarget.begin(), mMapTarget.end(), -1);
   mMapPassthrough = false;

   // 2. Peaks.
   int peaks[kMaxPeaks];
   int numPeaks = 0;
   const float thresh = std::max(maxMag * kPeakRelFloor, kPeakAbsFloor);
   for (int k = 2; k < mNumBins - 2 && numPeaks < kMaxPeaks; k++)
   {
      const float m = mMag[k];
      if (m > thresh && m > mMag[k - 1] && m >= mMag[k + 1] && m > mMag[k - 2] && m >= mMag[k + 2])
         peaks[numPeaks++] = k;
   }

   std::fill(st.hasSynthNext.begin(), st.hasSynthNext.end(), (uint8_t)0);

   if (numPeaks == 0)
   {
      mMapPassthrough = true;
      st.hasSynth.swap(st.hasSynthNext);
      return;
   }

   // 3. Per peak: region, target, shift, phase.
   for (int pi = 0; pi < numPeaks; pi++)
   {
      const int p = peaks[pi];

      // Region = bins from the valley below to the valley above this peak.
      int lo = 1;
      if (pi > 0)
      {
         int v = peaks[pi - 1];
         for (int k = peaks[pi - 1]; k <= p; k++)
            if (mMag[k] < mMag[v]) v = k;
         lo = v;
      }
      int hi = mNumBins - 1;
      if (pi + 1 < numPeaks)
      {
         int v = p;
         for (int k = p; k <= peaks[pi + 1]; k++)
            if (mMag[k] < mMag[v]) v = k;
         hi = v;
      }
      if (lo > p) lo = p;
      if (hi < p) hi = p;

      // Target offset in semitones.
      const float inst = mInstBin[p];
      const float hz = inst * sr / (float)mFftSize;
      float targetSt = 0.0f;
      if (hz >= kMinHz && hz <= kMaxHz)
      {
         const float midi = 69.0f + 12.0f * log2f(hz / 440.0f);
         targetSt = (KeySnapKernel::NearestScaleNote(midi, mask) - midi) * snap;
      }

      // Glide: continue from last frame's offset at (or next to) this bin.
      float prevSt = targetSt;
      bool hadPrev = false;
      for (int d = 0; d <= 1 && !hadPrev; d++)
      {
         for (int s = -d; s <= d; s += (d == 0 ? 1 : 2))
         {
            const int b = p + s;
            if (b >= 1 && b < mNumBins && st.hasSynth[b])
            {
               prevSt = st.glideSt[b];
               hadPrev = true;
               break;
            }
         }
      }
      const float smoothSt = prevSt + glideA * (targetSt - prevSt);
      const float ratio = exp2f(smoothSt / 12.0f);

      const int pTarget = std::clamp((int)std::lround(inst * ratio), 1, mNumBins - 1);
      const int shift = pTarget - p;

      // Synthesis phase of the shifted peak: advance last frame's phase there
      // by hop * shifted frequency; with no history, start from analysis phase.
      float prevPhase = mPhaseA[p];
      bool havePhase = false;
      for (int d = 0; d <= 1 && !havePhase; d++)
      {
         for (int s = -d; s <= d; s += (d == 0 ? 1 : 2))
         {
            const int b = pTarget + s;
            if (b >= 1 && b < mNumBins && st.hasSynth[b])
            {
               prevPhase = st.synthPhase[b];
               havePhase = true;
               break;
            }
         }
      }
      const float newPhase = havePhase ? WrapPi(prevPhase + hopPhaseAdv * inst * ratio) : mPhaseA[p];

      // Identity phase locking: rotate every bin of the region by the same
      // angle and move it by `shift` bins.
      const float theta = newPhase - mPhaseA[p];
      const float rc = cosf(theta);
      const float rs = sinf(theta);
      for (int k = lo; k <= hi; k++)
      {
         const int kt = k + shift;
         if (kt < 1 || kt >= mNumBins)
            continue;
         // Last region to claim a shared valley bin wins - as before, where
         // the bin was summed into both; this keeps it counted once.
         mMapTarget[k] = kt;
         mMapRc[k] = rc;
         mMapRs[k] = rs;
      }

      st.synthPhaseNext[pTarget] = newPhase;
      st.glideStNext[pTarget] = smoothSt;
      st.hasSynthNext[pTarget] = 1;
   }

   st.synthPhase.swap(st.synthPhaseNext);
   st.glideSt.swap(st.glideStNext);
   st.hasSynth.swap(st.hasSynthNext);
}
