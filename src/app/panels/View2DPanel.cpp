// The 2D View: a Scene 2D document laid out as layers over node outputs, the way
// a picture is built in an image editor. Everything here edits the document's
// layers; the nodes themselves are only read, so a layer keeps animating under
// whatever is stacked on top of it. Plan: internal/plans/2d-view/README.md.
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
#include "core/BlendModes.h"
#include "nodes/OutputNode.h"
#include "nodes/Scene2DNode.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace app
{
using I18n::L;
using I18n::T;

bool gView2DOpen = false;
float gView2DHeight = 460.0f;
int gView2DDock = 0;
bool gView2DClaimedKeys = false;
ImVec2 gView2DRectMin, gView2DRectMax;

namespace
{
   using Layer = Scene2DNode::Layer;
   using Kind = Scene2DNode::Kind;

   constexpr float kMinHeight = 300.0f;
   constexpr float kSideW = 300.0f;
   constexpr float kIconW = 30.0f;
   constexpr float kRuler = 18.0f;
   constexpr float kHandle = 7.0f;
   constexpr float kSnapPx = 6.0f;
   constexpr float kPi = 3.14159265f;
   constexpr const char* kUndo = "Edit in 2D View";

   // Overlay colours are an image editor's, not the app's: they are read against the picture.
   constexpr unsigned kPasteboard = 0x282828, kOutline = 0x2D8CFF, kSmart = 0xFF2DB4, kGuide = 0x00D2E6, kGrid = 0x808080;

   enum Tool { kMove = 0, kCrop, kHand, kZoom };
   int sTool = kMove;
   uint64_t sDocUid = 0;
   std::vector<int> sSel;          // layer ids, in click order
   int sTab = 0;                   // 0 Layer, 1 Document, 2 Output
   bool sSideOpen = true;
   bool sAutoSelect = true;
   bool sDirty = false;            // a widget changed the model this frame
   float sZoom = 1.0f;
   ImVec2 sPan(0.0f, 0.0f);        // the document centre's offset from the view centre, screen px
   bool sNeedFit = true;
   uint64_t sFitFor = 0;
   int sFitW = 0, sFitH = 0;
   ImVec2 sViewMn, sViewMx;        // the picture area (inside the rulers)
   ImVec2 sMouseDoc(0.0f, 0.0f);
   bool sMouseIn = false;
   int sRename = 0;
   char sRenameBuf[128] = {};

   // Snap lines found by the last move, drawn while the drag lasts.
   std::vector<float> sSnapX, sSnapY;

   struct Op
   {
      int kind = 0;               // 1 move, 2 scale, 3 rotate, 4 pan, 5 crop box, 6 guide
      ImVec2 start;
      std::vector<Layer> before;
      Layer l0;
      float hx = 0, hy = 0;       // the dragged handle, as a box fraction
      bool fromCentre = false;
      int guideAxis = 0;          // 0 vertical (x), 1 horizontal (y)
      int guideIndex = -1;
      float crop0[4] = { 0, 0, 0, 0 };
      int cropHandle = -1;        // 0..7 handle, 8 inside
      ImVec2 pan0;
   } sOp;

   bool sCropOn = false;
   float sCrop[4] = { 0, 0, 0, 0 };   // x0 y0 x1 y1, document px

   struct Pending
   {
      enum What { None, NewDoc, DuplicateDoc, DeleteDoc, Convert, RenderImage, RenderAnim, Group, Ungroup, Duplicate, Delete, AddFill };
      What what = None;
   } sPending;

   ImU32 Hex(unsigned rgb, float a = 1.0f)
   {
      return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (int)(std::clamp(a, 0.0f, 1.0f) * 255.0f));
   }

   GraphNode* DocGraphNode()
   {
      if (GraphNode* chosen = sDocUid != 0 ? FindNodeByUid(sDocUid) : nullptr)
         if (dynamic_cast<Scene2DNode*>(chosen->node.get()) != nullptr)
            return chosen;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<Scene2DNode*>(gn.node.get()) != nullptr)
            return &gn;
      return nullptr;
   }

   Scene2DNode* FindDoc()
   {
      GraphNode* gn = DocGraphNode();
      return gn != nullptr ? dynamic_cast<Scene2DNode*>(gn->node.get()) : nullptr;
   }

   // With two documents, the panel shows the one last selected on the canvas.
   void FollowCanvasSelection()
   {
      if (gEditor == nullptr)
         return;
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      static uint64_t sFollowed = 0;
      uint64_t selected = 0;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<Scene2DNode*>(gn.node.get()) != nullptr && ed::IsNodeSelected(gn.NodeId()))
         {
            selected = gn.uid;
            break;
         }
      if (selected != 0 && selected != sFollowed)
         sDocUid = selected;
      sFollowed = selected;
      ed::SetCurrentEditor(prev);
   }

   ImVec2 NodePos(const GraphNode& gn)
   {
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      const ImVec2 at = gn.hiddenFromCanvas ? ImVec2(gn.spawnX, gn.spawnY) : ed::GetNodePosition(gn.NodeId());
      ed::SetCurrentEditor(prev);
      return at;
   }

   Scene2DNode* MakeDoc(float x, float y, const std::string& text)
   {
      GraphNode* made = SpawnNode("Scene 2D", "Compositing", x, y);
      auto* doc = made != nullptr ? dynamic_cast<Scene2DNode*>(made->node.get()) : nullptr;
      if (doc == nullptr)
         return nullptr;
      if (!text.empty())
         doc->SetDocText(text);
      doc->Layers();
      doc->panelOnly = true;
      doc->Commit();
      made->hiddenFromCanvas = true;
      sDocUid = made->uid;
      sSel.clear();
      sNeedFit = true;
      return doc;
   }

   Scene2DNode* EnsureDoc(const GraphNode& near)
   {
      if (Scene2DNode* d = FindDoc())
         return d;
      const ImVec2 at = NodePos(near);
      return MakeDoc(at.x + 340.0f, at.y, "");
   }

   bool Selected(int id) { return std::find(sSel.begin(), sSel.end(), id) != sSel.end(); }

   Layer* Active(Scene2DNode* doc) { return sSel.empty() ? nullptr : doc->FindLayer(sSel.back()); }

   std::string LayerName(const Layer& l)
   {
      if (!l.name.empty())
         return l.name;
      if (l.kind == Kind::Group)
         return std::string(T("Group")) + " " + std::to_string(l.id);
      if (l.kind == Kind::Fill)
         return std::string(T("Fill")) + " " + std::to_string(l.id);
      GraphNode* gn = FindNodeByUid(l.uid);
      return gn != nullptr ? gn->typeName + " " + std::to_string(gn->index) : std::string(T("source missing"));
   }

   bool Movable(const Layer& l) { return (l.lock & (Scene2DNode::kLockPosition | Scene2DNode::kLockAll)) == 0; }

   // ---- view mapping ----------------------------------------------------

   ImVec2 DocOrigin(Scene2DNode* doc)
   {
      const ImVec2 c((sViewMn.x + sViewMx.x) * 0.5f + sPan.x, (sViewMn.y + sViewMx.y) * 0.5f + sPan.y);
      return ImVec2(c.x - doc->Doc().w * sZoom * 0.5f, c.y - doc->Doc().h * sZoom * 0.5f);
   }
   ImVec2 ToScreen(Scene2DNode* doc, float x, float y)
   {
      const ImVec2 o = DocOrigin(doc);
      return ImVec2(o.x + x * sZoom, o.y + y * sZoom);
   }
   ImVec2 ToDoc(Scene2DNode* doc, ImVec2 s)
   {
      const ImVec2 o = DocOrigin(doc);
      return ImVec2((s.x - o.x) / sZoom, (s.y - o.y) / sZoom);
   }

   void FitView(Scene2DNode* doc)
   {
      const float vw = std::max(40.0f, sViewMx.x - sViewMn.x - 48.0f), vh = std::max(40.0f, sViewMx.y - sViewMn.y - 48.0f);
      sZoom = std::clamp(std::min(vw / (float)doc->Doc().w, vh / (float)doc->Doc().h), 0.01f, 64.0f);
      sPan = ImVec2(0, 0);
   }

   void ZoomAt(Scene2DNode* doc, float ratio, ImVec2 at)
   {
      const ImVec2 before = ToDoc(doc, at);
      sZoom = std::clamp(sZoom * ratio, 0.01f, 64.0f);
      const ImVec2 after = ToScreen(doc, before.x, before.y);
      sPan.x += at.x - after.x;
      sPan.y += at.y - after.y;
   }

   // ---- geometry --------------------------------------------------------

   void Apply(const float m[6], float x, float y, float& ox, float& oy)
   {
      ox = m[0] * x + m[1] * y + m[2];
      oy = m[3] * x + m[4] * y + m[5];
   }

   void InverseOf(const float m[6], float out[6])
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

   bool InQuad(const float c[8], ImVec2 p)
   {
      int sign = 0;
      for (int i = 0; i < 4; i++)
      {
         const int j = (i + 1) % 4;
         const float cr = (c[j * 2] - c[i * 2]) * (p.y - c[i * 2 + 1]) - (c[j * 2 + 1] - c[i * 2 + 1]) * (p.x - c[i * 2]);
         const int s = cr > 0 ? 1 : cr < 0 ? -1 : 0;
         if (s != 0)
         {
            if (sign != 0 && s != sign)
               return false;
            sign = s;
         }
      }
      return true;
   }

   // The topmost shown, unlocked node or fill layer under a document point.
   int HitLayer(Scene2DNode* doc, ImVec2 p)
   {
      std::vector<Layer>& layers = doc->Layers();
      for (int i = (int)layers.size() - 1; i >= 0; i--)
      {
         const Layer& l = layers[i];
         if (l.kind != Kind::Node || !doc->Shown(l) || (l.lock & Scene2DNode::kLockAll) != 0)
            continue;
         float c[8];
         doc->Corners(l, c);
         if (InQuad(c, p))
            return l.id;
      }
      return 0;
   }

   // The selection's bounds in document px.
   bool SelBounds(Scene2DNode* doc, float lo[2], float hi[2])
   {
      bool any = false;
      lo[0] = lo[1] = 1e30f;
      hi[0] = hi[1] = -1e30f;
      for (int id : sSel)
      {
         float a[2], b[2];
         if (!doc->Bounds(id, a, b))
            continue;
         lo[0] = std::min(lo[0], a[0]);
         lo[1] = std::min(lo[1], a[1]);
         hi[0] = std::max(hi[0], b[0]);
         hi[1] = std::max(hi[1], b[1]);
         any = true;
      }
      return any;
   }

   // The layers that move with the selection: a group moves everything in it.
   std::vector<int> MovingIds(Scene2DNode* doc)
   {
      std::vector<int> out;
      for (const Layer& l : doc->Layers())
      {
         if (l.kind == Kind::Group || !Movable(l))
            continue;
         for (int id : sSel)
            if (doc->IsInside(l.id, id))
            {
               out.push_back(l.id);
               break;
            }
      }
      return out;
   }

   // ---- edits -----------------------------------------------------------

   void Touch() { sDirty = true; }

   void PruneSelection(Scene2DNode* doc)
   {
      sSel.erase(std::remove_if(sSel.begin(), sSel.end(), [&](int id) { return doc->FindLayer(id) == nullptr; }), sSel.end());
   }

   void SelectOnly(int id)
   {
      sSel.clear();
      if (id != 0)
         sSel.push_back(id);
   }

   void ToggleSel(int id)
   {
      auto it = std::find(sSel.begin(), sSel.end(), id);
      if (it != sSel.end())
         sSel.erase(it);
      else
         sSel.push_back(id);
   }

   void Nudge(Scene2DNode* doc, float dx, float dy)
   {
      std::vector<int> ids = MovingIds(doc);
      if (ids.empty())
         return;
      PushUndoCheckpoint("Nudge in 2D View");
      for (int id : ids)
         if (Layer* l = doc->FindLayer(id))
         {
            l->x += dx;
            l->y += dy;
         }
      Touch();
   }

   void Restack(Scene2DNode* doc, int how)   // 0 down, 1 up, 2 bottom, 3 top
   {
      Layer* a = Active(doc);
      if (a == nullptr)
         return;
      std::vector<int> sibs = doc->ChildrenOf(a->parent);
      const int at = (int)(std::find(sibs.begin(), sibs.end(), a->id) - sibs.begin());
      int target = -1;
      bool below = false;
      if (how == 0 && at > 0) { target = sibs[at - 1]; below = true; }
      else if (how == 1 && at + 1 < (int)sibs.size()) { target = sibs[at + 1]; below = false; }
      else if (how == 2 && at > 0) { target = sibs.front(); below = true; }
      else if (how == 3 && at + 1 < (int)sibs.size()) { target = sibs.back(); below = false; }
      if (target < 0)
         return;
      PushUndoCheckpoint("Move layer");
      doc->MoveLayer(a->id, target, below);
   }

   // Align to the document with one layer, to the selection with more (align: 0 left, 1 centre, 2 right, 3 top, 4 middle, 5 bottom).
   void Align(Scene2DNode* doc, int how)
   {
      if (sSel.empty())
         return;
      float lo[2] = { 0, 0 }, hi[2] = { (float)doc->Doc().w, (float)doc->Doc().h };
      if (sSel.size() > 1)
         SelBounds(doc, lo, hi);
      PushUndoCheckpoint("Align in 2D View");
      const int axis = how < 3 ? 0 : 1;
      const int side = how % 3;
      for (int id : sSel)
      {
         float a[2], b[2];
         if (!doc->Bounds(id, a, b))
            continue;
         const float want = side == 0 ? lo[axis] : side == 1 ? (lo[axis] + hi[axis]) * 0.5f : hi[axis];
         const float have = side == 0 ? a[axis] : side == 1 ? (a[axis] + b[axis]) * 0.5f : b[axis];
         const float d = want - have;
         for (Layer& l : doc->Layers())
            if (l.kind != Kind::Group && Movable(l) && doc->IsInside(l.id, id))
               (axis == 0 ? l.x : l.y) += d;
      }
      Touch();
   }

   void DeleteSelected(Scene2DNode* doc)
   {
      if (sSel.empty())
         return;
      PushUndoCheckpoint("Delete layer");
      for (int id : std::vector<int>(sSel))
         if (doc->FindLayer(id) != nullptr)
            doc->RemoveLayer(id);
      sSel.clear();
   }

   void AddFill(Scene2DNode* doc)
   {
      PushUndoCheckpoint("New fill layer");
      Layer* a = Active(doc);
      Layer& l = doc->AddLayer(Kind::Fill, 0, a != nullptr ? a->id : 0);
      SelectOnly(l.id);
   }

   // ---- widgets ---------------------------------------------------------

   bool IconChip(const char* id, const char* glyphStr, bool on, const char* tip, float w = kIconW)
   {
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const bool clicked = ActionButton::Draw(id, ImVec2(w, tok::tile));
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", tip);
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), tok::icon_md,
                  on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text), glyphStr);
      return clicked;
   }

   bool TextChip(const char* label, bool on, const char* tip)
   {
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const float w = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + 2.0f * tok::space_2;
      const bool clicked = ActionButton::Draw(label, ImVec2(w, tok::tile));
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (tip != nullptr && ImGui::IsItemHovered())
         HelpTip("%s", tip);
      return clicked;
   }

   float sFieldW = 160.0f;
   constexpr float kLabelW = 84.0f;

   void FieldLabel(const char* text)
   {
      ImGui::AlignTextToFramePadding();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::TextColored(ImVec4(t.x, t.y, t.z, 0.72f), "%s", text);
      FormParts::Inline(kLabelW);
   }

   void UndoOnActivate(const char* what = kUndo)
   {
      // The model only reaches the saved text at the end of the frame, so the
      // checkpoint taken now still holds the value from before the press.
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint(what);
   }

   bool DragRow(const char* label, float* v, float speed, const char* fmt, float lo = -1e6f, float hi = 1e6f)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      FieldWell::PushStyle();
      const bool changed = ImGui::DragFloat(FormParts::Id(label).c_str(), v, speed, lo, hi, fmt);
      FieldWell::PopStyle();
      UndoOnActivate();
      if (changed)
         Touch();
      return changed;
   }

   bool SliderRow(const char* label, float* v, float lo, float hi, const char* fmt)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      const bool changed = FieldWell::Slider(FormParts::Id(label).c_str(), v, lo, hi, fmt);
      UndoOnActivate();
      if (changed)
         Touch();
      return changed;
   }

   bool IntRow(const char* label, int* v)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      const bool changed = FieldWell::InputInt(FormParts::Id(label).c_str(), v);
      return changed;
   }

   bool ComboRow(const char* label, int* v, const std::vector<std::string>& names, int offset = 0)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      const int shown = std::clamp(*v - offset, 0, (int)names.size() - 1);
      bool changed = false;
      if (MenuParts::BeginCombo(FormParts::Id(label).c_str(), names[shown].c_str()))
      {
         for (int i = 0; i < (int)names.size(); i++)
            if (MenuParts::Choice(names[i].c_str(), i == shown) && i != shown)
            {
               PushUndoCheckpoint(kUndo);
               *v = i + offset;
               changed = true;
            }
         MenuParts::EndCombo();
      }
      if (changed)
         Touch();
      return changed;
   }

   bool BoolRow(const char* label, bool* v)
   {
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
      PushUndoCheckpoint(kUndo);
      *v = on;
      Touch();
      return true;
   }

   bool ColorRow(const char* label, float* rgb)
   {
      FieldLabel(label);
      const bool changed = FormParts::ColorDot(FormParts::Id(label).c_str(), rgb, 15.0f, ImGui::GetFrameHeight());
      static double sLast = -1.0;
      if (changed && ImGui::GetTime() - sLast > 1.0)
         PushUndoCheckpoint(kUndo);
      if (changed)
      {
         sLast = ImGui::GetTime();
         Touch();
      }
      return changed;
   }

   // A node picker over every node that can be a layer; 0 = none (when allowed).
   bool NodeRow(const char* label, uint64_t* uid, bool allowNone, Scene2DNode* doc)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      GraphNode* cur = *uid != 0 ? FindNodeByUid(*uid) : nullptr;
      const std::string preview = cur != nullptr ? cur->typeName + " " + std::to_string(cur->index)
                                  : *uid != 0 ? std::string(T("source missing")) : std::string(T("None"));
      bool changed = false;
      if (MenuParts::BeginCombo(FormParts::Id(label).c_str(), preview.c_str()))
      {
         if (allowNone && MenuParts::Choice(T("None"), *uid == 0) && *uid != 0)
         {
            PushUndoCheckpoint(kUndo);
            *uid = 0;
            changed = true;
         }
         for (GraphNode& gn : gNodes)
         {
            if (gn.node.get() == doc || !View2DCanAdd(gn))
               continue;
            const std::string name = gn.typeName + " " + std::to_string(gn.index) + "##n" + std::to_string(gn.uid);
            if (MenuParts::Choice(name.c_str(), gn.uid == *uid) && gn.uid != *uid)
            {
               PushUndoCheckpoint(kUndo);
               *uid = gn.uid;
               changed = true;
            }
         }
         MenuParts::EndCombo();
      }
      if (changed)
         Touch();
      return changed;
   }

   // 3x3 anchor picker (row major from top left).
   bool AnchorRow(const char* label, int* anchor)
   {
      FieldLabel(label);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      bool changed = false;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float cell = 14.0f;
      ImGui::InvisibleButton(FormParts::Id(label).c_str(), ImVec2(cell * 3, cell * 3));
      if (ImGui::IsItemClicked())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const int cx = std::clamp((int)((m.x - p.x) / cell), 0, 2), cy = std::clamp((int)((m.y - p.y) / cell), 0, 2);
         *anchor = cy * 3 + cx;
         changed = true;
      }
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled), on = ImGui::GetColorU32(ImGuiCol_Text);
      for (int i = 0; i < 9; i++)
      {
         const ImVec2 c(p.x + (i % 3 + 0.5f) * cell, p.y + (i / 3 + 0.5f) * cell);
         if (i == *anchor)
            dl->AddRectFilled(ImVec2(c.x - 4, c.y - 4), ImVec2(c.x + 4, c.y + 4), on, 1.5f);
         else
            dl->AddRect(ImVec2(c.x - 3, c.y - 3), ImVec2(c.x + 3, c.y + 3), dim, 1.0f);
      }
      return changed;
   }

   // ---- render ----------------------------------------------------------

   OutputNode* FindOutput()
   {
      GraphNode* self = DocGraphNode();
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
      GraphNode* self = DocGraphNode();
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

   std::string sLastRender, sStillPath;

   bool EndsWith(const std::string& text, const char* tail)
   {
      const size_t n = std::strlen(tail);
      return text.size() >= n && text.compare(text.size() - n, n, tail) == 0;
   }

   void RenderImage()
   {
      if (gOfflineRender.active)
         return;
      const std::string path = Platform::SaveRenderDialog("infinite_2d.png", false);
      if (path.empty())
         return;
      if (OutputNode* out = EnsureOutputImpl())
      {
         out->exportImagePath = path;
         out->imageFormat = EndsWith(path, ".jpg") || EndsWith(path, ".jpeg") ? 1 : 0;
         sStillPath = path;
      }
   }

   void RenderAnimation(Scene2DNode* doc)
   {
      if (gOfflineRender.active)
         return;
      const std::string path = Platform::SaveRenderDialog("infinite_2d.mp4", true);
      if (path.empty())
         return;
      if (OutputNode* out = EnsureOutputImpl())
      {
         out->recordVideoPath = path;
         out->videoFormat = EndsWith(path, ".mov") ? 1 : 0;
         out->offlineDurationSeconds = std::max(1, (int)std::lround(doc->Doc().seconds));
         out->offlineFps = std::max(1, (int)std::lround(doc->Doc().fps));
         sLastRender = path;
         StartOfflineRenderSession(out);
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

   // ---- keys ------------------------------------------------------------

   void HandleKeys(Scene2DNode* doc, bool hovered)
   {
      const ImGuiIO& io = ImGui::GetIO();
      if (!hovered || ImGui::GetIO().WantTextInput)
         return;
      gView2DClaimedKeys = true;
      const bool cmd = io.KeyCtrl || io.KeySuper;
      const bool shift = io.KeyShift, alt = io.KeyAlt;
      auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      auto rep = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
      if (!cmd && !alt)
      {
         if (key(ImGuiKey_V)) sTool = kMove;
         if (key(ImGuiKey_C)) sTool = kCrop;
         if (key(ImGuiKey_H)) sTool = kHand;
         if (key(ImGuiKey_Z)) sTool = kZoom;
      }
      if (key(ImGuiKey_F12))
         sPending.what = cmd ? Pending::RenderAnim : Pending::RenderImage;
      if (cmd && key(ImGuiKey_0)) FitView(doc);
      if (cmd && key(ImGuiKey_1)) { sZoom = 1.0f; sPan = ImVec2(0, 0); }
      const ImVec2 mid((sViewMn.x + sViewMx.x) * 0.5f, (sViewMn.y + sViewMx.y) * 0.5f);
      if (cmd && (rep(ImGuiKey_Equal) || rep(ImGuiKey_KeypadAdd))) ZoomAt(doc, 1.25f, mid);
      if (cmd && (rep(ImGuiKey_Minus) || rep(ImGuiKey_KeypadSubtract))) ZoomAt(doc, 0.8f, mid);
      Scene2DNode::Document& d = doc->Doc();
      if (cmd && !shift && key(ImGuiKey_Apostrophe)) { d.grid = !d.grid; Touch(); }
      if (cmd && !shift && key(ImGuiKey_R)) { d.rulers = !d.rulers; Touch(); }
      if (cmd && !shift && key(ImGuiKey_Semicolon)) { d.guidesOn = !d.guidesOn; Touch(); }
      if (cmd && shift && key(ImGuiKey_Semicolon)) { d.snap = !d.snap; Touch(); }
      if (sTool == kCrop && sCropOn)
      {
         if (key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter))
         {
            PushUndoCheckpoint("Crop document");
            doc->CropTo(sCrop[0], sCrop[1], sCrop[2], sCrop[3]);
            sCropOn = false;
            sTool = kMove;
            sNeedFit = true;
         }
         if (key(ImGuiKey_Escape))
         {
            sCropOn = false;
            sTool = kMove;
         }
         return;
      }
      if (key(ImGuiKey_Escape) && sOp.kind == 0)
         sSel.clear();
      if (key(ImGuiKey_Delete) || key(ImGuiKey_Backspace))
         sPending.what = Pending::Delete;
      if (cmd && !shift && key(ImGuiKey_J)) sPending.what = Pending::Duplicate;
      if (cmd && !shift && key(ImGuiKey_G)) sPending.what = Pending::Group;
      if (cmd && shift && key(ImGuiKey_G)) sPending.what = Pending::Ungroup;
      if (cmd && alt && key(ImGuiKey_G))
         if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Clipping mask");
            a->clip = !a->clip;
            Touch();
         }
      if (cmd && key(ImGuiKey_LeftBracket)) Restack(doc, shift ? 2 : 0);
      if (cmd && key(ImGuiKey_RightBracket)) Restack(doc, shift ? 3 : 1);
      if (cmd && key(ImGuiKey_A))
      {
         sSel.clear();
         for (const Layer& l : doc->Layers())
            if (l.parent == 0)
               sSel.push_back(l.id);
      }
      const float step = shift ? 10.0f : 1.0f;
      if (!cmd && sTool == kMove)
      {
         if (rep(ImGuiKey_LeftArrow)) Nudge(doc, -step, 0);
         if (rep(ImGuiKey_RightArrow)) Nudge(doc, step, 0);
         if (rep(ImGuiKey_UpArrow)) Nudge(doc, 0, -step);
         if (rep(ImGuiKey_DownArrow)) Nudge(doc, 0, step);
      }
   }

   // ---- snapping --------------------------------------------------------

   // Offsets that bring one of `edges` onto a target (document edges and centre,
   // guides, other layers' edges and centres), or 0. Lines found go to sSnapX/Y.
   float SnapAxis(Scene2DNode* doc, int axis, const float edges[3], const std::vector<int>& moving)
   {
      const Scene2DNode::Document& d = doc->Doc();
      std::vector<float> targets = { 0.0f, (axis == 0 ? d.w : d.h) * 0.5f, (float)(axis == 0 ? d.w : d.h) };
      if (d.guidesOn)
         for (float g : axis == 0 ? d.guidesX : d.guidesY)
            targets.push_back(g);
      for (const Layer& l : doc->Layers())
      {
         if (l.kind != Kind::Node || !doc->Shown(l) || std::find(moving.begin(), moving.end(), l.id) != moving.end())
            continue;
         float lo[2], hi[2];
         if (!doc->Bounds(l.id, lo, hi))
            continue;
         targets.push_back(lo[axis]);
         targets.push_back((lo[axis] + hi[axis]) * 0.5f);
         targets.push_back(hi[axis]);
      }
      const float reach = kSnapPx / sZoom;
      float best = reach + 1.0f, offset = 0.0f;
      for (float t : targets)
         for (int e = 0; e < 3; e++)
            if (std::fabs(t - edges[e]) < std::fabs(best))
            {
               best = t - edges[e];
               offset = best;
            }
      if (std::fabs(best) > reach)
         return 0.0f;
      std::vector<float>& lines = axis == 0 ? sSnapX : sSnapY;
      for (float t : targets)
         for (int e = 0; e < 3; e++)
            if (std::fabs(t - (edges[e] + offset)) < 0.01f)
               lines.push_back(t);
      return offset;
   }

   // ---- canvas ----------------------------------------------------------

   // The 8 handles of one layer's box, as box fractions.
   const float kHandleAt[8][2] = { { 0, 0 }, { 0.5f, 0 }, { 1, 0 }, { 1, 0.5f }, { 1, 1 }, { 0.5f, 1 }, { 0, 1 }, { 0, 0.5f } };

   void HandlePoint(Scene2DNode* doc, const Layer& l, float fx, float fy, ImVec2& out)
   {
      float bw = 1, bh = 1, m[6], x, y;
      doc->LayerBox(l, bw, bh);
      doc->LayerMatrix(l, m);
      Apply(m, fx * bw, fy * bh, x, y);
      out = ToScreen(doc, x, y);
   }

   // Which handle (0..7) or the rotate zone (8) of the active layer the pointer is over; -1 none.
   int HoverHandle(Scene2DNode* doc, const Layer& l, ImVec2 mouse)
   {
      for (int i = 0; i < 8; i++)
      {
         ImVec2 p;
         HandlePoint(doc, l, kHandleAt[i][0], kHandleAt[i][1], p);
         if (std::fabs(mouse.x - p.x) <= kHandle && std::fabs(mouse.y - p.y) <= kHandle)
            return i;
      }
      for (int i = 0; i < 8; i += 2)
      {
         ImVec2 p;
         HandlePoint(doc, l, kHandleAt[i][0], kHandleAt[i][1], p);
         const float dx = mouse.x - p.x, dy = mouse.y - p.y;
         if (dx * dx + dy * dy <= 22.0f * 22.0f)
         {
            float c[8];
            doc->Corners(l, c);
            if (!InQuad(c, ToDoc(doc, mouse)))
               return 8;
         }
      }
      return -1;
   }

   void BeginOp(Scene2DNode* doc, int kind, ImVec2 mouse, const char* undo)
   {
      sOp = Op();
      sOp.kind = kind;
      sOp.start = mouse;
      sOp.pan0 = sPan;
      if (undo != nullptr)
         PushUndoCheckpoint(undo);
      for (int id : MovingIds(doc))
         if (const Layer* l = doc->FindLayer(id))
            sOp.before.push_back(*l);
      if (Layer* a = Active(doc))
         sOp.l0 = *a;
   }

   void ScaleActive(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      if (l == nullptr || l->kind != Kind::Node)
         return;
      const Layer& l0 = sOp.l0;
      float bw = 1, bh = 1, m0[6], inv[6];
      doc->LayerBox(l0, bw, bh);
      doc->LayerMatrix(l0, m0);
      InverseOf(m0, inv);
      const ImVec2 md = ToDoc(doc, mouse);
      float bx, by;
      Apply(inv, md.x, md.y, bx, by);
      const float ox = sOp.fromCentre ? 0.5f : 1.0f - sOp.hx, oy = sOp.fromCentre ? 0.5f : 1.0f - sOp.hy;
      float kx = 1.0f, ky = 1.0f;
      if (std::fabs(sOp.hx - ox) > 1e-3f)
         kx = (bx - ox * bw) / ((sOp.hx - ox) * bw);
      if (std::fabs(sOp.hy - oy) > 1e-3f)
         ky = (by - oy * bh) / ((sOp.hy - oy) * bh);
      const bool corner = std::fabs(sOp.hx - 0.5f) > 0.1f && std::fabs(sOp.hy - 0.5f) > 0.1f;
      // Corners keep the proportions unless Shift is held, like current image editors.
      if (corner && !ImGui::GetIO().KeyShift)
      {
         const float k = std::fabs(kx) > std::fabs(ky) ? kx : ky;
         kx = ky = k;
      }
      if (!corner && std::fabs(sOp.hx - 0.5f) < 0.1f) kx = 1.0f;
      if (!corner && std::fabs(sOp.hy - 0.5f) < 0.1f) ky = 1.0f;
      kx = std::fabs(kx) < 1e-3f ? 1e-3f : kx;
      ky = std::fabs(ky) < 1e-3f ? 1e-3f : ky;
      // The fixed point stays where it was on the document.
      float fx0, fy0;
      Apply(m0, ox * bw, oy * bh, fx0, fy0);
      *l = l0;
      l->sx = l0.sx * kx;
      l->sy = l0.sy * ky;
      float m1[6], fx1, fy1;
      doc->LayerMatrix(*l, m1);
      Apply(m1, ox * bw, oy * bh, fx1, fy1);
      l->x += fx0 - fx1;
      l->y += fy0 - fy1;
      Touch();
   }

   void RotateActive(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      if (l == nullptr || l->kind != Kind::Node)
         return;
      const ImVec2 c = ToScreen(doc, sOp.l0.x, sOp.l0.y);
      const float a0 = std::atan2(sOp.start.y - c.y, sOp.start.x - c.x), a1 = std::atan2(mouse.y - c.y, mouse.x - c.x);
      float angle = sOp.l0.angle + (a1 - a0) * 180.0f / kPi;
      if (ImGui::GetIO().KeyShift)
         angle = std::round(angle / 15.0f) * 15.0f;
      angle = std::fmod(angle + 540.0f, 360.0f) - 180.0f;
      l->angle = angle;
      Touch();
   }

   void MoveSelection(Scene2DNode* doc, ImVec2 mouse)
   {
      float dx = (mouse.x - sOp.start.x) / sZoom, dy = (mouse.y - sOp.start.y) / sZoom;
      if (ImGui::GetIO().KeyShift)
         (std::fabs(dx) > std::fabs(dy) ? dy : dx) = 0.0f;
      for (const Layer& b : sOp.before)
         if (Layer* l = doc->FindLayer(b.id))
         {
            l->x = b.x + dx;
            l->y = b.y + dy;
         }
      sSnapX.clear();
      sSnapY.clear();
      const bool snap = doc->Doc().snap != (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper);
      float lo[2], hi[2];
      if (snap && SelBounds(doc, lo, hi))
      {
         std::vector<int> moving;
         for (const Layer& b : sOp.before)
            moving.push_back(b.id);
         const float ex[3] = { lo[0], (lo[0] + hi[0]) * 0.5f, hi[0] }, ey[3] = { lo[1], (lo[1] + hi[1]) * 0.5f, hi[1] };
         const float sx = SnapAxis(doc, 0, ex, moving), sy = SnapAxis(doc, 1, ey, moving);
         for (const Layer& b : sOp.before)
            if (Layer* l = doc->FindLayer(b.id))
            {
               l->x += sx;
               l->y += sy;
            }
      }
      Touch();
   }

   void DrawChecker(ImDrawList* dl, ImVec2 a, ImVec2 b)
   {
      const float s = 8.0f;
      dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
      for (float y = a.y; y < b.y; y += s)
         for (float x = a.x + (((int)((y - a.y) / s)) % 2 == 0 ? s : 0.0f); x < b.x; x += 2.0f * s)
            dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(b.x, x + s), std::min(b.y, y + s)), IM_COL32(204, 204, 204, 255));
   }

   float RulerStep()
   {
      const float steps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
      for (float s : steps)
         if (s * sZoom >= 60.0f)
            return s;
      return 10000.0f;
   }

   void DrawRulers(ImDrawList* dl, Scene2DNode* doc, ImVec2 mn, ImVec2 mx)
   {
      const ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg), text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + kRuler), bg);
      dl->AddRectFilled(mn, ImVec2(mn.x + kRuler, mx.y), bg);
      const float step = RulerStep();
      const ImVec2 o = DocOrigin(doc);
      const float x0 = std::floor(((mn.x + kRuler) - o.x) / sZoom / step) * step;
      for (float v = x0; o.x + v * sZoom < mx.x; v += step * 0.1f)
      {
         const float sx = o.x + v * sZoom;
         const bool major = std::fabs(std::fmod(std::fabs(v) + 0.01f * step, step)) < 0.02f * step;
         if (sx < mn.x + kRuler)
            continue;
         dl->AddLine(ImVec2(sx, mn.y + kRuler - (major ? kRuler * 0.7f : 4.0f)), ImVec2(sx, mn.y + kRuler), text);
         if (major)
            dl->AddText(ImVec2(sx + 2.0f, mn.y), text, std::to_string((int)std::lround(v)).c_str());
      }
      const float y0 = std::floor(((mn.y + kRuler) - o.y) / sZoom / step) * step;
      for (float v = y0; o.y + v * sZoom < mx.y; v += step * 0.1f)
      {
         const float sy = o.y + v * sZoom;
         const bool major = std::fabs(std::fmod(std::fabs(v) + 0.01f * step, step)) < 0.02f * step;
         if (sy < mn.y + kRuler)
            continue;
         dl->AddLine(ImVec2(mn.x + kRuler - (major ? kRuler * 0.7f : 4.0f), sy), ImVec2(mn.x + kRuler, sy), text);
         if (major)
         {
            const std::string s = std::to_string((int)std::lround(v));
            for (size_t i = 0; i < s.size() && i < 5; i++)
               dl->AddText(ImVec2(mn.x + 2.0f, sy + 1.0f + i * 9.0f), text, s.substr(i, 1).c_str());
         }
      }
      // Where the pointer is.
      if (sMouseIn)
      {
         const ImVec2 p = ToScreen(doc, sMouseDoc.x, sMouseDoc.y);
         dl->AddLine(ImVec2(p.x, mn.y), ImVec2(p.x, mn.y + kRuler), Hex(kOutline));
         dl->AddLine(ImVec2(mn.x, p.y), ImVec2(mn.x + kRuler, p.y), Hex(kOutline));
      }
   }

   int HoverGuide(Scene2DNode* doc, ImVec2 mouse, int& axis)
   {
      const Scene2DNode::Document& d = doc->Doc();
      if (!d.guidesOn)
         return -1;
      for (int i = 0; i < (int)d.guidesX.size(); i++)
         if (std::fabs(ToScreen(doc, d.guidesX[i], 0).x - mouse.x) <= 3.0f)
         {
            axis = 0;
            return i;
         }
      for (int i = 0; i < (int)d.guidesY.size(); i++)
         if (std::fabs(ToScreen(doc, 0, d.guidesY[i]).y - mouse.y) <= 3.0f)
         {
            axis = 1;
            return i;
         }
      return -1;
   }

   void DrawCropOverlay(ImDrawList* dl, Scene2DNode* doc)
   {
      const ImVec2 a = ToScreen(doc, std::min(sCrop[0], sCrop[2]), std::min(sCrop[1], sCrop[3]));
      const ImVec2 b = ToScreen(doc, std::max(sCrop[0], sCrop[2]), std::max(sCrop[1], sCrop[3]));
      const ImU32 dim = IM_COL32(0, 0, 0, 140);
      dl->AddRectFilled(sViewMn, ImVec2(sViewMx.x, a.y), dim);
      dl->AddRectFilled(ImVec2(sViewMn.x, b.y), sViewMx, dim);
      dl->AddRectFilled(ImVec2(sViewMn.x, a.y), ImVec2(a.x, b.y), dim);
      dl->AddRectFilled(ImVec2(b.x, a.y), ImVec2(sViewMx.x, b.y), dim);
      dl->AddRect(a, b, IM_COL32(255, 255, 255, 230));
      for (int i = 1; i < 3; i++)
      {
         const float x = a.x + (b.x - a.x) * i / 3.0f, y = a.y + (b.y - a.y) * i / 3.0f;
         dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), IM_COL32(255, 255, 255, 70));
         dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), IM_COL32(255, 255, 255, 70));
      }
      for (int i = 0; i < 8; i++)
      {
         const ImVec2 p(a.x + (b.x - a.x) * kHandleAt[i][0], a.y + (b.y - a.y) * kHandleAt[i][1]);
         dl->AddRectFilled(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), IM_COL32(255, 255, 255, 255));
      }
   }

   int HoverCropHandle(Scene2DNode* doc, ImVec2 mouse)
   {
      const ImVec2 a = ToScreen(doc, std::min(sCrop[0], sCrop[2]), std::min(sCrop[1], sCrop[3]));
      const ImVec2 b = ToScreen(doc, std::max(sCrop[0], sCrop[2]), std::max(sCrop[1], sCrop[3]));
      for (int i = 0; i < 8; i++)
      {
         const ImVec2 p(a.x + (b.x - a.x) * kHandleAt[i][0], a.y + (b.y - a.y) * kHandleAt[i][1]);
         if (std::fabs(mouse.x - p.x) <= kHandle && std::fabs(mouse.y - p.y) <= kHandle)
            return i;
      }
      return mouse.x > a.x && mouse.x < b.x && mouse.y > a.y && mouse.y < b.y ? 8 : -1;
   }

   void DragCrop(Scene2DNode* doc, ImVec2 mouse)
   {
      const float dx = (mouse.x - sOp.start.x) / sZoom, dy = (mouse.y - sOp.start.y) / sZoom;
      float c[4];
      std::copy(sOp.crop0, sOp.crop0 + 4, c);
      // Normalise so 0,1 is the low corner.
      if (c[0] > c[2]) std::swap(c[0], c[2]);
      if (c[1] > c[3]) std::swap(c[1], c[3]);
      const int h = sOp.cropHandle;
      if (h == 8)
      {
         c[0] += dx; c[2] += dx; c[1] += dy; c[3] += dy;
      }
      else
      {
         const float fx = kHandleAt[h][0], fy = kHandleAt[h][1];
         if (fx == 0) c[0] += dx;
         if (fx == 1) c[2] += dx;
         if (fy == 0) c[1] += dy;
         if (fy == 1) c[3] += dy;
      }
      for (float& v : c)
         v = std::round(v);
      (void)doc;
      std::copy(c, c + 4, sCrop);
   }

   void DrawCanvas(Scene2DNode* doc, ImVec2 size)
   {
      ImGuiIO& io = ImGui::GetIO();
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##view2dcanvas", size,
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
      const bool hovered = ImGui::IsItemHovered();
      const ImVec2 mx(mn.x + size.x, mn.y + size.y);
      Scene2DNode::Document& d = doc->Doc();
      const float r = d.rulers ? kRuler : 0.0f;
      sViewMn = ImVec2(mn.x + r, mn.y + r);
      sViewMx = mx;
      const uint64_t fitKey = sDocUid;
      if (sFitFor != fitKey || sFitW != d.w || sFitH != d.h)
      {
         sFitFor = fitKey;
         sFitW = d.w;
         sFitH = d.h;
         sNeedFit = true;
      }
      if (sNeedFit && size.x > 60.0f && size.y > 60.0f)
      {
         FitView(doc);
         sNeedFit = false;
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->PushClipRect(mn, mx, true);
      dl->AddRectFilled(mn, mx, Hex(kPasteboard));
      const ImVec2 a = ToScreen(doc, 0, 0), b = ToScreen(doc, (float)d.w, (float)d.h);
      // The picture: the Scene 2D's own output, so what is seen here is what renders.
      dl->PushClipRect(sViewMn, sViewMx, true);
      if (d.background == 3)
         DrawChecker(dl, a, b);
      if (const unsigned tex = doc->GetOutputTexture())
         dl->AddImage((ImTextureID)(intptr_t)tex, a, b, ImVec2(0, 1), ImVec2(1, 0));
      dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), IM_COL32(0, 0, 0, 120));

      if (d.grid && d.gridSize > 0.0f)
      {
         const float sub = d.gridSize / (float)std::max(1, d.gridSubdiv);
         if (sub * sZoom >= 4.0f)
         {
            int n = 0;
            for (float x = 0; x <= d.w + 0.01f; x += sub, n++)
               dl->AddLine(ToScreen(doc, x, 0), ToScreen(doc, x, (float)d.h), Hex(kGrid, n % d.gridSubdiv == 0 ? 0.55f : 0.2f));
            n = 0;
            for (float y = 0; y <= d.h + 0.01f; y += sub, n++)
               dl->AddLine(ToScreen(doc, 0, y), ToScreen(doc, (float)d.w, y), Hex(kGrid, n % d.gridSubdiv == 0 ? 0.55f : 0.2f));
         }
      }
      if (d.guidesOn)
      {
         for (float g : d.guidesX)
            dl->AddLine(ImVec2(ToScreen(doc, g, 0).x, sViewMn.y), ImVec2(ToScreen(doc, g, 0).x, sViewMx.y), Hex(kGuide));
         for (float g : d.guidesY)
            dl->AddLine(ImVec2(sViewMn.x, ToScreen(doc, 0, g).y), ImVec2(sViewMx.x, ToScreen(doc, 0, g).y), Hex(kGuide));
      }

      const ImVec2 mouse = io.MousePos;
      sMouseIn = hovered;
      sMouseDoc = ToDoc(doc, mouse);
      Layer* active = Active(doc);
      const bool canTransform = sTool == kMove && active != nullptr && active->kind == Kind::Node && sSel.size() == 1 &&
                                Movable(*active) && doc->Shown(*active);

      // Outlines: each selected layer, then the active one's handles.
      if (sTool == kMove)
         for (int id : sSel)
            if (Layer* l = doc->FindLayer(id))
            {
               if (l->kind == Kind::Group)
               {
                  float lo[2], hi[2];
                  if (doc->Bounds(id, lo, hi))
                     dl->AddRect(ToScreen(doc, lo[0], lo[1]), ToScreen(doc, hi[0], hi[1]), Hex(kOutline), 0.0f, 0, 1.0f);
                  continue;
               }
               if (l->kind != Kind::Node)
                  continue;
               float c[8];
               doc->Corners(*l, c);
               ImVec2 q[4];
               for (int i = 0; i < 4; i++)
                  q[i] = ToScreen(doc, c[i * 2], c[i * 2 + 1]);
               dl->AddPolyline(q, 4, Hex(kOutline), ImDrawFlags_Closed, 1.0f);
            }
      if (canTransform)
      {
         for (int i = 0; i < 8; i++)
         {
            ImVec2 p;
            HandlePoint(doc, *active, kHandleAt[i][0], kHandleAt[i][1], p);
            dl->AddRectFilled(ImVec2(p.x - 3.5f, p.y - 3.5f), ImVec2(p.x + 3.5f, p.y + 3.5f), IM_COL32(255, 255, 255, 255));
            dl->AddRect(ImVec2(p.x - 3.5f, p.y - 3.5f), ImVec2(p.x + 3.5f, p.y + 3.5f), Hex(kOutline));
         }
         const ImVec2 an = ToScreen(doc, active->x, active->y);
         dl->AddCircle(an, 4.0f, Hex(kOutline), 12, 1.5f);
         dl->AddLine(ImVec2(an.x - 7, an.y), ImVec2(an.x + 7, an.y), Hex(kOutline));
         dl->AddLine(ImVec2(an.x, an.y - 7), ImVec2(an.x, an.y + 7), Hex(kOutline));
      }
      if (sOp.kind == 1)
      {
         for (float x : sSnapX)
            dl->AddLine(ToScreen(doc, x, -1e5f), ToScreen(doc, x, 1e5f), Hex(kSmart));
         for (float y : sSnapY)
            dl->AddLine(ToScreen(doc, -1e5f, y), ToScreen(doc, 1e5f, y), Hex(kSmart));
      }
      if (sTool == kCrop)
      {
         if (!sCropOn)
         {
            sCrop[0] = 0;
            sCrop[1] = 0;
            sCrop[2] = (float)d.w;
            sCrop[3] = (float)d.h;
            sCropOn = true;
         }
         DrawCropOverlay(dl, doc);
      }
      dl->PopClipRect();
      if (d.rulers)
         DrawRulers(dl, doc, mn, mx);

      // ---- input ----
      HandleKeys(doc, hovered);
      const bool space = hovered && ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput;
      const bool panTool = sTool == kHand || space;
      if (hovered && io.MouseWheel != 0.0f)
      {
         if (io.KeyCtrl || io.KeySuper || io.KeyAlt)
            ZoomAt(doc, std::pow(1.15f, io.MouseWheel), mouse);
         else
            sPan.y += io.MouseWheel * 40.0f;
      }
      if (hovered && io.MouseWheelH != 0.0f)
         sPan.x += io.MouseWheelH * 40.0f;

      // Cursor hints.
      int guideAxis = 0;
      const int hoverGuide = sTool == kMove && hovered ? HoverGuide(doc, mouse, guideAxis) : -1;
      const int hoverHandle = canTransform && hovered ? HoverHandle(doc, *active, mouse) : -1;
      if (hovered && sOp.kind == 0)
      {
         if (panTool)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         else if (hoverHandle >= 0 && hoverHandle < 8)
            ImGui::SetMouseCursor((hoverHandle % 4) == 1 ? ImGuiMouseCursor_ResizeNS : (hoverHandle % 4) == 3 ? ImGuiMouseCursor_ResizeEW
                                  : (hoverHandle == 0 || hoverHandle == 4) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
         else if (hoverHandle == 8)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
         else if (hoverGuide >= 0)
            ImGui::SetMouseCursor(guideAxis == 0 ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
      }

      const bool inRulerTop = d.rulers && mouse.y < mn.y + kRuler && mouse.x > mn.x + kRuler;
      const bool inRulerLeft = d.rulers && mouse.x < mn.x + kRuler && mouse.y > mn.y + kRuler;
      if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (panTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
         BeginOp(doc, 4, mouse, nullptr);
      else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         if (inRulerTop || inRulerLeft)
         {
            // A guide pulled from a ruler: the top ruler makes a horizontal one.
            BeginOp(doc, 6, mouse, "Add guide");
            sOp.before.clear();
            d.guidesOn = true;
            sOp.guideAxis = inRulerTop ? 1 : 0;
            std::vector<float>& gs = sOp.guideAxis == 0 ? d.guidesX : d.guidesY;
            gs.push_back(sOp.guideAxis == 0 ? sMouseDoc.x : sMouseDoc.y);
            sOp.guideIndex = (int)gs.size() - 1;
            Touch();
         }
         else if (sTool == kZoom)
            ZoomAt(doc, io.KeyAlt ? 0.5f : 2.0f, mouse);
         else if (sTool == kCrop)
         {
            const int h = HoverCropHandle(doc, mouse);
            sOp = Op();
            sOp.start = mouse;
            std::copy(sCrop, sCrop + 4, sOp.crop0);
            if (h >= 0)
            {
               sOp.kind = 5;
               sOp.cropHandle = h;
            }
            else
            {
               // A fresh box from where the drag starts.
               sOp.kind = 5;
               sOp.cropHandle = 4;
               sOp.crop0[0] = sOp.crop0[2] = std::round(sMouseDoc.x);
               sOp.crop0[1] = sOp.crop0[3] = std::round(sMouseDoc.y);
            }
         }
         else if (sTool == kMove)
         {
            if (hoverHandle >= 0 && hoverHandle < 8)
            {
               BeginOp(doc, 2, mouse, "Scale layer");
               sOp.hx = kHandleAt[hoverHandle][0];
               sOp.hy = kHandleAt[hoverHandle][1];
               sOp.fromCentre = io.KeyAlt;
            }
            else if (hoverHandle == 8)
               BeginOp(doc, 3, mouse, "Rotate layer");
            else if (hoverGuide >= 0)
            {
               BeginOp(doc, 6, mouse, "Move guide");
               sOp.guideAxis = guideAxis;
               sOp.guideIndex = hoverGuide;
            }
            else
            {
               const int hit = sAutoSelect || io.KeyCtrl || io.KeySuper ? HitLayer(doc, sMouseDoc) : 0;
               if (hit != 0)
               {
                  if (io.KeyShift)
                     ToggleSel(hit);
                  else if (!Selected(hit))
                     SelectOnly(hit);
                  else
                  {
                     // Clicking a layer already picked keeps the rest; it becomes the active one.
                     sSel.erase(std::find(sSel.begin(), sSel.end(), hit));
                     sSel.push_back(hit);
                  }
               }
               else if (sAutoSelect && !io.KeyShift)
                  sSel.clear();
               if (!sSel.empty())
                  BeginOp(doc, 1, mouse, "Move layer");
            }
         }
      }
      if (sOp.kind != 0)
      {
         const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left) || (sOp.kind == 4 && ImGui::IsMouseDown(ImGuiMouseButton_Middle));
         if (sOp.kind == 1)
            MoveSelection(doc, mouse);
         else if (sOp.kind == 2)
            ScaleActive(doc, mouse);
         else if (sOp.kind == 3)
            RotateActive(doc, mouse);
         else if (sOp.kind == 4)
            sPan = ImVec2(sOp.pan0.x + mouse.x - sOp.start.x, sOp.pan0.y + mouse.y - sOp.start.y);
         else if (sOp.kind == 5)
            DragCrop(doc, mouse);
         else if (sOp.kind == 6)
         {
            std::vector<float>& gs = sOp.guideAxis == 0 ? d.guidesX : d.guidesY;
            if (sOp.guideIndex >= 0 && sOp.guideIndex < (int)gs.size())
               gs[sOp.guideIndex] = std::round(sOp.guideAxis == 0 ? sMouseDoc.x : sMouseDoc.y);
            Touch();
         }
         if (!held)
         {
            // A guide let go over its ruler goes away.
            if (sOp.kind == 6 && ((sOp.guideAxis == 1 && mouse.y < sViewMn.y) || (sOp.guideAxis == 0 && mouse.x < sViewMn.x)))
            {
               std::vector<float>& gs = sOp.guideAxis == 0 ? d.guidesX : d.guidesY;
               if (sOp.guideIndex >= 0 && sOp.guideIndex < (int)gs.size())
                  gs.erase(gs.begin() + sOp.guideIndex);
               Touch();
            }
            sOp = Op();
            sSnapX.clear();
            sSnapY.clear();
         }
      }
      dl->PopClipRect();
   }

   // ---- header and status ----------------------------------------------

   void DrawHeader(Scene2DNode* doc)
   {
      const float availW = ImGui::GetContentRegionAvail().x;
      const float startX = ImGui::GetCursorPosX();
      auto gap = []() { FormParts::Inline(0.0f, tok::space_3); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      if (IconChip("##v2dmove", IconsInfinite::ToolMove, sTool == kMove, T("Move (V)"))) sTool = kMove;
      next();
      if (IconChip("##v2dcrop", IconsInfinite::Crop, sTool == kCrop, T("Crop (C). Enter applies, Esc cancels"))) sTool = kCrop;
      next();
      if (IconChip("##v2dhand", IconsInfinite::Hand, sTool == kHand, T("Hand (H, or hold Space)"))) sTool = kHand;
      next();
      if (IconChip("##v2dzoom", IconsInfinite::Zoom, sTool == kZoom, T("Zoom (Z). Alt-click zooms out"))) sTool = kZoom;
      if (sTool != kCrop)
         sCropOn = false;
      gap();
      // The context bar: what the current tool can do.
      Layer* a = Active(doc);
      if (sTool == kMove)
      {
         bool autoSel = sAutoSelect;
         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (tok::tile - CheckBox::kSize) * 0.5f);
         if (CheckBox::Box("##v2dauto", &autoSel))
            sAutoSelect = autoSel;
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::AlignTextToFramePadding();
         ImGui::TextDisabled("%s", T("Auto-select"));
         gap();
         static const char* kAlign[6] = { "##al0", "##al1", "##al2", "##al3", "##al4", "##al5" };
         static const char* kAlignTip[6] = { "Align left edges", "Align horizontal centres", "Align right edges",
                                             "Align top edges", "Align vertical centres", "Align bottom edges" };
         for (int i = 0; i < 6; i++)
         {
            if (i > 0)
               next();
            ImGui::BeginDisabled(sSel.empty());
            const bool hit = ActionButton::Draw(kAlign[i], ImVec2(kIconW, tok::tile));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               HelpTip("%s", T(kAlignTip[i]));
            // Drawn: a bar for the edge and two blocks lined up on it.
            const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
            const ImVec2 c((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 col = ImGui::GetColorU32(sSel.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            const int side = i % 3;
            if (i < 3)
            {
               const float x = side == 0 ? c.x - 6 : side == 1 ? c.x : c.x + 6;
               dl->AddLine(ImVec2(x, c.y - 7), ImVec2(x, c.y + 7), col, 1.5f);
               const float w1 = 10, w2 = 6;
               const float s1 = side == 0 ? x : side == 1 ? x - w1 / 2 : x - w1, s2 = side == 0 ? x : side == 1 ? x - w2 / 2 : x - w2;
               dl->AddRectFilled(ImVec2(s1, c.y - 5), ImVec2(s1 + w1, c.y - 1), col);
               dl->AddRectFilled(ImVec2(s2, c.y + 1), ImVec2(s2 + w2, c.y + 5), col);
            }
            else
            {
               const float y = side == 0 ? c.y - 6 : side == 1 ? c.y : c.y + 6;
               dl->AddLine(ImVec2(c.x - 7, y), ImVec2(c.x + 7, y), col, 1.5f);
               const float h1 = 10, h2 = 6;
               const float s1 = side == 0 ? y : side == 1 ? y - h1 / 2 : y - h1, s2 = side == 0 ? y : side == 1 ? y - h2 / 2 : y - h2;
               dl->AddRectFilled(ImVec2(c.x - 5, s1), ImVec2(c.x - 1, s1 + h1), col);
               dl->AddRectFilled(ImVec2(c.x + 1, s2), ImVec2(c.x + 5, s2 + h2), col);
            }
            if (hit)
               Align(doc, i);
         }
         if (a != nullptr && a->kind == Kind::Node)
         {
            gap();
            if (TextChip(L("Flip H##v2dfh"), a->flipX, T("Flip horizontal")))
            {
               PushUndoCheckpoint("Flip layer");
               a->flipX = !a->flipX;
               Touch();
            }
            next();
            if (TextChip(L("Flip V##v2dfv"), a->flipY, T("Flip vertical")))
            {
               PushUndoCheckpoint("Flip layer");
               a->flipY = !a->flipY;
               Touch();
            }
         }
      }
      else if (sTool == kCrop && sCropOn)
      {
         ImGui::AlignTextToFramePadding();
         ImGui::TextDisabled("%d x %d", (int)std::fabs(sCrop[2] - sCrop[0]), (int)std::fabs(sCrop[3] - sCrop[1]));
         gap();
         if (TextChip(L("Apply##v2dcropok"), false, T("Crop the document (Enter)")))
         {
            PushUndoCheckpoint("Crop document");
            doc->CropTo(sCrop[0], sCrop[1], sCrop[2], sCrop[3]);
            sCropOn = false;
            sTool = kMove;
            sNeedFit = true;
         }
         next();
         if (TextChip(L("Cancel##v2dcropno"), false, T("Esc")))
         {
            sCropOn = false;
            sTool = kMove;
         }
      }
      else
      {
         if (TextChip(L("Fit##v2dfit"), false, T("Fit on screen (Ctrl+0)")))
            FitView(doc);
         next();
         if (TextChip("100%##v2d100", false, T("Actual pixels (Ctrl+1)")))
         {
            sZoom = 1.0f;
            sPan = ImVec2(0, 0);
         }
      }
      ActionButton::RowNext();
      const char* renderL = L("Render##v2drender");
      const float renderW = ImGui::CalcTextSize(renderL, ImGui::FindRenderedTextEnd(renderL)).x + 2.0f * tok::space_3;
      float rightX = startX + availW - kIconW;
      float x = rightX - tok::space_1 - renderW;
      const char* convertL = L("Convert##v2dconvert");
      const float convertW = ImGui::CalcTextSize(convertL, ImGui::FindRenderedTextEnd(convertL)).x + 2.0f * tok::space_3;
      if (doc->panelOnly)
         x -= tok::space_1 + convertW;
      ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x));
      if (doc->panelOnly)
      {
         if (ActionButton::Draw(convertL, ImVec2(convertW, tok::tile)))
            sPending.what = Pending::Convert;
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Put this document on the canvas"));
         FormParts::Inline(0.0f, tok::space_1);
      }
      ImGui::BeginDisabled(gOfflineRender.active);
      if (ActionButton::Draw(renderL, ImVec2(renderW, tok::tile)))
         sPending.what = Pending::RenderImage;
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", T("Render Image (F12). Ctrl+F12 renders the animation"));
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dside", IconsInfinite::Layers, sSideOpen, T("Layers and properties"))) sSideOpen = !sSideOpen;
   }

   void DrawStatus(Scene2DNode* doc)
   {
      const Scene2DNode::Document& d = doc->Doc();
      char buf[200];
      if (sMouseIn)
         std::snprintf(buf, sizeof(buf), "%.0f%%   %d x %d   %.0f, %.0f   %.2f ms", sZoom * 100.0f, d.w, d.h, sMouseDoc.x,
                       sMouseDoc.y, doc->CookMs());
      else
         std::snprintf(buf, sizeof(buf), "%.0f%%   %d x %d   %.2f ms", sZoom * 100.0f, d.w, d.h, doc->CookMs());
      ImGui::TextDisabled("%s", buf);
   }

   // ---- layers list ------------------------------------------------------

   void DrawThumb(ImDrawList* dl, Scene2DNode* doc, const Layer& l, ImVec2 p, ImVec2 sz)
   {
      dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(40, 40, 40, 255), 2.0f);
      if (l.kind == Kind::Group)
      {
         glyph::Draw(dl, ImVec2(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), tok::icon_md, ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::Folder);
         return;
      }
      if (l.kind == Kind::Fill)
      {
         const ImU32 ca = ImGui::GetColorU32(ImVec4(l.colorA[0], l.colorA[1], l.colorA[2], 1.0f));
         const ImU32 cb = ImGui::GetColorU32(ImVec4(l.colorB[0], l.colorB[1], l.colorB[2], 1.0f));
         if (l.fillType == 0)
            dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ca, 2.0f);
         else
            dl->AddRectFilledMultiColor(p, ImVec2(p.x + sz.x, p.y + sz.y), ca, ca, cb, cb);
         return;
      }
      INode* src = doc->Source(l);
      const unsigned tex = src != nullptr ? src->GetOutputTexture() : 0;
      if (tex == 0)
      {
         dl->AddLine(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(200, 60, 60, 255));
         return;
      }
      const float sw = (float)std::max(1, src->GetOutputWidth()), sh = (float)std::max(1, src->GetOutputHeight());
      const float k = std::min(sz.x / sw, sz.y / sh);
      const ImVec2 c(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), h(sw * k * 0.5f, sh * k * 0.5f);
      dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y), ImVec2(0, 1), ImVec2(1, 0));
   }

   const ImU32 kLabelCol[8] = { 0, IM_COL32(230, 80, 80, 255), IM_COL32(240, 150, 60, 255), IM_COL32(235, 210, 70, 255),
                                IM_COL32(90, 190, 90, 255), IM_COL32(80, 150, 235, 255), IM_COL32(160, 110, 220, 255),
                                IM_COL32(150, 150, 150, 255) };

   void DrawLayerRows(Scene2DNode* doc, int parent, int depth, float w)
   {
      std::vector<int> kids = doc->ChildrenOf(parent);
      ImGuiIO& io = ImGui::GetIO();
      for (int k = (int)kids.size() - 1; k >= 0; k--)
      {
         Layer* l = doc->FindLayer(kids[k]);
         if (l == nullptr)
            continue;
         const int id = l->id;
         ImGui::PushID(id);
         const float rowH = 30.0f;
         const ImVec2 p = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool sel = Selected(id);
         if (sel)
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_Header, Active(doc) == l ? 1.0f : 0.6f), 3.0f);
         if (l->label > 0 && l->label < 8)
            dl->AddRectFilled(ImVec2(p.x, p.y + 3), ImVec2(p.x + 3, p.y + rowH - 3), kLabelCol[l->label]);
         // Eye: Alt-click shows only this one.
         ImGui::SetCursorScreenPos(ImVec2(p.x + 4, p.y + (rowH - 20) * 0.5f));
         if (ImGui::InvisibleButton("##eye", ImVec2(20, 20)))
         {
            if (io.KeyAlt)
               doc->soloLayer = doc->soloLayer == id ? 0 : id;
            else
            {
               PushUndoCheckpoint(l->visible ? "Hide layer" : "Show layer");
               l->visible = !l->visible;
               Touch();
            }
         }
         glyph::Draw(dl, ImVec2(p.x + 14, p.y + rowH * 0.5f), tok::icon_md,
                     ImGui::GetColorU32(l->visible ? ImGuiCol_Text : ImGuiCol_TextDisabled), l->visible ? IconsInfinite::Eye : IconsInfinite::EyeOff);
         float x = p.x + 28.0f + depth * 14.0f;
         if (l->kind == Kind::Group)
         {
            ImGui::SetCursorScreenPos(ImVec2(x, p.y + 7));
            if (ImGui::InvisibleButton("##open", ImVec2(12, 16)))
            {
               l->open = !l->open;
               Touch();
            }
            const ImVec2 c(x + 6, p.y + rowH * 0.5f);
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            if (l->open)
               dl->AddTriangleFilled(ImVec2(c.x - 4, c.y - 2), ImVec2(c.x + 4, c.y - 2), ImVec2(c.x, c.y + 3), col);
            else
               dl->AddTriangleFilled(ImVec2(c.x - 2, c.y - 4), ImVec2(c.x + 3, c.y), ImVec2(c.x - 2, c.y + 4), col);
         }
         x += 14.0f;
         if (l->clip)
         {
            // The bent arrow that points down at the layer it is clipped to.
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            dl->AddLine(ImVec2(x, p.y + 8), ImVec2(x, p.y + rowH - 9), col, 1.5f);
            dl->AddLine(ImVec2(x, p.y + rowH - 9), ImVec2(x + 6, p.y + rowH - 9), col, 1.5f);
            x += 10.0f;
         }
         DrawThumb(dl, doc, *l, ImVec2(x, p.y + 3), ImVec2(36, rowH - 6));
         x += 42.0f;
         ImGui::SetCursorScreenPos(ImVec2(x, p.y + (rowH - ImGui::GetFrameHeight()) * 0.5f));
         if (sRename == id)
         {
            ImGui::SetNextItemWidth(std::max(40.0f, p.x + w - x - 24.0f));
            if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
               ImGui::SetKeyboardFocusHere();
            const bool done = FieldWell::InputText("##rename", sRenameBuf, sizeof(sRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            if (done || ImGui::IsItemDeactivated())
            {
               PushUndoCheckpoint("Rename layer");
               l->name = sRenameBuf;
               Touch();
               sRename = 0;
            }
         }
         else
         {
            const std::string name = LayerName(*l);
            const bool missing = l->kind == Kind::Node && doc->Source(*l) == nullptr;
            ImGui::AlignTextToFramePadding();
            if (missing)
               ImGui::TextDisabled("%s", name.c_str());
            else
               ImGui::TextUnformatted(name.c_str());
         }
         if (l->lock != 0)
            glyph::Draw(dl, ImVec2(p.x + w - 12, p.y + rowH * 0.5f), tok::icon_sm, ImGui::GetColorU32(ImGuiCol_TextDisabled), IconsInfinite::Lock);
         // The row itself: select, rename, drag to restack.
         ImGui::SetCursorScreenPos(ImVec2(x, p.y));
         ImGui::SetNextItemAllowOverlap();
         ImGui::InvisibleButton("##row", ImVec2(std::max(1.0f, p.x + w - x), rowH));
         if (ImGui::IsItemClicked())
         {
            if (io.KeyShift || io.KeyCtrl || io.KeySuper)
               ToggleSel(id);
            else
               SelectOnly(id);
         }
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            sRename = id;
            std::snprintf(sRenameBuf, sizeof(sRenameBuf), "%s", LayerName(*l).c_str());
         }
         if (ImGui::BeginDragDropSource())
         {
            ImGui::SetDragDropPayload("V2D_LAYER", &id, sizeof(int));
            ImGui::TextUnformatted(LayerName(*l).c_str());
            ImGui::EndDragDropSource();
         }
         if (ImGui::BeginDragDropTarget())
         {
            const float t = (io.MousePos.y - p.y) / rowH;
            const bool into = l->kind == Kind::Group && t > 0.3f && t < 0.7f;
            const bool below = !into && t >= 0.5f;
            const ImU32 col = Hex(kOutline);
            if (into)
               dl->AddRect(p, ImVec2(p.x + w, p.y + rowH), col, 3.0f, 0, 2.0f);
            else
               dl->AddLine(ImVec2(p.x, below ? p.y + rowH : p.y), ImVec2(p.x + w, below ? p.y + rowH : p.y), col, 2.0f);
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("V2D_LAYER"))
            {
               const int moving = *(const int*)pl->Data;
               if (moving != id && !doc->IsInside(id, moving))
               {
                  PushUndoCheckpoint("Move layer");
                  doc->MoveLayer(moving, id, below, into);
               }
            }
            ImGui::EndDragDropTarget();
         }
         if (MenuParts::BeginContextItem("##rowmenu"))
         {
            if (!Selected(id))
               SelectOnly(id);
            if (MenuParts::Item(T("Duplicate Layer"), "Ctrl+J")) sPending.what = Pending::Duplicate;
            if (MenuParts::Item(T("Group Layers"), "Ctrl+G")) sPending.what = Pending::Group;
            if (l->kind == Kind::Group && MenuParts::Item(T("Ungroup"), "Shift+Ctrl+G")) sPending.what = Pending::Ungroup;
            if (MenuParts::Item(l->clip ? T("Release Clipping Mask") : T("Create Clipping Mask"), "Alt+Ctrl+G"))
            {
               PushUndoCheckpoint("Clipping mask");
               l->clip = !l->clip;
               Touch();
            }
            if (MenuParts::Item(T("Rename"), nullptr))
            {
               sRename = id;
               std::snprintf(sRenameBuf, sizeof(sRenameBuf), "%s", LayerName(*l).c_str());
            }
            MenuParts::Separator();
            if (MenuParts::Item(T("Delete Layer"), "Delete")) sPending.what = Pending::Delete;
            MenuParts::EndPopup();
         }
         ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + 1.0f));
         ImGui::Dummy(ImVec2(0, 0));
         const bool open = l->kind == Kind::Group && l->open;
         ImGui::PopID();
         if (open)
            DrawLayerRows(doc, id, depth + 1, w);
      }
   }

   std::vector<std::string> BlendNames(int current, bool group)
   {
      std::vector<std::string> names;
      if (group)
         names.push_back(T("Pass Through"));
      for (const std::string& n : BlendModes::Names())
         names.push_back(T(n.c_str()));
      (void)current;
      return names;
   }

   void DrawLayersCard(Scene2DNode* doc, float w, float listH)
   {
      Layer* a = Active(doc);
      // Blend, opacity and fill sit over the list, where the eye already is.
      if (a != nullptr)
      {
         const bool group = a->kind == Kind::Group;
         std::vector<std::string> names = BlendNames(a->blend, group);
         ImGui::SetNextItemWidth(w * 0.5f - tok::space_1);
         const int offset = group ? -1 : 0;
         const int shown = std::clamp(a->blend - offset, 0, (int)names.size() - 1);
         if (MenuParts::BeginCombo("##v2dblend", names[shown].c_str()))
         {
            for (int i = 0; i < (int)names.size(); i++)
               if (MenuParts::Choice(names[i].c_str(), i == shown) && i != shown)
               {
                  PushUndoCheckpoint("Blend mode");
                  a->blend = i + offset;
                  Touch();
               }
            MenuParts::EndCombo();
         }
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::SetNextItemWidth(w * 0.5f - tok::space_1);
         float pct = a->opacity * 100.0f;
         if (FieldWell::Slider("##v2dopacity", &pct, 0.0f, 100.0f, "Opacity %.0f%%"))
         {
            a->opacity = pct / 100.0f;
            Touch();
         }
         UndoOnActivate("Opacity");
         ImGui::SetNextItemWidth(w * 0.5f - tok::space_1);
         float fillPct = a->fill * 100.0f;
         if (FieldWell::Slider("##v2dfill", &fillPct, 0.0f, 100.0f, "Fill %.0f%%"))
         {
            a->fill = fillPct / 100.0f;
            Touch();
         }
         UndoOnActivate("Fill");
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::AlignTextToFramePadding();
         ImGui::TextDisabled("%s", T("Lock"));
         struct { unsigned bit; const char* label; const char* tip; } kLocks[4] = {
            { Scene2DNode::kLockAlpha, "A##lk0", "Lock transparent pixels" },
            { Scene2DNode::kLockPixels, "P##lk1", "Lock image pixels" },
            { Scene2DNode::kLockPosition, "M##lk2", "Lock position" },
            { Scene2DNode::kLockAll, "All##lk3", "Lock all" },
         };
         for (auto& lk : kLocks)
         {
            FormParts::Inline(0.0f, tok::space_1);
            if (TextChip(lk.label, (a->lock & lk.bit) != 0, T(lk.tip)))
            {
               PushUndoCheckpoint("Lock layer");
               a->lock ^= lk.bit;
               Touch();
            }
         }
      }
      ImGui::BeginChild("##v2dlayers", ImVec2(w, listH), ImGuiChildFlags_None);
      const float rowsW = ImGui::GetContentRegionAvail().x;
      if (doc->Layers().empty())
         ImGui::TextDisabled("%s", T("Right-click a node and choose Add to 2D View"));
      DrawLayerRows(doc, 0, 0, rowsW);
      // Dropping below the last row puts a layer at the bottom of the document.
      ImGui::Dummy(ImVec2(rowsW, std::max(8.0f, ImGui::GetContentRegionAvail().y)));
      if (ImGui::BeginDragDropTarget())
      {
         if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("V2D_LAYER"))
         {
            std::vector<int> top = doc->ChildrenOf(0);
            const int moving = *(const int*)pl->Data;
            if (!top.empty() && top.front() != moving)
            {
               PushUndoCheckpoint("Move layer");
               doc->MoveLayer(moving, top.front(), true);
            }
         }
         ImGui::EndDragDropTarget();
      }
      ImGui::EndChild();
      // The strip under the list, as in every layers panel.
      if (IconChip("##v2dnewfill", IconsInfinite::Fill, false, T("New fill layer")))
         sPending.what = Pending::AddFill;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewgroup", IconsInfinite::Folder, false, T("Group (Ctrl+G)")))
         sPending.what = Pending::Group;
      FormParts::Inline(0.0f, tok::space_1);
      ImGui::BeginDisabled(a == nullptr || a->kind == Kind::Group);
      if (IconChip("##v2dmask", IconsInfinite::Mask, a != nullptr && a->maskUid != 0, T("Mask: pick a node in the Layer tab")))
         sTab = 0;
      ImGui::EndDisabled();
      FormParts::Inline(0.0f, tok::space_1);
      ImGui::BeginDisabled(sSel.empty());
      if (IconChip("##v2ddup", IconsInfinite::Copy, false, T("Duplicate (Ctrl+J)")))
         sPending.what = Pending::Duplicate;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2ddel", IconsInfinite::Close, false, T("Delete layer")))
         sPending.what = Pending::Delete;
      ImGui::EndDisabled();
   }

   // ---- tabs ------------------------------------------------------------

   void DrawLayerTab(Scene2DNode* doc)
   {
      Layer* l = Active(doc);
      if (l == nullptr)
      {
         ImGui::TextDisabled("%s", T("No layer selected"));
         return;
      }
      SectionCard::Begin(T("Layer"));
      if (l->kind == Kind::Node)
      {
         NodeRow(T("Source"), &l->uid, false, doc);
         int fit = l->fit;
         if (ComboRow(T("Fit"), &fit, { T("None"), T("Fit"), T("Fill"), T("Stretch") }))
         {
            // The picture stays where it is: only how its box is sized changes.
            l->fit = fit;
         }
         int sampling = l->nearest ? 1 : 0;
         if (ComboRow(T("Sampling"), &sampling, { T("Smooth"), T("Nearest") }))
            l->nearest = sampling == 1;
      }
      int label = l->label;
      if (ComboRow(T("Colour"), &label, { T("None"), T("Red"), T("Orange"), T("Yellow"), T("Green"), T("Blue"), T("Violet"), T("Grey") }))
         l->label = label;
      BoolRow(T("Clip to layer below"), &l->clip);
      if (l->kind == Kind::Fill)
      {
         SectionCard::Begin(T("Fill"));
         int type = l->fillType;
         if (ComboRow(T("Type"), &type, { T("Solid"), T("Linear"), T("Radial") }))
            l->fillType = type;
         ColorRow(type == 0 ? T("Colour") : T("From"), l->colorA);
         SliderRow(type == 0 ? T("Alpha") : T("From alpha"), &l->colorA[3], 0.0f, 1.0f, "%.2f");
         if (type != 0)
         {
            ColorRow(T("To"), l->colorB);
            SliderRow(T("To alpha"), &l->colorB[3], 0.0f, 1.0f, "%.2f");
         }
         if (type == 1)
            SliderRow(T("Angle"), &l->fillAngle, -180.0f, 180.0f, "%.0f\xC2\xB0");
      }
      if (l->kind == Kind::Node)
      {
         SectionCard::Begin(T("Transform"));
         DragRow("X", &l->x, 1.0f, "%.1f px");
         DragRow("Y", &l->y, 1.0f, "%.1f px");
         float bw = 1, bh = 1;
         doc->LayerBox(*l, bw, bh);
         float wpx = bw * l->sx, hpx = bh * l->sy;
         if (DragRow("W", &wpx, 1.0f, "%.1f px"))
            l->sx = bw > 0 ? wpx / bw : 1.0f;
         if (DragRow("H", &hpx, 1.0f, "%.1f px"))
            l->sy = bh > 0 ? hpx / bh : 1.0f;
         DragRow(T("Angle"), &l->angle, 0.5f, "%.1f\xC2\xB0", -180.0f, 180.0f);
         int anchor = (int)std::lround(l->ay * 2.0f) * 3 + (int)std::lround(l->ax * 2.0f);
         if (AnchorRow(T("Anchor"), &anchor))
         {
            // Moving the anchor keeps the picture still: the position follows it.
            PushUndoCheckpoint("Anchor");
            float m[6], ox, oy;
            const float nax = (anchor % 3) * 0.5f, nay = (anchor / 3) * 0.5f;
            doc->LayerMatrix(*l, m);
            Apply(m, nax * bw, nay * bh, ox, oy);
            l->ax = nax;
            l->ay = nay;
            l->x = ox;
            l->y = oy;
            Touch();
         }
         BoolRow(T("Flip horizontal"), &l->flipX);
         BoolRow(T("Flip vertical"), &l->flipY);
         if (TextChip(L("Reset##v2dreset"), false, T("Back to the document centre, scale 1, no rotation")))
         {
            PushUndoCheckpoint("Reset transform");
            l->x = doc->Doc().w * 0.5f;
            l->y = doc->Doc().h * 0.5f;
            l->sx = l->sy = 1.0f;
            l->angle = 0.0f;
            l->ax = l->ay = 0.5f;
            l->flipX = l->flipY = false;
            Touch();
         }

         SectionCard::Begin(T("Crop"));
         BoolRow(T("Crop layer"), &l->cropOn);
         if (l->cropOn)
         {
            DragRow(T("Left"), &l->crop[0], 1.0f, "%.0f px", 0.0f, bw);
            DragRow(T("Top"), &l->crop[1], 1.0f, "%.0f px", 0.0f, bh);
            DragRow(T("Right"), &l->crop[2], 1.0f, "%.0f px", 0.0f, bw);
            DragRow(T("Bottom"), &l->crop[3], 1.0f, "%.0f px", 0.0f, bh);
         }
      }
      if (l->kind != Kind::Group || true)
      {
         SectionCard::Begin(T("Mask"));
         NodeRow(T("Node"), &l->maskUid, true, doc);
         if (l->maskUid != 0)
         {
            BoolRow(T("Enabled"), &l->maskOn);
            BoolRow(T("Invert"), &l->maskInvert);
            int ch = l->maskChannel;
            if (ComboRow(T("From"), &ch, { T("Luminance"), T("Alpha") }))
               l->maskChannel = ch;
            SliderRow(T("Density"), &l->maskDensity, 0.0f, 1.0f, "%.2f");
         }
      }
   }

   struct Preset
   {
      const char* name;
      int w, h;
   };
   const Preset kPresets[] = {
      { "HD 1920 x 1080", 1920, 1080 },  { "4K UHD 3840 x 2160", 3840, 2160 }, { "720p 1280 x 720", 1280, 720 },
      { "Square 1080", 1080, 1080 },     { "Portrait 1080 x 1920", 1080, 1920 }, { "Social 4:5 1080 x 1350", 1080, 1350 },
      { "A4 300 dpi", 2480, 3508 },      { "Letter 300 dpi", 2550, 3300 },
   };

   void DrawDocumentTab(Scene2DNode* doc)
   {
      Scene2DNode::Document& d = doc->Doc();
      SectionCard::Begin(T("Document"));
      FieldLabel(T("Preset"));
      ImGui::SetNextItemWidth(sFieldW);
      if (MenuParts::BeginCombo("##v2dpreset", T("Choose...")))
      {
         for (const Preset& p : kPresets)
            if (MenuParts::Choice(p.name, p.w == d.w && p.h == d.h))
            {
               PushUndoCheckpoint("Document size");
               doc->CanvasSize(p.w, p.h, 4);
            }
         MenuParts::EndCombo();
      }
      static int sCanvasW = 0, sCanvasH = 0, sAnchor = 4, sImageW = 0, sImageH = 0;
      static uint64_t sFor = 0;
      static int sSeenW = 0, sSeenH = 0;
      if (sFor != sDocUid || sSeenW != d.w || sSeenH != d.h)
      {
         sFor = sDocUid;
         sSeenW = sCanvasW = sImageW = d.w;
         sSeenH = sCanvasH = sImageH = d.h;
      }
      SectionCard::Begin(T("Canvas size"));
      if (IntRow(T("Width"), &sCanvasW) && d.ratioLock)
         sCanvasH = (int)std::lround(sCanvasW * (float)d.h / (float)d.w);
      if (IntRow(T("Height"), &sCanvasH) && d.ratioLock)
         sCanvasW = (int)std::lround(sCanvasH * (float)d.w / (float)d.h);
      AnchorRow(T("Anchor"), &sAnchor);
      BoolRow(T("Keep proportions"), &d.ratioLock);
      ImGui::BeginDisabled(sCanvasW == d.w && sCanvasH == d.h);
      if (TextChip(L("Apply##v2dcanvas"), false, T("Grow or shrink the document; layers keep their place")))
      {
         PushUndoCheckpoint("Canvas size");
         doc->CanvasSize(sCanvasW, sCanvasH, sAnchor);
      }
      ImGui::EndDisabled();
      SectionCard::Begin(T("Image size"));
      if (IntRow(T("Width##img"), &sImageW))
         sImageH = (int)std::lround(sImageW * (float)d.h / (float)d.w);
      if (IntRow(T("Height##img"), &sImageH))
         sImageW = (int)std::lround(sImageH * (float)d.w / (float)d.h);
      ImGui::BeginDisabled(sImageW == d.w && sImageH == d.h);
      if (TextChip(L("Apply##v2dimage"), false, T("Scale the document and every layer with it")))
      {
         PushUndoCheckpoint("Image size");
         doc->ImageSize(sImageW, sImageH);
      }
      ImGui::EndDisabled();

      SectionCard::Begin(T("Background"));
      ComboRow(T("Fill"), &d.background, { T("White"), T("Black"), T("Colour"), T("Transparent") });
      if (d.background == 2)
         ColorRow(T("Colour"), d.bgColor);
      BoolRow(T("Blend in linear light"), &d.linear);
      BoolRow(T("Keep light above 1.0"), &d.headroom);

      SectionCard::Begin(T("Rulers, guides, grid"));
      BoolRow(T("Rulers"), &d.rulers);
      BoolRow(T("Guides"), &d.guidesOn);
      BoolRow(T("Grid"), &d.grid);
      BoolRow(T("Snap"), &d.snap);
      if (d.grid)
      {
         DragRow(T("Grid size"), &d.gridSize, 1.0f, "%.0f px", 2.0f, 4096.0f);
         FieldLabel(T("Subdivisions"));
         ImGui::SetNextItemWidth(sFieldW);
         if (FieldWell::Slider("##v2dsub", &d.gridSubdiv, 1, 16, "%d"))
            Touch();
         UndoOnActivate();
      }
      ImGui::BeginDisabled(d.guidesX.empty() && d.guidesY.empty());
      if (TextChip(L("Clear Guides##v2dclear"), false, nullptr))
      {
         PushUndoCheckpoint("Clear guides");
         d.guidesX.clear();
         d.guidesY.clear();
         Touch();
      }
      ImGui::EndDisabled();
      FormParts::Inline(0.0f, tok::space_1);
      if (TextChip(L("Centre Guides##v2dcentre"), false, nullptr))
      {
         PushUndoCheckpoint("Add guides");
         d.guidesOn = true;
         d.guidesX.push_back(d.w * 0.5f);
         d.guidesY.push_back(d.h * 0.5f);
         Touch();
      }
   }

   void DrawOutputTab(Scene2DNode* doc, float availW)
   {
      Scene2DNode::Document& d = doc->Doc();
      SectionCard::Begin(T("Render"));
      ImGui::BeginDisabled(gOfflineRender.active);
      if (ActionButton::Draw(L("Render Image##v2dimg"), ImVec2(availW, 0)))
         sPending.what = Pending::RenderImage;
      ImGui::EndDisabled();
      ImGui::TextDisabled("%d x %d, F12", d.w, d.h);
      SectionCard::Begin(T("Animation"));
      DragRow(T("Frame rate"), &d.fps, 0.1f, "%.0f fps", 1.0f, 240.0f);
      DragRow(T("Length"), &d.seconds, 0.1f, "%.1f s", 0.1f, 3600.0f);
      ImGui::BeginDisabled(gOfflineRender.active);
      if (ActionButton::Draw(L("Render Animation##v2danim"), ImVec2(availW, 0)))
         sPending.what = Pending::RenderAnim;
      ImGui::EndDisabled();
      ImGui::TextDisabled("%s", "Ctrl+F12");
      OutputNode* out = FindOutput();
      if (out != nullptr && !out->RecordStatus().empty())
         ImGui::TextDisabled("%s", out->RecordStatus().c_str());
      if (!sStillPath.empty())
         ImGui::TextDisabled("%s", T("Saving image..."));
      std::error_code ec;
      if (!sLastRender.empty() && std::filesystem::exists(sLastRender, ec))
      {
         ImGui::PushTextWrapPos(0.0f);
         ImGui::TextDisabled("%s", sLastRender.c_str());
         ImGui::PopTextWrapPos();
         if (ActionButton::Draw(L("Reveal##v2dreveal"), ImVec2(availW, 0)))
            Platform::RevealInFileManager(sLastRender);
      }
   }

   void DrawDocPicker(Scene2DNode* doc, float w)
   {
      std::vector<GraphNode*> docs;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<Scene2DNode*>(gn.node.get()) != nullptr)
            docs.push_back(&gn);
      GraphNode* self = DocGraphNode();
      auto nameOf = [](const GraphNode* gn) { return std::string("Document ") + std::to_string(gn->index); };
      ImGui::SetNextItemWidth(w - kIconW - tok::space_1);
      if (MenuParts::BeginCombo("##v2ddocs", self != nullptr ? nameOf(self).c_str() : ""))
      {
         for (GraphNode* gn : docs)
            if (MenuParts::Choice((nameOf(gn) + "##d" + std::to_string(gn->uid)).c_str(), gn == self))
            {
               sDocUid = gn->uid;
               sSel.clear();
               sNeedFit = true;
            }
         MenuParts::EndCombo();
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2ddocmenu", IconsInfinite::Plus, false, T("New, duplicate or delete a document")))
         ImGui::OpenPopup("##v2ddocpop");
      if (MenuParts::BeginPopup("##v2ddocpop"))
      {
         if (MenuParts::Item(T("New Document"))) sPending.what = Pending::NewDoc;
         if (MenuParts::Item(T("Duplicate Document"))) sPending.what = Pending::DuplicateDoc;
         MenuParts::Separator();
         if (MenuParts::Item(T("Delete Document"), nullptr, false, doc != nullptr)) sPending.what = Pending::DeleteDoc;
         MenuParts::EndPopup();
      }
   }

   void DrawSide(Scene2DNode* doc, float panelW)
   {
      PushDockedPanelStyle(/*isChild=*/true);
      const ImVec2 savedPad = ImGui::GetStyle().WindowPadding;
      ImGui::GetStyle().WindowPadding = ImVec2(tok::space_3, tok::space_3);
      ImGui::BeginChild("##v2dside", ImVec2(panelW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::GetStyle().WindowPadding = savedPad;
      PopDockedPanelStyle();
      const float availW = ImGui::GetContentRegionAvail().x;
      DrawDocPicker(doc, availW);
      const float roomH = ImGui::GetContentRegionAvail().y;
      DrawLayersCard(doc, availW, std::max(90.0f, roomH * 0.42f - 70.0f));
      ImGui::Dummy(ImVec2(0, tok::space_1));
      const struct { const char* id; const char* label; } kTabs[3] = { { "##v2dt0", "Layer" }, { "##v2dt1", "Document" }, { "##v2dt2", "Output" } };
      for (int i = 0; i < 3; i++)
      {
         if (i > 0)
            FormParts::Inline(0.0f, tok::space_1);
         if (TextChip((std::string(T(kTabs[i].label)) + kTabs[i].id).c_str(), sTab == i, nullptr))
            sTab = i;
      }
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SectionCard::kPad, 0.0f));
      ImGui::BeginChild("##v2dprops", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::PopStyleVar();
      SectionCard::BeginWindow();
      const float w = ImGui::GetContentRegionAvail().x;
      sFieldW = std::max(70.0f, w - kLabelW);
      if (sTab == 0) DrawLayerTab(doc);
      else if (sTab == 1) DrawDocumentTab(doc);
      else DrawOutputTab(doc, w);
      SectionCard::EndWindow();
      ImGui::EndChild();
      ImGui::EndChild();
   }

   // ---- deferred actions -------------------------------------------------

   void DeleteDoc(Scene2DNode* doc)
   {
      PushUndoCheckpoint("Delete document");
      std::vector<int> nodes;
      for (GraphNode& gn : gNodes)
         if (auto* out = dynamic_cast<OutputNode*>(gn.node.get()))
            if (doc->panelOnly && out->Input().GetSource() == doc)
               nodes.push_back(gn.index);
      if (const GraphNode* self = DocGraphNode())
         nodes.push_back(self->index);
      sDocUid = 0;
      sSel.clear();
      for (int index : nodes)
         RemoveNodeByIndex(index);
   }

   void RunPending(Scene2DNode* doc)
   {
      const Pending todo = sPending;
      sPending = Pending();
      GraphNode* self = DocGraphNode();
      const ImVec2 at = self != nullptr ? NodePos(*self) : ImVec2(0, 0);
      switch (todo.what)
      {
      case Pending::NewDoc:
         PushUndoCheckpoint("New document");
         MakeDoc(at.x, at.y + 300.0f, "");
         break;
      case Pending::DuplicateDoc:
      {
         PushUndoCheckpoint("Duplicate document");
         const std::string text = doc->DocText();
         MakeDoc(at.x, at.y + 300.0f, text);
         break;
      }
      case Pending::DeleteDoc: DeleteDoc(doc); break;
      case Pending::Convert:
         if (self != nullptr)
         {
            PushUndoCheckpoint("Convert document to node");
            doc->panelOnly = false;
            doc->Commit();
            self->hiddenFromCanvas = false;
            gRequestSelectNodeIndex = self->index;
            gRequestFitViewNodeIndex = self->index;
         }
         break;
      case Pending::RenderImage: RenderImage(); break;
      case Pending::RenderAnim: RenderAnimation(doc); break;
      case Pending::Group:
         if (!sSel.empty())
         {
            PushUndoCheckpoint("Group layers");
            SelectOnly(doc->GroupLayers(sSel));
         }
         break;
      case Pending::Ungroup:
         if (Layer* a = Active(doc))
            if (a->kind == Kind::Group)
            {
               PushUndoCheckpoint("Ungroup");
               const std::vector<int> kids = doc->ChildrenOf(a->id);
               doc->Ungroup(a->id);
               sSel = kids;
            }
         break;
      case Pending::Duplicate:
         if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Duplicate layer");
            SelectOnly(doc->Duplicate(a->id));
         }
         break;
      case Pending::Delete: DeleteSelected(doc); break;
      case Pending::AddFill: AddFill(doc); break;
      case Pending::None: break;
      }
   }

   void DrawDockMenu(float top, float bottom)
   {
      const ImVec2 mn(ImGui::GetWindowPos().x, top), mx(mn.x + ImGui::GetWindowSize().x, bottom);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(mn, mx) && !ImGui::IsAnyItemHovered() &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
         ImGui::OpenPopup("##v2ddockmenu");
      if (MenuParts::BeginPopup("##v2ddockmenu"))
      {
         if (MenuParts::SubMenu(T("Dock Position")))
         {
            const bool atTop = gView2DDock == 3;
            if (MenuParts::Item(T("2D View at Bottom"), nullptr, !atTop) && atTop)
               gView2DDock = 0;
            if (MenuParts::Item(T("2D View at Top"), nullptr, atTop) && !atTop)
               gView2DDock = 3;
            ImGui::EndMenu();
         }
         ImGui::Dummy(ImVec2(0.0f, tok::space_1));
         if (MenuParts::Item(T("Close 2D View")))
            gView2DOpen = false;
         MenuParts::EndPopup();
      }
   }

   void DrawPanel()
   {
      FollowCanvasSelection();
      Scene2DNode* doc = FindDoc();
      if (doc == nullptr)
         doc = MakeDoc(0.0f, 0.0f, "");   // the first document is there from the start
      if (doc == nullptr)
      {
         gView2DClaimedKeys = false;
         EmptyState::DrawInWindow(T("Nothing in the 2D View yet"), T("Right-click any node and choose Add to 2D View."));
         return;
      }
      if (GraphNode* self = DocGraphNode())
         sDocUid = self->uid;
      doc->Layers();
      PruneSelection(doc);
      if (doc->FindLayer(doc->soloLayer) == nullptr)
         doc->soloLayer = 0;
      sDirty = false;
      PumpStill();

      const float headTop = ImGui::GetCursorScreenPos().y;
      DrawHeader(doc);
      DrawDockMenu(headTop, ImGui::GetCursorScreenPos().y);
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float gapX = ImGui::GetStyle().ItemSpacing.x;
      const float sideW = sSideOpen ? std::min(kSideW, avail.x * 0.5f) : 0.0f;
      const float viewW = std::max(120.0f, avail.x - sideW - (sSideOpen ? gapX : 0.0f));
      const float statusH = ImGui::GetTextLineHeightWithSpacing();
      ImGui::BeginGroup();
      DrawCanvas(doc, ImVec2(viewW, std::max(80.0f, avail.y - statusH)));
      DrawStatus(doc);
      ImGui::EndGroup();
      if (sSideOpen)
      {
         ActionButton::RowNext();
         DrawSide(doc, sideW);
      }
      if (sDirty)
         doc->Commit();
      RunPending(doc);
   }
}

