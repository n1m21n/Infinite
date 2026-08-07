#include "OutputNode.h"

#include <OpenGL/gl3.h>

#include "AnalyzeNodes.h"

namespace
{
   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTex;\n"
      "void main() { fragColor = texture(uTex, vUv); }\n";
}

OutputNode::~OutputNode()
{
   if (mRecorder != nullptr)
   {
      std::string ignored;
      Platform::RecorderStop(mRecorder, ignored);
      mRecorder = nullptr;
   }
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool OutputNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

bool OutputNode::StartRecording(const std::string& path)
{
   if (mRecorder != nullptr)
      return false;
   if (mOut.w <= 1 || mOut.h <= 1)
   {
      mRecordStatus = "nothing connected to record";
      return false;
   }

   // H.264 needs even dimensions; lock the size for the whole take so a
   // mid-recording resolution change can't corrupt the stream.
   mRecordW = mOut.w & ~1;
   mRecordH = mOut.h & ~1;

   // The audio file is read straight off disk by the recorder, independent of
   // whatever the node's own playback/loop state is doing, so a paused or
   // muted Audio File source still gets recorded correctly.
   std::string audioPath;
   bool audioLoop = true;
   if (includeAudio && audioSource != nullptr && audioSource->IsLoaded())
   {
      audioPath = audioSource->FilePath();
      audioLoop = audioSource->loop;
   }

   std::string error;
   mRecorder = Platform::RecorderStart(path, mRecordW, mRecordH, recordFps, error,
                                       audioPath, audioLoop);
   if (mRecorder == nullptr)
   {
      mRecordStatus = error.empty() ? "could not start recording" : error;
      return false;
   }

   mRecordStatus = (includeAudio && !audioPath.empty()) ? "recording with audio..."
                                                        : "recording...";
   return true;
}

void OutputNode::StopRecording()
{
   if (mRecorder == nullptr)
      return;

   const int frames = Platform::RecorderFrameCount(mRecorder);
   std::string error;
   const bool ok = Platform::RecorderStop(mRecorder, error);
   mRecorder = nullptr;

   if (ok)
      mRecordStatus = "saved " + std::to_string(frames) + " frames";
   else
      mRecordStatus = error;
}

int OutputNode::RecordedFrames() const
{
   return Platform::RecorderFrameCount(mRecorder);
}

void OutputNode::CaptureFrame()
{
   if (mRecorder == nullptr)
      return;

   // Read back the locked-size region; if the graph resolution changed mid-take
   // we still feed the encoder frames of the size it was opened with.
   // If the graph resolution changed mid-take, only read what actually exists
   // and leave the rest of the frame black rather than reading out of bounds.
   const int w = mRecordW;
   const int h = mRecordH;
   if (mOut.w < w || mOut.h < h)
   {
      mReadback.assign((size_t)w * h * 4, 0);
      return;
   }
   mReadback.assign((size_t)w * h * 4, 0);

   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   GLuint fbo = 0;
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GLUtil::FboTexture(mOut), 0);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, mReadback.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glDeleteFramebuffers(1, &fbo);

   Platform::RecorderAppend(mRecorder, mReadback);
}

void OutputNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int tex = mInput.Pull(frameId);
   if (tex == 0)
      return;

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, mInput.Width(), mInput.Height()))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, tex]()
   {
      GLint loc = glGetUniformLocation(mProgram, "uTex");
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(loc, 0);
   });

   CaptureFrame();
}
