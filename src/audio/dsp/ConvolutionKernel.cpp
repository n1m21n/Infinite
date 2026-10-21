#include "ConvolutionKernel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/AudioBuffer.h"
#include "audio/DspMath.h"
#include "core/AudioDecodeCache.h"
#include "nodes/AudioEffectNode.h"

namespace
{
   constexpr float kSpeedOfSound = 343.0f;
   constexpr int kBigTier = ConvolutionKernel::kTiers - 1;
   // The big tier's work for one block is spread over this many small-tier
   // blocks (64 of them fit in one big block; the rest is margin).
   constexpr int kBigTicks = 56;

   // Longest side : the other two, per shape.
   const float kShapeRatio[ConvolutionKernel::kNumShapes][2] = {
      { 0.72f, 0.45f }, // room
      { 0.55f, 0.38f }, // hall
      { 0.90f, 0.80f }, // chamber
      { 0.16f, 0.13f }, // tunnel
   };

   struct Rng
   {
      uint32_t s;
      float Uniform() // -1..1
      {
         s ^= s << 13;
         s ^= s >> 17;
         s ^= s << 5;
         return (float)(int32_t)s * (1.0f / 2147483648.0f);
      }
      float Gauss() { return Uniform() + Uniform() + Uniform(); } // variance 1
   };

   void FadeTail(std::vector<float>& x, double sampleRate)
   {
      const int n = (int)x.size();
      const int fade = std::min(n, std::max(16, std::min((int)(0.05 * sampleRate), n / 20)));
      for (int i = 0; i < fade; i++)
         x[n - 1 - i] *= 0.5f - 0.5f * cosf((float)M_PI * (float)i / (float)fade);
   }
}

const char* ConvolutionKernel::ShapeName(int shape)
{
   static const char* kNames[kNumShapes] = { "room", "hall", "chamber", "tunnel" };
   return kNames[std::clamp(shape, 0, kNumShapes - 1)];
}

int ConvolutionKernel::MaxIrSamples(double sampleRate)
{
   return std::min(kMaxSamples, (int)(kMaxSeconds * sampleRate));
}

// ------------------------------------------------------------------ design

