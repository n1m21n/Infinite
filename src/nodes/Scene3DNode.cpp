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
   if (mCompositeProgram != 0)
      glDeleteProgram(mCompositeProgram);
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
     << " " << worldColor[2] << "\n";
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

bool Scene3DNode::CameraView(Mat4& view, float eye[3], CameraNode** lens) const
{
   for (const Item& it : mItems)
   {
      if (it.id != activeCamera)
         continue;
      auto* cam = dynamic_cast<CameraNode*>(ResolveUid ? ResolveUid(it.uid) : nullptr);
      if (cam == nullptr)
         return false;
      const Mat4 place = Placement(it);
      float own[3];
      cam->ComputeEye(own);
      TransformPoint(place, own, eye);
      view = Mat4::Multiply(cam->ViewMatrix(), Mat4::Inverse(place));
      if (lens != nullptr)
         *lens = cam;
      return true;
   }
   return false;
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
   }
   if (stamp != mRigStamp)
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

void Scene3DNode::ApplyWorld(Render3DNode& render, bool final)
{
   auto* env = dynamic_cast<EnvironmentNode*>(mWorldNode);
   if (env != nullptr && final)
   {
      if (render.envInput.Resolved() != env)
         render.envInput.Connect(env);
   }
   else if (render.envInput.IsConnected())
      render.envInput.Disconnect();
   render.envAsBackground = true;
   render.bgOpacity = (final && worldColorOn) ? 1.0f : 0.0f;
   std::memcpy(render.bgColor, worldColor, sizeof(worldColor));
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

unsigned int Scene3DNode::RenderView(int w, int h, const Mat4& view, const float eye[3],
                                     float fov, Shading shading, bool throughCamera)
{
   // Always afresh: a node may have been deleted since the output cooked this frame.
   // The cook already ran this frame, so the sources it asks for are no-ops.
   mSyncedFrame = -1;
   Sync(mLastCookFrame);
   if (!mEditor)
   {
      mEditor = std::make_unique<Render3DNode>();
      mEditor->hdr = true;
      mEditor->samples = 2;
   }
   Render3DNode& e = *mEditor;
   e.geometry[0] = mRoot.get();
   e.width = (float)w;
   e.height = (float)h;
   e.fov = fov;
   const bool final = shading == Shading::Final;
   mEditorRig = final ? mRig : Render3DNode::Rig();
   mEditorRig.hasView = true;
   mEditorRig.view = view;
   mEditorRig.eye[0] = eye[0]; mEditorRig.eye[1] = eye[1]; mEditorRig.eye[2] = eye[2];
   mEditorRig.lens = throughCamera ? mRig.lens : nullptr;
   mEditorRig.wireframe = shading == Shading::Wire;

   std::vector<float> stamp(view.m, view.m + 16);
   stamp.push_back(fov);
   stamp.push_back((float)shading);
   stamp.push_back(throughCamera ? 1.0f : 0.0f);
   stamp.insert(stamp.end(), mRigStamp.begin(), mRigStamp.end());
   if (stamp != mEditorStamp)
   {
      mEditorStamp.swap(stamp);
      mEditorRevision++;
   }
   mEditorRig.revision = mEditorRevision;
   e.rig = &mEditorRig;
   e.shadowsEnabled = final && mRender.shadowsEnabled;
   e.exposure = final ? mRender.exposure : 1.0f;
   e.bloom = final ? mRender.bloom : 0.0f;
   e.aoStrength = final ? mRender.aoStrength : 0.0f;
   ApplyWorld(e, final);
   e.CookIfNeeded(++mEditorFrame);

   unsigned int tex = e.GetOutputTexture();
   if (final && mWorldNode != nullptr && dynamic_cast<EnvironmentNode*>(mWorldNode) == nullptr &&
       mWorldNode->GetOutputTexture() != 0)
      tex = Composite(tex, e.GetOutputWidth(), e.GetOutputHeight(), mWorldNode->GetOutputTexture(), mEditorFbo);
   return tex;
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

Scene3DNode::Scene3DNode() = default;
