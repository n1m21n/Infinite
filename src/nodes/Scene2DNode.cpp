#include "Scene2DNode.h"
#include <map>

#include "gl3.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <sstream>

#include "BlendModes.h"
#include "Geometry3DNodes.h"
#include "Modulation.h"
#include "NodeViewport.h"
#include "TileStore.h"

std::function<INode*(uint64_t)> Scene2DNode::ResolveUid;

const char* Scene2DNode::AdjName(int type)
{
   static const char* kNames[kAdjCount] = { "Pixelate", "Halftone", "Dither", "Threshold", "Gradient Map", "Posterize" };
   return kNames[std::clamp(type, 0, kAdjCount - 1)];
}

float Scene2DNode::AdjDefault(int type)
{
   static const float kDef[kAdjCount] = { 16.0f, 10.0f, 3.0f, 0.5f, 0.0f, 4.0f };
   return kDef[std::clamp(type, 0, kAdjCount - 1)];
}

void Scene2DNode::AdjRange(int type, float& lo, float& hi)
{
   static const float kLo[kAdjCount] = { 1.0f, 2.0f, 2.0f, 0.0f, 0.0f, 2.0f };
   static const float kHi[kAdjCount] = { 256.0f, 128.0f, 16.0f, 1.0f, 1.0f, 32.0f };
   const int t = std::clamp(type, 0, kAdjCount - 1);
   lo = kLo[t];
   hi = kHi[t];
}

float Scene2DNode::AdjAmount(const Layer& l) const
{
   if (l.adjDrive < 1 || l.adjDrive > kDrives)
      return l.adjAmt;
   const float k = std::clamp(drive[l.adjDrive - 1], 0.0f, 1.0f);
   return l.adjAmt + (l.adjTo - l.adjAmt) * k;
}

namespace
{
   const std::string& PassSrc()
   {
      static const std::string src =
         std::string(
            "#version 150\n"
            "in vec2 vUv;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D uAcc;\n"
            "uniform sampler2D uTex;\n"
            "uniform sampler2D uMask;\n"
            "uniform vec2 uDoc;\n"
            "uniform vec3 uInvA;\n"
            "uniform vec3 uInvB;\n"
            "uniform vec3 uInvC;\n"
            "out float fragDepth;\n"
            "uniform sampler2D uDepthIn;\n"
            "uniform int uDepthMode;\n"
            "uniform int uBif;\n"
            "uniform vec4 uBifT;\n"
            "uniform vec4 uBifU;\n"
            "uniform vec2 uBox;\n"
            "uniform vec4 uCrop;\n"
            "uniform vec2 uTexSize;\n"
            "uniform int uKind;\n"
            "uniform int uMode;\n"
            "uniform int uClip;\n"
            "uniform int uNearest;\n"
            "uniform int uLinear;\n"
            "uniform int uSrgb;\n"
            "uniform int uHasMask;\n"
            "uniform int uMaskChan;\n"
            "uniform int uMaskInv;\n"
            "uniform int uHead;\n"
            "uniform float uOpacity;\n"
            "uniform float uMaskDensity;\n"
            "uniform vec4 uColA;\n"
            "uniform vec4 uColB;\n"
            "uniform float uFillAngle;\n"
            "uniform sampler2D uPMask;\n"
            "uniform int uHasPMask;\n"
            "uniform sampler2D uVMask;\n"
            "uniform int uHasVMask;\n")
         + GLUtil::kSrgbGlsl
         + BlendModes::kBlendGLSL
         + "float ramp(float lo, float hi, float v) { return hi > lo + 1e-4 ? clamp((v - lo) / (hi - lo), 0.0, 1.0) : step(lo, v); }\n"
           "float bifW(vec4 t, float l) { return ramp(t.x, t.y, l) * (1.0 - ramp(t.z, t.w, l)); }\n"
           "float gLum(vec3 c) { c = clamp(c, 0.0, 1.0); return Lum(uLinear == 1 ? SrgbEncode(c) : c); }\n"
           "vec4 layerAt(vec2 uvb, vec2 uv) {\n"
           "   if (uvb.x < uCrop.x || uvb.x > uCrop.z || uvb.y < uCrop.y || uvb.y > uCrop.w) return vec4(0.0);\n"
           "   if (uKind == 1) return uColA;\n"
           "   if (uKind == 2) {\n"
           "      float a = radians(uFillAngle);\n"
           "      vec2 d = vec2(cos(a), sin(a));\n"
           "      float t = clamp(dot(uvb - 0.5, d) / (abs(d.x) + abs(d.y)) * 2.0 * 0.5 + 0.5, 0.0, 1.0);\n"
           "      return mix(uColA, uColB, t);\n"
           "   }\n"
           "   if (uKind == 3) return mix(uColA, uColB, clamp(length(uvb - 0.5) * 2.0, 0.0, 1.0));\n"
           "   if (uNearest == 1) {\n"
           "      ivec2 px = ivec2(clamp(floor(uv * uTexSize), vec2(0.0), uTexSize - 1.0));\n"
           "      return texelFetch(uTex, px, 0);\n"
           "   }\n"
           "   return texture(uTex, uv);\n"
           "}\n"
           "void main() {\n"
           "   vec4 acc = texture(uAcc, vUv);\n"
           "   float dIn = uDepthMode != 0 ? texture(uDepthIn, vUv).r : 1e30;\n"
           "   fragDepth = dIn;\n"
           "   vec2 p = vec2(vUv.x * uDoc.x, (1.0 - vUv.y) * uDoc.y);\n"
           "   vec3 q = vec3(dot(uInvA, vec3(p, 1.0)), dot(uInvB, vec3(p, 1.0)), dot(uInvC, vec3(p, 1.0)));\n"
           "   if (q.z <= 1e-12) { fragColor = acc; return; }\n"
           "   vec2 b = q.xy / q.z;\n"
           "   vec2 uvb = b / uBox;\n"
           "   vec2 uv = vec2(uvb.x, 1.0 - uvb.y);\n"
           "   vec4 layer = layerAt(uvb, uv);\n"
           "   if (uLinear == 1 && uSrgb == 1) layer.rgb = SrgbDecode(layer.rgb);\n"
           "   if (uHasMask == 1) {\n"
           "      vec4 m = texture(uMask, uv);\n"
           "      float v = uMaskChan == 0 ? Lum(m.rgb) * m.a : m.a;\n"
           "      if (uMaskInv == 1) v = 1.0 - v;\n"
           "      layer.a *= mix(1.0, clamp(v, 0.0, 1.0), uMaskDensity);\n"
           "   }\n"
           "   if (uHasPMask == 1) layer.a *= texture(uPMask, uv).r;\n"
           "   if (uHasVMask == 1) layer.a *= texture(uVMask, uv).a;\n"
           "   float amt = uOpacity * layer.a;\n"
           "   if (uBif == 1) amt *= bifW(uBifT, gLum(layer.rgb)) * bifW(uBifU, gLum(acc.rgb));\n"
           "   if (uDepthMode == 1) {\n"
           "      float d = 1.0 / q.z;\n"
           "      if (d > dIn * 1.0005 + 0.01) { fragColor = acc; return; }\n"
           "      if (layer.a > 0.5) fragDepth = min(dIn, d);\n"
           "   }\n"
           "   if (uMode == " + std::to_string(BlendModes::kEraseMode) + ") { fragColor = vec4(acc.rgb, acc.a * (1.0 - amt)); return; }\n"
           "   if (uMode == " + std::to_string(BlendModes::kAntiEraseMode) + ") { fragColor = vec4(acc.rgb, acc.a * (1.0 - (1.0 - layer.a) * uOpacity)); return; }\n"
           "   vec3 blended = blendModeImage(uMode, acc.rgb, layer.rgb, 1, uHead);\n"
           "   vec3 cs = mix(layer.rgb, blended, acc.a);\n"
           "   if (uClip == 1) { fragColor = vec4(mix(acc.rgb, cs, amt), acc.a); return; }\n"
           "   float ar = amt + acc.a * (1.0 - amt);\n"
           "   vec3 cr = (ar > 1e-5) ? (cs * amt + acc.rgb * acc.a * (1.0 - amt)) / ar : vec3(0.0);\n"
           "   fragColor = vec4(cr, ar);\n"
           "}\n";
      return src;
   }

   const std::string& FinishSrc()
   {
      static const std::string src = std::string(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uAcc;\n"
         "uniform int uLinear;\n")
         + GLUtil::kSrgbGlsl +
         "void main() { fragColor = texture(uAcc, vUv); }\n";
      return src;
   }

