#include "SyphonInNode.h"

#include "gl3.h"
#include <vector>

namespace
{
   const char* kRectFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2DRect uTex;\n"
      "uniform vec2 uSize;\n"
      "void main() {\n"
      "   fragColor = texture(uTex, vUv * uSize);\n"
      "}\n";
}

SyphonInNode::SyphonInNode()
{
   mClient = Platform::SyphonClientCreate();
   RefreshServers();
}

SyphonInNode::~SyphonInNode()
{
   if (mClient != nullptr)
   {
      Platform::SyphonClientDestroy(mClient);
      mClient = nullptr;
   }
   GLUtil::DestroyFbo(mOut);
   if (mProg != 0)
   {
      glDeleteProgram(mProg);
      mProg = 0;
   }
   if (mPlaceholderTex != 0)
   {
      glDeleteTextures(1, &mPlaceholderTex);
      mPlaceholderTex = 0;
   }
}

void SyphonInNode::RefreshServers()
{
   mCachedServers = Platform::SyphonGetAvailableServers();
   mSelectedIndex = -1;
   for (size_t i = 0; i < mCachedServers.size(); i++)
   {
      const auto& s = mCachedServers[i];
      if (!mTargetUuid.empty() && s.uuid == mTargetUuid)
      {
         mSelectedIndex = (int)i;
         break;
      }
      if (s.appName == mTargetAppName && s.serverName == mTargetServerName)
      {
         mSelectedIndex = (int)i;
         break;
      }
   }

   // Auto-connect if only one server is available and none was selected
   if (mSelectedIndex < 0 && !mCachedServers.empty() && mTargetAppName.empty() && mTargetServerName.empty())
   {
      SelectServer(0);
   }
}

void SyphonInNode::SelectServer(int index)
{
   if (index < 0 || index >= (int)mCachedServers.size())
      return;

   mSelectedIndex = index;
   const auto& s = mCachedServers[(size_t)index];
   Connect(s.appName, s.serverName, s.uuid);
}

void SyphonInNode::Connect(const std::string& appName, const std::string& serverName, const std::string& uuid)
{
   mTargetAppName = appName;
   mTargetServerName = serverName;
   mTargetUuid = uuid;

   if (mClient != nullptr)
   {
      Platform::SyphonClientConnect(mClient, appName, serverName, uuid);
   }
}

bool SyphonInNode::IsConnected() const
{
   if (mClient == nullptr)
      return false;
   return Platform::SyphonClientIsConnected(mClient);
}

bool SyphonInNode::EnsureShader()
{
   if (mProg != 0)
      return true;
   mProg = GLUtil::CompileProgram(kRectFragSrc);
   return mProg != 0;
}

void SyphonInNode::EnsurePlaceholder()
{
   if (mPlaceholderTex != 0)
      return;

   const int w = mPlaceholderW;
   const int h = mPlaceholderH;
   std::vector<unsigned char> pixels((size_t)w * h * 4, 0);
   const int checker = 16;
   for (int y = 0; y < h; y++)
   {
      for (int x = 0; x < w; x++)
      {
         bool dark = ((x / checker) ^ (y / checker)) & 1;
         unsigned char c = dark ? 38 : 56;
         size_t idx = ((size_t)y * w + x) * 4;
         pixels[idx + 0] = c;
         pixels[idx + 1] = (unsigned char)(c + 6);
         pixels[idx + 2] = (unsigned char)(c + 12);
         pixels[idx + 3] = 255;
      }
   }

   glGenTextures(1, &mPlaceholderTex);
   glBindTexture(GL_TEXTURE_2D, mPlaceholderTex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int SyphonInNode::GetOutputTexture()
{
   if (mOut.tex != 0)
      return GLUtil::FboTexture(mOut);

   EnsurePlaceholder();
   return mPlaceholderTex;
}

int SyphonInNode::GetOutputWidth() const
{
   return mWidth > 0 ? mWidth : mPlaceholderW;
}

int SyphonInNode::GetOutputHeight() const
{
   return mHeight > 0 ? mHeight : mPlaceholderH;
}

void SyphonInNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   // Periodically refresh servers every 60 frames
   if (mLastServerScanFrame < 0 || (frameId - mLastServerScanFrame) >= 60)
   {
      mLastServerScanFrame = frameId;
      RefreshServers();
   }

   if (mClient == nullptr)
      return;

   if (!IsConnected() && (!mTargetAppName.empty() || !mTargetServerName.empty() || !mTargetUuid.empty()))
   {
      Platform::SyphonClientConnect(mClient, mTargetAppName, mTargetServerName, mTargetUuid);
   }

   int srcW = 0;
   int srcH = 0;
   unsigned int rectTex = Platform::SyphonClientGetFrameTexture(mClient, srcW, srcH);

   if (rectTex != 0 && srcW > 0 && srcH > 0)
   {
      if (!EnsureShader())
         return;
      if (!GLUtil::EnsureFbo(mOut, srcW, srcH))
         return;

      GLUtil::RunShaderPass(mOut, mProg, [this, rectTex, srcW, srcH]()
      {
         GLint locTex = glGetUniformLocation(mProg, "uTex");
         GLint locSize = glGetUniformLocation(mProg, "uSize");

         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_RECTANGLE, rectTex);
         glUniform1i(locTex, 0);
         glUniform2f(locSize, (float)srcW, (float)srcH);
      });

      mWidth = srcW;
      mHeight = srcH;
      mRevision++;
   }
}
