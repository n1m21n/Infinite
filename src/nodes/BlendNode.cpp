#include "BlendNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <cmath>

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
            // Turbo (TOP parity): where B lands on the output, in output
            // pixels - its pre-fit rectangle, then B's own transform about the
            // output centre - and what shows outside it.
            "uniform vec2 uOutSize;\n"
            "uniform vec2 uRectMin;\n"
            "uniform vec2 uRectSize;\n"
            "uniform vec2 uBShift;\n"
            "uniform float uBScale;\n"
            "uniform float uBRot;\n"
            "uniform int uZeroB;\n"
            "uniform int uAlphaOp;\n")
         + BlendModes::kBlendGLSL
         + "vec4 sampleB() {\n"
           "   vec2 c = uOutSize * 0.5;\n"
           "   vec2 q = vUv * uOutSize - c - uBShift;\n"
           "   float s = sin(-uBRot), co = cos(-uBRot);\n"
           "   q = vec2(co*q.x - s*q.y, s*q.x + co*q.y) / max(uBScale, 0.0001);\n"
           "   vec2 uv = (q + c - uRectMin) / max(uRectSize, vec2(1.0));\n"
           "   if (uZeroB == 1 && (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)) return vec4(0.0);\n"
           "   return texture(uTexB, uv);\n"
           "}\n"
           "void main() {\n"
           "   vec4 a = texture(uTexA, vUv);\n"
           "   vec4 b = sampleB();\n"
           "   if (uMode == 30) { fragColor = vec4(a.rgb, a.a * (1.0 - b.a * uMix)); return; }\n"
           "   if (uMode == 31) { fragColor = vec4(a.rgb, a.a * (1.0 - (1.0 - b.a) * uMix)); return; }\n"
           "   vec3 blended = blendMode(uMode, a.rgb, b.rgb);\n"
           "   if (uAlphaOp == 0) {\n"
           "      fragColor = vec4(mix(a.rgb, blended, uMix * b.a), max(a.a, b.a));\n"
           "      return;\n"
           "   }\n"
           // Porter-Duff with B (its colour already through the blend mode)
           // as the source and A as the destination, premultiplied.
           "   float ba = b.a * uMix, aa = a.a;\n"
           "   vec3 bp = blended * ba, ap = a.rgb * aa;\n"
           "   vec3 rgb; float alpha;\n"
           "   if (uAlphaOp == 1)      { rgb = bp + ap * (1.0 - ba);              alpha = ba + aa * (1.0 - ba); }\n"
           "   else if (uAlphaOp == 2) { rgb = ap + bp * (1.0 - aa);              alpha = aa + ba * (1.0 - aa); }\n"
           "   else if (uAlphaOp == 3) { rgb = bp * aa;                           alpha = ba * aa; }\n"
           "   else if (uAlphaOp == 4) { rgb = bp * (1.0 - aa);                   alpha = ba * (1.0 - aa); }\n"
           "   else if (uAlphaOp == 5) { rgb = bp * aa + ap * (1.0 - ba);         alpha = aa; }\n"
           "   else                    { rgb = bp * (1.0 - aa) + ap * (1.0 - ba); alpha = ba * (1.0 - aa) + aa * (1.0 - ba); }\n"
           "   fragColor = vec4(alpha > 1e-5 ? rgb / alpha : vec3(0.0), alpha);\n"
           "}\n";
      return src;
   }
}

const std::vector<std::string>& BlendNode::ModeNames()
{
   return BlendModes::Names();
}

const std::vector<std::string>& BlendNode::AlphaOpNames()
{
   static const std::vector<std::string> k = { "Blend mode only", "Over (B over A)", "Under (A over B)",
                                               "Inside (B in A)", "Outside (B out of A)", "Atop (B atop A)", "Xor" };
   return k;
}

const std::vector<std::string>& BlendNode::SizeFromNames()
{
   static const std::vector<std::string> k = { "Input A", "Input B", "Custom" };
   return k;
}

const std::vector<std::string>& BlendNode::PreFitNames()
{
   static const std::vector<std::string> k = { "Stretch", "Fit", "Fill", "Native", "Fit Horizontal", "Fit Vertical" };
   return k;
}

const std::vector<std::string>& BlendNode::JustifyHNames()
{
   static const std::vector<std::string> k = { "Left", "Centre", "Right" };
   return k;
}

const std::vector<std::string>& BlendNode::JustifyVNames()
{
   static const std::vector<std::string> k = { "Bottom", "Centre", "Top" };
   return k;
}

const std::vector<std::string>& BlendNode::ExtendNames()
{
   static const std::vector<std::string> k = { "Hold", "Zero", "Repeat", "Mirror" };
   return k;
}

