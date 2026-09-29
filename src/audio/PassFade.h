#pragma once

#include <algorithm>

// Per-pass fade gain shared by the Sampler, the Looper and the MPC.
//
// A "pass" is one traversal of the range [startPos, endPos] (source frames) in
// the direction of travel (dirSign < 0 walks it backwards, so reverse and
// negative speed fold in): the fade-in ramps from the edge playback enters at,
// the fade-out ramps to the edge it leaves by. A looping sample or loop dips at
// its seam instead of clicking, and a one-shot opens and closes softly.
//
// Lengths are output-time milliseconds, converted to source frames through
// `rate` (source frames per output sample), and scaled down together if the two
// would not fit in the range. A voice that starts mid-range already sits past
// the fade-in and gets no ramp. Both at 0 is exactly 1.0 (bit-identical to no
// fade). Audio-thread safe: no allocation, no state.
namespace PassFade
{
   inline float Gain(double pos, double startPos, double endPos, float dirSign, float fadeInMs, float fadeOutMs,
                     float rate, double sampleRate)
   {
      if ((fadeInMs <= 0.0f && fadeOutMs <= 0.0f) || sampleRate <= 0.0)
         return 1.0f;
      const double perMs = 0.001 * sampleRate * (double)rate;
      double inF = std::max(0.0, (double)fadeInMs) * perMs;
      double outF = std::max(0.0, (double)fadeOutMs) * perMs;
      const double len = std::max(1.0, endPos - startPos);
      if (inF + outF > len)
      {
         const double k = len / (inF + outF);
         inF *= k;
         outF *= k;
      }
      const double fromEntry = (dirSign >= 0.0f) ? (pos - startPos) : (endPos - pos);
      const double toExit = (dirSign >= 0.0f) ? (endPos - pos) : (pos - startPos);
      float g = 1.0f;
      if (inF > 0.0)
         g *= (float)std::clamp(fromEntry / inF, 0.0, 1.0);
      if (outF > 0.0)
         g *= (float)std::clamp(toExit / outF, 0.0, 1.0);
      return g;
   }
}
