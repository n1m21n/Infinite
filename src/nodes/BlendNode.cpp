#include "BlendNode.h"

#include "gl3.h"

#include "BlendModes.h"

namespace
{
   const std::string& FragSrc()
   {
      static const std::string src =
         std::string(
            "#version 150\n"
            "in vec2 vUv;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D uTexA;\n"
            "uniform sampler2D uTexB;\n"
            "uniform int uMode;\n"
            "uniform float uMix;\n")
         + BlendModes::kBlendGLSL
         + "void main() {\n"
           "   vec4 a = texture(uTexA, vUv);\n"
           "   vec4 b = texture(uTexB, vUv);\n"
           "   if (uMode == 30) { fragColor = vec4(a.rgb, a.a * (1.0 - b.a * uMix)); return; }\n"
           "   vec3 blended = blendMode(uMode, a.rgb, b.rgb);\n"
           "   fragColor = vec4(mix(a.rgb, blended, uMix * b.a), max(a.a, b.a));\n"
           "}\n";
      return src;
   }
}

const std::vector<std::string>& BlendNode::ModeNames()
{
   return BlendModes::Names();
}

BlendNode::~BlendNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool BlendNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(FragSrc().c_str());
   return mProgram != 0;
}

void BlendNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int texA = mInputA.Pull(frameId);
   unsigned int texB = mInputB.Pull(frameId);
   if (texA == 0 && texB == 0)
   {
      GLUtil::DestroyFbo(mOut);
      return;
   }

   int w = mInputA.IsConnected() ? mInputA.Width() : mInputB.Width();
   int h = mInputA.IsConnected() ? mInputA.Height() : mInputB.Height();

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, texA, texB]()
   {
      GLint locA = glGetUniformLocation(mProgram, "uTexA");
      GLint locB = glGetUniformLocation(mProgram, "uTexB");
      GLint locMode = glGetUniformLocation(mProgram, "uMode");
      GLint locMix = glGetUniformLocation(mProgram, "uMix");

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texA);
      glUniform1i(locA, 0);

      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, texB);
      glUniform1i(locB, 1);

      glUniform1i(locMode, mModeIndex);
      glUniform1f(locMix, mMix);
   });
}
