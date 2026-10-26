#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "platform/Platform.h"
#include "audio/dsp/TransientDetect.h"

// Accelerate FFT on macOS; Windows/Linux use Signalsmith's portable C++ FFT.
// INFINITE_TEST_PORTABLE_FFT forces the portable path on macOS for testing.
#if defined(__APPLE__) && !defined(SIGNALSMITH_USE_ACCELERATE) && !defined(INFINITE_TEST_PORTABLE_FFT)
#define SIGNALSMITH_USE_ACCELERATE
#endif
#include "signalsmith-stretch/signalsmith-stretch.h"

// One channel of a planar Platform::SampleBuffer at a fractional frame
// position, 4-point cubic Hermite interpolated (the file's rate generally
// differs from the engine's, and linear interpolation audibly dulls the top
// octave when resampling). A channel the file does not have reads as its last
// real channel, so a mono file feeds both sides of a stereo output.
// Out-of-range positions read as silence.
inline float ReadBufferInterp(const Platform::SampleBuffer& buf, int channel, double pos)
{
   if (buf.channels <= 0 || buf.numFrames <= 0)
      return 0.0f;
   const int64_t i1 = (int64_t)std::floor(pos);
   if (i1 < -1 || i1 >= (int64_t)buf.numFrames)
      return 0.0f;
   const int ch = std::min(channel, buf.channels - 1);
   const float* data = buf.channelData.data() + (size_t)ch * (size_t)buf.numFrames;
   const int64_t n = buf.numFrames;
   auto at = [&](int64_t i) { return (i >= 0 && i < n) ? data[i] : 0.0f; };
   const float y0 = at(i1 - 1), y1 = at(i1), y2 = at(i1 + 1), y3 = at(i1 + 2);
   const float t = (float)(pos - (double)i1);
   const float c1 = 0.5f * (y2 - y0);
   const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
   const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
   return ((c3 * t + c2) * t + c1) * t + y1;
}

// Channel 0 only - kept for callers that analyse rather than play.
inline float ReadBufferInterp(const Platform::SampleBuffer& buf, double pos)
{
   return ReadBufferInterp(buf, 0, pos);
}

// Position-locked, time- and pitch-independent stretch of a decoded buffer for
// Arrangement Timeline Samples, on Signalsmith Stretch (MIT, spectral
// phase-vocoder family - the same class of algorithm as zplane elastique).
//
// Domains: the stretcher runs at the ENGINE rate. "Input frame" x is the
// source at engine-rate spacing, i.e. file frame x * fileFramesPerEngineFrame,
// resampled on the fly by ReadBufferInterp - no input copy, no scratch.
//
// Position contract: after Start(p, rate), the next output frame holds the
// source at input frame p. From then on, Render(p, rate, n) keeps the input fed
// through p + inputLatency + rate * outputLatency, where p is the input frame
// that belongs at the block's first output frame. That target is recomputed
// from the timeline every block rather than accumulated, so it cannot drift.
//
// Real-time safety: Prepare allocates (main thread, PrepareToPlay). Start and
// Render only touch capacity reserved by configure().
class ClipTimeStretch
{
public:
   void Prepare(double engineRate, int maxBlockFrames)
   {
      // Turbo 0.44.1: topology rebuilds re-run PrepareToPlay on nodes the
      // audio thread may still be processing; reallocating then would free
      // buffers under it. Same rate and enough room = nothing to do.
      if (mPrepared && engineRate == mEngineRate && (int)mOut[0].size() >= maxBlockFrames)
         return;
      // splitComputation spreads each spectral frame's work over the interval
      // instead of doing it all in one callback, at one interval of latency.
      mStretch.presetDefault(2, (float)engineRate, true);
      mOut[0].assign((size_t)maxBlockFrames, 0.0f);
      mOut[1].assign((size_t)maxBlockFrames, 0.0f);
      mEngineRate = engineRate;
      mPrepared = true;
   }

   bool Prepared() const { return mPrepared; }

   void SetPitch(float semitones)
   {
      if (semitones == mPitch)
         return;
      mPitch = semitones;
      // Tonality limit keeps timbre above ~8 kHz from being transposed along
      // with the partials, which is what makes big shifts sound "chipmunk".
      mStretch.setTransposeSemitones(semitones, semitones != 0.0f ? (float)(8000.0 / mEngineRate) : 0.0f);
   }

   // Hard re-anchor (transport jump, new window, first block): resets and
   // pre-rolls so the very next output frame is the source at `inputFrame`.
   void Start(const Platform::SampleBuffer& buf, double fileFramesPerInput, double inputFrame, double rate)
   {
      Reader reader{ &buf, fileFramesPerInput, 0.0 };
      const int64_t first = (int64_t)std::llround(inputFrame);
      reader.origin = (double)first;
      const int length = mStretch.outputSeekLength((float)rate);
      mStretch.outputSeek(reader, length);
      mFedEnd = first + length;
      mNextTransient = (size_t)(std::lower_bound(buf.transients.begin(), buf.transients.end(),
                                                 inputFrame * fileFramesPerInput) - buf.transients.begin());
      mAttackAge = -1;
      mOutPeak = 0.0f;
   }

