#pragma once

#include <memory>
#include <string>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioMpcNode;
class AudioMpcOutNode;

// Infinite-Turbo: a 16-pad sample player in the spirit of an MPC.
//
// Each pad holds one sample and one of three play modes:
//   One shot - a hit plays the whole sample (a new hit restarts it)
//   Gate     - plays while the pad is held, stops on release; a new hit
//              restarts from the beginning ("radio" behaviour)
//   Loop     - a hit toggles a looping playback on/off; off rewinds
// Pads are hit with the mouse, with each pad's CV pin (a MIDI controller via
// a MIDI CC / Note modulator), or with the note input (note = base note +
// pad index, 36..51 by default, the usual pad-controller layout).
//
// Output: the node's own audio output is the master mix. Every pad is also
// rendered to its own stereo buffer; an "MPC Out" node wired from this node
// picks one pad, which is how a pad gets its own effect chain.
class MpcNode : public INode, public IAudioSource
{
public:
   static constexpr int kPads = 16;
   static constexpr int kWaveCache = 64;
   enum PadMode { kOneShot = 0, kGate = 1, kLoopToggle = 2 };

   static INode* Create() { return new MpcNode(); }
   MpcNode();
   ~MpcNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // Main thread. Returns false (pad left as it was) when decoding fails.
   bool LoadPad(int pad, const std::string& path);
   // Loads the first 16 audio files of a folder (alphabetical) into the pads.
   int LoadFolder(const std::string& folder);
   void ClearPad(int pad);
   void ReloadFromPaths();

   // A pad press (down=true) or release from the UI / CV. Edge events only.
   void PadEvent(int pad, bool down, float velocity = 1.0f);

   bool PadPlaying(int pad) const;
   bool PadLoaded(int pad) const { return !padPath[Clamp(pad)].empty(); }
   const std::string& PadName(int pad) const { return padName[Clamp(pad)]; }
   const std::string& PadStatus(int pad) const { return padStatus[Clamp(pad)]; }
   float Level() const { return mLevel; }

   // For MPC Out's tap (main thread, topology rebuild only).
   AudioMpcNode* AudioHalf() { return mAudioNode.get(); }

   int padMode[kPads];
   float padVolume[kPads];
   float padPitch[kPads]; // semitones
   float padPan[kPads];
   float padStart[kPads]; // trim in, 0..1 of the sample
   float padEnd[kPads];   // trim out, 0..1 of the sample
   int baseNote = 36;
   float volume = 0.8f;
   bool velocitySensitive = true;
   int selectedPad = 0;
   NoteCable noteInput;

   // UI edge detection for the pad buttons (main thread only).
   bool uiHeld[kPads] = {};

   float padWaveMin[kPads][kWaveCache] = {};
   float padWaveMax[kPads][kWaveCache] = {};
   int padWaveCount[kPads] = {};

   // Persisted file paths (VisitParams).
   std::string padPath[kPads];

private:
   static int Clamp(int pad) { return pad < 0 ? 0 : (pad >= kPads ? kPads - 1 : pad); }

   std::unique_ptr<AudioMpcNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
   unsigned int mPlayingMask = 0;
   std::string padName[kPads];
   std::string padStatus[kPads];
};

// Picks one pad's own stereo output from an MPC wired into its input.
// Anything else wired in simply passes through.
class MpcOutNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new MpcOutNode(); }
   MpcOutNode();
   ~MpcOutNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "mpc" : nullptr; }
   void ResolveAudioTaps() override;

   bool ConnectedToMpc() const { return mConnectedToMpc; }
   float Level() const { return mLevel; }

   int pad = 0;       // 0..15
   float gainDb = 0.0f;
   AudioCable input;

private:
   std::unique_ptr<AudioMpcOutNode> mAudioNode;
   int mLastCookFrame = -1;
   bool mConnectedToMpc = false;
   float mLevel = 0.0f;
};
