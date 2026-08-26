#pragma once

#include <string>
#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Platform.h"

// Syphon Out node: broadcasts any connected video, 3D render, or visual shader
// to other macOS apps (Resolume, OBS, TouchDesigner, MadMapper) in real-time
// via zero-copy GPU memory sharing (IOSurface). On Windows, the same
// Platform::Syphon* surface is backed by Spout2 instead (see
// src/platform/win/PlatformWinSyphon.cpp), so this node works unchanged there
// too, publishing to a Spout sender rather than a Syphon server.
class SyphonOutNode : public INode
{
public:
   static INode* Create() { return new SyphonOutNode(); }

   SyphonOutNode();
   ~SyphonOutNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int slot) const override { return slot == 0 ? "in" : nullptr; }

   const std::string& GetServerName() const { return mServerName; }
   void SetServerName(const std::string& name);

   bool HasClients() const;
   int PublishedWidth() const { return mOut.w; }
   int PublishedHeight() const { return mOut.h; }

   std::string serverNameInput; // Bound to ImGui input

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("serverName", mServerName);
   }

private:
   bool EnsureShader();
   void EnsureServer();

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProg = 0;
   Platform::SyphonServerHandle* mServer = nullptr;
   std::string mServerName = "Infinite Output";
   int mLastCookFrame = -1;
};
