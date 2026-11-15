// The 2D View: a Scene 2D document laid out as layers over node outputs, the way
// a picture is built in an image editor. Everything here edits the document's
// layers; the nodes themselves are only read, so a layer keeps animating under
// whatever is stacked on top of it. Plan: internal/plans/2d-view/README.md.
#include "app/AppCommon.h"
#include "app/AppShared.h"
#include "ImageCable.h"
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
#include "nodes/PredictiveColoringNode.h"
#include "nodes/Scene2DNode.h"
#include "nodes/SketchNode.h"
#include "nodes/TextNode.h"
#include "nodes/ModelSourceNode.h"
#include "nodes/VideoSourceNode.h"
#include "audio/MediaExtensions.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <fstream>
#include <string>
#include <vector>

namespace app
{
static bool Is3DNode(INode* n);
static bool ImportSvgInto(Scene2DNode* doc, const std::string& path);
bool AddUserFont(const std::string& path, std::string& family);

using I18n::L;
using I18n::T;

bool gView2DOpen = false;
float gView2DHeight = 460.0f;
int gView2DDock = 0;
bool gView2DClaimedKeys = false;
std::string gView2DTools;
ImVec2 gView2DRectMin, gView2DRectMax;

namespace
{
   using Layer = Scene2DNode::Layer;
   using Kind = Scene2DNode::Kind;

   constexpr float kMinHeight = 300.0f;
   constexpr float kSideW = 300.0f;
   constexpr float kIconW = tok::tile;   // icon chips are square, one tile
   constexpr float kSideChip = 22.0f;    // the layers side panel packs its chips and wells tighter
   constexpr float kRowH = 22.0f;        // one layer row
   float sChip = tok::tile;              // chip size for the panel being drawn
   float sRowH = tok::tile;              // height of text chips and bar rows in the part being drawn

