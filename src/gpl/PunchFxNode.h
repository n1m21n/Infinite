// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
//
// Punch FX: pocket-operator style performance effects on an audio stream.
// 16 pads, each with its own effect and its own MIDI note (learnable). Hold a
// pad (mouse or controller) and the signal goes through its effect; let go
// and the dry signal comes back, or latch pads on/off. Any number at once,
// chained in table order. Loops, stutter, reverse, tape stop, half speed,
// filter sweeps, kills, crush, gate, echo, rewind, octave up, flanger,
// auto-pan, fade out, freeze.
// The effects are a float port of punch.c from SLOOP / Felucca
// (github.com/isod89/sloop-fm1, GPL-3.0-only, Leo Kuroshita and contributors).
#pragma once

#include <cstdint>
#include <memory>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPunchFxNode;

class PunchFxNode : public INode, public IAudioSource
{
public:
   static constexpr int kNumEffects = 27;
   static constexpr int kNumPads = 16;
   static constexpr int kBaseNote = 36; // default: pad p answers note 36 + p
   static const char* EffectName(int i);

   static INode* Create() { return new PunchFxNode(); }
   PunchFxNode();
   ~PunchFxNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   NoteCable* NoteInputSlot(int slot) override { return slot == 1 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : slot == 1 ? "notes" : nullptr; }

   // Saved. Each pad: an effect and the MIDI note that plays it (several
   // pads may share a note: one key, several effects).
   int padFx[kNumPads] = {};
   int padNote[kNumPads] = {};
   bool latch = false;   // pads toggle on/off instead of momentary
   // Saved / modulatable / MIDI-learnable single trigger.
   int effect = 0;
   bool engage = false;

   // Runtime only (UI). Each pad is a gate button with its own CV pin, so
   // global MIDI learn, a Macro or a MIDI CC can hold it, like the MPC pads.
   uint32_t heldPads = 0;  // bit per pad held (mouse or its pin)
   bool uiHeld[kNumPads] = {};
   int selectedPad = 0;    // pad whose effect the dropdown edits
   void TogglePadLatch(int pad);
   bool PadLatched(int pad) const { return (mMouseLatchPads >> pad) & 1u; }
   uint32_t ActiveMask() const { return mActiveMask; } // bit = effect playing

   AudioCable input;
   NoteCable noteInput;

private:
   std::unique_ptr<AudioPunchFxNode> mAudioNode;
   int mLastCookFrame = -1;
   uint32_t mActiveMask = 0;
   uint32_t mMouseLatchPads = 0;
};
