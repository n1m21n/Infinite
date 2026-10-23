#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/GLUtil.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioVmpcNode;
class VideoSourceNode;

// VMPC: a 16-pad video player in the spirit of the MPC node, for clips instead
// of samples. The idea comes from Ricardo Palmieri's Infinite-Turbo fork; this
// is a fresh implementation.
//
// Each pad holds one video clip and the MPC's three play modes:
//   One shot - a hit plays the clip's window once (a new hit restarts it)
//   Gate     - plays while the pad is held; a release fades out over "fade out"
//   Loop     - a hit toggles a looping playback on/off
// Pads are hit with the mouse, with each pad's CV pin, or with the note input
// (notes 36..51 = pads 1..16, the MPC's fixed layout). Sync is the MPC's
// Pattern A pair (Synced latches the hit to the next grid line; a synced loop
// restarts on every division).
//
// Output 0 is one picture: the most recently hit playing pad on top, with the
// node's transition (cut, crossfade, dip to black over "time") from whatever
// was showing before. When the top pad ends, the most recent pad still playing
// comes back in with the same transition; with none left the picture goes to
// black, or holds its last frame when "hold last frame" is on. Output 1 is the
// visible pads' own soundtracks, each at the weight its picture is shown with
// (decoded only while "soundtrack" is on).
//
// The pad engine runs on the main thread (CookIfNeeded): video is decoded
// there anyway. The clock is the transport while it plays, wall time while it
// is stopped, so pads play either way. At most four pads decode per frame (the
// two on screen, then the most recent playing ones); the rest keep time without
// decoding.
class VmpcNode : public INode, public IAudioSource
{
public:
   static constexpr int kPads = 16;
   static constexpr int kFirstNote = 36;
   static constexpr int kThumbs = 8;     // filmstrip frames per clip
   static constexpr int kThumbW = 96;
   static constexpr int kThumbH = 54;
   enum PadMode { kOneShot = 0, kGate = 1, kLoopToggle = 2 };
   enum SyncMode { kSynced = 0, kFree = 1 }; // Pattern A order: index 0 is "Synced"
   enum Transition { kCut = 0, kCrossfade = 1, kDip = 2 };

   // The float params every pad has, in id order.
   enum PadParam { kOpacity = 0, kSpeed, kStart, kEnd, kFadeIn, kFadeOut, kNumPadParams };
   struct ParamInfo
   {
      const char* name;
      float lo, hi, def;
      const char* fmt;
   };
   static const ParamInfo& Info(int k)
   {
      static const ParamInfo kInfo[kNumPadParams] = {
         { "opacity", 0.0f, 1.0f, 1.0f, "%.2f" },
         { "speed", -2.0f, 2.0f, 1.0f, "%.2fx" },
         { "start", 0.0f, 100.0f, 0.0f, "%.1f %%" },
         { "end", 0.0f, 100.0f, 100.0f, "%.1f %%" },
         { "fade in", 0.0f, 2000.0f, 0.0f, "%.0f ms" },
         { "fade out", 0.0f, 2000.0f, 0.0f, "%.0f ms" },
      };
      return kInfo[k < 0 ? 0 : (k >= kNumPadParams ? kNumPadParams - 1 : k)];
   }
   // Stable modulation address of pad `pad`'s float param `k` (100..195): a
   // function of (pad, k) only, never of the selected pad. The transition time
   // is 200; the dropdowns and toggles live in the label-hashed slots.
   static constexpr int ParamId(int pad, int k) { return 100 + pad * kNumPadParams + k; }
   static constexpr int kTimeParamId = 200;

   static INode* Create() { return new VmpcNode(); }
   VmpcNode();
   ~VmpcNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   void SweepPrepare() override;

   int OutputCount() const override { return 2; }
   const char* OutputLabel(int i) const override { return i == 1 ? "audio" : "video"; }
   AudioNode* GetAudioNode() override;
   bool IsAudioOutputIndex(int index) const override { return index == 1; }
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // Main thread. Returns false (pad left as it was) when the clip will not open.
   bool LoadPad(int pad, const std::string& path);
   // Loads the first 16 video files of a folder (alphabetical) into the pads.
   int LoadFolder(const std::string& folder);
   void ClearPad(int pad);
   void ReloadFromPaths();

   // The pad's gate level from the UI or a CV pin. Only edges count, so a held
   // level fires once.
   void SetPadHeld(int pad, bool held, float velocity = 1.0f);