void ConvolutionKernel::DesignImpulse(const Design& d, double sampleRate, std::vector<float>& left, std::vector<float>& right)
{
   const float sr = (float)sampleRate;
   const float size = std::clamp(d.size, 0.0f, 1.0f);
   const int shape = std::clamp(d.shape, 0, kNumShapes - 1);
   const float material = std::clamp(d.material, 0.0f, 1.0f);
   const float distance = std::clamp(d.distance, 0.0f, 1.0f);
   const float rt = std::clamp(d.decay, 0.1f, kMaxSeconds);
   const float spread = std::clamp(d.spread, 0.0f, 1.0f);

   // The room.
   const float L[3] = { 3.0f * powf(20.0f, size), 0.0f, 0.0f };
   const float dim[3] = { L[0], L[0] * kShapeRatio[shape][0], L[0] * kShapeRatio[shape][1] };
   const float volume = dim[0] * dim[1] * dim[2];
   const float surface = 2.0f * (dim[0] * dim[1] + dim[1] * dim[2] + dim[0] * dim[2]);
   // Eyring: RT60 = 0.161 V / (-S ln(1 - a)). Solved for the absorption that
   // gives the asked decay, so the image sources die at the same rate as the tail.
   const float absorption = std::clamp(1.0f - expf(-0.161f * volume / (surface * rt)), 0.0005f, 0.99f);
   const float beta = sqrtf(1.0f - absorption);

   // Listener off every axis of symmetry, looking down -x at the source.
   const float listener[3] = { 0.62f * dim[0], 0.41f * dim[1], 0.37f * dim[2] };
   const float gap = std::max(0.3f, (0.03f + 0.52f * distance) * dim[0]);
   const float source[3] = { std::max(0.05f * dim[0], listener[0] - gap), 0.47f * dim[1], 0.43f * dim[2] };
   float direct = 0.0f;
   for (int a = 0; a < 3; a++)
      direct += (source[a] - listener[a]) * (source[a] - listener[a]);
   direct = std::max(0.3f, sqrtf(direct));
   const float directTime = direct / kSpeedOfSound;

   const float deltaMid = 6.908f / rt; // amplitude decay, nepers per second
   const int length = std::clamp((int)((rt * 1.15f + 0.05f) * sr), kHead * 2, MaxIrSamples(sampleRate));
   left.assign(length, 0.0f);
   right.assign(length, 0.0f);

   // Mixing time: when reflections have become too dense to hear apart (about
   // sqrt(V) ms, Polack); image sources until then, noise after.
   const float mixTime = std::clamp(0.002f * sqrtf(volume), 0.025f, 0.16f);

   auto place = [&](float time, float gainL, float gainR)
   {
      const float pos = time * sr;
      const int i = (int)pos;
      const float frac = pos - (float)i;
      if (i < 0 || i + 1 >= length)
         return;
      left[i] += gainL * (1.0f - frac);
      left[i + 1] += gainL * frac;
      right[i] += gainR * (1.0f - frac);
      right[i + 1] += gainR * frac;
   };

   // Direct sound: time zero, centre.
   place(0.0f, 1.0f / direct, 1.0f / direct);

   // Early reflections. Along each axis image i sits at i*L + (xs or L - xs)
   // and has bounced |i| times.
   const int kRange = 12;
   for (int ix = -kRange; ix <= kRange; ix++)
      for (int iy = -kRange; iy <= kRange; iy++)
         for (int iz = -kRange; iz <= kRange; iz++)
         {
            const int order = std::abs(ix) + std::abs(iy) + std::abs(iz);
            if (order == 0)
               continue;
            const int idx[3] = { ix, iy, iz };
            float delta[3], dist2 = 0.0f;
            for (int a = 0; a < 3; a++)
            {
               const float img = (float)idx[a] * dim[a] + ((idx[a] & 1) ? dim[a] - source[a] : source[a]);
               delta[a] = img - listener[a];
               dist2 += delta[a] * delta[a];
            }
            const float dist = sqrtf(dist2);
            const float t = dist / kSpeedOfSound - directTime;
            if (t <= 0.0f || t >= mixTime)
               continue;
            // Fade the discrete reflections out over the second half of the mixing time.
            const float u = std::clamp((t - 0.5f * mixTime) / (0.5f * mixTime), 0.0f, 1.0f);
            const float gain = powf(beta, (float)order) / dist * cosf(u * (float)M_PI * 0.5f);
            const float pan = spread * delta[1] / dist; // lateral part of the arrival direction
            place(t, gain * sqrtf(1.0f - pan), gain * sqrtf(1.0f + pan));
         }

   // Late field. Reflections arrive at 4 pi c^3 t^2 / V per second, each at
   // 1/(c t): energy per second 4 pi c / V, times the decay.
   {
      Rng rngL { 0x9E3779B9u }, rngR { 0x7F4A7C15u };
      const float sigma = sqrtf(4.0f * (float)M_PI * kSpeedOfSound / volume / sr);
      // spread 0: both channels carry the same noise. spread 1: independent.
      const float same = cosf(spread * (float)M_PI * 0.5f), other = sinf(spread * (float)M_PI * 0.5f);
      const float step = expf(-deltaMid / sr);
      float env = sigma * expf(-deltaMid * directTime);
      const int fadeStart = (int)(0.5f * mixTime * sr), fadeEnd = (int)(mixTime * sr);
      for (int i = 0; i < length; i++)
      {
         const float a = rngL.Gauss(), b = rngR.Gauss();
         if (i >= fadeStart)
         {
            const float u = i >= fadeEnd ? 1.0f : (float)(i - fadeStart) / (float)std::max(1, fadeEnd - fadeStart);
            const float g = env * sinf(u * (float)M_PI * 0.5f);
            left[i] += g * a;
            right[i] += g * (same * a + other * b);
         }
         env *= step;
      }
   }

   // Decay per band: the material's absorption curve and the air. Bands come
   // from complementary splits (high = input - lowpass), which add back to the
   // input exactly, so the direct sound stays one clean impulse.
   {
      static const float kEdges[6] = { 8000.0f, 4000.0f, 2000.0f, 1000.0f, 500.0f, 250.0f };
      static const float kCentre[7] = { 12000.0f, 5600.0f, 2800.0f, 1400.0f, 700.0f, 350.0f, 125.0f };
      // Decay time of each band relative to the mid band.
      static const float kSoft[7] = { 0.30f, 0.42f, 0.60f, 0.80f, 1.0f, 1.05f, 1.15f };
      static const float kHard[7] = { 0.80f, 0.88f, 0.93f, 0.97f, 1.0f, 1.05f, 1.10f };
      float ratio[7];
      for (int b = 0; b < 7; b++)
      {
         const float rel = kSoft[b] + (kHard[b] - kSoft[b]) * material;
         // Air: energy attenuation about 5.5e-4 (f/1 kHz)^1.7 per metre at 50% humidity (Kuttruff).
         const float air = 0.5f * kSpeedOfSound * 5.5e-4f * powf(kCentre[b] / 1000.0f, 1.7f);
         ratio[b] = expf(-(6.908f / (rt * rel) + air - deltaMid) / sr);
      }
      for (std::vector<float>* chan : { &left, &right })
      {
         DspMath::Biquad lp[6];
         bool live[6];
         for (int e = 0; e < 6; e++)
         {
            live[e] = kEdges[e] < 0.45f * sr;
            if (live[e])
               lp[e].SetLowpass(kEdges[e], 0.7071, sampleRate);
         }
         float gain[7] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
         float* x = chan->data();
         for (int i = 0; i < length; i++)
         {
            float rest = x[i], sum = 0.0f;
            for (int e = 0; e < 6; e++)
            {
               if (!live[e])
                  continue;
               const float low = lp[e].Process(rest);
               sum += (rest - low) * gain[e];
               rest = low;
            }
            x[i] = sum + rest * gain[6];
            for (int b = 0; b < 7; b++)
               gain[b] *= ratio[b];
         }
      }
   }

   // Image sources are all positive pulses, so their sum climbs toward DC,
   // where no source or microphone reaches: 30 Hz high-pass, 12 dB/octave.
   for (std::vector<float>* chan : { &left, &right })
   {
      DspMath::Biquad hp;
      hp.SetHighpass(30.0, 0.7071, sampleRate);
      for (float& v : *chan)
         v = hp.Process(v);
   }

   FadeTail(left, sampleRate);
   FadeTail(right, sampleRate);
}

