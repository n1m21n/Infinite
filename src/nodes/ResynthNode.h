#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Iterative image resynthesizer, the visual counterpart of a sampler's
// analyse -> mutate -> resynthesize loop. Each generation reads the previous
// generation (ping-ponged FBOs) rather than the source, so the image drifts
// away from the original over time; `chaos` sets how violently.
//
// The FX pad is a 2D control surface: its X/Y position cross-fades a set of
// mutation weights that are re-rolled by Randomise. The pad position can be
// recorded as a path and played back or looped, so a performance on the pad
// becomes part of the patch.
class ResynthNode : public INode
{
public:
   static INode* Create() { return new ResynthNode(); }
   static const std::vector<std::string>& ModeNames();

   // The eight mutation operators the pad can sweep between. Each pad corner is
   // assigned one of these, and the orb's position bilinearly blends the four
   // corner amounts - so the pad shows you exactly what it is mixing.
   static const std::vector<std::string>& EffectNames();
   static const int kEffectCount = 8;
   static const int kCorners = 4; // 0=BL, 1=BR, 2=TL, 3=TR

   int cornerEffect[kCorners] = { 0, 4, 5, 7 };
   float cornerAmount[kCorners] = { 1.0f, 1.0f, 1.0f, 1.0f };
   const char* CornerLabel(int corner) const;

   ~ResynthNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mBuffers[mFront].w; }
   int GetOutputHeight() const override { return mBuffers[mFront].h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }

   // --- generation control ---
   void StepOnce() { mPendingSteps++; }
   void Reset() { mNeedsReset = true; mGeneration = 0; }
   void Randomise();
   int Generation() const { return mGeneration; }

   // --- FX pad path recording ---
   struct PadPoint
   {
      float x = 0.5f;
      float y = 0.5f;
      double beat = 0.0;
   };
   void StartRecording();
   void StopRecording();
   bool IsRecordingPath() const { return mRecordingPath; }
   void PlayPath();
   void StopPath();
   bool IsPlayingPath() const { return mPlayingPath; }
   void ClearPath();
   const std::vector<PadPoint>& Path() const { return mPath; }

   // Pad position, 0..1. Driven by the user, or by path playback.
   float padX = 0.5f;
   float padY = 0.5f;

   int mode = 0;
   float chaos = 0.35f;
   float mutation = 0.4f;
   float feedback = 0.85f;   // how much of the previous generation survives
   float sourcePull = 0.12f; // how strongly it is dragged back to the original
   float stepsPerBeat = 1.0f;
   bool autoIterate = false;
   bool loopPath = true;
   float seed = 0.0f;

private:
   bool EnsureShader();
   void RunGeneration(unsigned int srcTex, int w, int h);
   void UpdatePathPlayback();

   ImageCable mInput;
   GLUtil::Fbo mBuffers[2];
   int mFront = 0;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   int mGeneration = 0;
   int mPendingSteps = 0;
   bool mNeedsReset = true;
   double mLastStepBeat = 0.0;



   std::vector<PadPoint> mPath;
   bool mRecordingPath = false;
   bool mPlayingPath = false;
   double mPathStartBeat = 0.0;
};