   bool PadLoaded(int pad) const { return !padPath[Clamp(pad)].empty() && mDuration[Clamp(pad)] > 0.0; }
   bool PadPlaying(int pad) const { return mRun[Clamp(pad)].playing; }
   // Where the pad is in its clip, 0..1 of the whole clip (not the window), -1 when idle.
   float PadCursor(int pad) const;
   double PadDuration(int pad) const { return mDuration[Clamp(pad)]; }
   const std::string& PadName(int pad) const { return padName[Clamp(pad)]; }
   bool PadIsHap(int pad) const { return mHap[Clamp(pad)]; }
   // The filmstrip atlas (kThumbs frames side by side, rows as the decoder gives
   // them: draw with v flipped), uploaded on first use; 0 when there is none.
   unsigned int ThumbTexture(int pad);
   int VisibleTop() const { return mTop; }
   int VisibleUnder() const { return mUnder; }
   // The weight the top/under picture is shown with this frame (opacity x envelopes x transition).
   float TopWeight() const { return mWt; }
   float UnderWeight() const { return mWb; }

   // Fixtures: a fixed clock step for the next cooks (< 0: real time again).
   void SetTestClock(double dt) { mTestDt = dt; }

   int padMode[kPads];
   int padSync[kPads];
   int padDiv[kPads];
   float padOpacity[kPads];
   float padSpeed[kPads];
   float padStart[kPads]; // % of the clip
   float padEnd[kPads];   // % of the clip
   float padFadeIn[kPads];  // ms
   float padFadeOut[kPads]; // ms
   int transition = kCrossfade;
   float transitionMs = 200.0f;
   bool holdLast = false;
   bool soundtrack = false;
   int selectedPad = 0;
   NoteCable noteInput;
   std::string padPath[kPads]; // persisted

   float* PadParamPtr(int pad, int k)
   {
      pad = Clamp(pad);
      switch (k)
      {
      case kOpacity: return &padOpacity[pad];
      case kSpeed: return &padSpeed[pad];
      case kStart: return &padStart[pad];
      case kEnd: return &padEnd[pad];
      case kFadeIn: return &padFadeIn[pad];
      default: return &padFadeOut[pad];
      }
   }

   // UI-only, main thread: canvas-space rects of each pad and of the
   // filmstrip, cached while the card draws so a dropped file can be routed to
   // the pad under it; padFlash is the ImGui time of a pad's last hit.
   float padRect[kPads][4] = {};
   float stripRect[4] = {};
   double padFlash[kPads] = {};
   int PadAtCanvas(float x, float y) const
   {
      for (int p = 0; p < kPads; p++)
         if (x >= padRect[p][0] && x <= padRect[p][2] && y >= padRect[p][1] && y <= padRect[p][3])
            return p;
      return -1;
   }
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
   struct PadRun
   {
      bool playing = false;
      double pos = 0.0;      // seconds into the clip
      double relT = -1.0;    // >= 0: seconds into a gate/loop release fade
      bool loopOn = false;   // loop mode toggle state
      bool pending = false;  // synced hit waiting for its grid line
      double pendingBeat = 0.0;
      double lastLine = -1.0; // synced loop: the division last restarted on
      uint64_t order = 0;     // trigger order; the highest playing pad is on top
      float env = 0.0f;       // opacity x pass x release, as of the last step
      uint32_t serial = 0;    // bumped on every new pass or wrap (the soundtrack snaps to it)
   };

   void Engine(double dt);
   void HandleEdge(int pad, bool down);
   void Fire(int pad);
   void StartPass(int pad);
   void Window(int pad, double& s, double& e) const;
   void Composite(int frameId);
   void BuildThumbs(int pad, const std::string& path);
   void DecodeSoundtracks();

   std::unique_ptr<AudioVmpcNode> mAudioNode;
   std::unique_ptr<VideoSourceNode> mPlayer[kPads];
   double mDuration[kPads] = {};
   bool mHap[kPads] = {};
   std::string padName[kPads];
   std::vector<unsigned char> mThumbPx[kPads];
   unsigned int mThumbTex[kPads] = {};
   bool mThumbDirty[kPads] = {};
   bool mSoundDecoded[kPads] = {};

   PadRun mRun[kPads];
   bool mHeld[kPads] = {};
   uint64_t mOrder = 0;
   int mTop = -1, mUnder = -1;
   double mT = 1.0; // transition progress 0..1
   float mWt = 0.0f, mWb = 0.0f;
   bool mHolding = false; // the top pad ended and its last frame is held

   int mLastCookFrame = -1;
   double mLastTransport = 0.0;
   double mLastWall = 0.0;
   bool mWasPlaying = false;
   double mTestDt = -1.0;

   GLUtil::Fbo mOut;
   unsigned int mProg = 0;
   bool mProgTried = false;
   unsigned int mBlackTex = 0;
};
