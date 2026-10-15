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

void KeySnapKernel::ChannelState::Init()
{
   inputFifo.assign(kFftSize, 0.0f);
   outputOla.assign(kOlaSize, 0.0f);
   phasePrev.assign(kNumBins + 1, 0.0f);
   synthPhase.assign(kNumBins + 1, 0.0f);
   synthPhaseNext.assign(kNumBins + 1, 0.0f);
   glideSt.assign(kNumBins + 1, 0.0f);
   glideStNext.assign(kNumBins + 1, 0.0f);
   hasSynth.assign(kNumBins + 1, 0);
   hasSynthNext.assign(kNumBins + 1, 0);
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
   mFft.Prepare(kLog2Fft);

   mWindow.resize(kFftSize);
   for (int n = 0; n < kFftSize; n++)
      mWindow[n] = 0.5f * (1.0f - cosf((float)(2.0 * M_PI * (double)n / (double)kFftSize)));

   mRe.assign(kNumBins, 0.0f);
   mIm.assign(kNumBins, 0.0f);
   mOutRe.assign(kNumBins, 0.0f);
   mOutIm.assign(kNumBins, 0.0f);
   mTime.assign(kFftSize, 0.0f);
   mMag.assign(kNumBins + 1, 0.0f);
   mPhaseA.assign(kNumBins + 1, 0.0f);
   mInstBin.assign(kNumBins + 1, 0.0f);

   for (int ch = 0; ch < kMaxChannels; ch++)
      mChannels[ch].Init();
}

void KeySnapKernel::Reset()
{
   for (int ch = 0; ch < kMaxChannels; ch++)
      mChannels[ch].Reset();
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
   const float normScale = 1.0f / ((float)kFftSize * 1.5f);
   const int olaMask = kOlaSize - 1;

   for (int ch = 0; ch < numChannels; ch++)
   {
      ChannelState& st = mChannels[ch];
      const float* inData = in.channels[ch];
      float* outData = out.channels[ch];

      for (int i = 0; i < out.numFrames; i++)
      {
         // Pop first so the latency is exactly kFftSize.
         outData[i] = DspMath::FlushDenormal(st.outputOla[st.olaRead]);
         st.outputOla[st.olaRead] = 0.0f;
         st.olaRead = (st.olaRead + 1) & olaMask;

         st.inputFifo[st.fifoSamples++] = inData[i];
         if (st.fifoSamples >= kFftSize)
         {
            for (int n = 0; n < kFftSize; n++)
               mTime[n] = st.inputFifo[n] * mWindow[n];
            mFft.Forward(mTime.data(), kLog2Fft, mRe.data(), mIm.data());
            for (int k = 0; k < kNumBins; k++)
            {
               mRe[k] *= 0.5f;
               mIm[k] *= 0.5f;
            }

            ProcessFrame(st);

            mFft.Inverse(mOutRe.data(), mOutIm.data(), kLog2Fft, mTime.data());
            for (int n = 0; n < kFftSize; n++)
               st.outputOla[(st.olaRead + n) & olaMask] += mTime[n] * mWindow[n] * normScale;

            std::memmove(st.inputFifo.data(), st.inputFifo.data() + kHopSize, (kFftSize - kHopSize) * sizeof(float));
            st.fifoSamples = kFftSize - kHopSize;
         }
      }
   }

   for (int ch = numChannels; ch < out.numChannels; ch++)
      for (int i = 0; i < out.numFrames; i++)
         out.channels[ch][i] = 0.0f;
}

// Analyse mRe/mIm, correct every peak, write the result to mOutRe/mOutIm.
void KeySnapKernel::ProcessFrame(ChannelState& st)
{
   const float snap = mSnap.load(std::memory_order_relaxed);
   const float glideSec = mGlideSec.load(std::memory_order_relaxed);
   const uint32_t mask = mMask.load(std::memory_order_relaxed);
   const float sr = (float)mSampleRate;
   const float hopPhaseAdv = kTwoPi * (float)kHopSize / (float)kFftSize;
   const float glideA = glideSec <= 0.0f ? 1.0f : 1.0f - expf(-(float)kHopSize / (sr * glideSec));

   // 1. Magnitude, phase and instantaneous frequency (in bins) per bin.
   float maxMag = 0.0f;
   for (int k = 1; k < kNumBins; k++)
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

   // DC and Nyquist pass through untouched.
   mOutRe[0] = mRe[0];
   mOutIm[0] = mIm[0];
   std::fill(mOutRe.begin() + 1, mOutRe.end(), 0.0f);
   std::fill(mOutIm.begin() + 1, mOutIm.end(), 0.0f);

   // 2. Peaks.
   int peaks[kMaxPeaks];
   int numPeaks = 0;
   const float thresh = std::max(maxMag * kPeakRelFloor, kPeakAbsFloor);
   for (int k = 2; k < kNumBins - 2 && numPeaks < kMaxPeaks; k++)
   {
      const float m = mMag[k];
      if (m > thresh && m > mMag[k - 1] && m >= mMag[k + 1] && m > mMag[k - 2] && m >= mMag[k + 2])
         peaks[numPeaks++] = k;
   }

   std::fill(st.hasSynthNext.begin(), st.hasSynthNext.end(), (uint8_t)0);

   if (numPeaks == 0)
   {
      for (int k = 1; k < kNumBins; k++)
      {
         mOutRe[k] = mRe[k];
         mOutIm[k] = mIm[k];
      }
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
      int hi = kNumBins - 1;
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
      const float hz = inst * sr / (float)kFftSize;
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
            if (b >= 1 && b < kNumBins && st.hasSynth[b])
            {
               prevSt = st.glideSt[b];
               hadPrev = true;
               break;
            }
         }
      }
      const float smoothSt = prevSt + glideA * (targetSt - prevSt);
      const float ratio = exp2f(smoothSt / 12.0f);

      const int pTarget = std::clamp((int)std::lround(inst * ratio), 1, kNumBins - 1);
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
            if (b >= 1 && b < kNumBins && st.hasSynth[b])
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
         if (kt < 1 || kt >= kNumBins)
            continue;
         mOutRe[kt] += mRe[k] * rc - mIm[k] * rs;
         mOutIm[kt] += mRe[k] * rs + mIm[k] * rc;
      }

      st.synthPhaseNext[pTarget] = newPhase;
      st.glideStNext[pTarget] = smoothSt;
      st.hasSynthNext[pTarget] = 1;
   }

   st.synthPhase.swap(st.synthPhaseNext);
   st.glideSt.swap(st.glideStNext);
   st.hasSynth.swap(st.hasSynthNext);
}
