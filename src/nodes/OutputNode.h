#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Platform.h"

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
   INode* BypassSource() override { return mInput.GetSource(); }

   // --- recording ---
   bool StartRecording(const std::string& path);
   void StopRecording();
   bool IsRecording() const { return mRecorder != nullptr; }
   int RecordedFrames() const;
   const std::string& RecordStatus() const { return mRecordStatus; }

   int recordFps = 30;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("recordFps", recordFps);
   }

private:
   bool EnsureShader();
   void CaptureFrame();

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   Platform::RecorderHandle* mRecorder = nullptr;
   std::vector<unsigned char> mReadback;
   int mRecordW = 0;
   int mRecordH = 0;
   std::string mRecordStatus;
};