   // The top and bottom bars use the side panel's compact row, so a bar slider and a panel slider are the same size.
   float CompactRowH() { return std::round(ImGui::GetFontSize() + 6.0f); }
   struct CompactBar
   {
      CompactBar()
      {
         sRowH = sChip = CompactRowH();
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, 3.0f));
      }
      ~CompactBar()
      {
         ImGui::PopStyleVar();
         sRowH = sChip = tok::tile;
      }
   };
   constexpr float kRuler = 18.0f;
   constexpr float kHandle = 7.0f;
   constexpr float kSnapPx = 6.0f;
   constexpr float kPi = 3.14159265f;
   constexpr const char* kUndo = "Edit in 2D View";

   // Overlay colours are an image editor's, not the app's: they are read against the picture.
   constexpr unsigned kPasteboard = 0x282828, kOutline = 0x2D8CFF, kSmart = 0xFF2DB4, kGuide = 0x00D2E6, kGrid = 0x808080;

   enum Tool { kMove = 0, kCrop, kHand, kZoom, kBrush, kEraser, kFill, kPick, kMarquee, kLasso, kWand, kPen, kNode, kShape, kText,
               kGradient, kClone, kHeal, kBlur, kSharpen, kDodge, kBurn, kToolCount };
   int sTool = kMove;
   bool RetouchTool(int t) { return t >= kClone && t <= kBurn; }
   bool BrushTool(int t) { return t == kBrush || t == kEraser || RetouchTool(t); }
   bool PaintTool(int t) { return BrushTool(t) || t == kFill || t == kGradient; }
   bool PixelTool(int t) { return (t >= kBrush && t <= kWand) || t >= kGradient; }

   // Paint tools. The brush and the eraser keep their own settings.
   BrushEngine::Brush sBrush, sEraser;
   // The brush's numbers are params of the open document, after Drive A..D and before the layer slots.
   constexpr int kBrushSizeParam = 4, kBrushOpacityParam = 5, kBrushFlowParam = 6, kBrushHueParam = 7;
   // Whole steps for counts and degrees, two decimals for the rest.
   const char* AdjFmt(int t)
   {
      using S = Scene2DNode;
      return t == S::kAdjPixelate || t == S::kAdjHalftone || t == S::kAdjDither || t == S::kAdjPosterize || t == S::kAdjHue ? "%.0f" : "%.2f";
   }
   float sBrushHue = 0.0f;   // degrees, turns the colour each stroke starts with (sound as a paint input)
   // Turns an RGB colour round the hue circle, keeping its value and saturation.
   void HueTurn(float c[3], float degrees)
   {
      float h = 0, sat = 0, v = 0;
      ImGui::ColorConvertRGBtoHSV(c[0], c[1], c[2], h, sat, v);
      h = std::fmod(h + degrees / 360.0f + 1.0f, 1.0f);
      ImGui::ColorConvertHSVtoRGB(h, sat, v, c[0], c[1], c[2]);
   }
   int sBrushPreset = -1, sEraserPreset = -1;
   bool sBrushInit = false;
   float sFg[3] = { 0.0f, 0.0f, 0.0f }, sBg[3] = { 1.0f, 1.0f, 1.0f };
   int sFillKind = 1;              // gradient: 1 linear, 2 radial
   bool sGradReverse = false;      // gradient runs background to foreground
   int sTolerance = 32;
   bool sContiguous = true, sAllLayers = false;
   int sImageFormat = 0, sVideoFormat = 0;   // the Render tab's default file types
   bool sSurfacePaint = true;      // brush / eraser / eyedropper work on a 3D object's surface
   bool sSurfaceStroke = false;    // the stroke now running paints a surface
   int sSkinSide = 2;              // 0..3: 512 .. 4096 px, for the next new skin
   bool sEditMask = false;         // the brush paints the active layer's pixel mask
   int sMarqueeShape = 0;          // 0 rectangle, 1 ellipse
   int sLassoKind = 0;             // 0 freehand, 1 polygon
   float sFeather = 0.0f;
   float sSelModPx = 4.0f;         // Expand / Contract / Smooth / Border, document px
   int sSelOp = 0;                 // Scene2DNode::SelOp
   std::vector<float> sPoly;       // polygon lasso points so far, document px
   int sPolyOp = 0;
   bool sHaveLast = false;         // where the last stroke ended (Shift-click draws a line from it)
   ImVec2 sLastPaint(0.0f, 0.0f);
   uint64_t sDocUid = 0;
   std::vector<int> sSel;          // layer ids, in click order
   int sTab = 0;                   // 0 Layers, 1 Layer, 2 Transform, 3 Document, 4 Output
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
      int warpHandle = -1;        // kind 18: the handle (0..7) pulled; corners distort or perspective, edges skew
      bool perspective = false;
      bool fromCentre = false;
      int guideAxis = 0;          // 0 vertical (x), 1 horizontal (y)
      int guideIndex = -1;
      float crop0[4] = { 0, 0, 0, 0 };
      int cropHandle = -1;        // 0..7 handle, 8 inside
      ImVec2 pan0;
      bool made = false;          // the shape tool has added its item
      int gizmoAxis = -1;         // 16: the 3D gizmo part held (0..2 axes, 3 centre)
   } sOp;

   // The 3D gizmo on a 3D layer: 0 move, 1 rotate, 2 scale (W / E / R, like the 3D View).
   int sGizmoMode = 0;
   int sGizmoHot = -1;

   bool sCropOn = false;
   int sImageW = 0, sImageH = 0;   // Document tab's Image size fields (namespace scope: the audit sets them)
   uint64_t sSizeFor = 0;           // the document and size those fields were last synced to
   int sSizeSeenW = 0, sSizeSeenH = 0;
   float sCrop[4] = { 0, 0, 0, 0 };   // x0 y0 x1 y1, document px
   std::vector<Scene2DNode::Layer> sClip;   // Copy / Cut: the layers waiting to be pasted
   int sPasteCount = 0;                     // each paste of the same copy steps further from the original
   float sZoomVel = 0.0f;                   // smoothed wheel zoom: log-zoom still to apply
   ImVec2 sZoomAt(0, 0);

   struct Pending
   {
      enum What { None, NewDoc, DuplicateDoc, DeleteDoc, Convert, RenderImage, RenderAnim, Group, Ungroup, Duplicate, Copy, Cut, Paste, Delete, AddFill, AddPaint, AddMask, MaskFromSel, RemoveMask, ToDraw, AddVector, AddVMask, RemoveVMask, ToSketch, ToText, ToShapes, AddAdjust, AddNode, ReplaceNode, LayerOutput, LayerBelow };
      What what = None;
      int arg = 0;         // AddAdjust: the type
      uint64_t uid = 0;    // AddNode / ReplaceNode: the canvas node
   } sPending;

   // Click-a-node-on-the-canvas mode, as the 3D View has: 0 off, 1 add the picked node as a layer, 2 replace the active layer's source.
   int sPickMode = 0;
   uint64_t sPickedUid = 0;

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
      static uint64_t sLastNode = 0;
      uint64_t selected = 0, anyNode = 0;
      for (GraphNode& gn : gNodes)
         if (ed::IsNodeSelected(gn.NodeId()))
         {
            anyNode = gn.uid;
            break;
         }
      if (anyNode != 0 && anyNode != sLastNode && sPickMode != 0 && View2DCanAdd(*FindNodeByUid(anyNode)))
         sPickedUid = anyNode;
      sLastNode = anyNode;
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
      if (l.kind == Kind::Adjust)
         return std::string(T(Scene2DNode::AdjName(l.adjType))) + " " + std::to_string(l.id);
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
         if (!InQuad(c, p))
            continue;
         // Inside the box is not enough: a painted layer counts where it has paint, an object where the ray meets it.
         if (l.kind == Kind::Paint && l.pw > 0 && !l.tiles.empty())
         {
            float bx = 0, by = 0, bw = 1, bh = 1;
            doc->DocToBox(l, p.x, p.y, bx, by);
            doc->LayerBox(l, bw, bh);
            const int kT = 256;   // TileStore::kTile
            const int cols = (l.pw + kT - 1) / kT;
            const int tx = std::clamp((int)(bx * l.pw / bw) / kT, 0, cols - 1);
            const int ty = std::clamp((int)((l.ph - by * l.ph / bh)) / kT, 0, (int)(l.tiles.size() / cols) - 1);
            const size_t idx = (size_t)ty * cols + tx;
            if (idx < l.tiles.size() && l.tiles[idx] == "-" && !(doc->Painting() && doc->PaintingLayer() == l.id))
               continue;
         }
         else if (l.kind == Kind::Node && doc->CanSurfacePaint(l.id))
         {
            Scene2DNode::SurfacePoint sp;
            if (!doc->SurfaceAt(l.id, p.x, p.y, sp) && doc->HasGeometryView(l.id))
               continue;
         }
         return l.id;
      }
      return 0;
   }

   // What a layer marquee touches, in document px: a painted layer only where it has paint (as the click hit), the
   // rest by their corners. False when the layer has nothing to pick.
   bool PickBox(Scene2DNode* doc, const Layer& l, float lo[2], float hi[2])
   {
      lo[0] = lo[1] = 1e30f;
      hi[0] = hi[1] = -1e30f;
      auto grow = [&](float x, float y) {
         lo[0] = std::min(lo[0], x); lo[1] = std::min(lo[1], y);
         hi[0] = std::max(hi[0], x); hi[1] = std::max(hi[1], y);
      };
      if (l.kind == Kind::Paint && l.pw > 0 && !l.tiles.empty())
      {
         float bw = 1, bh = 1;
         doc->LayerBox(l, bw, bh);
         const int kT = 256;   // TileStore::kTile
         const int cols = (l.pw + kT - 1) / kT;
         bool any = false;
         for (size_t i = 0; i < l.tiles.size(); i++)
         {
            if (l.tiles[i] == "-")
               continue;
            any = true;
            const int tx = (int)(i % cols), ty = (int)(i / cols);
            const float bx0 = tx * kT * bw / l.pw, bx1 = std::min((float)l.pw, (float)(tx + 1) * kT) * bw / l.pw;
            const float by0 = std::max(0.0f, (float)(l.ph - (ty + 1) * kT)) * bh / l.ph, by1 = (float)(l.ph - ty * kT) * bh / l.ph;
            for (float bx : { bx0, bx1 })
               for (float by : { by0, by1 })
               {
                  float x = 0, y = 0;
                  doc->BoxToDoc(l, bx, by, x, y);
                  grow(x, y);
               }
         }
         return any;
      }
      float c[8];
      doc->Corners(l, c);
      for (int i = 0; i < 4; i++)
         grow(c[i * 2], c[i * 2 + 1]);
      return true;
   }

   // Layer marquee: every pickable layer the rectangle a..b (document px) touches joins the selection; without add the
   // selection starts empty.
   void MarqueeLayers(Scene2DNode* doc, ImVec2 a, ImVec2 b, bool add)
   {
      if (!add)
         sSel.clear();
      const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
      const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
      for (const Layer& l : doc->Layers())
      {
         if (l.kind == Kind::Group || l.kind == Kind::Fill || l.kind == Kind::Adjust || !doc->Shown(l) ||
             (l.lock & Scene2DNode::kLockAll) != 0)
            continue;
         float lo[2], hi[2];
         if (PickBox(doc, l, lo, hi) && hi[0] >= x0 && lo[0] <= x1 && hi[1] >= y0 && lo[1] <= y1 && !Selected(l.id))
            sSel.push_back(l.id);
      }
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

   // Even spacing: the outer two stay, the centres between them step evenly along the axis.
   void Distribute(Scene2DNode* doc, int axis)
   {
      std::vector<std::pair<float, int>> order;
      for (int id : sSel)
      {
         float a[2], b[2];
         if (doc->Bounds(id, a, b))
            order.push_back({ (a[axis] + b[axis]) * 0.5f, id });
      }
      if (order.size() < 3)
         return;
      std::sort(order.begin(), order.end());
      PushUndoCheckpoint("Distribute in 2D View");
      const float first = order.front().first, step = (order.back().first - first) / (float)(order.size() - 1);
      for (size_t i = 1; i + 1 < order.size(); i++)
      {
         const float d = first + step * (float)i - order[i].first;
         for (Layer& l : doc->Layers())
            if (l.kind != Kind::Group && Movable(l) && doc->IsInside(l.id, order[i].second))
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

   void AddAdjust(Scene2DNode* doc, int type)
   {
      PushUndoCheckpoint("New adjustment layer");
      Layer* a = Active(doc);
      Layer& l = doc->AddLayer(Kind::Adjust, 0, a != nullptr ? a->id : 0);
      l.adjType = std::clamp(type, 0, Scene2DNode::kAdjCount - 1);
      l.adjAmt = l.adjTo = Scene2DNode::AdjDefault(l.adjType);
      if (l.adjType == Scene2DNode::kAdjGradientMap)
      {
         const float a0[4] = { 0.05f, 0.1f, 0.35f, 1.0f }, b0[4] = { 1.0f, 0.85f, 0.4f, 1.0f };
         std::copy(a0, a0 + 4, l.colorA);
         std::copy(b0, b0 + 4, l.colorB);
      }
      const int id = l.id;
      doc->Commit();
      if (doc->HasSelection())
         doc->MaskFromSelection(id);
      SelectOnly(id);
   }

   // A canvas node as a layer, centred at a document point (the middle when null).
   int AddNodeLayer(Scene2DNode* doc, uint64_t uid, const ImVec2* at)
   {
      GraphNode* gn = FindNodeByUid(uid);
      if (gn == nullptr || !View2DCanAdd(*gn))
         return 0;
      Layer* a = Active(doc);
      Layer& l = doc->AddLayer(Kind::Node, uid, a != nullptr && a->kind != Kind::Group ? a->id : 0);
      l.is3d = Is3DNode(gn->node.get());
      if (at != nullptr)
      {
         l.x = at->x;
         l.y = at->y;
      }
      const int id = l.id;
      doc->Commit();
      SelectOnly(id);
      return id;
   }

   // Swap a node layer's source and keep everything else: transform, 3D pose, mask, blend, effects.
   bool ReplaceSource(Scene2DNode* doc, int id, uint64_t uid)
   {
      Layer* l = doc->FindLayer(id);
      GraphNode* gn = FindNodeByUid(uid);
      if (l == nullptr || l->kind != Kind::Node || gn == nullptr || !View2DCanAdd(*gn) || gn->node.get() == doc || l->uid == uid)
         return false;
      l->uid = uid;
      doc->Commit();
      return true;
   }

   // "From canvas": every node that can be a layer, the one in use ticked.
   void NodeChoiceMenu(const char* label, Scene2DNode* doc, Pending::What what, uint64_t current)
   {
      if (!MenuParts::SubMenu(label, true))
         return;
      bool any = false;
      for (GraphNode& gn : gNodes)
      {
         if (gn.node.get() == doc || !View2DCanAdd(gn))
            continue;
         any = true;
         const std::string name = gn.typeName + " " + std::to_string(gn.index) + "##n" + std::to_string(gn.uid);
         if (MenuParts::Item(name.c_str(), nullptr, gn.uid == current))
         {
            sPending.what = what;
            sPending.uid = gn.uid;
         }
      }
      if (!any)
         ImGui::TextDisabled("%s", T("No fitting nodes"));
      ImGui::EndMenu();
   }

   // ---- widgets ---------------------------------------------------------

   bool IconChip(const char* id, const char* glyphStr, bool on, const char* tip, float w = 0.0f)
   {
      if (w <= 0.0f)
         w = sChip;
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const bool clicked = ActionButton::Draw(id, ImVec2(w, sChip));
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", tip);
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      const ImVec2 gc((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
      const float roleGs = sChip < tok::tile ? tok::icon_sm : tok::icon_md;
      float gs = roleGs;
      {
         // Same ink, not just the same box: a glyph drawn small inside its em (the pointer, the dot grid) is
         // scaled up to the set's ink, about 0.78 of the em, so a bar of icons reads as one size.
         ImVec2 imn, imx;
         glyph::InkBox(gc, gs, glyphStr, imn, imx);
         const float ink = std::max(imx.x - imn.x, imx.y - imn.y), target = 0.78f * gs;
         if (ink > 1.0f && ink < target - 1.5f)
            gs = std::round(gs * std::min(1.4f, target / ink));
      }
      glyph::Draw(ImGui::GetWindowDrawList(), gc, gs, on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text), glyphStr);
#ifdef IMGUI_ENABLE_TEST_ENGINE
      ImVec2 imn, imx;
      glyph::InkBox(gc, gs, glyphStr, imn, imx);
      UiAudit::NoteInk(1, imn, imx, roleGs, tip, glyphStr);   // the role's size; the ink is what was drawn
#endif
      return clicked;
   }

   bool ZoomChip(const char* id, bool plus, const char* tip)
   {
      return IconChip(id, plus ? IconsInfinite::Zoom : IconsInfinite::ZoomOut, false, tip);
   }

   // Dimmed text centred on a tile row that starts at rowTop. Drawn, not laid out as Text: ImGui shifts Text by the
   // line's frame-padding baseline offset, which put labels ~6 pt under the chips' centre line.
   void CentredText(float rowTop, const char* text, bool dim = true, float rowH = 0.0f)
   {
      if (rowH <= 0.0f)
         rowH = sRowH;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const ImVec2 sz = ImGui::CalcTextSize(text);
      const float px = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.y); // device-pixel snap, as ActionButton
      const ImVec2 tp(p.x, std::round((rowTop + (rowH - sz.y) * 0.5f) * px) / px);
      ImGui::GetWindowDrawList()->AddText(tp, ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text), text);
      UiAudit::NoteInk(2, tp, ImVec2(tp.x + sz.x, tp.y + sz.y), 0.0f, text);
      ImGui::SetCursorScreenPos(ImVec2(p.x, rowTop));
      ImGui::Dummy(ImVec2(sz.x, rowH));
   }

   bool TextChip(const char* label, bool on, const char* tip)
   {
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const float w = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + 2.0f * tok::space_2;
      const bool clicked = ActionButton::Draw(label, ImVec2(w, sRowH));
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (tip != nullptr && ImGui::IsItemHovered())
         HelpTip("%s", tip);
      return clicked;
   }

   // Field widths by content class (plan §3): S numbers, M short words, L labelled sliders and names.
   constexpr float kWellS = 56.0f, kWellM = 72.0f, kWellL = 110.0f;
   float sFieldW = 160.0f;
   constexpr float kLabelW = 84.0f;

   void FieldLabel(const char* text)
   {
      ImGui::AlignTextToFramePadding();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const char* end = ImGui::FindRenderedTextEnd(text);   // "Width##img" shows as "Width"
      ImGui::TextColored(ImVec4(t.x, t.y, t.z, 0.72f), "%.*s", (int)(end - text), text);
      UiAudit::NoteInk(3, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 0.0f, std::string(text, end).c_str());
      FormParts::Inline(kLabelW);
   }

   // Shortcut text in the platform's words: Cmd on macOS, Ctrl elsewhere.
   const char* Sc(const char* s)
   {
#ifdef __APPLE__
      static std::string buf[8];
      static int next = 0;
      std::string& b = buf[next++ & 7];
      b = s;
      for (size_t at = b.find("Ctrl"); at != std::string::npos; at = b.find("Ctrl", at))
         b.replace(at, 4, "Cmd");
      return b.c_str();
#else
      return s;
#endif
   }

   void UndoOnActivate(const char* what = kUndo)
   {
      // The model only reaches the saved text at the end of the frame, so the
      // checkpoint taken now still holds the value from before the press.
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint(what);
   }

   // Motion-first layers: ModNext names the layer number the next row edits. Right-click on that row binds it
   // (modulator, expression, recording, MIDI learn, matrix); while something drives it, the row shows the driven
   // value and does not take edits.
   struct ModTarget { int layer = -1, field = -1; bool driven = false; int slot = -1; };
   ModTarget sModNext;
   void ModNext(const Layer& l, int field) { sModNext = { l.id, field, false, -1 }; }
   // A fixed param of the open document (the brush): no layer, a set index.
   void ModNextParam(int slot) { sModNext = { -1, 0, false, slot }; }

   int DocNodeIndex()
   {
      GraphNode* g = sDocUid != 0 ? FindNodeByUid(sDocUid) : nullptr;
      return g != nullptr ? g->index : -1;
   }

   bool LayerFieldDriven(Scene2DNode* doc, int layer, int field, int fixed = -1)
   {
      const int ni = DocNodeIndex();
      const int slot = fixed >= 0 ? (ni >= 0 ? fixed : -1) : doc != nullptr && ni >= 0 ? doc->SlotFor(layer, field, false) : -1;
      return slot >= 0 && (Modulation::Instance().IsModulated(ni, slot) || Modulation::Instance().HasExpression(ni, slot));
   }

   void ModBegin()
   {
      if (sModNext.field < 0)
         return;
      sModNext.driven = LayerFieldDriven(FindDoc(), sModNext.layer, sModNext.field, sModNext.slot);
      if (sModNext.driven)
         ImGui::BeginDisabled();
   }

   void ModEnd()
   {
      if (sModNext.field < 0)
         return;
      if (sModNext.driven)
         ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
         Scene2DNode* doc = FindDoc();
         const int ni = DocNodeIndex();
         const int slot = sModNext.slot >= 0 ? (ni >= 0 ? sModNext.slot : -1)
                          : doc != nullptr && ni >= 0 ? doc->SlotFor(sModNext.layer, sModNext.field, true) : -1;
         if (slot >= 0)
         {
            gModBindingMenuNode = ni;
            gModBindingMenuParam = slot;
            gOpenModBindingMenu = true;
            gParamRightClickConsumedThisFrame = true;
         }
      }
      sModNext = {};
   }

   bool DragRow(const char* label, float* v, float speed, const char* fmt, float lo = -1e6f, float hi = 1e6f)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      FieldWell::PushStyle();
      const std::string wid = FormParts::Id(label);
      const ImGuiID gid = ImGui::GetID(wid.c_str());
      // Hover the box and just type a digit: the number field opens with that first character in it.
      {
         static ImGuiID sTypeId = 0;
         static ImWchar sTypeCh = 0;
         const ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + ImGui::CalcItemWidth(), mn.y + ImGui::GetFrameHeight());
         const bool editing = ImGui::TempInputIsActive(gid);
         if (editing && sTypeId == gid && sTypeCh != 0)
         {
            ImGui::GetIO().AddInputCharacter(sTypeCh);
            sTypeId = 0;
            sTypeCh = 0;
         }
         else if (!editing && ImGui::GetCurrentContext()->ActiveId == 0 && ImGui::IsMouseHoveringRect(mn, mx) &&
                  ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
         {
            for (const ImWchar c : ImGui::GetIO().InputQueueCharacters)
               if ((c >= '0' && c <= '9') || c == '-' || c == '.')
               {
                  sTypeId = gid;
                  sTypeCh = c;
                  ImGuiContext& g = *ImGui::GetCurrentContext();
                  g.NavNextActivateId = gid;
                  g.NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
                  break;
               }
         }
      }
      ModBegin();
      const bool changed = ImGui::DragFloat(wid.c_str(), v, speed, lo, hi, fmt);
      ModEnd();
      FieldWell::PopStyle();
      UndoOnActivate();
      if (changed)
         Touch();
      return changed;
   }

   // Two or three numbers on one row ("Position  X 120  Y 80"); returns a bit per field that changed.
   int DragMulti(const char* label, float* const* v, const char* const* fmts, int n, float speed, float lo = -1e6f, float hi = 1e6f,
                 const Layer* modLayer = nullptr, const int* modFields = nullptr)
   {
      FieldLabel(label);
      const float each = (sFieldW - (n - 1) * tok::space_1) / n;
      int changed = 0;
      FieldWell::PushStyle();
      ImGui::PushID(label);
      for (int i = 0; i < n; i++)
      {
         if (i > 0)
            ImGui::SameLine(0.0f, tok::space_1);
         ImGui::PushID(i);
         ImGui::SetNextItemWidth(each);
         if (modLayer != nullptr && modFields != nullptr && modFields[i] >= 0)
            ModNext(*modLayer, modFields[i]);
         ModBegin();
         if (ImGui::DragFloat("##v", v[i], speed, lo, hi, fmts[i]))
            changed |= 1 << i;
         ModEnd();
         UndoOnActivate();
         ImGui::PopID();
      }
      ImGui::PopID();
      FieldWell::PopStyle();
      if (changed)
         Touch();
      return changed;
   }

   bool SliderRow(const char* label, float* v, float lo, float hi, const char* fmt)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      ModBegin();
      const bool changed = FieldWell::Slider(FormParts::Id(label).c_str(), v, lo, hi, fmt);
      ModEnd();
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
      UiAudit::NoteInk(4, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 0.0f, label);
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
      // The label sits on the grid's centre line, as every other label sits on its field's.
      const float cell = 14.0f;
      const ImVec2 row = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + std::round((cell * 3.0f - ImGui::GetFrameHeight()) * 0.5f)));
      FieldLabel(label);
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, row.y));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      bool changed = false;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton(FormParts::Id(label).c_str(), ImVec2(cell * 3, cell * 3));
      if (*anchor == 4)
         UiAudit::NoteOn();   // the grid's centre cell is the chosen one: a click there rightly changes nothing
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

   int sAuditDialogs = 0;   // UI audit: native dialogs it asked for (and did not open)
   bool AuditNoDialog()
   {
#ifdef IMGUI_ENABLE_TEST_ENGINE
      if (getenv("INFINITE_UIAUDIT_VIEW2D") != nullptr)
      {
         sAuditDialogs++;
         return true;
      }
#endif
      return false;
   }

   void RenderImage()
   {
      if (gOfflineRender.active || AuditNoDialog())
         return;
      const std::string path = Platform::SaveRenderDialog(sImageFormat == 1 ? "infinite_2d.jpg" : "infinite_2d.png", false);
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
      if (gOfflineRender.active || AuditNoDialog())
         return;
      const std::string path = Platform::SaveRenderDialog(sVideoFormat == 1 ? "infinite_2d.mov" : "infinite_2d.mp4", true);
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
      ExportImage(out, sStillPath, std::clamp(out->jpegQuality, 1, 100));
      sLastRender = sStillPath;
      sStillPath.clear();
   }

   // ---- keys ------------------------------------------------------------

   void HandlePixelKeys(Scene2DNode* doc, bool cmd, bool shift);

   bool HandleVectorKeys(Scene2DNode* doc);

   // Layer edits work from the layer list as well as the canvas: delete, copy, cut, paste, duplicate, group,
   // clip, restack and select all.
   void HandleLayerKeys(Scene2DNode* doc, bool cmd, bool shift, bool alt, bool onCanvas)
   {
      auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      if (key(ImGuiKey_Delete) || key(ImGuiKey_Backspace))
         sPending.what = Pending::Delete;
      if (cmd && !shift && (key(ImGuiKey_J) || (key(ImGuiKey_D) && !(onCanvas && PixelTool(sTool))))) sPending.what = Pending::Duplicate;
      if (cmd && !shift)
      {
         if (key(ImGuiKey_C)) sPending.what = Pending::Copy;
         if (key(ImGuiKey_X)) sPending.what = Pending::Cut;
         if (key(ImGuiKey_V)) sPending.what = Pending::Paste;
      }
      if (cmd && !shift && !alt && key(ImGuiKey_G)) sPending.what = Pending::Group;
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
      if (cmd && key(ImGuiKey_A) && onCanvas && PixelTool(sTool))
         doc->SelectAll();
      else if (cmd && key(ImGuiKey_A))
      {
         sSel.clear();
         for (const Layer& l : doc->Layers())
            if (l.parent == 0)
               sSel.push_back(l.id);
      }
   }

   bool sPanelHovered = false;   // pointer anywhere over the 2D View (side panel included)
   bool gView2DTestCmd = false;

   void HandleKeys(Scene2DNode* doc, bool hovered)
   {
      const ImGuiIO& io = ImGui::GetIO();
      if ((!hovered && (!sPanelHovered || ImGui::IsAnyItemActive())) || ImGui::GetIO().WantTextInput)
         return;
      gView2DClaimedKeys = true;
      const bool cmd = io.KeyCtrl || io.KeySuper || gView2DTestCmd;
      const bool shift = io.KeyShift, alt = io.KeyAlt;
      auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      auto rep = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
      if (!hovered)
      {
         HandleLayerKeys(doc, cmd, shift, alt, false);
         return;
      }
      if (!cmd && !alt)
      {
         if (key(ImGuiKey_V)) sTool = kMove;
         if (key(ImGuiKey_C)) sTool = kCrop;
         if (key(ImGuiKey_H)) sTool = kHand;
         if (key(ImGuiKey_Z)) sTool = kZoom;
         if (key(ImGuiKey_B)) sTool = kBrush;
         if (key(ImGuiKey_E)) sTool = kEraser;
         if (key(ImGuiKey_G)) sTool = sTool == kFill ? kGradient : kFill;
         if (key(ImGuiKey_S)) sTool = kClone;
         if (key(ImGuiKey_J)) sTool = kHeal;
         if (key(ImGuiKey_R)) sTool = sTool == kBlur ? kSharpen : kBlur;
         if (key(ImGuiKey_O)) sTool = sTool == kDodge ? kBurn : kDodge;
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
      HandleLayerKeys(doc, cmd, shift, alt, true);
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
         // Every layer with a box of its own: photos, paint, text, shapes and nodes alike (fills and adjustments
         // cover the page, whose edges are already targets; a group's children are listed themselves).
         if (l.kind == Kind::Group || l.kind == Kind::Fill || l.kind == Kind::Adjust || !doc->Shown(l) ||
             std::find(moving.begin(), moving.end(), l.id) != moving.end())
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

   // Retouch tools share one brush; the tool picks what it does to the pixels.
   BrushEngine::Brush sRetouch = []()
   {
      BrushEngine::Brush b;
      b.size = 40.0f;
      b.hardness = 0.4f;
      b.opacity = 0.5f;
      b.spacing = 0.1f;
      return b;
   }();
   ImVec2 sCloneSrc(0, 0), sCloneOffset(0, 0);
   bool sHaveCloneSrc = false, sCloneOffsetSet = false;

   BrushEngine::Brush& ToolBrush()
   {
      if (RetouchTool(sTool))
      {
         static const int kModes[] = { BrushEngine::kClone, BrushEngine::kHeal, BrushEngine::kBlur, BrushEngine::kSharpen,
                                       BrushEngine::kDodge, BrushEngine::kBurn };
         sRetouch.mode = kModes[sTool - kClone];
         return sRetouch;
      }
      return sTool == kEraser ? sEraser : sBrush;
   }

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

   // The active layer takes no paint: hidden (Photoshop refuses too) or its pixels locked. Nothing is made, no undo step.
   bool PaintRefused(Scene2DNode* doc)
   {
      if (doc->quickMask)
         return false;
      const Layer* a = Active(doc);
      if (a == nullptr || (a->kind != Kind::Paint && !(sEditMask && a->HasPixelMask())))
         return false;
      return !doc->Shown(*a) || (!sEditMask && (a->lock & (Scene2DNode::kLockPixels | Scene2DNode::kLockAll)) != 0);
   }

   void BeginStroke(Scene2DNode* doc, ImVec2 at, bool line)
   {
      const bool cloning = sTool == kClone || sTool == kHeal;
      if (cloning && !sHaveCloneSrc)
         return;   // Alt-click sets the source first
      // A 3D object under the brush takes the paint on its surface (by UV) instead of a flat layer.
      if (Layer* a = Active(doc); a != nullptr && sSurfacePaint && !sEditMask && (sTool == kBrush || sTool == kEraser) &&
                                  doc->CanSurfacePaint(a->id))
      {
         Scene2DNode::SurfacePoint sp;
         if (doc->SurfaceAt(a->id, at.x, at.y, sp))
         {
            PushUndoCheckpoint(sTool == kEraser ? "Erase surface" : "Paint surface");
            BrushEngine::Brush b = ToolBrush();
            std::copy(sFg, sFg + 3, b.color);
            if (doc->BeginSurfacePaint(a->id, b, at.x, at.y, PenNow(), 512 << sSkinSide))
            {
               sOp = Op();
               sOp.kind = 7;
               sOp.docStart = at;
               sSurfaceStroke = true;
               return;
            }
         }
      }
      if (PaintRefused(doc))
         return;
      PushUndoCheckpoint(sTool == kEraser ? "Erase" : RetouchTool(sTool) ? "Retouch" : "Paint");
      int target = 0;
      const int id = PaintTarget(doc, target, sTool == kBrush);
      if (id == 0)
         return;
      BrushEngine::Brush b = ToolBrush();
      if (cloning)
      {
         // Aligned: the first stroke after Alt-click fixes the offset; later strokes keep it.
         if (!sCloneOffsetSet)
         {
            sCloneOffset = ImVec2(sCloneSrc.x - at.x, sCloneSrc.y - at.y);
            sCloneOffsetSet = true;
         }
         b.sourceOffset[0] = sCloneOffset.x;
         b.sourceOffset[1] = sCloneOffset.y;
      }
      std::copy(sFg, sFg + 3, b.color);
      if (sTool == kBrush && sBrushHue != 0.0f)
         HueTurn(b.color, sBrushHue);
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
      sSurfaceStroke = false;
      if (doc->Painting())
         doc->EndPaint();
      sLastPaint = sMouseDoc;
      sHaveLast = true;
   }

   void FillAt(Scene2DNode* doc, ImVec2 at)
   {
      if (PaintRefused(doc))
         return;
      PushUndoCheckpoint("Fill");
      int target = 0;
      const int id = PaintTarget(doc, target, true);
      if (id > 0 && target == 0)
         doc->Bucket(id, at.x, at.y, sFg, 1.0f, sTolerance, sContiguous, sAllLayers);
   }

   void GradientTo(Scene2DNode* doc, ImVec2 a, ImVec2 b)
   {
      if (PaintRefused(doc))
         return;
      PushUndoCheckpoint("Gradient");
      int target = 0;
      const int id = PaintTarget(doc, target, true);
      const float* c0 = sGradReverse ? sBg : sFg;
      const float* c1 = sGradReverse ? sFg : sBg;
      const float ca[4] = { c0[0], c0[1], c0[2], 1.0f }, cb[4] = { c1[0], c1[1], c1[2], 1.0f };
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
   const char* ToolGlyph(int tool);

   // The pointer follows the tool, as in every image editor: an I-beam for text, a crosshair with the tool's
   // badge for tools that act on a point, the arrow for the selection tools.
   void DrawToolCursor(ImDrawList* dl, ImVec2 mouse)
   {
      if (sTool == kText)
      {
         ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
         return;
      }
      if (sTool == kMove || sTool == kNode || sTool == kHand)
         return;
      const char* g = ToolGlyph(sTool);
      if (g == nullptr)
         return;
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      for (int pass = 0; pass < 2; pass++)
      {
         const ImU32 col = pass == 0 ? IM_COL32(0, 0, 0, 170) : IM_COL32(255, 255, 255, 255);
         const float th = pass == 0 ? 3.0f : 1.0f;
         dl->AddLine(ImVec2(mouse.x - 8, mouse.y), ImVec2(mouse.x - 2, mouse.y), col, th);
         dl->AddLine(ImVec2(mouse.x + 2, mouse.y), ImVec2(mouse.x + 8, mouse.y), col, th);
         dl->AddLine(ImVec2(mouse.x, mouse.y - 8), ImVec2(mouse.x, mouse.y - 2), col, th);
         dl->AddLine(ImVec2(mouse.x, mouse.y + 2), ImVec2(mouse.x, mouse.y + 8), col, th);
      }
      const ImVec2 badge(mouse.x + 15.0f, mouse.y + 15.0f);
      for (int dx = -1; dx <= 1; dx++)
         for (int dy = -1; dy <= 1; dy++)
            if (dx != 0 || dy != 0)
               glyph::Draw(dl, ImVec2(badge.x + dx, badge.y + dy), tok::icon_md, IM_COL32(0, 0, 0, 200), g);
      glyph::Draw(dl, badge, tok::icon_md, IM_COL32(255, 255, 255, 255), g);
   }

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
         if (BrushTool(sTool))
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
      // A distorted layer: the handles sit on its pulled corners (and between them).
      float c[8];
      if (l.warpOn && doc->Corners(l, c))
      {
         const float tx = c[0] + (c[2] - c[0]) * fx, ty = c[1] + (c[3] - c[1]) * fx;
         const float bx = c[6] + (c[4] - c[6]) * fx, by = c[7] + (c[5] - c[7]) * fx;
         out = ToScreen(doc, tx + (bx - tx) * fy, ty + (by - ty) * fy);
         return;
      }
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

   bool CanWarp(Scene2DNode* doc)
   {
      const Layer* l = Active(doc);
      return l != nullptr && l->kind != Kind::Group && l->kind != Kind::Fill && l->kind != Kind::Adjust && !doc->IsObjectLayer(*l);
   }

   // Distort, perspective and skew all move the layer's corners (warp: document px per corner, TL TR BR BL).
   void WarpActive(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      if (l == nullptr || sOp.warpHandle < 0)
         return;
      const Layer& l0 = sOp.l0;
      const ImVec2 md = ToDoc(doc, mouse);
      float dx = md.x - sOp.docStart.x, dy = md.y - sOp.docStart.y;
      *l = l0;
      l->warpOn = true;
      float c[8];
      if (!doc->Corners(l0, c))
         return;
      auto pull = [&](int k, float x, float y) { l->warp[k * 2] += x; l->warp[k * 2 + 1] += y; };
      const int h = sOp.warpHandle;
      if (h % 2 == 0)
      {
         const int k = h == 0 ? 0 : h == 2 ? 1 : h == 4 ? 2 : 3;   // handle -> corner (TL TR BR BL)
         if (!sOp.perspective)
            pull(k, dx, dy);
         else if (std::fabs(dx) >= std::fabs(dy))
         {
            pull(k, dx, 0.0f);   // along the top or bottom edge: its other corner moves the other way
            pull(k ^ 1, -dx, 0.0f);
         }
         else
         {
            pull(k, 0.0f, dy);   // along the left or right edge
            pull(3 - k, 0.0f, -dy);
         }
      }
      else
      {
         // An edge slides along itself: both its corners move by the drag along the edge.
         const int a = h == 1 ? 0 : h == 3 ? 1 : h == 5 ? 2 : 3, b = (a + 1) % 4;
         float ex = c[b * 2] - c[a * 2], ey = c[b * 2 + 1] - c[a * 2 + 1];
         const float len = std::max(1e-3f, std::sqrt(ex * ex + ey * ey));
         ex /= len;
         ey /= len;
         const float t = dx * ex + dy * ey;
         pull(a, ex * t, ey * t);
         pull(b, ex * t, ey * t);
      }
      Touch();
   }

   void ScaleActive(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      if (l == nullptr || l->kind == Kind::Group || l->kind == Kind::Fill || l->kind == Kind::Adjust)
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
      // The fixed point stays where it was on the document.
      float fx0, fy0;
      Apply(m0, ox * bw, oy * bh, fx0, fy0);
      // Magnetic: the dragged handle's edge snaps to the page, guides and other layers' edges and centres, the same
      // targets a move uses. Upright 2D layers only (a turned box has no edge along the page's axes).
      sSnapX.clear();
      sSnapY.clear();
      const bool snap = doc->Doc().snap != (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper);
      if (snap && !l0.is3d && std::fabs(std::remainder(l0.angle, 360.0f)) < 0.01f)
      {
         float hx0, hy0;
         Apply(m0, sOp.hx * bw, sOp.hy * bh, hx0, hy0);
         const std::vector<int> moving = { l0.id };
         float r[2] = { 1.0f, 1.0f };
         bool got[2] = { false, false };
         for (int ax = 0; ax < 2; ax++)
         {
            const float h = ax == 0 ? sOp.hx : sOp.hy, k = ax == 0 ? kx : ky, f = ax == 0 ? fx0 : fy0, h0 = ax == 0 ? hx0 : hy0;
            if (std::fabs(h - 0.5f) < 0.1f || std::fabs(h0 - f) < 1e-3f)
               continue;
            const float now = f + (h0 - f) * k;
            const float e[3] = { now, now, now };
            const float off = SnapAxis(doc, ax, e, moving);
            if (off != 0.0f && std::fabs(now - f) > 1e-3f)
            {
               r[ax] = (now + off - f) / (now - f);
               got[ax] = true;
            }
         }
         if (corner && !ImGui::GetIO().KeyShift)
         {
            // Proportions held: one snap drives both; the other axis' lines no longer sit on an edge.
            const int use = got[0] && got[1] ? (std::fabs(r[0] - 1.0f) < std::fabs(r[1] - 1.0f) ? 0 : 1) : (got[0] ? 0 : 1);
            if (got[use])
            {
               r[1 - use] = r[use];
               (use == 0 ? sSnapY : sSnapX).clear();
            }
         }
         kx *= r[0];
         ky *= r[1];
      }
      kx = std::fabs(kx) < 1e-3f ? 1e-3f : kx;
      ky = std::fabs(ky) < 1e-3f ? 1e-3f : ky;
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
      if (l == nullptr || l->kind == Kind::Group || l->kind == Kind::Fill || l->kind == Kind::Adjust)
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

   // ---- the 3D gizmo (3D layers) -----------------------------------------

   constexpr unsigned kGizmoHex[3] = { 0xFF3352, 0x8BDC00, 0x2890FF };   // the 3D View's axis colours
   constexpr float kGizmoPx = 80.0f;

   bool GizmoScreen(Scene2DNode* doc, const float p[3], ImVec2& out)
   {
      float x, y;
      if (!doc->ProjectPoint(p, x, y))
         return false;
      out = ToScreen(doc, x, y);
      return true;
   }

   // The three directions the gizmo shows for the mode: world X / Y-up / Z for move, the rotation axes for
   // rotate, the card's right / up / normal for scale.
   void GizmoAxes(Scene2DNode* doc, const Layer& l, int mode, float ax[3][3])
   {
      float rot[3][3], card[2][3];
      doc->CardAxes(l, rot, card);
      if (mode == 1)
         std::memcpy(ax, rot, sizeof(rot));
      else if (mode == 2)
      {
         for (int i = 0; i < 3; i++)
         {
            ax[0][i] = card[0][i];
            ax[1][i] = -card[1][i];
         }
         ax[2][0] = card[0][1] * card[1][2] - card[0][2] * card[1][1];
         ax[2][1] = card[0][2] * card[1][0] - card[0][0] * card[1][2];
         ax[2][2] = card[0][0] * card[1][1] - card[0][1] * card[1][0];
      }
      else
      {
         const float w[3][3] = { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 } };
         std::memcpy(ax, w, sizeof(w));
      }
   }

   // World length of the gizmo's arms: a fixed size on screen whatever the depth.
   float GizmoLen(Scene2DNode* doc, const Layer& l)
   {
      const float p[3] = { l.x, l.y, l.z };
      float x, y, depth = 1.0f;
      doc->ProjectPoint(p, x, y, &depth);
      return kGizmoPx / std::max(sZoom, 1e-4f) * std::max(depth, 1.0f) / std::max(doc->Camera().zoom, 1.0f);
   }

   float SegDist2D(ImVec2 p, ImVec2 a, ImVec2 b)
   {
      const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
      const float t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
      return std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t);
   }

   // Draws the gizmo; returns the part under the pointer (0..2 axes, 3 centre) or -1.
   int DrawGizmo3D(ImDrawList* dl, Scene2DNode* doc, const Layer& l, ImVec2 mouse, int held)
   {
      const float at[3] = { l.x, l.y, l.z };
      ImVec2 c;
      if (!GizmoScreen(doc, at, c))
         return -1;
      const int mode = sGizmoMode;
      float ax[3][3];
      GizmoAxes(doc, l, mode, ax);
      const float len = GizmoLen(doc, l);
      const ImU32 white = IM_COL32(255, 255, 255, 255);
      auto tone = [&](int id, float alpha) { return held == id || (held < 0 && sGizmoHot == id) ? white : Hex(kGizmoHex[id], alpha); };
      int hot = -1;
      float best = 9.0f;
      if (mode == 1)
      {
         for (int a = 0; a < 3; a++)
         {
            // Two vectors across the ring's plane.
            const float* n = ax[a];
            float u[3] = { -n[1], n[0], 0.0f };
            if (std::fabs(n[2]) > 0.9f)
            {
               u[0] = 0.0f;
               u[1] = -n[2];
               u[2] = n[1];
            }
            const float ul = std::max(1e-6f, std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]));
            for (float& v : u) v /= ul;
            const float v[3] = { n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0] };
            ImVec2 prev;
            bool prevOk = false;
            for (int i = 0; i <= 64; i++)
            {
               const float t = (float)i / 64.0f * 2.0f * kPi;
               float p[3];
               for (int k = 0; k < 3; k++)
                  p[k] = at[k] + (u[k] * std::cos(t) + v[k] * std::sin(t)) * len;
               ImVec2 q;
               const bool ok = GizmoScreen(doc, p, q);
               if (ok && prevOk)
               {
                  dl->AddLine(prev, q, tone(a, 1.0f), 2.5f);
                  const float d = SegDist2D(mouse, prev, q);
                  if (d < best)
                  {
                     best = d;
                     hot = a;
                  }
               }
               prev = q;
               prevOk = ok;
            }
         }
         return hot;
      }
      for (int a = 0; a < 3; a++)
      {
         if (mode == 2 && a == 2 && !doc->IsObjectLayer(l))
            continue;   // a card has no thickness to scale
         const float tip[3] = { at[0] + ax[a][0] * len, at[1] + ax[a][1] * len, at[2] + ax[a][2] * len };
         ImVec2 s;
         if (!GizmoScreen(doc, tip, s))
            continue;
         const float dx = s.x - c.x, dy = s.y - c.y, sl = std::hypot(dx, dy);
         if (sl < 10.0f)
         {
            // Pointing at the camera: a dot that drags with the pointer's up and down.
            dl->AddCircleFilled(ImVec2(c.x + 16.0f, c.y - 16.0f), 5.0f, tone(a, 1.0f));
            const float d = std::hypot(mouse.x - c.x - 16.0f, mouse.y - c.y + 16.0f);
            if (d < best)
            {
               best = d;
               hot = a;
            }
            continue;
         }
         const ImVec2 dir(dx / sl, dy / sl), side(-dy / sl, dx / sl);
         const ImU32 col = tone(a, 1.0f);
         dl->AddLine(ImVec2(c.x + dir.x * 12.0f, c.y + dir.y * 12.0f), s, col, 2.5f);
         if (mode == 0)
            dl->AddTriangleFilled(ImVec2(s.x + dir.x * 14.0f, s.y + dir.y * 14.0f), ImVec2(s.x + side.x * 5.5f, s.y + side.y * 5.5f),
                                  ImVec2(s.x - side.x * 5.5f, s.y - side.y * 5.5f), col);
         else
            dl->AddRectFilled(ImVec2(s.x - 5, s.y - 5), ImVec2(s.x + 5, s.y + 5), col);
         const float d = SegDist2D(mouse, c, ImVec2(s.x + dir.x * 14.0f, s.y + dir.y * 14.0f));
         if (d < best && std::hypot(mouse.x - c.x, mouse.y - c.y) > 11.0f)
         {
            best = d;
            hot = a;
         }
      }
      dl->AddCircle(c, 8.0f, held == 3 || (held < 0 && sGizmoHot == 3) ? white : IM_COL32(255, 255, 255, 190), 24, 1.5f);
      if (std::hypot(mouse.x - c.x, mouse.y - c.y) <= 10.0f)
         hot = 3;
      return hot;
   }

   void DragGizmo3D(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      if (l == nullptr)
         return;
      const Layer& l0 = sOp.l0;
      const int a = sOp.gizmoAxis, mode = sGizmoMode;
      const float at[3] = { l0.x, l0.y, l0.z };
      ImVec2 c;
      if (!GizmoScreen(doc, at, c))
         return;
      float ax[3][3];
      GizmoAxes(doc, l0, mode, ax);
      const float len = GizmoLen(doc, l0);
      const float mx = mouse.x - sOp.start.x, my = mouse.y - sOp.start.y;
      const bool snap = ImGui::GetIO().KeyShift;
      *l = l0;
      // The axis on screen: how far the pointer moved along it, in arm lengths.
      auto along = [&](int k, float& t) {
         const float tip[3] = { at[0] + ax[k][0] * len, at[1] + ax[k][1] * len, at[2] + ax[k][2] * len };
         ImVec2 s;
         if (!GizmoScreen(doc, tip, s))
            return false;
         const float dx = s.x - c.x, dy = s.y - c.y, l2 = dx * dx + dy * dy;
         t = l2 >= 100.0f ? (mx * dx + my * dy) / l2 : -my / kGizmoPx;
         return true;
      };
      if (mode == 0)
      {
         if (a == 3)
         {
            float depth = 1.0f, x, y;
            doc->ProjectPoint(at, x, y, &depth);
            const float k = std::max(depth, 1.0f) / std::max(doc->Camera().zoom, 1.0f) / std::max(sZoom, 1e-4f);
            l->x = l0.x + mx * k;
            l->y = l0.y + my * k;
         }
         else
         {
            float t = 0.0f;
            if (!along(a, t))
               return;
            float d = t * len;
            if (snap)
               d = std::round(d / 10.0f) * 10.0f;
            l->x = l0.x + ax[a][0] * d;
            l->y = l0.y + ax[a][1] * d;
            l->z = l0.z + ax[a][2] * d;
         }
      }
      else if (mode == 1)
      {
         const float a0 = std::atan2(sOp.start.y - c.y, sOp.start.x - c.x), a1 = std::atan2(mouse.y - c.y, mouse.x - c.x);
         float deg = (a1 - a0) * 180.0f / kPi;
         // Turning the way the pointer goes round: flip when the axis points away from the camera.
         const Scene2DNode::CompCam cam = doc->Camera();
         const float facing = ax[a][0] * cam.rot[6] + ax[a][1] * cam.rot[7] + ax[a][2] * cam.rot[8];
         if (facing < 0.0f)
            deg = -deg;
         float* v = a == 0 ? &l->rx : a == 1 ? &l->ry : &l->angle;
         float r = (a == 0 ? l0.rx : a == 1 ? l0.ry : l0.angle) + deg;
         if (snap)
            r = std::round(r / 15.0f) * 15.0f;
         *v = std::fmod(r + 540.0f, 360.0f) - 180.0f;
      }
      else
      {
         float k = 1.0f;
         if (a == 3)
         {
            const float d0 = std::max(1.0f, std::hypot(sOp.start.x - c.x, sOp.start.y - c.y));
            k = std::hypot(mouse.x - c.x, mouse.y - c.y) / d0;
            l->sx = l0.sx * k;
            l->sy = l0.sy * k;
         }
         else
         {
            float t = 0.0f;
            if (!along(a, t))
               return;
            k = std::max(0.01f, 1.0f + t);
            if (snap)
               k = std::max(0.1f, std::round(k * 10.0f) / 10.0f);
            float* v = a == 0 ? &l->sx : a == 1 ? &l->sy : &l->sz;
            *v = (a == 0 ? l0.sx : a == 1 ? l0.sy : l0.sz) * k;
         }
      }
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
   bool sLockScale = true;         // Transform tab: W and H % move together
   int sShapeKind = 0;             // 0 rectangle, 1 rounded, 2 ellipse, 3 polygon, 4 star, 5 line
   int sVecTarget = 0;             // 0 the layer's items, 1 its vector mask
   int sItem = -1, sItemLayer = 0; // the picked item and the layer it is in
   int sPenItem = -1;              // the open path the pen adds to
   int sAnchor = -1, sPart = 0;    // node tool: 0 anchor, 1 in handle, 2 out handle, 3 box corner, 4 the whole item
   Shape sItem0;                   // the item as the drag began
   std::string sFont;
   float sTextSize = 0.0f, sLeading = 1.2f, sTracking = 0.0f;
   int sTextAlign = 0;
   bool sBold = false, sItalic = false, sUnder = false, sCaps = false;
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
         l->bold = sBold;
         l->italic = sItalic;
         l->underline = sUnder;
         l->caps = sCaps;
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
      // Type in place: the layer itself shows the text, so the input sits invisibly over the layer's box and
      // only a caret is drawn (one copy of the text, not two).
      const ImVec2 bmn(std::min(std::min(c[0].x, c[1].x), std::min(c[2].x, c[3].x)), std::min(std::min(c[0].y, c[1].y), std::min(c[2].y, c[3].y)));
      const ImVec2 bmx(std::max(std::max(c[0].x, c[1].x), std::max(c[2].x, c[3].x)), std::max(std::max(c[0].y, c[1].y), std::max(c[2].y, c[3].y)));
      const float w = std::max(40.0f, bmx.x - bmn.x), h = std::max(ImGui::GetFrameHeight(), bmx.y - bmn.y);
      const ImVec2 at = bmn;
      const ImVec2 keep = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(at);
      ImGui::PushID(sTextEdit);
      if (sTextFocus)
      {
         ImGui::SetKeyboardFocusHere();
         sTextFocus = false;
      }
      const ImU32 clear = IM_COL32(0, 0, 0, 0);
      ImGui::PushStyleColor(ImGuiCol_Text, clear);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, clear);
      ImGui::PushStyleColor(ImGuiCol_Border, clear);
      ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, clear);
      ImGui::PushStyleColor(ImGuiCol_InputTextCursor, clear);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      const bool changed = ImGui::InputTextMultiline("##v2dtextedit", sTextBuf, sizeof(sTextBuf), ImVec2(w, h),
                                                     ImGuiInputTextFlags_NoHorizontalScroll);
      const bool editing = ImGui::IsItemActive();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(5);
      if (editing && std::fmod(ImGui::GetTime(), 1.0) < 0.6)
      {
         // Caret at the end of the last line, on the layer's right edge.
         int lines = 1;
         for (const char* q = sTextBuf; *q; q++)
            lines += *q == '\n';
         const float f = (float)(lines - 1) / (float)lines;
         const ImVec2 top(c[1].x + (c[2].x - c[1].x) * f + 2.0f, c[1].y + (c[2].y - c[1].y) * f);
         dl->AddLine(top, ImVec2(c[2].x + 2.0f, c[2].y), Hex(kOutline), 1.5f);
      }
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
      if (MenuParts::BeginContextItem("##v2dcanvasmenu"))
      {
         const bool any = !sSel.empty();
         if (MenuParts::Item(T("Copy"), Sc("Ctrl+C"), false, any)) sPending.what = Pending::Copy;
         if (MenuParts::Item(T("Cut"), Sc("Ctrl+X"), false, any)) sPending.what = Pending::Cut;
         if (MenuParts::Item(T("Paste"), Sc("Ctrl+V"), false, !sClip.empty())) sPending.what = Pending::Paste;
         if (MenuParts::Item(T("Duplicate"), Sc("Ctrl+J"), false, any)) sPending.what = Pending::Duplicate;
         if (MenuParts::Item(T("Delete"), "Delete", false, any)) sPending.what = Pending::Delete;
         MenuParts::Separator();
         NodeChoiceMenu(T("Add Node as Layer"), doc, Pending::AddNode, 0);
         if (Layer* ra = Active(doc); ra != nullptr && ra->kind == Kind::Node)
            NodeChoiceMenu(T("Replace with"), doc, Pending::ReplaceNode, ra->uid);
         MenuParts::Separator();
         if (MenuParts::Item(T("Group"), Sc("Ctrl+G"), false, any)) sPending.what = Pending::Group;
         if (MenuParts::Item(T("Bring to Front"), Sc("Shift+Ctrl+]"), false, any)) Restack(doc, 3);
         if (MenuParts::Item(T("Bring Forward"), Sc("Ctrl+]"), false, any)) Restack(doc, 1);
         if (MenuParts::Item(T("Send Backward"), Sc("Ctrl+["), false, any)) Restack(doc, 0);
         if (MenuParts::Item(T("Send to Back"), Sc("Shift+Ctrl+["), false, any)) Restack(doc, 2);
         MenuParts::Separator();
         if (MenuParts::Item(T("Select All"), Sc("Ctrl+A")))
         {
            sSel.clear();
            for (const Layer& l : doc->Layers())
               if (l.parent == 0)
                  sSel.push_back(l.id);
         }
         if (MenuParts::Item(T("Fit on Screen"), Sc("Ctrl+0"))) FitView(doc);
         if (MenuParts::Item(T("Actual Pixels"), Sc("Ctrl+1")))
         {
            sZoom = 1.0f;
            sPan = ImVec2(0, 0);
         }
         MenuParts::EndPopup();
      }
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

      // Symmetry guides while a painting tool is up: the mirror axes, or the radial spokes, through the centre.
      if (d.symmetry > 0 && (sTool == kBrush || sTool == kEraser || RetouchTool(sTool)))
      {
         const float cx = d.w * 0.5f, cy = d.h * 0.5f;
         const ImU32 col = Hex(kGuide, 0.7f);
         if (d.symmetry == 1 || d.symmetry == 3)
            dl->AddLine(ToScreen(doc, cx, 0), ToScreen(doc, cx, (float)d.h), col);
         if (d.symmetry == 2 || d.symmetry == 3)
            dl->AddLine(ToScreen(doc, 0, cy), ToScreen(doc, (float)d.w, cy), col);
         if (d.symmetry == 4)
         {
            const int n = std::clamp(d.symN, 2, 32);
            const float r = std::hypot(cx, cy);
            for (int k = 0; k < n; k++)
            {
               const float a = 2.0f * 3.14159265f * k / n - 1.5707963f;
               dl->AddLine(ToScreen(doc, cx, cy), ToScreen(doc, cx + std::cos(a) * r, cy + std::sin(a) * r), col);
            }
         }
      }

      const ImVec2 mouse = io.MousePos;
      sMouseIn = hovered;
      sMouseDoc = ToDoc(doc, mouse);
      Layer* active = Active(doc);
      const bool canTransform = sTool == kMove && active != nullptr && active->kind != Kind::Group && active->kind != Kind::Fill && active->kind != Kind::Adjust && sSel.size() == 1 &&
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
               if (l->kind == Kind::Fill || l->kind == Kind::Adjust)
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
      const bool gizmo3d = canTransform && active->is3d;
      sGizmoHot = -1;
      if (gizmo3d)
      {
         const int h = DrawGizmo3D(dl, doc, *active, mouse, sOp.kind == 16 ? sOp.gizmoAxis : -1);
         sGizmoHot = hovered && sOp.kind == 0 ? h : -1;
      }
      else if (canTransform)
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
      if (sOp.kind == 1 || sOp.kind == 2)
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
      if (sOp.kind == 17)
      {
         const ImVec2 p0 = ToScreen(doc, sOp.docStart.x, sOp.docStart.y);
         dl->AddRectFilled(p0, mouse, IM_COL32(80, 160, 255, 40));
         dl->AddRect(p0, mouse, IM_COL32(80, 160, 255, 220), 0.0f, 0, 1.0f);
      }
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
      // Two fingers never pan: they zoom, softly, about the pointer (a pinch arrives as Ctrl + wheel and zooms a
      // little faster). Panning is the Hand tool, Space-drag, middle-drag, or Shift + wheel.
      if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
      {
         if (io.KeyShift)
         {
            sPan.x += (io.MouseWheelH + io.MouseWheel) * 12.0f;
         }
         else
         {
            const bool pinch = io.KeyCtrl || io.KeySuper || io.KeyAlt;
            sZoomVel += io.MouseWheel * (pinch ? 0.10f : 0.045f);
            sZoomAt = mouse;
         }
      }
      if (std::fabs(sZoomVel) > 0.0005f)
      {
         // Ease the zoom in over a few frames so a notched mouse wheel glides instead of jumping.
         const float step = sZoomVel * 0.35f;
         ZoomAt(doc, std::exp(step), sZoomAt);
         sZoomVel -= step;
      }
      else
         sZoomVel = 0.0f;

      // Cursor hints.
      int guideAxis = 0;
      const int hoverGuide = sTool == kMove && hovered ? HoverGuide(doc, mouse, guideAxis) : -1;
      const int hoverHandle = canTransform && hovered && !gizmo3d ? HoverHandle(doc, *active, mouse) : -1;
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
      if (hovered && !panTool && hoverHandle < 0 && hoverGuide < 0 && sGizmoHot < 0 && !BrushTool(sTool))
      {
         dl->PushClipRect(sViewMn, sViewMx, true);
         DrawToolCursor(dl, mouse);
         dl->PopClipRect();
      }
      if (hovered && (sTool == kClone || sTool == kHeal) && sHaveCloneSrc)
      {
         // Where the stamp copies from: the source point, or (once aligned) the pointer plus the offset.
         const ImVec2 src = sCloneOffsetSet ? ToScreen(doc, sMouseDoc.x + sCloneOffset.x, sMouseDoc.y + sCloneOffset.y)
                                            : ToScreen(doc, sCloneSrc.x, sCloneSrc.y);
         dl->PushClipRect(sViewMn, sViewMx, true);
         for (int pass = 0; pass < 2; pass++)
         {
            const ImU32 col = pass == 0 ? IM_COL32(0, 0, 0, 170) : IM_COL32(255, 255, 255, 255);
            const float th = pass == 0 ? 3.0f : 1.0f;
            dl->AddLine(ImVec2(src.x - 7, src.y), ImVec2(src.x + 7, src.y), col, th);
            dl->AddLine(ImVec2(src.x, src.y - 7), ImVec2(src.x, src.y + 7), col, th);
            dl->AddCircle(src, 4.0f, col, 16, th);
         }
         dl->PopClipRect();
      }
      if (hovered && !panTool && BrushTool(sTool) && !io.KeyAlt && (sOp.kind == 0 || sOp.kind == 7))
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
         else if ((sTool == kClone || sTool == kHeal) && io.KeyAlt)
         {
            sCloneSrc = sMouseDoc;
            sHaveCloneSrc = true;
            sCloneOffsetSet = false;
         }
         else if (sTool == kPick || ((sTool == kBrush || sTool == kFill) && io.KeyAlt))
         {
            // The eyedropper; Alt with the brush or bucket picks too. Alt on the eyedropper sets the background.
            sOp = Op();
            sOp.kind = 11;
            sOp.selOp = sTool == kPick && io.KeyAlt ? 1 : 0;
         }
         else if (BrushTool(sTool))
            BeginStroke(doc, sMouseDoc, io.KeyShift && !RetouchTool(sTool));
         else if (sTool == kFill)
            FillAt(doc, sMouseDoc);
         else if (sTool == kGradient)
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
            if (sGizmoHot >= 0)
            {
               BeginOp(doc, 16, mouse, sGizmoMode == 0 ? "Move layer" : sGizmoMode == 1 ? "Rotate layer" : "Scale layer");
               sOp.gizmoAxis = sGizmoHot;
            }
            else if (hoverHandle >= 0 && hoverHandle < 8 && (io.KeyCtrl || io.KeySuper) && CanWarp(doc))
            {
               // Cmd/Ctrl: a corner distorts (with Alt+Shift, perspective), an edge skews.
               const bool corner = hoverHandle % 2 == 0;
               BeginOp(doc, 18, mouse, corner ? "Distort layer" : "Skew layer");
               sOp.warpHandle = hoverHandle;
               sOp.perspective = corner && io.KeyAlt && io.KeyShift;
               sOp.docStart = ToDoc(doc, mouse);
            }
            else if (hoverHandle >= 0 && hoverHandle < 8)
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
               else if (!io.KeyShift)
                  sSel.clear();
               if (hit == 0)
               {
                  // Dragging over empty canvas rubber-bands layers; Shift adds to the selection.
                  BeginOp(doc, 17, mouse, nullptr);
                  sOp.docStart = sMouseDoc;
                  sOp.selOp = io.KeyShift ? 1 : 0;
                  sOp.before.clear();
               }
               else if (!sSel.empty())
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
         else if (sOp.kind == 16)
            DragGizmo3D(doc, mouse);
         else if (sOp.kind == 18)
            WarpActive(doc, mouse);
         else if (sOp.kind == 4)
            sPan = ImVec2(sOp.pan0.x + mouse.x - sOp.start.x, sOp.pan0.y + mouse.y - sOp.start.y);
         else if (sOp.kind == 5)
            DragCrop(doc, mouse);
         else if (sOp.kind == 7)
         {
            if (sSurfaceStroke)
               doc->SurfacePaintTo(sMouseDoc.x, sMouseDoc.y, PenNow());
            else
            {
               if (sTool == kBrush)
                  doc->PaintFollow(sBrush);
               doc->PaintTo(sMouseDoc.x, sMouseDoc.y, PenNow());
            }
         }
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
         {
            // On a painted 3D surface the eyedropper reads the paint, not the lit picture.
            float c[4];
            Layer* a = Active(doc);
            if (a != nullptr && sSurfacePaint && a->HasSkin() && doc->SurfaceColor(a->id, sMouseDoc.x, sMouseDoc.y, c))
               std::copy(c, c + 3, sOp.selOp == 1 ? sBg : sFg);
            else
               doc->Pick(sMouseDoc.x, sMouseDoc.y, sOp.selOp == 1 ? sBg : sFg);
         }
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
            else if (sOp.kind == 17)
               MarqueeLayers(doc, sOp.docStart, sMouseDoc, true);   // the press already cleared it unless Shift
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

   // Between two groups of a bar: space_2, a 1 px hairline as tall as the chips' inner area, space_2 (Photopea's divider).
   void BarDivider()
   {
      const float top = ImGui::GetItemRectMin().y;
      ImGui::SameLine(0.0f, tok::space_2);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float x = std::round(p.x) + 0.5f;
      ImGui::GetWindowDrawList()->AddLine(ImVec2(x, top + 6.0f), ImVec2(x, top + sRowH - 6.0f), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
      ImGui::SetCursorScreenPos(ImVec2(p.x, top));
      ImGui::Dummy(ImVec2(1.0f, sRowH));
      ImGui::SameLine(0.0f, tok::space_2);
   }

   // ---- the pixel tools' context bar ---------------------------------------

   void BarSlider(const char* id, float* v, float lo, float hi, const char* fmt, float w = 0.0f)
   {
      // As wide as its longest text ("Hardness 100%") plus the well's padding, so bars carry no empty wells.
      char a[64], b[64];
      std::snprintf(a, sizeof a, fmt, lo);
      std::snprintf(b, sizeof b, fmt, hi);
      const float fit = std::max(ImGui::CalcTextSize(a).x, ImGui::CalcTextSize(b).x) + 2.0f * tok::space_2 + 4.0f;
      // One width for every bar slider (wider only when its text needs it), so bars line up tool to tool.
      ImGui::SetNextItemWidth(w > 0.0f ? std::max(w, fit) : fit);
      FieldWell::Slider(id, v, lo, hi, fmt);
      FormParts::Inline(0.0f, tok::space_1);
   }

   void SelOpChips(const char* idBase)
   {
      static const char* kGlyphs[4] = { IconsInfinite::SelNew, IconsInfinite::SelAdd, IconsInfinite::SelSubtract, IconsInfinite::SelIntersect };
      static const char* kTips[4] = { "New selection", "Add to selection (Shift)", "Subtract from selection (Alt)", "Intersect with selection (Shift+Alt)" };
      for (int i = 0; i < 4; i++)
      {
         if (i > 0)
            FormParts::Inline(0.0f, tok::space_1);
         const std::string id = std::string("##") + idBase + std::to_string(i);
         if (IconChip(id.c_str(), kGlyphs[i], sSelOp == i, T(kTips[i])))
            sSelOp = i;
      }
   }

   // Expand, contract, smooth or border the live selection by sSelModPx.
   void SelModChips(Scene2DNode* doc)
   {
      // Always on the bar, greyed without a selection, so the bar never changes shape under the pointer.
      BarDivider();
      ImGui::BeginDisabled(!doc->HasSelection());
      BarSlider("##v2dselmod", &sSelModPx, 1.0f, 100.0f, "%.0f px", kWellL);
      static const char* kOps[4] = { "Expand##v2dsm0", "Contract##v2dsm1", "Smooth##v2dsm2", "Border##v2dsm3" };
      static const char* kUndo[4] = { "Expand selection", "Contract selection", "Smooth selection", "Border selection" };
      for (int i = 0; i < 4; i++)
      {
         if (i > 0)
            FormParts::Inline(0.0f, tok::space_1);
         if (TextChip(L(kOps[i]), false, nullptr))
         {
            PushUndoCheckpoint(kUndo[i]);
            doc->ModifySelection(i, (int)std::lround(sSelModPx));
            Touch();
         }
      }
      ImGui::EndDisabled();
   }

   // A tool's less-used settings: one icon at the bar's end opens them as a vertical form (label, well), the way
   // Photoshop's brush settings sit behind one button. Bars keep only the everyday controls, so none overflows.
   // A More.. form: side-panel rows (kSideChip tall, 4 px apart), a 150 px well column, capped height with a scroll.
   struct MoreRows
   {
      float keepW;
      MoreRows() : keepW(sFieldW)
      {
         sFieldW = 150.0f;
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, std::max(1.0f, (kSideChip - ImGui::GetFontSize()) * 0.5f)));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 4.0f));
      }
      ~MoreRows()
      {
         ImGui::PopStyleVar(2);
         sFieldW = keepW;
      }
   };
   struct MorePopupScope
   {
      MorePopupScope() { ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, std::max(200.0f, ImGui::GetMainViewport()->Size.y * 0.6f))); }
   };
   template <class F> void BarMore(const char* id, F&& body)
   {
      const std::string pop = std::string(id) + "pop";
      const bool open = ImGui::IsPopupOpen(pop.c_str());
      const std::string chip = std::string(L("More..")) + id;
      if (TextChip(chip.c_str(), open, T("More settings")))
         ImGui::OpenPopup(pop.c_str());
      MorePopupScope scope;
      if (MenuParts::BeginPopup(pop.c_str()))
      {
         MoreRows rows;
         body();
         MenuParts::EndPopup();
      }
   }

   void MoreHeader(const char* text, bool first = false)
   {
      if (!first)
         ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      ImGui::TextDisabled("%s", text);
   }

   // A percent row in a BarMore form: v is 0..1 (or 0..hi/100), shown as percent.
   void MorePct(const char* label, float* v, float lo, float hi)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      float pct = *v * 100.0f;
      if (FieldWell::Slider(FormParts::Id(label).c_str(), &pct, lo, hi, "%.0f%%"))
         *v = pct / 100.0f;
   }

   void MoreSlider(const char* label, float* v, float lo, float hi, const char* fmt)
   {
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      FieldWell::Slider(FormParts::Id(label).c_str(), v, lo, hi, fmt);
   }

   void MoreCheck(const char* label, bool* v)
   {
      const float rowY = ImGui::GetCursorPosY();
      ImGui::SetCursorPosY(rowY + (ImGui::GetFrameHeight() - CheckBox::kSize) * 0.5f);
      bool on = *v;
      if (CheckBox::Box(FormParts::Id(label).c_str(), &on))
         *v = on;
      FormParts::Inline(0.0f, tok::space_2);
      ImGui::SetCursorPosY(rowY);
      ImGui::AlignTextToFramePadding();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::TextColored(ImVec4(t.x, t.y, t.z, 0.72f), "%s", label);
      if (ImGui::IsItemClicked())
         *v = !*v;
   }

   void MatchChips()
   {
      float tol = (float)sTolerance;
      BarSlider("##v2dtol", &tol, 0.0f, 255.0f, "Tolerance %.0f", kWellL);
      sTolerance = (int)std::lround(tol);
      if (TextChip(L("Contiguous##v2dcont"), sContiguous, T("Only touching pixels")))
         sContiguous = !sContiguous;
      FormParts::Inline(0.0f, tok::space_1);
      if (TextChip(L("All layers##v2dall"), sAllLayers, T("Sample the whole picture")))
         sAllLayers = !sAllLayers;
   }

   // Painting symmetry (Procreate's drawing guide, Krita's mirror): every stroke repeats across the axes or round
   // the centre. The document keeps it, so it saves with the patch; the canvas shows the guides while it is on.
   void SymmetryChip(Scene2DNode* doc)
   {
      Scene2DNode::Document& d = doc->Doc();
      static const char* const kNames[5] = { "Off", "Left-right", "Top-bottom", "Both", "Radial" };
      if (TextChip(L("Symmetry##v2dsym"), d.symmetry > 0, T("Repeat strokes across the picture")))
         ImGui::OpenPopup("##v2dsympop");
      MorePopupScope scope;
      if (MenuParts::BeginPopup("##v2dsympop"))
      {
         for (int i = 0; i < 5; i++)
            if (MenuParts::Choice(T(kNames[i]), d.symmetry == i) && d.symmetry != i)
            {
               PushUndoCheckpoint("Symmetry");
               d.symmetry = i;
               Touch();
            }
         if (d.symmetry == 4)
         {
            MoreRows rows;
            MenuParts::Separator();
            float n = (float)d.symN;
            FieldLabel(T("Spokes"));
            ImGui::SetNextItemWidth(sFieldW);
            if (FieldWell::Slider("##v2dsymn", &n, 2.0f, 32.0f, "%.0f"))
            {
               d.symN = std::clamp((int)std::lround(n), 2, 32);
               Touch();
            }
            UndoOnActivate("Symmetry spokes");
         }
         MenuParts::EndPopup();
      }
   }

   void DrawPixelBar(Scene2DNode* doc)
   {
      auto gap = []() { BarDivider(); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      Layer* a = Active(doc);
      if (RetouchTool(sTool))
      {
         BrushEngine::Brush& b = ToolBrush();
         BarSlider("##v2drsize", &b.size, 1.0f, 1000.0f, "Size %.0f px", kWellL);
         float hard = b.hardness * 100.0f, op = b.opacity * 100.0f;
         BarSlider("##v2drhard", &hard, 0.0f, 100.0f, "Hardness %.0f%%", kWellL);
         BarSlider("##v2drop", &op, 1.0f, 100.0f, "Strength %.0f%%", kWellL);
         b.hardness = hard / 100.0f;
         b.opacity = op / 100.0f;
         gap();
         SymmetryChip(doc);
         FormParts::Inline(0.0f, tok::space_1);
         BarMore("##v2drmore", [&]()
         {
            MoreHeader(T("Tip"), true);
            MorePct(T("Spacing"), &b.spacing, 1.0f, 200.0f);
            MorePct(T("Roundness"), &b.roundness, 5.0f, 100.0f);
            MoreSlider(T("Angle"), &b.angle, -180.0f, 180.0f, "%.0f\xC2\xB0");
            MorePct(T("Smoothing"), &b.smoothing, 0.0f, 100.0f);
            MoreHeader(T("Pen pressure"));
            MoreCheck(T("Size"), &b.pressureSize);
            MoreCheck(T("Strength"), &b.pressureOpacity);
         });
      }
      else if (sTool == kBrush || sTool == kEraser)
      {
         BrushEngine::Brush& b = ToolBrush();
         int& preset = sTool == kEraser ? sEraserPreset : sBrushPreset;
         const auto& presets = BrushEngine::Presets();
         ImGui::SetNextItemWidth(kWellL);
         const char* shown = preset >= 0 && preset < (int)presets.size() ? presets[preset].name : "";
         if (MenuParts::BeginCombo("##v2dpreset", T(shown)))
         {
            const char* group = nullptr;
            for (int i = 0; i < (int)presets.size(); i++)
            {
               const bool erase = presets[i].brush.mode == BrushEngine::kErase;
               if (erase != (sTool == kEraser))
                  continue;
               if (!erase && presets[i].group != nullptr && (group == nullptr || std::strcmp(group, presets[i].group) != 0))
               {
                  if (group != nullptr)
                     ImGui::Separator();
                  group = presets[i].group;
                  ImGui::TextDisabled("%s", T(group));
               }
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
         const bool brush = &b == &sBrush;   // the brush's numbers can follow a modulator (sound, LFO); the eraser's do not
         if (brush)
            ModNextParam(kBrushSizeParam);
         ModBegin();
         BarSlider("##v2dsize", &b.size, 1.0f, 1000.0f, "Size %.0f px", kWellL);
         ModEnd();
         float hard = b.hardness * 100.0f, op = b.opacity * 100.0f, flow = b.flow * 100.0f;
         BarSlider("##v2dhard", &hard, 0.0f, 100.0f, "Hardness %.0f%%", kWellL);
         if (brush)
            ModNextParam(kBrushOpacityParam);
         ModBegin();
         BarSlider("##v2dop", &op, 1.0f, 100.0f, "Opacity %.0f%%", kWellL);
         ModEnd();
         if (brush)
            ModNextParam(kBrushFlowParam);
         ModBegin();
         BarSlider("##v2dflow", &flow, 1.0f, 100.0f, "Flow %.0f%%", kWellL);
         ModEnd();
         b.hardness = hard / 100.0f;
         b.opacity = op / 100.0f;
         b.flow = flow / 100.0f;
         float smooth = b.smoothing * 100.0f;
         BarSlider("##v2dsmooth", &smooth, 0.0f, 100.0f, "Smoothing %.0f%%", kWellL);
         b.smoothing = smooth / 100.0f;
         gap();
         SymmetryChip(doc);
         FormParts::Inline(0.0f, tok::space_1);
         BarMore("##v2dbmore", [&]()
         {
            MoreHeader(T("Tip"), true);
            MorePct(T("Spacing"), &b.spacing, 1.0f, 200.0f);
            MorePct(T("Roundness"), &b.roundness, 5.0f, 100.0f);
            MoreSlider(T("Angle"), &b.angle, -180.0f, 180.0f, "%.0f\xC2\xB0");
            MoreCheck(T("Hard edge"), &b.pencil);
            MoreHeader(T("Dynamics"));
            MorePct(T("Scatter"), &b.jitter, 0.0f, 200.0f);
            MoreCheck(T("Follow stroke"), &b.follow);
            if (brush)
            {
               FieldLabel(T("Hue"));
               ImGui::SetNextItemWidth(sFieldW);
               ModNextParam(kBrushHueParam);
               ModBegin();
               FieldWell::Slider("##v2dhue", &sBrushHue, -180.0f, 180.0f, "%+.0f");
               ModEnd();
            }
            MoreHeader(T("Pen pressure"));
            MoreCheck(T("Size"), &b.pressureSize);
            MoreCheck(T("Opacity"), &b.pressureOpacity);
            MoreHeader(T("Paint"));
            if (brush)
               MoreCheck(T("Lock alpha"), &b.lockAlpha);
            ImGui::BeginDisabled(a == nullptr || !a->HasPixelMask());
            bool mask = sEditMask && a != nullptr && a->HasPixelMask();
            MoreCheck(T("Paint the mask"), &mask);
            if (a != nullptr && a->HasPixelMask())
               sEditMask = mask;
            ImGui::EndDisabled();
         });
      }
      else if (sTool == kFill)
         MatchChips();
      else if (sTool == kGradient)
      {
         static const char* kKinds[2] = { "Linear", "Radial" };
         for (int i = 0; i < 2; i++)
         {
            if (i > 0)
               next();
            const std::string id = std::string(L(kKinds[i])) + "##v2dfk" + std::to_string(i);
            if (TextChip(id.c_str(), (sFillKind == 2) == (i == 1), nullptr))
               sFillKind = i + 1;
         }
         gap();
         if (TextChip(L("Reverse##v2dgrev"), sGradReverse, T("Background colour to foreground")))
            sGradReverse = !sGradReverse;
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
         BarSlider("##v2dfeather", &sFeather, 0.0f, 250.0f, "Feather %.0f px", kWellL);
         SelModChips(doc);
      }
      else if (sTool == kWand)
      {
         SelOpChips("v2dwop");
         gap();
         MatchChips();
         SelModChips(doc);
      }
   }

   // ---- the vector and text tools' context bars ----------------------------

   // Edits the picked item when there is one, and always what the next item gets.
   void DrawVectorBar(Scene2DNode* doc)
   {
      auto gap = []() { BarDivider(); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      Shape* picked = PickedItem(doc);
      Shape& t = picked != nullptr ? *picked : sStyle;
      bool changed = false;
      auto undo = [&]() { if (picked != nullptr) PushUndoCheckpoint("Shape style"); };
      if (sTool == kShape)
      {
         static const char* kKinds[6] = { "Rectangle", "Rounded", "Ellipse", "Polygon", "Star", "Line" };
         ImGui::SetNextItemWidth(kWellL);
         if (MenuParts::BeginCombo("##v2dshapekind", T(kKinds[std::clamp(sShapeKind, 0, 5)])))
         {
            for (int i = 0; i < 6; i++)
               if (MenuParts::Choice(T(kKinds[i]), sShapeKind == i))
                  sShapeKind = i;
            MenuParts::EndCombo();
         }
         gap();
      }
      // Fill and stroke: the chip toggles, the dot beside it picks the colour (Photopea: "Fill [ ] Stroke [ ] 1 px").
      if (TextChip(L("Fill##v2dvfill"), t.fillOn, T("Fill on or off")))
      {
         undo();
         t.fillOn = !t.fillOn;
         changed = true;
      }
      next();
      if (FormParts::ColorDot("##v2dvfillc", t.fillC, std::min(18.0f, sRowH - 4.0f), sRowH))
      {
         // One undo step per colour edit, as ColorRow: the first change in a second pushes it.
         static double sAt = -1.0;
         if (ImGui::GetTime() - sAt > 1.0)
            undo();
         sAt = ImGui::GetTime();
         changed = true;
      }
      gap();
      if (TextChip(L("Stroke##v2dvstroke"), t.strokeOn, T("Stroke on or off")))
      {
         undo();
         t.strokeOn = !t.strokeOn;
         changed = true;
      }
      next();
      if (FormParts::ColorDot("##v2dvstrokec", t.strokeC, std::min(18.0f, sRowH - 4.0f), sRowH))
      {
         // One undo step per colour edit, as ColorRow: the first change in a second pushes it.
         static double sAt = -1.0;
         if (ImGui::GetTime() - sAt > 1.0)
            undo();
         sAt = ImGui::GetTime();
         changed = true;
      }
      next();
      ImGui::SetNextItemWidth(kWellS);
      changed |= FieldWell::Slider("##v2dvwidth", &t.width, 0.0f, 200.0f, "%.0f px");
      if (ImGui::IsItemActivated())
         undo();
      next();
      // Caps, joins, alignment and dashes live in one popup, as Photoshop's stroke options do.
      if (IconChip("##v2dvstyle", IconsInfinite::StrokeStyle, false, T("Stroke options")))
         ImGui::OpenPopup("##v2dvstylepop");
      if (MenuParts::BeginPopup("##v2dvstylepop"))
      {
         auto pick = [&](const char* title, int* v, std::initializer_list<const char*> names) {
            std::vector<const char*> n(names);
            ImGui::TextDisabled("%s", T(title));
            for (int i = 0; i < (int)n.size(); i++)
            {
               if (i > 0)
                  FormParts::Inline(0.0f, tok::space_1);
               const std::string id = std::string(T(n[i])) + "##" + title + std::to_string(i);
               if (TextChip(id.c_str(), *v == i, nullptr) && *v != i)
               {
                  undo();
                  *v = i;
                  changed = true;
               }
            }
         };
         pick("Cap", &t.cap, { "Butt", "Round", "Square" });
         pick("Join", &t.join, { "Miter", "Round", "Bevel" });
         pick("Align", &t.align, { "Centre", "Inside", "Outside" });
         ImGui::TextDisabled("%s", T("Dash"));
         ImGui::SetNextItemWidth(kWellL);
         changed |= FieldWell::Slider("##v2dvdash", &t.dash, 0.0f, 200.0f, t.dash > 0.0f ? "Dash %.0f" : "Solid");
         if (ImGui::IsItemActivated())
            undo();
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::BeginDisabled(t.dash <= 0.0f);
         ImGui::SetNextItemWidth(kWellL);
         changed |= FieldWell::Slider("##v2dvgap", &t.gap, 0.0f, 200.0f, "Gap %.0f");
         if (ImGui::IsItemActivated())
            undo();
         ImGui::EndDisabled();
         MenuParts::EndPopup();
      }
      gap();
      static const char* kOpGlyphs[4] = { IconsInfinite::SelAdd, IconsInfinite::SelSubtract, IconsInfinite::SelIntersect, IconsInfinite::SelExclude };
      static const char* kOpTips[4] = { "Combine shapes", "Subtract front shape", "Intersect shape areas", "Exclude overlapping shapes" };
      for (int i = 0; i < 4; i++)
      {
         if (i > 0)
            next();
         const std::string id = std::string("##v2dvop") + std::to_string(i);
         if (IconChip(id.c_str(), kOpGlyphs[i], t.op == i, T(kOpTips[i])) && t.op != i)
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
         ImGui::SetNextItemWidth(kWellL);
         changed |= FieldWell::Slider("##v2dvrad", &t.radius, 0.0f, 500.0f, "Radius %.0f");
         if (ImGui::IsItemActivated())
            undo();
      }
      if (poly && (picked != nullptr || sTool == kShape))
      {
         gap();
         float sides = (float)t.sides;
         ImGui::SetNextItemWidth(kWellL);
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
            ImGui::SetNextItemWidth(kWellL);
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

   // ---- font picker -----------------------------------------------------

   // Each family's name drawn in its own face: rendered once by the OS text engine, cached as a small white
   // texture tinted at draw time. A few new ones a frame, so opening the list never stalls.
   struct FontFace { GLuint tex = 0; float w = 0.0f, h = 0.0f, base = 0.0f; bool tried = false; };
   std::map<std::string, FontFace> sFaces;
   std::vector<std::string> sRecentFonts;   // last 5 picked, newest first

   // Rows of ink in an RGBA canvas: [y0, y1], and columns [x0, x1]. False when empty.
   bool InkBox(const std::vector<unsigned char>& px, int w, int h, int& x0, int& y0, int& x1, int& y1)
   {
      x0 = w, y0 = h, x1 = -1, y1 = -1;
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
            if (px[((size_t)y * w + x) * 4 + 3] > 8)
            {
               x0 = std::min(x0, x);
               x1 = std::max(x1, x);
               y0 = std::min(y0, y);
               y1 = std::max(y1, y);
            }
      return x1 >= x0;
   }

   // Each face is drawn at one cap height (the UI font's), on one baseline, so a list of very different
   // typefaces reads as one even column instead of jumping in size row to row.
   const FontFace* FaceFor(const std::string& fam, int& budget)
   {
      FontFace& f = sFaces[fam];
      if (f.tried)
         return f.tex != 0 ? &f : nullptr;
      if (budget <= 0)
         return nullptr;
      budget--;
      f.tried = true;
      const float scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.y);
      const float probe = 40.0f * scale;   // render big, then scale down to the row
      const int cw = (int)std::ceil(probe * 16.0f), ch = (int)std::ceil(probe * 2.0f);
      auto raster = [&](const std::string& text, std::vector<unsigned char>& buf)
      {
         buf.assign((size_t)cw * ch * 4, 0);
         Platform::TextRasterRequest req;
         req.text = text;
         req.fontName = fam;
         req.fontSize = probe;
         req.align = 1;
         req.posX = 0.5f;
         req.posY = 0.5f;
         req.wrapWidth = 1.0f;
         req.wrapHeight = 1.0f;
         float fitted = probe;
         Platform::RasterizeText(req, cw, ch, buf.data(), fitted);
      };
      std::vector<unsigned char> name, cap;
      raster(fam, name);
      raster("H", cap);
      int nx0, ny0, nx1, ny1, hx0, hy0, hx1, hy1;
      if (!InkBox(name, cw, ch, nx0, ny0, nx1, ny1))
         return nullptr;
      float capPx = probe * 0.7f, baseRow = (float)ny1;
      if (InkBox(cap, cw, ch, hx0, hy0, hx1, hy1) && hy1 > hy0)
      {
         capPx = (float)(hy1 - hy0 + 1);
         baseRow = (float)hy1 + 1.0f;
      }
      // Target: the UI font's cap height, in screen points.
      const float k = std::clamp(ImGui::GetFontSize() * 0.74f / capPx, 0.0f, 4.0f);   // probe px -> points
      const int w = nx1 - nx0 + 1, h = ny1 - ny0 + 1;
      std::vector<unsigned char> out((size_t)w * h * 4);
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            const unsigned char* sp = &name[((size_t)(ny0 + y) * cw + nx0 + x) * 4];
            unsigned char* dp = &out[((size_t)y * w + x) * 4];
            dp[0] = dp[1] = dp[2] = 255;   // white, tinted by the row's text colour
            dp[3] = sp[3];
         }
      glGenTextures(1, &f.tex);
      glBindTexture(GL_TEXTURE_2D, f.tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
      glGenerateMipmap(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
      f.w = (float)w * k;
      f.h = (float)h * k;
      f.base = (baseRow - (float)ny0) * k;   // image top to the baseline, points
      return &f;
   }

   // One row: the name in its own face on the row's baseline (the UI font until the face is ready).
   bool FontRow(const std::string& fam, const char* shownName, bool cur, int& budget)
   {
      ImGui::PushID(fam.c_str());
      const bool hit = MenuParts::Choice("##row", cur);
      ImGui::PopID();
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      if (!ImGui::IsRectVisible(a, b))
         return hit;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 col = cur ? ImGui::GetColorU32(app::AccentEmphasisSelected()) : ImGui::GetColorU32(ImGuiCol_Text);
      const float baseline = a.y + ImGui::GetFontBaked()->Ascent;
      const FontFace* f = fam.empty() ? nullptr : FaceFor(fam, budget);
      if (f != nullptr)
      {
         const float maxW = b.x - a.x;
         const float w = std::min(f->w, maxW);
         const float top = std::round(baseline - f->base);
         dl->PushClipRect(ImVec2(a.x, a.y - 8.0f), ImVec2(b.x, b.y + 8.0f), true);
         dl->AddImage((ImTextureID)(intptr_t)f->tex, ImVec2(a.x, top), ImVec2(a.x + w, top + f->h), ImVec2(0, 0), ImVec2(w / f->w, 1), col);
         dl->PopClipRect();
      }
      else
         dl->AddText(a, col, shownName);
      return hit;
   }

   void NoteRecentFont(const std::string& fam)
   {
      if (fam.empty())
         return;
      sRecentFonts.erase(std::remove(sRecentFonts.begin(), sRecentFonts.end(), fam), sRecentFonts.end());
      sRecentFonts.insert(sRecentFonts.begin(), fam);
      if (sRecentFonts.size() > 5)
         sRecentFonts.resize(5);
   }

   void DimHeader(const char* text)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(t.x, t.y, t.z, 0.5f));
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
   }

   // Font dropdown: search, Recent, All fonts (12 rows, scrolls), Add font... Hovering a row previews it on the
   // layer; Esc or a click outside puts the old font back.
   template <class Undo>
   bool FontPicker(const char* id, std::string& font, float width, Undo&& undo)
   {
      const auto& fonts = Platform::AvailableFontFamilies();
      static std::string sFontOrig;
      static std::string sOpenId;
      static char sFontFind[64] = "";
      const bool openHere = sOpenId == id;
      auto nameOf = [](const std::string& f) { return f.empty() ? std::string(T("Default")) : f; };
      bool changed = false;
      ImGui::SetNextItemWidth(width);
      ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 0.0f), ImVec2(300.0f, MenuParts::ListMaxH(MenuParts::kMaxListRows + 5)));
      if (MenuParts::BeginCombo(id, nameOf(openHere ? sFontOrig : font).c_str(), ImGuiComboFlags_HeightLargest))
      {
         // The bar's tile padding stops here: the search field and rows are the side panel's size.
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, std::max(1.0f, (kSideChip - ImGui::GetFontSize()) * 0.5f)));
         if (!openHere)
         {
            sOpenId = id;
            sFontOrig = font;
            sFontFind[0] = 0;
            ImGui::SetKeyboardFocusHere();
         }
         ImGui::SetNextItemWidth(-1.0f);
         FieldWell::InputText("##fontfind", sFontFind, sizeof(sFontFind));
         if (sFontFind[0] == 0)
         {
            const ImVec2 fa = ImGui::GetItemRectMin();
            const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::GetWindowDrawList()->AddText(ImVec2(fa.x + ImGui::GetStyle().FramePadding.x, fa.y + ImGui::GetStyle().FramePadding.y),
                                                ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.4f)), T("Search"));
         }
         ImGui::Dummy(ImVec2(0.0f, 2.0f));
         std::string want = sFontFind;
         std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         const std::string* hovered = nullptr;
         const std::string* picked = nullptr;
         int budget = 6;
         auto isCur = [&](const std::string& f) { return f == sFontOrig; };
         if (want.empty())
         {
            static const std::string kDefault;
            if (FontRow(kDefault, T("Default"), isCur(kDefault), budget))
               picked = &kDefault;
            if (ImGui::IsItemHovered())
               hovered = &kDefault;
         }
         if (want.empty() && !sRecentFonts.empty())
         {
            DimHeader(T("Recent"));
            for (const std::string& f : sRecentFonts)
            {
               if (FontRow(f, f.c_str(), isCur(f), budget))
                  picked = &f;
               if (ImGui::IsItemHovered())
                  hovered = &f;
            }
            DimHeader(T("All fonts"));
         }
         std::vector<int> rows;
         rows.reserve(fonts.size());
         int curRow = -1;
         for (int i = 0; i < (int)fonts.size(); i++)
         {
            if (!want.empty())
            {
               std::string lf = fonts[i];
               std::transform(lf.begin(), lf.end(), lf.begin(), [](unsigned char c) { return (char)std::tolower(c); });
               if (lf.find(want) == std::string::npos)
                  continue;
            }
            if (isCur(fonts[i]))
               curRow = (int)rows.size();
            rows.push_back(i);
         }
         const float rowH = MenuParts::kRow;
         // Default and Recent rows come out of the list's 12, so the whole popup stays one dropdown tall.
         const int extra = want.empty() ? 1 + (sRecentFonts.empty() ? 0 : (int)sRecentFonts.size() + 2) : 0;
         const int listRows = std::clamp(MenuParts::kMaxListRows - extra, 6, MenuParts::kMaxListRows);
         const float listH = std::min((float)rows.size(), (float)listRows) * rowH;
         // The list spans the popup edge to edge with the popup's own side padding, so its rows, washes and text
         // line up with Add font... below (a bare child put the text against its left edge).
         {
            ImGuiWindow* pw = ImGui::GetCurrentWindow();
            ImGui::SetCursorScreenPos(ImVec2(pw->Pos.x, ImGui::GetCursorScreenPos().y));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(MenuParts::kInset + MenuParts::kTextInset, 0.0f));
         }
         ImGui::BeginChild("##fontlist", ImVec2(ImGui::GetCurrentWindow()->Size.x, std::max(rowH, listH)), ImGuiChildFlags_AlwaysUseWindowPadding,
                           ImGuiWindowFlags_NoBackground);
         ImGui::PopStyleVar();
         if (ImGui::IsWindowAppearing() && curRow >= 0)
            ImGui::SetScrollY(std::max(0.0f, curRow * rowH - listH * 0.4f));
         ImGuiListClipper clip;
         clip.Begin((int)rows.size(), rowH);
         while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++)
            {
               const std::string& f = fonts[rows[r]];
               if (FontRow(f, f.c_str(), isCur(f), budget))
                  picked = &f;
               if (ImGui::IsItemHovered())
                  hovered = &f;
            }
         ImGui::EndChild();
         MenuParts::Separator();
         std::string added;
         bool add = false;
         if (MenuParts::Choice(L("Add font...##fontadd"), false))
         {
            for (const std::string& path : Platform::OpenFilesDialog(T("Add font"), { "ttf", "otf", "ttc" }))
               AddUserFont(path, added);
            add = true;
         }
         if (picked != nullptr || add)
         {
            const std::string to = picked != nullptr ? *picked : added;
            font = sFontOrig;
            if ((picked != nullptr || !to.empty()) && to != sFontOrig)
            {
               undo();
               font = to;
            }
            NoteRecentFont(to);
            changed = true;
            sOpenId.clear();
            ImGui::CloseCurrentPopup();
         }
         else
         {
            // Preview: the hovered family on the layer, the original back when the pointer leaves the list.
            const std::string& p = hovered != nullptr ? *hovered : sFontOrig;
            if (font != p)
            {
               font = p;
               changed = true;
            }
         }
         ImGui::PopStyleVar();
         MenuParts::EndCombo();
      }
      else if (openHere)
      {
         // Closed with no pick (Esc, a click outside): back to what it was.
         sOpenId.clear();
         if (font != sFontOrig)
         {
            font = sFontOrig;
            changed = true;
         }
      }
      return changed;
   }

   // Justified: an icon chip with four full-width lines (the icon font has left, centre and right only).
   bool JustifyChip(const char* id, bool on)
   {
      const bool hit = IconChip(id, "", on, T("Justify text"));
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      const float s = std::floor(std::min(b.x - a.x, b.y - a.y) * 0.5f);
      const float cx = std::floor((a.x + b.x) * 0.5f), cy = std::floor((a.y + b.y) * 0.5f);
      const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
      for (int i = 0; i < 4; i++)
      {
         const float y = cy - s * 0.5f + i * s / 3.0f + 0.5f;
         ImGui::GetWindowDrawList()->AddLine(ImVec2(cx - s * 0.5f, y), ImVec2(cx + s * 0.5f, y), col, 1.0f);
      }
      return hit;
   }

   // Bold, Italic, Underline, All caps: text chips, as every editor's character row.
   template <class Undo, class Next>
   bool TextStyleChips(bool* bold, bool* italic, bool* under, bool* caps, Undo&& undo, Next&& next)
   {
      bool* const v[4] = { bold, italic, under, caps };
      static const char* const kLabels[4] = { "B##tsb", "I##tsi", "U##tsu", "TT##tsc" };
      static const char* const kTips[4] = { "Bold", "Italic", "Underline", "All caps" };
      bool changed = false;
      for (int i = 0; i < 4; i++)
      {
         if (i > 0)
            next();
         if (TextChip(kLabels[i], *v[i], T(kTips[i])))
         {
            undo();
            *v[i] = !*v[i];
            changed = true;
         }
      }
      return changed;
   }

   void DrawTextBar(Scene2DNode* doc)
   {
      auto gap = []() { BarDivider(); };
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
      changed |= FontPicker("##v2dfont", font, 2.0f * kWellM, undo);
      next();
      ImGui::SetNextItemWidth(kWellM);
      changed |= FieldWell::Slider("##v2dfsize", &size, 4.0f, 800.0f, "%.0f px");
      if (ImGui::IsItemActivated())
         undo();
      next();
      ImGui::SetNextItemWidth(kWellL);
      changed |= FieldWell::Slider("##v2dlead", &leading, 0.5f, 3.0f, "Leading %.2f");
      if (ImGui::IsItemActivated())
         undo();
      next();
      ImGui::SetNextItemWidth(kWellL);
      changed |= FieldWell::Slider("##v2dtrack", &tracking, -100.0f, 400.0f, "Tracking %.0f");
      if (ImGui::IsItemActivated())
         undo();
      gap();
      static const char* kAlignGlyphs[3] = { IconsInfinite::TextLeft, IconsInfinite::TextCenter, IconsInfinite::TextRight };
      static const char* kAlignTips[3] = { "Left align text", "Centre text", "Right align text" };
      for (int i = 0; i < 4; i++)
      {
         if (i > 0)
            next();
         const std::string id = std::string("##v2dtal") + std::to_string(i);
         if ((i < 3 ? IconChip(id.c_str(), kAlignGlyphs[i], align == i, T(kAlignTips[i])) : JustifyChip(id.c_str(), align == i)) && align != i)
         {
            undo();
            align = i;
            changed = true;
         }
      }
      gap();
      changed |= TextStyleChips(l != nullptr ? &l->bold : &sBold, l != nullptr ? &l->italic : &sItalic,
                                l != nullptr ? &l->underline : &sUnder, l != nullptr ? &l->caps : &sCaps, undo, next);
      gap();
      if (FormParts::ColorDot("##v2dtextc", colour, std::min(18.0f, sRowH - 4.0f), sRowH))
      {
         // One undo step per colour edit, as ColorRow: the first change in a second pushes it.
         static double sAt = -1.0;
         if (ImGui::GetTime() - sAt > 1.0)
            undo();
         sAt = ImGui::GetTime();
         changed = true;
      }
      if (changed && l != nullptr)
         Touch();
   }

   // ---- header and status ----------------------------------------------

   // The tools, down the left of the picture as in every image editor, in Photoshop's order and sections
   // (Photopea and PhotoCraft keep the same). Under them the foreground / background chips and quick mask.
   struct ToolDef { int tool; const char* id; const char* glyph; const char* tip; int section; };
   const ToolDef kTools[kToolCount] = {
      { kMove, "##v2dmove", IconsInfinite::ToolMove, "Move (V)", 0 },
      { kMarquee, "##v2dmarquee", IconsInfinite::Marquee, "Marquee (M)", 1 },
      { kLasso, "##v2dlasso", IconsInfinite::Lasso, "Lasso (L)", 1 },
      { kWand, "##v2dwand", IconsInfinite::Wand, "Magic wand (W)", 1 },
      { kCrop, "##v2dcrop", IconsInfinite::Crop, "Crop (C). Enter applies, Esc cancels", 1 },
      { kPick, "##v2dpick", IconsInfinite::Eyedropper, "Eyedropper (I). Alt sets the background", 1 },
      { kHeal, "##v2dheal", IconsInfinite::Heal, "Healing brush (J). Alt-click sets the source", 2 },
      { kBrush, "##v2dbrush", IconsInfinite::Brush, "Brush (B). Alt picks a colour, Shift-click draws a line", 2 },
      { kClone, "##v2dclone", IconsInfinite::CloneStamp, "Clone stamp (S). Alt-click sets the source", 2 },
      { kEraser, "##v2derase", IconsInfinite::Erase, "Eraser (E)", 2 },
      { kGradient, "##v2dgradient", IconsInfinite::Gradient, "Gradient (G). Drag to draw", 2 },
      { kFill, "##v2dfilltool", IconsInfinite::Fill, "Paint bucket (G)", 2 },
      { kBlur, "##v2dblur", IconsInfinite::Blur, "Blur (R)", 2 },
      { kSharpen, "##v2dsharpen", IconsInfinite::Sharpen, "Sharpen (R)", 2 },
      { kDodge, "##v2ddodge", IconsInfinite::Dodge, "Dodge (O). Lightens", 2 },
      { kBurn, "##v2dburn", IconsInfinite::Burn, "Burn (O). Darkens", 2 },
      { kPen, "##v2dpen", IconsInfinite::PenTool, "Pen (P). Drag for curves, Enter ends", 3 },
      { kText, "##v2dtext", IconsInfinite::Type, "Text (T). Click for a line, drag for a box", 3 },
      { kNode, "##v2dnode", IconsInfinite::Pointer, "Direct select (A). Alt-click an anchor makes a corner", 3 },
      { kShape, "##v2dshape", IconsInfinite::Shape, "Shape (U). Shift keeps it square, Alt from the centre", 3 },
      { kHand, "##v2dhand", IconsInfinite::Hand, "Hand (H, or hold Space)", 4 },
      { kZoom, "##v2dzoom", IconsInfinite::Zoom, "Zoom (Z). Alt-click zooms out", 4 },
   };
   // "Brush (B). Alt picks..." -> "Brush"
   const char* ToolTitle(const char* tip)
   {
      static std::string buf;
      buf = tip;
      const size_t cut = buf.find_first_of("(.");
      if (cut != std::string::npos)
         buf.erase(buf.find_last_not_of(' ', cut - 1) + 1);
      return buf.c_str();
   }

   const ToolDef* ToolFor(int tool)
   {
      for (const ToolDef& t : kTools)
         if (t.tool == tool)
            return &t;
      return nullptr;
   }

   const char* ToolGlyph(int tool)
   {
      const ToolDef* t = ToolFor(tool);
      return t != nullptr ? t->glyph : nullptr;
   }

   // The strip's order and hidden tools, kept in the workspace settings as "move,marquee,-lasso,...": drag a tool
   // to move it, right-click the strip to show or hide tools.
   std::vector<int> sStripOrder;     // indices into kTools
   std::vector<bool> sStripHidden;
   std::string sStripParsed = "\x01";

   void ParseStrip()
   {
      if (sStripParsed == gView2DTools)
         return;
      sStripParsed = gView2DTools;
      sStripOrder.clear();
      sStripHidden.assign(kToolCount, false);
      std::vector<bool> seen(kToolCount, false);
      size_t at = 0;
      while (at < gView2DTools.size())
      {
         size_t end = gView2DTools.find(',', at);
         if (end == std::string::npos)
            end = gView2DTools.size();
         std::string name = gView2DTools.substr(at, end - at);
         at = end + 1;
         const bool hide = !name.empty() && name[0] == '-';
         if (hide)
            name.erase(0, 1);
         for (int i = 0; i < kToolCount; i++)
            if (!seen[i] && name == kTools[i].id + 5)
            {
               seen[i] = true;
               sStripOrder.push_back(i);
               sStripHidden[i] = hide;
            }
      }
      for (int i = 0; i < kToolCount; i++)   // tools added since the order was saved go at their default place
         if (!seen[i])
            sStripOrder.insert(sStripOrder.begin() + std::min((size_t)i, sStripOrder.size()), i);
   }

   void SaveStrip()
   {
      std::string out;
      for (int i : sStripOrder)
         out += (out.empty() ? "" : ",") + std::string(sStripHidden[i] ? "-" : "") + (kTools[i].id + 5);
      gView2DTools = out;
      sStripParsed = out;
      SaveWorkspaceSettings();
   }

   int VisibleTools()
   {
      ParseStrip();
      int n = 0;
      for (int i : sStripOrder)
         n += !sStripHidden[i] || kTools[i].tool == sTool;
      return n;
   }

   // One column when the tools fit the height, else as many as it takes (never clipped, never scrolled).
   int StripColumns(float h)
   {
      const int n = VisibleTools();
      const float step = tok::tile + tok::space_1;
      const float oneCol = n * step + 4 * (tok::space_1 + 1.0f) + tok::space_1 + 2 * step;   // + sections + chips row
      if (oneCol <= h)
         return 1;
      for (int cols = 2; cols < 6; cols++)
         if (((n + cols - 1) / cols + 1) * step + tok::space_1 <= h)
            return cols;
      return 6;
   }

   float StripWidth(int cols) { return cols * tok::tile + (cols - 1) * tok::space_1; }

   // Foreground over background in one tile, Photoshop's corner layout. Click either square for its picker;
   // the small corner arrow swaps (X), the tiny pair resets (D).
   void ColourChips()
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float t = tok::tile, sq = 17.0f;
      ImGui::InvisibleButton("##v2dcolours", ImVec2(t, t));
      const ImVec2 m = ImGui::GetIO().MousePos;
      const bool inFg = m.x < p.x + sq + 1 && m.y < p.y + sq + 1;
      const bool inSwap = !inFg && m.x > p.x + sq && m.y < p.y + t - sq;
      const bool inReset = !inFg && m.x < p.x + t - sq && m.y > p.y + sq;
      if (ImGui::IsItemHovered())
         HelpTip("%s", T(inFg ? "Foreground" : inSwap ? "Swap colours (X)" : inReset ? "Default colours (D)" : "Background"));
      if (ImGui::IsItemClicked())
      {
         if (inSwap) SwapColours();
         else if (inReset) DefaultColours();
         else ImGui::OpenPopup(inFg ? "##v2dfgpick" : "##v2dbgpick");
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 edge = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      auto square = [&](ImVec2 a, const float* c) {
         dl->AddRectFilled(a, ImVec2(a.x + sq, a.y + sq), ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], 1.0f)), tok::radius_cell);
         dl->AddRect(a, ImVec2(a.x + sq, a.y + sq), edge, tok::radius_cell);
      };
      square(ImVec2(p.x + t - sq, p.y + t - sq), sBg);
      square(p, sFg);
      const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
      // swap: a bent arrow in the top-right corner
      const ImVec2 s0(p.x + sq + 2.5f, p.y + 2.5f);
      dl->AddBezierQuadratic(ImVec2(s0.x, s0.y + 0.5f), ImVec2(s0.x + 6.0f, s0.y + 0.5f), ImVec2(s0.x + 6.0f, s0.y + 6.5f), ink, 1.2f);
      // reset: a tiny black-over-white pair in the bottom-left corner
      const ImVec2 r0(p.x + 1.5f, p.y + sq + 3.0f);
      dl->AddRectFilled(ImVec2(r0.x + 2.5f, r0.y + 2.5f), ImVec2(r0.x + 7.0f, r0.y + 7.0f), IM_COL32(255, 255, 255, 255));
      dl->AddRectFilled(r0, ImVec2(r0.x + 4.5f, r0.y + 4.5f), IM_COL32(0, 0, 0, 255));
      dl->AddRect(r0, ImVec2(r0.x + 4.5f, r0.y + 4.5f), edge);
      for (int i = 0; i < 2; i++)
         if (ImGui::BeginPopup(i == 0 ? "##v2dfgpick" : "##v2dbgpick"))
         {
            ImGui::ColorPicker3("##pick", i == 0 ? sFg : sBg, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
            ImGui::EndPopup();
         }
   }

   void DrawToolStrip(float h, Scene2DNode* doc)
   {
      const int cols = StripColumns(h);
      const float w = StripWidth(cols);
      ImGui::BeginChild("##v2dtools", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(tok::space_1, tok::space_1));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ParseStrip();
      int shown = 0, lastSection = -1, dropFrom = -1, dropTo = -1;
      for (int k = 0; k < (int)sStripOrder.size(); k++)
      {
         const int i = sStripOrder[k];
         if (sStripHidden[i] && kTools[i].tool != sTool)
            continue;
         if (cols == 1 && shown > 0 && kTools[i].section != lastSection)
         {
            // A hairline between sections, centred in its gap.
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float y = std::round(p.y + 0.5f * (tok::space_1 + 1.0f) - tok::space_1 * 0.5f);
            dl->AddLine(ImVec2(p.x + 4.0f, y), ImVec2(p.x + w - 4.0f, y), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
            ImGui::Dummy(ImVec2(w, 1.0f));
         }
         else if (cols > 1 && shown % cols != 0)
            ImGui::SameLine(0.0f, tok::space_1);
         lastSection = kTools[i].section;
         shown++;
         if (IconChip(kTools[i].id, kTools[i].glyph, sTool == kTools[i].tool, Sc(T(kTools[i].tip))))
            sTool = kTools[i].tool;
         if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
         {
            ImGui::SetDragDropPayload("V2DTOOL", &k, sizeof(int));
            ImGui::EndDragDropSource();
         }
         if (ImGui::BeginDragDropTarget())
         {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("V2DTOOL"))
            {
               dropFrom = *(const int*)pl->Data;
               dropTo = k;
            }
            ImGui::EndDragDropTarget();
         }
      }
      if (dropFrom >= 0 && dropFrom != dropTo)
      {
         const int moved = sStripOrder[dropFrom];
         sStripOrder.erase(sStripOrder.begin() + dropFrom);
         sStripOrder.insert(sStripOrder.begin() + dropTo, moved);
         SaveStrip();
      }
      if (MenuParts::BeginContextWindow("##v2dtoolsmenu", ImGuiPopupFlags_MouseButtonRight))
      {
         for (int i : sStripOrder)
            if (MenuParts::Item((std::string(T(ToolTitle(kTools[i].tip))) + kTools[i].id).c_str(), nullptr, !sStripHidden[i]))
            {
               sStripHidden[i] = !sStripHidden[i];
               SaveStrip();
            }
         ImGui::Separator();
         if (MenuParts::Item(T("Reset Tools")))
         {
            gView2DTools.clear();
            ParseStrip();
            sStripParsed = "\x01";
         }
         MenuParts::EndPopup();
      }
      ImGui::Dummy(ImVec2(w, 0.0f));
      ColourChips();
      if (cols == 1)
         ImGui::Dummy(ImVec2(w, 0.0f));
      else
         ImGui::SameLine(0.0f, tok::space_1);
      if (IconChip("##v2dqm", IconsInfinite::QuickMask, doc->quickMask, T(doc->quickMask ? "Quick mask on (Q)" : "Quick mask (Q)")))
         doc->quickMask = !doc->quickMask;
      ImGui::PopStyleVar();
      ImGui::EndChild();
   }

   // Text centred on the 28 px tile row, so labels share the chips' centre line.
   void RowLabel(const char* text)
   {
      const float y = ImGui::GetCursorPosY();
      ImGui::SetCursorPosY(y + (tok::tile - ImGui::GetTextLineHeight()) * 0.5f);
      ImGui::TextDisabled("%s", text);
      ImGui::SetCursorPosY(y);
   }

   // The context bar: what the current tool can do (plan §5). Laid out on one tile row from the cursor.
   void DrawContextBarItems(Scene2DNode* doc);

   // Wells, combos and chips share one row height (the bar's sRowH): a well taller than its chips sat low and made
   // the chips look like they floated.
   void DrawContextBar(Scene2DNode* doc)
   {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, std::max(1.0f, (sRowH - ImGui::GetFontSize()) * 0.5f)));
      DrawContextBarItems(doc);
      ImGui::PopStyleVar();
   }

   void DrawContextBarItems(Scene2DNode* doc)
   {
      auto gap = []() { BarDivider(); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      Layer* a = Active(doc);
      if (sTool == kMove)
      {
         bool autoSel = sAutoSelect;
         // Checkbox and label sit on the chips' centre line; the cursor then returns to the row's top edge.
         const ImVec2 row0 = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(row0.x, row0.y + (sRowH - CheckBox::kSize) * 0.5f));
         if (CheckBox::Box("##v2dauto", &autoSel))
            sAutoSelect = autoSel;
         ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + tok::space_1, row0.y));
         CentredText(row0.y, T("Auto-select"));
         if (ImGui::IsItemClicked())
            sAutoSelect = !sAutoSelect;
         gap();
         {
            ImGui::BeginDisabled(a == nullptr || !a->is3d);
            static const char* kModes[3] = { "Move##v2dgz0", "Rotate##v2dgz1", "Scale##v2dgz2" };
            static const char* kModeTip[3] = { "Move along X, Y or Z", "Rotate about X, Y or Z", "Scale the card" };
            for (int i = 0; i < 3; i++)
            {
               if (i > 0)
                  next();
               if (TextChip(L(kModes[i]), sGizmoMode == i && a != nullptr && a->is3d, T(kModeTip[i])))
                  sGizmoMode = i;
            }
            ImGui::EndDisabled();
            gap();
         }
         static const char* kAlign[6] = { "##al0", "##al1", "##al2", "##al3", "##al4", "##al5" };
         static const char* kAlignTip[6] = { "Align left edges", "Align horizontal centres", "Align right edges",
                                             "Align top edges", "Align vertical centres", "Align bottom edges" };
         for (int i = 0; i < 6; i++)
         {
            if (i > 0)
               next();
            ImGui::BeginDisabled(sSel.empty());
            const bool hit = ActionButton::Draw(kAlign[i], ImVec2(kIconW, sRowH));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               HelpTip("%s", T(kAlignTip[i]));
            // Drawn: a bar for the edge and two blocks lined up on it; bar at +-5 so bar + 10 pt block is centred in the chip.
            const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
            const ImVec2 c((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 col = ImGui::GetColorU32(sSel.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            const int side = i % 3;
            // Drawn to the icon set's ink (0.78 of the role's em), so the align marks match the other bar icons.
            const float k = 0.78f * (sChip < tok::tile ? tok::icon_sm : tok::icon_md) / 14.0f;
            if (i < 3)
            {
               const float x = side == 0 ? c.x - 5 * k : side == 1 ? c.x : c.x + 5 * k;
               dl->AddLine(ImVec2(x, c.y - 7 * k), ImVec2(x, c.y + 7 * k), col, 1.5f);
               const float w1 = 10 * k, w2 = 6 * k;
               const float s1 = side == 0 ? x : side == 1 ? x - w1 / 2 : x - w1, s2 = side == 0 ? x : side == 1 ? x - w2 / 2 : x - w2;
               dl->AddRectFilled(ImVec2(s1, c.y - 5 * k), ImVec2(s1 + w1, c.y - 1 * k), col);
               dl->AddRectFilled(ImVec2(s2, c.y + 1 * k), ImVec2(s2 + w2, c.y + 5 * k), col);
            }
            else
            {
               const float y = side == 0 ? c.y - 5 * k : side == 1 ? c.y : c.y + 5 * k;
               dl->AddLine(ImVec2(c.x - 7 * k, y), ImVec2(c.x + 7 * k, y), col, 1.5f);
               const float h1 = 10 * k, h2 = 6 * k;
               const float s1 = side == 0 ? y : side == 1 ? y - h1 / 2 : y - h1, s2 = side == 0 ? y : side == 1 ? y - h2 / 2 : y - h2;
               dl->AddRectFilled(ImVec2(c.x - 5 * k, s1), ImVec2(c.x - 1 * k, s1 + h1), col);
               dl->AddRectFilled(ImVec2(c.x + 1 * k, s2), ImVec2(c.x + 5 * k, s2 + h2), col);
            }
            if (hit)
               Align(doc, i);
         }
         // Distribute: three bars, evenly spaced across (horizontal) or down (vertical).
         for (int i = 0; i < 2; i++)
         {
            next();
            ImGui::BeginDisabled(sSel.size() < 3);
            const bool hit = ActionButton::Draw(i == 0 ? "##dist0" : "##dist1", ImVec2(kIconW, sRowH));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               HelpTip("%s", T(i == 0 ? "Distribute horizontally" : "Distribute vertically"));
            const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
            const ImVec2 c((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 col = ImGui::GetColorU32(sSel.size() < 3 ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            const float ks = 0.78f * (sChip < tok::tile ? tok::icon_sm : tok::icon_md) / 14.0f;
            for (int k = -1; k <= 1; k++)
            {
               const float o = k * 5.5f * ks, len = (k == 0 ? 4.0f : 6.0f) * ks;
               if (i == 0)
                  dl->AddRectFilled(ImVec2(c.x + o - 1.25f, c.y - len), ImVec2(c.x + o + 1.25f, c.y + len), col);
               else
                  dl->AddRectFilled(ImVec2(c.x - len, c.y + o - 1.25f), ImVec2(c.x + len, c.y + o + 1.25f), col);
            }
            if (hit)
               Distribute(doc, i);
         }
         {
            gap();
            const bool canFlip = a != nullptr && a->kind == Kind::Node;
            ImGui::BeginDisabled(!canFlip);
            if (TextChip(L("Flip H##v2dfh"), canFlip && a->flipX, T("Flip horizontal")))
            {
               PushUndoCheckpoint("Flip layer");
               a->flipX = !a->flipX;
               Touch();
            }
            next();
            if (TextChip(L("Flip V##v2dfv"), canFlip && a->flipY, T("Flip vertical")))
            {
               PushUndoCheckpoint("Flip layer");
               a->flipY = !a->flipY;
               Touch();
            }
            ImGui::EndDisabled();
         }
      }
      else if (sTool == kCrop && sCropOn)
      {
         char dims[64];
         std::snprintf(dims, sizeof(dims), "%d x %d", (int)std::fabs(sCrop[2] - sCrop[0]), (int)std::fabs(sCrop[3] - sCrop[1]));
         const float rowTop = ImGui::GetCursorScreenPos().y;
         CentredText(rowTop, dims);
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
         if (TextChip(L("Fit##v2dfit"), false, Sc(T("Fit on screen (Ctrl+0)"))))
            FitView(doc);
         next();
         if (TextChip("100%##v2d100", false, Sc(T("Actual pixels (Ctrl+1)"))))
         {
            sZoom = 1.0f;
            sPan = ImVec2(0, 0);
         }
      }
   }

   void DrawHeader(Scene2DNode* doc)
   {
      const float availW = ImGui::GetContentRegionAvail().x;
      const float startX = ImGui::GetCursorPosX();
      if (sTool != kCrop)
         sCropOn = false;
      if (sTool != kLasso)
         sPoly.clear();
      if (sTool != kPen && sPenItem >= 0)
         FinishPen(doc);
      if (sTool != kText && sTextEdit != 0)
         EndTextEdit(doc);
      // Right cluster first, so the bar knows its room: [Convert] [Render] [Layers], right-aligned on the bar's row.
      const char* renderL = L("Render##v2drender");
      const char* convertL = L("Convert##v2dconvert");
      auto chipW = [](const char* l) { return ImGui::CalcTextSize(l, ImGui::FindRenderedTextEnd(l)).x + 2.0f * tok::space_2; };
      const float rightW = (doc->panelOnly ? chipW(convertL) + tok::space_1 : 0.0f) + chipW(renderL) + tok::space_1 + sChip;
      const float rowTop = ImGui::GetCursorScreenPos().y;
      const float barRoom = availW - rightW - (2.0f * tok::space_2 + 1.0f);   // one bar divider before the right cluster
      // A bar wider than its room is clipped at a chip edge-free boundary and a "..." chip opens the whole bar.
      static float sBarW = 0.0f;
      const bool overflow = sBarW > barRoom;
      const float barX = ImGui::GetCursorScreenPos().x;
      if (overflow)
      {
         const float clipW = std::max(0.0f, barRoom - chipW(L("More..##v2dmore")) - tok::space_1);
         ImGui::BeginChild("##v2dbarclip", ImVec2(clipW, sRowH), ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
         const ImGuiID before = ImGui::GetItemID();
         DrawContextBar(doc);
         sBarW = ImGui::GetItemID() != before ? ImGui::GetItemRectMax().x - barX : 0.0f;   // an empty bar (Pick) is 0 wide, not the last item's edge
         ImGui::EndChild();
         ImGui::SameLine(0.0f, tok::space_1);
         if (TextChip(L("More..##v2dmore"), false, T("The rest of the bar")))
            ImGui::OpenPopup("##v2dbarmore");
         if (MenuParts::BeginPopup("##v2dbarmore"))
         {
            DrawContextBar(doc);
            MenuParts::EndPopup();
         }
         BarDivider();   // the clipped bar ends in "More.."; a divider, not a loose gap, sets the right cluster apart
      }
      else
      {
         const ImGuiID before = ImGui::GetItemID();
         DrawContextBar(doc);
         sBarW = ImGui::GetItemID() != before ? ImGui::GetItemRectMax().x - barX : 0.0f;   // an empty bar (Pick) is 0 wide, not the last item's edge
      }
      ImGui::SameLine();
      ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, ImGui::GetWindowPos().x + startX + availW - rightW), rowTop));
      if (doc->panelOnly)
      {
         if (ActionButton::Draw(convertL, ImVec2(chipW(convertL), sRowH)))
            sPending.what = Pending::Convert;
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Put this document on the canvas"));
         FormParts::Inline(0.0f, tok::space_1);
      }
      ImGui::BeginDisabled(gOfflineRender.active);
      if (ActionButton::Draw(renderL, ImVec2(chipW(renderL), sRowH)))
         ImGui::OpenPopup("##v2drendermenu");
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", Sc(T("Render an image (F12) or the animation (Ctrl+F12)")));
      if (MenuParts::BeginPopup("##v2drendermenu"))
      {
         if (MenuParts::Item(T("Image"), "F12"))
            sPending.what = Pending::RenderImage;
         if (MenuParts::Item(T("Animation"), Sc("Ctrl+F12")))
            sPending.what = Pending::RenderAnim;
         MenuParts::EndPopup();
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dside", IconsInfinite::Layers, sSideOpen, T("Layers and properties"))) sSideOpen = !sSideOpen;
   }

   // The bottom row: zoom out / in, the zoom itself (click it to type one), Fit and 1:1, then the document facts.
   void DrawStatus(Scene2DNode* doc)
   {
      const Scene2DNode::Document& d = doc->Doc();
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      float row0Y = 0.0f;
      const ImVec2 mid((sViewMn.x + sViewMx.x) * 0.5f, (sViewMn.y + sViewMx.y) * 0.5f);
      if (ZoomChip("##v2dzout", false, Sc(T("Zoom out (Ctrl+-)"))))
         ZoomAt(doc, 0.8f, mid);
      row0Y = ImGui::GetItemRectMin().y;   // every later item and the text share this chip's top edge
      next();
      float pct = sZoom * 100.0f;
      ImGui::SetNextItemWidth(kWellS);
      FieldWell::PushStyle();
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, (sRowH - ImGui::GetFontSize()) * 0.5f));
      const bool zoomTyped = ImGui::InputFloat("##v2dzoompct", &pct, 0.0f, 0.0f, "%.0f%%", ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::PopStyleVar();
      if (zoomTyped)
         ZoomAt(doc, std::clamp(pct, 1.0f, 6400.0f) / 100.0f / sZoom, mid);
      FieldWell::PopStyle();
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Zoom - type a percentage"));
      next();
      if (ZoomChip("##v2dzin", true, Sc(T("Zoom in (Ctrl++)"))))
         ZoomAt(doc, 1.25f, mid);
      next();
      if (TextChip(L("Fit##v2dstfit"), false, Sc(T("Fit on screen (Ctrl+0)"))))
         FitView(doc);
      next();
      if (TextChip("1:1##v2dst11", false, Sc(T("Actual pixels (Ctrl+1)"))))
      {
         sZoom = 1.0f;
         sPan = ImVec2(0, 0);
      }
      FormParts::Inline(0.0f, tok::space_3);
      char buf[200];
      if (sMouseIn)
         std::snprintf(buf, sizeof(buf), "%d x %d   %.0f, %.0f   %.2f ms", d.w, d.h, sMouseDoc.x, sMouseDoc.y, doc->CookMs());
      else
         std::snprintf(buf, sizeof(buf), "%d x %d   %.2f ms", d.w, d.h, doc->CookMs());
      CentredText(row0Y, buf);
   }

   // ---- layers list ------------------------------------------------------

   void DrawThumb(ImDrawList* dl, Scene2DNode* doc, const Layer& l, ImVec2 p, ImVec2 sz)
   {
      dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(40, 40, 40, 255), 2.0f);
      if (l.kind == Kind::Group)
      {
         glyph::Draw(dl, ImVec2(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), tok::icon_sm, ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::Folder);
         return;
      }
      if (l.kind == Kind::Adjust)
      {
         glyph::Draw(dl, ImVec2(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), tok::icon_sm, ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::ShadeSolid);
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

   int CountRows(Scene2DNode* doc, int parent)
   {
      int n = 0;
      for (int id : doc->ChildrenOf(parent))
         if (const Layer* l = doc->FindLayer(id))
            n += 1 + (l->kind == Kind::Group && l->open ? CountRows(doc, id) : 0);
      return n;
   }

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
         const float rowH = kRowH;
         const ImVec2 p = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool sel = Selected(id);
         if (sel)
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_Header, Active(doc) == l ? 1.0f : 0.6f), 3.0f);
         if (l->label > 0 && l->label < 8)
            dl->AddRectFilled(ImVec2(p.x, p.y + 3), ImVec2(p.x + 3, p.y + rowH - 3), kLabelCol[l->label]);
         // Eye: Alt-click shows only this one.
         ImGui::SetCursorScreenPos(ImVec2(p.x + 4, p.y + (rowH - 18) * 0.5f));
         if (ImGui::InvisibleButton("##eye", ImVec2(18, 18)))
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
         glyph::Draw(dl, ImVec2(p.x + 13, p.y + rowH * 0.5f), tok::icon_sm,
                     ImGui::GetColorU32(l->visible ? ImGuiCol_Text : ImGuiCol_TextDisabled), l->visible ? IconsInfinite::Eye : IconsInfinite::EyeOff);
         float x = p.x + 24.0f + depth * 12.0f;
         if (l->kind == Kind::Group)
         {
            ImGui::SetCursorScreenPos(ImVec2(x, p.y + (rowH - 16) * 0.5f));
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
            dl->AddLine(ImVec2(x, p.y + 5), ImVec2(x, p.y + rowH - 6), col, 1.5f);
            dl->AddLine(ImVec2(x, p.y + rowH - 6), ImVec2(x + 6, p.y + rowH - 6), col, 1.5f);
            x += 10.0f;
         }
         DrawThumb(dl, doc, *l, ImVec2(x, p.y + 2), ImVec2(28, rowH - 4));
         x += 33.0f;
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
            if (MenuParts::Item(T("Copy"), Sc("Ctrl+C"))) sPending.what = Pending::Copy;
            if (MenuParts::Item(T("Cut"), Sc("Ctrl+X"))) sPending.what = Pending::Cut;
            if (MenuParts::Item(T("Paste"), Sc("Ctrl+V"), false, !sClip.empty())) sPending.what = Pending::Paste;
            if (MenuParts::Item(T("Duplicate Layer"), Sc("Ctrl+J"))) sPending.what = Pending::Duplicate;
            MenuParts::Separator();
            if (MenuParts::Item(T("Group Layers"), Sc("Ctrl+G"))) sPending.what = Pending::Group;
            if (l->kind == Kind::Group && MenuParts::Item(T("Ungroup"), Sc("Shift+Ctrl+G"))) sPending.what = Pending::Ungroup;
            if (MenuParts::Item(l->clip ? T("Release Clipping Mask") : T("Create Clipping Mask"), Sc("Alt+Ctrl+G")))
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
            if (l->kind == Kind::Node)
            {
               NodeChoiceMenu(T("Replace with"), doc, Pending::ReplaceNode, l->uid);
               if (MenuParts::Item(T("Replace by Clicking a Node"), nullptr, sPickMode == 2))
                  sPickMode = sPickMode == 2 ? 0 : 2;
            }
            if (l->kind == Kind::Paint && MenuParts::Item(T("Convert to Draw Node"), nullptr)) sPending.what = Pending::ToDraw;
            if (l->kind == Kind::Vector && MenuParts::Item(T("Convert to Sketch Node"), nullptr)) sPending.what = Pending::ToSketch;
            if (l->kind == Kind::Text && MenuParts::Item(T("Convert to Text Node"), nullptr)) sPending.what = Pending::ToText;
            if (l->kind == Kind::Text && MenuParts::Item(T("Convert to Shapes"), nullptr)) sPending.what = Pending::ToShapes;
            if (MenuParts::Item(T("Layer as Output"), nullptr, l->output)) sPending.what = Pending::LayerOutput;
            if (doc->CanTakeBelow(*l) && MenuParts::Item(T("Effect on Below"), nullptr, l->below)) sPending.what = Pending::LayerBelow;
            if (l->warpOn && MenuParts::Item(T("Reset Distort"), nullptr))
            {
               PushUndoCheckpoint("Reset distort");
               l->warpOn = false;
               std::fill(l->warp, l->warp + 8, 0.0f);
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
      // The old Overlay / Soft / Hard Light only show on a layer that already uses one (patches from before the fix).
      for (const std::string& n : BlendModes::NamesFor(current))
         names.push_back(T(n.c_str()));
      return names;
   }

   void DrawLayersCard(Scene2DNode* doc, float w, float listH)
   {
      Layer* a = Active(doc);
      // Blend, opacity and fill sit over the list, where the eye already is. Always there, greyed with nothing picked,
      // so the panel never jumps when the selection empties.
      {
         static Layer sNone;
         const bool none = a == nullptr;
         if (none)
         {
            sNone = Layer();
            a = &sNone;
         }
         ImGui::BeginDisabled(none);
         const bool group = a->kind == Kind::Group;
         std::vector<std::string> names = BlendNames(a->blend, group);
         // Blend + Opacity, then Fill + Lock: tile-high wells so they share the chips' 28 pt rows, both rows flush to w.
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, (sChip - ImGui::GetFontSize()) * 0.5f));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, tok::space_1));
         const float half = (w - tok::space_1) * 0.5f;
         ImGui::SetNextItemWidth(half);
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
         ImGui::SetNextItemWidth(w - half - tok::space_1);
         float pct = a->opacity * 100.0f;
         ModNext(*a, Scene2DNode::kLfOpacity);
         ModBegin();
         if (FieldWell::Slider("##v2dopacity", &pct, 0.0f, 100.0f, "Opacity %.0f%%"))
         {
            a->opacity = pct / 100.0f;
            Touch();
         }
         ModEnd();
         UndoOnActivate("Opacity");
         const float lockW = ImGui::CalcTextSize(T("Lock")).x + 4.0f * (tok::space_1 + sChip);
         ImGui::SetNextItemWidth(std::max(kWellM, w - lockW - tok::space_1));
         float fillPct = a->fill * 100.0f;
         ModNext(*a, Scene2DNode::kLfFill);
         ModBegin();
         if (FieldWell::Slider("##v2dfill", &fillPct, 0.0f, 100.0f, "Fill %.0f%%"))
         {
            a->fill = fillPct / 100.0f;
            Touch();
         }
         ModEnd();
         UndoOnActivate("Fill");
         // Lock row, Photoshop's four icons: transparent pixels, image pixels, position, all.
         struct { unsigned bit; const char* id; const char* glyph; const char* tip; } kLocks[4] = {
            { Scene2DNode::kLockAlpha, "##lk0", IconsInfinite::GridDots, "Lock transparent pixels" },
            { Scene2DNode::kLockPixels, "##lk1", IconsInfinite::Brush, "Lock image pixels" },
            { Scene2DNode::kLockPosition, "##lk2", IconsInfinite::ToolMove, "Lock position" },
            { Scene2DNode::kLockAll, "##lk3", IconsInfinite::Lock, "Lock all" },
         };
         FormParts::Inline(0.0f, tok::space_1);
         const float lockTop = ImGui::GetCursorScreenPos().y;
         CentredText(lockTop, T("Lock"), true, sChip);   // the lock chips' centre line, not the tool bar's
         for (auto& lk : kLocks)
         {
            FormParts::Inline(0.0f, tok::space_1);
            if (IconChip(lk.id, lk.glyph, (a->lock & lk.bit) != 0, T(lk.tip)))
            {
               PushUndoCheckpoint("Lock layer");
               a->lock ^= lk.bit;
               Touch();
            }
         }
         ImGui::PopStyleVar(2);
         ImGui::EndDisabled();
         if (none)
            a = nullptr;
      }
      // The blend / opacity / lock rows above already took their height: the list gets what is left, so it scrolls.
      listH = std::max(3.0f * (kRowH + 1.0f), std::min(listH, ImGui::GetContentRegionAvail().y - 2.0f));
      ImGui::BeginChild("##v2dlayers", ImVec2(w, listH), ImGuiChildFlags_None);
      const float rowsW = ImGui::GetContentRegionAvail().x;
      if (doc->Layers().empty())
         CentredText(ImGui::GetCursorScreenPos().y, T("No layers"));
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
      // The strip under the list, as in every layers panel, centred under it.
      const float footW = 9 * sChip + 8 * tok::space_1;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (w - footW) * 0.5f));
      if (IconChip("##v2dnewpaint", IconsInfinite::Brush, false, T("New layer")))
         sPending.what = Pending::AddPaint;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewfill", IconsInfinite::Fill, false, T("New fill layer")))
         sPending.what = Pending::AddFill;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewadj", IconsInfinite::ShadeSolid, false, T("New adjustment layer")))
         ImGui::OpenPopup("##v2dadjpop");
      if (MenuParts::BeginPopup("##v2dadjpop"))
      {
         for (int t = 0; t < Scene2DNode::kAdjCount; t++)
            if (MenuParts::Item(T(Scene2DNode::AdjName(t)), nullptr, false, true))
            {
               sPending.what = Pending::AddAdjust;
               sPending.arg = t;
            }
         MenuParts::EndPopup();
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewvec", IconsInfinite::PenTool, false, T("New vector layer")))
         sPending.what = Pending::AddVector;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewnode", IconsInfinite::Plus, sPickMode == 1, T("Add a node from the canvas as a layer")))
         ImGui::OpenPopup("##v2dnodepop");
      if (MenuParts::BeginPopup("##v2dnodepop"))
      {
         if (MenuParts::Item(T("Click a Node on the Canvas"), nullptr, sPickMode == 1))
            sPickMode = sPickMode == 1 ? 0 : 1;
         MenuParts::Separator();
         bool any = false;
         for (GraphNode& gn : gNodes)
         {
            if (gn.node.get() == doc || !View2DCanAdd(gn))
               continue;
            any = true;
            const std::string name = gn.typeName + " " + std::to_string(gn.index) + "##n" + std::to_string(gn.uid);
            if (MenuParts::Item(name.c_str(), nullptr, false))
            {
               sPending.what = Pending::AddNode;
               sPending.uid = gn.uid;
            }
         }
         if (!any)
            ImGui::TextDisabled("%s", T("No fitting nodes"));
         MenuParts::EndPopup();
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dnewgroup", IconsInfinite::Folder, false, Sc(T("Group (Ctrl+G)"))))
         sPending.what = Pending::Group;
      FormParts::Inline(0.0f, tok::space_1);
      ImGui::BeginDisabled(a == nullptr || a->kind == Kind::Group);
      if (IconChip("##v2dmask", IconsInfinite::Mask, a != nullptr && (a->maskUid != 0 || a->HasPixelMask()),
                   T("Add a layer mask (from the selection when there is one)")))
         sPending.what = doc->HasSelection() ? Pending::MaskFromSel : Pending::AddMask;
      ImGui::EndDisabled();
      FormParts::Inline(0.0f, tok::space_1);
      ImGui::BeginDisabled(sSel.empty());
      if (IconChip("##v2ddup", IconsInfinite::Copy, false, Sc(T("Duplicate (Ctrl+J)"))))
         sPending.what = Pending::Duplicate;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2ddel", IconsInfinite::Close, false, T("Delete layer")))
         sPending.what = Pending::Delete;
      ImGui::EndDisabled();
   }

   // ---- tabs ------------------------------------------------------------

   // part 0: the layer's own settings and mask; part 1: where it sits (transform, 3D, crop).
   void DrawLayerTab(Scene2DNode* doc, int part)
   {
      Layer* l = Active(doc);
      if (l == nullptr)
      {
         CentredText(ImGui::GetCursorScreenPos().y, T("No layer selected"));
         return;
      }
      if (part == 1 && (l->kind == Kind::Group || l->kind == Kind::Fill || l->kind == Kind::Adjust))
      {
         CentredText(ImGui::GetCursorScreenPos().y, T("Nothing to transform"));
         return;
      }
      if (part == 0)
      {
      SectionCard::Begin(T("Layer"));
      if (l->kind == Kind::Node)
      {
         NodeRow(T("Source"), &l->uid, false, doc);
         if (ActionButton::Draw(sPickMode == 2 ? T("Click a node on the canvas...") : T("Replace from canvas..."), ImVec2(-1.0f, 0.0f),
                                sPickMode == 2 ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
            sPickMode = sPickMode == 2 ? 0 : 2;
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
            ModNext(*l, Scene2DNode::kLfFillAngle);
            SliderRow(T("Angle"), &l->fillAngle, -180.0f, 180.0f, "%.0f\xC2\xB0");
      }
      if (l->kind == Kind::Adjust)
      {
         SectionCard::Begin(T("Adjustment"));
         std::vector<std::string> names;
         for (int t = 0; t < Scene2DNode::kAdjCount; t++)
            names.push_back(T(Scene2DNode::AdjName(t)));
         int type = l->adjType;
         if (ComboRow(T("Type"), &type, names))
         {
            l->adjType = type;
            l->adjAmt = l->adjTo = Scene2DNode::AdjDefault(type);
         }
         float lo = 0, hi = 1;
         Scene2DNode::AdjRange(l->adjType, lo, hi);
         const char* fmt = AdjFmt(l->adjType);
         if (l->adjType != Scene2DNode::kAdjGradientMap)
         {
            ModNext(*l, Scene2DNode::kLfAdjust);
            SliderRow(T("Amount"), &l->adjAmt, lo, hi, fmt);
            int drive = l->adjDrive;
            if (ComboRow(T("Drive"), &drive, { T("None"), "A", "B", "C", "D" }))
               l->adjDrive = drive;
            if (l->adjDrive > 0)
               SliderRow(T("To"), &l->adjTo, lo, hi, fmt);
            BoolRow(T("Map colours"), &l->adjMap);
         }
         if (l->adjType == Scene2DNode::kAdjGradientMap || l->adjMap)
         {
            ColorRow(T("Shadows"), l->colorA);
            ColorRow(T("Highlights"), l->colorB);
         }
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
         {
            auto undo = []() { PushUndoCheckpoint("Text style"); };
            auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
            FieldLabel(T("Font"));
            if (FontPicker("##v2dtabfont", l->font, sFieldW, undo))
               Touch();
            FieldLabel(T("Style"));
            if (TextStyleChips(&l->bold, &l->italic, &l->underline, &l->caps, undo, next))
               Touch();
            FieldLabel(T("Align"));
            static const char* kGlyphs[3] = { IconsInfinite::TextLeft, IconsInfinite::TextCenter, IconsInfinite::TextRight };
            static const char* kTips[3] = { "Left align text", "Centre text", "Right align text" };
            for (int i = 0; i < 4; i++)
            {
               if (i > 0)
                  next();
               const std::string id = std::string("##v2dtabal") + std::to_string(i);
               if ((i < 3 ? IconChip(id.c_str(), kGlyphs[i], l->align == i, T(kTips[i])) : JustifyChip(id.c_str(), l->align == i)) &&
                   l->align != i)
               {
                  undo();
                  l->align = i;
                  Touch();
               }
            }
         }
         ModNext(*l, Scene2DNode::kLfFontSize);
         SliderRow(T("Size"), &l->fontSize, 4.0f, 800.0f, "%.0f px");
         ModNext(*l, Scene2DNode::kLfLeading);
         SliderRow(T("Leading"), &l->leading, 0.5f, 3.0f, "%.2f");
         ModNext(*l, Scene2DNode::kLfTracking);
         SliderRow(T("Tracking"), &l->tracking, -100.0f, 400.0f, "%.0f");
         ColorRow(T("Colour"), l->textC);
         DragRow(T("Box width"), &l->boxW, 1.0f, "%.0f px", 0.0f, 8192.0f);
         FieldLabel("");
         if (TextChip(L("Convert to shapes##v2dtoshapes"), false, T("Outlines as a vector layer")))
            sPending.what = Pending::ToShapes;
      }
      if (l->kind == Kind::Vector)
      {
         SectionCard::Begin(T("Vector"));
         ImGui::TextDisabled("%d %s", (int)l->shapes.size(), T("items"));
      }
      }
      if (part == 1)
      {
         SectionCard::Begin(T("Transform"));
         {
            float* const pos[2] = { &l->x, &l->y };
            const char* const fmts[2] = { "X %.0f", "Y %.0f" };
            const int mf[2] = { Scene2DNode::kLfX, Scene2DNode::kLfY };
            DragMulti(T("Position"), pos, fmts, 2, 1.0f, -1e6f, 1e6f, l, mf);
         }
         float bw = 1, bh = 1;
         doc->LayerBox(*l, bw, bh);
         float wpx = bw * l->sx, hpx = bh * l->sy;
         {
            float* const size[2] = { &wpx, &hpx };
            const char* const fmts[2] = { "W %.0f", "H %.0f" };
            const int c = DragMulti(T("Size"), size, fmts, 2, 1.0f);
            if (c & 1)
               l->sx = bw > 0 ? wpx / bw : 1.0f;
            if (c & 2)
               l->sy = bh > 0 ? hpx / bh : 1.0f;
         }
         {
            // Scale in percent, as Photoshop's options bar: typed, dragged, or locked to one ratio.
            float pct[2] = { l->sx * 100.0f, l->sy * 100.0f };
            float* const pp[2] = { &pct[0], &pct[1] };
            const char* const pf[2] = { "W %.1f%%", "H %.1f%%" };
            const int mf[2] = { Scene2DNode::kLfScaleX, Scene2DNode::kLfScaleY };
            const int c = DragMulti(T("Scale"), pp, pf, 2, 0.5f, 0.1f, 10000.0f, l, mf);
            if (c != 0)
            {
               if (sLockScale)
               {
                  const float k = (c & 1) ? pct[0] / std::max(0.1f, l->sx * 100.0f) : pct[1] / std::max(0.1f, l->sy * 100.0f);
                  l->sx *= k;
                  l->sy *= k;
               }
               else
               {
                  if (c & 1) l->sx = pct[0] / 100.0f;
                  if (c & 2) l->sy = pct[1] / 100.0f;
               }
            }
            BoolRow(T("Keep proportions"), &sLockScale);
         }
         // 3D: the layer is a card the comp camera sees; Z pushes it away, X / Y / Z rotate it.
         BoolRow(T("3D"), &l->is3d);
         if (l->is3d)
         {
            ModNext(*l, Scene2DNode::kLfZ);
            DragRow(T("Depth"), &l->z, 1.0f, "Z %.0f");
            float* const rot[3] = { &l->rx, &l->ry, &l->angle };
            const char* const fmts[3] = { "X %.0f\xC2\xB0", "Y %.0f\xC2\xB0", "Z %.0f\xC2\xB0" };
            const int mf[3] = { Scene2DNode::kLfRotX, Scene2DNode::kLfRotY, Scene2DNode::kLfAngle };
            DragMulti(T("Rotate"), rot, fmts, 3, 0.5f, -360.0f, 360.0f, l, mf);
         }
         else
         {
            ModNext(*l, Scene2DNode::kLfAngle);
            DragRow(T("Angle"), &l->angle, 0.5f, "%.1f\xC2\xB0", -180.0f, 180.0f);
         }
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
            l->z = l->rx = l->ry = 0.0f;
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
      if (part == 0 && doc->CanSurfacePaint(l->id))
      {
         SectionCard::Begin(T("Surface"));
         BoolRow(T("Paint on surface"), &sSurfacePaint);
         if (!l->HasSkin())
            ComboRow(T("Skin"), &sSkinSide, { "512", "1024", "2048", "4096" });
         else
         {
            const std::string sk = std::to_string(l->pw) + " px";
            ImGui::TextUnformatted(sk.c_str());
         }
         const char* const axis[3] = { "X##v2dsmx", "Y##v2dsmy", "Z##v2dsmz" };
         ImGui::TextUnformatted(T("Mirror"));
         for (int i = 0; i < 3; i++)
         {
            ImGui::SameLine();
            if (TextChip(axis[i], (l->skinMirror >> i & 1) != 0, T("Mirror strokes across the object")))
            {
               PushUndoCheckpoint("Surface mirror");
               l->skinMirror ^= 1 << i;
               Touch();
            }
         }
         if (l->HasSkin() && TextChip(L("Clear paint##v2dskc"), false, T("Remove all paint from the surface")))
         {
            PushUndoCheckpoint("Clear surface paint");
            doc->ClearSkin(l->id);
            Touch();
         }
      }
      if (part == 0)
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
            ModNext(*l, Scene2DNode::kLfMaskDensity);
            SliderRow(T("Density"), &l->maskDensity, 0.0f, 1.0f, "%.2f");
         }
      }
   }

   // Layer effects (Photoshop's Layer Style): each one on or off, with its own settings, for the active layer.
   void DrawEffectsTab(Scene2DNode* doc)
   {
      Layer* l = Active(doc);
      if (l == nullptr || l->kind == Kind::Group || l->kind == Kind::Adjust)
      {
         CentredText(ImGui::GetCursorScreenPos().y, l == nullptr ? T("No layer selected") : T("No effects here"));
         return;
      }
      for (int t = 0; t < Scene2DNode::kFxCount; t++)
      {
         Scene2DNode::Fx& f = l->fx[t];
         ImGui::PushID(t);
         SectionCard::Begin(T(Scene2DNode::FxName(t)));
         BoolRow(T("On"), &f.on);
         if (f.on)
         {
            const bool two = t == Scene2DNode::kFxBevel || t == Scene2DNode::kFxGradient;
            ColorRow(T(t == Scene2DNode::kFxBevel ? "Highlight" : two ? "Start" : "Colour"), f.c);
            if (two)
               ColorRow(T(t == Scene2DNode::kFxBevel ? "Shadow" : "End"), f.c2);
            const int fb = Scene2DNode::kLfFxBase + t * Scene2DNode::kLfFxFields;
            ModNext(*l, fb + 3);
            SliderRow(T("Opacity"), &f.c[3], 0.0f, 1.0f, "%.2f");
            if (t == Scene2DNode::kFxShadow || t == Scene2DNode::kFxInner || two)
            {
               ModNext(*l, fb + 2);
               SliderRow(T("Angle"), &f.angle, -180.0f, 180.0f, "%.0f\xC2\xB0");
            }
            if (t == Scene2DNode::kFxShadow || t == Scene2DNode::kFxInner || t == Scene2DNode::kFxBevel)
            {
               ModNext(*l, fb + 1);
               if (t == Scene2DNode::kFxBevel)
                  SliderRow(T("Depth"), &f.dist, 0.0f, 200.0f, "%.0f%%");
               else
                  SliderRow(T("Distance"), &f.dist, 0.0f, 200.0f, "%.0f px");
            }
            if (t != Scene2DNode::kFxOverlay && t != Scene2DNode::kFxGradient)
            {
               ModNext(*l, fb);
               SliderRow(T(t == Scene2DNode::kFxStroke ? "Width" : "Size"), &f.size, 0.0f, 200.0f, "%.0f px");
            }
            if (t == Scene2DNode::kFxShadow || t == Scene2DNode::kFxInner || t == Scene2DNode::kFxGlow || t == Scene2DNode::kFxInnerGlow)
               SliderRow(T("Spread"), &f.spread, 0.0f, 1.0f, "%.2f");
            if (t == Scene2DNode::kFxStroke)
            {
               int pos = f.pos;
               if (ComboRow(T("Position"), &pos, { T("Outside"), T("Inside"), T("Centre") }))
                  f.pos = pos;
            }
         }
         ImGui::PopID();
      }
   }

   // Colour: the adjustment layers as one panel, a click adds one above the active layer.
   void DrawColourTab(Scene2DNode* doc)
   {
      SectionCard::Begin(T("Adjustments"));
      for (int t = 0; t < Scene2DNode::kAdjCount; t++)
         if (ActionButton::Draw((std::string(T(Scene2DNode::AdjName(t))) + "##v2dcadj" + std::to_string(t)).c_str(), ImVec2(-1.0f, 0.0f), ActionButton::Kind::Plain))
         {
            sPending.what = Pending::AddAdjust;
            sPending.arg = t;
         }
      // Every adjustment in the document keeps its card, top of the stack first: adding a second one never takes
      // the first one's settings away.
      std::vector<Layer>& ls = doc->Layers();
      for (int i = (int)ls.size() - 1; i >= 0; i--)
      {
         Layer* l = &ls[i];
         if (l->kind != Kind::Adjust)
            continue;
         ImGui::PushID(l->id);
         SectionCard::Begin(LayerName(*l).c_str());
         BoolRow(T("On"), &l->visible);
         float lo = 0, hi = 1;
         Scene2DNode::AdjRange(l->adjType, lo, hi);
         if (l->adjType != Scene2DNode::kAdjGradientMap)
            SliderRow(T("Amount"), &l->adjAmt, lo, hi, AdjFmt(l->adjType));
         if (l->adjType == Scene2DNode::kAdjGradientMap || l->adjMap)
         {
            ColorRow(T("Shadows"), l->colorA);
            ColorRow(T("Highlights"), l->colorB);
         }
         ImGui::PopID();
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
      static int sCanvasW = 0, sCanvasH = 0, sAnchor = 4;
      if (sSizeFor != sDocUid || sSizeSeenW != d.w || sSizeSeenH != d.h)
      {
         sSizeFor = sDocUid;
         sSizeSeenW = sCanvasW = sImageW = d.w;
         sSizeSeenH = sCanvasH = sImageH = d.h;
      }
      SectionCard::Begin(T("Canvas size"));
      {
         // Common sizes, as the new-document dialogs of Photoshop and Affinity offer them.
         static const int kPresets[][2] = { { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }, { 1080, 1080 },
                                            { 1080, 1350 }, { 1080, 1920 }, { 2048, 2048 }, { 2480, 3508 }, { 3508, 2480 } };
         static const char* const kPresetNames[] = { "HD 1280 x 720", "Full HD 1920 x 1080", "QHD 2560 x 1440", "4K 3840 x 2160",
                                                     "Square 1080", "Portrait 1080 x 1350", "Story 1080 x 1920", "Square 2048",
                                                     "A4 300 dpi", "A4 landscape 300 dpi" };
         int match = -1;
         for (int i = 0; i < 10; i++)
            if (kPresets[i][0] == sCanvasW && kPresets[i][1] == sCanvasH)
               match = i;
         int pick = match + 1;
         if (ComboRow(T("Preset"), &pick, { T("Custom"), kPresetNames[0], kPresetNames[1], kPresetNames[2], kPresetNames[3],
                                            kPresetNames[4], kPresetNames[5], kPresetNames[6], kPresetNames[7], kPresetNames[8],
                                            kPresetNames[9] }) &&
             pick > 0)
         {
            sCanvasW = kPresets[pick - 1][0];
            sCanvasH = kPresets[pick - 1][1];
         }
      }
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
      ComboRow(T("Image"), &sImageFormat, { "PNG", "JPEG" });
      if (OutputNode* o = sImageFormat == 1 ? FindOutput() : nullptr)
      {
         float q = (float)std::clamp(o->jpegQuality, 1, 100);
         if (SliderRow(T("Quality"), &q, 1.0f, 100.0f, "%.0f"))
            o->jpegQuality = (int)std::lround(q);
      }
      ImGui::BeginDisabled(gOfflineRender.active);
      if (ActionButton::Draw(L("Render Image##v2dimg"), ImVec2(availW, 0)))
         sPending.what = Pending::RenderImage;
      ImGui::EndDisabled();
      ImGui::TextDisabled("%d x %d, F12", d.w, d.h);
      SectionCard::Begin(T("Animation"));
      ComboRow(T("Video"), &sVideoFormat, { "MP4", "MOV" });
      DragRow(T("Frame rate"), &d.fps, 0.1f, "%.0f fps", 1.0f, 240.0f);
      DragRow(T("Length"), &d.seconds, 0.1f, "%.1f s", 0.1f, 3600.0f);
      ImGui::BeginDisabled(gOfflineRender.active);
      if (ActionButton::Draw(L("Render Animation##v2danim"), ImVec2(availW, 0)))
         sPending.what = Pending::RenderAnim;
      ImGui::EndDisabled();
      ImGui::TextDisabled("%s", Sc("Ctrl+F12"));
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
            if (!AuditNoDialog())
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
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, (tok::tile - ImGui::GetFontSize()) * 0.5f));
      const bool docsOpen = MenuParts::BeginCombo("##v2ddocs", self != nullptr ? nameOf(self).c_str() : "");
      ImGui::PopStyleVar();
      if (docsOpen)
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

      // The tab column, as the 3D View: each group of settings one click away instead of down one long scroll.
      const struct { const char* id; const char* icon; const char* tip; } kTabs[7] = {
         { "##v2dt0", IconsInfinite::Layers, "Layers" },
         { "##v2dt1", IconsInfinite::Edit, "Layer" },
         { "##v2dt2", IconsInfinite::ToolMove, "Transform" },
         { "##v2dt3", IconsInfinite::Crop, "Document" },
         { "##v2dt4", IconsInfinite::ShadeRender, "Render" },
         { "##v2dt5", IconsInfinite::FamilyEffects, "Effects" },
         { "##v2dt6", IconsInfinite::Curve, "Colour" },
      };
      sTab = std::clamp(sTab, 0, 6);
      ImGui::BeginGroup();
      for (int i = 0; i < 7; i++)
         if (IconChip(kTabs[i].id, kTabs[i].icon, sTab == i, T(kTabs[i].tip)))
            sTab = i;
      ImGui::EndGroup();
      ActionButton::RowNext();

      sChip = kSideChip;
      if (sTab == 0)
      {
         ImGui::BeginChild("##v2dlayers", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
         const float availW = ImGui::GetContentRegionAvail().x;
         DrawDocPicker(doc, availW);
         const float roomH = ImGui::GetContentRegionAvail().y;
         DrawLayersCard(doc, availW, std::max(3.0f * (kRowH + 1.0f), roomH - 4.0f));
         ImGui::EndChild();
      }
      else
      {
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SectionCard::kPad, 0.0f));
         ImGui::BeginChild("##v2dprops", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
         ImGui::PopStyleVar();
         SectionCard::BeginWindow();
         ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
         const float w = ImGui::GetContentRegionAvail().x;
         sFieldW = std::max(70.0f, w - kLabelW);
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 3.0f));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 3.0f));
         if (sTab == 1) DrawLayerTab(doc, 0);
         else if (sTab == 2) DrawLayerTab(doc, 1);
         else if (sTab == 3) DrawDocumentTab(doc);
         else if (sTab == 5) DrawEffectsTab(doc);
         else if (sTab == 6) DrawColourTab(doc);
         else DrawOutputTab(doc, w);
         ImGui::PopStyleVar(2);
         SectionCard::EndWindow();
         ImGui::EndChild();
      }
      ImGui::EndChild();
      sChip = tok::tile;
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
      case Pending::LayerOutput:
         if (Layer* l = Active(doc))
         {
            PushUndoCheckpoint("Layer as output");
            l->output = !l->output;
            // Other nodes read it from the canvas, so the document becomes a node.
            if (l->output && doc->panelOnly && self != nullptr)
            {
               doc->panelOnly = false;
               self->hiddenFromCanvas = false;
               gRequestSelectNodeIndex = self->index;
            }
            doc->Commit();
         }
         break;
      case Pending::LayerBelow:
         if (Layer* l = Active(doc))
         {
            PushUndoCheckpoint("Effect on below");
            l->below = !l->below;
            doc->Commit();
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
      case Pending::Copy:
      case Pending::Cut:
         if (!sSel.empty())
         {
            sClip = doc->CopyLayers(sSel);
            sPasteCount = 0;
            if (todo.what == Pending::Cut)
               DeleteSelected(doc);
         }
         break;
      case Pending::Paste:
         if (!sClip.empty())
         {
            PushUndoCheckpoint("Paste layer");
            Layer* a = Active(doc);
            sPasteCount++;
            const float nudge = 16.0f * sPasteCount;
            sSel = doc->PasteLayers(sClip, a != nullptr ? a->id : 0, nudge, nudge);
         }
         break;
      case Pending::Delete: DeleteSelected(doc); break;
      case Pending::AddFill: AddFill(doc); break;
      case Pending::AddAdjust: AddAdjust(doc, todo.arg); break;
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
      case Pending::ToShapes:
         if (Layer* a = Active(doc))
            if (a->kind == Kind::Text)
            {
               PushUndoCheckpoint("Convert to shapes");
               if (const int vid = doc->ConvertTextToShapes(a->id))
               {
                  // Straight into Direct select on the first letter: its anchors and handles show, a click picks another.
                  sSel.assign(1, vid);
                  sTool = kNode;
                  sVecTarget = 0;
                  sItem = 0;
                  sItemLayer = vid;
                  Touch();
               }
            }
         break;
      case Pending::AddNode:
      {
         PushUndoCheckpoint("Add node as layer");
         AddNodeLayer(doc, todo.uid, nullptr);
         break;
      }
      case Pending::ReplaceNode:
      {
         const int id = sSel.empty() ? 0 : sSel.back();
         PushUndoCheckpoint("Replace layer source");
         ReplaceSource(doc, id, todo.uid);
         break;
      }
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

   // Files dropped on the 2D View: each one spawns its source node (the same node types the canvas
   // drop makes) off-canvas and lands as a layer centred on the drop point. Returns the layers added.
   int DropFiles(Scene2DNode* doc, const std::vector<std::string>& files, ImVec2 docPt)
   {
      static const std::vector<std::string> kModels = { "obj", "ply", "stl", "usd", "usda", "usdc", "usdz", "abc" };
      static const std::vector<std::string> kGltf = { "gltf", "glb" };
      static const std::vector<std::string> kSvg = { "svg" };
      GraphNode* self = DocGraphNode();
      const ImVec2 base = self != nullptr ? NodePos(*self) : ImVec2(0, 0);
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      PushUndoCheckpoint("Drop into 2D View");
      gSuppressUndoCheckpoints = true;
      int added = 0;
      auto place = [&](GraphNode* gn) {
         if (gn == nullptr)
            return;
         gn->hiddenFromCanvas = true;
         const ImVec2 at(docPt.x + 24.0f * (float)added, docPt.y + 24.0f * (float)added);
         if (AddNodeLayer(doc, gn->uid, &at) != 0)
            added++;
      };
      float row = 0.0f;
      for (const std::string& path : files)
      {
         const ImVec2 spot(base.x - 360.0f, base.y + row);
         row += 200.0f;
         if (HasExtension(path, kSvg))
         {
            if (ImportSvgInto(doc, path))
               added++;
         }
         else if (HasExtension(path, kGltf))
         {
            for (int index : SpawnGltfParts(path, spot.x, spot.y))
               place(FindNodeByIndex(index));
         }
         else if (HasExtension(path, kModels))
         {
            GraphNode* gn = SpawnNode("Model 3D", "3D", spot.x, spot.y);
            if (gn != nullptr)
               static_cast<ModelSourceNode*>(gn->node.get())->Load(path);
            place(gn);
         }
         else if (HasExtension(path, MediaExtensions::Video()))
         {
            GraphNode* gn = SpawnNode("Video", "Source", spot.x, spot.y);
            if (gn != nullptr)
               static_cast<VideoSourceNode*>(gn->node.get())->Open(path);
            place(gn);
         }
         else if (HasExtension(path, MediaExtensions::Image()))
         {
            GraphNode* gn = SpawnNode("Image Source", "Source", spot.x, spot.y);
            if (gn != nullptr)
               static_cast<ImageSourceNode*>(gn->node.get())->Load(path);
            place(gn);
         }
         // Anything else (audio, patches, plugins) has no picture to put on a layer.
      }
      gSuppressUndoCheckpoints = wasSuppressed;
      if (added > 0)
         gPatchDirty = true;
      return added;
   }

   void TakeDroppedFiles(Scene2DNode* doc)
   {
      if (gDroppedFiles.empty() || !gView2DOpen || gDropPos.x < gView2DRectMin.x || gDropPos.x >= gView2DRectMax.x ||
          gDropPos.y < gView2DRectMin.y || gDropPos.y >= gView2DRectMax.y)
         return;
      const std::vector<std::string> files = std::move(gDroppedFiles);
      gDroppedFiles.clear();
      DropFiles(doc, files, ToDoc(doc, gDropPos));
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
      sPanelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

      const float headTop = ImGui::GetCursorScreenPos().y;
      DrawHeader(doc);   // tile height, as Sample Studio's and the Piano Roll's bars
      DrawDockMenu(headTop, ImGui::GetCursorScreenPos().y);
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float gapX = ImGui::GetStyle().ItemSpacing.x;
      const float sideW = sSideOpen ? std::min(kSideW, avail.x * 0.5f) : 0.0f;
      const float viewW = std::max(120.0f, avail.x - sideW - (sSideOpen ? gapX : 0.0f));
      const float statusH = CompactRowH() + ImGui::GetStyle().ItemSpacing.y;
      const float canvasH = std::max(80.0f, avail.y - statusH);
      const float stripW = StripWidth(StripColumns(canvasH));
      DrawToolStrip(canvasH, doc);
      ActionButton::RowNext();
      ImGui::BeginGroup();
      DrawCanvas(doc, ImVec2(viewW - stripW - tok::space_1, canvasH));
      {
         CompactBar bar;
         DrawStatus(doc);
      }
      ImGui::EndGroup();
      if (sSideOpen)
      {
         ActionButton::RowNext();
         DrawSide(doc, sideW);
      }
      if (sDirty)
         doc->Commit();
      TakeDroppedFiles(doc);
      if (sPickedUid != 0)
      {
         // A node clicked on the canvas while a pick mode was on.
         if (sPickMode == 1)
         {
            sPending.what = Pending::AddNode;
            sPending.uid = sPickedUid;
         }
         else if (sPickMode == 2)
         {
            sPending.what = Pending::ReplaceNode;
            sPending.uid = sPickedUid;
         }
         sPickedUid = 0;
         sPickMode = 0;
      }
      RunPending(doc);
   }
}