   // Adjustment layers: the picture below with one effect, in display values.
   const std::string& AdjustSrc()
   {
      static const std::string src = std::string(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uSrc;\n"
         "uniform vec2 uDoc;\n"
         "uniform int uType;\n"
         "uniform float uAmt;\n"
         "uniform int uMap;\n"
         "uniform vec4 uColA;\n"
         "uniform vec4 uColB;\n"
         "uniform int uLinear;\n")
         + GLUtil::kSrgbGlsl +
         "vec3 Disp(vec4 c) { return uLinear == 1 ? SrgbEncode(max(c.rgb, 0.0)) : c.rgb; }\n"
         "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
         "const int kBayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);\n"
         "void main() {\n"
         "   vec2 px = gl_FragCoord.xy;\n"
         "   vec4 src = texture(uSrc, vUv);\n"
         "   vec3 c = Disp(src);\n"
         "   if (uType == 0) {\n"
         "      float b = max(1.0, uAmt);\n"
         "      vec2 q = (floor(px / b) + 0.5) * b;\n"
         "      c = Disp(texture(uSrc, clamp(q / uDoc, vec2(0.0), vec2(1.0))));\n"
         "   } else if (uType == 1) {\n"
         "      float cell = max(2.0, uAmt);\n"
         "      mat2 r = mat2(0.7071, 0.7071, -0.7071, 0.7071);\n"
         "      vec2 p = r * px;\n"
         "      vec2 ctr = (floor(p / cell) + 0.5) * cell;\n"
         "      vec2 cpx = transpose(r) * ctr;\n"
         "      float L = Luma(Disp(texture(uSrc, clamp(cpx / uDoc, vec2(0.0), vec2(1.0)))));\n"
         "      float rad = sqrt(clamp(1.0 - L, 0.0, 1.0)) * cell * 0.7071;\n"
         "      float d = length(p - ctr);\n"
         "      c = vec3(smoothstep(rad - 0.75, rad + 0.75, d));\n"
         "   } else if (uType == 2) {\n"
         "      float n = max(2.0, floor(uAmt + 0.5));\n"
         "      ivec2 ip = ivec2(mod(px, 4.0));\n"
         "      float t = (float(kBayer[ip.y * 4 + ip.x]) + 0.5) / 16.0;\n"
         "      c = floor(c * (n - 1.0) + t) / (n - 1.0);\n"
         "   } else if (uType == 3) {\n"
         "      c = vec3(step(uAmt, Luma(c)));\n"
         "   } else if (uType == 5) {\n"
         "      float n = max(2.0, floor(uAmt + 0.5));\n"
         "      c = floor(c * n) / (n - 1.0);\n"
         "      c = min(c, vec3(1.0));\n"
         "   }\n"
         "   if (uType == 4 || uMap == 1) c = mix(uColA.rgb, uColB.rgb, clamp(Luma(c), 0.0, 1.0));\n"
         "   c = clamp(c, 0.0, 1.0);\n"
         "   fragColor = vec4(uLinear == 1 ? SrgbDecode(c) : c, src.a);\n"
         "}\n";
      return src;
   }

   float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

   // 3x3 inverse by the adjugate; the sign is kept so w stays positive in front of the camera.
   void Invert3(const float m[9], float o[9])
   {
      o[0] = m[4] * m[8] - m[5] * m[7];
      o[1] = m[2] * m[7] - m[1] * m[8];
      o[2] = m[1] * m[5] - m[2] * m[4];
      o[3] = m[5] * m[6] - m[3] * m[8];
      o[4] = m[0] * m[8] - m[2] * m[6];
      o[5] = m[2] * m[3] - m[0] * m[5];
      o[6] = m[3] * m[7] - m[4] * m[6];
      o[7] = m[1] * m[6] - m[0] * m[7];
      o[8] = m[0] * m[4] - m[1] * m[3];
      const float det = m[0] * o[0] + m[1] * o[3] + m[2] * o[6];
      const float id = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
      for (int i = 0; i < 9; i++)
         o[i] *= id;
   }

   unsigned long long Mix(unsigned long long h, unsigned long long v) { return (h ^ v) * 1099511628211ull; }
}

const std::vector<std::string>& Scene2DNode::FitNames()
{
   static const std::vector<std::string> names = { "None", "Fit", "Fill", "Stretch" };
   return names;
}

Scene2DNode::~Scene2DNode()
{
   for (Pair& p : mPool)
   {
      GLUtil::DestroyFbo(p.f[0]);
      GLUtil::DestroyFbo(p.f[1]);
   }
   GLUtil::DestroyFbo(mOut);
   GLUtil::DestroyFbo(mAdjFbo);
   GLUtil::DestroyFbo(mBelow);
   for (auto& kv : mSurfaces)
      GLUtil::DestroyFbo(kv.second.fbo);
   for (auto& kv : mMasks)
      GLUtil::DestroyFbo(kv.second.fbo);
   for (auto* m : { &mVec, &mVMasks })
      for (auto& kv : *m)
         if (kv.second.tex != 0)
            glDeleteTextures(1, &kv.second.tex);
   GLUtil::DestroyFbo(mSelTex);
   GLUtil::DestroyFbo(mSelOverlay);
   GLUtil::DestroyFbo(mSelScratch);
   for (unsigned p : { mFillProgram, mAntsProgram, mResampleProgram, mAdjProgram })
      if (p != 0)
         glDeleteProgram(p);
   if (mPassProgram != 0)
      glDeleteProgram(mPassProgram);
   if (mFinishProgram != 0)
      glDeleteProgram(mFinishProgram);
   for (auto& kv : mDepth)
      for (unsigned t : kv.second.tex)
         if (t != 0)
            glDeleteTextures(1, &t);
   GLUtil::DestroyFbo(mFxBlur[0]);
   GLUtil::DestroyFbo(mFxBlur[1]);
   GLUtil::DestroyFbo(mFxOut);
   for (unsigned p : { mBlurProgram, mFxProgram })
      if (p != 0)
         glDeleteProgram(p);
}

// ---- model --------------------------------------------------------------

const Scene2DNode::Layer* Scene2DNode::FindConst(int id) const
{
   for (const Layer& l : mLayers)
      if (l.id == id)
         return &l;
   return nullptr;
}

Scene2DNode::Layer* Scene2DNode::FindLayer(int id)
{
   Layers();
   for (Layer& l : mLayers)
      if (l.id == id)
         return &l;
   return nullptr;
}

Scene2DNode::Layer& Scene2DNode::AddLayer(Kind kind, uint64_t uid, int above)
{
   Layers();
   Layer l;
   l.id = mNextId++;
   l.kind = kind;
   l.uid = uid;
   l.x = mDoc.w * 0.5f;
   l.y = mDoc.h * 0.5f;
   if (kind == Kind::Group)
      l.blend = kPassThrough;
   if (kind == Kind::Adjust)
   {
      const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
      std::copy(black, black + 4, l.colorA);
      std::copy(white, white + 4, l.colorB);
   }
   if (kind == Kind::Vector)
   {
      l.pw = mDoc.w;
      l.ph = mDoc.h;
   }
   if (kind == Kind::Paint)
   {
      l.pw = mDoc.w;
      l.ph = mDoc.h;
      const int n = ((l.pw + TileStore::kTile - 1) / TileStore::kTile) * ((l.ph + TileStore::kTile - 1) / TileStore::kTile);
      l.tiles.assign(n, "-");
   }
   int at = (int)mLayers.size();
   if (const Layer* ref = FindConst(above))
   {
      l.parent = ref->parent;
      for (int i = 0; i < (int)mLayers.size(); i++)
         if (mLayers[i].id == above)
            at = i + 1;
      // Past the reference's own children when it is a group.
      while (at < (int)mLayers.size() && IsInside(mLayers[at].id, above))
         at++;
   }
   mLayers.insert(mLayers.begin() + at, l);
   Commit();
   return *FindLayer(l.id);
}

void Scene2DNode::RemoveLayer(int id)
{
   Layers();
   mLayers.erase(std::remove_if(mLayers.begin(), mLayers.end(), [&](const Layer& l) { return IsInside(l.id, id); }),
                 mLayers.end());
   Commit();
}

std::vector<int> Scene2DNode::ChildrenOf(int parent)
{
   std::vector<int> out;
   for (const Layer& l : Layers())
      if (l.parent == parent)
         out.push_back(l.id);
   return out;
}

bool Scene2DNode::IsInside(int id, int group)
{
   for (int hops = 0; id != 0 && hops < 64; hops++)
   {
      if (id == group)
         return true;
      const Layer* l = FindConst(id);
      id = l != nullptr ? l->parent : 0;
   }
   return false;
}

int Scene2DNode::Depth(int id)
{
   int d = 0;
   const Layer* l = FindConst(id);
   while (l != nullptr && l->parent != 0 && d < 64)
   {
      d++;
      l = FindConst(l->parent);
   }
   return d;
}

void Scene2DNode::MoveLayer(int id, int target, bool below, bool into)
{
   Layers();
   if (id == target || FindConst(id) == nullptr || FindConst(target) == nullptr || IsInside(target, id))
      return;
   // Pull out the moving layer and everything under it, in order.
   std::vector<Layer> moving, rest;
   for (const Layer& l : mLayers)
      (IsInside(l.id, id) ? moving : rest).push_back(l);
   mLayers.swap(rest);
   const Layer* t = FindConst(target);
   const int newParent = into ? target : t->parent;
   int at = 0;
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (mLayers[i].id == target)
         at = i;
   if (into || !below)
   {
      // Above the target: past it and its children.
      at++;
      while (at < (int)mLayers.size() && IsInside(mLayers[at].id, target))
         at++;
   }
   else
   {
      // Below the target: before its first child.
      while (at > 0 && IsInside(mLayers[at - 1].id, target))
         at--;
   }
   moving[0].parent = newParent;
   mLayers.insert(mLayers.begin() + at, moving.begin(), moving.end());
   Commit();
}

