#include "Scene3DNode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <ctime>
#include <sstream>

#include "gl3.h"
#include "Platform.h"
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

   // Every saved setting in one order, so writing and reading cannot drift.
   // New ones go at the end: a line from an older patch just stops early.
   template <typename F>
   void SettingFields(Scene3DNode::Settings& s, F&& f)
   {
      f(s.fps); f(s.seconds); f(s.percent); f(s.transparent); f(s.snap);
      f(s.snapMove); f(s.snapRot); f(s.snapScale); f(s.passAlpha);
      f(s.safe); f(s.thirds); f(s.center); f(s.golden); f(s.safeTitle); f(s.safeAction);
      f(s.orientation); f(s.pivot); f(s.cursor[0]); f(s.cursor[1]); f(s.cursor[2]);
      f(s.zoomToMouse); f(s.orbitSelection); f(s.shadowItem); f(s.fStop);
      f(s.proportional); f(s.propSize); f(s.propFalloff);
      f(s.invertZoom); f(s.textInfo); f(s.stats); f(s.originsAll); f(s.gizmoMove); f(s.gizmoNav); f(s.snapObjects);
      f(s.border); f(s.crop); f(s.borderMin[0]); f(s.borderMin[1]); f(s.borderMax[0]); f(s.borderMax[1]);
      f(s.safeCenter); f(s.guideColor[0]); f(s.guideColor[1]); f(s.guideColor[2]);
      f(s.diagonal); f(s.goldenTriA); f(s.goldenTriB); f(s.harmonyTriA); f(s.harmonyTriB);
      // Block D, in the order of the struct.
      f(s.projectedProp); f(s.onlyLocations); f(s.snapAlign); f(s.snapAbsolute); f(s.snapRotFine); f(s.snapTarget);
      f(s.snapSelectable); f(s.snapTranslate); f(s.snapRotate); f(s.snapScaleOn);
      f(s.planeAxis); f(s.planeAuto); f(s.planeDepth); f(s.planeOrient);
      f(s.customCount);
      for (auto& c : s.customRot) { f(c[0]); f(c[1]); f(c[2]); }
      f(s.cursorRot[0]); f(s.cursorRot[1]); f(s.cursorRot[2]); f(s.cursorRotMode);
      f(s.lockCursor); f(s.viewLens); f(s.clipStart); f(s.clipEnd);
      f(s.gizmoLightSize); f(s.gizmoLightLook); f(s.gizmoCamLens); f(s.gizmoCamFocus);
      f(s.showCameras); f(s.showLights); f(s.pickCameras); f(s.pickLights);
      f(s.outline); f(s.outlineColor[0]); f(s.outlineColor[1]); f(s.outlineColor[2]);
      f(s.bgType); f(s.bgColor[0]); f(s.bgColor[1]); f(s.bgColor[2]);
      f(s.orthoGrid); f(s.axisX); f(s.axisY); f(s.axisZ); f(s.gridScale); f(s.gridLines); f(s.gridSubdiv);
      f(s.showCursor); f(s.showPerf); f(s.showExtras); f(s.lightColors); f(s.showFps);
      f(s.miniAxes); f(s.miniSize); f(s.miniBright); f(s.smoothTime); f(s.rotStep);
      f(s.gizmoSize); f(s.gizmoNavSize); f(s.alignNew); f(s.autoPerspective); f(s.continuousGrab); f(s.orbitSens);
      f(s.pixelAspect[0]); f(s.pixelAspect[1]); f(s.fpsBase); f(s.simplify);
      f(s.stamp); f(s.stampTime); f(s.stampDate); f(s.stampFrame); f(s.stampRange); f(s.stampCamera);
      f(s.stampLens); f(s.stampScene); f(s.stampNote); f(s.stampRender); f(s.stampLabels);
      f(s.stampSize); for (float& c : s.stampFg) f(c); for (float& c : s.stampBg) f(c);
      f(s.frameStart); f(s.frameStep);
      f(s.unitSystem); f(s.rotUnits); f(s.lengthUnit); f(s.unitScale);
      f(s.listCase); f(s.listComplete); f(s.listSort); f(s.colSelect); f(s.colRender);
      f(s.listObjects); f(s.listInvert); f(s.listLights); f(s.listCameras); f(s.listState);
      f(s.outlineSel);
      f(s.dither); f(s.filterSize); f(s.gamma); f(s.look);
   }

   void Normalize(float v[3])
   {
      const float len = std::max(1e-6f, std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
      for (int k = 0; k < 3; k++)
         v[k] /= len;
   }
   void Cross(const float a[3], const float b[3], float out[3])
   {
      out[0] = a[1] * b[2] - a[2] * b[1];
      out[1] = a[2] * b[0] - a[0] * b[2];
      out[2] = a[0] * b[1] - a[1] * b[0];
   }
   // Columns right, up, back of a frame looking along `forward`.
   void LookBasis(const float forward[3], const float upHint[3], float basis[3][3])
   {
      for (int k = 0; k < 3; k++)
         basis[2][k] = -forward[k];
      Normalize(basis[2]);
      Cross(upHint, basis[2], basis[0]);
      if (basis[0][0] * basis[0][0] + basis[0][1] * basis[0][1] + basis[0][2] * basis[0][2] < 1e-8f)
      {
         const float other[3] = { 0, 0, 1 };
         Cross(other, basis[2], basis[0]);
      }
      Normalize(basis[0]);
      Cross(basis[2], basis[0], basis[1]);
   }
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
   int passIndex = 0;
   float color[3] = { 1.0f, 1.0f, 1.0f };
   bool casts = true;
   std::vector<std::unique_ptr<Placed>> children;

   void Update(IGeometrySource* source, const Mat4& placement, unsigned int mask, int index, const float* rgb, bool castsShadow = true)
   {
      casts = castsShadow;
      src = source;
      place = placement;
      lightMask = mask;
      passIndex = index;
      std::copy(rgb, rgb + 3, color);
      const int count = src != nullptr ? src->GroupChildCount() : 0;
      children.resize(count);
      for (int i = 0; i < count; i++)
      {
         if (!children[i])
            children[i] = std::make_unique<Placed>();
         children[i]->Update(src->GroupChild(i), placement, mask, index, rgb, castsShadow);
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
   int PassIndex() const override { return passIndex; }
   void ObjectColor(float c[3]) const override { std::copy(color, color + 3, c); }
   bool CastsShadow() const override { return casts; }
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
   GLUtil::DestroyFbo(mBorderFbo);
   GLUtil::DestroyFbo(mFinishFbo);
   if (mFinishProgram != 0)
      glDeleteProgram(mFinishProgram);
   if (mStampTex != 0)
      glDeleteTextures(1, &mStampTex);
   if (mBorderProgram != 0)
      glDeleteProgram(mBorderProgram);
   if (mCompositeProgram != 0)
      glDeleteProgram(mCompositeProgram);
   if (mGridProgram != 0)
      glDeleteProgram(mGridProgram);
   for (auto& [path, pic] : mPictures)
      if (pic.tex != 0)
         glDeleteTextures(1, &pic.tex);
}

namespace
{
   Mat4 Turn(const float rot[3])
   {
      Mat4 m = Mat4::RotationZ(rot[2] * kDeg);
      m = Mat4::Multiply(m, Mat4::RotationY(rot[1] * kDeg));
      return Mat4::Multiply(m, Mat4::RotationX(rot[0] * kDeg));
   }
   // X then Y then Z angles, in degrees, of the rotation with rows `r`.
   void TurnAngles(const float r[3][3], float rot[3])
   {
      const float d = 180.0f / 3.14159265f;
      rot[1] = std::asin(std::clamp(-r[2][0], -1.0f, 1.0f)) * d;
      rot[0] = std::atan2(r[2][1], r[2][2]) * d;
      rot[2] = std::atan2(r[1][0], r[0][0]) * d;
   }
   // The motion turn, from the 3D View's axes (X, Y, Z up) into the engine's (X, Y up, -Z): about
   // view X is about engine X, about view Y is about engine -Z, about view Z is about engine Y.
   Mat4 MotionTurn(const float m[9])
   {
      Mat4 r = Mat4::RotationY(m[5] * kDeg);
      r = Mat4::Multiply(r, Mat4::RotationZ(-m[4] * kDeg));
      return Mat4::Multiply(r, Mat4::RotationX(m[3] * kDeg));
   }
   void Rows(const Mat4& m, float r[3][3])
   {
      for (int row = 0; row < 3; row++)
         for (int col = 0; col < 3; col++)
            r[row][col] = m.m[col * 4 + row];
   }
   // A placement split into where it is, how it is turned and how big it is on each of its own axes.
   struct Frame
   {
      float t[3];
      float r[3][3];
      float s[3];
   };
   Frame FrameOf(const Mat4& m)
   {
      Frame f;
      for (int k = 0; k < 3; k++)
         f.t[k] = m.m[12 + k];
      for (int col = 0; col < 3; col++)
      {
         const float* c = &m.m[col * 4];
         f.s[col] = std::max(1e-6f, std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]));
         for (int row = 0; row < 3; row++)
            f.r[row][col] = c[row] / f.s[col];
      }
      return f;
   }
   void MulRows(const float a[3][3], const float b[3][3], float out[3][3])
   {
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++)
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
   }
}

