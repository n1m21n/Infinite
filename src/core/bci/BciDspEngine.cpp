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

   mBlinkDcInit = false;
   mBlinkDcPrevIn = mBlinkDcPrevOut = 0.0;
   mBlinkDiffDcPrevIn = mBlinkDiffDcPrevOut = 0.0;
   mBlinkInPulse = false;
   mBlinkPulseSamples = 0;
   mBlinkPeakUv = 0.0f;
   mBlinkDiffPeakUv = 0.0f;
   mBlinkStartTimeSec = -10.0;
   mBlinkPeakTimeSec = -10.0;
   mBlinkValleyUv = 0.0;
   mPrevValidBlinkPeakSec = -10.0;
   mPrevValidBlinkEndSec = -10.0;
   mPrevValidBlinkPeakUv = 0.0f;
   mLastBlinkEndSec = -10.0;
   mBlinkLatchUntilSec = -10.0;
   mBlinkRefractoryUntilSec = -10.0;
   mBlinkVotePos = mBlinkVoteNeg = 0.0;
   mBlinkSign = 1.0;
   mArtifactHoldUntilSec = -10.0;
   for (int c = 0; c < kChannelCount; ++c)
   {
      mContactQuality[c] = 1.0f;
      mCalibChanQualitySum[c] = 0.0;
   }
   mCalibChanQualityCount = 0;
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
   std::fill(std::begin(mFeatEma), std::end(mFeatEma), 0.0);
   mFeatEmaInit = false;
   std::fill(std::begin(mRegionAlpha), std::end(mRegionAlpha), 0.0f);
   mStateScore = 0.0f;
   mImgInit = false;
   mImgSeq = 0;
   std::fill(std::begin(mImgCol), std::end(mImgCol), 0.5f);
   for (int c = 0; c < kChannelCount; ++c)
   {
      std::fill(std::begin(mAudLine[c]), std::end(mAudLine[c]), 0.0);
      mAudPrevPhase[c] = 0.0;
      mAudRunAmp[c] = 1.0;
   }
   mAudHead = 0;

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
   mBlinkDiffLp.Reset(); mBlinkDiffLp.SetLowPass(mSampleRate, 8.0);
   mSaccadeHp.Reset(); mSaccadeHp.SetHighPass(mSampleRate, 0.6);
   mSaccadeLp.Reset(); mSaccadeLp.SetLowPass(mSampleRate, 8.0);

   for (int i = 0; i < 2; ++i)
   {
      mJawHp[i].Reset(); mJawHp[i].SetHighPass(mSampleRate, 40.0);
      mJawLp[i].Reset(); mJawLp[i].SetLowPass(mSampleRate, 110.0);
   }
   mBrowHp.Reset(); mBrowHp.SetHighPass(mSampleRate, 30.0);
   mBrowLp.Reset(); mBrowLp.SetLowPass(mSampleRate, 75.0);

   // Audio pin: 4th-order Butterworth high-pass 2 Hz + low-pass 40 Hz (Q 0.5412, 1.3066),
   // Hamming-windowed ideal Hilbert transformer h[n] = 2 / (pi n) for odd n.
   static constexpr double kButterQ[2] = { 0.54119610, 1.30656296 };
   for (int c = 0; c < kChannelCount; ++c)
      for (int k = 0; k < 2; ++k)
      {
         mAudHp[c][k].Reset(); mAudHp[c][k].SetHighPass(mSampleRate, 2.0, kButterQ[k]);
         mAudLp[c][k].Reset(); mAudLp[c][k].SetLowPass(mSampleRate, 40.0, kButterQ[k]);
      }
   const int mid = (kHilbertTaps - 1) / 2;
   for (int i = 0; i < kHilbertTaps; ++i)
   {
      const int n = i - mid;
      const double w = 0.54 - 0.46 * std::cos(kTwoPi * i / (double)(kHilbertTaps - 1));
      mHilbertH[i] = (n % 2 != 0) ? w * 2.0 / (3.14159265358979323846 * n) : 0.0;
   }
}

void BciDspEngine::StartCalibration()
{
   mCalibPhase = CalibrationPhase::EyesOpen;
   mStateCalibFailed = false;
   mCalibElapsedSec = 0.0;
   for (int c = 0; c < kChannelCount; ++c)
      mCalibChanSamples[c].clear();
   mCalibClosedAlphaBins.clear();
   mCalibOpenAlpha.clear();
   mCalibOpenBeta.clear();
   mCalibAlphaBinsAccumCount = 0;
   mCalibClosedPsdAccum.fill(0.0);
   mCalibFeatOpen.clear();
   mCalibFeatClosed.clear();
   for (int c = 0; c < kChannelCount; ++c)
      mCalibChanQualitySum[c] = 0.0;
   mCalibChanQualityCount = 0;
   const size_t perPhase = (size_t)(kCalibPhaseSec * mSampleRate / 12.0) + 8;
   mCalibFeatOpen.reserve(perPhase);
   mCalibFeatClosed.reserve(perPhase);
   for (int c = 0; c < kChannelCount; ++c)
      mCalibChanSamples[c].reserve((size_t)(kCalibPhaseSec * mSampleRate) + 64);
   mCalibImgOpen.clear();
   mCalibImgOpen.reserve(perPhase);
   for (int c = 0; c < kChannelCount; ++c)
   {
      mCalibAudAmp[c].clear();
      mCalibAudAmp[c].reserve((size_t)(kCalibPhaseSec * mSampleRate) + 64);
   }
}