int Scene2DNode::GroupLayers(const std::vector<int>& ids)
{
   Layers();
   if (ids.empty())
      return 0;
   // The group takes the place of the topmost picked layer.
   int top = ids[0], topIndex = -1;
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (std::find(ids.begin(), ids.end(), mLayers[i].id) != ids.end() && i > topIndex)
      {
         topIndex = i;
         top = mLayers[i].id;
      }
   const int gid = AddLayer(Kind::Group, 0, top).id;
   Layer* g = FindLayer(gid);
   g->name = "Group " + std::to_string(gid);
   // Move each picked layer (bottom first) into the group's top, keeping their order.
   std::vector<int> order;
   for (const Layer& l : mLayers)
      if (std::find(ids.begin(), ids.end(), l.id) != ids.end())
         order.push_back(l.id);
   for (int id : order)
   {
      bool parentPicked = false;
      for (int other : order)
         if (other != id && IsInside(id, other))
            parentPicked = true;
      if (!parentPicked)
         MoveLayer(id, gid, false, true);
   }
   Commit();
   return gid;
}

void Scene2DNode::Ungroup(int group)
{
   Layers();
   const Layer* g = FindConst(group);
   if (g == nullptr || g->kind != Kind::Group)
      return;
   const int parent = g->parent;
   for (Layer& l : mLayers)
      if (l.parent == group)
         l.parent = parent;
   mLayers.erase(std::remove_if(mLayers.begin(), mLayers.end(), [&](const Layer& l) { return l.id == group; }),
                 mLayers.end());
   Commit();
}

int Scene2DNode::Duplicate(int id)
{
   Layers();
   if (FindConst(id) == nullptr)
      return 0;
   std::vector<Layer> copies;
   std::vector<std::pair<int, int>> remap;
   int last = -1;
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (IsInside(mLayers[i].id, id))
      {
         Layer c = mLayers[i];
         remap.push_back({ c.id, mNextId });
         c.id = mNextId++;
         copies.push_back(c);
         last = i;
      }
   for (Layer& c : copies)
      for (auto& r : remap)
         if (c.parent == r.first)
            c.parent = r.second;
   if (!copies.empty() && !copies[0].name.empty())
      copies[0].name += " copy";
   mLayers.insert(mLayers.begin() + last + 1, copies.begin(), copies.end());
   Commit();
   return copies.empty() ? 0 : copies[0].id;
}

std::vector<Scene2DNode::Layer> Scene2DNode::CopyLayers(const std::vector<int>& ids)
{
   Layers();
   std::vector<Layer> out;
   for (const Layer& l : mLayers)
      for (int id : ids)
         if (IsInside(l.id, id))
         {
            out.push_back(l);
            break;
         }
   return out;
}

std::vector<int> Scene2DNode::PasteLayers(const std::vector<Layer>& clip, int above, float dx, float dy)
{
   Layers();
   std::vector<int> tops;
   if (clip.empty())
      return tops;
   std::vector<std::pair<int, int>> remap;
   std::vector<Layer> copies;
   for (const Layer& src : clip)
   {
      Layer c = src;
      remap.push_back({ c.id, mNextId });
      c.id = mNextId++;
      copies.push_back(c);
   }
   int parentOfPaste = 0;
   if (const Layer* a = FindConst(above))
      parentOfPaste = a->parent;
   for (Layer& c : copies)
   {
      bool inside = false;
      for (auto& r : remap)
         if (c.parent == r.first)
         {
            c.parent = r.second;
            inside = true;
            break;
         }
      if (!inside)
      {
         c.parent = parentOfPaste;
         c.x += dx;
         c.y += dy;
         tops.push_back(c.id);
      }
   }
   int at = (int)mLayers.size();
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (mLayers[i].id == above || IsInside(mLayers[i].id, above))
         at = i + 1;
   mLayers.insert(mLayers.begin() + at, copies.begin(), copies.end());
   Commit();
   return tops;
}

INode* Scene2DNode::Source(const Layer& l) const
{
   if (l.kind != Kind::Node || l.uid == 0 || !ResolveUid)
      return nullptr;
   INode* node = ResolveUid(l.uid);
   // Bypass walks the same way a cable does.
   for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
      node = node->BypassSource();
   return (node != nullptr && !node->bypassed && node != this) ? node : nullptr;
}

INode* Scene2DNode::MaskSource(const Layer& l) const
{
   if (l.maskUid == 0 || !ResolveUid)
      return nullptr;
   INode* node = ResolveUid(l.maskUid);
   for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
      node = node->BypassSource();
   return (node != nullptr && !node->bypassed && node != this) ? node : nullptr;
}

bool Scene2DNode::LayerBox(const Layer& l, float& w, float& h)
{
   const float W = (float)mDoc.w, H = (float)mDoc.h;
   if (l.kind == Kind::Paint || l.kind == Kind::Vector)
   {
      w = (float)std::max(1, l.pw);
      h = (float)std::max(1, l.ph);
      return true;
   }
   if (l.kind == Kind::Text)
   {
      int tw = 0, th = 0;
      const bool ok = TextSize(l, tw, th);
      w = (float)std::max(1, tw);
      h = (float)std::max(1, th);
      return ok;
   }
   if (l.kind != Kind::Node)
   {
      w = W;
      h = H;
      return true;
   }
   INode* src = Source(l);
   float sw = src != nullptr ? (float)src->GetOutputWidth() : 0.0f;
   float sh = src != nullptr ? (float)src->GetOutputHeight() : 0.0f;
   if (dynamic_cast<IGeometrySource*>(src) != nullptr)
      sw = sh = (float)std::clamp(std::min(mDoc.w, mDoc.h), 64, 2048);   // a solo render, square

   if (sw <= 0.0f || sh <= 0.0f)
   {
      w = W;
      h = H;
      return false;
   }
   if (l.fit == kFitStretch)
   {
      w = W;
      h = H;
   }
   else if (l.fit == kFitNone)
   {
      w = sw;
      h = sh;
   }
   else
   {
      const float k = l.fit == kFitFit ? std::min(W / sw, H / sh) : std::max(W / sw, H / sh);
      w = sw * k;
      h = sh * k;
   }
   return true;
}

void Scene2DNode::LayerMatrix(const Layer& l, float m[6])
{
   if (l.kind == Kind::Fill || l.kind == Kind::Group || l.kind == Kind::Adjust)
   {
      // Fills, groups and adjustments are the document itself.
      const float id[6] = { 1, 0, 0, 0, 1, 0 };
      std::copy(id, id + 6, m);
      return;
   }
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   const float a = l.angle * 3.14159265f / 180.0f;
   const float c = std::cos(a), s = std::sin(a);
   const float kx = l.sx * (l.flipX ? -1.0f : 1.0f), ky = l.sy * (l.flipY ? -1.0f : 1.0f);
   m[0] = c * kx;
   m[1] = -s * ky;
   m[3] = s * kx;
   m[4] = c * ky;
   // The anchor point of the box lands on (x, y); a flip turns about the anchor.
   const float px = l.ax * bw, py = l.ay * bh;
   m[2] = l.x - (m[0] * px + m[1] * py);
   m[5] = l.y - (m[3] * px + m[4] * py);
}

