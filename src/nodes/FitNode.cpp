#include "FitNode.h"

#include "gl3.h"
#include <algorithm>

namespace
{
   const std::vector<std::string> kModeNames = { "Fit", "Fill", "Stretch", "Native" };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform vec2 uScale;\n"   // source-uv scale about the centre
      "uniform vec3 uBgColor;\n"
      "uniform float uBgOpacity;\n"
      "void main() {\n"
      "   vec2 uv = (vUv - 0.5) * uScale + 0.5;\n"
      "   if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
      "      fragColor = vec4(uBgColor, uBgOpacity);\n"
      "      return;\n"
      "   }\n"
      "   fragColor = texture(uSrc, uv);\n"
      "}\n";
}

const std::vector<std::string>& FitNode::ModeNames()
{
   return kModeNames;
}

FitNode::~FitNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool FitNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void FitNode::CookIfNeeded(int frameId)
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

   const int srcW = std::max(1, mInput.Width());
   const int srcH = std::max(1, mInput.Height());

   int dstW = matchInput ? srcW : std::max(1, (int)width);
   int dstH = matchInput ? srcH : std::max(1, (int)height);

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, dstW, dstH))
      return;

   // uScale > 1 on an axis means we sample beyond the source there, i.e. bars.
   const float srcAspect = (float)srcW / (float)srcH;
   const float dstAspect = (float)dstW / (float)dstH;
   float scaleX = 1.0f;
   float scaleY = 1.0f;

   switch ((Mode)mode)
   {
      case Mode::Fit:
         if (dstAspect > srcAspect)
            scaleX = dstAspect / srcAspect; // pillarbox
         else
            scaleY = srcAspect / dstAspect; // letterbox
         break;
      case Mode::Fill:
         if (dstAspect > srcAspect)
            scaleY = srcAspect / dstAspect; // crop top/bottom
         else
            scaleX = dstAspect / srcAspect; // crop sides
         break;
      case Mode::Stretch:
         break; // 1:1 uv, aspect ignored
      case Mode::Native:
         scaleX = (float)dstW / (float)srcW;
         scaleY = (float)dstH / (float)srcH;
         break;
   }

   GLUtil::RunShaderPass(mOut, mProgram, [this, srcTex, scaleX, scaleY]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
      glUniform2f(glGetUniformLocation(mProgram, "uScale"), scaleX, scaleY);
      glUniform3f(glGetUniformLocation(mProgram, "uBgColor"), bgColor[0], bgColor[1], bgColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uBgOpacity"), bgOpacity);
   });
}
