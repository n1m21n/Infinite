#include "BciTypes.h"

#include <algorithm>
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
}
