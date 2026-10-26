#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "platform/Platform.h"

// Hit starts of a decoded buffer, in file frames, into buf.transients. Runs
// where the file is decoded (import worker, decode cache), once per decode,
// so opening a file never pays for it on the main thread. A hit is a 2.5 ms
// peak envelope that jumps 9 dB above its lowest point over the previous 10 ms, within 45 dB of
// the file's loudest moment, at least 40 ms after the last one; its start is
// the first sample above a fifth of that peak, less 1 ms so the attack is
// whole. A note fading in from silence counts too, which is harmless: the
// transient pass then plays the note's first few ms unstretched.
inline void DetectTransients(Platform::SampleBuffer& buf)
{
   buf.transients.clear();
   buf.envelope.clear();
   buf.envelopeHop = 0;
   if (buf.channels <= 0 || buf.numFrames <= 0)
      return;
   const double sr = buf.sampleRate > 0.0 ? buf.sampleRate : 48000.0;
   const int hop = std::max(16, (int)std::lround(sr * 0.0025));
   const int n = buf.numFrames;
   const int hops = n / hop;
   if (hops < 8)
      return;
   auto mono = [&](int i) {
      float v = 0.0f;
      for (int c = 0; c < buf.channels; c++)
         v += buf.channelData[(size_t)c * (size_t)n + (size_t)i];
      return std::fabs(v) / (float)buf.channels;
   };
   std::vector<float> env((size_t)hops);
   buf.envelope.assign((size_t)hops, 0.0f);
   buf.envelopeHop = hop;
   float loudest = 0.0f;
   for (int j = 0; j < hops; j++)
   {
      float pk = 0.0f;
      for (int i = j * hop; i < (j + 1) * hop; i++)
         pk = std::max(pk, mono(i));
      buf.envelope[(size_t)j] = pk;
      env[(size_t)j] = 20.0f * std::log10(pk + 1.0e-9f);
      loudest = std::max(loudest, env[(size_t)j]);
   }
   const int look = 4, refractory = (int)std::lround(0.040 * sr);
   int64_t last = -(int64_t)refractory;
   for (int j = look; j < hops; j++)
   {
      float lo = env[(size_t)j - 1];
      for (int k = 2; k <= look; k++)
         lo = std::min(lo, env[(size_t)j - k]);
      if (env[(size_t)j] < loudest - 45.0f || env[(size_t)j] - lo < 9.0f)
         continue;
      const float th = 0.2f * std::pow(10.0f, env[(size_t)j] / 20.0f);
      int start = (j - 1) * hop;
      while (start < (j + 1) * hop - 1 && mono(start) < th)
         start++;
      if ((int64_t)start - last < refractory)
         continue;
      last = start;
      buf.transients.push_back(std::max(0.0, (double)start - 0.001 * sr));
   }
}
