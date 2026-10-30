#include "Scene3DNode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "gl3.h"
#include "EnvironmentNode.h"
#include "GeometryOpNodes.h"
#include "SceneNodes.h"

std::function<INode*(uint64_t)> Scene3DNode::ResolveUid;

namespace
{
   const float kDeg = 3.14159265f / 180.0f;

   void TransformPoint(const Mat4& m, const float in[3], float out[3])
   {
      for (int k = 0; k < 3; k++)
         out[k] = m.m[k] * in[0] + m.m[4 + k] * in[1] + m.m[8 + k] * in[2] + m.m[12 + k];
   }
   void TransformDir(const Mat4& m, const float in[3], float out[3])
   {
      for (int k = 0; k < 3; k++)
         out[k] = m.m[k] * in[0] + m.m[4 + k] * in[1] + m.m[8 + k] * in[2];
   }
   // Directional, sun and ambient lights hold a direction, the others a position.
   bool LightIsPositioned(int type) { return type == 1 || type == 4 || type == 5; }
}

// One item as Render 3D sees it: the source's own result with the placement
// multiplied in front. Every field of IGeometrySource is forwarded on purpose;
// a group's children are wrapped the same way, since Render 3D draws them one
// by one and would otherwise miss the placement.
class Scene3DNode::Placed : public IGeometrySource
{
public:
   IGeometrySource* src = nullptr;
   Mat4 place;
   unsigned int lightMask = ~0u;
   std::vector<std::unique_ptr<Placed>> children;

   void Update(IGeometrySource* source, const Mat4& placement, unsigned int mask)
   {
      src = source;
      place = placement;
      lightMask = mask;
      const int count = src != nullptr ? src->GroupChildCount() : 0;
      children.resize(count);
      for (int i = 0; i < count; i++)
      {
         if (!children[i])
            children[i] = std::make_unique<Placed>();
         children[i]->Update(src->GroupChild(i), placement, mask);
      }
   }

   const Mesh& GetMesh() override { return src->GetMesh(); }
   unsigned long long MeshRevision() override { return src->MeshRevision(); }
   Mat4 GetModelMatrix() const override { return Mat4::Multiply(place, src->GetModelMatrix()); }
   Material GetMaterial() const override { return src->GetMaterial(); }
   unsigned long long MaterialRevision() const override { return src->MaterialRevision(); }
   unsigned int GetSurfaceTexture() override { return src->GetSurfaceTexture(); }
   unsigned int GetMaterialTexture(int map) override { return src->GetMaterialTexture(map); }
   unsigned long long SurfaceTextureRevision() const override { return src->SurfaceTextureRevision(); }
   MappingTransform GetMappingTransform() const override { return src->GetMappingTransform(); }
   unsigned long long MappingRevision() const override { return src->MappingRevision(); }
   IGeometrySource* PassthroughSource() const override { return src; }
   int GroupChildCount() const override { return (int)children.size(); }
   IGeometrySource* GroupChild(int index) const override { return children[index].get(); }
   unsigned int LightMask() const override { return lightMask; }
   Mat4 GetInstanceGroupMatrix() const override { return Mat4::Multiply(place, src->GetInstanceGroupMatrix()); }
   const std::vector<unsigned char>* InstanceSelection() const override { return src->InstanceSelection(); }
   unsigned long long InstanceSelectionRevision() const override { return src->InstanceSelectionRevision(); }
   const std::vector<Mat4>* InstanceTransformOverride() const override { return src->InstanceTransformOverride(); }
   const std::vector<Particle>* GetPointCloud() override { return src->GetPointCloud(); }
   unsigned long long PointCloudRevision() override { return src->PointCloudRevision(); }
   float PointBaseSize() const override { return src->PointBaseSize(); }
   const GpuPoints* GetGpuPoints() override { return src->GetGpuPoints(); }
   const Polyline* GetCurve() override { return src->GetCurve(); }
   unsigned long long CurveStamp() override { return src->CurveStamp(); }
};

