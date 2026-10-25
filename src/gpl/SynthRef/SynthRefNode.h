// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
//
// Synth REF: plays a real instrument firmware inside the patch. Pick an
// instrument + firmware (SynthRefCore.h's registry), and the firmware's own
// code runs natively: its engines, presets, screen and panel logic. The body
// draws the instrument's panel (keys, pads, encoders, faders, buttons and the
// firmware's LCD), and every control there is an event the firmware handles
// exactly as on the hardware. Notes come from the notes pin when it is
// cabled, otherwise from a MIDI controller chosen on the node (locked to it,
// or following the selected Synth REF). AUTO MAP drives the instrument's
// controls from a controller profile; MIDI LEARN instead exposes encoders,
// faders and pads as parameters for Infinite's own MIDI learn. The firmware's
// whole state (its storage and working project) is saved in the patch.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"

#include "SynthRefCoreApi.h"
#include "SynthRefMidi.h"

class AudioSynthRefNode;

class SynthRefNode : public INode, public IAudioSource
{
public:
   static constexpr int kLearnKnobs = 8;
   static constexpr int kLearnFaders = 4;
   static constexpr int kLearnPads = 16;
   enum { kViewCompact = 0, kViewFull = 1 };
   enum { kMapAuto = 0, kMapLearn = 1 };

   static INode* Create() { return new SynthRefNode(); }
   SynthRefNode();
   ~SynthRefNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // ---- saved (VisitParams order is the patch format: append only) ----
   std::string coreId = "smk37_open";
   float volume = 0.0f;       // dB
   int view = kViewFull;
   std::string deviceKey;     // MIDI input, "" = none
   bool deviceLock = false;   // receive even when another Synth REF is focused
   int mapMode = kMapAuto;
   std::string profileId;     // "" = automatic for the device
   float learnKnob[kLearnKnobs] = {};   // MIDI LEARN strip, 0..1, turned into encoder detents
   float learnFader[kLearnFaders] = {}; // MIDI LEARN strip, 0..1 -> fader 0..127
   std::string state;         // base64 of the core's save_state()

   NoteCable noteInput;

   // ---- runtime, main thread ----
   bool CoreReady() const;
   const std::string& CoreError() const { return mCoreError; }
   const SynthRefCoreInfo* Info() const;
   bool SwitchCore(const std::string& id); // keeps nothing of the old firmware
   void ResetFirmware();                   // factory state (wipes the virtual storage)

   void SendControl(int kind, int id, int value);
   void SendMidi(const uint8_t* bytes, int count);
   void Panic();
   int LoadFactory(int index);
   int LoadProgram(int program);
   // .syx (DX7 voice or 32-voice bank into `dx7Bank` 0..3), .fm1 project, .fm1t timbre.
   int ImportFile(const std::string& path, int dx7Bank, std::string& message);
   bool ExportProject(const std::string& path, std::string& message);

   // Snapshots the audio thread publishes (no lock): firmware screen, LEDs
   // and state. LcdPixels returns nullptr when the core has no screen.
   const uint16_t* LcdPixels(uint32_t& revision);
   int Query(int key) const;
   int Led(int index) const;
   std::string ProjectName() const;
   std::string PadLabel(int pad) const;
   std::string TrackSource(int track) const;
   const std::vector<std::string>& ProgramNames() const { return mProgramNames; }
   const std::vector<std::string>& FactoryNames() const { return mFactoryNames; }
   // Dropdown labels, rebuilt only when the names change ("001 PIANO 1"...,
   // "factory project..." + names).
   const std::vector<std::string>& ProgramOptions() const { return mProgramOptions; }
   const std::vector<std::string>& FactoryOptions() const { return mFactoryOptions; }
   // Body-owned UI state (held keys, encoder angles), freed with the node.
   std::shared_ptr<void> uiState;
   float MeterPeak(int ch) const;
   bool MidiRecent() const; // a device message or pin note in the last 150 ms
   bool AudioLive() const;  // an audio callback is rendering this node

