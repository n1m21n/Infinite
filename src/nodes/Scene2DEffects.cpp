// Scene 2D effect stacks (Photoshop's smart filters): each layer's picture runs through a list of native
// adjustments and stack-owned nodes, each with its own opacity and blend, then the stack's mask, before the
// layer blends with what is under it. Also the colour lookup (.cube) and Shadows / Highlights helpers.
#include "ColorSpace.h"
#include "Scene2DNode.h"
#include "GLUtil.h"
#include "RemoveBgNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "BlendModes.h"
#include "platform/AppPaths.h"

const char* const Scene2DNode::kEffBlendNames[5] = { "Normal", "Multiply", "Screen", "Overlay", "Luminosity" };

namespace
{
   unsigned long long EMix(unsigned long long h, unsigned long long v) { return (h ^ v) * 1099511628211ull; }

   unsigned long long FloatBits(float v)
   {
      unsigned bits = 0;
      std::memcpy(&bits, &v, sizeof bits);
      return bits;
   }

   // One effect onto the picture so far: out = mix(base, blend(base, fx), opacity x mask). toMask: the effect's alpha
   // cuts the base instead of replacing it.
   const char* kEffMixSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uBase;\n"
      "uniform sampler2D uFx;\n"
      "uniform sampler2D uMask;\n"
      "uniform int uHasMask;\n"
      "uniform int uBlend;\n"
      "uniform int uToMask;\n"
      "uniform float uOpacity;\n"
      "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
      "void main() {\n"
      "   vec4 b = texture(uBase, vUv);\n"
      "   vec4 f = texture(uFx, vUv);\n"
      "   float k = clamp(uOpacity, 0.0, 1.0);\n"
      "   if (uHasMask == 1) k *= clamp(Luma(texture(uMask, vUv).rgb), 0.0, 1.0);\n"
      "   if (uToMask == 1) { fragColor = vec4(b.rgb, b.a * mix(1.0, f.a, k)); return; }\n"
      "   vec3 r = f.rgb;\n"
      "   if (uBlend == 1) r = b.rgb * f.rgb;\n"
      "   else if (uBlend == 2) r = 1.0 - (1.0 - b.rgb) * (1.0 - f.rgb);\n"
      "   else if (uBlend == 3) r = mix(2.0 * b.rgb * f.rgb, 1.0 - 2.0 * (1.0 - b.rgb) * (1.0 - f.rgb), step(0.5, b.rgb));\n"
      "   else if (uBlend == 4) r = b.rgb + (Luma(f.rgb) - Luma(b.rgb));\n"
      "   float a = uBlend == 0 ? f.a : b.a;\n"
      "   fragColor = vec4(mix(b.rgb, r, k), mix(b.a, a, k));\n"
      "}\n";

   // Luminance (display values) blurred along one axis; the first pass reads colour, the second its own red.
   const char* kLumaBlurSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform vec2 uDir;\n"
      "uniform float uSigma;\n"
      "uniform int uFirst;\n"
      "uniform int uLinear;\n"
      "float L(vec2 uv) {\n"
      "   vec4 c = texture(uSrc, uv);\n"
      "   if (uFirst == 0) return c.r;\n"
      "   vec3 d = uLinear == 1 ? pow(max(c.rgb, 0.0), vec3(1.0 / 2.2)) : c.rgb;\n"
      "   return dot(d, vec3(0.2126, 0.7152, 0.0722));\n"
      "}\n"
      "void main() {\n"
      "   float s = max(uSigma, 0.5), stride = max(1.0, 3.0 * s / 24.0);\n"
      "   int taps = int(min(24.0, ceil(3.0 * s / stride)));\n"
      "   float acc = 0.0, wsum = 0.0;\n"
      "   for (int i = -24; i <= 24; i++) {\n"
      "      if (abs(i) > taps) continue;\n"
      "      float x = float(i) * stride, w = exp(-0.5 * x * x / (s * s));\n"
      "      acc += w * L(vUv + uDir * x);\n"
      "      wsum += w;\n"
      "   }\n"
      "   fragColor = vec4(vec3(acc / max(wsum, 1e-6)), 1.0);\n"
      "}\n";

   std::string RestOfLine(std::istream& s)
   {
      std::string t;
      std::getline(s, t);
      const size_t a = t.find_first_not_of(' ');
      return a == std::string::npos ? std::string() : t.substr(a);
   }

   void WriteCurve(std::ostream& o, const std::vector<float>& c)
   {
      o << " " << c.size() / 2;
      for (float v : c) o << " " << v;
   }

   bool ReadCurve(std::istream& s, std::vector<float>& c)
   {
      int n = 0;
      if (!(s >> n))
         return false;
      n = std::clamp(n, 0, 16);
      std::vector<float> pts((size_t)n * 2);
      for (float& v : pts) s >> v;
      if (!s)
         return false;
      if (n >= 2)
      {
         for (float& v : pts) v = std::clamp(v, 0.0f, 1.0f);
         c = pts;
      }
      return true;
   }