class Scene3DNode::Root : public IGeometrySource
{
public:
   std::vector<IGeometrySource*> draws;
   Mesh empty;
   const Mesh& GetMesh() override { return empty; }
   unsigned long long MeshRevision() override { return 1; }
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override { return Material(); }
   int GroupChildCount() const override { return (int)draws.size(); }
   IGeometrySource* GroupChild(int index) const override { return draws[index]; }
};

Scene3DNode::~Scene3DNode()
{
   GLUtil::DestroyFbo(mOutFbo);
   GLUtil::DestroyFbo(mEditorFbo);
   GLUtil::DestroyFbo(mGridFbo);
   if (mCompositeProgram != 0)
      glDeleteProgram(mCompositeProgram);
   if (mGridProgram != 0)
      glDeleteProgram(mGridProgram);
}

Mat4 Scene3DNode::Placement(const Item& it)
{
   Mat4 m = Mat4::Translation(it.pos[0], it.pos[1], it.pos[2]);
   m = Mat4::Multiply(m, Mat4::RotationZ(it.rot[2] * kDeg));
   m = Mat4::Multiply(m, Mat4::RotationY(it.rot[1] * kDeg));
   m = Mat4::Multiply(m, Mat4::RotationX(it.rot[0] * kDeg));
   return Mat4::Multiply(m, Mat4::Scale(it.scale[0], it.scale[1], it.scale[2]));
}

Scene3DNode::Kind Scene3DNode::KindOf(const INode* node)
{
   if (node == nullptr)
      return Kind::Missing;
   if (dynamic_cast<const CameraNode*>(node) != nullptr)
      return Kind::Camera;
   if (dynamic_cast<const LightNode*>(node) != nullptr)
      return Kind::Light;
   return dynamic_cast<const IGeometrySource*>(node) != nullptr ? Kind::Geometry : Kind::Missing;
}

const Scene3DNode::Item* Scene3DNode::FindConst(int id) const
{
   for (const Item& it : mItems)
      if (it.id == id)
         return &it;
   return nullptr;
}

bool Scene3DNode::ItemLightAim(const Item& item, float aim[3]) const
{
   auto* light = dynamic_cast<LightNode*>(ResolveUid ? ResolveUid(item.uid) : nullptr);
   if (light == nullptr)
      return false;
   float own[3];
   light->ComputeVector(own);
   const float back[3] = { -own[0], -own[1], -own[2] };
   TransformDir(Placement(item), back, aim);
   const float len = std::max(1e-4f, std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]));
   for (int k = 0; k < 3; k++)
      aim[k] /= len;
   return true;
}

Scene3DNode::Kind Scene3DNode::ItemKind(const Item& item) const
{
   return ResolveUid ? KindOf(ResolveUid(item.uid)) : Kind::Missing;
}

Scene3DNode::Item* Scene3DNode::FindItem(int id)
{
   for (Item& it : mItems)
      if (it.id == id)
         return &it;
   return nullptr;
}

Scene3DNode::Item& Scene3DNode::AddItem(uint64_t uid)
{
   Item it;
   it.id = mNextId++;
   it.uid = uid;
   mItems.push_back(it);
   Commit();
   return mItems.back();
}

void Scene3DNode::RemoveItem(int id)
{
   mItems.erase(std::remove_if(mItems.begin(), mItems.end(), [&](const Item& it) { return it.id == id; }),
                mItems.end());
   if (activeCamera == id)
      activeCamera = 0;
   mPlaced.erase(id);
   Commit();
}

void Scene3DNode::Commit()
{
   Serialize();
   mSyncedFrame = -1;
}

