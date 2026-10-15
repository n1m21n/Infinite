#include <cmath>
#include <map>
#include <mutex>

#include "audio/dsp/HrtfKemarData.h"
#include "audio/dsp/HrtfVoice.h"

namespace Hrtf
{
   namespace
   {
      // Windowed-sinc value of the 44.1 kHz response h at fractional index x.
      float SincAt(const float* h, int n, double x)
      {
         const int c = (int)std::floor(x);
         double acc = 0.0;
         for (int k = c - 7; k <= c + 8; k++)
         {
            if (k < 0 || k >= n)
               continue;
            const double d = x - (double)k;
            const double sinc = std::fabs(d) < 1e-9 ? 1.0 : std::sin(M_PI * d) / (M_PI * d);
            const double win = 0.5 + 0.5 * std::cos(M_PI * d / 8.5);
            acc += (double)h[k] * sinc * win;
         }
         return (float)acc;
      }

      std::shared_ptr<const Set> Build(double sr)
      {
         using namespace HrtfKemar;
         auto s = std::make_shared<Set>();
         const double ratio = sr / kSampleRate;
         s->numEl = kNumEl;
         s->numAz = kNumAz;
         s->taps = (int)std::ceil((double)kTaps * ratio);
         s->elMin = (float)kElMin;
         s->elStep = (float)kElStep;
         s->azStep = (float)kAzStep;
         s->sampleRate = sr;
         s->ir.assign((size_t)kNumEl * kNumAz * 2 * s->taps, 0.0f);
         s->delay.assign((size_t)kNumEl * kNumAz * 2, 0.0f);
         std::vector<float> src(kTaps);
         for (int e = 0; e < kNumEl; e++)
            for (int a = 0; a < kNumAz; a++)
               for (int ear = 0; ear < 2; ear++)
               {
                  const size_t idx = ((size_t)e * kNumAz + a) * 2 + ear;
                  for (int k = 0; k < kTaps; k++)
                     src[k] = (float)kIr[idx * kTaps + k] * kScale;
                  float* dst = &s->ir[idx * s->taps];
                  if (std::fabs(ratio - 1.0) < 1e-9)
                     for (int k = 0; k < kTaps; k++)
                        dst[k] = src[k];
                  else
                     for (int k = 0; k < s->taps; k++)
                        dst[k] = SincAt(src.data(), kTaps, (double)k / ratio) / (float)ratio;
                  s->delay[idx] = (float)((double)kDelay[idx] * ratio);
               }
         return s;
      }
   }

   std::shared_ptr<const Set> Set::Get(double sampleRate)
   {
      static std::mutex mu;
      static std::map<long, std::shared_ptr<const Set>> cache;
      std::lock_guard<std::mutex> lock(mu);
      const long key = std::lround(sampleRate);
      auto it = cache.find(key);
      if (it != cache.end())
         return it->second;
      auto s = Build(sampleRate > 0.0 ? sampleRate : 48000.0);
      cache[key] = s;
      return s;
   }
}
