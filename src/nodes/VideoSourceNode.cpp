#include "VideoSourceNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>

#include "Transport.h"

VideoSourceNode::~VideoSourceNode()
{
   if (mVideo != nullptr)
      Platform::VideoClose(mVideo);
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

void VideoSourceNode::EnsurePlaceholder()
{
   if (mTex != 0)
      return;

   const int kSize = 256;
   std::vector<unsigned char> pixels(kSize * kSize * 4);
   for (int y = 0; y < kSize; y++)
   {
      for (int x = 0; x < kSize; x++)
      {
         bool checker = ((x / 32) + (y / 32)) % 2 == 0;
         unsigned char v = checker ? 70 : 40;
         int i = (y * kSize + x) * 4;
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

bool VideoSourceNode::Open(const std::string& path)
{
   if (path.empty())
      return false;

   std::string error;
   Platform::VideoHandle* handle = Platform::VideoOpen(path, error);
   if (handle == nullptr)
   {
      mLastError = error;
      return false;
   }

   if (mVideo != nullptr)
      Platform::VideoClose(mVideo);
   mVideo = handle;
   mDuration = Platform::VideoDuration(mVideo);
   mLoadedPath = path;
   mLastError.clear();
   mHasPlaceholder = false;
   mPosition = 0.0;
   mLastTransportSeconds = Transport::Instance().Seconds();
   return true;
}

bool VideoSourceNode::OpenViaDialog()
{
   std::string path = Platform::OpenVideoDialog();
   if (path.empty())
      return false; // cancelled
   return Open(path);
}

unsigned int VideoSourceNode::GetOutputTexture()
{
   EnsurePlaceholder();
   return mTex;
}

void VideoSourceNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   EnsurePlaceholder();

   if (mVideo == nullptr)
      return;

   // Advance by the transport's own delta so pausing holds the frame and the
   // speed control retimes playback without touching wall time.
   const double now = Transport::Instance().Seconds();
   double delta = now - mLastTransportSeconds;
   if (delta < 0.0)
      delta = 0.0;
   mLastTransportSeconds = now;
   mPosition += delta * (double)speed;

   if (mDuration > 0.0)
   {
      if (loop)
      {
         mPosition = std::fmod(mPosition, mDuration);
         if (mPosition < 0.0)
            mPosition += mDuration; // fmod keeps the sign of the dividend
      }
      else
      {
         mPosition = std::clamp(mPosition, 0.0, mDuration);
      }
   }
   else
   {
      mPosition = std::max(mPosition, 0.0);
   }

   if (Platform::VideoFrameAt(mVideo, mPosition, mFrame) && !mFrame.empty())
   {
      const int w = Platform::VideoWidth(mVideo);
      const int h = Platform::VideoHeight(mVideo);
      if (w > 0 && h > 0)
      {
         glBindTexture(GL_TEXTURE_2D, mTex);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mFrame.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         mWidth = w;
         mHeight = h;
         mHasPlaceholder = false;
      }
   }
}