// One line per fact, so a patch diff reads and an agent can write it by hand.
void Scene3DNode::Serialize()
{
   std::ostringstream o;
   o << "next " << mNextId << "\n";
   o << "camera " << activeCamera << "\n";
   o << "world " << worldUid << " " << (worldColorOn ? 1 : 0) << " " << worldColor[0] << " " << worldColor[1]
     << " " << worldColor[2] << " " << worldStrength << " " << focusItem << "\n";
   for (const Item& it : mItems)
   {
      o << "item " << it.id << " " << it.uid;
      for (float v : it.pos) o << " " << v;
      for (float v : it.rot) o << " " << v;
      for (float v : it.scale) o << " " << v;
      o << " " << (it.visible ? 1 : 0);
      for (uint64_t u : it.unlit) o << " " << u;
      o << "\n";
   }
   mText = o.str();
   mParsedText = mText;
}

void Scene3DNode::Parse()
{
   mParsedText = mText;
   mItems.clear();
   std::istringstream in(mText);
   std::string line;
   while (std::getline(in, line))
   {
      std::istringstream l(line);
      std::string key;
      l >> key;
      if (key == "next")
         l >> mNextId;
      else if (key == "camera")
         l >> activeCamera;
      else if (key == "world")
      {
         int on = 0;
         l >> worldUid >> on >> worldColor[0] >> worldColor[1] >> worldColor[2];
         if (!(l >> worldStrength >> focusItem))
         {
            worldStrength = 1.0f;
            focusItem = 0;
         }
         worldColorOn = on != 0;
      }
      else if (key == "item")
      {
         Item it;
         int vis = 1;
         l >> it.id >> it.uid;
         for (float& v : it.pos) l >> v;
         for (float& v : it.rot) l >> v;
         for (float& v : it.scale) l >> v;
         l >> vis;
         it.visible = vis != 0;
         uint64_t u = 0;
         while (l >> u)
            it.unlit.push_back(u);
         mNextId = std::max(mNextId, it.id + 1);
         mItems.push_back(std::move(it));
      }
   }
}

void Scene3DNode::VisitParams(ParamVisitor& v)
{
   v.Float("width", mRender.width);
   v.Float("height", mRender.height);
   v.Int("samples", mRender.samples);
   v.Bool("shadows", mRender.shadowsEnabled);
   v.Float("exposure", mRender.exposure);
   v.Float("bloom", mRender.bloom);
   v.Float("ao", mRender.aoStrength);
   v.Int("supersample", mRender.supersample);
   v.Int("tonemap", mRender.tonemap);
   v.Float("bloomThreshold", mRender.bloomThreshold);
   v.Float("bloomRadius", mRender.bloomRadius);
   v.Float("aoRadius", mRender.aoRadius);
   v.Int("shadowQuality", mRender.shadowQuality);
   v.Float("shadowBias", mRender.shadowBias);
   v.Float("shadowSoftness", mRender.shadowSoftness);
   v.Float("shadowStrength", mRender.shadowStrength);
   v.Float("dofAperture", mRender.dofAperture);
   v.Float("focusDistance", mRender.focusDistance);
   v.Color("ambient", mRender.ambientColor);
   v.Float("envIntensity", mRender.envIntensity);
   v.Text("scene", mText);
}

bool Scene3DNode::ItemPoint(const Item& item, float out[3]) const
{
   INode* node = ResolveUid ? ResolveUid(item.uid) : nullptr;
   float p[3] = { 0, 0, 0 };
   if (auto* cam = dynamic_cast<CameraNode*>(node))
      cam->ComputeEye(p);
   else if (auto* light = dynamic_cast<LightNode*>(node))
   {
      light->ComputeVector(p);
      if (!LightIsPositioned(light->type))
      {
         const float d = light->distance;
         p[0] *= d; p[1] *= d; p[2] *= d;
      }
   }
   else
      return false;
   TransformPoint(Placement(item), p, out);
   return true;
}

bool Scene3DNode::ItemCamera(const Item& item, Mat4& view, float eye[3], CameraNode** lens) const
{
   auto* cam = dynamic_cast<CameraNode*>(ResolveUid ? ResolveUid(item.uid) : nullptr);
   if (cam == nullptr)
      return false;
   const Mat4 place = Placement(item);
   float own[3];
   cam->ComputeEye(own);
   TransformPoint(place, own, eye);
   view = Mat4::Multiply(cam->ViewMatrix(), Mat4::Inverse(place));
   if (lens != nullptr)
      *lens = cam;
   return true;
}

