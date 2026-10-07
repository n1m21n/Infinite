#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "NdiRuntime.h"

// NDI Out node: publishes any connected image as a named NDI video source on
// the local network (OBS, vMix, Resolume, NDI Studio Monitor ...). Video only.
// Each cook blits the input into the node's own FBO (identity pass), reads it
// back through a two-deep PBO ring so the GPU never stalls, and hands the
// previous frame to the NDI async sender. The NDI runtime is loaded
// dynamically (see platform/ndi/NdiRuntime.h); without it the node still
// passes the image through and reports why it is silent.
class NdiOutNode : public INode
{
public:
   static INode* Create() { return new NdiOutNode(); }

   NdiOutNode();
   ~NdiOutNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int slot) const override { return slot == 0 ? "in" : nullptr; }

   const std::string& GetSourceName() const { return mSourceName; }
   void SetSourceName(const std::string& name);

   // Takes the source off the network (called while bypassed) so receivers see
   // it vanish instead of freezing on the last frame.
   void Withdraw();
   bool IsPublishing() const { return mSender != nullptr; }
   int Connections() const;
   int PublishedWidth() const { return mSender != nullptr ? mOut.w : 0; }
   int PublishedHeight() const { return mSender != nullptr ? mOut.h : 0; }

   std::string sourceNameInput; // bound to the ImGui text field

   void VisitParams(ParamVisitor& v) override { v.Text("sourceName", mSourceName); }

private:
   bool EnsureShader();
   bool EnsureSender();
   void ReleasePbos();

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProg = 0;
   NDIlib_send_instance_t mSender = nullptr;
   std::string mSourceName = "Infinite";

   unsigned int mPbo[2] = {0, 0};
   int mPboW = 0, mPboH = 0;
   int mPboWrite = 0;
   bool mPboPrimed = false;
   // Two buffers alternate: NDI's async send keeps reading the last frame
   // until the next send call, so one buffer cannot be rewritten in between.
   std::vector<unsigned char> mSendBuf[2];
   int mSendSlot = 0;
   int mLastCookFrame = -1;
};
