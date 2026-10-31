// The 3D View: a final-stage composer over node outputs. Everything here edits
// the Scene3DNode's placements; the nodes themselves are only ever read, so a
// source keeps animating under whatever arrangement is made on top of it.
#include "app/AppCommon.h"
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/CheckBox.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/SectionCard.h"
#include "nodes/EnvironmentNode.h"
#include "nodes/OutputNode.h"
#include "nodes/Scene3DNode.h"
#include "nodes/SceneNodes.h"
#include "Transport.h"

#include <algorithm>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace app
{
using I18n::L;
using I18n::T;

bool gView3DOpen = false;
float gView3DHeight = 420.0f;
int gView3DDock = 0;
bool gView3DClaimedKeys = false;
ImVec2 gView3DRectMin, gView3DRectMax;

namespace
{
   constexpr float kMinHeight = 300.0f;
   constexpr float kSideW = 340.0f;
   constexpr float kPi = 3.14159265f;
   constexpr float kFov = 45.0f;
   constexpr float kIconW = 30.0f;
   constexpr float kGizmoPx = 84.0f;

   // Overlay colours are Blender's factory theme, not the app's: a 3D viewport is
   // read against the picture, and every one of these has one job there.
   constexpr unsigned kBackdrop = 0x303030, kWire = 0xC8C8C8, kSelected = 0xED5700, kActive = 0xFFA028, kHotHandle = 0xFFFFFF;
   constexpr unsigned kAxisHex[3] = { 0xFF3352, 0x8BDC00, 0x2890FF };

   using Item = Scene3DNode::Item;
   using Kind = Scene3DNode::Kind;
   using Shading = Scene3DNode::Shading;

   Shading sShading = Shading::Solid;
   bool sCameraView = false;
   bool sGrid = true;
   bool sOrtho = false;
   bool sAutoOrtho = false;        // an axis view went orthographic; orbiting gives it back
   bool sSideOpen = true;
   int sViewName = -1;             // 0..5 while looking straight down an axis
   int sGridAxis = 1;
   constexpr int kSceneTab = 6;
   int sTab = kSceneTab;            // the settings tab
   int sLensUnit = 0;               // 0 millimeters, 1 field of view
   bool sLockCam = false;           // navigating in camera view moves the camera
   bool sLockDirty = false;
   bool sLockUndo = false;
   bool sToolKey = false;           // Shift+Space was pressed; the next key picks the tool
   int sPadDrag = 0;                // 1 zoom, 2 pan, by dragging the toolpad
   int sTool = 1;                  // 0 select, 1 move, 2 rotate, 3 scale
   int sHot = -1;                  // last frame's handle: 0-2 axes, 3 centre, 4-6 planes
   std::vector<int> sSel;
   std::string sCopiedScene;
   std::vector<int> sLocal;        // local view: only these, empty when off
   bool sOverlays = true;
   bool sZoomArmed = false, sBoxZoom = false;   // Shift+B, then a box to zoom into
   int sReduceTo = 0;              // a click inside a selection narrows it on release, not a drag
   float sCamZoom = 1.0f;          // how close the camera frame is drawn
   ImVec2 sCamPan(0, 0);
   int sRename = 0;
   bool sRenameFocus = false;
   char sRenameBuf[96] = "";
   char sSearch[64] = "";
   uint64_t sSceneUid = 0;         // the 3D View node last selected on the canvas

   // What the scene remembers. Points at the shown scene's own settings while the panel draws.
   Scene3DNode::Settings sNoScene;
   Scene3DNode::Settings* sSet = &sNoScene;

   float sTarget[3] = { 0, 0, 0 };
   float sDist = 6.0f, sAz = 0.7f, sEl = 0.4f;

   bool sBoxOn = false, sTweak = false, sRightOk = false, sNavDrag = false, sOpenAdd = false, sOpenMenu = false, sOpenPivot = false, sOpenOrient = false;
   ImVec2 sPressAt;

   // Spawning moves gNodes, so anything that makes a node waits until the frame's
   // drawing has let go of it.
   struct Pending
   {
      enum What { None, Camera, Light, Hdri, PickHdri, Node, Scene, DuplicateScene, PasteScene, DeleteScene, ConvertScene } what = None;
      int lightType = 1;
      uint64_t uid = 0;
   } sPending;

   struct Cam
   {
      Mat4 view, proj, viewProj;
      float eye[3] = { 0, 0, 0 };
      float right[3], up[3], fwd[3];
      ImVec2 mn, mx;
      ImVec2 frameMn, frameMx;     // the render's shape, through a camera
      bool ortho = false;
   } sCam;

   struct Op
   {
      char mode = 0;               // 'g', 'r', 's'
      int axis = -1;
      int plane = -1;              // moves in the plane facing this axis
      int orient = 0;              // 0 global, 1 local, 2 view
      bool gizmo = false;
      ImVec2 start;
      float pivot[3] = { 0, 0, 0 };
      std::vector<Item> before;
      std::vector<float> centers;  // three per item, for individual origins
      std::string typed;
   } sOp;

   ImU32 Hex(unsigned rgb, float a = 1.0f)
   {
      return ImGui::ColorConvertFloat4ToU32(ImVec4(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a));
   }

   ImU32 AxisColor(int a, float alpha = 1.0f) { return Hex(kAxisHex[a], alpha); }

   float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

   void Normalize(float v[3])
   {
      const float len = std::max(1e-6f, std::sqrt(Dot(v, v)));
      for (int k = 0; k < 3; k++)
         v[k] /= len;
   }

   void Cross(const float a[3], const float b[3], float out[3])
   {
      out[0] = a[1] * b[2] - a[2] * b[1];
      out[1] = a[2] * b[0] - a[0] * b[2];
      out[2] = a[0] * b[1] - a[1] * b[0];
   }

   GraphNode* SceneGraphNode()
   {
      if (GraphNode* chosen = sSceneUid != 0 ? FindNodeByUid(sSceneUid) : nullptr)
         if (dynamic_cast<Scene3DNode*>(chosen->node.get()) != nullptr)
            return chosen;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<Scene3DNode*>(gn.node.get()) != nullptr)
            return &gn;
      return nullptr;
   }

   Scene3DNode* FindScene()
   {
      GraphNode* gn = SceneGraphNode();
      return gn != nullptr ? dynamic_cast<Scene3DNode*>(gn->node.get()) : nullptr;
   }

   // With two 3D View nodes, the panel shows the one last selected on the canvas.
   // "Assign Node...": the next node clicked on the canvas becomes this item's source.
   int sPickItem = -1;
   uint64_t sPickedUid = 0;

   void FollowCanvasSelection()
   {
      if (gEditor == nullptr)
         return;
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      // Only a new selection moves it, so the panel's own scene picker holds while a node stays selected.
      static uint64_t sFollowed = 0;
      static uint64_t sLastNode = 0;
      uint64_t selected = 0, anyNode = 0;
      for (GraphNode& gn : gNodes)
         if (ed::IsNodeSelected(gn.NodeId()))
         {
            anyNode = gn.uid;
            break;
         }
      if (anyNode != 0 && anyNode != sLastNode && sPickItem >= 0)
         sPickedUid = anyNode;
      sLastNode = anyNode;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<Scene3DNode*>(gn.node.get()) != nullptr && ed::IsNodeSelected(gn.NodeId()))
         {
            selected = gn.uid;
            break;
         }
      if (selected != 0 && selected != sFollowed)
         sSceneUid = selected;
      sFollowed = selected;
      ed::SetCurrentEditor(prev);
   }

   // Panels draw outside the canvas, where no node editor is current.
   ImVec2 NodePos(const GraphNode& gn)
   {
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      // A node off the canvas has never been placed, only given a spot.
      const ImVec2 at = gn.hiddenFromCanvas ? ImVec2(gn.spawnX, gn.spawnY) : ed::GetNodePosition(gn.NodeId());
      ed::SetCurrentEditor(prev);
      return at;
   }

   bool Selected(int id) { return std::find(sSel.begin(), sSel.end(), id) != sSel.end(); }

   bool InLocal(int id) { return sLocal.empty() || std::find(sLocal.begin(), sLocal.end(), id) != sLocal.end(); }

   bool Shown(const Item& it) { return it.visible && InLocal(it.id); }

   std::string ItemName(const Item& it)
   {
      if (!it.name.empty())
         return it.name;
      GraphNode* gn = FindNodeByUid(it.uid);
      return gn != nullptr ? gn->typeName + " " + std::to_string(gn->index) : std::string(T("source missing"));
   }

   void EyeFromOrbit(float eye[3])
   {
      const float ce = std::cos(sEl);
      eye[0] = sTarget[0] + sDist * ce * std::cos(sAz);
      eye[1] = sTarget[1] + sDist * std::sin(sEl);
      eye[2] = sTarget[2] + sDist * ce * std::sin(sAz);
   }

   Mat4 OrbitView()
   {
      float eye[3];
      EyeFromOrbit(eye);
      const float up[3] = { 0, 1, 0 };
      return Mat4::LookAt(eye, sTarget, up);
   }

   float OrthoHalfHeight() { return sDist * std::tan(kFov * 0.5f * kPi / 180.0f); }

   void BuildCam(Scene3DNode* scene, ImVec2 mn, ImVec2 mx)
   {
      sCam.mn = mn;
      sCam.mx = mx;
      sCam.frameMn = mn;
      sCam.frameMx = mx;
      const float w = mx.x - mn.x, h = std::max(1.0f, mx.y - mn.y);
      const float aspect = w / h;
      CameraNode* lens = nullptr;
      sCam.ortho = false;
      if (sCameraView && scene->CameraView(sCam.view, sCam.eye, &lens))
      {
         // The lens sees the render's shape; the rest of the panel looks past its edge.
         const float renderAspect = (float)std::max(1, scene->GetOutputWidth()) / (float)std::max(1, scene->GetOutputHeight());
         float fw = w * 0.92f, fh = fw / renderAspect;
         if (fh > h * 0.92f)
         {
            fh = h * 0.92f;
            fw = fh * renderAspect;
         }
         fw *= sCamZoom;
         fh *= sCamZoom;
         sCam.frameMn = ImVec2(mn.x + (w - fw) * 0.5f + sCamPan.x, mn.y + (h - fh) * 0.5f + sCamPan.y);
         sCam.frameMx = ImVec2(sCam.frameMn.x + fw, sCam.frameMn.y + fh);
         sCam.proj = lens->ProjectionMatrix(renderAspect);
         sCam.proj.m[0] *= fw / w;
         sCam.proj.m[5] *= fh / h;
         sCam.proj.m[8] *= fw / w;
         sCam.proj.m[9] *= fh / h;
         sCam.proj.m[12] *= fw / w;
         sCam.proj.m[13] *= fh / h;
         for (int i = 0; i < 4; i++)
         {
            sCam.proj.m[i * 4 + 0] += 2.0f * sCamPan.x / w * sCam.proj.m[i * 4 + 3];
            sCam.proj.m[i * 4 + 1] -= 2.0f * sCamPan.y / h * sCam.proj.m[i * 4 + 3];
         }
         sCam.ortho = lens->projection != 0;
      }
      else
      {
         sCameraView = false;
         EyeFromOrbit(sCam.eye);
         sCam.view = OrbitView();
         sCam.ortho = sOrtho;
         sCam.proj = sOrtho ? Mat4::Orthographic(OrthoHalfHeight(), aspect, -2000.0f, 2000.0f)
                            : Mat4::Perspective(kFov * kPi / 180.0f, aspect, 0.05f, 2000.0f);
      }
      sCam.viewProj = Mat4::Multiply(sCam.proj, sCam.view);
      for (int k = 0; k < 3; k++)
      {
         sCam.right[k] = sCam.view.m[k * 4];
         sCam.up[k] = sCam.view.m[k * 4 + 1];
         sCam.fwd[k] = -sCam.view.m[k * 4 + 2];
      }
      Normalize(sCam.right);
      Normalize(sCam.up);
      Normalize(sCam.fwd);
   }

   // Leaving a camera keeps the view where it is rather than jumping back.
   void LeaveCamera(bool force = false)
   {
      if (!sCameraView)
         return;
      const bool locked = sLockCam && !force;
      sLockDirty = locked;
      sCameraView = locked;
      sOrtho = false;
      sEl = std::asin(std::clamp(-sCam.fwd[1], -1.0f, 1.0f));
      sAz = std::atan2(-sCam.fwd[2], -sCam.fwd[0]);
      for (int k = 0; k < 3; k++)
         sTarget[k] = sCam.eye[k] + sCam.fwd[k] * sDist;
   }

   // Any free rotation ends an axis view.
   void Orbited()
   {
      LeaveCamera();
      sViewName = -1;
      sGridAxis = 1;
      if (sAutoOrtho)
         sOrtho = false;
      sAutoOrtho = false;
   }

   // 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z: which side the eye stands on.
   void ViewAxis(int which)
   {
      LeaveCamera();
      const float top = 1.5697f;
      const float az[6] = { 0.0f, kPi, kPi * 0.5f, kPi * 0.5f, kPi * 0.5f, -kPi * 0.5f };
      const float el[6] = { 0.0f, 0.0f, top, -top, 0.0f, 0.0f };
      sAz = az[which];
      sEl = el[which];
      sViewName = which;
      sGridAxis = which / 2;
      sAutoOrtho = sAutoOrtho || !sOrtho;
      sOrtho = true;
   }

   bool Project(const float p[3], ImVec2& out)
   {
      const float* m = sCam.viewProj.m;
      const float x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
      const float y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
      const float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
      if (w < 0.01f)
         return false;
      out.x = sCam.mn.x + (x / w * 0.5f + 0.5f) * (sCam.mx.x - sCam.mn.x);
      out.y = sCam.mn.y + (0.5f - y / w * 0.5f) * (sCam.mx.y - sCam.mn.y);
      return true;
   }

   // World units per screen pixel at a point.
   float UnitAt(const float p[3])
   {
      const float h = std::max(1.0f, sCam.mx.y - sCam.mn.y);
      if (sCam.ortho)
         return 2.0f / std::max(1e-5f, sCam.proj.m[5]) / h;
      const float depth = std::max(0.05f, (p[0] - sCam.eye[0]) * sCam.fwd[0] + (p[1] - sCam.eye[1]) * sCam.fwd[1] +
                                             (p[2] - sCam.eye[2]) * sCam.fwd[2]);
      return 2.0f * depth / std::max(1e-3f, sCam.proj.m[5]) / h;
   }

   // The mouse as a world ray.
   void MouseRay(ImVec2 mouse, float o[3], float d[3])
   {
      const float nx = (mouse.x - sCam.mn.x) / std::max(1.0f, sCam.mx.x - sCam.mn.x) * 2.0f - 1.0f;
      const float ny = 1.0f - (mouse.y - sCam.mn.y) / std::max(1.0f, sCam.mx.y - sCam.mn.y) * 2.0f;
      const Mat4 inv = Mat4::Inverse(sCam.viewProj);
      const float* m = inv.m;
      const float w = m[3] * nx + m[7] * ny - m[11] + m[15];
      for (int k = 0; k < 3; k++)
         o[k] = (m[k] * nx + m[4 + k] * ny - m[8 + k] + m[12 + k]) / w;
      for (int k = 0; k < 3; k++)
         d[k] = sCam.ortho ? sCam.fwd[k] : o[k] - sCam.eye[k];
      Normalize(d);
   }

   // Where on the line through p along a the ray passes closest.
   float LineParam(const float p[3], const float a[3], const float o[3], const float d[3])
   {
      const float w[3] = { p[0] - o[0], p[1] - o[1], p[2] - o[2] };
      const float b = Dot(a, d), denom = 1.0f - b * b;
      if (denom < 1e-4f)
         return 0.0f;
      return std::clamp((b * Dot(d, w) - Dot(a, w)) / denom, -sDist * 100.0f, sDist * 100.0f);
   }

   bool PlaneHit(const float p[3], const float n[3], const float o[3], const float d[3], float out[3])
   {
      const float denom = Dot(d, n);
      if (std::abs(denom) < 1e-3f)
         return false;
      const float w[3] = { p[0] - o[0], p[1] - o[1], p[2] - o[2] };
      const float t = Dot(w, n) / denom;
      for (int k = 0; k < 3; k++)
         out[k] = o[k] + d[k] * t;
      return true;
   }

   // Through a camera these move the frame; a locked camera moves with the view instead.
   void Pan(float dx, float dy)
   {
      if (sCameraView && !sLockCam)
      {
         sCamPan.x += dx;
         sCamPan.y += dy;
         return;
      }
      LeaveCamera();
      const float u = UnitAt(sTarget);
      for (int k = 0; k < 3; k++)
         sTarget[k] += -sCam.right[k] * dx * u + sCam.up[k] * dy * u;
   }

   void Zoom(float ratio, const ImVec2* mouse = nullptr)
   {
      if (sCameraView && !sLockCam)
      {
         sCamZoom = std::clamp(sCamZoom / ratio, 0.1f, 12.0f);
         return;
      }
      LeaveCamera();
      const float was = sDist;
      sDist = std::clamp(sDist * ratio, 0.1f, 500.0f);
      float o[3], d[3], h[3];
      if (mouse == nullptr || !sSet->zoomToMouse)
         return;
      MouseRay(*mouse, o, d);
      if (PlaneHit(sTarget, sCam.fwd, o, d, h))
         for (int k = 0; k < 3; k++)
            sTarget[k] = h[k] + (sTarget[k] - h[k]) * (sDist / was);
   }

   // The middle of the selection.
   bool SelectionCenter(Scene3DNode* scene, float out[3])
   {
      out[0] = out[1] = out[2] = 0.0f;
      int count = 0;
      for (int id : sSel)
         if (const Item* it = scene->FindItem(id))
         {
            float c[3];
            scene->ItemCenter(*it, c);
            for (int k = 0; k < 3; k++)
               out[k] += c[k];
            count++;
         }
      for (int k = 0; k < 3 && count > 0; k++)
         out[k] /= (float)count;
      return count > 0;
   }

   // Where the gizmo stands and what rotates and scales around.
   void PivotPoint(Scene3DNode* scene, float out[3])
   {
      SelectionCenter(scene, out);
      if (sSet->pivot == 3)
         std::copy(sSet->cursor, sSet->cursor + 3, out);
      else if (sSet->pivot == 2 && !sSel.empty())
         if (const Item* it = scene->FindItem(sSel.back()))
            scene->ItemCenter(*it, out);
   }

   // A turn of the view. With a selection the view swings round it, so it stays where it was on screen.
   void OrbitBy(Scene3DNode* scene, float dAz, float dEl)
   {
      Orbited();
      float p[3];
      const bool around = sSet->orbitSelection && SelectionCenter(scene, p);
      float eye[3], local[3] = { 0, 0, 0 };
      Mat4 v = OrbitView();
      if (around)
      {
         EyeFromOrbit(eye);
         for (int i = 0; i < 3; i++)
            for (int k = 0; k < 3; k++)
               local[i] += (p[k] - eye[k]) * v.m[k * 4 + i];
      }
      sAz += dAz;
      sEl = std::clamp(sEl + dEl, -1.5697f, 1.5697f);
      if (!around)
         return;
      v = OrbitView();
      float next[3], at[3];
      EyeFromOrbit(next);
      for (int k = 0; k < 3; k++)
         at[k] = p[k] - (local[0] * v.m[k * 4] + local[1] * v.m[k * 4 + 1] + local[2] * v.m[k * 4 + 2]);
      for (int k = 0; k < 3; k++)
         sTarget[k] += at[k] - next[k];
   }

   // The rotation part of an item, as rows.
   void RotationOf(const float rotDeg[3], float r[3][3])
   {
      Item t;
      t.rot[0] = rotDeg[0]; t.rot[1] = rotDeg[1]; t.rot[2] = rotDeg[2];
      const Mat4 m = Scene3DNode::Placement(t);
      for (int row = 0; row < 3; row++)
         for (int col = 0; col < 3; col++)
            r[row][col] = m.m[col * 4 + row];
   }

   void EulerFrom(const float r[3][3], float rotDeg[3])
   {
      const float d = 180.0f / kPi;
      rotDeg[1] = std::asin(std::clamp(-r[2][0], -1.0f, 1.0f)) * d;
      rotDeg[0] = std::atan2(r[2][1], r[2][2]) * d;
      rotDeg[2] = std::atan2(r[1][0], r[0][0]) * d;
   }

   void AxisAngle(const float a[3], float angle, float r[3][3])
   {
      const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
      r[0][0] = t * a[0] * a[0] + c;      r[0][1] = t * a[0] * a[1] - s * a[2]; r[0][2] = t * a[0] * a[2] + s * a[1];
      r[1][0] = t * a[0] * a[1] + s * a[2]; r[1][1] = t * a[1] * a[1] + c;      r[1][2] = t * a[1] * a[2] - s * a[0];
      r[2][0] = t * a[0] * a[2] - s * a[1]; r[2][1] = t * a[1] * a[2] + s * a[0]; r[2][2] = t * a[2] * a[2] + c;
   }

   void Mul(const float a[3][3], const float b[3][3], float out[3][3])
   {
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++)
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
   }

   // Axis a of an orientation: the world, the item's own, or the view's.
   void OrientAxis(const Item& item, int orient, int a, float out[3])
   {
      out[0] = out[1] = out[2] = 0.0f;
      if (orient == 1)
      {
         float r[3][3];
         RotationOf(item.rot, r);
         for (int k = 0; k < 3; k++)
            out[k] = r[k][a];
      }
      else if (orient == 2)
         for (int k = 0; k < 3; k++)
            out[k] = a == 0 ? sCam.right[k] : a == 1 ? sCam.up[k] : -sCam.fwd[k];
      else
         out[a] = 1.0f;
   }

   void OpAxis(const Item& first, float out[3])
   {
      out[0] = out[1] = out[2] = 0.0f;
      if (sOp.axis >= 0)
         OrientAxis(first, sOp.orient, sOp.axis, out);
   }

   // First press global, second the scene's orientation, third lets go.
   void LockAxis(int a)
   {
      const bool same = sOp.axis == a && sOp.plane < 0;
      sOp.plane = -1;
      if (sOp.mode == 's')
      {
         sOp.axis = same ? -1 : a;
         return;
      }
      if (!same)
      {
         sOp.axis = a;
         sOp.orient = 0;
      }
      else if (sOp.orient == 0)
         sOp.orient = sSet->orientation != 0 ? sSet->orientation : 1;
      else
         sOp.axis = -1;
   }

   float SnapTo(float v, float step) { return step > 0.0f ? std::round(v / step) * step : v; }

   void BeginOp(char mode, Scene3DNode* scene, ImVec2 mouse, int axis = -1, bool gizmo = false, int plane = -1)
   {
      sOp = Op();
      sOp.mode = mode;
      sOp.axis = axis;
      sOp.plane = plane;
      sOp.gizmo = gizmo;
      sOp.start = mouse;
      sOp.orient = mode == 's' ? 1 : gizmo ? sSet->orientation : 0;
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
         {
            sOp.before.push_back(*it);
            float c[3];
            scene->ItemCenter(*it, c);
            sOp.centers.insert(sOp.centers.end(), c, c + 3);
         }
      if (sOp.before.empty())
      {
         sOp.mode = 0;
         return;
      }
      PivotPoint(scene, sOp.pivot);
   }

   void ApplyOp(Scene3DNode* scene, ImVec2 mouse)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const bool snap = sSet->snap != (io.KeyCtrl || io.KeySuper);   // Cmd or Ctrl on macOS, Ctrl elsewhere
      const float fine = io.KeyShift && sOp.plane < 0 ? 0.1f : 1.0f;
      const bool hasTyped = !sOp.typed.empty() && sOp.typed != "-";
      const float typed = hasTyped ? std::strtof(sOp.typed.c_str(), nullptr) : 0.0f;
      const ImVec2 d((mouse.x - sOp.start.x) * fine, (mouse.y - sOp.start.y) * fine);
      float axis[3];
      const Item& first = sOp.before.back();
      OpAxis(first, axis);
      ImVec2 c(0, 0);
      Project(sOp.pivot, c);

      float move[3] = { 0, 0, 0 };
      bool moved[3] = { true, true, true };   // the components the gesture owns, for snapping
      bool snapStep = snap && !hasTyped;
      float angle = 0.0f;
      float factor = 1.0f;
      if (sOp.mode == 'g')
      {
         float o0[3], d0[3], o1[3], d1[3];
         MouseRay(sOp.start, o0, d0);
         MouseRay(mouse, o1, d1);
         if (sOp.axis >= 0)
         {
            float amount = hasTyped ? typed : (LineParam(sOp.pivot, axis, o1, d1) - LineParam(sOp.pivot, axis, o0, d0)) * fine;
            if (sOp.orient != 0 && snapStep)
            {
               amount = SnapTo(amount, sSet->snapMove);
               snapStep = false;
            }
            for (int k = 0; k < 3; k++)
            {
               move[k] = axis[k] * amount;
               moved[k] = axis[k] != 0.0f;
            }
         }
         else
         {
            float n[3] = { sCam.fwd[0], sCam.fwd[1], sCam.fwd[2] };
            if (sOp.plane >= 0)
            {
               OrientAxis(first, sOp.orient, sOp.plane, n);
               if (sOp.orient == 0)
                  moved[sOp.plane] = false;
               else
                  snapStep = false;
            }
            float h0[3], h1[3];
            if (hasTyped)
            {
               // A number needs a direction: X, or the plane's first axis.
               float along[3];
               OrientAxis(first, sOp.orient, sOp.plane >= 0 ? (sOp.plane + 1) % 3 : 0, along);
               for (int k = 0; k < 3; k++)
                  move[k] = along[k] * typed;
            }
            else if (PlaneHit(sOp.pivot, n, o0, d0, h0) && PlaneHit(sOp.pivot, n, o1, d1, h1))
               for (int k = 0; k < 3; k++)
                  move[k] = (h1[k] - h0[k]) * fine;
         }
      }
      else if (sOp.mode == 'r')
      {
         const float a0 = std::atan2(sOp.start.y - c.y, sOp.start.x - c.x);
         const float a1 = std::atan2(mouse.y - c.y, mouse.x - c.x);
         float deg = (a1 - a0) * fine * 180.0f / kPi;
         if (sOp.axis < 0)
         {
            for (int k = 0; k < 3; k++)
               axis[k] = -sCam.fwd[k];
            deg = -deg;
         }
         else if (Dot(axis, sCam.fwd) < 0.0f)
            deg = -deg;
         if (hasTyped)
            deg = typed;
         else if (snap)
            deg = SnapTo(deg, sSet->snapRot);
         angle = deg * kPi / 180.0f;
      }
      else
      {
         if (sOp.gizmo && sOp.axis >= 0)
         {
            const float len = UnitAt(sOp.pivot) * kGizmoPx;
            const float tip[3] = { sOp.pivot[0] + axis[0] * len, sOp.pivot[1] + axis[1] * len, sOp.pivot[2] + axis[2] * len };
            ImVec2 t = c;
            Project(tip, t);
            const float sx = t.x - c.x, sy = t.y - c.y;
            factor = 1.0f + (d.x * sx + d.y * sy) / std::max(1.0f, sx * sx + sy * sy);
         }
         else if (sOp.gizmo)
            factor = 1.0f + (d.x - d.y) / 120.0f;
         else
         {
            const float d0 = std::max(4.0f, std::hypot(sOp.start.x - c.x, sOp.start.y - c.y));
            const float d1 = std::hypot(mouse.x - c.x, mouse.y - c.y);
            factor = 1.0f + (d1 / d0 - 1.0f) * fine;
         }
         if (hasTyped)
            factor = typed;
         else if (snap)
            factor = SnapTo(factor, sSet->snapScale);
         if (factor == 0.0f)
            factor = 0.001f;
      }

      for (size_t i = 0; i < sOp.before.size(); i++)
      {
         const Item& was = sOp.before[i];
         Item* it = scene->FindItem(was.id);
         if (it == nullptr)
            continue;
         const float* pivot = sSet->pivot == 1 ? &sOp.centers[i * 3] : sOp.pivot;
         float off[3] = { was.pos[0] - pivot[0], was.pos[1] - pivot[1], was.pos[2] - pivot[2] };
         if (sOp.mode == 'g')
         {
            for (int k = 0; k < 3; k++)
            {
               it->pos[k] = was.pos[k] + move[k];
               if (snapStep && moved[k])
                  it->pos[k] = SnapTo(it->pos[k], sSet->snapMove);
            }
         }
         else if (sOp.mode == 'r')
         {
            float dr[3][3], r0[3][3], r1[3][3];
            AxisAngle(axis, angle, dr);
            RotationOf(was.rot, r0);
            Mul(dr, r0, r1);
            EulerFrom(r1, it->rot);
            for (int k = 0; k < 3; k++)
               it->pos[k] = pivot[k] + dr[k][0] * off[0] + dr[k][1] * off[1] + dr[k][2] * off[2];
         }
         else
         {
            // Always along the item's own axes: a scale can't shear what a rotation put there.
            float r[3][3], local[3];
            RotationOf(was.rot, r);
            for (int k = 0; k < 3; k++)
            {
               const float f = (sOp.axis < 0 && sOp.plane < 0) || sOp.axis == k || (sOp.plane >= 0 && sOp.plane != k) ? factor : 1.0f;
               local[k] = (r[0][k] * off[0] + r[1][k] * off[1] + r[2][k] * off[2]) * f;
               it->scale[k] = was.scale[k] * f;
            }
            for (int k = 0; k < 3; k++)
               it->pos[k] = pivot[k] + r[k][0] * local[0] + r[k][1] * local[1] + r[k][2] * local[2];
         }
      }
      scene->Commit();
   }

   void CancelOp(Scene3DNode* scene)
   {
      for (const Item& was : sOp.before)
         if (Item* it = scene->FindItem(was.id))
            *it = was;
      scene->Commit();
      sOp = Op();
   }

   // One gesture, one undo: the edit is taken back, checkpointed, and laid on again.
   void ConfirmOp(Scene3DNode* scene)
   {
      std::vector<Item> after;
      for (const Item& was : sOp.before)
         if (Item* it = scene->FindItem(was.id))
         {
            after.push_back(*it);
            *it = was;
         }
      scene->Commit();
      PushUndoCheckpoint(sOp.mode == 'g' ? "Move in 3D View" : sOp.mode == 'r' ? "Rotate in 3D View" : "Scale in 3D View");
      for (const Item& now : after)
         if (Item* it = scene->FindItem(now.id))
            *it = now;
      scene->Commit();
      sOp = Op();
   }

   void FrameItems(Scene3DNode* scene, bool all)
   {
      float lo[3], hi[3];
      bool any = false;
      for (const Item& it : scene->Items())
      {
         if (!all && !Selected(it.id))
            continue;
         float a[3], b[3];
         if (!scene->ItemBounds(it.id, a, b))
         {
            if (!scene->ItemPoint(it, a))
               continue;
            std::copy(a, a + 3, b);
         }
         for (int k = 0; k < 3; k++)
         {
            lo[k] = any ? std::min(lo[k], a[k]) : a[k];
            hi[k] = any ? std::max(hi[k], b[k]) : b[k];
         }
         any = true;
      }
      if (!any)
         return;
      LeaveCamera();
      float size = 0.0f;
      for (int k = 0; k < 3; k++)
      {
         sTarget[k] = (lo[k] + hi[k]) * 0.5f;
         size = std::max(size, hi[k] - lo[k]);
      }
      sDist = std::max(0.5f, size * 1.8f);
   }

   void DeleteSelected(Scene3DNode* scene)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Remove from 3D View");
      for (int id : sSel)
         scene->RemoveItem(id);
      sSel.clear();
   }

   int NextGroup(Scene3DNode* scene)
   {
      int next = 1;
      for (const Item& it : scene->Items())
         next = std::max(next, it.group + 1);
      return next;
   }

   void GroupSelected(Scene3DNode* scene, bool group)
   {
      if (sSel.size() < (group ? 2u : 1u))
         return;
      PushUndoCheckpoint(group ? "Group in 3D View" : "Ungroup in 3D View");
      const int number = group ? NextGroup(scene) : 0;
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
            it->group = number;
      scene->Commit();
   }

   // Picking one member picks them all, so move, duplicate and remove need no group code of their own.
   void KeepGroupsTogether(Scene3DNode* scene)
   {
      std::vector<int> groups;
      for (int id : sSel)
         if (const Item* it = scene->FindItem(id))
            if (it->group != 0)
               groups.push_back(it->group);
      for (const Item& it : scene->Items())
         if (it.group != 0 && !Selected(it.id) && std::find(groups.begin(), groups.end(), it.group) != groups.end())
            sSel.insert(sSel.begin(), it.id);
   }

   void DuplicateSelected(Scene3DNode* scene, ImVec2 mouse)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Duplicate in 3D View");
      std::vector<int> made;
      std::map<int, int> groups;
      for (int id : std::vector<int>(sSel))
      {
         Item* it = scene->FindItem(id);
         if (it == nullptr)
            continue;
         Item copy = *it;
         if (copy.group != 0)
         {
            // A copied group is a new group, not more of the old one.
            auto found = groups.find(copy.group);
            if (found == groups.end())
               found = groups.emplace(copy.group, NextGroup(scene) + (int)groups.size()).first;
            copy.group = found->second;
         }
         Item& fresh = scene->AddItem(copy.uid);
         const int freshId = fresh.id;
         fresh = copy;
         fresh.id = freshId;
         made.push_back(freshId);
      }
      scene->Commit();
      sSel = made;
      BeginOp('g', scene, mouse);
   }

   void StartRender(Scene3DNode* scene);
   void RenderStill(Scene3DNode* scene);

   void CursorToSelected(Scene3DNode* scene)
   {
      float c[3];
      if (!SelectionCenter(scene, c))
         return;
      PushUndoCheckpoint("Move 3D cursor");
      std::copy(c, c + 3, sSet->cursor);
   }

   void SelectionToCursor(Scene3DNode* scene)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Move to 3D cursor");
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
         {
            float c[3];
            scene->ItemCenter(*it, c);
            for (int k = 0; k < 3; k++)
               it->pos[k] += sSet->cursor[k] - c[k];
         }
      scene->Commit();
   }

   void ToggleLocal(Scene3DNode* scene)
   {
      if (!sLocal.empty())
         sLocal.clear();
      else if (!sSel.empty())
      {
         sLocal = sSel;
         FrameItems(scene, false);
      }
   }

   void StartRename(const Item& it)
   {
      sSideOpen = true;
      sRename = it.id;
      sRenameFocus = true;
      snprintf(sRenameBuf, sizeof(sRenameBuf), "%s", ItemName(it).c_str());
   }

   void ToggleCamera(Scene3DNode* scene)
   {
      if (scene->activeCamera == 0)
         sPending.what = Pending::Camera;
      else if (sCameraView)
         LeaveCamera(true);
      else
         sCameraView = true;
   }

   void CameraToView(Scene3DNode* scene)
   {
      Item* cam = scene->FindItem(scene->activeCamera);
      if (cam == nullptr)
      {
         sPending.what = Pending::Camera;
         return;
      }
      PushUndoCheckpoint("Move camera to view");
      scene->AlignToView(*cam, OrbitView());
      scene->Commit();
      sCameraView = true;
   }

   void HideSelected(Scene3DNode* scene)
   {
      PushUndoCheckpoint("Hide in 3D View");
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
            it->visible = false;
      scene->Commit();
   }

   void HandleKeys(Scene3DNode* scene, ImVec2 mouse, bool hovered)
   {
      const ImGuiIO& io = ImGui::GetIO();
      auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      auto digit = [&](int i) { return pressed((ImGuiKey)(ImGuiKey_0 + i)) || pressed((ImGuiKey)(ImGuiKey_Keypad0 + i)); };
      if (sOp.mode != 0)
      {
         for (int a = 0; a < 3; a++)
            if (pressed(a == 0 ? ImGuiKey_X : a == 1 ? ImGuiKey_Y : ImGuiKey_Z))
            {
               if (io.KeyShift && sOp.mode != 'r')
               {
                  sOp.plane = a;
                  sOp.axis = -1;
               }
               else
                  LockAxis(a);
            }
         for (int i = 0; i < 10; i++)
            if (digit(i))
               sOp.typed += (char)('0' + i);
         if (pressed(ImGuiKey_Period) || pressed(ImGuiKey_KeypadDecimal))
            sOp.typed += '.';
         if ((pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract)) && sOp.typed.empty())
            sOp.typed = "-";
         if (pressed(ImGuiKey_Backspace) && !sOp.typed.empty())
            sOp.typed.pop_back();
         ApplyOp(scene, mouse);
         if (pressed(ImGuiKey_Escape) || (!sOp.gizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
            CancelOp(scene);
         else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) ||
                  (!sOp.gizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
                  (sOp.gizmo && !ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            ConfirmOp(scene);
         return;
      }
      if (!hovered || io.WantTextInput)
         return;
      if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
         return; // WASD belongs to the fly camera while the button is held
      const bool command = io.KeyCtrl || io.KeySuper;
      if (io.KeyShift && pressed(ImGuiKey_Space))
      {
         sToolKey = true;
         return;
      }
      if (sToolKey)
      {
         if (pressed(ImGuiKey_W)) sTool = 0;
         else if (pressed(ImGuiKey_G)) sTool = 1;
         else if (pressed(ImGuiKey_R)) sTool = 2;
         else if (pressed(ImGuiKey_S)) sTool = 3;
         if (pressed(ImGuiKey_W) || pressed(ImGuiKey_G) || pressed(ImGuiKey_R) || pressed(ImGuiKey_S) || pressed(ImGuiKey_Escape))
            sToolKey = false;
         if (!sToolKey)
            return;
      }
      if (sZoomArmed && pressed(ImGuiKey_Escape))
         sZoomArmed = false;
      if (pressed(ImGuiKey_F12))
      {
         if (command)
            StartRender(scene);
         else
            RenderStill(scene);
      }
      if (!sSel.empty() && io.KeyAlt && !command && (pressed(ImGuiKey_G) || pressed(ImGuiKey_R) || pressed(ImGuiKey_S)))
      {
         PushUndoCheckpoint("Clear transform");
         for (int id : sSel)
            if (Item* it = scene->FindItem(id))
            {
               if (pressed(ImGuiKey_G)) std::fill(it->pos, it->pos + 3, 0.0f);
               if (pressed(ImGuiKey_R)) std::fill(it->rot, it->rot + 3, 0.0f);
               if (pressed(ImGuiKey_S)) std::fill(it->scale, it->scale + 3, 1.0f);
            }
         scene->Commit();
      }
      if (io.KeyShift && !command && pressed(ImGuiKey_Z))
         sShading = sShading == Shading::Wire ? Shading::Solid : Shading::Wire;
      if (io.KeyShift && !command && pressed(ImGuiKey_C))
      {
         if (sSet->cursor[0] != 0.0f || sSet->cursor[1] != 0.0f || sSet->cursor[2] != 0.0f)
         {
            PushUndoCheckpoint("Move 3D cursor");
            std::fill(sSet->cursor, sSet->cursor + 3, 0.0f);
         }
         FrameItems(scene, true);
      }
      if (io.KeyShift && !command && pressed(ImGuiKey_B))
         sZoomArmed = true;
      if (io.KeyShift && !command && pressed(ImGuiKey_Tab))
      {
         PushUndoCheckpoint("Snap in 3D View");
         sSet->snap = !sSet->snap;
      }
      if (io.KeyShift && !command && pressed(ImGuiKey_H))
      {
         PushUndoCheckpoint("Hide in 3D View");
         for (Item& it : scene->Items())
            if (!Selected(it.id))
               it.visible = false;
         scene->Commit();
      }
      if (command && pressed(ImGuiKey_GraveAccent))
         sOverlays = !sOverlays;
      if (command && pressed(ImGuiKey_G))
         GroupSelected(scene, !io.KeyAlt);
      if (command && !io.KeyAlt && !io.KeyShift && pressed(ImGuiKey_C))
         sCopiedScene = scene->SceneText();
      if (command && !io.KeyAlt && !io.KeyShift && pressed(ImGuiKey_V) && !sCopiedScene.empty())
         sPending.what = Pending::PasteScene;
      if (command && !io.KeyAlt && !io.KeyShift && pressed(ImGuiKey_D))
         sPending.what = Pending::DuplicateScene;
      if (command && !io.KeyAlt && pressed(ImGuiKey_I))
      {
         std::vector<int> others;
         for (const Item& it : scene->Items())
            if (Shown(it) && !Selected(it.id))
               others.push_back(it.id);
         sSel.swap(others);
      }
      if (command && !io.KeyAlt && digit(0))
         if (sSel.size() == 1)
            if (Item* it = scene->FindItem(sSel[0]))
               if (scene->ItemKind(*it) == Kind::Camera)
               {
                  PushUndoCheckpoint("Set 3D View camera");
                  scene->activeCamera = it->id;
                  scene->Commit();
                  sCameraView = true;
               }
      if (command && io.KeyAlt && digit(0))
         CameraToView(scene);
      else if (command && !io.KeyAlt)
      {
         if (digit(1)) ViewAxis(5);
         else if (digit(3)) ViewAxis(1);
         else if (digit(7)) ViewAxis(3);
      }
      const bool plain = !command && !io.KeyAlt;
      if (plain && !io.KeyShift)
      {
         if (pressed(ImGuiKey_G)) BeginOp('g', scene, mouse);
         else if (pressed(ImGuiKey_R)) BeginOp('r', scene, mouse);
         else if (pressed(ImGuiKey_S)) BeginOp('s', scene, mouse);
         else if (pressed(ImGuiKey_N)) sSideOpen = !sSideOpen;
         else if (pressed(ImGuiKey_F) || pressed(ImGuiKey_KeypadDecimal)) FrameItems(scene, sSel.empty());
         else if (pressed(ImGuiKey_Period)) sOpenPivot = true;
         else if (pressed(ImGuiKey_Comma)) sOpenOrient = true;
         else if (pressed(ImGuiKey_Slash) || pressed(ImGuiKey_KeypadDivide)) ToggleLocal(scene);
         else if (pressed(ImGuiKey_F2))
         {
            if (const Item* it = sSel.empty() ? nullptr : scene->FindItem(sSel.back()))
               StartRename(*it);
         }
         else if (pressed(ImGuiKey_Home))
         {
            if (sCameraView && !sLockCam)
            {
               sCamZoom = 1.0f;
               sCamPan = ImVec2(0, 0);
            }
            else
               FrameItems(scene, true);
         }
         else if (pressed(ImGuiKey_Z)) sShading = (Shading)(((int)sShading + 1) % 3);
         else if (digit(0)) ToggleCamera(scene);
         else if (digit(1)) ViewAxis(4);
         else if (digit(3)) ViewAxis(0);
         else if (digit(7)) ViewAxis(2);
         else if (digit(5))
         {
            sOrtho = !sOrtho;
            sAutoOrtho = false;
         }
         else if (digit(9))
            OrbitBy(scene, kPi, 0.0f);
         else if (digit(2) || digit(8))
            OrbitBy(scene, 0.0f, digit(8) ? 0.2618f : -0.2618f);
         else if (digit(4) || digit(6))
            OrbitBy(scene, digit(6) ? 0.2618f : -0.2618f, 0.0f);
         else if (pressed(ImGuiKey_A))
         {
            sSel.clear();
            for (const Item& it : scene->Items())
               if (Shown(it))
                  sSel.push_back(it.id);
         }
         else if (pressed(ImGuiKey_H))
            HideSelected(scene);
         else if (pressed(ImGuiKey_X) || pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace))
            DeleteSelected(scene);
         else if (pressed(ImGuiKey_KeypadAdd) || pressed(ImGuiKey_Equal))
            Zoom(0.85f);
         else if (pressed(ImGuiKey_KeypadSubtract) || pressed(ImGuiKey_Minus))
            Zoom(1.0f / 0.85f);
      }
      if (io.KeyAlt && pressed(ImGuiKey_A))
         sSel.clear();
      if (io.KeyAlt && pressed(ImGuiKey_H))
      {
         PushUndoCheckpoint("Show in 3D View");
         for (Item& it : scene->Items())
            it.visible = true;
         scene->Commit();
      }
      if (io.KeyShift && !command && pressed(ImGuiKey_D))
         DuplicateSelected(scene, mouse);
      if (io.KeyShift && !command && pressed(ImGuiKey_A))
         sOpenAdd = true;
   }

   void HandleNavigation(Scene3DNode* scene, bool hovered)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const bool orbitDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
                             (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) || sNavDrag;
      if ((hovered || sNavDrag) && orbitDrag && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
      {
         if (io.KeyShift)
            Pan(io.MouseDelta.x, io.MouseDelta.y);
         else
            OrbitBy(scene, io.MouseDelta.x * 0.008f, io.MouseDelta.y * 0.008f);
      }
      if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
      {
#ifdef __APPLE__
         const bool zoom = io.KeyCtrl || io.KeySuper;
#else
         const bool zoom = !io.KeyShift;
#endif
         if (zoom)
            Zoom(std::pow(0.9f, io.MouseWheel), &io.MousePos);
         else if (io.KeyShift)
            Pan(io.MouseWheelH * 12.0f, io.MouseWheel * 12.0f);
         else
            OrbitBy(scene, io.MouseWheelH * 0.05f, io.MouseWheel * 0.05f);
      }
      // GLFW never forwards a trackpad pinch, so it comes from the platform, not the wheel.
      const double pinch = hovered ? TrackpadPinchThisFrame() : 0.0;
      if (std::abs(pinch) > 0.0005)
         Zoom((float)(1.0 / (1.0 + pinch)), &io.MousePos);
      if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right) && sOp.mode == 0)
      {
         // Fly: the eye stays put under the look, WASD/QE walk it.
         float eye[3];
         EyeFromOrbit(eye);
         if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
         {
            Orbited();
            EyeFromOrbit(eye);
            sAz += io.MouseDelta.x * 0.005f;
            sEl = std::clamp(sEl + io.MouseDelta.y * 0.005f, -1.5697f, 1.5697f);
            float next[3];
            EyeFromOrbit(next);
            for (int k = 0; k < 3; k++)
               sTarget[k] += eye[k] - next[k];
         }
         const float speed = sDist * io.DeltaTime * (io.KeyShift ? 2.5f : 1.0f);
         const float f = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
         const float r = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
         const float u = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
         if (f != 0.0f || r != 0.0f || u != 0.0f)
         {
            Orbited();
            for (int k = 0; k < 3; k++)
               sTarget[k] += (sCam.fwd[k] * f + sCam.right[k] * r + sCam.up[k] * u) * speed;
         }
      }
   }

   float SegDist(ImVec2 p, ImVec2 a, ImVec2 b)
   {
      const float vx = b.x - a.x, vy = b.y - a.y;
      const float t = std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / std::max(1.0f, vx * vx + vy * vy), 0.0f, 1.0f);
      return std::hypot(p.x - a.x - vx * t, p.y - a.y - vy * t);
   }

   // A world line, cut at the near plane so a point behind the eye can't fling it.
   void Line3D(ImDrawList* dl, const float a[3], const float b[3], ImU32 col, float thick = 1.0f)
   {
      const float* m = sCam.viewProj.m;
      const float wa = m[3] * a[0] + m[7] * a[1] + m[11] * a[2] + m[15];
      const float wb = m[3] * b[0] + m[7] * b[1] + m[11] * b[2] + m[15];
      const float eps = 0.02f;
      if (wa < eps && wb < eps)
         return;
      float p[3], q[3];
      std::copy(a, a + 3, p);
      std::copy(b, b + 3, q);
      const float t = (eps - wa) / (wb - wa);
      for (int k = 0; k < 3; k++)
      {
         if (wa < eps)
            p[k] = a[k] + (b[k] - a[k]) * t;
         else if (wb < eps)
            q[k] = a[k] + (b[k] - a[k]) * t;
      }
      ImVec2 s0, s1;
      if (Project(p, s0) && Project(q, s1))
         dl->AddLine(s0, s1, col, thick);
   }

   // Cut at the near plane like any line, so a box the eye stands in still draws.
   void DrawBox(ImDrawList* dl, const float lo[3], const float hi[3], ImU32 col)
   {
      for (int i = 0; i < 8; i++)
         for (int bit : { 1, 2, 4 })
            if ((i & bit) == 0)
            {
               const int j = i | bit;
               const float p[3] = { (i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2] };
               const float q[3] = { (j & 1) ? hi[0] : lo[0], (j & 2) ? hi[1] : lo[1], (j & 4) ? hi[2] : lo[2] };
               Line3D(dl, p, q, col, 1.5f);
            }
   }

   // Blender's 3D cursor: a red and white ring with four ticks.
   void DrawCursor(ImDrawList* dl)
   {
      ImVec2 s;
      if (!Project(sSet->cursor, s))
         return;
      for (int i = 0; i < 8; i++)
      {
         const float a0 = (float)i / 8.0f * 2.0f * kPi, a1 = (float)(i + 1) / 8.0f * 2.0f * kPi;
         dl->PathArcTo(s, 9.0f, a0, a1, 6);
         dl->PathStroke(i % 2 == 0 ? Hex(0xFF3030) : Hex(0xFFFFFF), 0, 1.5f);
      }
      for (int i = 0; i < 4; i++)
      {
         const ImVec2 d(i == 0 ? 1.0f : i == 1 ? -1.0f : 0.0f, i == 2 ? 1.0f : i == 3 ? -1.0f : 0.0f);
         dl->AddLine(ImVec2(s.x + d.x * 6.0f, s.y + d.y * 6.0f), ImVec2(s.x + d.x * 14.0f, s.y + d.y * 14.0f), Hex(0x000000, 0.8f), 1.0f);
      }
   }

   void TransformBy(const Mat4& t, const float p[3], float out[3])
   {
      for (int k = 0; k < 3; k++)
         out[k] = t.m[k] * p[0] + t.m[4 + k] * p[1] + t.m[8 + k] * p[2] + t.m[12 + k];
   }

   // The camera as a frustum, the shape of what it will render.
   void DrawCameraShape(ImDrawList* dl, Scene3DNode* scene, const Item& it, ImU32 col)
   {
      Mat4 view;
      float eye[3];
      CameraNode* lens = nullptr;
      if (!scene->ItemCamera(it, view, eye, &lens) || lens == nullptr)
         return;
      const Mat4 world = Mat4::Inverse(view);
      const float aspect = (float)std::max(1, scene->GetOutputWidth()) / (float)std::max(1, scene->GetOutputHeight());
      const float depth = 1.0f;
      const bool ortho = lens->projection != 0;
      const float h = ortho ? lens->orthoHeight : depth * std::tan(lens->fov * 0.5f * kPi / 180.0f), w = h * aspect;
      float c[4][3];
      const float xs[4] = { -w, w, w, -w }, ys[4] = { -h, -h, h, h };
      for (int i = 0; i < 4; i++)
      {
         const float big = std::max(w, h) * 2.0f;
         const float p[3] = { xs[i] + lens->shiftX * big, ys[i] + lens->shiftY * big, -depth };
         TransformBy(world, p, c[i]);
         const float flat[3] = { p[0], p[1], 0.0f };
         float from[3];
         TransformBy(world, flat, from);
         Line3D(dl, ortho ? from : eye, c[i], col);
      }
      for (int i = 0; i < 4; i++)
         Line3D(dl, c[i], c[(i + 1) % 4], col, 1.5f);
      float tri[3][3];
      const float tx[3] = { -w * 0.5f, w * 0.5f, 0.0f }, ty[3] = { h * 1.1f, h * 1.1f, h * 1.6f };
      for (int i = 0; i < 3; i++)
      {
         const float p[3] = { tx[i], ty[i], -depth };
         TransformBy(world, p, tri[i]);
      }
      ImVec2 s[3];
      if (Project(tri[0], s[0]) && Project(tri[1], s[1]) && Project(tri[2], s[2]))
      {
         if (it.id == scene->activeCamera)
            dl->AddTriangleFilled(s[0], s[1], s[2], col);
         else
            dl->AddTriangle(s[0], s[1], s[2], col);
      }
   }

   // A light drawn as what it does: rays, a cone, a panel, each pointing where it shines.
   void DrawLightShape(ImDrawList* dl, Scene3DNode* scene, const Item& it, const float p[3], ImU32 col)
   {
      GraphNode* gn = FindNodeByUid(it.uid);
      auto* light = gn != nullptr ? dynamic_cast<LightNode*>(gn->node.get()) : nullptr;
      ImVec2 s;
      if (light == nullptr || !Project(p, s))
         return;
      float aim[3];
      if (!scene->ItemLightAim(it, aim))
         return;
      const int type = light->type;
      dl->AddCircleFilled(s, 4.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(light->color[0], light->color[1], light->color[2], 1.0f)), 16);
      dl->AddCircle(s, 7.0f, col, 24, 1.5f);
      if (type != 4 && type != 5)
         for (int i = 0; i < 8; i++)
         {
            const float a = (float)i / 8.0f * 2.0f * kPi, cs = std::cos(a), sn = std::sin(a);
            dl->AddLine(ImVec2(s.x + cs * 10.0f, s.y + sn * 10.0f), ImVec2(s.x + cs * 15.0f, s.y + sn * 15.0f), col);
         }
      float u[3], v[3];
      const float ref[3] = { std::abs(aim[1]) > 0.9f ? 1.0f : 0.0f, std::abs(aim[1]) > 0.9f ? 0.0f : 1.0f, 0.0f };
      Cross(aim, ref, u);
      Normalize(u);
      Cross(u, aim, v);
      if (type == 0 || type == 2 || type == 5)
      {
         const float tip[3] = { p[0] + aim[0] * 1.5f, p[1] + aim[1] * 1.5f, p[2] + aim[2] * 1.5f };
         Line3D(dl, p, tip, col);
      }
      if (type == 4)
      {
         const float len = 1.0f, r = len * std::tan(light->spotAngle * kPi / 180.0f);
         const float blend = r * (1.0f - light->spotPenumbra);
         float prev[3], prevIn[3];
         for (int i = 0; i <= 32; i++)
         {
            const float a = (float)i / 32.0f * 2.0f * kPi, cs = std::cos(a), sn = std::sin(a);
            float q[3], in[3];
            for (int k = 0; k < 3; k++)
            {
               const float base = p[k] + aim[k] * len;
               q[k] = base + (u[k] * cs + v[k] * sn) * r;
               in[k] = base + (u[k] * cs + v[k] * sn) * blend;
            }
            if (i > 0)
            {
               Line3D(dl, prev, q, col);
               Line3D(dl, prevIn, in, col & 0x80FFFFFF);
            }
            if (i % 8 == 0 && i < 32)
               Line3D(dl, p, q, col);
            std::copy(q, q + 3, prev);
            std::copy(in, in + 3, prevIn);
         }
      }
      if (type == 5)
      {
         const float hw = light->areaWidth * 0.5f, hh = light->areaHeight * 0.5f;
         const float xs[4] = { -hw, hw, hw, -hw }, ys[4] = { -hh, -hh, hh, hh };
         float c[4][3];
         for (int i = 0; i < 4; i++)
            for (int k = 0; k < 3; k++)
               c[i][k] = p[k] + u[k] * xs[i] + v[k] * ys[i];
         for (int i = 0; i < 4; i++)
            Line3D(dl, c[i], c[(i + 1) % 4], col, 1.5f);
      }
      if (type == 1 || type == 4 || type == 5)
      {
         const float ground[3] = { p[0], 0.0f, p[2] };
         Line3D(dl, p, ground, col & 0x55FFFFFF);
      }
   }

   // Axis directions the handles point along: the orientation, except scale, which is the item's own.
   void HandleAxis(Scene3DNode* scene, int a, float out[3])
   {
      static const Item kNone;
      const Item* it = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      OrientAxis(it != nullptr ? *it : kNone, sTool == 3 ? 1 : sSet->orientation, a, out);
   }

   // Draws the tool's handles at the selection and says which one the mouse is on.
   int DrawGizmo(ImDrawList* dl, Scene3DNode* scene, const float at[3], ImVec2 mouse, bool active)
   {
      ImVec2 c;
      if (sTool == 0 || !Project(at, c))
         return -1;
      const float len = UnitAt(at) * kGizmoPx;
      int hot = -1;
      float best = 9.0f;
      const ImU32 white = Hex(kHotHandle);
      auto tone = [&](int id, int a, float alpha = 1.0f) { return sHot == id ? white : AxisColor(a, alpha); };
      float axes[3][3];
      for (int a = 0; a < 3; a++)
         HandleAxis(scene, a, axes[a]);

      if (sTool == 2)
      {
         for (int a = 0; a < 3; a++)
         {
            float ua[3], va[3];
            Cross(axes[a], axes[(a + 1) % 3], ua);
            std::copy(axes[(a + 1) % 3], axes[(a + 1) % 3] + 3, va);
            ImVec2 prev;
            bool prevOk = false;
            for (int i = 0; i <= 64; i++)
            {
               const float t = (float)i / 64.0f * 2.0f * kPi;
               float p[3], off[3];
               for (int k = 0; k < 3; k++)
               {
                  off[k] = (ua[k] * std::cos(t) + va[k] * std::sin(t)) * len;
                  p[k] = at[k] + off[k];
               }
               ImVec2 s;
               const bool ok = Project(p, s);
               const bool front = Dot(off, sCam.fwd) <= 0.0f;
               if (ok && prevOk)
               {
                  dl->AddLine(prev, s, tone(a, a, front ? 1.0f : 0.25f), front ? 2.5f : 1.5f);
                  const float dist = SegDist(mouse, prev, s);
                  if (front && dist < best)
                  {
                     best = dist;
                     hot = a;
                  }
               }
               prev = s;
               prevOk = ok;
            }
         }
         const float r = kGizmoPx * 1.2f;
         dl->AddCircle(c, r, sHot == 3 ? white : Hex(kHotHandle, 0.6f), 64, 1.5f);
         const float dist = std::abs(std::hypot(mouse.x - c.x, mouse.y - c.y) - r);
         if (dist < best)
            hot = 3;
         return active ? hot : -1;
      }

      // The plane squares, for the tool that moves; the two axes either side of each.
      if (sTool == 1)
         for (int n = 0; n < 3; n++)
         {
            if (std::abs(Dot(axes[n], sCam.fwd)) < 0.14f)
               continue;
            const float* u = axes[(n + 1) % 3];
            const float* v = axes[(n + 2) % 3];
            const float lo = len * 0.3f, hi = len * 0.46f;
            const float uv[4][2] = { { lo, lo }, { hi, lo }, { hi, hi }, { lo, hi } };
            ImVec2 q[4];
            bool ok = true;
            for (int i = 0; i < 4; i++)
            {
               const float p[3] = { at[0] + u[0] * uv[i][0] + v[0] * uv[i][1], at[1] + u[1] * uv[i][0] + v[1] * uv[i][1],
                                    at[2] + u[2] * uv[i][0] + v[2] * uv[i][1] };
               ok = ok && Project(p, q[i]);
            }
            if (!ok)
               continue;
            dl->AddConvexPolyFilled(q, 4, sHot == 4 + n ? white : AxisColor(n, 0.55f));
            if (ImTriangleContainsPoint(q[0], q[1], q[2], mouse) || ImTriangleContainsPoint(q[0], q[2], q[3], mouse))
            {
               hot = 4 + n;
               best = 0.0f;
            }
         }

      for (int a = 0; a < 3; a++)
      {
         const float fade = 1.0f - std::clamp((std::abs(Dot(axes[a], sCam.fwd)) - 0.93f) / 0.05f, 0.0f, 1.0f);
         if (fade <= 0.0f)
            continue;
         const float tip[3] = { at[0] + axes[a][0] * len, at[1] + axes[a][1] * len, at[2] + axes[a][2] * len };
         ImVec2 s;
         if (!Project(tip, s))
            continue;
         const float dx = s.x - c.x, dy = s.y - c.y, l = std::max(1.0f, std::hypot(dx, dy));
         const ImVec2 dir(dx / l, dy / l), side(-dy / l, dx / l);
         const ImU32 col = tone(a, a, fade);
         dl->AddLine(ImVec2(c.x + dir.x * 14.0f, c.y + dir.y * 14.0f), s, col, 2.5f);
         if (sTool == 1)
            dl->AddTriangleFilled(ImVec2(s.x + dir.x * 16.0f, s.y + dir.y * 16.0f), ImVec2(s.x + side.x * 6.0f, s.y + side.y * 6.0f),
                                  ImVec2(s.x - side.x * 6.0f, s.y - side.y * 6.0f), col);
         else
            dl->AddRectFilled(ImVec2(s.x - 5, s.y - 5), ImVec2(s.x + 5, s.y + 5), col);
         const float dist = SegDist(mouse, c, ImVec2(s.x + dir.x * 16.0f, s.y + dir.y * 16.0f));
         if (dist < best && std::hypot(mouse.x - c.x, mouse.y - c.y) > 12.0f)
         {
            best = dist;
            hot = a;
         }
      }
      dl->AddCircle(c, 9.0f, sHot == 3 ? white : Hex(kHotHandle, 0.75f), 24, 1.5f);
      if (std::hypot(mouse.x - c.x, mouse.y - c.y) <= 11.0f)
         hot = 3;
      return active ? hot : -1;
   }

   // Top right: the three axes as they stand now. Click a ball for that view, drag to orbit.
   int DrawNavGizmo(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImVec2 mouse, bool hovered, bool& onDisc)
   {
      const ImVec2 c(mx.x - 52.0f, mn.y + 52.0f);
      const float r = 34.0f;
      onDisc = hovered && std::hypot(mouse.x - c.x, mouse.y - c.y) < r + 12.0f;
      if (onDisc || sNavDrag)
         dl->AddCircleFilled(c, r + 12.0f, Hex(kHotHandle, 0.10f), 48);
      struct Ball { float depth; ImVec2 at; int axis; bool neg; };
      std::vector<Ball> balls;
      for (int a = 0; a < 3; a++)
      {
         const float dir[3] = { a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f };
         const ImVec2 offs(Dot(dir, sCam.right) * r, -Dot(dir, sCam.up) * r);
         const float depth = Dot(dir, sCam.fwd);
         balls.push_back({ depth, ImVec2(c.x + offs.x, c.y + offs.y), a, false });
         balls.push_back({ -depth, ImVec2(c.x - offs.x, c.y - offs.y), a, true });
      }
      std::sort(balls.begin(), balls.end(), [](const Ball& x, const Ball& y) { return x.depth > y.depth; });
      int hit = -1;
      for (const Ball& b : balls)
      {
         const bool hot = onDisc && std::hypot(mouse.x - b.at.x, mouse.y - b.at.y) < 10.0f;
         if (hot)
            hit = b.axis * 2 + (b.neg ? 1 : 0);
         const float size = b.neg ? 7.0f : 9.0f;
         if (b.neg)
         {
            dl->AddCircleFilled(b.at, size, AxisColor(b.axis, hot ? 0.9f : 0.35f), 24);
            dl->AddCircle(b.at, size, AxisColor(b.axis), 24, 1.5f);
            continue;
         }
         dl->AddLine(c, b.at, AxisColor(b.axis), 2.0f);
         dl->AddCircleFilled(b.at, size, hot ? Hex(kHotHandle) : AxisColor(b.axis), 24);
         const char label[2] = { (char)('X' + b.axis), 0 };
         const ImVec2 ts = ImGui::CalcTextSize(label);
         dl->AddText(ImVec2(b.at.x - ts.x * 0.5f, b.at.y - ts.y * 0.5f), Hex(0x000000), label);
      }
      return hit;
   }

   // Nearest item under the ray. Cameras and lights go by their marker and win, being
   // small; geometry goes by its box, hit by the ray itself.
   int Pick(Scene3DNode* scene, ImVec2 mouse)
   {
      int best = 0;
      float bestDepth = 1e30f;
      for (const Item& it : scene->Items())
      {
         float p[3];
         ImVec2 s;
         if (!Shown(it) || !scene->ItemPoint(it, p) || (it.id == scene->activeCamera && sCameraView))
            continue;
         const float depth = (p[0] - sCam.eye[0]) * sCam.fwd[0] + (p[1] - sCam.eye[1]) * sCam.fwd[1] + (p[2] - sCam.eye[2]) * sCam.fwd[2];
         if (Project(p, s) && std::hypot(mouse.x - s.x, mouse.y - s.y) < 14.0f && depth < bestDepth)
         {
            bestDepth = depth;
            best = it.id;
         }
      }
      if (best != 0)
         return best;
      float o[3], d[3];
      MouseRay(mouse, o, d);
      for (const Item& it : scene->Items())
      {
         float p[3], lo[3], hi[3];
         if (!Shown(it) || scene->ItemPoint(it, p) || !scene->ItemBounds(it.id, lo, hi))
            continue;
         float near = -1e30f, far = 1e30f;
         for (int k = 0; k < 3 && near <= far; k++)
         {
            if (std::abs(d[k]) < 1e-6f)
            {
               if (o[k] < lo[k] || o[k] > hi[k])
                  far = -1e30f;
               continue;
            }
            const float a = (lo[k] - o[k]) / d[k], b = (hi[k] - o[k]) / d[k];
            near = std::max(near, std::min(a, b));
            far = std::min(far, std::max(a, b));
         }
         if (near > far || far < 0.0f)
            continue;
         const float depth = std::max(near, 0.0f);
         if (depth < bestDepth)
         {
            bestDepth = depth;
            best = it.id;
         }
      }
      return best;
   }

   void DrawOverlayText(ImDrawList* dl, Scene3DNode* scene, ImVec2 mn)
   {
      const char* names[6] = { T("Right Orthographic"), T("Left Orthographic"), T("Top Orthographic"),
                               T("Bottom Orthographic"), T("Front Orthographic"), T("Back Orthographic") };
      const char* view = sCameraView ? (sCam.ortho ? T("Camera Orthographic") : T("Camera Perspective"))
                         : sViewName >= 0 && sOrtho ? names[sViewName]
                         : sOrtho ? T("User Orthographic") : T("User Perspective");
      float y = mn.y + tok::space_2;
      auto line = [&](const std::string& text, float alpha)
      {
         dl->AddText(ImVec2(mn.x + tok::space_3, y), Hex(0xFFFFFF, alpha), text.c_str());
         y += ImGui::GetTextLineHeightWithSpacing();
      };
      line(sLocal.empty() ? std::string(view) : std::string(view) + " " + T("(Local)"), 0.9f);
      if (sSel.size() == 1)
      {
         if (const Item* it = scene->FindItem(sSel[0]))
            line(ItemName(*it), 0.6f);
      }
      else if (sSel.size() > 1)
      {
         char text[48];
         snprintf(text, sizeof(text), T("%d selected"), (int)sSel.size());
         line(text, 0.6f);
      }
      if (scene->ClippedDraws() > 0)
      {
         char text[64];
         snprintf(text, sizeof(text), T("%d objects over the limit"), (int)scene->ClippedDraws());
         line(text, 0.9f);
      }
   }

   void DashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col)
   {
      auto dash = [&](ImVec2 p, ImVec2 q)
      {
         const float len = std::hypot(q.x - p.x, q.y - p.y), step = 3.0f;
         for (float t = 0.0f; t < len; t += step * 2.0f)
         {
            const float e = std::min(len, t + step);
            dl->AddLine(ImVec2(p.x + (q.x - p.x) * t / len, p.y + (q.y - p.y) * t / len),
                        ImVec2(p.x + (q.x - p.x) * e / len, p.y + (q.y - p.y) * e / len), col);
         }
      };
      dash(a, ImVec2(b.x, a.y));
      dash(ImVec2(b.x, a.y), b);
      dash(b, ImVec2(a.x, b.y));
      dash(ImVec2(a.x, b.y), a);
   }

   // The frame, as Blender draws it: dashes round the render, then any guides inside.
   void DrawCameraGuides(ImDrawList* dl)
   {
      const ImVec2 a = sCam.frameMn, b = sCam.frameMx;
      const float w = b.x - a.x, h = b.y - a.y;
      const ImU32 edge = Hex(0x000000, 0.9f), guide = Hex(kWire, 0.7f);
      DashedRect(dl, a, b, edge);
      auto cross = [&](float fx, float fy)
      {
         dl->AddLine(ImVec2(a.x + w * fx, a.y), ImVec2(a.x + w * fx, b.y), guide);
         dl->AddLine(ImVec2(a.x, a.y + h * fy), ImVec2(b.x, a.y + h * fy), guide);
      };
      if (sSet->thirds)
      {
         cross(1.0f / 3.0f, 1.0f / 3.0f);
         cross(2.0f / 3.0f, 2.0f / 3.0f);
      }
      if (sSet->golden)
      {
         cross(0.382f, 0.382f);
         cross(0.618f, 0.618f);
      }
      if (sSet->center)
         cross(0.5f, 0.5f);
      if (sSet->safe)
      {
         const float act = sSet->safeAction;
         dl->AddRect(ImVec2(a.x + w * act, a.y + h * act), ImVec2(b.x - w * act, b.y - h * act), guide);
         const float m = act + sSet->safeTitle;
         dl->AddRect(ImVec2(a.x + w * m, a.y + h * m), ImVec2(b.x - w * m, b.y - h * m), guide);
      }
   }

   // Along the bottom edge: drag to zoom, drag to pan, camera view, then the lock or the projection.
   void DrawToolpad(ImDrawList* dl, Scene3DNode* scene, ImVec2 mn, ImVec2 mx, ImVec2 mouse, bool hovered, bool& onPad)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const float r = 15.0f, y = mx.y - r - 12.0f;
      const char* icons[4] = { IconsInfinite::Zoom, IconsInfinite::Hand, IconsInfinite::ViewCamera,
                               sCameraView ? IconsInfinite::Lock : sOrtho ? IconsInfinite::ViewOrtho : IconsInfinite::ViewPersp };
      const char* tips[4] = { T("Zoom: drag"), T("Pan: drag"), T("Camera view (0)"),
                              sCameraView ? T("Lock camera to view") : T("Perspective or orthographic (5)") };
      for (int i = 0; i < 4; i++)
      {
         const ImVec2 c(mx.x - r - 12.0f - (float)(3 - i) * (r * 2.0f + 6.0f), y);
         const bool hot = hovered && std::hypot(mouse.x - c.x, mouse.y - c.y) <= r;
         const bool on = (i == 2 && sCameraView) || (i == 3 && sCameraView && sLockCam) || sPadDrag == i + 1;
         dl->AddCircleFilled(c, r, Hex(on ? kSelected : 0x000000, hot || on ? 0.85f : 0.4f), 32);
         glyph::Draw(dl, c, tok::icon_md, Hex(0xFFFFFF), icons[i]);
         if (!hot)
            continue;
         onPad = true;
         HelpTip("%s", tips[i]);
         if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            continue;
         if (i < 2)
            sPadDrag = i + 1;
         else if (i == 2)
            ToggleCamera(scene);
         else if (sCameraView)
            sLockCam = !sLockCam;
         else
         {
            sOrtho = !sOrtho;
            sAutoOrtho = false;
         }
      }
      if (sPadDrag != 0)
      {
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            sPadDrag = 0;
         else if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
         {
            if (sPadDrag == 1)
               Zoom(std::pow(1.01f, io.MouseDelta.y));
            else
               Pan(io.MouseDelta.x, io.MouseDelta.y);
         }
      }
   }

   void DrawViewport(Scene3DNode* scene, ImVec2 size)
   {
      const ImGuiIO& io = ImGui::GetIO();
      ImVec2 mn = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##view3dcanvas", size,
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                                ImGuiButtonFlags_MouseButtonRight);
      const bool hovered = ImGui::IsItemHovered();
      const ImVec2 mx(mn.x + size.x, mn.y + size.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->PushClipRect(mn, mx, true);
      dl->AddRectFilled(mn, mx, Hex(kBackdrop));

      BuildCam(scene, mn, mx);
      Scene3DNode::View view;
      view.view = sCam.view;
      view.proj = sCam.proj;
      std::copy(sCam.eye, sCam.eye + 3, view.eye);
      view.shading = sShading;
      view.grid = sGrid && sOverlays;
      view.only = sLocal;
      view.gridAxis = sGridAxis;
      view.gridDist = sDist;
      const float dpi = io.DisplayFramebufferScale.y;
      const unsigned int tex = scene->RenderView((int)(size.x * dpi), (int)(size.y * dpi), view);
      if (tex != 0)
         dl->AddImage((ImTextureID)(intptr_t)tex, mn, mx, ImVec2(0, 1), ImVec2(1, 0));

      const ImVec2 mouse = io.MousePos;
      int selCount = 0;
      bool cameraSelected = false;
      for (const Item& it : scene->Items())
      {
         const bool sel = Selected(it.id);
         const bool active = !sSel.empty() && sSel.back() == it.id;
         const ImU32 col = active ? Hex(kActive) : sel ? Hex(kSelected) : Hex(kWire, it.visible ? 0.8f : 0.3f);
         float p[3];
         const Kind kind = scene->ItemKind(it);
         const bool looking = sCameraView && it.id == scene->activeCamera;
         if (sel)
         {
            selCount++;
            cameraSelected = cameraSelected || looking;
         }
         if (!sOverlays || !InLocal(it.id))
            continue;
         if (scene->ItemPoint(it, p))
         {
            if (kind == Kind::Camera && !looking)
               DrawCameraShape(dl, scene, it, col);
            else if (kind == Kind::Light)
               DrawLightShape(dl, scene, it, p, col);
         }
         else if (sel)
         {
            float lo[3], hi[3];
            if (scene->ItemBounds(it.id, lo, hi))
               DrawBox(dl, lo, hi, col);
         }
      }
      if (sOverlays)
         DrawCursor(dl);

      if (sCameraView)
      {
         const ImU32 shade = Hex(0x000000, sSet->passAlpha);
         dl->AddRectFilled(mn, ImVec2(mx.x, sCam.frameMn.y), shade);
         dl->AddRectFilled(ImVec2(mn.x, sCam.frameMx.y), mx, shade);
         dl->AddRectFilled(ImVec2(mn.x, sCam.frameMn.y), ImVec2(sCam.frameMn.x, sCam.frameMx.y), shade);
         dl->AddRectFilled(ImVec2(sCam.frameMx.x, sCam.frameMn.y), ImVec2(mx.x, sCam.frameMx.y), shade);
         DrawCameraGuides(dl);
      }

      const bool canGrab = hovered && sOp.mode == 0 && !sBoxOn;
      bool onNav = false;
      const int navBall = DrawNavGizmo(dl, mn, mx, mouse, hovered && sOp.mode == 0, onNav);
      bool onPad = false;
      DrawToolpad(dl, scene, mn, mx, mouse, hovered && sOp.mode == 0, onPad);
      onNav = onNav || onPad;
      int hot = -1;
      if (selCount > 0 && sOp.mode == 0 && !cameraSelected && sOverlays)
      {
         float center[3];
         PivotPoint(scene, center);
         hot = DrawGizmo(dl, scene, center, mouse, canGrab && !onNav);
      }
      sHot = hot;
      if (sOp.mode != 0)
      {
         ImVec2 c;
         const bool onScreen = Project(sOp.pivot, c);
         if (sOp.axis >= 0)
         {
            float axis[3];
            OpAxis(sOp.before.back(), axis);
            const float reach = sDist * 40.0f;
            const float a[3] = { sOp.pivot[0] - axis[0] * reach, sOp.pivot[1] - axis[1] * reach, sOp.pivot[2] - axis[2] * reach };
            const float b[3] = { sOp.pivot[0] + axis[0] * reach, sOp.pivot[1] + axis[1] * reach, sOp.pivot[2] + axis[2] * reach };
            Line3D(dl, a, b, AxisColor(sOp.axis), 1.5f);
         }
         if (onScreen && sOp.mode != 'g')
            dl->AddLine(c, mouse, Hex(kWire, 0.7f));
      }

      gView3DClaimedKeys = hovered || sOp.mode != 0;
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         sRightOk = sOp.mode == 0;
      HandleKeys(scene, mouse, hovered);
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         sNavDrag = false;
      HandleNavigation(scene, hovered);
      if (sLockDirty)
      {
         sLockDirty = false;
         if (Item* cam = scene->FindItem(scene->activeCamera))
         {
            if (!sLockUndo)
               PushUndoCheckpoint("Move camera in view");
            sLockUndo = true;
            scene->AlignToView(*cam, OrbitView());
            scene->Commit();
         }
      }
      if (!ImGui::IsAnyMouseDown())
         sLockUndo = false;

      if (hovered && sOp.mode == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
      {
         if (sZoomArmed)
         {
            sZoomArmed = false;
            sBoxZoom = sBoxOn = true;
            sPressAt = mouse;
         }
         else if (onPad)
         {
         }
         else if (navBall >= 0)
            ViewAxis(navBall);
         else if (onNav)
            sNavDrag = true;
         else if (hot >= 0)
         {
            const char mode = sTool == 1 ? 'g' : sTool == 2 ? 'r' : 's';
            if (hot >= 4)
               BeginOp(mode, scene, mouse, -1, true, hot - 4);
            else
               BeginOp(mode, scene, mouse, hot == 3 ? -1 : hot, true);
         }
         else
         {
            const int id = Pick(scene, mouse);
            sPressAt = mouse;
            if (id != 0)
            {
               if (!io.KeyShift)
               {
                  if (Selected(id))
                     sReduceTo = id;
                  else
                     sSel = { id };
               }
               else
               {
                  // Shift: add, make active, and only a click on the active one takes it off.
                  const bool wasActive = !sSel.empty() && sSel.back() == id;
                  if (Selected(id))
                     sSel.erase(std::find(sSel.begin(), sSel.end(), id));
                  if (!wasActive)
                     sSel.push_back(id);
               }
               sTweak = sTool == 1 && !io.KeyShift;
            }
            else
               sBoxOn = true;
         }
      }
      if (sReduceTo != 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         if (io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 25.0f)
            sSel = { sReduceTo };
         sReduceTo = 0;
      }
      if (sTweak)
      {
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            sTweak = false;
         else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f))
         {
            sTweak = false;
            BeginOp('g', scene, sPressAt, -1, true);
         }
      }
      if (sBoxOn)
      {
         const ImVec2 lo(std::min(sPressAt.x, mouse.x), std::min(sPressAt.y, mouse.y));
         const ImVec2 hi(std::max(sPressAt.x, mouse.x), std::max(sPressAt.y, mouse.y));
         const bool dragged = hi.x - lo.x > 4.0f || hi.y - lo.y > 4.0f;
         if (dragged)
         {
            dl->AddRectFilled(lo, hi, Hex(0xFFFFFF, 0.06f));
            dl->AddRect(lo, hi, Hex(0xFFFFFF, 0.7f));
         }
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            sBoxOn = false;
            const bool zoomBox = sBoxZoom;
            sBoxZoom = false;
            if (zoomBox)
            {
               float o[3], d[3], hit[3];
               const ImVec2 mid((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
               MouseRay(mid, o, d);
               const float fill = std::max((hi.x - lo.x) / std::max(1.0f, mx.x - mn.x), (hi.y - lo.y) / std::max(1.0f, mx.y - mn.y));
               const bool aimed = dragged && PlaneHit(sTarget, sCam.fwd, o, d, hit);
               if (aimed)
               {
                  LeaveCamera();
                  std::copy(hit, hit + 3, sTarget);
                  sDist = std::clamp(sDist * fill, 0.1f, 500.0f);
               }
            }
            else if (!io.KeyShift)
               sSel.clear();
            if (dragged && !zoomBox)
               for (const Item& it : scene->Items())
               {
                  float c[3];
                  scene->ItemCenter(it, c);
                  ImVec2 s;
                  if (Shown(it) && Project(c, s) && s.x >= lo.x && s.x <= hi.x && s.y >= lo.y && s.y <= hi.y && !Selected(it.id))
                     sSel.push_back(it.id);
               }
         }
      }

      if (hovered && sRightOk && sOp.mode == 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
          io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 16.0f)
      {
         float o[3], d[3], hit[3];
         MouseRay(mouse, o, d);
         if (io.KeyShift)
         {
            if (PlaneHit(sSet->cursor, sCam.fwd, o, d, hit))
            {
               PushUndoCheckpoint("Move 3D cursor");
               std::copy(hit, hit + 3, sSet->cursor);
            }
         }
         else
         {
            const int id = Pick(scene, mouse);
            if (id != 0 && !Selected(id))
               sSel = { id };
            sOpenMenu = true;
         }
      }

      DrawOverlayText(dl, scene, mn);
      if (sOp.mode != 0)
      {
         char line[96];
         const char* orient = sOp.axis < 0 && sOp.plane < 0 ? "" : sOp.orient == 1 ? T("Local ") : sOp.orient == 2 ? T("View ") : T("Global ");
         snprintf(line, sizeof(line), "%s %s%s%s", sOp.mode == 'g' ? T("Move") : sOp.mode == 'r' ? T("Rotate") : T("Scale"), orient,
                  sOp.axis < 0 ? "" : sOp.axis == 0 ? "X " : sOp.axis == 1 ? "Y " : "Z ", sOp.typed.c_str());
         dl->AddText(ImVec2(mn.x + tok::space_3, mx.y - 24), Hex(0xFFFFFF, 0.95f), line);
      }
      else if (sZoomArmed)
         dl->AddText(ImVec2(mn.x + tok::space_3, mx.y - 24), Hex(0xFFFFFF, 0.95f), T("Drag a box to zoom into"));
      if (scene->Items().empty())
      {
         const char* head = T("Nothing in the 3D View yet");
         const char* hint = T("Shift+A adds a camera, a light or a node from the canvas");
         const ImVec2 hs = ImGui::CalcTextSize(head), ts = ImGui::CalcTextSize(hint);
         const float midY = (mn.y + mx.y) * 0.5f;
         dl->AddText(ImVec2((mn.x + mx.x - hs.x) * 0.5f, midY - hs.y - tok::space_1), Hex(0xFFFFFF, 0.8f), head);
         dl->AddText(ImVec2((mn.x + mx.x - ts.x) * 0.5f, midY + tok::space_1), Hex(0xFFFFFF, 0.55f), hint);
      }
      dl->PopClipRect();
   }

   bool IconChip(const char* id, const char* glyphStr, bool on, const char* tip)
   {
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const bool clicked = ActionButton::Draw(id, ImVec2(kIconW, tok::tile));
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", tip);
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), tok::icon_md,
                  on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text), glyphStr);
      return clicked;
   }

   void DrawHeader(Scene3DNode* scene)
   {
      const float availW = ImGui::GetContentRegionAvail().x;
      const float startX = ImGui::GetCursorPosX();
      auto gap = []() { FormParts::Inline(0.0f, tok::space_3); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      if (IconChip("##v3dadd", IconsInfinite::Plus, false, T("Add a node, a camera, a light, an HDRI or a new scene (Shift+A)")))
         sOpenAdd = true;
      gap();
      if (IconChip("##v3dselect", IconsInfinite::Pointer, sTool == 0, T("Select (Shift+Space, W)"))) sTool = 0;
      next();
      if (IconChip("##v3dmove", IconsInfinite::ToolMove, sTool == 1, T("Move tool (Shift+Space, G). G moves at once"))) sTool = 1;
      next();
      if (IconChip("##v3drotate", IconsInfinite::ToolRotate, sTool == 2, T("Rotate tool (Shift+Space, R). R rotates at once"))) sTool = 2;
      next();
      if (IconChip("##v3dscale", IconsInfinite::ToolScale, sTool == 3, T("Scale tool (Shift+Space, S). S scales at once"))) sTool = 3;
      gap();
      if (IconChip("##v3dwire", IconsInfinite::ShadeWire, sShading == Shading::Wire, T("Wireframe (Shift+Z)"))) sShading = Shading::Wire;
      next();
      if (IconChip("##v3dsolid", IconsInfinite::ShadeSolid, sShading == Shading::Solid, T("Solid: a neutral light, to see the shapes"))) sShading = Shading::Solid;
      next();
      if (IconChip("##v3dfinal", IconsInfinite::ShadeRender, sShading == Shading::Final, T("Rendered: your lights, world and render settings (Z cycles)"))) sShading = Shading::Final;
      gap();
      if (IconChip("##v3dcamera", IconsInfinite::ViewCamera, sCameraView,
                   scene->activeCamera != 0 ? T("Camera view (Numpad 0 or 0)") : T("Add a camera from this view (0)")))
         ToggleCamera(scene);
      next();
      if (IconChip("##v3dortho", sOrtho ? IconsInfinite::ViewOrtho : IconsInfinite::ViewPersp, sOrtho, T("Perspective or orthographic (5)")))
      {
         sOrtho = !sOrtho;
         sAutoOrtho = false;
      }
      gap();
      if (IconChip("##v3dsnap", IconsInfinite::Snap, sSet->snap, T("Snap (Shift+Tab, hold Ctrl to flip)")))
      {
         PushUndoCheckpoint("Snap in 3D View");
         sSet->snap = !sSet->snap;
      }
      next();
      if (IconChip("##v3dgrid", IconsInfinite::GridDots, sGrid, T("Grid"))) sGrid = !sGrid;
      next();
      if (IconChip("##v3doverlays", IconsInfinite::Eye, sOverlays, T("Overlays (Ctrl+`)"))) sOverlays = !sOverlays;
      ActionButton::RowNext();
      // Pinned right like the Sample Studio's Convert.
      const float rightX = startX + availW - kIconW;
      if (scene->panelOnly)
      {
         const char* convertL = L("Convert##v3dconvert");
         const float convertW = ImGui::CalcTextSize(convertL, ImGui::FindRenderedTextEnd(convertL)).x + 2.0f * tok::space_3;
         ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightX - tok::space_1 - convertW));
         if (ActionButton::Draw(convertL, ImVec2(convertW, tok::tile)))
            sPending.what = Pending::ConvertScene;
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Put this scene on the canvas"));
         ActionButton::RowNext();
      }
      ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightX));
      if (IconChip("##v3dside", IconsInfinite::Edit, sSideOpen, T("Properties (N)"))) sSideOpen = !sSideOpen;
   }

   OutputNode* FindOutput()
   {
      GraphNode* self = SceneGraphNode();
      if (self == nullptr)
         return nullptr;
      for (GraphNode& gn : gNodes)
         if (auto* o = dynamic_cast<OutputNode*>(gn.node.get()))
            if (o->Input().GetSource() == self->node.get())
               return o;
      return nullptr;
   }

   OutputNode* EnsureOutputImpl()
   {
      if (OutputNode* found = FindOutput())
         return found;
      GraphNode* self = SceneGraphNode();
      if (self == nullptr)
         return nullptr;
      const int selfIndex = self->index;
      const ImVec2 at = NodePos(*self);
      GraphNode* spawned = SpawnNode("Output", "Utility", at.x + 320.0f, at.y);
      std::string error;
      if (spawned == nullptr)
         return nullptr;
      auto* out = dynamic_cast<OutputNode*>(spawned->node.get());
      return ConnectNodes(selfIndex, 0, spawned->index, 0, error) ? out : nullptr;
   }

   float sFieldW = 180.0f;
   constexpr float kFieldLabelW = 100.0f;   // the shared column is too narrow for Resolution X or Field of view

   void FieldLabel(const char* text)
   {
      ImGui::AlignTextToFramePadding();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float h = ImGui::GetFrameHeight();
      ImGui::PushClipRect(p, ImVec2(p.x + kFieldLabelW - ImGui::GetCursorPosX() - 4.0f, p.y + h), true);
      ImGui::TextColored(ImVec4(t.x, t.y, t.z, 0.72f), "%s", text);
      ImGui::PopClipRect();
      FormParts::Inline(kFieldLabelW);
   }

   void Hint(const char* text)
   {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", text);
      ImGui::PopTextWrapPos();
   }

   void Undoable(const char* what)
   {
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint(what);
   }

   bool FloatRow(const char* label, float* v, float lo, float hi, const char* fmt, const char* undo = "Edit in 3D View")
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      const bool changed = FieldWell::Slider(FormParts::Id(label).c_str(), v, lo, hi, fmt);
      Undoable(undo);
      return changed;
   }

   bool IntInputRow(const char* label, int* v)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      FieldWell::PushStyle();
      const bool changed = FieldWell::InputInt(FormParts::Id(label).c_str(), v);
      FieldWell::PopStyle();
      Undoable("Edit in 3D View");
      return changed;
   }

   bool ComboRow(const char* label, int* v, const std::vector<std::string>& names, const char* undo = "Edit in 3D View")
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      const int shown = std::clamp(*v, 0, (int)names.size() - 1);
      bool changed = false;
      if (MenuParts::BeginCombo(FormParts::Id(label).c_str(), names[shown].c_str()))
      {
         for (int i = 0; i < (int)names.size(); i++)
            if (MenuParts::Choice(names[i].c_str(), i == *v) && i != *v)
            {
               PushUndoCheckpoint(undo);
               *v = i;
               changed = true;
            }
         MenuParts::EndCombo();
      }
      return changed;
   }

   bool BoolRow(const char* label, bool* v, const char* undo = "Edit in 3D View")
   {
      // Box first and the label beside it, so a long label has the whole row.
      const float rowY = ImGui::GetCursorPosY();
      ImGui::SetCursorPosY(rowY + (ImGui::GetFrameHeight() - CheckBox::kSize) * 0.5f);
      bool on = *v;
      bool hit = CheckBox::Box(FormParts::Id(label).c_str(), &on);
      FormParts::Inline(0.0f, tok::space_2);
      ImGui::SetCursorPosY(rowY);
      ImGui::AlignTextToFramePadding();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::TextColored(ImVec4(t.x, t.y, t.z, 0.72f), "%s", label);
      if (ImGui::IsItemClicked())
      {
         on = !on;
         hit = true;
      }
      if (!hit)
         return false;
      PushUndoCheckpoint(undo);
      *v = on;
      return true;
   }

   bool ColorRow(const char* label, float* rgb, const char* undo = "Edit in 3D View")
   {
      FieldLabel(label);
      const bool changed = FormParts::ColorDot(FormParts::Id(label).c_str(), rgb, 15.0f, ImGui::GetFrameHeight());
      static float sLast = -1.0f;
      if (changed && ImGui::GetTime() - sLast > 1.0f)   // one drag across the picker, one undo
         PushUndoCheckpoint(undo);
      if (changed)
         sLast = (float)ImGui::GetTime();
      return changed;
   }

   // Blender's collapsing panel, optionally with its switch in the header.
   bool Card(const char* title, bool* enable = nullptr, bool startOpen = true)
   {
      static std::map<std::string, bool> sOpen;
      auto found = sOpen.find(title);
      bool open = found != sOpen.end() ? found->second : startOpen;
      SectionCard::Begin();
      ImGui::PushID(title);
      const float h = ImGui::GetTextLineHeight() + 2.0f;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      // Overlap lets the switch drawn on top take its own clicks.
      ImGui::SetNextItemAllowOverlap();
      if (ImGui::InvisibleButton("##head", ImVec2(w, h)))
         open = !open;
      sOpen[title] = open;
      const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p.x + 5.0f, p.y + h * 0.5f);
      if (open)
         dl->AddTriangleFilled(ImVec2(c.x - 4, c.y - 2), ImVec2(c.x + 4, c.y - 2), ImVec2(c.x, c.y + 3), text);
      else
         dl->AddTriangleFilled(ImVec2(c.x - 2, c.y - 4), ImVec2(c.x + 3, c.y), ImVec2(c.x - 2, c.y + 4), text);
      float x = p.x + 16.0f;
      if (enable != nullptr)
      {
         const ImVec2 after = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(x, p.y));
         bool on = *enable;
         if (CheckBox::Box("##on", &on))
         {
            PushUndoCheckpoint("Edit in 3D View");
            *enable = on;
         }
         ImGui::SetCursorScreenPos(after);
         x += 22.0f;
      }
      dl->AddText(ImVec2(x, p.y + 1.0f), text, title);
      ImGui::PopID();
      return open;
   }

   const char* KindGlyph(Scene3DNode* scene, const Item& it)
   {
      const Kind kind = scene->ItemKind(it);
      if (kind == Kind::Camera)
         return IconsInfinite::ViewCamera;
      if (kind != Kind::Light)
         return IconsInfinite::Cube;
      GraphNode* gn = FindNodeByUid(it.uid);
      auto* light = gn != nullptr ? dynamic_cast<LightNode*>(gn->node.get()) : nullptr;
      const int type = light != nullptr ? light->type : 1;
      return type == 4 ? IconsInfinite::LightSpot : type == 5 ? IconsInfinite::LightArea
             : type == 1 ? IconsInfinite::LightPoint : IconsInfinite::LightSun;
   }

   // The list of what's in the scene. Eye hides, the camera mark says which one renders.
   void DrawOutliner(Scene3DNode* scene, float availW, float roomH)
   {
      SectionCard::Begin(T("Scene"));
      std::vector<GraphNode*> scenes;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<Scene3DNode*>(gn.node.get()) != nullptr)
            scenes.push_back(&gn);
      if (scenes.size() > 1)
      {
         auto title = [](const GraphNode& gn) { return gn.typeName + " " + std::to_string(gn.index); };
         const GraphNode* current = SceneGraphNode();
         ImGui::SetNextItemWidth(availW);
         if (MenuParts::BeginCombo("##v3dscene", current != nullptr ? title(*current).c_str() : ""))
         {
            for (GraphNode* gn : scenes)
               if (MenuParts::Choice(title(*gn).c_str(), gn == current) && gn != current)
               {
                  sSceneUid = gn->uid;
                  sSel.clear();
               }
            MenuParts::EndCombo();
         }
      }
      if (scene->Items().empty())
         ImGui::TextDisabled("%s", T("Empty. Shift+A adds to it."));
      ImGui::SetNextItemWidth(availW);
      FieldWell::InputTextWithHint("##v3dsearch", T("Search"), sSearch, sizeof(sSearch));
      auto lower = [](std::string text)
      {
         std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return text;
      };
      const std::string search = lower(sSearch);
      const ImGuiIO& io = ImGui::GetIO();
      const float rowH = 24.0f;
      // Fills the tab below the search box, then scrolls.
      const int count = (int)scene->Items().size();
      const float step = rowH + ImGui::GetStyle().ItemSpacing.y;
      const int shown = std::min(count, std::max(2, (int)((roomH - 90.0f) / step)));
      const bool scrolls = count > shown;
      if (count > 0)
         ImGui::BeginChild("##v3dlist", ImVec2(availW, step * (float)shown));
      if (scrolls)
         availW -= ImGui::GetStyle().ScrollbarSize;
      std::vector<Item>& items = scene->Items();
      for (int row = 0; row < count; row++)
      {
         Item& it = items[row];
         if (!search.empty() && lower(ItemName(it)).find(search) == std::string::npos)
            continue;
         ImGui::PushID(it.id);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         ImGui::SetNextItemAllowOverlap();
         if (ImGui::Selectable("##row", Selected(it.id), ImGuiSelectableFlags_None, ImVec2(availW, rowH)))
         {
            const bool toggle = io.KeyCtrl || io.KeySuper;
            auto anchor = sSel.empty() ? items.end() : std::find_if(items.begin(), items.end(), [](const Item& x) { return x.id == sSel.back(); });
            if (io.KeyShift && anchor != items.end())
            {
               // The whole run from the active row, which stays where the click landed.
               const int from = (int)(anchor - items.begin());
               for (int i = std::min(from, row); i <= std::max(from, row); i++)
                  if (!Selected(items[i].id))
                     sSel.push_back(items[i].id);
               sSel.erase(std::find(sSel.begin(), sSel.end(), it.id));
               sSel.push_back(it.id);
            }
            else if (toggle && Selected(it.id))
               sSel.erase(std::find(sSel.begin(), sSel.end(), it.id));
            else if (toggle)
               sSel.push_back(it.id);
            else
               sSel = { it.id };
         }
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            StartRename(it);
         if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
         {
            if (!Selected(it.id))
               sSel = { it.id };
            sOpenMenu = true;
         }
         const bool missing = scene->ItemKind(it) == Kind::Missing;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImU32 text = ImGui::GetColorU32(missing || !it.visible ? ImGuiCol_TextDisabled : ImGuiCol_Text);
         const float midY = p.y + rowH * 0.5f;
         glyph::Draw(dl, ImVec2(p.x + 12.0f, midY), tok::icon_sm, text, KindGlyph(scene, it));
         const std::string name = ItemName(it);
         const std::string shown = it.group != 0 ? name + "  (" + T("Group") + " " + std::to_string(it.group) + ")" : name;
         if (sRename == it.id)
         {
            ImGui::SetCursorScreenPos(ImVec2(p.x + 24.0f, midY - ImGui::GetFrameHeight() * 0.5f));
            ImGui::SetNextItemWidth(availW - 72.0f);
            if (sRenameFocus)
               ImGui::SetKeyboardFocusHere();
            const bool enter = FieldWell::InputText("##rename", sRenameBuf, sizeof(sRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            if (!sRenameFocus && (enter || ImGui::IsItemDeactivated()))
            {
               if (name != sRenameBuf)
               {
                  PushUndoCheckpoint("Rename in 3D View");
                  it.name = sRenameBuf;
                  scene->Commit();
               }
               sRename = 0;
            }
            sRenameFocus = false;
         }
         else
         {
            dl->PushClipRect(p, ImVec2(p.x + availW - 44.0f, p.y + rowH), true);
            dl->AddText(ImVec2(p.x + 26.0f, midY - ImGui::GetTextLineHeight() * 0.5f), text, shown.c_str());
            dl->PopClipRect();
         }
         if (it.id == scene->activeCamera)
            glyph::Draw(dl, ImVec2(p.x + availW - 36.0f, midY), tok::icon_sm, ImGui::GetColorU32(AccentEmphasisSelected()), IconsInfinite::ViewCamera);
         ImGui::SetCursorScreenPos(ImVec2(p.x + availW - 24.0f, p.y));
         if (ImGui::InvisibleButton("##eye", ImVec2(24.0f, rowH)))
         {
            PushUndoCheckpoint("Show or hide in 3D View");
            it.visible = !it.visible;
            scene->Commit();
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", it.visible ? T("Hide (H)") : T("Show (Alt+H shows everything)"));
         glyph::Draw(dl, ImVec2(p.x + availW - 12.0f, midY), tok::icon_sm, text, it.visible ? IconsInfinite::Eye : IconsInfinite::EyeOff);
         ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
         ImGui::Dummy(ImVec2(0.0f, 0.0f));
         ImGui::PopID();
      }
      if (count > 0)
         ImGui::EndChild();
   }

   // What an item can point at: its own kind, or anything once its node is gone.
   std::vector<GraphNode*> SourceChoices(Scene3DNode* scene, const Item& it)
   {
      const Kind kind = scene->ItemKind(it);
      std::vector<GraphNode*> nodes;
      for (GraphNode& gn : gNodes)
      {
         const Kind k = Scene3DNode::KindOf(gn.node.get());
         if (k != Kind::Missing && (kind == Kind::Missing || k == kind) && gn.node.get() != scene)
            nodes.push_back(&gn);
      }
      return nodes;
   }

   void SetSource(Scene3DNode* scene, Item& it, uint64_t uid)
   {
      PushUndoCheckpoint("Source in 3D View");
      it.uid = uid;
      scene->Commit();
   }

   void PointAtCursor(Scene3DNode* scene, Item* item, float availW)
   {
      if (!ActionButton::Draw(L("Point at 3D Cursor"), ImVec2(availW, 0)))
         return;
      PushUndoCheckpoint("Aim in 3D View");
      scene->AimAt(*item, sSet->cursor);
      scene->Commit();
   }

   // The light that casts: the one chosen, else the first that can.
   const Item* ShadowCaster(Scene3DNode* scene)
   {
      if (const Item* chosen = scene->FindItem(sSet->shadowItem))
         return chosen;
      for (const Item& it : scene->Items())
      {
         GraphNode* gn = FindNodeByUid(it.uid);
         auto* light = gn != nullptr ? dynamic_cast<LightNode*>(gn->node.get()) : nullptr;
         if (light != nullptr && light->type != 1 && light->type != 3)
            return &it;
      }
      return nullptr;
   }

   void DrawLightCard(Scene3DNode* scene, Item* item, LightNode* light, float availW)
   {
      if (!Card(T("Light")))
         return;
      static const std::vector<std::string> kTypes = { "Directional", "Point", "Sun", "Ambient", "Spot", "Area" };
      std::vector<std::string> names;
      for (const std::string& n : kTypes)
         names.push_back(T(n.c_str()));
      ComboRow(T("Type"), &light->type, names, "Light type");
      ColorRow(T("Colour"), light->color, "Light colour");
      FloatRow(T("Strength"), &light->intensity, 0.0f, 20.0f, "%.2f");
      if (light->type != 3)
      {
         FloatRow(T("Orbit"), &light->azimuth, -180.0f, 180.0f, "%.1f");
         FloatRow(T("Elevation"), &light->elevation, -90.0f, 90.0f, "%.1f");
      }
      if (light->type == 1 || light->type == 4 || light->type == 5)
         FloatRow(T("Distance"), &light->distance, 0.05f, 100.0f, "%.2f");
      if (light->type == 4)
      {
         FloatRow(T("Cone"), &light->spotAngle, 1.0f, 89.0f, "%.1f");
         FloatRow(T("Blend"), &light->spotPenumbra, 0.0f, 1.0f, "%.2f");
      }
      if (light->type == 5)
      {
         FloatRow(T("Width"), &light->areaWidth, 0.05f, 20.0f, "%.2f");
         FloatRow(T("Height"), &light->areaHeight, 0.05f, 20.0f, "%.2f");
      }
      if (light->type != 1 && light->type != 3)
      {
         bool casts = ShadowCaster(scene) == item;
         if (BoolRow(T("Casts shadow"), &casts, "Shadow light"))
            sSet->shadowItem = casts ? item->id : 0;
         PointAtCursor(scene, item, availW);
      }
   }

   void DrawLitBy(Scene3DNode* scene, Item* item)
   {
      bool header = false;
      for (const Item& other : scene->Items())
      {
         if (scene->ItemKind(other) != Kind::Light)
            continue;
         if (!header)
            if (!Card(T("Lit by")))
               return;
         header = true;
         ImGui::PushID(other.id);
         auto found = std::find(item->unlit.begin(), item->unlit.end(), other.uid);
         bool lit = found == item->unlit.end();
         if (CheckBox::Box("##lit", &lit))
         {
            PushUndoCheckpoint("Light linking");
            if (lit)
               item->unlit.erase(found);
            else
               item->unlit.push_back(other.uid);
            scene->Commit();
         }
         ActionButton::RowNext();
         ImGui::TextUnformatted(ItemName(other).c_str());
         ImGui::PopID();
      }
   }

   float RenderAspect(Scene3DNode* scene)
   {
      return std::max(1.0f, scene->renderW) / std::max(1.0f, scene->renderH);
   }

   float FocalFromFov(float fov, float sensor, float aspect)
   {
      return sensor / (2.0f * std::tan(fov * 0.5f * kPi / 180.0f) * std::max(1.0f, aspect));
   }

   float FovFromFocal(float mm, float sensor, float aspect)
   {
      return 2.0f * std::atan(sensor / (2.0f * mm * std::max(1.0f, aspect))) * 180.0f / kPi;
   }

   // Blender's focal length holds when the frame or sensor changes; only the view angle moves.
   template <typename Change>
   void KeepingLenses(Scene3DNode* scene, Change&& change)
   {
      std::vector<std::pair<CameraNode*, float>> lenses;
      for (const Item& it : scene->Items())
      {
         GraphNode* gn = FindNodeByUid(it.uid);
         if (auto* cam = gn != nullptr ? dynamic_cast<CameraNode*>(gn->node.get()) : nullptr)
            lenses.push_back({ cam, FocalFromFov(cam->fov, cam->sensorWidth, RenderAspect(scene)) });
      }
      change();
      for (auto& lens : lenses)
         lens.first->fov = FovFromFocal(lens.second, lens.first->sensorWidth, RenderAspect(scene));
   }

   void DrawCameraCards(Scene3DNode* scene, Item* item, CameraNode* cam, float availW)
   {
      if (Card(T("View")))
      {
         FloatRow(T("Distance"), &cam->distance, 0.05f, 100.0f, "%.2f");
         FloatRow(T("Orbit"), &cam->azimuth, -180.0f, 180.0f, "%.1f");
         FloatRow(T("Elevation"), &cam->elevation, -90.0f, 90.0f, "%.1f");
         FloatRow(T("Roll"), &cam->roll, -180.0f, 180.0f, "%.1f");
         FloatRow(T("Target X"), &cam->targetX, -50.0f, 50.0f, "%.2f");
         FloatRow(T("Target Y"), &cam->targetY, -50.0f, 50.0f, "%.2f");
         FloatRow(T("Target Z"), &cam->targetZ, -50.0f, 50.0f, "%.2f");
      }
      if (Card(T("Lens")))
      {
         static const std::vector<std::string> kTypes = { "Perspective", "Orthographic" };
         std::vector<std::string> names;
         for (const std::string& n : kTypes)
            names.push_back(T(n.c_str()));
         ComboRow(T("Type"), &cam->projection, names);
         if (cam->projection == 0)
         {
            if (sLensUnit == 0)
            {
               float mm = FocalFromFov(cam->fov, cam->sensorWidth, RenderAspect(scene));
               if (FloatRow(T("Focal length"), &mm, 1.0f, 300.0f, "%.0f mm"))
                  cam->fov = FovFromFocal(mm, cam->sensorWidth, RenderAspect(scene));
            }
            else
               FloatRow(T("Field of view"), &cam->fov, 1.0f, 170.0f, "%.1f");
            static const std::vector<std::string> kUnits = { "Millimeters", "Field of view" };
            std::vector<std::string> units;
            for (const std::string& n : kUnits)
               units.push_back(T(n.c_str()));
            ComboRow(T("Lens unit"), &sLensUnit, units);
         }
         else
            FloatRow(T("Scale"), &cam->orthoHeight, 0.01f, 100.0f, "%.2f");
         FloatRow(T("Shift X"), &cam->shiftX, -2.0f, 2.0f, "%.3f");
         FloatRow(T("Shift Y"), &cam->shiftY, -2.0f, 2.0f, "%.3f");
         FloatRow(T("Clip start"), &cam->nearPlane, 0.001f, 10.0f, "%.3f");
         FloatRow(T("Clip end"), &cam->farPlane, 1.0f, 10000.0f, "%.0f");
      }
      if (Card(T("Camera")))
      {
         float sensor = cam->sensorWidth;
         if (FloatRow(T("Sensor"), &sensor, 1.0f, 100.0f, "%.0f mm"))
         {
            const float mm = FocalFromFov(cam->fov, cam->sensorWidth, RenderAspect(scene));
            cam->sensorWidth = sensor;
            cam->fov = FovFromFocal(mm, sensor, RenderAspect(scene));
         }
         if (scene->activeCamera != item->id)
         {
            if (ActionButton::Draw(L("Render through this camera"), ImVec2(availW, 0)))
            {
               PushUndoCheckpoint("Set 3D View camera");
               scene->activeCamera = item->id;
               scene->Commit();
            }
         }
         else if (ActionButton::Draw(L("Move camera to view"), ImVec2(availW, 0)))
            CameraToView(scene);
         PointAtCursor(scene, item, availW);
      }
      Render3DNode& look = scene->Look();
      Scene3DNode::Settings& set = *sSet;
      bool dof = set.fStop > 0.0f || look.dofAperture > 0.0f;
      const bool was = dof;
      const bool open = Card(T("Depth of Field"), &dof);
      if (dof != was)
      {
         set.fStop = dof ? 2.8f : 0.0f;
         look.dofAperture = 0.0f;
      }
      if (open)
      {
         ImGui::BeginDisabled(!dof);
         std::vector<std::string> names = { T("Distance") };
         std::vector<int> ids = { 0 };
         for (const Item& other : scene->Items())
            if (other.id != item->id)
            {
               names.push_back(ItemName(other));
               ids.push_back(other.id);
            }
         int pick = std::max(0, (int)(std::find(ids.begin(), ids.end(), scene->focusItem) - ids.begin()));
         if (pick >= (int)ids.size())
            pick = 0;
         if (ComboRow(T("Focus on"), &pick, names))
         {
            scene->focusItem = ids[pick];
            scene->Commit();
         }
         ImGui::BeginDisabled(scene->focusItem != 0);
         FloatRow(T("Distance"), &look.focusDistance, 0.05f, 100.0f, "%.2f");
         ImGui::EndDisabled();
         float stop = set.fStop > 0.0f ? set.fStop : 2.8f;
         if (FloatRow(T("F-Stop"), &stop, 0.7f, 128.0f, "%.1f"))
            set.fStop = stop;
         ImGui::EndDisabled();
      }
   }

   void DrawObjectTab(Scene3DNode* scene, float availW)
   {
      Item* item = sSel.size() == 1 ? scene->FindItem(sSel[0]) : nullptr;
      if (item == nullptr)
      {
         Hint(T("Select one object."));
         return;
      }
      if (Card(T("Source")))
      {
         const std::vector<GraphNode*> nodes = SourceChoices(scene, *item);
         GraphNode* current = FindNodeByUid(item->uid);
         FieldLabel(T("Node"));
         ImGui::SetNextItemWidth(sFieldW);
         auto title = [](const GraphNode& gn) { return gn.typeName + " " + std::to_string(gn.index); };
         if (MenuParts::BeginCombo("##v3dsource", current != nullptr ? title(*current).c_str() : T("source missing")))
         {
            for (GraphNode* gn : nodes)
               if (MenuParts::Choice(title(*gn).c_str(), gn == current) && gn != current)
                  SetSource(scene, *item, gn->uid);
            if (nodes.empty())
               ImGui::TextDisabled("%s", T("No fitting nodes in this patch"));
            MenuParts::EndCombo();
         }
         const bool picking = sPickItem == item->id;
         if (ActionButton::Draw(picking ? T("Click a node on the canvas...") : current != nullptr ? T("Assign Different Node...") : T("Assign Node..."),
                                ImVec2(-1.0f, 0.0f), picking ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
            sPickItem = picking ? -1 : item->id;
      }
      if (Card(T("Transform")))
      {
         auto field = [&](const char* label, float* v, float speed)
         {
            FieldLabel(label);
            ImGui::SetNextItemWidth(sFieldW);
            const bool changed = ImGui::DragFloat3(FormParts::Id(label).c_str(), v, speed, 0.0f, 0.0f, "%.2f");
            Undoable("Edit in 3D View");
            if (changed)
               scene->Commit();
         };
         FieldWell::PushStyle();
         field(T("Location"), item->pos, 0.01f);
         field(T("Rotation"), item->rot, 0.5f);
         field(T("Scale"), item->scale, 0.01f);
         FieldWell::PopStyle();
      }
      if (Card(T("Visibility")))
         if (BoolRow(T("Show in renders"), &item->render, "Show in renders"))
            scene->Commit();
      GraphNode* gn = FindNodeByUid(item->uid);
      if (scene->ItemKind(*item) == Kind::Geometry)
         DrawLitBy(scene, item);
      if (gn != nullptr && ActionButton::Draw(L("Go to node"), ImVec2(availW, 0)))
      {
         gRequestSelectNodeIndex = gn->index;
         gRequestFitViewNodeIndex = gn->index;
      }
   }

   void DrawDataTab(Scene3DNode* scene, float availW)
   {
      Item* item = sSel.size() == 1 ? scene->FindItem(sSel[0]) : nullptr;
      GraphNode* gn = item != nullptr ? FindNodeByUid(item->uid) : nullptr;
      INode* node = gn != nullptr ? gn->node.get() : nullptr;
      if (auto* light = dynamic_cast<LightNode*>(node))
         DrawLightCard(scene, item, light, availW);
      else if (auto* cam = dynamic_cast<CameraNode*>(node))
         DrawCameraCards(scene, item, cam, availW);
      else
         Hint(T("Select a camera or a light."));
   }

   void DrawWorldTab(Scene3DNode* scene, float availW)
   {
      GraphNode* world = scene->worldUid != 0 ? FindNodeByUid(scene->worldUid) : nullptr;
      auto* env = world != nullptr ? dynamic_cast<EnvironmentNode*>(world->node.get()) : nullptr;
      if (Card(T("Surface")))
      {
         int mode = world != nullptr ? (env != nullptr ? 2 : 3) : scene->worldColorOn ? 1 : 0;
         const int was = mode;
         std::vector<std::string> names = { T("None, transparent"), T("Background colour"), T("Environment texture") };
         if (was == 3)
            names.push_back(T("Picture"));
         if (ComboRow(T("Surface"), &mode, names, "3D View world") && mode != was)
         {
            if (mode < 2)
            {
               scene->worldUid = 0;
               scene->worldColorOn = mode == 1;
               scene->Commit();
            }
            else
               sPending.what = Pending::Hdri;
         }
         if (mode == 1)
         {
            if (ColorRow(T("Color"), scene->worldColor, "3D View world"))
               scene->Commit();
            if (FloatRow(T("Strength"), &scene->worldStrength, 0.0f, 10.0f, "%.3f", "3D View world"))
               scene->Commit();
         }
         if (env == nullptr && ActionButton::Draw(L("Load HDRI..."), ImVec2(availW, 0)))
            sPending.what = Pending::PickHdri;
         if (world != nullptr && env == nullptr)
         {
            FieldLabel(T("Source"));
            ImGui::TextUnformatted(world->typeName.c_str());
         }
         if (env != nullptr)
         {
            if (ActionButton::Draw(L("Load HDRI..."), ImVec2(availW, 0)))
               sPending.what = Pending::PickHdri;
            FloatRow(T("Rotation"), &env->rotation, -180.0f, 180.0f, "%.0f");
            FloatRow(T("Strength"), &env->intensity, 0.0f, 10.0f, "%.3f");
         }
      }
      Render3DNode& look = scene->Look();
      if (Card(T("Light")))
      {
         ColorRow(T("Ambient"), look.ambientColor);
         FloatRow(T("Reflections"), &look.envIntensity, 0.0f, 3.0f, "%.2f");
      }
   }

   std::string sLastRender;     // where the last render went, shown under the buttons
   std::string sStillPath;      // a picture waiting for the Output's first cook

   bool EndsWith(const std::string& text, const char* tail)
   {
      const size_t n = std::strlen(tail);
      return text.size() >= n && text.compare(text.size() - n, n, tail) == 0;
   }

   void StartRender(Scene3DNode* scene)
   {
      if (gOfflineRender.active || scene->activeCamera == 0)
         return;
      const std::string path = Platform::SaveRenderDialog("infinite_render.mp4", true);
      if (path.empty())
         return;
      if (OutputNode* out = View3DEnsureOutput())
      {
         // A hidden Output never draws the body that fills these in.
         out->recordVideoPath = path;
         out->videoFormat = EndsWith(path, ".mov") ? 1 : 0;
         out->offlineDurationSeconds = (int)sSet->seconds;
         out->offlineFps = (int)sSet->fps;
         sLastRender = path;
         StartOfflineRenderSession(out);
      }
   }

   // One picture through the active camera, once the Output has one to give.
   void RenderStill(Scene3DNode* scene)
   {
      if (gOfflineRender.active || scene->activeCamera == 0)
         return;
      const std::string path = Platform::SaveRenderDialog("infinite_render.png", false);
      if (path.empty())
         return;
      if (OutputNode* out = View3DEnsureOutput())
      {
         out->exportImagePath = path;
         out->imageFormat = EndsWith(path, ".jpg") || EndsWith(path, ".jpeg") ? 1 : 0;
         sStillPath = path;
      }
   }

   void PumpStill()
   {
      OutputNode* out = sStillPath.empty() ? nullptr : FindOutput();
      if (out == nullptr || out->GetOutputWidth() <= 0)
         return;
      ExportImage(out, sStillPath);
      sLastRender = sStillPath;
      sStillPath.clear();
   }

   void DrawRenderResult(float availW)
   {
      if (!sStillPath.empty())
         Hint(T("Saving image..."));
      OutputNode* out = FindOutput();
      if (out != nullptr && !gOfflineRender.active && !out->RecordStatus().empty())
         Hint(out->RecordStatus().c_str());
      std::error_code ec;
      if (sLastRender.empty() || !std::filesystem::exists(sLastRender, ec))
         return;
      Hint(sLastRender.c_str());
      if (ActionButton::Draw(L("Reveal File"), ImVec2(availW, 0)))
         Platform::RevealInFileManager(sLastRender);
   }

   void DrawRenderTab(Scene3DNode* scene)
   {
      Render3DNode& look = scene->Look();
      if (Card(T("Sampling")))
      {
         ComboRow(T("Viewport"), &look.samples, Render3DNode::SampleNames());
         FieldLabel(T("Render"));
         ImGui::SetNextItemWidth(sFieldW);
         FieldWell::Slider("##v3dsuper", &look.supersample, 1, 64, "%d");
         Undoable("Edit in 3D View");
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Samples per frame when rendering out."));
      }
      if (Card(T("Shadows"), &look.shadowsEnabled))
      {
         ImGui::BeginDisabled(!look.shadowsEnabled);
         ComboRow(T("Quality"), &look.shadowQuality, Render3DNode::ShadowQualityNames());
         FloatRow(T("Strength"), &look.shadowStrength, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Softness"), &look.shadowSoftness, 0.0f, 4.0f, "%.2f");
         FloatRow(T("Bias"), &look.shadowBias, 0.0002f, 0.02f, "%.4f");
         ImGui::EndDisabled();
         Hint(T("Cast by one light, any but a point. Pick it in the light's settings."));
      }
      if (Card(T("Ambient Occlusion")))
      {
         FloatRow(T("Strength"), &look.aoStrength, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Distance"), &look.aoRadius, 0.05f, 3.0f, "%.2f");
      }
      if (Card(T("Bloom")))
      {
         FloatRow(T("Strength"), &look.bloom, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Threshold"), &look.bloomThreshold, 0.5f, 8.0f, "%.2f");
         FloatRow(T("Radius"), &look.bloomRadius, 0.0f, 1.0f, "%.2f");
      }
      if (Card(T("Film")))
         BoolRow(T("Transparent"), &sSet->transparent);
      if (Card(T("Color Management")))
      {
         ComboRow(T("View"), &look.tonemap, Render3DNode::TonemapNames());
         FloatRow(T("Exposure"), &look.exposure, 0.1f, 4.0f, "%.2f");
      }
   }

   void DrawOutputTab(Scene3DNode* scene, float availW)
   {
      PumpStill();
      Render3DNode& look = scene->Look();
      if (Card(T("Format")))
      {
         int w = (int)scene->renderW, h = (int)scene->renderH;
         if (IntInputRow(T("Resolution X"), &w))
            KeepingLenses(scene, [&] { scene->renderW = (float)std::clamp(w, 16, 8192); });
         if (IntInputRow(T("Y"), &h))
            KeepingLenses(scene, [&] { scene->renderH = (float)std::clamp(h, 16, 8192); });
         FloatRow(T("Resolution %"), &sSet->percent, 1.0f, 200.0f, "%.0f%%");
         FieldLabel(T("Final size"));
         char finalSize[32];
         snprintf(finalSize, sizeof(finalSize), "%d x %d", (int)(scene->renderW * sSet->percent / 100.0f), (int)(scene->renderH * sSet->percent / 100.0f));
         ImGui::TextUnformatted(finalSize);
         static const std::vector<int> kRates = { 24, 25, 30, 48, 50, 60, 120 };
         static const std::vector<std::string> kRateNames = { "24 fps", "25 fps", "30 fps", "48 fps", "50 fps", "60 fps", "120 fps" };
         int rate = (int)(std::find(kRates.begin(), kRates.end(), (int)sSet->fps) - kRates.begin());
         if (ComboRow(T("Frame rate"), &rate, kRateNames))
            sSet->fps = (float)kRates[rate];
      }
      if (Card(T("Frame Range")))
      {
         int seconds = (int)sSet->seconds;
         if (IntInputRow(T("Seconds"), &seconds))
            sSet->seconds = (float)std::clamp(seconds, 1, 3600);
         FieldLabel(T("Frames"));
         ImGui::Text("%d", (int)(sSet->seconds * sSet->fps));
      }
      if (Card(T("Output")))
      {
         static const std::vector<std::string> kDepths = { "8", "16" };
         int depth = look.floatOutput ? 1 : 0;
         if (ComboRow(T("Color depth"), &depth, kDepths))
            look.floatOutput = depth == 1;
         ComboRow(T("Pass"), &look.renderPass, Render3DNode::RenderPassNames());
         ComboRow(T("Projection"), &look.projection, Render3DNode::ProjectionNames());
         if (look.projection == 4)
         {
            FloatRow(T("Dome angle"), &look.domeAngle, 90.0f, 360.0f, "%.0f");
            FloatRow(T("Dome tilt"), &look.domeTilt, 0.0f, 90.0f, "%.0f");
         }
      }
      ImGui::BeginDisabled(gOfflineRender.active || scene->activeCamera == 0);
      if (ActionButton::Draw(L("Render Image"), ImVec2(availW, 0)))
         RenderStill(scene);
      if (ActionButton::Draw(L("Render Animation"), ImVec2(availW, 0), ActionButton::Kind::Primary))
         StartRender(scene);
      ImGui::EndDisabled();
      if (scene->activeCamera == 0)
         Hint(T("Add a camera to render."));
      DrawRenderResult(availW);
   }

   const std::vector<std::string>& OrientationNames()
   {
      static const std::vector<std::string> kNames = { T("Global"), T("Local"), T("View") };
      return kNames;
   }

   const std::vector<std::string>& PivotNames()
   {
      static const std::vector<std::string> kNames = { T("Median Point"), T("Individual Origins"), T("Active Element"), T("3D Cursor") };
      return kNames;
   }

   void DrawViewTab(Scene3DNode* scene, float availW)
   {
      Scene3DNode::Settings& set = *sSet;
      if (Card(T("Transform")))
      {
         ComboRow(T("Orientation"), &set.orientation, OrientationNames());
         ComboRow(T("Pivot"), &set.pivot, PivotNames());
      }
      if (Card(T("Snap")))
      {
         BoolRow(T("On"), &set.snap);
         FloatRow(T("Move"), &set.snapMove, 0.01f, 5.0f, "%.2f");
         FloatRow(T("Rotate"), &set.snapRot, 1.0f, 90.0f, "%.0f");
         FloatRow(T("Scale"), &set.snapScale, 0.01f, 1.0f, "%.2f");
      }
      if (Card(T("3D Cursor")))
      {
         FieldLabel(T("Location"));
         ImGui::SetNextItemWidth(sFieldW);
         FieldWell::PushStyle();
         ImGui::DragFloat3("##v3dcursor", set.cursor, 0.01f, 0.0f, 0.0f, "%.2f");
         FieldWell::PopStyle();
         Undoable("Move 3D cursor");
         ImGui::BeginDisabled(sSel.empty());
         if (ActionButton::Draw(L("Cursor to Selected"), ImVec2(availW, 0)))
            CursorToSelected(scene);
         if (ActionButton::Draw(L("Selection to Cursor"), ImVec2(availW, 0)))
            SelectionToCursor(scene);
         ImGui::EndDisabled();
      }
      if (Card(T("Navigation")))
      {
         BoolRow(T("Zoom to mouse"), &set.zoomToMouse);
         BoolRow(T("Orbit around selection"), &set.orbitSelection);
      }
      if (Card(T("Passepartout")))
         FloatRow(T("Opacity"), &set.passAlpha, 0.0f, 1.0f, "%.2f");
      if (Card(T("Safe Areas"), &set.safe))
      {
         FloatRow(T("Title"), &set.safeTitle, 0.0f, 0.3f, "%.3f");
         FloatRow(T("Action"), &set.safeAction, 0.0f, 0.3f, "%.3f");
      }
      if (Card(T("Composition Guides")))
      {
         BoolRow(T("Thirds"), &set.thirds);
         BoolRow(T("Center"), &set.center);
         BoolRow(T("Golden"), &set.golden);
      }
   }

   // A child whose first card touches its top and sides, so the card lines up with the rail.
   void BeginCardColumn(const char* id, ImGuiChildFlags flags)
   {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SectionCard::kPad, 0.0f));
      ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | flags);
      ImGui::PopStyleVar();
      SectionCard::BeginWindow();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
   }

   void EndCardColumn()
   {
      SectionCard::EndWindow();
      ImGui::EndChild();
   }

   void DrawSide(Scene3DNode* scene, float panelW)
   {
      PushDockedPanelStyle(/*isChild=*/true);
      const ImVec2 savedPad = ImGui::GetStyle().WindowPadding;
      ImGui::GetStyle().WindowPadding = ImVec2(tok::space_3, tok::space_3);
      ImGui::BeginChild("##v3dside", ImVec2(panelW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::GetStyle().WindowPadding = savedPad;
      PopDockedPanelStyle();
      float availW = ImGui::GetContentRegionAvail().x;
      SectionCard::Title(T("3D View"));
      FormParts::Inline(availW - 16.0f + ImGui::GetStyle().WindowPadding.x);
      {
         const float sz = 16.0f;
         if (ImGui::InvisibleButton("##closev3dside", ImVec2(sz, sz)))
            sSideOpen = false;
         const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
         glyph::DrawX(ImGui::GetWindowDrawList(), ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), sz * 0.65f,
                      ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled));
      }

      // The tab strip, Blender's order after the scene list: what renders, where it goes, the world, then the selection.
      Item* item = sSel.size() == 1 ? scene->FindItem(sSel[0]) : nullptr;
      const char* dataGlyph = item != nullptr ? KindGlyph(scene, *item) : IconsInfinite::ViewCamera;
      const Kind kind = item != nullptr ? scene->ItemKind(*item) : Kind::Missing;
      const bool hasData = kind == Kind::Camera || kind == Kind::Light;
      // Picking a camera or a light opens its settings, the likeliest reason to pick it, unless the list is open.
      static int sPicked = 0;
      if ((item != nullptr ? item->id : 0) != sPicked)
      {
         sPicked = item != nullptr ? item->id : 0;
         if (hasData && sTab != kSceneTab)
            sTab = 4;
      }
      if (sTab == 4 && !hasData)
         sTab = item != nullptr ? 3 : 0;
      ImGui::BeginGroup();
      const struct { int tab; const char* id; const char* icon; const char* tip; bool show; } kTabs[7] = {
         { kSceneTab, "##t6", IconsInfinite::Arrange, T("Scene"), true },
         { 0, "##t0", IconsInfinite::ShadeRender, T("Render"), true },
         { 1, "##t1", IconsInfinite::Folder, T("Output"), true },
         { 2, "##t2", IconsInfinite::World, T("World"), true },
         { 3, "##t3", IconsInfinite::Cube, T("Object"), item != nullptr },
         { 4, "##t4", dataGlyph, kind == Kind::Light ? T("Light") : T("Camera"), hasData },
         { 5, "##t5", IconsInfinite::Settings, T("Viewport"), true },
      };
      for (const auto& tab : kTabs)
         if (tab.show && IconChip(tab.id, tab.icon, sTab == tab.tab, tab.tip))
            sTab = tab.tab;
      ImGui::EndGroup();
      ActionButton::RowNext();
      BeginCardColumn("##v3dprops", ImGuiChildFlags_None);
      availW = ImGui::GetContentRegionAvail().x;
      sFieldW = std::max(70.0f, availW - kFieldLabelW);
      if (sTab == kSceneTab) DrawOutliner(scene, availW, ImGui::GetContentRegionAvail().y);
      else if (sTab == 0) DrawRenderTab(scene);
      else if (sTab == 1) DrawOutputTab(scene, availW);
      else if (sTab == 2) DrawWorldTab(scene, availW);
      else if (sTab == 3) DrawObjectTab(scene, availW);
      else if (sTab == 4) DrawDataTab(scene, availW);
      else DrawViewTab(scene, availW);
      EndCardColumn();
      ImGui::EndChild();
   }

   void DrawAddItems(Scene3DNode* scene)
   {
      std::vector<const GraphNode*> nodes;
      for (const GraphNode& gn : gNodes)
      {
         bool inScene = false;
         for (const Item& it : scene->Items())
            inScene = inScene || it.uid == gn.uid;
         if (!inScene && View3DCanAdd(gn) && dynamic_cast<EnvironmentNode*>(gn.node.get()) == nullptr)
            nodes.push_back(&gn);
      }
      if (MenuParts::SubMenu(T("From canvas"), !nodes.empty()))
      {
         for (const GraphNode* gn : nodes)
            if (MenuParts::Item((gn->typeName + " " + std::to_string(gn->index)).c_str()))
            {
               sPending.what = Pending::Node;
               sPending.uid = gn->uid;
            }
         ImGui::EndMenu();
      }
      MenuParts::Separator();
      if (MenuParts::Item(T("Camera")))
         sPending.what = Pending::Camera;
      MenuParts::Separator();
      const struct { const char* name; int type; } kLights[4] = {
         { "Point Light", 1 }, { "Sun Light", 2 }, { "Spot Light", 4 }, { "Area Light", 5 }
      };
      for (const auto& l : kLights)
         if (MenuParts::Item(T(l.name)))
         {
            sPending.what = Pending::Light;
            sPending.lightType = l.type;
         }
      MenuParts::Separator();
      if (MenuParts::Item(T("HDRI...")))
         sPending.what = Pending::PickHdri;
      MenuParts::Separator();
      if (MenuParts::Item(T("New scene")))
         sPending.what = Pending::Scene;
      if (MenuParts::Item(T("Duplicate scene"), MODKEY "+D"))
         sPending.what = Pending::DuplicateScene;
      if (MenuParts::Item(T("Copy scene"), MODKEY "+C"))
         sCopiedScene = scene->SceneText();
      if (MenuParts::Item(T("Paste scene"), MODKEY "+V", false, !sCopiedScene.empty()))
         sPending.what = Pending::PasteScene;
      if (MenuParts::Item(T("Delete scene")))
         sPending.what = Pending::DeleteScene;
   }

   void DrawPopups(Scene3DNode* scene)
   {
      if (sOpenAdd)
         ImGui::OpenPopup("##v3daddmenu");
      if (sOpenMenu)
         ImGui::OpenPopup("##v3dcontext");
      sOpenAdd = sOpenMenu = false;
      if (sOpenPivot)
         ImGui::OpenPopup("##v3dpivot");
      if (sOpenOrient)
         ImGui::OpenPopup("##v3dorient");
      sOpenPivot = sOpenOrient = false;
      auto choose = [](const std::vector<std::string>& names, int* v, const char* undo)
      {
         for (int i = 0; i < (int)names.size(); i++)
            if (MenuParts::Item(names[i].c_str(), nullptr, i == *v) && i != *v)
            {
               PushUndoCheckpoint(undo);
               *v = i;
            }
      };
      if (MenuParts::BeginPopup("##v3dpivot"))
      {
         choose(PivotNames(), &sSet->pivot, "Pivot in 3D View");
         MenuParts::EndPopup();
      }
      if (MenuParts::BeginPopup("##v3dorient"))
      {
         choose(OrientationNames(), &sSet->orientation, "Orientation in 3D View");
         MenuParts::EndPopup();
      }
      if (MenuParts::BeginPopup("##v3daddmenu"))
      {
         DrawAddItems(scene);
         MenuParts::EndPopup();
      }
      if (MenuParts::BeginPopup("##v3dcontext"))
      {
         const bool any = !sSel.empty();
         if (MenuParts::SubMenu(T("Add"), true))
         {
            DrawAddItems(scene);
            ImGui::EndMenu();
         }
         Item* one = sSel.size() == 1 ? scene->FindItem(sSel[0]) : nullptr;
         if (MenuParts::SubMenu(T("Source"), one != nullptr))
         {
            for (GraphNode* gn : SourceChoices(scene, *one))
               if (MenuParts::Item((gn->typeName + " " + std::to_string(gn->index)).c_str(), nullptr, gn->uid == one->uid) && gn->uid != one->uid)
                  SetSource(scene, *one, gn->uid);
            ImGui::EndMenu();
         }
         MenuParts::Separator();
         if (MenuParts::Item(T("Group"), MODKEY "+G", false, sSel.size() > 1)) GroupSelected(scene, true);
         if (MenuParts::Item(T("Ungroup"), MODKEY "+Alt+G", false, any)) GroupSelected(scene, false);
         MenuParts::Separator();
         if (MenuParts::Item(T("Frame"), "F", false, any)) FrameItems(scene, false);
         if (MenuParts::Item(T("Frame all"), "Home")) FrameItems(scene, true);
         MenuParts::Separator();
         if (MenuParts::Item(T("Duplicate"), "Shift+D", false, any)) DuplicateSelected(scene, ImGui::GetIO().MousePos);
         if (MenuParts::Item(T("Hide"), "H", false, any)) HideSelected(scene);
         if (MenuParts::Item(T("Remove from scene"), "X", false, any)) DeleteSelected(scene);
         if (MenuParts::Item(T("Rename"), "F2", false, sSel.size() == 1))
            if (const Item* it = scene->FindItem(sSel.back()))
               StartRename(*it);
         MenuParts::Separator();
         if (MenuParts::Item(T("Cursor to Selected"), nullptr, false, any)) CursorToSelected(scene);
         if (MenuParts::Item(T("Selection to Cursor"), nullptr, false, any)) SelectionToCursor(scene);
         MenuParts::Separator();
         if (sSel.size() == 1)
            if (MenuParts::Item(T("Go to node")))
               if (const Item* it = scene->FindItem(sSel[0]))
                  if (GraphNode* gn = FindNodeByUid(it->uid))
                  {
                     gRequestSelectNodeIndex = gn->index;
                     gRequestFitViewNodeIndex = gn->index;
                  }
         MenuParts::EndPopup();
      }
   }

   ImVec2 SpawnSpot(Scene3DNode* scene)
   {
      GraphNode* self = SceneGraphNode();
      ImVec2 at = self != nullptr ? NodePos(*self) : ImVec2(0, 0);
      at.x -= 360.0f;
      at.y += 200.0f * (float)scene->Items().size();
      return at;
   }

   // Lights and worlds can't be judged in Solid, so asking for one shows it.
   void ShowLighting()
   {
      if (sShading != Shading::Final)
         sShading = Shading::Final;
   }

   void AddCameraNow(Scene3DNode* scene)
   {
      PushUndoCheckpoint("Add camera to 3D View");
      const ImVec2 at = SpawnSpot(scene);
      GraphNode* gn = SpawnNode("Camera", "3D", at.x, at.y);
      if (gn == nullptr)
         return;
      auto* cam = dynamic_cast<CameraNode*>(gn->node.get());
      cam->fov = kFov;
      cam->farPlane = 1000.0f;
      Item& it = scene->AddItem(gn->uid);
      const int id = it.id;
      scene->AlignToView(it, OrbitView());
      if (scene->activeCamera == 0)
         scene->activeCamera = id;
      scene->Commit();
      sSel = { id };
      sCameraView = scene->activeCamera == id;
   }

   void AddLightNow(Scene3DNode* scene, int type)
   {
      PushUndoCheckpoint("Add light to 3D View");
      const ImVec2 at = SpawnSpot(scene);
      GraphNode* gn = SpawnNode("Light", "3D", at.x, at.y);
      if (gn == nullptr)
         return;
      dynamic_cast<LightNode*>(gn->node.get())->type = type;
      sSel = { scene->AddItem(gn->uid).id };
      scene->Commit();
      ShowLighting();
   }

   void ChooseHdri(Scene3DNode* scene, bool pick)
   {
      GraphNode* world = scene->worldUid != 0 ? FindNodeByUid(scene->worldUid) : nullptr;
      if (world == nullptr || dynamic_cast<EnvironmentNode*>(world->node.get()) == nullptr)
      {
         world = nullptr;
         for (GraphNode& gn : gNodes)
            if (!pick && dynamic_cast<EnvironmentNode*>(gn.node.get()) != nullptr)
               world = &gn;
      }
      PushUndoCheckpoint("3D View world");
      const bool made = world == nullptr;
      if (made)
      {
         const ImVec2 at = SpawnSpot(scene);
         world = SpawnNode("HDRI", "3D", at.x, at.y);
      }
      if (world == nullptr)
         return;
      auto* env = dynamic_cast<EnvironmentNode*>(world->node.get());
      const int index = world->index;
      const uint64_t uid = world->uid;
      const bool ask = pick || !env->HasImage();
      if (ask && !env->LoadViaDialog())
      {
         if (made)
            RemoveNodeByIndex(index);
         return;
      }
      scene->worldUid = uid;
      scene->worldColorOn = false;
      scene->Commit();
      ShowLighting();
   }

   // A scene is a 3D View node, so it saves, renders and has an Output like the first one.
   void AddSceneNow(const std::string& text, const char* undo)
   {
      GraphNode* self = SceneGraphNode();
      const ImVec2 at = self != nullptr ? NodePos(*self) : ImVec2(0, 0);
      PushUndoCheckpoint(undo);
      GraphNode* made = SpawnNode("3D View", "3D", at.x, at.y + 360.0f);
      auto* fresh = made != nullptr ? dynamic_cast<Scene3DNode*>(made->node.get()) : nullptr;
      if (fresh == nullptr)
         return;
      fresh->SetSceneText(text);
      fresh->Items();
      fresh->panelOnly = true;
      fresh->Commit();
      made->hiddenFromCanvas = true;
      sSceneUid = made->uid;
      sSel.clear();
   }

   void ConvertScene(Scene3DNode* scene)
   {
      GraphNode* self = SceneGraphNode();
      if (self == nullptr)
         return;
      PushUndoCheckpoint("Convert scene to node");
      scene->panelOnly = false;
      scene->Commit();
      self->hiddenFromCanvas = false;
      gRequestSelectNodeIndex = self->index;
      gRequestFitViewNodeIndex = self->index;
   }

   void DeleteScene(Scene3DNode* scene)
   {
      PushUndoCheckpoint("Delete scene");
      std::vector<int> nodes;
      for (GraphNode& gn : gNodes)
         if (auto* out = dynamic_cast<OutputNode*>(gn.node.get()))
            if (scene->panelOnly && out->Input().GetSource() == scene)
               nodes.push_back(gn.index);
      if (const GraphNode* self = SceneGraphNode())
         nodes.push_back(self->index);
      sSceneUid = 0;
      sSel.clear();
      for (int index : nodes)
         RemoveNodeByIndex(index);
   }

   void RunPending(Scene3DNode* scene)
   {
      const Pending todo = sPending;
      sPending = Pending();
      if (todo.what == Pending::Camera)
         AddCameraNow(scene);
      else if (todo.what == Pending::Light)
         AddLightNow(scene, todo.lightType);
      else if (todo.what == Pending::Hdri || todo.what == Pending::PickHdri)
         ChooseHdri(scene, todo.what == Pending::PickHdri);
      else if (todo.what == Pending::Scene)
         AddSceneNow("", "New scene in 3D View");
      else if (todo.what == Pending::DuplicateScene)
         AddSceneNow(scene->SceneText(), "Duplicate scene");
      else if (todo.what == Pending::PasteScene)
         AddSceneNow(sCopiedScene, "Paste scene");
      else if (todo.what == Pending::ConvertScene)
         ConvertScene(scene);
      else if (todo.what == Pending::DeleteScene)
         DeleteScene(scene);
      else if (todo.what == Pending::Node)
         if (GraphNode* gn = FindNodeByUid(todo.uid))
            View3DAddNode(*gn);
   }

   void DrawPanel()
   {
      FollowCanvasSelection();
      Scene3DNode* scene = FindScene();
      if (scene == nullptr)
      {
         // The first scene is there from the start, so the toolbar and settings always show.
         if (GraphNode* made = SpawnNode("3D View", "3D", 0.0f, 0.0f))
            if ((scene = dynamic_cast<Scene3DNode*>(made->node.get())) != nullptr)
            {
               scene->panelOnly = true;
               scene->Commit();
               made->hiddenFromCanvas = true;
               sSceneUid = made->uid;
            }
      }
      sSet = scene != nullptr ? &scene->settings : &sNoScene;
      if (scene == nullptr)
      {
         gView3DClaimedKeys = false;
         EmptyState::DrawInWindow(T("Nothing in the 3D View yet"), T("Right-click any 3D node and choose Add to 3D View."));
         return;
      }
      std::vector<int> live;
      for (int id : sSel)
         if (scene->FindItem(id) != nullptr)
            live.push_back(id);
      sSel.swap(live);
      if (scene->FindItem(sPickItem) == nullptr)
         sPickItem = -1;
      if (sPickedUid != 0 && sPickItem >= 0)
      {
         Item* item = scene->FindItem(sPickItem);
         for (GraphNode* gn : SourceChoices(scene, *item))
            if (gn->uid == sPickedUid)
            {
               SetSource(scene, *item, gn->uid);
               sPickItem = -1;
               break;
            }
      }
      sPickedUid = 0;
      sLocal.erase(std::remove_if(sLocal.begin(), sLocal.end(), [&](int id) { return scene->FindItem(id) == nullptr; }), sLocal.end());
      static Scene3DNode::Settings sSeen;
      std::memcpy(&sSeen, sSet, sizeof(sSeen));

      DrawHeader(scene);
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float gapX = ImGui::GetStyle().ItemSpacing.x;
      const float sideW = sSideOpen ? std::min(kSideW, avail.x * 0.5f) : 0.0f;
      const float viewW = std::max(120.0f, avail.x - sideW - (sSideOpen ? gapX : 0.0f));
      DrawViewport(scene, ImVec2(viewW, std::max(80.0f, avail.y)));
      if (sSideOpen)
      {
         ActionButton::RowNext();
         DrawSide(scene, sideW);
      }
      DrawPopups(scene);
      if (std::memcmp(&sSeen, sSet, sizeof(sSeen)) != 0)
         scene->Commit();
      KeepGroupsTogether(scene);
      RunPending(scene);
   }
}

OutputNode* View3DEnsureOutput() { return EnsureOutputImpl(); }

void View3DAddCamera()
{
   if (Scene3DNode* scene = FindScene())
      AddCameraNow(scene);
}

// For the screenshot tool: which tab shows, and whether through the camera.
void View3DShotState(int state)
{
   sTab = state % 10;
   sCameraView = state >= 10;
   sShading = Shading::Final;
   sSideOpen = true;
   if (Scene3DNode* scene = FindScene())
   {
      scene->settings.thirds = sCameraView;
      if (const Item* cam = scene->FindItem(scene->activeCamera))
         sSel = { cam->id };
   }
}

void View3DAddLight(int type)
{
   if (Scene3DNode* scene = FindScene())
      AddLightNow(scene, type);
}

bool View3DCanAdd(const GraphNode& gn)
{
   if (dynamic_cast<Scene3DNode*>(gn.node.get()) != nullptr)
      return false;
   return Scene3DNode::KindOf(gn.node.get()) != Scene3DNode::Kind::Missing ||
          dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr ||
          dynamic_cast<EnvironmentNode*>(gn.node.get()) != nullptr;
}

namespace
{
   uint64_t UidOf(const INode* node)
   {
      for (const GraphNode& gn : gNodes)
         if (gn.node.get() == node)
            return gn.uid;
      return 0;
   }

   Scene3DNode* EnsureScene(const GraphNode& near)
   {
      if (Scene3DNode* s = FindScene())
         return s;
      const ImVec2 at = NodePos(near);
      GraphNode* made = SpawnNode("3D View", "3D", at.x + 340.0f, at.y);
      auto* scene = made != nullptr ? dynamic_cast<Scene3DNode*>(made->node.get()) : nullptr;
      if (scene != nullptr)
      {
         scene->panelOnly = true;
         scene->Commit();
         made->hiddenFromCanvas = true;
      }
      return scene;
   }
}

void View3DSetWorld(GraphNode& gn)
{
   const uint64_t uid = gn.uid;
   PushUndoCheckpoint("3D View world");
   Scene3DNode* scene = EnsureScene(gn);
   if (scene == nullptr)
      return;
   scene->worldUid = uid;
   scene->Commit();
   gView3DOpen = true;
}

void View3DAddNode(GraphNode& gn)
{
   if (dynamic_cast<EnvironmentNode*>(gn.node.get()) != nullptr)
   {
      View3DSetWorld(gn);
      return;
   }
   // SpawnNode can move gNodes, so read what's needed from gn first.
   INode* node = gn.node.get();
   const uint64_t uid = gn.uid;
   PushUndoCheckpoint("Add to 3D View");
   Scene3DNode* scene = EnsureScene(gn);
   if (scene == nullptr)
      return;
   gView3DOpen = true;

   auto addCamera = [&](uint64_t cam)
   {
      Item& it = scene->AddItem(cam);
      if (scene->activeCamera == 0)
      {
         scene->activeCamera = it.id;
         scene->Commit();
      }
   };

   auto* render = dynamic_cast<Render3DNode*>(node);
   if (render == nullptr)
   {
      if (Scene3DNode::KindOf(node) == Scene3DNode::Kind::Camera)
         addCamera(uid);
      else
         sSel = { scene->AddItem(uid).id };
      return;
   }

   // A whole Render 3D comes in as a set: its lights light only what it brought,
   // so two look-developed nodes keep their own lighting side by side.
   std::vector<uint64_t> oldLights, newLights;
   for (const Item& it : scene->Items())
      if (scene->ItemKind(it) == Scene3DNode::Kind::Light)
         oldLights.push_back(it.uid);
   for (LightNode* light : render->lights)
      if (light != nullptr)
         if (const uint64_t lightUid = UidOf(light))
            newLights.push_back(lightUid);
   for (Item& it : scene->Items())
      if (scene->ItemKind(it) == Scene3DNode::Kind::Geometry)
         it.unlit.insert(it.unlit.end(), newLights.begin(), newLights.end());
   for (IGeometrySource* source : render->geometry)
   {
      const uint64_t sourceUid = UidOf(dynamic_cast<INode*>(source));
      if (sourceUid == 0)
         continue;
      Item& it = scene->AddItem(sourceUid);
      it.unlit = oldLights;
   }
   for (uint64_t lightUid : newLights)
      scene->AddItem(lightUid);
   if (render->camera != nullptr)
      if (const uint64_t camUid = UidOf(render->camera))
         addCamera(camUid);
   if (const uint64_t envUid = UidOf(render->envInput.Resolved()))
      scene->worldUid = envUid;
   scene->Commit();
}

void DrawView3DDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const bool gripFirst = gView3DDock != 3;
   ImGui::BeginChild(id, size, false);
   gView3DRectMin = ImGui::GetWindowPos();
   gView3DRectMax = ImVec2(gView3DRectMin.x + ImGui::GetWindowSize().x, gView3DRectMin.y + ImGui::GetWindowSize().y);
   gView3DClaimedKeys = false;
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]()
   {
      ImGui::InvisibleButton("##view3dgrip", ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(false, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         gView3DHeight += gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y;
         gView3DHeight = std::max(kMinHeight, gView3DHeight);
      }
   };
   if (gripFirst)
      grip();
   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const ImVec2 cardSize(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
   PanelFrame::Insets in;
   if (gripFirst)
      in.t = 0.0f;
   else
      in.b = 0.0f;
   PanelFrame::BeginCard("##view3dcontent", cardSize, in);
   DrawPanel();
   PanelFrame::EndCard();
   if (!gripFirst)
      grip();
   ImGui::EndChild();
}

} // namespace app
