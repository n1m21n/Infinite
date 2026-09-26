#include "FilterNode.h"

#include "platform/OpenGLHeaders.h"
#include <cstdio>
#include <string>

#include "Transport.h"

namespace
{
   // Shared preamble every FilterDef's fragmentBody is appended to.
   const char* kPreamble =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform vec2 uTexelSize;\n"
      "uniform float uTime;\n"
      "uniform sampler2D uSrc2;\n"
      "uniform int uHasSrc2;\n"
      "uniform sampler2D uPass;\n"; // prePassBody's output, two-pass filters only


}

FilterNode::~FilterNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
   if (mPreProgram != 0)
      glDeleteProgram(mPreProgram);
}

FilterNode::FilterNode(const FilterDef& def)
: mDef(def)
{
   mParamValues.resize(def.params.size());
   for (size_t i = 0; i < def.params.size(); i++)
   {
      mParamValues[i][0] = def.params[i].defaultVal[0];
      mParamValues[i][1] = def.params[i].defaultVal[1];
      mParamValues[i][2] = def.params[i].defaultVal[2];
   }
   mUsesTime = mDef.fragmentBody.find("uTime") != std::string::npos ||
               mDef.prePassBody.find("uTime") != std::string::npos;
}

bool FilterNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;

   if (!mDef.prePassBody.empty())
   {
      std::string preSrc = std::string(kPreamble) + mDef.prePassBody;
      mPreProgram = GLUtil::CompileProgram(preSrc.c_str());
      if (mPreProgram == 0)
         return false;
      LookupLocs(mPreProgram, mPreLocs);
   }

   std::string src = std::string(kPreamble) + mDef.fragmentBody;
   mProgram = GLUtil::CompileProgram(src.c_str());
   if (mProgram == 0)
      return false;
   LookupLocs(mProgram, mMainLocs);
   return true;
}

void FilterNode::LookupLocs(unsigned int program, PassLocs& locs) const
{
   locs.src = glGetUniformLocation(program, "uSrc");
   locs.src2 = glGetUniformLocation(program, "uSrc2");
   locs.hasSrc2 = glGetUniformLocation(program, "uHasSrc2");
   locs.pass = glGetUniformLocation(program, "uPass");
   locs.texel = glGetUniformLocation(program, "uTexelSize");
   locs.time = glGetUniformLocation(program, "uTime");
   locs.params.resize(mDef.params.size());
   for (size_t i = 0; i < mDef.params.size(); i++)
      locs.params[i] = glGetUniformLocation(program, mDef.params[i].uniformName.c_str());
}

void FilterNode::BindUniforms(const PassLocs& locs, unsigned int srcTex, unsigned int srcTex2,
                              unsigned int passTex, float time) const
{
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, srcTex);
   glUniform1i(locs.src, 0);

   // bind the second sampler to a real texture even when unused; sampling an
   // unbound unit is undefined and spams the GL driver log
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, srcTex2 != 0 ? srcTex2 : srcTex);
   glUniform1i(locs.src2, 1);
   glUniform1i(locs.hasSrc2, srcTex2 != 0 ? 1 : 0);

   // Same rule for uPass: single-pass filters alias it to the input.
   glActiveTexture(GL_TEXTURE2);
   glBindTexture(GL_TEXTURE_2D, passTex != 0 ? passTex : srcTex);
   glUniform1i(locs.pass, 2);

   glUniform2f(locs.texel, 1.0f / mInput.Width(), 1.0f / mInput.Height());

   glUniform1f(locs.time, time);

   for (size_t i = 0; i < mDef.params.size(); i++)
   {
      const FilterParamDef& p = mDef.params[i];
      GLint loc = locs.params[i];
      if (loc < 0)
         continue;

      switch (p.type)
      {
         case FilterParamDef::Type::Float:
            glUniform1f(loc, mParamValues[i][0]);
            break;
         case FilterParamDef::Type::Int:
         case FilterParamDef::Type::Bool:
         case FilterParamDef::Type::Enum:
            glUniform1i(loc, (int)mParamValues[i][0]);
            break;
         case FilterParamDef::Type::Color:
            glUniform3f(loc, mParamValues[i][0], mParamValues[i][1], mParamValues[i][2]);
            break;
      }
   }
   glActiveTexture(GL_TEXTURE0);
}

void FilterNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0)
   {
      GLUtil::DestroyFbo(mOut);
         mHasBuilt = false;
      return;
   }
   unsigned int srcTex2 = (mDef.inputs > 1) ? mInput2.Pull(frameId) : 0;

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, mInput.Width(), mInput.Height()))
      return;

   Signature sig;
   sig.upstreamRev = mInput.Revision();
   sig.upstreamRev2 = (mDef.inputs > 1) ? mInput2.Revision() : 0;
   sig.width = mInput.Width();
   sig.height = mInput.Height();
   sig.params = mParamValues;
   sig.time = mUsesTime ? (float)Transport::Instance().Seconds() : 0.0f;

   if (mHasBuilt && sig == mBuilt)
      return; // nothing changed since the last cook - reuse mOut as-is

   NodeWorkCounter()++;
   unsigned int passTex = 0;
   if (mPreProgram != 0)
   {
      // Two-pass filters (separable blurs): horizontal pass into a float
      // intermediate, then the main body reads it as uPass.
      // Shared scratch target: it only lives from this pass to the main
      // pass below, and both input Pulls already ran, so nothing can
      // acquire it in between. Keep it that way.
      GLUtil::Fbo* mid = GLUtil::AcquireScratchFbo(mInput.Width(), mInput.Height(), GL_RGBA16F);
      if (mid == nullptr)
         return;
      GLUtil::RunShaderPass(*mid, mPreProgram, [this, srcTex, srcTex2, &sig]()
      {
         BindUniforms(mPreLocs, srcTex, srcTex2, 0, sig.time);
      });
      passTex = GLUtil::FboTexture(*mid);
   }
   GLUtil::RunShaderPass(mOut, mProgram, [this, srcTex, srcTex2, passTex, &sig]()
   {
      BindUniforms(mMainLocs, srcTex, srcTex2, passTex, sig.time);
   });

   mBuilt = sig;
   mHasBuilt = true;
   mRevision = NextTextureRevision();
}
