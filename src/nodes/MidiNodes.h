#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Modulation.h"

// Tracks one physical MIDI control (a knob, fader, or pad) via MIDI Learn:
// click Learn, move the control on the hardware, this node remembers its
// channel + controller number and keeps polling that one binding. Works
// identically across any class-compliant USB MIDI controller since it only
// ever looks at generic Control Change / Note On messages - see
// src/platform/Platform.h's MIDI input section for the capture engine.
class MidiCCNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MidiCCNode(); }
   ~MidiCCNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   bool IsHardwareDriven() const override { return true; }

   float Value01() override { return mValue; }

   bool StartLearn();
   void CancelLearn();
   bool IsLearning() const { return mLearning; }
   // A binding needs both: two controllers can easily share a channel/CC
   // number (e.g. both default to channel 1), so device disambiguates which
   // physical source this node actually listens to.
   bool IsBound() const { return channel >= 0 && device != 0; }
   std::string BindingLabel() const;
   std::string Status() const;

   int device = 0; // Platform::MidiDeviceId of the source this was learned on (0 = unbound)
   int channel = -1;
   int controller = -1;
   bool isNote = false;
   float low = 0.0f;
   float high = 1.0f;
   bool invert = false;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("device", device);
      v.Int("channel", channel); v.Int("controller", controller); v.Bool("isNote", isNote);
      v.Float("low", low); v.Float("high", high); v.Bool("invert", invert);
   }

private:
   bool mLearning = false;
   float mValue = 0.0f;
   int mLastCookFrame = -1;
};

// Fires a decaying pulse (0..1, back down to 0) whenever a specific MIDI
// note is hit - the pad-triggered counterpart to MidiCCNode above. A pad hit
// is an event, not a position, so this mirrors AudioAnalyzeNode's onset
// envelope (src/nodes/AnalyzeNodes.cpp) rather than holding a live value:
// spike on the event, decay over `hold` seconds, sit at 0 otherwise.
class MidiTriggerNode : public INode, public IModulator
{
public:
   // Pad: bound to one specific note; fires a decaying 0..1 pulse on every
   // hit of that note, everything else on the channel is ignored.
   // Keyboard: bound to a whole channel; any note played updates the output
   // to that note's number (0-127, normalised), held until the next note -
   // a keyboard reports "which key", not "was a pad hit".
   enum Mode { kPad = 0, kKeyboard, kModeCount };

   static INode* Create() { return new MidiTriggerNode(); }
   static const std::vector<std::string>& ModeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   bool IsHardwareDriven() const override { return true; }

   float Value01() override { return mEnvelope; }

   bool StartLearn();
   void CancelLearn();
   bool IsLearning() const { return mLearning; }
   // Both device and channel matter: two controllers can share a channel
   // (most default to channel 1), so device disambiguates which physical
   // source this node actually listens to.
   bool IsBound() const { return channel >= 0 && device != 0; }
   std::string BindingLabel() const;
   std::string Status() const;

   int mode = kPad;
   int device = 0; // Platform::MidiDeviceId of the source this was learned on (0 = unbound)
   int channel = -1;
   int note = -1;                 // kPad only; -1 in kKeyboard (any note on the channel)
   float hold = 0.15f;            // kPad only: seconds the envelope stays lit before decaying
   bool velocitySensitive = true; // kPad only: peak = velocity/127 vs. always 1.0

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Int("device", device); v.Int("channel", channel); v.Int("note", note);
      v.Float("hold", hold); v.Bool("velocitySensitive", velocitySensitive);
   }

private:
   bool mLearning = false;
   float mEnvelope = 0.0f;
   double mLastSeconds = 0.0;
   unsigned int mLastHitSeq = 0; // last hit-counter value consumed from Platform
   int mLastCookFrame = -1;
};
