#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/AudioCable.h"
#include "core/GLUtil.h"
#include "core/INode.h"
#include "core/NoteCable.h"
#include "platform/Platform.h"

class AudioClipMatrixNode;
class AudioClipMatrixTap;

// Infinite-Turbo 0.44: Clip Matrix - a session / clip launcher in the spirit
// of Ableton Live's Session View, for audio, video and image clips.
//
//   rows    = tracks (default 4). One clip plays per row at a time.
//   columns = scenes (default 8). A scene button launches its whole column.
//
// Every launch, stop and scene is quantized on the AUDIO THREAD to the next
// grid line of the transport (None, 1/16 .. 4 bars; global, or per clip),
// sample-accurately inside the block, so timing never depends on the UI
// frame rate. "None" fires on the next audio block (free play).
//
// Clip modes: Loop (default), Play once, Gate (plays while held).
// Audio clips are timeline-locked: the file position is a function of the
// beat since launch, optionally synced to the project tempo (stretched,
// pitch kept) with per-clip pitch. Video clips follow the same clock (their
// soundtrack plays like an audio clip). Images just show.
//
// Follow actions (per clip): after N bars (or the clip's length), do action A
// or (with the given chance) action B: stop, again, next, previous, first,
// last, any, other. Scenes can carry a tempo and a time signature that
// apply the moment the scene starts.
//
// Outputs: 0 = master video (rows composited, row 1 in front), 1 = master
// audio. A "Clip Matrix Out" node wired from the audio output splits every
// row into its own video + audio outputs (like MPC / MPC Out).
//
// Recording to the Arrangement Timeline (REC > ARR) is driven from main.cpp
// through PopEvent: every launch / loop pass / stop becomes a timeline clip.
class ClipMatrixNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxRows = 16;
   static constexpr int kMaxCols = 16;
   enum Kind { kEmpty = 0, kAudio = 1, kVideo = 2, kImage = 3 };
   enum Mode { kLoop = 0, kOnce = 1, kGate = 2 };
   enum Follow { kFollowNone = 0, kFollowStop, kFollowAgain, kFollowNext, kFollowPrev, kFollowFirst,
                 kFollowLast, kFollowAny, kFollowOther, kFollowCount };
   static constexpr int kQuantCount = 8; // None, 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars
   static const char* QuantName(int q);
   static double QuantBeats(int q, double beatsPerBar);
   static const char* FollowName(int f);

   struct Cell
   {
      std::string path;
      std::string name;
      int kind = kEmpty;
      int mode = kLoop;
      int quant = -1; // -1 = the matrix's global quantize
      float gainDb = 0.0f;
      float pitch = 0.0f; // semitones, audio only, time preserving
      bool sync = false;  // stretch to the project tempo
      float sampleBpm = 120.0f;
      float origBpm = 0.0f; // detected, display only
      int color = 0;        // palette index, 0 = by kind
      bool followOn = false;
      int followA = kFollowNext;
      int followB = kFollowAgain;
      float followChanceB = 0.0f; // 0..1
      float followBars = 0.0f;    // 0 = the clip's own length
      // runtime (not saved)
      double duration = 0.0;
      bool hasAudio = false;
      Platform::VideoHandle* video = nullptr;
      unsigned int imageTex = 0;
      int w = 0, h = 0;
      std::string status;
   };

   struct Scene
   {
      std::string name;
      float tempo = 0.0f; // 0 = leave the tempo alone
      int sigNum = 0;     // 0 = leave the time signature alone
      int sigDen = 4;
   };

   struct Row
   {
      std::string name;
      float gainDb = 0.0f;
      bool mute = false;
      int blendMode = 0;
      float opacity = 1.0f;
   };

   // Audio -> main events (recording to the arrangement, UI flashes).
   struct Event
   {
      enum Type { kStart = 0, kWrap = 1, kStop = 2 };
      int type = kStart;
      int row = 0;
      int col = -1;
      double beat = 0.0;
      double sourceSeconds = 0.0;
   };

   // Tempo detection for new audio clips (main.cpp's estimator, set at start).
   using BpmEstimator = float (*)(const Platform::SampleBuffer*, const std::string&, float, bool*);
   static BpmEstimator sEstimateBpm;

   static INode* Create() { return new ClipMatrixNode(); }
   ClipMatrixNode();
   ~ClipMatrixNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return width; }
   int GetOutputHeight() const override { return height; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   int OutputCount() const override { return 2; }
   const char* OutputLabel(int index) const override { return index == 1 ? "audio" : "video"; }
   AudioNode* GetAudioNode() override;
   bool RequiresAudioProcessing() const override { return true; }
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // ---- main thread: content ----------------------------------------------
   bool LoadCell(int row, int col, const std::string& path);
   void ClearCell(int row, int col);
   void ReloadFromPaths(); // after a patch load / undo
   void SwapCells(int r0, int c0, int r1, int c1);
   void CopyCell(int r0, int c0, int r1, int c1);
   Cell& CellAt(int row, int col) { return mCells[Clamp(row, kMaxRows)][Clamp(col, kMaxCols)]; }
   const Cell& CellAt(int row, int col) const { return mCells[Clamp(row, kMaxRows)][Clamp(col, kMaxCols)]; }
   Scene& SceneAt(int col) { return scenes[Clamp(col, kMaxCols)]; }
   Row& RowAt(int row) { return rowParams[Clamp(row, kMaxRows)]; }

   // ---- main thread: performance -------------------------------------------
   void Launch(int row, int col);   // press (a gate clip plays until Release)
   void Release(int row, int col);  // gate release
   void StopRow(int row);
   void LaunchScene(int col);
   void StopAll();

   // Published by the audio thread (refreshed each CookIfNeeded).
   int PlayingCol(int row) const { return mPlaying[Clamp(row, kMaxRows)]; }
   int QueuedCol(int row) const { return mQueued[Clamp(row, kMaxRows)]; } // -1 none, -2 stop queued
   float Progress(int row) const { return mProgress[Clamp(row, kMaxRows)]; }
   float RowLevel(int row) const { return mRowPeak[Clamp(row, kMaxRows)]; }
   double LaunchBeat(int row) const { return mLaunchBeat[Clamp(row, kMaxRows)]; }
   bool PopEvent(Event& out);

   // For Clip Matrix Out (main thread).
   unsigned int RowTexture(int row);
   int RowTextureWidth(int row) const;
   int RowTextureHeight(int row) const;
   AudioClipMatrixNode* AudioHalf() { return mAudio.get(); }

   int rows = 4;
   int cols = 8;
   int quantize = 5; // 1 bar
   int baseNote = 36;
   float volume = 1.0f;
   int width = 1920;
   int height = 1080;
   bool recordToArrangement = false;
   Scene scenes[kMaxCols];
   Row rowParams[kMaxRows];
   NoteCable noteInput;

   // UI state (main thread only).
   bool uiHeld[kMaxRows][kMaxCols] = {};
   float cellRect[kMaxRows][kMaxCols][4] = {}; // canvas-space x0,y0,x1,y1 (file drops)
   int selRow = 0, selCol = 0;

private:
   static int Clamp(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }
   void PushCellParams(int row, int col);
   void ReleaseCellMedia(Cell& c);
   void UpdateRowVideo(int row, double beat);
   void EnsureCompose();
   void Compose();

   Cell mCells[kMaxRows][kMaxCols];
   std::unique_ptr<AudioClipMatrixNode> mAudio;
   int mLastCookFrame = -1;
   int mPlaying[kMaxRows];
   int mQueued[kMaxRows];
   float mProgress[kMaxRows] = {};
   float mRowPeak[kMaxRows] = {};
   double mLaunchBeat[kMaxRows] = {};

   // Video: one texture per row (the playing clip's frame) + the composite.
   unsigned int mRowTex[kMaxRows] = {};
   int mRowTexW[kMaxRows] = {}, mRowTexH[kMaxRows] = {};
   unsigned int mRowShowTex[kMaxRows] = {}; // what the row shows this frame (0 = nothing)
   int mRowShowW[kMaxRows] = {}, mRowShowH[kMaxRows] = {};
   std::vector<unsigned char> mFrame;
   GLUtil::Fbo mScratch[2];
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mProgramTried = false;
   unsigned int mClearTex = 0;
};

