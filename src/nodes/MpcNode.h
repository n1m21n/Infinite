#pragma once

#include <memory>
#include <string>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioMpcNode;

// A 16-pad sample player in the spirit of an MPC.
//
// Each pad holds one sample and one of three play modes:
//   One shot - a hit plays the whole sample (a new hit restarts it)
//   Gate     - plays while the pad is held, stops on release
//   Loop     - a hit toggles a looping playback on/off
// Pads are hit with the mouse, with each pad's CV pin (a MIDI controller via a
// MIDI CC / Note modulator), or with the note input (notes 36..51 = pads 1..16,
// the usual pad-controller layout; fixed).
//
// Every pad is its own voice: two pads sound at once, each with its own mode,
// and a retrigger follows the pad's mode (one shot and gate restart the sample,
// loop toggles it off). Per pad: mode, sync (+ division), volume, pitch, pan,
// speed, fine tune, fade in, fade out, with the Sampler's definitions (see
// kInfo). The card shows the selected
// pad's rows, but ALL 16 pads' params are registered for modulation every frame
// under stable ids (ParamId) that do not depend on the selection.
//
// Per-pad transport (sync). A pad is Free (default) or Synced to a MusicTime
// division (Pattern A of the rhythmic-quantization-standard: "Synced"/"Free"
// plus the canonical division list).
//   Free   - a trigger plays at once (the behaviour before sync existed).
//   Synced - a trigger is LATCHED and fires on the next grid line of the
//            transport (sample-accurate: the audio thread places it at the exact
//            frame the line falls on). A trigger that lands exactly on a line
//            fires on it. With the transport stopped it fires at once. In gate
//            mode, releasing before the line cancels the hit (the release itself
//            is never delayed). In loop mode the toggle is quantised as well, and
//            while the pad is on the sample re-triggers on EVERY division (one
//            pass per division, rate-locked; a sample longer than the division
//            is cut at the line, a shorter one leaves a gap). With the transport
//            stopped a synced loop pad plays as a plain loop.
//
// Output: one master stereo mix of all pads.
class MpcNode : public INode, public IAudioSource
{
public:
   static constexpr int kPads = 16;
   static constexpr int kWaveCache = 128;
   enum PadMode { kOneShot = 0, kGate = 1, kLoopToggle = 2 };

   // The float params every pad has, in id order.
   enum PadParam { kVolume = 0, kPitch, kPan, kSpeed, kFine, kFadeIn, kFadeOut, kNumPadParams };
   enum SyncMode { kSynced = 0, kFree = 1 }; // Pattern A order: index 0 is "Synced"
   struct ParamInfo
   {
      const char* name;
      float lo, hi, def;
      const char* fmt;
   };
   static const ParamInfo& Info(int k)
   {
      static const ParamInfo kInfo[kNumPadParams] = {
         { "volume", 0.0f, 1.0f, 0.8f, "%.2f" },
         { "pitch", -24.0f, 24.0f, 0.0f, "%.1f st" },
         { "pan", -1.0f, 1.0f, 0.0f, "%.2f" },
         { "speed", -2.0f, 2.0f, 1.0f, "%.2fx" },
         { "fine tune", -50.0f, 50.0f, 0.0f, "%.0f c" },
         { "fade in", 0.0f, 250.0f, 3.0f, "%.0f ms" },
         { "fade out", 0.0f, 250.0f, 3.0f, "%.0f ms" },
      };
      return kInfo[k < 0 ? 0 : (k >= kNumPadParams ? kNumPadParams - 1 : k)];
   }
   // Stable modulation address of pad `pad`'s float param `k`: a function of
   // (pad, k) only, never of the selected pad or of draw order. The first five
   // keep the ids they had before fades existed (100 + pad * 5 + k); the two fade
   // params sit in their own block at 180 + pad * 2 + (k - 5), so no saved
   // binding moved. The discrete params (mode, sync, division) live in the
   // label-hashed 400..799 slots.
   static constexpr int ParamId(int pad, int k)
   {
      return k < 5 ? 100 + pad * 5 + k : 180 + pad * 2 + (k - 5);
   }
   static constexpr int kFirstNote = 36;

   static INode* Create() { return new MpcNode(); }
   MpcNode();
   ~MpcNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   void SweepPrepare() override;
   void PushParamsNow(); // fixtures: publish params without a frame cook

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

   int padMode[kPads];
   float padVolume[kPads];
   float padPitch[kPads]; // semitones
   float padPan[kPads];
   float padSpeed[kPads]; // -2..2, negative plays backwards
   float padFine[kPads];  // cents
   float padFadeIn[kPads];  // ms, 0..250
   float padFadeOut[kPads]; // ms, 0..250
   int padSync[kPads];      // SyncMode: 0 Synced, 1 Free (default)
   int padDiv[kPads];       // MusicTime::RateDivision index (used when Synced)
   int selectedPad = 0;

   float* PadParamPtr(int pad, int k)
   {
      pad = Clamp(pad);
      switch (k)
      {
      case kVolume: return &padVolume[pad];
      case kPitch: return &padPitch[pad];
      case kPan: return &padPan[pad];
      case kSpeed: return &padSpeed[pad];
      case kFadeIn: return &padFadeIn[pad];
      case kFadeOut: return &padFadeOut[pad];
      default: return &padFine[pad];
      }
   }
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