void BciDspEngine::StartBlinkCalibration(double seconds)
{
   mCalibPhase = CalibrationPhase::Blinks;
   mCalibElapsedSec = 0.0;
   mBlinkPhaseLen = std::max(2.0, seconds);
   mCalibBlinkSig.clear();
   mCalibBlinkSig.reserve((size_t)(mBlinkPhaseLen * mSampleRate) + 64);
}

// The largest blink-shaped pulses of the Blinks phase (0.3 s apart, 40..350 ms at half height) give
// the polarity (majority of up to 8) and the size and width (median of up to 5 of that sign). Fewer than 3 clear same-sign pulses (no blinks, a dead lead): keep the previous blink settings.
void BciDspEngine::FinishBlinkCalibration()
{
   const std::vector<float>& s = mCalibBlinkSig;
   const size_t n = s.size();
   if (n > 16)
   {
      const float med = MedianOf(s);
      std::vector<float> dev(n);
      for (size_t i = 0; i < n; ++i)
         dev[i] = std::fabs(s[i] - med);
      const float floorUv = std::max(50.0f, 5.0f * 1.4826f * MedianOf(dev));
      std::vector<size_t> order(n);
      std::iota(order.begin(), order.end(), (size_t)0);
      std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return std::fabs(s[a]) > std::fabs(s[b]); });
      const size_t gap = (size_t)(0.8 * mSampleRate); // 0.8 s excludes each blink's own rebound
      // Width at half the pulse's own height: blinks are 40..350 ms, eye movements and drift are wider.
      auto halfWidthMs = [&](size_t p)
      {
         const float h = 0.5f * s[p];
         size_t a = p, b = p;
         while (a > 0 && s[a - 1] * h > h * h)
            --a;
         while (b + 1 < n && s[b + 1] * h > h * h)
            ++b;
         return (float)((b - a + 1) * 1000.0 / mSampleRate);
      };
      std::vector<size_t> cand; // up to 8 blink-shaped pulses, largest first, 0.3 s apart
      for (size_t i : order)
      {
         if (cand.size() == 8 || std::fabs(s[i]) < floorUv)
            break;
         bool clear = true;
         for (size_t p : cand)
            clear = clear && (i > p ? i - p : p - i) >= gap;
         if (!clear)
            continue;
         const float wMs = halfWidthMs(i);
         if (wMs >= 40.0f && wMs <= 350.0f)
            cand.push_back(i);
      }
      int vote = 0;
      for (size_t p : cand)
         vote += s[p] > 0.0f ? 1 : -1;
      const float sign = vote >= 0 ? 1.0f : -1.0f;
      std::vector<float> amps, widths;
      for (size_t p : cand)
         if (s[p] * sign > 0.0f && amps.size() < 5)
         {
            amps.push_back(s[p] * sign);
            widths.push_back(halfWidthMs(p));
         }
      if (amps.size() >= 3)
      {
         mProfile.medianBlinkUv = std::clamp(MedianOf(amps), 60.0f, 400.0f);
         mProfile.blinkWidthMs = std::clamp(MedianOf(widths), 40.0f, 600.0f);
         mProfile.blinkSign = sign;
         mProfile.blinkValid = true;
         mBlinkSign = sign;
      }
   }
   mCalibBlinkSig.clear();
   mCalibPhase = CalibrationPhase::Complete;
   if (mPersistProfile)
      SaveProfile();
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
       << "  \"blinkValid\": " << (mProfile.blinkValid ? "true" : "false") << ",\n"
       << "  \"blinkSign\": " << mProfile.blinkSign << ",\n"
       << "  \"blinkWidthMs\": " << mProfile.blinkWidthMs << ",\n"
       << "  \"medianJawUv\": " << mProfile.medianJawUv << ",\n"
       << "  \"baselineArousal\": " << mProfile.baselineArousal << ",\n"
       << "  \"baselineAlpha\": " << mProfile.baselineAlpha << ",\n"
       << "  \"baselineTheta\": " << mProfile.baselineTheta << ",\n"
       << "  \"baselineBeta\": " << mProfile.baselineBeta << ",\n"
       << "  \"chanMad\": [" << mProfile.chanMad[0] << ", " << mProfile.chanMad[1]
       << ", " << mProfile.chanMad[2] << ", " << mProfile.chanMad[3] << "],\n"
       << "  \"stateValid\": " << (mProfile.stateValid ? "true" : "false") << ",\n"
       << "  \"stateChanMask\": " << (int)mProfile.stateChanMask << ",\n"
       << "  \"stateBias\": " << mProfile.stateBias << ",\n"
       << "  \"stateScale\": " << mProfile.stateScale << ",\n"
       << "  \"stateW\": [";
   for (int k = 0; k < kStateFeatureCount; ++k)
      oss << (k ? ", " : "") << mProfile.stateW[k];
   oss << "],\n"
       << "  \"directValid\": " << (mProfile.directValid ? "true" : "false") << ",\n";
   auto writeArr = [&](const char* key, const float* v, int n, bool last)
   {
      oss << "  \"" << key << "\": [";
      for (int k = 0; k < n; ++k)
         oss << (k ? ", " : "") << v[k];
      oss << (last ? "]\n" : "],\n");
   };
   writeArr("audAmp", mProfile.audAmp, kChannelCount, false);
   writeArr("imgMed", mProfile.imgMed, kImageColumnSize, false);
   writeArr("imgMad", mProfile.imgMad, kImageColumnSize, true);
   oss << "}\n";

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
   prof.blinkValid = content.find("\"blinkValid\": true") != std::string::npos;
   prof.blinkSign = parseFloat("blinkSign", 1.0f) < 0.0f ? -1.0f : 1.0f;
   prof.blinkWidthMs = std::clamp(parseFloat("blinkWidthMs", 150.0f), 40.0f, 600.0f);
   prof.baselineArousal = std::clamp(parseFloat("baselineArousal", 0.0f), -2.0f, 2.0f);
   prof.baselineAlpha = std::clamp(parseFloat("baselineAlpha", 0.35f), 0.05f, 0.95f);
   prof.baselineTheta = std::clamp(parseFloat("baselineTheta", 0.25f), 0.05f, 0.95f);
   prof.baselineBeta = std::clamp(parseFloat("baselineBeta", 0.25f), 0.05f, 0.95f);
   if (content.find("\"stateValid\": true") != std::string::npos)
   {
      const size_t at = content.find("\"stateW\": [");
      prof.stateBias = parseFloat("stateBias", 0.0f);
      prof.stateScale = parseFloat("stateScale", 1.0f);
      prof.stateChanMask = (uint8_t)std::clamp((int)std::lround(parseFloat("stateChanMask", 15.0f)), 0, 15);
      bool ok = at != std::string::npos && std::isfinite(prof.stateBias) && prof.stateScale > 1e-6f;
      const char* cur = ok ? content.c_str() + at + 10 : nullptr;
      for (int k = 0; ok && k < kStateFeatureCount; ++k)
      {
         while (*cur == ' ' || *cur == ',' || *cur == '\n')
            ++cur;
         char* end = nullptr;
         const double v = std::strtod(cur, &end);
         ok = end != cur && std::isfinite(v);
         prof.stateW[k] = (float)v;
         cur = end;
      }
      prof.stateValid = ok;
   }
   auto parseArr = [&](const char* key, float* out, int n) -> bool
   {
      const std::string pat = std::string("\"") + key + "\": [";
      const size_t at = content.find(pat);
      if (at == std::string::npos)
         return false;
      const char* cur = content.c_str() + at + pat.size();
      for (int k = 0; k < n; ++k)
      {
         while (*cur == ' ' || *cur == ',' || *cur == '\n')
            ++cur;
         char* end = nullptr;
         const double v = std::strtod(cur, &end);
         if (end == cur || !std::isfinite(v))
            return false;
         out[k] = (float)v;
         cur = end;
      }
      return true;
   };
   if (content.find("\"directValid\": true") != std::string::npos)
      prof.directValid = parseArr("audAmp", prof.audAmp, kChannelCount) &&
                         parseArr("imgMed", prof.imgMed, kImageColumnSize) &&
                         parseArr("imgMad", prof.imgMad, kImageColumnSize);
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

   FinishDirectBaselines();
   mProfile.stateValid = FitStateWeights();
   mStateCalibFailed = !mProfile.stateValid;
   mCalibFeatOpen.clear();
   mCalibFeatClosed.clear();
   for (int c = 0; c < kChannelCount; ++c)
      mCalibChanQualitySum[c] = 0.0;
   mCalibChanQualityCount = 0;

   mProfile.valid = true;
   if (mPersistProfile)
      SaveProfile();
   StartBlinkCalibration(kBlinkPhaseSec);
}