bool Scene2DNode::Corners(const Layer& l, float out[8])
{
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   float h[9];
   const bool front = LayerHomography(l, h);
   float x0 = 0, y0 = 0, x1 = bw, y1 = bh;
   if (l.cropOn)
   {
      x0 = std::min(bw, l.crop[0]);
      y0 = std::min(bh, l.crop[1]);
      x1 = std::max(x0, bw - l.crop[2]);
      y1 = std::max(y0, bh - l.crop[3]);
   }
   const float pts[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
   for (int i = 0; i < 4; i++)
   {
      // A corner behind the camera is held just in front of it, so handles stay finite.
      const float w = std::max(1e-3f, h[6] * pts[i * 2] + h[7] * pts[i * 2 + 1] + h[8]);
      out[i * 2] = (h[0] * pts[i * 2] + h[1] * pts[i * 2 + 1] + h[2]) / w;
      out[i * 2 + 1] = (h[3] * pts[i * 2] + h[4] * pts[i * 2 + 1] + h[5]) / w;
   }
   return front;
}

bool Scene2DNode::Bounds(int id, float lo[2], float hi[2])
{
   const Layer* l = FindLayer(id);
   if (l == nullptr)
      return false;
   lo[0] = lo[1] = 1e30f;
   hi[0] = hi[1] = -1e30f;
   bool any = false;
   auto add = [&](const Layer& one)
   {
      float c[8];
      Corners(one, c);
      for (int i = 0; i < 4; i++)
      {
         lo[0] = std::min(lo[0], c[i * 2]);
         lo[1] = std::min(lo[1], c[i * 2 + 1]);
         hi[0] = std::max(hi[0], c[i * 2]);
         hi[1] = std::max(hi[1], c[i * 2 + 1]);
      }
      any = true;
   };
   if (l->kind == Kind::Group)
   {
      for (const Layer& one : mLayers)
         if (one.kind != Kind::Group && one.id != id && IsInside(one.id, id))
            add(one);
   }
   else
      add(*l);
   return any;
}

bool Scene2DNode::Shown(const Layer& l)
{
   if (soloLayer != 0 && FindConst(soloLayer) != nullptr && !IsInside(l.id, soloLayer) && !IsInside(soloLayer, l.id))
      return false;
   const Layer* p = &l;
   for (int hops = 0; p != nullptr && hops < 64; hops++)
   {
      if (!p->visible)
         return false;
      p = p->parent != 0 ? FindConst(p->parent) : nullptr;
   }
   return true;
}

// ---- document edits -----------------------------------------------------

namespace
{
   // A Fit / Fill / Stretch layer is sized off the document, so before the document
   // changes size each one is turned into the plain scale it shows at now.
   void BakeFit(Scene2DNode& doc, Scene2DNode::Layer& l)
   {
      if (l.kind != Scene2DNode::Kind::Node || l.fit == Scene2DNode::kFitNone)
         return;
      INode* src = doc.Source(l);
      if (src == nullptr || src->GetOutputWidth() <= 0 || src->GetOutputHeight() <= 0)
         return;
      float bw = 0, bh = 0;
      doc.LayerBox(l, bw, bh);
      const float kx = bw / (float)src->GetOutputWidth(), ky = bh / (float)src->GetOutputHeight();
      l.fit = Scene2DNode::kFitNone;
      l.sx *= kx;
      l.sy *= ky;
      for (int i = 0; i < 4; i++)
         l.crop[i] /= (i % 2 == 0) ? kx : ky;
   }
}

void Scene2DNode::CanvasSize(int w, int h, int anchor)
{
   Layers();
   w = std::clamp(w, 1, GLUtil::kMaxTargetSide);
   h = std::clamp(h, 1, GLUtil::kMaxTargetSide);
   const float dx = (float)(w - mDoc.w) * (float)(anchor % 3) * 0.5f;
   const float dy = (float)(h - mDoc.h) * (float)(anchor / 3) * 0.5f;
   for (Layer& l : mLayers)
   {
      BakeFit(*this, l);
      l.x += dx;
      l.y += dy;
   }
   for (float& g : mDoc.guidesX) g += dx;
   for (float& g : mDoc.guidesY) g += dy;
   mDoc.w = w;
   mDoc.h = h;
   Commit();
}

void Scene2DNode::ImageSize(int w, int h)
{
   Layers();
   w = std::clamp(w, 1, GLUtil::kMaxTargetSide);
   h = std::clamp(h, 1, GLUtil::kMaxTargetSide);
   const float kx = (float)w / (float)mDoc.w, ky = (float)h / (float)mDoc.h;
   for (Layer& l : mLayers)
   {
      BakeFit(*this, l);
      l.x *= kx;
      l.y *= ky;
      l.sx *= kx;
      l.sy *= ky;
   }
   for (float& g : mDoc.guidesX) g *= kx;
   for (float& g : mDoc.guidesY) g *= ky;
   mDoc.w = w;
   mDoc.h = h;
   Commit();
}

void Scene2DNode::CropTo(float x0, float y0, float x1, float y1)
{
   Layers();
   if (x1 < x0) std::swap(x0, x1);
   if (y1 < y0) std::swap(y0, y1);
   const int w = std::clamp((int)std::lround(x1 - x0), 1, GLUtil::kMaxTargetSide);
   const int h = std::clamp((int)std::lround(y1 - y0), 1, GLUtil::kMaxTargetSide);
   for (Layer& l : mLayers)
   {
      BakeFit(*this, l);
      l.x -= x0;
      l.y -= y0;
   }
   for (float& g : mDoc.guidesX) g -= x0;
   for (float& g : mDoc.guidesY) g -= y0;
   mDoc.w = w;
   mDoc.h = h;
   Commit();
}

// ---- saved text ---------------------------------------------------------

void Scene2DNode::Commit()
{
   Serialize();
}

// One line per fact, like the 3D View, so a patch diff reads and an agent can write it by hand.
void Scene2DNode::Serialize()
{
   std::ostringstream o;
   const Document& d = mDoc;
   o << "next " << mNextId << "\n";
   if (panelOnly)
      o << "panel 1\n";
   o << "doc " << d.w << " " << d.h << " " << d.background << " " << d.bgColor[0] << " " << d.bgColor[1] << " "
     << d.bgColor[2] << " " << +d.ratioLock << " " << +d.rulers << " " << +d.grid << " " << +d.snap << " "
     << +d.guidesOn << " " << d.gridSize << " " << d.gridSubdiv << " " << d.fps << " " << d.seconds << " "
     << +d.linear << " " << +d.headroom << "\n";
   if (!d.guidesX.empty())
   {
      o << "guidesx";
      for (float g : d.guidesX) o << " " << g;
      o << "\n";
   }
   if (!d.guidesY.empty())
   {
      o << "guidesy";
      for (float g : d.guidesY) o << " " << g;
      o << "\n";
   }
   if (d.camUid != 0)
      o << "cam " << d.camUid << "\n";
   if (d.symmetry != 0 || d.symN != 6)
      o << "sym " << d.symmetry << " " << d.symN << "\n";
   for (const Layer& l : mLayers)
   {
      o << "layer " << l.id << " " << (int)l.kind << " " << l.parent << " " << l.uid << " " << +l.visible << " "
        << l.blend << " " << l.opacity << " " << l.fill << " " << l.lock << " " << l.label << " " << +l.clip << " "
        << +l.open << "\n";
      o << "xf " << l.id << " " << l.x << " " << l.y << " " << l.sx << " " << l.sy << " " << l.angle << " " << l.ax
        << " " << l.ay << " " << +l.flipX << " " << +l.flipY << " " << l.fit << " " << +l.nearest << "\n";
      if (!l.name.empty())
         o << "name " << l.id << " " << l.name << "\n";
      if (l.cropOn || l.crop[0] != 0 || l.crop[1] != 0 || l.crop[2] != 0 || l.crop[3] != 0)
         o << "crop " << l.id << " " << +l.cropOn << " " << l.crop[0] << " " << l.crop[1] << " " << l.crop[2] << " "
           << l.crop[3] << "\n";
      if (l.maskUid != 0)
         o << "mask " << l.id << " " << l.maskUid << " " << +l.maskOn << " " << +l.maskInvert << " " << l.maskChannel
           << " " << l.maskDensity << "\n";
      if (l.kind == Kind::Adjust)
         o << "adj " << l.id << " " << l.adjType << " " << l.adjAmt << " " << l.adjTo << " " << l.adjDrive << " " << +l.adjMap
           << "\n";
      if (l.kind == Kind::Fill || l.kind == Kind::Adjust)
      {
         o << "fillc " << l.id << " " << l.fillType;
         for (float v : l.colorA) o << " " << v;
         for (float v : l.colorB) o << " " << v;
         o << " " << l.fillAngle << "\n";
      }
      if (l.kind == Kind::Paint)
      {
         o << "paint " << l.id << " " << l.pw << " " << l.ph;
         for (const std::string& k : l.tiles) o << " " << k;
         o << "\n";
      }
      auto shapeLine = [&](const char* key, const Shape& sh)
      {
         o << key << " " << l.id << " " << sh.type << " " << sh.x0 << " " << sh.y0 << " " << sh.x1 << " " << sh.y1 << " "
           << sh.radius << " " << sh.sides << " " << sh.inner << " " << +sh.closed << " " << +sh.fillOn;
         for (float v : sh.fillC) o << " " << v;
         o << " " << +sh.strokeOn;
         for (float v : sh.strokeC) o << " " << v;
         o << " " << sh.width << " " << sh.cap << " " << sh.join << " " << sh.align << " " << sh.dash << " " << sh.gap << " "
           << sh.op << " " << EscapeToken(sh.svg) << " " << sh.pts.size();
         for (float v : sh.pts) o << " " << v;
         o << "\n";
      };
      if (l.kind == Kind::Vector)
      {
         o << "vsize " << l.id << " " << l.pw << " " << l.ph << "\n";
         for (const Shape& sh : l.shapes)
            shapeLine("shape", sh);
      }
      if (l.hasVMask)
      {
         o << "vmask " << l.id << " " << +l.vmaskOn << "\n";
         for (const Shape& sh : l.vmask)
            shapeLine("vshape", sh);
      }
      if (l.kind == Kind::Text)
      {
         o << "text " << l.id << " " << l.fontSize << " " << l.leading << " " << l.tracking << " " << l.align;
         for (float v : l.textC) o << " " << v;
         o << " " << l.boxW << " " << EscapeToken(l.font) << " " << EscapeToken(l.text) << "\n";
      }
      if (l.is3d || l.z != 0 || l.az != 0 || l.rx != 0 || l.ry != 0 || l.orient[0] != 0 || l.orient[1] != 0 ||
          l.orient[2] != 0 || l.collapse)
         o << "d3 " << l.id << " " << +l.is3d << " " << l.z << " " << l.az << " " << l.rx << " " << l.ry << " "
           << l.orient[0] << " " << l.orient[1] << " " << l.orient[2] << " " << +l.collapse << "\n";
      if (l.warpOn || std::any_of(l.warp, l.warp + 8, [](float v) { return v != 0.0f; }))
      {
         o << "warp " << l.id << " " << +l.warpOn;
         for (float v : l.warp) o << " " << v;
         o << "\n";
      }
      for (int t = 0; t < kFxCount; t++)
      {
         const Fx& f = l.fx[t];
         const Fx z = FxDefault(t);
         if (f.on == z.on && std::equal(f.c, f.c + 4, z.c) && f.angle == z.angle && f.dist == z.dist && f.size == z.size &&
             f.spread == z.spread && f.pos == z.pos)
            continue;
         o << "fx " << l.id << " " << t << " " << +f.on;
         for (float v : f.c) o << " " << v;
         o << " " << f.angle << " " << f.dist << " " << f.size << " " << f.spread << " " << f.pos << "\n";
      }
      if (l.HasBlendIf())
      {
         o << "bif " << l.id;
         for (float v : l.bifT) o << " " << v;
         for (float v : l.bifU) o << " " << v;
         o << "\n";
      }
      if (l.HasPixelMask())
      {
         o << "pmask " << l.id << " " << +l.pmaskOn << " " << l.mw << " " << l.mh;
         for (const std::string& k : l.mtiles) o << " " << k;
         o << "\n";
      }
   }
   mText = o.str();
   mParsedText = mText;
}

void Scene2DNode::Parse()
{
   mParsedText = mText;
   mLayers.clear();
   mDoc = Document();
   panelOnly = false;
   mNextId = 1;
   std::istringstream in(mText);
   std::string line;
   auto find = [&](int id) -> Layer*
   {
      for (Layer& l : mLayers)
         if (l.id == id)
            return &l;
      return nullptr;
   };
   while (std::getline(in, line))
   {
      std::istringstream s(line);
      std::string key;
      s >> key;
      if (key == "next")
         s >> mNextId;
      else if (key == "panel")
         panelOnly = true;
      else if (key == "doc")
      {
         Document& d = mDoc;
         int ratio = 0, rulers = 0, grid = 0, snap = 1, guides = 1, lin = 1, head = 1;
         s >> d.w >> d.h >> d.background >> d.bgColor[0] >> d.bgColor[1] >> d.bgColor[2] >> ratio >> rulers >> grid >>
            snap >> guides >> d.gridSize >> d.gridSubdiv >> d.fps >> d.seconds >> lin >> head;
         d.ratioLock = ratio != 0;
         d.rulers = rulers != 0;
         d.grid = grid != 0;
         d.snap = snap != 0;
         d.guidesOn = guides != 0;
         d.linear = lin != 0;
         d.headroom = head != 0;
         d.w = std::clamp(d.w, 1, GLUtil::kMaxTargetSide);
         d.h = std::clamp(d.h, 1, GLUtil::kMaxTargetSide);
      }
      else if (key == "guidesx" || key == "guidesy")
      {
         float g = 0;
         while (s >> g)
            (key == "guidesx" ? mDoc.guidesX : mDoc.guidesY).push_back(g);
      }
      else if (key == "cam")
         s >> mDoc.camUid;
      else if (key == "sym")
      {
         s >> mDoc.symmetry >> mDoc.symN;
         mDoc.symmetry = std::clamp(mDoc.symmetry, 0, 4);
         mDoc.symN = std::clamp(mDoc.symN, 2, 32);
      }
      else if (key == "layer")
      {
         Layer l;
         int kind = 0, vis = 1, clip = 0, open = 1;
         s >> l.id >> kind >> l.parent >> l.uid >> vis >> l.blend >> l.opacity >> l.fill >> l.lock >> l.label >> clip >> open;
         l.kind = (Kind)std::clamp(kind, 0, kKindCount - 1);
         l.visible = vis != 0;
         l.clip = clip != 0;
         l.open = open != 0;
         mNextId = std::max(mNextId, l.id + 1);
         mLayers.push_back(l);
      }
      else
      {
         int id = 0;
         s >> id;
         Layer* l = find(id);
         if (l == nullptr)
            continue;
         if (key == "xf")
         {
            int fx = 0, fy = 0, nearest = 0;
            s >> l->x >> l->y >> l->sx >> l->sy >> l->angle >> l->ax >> l->ay >> fx >> fy >> l->fit >> nearest;
            l->flipX = fx != 0;
            l->flipY = fy != 0;
            l->nearest = nearest != 0;
         }
         else if (key == "name")
         {
            s >> std::ws;
            std::getline(s, l->name);
         }
         else if (key == "d3")
         {
            int on = 0, col = 0;
            s >> on >> l->z >> l->az >> l->rx >> l->ry >> l->orient[0] >> l->orient[1] >> l->orient[2] >> col;
            l->is3d = on != 0;
            l->collapse = col != 0;
         }
         else if (key == "warp")
         {
            int on = 0;
            s >> on;
            for (float& v : l->warp) s >> v;
            l->warpOn = on != 0;
         }
         else if (key == "fx")
         {
            int t = -1, on = 0;
            s >> t;
            if (t >= 0 && t < kFxCount)
            {
               Fx& f = l->fx[t];
               s >> on >> f.c[0] >> f.c[1] >> f.c[2] >> f.c[3] >> f.angle >> f.dist >> f.size >> f.spread >> f.pos;
               f.on = on != 0;
            }
         }
         else if (key == "bif")
         {
            for (float& v : l->bifT) s >> v;
            for (float& v : l->bifU) s >> v;
         }
         else if (key == "crop")
         {
            int on = 0;
            s >> on >> l->crop[0] >> l->crop[1] >> l->crop[2] >> l->crop[3];
            l->cropOn = on != 0;
         }
         else if (key == "mask")
         {
            int on = 1, inv = 0;
            s >> l->maskUid >> on >> inv >> l->maskChannel >> l->maskDensity;
            l->maskOn = on != 0;
            l->maskInvert = inv != 0;
         }
         else if (key == "adj")
         {
            int map = 0;
            s >> l->adjType >> l->adjAmt >> l->adjTo >> l->adjDrive >> map;
            l->adjType = std::clamp(l->adjType, 0, kAdjCount - 1);
            l->adjDrive = std::clamp(l->adjDrive, 0, kDrives);
            l->adjMap = map != 0;
         }
         else if (key == "fillc")
         {
            s >> l->fillType;
            for (float& v : l->colorA) s >> v;
            for (float& v : l->colorB) s >> v;
            s >> l->fillAngle;
         }
         else if (key == "vsize")
         {
            s >> l->pw >> l->ph;
            l->pw = std::clamp(l->pw, 1, GLUtil::kMaxTargetSide);
            l->ph = std::clamp(l->ph, 1, GLUtil::kMaxTargetSide);
         }
         else if (key == "vmask")
         {
            int on = 1;
            s >> on;
            l->hasVMask = true;
            l->vmaskOn = on != 0;
         }
         else if (key == "shape" || key == "vshape")
         {
            Shape sh;
            int closed = 0, fillOn = 1, strokeOn = 0;
            size_t n = 0;
            std::string svg;
            s >> sh.type >> sh.x0 >> sh.y0 >> sh.x1 >> sh.y1 >> sh.radius >> sh.sides >> sh.inner >> closed >> fillOn;
            for (float& v : sh.fillC) s >> v;
            s >> strokeOn;
            for (float& v : sh.strokeC) s >> v;
            s >> sh.width >> sh.cap >> sh.join >> sh.align >> sh.dash >> sh.gap >> sh.op >> svg >> n;
            if (!s)
               continue;
            sh.type = std::clamp(sh.type, 0, (int)Shape::kTypeCount - 1);
            sh.sides = std::clamp(sh.sides, 3, 64);
            sh.op = std::clamp(sh.op, 0, 3);
            sh.closed = closed != 0;
            sh.fillOn = fillOn != 0;
            sh.strokeOn = strokeOn != 0;
            sh.svg = UnescapeToken(svg);
            n = std::min<size_t>(n, 6 * 100000) / 6 * 6;
            sh.pts.reserve(n);
            float v = 0;
            while (sh.pts.size() < n && s >> v)
               sh.pts.push_back(v);
            sh.pts.resize(sh.pts.size() / 6 * 6);
            (key == "shape" ? l->shapes : l->vmask).push_back(sh);
         }
         else if (key == "text")
         {
            std::string font, text;
            s >> l->fontSize >> l->leading >> l->tracking >> l->align;
            for (float& v : l->textC) s >> v;
            s >> l->boxW >> font >> text;
            l->font = UnescapeToken(font);
            l->text = UnescapeToken(text);
            l->fontSize = std::clamp(l->fontSize, 1.0f, 2000.0f);
            l->align = std::clamp(l->align, 0, 3);
         }
         else if (key == "paint" || key == "pmask")
         {
            const bool mask = key == "pmask";
            int on = 1, w = 0, h = 0;
            if (mask)
               s >> on;
            s >> w >> h;
            w = std::clamp(w, 0, GLUtil::kMaxTargetSide);
            h = std::clamp(h, 0, GLUtil::kMaxTargetSide);
            const size_t n = (size_t)((w + TileStore::kTile - 1) / TileStore::kTile) * (size_t)((h + TileStore::kTile - 1) / TileStore::kTile);
            std::vector<std::string> keys;
            std::string k;
            while (keys.size() < n && s >> k)
               keys.push_back(TileStore::IsKey(k.data(), k.size()) ? k : "-");
            keys.resize(n, "-");
            if (mask)
            {
               l->pmaskOn = on != 0;
               l->mw = w;
               l->mh = h;
               l->mtiles = keys;
            }
            else
            {
               l->pw = w;
               l->ph = h;
               l->tiles = keys;
            }
         }
      }
   }
   // A parent that is not a group (hand-written text) puts the layer back at the top level.
   for (Layer& l : mLayers)
   {
      const Layer* p = l.parent != 0 ? find(l.parent) : nullptr;
      if (l.parent != 0 && (p == nullptr || p->kind != Kind::Group || p == &l))
         l.parent = 0;
   }
}

// ---- cook ---------------------------------------------------------------

bool Scene2DNode::EnsureShaders()
{
   if (mShaderTried)
      return mPassProgram != 0 && mFinishProgram != 0;
   mShaderTried = true;
   mPassProgram = GLUtil::CompileProgram(PassSrc().c_str());
   mFinishProgram = GLUtil::CompileProgram(
      GLUtil::WithClampedOutput(FinishSrc(), "if (uLinear == 1) fragColor.rgb = SrgbEncode(fragColor.rgb);").c_str());
   return mPassProgram != 0 && mFinishProgram != 0;
}

Scene2DNode::Pair& Scene2DNode::PairAt(int depth)
{
   while ((int)mPool.size() <= depth)
      mPool.emplace_back();
   Pair& p = mPool[depth];
   for (GLUtil::Fbo& f : p.f)
      GLUtil::EnsureFbo(f, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2D");
   return p;
}

void Scene2DNode::Clear(Pair& p, const float rgba[4])
{
   GLint prev = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
   glBindFramebuffer(GL_FRAMEBUFFER, p.f[p.cur].fbo);
   glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
   glClear(GL_COLOR_BUFFER_BIT);
   glBindFramebuffer(GL_FRAMEBUFFER, prev);
}

void Scene2DNode::Pass(int depth, const PassArgs& a)
{
   Pair& p = PairAt(depth);
   const unsigned int acc = p.Tex();
   const int next = 1 - p.cur;
   const unsigned int prog = mPassProgram;
   const int depthMode = depth == mDepthLevel ? (a.card ? 1 : 2) : 0;
   GLUtil::RunShaderPass(p.f[next], prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, acc);
      glUniform1i(glGetUniformLocation(prog, "uAcc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, a.tex);
      glUniform1i(glGetUniformLocation(prog, "uTex"), 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, a.mask);
      glUniform1i(glGetUniformLocation(prog, "uMask"), 2);
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, a.pmask);
      glUniform1i(glGetUniformLocation(prog, "uPMask"), 3);
      glUniform1i(glGetUniformLocation(prog, "uHasPMask"), a.pmask != 0 ? 1 : 0);
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, a.vmask);
      glUniform1i(glGetUniformLocation(prog, "uVMask"), 4);
      glUniform1i(glGetUniformLocation(prog, "uHasVMask"), a.vmask != 0 ? 1 : 0);
      glActiveTexture(GL_TEXTURE0);
      glUniform2f(glGetUniformLocation(prog, "uDoc"), (float)mDoc.w, (float)mDoc.h);
      glUniform3f(glGetUniformLocation(prog, "uInvA"), a.inv[0], a.inv[1], a.inv[2]);
      glUniform3f(glGetUniformLocation(prog, "uInvB"), a.inv[3], a.inv[4], a.inv[5]);
      glUniform3f(glGetUniformLocation(prog, "uInvC"), a.inv[6], a.inv[7], a.inv[8]);
      glUniform1i(glGetUniformLocation(prog, "uDepthMode"), depthMode);
      if (depthMode != 0)
      {
         glActiveTexture(GL_TEXTURE5);
         glBindTexture(GL_TEXTURE_2D, mDepth[depth].tex[p.cur]);
         glUniform1i(glGetUniformLocation(prog, "uDepthIn"), 5);
         glActiveTexture(GL_TEXTURE0);
         const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
         glDrawBuffers(2, bufs);
      }
      glUniform1i(glGetUniformLocation(prog, "uBif"), a.bif != nullptr ? 1 : 0);
      if (a.bif != nullptr)
      {
         glUniform4fv(glGetUniformLocation(prog, "uBifT"), 1, a.bif->bifT);
         glUniform4fv(glGetUniformLocation(prog, "uBifU"), 1, a.bif->bifU);
      }
      glUniform2f(glGetUniformLocation(prog, "uBox"), std::max(1e-3f, a.box[0]), std::max(1e-3f, a.box[1]));
      glUniform4f(glGetUniformLocation(prog, "uCrop"), a.crop[0], a.crop[1], a.crop[2], a.crop[3]);
      glUniform2f(glGetUniformLocation(prog, "uTexSize"), (float)std::max(1, a.texW), (float)std::max(1, a.texH));
      glUniform1i(glGetUniformLocation(prog, "uKind"), a.kind);
      glUniform1i(glGetUniformLocation(prog, "uMode"), a.blend);
      glUniform1i(glGetUniformLocation(prog, "uClip"), a.clipMode ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uNearest"), a.nearest ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uLinear"), mDoc.linear ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uSrgb"), a.srgb ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uHasMask"), a.mask != 0 ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uMaskChan"), a.maskChannel);
      glUniform1i(glGetUniformLocation(prog, "uMaskInv"), a.maskInvert ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uHead"), mDoc.headroom ? 1 : 0);
      glUniform1f(glGetUniformLocation(prog, "uOpacity"), std::clamp(a.opacity, 0.0f, 1.0f));
      glUniform1f(glGetUniformLocation(prog, "uMaskDensity"), std::clamp(a.maskDensity, 0.0f, 1.0f));
      if (a.fill != nullptr)
      {
         glUniform4fv(glGetUniformLocation(prog, "uColA"), 1, a.fill->colorA);
         glUniform4fv(glGetUniformLocation(prog, "uColB"), 1, a.fill->colorB);
         glUniform1f(glGetUniformLocation(prog, "uFillAngle"), a.fill->fillAngle);
      }
   });
   if (depthMode != 0)
   {
      GLint prev = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
      glBindFramebuffer(GL_FRAMEBUFFER, p.f[next].fbo);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      glBindFramebuffer(GL_FRAMEBUFFER, prev);
   }
   p.cur = next;
}

