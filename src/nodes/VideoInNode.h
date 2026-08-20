#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Platform.h"

// Live camera video source (built-in FaceTime HD camera, external USB webcams, Continuity Camera).
class VideoInNode : public INode
{
public:
   static INode* Create() { return new VideoInNode(); }
   VideoInNode();
   ~VideoInNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   unsigned long long TextureRevision() const override { return mRevision; }
   void CookIfNeeded(int frameId) override;

   bool active = true;
   std::string deviceId;
   int resolution = 0; // 0 = Auto, 1 = 1080p, 2 = 720p, 3 = 480p
   bool mirror = true;

   void VisitParams(ParamVisitor& v) override
   {
      v.Bool("active", active);
      v.Text("deviceId", deviceId);
      v.Int("resolution", resolution);
      v.Bool("mirror", mirror);
   }

   const std::string& LastError() const { return mLastError; }
   bool IsRunning() const;

   void RefreshDevices();
   const std::vector<Platform::CameraDeviceInfo>& AvailableDevices() const { return mCachedDevices; }
   static const std::vector<std::string>& ResolutionNames();

private:
   void EnsurePlaceholder();
   void ReopenCamera();
   void CloseCamera();

   Platform::CameraHandle* mCamera = nullptr;
   unsigned int mTex = 0;
   int mWidth = 0;
   int mHeight = 0;
   bool mHasPlaceholder = false;
   unsigned long long mRevision = 1;
   std::vector<unsigned char> mFrame;
   std::vector<Platform::CameraDeviceInfo> mCachedDevices;
   std::string mLastError;
   int mLastCookFrame = -1;

   bool mLastActive = false;
   std::string mLastDeviceId;
   int mLastResolution = -1;
   bool mLastMirror = true;
};
