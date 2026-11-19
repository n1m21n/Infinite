#include "SyntheticCortex.h"

#include "platform/AppPaths.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Bci
{
namespace
{
   constexpr double kTwoPi = 6.28318530717958647692;
}

float SyntheticCortex::PinkState::Step(float white, float slopeChi)
{
   // Paul Kellet's refined pink-noise filter blended with white noise to control 1/f^chi slope.
   b0 = 0.99886f * b0 + white * 0.0555179f;
   b1 = 0.99332f * b1 + white * 0.0750759f;
   b2 = 0.96900f * b2 + white * 0.1538520f;
   b3 = 0.86650f * b3 + white * 0.3104856f;
   b4 = 0.55000f * b4 + white * 0.5329522f;
   b5 = -0.7616f * b5 - white * 0.0168980f;
   const float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.11f;
   b6 = white * 0.115926f;

   // slopeChi in [0.9, 2.0]: higher chi emphasizes pink/slow tilt, lower chi flattens toward white.
   const float tilt = std::clamp((slopeChi - 0.8f) / 1.1f, 0.0f, 1.0f);
   return white * (1.0f - tilt) * 0.35f + pink * (0.55f + 0.85f * tilt);
}

SyntheticCortex::SyntheticCortex()
{
   Reset();
}

void SyntheticCortex::Reset(uint32_t seed)
{
   mRng = seed ? seed : 0x494E4642u;
   mTimeSec = 0.0;
   mAutoTimerSec = 0.0;
   mSpontBlinkTimerSec = 3.2;
   mAutoStep = 0;
   mFocusBoost = 0.0f;
   mCalmBoost = 0.0f;
   for (auto& p : mPink)
      p = PinkState{};
   for (int b = 0; b < kBandCount; ++b)
      for (int c = 0; c < kChannelCount; ++c)
         mPhase[b][c] = 0.17 * c + 0.41 * b;
   std::lock_guard<std::mutex> lk(mGestureMu);
   mGestures.clear();
}

void SyntheticCortex::SetPreset(SyntheticPreset preset)
{
   mPreset = preset;
}

float SyntheticCortex::NextUniform()
{
   // xorshift32
   uint32_t x = mRng;
   x ^= x << 13;
   x ^= x >> 17;
   x ^= x << 5;
   mRng = x;
   return (float)(x & 0x00FFFFFFu) / (float)0x01000000u;
}

float SyntheticCortex::NextGaussian()
{
   const float u1 = std::max(1e-6f, NextUniform());
   const float u2 = NextUniform();
   return std::sqrt(-2.0f * std::log(u1)) * std::cos((float)kTwoPi * u2);
}

void SyntheticCortex::TriggerGesture(GestureKind kind)
{
   if (kind == GestureKind::None)
      return;
   if (kind == GestureKind::FocusSurge)
   {
      mFocusBoost = 1.0f;
      return;
   }
   if (kind == GestureKind::CalmDrop)
   {
      mCalmBoost = 1.0f;
      return;
   }

   ActiveGesture g;
   g.kind = kind;
   g.vegetative = false;
   g.elapsedSec = 0.0f;
   switch (kind)
   {
      case GestureKind::Blink:       g.durationSec = 0.30f; break; // Intentional squeeze > 210 ms
      case GestureKind::WinkLeft:
      case GestureKind::WinkRight:   g.durationSec = 0.24f; break;
      case GestureKind::JawLeft:
      case GestureKind::JawRight:    g.durationSec = 0.22f; break;
      case GestureKind::TonguePress: g.durationSec = 0.30f; break;
      default:                       g.durationSec = 0.22f; break;
   }
   std::lock_guard<std::mutex> lk(mGestureMu);
   mGestures.push_back(g);
}

void SyntheticCortex::TriggerVegetativeBlink()
{
   ActiveGesture g;
   g.kind = GestureKind::Blink;
   g.vegetative = true;
   g.elapsedSec = 0.0f;
   g.durationSec = 0.125f; // 125 ms spontaneous corneal wetting blink
   std::lock_guard<std::mutex> lk(mGestureMu);
   mGestures.push_back(g);
}

bool SyntheticCortex::LoadCsv(const std::string& path, std::string& outErr)
{
   outErr.clear();
   std::ifstream in(AppPaths::FsPath(path));
   if (!in)
   {
      outErr = "cannot open BCI csv file: " + path;
      return false;
   }

   std::vector<SampleFrame> loaded;
   std::string line;
   double t = 0.0;
   const double dt = 1.0 / kSampleRateHz;
   while (std::getline(in, line))
   {
      if (line.empty() || line[0] == '#')
         continue;
      const char sep = (line.find('\t') != std::string::npos) ? '\t' : ',';
      std::vector<double> cols;
      std::stringstream ss(line);
      std::string cell;
      bool badHeader = false;
      while (std::getline(ss, cell, sep))
      {
         try
         {
            size_t idx = 0;
            double v = std::stod(cell, &idx);
            if (idx == 0) { badHeader = true; break; }
            cols.push_back(v);
         }
         catch (...)
         {
            badHeader = true;
            break;
         }
      }
      if (badHeader || cols.size() < 4)
         continue;

      SampleFrame f;
      // BrainFlow format typically has packet index in col 0 and EEG in cols 1..4;
      // plain 4-col CSV has AF7, AF8, TP9, TP10 directly in cols 0..3.
      const size_t offset = (cols.size() >= 6 && std::floor(cols[0]) == cols[0]) ? 1 : 0;
      for (int c = 0; c < kChannelCount; ++c)
         f.ch[c] = (float)cols[offset + (size_t)c];
      if (cols.size() >= offset + 7)
      {
         f.imu[0] = (float)cols[offset + 4];
         f.imu[1] = (float)cols[offset + 5];
         f.imu[2] = (float)cols[offset + 6];
      }
      f.timestampSec = t;
      t += dt;
      loaded.push_back(f);
   }

   if (loaded.empty())
   {
      outErr = "no valid 4-channel samples found in: " + path;
      return false;
   }
   mCsvFrames = std::move(loaded);
   mCsvCursor = 0;
   return true;
}

void SyntheticCortex::ClearCsv()
{
   mCsvFrames.clear();
   mCsvCursor = 0;
}

void SyntheticCortex::Generate(int count, double sampleRateHz, std::vector<SampleFrame>& outFrames)
{
   outFrames.clear();
   if (count <= 0)
      return;
   outFrames.reserve((size_t)count);

   if (!mCsvFrames.empty())
   {
      for (int i = 0; i < count; ++i)
      {
         outFrames.push_back(mCsvFrames[mCsvCursor]);
         mCsvCursor = (mCsvCursor + 1) % mCsvFrames.size();
      }
      return;
   }

   const double dt = 1.0 / std::max(32.0, sampleRateHz);

   // Preset-specific biophysical regime parameters
   float ampDelta = 5.5f, ampTheta = 7.0f, ampAlpha = 14.0f, ampBeta = 5.0f, ampGamma = 2.2f;
   float slopeChi = 1.55f;
   float kuramotoK = 0.85f;
   float faaSkew = 0.12f; // Positive -> AF8 > AF7 alpha -> warm approach valence
   float tonicBrow = 2.0f;

   switch (mPreset)
   {
      case SyntheticPreset::CalmAlpha:
         ampDelta = 5.0f; ampTheta = 8.5f; ampAlpha = 20.0f; ampBeta = 3.5f; ampGamma = 1.6f;
         slopeChi = 1.68f; kuramotoK = 1.35f; faaSkew = 0.22f; tonicBrow = 1.5f;
         break;
      case SyntheticPreset::DeepFocus:
         ampDelta = 3.0f; ampTheta = 5.0f; ampAlpha = 6.0f; ampBeta = 14.5f; ampGamma = 6.5f;
         slopeChi = 1.12f; kuramotoK = 0.55f; faaSkew = 0.08f; tonicBrow = 16.0f;
         break;
      case SyntheticPreset::ThetaDream:
         ampDelta = 11.0f; ampTheta = 18.0f; ampAlpha = 11.0f; ampBeta = 3.0f; ampGamma = 1.4f;
         slopeChi = 1.85f; kuramotoK = 1.15f; faaSkew = -0.05f; tonicBrow = 1.0f;
         break;
      case SyntheticPreset::MicroGesturePerf:
         ampDelta = 4.5f; ampTheta = 7.5f; ampAlpha = 13.0f; ampBeta = 8.5f; ampGamma = 3.2f;
         slopeChi = 1.40f; kuramotoK = 0.90f; faaSkew = 0.16f; tonicBrow = 6.5f;
         break;
   }

   static constexpr double kBandFreq[kBandCount] = { 2.4, 6.0, 10.2, 20.5, 38.0 };

   std::lock_guard<std::mutex> lk(mGestureMu);
   auto& localGestures = mGestures;

   for (int i = 0; i < count; ++i)
   {
      mTimeSec += dt;
      mFocusBoost *= (float)std::exp(-dt / 2.2);
      mCalmBoost *= (float)std::exp(-dt / 2.5);

      if (mSpontaneousBlinks)
      {
         mSpontBlinkTimerSec -= dt;
         if (mSpontBlinkTimerSec <= 0.0)
         {
            ActiveGesture vb;
            vb.kind = GestureKind::Blink;
            vb.vegetative = true;
            vb.elapsedSec = 0.0f;
            vb.durationSec = 0.125f;
            localGestures.push_back(vb);
            mSpontBlinkTimerSec = 3.4 + 2.0 * NextUniform();
         }
      }

      if (mAutoDemo && mPreset == SyntheticPreset::MicroGesturePerf)
      {
         mAutoTimerSec += dt;
         if (mAutoTimerSec >= 1.65)
         {
            mAutoTimerSec = 0.0;
            static constexpr GestureKind kSeq[] = {
               GestureKind::JawLeft, GestureKind::JawRight, GestureKind::Blink,
               GestureKind::WinkLeft, GestureKind::JawRight, GestureKind::WinkRight
            };
            ActiveGesture ag;
            ag.kind = kSeq[mAutoStep % 6];
            ag.vegetative = false;
            ag.elapsedSec = 0.0f;
            ag.durationSec = (ag.kind == GestureKind::Blink) ? 0.27f : 0.23f;
            localGestures.push_back(ag);
            ++mAutoStep;
         }
      }

      // Modulate slowly over time for natural breathing / thalamocortical drift
      const float breath = 0.5f + 0.5f * (float)std::sin(kTwoPi * 0.16 * mTimeSec);
      const float effAlpha = std::max(2.0f, ampAlpha * (1.0f + 0.18f * breath + 0.9f * mCalmBoost - 0.55f * mFocusBoost));
      const float effBeta = std::max(1.5f, ampBeta * (1.0f + 1.25f * mFocusBoost - 0.4f * mCalmBoost));
      const float effDelta = ampDelta;
      const float effTheta = ampTheta * (1.0f + 0.15f * (float)std::sin(kTwoPi * 0.09 * mTimeSec));
      const float effGamma = ampGamma * (1.0f + 0.7f * mFocusBoost);
      const float bandAmps[kBandCount] = { effDelta, effTheta, effAlpha, effBeta, effGamma };

      SampleFrame f;
      f.timestampSec = mTimeSec;

      // Advance Kuramoto coupled oscillators across the 4 cortical sites
      for (int b = 0; b < kBandCount; ++b)
      {
         double sinSum = 0.0, cosSum = 0.0;
         for (int c = 0; c < kChannelCount; ++c)
         {
            sinSum += std::sin(mPhase[b][c]);
            cosSum += std::cos(mPhase[b][c]);
         }
         const double meanPhase = std::atan2(sinSum, cosSum);
         for (int c = 0; c < kChannelCount; ++c)
         {
            const double detune = (c - 1.5) * 0.18;
            const double dPhi = kTwoPi * (kBandFreq[b] + detune) + kuramotoK * std::sin(meanPhase - mPhase[b][c]);
            mPhase[b][c] = std::fmod(mPhase[b][c] + dPhi * dt + 0.035 * NextGaussian(), kTwoPi);
         }
      }

      // Base cortical signal = 1/f^chi noise + rhythmic bands + gentle brow EMG
      const float browBurst = tonicBrow * (0.6f + 0.4f * (float)std::sin(kTwoPi * 52.0 * mTimeSec)) * NextGaussian();
      for (int c = 0; c < kChannelCount; ++c)
      {
         float v = mPink[c].Step(NextGaussian(), slopeChi) * 14.0f;
         for (int b = 0; b < kBandCount; ++b)
         {
            float scale = 1.0f;
            if (b == 2) // Alpha FAA skew between AF7 (0) and AF8 (1)
            {
               if (c == 0) scale *= (1.0f - faaSkew);
               if (c == 1) scale *= (1.0f + faaSkew);
            }
            v += bandAmps[b] * scale * (float)std::sin(mPhase[b][c]);
         }
         if (c == 0 || c == 1)
            v += browBurst * 0.35f;
         f.ch[c] = v;
      }

      // Superimpose active micro-muscular / EOG dipole gestures
      for (auto it = localGestures.begin(); it != localGestures.end();)
      {
         it->elapsedSec += (float)dt;
         const float u = it->elapsedSec / std::max(0.01f, it->durationSec);
         if (u >= 1.0f)
         {
            it = localGestures.erase(it);
            continue;
         }
         const float env = std::sin(3.14159265f * u);
         switch (it->kind)
         {
            case GestureKind::Blink:
            {
               const float ampUv = it->vegetative ? 135.0f : 245.0f;
               const float shape = it->vegetative ? (env * env) : env;
               const float spike = ampUv * shape;
               f.ch[0] += spike;
               f.ch[1] += spike;
               if (!it->vegetative)
               {
                  // Deliberate squeeze recruits Orbicularis Oculi high-freq MUAPs
                  const float muap = 28.0f * env * NextGaussian();
                  f.ch[0] += muap;
                  f.ch[1] += muap;
               }
               break;
            }
            case GestureKind::WinkLeft:
            {
               const float dipole = 145.0f * env;
               const float muap = 55.0f * env * NextGaussian();
               f.ch[0] += dipole + muap;
               f.ch[1] -= dipole * 0.45f;
               break;
            }
            case GestureKind::WinkRight:
            {
               const float dipole = 145.0f * env;
               const float muap = 55.0f * env * NextGaussian();
               f.ch[1] += dipole + muap;
               f.ch[0] -= dipole * 0.45f;
               break;
            }
            case GestureKind::JawLeft:
            {
               // Broadband 45-110 Hz Temporalis MUAP burst on TP9 (ch 2)
               const float burst = 140.0f * env * (0.65f * (float)std::sin(kTwoPi * 74.0 * mTimeSec) + 0.75f * NextGaussian());
               f.ch[2] += burst;
               f.ch[3] += burst * 0.12f;
               break;
            }
            case GestureKind::JawRight:
            {
               // Broadband 45-110 Hz Temporalis MUAP burst on TP10 (ch 3)
               const float burst = 140.0f * env * (0.65f * (float)std::sin(kTwoPi * 78.0 * mTimeSec) + 0.75f * NextGaussian());
               f.ch[3] += burst;
               f.ch[2] += burst * 0.12f;
               break;
            }
            case GestureKind::TonguePress:
            {
               // Common-mode negative glossokinetic potential (GKP)
               const float gkp = -75.0f * env;
               for (int c = 0; c < kChannelCount; ++c)
                  f.ch[c] += gkp;
               break;
            }
            default:
               break;
         }
         ++it;
      }

      // Synthetic PPG (~1.1 Hz cardiac pulse + dicrotic notch) and gentle head IMU
      const double hrPhase = std::fmod(mTimeSec * 1.12, 1.0);
      f.ppg = (float)(std::exp(-std::pow((hrPhase - 0.18) / 0.08, 2.0)) +
                      0.35 * std::exp(-std::pow((hrPhase - 0.36) / 0.10, 2.0)));
      f.imu[0] = 0.02f * (float)std::sin(kTwoPi * 0.25 * mTimeSec);
      f.imu[1] = 0.015f * (float)std::cos(kTwoPi * 0.19 * mTimeSec);
      f.imu[2] = 0.01f * (float)std::sin(kTwoPi * 0.11 * mTimeSec);

      outFrames.push_back(f);
   }
}
}
