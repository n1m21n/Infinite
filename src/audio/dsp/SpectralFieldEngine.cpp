#include "SpectralFieldEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/DspMath.h"
#include "core/field/SampleRuntime.h"

namespace
{
   constexpr double kTwoPiD = 6.28318530717958647692;
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr float kPi = 3.14159265358979323846f;
   constexpr int kStride = Field::kSpectralMaxStateCells;
   constexpr float kMagCeiling = 1000.0f; // +60 dB over a full-scale sine

   inline float WrapPi(float x)
   {
      x = fmodf(x + kPi, kTwoPi);
      if (x < 0.0f)
         x += kTwoPi;
      return x - kPi;
   }

   inline bool Finite(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }
}

void SpectralFieldEngine::Prepare(double sampleRate)
{
   mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
   mFft.Prepare(kMaxLog2);
   mWindow.assign(kMaxSize, 0.0f);
   mTime.assign(kMaxSize, 0.0f);
   mRe.assign(kMaxSize / 2, 0.0f);
   mIm.assign(kMaxSize / 2, 0.0f);
   for (Channel& c : mCh)
   {
      c.fifo.assign(kMaxSize, 0.0f);
      c.bfifo.assign(kMaxSize, 0.0f);
      c.ola.assign(kOlaSize, 0.0f);
      for (std::vector<float>* v : { &c.mag, &c.phase, &c.freq, &c.prevPhase, &c.bmag, &c.bphase, &c.bfreq,
                                     &c.bprevPhase, &c.outMag, &c.outPhase, &c.outFreq })
         v->assign(kMaxBins, 0.0f);
      c.synthPhase.assign(kMaxBins, 0.0);
      c.moved.assign(kMaxBins, 0);
      c.stateA.assign((size_t)kMaxBins * kStride, 0.0f);
      c.stateB.assign((size_t)kMaxBins * kStride, 0.0f);
      c.statePrev = c.stateA.data();
      c.stateNext = c.stateB.data();
   }
   mPeaks.assign(kMaxBins, 0);
   const int log2 = mLog2;
   mLog2 = -1; // force the size setup
   SetSizeLog2(log2);
}

void SpectralFieldEngine::SetSizeLog2(int log2Size)
{
   log2Size = std::clamp(log2Size, kMinLog2, kMaxLog2);
   if (log2Size == mLog2)
      return;
   mLog2 = log2Size;
   mSize = 1 << log2Size;
   mHop = mSize / 4;
   mBins = mSize / 2 + 1;
   for (int n = 0; n < mSize; n++)
      mWindow[(size_t)n] = 0.5f * (1.0f - cosf((float)(kTwoPiD * (double)n / (double)mSize)));

   const double binhz = mSampleRate / (double)mSize;
   const double fLo = 20.0, fHi = mSampleRate * 0.5;
   for (int b = 0; b < kBands; b++)
   {
      const double f0 = fLo * std::pow(fHi / fLo, (double)b / kBands);
      const double f1 = fLo * std::pow(fHi / fLo, (double)(b + 1) / kBands);
      int lo = (int)std::floor(f0 / binhz);
      int hi = (int)std::ceil(f1 / binhz) - 1;
      lo = std::clamp(lo, 0, mBins - 1);
      hi = std::clamp(std::max(hi, lo), 0, mBins - 1);
      mBandLo[b] = lo;
      mBandHi[b] = hi;
   }
   Reset();
}

void SpectralFieldEngine::Reset()
{
   for (Channel& c : mCh)
   {
      if (c.fifo.empty())
         continue; // not prepared
      std::fill(c.fifo.begin(), c.fifo.end(), 0.0f);
      std::fill(c.bfifo.begin(), c.bfifo.end(), 0.0f);
      std::fill(c.ola.begin(), c.ola.end(), 0.0f);
      std::fill(c.prevPhase.begin(), c.prevPhase.end(), 0.0f);
      std::fill(c.bprevPhase.begin(), c.bprevPhase.end(), 0.0f);
      std::fill(c.bmag.begin(), c.bmag.end(), 0.0f);
      std::fill(c.bphase.begin(), c.bphase.end(), 0.0f);
      std::fill(c.bfreq.begin(), c.bfreq.end(), 0.0f);
      std::fill(c.synthPhase.begin(), c.synthPhase.end(), 0.0);
      c.stateValid = false;
      c.lastProg = nullptr;
      c.firstFrame = true;
      // Start one hop short of a full frame: the first frame lands after one
      // hop and the latency is still exactly Size (x0 sits at Size - hop).
      c.fifoCount = mSize - mHop;
      c.olaRead = 0;
   }
   std::fill(mBandsIn, mBandsIn + kBands, 0.0f);
   std::fill(mBandsOut, mBandsOut + kBands, 0.0f);
}