const char* Scene3DNode::MotionChannelName(int channel)
{
   static const char* kNames[9] = { "location X", "location Y", "location Z", "rotation X", "rotation Y",
                                    "rotation Z", "scale X", "scale Y", "scale Z" };
   return kNames[std::clamp(channel, 0, 8)];
}

bool Scene3DNode::FollowLoops(int id, int target)
{
   for (int hops = 0; target != 0 && hops < 64; hops++)
   {
      if (target == id)
         return true;
      const Item* t = FindItem(target);
      target = t != nullptr ? t->follow : 0;
   }
   return target != 0;
}

Mat4 Scene3DNode::Placement(const Item& it)
{
   const float* mo = it.motion;
   Mat4 m = Mat4::Translation(it.pos[0] + it.dpos[0] + mo[0], it.pos[1] + it.dpos[1] + mo[2], it.pos[2] + it.dpos[2] - mo[1]);
   m = Mat4::Multiply(m, Turn(it.rot));
   m = Mat4::Multiply(m, Turn(it.drot));
   m = Mat4::Multiply(m, MotionTurn(mo));
   return Mat4::Multiply(m, Mat4::Scale(it.scale[0] * it.dscale[0] * mo[6], it.scale[1] * it.dscale[1] * mo[8],
                                        it.scale[2] * it.dscale[2] * mo[7]));
}

void Scene3DNode::SetTurn(Item& item, const float r[3][3])
{
   // r = main * delta * motion, so main = r * transpose(delta * motion).
   const Mat4 dm = Mat4::Multiply(Turn(item.drot), MotionTurn(item.motion));
   float main[3][3];
   for (int row = 0; row < 3; row++)
      for (int col = 0; col < 3; col++)
         main[row][col] = r[row][0] * dm.m[col] + r[row][1] * dm.m[4 + col] + r[row][2] * dm.m[8 + col];
   TurnAngles(main, item.rot);
}

void Scene3DNode::TransformsToDeltas(Item& item)
{
   const Mat4 whole = Mat4::Multiply(Turn(item.rot), Turn(item.drot));
   float r[3][3];
   for (int row = 0; row < 3; row++)
      for (int col = 0; col < 3; col++)
         r[row][col] = whole.m[col * 4 + row];
   for (int k = 0; k < 3; k++)
   {
      item.dpos[k] += item.pos[k];
      item.dscale[k] *= item.scale[k];
      item.pos[k] = 0.0f;
      item.scale[k] = 1.0f;
      item.rot[k] = 0.0f;
   }
   TurnAngles(r, item.drot);
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
   for (Item& it : Items())
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
   AssignMotionSlots();
   Commit();
   return mItems.back();
}

void Scene3DNode::RemoveItem(int id)
{
   mItems.erase(std::remove_if(mItems.begin(), mItems.end(), [&](const Item& it) { return it.id == id; }),
                mItems.end());
   if (activeCamera == id)
      activeCamera = 0;
   if (focusItem == id)
      focusItem = 0;
   if (settings.shadowItem == id)
      settings.shadowItem = 0;
   for (Item& it : mItems)
      if (it.follow == id)
         it.follow = 0;   // the follower stays where it is
   mPlaced.erase(id);
   Commit();
}

void Scene3DNode::AssignMotionSlots()
{
   std::vector<bool> used(kMotionSlots + 1, false);
   for (const Item& it : mItems)
      if (it.motionSlot >= 1 && it.motionSlot <= kMotionSlots && !used[it.motionSlot])
         used[it.motionSlot] = true;
      else
         const_cast<Item&>(it).motionSlot = 0;
   int next = 1;
   for (Item& it : mItems)
      if (it.motionSlot == 0)
      {
         while (next <= kMotionSlots && used[next])
            next++;
         if (next > kMotionSlots)
            break;
         it.motionSlot = next;
         used[next] = true;
      }
}

// Each follower's place, from its target's and the offset it keeps in the target's space. A follower a tool
// has moved since (it is not where Follow last put it) first takes its offset again, so it stays put.
void Scene3DNode::ApplyFollow()
{
   for (int pass = 0; pass < 8; pass++)   // chains settle from the root out
      for (Item& it : mItems)
      {
         const Item* target = it.follow != 0 && it.follow != it.id ? FindItem(it.follow) : nullptr;
         if (target == nullptr)
            continue;
         float now[9];
         std::copy(it.pos, it.pos + 3, now);
         std::copy(it.rot, it.rot + 3, now + 3);
         std::copy(it.scale, it.scale + 3, now + 6);
         if (!std::equal(now, now + 9, it.followed))
            TakeFollowOffset(it, *target);
         const Frame tf = FrameOf(Placement(*target));
         if (it.followMode == 1)
            for (int k = 0; k < 3; k++)
               it.pos[k] = tf.t[k] + it.fpos[k];
         else
         {
            for (int row = 0; row < 3; row++)
            {
               it.pos[row] = tf.t[row];
               for (int col = 0; col < 3; col++)
                  it.pos[row] += tf.r[row][col] * tf.s[col] * it.fpos[col];
            }
            float local[3][3], world[3][3];
            Rows(Turn(it.frot), local);
            MulRows(tf.r, local, world);
            TurnAngles(world, it.rot);
            for (int k = 0; k < 3; k++)
               it.scale[k] = it.fscale[k] * tf.s[k];
         }
         std::copy(it.pos, it.pos + 3, it.followed);
         std::copy(it.rot, it.rot + 3, it.followed + 3);
         std::copy(it.scale, it.scale + 3, it.followed + 6);
      }
}

