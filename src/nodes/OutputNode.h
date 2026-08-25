#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "AudioCable.h"
#include "GLUtil.h"
#include "Platform.h"
#include "audio/AudioCaptureRing.h"

// Terminal node. Passes its input through into its own FBO (identity pass) so it
// has a real cook/output-texture lifecycle like any other node, and can also
// record the cooked result to an H.264 movie a frame at a time.
class OutputNode : public INode
{
public:
   static INode* Create() { return new OutputNode(); }

   ~OutputNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   AudioCable& AudioInput() { return mAudioInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int slot) const override { return slot == 0 ? "in" : (slot == 1 ? "audio" : nullptr); }
   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &mAudioInput : nullptr; }

   AudioCaptureRing& CaptureRing() { return mCaptureRing; }

   // --- recording ---
   bool StartRecording(const std::string& path);
   void StopRecording();
   bool IsRecording() const { return mRecorder != nullptr; }
   int RecordedFrames() const;
   int PendingFrames() const;
   int DroppedFrames() const;
   const std::string& RecordStatus() const { return mRecordStatus; }

   int recordFps = 30;
   bool includeAudio = false;
   int imageFormat = 0; // 0 = .png, 1 = .jpg
   int videoFormat = 0; // 0 = .mp4, 1 = .mov
   std::string exportImagePath;
   std::string recordVideoPath;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("recordFps", recordFps);
      v.Bool("includeAudio", includeAudio);
      v.Int("imageFormat", imageFormat);
      v.Int("videoFormat", videoFormat);
      v.Text("exportImagePath", exportImagePath);
      v.Text("recordVideoPath", recordVideoPath);
   }

private:
   bool EnsureShader();
   void CaptureFrame();
   void DrainAudioCapture();
   bool EnsureReadbackBuffers();
   void ReleaseReadbackBuffers();
   bool SubmitReadback(int repeatCount);
   bool DrainOneReadback(bool waitForGpu);
   void FlushReadbacks();

   ImageCable mInput;
   AudioCable mAudioInput;
   AudioCaptureRing mCaptureRing;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   Platform::RecorderHandle* mRecorder = nullptr;
   int mRecordW = 0;
   int mRecordH = 0;
   std::string mRecordStatus;

   // Three persistently allocated pixel-pack buffers keep GPU readback two
   // frames behind rendering. glReadPixels therefore never serialises the UI
   // against the current OpenGL frame, while the existing encoder thread owns
   // colour conversion and compression.
   unsigned int mCaptureFbo = 0;
   unsigned int mReadbackPbos[3] {0, 0, 0};
   GLsync mReadbackFences[3] {nullptr, nullptr, nullptr};
   int mReadbackRepeats[3] {0, 0, 0};
   int mReadbackWrite = 0;
   int mReadbackRead = 0;
   int mReadbackPending = 0;
   size_t mReadbackBytes = 0;
   double mRecordStartedSeconds = 0.0;
   int mActiveRecordFps = 30;
   long long mScheduledFrames = 0;
   int mReadbacksCompleted = 0;
   double mReadbackWaitTotalMs = 0.0;
   double mReadbackWaitMaxMs = 0.0;
};