const std::vector<std::string>& BlendNode::FormatNames()
{
   static const std::vector<std::string> k = { "8-bit", "16-bit float", "32-bit float", "Use Input A" };
   return k;
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
      mHasBuilt = false;
      return;
   }

   int w = mInputA.IsConnected() ? mInputA.Width() : mInputB.Width();
   int h = mInputA.IsConnected() ? mInputA.Height() : mInputB.Height();
   if (sizeFrom == 1 && texB != 0)
   {
      w = mInputB.Width();
      h = mInputB.Height();
   }
   else if (sizeFrom == 2)
   {
      w = std::clamp(customW, 1, 16384);
      h = std::clamp(customH, 1, 16384);
   }

   unsigned int glFormat = GL_RGBA8;
   if (format == 1)
      glFormat = GL_RGBA16F;
   else if (format == 2)
      glFormat = GL_RGBA32F;
   else if (format == 3 && texA != 0)
      glFormat = mInputA.Format();

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, w, h, glFormat))
      return;

   CookSignature sig;
   VisitParams(sig.params);
   sig.revs[0] = mInputA.Revision();
   sig.revs[1] = mInputB.Revision();
   // B's own size places its pre-fit rectangle, so it is part of the output too.
   sig.revs[2] = ((unsigned long long)std::max(0, mInputB.Width()) << 32) | (unsigned long long)std::max(0, mInputB.Height());
   sig.w = w;
   sig.h = h;
   if (mHasBuilt && sig == mBuiltSig)
      return;
   NodeWorkCounter()++;

   // B's placed rectangle in output pixels (pre-fit + justify).
   const float bw0 = (float)std::max(1, texB != 0 ? mInputB.Width() : w);
   const float bh0 = (float)std::max(1, texB != 0 ? mInputB.Height() : h);
   float rw = (float)w, rh = (float)h;
   switch (preFit)
   {
      case kFit:    { const float k = std::min(w / bw0, h / bh0); rw = bw0 * k; rh = bh0 * k; break; }
      case kFill:   { const float k = std::max(w / bw0, h / bh0); rw = bw0 * k; rh = bh0 * k; break; }
      case kNative: rw = bw0; rh = bh0; break;
      case kFitH:   rw = (float)w; rh = bh0 * (w / bw0); break;
      case kFitV:   rh = (float)h; rw = bw0 * (h / bh0); break;
      default: break;
   }
   const float rx = justifyH == 0 ? 0.0f : justifyH == 2 ? w - rw : (w - rw) * 0.5f;
   const float ry = justifyV == 0 ? 0.0f : justifyV == 2 ? h - rh : (h - rh) * 0.5f;
   const int ext = std::clamp(extendB, 0, (int)GLUtil::kExtendCount - 1);

   GLUtil::RunShaderPass(mOut, mProgram, [this, texA, texB, w, h, rx, ry, rw, rh, ext]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texA);
      glUniform1i(glGetUniformLocation(mProgram, "uTexA"), 0);

      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, texB);
      glUniform1i(glGetUniformLocation(mProgram, "uTexB"), 1);
      // Zero is the explicit outside test plus Hold, so B's own edge pixels
      // filter exactly as they did before the rectangle existed.
      GLUtil::BindSampler(1, ext == GLUtil::kExtendZero ? GLUtil::kExtendHold : ext, GLUtil::kFilterLinear);
      glActiveTexture(GL_TEXTURE0);

      glUniform1i(glGetUniformLocation(mProgram, "uMode"), mModeIndex);
      glUniform1f(glGetUniformLocation(mProgram, "uMix"), mMix);
      glUniform2f(glGetUniformLocation(mProgram, "uOutSize"), (float)w, (float)h);
      glUniform2f(glGetUniformLocation(mProgram, "uRectMin"), rx, ry);
      glUniform2f(glGetUniformLocation(mProgram, "uRectSize"), rw, rh);
      glUniform2f(glGetUniformLocation(mProgram, "uBShift"), bTx * w, bTy * h);
      glUniform1f(glGetUniformLocation(mProgram, "uBScale"), bScale);
      glUniform1f(glGetUniformLocation(mProgram, "uBRot"), bRotate * 3.14159265f / 180.0f);
      glUniform1i(glGetUniformLocation(mProgram, "uZeroB"), ext == GLUtil::kExtendZero ? 1 : 0);
      glUniform1i(glGetUniformLocation(mProgram, "uAlphaOp"), alphaOp);
   });
   GLUtil::UnbindSampler(1);
   mBuiltSig = std::move(sig);
   mHasBuilt = true;
   mRevision = NextTextureRevision();
}
