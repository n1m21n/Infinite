#include "VideoInNode.h"

#include "gl3.h"
#include <algorithm>

const std::vector<std::string>& VideoInNode::ResolutionNames()
{
   static const std::vector<std::string> sNames = {
      "Auto",
      "1080p",
      "720p",
      "480p"
   };
   return sNames;
}

VideoInNode::VideoInNode()
{
   RefreshDevices();
}

VideoInNode::~VideoInNode()
{
   CloseCamera();
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

void VideoInNode::RefreshDevices()
{
   mCachedDevices = Platform::CameraListDevices();
}

void VideoInNode::CloseCamera()
{
   if (mCamera != nullptr)
   {
      Platform::CameraClose(mCamera);
      mCamera = nullptr;
   }
}

void VideoInNode::ReopenCamera()
{
   CloseCamera();
   if (!active)
      return;

   std::string err;
   Platform::CameraResolution res = (resolution >= 0 && resolution < (int)Platform::CameraResolution::Count)
      ? (Platform::CameraResolution)resolution
      : Platform::CameraResolution::Auto;

   mCamera = Platform::CameraOpen(deviceId, res, mirror, err);
   if (mCamera == nullptr)
   {
      mLastError = err;
   }
   else
   {
      mLastError.clear();
   }
}

bool VideoInNode::IsRunning() const
{
   return Platform::CameraIsRunning(mCamera);
}

void VideoInNode::EnsurePlaceholder()
{
   if (mTex != 0)
      return;

   const int kSize = 256;
   std::vector<unsigned char> pixels((size_t)kSize * kSize * 4);
   for (int y = 0; y < kSize; y++)
   {
      for (int x = 0; x < kSize; x++)
      {
         bool checker = ((x / 32) + (y / 32)) % 2 == 0;
         unsigned char v = checker ? 70 : 40;
         size_t i = ((size_t)y * kSize + x) * 4;
         pixels[i + 0] = v;
         pixels[i + 1] = (unsigned char)(v + 10);
         pixels[i + 2] = (unsigned char)(v + 30);
         pixels[i + 3] = 255;
      }
   }

   glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mWidth = kSize;
   mHeight = kSize;
   mHasPlaceholder = true;
}

unsigned int VideoInNode::GetOutputTexture()
{
   EnsurePlaceholder();
   return mTex;
}

void VideoInNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   EnsurePlaceholder();

   const bool configChanged = (active != mLastActive ||
                               deviceId != mLastDeviceId ||
                               resolution != mLastResolution ||
                               mirror != mLastMirror);

   if (configChanged)
   {
      if (!active)
      {
         CloseCamera();
      }
      else if (mCamera != nullptr && deviceId == mLastDeviceId)
      {
         if (mirror != mLastMirror)
            Platform::CameraSetMirror(mCamera, mirror);
         if (resolution != mLastResolution)
         {
            Platform::CameraResolution res = (resolution >= 0 && resolution < (int)Platform::CameraResolution::Count)
               ? (Platform::CameraResolution)resolution
               : Platform::CameraResolution::Auto;
            Platform::CameraSetResolution(mCamera, res);
         }
      }
      else
      {
         ReopenCamera();
      }

      mLastActive = active;
      mLastDeviceId = deviceId;
      mLastResolution = resolution;
      mLastMirror = mirror;
   }

   if (mCamera == nullptr && active && !mLastError.empty())
   {
      // Retry periodically if waiting for permission
      static int sRetryCounter = 0;
      if (++sRetryCounter % 60 == 0)
      {
         ReopenCamera();
      }
   }

   if (mCamera != nullptr)
   {
      int w = 0;
      int h = 0;
      unsigned long long frameSeq = 0;
      if (Platform::CameraReadFrame(mCamera, mFrame, w, h, frameSeq) && !mFrame.empty())
      {
         if (w > 0 && h > 0)
         {
            glBindTexture(GL_TEXTURE_2D, mTex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mFrame.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            mWidth = w;
            mHeight = h;
            mHasPlaceholder = false;
            mRevision = NextTextureRevision();
            mLastError.clear();
         }
      }
   }
}