void ConvolutionKernel::ImpulseFromFile(const float* fileL, const float* fileR, int frames, double fileRate,
                                        double sampleRate, std::vector<float>& left, std::vector<float>& right)
{
   left.clear();
   right.clear();
   if (frames <= 0 || fileRate <= 0.0 || fileL == nullptr)
      return;
   if (fileR == nullptr)
      fileR = fileL;
   const double ratio = sampleRate / fileRate;
   const int outLen = std::min(MaxIrSamples(sampleRate), (int)std::ceil((double)frames * ratio));
   left.resize(outLen);
   right.resize(outLen);
   if (std::fabs(ratio - 1.0) < 1e-9)
   {
      std::copy(fileL, fileL + outLen, left.begin());
      std::copy(fileR, fileR + outLen, right.begin());
   }
   else
   {
      // Windowed-sinc resampling (Hann, 16 zero crossings a side), cutoff at
      // the lower of the two Nyquists.
      const double cutoff = std::min(1.0, ratio);
      const int half = (int)std::ceil(16.0 / cutoff);
      for (int n = 0; n < outLen; n++)
      {
         const double pos = (double)n / ratio;
         const int centre = (int)std::floor(pos);
         double accL = 0.0, accR = 0.0;
         for (int k = centre - half + 1; k <= centre + half; k++)
         {
            if (k < 0 || k >= frames)
               continue;
            const double t = (pos - (double)k) * cutoff;
            const double w = 0.5 + 0.5 * std::cos(M_PI * (pos - (double)k) / (double)half);
            const double s = std::fabs(t) < 1e-9 ? 1.0 : std::sin(M_PI * t) / (M_PI * t);
            accL += (double)fileL[k] * s * w * cutoff;
            accR += (double)fileR[k] * s * w * cutoff;
         }
         left[n] = (float)accL;
         right[n] = (float)accR;
      }
   }
   if (outLen < (int)std::ceil((double)frames * ratio)) // cut short by the cap
   {
      FadeTail(left, sampleRate);
      FadeTail(right, sampleRate);
   }
}