   // MIDI device policy.
   bool DeviceActive() const;      // receiving from deviceKey right now
   void Focus();                   // this node becomes the focused Synth REF
   bool Focused() const;
   bool DeviceConnected() const;   // deviceKey is an open input
   void Rescan();                  // MIDI DEVICE RESCAN
   const std::vector<SrProfile>& Profiles() const { return mProfiles; }
   const std::vector<std::string>& ProfileErrors() const { return mProfileErrors; }
   const SrProfile* ActiveProfile() const { return mActiveProfile; }
   void ReloadProfiles();

   // MIDI DIAGNOSTICS: newest last.
   const std::deque<std::string>& DiagLog() const { return mDiag; }
   void ClearDiag() { mDiag.clear(); }
   const std::string& LastEvent() const { return mLastEvent; }
   const std::string& DeviceStatus() const { return mDeviceStatus; }

   // UI-held controls (mouse), for drawing pressed states.
   uint64_t uiKeys = 0;
   uint32_t uiPads = 0;
   uint32_t uiButtons = 0;
   int uiFader[6] = { 0, 0, 0, 0, 64, 0 }; // F1..F4, pitch, mod as last sent / received
   bool uiPedal = false;
   bool learnPadHeld[kLearnPads] = {};
   bool showDiag = false;
   std::string uiMessage;      // result of the last import / export / switch
   // What the controller is doing, from the diagnostics stream: notes held on
   // the keyboard path, pads held, and when each mapped control last moved.
   bool midiNoteHeld[128] = {};
   uint32_t midiPads = 0;
   // Milliseconds since a mapped control last moved (INT64_MAX = never).
   int64_t TouchAgeMs(int target, int id) const;
   // Detents each encoder received from the controller (panel rotation).
   int encoderTurns[8] = {};
   // The firmware screen as a GL texture, owned by the body; released through
   // sDeleteTexture (set by the UI) when the node goes away.
   unsigned int lcdTexture = 0;
   uint32_t lcdTextureRevision = 0;
   static void (*sDeleteTexture)(unsigned int texture);

   // Test hooks: renders `frames` of audio without an engine (the node is not
   // in a running graph). Returns false without a core.
   bool RenderForTest(float* left, float* right, int frames, double sampleRate, int block = 512);
   bool SaveStateNow() { return RefreshState(); }
   // A message as if `deviceKey` had sent it (the AUTO MAP / keyboard path).
   void InjectDeviceMidiForTest(const uint8_t* bytes, int count);
   AudioSynthRefNode* AudioForTest() { return mAudioNode.get(); }

private:
   void EnsureAudioNode();
   void OpenCoreFromState();
   void ApplyMapping();
   void PollDevice();
   void DrainDiagnostics();
   void RefreshNames();
   bool RefreshState(); // export the core state into `state`

   std::unique_ptr<AudioSynthRefNode> mAudioNode;
   int mLastCookFrame = -1;
   std::string mCoreError;
   std::string mOpenedCore;     // coreId the running core belongs to
   std::string mAppliedState;   // the `state` text the core was loaded from / exported to
   int mStateRevision = -1;     // core edit revision `state` reflects
   int64_t mStatePendingSince = 0;
   std::vector<uint8_t> mExportBuf; // async state export target
   bool mExportBusy = false;
   int mExportRevision = -1;
   std::vector<std::string> mProgramOptions, mFactoryOptions;
   std::string mAvailableNames;     // OS MIDI input list at the last poll
   int64_t mNextRescanMs = 0;
   std::vector<std::string> mProgramNames, mFactoryNames;
   std::vector<SrProfile> mProfiles;
   std::vector<std::string> mProfileErrors;
   const SrProfile* mActiveProfile = nullptr;
   std::string mAppliedMapKey;
   std::deque<std::string> mDiag;
   std::string mLastEvent, mDeviceStatus;
   float mSentKnob[kLearnKnobs] = {};
   float mSentFader[kLearnFaders] = {};
   bool mSentPad[kLearnPads] = {};
   bool mLearnPrimed = false;
   double mNextDevicePoll = 0.0;
   uint32_t mLcdRevision = 0;
   int64_t mTouch[kSrTargets][32] = {};
};
