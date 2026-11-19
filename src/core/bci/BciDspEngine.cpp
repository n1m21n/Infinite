#include "BciDspEngine.h"

#include "platform/AppPaths.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <sstream>

namespace Bci
{
namespace
{
   constexpr double kTwoPi = 6.28318530717958647692;

   inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

   double NowSteadySec()
   {
      return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   }

   // In-place radix-2 Cooley-Tukey FFT on `n` (power of 2) complex numbers.
   void Radix2Fft(std::vector<double>& re, std::vector<double>& im)
   {
      const int n = (int)re.size();
      int j = 0;
      for (int i = 1; i < n; ++i)
      {
         int bit = n >> 1;
         for (; j & bit; bit >>= 1)
            j ^= bit;
         j ^= bit;
         if (i < j)
         {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
         }
      }
      for (int len = 2; len <= n; len <<= 1)
      {
         const double ang = -kTwoPi / len;
         const double wlenRe = std::cos(ang);
         const double wlenIm = std::sin(ang);
         for (int i = 0; i < n; i += len)
         {
            double wRe = 1.0, wIm = 0.0;
            for (int k = 0; k < len / 2; ++k)
            {
               const int u = i + k;
               const int v = i + k + len / 2;
               const double tRe = re[v] * wRe - im[v] * wIm;
               const double tIm = re[v] * wIm + im[v] * wRe;
               re[v] = re[u] - tRe;
               im[v] = im[u] - tIm;
               re[u] += tRe;
               im[u] += tIm;
               const double nextWRe = wRe * wlenRe - wIm * wlenIm;
               wIm = wRe * wlenIm + wIm * wlenRe;
               wRe = nextWRe;
            }
         }
      }
   }