float SpectralFieldEngine::Process(int ch, float x, float b, const Field::SampleProgram* prog, const float* params)
{
   Channel& c = mCh[ch];
   constexpr int olaMask = kOlaSize - 1;
   const float y = DspMath::FlushDenormal(c.ola[(size_t)c.olaRead]);
   c.ola[(size_t)c.olaRead] = 0.0f;
   c.olaRead = (c.olaRead + 1) & olaMask;

   c.fifo[(size_t)c.fifoCount] = x;
   c.bfifo[(size_t)c.fifoCount] = b;
   c.fifoCount++;
   if (c.fifoCount >= mSize)
   {
      Frame(c, ch, prog, params);
      const size_t keep = (size_t)(mSize - mHop) * sizeof(float);
      std::memmove(c.fifo.data(), c.fifo.data() + mHop, keep);
      std::memmove(c.bfifo.data(), c.bfifo.data() + mHop, keep);
      c.fifoCount = mSize - mHop;
   }
   return y;
}

void SpectralFieldEngine::Analyse(const float* fifo, float* mag, float* phase, float* freq, float* prevPhase, bool first)
{
   const int n2 = mSize / 2;
   for (int n = 0; n < mSize; n++)
      mTime[(size_t)((n + n2) & (mSize - 1))] = fifo[n] * mWindow[(size_t)n];
   mFft.Forward(mTime.data(), mLog2, mRe.data(), mIm.data());

   // PortableFft's bins are x2 the DFT; a Hann-windowed sine of amplitude A
   // peaks at A * Size / 4 in the DFT. One factor maps both to mag = A.
   const float magScale = 0.5f * 4.0f / (float)mSize;
   const float hopAdv = kTwoPi * (float)mHop / (float)mSize;
   const float binhz = (float)(mSampleRate / (double)mSize);

   for (int k = 0; k < mBins; k++)
   {
      float re, im;
      if (k == 0) { re = mRe[0]; im = 0.0f; }
      else if (k == n2) { re = mIm[0]; im = 0.0f; } // zrip packing: Nyquist in imag[0]
      else { re = mRe[(size_t)k]; im = mIm[(size_t)k]; }
      const float m = sqrtf(re * re + im * im) * magScale;
      const float ph = atan2f(im, re);
      float f = (float)k * binhz;
      if (!first)
      {
         const float dev = WrapPi(ph - prevPhase[k] - (float)k * hopAdv);
         f = ((float)k + dev / hopAdv) * binhz;
      }
      prevPhase[k] = ph;
      mag[k] = m;
      phase[k] = ph;
      freq[k] = f;
   }
}

void SpectralFieldEngine::SyncState(Channel& c, const Field::SampleProgram* prog)
{
   if (c.stateValid && prog == c.lastProg && !c.swapPending)
      return;
   const int cells = (int)prog->state.size();
   const bool canTransplant = c.stateValid;
   for (int k = 0; k < mBins; k++)
   {
      const float* src = c.statePrev + (size_t)k * kStride;
      float* dst = c.stateNext + (size_t)k * kStride;
      for (int i = 0; i < cells; i++)
      {
         const Field::SampleStateInit& cell = prog->state[(size_t)i];
         const int from = cell.transplantFromIndex;
         dst[i] = (canTransplant && from >= 0 && from < kStride) ? src[from] : cell.initialValue;
      }
   }
   std::swap(c.statePrev, c.stateNext);
   c.lastProg = prog;
   c.stateValid = true;
   c.swapPending = false;
}

