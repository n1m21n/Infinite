// The 3D View: a final-stage composer over node outputs. Everything here edits
// the Scene3DNode's placements; the nodes themselves are only ever read, so a
// source keeps animating under whatever arrangement is made on top of it.
#include "app/AppCommon.h"
#include "app/AppShared.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/CheckBox.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/PanelFrame.h"
#include "nodes/EnvironmentNode.h"
#include "nodes/OutputNode.h"
#include "nodes/Scene3DNode.h"
#include "nodes/SceneNodes.h"

#include <algorithm>
#include <cmath>
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
   constexpr float kSideW = 268.0f;
   constexpr float kPi = 3.14159265f;
   constexpr float kFov = 45.0f;
   const float kAxisCol[3][3] = { { 0.90f, 0.33f, 0.33f }, { 0.45f, 0.80f, 0.35f }, { 0.33f, 0.55f, 0.95f } };

   using Item = Scene3DNode::Item;
   using Shading = Scene3DNode::Shading;

   Shading sShading = Shading::Solid;
   bool sCameraView = false;
   bool sSnap = false;
   bool sGrid = true;
   float sSnapMove = 0.25f, sSnapRot = 15.0f, sSnapScale = 0.1f;
   int sTool = 1;                  // 0 select, 1 move, 2 rotate, 3 scale
   std::vector<int> sSel;
   int sDuration = 15, sFps = 60;

   float sTarget[3] = { 0, 0, 0 };
   float sDist = 6.0f, sAz = 0.7f, sEl = 0.4f;

   struct Cam
   {
      Mat4 view, proj, viewProj;
      float eye[3] = { 0, 0, 0 };
      float right[3], up[3], fwd[3];
      ImVec2 mn, mx;
   } sCam;

   struct Op
   {
      char mode = 0;               // 'g', 'r', 's'
      int axis = -1;
      bool local = false;
      bool gizmo = false;
      ImVec2 start;
      float pivot[3] = { 0, 0, 0 };
      std::vector<Item> before;
      std::string typed;
   } sOp;


   float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

   ImU32 AxisColor(int a, float alpha = 1.0f)
   {
      return ImGui::ColorConvertFloat4ToU32(ImVec4(kAxisCol[a][0], kAxisCol[a][1], kAxisCol[a][2], alpha));
   }

   Scene3DNode* FindScene()
   {
      for (GraphNode& gn : gNodes)
         if (auto* s = dynamic_cast<Scene3DNode*>(gn.node.get()))
            return s;
      return nullptr;
   }

   GraphNode* SceneGraphNode()
   {
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<Scene3DNode*>(gn.node.get()) != nullptr)
            return &gn;
      return nullptr;
   }

   bool Selected(int id) { return std::find(sSel.begin(), sSel.end(), id) != sSel.end(); }

   std::string ItemName(const Item& it)
   {
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

   void BuildCam(Scene3DNode* scene, ImVec2 mn, ImVec2 mx)
   {
      sCam.mn = mn;
      sCam.mx = mx;
      const float aspect = (mx.x - mn.x) / std::max(1.0f, mx.y - mn.y);
      CameraNode* lens = nullptr;
      if (!(sCameraView && scene->CameraView(sCam.view, sCam.eye, &lens)))
      {
         EyeFromOrbit(sCam.eye);
         const float up[3] = { 0, 1, 0 };
         sCam.view = Mat4::LookAt(sCam.eye, sTarget, up);
      }
      sCam.proj = lens != nullptr ? lens->ProjectionMatrix(aspect)
                                  : Mat4::Perspective(kFov * kPi / 180.0f, aspect, 0.05f, 100.0f);
      sCam.viewProj = Mat4::Multiply(sCam.proj, sCam.view);
      for (int k = 0; k < 3; k++)
      {
         sCam.right[k] = sCam.view.m[k * 4];
         sCam.up[k] = sCam.view.m[k * 4 + 1];
         sCam.fwd[k] = -sCam.view.m[k * 4 + 2];
      }
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
      const float depth = std::max(0.05f, (p[0] - sCam.eye[0]) * sCam.fwd[0] + (p[1] - sCam.eye[1]) * sCam.fwd[1] +
                                             (p[2] - sCam.eye[2]) * sCam.fwd[2]);
      return 2.0f * depth / std::max(1e-3f, sCam.proj.m[5]) / std::max(1.0f, sCam.mx.y - sCam.mn.y);
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

   void OpAxis(const Item& first, float out[3])
   {
      out[0] = out[1] = out[2] = 0.0f;
      if (sOp.axis < 0)
         return;
      if (!sOp.local)
      {
         out[sOp.axis] = 1.0f;
         return;
      }
      float r[3][3];
      RotationOf(first.rot, r);
      for (int k = 0; k < 3; k++)
         out[k] = r[k][sOp.axis];
   }

   float SnapTo(float v, float step) { return step > 0.0f ? std::round(v / step) * step : v; }

   void BeginOp(char mode, Scene3DNode* scene, ImVec2 mouse, int axis = -1, bool gizmo = false)
   {
      sOp = Op();
      sOp.mode = mode;
      sOp.axis = axis;
      sOp.gizmo = gizmo;
      sOp.start = mouse;
      for (int id : sSel)
         if (Item* it = scene->FindItem(id))
         {
            sOp.before.push_back(*it);
            for (int k = 0; k < 3; k++)
               sOp.pivot[k] += it->pos[k];
         }
      if (sOp.before.empty())
      {
         sOp.mode = 0;
         return;
      }
      for (int k = 0; k < 3; k++)
         sOp.pivot[k] /= (float)sOp.before.size();
   }

   void ApplyOp(Scene3DNode* scene, ImVec2 mouse)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const bool snap = sSnap != io.KeyCtrl;
      const float fine = io.KeyShift ? 0.1f : 1.0f;
      const bool hasTyped = !sOp.typed.empty() && sOp.typed != "-";
      const float typed = hasTyped ? std::strtof(sOp.typed.c_str(), nullptr) : 0.0f;
      const ImVec2 d((mouse.x - sOp.start.x) * fine, (mouse.y - sOp.start.y) * fine);
      float axis[3];
      OpAxis(sOp.before[0], axis);
      ImVec2 c(0, 0);
      Project(sOp.pivot, c);

      float move[3] = { 0, 0, 0 };
      float angle = 0.0f;
      float factor = 1.0f;
      if (sOp.mode == 'g')
      {
         if (sOp.axis < 0)
         {
            const float u = UnitAt(sOp.pivot);
            for (int k = 0; k < 3; k++)
               move[k] = sCam.right[k] * d.x * u - sCam.up[k] * d.y * u;
         }
         else
         {
            const float tip[3] = { sOp.pivot[0] + axis[0], sOp.pivot[1] + axis[1], sOp.pivot[2] + axis[2] };
            ImVec2 t = c;
            Project(tip, t);
            const float sx = t.x - c.x, sy = t.y - c.y;
            const float len2 = std::max(1.0f, sx * sx + sy * sy);
            float amount = hasTyped ? typed : (d.x * sx + d.y * sy) / len2;
            if (snap && !hasTyped)
               amount = SnapTo(amount, sSnapMove);
            for (int k = 0; k < 3; k++)
               move[k] = axis[k] * amount;
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
            deg = SnapTo(deg, sSnapRot);
         angle = deg * kPi / 180.0f;
      }
      else
      {
         const float d0 = std::max(4.0f, std::hypot(sOp.start.x - c.x, sOp.start.y - c.y));
         const float d1 = std::hypot(mouse.x - c.x, mouse.y - c.y);
         factor = 1.0f + (d1 / d0 - 1.0f) * fine;
         if (hasTyped)
            factor = typed;
         else if (snap)
            factor = SnapTo(factor, sSnapScale);
      }

      for (const Item& was : sOp.before)
      {
         Item* it = scene->FindItem(was.id);
         if (it == nullptr)
            continue;
         float off[3] = { was.pos[0] - sOp.pivot[0], was.pos[1] - sOp.pivot[1], was.pos[2] - sOp.pivot[2] };
         if (sOp.mode == 'g')
         {
            for (int k = 0; k < 3; k++)
            {
               it->pos[k] = was.pos[k] + move[k];
               if (snap && sOp.axis < 0 && !hasTyped)
                  it->pos[k] = SnapTo(it->pos[k], sSnapMove);
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
               it->pos[k] = sOp.pivot[k] + dr[k][0] * off[0] + dr[k][1] * off[1] + dr[k][2] * off[2];
         }
         else
         {
            for (int k = 0; k < 3; k++)
            {
               const bool on = sOp.axis < 0 || sOp.axis == k;
               it->scale[k] = was.scale[k] * (on ? factor : 1.0f);
               it->pos[k] = sOp.pivot[k] + off[k] * (on ? factor : 1.0f);
            }
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
      PushUndoCheckpoint("Move in 3D View");
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
      float size = 0.0f;
      for (int k = 0; k < 3; k++)
      {
         sTarget[k] = (lo[k] + hi[k]) * 0.5f;
         size = std::max(size, hi[k] - lo[k]);
      }
      sDist = std::max(0.5f, size * 1.8f);
      sCameraView = false;
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

   void DuplicateSelected(Scene3DNode* scene, ImVec2 mouse)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Duplicate in 3D View");
      std::vector<int> made;
      for (int id : std::vector<int>(sSel))
      {
         Item* it = scene->FindItem(id);
         if (it == nullptr)
            continue;
         const Item copy = *it;
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

   void HandleKeys(Scene3DNode* scene, ImVec2 mouse, bool hovered)
   {
      const ImGuiIO& io = ImGui::GetIO();
      auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      if (sOp.mode != 0)
      {
         for (int a = 0; a < 3; a++)
            if (pressed((ImGuiKey)(ImGuiKey_X + a)))
            {
               sOp.local = sOp.axis == a && !sOp.local;
               sOp.axis = a;
            }
         for (int i = 0; i < 10; i++)
            if (pressed((ImGuiKey)(ImGuiKey_0 + i)) || pressed((ImGuiKey)(ImGuiKey_Keypad0 + i)))
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
      const bool plain = !io.KeyCtrl && !io.KeySuper && !io.KeyAlt;
      if (plain && !io.KeyShift)
      {
         if (pressed(ImGuiKey_G)) BeginOp('g', scene, mouse);
         else if (pressed(ImGuiKey_R)) BeginOp('r', scene, mouse);
         else if (pressed(ImGuiKey_S)) BeginOp('s', scene, mouse);
         else if (pressed(ImGuiKey_Q)) sTool = 0;
         else if (pressed(ImGuiKey_W)) sTool = 1;
         else if (pressed(ImGuiKey_E)) sTool = 2;
         else if (pressed(ImGuiKey_T)) sTool = 3;
         else if (pressed(ImGuiKey_F)) FrameItems(scene, sSel.empty());
         else if (pressed(ImGuiKey_Home)) FrameItems(scene, true);
         else if (pressed(ImGuiKey_Z)) sShading = (Shading)(((int)sShading + 1) % 3);
         else if (pressed(ImGuiKey_0) && scene->activeCamera != 0) sCameraView = !sCameraView;
         else if (pressed(ImGuiKey_1)) { sAz = kPi * 0.5f; sEl = 0.0f; sCameraView = false; }
         else if (pressed(ImGuiKey_3)) { sAz = 0.0f; sEl = 0.0f; sCameraView = false; }
         else if (pressed(ImGuiKey_7)) { sAz = kPi * 0.5f; sEl = kPi * 0.495f; sCameraView = false; }
         else if (pressed(ImGuiKey_A))
         {
            sSel.clear();
            for (const Item& it : scene->Items())
               sSel.push_back(it.id);
         }
         else if (pressed(ImGuiKey_H))
         {
            PushUndoCheckpoint("Hide in 3D View");
            for (int id : sSel)
               if (Item* it = scene->FindItem(id))
                  it->visible = false;
            scene->Commit();
         }
         else if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace))
            DeleteSelected(scene);
      }
      if (io.KeyAlt && pressed(ImGuiKey_H))
      {
         PushUndoCheckpoint("Show in 3D View");
         for (Item& it : scene->Items())
            it.visible = true;
         scene->Commit();
      }
      if (io.KeyShift && !io.KeyCtrl && !io.KeySuper && pressed(ImGuiKey_D))
         DuplicateSelected(scene, mouse);
   }

   void HandleNavigation(ImVec2 mouse, bool hovered)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const bool orbitDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
                             (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f));
      auto pan = [&](float dx, float dy)
      {
         const float u = UnitAt(sTarget);
         for (int k = 0; k < 3; k++)
            sTarget[k] += -sCam.right[k] * dx * u + sCam.up[k] * dy * u;
      };
      if (hovered && orbitDrag)
      {
         if (io.KeyShift)
            pan(io.MouseDelta.x, io.MouseDelta.y);
         else
         {
            sAz += io.MouseDelta.x * 0.008f;
            sEl = std::clamp(sEl + io.MouseDelta.y * 0.008f, -1.55f, 1.55f);
         }
         sCameraView = false;
      }
      if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
      {
#ifdef __APPLE__
         const bool zoom = io.KeyCtrl || io.KeySuper;
#else
         const bool zoom = !io.KeyShift;
#endif
         if (zoom)
            sDist = std::clamp(sDist * std::pow(0.9f, io.MouseWheel), 0.1f, 90.0f);
         else if (io.KeyShift)
            pan(io.MouseWheelH * 12.0f, io.MouseWheel * 12.0f);
         else
         {
            sAz += io.MouseWheelH * 0.05f;
            sEl = std::clamp(sEl + io.MouseWheel * 0.05f, -1.55f, 1.55f);
         }
         sCameraView = false;
      }
      if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right) && sOp.mode == 0)
      {
         // Fly: the eye stays put under the look, WASD/QE walk it.
         float eye[3];
         EyeFromOrbit(eye);
         if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
         {
            sAz += io.MouseDelta.x * 0.005f;
            sEl = std::clamp(sEl + io.MouseDelta.y * 0.005f, -1.55f, 1.55f);
            float next[3];
            EyeFromOrbit(next);
            for (int k = 0; k < 3; k++)
               sTarget[k] += eye[k] - next[k];
         }
         const float speed = sDist * io.DeltaTime * (io.KeyShift ? 2.5f : 1.0f);
         const float f = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
         const float r = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
         const float u = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
         for (int k = 0; k < 3; k++)
            sTarget[k] += (sCam.fwd[k] * f + sCam.right[k] * r + sCam.up[k] * u) * speed;
         if (f != 0.0f || r != 0.0f || u != 0.0f)
            sCameraView = false;
      }
      (void)mouse;
   }

   void DrawGrid(ImDrawList* dl)
   {
      const ImU32 line = IM_COL32(128, 128, 128, 46);
      for (int i = -10; i <= 10; i++)
      {
         const float a[3] = { (float)i, 0, -10 }, b[3] = { (float)i, 0, 10 };
         const float c[3] = { -10, 0, (float)i }, d[3] = { 10, 0, (float)i };
         ImVec2 p, q;
         if (Project(a, p) && Project(b, q))
            dl->AddLine(p, q, i == 0 ? AxisColor(2, 0.55f) : line);
         if (Project(c, p) && Project(d, q))
            dl->AddLine(p, q, i == 0 ? AxisColor(0, 0.55f) : line);
      }
   }

   void DrawBox(ImDrawList* dl, const float lo[3], const float hi[3], ImU32 col)
   {
      ImVec2 s[8];
      for (int i = 0; i < 8; i++)
      {
         const float p[3] = { (i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2] };
         if (!Project(p, s[i]))
            return;
      }
      for (int i = 0; i < 8; i++)
         for (int bit : { 1, 2, 4 })
            if ((i & bit) == 0)
               dl->AddLine(s[i], s[i | bit], col, 1.5f);
   }

   float SegDist(ImVec2 p, ImVec2 a, ImVec2 b)
   {
      const float vx = b.x - a.x, vy = b.y - a.y;
      const float t = std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / std::max(1.0f, vx * vx + vy * vy), 0.0f, 1.0f);
      return std::hypot(p.x - a.x - vx * t, p.y - a.y - vy * t);
   }

   // Draws the tool's handles at the selection and says which axis the mouse is on.
   int DrawGizmo(ImDrawList* dl, const float at[3], ImVec2 mouse)
   {
      ImVec2 c;
      if (sTool == 0 || !Project(at, c))
         return -1;
      const float len = UnitAt(at) * 72.0f;
      int hot = -1;
      float best = 9.0f;
      for (int a = 0; a < 3; a++)
      {
         if (sTool == 2)
         {
            int prevOk = 0;
            ImVec2 prev;
            for (int i = 0; i <= 48; i++)
            {
               const float t = (float)i / 48.0f * 2.0f * kPi;
               float p[3] = { at[0], at[1], at[2] };
               p[(a + 1) % 3] += std::cos(t) * len;
               p[(a + 2) % 3] += std::sin(t) * len;
               ImVec2 s;
               const int ok = Project(p, s) ? 1 : 0;
               if (ok && prevOk)
               {
                  const float dist = SegDist(mouse, prev, s);
                  if (dist < best) { best = dist; hot = a; }
                  dl->AddLine(prev, s, AxisColor(a, 0.95f), 2.0f);
               }
               prev = s;
               prevOk = ok;
            }
            continue;
         }
         float tip[3] = { at[0], at[1], at[2] };
         tip[a] += len;
         ImVec2 s;
         if (!Project(tip, s))
            continue;
         const float dist = SegDist(mouse, c, s);
         if (dist < best) { best = dist; hot = a; }
         dl->AddLine(c, s, AxisColor(a), 2.5f);
         if (sTool == 1)
            dl->AddCircleFilled(s, 5.0f, AxisColor(a));
         else
            dl->AddRectFilled(ImVec2(s.x - 4, s.y - 4), ImVec2(s.x + 4, s.y + 4), AxisColor(a));
      }
      return hot;
   }

   // Nearest item under the ray: geometry by its box, cameras and lights by their marker.
   int Pick(Scene3DNode* scene, ImVec2 mouse)
   {
      int best = 0;
      float bestDepth = 1e30f;
      for (const Item& it : scene->Items())
      {
         float p[3];
         ImVec2 s;
         if (scene->ItemPoint(it, p))
         {
            if (Project(p, s) && std::hypot(mouse.x - s.x, mouse.y - s.y) < 10.0f)
               return it.id;
            continue;
         }
         float lo[3], hi[3];
         if (!it.visible || !scene->ItemBounds(it.id, lo, hi))
            continue;
         float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
         bool front = true;
         for (int i = 0; i < 8; i++)
         {
            const float c[3] = { (i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2] };
            if (!Project(c, s))
            {
               front = false;
               break;
            }
            x0 = std::min(x0, s.x); y0 = std::min(y0, s.y);
            x1 = std::max(x1, s.x); y1 = std::max(y1, s.y);
         }
         if (!front || mouse.x < x0 || mouse.x > x1 || mouse.y < y0 || mouse.y > y1)
            continue;
         const float mid[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
         const float depth = (mid[0] - sCam.eye[0]) * sCam.fwd[0] + (mid[1] - sCam.eye[1]) * sCam.fwd[1] +
                             (mid[2] - sCam.eye[2]) * sCam.fwd[2];
         if (depth < bestDepth)
         {
            bestDepth = depth;
            best = it.id;
         }
      }
      return best;
   }

   void DrawViewport(Scene3DNode* scene, ImVec2 size)
   {
      ImVec2 mn = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##view3dcanvas", size,
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                                ImGuiButtonFlags_MouseButtonRight);
      const bool hovered = ImGui::IsItemHovered();
      ImVec2 mx(mn.x + size.x, mn.y + size.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->PushClipRect(mn, mx, true);
      DrawCheckerboardBackdrop(dl, mn, mx);

      // Through the camera the frame takes the render's shape.
      ImVec2 fmn = mn, fmx = mx;
      if (sCameraView && scene->GetOutputHeight() > 0)
      {
         const float aspect = (float)scene->GetOutputWidth() / (float)scene->GetOutputHeight();
         float w = size.x, h = w / aspect;
         if (h > size.y) { h = size.y; w = h * aspect; }
         fmn = ImVec2(mn.x + (size.x - w) * 0.5f, mn.y + (size.y - h) * 0.5f);
         fmx = ImVec2(fmn.x + w, fmn.y + h);
      }
      BuildCam(scene, fmn, fmx);
      if (sGrid && !sCameraView)
         DrawGrid(dl);

      const float dpi = ImGui::GetIO().DisplayFramebufferScale.y;
      const unsigned int tex = scene->RenderView((int)((fmx.x - fmn.x) * dpi), (int)((fmx.y - fmn.y) * dpi), sCam.view,
                                                 sCam.eye, kFov, sShading, sCameraView);
      if (tex != 0)
         dl->AddImage((ImTextureID)(intptr_t)tex, fmn, fmx, ImVec2(0, 1), ImVec2(1, 0));

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      float center[3] = { 0, 0, 0 };
      int selCount = 0;
      for (const Item& it : scene->Items())
      {
         float p[3];
         const bool sel = Selected(it.id);
         if (scene->ItemPoint(it, p))
         {
            ImVec2 s;
            if (Project(p, s) && !sCameraView)
            {
               const bool cam = scene->ItemKind(it) == Scene3DNode::Kind::Camera;
               const ImU32 col = sel ? ImGui::GetColorU32(ImGuiCol_PlotHistogram) : IM_COL32(230, 230, 230, 200);
               if (cam)
                  dl->AddRect(ImVec2(s.x - 7, s.y - 5), ImVec2(s.x + 7, s.y + 5), col, 2.0f, 0, 1.5f);
               else
                  dl->AddCircle(s, 6.0f, col, 16, 1.5f);
            }
         }
         else if (sel)
         {
            float lo[3], hi[3];
            if (scene->ItemBounds(it.id, lo, hi))
               DrawBox(dl, lo, hi, ImGui::GetColorU32(ImGuiCol_PlotHistogram));
         }
         if (sel)
         {
            for (int k = 0; k < 3; k++)
               center[k] += it.pos[k];
            selCount++;
         }
      }

      int hot = -1;
      if (selCount > 0 && !sCameraView)
      {
         for (int k = 0; k < 3; k++)
            center[k] /= (float)selCount;
         hot = DrawGizmo(dl, center, mouse);
      }
      gView3DClaimedKeys = hovered || sOp.mode != 0;
      HandleKeys(scene, mouse, hovered);
      HandleNavigation(mouse, hovered);

      if (hovered && sOp.mode == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
      {
         if (hot >= 0)
            BeginOp(sTool == 1 ? 'g' : sTool == 2 ? 'r' : 's', scene, mouse, hot, true);
         else
         {
            const int id = Pick(scene, mouse);
            if (!ImGui::GetIO().KeyShift)
               sSel.clear();
            if (id != 0 && !Selected(id))
               sSel.push_back(id);
         }
      }

      if (sOp.mode != 0)
      {
         char line[96];
         snprintf(line, sizeof(line), "%s %s%s", sOp.mode == 'g' ? T("Move") : sOp.mode == 'r' ? T("Rotate") : T("Scale"),
                  sOp.axis < 0 ? "" : sOp.axis == 0 ? "X " : sOp.axis == 1 ? "Y " : "Z ", sOp.typed.c_str());
         dl->AddText(ImVec2(mn.x + 10, mx.y - 24), IM_COL32(240, 240, 240, 240), line);
      }
      dl->PopClipRect();
   }

   void DrawHeader(Scene3DNode* scene)
   {
      auto chip = [](const char* label, bool on) { bool hit = ChipButton::Draw(label, on, 24.0f); ImGui::SameLine(); return hit; };
      if (chip(L("Editor##v3d"), !sCameraView)) sCameraView = false;
      if (chip(L("Camera##v3d"), sCameraView) && scene->activeCamera != 0) sCameraView = true;
      ImGui::Dummy(ImVec2(8, 1)); ImGui::SameLine();
      if (chip(L("Wire##v3d"), sShading == Shading::Wire)) sShading = Shading::Wire;
      if (chip(L("Solid##v3d"), sShading == Shading::Solid)) sShading = Shading::Solid;
      if (chip(L("Final##v3d"), sShading == Shading::Final)) sShading = Shading::Final;
      ImGui::Dummy(ImVec2(8, 1)); ImGui::SameLine();
      if (chip(L("Select##v3d"), sTool == 0)) sTool = 0;
      if (chip(L("Move##v3d"), sTool == 1)) sTool = 1;
      if (chip(L("Rotate##v3d"), sTool == 2)) sTool = 2;
      if (chip(L("Scale##v3d"), sTool == 3)) sTool = 3;
      ImGui::Dummy(ImVec2(8, 1)); ImGui::SameLine();
      if (chip(L("Snap##v3d"), sSnap)) sSnap = !sSnap;
      if (chip(L("Grid##v3d"), sGrid)) sGrid = !sGrid;
      if (scene->ClippedDraws() > 0)
         ImGui::TextDisabled(T("%d objects over the limit"), (int)scene->ClippedDraws());
      else
         ImGui::NewLine();
   }

   void DrawRenderBox(Scene3DNode* scene)
   {
      ImGui::TextDisabled("%s", T("Render"));
      FieldWell::PushStyle();
      float wh[2] = { (float)scene->GetOutputWidth(), (float)scene->GetOutputHeight() };
      ImGui::SetNextItemWidth(-1);
      FieldWell::InputInt("##v3ddur", &sDuration);
      ImGui::SetNextItemWidth(-1);
      FieldWell::InputInt("##v3dfps", &sFps);
      FieldWell::PopStyle();
      sDuration = std::clamp(sDuration, 1, 3600);
      sFps = std::clamp(sFps, 1, 240);
      ImGui::TextDisabled(T("%d s at %d fps, %d x %d"), sDuration, sFps, (int)wh[0], (int)wh[1]);
      ImGui::BeginDisabled(gOfflineRender.active || scene->activeCamera == 0);
      if (ActionButton::Draw(L("Render camera view"), ImVec2(-1, 0), ActionButton::Kind::Primary))
      {
         GraphNode* self = SceneGraphNode();
         OutputNode* out = nullptr;
         if (self != nullptr)
            for (GraphNode& gn : gNodes)
               if (auto* o = dynamic_cast<OutputNode*>(gn.node.get()))
                  if (o->Input().GetSource() == self->node.get())
                     out = o;
         if (out == nullptr && self != nullptr)
         {
            const int selfIndex = self->index;
            const ImVec2 at = ed::GetNodePosition(self->NodeId());
            GraphNode* spawned = SpawnNode("Output", "Output", at.x + 320.0f, at.y);
            std::string error;
            if (spawned != nullptr && ConnectNodes(selfIndex, 0, spawned->index, 0, error))
               out = dynamic_cast<OutputNode*>(spawned->node.get());
         }
         if (out != nullptr)
         {
            out->offlineDurationSeconds = sDuration;
            out->offlineFps = sFps;
            StartOfflineRenderSession(out);
         }
      }
      ImGui::EndDisabled();
      if (scene->activeCamera == 0)
         ImGui::TextDisabled("%s", T("Add a Camera node to render."));
   }

   void DrawSide(Scene3DNode* scene)
   {
      ImGui::TextDisabled("%s", T("Scene"));
      ImGui::BeginChild("##v3dlist", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.34f), false);
      for (Item& it : scene->Items())
      {
         ImGui::PushID(it.id);
         bool vis = it.visible;
         if (CheckBox::Box("##vis", &vis))
         {
            PushUndoCheckpoint("Show or hide in 3D View");
            it.visible = vis;
            scene->Commit();
         }
         ImGui::SameLine();
         const bool missing = scene->ItemKind(it) == Scene3DNode::Kind::Missing;
         if (missing)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
         if (ImGui::Selectable(ItemName(it).c_str(), Selected(it.id)))
         {
            if (!ImGui::GetIO().KeyShift)
               sSel.clear();
            if (!Selected(it.id))
               sSel.push_back(it.id);
         }
         if (missing)
            ImGui::PopStyleColor();
         ImGui::PopID();
      }
      ImGui::EndChild();

      ImGui::BeginChild("##v3dinspect", ImVec2(0, 0), false);
      Item* item = sSel.size() == 1 ? scene->FindItem(sSel[0]) : nullptr;
      if (item != nullptr)
      {
         auto field = [&](const char* label, float* v, float speed)
         {
            ImGui::TextDisabled("%s", label);
            ImGui::SetNextItemWidth(-1);
            std::string id = std::string("##") + label;
            const bool changed = ImGui::DragFloat3(id.c_str(), v, speed);
            if (ImGui::IsItemActivated())
               PushUndoCheckpoint("Edit in 3D View");
            if (changed)
               scene->Commit();
         };
         FieldWell::PushStyle();
         field(T("Position"), item->pos, 0.01f);
         field(T("Rotation"), item->rot, 0.5f);
         field(T("Scale"), item->scale, 0.01f);
         FieldWell::PopStyle();

         const Scene3DNode::Kind kind = scene->ItemKind(*item);
         if (kind == Scene3DNode::Kind::Camera &&
             ActionButton::Draw(L("Render through this camera"), ImVec2(-1, 0),
                                scene->activeCamera == item->id ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
         {
            PushUndoCheckpoint("Set 3D View camera");
            scene->activeCamera = item->id;
            scene->Commit();
         }
         if (kind == Scene3DNode::Kind::Geometry)
         {
            bool header = false;
            for (const Item& other : scene->Items())
            {
               if (scene->ItemKind(other) != Scene3DNode::Kind::Light)
                  continue;
               if (!header)
                  ImGui::TextDisabled("%s", T("Lit by"));
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
               ImGui::SameLine();
               ImGui::TextUnformatted(ItemName(other).c_str());
               ImGui::PopID();
            }
         }
         if (ActionButton::Draw(L("Go to node"), ImVec2(-1, 0)))
            if (GraphNode* gn = FindNodeByUid(item->uid))
            {
               gRequestSelectNodeIndex = gn->index;
               gRequestFitViewNodeIndex = gn->index;
            }
      }

      ImGui::Dummy(ImVec2(1, 6));
      ImGui::TextDisabled("%s", T("World"));
      GraphNode* world = scene->worldUid != 0 ? FindNodeByUid(scene->worldUid) : nullptr;
      if (world != nullptr)
      {
         ImGui::TextUnformatted(world->typeName.c_str());
         if (ActionButton::Draw(L("Remove world"), ImVec2(-1, 0)))
         {
            PushUndoCheckpoint("Remove 3D View world");
            scene->worldUid = 0;
            scene->Commit();
         }
      }
      bool colorOn = scene->worldColorOn;
      if (CheckBox::Box("##v3dworldcol", &colorOn))
      {
         PushUndoCheckpoint("3D View world");
         scene->worldColorOn = colorOn;
         scene->Commit();
      }
      ImGui::SameLine();
      if (colorOn)
      {
         if (ImGui::ColorEdit3("##v3dcolor", scene->worldColor, ImGuiColorEditFlags_NoInputs))
            scene->Commit();
      }
      else
         ImGui::TextDisabled("%s", T("Transparent"));

      ImGui::Dummy(ImVec2(1, 6));
      DrawRenderBox(scene);
      ImGui::EndChild();
   }

   void DrawPanel()
   {
      Scene3DNode* scene = FindScene();
      if (scene == nullptr)
      {
         gView3DClaimedKeys = false;
         EmptyState::DrawInWindow(T("Nothing in the 3D View yet"), T("Right-click any 3D node and choose Add to 3D View."));
         return;
      }
      std::vector<int> live;
      for (const Item& it : scene->Items())
         if (Selected(it.id))
            live.push_back(it.id);
      sSel.swap(live);

      DrawHeader(scene);
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float viewW = std::max(120.0f, avail.x - kSideW - ImGui::GetStyle().ItemSpacing.x);
      DrawViewport(scene, ImVec2(viewW, std::max(80.0f, avail.y)));
      ImGui::SameLine();
      ImGui::BeginChild("##v3dside", ImVec2(0, avail.y), false);
      DrawSide(scene);
      ImGui::EndChild();
   }
}

bool View3DCanAdd(const GraphNode& gn)
{
   if (dynamic_cast<Scene3DNode*>(gn.node.get()) != nullptr)
      return false;
   return Scene3DNode::KindOf(gn.node.get()) != Scene3DNode::Kind::Missing ||
          dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr ||
          dynamic_cast<EnvironmentNode*>(gn.node.get()) != nullptr;
}

bool View3DCanSetWorld(const GraphNode& gn)
{
   INode* n = gn.node.get();
   return dynamic_cast<Scene3DNode*>(n) == nullptr && dynamic_cast<IGeometrySource*>(n) == nullptr &&
          n->GetOutputWidth() > 0;
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
      const ImVec2 at = ed::GetNodePosition(near.NodeId());
      GraphNode* made = SpawnNode("3D View", "3D", at.x + 340.0f, at.y);
      return made != nullptr ? dynamic_cast<Scene3DNode*>(made->node.get()) : nullptr;
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