   // Native settings shared by an effect and an adjustment layer: per-channel levels, the extra params, per-channel
   // curves. One line each so a reader skips what it does not know.
   template <class T> void WriteAdjExtra(std::ostream& o, const T& x)
   {
      for (int ch = 0; ch < 3; ch++)
         for (float v : x.levelsCh[ch]) o << " " << v;
      o << " " << x.adjP.size();
      for (float v : x.adjP) o << " " << v;
      for (int ch = 0; ch < 3; ch++)
         WriteCurve(o, x.curveCh[ch]);
   }

   template <class T> void ReadAdjExtra(std::istream& s, T& x)
   {
      for (int ch = 0; ch < 3; ch++)
         for (float& v : x.levelsCh[ch])
         {
            s >> v;
            v = std::clamp(v, 0.0f, 1.0f);
         }
      int n = 0;
      s >> n;
      n = std::clamp(n, 0, 40);
      x.adjP.assign((size_t)n, 0.0f);
      for (float& v : x.adjP) s >> v;
      for (int ch = 0; ch < 3; ch++)
         ReadCurve(s, x.curveCh[ch]);
   }

   bool DefaultExtra(const Scene2DNode::Layer& l)
   {
      for (int ch = 0; ch < 3; ch++)
      {
         const float* v = l.levelsCh[ch];
         if (v[0] != 0 || v[1] != 1 || v[2] != 0 || v[3] != 1 || l.curveCh[ch] != std::vector<float>{ 0, 0, 1, 1 })
            return false;
      }
      return l.adjP.empty();
   }
}

// ---- text ------------------------------------------------------------------------------------------------

void Scene2DNode::WriteEffectLines(std::ostream& o, const Layer& l, int id)
{
   if (l.kind == Kind::Adjust && !DefaultExtra(l))
   {
      o << "adjx " << id;
      WriteAdjExtra(o, l);
      o << "\n";
   }
   if (l.kind == Kind::Adjust && !l.adjFile.empty())
      o << "adjfile " << id << " " << l.adjFile << "\n";
   for (size_t i = 0; i < l.effects.size(); i++)
   {
      const Effect& e = l.effects[i];
      o << "eff " << id << " " << i << " " << +e.on << " " << e.uid << " " << +e.shown << " " << +e.animated << " " << +e.toMask
        << " " << e.opacity << " " << e.blend << " " << e.adjType << " " << e.adjAmt << " " << +e.adjMap;
      for (float v : e.colorA) o << " " << v;
      for (float v : e.colorB) o << " " << v;
      for (float v : e.levels) o << " " << v;
      WriteCurve(o, e.curve);
      WriteAdjExtra(o, e);
      o << "\n";
      if (!e.entry.empty())
         o << "effentry " << id << " " << i << " " << e.entry << "\n";
      if (!e.adjFile.empty())
         o << "efffile " << id << " " << i << " " << e.adjFile << "\n";
      if (!e.stops.empty())
      {
         o << "effstops " << id << " " << i;
         WriteStops(o, e.stops);
         o << "\n";
      }
   }
   if (l.effMaskUid != 0)
      o << "effmask " << id << " " << l.effMaskUid << "\n";
}

bool Scene2DNode::ParseEffectLine(const std::string& key, std::istream& s, Layer& l)
{
   if (key == "adjx")
   {
      ReadAdjExtra(s, l);
      return true;
   }
   if (key == "adjfile")
   {
      l.adjFile = RestOfLine(s);
      return true;
   }
   if (key == "effmask")
   {
      s >> l.effMaskUid;
      return true;
   }
   if (key != "eff" && key != "effentry" && key != "efffile" && key != "effstops")
      return false;
   int idx = -1;
   s >> idx;
   if (idx < 0 || idx >= 64)
      return true;
   if ((size_t)idx >= l.effects.size())
      l.effects.resize((size_t)idx + 1);
   Effect& e = l.effects[(size_t)idx];
   if (key == "effentry")
      e.entry = RestOfLine(s);
   else if (key == "efffile")
      e.adjFile = RestOfLine(s);
   else if (key == "effstops")
      ReadStops(s, e.stops);
   else
   {
      int on = 1, shown = 0, anim = 0, toMask = 0, map = 0;
      s >> on >> e.uid >> shown >> anim >> toMask >> e.opacity >> e.blend >> e.adjType >> e.adjAmt >> map;
      for (float& v : e.colorA) s >> v;
      for (float& v : e.colorB) s >> v;
      for (float& v : e.levels) s >> v;
      ReadCurve(s, e.curve);
      ReadAdjExtra(s, e);
      e.on = on != 0;
      e.shown = shown != 0;
      e.animated = anim != 0;
      e.toMask = toMask != 0;
      e.adjMap = map != 0;
      e.opacity = std::clamp(e.opacity, 0.0f, 1.0f);
      e.blend = std::clamp(e.blend, 0, 4);
      e.adjType = std::clamp(e.adjType, 0, kAdjCount - 1);
   }
   return true;
}

