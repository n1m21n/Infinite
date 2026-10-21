#pragma once

#include <atomic>

#include "DspMath.h"

// Lock-free main-thread -> audio-thread param path, one atomic slot per
// param id ("latest value wins"), interpolated per sample on the consumer
// side.
//
// Interpolation (docs/plans/modulation/README.md, Block 2): params are
// written once per UI frame, so the audio thread sees a staircase. Each
// frame publishes when its values were taken (PublishFrame: engine-time
// stamp) and how long the next one will take to arrive (delay). A slot
// that sees a new value ramps linearly to it, arriving at stamp + delay -
// a fixed delay, so the ramps join into a straight-line interpolation of
// the frame values regardless of when in the block they landed. A value
// that arrives while the previous ramp is still running waits for it. Before
// any frame has been published (headless DSP tests) a change ramps over 5 ms.
// A slot's first value is taken as is: ramping up to it from 0 made every
// kernel start on a 5 ms sweep from zero, read at a different point per
// device rate by anything sampled at the start (a strike, a hold clock).
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
   static constexpr int kMaxParams = 128;

   void PrepareToPlay(double sampleRate);

   // Main-thread-only observability hook. Lets a test confirm which rate this mailbox was
   // actually prepared with, e.g. INFINITE_AUDIOLIFECYCLETEST checking that a
   // node's mailbox saw the device's negotiated rate rather than whatever it
   // was constructed/last-rebuilt with.
   double SampleRate() const { return mSampleRate; }

   // Main thread only. Last write before the audio thread next reads wins.
   void Push(int paramId, float value);

   // Main thread, once per frame before any Push: the engine time the
   // frame's values were taken at (Transport::EngineFrameSeconds, <0 =
   // unknown) and the ramp delay in seconds. Shared by every mailbox.
   static void PublishFrame(double stampSeconds, double delaySeconds);

   // Audio thread only: advance smoothing by one sample and return the
   // current (smoothed) value for paramId. Call at most once per sample per
   // paramId - each call advances the smoother.
   float SmoothedValue(int paramId);

   // Audio thread: for params read once per block rather than per sample.
   // Advances the slot by numFrames samples (the ramps count samples, so
   // one SmoothedValue call per block would stretch them by the block size)
   // and returns the value at the block's end.
   float BlockValue(int paramId, int numFrames);

   // Audio thread only: initialize both current and target without ramping
   // (e.g. at startup, before the first block).
   void SetImmediate(int paramId, float value);

   // Audio thread only: jump the smoother straight to the last pushed target.
   void SnapToTarget(int paramId);

private:
   // Audio-thread state for one slot: a linear ramp toward `target`,
   // ending at engine time `endTime`, plus at most one queued ramp.
   struct Ramp
   {
      float cur = 0.0f;
      float inc = 0.0f;
      float target = 0.0f;
      float seen = 0.0f;
      int remaining = 0;
      bool primed = false; // a slot's first value lands, it does not ramp up from 0
      bool hasPending = false;
      float pendingValue = 0.0f;
      double endTime = 0.0;
      double pendingEnd = 0.0;
   };
   void OnNewTarget(Ramp& r, float value);
   void StartRamp(Ramp& r, float value, double startTime, double endTime);

   std::atomic<float> mTarget[kMaxParams] {};
   std::atomic<bool> mPushed { false }; // any Push yet: a slot is primed on its first read after one
   Ramp mRamps[kMaxParams];
   double mSampleRate = 0.0;
};
