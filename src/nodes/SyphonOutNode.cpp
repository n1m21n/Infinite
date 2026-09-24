#include "SyphonOutNode.h"

#include "gl3.h"
#include "BenchMediaIo.h"

namespace
{
   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTex;\n"
      "void main() { fragColor = texture(uTex, vUv); }\n";
}

SyphonOutNode::SyphonOutNode()
{
   serverNameInput = mServerName;
}

SyphonOutNode::~SyphonOutNode()
{
   if (mServer != nullptr)
   {
      Platform::SyphonServerDestroy(mServer);
      mServer = nullptr;
   }
   GLUtil::DestroyFbo(mOut);
   if (mProg != 0)
   {
      glDeleteProgram(mProg);
      mProg = 0;
   }
}

bool SyphonOutNode::EnsureShader()
{
   if (mProg != 0)
      return true;
   mProg = GLUtil::CompileProgram(kFragSrc);
   return mProg != 0;
}

void SyphonOutNode::EnsureServer()
{
   if (mServer == nullptr)
   {
      mServer = Platform::SyphonServerCreate(mServerName);
   }
}

void SyphonOutNode::SetServerName(const std::string& name)
{
   if (mServerName == name)
      return;
   mServerName = name;
   serverNameInput = name;
   if (mServer != nullptr)
   {
      Platform::SyphonServerUpdateName(mServer, mServerName);
   }
}

void SyphonOutNode::Withdraw()
{
   if (mServer != nullptr)
   {
      Platform::SyphonServerDestroy(mServer);
      mServer = nullptr;
   }
}

bool SyphonOutNode::HasClients() const
{
   if (mServer == nullptr)
      return false;
   return Platform::SyphonServerHasClients(mServer);
}

void SyphonOutNode::CookIfNeeded(int frameId)
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
      GLint loc = glGetUniformLocation(mProg, "uTex");
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(loc, 0);
   });

   EnsureServer();
   if (mServer != nullptr)
   {
      const bool bench = Bench::MediaIoEnabled().load(std::memory_order_relaxed);
      const double benchStartMs = bench ? Bench::MediaNowMs() : 0.0;
      Platform::SyphonServerPublish(mServer, GLUtil::FboTexture(mOut), mOut.w, mOut.h, false);
      if (bench)
         mBenchPublishMs.push_back(Bench::MediaNowMs() - benchStartMs);
   }
}