std::string Scene2DNode::EffectsText(const Layer& l)
{
   Layer only;
   only.kind = Kind::Paint;
   only.effects = l.effects;
   only.effMaskUid = l.effMaskUid;
   std::ostringstream o;
   o << "inf2dfx 1\n";
   WriteEffectLines(o, only, 0);
   return o.str();
}

bool Scene2DNode::ParseEffects(const std::string& text, Layer& l)
{
   std::istringstream in(text);
   std::string line;
   if (!std::getline(in, line) || line.rfind("inf2dfx", 0) != 0)
      return false;
   Layer got;
   got.kind = Kind::Paint;
   while (std::getline(in, line))
   {
      std::istringstream s(line);
      std::string key;
      int id = 0;
      if (!(s >> key >> id))
         continue;
      ParseEffectLine(key, s, got);
   }
   l.effects = got.effects;
   l.effMaskUid = got.effMaskUid;
   return true;
}

Scene2DNode::Layer Scene2DNode::AdjLayerOf(const Effect& e)
{
   Layer a;
   a.kind = Kind::Adjust;
   a.adjType = e.adjType;
   a.adjAmt = a.adjTo = e.adjAmt;
   a.adjDrive = 0;
   a.adjMap = e.adjMap;
   std::copy(e.colorA, e.colorA + 4, a.colorA);
   std::copy(e.colorB, e.colorB + 4, a.colorB);
   std::copy(e.levels, e.levels + 4, a.levels);
   for (int ch = 0; ch < 3; ch++)
   {
      std::copy(e.levelsCh[ch], e.levelsCh[ch] + 4, a.levelsCh[ch]);
      a.curveCh[ch] = e.curveCh[ch];
   }
   a.curve = e.curve;
   a.adjP = e.adjP;
   a.stops = e.stops;
   a.adjFile = e.adjFile;
   return a;
}

// ---- running ---------------------------------------------------------------------------------------------

INode* Scene2DNode::NodeThrough(uint64_t uid) const
{
   if (uid == 0 || !ResolveUid)
      return nullptr;
   INode* node = ResolveUid(uid);
   for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
      node = node->BypassSource();
   return (node != nullptr && !node->bypassed && node != this) ? node : nullptr;
}

// What the stack reads that the text does not hold: each node effect's params (modulation writes them in place) and
// bypass, the mask's picture; an effect that moves by itself changes every frame.
unsigned long long Scene2DNode::EffectsHash(const Layer& l, int frameId)
{
   unsigned long long h = 1469598103934665603ull;
   if (!l.HasEffects())
      return h;
   for (const Effect& e : l.effects)
   {
      if (!e.on || e.uid == 0)
         continue;
      INode* n = ResolveUid ? ResolveUid(e.uid) : nullptr;
      h = EMix(h, (unsigned long long)(uintptr_t)n);
      if (n == nullptr)
         continue;
      h = EMix(h, n->bypassed ? 1ull : 2ull);
      ParamSnapshot snap;
      n->VisitParams(snap);
      for (float v : snap.values)
         h = EMix(h, FloatBits(v));
      for (const std::string& t : snap.texts)
         for (char c : t)
            h = EMix(h, (unsigned char)c);
      if (auto* rbg = dynamic_cast<RemoveBgNode*>(n))
      {
         h = EMix(h, n->TextureRevision());
         for (char c : rbg->Status())
            h = EMix(h, (unsigned char)c);
         if (rbg->NeedsMask() || rbg->autoRefresh || (!rbg->HasMask() && rbg->maskActive))
            h = EMix(h, NextTextureRevision());
      }
      else if (e.animated)
         h = EMix(h, NextTextureRevision());
   }
   if (INode* m = NodeThrough(l.effMaskUid))
   {
      if (frameId >= 0)
         m->CookIfNeeded(frameId);
      h = EMix(EMix(h, (unsigned long long)(uintptr_t)m), m->TextureRevision());
   }
   return h;
}