void Scene2DNode::DrawLayer(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode)
{
   if (l.kind == Kind::Adjust && !mAdjReady)
   {
      // The picture below this layer, changed once; the fx path then draws it like any other layer.
      if (!RunAdjust(l, PairAt(depth).Tex()))
         return;
      mAdjReady = true;
      DrawLayer(l, depth, frameId, opacityMul, clipMode);
      mAdjReady = false;
      return;
   }
   if (l.AnyFx())
   {
      DrawFx(l, depth, frameId, opacityMul, clipMode);
      return;
   }
   PassArgs a;
   a.bif = l.HasBlendIf() ? &l : nullptr;
   a.opacity = l.opacity * l.fill * opacityMul;
   a.blend = l.kind == Kind::Group && l.blend == kPassThrough ? 0 : std::clamp(l.blend, 0, BlendModes::kAntiEraseMode);
   a.clipMode = clipMode;
   if (INode* mask = l.maskOn ? MaskSource(l) : nullptr)
   {
      mask->CookIfNeeded(frameId);
      a.mask = mask->GetOutputTexture();
      a.maskChannel = l.maskChannel;
      a.maskInvert = l.maskInvert;
      a.maskDensity = l.maskDensity;
   }
   if (l.kind == Kind::Group)
   {
      const bool isolate = l.blend != kPassThrough || a.mask != 0 || l.opacity < 0.999f || clipMode;
      if (!isolate)
      {
         DrawList(l.id, depth, frameId, opacityMul * l.fill);
         return;
      }
      Pair& sub = PairAt(depth + 1);
      const float clear[4] = { 0, 0, 0, 0 };
      Clear(sub, clear);
      DrawList(l.id, depth + 1, frameId, 1.0f);
      a.tex = PairAt(depth + 1).Tex();
      a.texW = mDoc.w;
      a.texH = mDoc.h;
      a.box[0] = (float)mDoc.w;
      a.box[1] = (float)mDoc.h;
      a.srgb = false;   // already in working space
      a.opacity = l.opacity * opacityMul;
      Pass(depth, a);
      return;
   }
   if (l.kind == Kind::Fill)
   {
      a.kind = l.fillType == 1 ? 2 : l.fillType == 2 ? 3 : 1;
      a.fill = &l;
      a.box[0] = (float)mDoc.w;
      a.box[1] = (float)mDoc.h;
      Pass(depth, a);
      return;
   }
   if (l.kind == Kind::Paint)
   {
      auto it = mSurfaces.find(l.id);
      if (it == mSurfaces.end())
         return;
      const bool live = mStroke.Active() && mPaintId == l.id && mPaintTarget == 0;
      a.tex = live ? mStroke.DisplayTexture(it->second.fbo) : it->second.fbo.tex;
      a.texW = it->second.fbo.w;
      a.texH = it->second.fbo.h;
   }
   else if (l.kind == Kind::Adjust)
   {
      a.tex = mAdjFbo.tex;
      a.texW = mAdjFbo.w;
      a.texH = mAdjFbo.h;
      a.srgb = false;   // already in working space
   }
   else if (l.kind == Kind::Vector || l.kind == Kind::Text)
   {
      Raster* r = SyncRaster(mVec, l, false);
      if (r == nullptr)
         return;
      a.tex = r->tex;
      a.texW = r->w;
      a.texH = r->h;
   }
   else if (dynamic_cast<IGeometrySource*>(Source(l)) != nullptr)
   {
      a.tex = GeometryTexture(l, a.texW, a.texH);
      a.srgb = true;
   }
   else
   {
      INode* src = Source(l);
      if (src == nullptr)
         return;
      src->CookIfNeeded(frameId);
      a.tex = src->GetOutputTexture();
      a.texW = src->GetOutputWidth();
      a.texH = src->GetOutputHeight();
   }
   if (a.tex == 0 || a.texW <= 0 || a.texH <= 0)
      return;
   LayerBox(l, a.box[0], a.box[1]);
   float h[9];
   if (!LayerHomography(l, h))
      return;
   Invert3(h, a.inv);
   a.card = l.is3d && !l.warpOn;
   if (l.cropOn)
   {
      a.crop[0] = std::clamp(l.crop[0] / a.box[0], 0.0f, 1.0f);
      a.crop[1] = std::clamp(l.crop[1] / a.box[1], 0.0f, 1.0f);
      a.crop[2] = std::clamp(1.0f - l.crop[2] / a.box[0], a.crop[0], 1.0f);
      a.crop[3] = std::clamp(1.0f - l.crop[3] / a.box[1], a.crop[1], 1.0f);
   }
   a.nearest = l.nearest;
   if (l.HasPixelMask() && l.pmaskOn)
   {
      auto it = mMasks.find(l.id);
      if (it != mMasks.end())
      {
         const bool live = mStroke.Active() && mPaintId == l.id && mPaintTarget == 1;
         a.pmask = live ? mStroke.DisplayTexture(it->second.fbo) : it->second.fbo.tex;
      }
   }
   if (l.hasVMask && l.vmaskOn && !l.vmask.empty())
      if (Raster* r = SyncRaster(mVMasks, l, true))
         a.vmask = r->tex;
   Pass(depth, a);
}