OutputNode* View2DEnsureOutput() { return EnsureOutputImpl(); }

bool View2DImportSvg(const std::string& path)
{
   return ImportSvgInto(FindDoc(), path);
}

static bool ImportSvgInto(Scene2DNode* doc, const std::string& path)
{
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

static bool Is3DNode(INode* n);
// ---- H5: the 2D View for agents (RPC "view2d", CLI `infinite view2d`) ------------------------------------------
// Every write is one batch through a shape gate: each call is checked first (layer ids, field names, kinds, ranges)
// and a bad call fails the whole batch with typed errors and the closest names, the document untouched. A batch that
// passes lands as one undo step "Agent: <label>".
namespace
{
   using nlohmann::json;
   const char* kV2KindNames[Scene2DNode::kKindCount] = { "node", "fill", "group", "paint", "vector", "text", "adjust" };

   std::string V2Key(const std::string& s)
   {
      std::string k;
      for (char c : s)
         if (std::isalnum((unsigned char)c))
            k += (char)std::tolower((unsigned char)c);
      return k;
   }

   int V2Edit(const std::string& a, const std::string& b)
   {
      std::vector<int> row(b.size() + 1);
      for (size_t j = 0; j <= b.size(); j++)
         row[j] = (int)j;
      for (size_t i = 1; i <= a.size(); i++)
      {
         int diag = row[0];
         row[0] = (int)i;
         for (size_t j = 1; j <= b.size(); j++)
         {
            const int up = row[j];
            row[j] = std::min({ row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1) });
            diag = up;
         }
      }
      return row[b.size()];
   }

   json V2Near(const std::string& want, const std::vector<std::string>& names)
   {
      std::vector<std::pair<int, std::string>> d;
      for (const std::string& n : names)
         d.push_back({ V2Edit(V2Key(want), V2Key(n)), n });
      std::stable_sort(d.begin(), d.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
      json out = json::array();
      for (size_t i = 0; i < d.size() && i < 4; i++)
         out.push_back(d[i].second);
      return out;
   }

   const std::vector<std::string> kV2Extra = { "name", "visible", "blend", "text", "color", "colorB", "is3d", "depthScale", "output", "below" };

   std::vector<std::string> V2FieldNames()
   {
      std::vector<std::string> out;
      for (int f = 0; f < Scene2DNode::kLfCount; f++)
         out.push_back(Scene2DNode::LayerFieldName(f));
      out.insert(out.end(), kV2Extra.begin(), kV2Extra.end());
      return out;
   }

   int V2Field(const std::string& name)
   {
      const std::string k = V2Key(name);
      for (int f = 0; f < Scene2DNode::kLfCount; f++)
         if (V2Key(Scene2DNode::LayerFieldName(f)) == k)
            return f;
      return -1;
   }

   int V2Blend(const json& v)
   {
      const auto& names = BlendModes::Names();
      if (v.is_number_integer())
         return v.get<int>() >= 0 && v.get<int>() < (int)names.size() ? v.get<int>() : -1;
      if (v.is_string())
         for (size_t i = 0; i < names.size(); i++)
            if (V2Key(names[i]) == V2Key(v.get<std::string>()))
               return (int)i;
      return -1;
   }

   bool V2Color(const json& v, float out[4])
   {
      if (!v.is_array() || v.size() < 3 || v.size() > 4)
         return false;
      for (size_t i = 0; i < v.size(); i++)
      {
         if (!v[i].is_number())
            return false;
         out[i] = std::clamp(v[i].get<float>(), 0.0f, 1.0f);
      }
      if (v.size() == 3)
         out[3] = 1.0f;
      return true;
   }

   json V2Layer(Scene2DNode* doc, const Layer& l)
   {
      json fields = json::object();
      Layer copy = l;
      for (int f = 0; f < Scene2DNode::kLfCount; f++)
         if (Scene2DNode::LayerFieldApplies(l, f))
            if (const float* v = Scene2DNode::LayerFieldPtr(copy, f))
               fields[Scene2DNode::LayerFieldName(f)] = *v;
      json out = { { "id", l.id }, { "kind", kV2KindNames[(int)l.kind] }, { "name", LayerName(l) }, { "parent", l.parent },
                   { "visible", l.visible }, { "blend", BlendModes::Names()[std::clamp(l.blend, 0, (int)BlendModes::Names().size() - 1)] },
                   { "is3d", l.is3d }, { "fields", fields } };
      if (l.kind == Kind::Node)
         if (GraphNode* gn = FindNodeByUid(l.uid))
            out["node"] = gn->index;
      if (l.kind == Kind::Text)
         out["text"] = l.text;
      (void)doc;
      return out;
   }

   struct V2Err
   {
      json list = json::array();
      void Add(size_t call, const char* code, const std::string& msg, json near = nullptr)
      {
         json e = { { "code", code }, { "call", (int)call }, { "message", msg } };
         if (!near.is_null())
            e["near"] = near;
         list.push_back(e);
      }
   };

   // Shape gate for one call. `made` holds the kinds the batch's earlier adds will make (layer -1 = the last one).
   void V2Check(Scene2DNode* doc, size_t i, const json& c, std::vector<Kind>& made, V2Err& err)
   {
      const std::string op = c.value("op", std::string());
      auto layerKind = [&](const char* key, bool required, Kind& kind) {
         if (!c.contains(key))
         {
            if (required)
               err.Add(i, "E_LAYER", std::string("'") + key + "' is missing");
            return false;
         }
         if (!c[key].is_number_integer())
         {
            err.Add(i, "E_LAYER", std::string("'") + key + "' wants a layer id");
            return false;
         }
         const int id = c[key].get<int>();
         if (id == -1)
         {
            if (made.empty())
            {
               err.Add(i, "E_LAYER", "layer -1 means the batch's last add; there is none before this call");
               return false;
            }
            kind = made.back();
            return true;
         }
         const Layer* l = doc != nullptr ? doc->FindLayer(id) : nullptr;
         if (l == nullptr)
         {
            json ids = json::array();
            if (doc != nullptr)
               for (const Layer& x : doc->Layers())
                  ids.push_back(x.id);
            err.Add(i, "E_LAYER", "no layer " + std::to_string(id), ids);
            return false;
         }
         kind = l->kind;
         return true;
      };
      Kind kind = Kind::Fill;
      if (op == "add")
      {
         const std::string k = c.value("kind", std::string());
         int found = -1;
         for (int n = 0; n < Scene2DNode::kKindCount; n++)
            if (k == kV2KindNames[n])
               found = n;
         if (found < 0 || found == (int)Kind::Vector)
         {
            err.Add(i, "E_KIND", "'" + k + "' is not a kind to add", json::array({ "fill", "text", "paint", "group", "adjust", "node" }));
            return;
         }
         if (found == (int)Kind::Node)
         {
            GraphNode* gn = c.contains("node") && c["node"].is_number_integer() ? FindNodeByIndex(c["node"].get<int>()) : nullptr;
            if (gn == nullptr)
               err.Add(i, "E_NODE", "'node' wants a canvas node index");
            else if (!View2DCanAdd(*gn))
               err.Add(i, "E_NODE", "node " + std::to_string(gn->index) + " (" + gn->typeName + ") cannot be a layer");
         }
         if (found == (int)Kind::Adjust)
         {
            const std::string t = c.value("type", std::string("Pixelate"));
            std::vector<std::string> names;
            bool ok = false;
            for (int a = 0; a < Scene2DNode::kAdjCount; a++)
            {
               names.push_back(Scene2DNode::AdjName(a));
               ok = ok || V2Key(names.back()) == V2Key(t);
            }
            if (!ok)
               err.Add(i, "E_ENUM", "'" + t + "' is not an adjustment", V2Near(t, names));
         }
         if (c.contains("above"))
            layerKind("above", false, kind);
         made.push_back((Kind)found);
      }
      else if (op == "set")
      {
         if (!layerKind("layer", true, kind))
            return;
         if (!c.contains("values") || !c["values"].is_object())
         {
            err.Add(i, "E_SHAPE", "'values' wants an object of field: value");
            return;
         }
         Layer probe;
         probe.kind = kind;
         probe.is3d = true;
         for (auto it = c["values"].begin(); it != c["values"].end(); ++it)
         {
            const std::string key = it.key();
            const json& v = it.value();
            const int f = V2Field(key);
            if (f >= 0)
            {
               if (!v.is_number())
                  err.Add(i, "E_VALUE", "'" + key + "' wants a number");
               else if (!Scene2DNode::LayerFieldApplies(probe, f))
                  err.Add(i, "E_FIELD_NA", "'" + key + "' does not apply to a " + kV2KindNames[(int)kind] + " layer");
               continue;
            }
            const std::string k = V2Key(key);
            if (k == "name" || k == "text")
            {
               if (!v.is_string())
                  err.Add(i, "E_VALUE", "'" + key + "' wants a string");
               else if (k == "text" && kind != Kind::Text)
                  err.Add(i, "E_FIELD_NA", "'text' is for text layers");
            }
            else if (k == "visible" || k == "is3d" || k == "output" || k == "below")
            {
               if (!v.is_boolean())
                  err.Add(i, "E_VALUE", "'" + key + "' wants true or false");
            }
            else if (k == "blend")
            {
               if (V2Blend(v) < 0)
                  err.Add(i, "E_ENUM", "not a blend mode", V2Near(v.is_string() ? v.get<std::string>() : "", BlendModes::Names()));
            }
            else if (k == "color" || k == "colorb")
            {
               float tmp[4];
               if (!V2Color(v, tmp))
                  err.Add(i, "E_VALUE", "'" + key + "' wants [r, g, b] or [r, g, b, a], 0..1");
            }
            else if (k == "depthscale")
            {
               if (!v.is_number() || std::fabs(v.get<float>()) < 0.01f)
                  err.Add(i, "E_VALUE", "'depthScale' wants a number, not near 0");
            }
            else
               err.Add(i, "E_FIELD", "no field '" + key + "'", V2Near(key, V2FieldNames()));
         }
      }
      else if (op == "move")
      {
         Kind k2;
         layerKind("layer", true, kind);
         layerKind("target", true, k2);
         if (c.value("into", false) && k2 != Kind::Group)
            err.Add(i, "E_LAYER", "'into' wants a group target");
      }
      else if (op == "remove")
         layerKind("layer", true, kind);
      else if (op == "select")
      {
         const std::string how = c.value("shape", std::string("rect"));
         static const std::vector<std::string> kShapes = { "rect", "ellipse", "all", "none", "invert" };
         if (std::find(kShapes.begin(), kShapes.end(), how) == kShapes.end())
            err.Add(i, "E_ENUM", "'" + how + "' is not a selection shape", V2Near(how, kShapes));
         else if ((how == "rect" || how == "ellipse") && !(c.contains("rect") && c["rect"].is_array() && c["rect"].size() == 4))
            err.Add(i, "E_SHAPE", "'rect' wants [x0, y0, x1, y1] in document px");
      }
      else if (op == "fill" || op == "stroke")
      {
         if (layerKind("layer", true, kind) && kind != Kind::Paint)
            err.Add(i, "E_FIELD_NA", op + " paints on a paint layer");
         float tmp[4];
         if (c.contains("color") && !V2Color(c["color"], tmp))
            err.Add(i, "E_VALUE", "'color' wants [r, g, b], 0..1");
         if (op == "fill" && !(c.contains("at") && c["at"].is_array() && c["at"].size() == 2))
            err.Add(i, "E_SHAPE", "'at' wants [x, y] in document px");
         if (op == "stroke" && !(c.contains("points") && c["points"].is_array() && c["points"].size() >= 2 && c["points"].size() % 2 == 0))
            err.Add(i, "E_SHAPE", "'points' wants [x0, y0, x1, y1, ...] in document px");
      }
      else
         err.Add(i, "E_OP", "'" + op + "' is not an op", V2Near(op, { "add", "set", "move", "remove", "select", "fill", "stroke" }));
   }

   // Applies one call that passed the gate; false when it still could not (the batch rolls back).
   bool V2Apply(Scene2DNode* doc, const json& c, int& last, json& made)
   {
      const std::string op = c.value("op", std::string());
      auto id = [&](const char* key) { const int v = c.value(key, 0); return v == -1 ? last : v; };
      if (op == "add")
      {
         const std::string k = c.value("kind", std::string());
         const int above = c.contains("above") ? id("above") : 0;
         int nid = 0;
         if (k == "paint")
            nid = doc->AddPaintLayer(above);
         else if (k == "node")
         {
            GraphNode* gn = FindNodeByIndex(c["node"].get<int>());
            if (gn == nullptr)
               return false;
            Layer& l = doc->AddLayer(Kind::Node, gn->uid, above);
            l.is3d = Is3DNode(gn->node.get());
            nid = l.id;
         }
         else
         {
            const Kind kind = k == "fill" ? Kind::Fill : k == "text" ? Kind::Text : k == "group" ? Kind::Group : Kind::Adjust;
            Layer& l = doc->AddLayer(kind, 0, above);
            if (kind == Kind::Text)
               l.text = c.value("text", std::string("Text"));
            if (kind == Kind::Adjust)
            {
               const std::string t = c.value("type", std::string("Pixelate"));
               for (int a = 0; a < Scene2DNode::kAdjCount; a++)
                  if (V2Key(Scene2DNode::AdjName(a)) == V2Key(t))
                     l.adjType = a;
               l.adjAmt = l.adjTo = Scene2DNode::AdjDefault(l.adjType);
            }
            nid = l.id;
         }
         doc->Commit();
         last = nid;
         made.push_back(nid);
         return nid > 0;
      }
      if (op == "set")
      {
         Layer* l = doc->FindLayer(id("layer"));
         if (l == nullptr)
            return false;
         for (auto it = c["values"].begin(); it != c["values"].end(); ++it)
         {
            const json& v = it.value();
            const int f = V2Field(it.key());
            if (f >= 0)
            {
               float lo = 0, hi = 0;
               doc->LayerFieldRange(*l, f, lo, hi);
               if (float* p = Scene2DNode::LayerFieldPtr(*l, f))
                  *p = hi > lo ? std::clamp(v.get<float>(), lo, hi) : v.get<float>();
               continue;
            }
            const std::string k = V2Key(it.key());
            if (k == "name")
               l->name = v.get<std::string>();
            else if (k == "text")
               l->text = v.get<std::string>();
            else if (k == "visible")
               l->visible = v.get<bool>();
            else if (k == "is3d")
               l->is3d = v.get<bool>();
            else if (k == "output")
               l->output = v.get<bool>();
            else if (k == "below")
               l->below = v.get<bool>() && doc->CanTakeBelow(*l);
            else if (k == "blend")
               l->blend = V2Blend(v);
            else if (k == "depthscale")
               l->sz = v.get<float>();
            else if (k == "color" || k == "colorb")
            {
               float rgba[4];
               V2Color(v, rgba);
               float* dst = k == "colorb" ? l->colorB : l->kind == Kind::Text ? l->textC : l->colorA;
               std::copy(rgba, rgba + 4, dst);
            }
         }
         doc->Commit();
         return true;
      }
      if (op == "move")
      {
         doc->MoveLayer(id("layer"), id("target"), c.value("below", false), c.value("into", false));
         doc->Commit();
         return true;
      }
      if (op == "remove")
      {
         doc->RemoveLayer(id("layer"));
         doc->Commit();
         return true;
      }
      if (op == "select")
      {
         const std::string how = c.value("shape", std::string("rect"));
         const std::string mode = c.value("mode", std::string("new"));
         const int sop = mode == "add" ? Scene2DNode::kSelAdd : mode == "subtract" ? Scene2DNode::kSelSub
                       : mode == "intersect" ? Scene2DNode::kSelIntersect : Scene2DNode::kSelNew;
         if (how == "all")
            doc->SelectAll();
         else if (how == "none")
            doc->Deselect();
         else if (how == "invert")
            doc->InvertSelection();
         else if (how == "expand" || how == "contract" || how == "smooth" || how == "border")
            doc->ModifySelection(how == "expand" ? 0 : how == "contract" ? 1 : how == "smooth" ? 2 : 3, std::clamp(c.value("px", 4), 1, 100));
         else
         {
            const json& r = c["rect"];
            doc->SelectRect(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>(), sop, c.value("feather", 0.0f),
                            how == "ellipse");
         }
         return true;
      }
      if (op == "fill" || op == "stroke")
      {
         float rgba[4] = { 0, 0, 0, 1 };
         if (c.contains("color"))
            V2Color(c["color"], rgba);
         const int lid = id("layer");
         if (op == "fill")
            return doc->Bucket(lid, c["at"][0].get<float>(), c["at"][1].get<float>(), rgba, c.value("opacity", 1.0f),
                               c.value("tolerance", 32), c.value("contiguous", true), false);
         BrushEngine::Brush b;
         b.size = c.value("size", 20.0f);
         b.hardness = c.value("hardness", 1.0f);
         std::copy(rgba, rgba + 3, b.color);
         const json& pts = c["points"];
         if (!doc->BeginPaint(lid, 0, b, pts[0].get<float>(), pts[1].get<float>(), 1.0f))
            return false;
         for (size_t k = 2; k + 1 < pts.size(); k += 2)
            doc->PaintTo(pts[k].get<float>(), pts[k + 1].get<float>(), 1.0f);
         doc->EndPaint();
         return true;
      }
      return false;
   }
}

bool View2DRpc(const json& params, json& out, std::string& error)
{
   const std::string op = params.value("op", std::string());
   Scene2DNode* doc = FindDoc();
   if (op == "layers" || op.empty())
   {
      json layers = json::array();
      if (doc != nullptr)
         for (const Layer& l : doc->Layers())
            layers.push_back(V2Layer(doc, l));
      out = { { "doc", doc != nullptr }, { "layers", layers } };
      if (doc != nullptr)
         out["size"] = { doc->Doc().w, doc->Doc().h };
      return true;
   }
   if (op == "fields")
   {
      out = { { "fields", V2FieldNames() }, { "blends", BlendModes::Names() } };
      return true;
   }
   // Writes: one call or a batch, all through the gate.
   json calls = json::array();
   if (op == "apply")
   {
      if (!params.contains("calls") || !params["calls"].is_array() || params["calls"].empty())
      {
         error = "apply wants 'calls': [{op, ...}, ...]";
         return true;
      }
      calls = params["calls"];
   }
   else
      calls.push_back(params);
   V2Err err;
   std::vector<Kind> made;
   for (size_t i = 0; i < calls.size(); i++)
      if (calls[i].is_object())
         V2Check(doc, i, calls[i], made, err);
      else
         err.Add(i, "E_SHAPE", "each call is an object {op, ...}");
   if (!err.list.empty())
   {
      out = { { "ok", false }, { "errors", err.list } };
      return true;
   }
   if (doc == nullptr)
      doc = MakeDoc(0.0f, 0.0f, "");
   if (doc == nullptr)
   {
      error = "could not make a 2D document";
      return true;
   }
   const std::string label = "Agent: " + params.value("label", op == "apply" ? std::string("2D View edit") : op + " layer");
   PushUndoCheckpoint(label.c_str());
   const std::string before = doc->DocText();
   int last = 0;
   json ids = json::array();
   for (size_t i = 0; i < calls.size(); i++)
      if (!V2Apply(doc, calls[i], last, ids))
      {
         doc->SetDocText(before);
         doc->Commit();
         err.Add(i, "E_APPLY", "the call passed the checks but could not be applied; nothing changed");
         out = { { "ok", false }, { "errors", err.list } };
         return true;
      }
   doc->Commit();
   sSel.erase(std::remove_if(sSel.begin(), sSel.end(), [&](int id) { return doc->FindLayer(id) == nullptr; }), sSel.end());
   Touch();
   out = { { "ok", true }, { "ids", ids }, { "undo", label } };
   return true;
}

// 3D nodes land as 3D cards.
static bool Is3DNode(INode* n)
{
   return dynamic_cast<IGeometrySource*>(n) != nullptr || dynamic_cast<Render3DNode*>(n) != nullptr ||
          dynamic_cast<Scene3DNode*>(n) != nullptr;
}

// INFINITE_VIEW2DTEST: the panel's own actions on a real document (the Effects menu, the 3D gizmo). Returns failures.
int View2DPanelSelfCheck(Scene2DNode* doc)
{
   int fails = 0;
   auto expect = [&](const char* what, bool pass) {
      printf("  [%s] panel: %s\n", pass ? "pass" : "FAIL", what);
      fails += pass ? 0 : 1;
   };
   if (doc == nullptr || doc->Layers().empty())
   {
      expect("a document to test on", false);
      return fails;
   }
   // Effects menu: each pick adds that adjustment, not always the first (Pixelate).
   for (int type : { (int)Scene2DNode::kAdjThreshold, (int)Scene2DNode::kAdjPosterize, (int)Scene2DNode::kAdjHalftone })
   {
      sPending.what = Pending::AddAdjust;
      sPending.arg = type;
      RunPending(doc);
      const Layer* l = Active(doc);
      expect("an Effects pick adds that adjustment", l != nullptr && l->kind == Kind::Adjust && l->adjType == type);
   }

   // Pixels of the composite, for "the render changed".
   auto picture = [&](Scene2DNode* d) {
      static int frame = 7000000;
      d->CookIfNeeded(++frame);
      std::vector<uint8_t> px;
      int w = 0, h = 0;
      d->ReadOutput(px, w, h);
      return px;
   };
   const float W = (float)doc->Doc().w, H = (float)doc->Doc().h;
   auto stroke = [&](int id, float x0, float y0, float x1, float y1) {
      BrushEngine::Brush b;
      b.size = 30.0f;
      b.hardness = 1.0f;
      b.color[0] = 1.0f;
      if (doc->BeginPaint(id, 0, b, x0, y0, 1.0f))
      {
         doc->PaintTo(x1, y1, 1.0f);
         doc->EndPaint();
      }
   };
   // Layer marquee: touches only layers whose paint it covers; Shift adds to what is picked.
   {
      const int a = doc->AddPaintLayer();
      stroke(a, W * 0.08f, H * 0.12f, W * 0.1f, H * 0.12f);
      const int b = doc->AddPaintLayer();
      stroke(b, W * 0.85f, H * 0.85f, W * 0.88f, H * 0.85f);
      doc->Commit();
      sSel = { b };
      MarqueeLayers(doc, ImVec2(W * 0.05f, H * 0.08f), ImVec2(W * 0.14f, H * 0.16f), false);
      expect("marquee picks the layer it covers, not one painted elsewhere", Selected(a) && !Selected(b));
      sSel = { b };
      MarqueeLayers(doc, ImVec2(W * 0.05f, H * 0.08f), ImVec2(W * 0.14f, H * 0.16f), true);
      expect("Shift marquee adds to the selection", Selected(a) && Selected(b));
      MarqueeLayers(doc, ImVec2(W * 0.45f, H * 0.45f), ImVec2(W * 0.46f, H * 0.46f), false);
      expect("marquee over empty paint picks neither", !Selected(a) && !Selected(b));
      // Pixel-accurate hit: the top layer is b, painted bottom right; a click on a's paint is a, not b's empty box.
      expect("a click on paint picks that layer", HitLayer(doc, ImVec2(W * 0.09f, H * 0.12f)) == a);
      const int empty = HitLayer(doc, ImVec2(W * 0.5f, H * 0.5f));
      expect("a click on empty paint picks neither paint layer", empty != a && empty != b);
   }
   // Pixel-accurate hit on a 3D object: the box corner misses the sphere, the centre hits it.
   {
      GraphNode* sph = SpawnNode("Sphere", "3D", 0.0f, 0.0f);
      if (sph != nullptr)
      {
         sPending.what = Pending::AddNode;
         sPending.uid = sph->uid;
         RunPending(doc);
         const Layer* sl = Active(doc);
         const int sid = sl != nullptr ? sl->id : 0;
         static int frame = 9500;
         doc->CookIfNeeded(++frame);
         float c[8] = {};
         if (sl != nullptr)
            doc->Corners(*sl, c);
         const float cx = (c[0] + c[2] + c[4] + c[6]) * 0.25f, cy = (c[1] + c[3] + c[5] + c[7]) * 0.25f;
         const ImVec2 corner(c[0] + (cx - c[0]) * 0.04f, c[1] + (cy - c[1]) * 0.04f);
         expect("a click on the object picks it", sid != 0 && HitLayer(doc, ImVec2(cx, cy)) == sid);
         expect("a click beside the object, inside its box, does not", sid != 0 && HitLayer(doc, corner) != sid);
      }
   }
   // Effects tab: switching a layer style on changes the picture.
   {
      const int a = doc->AddPaintLayer();
      stroke(a, W * 0.3f, H * 0.5f, W * 0.4f, H * 0.5f);
      doc->Commit();
      const std::vector<uint8_t> before = picture(doc);
      Layer* l = doc->FindLayer(a);
      Scene2DNode::Fx& f = l->fx[Scene2DNode::kFxStroke];
      f.on = true;
      f.size = 12.0f;
      f.c[0] = 0.0f; f.c[1] = 1.0f; f.c[2] = 0.0f; f.c[3] = 1.0f;
      doc->Commit();
      const std::vector<uint8_t> on = picture(doc);
      expect("a layer style switched on changes the render", !before.empty() && on != before);
      doc->FindLayer(a)->fx[Scene2DNode::kFxStroke].on = false;
      doc->Commit();
      expect("switched off, the render is back", picture(doc) == before);
   }
   // Colour tab: a button adds that adjustment right above the active layer, in its group.
   {
      const int a = doc->GroupLayers({ doc->AddPaintLayer() });
      const int inner = doc->ChildrenOf(a).empty() ? 0 : doc->ChildrenOf(a)[0];
      doc->AddPaintLayer();   // something above, so "on top" is not the same as "above the active"
      doc->Commit();
      SelectOnly(inner);
      auto indexOf = [&](int id) {
         for (size_t i = 0; i < doc->Layers().size(); i++)
            if (doc->Layers()[i].id == id)
               return (int)i;
         return -1;
      };
      const int at = indexOf(inner);
      sPending.what = Pending::AddAdjust;
      sPending.arg = Scene2DNode::kAdjPosterize;
      RunPending(doc);
      const Layer* adj = Active(doc);
      expect("a Colour button adds that adjustment", adj != nullptr && adj->kind == Kind::Adjust && adj->adjType == Scene2DNode::kAdjPosterize);
      expect("it lands right above the active layer, in its group",
             adj != nullptr && indexOf(adj->id) == at + 1 && adj->parent == doc->FindLayer(inner)->parent);
   }
   // Tool x layer: brush, eraser, fill and gradient paint, undo and redo; a hidden, a pixel-locked layer and the
   // area outside the selection take nothing.
   {
      const int tools[] = { kBrush, kEraser, kFill, kGradient };
      const char* names[] = { "brush", "eraser", "fill", "gradient" };
      const int keepTool = sTool;
      for (int ti = 0; ti < 4; ti++)
      {
         doc = FindDoc() != nullptr ? FindDoc() : doc;
         const int p = doc->AddPaintLayer();
         stroke(p, W * 0.2f, H * 0.3f, W * 0.6f, H * 0.3f);
         doc->Commit();
         doc->Deselect();
         sTool = tools[ti];
         sFg[0] = 0.0f; sFg[1] = 0.0f; sFg[2] = 1.0f;
         auto use = [&]() {
            const ImVec2 a(W * 0.3f, H * 0.3f), b(W * 0.5f, H * 0.32f);
            if (sTool == kFill)
               FillAt(doc, a);
            else if (sTool == kGradient)
               GradientTo(doc, a, b);
            else
            {
               BeginStroke(doc, a, false);
               if (doc->Painting())
                  doc->PaintTo(b.x, b.y, 1.0f);
               EndStroke(doc);
            }
            doc->Commit();
         };
         char what[160];
         SelectOnly(p);
         const std::string t0 = doc->DocText();
         const size_t undo0 = gUndoStack.size();
         use();
         const std::string t1 = doc->DocText();
         snprintf(what, sizeof what, "%s changes the paint layer", names[ti]);
         expect(what, t1 != t0);
         snprintf(what, sizeof what, "%s pushes one undo step", names[ti]);
         expect(what, gUndoStack.size() == undo0 + 1);
         Undo();
         doc = FindDoc();
         snprintf(what, sizeof what, "%s: undo restores the layer", names[ti]);
         expect(what, doc != nullptr && doc->DocText() == t0);
         Redo();
         doc = FindDoc();
         snprintf(what, sizeof what, "%s: redo puts it back", names[ti]);
         expect(what, doc != nullptr && doc->DocText() == t1);
         if (doc == nullptr)
            return fails;
         const size_t n = doc->Layers().size();
         auto refuses = [&](const char* how) {
            const std::string b0 = doc->DocText();
            const size_t u0 = gUndoStack.size();
            use();
            snprintf(what, sizeof what, "%s on a %s layer changes nothing", names[ti], how);
            expect(what, doc->DocText() == b0 && doc->Layers().size() == n);
            snprintf(what, sizeof what, "%s on a %s layer pushes no undo step", names[ti], how);
            expect(what, gUndoStack.size() == u0);
         };
         SelectOnly(p);
         doc->FindLayer(p)->lock = Scene2DNode::kLockPixels;
         doc->Commit();
         refuses("pixel-locked");
         doc->FindLayer(p)->lock = 0;
         doc->FindLayer(p)->visible = false;
         doc->Commit();
         refuses("hidden");
         doc->FindLayer(p)->visible = true;
         doc->Commit();
         // Outside the selection: select a far corner, paint in the middle.
         doc->SelectRect(W * 0.9f, H * 0.9f, W * 0.95f, H * 0.95f, Scene2DNode::kSelNew, 0.0f, false);
         {
            const std::string b0 = doc->DocText();
            const std::vector<uint8_t> pic0 = picture(doc);
            use();
            snprintf(what, sizeof what, "%s outside the selection leaves the picture alone", names[ti]);
            expect(what, picture(doc) == pic0);
            (void)b0;
         }
         doc->Deselect();
      }
      sTool = keepTool;
   }
   // Node layers: From canvas adds, Replace swaps the source and keeps the pose.
   {
      GraphNode* na = SpawnNode("Image Source", "Source", 0.0f, 0.0f);
      GraphNode* nb = SpawnNode("Image Source", "Source", 0.0f, 0.0f);
      expect("two source nodes to use", na != nullptr && nb != nullptr);
      if (na != nullptr && nb != nullptr)
      {
         const size_t n0 = doc->Layers().size();
         sPending.what = Pending::AddNode;
         sPending.uid = na->uid;
         RunPending(doc);
         Layer* l = Active(doc);
         expect("From canvas adds the node as a layer",
                l != nullptr && l->kind == Kind::Node && l->uid == na->uid && doc->Layers().size() == n0 + 1);
         if (l != nullptr)
         {
            const int id = l->id;
            l->x = 111.0f; l->rx = 20.0f; l->opacity = 0.5f; l->blend = 3; l->is3d = true; l->z = 40.0f;
            doc->Commit();
            sSel = { id };
            sPending.what = Pending::ReplaceNode;
            sPending.uid = nb->uid;
            RunPending(doc);
            const Layer* r = doc->FindLayer(id);
            expect("Replace swaps the source", r != nullptr && r->uid == nb->uid);
            expect("Replace keeps transform, 3D pose, blend and opacity",
                   r != nullptr && r->x == 111.0f && r->rx == 20.0f && r->opacity == 0.5f && r->blend == 3 && r->is3d && r->z == 40.0f);
            expect("Replace keeps the layer count", doc->Layers().size() == n0 + 1);
         }
      }
   }
   // File drops: a picture, an SVG and a model each land as a layer at the drop point; a video makes its node.
   {
      namespace fs = std::filesystem;
      const fs::path dir = fs::temp_directory_path() / "inf_v2d_drop";
      fs::create_directories(dir);
      static const unsigned char kPng[] = {
         0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01,
         0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
         0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00,
         0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
      auto put = [&](const char* name, const std::string& bytes) {
         const fs::path f = dir / name;
         std::ofstream(f, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());
         return f.string();
      };
      const std::string png = put("d.png", std::string((const char*)kPng, sizeof(kPng)));
      const std::string svg = put("d.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\"><rect x=\"0\" y=\"0\" width=\"40\" height=\"30\" fill=\"#ff0000\"/></svg>");
      const std::string obj = put("d.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
      const std::string mp4 = put("d.mp4", "not a movie");
      const ImVec2 at(300.0f, 200.0f);
      size_t n = doc->Layers().size();
      expect("a dropped picture is one layer", DropFiles(doc, { png }, at) == 1 && doc->Layers().size() == n + 1);
      const Layer* pl = Active(doc);
      expect("it sits at the drop point and is not 3D", pl != nullptr && pl->kind == Kind::Node && pl->x == at.x && pl->y == at.y && !pl->is3d);
      n = doc->Layers().size();
      expect("a dropped SVG is one vector layer", DropFiles(doc, { svg }, at) == 1 && doc->Layers().size() == n + 1 && Active(doc)->kind == Kind::Vector);
      n = doc->Layers().size();
      const int addedModel = DropFiles(doc, { obj }, at);
      expect("a dropped model is one 3D layer", addedModel == 1 && doc->Layers().size() == n + 1 && Active(doc)->is3d);
      size_t nodes = gNodes.size();
      DropFiles(doc, { mp4 }, at);
      expect("a dropped video spawns a Video node", gNodes.size() == nodes + 1 && gNodes.back().typeName == "Video");
      n = doc->Layers().size();
      expect("an audio file adds nothing", DropFiles(doc, { put("d.wav", "RIFF") }, at) == 0 && doc->Layers().size() == n);
      fs::remove_all(dir);
   }
   // 3D surface paint: a stroke on a sphere lands by UV, erases, survives a tilt and is saved with the patch.
   {
      GraphNode* sph = SpawnNode("Sphere", "3D", 0.0f, 0.0f);
      expect("a 3D object to paint on", sph != nullptr);
      if (sph != nullptr)
      {
         sPending.what = Pending::AddNode;
         sPending.uid = sph->uid;
         RunPending(doc);
         Layer* sl = Active(doc);
         const int sid = sl != nullptr ? sl->id : 0;
         int frame = 9000;
         doc->CookIfNeeded(++frame);
         expect("an object layer can take surface paint", sid != 0 && doc->CanSurfacePaint(sid));
         Scene2DNode::SurfacePoint sp;
         const float cx = sl != nullptr ? sl->x : 0.0f, cy = sl != nullptr ? sl->y : 0.0f;
         const bool hit = sid != 0 && doc->SurfaceAt(sid, cx, cy, sp);
         expect("the ray through the layer centre hits the object", hit && sp.facing > 0.5f);
         if (hit)
         {
            BrushEngine::Brush b;
            b.size = 40.0f;
            b.hardness = 1.0f;
            b.color[0] = 1.0f;
            b.color[1] = b.color[2] = 0.0f;
            expect("a surface stroke begins", doc->BeginSurfacePaint(sid, b, cx, cy, 1.0f, 512));
            doc->SurfacePaintTo(cx + 6.0f, cy, 1.0f);
            doc->EndPaint();
            const Layer* now = doc->FindLayer(sid);
            expect("the stroke made a skin", now != nullptr && now->HasSkin());
            float c[4] = { 0, 0, 0, 0 };
            expect("the painted texel at the picked UV is red", doc->SkinAt(sid, sp.uv[0], sp.uv[1], c) && c[3] > 0.5f && c[0] > 0.8f && c[1] < 0.2f);
            doc->CookIfNeeded(++frame);
            float shown[3] = { 0, 0, 0 };
            expect("the composite shows the paint on the object", doc->Pick(cx, cy, shown) && shown[0] > shown[1] + 0.2f);
            expect("the eyedropper reads the paint", doc->SurfaceColor(sid, cx, cy, c) && c[0] > 0.8f);
            const std::string saved = doc->DocText();
            expect("the skin is saved with the patch", saved.find("paint " + std::to_string(sid) + " 512 512") != std::string::npos);
            // Tilt the object card: the same UV is now under a different document point, still painted.
            Layer* tl = doc->FindLayer(sid);
            tl->is3d = true;
            tl->ry = 35.0f;
            doc->Commit();
            doc->CookIfNeeded(++frame);
            // A real object turns about its centre, so the spot travels far: search the whole object for the nearest UV.
            auto nearestUv = [&](float& bx, float& by) {
               float best = 1e9f;
               auto scan = [&](float ox, float oy, float r, float step) {
                  for (float dx = -r; dx <= r; dx += step)
                     for (float dy = -r; dy <= r; dy += step)
                     {
                        Scene2DNode::SurfacePoint q;
                        if (!doc->SurfaceAt(sid, ox + dx, oy + dy, q) || q.facing <= 0.0f)
                           continue;
                        float du = std::fabs(q.uv[0] - sp.uv[0]);
                        du = std::min(du, 1.0f - du);
                        const float d = du + std::fabs(q.uv[1] - sp.uv[1]);
                        if (d < best)
                        {
                           best = d;
                           bx = ox + dx;
                           by = oy + dy;
                        }
                     }
               };
               scan(cx, cy, 420.0f, 12.0f);
               scan(bx, by, 12.0f, 1.5f);
               return best;
            };
            float ex = cx, ey = cy;
            const float near = nearestUv(ex, ey);
            bool found = false;
            {
               Scene2DNode::SurfacePoint q;
               float k[4] = { 0, 0, 0, 0 };
               found = near < 0.02f && doc->SurfaceAt(sid, ex, ey, q) && doc->SkinAt(sid, q.uv[0], q.uv[1], k) && k[3] > 0.5f && k[0] > 0.8f;
               printf("  surface tilt: spot moved %.0f, %.0f px (uv off %.4f)\n", ex - cx, ey - cy, near);
            }
            expect("after a tilt the paint is still on the object", found);
            // Erase at the original spot of the surface.
            const bool eh = near < 0.02f;
            b.mode = BrushEngine::kErase;
            b.size = 80.0f;
            const bool eb = eh && doc->BeginSurfacePaint(sid, b, ex, ey, 1.0f, 512);
            doc->EndPaint();
            expect("the eraser takes the paint off", eb && doc->SkinAt(sid, sp.uv[0], sp.uv[1], c) && c[3] < 0.2f);
            doc->ClearSkin(sid);
            expect("Clear removes the skin paint", doc->SkinAt(sid, sp.uv[0], sp.uv[1], c) && c[3] < 0.01f);
         }
      }
   }
   // 3D gizmo: each handle changes only what it should.
   int id = 0;
   for (const Layer& l : doc->Layers())
      if (l.kind == Kind::Node)
         id = l.id;
   Layer* l = doc->FindLayer(id);
   expect("a node layer for the gizmo", l != nullptr);
   if (l == nullptr)
      return fails;
   l->is3d = true;
   doc->Commit();
   SelectOnly(id);
   const Layer base = *l;
   const float at[3] = { base.x, base.y, base.z };
   ImVec2 c;
   const bool seen = GizmoScreen(doc, at, c);
   expect("the gizmo centre is on screen", seen);
   if (!seen)
      return fails;
   auto drag = [&](int mode, int axis, ImVec2 from, ImVec2 to) {
      *doc->FindLayer(id) = base;
      sGizmoMode = mode;
      BeginOp(doc, 16, from, nullptr);
      sOp.gizmoAxis = axis;
      DragGizmo3D(doc, to);
      sOp = Op();
      return *doc->FindLayer(id);
   };
   Layer r = drag(0, 3, c, ImVec2(c.x + 40.0f, c.y));
   expect("move centre: x changes, z stays", std::fabs(r.x - base.x) > 1e-3f && r.z == base.z);
   float ax[3][3];
   GizmoAxes(doc, base, 0, ax);
   const float tip[3] = { at[0] + ax[0][0] * 100.0f, at[1] + ax[0][1] * 100.0f, at[2] + ax[0][2] * 100.0f };
   ImVec2 tx;
   if (GizmoScreen(doc, tip, tx))
   {
      r = drag(0, 0, c, ImVec2(c.x + (tx.x - c.x) * 0.5f, c.y + (tx.y - c.y) * 0.5f));
      expect("move X: only x changes", std::fabs(r.x - base.x) > 1e-3f && std::fabs(r.y - base.y) < 1e-3f && std::fabs(r.z - base.z) < 1e-3f);
   }
   r = drag(1, 2, ImVec2(c.x + 50.0f, c.y), ImVec2(c.x, c.y + 50.0f));
   expect("rotate Z: only the angle changes", std::fabs(r.angle - base.angle) > 1.0f && r.rx == base.rx && r.ry == base.ry);
   r = drag(1, 0, ImVec2(c.x + 50.0f, c.y), ImVec2(c.x, c.y + 50.0f));
   expect("rotate X: only rx changes", std::fabs(r.rx - base.rx) > 1.0f && r.angle == base.angle);
   r = drag(2, 3, ImVec2(c.x + 40.0f, c.y), ImVec2(c.x + 80.0f, c.y));
   expect("scale centre: uniform x2", std::fabs(r.sx - base.sx * 2.0f) < 1e-3f && std::fabs(r.sy - base.sy * 2.0f) < 1e-3f);
   // On a real 3D object the gizmo sits on the object and its depth arm scales the object's depth.
   if (doc->IsObjectLayer(base))
   {
      *doc->FindLayer(id) = base;
      doc->Commit();
      float gx = 0, gy = 0;
      Scene2DNode::SurfacePoint q;
      expect("gizmo: the centre sits on the object", doc->ProjectPoint(at, gx, gy) && doc->SurfaceAt(id, gx, gy, q));
      GizmoAxes(doc, base, 2, ax);
      const float zt[3] = { at[0] + ax[2][0] * 100.0f, at[1] + ax[2][1] * 100.0f, at[2] + ax[2][2] * 100.0f };
      ImVec2 zs;
      if (GizmoScreen(doc, zt, zs))
      {
         const bool longArm = std::hypot(zs.x - c.x, zs.y - c.y) >= 10.0f;
         r = drag(2, 2, c, longArm ? ImVec2(c.x + (zs.x - c.x) * 0.5f, c.y + (zs.y - c.y) * 0.5f) : ImVec2(c.x, c.y - 30.0f));
         expect("gizmo: scale Z on an object changes only its depth", std::fabs(r.sz - base.sz) > 1e-3f && r.sx == base.sx && r.sy == base.sy);
      }
      // A turn about the object's own axis keeps the gizmo centre on the object.
      Layer* lt = doc->FindLayer(id);
      lt->ry = base.ry + 60.0f;
      doc->Commit();
      const float at2[3] = { lt->x, lt->y, lt->z };
      expect("gizmo: after a turn the centre is still on the object", doc->ProjectPoint(at2, gx, gy) && doc->SurfaceAt(id, gx, gy, q));
   }
   *doc->FindLayer(id) = base;
   sGizmoMode = 0;
   doc->Commit();

   // H6 sound as a paint input: the brush's size is a param of the open document, so a modulator (an Audio Analyze
   // level, an LFO, an expression) sets it, and a stroke already under way follows it. A spectrum is a mask source.
   {
      GraphNode* dg = nullptr;
      for (GraphNode& g : gNodes)
         if (g.node.get() == doc)
            dg = &g;
      const uint64_t keepDoc = sDocUid;
      sDocUid = dg != nullptr ? dg->uid : 0;
      const BrushEngine::Brush keep = sBrush;
      Modulation::Instance().ClearFrameParams();
      if (dg != nullptr)
         RegisterScene2DParams(*dg);
      bool reg = false;
      for (const ParamRef& r : Modulation::Instance().FrameParams())
         reg = reg || (dg != nullptr && r.nodeIndex == dg->index && r.paramIndex == kBrushSizeParam && r.value == &sBrush.size);
      expect("H6: the brush size is a param of the open document", reg);
      sBrush.size = 4.0f;
      sBrush.hardness = 1.0f;
      sBrush.opacity = sBrush.flow = 1.0f;
      sBrush.color[0] = 1.0f; sBrush.color[1] = sBrush.color[2] = 0.0f;
      const int pl = doc->AddPaintLayer();
      doc->Commit();
      const float W2 = (float)doc->Doc().w, H2 = (float)doc->Doc().h;
      // A thin stroke starts; then an expression drives the size to 120 and the rest of the stroke is wide.
      bool began = doc->BeginPaint(pl, 0, sBrush, W2 * 0.2f, H2 * 0.5f, 1.0f);
      doc->PaintTo(W2 * 0.3f, H2 * 0.5f, 1.0f);
      if (dg != nullptr)
         Modulation::Instance().SetExpression(dg->index, kBrushSizeParam, "120");
      ApplyModulationAndPalette(7100000);
      const float driven = sBrush.size;
      doc->PaintFollow(sBrush);
      doc->PaintTo(W2 * 0.8f, H2 * 0.5f, 1.0f);
      doc->EndPaint();
      doc->Commit();
      std::vector<uint8_t> px;
      int pw = 0, ph = 0;
      static int frame = 7200000;
      doc->soloLayer = pl;
      doc->CookIfNeeded(++frame);
      doc->ReadOutput(px, pw, ph);
      doc->soloLayer = 0;
      auto red = [&](float fx, float fy) {
         const int x = (int)(fx * pw), y = (int)(fy * ph);
         const size_t at = ((size_t)y * pw + x) * 4;
         return at + 3 < px.size() && px[at] > 200 && px[at + 1] < 60;
      };
      printf("  H6 brush size driven to %.0f; thin part 40 px off the line %s, wide part %s\n", driven,
             red(0.25f, 0.5f + 40.0f / H2) ? "painted" : "clear", red(0.65f, 0.5f + 40.0f / H2) ? "painted" : "clear");
      expect("H6: a modulator drives the brush size", began && std::fabs(driven - 120.0f) < 0.5f);
      expect("H6: the stroke follows the driven size", red(0.65f, 0.5f) && red(0.65f, 0.5f + 40.0f / H2) && !red(0.25f, 0.5f + 40.0f / H2));
      if (dg != nullptr)
         Modulation::Instance().ClearExpression(dg->index, kBrushSizeParam);
      Modulation::Instance().ClearFrameParams();
      doc->RemoveLayer(pl);
      doc->Commit();
      sBrush = keep;
      sDocUid = keepDoc;
   }
   // H5: an agent edits the document over RPC through the gate: one batch, one undo step; a bad call changes nothing.
   {
      using nlohmann::json;
      json out;
      std::string err;
      const size_t n0 = doc->Layers().size(), u0 = gUndoStack.size();
      View2DRpc({ { "op", "apply" }, { "label", "test" }, { "calls", json::array({
                    { { "op", "add" }, { "kind", "fill" } },
                    { { "op", "set" }, { "layer", -1 }, { "values", { { "Opacity", 0.5 }, { "name", "agent fill" }, { "color", { 1, 0, 0 } } } } } }) } },
                out, err);
      doc = FindDoc();
      const Layer* made = out.value("ok", false) && out["ids"].size() == 1 ? doc->FindLayer(out["ids"][0].get<int>()) : nullptr;
      expect("H5: an RPC batch adds and sets a layer", made != nullptr && made->opacity == 0.5f && made->name == "agent fill" && made->colorA[0] == 1.0f);
      expect("H5: the batch is one undo step", gUndoStack.size() == u0 + 1);
      Undo();
      doc = FindDoc();
      expect("H5: one undo removes it", doc != nullptr && doc->Layers().size() == n0);
      if (doc == nullptr)
         return fails;
      const std::string t0 = doc->DocText();
      const size_t u1 = gUndoStack.size();
      out = json();
      View2DRpc({ { "op", "apply" }, { "calls", json::array({ { { "op", "add" }, { "kind", "fill" } },
                                                              { { "op", "set" }, { "layer", -1 }, { "values", { { "opacty", 0.5 } } } } }) } },
                out, err);
      const bool typed = !out.value("ok", true) && out["errors"].size() == 1 && out["errors"][0]["code"] == "E_FIELD" &&
                         out["errors"][0]["near"].size() > 0 && out["errors"][0]["near"][0] == "Opacity";
      expect("H5: a misspelt field fails the batch with the closest name", typed);
      expect("H5: a failed batch changes nothing", doc->DocText() == t0 && gUndoStack.size() == u1);
      out = json();
      View2DRpc({ { "op", "layers" } }, out, err);
      expect("H5: layers lists the document", out.value("doc", false) && out["layers"].size() == doc->Layers().size());
   }
   // H7 one source, many outputs: the still export and the Output node (which also feeds the loop render and a live
   // show) read the document's own frame; the written PNG holds exactly the live pixels.
   if (OutputNode* out = EnsureOutputImpl())
   {
      static int h7 = 7300000;
      doc->CookIfNeeded(++h7);
      out->CookIfNeeded(h7);
      std::vector<uint8_t> live, kept;
      int lw = 0, lh = 0;
      doc->ReadOutput(live, lw, lh);
      const std::string png = (std::filesystem::temp_directory_path() / "infinite_h7.png").string();
      ExportImage(out, png, 90, &kept);
      expect("H7: the Output node is the document's size", out->GetOutputWidth() == lw && out->GetOutputHeight() == lh && lw > 0);
      int worst = 0;
      if (kept.size() == live.size())
         for (size_t i = 0; i < live.size(); i++)
            worst = std::max(worst, std::abs((int)kept[i] - (int)live[i]));
      else
         worst = 999;
      std::vector<unsigned char> bytes;
      if (std::ifstream f{ png, std::ios::binary })
         bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
      int fw = 0, fh = 0, ch = 0, fileWorst = 999;
      if (unsigned char* img = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &fw, &fh, &ch, 4))
      {
         if (fw == lw && fh == lh)
         {
            fileWorst = 0;
            for (int y = 0; y < fh; y++)   // the file is top row first, the read-back bottom row first
               for (int x = 0; x < fw * 4; x++)
                  fileWorst = std::max(fileWorst, std::abs((int)img[(size_t)y * fw * 4 + x] - (int)live[(size_t)(fh - 1 - y) * fw * 4 + x]));
         }
         stbi_image_free(img);
      }
      std::error_code ec;
      std::filesystem::remove(png, ec);
      printf("  H7 live vs export: Output node worst %d, PNG file worst %d (of 255)\n", worst, fileWorst);
      expect("H7: the export frame is the live frame", worst <= 1);
      expect("H7: the written PNG is the live frame", fileWorst <= 1);
   }
   // H6 Audio Texture as a layer source: the 2D View takes it, and a tone on FFT bin 64 lights its spectrum column.
   {
      AudioTextureNode at;
      at.mode = AudioTextureNode::kModeSpectrum;
      at.GetAudioInput().Connect(&at);   // any source: the picture is read from what the sink was handed
      auto spectrumAt = [&](float cyclesPerSample, int frame) {
         std::vector<float> in(1024), outL(1024), outR(1024), px;
         for (int i = 0; i < 1024; i++)
            in[i] = cyclesPerSample > 0 ? 0.8f * std::sin(6.28318530718f * cyclesPerSample * (float)i) : 0.0f;
         float* inCh[1] = { in.data() };
         float* outCh[2] = { outL.data(), outR.data() };
         AudioBuffer ib{ inCh, 1, 1024 }, ob{ outCh, 2, 1024 };
         const AudioBuffer* ins[1] = { &ib };
         at.GetAudioNode()->ProcessBlock(ins, 1, ob);
         at.CookIfNeeded(frame);
         unsigned scratch = 0;
         const int w = at.GetOutputWidth(), h = at.GetOutputHeight();
         if (at.GetOutputTexture() == 0 || !GLUtil::ReadTexturePixels(scratch, at.GetOutputTexture(), w, h, px))
            px.clear();
         if (scratch != 0)
            glDeleteFramebuffers(1, &scratch);
         return std::make_pair(px, w);
      };
      const auto quiet = spectrumAt(0.0f, 1);
      const auto tone = spectrumAt(64.0f / 1024.0f, 2);
      auto col = [](const std::pair<std::vector<float>, int>& p, int x) {
         const int w = p.second;
         const size_t i = ((size_t)(w / 2) * w + x) * 4;
         return w > 0 && p.first.size() > i ? p.first[i] : -1.0f;
      };
      int peak = -1;
      float best = -1.0f;
      for (int x = 1; x < tone.second; x++)
         if (col(tone, x) > best)
         {
            best = col(tone, x);
            peak = x;
         }
      // Columns run on log frequency, 20 Hz..20 kHz: bin 64 (2756 Hz at 44.1 kHz) sits at column ~364.
      const int want = (int)std::lround(std::log((64.0f * 44100.0f / 1024.0f) / 20.0f) / std::log(1000.0f) * 511.0f);
      printf("  H6 audio texture: column %d quiet %.2f tone %.2f, brightest column %d\n", want, col(quiet, want), col(tone, want), peak);
      bool gate = false;
      {
         GraphNode probe;
         probe.node.reset(new AudioTextureNode());
         gate = View2DCanAdd(probe);
      }
      expect("H6: the 2D View takes an Audio Texture as a layer", gate);
      {
         GraphNode probe;   // H2: the one Prediction node with a picture is a layer (and so a mask) source
         probe.node.reset(new PredictiveColoringNode());
         expect("H2: the 2D View takes Predictive Coloring as a layer", View2DCanAdd(probe));
      }
      expect("H6: a tone lights its known spectrum pixel", col(tone, want) > col(quiet, want) + 0.3f && std::abs(peak - want) <= 1);
   }
   // H8 a layer as an output: a hidden red fill marked output reaches a cable on output 1, alone, and not the comp.
   {
      json out;
      std::string err;
      View2DRpc({ { "op", "apply" }, { "calls", json::array({ { { "op", "add" }, { "kind", "fill" } },
                                                              { { "op", "set" }, { "layer", -1 }, { "values", { { "name", "h8" }, { "color", { 1, 0, 0, 1 } }, { "visible", false } } } } }) } },
                out, err);
      doc = FindDoc();
      Layer* h8 = doc != nullptr && out.value("ok", false) ? doc->FindLayer(out["ids"][0].get<int>()) : nullptr;
      if (h8 != nullptr)
      {
         h8->output = true;
         doc->Commit();
         const bool saved = doc->DocText().find("out " + std::to_string(h8->id) + "\n") != std::string::npos;
         ImageCable cable;
         cable.Connect(doc, 1);
         static int h8f = 7400000;
         const unsigned tex = cable.Pull(++h8f);
         std::vector<float> px, comp;
         unsigned scratch = 0;
         const int w = doc->GetOutputWidth(), h = doc->GetOutputHeight();
         const bool readA = tex != 0 && GLUtil::ReadTexturePixels(scratch, tex, w, h, px);
         const bool readB = GLUtil::ReadTexturePixels(scratch, doc->GetOutputTexture(), w, h, comp);
         if (scratch != 0)
            glDeleteFramebuffers(1, &scratch);
         const size_t mid = ((size_t)(h / 2) * w + w / 2) * 4;
         const bool red = readA && px.size() > mid + 3 && px[mid] > 0.9f && px[mid + 1] < 0.1f && px[mid + 3] > 0.9f;
         const bool notComp = readB && comp.size() > mid + 3 && !(comp[mid] > 0.9f && comp[mid + 1] < 0.1f);
         printf("  H8 outputs %d label '%s' layer px %.2f %.2f %.2f %.2f\n", doc->OutputCount(), doc->OutputLabel(1),
                red ? px[mid] : -1.0f, readA ? px[mid + 1] : -1.0f, readA ? px[mid + 2] : -1.0f, readA ? px[mid + 3] : -1.0f);
         expect("H8: a layer marked output adds a named output", doc->OutputCount() == 2 && std::string(doc->OutputLabel(1)) == "h8");
         expect("H8: the output flag is saved", saved);
         expect("H8: a cable on the layer output reads that layer alone", red);
         expect("H8: a hidden output layer stays out of the comp", notComp);
         doc->RemoveLayer(h8->id);
         doc->Commit();
         expect("H8: removing the layer removes the output", doc->OutputCount() == 1);
      }
      else
         expect("H8: setup", false);
   }
   // C: an effect node as a layer runs on the picture under it (Invert over red reads cyan), and only there.
   {
      json out;
      std::string err;
      GraphNode* inv = SpawnNode("invert", "Compositing", -4000.0f, -4000.0f);
      const int invIndex = inv != nullptr ? inv->index : -1;
      View2DRpc({ { "op", "apply" }, { "calls", json::array({ { { "op", "add" }, { "kind", "fill" } },
                                                              { { "op", "set" }, { "layer", -1 }, { "values", { { "color", { 1, 0, 0, 1 } } } } },
                                                              { { "op", "add" }, { "kind", "node" }, { "node", invIndex } },
                                                              { { "op", "set" }, { "layer", -1 }, { "values", { { "below", true } } } } }) } },
                out, err);
      Scene2DNode* cd = FindDoc();
      const bool ok = cd != nullptr && out.value("ok", false) && out["ids"].size() >= 2;
      const Layer* fx = ok ? cd->FindLayer(out["ids"][1].get<int>()) : nullptr;
      std::vector<float> px;
      if (fx != nullptr)
      {
         static int cf = 7500000;
         cd->CookIfNeeded(++cf);
         unsigned scratch = 0;
         GLUtil::ReadTexturePixels(scratch, cd->GetOutputTexture(), cd->GetOutputWidth(), cd->GetOutputHeight(), px);
         if (scratch != 0)
            glDeleteFramebuffers(1, &scratch);
      }
      const size_t mid = cd != nullptr ? ((size_t)(cd->GetOutputHeight() / 2) * cd->GetOutputWidth() + cd->GetOutputWidth() / 2) * 4 : 0;
      const bool cyan = px.size() > mid + 2 && px[mid] < 0.1f && px[mid + 1] > 0.9f && px[mid + 2] > 0.9f;
      printf("  C effect layer: below=%d centre %.2f %.2f %.2f\n", fx != nullptr && fx->below ? 1 : 0, cyan || px.size() > mid + 2 ? px[mid] : -1.0f,
             px.size() > mid + 2 ? px[mid + 1] : -1.0f, px.size() > mid + 2 ? px[mid + 2] : -1.0f);
      expect("C: an effect node layer runs on the picture under it", fx != nullptr && fx->below && cyan);
      const bool saved = fx != nullptr && cd->DocText().find("below " + std::to_string(fx->id) + "\n") != std::string::npos;
      expect("C: the effect flag is saved", saved);
      ImageCable* in = inv != nullptr ? CableFor(*inv, 0) : nullptr;
      expect("C: the node's own input is left as it was", in != nullptr && !in->IsConnected());
      if (ok)
      {
         for (int k = 1; k >= 0; k--)
            cd->RemoveLayer(out["ids"][k].get<int>());
         cd->Commit();
      }
      if (invIndex >= 0)
         RemoveNodeByIndex(invIndex);
   }
   // D: the new colour adjustments land on a known pixel (red under Invert reads cyan, under Saturation 0 reads grey).
   {
      auto centreUnder = [&](const char* type, float amt) {
         json out;
         std::string err;
         View2DRpc({ { "op", "apply" }, { "calls", json::array({ { { "op", "add" }, { "kind", "fill" } },
                                                                 { { "op", "set" }, { "layer", -1 }, { "values", { { "color", { 1, 0, 0, 1 } } } } },
                                                                 { { "op", "add" }, { "kind", "adjust" }, { "type", type } },
                                                                 { { "op", "set" }, { "layer", -1 }, { "values", { { "Amount", amt } } } } }) } },
                   out, err);
         std::vector<float> px;
         Scene2DNode* dd = FindDoc();
         if (dd == nullptr || !out.value("ok", false))
         {
            printf("  D %s: %s\n", type, err.c_str());
            return px;
         }
         static int df = 7600000;
         dd->CookIfNeeded(++df);
         unsigned scratch = 0;
         const int w = dd->GetOutputWidth(), h = dd->GetOutputHeight();
         std::vector<float> all;
         GLUtil::ReadTexturePixels(scratch, dd->GetOutputTexture(), w, h, all);
         if (scratch != 0)
            glDeleteFramebuffers(1, &scratch);
         const size_t mid = ((size_t)(h / 2) * w + w / 2) * 4;
         if (all.size() > mid + 2)
            px.assign(all.begin() + mid, all.begin() + mid + 3);
         for (int k = (int)out["ids"].size() - 1; k >= 0; k--)
            dd->RemoveLayer(out["ids"][k].get<int>());
         dd->Commit();
         return px;
      };
      const std::vector<float> inv = centreUnder("Invert", 1.0f), sat = centreUnder("Saturation", 0.0f), bri = centreUnder("Brightness", -1.0f);
      printf("  D adjust: invert %.2f %.2f %.2f  sat0 %.2f %.2f %.2f  bright-1 %.2f\n", inv.size() == 3 ? inv[0] : -1.0f, inv.size() == 3 ? inv[1] : -1.0f,
             inv.size() == 3 ? inv[2] : -1.0f, sat.size() == 3 ? sat[0] : -1.0f, sat.size() == 3 ? sat[1] : -1.0f, sat.size() == 3 ? sat[2] : -1.0f,
             bri.size() == 3 ? bri[0] : -1.0f);
      expect("D: Invert turns red cyan", inv.size() == 3 && inv[0] < 0.05f && inv[1] > 0.95f && inv[2] > 0.95f);
      expect("D: Saturation 0 turns red grey", sat.size() == 3 && std::fabs(sat[0] - sat[1]) < 0.02f && std::fabs(sat[1] - sat[2]) < 0.02f && sat[0] > 0.02f);
      expect("D: Brightness -1 turns red black", bri.size() == 3 && bri[0] < 0.02f);
   }
   // J: expand, contract, smooth and border change a 100 px square selection where they should.
   if (Scene2DNode* sd = FindDoc())
   {
      const int cx = sd->Doc().w / 2, cy = sd->Doc().h / 2;
      auto square = [&]() { sd->SelectRect((float)cx - 50, (float)cy - 50, (float)cx + 50, (float)cy + 50, Scene2DNode::kSelNew, 0.0f, false); };
      square();
      sd->ModifySelection(0, 10);
      const bool grew = sd->SelectionAt(cx - 56, cy) > 0.5f && sd->SelectionAt(cx - 64, cy) < 0.5f;
      square();
      sd->ModifySelection(1, 10);
      const bool shrank = sd->SelectionAt(cx - 44, cy) < 0.5f && sd->SelectionAt(cx - 36, cy) > 0.5f;
      square();
      sd->ModifySelection(3, 10);
      const bool ring = sd->SelectionAt(cx - 50, cy) > 0.5f && sd->SelectionAt(cx, cy) < 0.5f && sd->SelectionAt(cx - 70, cy) < 0.5f;
      square();
      sd->ModifySelection(2, 10);
      const bool round = sd->SelectionAt(cx, cy) > 0.5f && sd->SelectionAt(cx - 49, cy - 49) < 0.5f;
      sd->Deselect();
      expect("J: Expand grows the selection by its px", grew);
      expect("J: Contract shrinks it by its px", shrank);
      expect("J: Border is a ring on the edge", ring);
      expect("J: Smooth rounds the corners", round);
   }
   // L: Cmd-drag a corner distorts it alone, an edge skews both its corners, Alt+Shift on a corner mirrors (perspective).
   {
      json out;
      std::string err;
      View2DRpc({ { "op", "apply" }, { "calls", json::array({ { { "op", "add" }, { "kind", "text" } } }) } }, out, err);
      doc = FindDoc();
      Layer* lw = doc != nullptr && out.value("ok", false) ? doc->FindLayer(out["ids"][0].get<int>()) : nullptr;
      if (lw != nullptr)
      {
         sSel = { lw->id };
         auto drag = [&](int handle, bool persp, float dx, float dy) {
            Layer* l = doc->FindLayer(lw->id);
            l->warpOn = false;
            std::fill(l->warp, l->warp + 8, 0.0f);
            BeginOp(doc, 18, ImVec2(0, 0), nullptr);
            sOp.warpHandle = handle;
            sOp.perspective = persp;
            sOp.docStart = ImVec2(0, 0);
            WarpActive(doc, ToScreen(doc, dx, dy));
            sOp = Op();
            return *doc->FindLayer(lw->id);
         };
         const Layer a = drag(0, false, 30.0f, 20.0f);
         expect("L: a corner drag distorts that corner alone", a.warpOn && std::fabs(a.warp[0] - 30.0f) < 0.5f && std::fabs(a.warp[1] - 20.0f) < 0.5f
                && a.warp[2] == 0.0f && a.warp[4] == 0.0f && a.warp[6] == 0.0f);
         const Layer b = drag(1, false, 25.0f, 40.0f);   // top edge, axis-aligned: only the x part counts
         expect("L: an edge drag skews both its corners along it", std::fabs(b.warp[0] - 25.0f) < 0.5f && std::fabs(b.warp[2] - 25.0f) < 0.5f
                && std::fabs(b.warp[1]) < 0.5f && b.warp[4] == 0.0f);
         const Layer c = drag(0, true, 15.0f, 2.0f);
         expect("L: perspective mirrors the partner corner", std::fabs(c.warp[0] - 15.0f) < 0.5f && std::fabs(c.warp[2] + 15.0f) < 0.5f);
         doc->RemoveLayer(lw->id);
         doc->Commit();
      }
      else
         expect("L: setup", false);
   }
   return fails;
}

// Fonts added with Add font..., kept by path in the app's support folder and registered again at start.
static std::string UserFontList()
{
   return AppPaths::AppSupportDir() + "/fonts.txt";
}

void LoadUserFonts()
{
   std::ifstream in(UserFontList());
   std::string line, fam;
   while (std::getline(in, line))
      if (!line.empty())
         Platform::AddFontFile(line, fam);
}

bool AddUserFont(const std::string& path, std::string& family)
{
   if (!Platform::AddFontFile(path, family))
      return false;
   std::vector<std::string> have;
   {
      std::ifstream in(UserFontList());
      std::string line;
      while (std::getline(in, line))
         if (!line.empty())
            have.push_back(line);
   }
   if (std::find(have.begin(), have.end(), path) == have.end())
   {
      std::ofstream out(UserFontList(), std::ios::app);
      out << path << "\n";
   }
   return true;
}

#ifdef IMGUI_ENABLE_TEST_ENGINE
// UI audit (INFINITE_UIAUDIT_VIEW2D, docs/plans/ui-system/ui-audit.md): a document holding one layer of every kind,
// and the panel states the audit steps through (every side tab x every layer kind, every tool's bar, both docks).
namespace
{
   constexpr int kAuditKinds = 10;   // 9 layer kinds and "nothing selected"
   const char* kAuditKindNames[kAuditKinds] = { "paint", "node", "3D node", "text", "shape", "fill", "adjust", "group", "masked", "none" };
   const char* kAuditTabNames[7] = { "Layers", "Layer", "Transform", "Document", "Render", "Effects", "Colour" };
   const char* kAuditToolNames[kToolCount] = { "Move", "Crop", "Hand", "Zoom", "Brush", "Eraser", "Fill", "Pick", "Marquee", "Lasso", "Wand",
                                               "Pen", "Node", "Shape", "Text", "Gradient", "Clone", "Heal", "Blur", "Sharpen", "Dodge", "Burn" };
   uint64_t sAuditDoc = 0;
   int sAuditIds[kAuditKinds - 1] = {};
   int sAuditState = -1;

   Scene2DNode* AuditDoc()
   {
      Scene2DNode* doc = FindDoc();
      if (doc != nullptr && sAuditDoc == sDocUid && doc->FindLayer(sAuditIds[0]) != nullptr)
         return doc;
      if (doc == nullptr)
         doc = MakeDoc(0.0f, 0.0f, "");
      if (doc == nullptr)
         return nullptr;
      sAuditDoc = sDocUid;
      doc->Doc().w = 960;
      doc->Doc().h = 540;
      doc->Commit();
      int* id = sAuditIds;
      sSel.clear();
      id[0] = doc->AddPaintLayer();
      {
         BrushEngine::Brush b;
         b.size = 60.0f;
         b.color[0] = 0.9f;
         if (doc->BeginPaint(id[0], 0, b, 200.0f, 200.0f, 1.0f))
         {
            doc->PaintTo(400.0f, 260.0f, 1.0f);
            doc->EndPaint();
         }
      }
      const uint64_t noise = [] { GraphNode* g = SpawnNode("Noise", "Source", 0.0f, 0.0f); return g != nullptr ? g->uid : 0; }();
      const uint64_t sphere = [] { GraphNode* g = SpawnNode("Sphere", "3D", 0.0f, 0.0f); return g != nullptr ? g->uid : 0; }();
      sSel.clear();
      id[1] = noise != 0 ? AddNodeLayer(doc, noise, nullptr) : 0;
      sSel.clear();
      id[2] = sphere != 0 ? AddNodeLayer(doc, sphere, nullptr) : 0;
      id[3] = doc->AddTextLayer("Audit", 480.0f, 270.0f, 300.0f);
      id[4] = doc->AddVectorLayer();
      doc->ImportSvg(id[4], "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"120\"><rect x=\"0\" y=\"0\" width=\"200\" height=\"120\" fill=\"#3366ff\"/></svg>");
      id[5] = doc->AddLayer(Kind::Fill, 0).id;
      id[6] = doc->AddLayer(Kind::Adjust, 0).id;
      id[7] = doc->GroupLayers({ doc->AddPaintLayer() });
      id[8] = doc->AddPaintLayer();
      doc->AddPixelMask(id[8]);
      if (Layer* f = doc->FindLayer(id[5]))
         f->opacity = 0.3f;
      doc->Commit();
      sSel.clear();
      sNeedFit = true;
      return doc;
   }
}

int View2DAuditStateCount() { return 7 * kAuditKinds + kToolCount + 4; }

// Puts the panel into state i and returns its name ("Effects / text", "tool Brush", "dock top"); empty past the end.
std::string View2DAuditState(int i)
{
   gView2DOpen = true;
   Scene2DNode* doc = AuditDoc();
   if (doc == nullptr || i < 0 || i >= View2DAuditStateCount())
      return std::string();
   sAuditState = i;
   sSideOpen = true;
   sOp = Op();
   sRename = 0;
   auto select = [&](int k) {
      sSel.clear();
      if (k < kAuditKinds - 1 && doc->FindLayer(sAuditIds[k]) != nullptr)
         sSel = { sAuditIds[k] };
   };
   if (i < 7 * kAuditKinds)
   {
      gView2DDock = 0;
      sTool = kMove;
      sTab = i / kAuditKinds;
      select(i % kAuditKinds);
      sSizeFor = sDocUid;   // synced, so the panel keeps the audit's numbers
      sSizeSeenW = doc->Doc().w;
      sSizeSeenH = doc->Doc().h;
      sImageW = doc->Doc().w / 2;   // Image size's Apply is live only off the document's own size
      sImageH = doc->Doc().h / 2;
      if (doc->Doc().guidesX.empty())
         doc->Doc().guidesX = { 240.0f };   // so Clear Guides has something to clear
      return std::string(kAuditTabNames[sTab]) + " / " + kAuditKindNames[i % kAuditKinds];
   }
   i -= 7 * kAuditKinds;
   if (i < kToolCount)
   {
      gView2DDock = 0;
      sTab = 0;
      select(i == kText ? 3 : i == kPen || i == kNode || i == kShape ? 4 : 0);
      sTool = i;
      if (i == kHand)
      {
         sZoom = 0.37f;   // off the fit, so the bar's Fit has a view to change
         sPan = ImVec2(0.0f, 0.0f);
      }
      return std::string("tool ") + kAuditToolNames[i];
   }
   i -= kToolCount;
   sTool = kMove;
   if (i == 3)
   {
      // A crop in progress: the tool state's Apply ends the crop before Cancel's turn, so Cancel gets its own state.
      gView2DDock = 0;
      sTab = 0;
      select(0);
      sTool = kCrop;
      sCropOn = true;
      sCrop[0] = 100.0f; sCrop[1] = 80.0f; sCrop[2] = 700.0f; sCrop[3] = 420.0f;
      return "crop open";
   }
   if (i == 2)
   {
      // Three layers picked: align and distribute come alive.
      gView2DDock = 0;
      sTab = 0;
      sSel.clear();
      // Uneven spacing on both axes, so distribute has gaps to even out.
      static const float kAt[3][2] = { { 160.0f, 120.0f }, { 260.0f, 220.0f }, { 760.0f, 420.0f } };
      for (int k = 0; k < 3; k++)
         if (Layer* l = doc->FindLayer(sAuditIds[k]))
         {
            l->x = kAt[k][0];
            l->y = kAt[k][1];
            sSel.push_back(sAuditIds[k]);
         }
      doc->Commit();
      return "three picked";
   }
   sTab = 1;
   select(0);
   gView2DDock = i == 0 ? 3 : 0;
   return i == 0 ? "dock top" : "dock bottom";
}

// The panel's own state that is not saved with the patch (tool, tab, selection, view, tool settings): a click that
// changes only this still did something.
std::string View2DAuditPanelState()
{
   char buf[900];
   const BrushEngine::Brush& b = sBrush;
   const BrushEngine::Brush& e = sEraser;
   std::string sel;
   for (int id : sSel)
      sel += std::to_string(id) + ",";
   snprintf(buf, sizeof buf,
            "tool %d tab %d side %d dock %d zoom %.4f pan %.1f %.1f fg %.3f %.3f %.3f bg %.3f %.3f %.3f fill %d tol %d cont %d all %d "
            "marq %d lasso %d feather %.2f selop %d surf %d skin %d mask %d auto %d img %d vid %d gizmo %d pick %d "
            "brush %d %.2f %.3f %.3f %.3f %.3f %.3f %.1f %.3f %.3f %d %d %d %d %d %d eraser %.2f %.3f %d %d %d %d %.3f %.3f %.3f %.1f "
            "retouch %.2f %.3f %.3f %.3f %d %d chip %d dialogs %d",
            sTool, sTab, sSideOpen, gView2DDock, sZoom, sPan.x, sPan.y, sFg[0], sFg[1], sFg[2], sBg[0], sBg[1], sBg[2], sFillKind, sTolerance,
            sContiguous, sAllLayers, sMarqueeShape, sLassoKind, sFeather, sSelOp, sSurfacePaint, sSkinSide, sEditMask, sAutoSelect,
            sImageFormat, sVideoFormat, sGizmoMode, sPickMode, b.tip, b.size, b.hardness, b.opacity, b.flow, b.spacing, b.jitter, b.angle,
            b.roundness, b.smoothing, b.pencil, b.pressureSize, b.pressureOpacity, b.mode, b.follow, b.lockAlpha, e.size, e.hardness, e.mode,
            e.pressureSize, e.pressureOpacity, e.follow, e.spacing, e.jitter, e.smoothing, e.angle, sRetouch.size, sRetouch.hardness,
            sRetouch.opacity, sRetouch.spacing, sRetouch.pressureSize, sRetouch.pressureOpacity, sBrushPreset, sAuditDialogs);
   Scene2DNode* doc = FindDoc();
   // The vector and text tools' "next item" settings, which the bar edits when nothing is picked.
   const Shape& v = sStyle;
   char tools[512];
   snprintf(tools, sizeof tools,
            " lockscale %d shape %d vec %d %.3f %.3f %.3f %.3f %d %.3f %.3f %.3f %.3f %.2f %d %d %d %.1f %.1f %d %.1f"
            " text %s %.1f %.2f %.1f %d %.3f %.3f %.3f %.3f",
            sLockScale, sShapeKind, v.fillOn, v.fillC[0], v.fillC[1], v.fillC[2], v.fillC[3], v.strokeOn, v.strokeC[0], v.strokeC[1],
            v.strokeC[2], v.strokeC[3], v.width, v.cap, v.join, v.align, v.dash, v.gap, v.op, v.radius, sFont.c_str(), sTextSize,
            sLeading, sTracking, sTextAlign, sTextC[0], sTextC[1], sTextC[2], sTextC[3]);
   return std::string(buf) + tools + " grev " + std::to_string(sGradReverse ? 1 : 0) + " sel " + sel + (doc != nullptr ? " qm " + std::to_string(doc->quickMask) : std::string());
}

// The document survives its own save text: written, read back into a fresh node, written again, identical.
std::string View2DAuditRoundTrip()
{
   Scene2DNode* doc = FindDoc();
   if (doc == nullptr)
      return "no document";
   const std::string a = doc->DocText();
   Scene2DNode copy;
   copy.SetDocText(a);
   const std::string b = copy.DocText();
   if (a == b)
      return std::string();
   size_t i = 0;
   while (i < a.size() && i < b.size() && a[i] == b[i])
      i++;
   const size_t from = i > 24 ? i - 24 : 0;
   std::string q = a.substr(from, 56) + " -> " + b.substr(from, 56);
   for (char& c : q)
      if (c == '\n' || c == '\t')
         c = ' ';
   return q;
}

// A hash of the composited picture (cooked now), for "the control changed the render".
uint64_t View2DAuditRenderHash()
{
   Scene2DNode* doc = FindDoc();
   if (doc == nullptr)
      return 0;
   static int frame = 5000000;
   doc->CookIfNeeded(++frame);
   std::vector<uint8_t> px;
   int w = 0, h = 0;
   if (!doc->ReadOutput(px, w, h))
      return 0;
   uint64_t hsh = 1469598103934665603ull;
   for (size_t k = 0; k < px.size(); k += 7)
      hsh = (hsh ^ px[k]) * 1099511628211ull;
   return hsh ^ ((uint64_t)w << 32) ^ (uint64_t)h;
}

// Values a typed number must never leave in the document (opacity past 1, a negative size, NaN): empty when all hold.
std::string View2DAuditRanges()
{
   Scene2DNode* doc = FindDoc();
   if (doc == nullptr)
      return std::string();
   char buf[160];
   for (const Layer& l : doc->Layers())
   {
      auto bad = [&](const char* what, float v, float lo, float hi) {
         if (!(v >= lo && v <= hi))
         {
            snprintf(buf, sizeof buf, "layer %d %s = %g (%g..%g)", l.id, what, v, lo, hi);
            return true;
         }
         return false;
      };
      if (bad("opacity", l.opacity, 0, 1) || bad("fill", l.fill, 0, 1) || bad("x", l.x, -1e6f, 1e6f) || bad("y", l.y, -1e6f, 1e6f) ||
          bad("scale x", l.sx, -1e4f, 1e4f) || bad("scale y", l.sy, -1e4f, 1e4f) || bad("angle", l.angle, -1e6f, 1e6f) ||
          bad("mask density", l.maskDensity, 0, 1))
         return buf;
      for (int f = 0; f < Scene2DNode::kFxCount; f++)
         if (bad("fx opacity", l.fx[f].c[3], 0, 1) || bad("fx size", l.fx[f].size, 0, 1e4f) || bad("fx spread", l.fx[f].spread, 0, 1))
            return buf;
   }
   const auto& d = doc->Doc();
   if (d.w < 1 || d.h < 1 || d.w > 16384 || d.h > 16384)
   {
      snprintf(buf, sizeof buf, "document %d x %d", d.w, d.h);
      return buf;
   }
   for (const BrushEngine::Brush* b : { &sBrush, &sEraser })
      if (!(b->size >= 1.0f && b->size <= 5000.0f) || !(b->hardness >= 0 && b->hardness <= 1) || !(b->opacity >= 0 && b->opacity <= 1) ||
          !(b->flow >= 0 && b->flow <= 1))
      {
         snprintf(buf, sizeof buf, "brush size %g hardness %g opacity %g flow %g", b->size, b->hardness, b->opacity, b->flow);
         return buf;
      }
   return std::string();
}

// Puts the panel back as the current state had it (a poke may have changed the tool, tab or selection).
void View2DAuditRestore()
{
   if (sAuditState >= 0)
      View2DAuditState(sAuditState);
}
#endif

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

// The brush follows modulators through the open document: registered, not drawn (the tool bar is where it shows).
static void RegisterBrushParams(const GraphNode& gn)
{
   if (gn.uid != sDocUid)
      return;
   const bool was = gParamRegisterOnly;
   gParamRegisterOnly = true;
   ModSlider("Brush size", &sBrush.size, 1.0f, 1000.0f, "%.0f", kParamWidth, false, 0.0f, nullptr, nullptr, kBrushSizeParam);
   ModSlider("Brush opacity", &sBrush.opacity, 0.01f, 1.0f, "%.2f", kParamWidth, false, 0.0f, nullptr, nullptr, kBrushOpacityParam);
   ModSlider("Brush flow", &sBrush.flow, 0.01f, 1.0f, "%.2f", kParamWidth, false, 0.0f, nullptr, nullptr, kBrushFlowParam);
   ModSlider("Brush hue", &sBrushHue, -180.0f, 180.0f, "%.0f", kParamWidth, false, 0.0f, nullptr, nullptr, kBrushHueParam);
   gParamRegisterOnly = was;
}

// Bound layer numbers are this node's params after Drive D: one slider each on the card, so the pin takes a cable.
static void DrawScene2DSlots(Scene2DNode* doc)
{
   const std::vector<Scene2DNode::ModSlot> slots = doc->Doc().slots;   // a slider may commit, which rewrites the list
   for (const Scene2DNode::ModSlot& m : slots)
   {
      Layer* l = doc->FindLayer(m.layer);
      float* v = l != nullptr ? Scene2DNode::LayerFieldPtr(*l, m.field) : nullptr;
      if (v == nullptr)
         continue;
      float lo = 0.0f, hi = 1.0f;
      doc->LayerFieldRange(*l, m.field, lo, hi);
      const std::string name = LayerName(*l) + " " + T(Scene2DNode::LayerFieldName(m.field));
      const std::string label = name + "##v2dslot" + std::to_string(m.slot);
      ModSlider(label.c_str(), v, lo, hi, "%.2f", kParamWidth, false, 0.0f, nullptr, nullptr, m.slot, name.c_str());
   }
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
   // Drive A..D: adjustment layers follow these, so a modulator animates what is inside a layer.
   static const char* kLabels[Scene2DNode::kDrives] = { "Drive A", "Drive B", "Drive C", "Drive D" };
   for (int i = 0; i < Scene2DNode::kDrives; i++)
      ModSlider(kLabels[i], &doc->drive[i], 0.0f, 1.0f, "%.2f");
   RegisterBrushParams(gn);
   DrawScene2DSlots(doc);
}

// A 2D-View-only document never draws a card, so nothing would register its params: register them here without
// drawing, so a modulator bound in the 2D View keeps driving the layer.
void RegisterScene2DParams(GraphNode& gn)
{
   auto* doc = dynamic_cast<Scene2DNode*>(gn.node.get());
   if (doc == nullptr)
      return;
   BeginNodeParams(gn.index);
   const bool was = gParamRegisterOnly;
   gParamRegisterOnly = true;
   static const char* kLabels[Scene2DNode::kDrives] = { "Drive A", "Drive B", "Drive C", "Drive D" };
   for (int i = 0; i < Scene2DNode::kDrives; i++)
      ModSlider(kLabels[i], &doc->drive[i], 0.0f, 1.0f, "%.2f");
   RegisterBrushParams(gn);
   DrawScene2DSlots(doc);
   gParamRegisterOnly = was;
   EndNodeParams();
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