void BciDspEngine::UpdateContactQuality()
{
   // Fast time-domain contact check per channel, run unconditionally every 12 samples so a single
   // noisy or disconnected electrode is flagged even while artifact blanking is active.
   for (int c = 0; c < kChannelCount; ++c)
   {
      double sumSq = 0.0, diffSq = 0.0;
      double prev = (double)mRing[c][mRingHead];
      for (int i = 0; i < kFftSize; ++i)
      {
         const int idx = (mRingHead + i) % kFftSize;
         const double x = (double)mRing[c][idx];
         sumSq += x * x;
         if (i > 0)
         {
            const double d = x - prev;
            diffSq += d * d;
         }
         prev = x;
      }
      const float rms = (float)std::sqrt(sumSq / (double)kFftSize);
      const float diffRms = (float)std::sqrt(diffSq / (double)(kFftSize - 1));
      float q = 1.0f;
      if (rms < 1.2f)
         q = rms / 1.2f; // dead / disconnected lead
      else if (rms > 180.0f)
         q = std::clamp(1.0f - (rms - 180.0f) / 140.0f, 0.05f, 1.0f); // rail clipping / noisy lead
      else if (rms > 1e-4f && (diffRms / rms) > 1.35f)
         q = 0.25f; // mostly broadband high-freq contact hiss
      mContactQuality[c] = mContactQuality[c] * 0.85f + std::clamp(q, 0.0f, 1.0f) * 0.15f;
   }
}

