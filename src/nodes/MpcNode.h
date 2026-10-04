#pragma once

#include <memory>
#include <string>

#include "audio/WavePeaks.h"
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
//
// Turbo 0.48 (from upstream): per pad speed (-2..2, negative plays the trim
// range backwards), fine tune (cents), fade in / fade out (ms, every pass
// through the trim range, see audio/PassFade.h), and sync. A Free pad
// (default) plays at once. A Synced pad latches a hit and fires it on the
// next grid line of its MusicTime division (sample-accurate; at once with the
// transport stopped). Gate: a release before the line cancels the hit. Loop:
// the toggle is quantised too, and while on the sample restarts on EVERY
// division line (one pass per division); with the transport stopped a synced
// loop plays as a plain loop. Defaults keep the old sound.
class MpcNode : public INode, public IAudioSource
{
public:
   static constexpr int kPads = 16;
   static constexpr int kWaveCache = 64;
   enum PadMode { kOneShot = 0, kGate = 1, kLoopToggle = 2 };
   enum SyncMode { kSynced = 0, kFree = 1 }; // upstream's order (pad<n>_sync)

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
   // Turbo 0.48 (saved after every older key, see VisitParams).
   float padSpeed[kPads];   // -2..2, negative plays backwards
   float padFine[kPads];    // cents, -50..50
   float padFadeIn[kPads];  // ms, 0..250
   float padFadeOut[kPads]; // ms, 0..250
   int padSync[kPads];      // SyncMode, kFree by default
   int padDiv[kPads];       // MusicTime::RateDivision index (Synced)
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
   // Turbo 0.49: multi-resolution peaks for the pad waveform view.
   WavePeaks padPeaks[kPads];
   // Turbo 0.49: the pad voice's play position, 0..1 of the whole sample,
   // published per block by the audio thread; < 0 when the pad is silent.
   float PadPlayPosition(int pad) const;

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
