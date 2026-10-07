#include "NdiOutNode.h"

#include "gl3.h"

namespace
{
   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTex;\n"
      "void main() { fragColor = texture(uTex, vUv); }\n";
}

NdiOutNode::NdiOutNode()
{
   sourceNameInput = mSourceName;
}

NdiOutNode::~NdiOutNode()
{
   Withdraw();
   ReleasePbos();
   GLUtil::DestroyFbo(mOut);
   if (mProg != 0)
   {
      glDeleteProgram(mProg);
      mProg = 0;
   }
}

void NdiOutNode::ReleasePbos()
{
   if (mPbo[0] != 0 || mPbo[1] != 0)
      glDeleteBuffers(2, mPbo);
   mPbo[0] = mPbo[1] = 0;
   mPboW = mPboH = 0;
   mPboPrimed = false;
}

bool NdiOutNode::EnsureShader()
{
   if (mProg != 0)
      return true;
   mProg = GLUtil::CompileProgram(kFragSrc);
   return mProg != 0;
}

bool NdiOutNode::EnsureSender()
{
   if (mSender != nullptr)
      return true;
   const NDIlib_v6* api = Ndi::Api();
   if (api == nullptr || mSourceName.empty())
      return false;
   NDIlib_send_create_t desc;
   desc.p_ndi_name = mSourceName.c_str();
   desc.p_groups = nullptr;
   desc.clock_video = false; // the patch's own frame rate paces us; never block a cook
   desc.clock_audio = false;
   mSender = api->send_create(&desc);
   return mSender != nullptr;
}

void NdiOutNode::SetSourceName(const std::string& name)
{
   if (mSourceName == name)
      return;
   mSourceName = name;
   sourceNameInput = name;
   Withdraw(); // NDI names are fixed at creation; the next cook republishes under the new one
}

void NdiOutNode::Withdraw()
{
   if (mSender != nullptr)
   {
      if (const NDIlib_v6* api = Ndi::Api())
         api->send_destroy(mSender); // flushes any async frame still in flight
      mSender = nullptr;
   }
   mPboPrimed = false;
}

int NdiOutNode::Connections() const
{
   const NDIlib_v6* api = Ndi::Api();
   if (api == nullptr || mSender == nullptr)
      return 0;
   return api->send_get_no_connections(mSender, 0);
}

void NdiOutNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int tex = mInput.Pull(frameId);
   if (tex == 0 || mInput.Width() <= 0 || mInput.Height() <= 0)
      return;

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, mInput.Width(), mInput.Height()))
      return;

   GLUtil::RunShaderPass(mOut, mProg, [this, tex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(glGetUniformLocation(mProg, "uTex"), 0);
   });

   if (!Ndi::Available() || !EnsureSender())
      return;

   const int w = mOut.w, h = mOut.h;
   const size_t bytes = (size_t)w * h * 4;
   if (mPboW != w || mPboH != h)
   {
      ReleasePbos();
      glGenBuffers(2, mPbo);
      for (int i = 0; i < 2; i++)
      {
         glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[i]);
         glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STREAM_READ);
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      mPboW = w;
      mPboH = h;
      mPboWrite = 0;
      mPboPrimed = false;
   }

   GLint prevFbo = 0;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevFbo);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, mOut.fbo);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[mPboWrite]);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);

   // Send the frame read last cook (one frame of latency, zero GPU stall).
   if (mPboPrimed)
   {
      const int readIdx = 1 - mPboWrite;
      glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[readIdx]);
      const void* mapped = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
      if (mapped != nullptr)
      {
         std::vector<unsigned char>& buf = mSendBuf[mSendSlot];
         buf.resize(bytes);
         Ndi::CopyRowsFlipped((const unsigned char*)mapped, w * 4, buf.data(), w * 4, w * 4, h);
         glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

         NDIlib_video_frame_v2_t frame;
         frame.xres = w;
         frame.yres = h;
         frame.FourCC = NDIlib_FourCC_video_type_BGRA;
         frame.frame_rate_N = 60;
         frame.frame_rate_D = 1;
         frame.picture_aspect_ratio = (float)w / (float)h;
         frame.frame_format_type = NDIlib_frame_format_type_progressive;
         frame.timecode = NDIlib_send_timecode_synthesize;
         frame.p_data = buf.data();
         frame.line_stride_in_bytes = w * 4;
         Ndi::Api()->send_send_video_async_v2(mSender, &frame);
         mSendSlot = 1 - mSendSlot;
      }
   }
   mPboPrimed = true;
   mPboWrite = 1 - mPboWrite;

   glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevFbo);
}
