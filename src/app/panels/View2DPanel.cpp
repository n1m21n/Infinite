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
#include "core/BrushEngine.h"
#include "nodes/DrawNode.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/Scene3DNode.h"
#include "nodes/SceneNodes.h"
#include "nodes/ImageSourceNode.h"
#include "nodes/OutputNode.h"
#include "nodes/Scene2DNode.h"
#include "nodes/SketchNode.h"
#include "nodes/TextNode.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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

   enum Tool { kMove = 0, kCrop, kHand, kZoom, kBrush, kEraser, kFill, kPick, kMarquee, kLasso, kWand, kPen, kNode, kShape, kText };
   int sTool = kMove;
   bool PaintTool(int t) { return t == kBrush || t == kEraser || t == kFill; }
   bool PixelTool(int t) { return t >= kBrush && t <= kWand; }

   // Paint tools. The brush and the eraser keep their own settings.
   BrushEngine::Brush sBrush, sEraser;
   int sBrushPreset = -1, sEraserPreset = -1;
   bool sBrushInit = false;
   float sFg[3] = { 0.0f, 0.0f, 0.0f }, sBg[3] = { 1.0f, 1.0f, 1.0f };
   int sFillKind = 0;              // 0 bucket, 1 linear, 2 radial
   int sTolerance = 32;
   bool sContiguous = true, sAllLayers = false;
   bool sEditMask = false;         // the brush paints the active layer's pixel mask
   int sMarqueeShape = 0;          // 0 rectangle, 1 ellipse
   int sLassoKind = 0;             // 0 freehand, 1 polygon
   float sFeather = 0.0f;
   int sSelOp = 0;                 // Scene2DNode::SelOp
   std::vector<float> sPoly;       // polygon lasso points so far, document px
   int sPolyOp = 0;
   bool sHaveLast = false;         // where the last stroke ended (Shift-click draws a line from it)
   ImVec2 sLastPaint(0.0f, 0.0f);
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
      int kind = 0;               // 1 move, 2 scale, 3 rotate, 4 pan, 5 crop box, 6 guide, 7 paint, 8 gradient, 9 marquee, 10 lasso, 11 pick
      ImVec2 docStart;
      int selOp = 0;
      std::vector<float> path;    // freehand lasso, document px
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
      bool made = false;          // the shape tool has added its item
   } sOp;

   bool sCropOn = false;
   float sCrop[4] = { 0, 0, 0, 0 };   // x0 y0 x1 y1, document px

   struct Pending
   {
      enum What { None, NewDoc, DuplicateDoc, DeleteDoc, Convert, RenderImage, RenderAnim, Group, Ungroup, Duplicate, Delete, AddFill, AddPaint, AddMask, MaskFromSel, RemoveMask, ToDraw, AddVector, AddVMask, RemoveVMask, ToSketch, ToText };
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
      if (l.kind == Kind::Paint)
         return std::string(T("Layer")) + " " + std::to_string(l.id);
      if (l.kind == Kind::Vector)
         return std::string(T("Vector")) + " " + std::to_string(l.id);
      if (l.kind == Kind::Text)
      {
         std::string first = l.text.substr(0, l.text.find('\n'));
         if (first.size() > 28)
            first = first.substr(0, 28) + "...";
         return first.empty() ? std::string(T("Text")) + " " + std::to_string(l.id) : first;
      }
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
         if ((l.kind != Kind::Node && l.kind != Kind::Paint && l.kind != Kind::Vector && l.kind != Kind::Text) || !doc->Shown(l) ||
             (l.lock & Scene2DNode::kLockAll) != 0)
            continue;
         if (l.kind == Kind::Vector)
         {
            // Only where the layer has items, not its whole document-sized box.
            float bx = 0, by = 0;
            doc->DocToBox(l, p.x, p.y, bx, by);
            for (const Scene2DNode::Shape& s : l.shapes)
            {
               float lo[2], hi[2];
               if (Scene2DNode::ShapeBounds(s, lo, hi) && bx >= lo[0] && bx <= hi[0] && by >= lo[1] && by <= hi[1])
                  return l.id;
            }
            continue;
         }
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

   void AddPaint(Scene2DNode* doc)
   {
      PushUndoCheckpoint("New layer");
      Layer* a = Active(doc);
      SelectOnly(doc->AddPaintLayer(a != nullptr && a->kind != Kind::Group ? a->id : 0));
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

   void HandlePixelKeys(Scene2DNode* doc, bool cmd, bool shift);

   bool HandleVectorKeys(Scene2DNode* doc);

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
         if (key(ImGuiKey_B)) sTool = kBrush;
         if (key(ImGuiKey_E)) sTool = kEraser;
         if (key(ImGuiKey_G)) sTool = kFill;
         if (key(ImGuiKey_I)) sTool = kPick;
         if (key(ImGuiKey_M)) sTool = kMarquee;
         if (key(ImGuiKey_L)) sTool = kLasso;
         if (key(ImGuiKey_W)) sTool = kWand;
         if (key(ImGuiKey_P)) sTool = kPen;
         if (key(ImGuiKey_A)) sTool = kNode;
         if (key(ImGuiKey_U)) sTool = kShape;
         if (key(ImGuiKey_T)) sTool = kText;
      }
      if (HandleVectorKeys(doc))
         return;
      HandlePixelKeys(doc, cmd, shift);
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
      if (key(ImGuiKey_Escape) && sOp.kind == 0 && sPoly.empty() && !PixelTool(sTool))
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
      if (cmd && key(ImGuiKey_A) && PixelTool(sTool))
         doc->SelectAll();
      else if (cmd && key(ImGuiKey_A))
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

   // ---- paint tools -------------------------------------------------------

   void InitBrushes()
   {
      if (sBrushInit)
         return;
      sBrushInit = true;
      const auto& presets = BrushEngine::Presets();
      for (int i = 0; i < (int)presets.size(); i++)
      {
         if (sBrushPreset < 0 && presets[i].brush.mode == BrushEngine::kNormal && presets[i].brush.tip == BrushEngine::kSoftRound)
            sBrushPreset = i;
         if (sEraserPreset < 0 && presets[i].brush.mode == BrushEngine::kErase)
            sEraserPreset = i;
      }
      if (sBrushPreset >= 0)
         sBrush = presets[sBrushPreset].brush;
      if (sEraserPreset >= 0)
         sEraser = presets[sEraserPreset].brush;
      sEraser.mode = BrushEngine::kErase;
   }

   BrushEngine::Brush& ToolBrush() { return sTool == kEraser ? sEraser : sBrush; }

   float PenNow()
   {
      const float p = Platform::PenPressure(glfwGetCurrentContext());
      return p < 0.0f ? 1.0f : std::max(0.02f, p);
   }

   int SelOpFor(const ImGuiIO& io)
   {
      if (io.KeyShift && io.KeyAlt)
         return Scene2DNode::kSelIntersect;
      if (io.KeyShift)
         return Scene2DNode::kSelAdd;
      if (io.KeyAlt)
         return Scene2DNode::kSelSub;
      return sSelOp;
   }

   // What a paint tool paints on: 2 the quick mask, 1 the active layer's pixel
   // mask, 0 a paint layer (one is made above the active layer when it is not).
   // Returns the layer id, 0 when nothing can be painted.
   int PaintTarget(Scene2DNode* doc, int& target, bool make)
   {
      if (doc->quickMask)
      {
         target = 2;
         return -1;
      }
      Layer* a = Active(doc);
      if (sEditMask && a != nullptr && a->HasPixelMask())
      {
         target = 1;
         return a->id;
      }
      target = 0;
      if (a != nullptr && a->kind == Kind::Paint)
         return a->id;
      if (!make)
         return 0;
      const int id = doc->AddPaintLayer(a != nullptr && a->kind != Kind::Group ? a->id : 0);
      SelectOnly(id);
      return id;
   }

   void BeginStroke(Scene2DNode* doc, ImVec2 at, bool line)
   {
      PushUndoCheckpoint(sTool == kEraser ? "Erase" : "Paint");
      int target = 0;
      const int id = PaintTarget(doc, target, sTool == kBrush);
      if (id == 0)
         return;
      BrushEngine::Brush b = ToolBrush();
      std::copy(sFg, sFg + 3, b.color);
      const ImVec2 from = line && sHaveLast ? sLastPaint : at;
      if (!doc->BeginPaint(id, target, b, from.x, from.y, PenNow()))
         return;
      sOp = Op();
      sOp.kind = 7;
      sOp.docStart = at;
      if (line && sHaveLast)
         doc->PaintTo(at.x, at.y, PenNow());
   }

   void EndStroke(Scene2DNode* doc)
   {
      if (doc->Painting())
         doc->EndPaint();
      sLastPaint = sMouseDoc;
      sHaveLast = true;
   }

   void FillAt(Scene2DNode* doc, ImVec2 at)
   {
      PushUndoCheckpoint("Fill");
      int target = 0;
      const int id = PaintTarget(doc, target, true);
      if (id > 0 && target == 0)
         doc->Bucket(id, at.x, at.y, sFg, 1.0f, sTolerance, sContiguous, sAllLayers);
   }

   void GradientTo(Scene2DNode* doc, ImVec2 a, ImVec2 b)
   {
      PushUndoCheckpoint("Gradient");
      int target = 0;
      const int id = PaintTarget(doc, target, true);
      const float ca[4] = { sFg[0], sFg[1], sFg[2], 1.0f }, cb[4] = { sBg[0], sBg[1], sBg[2], 1.0f };
      if (id > 0 && target == 0)
         doc->Gradient(id, a.x, a.y, b.x, b.y, ca, cb, sFillKind == 2);
   }

   void ClosePoly(Scene2DNode* doc)
   {
      if (sPoly.size() >= 6)
         doc->SelectPolygon(sPoly, sPolyOp, sFeather);
      sPoly.clear();
   }

   void SwapColours()
   {
      for (int i = 0; i < 3; i++)
         std::swap(sFg[i], sBg[i]);
   }

   void DefaultColours()
   {
      sFg[0] = sFg[1] = sFg[2] = 0.0f;
      sBg[0] = sBg[1] = sBg[2] = 1.0f;
   }

   // The brush outline at its real size, and the hardness inside it.
   void DrawBrushCursor(ImDrawList* dl, ImVec2 mouse)
   {
      const BrushEngine::Brush& b = ToolBrush();
      const float r = b.size * 0.5f * sZoom;
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      auto ring = [&](float rr, float alpha)
      {
         dl->AddCircle(mouse, rr, IM_COL32(0, 0, 0, (int)(160 * alpha)), 48, 2.5f);
         dl->AddCircle(mouse, rr, IM_COL32(255, 255, 255, (int)(230 * alpha)), 48, 1.0f);
      };
      if (r < 2.0f)
      {
         dl->AddLine(ImVec2(mouse.x - 6, mouse.y), ImVec2(mouse.x + 6, mouse.y), IM_COL32(0, 0, 0, 200), 2.5f);
         dl->AddLine(ImVec2(mouse.x, mouse.y - 6), ImVec2(mouse.x, mouse.y + 6), IM_COL32(0, 0, 0, 200), 2.5f);
         dl->AddLine(ImVec2(mouse.x - 6, mouse.y), ImVec2(mouse.x + 6, mouse.y), IM_COL32(255, 255, 255, 255));
         dl->AddLine(ImVec2(mouse.x, mouse.y - 6), ImVec2(mouse.x, mouse.y + 6), IM_COL32(255, 255, 255, 255));
         return;
      }
      ring(r, 1.0f);
      if (!b.pencil && b.hardness < 0.95f && r * b.hardness > 3.0f)
         ring(r * b.hardness, 0.45f);
   }

   // Keys for the pixel tools. Returns true when it used the key.
   void HandlePixelKeys(Scene2DNode* doc, bool cmd, bool shift)
   {
      auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      auto rep = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
      if (!cmd)
      {
         if (key(ImGuiKey_Q)) doc->quickMask = !doc->quickMask;
         if (key(ImGuiKey_X)) SwapColours();
         if (key(ImGuiKey_D)) DefaultColours();
         if (sTool == kBrush || sTool == kEraser)
         {
            BrushEngine::Brush& b = ToolBrush();
            if (!shift && rep(ImGuiKey_LeftBracket)) b.size = std::max(1.0f, b.size * 0.9f - 1.0f);
            if (!shift && rep(ImGuiKey_RightBracket)) b.size = std::min(2000.0f, b.size * 1.1f + 1.0f);
            if (shift && rep(ImGuiKey_LeftBracket)) b.hardness = std::max(0.0f, b.hardness - 0.25f);
            if (shift && rep(ImGuiKey_RightBracket)) b.hardness = std::min(1.0f, b.hardness + 0.25f);
            static const ImGuiKey kDigits[10] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
                                                  ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9, ImGuiKey_0 };
            for (int i = 0; i < 10; i++)
               if (key(kDigits[i]))
                  b.opacity = (i + 1) / 10.0f;
         }
      }
      if (cmd && !shift && key(ImGuiKey_D)) doc->Deselect();
      if (cmd && shift && key(ImGuiKey_I)) doc->InvertSelection();
      if (sTool == kLasso && !sPoly.empty())
      {
         if (key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter)) ClosePoly(doc);
         if (key(ImGuiKey_Escape)) sPoly.clear();
      }
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
      if (l == nullptr || l->kind == Kind::Group || l->kind == Kind::Fill)
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
      if (l == nullptr || l->kind == Kind::Group || l->kind == Kind::Fill)
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

   // ---- vector and text tools ----------------------------------------------

   using Shape = Scene2DNode::Shape;
   Shape sStyle;                   // what the next drawn item gets
   int sShapeKind = 0;             // 0 rectangle, 1 rounded, 2 ellipse, 3 polygon, 4 star, 5 line
   int sVecTarget = 0;             // 0 the layer's items, 1 its vector mask
   int sItem = -1, sItemLayer = 0; // the picked item and the layer it is in
   int sPenItem = -1;              // the open path the pen adds to
   int sAnchor = -1, sPart = 0;    // node tool: 0 anchor, 1 in handle, 2 out handle, 3 box corner, 4 the whole item
   Shape sItem0;                   // the item as the drag began
   std::string sFont;
   float sTextSize = 0.0f, sLeading = 1.2f, sTracking = 0.0f;
   int sTextAlign = 0;
   float sTextC[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
   int sTextEdit = 0;              // the text layer being typed into
   bool sTextFocus = false;
   char sTextBuf[4096] = {};
   ImVec2 sEditMn(0, 0), sEditMx(0, 0);

   bool VectorTool(int t) { return t == kPen || t == kNode || t == kShape; }

   std::vector<Shape>* Items(Layer* l)
   {
      if (l == nullptr)
         return nullptr;
      if (sVecTarget == 1 && l->hasVMask)
         return &l->vmask;
      return l->kind == Kind::Vector ? &l->shapes : nullptr;
   }

   Shape* PickedItem(Scene2DNode* doc)
   {
      Layer* a = Active(doc);
      std::vector<Shape>* items = Items(a);
      if (items == nullptr || a->id != sItemLayer || sItem < 0 || sItem >= (int)items->size())
         return nullptr;
      return &(*items)[sItem];
   }

   // Where drawing goes: the active vector layer (or the mask being drawn), else a new vector layer over it.
   Layer* DrawTarget(Scene2DNode* doc)
   {
      Layer* a = Active(doc);
      if (Items(a) != nullptr)
         return a;
      const int id = doc->AddVectorLayer(a != nullptr && a->kind != Kind::Group ? a->id : 0);
      SelectOnly(id);
      sVecTarget = 0;
      sItem = -1;
      return doc->FindLayer(id);
   }

   ImVec2 BoxPt(Scene2DNode* doc, const Layer& l, ImVec2 p)
   {
      float bx = 0, by = 0;
      doc->DocToBox(l, p.x, p.y, bx, by);
      return ImVec2(bx, by);
   }

   ImVec2 BoxScreen(Scene2DNode* doc, const Layer& l, float bx, float by)
   {
      float x = 0, y = 0;
      doc->BoxToDoc(l, bx, by, x, y);
      return ToScreen(doc, x, y);
   }

   bool VectorBounds(const Layer& l, float lo[2], float hi[2])
   {
      lo[0] = lo[1] = 1e30f;
      hi[0] = hi[1] = -1e30f;
      for (const Shape& s : l.shapes)
      {
         float a[2], b[2];
         if (!Scene2DNode::ShapeBounds(s, a, b))
            continue;
         lo[0] = std::min(lo[0], a[0]);
         lo[1] = std::min(lo[1], a[1]);
         hi[0] = std::max(hi[0], b[0]);
         hi[1] = std::max(hi[1], b[1]);
      }
      return lo[0] <= hi[0];
   }

   void CopyStyle(const Shape& from, Shape& to)
   {
      to.fillOn = from.fillOn;
      to.strokeOn = from.strokeOn;
      std::copy(from.fillC, from.fillC + 4, to.fillC);
      std::copy(from.strokeC, from.strokeC + 4, to.strokeC);
      to.width = from.width;
      to.cap = from.cap;
      to.join = from.join;
      to.align = from.align;
      to.dash = from.dash;
      to.gap = from.gap;
      to.op = from.op;
   }

   // Shape tool: the item appears once the drag has moved, so a stray click leaves nothing behind.
   void DragShape(Scene2DNode* doc, ImVec2 mouse)
   {
      const ImGuiIO& io = ImGui::GetIO();
      if (!sOp.made)
      {
         if (std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) < 3.0f)
            return;
         PushUndoCheckpoint("Draw shape");
         Layer* l = DrawTarget(doc);
         std::vector<Shape>* items = Items(l);
         if (items == nullptr)
            return;
         static const int kTypes[6] = { Shape::kRect, Shape::kRect, Shape::kEllipse, Shape::kPolygon, Shape::kStar, Shape::kLine };
         Shape s = sStyle;
         s.type = kTypes[std::clamp(sShapeKind, 0, 5)];
         s.radius = sShapeKind == 1 ? std::max(1.0f, sStyle.radius) : 0.0f;
         s.pts.clear();
         s.svg.clear();
         s.closed = false;
         items->push_back(s);
         sItem = (int)items->size() - 1;
         sItemLayer = l->id;
         sOp.made = true;
      }
      Layer* l = Active(doc);
      Shape* s = PickedItem(doc);
      if (l == nullptr || s == nullptr)
         return;
      const ImVec2 a = BoxPt(doc, *l, sOp.docStart), b = BoxPt(doc, *l, sMouseDoc);
      float dx = b.x - a.x, dy = b.y - a.y;
      if (io.KeyShift)
      {
         if (s->type == Shape::kLine)
         {
            const float ang = std::round(std::atan2(dy, dx) / (kPi * 0.25f)) * (kPi * 0.25f), len = std::hypot(dx, dy);
            dx = std::cos(ang) * len;
            dy = std::sin(ang) * len;
         }
         else
         {
            const float m = std::max(std::fabs(dx), std::fabs(dy));
            dx = dx < 0 ? -m : m;
            dy = dy < 0 ? -m : m;
         }
      }
      const bool centre = io.KeyAlt && s->type != Shape::kLine;
      s->x0 = centre ? a.x - dx : a.x;
      s->y0 = centre ? a.y - dy : a.y;
      s->x1 = a.x + dx;
      s->y1 = a.y + dy;
      Touch();
   }

   void FinishPen(Scene2DNode* doc)
   {
      Layer* a = Active(doc);
      std::vector<Shape>* items = Items(a);
      if (items != nullptr && a->id == sItemLayer && sPenItem >= 0 && sPenItem < (int)items->size() && (*items)[sPenItem].Anchors() < 2)
      {
         items->erase(items->begin() + sPenItem);
         sItem = -1;
         Touch();
      }
      sPenItem = -1;
   }

   // Pen: a click adds a corner, a drag pulls out smooth handles, a click on the first anchor closes the path.
   void PenClick(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      std::vector<Shape>* items = Items(l);
      Shape* open = nullptr;
      if (items != nullptr && l->id == sItemLayer && sPenItem >= 0 && sPenItem < (int)items->size() &&
          (*items)[sPenItem].type == Shape::kPath && !(*items)[sPenItem].closed)
         open = &(*items)[sPenItem];
      if (open == nullptr)
      {
         PushUndoCheckpoint("Pen path");
         l = DrawTarget(doc);
         items = Items(l);
         if (items == nullptr)
            return;
         Shape s = sStyle;
         s.type = Shape::kPath;
         s.pts.clear();
         s.svg.clear();
         s.closed = false;
         items->push_back(s);
         sPenItem = sItem = (int)items->size() - 1;
         sItemLayer = l->id;
         open = &items->back();
      }
      else
         PushUndoCheckpoint("Pen point");
      sOp = Op();
      sOp.kind = 13;
      sOp.start = mouse;
      if (open->Anchors() >= 2)
      {
         const ImVec2 f = BoxScreen(doc, *l, open->pts[0], open->pts[1]);
         if (std::hypot(mouse.x - f.x, mouse.y - f.y) < 8.0f)
         {
            open->closed = true;
            sAnchor = 0;
            Touch();
            return;
         }
      }
      const ImVec2 b = BoxPt(doc, *l, sMouseDoc);
      open->pts.insert(open->pts.end(), { b.x, b.y, b.x, b.y, b.x, b.y });
      sAnchor = open->Anchors() - 1;
      Touch();
   }

   void PenDrag(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      std::vector<Shape>* items = Items(l);
      if (items == nullptr || sPenItem < 0 || sPenItem >= (int)items->size())
         return;
      Shape& s = (*items)[sPenItem];
      if (sAnchor < 0 || sAnchor >= s.Anchors() || std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) < 2.0f)
         return;
      float* p = &s.pts[(size_t)sAnchor * 6];
      const ImVec2 b = BoxPt(doc, *l, sMouseDoc);
      p[4] = b.x;
      p[5] = b.y;
      p[2] = 2.0f * p[0] - b.x;
      p[3] = 2.0f * p[1] - b.y;
      Touch();
   }

   // The item under the pointer: the picked item's anchors, handles and corners first, then any item's body, top first.
   int HitItem(Scene2DNode* doc, const Layer& l, const std::vector<Shape>& items, ImVec2 mouse, int& anchor, int& part)
   {
      auto near = [&](float bx, float by) {
         const ImVec2 q = BoxScreen(doc, l, bx, by);
         return std::hypot(mouse.x - q.x, mouse.y - q.y) < 6.0f;
      };
      if (l.id == sItemLayer && sItem >= 0 && sItem < (int)items.size())
      {
         const Shape& s = items[sItem];
         if (s.type == Shape::kPath)
         {
            for (int i = 0; i < s.Anchors(); i++)
               for (int k = 0; k < 3; k++)
                  if (near(s.pts[(size_t)i * 6 + k * 2], s.pts[(size_t)i * 6 + k * 2 + 1]))
                  {
                     anchor = i;
                     part = k;
                     return sItem;
                  }
         }
         else
         {
            if (near(s.x0, s.y0) || near(s.x1, s.y1))
            {
               anchor = near(s.x0, s.y0) ? 0 : 1;
               part = 3;
               return sItem;
            }
         }
      }
      const ImVec2 b = BoxPt(doc, l, ToDoc(doc, mouse));
      const float pad = 4.0f / std::max(0.01f, sZoom);
      for (int i = (int)items.size() - 1; i >= 0; i--)
      {
         float lo[2], hi[2];
         if (Scene2DNode::ShapeBounds(items[i], lo, hi) && b.x >= lo[0] - pad && b.x <= hi[0] + pad && b.y >= lo[1] - pad && b.y <= hi[1] + pad)
         {
            anchor = -1;
            part = 4;
            return i;
         }
      }
      return -1;
   }

   void NodeClick(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      std::vector<Shape>* items = Items(l);
      int anchor = -1, part = 0;
      int hit = items != nullptr ? HitItem(doc, *l, *items, mouse, anchor, part) : -1;
      if (hit < 0)
      {
         // Another vector layer under the pointer becomes the one edited.
         Layer* o = doc->FindLayer(HitLayer(doc, sMouseDoc));
         if (o != nullptr && o->kind == Kind::Vector && o != l)
         {
            SelectOnly(o->id);
            sVecTarget = 0;
            l = o;
            items = Items(l);
            hit = HitItem(doc, *l, *items, mouse, anchor, part);
         }
      }
      if (hit < 0)
      {
         sItem = -1;
         return;
      }
      sItem = hit;
      sItemLayer = l->id;
      Shape& s = (*items)[hit];
      if (ImGui::GetIO().KeyAlt && part == 0 && s.type == Shape::kPath)
      {
         // Alt-click on an anchor makes it a corner.
         PushUndoCheckpoint("Corner point");
         float* p = &s.pts[(size_t)anchor * 6];
         p[2] = p[4] = p[0];
         p[3] = p[5] = p[1];
         Touch();
         return;
      }
      PushUndoCheckpoint(part == 4 ? "Move shape" : "Edit path");
      sOp = Op();
      sOp.kind = 14;
      sOp.start = mouse;
      sOp.docStart = sMouseDoc;
      sAnchor = anchor;
      sPart = part;
      sItem0 = s;
   }

   void NodeDrag(Scene2DNode* doc)
   {
      Layer* l = Active(doc);
      Shape* s = PickedItem(doc);
      if (l == nullptr || s == nullptr)
         return;
      const ImVec2 a = BoxPt(doc, *l, sOp.docStart), b = BoxPt(doc, *l, sMouseDoc);
      const float dx = b.x - a.x, dy = b.y - a.y;
      *s = sItem0;
      if (sPart == 4)
      {
         s->x0 += dx;
         s->x1 += dx;
         s->y0 += dy;
         s->y1 += dy;
         for (size_t i = 0; i < s->pts.size(); i++)
            s->pts[i] += (i % 2) == 0 ? dx : dy;
      }
      else if (sPart == 3)
      {
         (sAnchor == 0 ? s->x0 : s->x1) += dx;
         (sAnchor == 0 ? s->y0 : s->y1) += dy;
      }
      else if (sAnchor >= 0 && sAnchor < s->Anchors())
      {
         float* p = &s->pts[(size_t)sAnchor * 6];
         if (sPart == 0)
            for (int k = 0; k < 6; k++)
               p[k] += (k % 2) == 0 ? dx : dy;
         else
         {
            const int h = sPart == 1 ? 2 : 4, o = sPart == 1 ? 4 : 2;
            p[h] += dx;
            p[h + 1] += dy;
            // The opposite handle turns with it, keeping its length; Alt breaks the pair.
            const float ol = std::hypot(p[o] - p[0], p[o + 1] - p[1]), hl = std::hypot(p[h] - p[0], p[h + 1] - p[1]);
            if (!ImGui::GetIO().KeyAlt && ol > 1e-3f && hl > 1e-3f)
            {
               p[o] = p[0] - (p[h] - p[0]) / hl * ol;
               p[o + 1] = p[1] - (p[h + 1] - p[1]) / hl * ol;
            }
         }
      }
      Touch();
   }

   void DrawVectorOverlay(ImDrawList* dl, Scene2DNode* doc, ImVec2 mouse, bool hovered)
   {
      Layer* l = Active(doc);
      std::vector<Shape>* items = Items(l);
      if (items == nullptr)
         return;
      std::vector<std::vector<float>> lines;
      const ImU32 white = IM_COL32(255, 255, 255, 255);
      for (int i = 0; i < (int)items->size(); i++)
      {
         const Shape& s = (*items)[i];
         const bool picked = i == sItem && l->id == sItemLayer;
         Scene2DNode::Outline(s, lines);
         for (const auto& ln : lines)
         {
            std::vector<ImVec2> q;
            for (size_t k = 0; k + 1 < ln.size(); k += 2)
               q.push_back(BoxScreen(doc, *l, ln[k], ln[k + 1]));
            if (q.size() >= 2)
               dl->AddPolyline(q.data(), (int)q.size(), Hex(kOutline, picked ? 1.0f : 0.45f), ImDrawFlags_None, picked ? 1.5f : 1.0f);
         }
         if (!picked || sTool == kShape)
            continue;
         auto dot = [&](float bx, float by, bool square) {
            const ImVec2 p = BoxScreen(doc, *l, bx, by);
            if (square)
            {
               dl->AddRectFilled(ImVec2(p.x - 3.5f, p.y - 3.5f), ImVec2(p.x + 3.5f, p.y + 3.5f), white);
               dl->AddRect(ImVec2(p.x - 3.5f, p.y - 3.5f), ImVec2(p.x + 3.5f, p.y + 3.5f), Hex(kOutline));
            }
            else
            {
               dl->AddCircleFilled(p, 3.0f, Hex(kOutline));
            }
         };
         if (s.type == Shape::kPath)
            for (int a = 0; a < s.Anchors(); a++)
            {
               const float* p = &s.pts[(size_t)a * 6];
               for (int k = 1; k < 3; k++)
                  if (std::fabs(p[k * 2] - p[0]) + std::fabs(p[k * 2 + 1] - p[1]) > 0.01f)
                  {
                     dl->AddLine(BoxScreen(doc, *l, p[0], p[1]), BoxScreen(doc, *l, p[k * 2], p[k * 2 + 1]), Hex(kOutline, 0.8f));
                     dot(p[k * 2], p[k * 2 + 1], false);
                  }
               dot(p[0], p[1], true);
            }
         else
         {
            dot(s.x0, s.y0, true);
            dot(s.x1, s.y1, true);
         }
      }
      // The pen's next segment follows the pointer.
      if (sTool == kPen && hovered && sOp.kind == 0 && l->id == sItemLayer && sPenItem >= 0 && sPenItem < (int)items->size())
      {
         const Shape& s = (*items)[sPenItem];
         if (s.Anchors() > 0 && !s.closed)
         {
            const float* p = &s.pts[(size_t)(s.Anchors() - 1) * 6];
            dl->AddLine(BoxScreen(doc, *l, p[0], p[1]), mouse, Hex(kOutline, 0.7f), 1.0f);
         }
      }
   }

   // ---- text -------------------------------------------------------------

   void EndTextEdit(Scene2DNode* doc)
   {
      Layer* l = doc->FindLayer(sTextEdit);
      sTextEdit = 0;
      if (l != nullptr && l->kind == Kind::Text && l->text.empty())
      {
         doc->RemoveLayer(l->id);
         PruneSelection(doc);
      }
   }

   void BeginTextEdit(Scene2DNode* doc, int id)
   {
      Layer* l = doc->FindLayer(id);
      if (l == nullptr || l->kind != Kind::Text)
         return;
      if (sTextEdit != 0 && sTextEdit != id)
         EndTextEdit(doc);
      sTextEdit = id;
      sTextFocus = true;
      std::snprintf(sTextBuf, sizeof(sTextBuf), "%s", l->text.c_str());
      SelectOnly(id);
   }

   void TextClick(Scene2DNode* doc, ImVec2 mouse)
   {
      if (sTextEdit != 0)
         EndTextEdit(doc);
      Layer* hit = doc->FindLayer(HitLayer(doc, sMouseDoc));
      if (hit != nullptr && hit->kind == Kind::Text)
      {
         PushUndoCheckpoint("Edit text");
         BeginTextEdit(doc, hit->id);
         return;
      }
      sOp = Op();
      sOp.kind = 15;
      sOp.start = mouse;
      sOp.docStart = sMouseDoc;
   }

   // A click places point text; a drag sets a box the text wraps in.
   void TextRelease(Scene2DNode* doc, ImVec2 mouse)
   {
      PushUndoCheckpoint("New text");
      const bool box = std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) > 6.0f;
      const float x = box ? std::min(sOp.docStart.x, sMouseDoc.x) : sOp.docStart.x;
      const float y = box ? std::min(sOp.docStart.y, sMouseDoc.y) : sOp.docStart.y;
      Layer* a = Active(doc);
      const int id = doc->AddTextLayer("", x, y, box ? std::fabs(sMouseDoc.x - sOp.docStart.x) : 0.0f,
                                       a != nullptr && a->kind != Kind::Group ? a->id : 0);
      if (Layer* l = doc->FindLayer(id))
      {
         l->font = sFont;
         if (sTextSize > 0.0f)
            l->fontSize = sTextSize;
         l->leading = sLeading;
         l->tracking = sTracking;
         l->align = sTextAlign;
         std::copy(sTextC, sTextC + 4, l->textC);
         doc->Commit();
      }
      BeginTextEdit(doc, id);
   }

   // Typing happens in a field under the text; the picture above updates as you type.
   void DrawTextEditor(Scene2DNode* doc)
   {
      Layer* l = doc->FindLayer(sTextEdit);
      if (l == nullptr || l->kind != Kind::Text)
      {
         sTextEdit = 0;
         return;
      }
      float bw = 1, bh = 1;
      doc->LayerBox(*l, bw, bh);
      ImVec2 c[4] = { BoxScreen(doc, *l, 0, 0), BoxScreen(doc, *l, bw, 0), BoxScreen(doc, *l, bw, bh), BoxScreen(doc, *l, 0, bh) };
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddPolyline(c, 4, Hex(kOutline), ImDrawFlags_Closed, 1.0f);
      const float w = std::clamp(bw * sZoom, 220.0f, 520.0f);
      const float h = ImGui::GetTextLineHeight() * 4.0f + 2.0f * ImGui::GetStyle().FramePadding.y;
      ImVec2 at(std::min(c[0].x, c[3].x), std::max(c[2].y, c[3].y) + 6.0f);
      at.x = std::clamp(at.x, sViewMn.x + 4.0f, std::max(sViewMn.x + 4.0f, sViewMx.x - w - 4.0f));
      at.y = std::clamp(at.y, sViewMn.y + 4.0f, std::max(sViewMn.y + 4.0f, sViewMx.y - h - 4.0f));
      const ImVec2 keep = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(at);
      ImGui::PushID(sTextEdit);
      if (sTextFocus)
      {
         ImGui::SetKeyboardFocusHere();
         sTextFocus = false;
      }
      FieldWell::PushStyle();
      const bool changed = ImGui::InputTextMultiline("##v2dtextedit", sTextBuf, sizeof(sTextBuf), ImVec2(w, h));
      FieldWell::PopStyle();
      sEditMn = ImGui::GetItemRectMin();
      sEditMx = ImGui::GetItemRectMax();
      const bool done = ImGui::IsItemDeactivated();
      ImGui::PopID();
      ImGui::SetCursorScreenPos(keep);
      if (changed)
      {
         l->text = sTextBuf;
         Touch();
      }
      if (done)
         EndTextEdit(doc);
   }

   // Pen: Esc/Enter finishes the path. Node: Delete removes the picked item.
   bool HandleVectorKeys(Scene2DNode* doc)
   {
      auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      if (sTool == kPen && sPenItem >= 0 && (key(ImGuiKey_Escape) || key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter)))
      {
         FinishPen(doc);
         return true;
      }
      if (sTool == kNode && (key(ImGuiKey_Delete) || key(ImGuiKey_Backspace)))
      {
         std::vector<Shape>* items = Items(Active(doc));
         if (PickedItem(doc) != nullptr && items != nullptr)
         {
            PushUndoCheckpoint("Delete shape");
            items->erase(items->begin() + sItem);
            sItem = -1;
            Touch();
            return true;
         }
      }
      return false;
   }

   void DrawCanvas(Scene2DNode* doc, ImVec2 size)
   {
      ImGuiIO& io = ImGui::GetIO();
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##view2dcanvas", size,
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
      const ImVec2 mp = ImGui::GetIO().MousePos;
      const bool inEditor = sTextEdit != 0 && mp.x >= sEditMn.x && mp.x <= sEditMx.x && mp.y >= sEditMn.y && mp.y <= sEditMx.y;
      const bool hovered = ImGui::IsItemHovered() && !inEditor;
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
      // The selection's marching ants, or the red quick mask, over the picture.
      if (doc->HasSelection() || doc->quickMask)
         if (const unsigned ants = doc->SelectionOverlay((float)ImGui::GetTime(), doc->quickMask))
            dl->AddImage((ImTextureID)(intptr_t)ants, a, b, ImVec2(0, 1), ImVec2(1, 0));
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
      const bool canTransform = sTool == kMove && active != nullptr && active->kind != Kind::Group && active->kind != Kind::Fill && sSel.size() == 1 &&
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
               if (l->kind == Kind::Fill)
                  continue;
               if (l->kind == Kind::Vector)
               {
                  float lo[2], hi[2];
                  if (VectorBounds(*l, lo, hi))
                  {
                     ImVec2 q[4] = { BoxScreen(doc, *l, lo[0], lo[1]), BoxScreen(doc, *l, hi[0], lo[1]), BoxScreen(doc, *l, hi[0], hi[1]),
                                     BoxScreen(doc, *l, lo[0], hi[1]) };
                     dl->AddPolyline(q, 4, Hex(kOutline), ImDrawFlags_Closed, 1.0f);
                  }
                  continue;
               }
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
      if (VectorTool(sTool))
         DrawVectorOverlay(dl, doc, mouse, hovered);
      if (sOp.kind == 15)
      {
         const ImVec2 p0 = ToScreen(doc, sOp.docStart.x, sOp.docStart.y);
         if (std::hypot(mouse.x - p0.x, mouse.y - p0.y) > 6.0f)
            dl->AddRect(ImVec2(std::min(p0.x, mouse.x), std::min(p0.y, mouse.y)), ImVec2(std::max(p0.x, mouse.x), std::max(p0.y, mouse.y)), Hex(kOutline));
      }
      if (sOp.kind == 1)
      {
         for (float x : sSnapX)
            dl->AddLine(ToScreen(doc, x, -1e5f), ToScreen(doc, x, 1e5f), Hex(kSmart));
         for (float y : sSnapY)
            dl->AddLine(ToScreen(doc, -1e5f, y), ToScreen(doc, 1e5f, y), Hex(kSmart));
      }
      // Shapes being dragged out by the pixel tools.
      const ImU32 ink = IM_COL32(255, 255, 255, 230), shade = IM_COL32(0, 0, 0, 160);
      if (sOp.kind == 9)
      {
         const ImVec2 p0 = ToScreen(doc, sOp.docStart.x, sOp.docStart.y), p1 = mouse;
         if (sMarqueeShape == 1)
         {
            const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const ImVec2 rr(std::fabs(p1.x - p0.x) * 0.5f, std::fabs(p1.y - p0.y) * 0.5f);
            dl->AddEllipse(c, rr, shade, 0.0f, 64, 2.5f);
            dl->AddEllipse(c, rr, ink, 0.0f, 64, 1.0f);
         }
         else
         {
            const ImVec2 lo(std::min(p0.x, p1.x), std::min(p0.y, p1.y)), hi(std::max(p0.x, p1.x), std::max(p0.y, p1.y));
            dl->AddRect(lo, hi, shade, 0.0f, 0, 2.5f);
            dl->AddRect(lo, hi, ink, 0.0f, 0, 1.0f);
         }
      }
      auto drawPath = [&](const std::vector<float>& pts, bool toMouse)
      {
         std::vector<ImVec2> q;
         for (size_t i = 0; i + 1 < pts.size(); i += 2)
            q.push_back(ToScreen(doc, pts[i], pts[i + 1]));
         if (toMouse)
            q.push_back(mouse);
         if (q.size() >= 2)
         {
            dl->AddPolyline(q.data(), (int)q.size(), shade, ImDrawFlags_None, 2.5f);
            dl->AddPolyline(q.data(), (int)q.size(), ink, ImDrawFlags_None, 1.0f);
         }
      };
      if (sOp.kind == 10)
         drawPath(sOp.path, false);
      if (!sPoly.empty())
         drawPath(sPoly, true);
      if (sOp.kind == 8)
      {
         const ImVec2 p0 = ToScreen(doc, sOp.docStart.x, sOp.docStart.y);
         dl->AddLine(p0, mouse, shade, 2.5f);
         dl->AddLine(p0, mouse, ink, 1.0f);
         dl->AddCircleFilled(p0, 3.0f, ink);
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
      if (hovered && !panTool && (sTool == kBrush || sTool == kEraser) && !io.KeyAlt && (sOp.kind == 0 || sOp.kind == 7))
      {
         dl->PushClipRect(sViewMn, sViewMx, true);
         DrawBrushCursor(dl, mouse);
         dl->PopClipRect();
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
         else if (sTool == kPick || ((sTool == kBrush || sTool == kFill) && io.KeyAlt))
         {
            // The eyedropper; Alt with the brush or bucket picks too. Alt on the eyedropper sets the background.
            sOp = Op();
            sOp.kind = 11;
            sOp.selOp = sTool == kPick && io.KeyAlt ? 1 : 0;
         }
         else if (sTool == kBrush || sTool == kEraser)
            BeginStroke(doc, sMouseDoc, io.KeyShift);
         else if (sTool == kFill && sFillKind == 0)
            FillAt(doc, sMouseDoc);
         else if (sTool == kFill)
         {
            sOp = Op();
            sOp.kind = 8;
            sOp.docStart = sMouseDoc;
         }
         else if (sTool == kMarquee)
         {
            sOp = Op();
            sOp.kind = 9;
            sOp.docStart = sMouseDoc;
            sOp.start = mouse;
            sOp.selOp = SelOpFor(io);
         }
         else if (sTool == kLasso && sLassoKind == 1)
         {
            // Polygon: each click adds a corner; a click on the first, a double-click or Enter closes it.
            if (sPoly.empty())
               sPolyOp = SelOpFor(io);
            const ImVec2 first = sPoly.empty() ? mouse : ToScreen(doc, sPoly[0], sPoly[1]);
            const bool closing = sPoly.size() >= 6 && (std::hypot(mouse.x - first.x, mouse.y - first.y) < 8.0f ||
                                                       ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
            if (closing)
               ClosePoly(doc);
            else
            {
               sPoly.push_back(sMouseDoc.x);
               sPoly.push_back(sMouseDoc.y);
            }
         }
         else if (sTool == kLasso)
         {
            sOp = Op();
            sOp.kind = 10;
            sOp.selOp = SelOpFor(io);
            sOp.path = { sMouseDoc.x, sMouseDoc.y };
            sOp.start = mouse;
         }
         else if (sTool == kWand)
         {
            Layer* al = Active(doc);
            doc->SelectWand(sMouseDoc.x, sMouseDoc.y, sTolerance, sContiguous, SelOpFor(io), sAllLayers, al != nullptr ? al->id : 0);
         }
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
         else if (sTool == kShape)
         {
            sOp = Op();
            sOp.kind = 12;
            sOp.start = mouse;
            sOp.docStart = sMouseDoc;
         }
         else if (sTool == kPen)
            PenClick(doc, mouse);
         else if (sTool == kNode)
            NodeClick(doc, mouse);
         else if (sTool == kText)
            TextClick(doc, mouse);
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
         else if (sOp.kind == 7)
            doc->PaintTo(sMouseDoc.x, sMouseDoc.y, PenNow());
         else if (sOp.kind == 10)
         {
            const ImVec2 last = ToScreen(doc, sOp.path[sOp.path.size() - 2], sOp.path.back());
            if (std::hypot(mouse.x - last.x, mouse.y - last.y) >= 2.0f)
            {
               sOp.path.push_back(sMouseDoc.x);
               sOp.path.push_back(sMouseDoc.y);
            }
         }
         else if (sOp.kind == 12)
            DragShape(doc, mouse);
         else if (sOp.kind == 13)
            PenDrag(doc, mouse);
         else if (sOp.kind == 14)
            NodeDrag(doc);
         else if (sOp.kind == 11)
            doc->Pick(sMouseDoc.x, sMouseDoc.y, sOp.selOp == 1 ? sBg : sFg);
         else if (sOp.kind == 6)
         {
            std::vector<float>& gs = sOp.guideAxis == 0 ? d.guidesX : d.guidesY;
            if (sOp.guideIndex >= 0 && sOp.guideIndex < (int)gs.size())
               gs[sOp.guideIndex] = std::round(sOp.guideAxis == 0 ? sMouseDoc.x : sMouseDoc.y);
            Touch();
         }
         if (!held)
         {
            if (sOp.kind == 7)
               EndStroke(doc);
            else if (sOp.kind == 13)
            {
               Layer* pl = Active(doc);
               std::vector<Shape>* items = Items(pl);
               if (items != nullptr && sPenItem >= 0 && sPenItem < (int)items->size() && (*items)[sPenItem].closed)
                  sPenItem = -1;
            }
            else if (sOp.kind == 15)
               TextRelease(doc, mouse);
            else if (sOp.kind == 8 && std::hypot(mouse.x - ToScreen(doc, sOp.docStart.x, sOp.docStart.y).x,
                                                 mouse.y - ToScreen(doc, sOp.docStart.x, sOp.docStart.y).y) > 2.0f)
               GradientTo(doc, sOp.docStart, sMouseDoc);
            else if (sOp.kind == 9)
            {
               // A click without a drag drops the selection, as in every image editor.
               if (std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) < 3.0f)
               {
                  if (sOp.selOp == Scene2DNode::kSelNew)
                     doc->Deselect();
               }
               else
                  doc->SelectRect(sOp.docStart.x, sOp.docStart.y, sMouseDoc.x, sMouseDoc.y, sOp.selOp, sFeather, sMarqueeShape == 1);
            }
            else if (sOp.kind == 10)
            {
               if (sOp.path.size() >= 6)
                  doc->SelectPolygon(sOp.path, sOp.selOp, sFeather);
               else if (sOp.selOp == Scene2DNode::kSelNew)
                  doc->Deselect();
            }
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
      if (sTextEdit != 0)
         DrawTextEditor(doc);
   }

   // ---- the pixel tools' context bar ---------------------------------------

   void BarSlider(const char* id, float* v, float lo, float hi, const char* fmt, float w = 104.0f)
   {
      ImGui::SetNextItemWidth(w);
      FieldWell::Slider(id, v, lo, hi, fmt);
      FormParts::Inline(0.0f, tok::space_1);
   }

   void Swatches()
   {
      const float d = 18.0f;
      FormParts::ColorDot("##v2dfg", sFg, d, tok::tile);
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Foreground. X swaps, D resets"));
      FormParts::Inline(0.0f, tok::space_1);
      FormParts::ColorDot("##v2dbg", sBg, d, tok::tile);
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Background"));
   }

   void SelOpChips(const char* idBase)
   {
      static const char* kOps[4] = { "New", "Add", "Subtract", "Intersect" };
      static const char* kTips[4] = { "New selection", "Add (Shift)", "Subtract (Alt)", "Intersect (Shift+Alt)" };
      for (int i = 0; i < 4; i++)
      {
         if (i > 0)
            FormParts::Inline(0.0f, tok::space_1);
         const std::string id = std::string(L(kOps[i])) + "##" + idBase + std::to_string(i);
         if (TextChip(id.c_str(), sSelOp == i, T(kTips[i])))
            sSelOp = i;
      }
   }

   void MatchChips()
   {
      float tol = (float)sTolerance;
      BarSlider("##v2dtol", &tol, 0.0f, 255.0f, "Tolerance %.0f", 120.0f);
      sTolerance = (int)std::lround(tol);
      if (TextChip(L("Contiguous##v2dcont"), sContiguous, T("Only touching pixels")))
         sContiguous = !sContiguous;
      FormParts::Inline(0.0f, tok::space_1);
      if (TextChip(L("All layers##v2dall"), sAllLayers, T("Sample the whole picture")))
         sAllLayers = !sAllLayers;
   }

   void DrawPixelBar(Scene2DNode* doc)
   {
      auto gap = []() { FormParts::Inline(0.0f, tok::space_3); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      Layer* a = Active(doc);
      if (sTool == kBrush || sTool == kEraser)
      {
         BrushEngine::Brush& b = ToolBrush();
         int& preset = sTool == kEraser ? sEraserPreset : sBrushPreset;
         const auto& presets = BrushEngine::Presets();
         ImGui::SetNextItemWidth(130.0f);
         const char* shown = preset >= 0 && preset < (int)presets.size() ? presets[preset].name : "";
         if (MenuParts::BeginCombo("##v2dpreset", T(shown)))
         {
            for (int i = 0; i < (int)presets.size(); i++)
            {
               const bool erase = presets[i].brush.mode == BrushEngine::kErase;
               if (erase != (sTool == kEraser))
                  continue;
               if (MenuParts::Choice(T(presets[i].name), i == preset))
               {
                  preset = i;
                  b = presets[i].brush;
                  if (sTool == kEraser)
                     b.mode = BrushEngine::kErase;
               }
            }
            MenuParts::EndCombo();
         }
         next();
         BarSlider("##v2dsize", &b.size, 1.0f, 1000.0f, "Size %.0f px", 112.0f);
         float hard = b.hardness * 100.0f, op = b.opacity * 100.0f, flow = b.flow * 100.0f;
         BarSlider("##v2dhard", &hard, 0.0f, 100.0f, "Hardness %.0f%%", 120.0f);
         BarSlider("##v2dop", &op, 1.0f, 100.0f, "Opacity %.0f%%", 112.0f);
         BarSlider("##v2dflow", &flow, 1.0f, 100.0f, "Flow %.0f%%", 96.0f);
         b.hardness = hard / 100.0f;
         b.opacity = op / 100.0f;
         b.flow = flow / 100.0f;
         if (TextChip(L("Pressure##v2dpress"), b.pressureSize, T("Pen pressure sets size")))
            b.pressureSize = !b.pressureSize;
         if (a != nullptr && a->HasPixelMask())
         {
            next();
            if (TextChip(L("Mask##v2deditmask"), sEditMask, T("Paint the layer's mask")))
               sEditMask = !sEditMask;
         }
      }
      else if (sTool == kFill)
      {
         static const char* kKinds[3] = { "Bucket", "Linear", "Radial" };
         for (int i = 0; i < 3; i++)
         {
            if (i > 0)
               next();
            const std::string id = std::string(L(kKinds[i])) + "##v2dfk" + std::to_string(i);
            if (TextChip(id.c_str(), sFillKind == i, i == 0 ? T("Click fills alike pixels") : T("Drag for the gradient")))
               sFillKind = i;
         }
         if (sFillKind == 0)
         {
            gap();
            MatchChips();
         }
      }
      else if (sTool == kMarquee || sTool == kLasso)
      {
         const bool m = sTool == kMarquee;
         int& kind = m ? sMarqueeShape : sLassoKind;
         if (TextChip(m ? L("Rectangle##v2dsh0") : L("Freehand##v2dsh0"), kind == 0, nullptr)) { kind = 0; sPoly.clear(); }
         next();
         if (TextChip(m ? L("Ellipse##v2dsh1") : L("Polygon##v2dsh1"), kind == 1, m ? nullptr : T("Click corners; Enter closes")))
            kind = 1;
         gap();
         SelOpChips("v2dop");
         gap();
         BarSlider("##v2dfeather", &sFeather, 0.0f, 250.0f, "Feather %.0f px", 120.0f);
      }
      else if (sTool == kWand)
      {
         SelOpChips("v2dwop");
         gap();
         MatchChips();
      }
      gap();
      Swatches();
      gap();
      if (TextChip(L("Quick mask##v2dqm"), doc->quickMask, T("Paint the selection (Q)")))
         doc->quickMask = !doc->quickMask;
   }

   // ---- the vector and text tools' context bars ----------------------------

   // Edits the picked item when there is one, and always what the next item gets.
   void DrawVectorBar(Scene2DNode* doc)
   {
      auto gap = []() { FormParts::Inline(0.0f, tok::space_3); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      Shape* picked = PickedItem(doc);
      Shape& t = picked != nullptr ? *picked : sStyle;
      bool changed = false;
      auto undo = [&]() { if (picked != nullptr) PushUndoCheckpoint("Shape style"); };
      if (sTool == kShape)
      {
         static const char* kKinds[6] = { "Rectangle", "Rounded", "Ellipse", "Polygon", "Star", "Line" };
         for (int i = 0; i < 6; i++)
         {
            if (i > 0)
               next();
            const std::string id = std::string(L(kKinds[i])) + "##v2dsk" + std::to_string(i);
            if (TextChip(id.c_str(), sShapeKind == i, nullptr))
               sShapeKind = i;
         }
         gap();
      }
      if (TextChip(L("Fill##v2dvfill"), t.fillOn, T("Fill on or off")))
      {
         undo();
         t.fillOn = !t.fillOn;
         changed = true;
      }
      next();
      if (FormParts::ColorDot("##v2dvfillc", t.fillC, 18.0f, tok::tile))
         changed = true;
      next();
      if (TextChip(L("Stroke##v2dvstroke"), t.strokeOn, T("Stroke on or off")))
      {
         undo();
         t.strokeOn = !t.strokeOn;
         changed = true;
      }
      next();
      if (FormParts::ColorDot("##v2dvstrokec", t.strokeC, 18.0f, tok::tile))
         changed = true;
      next();
      ImGui::SetNextItemWidth(96.0f);
      changed |= FieldWell::Slider("##v2dvwidth", &t.width, 0.0f, 200.0f, "%.0f px");
      if (ImGui::IsItemActivated())
         undo();
      next();
      auto pick = [&](const char* id, int* v, std::initializer_list<const char*> names, float w) {
         std::vector<const char*> n(names);
         ImGui::SetNextItemWidth(w);
         if (MenuParts::BeginCombo(id, T(n[std::clamp(*v, 0, (int)n.size() - 1)])))
         {
            for (int i = 0; i < (int)n.size(); i++)
               if (MenuParts::Choice(T(n[i]), i == *v) && i != *v)
               {
                  undo();
                  *v = i;
                  changed = true;
               }
            MenuParts::EndCombo();
         }
         next();
      };
      pick("##v2dvcap", &t.cap, { "Butt", "Round cap", "Square" }, 84.0f);
      pick("##v2dvjoin", &t.join, { "Miter", "Round join", "Bevel" }, 84.0f);
      pick("##v2dvalign", &t.align, { "Centre", "Inside", "Outside" }, 84.0f);
      ImGui::SetNextItemWidth(84.0f);
      changed |= FieldWell::Slider("##v2dvdash", &t.dash, 0.0f, 200.0f, t.dash > 0.0f ? "Dash %.0f" : "Solid");
      if (ImGui::IsItemActivated())
         undo();
      if (t.dash > 0.0f)
      {
         next();
         ImGui::SetNextItemWidth(76.0f);
         changed |= FieldWell::Slider("##v2dvgap", &t.gap, 0.0f, 200.0f, "Gap %.0f");
         if (ImGui::IsItemActivated())
            undo();
      }
      gap();
      static const char* kOps[4] = { "Add", "Subtract", "Intersect", "Exclude" };
      static const char* kOpTips[4] = { "Over what is below", "Cuts what is below", "Keeps only the overlap", "Keeps all but the overlap" };
      for (int i = 0; i < 4; i++)
      {
         if (i > 0)
            next();
         const std::string id = std::string(L(kOps[i])) + "##v2dvop" + std::to_string(i);
         if (TextChip(id.c_str(), t.op == i, T(kOpTips[i])) && t.op != i)
         {
            undo();
            t.op = i;
            changed = true;
         }
      }
      // The live shape's own numbers.
      const bool rect = picked != nullptr ? picked->type == Shape::kRect : sShapeKind <= 1;
      const bool poly = picked != nullptr ? (picked->type == Shape::kPolygon || picked->type == Shape::kStar) : (sShapeKind == 3 || sShapeKind == 4);
      const bool star = picked != nullptr ? picked->type == Shape::kStar : sShapeKind == 4;
      if (rect && (picked != nullptr || sTool == kShape))
      {
         gap();
         ImGui::SetNextItemWidth(96.0f);
         changed |= FieldWell::Slider("##v2dvrad", &t.radius, 0.0f, 500.0f, "Radius %.0f");
         if (ImGui::IsItemActivated())
            undo();
      }
      if (poly && (picked != nullptr || sTool == kShape))
      {
         gap();
         float sides = (float)t.sides;
         ImGui::SetNextItemWidth(84.0f);
         if (FieldWell::Slider("##v2dvsides", &sides, 3.0f, 64.0f, star ? "Points %.0f" : "Sides %.0f"))
         {
            t.sides = (int)std::lround(sides);
            changed = true;
         }
         if (ImGui::IsItemActivated())
            undo();
         if (star)
         {
            next();
            ImGui::SetNextItemWidth(84.0f);
            changed |= FieldWell::Slider("##v2dvinner", &t.inner, 0.05f, 1.0f, "Inner %.2f");
            if (ImGui::IsItemActivated())
               undo();
         }
      }
      Layer* a = Active(doc);
      if (a != nullptr && a->hasVMask)
      {
         gap();
         if (TextChip(L("Mask##v2dvmask"), sVecTarget == 1, T("Draw into the vector mask")))
         {
            sVecTarget = sVecTarget == 1 ? 0 : 1;
            sItem = -1;
            sPenItem = -1;
         }
      }
      if (changed)
      {
         if (picked != nullptr)
         {
            CopyStyle(*picked, sStyle);
            sStyle.radius = picked->radius;
            sStyle.sides = picked->sides;
            sStyle.inner = picked->inner;
         }
         Touch();
      }
   }

   void DrawTextBar(Scene2DNode* doc)
   {
      auto gap = []() { FormParts::Inline(0.0f, tok::space_3); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      Layer* a = Active(doc);
      Layer* l = a != nullptr && a->kind == Kind::Text ? a : nullptr;
      std::string& font = l != nullptr ? l->font : sFont;
      float autoSize = std::clamp(std::round(doc->Doc().h / 12.0f), 12.0f, 400.0f);
      float& size = l != nullptr ? l->fontSize : (sTextSize > 0.0f ? sTextSize : (sTextSize = autoSize));
      float& leading = l != nullptr ? l->leading : sLeading;
      float& tracking = l != nullptr ? l->tracking : sTracking;
      int& align = l != nullptr ? l->align : sTextAlign;
      float* colour = l != nullptr ? l->textC : sTextC;
      bool changed = false;
      auto undo = [&]() { if (l != nullptr) PushUndoCheckpoint("Text style"); };
      const auto& fonts = Platform::AvailableFontFamilies();
      ImGui::SetNextItemWidth(160.0f);
      const std::string shown = font.empty() ? (fonts.empty() ? std::string("Helvetica") : fonts.front()) : font;
      if (MenuParts::BeginCombo("##v2dfont", shown.c_str()))
      {
         for (const std::string& f : fonts)
            if (MenuParts::Choice(f.c_str(), f == shown) && f != shown)
            {
               undo();
               font = f;
               changed = true;
            }
         MenuParts::EndCombo();
      }
      next();
      ImGui::SetNextItemWidth(96.0f);
      changed |= FieldWell::Slider("##v2dfsize", &size, 4.0f, 800.0f, "%.0f px");
      if (ImGui::IsItemActivated())
         undo();
      next();
      ImGui::SetNextItemWidth(96.0f);
      changed |= FieldWell::Slider("##v2dlead", &leading, 0.5f, 3.0f, "Leading %.2f");
      if (ImGui::IsItemActivated())
         undo();
      next();
      ImGui::SetNextItemWidth(104.0f);
      changed |= FieldWell::Slider("##v2dtrack", &tracking, -100.0f, 400.0f, "Tracking %.0f");
      if (ImGui::IsItemActivated())
         undo();
      gap();
      static const char* kAligns[3] = { "Left", "Centre", "Right" };
      for (int i = 0; i < 3; i++)
      {
         if (i > 0)
            next();
         const std::string id = std::string(L(kAligns[i])) + "##v2dtal" + std::to_string(i);
         if (TextChip(id.c_str(), align == i, nullptr) && align != i)
         {
            undo();
            align = i;
            changed = true;
         }
      }
      gap();
      if (FormParts::ColorDot("##v2dtextc", colour, 18.0f, tok::tile))
         changed = true;
      if (changed && l != nullptr)
         Touch();
   }

   // ---- header and status ----------------------------------------------

   // The tools, down the left of the picture as in every image editor.
   void DrawToolStrip(float h)
   {
      ImGui::BeginChild("##v2dtools", ImVec2(kIconW, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, tok::space_1));
      struct { int tool; const char* id; const char* glyph; const char* tip; } kTools[15] = {
         { kMove, "##v2dmove", IconsInfinite::ToolMove, "Move (V)" },
         { kNode, "##v2dnode", IconsInfinite::Pointer, "Direct select (A). Alt-click an anchor makes a corner" },
         { kMarquee, "##v2dmarquee", IconsInfinite::Marquee, "Marquee (M)" },
         { kLasso, "##v2dlasso", IconsInfinite::Lasso, "Lasso (L)" },
         { kWand, "##v2dwand", IconsInfinite::Wand, "Magic wand (W)" },
         { kCrop, "##v2dcrop", IconsInfinite::Crop, "Crop (C). Enter applies, Esc cancels" },
         { kPick, "##v2dpick", IconsInfinite::Eyedropper, "Eyedropper (I). Alt sets the background" },
         { kBrush, "##v2dbrush", IconsInfinite::Brush, "Brush (B). Alt picks a colour, Shift-click draws a line" },
         { kEraser, "##v2derase", IconsInfinite::Erase, "Eraser (E)" },
         { kFill, "##v2dfilltool", IconsInfinite::Fill, "Fill and gradient (G)" },
         { kPen, "##v2dpen", IconsInfinite::PenTool, "Pen (P). Drag for curves, Enter ends" },
         { kShape, "##v2dshape", IconsInfinite::Shape, "Shape (U). Shift keeps it square, Alt from the centre" },
         { kText, "##v2dtext", IconsInfinite::Type, "Text (T). Click for a line, drag for a box" },
         { kHand, "##v2dhand", IconsInfinite::Hand, "Hand (H, or hold Space)" },
         { kZoom, "##v2dzoom", IconsInfinite::Zoom, "Zoom (Z). Alt-click zooms out" },
      };
      for (int i = 0; i < 15; i++)
      {
         // Gaps between the families: move, select, crop, paint, vector, view.
         if (i == 2 || i == 5 || i == 6 || i == 10 || i == 13)
            ImGui::Dummy(ImVec2(0.0f, tok::space_1));
         if (IconChip(kTools[i].id, kTools[i].glyph, sTool == kTools[i].tool, T(kTools[i].tip)))
            sTool = kTools[i].tool;
      }
      ImGui::PopStyleVar();
      ImGui::EndChild();
   }

   void DrawHeader(Scene2DNode* doc)
   {
      const float availW = ImGui::GetContentRegionAvail().x;
      const float startX = ImGui::GetCursorPosX();
      auto gap = []() { FormParts::Inline(0.0f, tok::space_3); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      if (sTool != kCrop)
         sCropOn = false;
      if (sTool != kLasso)
         sPoly.clear();
      if (sTool != kPen && sPenItem >= 0)
         FinishPen(doc);
      if (sTool != kText && sTextEdit != 0)
         EndTextEdit(doc);
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
      else if (PixelTool(sTool))
         DrawPixelBar(doc);
      else if (VectorTool(sTool))
         DrawVectorBar(doc);
      else if (sTool == kText)
         DrawTextBar(doc);
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
      if (l.kind == Kind::Paint)
      {
         DrawChecker(dl, p, ImVec2(p.x + sz.x, p.y + sz.y));
         const unsigned ptex = doc->PaintTexture(l.id);
         const float pw = (float)std::max(1, l.pw), ph = (float)std::max(1, l.ph);
         const float k = std::min(sz.x / pw, sz.y / ph);
         const ImVec2 c(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), h(pw * k * 0.5f, ph * k * 0.5f);
         if (ptex != 0)
            dl->AddImage((ImTextureID)(intptr_t)ptex, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y), ImVec2(0, 1), ImVec2(1, 0));
         return;
      }
      if (l.kind == Kind::Vector || l.kind == Kind::Text)
      {
         DrawChecker(dl, p, ImVec2(p.x + sz.x, p.y + sz.y));
         const unsigned vtex = doc->VectorTexture(l.id);
         float vw = 1, vh = 1;
         doc->LayerBox(l, vw, vh);
         const float k = std::min(sz.x / std::max(1.0f, vw), sz.y / std::max(1.0f, vh));
         const ImVec2 c(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), h(vw * k * 0.5f, vh * k * 0.5f);
         if (vtex != 0)
            dl->AddImage((ImTextureID)(intptr_t)vtex, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y), ImVec2(0, 1), ImVec2(1, 0));
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
            if (l->kind != Kind::Group)
            {
               if (!l->HasPixelMask() && MenuParts::Item(T("Add Layer Mask"), nullptr)) sPending.what = Pending::AddMask;
               if (MenuParts::Item(T("Mask from Selection"), nullptr, false, doc->HasSelection())) sPending.what = Pending::MaskFromSel;
               if (l->HasPixelMask() && MenuParts::Item(T("Delete Layer Mask"), nullptr)) sPending.what = Pending::RemoveMask;
               if (!l->hasVMask && MenuParts::Item(T("Add Vector Mask"), nullptr)) sPending.what = Pending::AddVMask;
               if (l->hasVMask && MenuParts::Item(T("Delete Vector Mask"), nullptr)) sPending.what = Pending::RemoveVMask;
            }
            if (l->kind == Kind::Paint && MenuParts::Item(T("Convert to Draw Node"), nullptr)) sPending.what = Pending::ToDraw;
            if (l->kind == Kind::Vector && MenuParts::Item(T("Convert to Sketch Node"), nullptr)) sPending.what = Pending::ToSketch;
            if (l->kind == Kind::Text && MenuParts::Item(T("Convert to Text Node"), nullptr)) sPending.what = Pending::ToText;
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
      if (IconChip("##v2dnewpaint", IconsInfinite::Brush, false, T("New layer")))
         sPending.what = Pending::AddPaint;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewfill", IconsInfinite::Fill, false, T("New fill layer")))
         sPending.what = Pending::AddFill;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewvec", IconsInfinite::PenTool, false, T("New vector layer")))
         sPending.what = Pending::AddVector;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewgroup", IconsInfinite::Folder, false, T("Group (Ctrl+G)")))
         sPending.what = Pending::Group;
      FormParts::Inline(0.0f, tok::space_1);
      ImGui::BeginDisabled(a == nullptr || a->kind == Kind::Group);
      if (IconChip("##v2dmask", IconsInfinite::Mask, a != nullptr && (a->maskUid != 0 || a->HasPixelMask()),
                   T("Add a layer mask (from the selection when there is one)")))
         sPending.what = doc->HasSelection() ? Pending::MaskFromSel : Pending::AddMask;
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
      if (l->kind == Kind::Text)
      {
         SectionCard::Begin(T("Text"));
         static char buf[4096];
         std::snprintf(buf, sizeof(buf), "%s", l->text.c_str());
         FieldWell::PushStyle();
         if (ImGui::InputTextMultiline("##v2dtexttab", buf, sizeof(buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f)))
         {
            l->text = buf;
            Touch();
         }
         FieldWell::PopStyle();
         UndoOnActivate("Edit text");
         SliderRow(T("Size"), &l->fontSize, 4.0f, 800.0f, "%.0f px");
         SliderRow(T("Leading"), &l->leading, 0.5f, 3.0f, "%.2f");
         SliderRow(T("Tracking"), &l->tracking, -100.0f, 400.0f, "%.0f");
         ColorRow(T("Colour"), l->textC);
         DragRow(T("Box width"), &l->boxW, 1.0f, "%.0f px", 0.0f, 8192.0f);
      }
      if (l->kind == Kind::Vector)
      {
         SectionCard::Begin(T("Vector"));
         ImGui::TextDisabled("%d %s", (int)l->shapes.size(), T("items"));
      }
      if (l->kind != Kind::Group && l->kind != Kind::Fill)
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
         if (l->HasPixelMask())
         {
            BoolRow(T("Layer mask"), &l->pmaskOn);
            if (TextChip(L("Paint mask##v2dpm"), sEditMask, T("The brush paints the mask")))
               sEditMask = !sEditMask;
            FormParts::Inline(0.0f, tok::space_1);
            if (TextChip(L("Delete##v2dpmdel"), false, T("Delete the layer mask")))
               sPending.what = Pending::RemoveMask;
         }
         if (l->hasVMask)
         {
            BoolRow(T("Vector mask"), &l->vmaskOn);
            if (TextChip(L("Draw mask##v2dvm"), sVecTarget == 1, T("Pen and shapes draw the mask")))
            {
               sVecTarget = sVecTarget == 1 ? 0 : 1;
               sItem = -1;
               if (sVecTarget == 1 && !VectorTool(sTool))
                  sTool = kPen;
            }
            FormParts::Inline(0.0f, tok::space_1);
            if (TextChip(L("Delete##v2dvmdel"), false, T("Delete the vector mask")))
               sPending.what = Pending::RemoveVMask;
         }
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

   // A paint layer becomes a Draw node fed by the pixels (saved as a PNG next
   // to the patch), and the layer then shows that node.
   void ConvertToDraw(Scene2DNode* doc, int id)
   {
      std::vector<uint8_t> rgba;
      int w = 0, h = 0;
      if (!doc->ReadPaint(id, rgba, w, h) || w <= 0 || h <= 0)
         return;
      // ReadPaint gives rows bottom first; files are top first.
      std::vector<uint8_t> flipped(rgba.size());
      for (int y = 0; y < h; y++)
         std::memcpy(&flipped[(size_t)y * w * 4], &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
      namespace fs = std::filesystem;
      fs::path dir = gPatchPath.empty() ? fs::path(AppPaths::AppSupportDir()) / "Paint" : fs::path(gPatchPath).parent_path();
      std::error_code ec;
      fs::create_directories(dir, ec);
      const std::string stem = gPatchPath.empty() ? std::string("paint") : fs::path(gPatchPath).stem().string();
      fs::path file;
      for (int n = 1; n < 10000; n++)
      {
         file = dir / (stem + "-layer" + std::to_string(id) + (n > 1 ? "-" + std::to_string(n) : std::string()) + ".png");
         if (!fs::exists(file, ec))
            break;
      }
      if (stbi_write_png(file.string().c_str(), w, h, 4, flipped.data(), w * 4) == 0)
         return;
      PushUndoCheckpoint("Convert to Draw node");
      GraphNode* self = DocGraphNode();
      const ImVec2 at = self != nullptr ? NodePos(*self) : ImVec2(0, 0);
      GraphNode* img = SpawnNode("Image Source", "Source", at.x - 760.0f, at.y);
      if (img == nullptr)
         return;
      const int imgIndex = img->index;
      if (auto* src = dynamic_cast<ImageSourceNode*>(img->node.get()))
         src->Load(file.string());
      GraphNode* draw = SpawnNode("Draw", "Source", at.x - 380.0f, at.y);
      if (draw == nullptr)
         return;
      std::string err;
      ConnectNodes(imgIndex, 0, draw->index, 0, err);
      const uint64_t drawUid = draw->uid;
      if (Layer* l = doc->FindLayer(id))
      {
         // The picture keeps its place: a Fit-none node layer is placed by the same box.
         l->kind = Kind::Node;
         l->uid = drawUid;
         l->fit = Scene2DNode::kFitNone;
         l->tiles.clear();
         l->pw = l->ph = 0;
         doc->Commit();
      }
      gPatchDirty = true;
   }

   // The vector layer becomes a Sketch node drawing the same picture as SVG; the
   // layer keeps its place and points at the node.
   void ConvertToSketch(Scene2DNode* doc, int id)
   {
      Layer* l = doc->FindLayer(id);
      if (l == nullptr)
         return;
      float bw = 1, bh = 1;
      doc->LayerBox(*l, bw, bh);
      const std::string svg = doc->ToSvg(*l);
      PushUndoCheckpoint("Convert to Sketch node");
      GraphNode* self = DocGraphNode();
      const ImVec2 at = self != nullptr ? NodePos(*self) : ImVec2(0, 0);
      GraphNode* gn = SpawnNode("Sketch", "Source", at.x - 380.0f, at.y);
      if (gn == nullptr)
         return;
      if (auto* sk = dynamic_cast<SketchNode*>(gn->node.get()))
      {
         sk->svg = svg;
         sk->svgName = LayerName(*l) + ".svg";
         sk->width = bw;
         sk->height = bh;
         sk->code = "// Drawn from a 2D View vector layer. Animate parts with svgSet(\"#id\", \"attr\", value).\n"
                    "function draw(t) {\n  svgDraw(0, 0, width, height);\n}\n";
         sk->Apply();
      }
      const uint64_t uid = gn->uid;
      if ((l = doc->FindLayer(id)) != nullptr)
      {
         l->kind = Kind::Node;
         l->uid = uid;
         l->fit = Scene2DNode::kFitNone;
         l->shapes.clear();
         l->pw = l->ph = 0;
         doc->Commit();
      }
      gPatchDirty = true;
   }

   // The text layer becomes a Text node set the same way.
   void ConvertToTextNode(Scene2DNode* doc, int id)
   {
      Layer* l = doc->FindLayer(id);
      if (l == nullptr)
         return;
      float bw = 1, bh = 1;
      doc->LayerBox(*l, bw, bh);
      PushUndoCheckpoint("Convert to Text node");
      GraphNode* self = DocGraphNode();
      const ImVec2 at = self != nullptr ? NodePos(*self) : ImVec2(0, 0);
      GraphNode* gn = SpawnNode("Text", "Source", at.x - 380.0f, at.y);
      if (gn == nullptr)
         return;
      if (auto* tn = dynamic_cast<TextNode*>(gn->node.get()))
      {
         tn->text = l->text;
         tn->fontName = l->font;
         tn->fontSize = l->fontSize;
         std::copy(l->textC, l->textC + 3, tn->color);
         tn->tracking = l->tracking;
         tn->align = l->align;
         tn->lineSpacing = l->leading;
         tn->wordWrap = l->boxW > 0.0f;
         tn->wrapWidth = 1.0f;
         tn->wrapHeight = 1.0f;
         tn->fitToBox = false;
         tn->width = bw;
         tn->height = bh;
      }
      const uint64_t uid = gn->uid;
      if ((l = doc->FindLayer(id)) != nullptr)
      {
         // The box keeps its size and place; the text's alpha moves to the layer.
         l->opacity *= std::clamp(l->textC[3], 0.0f, 1.0f);
         l->kind = Kind::Node;
         l->uid = uid;
         l->fit = Scene2DNode::kFitNone;
         doc->Commit();
      }
      gPatchDirty = true;
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
      case Pending::AddPaint: AddPaint(doc); break;
      case Pending::AddMask:
         if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Add layer mask");
            doc->AddPixelMask(a->id);
         }
         break;
      case Pending::MaskFromSel:
         if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Mask from selection");
            doc->MaskFromSelection(a->id);
            doc->Deselect();
         }
         break;
      case Pending::RemoveMask:
         if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Delete layer mask");
            doc->RemovePixelMask(a->id);
            sEditMask = false;
         }
         break;
      case Pending::ToDraw:
         if (Layer* a = Active(doc))
            if (a->kind == Kind::Paint)
               ConvertToDraw(doc, a->id);
         break;
      case Pending::AddVector:
      {
         PushUndoCheckpoint("New vector layer");
         Layer* a = Active(doc);
         SelectOnly(doc->AddVectorLayer(a != nullptr && a->kind != Kind::Group ? a->id : 0));
         sVecTarget = 0;
         if (!VectorTool(sTool))
            sTool = kPen;
         break;
      }
      case Pending::AddVMask:
         if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Add vector mask");
            a->hasVMask = true;
            a->vmaskOn = true;
            sVecTarget = 1;
            sItem = -1;
            if (!VectorTool(sTool))
               sTool = kPen;
            doc->Commit();
         }
         break;
      case Pending::RemoveVMask:
         if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Delete vector mask");
            a->hasVMask = false;
            a->vmask.clear();
            sVecTarget = 0;
            sItem = -1;
            doc->Commit();
         }
         break;
      case Pending::ToSketch:
         if (Layer* a = Active(doc))
            if (a->kind == Kind::Vector)
               ConvertToSketch(doc, a->id);
         break;
      case Pending::ToText:
         if (Layer* a = Active(doc))
            if (a->kind == Kind::Text)
               ConvertToTextNode(doc, a->id);
         break;
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
      InitBrushes();
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
      const float canvasH = std::max(80.0f, avail.y - statusH);
      DrawToolStrip(canvasH);
      ActionButton::RowNext();
      ImGui::BeginGroup();
      DrawCanvas(doc, ImVec2(viewW - kIconW - tok::space_1, canvasH));
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

bool View2DImportSvg(const std::string& path)
{
   Scene2DNode* doc = FindDoc();
   if (doc == nullptr)
      return false;
   std::ifstream f(path, std::ios::binary);
   if (!f)
      return false;
   const std::string svg((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
   PushUndoCheckpoint("Import SVG");
   doc->Layers();
   Layer* a = Active(doc);
   const int id = doc->AddVectorLayer(a != nullptr && a->kind != Kind::Group ? a->id : 0);
   if (!doc->ImportSvg(id, svg))
   {
      doc->RemoveLayer(id);
      return false;
   }
   if (Layer* l = doc->FindLayer(id))
      l->name = std::filesystem::path(path).stem().string();
   doc->Commit();
   SelectOnly(id);
   return true;
}

// Anything with a picture except a document; bare geometry comes in as a solo
// render. Uses the viewport panel's pixel gate, not the output size: a node
// that has not cooked yet reports 0x0.
bool View2DCanAdd(const GraphNode& gn)
{
   INode* n = gn.node.get();
   return n != nullptr && dynamic_cast<Scene2DNode*>(n) == nullptr &&
          (dynamic_cast<IGeometrySource*>(n) != nullptr || CanShowInViewportPanel(gn));
}

// 3D nodes land as 3D cards.
static bool Is3DNode(INode* n)
{
   return dynamic_cast<IGeometrySource*>(n) != nullptr || dynamic_cast<Render3DNode*>(n) != nullptr ||
          dynamic_cast<Scene3DNode*>(n) != nullptr;
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
   GraphNode* added = FindNodeByUid(uid);
   l.is3d = added != nullptr && Is3DNode(added->node.get());
   if (l.is3d)
      doc->Commit();
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