void Scene3DNode::TakeFollowOffset(Item& it, const Item& target)
{
   const Frame tf = FrameOf(Placement(target));
   float d[3];
   for (int k = 0; k < 3; k++)
      d[k] = it.pos[k] - tf.t[k];
   if (it.followMode == 1)
   {
      std::copy(d, d + 3, it.fpos);
      return;
   }
   for (int col = 0; col < 3; col++)
      it.fpos[col] = (tf.r[0][col] * d[0] + tf.r[1][col] * d[1] + tf.r[2][col] * d[2]) / tf.s[col];
   float own[3][3], tT[3][3], local[3][3];
   Rows(Turn(it.rot), own);
   for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
         tT[i][j] = tf.r[j][i];
   MulRows(tT, own, local);
   TurnAngles(local, it.frot);
   for (int k = 0; k < 3; k++)
      it.fscale[k] = it.scale[k] / tf.s[k];
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
   if (panelOnly)
      o << "panel 1\n";
   o << "world " << worldUid << " " << (worldColorOn ? 1 : 0) << " " << worldColor[0] << " " << worldColor[1]
     << " " << worldColor[2] << " " << worldStrength << " " << focusItem << "\n";
   o << "settings";
   SettingFields(settings, [&](auto& v) { o << " " << +v; });
   o << "\n";
   if (!stampNote.empty())
      o << "stampnote " << stampNote << "\n";
   for (const Item& it : mItems)
   {
      o << "item " << it.id << " " << it.uid;
      for (float v : it.pos) o << " " << v;
      for (float v : it.rot) o << " " << v;
      for (float v : it.scale) o << " " << v;
      o << " " << (it.visible ? 1 : 0);
      for (uint64_t u : it.unlit) o << " " << u;
      o << "\n";
      if (!it.name.empty())
         o << "label " << it.id << " " << it.name << "\n";
      if (!it.render)
         o << "itemx " << it.id << " 0\n";
      if (it.group != 0)
         o << "group " << it.id << " " << it.group << "\n";
      if (it.lock != 0)
         o << "lock " << it.id << " " << it.lock << "\n";
      if (it.lookAt != 0)
         o << "look " << it.id << " " << it.lookAt << "\n";
      if (it.motionSlot != 0)
      {
         o << "motion " << it.id << " " << it.motionSlot;
         for (float v : it.motion) o << " " << v;
         o << "\n";
      }
      if (it.follow != 0)
      {
         o << "follow " << it.id << " " << it.follow << " " << it.followMode;
         for (float v : it.fpos) o << " " << v;
         for (float v : it.frot) o << " " << v;
         for (float v : it.fscale) o << " " << v;
         o << "\n";
      }
      if (it.color[0] != 1.0f || it.color[1] != 1.0f || it.color[2] != 1.0f)
         o << "ocolor " << it.id << " " << it.color[0] << " " << it.color[1] << " " << it.color[2] << "\n";
      if (!it.castShadow)
         o << "shadowoff " << it.id << "\n";
      if (it.passIndex != 0)
         o << "pass " << it.id << " " << it.passIndex << "\n";
      if (it.trackAxis != 5 || it.upAxis != 1 || it.lookInfluence != 1.0f || it.lookTargetZ)
         o << "lookx " << it.id << " " << it.trackAxis << " " << it.upAxis << " " << it.lookInfluence << " "
           << (it.lookTargetZ ? 1 : 0) << "\n";
      if (it.hideSelect || it.showBounds || it.boundsType != 0 || it.showName || it.showAxes)
         o << "show " << it.id << " " << (it.hideSelect ? 1 : 0) << " " << (it.showBounds ? 1 : 0) << " " << it.boundsType
           << " " << (it.showName ? 1 : 0) << " " << (it.showAxes ? 1 : 0) << "\n";
      if (!it.backdrop.path.empty())
      {
         const Backdrop& b = it.backdrop;
         o << "backdrop " << it.id << " " << (b.show ? 1 : 0) << " " << b.alpha << " " << (b.front ? 1 : 0) << " "
           << b.fit << " " << b.offset[0] << " " << b.offset[1] << " " << b.rotation << " " << b.scale << " "
           << (b.flipX ? 1 : 0) << " " << (b.flipY ? 1 : 0) << " " << b.path << "\n";
      }
      const bool delta = it.dpos[0] != 0 || it.dpos[1] != 0 || it.dpos[2] != 0 || it.drot[0] != 0 ||
                         it.drot[1] != 0 || it.drot[2] != 0 || it.dscale[0] != 1 || it.dscale[1] != 1 ||
                         it.dscale[2] != 1;
      if (delta)
      {
         o << "delta " << it.id;
         for (float v : it.dpos) o << " " << v;
         for (float v : it.drot) o << " " << v;
         for (float v : it.dscale) o << " " << v;
         o << "\n";
      }
   }
   mText = o.str();
   mParsedText = mText;
}

void Scene3DNode::Parse()
{
   mParsedText = mText;
   mItems.clear();
   settings = Settings();
   panelOnly = false;
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
      else if (key == "panel")
         panelOnly = true;
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
      else if (key == "stampnote")
      {
         l >> std::ws;
         std::getline(l, stampNote);
      }
      else if (key == "settings")
         SettingFields(settings, [&](auto& v)
         {
            float x = 0;
            if (l >> x)
               v = (std::decay_t<decltype(v)>)x;
         });
      else if (key == "label" || key == "itemx" || key == "group" || key == "lock" || key == "look" ||
               key == "delta" || key == "pass" || key == "ocolor" || key == "shadowoff" || key == "backdrop" || key == "lookx" || key == "show" ||
               key == "motion" || key == "follow")
      {
         int id = 0;
         l >> id;
         Item* it = FindItem(id);
         if (it == nullptr)
            continue;
         if (key == "label")
         {
            l >> std::ws;
            std::getline(l, it->name);
         }
         else if (key == "itemx")
         {
            int on = 1;
            l >> on;
            it->render = on != 0;
         }
         else if (key == "lock")
            l >> it->lock;
         else if (key == "look")
            l >> it->lookAt;
         else if (key == "motion")
         {
            l >> it->motionSlot;
            for (float& v : it->motion) l >> v;
         }
         else if (key == "follow")
         {
            l >> it->follow >> it->followMode;
            for (float& v : it->fpos) l >> v;
            for (float& v : it->frot) l >> v;
            for (float& v : it->fscale) l >> v;
         }
         else if (key == "pass")
            l >> it->passIndex;
         else if (key == "shadowoff")
            it->castShadow = false;
         else if (key == "ocolor")
            l >> it->color[0] >> it->color[1] >> it->color[2];
         else if (key == "lookx")
         {
            int tz = 0;
            l >> it->trackAxis >> it->upAxis >> it->lookInfluence >> tz;
            it->lookTargetZ = tz != 0;
         }
         else if (key == "show")
         {
            int a = 0, b = 0, c = 0, d = 0;
            l >> a >> b >> it->boundsType >> c >> d;
            it->hideSelect = a != 0; it->showBounds = b != 0; it->showName = c != 0; it->showAxes = d != 0;
         }
         else if (key == "backdrop")
         {
            Backdrop& b = it->backdrop;
            int show = 1, front = 0, fx = 0, fy = 0;
            l >> show >> b.alpha >> front >> b.fit >> b.offset[0] >> b.offset[1] >> b.rotation >> b.scale >> fx >> fy >> std::ws;
            std::getline(l, b.path);
            b.show = show != 0;
            b.front = front != 0;
            b.flipX = fx != 0;
            b.flipY = fy != 0;
         }
         else if (key == "delta")
         {
            for (float& v : it->dpos) l >> v;
            for (float& v : it->drot) l >> v;
            for (float& v : it->dscale) l >> v;
         }
         else
            l >> it->group;
      }
   }
   AssignMotionSlots();
}