   float MedianOf(std::vector<float> v)
   {
      if (v.empty())
         return 0.0f;
      const size_t mid = v.size() / 2;
      std::nth_element(v.begin(), v.begin() + (ptrdiff_t)mid, v.end());
      return v[mid];
   }
}

void BciDspEngine::Biquad::SetHighPass(double fs, double fc, double q)
{
   const double w0 = kTwoPi * std::min(fc, fs * 0.45) / fs;
   const double cw = std::cos(w0), sw = std::sin(w0);
   const double alpha = sw / (2.0 * std::max(0.1, q));
   const double a0 = 1.0 + alpha;
   b0 = (1.0 + cw) * 0.5 / a0;
   b1 = -(1.0 + cw) / a0;
   b2 = (1.0 + cw) * 0.5 / a0;
   a1 = -2.0 * cw / a0;
   a2 = (1.0 - alpha) / a0;
}

void BciDspEngine::Biquad::SetLowPass(double fs, double fc, double q)
{
   const double w0 = kTwoPi * std::min(fc, fs * 0.45) / fs;
   const double cw = std::cos(w0), sw = std::sin(w0);
   const double alpha = sw / (2.0 * std::max(0.1, q));
   const double a0 = 1.0 + alpha;
   b0 = (1.0 - cw) * 0.5 / a0;
   b1 = (1.0 - cw) / a0;
   b2 = (1.0 - cw) * 0.5 / a0;
   a1 = -2.0 * cw / a0;
   a2 = (1.0 - alpha) / a0;
}

void BciDspEngine::Biquad::SetBandPass(double fs, double f0, double q)
{
   const double w0 = kTwoPi * std::min(f0, fs * 0.45) / fs;
   const double cw = std::cos(w0), sw = std::sin(w0);
   const double alpha = sw / (2.0 * std::max(0.1, q));
   const double a0 = 1.0 + alpha;
   b0 = alpha / a0;
   b1 = 0.0;
   b2 = -alpha / a0;
   a1 = -2.0 * cw / a0;
   a2 = (1.0 - alpha) / a0;
}

void BciDspEngine::Biquad::SetNotch(double fs, double f0, double q)
{
   const double w0 = kTwoPi * std::min(f0, fs * 0.45) / fs;
   const double cw = std::cos(w0), sw = std::sin(w0);
   const double alpha = sw / (2.0 * std::max(0.5, q));
   const double a0 = 1.0 + alpha;
   b0 = 1.0 / a0;
   b1 = -2.0 * cw / a0;
   b2 = 1.0 / a0;
   a1 = -2.0 * cw / a0;
   a2 = (1.0 - alpha) / a0;
}

float BciDspEngine::EuroState::Step(float val, double timeSec, float minCutoff, float beta)
{
   if (t < 0.0)
   {
      x = val;
      dx = 0.0f;
      t = timeSec;
      return x;
   }
   const float dt = (float)std::max(1e-3, timeSec - t);
   t = timeSec;
   auto alphaOf = [dt](float cutoff)
   {
      const float tau = 1.0f / (float)(kTwoPi * std::max(0.05f, cutoff));
      return 1.0f / (1.0f + tau / dt);
   };
   const float curDx = (val - x) / dt;
   dx += alphaOf(1.0f) * (curDx - dx);
   const float cutoff = minCutoff + beta * std::fabs(dx);
   x += alphaOf(cutoff) * (val - x);
   return x;
}

BciDspEngine::BciDspEngine()
{
   Reset(kSampleRateHz);
   LoadProfile();
}

void BciDspEngine::Reset(double sampleRateHz)
{
   mSampleRate = std::max(64.0, sampleRateHz);
   mStreamTimeSec = 0.0;
   mRingHead = 0;
   mSamplesSincePsd = 0;
   for (auto& ch : mRing)
      ch.fill(0.0f);
   std::fill(std::begin(mTraceFrontal), std::end(mTraceFrontal), 0.0f);
   std::fill(std::begin(mTraceTemporal), std::end(mTraceTemporal), 0.0f);
   mTraceDecim = 0;

   mBlinkInPulse = false;
   mBlinkPulseSamples = 0;
   mBlinkPeakUv = 0.0f;
   mLastBlinkEndSec = -10.0;
   mBlinkLatchUntilSec = -10.0;
   mBlinkRefractoryUntilSec = -10.0;
   mArtifactHoldUntilSec = -10.0;
   mBlinkFlash = mWinkFlash = mJawLFlash = mJawRFlash = 0.0f;
   mJawRmsSq[0] = mJawRmsSq[1] = 0.0;
   mBrowRmsSq = 0.0;
   mImuVelSmooth = 0.0;
   mPpgLp = 0.0;
   mPpgBaseline = 0.0;
   mPlvRe = 0.5;
   mPlvIm = 0.0;
   for (int c = 0; c < kChannelCount; ++c)
      mResRe[c] = mResIm[c] = 0.0;
   for (auto& e : mEuro)
      e = EuroState{};

   RebuildFilters();
}

void BciDspEngine::SetSampleRate(double sampleRateHz)
{
   if (std::fabs(sampleRateHz - mSampleRate) > 1.0 && sampleRateHz >= 64.0)
   {
      mSampleRate = sampleRateHz;
      RebuildFilters();
   }
}

void BciDspEngine::RebuildFilters()
{
   for (int c = 0; c < kChannelCount; ++c)
   {
      mHpDc[c].Reset();
      mHpDc[c].SetHighPass(mSampleRate, 0.5);
      mNotch50[c].Reset();
      mNotch50[c].SetNotch(mSampleRate, 50.0, 20.0);
      mNotch60[c].Reset();
      mNotch60[c].SetNotch(mSampleRate, 60.0, 20.0);
   }

   mBlinkHp.Reset(); mBlinkHp.SetHighPass(mSampleRate, 1.0);
   mBlinkLp.Reset(); mBlinkLp.SetLowPass(mSampleRate, 8.0);
   mSaccadeHp.Reset(); mSaccadeHp.SetHighPass(mSampleRate, 0.6);
   mSaccadeLp.Reset(); mSaccadeLp.SetLowPass(mSampleRate, 8.0);

   for (int i = 0; i < 2; ++i)
   {
      mJawHp[i].Reset(); mJawHp[i].SetHighPass(mSampleRate, 40.0);
      mJawLp[i].Reset(); mJawLp[i].SetLowPass(mSampleRate, 110.0);
   }
   mBrowHp.Reset(); mBrowHp.SetHighPass(mSampleRate, 30.0);
   mBrowLp.Reset(); mBrowLp.SetLowPass(mSampleRate, 75.0);
}

void BciDspEngine::StartCalibration()
{
   mCalibPhase = CalibrationPhase::EyesOpen;
   mCalibElapsedSec = 0.0;
   for (int c = 0; c < kChannelCount; ++c)
      mCalibChanSamples[c].clear();
   mCalibClosedAlphaBins.clear();
   mCalibOpenAlpha.clear();
   mCalibOpenBeta.clear();
   mCalibAlphaBinsAccumCount = 0;
   mCalibClosedPsdAccum.fill(0.0);
}

void BciDspEngine::CancelCalibration()
{
   mCalibPhase = CalibrationPhase::Idle;
   mCalibElapsedSec = 0.0;
}

std::string BciDspEngine::DefaultProfilePath()
{
   const std::string dir = AppPaths::AppSupportDir();
   if (dir.empty())
      return {};
   return dir + "/bci_profile.json";
}

bool BciDspEngine::SaveProfile(const std::string& path) const
{
   const std::string target = path.empty() ? DefaultProfilePath() : path;
   if (target.empty())
      return false;

   std::ostringstream oss;
   oss << "{\n"
       << "  \"valid\": " << (mProfile.valid ? "true" : "false") << ",\n"
       << "  \"iafHz\": " << mProfile.iafHz << ",\n"
       << "  \"medianBlinkUv\": " << mProfile.medianBlinkUv << ",\n"
       << "  \"medianJawUv\": " << mProfile.medianJawUv << ",\n"
       << "  \"baselineArousal\": " << mProfile.baselineArousal << ",\n"
       << "  \"baselineAlpha\": " << mProfile.baselineAlpha << ",\n"
       << "  \"baselineTheta\": " << mProfile.baselineTheta << ",\n"
       << "  \"baselineBeta\": " << mProfile.baselineBeta << ",\n"
       << "  \"chanMad\": [" << mProfile.chanMad[0] << ", " << mProfile.chanMad[1]
       << ", " << mProfile.chanMad[2] << ", " << mProfile.chanMad[3] << "]\n"
       << "}\n";

   std::string err;
   return AppPaths::WriteFileAtomic(target, oss.str(), err);
}

bool BciDspEngine::LoadProfile(const std::string& path)
{
   const std::string target = path.empty() ? DefaultProfilePath() : path;
   if (target.empty())
      return false;
   std::ifstream in(AppPaths::FsPath(target));
   if (!in)
      return false;

   std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
   auto parseFloat = [&](const char* key, float fallback) -> float
   {
      const std::string pat = std::string("\"") + key + "\":";
      const size_t pos = content.find(pat);
      if (pos == std::string::npos)
         return fallback;
      try
      {
         return std::stof(content.substr(pos + pat.size()));
      }
      catch (...)
      {
         return fallback;
      }
   };

   if (content.find("\"valid\": true") == std::string::npos)
      return false;

   CalibrationProfile prof;
   prof.valid = true;
   prof.iafHz = std::clamp(parseFloat("iafHz", 10.0f), 7.5f, 13.0f);
   prof.medianBlinkUv = std::clamp(parseFloat("medianBlinkUv", 135.0f), 60.0f, 400.0f);
   prof.medianJawUv = std::clamp(parseFloat("medianJawUv", 42.0f), 15.0f, 200.0f);
   prof.baselineArousal = std::clamp(parseFloat("baselineArousal", 0.0f), -2.0f, 2.0f);
   prof.baselineAlpha = std::clamp(parseFloat("baselineAlpha", 0.35f), 0.05f, 0.95f);
   prof.baselineTheta = std::clamp(parseFloat("baselineTheta", 0.25f), 0.05f, 0.95f);
   prof.baselineBeta = std::clamp(parseFloat("baselineBeta", 0.25f), 0.05f, 0.95f);
   mProfile = prof;
   return true;
}

void BciDspEngine::FinishCalibration()
{
   for (int c = 0; c < kChannelCount; ++c)
   {
      if (!mCalibChanSamples[c].empty())
      {
         const float med = MedianOf(mCalibChanSamples[c]);
         std::vector<float> absDev;
         absDev.reserve(mCalibChanSamples[c].size());
         for (float x : mCalibChanSamples[c])
            absDev.push_back(std::fabs(x - med));
         mProfile.chanMean[c] = med;
         mProfile.chanMad[c] = std::max(4.0f, 1.4826f * MedianOf(std::move(absDev)));
      }
      mCalibChanSamples[c].clear();
   }

   // Find peak Individual Alpha Frequency (IAF) in 7.5..13 Hz from Eyes Closed PSD
   if (mCalibAlphaBinsAccumCount > 0)
   {
      const double binHz = mSampleRate / (double)kFftSize;
      double bestPow = -1.0;
      float bestHz = 10.0f;
      for (int bin = 7; bin <= 14; ++bin)
      {
         const float hz = (float)(bin * binHz);
         if (hz >= 7.5f && hz <= 13.0f && mCalibClosedPsdAccum[(size_t)bin] > bestPow)
         {
            bestPow = mCalibClosedPsdAccum[(size_t)bin];
            bestHz = hz;
         }
      }
      mProfile.iafHz = bestHz;
   }

   if (!mCalibOpenAlpha.empty())
   {
      float sumA = 0.0f;
      for (float v : mCalibOpenAlpha) sumA += v;
      mProfile.baselineAlpha = sumA / (float)mCalibOpenAlpha.size();
   }
   if (!mCalibOpenBeta.empty())
   {
      float sumB = 0.0f;
      for (float v : mCalibOpenBeta) sumB += v;
      mProfile.baselineBeta = sumB / (float)mCalibOpenBeta.size();
   }

   mProfile.valid = true;
   mCalibPhase = CalibrationPhase::Complete;
   SaveProfile();
}

void BciDspEngine::ComputeWelchCorticalPsd()
{
   std::vector<double> re(kFftSize, 0.0), im(kFftSize, 0.0);
   std::array<double, 48> avgPsd{};
   std::array<double, 48> psdAf7{};
   std::array<double, 48> psdAf8{};

   const int maxBin = std::min(47, kFftSize / 2 - 1);
   const double binHz = mSampleRate / (double)kFftSize;

   for (int c = 0; c < kChannelCount; ++c)
   {
      for (int i = 0; i < kFftSize; ++i)
      {
         const int idx = (mRingHead + i) % kFftSize;
         const double hann = 0.5 * (1.0 - std::cos(kTwoPi * i / (double)(kFftSize - 1)));
         re[i] = (double)mRing[c][idx] * hann;
         im[i] = 0.0;
      }
      Radix2Fft(re, im);

      // Accumulate channel PSD + check line noise ratio for contact quality
      double bandPowerSum = 0.0;
      double totalSq = 0.0;
      for (int b = 1; b <= maxBin; ++b)
      {
         const double p = (re[b] * re[b] + im[b] * im[b]) / (double)(kFftSize * kFftSize);
         avgPsd[(size_t)b] += p * 0.25;
         if (c == 0) psdAf7[(size_t)b] = p;
         if (c == 1) psdAf8[(size_t)b] = p;
         const double hz = b * binHz;
         if (hz >= 1.0 && hz <= 42.0)
            bandPowerSum += p;
         totalSq += p;
      }

      // Contact quality heuristic: healthy EEG is non-flat, not rail-clipped, and low extreme high-freq hash
      const float rms = (float)std::sqrt(std::max(0.0, totalSq));
      float q = 1.0f;
      if (rms < 1.2f)
         q = rms / 1.2f; // dead / disconnected lead
      else if (rms > 240.0f)
         q = std::clamp(1.0f - (rms - 240.0f) / 260.0f, 0.1f, 1.0f); // rail clipping / poor contact
      else if (totalSq > 1e-9 && bandPowerSum / totalSq < 0.25)
         q = 0.45f; // mostly high-freq contact hiss
      mContactQuality[c] = mContactQuality[c] * 0.85f + std::clamp(q, 0.0f, 1.0f) * 0.15f;
   }

   // 1/f^chi Aperiodic Fit (linear regression in log10(f) vs log10(P) over 2..40 Hz excluding 7..14 Hz alpha bump)
   double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
   int fitN = 0;
   for (int b = 2; b <= maxBin; ++b)
   {
      const double f = b * binHz;
      if (f < 2.0 || f > 40.0)
         continue;
      if (f >= 7.0 && f <= 14.0)
         continue;
      const double lx = std::log10(f);
      const double ly = std::log10(std::max(1e-8, avgPsd[(size_t)b]));
      sumX += lx;
      sumY += ly;
      sumXX += lx * lx;
      sumXY += lx * ly;
      ++fitN;
   }

   double slope = -1.5;
   double intercept = 1.0;
   if (fitN >= 4)
   {
      const double denom = fitN * sumXX - sumX * sumX;
      if (std::fabs(denom) > 1e-9)
      {
         slope = (fitN * sumXY - sumX * sumY) / denom;
         intercept = (sumY - slope * sumX) / fitN;
      }
   }
   mAperiodicSlope = std::clamp((float)(-slope), 0.4f, 3.0f);
   mAperiodicOffset = (float)intercept;

   // Integrate 1/f-detrended oscillatory prominence per band
   const float iaf = mProfile.iafHz;
   double bandAccum[kBandCount] = {};
   double entropyPsd[48] = {};
   double entropySum = 0.0;
   double alphaAf7 = 1e-6, alphaAf8 = 1e-6;

   for (int b = 1; b <= maxBin; ++b)
   {
      const double f = b * binHz;
      if (f < 1.0 || f > 44.0)
         continue;
      const double logF = std::log10(f);
      const double floorLog = intercept + slope * logF;
      const double rawLog = std::log10(std::max(1e-8, avgPsd[(size_t)b]));
      // Positive oscillatory prominence above the 1/f^chi floor (in log10 decades -> linear ratio)
      const double resid = std::max(0.0, rawLog - floorLog);
      const double normP = avgPsd[(size_t)b];
      entropyPsd[(size_t)b] = normP;
      entropySum += normP;

      if (f >= 1.0 && f < 4.0)
         bandAccum[0] += resid;
      else if (f >= 4.0 && f < 8.0)
         bandAccum[1] += resid;
      else if (f >= (iaf - 2.5) && f <= (iaf + 2.5))
      {
         bandAccum[2] += resid;
         alphaAf7 += psdAf7[(size_t)b];
         alphaAf8 += psdAf8[(size_t)b];
      }
      else if (f > 13.0 && f <= 30.0)
         bandAccum[3] += resid;
      else if (f > 30.0 && f <= 44.0)
         bandAccum[4] += resid;
   }

   // Normalize band prominences smoothly into [0, 1] via saturating sigmoid/tanh mapping
   const float normScale[kBandCount] = { 1.6f, 1.8f, 3.2f, 4.2f, 2.8f };
   for (int k = 0; k < kBandCount; ++k)
   {
      const float val = (float)std::tanh(bandAccum[k] / normScale[k]);
      mBands[k] = mBands[k] * 0.6f + std::clamp(val, 0.0f, 1.0f) * 0.4f;
   }

   // Shannon Spectral Entropy (1..40 Hz)
   double H = 0.0;
   int hBins = 0;
   if (entropySum > 1e-12)
   {
      for (int b = 1; b <= maxBin; ++b)
      {
         const double f = b * binHz;
         if (f >= 1.0 && f <= 40.0 && entropyPsd[(size_t)b] > 1e-12)
         {
            const double p = entropyPsd[(size_t)b] / entropySum;
            H -= p * std::log(p);
            ++hBins;
         }
      }
      if (hBins > 1)
         H /= std::log((double)hBins);
   }
   mSpectralEntropy = mSpectralEntropy * 0.65f + std::clamp((float)H, 0.0f, 1.0f) * 0.35f;

   // Frontal Alpha Asymmetry (FAA): ln(Alpha_AF8) - ln(Alpha_AF7)
   const float faa = (float)(std::log(alphaAf8) - std::log(alphaAf7));
   mFaaRaw = mFaaRaw * 0.7f + faa * 0.3f;

   // Calibration accumulation
   if (mCalibPhase == CalibrationPhase::EyesClosed)
   {
      for (int b = 0; b < 32 && b <= maxBin; ++b)
         mCalibClosedPsdAccum[(size_t)b] += avgPsd[(size_t)b];
      ++mCalibAlphaBinsAccumCount;
   }
   else if (mCalibPhase == CalibrationPhase::EyesOpen)
   {
      mCalibOpenAlpha.push_back(mBands[2]);
      mCalibOpenBeta.push_back(mBands[3]);
   }
}

void BciDspEngine::Process(const SampleFrame* frames, int count, float outPins24[kOutputPinCount], TelemetrySnapshot& outTelemetry)
{
   if (!frames || count <= 0)
      return;

   const double dt = 1.0 / std::max(64.0, mSampleRate);
   const double nowSteady = NowSteadySec();
   const bool startleActive = mStartleGuard && (nowSteady - LastOutputTransientSec() < 0.12);
   bool batchBlinkFired = false;

   // Complex resonator coefficient centered on Individual Alpha Frequency (IAF)
   const double iafOmega = kTwoPi * mProfile.iafHz / mSampleRate;
   const double resCos = std::cos(iafOmega);
   const double resSin = std::sin(iafOmega);
   const double resDecay = 0.965; // ~150 ms coherent integration

   for (int i = 0; i < count; ++i)
   {
      const SampleFrame& f = frames[i];
      mStreamTimeSec += dt;

      if (mCalibPhase == CalibrationPhase::EyesOpen)
      {
         mCalibElapsedSec += dt;
         for (int c = 0; c < kChannelCount; ++c)
            mCalibChanSamples[c].push_back(f.ch[c]);
         if (mCalibElapsedSec >= 7.5)
         {
            mCalibPhase = CalibrationPhase::EyesClosed;
            mCalibElapsedSec = 0.0;
         }
      }
      else if (mCalibPhase == CalibrationPhase::EyesClosed)
      {
         mCalibElapsedSec += dt;
         if (mCalibElapsedSec >= 7.5)
            FinishCalibration();
      }

      // 1. Preprocessing (0.5 Hz DC HP + 50/60 Hz Notch)
      double clean[kChannelCount];
      for (int c = 0; c < kChannelCount; ++c)
      {
         double x = mHpDc[c].Step((double)f.ch[c]);
         x = mNotch50[c].Step(x);
         x = mNotch60[c].Step(x);
         clean[c] = x;

         // Advance instantaneous IAF complex resonator
         const double rPrev = mResRe[c];
         const double iPrev = mResIm[c];
         mResRe[c] = resDecay * (rPrev * resCos - iPrev * resSin) + (1.0 - resDecay) * x;
         mResIm[c] = resDecay * (rPrev * resSin + iPrev * resCos);
      }

      // Inter-hemispheric PLV across AF7<->AF8 and TP9<->TP10
      auto phaseDiffUnit = [&](int a, int b, double& outRe, double& outIm)
      {
         const double magA = std::hypot(mResRe[a], mResIm[a]) + 1e-6;
         const double magB = std::hypot(mResRe[b], mResIm[b]) + 1e-6;
         const double uaRe = mResRe[a] / magA, uaIm = mResIm[a] / magA;
         const double ubRe = mResRe[b] / magB, ubIm = mResIm[b] / magB;
         // e^{i(phi_a - phi_b)} = u_a * conj(u_b)
         outRe = uaRe * ubRe + uaIm * ubIm;
         outIm = uaIm * ubRe - uaRe * ubIm;
      };
      double fRe = 0, fIm = 0, tRe = 0, tIm = 0;
      phaseDiffUnit(0, 1, fRe, fIm);
      phaseDiffUnit(2, 3, tRe, tIm);
      const double plvAlpha = dt / 0.35;
      mPlvRe += plvAlpha * (0.5 * (fRe + tRe) - mPlvRe);
      mPlvIm += plvAlpha * (0.5 * (fIm + tIm) - mPlvIm);

      // 2. Fast Orthogonal Somatic Demixer
      const double frontalCommon = 0.5 * (clean[0] + clean[1]);
      const double frontalDiff = clean[0] - clean[1];
      // `clean` already has 0.5 Hz DC high-pass; low-pass at 8 Hz preserves intentional blink width
      const double sBlink = mBlinkLp.Step(frontalCommon);
      const double sSaccade = mSaccadeLp.Step(mSaccadeHp.Step(frontalDiff));

      const double jawL = mJawLp[0].Step(mJawHp[0].Step(clean[2]));
      const double jawR = mJawLp[1].Step(mJawHp[1].Step(clean[3]));
      const double brow = mBrowLp.Step(mBrowHp.Step(frontalCommon));

      const double alphaJaw = dt / 0.060;
      const double alphaBrow = dt / 0.100;
      mJawRmsSq[0] += alphaJaw * (jawL * jawL - mJawRmsSq[0]);
      mJawRmsSq[1] += alphaJaw * (jawR * jawR - mJawRmsSq[1]);
      mBrowRmsSq += alphaBrow * (brow * brow - mBrowRmsSq);

      const float jawLRms = (float)std::sqrt(std::max(0.0, mJawRmsSq[0]));
      const float jawRRms = (float)std::sqrt(std::max(0.0, mJawRmsSq[1]));
      const float browRms = (float)std::sqrt(std::max(0.0, mBrowRmsSq));

      // Blink Pulse Width & Amplitude Discrimination (Gap 1: Reflex vs Intentional)
      const float blinkThreshUv = std::max(55.0f, 0.55f * mProfile.medianBlinkUv);
      if (sBlink > blinkThreshUv)
      {
         if (!mBlinkInPulse)
         {
            mBlinkInPulse = true;
            mBlinkPulseSamples = 0;
            mBlinkPeakUv = 0.0f;
            // Double-blink rising edge fires immediately when a second blink lands within 380 ms
            const bool isDouble = (mStreamTimeSec - mLastBlinkEndSec) < 0.38;
            if (isDouble && !startleActive && mStreamTimeSec >= mBlinkRefractoryUntilSec)
            {
               mBlinkLatchUntilSec = mStreamTimeSec + 0.090;
               mBlinkRefractoryUntilSec = mStreamTimeSec + 0.150;
               mBlinkFlash = 1.0f;
               batchBlinkFired = true;
            }
         }
         ++mBlinkPulseSamples;
         mBlinkPeakUv = std::max(mBlinkPeakUv, (float)sBlink);
      }
      else if (mBlinkInPulse)
      {
         const float widthMs = (float)(mBlinkPulseSamples * dt * 1000.0);
         const bool isDouble = (mStreamTimeSec - mLastBlinkEndSec) < 0.38;
         const bool isIntentional = (widthMs >= 135.0f) || (mBlinkPeakUv >= 1.35f * mProfile.medianBlinkUv) || isDouble;
         const bool allowFire = (mBlinkMode == BlinkMode::AllBlinks) || isIntentional;

         if (allowFire && !startleActive && mStreamTimeSec >= mBlinkRefractoryUntilSec)
         {
            mBlinkLatchUntilSec = mStreamTimeSec + 0.090;     // 90 ms stretch across 60 Hz & 16-sample batches
            mBlinkRefractoryUntilSec = mStreamTimeSec + 0.150;
            mBlinkFlash = 1.0f;
            batchBlinkFired = true;
         }
         // Update median blink baseline gently on vegetative blinks
         if (widthMs >= 45.0f && widthMs < 130.0f)
            mProfile.medianBlinkUv = mProfile.medianBlinkUv * 0.9f + mBlinkPeakUv * 0.1f;

         mLastBlinkEndSec = mStreamTimeSec;
         mBlinkInPulse = false;
      }

      // Artifact blanking gate (protects cortical PSD from blinks & heavy jaw/brow EMG)
      const bool somaticArtifactNow = (std::fabs(sBlink) > blinkThreshUv) ||
                                      (jawLRms > 35.0f) || (jawRRms > 35.0f) || (browRms > 30.0f);
      if (somaticArtifactNow && mArtifactBlanking)
         mArtifactHoldUntilSec = mStreamTimeSec + 0.250;

      // Write clean samples to cortical 256-sample ring buffer
      for (int c = 0; c < kChannelCount; ++c)
         mRing[c][mRingHead] = (float)clean[c];
      mRingHead = (mRingHead + 1) % kFftSize;
      ++mSamplesSincePsd;

      if (mSamplesSincePsd >= 12)
      {
         mSamplesSincePsd = 0;
         if (!mArtifactBlanking || mStreamTimeSec >= mArtifactHoldUntilSec)
            ComputeWelchCorticalPsd();
      }

      // Trace buffer decimated 2:1 (128 Hz view -> 1.0 second window)
      if (++mTraceDecim >= 2)
      {
         mTraceDecim = 0;
         std::copy(mTraceFrontal + 1, mTraceFrontal + kTraceSamples, mTraceFrontal);
         std::copy(mTraceTemporal + 1, mTraceTemporal + kTraceSamples, mTraceTemporal);
         mTraceFrontal[kTraceSamples - 1] = (float)(frontalCommon * 0.008);
         mTraceTemporal[kTraceSamples - 1] = (float)(0.5 * (clean[2] + clean[3]) * 0.008);
      }

      // IMU & PPG tracking
      const double imuMag = std::sqrt(f.imu[0] * f.imu[0] + f.imu[1] * f.imu[1] + f.imu[2] * f.imu[2]);
      mImuVelSmooth += (dt / 0.15) * (imuMag - mImuVelSmooth);
      mPpgLp += (dt / 0.05) * (f.ppg - mPpgLp);
      mPpgBaseline += (dt / 2.0) * (mPpgLp - mPpgBaseline);

      // Decay UI flashes
      mBlinkFlash *= (float)std::exp(-dt / 0.16);
      mWinkFlash *= (float)std::exp(-dt / 0.16);
      mJawLFlash *= (float)std::exp(-dt / 0.16);
      mJawRFlash *= (float)std::exp(-dt / 0.16);

      // Map somatic signals to 0..1 actuators
      const float gazeNorm = Clamp01(0.5f + (float)(sSaccade / 140.0));
      if (std::fabs(sSaccade) > 65.0 && !startleActive)
         mWinkFlash = std::max(mWinkFlash, 0.85f);

      const float jawLNorm = startleActive ? 0.0f : Clamp01((jawLRms - 8.0f) / 55.0f);
      const float jawRNorm = startleActive ? 0.0f : Clamp01((jawRRms - 8.0f) / 55.0f);
      const float browNorm = startleActive ? 0.0f : Clamp01((browRms - 5.0f) / 35.0f);
      if (jawLNorm > 0.55f) mJawLFlash = 1.0f;
      if (jawRNorm > 0.55f) mJawRFlash = 1.0f;

      const float blinkPin = (batchBlinkFired || mStreamTimeSec < mBlinkLatchUntilSec) ? 1.0f : 0.0f;
      const float heartPin = Clamp01(0.5f + (float)(mPpgLp - mPpgBaseline) * 0.9f);

      // Compute 4-Axis Cortical Latent Manifold
      const float delta = mBands[0], theta = mBands[1], alpha = mBands[2], beta = mBands[3], gamma = mBands[4];
      const float rawRatio = std::log((beta + 0.5f * gamma + 0.05f) / (alpha + theta + 0.05f));
      const float slopeBias = (1.5f - mAperiodicSlope) * 0.45f;
      const float rawArousal = rawRatio + slopeBias - mProfile.baselineArousal;

      // Slow 180s adaptive quantile normalizer (Gap 6)
      const double homeoAlpha = dt / 180.0;
      mHomeoArousalMean += homeoAlpha * (rawArousal - mHomeoArousalMean);
      const double dev = rawArousal - mHomeoArousalMean;
      mHomeoArousalVar += homeoAlpha * (dev * dev - mHomeoArousalVar);
      const float arousalNorm = Clamp01(0.5f + 0.42f * (float)std::tanh((rawArousal - 0.15f * mHomeoArousalMean) * 1.15f));

      const float valenceNorm = Clamp01(0.5f + 0.5f * std::tanh(mFaaRaw * 2.2f));
      const float entropyNorm = Clamp01(mSpectralEntropy);
      const float coherenceNorm = Clamp01((float)std::hypot(mPlvRe, mPlvIm));

      const float emgCombined = std::max({ browNorm, jawLNorm, jawRNorm });
      const float imuNorm = Clamp01((float)(mImuVelSmooth * 1.5));
      const float dArousalPos = std::max(0.0f, (arousalNorm - mPrevArousal) * 8.0f);
      mPrevArousal = arousalNorm;

      // 8-second homeostatic Drift integral & phasic Surge
      mDriftState += (float)(dt / 8.0) * ((0.6f * theta + 0.4f * delta) - mDriftState);
      mSurgeState = std::max(mSurgeState * (float)std::exp(-dt / 0.25),
                             Clamp01(dArousalPos + 0.4f * std::max(jawLNorm, jawRNorm)));

      const float presenceAvg = 0.25f * (mContactQuality[0] + mContactQuality[1] + mContactQuality[2] + mContactQuality[3]);

      // Populate raw 24 pins (exact match to BrainSourceNode::Output enum)
      mRawPins[0]  = Clamp01(presenceAvg);
      mRawPins[1]  = arousalNorm;
      mRawPins[2]  = valenceNorm;
      mRawPins[3]  = entropyNorm;
      mRawPins[4]  = coherenceNorm;
      mRawPins[5]  = Clamp01(0.55f * arousalNorm + 0.45f * entropyNorm);                          // kDensity
      mRawPins[6]  = Clamp01(0.50f * alpha + 0.30f * (1.0f - entropyNorm) + 0.20f * (1.0f - emgCombined)); // kSmoothness
      mRawPins[7]  = Clamp01(0.50f * arousalNorm + 0.30f * dArousalPos + 0.20f * imuNorm);      // kMotion
      mRawPins[8]  = Clamp01(0.50f * theta + 0.35f * alpha + 0.15f * (1.0f - arousalNorm));     // kSpace
      mRawPins[9]  = Clamp01(0.45f * arousalNorm + 0.35f * (1.0f - coherenceNorm) + 0.20f * entropyNorm + 0.25f * browNorm); // kTension
      mRawPins[10] = Clamp01(mDriftState);                                                      // kDrift
      mRawPins[11] = Clamp01(mSurgeState);                                                      // kSurge
      mRawPins[12] = Clamp01(alpha * (1.0f - imuNorm) * (1.0f - emgCombined));                  // kStillness
      mRawPins[13] = delta;
      mRawPins[14] = theta;
      mRawPins[15] = alpha;
      mRawPins[16] = beta;
      mRawPins[17] = gamma;
      mRawPins[18] = blinkPin;                                                                  // kBlink (discrete)
      mRawPins[19] = gazeNorm;
      mRawPins[20] = browNorm;
      mRawPins[21] = jawLNorm;
      mRawPins[22] = jawRNorm;
      mRawPins[23] = heartPin;
   }

   // Apply One-Euro smoother on continuous pins (skip kPresence=0 and discrete kBlink=18)
   const float sm = Clamp01(mSmoothing);
   const float minCut = 10.0f * std::pow(0.45f / 10.0f, sm);
   for (int p = 0; p < kOutputPinCount; ++p)
   {
      if (p == 0 || p == 18 || sm <= 0.001f)
      {
         outPins24[p] = Clamp01(mRawPins[p]);
      }
      else
      {
         outPins24[p] = Clamp01(mEuro[p].Step(mRawPins[p], mStreamTimeSec, minCut, 1.8f));
      }
   }

   // Fill TelemetrySnapshot for UI rendering
   for (int c = 0; c < kChannelCount; ++c)
      outTelemetry.contactQuality[c] = mContactQuality[c];
   std::copy(mTraceFrontal, mTraceFrontal + kTraceSamples, outTelemetry.traceFrontal);
   std::copy(mTraceTemporal, mTraceTemporal + kTraceSamples, outTelemetry.traceTemporal);
   for (int b = 0; b < kBandCount; ++b)
      outTelemetry.bandPower[b] = mBands[b];
   outTelemetry.aperiodicSlope = mAperiodicSlope;
   outTelemetry.aperiodicOffset = mAperiodicOffset;
   outTelemetry.iafHz = mProfile.iafHz;
   outTelemetry.alphaPhaseRad = (float)std::atan2(mResIm[0], mResRe[0]);
   outTelemetry.latent[0] = outPins24[1];
   outTelemetry.latent[1] = outPins24[2];
   outTelemetry.latent[2] = outPins24[3];
   outTelemetry.latent[3] = outPins24[4];
   outTelemetry.blinkFlash = mBlinkFlash;
   outTelemetry.winkFlash = mWinkFlash;
   outTelemetry.jawLFlash = mJawLFlash;
   outTelemetry.jawRFlash = mJawRFlash;
   outTelemetry.artifactBlanked = mArtifactBlanking && (mStreamTimeSec < mArtifactHoldUntilSec);
   outTelemetry.startleMasked = startleActive;
   outTelemetry.calibrating = IsCalibrating();
   outTelemetry.calibPhase = mCalibPhase;
   outTelemetry.calibProgress = IsCalibrating() ? Clamp01((float)(mCalibElapsedSec / 7.5)) : 0.0f;
   outTelemetry.sampleRateHz = mSampleRate;
}
}