ConvolutionKernel::IrSet* ConvolutionKernel::BuildSet(std::vector<float>& left, std::vector<float>& right, double sampleRate,
                                                      bool normalise)
{
   IrSet* set = new IrSet();
   const int length = std::min({ (int)left.size(), (int)right.size(), MaxIrSamples(sampleRate) });
   set->length = length;
   set->seconds = (float)((double)length / sampleRate);
   const float* h[kChannels] = { left.data(), right.data() };

   float scale = 1.0f;
   if (normalise)
   {
      // Equal level for pink noise, which is how music is spread over
      // frequency: a room rings longer in the lows, so matching white noise
      // instead leaves a mix several dB louder with the reverb in.
      // Weighting: first-order sections one octave apart from 20 Hz, each
      // +3 dB below its corner, so -3 dB per octave across the band.
      static constexpr int kSections = 11;
      double coef[kSections];
      int sections = 0;
      for (double corner = 20.0; sections < kSections && corner < 0.45 * sampleRate; corner *= 2.0)
         coef[sections++] = 1.0 - std::exp(-6.283185307179586 * corner / sampleRate);
      const int run = length + (int)(sampleRate * 0.25);
      auto weightedEnergy = [&](const float* x, int count) {
         double lp[kSections] = {};
         double e = 0.0;
         for (int i = 0; i < run; i++)
         {
            double v = x != nullptr ? (i < count ? (double)x[i] : 0.0) : (i == 0 ? 1.0 : 0.0);
            for (int k = 0; k < sections; k++)
            {
               lp[k] += coef[k] * (v - lp[k]);
               v += 0.41421356 * lp[k];
            }
            e += v * v;
         }
         return e;
      };
      const double reference = weightedEnergy(nullptr, 0);
      double energy = 0.0;
      for (int ch = 0; ch < kChannels; ch++)
         energy = std::max(energy, weightedEnergy(h[ch], length));
      if (energy > 1e-20)
         scale = (float)std::sqrt(reference / energy);
   }

   // Head taps, stored newest-last to match the history buffer.
   for (int ch = 0; ch < kChannels; ch++)
      for (int j = 0; j < kHead; j++)
         set->head[ch][kHead - 1 - j] = j < length ? h[ch][j] * scale : 0.0f;

   PortableFft::RealFft fft;
   fft.Prepare(kBlockLog2[kBigTier] + 1);
   std::vector<float> block;
   for (int t = 0; t < kTiers; t++)
   {
      const int B = 1 << kBlockLog2[t];
      const int start = kTierStart[t];
      const int end = std::min(length, t + 1 < kTiers ? kTierStart[t + 1] : length);
      const int parts = end > start ? (end - start + B - 1) / B : 0;
      set->parts[t] = parts;
      // Forward gives 2X and the inverse is unnormalised (N x): fold the
      // 1/(4N) into the stored spectrum so the audio thread multiplies only.
      const float norm = scale / (4.0f * (float)(2 * B));
      block.assign((size_t)2 * B, 0.0f);
      for (int ch = 0; ch < kChannels; ch++)
      {
         set->spec[t][ch].assign((size_t)parts * 2 * B, 0.0f);
         for (int p = 0; p < parts; p++)
         {
            const int from = start + p * B;
            const int count = std::min(B, end - from);
            std::fill(block.begin(), block.end(), 0.0f);
            for (int i = 0; i < count; i++)
               block[i] = h[ch][from + i] * norm;
            float* dst = set->spec[t][ch].data() + (size_t)p * 2 * B;
            fft.Forward(block.data(), kBlockLog2[t] + 1, dst, dst + B);
         }
      }
   }

   for (int p = 0; p < kEnvelopePoints; p++)
   {
      const int from = (int)((long long)p * length / kEnvelopePoints);
      const int to = std::max(from + 1, (int)((long long)(p + 1) * length / kEnvelopePoints));
      float peak = 0.0f;
      for (int ch = 0; ch < kChannels; ch++)
         for (int i = from; i < to && i < length; i++)
            peak = std::max(peak, std::fabs(h[ch][i] * scale));
      set->envelopeDb[p] = peak > 1.6e-5f ? 20.0f * log10f(peak) : -96.0f;
   }
   return set;
}

// --------------------------------------------------------------- lifecycle

ConvolutionKernel::ConvolutionKernel() {}

ConvolutionKernel::~ConvolutionKernel()
{
   if (mWorker.joinable())
      mWorker.join();
   delete mJobResult;
   delete mPending.exchange(nullptr);
   delete mRetired.exchange(nullptr);
   if (mNext != mCur)
      delete mNext;
   delete mCur;
}