void Scene3DNode::VisitParams(ParamVisitor& v)
{
   v.Float("width", renderW);
   v.Float("height", renderH);
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
   v.Int("dofBlades", mRender.dofBlades);
   v.Float("dofRotation", mRender.dofRotation);
   v.Float("dofRatio", mRender.dofRatio);
   v.Color("ambient", mRender.ambientColor);
   v.Float("envIntensity", mRender.envIntensity);
   v.Bool("floatOutput", mRender.floatOutput);
   v.Int("renderPass", mRender.renderPass);
   v.Float("mistStart", mRender.mistStart);
   v.Float("mistDepth", mRender.mistDepth);
   v.Int("mistFalloff", mRender.mistFalloff);
   v.Float("mistIntensity", mRender.mistIntensity);
   v.Float("mistHeight", mRender.mistHeight);
   v.Float("passThreshold", mRender.passThreshold);
   v.Bool("cavity", mRender.cavity);
   v.Int("cavityType", mRender.cavityType);
   v.Float("cavityRidge", mRender.cavityRidge);
   v.Float("cavityValley", mRender.cavityValley);
   v.Float("curvatureRidge", mRender.curvatureRidge);
   v.Float("curvatureValley", mRender.curvatureValley);
   v.Float("cavityDistance", mRender.cavityDistance);
   v.Bool("motionBlur", mRender.motionBlur);
   v.Float("motionShutter", mRender.motionShutter);
   v.Int("motionPosition", mRender.motionPosition);
   v.Float("motionMax", mRender.motionMax);
   v.Int("viewShading", mRender.viewShading);
   v.Int("solidLighting", mRender.solidLighting);
   v.Int("solidColor", mRender.solidColor);
   v.Color("solidSingle", mRender.solidSingle);
   v.Int("matcap", mRender.matcap);
   v.Bool("solidSpecular", mRender.solidSpecular);
   v.Bool("studioWorld", mRender.studioWorld);
   v.Float("studioRotation", mRender.studioRotation);
   v.Int("projection", mRender.projection);
   v.Float("domeAngle", mRender.domeAngle);
   v.Float("domeTilt", mRender.domeTilt);
   v.Float("latMin", mRender.latMin);
   v.Float("latMax", mRender.latMax);
   v.Float("lonMin", mRender.lonMin);
   v.Float("lonMax", mRender.lonMax);
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

void Scene3DNode::ItemCenter(const Item& item, float out[3])
{
   if (ItemPoint(item, out))
      return;
   float lo[3], hi[3];
   if (ItemBounds(item.id, lo, hi))
   {
      for (int k = 0; k < 3; k++)
         out[k] = (lo[k] + hi[k]) * 0.5f;
      return;
   }
   std::memcpy(out, item.pos, sizeof(item.pos));
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
   float r[3][3];
   for (int k = 0; k < 3; k++)
   {
      item.pos[k] = place.m[12 + k] - item.dpos[k];
      item.scale[k] = 1.0f / (item.dscale[k] != 0.0f ? item.dscale[k] : 1.0f);
      for (int col = 0; col < 3; col++)
         r[k][col] = place.m[col * 4 + k];
   }
   SetTurn(item, r);
}

bool Scene3DNode::AimAt(Item& item, const float target[3], const float* targetUp) const
{
   INode* node = ResolveUid ? ResolveUid(item.uid) : nullptr;
   float own[3], forward[3], up[3] = { 0, 1, 0 };
   if (auto* cam = dynamic_cast<CameraNode*>(node))
   {
      cam->ComputeEye(own);
      const Mat4 v = cam->ViewMatrix();
      forward[0] = -v.m[2]; forward[1] = -v.m[6]; forward[2] = -v.m[10];
      up[0] = v.m[1]; up[1] = v.m[5]; up[2] = v.m[9];
   }
   else if (auto* light = dynamic_cast<LightNode*>(node))
   {
      light->ComputeVector(own);
      for (int k = 0; k < 3; k++)
         forward[k] = -own[k];
      if (!LightIsPositioned(light->type))
         for (float& v : own)
            v *= light->distance;
   }
   else
      return false;

   float point[3];
   TransformPoint(Placement(item), own, point);
   float wanted[3] = { target[0] - point[0], target[1] - point[1], target[2] - point[2] };
   if (wanted[0] * wanted[0] + wanted[1] * wanted[1] + wanted[2] * wanted[2] < 1e-8f)
      return false;

   // The item's own axes in the node's frame, so any of them can be the one that points at the target.
   float axes[3][3];
   Normalize(forward);
   Cross(forward, up, axes[0]);
   Normalize(axes[0]);
   std::copy(up, up + 3, axes[1]);
   for (int k = 0; k < 3; k++)
      axes[2][k] = -forward[k];
   const int track = std::clamp(item.trackAxis, 0, 5);
   float trackVec[3], upVec[3];
   for (int k = 0; k < 3; k++)
   {
      trackVec[k] = axes[track % 3][k] * (track >= 3 ? -1.0f : 1.0f);
      upVec[k] = axes[std::clamp(item.upAxis, 0, 2)][k];
   }

   float worldUp[3] = { 0, 1, 0 };
   if (item.lookTargetZ && targetUp != nullptr)
      std::copy(targetUp, targetUp + 3, worldUp);
   float from[3][3], to[3][3], r[3][3];
   LookBasis(trackVec, upVec, from);
   LookBasis(wanted, worldUp, to);
   for (int row = 0; row < 3; row++)
      for (int col = 0; col < 3; col++)
         r[row][col] = to[0][row] * from[0][col] + to[1][row] * from[1][col] + to[2][row] * from[2][col];

   // Influence: slide from the unturned pose to the full aim along the shortest turn.
   const float infl = std::clamp(item.lookInfluence, 0.0f, 1.0f);
   if (infl < 1.0f)
   {
      const float tr = r[0][0] + r[1][1] + r[2][2];
      const float angle = std::acos(std::clamp((tr - 1.0f) * 0.5f, -1.0f, 1.0f));
      float ax[3] = { r[2][1] - r[1][2], r[0][2] - r[2][0], r[1][0] - r[0][1] };
      float len = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
      if (len < 1e-5f)   // a half turn (or none): the axis is the biggest diagonal term
      {
         int m = r[0][0] >= r[1][1] && r[0][0] >= r[2][2] ? 0 : r[1][1] >= r[2][2] ? 1 : 2;
         ax[0] = ax[1] = ax[2] = 0.0f;
         ax[m] = 1.0f;
         len = 1.0f;
      }
      for (float& c : ax)
         c /= len;
      const float c = std::cos(angle * infl), sn = std::sin(angle * infl), t = 1.0f - c;
      r[0][0] = t * ax[0] * ax[0] + c;          r[0][1] = t * ax[0] * ax[1] - sn * ax[2]; r[0][2] = t * ax[0] * ax[2] + sn * ax[1];
      r[1][0] = t * ax[0] * ax[1] + sn * ax[2]; r[1][1] = t * ax[1] * ax[1] + c;          r[1][2] = t * ax[1] * ax[2] - sn * ax[0];
      r[2][0] = t * ax[0] * ax[2] - sn * ax[1]; r[2][1] = t * ax[1] * ax[2] + sn * ax[0]; r[2][2] = t * ax[2] * ax[2] + c;
   }

   // r turns the node's own frame; an item already carrying a delta turn keeps it.
   SetTurn(item, r);

   // The point stays put; the turn must not swing it about the node's origin.
   Item turned = item;
   std::memset(turned.pos, 0, sizeof(turned.pos));
   float swung[3];
   TransformPoint(Placement(turned), own, swung);
   for (int k = 0; k < 3; k++)
      item.pos[k] = point[k] - swung[k];
   return true;
}

void Scene3DNode::Sync(int frameId)
{
   if (mSyncedFrame == frameId)
      return;
   mSyncedFrame = frameId;
   if (mText != mParsedText)
      Parse();
   ApplyFollow();
   // Items told to look at another keep facing it as it moves or animates.
   for (Item& it : mItems)
      if (it.lookAt != 0 && it.lookAt != it.id)
         if (Item* target = FindItem(it.lookAt))
         {
            float c[3], tUp[3];
            ItemCenter(*target, c);
            TransformDir(Placement(*target), std::array<float, 3>{ 0, 1, 0 }.data(), tUp);
            Normalize(tUp);
            AimAt(it, c, tUp);
         }
   if (!mRoot)
   {
      mRoot = std::make_unique<Root>();
      mOutRoot = std::make_unique<Root>();
      mLocalRoot = std::make_unique<Root>();
   }

   // A deleted world node reads as no world, in the same breath as its uid.
   mWorldNode = (worldUid != 0 && ResolveUid) ? ResolveUid(worldUid) : nullptr;
   if (mWorldNode != nullptr && mWorldNode->bypassed)
      mWorldNode = nullptr;
   if (mWorldNode != nullptr)
      mWorldNode->CookIfNeeded(frameId);

   mRender.width = std::max(16.0f, std::round(renderW * settings.percent * 0.01f));
   mRender.height = std::max(16.0f, std::round(renderH * settings.percent * 0.01f));

   // The renderer shadows from its first light, so the chosen one goes first.
   std::vector<const Item*> lightItems;
   for (const Item& it : mItems)
      if (it.id == settings.shadowItem)
         lightItems.push_back(&it);
   for (const Item& it : mItems)
      if (it.id != settings.shadowItem)
         lightItems.push_back(&it);

   // The lights first: each item's mask is by their order.
   std::vector<uint64_t> lightOrder;
   std::vector<float> marks;
   mRig.lights.clear();
   for (const Item* item : lightItems)
   {
      const Item& it = *item;
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
      l.From(*light);
      mRig.lights.push_back(l);
      lightOrder.push_back(it.uid);
      marks.push_back(it.render ? 1.0f : 0.0f);
   }
   mRig.hasLights = !mRig.lights.empty();

   mRoot->draws.clear();
   mOutRoot->draws.clear();
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
      placed->Update(source, Placement(it), mask, it.passIndex, it.color, it.castShadow);
      mRoot->draws.push_back(placed.get());
      if (it.render)
         mOutRoot->draws.push_back(placed.get());
      marks.push_back((float)(mask & 0xffffu));
   }
   mRender.geometry[0] = mOutRoot.get();
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
      stamp.insert(stamp.end(), { l.intensity, l.diffuse, l.specular, l.transmission, l.falloff,
                                  l.castShadow ? 1.0f : 0.0f, l.shadowSoft, (float)l.shadowRes, l.shadowClip, l.cutoff });
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
      float eye[3], c[3];
      if (CameraView(view, eye, nullptr))
      {
         ItemCenter(*target, c);
         mRender.focusDistance = std::max(0.05f, -(view.m[2] * c[0] + view.m[6] * c[1] + view.m[10] * c[2] + view.m[14]));
      }
   }
   // A real f-stop: the thin lens's blur circle as a share of the sensor height,
   // in the units dofAperture counts (24 px across a 1080 frame).
   Mat4 lensView;
   float lensEye[3];
   CameraNode* lens = nullptr;
   if (settings.fStop > 0.0f && CameraView(lensView, lensEye, &lens) && lens != nullptr)
   {
      const float aspect = std::max(1.0f, renderW) / std::max(1.0f, renderH);
      const float mm = lens->FocalLength(aspect);
      const float focusMm = std::max(mRender.focusDistance * 1000.0f, mm * 1.01f);
      mRender.dofAperture = mm * mm / (settings.fStop * (focusMm - mm)) / lens->SensorSpan(aspect) * 45.0f;
   }
   stamp.push_back(mRender.focusDistance);
   stamp.insert(stamp.end(), marks.begin(), marks.end());
   for (int shift = 0; shift < 64; shift += 16)
      stamp.push_back((float)((worldUid >> shift) & 0xffffu));
   if (stamp != mRigStamp)
   {
      mRigStamp.swap(stamp);
      mRig.revision++;
   }

   // What the picture sees: the same rig, less the lights kept out of renders.
   mOutRig = mRig;
   size_t lightIndex = 0;
   for (const Item* item : lightItems)
   {
      auto* light = dynamic_cast<LightNode*>(ResolveUid ? ResolveUid(item->uid) : nullptr);
      if (light == nullptr || light->bypassed || !item->visible || lightIndex >= mOutRig.lights.size())
         continue;
      if (!item->render)
         mOutRig.lights[lightIndex].intensity = 0.0f;
      lightIndex++;
   }
   mRender.rig = &mOutRig;
}

