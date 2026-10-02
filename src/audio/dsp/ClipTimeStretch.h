#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "platform/Platform.h"

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
   }

   float Out(int channel, int i) const { return mOut[channel & 1][(size_t)i]; }

private:
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
};