void SpectralFieldEngine::Frame(Channel& c, int ch, const Field::SampleProgram* prog, const float* params)
{
   const bool first = c.firstFrame;
   c.firstFrame = false;
   Analyse(c.fifo.data(), c.mag.data(), c.phase.data(), c.freq.data(), c.prevPhase.data(), first);

   const bool run = prog != nullptr && prog->valid && prog->spectral;
   if (run && prog->hasSpectralInput)
      Analyse(c.bfifo.data(), c.bmag.data(), c.bphase.data(), c.bfreq.data(), c.bprevPhase.data(), first);

   const float binhz = (float)(mSampleRate / (double)mSize);
   bool raw = false;
   if (run)
   {
      SyncState(c, prog);
      Field::SpectralBinInput sp;
      sp.mag = c.mag.data();
      sp.phase = c.phase.data();
      sp.freq = c.freq.data();
      sp.bmag = c.bmag.data();
      sp.bphase = c.bphase.data();
      sp.bfreq = c.bfreq.data();
      sp.statePrevBank = c.statePrev;
      sp.stateStride = kStride;
      sp.bins = mBins;
      sp.binhz = binhz;
      sp.hop = (float)mHop;

      Field::SampleRuntimeInput in;
      in.sr = (float)mSampleRate;
      in.rng = &c.rng;
      in.paramVals = params;
      in.spec = &sp;
      float regs[Field::kSampleMaxRegs];
      const int magR = prog->magOutReg, phR = prog->phaseOutReg, frR = prog->freqOutReg;
      for (int k = 0; k < mBins; k++)
      {
         in.bin = k;
         in.stateCur = c.statePrev + (size_t)k * kStride;
         in.stateNext = c.stateNext + (size_t)k * kStride;
         Field::RunSampleProgram(*prog, in, regs);
         c.outMag[k] = regs[magR];
         c.outPhase[k] = regs[phR];
         c.outFreq[k] = regs[frR];
      }
      std::swap(c.statePrev, c.stateNext);
      raw = prog->writesPhase;
   }
   else
   {
      std::memcpy(c.outMag.data(), c.mag.data(), (size_t)mBins * sizeof(float));
      std::memcpy(c.outPhase.data(), c.phase.data(), (size_t)mBins * sizeof(float));
      std::memcpy(c.outFreq.data(), c.freq.data(), (size_t)mBins * sizeof(float));
   }

   // Sanitize: non-finite -> 0; a negative mag is the same bin turned by pi.
   for (int k = 0; k < mBins; k++)
   {
      float m = c.outMag[k];
      float ph = c.outPhase[k];
      if (!Finite(m)) m = 0.0f;
      if (!Finite(ph)) ph = 0.0f;
      if (!Finite(c.outFreq[k])) c.outFreq[k] = c.freq[k];
      if (m < 0.0f) { m = -m; ph += kPi; c.moved[k] = 2; } else c.moved[k] = 0;
      c.outMag[k] = std::min(m, kMagCeiling);
      c.outPhase[k] = ph;
   }

   if (!raw)
   {
      // Vocoder: untouched bins keep the analysis phase (exact); moved bins
      // advance from last frame's synthesis phase at their new frequency.
      const double adv = kTwoPiD * (double)mHop / mSampleRate;
      for (int k = 0; k < mBins; k++)
      {
         const float flip = (c.moved[k] == 2) ? kPi : 0.0f;
         if (c.outFreq[k] == c.freq[k])
         {
            c.moved[k] = 0;
            c.outPhase[k] = c.phase[k] + flip;
         }
         else
         {
            c.moved[k] = 1;
            double ph = c.synthPhase[k] + adv * (double)c.outFreq[k];
            ph -= kTwoPiD * std::floor(ph / kTwoPiD);
            c.outPhase[k] = (float)ph + flip;
         }
      }
      // Identity phase locking over each run of moved bins: every bin takes
      // its region's peak phase (zero-phase framing makes a sinusoid's
      // main-lobe bins share one phase).
      int k = 0;
      while (k < mBins)
      {
         if (c.moved[k] != 1) { k++; continue; }
         const int s = k;
         while (k < mBins && c.moved[k] == 1) k++;
         const int e = k - 1;
         int np = 0;
         for (int j = s; j <= e; j++)
         {
            const float m = c.outMag[j];
            const bool left = (j == s) || m > c.outMag[j - 1];
            const bool right = (j == e) || m >= c.outMag[j + 1];
            if (left && right && m > 0.0f)
               mPeaks[(size_t)np++] = j;
         }
         if (np == 0)
            continue;
         int lo = s;
         for (int pi = 0; pi < np; pi++)
         {
            const int p = mPeaks[(size_t)pi];
            int hi = e;
            if (pi + 1 < np)
            {
               hi = p;
               for (int j = p; j <= mPeaks[(size_t)pi + 1]; j++)
                  if (c.outMag[j] < c.outMag[hi]) hi = j;
               if (hi == mPeaks[(size_t)pi + 1]) hi--;
            }
            const float php = c.outPhase[p];
            for (int j = lo; j <= hi; j++)
               c.outPhase[j] = php;
            lo = hi + 1;
         }
      }
   }

   for (int k = 0; k < mBins; k++)
      c.synthPhase[k] = (double)c.outPhase[k];

   // Rebuild the packed spectrum at DFT scale, inverse, window, overlap-add.
   const int n2 = mSize / 2;
   const float synthScale = (float)mSize / 4.0f;
   for (int k = 1; k < n2; k++)
   {
      const float m = c.outMag[k] * synthScale;
      mRe[(size_t)k] = m * cosf(c.outPhase[k]);
      mIm[(size_t)k] = m * sinf(c.outPhase[k]);
   }
   mRe[0] = c.outMag[0] * synthScale * cosf(c.outPhase[0]);
   mIm[0] = c.outMag[n2] * synthScale * cosf(c.outPhase[n2]);
   mFft.Inverse(mRe.data(), mIm.data(), mLog2, mTime.data());

   constexpr int olaMask = kOlaSize - 1;
   const float normScale = 1.0f / ((float)mSize * 1.5f);
   for (int n = 0; n < mSize; n++)
      c.ola[(size_t)((c.olaRead + n) & olaMask)] +=
         mTime[(size_t)((n + n2) & (mSize - 1))] * mWindow[(size_t)n] * normScale;

   if (ch == 0)
      UpdateBands(c);
}

void SpectralFieldEngine::UpdateBands(const Channel& c)
{
   for (int b = 0; b < kBands; b++)
   {
      float mi = 0.0f, mo = 0.0f;
      for (int k = mBandLo[b]; k <= mBandHi[b]; k++)
      {
         mi = std::max(mi, c.mag[k]);
         mo = std::max(mo, c.outMag[k]);
      }
      mBandsIn[b] = mi;
      mBandsOut[b] = mo;
   }
   mFrames++;
}