void Scene3DNode::ApplyWorld(Render3DNode& render, bool show, bool background)
{
   auto* env = dynamic_cast<EnvironmentNode*>(mWorldNode);
   if (env != nullptr && show)
   {
      if (render.envInput.Resolved() != env)
         render.envInput.Connect(env);
   }
   else if (render.envInput.IsConnected())
      render.envInput.Disconnect();
   render.envAsBackground = background;
   render.bgOpacity = (show && background && worldColorOn && mWorldNode == nullptr) ? 1.0f : 0.0f;
   // A world colour is what surfaces reflect and what lights them from all sides; with none set
   // they keep the default sky.
   const bool colorWorld = show && worldColorOn && mWorldNode == nullptr;
   for (int k = 0; k < 3; k++)
   {
      render.bgColor[k] = worldColor[k] * worldStrength;
      render.envSky[k] = colorWorld ? render.bgColor[k] : Render3DNode::kEnvSky[k];
      render.envHorizon[k] = colorWorld ? render.bgColor[k] : Render3DNode::kEnvHorizon[k];
      render.envGround[k] = colorWorld ? render.bgColor[k] : Render3DNode::kEnvGround[k];
   }
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

// The render region: clear outside it, or with crop the picture is just the region.
unsigned int Scene3DNode::Border(unsigned int sceneTex, int w, int h)
{
   const float x0 = std::clamp(std::min(settings.borderMin[0], settings.borderMax[0]), 0.0f, 1.0f);
   const float y0 = std::clamp(std::min(settings.borderMin[1], settings.borderMax[1]), 0.0f, 1.0f);
   const float x1 = std::clamp(std::max(settings.borderMin[0], settings.borderMax[0]), 0.0f, 1.0f);
   const float y1 = std::clamp(std::max(settings.borderMin[1], settings.borderMax[1]), 0.0f, 1.0f);
   const int ow = settings.crop ? std::max(1, (int)std::lround((x1 - x0) * w)) : w;
   const int oh = settings.crop ? std::max(1, (int)std::lround((y1 - y0) * h)) : h;
   if (mBorderProgram == 0)
      mBorderProgram = GLUtil::CompileProgram(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uScene;\n"
         "uniform vec4 uRegion;\n"
         "uniform int uCrop;\n"
         "void main() {\n"
         "   if (uCrop == 1) { fragColor = texture(uScene, mix(uRegion.xy, uRegion.zw, vUv)); return; }\n"
         "   bool inside = all(greaterThanEqual(vUv, uRegion.xy)) && all(lessThanEqual(vUv, uRegion.zw));\n"
         "   fragColor = inside ? texture(uScene, vUv) : vec4(0.0);\n"
         "}\n");
   if (mBorderProgram == 0 || !GLUtil::EnsureFbo(mBorderFbo, ow, oh))
      return sceneTex;
   GLUtil::RunShaderPass(mBorderFbo, mBorderProgram, [&]
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, sceneTex);
      glUniform1i(glGetUniformLocation(mBorderProgram, "uScene"), 0);
      glUniform4f(glGetUniformLocation(mBorderProgram, "uRegion"), x0, y0, x1, y1);
      glUniform1i(glGetUniformLocation(mBorderProgram, "uCrop"), settings.crop ? 1 : 0);
   });
   mOutW = ow;
   mOutH = oh;
   return mBorderFbo.tex;
}