bool Scene2DNode::RunAdjust(const Layer& l, unsigned src)
{
   if (!mAdjTried)
   {
      mAdjTried = true;
      mAdjProgram = GLUtil::CompileProgram(AdjustSrc().c_str());
   }
   if (mAdjProgram == 0 || src == 0)
      return false;
   if (!GLUtil::EnsureFbo(mAdjFbo, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2DAdj"))
      return false;
   const unsigned prog = mAdjProgram;
   GLUtil::RunShaderPass(mAdjFbo, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, src);
      glUniform1i(glGetUniformLocation(prog, "uSrc"), 0);
      glUniform2f(glGetUniformLocation(prog, "uDoc"), (float)mDoc.w, (float)mDoc.h);
      glUniform1i(glGetUniformLocation(prog, "uType"), l.adjType);
      glUniform1f(glGetUniformLocation(prog, "uAmt"), AdjAmount(l));
      glUniform1i(glGetUniformLocation(prog, "uMap"), l.adjMap ? 1 : 0);
      glUniform4fv(glGetUniformLocation(prog, "uColA"), 1, l.colorA);
      glUniform4fv(glGetUniformLocation(prog, "uColB"), 1, l.colorB);
      glUniform1i(glGetUniformLocation(prog, "uLinear"), mDoc.linear ? 1 : 0);
   });
   return true;
}

