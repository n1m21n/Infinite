#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// A paintable canvas. Strokes accumulate into a persistent FBO, so painting is
// destructive-to-the-buffer but the node itself stays a normal graph citizen -
// it can be fed by an input (paint over an image) and feeds anything downstream.
//
// Brush tips are generated procedurally rather than shipped as image packs:
// downloadable brush sets are almost always licensed, and a distance-field tip
// scales to any size without resampling artefacts.
class DrawNode : public INode
{
public:
   static INode* Create() { return new DrawNode(); }
   static const std::vector<std::string>& BrushNames();

   ~DrawNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mComposite); }
   int GetOutputWidth() const override { return mComposite.w; }
   int GetOutputHeight() const override { return mComposite.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }

   // Called by the editor while the user drags on the node's preview. Coordinates
   // are normalised 0..1 with the origin bottom-left, matching texture space.
   void BeginStroke(float x, float y);
   void ContinueStroke(float x, float y);
   void EndStroke();
   void ClearCanvas() { mNeedsClear = true; }

   bool HasStrokes() const { return mStrokeCount > 0; }

   // --- stroke recording -------------------------------------------------
   // Every stamp is logged with the brush settings in force at the time and a
   // transport timestamp, so replay reproduces the drawing exactly, including
   // colour and brush changes mid-drawing.
   void StartRecording();
   void StopRecording();
   void PlayRecording();
   void StopPlayback();
   void ClearRecording();
   bool IsRecordingStrokes() const { return mRecording; }
   bool IsPlayingBack() const { return mPlaying; }
   size_t RecordedStamps() const { return mRecorded.size(); }
   double RecordedLength() const { return mRecorded.empty() ? 0.0 : mRecorded.back().beat; }
   double PlayheadBeats() const { return mPlayhead; }

   bool loopPlayback = true;
   float playSpeed = 1.0f;

   int brush = 0;
   float brushSize = 0.05f;
   float opacity = 0.8f;
   float hardness = 0.5f;
   float spacing = 0.25f;   // as a fraction of the brush size
   float jitter = 0.0f;
   bool eraser = false;
   float color[3] = { 1.0f, 1.0f, 1.0f };
   float canvasWidth = 1024.0f;
   float canvasHeight = 1024.0f;

   // The painted canvas and stroke recording are runtime state, not settings -
   // like a video frame, they are not reasonable to round-trip through a text
   // patch file. Only the brush configuration is persisted.
   void VisitParams(ParamVisitor& v) override
   {
      v.Bool("loopPlayback", loopPlayback); v.Float("playSpeed", playSpeed);
      v.Int("brush", brush); v.Float("brushSize", brushSize);
      v.Float("opacity", opacity); v.Float("hardness", hardness);
      v.Float("spacing", spacing); v.Float("jitter", jitter);
      v.Bool("eraser", eraser); v.Color("color", color);
      v.Float("canvasWidth", canvasWidth); v.Float("canvasHeight", canvasHeight);
   }

private:
   struct Stamp
   {
      float x = 0.0f;
      float y = 0.0f;
      float seed = 0.0f;
      // Snapshot of the brush at stamp time. Replay must not use the *current*
      // brush, or changing a slider would rewrite history.
      float size = 0.05f;
      float opacity = 0.8f;
      float hardness = 0.5f;
      int brush = 0;
      bool erase = false;
      float color[3] = { 1.0f, 1.0f, 1.0f };
   };

   struct RecordedStamp
   {
      Stamp stamp;
      double beat = 0.0;
   };

   Stamp MakeStamp(float x, float y, float seed) const;

   bool EnsureShaders();
   void FlushStamps();

   ImageCable mInput;
   GLUtil::Fbo mCanvas;     // the painted layer alone
   GLUtil::Fbo mScratch;    // ping-pong target while stamping
   GLUtil::Fbo mComposite;  // input with the painted layer over it
   unsigned int mStampProgram = 0;
   unsigned int mCompositeProgram = 0;
   bool mShaderTried = false;
   bool mNeedsClear = true;
   int mLastCookFrame = -1;

   std::vector<Stamp> mPending;
   bool mStrokeActive = false;
   float mLastX = 0.0f;
   float mLastY = 0.0f;
   int mStrokeCount = 0;
   float mSeedCounter = 0.0f;

   std::vector<RecordedStamp> mRecorded;
   bool mRecording = false;
   bool mPlaying = false;
   double mRecordStartBeat = 0.0;
   double mPlayStartBeat = 0.0;
   double mPlayhead = 0.0;
   size_t mPlayIndex = 0;
};
