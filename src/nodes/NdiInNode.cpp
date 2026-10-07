#include "NdiInNode.h"

#include <chrono>
#include <cstring>

#include "gl3.h"

namespace
{
   // The frame arrives top-down BGRA; GL's row 0 is the bottom, so flip in the sample.
   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTex;\n"
      "void main() { fragColor = texture(uTex, vec2(vUv.x, 1.0 - vUv.y)); }\n";
}

NdiInNode::NdiInNode()
{
   if (Ndi::Available())
      mThread = std::thread([this] { Worker(); });
}

NdiInNode::~NdiInNode()
{
   mStop = true;
   if (mThread.joinable())
      mThread.join();
   GLUtil::DestroyFbo(mOut);
   if (mProg != 0)
      glDeleteProgram(mProg);
   if (mUploadTex != 0)
      glDeleteTextures(1, &mUploadTex);
   if (mPlaceholderTex != 0)
      glDeleteTextures(1, &mPlaceholderTex);
}

std::vector<std::string> NdiInNode::AvailableSources() const
{
   std::lock_guard<std::mutex> lock(mMutex);
   return mSources;
}

void NdiInNode::SelectSource(const std::string& fullName)
{
   std::lock_guard<std::mutex> lock(mMutex);
   mTarget = fullName;
}

void NdiInNode::Worker()
{
   const NDIlib_v6* api = Ndi::Api();
   if (api == nullptr)
      return;

   NDIlib_find_create_t findDesc;
   NDIlib_find_instance_t finder = api->find_create_v2(&findDesc);
   NDIlib_recv_instance_t recv = nullptr;
   std::string connectedTo;

   while (!mStop)
   {
      std::string target;
      {
         std::lock_guard<std::mutex> lock(mMutex);
         target = mTarget;
      }

      if (finder != nullptr)
      {
         uint32_t n = 0;
         const NDIlib_source_t* srcs = api->find_get_current_sources(finder, &n);
         std::vector<std::string> names;
         names.reserve(n);
         for (uint32_t i = 0; i < n; i++)
            if (srcs[i].p_ndi_name != nullptr)
               names.emplace_back(srcs[i].p_ndi_name);
         std::lock_guard<std::mutex> lock(mMutex);
         mSources = std::move(names);
      }

      if (target != connectedTo)
      {
         if (recv != nullptr)
         {
            api->recv_destroy(recv);
            recv = nullptr;
         }
         mConnected = false;
         connectedTo = target;
         if (!target.empty())
         {
            NDIlib_recv_create_v3_t desc;
            desc.source_to_connect_to.p_ndi_name = target.c_str();
            desc.color_format = NDIlib_recv_color_format_BGRX_BGRA;
            desc.bandwidth = NDIlib_recv_bandwidth_highest;
            desc.allow_video_fields = false;
            desc.p_ndi_recv_name = "Infinite";
            recv = api->recv_create_v3(&desc);
         }
      }

      if (recv == nullptr)
      {
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
         continue;
      }

      NDIlib_video_frame_v2_t video;
      const NDIlib_frame_type_e type = api->recv_capture_v2(recv, &video, nullptr, nullptr, 100);
      mConnected = api->recv_get_no_connections(recv) > 0;
      if (type == NDIlib_frame_type_video && video.p_data != nullptr && video.xres > 0 && video.yres > 0)
      {
         const bool bgra = video.FourCC == NDIlib_FourCC_video_type_BGRA || video.FourCC == NDIlib_FourCC_video_type_BGRX;
         if (bgra)
         {
            std::lock_guard<std::mutex> lock(mMutex);
            const size_t rowBytes = (size_t)video.xres * 4;
            mFrame.resize(rowBytes * video.yres);
            for (int y = 0; y < video.yres; y++)
               std::memcpy(mFrame.data() + y * rowBytes, video.p_data + (size_t)y * video.line_stride_in_bytes, rowBytes);
            mFrameW = video.xres;
            mFrameH = video.yres;
            mFrameSeq++;
         }
         api->recv_free_video_v2(recv, &video);
      }
   }

   if (recv != nullptr)
      api->recv_destroy(recv);
   if (finder != nullptr)
      api->find_destroy(finder);
}

bool NdiInNode::EnsureShader()
{
   if (mProg != 0)
      return true;
   mProg = GLUtil::CompileProgram(kFragSrc);
   return mProg != 0;
}

void NdiInNode::EnsurePlaceholder()
{
   if (mPlaceholderTex != 0)
      return;
   std::vector<unsigned char> px((size_t)kPlaceholder * kPlaceholder * 4, 0);
   for (int y = 0; y < kPlaceholder; y++)
      for (int x = 0; x < kPlaceholder; x++)
      {
         const unsigned char c = (((x / 16) ^ (y / 16)) & 1) ? 38 : 56;
         unsigned char* p = &px[((size_t)y * kPlaceholder + x) * 4];
         p[0] = c; p[1] = (unsigned char)(c + 6); p[2] = (unsigned char)(c + 12); p[3] = 255;
      }
   glGenTextures(1, &mPlaceholderTex);
   glBindTexture(GL_TEXTURE_2D, mPlaceholderTex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kPlaceholder, kPlaceholder, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
   glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int NdiInNode::GetOutputTexture()
{
   if (mOut.tex != 0 && mHaveFrame)
      return GLUtil::FboTexture(mOut);
   EnsurePlaceholder();
   return mPlaceholderTex;
}

void NdiInNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   // Pick the newest frame out of the mailbox; never wait on the worker.
   std::vector<unsigned char> frame;
   int fw = 0, fh = 0;
   {
      std::unique_lock<std::mutex> lock(mMutex, std::try_to_lock);
      if (!lock.owns_lock() || mFrameSeq == mUploadedSeq || mFrameW <= 0)
         return;
      frame.swap(mFrame); // worker re-sizes on its next frame; we own this one now
      fw = mFrameW;
      fh = mFrameH;
      mUploadedSeq = mFrameSeq;
   }

   if (!EnsureShader() || !GLUtil::EnsureFbo(mOut, fw, fh))
      return;

   if (mUploadTex == 0)
   {
      glGenTextures(1, &mUploadTex);
      glBindTexture(GL_TEXTURE_2D, mUploadTex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   }
   glBindTexture(GL_TEXTURE_2D, mUploadTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());

   GLUtil::RunShaderPass(mOut, mProg, [this]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, mUploadTex);
      glUniform1i(glGetUniformLocation(mProg, "uTex"), 0);
   });

   mW = fw;
   mH = fh;
   mHaveFrame = true;
   mRevision++;
}
