#include "BlendNode.h"
#include "GLUtil.h"

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
            "uniform float uMix;\n"
            "uniform int uLinear;\n"
            "uniform int uW3C;\n"
            "uniform int uImgHeadroom;\n")
         + GLUtil::kSrgbGlsl
         + BlendModes::kBlendGLSL
         + "void main() {\n"
           "   vec4 a = texture(uTexA, vUv);\n"
           "   vec4 b = texture(uTexB, vUv);\n"
           // linear light: modes and the over composite act on light (as in
           // Nuke, AE's linearized space, Affinity's 32-bit documents)
           "   if (uLinear == 1) { a.rgb = SrgbDecode(a.rgb); b.rgb = SrgbDecode(b.rgb); }\n"
           "   if (uMode == " + std::to_string(BlendModes::kEraseMode) + ") { fragColor = vec4(a.rgb, a.a * (1.0 - b.a * uMix)); return; }\n"
           "   if (uMode == " + std::to_string(BlendModes::kAntiEraseMode) + ") { fragColor = vec4(a.rgb, a.a * (1.0 - (1.0 - b.a) * uMix)); return; }\n"
           "   vec3 blended = blendModeImage(uMode, a.rgb, b.rgb, uW3C, uImgHeadroom);\n"
           "   float as = b.a * uMix;\n"
           "   vec3 cs = mix(b.rgb, blended, a.a);\n"
           "   float ar = as + a.a * (1.0 - as);\n"
           "   vec3 cr = (ar > 1e-5) ? (cs * as + a.rgb * a.a * (1.0 - as)) / ar : vec3(0.0);\n"
           "   fragColor = vec4(cr, ar);\n"
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
   mProgram = GLUtil::CompileProgram(GLUtil::WithClampedOutput(FragSrc(), "if (uLinear == 1) fragColor.rgb = SrgbEncode(fragColor.rgb);").c_str());
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
   if (!GLUtil::EnsureFbo(mOut, w, h, GLUtil::ImageWorkingFormat(), "Blend"))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, texA, texB]()
   {
      GLint locA = GLUtil::UniformLoc(mProgram, "uTexA");
      GLint locB = GLUtil::UniformLoc(mProgram, "uTexB");
      GLint locMode = GLUtil::UniformLoc(mProgram, "uMode");
      GLint locMix = GLUtil::UniformLoc(mProgram, "uMix");
      GLint locLinear = GLUtil::UniformLoc(mProgram, "uLinear");
      GLint locW3C = GLUtil::UniformLoc(mProgram, "uW3C");

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texA);
      glUniform1i(locA, 0);

      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, texB);
      glUniform1i(locB, 1);

      glUniform1i(locMode, mBlendFix ? mModeIndex : BlendModes::LegacyMode(mModeIndex));
      glUniform1f(locMix, mMix);
      glUniform1i(locLinear, mLinear ? 1 : 0);
      glUniform1i(locW3C, mBlendFix);
      GLUtil::SetImageHeadroom(mProgram, mHeadroom);
   });
}
