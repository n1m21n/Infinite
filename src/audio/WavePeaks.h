#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Turbo 0.49: multi-resolution peak cache for waveform views.
//
// Built once on the main thread when a buffer loads (never per frame), read
// only by the main thread's draw code. A mip chain of min/max/RMS buckets:
// level 0 is the finest (up to kMaxBuckets buckets over the whole buffer),
// each next level aggregates kLevelFactor buckets of the previous one, down
// to about kMinBuckets. The draw code picks the coarsest level that still
// gives at least one bucket per on-screen pixel, so a narrow node and a
// zoomed-in trim range both look right without re-reading the samples.
//
// Values are stored as int16 (-1..1 mapped to +-32767): 6 bytes per bucket,
// about 65 KB for a full chain of 8192 buckets. Stereo is folded into one
// envelope: min of the channel minima, max of the maxima, RMS over both.
class WavePeaks
{
public:
   static constexpr int kMaxBuckets = 8192;
   static constexpr int kMinBuckets = 64;
   static constexpr int kLevelFactor = 4;

   struct Level
   {
      int count = 0;   // buckets in this level
      int span = 1;    // level-0 buckets per bucket (kLevelFactor^level)
      std::vector<int16_t> mn, mx, rms;
   };

   void Clear()
   {
      mLevels.clear();
      mNumFrames = 0;
      mSampleRate = 0.0;
      mBase = 0;
   }

   bool Empty() const { return mLevels.empty(); }
   int NumFrames() const { return mNumFrames; }
   double SampleRate() const { return mSampleRate; }
   double Seconds() const { return mSampleRate > 0.0 ? (double)mNumFrames / mSampleRate : 0.0; }
   int NumLevels() const { return (int)mLevels.size(); }
   const Level& LevelAt(int i) const { return mLevels[(size_t)std::clamp(i, 0, NumLevels() - 1)]; }
   // Level-0 bucket count (the finest resolution held).
   int BaseCount() const { return mBase; }

   // `planar` follows Platform::SampleBuffer's layout: channel 0's numFrames
   // samples, then channel 1's, and so on.
   void Build(const float* planar, int channels, int numFrames, double sampleRate)
   {
      Clear();
      if (planar == nullptr || channels <= 0 || numFrames <= 0)
         return;
      mNumFrames = numFrames;
      mSampleRate = sampleRate;
      mBase = std::min(kMaxBuckets, numFrames);

      Level l0;
      l0.count = mBase;
      l0.span = 1;
      l0.mn.resize((size_t)mBase);
      l0.mx.resize((size_t)mBase);
      l0.rms.resize((size_t)mBase);
      for (int b = 0; b < mBase; b++)
      {
         // Exact integer bucket edges, so every frame lands in exactly one bucket.
         const int f0 = (int)((int64_t)b * numFrames / mBase);
         const int f1 = std::max(f0 + 1, (int)((int64_t)(b + 1) * numFrames / mBase));
         float mn = 0.0f, mx = 0.0f;
         double sq = 0.0;
         int n = 0;
         for (int c = 0; c < channels; c++)
         {
            const float* d = planar + (size_t)c * (size_t)numFrames;
            for (int f = f0; f < f1; f++)
            {
               const float s = d[f];
               mn = std::min(mn, s);
               mx = std::max(mx, s);
               sq += (double)s * s;
               n++;
            }
         }
         l0.mn[(size_t)b] = Quant(mn);
         l0.mx[(size_t)b] = Quant(mx);
         l0.rms[(size_t)b] = Quant(n > 0 ? (float)std::sqrt(sq / n) : 0.0f);
      }
      mLevels.push_back(std::move(l0));

      while (mLevels.back().count > kMinBuckets)
      {
         const Level& prev = mLevels.back();
         Level next;
         next.count = (prev.count + kLevelFactor - 1) / kLevelFactor;
         next.span = prev.span * kLevelFactor;
         next.mn.resize((size_t)next.count);
         next.mx.resize((size_t)next.count);
         next.rms.resize((size_t)next.count);
         for (int b = 0; b < next.count; b++)
         {
            int16_t mn = 0, mx = 0;
            double sq = 0.0;
            int n = 0;
            for (int k = b * kLevelFactor; k < std::min(prev.count, (b + 1) * kLevelFactor); k++)
            {
               mn = std::min(mn, prev.mn[(size_t)k]);
               mx = std::max(mx, prev.mx[(size_t)k]);
               const double r = prev.rms[(size_t)k];
               sq += r * r;
               n++;
            }
            next.mn[(size_t)b] = mn;
            next.mx[(size_t)b] = mx;
            next.rms[(size_t)b] = (int16_t)std::lround(n > 0 ? std::sqrt(sq / n) : 0.0);
         }
         mLevels.push_back(std::move(next));
      }
   }