bool Scene2DNode::EffMix(int ping, unsigned base, unsigned fx, unsigned mask, float opacity, int blend, bool toMask)
{
   if (mEffMixProgram == 0)
      mEffMixProgram = GLUtil::CompileProgram(kEffMixSrc);
   if (mEffMixProgram == 0 || !GLUtil::EnsureFbo(mEff[ping], mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2DEff"))
      return false;
   const unsigned prog = mEffMixProgram;
   GLUtil::RunShaderPass(mEff[ping], prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, base);
      glUniform1i(GLUtil::UniformLoc(prog, "uBase"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, fx);
      glUniform1i(GLUtil::UniformLoc(prog, "uFx"), 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, mask);
      glUniform1i(GLUtil::UniformLoc(prog, "uMask"), 2);
      glUniform1i(GLUtil::UniformLoc(prog, "uHasMask"), mask != 0 ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uBlend"), blend);
      glUniform1i(GLUtil::UniformLoc(prog, "uToMask"), toMask ? 1 : 0);
      glUniform1f(GLUtil::UniformLoc(prog, "uOpacity"), opacity);
      glActiveTexture(GL_TEXTURE0);
   });
   return true;
}

void Scene2DNode::DrawEffects(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode)
{
   mEffDraws++;
   // The layer alone (its filters already on it), at full strength, in document space.
   Layer plain = l;
   plain.effects.clear();
   plain.effMaskUid = 0;
   plain.blend = 0;
   plain.opacity = 1.0f;
   plain.fill = 1.0f;
   plain.clip = false;
   const Layer fresh;
   std::copy(fresh.bifT, fresh.bifT + 4, plain.bifT);
   std::copy(fresh.bifU, fresh.bifU + 4, plain.bifU);
   const float clear[4] = { 0, 0, 0, 0 };
   Clear(PairAt(depth + 1), clear);
   DrawLayer(plain, depth + 1, frameId, 1.0f, false);
   const unsigned orig = PairAt(depth + 1).Tex();
   unsigned cur = orig;
   int ping = 0;
   for (const Effect& e : l.effects)
   {
      if (!e.on)
         continue;
      unsigned out = 0;
      if (e.uid == 0)
      {
         if (RunAdjust(AdjLayerOf(e), cur))
            out = mAdjFbo.tex;
      }
      else
      {
         INode* n = ResolveUid ? ResolveUid(e.uid) : nullptr;
         if (n == nullptr || n->bypassed)
            continue;
         Layer tmp;
         tmp.kind = Kind::Node;
         tmp.uid = e.uid;
         if (CookOnBelow(tmp, n, cur))
            out = n->GetOutputTexture();
      }
      if (out == 0 || !EffMix(ping, cur, out, 0, e.opacity, e.blend, e.toMask))
         continue;
      cur = mEff[ping].tex;
      ping ^= 1;
   }
   if (INode* m = NodeThrough(l.effMaskUid))
   {
      m->CookIfNeeded(frameId);
      if (const unsigned mt = m->GetOutputTexture())
         if (cur != orig && EffMix(ping, orig, cur, mt, 1.0f, 0, false))
            cur = mEff[ping].tex;
   }
   PassArgs a;
   a.bif = l.HasBlendIf() ? &l : nullptr;
   a.gamma = BlendGammaOf(l);
   a.tex = cur;
   a.texW = mDoc.w;
   a.texH = mDoc.h;
   a.box[0] = (float)mDoc.w;
   a.box[1] = (float)mDoc.h;
   a.srgb = false;   // working space
   a.opacity = l.opacity * l.fill * opacityMul;
   a.blend = l.kind == Kind::Group && l.blend == kPassThrough ? 0 : std::clamp(l.blend, 0, BlendModes::kAntiEraseMode);
   a.clipMode = clipMode;
   Pass(depth, a);
}

// ---- colour lookup, shadows / highlights -----------------------------------------------------------------

bool Scene2DNode::ReadCube(const std::string& path, int& size, std::vector<float>& rgb)
{
   std::ifstream f(AppPaths::FsPath(path));
   if (!f)
      return false;
   size = 0;
   rgb.clear();
   std::string line;
   while (std::getline(f, line))
   {
      const size_t a = line.find_first_not_of(" \t\r");
      if (a == std::string::npos || line[a] == '#')
         continue;
      std::istringstream s(line.substr(a));
      if (line.compare(a, 11, "LUT_3D_SIZE") == 0)
      {
         std::string k;
         s >> k >> size;
         continue;
      }
      if (std::isalpha((unsigned char)line[a]))
         continue;   // TITLE, DOMAIN_MIN / MAX, LUT_1D_SIZE
      float r = 0, g = 0, b = 0;
      if (s >> r >> g >> b)
      {
         rgb.push_back(r);
         rgb.push_back(g);
         rgb.push_back(b);
      }
   }
   return size >= 2 && size <= 128 && rgb.size() == (size_t)size * size * size * 3;
}

bool Scene2DNode::BindLookup(const std::string& path)
{
   if (path.empty())
      return false;
   if (path == mLutPath)
      return mLutTex != 0;
   mLutPath = path;
   int n = 0;
   std::vector<float> rgb;
   if (!ReadCube(path, n, rgb))
   {
      mLutSize = 0;
      return false;
   }
   if (mLutTex == 0)
      glGenTextures(1, &mLutTex);
   glBindTexture(GL_TEXTURE_3D, mLutTex);
   glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, n, n, n, 0, GL_RGB, GL_FLOAT, rgb.data());
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_3D, 0);
   mLutSize = n;
   return true;
}