bool Scene3DNode::CameraView(Mat4& view, float eye[3], CameraNode** lens) const
{
   for (const Item& it : mItems)
      if (it.id == activeCamera)
         return ItemCamera(it, view, eye, lens);
   return false;
}

// view = camera * inverse(place), so place = inverse(view) * camera. Both are
// rigid, so the result splits cleanly back into a position and three angles.
void Scene3DNode::AlignToView(Item& item, const Mat4& view) const
{
   auto* cam = dynamic_cast<CameraNode*>(ResolveUid ? ResolveUid(item.uid) : nullptr);
   if (cam == nullptr)
      return;
   const Mat4 place = Mat4::Multiply(Mat4::Inverse(view), cam->ViewMatrix());
   const float d = 180.0f / 3.14159265f;
   for (int k = 0; k < 3; k++)
   {
      item.pos[k] = place.m[12 + k];
      item.scale[k] = 1.0f;
   }
   item.rot[1] = std::asin(std::clamp(-place.m[2], -1.0f, 1.0f)) * d;
   item.rot[0] = std::atan2(place.m[6], place.m[10]) * d;
   item.rot[2] = std::atan2(place.m[1], place.m[0]) * d;
}

void Scene3DNode::Sync(int frameId)
{
   if (mSyncedFrame == frameId)
      return;
   mSyncedFrame = frameId;
   if (mText != mParsedText)
      Parse();
   if (!mRoot)
      mRoot = std::make_unique<Root>();

   // The lights first: each item's mask is by their order.
   std::vector<uint64_t> lightOrder;
   mRig.lights.clear();
   for (const Item& it : mItems)
   {
      auto* light = dynamic_cast<LightNode*>(ResolveUid ? ResolveUid(it.uid) : nullptr);
      if (light == nullptr || light->bypassed || !it.visible ||
          (int)mRig.lights.size() >= Render3DNode::kMaxLights)
         continue;
      light->CookIfNeeded(frameId);
      Render3DNode::RigLight l;
      float own[3];
      light->ComputeVector(own);
      const Mat4 place = Placement(it);
      if (LightIsPositioned(light->type))
         TransformPoint(place, own, l.vec);
      else
         TransformDir(place, own, l.vec);
      const float back[3] = { -own[0], -own[1], -own[2] };
      TransformDir(place, back, l.aim);
      const float aimLen = std::max(1e-4f, std::sqrt(l.aim[0] * l.aim[0] + l.aim[1] * l.aim[1] + l.aim[2] * l.aim[2]));
      for (float& a : l.aim)
         a /= aimLen;
      std::memcpy(l.color, light->color, sizeof(l.color));
      l.intensity = light->intensity;
      l.type = light->type;
      l.spotAngle = light->spotAngle;
      l.spotPenumbra = light->spotPenumbra;
      l.areaW = light->areaWidth;
      l.areaH = light->areaHeight;
      mRig.lights.push_back(l);
      lightOrder.push_back(it.uid);
   }
   mRig.hasLights = !mRig.lights.empty();

   mRoot->draws.clear();
   for (const Item& it : mItems)
   {
      INode* node = ResolveUid ? ResolveUid(it.uid) : nullptr;
      auto* source = dynamic_cast<IGeometrySource*>(node);
      if (source == nullptr)
      {
         mPlaced.erase(it.id);   // deleted or replaced: nothing may keep its pointer
         continue;
      }
      if (!it.visible || node->bypassed)
      {
         mPlaced.erase(it.id);
         continue;
      }
      node->CookIfNeeded(frameId);
      unsigned int mask = ~0u;
      for (size_t i = 0; i < lightOrder.size(); i++)
         if (std::find(it.unlit.begin(), it.unlit.end(), lightOrder[i]) != it.unlit.end())
            mask &= ~(1u << i);
      std::unique_ptr<Placed>& placed = mPlaced[it.id];
      if (!placed)
         placed = std::make_unique<Placed>();
      placed->Update(source, Placement(it), mask);
      mRoot->draws.push_back(placed.get());
   }
   mRender.geometry[0] = mRoot.get();
   mRender.camera = nullptr;
   mClipped = 0;
   const size_t want = [&] { size_t n = 0; for (IGeometrySource* s : mRoot->draws) n += std::max(1, s->GroupChildCount()); return n; }();
   if (want > (size_t)Render3DNode::kMaxDraw)
      mClipped = want - Render3DNode::kMaxDraw;

   mRig.hasView = CameraView(mRig.view, mRig.eye, &mRig.lens);
   if (!mRig.hasView)
      mRig.lens = nullptr;

   // Stamped by value, so an orbiting camera or a dragged item invalidates the
   // cached render and a still scene keeps it.
   std::vector<float> stamp;
   stamp.insert(stamp.end(), mRig.view.m, mRig.view.m + 16);
   stamp.push_back(mRig.hasView ? 1.0f : 0.0f);
   if (mRig.lens != nullptr)
   {
      const Mat4 p = mRig.lens->ProjectionMatrix(1.0f);
      stamp.insert(stamp.end(), p.m, p.m + 16);
   }
   for (const Render3DNode::RigLight& l : mRig.lights)
   {
      const float* f = l.vec;
      stamp.insert(stamp.end(), f, f + 7);
      stamp.push_back((float)l.type);
      stamp.push_back(l.spotAngle); stamp.push_back(l.spotPenumbra);
      stamp.push_back(l.areaW); stamp.push_back(l.areaH);
      stamp.insert(stamp.end(), l.aim, l.aim + 3);
   }
   const bool worldLights = worldColorOn && mWorldNode == nullptr;
   for (int k = 0; k < 3; k++)
   {
      mRig.ambient[k] = worldLights ? worldColor[k] * worldStrength : 0.0f;
      stamp.push_back(mRig.ambient[k]);
   }
   // A focus object is measured along the camera, so it holds as either one moves.
   if (const Item* target = focusItem != 0 ? FindConst(focusItem) : nullptr)
   {
      Mat4 view;
      float eye[3];
      if (CameraView(view, eye, nullptr))
         mRender.focusDistance = std::max(0.05f, -(view.m[2] * target->pos[0] + view.m[6] * target->pos[1] +
                                                  view.m[10] * target->pos[2] + view.m[14]));
   }
   stamp.push_back(mRender.focusDistance);
   {
      mRigStamp.swap(stamp);
      mRig.revision++;
   }
   mRender.rig = &mRig;

   // A deleted world node reads as no world, in the same breath as its uid.
   mWorldNode = (worldUid != 0 && ResolveUid) ? ResolveUid(worldUid) : nullptr;
   if (mWorldNode != nullptr && mWorldNode->bypassed)
      mWorldNode = nullptr;
   if (mWorldNode != nullptr)
      mWorldNode->CookIfNeeded(frameId);
}

