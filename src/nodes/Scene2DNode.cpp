#include "Scene2DNode.h"

#include "gl3.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

#include "BlendModes.h"
#include "Geometry3DNodes.h"
#include "Modulation.h"
#include "TileStore.h"

std::function<INode*(uint64_t)> Scene2DNode::ResolveUid;

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
            "uniform int uHasPMask;\n")
         + GLUtil::kSrgbGlsl
         + BlendModes::kBlendGLSL
         + "vec4 layerAt(vec2 uvb, vec2 uv) {\n"
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
           "   vec2 p = vec2(vUv.x * uDoc.x, (1.0 - vUv.y) * uDoc.y);\n"
           "   vec2 b = vec2(dot(uInvA, vec3(p, 1.0)), dot(uInvB, vec3(p, 1.0)));\n"
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
           "   float amt = uOpacity * layer.a;\n"
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

   float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

   void Invert(const float m[6], float out[6])
   {
      const float det = m[0] * m[4] - m[1] * m[3];
      const float id = std::fabs(det) > 1e-12f ? 1.0f / det : 0.0f;
      out[0] = m[4] * id;
      out[1] = -m[1] * id;
      out[3] = -m[3] * id;
      out[4] = m[0] * id;
      out[2] = -(out[0] * m[2] + out[1] * m[5]);
      out[5] = -(out[3] * m[2] + out[4] * m[5]);
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
   for (auto& kv : mSurfaces)
      GLUtil::DestroyFbo(kv.second.fbo);
   for (auto& kv : mMasks)
      GLUtil::DestroyFbo(kv.second.fbo);
   GLUtil::DestroyFbo(mSelTex);
   GLUtil::DestroyFbo(mSelOverlay);
   GLUtil::DestroyFbo(mSelScratch);
   for (unsigned p : { mFillProgram, mAntsProgram, mResampleProgram })
      if (p != 0)
         glDeleteProgram(p);
   if (mPassProgram != 0)
      glDeleteProgram(mPassProgram);
   if (mFinishProgram != 0)
      glDeleteProgram(mFinishProgram);
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
   if (l.kind == Kind::Paint)
   {
      w = (float)std::max(1, l.pw);
      h = (float)std::max(1, l.ph);
      return true;
   }
   if (l.kind != Kind::Node)
   {
      w = W;
      h = H;
      return true;
   }
   INode* src = Source(l);
   const float sw = src != nullptr ? (float)src->GetOutputWidth() : 0.0f;
   const float sh = src != nullptr ? (float)src->GetOutputHeight() : 0.0f;
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
   if (l.kind != Kind::Node && l.kind != Kind::Paint)
   {
      // Fills and groups are the document itself.
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
   float m[6];
   LayerMatrix(l, m);
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
      out[i * 2] = m[0] * pts[i * 2] + m[1] * pts[i * 2 + 1] + m[2];
      out[i * 2 + 1] = m[3] * pts[i * 2] + m[4] * pts[i * 2 + 1] + m[5];
   }
   return true;
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
      if (l.kind == Kind::Fill)
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
      else if (key == "layer")
      {
         Layer l;
         int kind = 0, vis = 1, clip = 0, open = 1;
         s >> l.id >> kind >> l.parent >> l.uid >> vis >> l.blend >> l.opacity >> l.fill >> l.lock >> l.label >> clip >> open;
         l.kind = (Kind)std::clamp(kind, 0, 3);
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
         else if (key == "fillc")
         {
            s >> l->fillType;
            for (float& v : l->colorA) s >> v;
            for (float& v : l->colorB) s >> v;
            s >> l->fillAngle;
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
      glActiveTexture(GL_TEXTURE0);
      glUniform2f(glGetUniformLocation(prog, "uDoc"), (float)mDoc.w, (float)mDoc.h);
      glUniform3f(glGetUniformLocation(prog, "uInvA"), a.inv[0], a.inv[1], a.inv[2]);
      glUniform3f(glGetUniformLocation(prog, "uInvB"), a.inv[3], a.inv[4], a.inv[5]);
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
   p.cur = next;
}

void Scene2DNode::DrawLayer(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode)
{
   PassArgs a;
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
   float m[6];
   LayerMatrix(l, m);
   Invert(m, a.inv);
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
   Pass(depth, a);
}

void Scene2DNode::DrawList(int parent, int depth, int frameId, float opacityMul)
{
   std::vector<const Layer*> kids;
   for (const Layer& l : mLayers)
      if (l.parent == parent)
         kids.push_back(&l);
   for (size_t i = 0; i < kids.size(); i++)
   {
      const Layer& base = *kids[i];
      // A clipped layer with nothing unclipped below it in its list is drawn as it is.
      size_t end = i + 1;
      while (end < kids.size() && kids[end]->clip)
         end++;
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
   // A stroke changes the picture before it changes the text.
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
            stamp = Mix(stamp, n->TextureRevision());
            stamp = Mix(stamp, (unsigned long long)n->GetOutputWidth() * 65536ull + (unsigned long long)n->GetOutputHeight());
         }
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
