#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Multi-resolution min / max / RMS envelope of a sample, for waveform views.
//
// Built once on the main thread when a buffer loads (never per frame) and read only by
// draw code on the same thread. Level 0 holds up to kMaxBuckets buckets over the whole
// buffer; each further level merges kFactor buckets of the one below, down to about
// kMinBuckets. Range() answers "what does frames [f0, f1) look like" from the coarsest
// level that still resolves the range, so a draw call can ask for one column per screen
// pixel at any width or zoom without touching the samples again.
//
// Values are stored as int16 (-1..1 to +-32767). Every channel folds into one envelope:
// the lowest minimum, the highest maximum, RMS over all channels.
// The idea (a shared peak cache, one column per pixel) comes from Ricardo Palmieri's
// Infinite-Turbo fork; this is a fresh implementation.
class WavePeaks
{
public:
   static constexpr int kMaxBuckets = 16384;
   static constexpr int kMinBuckets = 64;
   static constexpr int kFactor = 4;

   void Clear()
   {
      mLevels.clear();
      mNumFrames = 0;
      mSampleRate = 0.0;
      mFramesPerBucket = 1.0;
   }

   bool Empty() const { return mLevels.empty(); }
   int NumFrames() const { return mNumFrames; }
   double SampleRate() const { return mSampleRate; }
   double Seconds() const { return mSampleRate > 0.0 ? (double)mNumFrames / mSampleRate : 0.0; }

   // `data` holds `channels` runs of `numFrames` samples each, `stride` floats apart
   // (0 = numFrames, the planar layout Platform::SampleBuffer uses).
   void Build(const float* data, int numFrames, double sampleRate, int channels = 1, size_t stride = 0)
   {
      Clear();
      if (data == nullptr || numFrames <= 0 || channels <= 0)
         return;
      if (stride == 0)
         stride = (size_t)numFrames;
      mNumFrames = numFrames;
      mSampleRate = sampleRate;
      const int base = std::min(kMaxBuckets, numFrames);
      mFramesPerBucket = (double)numFrames / (double)base;

      Level l0;
      l0.Resize(base);
      for (int b = 0; b < base; b++)
      {
         // Integer bucket edges, so every frame lands in exactly one bucket.
         const int f0 = (int)((int64_t)b * numFrames / base);
         const int f1 = std::max(f0 + 1, (int)((int64_t)(b + 1) * numFrames / base));
         float mn = 0.0f, mx = 0.0f;
         double sq = 0.0;
         for (int c = 0; c < channels; c++)
         {
            const float* d = data + (size_t)c * stride;
            for (int f = f0; f < f1; f++)
            {
               mn = std::min(mn, d[f]);
               mx = std::max(mx, d[f]);
               sq += (double)d[f] * d[f];
            }
         }
         l0.mn[(size_t)b] = Quant(mn);
         l0.mx[(size_t)b] = Quant(mx);
         l0.rms[(size_t)b] = Quant((float)std::sqrt(sq / (double)((f1 - f0) * channels)));
      }
      mLevels.push_back(std::move(l0));

      while (mLevels.back().Count() > kMinBuckets)
      {
         const Level& prev = mLevels.back();
         Level next;
         next.Resize((prev.Count() + kFactor - 1) / kFactor);
         for (int b = 0; b < next.Count(); b++)
         {
            const int k0 = b * kFactor;
            const int k1 = std::min(prev.Count(), k0 + kFactor);
            int16_t mn = 0, mx = 0;
            double sq = 0.0;
            for (int k = k0; k < k1; k++)
            {
               mn = std::min(mn, prev.mn[(size_t)k]);
               mx = std::max(mx, prev.mx[(size_t)k]);
               sq += (double)prev.rms[(size_t)k] * prev.rms[(size_t)k];
            }
            next.mn[(size_t)b] = mn;
            next.mx[(size_t)b] = mx;
            next.rms[(size_t)b] = (int16_t)std::lround(std::sqrt(sq / (double)(k1 - k0)));
         }
         mLevels.push_back(std::move(next));
      }
   }

   // From a decoded buffer with planar `channelData`, `channels`, `numFrames` and `sampleRate`
   // (Platform::SampleBuffer); a buffer that holds fewer channels than it claims reads what it has.
   template <class Buffer>
   void BuildFrom(const Buffer& b)
   {
      const int frames = b.numFrames;
      const int held = frames > 0 ? (int)(b.channelData.size() / (size_t)frames) : 0;
      const int channels = std::clamp(b.channels, 1, std::max(1, held));
      if (held <= 0)
      {
         Clear();
         return;
      }
      Build(b.channelData.data(), frames, b.sampleRate, channels);
   }

   // Envelope of frames [f0, f1). A range narrower than one level-0 bucket reads that bucket.
   void Range(double f0, double f1, float& mn, float& mx, float& rms) const
   {
      mn = mx = rms = 0.0f;
      if (mLevels.empty() || f1 <= 0.0 || f0 >= (double)mNumFrames)
         return;
      const int base = mLevels[0].Count();
      int b0 = (int)std::floor(std::max(0.0, f0) / mFramesPerBucket);
      int b1 = (int)std::ceil(std::min((double)mNumFrames, f1) / mFramesPerBucket);
      b0 = std::clamp(b0, 0, base - 1);
      b1 = std::clamp(b1, b0 + 1, base);

      // Coarsest level whose bucket is no wider than the range.
      int level = 0;
      int span = 1;
      while (level + 1 < (int)mLevels.size() && span * kFactor <= b1 - b0)
      {
         level++;
         span *= kFactor;
      }
      const Level& l = mLevels[(size_t)level];
      const int k0 = std::min(b0 / span, l.Count() - 1);
      const int k1 = std::clamp((b1 + span - 1) / span, k0 + 1, l.Count());
      int16_t qmn = 0, qmx = 0;
      double sq = 0.0;
      for (int k = k0; k < k1; k++)
      {
         qmn = std::min(qmn, l.mn[(size_t)k]);
         qmx = std::max(qmx, l.mx[(size_t)k]);
         sq += (double)l.rms[(size_t)k] * l.rms[(size_t)k];
      }
      mn = (float)qmn / 32767.0f;
      mx = (float)qmx / 32767.0f;
      rms = (float)std::sqrt(sq / (double)(k1 - k0)) / 32767.0f;
   }

   // Same, over a 0..1 fraction of the buffer.
   void RangeFrac(double a, double b, float& mn, float& mx, float& rms) const
   {
      Range(a * mNumFrames, b * mNumFrames, mn, mx, rms);
   }

private:
   struct Level
   {
      std::vector<int16_t> mn, mx, rms;
      int Count() const { return (int)mn.size(); }
      void Resize(int n)
      {
         mn.assign((size_t)n, 0);
         mx.assign((size_t)n, 0);
         rms.assign((size_t)n, 0);
      }
   };

   static int16_t Quant(float v) { return (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }

   std::vector<Level> mLevels;
   int mNumFrames = 0;
   double mSampleRate = 0.0;
   double mFramesPerBucket = 1.0;
};
