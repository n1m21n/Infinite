#pragma once

#include <string>
#include <vector>
#include "INode.h"
#include "GLUtil.h"
#include "Platform.h"

// Syphon In node: ingests real-time video/graphics from any active Syphon server
// on macOS (Resolume, OBS, TouchDesigner, MadMapper, Unreal, Unity) via zero-copy
// GPU texture sharing (IOSurface) into the node graph. On Windows, the same
// Platform::Syphon* surface is backed by Spout2 instead (see
// src/platform/win/PlatformWinSyphon.cpp), so this node works unchanged there
// too, connecting to Spout senders rather than Syphon servers.
class SyphonInNode : public INode
{
public:
   static INode* Create() { return new SyphonInNode(); }

   SyphonInNode();
   ~SyphonInNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override;
   int GetOutputHeight() const override;
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }
   bool IsHardwareDriven() const override { return true; }

   // Server discovery & connection
   void RefreshServers();
   const std::vector<Platform::SyphonServerInfo>& AvailableServers() const { return mCachedServers; }
   int SelectedServerIndex() const { return mSelectedIndex; }
   void SelectServer(int index);
   void Connect(const std::string& appName, const std::string& serverName, const std::string& uuid);

   bool IsConnected() const;
   const std::string& ConnectedAppName() const { return mTargetAppName; }
   const std::string& ConnectedServerName() const { return mTargetServerName; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("appName", mTargetAppName);
      v.Text("serverName", mTargetServerName);
      v.Text("uuid", mTargetUuid);
   }

private:
   bool EnsureShader();
   void EnsurePlaceholder();

   GLUtil::Fbo mOut;
   unsigned int mProg = 0;
   unsigned int mPlaceholderTex = 0;
   int mPlaceholderW = 256;
   int mPlaceholderH = 256;

   Platform::SyphonClientHandle* mClient = nullptr;
   std::vector<Platform::SyphonServerInfo> mCachedServers;
   int mSelectedIndex = -1;

   std::string mTargetAppName;
   std::string mTargetServerName;
   std::string mTargetUuid;

   int mWidth = 0;
   int mHeight = 0;
   unsigned long long mRevision = 0;
   int mLastServerScanFrame = -1;
   int mLastCookFrame = -1;
};