bool Scene2DNode::sBelowCache = true;

namespace
{
   bool IsCard(const Scene2DNode::Layer& l)
   {
      using K = Scene2DNode::Kind;
      return l.is3d && !l.clip && l.kind != K::Group && l.kind != K::Fill && l.kind != K::Adjust;
   }

   void Blit(unsigned src, unsigned dst, int w, int h)
   {
      GLint prevR = 0, prevD = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevR);
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevD);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
      glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevR);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevD);
   }
}

// One stamp per top-level entry (the layer and everything inside it): its saved lines, what its sources hand back,
// and the shared state it reads. `global` takes the lines that touch every layer.
void Scene2DNode::TopHashes(const std::vector<const Layer*>& kids, std::vector<unsigned long long>& out, unsigned long long& global)
{
   std::map<int, unsigned long long> lines;
   global = 1469598103934665603ull;
   size_t pos = 0;
   while (pos < mText.size())
   {
      size_t eol = mText.find('\n', pos);
      if (eol == std::string::npos)
         eol = mText.size();
      const size_t sp = mText.find(' ', pos);
      const std::string key = mText.substr(pos, (sp == std::string::npos || sp > eol ? eol : sp) - pos);
      int id = 0;
      const bool perLayer = key != "doc" && key != "next" && key != "panel" && key != "cam" && key != "sym" && sp != std::string::npos
                            && sp < eol && std::sscanf(mText.c_str() + sp + 1, "%d", &id) == 1 && FindConst(id) != nullptr;
      if (key != "next" && key != "guidesx" && key != "guidesy")
      {
         unsigned long long& h = perLayer ? lines[id] : global;
         for (size_t k = pos; k < eol; k++)
            h = Mix(h, (unsigned char)mText[k]);
         h = Mix(h, '\n');
      }
      pos = eol + 1;
   }
   global = Mix(global, (unsigned long long)soloLayer);
   const CompCam cam = Camera();
   out.assign(kids.size(), 0);
   for (size_t i = 0; i < kids.size(); i++)
   {
      unsigned long long h = 1469598103934665603ull;
      for (const Layer& l : mLayers)
      {
         if (!IsInside(l.id, kids[i]->id))
            continue;
         h = Mix(Mix(h, (unsigned long long)l.id), lines[l.id]);
         for (INode* n : { Source(l), l.maskOn ? MaskSource(l) : nullptr })
            if (n != nullptr)
            {
               h = Mix(h, (unsigned long long)(uintptr_t)n);
               if (auto* geo = dynamic_cast<IGeometrySource*>(n))
                  h = Mix(Mix(h, geo->MeshRevision()), geo->MaterialRevision());
               h = Mix(h, n->TextureRevision());
               h = Mix(h, (unsigned long long)n->GetOutputWidth() * 65536ull + (unsigned long long)n->GetOutputHeight());
            }
         if (l.kind == Kind::Paint || l.kind == Kind::Vector || l.kind == Kind::Text || l.HasPixelMask())
            h = Mix(h, mPaintEdits);
         if (mStroke.Active() && mPaintId == l.id)
            h = Mix(h, (unsigned long long)mStroke.DabCount() + 1ull);
         if (l.kind == Kind::Adjust)
            for (float v : drive)
            {
               unsigned bits = 0;
               std::memcpy(&bits, &v, sizeof bits);
               h = Mix(h, bits);
            }
         if (l.is3d || dynamic_cast<IGeometrySource*>(Source(l)) != nullptr)
            for (float v : { cam.zoom, cam.pos[0], cam.pos[1], cam.pos[2], cam.rot[0], cam.rot[1], cam.rot[2], cam.rot[4], cam.rot[8] })
            {
               unsigned bits = 0;
               std::memcpy(&bits, &v, sizeof bits);
               h = Mix(h, bits);
            }
      }
      out[i] = h;
   }
}

// What must hold for a picture saved at the start of entry `at` to be reused: everything below it unchanged, and
// the entry itself still starting a new step of DrawList (clip chains and 3D runs end where they ended).
unsigned long long Scene2DNode::BelowKey(const std::vector<const Layer*>& kids, const std::vector<unsigned long long>& hashes,
                                         size_t at, unsigned long long global)
{
   unsigned long long k = Mix(global, (unsigned long long)at);
   for (size_t i = 0; i < at && i < hashes.size(); i++)
      k = Mix(k, hashes[i]);
   if (at < kids.size())
   {
      k = Mix(k, (unsigned long long)kids[at]->clip * 2ull + (IsCard(*kids[at]) ? 1ull : 0ull));
      k = Mix(k, at + 1 < kids.size() && kids[at + 1]->clip ? 7ull : 3ull);
   }
   return k;
}

