#pragma once

#include <atomic>

#include "DspMath.h"

// Lock-free main-thread -> audio-thread param path, one atomic slot per
// param id ("latest value wins"), with per-block one-pole smoothing applied
// on the consumer side.
//
// This is deliberately not a ring/FIFO: a knob-driven param only ever needs
// its most recent value, never an ordered, none-dropped history of every
// intermediate value (that's what a ring would buy, at the cost of a
// second index the audio thread would have to own alongside the producer -
// see docs/plans/optimization/research-implementation-map.md 1.2). An
// earlier version of this class used a head/tail ring where the producer's
// overrun-drop path also wrote the consumer-owned head index, which broke
// the single-consumer invariant under real concurrent load; a flat array of
// std::atomic<float> has no such shared index to race on.
class ParamMailbox
{
public:
   static constexpr int kMaxParams = 64;

   void PrepareToPlay(double sampleRate);

   // Main-thread-only observability hook (not used by SmoothedValue/Push -
   // those never touch mSampleRate, only the per-slot smoothers PrepareToPlay
   // already configured). Lets a test confirm which rate this mailbox was
   // actually prepared with, e.g. INFINITE_AUDIOLIFECYCLETEST checking that a
   // node's mailbox saw the device's negotiated rate rather than whatever it
   // was constructed/last-rebuilt with.
   double SampleRate() const { return mSampleRate; }

   // Main thread only. Last write before the audio thread next reads wins.
   void Push(int paramId, float value);

   // Audio thread only: advance smoothing by one sample and return the
   // current (smoothed) value for paramId.
   float SmoothedValue(int paramId);

   // Audio thread only: initialize both current and target without ramping
   // (e.g. at startup, before the first block).
   void SetImmediate(int paramId, float value);

private:
   std::atomic<float> mTarget[kMaxParams] {};
   DspMath::OnePole mSmoothers[kMaxParams];
   double mSampleRate = 0.0;
};
