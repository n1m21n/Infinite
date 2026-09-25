#pragma once

#include <memory>
#include <string>

#include "core/AudioCable.h"
#include "core/INode.h"

class AudioLooperNode;

// Infinite-Turbo: a live audio looper (RC-style) on one audio input.
//
// REC starts a take (optionally waiting for the next bar / sub-bar line of
// the transport), PLAY starts/stops playback, DUB layers the input on top of
// the loop while it plays, CLEAR empties it. The take length is either a
// number of bars, a fraction of a bar ("sub-bar"), or free (REC again ends
// the take). Playback runs forward, reversed or ping-pong, looping or once.
//
// Two-object rule: this INode only publishes params and commands; the audio
// thread owns the loop buffer (preallocated once, see AudioLooperNode) and
// the whole record/play state machine. Every button goes through a
// modulatable widget in main.cpp, so each has a CV pin for MIDI controllers.
class LooperNode : public INode, public IAudioSource
{
public:
   enum LengthMode { kLengthBars = 0, kLengthSubBar = 1, kLengthFree = 2 };
   enum Direction { kForward = 0, kReverse = 1, kPingPong = 2 };
   enum State { kIdle = 0, kArmed = 1, kRecording = 2, kPlaying = 3, kOverdubbing = 4, kStopped = 5 };

   static INode* Create() { return new LooperNode(); }
   LooperNode();
   ~LooperNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }
   // Must keep running (recording/playing) even when nothing downstream
   // pulls it yet.
   bool RequiresAudioProcessing() const override { return true; }

   // Commands, main thread. Each asks for a state; the audio thread applies
   // it at the start of its next block.
   void SetRecord(bool on);
   void SetPlay(bool on);
   void SetOverdub(bool on);
   void Clear();

   // Published by the audio thread, refreshed each CookIfNeeded.
   int CurrentState() const { return mState; }
   bool IsRecordingOrArmed() const { return mState == kRecording || mState == kArmed; }
   bool IsPlaying() const { return mState == kPlaying || mState == kOverdubbing; }
   bool IsOverdubbing() const { return mState == kOverdubbing; }
   bool HasLoop() const { return mLengthSec > 0.0f; }
   float LengthSeconds() const { return mLengthSec; }
   float RecordedSeconds() const { return mRecordedSec; }
   float Position01() const { return mPos01; }
   float Level() const { return mLevel; }
   static double MaxSeconds();
   static const char* StateName(int state);

   int lengthMode = kLengthBars;
   int bars = 1;          // 1..32, Bars mode
   int subDivision = 1;   // 0 = 1/2 bar, 1 = 1/4, 2 = 1/8, 3 = 1/16 (Sub-bar mode)
   bool syncStart = true; // Bars / Sub-bar: the take waits for the next grid line
   bool loop = true;      // off = play the take once, then stop
   int direction = kForward;
   float thru = 1.0f;     // input monitoring level (0 = loop only)
   float level = 1.0f;    // loop playback level
   // Turbo: record latency compensation. What you play reaches the looper
   // late by the interface's round trip (you hear the beat late, the input
   // arrives late), so an uncompensated take sits behind the grid. The take
   // window is shifted by that amount: `autoLatency` uses what the driver
   // reports, `latencyOffsetMs` adds (or removes) a manual trim on top.
   bool autoLatency = true;
   float latencyOffsetMs = 0.0f; // -100..+300
   float CompensationMs() const { return mCompMs; }

   AudioCable input;

private:
   std::unique_ptr<AudioLooperNode> mAudioNode;
   int mLastCookFrame = -1;
   int mState = kIdle;
   float mLengthSec = 0.0f;
   float mRecordedSec = 0.0f;
   float mPos01 = 0.0f;
   float mLevel = 0.0f;
   float mCompMs = 0.0f;
};