   // Renders `frames` output frames. `inputFrame` is the input frame that
   // belongs at this block's first output frame, `rate` input frames per
   // output frame.
   void Render(const Platform::SampleBuffer& buf, double fileFramesPerInput, double inputFrame, double rate,
               int frames)
   {
      frames = std::min(frames, (int)mOut[0].size());
      const double endInput = inputFrame + rate * (double)frames;
      const int64_t target = (int64_t)std::llround(endInput + (double)mStretch.inputLatency() +
                                                   rate * (double)mStretch.outputLatency());
      const int64_t count = std::clamp<int64_t>(target - mFedEnd, 0, (int64_t)frames * 16);
      Reader reader{ &buf, fileFramesPerInput, (double)mFedEnd };
      float* outs[2] = { mOut[0].data(), mOut[1].data() };
      mStretch.process(reader, (int)count, outs, frames);
      mFedEnd += count;
      KeepTransients(buf, fileFramesPerInput, inputFrame, rate, frames);
   }

   float Out(int channel, int i) const { return mOut[channel & 1][(size_t)i]; }

private:
   // Transient pass (v0.6 1.6). A phase vocoder spreads a hit across its
   // 120 ms window: energy ahead of the hit (pre-echo, up to half a window of
   // source time early) and a softened attack, both of which read as extra
   // hits. Two fixes, both keyed to the source, which is a buffer and so
   // known ahead:
   //  - ceiling: the stretched output is held to kCeiling x the source's own
   //    peak envelope at the same source position, so nothing sounds where
   //    the source is quiet (pre-echo) while material that tracks its own
   //    envelope (tones, voices) is untouched;
   //  - attack: at each hit in buf.transients the attack is read straight
   //    from the source for kHoldSec (at the pitch ratio, so a shift still
   //    applies), then the stretched output fades back in over kFadeSec.
   // Per block this is a binary search and reads, nothing else.
   static constexpr double kHoldSec = 0.025, kFadeSec = 0.030;
   static constexpr float kCeiling = 1.41f; // +3 dB

   void KeepTransients(const Platform::SampleBuffer& buf, double fileFramesPerInput, double inputFrame,
                       double rate, int frames)
   {
      const std::vector<double>& hits = buf.transients;
      if (buf.envelope.empty() && hits.empty() && mAttackAge < 0)
         return;
      const double perOut = rate * fileFramesPerInput; // file frames per output frame
      const double attackStep = fileFramesPerInput * std::pow(2.0, (double)mPitch / 12.0);
      const double hold = kHoldSec * mEngineRate, fade = kFadeSec * mEngineRate;
      const float rel = (float)std::exp(-1.0 / (0.005 * mEngineRate));
      // A seek inside the block's span (or a rate change) can leave the index
      // behind; resync to the first hit at or after this block's start.
      const double first = inputFrame * fileFramesPerInput;
      if (mNextTransient > 0 && mNextTransient <= hits.size() && hits[mNextTransient - 1] > first + perOut)
         mNextTransient = 0;
      while (mNextTransient < hits.size() && hits[mNextTransient] < first - perOut)
         mNextTransient++;
      for (int i = 0; i < frames; i++)
      {
         const double q = first + perOut * (double)i;
         if (mNextTransient < hits.size() && q >= hits[mNextTransient])
         {
            mAttackPos = hits[mNextTransient];
            mAttackAge = 0;
            mNextTransient++;
         }
         // Ceiling: the larger of the two hops around q, so a hop boundary
         // never pinches a real peak; the output's own peak follows with a
         // 5 ms release so the gain moves at envelope speed, not per sample.
         float duck = 1.0f;
         if (buf.envelopeHop > 0 && q >= 0.0)
         {
            const size_t h = (size_t)(q / (double)buf.envelopeHop);
            const float src = h < buf.envelope.size()
                                 ? std::max(buf.envelope[h], h + 1 < buf.envelope.size() ? buf.envelope[h + 1] : 0.0f)
                                 : 0.0f;
            const float pk = std::max(std::fabs(mOut[0][(size_t)i]), std::fabs(mOut[1][(size_t)i]));
            mOutPeak = std::max(pk, mOutPeak * rel);
            if (mOutPeak > kCeiling * src)
               duck = kCeiling * src / mOutPeak;
         }
         float direct = 0.0f; // share of the unstretched attack
         if (mAttackAge >= 0)
         {
            const double age = (double)mAttackAge;
            direct = age < hold ? 1.0f : (float)(0.5 + 0.5 * std::cos(M_PI * std::min(1.0, (age - hold) / fade)));
            if (age >= hold + fade)
            {
               direct = 0.0f;
               mAttackAge = -1;
            }
         }
         for (int c = 0; c < 2; c++)
         {
            const float a = direct > 0.0f ? ReadBufferInterp(buf, c, mAttackPos) : 0.0f;
            mOut[c][(size_t)i] = mOut[c][(size_t)i] * duck * (1.0f - direct) + a * direct;
         }
         if (mAttackAge >= 0)
         {
            mAttackPos += attackStep;
            mAttackAge++;
         }
      }
   }

   // inputs[c][i] for Signalsmith: source channel c at input frame origin + i.
   struct Reader
   {
      const Platform::SampleBuffer* buf;
      double fileFramesPerInput;
      double origin;
      struct Channel
      {
         const Reader* r;
         int c;
         float operator[](int i) const
         {
            return ReadBufferInterp(*r->buf, c, (r->origin + (double)i) * r->fileFramesPerInput);
         }
      };
      Channel operator[](int c) const { return Channel{ this, c }; }
   };

   signalsmith::stretch::SignalsmithStretch<float> mStretch{ 0x5eed };
   std::vector<float> mOut[2];
   double mEngineRate = 48000.0;
   float mPitch = 0.0f;
   int64_t mFedEnd = 0;
   bool mPrepared = false;
   size_t mNextTransient = 0;  // first hit not yet reached
   double mAttackPos = 0.0;    // file frame of the unstretched attack being played
   int mAttackAge = -1;        // output frames into it, -1 when none
   float mOutPeak = 0.0f;      // stretched output's peak follower, for the ceiling
};
