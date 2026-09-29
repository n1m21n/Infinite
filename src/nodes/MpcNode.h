#pragma once

#include <memory>
#include <string>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioMpcNode;
class AudioMpcOutNode;

// A 16-pad sample player in the spirit of an MPC.
//
// Each pad holds one sample and one of three play modes:
//   One shot - a hit plays the whole sample (a new hit restarts it)
//   Gate     - plays while the pad is held, stops on release
//   Loop     - a hit toggles a looping playback on/off
// Pads are hit with the mouse, with each pad's CV pin (a MIDI controller via a
// MIDI CC / Note modulator), or with the note input (note = base note + pad
// index; 36..51 by default, the usual pad-controller layout).
//
// Output: the node's own audio output is the master mix. Every pad is also
// rendered to its own stereo buffer; an "MPC Out" node wired from this node
// picks one pad, which is how a pad gets its own effect chain.
class MpcNode : public INode, public IAudioSource
{
public:
   static constexpr int kPads = 16;
   static constexpr int kWaveCache = 128;
   enum PadMode { kOneShot = 0, kGate = 1, kLoopToggle = 2 };

   static INode* Create() { return new MpcNode(); }
   MpcNode();
   ~MpcNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   void SweepPrepare() override;

   AudioNode* GetAudioNode() override;
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // Main thread. Returns false (pad left as it was) when decoding fails.
   bool LoadPad(int pad, const std::string& path);
   // Loads the first 16 audio files of a folder (alphabetical) into the pads.
   int LoadFolder(const std::string& folder);
   void ClearPad(int pad);
   void ReloadFromPaths();

   // The pad's current gate level from the UI or a CV pin (mouse held, or CV
   // high). Only edges reach the audio thread, so a held level fires once.
   void SetPadHeld(int pad, bool held, float velocity = 1.0f);

   bool PadPlaying(int pad) const { return (mPlayingMask & (1u << Clamp(pad))) != 0; }
   bool PadLoaded(int pad) const { return !padPath[Clamp(pad)].empty(); }
   const std::string& PadName(int pad) const { return padName[Clamp(pad)]; }
   float Level() const { return mLevel; }

   // For MPC Out's tap (main thread, topology rebuild only).
   AudioMpcNode* AudioHalf() { return mAudioNode.get(); }

   int padMode[kPads];
   float padVolume[kPads];
   float padPitch[kPads]; // semitones
   float padPan[kPads];
   int baseNote = 36;
   float volume = 0.8f;
   int selectedPad = 0;
   NoteCable noteInput;

   float padWaveMin[kPads][kWaveCache] = {};
   float padWaveMax[kPads][kWaveCache] = {};
   int padWaveCount[kPads] = {};
   std::string padPath[kPads]; // persisted

   // UI-only, main thread. Canvas-space rects (x0,y0,x1,y1) of each pad and of
   // the selected-pad waveform, cached while the card draws so a file dropped
   // on the canvas can be routed to the pad under it; padFlash is the
   // ImGui time of a pad's last hit, for the pad's hit feedback.
   float padRect[kPads][4] = {};
   float waveRect[4] = {};
   double padFlash[kPads] = {};
   // Pad under a canvas point, -1 when none.
   int PadAtCanvas(float x, float y) const
   {
      for (int p = 0; p < kPads; p++)
         if (x >= padRect[p][0] && x <= padRect[p][2] && y >= padRect[p][1] && y <= padRect[p][3])
            return p;
      return -1;
   }
   // First empty pad at or after `from` (wrapping), or -1 when all 16 are loaded.
   int NextEmptyPad(int from) const
   {
      for (int i = 0; i < kPads; i++)
      {
         const int p = (Clamp(from) + i) % kPads;
         if (padPath[p].empty())
            return p;
      }
      return -1;
   }

   static int Clamp(int pad) { return pad < 0 ? 0 : (pad >= kPads ? kPads - 1 : pad); }

private:
   std::unique_ptr<AudioMpcNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
   unsigned int mPlayingMask = 0;
   bool mHeld[kPads] = {};
   std::string padName[kPads];
};

// Picks one pad's own stereo output from an MPC wired into its input. Anything
// else wired in simply passes through.
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
   void SweepPrepare() override;

   INode* BypassSource() override { return input.GetSource(); }
   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "mpc" : nullptr; }
   void ResolveAudioTaps() override;

   bool ConnectedToMpc() const { return mConnectedToMpc; }
   float Level() const { return mLevel; }

   int pad = 1; // 1..16, as numbered on the MPC's grid
   float gainDb = 0.0f;
   AudioCable input;

private:
   std::unique_ptr<AudioMpcOutNode> mAudioNode;
   int mLastCookFrame = -1;
   bool mConnectedToMpc = false;
   float mLevel = 0.0f;
};