void BciDspEngine::ComputeWelchCorticalPsd()
{
   std::vector<double> re(kFftSize, 0.0), im(kFftSize, 0.0);
   std::array<double, 48> avgPsd{};
   std::array<double, 48> psdAf7{};
   std::array<double, 48> psdAf8{};
   double chanPsd[kChannelCount][48] = {};

   const int maxBin = std::min(47, kFftSize / 2 - 1);
   const double binHz = mSampleRate / (double)kFftSize;

   bool chGood[kChannelCount];
   int nGood = 0;
   for (int c = 0; c < kChannelCount; ++c)
   {
      chGood[c] = mContactQuality[c] >= 0.45f;
      if (chGood[c]) ++nGood;
   }
   const double avgWeight = nGood > 0 ? (1.0 / (double)nGood) : 0.25;

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

      for (int b = 1; b <= maxBin; ++b)
      {
         const double p = (re[b] * re[b] + im[b] * im[b]) / (double)(kFftSize * kFftSize);
         if (nGood == 0 || chGood[c])
            avgPsd[(size_t)b] += p * avgWeight;
         if (c == 0) psdAf7[(size_t)b] = p;
         if (c == 1) psdAf8[(size_t)b] = p;
         chanPsd[c][b] = p;
      }
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

   UpdateStateFeatures(chanPsd, binHz);
   UpdateImageColumn(chanPsd, binHz);

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

void BciDspEngine::UpdateStateFeatures(const double (&chanPsd)[kChannelCount][48], double binHz)
{
   // Bands as half-open [lo, hi) Hz, total 2..40 Hz inclusive: the same bins tools/bci/calib_ab.py uses.
   static constexpr double kLo[4] = { 4.0, 8.0, 10.0, 12.0 };
   static constexpr double kHi[4] = { 8.0, 10.0, 12.0, 30.0 };
   double feat[kStateFeatureCount] = {};
   for (int c = 0; c < kChannelCount; ++c)
   {
      double tot = 0.0, band[4] = {};
      for (int b = 1; b < 48; ++b)
      {
         const double hz = b * binHz;
         if (hz >= 2.0 && hz <= 40.0)
            tot += chanPsd[c][b];
         for (int k = 0; k < 4; ++k)
            if (hz >= kLo[k] && hz < kHi[k])
               band[k] += chanPsd[c][b];
      }
      for (int k = 0; k < 4; ++k)
         feat[k * kChannelCount + c] = std::log10(std::max(1e-12, band[k]) / std::max(1e-12, tot));
   }

   const double hopSec = 12.0 / std::max(64.0, mSampleRate);
   const double a = mFeatEmaInit ? 1.0 - std::exp(-hopSec / std::max(0.25, mStateTauSec)) : 1.0;
   mFeatEmaInit = true;
   for (int k = 0; k < kStateFeatureCount; ++k)
      mFeatEma[k] += a * (feat[k] - mFeatEma[k]);

   for (int c = 0; c < kChannelCount; ++c)
      mRegionAlpha[c] = (float)(std::pow(10.0, mFeatEma[1 * kChannelCount + c]) +
                                std::pow(10.0, mFeatEma[2 * kChannelCount + c]));

   if (mProfile.stateValid)
   {
      double proj = 0.0;
      for (int k = 0; k < kStateFeatureCount; ++k)
         proj += mProfile.stateW[k] * mFeatEma[k];
      const double z = (proj - mProfile.stateBias) / std::max(1e-6f, mProfile.stateScale);
      mStateScore = (float)(1.0 / (1.0 + std::exp(-std::clamp(z, -20.0, 20.0))));
   }

   if ((mCalibPhase == CalibrationPhase::EyesOpen || mCalibPhase == CalibrationPhase::EyesClosed) &&
       mCalibElapsedSec >= kCalibSettleSec)
   {
      std::array<float, kStateFeatureCount> v{};
      for (int k = 0; k < kStateFeatureCount; ++k)
         v[(size_t)k] = (float)mFeatEma[k];
      (mCalibPhase == CalibrationPhase::EyesOpen ? mCalibFeatOpen : mCalibFeatClosed).push_back(v);
      for (int c = 0; c < kChannelCount; ++c)
         mCalibChanQualitySum[c] += mContactQuality[c];
      ++mCalibChanQualityCount;
   }
}

bool BciDspEngine::FitStateWeights()
{
   // Shrinkage LDA (shrink 0.3 toward a scaled identity), closed = positive class.
   // Supports 1-bad-sensor fallback: drops 1 noisy/disconnected channel and fits 12 features on the remaining 3.
   constexpr int kMaxN = kStateFeatureCount;
   const size_t n1 = mCalibFeatClosed.size(), n0 = mCalibFeatOpen.size();
   if (n1 < 20 || n0 < 20)
      return false;

   double mu1Full[kMaxN] = {}, mu0Full[kMaxN] = {};
   for (const auto& v : mCalibFeatClosed)
      for (int k = 0; k < kMaxN; ++k) mu1Full[k] += v[(size_t)k] / (double)n1;
   for (const auto& v : mCalibFeatOpen)
      for (int k = 0; k < kMaxN; ++k) mu0Full[k] += v[(size_t)k] / (double)n0;

   // Identify active channels: good mean contact quality AND non-degenerate (non-flatline) feature variance.
   uint8_t chanMask = 0;
   int activeChCount = 0;
   int activeIdx[kMaxN] = {};
   int d = 0;
   for (int c = 0; c < kChannelCount; ++c)
   {
      const double meanQ = mCalibChanQualityCount > 0 ? (mCalibChanQualitySum[c] / (double)mCalibChanQualityCount) : 1.0;
      double varSum = 0.0;
      for (int b = 0; b < 4; ++b)
      {
         const int k = b * kChannelCount + c;
         for (const auto& v : mCalibFeatClosed)
         {
            const double diff = v[(size_t)k] - mu1Full[k];
            varSum += diff * diff;
         }
         for (const auto& v : mCalibFeatOpen)
         {
            const double diff = v[(size_t)k] - mu0Full[k];
            varSum += diff * diff;
         }
      }
      varSum /= (double)(n1 + n0);
      if (meanQ >= 0.45 && varSum > 1e-8)
      {
         chanMask |= (uint8_t)(1u << c);
         ++activeChCount;
      }
   }
   if (activeChCount < 3)
      return false;

   for (int b = 0; b < 4; ++b)
      for (int c = 0; c < kChannelCount; ++c)
         if (chanMask & (1u << c))
            activeIdx[d++] = b * kChannelCount + c;

   double mu1[kMaxN] = {}, mu0[kMaxN] = {};
   for (int i = 0; i < d; ++i)
   {
      mu1[i] = mu1Full[activeIdx[i]];
      mu0[i] = mu0Full[activeIdx[i]];
   }

   double S[kMaxN][kMaxN] = {};
   auto addCov = [&](const std::vector<std::array<float, kMaxN>>& set, const double* mu)
   {
      for (const auto& v : set)
         for (int i = 0; i < d; ++i)
            for (int j = 0; j < d; ++j)
               S[i][j] += (v[(size_t)activeIdx[i]] - mu[i]) * (v[(size_t)activeIdx[j]] - mu[j]);
   };
   addCov(mCalibFeatClosed, mu1);
   addCov(mCalibFeatOpen, mu0);
   double tr = 0.0;
   for (int i = 0; i < d; ++i)
      for (int j = 0; j < d; ++j)
         S[i][j] /= (double)(n1 + n0 - 1);
   for (int i = 0; i < d; ++i)
      tr += S[i][i];

   constexpr double kShrink = 0.3;
   double A[kMaxN][kMaxN + 1] = {};
   for (int i = 0; i < d; ++i)
   {
      for (int j = 0; j < d; ++j)
         A[i][j] = (1.0 - kShrink) * S[i][j] + (i == j ? kShrink * tr / (double)d : 0.0);
      A[i][d] = mu1[i] - mu0[i];
   }
   for (int col = 0; col < d; ++col) // Gaussian elimination, partial pivoting
   {
      int piv = col;
      for (int r = col + 1; r < d; ++r)
         if (std::fabs(A[r][col]) > std::fabs(A[piv][col]))
            piv = r;
      if (std::fabs(A[piv][col]) < 1e-12)
         return false;
      if (piv != col)
         for (int j = 0; j <= d; ++j)
            std::swap(A[col][j], A[piv][j]);
      for (int r = 0; r < d; ++r)
      {
         if (r == col)
            continue;
         const double fct = A[r][col] / A[col][col];
         for (int j = col; j <= d; ++j)
            A[r][j] -= fct * A[col][j];
      }
   }
   double w[kMaxN] = {}, mid = 0.0, gap = 0.0;
   for (int i = 0; i < d; ++i)
   {
      w[i] = A[i][d] / A[i][i];
      mid += w[i] * 0.5 * (mu1[i] + mu0[i]);
      gap += w[i] * (mu1[i] - mu0[i]);
   }
   if (!(gap > 1e-9) || !std::isfinite(gap))
      return false;
   for (int k = 0; k < kMaxN; ++k)
      mProfile.stateW[k] = 0.0f;
   for (int i = 0; i < d; ++i)
      mProfile.stateW[activeIdx[i]] = (float)w[i];
   mProfile.stateChanMask = chanMask;
   mProfile.stateBias = (float)mid;
   mProfile.stateScale = (float)(gap / 4.0); // class means land at z = +-2 (score 0.88 / 0.12)
   return true;
}

void BciDspEngine::UpdateImageColumn(const double (&chanPsd)[kChannelCount][48], double binHz)
{
   const double hopSec = 12.0 / std::max(64.0, mSampleRate);
   const double a = mImgInit ? 1.0 - std::exp(-hopSec / kImageTauSec) : 1.0;
   const double ab = mImgInit ? 1.0 - std::exp(-hopSec / kBaselineTauSec) : 1.0;
   for (int c = 0; c < kChannelCount; ++c)
      for (int k = 0; k < kImageBins; ++k)
      {
         // bin k = (k + 1) Hz; with a 256-point window at 256 Hz that is FFT bin k + 1
         const int b = std::clamp((int)std::lround((k + 1) / binHz), 1, 47);
         const int i = c * kImageBins + k;
         const double lp = std::log10(std::max(1e-12, chanPsd[c][b] * (double)(kFftSize * kFftSize)));
         mImgLog[i] += a * (lp - mImgLog[i]);
         mImgRunMean[i] += ab * (mImgLog[i] - mImgRunMean[i]);
         mImgRunDev[i] += ab * (std::fabs(mImgLog[i] - mImgRunMean[i]) * 1.2533 - mImgRunDev[i]);
         if (!mImgInit)
            mImgRunDev[i] = 0.3;
         const double med = mProfile.directValid ? mProfile.imgMed[i] : mImgRunMean[i];
         const double mad = mProfile.directValid ? mProfile.imgMad[i] : mImgRunDev[i];
         const double z = (mImgLog[i] - med) / std::max(1e-3, mad);
         mImgCol[i] = (float)std::clamp((z + 3.0) / 6.0, 0.0, 1.0);
      }
   mImgInit = true;

   if (mCalibPhase == CalibrationPhase::EyesOpen && mCalibElapsedSec >= kCalibSettleSec)
   {
      std::array<float, kImageColumnSize> v{};
      for (int i = 0; i < kImageColumnSize; ++i)
         v[(size_t)i] = (float)mImgLog[i];
      mCalibImgOpen.push_back(v);
   }
}

void BciDspEngine::StepAudify(const double (&clean)[kChannelCount])
{
   AudifyFrame fr;
   const int mid = (kHilbertTaps - 1) / 2;
   const double ab = 1.0 - std::exp(-1.0 / (kBaselineTauSec * std::max(64.0, mSampleRate)));
   for (int c = 0; c < kChannelCount; ++c)
   {
      double x = clean[c];
      for (int k = 0; k < 2; ++k)
         x = mAudLp[c][k].Step(mAudHp[c][k].Step(x));
      mAudLine[c][mAudHead] = x;
      double im = 0.0;
      for (int t = 1; t < kHilbertTaps; t += 2) // odd taps only (even taps are zero)
         im += mHilbertH[t] * mAudLine[c][(mAudHead - t + kHilbertTaps) % kHilbertTaps];
      const double re = mAudLine[c][(mAudHead - mid + kHilbertTaps) % kHilbertTaps];
      const double amp = std::hypot(re, im);
      const double ph = std::atan2(im, re);
      double dph = ph - mAudPrevPhase[c];
      dph -= kTwoPi * std::floor((dph + 3.14159265358979323846) / kTwoPi);
      mAudPrevPhase[c] = ph;
      mAudRunAmp[c] += ab * (amp - mAudRunAmp[c]);
      if (mCalibPhase == CalibrationPhase::EyesOpen && mCalibElapsedSec >= kCalibSettleSec)
         mCalibAudAmp[c].push_back((float)amp);
      const double base = mProfile.directValid ? mProfile.audAmp[c] : mAudRunAmp[c];
      fr.amp[c] = (float)(amp / std::max(1e-3, base));
      fr.dphase[c] = (float)dph;
   }
   mAudHead = (mAudHead + 1) % kHilbertTaps;
   mAudify.push_back(fr);
}

void BciDspEngine::FinishDirectBaselines()
{
   const size_t n = mCalibImgOpen.size();
   bool ok = n >= 32;
   for (int c = 0; c < kChannelCount; ++c)
      ok = ok && mCalibAudAmp[c].size() >= 256;
   if (ok)
   {
      std::vector<float> col(n), dev(n);
      for (int i = 0; i < kImageColumnSize; ++i)
      {
         for (size_t j = 0; j < n; ++j)
            col[j] = mCalibImgOpen[j][(size_t)i];
         const float med = MedianOf(col);
         for (size_t j = 0; j < n; ++j)
            dev[j] = std::fabs(col[j] - med);
         mProfile.imgMed[i] = med;
         mProfile.imgMad[i] = std::max(1e-3f, 1.4826f * MedianOf(dev));
      }
      for (int c = 0; c < kChannelCount; ++c)
         mProfile.audAmp[c] = std::max(1e-3f, MedianOf(mCalibAudAmp[c]));
   }
   mProfile.directValid = ok;
   mCalibImgOpen.clear();
   for (auto& v : mCalibAudAmp)
      v.clear();
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

   mAudify.clear();
   for (int i = 0; i < count; ++i)
   {
      const SampleFrame& f = frames[i];
      mStreamTimeSec += dt;

      if (mCalibPhase == CalibrationPhase::EyesOpen)
      {
         mCalibElapsedSec += dt;
         for (int c = 0; c < kChannelCount; ++c)
            mCalibChanSamples[c].push_back(f.ch[c]);
         if (mCalibElapsedSec >= kCalibPhaseSec)
         {
            mCalibPhase = CalibrationPhase::EyesClosed;
            mCalibElapsedSec = 0.0;
         }
      }
      else if (mCalibPhase == CalibrationPhase::EyesClosed)
      {
         mCalibElapsedSec += dt;
         if (mCalibElapsedSec >= kCalibPhaseSec)
            FinishCalibration();
      }
      else if (mCalibPhase == CalibrationPhase::Blinks)
      {
         mCalibElapsedSec += dt;
         if (mCalibElapsedSec >= mBlinkPhaseLen)
            FinishBlinkCalibration();
      }

      // 1. Preprocessing (0.5 Hz DC HP + 50/60 Hz Notch)
      double clean[kChannelCount];
      double notched[kChannelCount];
      for (int c = 0; c < kChannelCount; ++c)
      {
         double n = mNotch50[c].Step((double)f.ch[c]);
         n = mNotch60[c].Step(n);
         notched[c] = n;
         double x = mHpDc[c].Step(n);
         clean[c] = x;

         // Advance instantaneous IAF complex resonator
         const double rPrev = mResRe[c];
         const double iPrev = mResIm[c];
         mResRe[c] = resDecay * (rPrev * resCos - iPrev * resSin) + (1.0 - resDecay) * x;
         mResIm[c] = resDecay * (rPrev * resSin + iPrev * resCos);
      }

      StepAudify(clean);

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

      // 2. Fast Orthogonal Somatic Demixer (per-channel contact-aware so 1 bad lead never deadlocks blanking)
      const bool af7Ok = mContactQuality[0] >= 0.45f;
      const bool af8Ok = mContactQuality[1] >= 0.45f;
      const bool tp9Ok = mContactQuality[2] >= 0.45f;
      const bool tp10Ok = mContactQuality[3] >= 0.45f;
      const double frontalCommon = (af7Ok && !af8Ok) ? clean[0]
                                 : (!af7Ok && af8Ok) ? clean[1]
                                                     : 0.5 * (clean[0] + clean[1]);
      const double frontalDiff = (af7Ok && af8Ok) ? (clean[0] - clean[1]) : 0.0;
      const double blinkIn = (af7Ok && !af8Ok) ? notched[0]
                           : (!af7Ok && af8Ok) ? notched[1]
                                               : 0.5 * (notched[0] + notched[1]);
      const double blinkDiffIn = (af7Ok && af8Ok) ? (notched[0] - notched[1]) : 0.0;

      // 1st-order 0.6 Hz DC blocker on notched frontal signal (zero 2nd-order ringing overshoot on double blinks,
      // while rejecting slow electrode drift cleanly).
      if (!mBlinkDcInit || std::fabs(blinkIn - mBlinkDcPrevIn) > 5000.0)
      {
         mBlinkDcPrevIn = blinkIn;
         mBlinkDcPrevOut = 0.0;
         mBlinkDiffDcPrevIn = blinkDiffIn;
         mBlinkDiffDcPrevOut = 0.0;
         mBlinkDcInit = true;
      }
      const double dcAlpha = std::exp(-kTwoPi * 0.60 / std::max(64.0, mSampleRate));
      const double blinkDc = dcAlpha * (mBlinkDcPrevOut + blinkIn - mBlinkDcPrevIn);
      mBlinkDcPrevIn = blinkIn;
      mBlinkDcPrevOut = blinkDc;
      const double blinkDiffDc = dcAlpha * (mBlinkDiffDcPrevOut + blinkDiffIn - mBlinkDiffDcPrevIn);
      mBlinkDiffDcPrevIn = blinkDiffIn;
      mBlinkDiffDcPrevOut = blinkDiffDc;

      const double sBlinkRaw = mBlinkLp.Step(blinkDc);
      const double sBlinkDiff = mBlinkDiffLp.Step(blinkDiffDc);
      // Blink polarity depends on headset and reference (Muse vs OpenBCI). Vote with the time spent past
      // the blink threshold on each side (tau 60 s): blinks are the large, frequent, one-sided pulses,
      // eye movements and drift split both ways. Flip only when the other side leads 1.5 : 1.
      if (mCalibPhase == CalibrationPhase::Blinks)
         mCalibBlinkSig.push_back((float)sBlinkRaw);
      if (mProfile.blinkValid)
         mBlinkSign = mProfile.blinkSign; // set by the prompted blinks
      else
      {
         const float th = std::max(55.0f, 0.55f * mProfile.medianBlinkUv);
         const double decay = std::exp(-dt / 60.0);
         mBlinkVotePos = mBlinkVotePos * decay + (sBlinkRaw > th ? dt : 0.0);
         mBlinkVoteNeg = mBlinkVoteNeg * decay + (sBlinkRaw < -th ? dt : 0.0);
         if (mBlinkSign > 0 ? mBlinkVoteNeg > 1.5 * mBlinkVotePos + 0.05 : mBlinkVotePos > 1.5 * mBlinkVoteNeg + 0.05)
            mBlinkSign = -mBlinkSign;
      }
      const double sBlink = mBlinkSign * sBlinkRaw;
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

      // Blink Pulse Width & Amplitude Discrimination (Gap 1: Reflex vs Intentional vs DoubleBlink)
      // Calibrated: half the person's cued blink. Otherwise a population default.
      const float blinkThreshUv = mProfile.blinkValid ? std::max(30.0f, 0.5f * mProfile.medianBlinkUv)
                                                      : std::max(55.0f, 0.55f * mProfile.medianBlinkUv);

      if (mBlinkMode == BlinkMode::DoubleBlink)
      {
         // Dedicated double-blink detector: two symmetric, amplitude-balanced blinks within 220..650 ms
         // peak-to-peak, with valley-prominence compensation for blink #2 sitting on blink #1's rebound.
         constexpr double kMinIbiSec = 0.22;
         constexpr double kMaxIbiSec = 0.58;
         constexpr double kMinGapSec = 0.04;
         const float dblThr = mProfile.blinkValid ? std::max(30.0f, 0.50f * mProfile.medianBlinkUv)
                                                  : std::max(55.0f, 0.55f * mProfile.medianBlinkUv);
         const float minPk = mProfile.blinkValid ? std::max(dblThr, 0.70f * mProfile.medianBlinkUv)
                                                 : std::max(dblThr, 0.80f * mProfile.medianBlinkUv);
         const bool inWin = (mPrevValidBlinkPeakSec > 0.0) &&
                            ((mStreamTimeSec - mPrevValidBlinkPeakSec) <= kMaxIbiSec) &&
                            ((mStreamTimeSec - mPrevValidBlinkEndSec) >= kMinGapSec);
         if (!mBlinkInPulse)
         {
            if (mPrevValidBlinkPeakSec > 0.0 && (mStreamTimeSec - mPrevValidBlinkEndSec) < kMaxIbiSec)
               mBlinkValleyUv = std::min(mBlinkValleyUv, std::max(-0.8 * (double)dblThr, sBlink));
            else
               mBlinkValleyUv = 0.0;
         }
         const double vProm = inWin ? (sBlink - mBlinkValleyUv) : sBlink;
         const bool above = (vProm > dblThr) && (!inWin || sBlink > 0.30 * dblThr);
         const bool stay = mBlinkInPulse && (vProm > 0.70 * dblThr) && (!inWin || sBlink > 0.15 * dblThr);
         if (above || stay)
         {
            if (!mBlinkInPulse)
            {
               mBlinkInPulse = true;
               mBlinkPulseSamples = 0;
               mBlinkPeakUv = 0.0f;
               mBlinkDiffPeakUv = 0.0f;
               mBlinkStartTimeSec = mStreamTimeSec;
               mBlinkPeakTimeSec = mStreamTimeSec;
            }
            if (above)
               ++mBlinkPulseSamples;
            if ((float)vProm > mBlinkPeakUv)
            {
               mBlinkPeakUv = (float)vProm;
               mBlinkPeakTimeSec = mStreamTimeSec;
            }
            mBlinkDiffPeakUv = std::max(mBlinkDiffPeakUv, (float)std::fabs(sBlinkDiff));
         }
         else if (mBlinkInPulse)
         {
            const float widthMs = (float)(mBlinkPulseSamples * dt * 1000.0);
            const bool validPulse = (widthMs >= 30.0f && widthMs <= 340.0f) &&
                                    (mBlinkPeakUv >= minPk) &&
                                    (mBlinkDiffPeakUv <= 0.85f * mBlinkPeakUv);
            if (validPulse)
            {
               const double ibi = mBlinkPeakTimeSec - mPrevValidBlinkPeakSec;
               const double gap = mBlinkStartTimeSec - mPrevValidBlinkEndSec;
               const float ampBal = mPrevValidBlinkPeakUv > 1e-3f
                                       ? (std::min(mBlinkPeakUv, mPrevValidBlinkPeakUv) /
                                          std::max(mBlinkPeakUv, mPrevValidBlinkPeakUv))
                                       : 0.0f;
               if (ibi >= kMinIbiSec && ibi <= kMaxIbiSec && gap >= kMinGapSec && ampBal >= 0.55f &&
                   !startleActive && mStreamTimeSec >= mBlinkRefractoryUntilSec)
               {
                  mBlinkLatchUntilSec = mStreamTimeSec + 0.090;
                  mBlinkRefractoryUntilSec = mStreamTimeSec + 0.900; // consume pair; ignore triple-blink/rebound
                  mBlinkFlash = 1.0f;
                  batchBlinkFired = true;
                  mPrevValidBlinkPeakSec = -10.0;
                  mPrevValidBlinkEndSec = -10.0;
                  mPrevValidBlinkPeakUv = 0.0f;
                  mBlinkValleyUv = 0.0;
               }
               else
               {
                  mPrevValidBlinkPeakSec = mBlinkPeakTimeSec;
                  mPrevValidBlinkEndSec = mStreamTimeSec;
                  mPrevValidBlinkPeakUv = mBlinkPeakUv;
                  mBlinkValleyUv = 0.0;
               }
            }
            mLastBlinkEndSec = mStreamTimeSec;
            mBlinkInPulse = false;
         }
      }
      else
      {
         // Hysteresis: a pulse ends below 70 % of the threshold, so one blink that dips mid-way is not
         // split in two (the second half would read as a double blink).
         if (sBlink > blinkThreshUv || (mBlinkInPulse && sBlink > 0.7f * blinkThreshUv))
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
            if (sBlink > blinkThreshUv)
               ++mBlinkPulseSamples; // width = time above the threshold, as before
            mBlinkPeakUv = std::max(mBlinkPeakUv, (float)sBlink);
         }
         else if (mBlinkInPulse)
         {
            const float widthMs = (float)(mBlinkPulseSamples * dt * 1000.0);
            const bool isDouble = (mStreamTimeSec - mLastBlinkEndSec) < 0.38;
            // Calibrated: a blink as big or as long as the cued ones is deliberate. Otherwise fixed rules.
            const bool isIntentional = mProfile.blinkValid
               ? (mBlinkPeakUv >= 0.8f * mProfile.medianBlinkUv || widthMs >= 0.8f * mProfile.blinkWidthMs || isDouble)
               : ((widthMs >= 135.0f) || (mBlinkPeakUv >= 1.35f * mProfile.medianBlinkUv) || isDouble);
            const bool allowFire = (mBlinkMode == BlinkMode::AllBlinks) || isIntentional;

            if (allowFire && !startleActive && mStreamTimeSec >= mBlinkRefractoryUntilSec)
            {
               mBlinkLatchUntilSec = mStreamTimeSec + 0.090;     // 90 ms stretch across 60 Hz & 16-sample batches
               mBlinkRefractoryUntilSec = mStreamTimeSec + 0.150;
               mBlinkFlash = 1.0f;
               batchBlinkFired = true;
            }
            // Update median blink baseline gently on vegetative blinks
            if (!mProfile.blinkValid && widthMs >= 45.0f && widthMs < 130.0f)
               mProfile.medianBlinkUv = mProfile.medianBlinkUv * 0.9f + mBlinkPeakUv * 0.1f;

            mLastBlinkEndSec = mStreamTimeSec;
            mBlinkInPulse = false;
         }
      }

      // Artifact blanking gate (protects cortical PSD from blinks & heavy jaw/brow EMG; ignores bad leads)
      const bool frontArt = (af7Ok || af8Ok) && ((std::fabs(sBlink) > blinkThreshUv) || (browRms > 30.0f));
      const bool jawLArt = tp9Ok && (jawLRms > 35.0f);
      const bool jawRArt = tp10Ok && (jawRRms > 35.0f);
      const bool somaticArtifactNow = frontArt || jawLArt || jawRArt;
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
         UpdateContactQuality();
         if (!mArtifactBlanking || mStreamTimeSec >= mArtifactHoldUntilSec)
            ComputeWelchCorticalPsd();
         ++mImgSeq; // the Image pin scrolls at a constant rate; a blanked update repeats the last column
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
      mRawPins[15] = mProfile.stateValid ? mStateScore : alpha; // calibrated: this person's eyes-closed state
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
   const double phaseLen = mCalibPhase == CalibrationPhase::Blinks ? mBlinkPhaseLen : kCalibPhaseSec;
   outTelemetry.calibProgress = IsCalibrating() ? Clamp01((float)(mCalibElapsedSec / phaseLen)) : 0.0f;
   outTelemetry.sampleRateHz = mSampleRate;
   for (int c = 0; c < kChannelCount; ++c)
      outTelemetry.regionAlpha[c] = mRegionAlpha[c];
   outTelemetry.stateCalibrated = mProfile.stateValid;
   outTelemetry.stateCalibFailed = mStateCalibFailed;
   outTelemetry.stateChanMask = mProfile.stateChanMask;
   outTelemetry.eyesClosedScore = mProfile.stateValid ? mStateScore : 0.0f;
   std::copy(std::begin(mImgCol), std::end(mImgCol), outTelemetry.imageCol);
   outTelemetry.imageState = mProfile.stateValid ? mStateScore : -1.0f;
   outTelemetry.imageSeq = mImgSeq;
}
}
