#pragma once

#include "platform/Platform.h"

#include <cmath>

// AUDIOPARAMSWEEPTEST helper: a one-second stereo 220 Hz tone for sample-based nodes to load from
// SweepPrepare(), so their params are observable instead of baselined as "no sample loaded".
// Caller owns the returned buffer (normally handing it straight to the node's SampleSlot).
inline Platform::SampleBuffer* MakeSweepToneBuffer()
{
   auto* buf = new Platform::SampleBuffer();
   buf->channels = 2;
   buf->numFrames = 48000;
   buf->sampleRate = 48000.0;
   buf->channelData.assign((size_t)buf->numFrames * 2, 0.0f);
   for (int i = 0; i < buf->numFrames; i++)
   {
      const float s = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * (float)i / 48000.0f);
      buf->channelData[(size_t)i] = s;
      buf->channelData[(size_t)buf->numFrames + (size_t)i] = s;
   }
   return buf;
}