unsigned Scene2DNode::BlurLuma(unsigned src, float radius)
{
   if (mLumaBlurProgram == 0)
      mLumaBlurProgram = GLUtil::CompileProgram(kLumaBlurSrc);
   if (mLumaBlurProgram == 0 || src == 0)
      return 0;
   for (int i = 0; i < 2; i++)
      if (!GLUtil::EnsureFbo(mShBlur[i], mDoc.w, mDoc.h, GL_RGBA16F, "Scene2DShBlur"))
         return 0;
   const unsigned prog = mLumaBlurProgram;
   for (int pass = 0; pass < 2; pass++)
   {
      const unsigned in = pass == 0 ? src : mShBlur[0].tex;
      GLUtil::RunShaderPass(mShBlur[pass], prog, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, in);
         glUniform1i(GLUtil::UniformLoc(prog, "uSrc"), 0);
         glUniform2f(GLUtil::UniformLoc(prog, "uDir"), pass == 0 ? 1.0f / mDoc.w : 0.0f, pass == 0 ? 0.0f : 1.0f / mDoc.h);
         glUniform1f(GLUtil::UniformLoc(prog, "uSigma"), std::max(0.5f, radius * 0.5f));
         glUniform1i(GLUtil::UniformLoc(prog, "uFirst"), pass == 0 ? 1 : 0);
         glUniform1i(GLUtil::UniformLoc(prog, "uLinear"), mDoc.linear ? 1 : 0);
      });
   }
   return mShBlur[1].tex;
}

// ---- the picture under a layer: histogram, auto levels ---------------------------------------------------

namespace
{
   // Downsample a texture (in working space when `finishProg != 0`, or already display-ready when 0) into a tiny
   // 128x128 RGBA8 buffer for fast histogram calculation (64 KB readback instead of 16+ MB full-res stalls).
   bool ReadDownsampled(unsigned srcTex, int srcW, int srcH, unsigned finishProg, bool linear, bool headroom,
                        std::vector<unsigned char>& px, int& outW, int& outH)
   {
      if (srcTex == 0 || srcW <= 0 || srcH <= 0)
         return false;
      const int kMaxSide = 128;
      if (srcW >= srcH)
      {
         outW = std::min(srcW, kMaxSide);
         outH = std::max(1, (int)std::lround((double)srcH * outW / srcW));
      }
      else
      {
         outH = std::min(srcH, kMaxSide);
         outW = std::max(1, (int)std::lround((double)srcW * outH / srcH));
      }
      GLUtil::Fbo smallFbo;
      if (!GLUtil::EnsureFbo(smallFbo, outW, outH, GL_RGBA8, "Scene2DHist"))
         return false;
      if (finishProg != 0)
      {
         GLUtil::RunShaderPass(smallFbo, finishProg, [&]()
         {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, srcTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glUniform1i(GLUtil::UniformLoc(finishProg, "uAcc"), 0);
            glUniform1i(GLUtil::UniformLoc(finishProg, "uLinear"), linear ? 1 : 0);
            GLUtil::SetImageHeadroom(finishProg, headroom);
         });
      }
      else
      {
         // Already display-encoded output texture: blit with linear downsample.
         GLuint srcFbo = 0;
         glGenFramebuffers(1, &srcFbo);
         GLint prevR = 0, prevD = 0;
         glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevR);
         glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevD);
         glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
         glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, smallFbo.fbo);
         glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, outW, outH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
         glBindFramebuffer(GL_READ_FRAMEBUFFER, prevR);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevD);
         glDeleteFramebuffers(1, &srcFbo);
      }
      px.assign((size_t)outW * outH * 4, 0);
      GLint prev = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, smallFbo.fbo);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);
      GLUtil::DestroyFbo(smallFbo);
      return true;
   }

   // The composite (display values, 8 bit) as the panel sees it: the document's own output (bottom-up rows).
   bool ReadOut(Scene2DNode& doc, std::vector<unsigned char>& px, int& w, int& h)
   {
      const unsigned tex = doc.GetOutputTexture();
      w = doc.GetOutputWidth();
      h = doc.GetOutputHeight();
      if (tex == 0 || w <= 0 || h <= 0)
         return false;
      px.assign((size_t)w * h * 4, 0);
      glBindTexture(GL_TEXTURE_2D, tex);
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      return true;
   }

   // The picture an adjustment works on (see Scene2DNode::AutoLevels).
   bool ReadUnder(Scene2DNode& doc, int id, int effect, std::vector<unsigned char>& px, int& w, int& h)
   {
      if (id == 0)
         return ReadOut(doc, px, w, h);
      w = doc.Doc().w;
      h = doc.Doc().h;
      return doc.PictureUnder(id, effect, 0, px) && !px.empty();
   }
}

