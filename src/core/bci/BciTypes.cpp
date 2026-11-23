#include "BciTypes.h"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>

namespace Bci
{
namespace
{
   struct GlobalEntry
   {
      const char* name;
      int pinIndex; // maps into BrainSourceNode::Output 0..23, or -1 for derived
   };

   constexpr GlobalEntry kBrainGlobals[] = {
      { "presence",   0 },
      { "arousal",    1 },
      { "valence",    2 },
      { "entropy",    3 },
      { "coherence",  4 },
      { "density",    5 },
      { "smoothness", 6 },
      { "motion",     7 },
      { "space",      8 },
      { "tension",    9 },
      { "drift",      10 },
      { "surge",      11 },
      { "stillness",  12 },
      { "delta",      13 },
      { "theta",      14 },
      { "alpha",      15 },
      { "beta",       16 },
      { "gamma",      17 },
      { "blink",      18 },
      { "gaze_lr",    19 },
      { "brow",       20 },
      { "jaw_left",   21 },
      { "jaw_right",  22 },
      { "heart_pulse",23 },
      // Friendly short aliases
      { "gaze",       19 },
      { "heart",      23 },
      { "jaw",        -1 } // max(jaw_left, jaw_right)
   };

   constexpr int kGlobalCount = (int)(sizeof(kBrainGlobals) / sizeof(kBrainGlobals[0]));

   std::atomic<float> gBrainVals[kOutputPinCount] = {};
   std::atomic<bool> gBrainActive{ false };
   std::atomic<double> gLastTransientSec{ -10.0 };

