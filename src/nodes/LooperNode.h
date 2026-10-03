#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <vector>

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
// take window is shifted by Platform::AudioRoundTripLatencyFrames(), always (there
// is no switch: the old trim and auto comp controls are gone); it is applied to overdubs as well.
//
// Playback is a sample player over the held loop, with the Sampler's own
// controls: finetune (cents), pitch (semitones), speed (negative plays it
// backwards) and volume, plus fade in / fade out (ms) applied at the start and
// end of every pass through the loop. The playback rate is
//    speed * 2^((pitch + finetune / 100) / 12)
// At exactly 1.0 the loop is its recorded length and stays on the grid. Any
// other rate plays it in length / |rate|, so it DRIFTS against the transport
// (that is the point of varispeed), and overdub is paused: layers are only
// written at the rate they were recorded at.
//
// The loop audio is kept: once a take or an overdub settles, the held loop is
// written as a 32-bit float stereo WAV in the Recordings folder and `loopFile`
// (a saved param) points at it, so reopening the patch, or a whole-patch undo
// that respawns the node, brings the loop back (stopped, ready for PLAY).
// Float, not the shared 16-bit writer: overdubs can sum past 0 dBFS and the
// file must hold exactly what the looper held. Each new take gets a new file;
// overdubs and CLEAR rewrite the take's own file, but only one this instance
// wrote - a loop that came from a patch is never modified on disk.
// UNDO steps back one settled layer at a time (take, overdub, CLEAR), up to
// kMaxUndo steps, from copies the main thread already makes to write the file;
// the audio thread holds no history and the loop keeps playing through it.
class LooperNode : public INode, public IAudioSource
{
public:
   enum Button { kRec = 0, kPlay, kDub, kClear, kUndo, kNumButtons };
   static constexpr int kMaxUndo = 8;
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

   // No BypassSource: the Looper is a generator (Synths). Bypassed, it outputs
   // nothing, like the Sampler. It is not a pass-through effect.
   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }
   // Recording and playback must keep running with nothing pulling the output.
   bool RequiresAudioProcessing() const override { return true; }

   // Main thread. `level` is the button's current level (mouse held, or CV
   // high); a rising edge posts the press to the audio thread, so a held CV or
   // a held mouse presses exactly once.
   void SetButtonLevel(int button, bool level);
   // Rising edges seen so far (main thread; fixtures assert on it).
   int ButtonPresses(int button) const { return button >= 0 && button < kNumButtons ? mButtonPresses[button] : 0; }

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

   // Playback rate of the held loop: speed * 2^((pitch + finetune / 100) / 12).
   static float RateFor(float pitchSemis, float fineCents, float speed)
   {
      return speed * std::pow(2.0f, (pitchSemis + fineCents / 100.0f) / 12.0f);
   }
   float Rate() const { return RateFor(pitch, finetune, speed); }
   // True when the loop plays at its recorded speed (locked to the grid).
   bool AtUnity() const { return std::fabs(Rate() - 1.0f) < 1e-4f; }

   // take: 0 = free, n>0 = MusicTime::RateDivision(n - 1). Default 1 bar.
   int take = 3;
   bool syncStart = true; // wait for the next grid line when the transport runs
   int testLatencyFrames = -1; // fixtures only: >= 0 replaces the measured round trip (0 = no compensation)
   std::string testLoopDir;    // fixtures only: non-empty writes loop files there, synchronously
   bool thru = true;      // monitor the input alongside the loop
   float finetune = 0.0f; // cents, +/-50, stacks on pitch
   float pitch = 0.0f;    // semitones, +/-24
   float speed = 1.0f;    // -2..2: scales rate and pitch together, negative plays backwards
   float volume = 1.0f;   // 0..1, loop playback level (unity: the loop sits level with the live input)
   float fadeIn = 3.0f;   // ms, 0..250, ramp at the start of every pass
   float fadeOut = 3.0f;  // ms, 0..250, ramp at the end of every pass

   // WAV holding the loop audio ("" = no loop). Written by the node, saved
   // with the patch, read back on load. See the class comment.
   std::string loopFile;
   // Why the saved loop could not be read back, or empty.
   const std::string& LoopStatus() const { return mLoopStatus; }
   // Settled layers UNDO can still step back through.
   int UndoDepth() const { return mHistory.empty() ? 0 : (int)mHistory.size() - 1; }

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
   int mButtonPresses[kNumButtons] = {};
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

   // Loop file bookkeeping (main thread). See SyncLoopFile.
   void SyncLoopFile();
   std::string mAppliedFile;  // the loopFile the audio half currently holds
   std::string mLoopStatus;
   int mSavedVersion = 0;     // audio loop version last written (or loaded)
   int mFileTake = -1;        // take serial loopFile was written for; -1 = not written here

   // Undo history (main thread): every settled loop, oldest first; back() is
   // what the looper holds now. An empty snapshot is a CLEAR.
   struct LoopSnap
   {
      std::vector<float> interleaved; // stereo
      int frames = 0;
      double sampleRate = 0.0;
   };
   void PushHistory(std::shared_ptr<const LoopSnap> snap);
   void RestoreSnap(const LoopSnap& snap);
   std::string NewLoopPath(const std::string& tag);
   void WriteLoop(const std::string& path, std::shared_ptr<const LoopSnap> snap);
   std::vector<std::shared_ptr<const LoopSnap>> mHistory;
   int mUndoRequests = 0;
   bool mUndoDubSent = false;   // an UNDO pressed mid-overdub closed the layer first
   int mUndoFromVersion = 0;    // version when that close was sent
   bool mAdoptNextCapture = false; // the next settled loop is an undo's own CLEAR, not a layer
   int mRestoreSerial = 0;
};