OutputNode* View2DEnsureOutput() { return EnsureOutputImpl(); }

// Anything with a 2D picture except a 3D node or a document. Uses the
// viewport panel's pixel gate, not the output size: a node that has not
// cooked yet reports 0x0.
bool View2DCanAdd(const GraphNode& gn)
{
   INode* n = gn.node.get();
   return n != nullptr && dynamic_cast<Scene2DNode*>(n) == nullptr && dynamic_cast<IGeometrySource*>(n) == nullptr &&
          CanShowInViewportPanel(gn);
}

void View2DAddNode(GraphNode& gn)
{
   // MakeDoc can move gNodes, so read what's needed from gn first.
   const uint64_t uid = gn.uid;
   PushUndoCheckpoint("Add to 2D View");
   Scene2DNode* doc = EnsureDoc(gn);
   if (doc == nullptr)
      return;
   gView2DOpen = true;
   Layer* a = Active(doc);
   Layer& l = doc->AddLayer(Kind::Node, uid, a != nullptr && a->kind != Kind::Group ? a->id : 0);
   SelectOnly(l.id);
}

void DrawScene2DBody(GraphNode& gn)
{
   auto* doc = dynamic_cast<Scene2DNode*>(gn.node.get());
   if (doc == nullptr)
      return;
   ImGui::TextDisabled("%d x %d, %d layers", doc->Doc().w, doc->Doc().h, (int)doc->Layers().size());
   if (ActionButton::Draw(L("Open in 2D View"), ImVec2(160.0f, 0)))
   {
      sDocUid = gn.uid;
      sSel.clear();
      sNeedFit = true;
      gView2DOpen = true;
   }
}

void DrawView2DDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const bool gripFirst = gView2DDock != 3;
   ImGui::BeginChild(id, size, false);
   gView2DRectMin = ImGui::GetWindowPos();
   gView2DRectMax = ImVec2(gView2DRectMin.x + ImGui::GetWindowSize().x, gView2DRectMin.y + ImGui::GetWindowSize().y);
   gView2DClaimedKeys = false;
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]()
   {
      ImGui::InvisibleButton("##view2dgrip", ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(false, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         gView2DHeight += gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y;
         gView2DHeight = std::max(kMinHeight, gView2DHeight);
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
   PanelFrame::BeginCard("##view2dcontent", cardSize, in);
   DrawPanel();
   PanelFrame::EndCard();
   if (!gripFirst)
      grip();
   ImGui::EndChild();
}

} // namespace app
