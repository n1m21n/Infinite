#include "RemoveBgNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>

#include "Platform.h"
#include "Transport.h"

namespace
{
   const std::vector<std::string> kModeNames = { "Subject (macOS 14+)", "Person (macOS 12+)" };
   const std::vector<std::string> kOutputModeNames = { "Cutout", "Mask only", "Background only" };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uMask;\n"
      "uniform int uHasMask;\n"
      "uniform vec2 uTexel;\n"
      "uniform int uOutputMode;\n"
      "uniform float uFeather;\n"
      "uniform float uThreshold;\n"
      "uniform float uContrast;\n"
      "uniform vec3 uBgColor;\n"
      "uniform float uBgOpacity;\n"
      "void main() {\n"
      "   vec4 c = texture(uSrc, vUv);\n"
      "   if (uHasMask == 0) { fragColor = c; return; }\n"
      "\n"
      "   float m;\n"
      "   if (uFeather > 0.0) {\n"
      "      // small blur of the mask only, to soften a hard segmentation edge\n"
      "      float sum = 0.0, total = 0.0;\n"
      "      for (int x = -2; x <= 2; x++) for (int y = -2; y <= 2; y++) {\n"
      "         vec2 off = vec2(float(x), float(y)) * uTexel * uFeather * 4.0;\n"
      "         float w = exp(-float(x*x + y*y) / 4.0);\n"
      "         sum += texture(uMask, vUv + off).r * w; total += w;\n"
      "      }\n"
      "      m = sum / max(total, 1e-4);\n"
      "   } else {\n"
      "      m = texture(uMask, vUv).r;\n"
      "   }\n"
      "\n"
      "   m = clamp((m - uThreshold) * uContrast + 0.5, 0.0, 1.0);\n"
      "\n"
      "   if (uOutputMode == 1) { fragColor = vec4(vec3(m), 1.0); return; }\n"
      "   if (uOutputMode == 2) m = 1.0 - m;\n"
      "\n"
      "   vec3 bg = uBgColor;\n"
      "   fragColor = vec4(mix(bg, c.rgb, m), max(c.a * m, uBgOpacity * (1.0 - m)));\n"
      "}\n";
}

const std::vector<std::string>& RemoveBgNode::ModeNames() { return kModeNames; }
const std::vector<std::string>& RemoveBgNode::OutputModeNames() { return kOutputModeNames; }

RemoveBgNode::~RemoveBgNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mMaskTex != 0)
      glDeleteTextures(1, &mMaskTex);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool RemoveBgNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void RemoveBgNode::ComputeMask(unsigned int srcTex, int w, int h)
{
   // Read the source back off the GPU so Vision can see it.
   std::vector<unsigned char> pixels((size_t)w * h * 4);
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   GLuint fbo = 0;
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glDeleteFramebuffers(1, &fbo);

   std::vector<unsigned char> mask;
   std::string error;
   const Platform::MattingMode mattingMode =
      (mode == 1) ? Platform::MattingMode::Person : Platform::MattingMode::Subject;

   if (!Platform::SubjectMask(pixels, w, h, mattingMode, mask, error))
   {
      mStatus = error;
      return;
   }

   if (mMaskTex == 0)
      glGenTextures(1, &mMaskTex);
   glBindTexture(GL_TEXTURE_2D, mMaskTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, mask.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mStatus = "mask ready";
}

void RemoveBgNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0)
   {
      GLUtil::DestroyFbo(mOut);
      return;
   }
   if (!EnsureShader())
      return;

   const int w = std::max(1, mInput.Width());
   const int h = std::max(1, mInput.Height());
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   // Auto-refresh is rate-limited by the transport, not the frame rate:
   // segmentation is far too expensive to run every frame.
   if (autoRefresh)
   {
      const double beat = Transport::Instance().Beats();
      if (beat < mLastMaskBeat || beat - mLastMaskBeat >= std::max(0.1f, refreshBeats))
      {
         mNeedsMask = true;
         mLastMaskBeat = beat;
      }
   }

   if (mNeedsMask)
   {
      mNeedsMask = false;
      ComputeMask(srcTex, w, h);
   }

   GLUtil::RunShaderPass(mOut, mProgram, [this, srcTex, w, h]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mMaskTex != 0 ? mMaskTex : srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uMask"), 1);
      glUniform1i(glGetUniformLocation(mProgram, "uHasMask"), mMaskTex != 0 ? 1 : 0);
      glUniform2f(glGetUniformLocation(mProgram, "uTexel"), 1.0f / w, 1.0f / h);
      glUniform1i(glGetUniformLocation(mProgram, "uOutputMode"), outputMode);
      glUniform1f(glGetUniformLocation(mProgram, "uFeather"), feather);
      glUniform1f(glGetUniformLocation(mProgram, "uThreshold"), threshold);
      glUniform1f(glGetUniformLocation(mProgram, "uContrast"), contrast);
      glUniform3f(glGetUniformLocation(mProgram, "uBgColor"), bgColor[0], bgColor[1], bgColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uBgOpacity"), bgOpacity);
   });
}