void Scene2DNode::DrawList(int parent, int depth, int frameId, float opacityMul)
{
   const std::vector<const Layer*> kids = ListOf(parent);
   auto card = [](const Layer& l) { return IsCard(l); };
   // Top level: resume from the below cache when nothing under the edited entry changed.
   const bool top = parent == 0 && depth == 0 && sBelowCache && mDepthLevel < 0;
   std::vector<unsigned long long> hashes;
   unsigned long long global = 0;
   size_t start = 0, changed = 0;
   if (top)
   {
      TopHashes(kids, hashes, global);
      if (global == mGlobalHash && hashes.size() == mKidHash.size())
      {
         changed = hashes.size();
         for (size_t i = 0; i < hashes.size(); i++)
            if (hashes[i] != mKidHash[i])
            {
               changed = i;
               break;
            }
         if (changed == hashes.size())
            changed = 0;   // the stamp moved for a reason no entry shows: draw it all, save nothing
      }
      else if (global == mGlobalHash)
      {
         // Layers added or removed: the first entry that differs.
         while (changed < hashes.size() && changed < mKidHash.size() && hashes[changed] == mKidHash[changed])
            changed++;
      }
      const bool sized = GLUtil::EnsureFbo(mBelow, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2D below");
      if (!sized)
         mBelowAt = 0;
      if (mBelowAt > 0 && mBelowAt <= changed && mBelowAt <= kids.size() && mBelowKey == BelowKey(kids, hashes, mBelowAt, global))
      {
         Pair& root = PairAt(0);
         Blit(mBelow.fbo, root.f[root.cur].fbo, mDoc.w, mDoc.h);
         start = mBelowAt;
      }
      else
         mBelowAt = 0;
      mGlobalHash = global;
      mKidHash = hashes;
   }
   for (size_t i = start; i < kids.size(); i++)
   {
      // Save the picture so far at each step start up to the edited entry; the last one saved is where the next
      // frame resumes.
      if (top && i > mBelowAt && i <= changed)
      {
         Pair& root = PairAt(0);
         Blit(root.f[root.cur].fbo, mBelow.fbo, mDoc.w, mDoc.h);
         mBelowAt = i;
         mBelowKey = BelowKey(kids, hashes, i, global);
      }
      const Layer& base = *kids[i];
      // A clipped layer with nothing unclipped below it in its list is drawn as it is.
      size_t end = i + 1;
      while (end < kids.size() && kids[end]->clip)
         end++;
      // Neighbouring 3D cards (none with clipped layers) draw as one run: far first, nearer ones hide them.
      if (card(base) && end == i + 1)
      {
         std::vector<const Layer*> run;
         size_t j = i;
         while (j < kids.size() && card(*kids[j]) && !(j + 1 < kids.size() && kids[j + 1]->clip))
         {
            if (Shown(*kids[j]))
               run.push_back(kids[j]);
            j++;
         }
         if (run.size() >= 2)
         {
            std::vector<std::pair<float, const Layer*>> order;
            for (const Layer* r : run)
               order.push_back({ CardDepth(*r), r });
            std::stable_sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
            BeginDepth(depth);
            for (const auto& o : order)
               DrawLayer(*o.second, depth, frameId, opacityMul, false);
            EndDepth(depth);
         }
         else if (!run.empty())
            DrawLayer(*run[0], depth, frameId, opacityMul, false);
         i = j - 1;
         continue;
      }
      if (!Shown(base))
      {
         if (!base.clip)
            i = end - 1;   // its clipped layers go with it
         continue;
      }
      bool anyClipped = false;
      for (size_t j = i + 1; j < end; j++)
         anyClipped = anyClipped || Shown(*kids[j]);
      if (!anyClipped || base.clip)
      {
         DrawLayer(base, depth, frameId, opacityMul, false);
         i = base.clip ? i : end - 1;
         continue;
      }
      // Base and its clipped layers together, then onto what is below with the base's blend and opacity.
      Pair& sub = PairAt(depth + 1);
      const float clear[4] = { 0, 0, 0, 0 };
      Clear(sub, clear);
      Layer plain = base;
      plain.blend = base.kind == Kind::Group ? kPassThrough : 0;
      plain.opacity = 1.0f;
      DrawLayer(plain, depth + 1, frameId, 1.0f, false);
      for (size_t j = i + 1; j < end; j++)
         if (Shown(*kids[j]))
            DrawLayer(*kids[j], depth + 1, frameId, 1.0f, true);
      PassArgs a;
      a.tex = PairAt(depth + 1).Tex();
      a.texW = mDoc.w;
      a.texH = mDoc.h;
      a.box[0] = (float)mDoc.w;
      a.box[1] = (float)mDoc.h;
      a.srgb = false;
      a.blend = base.kind == Kind::Group && base.blend == kPassThrough ? 0 : std::clamp(base.blend, 0, BlendModes::kAntiEraseMode);
      a.opacity = base.opacity * opacityMul;
      Pass(depth, a);
      i = end - 1;
   }
}

void Scene2DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   Layers();
   if (bypassed)
      return;
   const auto start = std::chrono::steady_clock::now();

   // Cook every shown source first; what they hand back decides whether the picture changes.
   unsigned long long stamp = 1469598103934665603ull;
   for (char c : mText)
      stamp = Mix(stamp, (unsigned char)c);
   stamp = Mix(stamp, (unsigned long long)soloLayer);
   SyncSurfaces();
   SyncRasters();
   // A stroke changes the picture before it changes the text; its queued dabs land now.
   if (mStroke.Active())
      mStroke.Flush();
   stamp = Mix(stamp, mStroke.Active() ? (unsigned long long)mStroke.DabCount() + 1ull : 0ull);
   stamp = Mix(stamp, mPaintEdits);
   for (const Layer& l : mLayers)
   {
      if (!Shown(l))
         continue;
      for (INode* n : { Source(l), l.maskOn ? MaskSource(l) : nullptr })
         if (n != nullptr)
         {
            n->CookIfNeeded(frameId);
            stamp = Mix(stamp, (unsigned long long)(uintptr_t)n);
            if (auto* geo = dynamic_cast<IGeometrySource*>(n))
               stamp = Mix(Mix(stamp, geo->MeshRevision()), geo->MaterialRevision());
            stamp = Mix(stamp, n->TextureRevision());
            stamp = Mix(stamp, (unsigned long long)n->GetOutputWidth() * 65536ull + (unsigned long long)n->GetOutputHeight());
         }
   }
   // Drive A..D move adjustment layers.
   for (float v : drive)
   {
      unsigned bits = 0;
      std::memcpy(&bits, &v, sizeof bits);
      stamp = Mix(stamp, bits);
   }
   // The comp camera moves 3D cards and turns geometry.
   const CompCam cam = Camera();
   for (float v : { cam.zoom, cam.pos[0], cam.pos[1], cam.pos[2], cam.rot[0], cam.rot[1], cam.rot[2], cam.rot[4], cam.rot[8] })
   {
      unsigned bits = 0;
      std::memcpy(&bits, &v, sizeof bits);
      stamp = Mix(stamp, bits);
   }
   for (auto it = mGeo.begin(); it != mGeo.end();)
   {
      const Layer* l = FindConst(it->first);
      it = l == nullptr || dynamic_cast<IGeometrySource*>(Source(*l)) == nullptr ? mGeo.erase(it) : std::next(it);
   }
   if (stamp == mStamp && mOut.tex != 0 && mOut.w == mDoc.w && mOut.h == mDoc.h)
      return;
   mStamp = stamp;

   if (!EnsureShaders())
      return;
   if (!GLUtil::EnsureFbo(mOut, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2D"))
      return;
   Pair& root = PairAt(0);
   float bg[4] = { 0, 0, 0, 0 };
   if (mDoc.background != 3)
   {
      const float white[3] = { 1, 1, 1 }, black[3] = { 0, 0, 0 };
      const float* c = mDoc.background == 0 ? white : mDoc.background == 1 ? black : mDoc.bgColor;
      for (int i = 0; i < 3; i++)
         bg[i] = mDoc.linear ? SrgbToLinear(c[i]) : c[i];
      bg[3] = 1.0f;
   }
   Clear(root, bg);
   DrawList(0, 0, frameId, 1.0f);

   const unsigned int acc = PairAt(0).Tex();
   const unsigned int prog = mFinishProgram;
   GLUtil::RunShaderPass(mOut, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, acc);
      glUniform1i(glGetUniformLocation(prog, "uAcc"), 0);
      glUniform1i(glGetUniformLocation(prog, "uLinear"), mDoc.linear ? 1 : 0);
      GLUtil::SetImageHeadroom(prog, mDoc.headroom);
   });
   mRevision++;
   mCookMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}