bool Scene2DNode::Histogram(int id, std::vector<float>& bins, int count, int effect)
{
   if (count < 2)
      return false;
   std::vector<unsigned char> px;
   int w = 0, h = 0;
   if (id == 0)
   {
      if (!ReadDownsampled(GetOutputTexture(), GetOutputWidth(), GetOutputHeight(), 0, false, false, px, w, h))
         return false;
   }
   else
   {
      Layers();
      if (FindConst(id) == nullptr || !EnsureShaders() || mFinishProgram == 0)
         return false;
      SyncSurfaces();
      SyncRasters();
      const std::vector<Layer> keep = mLayers;
      const bool keepOpaque = mAccOpaque;
      const int keepSolo = soloLayer;
      if (effect < 0)
      {
         for (int at = id, level = 0; at != 0; level++)
         {
            const Layer* t = FindConst(at);
            if (t == nullptr)
               break;
            const int parent = t->parent;
            bool after = false;
            for (const Layer* k : ListOf(parent))
            {
               if (k->id == at)
               {
                  after = true;
                  if (level > 0)
                     continue;
               }
               if (after)
                  if (Layer* m = FindLayer(k->id))
                     m->visible = false;
            }
            at = parent;
         }
         soloLayer = 0;
      }
      else
      {
         for (int at = id; at != 0;)
         {
            Layer* t = FindLayer(at);
            if (t == nullptr)
               break;
            t->visible = true;
            t->opacity = 1.0f;
            if (at == id)
            {
               t->blend = 0;
               t->clip = false;
               for (size_t i = (size_t)effect; i < t->effects.size(); i++)
                  t->effects[i].on = false;
            }
            at = t->parent;
         }
         soloLayer = id;
         mAccOpaque = false;
      }
      mIsolating = true;
      const float none[4] = { 0, 0, 0, 0 };
      Clear(PairAt(0), none);
      DrawList(0, 0, 0, 1.0f);
      FlushQueue();
      mIsolating = false;
      mAccOpaque = keepOpaque;
      soloLayer = keepSolo;
      const bool ok = ReadDownsampled(PairAt(0).Tex(), mDoc.w, mDoc.h, mFinishProgram, mDoc.linear, mDoc.headroom, px, w, h);
      mLayers = keep;
      if (!ok)
         return false;
   }
   bins.assign((size_t)count, 0.0f);
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
         const unsigned char* p = &px[((size_t)y * w + x) * 4];
         if (id != 0 && p[3] == 0)
            continue;   // nothing there: not part of the picture
         const float L = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
         bins[(size_t)std::min(count - 1, (int)(L * count))] += 1.0f;
      }
   const float mx = *std::max_element(bins.begin(), bins.end());
   if (mx > 0)
      for (float& b : bins) b /= mx;
   return true;
}

bool Scene2DNode::AutoLevels(int id, float out[3][4], int effect)
{
   std::vector<unsigned char> px;
   int w = 0, h = 0;
   if (!ReadUnder(*this, id, effect, px, w, h))
      return false;
   for (int ch = 0; ch < 3; ch++)
   {
      unsigned hist[256] = {};
      size_t total = 0;
      for (size_t i = 0; i + 3 < px.size(); i += 4)
      {
         if (id != 0 && px[i + 3] == 0)
            continue;
         hist[px[i + ch]]++;
         total++;
      }
      const size_t tail = std::max<size_t>(1, total / 1000);   // 0.1 % each end
      size_t acc = 0;
      int lo = 0, hi = 255;
      for (; lo < 255 && acc + hist[lo] < tail; lo++) acc += hist[lo];
      acc = 0;
      for (; hi > 0 && acc + hist[hi] < tail; hi--) acc += hist[hi];
      if (hi <= lo)
      {
         lo = 0;
         hi = 255;
      }
      out[ch][0] = lo / 255.0f;
      out[ch][1] = hi / 255.0f;
      out[ch][2] = 0.0f;
      out[ch][3] = 1.0f;
   }
   return true;
}

// ---- adjustments from a picked pixel ----------------------------------------------------------------------

