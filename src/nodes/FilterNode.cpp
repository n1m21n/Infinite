#include "FilterNode.h"

#include "gl3.h"
#include <cmath>
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
      "uniform int uHasSrc2;\n";


}

FilterNode::~FilterNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
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
   mUsesTime = mDef.fragmentBody.find("uTime") != std::string::npos;
}

bool FilterNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;

   std::string src = std::string(kPreamble) + mDef.fragmentBody;
   mProgram = GLUtil::CompileProgram(src.c_str());
   return mProgram != 0;
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

   if (!mUsesTime && mHasBuilt && sig == mBuilt)
      return; // nothing changed since the last cook - reuse mOut as-is

   NodeWorkCounter()++;
   GLUtil::RunShaderPass(mOut, mProgram, [this, srcTex, srcTex2]()
   {
      GLint locSrc = glGetUniformLocation(mProgram, "uSrc");
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(locSrc, 0);

      // bind the second sampler to a real texture even when unused; sampling an
      // unbound unit is undefined and spams the GL driver log
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, srcTex2 != 0 ? srcTex2 : srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc2"), 1);
      glUniform1i(glGetUniformLocation(mProgram, "uHasSrc2"), srcTex2 != 0 ? 1 : 0);

      GLint locTexel = glGetUniformLocation(mProgram, "uTexelSize");
      glUniform2f(locTexel, 1.0f / mInput.Width(), 1.0f / mInput.Height());

      GLint locTime = glGetUniformLocation(mProgram, "uTime");
      glUniform1f(locTime, (float)Transport::Instance().Seconds());

      for (size_t i = 0; i < mDef.params.size(); i++)
      {
         const FilterParamDef& p = mDef.params[i];
         GLint loc = glGetUniformLocation(mProgram, p.uniformName.c_str());
         if (loc < 0)
            continue;

         switch (p.type)
         {
            case FilterParamDef::Type::Float:
               // Degree-storing params (e.g. Transform's Rotation) are kept in
               // degrees for save/load and the params-panel slider; convert to
               // radians only here, at the point the shader actually reads it.
               glUniform1f(loc, p.isDegrees ? mParamValues[i][0] * (float)M_PI / 180.0f
                                             : mParamValues[i][0]);
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
   });

   mBuilt = sig;
   mHasBuilt = true;
   mRevision = NextTextureRevision();
}