void Scene3DNode::ApplyWorld(Render3DNode& render, bool show)
{
   auto* env = dynamic_cast<EnvironmentNode*>(mWorldNode);
   if (env != nullptr && show)
   {
      if (render.envInput.Resolved() != env)
         render.envInput.Connect(env);
   }
   else if (render.envInput.IsConnected())
      render.envInput.Disconnect();
   render.envAsBackground = true;
   render.bgOpacity = (show && worldColorOn && mWorldNode == nullptr) ? 1.0f : 0.0f;
   for (int k = 0; k < 3; k++)
      render.bgColor[k] = worldColor[k] * worldStrength;
}

// A plain picture as the world: the render, over it, with its own alpha.
unsigned int Scene3DNode::Composite(unsigned int sceneTex, int w, int h, unsigned int backdrop, GLUtil::Fbo& out)
{
   if (mCompositeProgram == 0)
      mCompositeProgram = GLUtil::CompileProgram(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uScene;\n"
         "uniform sampler2D uBack;\n"
         "void main() {\n"
         "   vec4 s = texture(uScene, vUv);\n"
         "   vec4 b = texture(uBack, vUv);\n"
         "   fragColor = vec4(s.rgb + b.rgb * (1.0 - s.a), s.a + b.a * (1.0 - s.a));\n"
         "}\n");
   if (mCompositeProgram == 0 || !GLUtil::EnsureFbo(out, w, h))
      return sceneTex;
   GLUtil::RunShaderPass(out, mCompositeProgram, [&]
   {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, backdrop);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uBack"), 1);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, sceneTex);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uScene"), 0);
   });
   return out.tex;
}