namespace
{
   float LevelsGammaCpu(float u, float g)   // the shader's LevelsGamma1
   {
      const float c = 1.0f / g;
      if (u <= 0.0f)
         return 0.0f;
      if (g <= 1.0f)
         return std::pow(u, c);
      const float a = std::exp2(g), q = 2.2f;
      const float u1 = std::pow((1.0f - c / q) / (a * (1.0f - 1.0f / q)), 1.0f / (1.0f - c));
      if (u >= u1 || u1 <= 0.0f)
         return std::pow(u, c);
      const float b = (a * u1 - std::pow(u1, c)) / std::pow(u1, q);
      if (!std::isfinite(b))
         return std::pow(u, c);
      return a * u - b * std::pow(u, q);
   }
   float LevelsStage(const float* L, float g, float v)
   {
      const float u = std::clamp((v - L[0]) / std::max(L[1] - L[0], 1e-3f), 0.0f, 1.0f);
      return L[2] + (L[3] - L[2]) * LevelsGammaCpu(u, std::max(g, 0.01f));
   }
   // The input a monotone map on [0, 1] turns into target (bisection; a falling map is walked the other way).
   template <class F> float InvertMonotone(F f, float target)
   {
      const bool up = f(1.0f) >= f(0.0f);
      float lo = 0.0f, hi = 1.0f;
      for (int i = 0; i < 32; i++)
      {
         const float mid = 0.5f * (lo + hi);
         ((f(mid) < target) == up ? lo : hi) = mid;
      }
      return 0.5f * (lo + hi);
   }
   float LutAt(const float* t, float v)   // the shader's Lut: 256 taps, linear between
   {
      const float f = std::clamp(v, 0.0f, 1.0f) * 255.0f;
      const int i = std::min((int)f, 254);
      return t[i] + (t[i + 1] - t[i]) * (f - (float)i);
   }
   const float kPointGap = 0.02f;   // closest two curve points (or Levels black and white) may sit
   // Moves a curve's first (black) or last (white) point to (x, y); points it passes are dropped.
   void SetCurveEnd(std::vector<float>& c, bool white, float x, float y)
   {
      if (c.size() < 4)
         c = { 0.0f, 0.0f, 1.0f, 1.0f };
      const int n = (int)c.size() / 2;
      std::vector<float> out;
      if (!white)
         out.insert(out.end(), { x, y });
      else
         out.insert(out.end(), { c[0], c[1] });
      for (int i = 1; i < n - 1; i++)
      {
         const float px = c[(size_t)i * 2];
         if (white ? px < x - kPointGap : px > x + kPointGap)
            out.insert(out.end(), { px, c[(size_t)i * 2 + 1] });
      }
      if (white)
         out.insert(out.end(), { x, y });
      else
         out.insert(out.end(), { c[(size_t)(n - 1) * 2], c[(size_t)(n - 1) * 2 + 1] });
      c = std::move(out);
   }
   // Puts a point at (x, y): the one already near x moves, else a new one goes in (16 at most).
   void SetCurveMid(std::vector<float>& c, float x, float y)
   {
      const int n = (int)c.size() / 2;
      if (n < 2)
         return;
      for (int i = 1; i < n - 1; i++)
         if (std::fabs(c[(size_t)i * 2] - x) < kPointGap * 1.5f)
         {
            c[(size_t)i * 2] = x;
            c[(size_t)i * 2 + 1] = y;
            return;
         }
      if (n >= 16 || x <= c[0] + kPointGap || x >= c[(size_t)(n - 1) * 2] - kPointGap)
         return;
      int at = 1;
      while (at < n - 1 && c[(size_t)at * 2] < x)
         at++;
      c.insert(c.begin() + at * 2, { x, y });
   }
   float Luma709(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
}

void Scene2DNode::WhiteBalanceFromPick(const float disp[3], float amt, float& temp, float& tint)
{
   // The pixel already has the current balance on it: that is undone first (the shader's forward step, iterated
   // back: it is close to identity), then temperature and tint are searched for the most neutral result.
   auto forward = [&](float t, float ti, const float in[3], float out[3])
   {
      float m[9];
      WhiteBalanceMatrix(t, ti, m);
      const float l[3] = { ColorSpace::SrgbToLinear(in[0]), ColorSpace::SrgbToLinear(in[1]), ColorSpace::SrgbToLinear(in[2]) };
      for (int r = 0; r < 3; r++)
      {
         const float n = std::clamp(m[r * 3] * l[0] + m[r * 3 + 1] * l[1] + m[r * 3 + 2] * l[2], 0.0f, 1.0f);
         out[r] = in[r] + (ColorSpace::LinearToSrgb(n) - in[r]) * amt;
      }
   };
   float src[3] = { disp[0], disp[1], disp[2] };
   for (int it = 0; it < 24; it++)
   {
      float f[3];
      forward(temp, tint, src, f);
      for (int k = 0; k < 3; k++)
         src[k] = std::clamp(src[k] + (disp[k] - f[k]), 0.0f, 1.0f);
   }
   auto chroma = [&](float t, float ti)
   {
      float o[3];
      forward(t, ti, src, o);
      const float mean = std::max(1e-3f, (o[0] + o[1] + o[2]) / 3.0f);
      float e = 0.0f;
      for (float v : o)
         e += (v - mean) * (v - mean);
      return e / (mean * mean);
   };
   float bt = 0.0f, bti = 0.0f, best = chroma(0.0f, 0.0f);
   for (int i = 0; i <= 40; i++)   // a coarse grid over [-1, 1]^2, then a shrinking local search
      for (int j = 0; j <= 40; j++)
      {
         const float t = -1.0f + (float)i * 0.05f, ti = -1.0f + (float)j * 0.05f, e = chroma(t, ti);
         if (e < best)
            best = e, bt = t, bti = ti;
      }
   for (float step = 0.025f; step > 0.001f; step *= 0.5f)
      for (int i = -2; i <= 2; i++)
         for (int j = -2; j <= 2; j++)
         {
            const float t = std::clamp(bt + (float)i * step, -1.0f, 1.0f), ti = std::clamp(bti + (float)j * step, -1.0f, 1.0f);
            const float e = chroma(t, ti);
            if (e < best)
               best = e, bt = t, bti = ti;
         }
   temp = bt;
   tint = bti;
}

void Scene2DNode::PointFromPick(int adjType, float adjAmt, float levels[4], float levelsCh[3][4], std::vector<float>& adjP,
                                std::vector<float>& curve, std::vector<float> curveCh[3], const float disp[3], int point)
{
   // The picked pixel already has the adjustment on it: each channel's forward map is run backwards to the colour
   // that went in, and the point is set from that (Photoshop's eyedroppers act on the colour before the adjustment).
   const std::vector<float> def = AdjParamDefault(adjType);
   for (size_t i = adjP.size(); i < def.size(); i++)
      adjP.push_back(def[i]);
   if (adjType == kAdjLevels)
   {
      if (adjP.size() < 3)
         return;
      float e[3], o[3];
      for (int ch = 0; ch < 3; ch++)
      {
         const float* C = levelsCh[ch];
         const float g = adjP[(size_t)ch];
         auto f = [&](float v) { return LevelsStage(C, g, LevelsStage(levels, adjAmt, v)); };
         const float pre = InvertMonotone(f, disp[ch]);
         e[ch] = LevelsStage(levels, adjAmt, pre);   // what reaches the channel's own levels
         o[ch] = f(pre);
      }
      const float target = Luma709(o);
      for (int ch = 0; ch < 3; ch++)
      {
         float* C = levelsCh[ch];
         if (point == 1)
            C[0] = std::clamp(e[ch], 0.0f, C[1] - kPointGap);
         else if (point == 3)
            C[1] = std::clamp(e[ch], C[0] + kPointGap, 1.0f);
         else
         {
            const float u = std::clamp((e[ch] - C[0]) / std::max(C[1] - C[0], 1e-3f), 1e-4f, 1.0f - 1e-4f);
            const float want = std::clamp((target - C[2]) / std::max(C[3] - C[2], 1e-3f), 1e-4f, 1.0f - 1e-4f);
            float lo = 0.1f, hi = 4.0f;   // the per-channel gamma slider's range
            for (int i = 0; i < 32; i++)
            {
               const float mid = 0.5f * (lo + hi);
               (LevelsGammaCpu(u, mid) < want ? lo : hi) = mid;
            }
            adjP[(size_t)ch] = 0.5f * (lo + hi);
         }
      }
      return;
   }
   if (adjType != kAdjCurves)
      return;
   const bool lumaOnly = !adjP.empty() && adjP[0] > 0.5f;
   const float amt = std::clamp(adjAmt, 0.0f, 1.0f);
   float master[256];
   CurveLut(curve, master, 256);
   const float lo = 0.05f, hi = 0.95f;   // a black / white / grey point's x stays this far inside the curve
   if (lumaOnly)
   {
      const float pre = InvertMonotone([&](float v) { return v + (LutAt(master, v) - v) * amt; }, Luma709(disp));
      if (point == 1)
         SetCurveEnd(curve, false, std::clamp(pre, 0.0f, hi), 0.0f);
      else if (point == 3)
         SetCurveEnd(curve, true, std::clamp(pre, lo, 1.0f), 1.0f);
      return;   // a neutral grey means nothing on luma alone
   }
   float e[3], o[3];
   for (int ch = 0; ch < 3; ch++)
   {
      float chLut[256];
      CurveLut(curveCh[ch], chLut, 256);
      auto f = [&](float v) { return v + (LutAt(chLut, LutAt(master, v)) - v) * amt; };
      const float pre = InvertMonotone(f, disp[ch]);
      e[ch] = LutAt(master, pre);   // what reaches the channel's own curve
      o[ch] = f(pre);
   }
   const float target = Luma709(o);
   for (int ch = 0; ch < 3; ch++)
   {
      if (point == 1)
         SetCurveEnd(curveCh[ch], false, std::clamp(e[ch], 0.0f, hi), 0.0f);
      else if (point == 3)
         SetCurveEnd(curveCh[ch], true, std::clamp(e[ch], lo, 1.0f), 1.0f);
      else
         SetCurveMid(curveCh[ch], std::clamp(e[ch], lo, hi), target);
   }
}
