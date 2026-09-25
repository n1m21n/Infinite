#include "LayoutNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTex;\n"
      "uniform float uOpacity;\n"
      "void main() {\n"
      "   vec4 c = texture(uTex, vUv);\n"
      "   fragColor = vec4(c.rgb, c.a * uOpacity);\n"
      "}\n";
}

LayoutNode::LayoutNode()
{
   for (int i = 0; i < kSlots; i++)
   {
      x[i] = 0.0f;
      y[i] = 0.0f;
      scale[i] = 1.0f;
      opacity[i] = 1.0f;
      sizeMode[i] = kSizeReal;
      customW[i] = 0;
      customH[i] = 0;
      visible[i] = true;
   }
}

LayoutNode::~LayoutNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool LayoutNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

int LayoutNode::SourceWidth(int slot) const { return (slot >= 0 && slot < kSlots) ? mSrcW[slot] : 0; }
int LayoutNode::SourceHeight(int slot) const { return (slot >= 0 && slot < kSlots) ? mSrcH[slot] : 0; }

void LayoutNode::BaseSize(int s, float& outW, float& outH) const
{
   if (sizeMode[s] == kSizeCustom && customW[s] > 0 && customH[s] > 0)
   {
      outW = (float)customW[s];
      outH = (float)customH[s];
   }
   else
   {
      outW = (float)mSrcW[s];
      outH = (float)mSrcH[s];
   }
}

// x/y is the top-left of the layer at scale 1 (its real or custom size);
// `scale` grows or shrinks it around its own centre.
void LayoutNode::LayerRect(int s, float& outX, float& outY, float& outW, float& outH) const
{
   float bw = 0.0f, bh = 0.0f;
   BaseSize(s, bw, bh);
   const float k = std::max(0.0f, scale[s]);
   outW = bw * k;
   outH = bh * k;
   outX = x[s] + (bw - outW) * 0.5f;
   outY = y[s] + (bh - outH) * 0.5f;
}

void LayoutNode::PlaceRealSize(int s)
{
   sizeMode[s] = kSizeReal;
   scale[s] = 1.0f;
}

void LayoutNode::PlaceCenter(int s)
{
   float bw = 0.0f, bh = 0.0f;
   BaseSize(s, bw, bh);
   x[s] = std::round(((float)canvasW - bw) * 0.5f);
   y[s] = std::round(((float)canvasH - bh) * 0.5f);
}

void LayoutNode::PlaceFit(int s)
{
   if (mSrcW[s] <= 0 || mSrcH[s] <= 0)
      return;
   float bw = 0.0f, bh = 0.0f;
   BaseSize(s, bw, bh);
   if (bw <= 0.0f || bh <= 0.0f)
      return;
   scale[s] = std::min((float)canvasW / bw, (float)canvasH / bh);
   PlaceCenter(s);
}

void LayoutNode::PlaceFill(int s)
{
   if (mSrcW[s] <= 0 || mSrcH[s] <= 0)
      return;
   float bw = 0.0f, bh = 0.0f;
   BaseSize(s, bw, bh);
   if (bw <= 0.0f || bh <= 0.0f)
      return;
   scale[s] = std::max((float)canvasW / bw, (float)canvasH / bh);
   PlaceCenter(s);
}

void LayoutNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   canvasW = std::clamp(canvasW, 1, 16384);
   canvasH = std::clamp(canvasH, 1, 16384);

   unsigned int tex[kSlots] = {};
   for (int i = 0; i < kSlots; i++)
   {
      tex[i] = mInputs[i].Pull(frameId);
      mSrcW[i] = tex[i] != 0 ? mInputs[i].Width() : 0;
      mSrcH[i] = tex[i] != 0 ? mInputs[i].Height() : 0;
   }

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, canvasW, canvasH))
      return;

   GLint prevFbo = 0;
   GLint prevViewport[4] = {};
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glGetIntegerv(GL_VIEWPORT, prevViewport);
   const GLboolean blendWasOn = glIsEnabled(GL_BLEND);

   glBindFramebuffer(GL_FRAMEBUFFER, mOut.fbo);
   glViewport(0, 0, canvasW, canvasH);
   glClearColor(bgColor[0], bgColor[1], bgColor[2], std::clamp(bgAlpha, 0.0f, 1.0f));
   glClear(GL_COLOR_BUFFER_BIT);

   glEnable(GL_BLEND);
   glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
   glUseProgram(mProgram);
   glActiveTexture(GL_TEXTURE0);
   glUniform1i(glGetUniformLocation(mProgram, "uTex"), 0);
   const GLint locOpacity = glGetUniformLocation(mProgram, "uOpacity");

   for (int i = 0; i < kSlots; i++)
   {
      if (tex[i] == 0 || !visible[i] || opacity[i] <= 0.0f)
         continue;
      float lx, ly, lw, lh;
      LayerRect(i, lx, ly, lw, lh);
      const int vw = (int)std::lround(lw);
      const int vh = (int)std::lround(lh);
      if (vw <= 0 || vh <= 0)
         continue;
      // Canvas pixels are top-left based; GL viewports are bottom-left.
      const int vx = (int)std::lround(lx);
      const int vy = canvasH - (int)std::lround(ly) - vh;
      glViewport(vx, vy, vw, vh);
      glBindTexture(GL_TEXTURE_2D, tex[i]);
      glUniform1f(locOpacity, std::clamp(opacity[i], 0.0f, 1.0f));
      GLUtil::DrawFullscreenQuad();
   }

   glUseProgram(0);
   glBindTexture(GL_TEXTURE_2D, 0);
   if (!blendWasOn)
      glDisable(GL_BLEND);
   glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
   glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

   NodeWorkCounter()++;
   mRevision = NextTextureRevision();
}

void LayoutNode::VisitParams(ParamVisitor& v)
{
   v.Int("canvasW", canvasW);
   v.Int("canvasH", canvasH);
   v.Color("bgColor", bgColor);
   v.Float("bgAlpha", bgAlpha);
   char key[32];
   for (int i = 0; i < kSlots; i++)
   {
      snprintf(key, sizeof(key), "x%d", i);
      v.Float(key, x[i]);
      snprintf(key, sizeof(key), "y%d", i);
      v.Float(key, y[i]);
      snprintf(key, sizeof(key), "scale%d", i);
      v.Float(key, scale[i]);
      snprintf(key, sizeof(key), "opacity%d", i);
      v.Float(key, opacity[i]);
      snprintf(key, sizeof(key), "sizeMode%d", i);
      v.Int(key, sizeMode[i]);
      snprintf(key, sizeof(key), "customW%d", i);
      v.Int(key, customW[i]);
      snprintf(key, sizeof(key), "customH%d", i);
      v.Int(key, customH[i]);
      snprintf(key, sizeof(key), "visible%d", i);
      v.Bool(key, visible[i]);
   }
}