   // Convenience for Platform::SampleBuffer (or anything with the same
   // channelData/channels/numFrames/sampleRate fields). Never reads past
   // channelData even if `channels` overstates what was decoded.
   template <typename Buf>
   void BuildFrom(const Buf& b)
   {
      const int frames = std::max(0, b.numFrames);
      const int ch = frames > 0 ? std::min(std::max(1, b.channels), (int)(b.channelData.size() / (size_t)frames)) : 0;
      Build(ch > 0 ? b.channelData.data() : nullptr, ch, frames, b.sampleRate);
   }

   // Min/max/peak RMS (as -1..1 floats) over the buffer fraction [f0, f1),
   // read from level `levelIndex` (see PickLevel). False for an empty cache.
   bool Range(float f0, float f1, int levelIndex, float& outMin, float& outMax, float& outRms) const
   {
      if (mLevels.empty())
         return false;
      const Level& l = LevelAt(levelIndex);
      const double scale = (double)mBase / (double)l.span;
      int i0 = (int)std::floor((double)f0 * scale);
      int i1 = (int)std::ceil((double)f1 * scale);
      i0 = std::clamp(i0, 0, l.count - 1);
      i1 = std::clamp(i1, i0 + 1, l.count);
      int16_t mn = 0, mx = 0, rms = 0;
      for (int i = i0; i < i1; i++)
      {
         mn = std::min(mn, l.mn[(size_t)i]);
         mx = std::max(mx, l.mx[(size_t)i]);
         rms = std::max(rms, l.rms[(size_t)i]);
      }
      outMin = (float)mn / 32767.0f;
      outMax = (float)mx / 32767.0f;
      outRms = (float)rms / 32767.0f;
      return true;
   }

   // Coarsest level whose bucket width is still at most one pixel, given
   // `pixels` columns covering `spanFrac` of the buffer.
   int PickLevel(float spanFrac, float pixels) const
   {
      if (mLevels.empty() || pixels <= 0.0f)
         return 0;
      const double bucketsNeeded = (double)pixels / std::max(1e-6, (double)spanFrac); // over the whole buffer
      int best = 0;
      for (int i = 0; i < NumLevels(); i++)
      {
         if ((double)mLevels[(size_t)i].count >= bucketsNeeded)
            best = i;
         else
            break;
      }
      return best;
   }

private:
   static int16_t Quant(float v) { return (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }

   std::vector<Level> mLevels;
   int mNumFrames = 0;
   double mSampleRate = 0.0;
   int mBase = 0;
};

// Turbo 0.49: lock-free play cursor slots. The audio thread publishes each
// voice's position once per block (0..1 of the WHOLE buffer, so a cursor
// lines up with the trim handles drawn over the same waveform; < 0 = idle);
// the main thread reads them in draw code. Relaxed atomics: a cursor one
// block stale is invisible, and nothing else is ordered against them.
template <int N>
class PlayCursorSet
{
public:
   static constexpr int kSlots = N;

   PlayCursorSet()
   {
      for (auto& p : mPos)
         p.store(-1.0f, std::memory_order_relaxed);
   }

   // Audio thread.
   void Publish(int slot, float frac)
   {
      if (slot >= 0 && slot < N)
         mPos[slot].store(frac, std::memory_order_relaxed);
   }
   void Idle(int slot) { Publish(slot, -1.0f); }
   void IdleAll()
   {
      for (auto& p : mPos)
         p.store(-1.0f, std::memory_order_relaxed);
   }

   // Main thread: raw slot value, < 0 when idle.
   float At(int slot) const { return (slot >= 0 && slot < N) ? mPos[slot].load(std::memory_order_relaxed) : -1.0f; }

   // Main thread: copies the active positions of slots [first, first+count)
   // into `out` (at most `max`), returns how many.
   int Collect(float* out, int max, int first = 0, int count = N) const
   {
      int k = 0;
      for (int s = std::max(0, first); s < std::min(N, first + count) && k < max; s++)
      {
         const float p = mPos[s].load(std::memory_order_relaxed);
         if (p >= 0.0f)
            out[k++] = p;
      }
      return k;
   }

private:
   std::atomic<float> mPos[N];
};
