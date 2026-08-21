#pragma once

#include <algorithm>
#include <vector>

#include "AudioBuffer.h"

// A per-branch, exact-integer-sample delay used purely for plugin/effect
// delay compensation (PDC) at a topology merge point - a multi-input node's
// input pin (Mixer, Dynamics' sidechain, ...) or the terminal summation in
// AudioEngine::RunTopology. Unlike DelayKernel's DelayLine (fractional,
// musically modulatable, read at an arbitrary swept delay time), this is a
// fixed integer delay decided once, on the main thread, when
// RebuildAudioTopology (main.cpp) computes each branch's cumulative latency
// and how far behind the slowest sibling merging with it each branch is -
// see its comment for the design. The amount never changes for the lifetime
// of one topology generation.
//
// Zero-delay branches - the overwhelming majority, since most chains carry
// no latent kernel/plugin at all - allocate nothing: Prepare(0, ...) leaves
// `mBuf` empty and ProcessBlock becomes a pure passthrough, so a topology
// with no latent nodes anywhere pays nothing extra for this.
class CompensationDelay
{
public:
   // Main thread only, called once when the topology is (re)built.
   // `delaySamples` is this branch's compensation amount; `numChannels` is
   // the channel count to allocate for - callers pass the same fixed
   // capacity the rest of the engine's pooled buffers use, so a topology is
   // never under-allocated if the device's actual channel count varies
   // within one topology generation's lifetime.
   void Prepare(int delaySamples, int numChannels)
   {
      mDelay = std::max(0, delaySamples);
      mChannels = std::max(0, numChannels);
      mWritePos = 0;
      if (mDelay == 0 || mChannels == 0)
      {
         mBuf.clear();
         mBuf.shrink_to_fit();
         return;
      }
      mBuf.assign((size_t)mChannels * (size_t)mDelay, 0.0f);
   }

   bool IsActive() const { return mDelay > 0 && !mBuf.empty(); }

   // Audio thread. Reads `in`, writes the delayed signal into `out` - `in`
   // and `out` may be the same buffer (each sample is read from the ring
   // before it's overwritten with the corresponding input sample, so
   // in-place is safe). A no-op copy when this delay is inactive (0
   // samples); callers only need to route a branch through this at all when
   // IsActive() is true, but calling it unconditionally is also correct.
   void ProcessBlock(const AudioBuffer& in, AudioBuffer& out)
   {
      const int numFrames = in.numFrames;
      const int numChannels = std::min({ in.numChannels, out.numChannels, mChannels });

      if (!IsActive())
      {
         if (in.channels != out.channels)
            for (int ch = 0; ch < numChannels; ch++)
               for (int i = 0; i < numFrames; i++)
                  out.channels[ch][i] = in.channels[ch][i];
         return;
      }

      int pos = mWritePos;
      for (int i = 0; i < numFrames; i++)
      {
         const int p = pos;
         for (int ch = 0; ch < numChannels; ch++)
         {
            float* ring = &mBuf[(size_t)ch * (size_t)mDelay];
            const float delayed = ring[p];
            ring[p] = in.channels[ch][i];
            out.channels[ch][i] = delayed;
         }
         pos++;
         if (pos >= mDelay)
            pos = 0;
      }
      mWritePos = pos;
   }

   void Reset()
   {
      std::fill(mBuf.begin(), mBuf.end(), 0.0f);
      mWritePos = 0;
   }

private:
   std::vector<float> mBuf; // [channel][delay], channel-major, empty when inactive
   int mDelay = 0;
   int mChannels = 0;
   int mWritePos = 0;
};
