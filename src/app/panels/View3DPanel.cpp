// The 3D View: a final-stage composer over node outputs. Everything here edits
// the Scene3DNode's placements; the nodes themselves are only ever read, so a
// source keeps animating under whatever arrangement is made on top of it.
#include "core/FactoryPresets.h"
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
#include "core/EditMesh.h"
#include "nodes/ArmatureNode.h"
#include "nodes/EditMeshNode.h"
#include "nodes/EnvironmentNode.h"
#include "nodes/OutputNode.h"
#include "nodes/Scene3DNode.h"
#include "nodes/SceneNodes.h"
#include "platform/Platform.h"
#include "Transport.h"
#include "core/Skinning.h"

#include <algorithm>
#include <filesystem>
#include <map>
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
   constexpr int kMaterialTab = 7;
   int sTab = kSceneTab;            // the settings tab
   int sLensUnit = 0;               // 0 millimeters, 1 field of view
   bool sLockCam = false;           // navigating in camera view moves the camera
   bool sLockDirty = false;
   bool sLockUndo = false;
   bool sToolKey = false;           // Shift+Space was pressed; the next key picks the tool
   int sPadDrag = 0;                // 1 zoom, 2 pan, by dragging the toolpad
   int sSelMode = 0;               // with the select tool: 0 box, 1 lasso, 2 place the 3D cursor, 3 measure, 4 circle
   bool sMeasuring = false, sMeasured = false;
   float sMeasureA[3] = {}, sMeasureB[3] = {};
   struct Ruler { float a[3], b[3]; };
   std::vector<Ruler> sRulers;     // finished measurements, kept until removed
   float sCircleR = 40.0f;         // circle select brush radius, pixels
   int sTool = 1;                  // 0 select, 1 move, 2 rotate, 3 scale
   int sHot = -1;                  // last frame's handle: 0-2 axes, 3 centre, 4-6 planes
   std::vector<int> sSel;
   std::string sCopiedScene;
   std::vector<Item> sCopiedItems;   // the list's Copy, for Paste
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
   bool sLockActive = false;       // the orbit pivot follows the active item
   // One orbit speed for drag and trackpad: 0.4 degrees per pixel, the Blender default.
   constexpr float kOrbitPerPixel = 0.4f * 3.14159265f / 180.0f;
   float RotStep();
   void TogglePose(Scene3DNode* scene);
   void PoseIk(Scene3DNode* scene, bool clear);
   // Pose mode's state; the mode itself is drawn by DrawPose.
   struct PoseState
   {
      bool on = false;
      int item = 0;
      int joint = -1;
      bool turning = false;
      bool madeSlot = false;
      int slot = -1;
      float startAngle = 0.0f;
      float axis[3] = { 0, 0, 1 };  // the turn's axis in the joint's own frame
      float q0[4] = { 0, 0, 0, 1 };
      float before[3] = { 0, 0, 0 };
      // Weight paint (W, on an Armature): the picked bone's weights, a
      // circle of this many pixels, one undo step per stroke.
      bool paint = false;
      bool stroking = false;
      float radius = 40.0f;
      ImVec2 lastDab;
   } sPose;
   void ToggleSculpt(Scene3DNode* scene);
   IRigged* PoseModel(const Item& it, Mat4& world);
   ArmatureNode* PoseArmature(Scene3DNode* scene);
   // Sculpt mode's state; the mode itself is drawn by DrawSculpt.
   struct SculptState
   {
      bool on = false;
      int item = 0;
      int brush = Sculpt::kDraw;
      float radius = 50.0f;          // screen pixels, Blender's default
      float strength = 0.5f;
      int symmetry = 1;              // X on, as in Blender
      bool stroking = false;
      ImVec2 last;
      int adjust = 0;                // 'r' radius, 's' strength (F / Shift+F)
      ImVec2 adjustAt;
      float adjustBefore = 0.0f;
      Sculpt::GrabHold grab;
      float grabWorld[3] = { 0, 0, 0 };
   } sSculpt;
   void ToggleEdit(Scene3DNode* scene);
   // Edit Mode's state; the mode itself is drawn by DrawEdit.
   struct EditState
   {
      bool on = false;
      int item = 0;
      int mode = 3;                // 1 points, 2 edges, 3 faces (Blender's 1 2 3)
      char op = 0;                 // 'g' 'r' 's' 'e' 'i' while one runs
      int axis = -1;
      ImVec2 start;
      float pivot[3] = { 0, 0, 0 };    // world
      float normal[3] = { 0, 1, 0 };   // world, the selected faces' (extrude)
      std::string before;              // the ops when the op began
      bool pressed = false;
      ImVec2 pressAt;
      // The shown mesh's edges that are not inside a flat patch, with the
      // faces on each side, rebuilt when the mesh changes.
      unsigned long long rev = ~0ull;
      std::vector<unsigned int> edges;   // two vertices per edge
      std::vector<int> edgeFaces;        // two faces per edge (-1: none)
      std::vector<int> point;            // point id per vertex
      std::vector<float> pos;            // world, per vertex, this frame
      std::vector<unsigned char> front;  // per face: faces the camera
   } sEdit;
   float sDist = 6.0f, sAz = 0.7f, sEl = 0.4f;

   bool sLasso = false;
   std::vector<ImVec2> sLassoPts;
   bool sBoxOn = false, sTweak = false, sRightOk = false, sNavDrag = false, sOpenAdd = false, sOpenMenu = false, sOpenPivot = false, sOpenOrient = false;
   bool sMenuOnItem = false;   // the right-click landed on an item (else empty space)
   ImVec2 sPressAt;

   // Spawning moves gNodes, so anything that makes a node waits until the frame's
   // drawing has let go of it.
   struct Pending
   {
      enum What { None, Camera, Light, Hdri, PickHdri, Node, Scene, DuplicateScene, PasteScene, DeleteScene, ConvertScene, Sculpt, EditMesh } what = None;
      int lightType = 1;
      int item = 0;
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
      bool track = false;          // rotate: free trackball turn (R again)
      ImVec2 start;
      float pivot[3] = { 0, 0, 0 };
      std::vector<Item> before;
      std::vector<float> centers;  // three per item, for individual origins
      std::vector<Item> near;      // proportional editing: the unselected items it may pull
      std::vector<float> nearCenters;
      std::string typed;
      bool fixed = false;          // Repeat Last: the numbers below stand in for the mouse
      float fMove[3] = { 0, 0, 0 }, fAxis[3] = { 0, 0, 1 }, fAngle = 0.0f, fFactor = 1.0f;
   } sOp;

   struct LastOp
   {
      bool have = false;
      char mode = 0;
      int axis = -1, plane = -1, orient = 0;
      float move[3], ax[3], angle, factor;
   } sLast;

   ImU32 Hex(unsigned rgb, float a = 1.0f)
   {
      return ImGui::ColorConvertFloat4ToU32(ImVec4(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a));
   }

   // The engine is Y up; the panel speaks Blender: X right, Y away from the viewer, Z up. Axis index 0 1 2 are the
   // engine's, kDisp gives the one the user sees (the swap is its own inverse). Display Y points down the engine's -Z.
   constexpr int kDisp[3] = { 0, 2, 1 };
   ImU32 AxisColor(int a, float alpha = 1.0f) { return Hex(kAxisHex[kDisp[a]], alpha); }

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

   // Shown and allowed to be picked: honours Hide Selection and the Selectability type filters.
   bool Pickable(Scene3DNode* scene, const Item& it)
   {
      if (!Shown(it) || it.hideSelect)
         return false;
      const Kind k = scene->ItemKind(it);
      if (k == Kind::Camera && (!sSet->pickCameras || !sSet->showCameras))
         return false;
      if (k == Kind::Light && (!sSet->pickLights || !sSet->showLights))
         return false;
      return true;
   }

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

   float sRoll = 0.0f;   // radians about the line of sight; any numpad view or orbit puts it back to 0

   Mat4 OrbitView()
   {
      float eye[3];
      EyeFromOrbit(eye);
      // Up follows the elevation, so the view can swing over a pole without a flip or a stall.
      float up[3] = { -std::sin(sEl) * std::cos(sAz), std::cos(sEl), -std::sin(sEl) * std::sin(sAz) };
      if (sRoll != 0.0f)
      {
         // Roll turns the view about the line of sight: up swings toward the right-hand side.
         float f[3] = { sTarget[0] - eye[0], sTarget[1] - eye[1], sTarget[2] - eye[2] };
         const float fl = std::max(1e-6f, std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]));
         for (float& v : f) v /= fl;
         const float r[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
         for (int k = 0; k < 3; k++)
            up[k] = up[k] * std::cos(sRoll) + r[k] * std::sin(sRoll);
      }
      return Mat4::LookAt(eye, sTarget, up);
   }

   // The view's field of view (vertical), from its lens: 50 mm is the 45 degrees the view always had.
   float ViewFov() { return 2.0f * std::atan(std::tan(kFov * 0.5f * kPi / 180.0f) * 50.0f / std::max(1.0f, sSet->viewLens)) * 180.0f / kPi; }
   float OrthoHalfHeight() { return sDist * std::tan(ViewFov() * 0.5f * kPi / 180.0f); }

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
         if (sLockActive && !sSel.empty())
            if (const Item* a = scene->FindItem(sSel.back()))
               scene->ItemCenter(*a, sTarget);
         if (sSet->lockCursor)
            std::copy(sSet->cursor, sSet->cursor + 3, sTarget);
         EyeFromOrbit(sCam.eye);
         sCam.view = OrbitView();
         sCam.ortho = sOrtho;
         sCam.proj = sOrtho ? Mat4::Orthographic(OrthoHalfHeight(), aspect, -2000.0f, 2000.0f)
                            : Mat4::Perspective(ViewFov() * kPi / 180.0f, aspect, std::max(0.001f, sSet->clipStart), std::max(sSet->clipStart + 0.01f, sSet->clipEnd));
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

   void LeaveCamera(bool force = false);

   // Smooth view. A jump of the view (numpad views, Frame, orbit steps) runs as a short ease instead of a cut. The
   // change is made at once, read back as the destination, and the view is put back to play towards it. A second
   // jump during a glide starts from where the view is now and aims at the new destination.
   struct Glide
   {
      bool on = false, orthoAtEnd = false;
      double t0 = 0.0;
      float dur = 0.2f;
      float src[6] = {}, dst[6] = {};   // az, el, dist, target xyz
   } sGlide;

   void ReadView(float s[6])
   {
      s[0] = sAz; s[1] = sEl; s[2] = sDist;
      for (int k = 0; k < 3; k++)
         s[3 + k] = sTarget[k];
   }

   void WriteView(const float s[6])
   {
      sAz = s[0]; sEl = s[1]; sDist = s[2];
      for (int k = 0; k < 3; k++)
         sTarget[k] = s[3 + k];
   }

   // Jumps to the end of a running glide, as when a hand takes over.
   void FinishGlide()
   {
      if (!sGlide.on)
         return;
      sGlide.on = false;
      WriteView(sGlide.dst);
      if (sGlide.orthoAtEnd)
         sOrtho = true;
      sGlide.orthoAtEnd = false;
   }

   void StepGlide()
   {
      if (!sGlide.on)
         return;
      const float t = (float)((ImGui::GetTime() - sGlide.t0) / std::max(1e-3f, sGlide.dur));
      if (t >= 1.0f)
      {
         FinishGlide();
         return;
      }
      const float e = t * t * (3.0f - 2.0f * t);
      float now[6];
      for (int i = 0; i < 6; i++)
         now[i] = sGlide.src[i] + (sGlide.dst[i] - sGlide.src[i]) * e;
      WriteView(now);
   }

   template <class Change>
   void GlideTo(Change change)
   {
      if (sCameraView && !sLockCam)
         LeaveCamera();
      float here[6], from[6];
      ReadView(here);
      const bool pendingOrtho = sGlide.orthoAtEnd;
      if (sGlide.on)
         WriteView(sGlide.dst);
      ReadView(from);
      sGlide.on = false;
      const bool wasOrtho = sOrtho;
      change();
      float to[6];
      ReadView(to);
      auto wrap = [](float a) { return a - 2.0f * kPi * std::floor((a + kPi) / (2.0f * kPi)); };
      to[0] = here[0] + wrap(to[0] - here[0]);   // the short way round
      to[1] = here[1] + wrap(to[1] - here[1]);
      float move = 0.0f;
      for (int i = 2; i < 6; i++)
         move = std::max(move, std::abs(to[i] - here[i]));
      const float turn = std::max(std::abs(to[0] - here[0]), std::abs(to[1] - here[1]));
      float dur = sSet->smoothTime;
      if (move < 1e-5f)
         dur *= turn / kPi;   // a small turn is quick, a half turn takes the full time
      if (dur < 0.012f || (turn < 1e-5f && move < 1e-5f))
         return;
      std::copy(here, here + 6, sGlide.src);
      std::copy(to, to + 6, sGlide.dst);
      sGlide.dur = dur;
      sGlide.t0 = ImGui::GetTime();
      sGlide.on = true;
      // Into an orthographic axis view only once it has arrived, so the lens does not snap mid-turn.
      sGlide.orthoAtEnd = pendingOrtho || (!wasOrtho && sOrtho);
      if (sGlide.orthoAtEnd)
         sOrtho = false;
      WriteView(here);
      (void)from;
   }

   float RotStep() { return std::clamp(sSet->rotStep, 1.0f, 90.0f) * 3.14159265f / 180.0f; }

   // The view from before the camera, so toggling the camera off goes back to it (orbiting out starts at the camera).
   struct { bool set = false; float view[6]; float roll; bool ortho; } sPreCam;

   void EnterCamera()
   {
      if (!sCameraView)
      {
         ReadView(sPreCam.view);
         sPreCam.roll = sRoll;
         sPreCam.ortho = sOrtho;
         sPreCam.set = true;
      }
      sCameraView = true;
   }

   // Leaving a camera keeps the view where it is rather than jumping back.
   void LeaveCamera(bool force)
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
      FinishGlide();
      LeaveCamera();
      sViewName = -1;
      sGridAxis = 1;
      if (sAutoOrtho)
         sOrtho = false;
      sAutoOrtho = false;
   }

   // 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z: which side the eye stands on.
   void ViewAxisNow(int which)
   {
      LeaveCamera();
      const float top = 1.5697f;
      const float az[6] = { 0.0f, kPi, kPi * 0.5f, kPi * 0.5f, kPi * 0.5f, -kPi * 0.5f };
      const float el[6] = { 0.0f, 0.0f, top, -top, 0.0f, 0.0f };
      sRoll = 0.0f;
      sAz = az[which];
      sEl = el[which];
      sViewName = which;
      sGridAxis = which / 2;
      if (sSet->autoPerspective)
      {
         sAutoOrtho = sAutoOrtho || !sOrtho;
         sOrtho = true;
      }
   }

   void ViewAxis(int which)
   {
      GlideTo([&] { ViewAxisNow(which); });
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
      FinishGlide();
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
      FinishGlide();
      if (sCameraView && !sLockCam)
      {
         sCamZoom = std::clamp(sCamZoom / ratio, 0.1f, 12.0f);
         return;
      }
      LeaveCamera();
      const float was = sDist;
      const float want = was * ratio;
      sDist = std::clamp(want, 0.1f, 500.0f);
      // Zooming in past the closest distance carries the pivot forward instead of stalling.
      if (want < sDist)
         for (int k = 0; k < 3; k++)
            sTarget[k] += sCam.fwd[k] * (sDist - want);
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
      sRoll = 0.0f;
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
      sEl += dEl;
      if (sEl > kPi)
         sEl -= 2.0f * kPi;
      else if (sEl < -kPi)
         sEl += 2.0f * kPi;
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

   // Rotations shown in Blender's frame: the matrix seen through the axis swap (x, y, z) -> (x, -z, y).
   static void Swap3(const float in[3][3], float out[3][3], bool toDisplay)
   {
      const float c[3][3] = { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } };
      float t[3][3] = {};
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
               t[i][j] += toDisplay ? c[i][k] * in[k][j] : c[k][i] * in[k][j];
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++)
         {
            out[i][j] = 0.0f;
            for (int k = 0; k < 3; k++)
               out[i][j] += toDisplay ? t[i][k] * c[j][k] : t[i][k] * c[k][j];
         }
   }

   void ToDisplayRotation(const float engineDeg[3], float displayDeg[3])
   {
      float r[3][3], d[3][3];
      RotationOf(engineDeg, r);
      Swap3(r, d, true);
      EulerFrom(d, displayDeg);
   }

   void FromDisplayRotation(const float displayDeg[3], float engineDeg[3])
   {
      float d[3][3], r[3][3];
      RotationOf(displayDeg, d);
      Swap3(d, r, false);
      EulerFrom(r, engineDeg);
   }

   // The 3D cursor's turn in the three ways Blender shows it, all read in the displayed frame.
   void QuaternionOf(const float engineDeg[3], float q[4])
   {
      float r[3][3], d[3][3];
      RotationOf(engineDeg, r);
      Swap3(r, d, true);
      const float tr = d[0][0] + d[1][1] + d[2][2];
      if (tr > 0.0f)
      {
         const float k = std::sqrt(tr + 1.0f) * 2.0f;
         q[0] = 0.25f * k; q[1] = (d[2][1] - d[1][2]) / k; q[2] = (d[0][2] - d[2][0]) / k; q[3] = (d[1][0] - d[0][1]) / k;
      }
      else if (d[0][0] > d[1][1] && d[0][0] > d[2][2])
      {
         const float k = std::sqrt(1.0f + d[0][0] - d[1][1] - d[2][2]) * 2.0f;
         q[0] = (d[2][1] - d[1][2]) / k; q[1] = 0.25f * k; q[2] = (d[0][1] + d[1][0]) / k; q[3] = (d[0][2] + d[2][0]) / k;
      }
      else if (d[1][1] > d[2][2])
      {
         const float k = std::sqrt(1.0f + d[1][1] - d[0][0] - d[2][2]) * 2.0f;
         q[0] = (d[0][2] - d[2][0]) / k; q[1] = (d[0][1] + d[1][0]) / k; q[2] = 0.25f * k; q[3] = (d[1][2] + d[2][1]) / k;
      }
      else
      {
         const float k = std::sqrt(1.0f + d[2][2] - d[0][0] - d[1][1]) * 2.0f;
         q[0] = (d[1][0] - d[0][1]) / k; q[1] = (d[0][2] + d[2][0]) / k; q[2] = (d[1][2] + d[2][1]) / k; q[3] = 0.25f * k;
      }
   }

   void SetQuaternion(const float qin[4], float engineDeg[3])
   {
      const float len = std::max(1e-6f, std::sqrt(qin[0] * qin[0] + qin[1] * qin[1] + qin[2] * qin[2] + qin[3] * qin[3]));
      const float w = qin[0] / len, x = qin[1] / len, y = qin[2] / len, z = qin[3] / len;
      const float d[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                              { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                              { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
      float r[3][3];
      Swap3(d, r, false);
      EulerFrom(r, engineDeg);
   }

   void CursorQuaternion(float q[4]) { QuaternionOf(sSet->cursorRot, q); }
   void SetCursorQuaternion(const float q[4]) { SetQuaternion(q, sSet->cursorRot); }

   // Axis-angle (degrees, then the axis) from a quaternion and back, for the Quaternion/Axis Angle rotation modes.
   void AxisAngleOf(const float q[4], float aa[4])
   {
      const float half = std::acos(std::clamp(q[0], -1.0f, 1.0f)), sn = std::max(1e-6f, std::sin(half));
      aa[0] = half * 2.0f * 180.0f / kPi; aa[1] = q[1] / sn; aa[2] = q[2] / sn; aa[3] = q[3] / sn;
      if (half < 1e-4f)
         aa[1] = aa[2] = 0.0f, aa[3] = 1.0f;
   }

   void QuaternionFromAxisAngle(const float aa[4], float q[4])
   {
      const float an = std::max(1e-6f, std::sqrt(aa[1] * aa[1] + aa[2] * aa[2] + aa[3] * aa[3]));
      const float h = aa[0] * kPi / 360.0f, sh = std::sin(h) / an;
      q[0] = std::cos(h); q[1] = aa[1] * sh; q[2] = aa[2] * sh; q[3] = aa[3] * sh;
   }

   int sItemRotMode = 0;   // 0 XYZ Euler, 1 Quaternion, 2 Axis Angle: how the Item tab shows the rotation

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
      if (orient == 1 || orient >= 3)
      {
         float r[3][3];
         // 1 the item's own turn, 3 the 3D cursor's, above that a saved one.
         RotationOf(orient == 1 ? item.rot : orient == 3 ? sSet->cursorRot : sSet->customRot[std::clamp(orient - 4, 0, 3)], r);
         for (int k = 0; k < 3; k++)
            out[k] = r[k][a];
      }
      else if (orient == 2)
         for (int k = 0; k < 3; k++)
            out[k] = a == 0 ? sCam.right[k] : a == 1 ? sCam.up[k] : -sCam.fwd[k];
      else
         out[a] = 1.0f;
      if (orient != 2 && a == 2)
         for (int k = 0; k < 3; k++)
            out[k] = -out[k];
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
      if (sSet->proportional)
         for (const Item& it : scene->Items())
            if (Shown(it) && !Selected(it.id))
            {
               sOp.near.push_back(it);
               float c[3];
               scene->ItemCenter(it, c);
               sOp.nearCenters.insert(sOp.nearCenters.end(), c, c + 3);
            }
   }

   // How strongly an item at `dist` from the selection follows: 1 at it, 0 at the size.
   float Falloff(float dist)
   {
      const float size = std::max(0.01f, sSet->propSize);
      if (dist >= size && sSet->propFalloff != 5)
         return 0.0f;
      const float u = std::clamp(1.0f - dist / size, 0.0f, 1.0f);
      switch (sSet->propFalloff)
      {
      case 0: return u * u * (3.0f - 2.0f * u);
      case 1: return std::sqrt(std::max(0.0f, 2.0f * u - u * u));
      case 2: return std::sqrt(u);
      case 3: return u;
      case 4: return u * u;
      default: return dist < size ? 1.0f : 0.0f;
      }
   }

   // A locked channel keeps the value it had when the gesture began.
   static void KeepLocked(Item& it, const Item& was)
   {
      if (it.lock == 0)
         return;
      for (int k = 0; k < 3; k++)
      {
         if (it.lock & (1u << k)) it.pos[k] = was.pos[k];
         if (it.lock & (8u << k)) it.rot[k] = was.rot[k];
         if (it.lock & (64u << k)) it.scale[k] = was.scale[k];
      }
   }

   void ApplyOp(Scene3DNode* scene, ImVec2 mouse)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const bool snapOn = sSet->snap != (io.KeyCtrl || io.KeySuper);   // Cmd or Ctrl on macOS, Ctrl elsewhere
      // One switch per tool (Snap card), so a grid can hold the move and leave the turn free.
      const bool snap = snapOn && (sOp.mode == 'g' ? sSet->snapTranslate : sOp.mode == 'r' ? sSet->snapRotate : sSet->snapScaleOn);
      // Turning with the snap on, Shift swaps the slow drag for the finer increment.
      const bool fineSnap = snap && sOp.mode == 'r' && io.KeyShift;
      const float fine = io.KeyShift && sOp.plane < 0 && !fineSnap ? 0.1f : 1.0f;
      const Item* alignTo = nullptr;   // snapped onto this item's origin: its turn follows when asked
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
         if (snap && sSet->snapObjects && !hasTyped && sOp.axis < 0 && sOp.plane < 0)
         {
            // Snap to objects: the nearest other origin within 24 px of where the pivot would land.
            const float land[3] = { sOp.pivot[0] + move[0], sOp.pivot[1] + move[1], sOp.pivot[2] + move[2] };
            ImVec2 ls;
            if (Project(land, ls))
            {
               float best = 24.0f;
               for (const Item& other : scene->Items())
               {
                  if (!Shown(other) || Selected(other.id) || (sSet->snapSelectable && other.hideSelect))
                     continue;
                  float oc[3];
                  ImVec2 os;
                  scene->ItemCenter(other, oc);
                  if (!Project(oc, os))
                     continue;
                  const float dist = std::hypot(os.x - ls.x, os.y - ls.y);
                  if (dist < best)
                  {
                     best = dist;
                     for (int k = 0; k < 3; k++)
                        move[k] = oc[k] - sOp.pivot[k];
                     snapStep = false;
                     alignTo = &other;
                  }
               }
            }
         }
      }
      else if (sOp.mode == 'r')
      {
         const float a0 = std::atan2(sOp.start.y - c.y, sOp.start.x - c.x);
         const float a1 = std::atan2(mouse.y - c.y, mouse.x - c.x);
         float deg = (a1 - a0) * fine * 180.0f / kPi;
         if (sOp.track)
         {
            // Trackball: the drag turns the item about the screen axis square to it.
            float t[3];
            for (int k = 0; k < 3; k++)
               t[k] = sCam.up[k] * d.x + sCam.right[k] * d.y;
            const float len = std::sqrt(Dot(t, t));
            if (len > 1e-4f)
               for (int k = 0; k < 3; k++)
                  axis[k] = t[k] / len;
            deg = len * 0.5f;
         }
         else if (sOp.axis < 0)
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
            deg = SnapTo(deg, io.KeyShift ? sSet->snapRotFine : sSet->snapRot);
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

      if (sOp.mode == 'g' && snapStep && !sOp.fixed)
      {
         // Relative: the move itself lands on the increments. Absolute: what the Snap Target names lands on the
         // grid (closest = each item on its own, the default).
         float ref[3] = { 0, 0, 0 };
         bool whole = false;
         if (!sSet->snapAbsolute)
         {
            for (int k = 0; k < 3; k++)
               if (moved[k])
                  move[k] = SnapTo(move[k], sSet->snapMove);
            snapStep = false;
         }
         else if (sSet->snapTarget != 0 && !sOp.centers.empty())
         {
            const size_t n = sOp.centers.size() / 3;
            for (int k = 0; k < 3; k++)
            {
               float lo = 1e30f, hi = -1e30f, sum = 0.0f;
               for (size_t j = 0; j < n; j++)
               {
                  lo = std::min(lo, sOp.centers[j * 3 + k]); hi = std::max(hi, sOp.centers[j * 3 + k]);
                  sum += sOp.centers[j * 3 + k];
               }
               ref[k] = sSet->snapTarget == 1 ? (lo + hi) * 0.5f : sSet->snapTarget == 2 ? sum / (float)n : sOp.centers[(n - 1) * 3 + k];
            }
            whole = true;
         }
         if (whole)
         {
            for (int k = 0; k < 3; k++)
               if (moved[k])
                  move[k] = SnapTo(ref[k] + move[k], sSet->snapMove) - ref[k];
            snapStep = false;
         }
      }
      if (sOp.fixed)
      {
         std::copy(sOp.fMove, sOp.fMove + 3, move);
         std::copy(sOp.fAxis, sOp.fAxis + 3, axis);
         angle = sOp.fAngle;
         factor = sOp.fFactor;
         snapStep = false;
      }
      else
      {
         std::copy(move, move + 3, sOp.fMove);
         std::copy(axis, axis + 3, sOp.fAxis);
         sOp.fAngle = angle;
         sOp.fFactor = factor;
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
            if (alignTo != nullptr && sSet->snapAlign)
               std::copy(alignTo->rot, alignTo->rot + 3, it->rot);
         }
         else if (sOp.mode == 'r')
         {
            float dr[3][3], r0[3][3], r1[3][3];
            AxisAngle(axis, angle, dr);
            RotationOf(was.rot, r0);
            Mul(dr, r0, r1);
            if (!sSet->onlyLocations)
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
               if (!sSet->onlyLocations)
                  it->scale[k] = was.scale[k] * f;
            }
            for (int k = 0; k < 3; k++)
               it->pos[k] = pivot[k] + r[k][0] * local[0] + r[k][1] * local[1] + r[k][2] * local[2];
         }
         KeepLocked(*it, was);
      }
      for (size_t i = 0; i < sOp.near.size(); i++)
      {
         const Item& was = sOp.near[i];
         Item* it = scene->FindItem(was.id);
         if (it == nullptr)
            continue;
         const float* nc = &sOp.nearCenters[i * 3];
         float dist = 1e30f;
         for (size_t j = 0; j < sOp.centers.size(); j += 3)
         {
            float dv[3] = { nc[0] - sOp.centers[j], nc[1] - sOp.centers[j + 1], nc[2] - sOp.centers[j + 2] };
            if (sSet->projectedProp)   // measured across the view: depth doesn't count
            {
               const float depth = Dot(dv, sCam.fwd);
               for (int k = 0; k < 3; k++)
                  dv[k] -= sCam.fwd[k] * depth;
            }
            dist = std::min(dist, std::hypot(dv[0], std::hypot(dv[1], dv[2])));
         }
         const float w = Falloff(dist);
         *it = was;
         if (w <= 0.0f)
            continue;
         const float off[3] = { was.pos[0] - sOp.pivot[0], was.pos[1] - sOp.pivot[1], was.pos[2] - sOp.pivot[2] };
         if (sOp.mode == 'g')
         {
            for (int k = 0; k < 3; k++)
               it->pos[k] = was.pos[k] + move[k] * w;
         }
         else if (sOp.mode == 'r')
         {
            float dr[3][3], r0[3][3], r1[3][3];
            AxisAngle(axis, angle * w, dr);
            RotationOf(was.rot, r0);
            Mul(dr, r0, r1);
            if (!sSet->onlyLocations)
               EulerFrom(r1, it->rot);
            for (int k = 0; k < 3; k++)
               it->pos[k] = sOp.pivot[k] + dr[k][0] * off[0] + dr[k][1] * off[1] + dr[k][2] * off[2];
         }
         else
         {
            float r[3][3], local[3];
            RotationOf(was.rot, r);
            for (int k = 0; k < 3; k++)
            {
               const float full = (sOp.axis < 0 && sOp.plane < 0) || sOp.axis == k || (sOp.plane >= 0 && sOp.plane != k) ? factor : 1.0f;
               const float f = 1.0f + (full - 1.0f) * w;
               local[k] = (r[0][k] * off[0] + r[1][k] * off[1] + r[2][k] * off[2]) * f;
               if (!sSet->onlyLocations)
                  it->scale[k] = was.scale[k] * f;
            }
            for (int k = 0; k < 3; k++)
               it->pos[k] = sOp.pivot[k] + r[k][0] * local[0] + r[k][1] * local[1] + r[k][2] * local[2];
         }
         KeepLocked(*it, was);
      }
      scene->Commit();
   }

   void CancelOp(Scene3DNode* scene)
   {
      for (const Item& was : sOp.before)
         if (Item* it = scene->FindItem(was.id))
            *it = was;
      for (const Item& was : sOp.near)
         if (Item* it = scene->FindItem(was.id))
            *it = was;
      scene->Commit();
      sOp = Op();
   }

   // One gesture, one undo: the edit is taken back, checkpointed, and laid on again.
   void ConfirmOp(Scene3DNode* scene)
   {
      sLast.have = true;
      sLast.mode = sOp.mode;
      sLast.axis = sOp.axis;
      sLast.plane = sOp.plane;
      sLast.orient = sOp.orient;
      std::copy(sOp.fMove, sOp.fMove + 3, sLast.move);
      std::copy(sOp.fAxis, sOp.fAxis + 3, sLast.ax);
      sLast.angle = sOp.fAngle;
      sLast.factor = sOp.fFactor;
      std::vector<Item> after;
      for (const std::vector<Item>* set : { &sOp.before, &sOp.near })
         for (const Item& was : *set)
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

   // Shift+R: the last move, turn or scale again, with the same numbers, on whatever is selected now.
   void RepeatLast(Scene3DNode* scene)
   {
      if (!sLast.have || sSel.empty())
         return;
      BeginOp(sLast.mode, scene, ImVec2(0, 0), sLast.axis, false, sLast.plane);
      if (sOp.mode == 0)
         return;
      sOp.orient = sLast.orient;
      sOp.fixed = true;
      std::copy(sLast.move, sLast.move + 3, sOp.fMove);
      std::copy(sLast.ax, sLast.ax + 3, sOp.fAxis);
      sOp.fAngle = sLast.angle;
      sOp.fFactor = sLast.factor;
      ApplyOp(scene, ImVec2(0, 0));
      ConfirmOp(scene);
   }

   void FrameItemsNow(Scene3DNode* scene, bool all)
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

   void FrameItems(Scene3DNode* scene, bool all)
   {
      GlideTo([&] { FrameItemsNow(scene, all); });
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

   // Adds copies of the items and selects them.
   void AddCopies(Scene3DNode* scene, const std::vector<Item>& from)
   {
      std::vector<int> made;
      std::map<int, int> groups;
      for (Item copy : from)
      {
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
   }

   std::vector<Item> SelectedItems(Scene3DNode* scene)
   {
      std::vector<Item> out;
      for (int id : sSel)
         if (const Item* it = scene->FindItem(id))
            out.push_back(*it);
      return out;
   }

   // From the viewport the copies follow the mouse; from the list they stay put.
   void DuplicateSelected(Scene3DNode* scene, ImVec2 mouse, bool grab = true)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Duplicate in 3D View");
      AddCopies(scene, SelectedItems(scene));
      if (grab)
         BeginOp('g', scene, mouse);
   }

   void PasteItems(Scene3DNode* scene)
   {
      if (sCopiedItems.empty())
         return;
      PushUndoCheckpoint("Paste in 3D View");
      AddCopies(scene, sCopiedItems);
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

   // Select menu. Each works on what is shown, like the A key.
   std::vector<int> ShownIds(Scene3DNode* scene)
   {
      std::vector<int> ids;
      for (const Item& it : scene->Items())
         if (Shown(it))
            ids.push_back(it.id);
      return ids;
   }

   void SelectInvert(Scene3DNode* scene)
   {
      std::vector<int> now;
      for (int id : ShownIds(scene))
         if (!Selected(id))
            now.push_back(id);
      sSel = now;
   }

   // Keeps each item with the given chance; the same press picks a new set.
   void SelectRandom(Scene3DNode* scene, float ratio)
   {
      static unsigned seed = 12345u;
      std::vector<int> now;
      for (int id : ShownIds(scene))
      {
         seed = seed * 1664525u + 1013904223u;
         if ((float)(seed >> 8) / 16777216.0f < ratio)
            now.push_back(id);
      }
      sSel = now;
   }

   // Drops every other selected item, in list order.
   void SelectChecker(Scene3DNode* scene)
   {
      std::vector<int> now;
      int n = 0;
      for (const Item& it : scene->Items())
         if (Selected(it.id) && (n++ % 2) == 0)
            now.push_back(it.id);
      sSel = now;
   }

   // Adds whatever shares a group with the selection.
   void SelectLinked(Scene3DNode* scene)
   {
      std::vector<int> groups;
      for (int id : sSel)
         if (const Item* it = scene->FindItem(id))
            if (it->group != 0)
               groups.push_back(it->group);
      for (const Item& it : scene->Items())
         if (Pickable(scene, it) && !Selected(it.id) && std::find(groups.begin(), groups.end(), it.group) != groups.end())
            sSel.insert(sSel.begin(), it.id);
   }

   // Adds every shown item of the same kind as the active one.
   void SelectSameType(Scene3DNode* scene)
   {
      const Item* active = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      if (active == nullptr)
         return;
      const Kind kind = scene->ItemKind(*active);
      for (const Item& it : scene->Items())
         if (Pickable(scene, it) && !Selected(it.id) && scene->ItemKind(it) == kind)
            sSel.insert(sSel.begin(), it.id);
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

   void ToggleLocal(Scene3DNode* scene);
   void CameraToView(Scene3DNode* scene);
   void HideSelected(Scene3DNode* scene);

   // Snap menu. Everything moves by the item's centre, like Selection to Cursor.
   void MoveSelectionBy(Scene3DNode* scene, const char* undo, const std::function<void(const float c[3], float d[3])>& delta)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint(undo);
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
         {
            float c[3], d[3] = { 0, 0, 0 };
            scene->ItemCenter(*it, c);
            delta(c, d);
            for (int k = 0; k < 3; k++)
               it->pos[k] += d[k];
         }
      scene->Commit();
   }

   void SelectionToGrid(Scene3DNode* scene)
   {
      const float step = sSet->snapMove > 0.0f ? sSet->snapMove : 1.0f;
      MoveSelectionBy(scene, "Snap to grid", [&](const float c[3], float d[3]) {
         for (int k = 0; k < 3; k++)
            d[k] = SnapTo(c[k], step) - c[k];
      });
   }

   void SelectionToActive(Scene3DNode* scene)
   {
      const Item* a = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      if (a == nullptr)
         return;
      float t[3];
      scene->ItemCenter(*a, t);
      MoveSelectionBy(scene, "Move to active", [&](const float c[3], float d[3]) {
         for (int k = 0; k < 3; k++)
            d[k] = t[k] - c[k];
      });
   }

   // Moves the whole selection by the offset that takes its middle to the cursor.
   void SelectionToCursorKeepOffset(Scene3DNode* scene)
   {
      float m[3];
      if (!SelectionCenter(scene, m))
         return;
      MoveSelectionBy(scene, "Move to 3D cursor", [&](const float[3], float d[3]) {
         for (int k = 0; k < 3; k++)
            d[k] = sSet->cursor[k] - m[k];
      });
   }

   // Saves the active item's turn as a named orientation, and picks it.
   void CreateOrientation(Scene3DNode* scene)
   {
      const Item* a = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      if (a == nullptr || sSet->customCount >= 4)
         return;
      PushUndoCheckpoint("Create orientation");
      const Mat4 m = Scene3DNode::Placement(*a);
      float whole[3][3];
      // The turn without the scale: each column of the placement, normalised.
      for (int col = 0; col < 3; col++)
      {
         const float len = std::max(1e-6f, std::sqrt(m.m[col * 4] * m.m[col * 4] + m.m[col * 4 + 1] * m.m[col * 4 + 1] + m.m[col * 4 + 2] * m.m[col * 4 + 2]));
         for (int row = 0; row < 3; row++)
            whole[row][col] = m.m[col * 4 + row] / len;
      }
      EulerFrom(whole, sSet->customRot[sSet->customCount]);
      sSet->orientation = 4 + sSet->customCount;
      sSet->customCount++;
   }

   // Drops the orientation in use, when it is a saved one.
   void DeleteOrientation()
   {
      const int at = sSet->orientation - 4;
      if (at < 0 || at >= sSet->customCount)
         return;
      PushUndoCheckpoint("Delete orientation");
      for (int i = at; i + 1 < sSet->customCount; i++)
         std::copy(sSet->customRot[i + 1], sSet->customRot[i + 1] + 3, sSet->customRot[i]);
      sSet->customCount--;
      sSet->orientation = 0;
   }

   // The plane a click sets the 3D cursor on (Cursor tool, Shift+right-click): across the view by default, or a
   // world plane through the cursor, its axis chosen or taken from the view.
   void PlacementPlane(float anchor[3], float n[3])
   {
      if (sSet->planeDepth != 1)
         std::copy(sSet->cursor, sSet->cursor + 3, anchor);
      if (sSet->planeDepth == 1 || sSet->planeOrient == 1)
      {
         std::copy(sCam.fwd, sCam.fwd + 3, n);
         return;
      }
      int axis = sSet->planeAxis;   // display X, Y, Z = engine 0, 2, 1
      if (sSet->planeAuto)
      {
         int best = 0;
         for (int k = 1; k < 3; k++)
            if (std::abs(sCam.fwd[k]) > std::abs(sCam.fwd[best]))
               best = k;
         axis = best == 0 ? 0 : best == 2 ? 1 : 2;
      }
      n[0] = n[1] = n[2] = 0.0f;
      n[axis == 0 ? 0 : axis == 1 ? 2 : 1] = 1.0f;
      std::copy(sSet->cursor, sSet->cursor + 3, anchor);
   }

   // A new item stands at the 3D cursor, turned as the Align New Items setting says.
   void PlaceNew(Item& it)
   {
      std::copy(sSet->cursor, sSet->cursor + 3, it.pos);
      if (sSet->alignNew == 2)
         std::copy(sSet->cursorRot, sSet->cursorRot + 3, it.rot);
      else if (sSet->alignNew == 1)
      {
         const float r[3][3] = { { sCam.right[0], sCam.up[0], -sCam.fwd[0] }, { sCam.right[1], sCam.up[1], -sCam.fwd[1] },
                                 { sCam.right[2], sCam.up[2], -sCam.fwd[2] } };
         EulerFrom(r, it.rot);
      }
   }

   void CursorToOrigin()
   {
      PushUndoCheckpoint("Move 3D cursor");
      std::fill(sSet->cursor, sSet->cursor + 3, 0.0f);
   }

   void CursorToActive(Scene3DNode* scene)
   {
      const Item* a = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      if (a == nullptr)
         return;
      PushUndoCheckpoint("Move 3D cursor");
      scene->ItemCenter(*a, sSet->cursor);
   }

   // display axis 0 X, 1 Y, 2 Z: flips the item's scale about its own origin.
   void MirrorSelected(Scene3DNode* scene, int displayAxis)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Mirror in 3D View");
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
            it->scale[kDisp[displayAxis]] *= -1.0f;
      scene->Commit();
   }

   // Ctrl+L: the active item's placement onto the rest of the selection.
   // Ctrl+P / Alt+P: Blender's parent and clear parent, as Follow. Every other
   // selected item follows the active one (last clicked), keeping where it is.
   void FollowActive(Scene3DNode* scene, bool clear)
   {
      const int active = sSel.empty() ? 0 : sSel.back();
      if (active == 0 || (!clear && sSel.size() < 2))
         return;
      PushUndoCheckpoint(clear ? "Clear follow in 3D View" : "Follow in 3D View");
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
         {
            const int target = clear ? 0 : active;
            if (id == active && !clear)
               continue;
            if (target != 0 && scene->FollowLoops(id, target))
               continue;
            it->follow = target;
            it->followed[0] = NAN;
         }
      scene->Commit();
   }

   void CopyTransformsFromActive(Scene3DNode* scene)
   {
      const Item* a = sSel.size() < 2 ? nullptr : scene->FindItem(sSel.back());
      if (a == nullptr)
         return;
      PushUndoCheckpoint("Copy transforms");
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
            if (it != a)
            {
               std::copy(a->pos, a->pos + 3, it->pos);
               std::copy(a->rot, a->rot + 3, it->rot);
               std::copy(a->scale, a->scale + 3, it->scale);
            }
      scene->Commit();
   }

   void HideUnselected(Scene3DNode* scene)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Hide in 3D View");
      for (Item& it : scene->Items())
         if (!Selected(it.id))
            it.visible = false;
      scene->Commit();
   }

   // ---- Object / Select / View operators ----
   enum class ClearWhat { Location, Rotation, Scale };

   void ClearTransform(Scene3DNode* scene, ClearWhat what)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Clear transform in 3D View");
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
         {
            float* v = what == ClearWhat::Location ? it->pos : what == ClearWhat::Rotation ? it->rot : it->scale;
            const float rest = what == ClearWhat::Scale ? 1.0f : 0.0f;
            for (int k = 0; k < 3; k++)
               if (!(it->lock & (1u << ((int)what * 3 + k))))
                  v[k] = rest;
         }
      scene->Commit();
   }

   // Align every selected item to the active one along a display axis: 0 min, 1 centre, 2 max of the active's box.
   void AlignSelected(Scene3DNode* scene, int displayAxis)
   {
      const Item* a = sSel.size() < 2 ? nullptr : scene->FindItem(sSel.back());
      if (a == nullptr)
         return;
      float ac[3];
      scene->ItemCenter(*a, ac);
      const int axis = kDisp[displayAxis];
      PushUndoCheckpoint("Align in 3D View");
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
            if (it != a)
            {
               float c[3];
               scene->ItemCenter(*it, c);
               it->pos[axis] += ac[axis] - c[axis];
            }
      scene->Commit();
   }

   void CopyGlobalTransform(Scene3DNode* scene)
   {
      const Item* a = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      if (a == nullptr)
         return;
      char buf[256];
      snprintf(buf, sizeof(buf), "infinite-transform %g %g %g %g %g %g %g %g %g", a->pos[0], a->pos[1], a->pos[2], a->rot[0],
               a->rot[1], a->rot[2], a->scale[0], a->scale[1], a->scale[2]);
      ImGui::SetClipboardText(buf);
   }

   void PasteGlobalTransform(Scene3DNode* scene)
   {
      const char* text = ImGui::GetClipboardText();
      float v[9];
      if (text == nullptr || sSel.empty() ||
          sscanf(text, "infinite-transform %f %f %f %f %f %f %f %f %f", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, v + 7, v + 8) != 9)
         return;
      PushUndoCheckpoint("Paste transform");
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
         {
            std::copy(v, v + 3, it->pos);
            std::copy(v + 3, v + 6, it->rot);
            std::copy(v + 6, v + 9, it->scale);
         }
      scene->Commit();
   }

   void ShowAllInRenders(Scene3DNode* scene)
   {
      PushUndoCheckpoint("Show in renders");
      for (Item& it : scene->Items())
         it.render = true;
      scene->Commit();
   }

   // Only the active item's kind stays in the render.
   void IsolateTypeRender(Scene3DNode* scene)
   {
      const Item* a = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      if (a == nullptr)
         return;
      const Kind kind = scene->ItemKind(*a);
      PushUndoCheckpoint("Render only one type");
      for (Item& it : scene->Items())
         it.render = scene->ItemKind(it) == kind;
      scene->Commit();
   }

   void RandomizeTransform(Scene3DNode* scene)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Randomize transform");
      static unsigned seed = 12345;
      auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f; };
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
            for (int k = 0; k < 3; k++)
               it->pos[k] += rnd() * 0.5f;
      scene->Commit();
   }

   void SelectActiveCamera(Scene3DNode* scene)
   {
      if (scene->FindItem(scene->activeCamera) != nullptr)
         sSel = { scene->activeCamera };
   }

   char sPattern[64] = "*";
   bool WildMatch(const char* pat, const char* str)
   {
      if (*pat == 0)
         return *str == 0;
      if (*pat == '*')
         return WildMatch(pat + 1, str) || (*str != 0 && WildMatch(pat, str + 1));
      return *str != 0 && std::tolower((unsigned char)*pat) == std::tolower((unsigned char)*str) && WildMatch(pat + 1, str + 1);
   }

   void SelectPattern(Scene3DNode* scene)
   {
      sSel.clear();
      for (const Item& it : scene->Items())
         if (Pickable(scene, it) && WildMatch(sPattern, ItemName(it).c_str()))
            sSel.push_back(it.id);
   }

   // Move to the ground point under the last place the pointer was over the view.
   ImVec2 sViewMouse(-1, -1);
   void MoveToMouse(Scene3DNode* scene)
   {
      if (sSel.empty() || sViewMouse.x < 0)
         return;
      float o[3], d[3];
      MouseRay(sViewMouse, o, d);
      if (std::fabs(d[1]) < 1e-5f)
         return;
      const float t = -o[1] / d[1];
      if (t <= 0.0f)
         return;
      const float hit[3] = { o[0] + d[0] * t, 0.0f, o[2] + d[2] * t };
      MoveSelectionBy(scene, "Move to mouse", [&](const float c[3], float out[3]) {
         for (int k = 0; k < 3; k++)
            out[k] = hit[k] - c[k];
      });
   }

   void SnapCursorToGrid()
   {
      const float step = sSet->snapMove > 0.0f ? sSet->snapMove : 1.0f;
      PushUndoCheckpoint("Move 3D cursor");
      for (int k = 0; k < 3; k++)
         sSet->cursor[k] = SnapTo(sSet->cursor[k], step);
   }

   void RemoveFromLocal()
   {
      if (sLocal.empty())
         return;
      for (int id : sSel)
         sLocal.erase(std::remove(sLocal.begin(), sLocal.end(), id), sLocal.end());
   }

   // The items last copied, to paste into this or another scene.
   std::vector<Item> sBuffer;
   void CopyBuffer(Scene3DNode* scene)
   {
      sBuffer.clear();
      for (int id : sSel)
         if (const Item* it = scene->FindItem(id))
            sBuffer.push_back(*it);
   }

   void PasteBuffer(Scene3DNode* scene)
   {
      if (sBuffer.empty())
         return;
      PushUndoCheckpoint("Paste in 3D View");
      std::vector<int> made;
      for (const Item& copy : sBuffer)
      {
         Item& fresh = scene->AddItem(copy.uid);
         const int freshId = fresh.id;
         fresh = copy;
         fresh.id = freshId;
         fresh.group = 0;
         made.push_back(freshId);
      }
      scene->Commit();
      sSel = made;
   }

   void DrawSelectItems(Scene3DNode* scene)
   {
      const bool any = !sSel.empty();
      if (MenuParts::Item(T("All"), "A")) sSel = ShownIds(scene);
      if (MenuParts::Item(T("None"), "Alt+A", false, any)) sSel.clear();
      if (MenuParts::Item(T("Invert"), MODKEY "+I")) SelectInvert(scene);
      MenuParts::Separator();
      if (MenuParts::Item(T("Select Random"))) SelectRandom(scene, 0.5f);
      if (MenuParts::Item(T("Checker Deselect"), nullptr, false, sSel.size() > 1)) SelectChecker(scene);
      MenuParts::Separator();
      if (MenuParts::Item(T("Select Linked"), nullptr, false, any)) SelectLinked(scene);
      if (MenuParts::Item(T("Select Same Type"), nullptr, false, any)) SelectSameType(scene);
      if (MenuParts::Item(T("Active Camera"), nullptr, false, scene->activeCamera != 0)) SelectActiveCamera(scene);
      if (MenuParts::SubMenu(T("Select Pattern"), true))
      {
         ImGui::SetNextItemWidth(160.0f);
         if (FieldWell::InputText("##v3dpattern", sPattern, sizeof(sPattern), ImGuiInputTextFlags_EnterReturnsTrue))
            SelectPattern(scene);
         if (MenuParts::Item(T("Select")))
            SelectPattern(scene);
         ImGui::EndMenu();
      }
   }

   void DrawSnapItems(Scene3DNode* scene)
   {
      const bool any = !sSel.empty();
      if (MenuParts::Item(T("Selection to Grid"), nullptr, false, any)) SelectionToGrid(scene);
      if (MenuParts::Item(T("Selection to Cursor"), nullptr, false, any)) SelectionToCursor(scene);
      if (MenuParts::Item(T("Selection to Cursor (Keep Offset)"), nullptr, false, any)) SelectionToCursorKeepOffset(scene);
      if (MenuParts::Item(T("Selection to Active"), nullptr, false, sSel.size() > 1)) SelectionToActive(scene);
      MenuParts::Separator();
      if (MenuParts::Item(T("Cursor to Selected"), nullptr, false, any)) CursorToSelected(scene);
      if (MenuParts::Item(T("Cursor to Active"), nullptr, false, any)) CursorToActive(scene);
      if (MenuParts::Item(T("Cursor to World Origin"))) CursorToOrigin();
      if (MenuParts::Item(T("Cursor to Grid"))) SnapCursorToGrid();
   }

   void DrawViewItems(Scene3DNode* scene)
   {
      const bool any = !sSel.empty();
      if (MenuParts::SubMenu(T("Viewpoint"), true))
      {
         if (MenuParts::Item(T("Top"), "7")) ViewAxis(2);
         if (MenuParts::Item(T("Bottom"), MODKEY "+7")) ViewAxis(3);
         if (MenuParts::Item(T("Front"), "1")) ViewAxis(4);
         if (MenuParts::Item(T("Back"), MODKEY "+1")) ViewAxis(5);
         if (MenuParts::Item(T("Right"), "3")) ViewAxis(0);
         if (MenuParts::Item(T("Left"), MODKEY "+3")) ViewAxis(1);
         ImGui::EndMenu();
      }
      if (MenuParts::Item(T("Persp / Ortho"), "5"))
      {
         sOrtho = !sOrtho;
         sAutoOrtho = false;
      }
      // Only what applies right now: no greyed-out rows.
      if (any && MenuParts::Item(T("Frame selected"), "F")) FrameItems(scene, false);
      if ((any || !sLocal.empty()) && MenuParts::Item(T("Local view"), "/", !sLocal.empty())) ToggleLocal(scene);
      if (!sLocal.empty() && any && MenuParts::Item(T("Remove from local view"))) RemoveFromLocal();
      if ((any || sLockActive) && MenuParts::Item(T("Lock to active"), nullptr, sLockActive)) sLockActive = !sLockActive;
      MenuParts::Separator();
      if (MenuParts::SubMenu(T("Camera"), true))
      {
         const bool cam = scene->activeCamera != 0;
         if (MenuParts::Item(T("Align to view"), MODKEY "+Alt+0")) CameraToView(scene);
         if (cam && any && MenuParts::Item(T("Align to selected")))
         {
            float m[3];
            if (Item* c = scene->FindItem(scene->activeCamera))
               if (SelectionCenter(scene, m))
               {
                  PushUndoCheckpoint("Aim camera");
                  scene->AimAt(*c, m);
                  scene->Commit();
               }
         }
         if (sCameraView && MenuParts::Item(T("Zoom 1:1")))
         {
            sCamZoom = 1.0f;
            sCamPan = ImVec2(0, 0);
         }
         if (sCameraView && MenuParts::Item(T("Center"))) sCamPan = ImVec2(0, 0);
         ImGui::EndMenu();
      }
      if (MenuParts::Item(T("Cursor + frame all"), "Shift+C"))
      {
         CursorToOrigin();
         FrameItems(scene, true);
      }
   }

   void DrawObjectItems(Scene3DNode* scene)
   {
      const bool any = !sSel.empty();
      if (MenuParts::SubMenu(T("Snap"), true))
      {
         DrawSnapItems(scene);
         ImGui::EndMenu();
      }
      if (MenuParts::SubMenu(T("Mirror"), any))
      {
         if (MenuParts::Item(T("X Global"))) MirrorSelected(scene, 0);
         if (MenuParts::Item(T("Y Global"))) MirrorSelected(scene, 1);
         if (MenuParts::Item(T("Z Global"))) MirrorSelected(scene, 2);
         ImGui::EndMenu();
      }
      if (MenuParts::Item(T("Copy Transforms from Active"), MODKEY "+L", false, sSel.size() > 1)) CopyTransformsFromActive(scene);
      if (MenuParts::SubMenu(T("Clear"), any))
      {
         if (MenuParts::Item(T("Location"))) ClearTransform(scene, ClearWhat::Location);
         if (MenuParts::Item(T("Rotation"))) ClearTransform(scene, ClearWhat::Rotation);
         if (MenuParts::Item(T("Scale"))) ClearTransform(scene, ClearWhat::Scale);
         ImGui::EndMenu();
      }
      if (MenuParts::SubMenu(T("Align"), sSel.size() > 1))
      {
         if (MenuParts::Item(T("X"))) AlignSelected(scene, 0);
         if (MenuParts::Item(T("Y"))) AlignSelected(scene, 1);
         if (MenuParts::Item(T("Z"))) AlignSelected(scene, 2);
         ImGui::EndMenu();
      }
      if (MenuParts::Item(T("Follow Active"), "Ctrl+P", false, sSel.size() > 1)) FollowActive(scene, false);
      if (MenuParts::Item(T("Clear Follow"), "Alt+P", false, any)) FollowActive(scene, true);
      if (MenuParts::Item(T("Pose Mode"), "Ctrl+Tab", sPose.on, any || sPose.on)) TogglePose(scene);
      if (MenuParts::Item(T("Sculpt Mode"), "Ctrl+Tab", sSculpt.on, any || sSculpt.on)) ToggleSculpt(scene);
      if (MenuParts::Item(T("Edit Mode"), "Tab", sEdit.on, any || sEdit.on)) ToggleEdit(scene);
      if (MenuParts::Item(T("Add IK to Bone"), "Shift+I", false, sPose.on && sPose.joint >= 0)) PoseIk(scene, false);
      if (MenuParts::Item(T("Clear IK"), "Alt+I", false, sPose.on)) PoseIk(scene, true);
      if (MenuParts::Item(T("Weight Paint"), "W", sPose.paint, PoseArmature(scene) != nullptr)) sPose.paint = !sPose.paint;
      if (MenuParts::Item(T("Clear Weight Paint"), nullptr, false, PoseArmature(scene) != nullptr && PoseArmature(scene)->HasPaint()))
      {
         PushUndoCheckpoint("Clear weight paint");
         PoseArmature(scene)->ClearPaint();
      }
      if (MenuParts::Item(T("Copy Transform"), nullptr, false, any)) CopyGlobalTransform(scene);
      if (MenuParts::Item(T("Paste Transform"), nullptr, false, any)) PasteGlobalTransform(scene);
      if (MenuParts::Item(T("Randomize Transform"), nullptr, false, any)) RandomizeTransform(scene);
      if (MenuParts::Item(T("Move to Mouse"), nullptr, false, any)) MoveToMouse(scene);
      MenuParts::Separator();
      if (MenuParts::Item(T("Copy Items"), nullptr, false, any)) CopyBuffer(scene);
      if (MenuParts::Item(T("Paste Items"), nullptr, false, !sBuffer.empty())) PasteBuffer(scene);
      MenuParts::Separator();
      if (MenuParts::Item(T("Show All in Renders"))) ShowAllInRenders(scene);
      if (MenuParts::Item(T("Render Only This Type"), nullptr, false, any)) IsolateTypeRender(scene);
      MenuParts::Separator();
      if (MenuParts::Item(T("Hide Selected"), "H", false, any)) HideSelected(scene);
      if (MenuParts::Item(T("Hide Unselected"), "Shift+H", false, any)) HideUnselected(scene);
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
      {
         LeaveCamera(true);
         if (sPreCam.set)
         {
            sRoll = sPreCam.roll;
            GlideTo([] { WriteView(sPreCam.view); sOrtho = sPreCam.ortho; });
         }
      }
      else
         EnterCamera();
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
      EnterCamera();
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
      // Tab is locked to the node keyboard owner whenever nothing else uses it; the 3D View asks as that owner.
      auto pressed = [](ImGuiKey k) { return k == ImGuiKey_Tab ? ImGui::IsKeyPressed(k, ImGuiInputFlags_None, kKbTabOwner) : ImGui::IsKeyPressed(k, false); };
      auto digit = [&](int i) { return pressed((ImGuiKey)(ImGuiKey_0 + i)) || pressed((ImGuiKey)(ImGuiKey_Keypad0 + i)); };
      if (sOp.mode != 0)
      {
         if (sOp.mode == 'r' && !sOp.gizmo && pressed(ImGuiKey_R))
         {
            sOp.track = !sOp.track;
            sOp.start = mouse;
            sOp.axis = -1;
         }
         for (int a = 0; a < 3; a++)
            if (pressed(a == 0 ? ImGuiKey_X : a == 1 ? ImGuiKey_Z : ImGuiKey_Y))
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
         if (sSet->proportional && (pressed(ImGuiKey_PageUp) || pressed(ImGuiKey_PageDown)))
            sSet->propSize = std::clamp(sSet->propSize * (pressed(ImGuiKey_PageUp) ? 1.1f : 1.0f / 1.1f), 0.05f, 500.0f);
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
      if (io.KeyShift && !io.KeyAlt && !command && pressed(ImGuiKey_Z))
         sShading = sShading == Shading::Wire ? Shading::Solid : Shading::Wire;
      if (io.KeyAlt && !command && !io.KeyShift && pressed(ImGuiKey_Z))
         sSet->xray = !sSet->xray;
      if (io.KeyAlt && io.KeyShift && !command && pressed(ImGuiKey_Z))
         sOverlays = !sOverlays;
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
      if (command && !io.KeyShift && pressed(ImGuiKey_Tab))
      {
         // A rig poses; anything else sculpts.
         Mat4 world;
         const Item* it = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
         if (it != nullptr && PoseModel(*it, world) != nullptr)
            TogglePose(scene);
         else
            ToggleSculpt(scene);
      }
      if (!io.KeyShift && !command && !io.KeyAlt && pressed(ImGuiKey_Tab))
         ToggleEdit(scene);
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
      if (command && !io.KeyAlt && digit(0))
         if (sSel.size() == 1)
            if (Item* it = scene->FindItem(sSel[0]))
               if (scene->ItemKind(*it) == Kind::Camera)
               {
                  PushUndoCheckpoint("Set 3D View camera");
                  scene->activeCamera = it->id;
                  scene->Commit();
                  EnterCamera();
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
            GlideTo([&] { OrbitBy(scene, kPi, 0.0f); });
         else if (digit(2) || digit(8))
            GlideTo([&] { OrbitBy(scene, 0.0f, digit(8) ? RotStep() : -RotStep()); });
         else if (digit(4) || digit(6))
            GlideTo([&] { OrbitBy(scene, digit(6) ? RotStep() : -RotStep(), 0.0f); });
         else if (pressed(ImGuiKey_A))
         {
            sSel.clear();
            for (const Item& it : scene->Items())
               if (Pickable(scene, it))
                  sSel.push_back(it.id);
         }
         else if (pressed(ImGuiKey_O) && !io.KeyShift)
         {
            PushUndoCheckpoint("Proportional editing in 3D View");
            sSet->proportional = !sSet->proportional;
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
      if (plain && io.KeyShift && (pressed(ImGuiKey_Keypad4) || pressed(ImGuiKey_Keypad6)))
      {
         LeaveCamera();
         sRoll += pressed(ImGuiKey_Keypad6) ? RotStep() : -RotStep();
         sViewName = -1;
      }
      if (io.KeyAlt && pressed(ImGuiKey_A))
         sSel.clear();
      if (command && !io.KeyShift && !io.KeyAlt && pressed(ImGuiKey_I))
         SelectInvert(scene);
      if (io.KeyAlt && pressed(ImGuiKey_H))
      {
         PushUndoCheckpoint("Show in 3D View");
         for (Item& it : scene->Items())
            it.visible = true;
         scene->Commit();
      }
      if (io.KeyShift && !command && pressed(ImGuiKey_R))
         RepeatLast(scene);
      if (command && !io.KeyShift && !io.KeyAlt && pressed(ImGuiKey_L))
         CopyTransformsFromActive(scene);
      if (command && !io.KeyShift && !io.KeyAlt && pressed(ImGuiKey_P))
         FollowActive(scene, false);
      if (io.KeyAlt && !command && !io.KeyShift && pressed(ImGuiKey_P))
         FollowActive(scene, true);
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
            OrbitBy(scene, io.MouseDelta.x * kOrbitPerPixel * sSet->orbitSens, io.MouseDelta.y * kOrbitPerPixel * sSet->orbitSens);
      }
      if (sSet->continuousGrab && (hovered || sNavDrag) && orbitDrag)
      {
         // The pointer wraps at the view's edge so a drag can go on turning.
         const float mx0 = sCam.mn.x, mx1 = sCam.mx.x, my0 = sCam.mn.y, my1 = sCam.mx.y;
         ImVec2 p = io.MousePos;
         const float w = mx1 - mx0, h = my1 - my0;
         if (w > 40.0f && h > 40.0f)
         {
            if (p.x < mx0 + 2.0f) p.x += w - 4.0f; else if (p.x > mx1 - 2.0f) p.x -= w - 4.0f;
            if (p.y < my0 + 2.0f) p.y += h - 4.0f; else if (p.y > my1 - 2.0f) p.y -= h - 4.0f;
            if (p.x != io.MousePos.x || p.y != io.MousePos.y)
            {
               ImGuiIO& wio = ImGui::GetIO();
               wio.WantSetMousePos = true;
               wio.MousePos = p;
               wio.MousePosPrev = p;
            }
         }
      }
      if (hovered && sOp.mode != 0 && sSet->proportional && io.MouseWheel != 0.0f)
         sSet->propSize = std::clamp(sSet->propSize * std::pow(1.1f, -io.MouseWheel), 0.05f, 500.0f);
      else if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
      {
#ifdef __APPLE__
         const bool zoom = io.KeyCtrl || io.KeySuper;
#else
         const bool zoom = !io.KeyShift;
#endif
         if (zoom)
            Zoom(std::pow(1.0f / 1.2f, sSet->invertZoom ? -io.MouseWheel : io.MouseWheel), &io.MousePos);   // a wheel notch is 1.2x
         else if (io.KeyShift)
            Pan(io.MouseWheelH * 10.0f, io.MouseWheel * 10.0f);   // GLFW hands a trackpad over as pixels / 10: the scene follows the fingers 1:1
         else
            OrbitBy(scene, io.MouseWheelH * 10.0f * kOrbitPerPixel, io.MouseWheel * 10.0f * kOrbitPerPixel);
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

   // Axis-aligned ellipse in the plane spanned by axes a and b, around centre c with half extents h.
   void DrawEllipse3D(ImDrawList* dl, const float c[3], const float h[3], int a, int b, ImU32 col, float fixed = 0.0f, int n = 32)
   {
      float prev[3] = {};
      for (int i = 0; i <= n; i++)
      {
         const float t = (float)i / n * 2.0f * kPi;
         float q[3] = { c[0], c[1], c[2] };
         const int o = 3 - a - b;
         q[o] += fixed;
         q[a] += std::cos(t) * h[a];
         q[b] += std::sin(t) * h[b];
         if (i > 0)
            Line3D(dl, prev, q, col, 1.0f);
         std::copy(q, q + 3, prev);
      }
   }

   // Per-object viewport display: bounds shape, name and axes (Object > Viewport Display).
   // Units: what a scene unit reads as in the Location row. None shows the bare number.
   float LengthFactor()
   {
      const Scene3DNode::Settings& u = *sSet;
      if (u.unitSystem == 0)
         return 1.0f;
      static const float kMetric[4] = { 1.0f, 100.0f, 1000.0f, 0.001f };
      static const float kImperial[3] = { 3.28084f, 39.3701f, 1.09361f };
      const float f = u.unitSystem == 1 ? kMetric[std::clamp(u.lengthUnit, 0, 3)] : kImperial[std::clamp(u.lengthUnit, 0, 2)];
      return f * u.unitScale;
   }

   const char* LengthFormat()
   {
      const Scene3DNode::Settings& u = *sSet;
      static const char* kMetric[4] = { "%.2f m", "%.2f cm", "%.2f mm", "%.4f km" };
      static const char* kImperial[3] = { "%.2f ft", "%.2f in", "%.2f yd" };
      if (u.unitSystem == 0)
         return "%.2f";
      return u.unitSystem == 1 ? kMetric[std::clamp(u.lengthUnit, 0, 3)] : kImperial[std::clamp(u.lengthUnit, 0, 2)];
   }

   void DashedLine(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 col)
   {
      const float len = std::hypot(q.x - p.x, q.y - p.y), step = 4.0f;
      for (float t = 0.0f; t < len; t += step * 2.0f)
      {
         const float e = std::min(len, t + step);
         dl->AddLine(ImVec2(p.x + (q.x - p.x) * t / len, p.y + (q.y - p.y) * t / len),
                     ImVec2(p.x + (q.x - p.x) * e / len, p.y + (q.y - p.y) * e / len), col);
      }
   }

   void DrawItemExtras(ImDrawList* dl, Scene3DNode* scene, const Item& it, ImU32 col)
   {
      float c[3];
      scene->ItemCenter(it, c);
      if (const Item* target = it.follow != 0 && sSet->showExtras ? scene->FindItem(it.follow) : nullptr)
      {
         float tc[3];
         ImVec2 a, b;
         scene->ItemCenter(*target, tc);
         if (Project(c, a) && Project(tc, b))
            DashedLine(dl, a, b, Hex(0x8FB0FF, 0.6f));   // relationship line, follower to target
      }
      if (it.showBounds)
      {
         float lo[3], hi[3];
         if (scene->ItemBounds(it.id, lo, hi))
         {
            const float m[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
            const float h[3] = { (hi[0] - lo[0]) * 0.5f, (hi[1] - lo[1]) * 0.5f, (hi[2] - lo[2]) * 0.5f };
            const ImU32 bc = Hex(0xFFFFFF, 0.55f);
            switch (it.boundsType)
            {
            case 1:
               for (int a = 0; a < 3; a++)
                  DrawEllipse3D(dl, m, h, a, (a + 1) % 3, bc);
               break;
            case 2:
            case 4:
               DrawEllipse3D(dl, m, h, 0, 2, bc, h[1]);
               DrawEllipse3D(dl, m, h, 0, 2, bc, -h[1]);
               for (int k = 0; k < 4; k++)
               {
                  float p[3] = { m[0] + ((k & 1) ? h[0] : -h[0]) * ((k & 2) ? 0.0f : 1.0f), m[1] - h[1], m[2] + ((k & 2) ? ((k & 1) ? h[2] : -h[2]) : 0.0f) };
                  float q[3] = { p[0], m[1] + h[1], p[2] };
                  Line3D(dl, p, q, bc, 1.0f);
               }
               if (it.boundsType == 4)
               {
                  DrawEllipse3D(dl, m, h, 0, 1, bc);
                  DrawEllipse3D(dl, m, h, 2, 1, bc);
               }
               break;
            case 3:
               DrawEllipse3D(dl, m, h, 0, 2, bc, -h[1]);
               for (int k = 0; k < 4; k++)
               {
                  float p[3] = { m[0] + ((k & 2) ? 0.0f : ((k & 1) ? h[0] : -h[0])), m[1] - h[1], m[2] + ((k & 2) ? ((k & 1) ? h[2] : -h[2]) : 0.0f) };
                  float apex[3] = { m[0], m[1] + h[1], m[2] };
                  Line3D(dl, p, apex, bc, 1.0f);
               }
               break;
            default:
               DrawBox(dl, lo, hi, bc);
               break;
            }
         }
      }
      if (it.showAxes)
      {
         const float len = UnitAt(c) * 40.0f;
         for (int a = 0; a < 3; a++)
         {
            float w[3];
            OrientAxis(it, 1, a, w);
            const float e[3] = { c[0] + w[0] * len, c[1] + w[1] * len, c[2] + w[2] * len };
            Line3D(dl, c, e, AxisColor(a, 0.95f), 1.5f);
         }
      }
      if (it.showName)
      {
         ImVec2 sp;
         if (Project(c, sp))
         {
            const std::string nm = ItemName(it);
            dl->AddText(ImVec2(sp.x + 8.0f, sp.y - 16.0f), col, nm.c_str());
         }
      }
   }

   void TransformBy(const Mat4& t, const float p[3], float out[3]);

   struct ItemHandle
   {
      int id = 0, kind = 0;   // 1 light size, 2 light look-at, 3 camera lens, 4 camera focus distance
   };
   ItemHandle sHandle;

   // Handles on the active camera or light (Gizmos > Light, Camera): small dots to drag. True while the mouse is on
   // one or dragging one, so the click does not also select.
   bool DrawItemHandles(ImDrawList* dl, Scene3DNode* scene, ImVec2 mouse, bool canGrab)
   {
      const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      if (sHandle.kind != 0 && !down)
         sHandle = {};
      if (sSel.empty() || !sOverlays)
         return false;
      Item* it = scene->FindItem(sSel.back());
      GraphNode* gn = it != nullptr ? FindNodeByUid(it->uid) : nullptr;
      if (gn == nullptr || !it->visible)
         return false;
      auto* cam = dynamic_cast<CameraNode*>(gn->node.get());
      auto* light = dynamic_cast<LightNode*>(gn->node.get());
      const Mat4 world = Scene3DNode::Placement(*it);
      auto at = [&](float x, float y, float z, float out[3]) { const float q[3] = { x, y, z }; TransformBy(world, q, out); };
      float o[3], d[3];
      MouseRay(mouse, o, d);
      bool onHandle = sHandle.kind != 0;
      auto handle = [&](int kind, const float w[3], ImU32 col)
      {
         ImVec2 sp;
         if (!Project(w, sp))
            return false;
         const float dist = std::hypot(mouse.x - sp.x, mouse.y - sp.y);
         const bool hot = canGrab && dist < 8.0f;
         dl->AddCircleFilled(sp, hot || (sHandle.kind == kind && sHandle.id == it->id) ? 6.0f : 4.5f, col, 16);
         dl->AddCircle(sp, 6.5f, Hex(0x000000, 0.6f), 16, 1.0f);
         if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && sHandle.kind == 0)
         {
            PushUndoCheckpoint("Drag handle");
            sHandle = { it->id, kind };
         }
         onHandle = onHandle || hot;
         return sHandle.kind == kind && sHandle.id == it->id;
      };
      float c[3], right[3], axis[3];
      at(0, 0, 0, c);
      at(1, 0, 0, right);
      at(0, 0, -1, axis);
      for (int k = 0; k < 3; k++)
      {
         right[k] -= c[k];
         axis[k] -= c[k];
      }
      Normalize(right);
      Normalize(axis);
      const ImU32 hcol = Hex(0xE8C840);
      if (cam != nullptr && sSet->gizmoCamLens)
      {
         const float dpt = 2.0f * cam->displaySize;
         float w[3];
         at(dpt * std::tan(cam->fov * 0.5f * kPi / 180.0f), 0, -dpt, w);
         if (handle(3, w, hcol))
         {
            float base[3];
            at(0, 0, -dpt, base);
            const float x = std::abs(LineParam(base, right, o, d));
            cam->fov = std::clamp(2.0f * std::atan(x / dpt) * 180.0f / kPi, 1.0f, 170.0f);
            scene->Commit();
         }
      }
      if (cam != nullptr && sSet->gizmoCamFocus && it->id == scene->activeCamera)
      {
         float w[3];
         at(0, 0, -scene->Look().focusDistance, w);
         if (handle(4, w, Hex(0x40C8E8)))
         {
            scene->Look().focusDistance = std::clamp(LineParam(c, axis, o, d), 0.05f, 100.0f);
            scene->Commit();
         }
      }
      float aim[3];
      if (light != nullptr && scene->ItemLightAim(*it, aim))
      {
         float u[3];
         const float ref[3] = { std::abs(aim[1]) > 0.9f ? 1.0f : 0.0f, std::abs(aim[1]) > 0.9f ? 0.0f : 1.0f, 0.0f };
         Cross(aim, ref, u);
         Normalize(u);
         if (sSet->gizmoLightSize && (light->type == 4 || light->type == 5))
         {
            float w[3];
            for (int k = 0; k < 3; k++)
               w[k] = c[k] + (light->type == 4 ? aim[k] + u[k] * std::tan(light->spotAngle * kPi / 180.0f) : u[k] * light->areaWidth * 0.5f);
            if (handle(1, w, hcol))
            {
               const float tip[3] = { c[0] + aim[0], c[1] + aim[1], c[2] + aim[2] };
               const float x = std::abs(LineParam(light->type == 4 ? tip : c, u, o, d));
               if (light->type == 4)
                  light->spotAngle = std::clamp(std::atan(x) * 180.0f / kPi, 1.0f, 89.0f);
               else
                  light->areaWidth = std::clamp(x * 2.0f, 0.05f, 20.0f);
               scene->Commit();
            }
         }
         if (sSet->gizmoLightLook && light->type != 1 && light->type != 3)
         {
            float w[3];
            const float reach = std::max(1.5f, light->distance * 0.5f);
            for (int k = 0; k < 3; k++)
               w[k] = c[k] + aim[k] * reach;
            if (handle(2, w, Hex(0xE86840)))
            {
               float hit[3];
               if (PlaneHit(w, sCam.fwd, o, d, hit) && scene->AimAt(*it, hit))
                  scene->Commit();
            }
         }
      }
      return onHandle;
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

   // Pose mode (Ctrl+Tab on a skinned Model 3D item, Blender's key): the
   // joints drawn as bones over the model, a click picks one, R turns it about
   // the view axis, Alt+R clears it. A turn is stored on the Model 3D node as
   // a pose slot (three params), so it saves, undoes and can be modulated.

   // The rigged node under an item (Armature, or a skinned Model 3D, through
   // any pass-through nodes; the one nearest the view wins) and the matrix
   // from its mesh space to the view's world.
   IRigged* PoseModel(const Item& it, Mat4& world)
   {
      GraphNode* gn = FindNodeByUid(it.uid);
      auto* top = gn != nullptr ? dynamic_cast<IGeometrySource*>(gn->node.get()) : nullptr;
      IRigged* model = nullptr;
      for (IGeometrySource* s = top; s != nullptr && model == nullptr; s = s->PassthroughSource())
         model = dynamic_cast<IRigged*>(s);
      if (model == nullptr || !model->Poseable())
         return nullptr;
      world = Mat4::Multiply(Scene3DNode::Placement(it), top->GetModelMatrix());
      return model;
   }

   IRigged* PoseTarget(Scene3DNode* scene, Mat4& world)
   {
      if (!sPose.on)
         return nullptr;
      const Item* it = scene->FindItem(sPose.item);
      IRigged* model = it != nullptr && it->visible ? PoseModel(*it, world) : nullptr;
      if (model == nullptr)
         sPose = {};
      return model;
   }

   ArmatureNode* PoseArmature(Scene3DNode* scene)
   {
      Mat4 world;
      return dynamic_cast<ArmatureNode*>(PoseTarget(scene, world));
   }

   // Only one Armature is tinted by its weights at a time, and none outside
   // weight paint. Found by uid, so a removed node is never touched.
   uint64_t sTintUid = 0;
   void SetWeightTint(ArmatureNode* arm, int bone)
   {
      uint64_t uid = 0;
      if (arm != nullptr)
         for (GraphNode& gn : gNodes)
            if (gn.node.get() == arm)
               uid = gn.uid;
      if (sTintUid != 0 && sTintUid != uid)
         if (GraphNode* old = FindNodeByUid(sTintUid))
            if (auto* a = dynamic_cast<ArmatureNode*>(old->node.get()))
               a->paintBone = -1;
      sTintUid = uid;
      if (arm != nullptr)
         arm->paintBone = bone;
   }

   // One dab: the front-facing points inside the circle move toward the
   // target, most at the middle.
   void PaintWeights(ArmatureNode* arm, const Mat4& world, ImVec2 mouse, int bone, float target)
   {
      const std::vector<float>& pts = arm->PosedPoints();
      const std::vector<float>& nrm = arm->PosedNormals();
      const float r = sPose.radius;
      std::vector<std::pair<unsigned int, float>> hits;
      for (size_t i = 0; i * 3 + 2 < pts.size() && i * 3 + 2 < nrm.size(); i++)
      {
         float w[3];
         ImVec2 at;
         TransformBy(world, &pts[i * 3], w);
         if (!Project(w, at))
            continue;
         const float d = std::hypot(at.x - mouse.x, at.y - mouse.y);
         if (d >= r)
            continue;
         const float* n = &nrm[i * 3];
         if (n[0] != 0.0f || n[1] != 0.0f || n[2] != 0.0f)
         {
            float wn[3];
            for (int k = 0; k < 3; k++)
               wn[k] = world.m[k] * n[0] + world.m[4 + k] * n[1] + world.m[8 + k] * n[2];
            float toEye[3];
            for (int k = 0; k < 3; k++)
               toEye[k] = sCam.ortho ? -sCam.fwd[k] : sCam.eye[k] - w[k];
            if (wn[0] * toEye[0] + wn[1] * toEye[1] + wn[2] * toEye[2] <= 0.0f)
               continue;
         }
         const float x = 1.0f - d / r;
         hits.push_back({ (unsigned int)i, 0.15f * x * x * (3.0f - 2.0f * x) });
      }
      arm->Paint(hits, bone, target);
      sPose.lastDab = mouse;
   }

   void TogglePose(Scene3DNode* scene)
   {
      if (sPose.on)
      {
         sPose = {};
         return;
      }
      Mat4 world;
      const Item* it = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      if (it != nullptr && PoseModel(*it, world) != nullptr)
      {
         sPose = {};
         sPose.on = true;
         sPose.item = it->id;
      }
   }

   bool JointHead(IRigged* model, const Mat4& world, int j, float out[3])
   {
      float m[16];
      if (!model->JointMatrix(j, m))
         return false;
      const float p[3] = { m[12], m[13], m[14] };
      TransformBy(world, p, out);
      return true;
   }

   void EndPoseTurn(IRigged* model, bool keep)
   {
      if (!keep && sPose.slot >= 0)
      {
         std::copy(sPose.before, sPose.before + 3, model->Pose().poseRot[sPose.slot]);
         if (sPose.madeSlot)
            model->Pose().poseJoint[sPose.slot] = -1;
      }
      sPose.turning = false;
      sPose.slot = -1;
   }

   // Shift+I (Blender's Add IK > To Active Bone): the picked bone reaches
   // for the last other selected item; one more selected item becomes the
   // pole. With nothing else selected the target starts on the bone's head,
   // to be typed or modulated in the node. Alt+I clears it.
   void PoseIk(Scene3DNode* scene, bool clear)
   {
      Mat4 world;
      IRigged* model = PoseTarget(scene, world);
      Item* it = model != nullptr ? scene->FindItem(sPose.item) : nullptr;
      if (it == nullptr || (!clear && (sPose.joint < 0 || sPose.joint >= model->JointCount())))
         return;
      PushUndoCheckpoint(clear ? "Clear IK" : "Add IK");
      if (clear)
      {
         model->Pose().ikOn = false;
         it->ikTarget = it->ikPole = 0;
         scene->Commit();
         return;
      }
      std::vector<int> others;
      for (int id : sSel)
         if (id != it->id)
            others.push_back(id);
      it->ikTarget = others.empty() ? 0 : others.back();
      it->ikPole = others.size() > 1 ? others[others.size() - 2] : 0;
      float m[16];
      if (it->ikTarget == 0 && model->JointMatrix(sPose.joint, m))
         std::copy(m + 12, m + 15, model->Pose().ikTarget);
      model->Pose().ikJoint = sPose.joint;
      model->Pose().ikPoleOn = it->ikPole != 0;
      model->Pose().ikOn = true;
      scene->Commit();
   }

   // Draws the bones and runs the mode's clicks and keys. Returns true while
   // the mode owns the mouse and keys (navigation still runs).
   bool DrawPose(ImDrawList* dl, Scene3DNode* scene, ImVec2 mouse, bool hovered)
   {
      Mat4 world;
      IRigged* model = PoseTarget(scene, world);
      ArmatureNode* arm = dynamic_cast<ArmatureNode*>(model);
      if (arm == nullptr || !arm->Paintable())
         sPose.paint = sPose.stroking = false;
      const int count = model != nullptr ? model->JointCount() : 0;
      // The last joint is the chain's tip, which carries no weight.
      const int paintBone = sPose.paint && sPose.joint >= 0 && sPose.joint < count - 1 ? sPose.joint : -1;
      SetWeightTint(sPose.paint ? arm : nullptr, paintBone);
      if (model == nullptr)
         return false;
      const ImGuiIO& io = ImGui::GetIO();
      // Tab is locked to the node keyboard owner whenever nothing else uses it; the 3D View asks as that owner.
      auto pressed = [](ImGuiKey k) { return k == ImGuiKey_Tab ? ImGui::IsKeyPressed(k, ImGuiInputFlags_None, kKbTabOwner) : ImGui::IsKeyPressed(k, false); };

      // Bones: parent head to child head belongs to the parent (turning the
      // parent swings it); a joint with no children gets a short tail along
      // its own Y, Blender's bone axis.
      std::vector<ImVec2> heads(count);
      std::vector<bool> shown(count, false), hasChild(count, false);
      for (int j = 0; j < count; j++)
      {
         float h[3];
         shown[j] = JointHead(model, world, j, h) && Project(h, heads[j]);
         if (model->JointParent(j) >= 0)
            hasChild[model->JointParent(j)] = true;
      }
      int hotJoint = -1;
      float hotDist = 9.0f;
      auto bone = [&](int owner, ImVec2 a, ImVec2 b)
      {
         const bool sel = owner == sPose.joint;
         dl->AddLine(a, b, Hex(0x000000, 0.6f), 4.0f);
         dl->AddLine(a, b, sel ? Hex(kActive) : Hex(kWire, 0.85f), 2.0f);
         const float d = SegDist(mouse, a, b);
         if (d < hotDist)
         {
            hotDist = d;
            hotJoint = owner;
         }
      };
      for (int j = 0; j < count; j++)
      {
         if (!shown[j])
            continue;
         const int parent = model->JointParent(j);
         if (parent >= 0 && shown[parent])
            bone(parent, heads[parent], heads[j]);
         if (!hasChild[j])
         {
            float m[16], tip[3], h[3];
            model->JointMatrix(j, m);
            const float len = 0.08f;
            const float local[3] = { m[12] + m[4] * len, m[13] + m[5] * len, m[14] + m[6] * len };
            TransformBy(world, local, tip);
            ImVec2 t;
            if (JointHead(model, world, j, h) && Project(tip, t))
               bone(j, heads[j], t);
         }
      }
      for (int j = 0; j < count; j++)
      {
         if (!shown[j])
            continue;
         const float d = std::hypot(mouse.x - heads[j].x, mouse.y - heads[j].y);
         if (d < 7.0f)
         {
            hotDist = 0.0f;
            hotJoint = j;
         }
         dl->AddCircleFilled(heads[j], j == sPose.joint ? 4.5f : 3.5f, j == sPose.joint ? Hex(kActive) : Hex(kWire), 12);
         dl->AddCircle(heads[j], 5.0f, Hex(0x000000, 0.6f), 12, 1.0f);
      }
      if (sPose.joint >= 0 && sPose.joint < count && shown[sPose.joint])
      {
         const std::string& name = model->JointName(sPose.joint);
         dl->AddText(ImVec2(heads[sPose.joint].x + 8.0f, heads[sPose.joint].y - 18.0f), Hex(kHotHandle), name.c_str());
      }
      // The IK goal: a line from the reaching joint to its target.
      if (model->Pose().ikOn && model->Pose().ikJoint >= 0 && model->Pose().ikJoint < count && shown[model->Pose().ikJoint])
      {
         float t[3];
         ImVec2 tp;
         TransformBy(world, model->Pose().ikTarget, t);
         if (Project(t, tp))
         {
            dl->AddLine(heads[model->Pose().ikJoint], tp, Hex(kHotHandle, 0.8f), 1.5f);
            dl->AddCircle(tp, 6.0f, Hex(kHotHandle), 16, 1.5f);
         }
      }
      dl->AddText(ImVec2(sCam.mn.x + 12.0f, sCam.mx.y - 26.0f), Hex(kActive), sPose.paint ? "Weight Paint" : "Pose Mode");
      if (sPose.paint && hovered)
         dl->AddCircle(mouse, sPose.radius, Hex(kWire, 0.9f), 48, 1.5f);

      if (sPose.stroking)
      {
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || paintBone < 0)
            sPose.stroking = false;
         else if (std::hypot(mouse.x - sPose.lastDab.x, mouse.y - sPose.lastDab.y) >= 2.0f)
            PaintWeights(arm, world, mouse, paintBone, (io.KeyCtrl || io.KeySuper) ? 0.0f : 1.0f);
         return true;
      }

      if (sPose.turning)
      {
         float h[3];
         ImVec2 c;
         if (sPose.slot < 0 || !JointHead(model, world, sPose.joint, h) || !Project(h, c))
         {
            EndPoseTurn(model, false);
            return true;
         }
         dl->AddLine(c, mouse, Hex(kWire, 0.7f));
         const float angle = std::atan2(mouse.y - c.y, mouse.x - c.x) - sPose.startAngle;
         const float half = angle * 0.5f;
         const float turn[4] = { sPose.axis[0] * std::sin(half), sPose.axis[1] * std::sin(half), sPose.axis[2] * std::sin(half), std::cos(half) };
         float q[4];
         Skinning::QuatMul(sPose.q0, turn, q);
         Skinning::EulerFromQuat(q, model->Pose().poseRot[sPose.slot]);
         if (pressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            EndPoseTurn(model, false);
         else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            EndPoseTurn(model, true);
         return true;
      }

      if (!hovered || io.WantTextInput)
         return true;
      const bool command = io.KeyCtrl || io.KeySuper;
      if ((command && pressed(ImGuiKey_Tab)) || pressed(ImGuiKey_Escape))
      {
         sPose = {};
         return true;
      }
      if (arm != nullptr && !command && !io.KeyShift && !io.KeyAlt && pressed(ImGuiKey_W) && arm->Paintable())
         sPose.paint = !sPose.paint;
      if (sPose.paint)
      {
         // [ and ] size the brush; a click on a bone picks it, anywhere else paints
         // (Ctrl takes weight away).
         if (pressed(ImGuiKey_LeftBracket))
            sPose.radius = std::max(4.0f, sPose.radius / 1.15f);
         if (pressed(ImGuiKey_RightBracket))
            sPose.radius = std::min(400.0f, sPose.radius * 1.15f);
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
         {
            if (hotJoint >= 0)
               sPose.joint = hotJoint;
            else if (paintBone >= 0)
            {
               PushUndoCheckpoint("Weight paint");
               sPose.stroking = true;
               PaintWeights(arm, world, mouse, paintBone, command ? 0.0f : 1.0f);
            }
         }
         return true;
      }
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
         sPose.joint = hotJoint;
      if (sPose.joint < 0 || sPose.joint >= count)
         return true;
      if (!command && pressed(ImGuiKey_I) && (io.KeyShift || io.KeyAlt))
      {
         PoseIk(scene, io.KeyAlt);
         return true;
      }
      if (io.KeyAlt && !command && pressed(ImGuiKey_R))
      {
         const int slot = model->Pose().Slot(sPose.joint, false);
         if (slot >= 0)
         {
            PushUndoCheckpoint("Clear pose");
            model->Pose().poseJoint[slot] = -1;
            std::fill(model->Pose().poseRot[slot], model->Pose().poseRot[slot] + 3, 0.0f);
         }
      }
      else if (!io.KeyAlt && !command && pressed(ImGuiKey_R))
      {
         float h[3], m[16];
         ImVec2 c;
         if (!JointHead(model, world, sPose.joint, h) || !Project(h, c) || !model->JointMatrix(sPose.joint, m))
            return true;
         PushUndoCheckpoint("Pose joint");
         sPose.madeSlot = model->Pose().Slot(sPose.joint, false) < 0;
         sPose.slot = model->Pose().Slot(sPose.joint, true);
         if (sPose.slot < 0)
            return true;
         std::copy(model->Pose().poseRot[sPose.slot], model->Pose().poseRot[sPose.slot] + 3, sPose.before);
         Skinning::QuatFromEuler(sPose.before, sPose.q0);
         // The view axis, into mesh space, then into the joint's frame.
         const Mat4 inv = Mat4::Inverse(world);
         float a[3];
         for (int k = 0; k < 3; k++)
            a[k] = inv.m[k] * sCam.fwd[0] + inv.m[4 + k] * sCam.fwd[1] + inv.m[8 + k] * sCam.fwd[2];
         for (int c3 = 0; c3 < 3; c3++)
         {
            float col[3] = { m[c3 * 4], m[c3 * 4 + 1], m[c3 * 4 + 2] };
            Normalize(col);
            sPose.axis[c3] = col[0] * a[0] + col[1] * a[1] + col[2] * a[2];
         }
         Normalize(sPose.axis);
         sPose.startAngle = std::atan2(mouse.y - c.y, mouse.x - c.x);
         sPose.turning = true;
      }
      return true;
   }

   // Sculpt mode (Ctrl+Tab on an item with no rig, or the brush chip):
   // brushes paint the offsets held by the item's Sculpt node (one is
   // added after the item's node if it has none). Each stroke is one undo
   // step. Blender's keys: X Draw, S Smooth, G Grab, I Inflate, P Pinch,
   // Shift+T Flatten, Shift+C Crease; F radius, Shift+F strength; Ctrl
   // inverts, Shift smooths.

   // The Sculpt node under an item (the one nearest the view) and the
   // matrix from its mesh space to the view's world.
   SculptNode* SculptOf(const Item& it, Mat4& world)
   {
      GraphNode* gn = FindNodeByUid(it.uid);
      auto* top = gn != nullptr ? dynamic_cast<IGeometrySource*>(gn->node.get()) : nullptr;
      SculptNode* node = nullptr;
      for (IGeometrySource* s = top; s != nullptr && node == nullptr; s = s->PassthroughSource())
         node = dynamic_cast<SculptNode*>(s);
      if (node == nullptr)
         return nullptr;
      world = Mat4::Multiply(Scene3DNode::Placement(it), top->GetModelMatrix());
      return node;
   }

   SculptNode* SculptTarget(Scene3DNode* scene, Mat4& world)
   {
      if (!sSculpt.on)
         return nullptr;
      const Item* it = scene->FindItem(sSculpt.item);
      SculptNode* node = it != nullptr && it->visible ? SculptOf(*it, world) : nullptr;
      if (node == nullptr)
      {
         const SculptState keep = sSculpt;
         sSculpt = SculptState();
         sSculpt.brush = keep.brush;
         sSculpt.radius = keep.radius;
         sSculpt.strength = keep.strength;
         sSculpt.symmetry = keep.symmetry;
      }
      return node;
   }

   void ToggleSculpt(Scene3DNode* scene)
   {
      if (sSculpt.on)
      {
         Mat4 world;
         if (SculptNode* node = sSculpt.stroking ? SculptTarget(scene, world) : nullptr)
            node->EndStroke();
         sSculpt.on = sSculpt.stroking = false;
         sSculpt.adjust = 0;
         return;
      }
      Mat4 world;
      const Item* it = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      GraphNode* gn = it != nullptr ? FindNodeByUid(it->uid) : nullptr;
      if (gn == nullptr || dynamic_cast<IGeometrySource*>(gn->node.get()) == nullptr)
         return;
      sPose = {};
      sEdit.on = false;
      sEdit.op = 0;
      if (SculptOf(*it, world) != nullptr)
      {
         sSculpt.on = true;
         sSculpt.item = it->id;
         return;
      }
      sPending.what = Pending::Sculpt;
      sPending.item = it->id;
   }

   // A new Sculpt between the item's node and the view, then Sculpt mode on it.
   void AddSculpt(Scene3DNode* scene, int itemId)
   {
      Item* item = scene->FindItem(itemId);
      GraphNode* src = item != nullptr ? FindNodeByUid(item->uid) : nullptr;
      if (src == nullptr || dynamic_cast<IGeometrySource*>(src->node.get()) == nullptr)
         return;
      PushUndoCheckpoint("Add Sculpt");
      const int srcIndex = src->index;
      const ImVec2 at = NodePos(*src);
      GraphNode* made = SpawnNode("Sculpt", "3D", at.x + 320.0f, at.y);   // can move gNodes: src is stale after this
      std::string error;
      if (made == nullptr || dynamic_cast<SculptNode*>(made->node.get()) == nullptr ||
          !ConnectNodes(srcIndex, 0, made->index, 0, error))
         return;
      item->uid = made->uid;
      scene->Commit();
      sSculpt.on = true;
      sSculpt.item = itemId;
   }

   void LinearBy(const Mat4& t, const float v[3], float out[3])
   {
      for (int k = 0; k < 3; k++)
         out[k] = t.m[k] * v[0] + t.m[4 + k] * v[1] + t.m[8 + k] * v[2];
   }

   // The mouse on the sculpted surface: mesh-space and world points.
   bool SculptHit(SculptNode* node, const Mat4& world, ImVec2 mouse, float mesh[3], float worldHit[3])
   {
      float o[3], d[3], mo[3], md[3], t;
      MouseRay(mouse, o, d);
      const Mat4 inv = Mat4::Inverse(world);
      TransformBy(inv, o, mo);
      LinearBy(inv, d, md);
      if (!node->Raycast(mo, md, t))
         return false;
      for (int k = 0; k < 3; k++)
      {
         mesh[k] = mo[k] + md[k] * t;
         worldHit[k] = o[k] + d[k] * t;
      }
      return true;
   }

   float WorldScale(const Mat4& w)
   {
      float s = 0.0f;
      for (int c = 0; c < 3; c++)
         s += std::sqrt(w.m[c * 4] * w.m[c * 4] + w.m[c * 4 + 1] * w.m[c * 4 + 1] + w.m[c * 4 + 2] * w.m[c * 4 + 2]);
      return std::max(1e-6f, s / 3.0f);
   }

   // The node's brush image, laid on the view: the view's axes in mesh space.
   void SculptAlpha(SculptNode* node, const Mat4& world, Sculpt::Dab& d)
   {
      d.alpha = node->BrushAlpha();
      if (d.alpha == nullptr)
         return;
      const Mat4 inv = Mat4::Inverse(world);
      LinearBy(inv, sCam.right, d.right);
      LinearBy(inv, sCam.up, d.up);
      Normalize(d.right);
      Normalize(d.up);
   }

   void SculptDab(SculptNode* node, const Mat4& world, ImVec2 at)
   {
      float c[3], w[3];
      if (!SculptHit(node, world, at, c, w))
         return;
      Sculpt::Dab d;
      d.brush = ImGui::GetIO().KeyShift ? (int)Sculpt::kSmooth : sSculpt.brush;
      std::copy(c, c + 3, d.center);
      d.radius = sSculpt.radius * UnitAt(w) / WorldScale(world);
      d.strength = sSculpt.strength;
      d.invert = ImGui::GetIO().KeyCtrl;
      d.symmetry = sSculpt.symmetry;
      SculptAlpha(node, world, d);
      d.areaNormals = node->StrokeNormals();
      node->StrokeStep(c, d.dir);
      static std::vector<unsigned int> touched;
      touched.clear();
      const Sculpt::Grid* grid = node->PointGrid(d.radius);
      Sculpt::Stroke(d, node->PointPos(), node->PointNrm(), node->Topology(), std::clamp(node->amount, 0.0f, 2.0f),
                     node->Offsets(), &touched, grid);
      node->OffsetsChanged(&touched);
   }

   // Draws the brush and its bar, runs strokes and keys. Returns true while
   // the mode owns the mouse and keys (navigation still runs).
   bool DrawSculpt(ImDrawList* dl, Scene3DNode* scene, ImVec2 mouse, bool hovered)
   {
      Mat4 world;
      SculptNode* node = SculptTarget(scene, world);
      if (node == nullptr)
         return false;
      const ImGuiIO& io = ImGui::GetIO();
      // Tab is locked to the node keyboard owner whenever nothing else uses it; the 3D View asks as that owner.
      auto pressed = [](ImGuiKey k) { return k == ImGuiKey_Tab ? ImGui::IsKeyPressed(k, ImGuiInputFlags_None, kKbTabOwner) : ImGui::IsKeyPressed(k, false); };
      const bool ok = node->Sculptable();

      // The bar: brushes, then X Y Z symmetry, bottom left.
      bool onBar = false;
      {
         float x = sCam.mn.x + 12.0f;
         const float y = sCam.mx.y - 54.0f, h = ImGui::GetFontSize() + 8.0f;
         auto pill = [&](const char* label, bool on) {
            const ImVec2 ts = ImGui::CalcTextSize(label);
            const ImVec2 a(x, y), b(x + ts.x + 14.0f, y + h);
            const bool hot = hovered && mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y;
            dl->AddRectFilled(a, b, Hex(on ? kSelected : 0x000000, hot || on ? 0.85f : 0.45f), h * 0.5f);
            dl->AddText(ImVec2(a.x + 7.0f, a.y + 4.0f), Hex(0xFFFFFF), label);
            x = b.x + 4.0f;
            onBar = onBar || hot;
            return hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
         };
         for (int b = 0; b < Sculpt::kBrushCount; b++)
            if (pill(Sculpt::BrushName(b), sSculpt.brush == b))
               sSculpt.brush = b;
         x += 8.0f;
         const char* axes[3] = { "X", "Y", "Z" };
         for (int k = 0; k < 3; k++)
            if (pill(axes[k], (sSculpt.symmetry >> k & 1) != 0))
               sSculpt.symmetry ^= 1 << k;
      }
      char label[96];
      snprintf(label, sizeof(label), ok ? "Sculpt Mode   %.0f px   %.2f" : "Sculpt Mode   Topology changed", sSculpt.radius,
               sSculpt.strength);
      dl->AddText(ImVec2(sCam.mn.x + 12.0f, sCam.mx.y - 26.0f), Hex(kActive), label);

      // F / Shift+F: drag the radius or strength, click to keep, Esc to undo.
      if (sSculpt.adjust != 0)
      {
         const float dist = std::hypot(mouse.x - sSculpt.adjustAt.x, mouse.y - sSculpt.adjustAt.y);
         if (sSculpt.adjust == 'r')
            sSculpt.radius = std::clamp(dist, 2.0f, 1000.0f);
         else
            sSculpt.strength = std::clamp(dist / 200.0f, 0.0f, 1.0f);
         dl->AddCircle(sSculpt.adjustAt, sSculpt.adjust == 'r' ? sSculpt.radius : sSculpt.strength * 200.0f, Hex(kHotHandle), 64, 1.5f);
         if (pressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            (sSculpt.adjust == 'r' ? sSculpt.radius : sSculpt.strength) = sSculpt.adjustBefore;
            sSculpt.adjust = 0;
         }
         else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            sSculpt.adjust = 0;
         return true;
      }

      if (sSculpt.stroking)
      {
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !ok)
         {
            sSculpt.stroking = false;
            node->EndStroke();
            return true;
         }
         const float amount = std::clamp(node->amount, 0.0f, 2.0f);
         if (sSculpt.brush == Sculpt::kGrab && !io.KeyShift)
         {
            float o[3], d[3];
            MouseRay(mouse, o, d);
            const float den = d[0] * sCam.fwd[0] + d[1] * sCam.fwd[1] + d[2] * sCam.fwd[2];
            if (std::fabs(den) > 1e-6f)
            {
               const float* h = sSculpt.grabWorld;
               const float t = ((h[0] - o[0]) * sCam.fwd[0] + (h[1] - o[1]) * sCam.fwd[1] + (h[2] - o[2]) * sCam.fwd[2]) / den;
               const float mw[3] = { o[0] + d[0] * t - h[0], o[1] + d[1] * t - h[1], o[2] + d[2] * t - h[2] };
               float mv[3];
               LinearBy(Mat4::Inverse(world), mw, mv);
               Sculpt::GrabMove(sSculpt.grab, mv, amount, node->Offsets());
               node->OffsetsChanged(&sSculpt.grab.idx);
            }
         }
         else
         {
            // Dabs along the drag.
            const float step = std::max(2.0f, sSculpt.radius * 0.15f);
            const float dist = std::hypot(mouse.x - sSculpt.last.x, mouse.y - sSculpt.last.y);
            const int n = (int)(dist / step);
            const ImVec2 from = sSculpt.last;
            for (int i = 1; i <= n; i++)
            {
               const float f = (float)i * step / dist;
               sSculpt.last = ImVec2(from.x + (mouse.x - from.x) * f, from.y + (mouse.y - from.y) * f);
               SculptDab(node, world, sSculpt.last);
            }
         }
      }

      if (hovered && !onBar)
         dl->AddCircle(mouse, sSculpt.radius, io.KeyCtrl ? Hex(0x60A0FF) : Hex(kHotHandle, 0.9f), 64, 1.5f);
      if (sSculpt.stroking || !hovered || io.WantTextInput)
         return true;
      const bool command = io.KeyCtrl || io.KeySuper;
      if ((command && pressed(ImGuiKey_Tab)) || pressed(ImGuiKey_Escape))
      {
         sSculpt.on = false;
         return true;
      }
      if (!command && !io.KeyAlt)
      {
         struct Key { ImGuiKey key; bool shift; int brush; };
         const Key keys[] = { { ImGuiKey_X, false, Sculpt::kDraw }, { ImGuiKey_S, false, Sculpt::kSmooth },
                              { ImGuiKey_G, false, Sculpt::kGrab }, { ImGuiKey_I, false, Sculpt::kInflate },
                              { ImGuiKey_P, false, Sculpt::kPinch }, { ImGuiKey_T, true, Sculpt::kFlatten },
                              { ImGuiKey_C, true, Sculpt::kCrease } };
         for (const Key& k : keys)
            if (io.KeyShift == k.shift && pressed(k.key))
               sSculpt.brush = k.brush;
         if (pressed(ImGuiKey_F))
         {
            sSculpt.adjust = io.KeyShift ? 's' : 'r';
            sSculpt.adjustBefore = io.KeyShift ? sSculpt.strength : sSculpt.radius;
            const float r = io.KeyShift ? sSculpt.strength * 200.0f : sSculpt.radius;
            sSculpt.adjustAt = ImVec2(mouse.x - r, mouse.y);
            return true;
         }
      }
      if (onBar || !ok || io.KeyAlt || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         return true;
      float c[3], w[3];
      if (!SculptHit(node, world, mouse, c, w))
         return true;
      static std::string undoLabel;
      undoLabel = std::string("Sculpt ") + Sculpt::BrushName(io.KeyShift ? (int)Sculpt::kSmooth : sSculpt.brush);
      PushUndoCheckpoint(undoLabel.c_str());
      sSculpt.stroking = true;
      sSculpt.last = mouse;
      node->BeginStroke();
      if (sSculpt.brush == Sculpt::kGrab && !io.KeyShift)
      {
         Sculpt::Dab d;
         std::copy(c, c + 3, d.center);
         d.radius = sSculpt.radius * UnitAt(w) / WorldScale(world);
         d.strength = sSculpt.strength;
         d.symmetry = sSculpt.symmetry;
         SculptAlpha(node, world, d);
         Sculpt::GrabBegin(d, node->PointPos(), node->Topology(), node->Offsets(), sSculpt.grab);
         std::copy(w, w + 3, sSculpt.grabWorld);
      }
      else
         SculptDab(node, world, mouse);
      return true;
   }

   // Edit Mode (Tab on an item, or the chip): the item's Edit Mesh node (one
   // is added after the item's node if it has none) holds the edits as ops.
   // Blender's keys: 1 2 3 points, edges, faces; click, Shift+click, drag a
   // box (Ctrl drags one off); A all, Alt+A none. G R S move, rotate, scale;
   // E extrude, I inset, X delete; X Y Z lock an axis while one runs. Click or
   // Enter keeps it, Esc or right-click cancels. Each op is one undo step.

   // The Edit Mesh node under an item (the one nearest the view) and the
   // matrix from its mesh space to the view's world.
   EditMeshNode* EditOf(const Item& it, Mat4& world)
   {
      GraphNode* gn = FindNodeByUid(it.uid);
      auto* top = gn != nullptr ? dynamic_cast<IGeometrySource*>(gn->node.get()) : nullptr;
      EditMeshNode* node = nullptr;
      for (IGeometrySource* s = top; s != nullptr && node == nullptr; s = s->PassthroughSource())
         node = dynamic_cast<EditMeshNode*>(s);
      if (node == nullptr)
         return nullptr;
      world = Mat4::Multiply(Scene3DNode::Placement(it), top->GetModelMatrix());
      return node;
   }

   EditMeshNode* EditTarget(Scene3DNode* scene, Mat4& world)
   {
      if (!sEdit.on)
         return nullptr;
      const Item* it = scene->FindItem(sEdit.item);
      EditMeshNode* node = it != nullptr && it->visible ? EditOf(*it, world) : nullptr;
      if (node == nullptr)
      {
         sEdit.on = false;
         sEdit.op = 0;
         sEdit.pressed = false;
      }
      return node;
   }

   void ToggleEdit(Scene3DNode* scene)
   {
      if (sEdit.on)
      {
         Mat4 world;
         if (EditMeshNode* node = sEdit.op != 0 ? EditTarget(scene, world) : nullptr)
            node->ops = sEdit.before;
         sEdit.on = false;
         sEdit.op = 0;
         sEdit.pressed = false;
         return;
      }
      Mat4 world;
      const Item* it = sSel.empty() ? nullptr : scene->FindItem(sSel.back());
      GraphNode* gn = it != nullptr ? FindNodeByUid(it->uid) : nullptr;
      if (gn == nullptr || dynamic_cast<IGeometrySource*>(gn->node.get()) == nullptr)
         return;
      if (sSculpt.on)
         ToggleSculpt(scene);
      sPose = {};
      sEdit.rev = ~0ull;
      if (EditOf(*it, world) != nullptr)
      {
         sEdit.on = true;
         sEdit.item = it->id;
         return;
      }
      sPending.what = Pending::EditMesh;
      sPending.item = it->id;
   }

   // A new Edit Mesh between the item's node and the view, then Edit Mode on it.
   void AddEditMesh(Scene3DNode* scene, int itemId)
   {
      Item* item = scene->FindItem(itemId);
      GraphNode* src = item != nullptr ? FindNodeByUid(item->uid) : nullptr;
      if (src == nullptr || dynamic_cast<IGeometrySource*>(src->node.get()) == nullptr)
         return;
      PushUndoCheckpoint("Add Edit Mesh");
      const int srcIndex = src->index;
      const ImVec2 at = NodePos(*src);
      GraphNode* made = SpawnNode("Edit Mesh", "3D", at.x + 320.0f, at.y);   // can move gNodes: src is stale after this
      std::string error;
      if (made == nullptr || dynamic_cast<EditMeshNode*>(made->node.get()) == nullptr ||
          !ConnectNodes(srcIndex, 0, made->index, 0, error))
         return;
      item->uid = made->uid;
      scene->Commit();
      sEdit.on = true;
      sEdit.item = itemId;
      sEdit.rev = ~0ull;
   }

   // The ops with their trailing sel line (if any) replaced by `sel`.
   std::string WithSelection(const std::string& ops, const std::string& sel)
   {
      std::string out = ops;
      while (!out.empty() && out.back() == '\n')
         out.pop_back();
      const size_t last = out.rfind('\n');
      const size_t from = last == std::string::npos ? 0 : last + 1;
      if (out.compare(from, 4, "sel ") == 0)
         out.erase(from);
      else if (!out.empty())
         out += '\n';
      return out + sel;
   }

   std::string WithOp(const std::string& ops, const std::string& op)
   {
      std::string out = ops;
      while (!out.empty() && out.back() == '\n')
         out.pop_back();
      return out.empty() ? op : out + '\n' + op;
   }

   // The selection as a sel line: faces in face mode, else their points.
   std::string SelectionLine(const Mesh& m, const std::vector<unsigned char>& face, const std::vector<unsigned char>& point)
   {
      std::string line = sEdit.mode == 3 ? "sel f" : "sel v";
      if (sEdit.mode == 3)
      {
         for (size_t f = 0; f < face.size(); f++)
            if (face[f])
               line += EditMesh::FaceSelector(m, f);
         return line;
      }
      std::vector<unsigned char> seen(sEdit.point.empty() ? 0 : *std::max_element(sEdit.point.begin(), sEdit.point.end()) + 1, 0);
      for (size_t v = 0; v < point.size() && v < sEdit.point.size(); v++)
         if (point[v] && !seen[sEdit.point[v]])
         {
            seen[sEdit.point[v]] = 1;
            line += EditMesh::PointSelector(m, (unsigned int)v);
         }
      return line;
   }

   void EditRebuild(const Mesh& m)
   {
      sEdit.point = EditMesh::PointIds(m);
      sEdit.edges.clear();
      sEdit.edgeFaces.clear();
      const size_t faces = m.FaceCount();
      std::vector<float> fn(faces * 3);
      for (size_t f = 0; f < faces; f++)
      {
         const Vertex &a = m.vertices[m.indices[f * 3]], &b = m.vertices[m.indices[f * 3 + 1]], &c = m.vertices[m.indices[f * 3 + 2]];
         const float u[3] = { b.px - a.px, b.py - a.py, b.pz - a.pz }, w[3] = { c.px - a.px, c.py - a.py, c.pz - a.pz };
         float n[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
         Normalize(n);
         std::copy(n, n + 3, &fn[f * 3]);
      }
      std::map<std::pair<int, int>, size_t> at;
      for (size_t f = 0; f < faces; f++)
         for (int k = 0; k < 3; k++)
         {
            const unsigned int va = m.indices[f * 3 + k], vb = m.indices[f * 3 + (k + 1) % 3];
            const int pa = sEdit.point[va], pb = sEdit.point[vb];
            const std::pair<int, int> key(std::min(pa, pb), std::max(pa, pb));
            auto it = at.find(key);
            if (it == at.end())
            {
               at.emplace(key, sEdit.edgeFaces.size() / 2);
               sEdit.edges.push_back(va);
               sEdit.edges.push_back(vb);
               sEdit.edgeFaces.push_back((int)f);
               sEdit.edgeFaces.push_back(-1);
            }
            else if (sEdit.edgeFaces[it->second * 2 + 1] < 0)
               sEdit.edgeFaces[it->second * 2 + 1] = (int)f;
         }
      // A diagonal inside a flat patch is not an edge Blender would show.
      size_t keep = 0;
      for (size_t e = 0; e * 2 < sEdit.edgeFaces.size(); e++)
      {
         const int f0 = sEdit.edgeFaces[e * 2], f1 = sEdit.edgeFaces[e * 2 + 1];
         if (f1 >= 0 && Dot(&fn[f0 * 3], &fn[f1 * 3]) > 0.9995f)
            continue;
         sEdit.edges[keep * 2] = sEdit.edges[e * 2];
         sEdit.edges[keep * 2 + 1] = sEdit.edges[e * 2 + 1];
         sEdit.edgeFaces[keep * 2] = f0;
         sEdit.edgeFaces[keep * 2 + 1] = f1;
         keep++;
      }
      sEdit.edges.resize(keep * 2);
      sEdit.edgeFaces.resize(keep * 2);
   }

   // World positions and which faces face the camera, for this frame.
   void EditFrame(const Mesh& m, const Mat4& world)
   {
      sEdit.pos.resize(m.vertices.size() * 3);
      for (size_t v = 0; v < m.vertices.size(); v++)
      {
         const float p[3] = { m.vertices[v].px, m.vertices[v].py, m.vertices[v].pz };
         TransformBy(world, p, &sEdit.pos[v * 3]);
      }
      const size_t faces = m.FaceCount();
      sEdit.front.assign(faces, 0);
      for (size_t f = 0; f < faces; f++)
      {
         const float* a = &sEdit.pos[m.indices[f * 3] * 3];
         const float* b = &sEdit.pos[m.indices[f * 3 + 1] * 3];
         const float* c = &sEdit.pos[m.indices[f * 3 + 2] * 3];
         const float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
         const float n[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
         const float view[3] = { a[0] - sCam.eye[0], a[1] - sCam.eye[1], a[2] - sCam.eye[2] };
         sEdit.front[f] = (sCam.ortho ? Dot(n, sCam.fwd) : Dot(n, view)) < 0.0f;
      }
   }

   // The face under the mouse (nearest along the ray), or -1.
   int EditPickFace(const Mesh& m, ImVec2 mouse)
   {
      float o[3], d[3];
      MouseRay(mouse, o, d);
      int best = -1;
      float bestT = 1e30f;
      for (size_t f = 0; f < m.FaceCount(); f++)
      {
         const float* a = &sEdit.pos[m.indices[f * 3] * 3];
         const float* b = &sEdit.pos[m.indices[f * 3 + 1] * 3];
         const float* c = &sEdit.pos[m.indices[f * 3 + 2] * 3];
         const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
         const float pv[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
         const float det = Dot(e1, pv);
         if (std::fabs(det) < 1e-12f)
            continue;
         const float tv[3] = { o[0] - a[0], o[1] - a[1], o[2] - a[2] };
         const float u = Dot(tv, pv) / det;
         if (u < 0.0f || u > 1.0f)
            continue;
         const float qv[3] = { tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0] };
         const float v = Dot(d, qv) / det;
         if (v < 0.0f || u + v > 1.0f)
            continue;
         const float t = Dot(e2, qv) / det;
         if (t > 1e-5f && t < bestT)
         {
            bestT = t;
            best = (int)f;
         }
      }
      return best;
   }

   float SegmentDistance(ImVec2 p, ImVec2 a, ImVec2 b)
   {
      const float dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
      const float t = len > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len, 0.0f, 1.0f) : 0.0f;
      return std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t);
   }

   // A vertex can be picked when a face that faces the camera uses it.
   std::vector<unsigned char> EditVisiblePoints(const Mesh& m)
   {
      std::vector<unsigned char> vis(m.vertices.size(), 0);
      for (size_t f = 0; f < sEdit.front.size(); f++)
         if (sEdit.front[f])
            for (int k = 0; k < 3; k++)
               vis[m.indices[f * 3 + k]] = 1;
      return vis;
   }

   bool EditEdgeVisible(size_t e)
   {
      const int f0 = sEdit.edgeFaces[e * 2], f1 = sEdit.edgeFaces[e * 2 + 1];
      return (f0 >= 0 && sEdit.front[f0]) || (f1 >= 0 && sEdit.front[f1]);
   }

   // Selects what is under a click (or in a box), adding with Shift, taking
   // away with Ctrl (a box) or a Shift+click on something already selected.
   void EditSelect(EditMeshNode* node, const Mesh& m, ImVec2 a, ImVec2 b, bool box)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const EditMesh::Selection& cur = node->Selected();
      std::vector<unsigned char> face(m.FaceCount(), 0), point(m.vertices.size(), 0);
      const bool keep = io.KeyShift || (box && io.KeyCtrl);
      if (keep)
      {
         if (cur.face.size() == face.size())
            face = cur.face;
         if (cur.point.size() == point.size())
            point = cur.point;
      }
      const ImVec2 lo(std::min(a.x, b.x), std::min(a.y, b.y)), hi(std::max(a.x, b.x), std::max(a.y, b.y));
      auto inBox = [&](const float* w) {
         ImVec2 s;
         return Project(w, s) && s.x >= lo.x && s.x <= hi.x && s.y >= lo.y && s.y <= hi.y;
      };
      // What the click or box hits: faces in face mode, else points.
      std::vector<unsigned char> hitFace(face.size(), 0), hitPoint(point.size(), 0);
      const std::vector<unsigned char> vis = EditVisiblePoints(m);
      if (sEdit.mode == 3)
      {
         if (box)
         {
            for (size_t f = 0; f < face.size(); f++)
            {
               if (!sEdit.front[f])
                  continue;
               float c[3];
               for (int k = 0; k < 3; k++)
                  c[k] = (sEdit.pos[m.indices[f * 3] * 3 + k] + sEdit.pos[m.indices[f * 3 + 1] * 3 + k] + sEdit.pos[m.indices[f * 3 + 2] * 3 + k]) / 3.0f;
               hitFace[f] = inBox(c);
            }
         }
         else if (const int f = EditPickFace(m, a); f >= 0)
            for (size_t g : EditMesh::FlatPatch(m, (size_t)f))
               hitFace[g] = 1;
      }
      else if (box)
      {
         for (size_t v = 0; v < point.size(); v++)
            hitPoint[v] = vis[v] && inBox(&sEdit.pos[v * 3]);
         if (sEdit.mode == 2)
         {
            // An edge is in the box when both its ends are.
            std::vector<unsigned char> ends(point.size(), 0);
            for (size_t e = 0; e * 2 < sEdit.edges.size(); e++)
               if (hitPoint[sEdit.edges[e * 2]] && hitPoint[sEdit.edges[e * 2 + 1]])
                  ends[sEdit.edges[e * 2]] = ends[sEdit.edges[e * 2 + 1]] = 1;
            hitPoint.swap(ends);
         }
      }
      else if (sEdit.mode == 1)
      {
         int best = -1;
         float bestD = 14.0f;
         for (size_t v = 0; v < point.size(); v++)
         {
            ImVec2 s;
            if (!vis[v] || !Project(&sEdit.pos[v * 3], s))
               continue;
            const float d = std::hypot(s.x - a.x, s.y - a.y);
            if (d < bestD)
            {
               bestD = d;
               best = (int)v;
            }
         }
         if (best >= 0)
            hitPoint[best] = 1;
      }
      else
      {
         int best = -1;
         float bestD = 10.0f;
         for (size_t e = 0; e * 2 < sEdit.edges.size(); e++)
         {
            ImVec2 sa, sb;
            if (!EditEdgeVisible(e) || !Project(&sEdit.pos[sEdit.edges[e * 2] * 3], sa) || !Project(&sEdit.pos[sEdit.edges[e * 2 + 1] * 3], sb))
               continue;
            const float d = SegmentDistance(a, sa, sb);
            if (d < bestD)
            {
               bestD = d;
               best = (int)e;
            }
         }
         if (best >= 0)
            hitPoint[sEdit.edges[best * 2]] = hitPoint[sEdit.edges[best * 2 + 1]] = 1;
      }
      // Every vertex of a hit point, so split corners go together.
      std::vector<unsigned char> hitIds(sEdit.point.empty() ? 0 : *std::max_element(sEdit.point.begin(), sEdit.point.end()) + 1, 0);
      for (size_t v = 0; v < point.size(); v++)
         if (hitPoint[v])
            hitIds[sEdit.point[v]] = 1;
      bool all = true, any = false;
      for (size_t f = 0; f < face.size(); f++)
         if (hitFace[f])
         {
            any = true;
            all = all && face[f];
         }
      for (size_t v = 0; v < point.size(); v++)
         if (hitIds[sEdit.point[v]])
         {
            any = true;
            all = all && point[v];
         }
      const bool off = (box && io.KeyCtrl) || (!box && io.KeyShift && any && all);
      for (size_t f = 0; f < face.size(); f++)
         if (hitFace[f])
            face[f] = !off;
      for (size_t v = 0; v < point.size(); v++)
         if (hitIds[sEdit.point[v]])
            point[v] = !off;
      PushUndoCheckpoint("Select in Edit Mode");
      node->ops = WithSelection(node->ops, SelectionLine(m, face, point));
   }

   // Starts G R S E I on the selection.
   void EditBegin(EditMeshNode* node, const Mesh& m, char op, ImVec2 mouse)
   {
      const EditMesh::Selection& sel = node->Selected();
      const bool needFaces = op == 'e' || op == 'i';
      bool any = false;
      float c[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 };
      std::vector<unsigned char> seen;
      int count = 0;
      for (size_t v = 0; v < sel.point.size() && v < sEdit.point.size(); v++)
         if (sel.point[v])
         {
            if (seen.size() <= (size_t)sEdit.point[v])
               seen.resize(sEdit.point[v] + 1, 0);
            if (seen[sEdit.point[v]])
               continue;
            seen[sEdit.point[v]] = 1;
            for (int k = 0; k < 3; k++)
               c[k] += sEdit.pos[v * 3 + k];
            count++;
         }
      for (size_t f = 0; f < sel.face.size(); f++)
         if (sel.face[f])
         {
            any = true;
            const float* a = &sEdit.pos[m.indices[f * 3] * 3];
            const float* b = &sEdit.pos[m.indices[f * 3 + 1] * 3];
            const float* d = &sEdit.pos[m.indices[f * 3 + 2] * 3];
            const float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
            n[0] += u[1] * w[2] - u[2] * w[1];
            n[1] += u[2] * w[0] - u[0] * w[2];
            n[2] += u[0] * w[1] - u[1] * w[0];
         }
      if (count == 0 || (needFaces && !any))
         return;
      for (int k = 0; k < 3; k++)
         sEdit.pivot[k] = c[k] / (float)count;
      Normalize(n);
      std::copy(n, n + 3, sEdit.normal);
      static const char* labels[] = { "Move in Edit Mode", "Rotate in Edit Mode", "Scale in Edit Mode", "Extrude in Edit Mode",
                                      "Inset in Edit Mode" };
      PushUndoCheckpoint(labels[op == 'g' ? 0 : op == 'r' ? 1 : op == 's' ? 2 : op == 'e' ? 3 : 4]);
      sEdit.op = op;
      sEdit.axis = -1;
      sEdit.start = mouse;
      sEdit.before = node->ops;
   }

   // Pixels per world unit along `dir` at the pivot, and that direction on screen.
   float ScreenAlong(const float dir[3], ImVec2& unit)
   {
      ImVec2 a, b;
      const float tip[3] = { sEdit.pivot[0] + dir[0], sEdit.pivot[1] + dir[1], sEdit.pivot[2] + dir[2] };
      if (!Project(sEdit.pivot, a) || !Project(tip, b))
         return 0.0f;
      const float len = std::hypot(b.x - a.x, b.y - a.y);
      if (len < 1e-3f)
         return 0.0f;
      unit = ImVec2((b.x - a.x) / len, (b.y - a.y) / len);
      return len;
   }

   // The running op's line for the mouse where it is.
   void EditUpdate(EditMeshNode* node, const Mat4& world, ImVec2 mouse)
   {
      const Mat4 inv = Mat4::Inverse(world);
      const float unit = UnitAt(sEdit.pivot);
      const ImVec2 md(mouse.x - sEdit.start.x, mouse.y - sEdit.start.y);
      ImVec2 pc;
      Project(sEdit.pivot, pc);
      const float axes[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
      char line[160] = "";
      if (sEdit.op == 'g')
      {
         float w[3];
         if (sEdit.axis >= 0)
         {
            ImVec2 dir;
            const float px = ScreenAlong(axes[sEdit.axis], dir);
            const float t = px > 0.0f ? (md.x * dir.x + md.y * dir.y) / px : 0.0f;
            for (int k = 0; k < 3; k++)
               w[k] = axes[sEdit.axis][k] * t;
         }
         else
            for (int k = 0; k < 3; k++)
               w[k] = (sCam.right[k] * md.x - sCam.up[k] * md.y) * unit;
         float m[3];
         LinearBy(inv, w, m);
         snprintf(line, sizeof(line), "move %.5g %.5g %.5g", m[0], m[1], m[2]);
      }
      else if (sEdit.op == 'r')
      {
         const float a0 = std::atan2(-(sEdit.start.y - pc.y), sEdit.start.x - pc.x);
         const float a1 = std::atan2(-(mouse.y - pc.y), mouse.x - pc.x);
         float deg = (a1 - a0) * 180.0f / kPi;
         // About the view axis toward the eye, or the locked axis turned to face it.
         float w[3] = { -sCam.fwd[0], -sCam.fwd[1], -sCam.fwd[2] };
         if (sEdit.axis >= 0)
         {
            std::copy(axes[sEdit.axis], axes[sEdit.axis] + 3, w);
            if (Dot(w, sCam.fwd) > 0.0f)
               deg = -deg;
         }
         float m[3];
         LinearBy(inv, w, m);
         Normalize(m);
         snprintf(line, sizeof(line), "rotate %.5g %.5g %.5g %.5g", m[0], m[1], m[2], deg);
      }
      else if (sEdit.op == 's')
      {
         const float d0 = std::max(1.0f, std::hypot(sEdit.start.x - pc.x, sEdit.start.y - pc.y));
         const float f = std::hypot(mouse.x - pc.x, mouse.y - pc.y) / d0;
         float s[3] = { f, f, f };
         if (sEdit.axis >= 0)
            for (int k = 0; k < 3; k++)
               s[k] = k == sEdit.axis ? f : 1.0f;
         snprintf(line, sizeof(line), "scale %.5g %.5g %.5g", s[0], s[1], s[2]);
      }
      else if (sEdit.op == 'e')
      {
         ImVec2 dir;
         const float px = ScreenAlong(sEdit.normal, dir);
         const float t = px > 2.0f ? (md.x * dir.x + md.y * dir.y) / px : -md.y * unit;
         snprintf(line, sizeof(line), "extrude %.5g", t / WorldScale(world));
      }
      else if (sEdit.op == 'i')
      {
         const float d0 = std::hypot(sEdit.start.x - pc.x, sEdit.start.y - pc.y);
         const float d = std::hypot(mouse.x - pc.x, mouse.y - pc.y);
         snprintf(line, sizeof(line), "inset %.5g", std::max(0.0f, d0 - d) * unit / WorldScale(world));
      }
      node->ops = WithOp(sEdit.before, line);
   }

   // Draws the mesh's points, edges and selected faces, runs selection and
   // ops. Returns true while the mode owns the mouse and keys (navigation
   // still runs).
   bool DrawEdit(ImDrawList* dl, Scene3DNode* scene, ImVec2 mouse, bool hovered)
   {
      Mat4 world;
      EditMeshNode* node = EditTarget(scene, world);
      if (node == nullptr)
         return false;
      const ImGuiIO& io = ImGui::GetIO();
      // Tab is locked to the node keyboard owner whenever nothing else uses it; the 3D View asks as that owner.
      auto pressed = [](ImGuiKey k) { return k == ImGuiKey_Tab ? ImGui::IsKeyPressed(k, ImGuiInputFlags_None, kKbTabOwner) : ImGui::IsKeyPressed(k, false); };
      const Mesh& m = node->GetMesh();
      if (node->MeshRevision() != sEdit.rev)
      {
         sEdit.rev = node->MeshRevision();
         EditRebuild(m);
      }
      EditFrame(m, world);
      const EditMesh::Selection& sel = node->Selected();
      const bool fits = sel.face.size() == m.FaceCount() && sel.point.size() == m.vertices.size();

      // Selected faces, then edges, then points (point mode).
      for (size_t f = 0; fits && f < sel.face.size(); f++)
      {
         ImVec2 a, b, c;
         if (sel.face[f] && sEdit.front[f] && Project(&sEdit.pos[m.indices[f * 3] * 3], a) &&
             Project(&sEdit.pos[m.indices[f * 3 + 1] * 3], b) && Project(&sEdit.pos[m.indices[f * 3 + 2] * 3], c))
            dl->AddTriangleFilled(a, b, c, Hex(kSelected, 0.22f));
      }
      for (size_t e = 0; e * 2 < sEdit.edges.size(); e++)
      {
         if (!EditEdgeVisible(e))
            continue;
         const unsigned int va = sEdit.edges[e * 2], vb = sEdit.edges[e * 2 + 1];
         const bool on = fits && sel.point[va] && sel.point[vb];
         Line3D(dl, &sEdit.pos[va * 3], &sEdit.pos[vb * 3], on ? Hex(kSelected) : Hex(0x000000, 0.8f), on ? 2.0f : 1.0f);
      }
      if (sEdit.mode == 1)
      {
         const std::vector<unsigned char> vis = EditVisiblePoints(m);
         for (size_t v = 0; v < m.vertices.size(); v++)
         {
            ImVec2 s;
            if (vis[v] && Project(&sEdit.pos[v * 3], s))
               dl->AddCircleFilled(s, 3.0f, fits && sel.point[v] ? Hex(kSelected) : Hex(0x000000, 0.9f));
         }
      }

      // The bar: Points Edges Faces, bottom left.
      bool onBar = false;
      {
         float x = sCam.mn.x + 12.0f;
         const float y = sCam.mx.y - 54.0f, h = ImGui::GetFontSize() + 8.0f;
         const char* names[3] = { T("Points"), T("Edges"), T("Faces") };
         for (int i = 0; i < 3; i++)
         {
            const ImVec2 ts = ImGui::CalcTextSize(names[i]);
            const ImVec2 a(x, y), b(x + ts.x + 14.0f, y + h);
            const bool hot = hovered && mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y;
            const bool on = sEdit.mode == i + 1;
            dl->AddRectFilled(a, b, Hex(on ? kSelected : 0x000000, hot || on ? 0.85f : 0.45f), h * 0.5f);
            dl->AddText(ImVec2(a.x + 7.0f, a.y + 4.0f), Hex(0xFFFFFF), names[i]);
            x = b.x + 4.0f;
            onBar = onBar || hot;
            if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && sEdit.op == 0 && !on)
            {
               sEdit.mode = i + 1;
               if (fits)
                  node->ops = WithSelection(node->ops, SelectionLine(m, sel.face, sel.point));
            }
         }
      }
      int count = 0;
      for (unsigned char f : sel.face)
         count += f;
      char label[96];
      snprintf(label, sizeof(label), "Edit Mode   %d ops   %d faces", node->OpCount(), count);
      dl->AddText(ImVec2(sCam.mn.x + 12.0f, sCam.mx.y - 26.0f), Hex(kActive), node->CookWarning().empty() ? label : node->CookWarning().c_str());

      if (sEdit.op != 0)
      {
         if (pressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            node->ops = sEdit.before;
            sEdit.op = 0;
            return true;
         }
         const ImGuiKey axisKeys[3] = { ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z };
         for (int k = 0; k < 3 && sEdit.op != 'e' && sEdit.op != 'i'; k++)
            if (pressed(axisKeys[k]))
               sEdit.axis = sEdit.axis == k ? -1 : k;
         EditUpdate(node, world, mouse);
         ImVec2 pc;
         if (sEdit.op != 'g' && Project(sEdit.pivot, pc))
            dl->AddLine(pc, mouse, Hex(kWire, 0.7f));
         if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            sEdit.op = 0;
         return true;
      }

      // A press becomes a click or, dragged past a few pixels, a box.
      if (sEdit.pressed)
      {
         const bool boxing = std::hypot(mouse.x - sEdit.pressAt.x, mouse.y - sEdit.pressAt.y) > 4.0f;
         if (boxing)
         {
            const ImVec2 lo(std::min(mouse.x, sEdit.pressAt.x), std::min(mouse.y, sEdit.pressAt.y));
            const ImVec2 hi(std::max(mouse.x, sEdit.pressAt.x), std::max(mouse.y, sEdit.pressAt.y));
            dl->AddRectFilled(lo, hi, Hex(0xFFFFFF, 0.06f));
            dl->AddRect(lo, hi, Hex(0xFFFFFF, 0.6f));
         }
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            sEdit.pressed = false;
            EditSelect(node, m, sEdit.pressAt, mouse, boxing);
         }
         return true;
      }
      if (!hovered || io.WantTextInput)
         return true;
      const bool command = io.KeyCtrl || io.KeySuper;
      if ((!command && !io.KeyShift && pressed(ImGuiKey_Tab)) || pressed(ImGuiKey_Escape))
      {
         sEdit.on = false;
         return true;
      }
      if (!command && !io.KeyShift)
      {
         const ImGuiKey modes[3] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3 };
         for (int i = 0; i < 3 && !io.KeyAlt; i++)
            if (pressed(modes[i]) && sEdit.mode != i + 1)
            {
               sEdit.mode = i + 1;
               if (fits)
                  node->ops = WithSelection(node->ops, SelectionLine(m, sel.face, sel.point));
            }
         if (pressed(ImGuiKey_A))
         {
            PushUndoCheckpoint("Select in Edit Mode");
            node->ops = WithSelection(node->ops, io.KeyAlt ? std::string(sEdit.mode == 3 ? "sel f" : "sel v") : std::string("sel all"));
         }
         if (!io.KeyAlt)
         {
            const struct { ImGuiKey key; char op; } ops[] = { { ImGuiKey_G, 'g' }, { ImGuiKey_R, 'r' }, { ImGuiKey_S, 's' },
                                                              { ImGuiKey_E, 'e' }, { ImGuiKey_I, 'i' } };
            for (const auto& o : ops)
               if (pressed(o.key))
               {
                  EditBegin(node, m, o.op, mouse);
                  return true;
               }
            if ((pressed(ImGuiKey_X) || pressed(ImGuiKey_Delete)) && fits &&
                std::find(sel.face.begin(), sel.face.end(), 1) != sel.face.end())
            {
               PushUndoCheckpoint("Delete in Edit Mode");
               node->ops = WithOp(node->ops, "delete");
            }
         }
      }
      if (!onBar && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         sEdit.pressed = true;
         sEdit.pressAt = mouse;
      }
      return true;
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
      const float depth = std::max(0.01f, lens->displaySize);
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
      if (lens->showMist)   // where the Mist pass starts and where it is full
      {
         auto along = [&](float d, float out[3]) { const float p[3] = { 0.0f, 0.0f, -d }; TransformBy(world, p, out); };
         const Render3DNode& look = scene->Look();
         float a[3], b[3];
         along(look.mistStart, a);
         along(look.mistStart + look.mistDepth, b);
         const ImU32 mist = Hex(0x60B0E8, 0.9f);
         Line3D(dl, a, b, mist);
         ImVec2 sa, sb;
         if (Project(a, sa))
            dl->AddCircleFilled(sa, 3.0f, mist);
         if (Project(b, sb))
            dl->AddCircle(sb, 4.0f, mist, 12, 1.5f);
      }
      if (lens->showLimits)   // the clip range along the view, and where it focuses
      {
         auto along = [&](float d, float out[3]) { const float p[3] = { 0.0f, 0.0f, -d }; TransformBy(world, p, out); };
         float n[3], f[3];
         along(lens->nearPlane, n);
         along(lens->farPlane, f);
         const ImU32 limit = Hex(0xE8C840, 0.9f);
         Line3D(dl, n, f, limit);
         ImVec2 sn, sf;
         if (Project(n, sn))
            dl->AddCircleFilled(sn, 3.0f, limit);
         if (Project(f, sf))
            dl->AddCircleFilled(sf, 3.0f, limit);
         if (it.id == scene->activeCamera && scene->settings.fStop > 0.0f)
         {
            float fp[3];
            ImVec2 sp;
            along(scene->Look().focusDistance, fp);
            if (Project(fp, sp))
            {
               dl->AddLine(ImVec2(sp.x - 5, sp.y - 5), ImVec2(sp.x + 5, sp.y + 5), limit, 1.5f);
               dl->AddLine(ImVec2(sp.x - 5, sp.y + 5), ImVec2(sp.x + 5, sp.y - 5), limit, 1.5f);
            }
         }
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
         // Inner ring where the blend ends, as the shader's smoothstep places it.
         const float cosOuter = std::cos(light->spotAngle * kPi / 180.0f);
         const float cosInner = std::min(1.0f, cosOuter + (1.0f - cosOuter) * light->spotPenumbra);
         const float blend = len * std::tan(std::acos(cosInner));
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
      if (type == 4 && light->showCone)   // the whole volume the spot lights, as a faint shell
      {
         const float len = light->useCutoff ? light->cutoffDistance : std::max(0.05f, light->distance);
         const float r = len * std::tan(light->spotAngle * kPi / 180.0f);
         const ImU32 fill = (col & 0x00FFFFFF) | 0x26000000;
         ImVec2 prev;
         bool prevOk = false;
         for (int i = 0; i <= 32; i++)
         {
            const float a = (float)i / 32.0f * 2.0f * kPi, cs = std::cos(a), sn = std::sin(a);
            float q[3];
            for (int k = 0; k < 3; k++)
               q[k] = p[k] + aim[k] * len + (u[k] * cs + v[k] * sn) * r;
            ImVec2 qs;
            const bool ok = Project(q, qs);
            if (ok && prevOk)
            {
               dl->AddTriangleFilled(s, prev, qs, fill);
               dl->AddLine(prev, qs, col & 0x80FFFFFF);
            }
            prev = qs;
            prevOk = ok;
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
      const float len = UnitAt(at) * kGizmoPx * sSet->gizmoSize / 75.0f;
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
      const float pad = 52.0f * sSet->miniSize / 80.0f;
      const ImVec2 c(mx.x - pad, mn.y + pad);
      const float r = 34.0f * sSet->miniSize / 80.0f;
      const float ballK = sSet->gizmoNavSize / 80.0f;
      hovered = hovered && sSet->miniAxes == 2;
      const int firstVtx = dl->VtxBuffer.Size;
      onDisc = hovered && std::hypot(mouse.x - c.x, mouse.y - c.y) < r + 12.0f;
      if (onDisc || sNavDrag)
         dl->AddCircleFilled(c, r + 12.0f, Hex(kHotHandle, 0.10f), 48);
      struct Ball { float depth; ImVec2 at; int axis; bool neg; };
      std::vector<Ball> balls;
      for (int a = 0; a < 3; a++)
      {
         const float sign = a == 2 ? -1.0f : 1.0f;   // display +Y looks down engine -Z
         const float dir[3] = { a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? sign : 0.0f };
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
            hit = b.axis * 2 + ((b.neg != (b.axis == 2)) ? 1 : 0);   // ViewAxis counts engine sides
         const float size = (b.neg ? 7.0f : 9.0f) * ballK;
         if (b.neg)
         {
            dl->AddCircleFilled(b.at, size, AxisColor(b.axis, hot ? 0.9f : 0.35f), 24);
            dl->AddCircle(b.at, size, AxisColor(b.axis), 24, 1.5f);
            continue;
         }
         dl->AddLine(c, b.at, AxisColor(b.axis), 2.0f);
         dl->AddCircleFilled(b.at, size, hot ? Hex(kHotHandle) : AxisColor(b.axis), 24);
         const char label[2] = { (char)('X' + kDisp[b.axis]), 0 };
         const ImVec2 ts = ImGui::CalcTextSize(label);
         dl->AddText(ImVec2(b.at.x - ts.x * 0.5f, b.at.y - ts.y * 0.5f), Hex(0x000000), label);
      }
      for (int i = firstVtx; i < dl->VtxBuffer.Size; i++)
      {
         ImU32& col = dl->VtxBuffer[i].col;
         const ImU32 al = (ImU32)(((col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(sSet->miniBright, 0.0f, 1.0f));
         col = (col & ~IM_COL32_A_MASK) | (al << IM_COL32_A_SHIFT);
      }
      return hit;
   }

   // Nearest item under the ray. Cameras and lights go by their marker and win, being
   // small; geometry goes by its box, hit by the ray itself.
   // Everything under the pointer, nearest first: markers, then boxes.
   // Clicking again on the same spot steps to the next one back, as in Blender.
   ImVec2 sPickAt(-100, -100);
   int sPicked = 0;

   int Pick(Scene3DNode* scene, ImVec2 mouse, bool cycle = false)
   {
      std::vector<std::pair<float, int>> markers, boxes;
      float o[3], d[3];
      MouseRay(mouse, o, d);
      for (const Item& it : scene->Items())
      {
         float p[3], lo[3], hi[3];
         ImVec2 s;
         if (!Pickable(scene, it) || (it.id == scene->activeCamera && sCameraView))
            continue;
         if (scene->ItemPoint(it, p))
         {
            const float depth = (p[0] - sCam.eye[0]) * sCam.fwd[0] + (p[1] - sCam.eye[1]) * sCam.fwd[1] + (p[2] - sCam.eye[2]) * sCam.fwd[2];
            if (Project(p, s) && std::hypot(mouse.x - s.x, mouse.y - s.y) < 14.0f)
               markers.push_back({ depth, it.id });
         }
         else if (scene->ItemBounds(it.id, lo, hi))
         {
            float near = -1e30f, far = 1e30f;
            for (int k = 0; k < 3 && near <= far; k++)
            {
               if (std::abs(d[k]) < 1e-6f)
               {
                  if (o[k] < lo[k] || o[k] > hi[k])
                     far = -1e30f;
                  continue;
               }
               const float a0 = (lo[k] - o[k]) / d[k], b0 = (hi[k] - o[k]) / d[k];
               near = std::max(near, std::min(a0, b0));
               far = std::min(far, std::max(a0, b0));
            }
            if (near <= far && far >= 0.0f)
               boxes.push_back({ std::max(near, 0.0f), it.id });
         }
      }
      std::sort(markers.begin(), markers.end());
      std::sort(boxes.begin(), boxes.end());
      std::vector<int> hits;
      for (const auto& m : markers) hits.push_back(m.second);
      for (const auto& m : boxes) hits.push_back(m.second);
      if (hits.empty())
         return 0;
      int pick = hits[0];
      if (cycle)
      {
         const bool same = std::hypot(mouse.x - sPickAt.x, mouse.y - sPickAt.y) < 4.0f;
         const auto at = std::find(hits.begin(), hits.end(), sPicked);
         if (same && at != hits.end() && Selected(sPicked))
            pick = *(at + 1 == hits.end() ? hits.begin() : at + 1);
         sPickAt = mouse;
         sPicked = pick;
      }
      return pick;
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
      if (sSet->stats)
      {
         int cams = 0, lights = 0, geo = 0;
         for (const Item& it : scene->Items())
         {
            if (!Shown(it))
               continue;
            const Kind k = scene->ItemKind(it);
            (k == Kind::Camera ? cams : k == Kind::Light ? lights : geo)++;
         }
         char text[96];
         snprintf(text, sizeof(text), T("Objects %d/%d   Cameras %d   Lights %d"), (int)sSel.size(), geo + cams + lights, cams, lights);
         dl->AddText(ImVec2(mn.x + tok::space_3, mn.y + tok::space_2 + (sSet->textInfo ? ImGui::GetTextLineHeightWithSpacing() * 2.0f : 0.0f)),
                     Hex(0xFFFFFF, 0.6f), text);
      }
      if (sSet->showPerf || sSet->showFps)
      {
         char text[64];
         const float fps = ImGui::GetIO().Framerate;
         if (sSet->showPerf && sSet->showFps)
            snprintf(text, sizeof(text), "%.0f fps   %.1f ms", fps, 1000.0f / std::max(1.0f, fps));
         else if (sSet->showPerf)
            snprintf(text, sizeof(text), "%.1f ms", 1000.0f / std::max(1.0f, fps));
         else
            snprintf(text, sizeof(text), "%.0f fps", fps);
         dl->AddText(ImVec2(sCam.mx.x - ImGui::CalcTextSize(text).x - tok::space_3, mn.y + tok::space_2), Hex(0xFFFFFF, 0.6f), text);
      }
      if (!sSet->textInfo)
         return;
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
   void DrawCameraGuides(ImDrawList* dl, Scene3DNode* scene)
   {
      const ImVec2 a = sCam.frameMn, b = sCam.frameMx;
      const float w = b.x - a.x, h = b.y - a.y;
      const float* gc = sSet->guideColor;
      const ImU32 edge = Hex(0x000000, 0.9f), guide = ImGui::ColorConvertFloat4ToU32(ImVec4(gc[0], gc[1], gc[2], 0.7f));
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
      if (sSet->safe && sSet->safeCenter)   // Blender's centre-cut margins: title 17.5% / 5%, action 15% / 5%
      {
         dl->AddRect(ImVec2(a.x + w * 0.15f, a.y + h * 0.05f), ImVec2(b.x - w * 0.15f, b.y - h * 0.05f), guide);
         dl->AddRect(ImVec2(a.x + w * 0.175f, a.y + h * 0.05f), ImVec2(b.x - w * 0.175f, b.y - h * 0.05f), guide);
      }
      if (sSet->diagonal)
      {
         dl->AddLine(a, b, guide);
         dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), guide);
      }
      // A triangle guide: one diagonal, then from the two other corners a line square to it. The golden kind
      // stops on the diagonal; the harmonious kind runs on to the frame's far edge.
      auto triangles = [&](ImVec2 p0, ImVec2 p1, ImVec2 c0, ImVec2 c1, bool harmonious)
      {
         dl->AddLine(p0, p1, guide);
         const ImVec2 d(p1.x - p0.x, p1.y - p0.y);
         const float dd = std::max(1e-6f, d.x * d.x + d.y * d.y);
         for (ImVec2 c : { c0, c1 })
         {
            const float k = ((c.x - p0.x) * d.x + (c.y - p0.y) * d.y) / dd;
            ImVec2 end(p0.x + d.x * k, p0.y + d.y * k);
            if (harmonious)
            {
               const ImVec2 r(end.x - c.x, end.y - c.y);
               float s = 1e9f;
               if (r.x > 1e-6f) s = std::min(s, (b.x - c.x) / r.x);
               if (r.x < -1e-6f) s = std::min(s, (a.x - c.x) / r.x);
               if (r.y > 1e-6f) s = std::min(s, (b.y - c.y) / r.y);
               if (r.y < -1e-6f) s = std::min(s, (a.y - c.y) / r.y);
               end = ImVec2(c.x + r.x * s, c.y + r.y * s);
            }
            dl->AddLine(c, end, guide);
         }
      };
      const ImVec2 bl(a.x, b.y), tr(b.x, a.y);
      if (sSet->goldenTriA)
         triangles(bl, tr, a, b, false);
      if (sSet->goldenTriB)
         triangles(a, b, bl, tr, false);
      if (sSet->harmonyTriA)
         triangles(bl, tr, a, b, true);
      if (sSet->harmonyTriB)
         triangles(a, b, bl, tr, true);

      const Item* active = scene->FindItem(scene->activeCamera);
      GraphNode* gn = active != nullptr ? FindNodeByUid(active->uid) : nullptr;
      auto* cam = gn != nullptr ? dynamic_cast<CameraNode*>(gn->node.get()) : nullptr;
      if (cam == nullptr)
         return;
      if (cam->showSensor)   // the sensor's own shape, fitted to the frame along the side it fits
      {
         const float ratio = std::max(0.01f, cam->sensorWidth) / std::max(0.01f, cam->sensorHeight);
         const bool tall = cam->sensorFit == 2 || (cam->sensorFit == 0 && w < h);
         const float gw = tall ? h * ratio : w, gh = tall ? h : w / ratio;
         const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
         DashedRect(dl, ImVec2(mid.x - gw * 0.5f, mid.y - gh * 0.5f), ImVec2(mid.x + gw * 0.5f, mid.y + gh * 0.5f), guide);
      }
      if (cam->showName)
         dl->AddText(ImVec2(a.x + 6.0f, b.y - ImGui::GetFontSize() - 4.0f), Hex(kWire, 0.9f), ItemName(*active).c_str());
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

   ImVec2 SpawnSpot(Scene3DNode* scene);

   // Files dropped on the 3D View: a model lands where it was dropped (on the ground, else at the
   // 3D cursor) as a Model 3D node, an .hdr or .exr becomes the world, and a picture dropped while
   // looking through a camera becomes that camera's background.
   void TakeDroppedFiles(Scene3DNode* scene)
   {
      if (gDroppedFiles.empty() || gDropPos.x < gView3DRectMin.x || gDropPos.x >= gView3DRectMax.x ||
          gDropPos.y < gView3DRectMin.y || gDropPos.y >= gView3DRectMax.y)
         return;
      static const std::vector<std::string> kModels = { "obj", "ply", "stl", "usd", "usda", "usdc", "usdz", "abc", "gltf", "glb" };
      static const std::vector<std::string> kWorlds = { "hdr", "exr" };
      static const std::vector<std::string> kPictures = { "png", "jpg", "jpeg", "tif", "tiff", "bmp", "webp" };
      const std::vector<std::string> files = std::move(gDroppedFiles);
      gDroppedFiles.clear();
      float at[3] = { sSet->cursor[0], sSet->cursor[1], sSet->cursor[2] };
      const bool inView = gDropPos.x >= sCam.mn.x && gDropPos.x < sCam.mx.x && gDropPos.y >= sCam.mn.y && gDropPos.y < sCam.mx.y;
      if (inView)
      {
         float o[3], d[3];
         MouseRay(gDropPos, o, d);
         if (d[1] < -1e-4f)
         {
            const float t = -o[1] / d[1];
            for (int k = 0; k < 3; k++)
               at[k] = o[k] + d[k] * t;
         }
      }
      PushUndoCheckpoint("Drop into 3D View");
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      gSuppressUndoCheckpoints = true;
      std::vector<int> added;
      for (const std::string& path : files)
      {
         if (HasExtension(path, { "gltf", "glb" }))
         {
            // One item per material, grouped, each with its Material and maps.
            const ImVec2 spot = SpawnSpot(scene);
            const std::vector<int> parts = SpawnGltfParts(path, spot.x, spot.y);
            const int group = parts.size() > 1 ? NextGroup(scene) : 0;
            for (int index : parts)
               if (GraphNode* gn = FindNodeByIndex(index))
               {
                  Item& it = scene->AddItem(gn->uid);
                  std::copy(at, at + 3, it.pos);
                  it.group = group;
                  added.push_back(it.id);
               }
         }
         else if (HasExtension(path, kModels))
         {
            const ImVec2 spot = SpawnSpot(scene);
            GraphNode* gn = SpawnNode("Model 3D", "3D", spot.x, spot.y + 120.0f * (float)added.size());
            if (gn == nullptr)
               continue;
            static_cast<ModelSourceNode*>(gn->node.get())->Load(path);
            Item& it = scene->AddItem(gn->uid);
            std::copy(at, at + 3, it.pos);
            added.push_back(it.id);
         }
         else if (HasExtension(path, kWorlds))
         {
            const ImVec2 spot = SpawnSpot(scene);
            if (GraphNode* gn = SpawnNode("HDRI", "3D", spot.x, spot.y))
               if (auto* env = dynamic_cast<EnvironmentNode*>(gn->node.get()); env != nullptr && env->Load(path))
               {
                  scene->worldUid = gn->uid;
                  scene->worldColorOn = false;
               }
         }
         else if (HasExtension(path, kPictures) && sCameraView)
         {
            if (Item* cam = scene->FindItem(scene->activeCamera))
            {
               cam->backdrop.path = path;
               cam->backdrop.show = true;
            }
         }
      }
      scene->Commit();
      gSuppressUndoCheckpoints = wasSuppressed;
      if (!added.empty())
         sSel = added;
      gPatchDirty = true;
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
      unsigned backdrop = kBackdrop;
      if (sShading == Scene3DNode::Shading::Solid && sSet->bgType == 2)
         backdrop = ((unsigned)(std::clamp(sSet->bgColor[0], 0.0f, 1.0f) * 255.0f) << 16) | ((unsigned)(std::clamp(sSet->bgColor[1], 0.0f, 1.0f) * 255.0f) << 8) |
                    (unsigned)(std::clamp(sSet->bgColor[2], 0.0f, 1.0f) * 255.0f);
      else if (sShading == Scene3DNode::Shading::Solid && sSet->bgType == 1 && scene->worldColorOn)
         backdrop = ((unsigned)(std::clamp(scene->worldColor[0] * scene->worldStrength, 0.0f, 1.0f) * 255.0f) << 16) |
                    ((unsigned)(std::clamp(scene->worldColor[1] * scene->worldStrength, 0.0f, 1.0f) * 255.0f) << 8) |
                    (unsigned)(std::clamp(scene->worldColor[2] * scene->worldStrength, 0.0f, 1.0f) * 255.0f);
      dl->AddRectFilled(mn, mx, Hex(backdrop));

      StepGlide();
      BuildCam(scene, mn, mx);
      TakeDroppedFiles(scene);
      Scene3DNode::View view;
      view.view = sCam.view;
      view.proj = sCam.proj;
      std::copy(sCam.eye, sCam.eye + 3, view.eye);
      view.shading = sShading;
      view.xray = sSet->xray;
      view.wireOver = sSet->wireOver && sOverlays;
      view.grid = sGrid && sOverlays && (!sCam.ortho || sSet->orthoGrid || sCameraView);
      view.only = sLocal;
      view.gridAxis = sGridAxis;
      view.gridDist = sDist;
      if (sCameraView)
      {
         view.camera = scene->activeCamera;
         view.frame[0] = (sCam.frameMn.x - mn.x) / std::max(1.0f, size.x);
         view.frame[1] = 1.0f - (sCam.frameMx.y - mn.y) / std::max(1.0f, size.y);
         view.frame[2] = (sCam.frameMx.x - mn.x) / std::max(1.0f, size.x);
         view.frame[3] = 1.0f - (sCam.frameMn.y - mn.y) / std::max(1.0f, size.y);
      }
      const float dpi = io.DisplayFramebufferScale.y;
      const unsigned int tex = scene->RenderView((int)(size.x * dpi), (int)(size.y * dpi), view);
      if (tex != 0)
         dl->AddImage((ImTextureID)(intptr_t)tex, mn, mx, ImVec2(0, 1), ImVec2(1, 0));

      const ImVec2 mouse = io.MousePos;
      if (hovered)
         sViewMouse = mouse;
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
         if (sSet->originsAll && it.visible && !sel)
         {
            float oc[3];
            ImVec2 os;
            scene->ItemCenter(it, oc);
            if (Project(oc, os))
               dl->AddCircleFilled(os, 2.5f, Hex(0xFFA040, 0.8f));
         }
         if (it.visible)
            DrawItemExtras(dl, scene, it, col);
         const bool extras = sSet->showExtras;
         if (scene->ItemPoint(it, p))
         {
            if (kind == Kind::Camera && !looking && sSet->showCameras && extras)
               DrawCameraShape(dl, scene, it, col);
            else if (kind == Kind::Light && sSet->showLights && extras)
            {
               GraphNode* lg = sSet->lightColors ? FindNodeByUid(it.uid) : nullptr;
               auto* ln = lg != nullptr ? dynamic_cast<LightNode*>(lg->node.get()) : nullptr;
               DrawLightShape(dl, scene, it, p, ln != nullptr && !sel ? ImGui::ColorConvertFloat4ToU32(ImVec4(ln->color[0], ln->color[1], ln->color[2], 0.9f)) : col);
            }
         }
         else if (sel && sSet->outlineSel)
         {
            float lo[3], hi[3];
            if (scene->ItemBounds(it.id, lo, hi))
               DrawBox(dl, lo, hi, col);
         }
      }
      if (sOverlays && sSet->showCursor)
         DrawCursor(dl);

      if (sCameraView)
      {
         const ImU32 shade = Hex(0x000000, sSet->passAlpha);
         dl->AddRectFilled(mn, ImVec2(mx.x, sCam.frameMn.y), shade);
         dl->AddRectFilled(ImVec2(mn.x, sCam.frameMx.y), mx, shade);
         dl->AddRectFilled(ImVec2(mn.x, sCam.frameMn.y), ImVec2(sCam.frameMn.x, sCam.frameMx.y), shade);
         dl->AddRectFilled(ImVec2(sCam.frameMx.x, sCam.frameMn.y), ImVec2(mx.x, sCam.frameMx.y), shade);
         DrawCameraGuides(dl, scene);
      }

      const bool canGrab = hovered && sOp.mode == 0 && !sBoxOn;
      bool onNav = false;
      int navBall = -1;
      if (sSet->gizmoNav && sSet->miniAxes > 0)
         navBall = DrawNavGizmo(dl, mn, mx, mouse, hovered && sOp.mode == 0, onNav);
      bool onPad = false;
      if (sSet->gizmoMove)
         DrawToolpad(dl, scene, mn, mx, mouse, hovered && sOp.mode == 0, onPad);
      onNav = onNav || onPad;
      int hot = -1;
      if (selCount > 0 && sOp.mode == 0 && !cameraSelected && sOverlays && sSet->gizmoMove && !sPose.on && !sSculpt.on && !sEdit.on)
      {
         float center[3];
         PivotPoint(scene, center);
         hot = DrawGizmo(dl, scene, center, mouse, canGrab && !onNav);
      }
      sHot = hot;
      bool onHandle = false;
      if (sOp.mode == 0 && (sSet->gizmoLightSize || sSet->gizmoLightLook || sSet->gizmoCamLens || sSet->gizmoCamFocus))
         onHandle = DrawItemHandles(dl, scene, mouse, canGrab && !onNav && hot < 0);
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

      const bool posing = sOp.mode == 0 && ((sOverlays && DrawPose(dl, scene, mouse, hovered)) || DrawSculpt(dl, scene, mouse, hovered) ||
                                            DrawEdit(dl, scene, mouse, hovered));
      gView3DClaimedKeys = hovered || sOp.mode != 0 || sPose.turning || sPose.stroking || sSculpt.stroking || sSculpt.adjust != 0 || sEdit.op != 0 || sEdit.pressed;
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         sRightOk = sOp.mode == 0 && !posing;
      if (!posing)
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

      if (hovered && sOp.mode == 0 && !posing && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
      {
         if (sZoomArmed)
         {
            sZoomArmed = false;
            sBoxZoom = sBoxOn = true;
            sPressAt = mouse;
         }
         else if (onPad || onHandle)
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
            const int id = Pick(scene, mouse, true);
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
            else if (io.KeyCtrl || io.KeySuper || (sTool == 0 && sSelMode == 1))
            {
               sLasso = true;
               sLassoPts.assign(1, mouse);
            }
            else if (sTool == 0 && sSelMode == 4)
            {
               // Circle select: the drag below picks what the brush passes over.
               if (!io.KeyShift && !io.KeyCtrl && !io.KeySuper)
                  sSel.clear();
            }
            else if (sTool == 0 && (sSelMode == 2 || sSelMode == 3))
            {
               // Both read a point on the plane through the view's centre that faces the viewer.
               float o[3], d[3], hit[3], pa[3] = { sTarget[0], sTarget[1], sTarget[2] }, pn[3];
               PlacementPlane(pa, pn);
               MouseRay(mouse, o, d);
               if (PlaneHit(pa, pn, o, d, hit))
               {
                  if (sSelMode == 2)
                  {
                     PushUndoCheckpoint("Move 3D cursor");
                     std::copy(hit, hit + 3, sSet->cursor);
                  }
                  else
                  {
                     std::copy(hit, hit + 3, sMeasureA);
                     std::copy(hit, hit + 3, sMeasureB);
                     sMeasuring = sMeasured = true;
                  }
               }
            }
            else
               sBoxOn = true;
         }
      }
      if (sMeasuring || sMeasured)
      {
         if (sMeasuring)
         {
            float o[3], d[3], hit[3];
            MouseRay(mouse, o, d);
            if (PlaneHit(sMeasureA, sCam.fwd, o, d, hit))
               std::copy(hit, hit + 3, sMeasureB);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               sMeasuring = false;
               const float dx0 = sMeasureB[0] - sMeasureA[0], dy0 = sMeasureB[1] - sMeasureA[1], dz0 = sMeasureB[2] - sMeasureA[2];
               if (dx0 * dx0 + dy0 * dy0 + dz0 * dz0 > 1e-8f)
               {
                  Ruler r;
                  std::copy(sMeasureA, sMeasureA + 3, r.a);
                  std::copy(sMeasureB, sMeasureB + 3, r.b);
                  sRulers.push_back(r);
               }
               sMeasured = false;
            }
         }
         ImVec2 a, b;
         if (Project(sMeasureA, a) && Project(sMeasureB, b))
         {
            dl->AddLine(a, b, Hex(0xFFD040, 0.95f), 2.0f);
            dl->AddCircleFilled(a, 3.0f, Hex(0xFFD040));
            dl->AddCircleFilled(b, 3.0f, Hex(0xFFD040));
            const float dx = sMeasureB[0] - sMeasureA[0], dy = -(sMeasureB[2] - sMeasureA[2]), dz = sMeasureB[1] - sMeasureA[1];
            char text[96];
            snprintf(text, sizeof(text), "%.3f  (X %.2f  Y %.2f  Z %.2f)", std::sqrt(dx * dx + dy * dy + dz * dz), dx, dy, dz);
            dl->AddText(ImVec2((a.x + b.x) * 0.5f + 8.0f, (a.y + b.y) * 0.5f - 18.0f), Hex(0xFFFFFF, 0.95f), text);
         }
         if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || sTool != 0 || sSelMode != 3)
            sMeasured = sMeasuring = false;
      }
      for (const Ruler& r : sRulers)
      {
         ImVec2 a, b;
         if (Project(r.a, a) && Project(r.b, b))
         {
            dl->AddLine(a, b, Hex(0xFFD040, 0.95f), 2.0f);
            dl->AddCircleFilled(a, 3.0f, Hex(0xFFD040));
            dl->AddCircleFilled(b, 3.0f, Hex(0xFFD040));
            const float dx = r.b[0] - r.a[0], dy = -(r.b[2] - r.a[2]), dz = r.b[1] - r.a[1];
            char text[64];
            snprintf(text, sizeof(text), "%.3f", std::sqrt(dx * dx + dy * dy + dz * dz));
            dl->AddText(ImVec2((a.x + b.x) * 0.5f + 8.0f, (a.y + b.y) * 0.5f - 18.0f), Hex(0xFFFFFF, 0.95f), text);
         }
      }
      if (sTool == 0 && sSelMode == 3 && hovered && !sRulers.empty() && ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
         sRulers.pop_back();
      if (sTool == 0 && sSelMode == 4 && hovered)
      {
         // Circle select: the wheel sizes the brush, a drag selects, shift or ctrl keeps what was selected.
         if (io.MouseWheel != 0.0f)
            sCircleR = std::clamp(sCircleR * std::pow(1.1f, io.MouseWheel), 8.0f, 400.0f);
         dl->AddCircle(mouse, sCircleR, Hex(0xFFFFFF, 0.8f), 48, 1.5f);
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !sNavDrag)
            for (const Item& it : scene->Items())
            {
               float c[3];
               ImVec2 sp;
               if (!Pickable(scene, it) || !InLocal(it.id) || Selected(it.id))
                  continue;
               scene->ItemCenter(it, c);
               if (Project(c, sp) && std::hypot(mouse.x - sp.x, mouse.y - sp.y) < sCircleR)
                  sSel.push_back(it.id);
            }
      }
      if (sLasso)
      {
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            const ImVec2 last = sLassoPts.back();
            if ((mouse.x - last.x) * (mouse.x - last.x) + (mouse.y - last.y) * (mouse.y - last.y) > 9.0f)
               sLassoPts.push_back(mouse);
            const size_t n = sLassoPts.size();
            for (size_t i = 0; i < n; i++)
               dl->AddLine(sLassoPts[i], sLassoPts[(i + 1) % n], Hex(0xFFFFFF, 0.7f));
         }
         else
         {
            sLasso = false;
            if (sLassoPts.size() > 2)
            {
               if (!io.KeyShift)
                  sSel.clear();
               const size_t n = sLassoPts.size();
               for (const Item& it : scene->Items())
               {
                  float c[3];
                  scene->ItemCenter(it, c);
                  ImVec2 q;
                  if (!Pickable(scene, it) || !Project(c, q) || Selected(it.id))
                     continue;
                  bool inside = false;
                  for (size_t i = 0, j = n - 1; i < n; j = i++)
                  {
                     const ImVec2 a = sLassoPts[i], b = sLassoPts[j];
                     if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x)
                        inside = !inside;
                  }
                  if (inside)
                     sSel.push_back(it.id);
               }
            }
            sLassoPts.clear();
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
                  if (Pickable(scene, it) && Project(c, s) && s.x >= lo.x && s.x <= hi.x && s.y >= lo.y && s.y <= hi.y && !Selected(it.id))
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
            float pa[3] = { sSet->cursor[0], sSet->cursor[1], sSet->cursor[2] }, pn[3];
            PlacementPlane(pa, pn);
            if (PlaneHit(pa, pn, o, d, hit))
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
            sMenuOnItem = id != 0;
            sOpenMenu = true;
         }
      }

      DrawOverlayText(dl, scene, mn);
      if (sOp.mode != 0 && sSet->proportional)
      {
         // The reach of the pull, as a circle facing the viewer about the pivot.
         ImVec2 prev;
         bool havePrev = false;
         for (int i = 0; i <= 48; i++)
         {
            const float a = (float)i / 48.0f * 2.0f * kPi;
            float p[3];
            for (int k = 0; k < 3; k++)
               p[k] = sOp.pivot[k] + (sCam.right[k] * std::cos(a) + sCam.up[k] * std::sin(a)) * sSet->propSize;
            ImVec2 q;
            const bool ok = Project(p, q);
            if (ok && havePrev)
               dl->AddLine(prev, q, Hex(0xFFFFFF, 0.35f));
            prev = q;
            havePrev = ok;
         }
      }
      if (sOp.mode != 0)
      {
         char line[96];
         const char* orient = sOp.axis < 0 && sOp.plane < 0 ? "" : sOp.orient == 1 ? T("Local ") : sOp.orient == 2 ? T("View ") : T("Global ");
         snprintf(line, sizeof(line), "%s %s%s%s", sOp.mode == 'g' ? T("Move") : sOp.mode == 'r' ? T("Rotate") : T("Scale"), orient,
                  sOp.axis < 0 ? "" : sOp.axis == 0 ? "X " : sOp.axis == 1 ? "Z " : "Y ", sOp.typed.c_str());
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
      if (IconChip("##v3dselect", IconsInfinite::Pointer, sTool == 0, T("Select (Shift+Space, W). Right-click: box, lasso, 3D cursor, measure"))) sTool = 0;
      if (MenuParts::BeginContextItem("##v3dselmode"))
      {
         const char* modes[5] = { T("Select Box"), T("Select Lasso"), T("Cursor"), T("Measure"), T("Select Circle") };
         for (int i = 0; i < 5; i++)
            if (MenuParts::Item(modes[i], nullptr, sSelMode == i))
            {
               sSelMode = i;
               sTool = 0;
            }
         if (MenuParts::Item(T("Clear Rulers"), nullptr, false, !sRulers.empty()))
            sRulers.clear();
         MenuParts::EndPopup();
      }
      next();
      if (IconChip("##v3dmove", IconsInfinite::ToolMove, sTool == 1, T("Move tool (Shift+Space, G). G moves at once"))) sTool = 1;
      next();
      if (IconChip("##v3drotate", IconsInfinite::ToolRotate, sTool == 2, T("Rotate tool (Shift+Space, R). R rotates at once"))) sTool = 2;
      next();
      if (IconChip("##v3dscale", IconsInfinite::ToolScale, sTool == 3, T("Scale tool (Shift+Space, S). S scales at once"))) sTool = 3;
      next();
      if (IconChip("##v3dsculpt", IconsInfinite::Pencil, sSculpt.on, T("Sculpt (Ctrl+Tab)"))) ToggleSculpt(scene);
      next();
      if (IconChip("##v3dedit", IconsInfinite::EditMesh, sEdit.on, T("Edit Mode (Tab)"))) ToggleEdit(scene);
      gap();
      if (IconChip("##v3dwire", IconsInfinite::ShadeWire, sShading == Shading::Wire, T("Wireframe (Shift+Z)"))) sShading = Shading::Wire;
      next();
      if (IconChip("##v3dsolid", IconsInfinite::ShadeSolid, sShading == Shading::Solid, T("Solid: a neutral light, to see the shapes"))) sShading = Shading::Solid;
      next();
      if (IconChip("##v3dfinal", IconsInfinite::ShadeRender, sShading == Shading::Final, T("Rendered: your lights, world and render settings (Z cycles)"))) sShading = Shading::Final;
      next();
      if (IconChip("##v3dxray", IconsInfinite::Cube, sSet->xray && sShading == Shading::Solid, T("X-ray: see through surfaces in Solid (Alt+Z)")))
         sSet->xray = !sSet->xray;
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
   std::string sCard;   // the open card: rows share labels across cards ("Strength" in Shadows and AO)

   std::string RowId(const char* label) { return FormParts::Id((sCard + "/" + label).c_str()); }
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

   // For a slider: its press has already moved the value, so the checkpoint must hold the one from before it.
   template <class V>
   void UndoableFrom(V* v, V was, const char* what)
   {
      if (!ImGui::IsItemActivated())
         return;
      const V now = *v;
      *v = was;
      PushUndoCheckpoint(what);
      *v = now;
   }

   bool FloatRow(const char* label, float* v, float lo, float hi, const char* fmt, const char* undo = "Edit in 3D View")
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      const float was = *v;
      const bool changed = FieldWell::Slider(RowId(label).c_str(), v, lo, hi, fmt);
      UndoableFrom(v, was, undo);
      return changed;
   }

   bool IntInputRow(const char* label, int* v)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      FieldWell::PushStyle();
      const bool changed = FieldWell::InputInt(RowId(label).c_str(), v);
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
      if (MenuParts::BeginCombo(RowId(label).c_str(), names[shown].c_str()))
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
      bool hit = CheckBox::Box(RowId(label).c_str(), &on);
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
      const bool changed = FormParts::ColorDot(RowId(label).c_str(), rgb, 15.0f, ImGui::GetFrameHeight());
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
      sCard = title;
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
      if (Card(T("Filter"), nullptr, false))
      {
         Scene3DNode::Settings& f = *sSet;
         BoolRow(T("Match case"), &f.listCase);
         BoolRow(T("Exact match"), &f.listComplete);
         BoolRow(T("Sort A to Z"), &f.listSort);
         BoolRow(T("Objects"), &f.listObjects);
         BoolRow(T("Lights"), &f.listLights);
         BoolRow(T("Cameras"), &f.listCameras);
         ComboRow(T("State"), &f.listState, std::vector<std::string>{ T("All"), T("Visible"), T("Selected"), T("Active"), T("Selectable") });
         BoolRow(T("Invert filter"), &f.listInvert);
         BoolRow(T("Selectable column"), &f.colSelect);
         BoolRow(T("Render column"), &f.colRender);
      }
      bool scrollToActive = false;
      if (ActionButton::Draw(L("Show Active"), ImVec2(availW, 0)))
         scrollToActive = true;
      const Scene3DNode::Settings& lf = *sSet;
      const std::string search = lf.listCase ? std::string(sSearch) : lower(sSearch);
      auto passes = [&](const Item& it)
      {
         const Kind k = scene->ItemKind(it);
         if (k == Kind::Light ? !lf.listLights : k == Kind::Camera ? !lf.listCameras : !lf.listObjects)
            return false;
         const bool state = lf.listState == 0 || (lf.listState == 1 && it.visible) || (lf.listState == 2 && Selected(it.id)) ||
                            (lf.listState == 3 && !sSel.empty() && sSel.back() == it.id) || (lf.listState == 4 && !it.hideSelect);
         if (!state)
            return false;
         if (search.empty())
            return true;
         const std::string name = lf.listCase ? ItemName(it) : lower(ItemName(it));
         const bool hit = lf.listComplete ? name == search : name.find(search) != std::string::npos;
         return hit != lf.listInvert;
      };
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
      std::vector<int> order;
      for (int row = 0; row < count; row++)
         if (passes(items[row]))
            order.push_back(row);
      if (lf.listSort)
         std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return lower(ItemName(items[a])) < lower(ItemName(items[b])); });
      if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !sSel.empty() && sRename == 0 &&
          (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) && !order.empty())
      {
         // Arrow keys walk the shown list.
         auto at = std::find_if(order.begin(), order.end(), [&](int r) { return items[r].id == sSel.back(); });
         int next = at == order.end() ? 0 : (int)(at - order.begin()) + (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : -1);
         next = std::clamp(next, 0, (int)order.size() - 1);
         sSel = { items[order[next]].id };
         scrollToActive = true;
      }
      int dragFrom = -1, dragTo = -1;
      std::vector<std::pair<float, int>> rowTops;   // for Alt-drag box select
      for (int row : order)
      {
         Item& it = items[row];
         ImGui::PushID(it.id);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         rowTops.push_back({ p.y, it.id });
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
         if (!io.KeyAlt && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
         {
            ImGui::SetDragDropPayload("v3ditem", &row, sizeof(int));
            ImGui::TextUnformatted(ItemName(it).c_str());
            ImGui::EndDragDropSource();
         }
         if (ImGui::BeginDragDropTarget())
         {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("v3ditem"))
            {
               dragFrom = *(const int*)pl->Data;
               dragTo = row;
            }
            ImGui::EndDragDropTarget();
         }
         if (scrollToActive && !sSel.empty() && sSel.back() == it.id)
            ImGui::SetScrollHereY();
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            StartRename(it);
         if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
         {
            if (!Selected(it.id))
               sSel = { it.id };
            sMenuOnItem = true;
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
         float colX = availW - 24.0f;
         if (lf.colRender)
         {
            colX -= 24.0f;
            ImGui::SetCursorScreenPos(ImVec2(p.x + colX, p.y));
            if (ImGui::InvisibleButton("##rnd", ImVec2(24.0f, rowH)))
            {
               PushUndoCheckpoint("Render toggle in 3D View");
               it.render = !it.render;
               scene->Commit();
            }
            glyph::Draw(dl, ImVec2(p.x + colX + 12.0f, midY), tok::icon_sm, text, it.render ? IconsInfinite::ViewCamera : IconsInfinite::EyeOff);
         }
         if (lf.colSelect)
         {
            colX -= 24.0f;
            ImGui::SetCursorScreenPos(ImVec2(p.x + colX, p.y));
            if (ImGui::InvisibleButton("##sel", ImVec2(24.0f, rowH)))
            {
               PushUndoCheckpoint("Selectable toggle in 3D View");
               it.hideSelect = !it.hideSelect;
               scene->Commit();
            }
            glyph::Draw(dl, ImVec2(p.x + colX + 12.0f, midY), tok::icon_sm, text, it.hideSelect ? IconsInfinite::EyeOff : IconsInfinite::Cube);
         }
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
      // Item keys act on the picked rows while the list is hovered or was the last thing clicked.
      static bool sListActive = false;
      const bool listHovered = count > 0 && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         sListActive = listHovered;
      if (count > 0)
         ImGui::EndChild();
      if ((listHovered || sListActive) && sRename == 0 && !io.WantTextInput && !sSel.empty())
      {
         const bool command = io.KeyCtrl || io.KeySuper;
         if (!command && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace) || ImGui::IsKeyPressed(ImGuiKey_X)))
            DeleteSelected(scene);
         else if (command && ImGui::IsKeyPressed(ImGuiKey_C))
            sCopiedItems = SelectedItems(scene);
         else if ((command && ImGui::IsKeyPressed(ImGuiKey_D)) || (io.KeyShift && !command && ImGui::IsKeyPressed(ImGuiKey_D)))
            DuplicateSelected(scene, io.MousePos, false);
      }
      if ((listHovered || sListActive) && sRename == 0 && !io.WantTextInput && !sCopiedItems.empty() &&
          (io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_V))
         PasteItems(scene);
      static float sBoxY0 = 0.0f;
      static bool sBoxing = false;
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         sBoxing = false;
      if (io.KeyAlt && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !rowTops.empty())
      {
         // Alt-drag over the rows selects every row the drag passes.
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         {
            sBoxY0 = ImGui::GetMousePos().y;
            sBoxing = true;
         }
         if (sBoxing && ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            const float y0 = std::min(sBoxY0, ImGui::GetMousePos().y), y1 = std::max(sBoxY0, ImGui::GetMousePos().y);
            std::vector<int> hit;
            for (const auto& r : rowTops)
               if (r.first + rowH > y0 && r.first < y1)
                  hit.push_back(r.second);
            if (!hit.empty())
               sSel = hit;
         }
      }
      if (dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo)
      {
         PushUndoCheckpoint("Reorder in 3D View");
         Item moved = items[dragFrom];
         items.erase(items.begin() + dragFrom);
         items.insert(items.begin() + dragTo, moved);
         scene->Commit();
      }
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
      BoolRow(T("Temperature"), &light->useTemperature);
      if (light->useTemperature)
         FloatRow(T("Kelvin"), &light->temperature, 800.0f, 20000.0f, "%.0f K");
      FloatRow(T("Strength"), &light->intensity, 0.0f, 20.0f, "%.2f");
      FloatRow(T("Exposure"), &light->exposure, -10.0f, 10.0f, "%.2f");
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
         BoolRow(T("Show cone"), &light->showCone);
      }
      if (light->type == 5)
      {
         FloatRow(T("Width"), &light->areaWidth, 0.05f, 20.0f, "%.2f");
         FloatRow(T("Height"), &light->areaHeight, 0.05f, 20.0f, "%.2f");
         BoolRow(T("Normalize"), &light->normalize);
      }
      if (light->type == 1 || light->type == 4 || light->type == 5)
      {
         FloatRow(T("Falloff"), &light->falloff, 0.0f, 4.0f, "%.2f");
         BoolRow(T("Soft falloff"), &light->softFalloff);
         BoolRow(T("Custom distance"), &light->useCutoff);
         if (light->useCutoff)
            FloatRow(T("Cutoff"), &light->cutoffDistance, 0.01f, 100.0f, "%.2f");
      }
      FloatRow(T("Diffuse"), &light->diffuse, 0.0f, 2.0f, "%.2f");
      if (light->type != 3)
      {
         FloatRow(T("Specular"), &light->specular, 0.0f, 2.0f, "%.2f");
         FloatRow(T("Transmission"), &light->transmission, 0.0f, 2.0f, "%.2f");
      }
      if (light->type != 1 && light->type != 3)
      {
         // One light casts (0 = the first), so the caster's box cannot be cleared: checking another light moves it.
         bool casts = ShadowCaster(scene) == item;
         const bool wasCaster = casts;
         ImGui::BeginDisabled(wasCaster);
         if (BoolRow(T("Casts shadow"), &casts, "Shadow light") && casts)
            sSet->shadowItem = item->id;
         ImGui::EndDisabled();
         if (casts)
         {
            BoolRow(T("Shadow"), &light->castShadow);
            FloatRow(T("Radius"), &light->shadowRadius, 0.0f, 5.0f, "%.2f");
            FloatRow(T("Filter"), &light->shadowFilter, 0.0f, 10.0f, "%.2f");
            ComboRow(T("Resolution"), &light->shadowRes, { T("From the look"), "1024", "2048", "4096" });
            FloatRow(T("Clip start"), &light->shadowClip, 0.05f, 20.0f, "%.2f");
         }
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

   // Blender's focal length holds when the frame or sensor changes; only the view angle moves.
   template <typename Change>
   void KeepingLenses(Scene3DNode* scene, Change&& change)
   {
      std::vector<std::pair<CameraNode*, float>> lenses;
      for (const Item& it : scene->Items())
      {
         GraphNode* gn = FindNodeByUid(it.uid);
         if (auto* cam = gn != nullptr ? dynamic_cast<CameraNode*>(gn->node.get()) : nullptr)
            lenses.push_back({ cam, cam->FocalLength(RenderAspect(scene)) });
      }
      change();
      for (auto& lens : lenses)
         lens.first->SetFocalLength(lens.second, RenderAspect(scene));
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
               float mm = cam->FocalLength(RenderAspect(scene));
               if (FloatRow(T("Focal length"), &mm, 1.0f, 300.0f, "%.0f mm"))
                  cam->SetFocalLength(mm, RenderAspect(scene));
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
         // Blender holds the focal length while the sensor changes; the view angle follows.
         const float mm = cam->FocalLength(RenderAspect(scene));
         int fit = cam->sensorFit;
         if (ComboRow(T("Sensor fit"), &fit, cam->SensorFitNames()))
         {
            cam->sensorFit = fit;
            cam->SetFocalLength(mm, RenderAspect(scene));
         }
         struct Preset { const char* name; float w, h; };
         static const Preset kPresets[] = { { "Full Frame", 36.0f, 24.0f }, { "APS-C", 23.6f, 15.6f }, { "APS-C (Canon)", 22.3f, 14.9f },
                                            { "Micro Four Thirds", 17.3f, 13.0f }, { "Super 35", 24.89f, 18.66f },
                                            { "16 mm film", 10.26f, 7.49f }, { "IMAX 70", 70.41f, 52.63f } };
         static int sPreset = 0;
         std::vector<std::string> presetNames{ T("Custom") };
         for (const Preset& pr : kPresets)
            presetNames.push_back(T(pr.name));
         if (ComboRow(T("Preset"), &sPreset, presetNames) && sPreset > 0)
         {
            cam->sensorWidth = kPresets[sPreset - 1].w;
            cam->sensorHeight = kPresets[sPreset - 1].h;
            cam->SetFocalLength(mm, RenderAspect(scene));
         }
         float* side = cam->sensorFit == 2 ? &cam->sensorHeight : &cam->sensorWidth;
         float sensor = *side;
         if (FloatRow(cam->sensorFit == 0 ? T("Sensor") : cam->sensorFit == 1 ? T("Width") : T("Height"), &sensor, 1.0f, 100.0f, "%.0f mm"))
         {
            *side = sensor;
            cam->SetFocalLength(mm, RenderAspect(scene));
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
      if (Card(T("Viewport Display")))
      {
         FloatRow(T("Size"), &cam->displaySize, 0.01f, 10.0f, "%.2f");
         BoolRow(T("Limits"), &cam->showLimits);
         BoolRow(T("Mist"), &cam->showMist);
         BoolRow(T("Name"), &cam->showName);
         BoolRow(T("Sensor"), &cam->showSensor);
      }
      Scene3DNode::Backdrop& back = item->backdrop;
      bool backShow = back.show;
      const bool backOpen = Card(T("Background Image"), &backShow, false);
      if (backShow != back.show)
      {
         PushUndoCheckpoint("Edit in 3D View");
         back.show = backShow;
         scene->Commit();
      }
      if (backOpen)
      {
         bool edit = false;
         const std::string file = back.path.empty() ? std::string(T("Open image")) : back.path.substr(back.path.find_last_of("/\\") + 1);
         if (ActionButton::Draw(file.c_str(), ImVec2(availW, 0)))
         {
            const std::string path = Platform::OpenImageDialog();
            if (!path.empty())
            {
               PushUndoCheckpoint("Set camera background image");
               back.path = path;
               back.show = true;
               edit = true;
            }
         }
         if (!back.path.empty())
         {
            if (ActionButton::Draw(L("Remove image"), ImVec2(availW, 0)))
            {
               PushUndoCheckpoint("Remove camera background image");
               back.path.clear();
               edit = true;
            }
            ImGui::BeginDisabled(!back.show);
            edit |= FloatRow(T("Opacity"), &back.alpha, 0.0f, 1.0f, "%.2f");
            int depth = back.front ? 1 : 0;
            if (ComboRow(T("Depth"), &depth, { T("Back"), T("Front") }))
            {
               back.front = depth == 1;
               edit = true;
            }
            edit |= ComboRow(T("Frame method"), &back.fit, { T("Stretch"), T("Fit"), T("Crop") });
            edit |= FloatRow(T("Offset X"), &back.offset[0], -2.0f, 2.0f, "%.3f");
            edit |= FloatRow(T("Offset Y"), &back.offset[1], -2.0f, 2.0f, "%.3f");
            edit |= FloatRow(T("Rotation"), &back.rotation, -180.0f, 180.0f, "%.1f");
            edit |= FloatRow(T("Scale"), &back.scale, 0.01f, 10.0f, "%.3f");
            edit |= BoolRow(T("Flip horizontally"), &back.flipX);
            edit |= BoolRow(T("Flip vertically"), &back.flipY);
            ImGui::EndDisabled();
         }
         if (edit)
            scene->Commit();
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
         if (IntInputRow(T("Blades"), &look.dofBlades))
            look.dofBlades = look.dofBlades < 3 ? (look.dofBlades > 0 ? 3 : 0) : std::min(look.dofBlades, 16);
         FloatRow(T("Rotation"), &look.dofRotation, -180.0f, 180.0f, "%.0f");
         FloatRow(T("Ratio"), &look.dofRatio, 0.01f, 2.0f, "%.2f");
         ImGui::EndDisabled();
      }
   }

   void DrawObjectTab(Scene3DNode* scene, float availW)
   {
      Item* item = sSel.empty() ? nullptr : scene->FindItem(sSel.back());   // the active one
      if (item == nullptr)
      {
         Hint(T("Select one object."));
         return;
      }
      scene->motionShown = item->id;
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
         auto field = [&](const char* label, float* v, float speed, const char* fmt = "%.2f")
         {
            FieldLabel(label);
            ImGui::SetNextItemWidth(sFieldW);
            const bool changed = ImGui::DragFloat3(RowId(label).c_str(), v, speed, 0.0f, 0.0f, fmt);
            Undoable("Edit in 3D View");
            return changed;
         };
         FieldWell::PushStyle();
         bool changed = false;
         int changedChannel = -1;
         const float lenK = LengthFactor();
         float loc[3] = { item->pos[0] * lenK, -item->pos[2] * lenK, item->pos[1] * lenK };
         if (field(T("Location"), loc, 0.01f * lenK, LengthFormat()))
         {
            item->pos[0] = loc[0] / lenK; item->pos[2] = -loc[1] / lenK; item->pos[1] = loc[2] / lenK;
            changed = true;
            changedChannel = 0;
         }
         float rot[3];
         ToDisplayRotation(item->rot, rot);
         const bool radians = sSet->rotUnits == 1;
         if (radians)
            for (float& r : rot)
               r *= 0.0174532925f;
         ComboRow(T("Rotation mode"), &sItemRotMode, { T("XYZ Euler"), T("Quaternion"), T("Axis Angle") }, "Rotation mode");
         bool rotEdited = false;
         if (sItemRotMode != 0)
         {
            float q[4];
            QuaternionOf(item->rot, q);
            FieldLabel(T("Rotation"));
            ImGui::SetNextItemWidth(sFieldW);
            if (sItemRotMode == 1)
            {
               if (ImGui::DragFloat4("##v3dirotq", q, 0.01f, -1.0f, 1.0f, "%.3f"))
               {
                  SetQuaternion(q, item->rot);
                  rotEdited = true;
               }
            }
            else
            {
               float aa[4];
               AxisAngleOf(q, aa);
               if (ImGui::DragFloat4("##v3dirotaa", aa, 0.5f, 0.0f, 0.0f, "%.2f"))
               {
                  QuaternionFromAxisAngle(aa, q);
                  SetQuaternion(q, item->rot);
                  rotEdited = true;
               }
            }
            Undoable("Edit in 3D View");
         }
         else if (field(T("Rotation"), rot, radians ? 0.01f : 0.5f, radians ? "%.3f rad" : "%.2f"))
         {
            if (radians)
               for (float& r : rot)
                  r *= 57.2957795f;
            FromDisplayRotation(rot, item->rot);
            rotEdited = true;
         }
         if (rotEdited)
         {
            changed = true;
            changedChannel = 1;
         }
         float scl[3] = { item->scale[0], item->scale[2], item->scale[1] };
         if (field(T("Scale"), scl, 0.01f))
         {
            item->scale[0] = scl[0]; item->scale[2] = scl[1]; item->scale[1] = scl[2];
            changed = true;
            changedChannel = 2;
         }
         float lo[3], hi[3];
         const bool turned = std::fabs(item->rot[0]) + std::fabs(item->rot[1]) + std::fabs(item->rot[2]) > 0.01f;
         const Kind boxKind = scene->ItemKind(*item);
         if ((boxKind == Kind::Geometry || boxKind == Kind::Picture) && scene->ItemBounds(item->id, lo, hi))
         {
            // The box the item fills; editing it rescales. Only exact while the item is not turned.
            float dim[3] = { hi[0] - lo[0], hi[2] - lo[2], hi[1] - lo[1] };
            ImGui::BeginDisabled(turned);
            if (field(T("Dimensions"), dim, 0.01f))
            {
               const float was[3] = { hi[0] - lo[0], hi[2] - lo[2], hi[1] - lo[1] };
               for (int a = 0; a < 3; a++)
                  if (was[a] > 1e-5f && dim[a] > 1e-5f)
                     item->scale[kDisp[a]] *= dim[a] / was[a];
               changed = true;
            }
            ImGui::EndDisabled();
         }
         if (changed && ImGui::GetIO().KeyAlt && sSel.size() > 1)
         {
            // Alt while editing: the same value goes to every selected item.
            for (int id : sSel)
               if (Item* o = scene->FindItem(id))
                  if (o != item)
                     for (int k = 0; k < 3; k++)
                     {
                        if (changedChannel == 0) o->pos[k] = item->pos[k];
                        if (changedChannel == 1) o->rot[k] = item->rot[k];
                        if (changedChannel == 2) o->scale[k] = item->scale[k];
                     }
         }
         if (changed)
            scene->Commit();
         FieldWell::PopStyle();
      }
      if (Card(T("Delta Transform"), nullptr, false))
      {
         // Same axes as Transform above: shown Z up.
         auto field = [&](const char* label, float* v, float speed)
         {
            FieldLabel(label);
            ImGui::SetNextItemWidth(sFieldW);
            const bool changed = ImGui::DragFloat3(FormParts::Id((std::string("d") + label).c_str()).c_str(), v, speed, 0.0f, 0.0f, "%.2f");
            Undoable("Edit in 3D View");
            return changed;
         };
         FieldWell::PushStyle();
         bool changed = false;
         float loc[3] = { item->dpos[0], -item->dpos[2], item->dpos[1] };
         if (field(T("Location"), loc, 0.01f))
         {
            item->dpos[0] = loc[0]; item->dpos[2] = -loc[1]; item->dpos[1] = loc[2];
            changed = true;
         }
         float rot[3];
         ToDisplayRotation(item->drot, rot);
         if (field(T("Rotation"), rot, 0.5f))
         {
            FromDisplayRotation(rot, item->drot);
            changed = true;
         }
         float scl[3] = { item->dscale[0], item->dscale[2], item->dscale[1] };
         if (field(T("Scale"), scl, 0.01f))
         {
            item->dscale[0] = scl[0]; item->dscale[2] = scl[1]; item->dscale[1] = scl[2];
            changed = true;
         }
         FieldWell::PopStyle();
         if (ActionButton::Draw(T("Transforms to Deltas"), ImVec2(-1.0f, 0.0f)))
         {
            PushUndoCheckpoint("Transforms to Deltas");
            Scene3DNode::TransformsToDeltas(*item);
            changed = true;
         }
         if (changed)
            scene->Commit();
      }
      if (Card(T("Lock")))
      {
         const char* axes[3] = { "X", "Y", "Z" };
         const char* names[3] = { T("Location"), T("Rotation"), T("Scale") };
         for (int row = 0; row < 3; row++)
         {
            FieldLabel(names[row]);
            for (int a = 0; a < 3; a++)
            {
               const unsigned bit = (1u << kDisp[a]) << (row * 3);
               bool on = (item->lock & bit) != 0;
               const std::string id = std::string("##v3dlock") + std::to_string(row) + axes[a];
               if (CheckBox::Box(id.c_str(), &on))
               {
                  PushUndoCheckpoint("Lock in 3D View");
                  item->lock = on ? (item->lock | bit) : (item->lock & ~bit);
                  scene->Commit();
               }
               FormParts::Inline(0.0f, tok::space_1);
               ImGui::AlignTextToFramePadding();
               ImGui::TextDisabled("%s", axes[a]);
               if (a < 2)
                  FormParts::Inline(0.0f, tok::space_3);
            }
         }
      }
      if (Card(T("Visibility")))
      {
         if (BoolRow(T("Show in renders"), &item->render, "Show in renders"))
            scene->Commit();
         if (BoolRow(T("Cast shadows"), &item->castShadow, "Cast shadows"))
            scene->Commit();
         if (BoolRow(T("Hide selection"), &item->hideSelect, "Hide selection"))
            scene->Commit();
      }
      if (Card(T("Viewport display")))
      {
         bool changed = BoolRow(T("Name"), &item->showName, "Show name");
         changed |= BoolRow(T("Axes"), &item->showAxes, "Show axes");
         changed |= BoolRow(T("Bounds"), &item->showBounds, "Show bounds");
         if (item->showBounds)
            changed |= ComboRow(T("Shape"), &item->boundsType, { T("Box"), T("Sphere"), T("Cylinder"), T("Cone"), T("Capsule") }, "Bounds shape");
         if (changed)
            scene->Commit();
      }
      if (scene->ItemKind(*item) == Kind::Picture && Card(T("Picture")))
      {
         if (BoolRow(T("Lit"), &item->pictureLit, "Picture lit"))
            scene->Commit();
      }
      if (scene->ItemKind(*item) == Kind::Geometry && Card(T("Viewport Display")))
      {
         if (ColorRow(T("Color"), item->color, "Edit in 3D View"))
            scene->Commit();
      }
      if (scene->ItemKind(*item) == Kind::Geometry && Card(T("Pass Index")))
      {
         if (IntInputRow(T("Index"), &item->passIndex))
         {
            item->passIndex = std::clamp(item->passIndex, 0, 255);
            scene->Commit();
         }
      }
      if (Card(T("Follow")))
      {
         std::vector<std::string> names = { T("None") };
         std::vector<int> ids = { 0 };
         for (const Item& other : scene->Items())
            if (other.id != item->id && !scene->FollowLoops(item->id, other.id))
            {
               names.push_back(ItemName(other));
               ids.push_back(other.id);
            }
         int pick = std::max(0, (int)(std::find(ids.begin(), ids.end(), item->follow) - ids.begin()));
         if (pick >= (int)ids.size())
            pick = 0;
         bool changed = ComboRow(T("Object"), &pick, names, "Follow in 3D View");
         if (changed)
            item->follow = ids[pick];
         if (item->follow != 0)
            changed |= ComboRow(T("Carry"), &item->followMode, { T("Location, rotation and scale"), T("Location only") },
                                "Follow carry");
         if (changed)
         {
            item->followed[0] = NAN;   // keep where it is now, relative to the new target
            PushUndoCheckpoint("Follow in 3D View");
            scene->Commit();
         }
      }
      if (Card(T("Motion")))
      {
         auto field = [&](const char* label, float* v, float speed)
         {
            FieldLabel(label);
            ImGui::SetNextItemWidth(sFieldW);
            const bool changed = ImGui::DragFloat3(RowId(label).c_str(), v, speed, 0.0f, 0.0f, "%.2f");
            Undoable("Edit motion in 3D View");
            return changed;
         };
         FieldWell::PushStyle();
         bool changed = field(T("Location"), item->motion, 0.01f);
         changed |= field(T("Rotation"), item->motion + 3, 0.5f);
         changed |= field(T("Scale"), item->motion + 6, 0.01f);
         FieldWell::PopStyle();
         if (ActionButton::Draw(L("Reset motion"), ImVec2(availW, 0)))
         {
            const float rest[9] = { 0, 0, 0, 0, 0, 0, 1, 1, 1 };
            std::copy(rest, rest + 9, item->motion);
            PushUndoCheckpoint("Reset motion in 3D View");
            changed = true;
         }
         if (changed)
            scene->Commit();
      }
      GraphNode* gn = FindNodeByUid(item->uid);
      if (scene->ItemKind(*item) == Kind::Geometry || (scene->ItemKind(*item) == Kind::Picture && item->pictureLit))
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
      {
         Hint(T("Select a camera or a light."));
         return;
      }
      if (Card(T("Look at")))
      {
         std::vector<std::string> names = { T("None") };
         std::vector<int> ids = { 0 };
         for (const Item& other : scene->Items())
            if (other.id != item->id)
            {
               names.push_back(ItemName(other));
               ids.push_back(other.id);
            }
         int pick = std::max(0, (int)(std::find(ids.begin(), ids.end(), item->lookAt) - ids.begin()));
         if (pick >= (int)ids.size())
            pick = 0;
         if (ComboRow(T("Object"), &pick, names, "Look at in 3D View"))
         {
            item->lookAt = ids[pick];
            scene->Commit();
         }
         if (item->lookAt != 0)
         {
            const std::vector<std::string> axes = { "+X", "+Y", "+Z", "-X", "-Y", "-Z" };
            bool changed = ComboRow(T("Track axis"), &item->trackAxis, axes, "Look at track axis");
            changed |= ComboRow(T("Up axis"), &item->upAxis, { "X", "Y", "Z" }, "Look at up axis");
            changed |= FloatRow(T("Influence"), &item->lookInfluence, 0.0f, 1.0f, "%.2f", "Look at influence");
            changed |= BoolRow(T("Target Z"), &item->lookTargetZ, "Look at target up");
            if (changed)
               scene->Commit();
         }
      }
   }

   // The Material node that colours an item: the item's own node, or the first one met walking up
   // through nodes that pass the surface through.
   GraphNode* MaterialOf(const Item& item)
   {
      GraphNode* gn = FindNodeByUid(item.uid);
      auto* geo = gn != nullptr ? dynamic_cast<IGeometrySource*>(gn->node.get()) : nullptr;
      for (int hops = 0; geo != nullptr && hops < 32; hops++)
      {
         if (auto* mat = dynamic_cast<MaterialNode*>(geo))
            for (GraphNode& other : gNodes)
               if (other.node.get() == mat)
                  return &other;
         geo = geo->PassthroughSource();
      }
      return nullptr;
   }

   // A new Material between the item's node and the view, starting from the look it already had.
   void AddMaterial(Scene3DNode* scene, Item& item)
   {
      GraphNode* src = FindNodeByUid(item.uid);
      auto* geo = src != nullptr ? dynamic_cast<IGeometrySource*>(src->node.get()) : nullptr;
      if (geo == nullptr)
         return;
      PushUndoCheckpoint("Add Material");
      const Material was = geo->GetMaterial();
      const int srcIndex = src->index;
      const ImVec2 at = NodePos(*src);
      GraphNode* made = SpawnNode("Material", "3D", at.x + 320.0f, at.y);   // can move gNodes: src is stale after this
      auto* mat = made != nullptr ? dynamic_cast<MaterialNode*>(made->node.get()) : nullptr;
      std::string error;
      if (mat == nullptr || !ConnectNodes(srcIndex, 0, made->index, 0, error))
         return;
      std::copy(was.color, was.color + 3, mat->color);
      mat->metallic = was.metallic;
      mat->roughness = was.roughness;
      mat->opacity = was.opacity;
      mat->shading = was.shading;
      std::copy(was.emissionColor, was.emissionColor + 3, mat->emissionColor);
      mat->emission = was.emission;
      mat->ior = was.ior;
      mat->specular = was.specular;
      mat->transmission = was.transmission;
      mat->transmissionRoughness = was.transmissionRoughness;
      mat->clearcoat = was.clearcoat;
      mat->clearcoatRoughness = was.clearcoatRoughness;
      mat->subsurface = was.subsurface;
      std::copy(was.subsurfaceColor, was.subsurfaceColor + 3, mat->subsurfaceColor);
      mat->subsurfaceRadius = was.subsurfaceRadius;
      item.uid = made->uid;
      scene->Commit();
   }

   // Nodes whose picture can feed a map pin.
   std::vector<GraphNode*> ImageChoices(const GraphNode& self)
   {
      std::vector<GraphNode*> out;
      for (GraphNode& gn : gNodes)
      {
         INode* n = gn.node.get();
         // Only nodes that make pixels (the viewport panel's test: no audio
         // sinks like Audio Out, no meters), and none of the 3D ones.
         if (&gn == &self || gn.hiddenFromCanvas || dynamic_cast<IGeometrySource*>(n) || dynamic_cast<EnvironmentNode*>(n) ||
             dynamic_cast<IPredictor*>(n) || dynamic_cast<IPaletteSource*>(n) || dynamic_cast<OutputNode*>(n) ||
             dynamic_cast<Scene3DNode*>(n) || !CanShowInViewportPanel(gn))
            continue;
         out.push_back(&gn);
      }
      return out;
   }

   // The Material tab's ball: a sphere wearing the selected Material's surface.
   class PreviewBall : public INode, public IGeometrySource
   {
   public:
      PreviewBall() { sphere.shape = 2; sphere.detail = 32; }
      unsigned int GetOutputTexture() override { return 0; }
      int GetOutputWidth() const override { return 0; }
      int GetOutputHeight() const override { return 0; }
      void CookIfNeeded(int frameId) override { sphere.CookIfNeeded(frameId); }
      const Mesh& GetMesh() override { return sphere.GetMesh(); }
      unsigned long long MeshRevision() override { return sphere.MeshRevision(); }
      Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
      Material GetMaterial() const override { return src->GetMaterial(); }
      unsigned long long MaterialRevision() const override { return src->MaterialRevision(); }
      unsigned int GetSurfaceTexture() override { return src->GetSurfaceTexture(); }
      unsigned int GetMaterialTexture(int map) override { return src->GetMaterialTexture(map); }
      unsigned long long SurfaceTextureRevision() const override { return src->SurfaceTextureRevision(); }

      IGeometrySource* src = nullptr;
      GeometryNode sphere;
   };

   // Renders only when the material or one of its maps changed.
   unsigned int PreviewBallTexture(MaterialNode* mat)
   {
      static PreviewBall* sBall = nullptr;   // kept for the app's life: GL objects outlive no context
      static Render3DNode* sRender = nullptr;
      static std::vector<unsigned long long> sKey;
      static int sFrame = 0;
      if (sRender == nullptr)
      {
         sBall = new PreviewBall();
         sRender = new Render3DNode();
         sRender->width = sRender->height = 192.0f;
         sRender->camDistance = 2.6f;
         sRender->camAzimuth = 35.0f;
         sRender->camElevation = 20.0f;
         sRender->geometry[0] = sBall;
      }
      sBall->src = mat;
      std::vector<unsigned long long> key = { (unsigned long long)(uintptr_t)mat, mat->MaterialRevision(),
                                              mat->SurfaceTextureRevision() };
      for (int map = 0; map < kMapCount; map++)
         key.push_back(mat->GetMaterialTexture(map));
      if (key != sKey)
      {
         sKey = key;
         sRender->CookIfNeeded(++sFrame);
      }
      return sRender->GetOutputTexture();
   }

   void DrawPreviewBall(MaterialNode* mat, float availW)
   {
      const unsigned int tex = PreviewBallTexture(mat);
      const float side = std::min(availW, 160.0f);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - side) * 0.5f);
      ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(side, side), ImVec2(0, 1), ImVec2(1, 0));
   }

   void DrawMaterialTab(Scene3DNode* scene, float availW)
   {
      Item* item = sSel.size() == 1 ? scene->FindItem(sSel[0]) : nullptr;
      if (item == nullptr || scene->ItemKind(*item) != Kind::Geometry)
      {
         Hint(T("Select one object."));
         return;
      }
      GraphNode* gn = MaterialOf(*item);
      auto* mat = gn != nullptr ? dynamic_cast<MaterialNode*>(gn->node.get()) : nullptr;
      if (mat == nullptr)
      {
         // The column starts a spacing high for a card; a bare button needs it back.
         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y);
         if (ActionButton::Draw(L("Add Material"), ImVec2(availW, 0)))
            AddMaterial(scene, *item);
         return;
      }
      DrawPreviewBall(mat, availW);
      if (const std::vector<FactoryPresets::Preset>* presets = FactoryPresets::For(gn->typeName))
      {
         // Picking lays a preset over the defaults; the row then shows "-" again.
         std::vector<std::string> names = { "-" };
         for (const FactoryPresets::Preset& p : *presets)
            names.push_back(p.name);
         int pick = 0;
         if (ComboRow(T("Preset"), &pick, names, "Material preset") && pick > 0)
         {
            FactoryPresets::Apply(mat, gn->typeName, pick - 1);
            gPatchDirty = true;
         }
      }
      if (Card(T("Surface")))
      {
         ComboRow(T("Shading"), &mat->shading, GeometryNode::ShadingNames(), "Material shading");
         ColorRow(T("Colour"), mat->color, "Material colour");
         FloatRow(T("Metallic"), &mat->metallic, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Roughness"), &mat->roughness, 0.02f, 1.0f, "%.2f");
         FloatRow(T("Specular"), &mat->specular, 0.0f, 1.0f, "%.2f");
         FloatRow(T("IOR"), &mat->ior, 1.0f, 3.0f, "%.2f");
         FloatRow(T("Opacity"), &mat->opacity, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Transmission"), &mat->transmission, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Clearcoat"), &mat->clearcoat, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Sheen"), &mat->sheen, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Subsurface"), &mat->subsurface, 0.0f, 1.0f, "%.2f");
      }
      if (Card(T("Emission")))
      {
         ColorRow(T("Colour"), mat->emissionColor, "Material emission colour");
         FloatRow(T("Strength"), &mat->emission, 0.0f, 8.0f, "%.2f");
      }
      if (Card(T("Maps")))
      {
         static const char* const kMaps[kMapCount] = { "Base colour", "Roughness", "Metallic", "Normal",
                                                       "AO", "Emission", "Clearcoat", "Sheen",
                                                       "Opacity", "Transmission" };
         const std::vector<GraphNode*> choices = ImageChoices(*gn);
         std::vector<std::string> names = { T("None") };
         for (GraphNode* c : choices)
            names.push_back(NodeTitle(*c));
         names.push_back(T("Load image..."));
         const int matIndex = gn->index;
         for (int map = 0; map < kMapCount; map++)
         {
            INode* src = mat->MapInput(map).GetSource();
            int pick = 0;
            for (size_t i = 0; i < choices.size(); i++)
               if (choices[i]->node.get() == src)
                  pick = (int)i + 1;
            if (ComboRow(T(kMaps[map]), &pick, names, "Material map"))
            {
               if (pick > (int)choices.size())
               {
                  const std::string path = Platform::OpenImageDialog();
                  if (!path.empty())
                     gPendingMapLoad = { mat, map, path };
               }
               else if (pick == 0)
                  DisconnectDstPin(gn->InputPinId(1 + map));
               else
               {
                  std::string error;
                  ConnectNodes(choices[pick - 1]->index, 0, matIndex, 1 + map, error);
               }
            }
         }
         if (mat->MapInput(kMapNormal).IsConnected())
            FloatRow(T("Normal strength"), &mat->normalStrength, 0.0f, 4.0f, "%.2f");
      }
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
            FloatRow(T("Sun threshold"), &env->sunThreshold, 0.0f, 100.0f, "%.1f");
            if (env->sunThreshold > 0.0f)
            {
               FloatRow(T("Sun angle"), &env->sunAngle, 0.0f, 10.0f, "%.2f");
               BoolRow(T("Sun shadow"), &env->sunShadow);
               ComboRow(T("Sun resolution"), &env->sunShadowRes, { T("From the look"), "1024", "2048", "4096" });
               FloatRow(T("Sun filter"), &env->sunShadowFilter, 0.0f, 10.0f, "%.2f");
            }
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

   // Render slots: the newest result and the one before it, to open side by side.
   std::string sSlot[2];
   void KeepRender(const std::string& path)
   {
      sLastRender = path;
      if (sSlot[0] != path)
      {
         sSlot[1] = sSlot[0];
         sSlot[0] = path;
      }
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
         out->offlineFps = std::max(1, (int)(sSet->fps / std::max(1, sSet->frameStep)));
         KeepRender(path);
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
      KeepRender(sStillPath);
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
      for (int i = 0; i < 2; i++)
      {
         if (sSlot[i].empty() || !std::filesystem::exists(sSlot[i], ec))
            continue;
         Hint(sSlot[i].c_str());
         const std::string label = std::string(i == 0 ? T("Reveal Latest") : T("Reveal Previous"));
         if (ActionButton::Draw(label.c_str(), ImVec2(availW, 0)))
            Platform::RevealInFileManager(sSlot[i]);
      }
   }

   void DrawRenderTab(Scene3DNode* scene)
   {
      Render3DNode& look = scene->Look();
      ComboRow(T("Engine"), &look.engine, Render3DNode::EngineNames());
      if (look.engine == 1 && Card(T("Path Tracing")))
      {
         struct { const char* label; const char* id; int* v; int hi; } rows[] = {
            { T("Samples"), "##v3dptsamples", &look.ptSamples, 4096 }, { T("Bounces"), "##v3dptbounces", &look.ptBounces, 16 } };
         for (const auto& r : rows)
         {
            FieldLabel(r.label);
            ImGui::SetNextItemWidth(sFieldW);
            const int was = *r.v;
            FieldWell::Slider(r.id, r.v, 1, r.hi, "%d");
            UndoableFrom(r.v, was, "Edit in 3D View");
         }
      }
      if (Card(T("Sampling")))
      {
         ComboRow(T("Viewport"), &look.samples, Render3DNode::SampleNames());
         FieldLabel(T("Render"));
         ImGui::SetNextItemWidth(sFieldW);
         const int was = look.supersample;
         FieldWell::Slider("##v3dsuper", &look.supersample, 1, 64, "%d");
         UndoableFrom(&look.supersample, was, "Edit in 3D View");
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
      }
      if (Card(T("Ambient Occlusion")))
      {
         FloatRow(T("Strength"), &look.aoStrength, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Distance"), &look.aoRadius, 0.05f, 3.0f, "%.2f");
      }
      if (Card(T("Shading")))
      {
         ComboRow(T("Mode"), &look.viewShading, Render3DNode::ViewShadingNames());
         if (look.viewShading == 1)
         {
            ComboRow(T("Lighting"), &look.solidLighting, Render3DNode::SolidLightingNames());
            if (look.solidLighting == 1)
               ComboRow(T("MatCap"), &look.matcap, Render3DNode::MatcapNames());
            if (look.solidLighting == 0)
            {
               BoolRow(T("World space"), &look.studioWorld);
               if (look.studioWorld)
                  FloatRow(T("Rotation"), &look.studioRotation, -180.0f, 180.0f, "%.0f");
               BoolRow(T("Specular"), &look.solidSpecular);
            }
            ComboRow(T("Color"), &look.solidColor, Render3DNode::SolidColorNames());
            if (look.solidColor == 2)
               ColorRow(T("Single color"), look.solidSingle);
            BoolRow(T("Outline"), &sSet->outline);
            if (sSet->outline)
               ColorRow(T("Outline color"), sSet->outlineColor);
            ComboRow(T("Background"), &sSet->bgType, { T("Theme"), T("World"), T("Viewport") });
            if (sSet->bgType == 2)
               ColorRow(T("Background color"), sSet->bgColor);
         }
      }
      if (Card(T("Cavity"), &look.cavity))
      {
         ImGui::BeginDisabled(!look.cavity);
         ComboRow(T("Type"), &look.cavityType, Render3DNode::CavityTypeNames());
         if (look.cavityType != 1)
         {
            FloatRow(T("Ridge"), &look.cavityRidge, 0.0f, 2.5f, "%.2f");
            FloatRow(T("Valley"), &look.cavityValley, 0.0f, 2.5f, "%.2f");
            FloatRow(T("Distance"), &look.cavityDistance, 0.01f, 2.0f, "%.2f");
         }
         if (look.cavityType != 0)
         {
            FloatRow(T("Curvature ridge"), &look.curvatureRidge, 0.0f, 2.0f, "%.2f");
            FloatRow(T("Curvature valley"), &look.curvatureValley, 0.0f, 2.0f, "%.2f");
         }
         ImGui::EndDisabled();
      }
      if (Card(T("Bloom")))
      {
         FloatRow(T("Strength"), &look.bloom, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Threshold"), &look.bloomThreshold, 0.5f, 8.0f, "%.2f");
         FloatRow(T("Radius"), &look.bloomRadius, 0.0f, 1.0f, "%.2f");
      }
      if (Card(T("Motion Blur"), &look.motionBlur))
      {
         ImGui::BeginDisabled(!look.motionBlur);
         ComboRow(T("Position"), &look.motionPosition, Render3DNode::MotionPositionNames());
         FloatRow(T("Shutter"), &look.motionShutter, 0.0f, 2.0f, "%.2f");
         FloatRow(T("Max blur"), &look.motionMax, 1.0f, 256.0f, "%.0f px");
         ImGui::EndDisabled();
      }
      if (Card(T("Film")))
      {
         BoolRow(T("Transparent"), &sSet->transparent);
         FloatRow(T("Filter size"), &sSet->filterSize, 0.0f, 4.0f, "%.2f px");
         FloatRow(T("Dither"), &sSet->dither, 0.0f, 2.0f, "%.2f");
         BoolRow(T("Simplify"), &sSet->simplify);
      }
      if (Card(T("Render Region")))
      {
         BoolRow(T("Region"), &sSet->border);
         if (sSet->border)
         {
            FloatRow(T("Min X"), &sSet->borderMin[0], 0.0f, 1.0f, "%.3f");
            FloatRow(T("Min Y"), &sSet->borderMin[1], 0.0f, 1.0f, "%.3f");
            FloatRow(T("Max X"), &sSet->borderMax[0], 0.0f, 1.0f, "%.3f");
            FloatRow(T("Max Y"), &sSet->borderMax[1], 0.0f, 1.0f, "%.3f");
            BoolRow(T("Crop to region"), &sSet->crop);
         }
      }
      if (Card(T("Color Management")))
      {
         ComboRow(T("View"), &look.tonemap, Render3DNode::TonemapNames());
         FloatRow(T("Exposure"), &look.exposure, 0.1f, 4.0f, "%.2f");
         FloatRow(T("Gamma"), &sSet->gamma, 0.2f, 3.0f, "%.2f");
         ComboRow(T("Look"), &sSet->look, std::vector<std::string>{ T("None"), T("Low contrast"), T("Medium contrast"), T("High contrast"), T("Faded") });
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
         FloatRow(T("Base"), &sSet->fpsBase, 1.0f, 1.01f, "%.3f");
         FloatRow(T("Pixel aspect X"), &sSet->pixelAspect[0], 0.1f, 10.0f, "%.2f");
         FloatRow(T("Pixel aspect Y"), &sSet->pixelAspect[1], 0.1f, 10.0f, "%.2f");
      }
      if (Card(T("Frame Range")))
      {
         int seconds = (int)sSet->seconds;
         if (IntInputRow(T("Seconds"), &seconds))
            sSet->seconds = (float)std::clamp(seconds, 1, 3600);
         FieldLabel(T("Frames"));
         ImGui::Text("%d", (int)(sSet->seconds * sSet->fps) / std::max(1, sSet->frameStep));
         IntInputRow(T("Step"), &sSet->frameStep);
         sSet->frameStep = std::clamp(sSet->frameStep, 1, 100);
      }
      if (Card(T("Stamp"), &sSet->stamp))
      {
         ImGui::BeginDisabled(!sSet->stamp);
         BoolRow(T("Date"), &sSet->stampDate);
         BoolRow(T("Time"), &sSet->stampTime);
         BoolRow(T("Frame"), &sSet->stampFrame);
         BoolRow(T("Frame range"), &sSet->stampRange);
         BoolRow(T("Camera"), &sSet->stampCamera);
         BoolRow(T("Lens"), &sSet->stampLens);
         BoolRow(T("Scene"), &sSet->stampScene);
         BoolRow(T("Render time"), &sSet->stampRender);
         BoolRow(T("Note"), &sSet->stampNote);
         if (sSet->stampNote)
         {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s", scene->stampNote.c_str());
            FieldLabel(T("Note text"));
            ImGui::SetNextItemWidth(sFieldW);
            if (ImGui::InputText("##v3dstampnote", buf, sizeof(buf)))
               scene->stampNote = buf;
            Undoable("Edit in 3D View");
         }
         BoolRow(T("Labels"), &sSet->stampLabels);
         FloatRow(T("Font size"), &sSet->stampSize, 6.0f, 96.0f, "%.0f");
         ColorRow(T("Text"), sSet->stampFg);
         ColorRow(T("Background"), sSet->stampBg);
         ImGui::EndDisabled();
      }
      if (Card(T("Output")))
      {
         static const std::vector<std::string> kDepths = { "8", "16" };
         int depth = look.floatOutput ? 1 : 0;
         if (ComboRow(T("Color depth"), &depth, kDepths))
            look.floatOutput = depth == 1;
         ComboRow(T("Pass"), &look.renderPass, Render3DNode::RenderPassNames());
         if (look.renderPass >= 1 && look.renderPass <= 9)
            FloatRow(T("Alpha threshold"), &look.passThreshold, 0.0f, 1.0f, "%.2f");
         if (look.renderPass == 5)
         {
            FloatRow(T("Mist start"), &look.mistStart, 0.0f, 100.0f, "%.2f");
            FloatRow(T("Mist depth"), &look.mistDepth, 0.01f, 200.0f, "%.2f");
            ComboRow(T("Falloff"), &look.mistFalloff, Render3DNode::MistFalloffNames());
            FloatRow(T("Intensity"), &look.mistIntensity, 0.0f, 1.0f, "%.2f");
            FloatRow(T("Height"), &look.mistHeight, 0.0f, 100.0f, "%.2f");
         }
         ComboRow(T("Projection"), &look.projection, Render3DNode::ProjectionNames());
         if (look.projection == Render3DNode::kEquirect || look.projection == Render3DNode::kCylindrical)
         {
            FloatRow(T("Min latitude"), &look.latMin, -90.0f, 90.0f, "%.0f");
            FloatRow(T("Max latitude"), &look.latMax, -90.0f, 90.0f, "%.0f");
            FloatRow(T("Min longitude"), &look.lonMin, -180.0f, 180.0f, "%.0f");
            FloatRow(T("Max longitude"), &look.lonMax, -180.0f, 180.0f, "%.0f");
         }
         if (look.projection == Render3DNode::kFisheye)
         {
            ComboRow(T("Lens"), &look.fisheyeLens, Render3DNode::FisheyeLensNames());
            FloatRow(T("Dome angle"), &look.domeAngle, 60.0f, 360.0f, "%.0f");
            FloatRow(T("Dome tilt"), &look.domeTilt, 0.0f, 90.0f, "%.0f");
         }
      }
      ImGui::BeginDisabled(gOfflineRender.active || scene->activeCamera == 0);
      if (ActionButton::Draw(L("Render Image"), ImVec2(availW, 0)))
         RenderStill(scene);
      if (ActionButton::Draw(L("Render Animation"), ImVec2(availW, 0), ActionButton::Kind::Primary))
         StartRender(scene);
      ImGui::EndDisabled();
      ImGui::BeginDisabled(!gOfflineRender.active);
      if (ActionButton::Draw(L("Cancel Render"), ImVec2(availW, 0)))
         ArrangeRenderCancelActive();
      ImGui::EndDisabled();
      if (scene->activeCamera == 0)
         Hint(T("Add a camera to render."));
      DrawRenderResult(availW);
   }

   const std::vector<std::string>& OrientationNames()
   {
      static std::vector<std::string> names;
      names = { T("Global"), T("Local"), T("View"), T("Cursor") };
      for (int i = 0; i < sSet->customCount; i++)
         names.push_back(std::string(T("Custom")) + " " + std::to_string(i + 1));
      return names;
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
         BoolRow(T("Only locations"), &set.onlyLocations);
         ImGui::BeginDisabled(sSel.empty() || set.customCount >= 4);
         if (ActionButton::Draw(L("Create Orientation"), ImVec2(availW, 0)))
            CreateOrientation(scene);
         ImGui::EndDisabled();
         ImGui::BeginDisabled(set.orientation < 4);
         if (ActionButton::Draw(L("Delete Orientation"), ImVec2(availW, 0)))
            DeleteOrientation();
         ImGui::EndDisabled();
      }
      if (Card(T("Preferences"), nullptr, false))
      {
         ComboRow(T("Mini axes"), &set.miniAxes, std::vector<std::string>{ T("Off"), T("Simple axes"), T("Navigation") });
         FloatRow(T("Mini axes size"), &set.miniSize, 40.0f, 160.0f, "%.0f");
         FloatRow(T("Mini axes brightness"), &set.miniBright, 0.0f, 1.0f, "%.2f");
         FloatRow(T("Navigation gizmo size"), &set.gizmoNavSize, 40.0f, 160.0f, "%.0f");
         FloatRow(T("Transform gizmo size"), &set.gizmoSize, 40.0f, 160.0f, "%.0f");
         FloatRow(T("Smooth view"), &set.smoothTime, 0.0f, 1.0f, "%.2f s");
         FloatRow(T("Rotation step"), &set.rotStep, 1.0f, 90.0f, "%.0f");
         BoolRow(T("Auto perspective"), &set.autoPerspective);
         BoolRow(T("Continuous grab"), &set.continuousGrab);
         FloatRow(T("Orbit sensitivity"), &set.orbitSens, 0.1f, 5.0f, "%.2f");
      }
      if (Card(T("Units")))
      {
         ComboRow(T("System"), &set.unitSystem, std::vector<std::string>{ T("None"), T("Metric"), T("Imperial") });
         if (set.unitSystem == 1)
            ComboRow(T("Length"), &set.lengthUnit, std::vector<std::string>{ T("Meters"), T("Centimeters"), T("Millimeters"), T("Kilometers") });
         if (set.unitSystem == 2)
            ComboRow(T("Length"), &set.lengthUnit, std::vector<std::string>{ T("Feet"), T("Inches"), T("Yards") });
         FloatRow(T("Unit scale"), &set.unitScale, 0.001f, 1000.0f, "%.3f");
         ComboRow(T("Rotation"), &set.rotUnits, std::vector<std::string>{ T("Degrees"), T("Radians") });
      }
      if (Card(T("Snap")))
      {
         BoolRow(T("On"), &set.snap);
         FloatRow(T("Move"), &set.snapMove, 0.01f, 5.0f, "%.2f");
         FloatRow(T("Rotate"), &set.snapRot, 1.0f, 90.0f, "%.0f");
         FloatRow(T("Scale"), &set.snapScale, 0.01f, 1.0f, "%.2f");
         BoolRow(T("To other objects"), &set.snapObjects);
         BoolRow(T("Use for move"), &set.snapTranslate);
         BoolRow(T("Use for rotate"), &set.snapRotate);
         BoolRow(T("Use for scale"), &set.snapScaleOn);
         BoolRow(T("Absolute increment"), &set.snapAbsolute);
         FloatRow(T("Rotate precision"), &set.snapRotFine, 0.1f, 15.0f, "%.1f");
         ComboRow(T("Snap target"), &set.snapTarget, std::vector<std::string>{ T("Closest"), T("Center"), T("Median"), T("Active") });
         BoolRow(T("Selectable only"), &set.snapSelectable);
         BoolRow(T("Align rotation to target"), &set.snapAlign);
      }
      if (Card(T("Proportional Editing")))
      {
         BoolRow(T("On (O)"), &set.proportional);
         ComboRow(T("Falloff"), &set.propFalloff, std::vector<std::string>{ T("Smooth"), T("Sphere"), T("Root"), T("Linear"), T("Sharp"), T("Constant") });
         FloatRow(T("Size"), &set.propSize, 0.05f, 50.0f, "%.2f");
         BoolRow(T("Projected from view"), &set.projectedProp);
      }
      if (Card(T("Placement"), nullptr, false))
      {
         ComboRow(T("Depth"), &set.planeDepth, std::vector<std::string>{ T("Cursor Plane"), T("Cursor View") });
         ComboRow(T("Orientation"), &set.planeOrient, std::vector<std::string>{ T("Default"), T("Surface") });
         ImGui::BeginDisabled(set.planeDepth == 1 || set.planeOrient == 1);
         BoolRow(T("Auto axis"), &set.planeAuto);
         ImGui::BeginDisabled(set.planeAuto);
         ComboRow(T("Plane axis"), &set.planeAxis, std::vector<std::string>{ "X", "Y", "Z" });
         ImGui::EndDisabled();
         ImGui::EndDisabled();
      }
      if (Card(T("3D Cursor")))
      {
         FieldLabel(T("Location"));
         ImGui::SetNextItemWidth(sFieldW);
         FieldWell::PushStyle();
         float cur[3] = { set.cursor[0], -set.cursor[2], set.cursor[1] };
         if (ImGui::DragFloat3("##v3dcursor", cur, 0.01f, 0.0f, 0.0f, "%.2f"))
         {
            set.cursor[0] = cur[0]; set.cursor[2] = -cur[1]; set.cursor[1] = cur[2];
         }
         FieldWell::PopStyle();
         Undoable("Move 3D cursor");
         ComboRow(T("Rotation mode"), &set.cursorRotMode, { T("XYZ Euler"), T("Quaternion"), T("Axis Angle") }, "3D cursor rotation mode");
         FieldLabel(T("Rotation"));
         ImGui::SetNextItemWidth(sFieldW);
         FieldWell::PushStyle();
         if (set.cursorRotMode == 0)
         {
            float disp[3];
            ToDisplayRotation(set.cursorRot, disp);
            if (ImGui::DragFloat3("##v3dcursorrot", disp, 0.5f, 0.0f, 0.0f, "%.1f"))
               FromDisplayRotation(disp, set.cursorRot);
         }
         else
         {
            float q[4];
            CursorQuaternion(q);
            if (set.cursorRotMode == 1)
            {
               if (ImGui::DragFloat4("##v3dcursorrot", q, 0.01f, -1.0f, 1.0f, "%.3f"))
                  SetCursorQuaternion(q);
            }
            else
            {
               const float half = std::acos(std::clamp(q[0], -1.0f, 1.0f)), sn = std::max(1e-6f, std::sin(half));
               float aa[4] = { half * 2.0f * 180.0f / kPi, q[1] / sn, q[2] / sn, q[3] / sn };
               if (half < 1e-4f)
                  aa[1] = aa[2] = 0.0f, aa[3] = 1.0f;
               if (ImGui::DragFloat4("##v3dcursorrot", aa, 0.5f, 0.0f, 0.0f, "%.2f"))
               {
                  const float an = std::max(1e-6f, std::sqrt(aa[1] * aa[1] + aa[2] * aa[2] + aa[3] * aa[3]));
                  const float h = aa[0] * kPi / 360.0f, sh = std::sin(h) / an;
                  const float nq[4] = { std::cos(h), aa[1] * sh, aa[2] * sh, aa[3] * sh };
                  SetCursorQuaternion(nq);
               }
            }
         }
         FieldWell::PopStyle();
         Undoable("Turn 3D cursor");
         ImGui::BeginDisabled(sSel.empty());
         if (ActionButton::Draw(L("Cursor to Selected"), ImVec2(availW, 0)))
            CursorToSelected(scene);
         if (ActionButton::Draw(L("Selection to Cursor"), ImVec2(availW, 0)))
            SelectionToCursor(scene);
         ImGui::EndDisabled();
      }
      if (Card(T("View"), nullptr, false))
      {
         FloatRow(T("Focal length"), &set.viewLens, 10.0f, 250.0f, "%.0f mm");
         FloatRow(T("Clip start"), &set.clipStart, 0.001f, 10.0f, "%.3f");
         FloatRow(T("Clip end"), &set.clipEnd, 10.0f, 10000.0f, "%.0f");
         BoolRow(T("Lock to 3D cursor"), &set.lockCursor);
      }
      if (Card(T("Viewport Visibility"), nullptr, false))
      {
         BoolRow(T("Cameras shown"), &set.showCameras);
         BoolRow(T("Cameras selectable"), &set.pickCameras);
         BoolRow(T("Lights shown"), &set.showLights);
         BoolRow(T("Lights selectable"), &set.pickLights);
      }
      if (Card(T("Navigation")))
      {
         BoolRow(T("Zoom to mouse"), &set.zoomToMouse);
         BoolRow(T("Orbit around selection"), &set.orbitSelection);
         BoolRow(T("Invert zoom direction"), &set.invertZoom);
      }
      if (Card(T("Overlays")))
      {
         BoolRow(T("Text info"), &set.textInfo);
         BoolRow(T("Statistics"), &set.stats);
         BoolRow(T("Origins (all)"), &set.originsAll);
         BoolRow(T("3D cursor"), &set.showCursor);
         BoolRow(T("Extras"), &set.showExtras);
         BoolRow(T("Light colors"), &set.lightColors);
         BoolRow(T("Outline selected"), &set.outlineSel);
         BoolRow(T("Wireframe"), &set.wireOver);
         BoolRow(T("Performance"), &set.showPerf);
      }
      if (Card(T("Grid"), nullptr, false))
      {
         BoolRow(T("Orthographic grid"), &set.orthoGrid);
         BoolRow(T("Axis X"), &set.axisX);
         BoolRow(T("Axis Y"), &set.axisY);
         BoolRow(T("Axis Z"), &set.axisZ);
         FloatRow(T("Scale"), &set.gridScale, 0.001f, 1000.0f, "%.3f");
         if (IntInputRow(T("Lines"), &set.gridLines))
            set.gridLines = std::clamp(set.gridLines, 1, 200);
         if (IntInputRow(T("Subdivisions"), &set.gridSubdiv))
            set.gridSubdiv = std::clamp(set.gridSubdiv, 2, 100);
      }
      if (Card(T("Gizmos")))
      {
         BoolRow(T("Navigate"), &set.gizmoNav);
         BoolRow(T("Move, rotate, scale"), &set.gizmoMove);
         BoolRow(T("Light size"), &set.gizmoLightSize);
         BoolRow(T("Light look-at"), &set.gizmoLightLook);
         BoolRow(T("Camera lens"), &set.gizmoCamLens);
         BoolRow(T("Camera focus distance"), &set.gizmoCamFocus);
      }
      if (Card(T("Passepartout")))
         FloatRow(T("Opacity"), &set.passAlpha, 0.0f, 1.0f, "%.2f");
      if (Card(T("Safe Areas"), &set.safe))
      {
         FloatRow(T("Title"), &set.safeTitle, 0.0f, 0.3f, "%.3f");
         FloatRow(T("Action"), &set.safeAction, 0.0f, 0.3f, "%.3f");
         BoolRow(T("Center-cut"), &set.safeCenter);
      }
      if (Card(T("Composition Guides")))
      {
         ColorRow(T("Colour"), set.guideColor);
         BoolRow(T("Thirds"), &set.thirds);
         BoolRow(T("Center"), &set.center);
         BoolRow(T("Diagonal"), &set.diagonal);
         BoolRow(T("Golden"), &set.golden);
         BoolRow(T("Golden triangle A"), &set.goldenTriA);
         BoolRow(T("Golden triangle B"), &set.goldenTriB);
         BoolRow(T("Harmonious triangle A"), &set.harmonyTriA);
         BoolRow(T("Harmonious triangle B"), &set.harmonyTriB);
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
      const bool isGeometry = kind == Kind::Geometry && sSel.size() == 1;
      if (sTab == kMaterialTab && !isGeometry)
         sTab = item != nullptr ? 3 : 0;
      const struct { int tab; const char* id; const char* icon; const char* tip; bool show; } kTabs[8] = {
         { kSceneTab, "##t6", IconsInfinite::Arrange, T("Scene"), true },
         { 0, "##t0", IconsInfinite::ShadeRender, T("Render"), true },
         { 1, "##t1", IconsInfinite::Folder, T("Output"), true },
         { 2, "##t2", IconsInfinite::World, T("World"), true },
         { 3, "##t3", IconsInfinite::Cube, T("Object"), item != nullptr },
         { 4, "##t4", dataGlyph, kind == Kind::Light ? T("Light") : T("Camera"), hasData },
         { kMaterialTab, "##t7", IconsInfinite::ShadeSolid, T("Material"), isGeometry },
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
      sCard.clear();
      if (sTab == kSceneTab) DrawOutliner(scene, availW, ImGui::GetContentRegionAvail().y);
      else if (sTab == 0) DrawRenderTab(scene);
      else if (sTab == 1) DrawOutputTab(scene, availW);
      else if (sTab == 2) DrawWorldTab(scene, availW);
      else if (sTab == 3) DrawObjectTab(scene, availW);
      else if (sTab == 4) DrawDataTab(scene, availW);
      else if (sTab == kMaterialTab) DrawMaterialTab(scene, availW);
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
         // On an object: what to do with it. On empty space: add, select, view.
         const bool any = !sSel.empty();
         Item* one = sSel.size() == 1 ? scene->FindItem(sSel[0]) : nullptr;
         if (sMenuOnItem && any)
         {
            if (MenuParts::Item(T("Duplicate"), "Shift+D")) DuplicateSelected(scene, ImGui::GetIO().MousePos);
            if (MenuParts::Item(T("Copy"))) sCopiedItems = SelectedItems(scene);
            if (MenuParts::Item(T("Paste"), nullptr, false, !sCopiedItems.empty())) PasteItems(scene);
            if (one != nullptr && MenuParts::Item(T("Rename"), "F2")) StartRename(*one);
            if (MenuParts::Item(T("Hide"), "H")) HideSelected(scene);
            if (MenuParts::Item(T("Remove"), "X")) DeleteSelected(scene);
            MenuParts::Separator();
            if (MenuParts::Item(T("Frame"), "F")) FrameItems(scene, false);
            bool grouped = false;
            for (int id : sSel)
               if (const Item* it = scene->FindItem(id); it != nullptr && it->group != 0)
                  grouped = true;
            if (sSel.size() > 1 && MenuParts::Item(T("Group"), MODKEY "+G")) GroupSelected(scene, true);
            if (grouped && MenuParts::Item(T("Ungroup"), MODKEY "+Alt+G")) GroupSelected(scene, false);
            if (one != nullptr && MenuParts::SubMenu(T("Source"), true))
            {
               for (GraphNode* gn : SourceChoices(scene, *one))
                  if (MenuParts::Item((gn->typeName + " " + std::to_string(gn->index)).c_str(), nullptr, gn->uid == one->uid) && gn->uid != one->uid)
                     SetSource(scene, *one, gn->uid);
               ImGui::EndMenu();
            }
            if (MenuParts::SubMenu(T("Object"), true))
            {
               DrawObjectItems(scene);
               ImGui::EndMenu();
            }
            if (one != nullptr)
            {
               MenuParts::Separator();
               if (MenuParts::Item(T("Go to node")))
                  if (GraphNode* gn = FindNodeByUid(one->uid))
                  {
                     gRequestSelectNodeIndex = gn->index;
                     gRequestFitViewNodeIndex = gn->index;
                  }
            }
         }
         else
         {
            if (MenuParts::SubMenu(T("Add"), true))
            {
               DrawAddItems(scene);
               ImGui::EndMenu();
            }
            if (MenuParts::SubMenu(T("Select"), true))
            {
               DrawSelectItems(scene);
               ImGui::EndMenu();
            }
            MenuParts::Separator();
            if (MenuParts::Item(T("Frame all"), "Home")) FrameItems(scene, true);
            if (MenuParts::SubMenu(T("View"), true))
            {
               DrawViewItems(scene);
               ImGui::EndMenu();
            }
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
      if (scene->activeCamera == id)
         EnterCamera();
      else
         sCameraView = false;
   }

   void AddLightNow(Scene3DNode* scene, int type)
   {
      PushUndoCheckpoint("Add light to 3D View");
      const ImVec2 at = SpawnSpot(scene);
      GraphNode* gn = SpawnNode("Light", "3D", at.x, at.y);
      if (gn == nullptr)
         return;
      dynamic_cast<LightNode*>(gn->node.get())->type = type;
      Item& placed = scene->AddItem(gn->uid);
      PlaceNew(placed);
      sSel = { placed.id };
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
      else if (todo.what == Pending::Sculpt)
         AddSculpt(scene, todo.item);
      else if (todo.what == Pending::EditMesh)
         AddEditMesh(scene, todo.item);
      else if (todo.what == Pending::Node)
         if (GraphNode* gn = FindNodeByUid(todo.uid))
            View3DAddNode(*gn);
   }

   // A card's Open in 3D View: the item whose chain holds the node, then its mode.
   struct OpenRequest
   {
      uint64_t uid = 0;
      int mode = 0; // 1 edit, 2 sculpt, 3 pose
   } sOpenReq;

   void RunOpenRequest(Scene3DNode* scene)
   {
      const OpenRequest req = sOpenReq;
      sOpenReq = {};
      GraphNode* target = FindNodeByUid(req.uid);
      if (target == nullptr)
         return;
      auto find = [&]() -> int
      {
         for (const Item& it : scene->Items())
         {
            GraphNode* gn = FindNodeByUid(it.uid);
            auto* top = gn != nullptr ? dynamic_cast<IGeometrySource*>(gn->node.get()) : nullptr;
            for (IGeometrySource* s = top; s != nullptr; s = s->PassthroughSource())
               if (dynamic_cast<INode*>(s) == target->node.get())
                  return it.id;
         }
         return -1;
      };
      int id = find();
      if (id < 0)
      {
         View3DAddNode(*target);
         id = find();
      }
      if (id < 0)
         return;
      sSel = { id };
      if (sEdit.on)
         ToggleEdit(scene);
      if (sSculpt.on)
         ToggleSculpt(scene);
      if (sPose.on)
         TogglePose(scene);
      if (req.mode == 1)
         ToggleEdit(scene);
      else if (req.mode == 2)
         ToggleSculpt(scene);
      else if (req.mode == 3)
         TogglePose(scene);
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
      if (sOpenReq.uid != 0)
         RunOpenRequest(scene);
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
   // Ones: the side tab. Tens: 1 looks through the camera. Hundreds: 1 Solid with X-ray, 2 Solid with the wire overlay.
   // Thousands: the right-click menu, 1 on an item, 2 on empty space.
   sTab = state % 10;
   if (state / 1000 != 0)
   {
      sMenuOnItem = state / 1000 == 1;
      sOpenMenu = true;
   }
   sCameraView = (state / 10) % 10 == 1;
   const int look = state / 100;
   sShading = look == 0 ? Shading::Final : Shading::Solid;
   sSideOpen = true;
   if (Scene3DNode* scene = FindScene())
   {
      scene->settings.xray = look == 1;
      scene->settings.wireOver = look == 2;
      scene->settings.thirds = sCameraView;
      if (const Item* cam = scene->FindItem(scene->activeCamera))
         sSel = { cam->id };
   }
}

#ifdef IMGUI_ENABLE_TEST_ENGINE
// UI audit: one side tab, with the item it needs picked (an object for Object and Material, a light for Light).
bool View3DAuditState(int tab)
{
   Scene3DNode* scene = FindScene();
   if (scene == nullptr)
      return false;
   const Kind want = tab == 3 || tab == kMaterialTab ? Kind::Geometry : tab == 4 ? Kind::Light : Kind::Missing;
   sSel.clear();
   if (want != Kind::Missing)
      for (Item& it : scene->Items())
         if (scene->ItemKind(it) == want)
         {
            sSel = { it.id };
            if (tab == kMaterialTab && MaterialOf(it) == nullptr)
               AddMaterial(scene, it);
            break;
         }
   sTab = tab;
   sSideOpen = true;
   sShading = Shading::Solid;
   return want == Kind::Missing || !sSel.empty();
}
#endif

void View3DAddLight(int type)
{
   if (Scene3DNode* scene = FindScene())
      AddLightNow(scene, type);
}

void View3DOpenNode(const GraphNode& gn, int mode)
{
   sOpenReq = { gn.uid, mode };
   gView3DOpen = true;
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

// The Material tab's two actions, for the self-test.
GraphNode* View3DMaterialOf(Scene3DNode* scene, int itemId)
{
   const Scene3DNode::Item* item = scene->FindItem(itemId);
   return item != nullptr ? MaterialOf(*item) : nullptr;
}

unsigned int View3DPreviewBall(MaterialNode* mat) { return PreviewBallTexture(mat); }
void View3DAddMaterial(Scene3DNode* scene, int itemId)
{
   if (Scene3DNode::Item* item = scene->FindItem(itemId))
      AddMaterial(scene, *item);
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