void ConvolutionKernel::PrepareToPlay(double sampleRate, int)
{
   mSampleRate = sampleRate;
   mFft.Prepare(kBlockLog2[kBigTier] + 1);
   const int maxB = 1 << kBlockLog2[kBigTier];
   mAccRe.assign(maxB, 0.0f);
   mAccIm.assign(maxB, 0.0f);
   mTime.assign((size_t)2 * maxB, 0.0f);
   mTimeB.assign((size_t)2 * maxB, 0.0f);
   const int maxLen = MaxIrSamples(sampleRate);
   for (int t = 0; t < kTiers; t++)
   {
      Tier& tier = mTier[t];
      tier.log2B = kBlockLog2[t];
      tier.B = 1 << tier.log2B;
      const int end = t + 1 < kTiers ? kTierStart[t + 1] : maxLen;
      tier.maxParts = std::max(1, (end - kTierStart[t] + tier.B - 1) / tier.B);
      for (int ch = 0; ch < kChannels; ch++)
      {
         tier.window[ch].assign((size_t)2 * tier.B, 0.0f);
         tier.fdl[ch].assign((size_t)tier.maxParts * 2 * tier.B, 0.0f);
         tier.out[ch].assign(tier.B, 0.0f);
         tier.next[ch].assign(t == kBigTier ? tier.B : 0, 0.0f);
      }
   }
   for (int ch = 0; ch < kChannels; ch++)
   {
      mBigIn[ch].assign((size_t)2 * maxB, 0.0f);
      for (int s = 0; s < 2; s++)
      {
         mBigAccRe[s][ch].assign(maxB, 0.0f);
         mBigAccIm[s][ch].assign(maxB, 0.0f);
      }
   }
   Reset();
}

void ConvolutionKernel::Reset()
{
   for (Tier& tier : mTier)
   {
      tier.pos = 0;
      tier.slot = 0;
      for (int ch = 0; ch < kChannels; ch++)
      {
         std::fill(tier.window[ch].begin(), tier.window[ch].end(), 0.0f);
         std::fill(tier.fdl[ch].begin(), tier.fdl[ch].end(), 0.0f);
         std::fill(tier.out[ch].begin(), tier.out[ch].end(), 0.0f);
         std::fill(tier.next[ch].begin(), tier.next[ch].end(), 0.0f);
      }
   }
   std::memset(mHist, 0, sizeof(mHist));
   mHistPos = 0;
   mBig = BigJob();
}

// ------------------------------------------------------------- main thread

void ConvolutionKernel::Hand(IrSet* set)
{
   delete mPending.exchange(set, std::memory_order_acq_rel); // a set the audio thread never took
}

void ConvolutionKernel::InstallForTest(IrSet* set)
{
   Hand(set);
   mHaveIr = true;
   mHaveRequest = true; // PushParams is not driving this kernel
}

void ConvolutionKernel::StartJob(Job&& job)
{
   if (mWorker.joinable())
      mWorker.join();
   mJobDone.store(false, std::memory_order_relaxed);
   mJobRunning = true;
   mJobUsesFile = job.fromFile;
   mLastJobStart = std::chrono::steady_clock::now();
   mWorker = std::thread(
      [this, job = std::move(job)]() mutable
      {
         std::vector<float> l, r;
         if (job.fromFile)
            ImpulseFromFile(job.fileL.data(), job.fileR.empty() ? nullptr : job.fileR.data(), (int)job.fileL.size(),
                            job.fileRate, job.sampleRate, l, r);
         else
            DesignImpulse(job.design, job.sampleRate, l, r);
         mJobResult = BuildSet(l, r, job.sampleRate);
         mJobDone.store(true, std::memory_order_release);
      });
}

void ConvolutionKernel::CollectJob()
{
   if (!mJobRunning || !mJobDone.load(std::memory_order_acquire))
      return;
   mWorker.join();
   mJobRunning = false;
   IrSet* set = mJobResult;
   mJobResult = nullptr;
   std::memcpy(mEnvelope, set->envelopeDb, sizeof(mEnvelope));
   mEnvelopeSeconds = set->seconds;
   mUsingFile = mJobUsesFile;
   mHaveIr = true;
   Hand(set);
}

void ConvolutionKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   CollectJob();
   delete mRetired.exchange(nullptr, std::memory_order_acq_rel);

   Design d;
   d.size = node.Param("size");
   d.shape = (int)(node.Param("shape") + 0.5f);
   d.material = node.Param("material");
   d.distance = node.Param("distance");
   d.decay = node.Param("decay");
   d.spread = node.Param("spread");
   const std::string& file = node.filePath;

   // With a file loaded the design controls change nothing.
   const bool fileActive = !file.empty() && mFileError.empty();
   const bool stale = !mHaveRequest || file != mRequestedFile || sampleRate != mRequestedRate ||
                      (!(fileActive && mHaveRequest) && !(d == mRequested));
   if (!stale || mJobRunning)
      return;
   // A dragged knob rebuilds a few times a second, not once a frame.
   if (mHaveRequest && std::chrono::steady_clock::now() - mLastJobStart < std::chrono::milliseconds(60))
      return;

   Job job;
   job.design = d;
   job.sampleRate = sampleRate;
   if (file != mRequestedFile || sampleRate != mRequestedRate || !mHaveRequest)
      mFileError.clear();
   if (!file.empty() && mFileError.empty())
   {
      Platform::SampleBuffer buf;
      std::string err;
      if (AudioDecodeCache::DecodeCached(file, buf, err) && buf.numFrames > 0 && buf.channels > 0)
      {
         job.fromFile = true;
         job.fileRate = buf.sampleRate;
         job.fileL.assign(buf.channelData.begin(), buf.channelData.begin() + buf.numFrames);
         if (buf.channels > 1)
            job.fileR.assign(buf.channelData.begin() + buf.numFrames, buf.channelData.begin() + 2 * (size_t)buf.numFrames);
      }
      else
         mFileError = err.empty() ? std::string("could not read the file") : err;
   }
   mRequested = d;
   mRequestedFile = file;
   mRequestedRate = sampleRate;
   mHaveRequest = true;
   StartJob(std::move(job));
}

int ConvolutionKernel::Envelope(float* outDb, int maxCount, float* seconds) const
{
   if (!mHaveIr || mEnvelopeSeconds <= 0.0f)
      return 0;
   const int n = std::min(maxCount, kEnvelopePoints);
   std::memcpy(outDb, mEnvelope, (size_t)n * sizeof(float));
   if (seconds)
      *seconds = mEnvelopeSeconds;
   return n;
}

// ------------------------------------------------------------ audio thread

// Adds partitions [firstPart, lastPart) of `set` times the matching input
// spectra to the accumulator. `slot` holds the newest input block.
static void AccumulateParts(const float* fdl, const float* spec, int B, int maxParts, int slot, int firstPart, int lastPart,
                            float* accRe, float* accIm)
{
   for (int k = firstPart; k < lastPart; k++)
   {
      int s = slot - k;
      if (s < 0)
         s += maxParts;
      const float* xr = fdl + (size_t)s * 2 * B;
      const float* xi = xr + B;
      const float* hr = spec + (size_t)k * 2 * B;
      const float* hi = hr + B;
      // Bin 0 packs two real values: DC in the real slot, Nyquist in the imaginary one.
      const float dc = xr[0] * hr[0], nyquist = xi[0] * hi[0];
      const float keepRe = accRe[0], keepIm = accIm[0];
      for (int j = 0; j < B; j++)
      {
         accRe[j] += xr[j] * hr[j] - xi[j] * hi[j];
         accIm[j] += xr[j] * hi[j] + xi[j] * hr[j];
      }
      accRe[0] = keepRe + dc;
      accIm[0] = keepIm + nyquist;
   }
}

void ConvolutionKernel::Accumulate(int t, int ch, const IrSet* set, int firstPart, int lastPart)
{
   const Tier& tier = mTier[t];
   AccumulateParts(tier.fdl[ch].data(), set->spec[t][ch].data(), tier.B, tier.maxParts, tier.slot, firstPart, lastPart,
                   mAccRe.data(), mAccIm.data());
}

