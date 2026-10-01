#pragma once

#include <memory>
#include <string>

#include "core/INode.h"
#include "core/Modulation.h"

// Turbo: Transport Control. Drives the global transport - tempo, key/scale,
// time signature, play/stop/rewind, tap tempo - from anything that can
// modulate a parameter (LFO, MIDI CC via MIDI learn, Macro, Chord
// Progression...) instead of the mouse. Main thread only: the transport's
// own setters are the thread-safe boundary (SetTempo stages the change for
// the audio thread's next block), so there is no audio half.
//
// Each section only takes over the transport while its "drive" switch is on.
// Off, its controls mirror the live transport, so switching drive on picks up
// from the current tempo/key instead of jumping to a stale value.
//
// Outputs (modulators, patch into any param): beat - a 0..1 ramp every beat;
// bar - a 0..1 ramp every bar; bpm - tempo mapped 20..300 -> 0..1; play - 1
// while the transport plays.
class AudioTransportClickNode;

class TransportControlNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new TransportControlNode(); }
   TransportControlNode();
   ~TransportControlNode() override; // turns the direct click off with the node

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   // 0..3 modulators (beat, bar, bpm, play), 4 = the metronome as audio.
   int OutputCount() const override { return 5; }
   AudioNode* GetAudioNode() override;
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;

   // tempo
   bool driveTempo = false;
   float bpm = 120.0f;       // 20..300, the target
   float glide = 0.0f;       // seconds to reach a new target (0 = jump)
   bool followClock = false; // external MIDI clock sets the target while present

   // key / scale
   bool driveKey = false;
   int key = 0;              // 0..11, C..B
   int scale = 0;            // MusicTime::ScaleType
   bool keyOnBar = true;     // a key/scale change waits for the next bar while playing

   // audio
   bool audioOnOpen = false; // opening a patch with this on starts the audio engine

   // metronome
   bool click = false;        // on/off (a pin: MIDI pad, Macro Toggle...)
   float clickVolume = 0.5f;  // 0..1
   bool clickDirect = true;   // straight to the device output, no cable needed
   bool clickAccent = true;   // higher click on the first beat of the bar

   // meter
   bool driveMeter = false;
   int meterNum = 4;         // 1..16
   int meterDenIndex = 1;    // 0..3 -> 2, 4, 8, 16

   // Actions (main thread, from the node body's buttons / trigger pins).
   void Play();
   void Stop();
   void Rewind();
   void Tap();               // tap tempo: the mean of the last taps sets bpm

   // Readouts for the body.
   float AppliedBpm() const { return mAppliedBpm; }
   bool KeyPending() const { return mKeyPending; }
   bool ClockPresent() const;

   static int MeterDen(int index);

private:
   class OutTap : public IModulator
   {
   public:
      int index = 0;
      float Value01() override;
   };
   OutTap mOutTaps[4];
   bool mTapsBound = false;

   std::unique_ptr<AudioTransportClickNode> mAudioNode;
   int mLastCookFrame = -1;
   float mAppliedBpm = -1.0f;   // the gliding tempo actually sent to the transport
   double mLastCookTime = -1.0;
   double mTaps[8] = {};
   int mNumTaps = 0;
   bool mKeyPending = false;
   bool mDirectSent = false;
   float mDirectVolSent = -1.0f;
   bool mDirectAccentSent = true;
   long long mPendingBar = 0;
};
