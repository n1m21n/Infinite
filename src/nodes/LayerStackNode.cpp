#include "LayerStackNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <string>

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
            "uniform sampler2D uTex0;\n"
            "uniform sampler2D uTex1;\n"
            "uniform sampler2D uTex2;\n"
            "uniform sampler2D uTex3;\n"
            "uniform int uActive[4];\n"
            "uniform int uModes[4];\n"
            "uniform float uOpacity[4];\n")
         + BlendModes::kBlendGLSL
         + "vec4 fetch(int i) {\n"
           "   if (i == 0) return texture(uTex0, vUv);\n"
           "   if (i == 1) return texture(uTex1, vUv);\n"
           "   if (i == 2) return texture(uTex2, vUv);\n"
           "   return texture(uTex3, vUv);\n"
           "}\n"
           "void main() {\n"
           "   vec4 acc = vec4(0.0);\n"
           "   bool haveBase = false;\n"
           "   for (int i = 0; i < 4; i++) {\n"
           "      if (uActive[i] == 0) continue;\n"
           "      vec4 layer = fetch(i);\n"
           "      float amt = uOpacity[i] * layer.a;\n"
           "      if (!haveBase) { acc = vec4(layer.rgb, layer.a * uOpacity[i]); haveBase = true; continue; }\n"
           "      if (uModes[i] == 30) { acc.a *= (1.0 - amt); continue; }\n"
           "      vec3 blended = blendMode(uModes[i], acc.rgb, layer.rgb);\n"
           "      acc.rgb = mix(acc.rgb, blended, amt);\n"
           "      acc.a = max(acc.a, layer.a * uOpacity[i]);\n"
           "   }\n"
           "   fragColor = acc;\n"
           "}\n";
      return src;
   }
}

LayerStackNode::~LayerStackNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

void LayerStackNode::SwapLayers(int a, int b)
{
   if (a < 0 || b < 0 || a >= kSlots || b >= kSlots || a == b)
      return;

   INode* srcA = mInputs[a].GetSource();
   INode* srcB = mInputs[b].GetSource();
   if (srcB != nullptr)
      mInputs[a].Connect(srcB);
   else
      mInputs[a].Disconnect();
   if (srcA != nullptr)
      mInputs[b].Connect(srcA);
   else
      mInputs[b].Disconnect();

   std::swap(modes[a], modes[b]);
   std::swap(opacities[a], opacities[b]);
}

bool LayerStackNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(FragSrc().c_str());
   return mProgram != 0;
}

void LayerStackNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int tex[kSlots] = { 0, 0, 0, 0 };
   int active[kSlots] = { 0, 0, 0, 0 };
   int w = 0, h = 0;

   for (int i = 0; i < kSlots; i++)
   {
      tex[i] = mInputs[i].Pull(frameId);
      active[i] = tex[i] != 0 ? 1 : 0;
      if (active[i] && w == 0)
      {
         w = mInputs[i].Width();
         h = mInputs[i].Height();
      }
   }

   if (w == 0 || h == 0)
      return;

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, &tex, &active]()
   {
      static const char* kTexNames[kSlots] = { "uTex0", "uTex1", "uTex2", "uTex3" };
      for (int i = 0; i < kSlots; i++)
      {
         glActiveTexture(GL_TEXTURE0 + i);
         glBindTexture(GL_TEXTURE_2D, tex[i]);
         glUniform1i(glGetUniformLocation(mProgram, kTexNames[i]), i);
      }
      glUniform1iv(glGetUniformLocation(mProgram, "uActive"), kSlots, active);
      glUniform1iv(glGetUniformLocation(mProgram, "uModes"), kSlots, modes);
      glUniform1fv(glGetUniformLocation(mProgram, "uOpacity"), kSlots, opacities);
   });
}