// One block of tier t, channel ch, against `set`: 2B samples into outTime, of
// which the last B are valid (overlap-save).
void ConvolutionKernel::RunTier(int t, int ch, const IrSet* set, float* outTime)
{
   const Tier& tier = mTier[t];
   const int parts = std::min(set->parts[t], tier.maxParts);
   if (parts <= 0)
   {
      std::memset(outTime, 0, (size_t)2 * tier.B * sizeof(float));
      return;
   }
   std::memset(mAccRe.data(), 0, (size_t)tier.B * sizeof(float));
   std::memset(mAccIm.data(), 0, (size_t)tier.B * sizeof(float));
   Accumulate(t, ch, set, 0, parts);
   mFft.Inverse(mAccRe.data(), mAccIm.data(), tier.log2B + 1, outTime);
}

void ConvolutionKernel::BigTierTick()
{
   if (!mBig.active)
      return;
   Tier& tier = mTier[kBigTier];
   const int B = tier.B;
   const int partsA = std::min(mBig.a->parts[kBigTier], tier.maxParts);
   const int partsB = mBig.b ? std::min(mBig.b->parts[kBigTier], tier.maxParts) : 0;
   for (int n = 0; n < mBig.perTick && mBig.step < mBig.steps; n++, mBig.step++)
   {
      int s = mBig.step;
      if (s < kChannels) // the input block's spectrum
      {
         float* dst = tier.fdl[s].data() + (size_t)mBig.slot * 2 * B;
         mFft.Forward(mBigIn[s].data(), tier.log2B + 1, dst, dst + B);
         continue;
      }
      s -= kChannels;
      if (s < kChannels * partsA)
      {
         const int ch = s % kChannels, k = s / kChannels;
         AccumulateParts(tier.fdl[ch].data(), mBig.a->spec[kBigTier][ch].data(), B, tier.maxParts, mBig.slot, k, k + 1,
                         mBigAccRe[0][ch].data(), mBigAccIm[0][ch].data());
         continue;
      }
      s -= kChannels * partsA;
      if (s < kChannels * partsB)
      {
         const int ch = s % kChannels, k = s / kChannels;
         AccumulateParts(tier.fdl[ch].data(), mBig.b->spec[kBigTier][ch].data(), B, tier.maxParts, mBig.slot, k, k + 1,
                         mBigAccRe[1][ch].data(), mBigAccIm[1][ch].data());
         continue;
      }
      const int ch = s - kChannels * partsB;
      mFft.Inverse(mBigAccRe[0][ch].data(), mBigAccIm[0][ch].data(), tier.log2B + 1, mTime.data());
      float* next = tier.next[ch].data();
      if (mBig.b)
      {
         mFft.Inverse(mBigAccRe[1][ch].data(), mBigAccIm[1][ch].data(), tier.log2B + 1, mTimeB.data());
         for (int j = 0; j < B; j++)
         {
            const float w = ((float)j + 0.5f) / (float)B;
            next[j] = mTime[B + j] * (1.0f - w) + mTimeB[B + j] * w;
         }
      }
      else
         std::memcpy(next, mTime.data() + B, (size_t)B * sizeof(float));
   }
   if (mBig.step >= mBig.steps)
      mBig.active = false;
}

void ConvolutionKernel::BigTierFinish()
{
   while (mBig.active)
      BigTierTick();
}

void ConvolutionKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer*, AudioBuffer& out)
{
   const int frames = out.numFrames;
   const int outChannels = std::min(out.numChannels, kChannels);

   // Take a new impulse response once the last swap is fully over.
   if (mNext == nullptr && mRetired.load(std::memory_order_acquire) == nullptr)
      if (IrSet* fresh = mPending.exchange(nullptr, std::memory_order_acq_rel))
      {
         if (mCur == nullptr)
         {
            mCur = fresh;
            for (Tier& tier : mTier)
               tier.set = fresh;
         }
         else
         {
            mNext = fresh;
            mHeadFade = kHead;
         }
      }

   if (mCur == nullptr || in.numChannels <= 0)
   {
      for (int ch = 0; ch < out.numChannels; ch++)
         std::memset(out.channels[ch], 0, (size_t)frames * sizeof(float));
      if (mCur == nullptr)
         return;
   }

   const float* src[kChannels];
   for (int ch = 0; ch < kChannels; ch++)
      src[ch] = in.numChannels > 0 ? in.channels[std::min(ch, in.numChannels - 1)] : nullptr;

   for (int i = 0; i < frames; i++)
   {
      const float fade = mNext ? 1.0f - (float)mHeadFade / (float)kHead : 0.0f;
      for (int ch = 0; ch < kChannels; ch++)
      {
         const float x = src[ch] ? src[ch][i] : 0.0f;
         mHist[ch][mHistPos] = x;
         mHist[ch][mHistPos + kHead] = x;
         const float* hist = &mHist[ch][mHistPos + 1];
         const float* taps = mCur->head[ch];
         float y = 0.0f;
         for (int j = 0; j < kHead; j++)
            y += hist[j] * taps[j];
         if (mNext)
         {
            const float* tapsNext = mNext->head[ch];
            float yn = 0.0f;
            for (int j = 0; j < kHead; j++)
               yn += hist[j] * tapsNext[j];
            y += (yn - y) * fade;
         }
         for (Tier& tier : mTier)
         {
            y += tier.out[ch][tier.pos];
            tier.window[ch][tier.B + tier.pos] = x;
         }
         if (ch < outChannels)
            out.channels[ch][i] = y;
      }
      mHistPos = (mHistPos + 1) % kHead;
      if (mHeadFade > 0)
         mHeadFade--;

      bool smallBlockDone = false;
      for (int t = 0; t < kTiers; t++)
      {
         Tier& tier = mTier[t];
         if (++tier.pos < tier.B)
            continue;
         tier.pos = 0;
         const int B = tier.B;
         if (t == kBigTier)
         {
            BigTierFinish(); // normally long done
            for (int ch = 0; ch < kChannels; ch++)
            {
               tier.out[ch].swap(tier.next[ch]);
               std::memcpy(mBigIn[ch].data(), tier.window[ch].data(), (size_t)2 * B * sizeof(float));
               std::memmove(tier.window[ch].data(), tier.window[ch].data() + B, (size_t)B * sizeof(float));
               for (int s = 0; s < 2; s++)
               {
                  std::memset(mBigAccRe[s][ch].data(), 0, (size_t)B * sizeof(float));
                  std::memset(mBigAccIm[s][ch].data(), 0, (size_t)B * sizeof(float));
               }
            }
            mBig.a = tier.set;
            mBig.b = (mNext && tier.set != mNext) ? mNext : nullptr;
            if (mBig.b)
               tier.set = mNext;
            mBig.slot = tier.slot;
            tier.slot = (tier.slot + 1) % tier.maxParts;
            const int partsA = std::min(mBig.a->parts[t], tier.maxParts);
            const int partsB = mBig.b ? std::min(mBig.b->parts[t], tier.maxParts) : 0;
            mBig.steps = kChannels * (2 + partsA + partsB);
            mBig.perTick = (mBig.steps + kBigTicks - 1) / kBigTicks;
            mBig.step = 0;
            mBig.active = true;
            continue;
         }
         if (t == 0)
            smallBlockDone = true;
         const bool swap = mNext && tier.set != mNext;
         for (int ch = 0; ch < kChannels; ch++)
         {
            float* dst = tier.fdl[ch].data() + (size_t)tier.slot * 2 * B;
            mFft.Forward(tier.window[ch].data(), tier.log2B + 1, dst, dst + B);
            RunTier(t, ch, tier.set, mTime.data());
            float* o = tier.out[ch].data();
            if (swap)
            {
               RunTier(t, ch, mNext, mTimeB.data());
               for (int j = 0; j < B; j++)
               {
                  const float w = ((float)j + 0.5f) / (float)B;
                  o[j] = mTime[B + j] * (1.0f - w) + mTimeB[B + j] * w;
               }
            }
            else
               std::memcpy(o, mTime.data() + B, (size_t)B * sizeof(float));
            std::memmove(tier.window[ch].data(), tier.window[ch].data() + B, (size_t)B * sizeof(float));
         }
         if (swap)
            tier.set = mNext;
         tier.slot = (tier.slot + 1) % tier.maxParts;
      }
      if (smallBlockDone)
         BigTierTick();

      // The swap is over once nothing reads the old set any more.
      if (mNext && mHeadFade == 0 && !(mBig.active && mBig.b != nullptr))
      {
         bool all = true;
         for (const Tier& tier : mTier)
            all = all && tier.set == mNext;
         if (all)
         {
            mRetired.store(const_cast<IrSet*>(mCur), std::memory_order_release);
            mCur = mNext;
            mNext = nullptr;
         }
      }
   }
}