   double NowSteadySec()
   {
      return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   }
}

int BrainGlobalCount()
{
   return kGlobalCount;
}

const char* BrainGlobalName(int id)
{
   if (id < 0 || id >= kGlobalCount)
      return "";
   return kBrainGlobals[id].name;
}

int FindBrainGlobalId(const std::string& field)
{
   for (int i = 0; i < kGlobalCount; ++i)
   {
      if (field == kBrainGlobals[i].name)
         return i;
   }
   return -1;
}

bool IsBrainGlobalField(const std::string& field)
{
   return FindBrainGlobalId(field) >= 0;
}

void PublishBrainGlobals(const float* vals24, bool active)
{
   gBrainActive.store(active, std::memory_order_relaxed);
   if (!vals24)
      return;
   for (int i = 0; i < kOutputPinCount; ++i)
      gBrainVals[i].store(std::clamp(vals24[i], 0.0f, 1.0f), std::memory_order_relaxed);
}

float ReadBrainGlobal(int id)
{
   if (id < 0 || id >= kGlobalCount)
      return 0.0f;
   const int pin = kBrainGlobals[id].pinIndex;
   if (pin >= 0 && pin < kOutputPinCount)
      return gBrainVals[pin].load(std::memory_order_relaxed);
   // Derived alias: `brain.jaw` = max(jaw_left, jaw_right)
   return std::max(gBrainVals[21].load(std::memory_order_relaxed),
                   gBrainVals[22].load(std::memory_order_relaxed));
}

float ReadBrainGlobal(const std::string& fieldOrPrefixed)
{
   const std::string field = (fieldOrPrefixed.rfind("brain.", 0) == 0)
      ? fieldOrPrefixed.substr(6)
      : fieldOrPrefixed;
   const int id = FindBrainGlobalId(field);
   return id >= 0 ? ReadBrainGlobal(id) : 0.0f;
}

void PopulateBrainGlobalsMap(std::map<std::string, float>& out)
{
   for (int i = 0; i < kGlobalCount; ++i)
      out[std::string("brain.") + kBrainGlobals[i].name] = ReadBrainGlobal(i);
}

void ReadBrainLatent6(float out6[6])
{
   out6[0] = gBrainVals[1].load(std::memory_order_relaxed);  // Arousal
   out6[1] = gBrainVals[2].load(std::memory_order_relaxed);  // Valence
   out6[2] = gBrainVals[3].load(std::memory_order_relaxed);  // Entropy
   out6[3] = gBrainVals[4].load(std::memory_order_relaxed);  // Coherence
   out6[4] = gBrainVals[20].load(std::memory_order_relaxed); // BrowTension
   out6[5] = std::max(gBrainVals[21].load(std::memory_order_relaxed),
                      gBrainVals[22].load(std::memory_order_relaxed)); // JawMax
}

void NotifyOutputTransient(float audioRmsDelta, float lumaDelta)
{
   if (audioRmsDelta > 0.28f || lumaDelta > 0.35f)
      gLastTransientSec.store(NowSteadySec(), std::memory_order_relaxed);
}

double LastOutputTransientSec()
{
   return gLastTransientSec.load(std::memory_order_relaxed);
}

void BrainLightFeatures(const float* z, const float* phase, float state01, BrainLightFrame& out)
{
   constexpr float kPi = 3.14159265358979f;
   for (int c = 0; c < kChannelCount; ++c)
   {
      double eSum = 0.0, e2Sum = 0.0, fastSum = 0.0, presSum = 0.0, vx = 0.0, vy = 0.0;
      int nFast = 0;
      for (int k = 0; k < kImageBins; ++k)
      {
         const double zz = (double)z[c * kImageBins + k];
         const double e = std::max(zz - 1.0, 0.0);
         const double hue = (double)(280.0f * kPi / 180.0f) * (double)k / 39.0; // bin k = (k + 1) Hz
         eSum += e;
         e2Sum += e * e;
         vx += e * e * std::cos(hue);
         vy += e * e * std::sin(hue);
         presSum += std::max(zz + 3.0, 0.0);
         if (k + 1 >= 25)
         {
            fastSum += e;
            ++nFast;
         }
      }
      out.a[c] = (float)std::tanh(eSum / kImageBins / 0.6);
      out.hue[c] = (float)std::atan2(vy, vx);
      out.vivid[c] = (float)(std::sqrt(vx * vx + vy * vy) / (e2Sum + 1e-9));
      out.grain[c] = (float)std::tanh(fastSum / std::max(1, nFast) / 6.0);
      out.pres[c] = (float)std::tanh(presSum / kImageBins / 3.0);
      out.phase[c] = phase[c];
   }
   out.state = std::clamp(state01, 0.0f, 1.0f);
}

void BrainLightPixel(const BrainLightFrame& f, float u, float v, float hOverW, float noise, float rgb[3])
{
   constexpr float kPi = 3.14159265358979f;
   const float hueAlpha = 280.0f * kPi / 180.0f * 9.0f / 39.0f; // 10 Hz
   float w[kChannelCount], d[kChannelCount], wSum = 0.0f;
   for (int c = 0; c < kChannelCount; ++c)
   {
      const float dx = u - kLightPos[c][0], dy = (v - kLightPos[c][1]) * hOverW;
      d[c] = std::sqrt(dx * dx + dy * dy);
      w[c] = 1.0f / (d[c] * d[c] + kLightEps);
      wSum += w[c];
   }
   float m = 0.0f, vx = 0.0f, vy = 0.0f, val = 0.0f, grain = 0.0f;
   for (int c = 0; c < kChannelCount; ++c)
   {
      const float wc = w[c] / wSum;
      const float pull = kLightState * f.state * f.pres[c];
      const float mc = wc * (f.a[c] + kLightGlow * f.pres[c] + pull);
      m += mc;
      vx += wc * (f.a[c] * f.vivid[c] * std::cos(f.hue[c]) + pull * std::cos(hueAlpha));
      vy += wc * (f.a[c] * f.vivid[c] * std::sin(f.hue[c]) + pull * std::sin(hueAlpha));
      val += mc * (0.6f + 0.4f * std::cos(kLightRing * d[c] - f.phase[c] / kLightSlow));
      grain += wc * f.grain[c];
   }
   val = std::clamp(val + grain * 0.5f * noise, 0.0f, 1.0f);
   const float sat = std::clamp(std::sqrt(vx * vx + vy * vy) / (m + 1e-9f), 0.0f, 1.0f);
   // HSV -> RGB, hue in radians
   float h = std::atan2(vy, vx);
   h = std::fmod(h, 2.0f * kPi);
   if (h < 0.0f)
      h += 2.0f * kPi;
   h /= kPi / 3.0f;
   const int i = (int)std::floor(h) % 6;
   const float fr = h - std::floor(h);
   const float p = val * (1 - sat), q = val * (1 - sat * fr), t = val * (1 - sat * (1 - fr));
   const float table[6][3] = { { val, t, p }, { q, val, p }, { p, val, t }, { p, q, val }, { t, p, val }, { val, p, q } };
   for (int k = 0; k < 3; ++k)
      rgb[k] = table[i][k];
}
}
