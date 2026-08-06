#include "FilterNode.h"

#include <OpenGL/gl3.h>
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
      return;
   unsigned int srcTex2 = (mDef.inputs > 1) ? mInput2.Pull(frameId) : 0;

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, mInput.Width(), mInput.Height()))
      return;

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
               glUniform1f(loc, mParamValues[i][0]);
               break;
            case FilterParamDef::Type::Int:
            case FilterParamDef::Type::Bool:
               glUniform1i(loc, (int)mParamValues[i][0]);
               break;
            case FilterParamDef::Type::Color:
               glUniform3f(loc, mParamValues[i][0], mParamValues[i][1], mParamValues[i][2]);
               break;
         }
      }
   });
}