void Scene3DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   Sync(frameId);
   ApplyWorld(mRender, true);
   mRender.CookIfNeeded(frameId);

   unsigned int backdrop = 0;
   if (mWorldNode != nullptr && dynamic_cast<EnvironmentNode*>(mWorldNode) == nullptr)
      backdrop = mWorldNode->GetOutputTexture();
   mOutTex = mRender.GetOutputTexture();
   if (backdrop != 0)
      mOutTex = Composite(mOutTex, mRender.GetOutputWidth(), mRender.GetOutputHeight(), backdrop, mOutFbo);
   const unsigned long long rev = mRender.TextureRevision() + (backdrop != 0 ? mWorldNode->TextureRevision() : 0);
   if (rev != mBuiltRevision)
   {
      mBuiltRevision = rev;
      mRevision = NextTextureRevision();
   }
}

unsigned int Scene3DNode::GetOutputTexture()
{
   return mOutTex;
}

// The floor, drawn after the scene against its depth: endless, always fine at
// any zoom, fading into the distance, and hidden by whatever stands in front.
// Colours are Blender's theme defaults.
unsigned int Scene3DNode::GridPass(unsigned int sceneTex, int w, int h, const View& v)
{
   if (mGridProgram == 0)
      mGridProgram = GLUtil::CompileProgram(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uScene;\n"
         "uniform sampler2D uDepth;\n"
         "uniform mat4 uInv;\n"
         "uniform mat4 uViewProj;\n"
         "uniform vec3 uEye;\n"
         "uniform int uAxis;\n"
         "uniform int uDraw;\n"
         "uniform float uDist;\n"
         "const vec3 kAxis[3] = vec3[3](vec3(1.0, 0.2, 0.32), vec3(0.545, 0.863, 0.0), vec3(0.157, 0.565, 1.0));\n"
         "float lines(vec2 uv, float s) {\n"
         "   vec2 c = uv / s;\n"
         "   vec2 g = abs(fract(c - 0.5) - 0.5) / max(fwidth(c), vec2(1e-5));\n"
         "   return 1.0 - min(min(g.x, g.y), 1.0);\n"
         "}\n"
         "void main() {\n"
         "   vec4 s = texture(uScene, vUv);\n"
         "   vec3 rgb = s.rgb;\n"
         "   float a = s.a;\n"
         "   if (uDraw == 1) {\n"
         "      vec4 n = uInv * vec4(vUv * 2.0 - 1.0, -1.0, 1.0);\n"
         "      vec4 f = uInv * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);\n"
         "      vec3 o = n.xyz / n.w;\n"
         "      vec3 d = f.xyz / f.w - o;\n"
         "      float t = abs(d[uAxis]) > 1e-6 ? -o[uAxis] / d[uAxis] : -1.0;\n"
         "      vec3 p = o + d * max(t, 0.0);\n"
         "      int ua = (uAxis + 1) % 3, ub = (uAxis + 2) % 3;\n"
         "      vec2 uv = vec2(p[ua], p[ub]);\n"
         "      vec4 clip = uViewProj * vec4(p, 1.0);\n"
         "      float pd = clip.z / clip.w * 0.5 + 0.5;\n"
         "      float graze = abs(normalize(d)[uAxis]);\n"
         "      float range = uDist * 14.0 + 12.0;\n"
         "      float fade = (1.0 - smoothstep(range * 0.35, range, length(p - uEye))) * smoothstep(0.0, 0.06, graze);\n"
         "      if (t > 0.0 && t <= 1.0 && pd <= texture(uDepth, vUv).r + 1e-6 && fade > 0.0) {\n"
         "         float level = log(max(uDist, 1e-3) * 0.25) / log(10.0);\n"
         "         float scale = pow(10.0, floor(level));\n"
         "         float step01 = fract(level);\n"
         "         float grid = max(lines(uv, scale) * 0.4 * (1.0 - step01), lines(uv, scale * 10.0) * 0.75);\n"
         "         vec3 col = vec3(0.329);\n"
         "         float ax = 1.0 - min(abs(uv.x) / max(fwidth(uv.x), 1e-5), 1.0);\n"
         "         float bx = 1.0 - min(abs(uv.y) / max(fwidth(uv.y), 1e-5), 1.0);\n"
         "         if (ax > 0.0) { col = kAxis[ub]; grid = max(grid, ax); }\n"
         "         if (bx > 0.0) { col = kAxis[ua]; grid = max(grid, bx); }\n"
         "         float g = grid * fade;\n"
         "         rgb = mix(rgb, col, g);\n"
         "         a = a + g * (1.0 - a);\n"
         "      }\n"
         "   }\n"
         "   fragColor = a > 1e-4 ? vec4(rgb / a, a) : vec4(0.0);\n"
         "}\n");
   if (mGridProgram == 0 || !GLUtil::EnsureFbo(mGridFbo, w, h))
      return sceneTex;
   const Mat4 viewProj = Mat4::Multiply(v.proj, v.view);
   const Mat4 inv = Mat4::Inverse(viewProj);
   GLUtil::RunShaderPass(mGridFbo, mGridProgram, [&]
   {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mEditor->DepthTexture());
      glUniform1i(glGetUniformLocation(mGridProgram, "uDepth"), 1);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, sceneTex);
      glUniform1i(glGetUniformLocation(mGridProgram, "uScene"), 0);
      glUniformMatrix4fv(glGetUniformLocation(mGridProgram, "uInv"), 1, GL_FALSE, inv.m);
      glUniformMatrix4fv(glGetUniformLocation(mGridProgram, "uViewProj"), 1, GL_FALSE, viewProj.m);
      glUniform3fv(glGetUniformLocation(mGridProgram, "uEye"), 1, v.eye);
      glUniform1i(glGetUniformLocation(mGridProgram, "uAxis"), v.gridAxis);
      glUniform1i(glGetUniformLocation(mGridProgram, "uDraw"), v.grid && mEditor->DepthValid() ? 1 : 0);
      glUniform1f(glGetUniformLocation(mGridProgram, "uDist"), v.gridDist);
   });
   return mGridFbo.tex;
}

