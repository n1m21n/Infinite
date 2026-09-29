#pragma once

#include <memory>

#include "core/AudioCable.h"
#include "core/INode.h"

class AudioLooperNode;

// A live audio looper on one audio input: record a take, play it back on
// repeat, layer more on top, clear it.
//
//   REC   starts a take (a fresh one, replacing whatever is held). A take is
//         either a fixed length - "take" picks a MusicTime division, 1 bar by
//         default - or "free", where the next REC press ends it. With "sync"
//         on and the transport running, the take waits for the next grid line
//         (the take's own division, capped at one bar) instead of starting on
//         the press. A press while recording ends the take early.
//   PLAY  toggles playback of the held loop. Pressed mid-take it ends the
//         take and starts playing.
//   DUB   layers the input over the playing loop (toggle). Pressed mid-take it
//         ends the take and goes straight into overdub, the classic looper
//         gesture.
//   CLEAR empties the loop.
//
// The audio thread owns the whole record/play state machine and the loop
// buffer; this INode only posts button presses through a lock-free queue and
// publishes params, and reads the state back for the readout. Each of the four
// buttons is a modulatable widget (see DrawLooperBody): a click or a rising
// edge on its CV pin presses it.
//
// Latency compensation: what you play arrives at the looper late by the
// interface's round trip, so an uncompensated take sits behind the grid. The
// take window is shifted by Platform::AudioRoundTripLatencyFrames() (auto) plus
// a manual trim in ms; both are applied to overdubs as well.
//
// Not saved: the loop audio itself (params only) - a patch reloads empty.
class LooperNode : public INode, public IAudioSource
{
public:
   enum Button { kRec = 0, kPlay, kDub, kClear, kNumButtons };
   enum State { kEmpty = 0, kArmed, kRecording, kPlaying, kOverdubbing, kStopped };

   static INode* Create() { return new LooperNode(); }
   LooperNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~LooperNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   void SweepPrepare() override;

   INode* BypassSource() override { return input.GetSource(); }
   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }
   // Recording and playback must keep running with nothing pulling the output.
   bool RequiresAudioProcessing() const override { return true; }

   // Main thread. `level` is the button's current level (mouse held, or CV
   // high); a rising edge posts the press to the audio thread, so a held CV or
   // a held mouse presses exactly once.
   void SetButtonLevel(int button, bool level);

   // Published by the audio thread, refreshed every CookIfNeeded.
   int CurrentState() const { return mState; }
   float LoopSeconds() const { return mLoopSec; }
   float Position01() const { return mPos01; }
   float Level() const { return mPeak; }
   float CompensationMs() const { return mCompMs; }
   float RecordedSeconds() const { return mRecordedSec; }
   // Frames held (loop length, or recorded so far), and the fixed take's target
   // length while recording (0 = free / not recording).
   float LengthSeconds() const { return mLenSec; }
   float TargetSeconds() const { return mTargetSec; }
   static const char* StateName(int state);
   static constexpr double MaxSeconds() { return 60.0; }

   // take: 0 = free, n>0 = MusicTime::RateDivision(n - 1). Default 1 bar.
   int take = 3;
   bool syncStart = true; // wait for the next grid line when the transport runs
   bool autoLatency = true;
   float trimMs = 0.0f;   // -100..300, added to the auto figure
   float thru = 1.0f;     // input monitoring level (0 = loop only)
   float level = 1.0f;    // loop playback level

   AudioCable input;

   // Loop waveform for the card: min/max columns spanning the loop (or, while a
   // fixed take records, the whole take with the not-yet-recorded tail zero).
   // Refreshed by CookIfNeeded from the audio thread's peak bins.
   static constexpr int kWaveCols = 200;
   float waveMin[kWaveCols] = {};
   float waveMax[kWaveCols] = {};

private:
   std::unique_ptr<AudioLooperNode> mAudioNode;
   int mLastCookFrame = -1;
   bool mButtonLevel[kNumButtons] = {};
   int mState = kEmpty;
   float mLoopSec = 0.0f;
   float mRecordedSec = 0.0f;
   float mPos01 = 0.0f;
   float mPeak = 0.0f;
   float mCompMs = 0.0f;
   float mLenSec = 0.0f;
   float mTargetSec = 0.0f;
   int mLatencyFrames = 0;   // cached AudioRoundTripLatencyFrames, main thread
   int mLatencyPollFrame = -1000000;
};