// Splits a Clip Matrix into one video + one audio output per row. Wire the
// matrix's audio output into its input.
class ClipMatrixOutNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new ClipMatrixOutNode(); }
   ClipMatrixOutNode();
   ~ClipMatrixOutNode() override;

   unsigned int GetOutputTexture() override { return GetOutputTextureAt(0); }
   int GetOutputWidth() const override;
   int GetOutputHeight() const override;
   unsigned int GetOutputTextureAt(int output) override;
   int GetOutputWidthAt(int output) const override;
   int GetOutputHeightAt(int output) const override;
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return 2 * Rows(); }
   const char* OutputLabel(int index) const override;
   AudioNode* GetAudioNode() override;
   int ExtraAudioNodeCount() const override { return ClipMatrixNode::kMaxRows - 1; }
   AudioNode* ExtraAudioNode(int i) override;
   AudioNode* AudioNodeForOutput(int output) override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "matrix" : nullptr; }
   void ResolveAudioTaps() override;

   ClipMatrixNode* Matrix() const;
   int Rows() const;
   AudioCable input;

private:
   std::unique_ptr<AudioClipMatrixTap> mTaps[ClipMatrixNode::kMaxRows];
   int mLastCookFrame = -1;
   std::string mLabels[2 * ClipMatrixNode::kMaxRows];
};
