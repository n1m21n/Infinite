#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "GLUtil.h"
#include "INode.h"
#include "NoteCable.h"
#include "Platform.h"

class AudioVmpcNode;

// Infinite-Turbo: VMPC, the MPC for video clips (a VJ clip launcher).
//
// 16 pads, each holding one video file with its own play mode:
//   One shot - a hit plays the clip once (a new hit restarts it)
//   Gate     - plays (looping) while the pad is held, stops on release
//   Loop     - a hit toggles a looping playback on/off
// plus trim in/out and speed (negative = reverse). Pads are hit with the
// mouse, with each pad's CV pin (a MIDI controller via a MIDI CC / Note
// modulator, or MIDI learn), or with the note input (note = base note + pad).
//
// Outputs: "video" (the clip of the last pad hit - monophonic, like a
// launcher column; transparent when nothing plays, or the last frame with
// `holdLastFrame`) and "audio" (that clip's own soundtrack, when the file has
// one and `playAudio` is on - wire it to an Audio Out or a mixer). With audio,
// the soundtrack is the clock and the picture follows it.
//
// Every pad keeps its decoder open (and its soundtrack decoded) once loaded,
// so a hit only seeks. Notes arrive on the audio thread; the audio object
// forwards them to the main thread through a lock-free ring.
class VmpcNode : public INode, public IAudioSource
{
public:
   static constexpr int kPads = 16;
   enum PadMode { kOneShot = 0, kGate = 1, kLoopToggle = 2 };

   static INode* Create() { return new VmpcNode(); }
   VmpcNode();
   ~VmpcNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   unsigned long long TextureRevision() const override { return mRevision; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   int OutputCount() const override { return 2; }
   const char* OutputLabel(int index) const override { return index == 1 ? "audio" : "video"; }
   AudioNode* GetAudioNode() override;
   bool RequiresAudioProcessing() const override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // Main thread. Returns false (pad left as it was) when the file won't open.
   bool LoadPad(int pad, const std::string& path);
   // Loads the first 16 video files of a folder (alphabetical) into the pads.
   int LoadFolder(const std::string& folder);
   void ClearPad(int pad);
   void ReloadFromPaths();

   // A pad press (down=true) or release, from the UI, CV or notes.
   void PadEvent(int pad, bool down, float velocity = 1.0f);
   void StopAll();

   bool PadLoaded(int pad) const { return mPads[Clamp(pad)].video != nullptr; }
   bool PadHasAudio(int pad) const { return mPads[Clamp(pad)].hasAudio; }
   bool PadPlaying(int pad) const { return mPlaying && mActive == pad; }
   const std::string& PadName(int pad) const { return mPads[Clamp(pad)].name; }
   const std::string& PadStatus(int pad) const { return mPads[Clamp(pad)].status; }
   double PadDuration(int pad) const { return mPads[Clamp(pad)].duration; }
   int PadWidth(int pad) const { return mPads[Clamp(pad)].w; }
   int PadHeight(int pad) const { return mPads[Clamp(pad)].h; }
   int ActivePad() const { return mPlaying ? mActive : -1; }
   // 0..1 of the whole clip, for the UI.
   float ActivePosition01() const;

   int padMode[kPads];
   float padStart[kPads]; // trim in, 0..1 of the clip
   float padEnd[kPads];   // trim out, 0..1 of the clip
   float padSpeed[kPads]; // -4..4, negative plays backwards
   int baseNote = 36;
   bool holdLastFrame = false;
   bool playAudio = true;
   float audioVolume = 1.0f;
   int selectedPad = 0;
   // Turbo 0.50: transition when a hit switches to another pad's clip.
   // 0 = Cut (the old behaviour), 1.. = ImageTransition styles + 1.
   int transitionStyle = 0;
   float transitionTime = 0.5f; // seconds
   static const std::vector<std::string>& TransitionNames();
   NoteCable noteInput;

   // UI edge detection for the pad buttons (main thread only).
   bool uiHeld[kPads] = {};

   // Persisted file paths (VisitParams).
   std::string padPath[kPads];

private:
   struct Pad
   {
      Platform::VideoHandle* video = nullptr;
      double duration = 0.0;
      int w = 0;
      int h = 0;
      bool hasAudio = false;
      std::string name;
      std::string status = "empty";
   };

   static int Clamp(int pad) { return pad < 0 ? 0 : (pad >= kPads ? kPads - 1 : pad); }
   void StartPad(int pad);
   void StopPlayback();
   void EnsureTextures();
   void RangeSeconds(int pad, double& from, double& to) const;
   bool AudioDriven() const;
   void CookClip();
   void RenderTransition();
   void CancelTransition();

   Pad mPads[kPads];
   std::unique_ptr<AudioVmpcNode> mAudio;
   unsigned int mTex = 0;
   unsigned int mClearTex = 0; // 1x1 transparent, shown when nothing plays
   int mTexW = 0;
   int mTexH = 0;
   int mWidth = 1280;
   int mHeight = 720;
   unsigned long long mRevision = 1;
   std::vector<unsigned char> mFrame;
   int mActive = -1;
   bool mPlaying = false;
   bool mShowClip = false;   // mTex holds a frame worth showing
   bool mAudioStarted = false; // the current take was started on the audio clock
   double mPos = 0.0;        // seconds, active pad
   uint64_t mLastAudioBlocks = 0;
   uint32_t mLastEndSerial = 0;
   std::chrono::steady_clock::time_point mLastTick;
   bool mHaveTick = false;
   int mLastCookFrame = -1;

   // Turbo 0.50: clip transitions. On a switch the outgoing frame's texture
   // is swapped (not copied) into mPrevTex and the incoming clip decodes into
   // mTex as usual; the blend into mOut starts on the incoming clip's first
   // frame, so the decode never waits on the transition. Both textures are
   // node-owned: clearing or reloading a pad mid-transition frees only the
   // pad's decoder, never a texture the blend is reading.
   enum TransState { kTransNone = 0, kTransWaiting = 1, kTransRunning = 2 };
   int mTransState = kTransNone;
   int mTransStyle = 0; // ImageTransition style of the running transition
   double mTransSeconds = 0.0;
   std::chrono::steady_clock::time_point mTransStart;
   unsigned int mPrevTex = 0;
   int mPrevW = 0; // allocated size of mPrevTex (= the outgoing frame's)
   int mPrevH = 0;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
};
