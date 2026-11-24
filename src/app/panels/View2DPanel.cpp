// The 2D View: a Scene 2D document laid out as layers over node outputs, the way
// a picture is built in an image editor. Everything here edits the document's
// layers; the nodes themselves are only read, so a layer keeps animating under
// whatever is stacked on top of it. Plan: internal/plans/2d-view/README.md.
#include "app/AppCommon.h"
#include "app/ui/design/ColourMath.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/ColourChip.h"
#include "app/ui/design/components/Thumb.h"
#include "app/ui/design/components/ListRow.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/Keymap.h"
#include "app/ui/design/components/Tip.h"
#include "app/AppShared.h"
#include "core/Notices.h"
#include "app/frame/CanvasLink.h"
#include "ImageCable.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiDensity.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/PanelKit.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/CheckBox.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/ReorderDrag.h"
#include "app/ui/design/components/SectionCard.h"
#include "app/ui/design/components/SettingsWindow.h"
#include "core/BlendModes.h"
#include "core/BrushEngine.h"
#include "nodes/AudioColorRampNode.h"
#include "nodes/DrawNode.h"
#include "nodes/FeedbackNodes.h"
#include "nodes/FluidNode.h"
#include "nodes/OpticalFlowNode.h"
#include "nodes/TimeMachineNode.h"
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
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <fstream>
#include <string>
#include <vector>
#include "app/ui/design/UiFmt.h"
#include "app/ui/design/UiScope.h"

namespace app
{
static bool Is3DNode(INode* n);
static bool ImportSvgInto(Scene2DNode* doc, const std::string& path);
bool AddUserFont(const std::string& path, std::string& family);

using I18n::L;
using I18n::T;

bool gView2DOpen = false;
float gView2DHeight = tok::view2d_view2_d_height;
int gView2DDock = 0;
float gView2DWidth = tok::view2d_view2_d_width;
int gView2DDockBack = 0;
bool gView2DClaimedKeys = false;
int gView2DMenuEdit = 0;
std::string gView2DTools;
std::string gView2DSwatches;
ImVec2 gView2DRectMin, gView2DRectMax;

namespace
{
   using Layer = Scene2DNode::Layer;
   using Kind = Scene2DNode::Kind;
   bool OpenLayeredFile(const std::string& path, ImVec2 at);   // PSD or ORA as a new document
   void ExportLayered(Scene2DNode* doc, const char* ext);

   constexpr float kSideW = UiDensity::kInspectorW + tok::control_h + tok::space_3;   // tab rail + one compact strip
   constexpr float kIconW = tok::control_h;   // icon chips are square, one panel chip
   inline float SideChip() { return PanelKit::Chip(); }   // the side panel's chips: one control tall at its density
   constexpr float kRowH = tok::row_h;   // one layer row
   float sChip = PanelKit::Chip();        // chip size for the panel being drawn
   float sRowH = PanelKit::Chip();        // height of text chips and bar rows in the part being drawn

   // The top and bottom bars use the side panel's compact row, so a bar slider and a panel slider are the same size.
   float CompactRowH() { return PanelKit::Chip(); }
   struct CompactBar
   {
      CompactBar()
      {
         sRowH = sChip = CompactRowH();
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, PanelKit::FramePadY()));
      }
      ~CompactBar()
      {
         ImGui::PopStyleVar();
         sRowH = sChip = PanelKit::Chip();
      }
   };
   constexpr float kRuler = tok::view2d_ruler;
   constexpr float kHandle = 7.0f;
   constexpr float kSnapPx = tok::hit_snap;
   constexpr float kPi = 3.14159265f;
   constexpr const char* kUndo = "Edit in 2D View";

   // Overlay colours are an image editor's, not the app's: they are read against the picture.
   constexpr unsigned kPasteboard = 0x282828, kOutline = 0x2D8CFF, kSmart = 0xFF2DB4, kGuide = 0x00D2E6, kGrid = 0x808080;

   enum Tool { kMove = 0, kCrop, kHand, kZoom, kBrush, kEraser, kFill, kPick, kMarquee, kLasso, kWand, kPen, kNode, kShape, kText,
               kGradient, kClone, kHeal, kBlur, kSharpen, kDodge, kBurn, kSponge, kMedian, kUndoBrush, kColourReplace, kMixer,
               kBgErase, kFloodErase, kSelBrush, kObjectSel, kRedEye, kBlemish, kPatch, kInpaint, kMeshWarp, kPerspective, kStylePick,
               kMeasure, kToolCount };
   int sTool = kMove;
   bool RetouchTool(int t)
   {
      return (t >= kClone && t <= kBurn) || t == kSponge || t == kMedian || t == kUndoBrush || t == kColourReplace || t == kBgErase ||
             t == kRedEye || t == kInpaint;
   }
   bool BrushTool(int t) { return t == kBrush || t == kEraser || t == kMixer || t == kSelBrush || RetouchTool(t); }
   [[maybe_unused]] bool PaintTool(int t) { return BrushTool(t) || t == kFill || t == kGradient; }
   bool PixelTool(int t) { return (t >= kBrush && t <= kWand) || (t >= kGradient && t != kPerspective && t != kStylePick); }

   // Paint tools. The brush and the eraser keep their own settings.
   BrushEngine::Brush sBrush, sEraser;
   // The brush's numbers are params of the open document, after Drive A..D and before the layer slots.
   constexpr int kBrushSizeParam = 4, kBrushOpacityParam = 5, kBrushFlowParam = 6, kBrushHueParam = 7;
   // Whole steps for counts and degrees, two decimals for the rest.
   const char* AdjFmt(int t)
   {
      using S = Scene2DNode;
      if (t == S::kAdjShadowsHighlights)
         return "%.0f px";
      if (t == S::kAdjPixelate || t == S::kAdjHalftone)
         return "%.0f px";
      if (t == S::kAdjHue)
         return "%.0f\xC2\xB0";
      return t == S::kAdjDither || t == S::kAdjPosterize ? "%.0f" : "%.2f";
   }
   float sBrushHue = 0.0f;   // degrees, turns the colour each stroke starts with (sound as a paint input)
   // Turns an RGB colour round the hue circle, keeping its value and saturation.
   void HueTurn(float c[3], float degrees)
   {
      float h = 0, sat = 0, v = 0;
      ImGui::ColorConvertRGBtoHSV(c[0], c[1], c[2], h, sat, v);
      h = std::fmod(h + degrees / tok::view2d_hue_turn_ratio + 1.0f, 1.0f);
      ImGui::ColorConvertHSVtoRGB(h, sat, v, c[0], c[1], c[2]);
   }
   int sBrushPreset = -1, sEraserPreset = -1;
   bool sBrushInit = false;
   float sFg[3] = { 0.0f, 0.0f, 0.0f }, sBg[3] = { 1.0f, 1.0f, 1.0f };
   void NoteColourUsed(const float* c);   // the recent-colours row, below
   int sFillKind = 1;              // gradient: 1 linear, 2 radial, 3 angle, 4 reflected, 5 diamond
   float sGradOpacity = 1.0f;
   int sPickSize = 1;              // eyedropper: 1 point, 3 or 5 px square averaged
   bool sPickLayer = false;        // eyedropper reads the active paint layer alone
   bool sCloneAligned = true;      // clone / heal: the offset holds across strokes
   bool sCloneAll = false;         // clone / heal: copy from the whole picture
   bool sGradReverse = false;      // gradient runs background to foreground
   double sCloneAsk = -10.0;       // clone stamp pressed without a source: a canvas note says how to set one
   std::string sExportNote;         // after a PSD export: what Photoshop gets as pixels or not at all
   double sExportNoteT = -10.0;
   double sGroupPickAsk = -10.0;   // eraser/retouch on an empty/locked group: a canvas note says "Pick a layer"
   struct GroupStrokePt { float x = 0.0f, y = 0.0f, p = 1.0f; };
   std::vector<int> sGroupStrokeIds;
   std::vector<GroupStrokePt> sGroupStrokePts;
   BrushEngine::Brush sGroupStrokeBrush;
   bool sMeshNearest = false;      // mesh warp: nearest-neighbour resampling (off: bilinear)
   bool sObjSoft = false;          // object selection: feathered edge
   bool sRadiusAbs = true;         // shape radius in px (off: % of the short side)
   bool sPatchSrc = false, sPatchClear = false;   // patch: selection is source; transparent
   int sStyleParts = Scene2DNode::kStAll;   // style picker: which parts travel
   int sGradExtend = 0;            // past the ends: 0 pad, 1 repeat, 2 reflect
   bool sGradDither = true;        // breaks up 8-bit banding
   bool sCropDarken = true, sCropReveal = true;   // crop: dim outside the box; show it at all
   bool sSelBrushSub = false;      // selection brush: takes away (Alt flips it)
   bool sSelBrushSoft = true;      // selection brush: soft edges (off: hard)
   int sTolerance = 32;  // ui-audit-ok: unit (colour tolerance default (0-255))
   bool sContiguous = true, sAllLayers = false;
   int sImageFormat = 0, sVideoFormat = 0;   // the Render tab's default file types
   bool sSurfacePaint = true;      // brush / eraser / eyedropper work on a 3D object's surface
   bool sSurfaceStroke = false;    // the stroke now running paints a surface
   int sSkinSide = 2;              // 0..3: 512 .. 4096 px, for the next new skin
   bool sEditMask = false;         // the brush paints the active layer's pixel mask
   int sMarqueeShape = 0;          // 0 rectangle, 1 ellipse
   int sMarqueeRatio = 0;          // 0 free, 1 1:1, 2 4:3, 3 16:9, 4 fixed size
   float sMarqueeFixed[2] = { 256.0f, 256.0f };
   int sCropRatio = 0;             // 0 free, 1 original, 2 1:1, 3 4:3, 4 3:2, 5 16:9, 6 9:16
   bool sCropThirds = true;
   int sLassoKind = 0;             // 0 freehand, 1 polygon
   float sFeather = 0.0f;
   float sSelModPx = tok::view2d_sel_mod_px;         // Expand / Contract / Smooth / Border, document px
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
   // Deep zoom to single pixels (Affinity goes past 18000%): 256x, sampled nearest from 2x, a pixel grid from 8x.
   constexpr float kZoomMax = 256.0f;
   ImVec2 sPan(0.0f, 0.0f);        // the document centre's offset from the view centre, screen px
   float sViewAngle = 0.0f;        // the view turned about the document centre, radians, clockwise on screen
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
      int warpStyle = 0;          // 18: 0 as the handle says, 1 a corner slides along one edge (skew), 2 an edge moves freely (distort)
      bool fromCentre = false;
      int guideAxis = 0;          // 0 vertical (x), 1 horizontal (y)
      int guideIndex = -1;
      float crop0[4] = { 0, 0, 0, 0 };
      int cropHandle = -1;        // 0..7 handle, 8 inside
      ImVec2 pan0;
      bool made = false;          // the shape tool has added its item
      int gizmoAxis = -1;         // 16: the 3D gizmo part held (0..2 axes, 3 centre)
      float g0[4] = { 0, 0, 0, 0 };   // 20/21: the group box at the start, x0 y0 x1 y1
      size_t undo0 = (size_t)-1;      // the undo depth before this drag's step, so Esc can drop it
      float cropWas[4] = { 0, 0, 0, 0 };   // 5: the crop box before the drag
      float zoom0 = 1.0f;             // 19: the zoom as the drag began
      float angle0 = 0.0f;            // 22: the view angle as the turn began
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
      enum What { None, NewDoc, DuplicateDoc, DeleteDoc, OpenFile, ExportPsd, ExportOra, Convert, RenderImage, RenderAnim, Group, Ungroup, Duplicate, Copy, Cut, Paste, Delete, AddFill, AddPaint, AddMask, MaskFromSel, RemoveMask, ToDraw, AddVector, AddVMask, RemoveVMask, ToSketch, ToText, ToShapes, AddAdjust, AddNode, ReplaceNode, LayerOutput, LayerBelow, MergeDown, MergeVisible, Flatten, FlattenGroup, CopyMerged, StampVisible, StampDown };
      What what = None;
      int arg = 0;         // AddAdjust: the type
      uint64_t uid = 0;    // AddNode / ReplaceNode: the canvas node
   } sPending;

   // Click-a-node-on-the-canvas mode, as the 3D View has: 0 off, 1 add the picked node as a layer, 2 replace the active layer's source.
   // Canvas Link picks: add a node as a layer, or replace the selected layer's source (CanvasLink.h).
   CanvasLink::Spec PickSpec(bool replace)
   {
      CanvasLink::Spec spec;
      spec.owner = replace ? "v2d.replace" : "v2d.addnode";
      spec.kind = T("picture node");
      spec.accepts = [](const GraphNode& gn) { return dynamic_cast<const Scene2DNode*>(gn.node.get()) == nullptr && View2DCanAdd(gn); };
      spec.onPicked = [replace](uint64_t uid)
      {
         if (uid == 0)
            return;
         sPending.what = replace ? Pending::ReplaceNode : Pending::AddNode;
         sPending.uid = uid;
      };
      return spec;
   }
   int PickModeNow() { return CanvasLink::Armed("v2d.addnode") ? 1 : CanvasLink::Armed("v2d.replace") ? 2 : 0; }

   ImU32 Hex(unsigned rgb, float a = 1.0f)
   {
      return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (int)(std::clamp(a, 0.0f, 1.0f) * 255.0f));  // ui-audit-ok: mask (channel mask / bit packing, not a colour value)
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
      uint64_t selected = 0, anyNode = 0;
      for (GraphNode& gn : gNodes)
         if (ed::IsNodeSelected(gn.NodeId()))
         {
            anyNode = gn.uid;
            break;
         }
      (void)anyNode;
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
      return MakeDoc(at.x + tok::view2d_ensure_doc_span, at.y, "");
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
         bool cut = false;
         std::string first = Scene2DNode::TextName(l.text, 28, &cut);
         if (cut)
            first += "...";
         return first.empty() ? std::string(T("Text")) + " " + std::to_string(l.id) : first;
      }
      GraphNode* gn = FindNodeByUid(l.uid);
      return gn != nullptr ? gn->typeName + " " + std::to_string(gn->index) : std::string(T("source missing"));
   }

   // A layer's locks with its groups' locks: a locked group locks every layer in it (Photoshop).
   // A document's text with its live selection dropped (saved selections stay): a duplicate starts with none.
   std::string DocTextWithoutSelection(const std::string& text)
   {
      std::string out;
      size_t at = 0;
      while (at < text.size())
      {
         size_t nl = text.find('\n', at);
         nl = nl == std::string::npos ? text.size() : nl + 1;
         if (text.compare(at, 4, "sel ") != 0)
            out.append(text, at, nl - at);
         at = nl;
      }
      return out;
   }

   // A lock chip's click. Locking image pixels clears the transparency lock and holds it off while on (Photoshop).
   unsigned ToggledLock(unsigned lock, unsigned bit)
   {
      if (bit == Scene2DNode::kLockAlpha && (lock & Scene2DNode::kLockPixels) != 0)
         return lock;
      lock ^= bit;
      if (bit == Scene2DNode::kLockPixels && (lock & Scene2DNode::kLockPixels) != 0)
         lock &= ~(unsigned)Scene2DNode::kLockAlpha;
      return lock;
   }

   int LockOf(Scene2DNode* doc, const Layer& l)
   {
      int lock = l.lock;
      for (int p = l.parent, guard = 0; p != 0 && guard < 64; guard++)
      {
         const Layer* g = doc->FindLayer(p);
         if (g == nullptr)
            break;
         lock |= g->lock;
         p = g->parent;
      }
      return lock;
   }
   bool Movable(Scene2DNode* doc, const Layer& l) { return (LockOf(doc, l) & (Scene2DNode::kLockPosition | Scene2DNode::kLockAll)) == 0; }

   // ---- view mapping ----------------------------------------------------

   // Where the document's centre sits on screen.
   ImVec2 DocCentre()
   {
      return ImVec2((sViewMn.x + sViewMx.x) * 0.5f + sPan.x, (sViewMn.y + sViewMx.y) * 0.5f + sPan.y);
   }
   // The document's (0, 0) on screen when the view is upright (the rulers only draw then).
   ImVec2 DocOrigin(Scene2DNode* doc)
   {
      const ImVec2 c = DocCentre();
      return ImVec2(c.x - doc->Doc().w * sZoom * 0.5f, c.y - doc->Doc().h * sZoom * 0.5f);
   }
   // Draw-list callback: high bit set = nearest magnification for the texture, clear = back to linear.
   void MagFilterCb(const ImDrawList*, const ImDrawCmd* cmd)
   {
      const unsigned v = (unsigned)(intptr_t)cmd->UserCallbackData;
      glBindTexture(GL_TEXTURE_2D, v & 0x7FFFFFFFu);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (v & tok::view2d_nearest_filter_bit) ? GL_NEAREST : GL_LINEAR);  // ui-audit-ok: mask (bit packing: the nearest-filter flag bit on a texture id)
   }
   bool ViewTurned() { return std::fabs(sViewAngle) > 1e-4f; }
   ImVec2 ToScreen(Scene2DNode* doc, float x, float y)
   {
      const ImVec2 c = DocCentre();
      const float dx = (x - doc->Doc().w * 0.5f) * sZoom, dy = (y - doc->Doc().h * 0.5f) * sZoom;
      if (!ViewTurned())
         return ImVec2(c.x + dx, c.y + dy);
      const float co = std::cos(sViewAngle), si = std::sin(sViewAngle);
      return ImVec2(c.x + co * dx - si * dy, c.y + si * dx + co * dy);
   }
   // A screen-px step as a document-px step (the view's turn and zoom undone).
   ImVec2 DocDelta(float sx, float sy)
   {
      const float co = std::cos(sViewAngle), si = std::sin(sViewAngle);
      return ImVec2((co * sx + si * sy) / sZoom, (-si * sx + co * sy) / sZoom);
   }
   ImVec2 ToDoc(Scene2DNode* doc, ImVec2 s)
   {
      const ImVec2 c = DocCentre();
      const ImVec2 d = DocDelta(s.x - c.x, s.y - c.y);
      return ImVec2(d.x + doc->Doc().w * 0.5f, d.y + doc->Doc().h * 0.5f);
   }
   // A document-space box drawn on screen (it turns with the view).
   void DocRect(Scene2DNode* doc, float x0, float y0, float x1, float y1, ImVec2 q[4])
   {
      q[0] = ToScreen(doc, x0, y0);
      q[1] = ToScreen(doc, x1, y0);
      q[2] = ToScreen(doc, x1, y1);
      q[3] = ToScreen(doc, x0, y1);
   }
   void AddDocRect(ImDrawList* dl, Scene2DNode* doc, float x0, float y0, float x1, float y1, ImU32 col, float thick = 1.0f)
   {
      ImVec2 q[4];
      DocRect(doc, x0, y0, x1, y1, q);
      dl->AddPolyline(q, tok::view2d_line_num_points_md, col, ImDrawFlags_Closed, thick);
   }
   // Turn the view by `rad` about a screen point (the document centre when it is the pivot).
   void TurnViewAbout(Scene2DNode* doc, float rad, ImVec2 at)
   {
      const ImVec2 before = ToDoc(doc, at);
      sViewAngle = std::remainder(sViewAngle + rad, 6.2831853f);
      const ImVec2 after = ToScreen(doc, before.x, before.y);
      sPan.x += at.x - after.x;
      sPan.y += at.y - after.y;
   }

   // Within 3 degrees of a multiple of 15 the turn settles on it, so upright is easy to find again.
   float SnapViewAngle(float a)
   {
      const float step = 15.0f * 3.14159265f / 180.0f, k = std::round(a / step) * step;
      return std::fabs(a - k) < 3.0f * 3.14159265f / 180.0f ? k : a;
   }

   void FitView(Scene2DNode* doc)
   {
      const float vw = std::max(40.0f, sViewMx.x - sViewMn.x - tok::view2d_fit_view_span), vh = std::max(40.0f, sViewMx.y - sViewMn.y - tok::view2d_fit_view_span);
      // A turned document fits by its turned box.
      const float co = std::fabs(std::cos(sViewAngle)), si = std::fabs(std::sin(sViewAngle));
      const float bw = co * doc->Doc().w + si * doc->Doc().h, bh = si * doc->Doc().w + co * doc->Doc().h;
      sZoom = std::clamp(std::min(vw / std::max(1.0f, bw), vh / std::max(1.0f, bh)), 0.01f, kZoomMax);
      sPan = ImVec2(0, 0);
   }

   void ZoomAt(Scene2DNode* doc, float ratio, ImVec2 at)
   {
      const ImVec2 before = ToDoc(doc, at);
      sZoom = std::clamp(sZoom * ratio, 0.01f, kZoomMax);
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
             (LockOf(doc, l) & Scene2DNode::kLockAll) != 0)
            continue;
         if (l.kind == Kind::Vector)
         {
            // Only where the layer has items, not its whole document-sized box.
            float bx = 0, by = 0;
            doc->DocToBox(l, p.x, p.y, bx, by);
            for (const Scene2DNode::Shape& s : l.shapes)
            {
               float lo[2], hi[2];
               if (Scene2DNode::ShapeBounds(s, lo, hi) && bx >= lo[0] && bx <= hi[0] && by >= lo[1] && by <= hi[1] &&
                   (s.type == Scene2DNode::Shape::kSvg || Scene2DNode::ShapeHit(s, bx, by, 2.0f)))
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
             (LockOf(doc, l) & Scene2DNode::kLockAll) != 0)
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
         if (l.kind == Kind::Group || !Movable(doc, l))
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

   int sTextEdit = 0;              // the text layer being typed into
   // ---- the target rule (internal/plans/2d-view/tool-layer-matrix.md) ------------------------------
   // A tool acts on the active layer when it has ink where the tool works, else on the topmost shown, unlocked layer
   // that does (Photoshop's Auto-Select, Layer mode), which then becomes active and shows in Layers.

   bool InkKind(const Layer& l) { return l.kind == Kind::Paint || l.kind == Kind::Node || l.kind == Kind::Text || l.kind == Kind::Vector; }

   // One layer's own picture, document size, rows top-down: read back once per frame per layer.
   const std::vector<uint8_t>* LayerInk(Scene2DNode* doc, int id)
   {
      struct Entry { int id; int frame; std::vector<uint8_t> rgba; };
      static std::vector<Entry> cache;
      static int sInkFrame = 1 << 21;
      const int now = ImGui::GetFrameCount();
      for (Entry& e : cache)
         if (e.id == id && e.frame == now)
            return e.rgba.empty() ? nullptr : &e.rgba;
      cache.erase(std::remove_if(cache.begin(), cache.end(), [&](const Entry& e) { return e.frame != now || e.id == id; }), cache.end());
      Entry e{ id, now, {} };
      if (!doc->RenderLayerAlone(id, ++sInkFrame, e.rgba) || e.rgba.size() != (size_t)doc->Doc().w * doc->Doc().h * 4)  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
         e.rgba.clear();
      cache.push_back(std::move(e));
      return cache.back().rgba.empty() ? nullptr : &cache.back().rgba;
   }

   // Any pixel with alpha in [lo, hi] (document px) of this layer's own picture.
   bool InkIn(Scene2DNode* doc, const Layer& l, float lo[2], float hi[2])
   {
      if (!InkKind(l) || !doc->Shown(l) || (LockOf(doc, l) & Scene2DNode::kLockAll) != 0)
         return false;
      // Cheap rejects before reading pixels back: an empty paint layer, a vector layer with no shape near.
      if (l.kind == Kind::Paint && std::all_of(l.tiles.begin(), l.tiles.end(), [](const std::string& k) { return k == "-"; }))
         return false;
      if (l.kind == Kind::Vector && l.shapes.empty())
         return false;
      const std::vector<uint8_t>* px = LayerInk(doc, l.id);
      if (px == nullptr)
         return false;
      const int w = doc->Doc().w, h = doc->Doc().h;
      const int x0 = std::clamp((int)std::floor(lo[0]), 0, w - 1), x1 = std::clamp((int)std::ceil(hi[0]), 0, w - 1);
      const int y0 = std::clamp((int)std::floor(lo[1]), 0, h - 1), y1 = std::clamp((int)std::ceil(hi[1]), 0, h - 1);
      // A big box is sampled on a grid (about 200 x 200 probes): a stroke thinner than the step can be missed.
      const int step = std::max(1, std::max(x1 - x0, y1 - y0) / tok::view2d_ink_in_ratio);
      for (int y = y0; y <= y1; y += step)
         for (int x = x0; x <= x1; x += step)
            if ((*px)[((size_t)y * w + x) * 4 + 3] > 8)  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
               return true;
      return false;
   }

   // The target rule for a point (lo == hi) or a box. Returns the layer acted on (0 = none) and makes it active.
   int ToolTarget(Scene2DNode* doc, float lo[2], float hi[2])
   {
      // Topmost first, as Photoshop's Auto-Select: a marquee round the "e" of a word over a photo picks the word even
      // while the photo is active.
      Layer* a = Active(doc);
      const std::vector<Layer>& ls = doc->Layers();
      for (int i = (int)ls.size() - 1; i >= 0; i--)
         if (InkIn(doc, ls[i], lo, hi))
         {
            const int id = ls[i].id;
            if (sTextEdit != 0 && sTextEdit != id)
               sTextEdit = 0;
            SelectOnly(id);
            return id;
         }
      return a != nullptr ? a->id : 0;
   }

   int ToolTargetAt(Scene2DNode* doc, ImVec2 p)
   {
      float lo[2] = { p.x, p.y }, hi[2] = { p.x, p.y };
      return ToolTarget(doc, lo, hi);
   }

   // A new selection picks the layer it covers.
   void TargetSelection(Scene2DNode* doc)
   {
      float lo[2], hi[2];
      if (!doc->quickMask && doc->HasSelection() && doc->SelectionBounds(lo, hi))
         ToolTarget(doc, lo, hi);
   }

   void ToggleSel(int id)
   {
      auto it = std::find(sSel.begin(), sSel.end(), id);
      if (it != sSel.end())
         sSel.erase(it);
      else
         sSel.push_back(id);
   }

   bool CanLift(Scene2DNode* doc);
   bool LiftSelection(Scene2DNode* doc);

   void Nudge(Scene2DNode* doc, float dx, float dy)
   {
      // With a marquee on a paint layer the arrows move the selected pixels, as a drag does (Photoshop;
      // PhotoCraft nudging_away_and_back_keeps_the_moved_pixels_selection). It goes down at the frame's end.
      if (CanLift(doc))
      {
         const size_t undo0 = gUndoStack.size();
         PushUndoCheckpoint("Nudge selection");
         if (!LiftSelection(doc))
            gUndoStack.resize(undo0);
         else
         {
            if (Layer* f = doc->FindLayer(sSel.front()))
            {
               f->x += dx;
               f->y += dy;
            }
            Touch();
            return;
         }
      }
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
            if (l.kind != Kind::Group && Movable(doc, l) && doc->IsInside(l.id, id))
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
      if (order.size() < tok::view2d_distribute_limit)
         return;
      std::sort(order.begin(), order.end());
      PushUndoCheckpoint("Distribute in 2D View");
      const float first = order.front().first, step = (order.back().first - first) / (float)(order.size() - 1);
      for (size_t i = 1; i + 1 < order.size(); i++)
      {
         const float d = first + step * (float)i - order[i].first;
         for (Layer& l : doc->Layers())
            if (l.kind != Kind::Group && Movable(doc, l) && doc->IsInside(l.id, order[i].second))
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
         if (const Layer* l = doc->FindLayer(id))
         {
            // A deleted clip base releases the layers clipped to it (Photoshop), not hands them to the next one down.
            if (!l->clip)
            {
               const std::vector<int> kids = doc->ChildrenOf(l->parent);
               for (auto it = std::find(kids.begin(), kids.end(), id) + 1; it < kids.end(); ++it)
               {
                  Layer* up = doc->FindLayer(*it);
                  if (up == nullptr || !up->clip)
                     break;
                  if (std::find(sSel.begin(), sSel.end(), *it) == sSel.end())
                     up->clip = false;
               }
            }
            doc->RemoveLayer(id);
         }
      sSel.clear();
   }

   // A new layer goes above the active one; with an open group active it goes inside the group, on top
   // (Photoshop). AddLayer already places it past the group's children, so only its parent changes.
   int NewAbove(Scene2DNode* doc)
   {
      const Layer* a = Active(doc);
      return a != nullptr ? a->id : 0;
   }

   // Call right after a layer was added above NewAbove(doc), before the active layer changes.
   Layer& PlaceNew(Scene2DNode* doc, int id)
   {
      const Layer* a = Active(doc);
      const int into = a != nullptr && a->kind == Kind::Group && a->open ? a->id : 0;
      if (into != 0)
         doc->FindLayer(id)->parent = into;
      // It joins the clip group it lands in: clipped when the layer just above its slot is (Photoshop; PhotoCraft
      // new_layer_joins_the_group_it_lands_in: "B C*" with C active gives "B C* N").
      const std::vector<int> kids = doc->ChildrenOf(doc->FindLayer(id)->parent);
      const auto at = std::find(kids.begin(), kids.end(), id) - kids.begin();
      const Layer* over = at + 1 < (long)kids.size() ? doc->FindLayer(kids[at + 1]) : nullptr;
      Layer& l = *doc->FindLayer(id);
      l.clip = over != nullptr && over->clip;
      return l;
   }

   Layer& NewLayerAt(Scene2DNode* doc, Kind kind, uint64_t uid)
   {
      return PlaceNew(doc, doc->AddLayer(kind, uid, NewAbove(doc)).id);
   }

   void AddPaint(Scene2DNode* doc)
   {
      PushUndoCheckpoint("New layer");
      const int id = NewLayerAt(doc, Kind::Paint, 0).id;
      doc->Commit();
      SelectOnly(id);
   }

   void AddFill(Scene2DNode* doc)
   {
      PushUndoCheckpoint("New fill layer");
      const int id = NewLayerAt(doc, Kind::Fill, 0).id;
      doc->Commit();
      // A marquee masks the new fill to it, as it does a new adjustment (Photoshop).
      if (doc->HasSelection() && !doc->quickMask && doc->MaskFromSelection(id))
         doc->Deselect();   // the selection went into the mask: it is dropped (Photoshop, PhotoCraft #1250)
      SelectOnly(id);
   }

   void AddAdjust(Scene2DNode* doc, int type)
   {
      PushUndoCheckpoint("New adjustment layer");
      Layer& l = NewLayerAt(doc, Kind::Adjust, 0);
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
      if (doc->HasSelection() && !doc->quickMask && doc->MaskFromSelection(id))
         doc->Deselect();   // the selection went into the mask: it is dropped (Photoshop, PhotoCraft #1250)
      SelectOnly(id);
   }

   // True when `candidate` already reads from `doc` (directly or through canvas cables / other Scene2D documents),
   // so adding `candidate` back into `doc` as a layer or mask would create a feedback loop.
   bool WouldCreateDocCycle(const Scene2DNode* doc, INode* candidate)
   {
      if (doc == nullptr || candidate == nullptr)
         return false;
      if (candidate == doc)
         return true;
      std::vector<INode*> stack{ candidate };
      std::set<INode*> visited;
      while (!stack.empty())
      {
         INode* cur = stack.back();
         stack.pop_back();
         if (cur == nullptr || !visited.insert(cur).second)
            continue;
         if (cur == doc)
            return true;
         if (auto* s2 = dynamic_cast<Scene2DNode*>(cur))
         {
            for (const auto& lay : s2->Layers())
            {
               if (lay.uid != 0)
                  if (GraphNode* u = FindNodeByUid(lay.uid))
                     stack.push_back(u->node.get());
               if (lay.maskUid != 0)
                  if (GraphNode* u = FindNodeByUid(lay.maskUid))
                     stack.push_back(u->node.get());
               if (lay.effMaskUid != 0)
                  if (GraphNode* u = FindNodeByUid(lay.effMaskUid))
                     stack.push_back(u->node.get());
               for (const auto& ef : lay.effects)
                  if (ef.uid != 0)
                     if (GraphNode* u = FindNodeByUid(ef.uid))
                        stack.push_back(u->node.get());
            }
            continue;
         }
         GraphNode* curGn = nullptr;
         for (GraphNode& gn : gNodes)
            if (gn.node.get() == cur)
            {
               curGn = &gn;
               break;
            }
         if (curGn == nullptr)
            continue;
         const int nIn = InputCountFor(*curGn);
         for (int slot = 0; slot < nIn; slot++)
         {
            if (ImageCable* ic = CableFor(*curGn, slot))
               if (INode* src = ic->GetSource())
                  stack.push_back(src);
            if (IGeometrySource** field = cur->GeometryInputSlot(slot))
               if (*field != nullptr)
                  if (INode* src = dynamic_cast<INode*>(*field))
                     stack.push_back(src);
         }
      }
      return false;
   }

   // A canvas node as a layer, centred at a document point (the middle when null).
   int AddNodeLayer(Scene2DNode* doc, uint64_t uid, const ImVec2* at)
   {
      GraphNode* gn = FindNodeByUid(uid);
      if (gn == nullptr || !View2DCanAdd(*gn))
         return 0;
      if (WouldCreateDocCycle(doc, gn->node.get()))
      {
         Notices::Post(Notices::Level::Warning, "view2d.cycle", T("Can't add layer"),
                       T("That node already reads from this 2D Document (would create a feedback loop)."));
         return 0;
      }
      Layer& l = NewLayerAt(doc, Kind::Node, uid);
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
      if (WouldCreateDocCycle(doc, gn->node.get()))
      {
         Notices::Post(Notices::Level::Warning, "view2d.cycle", T("Can't replace layer"),
                       T("That node already reads from this 2D Document (would create a feedback loop)."));
         return false;
      }
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
         if (gn.node.get() == doc || !View2DCanAdd(gn) || WouldCreateDocCycle(doc, gn.node.get()))
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
      return PanelKit::IconChip(id, glyphStr, on, tip, w > 0.0f ? w : sChip, sChip);
   }

   // Card headers (effects, styles, adjustment layers): on box, fold arrow + name, then action icons. Every mark is
   // drawn to one size, the on box's, and centred on the header row's middle; the actions are frameless (a well on
   // hover only), so the on box is the one box in the row.
   float HeaderMark() { return CheckBox::Size(); }

   float InkFit(ImVec2 c, const char* g, float ink)
   {
      ImVec2 a, b;
      glyph::InkBox(c, 16.0f, g, a, b);
      const float m = std::max(b.x - a.x, b.y - a.y);
      return m > 1.0f ? std::round(16.0f * ink / m) : 16.0f;
   }

   // One header action at the row's height; returns the click.
   bool HeaderIcon(const char* id, const char* g, bool lit, const char* tip, float rowY)
   {
      const float fh = ImGui::GetFrameHeight();
      ImGui::SetCursorPosY(rowY);
      const bool clicked = UiInteract::Hit(id, ImVec2(fh, fh));
      const bool hov = ImGui::IsItemHovered();
      if (hov && tip != nullptr)
         HelpTip("%s", tip);
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (hov || ImGui::IsItemActive())
         dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBgHovered),
                           tok::radius_control);
      const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
      const float ink = std::round(HeaderMark() * 0.72f);   // a little under the on box, as the 3D View's gear
      const float gs = InkFit(c, g, ink);
      const ImU32 col = lit ? ImGui::GetColorU32(app::AccentEmphasisSelected())
                            : ImGui::GetColorU32(ImGuiCol_Text, tok::view2d_header_icon_color_alpha_mul);
      glyph::Draw(dl, c, gs, col, g);
      ImVec2 imn, imx;
      glyph::InkBox(c, gs, g, imn, imx);
      UiAudit::NoteInk(1, imn, imx, ink, tip, g);
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
      Ui::Claim(ImVec2(sz.x, rowH));
   }

   bool TextChip(const char* label, bool on, const char* tip, float w = 0.0f)
   {
      return PanelKit::TextChip(label, on, tip, w, sRowH);
   }

   // Field widths by content class (plan §3): S numbers, M short words, L labelled sliders and names.
   constexpr float kWellS = tok::view2d_well_s, kWellM = tok::view2d_well_m, kWellL = tok::view2d_well_l;
   float sFieldW = tok::view2d_field_w;
   constexpr float kLabelW = tok::label_w;
   float sLabelW = kLabelW;   // the More.. forms widen it for their longer names

   // rowH > 0: centred on a row of chips that tall (chips are not frames, so frame padding would sit it high).
   void FieldLabel(const char* text, float rowH = 0.0f)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const char* end = ImGui::FindRenderedTextEnd(text);   // "Width##img" shows as "Width"
      // Longer than the label column: an ellipsis and the whole label in a tip, never text under the field.
      const std::string full(text, end);
      const std::string shown = app::TruncateWithEllipsis(full, sLabelW - ImGui::GetCursorPosX() - tok::space_1);
      if (rowH > 0.0f)
      {
         const ImVec2 p = ImGui::GetCursorScreenPos();
         const ImVec2 sz = ImGui::CalcTextSize(shown.c_str());
         const float px = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.y);
         const ImVec2 tp(p.x, std::round((p.y + (rowH - sz.y) * 0.5f) * px) / px);
         ImGui::GetWindowDrawList()->AddText(tp, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, tok::alpha_label)), shown.c_str());
         UiAudit::NoteInk(3, tp, ImVec2(tp.x + sz.x, tp.y + sz.y), 0.0f, full.c_str());
         Ui::Claim(ImVec2(sz.x, rowH));
      }
      else
      {
         ImGui::AlignTextToFramePadding();
         ImGui::TextColored(ImVec4(t.x, t.y, t.z, tok::alpha_label), "%s", shown.c_str());
         UiAudit::NoteInk(3, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 0.0f, full.c_str());
      }
      if (shown != full && ImGui::IsItemHovered())
         HelpTip("%s", full.c_str());
      FormParts::Inline(sLabelW);
   }

   // A row's control slot: in a grid, one cell (label goes in the well, fmt rewritten); else label column + well.
   std::string sCellFmt;
   const char* RowSlot(const char* label, const char* fmt)
   {
      const float cw = PanelKit::NextCell();
      if (cw > 0.0f)
      {
         ImGui::SetNextItemWidth(cw);
         sCellFmt = PanelKit::InWellFormat(label, fmt != nullptr ? fmt : "", cw);
         return sCellFmt.c_str();
      }
      FieldLabel(label);
      ImGui::SetNextItemWidth(sFieldW);
      return fmt;
   }

   // Short settings side by side (PanelKit form grid): the next rows share a row, `cols` across, each label inside
   // its well. Spans the label column and the field column, so a grid row ends where a plain row does.
   struct Pairs : PanelKit::GridScope
   {
      // Fields end at window x sLabelW + sFieldW; the grid starts at the cursor (after the card's inset), so it spans the rest.
      // Compact surfaces (settings strip, More.. popovers) are one vertical column: no side-by-side pairs there.
      explicit Pairs(int cols = 2)
         : PanelKit::GridScope(UiDensity::IsCompact() ? 0 : cols, sLabelW + sFieldW - ImGui::GetCursorPosX()) {}
   };

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
      // Modulation lives on the Graph Canvas (the Scene 2D node); the 2D View only shows that a field is driven.
      // A click on a driven field jumps to its node.
      if (sModNext.driven && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         Tip::Show("%s", T("Driven on canvas"));
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            if (const int ni = DocNodeIndex(); ni >= 0)
            {
               gRequestSelectNodeIndex = ni;
               gRequestFitViewNodeIndex = ni;
            }
      }
      sModNext = {};
   }

   bool DragRow(const char* label, float* v, float speed, const char* fmt, float lo = -1e6f, float hi = 1e6f)   // ui-audit-ok: sentinel (declaration default, an unbounded drag)
   {
      fmt = RowSlot(label, fmt);
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
   int DragMulti(const char* label, float* const* v, const char* const* fmts, int n, float speed, float lo = -1e6f, float hi = 1e6f,   // ui-audit-ok: sentinel (declaration default, an unbounded drag)
                 const Layer* modLayer = nullptr, const int* modFields = nullptr)
   {
      FieldLabel(label);
      int changed = 0;
      FieldWell::PushStyle();
      ImGui::PushID(label);
      for (int i = 0; i < n; i++)
      {
         if (i > 0)
            FormParts::Inline(0.0f, tok::space_1);
         ImGui::PushID(i);
         ImGui::SetNextItemWidth(PanelKit::StripW(sFieldW, n, i));   // whole-pixel cells that end on the column edge
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

   // Blend If's grey ramp (Photoshop's sliders): four handles kept in order - black, black to, white from, white.
   // Each pair starts as one handle: dragging it moves the pair together; Option (Alt) splits it, and a split half
   // then moves alone (drag it back onto its partner, or right-click it, to join them). The cut part of the ramp is veiled and the
   // soft range fades, so the row shows what passes. The values read under the ends; double-click resets.
   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   bool BlendIfRamp(const char* id, float* b, float width)
   {
      static int sHeld = -1;        // handle held (0..3); with sPair, the pair it leads
      static bool sPair = false;    // dragging a joined pair as one
      static float sGrabDx = 0.0f;  // pointer to handle, so a grab never jumps
      static ImGuiID sHeldId = 0;
      const float fh = ImGui::GetFrameHeight();
      const float lineH = ImGui::GetTextLineHeight();
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = std::max(fh * tok::view2d_blend_if_ramp_ratio, width);
      const float hw = std::round(fh * 0.32f);              // handle half width: the hit and the drawn triangle
      const float h = fh + lineH;                            // ramp + handles, then the values line
      UiInteract::Hit(id, ImVec2(w, h));
      const ImGuiID me = ImGui::GetItemID();
      const float x0 = p.x + hw, x1 = p.x + w - hw;
      const float barT = p.y + tok::space_1, barB = p.y + fh * 0.55f;
      const float triB = p.y + fh;
      auto xOf = [&](float v) { return x0 + std::clamp(v, 0.0f, 1.0f) * (x1 - x0); };
      auto vOf = [&](float x) { return std::clamp((x - x0) / std::max(1.0f, x1 - x0), 0.0f, 1.0f); };
      auto joined = [&](int pair) { return std::fabs(b[pair * 2] - b[pair * 2 + 1]) < 0.5f / 255.0f; };
      const ImGuiIO& io = ImGui::GetIO();
      const bool active = ImGui::IsItemActive() && sHeldId == me;
      bool changed = false;

      // The handle under the pointer (hover and grab): the nearest within reach; a split pair's shared spot picks
      // the half on the pointer's side.
      int under = -1;
      if (ImGui::IsItemHovered() || ImGui::IsItemActivated())
      {
         float best = hw + tok::space_1;
         for (int k = 0; k < 4; k++)
         {
            const float d = std::fabs(io.MousePos.x - xOf(b[k]));
            const bool sideOk = (k == 1 || k == 3) == (io.MousePos.x > xOf(b[k]));
            if (d < best - 0.01f || (std::fabs(d - best) < 0.5f && sideOk))
            {
               best = d;
               under = k;
            }
         }
      }
      if (ImGui::IsItemActivated())
      {
         sHeldId = me;
         sHeld = under;
         sPair = false;
         if (under >= 0)
         {
            const int pair = under / 2;
            sPair = joined(pair) && !io.KeyAlt;
            if (sPair)
               sHeld = pair * 2;
            sGrabDx = io.MousePos.x - xOf(b[sHeld]);
            PushUndoCheckpoint("Blend If");
         }
      }
      if (active && sHeld >= 0)
      {
         const float v = std::round(vOf(io.MousePos.x - sGrabDx) * 255.0f) / 255.0f;
         if (sPair)
         {
            const int k0 = sHeld, k1 = sHeld + 1;
            const float lo = k0 == 0 ? 0.0f : b[k0 - 1], hi = k1 == 3 ? 1.0f : b[k1 + 1];
            const float nv = std::clamp(v, lo, hi);
            if (nv != b[k0] || nv != b[k1])
            {
               b[k0] = b[k1] = nv;
               changed = true;
            }
         }
         else
         {
            const float lo = sHeld == 0 ? 0.0f : b[sHeld - 1], hi = sHeld == 3 ? 1.0f : b[sHeld + 1];
            const float nv = std::clamp(v, lo, hi);
            if (nv != b[sHeld])
            {
               b[sHeld] = nv;
               changed = true;
            }
         }
      }
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint("Blend If");
         b[0] = b[1] = 0.0f;
         b[2] = b[3] = 1.0f;
         changed = true;
      }
      // Right-click a handle takes away what it added: a split half folds back onto its partner (the outer one
      // stays), a joined pair goes back to its end (black to 0, white to 255).
      if (ImGui::IsItemHovered() && under >= 0 && MenuParts::RightPressed())
      {
         const int pair = under / 2, outer = pair == 0 ? 0 : 3, inner = pair == 0 ? 1 : 2;
         PushUndoCheckpoint("Blend If");
         if (!joined(pair))
            b[inner] = b[outer];
         else
            b[outer] = b[inner] = pair == 0 ? 0.0f : 1.0f;
         changed = true;
      }
      if ((ImGui::IsItemHovered() && under >= 0) || (active && sHeld >= 0))
      {
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
         if (!active)
            HelpTip("%s", T("Drag to move; Option-drag splits; right-click removes the split"));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float rr = tok::radius_control * 0.5f;
      dl->AddRectFilledMultiColor(ImVec2(x0, barT), ImVec2(x1, barB), tok::U32(tok::view2d_fill_shade_xs), tok::U32(tok::view2d_fill_shade_lg),
                                  tok::U32(tok::view2d_fill_shade_lg), tok::U32(tok::view2d_fill_shade_xs));
      // What does not pass: veiled outside the outer handles, fading across the soft ranges.
      const ImU32 veil = ImGui::GetColorU32(ImGuiCol_WindowBg, tok::view2d_veil_color_alpha_mul), clear = ImGui::GetColorU32(ImGuiCol_WindowBg, 0.0f);
      if (xOf(b[0]) > x0)
         dl->AddRectFilled(ImVec2(x0, barT), ImVec2(xOf(b[0]), barB), veil);
      if (xOf(b[1]) > xOf(b[0]))
         dl->AddRectFilledMultiColor(ImVec2(xOf(b[0]), barT), ImVec2(xOf(b[1]), barB), veil, clear, clear, veil);
      if (xOf(b[3]) > xOf(b[2]))
         dl->AddRectFilledMultiColor(ImVec2(xOf(b[2]), barT), ImVec2(xOf(b[3]), barB), clear, veil, veil, clear);
      if (xOf(b[3]) < x1)
         dl->AddRectFilled(ImVec2(xOf(b[3]), barT), ImVec2(x1, barB), veil);
      dl->AddRect(ImVec2(x0, barT), ImVec2(x1, barB), FormParts::EdgeCol(), rr);

      // Handles: a joined pair is one triangle; a split pair is two halves, each pointing at its own spot.
      const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text, tok::view2d_ink_color_alpha_mul_lg), lit = ImGui::GetColorU32(app::AccentEmphasisSelected());
      for (int pair = 0; pair < 2; pair++)
      {
         const ImU32 fill = pair == 0 ? tok::U32(tok::view2d_fill_shade_sm) : tok::U32(tok::view2d_fill_shade_md);
         for (int half = 0; half < 2; half++)
         {
            const int k = pair * 2 + half;
            if (joined(pair) && half == 1)
               break;
            const float x = xOf(b[k]);
            const bool on = (active && (sHeld == k || (sPair && sHeld == pair * 2))) || (!active && under == k) ||
                            (!active && joined(pair) && under / 2 == pair && under >= 0);
            ImVec2 t0(x, barB), t1(x - hw, triB), t2(x + hw, triB);
            if (!joined(pair))
            {
               // The half on the side away from its partner: black's "to" and white's outer point outwards.
               const bool right = half == 1;
               t1 = right ? ImVec2(x, triB) : ImVec2(x - hw, triB);
               t2 = right ? ImVec2(x + hw, triB) : ImVec2(x, triB);
            }
            dl->AddTriangleFilled(t0, t1, t2, fill);
            dl->AddTriangle(t0, t1, t2, on ? lit : ink, on ? tok::view2d_blend_if_ramp_stroke : 1.0f);
         }
      }

      // The values under the ends, compact: one number for a joined pair, "a / b" for a split one.
      auto val = [&](int pair) {
         char buf[24];
         const int a = (int)std::lround(b[pair * 2] * 255.0f), c = (int)std::lround(b[pair * 2 + 1] * 255.0f);
         if (a == c)
            snprintf(buf, sizeof buf, "%d", a);
         else
            snprintf(buf, sizeof buf, "%d / %d", a, c);
         return std::string(buf);
      };
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const std::string lv = val(0), rv = val(1);
      dl->AddText(ImVec2(x0 - hw * 0.5f, triB), dim, lv.c_str());
      dl->AddText(ImVec2(x1 + hw * 0.5f - ImGui::CalcTextSize(rv.c_str()).x, triB), dim, rv.c_str());

      if (ImGui::IsItemDeactivated())
      {
         // A half dropped onto its partner joins them again.
         if (sHeld >= 0 && !sPair)
         {
            const int pair = sHeld / 2;
            if (std::fabs(xOf(b[pair * 2]) - xOf(b[pair * 2 + 1])) < hw * 0.5f && !joined(pair))
            {
               b[pair * 2] = b[pair * 2 + 1] = b[sHeld];
               changed = true;
            }
         }
         sHeld = -1;
         sPair = false;
      }
      return changed;
   }
   // ui-content-end

   bool SliderRow(const char* label, float* v, float lo, float hi, const char* fmt)
   {
      fmt = RowSlot(label, fmt);
      ModBegin();
      const bool changed = FieldWell::Slider(FormParts::Id(label).c_str(), v, lo, hi, fmt);
      ModEnd();
      UndoOnActivate();
      if (changed)
         Touch();
      return changed;
   }

   // Curves: the graph is the control. Click adds a point, drag moves it (ends move up and down only),
   // drag an inner point off the graph (or double-click / right-click / Delete) to remove it.
   int sCurveDrag = -1;
   int sCurveSel = -1;
   int sCurveSelLayer = 0;
   int sCurveLayer = 0;
   uint64_t sCurveDoc = 0;         // the drag belongs to one layer of one document
   struct HistEntry
   {
      std::vector<float> bins;
      double at = -1.0;
      uint64_t rev = 0;
   };
   std::map<int, HistEntry> sHistCache;
   uint64_t sHistDoc = 0;
   // An adjustment's key (AdjExtras): an adjustment layer's id, or 100000 + layer id * 64 + effect index.
   void KeySource(int key, int& id, int& effect)
   {
      id = key;
      effect = -1;
      if (key >= 100000)
      {
         id = (key - 100000) / 64;
         effect = (key - 100000) % 64;
      }
   }
   const std::vector<float>& DocHistogram(Scene2DNode* doc, int key = 0)
   {
      static const std::vector<float> kEmpty;
      if (doc == nullptr)
         return kEmpty;
      if (sHistDoc != sDocUid)
      {
         sHistCache.clear();
         sHistDoc = sDocUid;
      }
      if (sHistCache.size() > tok::view2d_doc_histogram_limit)
         sHistCache.clear();
      HistEntry& e = sHistCache[key];
      const double now = ImGui::GetTime();
      const uint64_t rev = doc->TextureRevision();
      // Never trigger a GPU readback while the user is actively dragging a curve or slider; refresh only when
      // the cache is cold or the document actually changed and at least 0.5 s have elapsed.
      const bool cold = e.at < 0.0;
      const bool stale = !ImGui::IsAnyItemActive() && rev != e.rev && (now - e.at > 0.5);
      if (cold || stale)
      {
         e.at = now;
         e.rev = rev;
         int id = 0, eff = -1;
         KeySource(key, id, eff);
         if (!doc->Histogram(id, e.bins, 64, eff))
            e.bins.clear();
      }
      return e.bins;
   }
   void HistogramUnder(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const std::vector<float>& bins)
   {
      const int n = (int)bins.size();
      const ImU32 col = ImGui::GetColorU32(ImGuiCol_Border, tok::view2d_histogram_under_color_alpha_mul);
      for (int i = 0; i < n; i++)
      {
         const float x0 = p0.x + (p1.x - p0.x) * i / n, x1 = p0.x + (p1.x - p0.x) * (i + 1) / n;
         dl->AddRectFilled(ImVec2(x0, p1.y - (p1.y - p0.y) * bins[i]), ImVec2(x1, p1.y), col);
      }
   }

   ImU32 CurveChannelColor(int channel, float alpha = 1.0f)
   {
      if (channel == 1) return ImGui::ColorConvertFloat4ToU32(ImVec4(
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_red)).x,
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_red)).y,
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_red)).z, alpha));
      if (channel == 2) return ImGui::ColorConvertFloat4ToU32(ImVec4(
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_green)).x,
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_green)).y,
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_green)).z, alpha));
      if (channel == 3) return ImGui::ColorConvertFloat4ToU32(ImVec4(
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_blue)).x,
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_blue)).y,
         ImGui::ColorConvertU32ToFloat4(tok::U32(tok::label_blue)).z, alpha));
      return ImGui::GetColorU32(ImGuiCol_Text, alpha);
   }

   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   void CurveEditor(std::vector<float>& c, int key, const std::vector<float>* hist = nullptr, int channel = 0,
                    const std::vector<float>* const* otherCh = nullptr)
   {
      if (c.size() < tok::view2d_curve_editor_limit)
         c = { 0.0f, 0.0f, 1.0f, 1.0f };
      const float w = Ui::AvailW(), h = std::min(w, tok::view2d_curve_max);
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      UiInteract::Hit("##curve", ImVec2(w, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 p1(p0.x + w, p0.y + h);
      dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), tok::radius_control);
      if (hist != nullptr && !hist->empty())
         HistogramUnder(dl, p0, p1, *hist);
      // 4x4 grid + subtle diagonal identity reference line.
      const ImU32 gridCol = ImGui::GetColorU32(ImGuiCol_Border, tok::view2d_grid_col_color_alpha_mul);
      for (int i = 1; i < 4; i++)
      {
         const float fx = p0.x + w * i / 4.0f, fy = p0.y + h * i / 4.0f;
         dl->AddLine(ImVec2(fx, p0.y), ImVec2(fx, p1.y), gridCol);
         dl->AddLine(ImVec2(p0.x, fy), ImVec2(p1.x, fy), gridCol);
      }
      dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p0.y), ImGui::GetColorU32(ImGuiCol_Border, tok::view2d_curve_editor_color_alpha_mul_sm), tok::view2d_curve_editor_stroke);
      const auto ToScreen = [&](float x, float y) { return ImVec2(p0.x + x * w, p1.y - y * h); };
      const ImVec2 m = ImGui::GetIO().MousePos;
      const float mx = std::clamp((m.x - p0.x) / std::max(1.0f, w), 0.0f, 1.0f);
      const float my = std::clamp((p1.y - m.y) / std::max(1.0f, h), 0.0f, 1.0f);
      const int n = (int)c.size() / tok::view2d_n_ratio;
      int near = -1;
      float bestD2 = 10.0f * 10.0f;   // 10 px hit radius, pick closest point
      for (int i = 0; i < n; i++)
      {
         const ImVec2 q = ToScreen(c[i * 2], c[i * 2 + 1]);
         const float dx = q.x - m.x, dy = q.y - m.y, d2 = dx * dx + dy * dy;
         if (d2 < bestD2)
         {
            bestD2 = d2;
            near = i;
         }
      }
      bool changed = false;
      const bool draggingThis = ImGui::IsItemActive() && sCurveLayer == key && sCurveDoc == sDocUid && sCurveDrag >= 0 && sCurveDrag < n;
      const bool pullOff = draggingThis && sCurveDrag > 0 && sCurveDrag < n - 1 &&
                           (m.x < p0.x - tok::space_5 || m.x > p1.x + tok::space_5 || m.y < p0.y - tok::space_5 || m.y > p1.y + tok::space_5);
      if (ImGui::IsItemHovered() && (ImGui::IsMouseDoubleClicked(0) || MenuParts::RightPressed()) && near > 0 && near < n - 1)
      {
         PushUndoCheckpoint("Curves");
         c.erase(c.begin() + near * 2, c.begin() + near * 2 + 2);
         sCurveDrag = -1;
         if (sCurveSelLayer == key)
            sCurveSel = -1;
         changed = true;
      }
      else if (ImGui::IsItemActivated())
      {
         if (ImGui::GetIO().KeyAlt)
         {
            PushUndoCheckpoint("Reset curve");
            c = { 0.0f, 0.0f, 1.0f, 1.0f };
            sCurveDrag = -1;
            if (sCurveSelLayer == key)
               sCurveSel = -1;
            changed = true;
         }
         else
         {
            PushUndoCheckpoint("Curves");
            sCurveLayer = key;
            sCurveDoc = sDocUid;
            sCurveDrag = near;
            if (near < 0 && n < 16)
            {
               int at = 1;
               while (at < n - 1 && c[at * 2] < mx)
                  at++;
               c.insert(c.begin() + at * 2, { mx, my });
               sCurveDrag = at;
               changed = true;
            }
            sCurveSel = sCurveDrag;
            sCurveSelLayer = key;
         }
      }
      if (ImGui::IsItemActive() && sCurveLayer == key && sCurveDoc == sDocUid && sCurveDrag >= 0 && sCurveDrag < (int)c.size() / tok::view2d_curve_editor_ratio)
      {
         const int i = sCurveDrag, last = (int)c.size() / tok::view2d_last_ratio - 1;
         if (!pullOff)
         {
            float x = mx;
            if (i == 0)
               x = std::clamp(x, 0.0f, std::max(0.0f, c[2] - tok::view2d_curve_editor_gap));
            else if (i == last)
               x = std::clamp(x, std::min(1.0f, c[(last - 1) * 2] + tok::view2d_curve_editor_gap), 1.0f);
            else
               x = std::clamp(x, c[(i - 1) * 2] + tok::view2d_curve_editor_lo, c[(i + 1) * 2] - tok::view2d_curve_editor_lo);
            if (x != c[i * 2] || my != c[i * 2 + 1])
            {
               c[i * 2] = x;
               c[i * 2 + 1] = my;
               changed = true;
            }
         }
      }
      else if (ImGui::IsItemDeactivated() && sCurveLayer == key && sCurveDoc == sDocUid)
      {
         if (sCurveDrag > 0 && sCurveDrag < (int)c.size() / tok::view2d_curve_editor_ratio - 1 &&
             (m.x < p0.x - tok::space_5 || m.x > p1.x + tok::space_5 || m.y < p0.y - tok::space_5 || m.y > p1.y + tok::space_5))
         {
            c.erase(c.begin() + sCurveDrag * 2, c.begin() + sCurveDrag * 2 + 2);
            if (sCurveSelLayer == key)
               sCurveSel = -1;
            changed = true;
         }
         sCurveDrag = -1;
      }
      // Delete / Backspace removes the selected inner point when the graph is hovered.
      if (ImGui::IsItemHovered() && sCurveSelLayer == key && sCurveSel > 0 && sCurveSel < (int)c.size() / tok::view2d_curve_editor_ratio - 1 &&
          (Keymap::KeyPressed(Keymap::Act::Delete, true) || Keymap::KeyPressed(Keymap::Act::DeleteAlt, true)))
      {
         PushUndoCheckpoint("Curves");
         c.erase(c.begin() + sCurveSel * 2, c.begin() + sCurveSel * 2 + 2);
         sCurveSel = -1;
         changed = true;
      }
      // Faint ghost previews for other non-default channels so multi-channel grades remain readable.
      if (otherCh != nullptr)
      {
         for (int ch = 0; ch < 3; ch++)
         {
            if (ch + 1 == channel || otherCh[ch] == nullptr)
               continue;
            const std::vector<float>& oc = *otherCh[ch];
            if (oc.size() == 4 && oc[0] == 0.0f && oc[1] == 0.0f && oc[2] == 1.0f && oc[3] == 1.0f)
               continue;
            float glut[64];
            Scene2DNode::CurveLut(oc, glut, 64);
            ImVec2 gline[64];
            for (int i = 0; i < 64; i++)
               gline[i] = ToScreen(i / 63.0f, glut[i]);
            dl->AddPolyline(gline, tok::view2d_line_num_points_lg, CurveChannelColor(ch + 1, tok::view2d_curve_channel_color_alpha), 0, 1.0f);
         }
      }
      constexpr int kSamples = 128;
      float lut[kSamples];
      Scene2DNode::CurveLut(c, lut, kSamples);
      ImVec2 line[kSamples];
      for (int i = 0; i < kSamples; i++)
         line[i] = ToScreen(i / (float)(kSamples - 1), lut[i]);
      const ImU32 curveCol = CurveChannelColor(channel, 1.0f);
      dl->AddPolyline(line, kSamples, curveCol, 0, tok::view2d_line_stroke_md);
      const int curN = (int)c.size() / tok::view2d_cur_n_ratio;
      for (int i = 0; i < curN; i++)
      {
         const ImVec2 q = ToScreen(c[i * 2], c[i * 2 + 1]);
         const bool activePt = (sCurveLayer == key && sCurveDoc == sDocUid && i == sCurveDrag);
         const bool selPt = (sCurveSelLayer == key && i == sCurveSel);
         const bool hovPt = ImGui::IsItemHovered() && i == near;
         const float r = (activePt || hovPt) ? tok::view2d_r : tok::view2d_curve_editor_r;
         const ImU32 ptCol = (activePt && pullOff) ? ImGui::GetColorU32(ImGuiCol_TextDisabled, tok::view2d_pt_col_color_alpha_mul) : curveCol;
         dl->AddCircleFilled(q, r, ptCol);
         if (selPt || activePt)
            dl->AddCircle(q, r + tok::space_0_5, ImGui::GetColorU32(ImGuiCol_Text, tok::view2d_curve_editor_color_alpha_mul_lg), 0, tok::view2d_ring_stroke_sm);
      }
      dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), tok::radius_control);
      // Compact live In / Out coordinate readout when hovering or dragging.
      const int showIdx = (sCurveLayer == key && sCurveDoc == sDocUid && sCurveDrag >= 0 && sCurveDrag < curN) ? sCurveDrag
                        : (ImGui::IsItemHovered() && near >= 0 && near < curN) ? near
                        : (sCurveSelLayer == key && sCurveSel >= 0 && sCurveSel < curN) ? sCurveSel : -1;
      if (showIdx >= 0)
      {
         const int inV = (int)std::lround(c[showIdx * 2] * 255.0f);
         const int outV = (int)std::lround(c[showIdx * 2 + 1] * 255.0f);
         if (pullOff)
            ImGui::TextDisabled("%s", T("Release to remove point"));
         else
            ImGui::TextDisabled(T("In %d   Out %d"), inV, outV);
      }
      else if (ImGui::IsItemHovered())
      {
         ImGui::TextDisabled("%s", T("Click to add, drag off or right-click to remove"));
      }
      if (changed)
         Touch();
   }
   // ui-content-end

   const char* AdjAmountLabel(int t)
   {
      using S = Scene2DNode;
      // The slider's own meaning where it is the value itself; "Amount" only where it mixes the look in.
      switch (t)
      {
      case S::kAdjLevels: return "Gamma";
      case S::kAdjPhotoFilter: return "Density";
      case S::kAdjShadowsHighlights: return "Radius";
      case S::kAdjPixelate: case S::kAdjHalftone: return "Size";
      case S::kAdjDither: case S::kAdjPosterize: return "Levels";
      case S::kAdjThreshold: return "Level";
      case S::kAdjBrightness: return "Brightness";
      case S::kAdjContrast: return "Contrast";
      case S::kAdjHue: return "Shift";
      case S::kAdjSaturation: return "Saturation";
      case S::kAdjExposure: return "Exposure";
      case S::kAdjVibrance: return "Vibrance";
      default: return "Amount";
      }
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
      bool on = *v;
      if (!FormParts::Check(label, &on))
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
      // One undo step per second of dragging in one row; another row always starts its own step.
      static double sLast = -1.0;
      static ImGuiID sLastRow = 0;
      const ImGuiID row = ImGui::GetID(label);
      if (changed && (row != sLastRow || ImGui::GetTime() - sLast > tok::motion_s_undo_merge))
         PushUndoCheckpoint(kUndo);
      if (changed)
      {
         sLast = ImGui::GetTime();
         sLastRow = row;
         Touch();
      }
      return changed;
   }

   // A gradient's stops on one bar (Affinity's and Photoshop's gradient editor): click the bar to add a stop, drag a
   // marker to move it, drag it down off the bar or right-click it to remove it (two always stay). The picked stop's
   // colour, alpha and location sit below. Empty stops start from the two colours a and b.
   std::map<ImGuiID, int> sStopPick;
   int sStopDrag = -1;
   ImGuiID sStopDragId = 0;
   bool GradientRows(const char* id, std::vector<Scene2DNode::GradStop>& owner, const float a[4], const float b[4])
   {
      using G = Scene2DNode::GradStop;
      bool changed = false;
      // Edited on a copy, so a gradient nobody touched keeps no stops of its own.
      std::vector<G> st = owner;
      if (st.size() < tok::view2d_gradient_rows_limit)
      {
         G s0, s1;
         std::copy(a, a + 4, s0.c);
         std::copy(b, b + 4, s1.c);
         s1.t = 1.0f;
         st = { s0, s1 };
      }
      ImGui::PushID(id);
      const ImGuiID key = ImGui::GetID("bar");
      int& pick = sStopPick[key];
      pick = std::clamp(pick, 0, (int)st.size() - 1);
      FieldLabel(T("Gradient"));
      const float w = sFieldW, h = tok::view2d_h, mk = tok::view2d_mk;
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      UiInteract::Hit("##bar", ImVec2(w, h + mk + tok::space_1));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      auto u32 = [](const float c[4]) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], 1.0f)); };
      const float fx0 = p0.x, fx1 = p0.x + w;
      dl->AddRectFilled(p0, ImVec2(fx0 + w * st.front().t, p0.y + h), u32(st.front().c));
      for (size_t i = 1; i < st.size(); i++)
      {
         const float x0 = fx0 + w * st[i - 1].t, x1 = fx0 + w * st[i].t;
         dl->AddRectFilledMultiColor(ImVec2(x0, p0.y), ImVec2(x1, p0.y + h), u32(st[i - 1].c), u32(st[i].c), u32(st[i].c),
                                     u32(st[i - 1].c));
      }
      dl->AddRectFilled(ImVec2(fx0 + w * st.back().t, p0.y), ImVec2(fx1, p0.y + h), u32(st.back().c));
      dl->AddRect(p0, ImVec2(fx1, p0.y + h), ImGui::GetColorU32(ImGuiCol_Border));
      for (int i = 0; i < (int)st.size(); i++)
      {
         const float x = fx0 + w * st[(size_t)i].t, y = p0.y + h + 1.0f;
         dl->AddTriangleFilled(ImVec2(x, y), ImVec2(x - mk * tok::view2d_gradient_rows_ratio, y + mk), ImVec2(x + mk * tok::view2d_gradient_rows_ratio, y + mk), u32(st[(size_t)i].c));
         dl->AddTriangle(ImVec2(x, y), ImVec2(x - mk * tok::view2d_gradient_rows_ratio, y + mk), ImVec2(x + mk * tok::view2d_gradient_rows_ratio, y + mk),
                         ImGui::GetColorU32(i == pick ? ImGuiCol_Text : ImGuiCol_Border), i == pick ? tok::view2d_gradient_rows_stroke : 1.0f);
      }
      const ImVec2 m = ImGui::GetIO().MousePos;
      const float mt = std::clamp((m.x - fx0) / w, 0.0f, 1.0f);
      auto nearest = [&]() {
         int best = -1;
         float bd = 6.0f;
         for (int i = 0; i < (int)st.size(); i++)
         {
            const float d = std::fabs(fx0 + w * st[(size_t)i].t - m.x);
            if (d < bd) { bd = d; best = i; }
         }
         return best;
      };
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint(kUndo);
         int hit = nearest();
         if (hit < 0 && (int)st.size() < Scene2DNode::kMaxStops)
         {
            G g;
            g.t = mt;
            Scene2DNode::GradAt(st, a, b, mt, g.c);
            st.push_back(g);
            hit = (int)st.size() - 1;
            changed = true;
         }
         if (hit >= 0)
         {
            sStopDrag = hit;
            sStopDragId = key;
            pick = hit;
         }
      }
      if (MenuParts::ContextClicked() && st.size() > tok::view2d_gradient_rows_limit)
      {
         const int hit = nearest();
         if (hit >= 0)
         {
            PushUndoCheckpoint(kUndo);
            st.erase(st.begin() + hit);
            pick = 0;
            changed = true;
         }
      }
      if (sStopDragId == key && sStopDrag >= 0 && sStopDrag < (int)st.size())
      {
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            if (st[(size_t)sStopDrag].t != mt)
            {
               st[(size_t)sStopDrag].t = mt;
               changed = true;
            }
         }
         else
         {
            // Released well below the bar: the stop goes (Photoshop's drag-off).
            if (m.y > p0.y + h + mk + 24.0f && st.size() > tok::view2d_gradient_rows_limit)
            {
               st.erase(st.begin() + sStopDrag);
               changed = true;
            }
            sStopDrag = -1;
            sStopDragId = 0;
         }
      }
      // Keep the picked stop picked while the list re-sorts.
      G picked = st[(size_t)std::clamp(pick, 0, (int)st.size() - 1)];
      const bool dragging = sStopDragId == key && sStopDrag >= 0;
      Scene2DNode::SortStops(st);
      for (int i = 0; i < (int)st.size(); i++)
         if (st[(size_t)i] == picked)
         {
            if (dragging && sStopDrag == pick)
               sStopDrag = i;
            pick = i;
            break;
         }
      G& g = st[(size_t)pick];
      changed |= ColorRow(T("Stop"), g.c);
      changed |= SliderRow(T("Stop alpha"), &g.c[3], 0.0f, 1.0f, "%.2f");
      float loc = g.t * 100.0f;
      if (SliderRow(T("Location"), &loc, params::view2d::loc_lo, params::view2d::loc_hi, "%.0f%%"))
      {
         g.t = loc / 100.0f;
         changed = true;
      }
      ImGui::PopID();
      if (changed)
      {
         Scene2DNode::SortStops(st);
         owner = st;
         Touch();
      }
      return changed;
   }

   // The rows an adjustment has besides its amount: Levels and Curves per channel (with the picture's histogram and
   // Auto), and the settings of the newer adjustments. T is a Layer or an Effect: both hold the same fields.
   std::map<int, int> sAdjChMap;   // per-adjustment channel selector: 0 master RGB, 1..3 red, green, blue
   int& AdjChannel(int key) { return sAdjChMap[key]; }
   // Curves / Hue picked on the picture (Photoshop's hand): armed for one adjustment key; a drag on the canvas then moves
   // the curve point at the clicked tone up and down, or turns the hue left and right.
   struct AdjPick { int key = 0; uint64_t doc = 0; } sAdjPick;
   // Runs f on the adjustment fields an adjustment key names (a Layer or an Effect); false when it is gone.
   template <class F> bool WithAdjKey(Scene2DNode* doc, int key, F f)
   {
      int id = 0, eff = -1;
      KeySource(key, id, eff);
      Layer* l = doc != nullptr ? doc->FindLayer(id) : nullptr;
      if (l == nullptr)
         return false;
      if (eff < 0)
         f(*l);
      else if (eff < (int)l->effects.size())
         f(l->effects[(size_t)eff]);
      else
         return false;
      return true;
   }
   // The y of a curve at x, and the point at x (added when none is near).
   float CurveAt(const std::vector<float>& c, float x)
   {
      float lut[256];
      Scene2DNode::CurveLut(c, lut, 256);
      return lut[std::clamp((int)(x * 255.0f + 0.5f), 0, 255)];
   }
   int CurvePointAt(std::vector<float>& c, float x)
   {
      const int n = (int)c.size() / tok::view2d_n_ratio;
      for (int i = 0; i < n; i++)
         if (std::fabs(c[(size_t)i * 2] - x) < 0.03f)  // ui-audit-ok: model (curve point pick tolerance, in curve units 0-1)
            return i;
      if (n >= 16)
         return -1;
      const float y = CurveAt(c, x);
      int at = 1;
      while (at < n - 1 && c[(size_t)at * 2] < x)
         at++;
      c.insert(c.begin() + at * 2, { x, y });
      return at;
   }
   int sAdjTone = 1;        // Colour Balance: shadows, midtones, highlights
   int sAdjRange = 0;       // Selective Colour: which colours
   int sAdjOut = 0;         // Channel Mixer: which output
   template <class A> void AdjExtras(A& x, int key, Scene2DNode* doc)
   {
      using S = Scene2DNode;
      const int t = x.adjType;
      const int np = S::AdjParamCount(t);
      if ((int)x.adjP.size() < np)
      {
         const std::vector<float> def = S::AdjParamDefault(t);
         for (int i = (int)x.adjP.size(); i < np; i++)
            x.adjP.push_back(def[(size_t)i]);
      }
      float* p = x.adjP.data();
      int& adjCh = AdjChannel(key);
      if (t == S::kAdjLevels || t == S::kAdjCurves)
      {
         ComboRow(T("Channel"), &adjCh, { T("RGB"), T("Red"), T("Green"), T("Blue") });
         adjCh = std::clamp(adjCh, 0, 3);
      }
      if (t == S::kAdjLevels)
      {
         float* lv = adjCh == 0 ? x.levels : x.levelsCh[adjCh - 1];
         const std::vector<float>& h = DocHistogram(doc, key);
         {
            const float w = Ui::AvailW();
            const float histH = tok::view2d_hist_h, mkH = tok::view2d_mk_h;
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            UiInteract::Hit((std::string("##v2dlvh") + std::to_string(key)).c_str(), ImVec2(w, histH + mkH + tok::space_0_5));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 hp1(p0.x + w, p0.y + histH);
            dl->AddRectFilled(p0, hp1, ImGui::GetColorU32(ImGuiCol_FrameBg), tok::radius_control);
            if (!h.empty())
               HistogramUnder(dl, p0, hp1, h);
            dl->AddRect(p0, hp1, ImGui::GetColorU32(ImGuiCol_Border), tok::radius_control);
            // Interactive Black / Gamma / White triangle handles beneath the histogram.
            const float span = std::max(0.01f, lv[1] - lv[0]);
            const float gPos = adjCh == 0 ? std::clamp(lv[0] + span * std::pow(tok::view2d_pos_extras, std::clamp(x.adjAmt, tok::view2d_pos_lo, tok::view2d_pos_hi)), lv[0] + tok::view2d_adj_extras_gap, lv[1] - tok::view2d_adj_extras_gap)
                                          : (lv[0] + lv[1]) * 0.5f;
            const float hx[3] = { p0.x + lv[0] * w, p0.x + gPos * w, p0.x + lv[1] * w };
            const int nHandles = adjCh == 0 ? 3 : 2;
            const int idxMap[3] = { 0, adjCh == 0 ? 1 : 2, 2 };
            static int sLvDragKey = 0, sLvDragIdx = -1;
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const float mt = std::clamp((mp.x - p0.x) / std::max(1.0f, w), 0.0f, 1.0f);
            if (ImGui::IsItemActivated())
            {
               int best = -1;
               float bestD = 12.0f;
               for (int k = 0; k < nHandles; k++)
               {
                  const int hi = idxMap[k];
                  const float d = std::fabs(hx[hi] - mp.x);
                  if (d < bestD) { bestD = d; best = hi; }
               }
               if (best >= 0)
               {
                  PushUndoCheckpoint("Levels");
                  sLvDragKey = key;
                  sLvDragIdx = best;
               }
            }
            if (ImGui::IsItemActive() && sLvDragKey == key && sLvDragIdx >= 0)
            {
               if (sLvDragIdx == 0)
                  lv[0] = std::clamp(mt, 0.0f, std::max(0.0f, lv[1] - 0.02f));
               else if (sLvDragIdx == 2)
                  lv[1] = std::clamp(mt, std::min(1.0f, lv[0] + 0.02f), 1.0f);
               else if (sLvDragIdx == 1 && adjCh == 0)
               {
                  const float u = std::clamp((mt - lv[0]) / std::max(0.02f, lv[1] - lv[0]), 0.02f, 0.98f);
                  x.adjAmt = std::clamp(std::log(u) / std::log(0.5f), 0.1f, 4.0f);
               }
               Touch();
            }
            else if (ImGui::IsItemDeactivated() && sLvDragKey == key)
               sLvDragIdx = -1;
            for (int k = 0; k < nHandles; k++)
            {
               const int hi = idxMap[k];
               const float cx = hx[hi], cy = hp1.y + 1.0f;
               const ImU32 fill = hi == 0 ? ImGui::GetColorU32(ImGuiCol_FrameBg)
                                : hi == 2 ? ImGui::GetColorU32(ImGuiCol_Text)
                                          : ImGui::GetColorU32(ImGuiCol_TextDisabled);
               dl->AddTriangleFilled(ImVec2(cx, cy), ImVec2(cx - tok::space_1_25, cy + mkH), ImVec2(cx + tok::space_1_25, cy + mkH), fill);
               dl->AddTriangle(ImVec2(cx, cy), ImVec2(cx - tok::space_1_25, cy + mkH), ImVec2(cx + tok::space_1_25, cy + mkH),
                               ImGui::GetColorU32(sLvDragKey == key && sLvDragIdx == hi ? ImGuiCol_Text : ImGuiCol_Border), tok::view2d_adj_extras_stroke);
            }
         }
         SliderRow(T("In Black"), &lv[0], 0.0f, 1.0f, "%.2f");
         SliderRow(T("In White"), &lv[1], 0.0f, 1.0f, "%.2f");
         SliderRow(T("Out Black"), &lv[2], 0.0f, 1.0f, "%.2f");
         SliderRow(T("Out White"), &lv[3], 0.0f, 1.0f, "%.2f");
         float autoLv[3][4];
         if (ActionButton::Draw((std::string(T("Auto")) + "##v2dauto" + std::to_string(key)).c_str(), ImVec2(-1.0f, 0.0f), ActionButton::Kind::Plain) &&
             doc != nullptr && [&]() { int id = 0, eff = -1; KeySource(key, id, eff); return doc->AutoLevels(id, autoLv, eff); }())
         {
            PushUndoCheckpoint("Auto levels");
            for (int ch = 0; ch < 3; ch++)
               std::copy(autoLv[ch], autoLv[ch] + 4, x.levelsCh[ch]);
            Touch();
         }
      }
      if (t == S::kAdjCurves)
      {
         FieldLabel(T("Preset"));
         ImGui::SetNextItemWidth(sFieldW);
         if (MenuParts::BeginCombo((std::string("##v2dcrvpre") + std::to_string(key)).c_str(), T("Choose...")))
         {
            struct CurvePreset { const char* name; std::vector<float> pts; };
            static const CurvePreset kCurvePresets[] = {
               { "Linear",          { 0.0f, 0.0f, 1.0f, 1.0f } },
               { "S-Curve",         { 0.0f, 0.0f, 0.25f, 0.18f, 0.75f, 0.82f, 1.0f, 1.0f } },
               { "Strong Contrast", { 0.0f, 0.0f, 0.22f, 0.10f, 0.78f, 0.90f, 1.0f, 1.0f } },
               { "Fade Blacks",     { 0.0f, 0.12f, 0.30f, 0.30f, 1.0f, 1.0f } },
               { "Lift Shadows",    { 0.0f, 0.0f, 0.25f, 0.36f, 0.75f, 0.78f, 1.0f, 1.0f } },
               { "Invert",          { 0.0f, 1.0f, 1.0f, 0.0f } },
            };
            for (const CurvePreset& cp : kCurvePresets)
               if (MenuParts::Choice(T(cp.name), false))
               {
                  PushUndoCheckpoint("Curve preset");
                  (adjCh == 0 ? x.curve : x.curveCh[adjCh - 1]) = cp.pts;
                  Touch();
               }
            MenuParts::EndCombo();
         }
         const std::vector<float>* ghosts[3] = { &x.curveCh[0], &x.curveCh[1], &x.curveCh[2] };
         CurveEditor(adjCh == 0 ? x.curve : x.curveCh[adjCh - 1], key * 4 + adjCh, &DocHistogram(doc, key), adjCh, ghosts);
      }
      if (t == S::kAdjCurves || t == S::kAdjHue)
      {
         const bool armed = sAdjPick.key == key && sAdjPick.doc == sDocUid;
         if (ActionButton::Draw((std::string(armed ? T("Click the picture") : T("Pick on picture")) + "##v2dadjpick" + std::to_string(key)).c_str(),
                                ImVec2(-1.0f, 0.0f), armed ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
            sAdjPick = armed ? AdjPick() : AdjPick{ key, sDocUid };
      }
      if (t == S::kAdjColorBalance)
      {
         ComboRow(T("Tone"), &sAdjTone, { T("Shadows"), T("Midtones"), T("Highlights") });
         sAdjTone = std::clamp(sAdjTone, 0, 2);
         float* q = p + sAdjTone * 3;
         SliderRow(T("Red"), &q[0], -1.0f, 1.0f, "%.2f");
         SliderRow(T("Green"), &q[1], -1.0f, 1.0f, "%.2f");
         SliderRow(T("Blue"), &q[2], -1.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjSelective)
      {
         ComboRow(T("Colours"), &sAdjRange, { T("Reds"), T("Yellows"), T("Greens"), T("Cyans"), T("Blues"), T("Magentas"),
                                             T("Whites"), T("Neutrals"), T("Blacks") });
         sAdjRange = std::clamp(sAdjRange, 0, 8);
         float* q = p + sAdjRange * 4;
         SliderRow(T("Cyan"), &q[0], -1.0f, 1.0f, "%.2f");
         SliderRow(T("Magenta"), &q[1], -1.0f, 1.0f, "%.2f");
         SliderRow(T("Yellow"), &q[2], -1.0f, 1.0f, "%.2f");
         SliderRow(T("Black"), &q[3], -1.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjMixer)
      {
         bool mono = p[9] > 0.5f;
         if (BoolRow(T("Monochrome"), &mono))
            p[9] = mono ? 1.0f : 0.0f;
         if (!mono)
         {
            ComboRow(T("Output"), &sAdjOut, { T("Red"), T("Green"), T("Blue") });
            sAdjOut = std::clamp(sAdjOut, 0, 2);
         }
         float* q = p + (mono ? 0 : sAdjOut) * 3;
         SliderRow(T("Red"), &q[0], params::view2d::q_0_lo, params::view2d::q_0_hi, "%.2f");
         SliderRow(T("Green"), &q[1], params::view2d::q_1_lo, params::view2d::q_1_hi, "%.2f");
         SliderRow(T("Blue"), &q[2], params::view2d::q_2_lo, params::view2d::q_2_hi, "%.2f");
      }
      if (t == S::kAdjBlackWhite)
      {
         static const char* const kHues[6] = { "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas" };
         for (int i = 0; i < 6; i++)
            SliderRow(T(kHues[i]), &p[i], 0.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjPhotoFilter)
      {
         ColorRow(T("Filter"), x.colorA);
         bool keep = p[0] > 0.5f;
         if (BoolRow(T("Keep lightness"), &keep))
            p[0] = keep ? 1.0f : 0.0f;
      }
      if (t == S::kAdjShadowsHighlights)
      {
         SliderRow(T("Shadows"), &p[0], 0.0f, 1.0f, "%.2f");
         SliderRow(T("Highlights"), &p[1], 0.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjWhiteBalance)
      {
         SliderRow(T("Temp"), &p[0], -1.0f, 1.0f, "%.2f");
         SliderRow(T("Tint"), &p[1], -1.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjSplitToning)
      {
         SliderRow(T("Hi Hue"), &p[0], params::view2d::p_0_lo, params::view2d::p_0_hi, "%.0f°");
         SliderRow(T("Hi Sat"), &p[1], 0.0f, 1.0f, "%.2f");
         SliderRow(T("Shad Hue"), &p[2], params::view2d::p_2_lo, params::view2d::p_2_hi, "%.0f°");
         SliderRow(T("Shad Sat"), &p[3], 0.0f, 1.0f, "%.2f");
         SliderRow(T("Balance"), &p[4], 0.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjRecolour)
      {
         SliderRow(T("Hue"), &p[0], params::view2d::p_0_lo, params::view2d::p_0_hi, "%.0f°");
         SliderRow(T("Saturation"), &p[1], 0.0f, 1.0f, "%.2f");
         SliderRow(T("Lightness"), &p[2], -1.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjNormals)
      {
         SliderRow(T("Rotation"), &p[0], params::view2d::p_0_2_lo, params::view2d::p_0_2_hi, "%.0f°");
         SliderRow(T("Scale"), &p[1], params::view2d::p_1_lo, params::view2d::p_1_hi, "%.2f");
         bool fx = p[2] > 0.5f, fy = p[3] > 0.5f;
         if (BoolRow(T("Flip X"), &fx))
            p[2] = fx ? 1.0f : 0.0f;
         if (BoolRow(T("Flip Y"), &fy))
            p[3] = fy ? 1.0f : 0.0f;
      }
      if (t == S::kAdjToneCompression)
      {
         SliderRow(T("Exposure"), &p[0], params::view2d::p_0_3_lo, params::view2d::p_0_3_hi, "%.2f");
         SliderRow(T("Gamma"), &p[1], params::view2d::p_1_lo, params::view2d::p_1_hi, "%.2f");
         SliderRow(T("Colour"), &p[2], 0.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjToneStretch)
      {
         SliderRow(T("Stretch"), &p[0], 0.0f, 1.0f, "%.3f");
         SliderRow(T("Gamma"), &p[1], params::view2d::p_1_lo, params::view2d::p_1_hi, "%.2f");
         SliderRow(T("Compression"), &p[2], 0.0f, 1.0f, "%.2f");
      }
      if (t == S::kAdjLookup)
      {
         FieldLabel(T("Lookup"));
         const std::string shown = x.adjFile.empty() ? std::string(T("None")) : x.adjFile.substr(x.adjFile.find_last_of("/\\") + 1);
         if (ActionButton::Draw((shown + "##v2dcube" + std::to_string(key)).c_str(), ImVec2(sFieldW, 0.0f), ActionButton::Kind::Plain))
         {
            const std::vector<std::string> got = Platform::OpenFilesDialog(T("Colour lookup"), { "cube" });
            if (!got.empty())
            {
               PushUndoCheckpoint("Colour lookup");
               x.adjFile = got[0];
               Touch();
            }
         }
      }
   }
   // A node picker over every node that can be a layer; 0 = none (when allowed).
   // A node row as a Canvas Link field (choose from the list, pick on the canvas, show). A pick lands later, from
   // the canvas, so it is parked under the row's key and applied the next time the row draws.
   struct RowPick { std::string key; uint64_t uid = 0; bool has = false; } sRowPick;
   bool NodeRow(const char* label, uint64_t* uid, bool allowNone, const std::string& key)
   {
      FieldLabel(label);
      CanvasLink::Spec spec;
      spec.owner = "v2d.row." + key;
      spec.kind = T("picture node");
      const uint64_t docUid = sDocUid;
      spec.accepts = [docUid](const GraphNode& gn)
      {
         const GraphNode* dgn = FindNodeByUid(docUid);
         const auto* doc = dgn != nullptr ? dynamic_cast<const Scene2DNode*>(dgn->node.get()) : nullptr;
         return gn.uid != docUid && View2DCanAdd(gn) && !WouldCreateDocCycle(doc, gn.node.get());
      };
      spec.onPicked = [key](uint64_t u) { sRowPick = { key, u, true }; };
      CanvasLink::Field(FormParts::Id(label).c_str(), *uid, spec, allowNone, sFieldW);
      if (!sRowPick.has || sRowPick.key != key)
         return false;
      sRowPick.has = false;
      if (sRowPick.uid == *uid)
         return false;
      PushUndoCheckpoint(kUndo);
      *uid = sRowPick.uid;
      Touch();
      return true;
   }

   // 3x3 anchor picker (row major from top left).
   bool AnchorRow(const char* label, int* anchor)
   {
      // The label sits on the grid's centre line, as every other label sits on its field's.
      const float cell = tok::view2d_cell;
      const ImVec2 row = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + std::round((cell * tok::view2d_anchor_row_ratio - ImGui::GetFrameHeight()) * 0.5f)));
      FieldLabel(label);
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, row.y));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      bool changed = false;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      UiInteract::Hit(FormParts::Id(label).c_str(), ImVec2(cell * tok::view2d_anchor_row_ratio, cell * tok::view2d_anchor_row_ratio));
      if (*anchor == 4)
         UiAudit::NoteOn();   // the grid's centre cell is the chosen one: a click there rightly changes nothing
      if (ImGui::IsItemClicked())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const int cx = std::clamp((int)((m.x - p.x) / cell), 0, tok::view2d_cx_hi), cy = std::clamp((int)((m.y - p.y) / cell), 0, tok::view2d_cx_hi);
         *anchor = cy * tok::view2d_anchor_ratio + cx;
         changed = true;
      }
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled), on = ImGui::GetColorU32(ImGuiCol_Text);
      for (int i = 0; i < 9; i++)
      {
         const ImVec2 c(p.x + (i % tok::view2d_anchor_row_period + tok::view2d_anchor_row_inset) * cell, p.y + (i / tok::view2d_anchor_row_scale + tok::view2d_anchor_row_inset) * cell);
         if (i == *anchor)
            dl->AddRectFilled(ImVec2(c.x - tok::space_1, c.y - tok::space_1), ImVec2(c.x + tok::space_1, c.y + tok::space_1), on, tok::view2d_anchor_row_radius);
         else
            dl->AddRect(ImVec2(c.x - tok::space_0_75, c.y - tok::space_0_75), ImVec2(c.x + tok::space_0_75, c.y + tok::space_0_75), dim, tok::view2d_outline_radius);
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

   [[maybe_unused]] int sAuditDialogs = 0;   // UI audit: native dialogs it asked for (and did not open)
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
      // Layers left out of export: draw the document once without them for the file.
      Scene2DNode* doc = FindDoc();
      bool hide = false;
      if (doc != nullptr)
         for (const Layer& l : doc->Layers())
            hide = hide || l.noExport;
      if (hide)
      {
         static int ef = tok::view2d_ef;
         Scene2DNode::sExport = true;
         doc->CookIfNeeded(++ef);
         out->CookIfNeeded(ef);
      }
      ExportImage(out, sStillPath, std::clamp(out->jpegQuality, 1, 100));
      Scene2DNode::sExport = false;
      sLastRender = sStillPath;
      sStillPath.clear();
   }

   // ---- keys ------------------------------------------------------------

   void HandlePixelKeys(Scene2DNode* doc, bool cmd, bool shift);

   bool HandleVectorKeys(Scene2DNode* doc);

   bool PaintRefused(Scene2DNode* doc);
   bool EnsurePixels(Scene2DNode* doc);
   bool SelectionActsOnPixels(Scene2DNode* doc);
   int PaintTarget(Scene2DNode* doc, int& target, bool make);

   // A selection edit is an undo step: the selection is saved in the document text.
   void SelStep(const char* label)
   {
      PushUndoCheckpoint(label);
      Touch();
   }

   bool sFillAsk = false;          // Shift+F5: open the Fill dialog
   int sFillWith = 0;              // 0 foreground, 1 background, 2 black, 3 white, 4 50% grey
   float sFillOpacity = 1.0f;

   // Delete and the fill keys. how: 0 clear, 1 foreground, 2 background, 3.. the Fill dialog's choice.
   // Inside the selection when there is one, else the whole layer.
   bool ClearOrFill(Scene2DNode* doc, int how, float opacity = 1.0f)
   {
      if (doc->quickMask || PaintRefused(doc))
         return false;
      if (how == 0)
         TargetSelection(doc);
      const Layer* a = Active(doc);
      // Delete on a live layer (node, text, vector) hides the selected area with a mask: the layer stays editable.
      if (how == 0 && a != nullptr && a->kind != Kind::Paint && a->kind != Kind::Group && SelectionActsOnPixels(doc))
      {
         PushUndoCheckpoint("Clear");
         doc->InvertSelection();
         doc->MaskFromSelection(a->id);
         doc->InvertSelection();
         Touch();
         return true;
      }
      if (how == 0 && (a == nullptr || a->kind != Kind::Paint))
         return false;
      PushUndoCheckpoint(how == 0 ? "Clear" : "Fill");
      int target = 0;
      const int id = PaintTarget(doc, target, how != 0);
      if (id <= 0 || target != 0)
         return false;
      static const float kBlack[3] = { 0, 0, 0 }, kWhite[3] = { 1, 1, 1 }, kGrey[3] = { 0.5f, 0.5f, 0.5f };
      const float* c = how == 1 ? sFg : how == 2 ? sBg : how == 3 ? kBlack : how == 4 ? kWhite : kGrey;
      const bool ok = how == 0 ? doc->ClearPixels(id) : doc->FillPixels(id, c, opacity);
      Touch();
      return ok;
   }

   // Copy / Cut / Ctrl+J with a pixel selection on a paint layer: a new paint layer holding only the selected
   // pixels (Photoshop's Layer via Copy / Layer via Cut). Returns its id, 0 when the selection does not apply.
   // A pixel selection with no layer picked acts on the topmost visible pixel layer (Affinity always has one picked):
   // drawing a marquee, lasso or wand selection over a photo and pressing Copy copies those pixels.
   void PickPixelLayerForSelection(Scene2DNode* doc)
   {
      if (!sSel.empty() || !doc->HasSelection() || doc->quickMask)
         return;
      TargetSelection(doc);
   }

   // A selection copies pixels from any layer that draws them; the live layer itself is never flattened (matrix: Copy).
   bool SelectionActsOnPixels(Scene2DNode* doc)
   {
      PickPixelLayerForSelection(doc);
      const Layer* a = Active(doc);
      return a != nullptr && (a->kind == Kind::Paint || a->kind == Kind::Node || a->kind == Kind::Text || a->kind == Kind::Vector) &&
             doc->HasSelection() && !doc->quickMask;
   }

   // The selected pixels of the active layer as a new paint layer above it, same group (Layer via Copy / Cut).
   // A text, vector or node layer stays live: its copy is rasterised, and a cut hides the area with a mask instead of
   // destroying the layer.
   int SelectionToLayer(Scene2DNode* doc, bool cut)
   {
      if (!SelectionActsOnPixels(doc))
         return 0;
      const int src = Active(doc)->id;
      const Kind kind = Active(doc)->kind;
      int nid = 0;
      if (kind == Kind::Paint)
         nid = doc->Duplicate(src);
      else if (kind == Kind::Node)
      {
         // A duplicate of the node layer, drawn into pixels in its place.
         static int sCopyFrame = 1 << 22;
         const int dup = doc->Duplicate(src);
         nid = dup > 0 ? doc->RasterizeNodeLayer(dup, ++sCopyFrame) : 0;
         if (nid == 0 && dup > 0)
            doc->RemoveLayer(dup, false);
      }
      else
      {
         nid = doc->Duplicate(src);
         if (nid > 0 && !doc->RasterizeLayer(nid))
         {
            doc->RemoveLayer(nid, false);
            nid = 0;
         }
      }
      if (nid <= 0)
         return 0;
      if (Layer* n = doc->FindLayer(nid))
      {
         n->lock = 0;
         n->maskUid = 0;
      }
      doc->InvertSelection();
      doc->ClearPixels(nid);
      doc->InvertSelection();
      if (cut)
      {
         if (kind == Kind::Paint)
            doc->ClearPixels(src);
         else
         {
            // The cut area hidden on the live layer: a mask that shows everything but the selection.
            doc->InvertSelection();
            doc->MaskFromSelection(src);
            doc->InvertSelection();
         }
      }
      return nid;
   }

   // Photoshop: with a marquee on one paint layer, Move, Free Transform, Flip and Rotate take the selected pixels
   // only. They are lifted into a floating layer (the cut leaves a hole), transformed, and put back down into the
   // layer when the action ends; the marquee follows them. Put down unmoved, the layer gets its own pixels back.
   struct Float
   {
      int id = 0, src = 0;
      Layer at;                          // the floating layer as it was lifted
      std::vector<std::string> tiles;    // the source's pixels before the cut
      std::vector<uint8_t> sel;          // the marquee before the move
      uint64_t docUid = 0;               // the document it floats in
   } sFloat;

   bool CanLift(Scene2DNode* doc)
   {
      if (sFloat.id != 0 || sSel.size() != 1 || !SelectionActsOnPixels(doc))
         return false;
      const Layer* a = Active(doc);
      return a != nullptr && a->kind == Kind::Paint && !a->is3d && Movable(doc, *a) &&
             (LockOf(doc, *a) & (Scene2DNode::kLockPixels | Scene2DNode::kLockAll)) == 0;
   }

   // Call after the action's undo checkpoint. False: nothing lifted, the action takes whole layers.
   bool LiftSelection(Scene2DNode* doc)
   {
      if (!CanLift(doc))
         return false;
      const Layer* a = Active(doc);
      Float f;
      f.src = a->id;
      f.tiles = a->tiles;
      f.sel = doc->SelectionPixels();
      // A transparency lock does not stop the pixels floating (PhotoCraft locked_for_float): the cut is let
      // through it, then the locks go back as they were.
      std::vector<std::pair<int, unsigned>> held;
      for (int id = a->id, guard = 0; id != 0 && guard < 64; guard++)
      {
         Layer* l = doc->FindLayer(id);
         if (l == nullptr)
            break;
         held.emplace_back(id, l->lock);
         l->lock &= ~(unsigned)Scene2DNode::kLockAlpha;
         id = l->parent;
      }
      f.id = SelectionToLayer(doc, true);
      for (const auto& [id, lock] : held)
         if (Layer* l = doc->FindLayer(id))
            l->lock = lock;
      if (f.id == 0)
         return false;
      f.at = *doc->FindLayer(f.id);
      f.docUid = sDocUid;
      sFloat = std::move(f);
      SelectOnly(sFloat.id);
      return true;
   }

   void DropFloat(Scene2DNode* doc)
   {
      if (sFloat.id == 0)
         return;
      const Float f = std::move(sFloat);
      sFloat = Float();
      const Layer* fl = doc->FindLayer(f.id);
      Layer* sl = doc->FindLayer(f.src);
      if (fl == nullptr || sl == nullptr)
         return;
      float m0[6], m1[6];
      doc->LayerMatrix(f.at, m0);
      doc->LayerMatrix(*fl, m1);
      bool moved = fl->warpOn != f.at.warpOn;
      for (int k = 0; k < 6; k++)
         moved |= std::fabs(m0[k] - m1[k]) > 1e-4f;
      for (int k = 0; k < 8 && fl->warpOn; k++)
         moved |= fl->warp[k] != f.at.warp[k];
      if (!moved)
      {
         sl->tiles = f.tiles;
         doc->RemoveLayer(f.id, false);
         doc->Commit();
      }
      else
      {
         const Layer after = *fl;
         static int sDropFrame = 1 << 23;
         if (doc->StampLayerInto(f.id, f.src, ++sDropFrame))
            doc->CarrySelection(f.sel, f.at, after);
      }
      if (doc->FindLayer(f.src) != nullptr)
         SelectOnly(f.src);
      Touch();
   }

   // Esc during a drag puts everything back and drops the drag's undo step.
   // Free transform (Cmd/Ctrl+T): every move, scale and rotate drag until Enter is one undo step, and Esc puts
   // the layers back as they were when it began.
   struct FreeTransform
   {
      Scene2DNode* doc = nullptr;
      std::vector<Layer> before;
      size_t undo0 = (size_t)-1;
      int prevTool = 0;            // the tool to go back to when it ends (Cmd+T works from any tool)
   } sFreeT;
   // What a handle drag does inside a free transform (its right-click menu, Photoshop's Edit > Transform).
   enum FreeMode { kFreeAny = 0, kFreeScale, kFreeRotate, kFreeSkew, kFreeDistort, kFreePerspective, kFreeModes };
   int sFreeMode = kFreeAny;
   void FreeTransformMenu(Scene2DNode* doc);

   bool FreeTransformOn(Scene2DNode* doc) { return sFreeT.doc != nullptr && sFreeT.doc == doc; }

   void BeginFreeTransform(Scene2DNode* doc)
   {
      sFreeT = FreeTransform();
      const size_t undo0 = gUndoStack.size();
      const bool lift = CanLift(doc);
      if (lift)
      {
         PushUndoCheckpoint("Free transform");
         if (!LiftSelection(doc))
         {
            gUndoStack.resize(undo0);
            return;
         }
      }
      for (int id : MovingIds(doc))
         if (const Layer* l = doc->FindLayer(id))
            sFreeT.before.push_back(*l);
      if (sFreeT.before.empty())
      {
         DropFloat(doc);
         if (lift)
            gUndoStack.resize(undo0);
         return;
      }
      sFreeT.doc = doc;
      sFreeT.undo0 = undo0;
      sFreeT.prevTool = sTool;
      sFreeMode = kFreeAny;
      if (!lift)
         PushUndoCheckpoint("Free transform");
      sTool = kMove;
   }

   void EndFreeTransform(Scene2DNode* doc, bool keep)
   {
      if (!FreeTransformOn(doc))
      {
         sFreeT = FreeTransform();
         return;
      }
      if (!keep)
      {
         for (const Layer& b : sFreeT.before)
            if (Layer* l = doc->FindLayer(b.id))
               *l = b;
         DropFloat(doc);   // back where it was lifted: the layer gets its own pixels back
         while (sFreeT.undo0 != (size_t)-1 && gUndoStack.size() > sFreeT.undo0)
            gUndoStack.pop_back();
         Touch();
      }
      else
         DropFloat(doc);
      sTool = sFreeT.prevTool;
      sFreeMode = kFreeAny;
      sFreeT = FreeTransform();
   }

   bool CancelOp(Scene2DNode* doc)
   {
      const int k = sOp.kind;
      const bool layers = k == 1 || k == 2 || k == 3 || k == 16 || k == 18 || k == 20 || k == 21;
      if (layers)
      {
         for (const Layer& b : sOp.before)
            if (Layer* l = doc->FindLayer(b.id))
               *l = b;
         if (Layer* l = doc->FindLayer(sOp.l0.id))
            if (std::none_of(sOp.before.begin(), sOp.before.end(), [&](const Layer& b) { return b.id == l->id; }))
               *l = sOp.l0;
         while (sOp.undo0 != (size_t)-1 && gUndoStack.size() > sOp.undo0)
            gUndoStack.pop_back();
         Touch();
      }
      else if (k == 5)
         std::copy(sOp.cropWas, sOp.cropWas + 4, sCrop);
      else if (k == 19)
         sZoom = sOp.zoom0, sPan = sOp.pan0;
      else if (k == 22)
         sViewAngle = sOp.angle0, sPan = sOp.pan0;
      else if (k != 8 && k != 9 && k != 10 && k != 17)
         return false;
      sOp = Op();
      sSnapX.clear();
      sSnapY.clear();
      return true;
   }

   // A crop box under 2 px on a side is no box (a click without a drag).
   bool CropBoxOk()
   {
      return std::fabs(sCrop[2] - sCrop[0]) >= 2.0f && std::fabs(sCrop[3] - sCrop[1]) >= 2.0f;
   }

   // Layer edits work from the layer list as well as the canvas: delete, copy, cut, paste, duplicate, group,
   // clip, restack and select all.
   void HandleLayerKeys(Scene2DNode* doc, bool cmd, bool shift, bool alt, bool onCanvas)
   {
      auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      if (cmd && shift && key(ImGuiKey_F))
      {
         ViewDockToggleFill(2);
         return;
      }
      if (shift && !cmd && key(ImGuiKey_F5))
         sFillAsk = true;
      // Alt+Backspace fills with the foreground, Cmd/Ctrl+Backspace with the background (inside the selection when
      // there is one); Delete with a selection clears the pixels inside it and keeps the layer.
      if ((alt || cmd) && (key(ImGuiKey_Backspace) || key(ImGuiKey_Delete)))
         ClearOrFill(doc, alt ? 1 : 2);
      else if (key(ImGuiKey_Delete) || key(ImGuiKey_Backspace))
      {
         if (SelectionActsOnPixels(doc))
            ClearOrFill(doc, 0);
         else
            sPending.what = Pending::Delete;
      }
      if ((cmd && !shift && key(ImGuiKey_J)) || (!cmd && !alt && shift && key(ImGuiKey_D)))
         sPending.what = Pending::Duplicate;
      if (cmd && !shift && !alt && key(ImGuiKey_R))
      {
         if (Layer* active = Active(doc))
         {
            sRename = active->id;
            std::snprintf(sRenameBuf, sizeof(sRenameBuf), "%s", LayerName(*active).c_str());
         }
         else
         {
            PushUndoCheckpoint("Rulers");
            doc->Doc().rulers = !doc->Doc().rulers;
            Touch();
         }
      }
      if (cmd && !alt && key(ImGuiKey_E)) sPending.what = shift ? Pending::MergeVisible : Pending::MergeDown;
      if (cmd && alt && key(ImGuiKey_E)) sPending.what = shift ? Pending::StampVisible : Pending::StampDown;
      if (cmd && shift && !alt && key(ImGuiKey_C)) sPending.what = Pending::CopyMerged;
      if (cmd && shift && key(ImGuiKey_J) && SelectionActsOnPixels(doc))
      {
         PushUndoCheckpoint("Layer via cut");
         if (const int nid = SelectionToLayer(doc, true))
            SelectOnly(nid);
      }
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
      {
         SelStep("Select all");
         doc->SelectAll();
      }
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

   // Spring-loaded tools (Photoshop): a tool key held past a tap and let go puts back the tool there was.
   ImGuiKey sSpringKey = ImGuiKey_None;
   int sSpringPrev = 0;
   double sSpringT = 0.0;

   void HandleKeys(Scene2DNode* doc, bool hovered)
   {
      const ImGuiIO& io = ImGui::GetIO();
      if (sSpringKey != ImGuiKey_None && !ImGui::IsKeyDown(sSpringKey) && sOp.kind == 0)
      {
         if (ImGui::GetTime() - sSpringT > tok::motion_s_spring)
            sTool = sSpringPrev;
         sSpringKey = ImGuiKey_None;
      }
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
      // Plain letters only: Shift+letter belongs to the app (Shift+R Piano Roll, Shift+E Curve Editor, Shift+J Drums).
      if (!cmd && !alt && !shift)
      {
         const int toolWas = sTool;
         struct Spring { int was; ~Spring() {
            if (sTool == was)
               return;
            for (int k = ImGuiKey_A; k <= ImGuiKey_Z; k++)
               if (ImGui::IsKeyPressed((ImGuiKey)k, false))
               {
                  sSpringKey = (ImGuiKey)k;
                  sSpringPrev = was;
                  sSpringT = ImGui::GetTime();
               }
         } } spring{ toolWas };
         if (key(ImGuiKey_V)) sTool = kMove;
         if (key(ImGuiKey_C)) sTool = kCrop;
         if (key(ImGuiKey_H)) sTool = kHand;
         if (key(ImGuiKey_Z)) sTool = kZoom;
         // A key pressed again steps through the tools that share it (Photoshop's Shift-cycling, without Shift).
         auto cycle = [](std::initializer_list<int> ts) {
            const int* b = ts.begin();
            for (size_t i = 0; i < ts.size(); i++)
               if (b[i] == sTool) { sTool = b[(i + 1) % ts.size()]; return; }
            sTool = *b;
         };
         if (key(ImGuiKey_B)) cycle({ kBrush, kMixer, kColourReplace });
         if (key(ImGuiKey_E)) cycle({ kEraser, kBgErase, kFloodErase });
         if (key(ImGuiKey_G)) cycle({ kFill, kGradient });
         if (key(ImGuiKey_S)) cycle({ kClone, kUndoBrush });
         if (key(ImGuiKey_J)) cycle({ kHeal, kBlemish, kPatch, kInpaint, kRedEye });
         if (key(ImGuiKey_R)) cycle({ kBlur, kSharpen, kMedian });
         if (key(ImGuiKey_O)) cycle({ kDodge, kBurn, kSponge });
         if (key(ImGuiKey_I)) cycle({ kPick, kStylePick, kMeasure });
         if (key(ImGuiKey_M)) sTool = kMarquee;
         if (key(ImGuiKey_L)) sTool = kLasso;
         if (key(ImGuiKey_W)) cycle({ kWand, kSelBrush, kObjectSel });
         if (key(ImGuiKey_P)) sTool = kPen;
         if (key(ImGuiKey_A)) sTool = kNode;
         if (key(ImGuiKey_U)) sTool = kShape;
         if (key(ImGuiKey_T)) sTool = kText;
      }
      if (key(ImGuiKey_Escape) && sOp.kind != 0 && CancelOp(doc))
         return;
      if (sFreeT.doc != nullptr && (sFreeT.doc != doc || sTool != kMove))
      {
         const int chosen = sTool;   // picking another tool keeps what was done and stays on that tool
         EndFreeTransform(sFreeT.doc == doc ? doc : nullptr, true);
         sTool = chosen;
      }
      if (FreeTransformOn(doc) && sOp.kind == 0)
      {
         if (key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter))
         {
            EndFreeTransform(doc, true);
            return;
         }
         if (key(ImGuiKey_Escape))
         {
            EndFreeTransform(doc, false);
            return;
         }
      }
      if (cmd && !shift && key(ImGuiKey_T))
      {
         if (FreeTransformOn(doc))
            EndFreeTransform(doc, true);
         else
            BeginFreeTransform(doc);
         return;
      }
      if (HandleVectorKeys(doc))
         return;
      HandlePixelKeys(doc, cmd, shift);
      if (key(ImGuiKey_F12))
         sPending.what = cmd ? Pending::RenderAnim : Pending::RenderImage;
      if (cmd && shift && key(ImGuiKey_0))
         TurnViewAbout(doc, -sViewAngle, ImVec2((sViewMn.x + sViewMx.x) * 0.5f, (sViewMn.y + sViewMx.y) * 0.5f));
      else if (cmd && key(ImGuiKey_0))
         FitView(doc);
      if (cmd && key(ImGuiKey_1)) { sZoom = 1.0f; sPan = ImVec2(0, 0); }
      const ImVec2 mid((sViewMn.x + sViewMx.x) * 0.5f, (sViewMn.y + sViewMx.y) * 0.5f);
      if (cmd && (rep(ImGuiKey_Equal) || rep(ImGuiKey_KeypadAdd))) ZoomAt(doc, 1.25f, mid);
      if (cmd && (rep(ImGuiKey_Minus) || rep(ImGuiKey_KeypadSubtract))) ZoomAt(doc, 0.8f, mid);
      Scene2DNode::Document& d = doc->Doc();
      // The view switches are saved with the document, so each is an undo step like the same switch in the Document tab.
      auto flip = [&](bool& v, const char* label)
      {
         PushUndoCheckpoint(label);
         v = !v;
         Touch();
      };
      if (cmd && !shift && key(ImGuiKey_Apostrophe)) flip(d.grid, "Grid");
      if (cmd && shift && key(ImGuiKey_R)) flip(d.rulers, "Rulers");
      if (cmd && !shift && key(ImGuiKey_Semicolon)) flip(d.guidesOn, "Guides");
      if (cmd && shift && key(ImGuiKey_Semicolon)) flip(d.snap, "Snap");
      if (sTool == kCrop && sCropOn)
      {
         if ((key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter)) && CropBoxOk())
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
      {
         if (sSel.empty() && gView2DDock == 4)
            ViewDockToggleFill(2);   // Esc with nothing to drop leaves Fill
         sSel.clear();
      }
      HandleLayerKeys(doc, cmd, shift, alt, true);
      const float step = shift ? 10.0f : 1.0f;
      if (!cmd && !alt && sTool == kMove)
         if (Layer* a = Active(doc))
         {
            // Digits set the layer's opacity (1 = 10% ... 0 = 100%), as in Photoshop's Move tool.
            static const ImGuiKey kDigits[10] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
                                                  ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9, ImGuiKey_0 };
            for (int i = 0; i < 10; i++)
               if (key(kDigits[i]) && (LockOf(doc, *a) & Scene2DNode::kLockAll) == 0)
               {
                  PushUndoCheckpoint("Layer opacity");
                  a->opacity = (i + 1) / tok::view2d_opacity_ratio;
                  Touch();
               }
         }
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

   // The brush and eraser settings kept between runs: "<b|e> <field> <values>" lines in the app's support folder.
   std::string BrushFile() { return AppPaths::AppSupportDir() + "/2dview-brushes.txt"; }
   template <class F> void BrushFields(BrushEngine::Brush& b, F f)
   {
      auto i = [&](const char* n, int& v) { float x = (float)v; f(n, &x, 1); v = (int)std::lround(x); };
      auto bo = [&](const char* n, bool& v) { float x = v ? 1.0f : 0.0f; f(n, &x, 1); v = x > tok::view2d_v_limit; };
      i("tip", b.tip); f("size", &b.size, 1); f("hardness", &b.hardness, 1); f("opacity", &b.opacity, 1);
      f("flow", &b.flow, 1); f("spacing", &b.spacing, 1); f("jitter", &b.jitter, 1); f("angle", &b.angle, 1);
      f("roundness", &b.roundness, 1); f("smoothing", &b.smoothing, 1); i("stabiliser", b.stabiliser);
      bo("pencil", b.pencil); bo("pressureSize", b.pressureSize); bo("pressureOpacity", b.pressureOpacity);
      i("mode", b.mode); bo("lockAlpha", b.lockAlpha); bo("follow", b.follow); f("taperIn", &b.taperIn, 1);
      f("taperOut", &b.taperOut, 1); f("sizeJitter", &b.sizeJitter, 1); f("opacityJitter", &b.opacityJitter, 1);
      f("hueJitter", &b.hueJitter, 1); f("satJitter", &b.satJitter, 1); f("valJitter", &b.valJitter, 1);
      i("range", b.range); bo("sampleAll", b.sampleAll); i("relief", b.relief); i("tines", b.tines);
      i("material", b.material); i("profile", b.profile); f("depth", &b.depth, 1); f("lightAz", &b.lightAz, 1);
      f("lightEl", &b.lightEl, 1); i("grain", b.grain); f("grainScale", &b.grainScale, 1); f("grainDepth", &b.grainDepth, 1);
      i("grainMode", b.grainMode); i("dualTip", b.dualTip); f("dualScale", &b.dualScale, 1); f("dualScatter", &b.dualScatter, 1);
      i("colorAlong", b.colorAlong); f("colorB", b.colorB, 3); f("colorPeriod", &b.colorPeriod, 1); f("wet", &b.wet, 1);
      f("refill", &b.refill, 1);
      // Retouch and dab settings (the wiring audit found them unsaved: a preset lost them on reload).
      f("sourceRotate", &b.sourceRotate, 1); f("sourceScale", &b.sourceScale, 1); bo("sourceFlipX", b.sourceFlipX);
      bo("sourceFlipY", b.sourceFlipY); i("sourceBlend", b.sourceBlend); bo("protectHue", b.protectHue);
      i("sharpMode", b.sharpMode); i("spongeMode", b.spongeMode); f("keyTolerance", &b.keyTolerance, 1);
      bo("wetEdges", b.wetEdges); bo("autoClean", b.autoClean); f("accumulation", &b.accumulation, 1);
      f("accumJitter", &b.accumJitter, 1); f("hardJitter", &b.hardJitter, 1); f("angleJitter", &b.angleJitter, 1);
      f("roundJitter", &b.roundJitter, 1); f("scatterY", &b.scatterY, 1);
      bo("oneEuro", b.oneEuro); bo("catchUp", b.catchUp); f("pressureCurve", &b.pressureCurve, 1);
      bo("tiltSize", b.tiltSize); bo("tiltAngle", b.tiltAngle); bo("tiltRound", b.tiltRound);
      f("airbrushRate", &b.airbrushRate, 1);
   }
   std::string BrushText()
   {
      std::ostringstream o;
      auto put = [&](char who, BrushEngine::Brush& b)
      {
         BrushFields(b, [&](const char* n, float* v, int c)
         {
            o << who << " " << n;
            for (int k = 0; k < c; k++)
               o << " " << v[k];
            o << "\n";
         });
      };
      put('b', sBrush);
      put('e', sEraser);
      return o.str();
   }
   void LoadBrushes()
   {
      std::ifstream in(AppPaths::FsPath(BrushFile()));
      std::string line;
      while (std::getline(in, line))
      {
         std::istringstream s(line);
         char who = 0;
         std::string name;
         if (!(s >> who >> name))
            continue;
         std::vector<float> vals;
         for (float v; s >> v;)
            vals.push_back(v);
         BrushFields(who == 'e' ? sEraser : sBrush, [&](const char* n, float* v, int c)
         {
            if (name == n && (int)vals.size() >= c)
               std::copy(vals.begin(), vals.begin() + c, v);
         });
      }
      sEraser.mode = BrushEngine::kErase;
   }
   // Writes the settings when they changed, at most once a second.
   void SaveBrushesIfChanged()
   {
      static std::string last;
      static double at = -10.0;
      if (std::getenv("INFINITE_VIEW2DTEST") != nullptr || ImGui::GetTime() - at < tok::motion_s_save_settle)
         return;
      at = ImGui::GetTime();
      const std::string now = BrushText();
      if (last.empty())
      {
         last = now;   // the first look: what was loaded
         return;
      }
      if (now == last)
         return;
      last = now;
      std::error_code ec;
      std::filesystem::create_directories(AppPaths::AppSupportDir(), ec);
      std::ofstream(AppPaths::FsPath(BrushFile())) << now;
   }

   void InitBrushes()
   {
      if (sBrushInit)
         return;
      sBrushInit = true;
      const auto& presets = BrushEngine::Presets();
      for (int i = 0; i < (int)presets.size(); i++)
      {
         // A full-strength round, as every paint app opens with (Photoshop: round, 100% flow). The soft airbrush's 8% flow
         // made a first stroke look like nothing happened.
         if (sBrushPreset < 0 && std::strcmp(presets[i].name, "Hard round") == 0)
            sBrushPreset = i;
         if (sEraserPreset < 0 && std::strcmp(presets[i].name, "Eraser hard") == 0)   // 100% as Photoshop's; soft half-erased
            sEraserPreset = i;
      }
      if (sBrushPreset >= 0)
         sBrush = presets[sBrushPreset].brush;
      if (sEraserPreset >= 0)
         sEraser = presets[sEraserPreset].brush;
      sEraser.mode = BrushEngine::kErase;
      if (std::getenv("INFINITE_VIEW2DTEST") == nullptr)
         LoadBrushes();
   }

   // Retouch tools share one brush; the tool picks what it does to the pixels.
   BrushEngine::Brush sRetouch = []()
   {
      BrushEngine::Brush b;
      b.size = tok::view2d_size;
      b.hardness = 0.4f;
      b.opacity = tok::view2d_opacity;
      b.spacing = tok::view2d_spacing;
      return b;
   }();
   ImVec2 sCloneSrc(0, 0), sCloneOffset(0, 0), sCloneAnchor(0, 0);
   bool sHaveCloneSrc = false, sCloneOffsetSet = false;
   // Paint mixer: the brush with wet paint; selection brush: paints the selection.
   BrushEngine::Brush sMixer = []()
   {
      BrushEngine::Brush b;
      b.size = tok::view2d_size;
      b.wet = 0.6f;
      b.refill = 0.1f;
      return b;
   }();
   BrushEngine::Brush sSelBrush = []()
   {
      BrushEngine::Brush b;
      b.size = tok::view2d_size;
      b.hardness = 0.8f;
      return b;
   }();
   float sBlemishSize = tok::view2d_blemish_size;      // blemish removal: the spot's width, document px
   int sStyleFrom = 0;              // style picker: the layer whose style is held
   ImVec2 sMeasureA(0, 0), sMeasureB(0, 0);
   bool sMeasureOn = false;
   bool sMarqueeCentre = false, sMarqueeAA = true;
   // Mesh warp: the layer, its pixels when the warp began, and the grid as moved.
   int sMeshId = 0, sMeshPoint = -1;
   std::vector<std::string> sMeshFrom, sMeshStored;
   float sMeshPts[32] = {}, sMeshPts0[32] = {};
   ImVec2 sMeshGrab;               // the held point minus the mouse at the press: the point moves by the drag, not to it

   BrushEngine::Brush& ToolBrush()
   {
      if (RetouchTool(sTool))
      {
         switch (sTool)
         {
         case kClone: sRetouch.mode = BrushEngine::kClone; break;
         case kHeal: sRetouch.mode = BrushEngine::kHeal; break;
         case kBlur: sRetouch.mode = BrushEngine::kBlur; break;
         case kSharpen: sRetouch.mode = BrushEngine::kSharpen; break;
         case kDodge: sRetouch.mode = BrushEngine::kDodge; break;
         case kBurn: sRetouch.mode = BrushEngine::kBurn; break;
         case kSponge: sRetouch.mode = BrushEngine::kSpongeMode; break;
         case kMedian: sRetouch.mode = BrushEngine::kMedian; break;
         case kUndoBrush: sRetouch.mode = BrushEngine::kUndo; break;
         case kColourReplace: sRetouch.mode = BrushEngine::kColourReplace; break;
         case kBgErase: sRetouch.mode = BrushEngine::kBgErase; break;
         case kRedEye: sRetouch.mode = BrushEngine::kRedEye; break;
         default: sRetouch.mode = BrushEngine::kInpaint; break;
         }
         return sRetouch;
      }
      if (sTool == kMixer)
         return sMixer;
      if (sTool == kSelBrush)
         return sSelBrush;
      return sTool == kEraser ? sEraser : sBrush;
   }

   float PenNow(Scene2DNode* doc = nullptr)
   {
      const Platform::PenSample s = Platform::PenNowSample(glfwGetCurrentContext());
      if (doc != nullptr)
         doc->SetPaintMeta(s.tiltX, s.tiltY, s.rotation, s.timestamp > 0.0 ? s.timestamp : ImGui::GetTime());
      return s.pressure < 0.0f ? 1.0f : std::max(0.02f, s.pressure);
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
      // Directly above the active layer, in its group; a group active: at the top inside it (tool-layer-matrix.md).
      int above = a != nullptr ? a->id : 0;
      if (a != nullptr && a->kind == Kind::Group)
      {
         const std::vector<int> kids = doc->ChildrenOf(a->id);
         above = kids.empty() ? 0 : kids.back();
         if (kids.empty())
         {
            const int gid = a->id;
            const int id = doc->AddPaintLayer(gid);
            if (Layer* n = doc->FindLayer(id))
               n->parent = gid;
            doc->Commit();
            SelectOnly(id);
            return id;
         }
      }
      const int id = doc->AddPaintLayer(above);
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
      return !doc->Shown(*a) || (!sEditMask && (LockOf(doc, *a) & (Scene2DNode::kLockPixels | Scene2DNode::kLockAll)) != 0);
   }

   int sRasterAsk = 0;             // the text or vector layer a pixel tool was tried on
   ImVec2 sRasterAskMn, sRasterAskMx; // last frame's chip rect: the canvas under it gives the click to the chip
   double sRasterAskAt = 0.0;

   // The sibling just under a layer (0: it is the lowest at its level), for Merge Down.
   int LayerUnder(Scene2DNode* doc, int id)
   {
      const Layer* l = doc != nullptr ? doc->FindLayer(id) : nullptr;
      if (l == nullptr)
         return 0;
      int under = 0;
      for (int c : doc->ChildrenOf(l->parent))
      {
         if (c == id)
            return under;
         under = c;
      }
      return 0;
   }

   void RasterizeLayer(Scene2DNode* doc, int id)
   {
      const Layer* l = doc->FindLayer(id);
      if (l == nullptr || (l->kind != Kind::Text && l->kind != Kind::Vector))
         return;
      PushUndoCheckpoint("Rasterize layer");
      if (sTextEdit == id)
         sTextEdit = 0;
      doc->RasterizeLayer(id);
      Touch();
   }

   // An image / node layer under a pixel tool or a pixel cut: made into pixels in place (as Affinity does), one undo step.
   bool EnsurePixels(Scene2DNode* doc)
   {
      const Layer* a = Active(doc);
      if (a == nullptr || a->kind != Kind::Node || a->HasSkin())
         return false;
      static int sRasterFrame = 1 << 20;
      PushUndoCheckpoint("Rasterize layer");
      const int nid = doc->RasterizeNodeLayer(a->id, ++sRasterFrame);
      if (nid == 0)
         return false;
      SelectOnly(nid);
      Touch();
      return true;
   }

   // A preset's look, painted once: one S stroke on a small clear surface, shown beside its name in the
   // preset list (Photoshop's and Procreate's stroke thumbnails). Materials keep their colour; the rest paint in
   // the text colour so they read on the menu.
   unsigned BrushThumb(int i)
   {
      constexpr int kW = tok::view2d_w, kH = tok::view2d_brush_thumb_h;
      static std::map<int, GLUtil::Fbo> thumbs;
      const auto& presets = BrushEngine::Presets();
      if (i < 0 || i >= (int)presets.size())
         return 0;
      auto it = thumbs.find(i);
      if (it != thumbs.end())
         return it->second.tex;
      GLUtil::Fbo& f = thumbs[i];
      if (!GLUtil::EnsureFbo(f, kW, kH, GL_RGBA8, "BrushThumb"))
         return 0;
      GLint prevFbo = 0, vp[4];
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      glGetIntegerv(GL_VIEWPORT, vp);
      glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
      glViewport(0, 0, kW, kH);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glViewport(vp[0], vp[1], vp[2], vp[3]);
      BrushEngine::Brush b = presets[i].brush;
      if (BrushEngine::DirectMode(b.mode) || BrushEngine::RetouchMode(b.mode) || b.mode == BrushEngine::kErase || b.mode == BrushEngine::kClearMode)
         return f.tex;   // these move or remove what is there: nothing to show on a clear surface
      if (presets[i].group == nullptr || std::strcmp(presets[i].group, "Material") != 0)
      {
         const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         b.color[0] = t.x;
         b.color[1] = t.y;
         b.color[2] = t.z;
      }
      b.size = std::clamp(b.size, tok::view2d_brush_thumb_lo, (float)kH * tok::view2d_brush_thumb_ratio);
      b.stabiliser = 0;
      b.smoothing = 0.0f;
      b.colorTex = b.tipTex = b.grainTex = 0;
      BrushEngine::Stroke st;
      const float pad = b.size * 0.5f + tok::space_0_5;
      auto at = [&](float u, float& x, float& y) {
         x = pad + u * ((float)kW - tok::view2d_brush_thumb_scale * pad);
         y = (float)kH * 0.5f + std::sin(u * tok::view2d_sin_t) * ((float)kH * 0.5f - pad);
      };
      float x = 0, y = 0;
      at(0.0f, x, y);
      st.Begin(f, b, x, y, 0.3f);   // ui-audit-ok: model (preview stroke pen pressure (model value))
      for (int k = 1; k <= 48; k++)
      {
         const float u = (float)k / 48.0f;
         at(u, x, y);
         st.MoveTo(x, y, 0.3f + 0.7f * std::sin(u * 3.14159265f));
      }
      int dirty[4];
      st.End(f, dirty);
      return f.tex;
   }

   // After a pixel tool met a text or vector layer: one chip at the top of the canvas, gone after a few seconds.
   void DrawRasterAsk(Scene2DNode* doc)
   {
      if (sRasterAsk == 0)
         return;
      const Layer* l = doc->FindLayer(sRasterAsk);
      if (l == nullptr || (l->kind != Kind::Text && l->kind != Kind::Vector) || ImGui::GetTime() - sRasterAskAt > tok::motion_s_ask_hold)
      {
         sRasterAsk = 0;
         return;
      }
      const ImVec2 keep = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(sViewMn.x + tok::space_3, sViewMn.y + tok::space_3));
      const bool hit = TextChip(L("Rasterize?##v2drasterask"), true, T("Turn this layer into pixels so the tool can work on it"));
      sRasterAskMn = ImGui::GetItemRectMin();
      sRasterAskMx = ImGui::GetItemRectMax();
      if (hit)
      {
         RasterizeLayer(doc, sRasterAsk);
         sRasterAsk = 0;
      }
      ImGui::SetCursorScreenPos(keep);
   }

   // Shift+F5: what to fill with and how strongly; one click on a row fills (the selection, else the layer).
   void DrawFillDialog(Scene2DNode* doc)
   {
      if (sFillAsk)
      {
         sFillAsk = false;
         ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
         ImGui::OpenPopup("##v2dfilldlg");
      }
      if (!MenuParts::BeginPopup("##v2dfilldlg"))
         return;
      float pct = sFillOpacity * tok::view2d_pct_ratio;
      ImGui::SetNextItemWidth(tok::view2d_field_w);
      if (FieldWell::Slider("##v2dfillop", &pct, params::view2d::pct_lo, params::view2d::pct_hi, "%.0f%%"))
         sFillOpacity = std::clamp(pct / tok::view2d_fill_dialog_ratio, tok::view2d_fill_opacity_lo, 1.0f);
      static const char* kWith[5] = { "Foreground", "Background", "Black", "White", "50% Grey" };
      for (int i = 0; i < 5; i++)
         if (MenuParts::Item(T(kWith[i])))
         {
            sFillWith = i;
            ClearOrFill(doc, i + 1, sFillOpacity);
         }
      MenuParts::EndPopup();
   }

   // Selections kept with the document under a name.
   void SelectionMenu(Scene2DNode* doc)
   {
      if (!ImGui::BeginMenu(T("Selection")))
         return;
      if (MenuParts::Item(T("Save Selection"), nullptr, false, doc->HasSelection()))
      {
         const std::vector<std::string> have = doc->SavedSelections();
         std::string name;
         for (int n = 1; name.empty(); n++)
         {
            const std::string c = std::string(T("Selection")) + " " + std::to_string(n);
            if (std::find(have.begin(), have.end(), c) == have.end())
               name = c;
         }
         PushUndoCheckpoint("Save selection");
         doc->SaveSelection(name);
         Touch();
      }
      for (const std::string& name : doc->SavedSelections())
         if (ImGui::BeginMenu(name.c_str()))
         {
            static const char* kHow[4] = { "Load", "Add", "Subtract", "Intersect" };
            for (int op = 0; op < 4; op++)
               if (MenuParts::Item(T(kHow[op])))
               {
                  SelStep("Load selection");
                  doc->LoadSelection(name, op);
               }
            if (MenuParts::Item(T("Delete")))
            {
               PushUndoCheckpoint("Delete saved selection");
               doc->DeleteSavedSelection(name);
               Touch();
            }
            ImGui::EndMenu();
         }
      ImGui::EndMenu();
   }

   // A live layer's pixels as a new paint layer right above it, the original hidden and kept (Adobe: rasterise a duplicate).
   int RasterCopy(Scene2DNode* doc, int id)
   {
      const Layer* l = doc->FindLayer(id);
      if (l == nullptr)
         return 0;
      const Kind kind = l->kind;
      const std::string name = l->name;
      const int dup = doc->Duplicate(id);
      int nid = 0;
      if (dup > 0 && kind == Kind::Node)
      {
         static int sCopyFrame = 1 << 23;
         nid = doc->RasterizeNodeLayer(dup, ++sCopyFrame);
         if (nid == 0)
            doc->RemoveLayer(dup);
      }
      else if (dup > 0)
      {
         nid = doc->RasterizeLayer(dup) ? dup : 0;
         if (nid == 0)
            doc->RemoveLayer(dup);
      }
      if (nid == 0)
         return 0;
      if (Layer* o = doc->FindLayer(id))
         o->visible = false;
      if (Layer* n = doc->FindLayer(nid))
      {
         n->lock = 0;
         n->name = (name.empty() ? std::string(T("Layer")) : name) + " " + T("pixels");
      }
      doc->Commit();
      SelectOnly(nid);
      Touch();
      return nid;
   }

   // A pixel tool on a live layer works on a pixel copy above it (the original hidden, kept).
   void LivePixelCopy(Scene2DNode* doc)
   {
      const Layer* a = Active(doc);
      if (a != nullptr && (a->kind == Kind::Text || a->kind == Kind::Vector || (a->kind == Kind::Node && !a->HasSkin())))
      {
         PushUndoCheckpoint("Rasterise a copy");
         RasterCopy(doc, a->id);
      }
   }

   // The Eraser on a layer with locked transparency cannot take alpha away: it paints the background colour
   // (Photoshop; PhotoCraft erase_locked). The Background Eraser still erases.
   BrushEngine::Brush EraseLocked(Scene2DNode* doc, int id, int target, BrushEngine::Brush b)
   {
      const Layer* l = doc->FindLayer(id);
      if (target == 0 && sTool == kEraser && b.mode == BrushEngine::kErase && l != nullptr &&
          (b.lockAlpha || (LockOf(doc, *l) & Scene2DNode::kLockAlpha) != 0))
      {
         b.mode = BrushEngine::kNormal;
         std::copy(sBg, sBg + 3, b.color);
      }
      return b;
   }

   // Patch release. Destination mode: the selection goes with the patch (PhotoCraft #12119); Source mode keeps it.
   bool PatchApply(Scene2DNode* doc, int id, float dx, float dy)
   {
      if (!doc->PatchSelection(id, dx, dy, sPatchSrc, sPatchClear))
         return false;
      if (sPatchSrc)
         doc->ShiftSelection(dx, dy);
      return true;
   }

   void BeginStroke(Scene2DNode* doc, ImVec2 at, bool line)
   {
      // Healing with no Alt-click source yet mends from around the stroke (spot healing); the clone stamp still
      // needs its source.
      const bool spotHeal = sTool == kHeal && !sHaveCloneSrc;
      const bool cloning = (sTool == kClone || sTool == kHeal) && !spotHeal;
      if (sTool == kClone && !sHaveCloneSrc)
      {
         sCloneAsk = ImGui::GetTime();
         return;   // Alt-click sets the source first
      }
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
      if (sTool == kSelBrush)
      {
         // The selection brush paints the selection itself: white adds, Alt takes away.
         SelStep("Selection brush");
         BrushEngine::Brush b = sSelBrush;
         b.mode = ImGui::GetIO().KeyAlt != sSelBrushSub ? BrushEngine::kErase : BrushEngine::kNormal;
         b.color[0] = b.color[1] = b.color[2] = 1.0f;
         if (!sSelBrushSoft)
            b.hardness = 1.0f;
         if (!doc->BeginPaint(-1, 2, b, at.x, at.y, PenNow(doc)))
            return;
         sOp = Op();
         sOp.kind = 7;
         sOp.docStart = at;
         return;
      }
      if (PaintRefused(doc))
         return;
      const bool painter = sTool == kBrush || sTool == kMixer;
      // The eraser and the retouch tools need pixels. On a text, vector or other layer nothing is made and no
      // undo step is left; the canvas offers to rasterize instead.
      // On a live layer (node, text, vector) nothing is flattened (tool-layer-matrix.md): the eraser hides through the
      // layer's mask, the clone stamp paints a new layer above from the layers below, and the tools that rework pixels
      // get a rasterised copy above with the original hidden.
      bool cloneAbove = false;
      if (!painter && !doc->quickMask)
      {
         Layer* a = Active(doc);
         if (a != nullptr && a->kind == Kind::Group && !sEditMask)
         {
            std::vector<int> targets;
            for (const Layer& l : doc->Layers())
               if (l.kind == Kind::Paint && doc->IsInside(l.id, a->id) && doc->Shown(l) &&
                   (LockOf(doc, l) & (Scene2DNode::kLockPixels | Scene2DNode::kLockAll)) == 0)
                  targets.push_back(l.id);
            if (targets.empty())
            {
               sGroupPickAsk = ImGui::GetTime();
               return;
            }
            PushUndoCheckpoint(sTool == kEraser ? "Erase" : "Retouch");
            BrushEngine::Brush b = ToolBrush();
            if (spotHeal)
               b.mode = BrushEngine::kInpaint;
            if (sTool == kColourReplace || sTool == kBgErase)
            {
               float k[3] = { 0, 0, 0 };
               if (doc->Pick(at.x, at.y, k, 1, sAllLayers ? 0 : targets[0]))
                  std::copy(k, k + 3, b.key);
            }
            if (cloning)
            {
               if (!sCloneOffsetSet || !sCloneAligned)
               {
                  sCloneOffset = ImVec2(sCloneSrc.x - at.x, sCloneSrc.y - at.y);
                  sCloneAnchor = at;
                  sCloneOffsetSet = true;
               }
               b.sourceOffset[0] = sCloneOffset.x;
               b.sourceOffset[1] = sCloneOffset.y;
               b.sourceAnchor[0] = sCloneAnchor.x;
               b.sourceAnchor[1] = sCloneAnchor.y;
               b.sampleAll = sCloneAll;
            }
            const ImVec2 from = line && sHaveLast ? sLastPaint : at;
            const float pen = PenNow(doc);
            if (!doc->BeginPaint(targets[0], 0, EraseLocked(doc, targets[0], 0, b), from.x, from.y, pen))
               return;
            sOp = Op();
            sOp.kind = 7;
            sOp.docStart = at;
            sGroupStrokeIds.assign(targets.begin() + 1, targets.end());
            sGroupStrokePts.clear();
            if (!sGroupStrokeIds.empty())
            {
               sGroupStrokeBrush = b;
               sGroupStrokePts.push_back({ from.x, from.y, pen });
               if (line && sHaveLast)
                  sGroupStrokePts.push_back({ at.x, at.y, pen });
            }
            if (line && sHaveLast)
               doc->PaintTo(at.x, at.y, pen);
            return;
         }
         const bool live = a != nullptr && (a->kind == Kind::Node || a->kind == Kind::Text || a->kind == Kind::Vector) &&
                           !(a->kind == Kind::Node && a->HasSkin());
         if (live && !sEditMask && sTool == kEraser)
         {
            PushUndoCheckpoint("Erase");
            if (!a->HasPixelMask())
               doc->AddPixelMask(a->id);
            BrushEngine::Brush b = ToolBrush();
            b.mode = BrushEngine::kNormal;
            b.color[0] = b.color[1] = b.color[2] = 0.0f;
            if (!doc->BeginPaint(a->id, 1, b, at.x, at.y, PenNow(doc)))
               return;
            sOp = Op();
            sOp.kind = 7;
            sOp.docStart = at;
            return;
         }
         if (live && !sEditMask && sTool == kClone)
            cloneAbove = true;
         else if (live && !sEditMask)
         {
            PushUndoCheckpoint("Rasterise a copy");
            RasterCopy(doc, a->id);
         }
         a = Active(doc);
         if (!cloneAbove && (a == nullptr || (a->kind != Kind::Paint && !(sEditMask && a->HasPixelMask()))))
            return;
      }
      PushUndoCheckpoint(sTool == kEraser ? "Erase" : RetouchTool(sTool) ? "Retouch" : "Paint");
      int target = 0;
      const int id = PaintTarget(doc, target, painter || cloneAbove);
      if (id == 0)
         return;
      BrushEngine::Brush b = ToolBrush();
      if (spotHeal)
         b.mode = BrushEngine::kInpaint;
      if (sTool == kColourReplace || sTool == kBgErase)
      {
         // The colour under the first dab is the one replaced (or erased).
         float k[3] = { 0, 0, 0 };
         if (doc->Pick(at.x, at.y, k, 1, sAllLayers ? 0 : id))
            std::copy(k, k + 3, b.key);
      }
      if (target == 0 && sTool == kBrush)
         if (Layer* L = doc->FindLayer(id))
         {
            const bool empty = std::all_of(L->tiles.begin(), L->tiles.end(), [](const std::string& k) { return k == "-"; });
            // A relief preset makes its layer a relief (a new one above when this one already holds flat paint).
            if (b.relief >= 0 && !L->relief)
            {
               if (!empty)
               {
                  const int nid = doc->AddPaintLayer(id);
                  SelectOnly(nid);
                  L = doc->FindLayer(nid);
               }
               L->relief = true;
               L->reliefMat = b.relief;
               if (L->name.empty())
                  L->name = T("Relief");
               doc->Commit();
               Touch();
            }
            // Marbling floats on a bath: an empty layer fills with the background colour first.
            if (BrushEngine::MarbleMode(b.mode) && empty)
            {
               doc->Bucket(L->id, at.x, at.y, sBg, 1.0f, 255, false, false);
               if (L->name.empty())
                  L->name = T("Marbling");
            }
            if (L->id != id)
            {
               const int nid = L->id;
               std::copy(sFg, sFg + 3, b.color);
               if (sBrushHue != 0.0f)
                  HueTurn(b.color, sBrushHue);
               const ImVec2 from = line && sHaveLast ? sLastPaint : at;
               if (!doc->BeginPaint(nid, 0, b, from.x, from.y, PenNow(doc)))
                  return;
               sOp = Op();
               sOp.kind = 7;
               sOp.docStart = at;
               return;
            }
         }
      if (cloning)
      {
         // Aligned: the first stroke after Alt-click fixes the offset; later strokes keep it.
         if (!sCloneOffsetSet || !sCloneAligned)
         {
            sCloneOffset = ImVec2(sCloneSrc.x - at.x, sCloneSrc.y - at.y);
            sCloneAnchor = at;
            sCloneOffsetSet = true;
         }
         b.sourceOffset[0] = sCloneOffset.x;
         b.sourceOffset[1] = sCloneOffset.y;
         b.sourceAnchor[0] = sCloneAnchor.x;
         b.sourceAnchor[1] = sCloneAnchor.y;
         b.sampleAll = sCloneAll || cloneAbove;   // a new layer above has nothing of its own to clone
      }
      std::copy(sFg, sFg + 3, b.color);
      if (sTool == kBrush && sBrushHue != 0.0f)
         HueTurn(b.color, sBrushHue);
      b = EraseLocked(doc, id, target, b);
      const ImVec2 from = line && sHaveLast ? sLastPaint : at;
      if (!doc->BeginPaint(id, target, b, from.x, from.y, PenNow(doc)))
         return;
      if (painter && !BrushEngine::LiquifyMode(b.mode))
         NoteColourUsed(sFg);
      sOp = Op();
      sOp.kind = 7;
      sOp.docStart = at;
      if (line && sHaveLast)
         doc->PaintTo(at.x, at.y, PenNow(doc));
   }

   void EndStroke(Scene2DNode* doc)
   {
      sSurfaceStroke = false;
      if (doc->Painting())
         doc->EndPaint();
      if (!sGroupStrokeIds.empty() && !sGroupStrokePts.empty())
      {
         for (int gid : sGroupStrokeIds)
         {
            if (doc->BeginPaint(gid, 0, EraseLocked(doc, gid, 0, sGroupStrokeBrush), sGroupStrokePts[0].x, sGroupStrokePts[0].y, sGroupStrokePts[0].p))
            {
               for (size_t i = 1; i < sGroupStrokePts.size(); ++i)
                  doc->PaintTo(sGroupStrokePts[i].x, sGroupStrokePts[i].y, sGroupStrokePts[i].p);
               doc->EndPaint();
            }
         }
      }
      sGroupStrokeIds.clear();
      sGroupStrokePts.clear();
      sLastPaint = sMouseDoc;
      sHaveLast = true;
   }

   void FillAt(Scene2DNode* doc, ImVec2 at)
   {
      if (PaintRefused(doc))
         return;
      const size_t undo0 = gUndoStack.size();
      PushUndoCheckpoint("Fill");
      // The matrix: on a shape the bucket sets its fill, on text its colour (the live layer stays live).
      if (Layer* a = Active(doc); a != nullptr && !doc->quickMask && !sEditMask)
      {
         if (a->kind == Kind::Text)
         {
            std::copy(sFg, sFg + 3, a->textC);
            a->textC[3] = 1.0f;
            doc->Commit();
            Touch();
            NoteColourUsed(sFg);
            return;
         }
         if (a->kind == Kind::Vector)
         {
            float bx = 0, by = 0;
            doc->DocToBox(*a, at.x, at.y, bx, by);
            for (int i = (int)a->shapes.size() - 1; i >= 0; i--)
            {
               float lo[2], hi[2];
               if (Scene2DNode::ShapeBounds(a->shapes[i], lo, hi) && bx >= lo[0] && bx <= hi[0] && by >= lo[1] && by <= hi[1] &&
                   (a->shapes[i].type == Scene2DNode::Shape::kSvg || Scene2DNode::ShapeHit(a->shapes[i], bx, by, 0.0f)))
               {
                  Scene2DNode::Shape& sh = a->shapes[i];
                  sh.fillOn = true;
                  sh.fillType = 0;
                  std::copy(sFg, sFg + 3, sh.fillC);
                  sh.fillC[3] = 1.0f;
                  doc->Commit();
                  Touch();
                  NoteColourUsed(sFg);
                  return;
               }
            }
         }
      }
      // A node, text or vector layer (or none) has no pixels to flood: the region is found in the picture and
      // filled on a new paint layer above (Affinity's Source: current and below). Nothing landed, no layer.
      const Layer* was = Active(doc);
      const bool fresh = !doc->quickMask && !(sEditMask && was != nullptr && was->HasPixelMask()) &&
                         (was == nullptr || was->kind != Kind::Paint);
      const int wasId = was != nullptr ? was->id : 0;   // the layer list may move when a layer is added
      int target = 0;
      const int id = PaintTarget(doc, target, true);
      if (fresh && id > 0 && target == 0)
      {
         if (doc->Bucket(id, at.x, at.y, sFg, 1.0f, sTolerance, sContiguous, true))
            NoteColourUsed(sFg);
         else
         {
            doc->RemoveLayer(id);
            SelectOnly(wasId);
            gUndoStack.resize(undo0);   // nothing landed: no step
         }
         Touch();
         return;
      }
      bool landed = false;
      if (target == 2)
      {
         // Quick mask: the bucket fills the mask, the foreground's lightness as the amount (black masks, white selects).
         landed = doc->BucketSelection(at.x, at.y, 1.0f - (sFg[0] + sFg[1] + sFg[2]) / 3.0f < 0.5f ? 1.0f : 0.0f, sTolerance, sContiguous);
         Touch();
      }
      else if (id > 0 && target == 0)
         landed = doc->Bucket(id, at.x, at.y, sFg, 1.0f, sTolerance, sContiguous, sAllLayers);
      if (!landed)
      {
         gUndoStack.resize(undo0);   // off the layer, or hidden by the selection: no step
         return;
      }
      NoteColourUsed(sFg);
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
      if (target == 2)
      {
         doc->GradientSelection(a.x, a.y, b.x, b.y, (c0[0] + c0[1] + c0[2]) / 3.0f, (c1[0] + c1[1] + c1[2]) / 3.0f, sFillKind);
         Touch();
      }
      else if (id > 0 && target == 0)
         doc->Gradient(id, a.x, a.y, b.x, b.y, ca, cb, sFillKind, sGradOpacity, sGradExtend, sGradDither);
   }

   void ClosePoly(Scene2DNode* doc)
   {
      if (sPoly.size() >= tok::view2d_close_poly_limit)
      {
         SelStep("Lasso");
         doc->SelectPolygon(sPoly, sPolyOp, sFeather, true);
         TargetSelection(doc);
      }
      sPoly.clear();
   }

   // Recent colours: the last ten painted or filled with, newest first (this session). Swatches: the ones kept,
   // saved with the panel settings as "rrggbb,rrggbb,...".
   struct Rgb
   {
      float c[3];
   };
   std::vector<Rgb> sRecentCols;

   bool SameColour(const float* a, const float* b)
   {
      return std::fabs(a[0] - b[0]) < 0.002f && std::fabs(a[1] - b[1]) < 0.002f && std::fabs(a[2] - b[2]) < 0.002f;
   }

   void NoteColourUsed(const float* c)
   {
      sRecentCols.erase(std::remove_if(sRecentCols.begin(), sRecentCols.end(), [&](const Rgb& r) { return SameColour(r.c, c); }),
                        sRecentCols.end());
      sRecentCols.insert(sRecentCols.begin(), Rgb{ { c[0], c[1], c[2] } });
      if (sRecentCols.size() > tok::view2d_note_colour_used_limit)
         sRecentCols.resize(10);
   }

   std::vector<Rgb> Swatches()
   {
      std::vector<Rgb> out;
      size_t at = 0;
      while (at + tok::view2d_swatches_gap <= gView2DSwatches.size())
      {
         unsigned v = 0;
         if (std::sscanf(gView2DSwatches.c_str() + at, "%6x", &v) == 1)
            out.push_back(Rgb{ { ((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f } });
         at += 7;
      }
      return out;
   }

   void SetSwatches(const std::vector<Rgb>& list)
   {
      std::string s;
      char hex[8];
      for (const Rgb& r : list)
      {
         auto byte = [](float f) { return (unsigned)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
         std::snprintf(hex, sizeof(hex), "%s%s%s", UiFmt::Hex2(byte(r.c[0])).c, UiFmt::Hex2(byte(r.c[1])).c, UiFmt::Hex2(byte(r.c[2])).c);
         s += (s.empty() ? "" : ",") + std::string(hex);
      }
      gView2DSwatches = s;
   }

   // A row of colour chips; a click returns its index, a right click its index in `removed`.
   int SwatchStrip(const char* id, const std::vector<Rgb>& list, bool addChip, int* removed)
   {
      const float sq = tok::view2d_sq, gap = tok::space_0_75;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      int picked = -1;
      ImGui::PushID(id);
      const int n = (int)list.size() + (addChip ? 1 : 0);
      for (int i = 0; i < n; i++)
      {
         if (i > 0 && i % 10 != 0)
            FormParts::Inline(0.0f, gap);
         ImGui::PushID(i);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         UiInteract::Hit("##chip", ImVec2(sq, sq));
         if (i < (int)list.size())
         {
            const float* c = list[i].c;
            ColourChip::Swatch(dl, p, sq, c, ImGui::IsItemHovered());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
               picked = i;
            if (removed != nullptr && MenuParts::ContextClicked())
               *removed = i;
         }
         else
         {
            const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
            const ImVec2 c(p.x + sq * 0.5f, p.y + sq * 0.5f);
            dl->AddLine(ImVec2(c.x - tok::space_1, c.y), ImVec2(c.x + tok::space_1, c.y), ink, tok::view2d_swatch_strip_stroke);
            dl->AddLine(ImVec2(c.x, c.y - tok::space_1), ImVec2(c.x, c.y + tok::space_1), ink, tok::view2d_swatch_strip_stroke);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
               picked = i;
            ColourChip::Swatch(dl, p, sq, nullptr, ImGui::IsItemHovered());
         }
         ImGui::PopID();
      }
      ImGui::PopID();
      return picked;
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
   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
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
         const ImU32 col = pass == 0 ? tok::U32(tok::canvas_shade, tok::view2d_tool_cursor_alpha_sm) : tok::U32(tok::canvas_ink);
         const float th = pass == 0 ? 3.0f : 1.0f;
         dl->AddLine(ImVec2(mouse.x - tok::space_2, mouse.y), ImVec2(mouse.x - tok::space_0_5, mouse.y), col, th);
         dl->AddLine(ImVec2(mouse.x + tok::space_0_5, mouse.y), ImVec2(mouse.x + tok::space_2, mouse.y), col, th);
         dl->AddLine(ImVec2(mouse.x, mouse.y - tok::space_2), ImVec2(mouse.x, mouse.y - tok::space_0_5), col, th);
         dl->AddLine(ImVec2(mouse.x, mouse.y + tok::space_0_5), ImVec2(mouse.x, mouse.y + tok::space_2), col, th);
      }
      const ImVec2 badge(mouse.x + tok::view2d_tool_cursor_gap, mouse.y + tok::view2d_tool_cursor_gap);
      for (int dx = -1; dx <= 1; dx++)
         for (int dy = -1; dy <= 1; dy++)
            if (dx != 0 || dy != 0)
               glyph::Draw(dl, ImVec2(badge.x + dx, badge.y + dy), tok::icon_control, tok::U32(tok::canvas_shade, tok::view2d_tool_cursor_alpha_lg), g);
      glyph::Draw(dl, badge, tok::icon_control, tok::U32(tok::canvas_ink), g);
   }
   // ui-content-end

   // Predicted points: the last two pen samples carried 8 ms ahead and drawn in the paint colour, so the line
   // reaches the pen instead of trailing it by a frame. Only a picture: the next real sample replaces it.
   float sPen[2][3] = {};   // doc x, y, ms; [1] is the newest
   int sPenN = 0;
   void NotePenSample(ImVec2 at)
   {
      const float ms = (float)(ImGui::GetTime() * 1000.0);
      if (sPenN > 0 && at.x == sPen[1][0] && at.y == sPen[1][1])
         return;
      std::copy(sPen[1], sPen[1] + 3, sPen[0]);
      sPen[1][0] = at.x;
      sPen[1][1] = at.y;
      sPen[1][2] = ms;
      sPenN = std::min(sPenN + 1, 2);
   }

   void DrawPredicted(Scene2DNode* doc, ImDrawList* dl)
   {
      const BrushEngine::Brush& b = sBrush;
      float to[2];
      if (sPenN < 2 || b.stabiliser > 0 || b.smoothing > 0.0f
          || !BrushEngine::Stroke::Predict(sPen[0], sPen[1], 8.0f, std::max(tok::view2d_predicted_min, b.size * tok::view2d_predicted_ratio), to))   // ui-audit-ok: model value (prediction lookahead samples / handle index modulo / visible items)
         return;
      const ImVec2 a = ToScreen(doc, sPen[1][0], sPen[1][1]), c = ToScreen(doc, to[0], to[1]);
      const float w = std::max(1.0f, b.size * sZoom);
      const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(sFg[0], sFg[1], sFg[2], tok::view2d_predicted_y * b.opacity));
      dl->PushClipRect(sViewMn, sViewMx, true);
      dl->AddLine(a, c, col, w);
      dl->AddCircleFilled(c, w * 0.5f, col);
      dl->PopClipRect();
   }

   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   void DrawBrushCursor(ImDrawList* dl, ImVec2 mouse)
   {
      const BrushEngine::Brush& b = ToolBrush();
      const float r = b.size * 0.5f * sZoom;
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      auto ring = [&](float rr, float alpha)
      {
         dl->AddCircle(mouse, rr, IM_COL32(0, 0, 0, (int)(tok::view2d_brush_cursor_ratio * alpha)), tok::view2d_brush_cursor_segments, tok::view2d_brush_cursor_stroke);
         dl->AddCircle(mouse, rr, tok::U32(tok::view2d_ring_shade, (int)(tok::view2d_brush_cursor_shade * alpha)), tok::view2d_brush_cursor_segments, 1.0f);
      };
      if (r < tok::view2d_brush_cursor_limit)
      {
         dl->AddLine(ImVec2(mouse.x - tok::space_1_5, mouse.y), ImVec2(mouse.x + tok::space_1_5, mouse.y), tok::U32(tok::canvas_shade, tok::view2d_line_alpha_md), tok::view2d_brush_cursor_stroke);
         dl->AddLine(ImVec2(mouse.x, mouse.y - tok::space_1_5), ImVec2(mouse.x, mouse.y + tok::space_1_5), tok::U32(tok::canvas_shade, tok::view2d_line_alpha_md), tok::view2d_brush_cursor_stroke);
         dl->AddLine(ImVec2(mouse.x - tok::space_1_5, mouse.y), ImVec2(mouse.x + tok::space_1_5, mouse.y), tok::U32(tok::canvas_ink));
         dl->AddLine(ImVec2(mouse.x, mouse.y - tok::space_1_5), ImVec2(mouse.x, mouse.y + tok::space_1_5), tok::U32(tok::canvas_ink));
         return;
      }
      ring(r, 1.0f);
      if (!b.pencil && b.hardness < 0.95f && r * b.hardness > 3.0f)
         ring(r * b.hardness, 0.45f);
   }
   // ui-content-end

   // Quick mask is part of the document, so switching it is an undo step.
   void ToggleQuickMask(Scene2DNode* doc)
   {
      PushUndoCheckpoint("Quick mask");
      doc->quickMask = !doc->quickMask;
      doc->Commit();
      Touch();
   }

   // Keys for the pixel tools. Returns true when it used the key.
   void HandlePixelKeys(Scene2DNode* doc, bool cmd, bool shift)
   {
      auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
      auto rep = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
      if (!cmd)
      {
         if (!shift && key(ImGuiKey_Q)) ToggleQuickMask(doc);
         if (!shift && key(ImGuiKey_X)) SwapColours();
         if (!shift && key(ImGuiKey_D)) DefaultColours();
         if (BrushTool(sTool))
         {
            BrushEngine::Brush& b = ToolBrush();
            if (!shift && rep(ImGuiKey_LeftBracket)) b.size = std::max(1.0f, b.size * tok::view2d_pixel_keys_ratio - 1.0f);
            if (!shift && rep(ImGuiKey_RightBracket)) b.size = std::min(tok::view2d_pixel_keys_max_lg, b.size * tok::view2d_pixel_keys_max_sm + 1.0f);
            if (shift && rep(ImGuiKey_LeftBracket)) b.hardness = std::max(0.0f, b.hardness - 0.25f);
            if (shift && rep(ImGuiKey_RightBracket)) b.hardness = std::min(1.0f, b.hardness + 0.25f);
            static const ImGuiKey kDigits[10] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
                                                  ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9, ImGuiKey_0 };
            for (int i = 0; i < 10; i++)
               if (key(kDigits[i]))
                  b.opacity = (i + 1) / tok::view2d_opacity_ratio;
         }
      }
      if (cmd && !shift && key(ImGuiKey_D) && doc->HasSelection())
      {
         SelStep("Deselect");
         doc->Deselect();
      }
      if (cmd && shift && key(ImGuiKey_I))
      {
         SelStep("Invert selection");
         doc->InvertSelection();
      }
      if (sTool == kLasso && !sPoly.empty())
      {
         if (key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter)) ClosePoly(doc);
         if (key(ImGuiKey_Escape)) sPoly.clear();
      }
   }

   // ---- canvas ----------------------------------------------------------

   // The 8 handles of one layer's box, as box fractions.
   const float kHandleAt[8][2] = { { 0, 0 }, { 0.5f, 0 }, { 1, 0 }, { 1, 0.5f }, { 1, 1 }, { 0.5f, 1 }, { 0, 1 }, { 0, 0.5f } };

   // What the transform box frames, in the layer's own box px: x0, y0, x1, y1. A paint layer's box is its painted
   // pixels (transparent ones don't count, as in Photoshop), so a small cut-out on a big layer gets a small box. Any
   // other layer, or an empty / warped / 3D / cropped one, is its whole box. Kept per layer and tiles (reads back).
   // Marching ants drawn on the screen, 1 px at any zoom (Photoshop, Affinity): the selection's edge in the visible part
   // of the document, joined into runs, drawn white with black dashes that march. False when the texture overlay
   // should draw instead (zoomed out, a live selection-brush stroke, or an edge too busy to draw as lines).
   // Menu actions defined further down, used by the canvas menu.
   void TransformSubMenu(Scene2DNode* doc);
   void CropToSelection(Scene2DNode* doc);
   void TrimDocument(Scene2DNode* doc);
   void SelectLayerPixels(Scene2DNode* doc, int id, int op);

   bool DrawScreenAnts(ImDrawList* dl, Scene2DNode* doc)
   {
      if (sZoom < 1.5f || doc->quickMask || doc->SelectionStrokeLive())
         return false;
      const auto& d = doc->Doc();
      const std::vector<uint8_t>& sel = doc->SelectionPixels();
      if (sel.size() != (size_t)d.w * d.h)
         return false;
      const ImVec2 c0 = ToDoc(doc, sViewMn), c1 = ToDoc(doc, sViewMx), c2 = ToDoc(doc, ImVec2(sViewMn.x, sViewMx.y)),
                   c3 = ToDoc(doc, ImVec2(sViewMx.x, sViewMn.y));
      const int x0 = std::max(0, (int)std::floor(std::min({ c0.x, c1.x, c2.x, c3.x })) - 1);
      const int x1 = std::min(d.w, (int)std::ceil(std::max({ c0.x, c1.x, c2.x, c3.x })) + 1);
      const int y0 = std::max(0, (int)std::floor(std::min({ c0.y, c1.y, c2.y, c3.y })) - 1);
      const int y1 = std::min(d.h, (int)std::ceil(std::max({ c0.y, c1.y, c2.y, c3.y })) + 1);
      if (x1 <= x0 || y1 <= y0)
         return true;
      // in(x, y): document px, y from the top; outside the document counts as unselected.
      auto in = [&](int x, int y) {
         return x >= 0 && y >= 0 && x < d.w && y < d.h && sel[(size_t)(d.h - 1 - y) * d.w + x] >= 128;
      };
      struct Seg { float ax, ay, bx, by; };
      static std::vector<Seg> segs;
      segs.clear();
      constexpr size_t kMaxSegs = 40000;  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
      for (int y = y0; y <= y1 && segs.size() < kMaxSegs; y++)   // horizontal edges, between rows y-1 and y
         for (int x = x0; x < x1;)
         {
            if (in(x, y - 1) == in(x, y))
            {
               x++;
               continue;
            }
            const int s0 = x;
            while (x < x1 && in(x, y - 1) != in(x, y))
               x++;
            segs.push_back({ (float)s0, (float)y, (float)x, (float)y });
         }
      for (int x = x0; x <= x1 && segs.size() < kMaxSegs; x++)   // vertical edges, between columns x-1 and x
         for (int y = y0; y < y1;)
         {
            if (in(x - 1, y) == in(x, y))
            {
               y++;
               continue;
            }
            const int s0 = y;
            while (y < y1 && in(x - 1, y) != in(x, y))
               y++;
            segs.push_back({ (float)x, (float)s0, (float)x, (float)y });
         }
      if (segs.size() >= kMaxSegs)
         return false;
      const ImU32 white = tok::U32(tok::canvas_ink), black = tok::U32(tok::canvas_shade);
      constexpr float kDash = tok::view2d_dash;
      const float march = std::fmod((float)ImGui::GetTime() * tok::motion_rate_ants, kDash * 2.0f);  // ui-audit-ok: count (dash period: two dashes)
      for (const Seg& sg : segs)
      {
         const ImVec2 a = ToScreen(doc, sg.ax, sg.ay), b = ToScreen(doc, sg.bx, sg.by);
         dl->AddLine(a, b, white, tok::view2d_screen_ants_stroke);
         const float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
         if (len < 0.5f)
            continue;
         const ImVec2 u((b.x - a.x) / len, (b.y - a.y) / len);
         // The dash phase follows the screen position, so runs that meet keep one pattern.
         float t = -std::fmod(a.x + a.y + march, kDash * 2.0f);
         for (; t < len; t += kDash * 2.0f)
         {
            const float t0 = std::max(0.0f, t), t1 = std::min(len, t + kDash);
            if (t1 > t0)
               dl->AddLine(ImVec2(a.x + u.x * t0, a.y + u.y * t0), ImVec2(a.x + u.x * t1, a.y + u.y * t1), black, tok::view2d_screen_ants_stroke);
         }
      }
      return true;
   }

   void ContentRect(Scene2DNode* doc, const Layer& l, float r[4])
   {
      float bw = 1, bh = 1;
      doc->LayerBox(l, bw, bh);
      r[0] = 0, r[1] = 0, r[2] = bw, r[3] = bh;
      if (l.kind != Kind::Paint || l.warpOn || l.is3d || l.cropOn)
         return;
      struct Entry { int id; std::vector<std::string> tiles; bool has; float r[4]; };
      static std::vector<Entry> cache;
      for (const Entry& e : cache)
         if (e.id == l.id && e.tiles == l.tiles)
         {
            if (e.has)
               std::copy(e.r, e.r + 4, r);
            return;
         }
      Entry e{ l.id, l.tiles, false, { 0, 0, bw, bh } };
      std::vector<uint8_t> px;
      int w = 0, h = 0;
      if (doc->ReadPaint(l.id, px, w, h) && w > 0 && h > 0 && px.size() >= (size_t)w * h * 4)  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
      {
         int x0 = w, y0 = h, x1 = -1, y1 = -1;
         for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
               if (px[((size_t)y * w + x) * 4 + 3] > 8)
               {
                  x0 = std::min(x0, x); x1 = std::max(x1, x);
                  y0 = std::min(y0, y); y1 = std::max(y1, y);
               }
         if (x1 >= x0)
         {
            // Rows come bottom first; the box runs top down.
            const float kx = bw / (float)w, ky = bh / (float)h;
            e.r[0] = x0 * kx, e.r[2] = (x1 + 1) * kx;
            e.r[1] = (h - 1 - y1) * ky, e.r[3] = (h - y0) * ky;
            e.has = true;
         }
      }
      cache.erase(std::remove_if(cache.begin(), cache.end(), [&](const Entry& c) { return c.id == l.id; }), cache.end());
      if (cache.size() > tok::view2d_content_rect_limit)
         cache.erase(cache.begin());
      cache.push_back(e);
      if (e.has)
         std::copy(e.r, e.r + 4, r);
   }

   // The content rect's four corners on the document (clockwise from top-left), for the outline and the rotate zone.
   void ContentCorners(Scene2DNode* doc, const Layer& l, float c[8])
   {
      float r[4], m[6], bw = 1, bh = 1;
      ContentRect(doc, l, r);
      doc->LayerBox(l, bw, bh);
      if (r[0] == 0 && r[1] == 0 && r[2] == bw && r[3] == bh)
      {
         doc->Corners(l, c);
         return;
      }
      doc->LayerMatrix(l, m);
      const float pts[8] = { r[0], r[1], r[2], r[1], r[2], r[3], r[0], r[3] };
      for (int i = 0; i < 4; i++)
         Apply(m, pts[i * 2], pts[i * 2 + 1], c[i * 2], c[i * 2 + 1]);
   }

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
      float r[4], m[6], x, y;
      ContentRect(doc, l, r);
      doc->LayerMatrix(l, m);
      Apply(m, r[0] + fx * (r[2] - r[0]), r[1] + fy * (r[3] - r[1]), x, y);
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
         if (dx * dx + dy * dy <= tok::view2d_hover_handle_ratio * 22.0f)
         {
            float c[8];
            ContentCorners(doc, l, c);
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
      if (FreeTransformOn(doc) && (kind == 1 || kind == 2 || kind == 3 || kind == 20 || kind == 21))
         undo = nullptr;   // the free transform's one step covers it
      if (undo != nullptr)
      {
         sOp.undo0 = gUndoStack.size();
         PushUndoCheckpoint(undo);
      }
      for (int id : MovingIds(doc))
         if (const Layer* l = doc->FindLayer(id))
            sOp.before.push_back(*l);
      if (Layer* a = Active(doc))
         sOp.l0 = *a;
   }

   // A group or several layers picked: one upright box around them all, scaled (20) and turned (21) as one, like
   // Photoshop's Free Transform on a group or a multi-layer selection.
   bool GroupTarget(Scene2DNode* doc)
   {
      const Layer* a = Active(doc);
      if (sTool != kMove || a == nullptr || (sSel.size() == 1 && a->kind != Kind::Group))
         return false;
      return !MovingIds(doc).empty();
   }

   // The outermost movable group a layer sits in, or the layer itself.
   int OuterGroup(Scene2DNode* doc, int id)
   {
      int out = id;
      for (const Layer* up = doc->FindLayer(id); up != nullptr && up->parent != 0;)
      {
         up = doc->FindLayer(up->parent);
         if (up != nullptr && up->kind == Kind::Group && Movable(doc, *up))
            out = up->id;
      }
      return out;
   }

   ImVec2 GroupHandlePoint(Scene2DNode* doc, const float lo[2], const float hi[2], int i)
   {
      return ToScreen(doc, lo[0] + (hi[0] - lo[0]) * kHandleAt[i][0], lo[1] + (hi[1] - lo[1]) * kHandleAt[i][1]);
   }

   int HoverGroupHandle(Scene2DNode* doc, ImVec2 mouse)
   {
      float lo[2], hi[2];
      if (!SelBounds(doc, lo, hi))
         return -1;
      for (int i = 0; i < 8; i++)
      {
         const ImVec2 p = GroupHandlePoint(doc, lo, hi, i);
         if (std::fabs(mouse.x - p.x) <= kHandle && std::fabs(mouse.y - p.y) <= kHandle)
            return i;
      }
      const ImVec2 md = ToDoc(doc, mouse);
      const bool inside = md.x >= lo[0] && md.x <= hi[0] && md.y >= lo[1] && md.y <= hi[1];
      for (int i = 0; i < 8; i += 2)
      {
         const ImVec2 p = GroupHandlePoint(doc, lo, hi, i);
         const float dx = mouse.x - p.x, dy = mouse.y - p.y;
         if (!inside && dx * dx + dy * dy <= tok::view2d_hover_group_handle_ratio * 22.0f)
            return tok::view2d_hover_group;
      }
      return -1;
   }

   // One layer of the group, put back from its start state: its box centre goes where the group's move takes it,
   // its own scale and angle take the rest.
   void PlaceInGroup(Scene2DNode* doc, Layer& l, const Layer& b, float kx, float ky, float turnDeg, ImVec2 fix)
   {
      float bw = 1, bh = 1, m0[6], px, py;
      doc->LayerBox(b, bw, bh);
      doc->LayerMatrix(b, m0);
      Apply(m0, bw * 0.5f, bh * 0.5f, px, py);
      const float r = turnDeg * kPi / tok::view2d_r_ratio, cs = std::cos(r), sn = std::sin(r);
      const float qx = fix.x + ((px - fix.x) * cs - (py - fix.y) * sn) * kx;
      const float qy = fix.y + ((px - fix.x) * sn + (py - fix.y) * cs) * ky;
      l = b;
      // A layer turned a quarter takes the group's width scale on its height.
      const bool quarter = std::fabs(std::fabs(std::remainder(b.angle, 180.0f)) - 90.0f) < 45.0f;
      l.sx = b.sx * (quarter ? ky : kx);
      l.sy = b.sy * (quarter ? kx : ky);
      l.angle = std::fmod(b.angle + turnDeg + 540.0f, 360.0f) - 180.0f;
      float m1[6], nx, ny;
      doc->LayerMatrix(l, m1);
      Apply(m1, bw * 0.5f, bh * 0.5f, nx, ny);
      l.x += qx - nx;
      l.y += qy - ny;
   }

   void ScaleGroup(Scene2DNode* doc, ImVec2 mouse)
   {
      const float* g = sOp.g0;
      const float w = std::max(g[2] - g[0], tok::view2d_scale_group_min), h = std::max(g[3] - g[1], tok::view2d_scale_group_min);
      const float ox = sOp.fromCentre ? 0.5f : 1.0f - sOp.hx, oy = sOp.fromCentre ? 0.5f : 1.0f - sOp.hy;
      const ImVec2 md = ToDoc(doc, mouse);
      const float bx = (md.x - g[0]) / w, by = (md.y - g[1]) / h;
      float kx = std::fabs(sOp.hx - ox) > 1e-3f ? (bx - ox) / (sOp.hx - ox) : 1.0f;
      float ky = std::fabs(sOp.hy - oy) > 1e-3f ? (by - oy) / (sOp.hy - oy) : 1.0f;
      const bool corner = std::fabs(sOp.hx - 0.5f) > 0.1f && std::fabs(sOp.hy - 0.5f) > 0.1f;
      if (corner && !ImGui::GetIO().KeyShift)
         kx = ky = std::fabs(kx) > std::fabs(ky) ? kx : ky;
      if (!corner && std::fabs(sOp.hx - 0.5f) < 0.1f) kx = 1.0f;
      if (!corner && std::fabs(sOp.hy - 0.5f) < 0.1f) ky = 1.0f;
      kx = std::fabs(kx) < 1e-3f ? 1e-3f : kx;
      ky = std::fabs(ky) < 1e-3f ? 1e-3f : ky;
      const ImVec2 fix(g[0] + ox * w, g[1] + oy * h);
      for (const Layer& b : sOp.before)
         if (Layer* l = doc->FindLayer(b.id))
            PlaceInGroup(doc, *l, b, kx, ky, 0.0f, fix);
      Touch();
   }

   void RotateGroup(Scene2DNode* doc, ImVec2 mouse)
   {
      const float* g = sOp.g0;
      const ImVec2 cd((g[0] + g[2]) * 0.5f, (g[1] + g[3]) * 0.5f), c = ToScreen(doc, cd.x, cd.y);
      const float a0 = std::atan2(sOp.start.y - c.y, sOp.start.x - c.x), a1 = std::atan2(mouse.y - c.y, mouse.x - c.x);
      float turn = (a1 - a0) * 180.0f / kPi;
      if (ImGui::GetIO().KeyShift)
         turn = std::round(turn / 15.0f) * 15.0f;
      for (const Layer& b : sOp.before)
         if (Layer* l = doc->FindLayer(b.id))
            PlaceInGroup(doc, *l, b, 1.0f, 1.0f, turn, cd);
      Touch();
   }

   // Flip (about the picked layers' middle) and quarter turns, for one layer or many. A mirrored box is the same box
   // with its x (or y) scale negated and its angle negated; pulled corners mirror with it.
   bool Flippable(const Layer& l) { return l.kind != Kind::Group && l.kind != Kind::Fill && l.kind != Kind::Adjust; }

   // Flip and quarter turns pivot on what the selection shows (Photoshop's transform box): a cut-out eye on a
   // document-sized layer, or a photo masked down to an eye, turns about the eye, not the whole layer.
   // Flip and turn pivot on what the layers hold (their content boxes, groups through their layers), not their
   // document-sized boxes: a cut-out eye flips where it is. Unclipped by the canvas, so two flips land back.
   bool PivotBounds(Scene2DNode* doc, float lo[2], float hi[2])
   {
      // Lifted pixels turn about the marquee's own box (PhotoCraft: the transform frame is the selection bounds).
      if (sFloat.id != 0 && !FreeTransformOn(doc) && doc->SelectionBounds(lo, hi))
         return true;
      if (FreeTransformOn(doc))
         return SelBounds(doc, lo, hi);
      lo[0] = lo[1] = 1e30f;
      hi[0] = hi[1] = -1e30f;
      bool any = false;
      std::vector<int> todo(sSel.begin(), sSel.end());
      while (!todo.empty())
      {
         const int id = todo.back();
         todo.pop_back();
         const Layer* l = doc->FindLayer(id);
         if (l == nullptr)
            continue;
         if (l->kind == Kind::Group)
         {
            for (int k : doc->ChildrenOf(id))
               todo.push_back(k);
            continue;
         }
         if (l->kind == Kind::Fill || l->kind == Kind::Adjust)
            continue;
         float c[8];
         ContentCorners(doc, *l, c);
         for (int k = 0; k < 4; k++)
         {
            lo[0] = std::min(lo[0], c[k * 2]), hi[0] = std::max(hi[0], c[k * 2]);
            lo[1] = std::min(lo[1], c[k * 2 + 1]), hi[1] = std::max(hi[1], c[k * 2 + 1]);
         }
         any = true;
      }
      return any ? true : SelBounds(doc, lo, hi);
   }

   void FlipSelection(Scene2DNode* doc, bool vertical)
   {
      bool lift = !FreeTransformOn(doc) && CanLift(doc);
      if (lift)
      {
         const size_t undo0 = gUndoStack.size();
         PushUndoCheckpoint(vertical ? "Flip vertical" : "Flip horizontal");
         lift = LiftSelection(doc);
         if (!lift)
            gUndoStack.resize(undo0);   // nothing lifted: the whole layer turns, under its own step
      }
      float lo[2], hi[2];
      if (!PivotBounds(doc, lo, hi))
      {
         DropFloat(doc);
         return;
      }
      const float px = (lo[0] + hi[0]) * 0.5f, py = (lo[1] + hi[1]) * 0.5f;
      if (!FreeTransformOn(doc) && !lift)
         PushUndoCheckpoint(vertical ? "Flip vertical" : "Flip horizontal");
      for (int id : MovingIds(doc))
         if (Layer* l = doc->FindLayer(id); l != nullptr && Flippable(*l))
         {
            float bw = 1, bh = 1, m[6], cx, cy;
            doc->LayerBox(*l, bw, bh);
            doc->LayerMatrix(*l, m);
            Apply(m, bw * 0.5f, bh * 0.5f, cx, cy);
            if (vertical)
               l->flipY = !l->flipY;
            else
               l->flipX = !l->flipX;
            l->angle = l->angle == 0.0f ? 0.0f : -l->angle;
            if (l->warpOn)
               for (int k = 0; k < 4; k++)
                  l->warp[k * 2 + (vertical ? 1 : 0)] = -l->warp[k * 2 + (vertical ? 1 : 0)];
            float m1[6], nx, ny;
            doc->LayerMatrix(*l, m1);
            Apply(m1, bw * 0.5f, bh * 0.5f, nx, ny);
            l->x += (vertical ? cx : tok::view2d_flip_selection_ratio * px - cx) - nx;
            l->y += (vertical ? tok::view2d_flip_selection_ratio * py - cy : cy) - ny;
         }
      if (lift)
         DropFloat(doc);
      Touch();
   }

   void TurnSelection(Scene2DNode* doc, float deg)
   {
      bool lift = !FreeTransformOn(doc) && CanLift(doc);
      if (lift)
      {
         const size_t undo0 = gUndoStack.size();
         PushUndoCheckpoint("Rotate selection");
         lift = LiftSelection(doc);
         if (!lift)
            gUndoStack.resize(undo0);   // nothing lifted: the whole layer turns, under its own step
      }
      float lo[2], hi[2];
      if (!PivotBounds(doc, lo, hi))
      {
         DropFloat(doc);
         return;
      }
      const ImVec2 pivot((lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f);
      if (!FreeTransformOn(doc) && !lift)
         PushUndoCheckpoint("Rotate layers");
      for (int id : MovingIds(doc))
         if (Layer* l = doc->FindLayer(id); l != nullptr && Flippable(*l))
         {
            const Layer b = *l;
            PlaceInGroup(doc, *l, b, 1.0f, 1.0f, deg, pivot);
            if (b.warpOn)
            {
               // Pulled corners are document offsets: they turn with the layer.
               const float r = deg * kPi / tok::view2d_r_ratio, cs = std::cos(r), sn = std::sin(r);
               for (int k = 0; k < 4; k++)
               {
                  const float wx = b.warp[k * 2], wy = b.warp[k * 2 + 1];
                  l->warp[k * 2] = wx * cs - wy * sn;
                  l->warp[k * 2 + 1] = wx * sn + wy * cs;
               }
            }
         }
      if (lift)
         DropFloat(doc);
      Touch();
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
      if (h % tok::view2d_warp_active_period == 0)
      {
         const int k = h == 0 ? 0 : h == 2 ? 1 : h == 4 ? 2 : 3;   // handle -> corner (TL TR BR BL)
         if (sOp.warpStyle == 1)
         {
            // Skew: the corner slides along whichever of its two edges the drag follows more.
            float best = 0.0f, bx = 0.0f, by = 0.0f;
            for (int o : { k ^ 1, 3 - k })
            {
               float ex = c[o * 2] - c[k * 2], ey = c[o * 2 + 1] - c[k * 2 + 1];
               const float len = std::max(1e-3f, std::sqrt(ex * ex + ey * ey));
               ex /= len;
               ey /= len;
               const float t = dx * ex + dy * ey;
               if (std::fabs(t) > std::fabs(best))
               {
                  best = t;
                  bx = ex * t;
                  by = ey * t;
               }
            }
            pull(k, bx, by);
         }
         else if (!sOp.perspective)
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
         const int a = h == 1 ? 0 : h == 3 ? 1 : h == 5 ? 2 : 3, b = (a + 1) % 4;  // ui-audit-ok: index (corner handle index)
         if (sOp.warpStyle == 2)
         {
            pull(a, dx, dy);   // distort: the edge goes where it is dragged
            pull(b, dx, dy);
            Touch();
            return;
         }
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

   // Mesh warp: a 4 x 4 grid on the active paint layer. A drag moves one point; the pixels are pulled from the layer
   // as it was when this warp began, so a second drag does not resample the first.
   // The mesh's rest grid fitted to what the layer draws (its ink box plus a margin), so the handles sit on the picture,
   // not on the page corners. Kept per layer and tiles, since finding the box reads the layer back.
   bool MeshRestFit(Scene2DNode* doc, const Layer& l, float out[32])
   {
      static int cId = 0;
      static std::vector<std::string> cTiles;
      static float cPts[32];
      if (l.kind == Kind::Paint && cId == l.id && cTiles == l.tiles)
      {
         std::copy(cPts, cPts + 32, out);
         return true;
      }
      if (l.kind != Kind::Paint)
         return doc->MeshRest(l.id, out);
      const std::vector<uint8_t>* px = LayerInk(doc, l.id);
      const int w = doc->Doc().w, h = doc->Doc().h;
      int x0 = w, y0 = h, x1 = -1, y1 = -1;
      if (px != nullptr)
         for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
               if ((*px)[((size_t)y * w + x) * 4 + 3] > 8)
               {
                  x0 = std::min(x0, x); x1 = std::max(x1, x);
                  y0 = std::min(y0, y); y1 = std::max(y1, y);
               }
      if (x1 < x0)
         return doc->MeshRest(l.id, out);
      // On the ink's bounds, as Photoshop's warp grid sits on the layer's bounds.
      const float bx0 = (float)x0, by0 = (float)y0, bx1 = (float)(x1 + 1), by1 = (float)(y1 + 1);
      for (int j = 0; j < 4; j++)
         for (int i = 0; i < 4; i++)
         {
            out[(j * 4 + i) * 2] = bx0 + (bx1 - bx0) * i / 3.0f;
            out[(j * 4 + i) * 2 + 1] = by0 + (by1 - by0) * j / 3.0f;
         }
      cId = l.id;
      cTiles = l.tiles;
      std::copy(out, out + 32, cPts);
      return true;
   }

   // A press on a handle of the mesh being pulled keeps that layer; anywhere else the target rule picks again.
   bool NearMeshHandle(Scene2DNode* doc, ImVec2 mouse)
   {
      const Layer* al = Active(doc);
      if (al == nullptr || sMeshId != al->id)
         return false;
      for (int k = 0; k < 16; k++)
      {
         const ImVec2 q = ToScreen(doc, sMeshPts[k * 2], sMeshPts[k * 2 + 1]);
         if (std::hypot(q.x - mouse.x, q.y - mouse.y) < tok::view2d_near_mesh_handle_limit)
            return true;
      }
      return false;
   }

   void MeshPress(Scene2DNode* doc, ImVec2 mouse)
   {
      // A node, text or vector layer is turned into a pixel copy to bend (the original kept, hidden), as the user
      // asked: the image, object or text becomes a mesh you pull.
      if (Layer* a = Active(doc); a != nullptr && !PaintRefused(doc) &&
                                  (a->kind == Kind::Text || a->kind == Kind::Vector || (a->kind == Kind::Node && !a->HasSkin())))
      {
         PushUndoCheckpoint("Rasterise a copy");
         RasterCopy(doc, a->id);
      }
      Layer* al = Active(doc);
      if (al == nullptr || PaintRefused(doc))
         return;
      if (sMeshId != al->id || (al->tiles != sMeshFrom && al->tiles != sMeshStored))
      {
         if (!MeshRestFit(doc, *al, sMeshPts))
            return;
         std::copy(sMeshPts, sMeshPts + 32, sMeshPts0);
         sMeshId = al->id;
         sMeshFrom = al->tiles;
         sMeshStored = al->tiles;
      }
      sMeshPoint = -1;
      float bd = 10.0f;
      for (int k = 0; k < 16; k++)
      {
         const ImVec2 q = ToScreen(doc, sMeshPts[k * 2], sMeshPts[k * 2 + 1]);
         const float dd = std::hypot(q.x - mouse.x, q.y - mouse.y);
         if (dd < bd) { bd = dd; sMeshPoint = k; }
      }
      if (sMeshPoint < 0)
      {
         // Inside the grid but off a handle: the nearest point follows the drag, so pulling on the picture bends it.
         float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
         for (int k = 0; k < 16; k++)
         {
            lo[0] = std::min(lo[0], sMeshPts[k * 2]); hi[0] = std::max(hi[0], sMeshPts[k * 2]);
            lo[1] = std::min(lo[1], sMeshPts[k * 2 + 1]); hi[1] = std::max(hi[1], sMeshPts[k * 2 + 1]);
         }
         if (sMouseDoc.x < lo[0] || sMouseDoc.y < lo[1] || sMouseDoc.x > hi[0] || sMouseDoc.y > hi[1])
            return;
         float best = 1e30f;
         for (int k = 0; k < 16; k++)
         {
            const float dd = std::hypot(sMeshPts[k * 2] - sMouseDoc.x, sMeshPts[k * 2 + 1] - sMouseDoc.y);
            if (dd < best) { best = dd; sMeshPoint = k; }
         }
      }
      sMeshGrab = ImVec2(sMeshPts[sMeshPoint * 2] - sMouseDoc.x, sMeshPts[sMeshPoint * 2 + 1] - sMouseDoc.y);
      PushUndoCheckpoint("Mesh warp");
      sOp = Op();
      sOp.kind = 27;
      sOp.start = mouse;
      sOp.docStart = sMouseDoc;
   }

   void MeshDrag(Scene2DNode* doc)
   {
      if (sMeshPoint < 0 || sMeshPoint >= 16)
         return;
      sMeshPts[sMeshPoint * 2] = sMouseDoc.x + sMeshGrab.x;
      sMeshPts[sMeshPoint * 2 + 1] = sMouseDoc.y + sMeshGrab.y;
      if (doc->MeshWarp(sMeshId, sMeshFrom, sMeshPts, false, sMeshNearest, sMeshPts0))
         Touch();
   }

   void MeshRelease(Scene2DNode* doc)
   {
      if (sMeshPoint >= 0 && doc->MeshWarp(sMeshId, sMeshFrom, sMeshPts, true, sMeshNearest, sMeshPts0))
         if (const Layer* l = doc->FindLayer(sMeshId))
            sMeshStored = l->tiles;
      sMeshPoint = -1;
      Touch();
   }

   // What the newer pixel tools show on the canvas: boxes and lines being dragged, the mesh, corners, the spot.
   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   void DrawToolOverlays(Scene2DNode* doc, ImDrawList* dl, ImU32 ink, ImU32 shade)
   {
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      auto line = [&](ImVec2 a, ImVec2 b) {
         dl->AddLine(a, b, shade, tok::view2d_tool_overlays_stroke);
         dl->AddLine(a, b, ink, tok::view2d_line_stroke_sm);
      };
      auto dot = [&](ImVec2 p, float r, bool hot) {
         dl->AddCircleFilled(p, r + tok::view2d_tool_overlays_gap, shade);
         dl->AddCircleFilled(p, r, hot ? Hex(kOutline) : ink);
      };
      if (sOp.kind == 24)
      {
         AddDocRect(dl, doc, sOp.docStart.x, sOp.docStart.y, sMouseDoc.x, sMouseDoc.y, shade, 2.5f);
         AddDocRect(dl, doc, sOp.docStart.x, sOp.docStart.y, sMouseDoc.x, sMouseDoc.y, ink, 1.0f);
      }
      if (sOp.kind == 25 && doc->HasSelection())
      {
         float lo[2], hi[2];
         if (doc->SelectionBounds(lo, hi))
         {
            const float dx = sMouseDoc.x - sOp.docStart.x, dy = sMouseDoc.y - sOp.docStart.y;
            AddDocRect(dl, doc, lo[0] + dx, lo[1] + dy, hi[0] + dx, hi[1] + dy, shade, 2.5f);
            AddDocRect(dl, doc, lo[0] + dx, lo[1] + dy, hi[0] + dx, hi[1] + dy, ink, 1.0f);
         }
      }
      if (sTool == kClone && ImGui::GetTime() - sCloneAsk < tok::motion_s_hint)
      {
         const char* t = T("Alt-click to set the source");
         const ImVec2 at(ImGui::GetIO().MousePos.x + tok::space_3_5, ImGui::GetIO().MousePos.y + tok::space_3_5);
         dl->AddText(ImVec2(at.x + 1, at.y + 1), shade, t);
         dl->AddText(at, ink, t);
      }
      if (ImGui::GetTime() - sExportNoteT < tok::motion_s_notice && !sExportNote.empty())
      {
         const ImVec2 at(sViewMn.x + tok::space_3, sViewMn.y + tok::space_3);
         dl->AddText(ImVec2(at.x + 1, at.y + 1), shade, sExportNote.c_str());
         dl->AddText(at, ink, sExportNote.c_str());
      }
      if (ImGui::GetTime() - sGroupPickAsk < tok::motion_s_hint)
      {
         const char* t = T("Pick a layer");
         const ImVec2 at(ImGui::GetIO().MousePos.x + tok::space_3_5, ImGui::GetIO().MousePos.y + tok::space_3_5);
         dl->AddText(ImVec2(at.x + 1, at.y + 1), shade, t);
         dl->AddText(at, ink, t);
      }
      if (sTool == kMeasure && sMeasureOn)
      {
         const ImVec2 a = ToScreen(doc, sMeasureA.x, sMeasureA.y), b = ToScreen(doc, sMeasureB.x, sMeasureB.y);
         line(a, b);
         dot(a, 2.5f, false);
         dot(b, 2.5f, false);
         const float dx = sMeasureB.x - sMeasureA.x, dy = sMeasureB.y - sMeasureA.y;
         char t[64];
         std::snprintf(t, sizeof t, "%s  %s", UiFmt::Px(std::hypot(dx, dy)).c, UiFmt::Rad(std::atan2(-dy, dx)).c);
         const ImVec2 at(b.x + tok::space_2_5, b.y + tok::space_1_5);
         dl->AddText(ImVec2(at.x + 1, at.y + 1), shade, t);
         dl->AddText(at, ink, t);
      }
      if (sTool == kMeshWarp)
         if (const Layer* al = Active(doc))
         {
            float pts[32];
            const bool live = sMeshId == al->id && (al->tiles == sMeshFrom || al->tiles == sMeshStored);
            if (live)
               std::copy(sMeshPts, sMeshPts + 32, pts);
            if (live || MeshRestFit(doc, *al, pts))
            {
               auto P = [&](int i, int j) { return ToScreen(doc, pts[(j * 4 + i) * 2], pts[(j * 4 + i) * 2 + 1]); };
               for (int j = 0; j < 4; j++)
                  for (int i = 0; i < 3; i++)
                  {
                     line(P(i, j), P(i + 1, j));
                     line(P(j, i), P(j, i + 1));
                  }
               for (int k = 0; k < 16; k++)
                  dot(P(k % 4, k / 4), 3.5f, live && k == sMeshPoint);
            }
         }
      if (sTool == kPerspective)
         if (const Layer* al = Active(doc))
         {
            float c[8];
            if (CanWarp(doc) && doc->Corners(*al, c))
               for (int k = 0; k < 4; k++)
                  dot(ToScreen(doc, c[k * 2], c[k * 2 + 1]), 4.0f, sOp.kind == 18 && sOp.warpHandle == k * 2);
         }
      if (sTool == kBlemish && sMouseIn)
      {
         const float r = std::max(tok::view2d_tool_overlays_min, sBlemishSize * 0.5f * sZoom);
         dl->AddCircle(mouse, r, shade, tok::view2d_tool_overlays_segments, tok::view2d_tool_overlays_stroke);
         dl->AddCircle(mouse, r, ink, tok::view2d_tool_overlays_segments, 1.0f);
      }
   }
   // ui-content-end

   void ScaleActive(Scene2DNode* doc, ImVec2 mouse)
   {
      Layer* l = Active(doc);
      if (l == nullptr || l->kind == Kind::Group || l->kind == Kind::Fill || l->kind == Kind::Adjust)
         return;
      const Layer& l0 = sOp.l0;
      // Scaled about the content rect (what the handles frame), not the whole layer box.
      float cr[4], m0[6], inv[6];
      ContentRect(doc, l0, cr);
      const float bx0 = cr[0], by0 = cr[1], bw = cr[2] - cr[0], bh = cr[3] - cr[1];
      doc->LayerMatrix(l0, m0);
      InverseOf(m0, inv);
      const ImVec2 md = ToDoc(doc, mouse);
      float bx, by;
      Apply(inv, md.x, md.y, bx, by);
      const float ox = sOp.fromCentre ? 0.5f : 1.0f - sOp.hx, oy = sOp.fromCentre ? 0.5f : 1.0f - sOp.hy;
      float kx = 1.0f, ky = 1.0f;
      if (std::fabs(sOp.hx - ox) > 1e-3f)
         kx = (bx - bx0 - ox * bw) / ((sOp.hx - ox) * bw);
      if (std::fabs(sOp.hy - oy) > 1e-3f)
         ky = (by - by0 - oy * bh) / ((sOp.hy - oy) * bh);
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
      Apply(m0, bx0 + ox * bw, by0 + oy * bh, fx0, fy0);
      // Magnetic: the dragged handle's edge snaps to the page, guides and other layers' edges and centres, the same
      // targets a move uses. Upright 2D layers only (a turned box has no edge along the page's axes).
      sSnapX.clear();
      sSnapY.clear();
      const bool snap = doc->Doc().snap != (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper);
      if (snap && !l0.is3d && std::fabs(std::remainder(l0.angle, 360.0f)) < 0.01f)
      {
         float hx0, hy0;
         Apply(m0, bx0 + sOp.hx * bw, by0 + sOp.hy * bh, hx0, hy0);
         const std::vector<int> moving = { l0.id };
         float r[2] = { 1.0f, 1.0f };
         bool got[2] = { false, false };
         for (int ax = 0; ax < 2; ax++)
         {
            const float h = ax == 0 ? sOp.hx : sOp.hy, k = ax == 0 ? kx : ky, f = ax == 0 ? fx0 : fy0, h0 = ax == 0 ? hx0 : hy0;
            if (std::fabs(h - tok::px_align) < tok::view2d_scale_active_limit || std::fabs(h0 - f) < 1e-3f)
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
      Apply(m1, bx0 + ox * bw, by0 + oy * bh, fx1, fy1);
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
   constexpr float kGizmoPx = tok::view2d_gizmo_px;

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

   // A point of the gizmo on screen: a short step from its centre along `dir` (unit, world), projected and then
   // stretched to the gizmo's screen size. A world-sized gizmo seen zoomed far out reached toward the camera and
   // bent into huge arcs; this keeps it kGizmoPx and undistorted at any zoom.
   bool GizmoPoint(Scene2DNode* doc, const float at[3], const float dir[3], ImVec2 c, float sizePx, ImVec2& out)
   {
      float x, y, depth = 1.0f;
      doc->ProjectPoint(at, x, y, &depth);
      const float eps = std::max(depth, 1.0f) * 0.01f;
      const float p[3] = { at[0] + dir[0] * eps, at[1] + dir[1] * eps, at[2] + dir[2] * eps };
      ImVec2 q;
      if (!GizmoScreen(doc, p, q))
         return false;
      const float epsPx = eps * std::max(doc->Camera().zoom, 1.0f) / std::max(depth, 1.0f) * sZoom;
      const float k = sizePx / std::max(epsPx, 1e-6f);
      out = ImVec2(c.x + (q.x - c.x) * k, c.y + (q.y - c.y) * k);
      return true;
   }

   float SegDist2D(ImVec2 p, ImVec2 a, ImVec2 b)
   {
      const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
      const float t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
      return std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t);
   }

   // Draws the gizmo; returns the part under the pointer (0..2 axes, 3 centre) or -1.
   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   int DrawGizmo3D(ImDrawList* dl, Scene2DNode* doc, const Layer& l, ImVec2 mouse, int held)
   {
      const float at[3] = { l.x, l.y, l.z };
      ImVec2 c;
      if (!GizmoScreen(doc, at, c))
         return -1;
      const int mode = sGizmoMode;
      float ax[3][3];
      GizmoAxes(doc, l, mode, ax);
      const ImU32 white = tok::U32(tok::canvas_ink);
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
               float dir[3];
               for (int k = 0; k < 3; k++)
                  dir[k] = u[k] * std::cos(t) + v[k] * std::sin(t);
               ImVec2 q;
               const bool ok = GizmoPoint(doc, at, dir, c, kGizmoPx, q);
               if (ok && prevOk)
               {
                  dl->AddLine(prev, q, tone(a, tok::view2d_tone_alpha), tok::view2d_gizmo3_d_stroke);
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
         ImVec2 s;
         if (!GizmoPoint(doc, at, ax[a], c, kGizmoPx, s))
            continue;
         const float dx = s.x - c.x, dy = s.y - c.y, sl = std::hypot(dx, dy);
         if (sl < 10.0f)
         {
            // Pointing at the camera: a dot that drags with the pointer's up and down.
            dl->AddCircleFilled(ImVec2(c.x + tok::space_4, c.y - tok::space_4), tok::view2d_gizmo3_d_r, tone(a, 1.0f));
            const float d = std::hypot(mouse.x - c.x - tok::space_4, mouse.y - c.y + tok::space_4);
            if (d < best)
            {
               best = d;
               hot = a;
            }
            continue;
         }
         const ImVec2 dir(dx / sl, dy / sl), side(-dy / sl, dx / sl);
         const ImU32 col = tone(a, 1.0f);
         dl->AddLine(ImVec2(c.x + dir.x * tok::view2d_gizmo3_d_ratio, c.y + dir.y * tok::view2d_gizmo3_d_ratio), s, col, tok::view2d_gizmo3_d_stroke);
         if (mode == 0)
            dl->AddTriangleFilled(ImVec2(s.x + dir.x * tok::view2d_arrow_x_lg, s.y + dir.y * tok::view2d_arrow_x_lg), ImVec2(s.x + side.x * tok::view2d_arrow_x_md, s.y + side.y * tok::view2d_arrow_x_md),
                                  ImVec2(s.x - side.x * tok::view2d_arrow_x_md, s.y - side.y * tok::view2d_arrow_x_md), col);
         else
            dl->AddRectFilled(ImVec2(s.x - tok::space_1_25, s.y - tok::space_1_25), ImVec2(s.x + tok::space_1_25, s.y + tok::space_1_25), col);
         const float d = SegDist2D(mouse, c, ImVec2(s.x + dir.x * tok::view2d_arrow_x_lg, s.y + dir.y * tok::view2d_arrow_x_lg));
         if (d < best && std::hypot(mouse.x - c.x, mouse.y - c.y) > tok::view2d_gizmo3_d_limit)
         {
            best = d;
            hot = a;
         }
      }
      dl->AddCircle(c, tok::view2d_ring_r, held == tok::view2d_ring_limit || (held < 0 && sGizmoHot == 3) ? white : tok::U32(tok::canvas_ink, tok::view2d_ring_alpha), tok::view2d_gizmo3_d_segments, tok::view2d_ring_stroke_lg);
      if (std::hypot(mouse.x - c.x, mouse.y - c.y) <= tok::view2d_gizmo3_d_limit_sm)
         hot = 3;
      return hot;
   }
   // ui-content-end

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
         ImVec2 s;
         if (!GizmoPoint(doc, at, ax[k], c, kGizmoPx, s))
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
            r = std::round(r / tok::view2d_drag_gizmo3_d_ratio) * tok::view2d_drag_gizmo3_d_scale;
         *v = std::fmod(r + tok::view2d_drag_gizmo3_d_span, 360.0f) - tok::view2d_v_span;
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
      const ImVec2 dd = DocDelta(mouse.x - sOp.start.x, mouse.y - sOp.start.y);
      float dx = dd.x, dy = dd.y;
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

   // The transparency checker on a turned view: cells of 8 screen px laid in document space, only those in sight.
   void DrawCheckerTurned(ImDrawList* dl, Scene2DNode* doc)
   {
      const float w = (float)doc->Doc().w, h = (float)doc->Doc().h, s = 8.0f / std::max(sZoom, 1e-4f);
      ImVec2 q[4];
      DocRect(doc, 0, 0, w, h, q);
      dl->AddQuadFilled(q[0], q[1], q[2], q[3], tok::U32(tok::canvas_ink));
      float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
      const ImVec2 corner[4] = { sViewMn, ImVec2(sViewMx.x, sViewMn.y), sViewMx, ImVec2(sViewMn.x, sViewMx.y) };
      for (const ImVec2& c : corner)
      {
         const ImVec2 p = ToDoc(doc, c);
         lo[0] = std::min(lo[0], p.x), lo[1] = std::min(lo[1], p.y), hi[0] = std::max(hi[0], p.x), hi[1] = std::max(hi[1], p.y);
      }
      const int i0 = std::max(0, (int)std::floor(lo[0] / s)), i1 = std::min((int)std::ceil(w / s), (int)std::ceil(hi[0] / s));
      const int j0 = std::max(0, (int)std::floor(lo[1] / s)), j1 = std::min((int)std::ceil(h / s), (int)std::ceil(hi[1] / s));
      if ((long long)(i1 - i0) * (j1 - j0) > 40000)
         return;   // far too small to see: the white is enough
      for (int j = j0; j < j1; j++)
         for (int i = i0 + ((i0 + j) % 2 == 0 ? 1 : 0); i < i1; i += 2)
         {
            DocRect(doc, i * s, j * s, std::min(w, (i + 1) * s), std::min(h, (j + 1) * s), q);
            dl->AddQuadFilled(q[0], q[1], q[2], q[3], tok::U32(tok::canvas_check));
         }
   }

   void DrawChecker(ImDrawList* dl, ImVec2 a, ImVec2 b)
   {
      const float s = 8.0f;
      dl->AddRectFilled(a, b, tok::U32(tok::canvas_ink));
      for (float y = a.y; y < b.y; y += s)
         for (float x = a.x + (((int)((y - a.y) / s)) % 2 == 0 ? s : 0.0f); x < b.x; x += 2.0f * s)
            dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(b.x, x + s), std::min(b.y, y + s)), tok::U32(tok::canvas_check));
   }

   float RulerStep()
   {
      const float steps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
      for (float s : steps)
         if (s * sZoom >= 60.0f)
            return s;
      return 10000.0f;
   }

   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   void DrawRulers(ImDrawList* dl, Scene2DNode* doc, ImVec2 mn, ImVec2 mx)
   {
      const ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg), text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + kRuler), bg);
      dl->AddRectFilled(mn, ImVec2(mn.x + kRuler, mx.y), bg);
      if (ViewTurned())
         return;   // the document's axes no longer run along the rulers
      const float step = RulerStep();
      const ImVec2 o = DocOrigin(doc);
      const float x0 = std::floor(((mn.x + kRuler) - o.x) / sZoom / step) * step;
      for (float v = x0; o.x + v * sZoom < mx.x; v += step * 0.1f)
      {
         const float sx = o.x + v * sZoom;
         const bool major = std::fabs(std::fmod(std::fabs(v) + 0.01f * step, step)) < 0.02f * step;
         if (sx < mn.x + kRuler)
            continue;
         dl->AddLine(ImVec2(sx, mn.y + kRuler - (major ? kRuler * tok::view2d_rulers_ratio : tok::view2d_rulers)), ImVec2(sx, mn.y + kRuler), text);
         if (major)
            dl->AddText(ImVec2(sx + tok::space_0_5, mn.y), text, std::to_string((int)std::lround(v)).c_str());
      }
      const float y0 = std::floor(((mn.y + kRuler) - o.y) / sZoom / step) * step;
      for (float v = y0; o.y + v * sZoom < mx.y; v += step * 0.1f)
      {
         const float sy = o.y + v * sZoom;
         const bool major = std::fabs(std::fmod(std::fabs(v) + 0.01f * step, step)) < 0.02f * step;
         if (sy < mn.y + kRuler)
            continue;
         dl->AddLine(ImVec2(mn.x + kRuler - (major ? kRuler * tok::view2d_rulers_ratio : tok::view2d_rulers), sy), ImVec2(mn.x + kRuler, sy), text);
         if (major)
         {
            const std::string s = std::to_string((int)std::lround(v));
            for (size_t i = 0; i < s.size() && i < 5; i++)
               dl->AddText(ImVec2(mn.x + tok::space_0_5, sy + 1.0f + i * tok::view2d_text_y), text, s.substr(i, 1).c_str());
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
   // ui-content-end

   int HoverGuide(Scene2DNode* doc, ImVec2 mouse, int& axis)
   {
      const Scene2DNode::Document& d = doc->Doc();
      if (!d.guidesOn)
         return -1;
      const ImVec2 md = ToDoc(doc, mouse);
      for (int i = 0; i < (int)d.guidesX.size(); i++)
         if (std::fabs(d.guidesX[i] - md.x) * sZoom <= 3.0f)
         {
            axis = 0;
            return i;
         }
      for (int i = 0; i < (int)d.guidesY.size(); i++)
         if (std::fabs(d.guidesY[i] - md.y) * sZoom <= 3.0f)
         {
            axis = 1;
            return i;
         }
      return -1;
   }

   // The marquee's far corner under its Ratio: free, a fixed shape (width leads), or a fixed size in px.
   ImVec2 MarqueeEnd(ImVec2 a, ImVec2 b);
   // From centre: the press is the middle, so the box starts as far the other way.
   ImVec2 MarqueeStart(ImVec2 a, ImVec2 e)
   {
      if (!sMarqueeCentre)
         return a;
      return ImVec2(tok::view2d_marquee_start_ratio * a.x - e.x, tok::view2d_marquee_start_ratio * a.y - e.y);
   }
   ImVec2 MarqueeEnd(ImVec2 a, ImVec2 b)
   {
      static const float kR[4] = { 0.0f, 1.0f, 4.0f / 3.0f, 16.0f / 9.0f };
      const float sx = b.x >= a.x ? 1.0f : -1.0f, sy = b.y >= a.y ? 1.0f : -1.0f;
      if (sMarqueeRatio == 4)
         return ImVec2(a.x + sx * sMarqueeFixed[0], a.y + sy * sMarqueeFixed[1]);
      if (sMarqueeRatio >= 1 && sMarqueeRatio <= 3)
         return ImVec2(b.x, a.y + sy * std::fabs(b.x - a.x) / kR[sMarqueeRatio]);
      return b;
   }

   // Width over height for the crop's Ratio, 0 when free.
   float CropRatio(Scene2DNode* doc)
   {
      static const float kR[7] = { 0.0f, 0.0f, 1.0f, 4.0f / 3.0f, 3.0f / 2.0f, 16.0f / 9.0f, 9.0f / 16.0f };
      if (sCropRatio == 1)
         return doc->Doc().h > 0 ? (float)doc->Doc().w / (float)doc->Doc().h : 0.0f;
      return kR[std::clamp(sCropRatio, 0, 6)];
   }

   // The crop box set to the ratio, the same centre, as large as fits in the box it was.
   void FitCropRatio(Scene2DNode* doc)
   {
      const float r = CropRatio(doc);
      if (r <= 0.0f)
         return;
      const float x0 = std::min(sCrop[0], sCrop[2]), x1 = std::max(sCrop[0], sCrop[2]);
      const float y0 = std::min(sCrop[1], sCrop[3]), y1 = std::max(sCrop[1], sCrop[3]);
      float w = x1 - x0, h = y1 - y0;
      if (w / std::max(h, 1.0f) > r)
         w = h * r;
      else
         h = w / r;
      const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
      sCrop[0] = std::round(cx - w * 0.5f);
      sCrop[1] = std::round(cy - h * 0.5f);
      sCrop[2] = std::round(cx + w * 0.5f);
      sCrop[3] = std::round(cy + h * 0.5f);
   }

   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   void DrawCropOverlay(ImDrawList* dl, Scene2DNode* doc)
   {
      // Document-space box, so it turns with the view; the dim is four document-space bands around it.
      const float x0 = std::min(sCrop[0], sCrop[2]), x1 = std::max(sCrop[0], sCrop[2]);
      const float y0 = std::min(sCrop[1], sCrop[3]), y1 = std::max(sCrop[1], sCrop[3]);
      // Darken dims what falls outside; Reveal off hides it.
      const ImU32 dim = !sCropReveal ? Hex(kPasteboard) : tok::U32(tok::view2d_dim_shade, sCropDarken ? tok::view2d_crop_overlay_dim_2_alpha : 0);
      const float far = 1e6f;
      const float band[4][4] = { { -far, -far, far, y0 }, { -far, y1, far, far }, { -far, y0, x0, y1 }, { x1, y0, far, y1 } };
      ImVec2 q[4];
      for (const auto& b : band)
      {
         DocRect(doc, b[0], b[1], b[2], b[3], q);
         dl->AddQuadFilled(q[0], q[1], q[2], q[3], dim);
      }
      AddDocRect(dl, doc, x0, y0, x1, y1, tok::U32(tok::canvas_ink, tok::view2d_doc_rect_alpha));
      for (int i = 1; i < 3 && sCropThirds; i++)
      {
         const float x = x0 + (x1 - x0) * i / tok::view2d_x_ratio, y = y0 + (y1 - y0) * i / tok::view2d_y_ratio;
         dl->AddLine(ToScreen(doc, x, y0), ToScreen(doc, x, y1), tok::U32(tok::canvas_ink, tok::view2d_line_alpha_sm));
         dl->AddLine(ToScreen(doc, x0, y), ToScreen(doc, x1, y), tok::U32(tok::canvas_ink, tok::view2d_line_alpha_sm));
      }
      for (int i = 0; i < 8; i++)
      {
         const ImVec2 p = ToScreen(doc, x0 + (x1 - x0) * kHandleAt[i][0], y0 + (y1 - y0) * kHandleAt[i][1]);
         dl->AddRectFilled(ImVec2(p.x - tok::space_1, p.y - tok::space_1), ImVec2(p.x + tok::space_1, p.y + tok::space_1), tok::U32(tok::canvas_ink));
      }
   }
   // ui-content-end

   int HoverCropHandle(Scene2DNode* doc, ImVec2 mouse)
   {
      const float x0 = std::min(sCrop[0], sCrop[2]), x1 = std::max(sCrop[0], sCrop[2]);
      const float y0 = std::min(sCrop[1], sCrop[3]), y1 = std::max(sCrop[1], sCrop[3]);
      for (int i = 0; i < 8; i++)
      {
         const ImVec2 p = ToScreen(doc, x0 + (x1 - x0) * kHandleAt[i][0], y0 + (y1 - y0) * kHandleAt[i][1]);
         if (std::fabs(mouse.x - p.x) <= kHandle && std::fabs(mouse.y - p.y) <= kHandle)
            return i;
      }
      const ImVec2 md = ToDoc(doc, mouse);
      return md.x > x0 && md.x < x1 && md.y > y0 && md.y < y1 ? tok::view2d_hover_crop : -1;
   }

   void DragCrop(Scene2DNode* doc, ImVec2 mouse)
   {
      const ImVec2 dd = DocDelta(mouse.x - sOp.start.x, mouse.y - sOp.start.y);
      const float dx = dd.x, dy = dd.y;
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
         // A set ratio: a corner keeps the width and moves the free edge; a side handle grows the other side about the centre.
         const float r = CropRatio(doc);
         if (r > 0.0f)
         {
            const float w = std::fabs(c[2] - c[0]), hgt = std::fabs(c[3] - c[1]);
            if (fx != 0.5f && fy != 0.5f)
            {
               const float sgn = (fy == 1 ? c[3] - c[1] : c[1] - c[3]) >= 0.0f || hgt < 1.0f ? 1.0f : -1.0f;
               if (fy == 1) c[3] = c[1] + sgn * w / r;
               else c[1] = c[3] - sgn * w / r;
            }
            else if (fy == 0.5f)
            {
               const float cy = (c[1] + c[3]) * 0.5f;
               c[1] = cy - w / r * 0.5f;
               c[3] = cy + w / r * 0.5f;
            }
            else
            {
               const float cx = (c[0] + c[2]) * 0.5f;
               c[0] = cx - hgt * r * 0.5f;
               c[2] = cx + hgt * r * 0.5f;
            }
         }
      }
      for (float& v : c)
         v = std::round(v);
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
   bool sTextFocus = false;
   std::string sTextBuf;           // grows with the text
   bool sTextStep = false;         // editing an existing text: the undo step waits for the first keystroke
   bool sTextNew = false;          // the text layer was made by this edit
   size_t sTextUndo0 = 0;          // the undo depth before "New text", dropped again when nothing is typed
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
      const int id = PlaceNew(doc, doc->AddVectorLayer(NewAbove(doc))).id;
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

   // A new foreground colour (picker, swatch, eyedropper) carries into the next shape fill and the next text,
   // as in Photoshop and Affinity. A colour set on the shape or text bar itself holds until the foreground moves again.
   void TakeForeground()
   {
      static float seen[3] = { 0.0f, 0.0f, 0.0f };
      if (seen[0] == sFg[0] && seen[1] == sFg[1] && seen[2] == sFg[2])
         return;
      std::copy(sFg, sFg + 3, seen);
      std::copy(sFg, sFg + 3, sStyle.fillC);
      sStyle.fillC[3] = 1.0f;
      if (sStyle.fillType == 0)
         sStyle.fillOn = true;
      std::copy(sFg, sFg + 3, sTextC);
      sTextC[3] = 1.0f;
   }

   void CopyStyle(const Shape& from, Shape& to)
   {
      to.fillOn = from.fillOn;
      to.strokeOn = from.strokeOn;
      std::copy(from.fillC, from.fillC + 4, to.fillC);
      to.fillType = from.fillType;
      to.fillAngle = from.fillAngle;
      to.fillStops = from.fillStops;
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
         if (std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) < tok::view2d_drag_shape_limit)
            return;
         PushUndoCheckpoint("Draw shape");
         Layer* l = DrawTarget(doc);
         std::vector<Shape>* items = Items(l);
         if (items == nullptr)
            return;
         static const int kTypes[6] = { Shape::kRect, Shape::kRect, Shape::kEllipse, Shape::kPolygon, Shape::kStar, Shape::kLine };
         TakeForeground();
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
         TakeForeground();
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
         if (std::hypot(mouse.x - f.x, mouse.y - f.y) < tok::view2d_pen_click_limit)
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
      if (sAnchor < 0 || sAnchor >= s.Anchors() || std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) < tok::view2d_pen_drag_limit)
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
         return std::hypot(mouse.x - q.x, mouse.y - q.y) < tok::view2d_hit_item_limit;
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
      const float pad = tok::view2d_pad_ratio / std::max(tok::view2d_hit_min, sZoom);
      for (int i = (int)items.size() - 1; i >= 0; i--)
      {
         float lo[2], hi[2];
         // The item itself, not its box: a ring's hole and the space beside a diagonal line reach what is below.
         if (Scene2DNode::ShapeBounds(items[i], lo, hi) && b.x >= lo[0] - pad && b.x <= hi[0] + pad && b.y >= lo[1] - pad && b.y <= hi[1] + pad &&
             (items[i].type == Shape::kSvg || Scene2DNode::ShapeHit(items[i], b.x, b.y, pad)))
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
            const int h = sPart == 1 ? tok::view2d_else_h_sm : tok::view2d_else_h_lg, o = sPart == 1 ? 4 : 2;
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

   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   void DrawVectorOverlay(ImDrawList* dl, Scene2DNode* doc, ImVec2 mouse, bool hovered)
   {
      Layer* l = Active(doc);
      std::vector<Shape>* items = Items(l);
      if (items == nullptr)
         return;
      std::vector<std::vector<float>> lines;
      const ImU32 white = tok::U32(tok::canvas_ink);
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
            if (q.size() >= tok::view2d_vector_overlay_limit)
               dl->AddPolyline(q.data(), (int)q.size(), Hex(kOutline, picked ? 1.0f : tok::view2d_vector_overlay_line_alpha_sm), ImDrawFlags_None, picked ? tok::view2d_vector_overlay_stroke : 1.0f);
         }
         if (!picked || sTool == kShape)
            continue;
         auto dot = [&](float bx, float by, bool square) {
            const ImVec2 p = BoxScreen(doc, *l, bx, by);
            if (square)
            {
               dl->AddRectFilled(ImVec2(p.x - tok::view2d_vector_overlay_gap, p.y - tok::view2d_vector_overlay_gap), ImVec2(p.x + tok::view2d_vector_overlay_gap, p.y + tok::view2d_vector_overlay_gap), white);
               dl->AddRect(ImVec2(p.x - tok::view2d_vector_overlay_gap, p.y - tok::view2d_vector_overlay_gap), ImVec2(p.x + tok::view2d_vector_overlay_gap, p.y + tok::view2d_vector_overlay_gap), Hex(kOutline));
            }
            else
            {
               dl->AddCircleFilled(p, tok::view2d_vector_overlay_r, Hex(kOutline));
            }
         };
         if (s.type == Shape::kPath)
            for (int a = 0; a < s.Anchors(); a++)
            {
               const float* p = &s.pts[(size_t)a * 6];
               for (int k = 1; k < 3; k++)
                  if (std::fabs(p[k * 2] - p[0]) + std::fabs(p[k * 2 + 1] - p[1]) > 0.01f)
                  {
                     dl->AddLine(BoxScreen(doc, *l, p[0], p[1]), BoxScreen(doc, *l, p[k * 2], p[k * 2 + 1]), Hex(kOutline, tok::view2d_vector_overlay_line_alpha_lg));
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
            dl->AddLine(BoxScreen(doc, *l, p[0], p[1]), mouse, Hex(kOutline, tok::view2d_vector_overlay_line_alpha_md), tok::view2d_line_stroke_sm);
         }
      }
   }
   // ui-content-end

   // ---- text -------------------------------------------------------------

   void EndTextEdit(Scene2DNode* doc)
   {
      Layer* l = doc->FindLayer(sTextEdit);
      sTextEdit = 0;
      if (l != nullptr && l->kind == Kind::Text && l->text.empty())
      {
         doc->RemoveLayer(l->id);
         PruneSelection(doc);
         // A text box left empty was never made: its undo step goes with it.
         if (sTextNew)
            while (gUndoStack.size() > sTextUndo0)
               gUndoStack.pop_back();
      }
      sTextNew = sTextStep = false;
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
      sTextBuf = l->text;
      SelectOnly(id);
   }

   void TextClick(Scene2DNode* doc, ImVec2 mouse)
   {
      if (sTextEdit != 0)
         EndTextEdit(doc);
      Layer* hit = doc->FindLayer(HitLayer(doc, sMouseDoc));
      if (hit != nullptr && hit->kind == Kind::Text)
      {
         BeginTextEdit(doc, hit->id);
         sTextStep = true;   // the step is pushed by the first keystroke, so a look without a change leaves none
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
      const size_t undo0 = gUndoStack.size();
      PushUndoCheckpoint("New text");
      TakeForeground();
      const bool box = std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) > tok::view2d_box_limit;
      const float x = box ? std::min(sOp.docStart.x, sMouseDoc.x) : sOp.docStart.x;
      const float y = box ? std::min(sOp.docStart.y, sMouseDoc.y) : sOp.docStart.y;
      const int id = PlaceNew(doc, doc->AddTextLayer("", x, y, box ? std::fabs(sMouseDoc.x - sOp.docStart.x) : 0.0f, NewAbove(doc))).id;
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
      sTextNew = true;
      sTextUndo0 = undo0;
   }

   // Where the caret and the selection sit, in the layer's box (document px). Lines are the typed ones; text is
   // measured by drawing it in the layer's own style, so the caret follows the real glyphs.
   struct TextMarks
   {
      std::string key;
      float caret[3] = { 0, 0, 0 };            // x, y0, y1
      std::vector<std::array<float, 4>> sel;   // x0 y0 x1 y1 per line
   } sTextMarks;

   float TextRunWidth(Scene2DNode* doc, const Layer& l, const std::string& run)
   {
      if (run.empty())
         return 0.0f;
      Layer t = l;
      t.id = -77;   // a cache slot of its own
      t.text = run;
      t.boxW = 0.0f;
      int w = 0, h = 0;
      doc->TextSize(t, w, h);
      return (float)w;
   }

   void UpdateTextMarks(Scene2DNode* doc, const Layer& l, float bw, float bh, int cursor, int selA, int selB)
   {
      char num[160];
      std::snprintf(num, sizeof(num), "|%d|%d|%d|%s|%s|%s|%d|%d|%s|%s|", cursor, selA, selB, UiFmt::F2(l.fontSize).c, UiFmt::F2(l.tracking).c, UiFmt::F2(l.boxW).c, l.align,
                    (int)(l.bold | (l.italic << 1) | (l.caps << 3)), UiFmt::F1(bw).c, UiFmt::F1(bh).c);
      const std::string key = l.text + num + l.font;
      if (key == sTextMarks.key)
         return;
      sTextMarks = TextMarks();
      sTextMarks.key = key;
      const std::string& text = l.text;
      cursor = std::clamp(cursor, 0, (int)text.size());
      if (selA > selB)
         std::swap(selA, selB);
      std::vector<std::pair<int, int>> lines;   // [start, end) of each typed line
      for (int a = 0, i = 0; i <= (int)text.size(); i++)
         if (i == (int)text.size() || text[i] == '\n')
         {
            lines.push_back({ a, i });
            a = i + 1;
         }
      const float bar = TextRunWidth(doc, l, "|");
      // The pen's place after `n` bytes of a line: a closing bar keeps trailing spaces in the measure.
      auto advance = [&](const std::pair<int, int>& ln, int n)
      {
         return n <= 0 ? 0.0f : std::max(0.0f, TextRunWidth(doc, l, text.substr(ln.first, n) + "|") - bar);
      };
      const float rowH = bh / (float)lines.size();
      for (size_t i = 0; i < lines.size(); i++)
      {
         const std::pair<int, int>& ln = lines[i];
         const float lineW = TextRunWidth(doc, l, text.substr(ln.first, ln.second - ln.first));
         const float left = l.align == 1 ? (bw - lineW) * 0.5f : l.align == 2 ? bw - lineW : 0.0f;
         const float y0 = rowH * (float)i, y1 = rowH * (float)(i + 1);
         if (cursor >= ln.first && cursor <= ln.second)
         {
            sTextMarks.caret[0] = left + advance(ln, cursor - ln.first);
            sTextMarks.caret[1] = y0;
            sTextMarks.caret[2] = y1;
         }
         if (selB > selA && selB > ln.first && selA <= ln.second)
         {
            const int a = std::max(selA, ln.first), b = std::min(selB, ln.second);
            const float xa = left + advance(ln, a - ln.first);
            // A selected line break shows as a sliver past the line's end.
            const float xb = left + advance(ln, b - ln.first) + (selB > ln.second ? std::max(2.0f, l.fontSize * tok::view2d_text_marks_ratio) : 0.0f);
            sTextMarks.sel.push_back({ xa, y0, xb, y1 });
         }
      }
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
      dl->AddPolyline(c, tok::view2d_line_num_points_md, Hex(kOutline), ImDrawFlags_Closed, 1.0f);
      // Type in place: the layer itself shows the text, so the input sits invisibly over the layer's box and
      // only a caret is drawn (one copy of the text, not two).
      const ImVec2 bmn(std::min(std::min(c[0].x, c[1].x), std::min(c[2].x, c[3].x)), std::min(std::min(c[0].y, c[1].y), std::min(c[2].y, c[3].y)));
      const ImVec2 bmx(std::max(std::max(c[0].x, c[1].x), std::max(c[2].x, c[3].x)), std::max(std::max(c[0].y, c[1].y), std::max(c[2].y, c[3].y)));
      const float w = std::max(tok::text_min_w, bmx.x - bmn.x), h = std::max(ImGui::GetFrameHeight(), bmx.y - bmn.y);
      const ImVec2 at = bmn;
      const ImVec2 keep = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(at);
      ImGui::PushID(sTextEdit);
      if (sTextFocus)
      {
         UiInteract::FocusHere();
         sTextFocus = false;
      }
      const ImU32 clear = tok::U32(tok::transparent);
      ImGui::PushStyleColor(ImGuiCol_Text, clear);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, clear);
      ImGui::PushStyleColor(ImGuiCol_Border, clear);
      ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, clear);
      ImGui::PushStyleColor(ImGuiCol_InputTextCursor, clear);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      const ImGuiID editId = ImGui::GetID("##v2dtextedit");
      const bool changed = ImGui::InputTextMultiline("##v2dtextedit", &sTextBuf, ImVec2(w, h), ImGuiInputTextFlags_NoHorizontalScroll);
      const bool editing = ImGui::IsItemActive();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(5);
      if (changed)
      {
         if (sTextStep)
            PushUndoCheckpoint("Edit text");   // before the layer takes the new text
         sTextStep = false;
         l->text = sTextBuf;
         Touch();
         doc->LayerBox(*l, bw, bh);
      }
      if (const ImGuiInputTextState* st = ImGui::GetInputTextState(editId); editing && st != nullptr)
      {
         // The caret where the cursor is, and the selection behind the glyphs.
         UpdateTextMarks(doc, *l, bw, bh, st->GetCursorPos(), st->GetSelectionStart(), st->GetSelectionEnd());
         for (const std::array<float, 4>& r : sTextMarks.sel)
            dl->AddQuadFilled(BoxScreen(doc, *l, r[0], r[1]), BoxScreen(doc, *l, r[2], r[1]), BoxScreen(doc, *l, r[2], r[3]),
                              BoxScreen(doc, *l, r[0], r[3]), Hex(kOutline, tok::view2d_fill_alpha));
         if (std::fmod(ImGui::GetTime(), (double)tok::motion_s_blink_period) < tok::motion_s_blink_on)
            dl->AddLine(BoxScreen(doc, *l, sTextMarks.caret[0], sTextMarks.caret[1]),
                        BoxScreen(doc, *l, sTextMarks.caret[0], sTextMarks.caret[2]), Hex(kOutline), tok::stroke_caret);
      }
      sEditMn = ImGui::GetItemRectMin();
      sEditMx = ImGui::GetItemRectMax();
      const bool done = ImGui::IsItemDeactivated();
      ImGui::PopID();
      ImGui::SetCursorScreenPos(keep);
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

   // ui-content-begin: 2D canvas: the document and its on-canvas guides, handles and cursors
   void DrawCanvas(Scene2DNode* doc, ImVec2 size)
   {
      ImGuiIO& io = ImGui::GetIO();
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      // Chips drawn over the canvas later in the frame (Rasterize?) must be able to take the hover and the click.
      ImGui::SetNextItemAllowOverlap();
      UiInteract::Hit("##view2dcanvas", size,
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
      // A right-click picks what is under the cursor first (Affinity, Figma; Photoshop's Move tool lists the layers
      // there), so the menu offers what applies to that layer. A pixel selection keeps its layer; so does a click
      // on a layer already selected.
      if (MenuParts::ContextClicked() && !doc->HasSelection())
      {
         const ImVec2 p = ToDoc(doc, ImGui::GetIO().MousePos);
         float lo[2] = { p.x, p.y }, hi[2] = { p.x, p.y };
         bool onSel = false;
         for (int id : sSel)
            if (const Layer* l = doc->FindLayer(id))
               onSel |= InkIn(doc, *l, lo, hi);
         if (!onSel)
            ToolTarget(doc, lo, hi);
      }
      if (MenuParts::BeginContextItem("##v2dcanvasmenu"))
      {
         if (FreeTransformOn(doc))
         {
            FreeTransformMenu(doc);   // while transforming, the menu is the transform's (Photoshop)
            MenuParts::EndPopup();
         }
         else
         {
            const bool any = !sSel.empty();
            if (any && MenuParts::Item(T("Cut"), Sc("Ctrl+X"))) sPending.what = Pending::Cut;
            if (any && MenuParts::Item(T("Copy"), Sc("Ctrl+C"))) sPending.what = Pending::Copy;
            if (any && MenuParts::Item(T("Copy Merged"), Sc("Shift+Ctrl+C"))) sPending.what = Pending::CopyMerged;
            if (MenuParts::Item(T("Paste"), Sc("Ctrl+V"))) sPending.what = Pending::Paste;
            if (any && MenuParts::Item(T("Duplicate"), Sc("Ctrl+J"))) sPending.what = Pending::Duplicate;
            if (SelectionActsOnPixels(doc))
               if (MenuParts::Item(T("Layer via Cut"), Sc("Shift+Ctrl+J")))
               {
                  PushUndoCheckpoint("Layer via cut");
                  if (const int nid = SelectionToLayer(doc, true))
                     SelectOnly(nid);
               }
            if (any && MenuParts::Item(T("Free Transform"), Sc("Ctrl+T"), FreeTransformOn(doc)))
               FreeTransformOn(doc) ? EndFreeTransform(doc, true) : BeginFreeTransform(doc);
            if (any)
               TransformSubMenu(doc);
            MenuParts::Separator();
            // What the active layer is decides what else is offered: pixels, image, text, shapes, group, fill or adjustment.
            if (Layer* k = Active(doc))
            {
               const bool picked = any && sSel.size() == 1;
               if (doc->HasSelection())
               {
                  if (k->kind != Kind::Group && MenuParts::Item(T("Mask from Selection"))) sPending.what = Pending::MaskFromSel;
                  if (MenuParts::Item(T("Deselect"), Sc("Ctrl+D")))
                  {
                     SelStep("Deselect");
                     doc->Deselect();
                  }
                  if (MenuParts::Item(T("Invert Selection"), Sc("Shift+Ctrl+I")))
                  {
                     SelStep("Invert selection");
                     doc->InvertSelection();
                  }
                  if (MenuParts::Item(T("Crop to Selection"))) CropToSelection(doc);
               }
               // One Fill.. row: it fills the selection, or a pixel layer whole (tool-layer-matrix.md).
               if ((doc->HasSelection() || k->kind == Kind::Paint) && MenuParts::Item(T("Fill..."), "Shift+F5")) sFillAsk = true;
               switch (k->kind)
               {
                  case Kind::Paint:
                     if (picked && MenuParts::Item(T("Convert to Draw Node"))) sPending.what = Pending::ToDraw;
                     break;
                  case Kind::Node:
                     if (!k->HasSkin() && MenuParts::Item(T("Rasterize Layer")))
                        EnsurePixels(doc);
                     if (doc->CanTakeBelow(*k) && MenuParts::Item(T("Effect on Below"), nullptr, k->below)) sPending.what = Pending::LayerBelow;
                     NodeChoiceMenu(T("Replace with"), doc, Pending::ReplaceNode, k->uid);
                     break;
                  case Kind::Text:
                     if (picked && MenuParts::Item(T("Edit Text"))) BeginTextEdit(doc, k->id);
                     if (MenuParts::Item(T("Rasterize Layer"))) RasterizeLayer(doc, k->id);
                     if (MenuParts::Item(T("Convert to Shapes"))) sPending.what = Pending::ToShapes;
                     if (MenuParts::Item(T("Convert to Text Node"))) sPending.what = Pending::ToText;
                     break;
                  case Kind::Vector:
                     if (MenuParts::Item(T("Rasterize Layer"))) RasterizeLayer(doc, k->id);
                     if (MenuParts::Item(T("Convert to Sketch Node"))) sPending.what = Pending::ToSketch;
                     break;
                  case Kind::Group:
                     if (MenuParts::Item(T("Ungroup"), Sc("Shift+Ctrl+G"))) sPending.what = Pending::Ungroup;
                     if (MenuParts::Item(T("Flatten Group"))) sPending.what = Pending::FlattenGroup;
                     if (MenuParts::Item(T("Select Layer Pixels"))) SelectLayerPixels(doc, k->id, Scene2DNode::kSelNew);
                     break;
                  case Kind::Fill:
                  case Kind::Adjust:
                     // A fill or adjustment has no pixels of its own: its settings live on the Layer tab.
                     if (MenuParts::Item(k->kind == Kind::Fill ? T("Edit Fill...") : T("Edit Adjustment...")))
                     {
                        sTab = 1;
                        sSideOpen = true;
                     }
                     break;
                  default: break;
               }
               if (k->kind != Kind::Group)
               {
                  if (!k->HasPixelMask() && MenuParts::Item(T("Add Layer Mask")))
                     sPending.what = doc->HasSelection() ? Pending::MaskFromSel : Pending::AddMask;
                  if (k->HasPixelMask() && MenuParts::Item(T("Delete Layer Mask"))) sPending.what = Pending::RemoveMask;
               }
               MenuParts::Separator();
            }
            NodeChoiceMenu(T("Add Node as Layer"), doc, Pending::AddNode, 0);
            MenuParts::Separator();
            if (any && MenuParts::Item(T("Group"), Sc("Ctrl+G"))) sPending.what = Pending::Group;
            // Arrange as one submenu, as Photoshop's Layer > Arrange and Affinity's Arrange.
            if (any && MenuParts::SubMenu(T("Arrange")))
            {
               if (MenuParts::Item(T("Bring to Front"), Sc("Shift+Ctrl+]"))) Restack(doc, 3);
               if (MenuParts::Item(T("Bring Forward"), Sc("Ctrl+]"))) Restack(doc, 1);
               if (MenuParts::Item(T("Send Backward"), Sc("Ctrl+["))) Restack(doc, 0);
               if (MenuParts::Item(T("Send to Back"), Sc("Shift+Ctrl+["))) Restack(doc, 2);
               ImGui::EndMenu();
            }
            MenuParts::Separator();
            SelectionMenu(doc);
            if (MenuParts::Item(T("Select All Layers"), Sc("Ctrl+A")))
            {
               sSel.clear();
               for (const Layer& l : doc->Layers())
                  if (l.parent == 0)
                     sSel.push_back(l.id);
            }
            if (MenuParts::Item(T("Trim Transparent Edges"))) TrimDocument(doc);
            if (MenuParts::Item(T("Fit on Screen"), Sc("Ctrl+0"))) FitView(doc);
            if (MenuParts::Item(T("Actual Pixels"), Sc("Ctrl+1")))
            {
               sZoom = 1.0f;
               sPan = ImVec2(0, 0);
            }
            if (ViewTurned() && MenuParts::Item(T("Reset Rotation"), Sc("Ctrl+Shift+0")))
               TurnViewAbout(doc, -sViewAngle, ImVec2((sViewMn.x + sViewMx.x) * 0.5f, (sViewMn.y + sViewMx.y) * 0.5f));
            if (any)
            {
               MenuParts::Separator();
               if (MenuParts::Item(T("Delete"), "Delete"))
               {
                  if (SelectionActsOnPixels(doc))
                     ClearOrFill(doc, 0);
                  else
                     sPending.what = Pending::Delete;
               }
            }
            MenuParts::EndPopup();
      
         }
      }
      const ImVec2 mp = ImGui::GetIO().MousePos;
      const bool inEditor = sTextEdit != 0 && mp.x >= sEditMn.x && mp.x <= sEditMx.x && mp.y >= sEditMn.y && mp.y <= sEditMx.y;
      const bool onAsk = sRasterAsk != 0 && mp.x >= sRasterAskMn.x && mp.x <= sRasterAskMx.x && mp.y >= sRasterAskMn.y && mp.y <= sRasterAskMx.y;
      const bool hovered = ImGui::IsItemHovered() && !inEditor && !onAsk;
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
      if (sNeedFit && size.x > tok::view2d_canvas_limit && size.y > tok::view2d_canvas_limit)
      {
         FitView(doc);
         sNeedFit = false;
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->PushClipRect(mn, mx, true);
      dl->AddRectFilled(mn, mx, Hex(kPasteboard));
      const ImVec2 a = ToScreen(doc, 0, 0), b = ToScreen(doc, (float)d.w, (float)d.h);
      ImVec2 pq[4];
      DocRect(doc, 0, 0, (float)d.w, (float)d.h, pq);
      // The picture: the Scene 2D's own output, so what is seen here is what renders. A quad, so the view can turn.
      dl->PushClipRect(sViewMn, sViewMx, true);
      if (d.background == 3)
      {
         if (ViewTurned())
            DrawCheckerTurned(dl, doc);
         else
            DrawChecker(dl, a, b);
      }
      const ImVec2 uv[4] = { ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0) };
      if (const unsigned tex = doc->GetOutputTexture())
      {
         // Magnified past 2x the picture shows its pixels as squares (nearest), then the sampler goes back to smooth
         // for every other user of the texture.
         const bool pixels = sZoom >= 2.0f;
         if (pixels)
            dl->AddCallback(&MagFilterCb, (void*)(intptr_t)(tex | tok::view2d_canvas_alpha));  // ui-audit-ok: mask (bit packing: the nearest-filter flag bit on a texture id)
         Thumb::AddQuad(dl, (ImTextureID)(intptr_t)tex, pq[0], pq[1], pq[2], pq[3], uv[0], uv[1], uv[2], uv[3]);
         if (pixels)
         {
            dl->AddCallback(&MagFilterCb, (void*)(intptr_t)tex);
            dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
         }
         if (sZoom >= 8.0f)
         {
            // Pixel grid over the visible part only.
            const ImVec2 c0 = ToDoc(doc, sViewMn), c1 = ToDoc(doc, sViewMx), c2 = ToDoc(doc, ImVec2(sViewMn.x, sViewMx.y)),
                         c3 = ToDoc(doc, ImVec2(sViewMx.x, sViewMn.y));
            const int x0 = std::max(0, (int)std::floor(std::min({ c0.x, c1.x, c2.x, c3.x })));
            const int x1 = std::min(d.w, (int)std::ceil(std::max({ c0.x, c1.x, c2.x, c3.x })));
            const int y0 = std::max(0, (int)std::floor(std::min({ c0.y, c1.y, c2.y, c3.y })));
            const int y1 = std::min(d.h, (int)std::ceil(std::max({ c0.y, c1.y, c2.y, c3.y })));
            const float al = std::min(0.22f, (sZoom - 8.0f) / 24.0f * 0.22f + 0.08f);
            const ImU32 gc = tok::U32(tok::view2d_gc_shade, (int)(al * 255.0f));
            if (x1 - x0 < tok::view2d_canvas_limit_lg && y1 - y0 < tok::view2d_canvas_limit_lg)
            {
               for (int x = x0; x <= x1; x++)
                  dl->AddLine(ToScreen(doc, (float)x, (float)y0), ToScreen(doc, (float)x, (float)y1), gc);
               for (int y = y0; y <= y1; y++)
                  dl->AddLine(ToScreen(doc, (float)x0, (float)y), ToScreen(doc, (float)x1, (float)y), gc);
            }
         }
      }
      // The selection's marching ants, or the red quick mask, over the picture.
      if ((doc->HasSelection() || doc->quickMask) && !(doc->HasSelection() && DrawScreenAnts(dl, doc)))
         if (const unsigned ants = doc->SelectionOverlay((float)ImGui::GetTime(), doc->quickMask))
            Thumb::AddQuad(dl, (ImTextureID)(intptr_t)ants, pq[0], pq[1], pq[2], pq[3], uv[0], uv[1], uv[2], uv[3]);
      if (ViewTurned())
         dl->AddPolyline(pq, tok::view2d_line_num_points_md, tok::U32(tok::canvas_shade, tok::view2d_canvas_shade_alpha), ImDrawFlags_Closed, 1.0f);
      else
         dl->AddRect(ImVec2(a.x - tok::space_0_25, a.y - tok::space_0_25), ImVec2(b.x + tok::space_0_25, b.y + tok::space_0_25), tok::U32(tok::canvas_shade, tok::view2d_canvas_shade_alpha));

      if (d.grid && d.gridSize > 0.0f)
      {
         const float sub = d.gridSize / (float)std::max(1, d.gridSubdiv);
         if (sub * sZoom >= 4.0f)
         {
            int n = 0;
            for (float x = 0; x <= d.w + 0.01f; x += sub, n++)
               dl->AddLine(ToScreen(doc, x, 0), ToScreen(doc, x, (float)d.h), Hex(kGrid, n % std::max(tok::view2d_grid_shade, d.gridSubdiv) == 0 ? tok::view2d_canvas_line_alpha_lg : tok::view2d_canvas_line_alpha_sm));
            n = 0;
            for (float y = 0; y <= d.h + 0.01f; y += sub, n++)
               dl->AddLine(ToScreen(doc, 0, y), ToScreen(doc, (float)d.w, y), Hex(kGrid, n % std::max(tok::view2d_grid_shade, d.gridSubdiv) == 0 ? tok::view2d_canvas_line_alpha_lg : tok::view2d_canvas_line_alpha_sm));
         }
      }
      if (d.guidesOn)
      {
         for (float g : d.guidesX)
            dl->AddLine(ToScreen(doc, g, -tok::view2d_to_screen_y), ToScreen(doc, g, tok::view2d_to_screen_y), Hex(kGuide));
         for (float g : d.guidesY)
            dl->AddLine(ToScreen(doc, -tok::view2d_to_screen_y, g), ToScreen(doc, tok::view2d_to_screen_y, g), Hex(kGuide));
      }

      // Symmetry guides while a painting tool is up: the mirror axes, or the radial spokes, through the centre.
      if (d.symmetry > 0 && (sTool == kBrush || sTool == kEraser || sTool == kMixer || RetouchTool(sTool)))
      {
         const float cx = d.w * 0.5f, cy = d.h * 0.5f;
         const ImU32 col = Hex(kGuide, tok::view2d_guide_alpha);
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
               const float a = tok::view2d_a_ratio * tok::view2d_canvas_scale_sm * k / n - tok::view2d_a_gap;
               dl->AddLine(ToScreen(doc, cx, cy), ToScreen(doc, cx + std::cos(a) * r, cy + std::sin(a) * r), col);
            }
         }
      }

      const ImVec2 mouse = io.MousePos;
      sMouseIn = hovered;
      sMouseDoc = ToDoc(doc, mouse);
      Layer* active = Active(doc);
      const bool canTransform = sTool == kMove && active != nullptr && active->kind != Kind::Group && active->kind != Kind::Fill && active->kind != Kind::Adjust && sSel.size() == 1 &&
                                Movable(doc, *active) && doc->Shown(*active);

      // Outlines: each selected layer, then the active one's handles.
      if (sTool == kMove)
         for (int id : sSel)
            if (Layer* l = doc->FindLayer(id))
            {
               if (l->kind == Kind::Group)
               {
                  float lo[2], hi[2];
                  if (doc->Bounds(id, lo, hi))
                     AddDocRect(dl, doc, lo[0], lo[1], hi[0], hi[1], Hex(kOutline));
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
                     dl->AddPolyline(q, tok::view2d_line_num_points_md, Hex(kOutline), ImDrawFlags_Closed, 1.0f);
                  }
                  continue;
               }
               float c[8];
               ContentCorners(doc, *l, c);
               ImVec2 q[4];
               for (int i = 0; i < 4; i++)
                  q[i] = ToScreen(doc, c[i * 2], c[i * 2 + 1]);
               dl->AddPolyline(q, tok::view2d_line_num_points_md, Hex(kOutline), ImDrawFlags_Closed, 1.0f);
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
            dl->AddRectFilled(ImVec2(p.x - tok::view2d_canvas_gap, p.y - tok::view2d_canvas_gap), ImVec2(p.x + tok::view2d_canvas_gap, p.y + tok::view2d_canvas_gap), tok::U32(tok::canvas_ink));
            dl->AddRect(ImVec2(p.x - tok::view2d_canvas_gap, p.y - tok::view2d_canvas_gap), ImVec2(p.x + tok::view2d_canvas_gap, p.y + tok::view2d_canvas_gap), Hex(kOutline));
         }
         const ImVec2 an = ToScreen(doc, active->x, active->y);
         dl->AddCircle(an, tok::view2d_canvas_r, Hex(kOutline), tok::view2d_canvas_segments, tok::view2d_canvas_stroke);
         dl->AddLine(ImVec2(an.x - tok::view2d_line_x, an.y), ImVec2(an.x + tok::view2d_line_x, an.y), Hex(kOutline));
         dl->AddLine(ImVec2(an.x, an.y - tok::view2d_line_x), ImVec2(an.x, an.y + tok::view2d_line_x), Hex(kOutline));
      }
      const bool groupT = !canTransform && GroupTarget(doc);
      if (groupT)
      {
         float lo[2], hi[2];
         if (SelBounds(doc, lo, hi))
         {
            AddDocRect(dl, doc, lo[0], lo[1], hi[0], hi[1], Hex(kOutline));
            for (int i = 0; i < 8; i++)
            {
               const ImVec2 p = GroupHandlePoint(doc, lo, hi, i);
               dl->AddRectFilled(ImVec2(p.x - tok::view2d_canvas_gap, p.y - tok::view2d_canvas_gap), ImVec2(p.x + tok::view2d_canvas_gap, p.y + tok::view2d_canvas_gap), tok::U32(tok::canvas_ink));
               dl->AddRect(ImVec2(p.x - tok::view2d_canvas_gap, p.y - tok::view2d_canvas_gap), ImVec2(p.x + tok::view2d_canvas_gap, p.y + tok::view2d_canvas_gap), Hex(kOutline));
            }
         }
      }
      if (VectorTool(sTool))
         DrawVectorOverlay(dl, doc, mouse, hovered);
      if (sOp.kind == 15)
      {
         const ImVec2 p0 = ToScreen(doc, sOp.docStart.x, sOp.docStart.y);
         if (std::hypot(mouse.x - p0.x, mouse.y - p0.y) > tok::view2d_canvas_limit_md)
            AddDocRect(dl, doc, sOp.docStart.x, sOp.docStart.y, sMouseDoc.x, sMouseDoc.y, Hex(kOutline));
      }
      if (sOp.kind == 1 || sOp.kind == 2)
      {
         for (float x : sSnapX)
            dl->AddLine(ToScreen(doc, x, -tok::view2d_to_screen_y), ToScreen(doc, x, tok::view2d_to_screen_y), Hex(kSmart));
         for (float y : sSnapY)
            dl->AddLine(ToScreen(doc, -tok::view2d_to_screen_y, y), ToScreen(doc, tok::view2d_to_screen_y, y), Hex(kSmart));
      }
      // Shapes being dragged out by the pixel tools.
      const ImU32 ink = tok::U32(tok::canvas_ink, tok::view2d_canvas_ink_alpha), shade = tok::U32(tok::canvas_shade, tok::view2d_ink_alpha);
      DrawToolOverlays(doc, dl, ink, shade);
      if (sOp.kind == 9)
      {
         const ImVec2 e = MarqueeEnd(sOp.docStart, sMouseDoc), s0 = MarqueeStart(sOp.docStart, e);
         if (sMarqueeShape == 1)
         {
            // Traced in document space, so it turns with the view.
            const float cx = (s0.x + e.x) * 0.5f, cy = (s0.y + e.y) * 0.5f;
            const float rx = std::fabs(e.x - s0.x) * 0.5f, ry = std::fabs(e.y - s0.y) * 0.5f;
            ImVec2 ring[64];
            for (int i = 0; i < 64; i++)
               ring[i] = ToScreen(doc, cx + rx * std::cos(i * 6.2831853f / 64.0f), cy + ry * std::sin(i * 6.2831853f / 64.0f));
            dl->AddPolyline(ring, tok::view2d_line_num_points_lg, shade, ImDrawFlags_Closed, tok::view2d_line_stroke_lg);
            dl->AddPolyline(ring, tok::view2d_line_num_points_lg, ink, ImDrawFlags_Closed, 1.0f);
         }
         else
         {
            AddDocRect(dl, doc, s0.x, s0.y, e.x, e.y, shade, 2.5f);
            AddDocRect(dl, doc, s0.x, s0.y, e.x, e.y, ink, 1.0f);
         }
      }
      auto drawPath = [&](const std::vector<float>& pts, bool toMouse)
      {
         std::vector<ImVec2> q;
         for (size_t i = 0; i + 1 < pts.size(); i += 2)
            q.push_back(ToScreen(doc, pts[i], pts[i + 1]));
         if (toMouse)
            q.push_back(mouse);
         if (q.size() >= tok::view2d_canvas_limit_xs_i)
         {
            dl->AddPolyline(q.data(), (int)q.size(), shade, ImDrawFlags_None, tok::view2d_line_stroke_lg);
            dl->AddPolyline(q.data(), (int)q.size(), ink, ImDrawFlags_None, 1.0f);
         }
      };
      if (sOp.kind == 10)
         drawPath(sOp.path, false);
      if (!sPoly.empty())
         drawPath(sPoly, true);
      if (sOp.kind == 17)
      {
         ImVec2 q[4];
         DocRect(doc, sOp.docStart.x, sOp.docStart.y, sMouseDoc.x, sMouseDoc.y, q);
         dl->AddQuadFilled(q[0], q[1], q[2], q[3], tok::U32(tok::canvas_select, tok::view2d_canvas_select_alpha));
         dl->AddPolyline(q, tok::view2d_line_num_points_md, tok::U32(tok::canvas_select, tok::view2d_line_alpha_lg), ImDrawFlags_Closed, 1.0f);
      }
      if (sOp.kind == 8)
      {
         const ImVec2 p0 = ToScreen(doc, sOp.docStart.x, sOp.docStart.y);
         dl->AddLine(p0, mouse, shade, tok::view2d_line_stroke_lg);
         dl->AddLine(p0, mouse, ink, tok::view2d_line_stroke_sm);
         dl->AddCircleFilled(p0, tok::view2d_dot_r, ink);
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
      if (gView2DMenuEdit != 0)
      {
         sPending.what = gView2DMenuEdit == 1 ? Pending::Copy : Pending::Paste;
         gView2DMenuEdit = 0;
      }
      HandleKeys(doc, hovered);
      const bool space = hovered && ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput;
      const bool panTool = sTool == kHand || space;
      const bool sizeDragHint = BrushTool(sTool) && io.KeyAlt && (io.KeyCtrl || io.KeySuper);
      // Trackpad pinch zooms around the pointer cleanly; two-finger rotation is disabled so pinching never tilts the
      // canvas. On macOS, two-finger scroll pans 1:1 (Ctrl/Cmd/Alt + wheel zooms). Hand tool, Space-drag, middle-drag,
      // and Shift + wheel pan on every OS.
      const double pinchDelta = TrackpadPinchThisFrame();
      if (hovered && std::fabs(pinchDelta) > 0.0005 && sOp.kind == 0)
         ZoomAt(doc, (float)std::clamp(1.0 + pinchDelta, 0.25, 4.0), mouse);
      if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
      {
         const bool modZoom = io.KeyCtrl || io.KeySuper || io.KeyAlt;
#ifdef __APPLE__
         if (modZoom)
         {
            sZoomVel += io.MouseWheel * 0.10f;
            sZoomAt = mouse;
         }
         else if (io.KeyShift)
         {
            sPan.x += (io.MouseWheelH + io.MouseWheel) * tok::view2d_canvas_scale;
         }
         else
         {
            sPan.x += io.MouseWheelH * tok::view2d_else_scale_lg;
            sPan.y += io.MouseWheel * tok::view2d_else_scale_lg;
         }
#else
         if (io.KeyShift)
         {
            sPan.x += (io.MouseWheelH + io.MouseWheel) * tok::view2d_canvas_scale;
         }
         else
         {
            sZoomVel += io.MouseWheel * (modZoom ? 0.10f : 0.045f);
            sZoomAt = mouse;
         }
#endif
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
      int hoverHandle = !hovered || gizmo3d ? -1 : canTransform ? HoverHandle(doc, *active, mouse) : groupT ? HoverGroupHandle(doc, mouse) : -1;
      // Free transform modes: Rotate turns from any handle, Scale never turns.
      if (FreeTransformOn(doc) && sFreeMode == kFreeScale && hoverHandle == 8)
         hoverHandle = -1;
      else if (FreeTransformOn(doc) && sFreeMode == kFreeRotate && hoverHandle >= 0)
         hoverHandle = 8;
      if (hovered && sOp.kind == 0)
      {
         if (panTool)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         else if (hoverHandle >= 0 && hoverHandle < 8)
            ImGui::SetMouseCursor((hoverHandle % 4) == 1 ? ImGuiMouseCursor_ResizeNS : (hoverHandle % 4) == 3 ? ImGuiMouseCursor_ResizeEW   // ui-audit-ok: model value (prediction lookahead samples / handle index modulo / visible items)
                                  : (hoverHandle == 0 || hoverHandle == 4) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
         else if (hoverHandle == 8)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
         else if (hoverGuide >= 0)
            ImGui::SetMouseCursor(guideAxis == 0 ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
      }
      // Just outside a corner turns the layer: a curved-arrow badge says so (ImGui has no rotate cursor).
      if (hovered && !panTool && ((sOp.kind == 0 && hoverHandle == 8) || sOp.kind == 3 || sOp.kind == 21))
      {
         const ImVec2 c(mouse.x + tok::space_4, mouse.y + tok::space_4);
         const float r = tok::view2d_canvas_r_lg, a0 = -2.6f, a1 = tok::view2d_a1;
         for (int pass = 0; pass < 2; pass++)
         {
            const ImU32 col = pass == 0 ? tok::U32(tok::canvas_shade, tok::view2d_canvas_alpha_lg) : tok::U32(tok::canvas_ink);
            const float th = pass == 0 ? 3.5f : 1.5f;
            dl->PathArcTo(c, r, a0, a1, tok::view2d_arc_segments);
            dl->PathStroke(col, 0, th);
            const ImVec2 tip(c.x + r * std::cos(a1), c.y + r * std::sin(a1));
            const ImVec2 tan(-std::sin(a1), std::cos(a1));   // the way the arc runs at its end
            const ImVec2 nrm(std::cos(a1), std::sin(a1));
            const ImVec2 p1(tip.x - tan.x * tok::view2d_canvas_ratio + nrm.x * tok::view2d_p1_scale, tip.y - tan.y * tok::view2d_canvas_ratio + nrm.y * tok::view2d_p1_scale);
            const ImVec2 p2(tip.x - tan.x * tok::view2d_canvas_ratio - nrm.x * tok::view2d_p1_scale, tip.y - tan.y * tok::view2d_canvas_ratio - nrm.y * tok::view2d_p1_scale);
            if (pass == 0)
               dl->AddTriangleFilled(ImVec2(tip.x + tan.x * tok::view2d_arrow_x_sm, tip.y + tan.y * tok::view2d_arrow_x_sm), p1, p2, col);
            else
               dl->AddTriangleFilled(tip, p1, p2, col);
         }
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
         const ImVec2 src = sCloneOffsetSet && (sCloneAligned || sOp.kind == 7) ? ToScreen(doc, sMouseDoc.x + sCloneOffset.x, sMouseDoc.y + sCloneOffset.y)
                                            : ToScreen(doc, sCloneSrc.x, sCloneSrc.y);
         dl->PushClipRect(sViewMn, sViewMx, true);
         for (int pass = 0; pass < 2; pass++)
         {
            const ImU32 col = pass == 0 ? tok::U32(tok::canvas_shade, tok::view2d_canvas_alpha_sm) : tok::U32(tok::canvas_ink);
            const float th = pass == 0 ? 3.0f : 1.0f;
            dl->AddLine(ImVec2(src.x - tok::view2d_line_x, src.y), ImVec2(src.x + tok::view2d_line_x, src.y), col, th);
            dl->AddLine(ImVec2(src.x, src.y - tok::view2d_line_x), ImVec2(src.x, src.y + tok::view2d_line_x), col, th);
            dl->AddCircle(src, tok::view2d_canvas_r, col, tok::view2d_ring_segments, th);
         }
         dl->PopClipRect();
      }
      if (sOp.kind != 7)
         sPenN = 0;
      else if (sTool == kBrush && !sSurfaceStroke)
         DrawPredicted(doc, dl);
      if (hovered && !panTool && BrushTool(sTool) && ((!io.KeyAlt && (sOp.kind == 0 || sOp.kind == 7)) || sOp.kind == 28 ||
                                                      (sizeDragHint && sOp.kind == 0)))
      {
         dl->PushClipRect(sViewMn, sViewMx, true);
         DrawBrushCursor(dl, sOp.kind == 28 ? sOp.start : mouse);
         dl->PopClipRect();
      }

      const bool inRulerTop = d.rulers && mouse.y < mn.y + kRuler && mouse.x > mn.x + kRuler;
      const bool inRulerLeft = d.rulers && mouse.x < mn.x + kRuler && mouse.y > mn.y + kRuler;
      const bool sizeDrag = BrushTool(sTool) && io.KeyAlt && (io.KeyCtrl || io.KeySuper) && !space;
      if (hovered && space && !io.KeyShift && (io.KeyCtrl || io.KeySuper || io.KeyAlt) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         // Cmd + Space (or Alt + Space) drag scrubs the zoom about the press; a click steps in (Alt: out).
         sOp = Op();
         sOp.kind = 19;
         sOp.start = mouse;
         sOp.zoom0 = sZoom;
         sOp.pan0 = sPan;
      }
      else if (hovered && sizeDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         // Ctrl + Opt drag on a brush: across sets the size, up and down the hardness (Photoshop's HUD resize).
         sOp = Op();
         sOp.kind = 28;
         sOp.start = mouse;
         sOp.hx = ToolBrush().size;
         sOp.hy = ToolBrush().hardness;
      }
      else if (hovered && space && io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         // Shift + Space drag turns the view about its centre (Krita's key; R is Blur / Sharpen here).
         BeginOp(doc, 22, mouse, nullptr);
         sOp.angle0 = sViewAngle;
      }
      else if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (panTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
         BeginOp(doc, 4, mouse, nullptr);
      else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && sAdjPick.key != 0 && sAdjPick.doc == sDocUid &&
               !inRulerTop && !inRulerLeft)
      {
         // Curves / Hue from the picture: the tone (or hue) under the pointer is what the drag moves.
         float c[3] = { 0, 0, 0 };
         doc->Pick(sMouseDoc.x, sMouseDoc.y, c, 3, 0);
         const int key = sAdjPick.key;
         BeginOp(doc, 23, mouse, "Adjust from picture");
         if (!WithAdjKey(doc, key, [&](auto& x)
         {
            if (x.adjType == Scene2DNode::kAdjCurves)
            {
               const int ch = std::clamp(AdjChannel(key), 0, 3);
               const float v = ch == 0 ? ColourMath::Luma709(c[0], c[1], c[2]) : c[ch - 1];
               std::vector<float>& cv = ch == 0 ? x.curve : x.curveCh[ch - 1];
               sOp.hx = std::clamp(v, 0.0f, 1.0f);
               sOp.hy = CurveAt(cv, sOp.hx);
            }
            else
               sOp.hy = x.adjAmt;
         }))
         {
            sAdjPick = AdjPick();
            sOp = Op();
         }
         else
            sOp.guideIndex = key;
      }
      else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         // Tools that work on what is under the cursor pick that layer first (the matrix's target rule). The brush,
         // gradient, clone and shape tools keep the active layer: they add, they do not edit what is there.
         // Click tools find the layer under the click. Stroke tools (brush, retouch) stay on the active layer, as in
         // Photoshop, where only Move auto-selects: a blur stroke that starts beside a word must not jump to the page.
         const bool targets = sTool == kWand || sTool == kFill || sTool == kEraser || sTool == kFloodErase || sTool == kBgErase ||
                              sTool == kBlemish || sTool == kObjectSel || sTool == kPerspective ||
                              (sTool == kMeshWarp && !NearMeshHandle(doc, mouse));
         if (targets && !(inRulerTop || inRulerLeft) && !io.KeyAlt && !sEditMask && !doc->quickMask)
            ToolTargetAt(doc, sMouseDoc);
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
         {
            // A click zooms a step; a drag zooms smoothly about where it began (right in, left out).
            sOp = Op();
            sOp.kind = 19;
            sOp.start = mouse;
            sOp.zoom0 = sZoom;
            sOp.pan0 = sPan;
         }
         else if ((sTool == kClone || sTool == kHeal) && io.KeyAlt)
         {
            sCloneSrc = sMouseDoc;
            sHaveCloneSrc = true;
            sCloneOffsetSet = false;
            // A source off the active layer's pixels (a cut-out eye, a fresh empty layer) has nothing to copy on that
            // layer alone: the source turns to All layers, so the stroke takes what is seen there.
            if (const Layer* a = Active(doc); a != nullptr && !sCloneAll && !doc->IsObjectLayer(*a))
            {
               float c[8];
               ContentCorners(doc, *a, c);
               if (a->kind != Kind::Paint || !InQuad(c, sMouseDoc))
                  sCloneAll = true;
            }
         }
         else if (sTool == kPick || ((sTool == kBrush || sTool == kFill) && io.KeyAlt))
         {
            // The eyedropper; Alt with the brush or bucket picks too. Alt on the eyedropper sets the background.
            sOp = Op();
            sOp.kind = 11;
            sOp.selOp = sTool == kPick && io.KeyAlt ? 1 : 0;
         }
         else if (sTool == kUndoBrush && io.KeyAlt)
         {
            // Alt-click: the layer as it is now becomes what the undo brush paints back to.
            if (Layer* al = Active(doc))
               doc->SetUndoSource(al->id);
         }
         else if (BrushTool(sTool))
            BeginStroke(doc, sMouseDoc, io.KeyShift && !RetouchTool(sTool));
         else if (sTool == kFloodErase || sTool == kBlemish)
         {
            if (!PaintRefused(doc))
            {
               LivePixelCopy(doc);
               Layer* al = Active(doc);
               if (al != nullptr && al->kind == Kind::Paint)
               {
                  PushUndoCheckpoint(sTool == kBlemish ? "Blemish removal" : "Flood erase");
                  if (sTool == kBlemish)
                     doc->Blemish(al->id, sMouseDoc.x, sMouseDoc.y, sBlemishSize * 0.5f);
                  else
                     doc->FloodErase(al->id, sMouseDoc.x, sMouseDoc.y, sTolerance, sContiguous, sAllLayers);
                  Touch();
               }
            }
         }
         else if (sTool == kStylePick)
         {
            // The first click holds a layer's style; each later click gives it to the layer clicked. Alt picks again.
            const int hit = HitLayer(doc, sMouseDoc);
            if (hit != 0 && (sStyleFrom == 0 || io.KeyAlt || doc->FindLayer(sStyleFrom) == nullptr))
               sStyleFrom = hit;
            else if (hit != 0 && hit != sStyleFrom)
            {
               PushUndoCheckpoint("Paste style");
               doc->CopyLayerStyle(sStyleFrom, hit, sStyleParts);
               Touch();
            }
         }
         else if (sTool == kPatch && !doc->HasSelection())
         {
            // Patch with nothing selected draws the area to mend freehand first (Photoshop's and Affinity's patch
            // tool); the next drag moves it onto clean pixels.
            sOp = Op();
            sOp.kind = 10;
            sOp.selOp = SelOpFor(io);
            sOp.path = { sMouseDoc.x, sMouseDoc.y };
            sOp.start = mouse;
         }
         else if (sTool == kObjectSel || sTool == kPatch || sTool == kMeasure)
         {
            sOp = Op();
            sOp.kind = sTool == kObjectSel ? 24 : sTool == kPatch ? 25 : 26;
            sOp.docStart = sMouseDoc;
            sOp.start = mouse;
            sOp.selOp = SelOpFor(io);
         }
         else if (sTool == kMeshWarp)
            MeshPress(doc, mouse);
         else if (sTool == kPerspective)
         {
            // A corner of the active layer pulled on its own: the card takes the perspective.
            Layer* al = Active(doc);
            float c[8];
            if (al != nullptr && CanWarp(doc) && doc->Corners(*al, c))
            {
               int best = -1;
               float bd = 12.0f;
               for (int k = 0; k < 4; k++)
               {
                  const ImVec2 q = ToScreen(doc, c[k * 2], c[k * 2 + 1]);
                  const float dd = std::hypot(q.x - mouse.x, q.y - mouse.y);
                  if (dd < bd) { bd = dd; best = k; }
               }
               if (best >= 0)
               {
                  BeginOp(doc, 18, mouse, "Perspective");
                  sOp.warpHandle = best * 2;   // corner -> handle (TL 0, TR 2, BR 4, BL 6)
                  sOp.perspective = false;
                  sOp.docStart = ToDoc(doc, mouse);
               }
            }
         }
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
            const bool closing = sPoly.size() >= tok::view2d_closing_limit && (std::hypot(mouse.x - first.x, mouse.y - first.y) < tok::view2d_closing_limit_lg ||
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
            SelStep("Magic wand");
            doc->SelectWand(sMouseDoc.x, sMouseDoc.y, sTolerance, sContiguous, SelOpFor(io), sAllLayers, al != nullptr ? al->id : 0);
         }
         else if (sTool == kCrop)
         {
            const int h = HoverCropHandle(doc, mouse);
            sOp = Op();
            sOp.start = mouse;
            std::copy(sCrop, sCrop + 4, sOp.crop0);
            std::copy(sCrop, sCrop + 4, sOp.cropWas);
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
            // Skew / Distort / Perspective pull the corners (one layer; a group or several layers keep scale and turn).
            const int fm = FreeTransformOn(doc) ? sFreeMode : kFreeAny;
            const bool pullMode = fm == kFreeSkew || fm == kFreeDistort || fm == kFreePerspective;
            if (sGizmoHot >= 0)
            {
               BeginOp(doc, 16, mouse, sGizmoMode == 0 ? "Move layer" : sGizmoMode == 1 ? "Rotate layer" : "Scale layer");
               sOp.gizmoAxis = sGizmoHot;
            }
            else if (groupT && hoverHandle >= 0)
            {
               BeginOp(doc, hoverHandle < 8 ? 20 : 21, mouse, hoverHandle < 8 ? "Scale layers" : "Rotate layers");
               SelBounds(doc, sOp.g0, sOp.g0 + 2);
               if (hoverHandle < 8)
               {
                  sOp.hx = kHandleAt[hoverHandle][0];
                  sOp.hy = kHandleAt[hoverHandle][1];
                  sOp.fromCentre = io.KeyAlt;
               }
            }
            else if (hoverHandle >= 0 && hoverHandle < 8 && ((io.KeyCtrl || io.KeySuper) || pullMode) && CanWarp(doc))
            {
               // Cmd/Ctrl: a corner distorts (with Alt+Shift, perspective), an edge skews. The modes say it instead.
               const bool corner = hoverHandle % 2 == 0;
               BeginOp(doc, 18, mouse, corner ? "Distort layer" : "Skew layer");
               sOp.warpHandle = hoverHandle;
               sOp.perspective = corner && (fm == kFreePerspective || (fm == kFreeAny && io.KeyAlt && io.KeyShift));
               sOp.warpStyle = fm == kFreeSkew && corner ? 1 : fm == kFreeDistort && !corner ? 2 : 0;
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
            else if (!(io.KeyCtrl || io.KeySuper) && !FreeTransformOn(doc) && doc->HasSelection() &&
                     doc->SelectionAt((int)std::floor(sMouseDoc.x), (int)std::floor(sMouseDoc.y)) > 0.0f && CanLift(doc))
            {
               // Inside the marquee: the selected pixels move, a hole is left (Photoshop's Move with a selection).
               const size_t undo0 = gUndoStack.size();
               PushUndoCheckpoint("Move selection");
               if (LiftSelection(doc))
                  BeginOp(doc, 1, mouse, nullptr);
               else
                  gUndoStack.resize(undo0);
            }
            else
            {
               int hit = sAutoSelect || io.KeyCtrl || io.KeySuper ? HitLayer(doc, sMouseDoc) : 0;
               // A click picks the outermost group the layer sits in (Photoshop's Auto-Select Group, Figma);
               // Cmd/Ctrl-click reaches the layer itself.
               if (!(io.KeyCtrl || io.KeySuper))
                  hit = OuterGroup(doc, hit);
               if (hit != 0)
               {
                  // A layer inside a picked group moves the group (Photoshop's Auto-Select Group).
                  bool inPicked = false;
                  for (int id : sSel)
                     if (id != hit && doc->IsInside(hit, id))
                        inPicked = true;
                  if (inPicked && !io.KeyShift)
                     ;
                  else if (io.KeyShift)
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
                  // With no layers to select, dragging moves the canvas; otherwise dragging empty canvas rubber-bands.
                  if (doc->Layers().empty())
                     BeginOp(doc, 4, mouse, nullptr);
                  else
                  {
                     BeginOp(doc, 17, mouse, nullptr);
                     sOp.docStart = sMouseDoc;
                     sOp.selOp = io.KeyShift ? 1 : 0;
                     sOp.before.clear();
                  }
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
         else if (sOp.kind == 20)
            ScaleGroup(doc, mouse);
         else if (sOp.kind == 21)
            RotateGroup(doc, mouse);
         else if (sOp.kind == 16)
            DragGizmo3D(doc, mouse);
         else if (sOp.kind == 18)
            WarpActive(doc, mouse);
         else if (sOp.kind == 27)
            MeshDrag(doc);
         else if (sOp.kind == 26)
         {
            sMeasureA = sOp.docStart;
            sMeasureB = sMouseDoc;
            sMeasureOn = true;
         }
         else if (sOp.kind == 4)
            sPan = ImVec2(sOp.pan0.x + mouse.x - sOp.start.x, sOp.pan0.y + mouse.y - sOp.start.y);
         else if (sOp.kind == 28)
         {
            BrushEngine::Brush& b = ToolBrush();
            b.size = std::clamp(sOp.hx + (mouse.x - sOp.start.x) * tok::view2d_canvas_v / std::max(tok::view2d_canvas_min, sZoom), 1.0f, tok::view2d_canvas_hi);
            b.hardness = std::clamp(sOp.hy - (mouse.y - sOp.start.y) * tok::view2d_hardness_v, 0.0f, 1.0f);
         }
         else if (sOp.kind == 22)
         {
            const ImVec2 vc((sViewMn.x + sViewMx.x) * 0.5f, (sViewMn.y + sViewMx.y) * 0.5f);
            const float turn = std::atan2(mouse.y - vc.y, mouse.x - vc.x) - std::atan2(sOp.start.y - vc.y, sOp.start.x - vc.x);
            sViewAngle = sOp.angle0;
            sPan = sOp.pan0;
            TurnViewAbout(doc, SnapViewAngle(sOp.angle0 + turn) - sOp.angle0, vc);
         }
         else if (sOp.kind == 5)
            DragCrop(doc, mouse);
         else if (sOp.kind == 19)
         {
            if (std::fabs(mouse.x - sOp.start.x) >= tok::view2d_canvas_limit_sm)
            {
               sZoom = sOp.zoom0;
               sPan = sOp.pan0;
               ZoomAt(doc, std::exp((mouse.x - sOp.start.x) * tok::view2d_exp_reach), sOp.start);
            }
         }
         else if (sOp.kind == 7)
         {
            if (sSurfaceStroke)
               doc->SurfacePaintTo(sMouseDoc.x, sMouseDoc.y, PenNow());
            else
            {
               if (sTool == kBrush)
                  doc->PaintFollow(sBrush);
               const float pen = PenNow(doc);
               doc->PaintTo(sMouseDoc.x, sMouseDoc.y, pen);
               doc->PaintTick(ImGui::GetIO().DeltaTime);
               if (!sGroupStrokeIds.empty())
                  sGroupStrokePts.push_back({ sMouseDoc.x, sMouseDoc.y, pen });
               NotePenSample(sMouseDoc);
            }
         }
         else if (sOp.kind == 10)
         {
            const ImVec2 last = ToScreen(doc, sOp.path[sOp.path.size() - 2], sOp.path.back());
            if (std::hypot(mouse.x - last.x, mouse.y - last.y) >= tok::view2d_canvas_limit_xs)
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
         else if (sOp.kind == 23)
         {
            const float dx = (mouse.x - sOp.start.x) / tok::view2d_dx_ratio, dy = (sOp.start.y - mouse.y) / tok::view2d_dy_ratio;
            WithAdjKey(doc, sOp.guideIndex, [&](auto& x)
            {
               if (x.adjType == Scene2DNode::kAdjCurves)
               {
                  const int ch = std::clamp(AdjChannel(sOp.guideIndex), 0, 3);
                  std::vector<float>& cv = ch == 0 ? x.curve : x.curveCh[ch - 1];
                  const int i = CurvePointAt(cv, sOp.hx);
                  if (i >= 0)
                     cv[(size_t)i * 2 + 1] = std::clamp(sOp.hy + dy, 0.0f, 1.0f);
               }
               else
               {
                  float lo = 0.0f, hi = 1.0f;
                  Scene2DNode::AdjRange(x.adjType, lo, hi);
                  x.adjAmt = std::clamp(sOp.hy + dx * (hi - lo), lo, hi);
               }
            });
            Touch();
         }
         else if (sOp.kind == 11)
         {
            // On a painted 3D surface the eyedropper reads the paint, not the lit picture.
            float c[4];
            Layer* a = Active(doc);
            if (a != nullptr && sSurfacePaint && a->HasSkin() && doc->SurfaceColor(a->id, sMouseDoc.x, sMouseDoc.y, c))
               std::copy(c, c + 3, sOp.selOp == 1 ? sBg : sFg);
            else
               doc->Pick(sMouseDoc.x, sMouseDoc.y, sOp.selOp == 1 ? sBg : sFg, sPickSize,
                         sPickLayer && a != nullptr && a->kind == Kind::Paint ? a->id : 0);
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
            else if (sOp.kind == 19 && std::fabs(mouse.x - sOp.start.x) < tok::view2d_canvas_limit_sm)
               ZoomAt(doc, io.KeyAlt ? 0.5f : tok::view2d_zoom_at_ratio, mouse);
            else if (sOp.kind == 5 && !CropBoxOk())
               std::copy(sOp.cropWas, sOp.cropWas + 4, sCrop);   // a click without a drag keeps the box there was
            else if (sOp.kind == 13)
            {
               Layer* pl = Active(doc);
               std::vector<Shape>* items = Items(pl);
               if (items != nullptr && sPenItem >= 0 && sPenItem < (int)items->size() && (*items)[sPenItem].closed)
                  sPenItem = -1;
            }
            else if (sOp.kind == 17)
               MarqueeLayers(doc, sOp.docStart, sMouseDoc, true);   // the press already cleared it unless Shift
            else if (sOp.kind == 24 && std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) >= tok::view2d_canvas_limit_sm)
            {
               Layer* al = Active(doc);
               SelStep("Object selection");
               doc->SelectObject(sOp.docStart.x, sOp.docStart.y, sMouseDoc.x, sMouseDoc.y, sOp.selOp, sAllLayers, al != nullptr ? al->id : 0);
               if (sObjSoft)
                  doc->ModifySelection(4, 2);
            }
            else if (sOp.kind == 25 && doc->HasSelection() && std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) >= tok::view2d_canvas_limit_sm &&
                     !PaintRefused(doc))
            {
               // The selection is mended from where it was dragged to.
               LivePixelCopy(doc);
               Layer* al = Active(doc);
               if (al != nullptr && al->kind == Kind::Paint)
               {
                  PushUndoCheckpoint("Patch selection");
                  const float pdx = sMouseDoc.x - sOp.docStart.x, pdy = sMouseDoc.y - sOp.docStart.y;
                  PatchApply(doc, al->id, pdx, pdy);
                  Touch();
               }
            }
            else if (sOp.kind == 27)
               MeshRelease(doc);
            else if (sOp.kind == 15)
               TextRelease(doc, mouse);
            else if (sOp.kind == 8 && std::hypot(mouse.x - ToScreen(doc, sOp.docStart.x, sOp.docStart.y).x,
                                                 mouse.y - ToScreen(doc, sOp.docStart.x, sOp.docStart.y).y) > tok::view2d_canvas_limit_xs)
               GradientTo(doc, sOp.docStart, sMouseDoc);
            else if (sOp.kind == 9)
            {
               // A click without a drag drops the selection, as in every image editor.
               if (std::hypot(mouse.x - sOp.start.x, mouse.y - sOp.start.y) < tok::view2d_canvas_limit_sm)
               {
                  if (sOp.selOp == Scene2DNode::kSelNew && doc->HasSelection())
                  {
                     SelStep("Deselect");
                     doc->Deselect();
                  }
               }
               else
               {
                  const ImVec2 e = MarqueeEnd(sOp.docStart, sMouseDoc), s0 = MarqueeStart(sOp.docStart, e);
                  SelStep("Marquee");
                  doc->SelectRect(s0.x, s0.y, e.x, e.y, sOp.selOp, sFeather, sMarqueeShape == 1, sMarqueeAA);
                  TargetSelection(doc);
               }
            }
            else if (sOp.kind == 10)
            {
               if (sOp.path.size() >= tok::view2d_canvas_limit_sm_i)
               {
                  SelStep("Lasso");
                  doc->SelectPolygon(sOp.path, sOp.selOp, sFeather, true);
                  TargetSelection(doc);
               }
               else if (sOp.selOp == Scene2DNode::kSelNew && doc->HasSelection())
               {
                  SelStep("Deselect");
                  doc->Deselect();
               }
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
      DrawRasterAsk(doc);
      DrawFillDialog(doc);
   }
   // ui-content-end

   // Between two groups of a bar: space_2, a 1 px hairline as tall as the chips' inner area, space_2 (Photopea's divider).
   void BarDivider()
   {
      const float top = ImGui::GetItemRectMin().y;
      FormParts::Inline(0.0f, tok::space_2);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float x = std::round(p.x) + tok::px_align;
      ImGui::GetWindowDrawList()->AddLine(ImVec2(x, top + tok::space_1_5), ImVec2(x, top + sRowH - tok::space_1_5), ImGui::GetColorU32(ImGuiCol_Separator), tok::view2d_bar_divider_stroke);
      ImGui::SetCursorScreenPos(ImVec2(p.x, top));
      Ui::Claim(ImVec2(tok::view2d_spacer_w, sRowH));
      FormParts::Inline(0.0f, tok::space_2);
   }

   // ---- the pixel tools' context bar ---------------------------------------

   void BarSlider(const char* id, float* v, float lo, float hi, const char* fmt, float w = 0.0f)
   {
      // As wide as its longest text ("Hardness 100%") plus the well's padding, so bars carry no empty wells.
      char a[64], b[64];
      std::snprintf(a, sizeof a, fmt, lo);
      std::snprintf(b, sizeof b, fmt, hi);
      const float fit = std::max(ImGui::CalcTextSize(a).x, ImGui::CalcTextSize(b).x) + tok::view2d_fit_ratio * tok::space_2 + tok::space_1;
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
      BarSlider("##v2dselmod", &sSelModPx, params::view2d::sel_mod_px_lo, params::view2d::sel_mod_px_hi, "%.0f px", kWellL);
      static const char* kOps[5] = { "Expand##v2dsm0", "Contract##v2dsm1", "Smooth##v2dsm2", "Border##v2dsm3", "Feather##v2dsm5" };
      static const char* kUndo[5] = { "Expand selection", "Contract selection", "Smooth selection", "Border selection", "Feather selection" };
      static const int kOp[5] = { 0, 1, 2, 3, 5 };
      for (int i = 0; i < 5; i++)
      {
         if (i > 0)
            FormParts::Inline(0.0f, tok::space_1);
         if (TextChip(L(kOps[i]), false, nullptr))
         {
            PushUndoCheckpoint(kUndo[i]);
            doc->ModifySelection(kOp[i], (int)std::lround(sSelModPx));
            Touch();
         }
      }
      ImGui::EndDisabled();
   }

   // A tool's less-used settings: one icon at the bar's end opens them as a vertical form (label, well), the way
   // Photoshop's brush settings sit behind one button. Bars keep only the everyday controls, so none overflows.
   // A More.. form: panel rows (PanelKit::Row() tall, kRowGap apart), a 150 px well column, capped height with a scroll.
   // A More.. form is folded into sections: a header click folds its rows, remembered per header. The More*
   // rows below draw nothing while their section is folded (sMoreOpen), so the popover stays short.
   bool sMoreOpen = true;


   // A More.. form is the compact strip (tokens.json "compact"): one column, label then well, kPopoverW wide.
   struct MoreRows
   {
      float keepW, keepL;
      UiDensity::Compact compact;   // declared after keep*: ends (pops) before they are restored, in this popup
      MoreRows() : keepW(sFieldW), keepL(sLabelW)
      {
         sLabelW = UiDensity::M().label + tok::space_4;   // "Accumulation" at compact text fits
         // Fixed strip widths, like the main Settings window's fixed control width: a window resized taller (or a
         // popover) never stretches its rows.
         sFieldW = UiDensity::kPopoverW - sLabelW - tok::view2d_field_w_ratio * ImGui::GetStyle().WindowPadding.x; // padding both sides
      }
      ~MoreRows()
      {
         compact.End();
         sFieldW = keepW;
         sLabelW = keepL;
      }
   };
   struct MorePopupScope
   {
      MorePopupScope() { ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, std::max(tok::view2d_sel_mod_chips_min, ImGui::GetMainViewport()->Size.y * tok::view2d_more_popup_scope_ratio))); }
   };
   // The chip opens a settings window, not a popover: it stays open while you paint and closes like any floating
   // window (its close button, Esc, or the chip again). The window sits under the chip, right edges aligned.
   template <class F> void BarMore(const char* id, F&& body)
   {
      const std::string key = std::string("v2dmore/") + id;
      const bool open = SettingsWindow::IsOpen(key);
      const std::string chip = std::string(L("More..")) + id;
      if (TextChip(chip.c_str(), open, T("More settings")))
         SettingsWindow::Toggle(key);
      const ImVec2 chipMx = ImGui::GetItemRectMax();
#ifdef IMGUI_ENABLE_TEST_ENGINE
      View2DMoreProbe& probe = View2DMoreProbes()[id];
      probe.frame = ImGui::GetFrameCount();
      probe.chipMn = ImGui::GetItemRectMin();
      probe.chipMx = chipMx;
      probe.open = false;
#endif
      SettingsWindow::Draw(key, T("Settings"), ImVec2(chipMx.x, chipMx.y + tok::space_1), [&]()
      {
         {
            MoreRows rows; // pops its style vars here, while the window is still current
            sMoreOpen = true;
            body();
            sMoreOpen = true;
         }
#ifdef IMGUI_ENABLE_TEST_ENGINE
         probe.open = true;
         probe.winW = ImGui::GetCurrentWindow()->Size.x;
         probe.contentW = ImGui::GetCurrentWindow()->ContentSize.x;
#endif
      });
   }


   void MoreHeader(const char* text, bool first = false, bool startOpen = true)
   {
      sMoreOpen = PanelKit::FoldHeader(text, first, startOpen);
   }

   // A percent row in a BarMore form: v is 0..1 (or 0..hi/100), shown as percent.
   void MorePct(const char* label, float* v, float lo, float hi)
   {
      if (!sMoreOpen)
         return;
      const char* fmt = RowSlot(label, "%.0f%%");
      float pct = *v * 100.0f;
      if (FieldWell::Slider(FormParts::Id(label).c_str(), &pct, lo, hi, fmt))
         *v = pct / 100.0f;
   }

   void MoreSlider(const char* label, float* v, float lo, float hi, const char* fmt)
   {
      if (!sMoreOpen)
         return;
      FieldWell::Slider(FormParts::Id(label).c_str(), v, lo, hi, RowSlot(label, fmt));
   }

   void MoreCheck(const char* label, bool* v)
   {
      if (sMoreOpen)
         FormParts::Check(label, v);
   }

   // A dropdown on a tool bar: the value well alone, names translated.
   bool BarCombo(const char* id, int* v, const char* const* names, int n, float w)
   {
      bool changed = false;
      ImGui::SetNextItemWidth(w);
      if (MenuParts::BeginCombo(id, T(names[std::clamp(*v, 0, n - 1)])))
      {
         for (int i = 0; i < n; i++)
            if (MenuParts::Choice(T(names[i]), *v == i) && *v != i)
            {
               *v = i;
               changed = true;
            }
         MenuParts::EndCombo();
      }
      return changed;
   }

   // A dropdown row in a BarMore form.
   bool MoreCombo(const char* label, int* v, const char* const* names, int n)
   {
      if (!sMoreOpen)
         return false;
      FieldLabel(label);
      return BarCombo(FormParts::Id(label).c_str(), v, names, n, sFieldW);
   }

   // The material stroke's rows: light and height, canvas texture, dual tip, colour along, wet mix.
   void MaterialRows(BrushEngine::Brush& b)
   {
      MoreHeader(T("Material"), false, false);
      static std::vector<const char*> mats;
      if (mats.empty())
      {
         mats.push_back("Flat");
         for (const std::string& n : BrushEngine::MaterialNames())
            mats.push_back(n.c_str());
      }
      int m = b.material + 1;
      if (MoreCombo(T("Look"), &m, mats.data(), (int)mats.size()))
         b.material = m - 1;
      if (b.material >= 0)
      {
         static const char* kProfiles[4] = { "Flat", "Round", "Ridge", "Ribbon" };
         MoreCombo(T("Profile"), &b.profile, kProfiles, 4);
         MorePct(T("Depth"), &b.depth, params::view2d::depth_lo, params::view2d::depth_hi);
         MoreSlider(T("Light"), &b.lightAz, params::view2d::light_az_lo, params::view2d::light_az_hi, "%.0f°");
         MoreSlider(T("Light height"), &b.lightEl, params::view2d::light_el_lo, params::view2d::light_el_hi, "%.0f°");
      }
      MoreHeader(T("Texture"), false, false);
      static const char* kGrains[4] = { "Off", "Paper", "Weave", "Noise" };
      MoreCombo(T("Grain"), &b.grain, kGrains, 4);
      if (b.grain > 0)
      {
         static const char* kGrainModes[3] = { "Multiply", "Subtract", "Height" };
         MoreCombo(T("Grain mode"), &b.grainMode, kGrainModes, 3);
         MorePct(T("Grain scale"), &b.grainScale, params::view2d::grain_scale_lo, params::view2d::grain_scale_hi);
         MorePct(T("Grain depth"), &b.grainDepth, params::view2d::grain_depth_lo, params::view2d::grain_depth_hi);
      }
      MoreHeader(T("Dual tip"), false, false);
      static std::vector<const char*> tips;
      if (tips.empty())
      {
         tips.push_back("Off");
         for (const std::string& n : BrushEngine::TipNames())
            tips.push_back(n.c_str());
      }
      int d = b.dualTip + 1;
      if (MoreCombo(T("Second tip"), &d, tips.data(), (int)tips.size()))
         b.dualTip = d - 1;
      if (b.dualTip >= 0)
      {
         MorePct(T("Second size"), &b.dualScale, params::view2d::dual_scale_lo, params::view2d::dual_scale_hi);
         MorePct(T("Second scatter"), &b.dualScatter, params::view2d::dual_scatter_lo, params::view2d::dual_scatter_hi);
      }
      MoreHeader(T("Colour"), false, false);
      static const char* kAlong[3] = { "Flat", "Along", "Across" };
      MoreCombo(T("Gradient"), &b.colorAlong, kAlong, 3);
      if (sMoreOpen && b.colorAlong > 0)
      {
         FieldLabel(T("Second colour"));
         ImGui::ColorEdit3("##v2dcolb", b.colorB, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
         if (b.colorAlong == 1)
            MoreSlider(T("Repeat"), &b.colorPeriod, params::view2d::color_period_lo, params::view2d::color_period_hi, "%.0f px");
      }
      MorePct(T("Wet mix"), &b.wet, params::view2d::wet_lo, params::view2d::wet_hi);
      if (b.wet > 0.0f)
         MorePct(T("Refill"), &b.refill, params::view2d::refill_lo, params::view2d::refill_hi);
   }

   // A canvas node feeds the brush: its picture as the tip, the canvas grain, or the colour (Rebelle's custom
   // tips, Krita's pattern texture, Procreate's colour from a source). Kept by the document, so it saves.
   void BrushSourceRows(Scene2DNode* doc)
   {
      Scene2DNode::Document& d = doc->Doc();
      MoreHeader(T("From a node"), false, false);
      static const char* const kLabels[3] = { "Tip from", "Grain from", "Colour from" };
      for (int k = 0; k < 3; k++)
      {
         std::vector<std::string> names = { "Built in" };
         std::vector<uint64_t> uids = { 0 };
         int cur = 0;
         for (GraphNode& gn : gNodes)
         {
            if (gn.node.get() == doc || !View2DCanAdd(gn) || WouldCreateDocCycle(doc, gn.node.get()))
               continue;
            if (gn.uid == d.brushSrc[k])
               cur = (int)uids.size();
            names.push_back(gn.typeName + " " + std::to_string(gn.index));
            uids.push_back(gn.uid);
         }
         if (d.brushSrc[k] != 0 && cur == 0)
         {
            cur = (int)uids.size();
            names.push_back("Node gone");
            uids.push_back(d.brushSrc[k]);
         }
         std::vector<const char*> ptrs;
         for (const std::string& n : names)
            ptrs.push_back(n.c_str());
         int pick = cur;
         if (MoreCombo(T(kLabels[k]), &pick, ptrs.data(), (int)ptrs.size()) && pick != cur)
         {
            PushUndoCheckpoint("Brush source");
            d.brushSrc[k] = uids[pick];
            Touch();
         }
      }
      if (d.brushSrc[0] != 0 || d.brushSrc[1] != 0 || d.brushSrc[2] != 0)
      {
         bool live = d.brushLive;
         MoreCheck(T("Live"), &live);
         if (live != d.brushLive)
         {
            PushUndoCheckpoint("Brush source live");
            d.brushLive = live;
            Touch();
         }
      }
   }

   void MatchChips()
   {
      // Shown as Affinity shows it, a share of the colour range (kept 0..255 inside, as patches and the wand read it).
      float tol = (float)sTolerance * (100.0f / 255.0f);
      BarSlider("##v2dtol", &tol, params::view2d::tol_lo, params::view2d::tol_hi, "Tolerance %.0f%%", kWellL);
      sTolerance = std::clamp((int)std::lround(tol * 2.55f), 0, 255);  // ui-audit-ok: unit (percent to 0-255)
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
            if (FieldWell::Slider("##v2dsymn", &n, params::Scene2D::n_lo, params::Scene2D::n_hi, "%.0f"))
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
         BarSlider("##v2drsize", &b.size, params::view2d::size_lo, params::view2d::size_hi, "Size %.0f px", kWellL);
         float hard = b.hardness * tok::view2d_hard_ratio, op = b.opacity * 100.0f, flow = b.flow * 100.0f;  // ui-audit-ok: unit (0-1 brush param shown as percent)
         BarSlider("##v2drhard", &hard, params::view2d::hard_lo, params::view2d::hard_hi, "Hardness %.0f%%", kWellL);
         BarSlider("##v2drop", &op, params::view2d::op_lo, params::view2d::op_hi, "Opacity %.0f%%", kWellL);
         BarSlider("##v2drflow", &flow, params::view2d::flow_lo, params::view2d::flow_hi, "Flow %.0f%%", kWellL);
         b.hardness = hard / 100.0f;
         b.opacity = op / tok::view2d_v2drsize_scale;
         b.flow = flow / 100.0f;
         if (sTool == kDodge || sTool == kBurn)
         {
            gap();
            static const char* kRange[3] = { "Shadows", "Midtones", "Highlights" };
            BarCombo("##v2drange", &b.range, kRange, 3, kWellL);
         }
         if (sTool == kDodge || sTool == kBurn || sTool == kSponge)
         {
            gap();
            if (TextChip(L("Protect hue##v2dphue"), b.protectHue, nullptr))
               b.protectHue = !b.protectHue;
         }
         if (sTool == kSponge)
         {
            next();
            static const char* kSpongeModes[3] = { "Saturate", "Desaturate", "Vibrance" };
            BarCombo("##v2dspongemode", &b.spongeMode, kSpongeModes, 3, kWellL);
         }
         if (sTool == kUndoBrush)
         {
            gap();
            if (TextChip(L("Protect alpha##v2dupa"), b.lockAlpha, nullptr))
               b.lockAlpha = !b.lockAlpha;
            next();
            static const char* kUndoBlend[7] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference" };
            BarCombo("##v2dublend", &b.sourceBlend, kUndoBlend, 7, kWellM);
         }
         if (sTool == kSharpen)
         {
            gap();
            static const char* kSharp[3] = { "Clarity", "Unsharp", "Harsh" };
            BarCombo("##v2dsharpmode", &b.sharpMode, kSharp, 3, kWellL);
         }
         if (sTool == kColourReplace || sTool == kBgErase)
         {
            gap();
            float tol = b.keyTolerance * 100.0f;
            BarSlider("##v2dkeytol", &tol, params::view2d::tol_lo, params::view2d::tol_hi, "Tolerance %.0f%%", kWellL);
            b.keyTolerance = tol / 100.0f;
         }
         if (sTool == kClone || sTool == kHeal)
         {
            gap();
            if (TextChip(L("Aligned##v2dalign"), sCloneAligned, T("The source moves with each stroke")))
               sCloneAligned = !sCloneAligned;
            next();
            if (TextChip(L("All layers##v2dcloneall"), sCloneAll, T("Copy from the whole picture")))
               sCloneAll = !sCloneAll;
            gap();
            BarSlider("##v2dsrot", &b.sourceRotate, params::view2d::source_rotate_lo, params::view2d::source_rotate_hi, "Rotation %.0f°", kWellM);
            next();
            float sc = b.sourceScale * 100.0f;
            BarSlider("##v2dsscale", &sc, params::view2d::sc_lo, params::view2d::sc_hi, "Scale %.0f%%", kWellM);
            b.sourceScale = sc / 100.0f;
            next();
            if (TextChip(L("Flip X##v2dsflipx"), b.sourceFlipX, nullptr))
               b.sourceFlipX = !b.sourceFlipX;
            next();
            if (TextChip(L("Flip Y##v2dsflipy"), b.sourceFlipY, nullptr))
               b.sourceFlipY = !b.sourceFlipY;
            if (sTool == kClone)
            {
               next();
               static const char* kCloneBlend[7] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference" };
               BarCombo("##v2dsblend", &b.sourceBlend, kCloneBlend, 7, kWellM);
            }
         }
         gap();
         SymmetryChip(doc);
         FormParts::Inline(0.0f, tok::space_1);
         BarMore("##v2drmore", [&]()
         {
            MoreHeader(T("Tip"), true);
            MorePct(T("Spacing"), &b.spacing, params::view2d::spacing_lo, params::view2d::spacing_hi);
            MorePct(T("Roundness"), &b.roundness, params::view2d::roundness_lo, params::view2d::roundness_hi);
            MoreSlider(T("Angle"), &b.angle, params::view2d::angle_lo, params::view2d::angle_hi, "%.0f°");
            MorePct(T("Smoothing"), &b.smoothing, params::view2d::smoothing_lo, params::view2d::smoothing_hi);
            float stab = (float)b.stabiliser;
            MoreSlider(T("Stabiliser"), &stab, params::view2d::stab_lo, params::view2d::stab_hi, "%.0f");
            b.stabiliser = (int)std::lround(stab);
            MoreCheck(T("Wet edges"), &b.wetEdges);
            MoreHeader(T("Pen & tilt"));
            {
               Pairs g;
               MoreCheck(T("Size"), &b.pressureSize);
               MoreCheck(T("Opacity"), &b.pressureOpacity);
               MoreSlider(T("Response"), &b.pressureCurve, params::view2d::pressure_curve_lo, params::view2d::pressure_curve_hi, "%.2f");
               MoreCheck(T("Tilt size"), &b.tiltSize);
               MoreCheck(T("Tilt angle"), &b.tiltAngle);
               MoreCheck(T("Tilt flatness"), &b.tiltRound);
            }
         });
      }
      else if (sTool == kBrush || sTool == kEraser)
      {
         BrushEngine::Brush& b = ToolBrush();
         int& preset = sTool == kEraser ? sEraserPreset : sBrushPreset;
         const auto& presets = BrushEngine::Presets();
         ImGui::SetNextItemWidth(kWellL);
         const char* shown = preset >= 0 && preset < (int)presets.size() ? presets[preset].name : "";
         ImGui::SetNextWindowSizeConstraints(ImVec2(tok::view2d_window_w_sm, 0.0f), ImVec2(FLT_MAX, MenuParts::ListMaxH()));
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
               const bool chose = MenuParts::Choice(T(presets[i].name), i == preset);
               if (const unsigned thumb = erase ? 0u : BrushThumb(i))
               {
                  const ImVec2 rmx = ImGui::GetItemRectMax(), rmn = ImGui::GetItemRectMin();
                  const float cy = (rmn.y + rmx.y) * 0.5f;
                  Thumb::Add(ImGui::GetWindowDrawList(), (ImTextureID)(intptr_t)thumb, ImVec2(rmx.x - tok::view2d_pixel_bar_span - tok::space_2, cy - tok::space_3),
                                                       ImVec2(rmx.x - tok::space_2, cy + tok::space_3), ImVec2(0, 1), ImVec2(1, 0));
               }
               if (chose)
               {
                  preset = i;
                  b = presets[i].brush;
                  if (sTool == kEraser)
                     b.mode = BrushEngine::kErase;
                  // A material's colour is part of the look (gold, steel, denim): it becomes the foreground.
                  else if (presets[i].group != nullptr && std::strcmp(presets[i].group, "Material") == 0)
                     std::copy(b.color, b.color + 3, sFg);
               }
            }
            MenuParts::EndCombo();
         }
         next();
         const bool brush = &b == &sBrush;   // the brush's numbers can follow a modulator (sound, LFO); the eraser's do not
         if (brush)
            ModNextParam(kBrushSizeParam);
         ModBegin();
         BarSlider("##v2dsize", &b.size, params::view2d::size_lo, params::view2d::size_hi, "Size %.0f px", kWellL);
         ModEnd();
         float hard = b.hardness * tok::view2d_hard_ratio, op = b.opacity * 100.0f, flow = b.flow * 100.0f;  // ui-audit-ok: unit (0-1 brush param shown as percent)
         BarSlider("##v2dhard", &hard, params::view2d::hard_lo, params::view2d::hard_hi, "Hardness %.0f%%", kWellL);
         if (brush)
            ModNextParam(kBrushOpacityParam);
         ModBegin();
         BarSlider("##v2dop", &op, params::view2d::op_lo, params::view2d::op_hi, "Opacity %.0f%%", kWellL);
         ModEnd();
         if (brush)
            ModNextParam(kBrushFlowParam);
         ModBegin();
         BarSlider("##v2dflow", &flow, params::view2d::flow_lo, params::view2d::flow_hi, "Flow %.0f%%", kWellL);
         ModEnd();
         b.hardness = hard / 100.0f;
         b.opacity = op / tok::view2d_v2drsize_scale;
         b.flow = flow / 100.0f;
         float smooth = b.smoothing * 100.0f;
         BarSlider("##v2dsmooth", &smooth, params::view2d::smooth_lo, params::view2d::smooth_hi, "Smoothing %.0f%%", kWellL);
         b.smoothing = smooth / 100.0f;
         gap();
         SymmetryChip(doc);
         FormParts::Inline(0.0f, tok::space_1);
         BarMore("##v2dbmore", [&]()
         {
            MoreHeader(T("Tip"), true);
            {
               Pairs g;
               MorePct(T("Spacing"), &b.spacing, params::view2d::spacing_lo, params::view2d::spacing_hi);
               MorePct(T("Roundness"), &b.roundness, params::view2d::roundness_lo, params::view2d::roundness_hi);
               MoreSlider(T("Angle"), &b.angle, params::view2d::angle_lo, params::view2d::angle_hi, "%.0f°");
               MoreCheck(T("Hard edge"), &b.pencil);
            }
            MoreHeader(T("Dynamics"));
            {
               Pairs g;
               float scatterY = b.scatterY < 0.0f ? b.jitter : b.scatterY;
               MorePct(T("Scatter"), &b.jitter, params::view2d::jitter_lo, params::view2d::jitter_hi);
               MorePct(T("Scatter Y"), &scatterY, params::view2d::scatter_y_lo, params::view2d::scatter_y_hi);
               if (std::fabs(scatterY - (b.scatterY < 0.0f ? b.jitter : b.scatterY)) > tok::view2d_pixel_bar_limit)
                  b.scatterY = scatterY;
               MoreSlider(T("Taper in"), &b.taperIn, params::view2d::taper_in_lo, params::view2d::taper_in_hi, "%.0f px");
               MoreSlider(T("Taper out"), &b.taperOut, params::view2d::taper_out_lo, params::view2d::taper_out_hi, "%.0f px");
               float stab = (float)b.stabiliser;
               MoreSlider(T("Stabiliser"), &stab, params::view2d::stab_lo, params::view2d::stab_hi, "%.0f");
               b.stabiliser = (int)std::lround(stab);
               MorePct(T("Accumulation"), &b.accumulation, params::view2d::accumulation_lo, params::view2d::accumulation_hi);
               MoreSlider(T("Airbrush"), &b.airbrushRate, 0.0f, 1.0f, "%.2f");
               MoreCheck(T("Adaptive smooth"), &b.oneEuro);
               MoreCheck(T("Catch up"), &b.catchUp);
               MoreCheck(T("Follow stroke"), &b.follow);
               MoreCheck(T("Wet edges"), &b.wetEdges);
            }
            if (brush && sMoreOpen)
            {
               FieldLabel(T("Hue"));
               ImGui::SetNextItemWidth(sFieldW);
               ModNextParam(kBrushHueParam);
               ModBegin();
               FieldWell::Slider("##v2dhue", &sBrushHue, params::view2d::brush_hue_lo, params::view2d::brush_hue_hi, "%+.0f");
               ModEnd();
            }
            MoreHeader(T("Jitter"), false, false);
            {
               // The header names the set, so each well says only what jitters.
               Pairs g;
               ImGui::PushID("jit");
               MorePct(T("Size"), &b.sizeJitter, params::view2d::size_jitter_lo, params::view2d::size_jitter_hi);
               MorePct(T("Flow"), &b.opacityJitter, params::view2d::opacity_jitter_lo, params::view2d::opacity_jitter_hi);
               MorePct(T("Hardness"), &b.hardJitter, params::view2d::hard_jitter_lo, params::view2d::hard_jitter_hi);
               MorePct(T("Rotation"), &b.angleJitter, params::view2d::angle_jitter_lo, params::view2d::angle_jitter_hi);
               MorePct(T("Shape"), &b.roundJitter, params::view2d::round_jitter_lo, params::view2d::round_jitter_hi);
               MorePct(T("Accumulation"), &b.accumJitter, params::view2d::accum_jitter_lo, params::view2d::accum_jitter_hi);
               if (brush)
               {
                  MorePct(T("Hue"), &b.hueJitter, params::view2d::hue_jitter_lo, params::view2d::hue_jitter_hi);
                  MorePct(T("Saturation"), &b.satJitter, params::view2d::sat_jitter_lo, params::view2d::sat_jitter_hi);
                  MorePct(T("Brightness"), &b.valJitter, params::view2d::val_jitter_lo, params::view2d::val_jitter_hi);
               }
               ImGui::PopID();
            }
            MoreHeader(T("Pen & tilt"));
            {
               Pairs g;
               MoreCheck(T("Size"), &b.pressureSize);
               MoreCheck(T("Opacity"), &b.pressureOpacity);
               MoreSlider(T("Response"), &b.pressureCurve, params::view2d::pressure_curve_lo, params::view2d::pressure_curve_hi, "%.2f");
               MoreCheck(T("Tilt size"), &b.tiltSize);
               MoreCheck(T("Tilt angle"), &b.tiltAngle);
               MoreCheck(T("Tilt flatness"), &b.tiltRound);
            }
            MoreHeader(T("Paint"));
            if (brush)
            {
               // Relief and marbling presets have their own modes.
               static const char* kModeNames[5] = { "Normal", "Behind", "Multiply", "Screen", "Smudge" };
               static const int kModeOf[5] = { BrushEngine::kNormal, BrushEngine::kBehind, BrushEngine::kMultiply, BrushEngine::kScreen, BrushEngine::kSmudge };
               static const char* kReliefNames[4] = { "Raise", "Stack", "Press", "Erase" };
               static const int kReliefOf[4] = { BrushEngine::kNormal, BrushEngine::kStack, BrushEngine::kPress, BrushEngine::kErase };
               static const char* kMarbleNames[3] = { "Drop", "Comb", "Swirl" };
               static const int kMarbleOf[3] = { BrushEngine::kDrop, BrushEngine::kComb, BrushEngine::kSwirl };
               static const char* kLiquifyNames[4] = { "Push", "Twirl", "Bloat", "Pinch" };
               static const int kLiquifyOf[4] = { BrushEngine::kPush, BrushEngine::kTwirl, BrushEngine::kBloat, BrushEngine::kPinch };
               const bool marble = BrushEngine::MarbleMode(b.mode), liquify = BrushEngine::LiquifyMode(b.mode);
               const char* const* names = b.relief >= 0 ? kReliefNames : marble ? kMarbleNames : liquify ? kLiquifyNames : kModeNames;
               const int* of = b.relief >= 0 ? kReliefOf : marble ? kMarbleOf : liquify ? kLiquifyOf : kModeOf;
               const int count = b.relief >= 0 ? 4 : marble ? 3 : liquify ? 4 : 5;
               int m = 0;
               for (int i = 0; i < count; i++)
                  if (of[i] == b.mode)
                     m = i;
               if (MoreCombo(T("Mode"), &m, names, count))
                  b.mode = of[m];
               if (b.mode == BrushEngine::kComb)
               {
                  float tines = (float)b.tines;
                  MoreSlider(T("Tines"), &tines, params::view2d::tines_lo, params::view2d::tines_hi, "%.0f");
                  b.tines = std::clamp((int)std::lround(tines), 1, 16);
               }
               MoreCheck(T("Lock alpha"), &b.lockAlpha);
            }
            if (brush && !BrushEngine::DirectMode(b.mode) && b.relief < 0)
            {
               MaterialRows(b);
               BrushSourceRows(doc);
            }
            MoreHeader(T("Mask"));
            ImGui::BeginDisabled(a == nullptr || !a->HasPixelMask());
            bool mask = sEditMask && a != nullptr && a->HasPixelMask();
            MoreCheck(T("Paint the mask"), &mask);
            if (a != nullptr && a->HasPixelMask())
               sEditMask = mask;
            ImGui::EndDisabled();
         });
      }
      else if (sTool == kMixer || sTool == kSelBrush)
      {
         BrushEngine::Brush& b = ToolBrush();
         BarSlider("##v2dxsize", &b.size, params::view2d::size_lo, params::view2d::size_hi, "Size %.0f px", kWellL);
         float hard = b.hardness * 100.0f;
         BarSlider("##v2dxhard", &hard, params::view2d::hard_lo, params::view2d::hard_hi, "Hardness %.0f%%", kWellL);
         b.hardness = hard / 100.0f;
         if (sTool == kMixer)
         {
            float op = b.opacity * tok::view2d_op_ratio, flow = b.flow * 100.0f, wet = b.wet * 100.0f, refill = b.refill * 100.0f;
            BarSlider("##v2dxop", &op, params::view2d::op_lo, params::view2d::op_hi, "Opacity %.0f%%", kWellL);
            BarSlider("##v2dxflow", &flow, params::view2d::flow_lo, params::view2d::flow_hi, "Flow %.0f%%", kWellL);
            gap();
            BarSlider("##v2dxwet", &wet, params::view2d::wet_lo, params::view2d::wet_hi, "Wet %.0f%%", kWellL);
            BarSlider("##v2dxrefill", &refill, params::view2d::refill_lo, params::view2d::refill_hi, "Refill %.0f%%", kWellL);
            b.opacity = op / tok::view2d_v2drsize_scale;
            b.flow = flow / 100.0f;
            b.wet = wet / 100.0f;
            b.refill = refill / 100.0f;
            next();
            if (TextChip(L("Auto clean##v2dxclean"), b.autoClean, T("Each stroke starts with clean paint")))
               b.autoClean = !b.autoClean;
            gap();
            SymmetryChip(doc);
            FormParts::Inline(0.0f, tok::space_1);
            BarMore("##v2dxmore", [&]()
            {
               MoreHeader(T("Pen pressure"), true);
               {
                  Pairs g;
                  MoreCheck(T("Size"), &b.pressureSize);
                  MoreCheck(T("Opacity"), &b.pressureOpacity);
               }
               MoreHeader(T("Paint"));
               MoreCheck(T("Lock alpha"), &b.lockAlpha);
            });
         }
         else
         {
            gap();
            if (TextChip(L("Add##v2dsbadd"), !sSelBrushSub, T("Paint into the selection")))
               sSelBrushSub = false;
            next();
            if (TextChip(L("Subtract##v2dsbsub"), sSelBrushSub, T("Paint out of the selection (Alt)")))
               sSelBrushSub = true;
            next();
            if (TextChip(L("Soft edges##v2dsbsoft"), sSelBrushSoft, nullptr))
               sSelBrushSoft = !sSelBrushSoft;
            gap();
            SelModChips(doc);
         }
      }
      else if (sTool == kBlemish)
         BarSlider("##v2dblemsize", &sBlemishSize, params::view2d::blemish_size_lo, params::view2d::blemish_size_hi, "Size %.0f px", kWellL);
      else if (sTool == kFloodErase)
         MatchChips();
      else if (sTool == kObjectSel)
      {
         SelOpChips("v2doop");
         gap();
         if (TextChip(L("All layers##v2doall"), sAllLayers, T("Sample the whole picture")))
            sAllLayers = !sAllLayers;
         next();
         if (TextChip(L("Soft edges##v2dosoft"), sObjSoft, nullptr))
            sObjSoft = !sObjSoft;
         gap();
         SelModChips(doc);
      }
      else if (sTool == kPatch)
      {
         if (TextChip(L("Selection is source##v2dpsrc"), sPatchSrc, T("Drag the selection's pixels onto another place")))
            sPatchSrc = !sPatchSrc;
         next();
         if (TextChip(L("Transparent##v2dpclear"), sPatchClear, T("Blend the patch with what is there")))
            sPatchClear = !sPatchClear;
      }
      else if (sTool == kMeshWarp)
      {
         if (TextChip(L("Nearest##v2dmnear"), sMeshNearest, T("Resample nearest neighbour, no smoothing")))
            sMeshNearest = !sMeshNearest;
      }
      else if (sTool == kStylePick)
      {
         // What travels from the held layer: each part on or off.
         static const char* kParts[7] = { "Stroke##v2dsp0", "Fill##v2dsp1", "Opacity##v2dsp2", "Effects##v2dsp3",
                                          "Character##v2dsp4", "Paragraph##v2dsp5", "Object##v2dsp6" };
         for (int i = 0; i < 7; i++)
         {
            if (i > 0)
               next();
            const bool on = (sStyleParts & (1 << i)) != 0;
            if (TextChip(L(kParts[i]), on, nullptr))
               sStyleParts ^= 1 << i;
         }
      }
      else if (sTool == kMeasure)
      {
         if (sMeasureOn)
         {
            const float dx = sMeasureB.x - sMeasureA.x, dy = sMeasureB.y - sMeasureA.y;
            ImGui::AlignTextToFramePadding();
            ImGui::Text(T("%s   %s   W %s  H %s"), UiFmt::Tr(UiFmt::Px(std::hypot(dx, dy))).c, UiFmt::Rad(std::atan2(-dy, dx)).c,
                        UiFmt::F0(std::fabs(dx)).c, UiFmt::F0(std::fabs(dy)).c);
            next();
            if (TextChip(L("Clear##v2dmclear"), false, nullptr))
               sMeasureOn = false;
         }
         else
         {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", T("Drag to measure"));
         }
      }
      else if (sTool == kFill)
         MatchChips();
      else if (sTool == kGradient)
      {
         static const char* kKinds[5] = { "Linear", "Radial", "Angle", "Reflected", "Diamond" };
         int k = std::clamp(sFillKind, 1, 5) - 1;
         if (BarCombo("##v2dgkind", &k, kKinds, 5, kWellL))
            sFillKind = k + 1;
         gap();
         float op = sGradOpacity * tok::view2d_op_ratio;
         BarSlider("##v2dgop", &op, params::view2d::op_lo, params::view2d::op_hi, "Opacity %.0f%%", kWellL);
         sGradOpacity = op / tok::view2d_grad_opacity_ratio;
         next();
         if (TextChip(L("Reverse##v2dgrev"), sGradReverse, T("Background colour to foreground")))
            sGradReverse = !sGradReverse;
         gap();
         static const char* kExtend[3] = { "Pad", "Repeat", "Reflect" };
         BarCombo("##v2dgext", &sGradExtend, kExtend, 3, kWellM);
         next();
         if (TextChip(L("Dither##v2dgdither"), sGradDither, T("No banding")))
            sGradDither = !sGradDither;
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
         BarSlider("##v2dfeather", &sFeather, params::view2d::feather_lo, params::view2d::feather_hi, "Feather %.0f px", kWellL);
         if (m)
         {
            next();
            if (TextChip(L("From centre##v2dmcentre"), sMarqueeCentre, nullptr))
               sMarqueeCentre = !sMarqueeCentre;
            next();
            if (TextChip(L("Antialias##v2dmaa"), sMarqueeAA, nullptr))
               sMarqueeAA = !sMarqueeAA;
            gap();
            static const char* kRatio[5] = { "Free", "1:1", "4:3", "16:9", "Fixed size" };
            BarCombo("##v2dmratio", &sMarqueeRatio, kRatio, 5, kWellM);
            if (sMarqueeRatio == 4)
            {
               next();
               BarSlider("##v2dmfw", &sMarqueeFixed[0], params::view2d::marquee_fixed_0_lo, params::view2d::marquee_fixed_0_hi, "W %.0f px", kWellM);
               next();
               BarSlider("##v2dmfh", &sMarqueeFixed[1], params::view2d::marquee_fixed_1_lo, params::view2d::marquee_fixed_1_hi, "H %.0f px", kWellM);
            }
         }
         SelModChips(doc);
      }
      else if (sTool == kPick)
      {
         static const char* kSample[3] = { "Point", "3x3 average", "5x5 average" };
         int si = sPickSize / tok::view2d_si_ratio;
         if (BarCombo("##v2dpsize", &si, kSample, 3, kWellL))   // ui-audit-ok: model value (prediction lookahead samples / handle index modulo / visible items)
            sPickSize = si * tok::view2d_pick_size_ratio + 1;
         gap();
         static const char* kFrom[2] = { "All layers", "Current layer" };
         int from = sPickLayer ? 1 : 0;
         if (BarCombo("##v2dpfrom", &from, kFrom, 2, kWellL))
            sPickLayer = from == 1;
      }
      else if (sTool == kWand)
      {
         SelOpChips("v2dwop");
         gap();
         MatchChips();
         FormParts::Inline(0.0f, tok::space_1);
         if (TextChip(L("Antialias##v2dwaa"), doc->wandAntialias, nullptr))
            doc->wandAntialias = !doc->wandAntialias;
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
      if (FormParts::ColorDot("##v2dvfillc", t.fillC, std::min(tok::colour_dot, sRowH - tok::space_1), sRowH))
      {
         // One undo step per colour edit, as ColorRow: the first change in a second pushes it.
         static double sAt = -1.0;
         if (ImGui::GetTime() - sAt > tok::motion_s_undo_merge)
            undo();
         sAt = ImGui::GetTime();
         changed = true;
      }
      next();
      // Gradient fills live in one popup, beside the stroke's: type, angle and the stops.
      if (IconChip("##v2dvfillgrad", IconsInfinite::Gradient, t.fillType > 0, T("Fill gradient")))
         ImGui::OpenPopup("##v2dvfillpop");
      if (MenuParts::BeginPopup("##v2dvfillpop"))
      {
         ImGui::TextDisabled("%s", T("Fill"));
         static const char* kFillKinds[7] = { "Solid", "Linear", "Radial", "Reflected", "Conical", "Bitmap", "Mesh" };
         for (int i = 0; i < 7; i++)
         {
            if (i > 0 && i != 4)
               FormParts::Inline(0.0f, tok::space_1);
            const std::string cid = std::string(T(kFillKinds[i])) + "##v2dvft" + std::to_string(i);
            if (TextChip(cid.c_str(), t.fillType == i, nullptr) && t.fillType != i)
            {
               undo();
               t.fillType = i;
               changed = true;
            }
         }
         if (t.fillType >= 1 && t.fillType <= 4)
         {
            if (t.fillType != 2)
               changed |= SliderRow(T("Angle##v2dvfa"), &t.fillAngle, params::view2d::fill_angle_lo, params::view2d::fill_angle_hi, "%.0f°");
            const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            if (GradientRows("##v2dvfillstops", t.fillStops, t.fillC, white))
               changed = true;
         }
         else if (t.fillType == 5)
         {
            // Bitmap: an image file, placed in the shape's box. Opacity is the fill colour's alpha.
            const std::string shown = t.fillImage.empty() ? std::string(T("Choose image")) : t.fillImage.substr(t.fillImage.find_last_of("/\\") + 1);
            if (ImGui::Button((shown + "##v2dvfimg").c_str()))
            {
               const std::string path = Platform::OpenImageDialog();
               if (!path.empty())
               {
                  undo();
                  t.fillImage = path;
                  changed = true;
               }
            }
            static const char* kModes[4] = { "Stretch", "Fill", "Fit", "Tile" };
            for (int i = 0; i < 4; i++)
            {
               if (i > 0)
                  FormParts::Inline(0.0f, tok::space_1);
               const std::string cid = std::string(T(kModes[i])) + "##v2dvfim" + std::to_string(i);
               if (TextChip(cid.c_str(), t.imageMode == i, nullptr) && t.imageMode != i)
               {
                  undo();
                  t.imageMode = i;
                  changed = true;
               }
            }
         }
         else if (t.fillType == 6)
         {
            // Mesh: one colour per corner, blended across the box.
            static const char* kCorner[4] = { "Top left", "Top right", "Bottom right", "Bottom left" };
            for (int i = 0; i < 4; i++)
               changed |= ColorRow((std::string(T(kCorner[i])) + "##v2dvmesh" + std::to_string(i)).c_str(), t.meshC[i]);
         }
         if (t.fillType > 0)
         {
            changed |= SliderRow(T("Noise##v2dvfnoise"), &t.noise, 0.0f, 1.0f, "%.2f");
         }
         MenuParts::EndPopup();
      }
      gap();
      if (TextChip(L("Stroke##v2dvstroke"), t.strokeOn, T("Stroke on or off")))
      {
         undo();
         t.strokeOn = !t.strokeOn;
         changed = true;
      }
      next();
      if (FormParts::ColorDot("##v2dvstrokec", t.strokeC, std::min(tok::colour_dot, sRowH - tok::space_1), sRowH))
      {
         // One undo step per colour edit, as ColorRow: the first change in a second pushes it.
         static double sAt = -1.0;
         if (ImGui::GetTime() - sAt > tok::motion_s_undo_merge)
            undo();
         sAt = ImGui::GetTime();
         changed = true;
      }
      next();
      ImGui::SetNextItemWidth(kWellS);
      changed |= FieldWell::Slider("##v2dvwidth", &t.width, params::view2d::width_lo, params::view2d::width_hi, "%.0f px");
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
         changed |= FieldWell::Slider("##v2dvdash", &t.dash, params::view2d::dash_lo, params::view2d::dash_hi, t.dash > 0.0f ? "Dash %.0f" : "Solid");
         if (ImGui::IsItemActivated())
            undo();
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::BeginDisabled(t.dash <= 0.0f);
         ImGui::SetNextItemWidth(kWellL);
         changed |= FieldWell::Slider("##v2dvgap", &t.gap, params::view2d::gap_lo, params::view2d::gap_hi, "Gap %.0f");
         if (ImGui::IsItemActivated())
            undo();
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::SetNextItemWidth(kWellL);
         changed |= FieldWell::Slider("##v2dvphase", &t.dashPhase, params::view2d::dash_phase_lo, params::view2d::dash_phase_hi, "Phase %.0f");
         if (ImGui::IsItemActivated())
            undo();
         FormParts::Inline(0.0f, tok::space_1);
         if (TextChip(L("Balanced##v2dvbal"), t.balanced, T("Even dashes round a closed shape")))
         {
            undo();
            t.balanced = !t.balanced;
            changed = true;
         }
         ImGui::EndDisabled();
         if (t.join == 0)
         {
            ImGui::SetNextItemWidth(kWellL);
            changed |= FieldWell::Slider("##v2dvmiter", &t.miter, params::view2d::miter_lo, params::view2d::miter_hi, "Miter %.1f");
            if (ImGui::IsItemActivated())
               undo();
         }
         pick("Start", &t.arrowS, { "None", "Arrow", "Triangle", "Circle", "Square", "Bar" });
         pick("End", &t.arrowE, { "None", "Arrow", "Triangle", "Circle", "Square", "Bar" });
         if (t.arrowS > 0 || t.arrowE > 0)
         {
            ImGui::SetNextItemWidth(kWellL);
            changed |= FieldWell::Slider("##v2dvarrow", &t.arrowSize, params::view2d::arrow_size_lo, params::view2d::arrow_size_hi, "Head %.1f\xC3\x97");
            if (ImGui::IsItemActivated())
               undo();
         }
         ImGui::TextDisabled("%s", T("Pressure"));
         ImGui::SetNextItemWidth(kWellL);
         changed |= FieldWell::Slider("##v2dvtaps", &t.taperS, 0.0f, 1.0f, "In %.2f");
         if (ImGui::IsItemActivated())
            undo();
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::SetNextItemWidth(kWellL);
         changed |= FieldWell::Slider("##v2dvtape", &t.taperE, 0.0f, 1.0f, "Out %.2f");
         if (ImGui::IsItemActivated())
            undo();
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::BeginDisabled(t.taperS <= 0.0f && t.taperE <= 0.0f);
         ImGui::SetNextItemWidth(kWellL);
         changed |= FieldWell::Slider("##v2dvpmin", &t.pressMin, 0.0f, 1.0f, "Min %.2f");
         if (ImGui::IsItemActivated())
            undo();
         ImGui::EndDisabled();
         if (TextChip(L("Behind##v2dvbehind"), t.behind, T("Stroke under the fill")))
         {
            undo();
            t.behind = !t.behind;
            changed = true;
         }
         FormParts::Inline(0.0f, tok::space_1);
         if (TextChip(L("Scale##v2dvscst"), t.scaleStroke, T("Stroke width scales with the layer")))
         {
            undo();
            t.scaleStroke = !t.scaleStroke;
            changed = true;
         }
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
      if ((rect || poly) && (picked != nullptr || sTool == kShape))
      {
         gap();
         ImGui::SetNextItemWidth(kWellL);
         // Absolute: the radius in px; off, in % of the shape's short side (kept in px either way).
         const float side = std::max(1.0f, std::min(std::fabs(t.x1 - t.x0), std::fabs(t.y1 - t.y0)));
         if (sRadiusAbs || picked == nullptr)
            changed |= FieldWell::Slider("##v2dvrad", &t.radius, params::view2d::radius_lo, params::view2d::radius_hi, "Radius %.0f");
         else
         {
            float pct = t.radius / side * 100.0f;
            if (FieldWell::Slider("##v2dvradp", &pct, params::view2d::pct_2_lo, params::view2d::pct_2_hi, "Radius %.0f%%"))
            {
               t.radius = pct * side / tok::view2d_radius_ratio;
               changed = true;
            }
         }
         if (ImGui::IsItemActivated())
            undo();
         next();
         if (TextChip(L("Absolute##v2dvrabs"), sRadiusAbs, T("Radius in px, not a percentage of the shape")))
            sRadiusAbs = !sRadiusAbs;
         next();
         // Each corner on its own (rectangles): radius (blank follows Radius) and type.
         if (rect && IconChip("##v2dvcorners", IconsInfinite::StrokeStyle, false, T("Corners")))
            ImGui::OpenPopup("##v2dvcornpop");
         if (MenuParts::BeginPopup("##v2dvcornpop"))
         {
            static const char* kCorner[4] = { "Top left", "Top right", "Bottom right", "Bottom left" };
            static const char* kTypes[4] = { "Round", "Straight", "Concave", "Inset" };
            for (int c = 0; c < 4; c++)
            {
               ImGui::TextDisabled("%s", T(kCorner[c]));
               float r = t.CornerR(c);
               ImGui::SetNextItemWidth(kWellL);
               if (FieldWell::Slider(("##v2dvcr" + std::to_string(c)).c_str(), &r, params::view2d::r_lo, params::view2d::r_hi, "%.0f"))
               {
                  t.cornerR[c] = r;
                  changed = true;
               }
               if (ImGui::IsItemActivated())
                  undo();
               for (int k = 0; k < 4; k++)
               {
                  FormParts::Inline(0.0f, tok::space_1);
                  const std::string cid = std::string(T(kTypes[k])) + "##v2dvct" + std::to_string(c * 4 + k);
                  if (TextChip(cid.c_str(), t.cornerType[c] == k, nullptr) && t.cornerType[c] != k)
                  {
                     undo();
                     t.cornerType[c] = k;
                     changed = true;
                  }
               }
            }
            if (ImGui::Button(T("Same for all##v2dvcrall")))
            {
               undo();
               for (int c = 0; c < 4; c++)
               {
                  t.cornerR[c] = -1.0f;
                  t.cornerType[c] = t.cornerType[0];
               }
               changed = true;
            }
            MenuParts::EndPopup();
         }
      }
      if (poly && (picked != nullptr || sTool == kShape))
      {
         gap();
         float sides = (float)t.sides;
         ImGui::SetNextItemWidth(kWellL);
         if (FieldWell::Slider("##v2dvsides", &sides, params::view2d::sides_lo, params::view2d::sides_hi, star ? "Points %.0f" : "Sides %.0f"))
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
            changed |= FieldWell::Slider("##v2dvinner", &t.inner, params::view2d::inner_lo, params::view2d::inner_hi, "Inner %.2f");
            if (ImGui::IsItemActivated())
               undo();
         }
      }
      // A picked live shape's box size (Photoshop's W / H): it grows from its top-left corner.
      if (picked != nullptr && picked->type != Shape::kPath && picked->type != Shape::kLine)
      {
         gap();
         float w = std::fabs(t.x1 - t.x0), h = std::fabs(t.y1 - t.y0);
         ImGui::SetNextItemWidth(kWellM);
         if (FieldWell::Slider("##v2dvw", &w, params::view2d::w_lo, params::view2d::w_hi, "W %.0f"))
         {
            const float lo = std::min(t.x0, t.x1);
            t.x0 = lo;
            t.x1 = lo + std::max(1.0f, w);
            changed = true;
         }
         if (ImGui::IsItemActivated())
            undo();
         next();
         ImGui::SetNextItemWidth(kWellM);
         if (FieldWell::Slider("##v2dvh", &h, params::view2d::h_lo, params::view2d::h_hi, "H %.0f"))
         {
            const float lo = std::min(t.y0, t.y1);
            t.y0 = lo;
            t.y1 = lo + std::max(1.0f, h);
            changed = true;
         }
         if (ImGui::IsItemActivated())
            undo();
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
      const float probe = tok::view2d_probe_ratio * scale;   // render big, then scale down to the row
      const int cw = (int)std::ceil(probe * 16.0f), ch = (int)std::ceil(probe * 2.0f);
      auto raster = [&](const std::string& text, std::vector<unsigned char>& buf)
      {
         buf.assign((size_t)cw * ch * 4, 0);
         Platform::TextRasterRequest req;
         req.text = text;
         req.fontName = fam;
         req.fontSize = probe;
         req.align = 1;
         req.posX = tok::view2d_pos_x;
         req.posY = tok::view2d_pos_y;
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
      float capPx = probe * tok::view2d_cap_px_ratio, baseRow = (float)ny1;
      if (InkBox(cap, cw, ch, hx0, hy0, hx1, hy1) && hy1 > hy0)
      {
         capPx = (float)(hy1 - hy0 + 1);
         baseRow = (float)hy1 + 1.0f;
      }
      // Target: the UI font's cap height, in screen points.
      const float k = std::clamp(ImGui::GetFontSize() * tok::type_cap_em / capPx, 0.0f, 4.0f);   // probe px -> points
      const int w = nx1 - nx0 + 1, h = ny1 - ny0 + 1;
      std::vector<unsigned char> out((size_t)w * h * 4);  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
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
         dl->PushClipRect(ImVec2(a.x, a.y - tok::space_2), ImVec2(b.x, b.y + tok::space_2), true);
         Thumb::Add(dl, (ImTextureID)(intptr_t)f->tex, ImVec2(a.x, top), ImVec2(a.x + w, top + f->h), ImVec2(0, 0), ImVec2(w / f->w, 1), col);
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
      if (sRecentFonts.size() > tok::view2d_note_recent_font_limit)
         sRecentFonts.resize(5);
   }

   void DimHeader(const char* text)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(t.x, t.y, t.z, tok::view2d_dim_header_y));
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
      ImGui::SetNextWindowSizeConstraints(ImVec2(tok::view2d_window_w_lg, 0.0f), ImVec2(tok::view2d_window_w_lg, MenuParts::ListMaxH(MenuParts::kMaxListRows + tok::view2d_font_picker_gap)));
      if (MenuParts::BeginCombo(id, nameOf(openHere ? sFontOrig : font).c_str(), ImGuiComboFlags_HeightLargest))
      {
         // The bar's tile padding stops here: the search field and rows are the side panel's size.
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, std::max(tok::view2d_font_picker_min, (SideChip() - ImGui::GetFontSize()) * 0.5f)));
         if (!openHere)
         {
            sOpenId = id;
            sFontOrig = font;
            sFontFind[0] = 0;
            UiInteract::FocusHere();
         }
         ImGui::SetNextItemWidth(-1.0f);
         FieldWell::InputText("##fontfind", sFontFind, sizeof(sFontFind));
         if (sFontFind[0] == 0)
         {
            const ImVec2 fa = ImGui::GetItemRectMin();
            const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::GetWindowDrawList()->AddText(ImVec2(fa.x + ImGui::GetStyle().FramePadding.x, fa.y + ImGui::GetStyle().FramePadding.y),
                                                ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, tok::view2d_font_picker_y)), T("Search"));
         }
         Ui::Spacer(Ui::Space::S0_5);
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
         const int extra = want.empty() ? 1 + (sRecentFonts.empty() ? 0 : (int)sRecentFonts.size() + tok::view2d_extra_inset) : 0;
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
            ImGui::SetScrollY(std::max(0.0f, curRow * rowH - listH * tok::view2d_font_picker_ratio));
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
         EndChildSafe();
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
   bool JustifyChip(const char* id, bool on, float w = 0.0f) { return IconChip(id, IconsInfinite::TextJustify, on, T("Justify text"), w); }

   // Bold, Italic, Underline, All caps: text chips, as every editor's character row.
   template <class Undo, class Next>
   bool TextStyleChips(bool* bold, bool* italic, bool* under, bool* caps, Undo&& undo, Next&& next, float chipW = 0.0f)
   {
      bool* const v[4] = { bold, italic, under, caps };
      static const char* const kLabels[4] = { "B##tsb", "I##tsi", "U##tsu", "TT##tsc" };
      static const char* const kTips[4] = { "Bold", "Italic", "Underline", "All caps" };
      bool changed = false;
      for (int i = 0; i < 4; i++)
      {
         if (i > 0)
            next();
         // Cells fill the field column exactly, rounding spread so no chip is more than 1 px off another.
         const float w = chipW > 0.0f ? PanelKit::StripW(sFieldW, tok::view2d_strip_w_n, i) : 0.0f;
         if (TextChip(kLabels[i], *v[i], T(kTips[i]), w))
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
      float autoSize = std::clamp(std::round(doc->Doc().h / tok::view2d_text_bar_ratio), tok::view2d_auto_size_lo, tok::view2d_auto_size_hi);
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
      changed |= FieldWell::Slider("##v2dfsize", &size, params::view2d::size_2_lo, params::view2d::size_2_hi, "%.0f px");
      if (ImGui::IsItemActivated())
         undo();
      next();
      ImGui::SetNextItemWidth(kWellL);
      changed |= FieldWell::Slider("##v2dlead", &leading, params::view2d::leading_lo, params::view2d::leading_hi, "Leading %.2f");
      if (ImGui::IsItemActivated())
         undo();
      next();
      ImGui::SetNextItemWidth(kWellL);
      changed |= FieldWell::Slider("##v2dtrack", &tracking, params::view2d::tracking_lo, params::view2d::tracking_hi, "Tracking %.0f");
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
      if (FormParts::ColorDot("##v2dtextc", colour, std::min(tok::colour_dot, sRowH - tok::space_1), sRowH))
      {
         // One undo step per colour edit, as ColorRow: the first change in a second pushes it.
         static double sAt = -1.0;
         if (ImGui::GetTime() - sAt > tok::motion_s_undo_merge)
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
      { kPerspective, "##v2dpersp", IconsInfinite::Perspective, "Perspective. Drag a corner", 0 },
      { kMeshWarp, "##v2dmesh", IconsInfinite::MeshWarp, "Mesh warp. Drag a grid point", 0 },
      { kMarquee, "##v2dmarquee", IconsInfinite::Marquee, "Marquee (M)", 1 },
      { kLasso, "##v2dlasso", IconsInfinite::Lasso, "Lasso (L)", 1 },
      { kWand, "##v2dwand", IconsInfinite::Wand, "Magic wand (W)", 1 },
      { kSelBrush, "##v2dselbrush", IconsInfinite::SelBrush, "Selection brush (W). Alt takes away", 1 },
      { kObjectSel, "##v2dobjsel", IconsInfinite::ObjectSel, "Object selection (W). Drag a box round it", 1 },
      { kCrop, "##v2dcrop", IconsInfinite::Crop, "Crop (C). Enter applies, Esc cancels", 1 },
      { kPick, "##v2dpick", IconsInfinite::Eyedropper, "Eyedropper (I). Alt sets the background", 1 },
      { kStylePick, "##v2dstylepick", IconsInfinite::StylePick, "Style picker (I). Click to take, click to give", 1 },
      { kMeasure, "##v2dmeasure", IconsInfinite::Measure, "Measure (I). Drag a line", 1 },
      { kHeal, "##v2dheal", IconsInfinite::Heal, "Healing brush (J). Alt-click sets the source", 2 },
      { kBlemish, "##v2dblemish", IconsInfinite::Blemish, "Blemish removal (J). Click a spot", 2 },
      { kPatch, "##v2dpatch", IconsInfinite::Patch, "Patch (J). Drag the selection to clean pixels", 2 },
      { kInpaint, "##v2dinpaint", IconsInfinite::Inpaint, "Inpainting brush (J). Paint over to remove", 2 },
      { kRedEye, "##v2dredeye", IconsInfinite::RedEye, "Red eye (J)", 2 },
      { kBrush, "##v2dbrush", IconsInfinite::Brush, "Brush (B). Alt picks a colour, Shift-click draws a line", 2 },
      { kMixer, "##v2dmixer", IconsInfinite::PaintMixer, "Paint mixer (B). Wet paint", 2 },
      { kColourReplace, "##v2dcolrep", IconsInfinite::ColourReplace, "Colour replacement (B)", 2 },
      { kClone, "##v2dclone", IconsInfinite::CloneStamp, "Clone stamp (S). Alt-click sets the source", 2 },
      { kUndoBrush, "##v2dundobrush", IconsInfinite::UndoBrush, "Undo brush (S). Alt-click sets the state", 2 },
      { kEraser, "##v2derase", IconsInfinite::Erase, "Eraser (E)", 2 },
      { kBgErase, "##v2dbgerase", IconsInfinite::BgErase, "Background eraser (E)", 2 },
      { kFloodErase, "##v2dflooderase", IconsInfinite::FloodErase, "Flood eraser (E)", 2 },
      { kGradient, "##v2dgradient", IconsInfinite::Gradient, "Gradient (G). Drag to draw", 2 },
      { kFill, "##v2dfilltool", IconsInfinite::Fill, "Paint bucket (G)", 2 },
      { kBlur, "##v2dblur", IconsInfinite::Blur, "Blur (R)", 2 },
      { kSharpen, "##v2dsharpen", IconsInfinite::Sharpen, "Sharpen (R)", 2 },
      { kMedian, "##v2dmedian", IconsInfinite::Median, "Median (R)", 2 },
      { kDodge, "##v2ddodge", IconsInfinite::Dodge, "Dodge (O). Lightens", 2 },
      { kBurn, "##v2dburn", IconsInfinite::Burn, "Burn (O). Darkens", 2 },
      { kSponge, "##v2dsponge", IconsInfinite::Sponge, "Sponge (O)", 2 },
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

   // The tools a first-time user needs; the rest wait in the tool picker (the grid button under the strip).
   bool Essential(int tool)
   {
      switch (tool)
      {
         case kMove: case kMarquee: case kLasso: case kWand: case kCrop: case kPick: case kHeal: case kBrush: case kClone:
         case kEraser: case kGradient: case kFill: case kPen: case kText: case kShape: case kHand: case kZoom:
            return true;
         default:
            return false;
      }
   }

   // The picker's groups, in the order they are listed.
   struct ToolGroup { const char* name; std::vector<int> tools; };
   const std::vector<std::vector<ToolGroup>>& PickerColumns()
   {
      static const std::vector<std::vector<ToolGroup>> cols = {
         { { "Move", { kMove, kPerspective, kMeshWarp } },
           { "Select", { kMarquee, kLasso, kWand, kSelBrush, kObjectSel } },
           { "Crop and sample", { kCrop, kPick, kStylePick, kMeasure } } },
         { { "Retouch", { kHeal, kBlemish, kPatch, kInpaint, kRedEye } },
           { "Paint", { kBrush, kMixer, kColourReplace, kClone, kUndoBrush } },
           { "Erase", { kEraser, kBgErase, kFloodErase } } },
         { { "Fill and tune", { kGradient, kFill, kBlur, kSharpen, kMedian, kDodge, kBurn, kSponge } },
           { "Draw and type", { kPen, kText, kNode, kShape } },
           { "View", { kHand, kZoom } } },
      };
      return cols;
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
         {
            sStripOrder.insert(sStripOrder.begin() + std::min((size_t)i, sStripOrder.size()), i);
            sStripHidden[i] = !Essential(kTools[i].tool);   // only the essentials show until the user picks more
         }
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
      const float step = PanelKit::Chip() + tok::space_1;
      const float oneCol = n * step + tok::view2d_one_col_ratio * (tok::space_1 + 1.0f) + tok::space_1 + tok::view2d_one_col_scale * step;   // + sections + chips rows
      if (oneCol <= h)
         return 1;
      for (int cols = 2; cols < 6; cols++)
         if (((n + cols - 1) / cols + (cols >= 3 ? 1 : tok::view2d_strip_columns)) * step + tok::space_1 <= h)
            return cols;
      return 6;
   }

   float StripWidth(int cols) { return cols * PanelKit::Chip() + (cols - 1) * tok::space_1; }

   // Foreground over background in one tile, Photoshop's corner layout. Click either square for its picker;
   // the small corner arrow swaps (X), the tiny pair resets (D).
   void FgBgTile()
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float t = PanelKit::Chip(), sq = std::round(t * 0.6f);
      UiInteract::Hit("##v2dcolours", ImVec2(t, t));
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
      ColourChip::Swatch(dl, ImVec2(p.x + t - sq, p.y + t - sq), sq, sBg);
      ColourChip::Swatch(dl, p, sq, sFg);
      const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
      // swap: a bent arrow in the top-right corner
      const ImVec2 s0(p.x + sq + tok::view2d_fg_bg_tile_gap, p.y + tok::view2d_fg_bg_tile_gap);
      dl->AddBezierQuadratic(ImVec2(s0.x, s0.y + tok::px_align), ImVec2(s0.x + tok::space_1_5, s0.y + tok::px_align), ImVec2(s0.x + tok::space_1_5, s0.y + tok::view2d_curve_y), ink, tok::view2d_curve_stroke);
      // reset: a tiny black-over-white pair in the bottom-left corner
      const ImVec2 r0(p.x + tok::view2d_r0_inset, p.y + sq + tok::space_0_75);
      dl->AddRectFilled(ImVec2(r0.x + tok::view2d_fg_bg_tile_gap, r0.y + tok::view2d_fg_bg_tile_gap), ImVec2(r0.x + tok::view2d_fill_x_lg, r0.y + tok::view2d_fill_x_lg), tok::U32(tok::canvas_ink));
      dl->AddRectFilled(r0, ImVec2(r0.x + tok::view2d_fill_x_sm, r0.y + tok::view2d_fill_x_sm), tok::U32(tok::canvas_shade));
      dl->AddRect(r0, ImVec2(r0.x + tok::view2d_fill_x_sm, r0.y + tok::view2d_fill_x_sm), edge);
      for (int i = 0; i < 2; i++)
         if (MenuParts::BeginPopup(i == 0 ? "##v2dfgpick" : "##v2dbgpick"))
         {
            float* col = i == 0 ? sFg : sBg;
            ImGui::ColorPicker3("##pick", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
            if (!sRecentCols.empty())
            {
               ImGui::TextDisabled("%s", T("Recent"));
               const int r = SwatchStrip("##recent", sRecentCols, false, nullptr);
               if (r >= 0)
               {
                  std::copy(sRecentCols[r].c, sRecentCols[r].c + 3, col);
                  ImGui::CloseCurrentPopup();   // a pick is the whole job, as a swatch click is in Affinity
               }
            }
            ImGui::TextDisabled("%s", T("Swatches"));
            std::vector<Rgb> sw = Swatches();
            int removed = -1;
            const int k = SwatchStrip("##swatches", sw, true, &removed);
            if (k == (int)sw.size())
            {
               if (std::none_of(sw.begin(), sw.end(), [&](const Rgb& r) { return SameColour(r.c, col); }))
                  sw.push_back(Rgb{ { col[0], col[1], col[2] } });
               SetSwatches(sw);
            }
            else if (k >= 0)
            {
               std::copy(sw[k].c, sw[k].c + 3, col);
               ImGui::CloseCurrentPopup();
            }
            if (removed >= 0)
            {
               sw.erase(sw.begin() + removed);
               SetSwatches(sw);
            }
            MenuParts::EndPopup();
         }
   }

   // The window behind the grid button: every tool in its group with a tick, the strip shows the ticked ones.
   void DrawToolPicker()
   {
      if (!MenuParts::BeginPopup("##v2dtoolpick"))
         return;
      ParseStrip();
      // Header: the title, then the two set-alls as chips on the right.
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s", T("Tools on the strip"));
      {
         const char* a = T("Essentials");
         const char* b = T("All");
         const float pad = tok::view2d_essentials_scale * tok::space_2;
         const float wa = ImGui::CalcTextSize(a).x + pad, wb = ImGui::CalcTextSize(b).x + pad;
         Ui::SameRow();
         const float x = ImGui::GetWindowContentRegionMax().x - wa - wb - tok::space_1;
         Ui::ColumnAt(x);
         if (TextChip(L("Essentials##v2dpickess"), false, T("Show essentials only"), wa))
         {
            gView2DTools.clear();
            ParseStrip();
            sStripParsed = "\x01";
            SaveWorkspaceSettings();
         }
         FormParts::Inline(0.0f, tok::space_1);
         if (TextChip(L("All##v2dpickall"), false, T("Show all tools"), wb))
         {
            for (int i = 0; i < kToolCount; i++)
               sStripHidden[i] = false;
            SaveStrip();
         }
      }
      ImGui::Spacing();
      if (ListRow::BeginTable("##v2dpicktbl", tok::view2d_tool_picker_begin_table_layout, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
      {
         ImGui::TableNextRow();
         for (const std::vector<ToolGroup>& col : PickerColumns())
         {
            ImGui::TableNextColumn();
            for (const ToolGroup& g : col)
            {
               ImGui::TextDisabled("%s", T(g.name));
               for (int tool : g.tools)
               {
                  int idx = -1;
                  for (int i = 0; i < kToolCount; i++)
                     if (kTools[i].tool == tool)
                        idx = i;
                  if (idx < 0)
                     continue;
                  const bool on = !sStripHidden[idx];
                  ImGui::PushID(idx);
                  // The whole row is the toggle: the icon and name dim when the tool is off the strip.
                  const char* name = T(ToolTitle(kTools[idx].tip));
                  const float rh = ImGui::GetFrameHeight();
                  const float rw = std::max(Ui::AvailW(), tok::view2d_tool_picker_gap + tok::space_2 + ImGui::CalcTextSize(name).x + tok::space_2);
                  const ImVec2 rp = ImGui::GetCursorScreenPos();
                  if (UiInteract::Hit("##row", ImVec2(rw, rh)))
                  {
                     sStripHidden[idx] = on;
                     SaveStrip();
                  }
                  UiAudit::NoteKind("toggle", name);
                  if (on)
                     UiAudit::NoteOn();
                  ImDrawList* pdl = ImGui::GetWindowDrawList();
                  if (ImGui::IsItemHovered())
                     pdl->AddRectFilled(rp, ImVec2(rp.x + rw, rp.y + rh), ImGui::GetColorU32(ImGuiCol_HeaderHovered), tok::radius_control);
                  const ImU32 ink = ImGui::GetColorU32(on ? ImGuiCol_Text : ImGuiCol_TextDisabled, on ? 1.0f : tok::view2d_ink_color_alpha_mul_sm);
                  glyph::Draw(pdl, ImVec2(rp.x + tok::space_1 + tok::space_2, rp.y + rh * 0.5f), tok::icon_control, ink, kTools[idx].glyph);
                  const ImVec2 tp(rp.x + tok::space_1 + tok::view2d_tool_picker_gap + tok::space_2, rp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f);
                  pdl->AddText(tp, ink, name);
                  UiAudit::NoteInk(0, tp, ImVec2(tp.x + ImGui::CalcTextSize(name).x, tp.y + ImGui::GetTextLineHeight()));
                  ImGui::PopID();
               }
               Ui::Spacer(Ui::Space::S1);
            }
         }
         ImGui::EndTable();
      }
      MenuParts::EndPopup();
   }

   void DrawToolStrip(float h, Scene2DNode* doc)
   {
      const int cols = StripColumns(h);
      const float w = StripWidth(cols);
      ImGui::BeginChild("##v2dtools", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(tok::space_1, tok::space_1));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ParseStrip();
      int shown = 0, lastSection = -1;
      bool openPicker = false;
      // The tools on show, in strip order; a drag reorders among them.
      std::vector<int> vis;
      for (int k = 0; k < (int)sStripOrder.size(); k++)
         if (!sStripHidden[sStripOrder[k]] || kTools[sStripOrder[k]].tool == sTool)
            vis.push_back(k);
      // One column is a list (the others slide aside); two or more is an icon grid (a drop swaps two icons).
      for (int v : ReorderDrag::Order("V2DTOOL", (int)vis.size(), cols == 1, cols > 1))
      {
         const int i = sStripOrder[vis[v]];
         if (cols == 1 && shown > 0 && kTools[i].section != lastSection)
         {
            // A hairline between sections, centred in its gap.
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float y = std::round(p.y + 0.5f * (tok::space_1 + 1.0f) - tok::space_1 * 0.5f);
            dl->AddLine(ImVec2(p.x + tok::space_1, y), ImVec2(p.x + w - tok::space_1, y), ImGui::GetColorU32(ImGuiCol_Separator), tok::view2d_tool_strip_stroke);
            Ui::Claim(ImVec2(w, tok::view2d_spacer_h_sm));
         }
         else if (cols > 1 && shown % cols != 0)
            FormParts::Inline(0.0f, tok::space_1);
         lastSection = kTools[i].section;
         shown++;
         ReorderDrag::Flow(v);
         if (IconChip(kTools[i].id, kTools[i].glyph, sTool == kTools[i].tool, Sc(T(kTools[i].tip))))
            sTool = kTools[i].tool;
         ReorderDrag::Item(v, nullptr);
      }
      if (const ReorderDrag::Move m = ReorderDrag::End())
      {
         const int from = vis[m.from], to = vis[m.to];
         if (m.swap)
            std::swap(sStripOrder[from], sStripOrder[to]);
         else
         {
            const int moved = sStripOrder[from];
            sStripOrder.erase(sStripOrder.begin() + from);
            sStripOrder.insert(sStripOrder.begin() + to, moved);
         }
         SaveStrip();
      }
      if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && MenuParts::RightPressed())
         openPicker = true;
      Ui::Claim(ImVec2(w, 0.0f));
      FgBgTile();
      if (cols == 1)
         Ui::Claim(ImVec2(w, 0.0f));
      else
         FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dqm", IconsInfinite::QuickMask, doc->quickMask, T(doc->quickMask ? "Quick mask on (Q)" : "Quick mask (Q)")))
         ToggleQuickMask(doc);
      if (cols >= 3)
         FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dmoretools", IconsInfinite::GridDots, false, T("More tools. Tick the ones to show")))
         openPicker = true;
      if (openPicker)
         ImGui::OpenPopup("##v2dtoolpick");
      DrawToolPicker();
      ImGui::PopStyleVar();
      EndChildSafe();
   }

   // Text centred on the chip row, so labels share the chips' centre line.
   [[maybe_unused]] void RowLabel(const char* text)
   {
      Ui::CentreInRow centred(sRowH, ImGui::GetTextLineHeight());
      ImGui::TextDisabled("%s", text);
   }

   // The context bar: what the current tool can do (plan §5). Laid out on one tile row from the cursor.
   void DrawContextBarItems(Scene2DNode* doc);

   const char* const kFreeModeNames[kFreeModes] = { "Free", "Scale", "Rotate", "Skew", "Distort", "Perspective" };

   bool FreeTransformOne(Scene2DNode* doc) { const Layer* a = Active(doc); return a != nullptr && sSel.size() == 1 && Flippable(*a) && !a->is3d; }

   // Transform as one submenu on both right-click menus (Photoshop's Edit > Transform): turn or flip without
   // starting Free Transform first. Groups turn and flip with everything in them.
   void TransformSubMenu(Scene2DNode* doc)
   {
      const Layer* a = Active(doc);
      const bool can = a != nullptr && !sSel.empty() && (Flippable(*a) || a->kind == Kind::Group);
      if (!MenuParts::SubMenu(T("Transform"), can))
         return;
      if (MenuParts::Item(T("Free Transform"), Sc("Ctrl+T"))) BeginFreeTransform(doc);
      MenuParts::Separator();
      if (MenuParts::Item(T("Rotate 90° Clockwise"))) TurnSelection(doc, 90.0f);
      if (MenuParts::Item(T("Rotate 90° Counter Clockwise"))) TurnSelection(doc, -90.0f);
      if (MenuParts::Item(T("Rotate 180°"))) TurnSelection(doc, 180.0f);
      MenuParts::Separator();
      if (MenuParts::Item(T("Flip Horizontal"))) FlipSelection(doc, false);
      if (MenuParts::Item(T("Flip Vertical"))) FlipSelection(doc, true);
      ImGui::EndMenu();
   }

   // Document crops from a menu: to the selection's box, or Trim to what the layers show (transparent edges cut).
   void CropToSelection(Scene2DNode* doc)
   {
      float lo[2], hi[2];
      if (!doc->SelectionBounds(lo, hi))
         return;
      PushUndoCheckpoint("Crop to selection");
      doc->CropTo(lo[0], lo[1], hi[0], hi[1]);
      sNeedFit = true;
      Touch();
   }

   void TrimDocument(Scene2DNode* doc)
   {
      static int sTrimFrame = 1 << 27;
      float lo[2], hi[2];
      if (!doc->VisibleBounds(doc->ChildrenOf(0), ++sTrimFrame, lo, hi))
         return;
      const auto& d = doc->Doc();
      if (lo[0] <= 0 && lo[1] <= 0 && hi[0] >= d.w && hi[1] >= d.h)
         return;
      PushUndoCheckpoint("Trim");
      doc->CropTo(lo[0], lo[1], hi[0], hi[1]);
      sNeedFit = true;
      Touch();
   }

   // Cmd-click a layer thumbnail (Photoshop): its pixels become the selection; Shift adds, Alt takes away.
   void SelectLayerPixels(Scene2DNode* doc, int id, int op)
   {
      static int sSelPxFrame = 1 << 28;
      SelStep("Select layer pixels");
      doc->SelectLayerPixels(id, op, ++sSelPxFrame);
   }

   void FreeTransformMenu(Scene2DNode* doc)
   {
      const bool pulls = FreeTransformOne(doc) && CanWarp(doc);
      for (int m = kFreeScale; m < kFreeModes; m++)
      {
         const bool needsPull = m >= kFreeSkew;
         if (MenuParts::Item(T(kFreeModeNames[m]), nullptr, sFreeMode == m, !needsPull || pulls))
            sFreeMode = sFreeMode == m ? kFreeAny : m;
      }
      const Layer* a = Active(doc);
      // Warp bends the picture on a grid: the Mesh Warp tool, on a paint layer.
      if (MenuParts::Item(T("Warp"), nullptr, false, a != nullptr && sSel.size() == 1 && a->kind == Kind::Paint))
      {
         EndFreeTransform(doc, true);
         sTool = kMeshWarp;
      }
      MenuParts::Separator();
      if (MenuParts::Item(T("Rotate 180°"))) TurnSelection(doc, 180.0f);
      if (MenuParts::Item(T("Rotate 90° Clockwise"))) TurnSelection(doc, 90.0f);
      if (MenuParts::Item(T("Rotate 90° Counter Clockwise"))) TurnSelection(doc, -90.0f);
      MenuParts::Separator();
      if (MenuParts::Item(T("Flip Horizontal"))) FlipSelection(doc, false);
      if (MenuParts::Item(T("Flip Vertical"))) FlipSelection(doc, true);
      MenuParts::Separator();
      if (MenuParts::Item(T("Apply"), "Enter")) EndFreeTransform(doc, true);
      if (MenuParts::Item(T("Cancel"), "Esc")) EndFreeTransform(doc, false);
   }

   // A tick or a cross drawn in the last item, in the bar icons' ink.
   void DrawTickCross(bool tick)
   {
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
      const float k = tok::view2d_k_ratio * tok::icon_control / tok::view2d_tick_cross_scale;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
      if (tick)
      {
         const ImVec2 pts[3] = { ImVec2(c.x - tok::view2d_tick_cross_ratio * k, c.y), ImVec2(c.x - tok::view2d_tick_cross_x * k, c.y + tok::view2d_tick_cross_y * k), ImVec2(c.x + tok::view2d_tick_cross_ratio * k, c.y - tok::view2d_line_y * k) };
         dl->AddPolyline(pts, tok::view2d_line_num_points_sm, col, 0, tok::view2d_tick_cross_stroke);
      }
      else
      {
         dl->AddLine(ImVec2(c.x - tok::view2d_line_y * k, c.y - tok::view2d_line_y * k), ImVec2(c.x + tok::view2d_line_y * k, c.y + tok::view2d_line_y * k), col, tok::view2d_tick_cross_stroke);
         dl->AddLine(ImVec2(c.x + tok::view2d_line_y * k, c.y - tok::view2d_line_y * k), ImVec2(c.x - tok::view2d_line_y * k, c.y + tok::view2d_line_y * k), col, tok::view2d_tick_cross_stroke);
      }
   }

   // Photoshop's transform options bar: where it is, how big against where it began, its angle; the mode; tick / cross.
   void DrawFreeTransformBar(Scene2DNode* doc)
   {
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      Layer* a = Active(doc);
      const Layer* b0 = nullptr;
      for (const Layer& b : sFreeT.before)
         if (a != nullptr && b.id == a->id)
            b0 = &b;
      if (FreeTransformOne(doc) && b0 != nullptr)
      {
         float bw = 1, bh = 1, m[6], cx, cy;
         doc->LayerBox(*a, bw, bh);
         doc->LayerMatrix(*a, m);
         Apply(m, bw * 0.5f, bh * 0.5f, cx, cy);
         float v[5] = { cx, cy, std::fabs(b0->sx) > 1e-6f ? a->sx / b0->sx * 100.0f : 100.0f,
                        std::fabs(b0->sy) > 1e-6f ? a->sy / b0->sy * 100.0f : 100.0f, a->angle };
         static const char* kId[5] = { "##ftx", "##fty", "##ftw", "##fth", "##fta" };
         static const char* kFmt[5] = { "X %.0f", "Y %.0f", "W %.1f%%", "H %.1f%%", "%.1f°" };
         static const float kSpeed[5] = { 1.0f, 1.0f, 0.5f, 0.5f, 0.5f };
         int changed = -1;
         for (int i = 0; i < 5; i++)
         {
            ImGui::SetNextItemWidth(kWellM);
            FieldWell::PushStyle();
            if (ImGui::DragFloat(kId[i], &v[i], kSpeed[i], 0.0f, 0.0f, kFmt[i]))
               changed = i;
            FieldWell::PopStyle();
            next();
         }
         if (changed >= 0)
         {
            if (changed == 2) a->sx = b0->sx * std::max(0.1f, v[2]) / 100.0f;
            if (changed == 3) a->sy = b0->sy * std::max(0.1f, v[3]) / 100.0f;
            if (changed == 4) a->angle = std::fmod(v[4] + 540.0f, 360.0f) - 180.0f;
            // The middle stays put (or goes where X / Y say).
            float m1[6], nx, ny;
            doc->LayerMatrix(*a, m1);
            Apply(m1, bw * 0.5f, bh * 0.5f, nx, ny);
            a->x += (changed == 0 ? v[0] : cx) - nx;
            a->y += (changed == 1 ? v[1] : cy) - ny;
            Touch();
         }
         BarDivider();
      }
      if (!FreeTransformOne(doc) || !CanWarp(doc))
         sFreeMode = std::min(sFreeMode, (int)kFreeRotate);
      BarCombo("##ftmode", &sFreeMode, kFreeModeNames, FreeTransformOne(doc) && CanWarp(doc) ? kFreeModes : kFreeSkew, kWellL);
      BarDivider();
      if (ActionButton::Draw("##ftok", ImVec2(kIconW, sRowH)))
         EndFreeTransform(doc, true);
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Apply (Enter)"));
      DrawTickCross(true);
      next();
      if (ActionButton::Draw("##ftno", ImVec2(kIconW, sRowH)))
         EndFreeTransform(doc, false);
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Cancel (Esc)"));
      DrawTickCross(false);
   }

   // Wells, combos and chips share one row height (the bar's sRowH): a well taller than its chips sat low and made
   // the chips look like they floated.
   void DrawContextBar(Scene2DNode* doc)
   {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, std::max(tok::view2d_context_bar_min, (sRowH - ImGui::GetFontSize()) * 0.5f)));
      DrawContextBarItems(doc);
      ImGui::PopStyleVar();
   }

   void DrawContextBarItems(Scene2DNode* doc)
   {
      auto gap = []() { BarDivider(); };
      auto next = []() { FormParts::Inline(0.0f, tok::space_1); };
      Layer* a = Active(doc);
      if (FreeTransformOn(doc))
      {
         DrawFreeTransformBar(doc);
         return;
      }
      if (sTool == kMove)
      {
         bool autoSel = sAutoSelect;
         // Checkbox and label sit on the chips' centre line; the cursor then returns to the row's top edge.
         const ImVec2 row0 = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(row0.x, row0.y + (sRowH - CheckBox::Size()) * 0.5f));
         if (CheckBox::Box("##v2dauto", &autoSel))
            sAutoSelect = autoSel;
         ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + tok::space_1, row0.y));
         CentredText(row0.y, T("Auto-select"));
         if (ImGui::IsItemClicked())
            sAutoSelect = !sAutoSelect;
         gap();
         // The 3D gizmo's modes: only with a 3D layer active. Greyed on a flat layer they read as a 2D Rotate that
         // will not turn on; a flat layer turns from just outside a corner handle, or in Free Transform.
         if (a != nullptr && a->is3d)
         {
            ImGui::BeginDisabled(false);
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
            const float k = tok::view2d_k_ratio * tok::icon_control / tok::view2d_tick_cross_scale;
            if (i < 3)
            {
               const float x = side == 0 ? c.x - tok::view2d_context_bar_items_scale * k : side == 1 ? c.x : c.x + tok::view2d_context_bar_items_scale * k;
               dl->AddLine(ImVec2(x, c.y - tok::view2d_context_bar_items_ratio * k), ImVec2(x, c.y + tok::view2d_context_bar_items_ratio * k), col, tok::view2d_context_bar_items_stroke);
               const float w1 = 10 * k, w2 = 6 * k;
               const float s1 = side == 0 ? x : side == 1 ? x - w1 / 2 : x - w1, s2 = side == 0 ? x : side == 1 ? x - w2 / 2 : x - w2;
               dl->AddRectFilled(ImVec2(s1, c.y - tok::view2d_fill_y * k), ImVec2(s1 + w1, c.y - 1 * k), col);
               dl->AddRectFilled(ImVec2(s2, c.y + 1 * k), ImVec2(s2 + w2, c.y + tok::view2d_fill_y * k), col);
            }
            else
            {
               const float y = side == 0 ? c.y - tok::view2d_else_scale_sm * k : side == 1 ? c.y : c.y + tok::view2d_else_scale_sm * k;
               dl->AddLine(ImVec2(c.x - tok::view2d_context_bar_items_ratio * k, y), ImVec2(c.x + tok::view2d_context_bar_items_ratio * k, y), col, tok::view2d_context_bar_items_stroke);
               const float h1 = 10 * k, h2 = 6 * k;
               const float s1 = side == 0 ? y : side == 1 ? y - h1 / 2 : y - h1, s2 = side == 0 ? y : side == 1 ? y - h2 / 2 : y - h2;
               dl->AddRectFilled(ImVec2(c.x - tok::view2d_fill_y * k, s1), ImVec2(c.x - 1 * k, s1 + h1), col);
               dl->AddRectFilled(ImVec2(c.x + 1 * k, s2), ImVec2(c.x + tok::view2d_fill_y * k, s2 + h2), col);
            }
            if (hit)
               Align(doc, i);
         }
         // Distribute: three bars, evenly spaced across (horizontal) or down (vertical).
         for (int i = 0; i < 2; i++)
         {
            next();
            ImGui::BeginDisabled(sSel.size() < tok::view2d_disabled_limit);
            const bool hit = ActionButton::Draw(i == 0 ? "##dist0" : "##dist1", ImVec2(kIconW, sRowH));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               HelpTip("%s", T(i == 0 ? "Distribute horizontally" : "Distribute vertically"));
            const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
            const ImVec2 c((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 col = ImGui::GetColorU32(sSel.size() < tok::view2d_color_limit ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            const float ks = tok::view2d_ks_ratio * tok::icon_control / tok::view2d_ks_scale;
            for (int k = -1; k <= 1; k++)
            {
               const float o = k * 5.5f * ks, len = (k == 0 ? 4.0f : 6.0f) * ks;
               if (i == 0)
                  dl->AddRectFilled(ImVec2(c.x + o - tok::view2d_context_bar_items_gap, c.y - len), ImVec2(c.x + o + tok::view2d_context_bar_items_gap, c.y + len), col);
               else
                  dl->AddRectFilled(ImVec2(c.x - len, c.y + o - tok::view2d_context_bar_items_gap), ImVec2(c.x + len, c.y + o + tok::view2d_context_bar_items_gap), col);
            }
            if (hit)
               Distribute(doc, i);
         }
         {
            gap();
            const bool canFlip = a != nullptr && !sSel.empty() && (Flippable(*a) || a->kind == Kind::Group);
            ImGui::BeginDisabled(!canFlip);
            if (TextChip(L("Flip H##v2dfh"), false, T("Flip horizontal")))
               FlipSelection(doc, false);
            next();
            if (TextChip(L("Flip V##v2dfv"), false, T("Flip vertical")))
               FlipSelection(doc, true);
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
         static const char* kRatio[7] = { "Free", "Original", "1:1", "4:3", "3:2", "16:9", "9:16" };
         if (BarCombo("##v2dcratio", &sCropRatio, kRatio, 7, kWellM))
            FitCropRatio(doc);
         next();
         if (TextChip(L("Thirds##v2dthirds"), sCropThirds, T("Rule of thirds guides")))
            sCropThirds = !sCropThirds;
         next();
         if (TextChip(L("Darken##v2dcdark"), sCropDarken, T("Dim outside the crop")))
            sCropDarken = !sCropDarken;
         next();
         if (TextChip(L("Reveal##v2dcrev"), sCropReveal, T("Show outside the crop")))
            sCropReveal = !sCropReveal;
         gap();
         if (TextChip(L("Apply##v2dcropok"), false, T("Crop the document (Enter)")) && CropBoxOk())
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
      const float availW = Ui::AvailW();
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
      auto chipW = [](const char* l) { return ImGui::CalcTextSize(l, ImGui::FindRenderedTextEnd(l)).x + tok::view2d_header_ratio * tok::space_2; };
      const float rightW = (doc->panelOnly ? chipW(convertL) + tok::space_1 : 0.0f) + chipW(renderL) + tok::view2d_right_w_ratio * (tok::space_1 + sChip);
      const float rowTop = ImGui::GetCursorScreenPos().y;
      const float barRoom = availW - rightW - (tok::view2d_header_ratio * tok::space_2 + 1.0f);   // one bar divider before the right cluster
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
         EndChildSafe();
         FormParts::Inline(0.0f, tok::space_1);
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
      FormParts::Inline();
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
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##v2dfill", IconsInfinite::PopOut, gView2DDock == 4, ViewDockFillTip())) ViewDockToggleFill(2);
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
         ZoomAt(doc, std::clamp(pct, 1.0f, kZoomMax * 100.0f) / 100.0f / sZoom, mid);
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
         std::snprintf(buf, sizeof(buf), "%d x %d   %s, %s   %s", d.w, d.h, UiFmt::F0(sMouseDoc.x).c, UiFmt::F0(sMouseDoc.y).c, UiFmt::Ms(doc->CookMs()).c);
      else
         std::snprintf(buf, sizeof(buf), "%d x %d   %s", d.w, d.h, UiFmt::Ms(doc->CookMs()).c);
      CentredText(row0Y, buf);
   }

   // ---- layers list ------------------------------------------------------

   void DrawThumb(ImDrawList* dl, Scene2DNode* doc, const Layer& l, ImVec2 p, ImVec2 sz)
   {
      dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), tok::U32(tok::backdrop_base, app::IsThemeLight()), tok::radius_cell);
      if (l.kind == Kind::Group)
      {
         glyph::Draw(dl, ImVec2(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), tok::icon_control, ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::Folder);
         return;
      }
      if (l.kind == Kind::Adjust)
      {
         glyph::Draw(dl, ImVec2(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), tok::icon_control, ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::ShadeSolid);
         return;
      }
      if (l.kind == Kind::Fill)
      {
         const ImU32 ca = ImGui::GetColorU32(ImVec4(l.colorA[0], l.colorA[1], l.colorA[2], 1.0f));
         const ImU32 cb = ImGui::GetColorU32(ImVec4(l.colorB[0], l.colorB[1], l.colorB[2], 1.0f));
         if (l.fillType == 0)
            dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ca, tok::radius_cell);
         else
            dl->AddRectFilledMultiColor(p, ImVec2(p.x + sz.x, p.y + sz.y), ca, ca, cb, cb);
         return;
      }
      if (l.kind == Kind::Paint)
      {
         DrawChecker(dl, p, ImVec2(p.x + sz.x, p.y + sz.y));
         const unsigned ptex = doc->PaintTexture(l.id);
         const float pw = (float)std::max(1, l.pw), ph = (float)std::max(1, l.ph);
         // Cropped to what the layer holds (Affinity, Procreate): a small cut-out fills the thumbnail instead of
         // sitting as a speck in a document-sized square. A little margin keeps its edge off the frame.
         float r[4];
         ContentRect(doc, l, r);
         const float m = std::max(r[2] - r[0], r[3] - r[1]) * tok::view2d_m_ratio;
         r[0] = std::max(0.0f, r[0] - m), r[1] = std::max(0.0f, r[1] - m);
         r[2] = std::min(pw, r[2] + m), r[3] = std::min(ph, r[3] + m);
         const float cw = std::max(1.0f, r[2] - r[0]), ch = std::max(1.0f, r[3] - r[1]);
         const float k = std::min(sz.x / cw, sz.y / ch);
         const ImVec2 c(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), h(cw * k * 0.5f, ch * k * 0.5f);
         if (ptex != 0)
            Thumb::Add(dl, (ImTextureID)(intptr_t)ptex, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y),
                         ImVec2(r[0] / pw, 1.0f - r[1] / ph), ImVec2(r[2] / pw, 1.0f - r[3] / ph));
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
            Thumb::Add(dl, (ImTextureID)(intptr_t)vtex, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y), ImVec2(0, 1), ImVec2(1, 0));
         return;
      }
      INode* src = doc->Source(l);
      const unsigned tex = src != nullptr ? src->GetOutputTexture() : 0;
      if (tex == 0)
      {
         dl->AddLine(p, ImVec2(p.x + sz.x, p.y + sz.y), tok::U32(tok::record));
         return;
      }
      const float sw = (float)std::max(1, src->GetOutputWidth()), sh = (float)std::max(1, src->GetOutputHeight());
      const float k = std::min(sz.x / sw, sz.y / sh);
      const ImVec2 c(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f), h(sw * k * 0.5f, sh * k * 0.5f);
      Thumb::Add(dl, (ImTextureID)(intptr_t)tex, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y), ImVec2(0, 1), ImVec2(1, 0));
   }

   const ImU32 kLabelCol[8] = { 0, tok::U32(tok::label_red), tok::U32(tok::label_orange), tok::U32(tok::label_yellow),
                                tok::U32(tok::label_green), tok::U32(tok::label_blue), tok::U32(tok::label_violet),
                                tok::U32(tok::label_grey) };

   [[maybe_unused]] int CountRows(Scene2DNode* doc, int parent)
   {
      int n = 0;
      for (int id : doc->ChildrenOf(parent))
         if (const Layer* l = doc->FindLayer(id))
            n += 1 + (l->kind == Kind::Group && l->open ? CountRows(doc, id) : 0);
      return n;
   }

   int sEyeDrag = -1;   // a drag down the eye column: 1 shows, 0 hides what it crosses, -1 none

   bool HasCopiedEffects();
   void CopyLayerEffects(const Layer& l);
   void PasteLayerEffects(Scene2DNode* doc, int id);

   void DrawLayerRows(Scene2DNode* doc, int parent, int depth, float w)
   {
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         sEyeDrag = -1;
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
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_Header, Active(doc) == l ? 1.0f : tok::view2d_layer_rows_color_alpha_mul), tok::view2d_layer_rows_radius);
         if (l->label > 0 && l->label < 8)
            dl->AddRectFilled(ImVec2(p.x, p.y + tok::space_0_75), ImVec2(p.x + tok::space_0_75, p.y + rowH - tok::space_0_75), kLabelCol[l->label]);
         // Eye: Alt-click shows only this one.
         ImGui::SetCursorScreenPos(ImVec2(p.x + tok::space_1, p.y + (rowH - tok::view2d_layer_rows_gap) * 0.5f));
         // A press flips the eye; dragging on down the column sets every eye it crosses the same way (Photoshop).
         UiInteract::Hit("##eye", ImVec2(tok::view2d_layer_rows_hit_x, tok::view2d_layer_rows_hit_y));
         if (ImGui::IsItemActivated())
         {
            if (io.KeyAlt)
               doc->soloLayer = doc->soloLayer == id ? 0 : id;
            else
            {
               PushUndoCheckpoint(l->visible ? "Hide layer" : "Show layer");
               l->visible = !l->visible;
               sEyeDrag = l->visible ? 1 : 0;
               Touch();
            }
         }
         else if (sEyeDrag >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                  ImGui::IsMouseHoveringRect(ImVec2(p.x + tok::space_1, p.y), ImVec2(p.x + tok::view2d_layer_rows_x_md, p.y + rowH)) && l->visible != (sEyeDrag == 1))
         {
            l->visible = sEyeDrag == 1;
            Touch();
         }
         glyph::Draw(dl, ImVec2(p.x + tok::view2d_layer_rows_x_sm, p.y + rowH * 0.5f), tok::icon_control,
                     ImGui::GetColorU32(l->visible ? ImGuiCol_Text : ImGuiCol_TextDisabled), l->visible ? IconsInfinite::Eye : IconsInfinite::EyeOff);
         float x = p.x + tok::space_5 + depth * tok::view2d_canvas_scale;
         if (l->kind == Kind::Group)
         {
            ImGui::SetCursorScreenPos(ImVec2(x, p.y + (rowH - tok::view2d_open_inset) * 0.5f));
            if (UiInteract::Hit("##open", ImVec2(tok::view2d_ui_interact_x, tok::view2d_ui_interact_y)))
            {
               l->open = !l->open;
               Touch();
            }
            const ImVec2 c(x + tok::space_1_5, p.y + rowH * 0.5f);
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            if (l->open)
               dl->AddTriangleFilled(ImVec2(c.x - tok::space_1, c.y - tok::space_0_5), ImVec2(c.x + tok::space_1, c.y - tok::space_0_5), ImVec2(c.x, c.y + tok::space_0_75), col);
            else
               dl->AddTriangleFilled(ImVec2(c.x - tok::space_0_5, c.y - tok::space_1), ImVec2(c.x + tok::space_0_75, c.y), ImVec2(c.x - tok::space_0_5, c.y + tok::space_1), col);
         }
         x += tok::view2d_x;
         if (l->clip)
         {
            // The bent arrow that points down at the layer it is clipped to.
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            dl->AddLine(ImVec2(x, p.y + tok::space_1_25), ImVec2(x, p.y + rowH - tok::space_1_5), col, tok::view2d_layer_rows_stroke);
            dl->AddLine(ImVec2(x, p.y + rowH - tok::space_1_5), ImVec2(x + tok::space_1_5, p.y + rowH - tok::space_1_5), col, tok::view2d_layer_rows_stroke);
            x += tok::view2d_layer_rows_x_xs;
         }
         // Photoshop's pair: picture thumb, then the mask thumb. Click one to aim the brush at it (the framed one is
         // the target); Shift-click the mask to turn it off and on.
         const bool hasMask = l->kind != Kind::Group && l->HasPixelMask();
         const bool aimMask = hasMask && sEditMask && Active(doc) == l;
         const ImVec2 tsz(tok::view2d_layer_rows_x, rowH - tok::space_1);
         DrawThumb(dl, doc, *l, ImVec2(x, p.y + tok::space_0_5), tsz);
         if ((io.KeySuper || io.KeyCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
             ImGui::IsMouseHoveringRect(ImVec2(x, p.y + tok::space_0_5), ImVec2(x + tsz.x, p.y + tok::space_0_5 + tsz.y)) &&
             l->kind != Kind::Fill && l->kind != Kind::Adjust)
            SelectLayerPixels(doc, id, io.KeyShift ? Scene2DNode::kSelAdd : io.KeyAlt ? Scene2DNode::kSelSub : Scene2DNode::kSelNew);
         if (l->kind == Kind::Adjust)
         {
            // An adjustment layer's thumbnail opens its settings window (Photoshop's Properties for that layer).
            ImGui::SetCursorScreenPos(ImVec2(x, p.y + tok::space_0_5));
            if (UiInteract::Hit("##athumb", tsz))
            {
               SelectOnly(id);
               SettingsWindow::Toggle("v2dadj/" + std::to_string(sDocUid) + "/" + std::to_string(id));
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Click for its settings"));
         }
         if (hasMask)
         {
            ImGui::SetCursorScreenPos(ImVec2(x, p.y + tok::space_0_5));
            if (UiInteract::Hit("##pthumb", tsz))
            {
               SelectOnly(id);
               sEditMask = false;
            }
            if (!aimMask && Active(doc) == l)
               dl->AddRect(ImVec2(x - tok::space_0_25, p.y + tok::space_0_25), ImVec2(x + tsz.x + tok::space_0_25, p.y + tok::space_0_75 + tsz.y), ImGui::GetColorU32(ImGuiCol_Text), tok::radius_cell);
            x += tsz.x + tok::space_1;
            const ImVec2 m0(x, p.y + tok::space_0_5), m1(x + tsz.x, p.y + tok::view2d_m1_inset + tsz.y);
            dl->AddRectFilled(m0, m1, tok::U32(tok::backdrop_base, app::IsThemeLight()), tok::radius_cell);
            const float mw = (float)std::max(1, l->mw), mh = (float)std::max(1, l->mh);
            const float k = std::min(tsz.x / mw, tsz.y / mh);
            const ImVec2 c(x + tsz.x * 0.5f, p.y + tok::space_0_5 + tsz.y * 0.5f), h(mw * k * 0.5f, mh * k * 0.5f);
            if (const unsigned mt = doc->PixelMaskTexture(id))
               Thumb::Add(dl, (ImTextureID)(intptr_t)mt, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y), ImVec2(0, 1), ImVec2(1, 0));
            if (!l->pmaskOn)
            {
               dl->AddLine(ImVec2(m0.x + tok::space_0_75, m0.y + tok::space_0_75), ImVec2(m1.x - tok::space_0_75, m1.y - tok::space_0_75), tok::U32(tok::record), tok::view2d_line_stroke_md);
               dl->AddLine(ImVec2(m1.x - tok::space_0_75, m0.y + tok::space_0_75), ImVec2(m0.x + tok::space_0_75, m1.y - tok::space_0_75), tok::U32(tok::record), tok::view2d_line_stroke_md);
            }
            if (aimMask)
               dl->AddRect(ImVec2(m0.x - tok::space_0_25, m0.y - tok::space_0_25), ImVec2(m1.x + tok::space_0_25, m1.y + tok::space_0_25), ImGui::GetColorU32(ImGuiCol_Text), tok::radius_cell);
            ImGui::SetCursorScreenPos(m0);
            if (UiInteract::Hit("##mthumb", tsz))
            {
               if (io.KeySuper || io.KeyCtrl)
               {
                  // Cmd-click loads the mask as the selection (Shift adds, Alt subtracts).
                  SelStep("Load mask as selection");
                  doc->SelectMaskPixels(id, io.KeyShift ? Scene2DNode::kSelAdd : io.KeyAlt ? Scene2DNode::kSelSub : Scene2DNode::kSelNew);
               }
               else if (io.KeyShift)
               {
                  PushUndoCheckpoint(l->pmaskOn ? "Turn mask off" : "Turn mask on");
                  l->pmaskOn = !l->pmaskOn;
                  Touch();
               }
               else
               {
                  SelectOnly(id);
                  sEditMask = true;
               }
            }
            if (ImGui::IsItemHovered())
               Tip::Show("%s", T("Layer mask: click to paint it, Shift-click to turn it off"));
            // The mask thumbnail has its own menu: only what acts on the mask.
            if (MenuParts::ContextClicked())
               ImGui::OpenPopup("##maskmenu");
            if (MenuParts::BeginPopup("##maskmenu"))
            {
               if (!Selected(id))
                  SelectOnly(id);
               if (MenuParts::Item(T("Paint Mask"), nullptr, aimMask))
                  sEditMask = true;
               if (MenuParts::Item(l->pmaskOn ? T("Disable Mask") : T("Enable Mask"), nullptr))
               {
                  PushUndoCheckpoint(l->pmaskOn ? "Turn mask off" : "Turn mask on");
                  l->pmaskOn = !l->pmaskOn;
                  Touch();
               }
               MenuParts::Separator();
               if (MenuParts::Item(T("Mask to Selection"), nullptr))
               {
                  SelStep("Load mask as selection");
                  doc->SelectMaskPixels(id, Scene2DNode::kSelNew);
               }
               if (MenuParts::Item(T("Mask from Selection"), nullptr, false, doc->HasSelection())) sPending.what = Pending::MaskFromSel;
               MenuParts::Separator();
               if (MenuParts::Item(T("Delete Mask"), nullptr)) sPending.what = Pending::RemoveMask;
               MenuParts::EndPopup();
            }
         }
         x += tok::view2d_layer_rows_x_xl;
         ImGui::SetCursorScreenPos(ImVec2(x, p.y + (rowH - ImGui::GetFrameHeight()) * 0.5f));
         if (sRename == id)
         {
            ImGui::SetNextItemWidth(std::max(tok::text_min_w, p.x + w - x - tok::space_5));
            if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
               UiInteract::FocusHere();
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
            const std::string full = LayerName(*l);
            const std::string name = app::TruncateWithEllipsis(full, std::max(16.0f, p.x + w - x - tok::space_5));
            const bool missing = l->kind == Kind::Node && doc->Source(*l) == nullptr;
            ImGui::AlignTextToFramePadding();
            if (missing)
               ImGui::TextDisabled("%s", name.c_str());
            else
               ImGui::TextUnformatted(name.c_str());
         }
         if (l->lock != 0)
            glyph::Draw(dl, ImVec2(p.x + w - tok::space_3, p.y + rowH * 0.5f), tok::icon_control, ImGui::GetColorU32(ImGuiCol_TextDisabled), IconsInfinite::Lock);
         // The row itself: select, rename, drag to restack.
         ImGui::SetCursorScreenPos(ImVec2(x, p.y));
         ImGui::SetNextItemAllowOverlap();
         UiInteract::Hit("##row", ImVec2(std::max(1.0f, p.x + w - x), rowH));
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
         ReorderDrag::Source("V2D_LAYER", &id, sizeof(int), LayerName(*l).c_str());
         if (ImGui::BeginDragDropTarget())
         {
            const float t = (io.MousePos.y - p.y) / rowH;
            const ImGuiPayload* peek = ImGui::GetDragDropPayload();
            const int peekId = (peek != nullptr && peek->IsDataType("V2D_LAYER") && peek->DataSize == sizeof(int)) ? *(const int*)peek->Data : 0;
            const Layer* peekLayer = peekId != 0 ? doc->FindLayer(peekId) : nullptr;
            const bool clipDrop = peekLayer != nullptr && peekLayer->kind == Kind::Adjust && peekId != id &&
                                  l->kind != Kind::Group && l->kind != Kind::Adjust && t > 0.25f && t < 0.75f;
            const bool into = !clipDrop && l->kind == Kind::Group && t > 0.3f && t < 0.7f;
            const bool below = !into && !clipDrop && t >= 0.5f;
            if (peekId != 0)
            {
               // The app's one reorder mark (ReorderDrag): a box when it goes into a group or clips, else the bar.
               if (clipDrop || into)
                  ReorderDrag::Box(p, ImVec2(p.x + w, p.y + rowH));
               else
                  ReorderDrag::Bar(ImVec2(p.x + tok::space_0_5, below ? p.y + rowH : p.y), ImVec2(p.x + w - tok::space_0_5, below ? p.y + rowH : p.y));
               if (clipDrop)
                  Tip::Show(T("Clip to %s"), LayerName(*l).c_str());
            }
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("V2D_LAYER", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
               const int moving = *(const int*)pl->Data;
               if (io.KeyAlt && !(into && doc->IsInside(id, moving)))
               {
                  // Alt-drop copies: the duplicate lands where the drop says, the original stays (Photoshop).
                  PushUndoCheckpoint("Duplicate layer");
                  const int dup = doc->Duplicate(moving);
                  if (dup != 0 && dup != id)
                  {
                     doc->MoveLayer(dup, id, clipDrop ? false : below, into);
                     if (Layer* dl2 = doc->FindLayer(dup))
                        dl2->clip = clipDrop;
                     SelectOnly(dup);
                     Touch();
                  }
               }
               else if (moving != id && !doc->IsInside(id, moving))
               {
                  PushUndoCheckpoint(clipDrop ? "Clip adjustment to layer" : "Move layer");
                  doc->MoveLayer(moving, id, clipDrop ? false : below, into);
                  if (Layer* ml = doc->FindLayer(moving))
                  {
                     if (clipDrop)
                        ml->clip = true;
                     else if (ml->kind == Kind::Adjust && (below || into))
                        ml->clip = false;
                  }
                  Touch();
               }
            }
            ImGui::EndDragDropTarget();
         }
         // The whole row opens the menu: eye, fold arrow and thumbnail included.
         if (MenuParts::RightPressed() && ImGui::IsWindowHovered() &&
             ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + rowH)) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
            ImGui::OpenPopup("##rowmenu");
         else if (MenuParts::TestOpen("##rowmenu"))   // self-test: the top row, as a right-click on it would
            ImGui::OpenPopup("##rowmenu");
         if (MenuParts::BeginPopup("##rowmenu"))
         {
            if (!Selected(id))
               SelectOnly(id);
            if (MenuParts::Item(T("Rename"), nullptr))
            {
               sRename = id;
               std::snprintf(sRenameBuf, sizeof(sRenameBuf), "%s", LayerName(*l).c_str());
            }
            // One menu per kind: an adjustment layer is a setting, not a picture, so the picture-only items
            // (merges into pixels, copy merged, effects, output) stay off it; a clipped layer leads with Release.
            const bool adj = l->kind == Kind::Adjust;
            auto clipItem = [&]()
            {
               if (MenuParts::Item(l->clip ? T("Release Clipping Mask") : T("Create Clipping Mask"), Sc("Alt+Ctrl+G")))
               {
                  PushUndoCheckpoint("Clipping mask");
                  l->clip = !l->clip;
                  Touch();
               }
            };
            if (adj && MenuParts::Item(T("All Settings"), nullptr, SettingsWindow::IsOpen("v2dadj/" + std::to_string(sDocUid) + "/" + std::to_string(id))))
               SettingsWindow::Toggle("v2dadj/" + std::to_string(sDocUid) + "/" + std::to_string(id));
            if (l->clip)
               clipItem();
            MenuParts::Separator();
            if (MenuParts::Item(T("Cut"), Sc("Ctrl+X"))) sPending.what = Pending::Cut;
            if (MenuParts::Item(T("Copy"), Sc("Ctrl+C"))) sPending.what = Pending::Copy;
            if (!adj && MenuParts::Item(T("Copy Merged"), Sc("Shift+Ctrl+C"))) sPending.what = Pending::CopyMerged;
            if (MenuParts::Item(T("Paste"), Sc("Ctrl+V"))) sPending.what = Pending::Paste;
            if (!adj && !l->effects.empty() && MenuParts::Item(T("Copy Effects"), nullptr))
               CopyLayerEffects(*l);
            if (HasCopiedEffects() && !adj && MenuParts::Item(T("Paste Effects"), nullptr))
               PasteLayerEffects(doc, id);
            if (MenuParts::Item(l->kind == Kind::Group ? T("Duplicate Group") : adj ? T("Duplicate Adjustment") : T("Duplicate Layer"), Sc("Ctrl+J")))
               sPending.what = Pending::Duplicate;
            if (!adj)
               TransformSubMenu(doc);
            if (l->kind != Kind::Fill && l->kind != Kind::Adjust && MenuParts::Item(T("Select Layer Pixels")))
               SelectLayerPixels(doc, id, Scene2DNode::kSelNew);
            MenuParts::Separator();
            // Photoshop's merges: the picture as it looks, into one pixel layer.
            if (sSel.size() > 1 ? MenuParts::Item(T("Merge Layers"), Sc("Ctrl+E"))
                                : MenuParts::Item(T("Merge Down"), Sc("Ctrl+E"), false, LayerUnder(doc, id) != 0))
               sPending.what = Pending::MergeDown;
            if (!adj)
            {
               if (MenuParts::Item(T("Merge Visible"), Sc("Shift+Ctrl+E"))) sPending.what = Pending::MergeVisible;
               if (MenuParts::Item(T("Stamp Visible"), Sc("Shift+Alt+Ctrl+E"))) sPending.what = Pending::StampVisible;
               // A group flattens alone: its layers become one pixel layer, every other layer and group stays as it is.
               if (l->kind == Kind::Group && MenuParts::Item(T("Flatten Group"), nullptr)) sPending.what = Pending::FlattenGroup;
               if (MenuParts::Item(T("Flatten Image"), nullptr)) sPending.what = Pending::Flatten;
            }
            MenuParts::Separator();
            if (MenuParts::Item(T("Group Layers"), Sc("Ctrl+G"))) sPending.what = Pending::Group;
            if (l->kind == Kind::Group && MenuParts::Item(T("Ungroup"), Sc("Shift+Ctrl+G"))) sPending.what = Pending::Ungroup;
            if (!l->clip)
               clipItem();
            if (l->kind != Kind::Group)
            {
               if (!l->HasPixelMask() && MenuParts::Item(T("Add Layer Mask"), nullptr))
                  sPending.what = doc->HasSelection() ? Pending::MaskFromSel : Pending::AddMask;
               if (MenuParts::Item(T("Mask from Selection"), nullptr, false, doc->HasSelection())) sPending.what = Pending::MaskFromSel;
               if (l->HasPixelMask() && MenuParts::Item(T("Delete Layer Mask"), nullptr)) sPending.what = Pending::RemoveMask;
               if (!l->hasVMask && MenuParts::Item(T("Add Vector Mask"), nullptr)) sPending.what = Pending::AddVMask;
               if (l->hasVMask && MenuParts::Item(T("Delete Vector Mask"), nullptr)) sPending.what = Pending::RemoveVMask;
            }
            if (l->kind == Kind::Node)
            {
               NodeChoiceMenu(T("Replace with"), doc, Pending::ReplaceNode, l->uid);
               CanvasLink::PickMenuItem(PickSpec(true));
            }
            if (l->kind == Kind::Fill && MenuParts::Item(T("Edit Fill..."), nullptr))
            {
               sTab = 1;
               sSideOpen = true;
            }
            if (l->kind == Kind::Paint && MenuParts::Item(T("Convert to Draw Node"), nullptr)) sPending.what = Pending::ToDraw;
            if ((l->kind == Kind::Text || l->kind == Kind::Vector) && MenuParts::Item(T("Rasterize Layer"), nullptr))
               RasterizeLayer(doc, l->id);
            if (l->kind == Kind::Node && !l->HasSkin() && MenuParts::Item(T("Rasterize Layer"), nullptr))
               EnsurePixels(doc);
            if (l->kind == Kind::Vector && MenuParts::Item(T("Convert to Sketch Node"), nullptr)) sPending.what = Pending::ToSketch;
            if (l->kind == Kind::Text && MenuParts::Item(T("Convert to Text Node"), nullptr)) sPending.what = Pending::ToText;
            if (l->kind == Kind::Text && MenuParts::Item(T("Convert to Shapes"), nullptr)) sPending.what = Pending::ToShapes;
            if (!adj && MenuParts::Item(T("Layer as Output"), nullptr, l->output)) sPending.what = Pending::LayerOutput;
            if (!adj && doc->CanTakeBelow(*l) && MenuParts::Item(T("Effect on Below"), nullptr, l->below)) sPending.what = Pending::LayerBelow;
            if (l->warpOn && MenuParts::Item(T("Reset Distort"), nullptr))
            {
               PushUndoCheckpoint("Reset distort");
               l->warpOn = false;
               std::fill(l->warp, l->warp + 8, 0.0f);
               Touch();
            }
            MenuParts::Separator();
            if (MenuParts::Item(l->kind == Kind::Group ? T("Delete Group") : adj ? T("Delete Adjustment") : T("Delete Layer"), "Delete"))
               sPending.what = Pending::Delete;
            MenuParts::EndPopup();
         }
         ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + 1.0f));
         Ui::Settle();
         const bool open = l->kind == Kind::Group && l->open;
         ImGui::PopID();
         if (open)
            DrawLayerRows(doc, id, depth + 1, w);
      }
   }

   // The menu's names and the mode each one sets (a group's Pass Through is -1). The old Overlay / Soft / Hard Light
   // only show on a layer that already uses one; Contrast Invert sits after Erase, as in Affinity.
   std::vector<std::string> BlendNames(int current, bool group, std::vector<int>* modes = nullptr)
   {
      std::vector<std::string> names;
      std::vector<int> ids;
      if (group)
      {
         names.push_back(T("Pass Through"));
         ids.push_back(Scene2DNode::kPassThrough);
      }
      const std::vector<std::string>& all = BlendModes::NamesFor(current);
      for (int i = 0; i < (int)all.size(); i++)
      {
         names.push_back(T(all[i].c_str()));
         ids.push_back(i);
      }
      if (!group)
      {
         names.push_back(T(BlendModes::Names()[BlendModes::kContrastInvertMode].c_str()));
         ids.push_back(BlendModes::kContrastInvertMode);
      }
      if (modes != nullptr)
         *modes = std::move(ids);
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
         std::vector<int> modes;
         std::vector<std::string> names = BlendNames(a->blend, group, &modes);
         // Blend + Opacity, then Fill + Lock: chip-high wells so they share the chips' rows, both rows flush to w.
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, (sChip - ImGui::GetFontSize()) * 0.5f));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, PanelKit::RowGap()));
         const float half = (w - tok::space_1) * 0.5f;
         ImGui::SetNextItemWidth(half);
         const int shown = std::max(0, (int)(std::find(modes.begin(), modes.end(), a->blend) - modes.begin()) % (int)modes.size());
         if (MenuParts::BeginCombo("##v2dblend", names[shown].c_str()))
         {
            for (int i = 0; i < (int)names.size(); i++)
               if (MenuParts::Choice(names[i].c_str(), i == shown) && i != shown)
               {
                  PushUndoCheckpoint("Blend mode");
                  a->blend = modes[i];
                  Touch();
               }
            MenuParts::EndCombo();
         }
         FormParts::Inline(0.0f, tok::space_1);
         ImGui::SetNextItemWidth(w - half - tok::space_1);
         float pct = a->opacity * tok::view2d_pct_ratio;
         ModNext(*a, Scene2DNode::kLfOpacity);
         ModBegin();
         if (FieldWell::Slider("##v2dopacity", &pct, 0.0f, params::view2d::pct_hi, "Opacity %.0f%%"))
         {
            a->opacity = pct / tok::view2d_v2drsize_scale;
            Touch();
         }
         ModEnd();
         UndoOnActivate("Opacity");
         const float lockW = ImGui::CalcTextSize(T("Lock")).x + tok::view2d_lock_w_ratio * (tok::space_1 + sChip);
         ImGui::SetNextItemWidth(std::max(kWellM, w - lockW - tok::space_1));
         float fillPct = a->fill * 100.0f;
         ModNext(*a, Scene2DNode::kLfFill);
         ModBegin();
         if (FieldWell::Slider("##v2dfill", &fillPct, params::view2d::fill_pct_lo, params::view2d::fill_pct_hi, "Fill %.0f%%"))
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
            const bool heldOff = ToggledLock(a->lock, lk.bit) == a->lock;   // transparency under a pixel lock
            ImGui::BeginDisabled(heldOff);
            const bool hit = IconChip(lk.id, lk.glyph, (a->lock & lk.bit) != 0, T(lk.tip));
            ImGui::EndDisabled();
            if (hit)
            {
               const unsigned next = ToggledLock(a->lock, lk.bit);
               if (next != a->lock)
               {
                  PushUndoCheckpoint("Lock layer");
                  a->lock = next;
                  Touch();
               }
            }
         }
         ImGui::PopStyleVar(2);
         ImGui::EndDisabled();
         if (none)
            a = nullptr;
      }
      // New / delete up top, where the layer list starts (the same actions as the strip below).
      if (IconChip("##v2dnl", IconsInfinite::Plus, false, T("New layer")))
         sPending.what = Pending::AddPaint;
      FormParts::Inline(0.0f, tok::space_1);
      ImGui::BeginDisabled(sSel.empty());
      if (IconChip("##v2ddl", IconsInfinite::Close, false, T("Delete layer")))
         sPending.what = Pending::Delete;
      ImGui::EndDisabled();
      // The blend / opacity / lock rows above already took their height: the list gets what is left, so it scrolls.
      listH = std::max(tok::view2d_layers_card_ratio * (kRowH + 1.0f), std::min(listH, Ui::AvailH() - tok::space_0_5));
      ImGui::BeginChild("##v2dlayers", ImVec2(w, listH), ImGuiChildFlags_None);
      const float rowsW = Ui::AvailW();
      if (doc->Layers().empty())
         CentredText(ImGui::GetCursorScreenPos().y, T("No layers"));
      DrawLayerRows(doc, 0, 0, rowsW);
      // Dropping below the last row puts a layer at the bottom of the document.
      Ui::Claim(ImVec2(rowsW, std::max(tok::view2d_layers_card_min, Ui::AvailH())));
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
      EndChildSafe();
      // The strip under the list, as in every layers panel, centred under it.
      const float footW = tok::view2d_foot_w_ratio * sChip + tok::view2d_layers_card_foot_w * tok::space_1;
      Ui::CentreNext(footW, w);
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
      if (IconChip("##v2dnewnode", IconsInfinite::Plus, PickModeNow() == 1, T("Add a node from the canvas as a layer")))
         ImGui::OpenPopup("##v2dnodepop");
      if (MenuParts::BeginPopup("##v2dnodepop"))
      {
         CanvasLink::PickMenuItem(PickSpec(false));
         MenuParts::Separator();
         bool any = false;
         for (GraphNode& gn : gNodes)
         {
            if (gn.node.get() == doc || !View2DCanAdd(gn) || WouldCreateDocCycle(doc, gn.node.get()))
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
         FieldLabel(T("Source"));
         CanvasLink::Field("##v2dsrc", l->uid, PickSpec(true), false, sFieldW);
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
      if (ComboRow(T("Label"), &label, { T("None"), T("Red"), T("Orange"), T("Yellow"), T("Green"), T("Blue"), T("Violet"), T("Grey") }))
         l->label = label;
      BoolRow(T("Clip to layer below"), &l->clip);
      SliderRow(T("Gamma"), &l->blendGamma, params::view2d::blend_gamma_lo, params::view2d::blend_gamma_hi, "%.2f");
      if (l->kind == Kind::Vector || l->kind == Kind::Text || l->kind == Kind::Node)
         ComboRow(T("Antialias"), &l->aaMode, { T("Default"), T("On"), T("Off") });
      {
         bool inExport = !l->noExport;
         if (BoolRow(T("In export"), &inExport))
            l->noExport = !inExport;
      }
      if (l->kind == Kind::Group && l->blend == Scene2DNode::kPassThrough)
         BoolRow(T("Sort 3D with outside"), &l->collapse);
      if (l->kind != Kind::Adjust)
      {
         // Blend If (grey), Photoshop's sliders: one grey ramp per row, four handles on it - black, black to, white
         // from, white. The inner two split off the outer two (drag a handle off its pair to make the soft range).
         SectionCard::Begin(T("Blend If"));
         for (int side = 0; side < 2; side++)
         {
            float* b = side == 0 ? l->bifT : l->bifU;
            ImGui::PushID(side);
            FieldLabel(side == 0 ? T("This layer") : T("Underlying"));
            if (BlendIfRamp("##bif", b, sFieldW))
               Touch();
            ImGui::PopID();
         }
      }
      if (l->kind == Kind::Paint)
      {
         SectionCard::Begin(T("Relief"));
         BoolRow(T("On"), &l->relief);
      }
      if (l->kind == Kind::Paint && l->relief)
      {
         ComboRow(T("Material"), &l->reliefMat, { T("Plasticine"), T("Metal"), T("Chrome"), T("Ink stipple") });
         {
            Pairs g;
            SliderRow(T("Light"), &l->reliefLight, params::view2d::relief_light_lo, params::view2d::relief_light_hi, "%.0f°");
            SliderRow(T("Light height"), &l->reliefElev, params::view2d::relief_elev_lo, params::view2d::relief_elev_hi, "%.0f°");
         }
         SliderRow(T("Depth"), &l->reliefDepth, params::view2d::relief_depth_lo, params::view2d::relief_depth_hi, "%.1f px");
      }
      if (l->kind == Kind::Fill)
      {
         SectionCard::Begin(T("Fill"));
         int type = l->fillType;
         if (ComboRow(T("Type"), &type, { T("Solid"), T("Linear"), T("Radial"), T("Reflected"), T("Diamond"), T("Conical"), T("Bitmap") }))
            l->fillType = type;
         if (type == 6)
         {
            const std::string shown = l->fillImage.empty() ? std::string(T("Choose image")) : l->fillImage.substr(l->fillImage.find_last_of("/\\") + 1);
            FieldLabel(T("Image"));
            if (ImGui::Button((shown + "##v2dfillimg").c_str(), ImVec2(sFieldW, 0.0f)))
            {
               const std::string path = Platform::OpenImageDialog();
               if (!path.empty())
               {
                  PushUndoCheckpoint("Fill image");
                  l->fillImage = path;
                  Touch();
               }
            }
         }
         else if (type == 0)
         {
            ColorRow(T("Colour"), l->colorA);
            SliderRow(T("Alpha"), &l->colorA[3], 0.0f, 1.0f, "%.2f");
         }
         else if (GradientRows("##v2dfillgrad", l->stops, l->colorA, l->colorB))
         {
            // The ends stay in colour A and B, which the rest of the layer (and older readers) know.
            std::copy(l->stops.front().c, l->stops.front().c + 4, l->colorA);
            std::copy(l->stops.back().c, l->stops.back().c + 4, l->colorB);
         }
         SliderRow(T("Noise"), &l->fillNoise, 0.0f, 1.0f, "%.2f");
         if (type == 1 || type == 3 || type == 4 || type == 5)
         {
            ModNext(*l, Scene2DNode::kLfFillAngle);
            SliderRow(T("Angle"), &l->fillAngle, params::view2d::fill_angle_lo, params::view2d::fill_angle_hi, "%.0f°");
         }
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
            SliderRow(T(AdjAmountLabel(l->adjType)), &l->adjAmt, lo, hi, fmt);
            int drive = l->adjDrive;
            if (ComboRow(T("Drive"), &drive, { T("None"), "A", "B", "C", "D" }))
               l->adjDrive = drive;
            if (l->adjDrive > 0)
               SliderRow(T("To"), &l->adjTo, lo, hi, fmt);
            AdjExtras(*l, l->id, doc);
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
         if (ImGui::InputTextMultiline("##v2dtexttab", buf, sizeof(buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * tok::view2d_layer_tab_ratio)))
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
            // Four chips each, filling the field column so both rows end where the wells above and below do.
            const float chipW = PanelKit::StripW(sFieldW, tok::view2d_chip_w_strip_w_n, 0);
            FieldLabel(T("Style"), sRowH);
            if (TextStyleChips(&l->bold, &l->italic, &l->underline, &l->caps, undo, next, chipW))
               Touch();
            FieldLabel(T("Align"), sChip);
            static const char* kGlyphs[3] = { IconsInfinite::TextLeft, IconsInfinite::TextCenter, IconsInfinite::TextRight };
            static const char* kTips[3] = { "Left align text", "Centre text", "Right align text" };
            for (int i = 0; i < 4; i++)
            {
               if (i > 0)
                  next();
               const std::string id = std::string("##v2dtabal") + std::to_string(i);
               const float w = PanelKit::StripW(sFieldW, tok::view2d_chip_w_strip_w_n, i);
               if ((i < 3 ? IconChip(id.c_str(), kGlyphs[i], l->align == i, T(kTips[i]), w) : JustifyChip(id.c_str(), l->align == i, w)) &&
                   l->align != i)
               {
                  undo();
                  l->align = i;
                  Touch();
               }
            }
         }
         {
            Pairs g;
            ModNext(*l, Scene2DNode::kLfFontSize);
            SliderRow(T("Size"), &l->fontSize, params::view2d::font_size_lo, params::view2d::font_size_hi, "%.0f px");
            ModNext(*l, Scene2DNode::kLfLeading);
            SliderRow(T("Leading"), &l->leading, params::view2d::leading_lo, params::view2d::leading_hi, "%.2f");
            ModNext(*l, Scene2DNode::kLfTracking);
            SliderRow(T("Tracking"), &l->tracking, params::view2d::tracking_lo, params::view2d::tracking_hi, "%.0f");
         }
         ColorRow(T("Colour"), l->textC);
         DragRow(T("Box width"), &l->boxW, 1.0f, "%.0f px", params::view2d::box_w_lo, params::view2d::box_w_hi);
         // Character: lines, band, position and stretch.
         if (l->underline)
         {
            BoolRow(T("Underline colour"), &l->ulOwn);
            if (l->ulOwn)
               ColorRow(T("Underline"), l->ulC);
         }
         BoolRow(T("Strike"), &l->strike);
         if (l->strike)
         {
            BoolRow(T("Strike colour"), &l->stOwn);
            if (l->stOwn)
               ColorRow(T("Strike line"), l->stC);
         }
         BoolRow(T("Outline"), &l->outline);
         if (l->outline)
         {
            SliderRow(T("Outline width"), &l->outlineW, params::view2d::outline_w_lo, params::view2d::outline_w_hi, "%.1f px");
            ColorRow(T("Outline colour"), l->outlineC);
         }
         {
            Pairs g;
            BoolRow(T("No break"), &l->noBreak);
            BoolRow(T("Optical align"), &l->optical);
         }
         SliderRow(T("Offset X"), &l->offsetX, params::view2d::offset_x_lo, params::view2d::offset_x_hi, "%.0f px");
         {
            // OpenType language tag (e.g. tr, nl, sr-Latn); empty = none.
            char buf[24];
            std::snprintf(buf, sizeof buf, "%s", l->lang.c_str());
            FieldLabel(T("Language"));
            ImGui::SetNextItemWidth(sFieldW);
            FieldWell::PushStyle();
            if (ImGui::InputText("##v2dtlang", buf, sizeof buf, ImGuiInputTextFlags_CharsNoBlank))
            {
               l->lang = buf;
               Touch();
            }
            FieldWell::PopStyle();
            UndoOnActivate("Text language");
         }
         {
            Pairs g;
            BoolRow(T("Kerning"), &l->kerning);
            BoolRow(T("Highlight"), &l->highlight);
         }
         if (l->highlight)
            ColorRow(T("Band"), l->highlightC);
         ComboRow(T("Position"), &l->script, { T("Normal"), T("Superscript"), T("Subscript") });
         SliderRow(T("Baseline"), &l->baseShift, params::view2d::base_shift_lo, params::view2d::base_shift_hi, "%.0f px");
         {
            Pairs g;
            SliderRow(T("Width"), &l->textSX, params::view2d::text_sx_lo, params::view2d::text_sx_hi, "%.2f\xC3\x97");
            SliderRow(T("Height"), &l->textSY, params::view2d::text_sy_lo, params::view2d::text_sy_hi, "%.2f\xC3\x97");
         }
         // Paragraph: a new line starts one.
         {
            Pairs g;
            SliderRow(T("Indent left"), &l->indentL, params::view2d::indent_l_lo, params::view2d::indent_l_hi, "%.0f px");
            SliderRow(T("Indent right"), &l->indentR, params::view2d::indent_r_lo, params::view2d::indent_r_hi, "%.0f px");
         }
         SliderRow(T("First line"), &l->indentFirst, params::view2d::indent_first_lo, params::view2d::indent_first_hi, "%.0f px");
         {
            Pairs g;
            SliderRow(T("Above"), &l->spaceBefore, params::view2d::space_before_lo, params::view2d::space_before_hi, "%.0f px");
            SliderRow(T("Below"), &l->spaceAfter, params::view2d::space_after_lo, params::view2d::space_after_hi, "%.0f px");
         }
         FieldLabel("");
         if (TextChip(L("Convert to shapes##v2dtoshapes"), false, T("Outlines as a vector layer"), sFieldW))
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
            DragMulti(T("Position"), pos, fmts, params::view2d::position_speed, params::view2d::position_lo, params::view2d::position_hi, params::view2d::position_def, l, mf);
         }
         float bw = 1, bh = 1;
         doc->LayerBox(*l, bw, bh);
         float wpx = bw * l->sx, hpx = bh * l->sy;
         {
            float* const size[2] = { &wpx, &hpx };
            const char* const fmts[2] = { "W %.0f", "H %.0f" };
            const int c = DragMulti(T("Size"), size, fmts, params::view2d::size_speed, 1.0f);
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
            const int c = DragMulti(T("Scale"), pp, pf, params::view2d::scale_speed, params::view2d::scale_lo, params::view2d::scale_hi, params::view2d::scale_def, l, mf);
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
            const char* const fmts[3] = { "X %.0f°", "Y %.0f°", "Z %.0f°" };
            const int mf[3] = { Scene2DNode::kLfRotX, Scene2DNode::kLfRotY, Scene2DNode::kLfAngle };
            DragMulti(T("Rotate"), rot, fmts, params::view2d::rotate_speed, params::view2d::rotate_lo, params::view2d::rotate_hi, params::view2d::rotate_def, l, mf);
            float* const ori[3] = { &l->orient[0], &l->orient[1], &l->orient[2] };
            DragMulti(T("Orient"), ori, fmts, params::view2d::orient_speed, params::view2d::orient_lo, params::view2d::orient_hi, params::view2d::orient_def);
            DragRow(T("Anchor depth"), &l->az, 1.0f, "%.0f px");
         }
         else
         {
            ModNext(*l, Scene2DNode::kLfAngle);
            {
               Pairs g;
               DragRow(T("Angle"), &l->angle, params::view2d::angle_speed, "%.1f°", params::view2d::angle_lo, params::view2d::angle_hi);
               DragRow(T("Shear"), &l->shear, params::view2d::shear_speed, "%.1f°", params::view2d::shear_lo, params::view2d::shear_hi);
            }
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
         {
            Pairs g;
            BoolRow(T("Flip horizontal"), &l->flipX);
            BoolRow(T("Flip vertical"), &l->flipY);
         }
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
            l->shear = 0.0f;
            Touch();
         }

         SectionCard::Begin(T("Crop"));
         BoolRow(T("Crop layer"), &l->cropOn);
         if (l->cropOn)
         {
            Pairs g;
            DragRow(T("Left"), &l->crop[0], 1.0f, "%.0f px", 0.0f, bw);
            DragRow(T("Right"), &l->crop[2], 1.0f, "%.0f px", 0.0f, bw);
            DragRow(T("Top"), &l->crop[1], 1.0f, "%.0f px", 0.0f, bh);
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
            FormParts::Inline();
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
         NodeRow(T("Node"), &l->maskUid, true, "mask" + std::to_string(l->id));
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
   // ---- effect stack (Photoshop's smart filters) --------------------------------------------------------------
   // Node entries: a node the stack owns, kept off the canvas, run on the layer's picture.
   struct NodeEffectEntry
   {
      const char* label;
      const char* group;
      const char* type;       // factory name
      const char* category;
      bool animated;          // moves by itself: redraws every frame
      bool toMask;            // its alpha cuts the layer
      const char* helper = nullptr;      // a recipe: a second node wired into the effect's second input, left on the
      const char* helperCat = nullptr;   // canvas beside the document so it can be fed (audio) or set (palette file)
   };
   const NodeEffectEntry kNodeEffects[] = {
      { "Gaussian Blur", "Blur", "gaussianblur", "Effects", false, false },
      { "Box Blur", "Blur", "boxblur", "Effects", false, false },
      { "Motion Blur", "Blur", "motionblur", "Effects", false, false },
      { "Displace", "Distort", "displace", "Effects", false, false },
      { "Spectrum Displace", "Distort", "displace", "Effects", true, false, "Audio Texture", "Source" },
      { "Kaleidoscope", "Distort", "kaleidoscope", "Effects", false, false },
      { "Bloom", "Stylize", "bloom", "Effects", false, false },
      { "Audio Color Ramp", "Colour", "Audio Color Ramp", "Compositing", true, false },
      { "Predictive Coloring", "Colour", "Predictive Coloring", "Prediction", false, false },
      { "Map to Palette", "Colour", "lookup", "Compositing", false, false, "Palette", "Modulators" },
      { "Feedback", "Time", "Feedback", "Compositing", true, false },
      { "Trails", "Time", "Trails", "Compositing", true, false },
      { "Time Machine", "Time", "Time Machine", "Compositing", true, false },
      { "Optical Flow", "Time", "Optical Flow", "Compositing", true, false },
      { "Fluid", "Sim", "Fluid", "Compositing", true, false },
      { "Reaction Diffusion", "Sim", "Reaction Diffusion", "Compositing", true, false },
      { "Field Pixel", "Code", "FieldPixel", "Source", true, false },
      { "Remove Background", "Mask", "Remove Background", "Compositing", false, true },
   };
   const char* const kEffGroups[] = { "Adjust", "Blur", "Distort", "Stylize", "Colour", "Time", "Sim", "Code", "Mask" };

   struct EffAdd { int layer = 0; int native = -1; int node = -1; uint64_t doc = 0; } sEffAdd;
   // 1 up, 2 down, 3 delete, 4 move to `to`, 5 duplicate (native), 6 reset (native)
   struct EffOp { int layer = 0; int index = -1; int op = 0; uint64_t doc = 0; int to = -1; } sEffOp;

   // The shown top-level layers, bottom to top.
   std::vector<int> VisibleTop(Scene2DNode* doc)
   {
      std::vector<int> ids;
      for (int c : doc->ChildrenOf(0))
         if (const Layer* l = doc->FindLayer(c))
            if (doc->Shown(*l))
               ids.push_back(c);
      return ids;
   }

   // A new pixel layer holding `set` drawn as one picture (optionally cut to the selection), above `above`
   // (0: on top). 0 when nothing shows.
   int MergedLayer(Scene2DNode* doc, const std::vector<int>& set, bool cutToSelection, int above)
   {
      static int sStampFrame = 1 << 25;
      std::vector<uint8_t> rgba;
      const int W = doc->Doc().w, H = doc->Doc().h;
      if (set.empty() || !doc->RenderSet(set, ++sStampFrame, rgba) || rgba.size() < (size_t)W * H * 4)  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
         return 0;
      if (cutToSelection && doc->HasSelection())
      {
         const std::vector<uint8_t>& m = doc->SelectionPixels();
         if (m.size() == (size_t)W * H)
            for (size_t i = 0; i < m.size(); i++)
               rgba[i * 4 + 3] = (uint8_t)(rgba[i * 4 + 3] * m[i] / 255);
      }
      bool any = false;
      for (size_t i = 3; i < rgba.size() && !any; i += 4)
         any = rgba[i] != 0;
      if (!any)
         return 0;
      const int nid = doc->AddFileLayer(0, above != 0 ? "Stamp" : "Merged", 0, 0, W, H, rgba, above);
      doc->Commit();
      return nid;
   }

   // What this view last put on the system clipboard, so a paste of our own copy keeps its place and layers.
   size_t sOsClipHash = 0;

   // A pixel layer's picture onto the system clipboard as a PNG cropped to what shows, so other apps paste it.
   // Never during self-tests (the user's clipboard is not ours to change).
   void ClipToOs(Scene2DNode* doc, const std::vector<int>& set)
   {
      if (std::getenv("INFINITE_VIEW2DTEST") != nullptr || std::getenv("INFINITE_SELFTEST") != nullptr)
         return;
      static int sClipFrame = 1 << 26;
      std::vector<uint8_t> rgba;
      const int W = doc->Doc().w, H = doc->Doc().h;
      if (!doc->RenderSet(set, ++sClipFrame, rgba) || rgba.size() < (size_t)W * H * 4)  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
         return;
      int x0 = W, y0 = H, x1 = -1, y1 = -1;
      for (int y = 0; y < H; y++)
         for (int x = 0; x < W; x++)
            if (rgba[((size_t)y * W + x) * 4 + 3] != 0)
               x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
      if (x1 < x0)
         return;
      const int w = x1 - x0 + 1, h = y1 - y0 + 1;
      std::vector<uint8_t> crop((size_t)w * h * 4);  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
      for (int y = 0; y < h; y++)
         std::memcpy(&crop[(size_t)y * w * 4], &rgba[((size_t)(y0 + y) * W + x0) * 4], (size_t)w * 4);
      std::vector<uint8_t> png;
      stbi_write_png_to_func([](void* ctx, void* data, int size) {
         auto* out = static_cast<std::vector<uint8_t>*>(ctx);
         out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
      }, &png, w, h, 4, crop.data(), w * 4);
      if (Platform::SetClipboardImage(png, crop, w, h))
         sOsClipHash = std::hash<std::string>()(std::string(png.begin(), png.end()));
   }

   // A picture another app copied pastes as a new pixel layer, centred (Photoshop). False when the clipboard holds
   // no picture, or holds the one this view put there (then the layer clip pastes, in place).
   bool PasteFromOs(Scene2DNode* doc)
   {
      if (std::getenv("INFINITE_VIEW2DTEST") != nullptr || std::getenv("INFINITE_SELFTEST") != nullptr)
         return false;
      std::vector<unsigned char> png, rgba;
      int w = 0, h = 0;
      if (!Platform::ClipboardImage(png, rgba, w, h))
         return false;
      if (!png.empty())
      {
         if (std::hash<std::string>()(std::string(png.begin(), png.end())) == sOsClipHash && !sClip.empty())
            return false;
         int ch = 0;
         unsigned char* img = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &ch, 4);
         if (img == nullptr)
            return false;
         rgba.assign(img, img + (size_t)w * h * 4);
         stbi_image_free(img);
      }
      if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4)  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
         return false;
      Layer* a = Active(doc);
      PushUndoCheckpoint("Paste");
      const int x = (doc->Doc().w - w) / tok::view2d_infinite_view2_dtest_scale, y = (doc->Doc().h - h) / tok::view2d_infinite_view2_dtest_scale;
      const int nid = doc->AddFileLayer(0, "Pasted", x, y, w, h, rgba, a != nullptr ? a->id : 0);
      doc->Commit();
      SelectOnly(nid);
      Touch();
      return true;
   }

   // Spawns an entry's node and its recipe helper on the canvas, beside the document's node (the helper wired into
   // the second input); 0 on failure. Nothing the 2D View makes runs hidden.
   uint64_t SpawnEffectNode(const NodeEffectEntry& ne, float x, float y)
   {
      if (x == 0.0f && y == 0.0f)
         if (GraphNode* host = DocGraphNode())
            x = host->liveX - tok::view2d_x_span, y = host->liveY + tok::view2d_y_span;
      GraphNode* gn = SpawnNode(ne.type, ne.category, x, y);   // checkpoints the undo itself
      if (gn == nullptr)
         return 0;
      // Field Pixel starts as a UV generator with no image input pin; seed it with an image-input pass-through
      // kernel so DeclaredImageInput(0) exists immediately when attached as a 2D View effect.
      if (auto* fp = dynamic_cast<FieldPixelNode*>(gn->node.get()))
      {
         fp->code = "input pixel image img;\ncol = img.rgb;\nalpha = img.a;";
         fp->CookIfNeeded(-1);
      }
      const uint64_t uid = gn->uid;
      if (ne.helper != nullptr)
         if (GraphNode* h = SpawnNode(ne.helper, ne.helperCat, x - 260.0f, y))   // can move gNodes: find the effect again
         {
            const int hi = h->index;
            if (GraphNode* again = FindNodeByUid(uid))
            {
               std::string err;
               ConnectNodes(hi, 0, again->index, 1, err);
            }
         }
      return uid;
   }

   // A param name as a label: "edgeSoftness" -> "Edge softness" (with short labels for verbose names).
   std::string ParamLabel(const char* name)
   {
      if (std::strcmp(name, "blendMode") == 0) return "Blend";
      if (std::strcmp(name, "delayFrames") == 0) return "Delay";
      std::string out;
      for (const char* c = name; *c; c++)
      {
         if (std::isupper((unsigned char)*c) && !out.empty())
         {
            out += ' ';
            out += (char)std::tolower((unsigned char)*c);
         }
         else
            out += *c == '_' ? ' ' : *c;
      }
      if (!out.empty())
         out[0] = (char)std::toupper((unsigned char)out[0]);
      return out;
   }

   // How a node effect's param reads in the 2D panel: a short label, its real range and choices, and whether it waits
   // behind More (tier 1) or is never shown (tier 2: internal, or drawn as its own control). Names and ranges follow
   // the node's own canvas body, so the panel and the node card agree. A param with no entry keeps the generic row.
   using EffNames = const std::vector<std::string>& (*)();
   const std::vector<std::string>& EffResNames() { static const std::vector<std::string> v = { "Full", "Half", "Quarter" }; return v; }
   const std::vector<std::string>& EffTrailBlends() { static const std::vector<std::string> v = { "Max", "Add", "Screen" }; return v; }
   const std::vector<std::string>& EffFlowViews() { static const std::vector<std::string> v = { "Vectors", "Colour" }; return v; }
   const std::vector<std::string>& EffPixelDomains() { static const std::vector<std::string> v = { "Image", "Material" }; return v; }
   struct EffHint
   {
      const char* type;
      const char* param;
      const char* label;
      float lo, hi;
      const char* fmt;
      EffNames opts;
      int tier;
   };
   const EffHint kEffHints[] = {
      { "Trails", "blendMode", "Blend", 0, 0, nullptr, EffTrailBlends, 0 },
      { "Trails", "decay", "Decay", 0.5f, 0.999f, "%.3f", nullptr, 0 },
      { "Trails", "zoom", "Zoom", 0.9f, 1.1f, "%.3f", nullptr, 0 },
      { "Trails", "rotate", "Rotate", -5.7296f, 5.7296f, "%.1f\xC2\xB0", nullptr, 0 },
      { "Trails", "hueShift", "Hue shift", -0.05f, 0.05f, "%.3f", nullptr, 0 },
      { "Trails", "driftX", "Drift X", -0.02f, 0.02f, "%.3f", nullptr, 1 },
      { "Trails", "driftY", "Drift Y", -0.02f, 0.02f, "%.3f", nullptr, 1 },
      { "Feedback", "delayFrames", "", 0, 0, nullptr, nullptr, 2 },
      { "Time Machine", "length", "Length", 2.0f, (float)TimeMachineNode::kMaxLength, "%.0f fr", nullptr, 0 },
      { "Time Machine", "invert", "Invert", 0, 0, nullptr, nullptr, 0 },
      { "Time Machine", "resolution", "Quality", 0, 0, nullptr, EffResNames, 1 },
      { "Optical Flow", "view", "View", 0, 0, nullptr, EffFlowViews, 0 },
      { "Optical Flow", "gain", "Gain", 0.0f, 8.0f, "%.2f", nullptr, 0 },
      { "Optical Flow", "resolution", "Quality", 0, 0, nullptr, EffResNames, 1 },
      { "Optical Flow", "levels", "Levels", 1.0f, (float)OpticalFlowNode::kMaxLevels, "%.0f", nullptr, 1 },
      { "Optical Flow", "window", "Window", 1.0f, 6.0f, "%.0f", nullptr, 1 },
      { "Optical Flow", "iterations", "Passes", 1.0f, 6.0f, "%.0f", nullptr, 1 },
      { "Fluid", "view", "View", 0, 0, nullptr, &FluidNode::ViewNames, 0 },
      { "Fluid", "stirrer", "Stirrer", 0, 0, nullptr, &FluidNode::StirrerNames, 0 },
      { "Fluid", "stirrerStrength", "Stir", 0.0f, 4.0f, "%.2f", nullptr, 0 },
      { "Fluid", "stirrerSize", "Stir size", 0.01f, 0.3f, "%.3f", nullptr, 0 },
      { "Fluid", "forceGain", "Force", 0.0f, 8.0f, "%.2f", nullptr, 0 },
      { "Fluid", "vorticity", "Swirl", 0.0f, 2.0f, "%.2f", nullptr, 0 },
      { "Fluid", "dyeFade", "Fade", 0.0f, 0.1f, "%.3f", nullptr, 0 },
      { "Fluid", "refresh", "Refresh", 0.0f, 1.0f, "%.3f", nullptr, 0 },
      { "Fluid", "viscosity", "Viscosity", 0.0f, 0.2f, "%.3f", nullptr, 1 },
      { "Fluid", "velocityDamping", "Damping", 0.0f, 0.1f, "%.3f", nullptr, 1 },
      { "Fluid", "resolution", "Quality", 0, 0, nullptr, EffResNames, 1 },
      { "Fluid", "pressureIterations", "Solver", 1.0f, (float)FluidNode::kMaxPressureIterations, "%.0f", nullptr, 1 },
      { "Reaction Diffusion", "preset", "Preset", 0, 0, nullptr, &ReactionDiffusionNode::PresetNames, 0 },
      { "Reaction Diffusion", "feed", "Feed", 0.01f, 0.09f, "%.4f", nullptr, 0 },
      { "Reaction Diffusion", "kill", "Kill", 0.03f, 0.08f, "%.4f", nullptr, 0 },
      { "Reaction Diffusion", "stepsPerFrame", "Speed", 1.0f, 32.0f, "%.0f", nullptr, 0 },
      { "Reaction Diffusion", "sourceInfluence", "Source", 0.0f, 1.0f, "%.2f", nullptr, 0 },
      { "Reaction Diffusion", "lowColor", "Low", 0, 0, nullptr, nullptr, 0 },
      { "Reaction Diffusion", "highColor", "High", 0, 0, nullptr, nullptr, 0 },
      { "Reaction Diffusion", "diffuseA", "Diffuse A", 0.2f, 1.5f, "%.2f", nullptr, 1 },
      { "Reaction Diffusion", "diffuseB", "Diffuse B", 0.1f, 1.0f, "%.2f", nullptr, 1 },
      { "Reaction Diffusion", "width", "Width", 64.0f, 2048.0f, "%.0f px", nullptr, 1 },
      { "Reaction Diffusion", "height", "Height", 64.0f, 2048.0f, "%.0f px", nullptr, 1 },
      { "Remove Background", "mode", "Detect", 0, 0, nullptr, &RemoveBgNode::ModeNames, 0 },
      { "Remove Background", "outputMode", "Output", 0, 0, nullptr, &RemoveBgNode::OutputModeNames, 0 },
      { "Remove Background", "feather", "Feather", 0.0f, 4.0f, "%.2f", nullptr, 0 },
      { "Remove Background", "threshold", "Threshold", 0.0f, 1.0f, "%.2f", nullptr, 0 },
      { "Remove Background", "contrast", "Edge", 0.5f, 8.0f, "%.2f", nullptr, 0 },
      { "Remove Background", "bgColor", "Backdrop", 0, 0, nullptr, nullptr, 1 },
      { "Remove Background", "bgOpacity", "Back opacity", 0.0f, 1.0f, "%.2f", nullptr, 1 },
      { "Remove Background", "autoRefresh", "Live", 0, 0, nullptr, nullptr, 1 },
      { "Remove Background", "refreshFrames", "Every", 1.0f, 120.0f, "%.0f fr", nullptr, 1 },
      { "Remove Background", "maskActive", "", 0, 0, nullptr, nullptr, 2 },
      { "Predictive Coloring", "mix", "Mix", 0.0f, 1.0f, "%.2f", nullptr, 0 },
      { "Predictive Coloring", "wander", "Wander", 0.0f, 1.0f, "%.2f", nullptr, 0 },
      { "Predictive Coloring", "selfNormalize", "Normalize", 0, 0, nullptr, nullptr, 1 },
      { "Predictive Coloring", "seed", "Seed", 0, 0, nullptr, nullptr, 1 },
      { "Predictive Coloring", "learning", "", 0, 0, nullptr, nullptr, 2 },
      { "Audio Color Ramp", "mode", "Mode", 0, 0, nullptr, &AudioColorRampNode::ModeNames, 0 },
      { "Audio Color Ramp", "interpMode", "Blend", 0, 0, nullptr, &AudioColorRampNode::InterpNames, 0 },
      { "Audio Color Ramp", "bandCount", "Bands", 2.0f, (float)AudioColorRampNode::kMaxBands, "%.0f", nullptr, 0 },
      { "Audio Color Ramp", "gain", "Gain", 0.1f, 10.0f, "%.2f", nullptr, 0 },
      { "Audio Color Ramp", "minBrightness", "Min glow", 0.0f, 1.0f, "%.2f", nullptr, 0 },
      { "FieldPixel", "animate", "Animate", 0, 0, nullptr, nullptr, 0 },
      { "FieldPixel", "width", "Width", 16.0f, 4096.0f, "%.0f px", nullptr, 1 },
      { "FieldPixel", "height", "Height", 16.0f, 4096.0f, "%.0f px", nullptr, 1 },
      { "FieldPixel", "domain", "Domain", 0, 0, nullptr, EffPixelDomains, 1 },
      { "FieldPixel", "exposeAuxTexture", "", 0, 0, nullptr, nullptr, 2 },
   };
   const EffHint* FindEffHint(const std::string& type, const char* param)
   {
      for (const EffHint& h : kEffHints)
         if (type == h.type && std::strcmp(param, h.param) == 0)
            return &h;
      return nullptr;
   }

   // The owned node's own params (its full body, with modulation, is one "Show on canvas" away). Drawn in two passes:
   // tier 0 first, then the tier 1 rows under one More fold. `hasMore` reports whether a second pass has anything.
   // Rows drawn from data (node params) each get their own ID scope, so two rows with the same label in one window
   // never share an ImGui ID (a node's "Blend" next to the Mix "Blend").
   struct ParamIdScope
   {
      explicit ParamIdScope(const void* key) { ImGui::PushID(key); }
      explicit ParamIdScope(const char* key) { ImGui::PushID(key); }
      explicit ParamIdScope(int key) { ImGui::PushID(key); }
      ~ParamIdScope() { ImGui::PopID(); }
   };

   class EffParamRows : public ParamVisitor
   {
   public:
      EffParamRows(GraphNode& gn, int tier) : mGn(gn), mType(gn.typeName), mTier(tier) {}
      bool hasMore = false;

      void Float(const char* name, float& v) override
      {
         const ParamIdScope ids(name);   // one ID scope per param: a node param can share a label with a Mix row
         int band = -1;
         const EffHint* h = Hint(name, band);
         if (!Shown(h, name, band))
            return;
         if (band >= 0)   // Audio Color Ramp's per-band gain, behind More
         {
            SliderRow((std::string(T("Gain")) + " " + std::to_string(band + 1)).c_str(), &v, params::view2d::gain_lo, params::view2d::gain_hi, "%.2f");
            return;
         }
         if (h != nullptr && h->hi > h->lo)
         {
            SliderRow(T(h->label), &v, h->lo, h->hi, h->fmt != nullptr ? h->fmt : "%.2f");
            return;
         }
         const std::string label = h != nullptr ? std::string(T(h->label)) : ParamLabel(name);
         FieldLabel(label.c_str());
         ImGui::SetNextItemWidth(sFieldW);
         FieldWell::PushStyle();
         const float speed = std::max(0.001f, std::fabs(v) * 0.01f);
         // Plain digits sized to the value: 2120, 48.5, 0.25 (never 2.12e+03).
         const float av = std::fabs(v);
         const char* fmt = av >= 1000.0f ? "%.0f" : av >= 100.0f ? "%.1f" : av >= 1.0f || av == 0.0f ? "%.2f" : "%.3f";
         if (ImGui::DragFloat(FormParts::Id(label.c_str()).c_str(), &v, speed, 0.0f, 0.0f, fmt))
            Touch();
         FieldWell::PopStyle();
         UndoOnActivate("Effect");
      }
      void Int(const char* name, int& v) override
      {
         const ParamIdScope ids(name);   // one ID scope per param: a node param can share a label with a Mix row
         int band = -1;
         const EffHint* h = Hint(name, band);
         if (!Shown(h, name, band))
            return;
         if (h != nullptr && h->opts != nullptr && !h->opts().empty())
         {
            std::vector<std::string> opts;
            for (const std::string& o : h->opts())
               opts.push_back(T(o.c_str()));
            int nv = v;
            if (ComboRow(T(h->label), &nv, opts))
            {
               if (auto* rd = dynamic_cast<ReactionDiffusionNode*>(mGn.node.get()); rd != nullptr && &v == &rd->preset)
                  rd->ApplyPreset(nv);
               else
                  v = nv;
               if (auto* acr = dynamic_cast<AudioColorRampNode*>(mGn.node.get()))
                  acr->MarkDirty();
            }
            return;
         }
         if (h != nullptr && h->hi > h->lo)
         {
            float f = (float)v;
            if (SliderRow(T(h->label), &f, h->lo, h->hi, h->fmt != nullptr ? h->fmt : "%.0f"))
            {
               const int nv = (int)std::lround(f);
               if (auto* acr = dynamic_cast<AudioColorRampNode*>(mGn.node.get()); acr != nullptr && &v == &acr->bandCount)
                  acr->SetBandCount(nv);
               else
                  v = nv;
            }
            return;
         }
         if (IntRow(h != nullptr ? T(h->label) : ParamLabel(name).c_str(), &v))
            Touch();
         UndoOnActivate("Effect");
      }
      void Bool(const char* name, bool& v) override
      {
         const ParamIdScope ids(name);   // one ID scope per param: a node param can share a label with a Mix row
         int band = -1;
         const EffHint* h = Hint(name, band);
         if (!Shown(h, name, band))
            return;
         BoolRow(h != nullptr ? T(h->label) : ParamLabel(name).c_str(), &v);
      }
      void Text(const char*, std::string&) override {}
      void Color(const char* name, float rgb[3]) override
      {
         const ParamIdScope ids(name);   // one ID scope per param: a node param can share a label with a Mix row
         int band = -1;
         const EffHint* h = Hint(name, band);
         if (band >= 0)
         {
            // Audio Color Ramp's band colours read as one row of dots, low band on the left.
            if (mTier != 0 || band >= Bands())
               return;
            if (band == 0)
               FieldLabel(T("Colours"));
            else
               FormParts::Inline(0.0f, tok::space_1);
            float c[4] = { rgb[0], rgb[1], rgb[2], 1.0f };
            const float fh = ImGui::GetFrameHeight();
            if (FormParts::ColorDot(("##v2dacr" + std::to_string(band)).c_str(), c, 15.0f, fh))
            {
               std::copy(c, c + 3, rgb);
               if (auto* acr = dynamic_cast<AudioColorRampNode*>(mGn.node.get()))
                  acr->MarkDirty();
               Touch();
            }
            return;
         }
         if (!Shown(h, name, band))
            return;
         float c[4] = { rgb[0], rgb[1], rgb[2], 1.0f };
         if (ColorRow(h != nullptr ? T(h->label) : ParamLabel(name).c_str(), c))
            std::copy(c, c + 3, rgb);
      }

   private:
      GraphNode& mGn;
      const std::string& mType;
      int mTier;

      int Bands() const
      {
         if (auto* acr = dynamic_cast<AudioColorRampNode*>(mGn.node.get()))
            return std::clamp(acr->bandCount, 2, AudioColorRampNode::kMaxBands);
         return 0;
      }
      // Audio Color Ramp numbers its band params (cross0, color0, gain0): those map to a band, not to a table row.
      const EffHint* Hint(const char* name, int& band) const
      {
         band = -1;
         if (mType == "Audio Color Ramp")
            for (const char* pre : { "cross", "color", "gain" })
            {
               const size_t n = std::strlen(pre);
               if (std::strncmp(name, pre, n) == 0 && std::isdigit((unsigned char)name[n]))
               {
                  band = std::atoi(name + n);
                  if (pre[0] == 'c' && pre[1] == 'r')
                     band = -2;   // crossovers: dragged on the node's own band editor
                  return nullptr;
               }
            }
         return FindEffHint(mType, name);
      }
      // Whether this pass draws the row; a tier 1 row seen on the first pass asks for the More fold.
      bool Shown(const EffHint* h, const char*, int band)
      {
         if (band == -2)
            return false;
         int tier = h != nullptr ? h->tier : 0;
         if (band >= 0)
         {
            if (band >= Bands())
               return false;
            tier = 1;
         }
         if (tier == 2)
            return false;
         if (tier == 1 && mTier == 0)
            hasMore = true;
         return tier == mTier;
      }
   };

   // Opacity and blend, in the settings window. `blend` null = opacity only (a mask effect has no blend).
   void MixRow(float& opacity, int* blend)
   {
      // Its own ID scope: a node param can be called "Blend" or "Opacity" too (Trails), in the same window.
      const ParamIdScope ids("##mixrow");
      SliderRow(T("Opacity"), &opacity, 0.0f, 1.0f, "%.2f");
      if (blend == nullptr)
         return;
      std::vector<std::string> blends;
      for (const char* b : Scene2DNode::kEffBlendNames)
         blends.push_back(T(b));
      ComboRow(T("Blend"), blend, blends);
   }

   // A muted group title inside a settings window ("Mix", "More"): no fold, the window shows everything.
   void WinSection(const char* text)
   {
      Ui::Spacer(Ui::Space::S1);
      ImGui::TextDisabled("%s", text);
   }

   // Where a card's settings window first appears: left of the panel, level with the card's header.
   ImVec2 WinAt(float headerY) { return ImVec2(ImGui::GetWindowPos().x - tok::space_2, headerY); }

   ImVec2 sCardSpan[2];   // the last CardTitle's whole card (ReorderDrag bar extent)

   // The card title: click opens the settings window, drag (where the caller adds a source) reorders.
   // Returns the header rect's left top. `chips` = how many header icons follow on the right.
   ImVec2 CardTitle(const std::string& id, const std::string& title, bool on, int chips, const std::string& winKey, const char* tip)
   {
      const float fh = ImGui::GetFrameHeight();
      const float chipsW = chips * fh + std::max(0, chips - 1) * tok::space_1;
      const float nameW = std::max(1.0f, Ui::AvailW() - chipsW - tok::space_2);
      const ImVec2 np = ImGui::GetCursorScreenPos();
      // The whole card, for the drag-to-reorder bar: on box on the left, header icons on the right, card padding round.
      const float pad = SectionCard::Pad();
      sCardSpan[0] = ImVec2(np.x - CheckBox::Size() - tok::space_2 - pad, np.y - pad);
      sCardSpan[1] = ImVec2(np.x + Ui::AvailW() + pad, np.y + fh + pad);
      if (UiInteract::Hit(id.c_str(), ImVec2(nameW, fh)))
         SettingsWindow::Toggle(winKey);
      const bool hov = ImGui::IsItemHovered();
      if (hov && tip != nullptr)
         HelpTip("%s", tip);
      const ImU32 col = ImGui::GetColorU32(on ? ImGuiCol_Text : ImGuiCol_TextDisabled, hov || !on ? 1.0f : tok::view2d_card_title_color_alpha_mul);
      const std::string shown = app::TruncateWithEllipsis(title, std::max(16.0f, nameW - tok::space_0_5));
      ImGui::GetWindowDrawList()->AddText(ImVec2(np.x, np.y + (fh - ImGui::GetFontSize()) * 0.5f), col, shown.c_str());
      return np;
   }

   // Right-click on an effect card's header: its own menu (not the layer's).
   void EffectMenu(Layer& l, int i, const std::string& winKey)
   {
      const std::string pop = "##v2deffmenu" + std::to_string(l.id) + "_" + std::to_string(i);
      if (MenuParts::ContextClicked())
         ImGui::OpenPopup(pop.c_str());
      if (!MenuParts::BeginPopup(pop.c_str()))
         return;
      const Scene2DNode::Effect& e = l.effects[(size_t)i];
      const int n = (int)l.effects.size();
      if (MenuParts::Item(T("All Settings"), nullptr, SettingsWindow::IsOpen(winKey)))
         SettingsWindow::Toggle(winKey);
      if (e.uid != 0 && MenuParts::Item(T(CanvasLink::Label(e.shown ? CanvasLink::Verb::Hide : CanvasLink::Verb::Show)), nullptr, e.shown))
      {
         PushUndoCheckpoint("Show effect node");
         l.effects[(size_t)i].shown = !e.shown;
         Touch();
      }
      MenuParts::Separator();
      if (MenuParts::Item(T("Move Up"), nullptr, false, i > 0))
         sEffOp = { l.id, i, 1, sDocUid };
      if (MenuParts::Item(T("Move Down"), nullptr, false, i + 1 < n))
         sEffOp = { l.id, i, 2, sDocUid };
      if (e.uid == 0)
      {
         if (MenuParts::Item(T("Duplicate"), nullptr))
            sEffOp = { l.id, i, 5, sDocUid };
         if (MenuParts::Item(T("Reset"), nullptr))
            sEffOp = { l.id, i, 6, sDocUid };
      }
      MenuParts::Separator();
      if (MenuParts::Item(T("Remove Effect"), nullptr))
         sEffOp = { l.id, i, 3, sDocUid };
      MenuParts::EndPopup();
   }

   void EffectRows(Scene2DNode* doc, Layer& l, int i)
   {
      using S = Scene2DNode;
      S::Effect& e = l.effects[(size_t)i];
      const std::string name = e.uid != 0 ? e.entry : std::string(T(S::AdjName(e.adjType)));
      const std::string base = "##v2deff" + std::to_string(l.id) + "_" + std::to_string(i);
      // One header row: on box, name, gear (the 3D View grammar). The card shows
      // the effect's one or two everyday rows; the gear (or a click on the name) opens all of them in a window.
      const std::string cardKey = std::to_string(l.id) + "/" + (e.uid != 0 ? std::to_string(e.uid) : "a" + std::to_string(e.adjType));
      const std::string winKey = "v2deff/" + std::to_string(sDocUid) + "/" + cardKey;
      SectionCard::Begin();
      const float rowY = ImGui::GetCursorPosY();
      {
         Ui::CentreInRow centred(ImGui::GetFrameHeight(), CheckBox::Size());
         bool on = e.on;
         if (CheckBox::Box((base + "on").c_str(), &on))
         {
            PushUndoCheckpoint("Effect on");
            e.on = on;
            Touch();
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("On"));
         FormParts::Inline(0.0f, tok::space_2);
      }
      // The 3D View's card grammar: on box, name, gear. Show on canvas and Remove are on the right-click menu.
      const ImVec2 np = CardTitle(base + "name", name, e.on, 1, winKey, T("Click for all settings, drag to reorder, right-click for more"));
      // Drag the card to move the effect: the other cards slide aside live (the shared reorder motion).
      {
         const int pay[2] = { l.id, i };
         ReorderDrag::Item(i, name.c_str(), pay, sizeof(pay), sCardSpan);
      }
      EffectMenu(l, i, winKey);
      FormParts::Inline(0.0f, tok::space_2);
      if (HeaderIcon((base + "set").c_str(), IconsInfinite::Sliders, SettingsWindow::IsOpen(winKey), T("All settings"), rowY))
         SettingsWindow::Toggle(winKey);
      // The effect's controls, all in its settings window: every row, the rarely used ones under More, then Mix.
      // The card itself is the header alone.
      auto rows = [&]() {
         if (e.uid == 0)
         {
            float lo = 0, hi = 1;
            S::AdjRange(e.adjType, lo, hi);
            if (e.adjType != S::kAdjGradientMap)
            {
               SliderRow(T(AdjAmountLabel(e.adjType)), &e.adjAmt, lo, hi, AdjFmt(e.adjType));
               AdjExtras(e, 100000 + l.id * 64 + i, doc);
            }
            if (e.adjType == S::kAdjGradientMap || e.adjMap)
            {
               ColorRow(T("Shadows"), e.colorA);
               ColorRow(T("Highlights"), e.colorB);
            }
         }
         else
         {
            GraphNode* gn = FindNodeByUid(e.uid);
            if (gn == nullptr)
               FieldLabel(T("Node gone"));
            else
            {
               if (auto* fn = dynamic_cast<FilterNode*>(gn->node.get()))
               {
                  const FilterDef& def = fn->Def();
                  // Linear light is a rendering choice, not a look: it waits under More.
                  auto rare = [](const FilterParamDef& pd) { return pd.label == "linear light"; };
                  auto row = [&](size_t pi) {
                     const ParamIdScope ids((int)pi);
                     const FilterParamDef& pd = def.params[pi];
                     const std::string plabel = ParamLabel(pd.label.c_str());
                     float* pv = fn->ParamPtr(pi);
                     if (pd.type == FilterParamDef::Type::Float)
                     {
                        // Angles read in degrees everywhere in the panel; a filter that stores radians converts here.
                        if (pd.format.find("rad") != std::string::npos)
                        {
                           float deg = *pv * 57.29578f;
                           if (SliderRow(T(plabel.c_str()), &deg, pd.minVal * 57.29578f, pd.maxVal * 57.29578f, "%.0f\xC2\xB0"))   // ui-audit-ok: unit (radians to degrees)
                              *pv = deg / 57.29578f;
                           return;
                        }
                        const char* fmt = !pd.format.empty() ? pd.format.c_str() : (pd.integer ? "%.0f" : "%.2f");
                        if (SliderRow(T(plabel.c_str()), pv, pd.minVal, pd.maxVal, fmt) && pd.integer)
                           *pv = std::round(*pv);
                     }
                     else if (pd.type == FilterParamDef::Type::Int)
                     {
                        if (SliderRow(T(plabel.c_str()), pv, pd.minVal, pd.maxVal, "%.0f"))
                           *pv = std::round(*pv);
                     }
                     else if (pd.type == FilterParamDef::Type::Bool)
                     {
                        bool bv = *pv > 0.5f;
                        if (BoolRow(T(plabel.c_str()), &bv))
                           *pv = bv ? 1.0f : 0.0f;
                     }
                     else if (pd.type == FilterParamDef::Type::Enum && !pd.options.empty())
                     {
                        int ev = (int)std::lround(*pv);
                        std::vector<std::string> opts;
                        for (const std::string& o : pd.options)
                           opts.push_back(T(o.c_str()));
                        if (ComboRow(T(plabel.c_str()), &ev, opts))
                           *pv = (float)ev;
                     }
                     else if (pd.type == FilterParamDef::Type::Color)
                     {
                        float c[4] = { pv[0], pv[1], pv[2], 1.0f };
                        if (ColorRow(T(plabel.c_str()), c))
                           std::copy(c, c + 3, pv);
                     }
                  };
                  bool anyRare = false;
                  for (size_t pi = 0; pi < def.params.size(); pi++)
                     if (rare(def.params[pi]))
                        anyRare = true;
                     else
                        row(pi);
                  if (anyRare)
                  {
                     WinSection(T("More"));
                     for (size_t pi = 0; pi < def.params.size(); pi++)
                        if (rare(def.params[pi]))
                           row(pi);
                  }
               }
               else
               {
                  EffParamRows r(*gn, 0);
                  gn->node->VisitParams(r);
                  if (auto* pc = dynamic_cast<PredictiveColoringNode*>(gn->node.get()))
                  {
                     const bool learning = pc->IsLearning();
                     const char* learnLabel = learning ? T("Stop learning") : pc->TotalSamples() > 0 ? T("Learn again") : T("Learn");
                     if (ActionButton::Draw((std::string(learnLabel) + base + "learn").c_str(), ImVec2(-1.0f, 0.0f), ActionButton::Kind::Plain))
                     {
                        PushUndoCheckpoint("Learn");
                        pc->SetLearning(!learning);
                        Touch();
                     }
                  }
                  if (r.hasMore)
                  {
                     WinSection(T("More"));
                     EffParamRows more(*gn, 1);
                     gn->node->VisitParams(more);
                  }
               }
               if (auto* rbg = dynamic_cast<RemoveBgNode*>(gn->node.get()))
               {
                  if (ActionButton::Draw((std::string(T("Remove Background")) + base + "rbg").c_str(), ImVec2(-1.0f, 0.0f),
                                         ActionButton::Kind::Plain))
                  {
                     PushUndoCheckpoint("Remove Background");
                     rbg->RequestMask();
                     Touch();
                  }
                  ImGui::TextDisabled("%s", rbg->Status().c_str());
               }
            }
         }
         WinSection(T("Mix"));
         MixRow(e.opacity, e.toMask ? nullptr : &e.blend);
      };
      SettingsWindow::Draw(winKey, name.c_str(), WinAt(np.y), [&]() {
         MoreRows mr;
         rows();
      });
   }

   bool FilterMatch(const char* haystack, const char* needle)
   {
      if (needle == nullptr || needle[0] == 0)
         return true;
      std::string h(haystack), q(needle);
      for (char& ch : h) ch = (char)std::tolower((unsigned char)ch);
      for (char& ch : q) ch = (char)std::tolower((unsigned char)ch);
      return h.find(q) != std::string::npos;
   }

   void AddEffectMenu(Layer& l, float btnW = -1.0f)
   {
      using S = Scene2DNode;
      static char sEffSearch[64] = "";
      static ImVec2 sEffPopPos(0, 0);
      // Layer styles sit in the same menu (group "Style"); one already on adds a second of its kind.
      const bool styles = l.kind != Kind::Group;
      auto addStyle = [&](int t) {
         PushUndoCheckpoint("Add layer style");
         if (!l.fx[t].on)
            l.fx[t].on = true;
         else
         {
            Scene2DNode::Fx g = l.fx[t];
            l.fxMore.emplace_back(t, g);
         }
         Touch();
      };
      if (ActionButton::Draw((std::string(T("Add")) + "##v2deffadd").c_str(), ImVec2(btnW, 0.0f), ActionButton::Kind::Plain))
      {
         sEffSearch[0] = 0;
         sEffPopPos = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + tok::space_1);
         ImGui::OpenPopup("##v2deffpop");
      }
      ImGui::SetNextWindowPos(sEffPopPos, ImGuiCond_Appearing);
      if (!MenuParts::BeginPopup("##v2deffpop"))
         return;
      ImGui::SetNextItemWidth(sFieldW);
      if (ImGui::IsWindowAppearing())
         UiInteract::FocusHere();
      FieldWell::InputText("##v2deffq", sEffSearch, sizeof sEffSearch);
      MenuParts::Separator();
      if (sEffSearch[0] != 0)
      {
         bool any = false;
         for (int t = 0; t < S::kAdjCount; t++)
            if (FilterMatch(T(S::AdjName(t)), sEffSearch) || FilterMatch(S::AdjGroup(t), sEffSearch))
            {
               any = true;
               if (MenuParts::Item(T(S::AdjName(t)), T(S::AdjGroup(t)), false, true))
                  sEffAdd = { l.id, t, -1, sDocUid };
            }
         for (int k = 0; k < (int)(sizeof(kNodeEffects) / sizeof(kNodeEffects[0])); k++)
            if (FilterMatch(T(kNodeEffects[k].label), sEffSearch) || FilterMatch(kNodeEffects[k].group, sEffSearch))
            {
               any = true;
               if (MenuParts::Item(T(kNodeEffects[k].label), T(kNodeEffects[k].group), false, true))
                  sEffAdd = { l.id, -1, k, sDocUid };
            }
         for (int t = 0; styles && t < S::kFxCount; t++)
            if (FilterMatch(T(S::FxName(t)), sEffSearch) || FilterMatch("Style", sEffSearch))
            {
               any = true;
               if (MenuParts::Item((std::string(T(S::FxName(t))) + "##v2dfxq" + std::to_string(t)).c_str(), T("Style"), false, true))
                  addStyle(t);
            }
         if (!any)
            ImGui::TextDisabled("%s", T("No matches"));
      }
      else
      {
         for (const char* g : kEffGroups)
         {
            if (!MenuParts::SubMenu(T(g), true))
               continue;
            for (int t = 0; t < S::kAdjCount; t++)
               if (std::strcmp(S::AdjGroup(t), g) == 0 && MenuParts::Item(T(S::AdjName(t)), nullptr, false, true))
                  sEffAdd = { l.id, t, -1, sDocUid };
            for (int k = 0; k < (int)(sizeof(kNodeEffects) / sizeof(kNodeEffects[0])); k++)
               if (std::strcmp(kNodeEffects[k].group, g) == 0 && MenuParts::Item(T(kNodeEffects[k].label), nullptr, false, true))
                  sEffAdd = { l.id, -1, k, sDocUid };
            ImGui::EndMenu();
         }
         if (styles && MenuParts::SubMenu(T("Style"), true))
         {
            for (int t = 0; t < S::kFxCount; t++)
               if (MenuParts::Item((std::string(T(S::FxName(t))) + "##v2dfxm" + std::to_string(t)).c_str(), nullptr, false, true))
                  addStyle(t);
            ImGui::EndMenu();
         }
      }
      MenuParts::EndPopup();
   }

   std::string EffPresetDir() { return AppPaths::AppSupportDir() + "/2dfx"; }
   std::string StylePresetDir() { return AppPaths::AppSupportDir() + "/2dstyle"; }

   // A node effect's params in a preset: one "name value" line each (texts with newlines escaped), set back by name.
   class ParamLines : public ParamVisitor
   {
   public:
      std::ostringstream o;
      void Float(const char* n, float& v) override { o << n << " " << v << "\n"; }
      void Int(const char* n, int& v) override { o << n << " " << v << "\n"; }
      void Bool(const char* n, bool& v) override { o << n << " " << +v << "\n"; }
      void Color(const char* n, float c[3]) override { o << n << " " << c[0] << " " << c[1] << " " << c[2] << "\n"; }
      void Text(const char* n, std::string& v) override
      {
         std::string t;
         for (char c : v)
            t += c == '\n' ? std::string("\\n") : c == '\\' ? std::string("\\\\") : std::string(1, c);
         o << n << " " << t << "\n";
      }
   };
   class ParamSet : public ParamVisitor
   {
   public:
      std::map<std::string, std::string> v;
      bool Has(const char* n, std::istringstream& s)
      {
         const auto it = v.find(n);
         if (it == v.end())
            return false;
         s.str(it->second);
         return true;
      }
      void Float(const char* n, float& x) override { std::istringstream s; if (Has(n, s)) s >> x; }
      void Int(const char* n, int& x) override { std::istringstream s; if (Has(n, s)) s >> x; }
      void Bool(const char* n, bool& x) override { std::istringstream s; int b = x; if (Has(n, s) && (s >> b)) x = b != 0; }
      void Color(const char* n, float c[3]) override { std::istringstream s; if (Has(n, s)) s >> c[0] >> c[1] >> c[2]; }
      void Text(const char* n, std::string& x) override
      {
         const auto it = v.find(n);
         if (it == v.end())
            return;
         std::string t;
         for (size_t i = 0; i < it->second.size(); i++)
            if (it->second[i] == '\\' && i + 1 < it->second.size())
               t += it->second[++i] == 'n' ? '\n' : it->second[i];
            else
               t += it->second[i];
         x = t;
      }
   };
   // "--node <effect index>" then its param lines, after the stack text.
   std::string EffectParamsText(const Layer& l)
   {
      std::string out;
      for (size_t i = 0; i < l.effects.size(); i++)
         if (l.effects[i].uid != 0)
            if (GraphNode* gn = FindNodeByUid(l.effects[i].uid))
               if (gn->node)
               {
                  ParamLines pl;
                  gn->node->VisitParams(pl);
                  out += "--node " + std::to_string(i) + "\n" + pl.o.str();
               }
      return out;
   }
   std::map<int, ParamSet> ParseEffectParams(const std::string& text)
   {
      std::map<int, ParamSet> out;
      std::istringstream s(text);
      std::string line;
      int at = -1;
      while (std::getline(s, line))
      {
         if (line.rfind("--node ", 0) == 0)
         {
            at = std::atoi(line.c_str() + 7);
            continue;
         }
         const size_t sp = line.find(' ');
         if (at >= 0 && sp != std::string::npos)
            out[at].v[line.substr(0, sp)] = line.substr(sp + 1);
      }
      return out;
   }

   std::string sEffClipText, sEffClipParams;
   bool HasCopiedEffects() { return !sEffClipText.empty(); }
   void CopyLayerEffects(const Layer& l)
   {
      sEffClipText = Scene2DNode::EffectsText(l);
      sEffClipParams = EffectParamsText(l);
   }
   void PasteLayerEffects(Scene2DNode* doc, int id)
   {
      Layer* cur = doc != nullptr ? doc->FindLayer(id) : nullptr;
      if (cur == nullptr || sEffClipText.empty())
         return;
      Layer got = *cur;
      if (!Scene2DNode::ParseEffects(sEffClipText, got))
         return;
      std::map<int, ParamSet> sets = ParseEffectParams(sEffClipParams);
      std::vector<Scene2DNode::Effect> keep;
      for (int gi = 0; gi < (int)got.effects.size(); gi++)
      {
         Scene2DNode::Effect& ef = got.effects[(size_t)gi];
         if (ef.uid == 0)
         {
            keep.push_back(ef);
            continue;
         }
         for (const NodeEffectEntry& ne : kNodeEffects)
            if (ef.entry == ne.label)
               if (const uint64_t uid = SpawnEffectNode(ne, 0.0f, 0.0f))
               {
                  const auto ps = sets.find(gi);
                  if (ps != sets.end())
                     if (GraphNode* gn = FindNodeByUid(uid))
                        if (gn->node)
                           gn->node->VisitParams(ps->second);
                  ef.uid = uid;
                  ef.shown = false;
                  keep.push_back(ef);
               }
      }
      if (Layer* target = doc->FindLayer(id))
      {
         PushUndoCheckpoint("Paste effects");
         for (Scene2DNode::Effect& ef : keep)
            target->effects.push_back(ef);
         Touch();
      }
   }

   void EffectPresets(Layer& l, float btnW = -1.0f)
   {
      static char name[64] = "";
      static std::vector<std::string> sFiles;
      auto rescan = [&]()
      {
         sFiles.clear();
         std::error_code ec;
         for (const auto& de : std::filesystem::directory_iterator(EffPresetDir(), ec))
            if (de.path().extension() == ".inf2dfx")
               sFiles.push_back(de.path().stem().string());
         // Style presets saved before stacks held styles: listed too, they apply as a stack with styles only.
         for (const auto& de : std::filesystem::directory_iterator(StylePresetDir(), ec))
            if (de.path().extension() == ".inf2dstyle")
               sFiles.push_back(de.path().stem().string() + "\x1f");
         std::sort(sFiles.begin(), sFiles.end());
      };
      // Presets are their own window (the settings-window recipe: a title, a close button, it opens beside the
      // panel at the button and stays while you try presets on the canvas), not a popup hanging off the button.
      const std::string winKey = "v2dpre/" + std::to_string(sDocUid);
      const bool winOpen = SettingsWindow::IsOpen(winKey);
      if (ActionButton::Draw((std::string(T("Presets")) + "##v2deffprebtn").c_str(), ImVec2(btnW, 0.0f),
                             winOpen ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
      {
         if (!winOpen)
            rescan();
         SettingsWindow::Toggle(winKey);
      }
      const float btnY = ImGui::GetItemRectMin().y;
      SettingsWindow::Draw(winKey, T("Look presets"), WinAt(btnY), [&]() {
      MoreRows mr;
      if (ImGui::IsWindowAppearing())
         rescan();
      WinSection(T("Save this look"));
      ImGui::SetNextItemWidth(sLabelW + sFieldW);
      FieldWell::InputText("##v2deffname", name, sizeof name);
      // One kind of preset: the stack, which is the layer's whole look (its effects and its styles).
      const bool any = !l.effects.empty() || l.AnyFx();
      ImGui::BeginDisabled(name[0] == 0 || !any);
      const bool saveStack = ActionButton::Draw((std::string(T("Save")) + "##v2deffsave").c_str(), ImVec2(sLabelW + sFieldW, 0.0f), ActionButton::Kind::Plain);
      ImGui::EndDisabled();
      if (saveStack && name[0] != 0 && any)
      {
         std::error_code ec;
         std::filesystem::create_directories(EffPresetDir(), ec);
         std::ofstream f(AppPaths::FsPath(EffPresetDir() + "/" + name + ".inf2dfx"));
         f << Scene2DNode::EffectsText(l) << L("\n--params\n") << EffectParamsText(l);
         if (l.AnyFx())
            f << L("\n--styles\n") << Scene2DNode::StyleText(l);
         rescan();
      }
      WinSection(T("Saved"));
      if (sFiles.empty())
         ImGui::TextDisabled("%s", T("None yet"));
      else
      {
         for (size_t fi = 0; fi < sFiles.size(); fi++)
         {
            const bool styleOnly = !sFiles[fi].empty() && sFiles[fi].back() == '\x1f';
            const std::string shown = styleOnly ? sFiles[fi].substr(0, sFiles[fi].size() - 1) : sFiles[fi];
            if (MenuParts::Item((shown + "##pre" + std::to_string(fi)).c_str()))
            {
               std::ifstream f(AppPaths::FsPath(styleOnly ? StylePresetDir() + "/" + shown + ".inf2dstyle"
                                                          : EffPresetDir() + "/" + shown + ".inf2dfx"));
               std::stringstream ss;
               ss << f.rdbuf();
               std::string text = ss.str(), params, styles;
               if (styleOnly)
               {
                  styles = text;
                  text.clear();
               }
               if (const size_t sc = text.find("\n--styles\n"); sc != std::string::npos)
               {
                  styles = text.substr(sc + 10);
                  text.resize(sc);
               }
               if (!styles.empty())
               {
                  Layer got = l;
                  if (Scene2DNode::ParseStyle(styles, got))
                  {
                     PushUndoCheckpoint("Apply layer style");
                     std::copy(got.fx, got.fx + Scene2DNode::kFxCount, l.fx);
                     l.fxMore = got.fxMore;
                     Touch();
                  }
               }
               const size_t cut = text.find("\n--params\n");
               if (cut != std::string::npos)
               {
                  params = text.substr(cut + 10);
                  text.resize(cut);
               }
               Layer got = l;
               if (!text.empty() && Scene2DNode::ParseEffects(text, got))
               {
                  std::map<int, ParamSet> sets = ParseEffectParams(params);
                  std::vector<Scene2DNode::Effect> keep;
                  for (int gi = 0; gi < (int)got.effects.size(); gi++)
                  {
                     Scene2DNode::Effect& e = got.effects[(size_t)gi];
                     if (e.uid == 0)
                     {
                        keep.push_back(e);
                        continue;
                     }
                     for (const NodeEffectEntry& ne : kNodeEffects)
                        if (e.entry == ne.label)
                           if (const uint64_t uid = SpawnEffectNode(ne, 0.0f, 0.0f))
                           {
                              const auto ps = sets.find(gi);
                              if (ps != sets.end())
                                 if (GraphNode* gn = FindNodeByUid(uid))
                                    if (gn->node)
                                       gn->node->VisitParams(ps->second);
                              e.uid = uid;
                              e.shown = false;
                              keep.push_back(e);
                           }
                  }
                  PushUndoCheckpoint("Load effect stack");
                  for (Scene2DNode::Effect& e : keep)
                     l.effects.push_back(e);
                  Touch();
               }
            }
         }
      }
      });
   }

   // Attaches a node effect from `kNodeEffects` to `layerId` and triggers it immediately if applicable.
   void AddNodeEffect(Scene2DNode* doc, int layerId, int nodeIdx)
   {
      if (nodeIdx < 0 || nodeIdx >= (int)(sizeof(kNodeEffects) / sizeof(kNodeEffects[0])))
         return;
      const NodeEffectEntry& ne = kNodeEffects[nodeIdx];
      GraphNode* host = nullptr;
      for (GraphNode& gn : gNodes)
         if (gn.node.get() == doc)
            host = &gn;
      const float x = host != nullptr ? host->liveX - tok::view2d_x_span : 0.0f, y = host != nullptr ? host->liveY + tok::view2d_y_span : 0.0f;
      if (const uint64_t uid = SpawnEffectNode(ne, x, y))
      {
         Scene2DNode::Effect e;
         e.uid = uid;
         e.entry = ne.label;
         e.shown = false;
         e.animated = ne.animated;
         e.toMask = ne.toMask;
         if (GraphNode* spawnedGn = FindNodeByUid(uid))
            if (auto* rbg = dynamic_cast<RemoveBgNode*>(spawnedGn->node.get()))
               rbg->RequestMask();
         if (Layer* again = doc->FindLayer(layerId))
            again->effects.push_back(e);
         Touch();
      }
   }

   // Adds, moves and removes effects after the rows are drawn (adding spawns nodes, which can move gNodes).
   Scene2DNode::Effect NewNativeEffect(int type)
   {
      using S = Scene2DNode;
      S::Effect e;
      e.adjType = type;
      e.adjAmt = S::AdjDefault(e.adjType);
      e.adjP = S::AdjParamDefault(e.adjType);
      if (e.adjType == S::kAdjPhotoFilter)
      {
         const float warm[4] = { 0.93f, 0.54f, 0.13f, 1.0f };   // Photoshop's Warming Filter (85)
         std::copy(warm, warm + 4, e.colorA);
      }
      return e;
   }

   void ApplyEffectEdits(Scene2DNode* doc)
   {
      using S = Scene2DNode;
      if (sEffAdd.layer != 0 && sEffAdd.doc == sDocUid)
         if (Layer* l = doc->FindLayer(sEffAdd.layer))
         {
            if (sEffAdd.native >= 0)
            {
               PushUndoCheckpoint("Add effect");
               l->effects.push_back(NewNativeEffect(sEffAdd.native));
               Touch();
            }
            else if (sEffAdd.node >= 0)
            {
               AddNodeEffect(doc, sEffAdd.layer, sEffAdd.node);
            }
         }
      sEffAdd = {};
      if (sEffOp.layer != 0 && sEffOp.doc == sDocUid)
         if (Layer* l = doc->FindLayer(sEffOp.layer))
         {
            const int i = sEffOp.index, n = (int)l->effects.size();
            if (i >= 0 && i < n)
            {
               PushUndoCheckpoint("Edit effects");
               if (sEffOp.op == 1 && i > 0)
                  std::swap(l->effects[(size_t)i], l->effects[(size_t)i - 1]);
               else if (sEffOp.op == 2 && i + 1 < n)
                  std::swap(l->effects[(size_t)i], l->effects[(size_t)i + 1]);
               else if (sEffOp.op == 4 && sEffOp.to >= 0 && sEffOp.to < n && sEffOp.to != i)
               {
                  auto moved = std::move(l->effects[(size_t)i]);
                  l->effects.erase(l->effects.begin() + i);
                  l->effects.insert(l->effects.begin() + sEffOp.to, std::move(moved));
               }
               else if (sEffOp.op == 5 && l->effects[(size_t)i].uid == 0)
               {
                  const S::Effect copy = l->effects[(size_t)i];
                  l->effects.insert(l->effects.begin() + i + 1, copy);
               }
               else if (sEffOp.op == 6 && l->effects[(size_t)i].uid == 0)
               {
                  S::Effect& e = l->effects[(size_t)i];
                  S::Effect fresh = NewNativeEffect(e.adjType);
                  fresh.on = e.on;
                  e = fresh;
               }
               else if (sEffOp.op == 3)
               {
                  const uint64_t uid = l->effects[(size_t)i].uid;
                  l->effects.erase(l->effects.begin() + i);
                  if (GraphNode* gn = uid != 0 ? FindNodeByUid(uid) : nullptr)
                     RemoveNodeByIndex(gn->index);
               }
               Touch();
            }
         }
      sEffOp = {};
   }

   void EffectStack(Scene2DNode* doc, Layer& l)
   {
      SectionCard::Begin(T("Look"));
      const float avail = Ui::AvailW();
      const float preW = tok::view2d_pre_w;
      AddEffectMenu(l, std::max(tok::view2d_effect_stack_min, avail - preW - tok::space_1));
      FormParts::Inline(0.0f, tok::space_1);
      EffectPresets(l, preW);
      if (!l.effects.empty())
         NodeRow(T("Mask from"), &l.effMaskUid, true, "effmask" + std::to_string(l.id));
      for (int i : ReorderDrag::Order("V2DEFF", (int)l.effects.size()))
      {
         ReorderDrag::Flow(i);
         ImGui::PushID(1000 + i);
         EffectRows(doc, l, i);
         ImGui::PopID();
      }
      SectionCard::End();   // the last card closes inside the list, so its glide applies to it too
      if (const ReorderDrag::Move m = ReorderDrag::End())
         sEffOp = { l.id, m.from, 4, sDocUid, m.to };
   }

   // A style's settings; `primary` is the layer's own slot (its rows can be modulated), else one of its extras.
   void FxContourRow(Scene2DNode::Fx& f, int key)
   {
      static const float kPre[4][4] = { { 0.25f, 0.25f, 0.75f, 0.75f }, { 0.3f, 0.1f, 0.7f, 0.9f }, { 0.45f, 0.05f, 0.55f, 0.95f },
                                        { 0.4f, 1.0f, 0.8f, 0.0f } };
      int pick = 4;
      for (int i = 0; i < 4; i++)
         if (std::equal(f.contour, f.contour + 4, kPre[i]))
            pick = i;
      if (ComboRow(T("Contour"), &pick, { T("Linear"), T("Soft"), T("Hard"), T("Ring"), T("Custom") }) && pick < 4)
      {
         std::copy(kPre[pick], kPre[pick] + 4, f.contour);
         Touch();
      }
      if (pick == 4)
      {
         std::vector<float> c = { 0.0f, 0.0f, f.contour[0], f.contour[1], f.contour[2], f.contour[3], 1.0f, 1.0f };
         CurveEditor(c, key);
         const size_t n = c.size();
         const float got[4] = { n >= 6 ? c[2] : 0.25f, n >= 6 ? c[3] : 0.25f, n >= 6 ? c[n - 4] : 0.75f, n >= 6 ? c[n - 3] : 0.75f };
         if (!std::equal(got, got + 4, f.contour))
         {
            std::copy(got, got + 4, f.contour);
            Touch();
         }
      }
   }

   void FxRows(Layer& l, int t, Scene2DNode::Fx& f, bool primary)
   {
      using S = Scene2DNode;
      const bool two = t == S::kFxBevel || t == S::kFxGradient || t == S::kFxPattern;
      if (t == S::kFxGradient)
      {
         const float a[4] = { f.c[0], f.c[1], f.c[2], 1.0f }, b[4] = { f.c2[0], f.c2[1], f.c2[2], 1.0f };
         if (GradientRows("##v2dfxgrad", f.stops, a, b))
         {
            // The ends stay in the two colours; the first colour's alpha is the effect's opacity.
            std::copy(f.stops.front().c, f.stops.front().c + 3, f.c);
            std::copy(f.stops.back().c, f.stops.back().c + 3, f.c2);
         }
      }
      else
         ColorRow(T(t == S::kFxBevel ? "Highlight" : "Colour"), f.c);
      if (two && t != S::kFxGradient && !(t == S::kFxBevel && f.material >= 0))
         ColorRow(T(t == S::kFxBevel ? "Shadow" : "Second"), f.c2);
      const int fb = S::kLfFxBase + t * S::kLfFxFields;
      if (primary)
         ModNext(l, fb + 3);
      SliderRow(T("Opacity"), &f.c[3], 0.0f, 1.0f, "%.2f");
      if (t == S::kFxShadow || t == S::kFxInner || t == S::kFxSatin || two)
      {
         if (primary)
            ModNext(l, fb + 2);
         SliderRow(T("Angle"), &f.angle, params::view2d::angle_lo, params::view2d::angle_hi, "%.0f°");
      }
      if (t == S::kFxShadow || t == S::kFxInner || t == S::kFxBevel || t == S::kFxSatin)
      {
         if (primary)
            ModNext(l, fb + 1);
         if (t == S::kFxBevel)
            SliderRow(T("Depth"), &f.dist, params::view2d::dist_lo, params::view2d::dist_hi, "%.0f%%");
         else
            SliderRow(T("Distance"), &f.dist, params::view2d::dist_lo, params::view2d::dist_hi, "%.0f px");
      }
      if (t != S::kFxOverlay && t != S::kFxGradient)
      {
         if (primary)
            ModNext(l, fb);
         SliderRow(T(t == S::kFxStroke ? "Width" : t == S::kFxPattern ? "Scale" : "Size"), &f.size, params::view2d::size_3_lo, params::view2d::size_3_hi, "%.0f px");
      }
      if (t == S::kFxShadow || t == S::kFxInner || t == S::kFxGlow || t == S::kFxInnerGlow)
         SliderRow(T("Spread"), &f.spread, 0.0f, 1.0f, "%.2f");
      if (t == S::kFxStroke)
      {
         int pos = f.pos;
         if (ComboRow(T("Position"), &pos, { T("Outside"), T("Inside"), T("Centre") }))
            f.pos = pos;
      }
      if (t == S::kFxPattern)
      {
         std::vector<std::string> names;
         for (int p = 0; p < S::kFxPatternCount; p++)
            names.push_back(T(S::FxPatternName(p)));
         int p = std::clamp(f.pattern, 0, S::kFxPatternCount - 1);
         if (ComboRow(T("Pattern"), &p, names))
            f.pattern = p;
      }
      if (t == S::kFxBevel)
      {
         int m = f.material + 1;
         if (ComboRow(T("Material"), &m, { T("Plain"), T("Plasticine"), T("Metal"), T("Chrome"), T("Stipple") }))
            f.material = m - 1;
      }
      if (t == S::kFxShadow || t == S::kFxGlow || t == S::kFxBevel || t == S::kFxSatin)
         FxContourRow(f, (int)ImHashData(&t, sizeof t, ImGui::GetID("ct")));   // whole id: this layer's scope plus the effect
   }


   void DrawEffectsTab(Scene2DNode* doc)
   {
      Layer* l = Active(doc);
      if (l == nullptr || l->kind == Kind::Adjust)
      {
         CentredText(ImGui::GetCursorScreenPos().y, l == nullptr ? T("No layer selected") : T("No effects here"));
         return;
      }
      EffectStack(doc, *l);
      ApplyEffectEdits(doc);
      l = Active(doc);
      if (l == nullptr || l->kind == Kind::Group)
         return;
      int removeMore = -1;
      for (int t = 0; t < Scene2DNode::kFxCount; t++)
      {
         if (!l->fx[t].on)
            continue;
         ImGui::PushID(t);
         const std::string base = "##v2dfx" + std::to_string(l->id) + "_" + std::to_string(t);
         const std::string winKey = "v2dfx/" + std::to_string(sDocUid) + "/" + std::to_string(l->id) + "/" + std::to_string(t);
         const char* english = Scene2DNode::FxName(t);
         SectionCard::Begin();
         const float rowY = ImGui::GetCursorPosY();
         {
            Ui::CentreInRow centred(ImGui::GetFrameHeight(), CheckBox::Size());
            bool on = l->fx[t].on;
            if (CheckBox::Box((base + "on").c_str(), &on))
            {
               PushUndoCheckpoint("Toggle layer style");
               l->fx[t].on = on;
               Touch();
            }
            FormParts::Inline(0.0f, tok::space_2);
         }
         const ImVec2 np = CardTitle(base + "name", T(english), l->fx[t].on, 1, winKey, T("Click for all settings, right-click for more"));
         // Right-click on the header: the style's own menu.
         if (MenuParts::ContextClicked())
            ImGui::OpenPopup("##v2dfxmenu");
         if (MenuParts::BeginPopup("##v2dfxmenu"))
         {
            if (MenuParts::Item(T("All Settings"), nullptr, SettingsWindow::IsOpen(winKey)))
               SettingsWindow::Toggle(winKey);
            MenuParts::Separator();
            if (MenuParts::Item(T("Add Another"), nullptr))
            {
               PushUndoCheckpoint("Add layer style");
               Scene2DNode::Fx g = l->fx[t];
               l->fxMore.emplace_back(t, g);
               Touch();
            }
            if (MenuParts::Item(T("Reset"), nullptr))
            {
               PushUndoCheckpoint("Reset layer style");
               l->fx[t] = Scene2DNode::FxDefault(t);
               l->fx[t].on = true;
               Touch();
            }
            MenuParts::Separator();
            if (MenuParts::Item(T("Remove Style"), nullptr))
            {
               PushUndoCheckpoint("Remove layer style");
               l->fx[t].on = false;
               Touch();
            }
            MenuParts::EndPopup();
         }
         FormParts::Inline(0.0f, tok::space_2);
         if (HeaderIcon((base + "set").c_str(), IconsInfinite::Sliders, SettingsWindow::IsOpen(winKey), T("All settings"), rowY))
            SettingsWindow::Toggle(winKey);
         // The window: the style's every row, then the other copies of the same kind, each with its own settings.
         SettingsWindow::Draw(winKey, T(english), WinAt(np.y), [&]() {
            MoreRows mr;
            FxRows(*l, t, l->fx[t], true);
            for (int m = 0; m < (int)l->fxMore.size(); m++)
            {
               if (l->fxMore[(size_t)m].first != t)
                  continue;
               ImGui::PushID(100 + m);
               ImGui::Separator();
               Scene2DNode::Fx& g = l->fxMore[(size_t)m].second;
               BoolRow(T("On"), &g.on);
               FxRows(*l, t, g, false);
               if (ActionButton::Draw(T("Remove"), ImVec2(-1.0f, 0.0f), ActionButton::Kind::Plain))
                  removeMore = m;
               ImGui::PopID();
            }
         });
         ImGui::PopID();
      }
      if (removeMore >= 0 && removeMore < (int)l->fxMore.size())
      {
         PushUndoCheckpoint("Remove layer style");
         l->fxMore.erase(l->fxMore.begin() + removeMore);
         Touch();
      }
   }

   // Adjustment layers live in the Layers list (Look is the selected layer's own effects). Each one's settings window
   // opens from its thumbnail, its right-click menu or a double-click, and is drawn here every frame, whatever tab
   // is showing, so it stays open while you work.
   void DrawAdjustWindows(Scene2DNode* doc)
   {
      std::vector<Layer>& ls = doc->Layers();
      for (Layer& lr : ls)
      {
         Layer* l = &lr;
         if (l->kind != Kind::Adjust)
            continue;
         const std::string winKey = "v2dadj/" + std::to_string(sDocUid) + "/" + std::to_string(l->id);
         if (!SettingsWindow::IsOpen(winKey))
            continue;
         const std::string title = l->name.empty() ? std::string(T(Scene2DNode::AdjName(l->adjType))) : l->name;
         ImGui::PushID(l->id);
         SettingsWindow::Draw(winKey, title.c_str(), WinAt(ImGui::GetWindowPos().y + tok::view2d_adjust_windows_span), [&]() {
            MoreRows mr;
            float lo = 0, hi = 1;
            Scene2DNode::AdjRange(l->adjType, lo, hi);
            if (l->adjType != Scene2DNode::kAdjGradientMap)
            {
               SliderRow(T(AdjAmountLabel(l->adjType)), &l->adjAmt, lo, hi, AdjFmt(l->adjType));
               AdjExtras(*l, l->id, doc);
            }
            if (l->adjType == Scene2DNode::kAdjGradientMap || l->adjMap)
            {
               ColorRow(T("Shadows"), l->colorA);
               ColorRow(T("Highlights"), l->colorB);
            }
         });
         ImGui::PopID();
      }
   }

   // Colour: the document's adjustment layers as their own tab (whole-layer colour work), apart from Look (the
   // selected layer's effects). Searchable Add; each card is on box, name, gear; drag a card to reorder. The
   // settings windows themselves are drawn by DrawAdjustWindows, so they stay open on every tab.
   void DrawColourTab(Scene2DNode* doc)
   {
      SectionCard::Begin(T("Adjustment layers"));
      static char sAdjSearch[64] = "";
      static ImVec2 sAdjPopPos(0, 0);
      if (ActionButton::Draw((std::string(T("Add")) + "##v2daddadjbtn").c_str(), ImVec2(-1.0f, 0.0f), ActionButton::Kind::Plain))
      {
         sAdjSearch[0] = 0;
         sAdjPopPos = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + tok::space_1);
         ImGui::OpenPopup("##v2daddadjpop");
      }
      ImGui::SetNextWindowPos(sAdjPopPos, ImGuiCond_Appearing);
      if (MenuParts::BeginPopup("##v2daddadjpop"))
      {
         ImGui::SetNextItemWidth(sFieldW);
         if (ImGui::IsWindowAppearing())
            UiInteract::FocusHere();
         FieldWell::InputText("##v2dadjq", sAdjSearch, sizeof sAdjSearch);
         MenuParts::Separator();
         if (sAdjSearch[0] != 0)
         {
            bool any = false;
            for (int t = 0; t < Scene2DNode::kAdjCount; t++)
               if (FilterMatch(T(Scene2DNode::AdjName(t)), sAdjSearch) || FilterMatch(Scene2DNode::AdjGroup(t), sAdjSearch))
               {
                  any = true;
                  if (MenuParts::Item(T(Scene2DNode::AdjName(t)), T(Scene2DNode::AdjGroup(t)), false, true))
                  {
                     sPending.what = Pending::AddAdjust;
                     sPending.arg = t;
                  }
               }
            if (!any)
               ImGui::TextDisabled("%s", T("No matching adjustments"));
         }
         else
         {
            for (const char* g : { "Adjust", "Colour", "Stylize" })
            {
               if (!MenuParts::SubMenu(T(g), true))
                  continue;
               for (int t = 0; t < Scene2DNode::kAdjCount; t++)
                  if (std::strcmp(Scene2DNode::AdjGroup(t), g) == 0 &&
                      MenuParts::Item(T(Scene2DNode::AdjName(t)), nullptr, false, true))
                  {
                     sPending.what = Pending::AddAdjust;
                     sPending.arg = t;
                  }
               ImGui::EndMenu();
            }
         }
         MenuParts::EndPopup();
      }
      int moveFrom = 0, moveTo = 0;
      bool moveBelow = false;
      int deleteId = 0;
      std::vector<Layer>& ls = doc->Layers();
      // The adjustment layers top first, as the Layers list shows them; a drag reorders among them, live.
      std::vector<int> adj;
      for (int i = (int)ls.size() - 1; i >= 0; i--)
         if (ls[i].kind == Kind::Adjust)
            adj.push_back(i);
      for (int k : ReorderDrag::Order("V2DADJCARD", (int)adj.size()))
      {
         ReorderDrag::Flow(k);
         Layer* l = &ls[adj[k]];
         const int id = l->id;
         const std::string base = "##v2dadj" + std::to_string(id);
         const std::string winKey = "v2dadj/" + std::to_string(sDocUid) + "/" + std::to_string(id);
         const char* english = Scene2DNode::AdjName(l->adjType);
         ImGui::PushID(id);
         SectionCard::Begin();
         const float rowY = ImGui::GetCursorPosY();
         {
            Ui::CentreInRow centred(ImGui::GetFrameHeight(), CheckBox::Size());
            bool on = l->visible;
            if (CheckBox::Box((base + "on").c_str(), &on))
            {
               PushUndoCheckpoint(on ? "Show adjustment" : "Hide adjustment");
               l->visible = on;
               Touch();
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("On"));
            FormParts::Inline(0.0f, tok::space_2);
         }
         const std::string title = (l->name.empty() ? std::string(T(english)) : l->name) +
                                   (l->clip ? std::string(" · ") + T("Clipped") : std::string());
         CardTitle(base + "name", title, l->visible, 1, winKey, T("Click for all settings, right-click for more"));
         if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            SelectOnly(id);
         ReorderDrag::Item(k, title.c_str(), nullptr, 0, sCardSpan);
         // Right-click on the header: an adjustment layer's own menu (no pixel ops).
         if (MenuParts::ContextClicked())
         {
            SelectOnly(id);
            ImGui::OpenPopup("##v2dadjmenu");
         }
         if (MenuParts::BeginPopup("##v2dadjmenu"))
         {
            if (MenuParts::Item(T("All Settings"), nullptr, SettingsWindow::IsOpen(winKey)))
               SettingsWindow::Toggle(winKey);
            MenuParts::Separator();
            if (MenuParts::Item(l->clip ? T("Release Clipping Mask") : T("Create Clipping Mask"), Sc("Alt+Ctrl+G")))
            {
               PushUndoCheckpoint("Clipping mask");
               l->clip = !l->clip;
               Touch();
            }
            if (MenuParts::Item(T("Duplicate Adjustment"), Sc("Ctrl+J")))
               sPending.what = Pending::Duplicate;
            if (MenuParts::Item(T("Merge Down"), Sc("Ctrl+E"), false, LayerUnder(doc, id) != 0))
               sPending.what = Pending::MergeDown;
            MenuParts::Separator();
            if (MenuParts::Item(T("Delete Adjustment"), "Delete"))
               deleteId = id;
            MenuParts::EndPopup();
         }
         FormParts::Inline(0.0f, tok::space_2);
         if (HeaderIcon((base + "set").c_str(), IconsInfinite::Sliders, SettingsWindow::IsOpen(winKey), T("All settings"), rowY))
            SettingsWindow::Toggle(winKey);
         ImGui::PopID();
      }
      SectionCard::End();
      if (const ReorderDrag::Move m = ReorderDrag::End())
      {
         moveFrom = ls[adj[m.from]].id;
         moveTo = ls[adj[m.to]].id;
         moveBelow = m.to > m.from;   // moving down the list lands under the card it passes
      }
      if (moveFrom != 0 && moveTo != 0 && moveFrom != moveTo && !doc->IsInside(moveTo, moveFrom))
      {
         PushUndoCheckpoint("Reorder adjustment");
         doc->MoveLayer(moveFrom, moveTo, moveBelow, false);
         Touch();
      }
      if (deleteId != 0)
      {
         PushUndoCheckpoint("Delete adjustment");
         doc->RemoveLayer(deleteId);
         sSel.erase(std::remove(sSel.begin(), sSel.end(), deleteId), sSel.end());
         SettingsWindow::Close("v2dadj/" + std::to_string(sDocUid) + "/" + std::to_string(deleteId));
         Touch();
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
      static int sCanvasW = 0, sCanvasH = 0, sAnchor = tok::view2d_anchor;
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
      {
         // W and H on one row, as the layer's Size.
         float wh[2] = { (float)sCanvasW, (float)sCanvasH };
         float* const p[2] = { &wh[0], &wh[1] };
         const char* const fmts[2] = { "W %.0f", "H %.0f" };
         const int c = DragMulti(T("Size"), p, fmts, params::view2d::size_speed, 1.0f, 1.0f, params::view2d::size_def);
         sCanvasW = (int)std::lround(wh[0]);
         sCanvasH = (int)std::lround(wh[1]);
         if ((c & 1) && d.ratioLock)
            sCanvasH = (int)std::lround(sCanvasW * (float)d.h / (float)d.w);
         if ((c & 2) && d.ratioLock)
            sCanvasW = (int)std::lround(sCanvasH * (float)d.w / (float)d.h);
      }
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
      {
         float wh[2] = { (float)sImageW, (float)sImageH };
         float* const p[2] = { &wh[0], &wh[1] };
         const char* const fmts[2] = { "W %.0f", "H %.0f" };
         const int c = DragMulti(T("Size##img"), p, fmts, params::view2d::size_speed, 1.0f, 1.0f, params::view2d::size_def);
         sImageW = (int)std::lround(wh[0]);
         sImageH = (int)std::lround(wh[1]);
         if (c & 1)
            sImageH = (int)std::lround(sImageW * (float)d.h / (float)d.w);
         if (c & 2)
            sImageW = (int)std::lround(sImageH * (float)d.w / (float)d.h);
      }
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
      {
         Pairs g;
         BoolRow(T("Rulers"), &d.rulers);
         BoolRow(T("Guides"), &d.guidesOn);
         BoolRow(T("Grid"), &d.grid);
         BoolRow(T("Snap"), &d.snap);
      }
      if (d.grid)
      {
         DragRow(T("Grid size"), &d.gridSize, 1.0f, "%.0f px", params::view2d::grid_size_lo, params::view2d::grid_size_hi);
         FieldLabel(T("Subdivisions"));
         ImGui::SetNextItemWidth(sFieldW);
         if (FieldWell::Slider("##v2dsub", &d.gridSubdiv, params::view2d::grid_subdiv_lo, params::view2d::grid_subdiv_hi, "%d"))
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
         if (SliderRow(T("Quality"), &q, params::view2d::q_lo, params::view2d::q_hi, "%.0f"))
            o->jpegQuality = (int)std::lround(q);
      }
      ImGui::BeginDisabled(gOfflineRender.active);
      if (ActionButton::Draw(L("Render Image##v2dimg"), ImVec2(availW, 0)))
         sPending.what = Pending::RenderImage;
      ImGui::EndDisabled();
      ImGui::TextDisabled(T("%d x %d, F12"), d.w, d.h);
      SectionCard::Begin(T("Animation"));
      ComboRow(T("Video"), &sVideoFormat, { "MP4", "MOV" });
      {
         Pairs g;
         DragRow(T("Frame rate"), &d.fps, params::view2d::fps_speed, "%.0f fps", params::view2d::fps_lo, params::view2d::fps_hi);
         DragRow(T("Length"), &d.seconds, params::view2d::seconds_speed, "%.1f s", params::view2d::seconds_lo, params::view2d::seconds_hi);
      }
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
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, (PanelKit::Chip() - ImGui::GetFontSize()) * 0.5f));
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
         if (MenuParts::Item(T("Open PSD or ORA..."))) sPending.what = Pending::OpenFile;
         MenuParts::Separator();
         if (MenuParts::Item(T("Export PSD..."), nullptr, false, doc != nullptr)) sPending.what = Pending::ExportPsd;
         if (MenuParts::Item(T("Export ORA..."), nullptr, false, doc != nullptr)) sPending.what = Pending::ExportOra;
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
      UiDensity::Compact compact;   // the side panel is a compact strip (Photoshop / Affinity Properties size)
      sLabelW = SectionCard::LabelW();

      // The tab column: five stops. Layer holds its transform, Look holds the selected layer's effects and styles,
      // Colour holds the document's adjustment layers, Document holds the render settings.
      // sTab keeps its old values (1/2, 5/6, 3/4 open the same stop), so code that opens a tab by number still lands.
      const struct { const char* id; const char* icon; const char* tip; int tab; int alt; } kTabs[5] = {
         { "##v2dt0", IconsInfinite::Layers, "Layers", 0, 0 },
         { "##v2dt1", IconsInfinite::Edit, "Layer", 1, 2 },
         { "##v2dt5", IconsInfinite::FamilyEffects, "Look", 5, 5 },
         { "##v2dt6", IconsInfinite::Curve, "Colour", 6, 6 },
         { "##v2dt3", IconsInfinite::Crop, "Document", 3, 4 },
      };
      sTab = std::clamp(sTab, 0, 6);
      {
         Ui::Stack stack(Ui::Space::S1);
         ImGui::BeginGroup();
         for (int i = 0; i < 5; i++)
            if (IconChip(kTabs[i].id, kTabs[i].icon, sTab == kTabs[i].tab || sTab == kTabs[i].alt, T(kTabs[i].tip)))
               sTab = kTabs[i].tab;
         ImGui::EndGroup();
      }
      ActionButton::RowNext();

      sChip = SideChip();
      if (sTab == 0)
      {
         ImGui::BeginChild("##v2dlayers", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
         const float availW = Ui::AvailW();
         // One row gap for the whole tab (PanelKit), so the document row sits on the same pitch as the rows below it.
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, PanelKit::RowGap()));
         DrawDocPicker(doc, availW);
         const float roomH = Ui::AvailH();
         DrawLayersCard(doc, availW, std::max(tok::view2d_side_ratio * (kRowH + 1.0f), roomH - tok::space_1));
         ImGui::PopStyleVar();
         EndChildSafe();
      }
      else
      {
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SectionCard::Pad(), 0.0f));
         ImGui::BeginChild("##v2dprops", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
         ImGui::PopStyleVar();
         SectionCard::BeginWindow();
         Ui::JoinBelow();
         const float w = std::floor(Ui::AvailW());
         sFieldW = std::floor(std::max(tok::view2d_field_w_min, w - sLabelW));   // whole pixels, so strip cells and wells end on one edge
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, PanelKit::FramePadY()));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, PanelKit::RowGap()));
         if (sTab == 1 || sTab == 2)
         {
            DrawLayerTab(doc, 0);
            const Layer* al = Active(doc);
            if (al != nullptr && al->kind != Kind::Group && al->kind != Kind::Fill && al->kind != Kind::Adjust)
               DrawLayerTab(doc, 1);
         }
         else if (sTab == 5)
         {
            const Layer* al = Active(doc);
            if (al != nullptr && al->kind != Kind::Adjust)
               DrawEffectsTab(doc);
            else
            {
               SectionCard::Begin();
               ImGui::TextDisabled("%s", al != nullptr ? T("An adjustment layer: its settings are in Colour.")
                                                       : T("Select a layer to see its look."));
            }
         }
         else if (sTab == 6)
            DrawColourTab(doc);
         else
         {
            DrawDocumentTab(doc);
            DrawOutputTab(doc, w);
         }
         SectionCard::End();   // close the last card first, so the room below is not inside it
         Ui::Spacer(Ui::Space::S5);   // room under the last card so a scroll never ends on the edge
         ImGui::PopStyleVar(2);
         SectionCard::EndWindow();
         EndChildSafe();
      }
      DrawAdjustWindows(doc);   // open adjustment windows stay up on every tab
      compact.End(); // before the child closes: the pushes were made inside it
      EndChildSafe();
      sChip = tok::control_h;   // the compact scope is still alive here: back to the regular chip explicitly
      sLabelW = kLabelW;
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
         MakeDoc(at.x, at.y + tok::view2d_run_pending_span, "");
         break;
      case Pending::DuplicateDoc:
      {
         PushUndoCheckpoint("Duplicate document");
         MakeDoc(at.x, at.y + tok::view2d_run_pending_span, DocTextWithoutSelection(doc->DocText()));
         break;
      }
      case Pending::DeleteDoc: DeleteDoc(doc); break;
      case Pending::OpenFile:
         if (!AuditNoDialog())
            for (const std::string& path : Platform::OpenFilesDialog(T("Open"), { "psd", "ora" }))
               OpenLayeredFile(path, ImVec2(at.x, at.y + tok::view2d_run_pending_span));
         break;
      case Pending::ExportPsd:
      case Pending::ExportOra:
         ExportLayered(doc, todo.what == Pending::ExportOra ? "ora" : "psd");
         break;
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
            const int g = doc->GroupLayers(sSel);
            if (Layer* gl = doc->FindLayer(g))
               gl->open = false;   // Group from Layers starts collapsed, as in Photoshop
            doc->Commit();
            SelectOnly(g);
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
      case Pending::FlattenGroup:
         if (Layer* a = Active(doc))
            if (a->kind == Kind::Group)
            {
               static int sFlatGroupFrame = 1 << 25;
               const size_t undo0 = gUndoStack.size();
               PushUndoCheckpoint("Flatten group");
               if (const int nid = doc->MergeLayers({ a->id }, ++sFlatGroupFrame))
                  SelectOnly(nid);
               else
                  gUndoStack.resize(undo0);
               Touch();
            }
         break;
      case Pending::MergeDown:
      case Pending::MergeVisible:
      case Pending::Flatten:
      {
         // Merge Down: the active layer and the one under it (Merge Layers when more are selected). Merge Visible:
         // every shown top-level layer, hidden ones kept. Flatten: everything, hidden ones dropped. One undo step.
         std::vector<int> ids;
         if (todo.what == Pending::MergeDown)
         {
            if (sSel.size() > 1)
               ids = sSel;
            else if (Layer* a = Active(doc))
               if (const int under = LayerUnder(doc, a->id))
                  ids = { under, a->id };
         }
         else
            for (int c : doc->ChildrenOf(0))
               if (const Layer* l = doc->FindLayer(c))
                  if (todo.what == Pending::Flatten || doc->Shown(*l))
                     ids.push_back(c);
         if (ids.size() < tok::view2d_run_pending_limit && todo.what != Pending::Flatten)
            break;
         if (ids.empty())
            break;
         static int sMergeFrame = 1 << 24;
         const size_t undo0 = gUndoStack.size();
         PushUndoCheckpoint(todo.what == Pending::Flatten ? "Flatten image" : todo.what == Pending::MergeVisible ? "Merge visible" : "Merge layers");
         if (const int nid = doc->MergeLayers(ids, ++sMergeFrame))
            SelectOnly(nid);
         else
            gUndoStack.resize(undo0);   // a refused merge is no step
         Touch();
         break;
      }
      case Pending::Duplicate:
         if (SelectionActsOnPixels(doc))
         {
            PushUndoCheckpoint("Layer via copy");
            if (const int nid = SelectionToLayer(doc, false))
               SelectOnly(nid);
         }
         else if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Duplicate layer");
            SelectOnly(doc->Duplicate(a->id));
         }
         break;
      case Pending::Copy:
      case Pending::Cut:
         // The layer the user picked is the one copied. A new selection already picked the layer it covers; re-targeting
         // here made a leftover selection copy the same top layer's pixels again whatever layer was picked since.
         if (SelectionActsOnPixels(doc))
         {
            const bool cut = todo.what == Pending::Cut;
            PushUndoCheckpoint(cut ? "Cut" : "Copy");
            const int src = Active(doc)->id;
            if (const int nid = SelectionToLayer(doc, cut))
            {
               ClipToOs(doc, { nid });
               sClip = doc->CopyLayers({ nid });
               sPasteCount = -1;   // pixels paste in place first, like Affinity and Photoshop
               doc->RemoveLayer(nid);
               SelectOnly(src);
            }
         }
         else if (!sSel.empty())
         {
            sClip = doc->CopyLayers(sSel);
            sPasteCount = 0;
            if (todo.what == Pending::Cut)
               DeleteSelected(doc);
         }
         break;
      case Pending::CopyMerged:
         // Every shown layer as one picture, cut to the selection: a pixel clip that pastes in place (Photoshop).
         if (const int nid = MergedLayer(doc, VisibleTop(doc), true, 0))
         {
            ClipToOs(doc, { nid });
            sClip = doc->CopyLayers({ nid });
            sPasteCount = -1;
            doc->RemoveLayer(nid);
            doc->Commit();
         }
         break;
      case Pending::StampVisible:
      case Pending::StampDown:
      {
         // Stamp: the shown layers (or the selected ones) merged into a new layer above the active one; they stay.
         Layer* a = Active(doc);
         const std::vector<int> set = todo.what == Pending::StampVisible ? VisibleTop(doc) : sSel;
         if (set.empty() || a == nullptr)
            break;
         PushUndoCheckpoint(todo.what == Pending::StampVisible ? "Stamp visible" : "Stamp layers");
         if (const int nid = MergedLayer(doc, set, false, a->id))
            SelectOnly(nid);
         Touch();
         break;
      }
      case Pending::Paste:
         if (PasteFromOs(doc))
            break;
         if (!sClip.empty())
         {
            PushUndoCheckpoint("Paste layer");
            Layer* a = Active(doc);
            sPasteCount++;
            const float nudge = tok::view2d_nudge_ratio * std::max(0, sPasteCount);
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
            sEditMask = true;   // like Photoshop: the new mask is what the brush paints
         }
         break;
      case Pending::MaskFromSel:
         if (Layer* a = Active(doc))
         {
            PushUndoCheckpoint("Mask from selection");
            doc->MaskFromSelection(a->id);
            doc->Deselect();
            sEditMask = true;
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
         SelectOnly(PlaceNew(doc, doc->AddVectorLayer(NewAbove(doc))).id);
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
      if (MenuParts::RightPressed() && ImGui::IsMouseHoveringRect(mn, mx) && !ImGui::IsAnyItemHovered() &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
         ImGui::OpenPopup("##v2ddockmenu");
      if (MenuParts::TestOpen("##v2ddockmenu"))
         ImGui::OpenPopup("##v2ddockmenu");
      if (MenuParts::BeginPopup("##v2ddockmenu"))
      {
         if (MenuParts::SubMenu(T("Dock Position")))
         {
            DrawViewDockMenuItems(2);
            ImGui::EndMenu();
         }
         MenuParts::Separator();
         if (MenuParts::Item(T("Close 2D View")))
            gView2DOpen = false;
         MenuParts::EndPopup();
      }
   }

   // A .psd or .ora opened as a new document, its layers as paint layers. False (and nothing added) when it does not read.
   bool OpenLayeredFile(const std::string& path, ImVec2 at)
   {
      std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
      const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      Scene2DNode read;
      std::string err;
      static const std::vector<std::string> kOra = { "ora" };
      const bool ora = HasExtension(path, kOra);
      if (bytes.empty() || !(ora ? read.LoadOra(bytes, &err) : read.LoadPsd(bytes, &err)))
      {
         fprintf(stderr, "[2D View] could not open %s: %s\n", path.c_str(), err.empty() ? "unreadable" : err.c_str());
         return false;
      }
      PushUndoCheckpoint("Open file");
      return MakeDoc(at.x, at.y, read.DocText()) != nullptr;
   }

   void ExportLayered(Scene2DNode* doc, const char* ext)
   {
      if (doc == nullptr || AuditNoDialog())
         return;
      const std::string path = Platform::SaveLayeredDialog(std::string("infinite_2d.") + ext, ext);
      if (path.empty())
         return;
      static int frame = 6000000;
      std::vector<uint8_t> bytes;
      std::string note;
      const bool ok = std::strcmp(ext, "ora") == 0 ? doc->SaveOra(bytes, ++frame) : doc->SavePsd(bytes, ++frame, &note);
      if (ok && !note.empty())
      {
         sExportNote = "PSD: " + note;
         sExportNoteT = ImGui::GetTime();
      }
      std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
      if (!ok || !out.write((const char*)bytes.data(), (std::streamsize)bytes.size()))
         fprintf(stderr, "[2D View] could not write %s\n", path.c_str());
      else
         sLastRender = path;
   }

   // Files dropped on the 2D View: a still image becomes a pixel layer; a live source (video, model) spawns its
   // node, visible on the canvas, and lands as a layer centred on the drop point. Returns the layers added.
   int DropFiles(Scene2DNode* doc, const std::vector<std::string>& files, ImVec2 docPt)
   {
      static const std::vector<std::string> kModels = { "obj", "ply", "stl", "usd", "usda", "usdc", "usdz", "abc" };
      static const std::vector<std::string> kGltf = { "gltf", "glb" };
      static const std::vector<std::string> kSvg = { "svg" };
      static const std::vector<std::string> kLayered = { "psd", "ora" };
      GraphNode* self = DocGraphNode();
      const ImVec2 base = self != nullptr ? NodePos(*self) : ImVec2(0, 0);
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      PushUndoCheckpoint("Drop into 2D View");
      gSuppressUndoCheckpoints = true;
      int added = 0;
      auto place = [&](GraphNode* gn) {   // a live source (video, model) stays a visible node on the canvas
         if (gn == nullptr)
            return;
         const ImVec2 at(docPt.x + tok::view2d_drop_files_ratio * (float)added, docPt.y + tok::view2d_drop_files_ratio * (float)added);
         if (AddNodeLayer(doc, gn->uid, &at) != 0)
            added++;
      };
      float row = 0.0f;
      for (const std::string& path : files)
      {
         const ImVec2 spot(base.x - tok::view2d_drop_files_span, base.y + row);
         row += tok::view2d_row;
         if (HasExtension(path, kSvg))
         {
            if (ImportSvgInto(doc, path))
               added++;
         }
         else if (HasExtension(path, kLayered))
         {
            // A layered file is a document of its own.
            if (OpenLayeredFile(path, spot))
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
            // A still image is pixels: it lands as a native pixel layer, centred on the drop point.
            int w = 0, h = 0, ch = 0;
            std::vector<unsigned char> bytes;
            if (std::ifstream f(AppPaths::FsPath(path), std::ios::binary); f)
               bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            if (unsigned char* img = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 4))
            {
               const std::vector<uint8_t> rgba(img, img + (size_t)w * h * tok::view2d_rgba_scale);
               stbi_image_free(img);
               const size_t slash = path.find_last_of("/\\");
               std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
               if (const size_t dot = name.rfind('.'); dot != std::string::npos && dot > 0)
                  name.resize(dot);
               const int x = (int)std::lround(docPt.x + tok::view2d_drop_files_ratio * (float)added) - w / tok::view2d_infinite_view2_dtest_scale;
               const int y = (int)std::lround(docPt.y + tok::view2d_drop_files_ratio * (float)added) - h / tok::view2d_infinite_view2_dtest_scale;
               Layer* a = Active(doc);
               if (const int nid = doc->AddFileLayer(0, name, x, y, w, h, rgba, a != nullptr ? a->id : 0))
               {
                  doc->Commit();
                  SelectOnly(nid);
                  added++;
               }
            }
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

   uint64_t sSeenDoc = 0;

   // Another document came to the front: whatever was half-made in the last one ends there.
   void OnDocSwitch(Scene2DNode* doc)
   {
      (void)doc;
      sTextEdit = 0;
      sCropOn = false;
      sPenItem = -1;
      sItem = -1;
      sItemLayer = 0;
      sAnchor = -1;
      sPoly.clear();
      sCurveDrag = -1;
      sOp = Op();
   }

   void DrawPanel()
   {
      InitBrushes();
      SaveBrushesIfChanged();
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
      if (sSeenDoc != sDocUid)
      {
         OnDocSwitch(doc);
         sSeenDoc = sDocUid;
      }
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
      const ImVec2 avail = Ui::Avail();
      const float gapX = ImGui::GetStyle().ItemSpacing.x;
      const float sideBase = std::min(kSideW, avail.x * 0.5f);
      const float sideW = sSideOpen ? sideBase : 0.0f;
      const float viewW = std::max(tok::view2d_view_w_min, avail.x - sideW - (sSideOpen ? gapX : 0.0f));
      const float statusH = CompactRowH() + ImGui::GetStyle().ItemSpacing.y;
      const float canvasH = std::max(tok::view2d_canvas_h_min, avail.y - statusH);
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
      // The clone source is a point in one document: another document coming up drops it (Alt-click again there).
      static uint64_t cloneDoc = sDocUid;
      if (cloneDoc != sDocUid)
      {
         cloneDoc = sDocUid;
         sHaveCloneSrc = sCloneOffsetSet = false;
      }
      // A lifted selection goes back down once its drag or free transform is over.
      if (sFloat.id != 0 && sFloat.docUid != sDocUid)
      {
         // Another document came up while a piece floated: it goes down where it was shown, in its own document.
         GraphNode* g = FindNodeByUid(sFloat.docUid);
         if (auto* od = g != nullptr ? dynamic_cast<Scene2DNode*>(g->node.get()) : nullptr)
            DropFloat(od);
         sFloat = Float();
         sOp = Op();
      }
      if (sFloat.id != 0 && sOp.kind == 0 && !FreeTransformOn(doc))
         DropFloat(doc);
      if (sDirty)
         doc->Commit();
      TakeDroppedFiles(doc);
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
   std::ifstream f(AppPaths::FsPath(path), std::ios::binary);
   if (!f)
      return false;
   const std::string svg((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
   PushUndoCheckpoint("Import SVG");
   doc->Layers();
   const int id = PlaceNew(doc, doc->AddVectorLayer(NewAbove(doc))).id;
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
      if (!v.is_array() || v.size() < tok::view2d_v2_color_limit || v.size() > tok::view2d_v2_color_limit_lg)
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
         if (op == "stroke" && !(c.contains("points") && c["points"].is_array() && c["points"].size() >= tok::view2d_v2_check_limit && c["points"].size() % tok::view2d_points_period == 0))
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
            doc->ModifySelection(how == "expand" ? 0 : how == "contract" ? 1 : how == "smooth" ? 2 : how == "feather" ? 5 : 3, std::clamp(c.value("px", 4), 1, 100));
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
         b.size = c.value("size", tok::view2d_v2_apply);
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
   // Canvas rotate: a turned view maps screen to document and back exactly, turning about the pointer keeps the
   // document point under it fixed, and Esc during a turn puts the view back.
   {
      const float zoom0 = sZoom, angle0 = sViewAngle;
      const ImVec2 pan0 = sPan, mn0 = sViewMn, mx0 = sViewMx;
      sViewMn = ImVec2(0, 0), sViewMx = ImVec2(800, 600), sZoom = 0.5f, sPan = ImVec2(13, -7), sViewAngle = 0.0f;
      const ImVec2 pin(250, 180), under = ToDoc(doc, pin);
      TurnViewAbout(doc, 30.0f * 3.14159265f / 180.0f, pin);
      const ImVec2 still = ToDoc(doc, pin);
      expect("turning about the pointer keeps the document point under it", std::fabs(still.x - under.x) < 0.01f && std::fabs(still.y - under.y) < 0.01f);
      const ImVec2 s0 = ToScreen(doc, 37.0f, 91.0f), back = ToDoc(doc, s0);
      expect("a turned view maps document to screen and back", std::fabs(back.x - 37.0f) < 0.01f && std::fabs(back.y - 91.0f) < 0.01f);
      const ImVec2 sx = ToScreen(doc, 1.0f, 0.0f), so = ToScreen(doc, 0.0f, 0.0f);
      expect("the view turns by the asked angle", std::fabs(std::atan2(sx.y - so.y, sx.x - so.x) - 30.0f * 3.14159265f / 180.0f) < 1e-3f);
      expect("near a 15 degree step the turn settles on it", std::fabs(SnapViewAngle(46.5f * 3.14159265f / 180.0f) - 45.0f * 3.14159265f / 180.0f) < 1e-5f);
      sOp = Op();
      BeginOp(doc, 22, ImVec2(600, 300), nullptr);
      sOp.angle0 = sViewAngle;
      const ImVec2 panTurn = sPan;
      TurnViewAbout(doc, 1.0f, ImVec2(400, 300));
      CancelOp(doc);
      expect("Esc during a turn puts the view back", std::fabs(sViewAngle - 30.0f * 3.14159265f / 180.0f) < 1e-5f && std::fabs(sPan.x - panTurn.x) < 1e-3f && std::fabs(sPan.y - panTurn.y) < 1e-3f);
      sOp = Op();
      sZoom = zoom0, sPan = pan0, sViewAngle = angle0, sViewMn = mn0, sViewMx = mx0;
   }
   // Swatches round-trip through their saved text; recent colours keep ten, newest first, no repeats.
   {
      const std::string keep = gView2DSwatches;
      const std::vector<Rgb> was = sRecentCols;
      SetSwatches({ Rgb{ { 1.0f, 0.5f, 0.0f } }, Rgb{ { 0.0f, 0.0f, 1.0f } } });
      const std::vector<Rgb> sw = Swatches();
      expect("swatches save and load", gView2DSwatches == "ff8000,0000ff" && sw.size() == 2 && std::fabs(sw[0].c[1] - 128 / 255.0f) < 1e-4f);
      sRecentCols.clear();
      for (int i = 0; i < 12; i++)
      {
         const float c[3] = { i / 12.0f, 0.0f, 0.0f };
         NoteColourUsed(c);
      }
      const float again[3] = { 5 / 12.0f, 0.0f, 0.0f }, last[3] = { 11 / 12.0f, 0.0f, 0.0f };
      NoteColourUsed(again);
      expect("recent colours keep ten, newest first, no repeats", sRecentCols.size() == 10 && SameColour(sRecentCols[0].c, again)
                                                                   && !SameColour(sRecentCols[6].c, again) && SameColour(sRecentCols[1].c, last));
      gView2DSwatches = keep;
      sRecentCols = was;
   }
   // Free transform: drags inside it share one undo step; Esc puts the layer back, Enter keeps it.
   if (Layer* a = Active(doc))
   {
      const int id = a->id;
      const float x0 = a->x;
      const size_t depth = gUndoStack.size();
      BeginFreeTransform(doc);
      expect("Cmd+T starts a free transform", FreeTransformOn(doc) && gUndoStack.size() == depth + 1);
      BeginOp(doc, 1, ImVec2(0, 0), "Move layer");
      sOp = Op();
      BeginOp(doc, 3, ImVec2(0, 0), "Rotate layer");
      sOp = Op();
      doc->FindLayer(id)->x = x0 + 40.0f;
      expect("drags inside a free transform add no steps of their own", gUndoStack.size() == depth + 1);
      EndFreeTransform(doc, false);
      expect("Esc puts the layer back and drops the step", doc->FindLayer(id)->x == x0 && gUndoStack.size() == depth && !FreeTransformOn(doc));
      BeginFreeTransform(doc);
      doc->FindLayer(id)->x = x0 + 40.0f;
      EndFreeTransform(doc, true);
      expect("Enter keeps the transform as one step", doc->FindLayer(id)->x == x0 + 40.0f && gUndoStack.size() == depth + 1);
      doc->FindLayer(id)->x = x0;
   }
   // Free transform from any tool goes back to it; flips and quarter turns undo themselves; the modes pull corners.
   if (Layer* a = Active(doc); a != nullptr && Flippable(*a) && !a->is3d)
   {
      const int id = a->id;
      const Layer keep = *a;
      a->angle = a->shear = 0.0f;   // upright, so the corner checks read in document axes
      a->warpOn = a->flipX = a->flipY = false;
      std::fill(std::begin(a->warp), std::end(a->warp), 0.0f);
      const Layer upright = *a;
      auto near = [](float p, float q) { return std::fabs(p - q) < 0.05f; };
      auto corners = [&](float c[8]) { doc->Corners(*doc->FindLayer(id), c); };
      SelectOnly(id);
      sTool = kBrush;
      BeginFreeTransform(doc);
      expect("Cmd+T from the brush shows the box", FreeTransformOn(doc) && sTool == kMove);
      EndFreeTransform(doc, true);
      expect("ending it goes back to the brush", sTool == kBrush);
      float c0[8], c1[8];
      // A cut-out flips in place: the pivot is what the layer shows, not its whole (document-sized) box.
      static int sTestFrame = 1 << 24;
      float v0lo[2], v0hi[2], v1lo[2], v1hi[2];
      const bool seen0 = doc->VisibleBounds({ id }, ++sTestFrame, v0lo, v0hi);
      corners(c0);   // after the render: a node layer's box is the document until its source has a size
      FlipSelection(doc, false);
      corners(c1);
      const bool seen1 = doc->VisibleBounds({ id }, ++sTestFrame, v1lo, v1hi);
      expect("Flip Horizontal mirrors the layer where its pixels are",
             seen0 && seen1 && std::fabs(v0lo[0] - v1lo[0]) < 1.5f && std::fabs(v0hi[0] - v1hi[0]) < 1.5f
                && std::fabs(v0lo[1] - v1lo[1]) < 1.5f && std::fabs(v0hi[1] - v1hi[1]) < 1.5f && near(c1[1], c0[3]));
      FlipSelection(doc, false);
      FlipSelection(doc, true);
      FlipSelection(doc, true);
      corners(c1);
      bool same = true;
      for (int i = 0; i < 8; i++)
         same &= near(c0[i], c1[i]);
      expect("two flips each way put it back", same && !doc->FindLayer(id)->flipX && !doc->FindLayer(id)->flipY);
      for (int i = 0; i < 4; i++)
         TurnSelection(doc, 90.0f);
      corners(c1);
      same = true;
      for (int i = 0; i < 8; i++)
         same &= std::fabs(c0[i] - c1[i]) < 0.5f;
      expect("four quarter turns put it back", same);
      *doc->FindLayer(id) = upright;
      if (CanWarp(doc))
      {
         BeginFreeTransform(doc);
         sFreeMode = kFreeSkew;
         corners(c0);
         BeginOp(doc, 18, ImVec2(0, 0), nullptr);
         sOp.warpHandle = 2;   // top-right corner
         sOp.warpStyle = 1;
         sOp.docStart = ImVec2(0, 0);
         WarpActive(doc, ToScreen(doc, 30.0f, 4.0f));
         corners(c1);
         expect("Skew slides a corner along one edge", near(c1[3], c0[3]) && std::fabs(c1[2] - c0[2] - 30.0f) < 1.0f && near(c1[0], c0[0]));
         sOp = Op();
         *doc->FindLayer(id) = upright;
         BeginOp(doc, 18, ImVec2(0, 0), nullptr);
         sOp.warpHandle = 1;   // top edge
         sOp.warpStyle = 2;
         sOp.docStart = ImVec2(0, 0);
         WarpActive(doc, ToScreen(doc, 5.0f, 20.0f));
         corners(c1);
         expect("Distort moves an edge where it is dragged", near(c1[1] - c0[1], 20.0f) && near(c1[3] - c0[3], 20.0f) && near(c1[7], c0[7]));
         sOp = Op();
         EndFreeTransform(doc, false);
         expect("Esc after a distort puts the layer back", !doc->FindLayer(id)->warpOn && sFreeMode == kFreeAny);
      }
      *doc->FindLayer(id) = keep;
      sTool = kMove;
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

   // Tool strip: a fresh profile shows the essentials only; every other tool waits in the picker.
   {
      const std::string keepTools = gView2DTools;
      gView2DTools.clear();
      sStripParsed = "\x01";
      ParseStrip();
      int shownN = 0, essentialN = 0;
      bool offOk = true;
      for (int i = 0; i < kToolCount; i++)
      {
         shownN += !sStripHidden[i];
         essentialN += Essential(kTools[i].tool);
         offOk = offOk && (sStripHidden[i] == !Essential(kTools[i].tool));
      }
      expect("a fresh tool strip shows the essentials only", offOk && shownN == essentialN && shownN < kToolCount / 2 + 2);
      // Every tool is in exactly one picker group, so none can be lost from the picker.
      int grouped = 0;
      for (const std::vector<ToolGroup>& col : PickerColumns())
         for (const ToolGroup& g : col)
            grouped += (int)g.tools.size();
      expect("every tool sits in the picker", grouped == kToolCount);
      gView2DTools = keepTools;
      sStripParsed = "\x01";
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
      // A group scales and turns as one box; a click on a layer inside picks the group.
      {
         const int g = doc->GroupLayers({ a, b });
         doc->Commit();
         expect("a click inside a group picks the group", OuterGroup(doc, HitLayer(doc, ImVec2(W * 0.09f, H * 0.12f))) == g);
         const int tool0 = sTool;
         sTool = kMove;
         sSel = { g };
         expect("a picked group shows one transform box", GroupTarget(doc));
         float lo[2], hi[2], lo2[2], hi2[2];
         SelBounds(doc, lo, hi);
         BeginOp(doc, 20, ToScreen(doc, hi[0], hi[1]), nullptr);
         SelBounds(doc, sOp.g0, sOp.g0 + 2);
         sOp.hx = sOp.hy = 1.0f;
         ScaleGroup(doc, ToScreen(doc, lo[0] + (hi[0] - lo[0]) * 0.5f, lo[1] + (hi[1] - lo[1]) * 0.5f));
         doc->Commit();
         SelBounds(doc, lo2, hi2);
         const float kw = (hi2[0] - lo2[0]) / (hi[0] - lo[0]), kh = (hi2[1] - lo2[1]) / (hi[1] - lo[1]);
         expect("group corner drag halves the group, top-left stays", std::fabs(kw - 0.5f) < 0.06f && std::fabs(kh - 0.5f) < 0.06f &&
                                                                  std::fabs(lo2[0] - lo[0]) < 4.0f && std::fabs(lo2[1] - lo[1]) < 4.0f);
         sOp = Op();
         const ImVec2 c = ToScreen(doc, (lo2[0] + hi2[0]) * 0.5f, (lo2[1] + hi2[1]) * 0.5f);
         BeginOp(doc, 21, ImVec2(c.x + 50.0f, c.y), nullptr);
         SelBounds(doc, sOp.g0, sOp.g0 + 2);
         RotateGroup(doc, ImVec2(c.x, c.y + 50.0f));
         doc->Commit();
         const Layer* la = doc->FindLayer(a);
         const Layer* lb = doc->FindLayer(b);
         expect("group turn turns every layer in it", la != nullptr && lb != nullptr && std::fabs(la->angle - 90.0f) < 1.0f && std::fabs(lb->angle - 90.0f) < 1.0f);
         sOp = Op();
         sTool = tool0;
         doc->Ungroup(g);
      }
      expect("a click on empty paint picks neither paint layer", empty != a && empty != b);
   }
   // Flatten Group flattens that group alone; Select Layer Pixels takes what a layer shows; a cut-out flips in place.
   {
      const size_t n0 = doc->Layers().size();
      const int a = doc->AddPaintLayer();
      stroke(a, W * 0.2f, H * 0.2f, W * 0.25f, H * 0.2f);
      const int b = doc->AddPaintLayer();
      stroke(b, W * 0.3f, H * 0.3f, W * 0.35f, H * 0.3f);
      const int c = doc->AddPaintLayer();
      stroke(c, W * 0.6f, H * 0.7f, W * 0.7f, H * 0.7f);
      doc->Commit();
      const int g = doc->GroupLayers({ a, b });
      doc->Commit();
      SelectOnly(g);
      sPending.what = Pending::FlattenGroup;
      RunPending(doc);
      doc->Commit();
      const Layer* flat = Active(doc);
      const int flatId = flat != nullptr ? flat->id : 0;
      expect("Flatten Group makes one pixel layer of the group", flat != nullptr && flat->kind == Kind::Paint && doc->FindLayer(g) == nullptr
                                                                  && doc->FindLayer(a) == nullptr && doc->FindLayer(b) == nullptr);
      expect("Flatten Group leaves the other layers alone", doc->FindLayer(c) != nullptr && doc->Layers().size() == n0 + 2);
      // Stamp Visible and Copy Merged (Photoshop): one picture of what shows; the layers stay.
      {
         const size_t before = doc->Layers().size();
         SelectOnly(c);
         sPending.what = Pending::StampVisible;
         RunPending(doc);
         doc->Commit();
         const Layer* st = Active(doc);
         expect("Stamp Visible adds one pixel layer above the active one", doc->Layers().size() == before + 1 && st != nullptr &&
                                                                             st->kind == Kind::Paint && st->name == "Stamp" &&
                                                                             doc->FindLayer(c) != nullptr);
         if (st != nullptr && st->id != c)
            doc->RemoveLayer(st->id);
         doc->Commit();
         SelectOnly(c);
         sClip.clear();
         sPending.what = Pending::CopyMerged;
         RunPending(doc);
         expect("Copy Merged fills the clip and leaves the layers alone", !sClip.empty() && doc->Layers().size() == before);
         sClip.clear();
      }
      static int sTestFrame2 = 1 << 23;
      float vlo[2], vhi[2], slo[2], shi[2];
      const bool seen = doc->VisibleBounds({ c }, ++sTestFrame2, vlo, vhi);
      SelectOnly(c);
      SelectLayerPixels(doc, c, Scene2DNode::kSelNew);
      expect("Select Layer Pixels selects what the layer shows", seen && doc->SelectionBounds(slo, shi)
                                                                 && std::fabs(slo[0] - vlo[0]) < 2.0f && std::fabs(shi[0] - vhi[0]) < 2.0f
                                                                 && std::fabs(slo[1] - vlo[1]) < 2.0f && std::fabs(shi[1] - vhi[1]) < 2.0f);
      doc->Deselect();
      FlipSelection(doc, false);
      doc->Commit();
      float flo[2], fhi[2];
      const bool seenF = doc->VisibleBounds({ c }, ++sTestFrame2, flo, fhi);
      expect("a cut-out on a full-size layer flips where it is", seen && seenF && std::fabs(flo[0] - vlo[0]) < 2.0f && std::fabs(fhi[0] - vhi[0]) < 2.0f);
      if (flatId != 0)
         doc->RemoveLayer(flatId);
      doc->RemoveLayer(c);
      doc->Commit();
      sSel.clear();
   }
   // A marquee on a paint layer: Free Transform, Flip and Rotate take the selected pixels only (lift, move, stamp
   // back); Esc puts them back; the marquee follows; the whole action is one undo step.
   {
      static int sLiftFrame = 1 << 22;
      const float red[3] = { 1, 0, 0 };
      const int p = doc->AddPaintLayer();
      doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
      doc->FillPixels(p, red, 1.0f);
      doc->SelectRect(300, 100, 400, 200, Scene2DNode::kSelNew, 0, false);
      doc->FillPixels(p, red, 1.0f);
      doc->Commit();
      SelectOnly(p);
      const size_t n0 = doc->Layers().size();
      const std::vector<std::string> tiles0 = doc->FindLayer(p)->tiles;
      auto bounds = [&](float lo[2], float hi[2]) { return doc->VisibleBounds({ p }, ++sLiftFrame, lo, hi); };
      float lo[2], hi[2];
      // Free transform, move 50 right, Enter.
      doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
      BeginFreeTransform(doc);
      const bool lifted = sFloat.id != 0 && sFloat.src == p;
      if (Layer* f = sFloat.id != 0 ? doc->FindLayer(sFloat.id) : nullptr)
         f->x += 50.0f;
      EndFreeTransform(doc, true);
      doc->Commit();
      const Layer* act = Active(doc);
      const bool b1 = bounds(lo, hi);
      float slo[2], shi[2];
      const bool s1 = doc->SelectionBounds(slo, shi);
      expect("lift: free transform with a marquee lifts the selected pixels", lifted);
      expect("lift: moved pixels land in the layer (150..400)", b1 && std::fabs(lo[0] - 150) < 2 && std::fabs(hi[0] - 400) < 2);
      expect("lift: no extra layer, the source stays active", doc->Layers().size() == n0 && act != nullptr && act->id == p);
      expect("lift: the marquee follows the pixels (150..250)", s1 && std::fabs(slo[0] - 150) < 2 && std::fabs(shi[0] - 250) < 2);
      // Same, then Esc: nothing changes.
      doc->FindLayer(p)->tiles = tiles0;
      doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
      doc->Commit();
      BeginFreeTransform(doc);
      if (Layer* f = sFloat.id != 0 ? doc->FindLayer(sFloat.id) : nullptr)
         f->x += 50.0f;
      EndFreeTransform(doc, false);
      doc->Commit();
      expect("lift: Esc puts the pixels back, no extra layer", doc->FindLayer(p) != nullptr && doc->FindLayer(p)->tiles == tiles0 &&
                                                                doc->Layers().size() == n0 && sFloat.id == 0);
      // Flip Horizontal with a marquee 100..300 over A: A lands at 200..300, B stays.
      doc->SelectRect(100, 100, 300, 200, Scene2DNode::kSelNew, 0, false);
      SelectOnly(p);
      FlipSelection(doc, false);
      doc->Commit();
      const bool b2 = bounds(lo, hi);
      expect("lift: flip with a marquee flips inside it (A -> 200..300)", b2 && std::fabs(lo[0] - 200) < 2 && std::fabs(hi[0] - 400) < 2 &&
                                                                         doc->Layers().size() == n0);
      // Rotate 90 with a marquee: one undo step brings it all back.
      doc->FindLayer(p)->tiles = tiles0;
      doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
      SelectOnly(p);
      doc->Commit();
      const std::string t0 = doc->DocText();
      const size_t u0 = gUndoStack.size();
      TurnSelection(doc, 90.0f);
      doc->Commit();
      const bool changed = doc->DocText() != t0;
      expect("lift: rotate with a marquee is one undo step", changed && gUndoStack.size() == u0 + 1 && doc->Layers().size() == n0);
      Undo();
      doc = FindDoc();
      expect("lift: one undo after rotate brings it all back", doc != nullptr && doc->DocText() == t0);
      if (doc == nullptr)
         return fails;
      doc->Deselect();
      doc->RemoveLayer(p);
      doc->Commit();
      sSel.clear();
   }
   // Parity fixes against PhotoCraft's rules (2026-11-18): one check per fixed bug.
   {
      static int sParFrame = 1 << 21;
      const int W = doc->Doc().w, H = doc->Doc().h;
      const float red[3] = { 1, 0, 0 }, green[3] = { 0, 1, 0 };
      auto pixel = [&](const std::vector<int>& set, int x, int y, uint8_t out[4]) {
         std::vector<uint8_t> rgba;
         if (!doc->RenderSet(set, ++sParFrame, rgba) || rgba.size() < (size_t)W * H * 4)
            return false;
         std::copy_n(&rgba[((size_t)y * W + x) * 4], 4, out);
         return true;
      };
      auto undoBack = [&]() {
         Undo();
         doc = FindDoc();
         return doc != nullptr;
      };
      auto about = [](float a, float b) { return std::fabs(a - b) < 2.0f; };
      float lo[2], hi[2];
      // Bug 1: Canvas Size, Crop and Image Size all drop the selection; undo brings it back.
      doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
      doc->Commit();
      PushUndoCheckpoint("test canvas size");
      doc->CanvasSize(W + 100, H, 4);
      expect("parity: canvas size drops the selection", !doc->HasSelection());
      if (!undoBack())
         return fails;
      PushUndoCheckpoint("test crop");
      doc->CropTo(50, 0, (float)W, (float)H);
      expect("parity: crop drops the selection", !doc->HasSelection());
      if (!undoBack())
         return fails;
      PushUndoCheckpoint("test image size");
      doc->ImageSize(W / 2, H / 2);
      expect("parity: image size drops the selection", !doc->HasSelection());
      if (!undoBack())
         return fails;
      expect("parity: undo brings size and selection back", doc->Doc().w == W && doc->Doc().h == H && doc->HasSelection());
      // Bug 14: a bucket region the selection hides wholly adds no undo step and changes nothing.
      {
         const int p = doc->AddPaintLayer();
         doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
         doc->FillPixels(p, red, 1.0f);
         doc->SelectRect(0, 0, 50, 50, Scene2DNode::kSelNew, 0, false);
         doc->Commit();
         SelectOnly(p);
         const auto keepTool = sTool;
         const int keepTol = sTolerance;
         const bool keepAll = sAllLayers, keepCont = sContiguous;
         sTolerance = 0;
         sAllLayers = false;
         sContiguous = true;
         const std::vector<std::string> t0 = doc->FindLayer(p)->tiles;
         const size_t u0 = gUndoStack.size();
         FillAt(doc, ImVec2(150, 150));
         expect("parity: a bucket region outside the selection adds no undo step", gUndoStack.size() == u0 && doc->FindLayer(p)->tiles == t0);
         // Bug 12: transparent pixels match each other whatever colour they hide. Erase the red square
         // (alpha 0, colour kept), then a bucket on the clear part floods through it.
         doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
         doc->ClearPixels(p);
         doc->Deselect();
         doc->Commit();
         doc->Bucket(p, 10, 10, green, 1.0f, 0, true, false);
         uint8_t c[4] = {};
         expect("parity: a bucket floods through erased pixels", pixel({ p }, 150, 150, c) && c[1] > 200 && c[0] < 40 && c[3] > 200);
         sTool = keepTool;
         sTolerance = keepTol;
         sAllLayers = keepAll;
         sContiguous = keepCont;
         doc->RemoveLayer(p);
         doc->Commit();
      }
      // Bug 11: Feather is a Gaussian with sigma = radius; Expand is round (Euclidean), not square.
      {
         doc->SelectRect(100, 100, 300, 300, Scene2DNode::kSelNew, 0, false);
         doc->ModifySelection(5, 10);
         const float in = doc->SelectionAt(110, 200), out = doc->SelectionAt(90, 200);
         expect("parity: feather spreads one radius as one sigma (0.84 / 0.16)", in > 0.76f && in < 0.92f && out > 0.08f && out < 0.24f);
         doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
         doc->ModifySelection(0, 10);
         expect("parity: expand rounds the corners", doc->SelectionAt(205, 205) > 0.5f && doc->SelectionAt(208, 208) < 0.5f &&
                                                      doc->SelectionAt(208, 150) > 0.5f);
         doc->Deselect();
         doc->Commit();
      }
      // Bug 8: Patch in Destination mode ("selection is source") moves the marquee with the pixels.
      {
         const int p = doc->AddPaintLayer();
         doc->SelectRect(0, 0, (float)W, (float)H, Scene2DNode::kSelNew, 0, false);
         doc->FillPixels(p, green, 1.0f);
         doc->SelectRect(100, 100, 150, 150, Scene2DNode::kSelNew, 0, false);
         doc->FillPixels(p, red, 1.0f);
         doc->Commit();
         const bool keepSrc = sPatchSrc, keepClear = sPatchClear;
         sPatchSrc = true;
         sPatchClear = false;
         const bool ok = PatchApply(doc, p, 100.0f, 0.0f);
         expect("parity: patch destination mode moves the marquee (200..250)", ok && doc->SelectionBounds(lo, hi) && about(lo[0], 200) && about(hi[0], 250));
         sPatchSrc = false;
         doc->SelectRect(100, 100, 150, 150, Scene2DNode::kSelNew, 0, false);
         const bool ok2 = PatchApply(doc, p, 100.0f, 0.0f);
         expect("parity: patch source mode keeps the marquee", ok2 && doc->SelectionBounds(lo, hi) && about(lo[0], 100));
         sPatchSrc = keepSrc;
         sPatchClear = keepClear;
         doc->Deselect();
         doc->RemoveLayer(p);
         doc->Commit();
      }
      // Bug 7: the Eraser on a transparency-locked layer paints the background colour.
      {
         const int p = doc->AddPaintLayer();
         doc->FindLayer(p)->lock = Scene2DNode::kLockAlpha;
         const auto keepTool = sTool;
         sTool = kEraser;
         BrushEngine::Brush b;
         b.mode = BrushEngine::kErase;
         const BrushEngine::Brush e = EraseLocked(doc, p, 0, b);
         doc->FindLayer(p)->lock = 0;
         const BrushEngine::Brush f = EraseLocked(doc, p, 0, b);
         sTool = keepTool;
         expect("parity: eraser on a transparency lock paints the background colour",
                e.mode == BrushEngine::kNormal && e.color[0] == sBg[0] && e.color[1] == sBg[1] && e.color[2] == sBg[2]);
         expect("parity: eraser without the lock still erases", f.mode == BrushEngine::kErase);
         doc->RemoveLayer(p);
         doc->Commit();
      }
      // Bugs 2, 5, 6: group locks reach inside; a new layer goes inside an open group; clipping is inherited and
      // released when its base goes.
      {
         const int a = doc->AddPaintLayer();
         const int g = doc->GroupLayers({ a });
         doc->FindLayer(g)->lock = Scene2DNode::kLockAll;
         doc->Commit();
         const Layer* al = doc->FindLayer(a);
         expect("parity: a locked group locks what is inside", al != nullptr && (LockOf(doc, *al) & Scene2DNode::kLockAll) != 0 && !Movable(doc, *al));
         doc->FindLayer(g)->lock = 0;
         doc->FindLayer(g)->open = true;
         doc->Commit();
         SelectOnly(g);
         sPending.what = Pending::AddPaint;
         RunPending(doc);
         const Layer* n = Active(doc);
         const int nid = n != nullptr ? n->id : 0;
         expect("parity: a new layer goes inside an open group", n != nullptr && n->parent == g);
         int mid = 0, top = 0;
         if (nid != 0)
         {
            // PhotoCraft new_layer_joins_the_group_it_lands_in: "a nid*" + new with a active -> "a N* nid*";
            // with the top clipped layer active -> "... nid* M", M unclipped.
            doc->FindLayer(nid)->clip = true;
            doc->Commit();
            SelectOnly(a);
            sPending.what = Pending::AddPaint;
            RunPending(doc);
            const Layer* m = Active(doc);
            mid = m != nullptr && m->id != a ? m->id : 0;
            expect("parity: a new layer under a clipped one joins its clip group", mid != 0 && doc->FindLayer(mid)->clip);
            SelectOnly(nid);
            sPending.what = Pending::AddPaint;
            RunPending(doc);
            const Layer* t = Active(doc);
            top = t != nullptr && t->id != nid ? t->id : 0;
            expect("parity: a new layer above the top of a clip group stays unclipped", top != 0 && !doc->FindLayer(top)->clip);
            SelectOnly(a);
            DeleteSelected(doc);
            const Layer* nl = doc->FindLayer(nid);
            const Layer* ml = mid != 0 ? doc->FindLayer(mid) : nullptr;
            expect("parity: deleting a clip base releases its clipped layers", nl != nullptr && ml != nullptr && !nl->clip && !ml->clip);
            // Model rules, as PhotoCraft arrange_joins_and_leaves_groups: "B C* U", C forward -> "B U C".
            const int B = doc->AddPaintLayer(), C = doc->AddPaintLayer(B), U = doc->AddPaintLayer(C);
            doc->FindLayer(C)->clip = true;
            doc->MoveLayer(C, U, false);
            expect("parity: a clipped layer moved off its group is let go", !doc->FindLayer(C)->clip);
            // Grouping a base with its clipped layer keeps the clip inside the new group.
            doc->MoveLayer(C, B, false);
            doc->FindLayer(C)->clip = true;
            const int G2 = doc->GroupLayers({ B, C });
            expect("parity: grouping a clip group keeps it", G2 != 0 && doc->FindLayer(C)->clip && !doc->FindLayer(B)->clip);
            // A clipped layer left with nothing below it is let go.
            doc->RemoveLayer(B);
            expect("parity: deleting a base releases the layer clipped to it", doc->FindLayer(C) != nullptr && !doc->FindLayer(C)->clip);
            for (int id : { G2, U })
               if (doc->FindLayer(id) != nullptr)
                  doc->RemoveLayer(id);
            doc->Commit();
         }
         for (int id : { top, mid, nid, a, g })
            if (id != 0 && doc->FindLayer(id) != nullptr)
               doc->RemoveLayer(id);
         doc->Commit();
         sSel.clear();
      }
      // Bug 9: a fill layer made with a selection is masked by it.
      {
         doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
         doc->Commit();
         sPending.what = Pending::AddFill;
         RunPending(doc);
         const Layer* f = Active(doc);
         expect("parity: a fill layer takes the selection as its mask", f != nullptr && f->kind == Kind::Fill && f->HasPixelMask());
         expect("parity: the selection is dropped once it is the mask", !doc->HasSelection());
         if (f != nullptr && f->kind == Kind::Fill)
            doc->RemoveLayer(f->id);
         doc->Deselect();
         doc->Commit();
         sSel.clear();
      }
      // Bugs 3, 4: Merge keeps the lower layer's blend and the pixels past the canvas.
      {
         const int a = doc->AddPaintLayer();
         doc->FillPixels(a, red, 1.0f);
         const int b = doc->AddPaintLayer(a);
         doc->FillPixels(b, green, 1.0f);
         doc->FindLayer(a)->blend = 2;
         doc->FindLayer(b)->x += (float)W * 0.5f;   // half of it off the right edge
         doc->Commit();
         const int m = doc->MergeLayers({ a, b }, ++sParFrame);
         const Layer* ml = doc->FindLayer(m);
         expect("parity: merge keeps the lower layer's blend", ml != nullptr && ml->blend == 2);
         expect("parity: merge keeps pixels past the canvas", ml != nullptr && ml->pw >= W + W / 2 - 2);
         if (ml != nullptr)
            doc->RemoveLayer(m);
         for (int id : { a, b })
            if (doc->FindLayer(id) != nullptr)
               doc->RemoveLayer(id);
         doc->Commit();
         sSel.clear();
      }
      // Bug 13: Threshold weighs 0.30/0.59/0.11 (green 0.59 sits under 0.65); Hue turns the HSL hue (red +60 = yellow).
      {
         const int p = doc->AddPaintLayer();
         doc->FillPixels(p, green, 1.0f);
         Layer& t = doc->AddLayer(Kind::Adjust, 0, p);
         t.adjType = Scene2DNode::kAdjThreshold;
         t.adjAmt = t.adjTo = 0.65f;
         const int tid = t.id;
         doc->Commit();
         uint8_t c[4] = {};
         expect("parity: threshold uses Photoshop's grey (green is dark at 0.65)", pixel({ p, tid }, 10, 10, c) && c[1] < 40);
         doc->RemoveLayer(tid);
         doc->FillPixels(p, red, 1.0f);
         Layer& h = doc->AddLayer(Kind::Adjust, 0, p);
         h.adjType = Scene2DNode::kAdjHue;
         h.adjAmt = h.adjTo = 60.0f;
         const int hid = h.id;
         doc->Commit();
         expect("parity: hue +60 turns red into yellow", pixel({ p, hid }, 10, 10, c) && c[0] > 240 && c[1] > 240 && c[2] < 15);
         doc->RemoveLayer(hid);
         // Contrast and Levels follow Photoshop's measured curves (PhotoCraft compose tests): contrast +50 takes 64 to
         // 52 and 192 to 204; Levels gamma 2.0 takes 32 to 90 (the shadow toe) and 128 to 181.
         auto toneAt = [&](int type, float amt, int in) {
            const float g[3] = { in / 255.0f, in / 255.0f, in / 255.0f };
            doc->FillPixels(p, g, 1.0f);
            Layer& a = doc->AddLayer(Kind::Adjust, 0, p);
            a.adjType = type;
            a.adjAmt = a.adjTo = amt;
            const int aid = a.id;
            doc->Commit();
            uint8_t o[4] = {};
            const int got = pixel({ p, aid }, 10, 10, o) ? o[1] : -1;
            doc->RemoveLayer(aid);
            doc->Commit();
            return got;
         };
         const int c64 = toneAt(Scene2DNode::kAdjContrast, 0.5f, 64), c192 = toneAt(Scene2DNode::kAdjContrast, 0.5f, 192);
         expect("parity: contrast +50 follows Photoshop's S-curve", std::abs(c64 - 52) <= 3 && std::abs(c192 - 204) <= 3);
         const int l32 = toneAt(Scene2DNode::kAdjLevels, 2.0f, 32), l128 = toneAt(Scene2DNode::kAdjLevels, 2.0f, 128);
         expect("parity: levels gamma 2 has Photoshop's shadow toe", std::abs(l32 - 90) <= 4 && std::abs(l128 - 181) <= 4);
         // Black & White, Photoshop's defaults: red reads 102 (reds 40%), yellow 153 (yellows 60%).
         auto bwOf = [&](const float col[3]) {
            doc->FillPixels(p, col, 1.0f);
            Layer& a = doc->AddLayer(Kind::Adjust, 0, p);
            a.adjType = Scene2DNode::kAdjBlackWhite;
            a.adjAmt = a.adjTo = 1.0f;
            a.adjP = Scene2DNode::AdjParamDefault(Scene2DNode::kAdjBlackWhite);
            const int aid = a.id;
            doc->Commit();
            uint8_t o[4] = {};
            const int got = pixel({ p, aid }, 10, 10, o) ? o[0] : -1;
            doc->RemoveLayer(aid);
            doc->Commit();
            return got;
         };
         const float yel[3] = { 1.0f, 1.0f, 0.0f };
         const int bwr = bwOf(red), bwy = bwOf(yel);
         expect("parity: black & white weighs red 40% and yellow 60%", std::abs(bwr - 102) <= 3 && std::abs(bwy - 153) <= 3);
         // Colour Overlay ignores Fill but not Opacity (PhotoCraft color_overlay_ignores_fill_opacity_but_not_opacity).
         {
            doc->FillPixels(p, red, 1.0f);
            Layer* pl = doc->FindLayer(p);
            pl->fx[Scene2DNode::kFxOverlay] = Scene2DNode::FxDefault(Scene2DNode::kFxOverlay);
            pl->fx[Scene2DNode::kFxOverlay].on = true;
            const float gc[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
            std::copy(gc, gc + 4, pl->fx[Scene2DNode::kFxOverlay].c);
            pl->fill = 0.0f;
            doc->Commit();
            uint8_t o[4] = {};
            expect("parity: colour overlay shows at fill 0", pixel({ p }, 10, 10, o) && o[1] > 240 && o[0] < 15);
            pl = doc->FindLayer(p);
            pl->fx[Scene2DNode::kFxOverlay].on = false;
            pl->fill = 1.0f;
            doc->Commit();
         }
         // Colour Balance, Preserve Luminosity: shadows red +50 blue +50 on grey 128 reads 128/97/128 in Photoshop.
         const float grey[3] = { 128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f };
         doc->FillPixels(p, grey, 1.0f);
         Layer& cb = doc->AddLayer(Kind::Adjust, 0, p);
         cb.adjType = Scene2DNode::kAdjColorBalance;
         cb.adjAmt = cb.adjTo = 1.0f;
         cb.adjP.assign(9, 0.0f);
         cb.adjP[0] = cb.adjP[2] = 0.5f;
         const int cbid = cb.id;
         doc->Commit();
         expect("parity: colour balance matches Photoshop (shadows R+50 B+50 on grey)",
                pixel({ p, cbid }, 10, 10, c) && std::abs(c[0] - 128) <= 3 && std::abs(c[1] - 97) <= 3 && std::abs(c[2] - 128) <= 3);
         doc->RemoveLayer(cbid);
         doc->RemoveLayer(p);
         doc->Commit();
      }
      // Anti-alias: an ellipse marquee's edge is about one pixel of coverage, not a 6 px blur; the rectangle stays crisp.
      {
         doc->SelectRect(100.3f, 100.3f, 300.7f, 200.7f, Scene2DNode::kSelNew, 0, true, true);
         const std::vector<uint8_t>& m = doc->SelectionPixels();
         const int rowY = H - 1 - 150;
         int soft = 0;
         for (int x = 60; x < 340 && !m.empty(); x++)
         {
            const uint8_t v = m[(size_t)rowY * W + x];
            soft += v > 0 && v < 255;
         }
         expect("parity: an anti-aliased ellipse has a one-pixel edge", soft >= 1 && soft <= 4);
         doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false, true);
         const std::vector<uint8_t>& r = doc->SelectionPixels();
         int rsoft = 0;
         for (uint8_t v : r)
            rsoft += v > 0 && v < 255;
         expect("parity: an anti-aliased rectangle stays crisp", rsoft == 0);
         // Border is a ring by true distance: round outside the corners, not a square window.
         doc->SelectRect(100, 100, 300, 300, Scene2DNode::kSelNew, 0, false);
         doc->ModifySelection(3, 20);
         expect("parity: border is round at the corners",
                doc->SelectionAt(94, 94) > 0.5f && doc->SelectionAt(91, 91) < 0.5f && doc->SelectionAt(200, 95) > 0.5f &&
                    doc->SelectionAt(200, 200) < 0.5f);
         doc->Deselect();
      }
      // Polygon and star take a corner radius (Photoshop): the diamond's top tip is cut round.
      {
         Layer& vl = doc->AddLayer(Kind::Vector, 0, 0);
         const int vid = vl.id;
         Scene2DNode::Shape s;
         s.type = Scene2DNode::Shape::kPolygon;
         s.sides = 4;
         s.x0 = s.y0 = 100;
         s.x1 = s.y1 = 300;
         s.fillC[0] = 1.0f;
         vl.shapes.push_back(s);
         doc->Commit();
         uint8_t a0[4] = {}, a1[4] = {}, m[4] = {};
         const bool sharp = pixel({ vid }, 200, 104, a0) && a0[3] > 128;
         if (Layer* vv = doc->FindLayer(vid))
            vv->shapes[0].radius = 40.0f;
         doc->Commit();
         expect("parity: a polygon's corner radius rounds its tips",
                sharp && pixel({ vid }, 200, 104, a1) && a1[3] < 64 && pixel({ vid }, 200, 200, m) && m[3] > 200);
         doc->RemoveLayer(vid);
         doc->Commit();
      }
      // Pass Through at 50%: an Invert inside still sees the red below, so red mixes with cyan to grey (Photoshop).
      {
         const int p = doc->AddPaintLayer();
         doc->FillPixels(p, red, 1.0f);
         Layer& inv = doc->AddLayer(Kind::Adjust, 0, p);
         inv.adjType = Scene2DNode::kAdjInvert;
         inv.adjAmt = inv.adjTo = 1.0f;
         const int iid = inv.id;
         const int g = doc->GroupLayers({ iid });
         if (Layer* gl = doc->FindLayer(g))
         {
            gl->blend = Scene2DNode::kPassThrough;
            gl->opacity = 0.5f;
         }
         doc->Commit();
         uint8_t c[4] = {};
         // The mix happens in the document's blend space: linear light by default (0.5 -> 188), gamma as Photoshop (128).
         const bool keepLinear = doc->Doc().linear;
         doc->Doc().linear = false;
         doc->Commit();
         expect("parity: a 50% pass-through group's adjustment still sees below",
                g != 0 && pixel({ p, g }, 10, 10, c) && std::abs(c[0] - 128) <= 6 && std::abs(c[1] - 128) <= 6 && std::abs(c[2] - 128) <= 6);
         doc->Doc().linear = true;
         doc->Commit();
         expect("panel: a 50% pass-through group mixes in linear light (188)",
                pixel({ p, g }, 10, 10, c) && std::abs(c[0] - 188) <= 6 && std::abs(c[1] - 188) <= 6 && std::abs(c[2] - 188) <= 6);
         doc->Doc().linear = keepLinear;
         doc->Commit();
         // A locked group locks what is in it: no fill reaches a layer inside.
         const int q = doc->AddPaintLayer();
         const int lg = doc->GroupLayers({ q });
         if (Layer* gl = doc->FindLayer(lg))
            gl->lock = Scene2DNode::kLockAll;
         expect("parity: a locked group's layer takes no fill", lg != 0 && !doc->FillPixels(q, red, 1.0f));
         for (int id : { lg, g, p })
            if (doc->FindLayer(id) != nullptr)
               doc->RemoveLayer(id);
         doc->Commit();
         sSel.clear();
      }
      // Transparency lock does not stop a lift; the arrows lift and move the selected pixels.
      {
         const int p = doc->AddPaintLayer();
         doc->FillPixels(p, red, 1.0f);
         doc->FindLayer(p)->lock = Scene2DNode::kLockAlpha;
         doc->SelectRect(100, 0, 200, (float)H, Scene2DNode::kSelNew, 0, false);
         doc->Commit();
         SelectOnly(p);
         const size_t n0 = doc->Layers().size();
         Nudge(doc, 30, 0);
         const bool lifted = sFloat.id != 0;
         DropFloat(doc);
         doc->Commit();
         expect("parity: an alpha-locked layer's selection lifts and nudges", lifted && doc->Layers().size() == n0);
         float slo[2], shi[2];
         expect("parity: the marquee moves with the nudged pixels", doc->SelectionBounds(slo, shi) && std::fabs(slo[0] - 130) < 2);
         doc->Deselect();
         doc->Commit();
         // The Background Eraser erases through the transparency lock (PhotoCraft: as on an unlocked layer).
         const int tool0 = sTool;
         sTool = kBgErase;
         SelectOnly(p);
         BrushEngine::Brush& br = ToolBrush();
         const BrushEngine::Brush br0 = br;
         br.size = 40.0f;
         br.hardness = 1.0f;
         br.opacity = br.flow = 1.0f;
         br.pressureSize = br.pressureOpacity = false;
         sHaveLast = false;
         const float yy = (float)(H / 2);
         BeginStroke(doc, ImVec2(400, yy), false);
         const bool painting = doc->Painting();
         for (int i = 1; i <= 32 && painting; i++)
            doc->PaintTo(400.0f + 100.0f * i / 32.0f, yy, 1.0f);
         if (painting)
            EndStroke(doc);
         doc->Commit();
         uint8_t e[4] = {};
         expect("parity: background eraser ignores the transparency lock", painting && pixel({ p }, 450, H / 2, e) && e[3] < 40);
         br = br0;
         sTool = tool0;
         doc->RemoveLayer(p);
         doc->Commit();
         sSel.clear();
      }
      // Bug 15: copy names, the pixel lock, text names, Duplicate Document, Group from Layers.
      expect("parity: copy names number up", Scene2DNode::CopyName("Layer") == "Layer copy" && Scene2DNode::CopyName("Layer copy") == "Layer copy 2" &&
                                                 Scene2DNode::CopyName("Layer copy 2") == "Layer copy 3");
      expect("parity: locking pixels clears and holds off the transparency lock",
             ToggledLock(Scene2DNode::kLockAlpha, Scene2DNode::kLockPixels) == Scene2DNode::kLockPixels &&
             ToggledLock(Scene2DNode::kLockPixels, Scene2DNode::kLockAlpha) == Scene2DNode::kLockPixels);
      {
         bool cut = false;
         const std::string utf = Scene2DNode::TextName("\n  \n\xC3\xA9\xC3\xA9\xC3\xA9", 5, &cut);   // three e-acute, 6 bytes
         expect("parity: text names skip blank lines and cut on a character", Scene2DNode::TextName("\n\nHello\nWorld", 28) == "Hello" &&
                                                                                 utf == "\xC3\xA9\xC3\xA9" && cut);
      }
      doc->SelectRect(100, 100, 200, 200, Scene2DNode::kSelNew, 0, false);
      doc->Commit();
      {
         const std::string dup = DocTextWithoutSelection(doc->DocText());
         expect("parity: a duplicated document starts with no selection", doc->DocText().find("\nsel ") != std::string::npos &&
                                                                           dup.find("\nsel ") == std::string::npos);
      }
      doc->Deselect();
      doc->Commit();
      {
         const int a = doc->AddPaintLayer();
         sSel = { a };
         sPending.what = Pending::Group;
         RunPending(doc);
         const Layer* g = Active(doc);
         expect("parity: group from layers starts collapsed", g != nullptr && g->kind == Kind::Group && !g->open);
         const int gid = g != nullptr ? g->id : 0;
         if (doc->FindLayer(a) != nullptr)
            doc->RemoveLayer(a);
         if (gid != 0 && doc->FindLayer(gid) != nullptr)
            doc->RemoveLayer(gid);
         doc->Commit();
         sSel.clear();
      }
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
      expect("it is a pixel layer centred on the drop point", pl != nullptr && pl->kind == Kind::Paint && std::fabs(pl->x - at.x) <= 1.0f &&
                                                             std::fabs(pl->y - at.y) <= 1.0f && !pl->is3d);
      expect("no node is spawned for a dropped picture", pl != nullptr && pl->uid == 0);
      n = doc->Layers().size();
      expect("a dropped SVG is one vector layer", DropFiles(doc, { svg }, at) == 1 && doc->Layers().size() == n + 1 && Active(doc)->kind == Kind::Vector);
      n = doc->Layers().size();
      const int addedModel = DropFiles(doc, { obj }, at);
      expect("a dropped model is one 3D layer", addedModel == 1 && doc->Layers().size() == n + 1 && Active(doc)->is3d);
      size_t nodes = gNodes.size();
      DropFiles(doc, { mp4 }, at);
      expect("a dropped video spawns a Video node", gNodes.size() == nodes + 1 && gNodes.back().typeName == "Video");
      expect("the Video node is on the canvas", !gNodes.empty() && !gNodes.back().hiddenFromCanvas);
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
      // The arm as the drag sees it (GizmoPoint), not a far point: in perspective the two can point different ways.
      ImVec2 zs;
      if (GizmoPoint(doc, at, ax[2], c, kGizmoPx, zs))
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
      expect("H7: the export frame is the live frame", worst <= 2);   // 2: software GL rounds one step apart
      expect("H7: the written PNG is the live frame", fileWorst <= 2);
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
   // Block 4 bug hunt: Esc cancels a drag, a click is no crop box, Delete clears, a document switch drops edit state.
   {
      const int id = doc->AddPaintLayer();
      stroke(id, W * 0.4f, H * 0.4f, W * 0.6f, H * 0.4f);
      doc->Commit();
      sSel = { id };
      const Layer was = *doc->FindLayer(id);
      const size_t depth = gUndoStack.size();
      sOp = Op();
      sOp.kind = 1;
      sOp.before = { was };
      sOp.l0 = was;
      sOp.undo0 = depth;
      PushUndoCheckpoint("Move");
      doc->FindLayer(id)->x += 40.0f;
      const bool cancelled = CancelOp(doc);
      expect("Esc puts a dragged layer back", cancelled && doc->FindLayer(id)->x == was.x && sOp.kind == 0);
      expect("Esc drops the drag's undo step", gUndoStack.size() == depth);

      const float crop[4] = { 10, 10, 100, 80 };
      std::copy(crop, crop + 4, sCrop);
      sOp.kind = 5;
      std::copy(crop, crop + 4, sOp.cropWas);
      sCrop[2] = 11.0f;
      expect("a crop box under 2 px is no box", !CropBoxOk());
      CancelOp(doc);
      expect("Esc restores the crop box", sCrop[2] == 100.0f && CropBoxOk());

      const float z = sZoom;
      const ImVec2 pan = sPan;
      sOp.kind = 19;
      sOp.zoom0 = z;
      sOp.pan0 = pan;
      sZoom = z * 3.0f;
      CancelOp(doc);
      expect("Esc restores the zoom", sZoom == z && sPan.x == pan.x);

      const std::vector<uint8_t> full = picture(doc);
      doc->SelectRect(W * 0.3f, H * 0.3f, W * 0.5f, H * 0.5f, 0, 0.0f, false);
      const bool cleared = ClearOrFill(doc, 0);
      const std::vector<uint8_t> cut = picture(doc);
      expect("Delete clears inside the selection", cleared && cut != full);
      doc->Deselect();
      const bool filled = ClearOrFill(doc, 4);
      expect("Fill covers the layer", filled && picture(doc) != cut);
      doc->quickMask = true;
      expect("no clear while Quick Mask is on", !ClearOrFill(doc, 0));
      doc->quickMask = false;

      sCropOn = true;
      sTextEdit = id;
      sOp.kind = 3;
      OnDocSwitch(doc);
      expect("a document switch drops crop, text edit and drag", !sCropOn && sTextEdit == 0 && sOp.kind == 0);
      doc->RemoveLayer(id);
      doc->Commit();

      // Eraser on a group acts on every paint layer inside it in one undo step,
      // and on an empty group shows the "Pick a layer" note.
      {
         const int ga = doc->AddPaintLayer();
         const int gb = doc->AddPaintLayer();
         BrushEngine::Brush fill;
         fill.size = 80.0f;
         fill.color[0] = 0.9f;
         if (doc->BeginPaint(ga, 0, fill, W * 0.5f, H * 0.5f, 1.0f))
            doc->EndPaint();
         if (doc->BeginPaint(gb, 0, fill, W * 0.5f, H * 0.5f, 1.0f))
            doc->EndPaint();
         const int grp = doc->GroupLayers({ ga, gb });
         doc->Commit();
         SelectOnly(grp);
         const std::vector<uint8_t> beforeGrp = picture(doc);
         const size_t u0Grp = gUndoStack.size();
         const int prevTool = sTool;
         sTool = kEraser;
         BeginStroke(doc, ImVec2(W * 0.5f, H * 0.5f), false);
         EndStroke(doc);
         const std::vector<uint8_t> afterGrp = picture(doc);
         expect("eraser on a group changes pixels", afterGrp != beforeGrp);
         expect("eraser on a group pushes one undo step", gUndoStack.size() == u0Grp + 1);
         Undo();
         doc = FindDoc();
         expect("eraser on a group undoes in one step", doc != nullptr && picture(doc) == beforeGrp);
         doc->RemoveLayer(grp);
         const int emptyGrp = doc->AddLayer(Kind::Group, 0).id;
         doc->Commit();
         SelectOnly(emptyGrp);
         sGroupPickAsk = -10.0;
         BeginStroke(doc, ImVec2(W * 0.5f, H * 0.5f), false);
         expect("eraser on an empty group sets Pick a layer note", ImGui::GetTime() - sGroupPickAsk < 1.0);
         sTool = prevTool;
         doc->RemoveLayer(emptyGrp);
         doc->Commit();
      }
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
   std::ifstream in(AppPaths::FsPath(UserFontList()));
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
      std::ifstream in(AppPaths::FsPath(UserFontList()));
      std::string line;
      while (std::getline(in, line))
         if (!line.empty())
            have.push_back(line);
   }
   if (std::find(have.begin(), have.end(), path) == have.end())
   {
      std::ofstream out(AppPaths::FsPath(UserFontList()), std::ios::app);
      out << path << "\n";
   }
   return true;
}

#ifdef IMGUI_ENABLE_TEST_ENGINE
// UI audit (INFINITE_UIAUDIT_VIEW2D, docs/plans/ui-system/ui-audit.md): a document holding one layer of every kind,
// and the panel states the audit steps through (every side tab x every layer kind, every tool's bar, both docks).
namespace
{
   constexpr int kAuditKinds = tok::view2d_audit_kinds;   // 9 layer kinds and "nothing selected"
   const char* kAuditKindNames[kAuditKinds] = { "paint", "node", "3D node", "text", "shape", "fill", "adjust", "group", "masked", "none" };
   const char* kAuditTabNames[7] = { "Layers", "Layer", "Transform", "Document", "Render", "Effects", "Colour" };
   const char* kAuditToolNames[kToolCount] = { "Move", "Crop", "Hand", "Zoom", "Brush", "Eraser", "Fill", "Pick", "Marquee", "Lasso", "Wand",
                                               "Pen", "Node", "Shape", "Text", "Gradient", "Clone", "Heal", "Blur", "Sharpen", "Dodge", "Burn",
                                               "Sponge", "Median", "UndoBrush", "ColourReplace", "PaintMixer", "BgErase", "FloodErase",
                                               "SelBrush", "ObjectSel", "RedEye", "Blemish", "Patch", "Inpaint", "MeshWarp", "Perspective",
                                               "StylePick", "Measure" };
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
      doc->Doc().w = tok::view2d_audit_doc_w;
      doc->Doc().h = tok::view2d_audit_doc_h;
      doc->Commit();
      int* id = sAuditIds;
      sSel.clear();
      id[0] = doc->AddPaintLayer();
      {
         BrushEngine::Brush b;
         b.size = tok::view2d_audit_doc_size;
         b.color[0] = tok::view2d_color;
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
         f->opacity = tok::view2d_audit_doc_opacity;
      doc->Commit();
      sSel.clear();
      sNeedFit = true;
      return doc;
   }
}

#ifdef IMGUI_ENABLE_TEST_ENGINE
std::map<std::string, View2DMoreProbe>& View2DMoreProbes()
{
   static std::map<std::string, View2DMoreProbe> m;
   return m;
}
#endif

int View2DAuditStateCount()
{
   if (std::getenv("INFINITE_UIAUDIT_V2D_ALL_FX") != nullptr)
      return Scene2DNode::kAdjCount + (int)(sizeof(kNodeEffects) / sizeof(kNodeEffects[0])) + Scene2DNode::kFxCount;
   return 7 * kAuditKinds + kToolCount + 8;
}

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
   if (std::getenv("INFINITE_UIAUDIT_V2D_ALL_FX") != nullptr)
   {
      gView2DDock = 0;
      sTool = kMove;
      constexpr int kNE = (int)(sizeof(kNodeEffects) / sizeof(kNodeEffects[0]));
      // Clean up any previous audit node effects and styles on the paint layer.
      if (Layer* p = doc->FindLayer(sAuditIds[0]))
      {
         for (const auto& ef : p->effects)
            if (ef.uid != 0)
               if (GraphNode* gn = FindNodeByUid(ef.uid))
                  RemoveNodeByIndex(gn->index);
         p->effects.clear();
         for (int t = 0; t < Scene2DNode::kFxCount; t++)
            p->fx[t].on = false;
      }
      // Each state opens its card's settings window: the card is a header, its rows live there.
      const std::string du = std::to_string(sDocUid);
      SettingsWindow::CloseAll();
      if (i < Scene2DNode::kAdjCount)
      {
         SettingsWindow::Toggle("v2dadj/" + du + "/" + std::to_string(sAuditIds[6]));
         sTab = 6;
         sSel = { sAuditIds[6] };
         if (Layer* a = doc->FindLayer(sAuditIds[6]))
         {
            a->visible = true;
            a->adjType = i;
            a->adjAmt = Scene2DNode::AdjDefault(i);
            a->adjP = Scene2DNode::AdjParamDefault(i);
         }
         doc->Commit();
         return std::string("adj ") + Scene2DNode::AdjName(i);
      }
      i -= Scene2DNode::kAdjCount;
      if (i < kNE)
      {
         sTab = 5;
         sSel = { sAuditIds[0] };
         AddNodeEffect(doc, sAuditIds[0], i);
         if (Layer* p = doc->FindLayer(sAuditIds[0]); p != nullptr && !p->effects.empty())
            SettingsWindow::Toggle("v2deff/" + du + "/" + std::to_string(p->id) + "/" + std::to_string(p->effects.back().uid));
         doc->Commit();
         return std::string("fx ") + kNodeEffects[i].label;
      }
      i -= kNE;
      sTab = 5;
      sSel = { sAuditIds[0] };
      if (Layer* p = doc->FindLayer(sAuditIds[0]))
      {
         p->fx[i].on = true;
         SettingsWindow::Toggle("v2dfx/" + du + "/" + std::to_string(p->id) + "/" + std::to_string(i));
      }
      doc->Commit();
      return std::string("style ") + Scene2DNode::FxName(i);
   }
   // The last states (relief, curves, marbling, liquify) dress the audit layers and the brush; every state starts
   // from them undressed so the earlier states read the same as before.
   static int sAuditAdjWas = -1;
   if (Layer* p = doc->FindLayer(sAuditIds[0]); p != nullptr && p->relief)
      p->relief = false, doc->Commit();
   if (Layer* a = doc->FindLayer(sAuditIds[6]); a != nullptr && sAuditAdjWas >= 0)
      a->adjType = sAuditAdjWas, sAuditAdjWas = -1, doc->Commit();
   if (BrushEngine::WarpMode(sBrush.mode))
      sBrush.mode = BrushEngine::kNormal;
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
      if (i == kMarquee)
         doc->SelectRect(240.0f, 160.0f, 720.0f, 400.0f, 0, 0.0f, false);   // the bar's Expand / Contract / Smooth / Border need one
      else if (doc->HasSelection())
         doc->Deselect();
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
   if (i >= 4)
   {
      gView2DDock = 0;
      if (i == 4 || i == 5)
      {
         // The Relief card (a paint layer with relief on) and the Curves graph (an adjustment set to Curves).
         sTab = 1;
         select(i == 4 ? 0 : 6);
         if (i == 4)
         {
            if (Layer* p = doc->FindLayer(sAuditIds[0]))
               p->relief = true;
         }
         else if (Layer* a = doc->FindLayer(sAuditIds[6]))
         {
            sAuditAdjWas = a->adjType;
            a->adjType = Scene2DNode::kAdjCurves;
         }
         doc->Commit();
         return i == 4 ? "relief card" : "curves editor";
      }
      // The brush bar with a marbling comb (Mode + Tines) or a Liquify brush (its own Mode list).
      sTab = 0;
      select(0);
      sTool = kBrush;
      sBrush.mode = i == 6 ? BrushEngine::kComb : BrushEngine::kPinch;
      return i == 6 ? "brush marbling" : "brush liquify";
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
            sImageFormat, sVideoFormat, sGizmoMode, PickModeNow(), b.tip, b.size, b.hardness, b.opacity, b.flow, b.spacing, b.jitter, b.angle,
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
   // Tool switches and the vector style's popover fields (gradient fill, per-corner shape): the bar edits them too.
   const BrushEngine::Brush& r = sRetouch;
   char more[384];
   snprintf(more, sizeof more, " opts %d %d %d %d %d %d %d fillt %d %.1f %zu corn %.1f %.1f %.1f %.1f %d %d %d %d"
            " strk %d %d %d %.1f %.2f %.2f %.2f %.1f %.1f %d %d",
            sMarqueeCentre, sCloneAll, sObjSoft, sPatchClear, r.protectHue, r.sourceFlipX, r.sourceFlipY, v.fillType, v.fillAngle,
            v.fillStops.size(), v.cornerR[0], v.cornerR[1], v.cornerR[2], v.cornerR[3], v.cornerType[0], v.cornerType[1], v.cornerType[2],
            v.cornerType[3], v.arrowS, v.arrowE, v.balanced ? 1 : 0, v.arrowSize, v.taperS, v.taperE, v.pressMin, v.miter, v.dashPhase, v.behind ? 1 : 0, v.scaleStroke ? 1 : 0);
   return std::string(buf) + tools + more + " grev " + std::to_string(sGradReverse ? 1 : 0) + " sbr " + std::to_string(sSelBrushSub ? 1 : 0) +
          std::to_string(sSelBrushSoft ? 1 : 0) + std::to_string(r.lockAlpha ? 1 : 0) + std::to_string(sPatchSrc ? 1 : 0) +
          std::to_string(sMeshNearest ? 1 : 0) + " sel " + sel + (doc != nullptr ? " qm " + std::to_string(doc->quickMask) : std::string());
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
            snprintf(buf, sizeof buf, "layer %d %s = %s (%s..%s)", l.id, what, UiFmt::G(v).c, UiFmt::G(lo).c, UiFmt::G(hi).c);
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
   if (d.w < 1 || d.h < 1 || d.w > tok::view2d_view2_d_audit_ranges_limit || d.h > tok::view2d_view2_d_audit_ranges_limit)
   {
      snprintf(buf, sizeof buf, "document %d x %d", d.w, d.h);
      return buf;
   }
   for (const BrushEngine::Brush* b : { &sBrush, &sEraser })
      if (!(b->size >= 1.0f && b->size <= tok::view2d_view2_d_audit_ranges_limit_sm) || !(b->hardness >= 0 && b->hardness <= 1) || !(b->opacity >= 0 && b->opacity <= 1) ||
          !(b->flow >= 0 && b->flow <= 1))
      {
         snprintf(buf, sizeof buf, "brush size %s hardness %s opacity %s flow %s", UiFmt::G(b->size).c, UiFmt::G(b->hardness).c, UiFmt::G(b->opacity).c, UiFmt::G(b->flow).c);
         return buf;
      }
   return std::string();
}

// Puts the panel back as the current state had it (a poke may have changed the tool, tab or selection).
void View2DAuditEnd() { sAuditState = -1; }

void View2DAuditRestore()
{
   if (sAuditState >= 0)
      View2DAuditState(sAuditState);
}

// ---- journeys (src/app/selftest/Journeys.cpp, internal/plans/ui-system/journey-tests.md)
// Setup lines build the start state directly (not the journey's subject); queries read the result as plain text.

namespace
{
   std::map<std::string, int> sJourneyNames;   // setup layer name -> id

   int JourneyLayer(Scene2DNode* doc, const std::string& name)
   {
      auto it = sJourneyNames.find(name);
      return it != sJourneyNames.end() && doc->FindLayer(it->second) != nullptr ? it->second : 0;
   }

   const char* KindName(Kind k)
   {
      static const char* const kNames[Scene2DNode::kKindCount] = { "node", "fill", "group", "paint", "vector", "text", "adjust" };
      return kNames[(int)k];
   }

   std::string Fmt3(const float* c)
   {
      char b[64];
      std::snprintf(b, sizeof b, "%s %s %s", UiFmt::F2(c[0]).c, UiFmt::F2(c[1]).c, UiFmt::F2(c[2]).c);
      return b;
   }
}

// One setup line: "doc W H", "layer paint|text|vector NAME", "fill r g b" (the active paint layer, whole),
// "fg r g b", "bg r g b", "active NAME", "tool NAME", "select x0 y0 x1 y1", "swatches rrggbb ...", "norecent".
bool View2DJourneySetup(const std::string& line, std::string* err)
{
   std::istringstream in(line);
   std::string cmd;
   in >> cmd;
   InitBrushes();
   gView2DOpen = true;
   sSideOpen = true;
   if (cmd == "doc")
   {
      int w = tok::view2d_audit_doc_w, h = tok::view2d_audit_doc_h;
      in >> w >> h;
      sJourneyNames.clear();
      Scene2DNode* doc = MakeDoc(0.0f, 0.0f, "");
      if (doc == nullptr)
         return *err = "no document", false;
      doc->Doc().w = w;
      doc->Doc().h = h;
      doc->Commit();
      sTool = kMove;
      sTab = 0;
      gView2DDock = 0;
      gPianoRollOpen = false;   // a journey that opened it must not take the next journey's keys
      return true;
   }
   Scene2DNode* doc = FindDoc();
   if (doc == nullptr)
      return *err = "setup needs \"doc\" first", false;
   if (cmd == "layer")
   {
      std::string kind, name;
      in >> kind >> name;
      const Layer* a = Active(doc);
      const int above = a != nullptr ? a->id : 0;
      int id = 0;
      if (kind == "paint") id = doc->AddPaintLayer(above);
      else if (kind == "vector") id = doc->AddVectorLayer(above);
      else if (kind == "text") id = doc->AddTextLayer(name.empty() ? "Text" : name, doc->Doc().w * 0.5f, doc->Doc().h * 0.5f, 0.0f, above);
      else return *err = "layer kind " + kind, false;
      if (Layer* l = doc->FindLayer(id))
         l->name = name;
      sJourneyNames[name] = id;
      doc->Commit();
      SelectOnly(id);
      return true;
   }
   if (cmd == "fill")
   {
      float c[3] = { 0, 0, 0 };
      in >> c[0] >> c[1] >> c[2];
      const Layer* a = Active(doc);
      if (a == nullptr || !doc->FillPixels(a->id, c, 1.0f))
         return *err = "fill needs an active paint layer", false;
      doc->Commit();
      return true;
   }
   if (cmd == "rect")   // rect x0 y0 x1 y1 r g b: paint a box on the active paint layer
   {
      float r[4], c[3];
      in >> r[0] >> r[1] >> r[2] >> r[3] >> c[0] >> c[1] >> c[2];
      const Layer* a = Active(doc);
      doc->SelectRect(r[0], r[1], r[2], r[3], Scene2DNode::kSelNew, 0, false);
      const bool ok = a != nullptr && doc->FillPixels(a->id, c, 1.0f);
      doc->Deselect();
      if (!ok)
         return *err = "rect needs an active paint layer", false;
      doc->Commit();
      return true;
   }
   if (cmd == "shape")   // shape x0 y0 x1 y1 r g b: a filled rectangle item on the active vector layer
   {
      Scene2DNode::Shape s;
      s.type = Scene2DNode::Shape::kRect;
      in >> s.x0 >> s.y0 >> s.x1 >> s.y1 >> s.fillC[0] >> s.fillC[1] >> s.fillC[2];
      Layer* a = Active(doc);
      if (a == nullptr || a->kind != Kind::Vector)
         return *err = "shape needs an active vector layer", false;
      a->shapes.push_back(s);
      doc->Commit();
      return true;
   }
   if (cmd == "image")   // image NAME path: an Image Source node as a layer
   {
      std::string name, path;
      in >> name >> path;
      GraphNode* img = SpawnNode("Image Source", "Source", -800.0f, 0.0f);
      auto* src = img != nullptr ? dynamic_cast<ImageSourceNode*>(img->node.get()) : nullptr;
      if (src == nullptr)
         return *err = "no Image Source", false;
      if (const char* dir = getenv("INFINITE_JOURNEYS"); dir != nullptr && !std::filesystem::exists(path))
         path = (std::filesystem::path(dir).parent_path() / path).string();   // fixtures/.. beside the areas
      src->Load(path);
      const Layer* a = Active(doc);
      Layer& l = doc->AddLayer(Kind::Node, img->uid, a != nullptr ? a->id : 0);
      l.name = name;
      l.fit = Scene2DNode::kFitStretch;
      const int id = l.id;
      sJourneyNames[name] = id;
      doc->Commit();
      SelectOnly(id);
      return true;
   }
   if (cmd == "group")   // group NAME A B ..: the named layers in one group
   {
      std::string name, m;
      in >> name;
      std::vector<int> ids;
      while (in >> m)
         if (const int id = JourneyLayer(doc, m))
            ids.push_back(id);
      const int g = doc->GroupLayers(ids);
      if (g == 0)
         return *err = "group failed", false;
      if (Layer* l = doc->FindLayer(g))
         l->name = name;
      sJourneyNames[name] = g;
      doc->Commit();
      SelectOnly(g);
      return true;
   }
   if (cmd == "textstyle")   // textstyle SIZE r g b: the active text layer's size and colour
   {
      float size = 0, c[3];
      in >> size >> c[0] >> c[1] >> c[2];
      Layer* a = Active(doc);
      if (a == nullptr || a->kind != Kind::Text)
         return *err = "textstyle needs an active text layer", false;
      a->fontSize = size;
      std::copy(c, c + 3, a->textC);
      doc->Commit();
      return true;
   }
   if (cmd == "deselect")
   {
      doc->Deselect();
      return true;
   }
   if (cmd == "fg" || cmd == "bg")
   {
      float* c = cmd == "fg" ? sFg : sBg;
      in >> c[0] >> c[1] >> c[2];
      return true;
   }
   if (cmd == "active")
   {
      std::string name;
      in >> name;
      const int id = JourneyLayer(doc, name);
      if (id == 0)
         return *err = "no layer " + name, false;
      SelectOnly(id);
      return true;
   }
   if (cmd == "tool")
   {
      std::string name;
      in >> name;
      for (int i = 0; i < kToolCount; i++)
         if (name == kAuditToolNames[i])
            return sTool = i, true;
      return *err = "no tool " + name, false;
   }
   if (cmd == "select")
   {
      float r[4];
      in >> r[0] >> r[1] >> r[2] >> r[3];
      doc->SelectRect(r[0], r[1], r[2], r[3], Scene2DNode::kSelNew, 0, false);
      return true;
   }
   if (cmd == "swatches")
   {
      std::vector<Rgb> list;
      std::string hex;
      while (in >> hex)
      {
         unsigned v = 0;
         std::sscanf(hex.c_str(), "%6x", &v);
         Rgb c;
         c.c[0] = ((v >> 16) & 255) / 255.0f, c.c[1] = ((v >> 8) & 255) / 255.0f, c.c[2] = (v & 255) / 255.0f;
         list.push_back(c);
      }
      SetSwatches(list);
      return true;
   }
   if (cmd == "norecent")
   {
      sRecentCols.clear();
      return true;
   }
   return *err = "unknown setup " + cmd, false;
}

// A fact about the panel now, as text: "tool", "layers", "active.kind", "active.name", "selection", "fg",
// "px X Y" (the picture), "layerpx NAME X Y" (that paint layer alone), "shape.fill" (the active vector layer's top
// item), "rasterask" (the Rasterize? chip is up), "open.pianoroll", "open.view2d".
std::string View2DJourneyQuery(const std::string& q)
{
   std::istringstream in(q);
   std::string what;
   in >> what;
   Scene2DNode* doc = FindDoc();
   if (what == "tool") return kAuditToolNames[std::clamp(sTool, 0, kToolCount - 1)];
   if (what == "open.pianoroll") return gPianoRollOpen ? "1" : "0";
   if (what == "open.view2d") return gView2DOpen ? "1" : "0";
   if (what == "fg") return Fmt3(sFg);
   if (doc == nullptr) return "no document";
   const Layer* a = Active(doc);
   if (what == "layers") return std::to_string(doc->Layers().size());
   if (what == "active.kind") return a != nullptr ? KindName(a->kind) : "none";
   if (what == "active.name") return a != nullptr ? a->name : "none";
   if (what == "selection") return doc->HasSelection() ? "1" : "0";
   if (what == "rasterask") return sRasterAsk != 0 ? "1" : "0";
   if (what == "kind")   // kind NAME: that layer's kind, "gone" when it no longer exists
   {
      std::string name;
      in >> name;
      const int id = JourneyLayer(doc, name);
      const Layer* l = id != 0 ? doc->FindLayer(id) : nullptr;
      return l != nullptr ? KindName(l->kind) : "gone";
   }
   if (what == "ink")   // ink NAME X Y: 1 when that layer draws a pixel there (the target rule's test)
   {
      std::string name;
      float x = 0, y = 0;
      in >> name >> x >> y;
      const Layer* l = doc->FindLayer(JourneyLayer(doc, name));
      if (l == nullptr)
         return "gone";
      if (LayerInk(doc, l->id) == nullptr)
         return "nopx";
      float lo[2] = { x, y }, hi[2] = { x, y };
      return InkIn(doc, *l, lo, hi) ? "1" : "0";
   }
   if (what == "inkbox")   // inkbox NAME: x0 y0 x1 y1 of that layer's own ink, document px
   {
      std::string name;
      in >> name;
      const Layer* l = doc->FindLayer(JourneyLayer(doc, name));
      const std::vector<uint8_t>* px = l != nullptr ? LayerInk(doc, l->id) : nullptr;
      if (px == nullptr)
         return "none";
      const int w = doc->Doc().w, h = doc->Doc().h;
      int x0 = w, y0 = h, x1 = -1, y1 = -1;
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
            if ((*px)[((size_t)y * w + x) * 4 + 3] > 8)
               x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
      if (x1 < 0)
         return "none";
      char b[64];
      std::snprintf(b, sizeof b, "%d %d %d %d", x0, y0, x1 + 1, y1 + 1);
      return b;
   }
   if (what == "warp.box")   // the mesh grid's bounds: x0 y0 x1 y1, document px
   {
      if (sMeshId == 0)
         return "none";
      float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
      for (int k = 0; k < 16; k++)
      {
         lo[0] = std::min(lo[0], sMeshPts[k * 2]), lo[1] = std::min(lo[1], sMeshPts[k * 2 + 1]);
         hi[0] = std::max(hi[0], sMeshPts[k * 2]), hi[1] = std::max(hi[1], sMeshPts[k * 2 + 1]);
      }
      char b[96];
      std::snprintf(b, sizeof b, "%s %s %s %s", UiFmt::F0(lo[0]).c, UiFmt::F0(lo[1]).c, UiFmt::F0(hi[0]).c, UiFmt::F0(hi[1]).c);
      return b;
   }
   if (what == "shape.fill.at")   // shape.fill.at NAME: that vector layer's top item fill
   {
      std::string name;
      in >> name;
      const Layer* l = doc->FindLayer(JourneyLayer(doc, name));
      return l != nullptr && !l->shapes.empty() ? Fmt3(l->shapes.back().fillC) : "none";
   }
   if (what == "text.colour")
   {
      std::string name;
      in >> name;
      const Layer* l = doc->FindLayer(JourneyLayer(doc, name));
      return l != nullptr && l->kind == Kind::Text ? Fmt3(l->textC) : "none";
   }
   if (what == "shape.fill") return a != nullptr && a->kind == Kind::Vector && !a->shapes.empty() ? Fmt3(a->shapes.back().fillC) : "none";
   if (what == "px" || what == "layerpx" || what == "activepx")
   {
      int layer = what == "activepx" && a != nullptr ? a->id : 0;
      if (what == "layerpx")
      {
         std::string name;
         in >> name;
         layer = JourneyLayer(doc, name);
         if (layer == 0)
            return "no layer " + name;
      }
      float x = 0, y = 0, rgb[3] = { 0, 0, 0 };
      in >> x >> y;
      doc->CookIfNeeded(ImGui::GetFrameCount() + 9000000);
      if (!doc->Pick(x, y, rgb, 1, layer))
         return "none";
      return Fmt3(rgb);
   }
   return "unknown query " + what;
}

// A document point on screen (the view as last drawn).
bool View2DJourneyToScreen(float x, float y, ImVec2* out)
{
   Scene2DNode* doc = FindDoc();
   if (doc == nullptr || sViewMx.x <= sViewMn.x)
      return false;
   *out = ToScreen(doc, x, y);
   return true;
}
#endif

void View2DAddNode(GraphNode& gn)
{
   // MakeDoc can move gNodes, so read what's needed from gn first.
   const uint64_t uid = gn.uid;
   Scene2DNode* doc = EnsureDoc(gn);
   if (doc == nullptr)
      return;
   GraphNode* added = FindNodeByUid(uid);
   if (added == nullptr || WouldCreateDocCycle(doc, added->node.get()))
   {
      Notices::Post(Notices::Level::Warning, "view2d.cycle", T("Can't add to 2D View"),
                    T("That node already reads from this 2D Document (would create a feedback loop)."));
      return;
   }
   PushUndoCheckpoint("Add to 2D View");
   gView2DOpen = true;
   Layer& l = NewLayerAt(doc, Kind::Node, uid);
   l.is3d = Is3DNode(added->node.get());
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
   ModSlider("Brush size", &sBrush.size, params::view2d::size_lo, params::view2d::size_hi, "%.0f", kParamWidth, false, 0.0f, nullptr, nullptr, kBrushSizeParam);
   ModSlider("Brush opacity", &sBrush.opacity, params::view2d::opacity_lo, params::view2d::opacity_hi, "%.2f", kParamWidth, false, 0.0f, nullptr, nullptr, kBrushOpacityParam);
   ModSlider("Brush flow", &sBrush.flow, params::view2d::flow_2_lo, params::view2d::flow_2_hi, "%.2f", kParamWidth, false, 0.0f, nullptr, nullptr, kBrushFlowParam);
   ModSlider("Brush hue", &sBrushHue, params::view2d::brush_hue_lo, params::view2d::brush_hue_hi, "%.0f", kParamWidth, false, 0.0f, nullptr, nullptr, kBrushHueParam);
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
   ImGui::TextDisabled(T("%d x %d, %d layers"), doc->Doc().w, doc->Doc().h, (int)doc->Layers().size());
   if (ActionButton::Draw(L("Open in 2D View"), ImVec2(tok::view2d_open_in_d_view_x, 0)))
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

void SetViewDock(int which, int dock)
{
   int& mine = which == 2 ? gView2DDock : gView3DDock;
   int& back = which == 2 ? gView2DDockBack : gView3DDockBack;
   int& other = which == 2 ? gView3DDock : gView2DDock;
   const int otherBack = which == 2 ? gView3DDockBack : gView2DDockBack;
   dock = std::clamp(dock, 0, 4);
   if (dock == 4)
   {
      if (mine != 4)
         back = mine;
      if (other == 4)
         other = otherBack == 4 ? 0 : otherBack;   // one view fills at a time; the last one set wins
   }
   mine = dock;
}

const char* ViewDockFillTip() { return Sc(T("Fill the window (Ctrl+Shift+F)")); }

void ViewDockToggleFill(int which)
{
   const int mine = which == 2 ? gView2DDock : gView3DDock;
   const int back = which == 2 ? gView2DDockBack : gView3DDockBack;
   SetViewDock(which, mine == 4 ? (back == 4 ? 0 : back) : 4);
}

void DrawViewDockMenuItems(int which)
{
   static const char* kDocks[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top"), I18N_KEY("Fill") };
   const int mine = which == 2 ? gView2DDock : gView3DDock;
   for (int i = 0; i < 5; i++)
      if (MenuParts::Item(T(kDocks[i]), i == 4 ? Sc("Ctrl+Shift+F") : nullptr, mine == i) && mine != i)
         SetViewDock(which, i);
}

void DrawViewDocked(const char* id, const ImVec2& size, int dock, float& height, float& width, const char* gripId,
                    const char* cardId, ImVec2& rectMin, ImVec2& rectMax, void (*drawPanel)())
{
   const float kGrip = PanelFrame::kGap;
   const bool fill = dock == 4;                    // no seam: nothing to resize against
   const bool vertical = dock == 1 || dock == 2;   // the seam is a column
   const bool gripFirst = dock == 0 || dock == 1;  // the canvas is above or to the left
   // Fill sits over the canvas row, which still cooks beneath it: back the whole rect with the opaque panel fill so
   // no node, cable or pin shows through the toolbar band or the gap around the card.
   if (fill)
   {
      const CategoryColors::UiTheme& th = CategoryColors::CurrentUiTheme();
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(th.panelBg.r, th.panelBg.g, th.panelBg.b, 1.0f));
   }
   ImGui::BeginChild(id, size, false);
   if (fill)
      ImGui::PopStyleColor();
   rectMin = ImGui::GetWindowPos();
   rectMax = ImVec2(rectMin.x + ImGui::GetWindowSize().x, rectMin.y + ImGui::GetWindowSize().y);
   const ImVec2 inner = Ui::Avail();
   auto grip = [&]()
   {
      UiInteract::Hit(gripId, vertical ? ImVec2(kGrip, std::max(1.0f, inner.y)) : ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(vertical, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         const ImVec2 d = ImGui::GetIO().MouseDelta;
         switch (dock)
         {
            case 0: height -= d.y; break;
            case 1: width -= d.x; break;
            case 2: width += d.x; break;
            default: height += d.y; break;
         }
         height = std::max(tok::dock_view_min_h, height);
         width = std::max(tok::dock_view_min_w, width);
      }
   };
   if (gripFirst && !fill)
   {
      grip();
      if (vertical)
         FormParts::Inline();
   }
   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const ImVec2 cardSize = fill ? ImVec2(std::max(1.0f, inner.x), std::max(1.0f, inner.y))
                         : vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), std::max(1.0f, inner.y))
                                    : ImVec2(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
   PanelFrame::Insets in;
   switch (dock)   // the seam side needs no inset of its own
   {
      case 0: in.t = 0.0f; break;
      case 1: in.l = 0.0f; break;
      case 2: in.r = 0.0f; break;
      case 3: in.b = 0.0f; break;
      default: break;
   }
   PushDockedPanelStyle(/*isChild=*/true);   // opaque card fill, as every other docked panel
   PanelFrame::BeginCard(cardId, cardSize, in);
   PopDockedPanelStyle();
   drawPanel();
   PanelFrame::EndCard();
   if (!gripFirst && !fill)
   {
      if (vertical)
         FormParts::Inline();
      grip();
   }
   EndChildSafe();
}

void DrawView2DDocked(const char* id, const ImVec2& size)
{
   gView2DClaimedKeys = false;
   DrawViewDocked(id, size, gView2DDock, gView2DHeight, gView2DWidth, "##view2dgrip", "##view2dcontent", gView2DRectMin,
                  gView2DRectMax, [] { DrawPanel(); });
}

} // namespace app

namespace app
{
// INFINITE_AB2D=<dir>: the Affinity A/B (tools/re-survey/affinity_probe.py). Reads <dir>/photo.png, runs each
// operation through the panel's own actions on a fresh document holding the photo as an image layer, and writes
// <dir>/inf_<op>.png. Coordinates and settings match the Affinity side (affinity_probe.py OPS). Returns failures.
int View2DAffinityAb(const char* dir)
{
   namespace fs = std::filesystem;
   const std::string photo = std::string(dir) + "/photo.png";
   int fails = 0, frame = 7000000;
   InitBrushes();   // the panel does this on its first draw, which the A/B never reaches
   struct AbOp { const char* name; std::function<bool(Scene2DNode*, int)> run; };
   auto stroke = [&](Scene2DNode* doc, int tool, ImVec2 a, ImVec2 b, float size, float hard,
                     std::function<void(BrushEngine::Brush&)> setup = nullptr) {
      sTool = tool;
      BrushEngine::Brush& br = ToolBrush();
      br.mode = tool == kBrush ? 0 : br.mode;
      br.lockAlpha = false;
      br.spongeMode = 0;
      br.sourceBlend = 0;
      if (setup)
         setup(br);
      br.size = size;
      br.hardness = hard;
      br.opacity = br.flow = 1.0f;
      br.pressureSize = br.pressureOpacity = false;
      sHaveLast = false;
      BeginStroke(doc, a, false);
      if (!doc->Painting())
         return false;
      for (int i = 1; i <= 64; i++)
         doc->PaintTo(a.x + (b.x - a.x) * i / 64.0f, a.y + (b.y - a.y) * i / 64.0f, 1.0f);
      EndStroke(doc);
      return true;
   };
   const float kRed[3] = { 1.0f, 0.0f, 0.0f };
   const ImVec2 kBhA(330, 520), kBhB(480, 520);   // behaviour strokes: across the green slope
   auto shape = [&](Scene2DNode* doc, int type, float x0, float y0, float x1, float y1, const float* fill, bool line, float opacity,
                    int blend) {
      const Layer* a = Active(doc);
      const int id = doc->AddVectorLayer(a != nullptr ? a->id : 0);
      Layer* l = doc->FindLayer(id);
      if (l == nullptr)
         return false;
      Shape sh;
      sh.type = type;
      sh.x0 = x0, sh.y0 = y0, sh.x1 = x1, sh.y1 = y1;
      sh.fillOn = true;
      std::copy(fill, fill + 3, sh.fillC);
      sh.fillC[3] = 1.0f;
      sh.strokeOn = line;
      const float blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
      std::copy(blue, blue + 4, sh.strokeC);
      sh.width = 8.0f;
      l->shapes.push_back(sh);
      l->opacity = opacity;
      l->blend = blend;
      doc->Commit();
      SelectOnly(id);
      return true;
   };
   auto adjust = [&](Scene2DNode* doc, int type, std::vector<float> p) {
      AddAdjust(doc, type);
      Layer* a = Active(doc);
      if (a == nullptr || a->kind != Kind::Adjust)
         return false;
      a->adjP = std::move(p);
      doc->Commit();
      return true;
   };
   const std::vector<AbOp> ops = {
      { "rasterize", [&](Scene2DNode* d, int) { return EnsurePixels(d); } },
      { "delete_rect", [&](Scene2DNode* d, int) { d->SelectRect(100, 200, 250, 320, Scene2DNode::kSelNew, 0, false); return ClearOrFill(d, 0); } },
      { "delete_ellipse", [&](Scene2DNode* d, int) { d->SelectRect(150, 450, 350, 590, Scene2DNode::kSelNew, 0, true); return ClearOrFill(d, 0); } },
      { "copy_rect", [&](Scene2DNode* d, int src) {
           d->SelectRect(100, 200, 250, 320, Scene2DNode::kSelNew, 0, false);
           sPending.what = Pending::Duplicate;
           RunPending(d);
           const Layer* a = Active(d);
           if (a == nullptr || a->id == src)
              return false;
           for (Layer& l : d->Layers())
              if (l.id != a->id)
                 l.visible = false;
           d->Commit();
           return true; } },
      { "cut_rect", [&](Scene2DNode* d, int) {
           d->SelectRect(100, 200, 250, 320, Scene2DNode::kSelNew, 0, false);
           if (!SelectionActsOnPixels(d))
              return false;
           const int nid = SelectionToLayer(d, true);
           if (Layer* n = d->FindLayer(nid))
              n->visible = false;
           d->Commit();
           return nid != 0; } },
      { "wand_delete", [&](Scene2DNode* d, int) {
           EnsurePixels(d);   // the wand reads the active layer
           const Layer* a = Active(d);
           d->SelectWand(300, 100, 51, true, Scene2DNode::kSelNew, false, a != nullptr ? a->id : 0);
           return d->HasSelection() && ClearOrFill(d, 0); } },
      { "eraser", [&](Scene2DNode* d, int) { return stroke(d, kEraser, ImVec2(60, 600), ImVec2(450, 560), 40.0f, 1.0f); } },
      { "blur_brush", [&](Scene2DNode* d, int) { return stroke(d, kBlur, ImVec2(120, 640), ImVec2(420, 640), 80.0f, 0.5f); } },
      { "heal", [&](Scene2DNode* d, int) {
           sHaveCloneSrc = true;
           sCloneSrc = ImVec2(300, 380);
           sCloneOffsetSet = false;
           return stroke(d, kHeal, ImVec2(240, 380), ImVec2(241, 380), 50.0f, 0.5f); } },
      { "fill_rect", [&](Scene2DNode* d, int) {
           std::copy(kRed, kRed + 3, sFg);
           d->SelectRect(100, 200, 250, 320, Scene2DNode::kSelNew, 0, false);
           return ClearOrFill(d, 1); } },
      { "bucket", [&](Scene2DNode* d, int) {
           std::copy(kRed, kRed + 3, sFg);
           sTolerance = 51;
           sContiguous = true;
           sAllLayers = false;
           EnsurePixels(d);
           FillAt(d, ImVec2(300, 100));
           return true; } },
      { "adj_white_balance", [&](Scene2DNode* d, int) { return adjust(d, Scene2DNode::kAdjWhiteBalance, { 0.5f, 0.25f }); } },
      { "adj_split_toning", [&](Scene2DNode* d, int) { return adjust(d, Scene2DNode::kAdjSplitToning, { 30.0f, 0.6f, 210.0f, 0.5f, 0.4f }); } },
      { "adj_recolour", [&](Scene2DNode* d, int) { return adjust(d, Scene2DNode::kAdjRecolour, { 200.0f, 0.5f, 0.1f }); } },
      { "adj_normals", [&](Scene2DNode* d, int) { return adjust(d, Scene2DNode::kAdjNormals, { 45.0f, 0.75f, 0.0f, 1.0f }); } },
      { "adj_tone_compression", [&](Scene2DNode* d, int) { return adjust(d, Scene2DNode::kAdjToneCompression, { 1.5f, 0.8f, 0.25f }); } },
      { "adj_tone_stretch", [&](Scene2DNode* d, int) { return adjust(d, Scene2DNode::kAdjToneStretch, { 0.25f, 1.0f, 0.0f }); } },
      // Vector shapes and layer settings, which Affinity's SDK can draw too.
      { "shape_rect", [&](Scene2DNode* d, int) { return shape(d, Shape::kRect, 100, 200, 250, 320, kRed, false, 1.0f, 0); } },
      { "shape_ellipse_stroke", [&](Scene2DNode* d, int) { return shape(d, Shape::kEllipse, 150, 450, 350, 590, kRed, true, 1.0f, 0); } },
      { "shape_multiply", [&](Scene2DNode* d, int) {
           const float c[3] = { 1.0f, 0.5f, 0.25f };
           return shape(d, Shape::kRect, 50, 100, 460, 700, c, false, 1.0f, 2); } },
      { "shape_opacity", [&](Scene2DNode* d, int) { return shape(d, Shape::kRect, 100, 200, 250, 320, kRed, false, 0.5f, 0); } },
      { "layer_opacity", [&](Scene2DNode* d, int src) {
           Layer* l = d->FindLayer(src);
           if (l == nullptr)
              return false;
           l->opacity = 0.5f;
           d->Commit();
           return true; } },
      // Behaviour checks: brush tools the SDK cannot stroke. Python checks what each did to the photo (bh_* in
      // affinity_probe.py), against what Affinity's help says the tool does.
      { "bh_dodge", [&](Scene2DNode* d, int) { return stroke(d, kDodge, kBhA, kBhB, 60.0f, 0.5f); } },
      { "bh_burn", [&](Scene2DNode* d, int) { return stroke(d, kBurn, kBhA, kBhB, 60.0f, 0.5f); } },
      { "bh_sponge_sat", [&](Scene2DNode* d, int) { return stroke(d, kSponge, kBhA, kBhB, 60.0f, 0.5f); } },
      { "bh_sponge_desat", [&](Scene2DNode* d, int) {
           return stroke(d, kSponge, kBhA, kBhB, 60.0f, 0.5f, [](BrushEngine::Brush& b) { b.spongeMode = 1; }); } },
      { "bh_sponge_vibrance", [&](Scene2DNode* d, int) {
           return stroke(d, kSponge, kBhA, kBhB, 60.0f, 0.5f, [](BrushEngine::Brush& b) { b.spongeMode = 2; }); } },
      { "bh_sharpen", [&](Scene2DNode* d, int) { return stroke(d, kSharpen, kBhA, kBhB, 60.0f, 0.5f); } },
      { "bh_median", [&](Scene2DNode* d, int) { return stroke(d, kMedian, kBhA, kBhB, 60.0f, 0.5f); } },
      { "bh_paint", [&](Scene2DNode* d, int) {
           std::copy(kRed, kRed + 3, sFg);
           return stroke(d, kBrush, kBhA, kBhB, 60.0f, 1.0f); } },
      { "bh_smudge", [&](Scene2DNode* d, int) {
           EnsurePixels(d);   // smudge drags the photo's own pixels
           return stroke(d, kBrush, ImVec2(330, 330), ImVec2(480, 520), 60.0f, 0.5f, [](BrushEngine::Brush& b) { b.mode = BrushEngine::kSmudge; }); } },
      { "bh_clone", [&](Scene2DNode* d, int) {
           sHaveCloneSrc = true;
           sCloneSrc = ImVec2(330, 200);
           sCloneOffsetSet = false;
           sCloneAligned = true;
           return stroke(d, kClone, kBhA, kBhB, 60.0f, 1.0f); } },
      { "bh_colour_replace", [&](Scene2DNode* d, int) {
           std::copy(kRed, kRed + 3, sFg);
           return stroke(d, kColourReplace, kBhA, kBhB, 60.0f, 1.0f, [](BrushEngine::Brush& b) { b.keyTolerance = 0.4f; }); } },
      { "bh_bg_erase", [&](Scene2DNode* d, int) {
           return stroke(d, kBgErase, ImVec2(330, 120), ImVec2(480, 120), 60.0f, 1.0f, [](BrushEngine::Brush& b) { b.keyTolerance = 0.25f; }); } },
      { "bh_undo", [&](Scene2DNode* d, int) {
           EnsurePixels(d);
           if (Layer* al = Active(d))
              d->SetUndoSource(al->id);
           std::copy(kRed, kRed + 3, sFg);
           return stroke(d, kBrush, kBhA, kBhB, 60.0f, 1.0f) && stroke(d, kUndoBrush, kBhA, kBhB, 90.0f, 1.0f); } },
      { "bh_redeye", [&](Scene2DNode* d, int) {
           EnsurePixels(d);
           std::copy(kRed, kRed + 3, sFg);
           d->SelectRect(340, 500, 470, 540, Scene2DNode::kSelNew, 0, true);
           ClearOrFill(d, 1);
           d->Deselect();
           return stroke(d, kRedEye, kBhA, kBhB, 90.0f, 1.0f); } },
      { "bh_gradient", [&](Scene2DNode* d, int) {
           EnsurePixels(d);
           const float blue[3] = { 0.0f, 0.0f, 1.0f };
           std::copy(kRed, kRed + 3, sFg);
           std::copy(blue, blue + 3, sBg);
           sFillKind = 1;
           sGradOpacity = 1.0f;
           sGradReverse = false;
           sGradExtend = 0;
           GradientTo(d, ImVec2(0, 384), ImVec2(512, 384));
           return true; } },
      { "bh_gradient_repeat", [&](Scene2DNode* d, int) {
           EnsurePixels(d);
           const float blue[3] = { 0.0f, 0.0f, 1.0f };
           std::copy(kRed, kRed + 3, sFg);
           std::copy(blue, blue + 3, sBg);
           sFillKind = 1;
           sGradOpacity = 1.0f;
           sGradReverse = false;
           sGradExtend = 1;
           GradientTo(d, ImVec2(0, 384), ImVec2(100, 384));
           sGradExtend = 0;
           return true; } },
      // Undo brush, multiply: the earlier state multiplies over the current one, so a mid-grey state darkens it.
      { "bh_undo_multiply", [&](Scene2DNode* d, int) {
           EnsurePixels(d);
           if (Layer* al = Active(d))
              d->SetUndoSource(al->id);
           return stroke(d, kUndoBrush, kBhA, kBhB, 60.0f, 1.0f, [](BrushEngine::Brush& b) { b.sourceBlend = 1; }); } },
      // Retouch set (owner 2026-11-17: "nothing happened"): each must visibly change the stroke area.
      { "bh_heal_stroke", [&](Scene2DNode* d, int) {
           sHaveCloneSrc = true;
           sCloneSrc = ImVec2(330, 568);   // grass just below the stroke (clear of the black shadow): a textured source
           sCloneOffsetSet = false;
           return stroke(d, kHeal, kBhA, kBhB, 60.0f, 0.5f); } },
      { "bh_heal_spot", [&](Scene2DNode* d, int) {
           sHaveCloneSrc = false;
           return stroke(d, kHeal, kBhA, kBhB, 60.0f, 0.5f); } },
      { "bh_inpaint", [&](Scene2DNode* d, int) { return stroke(d, kInpaint, kBhA, kBhB, 60.0f, 1.0f); } },
      { "bh_blemish", [&](Scene2DNode* d, int) {
           EnsurePixels(d);
           Layer* al = Active(d);
           return al != nullptr && al->kind == Kind::Paint && d->Blemish(al->id, 400.0f, 520.0f, 40.0f); } },
      { "bh_patch", [&](Scene2DNode* d, int) {
           EnsurePixels(d);
           Layer* al = Active(d);
           d->SelectRect(340, 490, 470, 550, Scene2DNode::kSelNew, 0, false);
           return al != nullptr && al->kind == Kind::Paint && d->PatchSelection(al->id, 0.0f, 48.0f, false, false); } },   // from the grass just below, clear of the shadow
   };
   const char* only = std::getenv("INFINITE_AB2D_OP");
   for (const AbOp& op : ops)
   {
      if (only != nullptr && std::strcmp(only, op.name) != 0)
         continue;
      NewPatch();
      GraphNode* img = SpawnNode("Image Source", "Source", 0.0f, 0.0f);
      if (img == nullptr || !static_cast<ImageSourceNode*>(img->node.get())->Load(photo))
      {
         printf("AB2D %s FAIL no photo\n", op.name);
         return 1;
      }
      img->node->CookIfNeeded(++frame);
      View2DAddNode(*img);
      Scene2DNode* doc = nullptr;
      for (GraphNode& gn : gNodes)
         if (auto* s = dynamic_cast<Scene2DNode*>(gn.node.get()))
            doc = s;
      if (doc == nullptr || doc->Layers().empty())
      {
         printf("AB2D %s FAIL no document\n", op.name);
         return 1;
      }
      doc->Doc().w = 512;
      doc->Doc().h = 768;
      doc->Doc().background = 3;
      // Affinity's RGB/8 document blends display values; shapes and layer opacity are compared that way.
      doc->Doc().linear = std::strncmp(op.name, "shape_", 6) != 0 && std::strncmp(op.name, "layer_", 6) != 0;
      Layer& l = doc->Layers().back();
      l.x = 256.0f;
      l.y = 384.0f;
      doc->Commit();
      SelectOnly(l.id);
      doc->CookIfNeeded(++frame);
      sEditMask = false;
      doc->quickMask = false;
      const bool ran = op.run(doc, Active(doc) != nullptr ? Active(doc)->id : 0);
      doc->Deselect();
      doc->CookIfNeeded(++frame);
      std::vector<uint8_t> px;
      int w = 0, h = 0;
      const bool read = doc->ReadOutput(px, w, h);
      const Layer* a = Active(doc);
      const std::string out = std::string(dir) + "/inf_" + op.name + ".png";
      if (read)
         FlipRowsRgba8(px, w, h);
      const bool wrote = read && stbi_write_png(out.c_str(), w, h, 4, px.data(), w * 4) != 0;
      printf("AB2D %s ran=%d kind=%d layers=%d size=%dx%d %s\n", op.name, ran ? 1 : 0, a != nullptr ? (int)a->kind : -1,
             (int)doc->Layers().size(), w, h, wrote ? "ok" : "FAIL");
      fails += wrote ? 0 : 1;
   }
   return fails;
}

// MENUALIVETEST: the layer clipboard is state a person sees (Paste lights up), but the patch does not hold it.
std::string View2DTestFingerprint() { return "v2dclip:" + std::to_string(sClip.size()); }
void View2DTestReset() { sClip.clear(); }
} // namespace app