unsigned int Scene3DNode::RenderView(int w, int h, const View& v)
{
   // Always afresh: a node may have been deleted since the output cooked this frame.
   // The cook already ran this frame, so the sources it asks for are no-ops.
   mSyncedFrame = -1;
   Sync(mLastCookFrame);
   if (!mEditor)
   {
      mEditor = std::make_unique<Render3DNode>();
      mEditor->samples = 2;
      mEditor->RequestDepth();
   }
   Render3DNode& e = *mEditor;
   e.geometry[0] = mRoot.get();
   e.width = (float)w;
   e.height = (float)h;
   const bool final = v.shading == Shading::Final;
   const bool wire = v.shading == Shading::Wire;
   mEditorRig = final ? mRig : Render3DNode::Rig();
   mEditorRig.hasView = true;
   mEditorRig.view = v.view;
   mEditorRig.eye[0] = v.eye[0]; mEditorRig.eye[1] = v.eye[1]; mEditorRig.eye[2] = v.eye[2];
   mEditorRig.lens = nullptr;
   mEditorRig.hasProj = true;
   mEditorRig.proj = v.proj;
   mEditorRig.wireframe = wire;

   std::vector<float> stamp(v.view.m, v.view.m + 16);
   stamp.insert(stamp.end(), v.proj.m, v.proj.m + 16);
   stamp.push_back((float)v.shading);
   stamp.insert(stamp.end(), mRigStamp.begin(), mRigStamp.end());
   const Render3DNode& look = mRender;
   stamp.insert(stamp.end(), { look.exposure, (float)look.tonemap, look.bloom, look.bloomThreshold, look.bloomRadius,
                               look.aoStrength, look.aoRadius, look.dofAperture, look.focusDistance,
                               look.shadowsEnabled ? 1.0f : 0.0f, (float)look.shadowQuality, look.shadowBias,
                               look.shadowSoftness, look.shadowStrength, look.envIntensity, look.ambientColor[0],
                               look.ambientColor[1], look.ambientColor[2], worldColor[0], worldColor[1], worldColor[2],
                               worldColorOn ? 1.0f : 0.0f, (float)worldUid, worldStrength });
   if (stamp != mEditorStamp)
   {
      mEditorStamp.swap(stamp);
      mEditorRevision++;
   }
   mEditorRig.revision = mEditorRevision;
   e.rig = &mEditorRig;

   // HDR's finish reads one depth sample per pixel and would cut thin lines to dots.
   e.hdr = !wire;
   e.depthTest = !wire;
   e.backfaceCull = !wire;
   e.tonemap = mRender.tonemap;
   e.exposure = final ? mRender.exposure : 1.0f;
   e.ambientColor[0] = mRender.ambientColor[0];
   e.ambientColor[1] = mRender.ambientColor[1];
   e.ambientColor[2] = mRender.ambientColor[2];
   e.envIntensity = mRender.envIntensity;
   e.shadowsEnabled = final && mRender.shadowsEnabled;
   e.shadowQuality = mRender.shadowQuality;
   e.shadowBias = mRender.shadowBias;
   e.shadowSoftness = mRender.shadowSoftness;
   e.shadowStrength = mRender.shadowStrength;
   e.bloom = final ? mRender.bloom : 0.0f;
   e.bloomThreshold = mRender.bloomThreshold;
   e.bloomRadius = mRender.bloomRadius;
   e.aoStrength = final ? mRender.aoStrength : 0.0f;
   e.aoRadius = mRender.aoRadius;
   e.dofAperture = final ? mRender.dofAperture : 0.0f;
   e.focusDistance = mRender.focusDistance;
   ApplyWorld(e, !wire);
   e.CookIfNeeded(++mEditorFrame);

   unsigned int tex = e.GetOutputTexture();
   if (!wire && mWorldNode != nullptr && dynamic_cast<EnvironmentNode*>(mWorldNode) == nullptr &&
       mWorldNode->GetOutputTexture() != 0)
      tex = Composite(tex, e.GetOutputWidth(), e.GetOutputHeight(), mWorldNode->GetOutputTexture(), mEditorFbo);
   return tex != 0 ? GridPass(tex, e.GetOutputWidth(), e.GetOutputHeight(), v) : 0;
}

bool Scene3DNode::ItemBounds(int id, float lo[3], float hi[3])
{
   auto found = mPlaced.find(id);
   if (found == mPlaced.end() || !mEditor)
      return false;
   bool any = false;
   std::function<void(Placed&)> walk = [&](Placed& p)
   {
      float a[3], b[3];
      if (mEditor->SourceBounds(&p, a, b))
      {
         for (int k = 0; k < 3; k++)
         {
            lo[k] = any ? std::min(lo[k], a[k]) : a[k];
            hi[k] = any ? std::max(hi[k], b[k]) : b[k];
         }
         any = true;
      }
      for (auto& child : p.children)
         walk(*child);
   };
   walk(*found->second);
   return any;
}

Scene3DNode::Scene3DNode()
{
   mRender.width = 1920.0f;
   mRender.height = 1080.0f;
}