// The picture's last touches: filter, gamma, look, dither and the burned-in stamp.
unsigned int Scene3DNode::Finish(unsigned int sceneTex, int w, int h, int frameId)
{
   const Settings& st = settings;
   const bool tint = st.dither > 0.0f || st.filterSize > 0.0f || st.gamma != 1.0f || st.look != 0;
   std::string text;
   if (st.stamp)
   {
      auto add = [&](const char* label, const std::string& value)
      {
         if (!text.empty())
            text += "   ";
         text += (st.stampLabels ? std::string(label) + " " : std::string()) + value;
      };
      char buf[96];
      std::time_t now = std::time(nullptr);
      std::tm tmv{};
#ifdef _WIN32
      localtime_s(&tmv, &now);
#else
      localtime_r(&now, &tmv);
#endif
      if (st.stampDate) { std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv); add("Date", buf); }
      if (st.stampTime) { std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmv); add("Time", buf); }
      if (st.stampFrame) add("Frame", std::to_string(frameId));
      if (st.stampRange) add("Range", std::to_string(st.frameStart) + "-" + std::to_string((int)(st.seconds * st.fps)));
      Item* cam = activeCamera != 0 ? FindItem(activeCamera) : nullptr;
      if (st.stampCamera) add("Camera", cam != nullptr && !cam->name.empty() ? cam->name : std::string("Camera"));
      if (st.stampLens)
      {
         INode* n = cam != nullptr && ResolveUid ? ResolveUid(cam->uid) : nullptr;
         if (auto* cn = dynamic_cast<CameraNode*>(n))
         {
            std::snprintf(buf, sizeof(buf), "%.0f mm", 12.0f / std::tan(cn->fov * 0.5f * 3.14159265f / 180.0f));
            add("Lens", buf);
         }
      }
      if (st.stampScene) add("Scene", "Scene");
      if (st.stampRender) { std::snprintf(buf, sizeof(buf), "%.1f ms", mCookMs); add("Render", buf); }
      if (st.stampNote && !stampNote.empty()) add("Note", stampNote);
   }
   const bool stamped = !text.empty();
   if (!tint && !stamped)
      return sceneTex;
   if (mFinishProgram == 0)
      mFinishProgram = GLUtil::CompileProgram(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uScene;\n"
         "uniform sampler2D uStamp;\n"
         "uniform vec2 uTexel;\n"
         "uniform float uFilter, uGamma, uDither;\n"
         "uniform int uLook, uStamped;\n"
         "float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"
         "void main() {\n"
         "   vec4 c = texture(uScene, vUv);\n"
         "   if (uFilter > 0.0) {\n"
         "      vec4 sum = c;\n"
         "      for (int i = 0; i < 8; i++) {\n"
         "         float a = 0.785398 * float(i);\n"
         "         sum += texture(uScene, vUv + vec2(cos(a), sin(a)) * uTexel * uFilter);\n"
         "      }\n"
         "      c = sum / 9.0;\n"
         "   }\n"
         "   c.rgb = pow(max(c.rgb, vec3(0.0)), vec3(1.0 / max(uGamma, 0.05)));\n"
         "   if (uLook > 0) {\n"
         "      float k = uLook == 1 ? 0.25 : uLook == 2 ? 0.5 : uLook == 3 ? 0.8 : -0.35;\n"
         "      vec3 sc = clamp(c.rgb, 0.0, 1.0); sc = sc * sc * (3.0 - 2.0 * sc);\n"
         "      c.rgb = mix(c.rgb, sc, abs(k));\n"
         "      if (k < 0.0) c.rgb = mix(c.rgb, vec3(dot(c.rgb, vec3(0.299, 0.587, 0.114))) * 0.9 + 0.05, 0.3);\n"
         "   }\n"
         "   if (uDither > 0.0) c.rgb += (hash(vUv / uTexel) - 0.5) * uDither / 255.0 * 2.0;\n"
         "   if (uStamped == 1) {\n"
         "      vec4 t = texture(uStamp, vUv);\n"
         "      c.rgb = mix(c.rgb, t.rgb, t.a);\n"
         "      c.a = max(c.a, t.a);\n"
         "   }\n"
         "   fragColor = c;\n"
         "}\n");
   if (mFinishProgram == 0 || !GLUtil::EnsureFbo(mFinishFbo, w, h))
      return sceneTex;
   if (stamped)
   {
      char key[160];
      std::snprintf(key, sizeof(key), "%d %d %g %g %g %g %g %g %g|", w, h, st.stampSize, st.stampFg[0], st.stampFg[1],
                    st.stampFg[2], st.stampBg[0], st.stampBg[1], st.stampBg[2]);
      const std::string want = key + text;
      if (want != mStampKey || mStampTex == 0)
      {
         mStampKey = want;
         const int bar = std::max(4, (int)std::lround(st.stampSize * 1.8f * h / 720.0f));
         std::vector<unsigned char> px((size_t)w * h * 4, 0);
         for (int y = h - bar; y < h; y++)
            for (int x = 0; x < w; x++)
            {
               unsigned char* p = &px[((size_t)y * w + x) * 4];
               p[0] = (unsigned char)(st.stampBg[0] * 255.0f);
               p[1] = (unsigned char)(st.stampBg[1] * 255.0f);
               p[2] = (unsigned char)(st.stampBg[2] * 255.0f);
               p[3] = 255;
            }
         std::vector<unsigned char> glyphs((size_t)w * bar * 4, 0);
         Platform::TextRasterRequest req;
         req.text = text;
         req.fontSize = st.stampSize * h / 720.0f;
         req.color[0] = st.stampFg[0]; req.color[1] = st.stampFg[1]; req.color[2] = st.stampFg[2];
         req.align = 0;
         req.posX = 0.01f;
         req.posY = 0.5f;
         float fitted = 0;
         Platform::RasterizeText(req, w, bar, glyphs.data(), fitted);
         for (int y = 0; y < bar; y++)
            for (int x = 0; x < w; x++)
            {
               const unsigned char* g = &glyphs[((size_t)y * w + x) * 4];
               unsigned char* p = &px[((size_t)(h - bar + y) * w + x) * 4];
               const float a = g[3] / 255.0f;
               for (int k = 0; k < 3; k++)
                  p[k] = (unsigned char)(p[k] * (1.0f - a) + g[k] * a);
            }
         // The rasterizer's top row is first; GL's row 0 is the bottom, so the bar lands at the bottom after a flip.
         std::vector<unsigned char> flipped(px.size());
         for (int y = 0; y < h; y++)
            std::memcpy(&flipped[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
         if (mStampTex == 0)
            glGenTextures(1, &mStampTex);
         glBindTexture(GL_TEXTURE_2D, mStampTex);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
   }
   GLUtil::RunShaderPass(mFinishFbo, mFinishProgram, [&]
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, sceneTex);
      glUniform1i(glGetUniformLocation(mFinishProgram, "uScene"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, stamped ? mStampTex : 0);
      glUniform1i(glGetUniformLocation(mFinishProgram, "uStamp"), 1);
      glActiveTexture(GL_TEXTURE0);
      glUniform2f(glGetUniformLocation(mFinishProgram, "uTexel"), 1.0f / w, 1.0f / h);
      glUniform1f(glGetUniformLocation(mFinishProgram, "uFilter"), st.filterSize);
      glUniform1f(glGetUniformLocation(mFinishProgram, "uGamma"), st.gamma);
      glUniform1f(glGetUniformLocation(mFinishProgram, "uDither"), st.dither);
      glUniform1i(glGetUniformLocation(mFinishProgram, "uLook"), st.look);
      glUniform1i(glGetUniformLocation(mFinishProgram, "uStamped"), stamped ? 1 : 0);
   });
   return mFinishFbo.tex;
}

void Scene3DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   Sync(frameId);
   ApplyWorld(mRender, true, !settings.transparent);
   // Simplify: cook with the cheapest sampling, for a quick look.
   const int keptSamples = mRender.samples, keptSuper = mRender.supersample;
   if (settings.simplify)
   {
      mRender.samples = 0;
      mRender.supersample = 1;
   }
   const auto cookStart = std::chrono::steady_clock::now();
   mRender.CookIfNeeded(frameId);
   mCookMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - cookStart).count();
   mRender.samples = keptSamples;
   mRender.supersample = keptSuper;

   unsigned int backdrop = 0;
   if (mWorldNode != nullptr && dynamic_cast<EnvironmentNode*>(mWorldNode) == nullptr && !settings.transparent &&
       mRender.renderPass == 0)
      backdrop = mWorldNode->GetOutputTexture();
   mOutTex = mRender.GetOutputTexture();
   if (backdrop != 0)
      mOutTex = Composite(mOutTex, mRender.GetOutputWidth(), mRender.GetOutputHeight(), backdrop, mOutFbo);
   mOutW = mOutH = 0;
   if (settings.border)
      mOutTex = Border(mOutTex, mRender.GetOutputWidth(), mRender.GetOutputHeight());
   {
      const int fw = mOutW > 0 ? mOutW : mRender.GetOutputWidth(), fh = mOutH > 0 ? mOutH : mRender.GetOutputHeight();
      mOutTex = Finish(mOutTex, fw, fh, frameId);
   }
   unsigned long long rev = mRender.TextureRevision() + (backdrop != 0 ? mWorldNode->TextureRevision() : 0);
   if (settings.border)
   {
      // Moving the region changes the picture without a new render.
      unsigned long long region = settings.crop ? 7 : 3;
      for (float f : { settings.borderMin[0], settings.borderMin[1], settings.borderMax[0], settings.borderMax[1] })
      {
         uint32_t bits;
         std::memcpy(&bits, &f, sizeof(bits));
         region = region * 1099511628211ull ^ bits;
      }
      rev += region;
   }
   {
      // The finish changes the picture without a new render.
      unsigned long long fin = 11;
      std::vector<float> vals = { settings.dither, settings.filterSize, settings.gamma, (float)settings.look, (float)settings.stamp,
                                  settings.stampSize, (float)(settings.stampTime + 2 * settings.stampDate + 4 * settings.stampFrame +
                                  8 * settings.stampRange + 16 * settings.stampCamera + 32 * settings.stampLens +
                                  64 * settings.stampScene + 128 * settings.stampNote + 256 * settings.stampLabels + 512 * settings.stampRender),
                                  settings.stampFg[0], settings.stampFg[1], settings.stampFg[2], settings.stampBg[0],
                                  settings.stampBg[1], settings.stampBg[2], settings.stamp ? (float)frameId : 0.0f };
      for (float f : vals)
      {
         uint32_t bits;
         std::memcpy(&bits, &f, sizeof(bits));
         fin = fin * 1099511628211ull ^ bits;
      }
      fin = fin * 1099511628211ull ^ std::hash<std::string>()(stampNote);
      rev += fin;
   }
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
// A camera's backdrop, decoded once per path; a path that fails stays empty until it changes.
const Scene3DNode::Picture& Scene3DNode::LoadPicture(const std::string& path)
{
   auto found = mPictures.find(path);
   if (found != mPictures.end())
      return found->second;
   Picture& pic = mPictures[path];
   std::vector<unsigned char> pixels;
   std::string error;
   if (!Platform::LoadImageRGBA(path, pixels, pic.w, pic.h, error) || pic.w <= 0 || pic.h <= 0)
      return pic;
   glGenTextures(1, &pic.tex);
   glBindTexture(GL_TEXTURE_2D, pic.tex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pic.w, pic.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);
   return pic;
}

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
         "uniform sampler2D uPicture;\n"
         "uniform int uPictureOn;\n"     // 0 none, 1 behind, 2 over
         "uniform int uPictureWire;\n"
         "uniform vec4 uFrame;\n"
         "uniform vec4 uPictureHalf;\n"  // half size in frame widths, then 1/frame aspect, alpha
         "uniform vec4 uPictureMove;\n"  // offset, rotation in radians, scale
         "uniform vec2 uPictureFlip;\n"
         "uniform vec4 uGridOpt;\n"      // scale, subdivisions, range factor, 0
         "uniform vec3 uAxisOn;\n"       // a line on each world axis
         "uniform vec4 uOutline;\n"      // colour, on
         "uniform vec3 uNearFar;\n"      // near, far, 1 when orthographic
         "float linDepth(float d) {\n"
         "   if (uNearFar.z > 0.5) return d;\n"
         "   return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - (2.0 * d - 1.0) * (uNearFar.y - uNearFar.x));\n"
         "}\n"
         "vec4 picture() {\n"
         "   vec2 c = (vUv - uFrame.xy) / max(uFrame.zw - uFrame.xy, vec2(1e-6)) - 0.5;\n"
         "   vec2 q = vec2(c.x, c.y * uPictureHalf.z) - uPictureMove.xy;\n"
         "   float cr = cos(uPictureMove.z), sr = sin(uPictureMove.z);\n"
         "   q = vec2(cr * q.x + sr * q.y, -sr * q.x + cr * q.y) / max(uPictureMove.w, 1e-4);\n"
         "   vec2 uv = q / (2.0 * uPictureHalf.xy) + 0.5;\n"
         "   if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);\n"
         "   uv = mix(uv, 1.0 - uv, uPictureFlip);\n"
         "   vec4 p = texture(uPicture, vec2(uv.x, 1.0 - uv.y));\n"
         "   return vec4(p.rgb, p.a * uPictureHalf.w);\n"
         "}\n"
         "const vec3 kAxis[3] = vec3[3](vec3(1.0, 0.2, 0.32), vec3(0.157, 0.565, 1.0), vec3(0.545, 0.863, 0.0));\n"
         "float lines(vec2 uv, float s) {\n"
         "   vec2 c = uv / s;\n"
         "   vec2 g = abs(fract(c - 0.5) - 0.5) / max(fwidth(c), vec2(1e-5));\n"
         "   return 1.0 - min(min(g.x, g.y), 1.0);\n"
         "}\n"
         "void main() {\n"
         "   vec4 s = texture(uScene, vUv);\n"
         "   vec3 rgb = s.rgb;\n"
         "   float a = s.a;\n"
         "   if (uPictureOn == 1) {\n"
         "      vec4 p = picture();\n"
         "      float open = uPictureWire == 1 ? 1.0 - s.a : step(0.999999, texture(uDepth, vUv).r);\n"
         "      float k = p.a * open;\n"
         "      rgb = mix(rgb, p.rgb, k);\n"
         "      a = a + k * (1.0 - a);\n"
         "   }\n"
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
         "      float range = (uDist * 14.0 + 12.0) * uGridOpt.z;\n"
         "      float fade = (1.0 - smoothstep(range * 0.35, range, length(p - uEye))) * smoothstep(0.0, 0.06, graze);\n"
         "      if (t > 0.0 && t <= 1.0 && pd <= texture(uDepth, vUv).r + 1e-6 && fade > 0.0) {\n"
         "         float sub = max(uGridOpt.y, 2.0);\n"
         "         float level = log(max(uDist, 1e-3) * 0.25 / uGridOpt.x) / log(sub);\n"
         "         float scale = pow(sub, floor(level)) * uGridOpt.x;\n"
         "         float step01 = fract(level);\n"
         "         float grid = max(lines(uv, scale) * 0.4 * (1.0 - step01), lines(uv, scale * sub) * 0.75);\n"
         "         vec3 col = vec3(0.329);\n"
         "         float ax = 1.0 - min(abs(uv.x) / max(fwidth(uv.x), 1e-5), 1.0);\n"
         "         float bx = 1.0 - min(abs(uv.y) / max(fwidth(uv.y), 1e-5), 1.0);\n"
         "         if (ax > 0.0 && uAxisOn[ub] > 0.5) { col = kAxis[ub]; grid = max(grid, ax); }\n"
         "         if (bx > 0.0 && uAxisOn[ua] > 0.5) { col = kAxis[ua]; grid = max(grid, bx); }\n"
         "         float g = grid * fade;\n"
         "         rgb = mix(rgb, col, g);\n"
         "         a = a + g * (1.0 - a);\n"
         "      }\n"
         "   }\n"
         "   if (uOutline.w > 0.5) {\n"
         "      vec2 px = 1.0 / vec2(textureSize(uDepth, 0));\n"
         "      float d0 = texture(uDepth, vUv).r;\n"
         "      if (d0 < 0.999999) {\n"
         "         float l0 = linDepth(d0);\n"
         "         float edge = 0.0;\n"
         "         for (int i = 0; i < 4; i++) {\n"
         "            vec2 o = vec2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0) * px;\n"
         "            float ln = linDepth(texture(uDepth, vUv + o).r);\n"
         "            float jump = uNearFar.z > 0.5 ? 0.0003 : 0.08 * l0;\n"
         "            edge = max(edge, step(jump, ln - l0));\n"
         "         }\n"
         "         rgb = mix(rgb, uOutline.rgb, edge * 0.9);\n"
         "         a = max(a, edge * 0.9);\n"
         "      }\n"
         "   }\n"
         "   if (uPictureOn == 2) {\n"
         "      vec4 p = picture();\n"
         "      rgb = mix(rgb, p.rgb, p.a);\n"
         "      a = a + p.a * (1.0 - a);\n"
         "   }\n"
         "   fragColor = a > 1e-4 ? vec4(rgb / a, a) : vec4(0.0);\n"
         "}\n");
   if (mGridProgram == 0 || !GLUtil::EnsureFbo(mGridFbo, w, h))
      return sceneTex;
   const Mat4 viewProj = Mat4::Multiply(v.proj, v.view);
   const Mat4 inv = Mat4::Inverse(viewProj);
   const Item* camera = v.camera != 0 ? FindConst(v.camera) : nullptr;
   const Backdrop* back = camera != nullptr && camera->backdrop.show && !camera->backdrop.path.empty() ? &camera->backdrop : nullptr;
   const Picture& pic = back != nullptr ? LoadPicture(back->path) : Picture();
   const bool wire = v.shading == Shading::Wire;
   GLUtil::RunShaderPass(mGridFbo, mGridProgram, [&]
   {
      int on = 0;
      if (pic.tex != 0)
      {
         // The frame's shape against the picture's picks how it covers: stretch fills,
         // fit shows all of it, crop fills and cuts the overhang.
         const float frameW = (v.frame[2] - v.frame[0]) * w, frameH = (v.frame[3] - v.frame[1]) * h;
         const float fa = frameH > 0 ? frameW / frameH : 1.0f;
         const float ia = (float)pic.w / (float)std::max(1, pic.h);
         float hx = 0.5f, hy = 0.5f / fa;
         if (back->fit == 1)
         {
            if (ia > fa) hy = 0.5f / ia;
            else hx = 0.5f * ia / fa;
         }
         else if (back->fit == 2)
         {
            if (ia > fa) hx = 0.5f * ia / fa;
            else hy = 0.5f / ia;
         }
         on = back->front ? 2 : 1;
         glActiveTexture(GL_TEXTURE2);
         glBindTexture(GL_TEXTURE_2D, pic.tex);
         glUniform1i(glGetUniformLocation(mGridProgram, "uPicture"), 2);
         glUniform4fv(glGetUniformLocation(mGridProgram, "uFrame"), 1, v.frame);
         glUniform4f(glGetUniformLocation(mGridProgram, "uPictureHalf"), hx, hy, 1.0f / fa, std::clamp(back->alpha, 0.0f, 1.0f));
         glUniform4f(glGetUniformLocation(mGridProgram, "uPictureMove"), back->offset[0], back->offset[1],
                     back->rotation * 3.14159265f / 180.0f, back->scale);
         glUniform2f(glGetUniformLocation(mGridProgram, "uPictureFlip"), back->flipX ? 1.0f : 0.0f, back->flipY ? 1.0f : 0.0f);
      }
      glUniform1i(glGetUniformLocation(mGridProgram, "uPictureOn"), on);
      glUniform1i(glGetUniformLocation(mGridProgram, "uPictureWire"), wire || !mEditor->DepthValid() ? 1 : 0);
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
      glUniform4f(glGetUniformLocation(mGridProgram, "uGridOpt"), std::max(0.001f, settings.gridScale), (float)std::clamp(settings.gridSubdiv, 2, 100),
                  std::clamp(settings.gridLines, 1, 200) / 10.0f, 0.0f);
      // World axes: X, then the vertical one (shown as Z), then depth (shown as Y).
      glUniform3f(glGetUniformLocation(mGridProgram, "uAxisOn"), settings.axisX ? 1.0f : 0.0f, settings.axisZ ? 1.0f : 0.0f, settings.axisY ? 1.0f : 0.0f);
      const bool ortho = v.proj.m[15] != 0.0f;
      const float pn = ortho ? 0.0f : v.proj.m[14] / (v.proj.m[10] - 1.0f), pf = ortho ? 1.0f : v.proj.m[14] / (v.proj.m[10] + 1.0f);
      glUniform3f(glGetUniformLocation(mGridProgram, "uNearFar"), pn, pf, ortho ? 1.0f : 0.0f);
      const bool outline = settings.outline && v.shading == Shading::Solid && mEditor->DepthValid();
      glUniform4f(glGetUniformLocation(mGridProgram, "uOutline"), settings.outlineColor[0], settings.outlineColor[1], settings.outlineColor[2], outline ? 1.0f : 0.0f);
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
   Root* root = mRoot.get();
   if (!v.only.empty())
   {
      root = mLocalRoot.get();
      root->draws.clear();
      for (int id : v.only)
      {
         auto found = mPlaced.find(id);
         if (found != mPlaced.end())
            root->draws.push_back(found->second.get());
      }
   }
   e.geometry[0] = root;
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
                               worldColorOn ? 1.0f : 0.0f, worldStrength, (float)look.renderPass,
                               settings.transparent ? 1.0f : 0.0f });
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
   e.renderPass = final ? mRender.renderPass : 0;
   ApplyWorld(e, !wire, !settings.transparent);
   e.CookIfNeeded(++mEditorFrame);

   unsigned int tex = e.GetOutputTexture();
   if (!wire && !settings.transparent && e.renderPass == 0 && mWorldNode != nullptr &&
       dynamic_cast<EnvironmentNode*>(mWorldNode) == nullptr && mWorldNode->GetOutputTexture() != 0)
      tex = Composite(tex, e.GetOutputWidth(), e.GetOutputHeight(), mWorldNode->GetOutputTexture(), mEditorFbo);
   return tex != 0 ? GridPass(tex, e.GetOutputWidth(), e.GetOutputHeight(), v) : 0;
}

bool Scene3DNode::ItemBounds(int id, float lo[3], float hi[3])
{
   auto found = mPlaced.find(id);
   if (found == mPlaced.end())
      return false;
   Render3DNode& measure = mEditor ? *mEditor : mRender;
   bool any = false;
   std::function<void(Placed&)> walk = [&](Placed& p)
   {
      float a[3], b[3];
      if (measure.SourceBounds(&p, a, b))
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
