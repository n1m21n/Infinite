#define GLFW_INCLUDE_NONE
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_node_editor.h"
#include "imgui_stdlib.h"
// For direct ImGuiInputTextState access: ImGui's own AutoSelectAll flag (and
// the select-all it triggers implicitly whenever focus arrives through
// SetKeyboardFocusHere's nav-activation path) can't be trusted to land
// before a same-frame keystroke is processed, so typed-param entry sets the
// cursor/selection state explicitly once the field is confirmed active.
#include "imgui_internal.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <functional>
#include <chrono>
#include <map>
#include <unordered_map>
#include <set>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/NodeFactory.h"
#include "core/CategoryColors.h"
#include "core/GLUtil.h"
#include "core/GraphNode.h"
#include "core/FilterDefs.h"
#include "core/BlendModes.h"
#include "core/Transport.h"
#include "core/Modulation.h"
#include "core/Expression.h"
#include "core/Palette.h"
#include "core/Patch.h"
#include "core/NodeViewport.h"
#include "nodes/ImageSourceNode.h"
#include "nodes/ShapeNode.h"
#include "nodes/FormulaNode.h"
#include "nodes/FilterNode.h"
#include "nodes/BlendNode.h"
#include "nodes/LayerStackNode.h"
#include "nodes/TextNode.h"
#include "nodes/FitNode.h"
#include "nodes/VideoSourceNode.h"
#include "nodes/NoiseNode.h"
#include "nodes/TextureNode.h"
#include "nodes/ResynthNode.h"
#include "nodes/MacroNodes.h"
#include "nodes/CurvesNode.h"
#include "nodes/RemoveBgNode.h"
#include "nodes/RampNode.h"
#include "nodes/ColorRampNode.h"
#include "nodes/PaletteNode.h"
#include "nodes/AnalyzeNodes.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/GeometryOpNodes.h"
#include "nodes/SceneNodes.h"
#include "nodes/EnvironmentNode.h"
#include "nodes/ModelSourceNode.h"
#include "nodes/Text3DNode.h"
#include "nodes/UtilityNodes.h"
#include "nodes/PathNode.h"
#include "nodes/CurveNode.h"
#include "nodes/OceanNode.h"
#include "nodes/SimulationNodes.h"
#include "nodes/GenerativeNodes.h"
#include "nodes/DrawNode.h"
#include "nodes/FeedbackNodes.h"
#include "nodes/SwitcherNode.h"
#include "nodes/Switcher3DNode.h"
#include "nodes/ModulatorNodes.h"
#include "nodes/OutputNode.h"

namespace ed = ax::NodeEditor;

namespace
{
   // Every node renders its output at this size, square, above its params.
   const float kPreviewSize = 190.0f;
   // Render 3D's preview is a viewport you actually work in - orbiting and
   // framing a scene through a thumbnail is not workable.
   const float kViewportSize = 340.0f;
   // The dockable viewport panel (right-click a node -> "Open in viewport
   // panel"), separate from both the inline preview above and the inline
   // per-node mini 3D viewport toggle. Not const: the panel's canvas-facing
   // edge is a drag handle, so these are starting sizes, not fixed ones.
   // Clamped against the window every frame - see the layout section.
   float gViewportPanelWidth = 320.0f;
   float gViewportPanelHeight = 260.0f;
   const float kViewportPanelMinWidth = 160.0f;
   // Taller than the width floor's equivalent margin: a top/bottom card gives
   // up two full rows (the dock combo/close row, then its own title row)
   // before the image even starts, so a floor sized like the width one left
   // next to nothing for the image itself - see the "so small" screenshot.
   const float kViewportPanelMinHeight = 190.0f;
   const float kParamWidth = 168.0f;
   const float kPinRadius = 7.0f;
   const float kPinHit = 20.0f; // generous click target - small dots were unhittable


   // Registered node names are the patch-file keys and must not change, so
   // casing is a display concern only - lowering it here keeps saved patches
   // loading while the UI reads the way the user asked for.
   std::string DisplayName(const std::string& name)
   {
      std::string out = name;
      std::transform(out.begin(), out.end(), out.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      return out;
   }

   // GeometryOpNode shares one node type across every mesh operator, and its
   // dropdown lets the operator change after spawn - so unlike most other
   // nodes, its title can't be read from the type name frozen at spawn time.
   // GeometryNode and ShapeNode have the identical problem: each primitive is
   // registered as its own searchable spawn entry (typeName == "Torus Knot",
   // "Tube", ...) so the type name frozen at spawn matches the shape you
   // picked, but the node keeps a live "shape" dropdown that can change it
   // afterwards without ever updating typeName - so the title has to track
   // the live shape field the same way, or picking a new shape leaves the
   // header reading whatever primitive was spawned first.
   std::string NodeTitle(const GraphNode& gn)
   {
      if (auto* opNode = dynamic_cast<GeometryOpNode*>(gn.node.get()))
      {
         const auto& names = GeometryOpNode::OpNames();
         if (opNode->op >= 0 && opNode->op < (int)names.size())
            return DisplayName(names[opNode->op]);
      }
      if (auto* geoNode = dynamic_cast<GeometryNode*>(gn.node.get()))
      {
         const auto& names = GeometryNode::ShapeNames();
         if (geoNode->shape >= 0 && geoNode->shape < (int)names.size())
            return DisplayName(names[geoNode->shape]);
      }
      if (auto* shapeNode = dynamic_cast<ShapeNode*>(gn.node.get()))
      {
         const auto& names = ShapeNode::ShapeNames();
         if (shapeNode->shapeType >= 0 && shapeNode->shapeType < (int)names.size())
            return DisplayName(names[shapeNode->shapeType]);
      }
      return DisplayName(gn.typeName);
   }

   // Undo/redo. Snapshots are whole-graph Patch::Data - the same format a
   // patch file uses - captured just before a mutation rather than diffed
   // after one, so restoring one is exactly LoadPatchFrom's job. Defined near
   // SavePatchTo/LoadPatchFrom, far below; forward-declared here because
   // ModSlider and friends (the widgets that call it) are among the first
   // functions in the file.
   void PushUndoCheckpoint();

   std::vector<GraphNode> gNodes;

   // Which node indices each Group considers its own, once-in-always-in. Kept
   // outside GroupNode because it is keyed by GraphNode::index, not anything
   // the node itself knows about. Membership only grows (see DrawGroupNode) -
   // a stale index (its node deleted, or undo/redo having rewound past it) is
   // simply skipped wherever this is read, never treated as an error.
   std::map<GroupNode*, std::set<int>> gGroupMembers;
   // Starts at 1, not 0: node index 0 would give NodeId 0, and the node editor
   // reserves 0 as its Invalid id. A node with that id still draws, but every
   // `if (ed::NodeId n = ed::GetHoveredNode())` silently reads false for it, so
   // the first node spawned in a patch could not be hovered, selected or
   // dragged the way every other node could.
   int gNextIndex = 1;
   ed::EditorContext* gEditor = nullptr;
   GraphNode* gSelfTestFeeder = nullptr;
   bool gPaletteTestOk = false;    // dev test only
   bool gPaletteTestPending = false;
   ImVec2 gSpawnPos(0.0f, 0.0f);

   struct LinkInfo
   {
      int id = 0;
      int srcPin = 0;
      int dstPin = 0;
   };
   const int kLinkIdBase = 4000000; // far above any node/pin id
   std::vector<LinkInfo> gLinks;

   const LinkInfo* FindLink(int id)
   {
      for (const LinkInfo& link : gLinks)
      {
         if (link.id == id)
            return &link;
      }
      return nullptr;
   }
   bool gSnapToGrid = true;
   float gGridSnap = 20.0f;
   double gLastFrameMs = 0.0; // wall clock across the previous whole frame
   double gFrameStart = 0.0;  // when the current frame began, for the limiter
   // 60 by default: a light patch spinning the GPU at 400fps costs power for
   // nothing, and a predictable budget makes the cost of a setting readable.
   int gTargetFps = 60;       // 0 = uncapped; otherwise the frame limiter's budget
   bool gVsync = true;
   bool gRequestFitView = false;
   // Set by the Edit menu, consumed next to the matching keyboard shortcuts.
   // The menu bar draws outside ed::Begin/End, and grouping needs the live
   // selection, so both routes meet in one place inside the editor frame
   // rather than the menu reaching into the editor from outside it.
   bool gRequestGroup = false;
   bool gRequestUngroup = false;
   int gContextMenuNodeIndex = -1; // node the right-click context menu is open for
   int gHelpPopupNodeIndex = -1; // node the per-node "Help" popup is open for
   bool gOpenNodeHelpPopup = false; // set for one frame to open it (can't OpenPopup from inside another popup's Begin/End and have it show the same frame)
   // The node browser lives in a docked panel rather than only the canvas popup,
   // so modules can be found without knowing the double-click gesture exists.
   bool gNodePanelOpen = false;
   // The dockable viewport panel: every node index in this list gets its own
   // card, stacked left-to-right when bottom-docked or top-to-bottom when
   // right/left-docked (see DrawViewportPanelContainer). The panel is showing
   // at all iff this is non-empty. Session UI state only, like gNodePanelOpen
   // above - not serialized to patch data or tracked by undo.
   std::vector<int> gViewportPanelNodes;
   int gViewportPanelDock = 1;        // 0 = bottom, 1 = right, 2 = left
   ImVec2 gViewCenterCanvas(0.0f, 0.0f); // captured inside the editor for spawning
   ImVec2 gGraphScreenTL(0.0f, 0.0f);    // graph canvas's screen-space rect,
   ImVec2 gGraphScreenSize(0.0f, 0.0f);  // captured the same way, for the minimap overlay
   bool gMinimapEnabled = false;
   // Bottom-left by default: the module browser docks on the right, and the
   // minimap draws (and takes its clicks) on the foreground draw list, so a
   // right-hand corner would sit on top of the panel and swallow clicks meant
   // for the module list.
   int gMinimapCorner = 2; // 0=TL, 1=TR, 2=BL, 3=BR
   float gMinimapSize = 190.0f;
   float gMinimapOpacity = 0.85f;
   float gZoomSensitivity = 0.5f;
   bool gHoveringItem = false;   // last frame: cursor over a node/pin/link
   bool gPanWithLeft = false;    // current left-drag is a canvas pan, not a select
   ImVec2 gDragTestNodeScreen(0.0f, 0.0f);
   ImVec2 gDragTestNodePos(0.0f, 0.0f);
   ImVec2 gTestMouse(0.0f, 0.0f);
   ImVec2 gDragTestViewAnchor(0.0f, 0.0f);

   // ---- deferred dropdown -------------------------------------------------
   // ImGui combos opened inside a node get clipped and mis-scaled by the node
   // editor's canvas transform. Instead a node draws a plain button, records
   // what it wants to show, and the popup is rendered once per frame outside
   // the canvas (inside ed::Suspend) as a normal, scrollable ImGui popup.
   struct DropdownRequest
   {
      const std::vector<std::string>* options = nullptr;
      std::function<void(int)> onSelect;
      int current = 0;
      bool justOpened = false;
   };
   DropdownRequest gDropdown;

   // Same story for ImGui's colour picker: opened inside a node it inherits the
   // canvas transform and the hue bar / sliders stop tracking the cursor. Route
   // it through a popup rendered outside the canvas instead.
   struct ColorRequest
   {
      float* target = nullptr;
      const INode* owner = nullptr; // revalidated each frame; nodes can be deleted
      std::string label;
      bool justOpened = false;
   };
   ColorRequest gColor;
   ImVec4 gColorPickerRect(0, 0, 0, 0); // x, y, w, h of the picker widget on screen

   // And again for a comment's text. A multiline text field is an ImGui child
   // window, which the canvas cannot transform at all: drawn inside the node it
   // rendered the note at the canvas origin, nowhere near the comment it
   // belonged to. So the note is painted into the node by hand and typing into
   // it happens in a popup out here (see DrawCommentPreview).
   struct CommentEditRequest
   {
      CommentNode* target = nullptr; // revalidated each frame; nodes can be deleted
      bool justOpened = false;
      // The double-click that opens the editor is also a click on the canvas,
      // and the canvas takes the focus back when the button comes up - one
      // frame after the popup appeared. So the field asks for the keyboard for
      // the first few frames rather than only on the frame it appears, which
      // otherwise left it open but dead until it was clicked a third time.
      int framesOpen = 0;
   };
   CommentEditRequest gCommentEdit;
   // Screen-space rect of the last comment body drawn, so the dev test can aim
   // a synthetic double-click at it. Inside a node ImGui draws in canvas space,
   // hence the explicit conversion where this is filled in.
   ImVec4 gCommentBodyRect(0, 0, 0, 0); // x, y, w, h
   // Screen-space rect of whichever comment is currently open for editing,
   // refreshed every frame so the edit popup can be pinned exactly on top of
   // it - the point being that typing looks like it happens straight into the
   // node's own box rather than in a separate window somewhere else on screen.
   ImVec4 gCommentEditRect(0, 0, 0, 0); // x, y, w, h
   FormulaNode* gFormulaEditor = nullptr;
   bool gFormulaEditorOpen = false;
   bool gHelpOpen = false;

   // Files dropped on the window, consumed on the next frame so the spawn can
   // happen inside the editor where canvas coordinates are meaningful.
   std::vector<std::string> gDroppedFiles;
   ImVec2 gDropPos(0.0f, 0.0f);

   bool HasExtension(const std::string& path, const std::vector<std::string>& exts)
   {
      size_t dot = path.find_last_of('.');
      if (dot == std::string::npos)
         return false;
      std::string ext = path.substr(dot + 1);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      for (const std::string& e : exts)
      {
         if (ext == e)
            return true;
      }
      return false;
   }

   void OnFilesDropped(GLFWwindow*, int count, const char** paths)
   {
      gDropPos = ImGui::GetMousePos();
      for (int i = 0; i < count; i++)
         gDroppedFiles.push_back(paths[i]);
   }

   void DropdownButton(const char* label, const std::vector<std::string>& options,
                       int current, std::function<void(int)> onSelect)
   {
      if (options.empty())
         return;
      int safeCurrent = std::max(0, std::min(current, (int)options.size() - 1));
      std::string caption = options[safeCurrent] + "##" + label;
      if (ImGui::Button(caption.c_str(), ImVec2(kParamWidth, 0)))
      {
         gDropdown.options = &options;
         gDropdown.onSelect = std::move(onSelect);
         gDropdown.current = safeCurrent;
         gDropdown.justOpened = true;
      }
      ImGui::SameLine();
      // anything after "##" is an ImGui uniquifier, not part of the visible name
      std::string shown(label);
      size_t hash = shown.find("##");
      if (hash != std::string::npos)
         shown = shown.substr(0, hash);
      ImGui::TextDisabled("%s", shown.c_str());
   }

   // ---- modulatable parameters --------------------------------------------
   // Every slider that can be driven by a modulator goes through ModSlider. It
   // draws an input pin beside the control, registers the parameter for this
   // frame so the modulator can write into it, and shows the live value (and
   // locks the slider) while something is patched in.
   int gCurrentNodeIndex = -1;
   int gParamCounter = 0;
   // Colour pins are counted separately from parameter pins (see
   // GraphNode::kColorBase): sharing the counter would renumber every slider
   // that follows a swatch and repoint existing patches' modulation.
   int gColorCounter = 0;
   std::set<int> gDrawnColorPins;
   // Defined further down, once the gNodes lookup exists.
   IPaletteSource* PaletteSourceByIndex(int nodeIndex);
   std::set<std::pair<int, int>> gTypedParam;                 // params showing a text field
   // Params that entered typing mode via hover+type rather than double-click;
   // these get the cursor placed at the end instead of a full selection, so
   // the seeded digit is appended to rather than replaced by the next keystroke.
   std::set<std::pair<int, int>> gTypedParamNoAutoSelect;
   // Params whose InputFloat selection/cursor state still needs to be forced
   // once the field is confirmed active (see the comment at its use site).
   std::set<std::pair<int, int>> gTypedParamPendingInit;
   // Live text for the field currently being typed into. A plain number
   // commits as a value and clears any expression; text starting with '='
   // commits as an expression (see ModSlider) - the same field doubles as
   // both, like a spreadsheet cell.
   std::map<std::pair<int, int>, std::string> gTypedParamText;

   std::string TrimCopy(const std::string& s)
   {
      size_t start = s.find_first_not_of(" \t");
      if (start == std::string::npos)
         return std::string();
      size_t end = s.find_last_not_of(" \t");
      return s.substr(start, end - start + 1);
   }
   // Param pins declared this frame. A node with its params collapsed declares
   // none, and emitting a link to an undeclared pin makes the editor treat the
   // link as dead and delete it - which silently dropped the modulation.
   std::set<int> gDrawnParamPins;
   // Nodes to select next frame; they do not exist in the editor until they
   // have been drawn once, so selection has to wait a frame.
   std::vector<int> gPendingSelect;
   std::pair<int, int> gTypedParamJustOpened(-1, -1);
   std::vector<int> gParamPinsThisFrame;
   // Set when a right-click on a param already opened its text field this
   // frame, so the node-level right-click context menu (checked later, after
   // ed::Suspend()) knows to stay closed instead of covering the field.
   bool gParamRightClickConsumedThisFrame = false;

   void BeginNodeParams(int nodeIndex)
   {
      gCurrentNodeIndex = nodeIndex;
      gParamCounter = 0;
      gColorCounter = 0;
   }

   // Opens the text field for a param, seeded either from its current numeric
   // value or (if it's already driven by an expression) from that expression
   // text with its '=' prefix - used by both double-click and right-click.
   void BeginTypedEditFromCurrent(const std::pair<int, int>& editKey, int nodeIndex, int paramIndex,
                                   float* value, const char* fmt, bool hasExpr)
   {
      if (hasExpr)
      {
         const std::string* expr = Modulation::Instance().ExpressionFor(nodeIndex, paramIndex);
         gTypedParamText[editKey] = std::string("=") + (expr != nullptr ? *expr : std::string());
      }
      else
      {
         char seed[64];
         snprintf(seed, sizeof(seed), fmt, *value);
         gTypedParamText[editKey] = seed;
      }
      gTypedParam.insert(editKey);
      gTypedParamJustOpened = editKey;
      gTypedParamPendingInit.insert(editKey);
   }

   // Hovering a param (whether it's a plain slider or already showing an
   // expression) and pressing a digit/'-'/'.'/'=' jumps straight into typing
   // mode with that keystroke seeded, without needing to double-click first.
   // '=' always starts a blank formula - even over an existing one, so typing
   // '=' is a reliable way back into formula mode - and a digit always starts
   // a blank numeric entry, overriding whatever mode the param was already in.
   void HandleParamTypeHotkeys(const std::pair<int, int>& editKey, float* value)
   {
      ImGuiIO& io = ImGui::GetIO();
      for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
      {
         ImWchar ch = io.InputQueueCharacters[i];
         if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '=')
         {
            PushUndoCheckpoint();
            if (ch == '=')
               gTypedParamText[editKey] = "=";
            else if (ch >= '0' && ch <= '9')
            {
               *value = (float)(ch - '0');
               gTypedParamText[editKey] = std::string(1, (char)ch);
            }
            else if (ch == '-')
            {
               *value = -0.0f;
               gTypedParamText[editKey] = "-";
            }
            else
            {
               *value = 0.0f;
               gTypedParamText[editKey] = ".";
            }
            gTypedParam.insert(editKey);
            gTypedParamJustOpened = editKey;
            gTypedParamNoAutoSelect.insert(editKey);
            gTypedParamPendingInit.insert(editKey);
            break;
         }
      }
   }

   bool ModSlider(const char* label, float* value, float minV, float maxV, const char* fmt = "%.3f")
   {
      const int nodeIndex = gCurrentNodeIndex;
      const int paramIndex = gParamCounter++;

      ParamRef ref;
      ref.nodeIndex = nodeIndex;
      ref.paramIndex = paramIndex;
      ref.value = value;
      ref.minValue = minV;
      ref.maxValue = maxV;
      ref.name = label;
      Modulation::Instance().RegisterParam(ref);

      const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kParamBase + paramIndex;
      const bool modulated = Modulation::Instance().IsModulated(nodeIndex, paramIndex);
      // An expression only actually drives the field while nothing is wired
      // into its pin - see Modulation::SetExpression. It stays stored either
      // way, so unpatching the cable brings it straight back.
      const bool hasExpr = !modulated && Modulation::Instance().HasExpression(nodeIndex, paramIndex);
      // A stored expression that currently fails to parse/evaluate (bad
      // syntax, unknown function, division by zero) still counts as
      // "has an expression" - it stays queued for the moment it's fixed -
      // but nothing is overwriting *value while it's broken (see the apply
      // pass), so it's safe to just fall back to the plain, fully-interactive
      // slider look rather than a special locked/coloured state. The fx/x
      // controls stay so the stored formula can still be reopened or cleared.
      const std::string* hasExprErr = hasExpr ? Modulation::Instance().ExpressionErrorFor(nodeIndex, paramIndex)
                                               : nullptr;
      const bool exprErrored = hasExprErr != nullptr && !hasExprErr->empty();
      gDrawnParamPins.insert(pinId);

      ImGui::PushID(paramIndex + 5000);

      ed::BeginPin(pinId, ed::PinKind::Input);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
      ImVec2 p = ImGui::GetCursorScreenPos();
      const float box = 14.0f;
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      const ImU32 pinColor = modulated              ? IM_COL32(255, 190, 90, 255)
                            : hasExpr && !exprErrored ? IM_COL32(170, 130, 255, 255)
                                                      : IM_COL32(95, 100, 120, 255);
      dl->AddCircleFilled(c, 4.0f, pinColor);
      ed::EndPin();
      ImGui::SameLine(0.0f, 4.0f);

      // Double-clicking swaps the slider for a text field so an exact value
      // (or, prefixed with '=', an expression) can be typed. ImGui's built-in
      // Ctrl+click does this too, but double-click is what people reach for.
      const std::pair<int, int> editKey(nodeIndex, paramIndex);
      bool typing = gTypedParam.count(editKey) > 0;

      bool changed = false;
      if (typing && !modulated)
      {
         ImGui::SetNextItemWidth(kParamWidth - box - 4.0f);
         if (gTypedParamJustOpened == editKey)
         {
            ImGui::SetKeyboardFocusHere();
            gTypedParamJustOpened = std::pair<int, int>(-1, -1);
         }
         char buf[256];
         snprintf(buf, sizeof(buf), "%s", gTypedParamText[editKey].c_str());
         // A plain InputText rather than InputFloat: the field has to accept
         // letters too, so a typed expression like "sin(t)" is not filtered
         // out character-by-character the way InputFloat's numeric-only input
         // would filter it.
         //
         // ImGuiInputTextFlags_AutoSelectAll only selects on a *mouse* click;
         // this field is focused programmatically via SetKeyboardFocusHere,
         // which instead runs through ImGui's nav-activation path and forces
         // its own select-all the moment the field actually goes active
         // (which can land a frame after SetKeyboardFocusHere is called) -
         // regardless of any flag we pass. So don't rely on flags for
         // selection at all: drive it explicitly below once the field is
         // confirmed active, which also lets the hover+type path leave the
         // cursor at the end instead of selecting the seeded digit.
         const bool entered = ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
         gTypedParamText[editKey] = buf;
         if (gTypedParamPendingInit.count(editKey) && ImGui::IsItemActive())
         {
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID()))
            {
               if (gTypedParamNoAutoSelect.count(editKey))
               {
                  state->Stb.cursor = state->CurLenW;
                  state->ClearSelection();
               }
               else
               {
                  state->SelectAll();
               }
            }
            gTypedParamPendingInit.erase(editKey);
         }
         // IsItemActivated() has to be queried right after its widget, and it
         // fires on the frame focus begins - before any edit is applied that
         // same frame - so the checkpoint still captures the pre-edit value
         // even though the check reads textually "after" InputText here.
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (entered || ImGui::IsItemDeactivated())
         {
            const std::string trimmed = TrimCopy(gTypedParamText[editKey]);
            if (!trimmed.empty() && trimmed[0] == '=')
            {
               const std::string exprText = TrimCopy(trimmed.substr(1));
               if (exprText.empty())
                  Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               else
                  Modulation::Instance().SetExpression(nodeIndex, paramIndex, exprText);
            }
            else
            {
               Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               if (!trimmed.empty())
               {
                  char* end = nullptr;
                  float parsed = strtof(trimmed.c_str(), &end);
                  if (end != trimmed.c_str())
                  {
                     *value = parsed;
                     changed = true;
                  }
               }
            }
            gTypedParam.erase(editKey);
            gTypedParamText.erase(editKey);
            gTypedParamNoAutoSelect.erase(editKey);
            gTypedParamPendingInit.erase(editKey);
         }
      }
      else if (modulated)
      {
         // value is driven externally; show it read-only so it is obvious why
         // dragging does nothing
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.32f, 0.24f, 0.08f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.95f, 0.72f, 0.32f, 1.0f));
         float shown = *value;
         ImGui::SetNextItemWidth(kParamWidth - box - 4.0f);
         ImGui::SliderFloat(label, &shown, minV, maxV, fmt, ImGuiSliderFlags_NoInput);
         ImGui::PopStyleColor(2);
      }
      else if (hasExpr && !exprErrored)
      {
         // Driven by a typed expression, re-evaluated every frame by the
         // apply pass in the main loop. Read-only like the modulated state,
         // but a distinct colour and an fx badge. Double-click reopens the
         // expression text for editing, same as any other field.
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.20f, 0.15f, 0.32f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.66f, 0.51f, 0.98f, 1.0f));
         float shown = *value;
         ImGui::SetNextItemWidth(kParamWidth - box - 4.0f);
         ImGui::SliderFloat(label, &shown, minV, maxV, fmt, ImGuiSliderFlags_NoInput);
         const bool sliderHovered = ImGui::IsItemHovered();
         ImGui::PopStyleColor(2);
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::TextColored(ImVec4(0.66f, 0.51f, 0.98f, 1.0f), "fx");
         const bool hovered = sliderHovered || ImGui::IsItemHovered();
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
         if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
            gParamRightClickConsumedThisFrame = true;
         }
         if (hovered)
            HandleParamTypeHotkeys(editKey, value);
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
         if (ImGui::SmallButton("x"))
            Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
         ImGui::PopStyleColor(3);
      }
      else if (hasExpr) // exprErrored
      {
         // The stored expression is currently failing to evaluate. Nothing
         // is writing to *value while that's true (see the apply pass), so
         // there's nothing unsafe about just falling back to a plain,
         // fully-interactive slider on the real value - no special colour,
         // no lock, and no fx/x badge either: it looks exactly like a param
         // with no expression at all. The stored (broken) formula is still
         // reachable to fix or clear via double-click, right-click, or
         // hovering and typing '=' - see HandleParamTypeHotkeys - same as it
         // would be if this were a fresh, non-expression param.
         ImGui::SetNextItemWidth(kParamWidth - box - 4.0f);
         changed = ImGui::SliderFloat(label, value, minV, maxV, fmt);
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         const bool hovered = ImGui::IsItemHovered();
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
         if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
            gParamRightClickConsumedThisFrame = true;
         }
         if (hovered && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }
      else
      {
         ImGui::SetNextItemWidth(kParamWidth - box - 4.0f);
         changed = ImGui::SliderFloat(label, value, minV, maxV, fmt);
         // Activation is the first frame of the drag, before that frame's own
         // delta is applied, so this is still the pre-drag value.
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/false);
         // Right-click also jumps straight into the text field, same as
         // double-click - and marks the click consumed so the node-level
         // right-click context menu doesn't also try to open over it.
         if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/false);
            gParamRightClickConsumedThisFrame = true;
         }
         // Hovering (not dragging) and pressing a digit/'-'/'.'/'=' starts a
         // fresh typed value (or expression) immediately, without needing to
         // double-click first. IsItemActive() guards against a mouse-drag
         // coinciding with a keypress from stealing the drag into typing mode.
         if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }

      ImGui::PopID();
      return changed;
   }

   // Integer parameter that is still modulatable. The backing float lives in a
   // std::map so its address stays valid for the whole frame (node references are
   // stable), which matters because the modulation pass writes through that pointer.
   std::map<std::pair<int, int>, float> gIntParamStore;

   bool ModSliderInt(const char* label, int* value, int minV, int maxV)
   {
      const std::pair<int, int> key(gCurrentNodeIndex, gParamCounter);
      float& slot = gIntParamStore[key];
      if (!Modulation::Instance().IsModulated(key.first, key.second))
         slot = (float)*value;

      bool changed = ModSlider(label, &slot, (float)minV, (float)maxV, "%.0f");
      *value = (int)(slot + 0.5f);
      return changed;
   }

   // ---- palette-bindable colours ------------------------------------------
   // The colour counterpart of ModSlider. Every swatch in the app already goes
   // through this one function, so giving it a pin here is what makes every
   // colour in every node bindable to a Palette at once - the alternative was
   // adding a pin by hand at fifty call sites and missing some.
   //
   // Colour pins are counted separately from parameter pins (see
   // GraphNode::kColorBase) so that adding one to a node cannot renumber the
   // sliders around it and repoint existing patches' modulation.
   void ColorSwatch(const char* label, float* col, const INode* owner)
   {
      const int nodeIndex = gCurrentNodeIndex;
      const int colorIndex = gColorCounter++;

      ColorRef ref;
      ref.nodeIndex = nodeIndex;
      ref.colorIndex = colorIndex;
      ref.value = col;
      ref.name = label;
      PaletteBinding::Instance().RegisterColor(ref);

      const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kColorBase + colorIndex;
      const PaletteBinding::Source bound =
         PaletteBinding::Instance().SourceFor(nodeIndex, colorIndex);
      const bool isBound = bound.nodeIndex >= 0;
      gDrawnColorPins.insert(pinId);

      ImGui::PushID(label);
      ImGui::PushID(colorIndex + 9000);

      ed::BeginPin(pinId, ed::PinKind::Input);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float box = 14.0f;
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      // Square where a modulation pin is round: the two accept different cables
      // and sit right next to each other, so they should not look alike.
      dl->AddRectFilled(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f),
                        isBound ? IM_COL32(130, 220, 190, 255) : IM_COL32(95, 100, 120, 255),
                        1.0f);
      ed::EndPin();
      ImGui::SameLine(0.0f, 4.0f);

      if (ImGui::ColorButton(label, ImVec4(col[0], col[1], col[2], 1.0f),
                             ImGuiColorEditFlags_NoTooltip, ImVec2(38, 0)))
      {
         if (isBound)
         {
            // A bound colour is rewritten from the palette every frame, so
            // opening the picker on it would show an edit that vanishes on the
            // next tick. Step to the next swatch instead, which is the edit
            // someone is actually reaching for here.
            PushUndoCheckpoint();
            int count = 1;
            if (IPaletteSource* source = PaletteSourceByIndex(bound.nodeIndex))
               count = std::max(1, source->SwatchCount());
            PaletteBinding::Instance().Bind(nodeIndex, colorIndex, bound.nodeIndex,
                                            (bound.swatchIndex + 1) % count);
         }
         else
         {
            // Captured once, at the moment the picker opens rather than per-frame
            // while it's being dragged - the picker writes into *col continuously,
            // so anywhere later would already see the edited value.
            PushUndoCheckpoint();
            gColor.target = col;
            gColor.owner = owner;
            gColor.label = label;
            gColor.justOpened = true;
         }
      }
      ImGui::SameLine();
      if (isBound)
         ImGui::TextColored(ImVec4(0.5f, 0.86f, 0.74f, 1.0f), "%s  #%d",
                            label, bound.swatchIndex + 1);
      else
         ImGui::TextDisabled("%s", label);

      ImGui::PopID();
      ImGui::PopID();
   }

   // A node with its params collapsed draws no param or colour pins, and a link
   // pointing at an undeclared pin is dead to the editor - so every cable feeding
   // a collapsed node used to vanish the moment the eye was closed, even though
   // the binding was still live and driving the value.
   //
   // Declare a stub pin for each bound parameter/colour instead, all stacked on
   // the collapsed "mod"/"pal" tag. The pins are 1px and invisible; they exist
   // purely so the cable has somewhere to land, and they make the tag itself the
   // node's collapsed landing point.
   void CollapsedBindingPins(int nodeIndex, const ImVec2& tagMin, const ImVec2& tagMax, bool colors)
   {
      const ImVec2 anchor((tagMin.x + tagMax.x) * 0.5f, (tagMin.y + tagMax.y) * 0.5f);
      const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();

      auto stub = [&](int pinId)
      {
         ImGui::SetCursorScreenPos(ImVec2(anchor.x - 0.5f, anchor.y - 0.5f));
         ed::BeginPin(pinId, ed::PinKind::Input);
         ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
         ImGui::Dummy(ImVec2(1.0f, 1.0f));
         ed::EndPin();
      };

      if (colors)
      {
         for (const auto& link : PaletteBinding::Instance().Links())
         {
            if (link.first.first != nodeIndex)
               continue;
            const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kColorBase + link.first.second;
            gDrawnColorPins.insert(pinId);
            stub(pinId);
         }
      }
      else
      {
         for (const auto& link : Modulation::Instance().Links())
         {
            if (link.first.first != nodeIndex)
               continue;
            const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kParamBase + link.first.second;
            gDrawnParamPins.insert(pinId);
            stub(pinId);
         }
      }

      // The stubs are placed out of layout order, so hand the cursor back where
      // the tag row left it. Call this only once the whole row is drawn: nothing
      // after it may rely on SameLine().
      ImGui::SetCursorScreenPos(restoreCursor);
   }


   // ImGui's Separator / SeparatorText span the available content width, and
   // inside a node that width is unbounded - the rule shot off across the whole
   // canvas. Draw our own, clamped to the node's preview width.
   void NodeSeparator(const char* label = nullptr)
   {
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float y = origin.y + 6.0f;
      const ImU32 col = IM_COL32(78, 82, 100, 255);

      if (label != nullptr && label[0] != '\0')
      {
         const ImVec2 textSize = ImGui::CalcTextSize(label);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + 10.0f, y), col);
         dl->AddText(ImVec2(origin.x + 16.0f, origin.y), IM_COL32(150, 156, 180, 255), label);
         const float lineStart = origin.x + 22.0f + textSize.x;
         if (lineStart < origin.x + kPreviewSize)
            dl->AddLine(ImVec2(lineStart, y), ImVec2(origin.x + kPreviewSize, y), col);
         ImGui::Dummy(ImVec2(kPreviewSize, textSize.y));
      }
      else
      {
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + kPreviewSize, y), col);
         ImGui::Dummy(ImVec2(kPreviewSize, 8.0f));
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
   }

   const std::vector<std::string>& AlignOptions()
   {
      static const std::vector<std::string> kAlign = { "Left", "Center", "Right", "Justified" };
      return kAlign;
   }

   // Small eye toggle. Drawn rather than typed: the UI font has no eye glyph,
   // and an emoji would not render in a non-emoji face.
   bool EyeToggle(bool shown)
   {
      const float w = 26.0f;
      const float h = 18.0f;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const bool pressed = ImGui::InvisibleButton("##eye", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(origin.x + w * 0.5f, origin.y + h * 0.5f);
      ImU32 col = hovered ? IM_COL32(235, 240, 255, 255)
                          : (shown ? IM_COL32(150, 190, 255, 255) : IM_COL32(120, 124, 140, 255));

      // almond outline: two arcs meeting at the corners
      const float rx = 9.0f, ry = 5.5f;
      dl->PathClear();
      for (int i = 0; i <= 16; i++)
      {
         float t = (float)i / 16.0f;
         float x = -rx + 2.0f * rx * t;
         float y = -ry * (float)sin(3.14159f * t);
         dl->PathLineTo(ImVec2(c.x + x, c.y + y));
      }
      for (int i = 0; i <= 16; i++)
      {
         float t = (float)i / 16.0f;
         float x = rx - 2.0f * rx * t;
         float y = ry * (float)sin(3.14159f * t);
         dl->PathLineTo(ImVec2(c.x + x, c.y + y));
      }
      dl->PathStroke(col, ImDrawFlags_Closed, 1.6f);

      if (shown)
         dl->AddCircleFilled(c, 2.6f, col);
      else
         dl->AddLine(ImVec2(c.x - rx, c.y + ry * 0.9f), ImVec2(c.x + rx, c.y - ry * 0.9f), col, 1.6f);

      return pressed;
   }

   // Small monitor/screen toggle for a node's own mini 3D viewport. Deliberately
   // distinct from EyeToggle (params visibility) - this switches on a live
   // render pass, not just a UI panel, so it gets its own affordance rather
   // than overloading the eye icon.
   bool ViewportToggle(bool shown)
   {
      const float w = 22.0f;
      const float h = 18.0f;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const bool pressed = ImGui::InvisibleButton("##miniviewport", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(origin.x + w * 0.5f, origin.y + h * 0.5f);
      ImU32 col = hovered ? IM_COL32(235, 240, 255, 255)
                          : (shown ? IM_COL32(150, 190, 255, 255) : IM_COL32(120, 124, 140, 255));

      // A little screen/monitor glyph: rounded rect body plus a stand, filled
      // when the viewport is on so it reads at a glance in a busy graph.
      const float bw = 13.0f, bh = 9.0f;
      ImVec2 tl(c.x - bw * 0.5f, c.y - bh * 0.5f - 1.0f);
      ImVec2 br(c.x + bw * 0.5f, c.y + bh * 0.5f - 1.0f);
      if (shown)
         dl->AddRectFilled(tl, br, col, 1.5f);
      else
         dl->AddRect(tl, br, col, 1.5f, 0, 1.4f);
      dl->AddLine(ImVec2(c.x, br.y), ImVec2(c.x, br.y + 2.5f), col, 1.4f);
      dl->AddLine(ImVec2(c.x - 3.5f, br.y + 2.5f), ImVec2(c.x + 3.5f, br.y + 2.5f), col, 1.4f);

      return pressed;
   }

   // ---- pins --------------------------------------------------------------
   // Drawn inside a kPinHit-wide box so the clickable area is far larger than
   // the visible dot; connecting used to require pixel-perfect aim.
   void DrawPin(int pinId, ed::PinKind kind, const char* label, bool labelFirst = false)
   {
      ed::BeginPin(pinId, kind);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));

      if (labelFirst && label != nullptr && label[0] != '\0')
      {
         ImGui::TextDisabled("%s", label);
         ImGui::SameLine(0.0f, 4.0f);
      }

      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(kPinHit, kPinHit));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(p.x + kPinHit * 0.5f, p.y + kPinHit * 0.5f);
      dl->AddCircleFilled(c, kPinRadius, IM_COL32(150, 190, 255, 255));
      dl->AddCircle(c, kPinRadius, IM_COL32(20, 22, 30, 255), 0, 1.5f);

      if (!labelFirst && label != nullptr && label[0] != '\0')
      {
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::TextDisabled("%s", label);
      }
      ed::EndPin();
   }

   void RegisterNodes()
   {
      REGISTER_NODE(ImageSourceNode, Image Source, "Source");
      REGISTER_NODE(ShapeNode, Shape, "Source");
      for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
      {
         NodeFactory::Instance().Register(
            ShapeNode::ShapeNames()[i],
            [i]() -> INode* { return ShapeNode::CreateFor(i); }, "Source");
      }
      REGISTER_NODE(FormulaNode, Formula, "Source");
      REGISTER_NODE(TextNode, Text, "Text");
      REGISTER_NODE(VideoSourceNode, Video, "Source");
      REGISTER_NODE(NoiseNode, Noise, "Source");
      REGISTER_NODE(TextureNode, Texture, "Source");
      REGISTER_NODE(RampNode, Ramp, "Source");
      REGISTER_NODE(GeometryNode, Geometry, "3D");
      // Every primitive as its own searchable node, sharing one class.
      for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
      {
         NodeFactory::Instance().Register(
            GeometryNode::ShapeNames()[i],
            [i]() -> INode* { return GeometryNode::CreateFor(i); }, "3D");
      }
      REGISTER_NODE(ModelSourceNode, Model 3D, "3D");
      REGISTER_NODE(Text3DNode, Text 3D, "3D");
      REGISTER_NODE(Null3DNode, Null 3D, "3D");
      REGISTER_NODE(OceanNode, Ocean, "3D");
      REGISTER_NODE(MaterialNode, Material, "3D");
      REGISTER_NODE(DisplacementNode, Displacement, "3D");
      REGISTER_NODE(MappingNode, Mapping, "3D");
      REGISTER_NODE(ParticleSystemNode, Particle System, "3D");
      REGISTER_NODE(ClothNode, Cloth, "3D");
      REGISTER_NODE(JoinGeometryNode, Join Geometry, "3D");
      // The boolean modes as their own nodes: "difference" is what gets
      // searched for, not "join geometry with a dropdown set to difference".
      for (int i = 1; i < JoinGeometryNode::kModeCount; i++)
      {
         NodeFactory::Instance().Register(
            JoinGeometryNode::ModeNames()[i],
            [i]() -> INode* { return JoinGeometryNode::CreateFor(i); }, "3D");
      }
      REGISTER_NODE(MetaBallNode, Metaballs, "3D");
      REGISTER_NODE(MeshResynthNode, Resynthesize 3D, "3D");
      REGISTER_NODE(ImageToPointsNode, Image to Points, "3D");
      REGISTER_NODE(CurveNode, Curve, "3D");
      // Three names, one class - Points/Edges/Faces are the same sampler.
      for (int i = 0; i < 3; i++)
      {
         static const char* kNames[] = { "Mesh to Points", "Mesh to Edges", "Mesh to Faces" };
         NodeFactory::Instance().Register(
            kNames[i], [i]() -> INode* { return MeshToPointsNode::CreateFor(i); }, "3D");
      }
      // Ten named operator nodes, all backed by GeometryOpNode.
      for (int i = 0; i < GeometryOpNode::kOpCount; i++)
      {
         NodeFactory::Instance().Register(
            GeometryOpNode::OpNames()[i],
            [i]() -> INode* { return GeometryOpNode::CreateFor(i); },
            "3D");
      }
      REGISTER_NODE(InstanceOnPointsNode, Instance on Points, "3D");
      REGISTER_NODE(WrapNode, Wrap, "3D");
      REGISTER_NODE(Switcher3DNode, Switcher 3D, "3D");
      REGISTER_NODE(CameraNode, Camera, "3D");
      REGISTER_NODE(LightNode, Light, "3D");
      REGISTER_NODE(EnvironmentNode, HDRI, "3D");
      REGISTER_NODE(Render3DNode, Render 3D, "3D");
      REGISTER_NODE(DrawNode, Draw, "Source");
      REGISTER_NODE(ResynthNode, Resynthesize, "Resynth");
      REGISTER_NODE(FitNode, Fit, "Compositing");
      REGISTER_NODE(CommentNode, Comment, "Compositing");
      REGISTER_NODE(GroupNode, Group, "Compositing");
      REGISTER_NODE(NullNode, Null, "Compositing");
      REGISTER_NODE(ViewportNode, Viewport, "Compositing");
      REGISTER_NODE(CurvesNode, Curves, "Color");
      REGISTER_NODE(ColorRampNode, Color Ramp, "Color");
      REGISTER_NODE(RemoveBgNode, Remove Background, "Mask");
      REGISTER_NODE(FeedbackNode, Feedback, "Feedback");
      REGISTER_NODE(TrailsNode, Trails, "Feedback");
      REGISTER_NODE(ReactionDiffusionNode, Reaction Diffusion, "Feedback");
      REGISTER_NODE(BlendNode, Blend, "Compositing");
      REGISTER_NODE(LayerStackNode, Layer Stack, "Compositing");
      REGISTER_NODE(SwitcherNode, Switcher, "Compositing");
      REGISTER_NODE(OutputNode, Output, "Output");
      REGISTER_NODE(LFONode, LFO, "Modulators");
      REGISTER_NODE(RandomNode, Random, "Modulators");
      REGISTER_NODE(PatternNode, Pattern, "Modulators");
      REGISTER_NODE(MathNode, Math, "Modulators");
      REGISTER_NODE(MacroKnobNode, Macro Knob, "Modulators");
      REGISTER_NODE(MacroXYNode, Macro XY, "Modulators");
      REGISTER_NODE(PathNode, Path, "Modulators");
      REGISTER_NODE(ConstantNode, Constant, "Modulators");
      REGISTER_NODE(ImageAnalyzeNode, Image Analyze, "Modulators");
      REGISTER_NODE(PaletteNode, Palette, "Modulators");
      REGISTER_NODE(AudioFileNode, Audio File, "Modulators");
      REGISTER_NODE(AudioAnalyzeNode, Audio Analyze, "Modulators");

      // Every entry in the filter table becomes its own spawnable node type,
      // all sharing FilterNode. `def` is a reference into the static table, so
      // capturing it by pointer is safe for the process lifetime.
      for (const FilterDef& def : GetFilterDefs())
      {
         const FilterDef* defPtr = &def;
         NodeFactory::Instance().Register(
            def.name,
            [defPtr]() -> INode* { return FilterNode::CreateFor(*defPtr); },
            def.category);
      }
   }

   // Restyles ImGui's whole palette plus the node-editor canvas from the
   // active CategoryColors preset, so switching presets re-themes the app
   // rather than just the graph. Mutates ImGuiStyle/ed::Style directly
   // (like StyleColorsDark() itself), not a push/pop - it's meant to stick
   // until the user picks a different preset. Node header/border tinting
   // is separate, applied per-node from the same preset's category table.
   void ApplyTheme()
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      auto vec = [](const CategoryColors::Color& c, float a = 1.0f) {
         return ImVec4(c.r, c.g, c.b, a);
      };
      auto lighten = [](const CategoryColors::Color& c, float amt) {
         return ImVec4(c.r + (1.0f - c.r) * amt, c.g + (1.0f - c.g) * amt,
                       c.b + (1.0f - c.b) * amt, 1.0f);
      };

      ImGuiStyle& style = ImGui::GetStyle();
      style.Colors[ImGuiCol_Text] = vec(t.text);
      style.Colors[ImGuiCol_TextDisabled] = vec(t.textDim);
      style.Colors[ImGuiCol_WindowBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_ChildBg] = vec(t.panelBg, 0.0f);
      style.Colors[ImGuiCol_PopupBg] = vec(t.panelBg, 0.98f);
      style.Colors[ImGuiCol_Border] = vec(t.border);
      style.Colors[ImGuiCol_FrameBg] = vec(t.panelBg);
      style.Colors[ImGuiCol_FrameBgHovered] = lighten(t.panelBg, 0.12f);
      style.Colors[ImGuiCol_FrameBgActive] = lighten(t.panelBg, 0.20f);
      style.Colors[ImGuiCol_TitleBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_TitleBgActive] = vec(t.windowBg);
      style.Colors[ImGuiCol_TitleBgCollapsed] = vec(t.windowBg, 0.75f);
      style.Colors[ImGuiCol_MenuBarBg] = vec(t.panelBg);
      style.Colors[ImGuiCol_ScrollbarBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_ScrollbarGrab] = vec(t.border);
      style.Colors[ImGuiCol_ScrollbarGrabHovered] = lighten(t.border, 0.25f);
      style.Colors[ImGuiCol_ScrollbarGrabActive] = vec(t.accent);
      style.Colors[ImGuiCol_CheckMark] = vec(t.accent);
      style.Colors[ImGuiCol_SliderGrab] = vec(t.accent, 0.85f);
      style.Colors[ImGuiCol_SliderGrabActive] = vec(t.accent);
      style.Colors[ImGuiCol_Button] = vec(t.panelBg);
      style.Colors[ImGuiCol_ButtonHovered] = lighten(t.panelBg, 0.18f);
      style.Colors[ImGuiCol_ButtonActive] = vec(t.accent, 0.65f);
      style.Colors[ImGuiCol_Header] = vec(t.accent, 0.30f);
      style.Colors[ImGuiCol_HeaderHovered] = vec(t.accent, 0.45f);
      style.Colors[ImGuiCol_HeaderActive] = vec(t.accent, 0.60f);
      style.Colors[ImGuiCol_Separator] = vec(t.border);
      style.Colors[ImGuiCol_SeparatorHovered] = vec(t.accent, 0.6f);
      style.Colors[ImGuiCol_SeparatorActive] = vec(t.accent);
      style.Colors[ImGuiCol_ResizeGrip] = vec(t.border, 0.4f);
      style.Colors[ImGuiCol_ResizeGripHovered] = vec(t.accent, 0.6f);
      style.Colors[ImGuiCol_ResizeGripActive] = vec(t.accent);
      style.Colors[ImGuiCol_Tab] = vec(t.panelBg);
      style.Colors[ImGuiCol_TabHovered] = vec(t.accent, 0.5f);
      style.Colors[ImGuiCol_TabActive] = lighten(t.panelBg, 0.15f);
      style.Colors[ImGuiCol_TabUnfocused] = vec(t.windowBg);
      style.Colors[ImGuiCol_TabUnfocusedActive] = vec(t.panelBg);
      style.Colors[ImGuiCol_TextSelectedBg] = vec(t.accent, 0.35f);
      style.Colors[ImGuiCol_DragDropTarget] = vec(t.accent);
      style.Colors[ImGuiCol_NavHighlight] = vec(t.accent);

      // The node-graph canvas itself is drawn by imgui-node-editor from its
      // own style table, not ImGui's - Bg/Grid are the two slots visible
      // behind every node regardless of category. ed::GetStyle() reads
      // through the *current* editor context, which is null here when this
      // runs from the menu bar (that frame's ed::SetCurrentEditor(gEditor)
      // happens later, after the menu bar) - set/restore it explicitly
      // rather than relying on caller state, since the startup call and the
      // menu-bar call have different current-editor state at the time.
      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      ed::Style& edStyle = ed::GetStyle();
      edStyle.Colors[ed::StyleColor_Bg] = vec(t.windowBg, 0.784f);
      edStyle.Colors[ed::StyleColor_Grid] = vec(t.border, 0.35f);
      ed::SetCurrentEditor(prevEditor);
   }

   IModulator* ModulatorForOutput(INode* node, int outputIndex)
   {
      if (node == nullptr)
         return nullptr;
      if (IModulator* specific = node->ModulatorOutput(outputIndex))
         return specific;
      return outputIndex == 0 ? dynamic_cast<IModulator*>(node) : nullptr;
   }

   GraphNode* FindNodeByIndex(int index)
   {
      for (GraphNode& gn : gNodes)
      {
         if (gn.index == index)
            return &gn;
      }
      return nullptr;
   }

   IPaletteSource* PaletteSourceByIndex(int nodeIndex)
   {
      GraphNode* gn = FindNodeByIndex(nodeIndex);
      return gn ? dynamic_cast<IPaletteSource*>(gn->node.get()) : nullptr;
   }

   // How many image inputs a node exposes (drives pin count + link routing).
   int InputCountFor(const GraphNode& gn)
   {
      if (dynamic_cast<LayerStackNode*>(gn.node.get()) != nullptr)
         return LayerStackNode::kSlots;
      if (dynamic_cast<SwitcherNode*>(gn.node.get()) != nullptr)
         return SwitcherNode::kSlots;
      // Math takes two modulator cables rather than images, but they are still
      // ordinary input pins as far as the editor is concerned.
      if (dynamic_cast<MathNode*>(gn.node.get()) != nullptr)
         return 2;
      if (dynamic_cast<BlendNode*>(gn.node.get()) != nullptr)
         return 2;
      if (auto* filter = dynamic_cast<FilterNode*>(gn.node.get()))
         return filter->Def().inputs;
      if (dynamic_cast<FitNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ResynthNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<CurvesNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ColorRampNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<RemoveBgNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<DrawNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ImageAnalyzeNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<PaletteNode*>(gn.node.get()) != nullptr)
         return 1; // the reference image, when it comes from the graph
      if (dynamic_cast<AudioAnalyzeNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<GeometryNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ModelSourceNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<Text3DNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<NullNode*>(gn.node.get()) != nullptr)
         return 1; // also covers Viewport, which derives from it
      if (dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MappingNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ImageToPointsNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ClothNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr)
         return JoinGeometryNode::kSlots;
      if (dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr)
         return 1; // an optional point cloud to surface
      if (dynamic_cast<PathNode*>(gn.node.get()) != nullptr)
         return 2; // an optional curve, or geometry to travel around
      if (dynamic_cast<OceanNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr)
         return 1 + kMapCount; // geometry, then one pin per material channel
      if (dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr)
         return Render3DNode::kEnvSlot + 1; // geo, camera, lights, env
      if (dynamic_cast<GeometryOpNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr)
         return 2; // geometry, then the displacement texture
      if (dynamic_cast<InstanceOnPointsNode*>(gn.node.get()) != nullptr)
         return 3; // points, shape, cloud
      if (dynamic_cast<WrapNode*>(gn.node.get()) != nullptr)
         return 2; // source, target
      if (dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr)
         return Switcher3DNode::kSlots;
      if (dynamic_cast<FeedbackNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<TrailsNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ReactionDiffusionNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr)
         return 2; // slot 0 is the image, slot 1 an optional Audio File for recording
      return 0; // sources and modulators have no image inputs
   }

   ImageCable* CableFor(GraphNode& gn, int slot)
   {
      // Every other Render3D input (geometry/camera/light) is a raw pointer
      // handled by ConnectGeometrySlot; only the env slot is an ImageCable.
      if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         return (slot == Render3DNode::kEnvSlot) ? &render->envInput : nullptr;
      if (auto* stack = dynamic_cast<LayerStackNode*>(gn.node.get()))
         return (slot >= 0 && slot < LayerStackNode::kSlots) ? &stack->Input(slot) : nullptr;
      if (auto* sw = dynamic_cast<SwitcherNode*>(gn.node.get()))
         return (slot >= 0 && slot < SwitcherNode::kSlots) ? &sw->Input(slot) : nullptr;
      if (auto* blend = dynamic_cast<BlendNode*>(gn.node.get()))
         return slot == 0 ? &blend->InputA() : &blend->InputB();
      if (auto* filter = dynamic_cast<FilterNode*>(gn.node.get()))
      {
         if (slot == 0)
            return &filter->Input();
         return (slot == 1 && filter->Def().inputs > 1) ? &filter->Input2() : nullptr;
      }
      if (auto* fit = dynamic_cast<FitNode*>(gn.node.get()))
         return slot == 0 ? &fit->Input() : nullptr;
      if (auto* resynth = dynamic_cast<ResynthNode*>(gn.node.get()))
         return slot == 0 ? &resynth->Input() : nullptr;
      if (auto* i2p = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
         return slot == 0 ? &i2p->Input() : nullptr;
      if (auto* curves = dynamic_cast<CurvesNode*>(gn.node.get()))
         return slot == 0 ? &curves->Input() : nullptr;
      if (auto* cramp = dynamic_cast<ColorRampNode*>(gn.node.get()))
         return slot == 0 ? &cramp->Input() : nullptr;
      if (auto* rbg = dynamic_cast<RemoveBgNode*>(gn.node.get()))
         return slot == 0 ? &rbg->Input() : nullptr;
      if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
         return slot == 0 ? &draw->Input() : nullptr;
      if (auto* an = dynamic_cast<ImageAnalyzeNode*>(gn.node.get()))
         return slot == 0 ? &an->Input() : nullptr;
      if (auto* pal = dynamic_cast<PaletteNode*>(gn.node.get()))
         return slot == 0 ? &pal->Input() : nullptr;
      if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
         return slot == 0 ? &model->TextureInput() : nullptr;
      if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
         return slot == 0 ? &t3d->TextureInput() : nullptr;
      if (auto* nul = dynamic_cast<NullNode*>(gn.node.get()))
         return slot == 0 ? &nul->Input() : nullptr;
      if (auto* ocean = dynamic_cast<OceanNode*>(gn.node.get()))
         return slot == 0 ? &ocean->TextureInput() : nullptr;
      // Slot 0 is a geometry pin, wired by pointer, not an image cable; the
      // rest are the material channels in MaterialMap order.
      if (auto* mat = dynamic_cast<MaterialNode*>(gn.node.get()))
         return (slot >= 1 && slot <= kMapCount) ? &mat->MapInput(slot - 1) : nullptr;
      // Slot 0 is the geometry pin, wired by pointer in ConnectGeometrySlot;
      // slot 1 is the height/vector displacement texture.
      if (auto* disp = dynamic_cast<DisplacementNode*>(gn.node.get()))
         return slot == 1 ? &disp->TextureInput() : nullptr;
      if (auto* geo = dynamic_cast<GeometryNode*>(gn.node.get()))
         return slot == 0 ? &geo->TextureInput() : nullptr;
      if (auto* fb = dynamic_cast<FeedbackNode*>(gn.node.get()))
         return slot == 0 ? &fb->Input() : nullptr;
      if (auto* trails = dynamic_cast<TrailsNode*>(gn.node.get()))
         return slot == 0 ? &trails->Input() : nullptr;
      if (auto* rd = dynamic_cast<ReactionDiffusionNode*>(gn.node.get()))
         return slot == 0 ? &rd->Input() : nullptr;
      if (auto* out = dynamic_cast<OutputNode*>(gn.node.get()))
         return slot == 0 ? &out->Input() : nullptr;
      return nullptr;
   }

   // Which geometry-ish pin a node exposes at a given slot, and how to set it.
   // Geometry, camera and light connections are raw pointers rather than
   // ImageCables, so saving and loading them needs this one place that knows
   // the mapping - the same knowledge the link-drawing and connect paths use.
   void ConnectGeometrySlot(GraphNode& dst, int slot, GraphNode& src)
   {
      auto* geo = dynamic_cast<IGeometrySource*>(src.node.get());
      auto* cloud = dynamic_cast<IPointCloudSource*>(src.node.get());
      auto* cam = dynamic_cast<CameraNode*>(src.node.get());
      auto* light = dynamic_cast<LightNode*>(src.node.get());

      if (auto* render = dynamic_cast<Render3DNode*>(dst.node.get()))
      {
         if (slot < Render3DNode::kSlots)
         {
            render->geometry[slot] = geo;
            render->clouds[slot] = cloud;
         }
         else if (slot == Render3DNode::kSlots)
            render->camera = cam;
         else if (slot - Render3DNode::kSlots - 1 < Render3DNode::kLightSlots)
            render->lights[slot - Render3DNode::kSlots - 1] = light;
         return;
      }
      if (auto* op = dynamic_cast<GeometryOpNode*>(dst.node.get())) { op->input = geo; return; }
      if (auto* disp = dynamic_cast<DisplacementNode*>(dst.node.get())) { if (slot == 0) disp->input = geo; return; }
      if (auto* n3d = dynamic_cast<Null3DNode*>(dst.node.get())) { n3d->input = geo; return; }
      if (auto* mapn = dynamic_cast<MappingNode*>(dst.node.get())) { mapn->input = geo; return; }
      if (auto* mat = dynamic_cast<MaterialNode*>(dst.node.get())) { mat->input = geo; return; }
      if (auto* m2p = dynamic_cast<MeshToPointsNode*>(dst.node.get())) { m2p->input = geo; return; }
      if (auto* mrs = dynamic_cast<MeshResynthNode*>(dst.node.get())) { mrs->input = geo; return; }
      if (auto* cloth = dynamic_cast<ClothNode*>(dst.node.get())) { cloth->input = geo; return; }
      if (auto* join = dynamic_cast<JoinGeometryNode*>(dst.node.get()))
      {
         if (slot >= 0 && slot < JoinGeometryNode::kSlots)
            join->inputs[slot] = geo;
         return;
      }
      if (auto* meta = dynamic_cast<MetaBallNode*>(dst.node.get()))
      {
         meta->cloudSource = dynamic_cast<IPointCloudSource*>(src.node.get());
         return;
      }
      if (auto* path = dynamic_cast<PathNode*>(dst.node.get()))
      {
         if (slot == 0)
            path->curveSource = dynamic_cast<ICurveSource*>(src.node.get());
         else
            path->geometrySource = geo;
         return;
      }
      if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(dst.node.get()))
      {
         if (slot == 0)
            inst->pointSource = geo;
         else if (slot == 1)
            inst->instanceShape = geo;
         else
            inst->cloudSource = dynamic_cast<IPointCloudSource*>(src.node.get());
         return;
      }
      if (auto* wrap = dynamic_cast<WrapNode*>(dst.node.get()))
      {
         if (slot == 0) wrap->sourceInput = geo;
         else if (slot == 1) wrap->targetInput = geo;
         return;
      }
      if (auto* sw3 = dynamic_cast<Switcher3DNode*>(dst.node.get()))
      {
         if (slot >= 0 && slot < Switcher3DNode::kSlots)
            sw3->inputs[slot] = geo;
         return;
      }
      if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(dst.node.get()))
      {
         audio->fileSource = dynamic_cast<AudioFileNode*>(src.node.get());
         return;
      }
      if (auto* out = dynamic_cast<OutputNode*>(dst.node.get()))
      {
         if (slot == 1)
            out->audioSource = dynamic_cast<AudioFileNode*>(src.node.get());
         return;
      }
      if (auto* math = dynamic_cast<MathNode*>(dst.node.get()))
      {
         IModulator* mod = ModulatorForOutput(src.node.get(), 0);
         if (slot == 0)
            math->inputA = mod;
         else
            math->inputB = mod;
      }
   }

   // Finds a spawn position near `center` that doesn't land on top of any
   // existing node. Tries the exact center first (the common case: an empty
   // canvas, or a center that has scrolled clear), then walks outward ring by
   // ring over a grid of node-sized slots until it finds one whose padded box
   // clears every current node - so the first free slot returned is always
   // the one closest to where the user was actually looking.
   //
   // Reads node boxes via ed::GetNodePosition/GetNodeSize, which need a
   // current editor context; this runs from the node panel, after that
   // frame's ed::End() has already cleared it (see ed::SetCurrentEditor(nullptr)
   // at the bottom of the graph draw), so the context is set/restored
   // explicitly here rather than relying on caller state - same pattern as
   // ApplyTheme's ed::GetStyle() call above.
   ImVec2 FindFreeSpawnPosition(const ImVec2& center)
   {
      const float kFootprintW = 260.0f;
      const float kFootprintH = 300.0f;
      const float kMargin = 24.0f;

      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);

      std::vector<std::pair<ImVec2, ImVec2>> occupied;
      occupied.reserve(gNodes.size());
      for (GraphNode& gn : gNodes)
      {
         ImVec2 p = ed::GetNodePosition(gn.NodeId());
         ImVec2 s = ed::GetNodeSize(gn.NodeId());
         if (s.x <= 0.0f || s.y <= 0.0f)
            continue; // never laid out (e.g. this frame's spawn) - nothing to avoid yet
         occupied.emplace_back(ImVec2(p.x - kMargin, p.y - kMargin),
                                ImVec2(p.x + s.x + kMargin, p.y + s.y + kMargin));
      }

      ed::SetCurrentEditor(prevEditor);

      auto overlapsAny = [&](const ImVec2& candMin, const ImVec2& candMax)
      {
         for (const auto& box : occupied)
         {
            if (candMax.x > box.first.x && candMin.x < box.second.x &&
                candMax.y > box.first.y && candMin.y < box.second.y)
               return true;
         }
         return false;
      };

      for (int ring = 0; ring < 12; ++ring)
      {
         if (ring == 0)
         {
            if (!overlapsAny(center, ImVec2(center.x + kFootprintW, center.y + kFootprintH)))
               return center;
            continue;
         }
         for (int gx = -ring; gx <= ring; ++gx)
         {
            for (int gy = -ring; gy <= ring; ++gy)
            {
               // Only the new outer ring of the square - the interior was
               // already tried by earlier, smaller rings.
               if (std::abs(gx) != ring && std::abs(gy) != ring)
                  continue;
               ImVec2 cand(center.x + gx * (kFootprintW + kMargin),
                           center.y + gy * (kFootprintH + kMargin));
               ImVec2 candMax(cand.x + kFootprintW, cand.y + kFootprintH);
               if (!overlapsAny(cand, candMax))
                  return cand;
            }
         }
      }
      return center; // exhausted the search area - stack rather than fail
   }

   GraphNode* SpawnNode(const std::string& typeName, const std::string& category,
                        float x = 0.0f, float y = 0.0f)
   {
      INode* node = NodeFactory::Instance().MakeNode(typeName);
      if (node == nullptr)
         return nullptr;

      PushUndoCheckpoint();

      GraphNode gn;
      gn.node.reset(node);
      gn.typeName = typeName;
      gn.category = category;
      gn.index = gNextIndex++;
      gn.spawnX = x;
      gn.spawnY = y;
      gNodes.push_back(std::move(gn));
      return &gNodes.back();
   }

   // File-backed and compiled-from-text nodes keep derived state (a loaded
   // texture, a compiled GL program) that VisitParams deliberately does not
   // touch - it declares settings, not the runtime side effects of restoring
   // them. Both patch load and copy/paste restore a node from bare settings,
   // so both call this afterwards rather than duplicating the same dynamic
   // casts in two places.
   void ReloadDerivedState(INode* node)
   {
      if (auto* img = dynamic_cast<ImageSourceNode*>(node))
         img->ReloadFromPath();
      if (auto* env = dynamic_cast<EnvironmentNode*>(node))
         env->ReloadFromPath();
      if (auto* model = dynamic_cast<ModelSourceNode*>(node))
         model->ReloadFromPath();
      if (auto* audio = dynamic_cast<AudioFileNode*>(node))
         audio->ReloadFromPath();
      if (auto* video = dynamic_cast<VideoSourceNode*>(node))
         video->ReloadFromPath();
      if (auto* palette = dynamic_cast<PaletteNode*>(node))
         palette->ReloadFromPath();
      if (auto* formula = dynamic_cast<FormulaNode*>(node))
         formula->Apply();
   }

   // Copies every parameter VisitParams declares, for any node type, by
   // routing through the same save/load format used for patch files: write src
   // to an in-memory param list, then read it back into dst. This used to be a
   // hand-maintained field-by-field switch covering 7 of roughly 50 node types;
   // every type it didn't cover pasted a node whose values silently stayed at
   // spawn defaults. Now paste and patch load share one source of truth, so a
   // node that round-trips through Save also round-trips through Copy.
   void CopyParams(INode* dstNode, INode* srcNode)
   {
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(srcNode, params);
      Patch::LoadParams(dstNode, params);
      ReloadDerivedState(dstNode);
   }

   // ---------------- per-node parameter UI ----------------

   void DrawImageSourceParams(ImageSourceNode* n)
   {
      if (ImGui::Button("Choose image...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         // wrap pos is window-relative; passing a bare width put it left of the
         // cursor and wrapped every single character onto its own line
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s  (%dx%d)", file.c_str(), n->GetOutputWidth(), n->GetOutputHeight());
         ImGui::PopTextWrapPos();
      }
   }

   void DrawEnvironmentParams(EnvironmentNode* n)
   {
      if (ImGui::Button("Choose HDRI...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s  (%dx%d)", file.c_str(), n->GetOutputWidth(), n->GetOutputHeight());
         ImGui::PopTextWrapPos();
      }
      else
      {
         ImGui::TextDisabled("no image - patch into Render 3D's env pin anyway");
         ImGui::TextDisabled("to use its procedural sky instead");
      }

      ModSlider("intensity", &n->intensity, 0.0f, 8.0f);
      ModSlider("rotation", &n->rotation, -180.0f, 180.0f, "%.1f\xC2\xB0");
   }

   void DrawShapeParams(ShapeNode* n)
   {
      DropdownButton("shape", ShapeNode::ShapeNames(), n->shapeType,
                     [n](int i) { n->shapeType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("size", &n->size, 0.01f, 0.5f);
      ModSlider("aspect", &n->aspect, 0.2f, 3.0f);
      ModSlider("corner/thick", &n->cornerRadius, 0.0f, 0.3f);
      ModSliderInt("sides", &n->sides, 3, 20);
      ModSlider("inner ratio", &n->innerRatio, 0.05f, 1.0f);
      ModSlider("rotation", &n->rotation, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("pos x", &n->posX, 0.0f, 1.0f);
      ModSlider("pos y", &n->posY, 0.0f, 1.0f);
      ColorSwatch("fill", n->fillColor, n);
      ModSlider("fill opacity", &n->fillOpacity, 0.0f, 1.0f);
      ModSlider("stroke width", &n->strokeWidth, 0.0f, 0.1f);
      ColorSwatch("stroke", n->strokeColor, n);
      ModSlider("feather", &n->feather, 0.0f, 0.1f);
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);
   }

   void DrawFormulaParams(FormulaNode* n)
   {
      // The GLSL editor lives in its own window, not inline: ImGui multi-line
      // fields are child windows, and child windows inside the node canvas get
      // clipped away - which is why the box used to render empty.
      DropdownButton("preset", FormulaNode::PresetNames(), n->presetIndex,
                     [n](int i) { n->presetIndex = i; n->LoadPreset(i); });

      if (ImGui::Button("Edit GLSL...", ImVec2(kPreviewSize, 0)))
      {
         gFormulaEditor = n;
         gFormulaEditorOpen = true;
      }

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }

      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("uA", &n->knobA, 0.0f, 1.0f);
      ModSlider("uB", &n->knobB, 0.0f, 1.0f);
      ModSlider("uC", &n->knobC, 0.0f, 1.0f);
      ModSlider("uD", &n->knobD, 0.0f, 1.0f);
      ImGui::Checkbox("animate", &n->animate);
   }

   void DrawTextParams(TextNode* n)
   {
      char buf[512];
      snprintf(buf, sizeof(buf), "%s", n->text.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (ImGui::InputText("text", buf, sizeof(buf)))
         n->text = buf;

      const std::vector<std::string>& fonts = TextNode::AvailableFonts();
      if (n->fontName.empty())
         n->fontName = fonts.front();
      int fontIdx = 0;
      for (int i = 0; i < (int)fonts.size(); i++)
      {
         if (fonts[i] == n->fontName)
            fontIdx = i;
      }
      DropdownButton("font", fonts, fontIdx, [n, &fonts](int i) { n->fontName = fonts[i]; });

      ModSlider("size", &n->fontSize, 8.0f, 300.0f);
      ColorSwatch("color", n->color, n);
      ModSlider("tracking", &n->tracking, -10.0f, 40.0f);
      ModSlider("pos x", &n->posX, 0.0f, 1.0f);
      ModSlider("pos y", &n->posY, 0.0f, 1.0f);
      DropdownButton("align", AlignOptions(), n->align, [n](int i) { n->align = i; });
      ModSlider("scale x", &n->scaleX, 0.1f, 4.0f);
      ModSlider("scale y", &n->scaleY, 0.1f, 4.0f);
      ImGui::Checkbox("word wrap", &n->wordWrap);
      if (n->wordWrap)
      {
         ModSlider("box width", &n->wrapWidth, 0.1f, 1.0f);
         ModSlider("box height", &n->wrapHeight, 0.1f, 1.0f);
         ImGui::Checkbox("fit text to box", &n->fitToBox);
         if (n->fitToBox)
            ImGui::TextDisabled("size is a maximum - fitted to %.0f pt", n->FittedSize());
         ModSlider("line spacing", &n->lineSpacing, 0.5f, 2.5f);
      }
      ModSlider("outline", &n->outlineWidth, 0.0f, 20.0f);
      if (n->outlineWidth > 0.0f)
      {
         ColorSwatch("outline colour", n->outlineColor, n);
         ImGui::Checkbox("outline only", &n->outlineOnly);
      }
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
   }

   void DrawVideoParams(VideoSourceNode* n)
   {
      if (ImGui::Button("Choose video...", ImVec2(kPreviewSize, 0)))
         n->OpenViaDialog();

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         ImGui::PopTextWrapPos();
         ImGui::TextDisabled("%dx%d  %.1fs / %.1fs", n->GetOutputWidth(), n->GetOutputHeight(),
                             n->Position(), n->Duration());
      }
      ImGui::Checkbox("loop", &n->loop);
      ModSlider("speed", &n->speed, -2.0f, 4.0f);
   }

   void DrawFitParams(FitNode* n)
   {
      DropdownButton("mode", FitNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ImGui::Checkbox("match input res", &n->matchInput);
      if (!n->matchInput)
      {
         ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
         ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      }
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);
   }

   void DrawLFOParams(LFONode* n)
   {
      DropdownButton("shape", LFONode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      ModSlider("rate (beats)", &n->rateBeats, 0.05f, 32.0f);
      ModSlider("phase", &n->phase, 0.0f, 1.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }

   void DrawRandomParams(RandomNode* n)
   {
      ModSlider("rate (beats)", &n->rateBeats, 0.05f, 32.0f);
      ModSlider("smooth", &n->smooth, 0.0f, 1.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
      ModSlider("seed", &n->seed, 0.0f, 200.0f);
   }

   void DrawPatternParams(PatternNode* n)
   {
      ImGui::TextDisabled("8 steps, looped:");
      for (int i = 0; i < PatternNode::kSteps; i++)
      {
         char label[16];
         snprintf(label, sizeof(label), "%d", i + 1);
         const bool active = (i == n->CurrentStep()) && i < n->length;
         if (active)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.32f, 0.24f, 0.08f, 1.0f));
         ModSlider(label, &n->steps[i], 0.0f, 1.0f);
         if (active)
            ImGui::PopStyleColor();
      }
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("length", &n->length, 1, PatternNode::kSteps);
      ModSlider("beats / step", &n->stepBeats, 0.05f, 8.0f);
      ImGui::Checkbox("glide between steps", &n->smoothSteps);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }

   void DrawMathParams(MathNode* n)
   {
      DropdownButton("operation", MathNode::OpNames(), n->op, [n](int i) { n->op = i; });
      if (n->inputA == nullptr)
         ModSlider("A (no cable)", &n->constantA, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("A: patched");
      if (n->inputB == nullptr)
         ModSlider("B (no cable)", &n->constantB, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("B: patched");
      ModSlider("gain", &n->gain, -4.0f, 4.0f);
      ModSlider("offset", &n->offset, -1.0f, 1.0f);
      ImGui::Checkbox("clamp to 0..1", &n->clampOutput);
   }

   void DrawNoiseParams(NoiseNode* n)
   {
      DropdownButton("type", NoiseNode::TypeNames(), n->noiseType, [n](int i) { n->noiseType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("scale", &n->scale, 0.5f, 60.0f);
      ModSlider("octaves", &n->octaves, 1.0f, 8.0f, "%.0f");
      ModSlider("lacunarity", &n->lacunarity, 1.0f, 4.0f);
      ModSlider("gain", &n->gain, 0.1f, 0.9f);
      ModSlider("warp", &n->warp, 0.0f, 2.0f);
      ModSlider("speed", &n->speed, -2.0f, 2.0f);
      ModSlider("contrast", &n->contrast, 0.1f, 4.0f);
      ModSlider("brightness", &n->brightness, -0.5f, 0.5f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);
      ImGui::Checkbox("rgb noise", &n->colorNoise);
      if (!n->colorNoise)
      {
         ColorSwatch("low", n->lowColor, n);
         ColorSwatch("high", n->highColor, n);
      }
   }

   void DrawTextureParams(TextureNode* n)
   {
      DropdownButton("type", TextureNode::TypeNames(), n->textureType, [n](int i) { n->textureType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("scale", &n->scale, 0.5f, 60.0f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);

      if (n->textureType == 0) // Voronoi
      {
         DropdownButton("distance", TextureNode::VoronoiDistanceNames(), n->voronoiDistance, [n](int i) { n->voronoiDistance = i; });
         DropdownButton("feature", TextureNode::VoronoiFeatureNames(), n->voronoiFeature, [n](int i) { n->voronoiFeature = i; });
         if (n->voronoiDistance == 3)
            ModSlider("minkowski exponent", &n->voronoiMinkowskiExponent, 0.1f, 8.0f);
         if (n->voronoiFeature == 2)
            ModSlider("smoothness", &n->voronoiSmoothness, 0.001f, 1.0f);
         ModSlider("randomness", &n->voronoiRandomness, 0.0f, 1.0f);
         ImGui::Checkbox("cell color", &n->voronoiCellColor);
      }
      else if (n->textureType == 1) // Brick
      {
         ModSlider("brick width", &n->brickWidth, 0.05f, 2.0f);
         ModSlider("row height", &n->brickHeight, 0.05f, 2.0f);
         ModSlider("row offset", &n->brickRowOffset, 0.0f, 1.0f);
         ModSlider("mortar size", &n->brickMortarSize, 0.0f, 0.2f);
         ModSlider("mortar smooth", &n->brickMortarSmooth, 0.0f, 1.0f);
         ModSlider("bias", &n->brickBias, -0.5f, 0.5f);
         ColorSwatch("mortar", n->mortarColor, n);
      }
      else if (n->textureType == 2) // Magic
      {
         ModSlider("depth", &n->magicDepth, 1.0f, 10.0f, "%.0f");
         ModSlider("distortion", &n->magicDistortion, 0.0f, 4.0f);
      }
      else if (n->textureType == 3) // Wave
      {
         DropdownButton("wave type", TextureNode::WaveTypeNames(), n->waveType, [n](int i) { n->waveType = i; });
         if (n->waveType == 0)
            DropdownButton("bands direction", TextureNode::WaveBandsDirectionNames(), n->waveBandsDirection, [n](int i) { n->waveBandsDirection = i; });
         DropdownButton("profile", TextureNode::WaveProfileNames(), n->waveProfile, [n](int i) { n->waveProfile = i; });
         ModSlider("distortion", &n->waveDistortion, 0.0f, 4.0f);
         ModSlider("detail", &n->waveDetail, 1.0f, 8.0f, "%.0f");
         ModSlider("detail scale", &n->waveDetailScale, 0.1f, 8.0f);
         ModSlider("phase offset", &n->wavePhaseOffset, 0.0f, 1.0f);
      }
      else if (n->textureType == 4) // Musgrave
      {
         DropdownButton("musgrave type", TextureNode::MusgraveTypeNames(), n->musgraveType, [n](int i) { n->musgraveType = i; });
         ModSlider("dimension", &n->musgraveDimension, 0.0f, 4.0f);
         ModSlider("lacunarity", &n->musgraveLacunarity, 1.001f, 6.0f);
         ModSlider("octaves", &n->musgraveOctaves, 1.0f, 8.0f, "%.0f");
         if (n->musgraveType == 3)
            ModSlider("gain", &n->musgraveGain, 0.0f, 4.0f);
         if (n->musgraveType >= 2)
            ModSlider("offset", &n->musgraveOffset, 0.0f, 4.0f);
      }
      else if (n->textureType == 5) // Checker
      {
         // No extra params - scale/seed above already control cell size.
      }
      else if (n->textureType == 6) // Gradient
      {
         DropdownButton("gradient type", TextureNode::GradientTypeNames(), n->gradientType, [n](int i) { n->gradientType = i; });
      }
      else if (n->textureType == 7) // Clouds
      {
         ModSlider("depth", &n->cloudsDepth, 1.0f, 8.0f, "%.0f");
         ImGui::Checkbox("hard", &n->cloudsHard);
      }
      else if (n->textureType == 8) // Marble
      {
         DropdownButton("marble type", TextureNode::MarbleTypeNames(), n->marbleType, [n](int i) { n->marbleType = i; });
         ModSlider("turbulence", &n->marbleTurbulence, 0.0f, 20.0f);
         ModSlider("noise scale", &n->marbleNoiseScale, 0.1f, 8.0f);
         ModSlider("noise depth", &n->marbleNoiseDepth, 1.0f, 8.0f, "%.0f");
      }
      else // Wood
      {
         DropdownButton("wood type", TextureNode::WoodTypeNames(), n->woodType, [n](int i) { n->woodType = i; });
         ModSlider("turbulence", &n->woodTurbulence, 0.0f, 20.0f);
         ModSlider("noise scale", &n->woodNoiseScale, 0.1f, 8.0f);
      }

      ModSlider("contrast", &n->contrast, 0.1f, 4.0f);
      ModSlider("brightness", &n->brightness, -0.5f, 0.5f);
      if (n->textureType != 1 && !(n->textureType == 0 && n->voronoiCellColor) && n->textureType != 2)
      {
         ColorSwatch("low", n->lowColor, n);
         ColorSwatch("high", n->highColor, n);
      }
   }

   void DrawSwitcherParams(SwitcherNode* n)
   {
      DropdownButton("unit", SwitcherNode::UnitNames(), n->unit, [n](int i) { n->unit = i; });
      ModSlider("every", &n->interval, 0.05f, 32.0f);
      ModSlider("crossfade", &n->crossfade, 0.0f, 0.99f);
      ImGui::Checkbox("manual", &n->manual);
      if (n->manual)
      {
         ImGui::SetNextItemWidth(kParamWidth);
         ImGui::SliderInt("slot", &n->manualSlot, 0, SwitcherNode::kSlots - 1);
      }
      else
      {
         ImGui::TextDisabled("showing input %c", 'A' + n->ActiveSlot());
      }
   }

   // 2D control surface. Dragging the orb sweeps the mutation weights; the
   // recorded path is drawn behind it so a loop is visible while it plays.
   void DrawFxPad(ResynthNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##fxpad", ImVec2(size, size));
      const bool active = ImGui::IsItemActive();

      if (active)
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      dl->AddRectFilled(origin, br, IM_COL32(16, 16, 22, 255), 4.0f);

      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y),
                     IM_COL32(48, 50, 62, 255));
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f),
                     IM_COL32(48, 50, 62, 255));
      }

      const std::vector<ResynthNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, IM_COL32(120, 200, 255, 170), 1.4f);
      }

      // Corner labels: the pad blends between these four named effects, so it is
      // obvious what is being swept rather than four anonymous weights.
      const ImU32 labelCol = IM_COL32(150, 156, 180, 255);
      const char* bl = n->CornerLabel(0);
      const char* brName = n->CornerLabel(1);
      const char* tl = n->CornerLabel(2);
      const char* trName = n->CornerLabel(3);
      dl->AddText(ImVec2(origin.x + 5, origin.y + 4), labelCol, tl);
      ImVec2 trSize = ImGui::CalcTextSize(trName);
      dl->AddText(ImVec2(br.x - trSize.x - 5, origin.y + 4), labelCol, trName);
      ImVec2 blSize = ImGui::CalcTextSize(bl);
      dl->AddText(ImVec2(origin.x + 5, br.y - blSize.y - 4), labelCol, bl);
      ImVec2 brSize = ImGui::CalcTextSize(brName);
      dl->AddText(ImVec2(br.x - brSize.x - 5, br.y - brSize.y - 4), labelCol, brName);

      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? IM_COL32(255, 90, 90, 255)
                     : n->IsPlayingPath()   ? IM_COL32(120, 235, 150, 255)
                                            : IM_COL32(255, 190, 90, 255);
      dl->AddCircleFilled(orb, 9.0f, orbColor);
      dl->AddCircle(orb, 9.0f, IM_COL32(20, 20, 28, 255), 0, 2.0f);
      dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
   }

   void DrawResynthParams(ResynthNode* n)
   {
      DrawFxPad(n);

      if (n->IsRecordingPath())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
         if (ImGui::Button("Stop rec", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopRecording();
         ImGui::PopStyleColor();
      }
      else
      {
         if (ImGui::Button("Rec path", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingPath())
      {
         if (ImGui::Button("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPath();
      }
      else
      {
         if (ImGui::Button("Play path", ImVec2(kPreviewSize * 0.48f, 0)))
            n->PlayPath();
      }
      ImGui::Checkbox("loop path", &n->loopPath);
      ImGui::SameLine();
      if (ImGui::SmallButton("clear"))
         n->ClearPath();

      NodeSeparator();
      DropdownButton("mode", ResynthNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ModSlider("chaos", &n->chaos, 0.0f, 1.0f);
      ModSlider("mutation", &n->mutation, 0.0f, 1.0f);
      ModSlider("feedback", &n->feedback, 0.0f, 1.0f);
      ModSlider("source pull", &n->sourcePull, 0.0f, 1.0f);

      NodeSeparator();
      ImGui::TextDisabled("generation %d", n->Generation());
      if (ImGui::Button("Iterate", ImVec2(kPreviewSize * 0.48f, 0)))
         n->StepOnce();
      ImGui::SameLine();
      if (ImGui::Button("Reset", ImVec2(kPreviewSize * 0.48f, 0)))
         n->Reset();
      if (ImGui::Button("Randomise FX", ImVec2(kPreviewSize, 0)))
         n->Randomise();

      if (ImGui::TreeNode("pad corners"))
      {
         static const char* kCornerNames[4] = { "bottom left", "bottom right", "top left", "top right" };
         for (int c = 0; c < ResynthNode::kCorners; c++)
         {
            ImGui::PushID(c);
            char label[32];
            snprintf(label, sizeof(label), "%s##c%d", kCornerNames[c], c);
            DropdownButton(label, ResynthNode::EffectNames(), n->cornerEffect[c],
                           [n, c](int i) { n->cornerEffect[c] = i; });
            ModSlider("amount", &n->cornerAmount[c], 0.0f, 1.0f);
            ImGui::PopID();
         }
         ImGui::TreePop();
      }
      ImGui::Checkbox("auto iterate", &n->autoIterate);
      if (n->autoIterate)
         ModSlider("steps / beat", &n->stepsPerBeat, 0.05f, 16.0f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);
   }

   void DrawMacroKnobParams(MacroKnobNode* n)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "%s", n->label.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (ImGui::InputText("name", buf, sizeof(buf)))
         n->label = buf;

      ImGui::SetNextItemWidth(kPreviewSize);
      ImGui::SliderFloat("##macro", &n->value, 0.0f, 1.0f, "%.3f");
      ModSlider("curve", &n->curve, 0.2f, 4.0f);
      ImGui::Checkbox("invert", &n->invert);
   }

   void DrawMacroXYParams(MacroXYNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##macroxy", ImVec2(size, size));
      if (ImGui::IsItemActive())
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      dl->AddRectFilled(origin, br, IM_COL32(16, 16, 22, 255), 4.0f);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y), IM_COL32(48, 50, 62, 255));
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f), IM_COL32(48, 50, 62, 255));
      }
      const std::vector<MacroXYNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, IM_COL32(120, 200, 255, 170), 1.4f);
      }
      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? IM_COL32(255, 90, 90, 255)
                     : n->IsPlayingPath()   ? IM_COL32(120, 235, 150, 255)
                                            : IM_COL32(255, 190, 90, 255);
      dl->AddCircleFilled(orb, 9.0f, orbColor);
      dl->AddCircle(orb, 9.0f, IM_COL32(20, 20, 28, 255), 0, 2.0f);
      dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);

      ImGui::TextDisabled("x %.3f   y %.3f", n->padX, n->padY);

      if (n->IsRecordingPath())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
         if (ImGui::Button("Stop rec", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopRecording();
         ImGui::PopStyleColor();
      }
      else if (ImGui::Button("Rec path", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingPath())
      {
         if (ImGui::Button("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPath();
      }
      else if (ImGui::Button("Play path", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->PlayPath();
      }
      ImGui::Checkbox("loop", &n->loopPath);
      ImGui::SameLine();
      if (ImGui::SmallButton("clear"))
         n->ClearPath();
      ModSlider("speed", &n->speed, 0.05f, 4.0f);
   }

   // Interactive tone curve. Drag points, click empty space to add one,
   // right-click a point to remove it.
   void DrawCurveEditor(CurvesNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##curve", ImVec2(size, size));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      auto toScreen = [&](float x, float y) {
         return ImVec2(origin.x + x * size, origin.y + (1.0f - y) * size);
      };
      auto toCurve = [&](ImVec2 p) {
         return ImVec2(std::min(1.0f, std::max(0.0f, (p.x - origin.x) / size)),
                       std::min(1.0f, std::max(0.0f, 1.0f - (p.y - origin.y) / size)));
      };

      std::vector<CurvesNode::Point>& pts = n->Points(n->activeChannel);

      static int sDragIndex = -1;
      static CurvesNode* sDragNode = nullptr;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      int nearest = -1;
      float nearestDist = 12.0f;
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         float d = std::sqrt((sp.x - mouse.x) * (sp.x - mouse.x) + (sp.y - mouse.y) * (sp.y - mouse.y));
         if (d < nearestDist)
         {
            nearestDist = d;
            nearest = i;
         }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         if (nearest >= 0)
         {
            sDragIndex = nearest;
            sDragNode = n;
         }
         else
         {
            ImVec2 c = toCurve(mouse);
            sDragIndex = n->AddPoint(n->activeChannel, c.x, c.y);
            sDragNode = n;
         }
      }
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && nearest >= 0)
         n->RemovePoint(n->activeChannel, nearest);

      if (active && sDragNode == n && sDragIndex >= 0)
      {
         ImVec2 c = toCurve(mouse);
         n->MovePoint(n->activeChannel, sDragIndex, c.x, c.y);
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragIndex = -1;
         sDragNode = nullptr;
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      dl->AddRectFilled(origin, br, IM_COL32(16, 16, 22, 255), 4.0f);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y), IM_COL32(44, 46, 58, 255));
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f), IM_COL32(44, 46, 58, 255));
      }
      dl->AddLine(origin, ImVec2(br.x, br.y), IM_COL32(58, 60, 74, 255)); // identity reference

      ImU32 lineCol = IM_COL32(230, 235, 250, 255);
      if (n->activeChannel == CurvesNode::kRed)   lineCol = IM_COL32(255, 110, 110, 255);
      if (n->activeChannel == CurvesNode::kGreen) lineCol = IM_COL32(120, 230, 130, 255);
      if (n->activeChannel == CurvesNode::kBlue)  lineCol = IM_COL32(120, 170, 255, 255);

      const int kSegments = 64;
      for (int i = 1; i <= kSegments; i++)
      {
         float x0 = (float)(i - 1) / kSegments;
         float x1 = (float)i / kSegments;
         dl->AddLine(toScreen(x0, n->Evaluate(n->activeChannel, x0)),
                     toScreen(x1, n->Evaluate(n->activeChannel, x1)), lineCol, 1.8f);
      }
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         dl->AddCircleFilled(sp, i == nearest ? 6.0f : 4.5f, lineCol);
         dl->AddCircle(sp, i == nearest ? 6.0f : 4.5f, IM_COL32(18, 18, 26, 255), 0, 1.5f);
      }
      dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
   }

   void DrawCurvesParams(CurvesNode* n)
   {
      DropdownButton("channel", CurvesNode::ChannelNames(), n->activeChannel,
                     [n](int i) { n->activeChannel = i; });
      DrawCurveEditor(n);
      if (ImGui::Button("Reset channel", ImVec2(kPreviewSize, 0)))
         n->ResetChannel(n->activeChannel);
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
   }

   void DrawRemoveBgParams(RemoveBgNode* n)
   {
      DropdownButton("detect", RemoveBgNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      DropdownButton("output", RemoveBgNode::OutputModeNames(), n->outputMode,
                     [n](int i) { n->outputMode = i; });

      if (ImGui::Button("Remove Background", ImVec2(kPreviewSize, 0)))
         n->RequestMask();

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      ModSlider("feather", &n->feather, 0.0f, 4.0f);
      ModSlider("threshold", &n->threshold, 0.0f, 1.0f);
      ModSlider("edge contrast", &n->contrast, 0.5f, 8.0f);
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);

      ImGui::Checkbox("auto refresh (video)", &n->autoRefresh);
      if (n->autoRefresh)
      {
         ModSlider("every beats", &n->refreshBeats, 0.1f, 8.0f);
      }
   }

   void DrawFeedbackParams(FeedbackNode*)
   {
   }

   void DrawTrailsParams(TrailsNode* n)
   {
      static const std::vector<std::string> kBlends = { "Max", "Add", "Screen" };
      DropdownButton("blend", kBlends, n->blendMode, [n](int i) { n->blendMode = i; });
      ModSlider("decay", &n->decay, 0.5f, 0.999f);
      ModSlider("zoom", &n->zoom, 0.9f, 1.1f);
      ModSlider("rotate", &n->rotate, -5.7296f, 5.7296f, "%.1f\xC2\xB0");
      ModSlider("drift x", &n->driftX, -0.02f, 0.02f);
      ModSlider("drift y", &n->driftY, -0.02f, 0.02f);
      ModSlider("hue shift", &n->hueShift, -0.05f, 0.05f);
      if (ImGui::Button("Clear", ImVec2(kPreviewSize, 0)))
         n->Clear();
   }

   void DrawReactionDiffusionParams(ReactionDiffusionNode* n)
   {
      DropdownButton("preset", ReactionDiffusionNode::PresetNames(), n->preset,
                     [n](int i) { n->ApplyPreset(i); });
      ModSlider("feed", &n->feed, 0.01f, 0.09f, "%.4f");
      ModSlider("kill", &n->kill, 0.03f, 0.08f, "%.4f");
      ModSlider("diffuse A", &n->diffuseA, 0.2f, 1.5f);
      ModSlider("diffuse B", &n->diffuseB, 0.1f, 1.0f);
      ModSlider("steps / frame", &n->stepsPerFrame, 1.0f, 32.0f, "%.0f");
      ImGui::TextDisabled("with an input connected:");
      ModSlider("source influence", &n->sourceInfluence, 0.0f, 1.0f);
      ModSlider("width", &n->width, 64.0f, 2048.0f, "%.0f");
      ModSlider("height", &n->height, 64.0f, 2048.0f, "%.0f");
      ColorSwatch("low", n->lowColor, n);
      ColorSwatch("high", n->highColor, n);
      if (ImGui::Button("Reseed", ImVec2(kPreviewSize, 0)))
         n->Reseed();
   }

   // --- Palette from Image ---------------------------------------------
   // Preview is the palette itself, not the strip texture: the strip is a
   // by-product, while the swatches are what the rest of the graph binds to,
   // and they need to be readable and countable at a glance.
   void DrawPalettePreview(PaletteNode* n)
   {
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 118.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                        IM_COL32(18, 18, 24, 255), 4.0f);

      const int count = std::max(1, n->SwatchCount());
      const float pad = 6.0f;
      const float chipW = (kPreviewSize - pad * 2.0f) / (float)count;
      const float chipH = 60.0f;
      for (int i = 0; i < count; i++)
      {
         float rgb[3];
         n->GetSwatch(i, rgb);
         const ImVec2 tl(origin.x + pad + chipW * (float)i, origin.y + pad);
         const ImVec2 br(tl.x + chipW - 2.0f, tl.y + chipH);
         dl->AddRectFilled(tl, br,
                           IM_COL32((int)(rgb[0] * 255.0f), (int)(rgb[1] * 255.0f),
                                    (int)(rgb[2] * 255.0f), 255),
                           3.0f);

         // How much of the reference each swatch actually covers. Without it a
         // one-percent accent looks exactly as important as the colour half the
         // photo is made of, which is the wrong thing to build a look on.
         const float weight = n->SwatchWeight(i);
         dl->AddRectFilled(ImVec2(tl.x, br.y + 4.0f),
                           ImVec2(tl.x + (chipW - 2.0f) * std::min(1.0f, weight), br.y + 7.0f),
                           IM_COL32(150, 156, 176, 255));

         char idx[8];
         snprintf(idx, sizeof(idx), "%d", i + 1);
         dl->AddText(ImVec2(tl.x + 2.0f, br.y + 9.0f), IM_COL32(118, 124, 144, 255), idx);
      }

      const char* status;
      if (n->Input().IsConnected())
         status = n->live ? "live from cable" : "from cable";
      else if (!n->LoadedPath().empty())
         status = "from file";
      else
         status = "choose a photo, or cable one to 'ref'";
      dl->AddText(ImVec2(origin.x + pad, origin.y + h - 18.0f),
                  IM_COL32(126, 132, 152, 255), status);

      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                  IM_COL32(70, 74, 90, 255), 4.0f);
      ImGui::Dummy(ImVec2(kPreviewSize, h));
   }

   void DrawPaletteParams(PaletteNode* n)
   {
      if (ImGui::Button("Choose reference...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         const size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         ImGui::PopTextWrapPos();
      }
      ModSliderInt("swatches", &n->swatchCount, 2, PaletteNode::kMaxSwatches);
      DropdownButton("order", PaletteNode::SortNames(), n->sortMode,
                     [n](int i) { PushUndoCheckpoint(); n->sortMode = i; });

      NodeSeparator("extract");
      ModSlider("min chroma", &n->minChroma, 0.0f, 0.2f);
      ImGui::Checkbox("include neutrals", &n->includeNeutrals);
      ModSlider("seed", &n->seed, 0.0f, 64.0f, "%.1f");
      ModSliderInt("sample", &n->sampleSize, 16, 256);
      ImGui::Checkbox("live", &n->live);
      // Drawn whether or not Live is on: hiding a modulatable slider behind a
      // checkbox renumbers every pin after it, which silently repoints this
      // node's modulation the moment the box is ticked.
      ModSlider("rate", &n->sampleRate, 1.0f, 60.0f, "%.0f");
      if (ImGui::Button("Re-extract", ImVec2(kPreviewSize, 0)))
         n->RequestExtract();

      // Shaping runs on the stored cluster centres, so these are live: they
      // re-grade the palette without re-clustering the photo behind it.
      NodeSeparator("shape");
      ModSlider("hue", &n->hueShift, -0.5f, 0.5f);
      ModSlider("saturation", &n->saturation, 0.0f, 2.0f);
      ModSlider("brightness", &n->brightness, -0.4f, 0.4f);
      ModSlider("spread", &n->spread, 0.0f, 2.0f);

      NodeSeparator("strip output");
      DropdownButton("blend", PaletteNode::StripNames(), n->stripMode,
                     [n](int i) { PushUndoCheckpoint(); n->stripMode = i; });
      ImGui::TextDisabled("out is a gradient of the palette");
   }

   void DrawRampParams(RampNode* n)
   {
      DropdownButton("type", RampNode::TypeNames(), n->type, [n](int i) { n->type = i; });
      DropdownButton("repeat", RampNode::RepeatNames(), n->repeat, [n](int i) { n->repeat = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("angle", &n->angle, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("center x", &n->centerX, -0.5f, 1.5f);
      ModSlider("center y", &n->centerY, -0.5f, 1.5f);
      ModSlider("scale", &n->scale, 0.05f, 8.0f);
      ModSlider("offset", &n->offset, -1.0f, 1.0f);
      ModSlider("gamma", &n->gamma, 0.1f, 4.0f);
      ModSlider("dither", &n->dither, 0.0f, 1.0f);

      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("stops", &n->stopCount, 2, RampNode::kStops);
      for (int i = 0; i < n->stopCount; i++)
      {
         ImGui::PushID(i);
         char label[24];
         snprintf(label, sizeof(label), "stop %d", i + 1);
         ColorSwatch(label, n->stopColor[i], n);
         ModSlider("at", &n->stopPos[i], 0.0f, 1.0f);
         ImGui::PopID();
      }
   }

   // Gradient-tool-style stop editor: drag a marker to reposition, click empty
   // track to add a stop (seeded with the color already showing there), right
   // click a marker to remove it. Position isn't run through ModSlider like
   // RampNode's - dragging on the bar is the primary interaction, and it would
   // otherwise fight the drag for control of the same float every frame.
   void DrawColorRampEditor(ColorRampNode* n)
   {
      const float size = kPreviewSize;
      const float barH = 26.0f;
      const float trackH = 22.0f;
      const float gap = 4.0f;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // gradient preview strip
      const int kSegments = 96;
      for (int i = 0; i < kSegments; i++)
      {
         float t0 = (float)i / kSegments;
         float t1 = (float)(i + 1) / kSegments;
         float c0[3], c1[3];
         n->Evaluate(t0, c0);
         n->Evaluate(t1, c1);
         ImVec2 tl(origin.x + t0 * size, origin.y);
         ImVec2 br(origin.x + t1 * size + 1.0f, origin.y + barH);
         ImU32 col0 = IM_COL32((int)(c0[0] * 255), (int)(c0[1] * 255), (int)(c0[2] * 255), 255);
         ImU32 col1 = IM_COL32((int)(c1[0] * 255), (int)(c1[1] * 255), (int)(c1[2] * 255), 255);
         dl->AddRectFilledMultiColor(tl, br, col0, col1, col1, col0);
      }
      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + barH), IM_COL32(70, 74, 90, 255), 3.0f);

      // stop track
      ImVec2 trackOrigin(origin.x, origin.y + barH + gap);
      ImVec2 trackBr(origin.x + size, trackOrigin.y + trackH);
      dl->AddRectFilled(trackOrigin, trackBr, IM_COL32(16, 16, 22, 255), 3.0f);
      dl->AddRect(trackOrigin, trackBr, IM_COL32(70, 74, 90, 255), 3.0f);

      ImGui::SetCursorScreenPos(trackOrigin);
      ImGui::InvisibleButton("##colorramp", ImVec2(size, trackH));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      auto toScreenX = [&](float x) { return trackOrigin.x + x * size; };
      auto toX = [&](float screenX) {
         return std::min(1.0f, std::max(0.0f, (screenX - trackOrigin.x) / size));
      };

      static ColorRampNode* sDragNode = nullptr;
      static int sDragIndex = -1;
      static ColorRampNode* sSelNode = nullptr;
      static int sSelIndex = -1;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      int nearest = -1;
      float nearestDist = 10.0f;
      for (int i = 0; i < n->stopCount; i++)
      {
         float d = std::fabs(toScreenX(n->stopPos[i]) - mouse.x);
         if (d < nearestDist)
         {
            nearestDist = d;
            nearest = i;
         }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         if (nearest >= 0)
         {
            sDragIndex = nearest;
         }
         else
         {
            float x = toX(mouse.x);
            float seedColor[3];
            n->Evaluate(x, seedColor);
            sDragIndex = n->AddStop(x, seedColor);
         }
         sDragNode = n;
         sSelNode = n;
         sSelIndex = sDragIndex;
      }
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && nearest >= 0)
      {
         PushUndoCheckpoint();
         n->RemoveStop(nearest);
         if (sSelNode == n && sSelIndex == nearest)
            sSelIndex = -1;
      }
      if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && nearest >= 0)
      {
         PushUndoCheckpoint();
         n->RemoveStop(nearest);
         if (sSelNode == n && sSelIndex == nearest)
            sSelIndex = -1;
         sDragIndex = -1;
         sDragNode = nullptr;
      }
      if (active && sDragNode == n && sDragIndex >= 0)
         n->MoveStop(sDragIndex, toX(mouse.x));
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragIndex = -1;
         sDragNode = nullptr;
      }
      if (sSelNode == n && sSelIndex >= n->stopCount)
         sSelIndex = -1;

      for (int i = 0; i < n->stopCount; i++)
      {
         float x = toScreenX(n->stopPos[i]);
         const bool isSel = (sSelNode == n && sSelIndex == i);
         ImU32 fill = IM_COL32((int)(n->stopColor[i][0] * 255), (int)(n->stopColor[i][1] * 255),
                               (int)(n->stopColor[i][2] * 255), 255);
         float r = (i == nearest || isSel) ? 7.0f : 5.5f;
         ImVec2 tip(x, trackOrigin.y + 2.0f);
         dl->AddTriangleFilled(ImVec2(x - r, tip.y + r * 1.6f), ImVec2(x + r, tip.y + r * 1.6f), tip,
                               isSel ? IM_COL32(255, 220, 120, 255) : IM_COL32(220, 224, 236, 255));
         ImVec2 chipTl(x - r * 0.6f, tip.y + r * 1.6f + 1.0f);
         dl->AddRectFilled(chipTl, ImVec2(chipTl.x + r * 1.2f, chipTl.y + 5.0f), fill);
      }

      ImGui::SetCursorScreenPos(ImVec2(origin.x, trackBr.y + gap));

      // One row per stop, left-to-right by position along the ramp - not by
      // raw array index, since RemoveStop's compaction reassigns indices and
      // a raw-index label would relabel unrelated stops out from under the
      // user every time one is deleted.
      int order[ColorRampNode::kMaxStops];
      for (int i = 0; i < n->stopCount; i++)
         order[i] = i;
      for (int i = 0; i < n->stopCount; i++)
         for (int j = i + 1; j < n->stopCount; j++)
            if (n->stopPos[order[j]] < n->stopPos[order[i]])
               std::swap(order[i], order[j]);

      for (int rank = 0; rank < n->stopCount; rank++)
      {
         const int idx = order[rank];
         ImGui::PushID(idx + 20000);
         char label[16];
         snprintf(label, sizeof(label), "stop %d", rank + 1);
         ColorSwatch(label, n->stopColor[idx], n);
         ImGui::SameLine(size - 18.0f);
         ImGui::BeginDisabled(n->stopCount <= 2);
         if (ImGui::SmallButton("x"))
         {
            PushUndoCheckpoint();
            n->RemoveStop(idx);
            if (sSelNode == n && sSelIndex == idx)
               sSelIndex = -1;
         }
         ImGui::EndDisabled();
         ImGui::PopID();
      }
      n->MarkDirty();

      ImGui::BeginDisabled(n->stopCount >= ColorRampNode::kMaxStops);
      if (ImGui::Button("+ stop", ImVec2(size, 0)))
      {
         PushUndoCheckpoint();
         float x = n->stopCount > 0 ? std::min(1.0f, n->stopPos[order[n->stopCount - 1]] + 0.1f) : 0.5f;
         float seedColor[3];
         n->Evaluate(x, seedColor);
         n->AddStop(x, seedColor);
      }
      ImGui::EndDisabled();
   }

   void DrawColorRampParams(ColorRampNode* n)
   {
      DrawColorRampEditor(n);
      DropdownButton("interpolation", ColorRampNode::InterpNames(), n->interpMode,
                     [n](int i) { PushUndoCheckpoint(); n->interpMode = i; n->MarkDirty(); });
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
   }

   void DrawImageAnalyzeParams(ImageAnalyzeNode* n)
   {
      for (int i = 0; i < ImageAnalyzeNode::kOutputCount; i++)
      {
         const float v = n->Value(i);
         ImGui::Text("%-9s", n->OutputLabel(i));
         ImGui::SameLine();
         ImGui::ProgressBar(v, ImVec2(kPreviewSize * 0.55f, 0), "");
      }
      ModSlider("gain", &n->gain, 0.1f, 8.0f);
      ModSlider("smoothing", &n->smoothing, 0.0f, 0.95f);
      ModSlider("samples / sec", &n->sampleRate, 1.0f, 60.0f, "%.0f");
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("sample res", &n->sampleSize, 8, 256);
   }

   void DrawAudioFileParams(AudioFileNode* n)
   {
      if (ImGui::Button("Choose audio...", ImVec2(kPreviewSize, 0)))
         n->OpenViaDialog();

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (!n->FileName().empty())
         ImGui::TextDisabled("%s", n->FileName().c_str());
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      if (n->IsLoaded())
      {
         const double dur = std::max(0.001, n->Duration());
         ImGui::ProgressBar((float)(n->Position() / dur), ImVec2(kPreviewSize, 0));
         ImGui::TextDisabled("%.1f / %.1f s", n->Position(), dur);

         if (n->IsPlaying())
         {
            if (ImGui::Button("Pause", ImVec2(kPreviewSize * 0.48f, 0)))
               n->Pause();
         }
         else if (ImGui::Button("Play", ImVec2(kPreviewSize * 0.48f, 0)))
         {
            n->Play();
         }
         ImGui::SameLine();
         if (ImGui::Button("Restart", ImVec2(kPreviewSize * 0.48f, 0)))
            n->Restart();

         ImGui::Checkbox("follow transport", &n->followTransport);
         ImGui::Checkbox("loop", &n->loop);
         ImGui::Checkbox("audible", &n->monitor);
         ModSlider("volume", &n->volume, 0.0f, 1.0f);
         ModSlider("gain", &n->gain, 0.1f, 16.0f);

         const Platform::AudioLevels& lv = n->Levels();
         ImGui::Text("level "); ImGui::SameLine();
         ImGui::ProgressBar(std::min(1.0f, lv.rms * n->gain), ImVec2(kPreviewSize * 0.6f, 0), "");
      }
   }

   void DrawAudioAnalyzeParams(AudioAnalyzeNode* n)
   {
      if (n->fileSource != nullptr)
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "source: Audio File");
         ImGui::PopTextWrapPos();
      }
      else
      {
         if (Platform::AudioIsRunning())
         {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("Stop listening", ImVec2(kPreviewSize, 0)))
               n->Stop();
            ImGui::PopStyleColor();
         }
         else if (ImGui::Button("Start listening", ImVec2(kPreviewSize, 0)))
         {
            n->Start();
         }
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s", n->Status().c_str());
         ImGui::PopTextWrapPos();
      }

      // spectrum, so it is obvious whether audio is actually arriving
      const Platform::AudioLevels& levels = n->Levels();
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 60.0f;
      ImGui::Dummy(ImVec2(kPreviewSize, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h), IM_COL32(16, 16, 22, 255), 3.0f);
      const float bw = kPreviewSize / (float)Platform::kAudioBands;
      for (int i = 0; i < Platform::kAudioBands; i++)
      {
         const float v = std::min(1.0f, levels.bands[i] * n->gain);
         dl->AddRectFilled(ImVec2(origin.x + i * bw + 1, origin.y + h - v * h),
                           ImVec2(origin.x + (i + 1) * bw - 1, origin.y + h),
                           IM_COL32(120, 200, 255, 235));
      }
      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h), IM_COL32(70, 74, 90, 255), 3.0f);

      for (int i = 0; i < 5; i++)
      {
         ImGui::Text("%-6s", n->OutputLabel(i));
         ImGui::SameLine();
         ImGui::ProgressBar(n->Value(i), ImVec2(kPreviewSize * 0.6f, 0), "");
      }
      ModSlider("gain", &n->gain, 0.1f, 16.0f);
      ModSlider("attack", &n->attack, 0.02f, 1.0f);
      ModSlider("release", &n->release, 0.005f, 1.0f);
      ModSlider("onset hold", &n->onsetHold, 0.02f, 1.0f);
   }

   void DrawPathParams(PathNode* n)
   {
      if (n->IsFollowing())
         ImGui::TextDisabled("following %zu points", n->FollowPointCount());
      else
         DropdownButton("shape", PathNode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      float p[3];
      n->CurrentPoint(p);
      ImGui::TextDisabled("t %.2f   (%.2f, %.2f, %.2f)", n->Progress(), p[0], p[1], p[2]);

      NodeSeparator("motion");
      ModSlider("speed / beat", &n->speed, -2.0f, 2.0f);
      ModSlider("phase", &n->phase, 0.0f, 1.0f);
      ImGui::Checkbox("ping-pong", &n->pingPong);

      if (n->geometrySource != nullptr && n->curveSource == nullptr)
      {
         NodeSeparator("follow");
         DropdownButton("mode", PathNode::FollowModeNames(), n->followMode,
                        [n](int i) { n->followMode = i; });
         if (n->followMode == PathNode::kFollowSlice)
         {
            ModSliderInt("axis 0=X 1=Y 2=Z", &n->sliceAxis, 0, 2);
            ModSlider("slice at", &n->slicePosition, -3.0f, 3.0f);
         }
         ModSliderInt("contour", &n->contourIndex, 0, 8);
      }

      NodeSeparator("shape");
      ModSlider("size x", &n->sizeX, 0.0f, 3.0f);
      ModSlider("size y", &n->sizeY, 0.0f, 3.0f);
      ModSlider("size z", &n->sizeZ, 0.0f, 3.0f);
      if (n->shape == PathNode::kHelix || n->shape == PathNode::kSpiral)
         ModSlider("turns", &n->turns, 0.5f, 12.0f);
      if (n->shape == PathNode::kLissajous)
      {
         ModSliderInt("ratio a", &n->lissajousA, 1, 9);
         ModSliderInt("ratio b", &n->lissajousB, 1, 9);
      }
   }

   void DrawOceanParams(OceanNode* n)
   {
      NodeSeparator("surface");
      ModSliderInt("resolution", &n->resolution, 8, 300);
      ModSlider("size", &n->size, 0.5f, 20.0f);

      NodeSeparator("waves");
      ModSlider("amplitude", &n->amplitude, 0.0f, 1.0f);
      ModSlider("wavelength", &n->wavelength, 0.1f, 8.0f);
      ModSlider("steepness", &n->steepness, 0.0f, 2.0f);
      ModSlider("choppiness", &n->choppiness, 0.0f, 2.0f);
      ModSlider("direction", &n->direction, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSliderInt("octaves", &n->octaves, 1, 8);
      ModSlider("speed", &n->speed, -3.0f, 3.0f);
   }

   void DrawCurveParams(CurveNode* n)
   {
      ImGui::TextDisabled("%zu points, %zu triangles", n->PointCount(), n->TriangleCount());
      DropdownButton("type", CurveNode::KindNames(), n->kind, [n](int i) { n->kind = i; });
      DropdownButton("preset", CurveNode::PresetNames(), n->preset, [n](int i) { n->preset = i; });

      NodeSeparator("shape");
      ModSliderInt("points", &n->pointCount, 2, CurveNode::kMaxPoints);
      ModSliderInt("smoothness", &n->segments, 1, 64);
      ImGui::Checkbox("closed", &n->closed);
      ModSlider("spread", &n->spread, 0.0f, 4.0f);
      ModSlider("height", &n->height, -3.0f, 3.0f);
      ModSlider("twist", &n->twist, -180.0f, 180.0f, "%.1f\xC2\xB0");
      if (n->preset == 3)
         ModSlider("seed", &n->seed, 0.0f, 100.0f);

      NodeSeparator("tube");
      ModSlider("radius", &n->radius, 0.0f, 0.5f);
      ModSliderInt("sides", &n->sides, 3, 32);
      ModSlider("taper", &n->taper, 0.0f, 1.0f);
   }

   void DrawMetaBallParams(MetaBallNode* n)
   {
      ImGui::TextDisabled("%zu balls, %zu triangles", n->BallCount(), n->TriangleCount());
      NodeSeparator("field");
      if (n->cloudSource == nullptr)
      {
         ModSliderInt("balls", &n->ballCount, 1, MetaBallNode::kMaxBalls);
         ModSlider("spread", &n->spread, 0.0f, 2.0f);
         ModSlider("orbit / beat", &n->spin, -1.0f, 1.0f);
      }
      else
      {
         ModSliderInt("max from cloud", &n->maxFromCloud, 1, 64);
      }
      ModSlider("radius", &n->radius, 0.05f, 1.5f);
      ModSlider("threshold", &n->threshold, 0.5f, 40.0f);
      ModSlider("bounds", &n->bounds, 0.5f, 6.0f);
      ModSliderInt("resolution", &n->resolution, 8, 96);
   }

   void DrawJoinGeometryParams(JoinGeometryNode* n)
   {
      ImGui::TextDisabled("%d inputs, %zu triangles", n->ConnectedCount(), n->TriangleCount());
      DropdownButton("mode", JoinGeometryNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      // A merged mesh is one draw call, so it can only wear one material -
      // picked from an input rather than authored here, since editing colour
      // and shading now lives on the dedicated Material node.
      ModSliderInt("material from input", &n->materialFrom, 0, JoinGeometryNode::kSlots - 1);
   }

   void DrawSwitcher3DParams(Switcher3DNode* n)
   {
      DropdownButton("unit", Switcher3DNode::UnitNames(), n->unit, [n](int i) { n->unit = i; });
      ModSlider("every", &n->interval, 0.05f, 32.0f);
      ImGui::Checkbox("manual", &n->manual);
      if (n->manual)
      {
         ImGui::SetNextItemWidth(kParamWidth);
         ImGui::SliderInt("slot", &n->manualSlot, 0, Switcher3DNode::kSlots - 1);
      }
      else
      {
         ImGui::TextDisabled("showing input %c", 'A' + n->ActiveSlot());
      }
   }

   void DrawWrapParams(WrapNode* n)
   {
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());
      DropdownButton("mode", WrapNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      // Axis, radius and fit steer the parametric bend; nearest-surface has no
      // parameterisation for them to act on.
      if (n->mode != MeshOps::kWrapNearest)
      {
         ModSliderInt("axis 0=X 1=Y 2=Z", &n->axis, 0, 2);
         // With a target the radius always follows the target's size, so the
         // only control is a multiplier - and the resolved value is shown so
         // that link stays visible. Without one there is nothing to follow,
         // so the radius is set outright. Never both.
         if (n->targetInput != nullptr)
         {
            ImGui::TextDisabled("radius %.3f (from target)", n->ResolvedRadius());
            ModSlider("radius scale", &n->radiusScale, 0.05f, 3.0f);
         }
         else
         {
            ModSlider("radius", &n->radiusOverride, 0.05f, 5.0f);
         }
         ImGui::Checkbox("fit around", &n->fitAround);
      }
      ModSlider("offset", &n->offset, -0.5f, 0.5f);
      ModSlider("blend", &n->blend, 0.0f, 1.0f);
      ImGui::Checkbox("flat shade", &n->flatShade);
      ImGui::Checkbox("flip normals", &n->flipNormals);
   }

   void DrawClothParams(ClothNode* n)
   {
      ImGui::TextDisabled("%zu triangles, %zu links", n->TriangleCount(), n->ConstraintCount());
      if (ImGui::Button("Reset", ImVec2(kParamWidth, 0)))
         n->Reset();

      NodeSeparator("solver");
      DropdownButton("pinned", ClothNode::PinModeNames(), n->pinMode, [n](int i) { n->pinMode = i; });
      ModSlider("stiffness", &n->stiffness, 0.0f, 1.0f);
      ModSliderInt("iterations", &n->iterations, 1, 40);
      ModSlider("damping", &n->damping, 0.0f, 0.5f);
      ModSlider("mass", &n->mass, 0.05f, 10.0f);
      ModSlider("hold shape", &n->shapeRetention, 0.0f, 1.0f);

      NodeSeparator("forces");
      ModSlider("gravity x", &n->gravityX, -20.0f, 20.0f);
      ModSlider("gravity y", &n->gravityY, -20.0f, 20.0f);
      ModSlider("gravity z", &n->gravityZ, -20.0f, 20.0f);
      ModSlider("wind x", &n->windX, -20.0f, 20.0f);
      ModSlider("wind y", &n->windY, -20.0f, 20.0f);
      ModSlider("wind z", &n->windZ, -20.0f, 20.0f);
      ModSlider("wind turbulence", &n->windTurbulence, 0.0f, 20.0f);

      NodeSeparator("ground");
      ImGui::Checkbox("collide with ground", &n->groundEnabled);
      if (n->groundEnabled)
      {
         ModSlider("height", &n->groundHeight, -5.0f, 5.0f);
         ModSlider("bounce", &n->bounce, 0.0f, 1.0f);
         ModSlider("friction", &n->friction, 0.0f, 1.0f);
      }
   }

   void DrawParticleSystemParams(ParticleSystemNode* n)
   {
      ImGui::TextDisabled("%zu alive", n->AliveCount());
      if (ImGui::Button("Reset", ImVec2(kParamWidth, 0)))
         n->Reset();

      NodeSeparator("emitter");
      DropdownButton("shape", ParticleSystemNode::EmitShapeNames(), n->emitShape,
                     [n](int i) { n->emitShape = i; });
      ModSliderInt("max particles", &n->maxParticles, 16, 50000);
      ModSlider("rate / sec", &n->emitRate, 0.0f, 3000.0f);
      ModSlider("radius", &n->emitRadius, 0.0f, 3.0f);
      ModSlider("lifetime", &n->lifetime, 0.1f, 20.0f);
      ModSlider("life random", &n->lifetimeRandom, 0.0f, 1.0f);

      NodeSeparator("launch");
      ModSlider("speed", &n->initialSpeed, 0.0f, 10.0f);
      ModSlider("speed random", &n->speedRandom, 0.0f, 1.0f);
      ModSlider("spread", &n->spread, 0.0f, 1.0f);
      ModSlider("dir x", &n->dirX, -1.0f, 1.0f);
      ModSlider("dir y", &n->dirY, -1.0f, 1.0f);
      ModSlider("dir z", &n->dirZ, -1.0f, 1.0f);

      NodeSeparator("forces");
      ModSlider("gravity x", &n->gravityX, -10.0f, 10.0f);
      ModSlider("gravity y", &n->gravityY, -10.0f, 10.0f);
      ModSlider("gravity z", &n->gravityZ, -10.0f, 10.0f);
      ModSlider("drag", &n->drag, 0.0f, 5.0f);
      ModSlider("turbulence", &n->turbulence, 0.0f, 10.0f);
      ModSlider("turb scale", &n->turbulenceScale, 0.1f, 8.0f);

      NodeSeparator("over life");
      ModSlider("start size", &n->startSize, 0.0f, 4.0f);
      ModSlider("end size", &n->endSize, 0.0f, 4.0f);
      ColorSwatch("start colour", n->startColor, n);
      ColorSwatch("end colour", n->endColor, n);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);
   }

   void DrawMaterialParams(MaterialNode* n)
   {
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());
      // Only shown when a normal map is actually patched in - a strength slider
      // for a map that is not there is just another dead control.
      if (n->MapInput(kMapNormal).IsConnected())
         ModSlider("normal strength", &n->normalStrength, 0.0f, 4.0f);
      NodeSeparator("material");
      DropdownButton("shading", GeometryNode::ShadingNames(), n->shading, [n](int i) { n->shading = i; });
      ColorSwatch("colour", n->color, n);
      ModSlider("metallic", &n->metallic, 0.0f, 1.0f);
      ModSlider("roughness", &n->roughness, 0.02f, 1.0f);
      ModSlider("opacity", &n->opacity, 0.0f, 1.0f);
      ColorSwatch("emission", n->emissionColor, n);
      ModSlider("emission", &n->emission, 0.0f, 8.0f);
   }

   void DrawMappingParams(MappingNode* n)
   {
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());
      DropdownButton("space", MappingNode::SpaceNames(), n->space, [n](int i) { n->space = i; });

      NodeSeparator("translate");
      ModSlider("x", &n->translateX, -4.0f, 4.0f);
      ModSlider("y", &n->translateY, -4.0f, 4.0f);
      if (n->space != kMapSpaceUv)
         ModSlider("z", &n->translateZ, -4.0f, 4.0f);

      NodeSeparator("rotate");
      if (n->space == kMapSpaceUv)
      {
         ModSlider("z", &n->rotateZ, -180.0f, 180.0f, "%.1f\xC2\xB0");
      }
      else
      {
         ModSlider("x", &n->rotateX, -180.0f, 180.0f, "%.1f\xC2\xB0");
         ModSlider("y", &n->rotateY, -180.0f, 180.0f, "%.1f\xC2\xB0");
         ModSlider("z", &n->rotateZ, -180.0f, 180.0f, "%.1f\xC2\xB0");
      }

      NodeSeparator("scale");
      ModSlider("x", &n->scaleX, 0.05f, 8.0f);
      ModSlider("y", &n->scaleY, 0.05f, 8.0f);
      if (n->space != kMapSpaceUv)
         ModSlider("z", &n->scaleZ, 0.05f, 8.0f);
   }

   void DrawMeshResynthParams(MeshResynthNode* n)
   {
      ImGui::TextDisabled("generation %d, %zu triangles", n->Generation(), n->TriangleCount());
      if (ImGui::Button("step")) n->StepOnce();
      ImGui::SameLine();
      if (ImGui::Button("reset")) n->Reset();
      ImGui::SameLine();
      if (ImGui::Button("randomise")) n->Randomise();

      NodeSeparator("evolve");
      ModSlider("chaos", &n->chaos, 0.0f, 1.5f);
      ImGui::Checkbox("auto step", &n->autoStep);
      if (n->autoStep)
         ModSlider("steps per beat", &n->stepsPerBeat, 0.05f, 8.0f);
      ModSlider("seed", &n->seed, 0.0f, 1000.0f);
      ModSliderInt("triangle budget", &n->triangleBudget, 2000, 500000);

      NodeSeparator("operators");
      for (int i = 0; i < MeshResynthNode::kOpCount; i++)
         ModSlider(MeshResynthNode::OpNames()[i].c_str(), &n->weight[i], 0.0f, 1.0f);
   }

   void DrawImageToPointsParams(ImageToPointsNode* n)
   {
      ImGui::TextDisabled("%zu points", n->PointCount());
      ModSliderInt("density", &n->density, 4, 400);
      ModSlider("width", &n->width, 0.1f, 8.0f);
      ModSlider("height", &n->height, 0.1f, 8.0f);
      ModSlider("threshold", &n->threshold, 0.0f, 1.0f);

      NodeSeparator("depth");
      DropdownButton("from", ImageToPointsNode::DepthSourceNames(), n->depthSource,
                     [n](int i) { n->depthSource = i; });
      ModSlider("depth scale", &n->depthScale, -4.0f, 4.0f);

      NodeSeparator("points");
      ModSlider("point size", &n->pointSize, 0.01f, 4.0f);
      ModSlider("size from luma", &n->sizeFromLuma, -1.0f, 1.0f);
      ImGui::Checkbox("use image colour", &n->useImageColor);
      ColorSwatch("tint", n->tint, n);
   }

   void DrawMeshToPointsParams(MeshToPointsNode* n)
   {
      DropdownButton("sample", MeshToPointsNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ImGui::TextDisabled("%zu points, %zu triangles", n->PointCount(), n->TriangleCount());
      ModSliderInt("max points", &n->maxPoints, 16, 20000);
      ModSlider("point size", &n->pointSize, 0.002f, 0.3f);
      ImGui::Checkbox("weld seams", &n->weld);
      if (n->mode == 1)
         ModSlider("dissolve angle", &n->dissolveAngleDegrees, 0.0f, 30.0f);
   }

   void DrawText3DParams(Text3DNode* n)
   {
      char buf[256];
      snprintf(buf, sizeof(buf), "%s", n->text.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (ImGui::InputText("text", buf, sizeof(buf)))
         n->text = buf;

      {
         const std::vector<std::string>& families = Platform::AvailableFontFamilies();
         int current = 0;
         for (size_t i = 0; i < families.size(); i++)
            if (families[i] == n->fontName)
               current = (int)i;
         DropdownButton("font", families, current,
                        [n, &families](int i)
                        {
                           if (i >= 0 && i < (int)families.size())
                              n->fontName = families[(size_t)i];
                        });
      }

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextUnformatted(n->Status().c_str());
      ImGui::PopTextWrapPos();

      NodeSeparator("form");
      ModSlider("depth", &n->depth, 0.0f, 1.5f);
      ModSlider("bevel", &n->bevel, 0.0f, 0.45f);
      ModSlider("tracking", &n->letterSpacing, -0.1f, 0.5f);
   }

   void DrawModelParams(ModelSourceNode* n)
   {
      if (ImGui::Button("Open model...", ImVec2(kParamWidth, 0)))
      {
         const std::string path = Platform::OpenModelDialog();
         if (!path.empty())
            n->Load(path);
      }
      // Bare TextWrapped has no usable content width inside the node editor and
      // wraps to one character per line; every other panel here sets the wrap
      // position explicitly.
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextUnformatted(n->Status().c_str());
      ImGui::PopTextWrapPos();

      NodeSeparator("import");
      // Evaluated into separate variables rather than OR'd inline: `||` would
      // short-circuit and skip drawing the second checkbox on any frame the
      // first one was clicked.
      const bool fitChanged = ImGui::Checkbox("fit to unit size", &n->normalizeScale);
      const bool centreChanged = ImGui::Checkbox("recentre", &n->recenter);
      if (fitChanged || centreChanged)
      {
         // Both are applied at load time, so changing them has to re-import.
         if (!n->Path().empty())
            n->Load(n->Path());
      }
   }

   void DrawGeometryParams(GeometryNode* n)
   {
      DropdownButton("shape", GeometryNode::ShapeNames(), n->shape,
         [n](int i)
         {
            // Pyramid and Prism are Cylinder/Cone under the hood, told apart
            // only by `sides` - at the high facet counts those default to
            // (left over from a round shape, or just never touched) they
            // read as a cone/cylinder instead of the flat-faced primitive
            // the name promises. Snap to a canonical low count on selection
            // so the dropdown pick is recognizable immediately; the user can
            // still dial `sides` back up afterward for a rounder look.
            if (i == 10 && n->shape != 10) n->sides = 4;       // Pyramid -> square pyramid
            else if (i == 11 && n->shape != 11) n->sides = 3;  // Prism -> triangular prism
            n->shape = i;
         });
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());

      NodeSeparator("form");
      ModSliderInt("detail", &n->detail, 2, 96);
      ModSlider("bevel", &n->bevel, 0.0f, 1.0f);
      if (n->shape == 13) // supershape
      {
         ModSlider("n2", &n->superN2, 0.1f, 8.0f);
         ModSlider("n3", &n->superN3, 0.1f, 8.0f);
         ModSlider("p2", &n->superP2, 0.1f, 8.0f);
         ModSlider("p3", &n->superP3, 0.1f, 8.0f);
      }
      if (n->bevel > 0.0f)
         ModSliderInt("bevel segments", &n->bevelSegments, 1, 3);
      ModSliderInt("sides", &n->sides, 3, 64);
      // `tube` and `n2` are reused as the second and third form parameter by
      // several primitives, so each one is labelled for the shape in front of
      // you rather than by the variable it happens to live in. Capsule, Tube
      // and Helix were reading tubeRadius all along with no way to set it.
      switch (n->shape)
      {
         case 4: case 7: case 8:
            ModSlider("tube", &n->tubeRadius, 0.02f, 0.95f); break;
         case 9:
            ModSlider("inner radius", &n->tubeRadius, 0.02f, 0.95f); break;
         case 12:
            ModSlider("wire", &n->tubeRadius, 0.02f, 0.95f); break;
         case 17:
            ModSlider("corner radius", &n->tubeRadius, 0.0f, 0.99f); break;
         case 18:
            ModSlider("width", &n->tubeRadius, 0.02f, 0.95f); break;
         case 20:
            ModSlider("depth", &n->tubeRadius, 0.02f, 0.95f);
            ModSlider("tooth depth", &n->superN2, 0.1f, 2.0f);
            ModSlider("hub hole", &n->superN3, 0.0f, 2.0f);
            break;
         case 21:
            ModSlider("depth", &n->tubeRadius, 0.02f, 0.95f);
            ModSlider("inner ratio", &n->superN2, 0.1f, 1.9f);
            break;
         case 22:
            ModSlider("inner radius", &n->discInner, 0.0f, 0.98f); break;
         case 23:
            ModSlider("shaft radius", &n->tubeRadius, 0.02f, 0.8f);
            ModSlider("head length", &n->superN2, 0.15f, 2.5f);
            break;
         default: break;
      }
      if (n->shape == 7 || n->shape == 12)
      {
         ModSliderInt(n->shape == 12 ? "turns" : "knot p", &n->knotP, 1, 8);
         ModSliderInt(n->shape == 12 ? "height" : "knot q", &n->knotQ, 1, 8);
      }
   }

   void DrawGeometryOpParams(GeometryOpNode* n)
   {
      DropdownButton("operation", GeometryOpNode::OpNames(), n->op, [n](int i) { n->op = i; });
      if (n->input != nullptr)
         ImGui::TextDisabled("%zu triangles out", n->TriangleCount());

      switch (n->op)
      {
         case GeometryOpNode::kTransform:
            ModSlider("move x", &n->offsetX, -3.0f, 3.0f);
            ModSlider("move y", &n->offsetY, -3.0f, 3.0f);
            ModSlider("move z", &n->offsetZ, -3.0f, 3.0f);
            ModSlider("rotate x", &n->rotX, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("rotate y", &n->rotY, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("rotate z", &n->rotZ, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("scale x", &n->scaleX, 0.05f, 4.0f);
            ModSlider("scale y", &n->scaleY, 0.05f, 4.0f);
            ModSlider("scale z", &n->scaleZ, 0.05f, 4.0f);
            ModSlider("spin / beat", &n->spin, -2.0f, 2.0f);
            break;
         case GeometryOpNode::kArray:
            ModSliderInt("count", &n->count, 1, 128);
            ImGui::Checkbox("radial", &n->radial);
            if (n->radial)
               ModSlider("radius", &n->radius, 0.0f, 5.0f);
            else
            {
               ModSlider("offset x", &n->offsetX, -2.0f, 2.0f);
               ModSlider("offset y", &n->offsetY, -2.0f, 2.0f);
               ModSlider("offset z", &n->offsetZ, -2.0f, 2.0f);
            }
            ModSlider("rot / step", &n->rotStep, -85.9437f, 85.9437f, "%.1f\xC2\xB0");
            ModSlider("scale / step", &n->scaleStep, 0.5f, 1.5f);
            break;
         case GeometryOpNode::kSubdivide:
            ModSliderInt("levels", &n->levels, 0, 3);
            ModSlider("smooth", &n->smooth, 0.0f, 2.0f);
            break;
         case GeometryOpNode::kSolidify:
            ModSlider("thickness", &n->thickness, 0.001f, 0.5f);
            ImGui::Checkbox("keep original", &n->keepOriginal);
            break;
         case GeometryOpNode::kExtrude:
            ModSlider("distance", &n->thickness, -0.5f, 0.5f);
            ModSlider("inset", &n->inset, 0.0f, 0.9f);
            break;
         case GeometryOpNode::kWireframe:
            ModSlider("thickness", &n->thickness, 0.002f, 0.1f);
            break;
         case GeometryOpNode::kTriangulate:
            ModSlider("jitter", &n->amount, 0.0f, 2.0f);
            break;
         case GeometryOpNode::kNormals:
            ImGui::Checkbox("flat shade", &n->flatShade);
            ImGui::Checkbox("flip", &n->flipNormals);
            break;
         case GeometryOpNode::kExplode:
            ModSlider("amount", &n->amount, 0.0f, 3.0f);
            ModSlider("seed", &n->seed, 0.0f, 100.0f);
            break;
         case GeometryOpNode::kSmooth:
            ModSliderInt("pre-subdivide", &n->levels, 0, 3);
            ModSliderInt("iterations", &n->iterations, 1, 20);
            ModSlider("strength", &n->amount, 0.0f, 1.0f);
            break;
         case GeometryOpNode::kMirror:
            ModSliderInt("axis 0=X 1=Y 2=Z", &n->axis, 0, 2);
            ModSlider("plane offset", &n->mirrorOffset, -2.0f, 2.0f);
            ImGui::Checkbox("keep original", &n->keepOriginal);
            ImGui::Checkbox("weld seam", &n->weldSeam);
            break;
         case GeometryOpNode::kSelect:
         {
            static const std::vector<std::string> kSelectModes = {
               "All", "By index", "By position", "By normal", "Random", "By radius"
            };
            ImGui::TextDisabled("%zu of %zu faces", n->SelectedCount(), n->TriangleCount());
            DropdownButton("mode", kSelectModes, n->selectMode, [n](int i) { n->selectMode = i; });
            switch (n->selectMode)
            {
               case 1: // index
                  ModSlider("start", &n->selectA, 0.0f, 2000.0f, "%.0f");
                  ModSlider("count", &n->selectB, 0.0f, 2000.0f, "%.0f");
                  ModSlider("every", &n->selectC, 1.0f, 32.0f, "%.0f");
                  break;
               case 2: // position along an axis
                  ModSliderInt("axis 0=X 1=Y 2=Z", &n->axis, 0, 2);
                  ModSlider("min", &n->selectA, -3.0f, 3.0f);
                  ModSlider("max", &n->selectB, -3.0f, 3.0f);
                  break;
               case 3: // normal direction
                  ModSliderInt("axis 0=X 1=Y 2=Z", &n->axis, 0, 2);
                  ModSlider("facing", &n->selectA, -1.0f, 1.0f);
                  ModSlider("sign", &n->selectC, -1.0f, 1.0f);
                  break;
               case 4: // random
                  ModSlider("amount", &n->selectA, 0.0f, 1.0f);
                  ModSlider("seed", &n->selectSeed, 0.0f, 100.0f);
                  break;
               case 5: // radius
                  ModSlider("x", &n->selectA, -3.0f, 3.0f);
                  ModSlider("y", &n->selectB, -3.0f, 3.0f);
                  ModSlider("z", &n->selectC, -3.0f, 3.0f);
                  ModSlider("radius", &n->selectSeed, 0.0f, 3.0f);
                  break;
               default: break;
            }
            ImGui::Checkbox("invert", &n->selectInvert);
            ImGui::Checkbox("add to selection", &n->selectAppend);
            break;
         }
         case GeometryOpNode::kDeleteSelected:
            ImGui::TextDisabled("%zu of %zu faces selected", n->SelectedCount(), n->TriangleCount());
            ImGui::Checkbox("keep selected instead", &n->keepSelected);
            break;
         case GeometryOpNode::kTransformSelected:
            ImGui::Checkbox("move along normals", &n->moveAlongNormals);
            if (n->moveAlongNormals)
               ModSlider("normal amount", &n->normalAmount, -2.0f, 2.0f);
            ModSlider("move x", &n->offsetX, -3.0f, 3.0f);
            ModSlider("move y", &n->offsetY, -3.0f, 3.0f);
            ModSlider("move z", &n->offsetZ, -3.0f, 3.0f);
            ModSlider("rotate x", &n->rotX, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("rotate y", &n->rotY, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("rotate z", &n->rotZ, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("scale x", &n->scaleX, 0.1f, 3.0f);
            ModSlider("scale y", &n->scaleY, 0.1f, 3.0f);
            ModSlider("scale z", &n->scaleZ, 0.1f, 3.0f);
            ModSlider("spin / beat", &n->spin, -2.0f, 2.0f);
            break;
         case GeometryOpNode::kExtrudeSelected:
            ModSlider("distance", &n->thickness, -1.0f, 1.0f);
            ModSlider("inset", &n->inset, 0.0f, 0.9f);
            break;
         case GeometryOpNode::kScrew:
            ModSliderInt("steps", &n->screwSteps, 3, 256);
            ModSlider("turns", &n->turns, 0.05f, 6.0f);
            ModSlider("rise / turn", &n->rise, -2.0f, 2.0f);
            ModSlider("radius", &n->radiusOffset, 0.0f, 3.0f);
            ModSliderInt("axis 0=X 1=Y 2=Z", &n->axis, 0, 2);
            break;
         default:
            ModSlider("angle", &n->amount, -171.8873f, 171.8873f, "%.1f\xC2\xB0");
            ModSliderInt("axis 0=X 1=Y 2=Z", &n->axis, 0, 2);
            break;
      }
   }

   void DrawDisplacementParams(DisplacementNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%zu triangles", n->TriangleCount());

      static const std::vector<std::string> kModeNames = { "Scalar (along normal)", "Vector (RGB = XYZ)" };
      DropdownButton("mode", kModeNames, n->mode, [n](int i) { n->mode = i; });
      ModSlider("strength", &n->strength, -2.0f, 2.0f);
      if (n->mode == DisplacementNode::kScalar)
         ModSlider("midlevel", &n->midlevel, 0.0f, 1.0f);
      ImGui::Checkbox("flat shade", &n->flatShade);
      ImGui::Checkbox("flip normals", &n->flipNormals);
   }

   void DrawInstanceParams(InstanceOnPointsNode* n)
   {
      if (n->pointSource != nullptr && n->instanceShape != nullptr)
         ImGui::TextDisabled("%zu instances, %zu triangles", n->InstanceCount(), n->TriangleCount());

      DropdownButton("points from", InstanceOnPointsNode::SourceNames(), n->pointMode,
                     [n](int i) { n->pointMode = i; });
      ModSliderInt("max points", &n->maxPoints, 1, 20000);
      ModSlider("scale", &n->instanceScale, 0.005f, 1.0f);
      ModSlider("scale random", &n->scaleRandom, 0.0f, 1.0f);
      ModSlider("rotation random", &n->rotationRandom, 0.0f, 1.0f);
      ModSlider("normal offset", &n->normalOffset, -0.5f, 0.5f);
      ImGui::Checkbox("align to normal", &n->alignToNormal);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);
   }

   void DrawCameraParams(CameraNode* n)
   {
      DropdownButton("projection", CameraNode::ProjectionNames(), n->projection,
                     [n](int i) { n->projection = i; });
      if (n->projection == 0)
         ModSlider("fov", &n->fov, 10.0f, 120.0f);
      else
         ModSlider("ortho height", &n->orthoHeight, 0.2f, 8.0f);
      ModSlider("distance", &n->distance, 0.3f, 20.0f);
      ModSlider("orbit", &n->azimuth, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("elevation", &n->elevation, -85.9437f, 85.9437f, "%.1f\xC2\xB0");
      ModSlider("roll", &n->roll, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("orbit / beat", &n->orbitPerBeat, -1.0f, 1.0f);
      ModSlider("target x", &n->targetX, -3.0f, 3.0f);
      ModSlider("target y", &n->targetY, -3.0f, 3.0f);
      ModSlider("target z", &n->targetZ, -3.0f, 3.0f);
      ModSlider("near", &n->nearPlane, 0.01f, 1.0f);
      ModSlider("far", &n->farPlane, 5.0f, 500.0f);
   }

   void DrawLightParams(LightNode* n)
   {
      DropdownButton("type", LightNode::TypeNames(), n->type, [n](int i) { n->type = i; });
      ModSlider("orbit", &n->azimuth, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("elevation", &n->elevation, -85.9437f, 85.9437f, "%.1f\xC2\xB0");
      if (n->type == 1)
         ModSlider("distance", &n->distance, 0.2f, 20.0f);
      ColorSwatch("colour", n->color, n);
      ModSlider("intensity", &n->intensity, 0.0f, 5.0f);
      ModSlider("orbit / beat", &n->orbitPerBeat, -1.0f, 1.0f);
   }

   // Fits the camera around everything patched into a Render node.
   //
   // Bounds are taken from each mesh's object-space corners pushed through its
   // model matrix, rather than from every vertex: a rotated box's true extent
   // needs the corners, and a scene can carry hundreds of thousands of vertices
   // that would be pointless to walk for a button press.
   void FrameSceneInView(Render3DNode* n)
   {
      float lo[3] = { 1e30f, 1e30f, 1e30f };
      float hi[3] = { -1e30f, -1e30f, -1e30f };
      bool any = false;

      for (int i = 0; i < Render3DNode::kSlots; i++)
      {
         IGeometrySource* source = n->geometry[i];
         if (source == nullptr)
            continue;
         const Mesh& mesh = source->GetMesh();
         if (mesh.Empty())
            continue;

         float mlo[3] = { 1e30f, 1e30f, 1e30f };
         float mhi[3] = { -1e30f, -1e30f, -1e30f };
         for (const Vertex& v : mesh.vertices)
         {
            const float p[3] = { v.px, v.py, v.pz };
            for (int k = 0; k < 3; k++)
            {
               if (!std::isfinite(p[k]))
                  continue;
               mlo[k] = std::min(mlo[k], p[k]);
               mhi[k] = std::max(mhi[k], p[k]);
            }
         }
         if (mlo[0] > mhi[0])
            continue;

         const Mat4 model = source->GetModelMatrix();
         for (int corner = 0; corner < 8; corner++)
         {
            const float c[3] = {
               (corner & 1) ? mhi[0] : mlo[0],
               (corner & 2) ? mhi[1] : mlo[1],
               (corner & 4) ? mhi[2] : mlo[2]
            };
            for (int k = 0; k < 3; k++)
            {
               const float w = model.m[k] * c[0] + model.m[4 + k] * c[1] +
                               model.m[8 + k] * c[2] + model.m[12 + k];
               lo[k] = std::min(lo[k], w);
               hi[k] = std::max(hi[k], w);
            }
         }
         any = true;
      }

      if (!any)
         return;

      const float centre[3] = { (lo[0]+hi[0])*0.5f, (lo[1]+hi[1])*0.5f, (lo[2]+hi[2])*0.5f };
      const float radius = 0.5f * std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0]) +
                                            (hi[1]-lo[1])*(hi[1]-lo[1]) +
                                            (hi[2]-lo[2])*(hi[2]-lo[2]));

      // Distance that puts the bounding sphere just inside the vertical field
      // of view, with a little air around it.
      const float fovDegrees = n->camera ? n->camera->fov : n->fov;
      const float halfFov = std::max(0.1f, fovDegrees * 0.5f * 3.14159265f / 180.0f);
      const float distance = std::max(0.3f, (radius / std::sin(halfFov)) * 1.15f);

      if (n->camera != nullptr)
      {
         n->camera->targetX = centre[0];
         n->camera->targetY = centre[1];
         n->camera->targetZ = centre[2];
         n->camera->distance = distance;
      }
      else
      {
         n->targetX = centre[0];
         n->targetY = centre[1];
         n->targetZ = centre[2];
         n->camDistance = distance;
      }
   }

   void DrawRender3DParams(Render3DNode* n)
   {
      int connected = 0;
      for (int i = 0; i < Render3DNode::kSlots; i++)
         if (n->geometry[i] != nullptr || n->clouds[i] != nullptr)
            connected++;
      ImGui::TextDisabled("%d geometry, %s camera", connected, n->camera ? "patched" : "built-in");
      ImGui::TextDisabled("%zu triangles in %zu draw calls", n->LastTriangleCount(), n->LastDrawCalls());
      if (ImGui::Button("Frame scene", ImVec2(kParamWidth, 0)))
         FrameSceneInView(n);

      NodeSeparator("output");
      // This is the export resolution: Output sizes its own buffer from whatever
      // its input hands it, so a 4000px PNG needs 4000 set here or it is an
      // upscale of a smaller render.
      ModSlider("width", &n->width, 64.0f, 8192.0f, "%.0f");
      ModSlider("height", &n->height, 64.0f, 8192.0f, "%.0f");
      DropdownButton("antialias", Render3DNode::SampleNames(), n->samples,
                     [n](int i) { n->samples = i; });
      {
         // The requested sample count is clamped by both the driver and a memory
         // budget, so show what actually happened when they disagree.
         const int wanted = (n->samples <= 0) ? 0 : (1 << n->samples);
         if (n->ActiveSamples() != wanted)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                               "antialias reduced to %dx at this size",
                               n->ActiveSamples());
      }
      DropdownButton("tonemap", Render3DNode::TonemapNames(), n->tonemap,
                     [n](int i) { n->tonemap = i; });
      ModSlider("exposure", &n->exposure, 0.1f, 4.0f);
      ColorSwatch("background", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);

      NodeSeparator("points");
      DropdownButton("sprite shape", Render3DNode::SpriteShapeNames(), n->spriteShape,
                     [n](int i) { n->spriteShape = i; });
      DropdownButton("sprite size", Render3DNode::SpriteSizeModeNames(), n->spriteSizeMode,
                     [n](int i) { n->spriteSizeMode = i; });

      NodeSeparator("camera");
      if (n->camera != nullptr)
         ImGui::TextDisabled("driven by a Camera node");
      else
      {
      DropdownButton("projection", Render3DNode::ProjectionNames(), n->projection,
                     [n](int i) { n->projection = i; });
      if (n->projection == 0)
         ModSlider("fov", &n->fov, 10.0f, 120.0f);
      else
         ModSlider("ortho height", &n->orthoHeight, 0.2f, 8.0f);
      ModSlider("distance", &n->camDistance, 0.3f, 20.0f);
      ModSlider("orbit", &n->camAzimuth, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("elevation", &n->camElevation, -85.9437f, 85.9437f, "%.1f\xC2\xB0");
      ModSlider("target x", &n->targetX, -3.0f, 3.0f);
      ModSlider("target y", &n->targetY, -3.0f, 3.0f);
      ModSlider("target z", &n->targetZ, -3.0f, 3.0f);
      }

      NodeSeparator("shadows");
      ImGui::Checkbox("cast shadows", &n->shadowsEnabled);
      if (n->shadowsEnabled)
      {
         DropdownButton("resolution", Render3DNode::ShadowQualityNames(), n->shadowQuality,
                        [n](int i) { n->shadowQuality = i; });
         ModSlider("strength", &n->shadowStrength, 0.0f, 1.0f);
         ModSlider("softness", &n->shadowSoftness, 0.0f, 4.0f);
         ModSlider("bias", &n->shadowBias, 0.0002f, 0.02f, "%.4f");
      }

      NodeSeparator("light");
      int patchedLights = 0;
      for (int i = 0; i < Render3DNode::kLightSlots; i++)
         if (n->lights[i] != nullptr)
            patchedLights++;
      if (patchedLights > 0)
         ImGui::TextDisabled("%d Light node%s patched", patchedLights, patchedLights == 1 ? "" : "s");
      else
      {
      ModSlider("light orbit", &n->lightAzimuth, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("light height", &n->lightElevation, -85.9437f, 85.9437f, "%.1f\xC2\xB0");
      ColorSwatch("light", n->lightColor, n);
      ModSlider("intensity", &n->lightIntensity, 0.0f, 4.0f);
      }
      ColorSwatch("ambient", n->ambientColor, n);
      ModSlider("rim", &n->rimIntensity, 0.0f, 2.0f);

      NodeSeparator("environment");
      const bool envConnected = n->envInput.IsConnected();
      if (envConnected)
      {
         ImGui::TextDisabled("driven by an HDRI node");
         ImGui::Checkbox("use as background", &n->envAsBackground);
      }
      else
      {
         ColorSwatch("sky", n->envSky, n);
         ColorSwatch("horizon", n->envHorizon, n);
         ColorSwatch("ground", n->envGround, n);
      }
      ModSlider("env intensity", &n->envIntensity, 0.0f, 3.0f);

      NodeSeparator("raster");
      ImGui::Checkbox("depth test", &n->depthTest);
      ImGui::Checkbox("cull backfaces", &n->backfaceCull);
   }

   void DrawBlendParams(BlendNode* n)
   {
      DropdownButton("mode", BlendNode::ModeNames(), n->ModeIndex(),
                     [n](int i) { n->ModeIndex() = i; });
      ModSlider("opacity", &n->Mix(), 0.0f, 1.0f);
   }

   void DrawLayerStackParams(LayerStackNode* n)
   {
      // Layers composite bottom-up: A is the base, D sits on top. Grab a layer's
      // header strip and drag vertically to reorder - the whole layer moves,
      // cable, blend mode and opacity together.
      static LayerStackNode* sDragNode = nullptr;
      static int sDragSlot = -1;
      static float sDragAccum = 0.0f;
      const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

      for (int slot = 0; slot < LayerStackNode::kSlots; slot++)
      {
         ImGui::PushID(slot);

         const bool dragging = (sDragNode == n && sDragSlot == slot);
         ImVec2 headerPos = ImGui::GetCursorScreenPos();
         ImGui::InvisibleButton("##grip", ImVec2(kPreviewSize, rowHeight));
         if (ImGui::IsItemActive() && sDragNode == nullptr)
         {
            sDragNode = n;
            sDragSlot = slot;
            sDragAccum = 0.0f;
         }

         ImDrawList* dl = ImGui::GetWindowDrawList();
         if (dragging || ImGui::IsItemHovered())
         {
            dl->AddRectFilled(headerPos,
                              ImVec2(headerPos.x + kPreviewSize, headerPos.y + rowHeight),
                              dragging ? IM_COL32(70, 90, 130, 200) : IM_COL32(50, 54, 68, 160), 3.0f);
         }
         // grip dots, so the header reads as draggable
         for (int d = 0; d < 3; d++)
         {
            dl->AddCircleFilled(ImVec2(headerPos.x + 6, headerPos.y + 5 + d * 4.0f), 1.3f,
                                IM_COL32(140, 146, 168, 255));
            dl->AddCircleFilled(ImVec2(headerPos.x + 11, headerPos.y + 5 + d * 4.0f), 1.3f,
                                IM_COL32(140, 146, 168, 255));
         }
         // Name the layer after whatever is feeding it - far more useful than
         // "layer C" once a stack has four things in it.
         char title[96];
         const INode* source = n->Input(slot).GetSource();
         if (source != nullptr)
         {
            const char* sourceName = "?";
            for (const GraphNode& other : gNodes)
            {
               if (other.node.get() == source)
                  sourceName = other.typeName.c_str();
            }
            snprintf(title, sizeof(title), "%c  %s", 'A' + slot, sourceName);
         }
         else
         {
            snprintf(title, sizeof(title), "%c  (empty)", 'A' + slot);
         }
         dl->AddText(ImVec2(headerPos.x + 20, headerPos.y + 2),
                     source ? IM_COL32(190, 196, 215, 255) : IM_COL32(120, 124, 142, 255), title);

         char modeLabel[32];
         snprintf(modeLabel, sizeof(modeLabel), "mode##%d", slot);
         DropdownButton(modeLabel, BlendModes::Names(), n->modes[slot],
                        [n, slot](int i) { n->modes[slot] = i; });
         ModSlider("opacity", &n->opacities[slot], 0.0f, 1.0f);
         ImGui::PopID();
      }

      // Resolve the drag once per frame: accumulate vertical movement and swap a
      // slot at a time so the layer follows the cursor.
      if (sDragNode == n && sDragSlot >= 0)
      {
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            sDragNode = nullptr;
            sDragSlot = -1;
         }
         else
         {
            sDragAccum += ImGui::GetIO().MouseDelta.y;
            const float threshold = rowHeight * 3.0f; // one full layer block
            while (sDragAccum > threshold && sDragSlot < LayerStackNode::kSlots - 1)
            {
               n->SwapLayers(sDragSlot, sDragSlot + 1);
               sDragSlot++;
               sDragAccum -= threshold;
            }
            while (sDragAccum < -threshold && sDragSlot > 0)
            {
               n->SwapLayers(sDragSlot, sDragSlot - 1);
               sDragSlot--;
               sDragAccum += threshold;
            }
         }
      }
   }

   void DrawFilterParams(FilterNode* n)
   {
      const FilterDef& def = n->Def();
      for (size_t i = 0; i < def.params.size(); i++)
      {
         const FilterParamDef& p = def.params[i];
         if (!p.sectionLabel.empty())
            NodeSeparator(p.sectionLabel.c_str());
         ImGui::PushID((int)i);
         if (p.type == FilterParamDef::Type::Color)
         {
            ColorSwatch(p.label.c_str(), n->ParamPtr(i), n);
         }
         else if (p.type == FilterParamDef::Type::Enum)
         {
            float* slot = n->ParamPtr(i);
            DropdownButton(p.label.c_str(), p.options, (int)(*slot + 0.5f),
                           [slot](int choice) { *slot = (float)choice; });
         }
         else if (p.type == FilterParamDef::Type::Bool)
         {
            float* slot = n->ParamPtr(i);
            bool checked = *slot != 0.0f;
            if (ImGui::Checkbox(p.label.c_str(), &checked))
               *slot = checked ? 1.0f : 0.0f;
         }
         else
         {
            ModSlider(p.label.c_str(), n->ParamPtr(i), p.minVal, p.maxVal);
         }
         ImGui::PopID();
      }
      if (def.params.empty())
         ImGui::TextDisabled("(no parameters)");
   }

   void ExportPng(OutputNode* out, const std::string& path)
   {
      int w = out->GetOutputWidth();
      int h = out->GetOutputHeight();
      if (w <= 0 || h <= 0)
         return;

      std::vector<unsigned char> pixels(w * h * 4);
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out->GetOutputTexture(), 0);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glDeleteFramebuffers(1, &fbo);

      stbi_flip_vertically_on_write(1);
      stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4);
   }

   // Paintable preview: the Draw node turns its 1:1 preview into the canvas, so
   // you draw directly on the node rather than in a separate window.
   void DrawPaintablePreview(DrawNode* node)
   {
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##canvas", ImVec2(kPreviewSize, kPreviewSize));

      const int w = node->GetOutputWidth();
      const int h = node->GetOutputHeight();
      float dw = kPreviewSize, dh = kPreviewSize;
      if (w > 0 && h > 0)
      {
         const float scale = kPreviewSize / (float)std::max(w, h);
         dw = w * scale;
         dh = h * scale;
      }
      const ImVec2 tl(origin.x + (kPreviewSize - dw) * 0.5f, origin.y + (kPreviewSize - dh) * 0.5f);

      if (ImGui::IsItemActive())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const float u = (m.x - tl.x) / std::max(1.0f, dw);
         const float v = 1.0f - (m.y - tl.y) / std::max(1.0f, dh); // texture space is bottom-up
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            node->BeginStroke(u, v);
         else
            node->ContinueStroke(u, v);
      }
      else if (ImGui::IsItemDeactivated())
      {
         node->EndStroke();
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize),
                        IM_COL32(18, 18, 24, 255), 4.0f);
      // checkerboard, so transparent areas of the canvas are obvious
      for (int y = 0; y < 8; y++)
      {
         for (int x = 0; x < 8; x++)
         {
            if ((x + y) % 2)
               continue;
            const float c = kPreviewSize / 8.0f;
            dl->AddRectFilled(ImVec2(origin.x + x * c, origin.y + y * c),
                              ImVec2(origin.x + (x + 1) * c, origin.y + (y + 1) * c),
                              IM_COL32(30, 30, 38, 255));
         }
      }
      if (node->GetOutputTexture() != 0)
         dl->AddImage((ImTextureID)(intptr_t)node->GetOutputTexture(), tl,
                      ImVec2(tl.x + dw, tl.y + dh), ImVec2(0, 1), ImVec2(1, 0));
      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize),
                  IM_COL32(90, 130, 190, 255), 4.0f, 0, 2.0f);
   }

   // A comment shows its note on the face of the node, not behind the params
   // (eye) toggle - the whole point of a note is to be readable without an
   // extra click, the same reasoning DrawNode's canvas is drawn directly
   // rather than collapsed.
   //
   // The text is painted into the node's draw list rather than sitting in an
   // InputTextMultiline, which cannot go here at all: a multiline field is an
   // ImGui child window, and a child window is the one thing the canvas
   // transform cannot carry, so the note came out at the canvas origin instead
   // of on the node. Double-click opens the real editor as a popup, out in
   // screen space, exactly like the colour picker and the dropdowns.
   void DrawCommentPreview(CommentNode* n)
   {
      const float w = std::max(120.0f, n->width);
      const float h = std::max(60.0f, n->height);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);

      // Reserved first: this is what the node measures itself against and what
      // the double-click below hit-tests, so it has to be a real item.
      ImGui::Dummy(ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      dl->AddRectFilled(origin, br,
                        IM_COL32((int)(n->color[0] * 40), (int)(n->color[1] * 40),
                                 (int)(n->color[2] * 40), 255), 4.0f);
      dl->AddRect(origin, br,
                  IM_COL32((int)(n->color[0] * 255), (int)(n->color[1] * 255),
                           (int)(n->color[2] * 255), 200), 4.0f, 0, 1.5f);

      const ImU32 textCol = IM_COL32((int)((n->color[0] * 0.6f + 0.4f) * 255),
                                     (int)((n->color[1] * 0.6f + 0.4f) * 255),
                                     (int)((n->color[2] * 0.6f + 0.4f) * 255), 255);
      // Clipped to the box so a note longer than its height is cut off at the
      // edge instead of spilling over the params below it.
      dl->PushClipRect(origin, br, true);
      if (n->text.empty())
         dl->AddText(ImVec2(origin.x + 8, origin.y + 6), IM_COL32(150, 150, 160, 255),
                     "double-click to write");
      else
         dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                     ImVec2(origin.x + 8, origin.y + 6), textCol,
                     n->text.c_str(), nullptr, w - 16.0f);
      dl->PopClipRect();

      {
         const ImVec2 tl = ed::CanvasToScreen(origin);
         const ImVec2 rb = ed::CanvasToScreen(br);
         gCommentBodyRect = ImVec4(tl.x, tl.y, rb.x - tl.x, rb.y - tl.y);
         if (n == gCommentEdit.target)
            gCommentEditRect = gCommentBodyRect;
      }

      if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint(); // before the edit, so one undo restores the old note
         gCommentEdit.target = n;
         gCommentEdit.justOpened = true;
      }
   }

   void DrawCommentParams(CommentNode* n)
   {
      ModSlider("width", &n->width, 120.0f, 800.0f, "%.0f");
      ModSlider("height", &n->height, 60.0f, 600.0f, "%.0f");
      ColorSwatch("colour", n->color, n);
   }

   // Padding kept between a group's members and the edge of its box.
   const float kGroupPadding = 24.0f;

   // Which group, if any, currently owns a node. Membership is exclusive: a
   // node belongs to at most one group, which is what stops two groups from
   // both auto-fitting around the same nodes and ending up drawn inside one
   // another.
   GroupNode* GroupOwning(int nodeIndex)
   {
      for (const auto& entry : gGroupMembers)
      {
         if (entry.second.count(nodeIndex) != 0)
            return entry.first;
      }
      return nullptr;
   }

   // The gNodes index a GroupNode* lives at, or -1 if it is not (or no longer)
   // in the graph. Used to translate group ownership into an index that
   // survives being carried around in a clipboard/duplicate item list.
   int IndexOfGroupNode(GroupNode* g)
   {
      if (g == nullptr)
         return -1;
      for (const GraphNode& gn : gNodes)
      {
         if (gn.node.get() == g)
            return gn.index;
      }
      return -1;
   }

   // How far to shift a duplicated/pasted cluster so the copy reads as a
   // separate thing directly below the original, rather than sitting on
   // top of it. A flat 40px works for a single node, but a whole group can
   // span hundreds of pixels - the same flat offset there leaves the copy's
   // box nearly fully overlapping the original. Shifting straight down by
   // the cluster's own height (plus a margin) clears it in one predictable
   // direction, landing right underneath rather than off at a diagonal.
   ImVec2 ClusterOffset(const std::set<int>& indices)
   {
      bool any = false;
      ImVec2 bmin(0.0f, 0.0f), bmax(0.0f, 0.0f);
      for (int index : indices)
      {
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
            continue;
         const ImVec2 p = ed::GetNodePosition(gn->NodeId());
         const ImVec2 s = ed::GetNodeSize(gn->NodeId());
         if (!any)
         {
            bmin = p; bmax = ImVec2(p.x + s.x, p.y + s.y); any = true;
         }
         else
         {
            bmin.x = std::min(bmin.x, p.x);
            bmin.y = std::min(bmin.y, p.y);
            bmax.x = std::max(bmax.x, p.x + s.x);
            bmax.y = std::max(bmax.y, p.y + s.y);
         }
      }
      if (!any)
         return ImVec2(0.0f, 40.0f);
      const float kMargin = 60.0f;
      return ImVec2(0.0f, (bmax.y - bmin.y) + kMargin);
   }

   // Drops membership sets whose group no longer exists. Deleting a node goes
   // through RemoveNodeByIndex, which cleans up as it goes, but undo/redo
   // rebuilds gNodes wholesale without ever calling it - and a stale
   // GroupNode* key is not merely wasted memory, it is an address the
   // allocator can hand back to a brand new group, which would then inherit a
   // stranger's member list.
   void PruneDeadGroups()
   {
      std::set<GroupNode*> live;
      for (GraphNode& gn : gNodes)
      {
         if (auto* g = dynamic_cast<GroupNode*>(gn.node.get()))
            live.insert(g);
      }
      for (auto it = gGroupMembers.begin(); it != gGroupMembers.end(); )
         it = (live.count(it->first) != 0) ? std::next(it) : gGroupMembers.erase(it);
   }

   // Fits a group's box to exactly its members' bounding box plus padding,
   // every frame, in both directions - so dragging a node towards the edge
   // stretches the box and dragging it back shrinks the box down again.
   //
   // Because the box is fully derived from the member set, a manual resize
   // does not survive as a size; what it does instead is decide membership.
   // Dragging an edge out over a loose node adopts that node on the next
   // frame, and the box then refits around the enlarged set - so resizing
   // still reads as "reach out and grab that one too", which is the only
   // thing resizing a group is ever really for.
   //
   // Runs before the group draws itself, using last frame's box (this node's
   // position plus the width/height/headerH already synced onto n) against
   // every other node's current position. Drag deltas for the frame are
   // applied before this per-node loop runs, so every position read here is
   // this frame's live one.
   void AutoFitGroupToMembers(GraphNode& gn, GroupNode* n)
   {
      const ImVec2 nodePos = ed::GetNodePosition(gn.NodeId());
      const ImVec2 boxMin(nodePos.x, nodePos.y + n->headerH);
      const ImVec2 boxMax(boxMin.x + n->width, boxMin.y + n->height);

      std::set<int>& members = gGroupMembers[n];

      // Adopt anything now fully inside the box. A group never adopts another
      // group (nesting is not supported), nor a node another group already
      // owns.
      for (GraphNode& other : gNodes)
      {
         if (other.index == gn.index || dynamic_cast<GroupNode*>(other.node.get()) != nullptr)
            continue;
         GroupNode* owner = GroupOwning(other.index);
         if (owner != nullptr && owner != n)
            continue;
         const ImVec2 p = ed::GetNodePosition(other.NodeId());
         const ImVec2 s = ed::GetNodeSize(other.NodeId());
         if (p.x >= boxMin.x && p.y >= boxMin.y && p.x + s.x <= boxMax.x && p.y + s.y <= boxMax.y)
            members.insert(other.index);
      }

      // Union the members' bounds, dropping any whose node has been deleted.
      ImVec2 fitMin(0.0f, 0.0f), fitMax(0.0f, 0.0f);
      bool any = false;
      for (auto it = members.begin(); it != members.end(); )
      {
         GraphNode* member = FindNodeByIndex(*it);
         if (member == nullptr)
         {
            it = members.erase(it);
            continue;
         }
         const ImVec2 p = ed::GetNodePosition(member->NodeId());
         const ImVec2 s = ed::GetNodeSize(member->NodeId());
         if (!any)
         {
            fitMin = p;
            fitMax = ImVec2(p.x + s.x, p.y + s.y);
            any = true;
         }
         else
         {
            fitMin.x = std::min(fitMin.x, p.x);
            fitMin.y = std::min(fitMin.y, p.y);
            fitMax.x = std::max(fitMax.x, p.x + s.x);
            fitMax.y = std::max(fitMax.y, p.y + s.y);
         }
         ++it;
      }

      // An empty group has nothing to fit to, so it keeps whatever size the
      // user gave it - otherwise it would collapse the moment it was spawned
      // and there would be no box left to drag over anything.
      if (!any)
         return;

      fitMin.x -= kGroupPadding; fitMin.y -= kGroupPadding;
      fitMax.x += kGroupPadding; fitMax.y += kGroupPadding;

      const ImVec2 size(fitMax.x - fitMin.x, fitMax.y - fitMin.y);
      // Both setters no-op when the value already matches, so this does not
      // mark the patch dirty every frame just by sitting there.
      ed::SetNodePosition(gn.NodeId(), ImVec2(fitMin.x, fitMin.y - n->headerH));
      ed::SetGroupSize(gn.NodeId(), size);
      n->width = size.x;
      n->height = size.y;
   }

   // Drawn as an ed::Group() rather than through the normal pin/param flow: a
   // group has no image in or out, and it needs the library's own notion of
   // "group" (a node the editor drags its geometric contents along with) to
   // get the stick-together behavior at all. The label sits above the
   // resizable box as the node's only ordinary content, so it becomes the
   // group's Header region - the part the editor lets you drag by.
   //
   // The header is plain text, not a text field: an ImGui widget spanning
   // most of the header would capture clicks itself (the same reason a
   // slider inside an ordinary node's body doesn't drag the node), leaving
   // almost nowhere on the header for the editor's own drag-to-move to ever
   // trigger. Renaming instead happens in-place on double-click.
   //
   // ed::Group(size) only honors `size` the first frame a node becomes a
   // group; after that the library tracks the user's own resize (and our own
   // growth, via SetGroupSize) internally and ignores whatever we pass. So
   // width/height here exist purely to seed that first frame and to survive
   // save/load - every subsequent frame they are overwritten from the node's
   // actual measured size, the same pattern liveX/liveY use for position.
   void DrawGroupNode(GraphNode& gn, GroupNode* n)
   {
      AutoFitGroupToMembers(gn, n);

      ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(0, 0, 0, 0));
      ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0, 0, 0, 0));
      ed::PushStyleColor(ed::StyleColor_GroupBg,
                         ImColor(n->color[0], n->color[1], n->color[2], 0.10f));
      ed::PushStyleColor(ed::StyleColor_GroupBorder,
                         ImColor(n->color[0], n->color[1], n->color[2], 0.85f));

      ed::BeginNode(gn.NodeId());
      ImGui::PushID(gn.index);
      ImGui::BeginGroup();
      if (ImGui::ColorButton("##groupcolor", ImVec4(n->color[0], n->color[1], n->color[2], 1.0f),
                             ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                             ImVec2(14, 14)))
      {
         PushUndoCheckpoint();
         gColor.target = n->color;
         gColor.owner = n;
         gColor.label = "colour";
         gColor.justOpened = true;
      }
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(n->color[0] * 0.4f + 0.6f, n->color[1] * 0.4f + 0.6f,
                                   n->color[2] * 0.4f + 0.6f, 1.0f));
      if (n->renaming)
      {
         if (n->renameJustStarted)
         {
            ImGui::SetKeyboardFocusHere();
            n->renameJustStarted = false;
         }
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0.35f));
         ImGui::SetNextItemWidth(std::max(60.0f, n->width - 40.0f));
         if (ImGui::InputText("##grouprename", &n->label, ImGuiInputTextFlags_EnterReturnsTrue) ||
             ImGui::IsItemDeactivated())
            n->renaming = false;
         ImGui::PopStyleColor();
      }
      else
      {
         ImGui::TextUnformatted(n->label.c_str());
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            PushUndoCheckpoint();
            n->renaming = true;
            n->renameJustStarted = true;
         }
      }
      ImGui::PopStyleColor();
      ImGui::EndGroup();
      const ImVec2 headerSize = ImGui::GetItemRectSize();
      ed::Group(ImVec2(n->width, n->height));
      ImGui::PopID();
      ed::EndNode();

      ed::PopStyleColor(4);

      const ImVec2 total = ed::GetNodeSize(gn.NodeId());
      n->width = std::max(120.0f, total.x);
      n->headerH = headerSize.y + ImGui::GetStyle().ItemSpacing.y;
      n->height = std::max(60.0f, total.y - n->headerH);
   }

   void DrawDrawParams(DrawNode* n)
   {
      DropdownButton("brush", DrawNode::BrushNames(), n->brush, [n](int i) { n->brush = i; });
      ModSlider("size", &n->brushSize, 0.002f, 0.5f);
      ModSlider("opacity", &n->opacity, 0.02f, 1.0f);
      ModSlider("hardness", &n->hardness, 0.0f, 0.98f);
      ModSlider("spacing", &n->spacing, 0.02f, 1.0f);
      ModSlider("jitter", &n->jitter, 0.0f, 2.0f);
      ColorSwatch("colour", n->color, n);
      ImGui::Checkbox("eraser", &n->eraser);
      if (ImGui::Button("Clear canvas", ImVec2(kPreviewSize, 0)))
         n->ClearCanvas();

      NodeSeparator("animation");
      if (n->IsRecordingStrokes())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
         if (ImGui::Button("Stop rec", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopRecording();
         ImGui::PopStyleColor();
      }
      else if (ImGui::Button("Rec strokes", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingBack())
      {
         if (ImGui::Button("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPlayback();
      }
      else if (ImGui::Button("Replay", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->PlayRecording();
      }

      if (n->RecordedStamps() > 0)
      {
         ImGui::TextDisabled("%zu marks over %.1f beats", n->RecordedStamps(), n->RecordedLength());
         if (n->IsPlayingBack())
            ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.6f, 1.0f), "playhead %.1f", n->PlayheadBeats());
      }
      ImGui::Checkbox("loop replay", &n->loopPlayback);
      ModSlider("replay speed", &n->playSpeed, 0.1f, 4.0f);
      if (ImGui::SmallButton("clear recording"))
         n->ClearRecording();
      ModSlider("canvas w", &n->canvasWidth, 64.0f, 4096.0f, "%.0f");
      ModSlider("canvas h", &n->canvasHeight, 64.0f, 4096.0f, "%.0f");
   }

   // Square, letterboxed preview so non-square sources still read 1:1.
   void DrawPreview(INode* node)
   {
      unsigned int tex = node->GetOutputTexture();
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // Render 3D gets a working viewport rather than a thumbnail: it is the
      // one preview you orbit and frame a scene in. Viewport nodes are asking
      // for the same thing explicitly.
      auto* render = dynamic_cast<Render3DNode*>(node);
      const bool wantsBigCanvas =
         render != nullptr || dynamic_cast<ViewportNode*>(node) != nullptr;
      const float size = wantsBigCanvas ? kViewportSize : kPreviewSize;

      dl->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size),
                        IM_COL32(18, 18, 24, 255), 4.0f);

      if (tex != 0 && node->GetOutputWidth() > 0)
      {
         float w = (float)node->GetOutputWidth();
         float h = (float)node->GetOutputHeight();
         float scale = size / std::max(w, h);
         float dw = w * scale;
         float dh = h * scale;
         ImVec2 tl(origin.x + (size - dw) * 0.5f, origin.y + (size - dh) * 0.5f);
         dl->AddImage((ImTextureID)(intptr_t)tex, tl, ImVec2(tl.x + dw, tl.y + dh),
                      ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 10, origin.y + size * 0.5f - 8),
                     IM_COL32(120, 120, 135, 255), "no input");
      }

      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                  IM_COL32(70, 74, 90, 255), 4.0f);

      // A Render 3D preview is a viewport, not a picture: drag to orbit, scroll
      // to zoom. An InvisibleButton is what makes this safe inside the node
      // editor - while it is active the editor leaves the drag alone, which is
      // the same mechanism the in-node sliders already rely on.
      if (render == nullptr)
      {
         ImGui::Dummy(ImVec2(size, size));
         return;
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##viewport", ImVec2(size, size));

      // A patched Camera node owns the view, so drive that instead of the
      // built-in values; otherwise the drag would appear to do nothing.
      float* azimuth = render->camera ? &render->camera->azimuth : &render->camAzimuth;
      float* elevation = render->camera ? &render->camera->elevation : &render->camElevation;
      float* distance = render->camera ? &render->camera->distance : &render->camDistance;

      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      {
         const ImVec2 drag = ImGui::GetIO().MouseDelta;
         // azimuth/elevation are stored in degrees; 0.6875 deg/px matches the
         // drag feel of the old 0.012 rad/px (0.012 * 180/pi).
         *azimuth -= drag.x * 0.6875f;
         // Clamped just short of the poles: straight overhead makes the view
         // matrix's up vector parallel to the view direction and the image rolls.
         *elevation = std::max(-85.9437f, std::min(*elevation + drag.y * 0.6875f, 85.9437f));
      }

      if (ImGui::IsItemHovered())
      {
         ImGuiIO& vio = ImGui::GetIO();
         if (vio.MouseWheel != 0.0f)
         {
            *distance = std::max(0.2f, std::min(*distance * (1.0f - vio.MouseWheel * 0.15f), 60.0f));
            // Consumed, or the canvas zooms at the same time as the camera.
            vio.MouseWheel = 0.0f;
         }
      }

      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                     IM_COL32(120, 200, 255, 200), 4.0f, 0, 2.0f);
   }

   // Rolling history so a modulator reads like a scope rather than a number.
   std::map<int, std::vector<float>> gModHistory;

   // Per-node mini 3D viewport GL state, keyed by GraphNode::index - same
   // per-index-map pattern as gModHistory above. Lazily created the first time
   // a node's viewport toggle is switched on; nothing is allocated for a node
   // that never opts in.
   std::map<int, NodeViewport> gNodeViewports;

   // Nodes/viewports retired by RemoveNodeByIndex this frame, held until the
   // start of the next frame - see RemoveNodeByIndex for why teardown can't
   // happen synchronously. NodeViewport has a user-declared destructor (which
   // suppresses its implicit move ctor), so it can't live in a
   // vector<NodeViewport> without a copy that would double-free its GL
   // texture; map::node_type from extract() moves the whole tree node instead
   // of the mapped value, sidestepping that.
   std::vector<std::unique_ptr<INode>> gRetiredNodes;
   std::vector<std::map<int, NodeViewport>::node_type> gRetiredViewports;

   // A geometry-producing node's own solo render, independent of whatever a
   // downstream Render 3D shows - the point is seeing what *this* node
   // produced (e.g. what a Select actually selected) without having to wire
   // it all the way to the end of the graph. Off by default per node
   // (GraphNode::showMiniViewport); only drawn/rendered at all when a node
   // opts in, so patches that never touch the toggle pay nothing for it.
   void DrawMiniViewport(GraphNode& gn, IGeometrySource* geo)
   {
      NodeViewport& viewport = gNodeViewports[gn.index];

      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size),
                        IM_COL32(18, 18, 24, 255), 4.0f);

      const unsigned int tex = viewport.Render(geo, (int)size, (int)size);
      if (tex != 0)
      {
         dl->AddImage((ImTextureID)(intptr_t)tex, origin, ImVec2(origin.x + size, origin.y + size),
                      ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 10, origin.y + size * 0.5f - 8),
                     IM_COL32(120, 120, 135, 255), "no geometry");
      }
      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                  IM_COL32(70, 74, 90, 255), 4.0f);

      // Same drag-to-orbit / scroll-to-zoom feel as DrawPreview's embedded
      // Render3DNode viewport, driving this node's own NodeViewport camera
      // instead.
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##miniviewportcanvas", ImVec2(size, size));

      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      {
         const ImVec2 drag = ImGui::GetIO().MouseDelta;
         viewport.Orbit(drag.x * 0.012f, drag.y * 0.012f);
      }
      if (ImGui::IsItemHovered())
      {
         ImGuiIO& vio = ImGui::GetIO();
         if (vio.MouseWheel != 0.0f)
         {
            viewport.Zoom(vio.MouseWheel);
            vio.MouseWheel = 0.0f;
         }
      }
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                     IM_COL32(120, 200, 255, 200), 4.0f, 0, 2.0f);
   }

   // Viewport-panel 3D renders, kept in their own map rather than sharing
   // gNodeViewports with the inline per-node toggle above: the two draw at
   // different sizes, and one NodeViewport reallocates its FBO whenever the
   // requested size changes - sharing would make a node that is open in both
   // places thrash its FBO every frame.
   std::map<int, NodeViewport> gPanelViewports;

   // One node's card inside the viewport panel: a title row and the render,
   // nothing else. Deliberately non-interactive - unlike the inline
   // mini-viewport, there is no drag-to-orbit or scroll-to-zoom here, so
   // dragging across the panel can never knock a render askew. imageSize is
   // the exact box the render fills, so the card never has leftover space for
   // a scrollbar to appear in.
   // Whether a node has anything the viewport panel could actually show.
   // Mirrors the branches DrawPreview's dispatch takes in the node body: a
   // modulator is a value with a scope trace, not an image; Camera/Light show
   // a stat box and implement no geometry interface at all; a Comment is
   // text. All of those would render as a permanent "no input" box, so they
   // don't get offered the menu item in the first place.
   bool CanShowInViewportPanel(const GraphNode& gn)
   {
      INode* n = gn.node.get();
      if (dynamic_cast<IGeometrySource*>(n) != nullptr)
         return true; // meshes render as their own solo viewport
      if (dynamic_cast<IModulator*>(n) != nullptr)
         return false;
      // The multi-output modulators, which draw their meters in the params
      // panel rather than producing an image.
      if (dynamic_cast<ImageAnalyzeNode*>(n) != nullptr ||
          dynamic_cast<AudioFileNode*>(n) != nullptr ||
          dynamic_cast<AudioAnalyzeNode*>(n) != nullptr)
         return false;
      if (dynamic_cast<CameraNode*>(n) != nullptr || dynamic_cast<LightNode*>(n) != nullptr)
         return false;
      if (dynamic_cast<CommentNode*>(n) != nullptr || dynamic_cast<GroupNode*>(n) != nullptr)
         return false;
      return true;
   }

   // The dock picker, shared by the panel's own header and the Menu dropdown
   // so the two can't drift apart.
   void ViewportPanelDockCombo()
   {
      static const char* kDockLabels[] = { "Bottom", "Right", "Left", "Top" };
      if (ImGui::BeginCombo("##viewportdock", kDockLabels[gViewportPanelDock]))
      {
         for (int i = 0; i < 4; i++)
            if (ImGui::Selectable(kDockLabels[i], i == gViewportPanelDock))
               gViewportPanelDock = i;
         ImGui::EndCombo();
      }
   }

   // The title row's exact height: SmallButton is a text-height control (it
   // zeroes FramePadding.y), so this is what the row above the render costs.
   // Card and container both size against it, or the render overflows its
   // card by a few pixels and gets clipped.
   float ViewportPanelTitleHeight()
   {
      return ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
   }

   void DrawViewportPanelCard(GraphNode& gn, const ImVec2& imageSize)
   {
      const float titleH = ViewportPanelTitleHeight();
      char childId[32];
      snprintf(childId, sizeof(childId), "##viewportcard%d", gn.index);
      ImGui::BeginChild(childId, ImVec2(imageSize.x, imageSize.y + titleH), false,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

      const bool closeRequested = ImGui::SmallButton("X");
      ImGui::SameLine();
      ImGui::TextUnformatted(NodeTitle(gn).c_str());

      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + imageSize.x, origin.y + imageSize.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, br, IM_COL32(18, 18, 24, 255), 4.0f);

      if (auto* geo = dynamic_cast<IGeometrySource*>(gn.node.get()))
      {
         // Same solo mesh render as the inline mini viewport, minus its
         // orbit/zoom input handling.
         const unsigned int tex =
            gPanelViewports[gn.index].Render(geo, (int)imageSize.x, (int)imageSize.y);
         if (tex != 0)
            dl->AddImage((ImTextureID)(intptr_t)tex, origin, br, ImVec2(0, 1), ImVec2(1, 0));
         else
            dl->AddText(ImVec2(origin.x + 10, origin.y + imageSize.y * 0.5f - 8),
                        IM_COL32(120, 120, 135, 255), "no geometry");
      }
      else
      {
         // Everything else: blit the node's output texture, letterboxed to
         // its own aspect, mirroring DrawPreview's blit/"no input" fallback.
         const unsigned int tex = gn.node->GetOutputTexture();
         if (tex != 0 && gn.node->GetOutputWidth() > 0)
         {
            const float w = (float)gn.node->GetOutputWidth();
            const float h = (float)gn.node->GetOutputHeight();
            const float scale = std::min(imageSize.x / w, imageSize.y / h);
            const float dw = w * scale;
            const float dh = h * scale;
            const ImVec2 tl(origin.x + (imageSize.x - dw) * 0.5f,
                            origin.y + (imageSize.y - dh) * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)tex, tl, ImVec2(tl.x + dw, tl.y + dh),
                         ImVec2(0, 1), ImVec2(1, 0));
         }
         else
         {
            dl->AddText(ImVec2(origin.x + 10, origin.y + imageSize.y * 0.5f - 8),
                        IM_COL32(120, 120, 135, 255), "no input");
         }
      }

      ImGui::Dummy(imageSize);
      ImGui::EndChild();

      if (closeRequested)
      {
         gViewportPanelNodes.erase(
            std::remove(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), gn.index),
            gViewportPanelNodes.end());
         // Note: this node's NodeViewport is NOT destroyed here - see the
         // deferred prune in the frame loop. Its texture has already been
         // submitted to this frame's draw list.
      }
   }

   // The dockable global viewport panel (see gViewportPanelNodes), opened via
   // a node's right-click menu -> "Open in viewport panel". Unlike the inline
   // per-node preview/mini-viewport above, this holds any number of nodes at
   // once - stacked left-to-right when docked top/bottom, or top-to-bottom
   // when docked left/right - in a single shared strip.
   //
   // Every render is sized to exactly fill the strip's short axis, so cards
   // butt up against each other with no dead space between them, and no card
   // ever has room left over for a scrollbar of its own.
   void DrawViewportPanelContainer()
   {
      const bool horizontal = (gViewportPanelDock == 0 || gViewportPanelDock == 3);

      // The render's fixed axis: the strip's height when cards run across it,
      // its width when they run down it. Squared off, since a mesh render has
      // no aspect of its own and an image is letterboxed into it anyway.
      //
      // Measured HERE, outside the scrolling child, and with the scroll
      // axis's scrollbar reserved unconditionally. Both matter: sizing cards
      // from the region *inside* the scrolling child feeds back on itself -
      // enough cards make a scrollbar appear, which shrinks the region, which
      // shrinks the cards, which can drop the total back under the scroll
      // threshold, which removes the scrollbar again. That loop is what makes
      // the panel flicker once a certain number of cards is open.
      const float bar = ImGui::GetStyle().ScrollbarSize;
      const ImVec2 panelOrigin = ImGui::GetCursorScreenPos();
      const ImVec2 strip = ImGui::GetContentRegionAvail();
      const float box = horizontal ? std::max(48.0f, strip.y - bar)
                                   : std::max(48.0f, strip.x - bar);

      ImGui::BeginChild("##viewportcards", strip, false,
                        horizontal ? ImGuiWindowFlags_HorizontalScrollbar : 0);

      // Snapshot before drawing: a card's own close button mutates
      // gViewportPanelNodes, which would otherwise invalidate this loop.
      const std::vector<int> nodes = gViewportPanelNodes;
      bool first = true;
      for (int idx : nodes)
      {
         GraphNode* gn = FindNodeByIndex(idx);
         if (gn == nullptr)
         {
            // Stale - the node was deleted, or undo/redo rewound past it.
            // Drop silently rather than show stale content.
            gViewportPanelNodes.erase(
               std::remove(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx),
               gViewportPanelNodes.end());
            continue;
         }

         if (!first && horizontal)
            ImGui::SameLine();
         first = false;

         DrawViewportPanelCard(*gn, ImVec2(box, box));
      }

      ImGui::EndChild();

      // Right-click anywhere on the panel - empty space or a card - to
      // reposition or close it. Replaces the dock-combo/close-button header
      // row, which cost a whole line of chrome for controls used rarely.
      //
      // A plain rect test against the mouse, not BeginPopupContextWindow:
      // the cards above are nested child windows, and ImGui's window-hover
      // test only sees the topmost window under the cursor, so a
      // context-window helper attached here would only fire in the gaps
      // between cards, not over the renders themselves.
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const bool overPanel = mouse.x >= panelOrigin.x && mouse.x < panelOrigin.x + strip.x &&
                             mouse.y >= panelOrigin.y && mouse.y < panelOrigin.y + strip.y;
      if (overPanel && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
         ImGui::OpenPopup("##viewportpanelctx");

      if (ImGui::BeginPopup("##viewportpanelctx"))
      {
         static const char* kDockLabels[] = { "Bottom", "Right", "Left", "Top" };
         for (int i = 0; i < 4; i++)
            if (ImGui::MenuItem(kDockLabels[i], nullptr, i == gViewportPanelDock))
               gViewportPanelDock = i;
         ImGui::Separator();
         if (ImGui::MenuItem("Close panel"))
            gViewportPanelNodes.clear();
         ImGui::EndPopup();
      }
   }

   // The panel's outer frame at every dock position: a borderless child plus
   // exactly one hairline along the canvas-facing edge. One line is all the
   // separation this needs - a full border here (and another around every
   // card inside it) just stacked up rules for no extra information.
   //
   // That same canvas-facing edge is the panel's resize handle: a thin strip
   // reserved inside the panel, so it belongs to the panel's own window and
   // reliably takes the hover (a strip drawn just outside would be over the
   // node canvas, whose window would win the hover instead).
   void DrawViewportPanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = 6.0f;
      const int dock = gViewportPanelDock;
      const bool vertical = (dock == 1 || dock == 2);  // grip is a column, not a row
      const bool gripFirst = (dock == 0 || dock == 1); // canvas is above / to the left

      ImGui::BeginChild(id, size, false);
      const ImVec2 inner = ImGui::GetContentRegionAvail();

      auto grip = [&]()
      {
         ImGui::InvisibleButton("##viewportgrip",
                                vertical ? ImVec2(kGrip, std::max(1.0f, inner.y))
                                         : ImVec2(std::max(1.0f, inner.x), kGrip));
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsItemActive())
         {
            // Each dock grows toward the canvas, so the sign flips with the
            // side the panel is on.
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            switch (dock)
            {
               case 0: gViewportPanelHeight -= d.y; break;
               case 1: gViewportPanelWidth -= d.x; break;
               case 2: gViewportPanelWidth += d.x; break;
               default: gViewportPanelHeight += d.y; break;
            }
            gViewportPanelWidth = std::max(kViewportPanelMinWidth, gViewportPanelWidth);
            gViewportPanelHeight = std::max(kViewportPanelMinHeight, gViewportPanelHeight);
         }
      };

      if (gripFirst)
      {
         grip();
         if (vertical)
            ImGui::SameLine();
      }

      // The grip and the content sit on one layout axis with ImGui's item
      // spacing between them, so the content has to give up that spacing as
      // well as the grip itself - otherwise the two together overrun the
      // panel and the panel grows a scrollbar of its own.
      const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
      ImGui::BeginChild("##viewportpanelcontent",
                        vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), 0)
                                 : ImVec2(0, std::max(1.0f, inner.y - kGrip - gap.y)),
                        false);
      DrawViewportPanelContainer();
      ImGui::EndChild();

      if (!gripFirst)
      {
         if (vertical)
            ImGui::SameLine();
         grip();
      }
      ImGui::EndChild();

      const ImVec2 tl = ImGui::GetItemRectMin();
      const ImVec2 br = ImGui::GetItemRectMax();
      const ImU32 line = IM_COL32(70, 74, 90, 255);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      switch (dock)
      {
         case 0: dl->AddLine(tl, ImVec2(br.x, tl.y), line); break;          // bottom dock -> top edge
         case 1: dl->AddLine(tl, ImVec2(tl.x, br.y), line); break;          // right dock -> left edge
         case 2: dl->AddLine(ImVec2(br.x, tl.y), br, line); break;          // left dock -> right edge
         default: dl->AddLine(ImVec2(tl.x, br.y), br, line); break;         // top dock -> bottom edge
      }
   }

   void DrawModulatorMeter(IModulator* mod, int nodeIndex)
   {
      const float value = mod->Value01();
      std::vector<float>& history = gModHistory[nodeIndex];
      history.push_back(value);
      if (history.size() > 160)
         history.erase(history.begin());

      ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 90.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                        IM_COL32(18, 18, 24, 255), 4.0f);

      for (size_t i = 1; i < history.size(); i++)
      {
         float x0 = origin.x + kPreviewSize * (float)(i - 1) / 160.0f;
         float x1 = origin.x + kPreviewSize * (float)i / 160.0f;
         float y0 = origin.y + h - history[i - 1] * h;
         float y1 = origin.y + h - history[i] * h;
         dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 190, 90, 255), 1.6f);
      }

      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                  IM_COL32(70, 74, 90, 255), 4.0f);
      ImGui::Dummy(ImVec2(kPreviewSize, h));
      ImGui::Text("%.3f", value);
   }

   void DisconnectLinkById(int id)
   {
      const LinkInfo* link = FindLink(id);
      if (link == nullptr)
         return;

      const int dstPin = link->dstPin;
      GraphNode* dst = FindNodeByIndex(GraphNode::NodeIndexFromPin(dstPin));
      if (dst == nullptr)
         return;

      if (GraphNode::IsParamPin(dstPin))
      {
         Modulation::Instance().Unbind(dst->index, GraphNode::ParamIndexFromPin(dstPin));
      }
      else if (GraphNode::IsColorPin(dstPin))
      {
         PaletteBinding::Instance().Unbind(dst->index, GraphNode::ColorIndexFromPin(dstPin));
      }
      else if (auto* render = dynamic_cast<Render3DNode*>(dst->node.get()))
      {
         const int slot = GraphNode::InputSlotFromPin(dstPin);
         if (slot >= 0 && slot < Render3DNode::kSlots)
         {
            render->geometry[slot] = nullptr;
            render->clouds[slot] = nullptr;
         }
         else if (slot == Render3DNode::kSlots)
            render->camera = nullptr;
         else if (slot == Render3DNode::kEnvSlot)
            render->envInput.Disconnect();
         else if (slot > Render3DNode::kSlots && slot < Render3DNode::kEnvSlot)
            render->lights[slot - Render3DNode::kSlots - 1] = nullptr;
      }
      else if (auto* geoOp = dynamic_cast<GeometryOpNode*>(dst->node.get()))
      {
         geoOp->input = nullptr;
      }
      else if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(dst->node.get()))
      {
         if (GraphNode::InputSlotFromPin(dstPin) == 0)
            inst->pointSource = nullptr;
         else
            inst->instanceShape = nullptr;
      }
      else if (auto* sw3 = dynamic_cast<Switcher3DNode*>(dst->node.get()))
      {
         const int slot = GraphNode::InputSlotFromPin(dstPin);
         if (slot >= 0 && slot < Switcher3DNode::kSlots)
            sw3->inputs[slot] = nullptr;
      }
      else if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(dst->node.get()))
      {
         audio->fileSource = nullptr;
      }
      else if (auto* math = dynamic_cast<MathNode*>(dst->node.get()))
      {
         if (GraphNode::InputSlotFromPin(dstPin) == 0)
            math->inputA = nullptr;
         else
            math->inputB = nullptr;
      }
      else if (ImageCable* cable = CableFor(*dst, GraphNode::InputSlotFromPin(dstPin)))
      {
         cable->Disconnect();
      }
   }

   // Per-node "Help" popup text, keyed by the exact registered typeName (the
   // same string GraphNode::typeName holds - see REGISTER_NODE / FilterDef::name).
   // This is where usage notes, wiring conventions and gotchas live now, rather
   // than as permanent text drawn on the node body: a node you already know how
   // to use shouldn't have to carry its own manual around forever.
   const char* SpecificNodeHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         // ---------------- Source / Text ----------------
         { "Image Source", "Loads a still image. Opens the native file picker and decodes anything macOS can read - PNG, JPEG, TIFF, HEIC, RAW and more." },
         { "Video", "Plays a video file. Position follows the transport, so it pauses with everything else. Loop and speed (including reverse) are available." },
         { "Noise", "Procedural noise: value, fBm, ridged, Voronoi, Worley edges and white. Domain warping, octaves and colour mapping included." },
         { "Shape", "The base 2D vector-primitive node - pick any of its 20 shapes from the dropdown, with fill, stroke, feather and background controls. Each shape also has its own directly-spawnable named node (Circle, Hexagon, Star, ...) that just starts on that shape." },
         { "Draw", "Paint straight onto the node preview. Six procedural brushes, eraser, spacing and jitter. Patch an image in to paint over it. Record, then draw - replaying redraws the stroke in time, and the canvas size follows the input when one is patched in." },
         { "Formula", "A live GLSL shader. Pick a preset or press 'Edit GLSL...' to write your own; four knobs (uA-uD) are exposed for modulation." },
         { "Texture", "Blender-standard procedural textures: Voronoi, Brick, Magic, Wave and Musgrave, each with its own parameter block." },
         { "Ramp", "Generates a gradient from scratch (no input) between up to 8 user-set colour stops, at a chosen angle, scale and offset. Gamma and dither smooth out visible banding." },
         { "Text", "Renders text using any font installed on the system, with size, colour, tracking, alignment and position." },

         // ---------------- Effects (built-in, non-filter-table) ----------------
         { "Curves", "Shadow / midtone / highlight lift plus an S-curve control, per RGB channel or all together. Drag points on the curve to bend it, click empty curve to add a point, right-click a point to remove it." },

         // ---------------- Color ----------------
         { "Color Ramp", "Recolors any 0-1 grayscale input through user-authored stops, up to 32 of them, with linear or constant interpolation. Unlike Gradient Map, it has no shape of its own - the shape comes from upstream." },

         // ---------------- Compositing ----------------
         { "Blend", "Two inputs and 31 blend modes - the full Normal / Multiply / Screen / Overlay / Hue / Saturation / Colour / Luminosity set, plus Erase." },
         { "Layer Stack", "Four inputs stacked bottom-up: A is the base, D sits on top. Each layer has its own blend mode and opacity, and dragging a layer header reorders the whole layer." },
         { "Switcher", "Cycles between its connected inputs every N beats or seconds, with an optional crossfade. Can be pinned to one input with 'manual'." },
         { "Fit", "Resamples an input to a chosen resolution. Fit letterboxes, Fill crops, Stretch ignores aspect, Native passes through. Use it to make differently-sized sources composite predictably." },
         { "Comment", "A free-floating note on the canvas - has no image input or output, just text. Double-click to edit." },
         { "Group", "A resizable box that owns whatever nodes are geometrically inside it - drag it and its members move together, right-click > Ungroup dissolves it (or ungroup one member from its own context menu)." },
         { "Null", "A pass-through node: its output is exactly its input, unchanged. Useful as a stable junction point to branch a cable to several destinations, or as a placeholder while rewiring." },
         { "Viewport", "Shows its input at actual pixel size in its own resizable window, separate from the small node preview - useful for judging detail without zooming the whole canvas." },

         // ---------------- Modulators ----------------
         { "LFO", "Sine, triangle, saw up/down, square and sample-and-hold. Rate in beats, plus phase and an output range." },
         { "Random", "A new random value every N beats, with adjustable smoothing between steps. Deterministic, so rewinding replays the same sequence." },
         { "Pattern", "An eight-step sequencer. Set the eight sliders, choose how many steps to use, and it loops through them one step every N beats. Optional glide between steps." },
         { "Math", "Combines two modulators - add, subtract, multiply, divide, min, max, average, difference - with gain and offset. Unpatched inputs fall back to a constant, shown as an editable slider; a patched input just shows as 'patched'." },
         { "Macro Knob", "A single named slider (0-1, with a response curve and invert) meant to be patched out to several other sliders at once - one control that fans out to many parameters." },
         { "Macro XY", "A 2D pad exposing X and Y as two separate modulator outputs from one drag. The pad's path can be recorded, looped and replayed in time, like Resynthesize's orb." },
         { "Path", "Outputs a moving 3D point (X/Y/Z, each patchable separately) travelling around a built-in shape (circle, helix, spiral, lissajous, etc.) at a beat-synced speed, or along the points of a patched-in geometry/curve source instead." },
         { "Constant", "Outputs one fixed number - the simplest possible modulator, useful for feeding a Math input or a modulation slot that expects a cable rather than manual control." },
         { "Palette", "Samples colours from a reference image, loaded here or patched in - a patched cable overrides the loaded file, but the file is kept so unplugging falls back to it. Drag its 'out' onto the square dot beside any colour swatch to bind it - each new cable takes the next swatch, and clicking a bound swatch steps it. Its image output is a gradient of the palette." },
         { "Audio File", "Loads an audio file for playback and Audio Analyze to read. Keeps analysing even while muted - the 'audible' checkbox only controls monitoring." },
         { "Image Analyze", "Turns an image into control values. Patch any of its outputs into any slider's modulation dot - a video can drive a blur. Sample res readback is rate-limited because it stalls the GPU." },
         { "Audio Analyze", "Extracts level/frequency-band values from an Audio File for modulation." },

         // ---------------- Feedback ----------------
         { "Feedback", "Outputs the previous frame - a one-frame delay, not an effect. It only does something visible inside a loop: patch a downstream node's output back into a Feedback node's input, then use that Feedback output upstream, without the graph recursing. See Menu > Help > 'Using Feedback' for the full patch." },
         { "Trails", "A pre-wired feedback loop: decaying accumulation with drift, zoom, rotation and hue rotation. Reach for this before wiring a Feedback loop by hand." },
         { "Reaction Diffusion", "Gray-Scott chemical simulation, six presets. Needs no input; patch one in and its luminance varies the feed rate so the pattern grows differently through light and dark." },

         // ---------------- Mask ----------------
         { "Remove Background", "On-device segmentation via the OS - no model download, no network, no key. Subject mode needs macOS 14, Person mode macOS 12. Segmentation is expensive, so the mask is computed on this interval rather than every frame; for video, raise the interval or use auto-refresh." },

         // ---------------- Resynth ----------------
         { "Resynthesize", "Each generation reads the previous one, so the image drifts away from the source. The XY pad blends four named mutation effects assigned to its corners - drag the orb to mix them. Randomise re-rolls which four effects sit in the corners, and the orb's path can be recorded, looped and replayed in time." },
         { "Resynthesize 3D", "The mesh equivalent of Resynthesize: each generation applies a weighted mix of geometry operators to the previous one, drifting the shape over time. 'step' advances one generation manually, 'auto step' runs it continuously, 'randomise' re-rolls the seed." },

         // ---------------- Output ----------------
         { "Output", "Terminal node. Shows the final image, exports a PNG, and records an H.264 .mov at a chosen frame rate. Recording captures the cooked output, so what you see is what is written." },

         // ---------------- 3D / geometry pipeline ----------------
         { "Geometry", "The base 3D primitive node - pick any of its 24 shapes from the dropdown. Each shape also has its own directly-spawnable named node (Cube, Sphere, Torus, ...) that just starts on that shape." },
         { "Text 3D", "Extrudes text into a 3D mesh, with depth, bevel and letter tracking, using any font installed on the system." },
         { "Ocean", "A simulated ocean surface mesh - Gerstner-style waves with amplitude, wavelength, steepness, choppiness, direction and octaves for layered swell." },
         { "Material", "Applies shading, colour, metallic/roughness/opacity and emission to a geometry input, plus an optional normal map input (its strength slider only appears once something is patched into it)." },
         { "Displacement", "Displaces a geometry's vertices along their normals using a texture's luminance. Needs both a geometry input and a texture input to do anything." },
         { "Mapping", "Transforms the UV or 3D texture coordinates used by materials/textures downstream - translate, rotate and scale, independent of moving the geometry itself." },
         { "Particle System", "A GPU particle emitter - shape, rate, lifetime, launch speed/spread/direction, gravity/drag/turbulence, and start/end size and colour over each particle's life." },
         { "Cloth", "A cloth simulation over an input mesh - stiffness, iterations, damping, mass and shape retention, plus gravity, wind and optional ground collision. 'Reset' re-drops the cloth from its rest pose." },
         { "Join Geometry", "Combines two or more geometry inputs into one. The boolean modes (Union, Difference, Intersection, etc, each also spawnable as its own named node) need closed, manifold solids to produce a clean result - open surfaces can give garbage." },
         { "Metaballs", "Builds an isosurface (blobby, merging spheres) from a point cloud source, or from a manually-placed set of balls when no cloud is patched in." },
         { "Image to Points", "Converts an image into a 3D point cloud - brightness/depth-source drives per-point depth, with density, threshold, point size and optional colour-from-image." },
         { "Curve", "A generative parametric curve/tube (line, circle, spiral, helix and other presets) extruded into a mesh, with point count, smoothness, spread, height, twist and tube radius/taper controls." },
         { "Mesh to Points", "Samples the input mesh's vertices as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Mesh to Edges", "Samples points along the input mesh's edges as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Mesh to Faces", "Samples each face's centre point of the input mesh as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Instance on Points", "Stamps a shape at every point of a point source, drawn in one instanced call. Input A is the points source (Faces/Edges/Points sampler, a point cloud, etc.), input B is the shape to stamp at each point - both are required before it draws anything." },
         { "Wrap", "Bends or conforms the source input onto the target input. Cylindrical rolls it around one axis at the target's radius with no distortion at all - letterforms, spacing and extrusion depth survive intact, which is what curves 3D text around a sphere or cylinder. Spherical adds the same bend the other way so long text also curves over the poles. Nearest Surface snaps every vertex to the closest point on the target instead: right for conforming a dense mesh to irregular geometry, but it squashes flat text. With a target connected the bend radius follows the target's size, so scaling the target moves the source with it - radius scale tunes it as a multiplier. With no target, radius sets the bend outright." },
         { "Camera", "A 3D viewpoint. Patch it into a Render 3D node's camera input to render from it instead of the default view." },
         { "Light", "A light source for the 3D scene. Render 3D takes up to 3 lights - patch more in and only the first 3 are used." },
         { "HDRI", "Loads an equirectangular .hdr or .exr image and patches into Render 3D's env input, replacing the fixed sky gradient with a real image for the background and for reflections/ambient light. Rotation turns the image around Y; intensity scales it independently of Render 3D's own env intensity. Reflections use the image's own mip chain scaled by roughness as a cheap stand-in for a proper blurred prefilter - very glossy metal will read a little softer than a full IBL renderer would give it. Use this node rather than Image Source for HDRIs - Image Source clamps to 8-bit sRGB, which throws away exactly the above-1.0 highlight range an HDRI needs." },
         { "Render 3D", "Rasterizes the geometry/camera/light/material graph into an image. Antialiasing is reduced automatically at large output sizes to stay within GPU limits. Scenes over ~2 million triangles get noticeably heavier to render. An HDRI node patched into the env input replaces the procedural sky gradient for background, reflections and ambient light." },
         { "Model 3D", "Loads a 3D model file - obj, ply, stl, usd or usdz." },
         { "Null 3D", "A pass-through node for geometry: its output is exactly its input mesh, unchanged. Useful as a stable junction point to branch geometry to several destinations." },
         { "Switcher 3D", "Cycles between up to four connected geometry inputs every N beats or seconds, forwarding whichever one is active. Can be pinned to one input with 'manual'. Unlike the 2D Switcher, there is no crossfade - it always hard-cuts, since interpolating between two arbitrary meshes' topology isn't generally well-defined." },

         // ---------------- Join Geometry boolean modes (also spawnable directly) ----------------
         { "Union", "Boolean union: fuses two or more closed, manifold solids into one, keeping their combined outer surface." },
         { "Intersect", "Boolean intersection: keeps only the volume where all the closed, manifold input solids overlap." },
         { "Difference", "Boolean difference: subtracts every input after the first from the first, like a cookie cutter. Needs closed, manifold input." },

         // ---------------- GeometryOp operators ----------------
         { "Transform", "Moves, rotates and scales the input mesh by a fixed offset - the geometry equivalent of the 2D Transform effect." },
         { "Array", "Duplicates the input mesh in a line or radial ring. Count sets how many copies; radial mode spaces them by Radius, linear mode by Offset X/Y/Z; each copy can additionally rotate/scale a step further than the last." },
         { "Subdivide", "Subdivision surface. Each level roughly quadruples the triangle count, so higher levels get expensive fast; Smooth controls how much it rounds corners." },
         { "Solidify", "Gives a flat or open surface real Thickness, turning it into a solid shell. 'keep original' also retains the source surface." },
         { "Extrude", "Extrudes every face of the input mesh along its own normal by Distance, with Inset shrinking each face first." },
         { "Wireframe", "Replaces solid faces with a wireframe tube running along each edge, at a chosen Thickness." },
         { "Triangulate", "Re-triangulates the mesh; Jitter randomizes how each face is split, for a more organic, less uniform look." },
         { "Normals", "Recomputes vertex normals - 'flat shade' gives hard per-face edges instead of smooth shading, 'flip' inverts which way every face points." },
         { "Explode", "Pushes each face outward from the mesh's centre along its own normal, by Amount, randomized per-face by Seed." },
         { "Twist", "Twists the mesh around a chosen Axis by Angle - the further a point is along that axis, the more it rotates." },
         { "Smooth", "Iteratively averages each vertex toward its neighbours (a Laplacian smooth) - Iterations and Strength control how much it relaxes. On a low-poly mesh like a Cube there's no extra geometry between the corners to round out, so raise Pre-subdivide first to see rounded edges." },
         { "Mirror", "Mirrors the mesh across a plane perpendicular to a chosen Axis, at a given plane offset. 'keep original' keeps the source geometry too, 'weld seam' merges the two halves where they meet." },
         { "Screw", "Sweeps the input profile around an Axis in a helical path - Steps sets resolution, Turns the number of revolutions, Rise/turn the pitch, Radius offsets the sweep outward." },
         { "Select", "Marks a subset of faces - by index range, position along an axis, normal direction, random chance, or within a radius - for downstream ops (Delete Selected / Transform Selected / Extrude Selected) to act on. Invert flips the selection, 'add to selection' unions with whatever was already selected." },
         { "Delete Selected", "Deletes the faces marked by an upstream Select op - or, with 'keep selected instead', deletes everything else and keeps only the selection." },
         { "Transform Selected", "Moves, rotates and scales only the faces marked by an upstream Select op, optionally sliding them along their own normals instead of a fixed direction." },
         { "Extrude Selected", "Extrudes only the faces marked by an upstream Select op along their own normals, by Distance, with Inset." },
      };
      auto it = kText.find(typeName);
      return it != kText.end() ? it->second : nullptr;
   }

   // Every filter-table effect - see core/FilterDefs.cpp for the actual shader
   // each of these names drives. Keyed by the raw (lowercase, spaceless-ish)
   // FilterDef::name, which is what GraphNode::typeName actually holds for these.
   const char* FilterHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         { "gaussianblur", "Blurs the image with a Gaussian-weighted kernel - a smooth, natural blur. Radius sets how far it samples." },
         { "boxblur", "Blurs by averaging a flat square of neighbouring pixels - cheaper and blockier than Gaussian Blur. Radius sets the box size." },
         { "motionblur", "Smears the image along a straight line, like camera or subject motion. Angle sets the direction, Distance how far it smears." },
         { "radialblur", "Blurs outward from a centre point, like a zoom or spin blur. Amount sets the strength, Center X/Y the origin." },
         { "unsharpmask", "Unsharp mask: blurs a copy of the image and adds back the difference, exaggerating edges. Amount is the strength, Radius how wide an edge it reacts to." },
         { "twirl", "Rotates pixels increasingly the closer they are to a centre point, like a whirlpool. Angle is the twist amount, Radius how far it reaches." },
         { "pinchpunch", "Pulls pixels toward, or pushes them away from, a centre point. Amount above 1 punches outward, below 1 pinches inward." },
         { "ripple", "Displaces pixels outward from a centre in concentric waves. Amplitude is wave height, Wavelength the spacing, Phase animates it." },
         { "pixelate", "Chunks the image into flat colour blocks. Block Size sets how large each block is." },
         { "addnoise", "Adds random per-pixel grain, re-randomised every frame. Amount sets how strong it is." },
         { "vignette", "Darkens the image toward the edges, framing the centre. Radius and Softness shape the falloff, Center X/Y offsets it." },
         { "transform", "Translates, scales and rotates the whole image. Scale X/Y let you stretch non-uniformly on top of the overall Scale." },
         { "brightnesscontrast", "Basic exposure control: Brightness adds or subtracts a flat amount, Contrast steepens or flattens the curve around mid-grey." },
         { "levels", "Remaps the input range: Black Point and White Point set what maps to pure black/white, Gamma curves the midtones." },
         { "hsl", "Shifts Hue, scales Saturation and adds Lightness, independent of the underlying colour." },
         { "invert", "Inverts every colour channel (alpha untouched) - a photographic negative. No parameters." },
         { "posterize", "Reduces the image to a fixed number of tonal Levels per channel, producing flat colour bands." },
         { "threshold", "Converts to pure black or white based on luminance, split at Threshold." },
         { "vibrance", "Boosts or reduces saturation relative to each pixel's own average brightness - gentler and more selective than a flat saturation push." },
         { "blackandwhite", "Converts to greyscale using per-channel Red/Green/Blue Weight, so you can control which colours read light or dark." },
         { "colorbalance", "Shifts colour along three axes - Cyan-Red, Magenta-Green, Yellow-Blue - without touching overall brightness." },
         { "exposure", "Multiplies brightness by powers of two, like a camera's exposure stop (compare Levels' linear/gamma remap)." },
         { "bloom", "Isolates pixels above a brightness Threshold, blurs them outward by Radius, and adds the glow back at Intensity - classic HDR-style bloom." },
         { "diffuseglow", "Screens a blurred copy of the whole image back over itself, glowing everything rather than just bright spots (compare Bloom, which is threshold-based)." },
         { "glitch", "Six glitch algorithms behind one 'kind' dropdown: Slice Shift (blocky RGB-split rows), RGB Shift, Scanlines, Blocks (jittered tiles), Wave, and Datamosh (sliced/shuffled rows with colour smear). Amount/Detail control strength/scale, Speed animates it, Seed reseeds the randomness." },
         { "lensdistortion", "Simulates a camera lens: Barrel bows the image in or out, Chromatic separates the colour channels' distortion for fringing, Zoom scales the result." },
         { "displace", "Offsets each pixel using a second patched-in image's red/green channels as a displacement map, or a built-in animated wave if nothing is patched. Map Scale tiles the map." },
         { "liquify", "Flows pixels around using animated Perlin-style noise, like a liquid warp. Scale sets the flow's feature size, Speed animates it." },
         { "symmetry", "Mirrors the image about the X axis, Y axis, or both, from a chosen centre point. Flip additionally reverses the whole image before mirroring." },
         { "kaleidoscope", "Slices the image into angular Segments around a centre and mirrors each one, kaleidoscope-style. Rotation spins the pattern, Zoom scales it." },
         { "mirror tile", "Tiles the image and alternately mirrors each tile so the edges line up seamlessly, unlike a plain repeat. Tiles sets how many repeats." },
         { "chroma key", "Removes a chosen Key Colour (green/blue-screen style), matched in a brightness-independent colour space. Tolerance/Softness shape the edge, Spill Removal desaturates colour-cast fringes, 'show: Matte' previews the alpha instead of the keyed result." },
         { "luma key", "Keys out a brightness range (Low-High) instead of a colour, with Softness at the edges and Invert to flip which range is kept. 'show: Matte' previews the alpha." },
         { "crop", "Crops in from each edge (Left/Right/Top/Bottom). 'outside' chooses what happens to the cropped-away area: stays transparent, the kept region zooms to fill the frame, or gets replaced with a Fill colour." },
         { "emboss", "Turns edges into a relief/bump look by differencing the image against itself offset along Angle/Distance. 'style: Over colour' keeps the original colours and adds the emboss on top; 'Grey' replaces the image with a flat emboss." },
         { "normal map", "Derives a tangent-space normal map from the image's luminance treated as a height field, for use as a bump/normal input elsewhere. 'output' can show the raw Normals, the source Height, or the gradient's Slope instead." },
         { "convolve", "A user-editable 3x3 convolution kernel (k11-k33) - blur, sharpen, edge-detect and emboss are all just different numbers in this grid. Divisor and Bias adjust the result, Spread scales the sample distance, Mix blends with the original." },
         { "lookup", "Recolors using a second patched-in image as a 1D palette strip - this pixel's Luminance (or a chosen channel) indexes across it. Offset shifts the index, Mix blends with the original." },
         { "halftone", "Renders the image as a dot pattern like offset printing. Scale sets dot density, Angle rotates the dot grid, 'style: Colour' uses three angled CMY-style dot layers instead of one greyscale layer." },
         { "edge sobel", "Classic Sobel edge detection: outputs the gradient magnitude as greyscale edges. Invert flips black and white." },
         { "edge outline", "Detects edges via a luminance gradient and draws them as flat-Colour outlines at a chosen Thickness and Threshold, over the original image." },
         { "tone shaper", "Lift/gamma/gain-style tone control via four blended curves: Shadows lift blacks, Highlights lift whites, Midtones bulge the middle, S-Curve blends in an S-shaped contrast curve." },
         { "lut", "Grades the image through a second patched-in HALD/strip LUT image - an N x N-sliced colour cube encoded as a flat strip. LUT Size must match the LUT image's slice count, Mix blends with the original." },
         { "gradientmap", "Remaps luminance onto a two-colour gradient (Shadow to Highlight), Photoshop Gradient Map-style. Mix blends with the original colour." },
         { "channelmixer", "Rebuilds each output channel as a weighted mix of the input's Red/Green/Blue - e.g. set 'Red from RGB' to swap or blend channels." },
         { "outerglow", "Adds a soft glow of a chosen Colour around the image's alpha edge, blurred outward. Amount controls strength." },
         { "coloroverlay", "Flat-tints the image toward a chosen Colour at a given Opacity." },
         { "dropshadow", "Offsets a blurred copy of the image's alpha behind it as a shadow, in a chosen Colour, Offset X/Y and Opacity." },
      };
      auto it = kText.find(typeName);
      return it != kText.end() ? it->second : nullptr;
   }

   // The twenty 2D vector primitives spawned from ShapeNode::ShapeNames() (see
   // nodes/ShapeNode.cpp) - all one shader/class, told apart only by this text
   // since their on-node params are the shared fill/stroke/feather/background set.
   const char* ShapeHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         { "Circle", "A filled/stroked circle - one of the Shape vector primitives, with size, feather, stroke and background controls." },
         { "Ellipse", "A filled/stroked ellipse - Aspect stretches it from a circle." },
         { "Rectangle", "A filled/stroked rectangle, with an optional corner radius." },
         { "Rounded Rect", "A rectangle with rounded corners - like Rectangle but with the corner radius exposed as its own control." },
         { "Triangle", "A filled/stroked equilateral-style triangle." },
         { "Polygon", "A regular polygon - Sides sets how many." },
         { "Star", "A star with adjustable point count (Sides) and Inner Ratio for how deep the points cut in." },
         { "Ring", "A donut / annulus - a circle with a hole; corner/thick controls the ring's thickness." },
         { "Cross", "A plus-sign / cross shape." },
         { "Line", "A straight stroked line, without a fill interior." },
         { "Hexagon", "A six-sided regular polygon (a Polygon preset with Sides fixed to 6)." },
         { "Heart", "A heart-shaped outline." },
         { "Arrow", "An arrow shape - direction follows the Rotation control." },
         { "Crescent", "A crescent-moon shape - two overlapping circles subtracted from each other." },
         { "Gear", "A cogwheel silhouette - Sides sets tooth count." },
         { "Superellipse", "A 'squircle' - a shape between a rectangle and an ellipse, tunable via Inner Ratio." },
         { "Pie", "A circular wedge / pie-slice - Inner Ratio controls how much of the circle is cut away." },
         { "Teardrop", "A teardrop / droplet outline." },
         { "Chevron", "An angled bracket / chevron shape." },
         { "Blob", "An organic, wobbly rounded shape - randomised per Seed-like controls rather than a precise geometric primitive." },
      };
      auto it = kText.find(typeName);
      return it != kText.end() ? it->second : nullptr;
   }

   // The twenty-four 3D primitives spawned from GeometryNode::ShapeNames() (see
   // nodes/Geometry3DNodes.cpp) - one class/shader, told apart by this text since
   // their on-node params are the shared sides/tube/material set from DrawGeometryParams.
   const char* Geometry3DHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         { "Plane", "A flat subdivided plane - one of the Geometry 3D primitives." },
         { "Cube", "A subdivided cube/box." },
         { "Sphere", "A UV sphere - latitude/longitude subdivision." },
         { "Icosphere", "A sphere built from subdivided triangles (an icosahedron refined outward) - more even triangle distribution than a UV Sphere." },
         { "Torus", "A ring/donut solid - 'tube' sets the tube radius relative to the overall ring." },
         { "Cylinder", "A cylinder - Sides sets how many-sided (low values give a prism)." },
         { "Cone", "A cone - Sides sets how many-sided." },
         { "Torus Knot", "A knotted torus - a tube swept along a (p,q) knot path rather than a plain circle." },
         { "Capsule", "A cylinder with hemispherical caps - 'tube' sets the cap/body radius." },
         { "Tube", "A hollow cylinder - 'tube' sets the wall's inner radius." },
         { "Pyramid", "A pyramid - Sides sets the base's side count (4 = classic square pyramid)." },
         { "Prism", "A prism - an extruded polygon with Sides sides." },
         { "Helix", "A helical tube spiraling around an axis - 'tube' sets the tube's radius." },
         { "Supershape", "A superformula-based shape - 'depth', 'tooth depth' and 'hub hole' warp it into gear-like or organic silhouettes." },
         { "Tetrahedron", "A 4-faced platonic solid." },
         { "Octahedron", "An 8-faced platonic solid." },
         { "Dodecahedron", "A 12-faced platonic solid." },
         { "Rounded Cube", "A cube with rounded edges/corners - 'corner radius' sets how rounded." },
         { "Mobius Strip", "A one-sided, one-edge twisted strip - 'width' sets the strip's width." },
         { "Klein Bottle", "A non-orientable closed surface - the classic self-intersecting 'bottle with no inside or outside'." },
         { "Gear 3D", "A 3D cogwheel - 'depth' sets tooth depth, 'tooth depth'/'hub hole' shape the teeth and centre bore. Named with a '3D' suffix because the 2D Shape node already has its own 'Gear'." },
         { "Star 3D", "A 3D extruded star - 'depth' sets extrusion depth, 'inner ratio' how deep the points cut in. Named with a '3D' suffix because the 2D Shape node already has its own 'Star'." },
         { "Disc", "A flat filled circle/disc mesh (as opposed to Cylinder, which has height)." },
         { "Arrow 3D", "A 3D arrow mesh - shaft plus arrowhead. Named with a '3D' suffix because the 2D Shape node already has its own 'Arrow'." },
      };
      auto it = kText.find(typeName);
      return it != kText.end() ? it->second : nullptr;
   }

   const char* CategoryHelpText(const std::string& category)
   {
      if (category == "Source") return "A source node - generates or loads an image with no image input of its own.";
      if (category == "Text") return "Renders text as an image.";
      if (category == "Effects") return "Transforms one image into another. Its parameters (and the shape of its name) describe what it changes; see Menu > Help > Module reference for the effect families.";
      if (category == "Color") return "Adjusts the colour of its input. See Menu > Help > Module reference, 'Color' section, for the full family.";
      if (category == "Compositing") return "Combines images, or otherwise manages how they flow through the patch.";
      if (category == "Modulators") return "Produces a changing number over time, not an image. Patch its output onto the small dot beside any slider to drive that parameter.";
      if (category == "Feedback") return "Reads back a previous frame to build loops - trails, echoes, growth. See Menu > Help > 'Using Feedback'.";
      if (category == "Mask") return "Produces a mask, or isolates part of the image by colour or luminance.";
      if (category == "Resynth") return "Iteratively regenerates the image, each generation drifting from its source.";
      if (category == "3D") return "Part of the 3D geometry/render pipeline - geometry and point-cloud nodes feed into Render 3D via a Camera and Lights.";
      if (category == "Output") return "Terminal node: shows, exports or records the final result.";
      return "No additional notes for this node.";
   }

   const char* NodeHelpText(const GraphNode& gn)
   {
      if (const char* specific = SpecificNodeHelpText(gn.typeName))
         return specific;
      if (const char* filter = FilterHelpText(gn.typeName))
         return filter;
      if (const char* shape = ShapeHelpText(gn.typeName))
         return shape;
      if (const char* geo3d = Geometry3DHelpText(gn.typeName))
         return geo3d;
      return CategoryHelpText(gn.category);
   }

   void DrawHelpWindow(bool* open)
   {
      ImGui::SetNextWindowSize(ImVec2(720, 620), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin("Infinite - help & module reference", open))
      {
         ImGui::End();
         return;
      }

      if (ImGui::CollapsingHeader("Getting started", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "Infinite is a node graph. Every node renders an image and passes it "
            "down a cable to the next one. A typical patch reads left to right:");
         ImGui::Bullet(); ImGui::TextWrapped("Source (image, video, shape, noise, formula) makes a picture.");
         ImGui::Bullet(); ImGui::TextWrapped("Effects and Color nodes change it.");
         ImGui::Bullet(); ImGui::TextWrapped("Compositing nodes combine several pictures into one.");
         ImGui::Bullet(); ImGui::TextWrapped("Output shows the result, exports a PNG, or records a video.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Nothing enforces that order - any output can feed any input, including "
            "back into effects for feedback-style chains.");
      }

      if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
      {
         if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
         {
            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("How");
            ImGui::TableHeadersRow();
            struct Row { const char* a; const char* b; };
            static const Row rows[] = {
               { "Add a node", "Right-click or double-click the canvas, then type to filter" },
               { "Note on the canvas", "Press / and start typing. Double-click an existing note to edit it." },
               { "Connect", "Drag from a node's 'out' dot to another node's input dot" },
               { "Modulate a parameter", "Drag a modulator's 'out' onto the small dot beside any slider" },
               { "Colour from a photo", "Add a Palette node, give it a reference image, then drag its 'out' onto the square dot beside any colour swatch. Each new cable takes the next swatch; click a bound swatch to step it." },
               { "Type an exact value", "Double-click a slider" },
               { "Pan the canvas", "Drag empty canvas" },
               { "Rubber-band select", "Shift + drag" },
               { "Duplicate", "Cmd+C / Cmd+V, or Shift+D to duplicate in place" },
               { "Select several", "Shift + drag a box around them, or Shift-click (or Ctrl-click) each one to add it. Then move, duplicate or delete as a group." },
               { "Delete", "Select, then Delete or Backspace" },
               { "Zoom", "Scroll (speed is adjustable in the Menu)" },
               { "Play / pause everything", "Play button in the top bar" },
            };
            for (const Row& r : rows)
            {
               ImGui::TableNextRow();
               ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.a);
               ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", r.b);
            }
            ImGui::EndTable();
         }
      }

      if (ImGui::CollapsingHeader("Transport and modulation", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "The top bar holds a global clock: Play/Pause, Rewind and BPM. Everything "
            "time-based reads from it - modulators, video playback and animated shaders - "
            "so pausing freezes the whole patch and changing the tempo retimes all of it "
            "at once.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Modulator rates are given in beats, not seconds. A rate of 4 means one "
            "cycle every four beats, so it stays in time when you change the BPM.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Every slider has a small dot to its left. Patch a modulator into that dot "
            "and the slider turns amber and becomes read-only - the value is now being "
            "driven. Delete the cable to take manual control back.");
      }

      if (ImGui::CollapsingHeader("Using Feedback", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "A Feedback node outputs what its input produced on the PREVIOUS frame. "
            "That one-frame delay is the whole point: it lets you wire a cycle "
            "without the graph chasing its own tail forever.");
         ImGui::Spacing();
         ImGui::TextWrapped("Feedback on its own does nothing visible - it is a delay, not an effect. "
                            "It only earns its keep inside a loop. The classic patch:");
         ImGui::Indent();
         ImGui::Bullet(); ImGui::TextWrapped("Shape (or any source)  ->  Blend input A");
         ImGui::Bullet(); ImGui::TextWrapped("Feedback  ->  Blend input B");
         ImGui::Bullet(); ImGui::TextWrapped("Blend  ->  transform  (scale 1.02, small rotation)");
         ImGui::Bullet(); ImGui::TextWrapped("transform  ->  back into Feedback's input   <- this closes the loop");
         ImGui::Unindent();
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Now every frame is the previous frame, nudged, with the source drawn "
            "over the top - which gives you infinite-zoom tunnels, echoes and "
            "growth. Set the Blend to Screen or Lighten and lower the opacity so "
            "the history fades rather than saturating.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "If you just want trails, use the Trails node instead - it is that same "
            "loop wrapped into one node, with decay, drift, zoom and rotation built "
            "in. Reaction-Diffusion is the other pre-wired feedback node: it needs "
            "no input at all and simulates a chemical system frame over frame.");
      }

      if (ImGui::CollapsingHeader("Module reference"))
      {
         struct Entry { const char* name; const char* text; };
         struct Group { const char* category; std::vector<Entry> entries; };
         static const std::vector<Group> groups = {
            { "Source", {
               { "Image Source", "Loads a still image. Opens the native file picker and decodes anything macOS can read - PNG, JPEG, TIFF, HEIC, RAW and more." },
               { "Video", "Plays a video file. Position follows the transport, so it pauses with everything else. Loop and speed (including reverse) are available." },
               { "Shape", "Ten vector primitives - circle, ellipse, rectangle, rounded rect, triangle, polygon, star, ring, cross, line - with fill, stroke, feather and background." },
               { "Noise", "Procedural noise: value, fBm, ridged, Voronoi, Worley edges and white. Domain warping, octaves and colour mapping included." },
               { "Draw", "Paint straight onto the node preview. Six procedural brushes, eraser, spacing and jitter. Patch an image in to paint over it. Strokes can be recorded and replayed as an animation." },
               { "Formula", "A live GLSL shader. Pick a preset or press 'Edit GLSL...' to write your own; four knobs (uA-uD) are exposed for modulation." },
               { "Texture", "Blender-standard procedural textures: Voronoi, Brick, Magic, Wave and Musgrave, each with its own parameter block." },
            } },
            { "Text", {
               { "Text", "Renders text using any font installed on the system, with size, colour, tracking, alignment and position." },
            } },
            { "Effects", {
               { "Blur family", "Gaussian, box, motion (angle + distance) and radial (with a centre point)." },
               { "Bloom / Diffuse Glow", "Bloom isolates pixels above a threshold and blooms them outward. Diffuse Glow screens a blurred copy back over the image." },
               { "Sharpen", "Unsharp mask - blurs a copy and adds back the difference." },
               { "Distortion", "Twirl, pinch/punch, ripple, lens distortion (with chromatic aberration), displace and liquify. Position-dependent effects have Centre X/Y." },
               { "Glitch family", "Five kinds: the original combined glitch, RGB shift, scanlines, blocks, wave and datamosh." },
               { "Symmetry", "Symmetry (mirror about X, Y or both), Kaleidoscope (segment count, rotation, zoom) and Mirror Tile." },
               { "Stylise", "Halftone (mono or CMY-style colour), Sobel edge detection and Edge Outline." },
               { "Transform", "Translate, scale and rotate - filed under Effects rather than as a separate category." },
               { "Pixelate / Noise / Vignette", "Block pixelation, additive grain and a vignette with its own centre." },
            } },
            { "Color", {
               { "Basic", "Brightness/contrast, exposure, levels (black/white point + gamma), invert, posterize, threshold, vibrance." },
               { "Curves", "Shadow / midtone / highlight lift plus an S-curve control." },
               { "LUT", "Applies a lookup-table image patched into the second input." },
               { "Gradient Map", "Remaps luminance onto a two-colour gradient." },
               { "Channel Mixer", "Rebuilds each output channel from a weighted mix of the input channels." },
               { "HSL / Colour Balance / Black & White", "Hue, saturation and lightness; per-axis colour shifts; weighted greyscale." },
               { "Color Ramp", "Recolors any 0-1 grayscale input through user-authored stops, up to 32 of them, with linear or constant interpolation. Unlike Gradient Map, it has no shape of its own - the shape comes from upstream." },
            } },
            { "Compositing", {
               { "Blend", "Two inputs and 31 blend modes - the full Normal / Multiply / Screen / Overlay / Hue / Saturation / Colour / Luminosity set, plus Erase." },
               { "Layer Stack", "Four inputs stacked bottom-up: A is the base, D sits on top. Each layer has its own blend mode and opacity, and dragging a layer header reorders the whole layer." },
               { "Switcher", "Cycles between its connected inputs every N beats or seconds, with an optional crossfade. Can be pinned to one input with 'manual'." },
               { "Fit", "Resamples an input to a chosen resolution. Fit letterboxes, Fill crops, Stretch ignores aspect, Native passes through. Use it to make differently-sized sources composite predictably." },
               { "Drop Shadow / Outer Glow / Colour Overlay", "Layer-effect style filters." },
            } },
            { "Modulators", {
               { "LFO", "Sine, triangle, saw up/down, square and sample-and-hold. Rate in beats, plus phase and an output range." },
               { "Random", "A new random value every N beats, with adjustable smoothing between steps. Deterministic, so rewinding replays the same sequence." },
               { "Pattern", "An eight-step sequencer. Set the eight sliders, choose how many steps to use, and it loops through them one step every N beats. Optional glide." },
               { "Math", "Combines two modulators - add, subtract, multiply, divide, min, max, average, difference - with gain and offset. Unpatched inputs fall back to a constant." },
            } },
            { "Feedback", {
               { "Feedback", "Outputs the previous frame. Nothing visible on its own - it is the delay that makes a loop legal. See 'Using Feedback' above." },
               { "Trails", "A pre-wired feedback loop: decaying accumulation with drift, zoom, rotation and hue rotation. Reach for this before wiring a loop by hand." },
               { "Reaction-Diffusion", "Gray-Scott chemical simulation, six presets. Needs no input; patch one in and its luminance varies the feed rate so the pattern grows differently through light and dark." },
            } },
            { "Mask", {
               { "Remove Background", "On-device segmentation via the OS - no model download, no network, no key. Subject mode needs macOS 14, Person mode macOS 12. Segmentation is slow, so the mask is computed on demand and cached; for video use auto-refresh, which runs on a beat interval rather than every frame." },
            } },
            { "Resynth", {
               { "Resynthesize", "Each generation reads the previous one, so the image drifts away from the source. The XY pad blends four named mutation effects assigned to its corners; Randomise re-rolls which four. The orb's path can be recorded, looped and replayed in time." },
            } },
            { "Output", {
               { "Output", "Terminal node. Shows the final image, exports a PNG, and records an H.264 .mov at a chosen frame rate. Recording captures the cooked output, so what you see is what is written." },
            } },
         };

         for (const Group& group : groups)
         {
            ImGui::SeparatorText(group.category);
            for (const Entry& entry : group.entries)
            {
               ImGui::Bullet();
               ImGui::TextUnformatted(entry.name);
               ImGui::Indent();
               ImGui::PushTextWrapPos(0.0f);
               ImGui::TextDisabled("%s", entry.text);
               ImGui::PopTextWrapPos();
               ImGui::Unindent();
            }
         }
      }

      if (ImGui::CollapsingHeader("Tips"))
      {
         ImGui::Bullet(); ImGui::TextWrapped("Put a Fit node before a Blend or Layer Stack when your sources are different sizes.");
         ImGui::Bullet(); ImGui::TextWrapped("Modulate a Switcher's 'every' with an LFO for irregular cutting.");
         ImGui::Bullet(); ImGui::TextWrapped("Chain a Math node off two LFOs at different rates to get slow drifting motion.");
         ImGui::Bullet(); ImGui::TextWrapped("A node's output can feed several inputs at once - it only renders once per frame.");
         ImGui::Bullet(); ImGui::TextWrapped("Settings and the last graph layout are stored in ~/Library/Application Support/Infinite.");
      }

      ImGui::End();
   }

   void DisconnectAllTo(INode* dying)
   {
      // a deleted modulator must also be cleared from any Math node feeding on it
      auto* dyingMod = dynamic_cast<IModulator*>(dying);
      auto* dyingFile = dynamic_cast<AudioFileNode*>(dying);
      auto* dyingGeometry = dynamic_cast<IGeometrySource*>(dying);
      auto* dyingCloud = dynamic_cast<IPointCloudSource*>(dying);
      auto* dyingXY = dynamic_cast<MacroXYNode*>(dying);
      IModulator* dyingY = dyingXY ? dyingXY->YOutput() : nullptr;
      for (GraphNode& other : gNodes)
      {
         if (auto* render = dynamic_cast<Render3DNode*>(other.node.get()))
         {
            for (int slot = 0; slot < Render3DNode::kSlots; slot++)
            {
               if (dyingGeometry != nullptr && render->geometry[slot] == dyingGeometry)
                  render->geometry[slot] = nullptr;
               if (dyingCloud != nullptr && render->clouds[slot] == dyingCloud)
                  render->clouds[slot] = nullptr;
            }
            if ((const void*)render->camera == (const void*)dying)
               render->camera = nullptr;
            for (int i = 0; i < Render3DNode::kLightSlots; i++)
               if ((const void*)render->lights[i] == (const void*)dying)
                  render->lights[i] = nullptr;
         }
         if (auto* geoOp = dynamic_cast<GeometryOpNode*>(other.node.get()))
         {
            if (dyingGeometry != nullptr && geoOp->input == dyingGeometry)
               geoOp->input = nullptr;
         }
         if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(other.node.get()))
         {
            if (dyingGeometry != nullptr && inst->pointSource == dyingGeometry)
               inst->pointSource = nullptr;
            if (dyingGeometry != nullptr && inst->instanceShape == dyingGeometry)
               inst->instanceShape = nullptr;
            if (dyingCloud != nullptr && inst->cloudSource == dyingCloud)
               inst->cloudSource = nullptr;
         }
         if (auto* meta = dynamic_cast<MetaBallNode*>(other.node.get()))
            if (dyingCloud != nullptr && meta->cloudSource == dyingCloud)
               meta->cloudSource = nullptr;
         if (auto* path = dynamic_cast<PathNode*>(other.node.get()))
         {
            if (dyingGeometry != nullptr && path->geometrySource == dyingGeometry)
               path->geometrySource = nullptr;
            if (auto* dyingCurve = dynamic_cast<ICurveSource*>(dying))
               if (path->curveSource == dyingCurve)
                  path->curveSource = nullptr;
         }
         // The single-geometry-input nodes. Missing one here would leave a
         // pointer to a freed node and crash on the next cook.
         if (dyingGeometry != nullptr)
         {
            if (auto* n3d = dynamic_cast<Null3DNode*>(other.node.get()))
               if (n3d->input == dyingGeometry)
                  n3d->input = nullptr;
            if (auto* mapn = dynamic_cast<MappingNode*>(other.node.get()))
               if (mapn->input == dyingGeometry)
                  mapn->input = nullptr;
            if (auto* mat = dynamic_cast<MaterialNode*>(other.node.get()))
               if (mat->input == dyingGeometry)
                  mat->input = nullptr;
            if (auto* disp = dynamic_cast<DisplacementNode*>(other.node.get()))
               if (disp->input == dyingGeometry)
                  disp->input = nullptr;
            if (auto* m2p = dynamic_cast<MeshToPointsNode*>(other.node.get()))
               if (m2p->input == dyingGeometry)
                  m2p->input = nullptr;
            if (auto* mrs = dynamic_cast<MeshResynthNode*>(other.node.get()))
               if (mrs->input == dyingGeometry)
                  mrs->input = nullptr;
            if (auto* cloth = dynamic_cast<ClothNode*>(other.node.get()))
               if (cloth->input == dyingGeometry)
                  cloth->input = nullptr;
            if (auto* join = dynamic_cast<JoinGeometryNode*>(other.node.get()))
               for (int i = 0; i < JoinGeometryNode::kSlots; i++)
                  if (join->inputs[i] == dyingGeometry)
                     join->inputs[i] = nullptr;
            if (auto* wrap = dynamic_cast<WrapNode*>(other.node.get()))
            {
               if (wrap->sourceInput == dyingGeometry)
                  wrap->sourceInput = nullptr;
               if (wrap->targetInput == dyingGeometry)
                  wrap->targetInput = nullptr;
            }
            if (auto* sw3 = dynamic_cast<Switcher3DNode*>(other.node.get()))
               for (int i = 0; i < Switcher3DNode::kSlots; i++)
                  if (sw3->inputs[i] == dyingGeometry)
                     sw3->inputs[i] = nullptr;
         }
         if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(other.node.get()))
         {
            if (dyingFile != nullptr && audio->fileSource == dyingFile)
               audio->fileSource = nullptr;
         }
         if (auto* out = dynamic_cast<OutputNode*>(other.node.get()))
         {
            if (dyingFile != nullptr && out->audioSource == dyingFile)
               out->audioSource = nullptr;
         }
         if (auto* math = dynamic_cast<MathNode*>(other.node.get()))
         {
            if ((dyingMod != nullptr && math->inputA == dyingMod) ||
                (dyingY != nullptr && math->inputA == dyingY))
               math->inputA = nullptr;
            if ((dyingMod != nullptr && math->inputB == dyingMod) ||
                (dyingY != nullptr && math->inputB == dyingY))
               math->inputB = nullptr;
         }
         int inputs = InputCountFor(other);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(other, slot);
            if (cable != nullptr && cable->GetSource() == dying)
               cable->Disconnect();
         }
      }
   }

   void RemoveNodeByIndex(int index)
   {
      GraphNode* victim = FindNodeByIndex(index);
      if (victim == nullptr)
         return;
      PushUndoCheckpoint();
      Modulation::Instance().UnbindAllFor(index);
      PaletteBinding::Instance().UnbindAllFor(index);
      gModHistory.erase(index);
      DisconnectAllTo(victim->node.get());
      // A deleted Group's membership set must go with it - otherwise its
      // GroupNode* stays around as a dangling map key that a future
      // allocation could reuse, silently handing a stranger's members to a
      // brand new group. Every other group also drops the index in case it
      // was one of its members.
      if (auto* deadGroup = dynamic_cast<GroupNode*>(victim->node.get()))
         gGroupMembers.erase(deadGroup);
      for (auto& entry : gGroupMembers)
         entry.second.erase(index);

      // Don't destroy the node/viewport here: this frame's ImGui draw list may
      // already contain AddImage() calls (queued while drawing this node's
      // body/mini-viewport earlier this frame) referencing their GL output
      // textures. Deleting those textures now, before
      // ImGui_ImplOpenGL3_RenderDrawData() actually submits the draw list at
      // the end of the frame, frees a GL name that the pending draw commands
      // still reference - producing a one-frame flash of garbage/reused-texture
      // content. Retire ownership instead; actual teardown happens at the top
      // of the next frame's loop, once this frame is fully rendered and
      // presented (see gRetiredNodes/gRetiredViewports drain next to
      // glfwPollEvents()).
      if (auto vp = gNodeViewports.extract(index); !vp.empty())
         gRetiredViewports.push_back(std::move(vp));
      gRetiredNodes.push_back(std::move(victim->node));

      gNodes.erase(std::remove_if(gNodes.begin(), gNodes.end(),
                                  [index](const GraphNode& g) { return g.index == index; }),
                   gNodes.end());
   }
   std::string gPatchPath;      // "" until the patch has been saved somewhere
   bool gPatchDirty = false;
   std::string gPatchStatus;

   // ---- saving ----
   // Everything SavePatchTo used to build in place, minus the file write -
   // shared with undo/redo, which snapshots this same in-memory shape rather
   // than round-tripping through disk.
   Patch::Data BuildPatchData()
   {
      Patch::Data data;
      for (GraphNode& gn : gNodes)
      {
         Patch::NodeRecord rec;
         rec.index = gn.index;
         rec.category = gn.category;
         rec.typeName = gn.typeName;
         // The cached live position, not the spawn position: the node has almost
         // certainly been dragged since it was created. Read from the cache
         // rather than the editor, since saving runs outside the editor context.
         rec.x = gn.liveX;
         rec.y = gn.liveY;
         rec.showParams = gn.showParams;
         rec.bypassed = gn.node->bypassed;
         rec.showMiniViewport = gn.showMiniViewport;
         Patch::SaveParams(gn.node.get(), rec.params);
         data.nodes.push_back(std::move(rec));

         for (int slot = 0; slot < InputCountFor(gn); slot++)
         {
            if (ImageCable* cable = CableFor(gn, slot))
            {
               if (!cable->IsConnected())
                  continue;
               for (GraphNode& src : gNodes)
               {
                  if (src.node.get() == cable->GetSource())
                  {
                     data.cables.push_back({ gn.index, slot, src.index });
                     break;
                  }
               }
            }
         }
      }

      // Geometry, camera, light, audio and modulator-input pins, found by
      // comparing against each candidate source rather than stored by index.
      for (GraphNode& gn : gNodes)
      {
         auto record = [&](const void* wanted, int slot)
         {
            if (wanted == nullptr)
               return;
            for (GraphNode& src : gNodes)
            {
               // Compared against each interface separately: with multiple
               // inheritance an IPointCloudSource* and an INode* into the same
               // object are different addresses, so one comparison is not enough.
               const void* asGeo = dynamic_cast<IGeometrySource*>(src.node.get());
               const void* asCloud = dynamic_cast<IPointCloudSource*>(src.node.get());
               if (asGeo == wanted || asCloud == wanted || (const void*)src.node.get() == wanted)
               {
                  data.geometry.push_back({ gn.index, slot, src.index });
                  return;
               }
            }
         };

         if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         {
            for (int i = 0; i < Render3DNode::kSlots; i++)
            {
               // A pure cloud source (no IGeometrySource) has nothing in
               // geometry[i] to record from - fall back to clouds[i]. A dual
               // source is fully found via geometry[i] already; recording
               // both would push the same cable twice.
               if (render->geometry[i] != nullptr)
                  record(render->geometry[i], i);
               else
                  record(render->clouds[i], i);
            }
            record(render->camera, Render3DNode::kSlots);
            for (int i = 0; i < Render3DNode::kLightSlots; i++)
               record(render->lights[i], Render3DNode::kSlots + 1 + i);
         }
         if (auto* op = dynamic_cast<GeometryOpNode*>(gn.node.get())) record(op->input, 0);
         if (auto* n3d = dynamic_cast<Null3DNode*>(gn.node.get())) record(n3d->input, 0);
         if (auto* mapn = dynamic_cast<MappingNode*>(gn.node.get())) record(mapn->input, 0);
         if (auto* mat = dynamic_cast<MaterialNode*>(gn.node.get())) record(mat->input, 0);
         if (auto* disp = dynamic_cast<DisplacementNode*>(gn.node.get())) record(disp->input, 0);
         if (auto* m2p = dynamic_cast<MeshToPointsNode*>(gn.node.get())) record(m2p->input, 0);
         if (auto* mrs = dynamic_cast<MeshResynthNode*>(gn.node.get())) record(mrs->input, 0);
         if (auto* cloth = dynamic_cast<ClothNode*>(gn.node.get())) record(cloth->input, 0);
         if (auto* join = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
            for (int i = 0; i < JoinGeometryNode::kSlots; i++)
               record(join->inputs[i], i);
         if (auto* meta = dynamic_cast<MetaBallNode*>(gn.node.get()))
            record(meta->cloudSource, 0);
         if (auto* path = dynamic_cast<PathNode*>(gn.node.get()))
         {
            record(path->curveSource, 0);
            record(path->geometrySource, 1);
         }
         if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
         {
            record(inst->pointSource, 0);
            record(inst->instanceShape, 1);
            record(inst->cloudSource, 2);
         }
         if (auto* w = dynamic_cast<WrapNode*>(gn.node.get()))
         {
            record(w->sourceInput, 0);
            record(w->targetInput, 1);
         }
         if (auto* sw3 = dynamic_cast<Switcher3DNode*>(gn.node.get()))
            for (int i = 0; i < Switcher3DNode::kSlots; i++)
               record(sw3->inputs[i], i);
         if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(gn.node.get()))
            record(audio->fileSource, 0);
         if (auto* out = dynamic_cast<OutputNode*>(gn.node.get()))
            record(out->audioSource, 1);
         if (auto* math = dynamic_cast<MathNode*>(gn.node.get()))
         {
            for (int slot = 0; slot < 2; slot++)
            {
               IModulator* wanted = (slot == 0) ? math->inputA : math->inputB;
               if (wanted == nullptr)
                  continue;
               for (GraphNode& src : gNodes)
               {
                  bool found = false;
                  for (int o = 0; o < std::max(1, src.node->OutputCount()) && !found; o++)
                     if (ModulatorForOutput(src.node.get(), o) == wanted)
                     {
                        data.geometry.push_back({ gn.index, slot, src.index });
                        found = true;
                     }
                  if (found)
                     break;
               }
            }
         }
      }

      for (const auto& link : Modulation::Instance().Links())
         data.modulation.push_back({ link.first.first, link.first.second,
                                     link.second.nodeIndex, link.second.outputIndex });
      for (const auto& link : PaletteBinding::Instance().Links())
         data.palette.push_back({ link.first.first, link.first.second,
                                  link.second.nodeIndex, link.second.swatchIndex });
      for (const auto& expr : Modulation::Instance().Expressions())
         data.expressions.push_back({ expr.first.first, expr.first.second, expr.second });
      return data;
   }

   bool SavePatchTo(const std::string& path)
   {
      Patch::Data data = BuildPatchData();
      std::string error;
      if (!Patch::Write(path, data, error))
      {
         gPatchStatus = "Save failed: " + error;
         return false;
      }

      gPatchPath = path;
      gPatchDirty = false;
      gPatchStatus = "Saved";
      Patch::NoteRecent(path);
      return true;
   }

   // Set while ApplyPatchData is repopulating the graph from a snapshot, so
   // the SpawnNode/connect calls it makes don't themselves push new undo
   // checkpoints - that would corrupt the very stack an undo/redo is reading
   // from, and turn opening a 50-node patch into 50 checkpoints.
   bool gSuppressUndoCheckpoints = false;
   std::vector<Patch::Data> gUndoStack;
   std::vector<Patch::Data> gRedoStack;
   const size_t kMaxUndoDepth = 200;

   void NewPatch()
   {
      gNodes.clear();
      gLinks.clear();
      gModHistory.clear();
      Modulation::Instance().Clear();
      PaletteBinding::Instance().Clear();
      gNextIndex = 1;
      gPatchPath.clear();
      gPatchDirty = false;
      gPatchStatus = "New patch";
      // Only for a genuine "start a fresh document" - not when NewPatch is
      // called from inside ApplyPatchData as the first step of restoring a
      // snapshot, which must leave the stacks alone.
      if (!gSuppressUndoCheckpoints)
      {
         gUndoStack.clear();
         gRedoStack.clear();
      }
   }

   // Rebuilds the live graph from a snapshot - shared by LoadPatchFrom (from
   // disk) and Undo/Redo (from the in-memory stacks). Callers decide what
   // happens to gPatchPath/gUndoStack/gRedoStack afterwards; a loaded file is
   // a new document boundary, an undo is not.
   void ApplyPatchData(const Patch::Data& data)
   {
      gSuppressUndoCheckpoints = true;
      NewPatch();

      // Saved indices are remapped rather than reused: they only have to be
      // internally consistent, and reusing them would collide with the running
      // counter and with the editor's own per-id state.
      std::map<int, int> remap;
      for (const Patch::NodeRecord& rec : data.nodes)
      {
         GraphNode* spawned = SpawnNode(rec.typeName, rec.category, rec.x, rec.y);
         if (spawned == nullptr)
         {
            // A patch naming a node type this build does not have still opens;
            // it just comes back missing that node.
            fprintf(stderr, "patch: unknown node type '%s', skipped\n", rec.typeName.c_str());
            continue;
         }
         remap[rec.index] = spawned->index;
         spawned->showParams = rec.showParams;
         spawned->node->bypassed = rec.bypassed;
         spawned->showMiniViewport = rec.showMiniViewport;
         Patch::LoadParams(spawned->node.get(), rec.params);
         ReloadDerivedState(spawned->node.get());
      }

      auto resolve = [&](int savedIndex) -> GraphNode*
      {
         auto it = remap.find(savedIndex);
         return (it == remap.end()) ? nullptr : FindNodeByIndex(it->second);
      };

      for (const Patch::CableRecord& c : data.cables)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst == nullptr || src == nullptr)
            continue;
         if (ImageCable* cable = CableFor(*dst, c.dstSlot))
            cable->Connect(src->node.get());
      }
      for (const Patch::CableRecord& c : data.geometry)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst != nullptr && src != nullptr)
            ConnectGeometrySlot(*dst, c.dstSlot, *src);
      }
      for (const Patch::ModRecord& m : data.modulation)
      {
         GraphNode* dst = resolve(m.dstIndex);
         GraphNode* src = resolve(m.srcIndex);
         if (dst != nullptr && src != nullptr)
            Modulation::Instance().Bind(dst->index, m.dstParam, src->index, m.srcOutput);
      }
      for (const Patch::PaletteRecord& c : data.palette)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst != nullptr && src != nullptr)
            PaletteBinding::Instance().Bind(dst->index, c.dstColor, src->index, c.srcSwatch);
      }
      for (const Patch::ExprRecord& e : data.expressions)
      {
         GraphNode* dst = resolve(e.dstIndex);
         if (dst != nullptr)
            Modulation::Instance().SetExpression(dst->index, e.dstParam, e.text);
      }

      gSuppressUndoCheckpoints = false;
   }

   bool LoadPatchFrom(const std::string& path)
   {
      Patch::Data data;
      std::string error;
      if (!Patch::Read(path, data, error))
      {
         gPatchStatus = "Open failed: " + error;
         return false;
      }

      ApplyPatchData(data);

      // A freshly opened file is a new-document boundary: undoing back into
      // whatever was open before this file is not a thing anyone wants.
      gUndoStack.clear();
      gRedoStack.clear();

      gPatchPath = path;
      gPatchDirty = false;
      gPatchStatus = "Opened";
      Patch::NoteRecent(path);
      gRequestFitView = true;
      return true;
   }

   // Shared by PushUndoCheckpoint (freshly captured) and the node-drag
   // checkpoint (captured earlier, at mouse-down, before the drag moved
   // anything - by the time a drag is detected the live positions have
   // already changed, so that caller can't use BuildPatchData() at the point
   // it decides to push).
   void PushUndoSnapshot(Patch::Data snapshot)
   {
      if (gSuppressUndoCheckpoints)
         return;
      gUndoStack.push_back(std::move(snapshot));
      if (gUndoStack.size() > kMaxUndoDepth)
         gUndoStack.erase(gUndoStack.begin());
      // A fresh action invalidates whatever redo history pointed at a future
      // that no longer follows from the graph's current state.
      gRedoStack.clear();
      gPatchDirty = true;
   }

   // Captures the graph as it is RIGHT NOW, before the caller's mutation runs.
   // Undo restores this; Redo re-applies whatever the mutation was about to do.
   void PushUndoCheckpoint()
   {
      if (gSuppressUndoCheckpoints)
         return;
      PushUndoSnapshot(BuildPatchData());
   }

   // Node-drag checkpoint state. A drag gesture spans many frames of
   // liveX/liveY already having changed, so the pre-drag snapshot has to be
   // taken speculatively at mouse-down and only actually pushed once real
   // movement is confirmed - otherwise a plain click-to-select would push a
   // checkpoint identical to the current state.
   Patch::Data gDragStartSnapshot;
   bool gDragSnapshotValid = false;
   bool gDragSnapshotPushed = false;

   void Undo()
   {
      if (gUndoStack.empty())
         return;
      gRedoStack.push_back(BuildPatchData());
      Patch::Data prev = gUndoStack.back();
      gUndoStack.pop_back();
      ApplyPatchData(prev);
      gPatchDirty = true;
      gPatchStatus = "Undo";
   }

   void Redo()
   {
      if (gRedoStack.empty())
         return;
      gUndoStack.push_back(BuildPatchData());
      Patch::Data next = gRedoStack.back();
      gRedoStack.pop_back();
      ApplyPatchData(next);
      gPatchDirty = true;
      gPatchStatus = "Redo";
   }

   // Drawn inside the ed::Suspend() block alongside the popups, so plain
   // ImGui widgets work normally without the editor intercepting the mouse.
   void DrawMinimap()
   {
      if (!gMinimapEnabled || gNodes.empty())
         return;

      const float pad = 14.0f;
      const float w = gMinimapSize;
      const float h = gMinimapSize * 0.72f;

      ImVec2 origin;
      switch (gMinimapCorner)
      {
         case 0: origin = ImVec2(gGraphScreenTL.x + pad, gGraphScreenTL.y + pad); break;
         case 1: origin = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x - w - pad,
                                 gGraphScreenTL.y + pad); break;
         case 2: origin = ImVec2(gGraphScreenTL.x + pad,
                                 gGraphScreenTL.y + gGraphScreenSize.y - h - pad); break;
         default: origin = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x - w - pad,
                                  gGraphScreenTL.y + gGraphScreenSize.y - h - pad); break;
      }

      // World-space bounds of every node, in an assumed node footprint - the
      // editor does not expose node sizes outside BeginNode/EndNode, and a
      // minimap dot does not need to be pixel-accurate to be useful.
      //
      // Deliberately NOT including the current viewport in this box: before
      // the view has ever been fit to content (or after zooming way out) the
      // viewport can span a world area far larger than where the nodes
      // actually are, which would crush every node down into one corner to
      // make room for a mostly-empty viewport rectangle. The map fits the
      // content; the viewport indicator below is clipped to the panel
      // instead, the same way a game minimap lets the camera frustum run off
      // the edge rather than rescaling the whole map to contain it.
      const float kNodeW = 260.0f, kNodeH = 90.0f;
      float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
      for (const GraphNode& gn : gNodes)
      {
         minX = std::min(minX, gn.liveX); maxX = std::max(maxX, gn.liveX + kNodeW);
         minY = std::min(minY, gn.liveY); maxY = std::max(maxY, gn.liveY + kNodeH);
      }

      const ImVec2 viewWorldTL = ed::ScreenToCanvas(gGraphScreenTL);
      const ImVec2 viewWorldBR = ed::ScreenToCanvas(
         ImVec2(gGraphScreenTL.x + gGraphScreenSize.x, gGraphScreenTL.y + gGraphScreenSize.y));

      const float worldW = std::max(1.0f, maxX - minX);
      const float worldH = std::max(1.0f, maxY - minY);
      const float margin = 10.0f;
      // Uniform, not per-axis: a per-axis fit would stretch node rectangles
      // and make the layout unrecognisable against the real canvas.
      const float scale = std::min((w - margin * 2.0f) / worldW, (h - margin * 2.0f) / worldH);

      auto toMinimap = [&](ImVec2 world) -> ImVec2
      {
         return ImVec2(origin.x + margin + (world.x - minX) * scale,
                       origin.y + margin + (world.y - minY) * scale);
      };

      ImDrawList* dl = ImGui::GetForegroundDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h),
                        IM_COL32(14, 15, 20, (int)(gMinimapOpacity * 255)), 6.0f);
      dl->AddRect(origin, ImVec2(origin.x + w, origin.y + h), IM_COL32(70, 74, 90, 200), 6.0f, 0, 1.5f);

      for (const GraphNode& gn : gNodes)
      {
         const ImVec2 a = toMinimap(ImVec2(gn.liveX, gn.liveY));
         const ImVec2 b = toMinimap(ImVec2(gn.liveX + kNodeW, gn.liveY + kNodeH));
         const bool selected = ed::IsNodeSelected(gn.NodeId());
         dl->AddRectFilled(a, b, selected ? IM_COL32(255, 190, 90, 230) : IM_COL32(110, 150, 210, 200), 2.0f);
      }

      // The current viewport, outlined - the one thing a static "map" view
      // gives you that scrolling around by feel does not. Clipped to the
      // panel: a viewport far outside the node bounds (unfit, or zoomed out
      // past every node) still draws as much of its edge as overlaps rather
      // than being allowed to warp the map's own scale.
      {
         dl->PushClipRect(origin, ImVec2(origin.x + w, origin.y + h), true);
         const ImVec2 a = toMinimap(viewWorldTL);
         const ImVec2 b = toMinimap(viewWorldBR);
         dl->AddRect(a, b, IM_COL32(255, 255, 255, 220), 2.0f, 0, 1.5f);
         dl->PopClipRect();
      }

      // Click or drag inside the minimap to jump to whatever node is nearest
      // that point - there is no public API to pan to an arbitrary empty
      // spot, only to navigate to a node, so this snaps to the closest one.
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##minimap", ImVec2(w, h));
      if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         const ImVec2 mouse = ImGui::GetMousePos();
         const float worldX = minX + (mouse.x - origin.x - margin) / scale;
         const float worldY = minY + (mouse.y - origin.y - margin) / scale;

         const GraphNode* nearest = nullptr;
         float bestDist = 1e18f;
         for (const GraphNode& gn : gNodes)
         {
            const float dx = (gn.liveX + kNodeW * 0.5f) - worldX;
            const float dy = (gn.liveY + kNodeH * 0.5f) - worldY;
            const float dist = dx * dx + dy * dy;
            if (dist < bestDist) { bestDist = dist; nearest = &gn; }
         }
         if (nearest != nullptr)
         {
            ed::SelectNode(nearest->NodeId());
            ed::NavigateToSelection(false, 0.0f);
         }
      }
   }

   void SavePatchInteractive(bool forceDialog)
   {
      std::string path = gPatchPath;
      if (path.empty() || forceDialog)
      {
         std::string suggested = "Untitled.inf";
         if (!gPatchPath.empty())
         {
            const size_t slash = gPatchPath.find_last_of('/');
            suggested = (slash == std::string::npos) ? gPatchPath : gPatchPath.substr(slash + 1);
         }
         path = Platform::SavePatchDialog(suggested);
         if (path.empty())
            return; // cancelled
         // The dialog does not force an extension, and a patch without one is
         // awkward to find again.
         if (path.size() < 4 || path.compare(path.size() - 4, 4, ".inf") != 0)
            path += ".inf";
      }
      SavePatchTo(path);
   }

   // Set for one frame when a close/quit was deferred because the patch has
   // unsaved changes, so the UI pass knows to pop the confirmation modal.
   bool gShowUnsavedChangesModal = false;

   // The single gate every real close path (Quit menu, Cmd+Q, red button)
   // routes through. Dev-harness exits (INFINITE_EXITAFTER, selftest modes,
   // screenshot mode) call glfwSetWindowShouldClose directly and deliberately
   // skip this - a scripted run should never block on a modal nobody can see.
   void RequestClose(GLFWwindow* window)
   {
      if (gPatchDirty)
      {
         // GLFW already set shouldClose before invoking the close callback
         // that leads here (see _glfwInputWindowCloseRequest) - undo that so
         // the app stays open until the modal is resolved.
         glfwSetWindowShouldClose(window, GLFW_FALSE);
         gShowUnsavedChangesModal = true;
      }
      else
      {
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
   }

}

int main()
{
   if (!glfwInit())
   {
      fprintf(stderr, "glfwInit failed\n");
      return 1;
   }

   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
   glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
   glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

   GLFWwindow* window = glfwCreateWindow(1600, 1000, "Infinite", nullptr, nullptr);
   if (!window)
   {
      fprintf(stderr, "glfwCreateWindow failed\n");
      glfwTerminate();
      return 1;
   }

   glfwMakeContextCurrent(window);
   glfwSwapInterval(1);
   Platform::PreventAppNap();

   // Covers both the red close button and Cmd+Q: GLFW's Cocoa backend routes
   // applicationShouldTerminate: through the same _glfwInputWindowCloseRequest
   // as the window's own close button, so one callback gates both.
   glfwSetWindowCloseCallback(window, [](GLFWwindow* w) { RequestClose(w); });

   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::StyleColorsDark();

   // A proper UI typeface instead of ImGui's bitmap default. Retina-aware:
   // load at 2x and scale down so text stays sharp on a HiDPI display.
   {
      float xscale = 1.0f, yscale = 1.0f;
      glfwGetWindowContentScale(window, &xscale, &yscale);
      const float baseSize = 15.0f;
      const char* candidates[] = {
         "/System/Library/Fonts/SFNS.ttf",
         "/System/Library/Fonts/HelveticaNeue.ttc",
         "/System/Library/Fonts/Helvetica.ttc",
         "/System/Library/Fonts/Supplemental/Arial.ttf",
      };
      ImGuiIO& io = ImGui::GetIO();
      for (const char* path : candidates)
      {
         if (io.Fonts->AddFontFromFileTTF(path, baseSize * xscale) != nullptr)
         {
            io.FontGlobalScale = 1.0f / xscale;
            break;
         }
      }
   }

   ImGuiStyle& style = ImGui::GetStyle();
   style.FrameRounding = 3.0f;
   style.GrabRounding = 3.0f;
   style.WindowRounding = 4.0f;
   style.ItemSpacing = ImVec2(6, 5);

   ImGui_ImplGlfw_InitForOpenGL(window, true);
   // Installed after the backend so it chains rather than replacing ImGui's.
   glfwSetDropCallback(window, OnFilesDropped);
   ImGui_ImplOpenGL3_Init("#version 150");

   // Cocoa chdir's a bundled app into Contents/Resources, so anything written
   // with a relative path lands inside the .app - which breaks its code
   // signature on first launch and can get the app reported as damaged. Keep
   // all mutable state in Application Support instead.
   std::string settingsDir;
   if (const char* home = getenv("HOME"))
   {
      settingsDir = std::string(home) + "/Library/Application Support/Infinite";
      mkdir(settingsDir.c_str(), 0755); // fine if it already exists
   }
   static std::string iniPath = settingsDir.empty() ? std::string("imgui.ini")
                                                    : settingsDir + "/imgui.ini";
   static std::string graphPath = settingsDir.empty() ? std::string("Infinite.json")
                                                      : settingsDir + "/Infinite.json";
   if (getenv("INFINITE_DRAGTEST") != nullptr)
   {
      // DRAGTEST pans the view; SettingsFile persists that pan to disk, so
      // sharing the real settings file means every run starts from wherever
      // the last one left the camera, slowly walking the fixture node off
      // the visible window over repeated runs (e.g. one hygiene-check run
      // per commit). Use a throwaway path instead, reset before use, so the
      // test always starts from a known view.
      graphPath = settingsDir.empty() ? std::string("InfiniteDragTest.json")
                                       : settingsDir + "/InfiniteDragTest.json";
      remove(graphPath.c_str());
   }
   ImGui::GetIO().IniFilename = iniPath.c_str();

   ed::Config config;
   config.SettingsFile = graphPath.c_str();
   config.EnableSmoothZoom = true; // trackpad momentum made stepped zoom feel jumpy
   Patch::LoadRecents();
   CategoryColors::LoadPreference();

   gEditor = ed::CreateEditor(&config);
   ed::SetCurrentEditor(gEditor); // ed::GetStyle() below needs a current editor

   RegisterNodes();
   ApplyTheme();

   // Flat list of every registered type, for the double-click search box.
   std::vector<std::pair<std::string, std::string>> allTypes; // (name, category)
   for (const std::string& category : NodeFactory::Instance().GetCategories())
   {
      for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
         allTypes.emplace_back(name, category);
   }

   const bool selfTest = getenv("IMAGERESYNTH_SELFTEST") != nullptr;
   if (selfTest)
   {
      // headless smoke test of the whole registry: spawn one of every registered
      // node type, feed every input-taking node from a real source so its shader
      // actually compiles and cooks, then report dimensions.
      for (const auto& t : allTypes)
         SpawnNode(t.first, t.second);

      for (GraphNode& gn : gNodes)
      {
         if (gn.typeName == "Shape")
            gSelfTestFeeder = &gn;
      }
      for (GraphNode& gn : gNodes)
      {
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            if (ImageCable* cable = CableFor(gn, slot))
               cable->Connect(gSelfTestFeeder->node.get());
         }
      }
   }
   else
   {
      // The canvas starts empty; the dev test modes below need a fixture graph,
      // but a normal launch gives the user a blank patch.
      const bool wantsFixture =
         getenv("INFINITE_RESYNTHTEST") != nullptr ||
         getenv("INFINITE_HIDETEST") != nullptr ||
         getenv("INFINITE_MACROTEST") != nullptr ||
         getenv("INFINITE_RECTEST") != nullptr || getenv("INFINITE_MODTEST") != nullptr ||
         getenv("INFINITE_SIZETEST") != nullptr || getenv("INFINITE_INPUTTEST") != nullptr ||
         getenv("INFINITE_DRAGTEST") != nullptr || getenv("INFINITE_COLORTEST") != nullptr ||
         getenv("INFINITE_PICKERTEST") != nullptr;

      if (getenv("INFINITE_BYPASSTEST") != nullptr)
      {
         SpawnNode("Shape", "Source", 40.0f, 40.0f);   // 0 white circle
         SpawnNode("invert", "Color", 320.0f, 40.0f);  // 1
         SpawnNode("Output", "Output", 600.0f, 40.0f); // 2
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[2], 0)->Connect(gNodes[1].node.get());
      }
      else if (getenv("INFINITE_MINIVIEWPORTTEST") != nullptr)
      {
         // Cube -> Select (the +Y side, by normal) -> Transform Selected. Slot B
         // is the same cube with no Select in front of it, so the mini viewport
         // test can compare a mesh carrying a face mask against one that never
         // went through a Select at all.
         // Render first and closest to the origin: the canvas opens at the
         // top-left, and the point of this fixture is to look at its output.
         SpawnNode("Render 3D", "3D", 40.0f, 40.0f);            // 0
         SpawnNode("Geometry", "3D", 1400.0f, 40.0f);           // 1
         SpawnNode("Select", "3D", 1700.0f, 40.0f);             // 2
         SpawnNode("Transform Selected", "3D", 2000.0f, 40.0f); // 3
         SpawnNode("Geometry", "3D", 1400.0f, 500.0f);          // 4

         auto* cube = static_cast<GeometryNode*>(gNodes[1].node.get());
         cube->shape = 1; cube->detail = 24; // 3x3 grid per side, 108 triangles
         auto* select = static_cast<GeometryOpNode*>(gNodes[2].node.get());
         select->input = cube;
         select->op = GeometryOpNode::kSelect;
         select->selectMode = MeshOps::kSelectNormal;
         select->axis = 1;          // Y
         select->selectA = 0.9f;    // facing
         select->selectC = 1.0f;    // +
         auto* move = static_cast<GeometryOpNode*>(gNodes[3].node.get());
         move->input = select;
         move->op = GeometryOpNode::kTransformSelected;
         move->moveAlongNormals = true;
         move->normalAmount = 0.35f;
         move->offsetX = move->offsetY = move->offsetZ = 0.0f;
         move->rotX = move->rotY = move->rotZ = 0.0f;
         move->scaleX = move->scaleY = move->scaleZ = 1.0f;

         auto* plain = static_cast<GeometryNode*>(gNodes[4].node.get());
         plain->shape = 1; plain->detail = 24;
         plain->posX = 1.4f;

         auto* r = static_cast<Render3DNode*>(gNodes[0].node.get());
         r->geometry[0] = move;
         r->geometry[1] = plain;
         r->width = 700.0f; r->height = 700.0f;
         r->camDistance = 4.2f;
         r->targetX = 0.7f;
         for (GraphNode& gn : gNodes)
            gn.showParams = false;
      }
      else if (getenv("INFINITE_GEOTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);      // 0 points source
         SpawnNode("Geometry", "3D", 40.0f, 400.0f);     // 1 instance shape
         SpawnNode("Instance on Points", "3D", 320.0f, 40.0f); // 2
         // "Array" rather than "Geometry Op": the operators were split into ten
         // separately registered nodes, and spawning the old name silently
         // failed, leaving this fixture reading past the end of gNodes.
         SpawnNode("Array", "3D", 320.0f, 400.0f);       // 3
         SpawnNode("Camera", "3D", 620.0f, 400.0f);      // 4
         SpawnNode("Light", "3D", 620.0f, 620.0f);       // 5
         SpawnNode("Render 3D", "3D", 900.0f, 40.0f);    // 6

         auto* pts = static_cast<GeometryNode*>(gNodes[0].node.get());
         pts->shape = 3; pts->detail = 24;               // icosphere
         auto* shape = static_cast<GeometryNode*>(gNodes[1].node.get());
         shape->shape = 1;                               // cube
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         inst->pointSource = pts; inst->instanceShape = shape;
         inst->pointMode = 0; inst->instanceScale = 0.07f; inst->maxPoints = 5000;
         inst->color[0] = 1.0f; inst->color[1] = 0.55f; inst->color[2] = 0.25f;
         auto* op = static_cast<GeometryOpNode*>(gNodes[3].node.get());
         op->input = shape; op->op = GeometryOpNode::kArray;
         op->count = 8; op->radial = true; op->radius = 1.6f; op->scaleStep = 0.92f;
         op->inheritMaterial = false;
         op->color[0] = 0.35f; op->color[1] = 0.7f; op->color[2] = 1.0f;
         auto* cam = static_cast<CameraNode*>(gNodes[4].node.get());
         cam->distance = 5.0f; cam->elevation = 25.7831f;
         auto* light = static_cast<LightNode*>(gNodes[5].node.get());
         light->intensity = 1.6f;
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());
         r->geometry[0] = inst; r->geometry[1] = op;
         r->camera = cam; r->lights[0] = light;
         r->width = 700.0f; r->height = 700.0f;
         gNodes[2].showParams = true;
         gNodes[6].showParams = true;
      }
      else if (getenv("INFINITE_MAPTEST") != nullptr)
      {
         SpawnNode("Sphere", "3D", 40.0f, 40.0f);        // 0
         SpawnNode("Material", "3D", 400.0f, 40.0f);     // 1
         SpawnNode("Noise", "Source", 40.0f, 400.0f);    // 2
         SpawnNode("Render 3D", "3D", 760.0f, 40.0f);    // 3
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         mat->input = static_cast<GeometryNode*>(gNodes[0].node.get());
         mat->metallic = 0.4f;
         mat->roughness = 0.5f;
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         render->geometry[0] = mat;
         render->width = 300.0f; render->height = 300.0f;
         render->samples = 0;
      }
      else if (getenv("INFINITE_DISPLACETEST") != nullptr)
      {
         // A subdivided sphere pushed by a Noise texture, end to end through
         // the actual GPU readback path (not a synthetic buffer, unlike the
         // scalar/vector checks in MESHOPTEST) - this is what verifies the
         // texture-to-mesh plumbing itself, not just MeshOps::Displace.
         SpawnNode(getenv("INFINITE_DISPLACE_CUBE") ? "Cube" : "Sphere", "3D", 40.0f, 40.0f); // 0
         SpawnNode("Subdivide", "3D", 320.0f, 40.0f);      // 1
         SpawnNode("Displacement", "3D", 600.0f, 40.0f);   // 2
         SpawnNode("Noise", "Source", 40.0f, 400.0f);      // 3
         SpawnNode("Camera", "3D", 900.0f, 400.0f);        // 4
         SpawnNode("Light", "3D", 900.0f, 620.0f);         // 5
         SpawnNode("Render 3D", "3D", 900.0f, 40.0f);      // 6

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         geo->detail = 24;
         auto* sub = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         sub->op = GeometryOpNode::kSubdivide; sub->input = geo; sub->levels = 2;
         auto* disp = static_cast<DisplacementNode*>(gNodes[2].node.get());
         disp->input = sub;
         disp->mode = DisplacementNode::kScalar;
         disp->strength = 0.35f;
         disp->TextureInput().Connect(gNodes[3].node.get());
         auto* cam = static_cast<CameraNode*>(gNodes[4].node.get());
         cam->distance = 4.0f;
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());
         r->geometry[0] = disp;
         r->camera = cam; r->lights[0] = static_cast<LightNode*>(gNodes[5].node.get());
         r->width = 700.0f; r->height = 700.0f;
         gNodes[2].showParams = true;
         gNodes[6].showParams = true;
      }
      else if (getenv("INFINITE_MAPPINGVIZTEST") != nullptr)
      {
         // Dev-only visual check: a checkerboard (Image Source's built-in
         // fallback when no file is loaded) box-projected onto a cube through
         // Generated coordinates, next to the same cube left on plain UV, so a
         // screenshot shows the per-face orientation directly.
         SpawnNode("Cube", "3D", 40.0f, 40.0f);            // 0
         SpawnNode("Mapping", "3D", 320.0f, 40.0f);        // 1
         SpawnNode("Image Source", "Source", 40.0f, 400.0f); // 2
         SpawnNode("Material", "3D", 600.0f, 40.0f);       // 3
         SpawnNode("Cube", "3D", 40.0f, 700.0f);           // 4 plain UV comparison
         SpawnNode("Material", "3D", 320.0f, 700.0f);      // 5
         SpawnNode("Render 3D", "3D", 900.0f, 40.0f);      // 6
         SpawnNode("Noise", "Source", 40.0f, 1000.0f);     // 7 normal map source

         auto* cube = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mapping = static_cast<MappingNode*>(gNodes[1].node.get());
         mapping->input = cube;
         mapping->space = kMapSpaceGenerated;
         mapping->scaleX = 2.0f; mapping->scaleY = 2.0f; mapping->scaleZ = 2.0f;

         auto* img = static_cast<ImageSourceNode*>(gNodes[2].node.get());
         auto* noise = static_cast<NoiseNode*>(gNodes[7].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[3].node.get());
         mat->input = mapping;
         mat->TextureInput().Connect(img);
         mat->MapInput(kMapNormal).Connect(noise);
         mat->roughness = 0.6f;
         mat->normalStrength = 2.0f;

         auto* cubeB = static_cast<GeometryNode*>(gNodes[4].node.get());
         cubeB->posX = 2.5f;
         auto* matB = static_cast<MaterialNode*>(gNodes[5].node.get());
         matB->input = cubeB;
         matB->TextureInput().Connect(img);
         matB->roughness = 0.6f;

         auto* render = static_cast<Render3DNode*>(gNodes[6].node.get());
         render->geometry[0] = mat;
         render->geometry[1] = matB;
         render->width = 900.0f; render->height = 500.0f;
         render->samples = 0;
         render->camDistance = 6.0f;
         render->camAzimuth = 40.107f;
         render->camElevation = 20.0535f;
         render->targetX = 1.25f;
         gNodes[6].showParams = true;
      }
      else if (getenv("INFINITE_SHADOWTEST") != nullptr)
      {
         // A sphere above a wide flat plane: the arrangement where a shadow is
         // unmistakable if it works and obviously absent if it does not.
         SpawnNode("Plane", "3D", 40.0f, 40.0f);        // 0 ground
         SpawnNode("Sphere", "3D", 40.0f, 400.0f);      // 1 caster
         SpawnNode("Light", "3D", 40.0f, 760.0f);       // 2
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);   // 3

         auto* ground = static_cast<GeometryNode*>(gNodes[0].node.get());
         ground->rotX = -1.5707963f;   // lay it flat
         ground->uniformScale = 6.0f;
         ground->posY = -1.0f;
         auto* ball = static_cast<GeometryNode*>(gNodes[1].node.get());
         ball->posY = 0.4f;
         auto* light = static_cast<LightNode*>(gNodes[2].node.get());
         light->type = 0;              // directional
         light->elevation = 63.0254f;  // high, so the shadow lands on the plane
         light->intensity = 2.0f;

         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         render->geometry[0] = ground;
         render->geometry[1] = ball;
         render->lights[0] = light;
         render->width = 400.0f; render->height = 400.0f;
         render->samples = 0;
         render->shadowsEnabled = false; // turned on mid-test to compare
         render->camElevation = 40.107f;
         render->camDistance = 7.0f;
      }
      else if (getenv("INFINITE_BUGTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);       // 0 -> array -> render
         SpawnNode("Array", "3D", 400.0f, 40.0f);         // 1
         SpawnNode("Render 3D", "3D", 760.0f, 40.0f);     // 2
         SpawnNode("Geometry", "3D", 40.0f, 500.0f);      // 3 -> join
         SpawnNode("Geometry", "3D", 40.0f, 760.0f);      // 4 -> join
         SpawnNode("Join Geometry", "3D", 400.0f, 500.0f); // 5

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* arr = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         arr->input = geo;
         static_cast<Render3DNode*>(gNodes[2].node.get())->geometry[0] = arr;

         auto* ja = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* jb = static_cast<GeometryNode*>(gNodes[4].node.get());
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());
         join->inputs[0] = ja;
         join->inputs[1] = jb;
      }
      else if (getenv("INFINITE_FIXTEST") != nullptr)
      {
         SpawnNode("Random", "Modulators", 40.0f, 40.0f);   // 0
         SpawnNode("Random", "Modulators", 40.0f, 300.0f);  // 1
         SpawnNode("Random", "Modulators", 40.0f, 560.0f);  // 2
         SpawnNode("Geometry", "3D", 400.0f, 40.0f);        // 3
         SpawnNode("Geometry", "3D", 400.0f, 300.0f);       // 4
         SpawnNode("Join Geometry", "3D", 760.0f, 40.0f);   // 5
         SpawnNode("Render 3D", "3D", 1100.0f, 40.0f);      // 6

         auto* a = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* b = static_cast<GeometryNode*>(gNodes[4].node.get());
         a->posX = -1.0f;
         b->posX = 1.5f; b->shape = 2;
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());
         join->inputs[0] = a;
         join->inputs[1] = b;
         static_cast<Render3DNode*>(gNodes[6].node.get())->geometry[0] = join;
      }
      else if (getenv("INFINITE_CLOTHTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);     // 0
         SpawnNode("Cloth", "3D", 400.0f, 40.0f);       // 1
         SpawnNode("Render 3D", "3D", 760.0f, 40.0f);   // 2
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         geo->shape = 0;    // plane
         geo->detail = 16;
         geo->rotX = -1.5707963f; // stand it up so gravity has something to do
         auto* cloth = static_cast<ClothNode*>(gNodes[1].node.get());
         cloth->input = geo;
         cloth->pinMode = ClothNode::kPinTop;
         static_cast<Render3DNode*>(gNodes[2].node.get())->geometry[0] = cloth;
      }
      else if (getenv("INFINITE_PARTICLETEST") != nullptr)
      {
         SpawnNode("Particle System", "3D", 40.0f, 40.0f);      // 0
         SpawnNode("Geometry", "3D", 40.0f, 500.0f);            // 1 instanced shape
         SpawnNode("Instance on Points", "3D", 400.0f, 40.0f);  // 2
         SpawnNode("Render 3D", "3D", 760.0f, 40.0f);           // 3

         auto* ps = static_cast<ParticleSystemNode*>(gNodes[0].node.get());
         ps->emitRate = 600.0f;
         ps->lifetime = 2.0f;
         ps->seed = 7.0f;
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         inst->cloudSource = ps;
         inst->instanceShape = static_cast<GeometryNode*>(gNodes[1].node.get());
         inst->instanceScale = 0.04f;
         static_cast<Render3DNode*>(gNodes[3].node.get())->geometry[0] = inst;
      }
      else if (getenv("INFINITE_AUDIORECTEST") != nullptr)
      {
         SpawnNode("Shape", "Source", 40.0f, 40.0f);       // 0
         SpawnNode("Output", "Output", 320.0f, 40.0f);     // 1
         SpawnNode("Audio File", "Modulators", 40.0f, 400.0f); // 2
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         auto* audio = static_cast<AudioFileNode*>(gNodes[2].node.get());
         audio->Open(getenv("INFINITE_AUDIORECTEST"));
         audio->monitor = false; // silent while the test runs
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         out->includeAudio = true;
         out->audioSource = audio;
         out->recordFps = 30;
      }
      else if (getenv("INFINITE_PATCHTEST") != nullptr)
      {
         // A patch touching every kind of connection: image cables, a geometry
         // chain, camera and light pins, and a modulation binding.
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);        // 0
         SpawnNode("Smooth", "3D", 320.0f, 40.0f);         // 1
         SpawnNode("Material", "3D", 600.0f, 40.0f);       // 2
         SpawnNode("Camera", "3D", 320.0f, 400.0f);        // 3
         SpawnNode("Light", "3D", 320.0f, 620.0f);         // 4
         SpawnNode("Render 3D", "3D", 880.0f, 40.0f);      // 5
         SpawnNode("invert", "Color", 1160.0f, 40.0f);     // 6
         SpawnNode("Output", "Output", 1440.0f, 40.0f);    // 7
         SpawnNode("Path", "Modulators", 40.0f, 800.0f);   // 8
         SpawnNode("Audio File", "Modulators", 40.0f, 1000.0f); // 9

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* smooth = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[5].node.get());
         auto* path = static_cast<PathNode*>(gNodes[8].node.get());
         auto* audioFile = static_cast<AudioFileNode*>(gNodes[9].node.get());
         audioFile->Open("/tmp/models/tone.wav");
         audioFile->monitor = false;

         geo->shape = 4; geo->detail = 33; geo->posX = 1.25f;
         geo->color[0] = 0.11f; geo->color[1] = 0.22f; geo->color[2] = 0.33f;
         geo->emission = 2.5f;
         smooth->iterations = 7; smooth->amount = 0.66f;
         mat->metallic = 0.77f; mat->roughness = 0.11f;
         render->samples = 3; render->exposure = 1.8f; render->width = 512.0f;
         path->shape = PathNode::kHelix; path->turns = 5.0f; path->pingPong = true;

         smooth->input = geo;
         mat->input = smooth;
         render->geometry[0] = mat;
         render->camera = static_cast<CameraNode*>(gNodes[3].node.get());
         render->lights[0] = static_cast<LightNode*>(gNodes[4].node.get());
         CableFor(gNodes[6], 0)->Connect(gNodes[5].node.get());
         CableFor(gNodes[7], 0)->Connect(gNodes[6].node.get());
         auto* outNode = static_cast<OutputNode*>(gNodes[7].node.get());
         outNode->includeAudio = true;
         outNode->audioSource = audioFile;
         Modulation::Instance().Bind(gNodes[0].index, 6, gNodes[8].index, 2);
      }
      else if (getenv("INFINITE_MATFRAMETEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);     // 0
         SpawnNode("Material", "3D", 320.0f, 40.0f);    // 1
         SpawnNode("Render 3D", "3D", 620.0f, 40.0f);   // 2
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         geo->posX = 4.0f; geo->posY = 2.0f; geo->uniformScale = 2.0f;
         mat->input = geo;
         static_cast<Render3DNode*>(gNodes[2].node.get())->geometry[0] = mat;
      }
      else if (getenv("INFINITE_ENVTEST") != nullptr)
      {
         // A tiny synthetic equirectangular HDR, one flat bright warm colour
         // across the whole sphere of directions - deterministic regardless
         // of which way the test camera happens to be looking - written to a
         // real .hdr file so this exercises the actual stb_image decode path
         // rather than poking EnvironmentNode's private texture upload
         // directly.
         const int ew = 16, eh = 8;
         std::vector<float> envPixels((size_t)ew * eh * 3);
         for (int i = 0; i < ew * eh; i++)
         {
            envPixels[i * 3 + 0] = 8.0f;
            envPixels[i * 3 + 1] = 5.0f;
            envPixels[i * 3 + 2] = 2.0f;
         }
         // One blown-out "sun" texel, far above half-float range, exactly like
         // the sun in a real HDRI. It survives the float32 file fine but turns
         // into +Inf when converted into the 16F texture unless Upload clamps
         // it - and glGenerateMipmap then averages that Inf up into every
         // higher mip as NaN, which is what used to render the lit geometry
         // solid black (the diffuse term always samples the topmost mip).
         envPixels[0] = envPixels[1] = envPixels[2] = 1.0e30f;
         const std::string envPath = "/tmp/infinite_envtest.hdr";
         stbi_write_hdr(envPath.c_str(), ew, eh, 3, envPixels.data());

         SpawnNode("Geometry", "3D", 40.0f, 40.0f);     // 0 - the reflective sphere
         SpawnNode("Material", "3D", 320.0f, 40.0f);    // 1
         SpawnNode("HDRI", "3D", 40.0f, 400.0f);        // 2
         SpawnNode("Render 3D", "3D", 620.0f, 40.0f);   // 3
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         auto* env = static_cast<EnvironmentNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         geo->shape = 2; // sphere
         mat->input = geo;
         mat->metallic = 1.0f;
         mat->roughness = 0.03f; // near-mirror, so mip 0 dominates the reflection
         env->Load(envPath);
         render->geometry[0] = mat;
         render->envInput.Connect(env);
         render->width = 400.0f; render->height = 400.0f;
         render->samples = 0;
      }
      else if (getenv("INFINITE_PATHOCEANTEST") != nullptr)
      {
         SpawnNode("Path", "Modulators", 40.0f, 40.0f);   // 0
         SpawnNode("Ocean", "3D", 40.0f, 400.0f);         // 1
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);     // 2
         static_cast<Render3DNode*>(gNodes[2].node.get())->geometry[0] =
            static_cast<OceanNode*>(gNodes[1].node.get());
      }
      else if (getenv("INFINITE_UTILTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);        // 0
         SpawnNode("Null 3D", "3D", 300.0f, 40.0f);        // 1
         SpawnNode("Mesh to Points", "3D", 560.0f, 40.0f); // 2
         SpawnNode("Render 3D", "3D", 820.0f, 40.0f);      // 3
         SpawnNode("Shape", "Source", 40.0f, 500.0f);      // 4
         SpawnNode("Null", "Compositing", 300.0f, 500.0f); // 5
         SpawnNode("Output", "Output", 560.0f, 500.0f);    // 6

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* null3d = static_cast<Null3DNode*>(gNodes[1].node.get());
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[2].node.get());
         null3d->input = geo;
         m2p->input = null3d;
         static_cast<Render3DNode*>(gNodes[3].node.get())->geometry[0] = m2p;
         CableFor(gNodes[5], 0)->Connect(gNodes[4].node.get());
         CableFor(gNodes[6], 0)->Connect(gNodes[5].node.get());
      }
      else if (getenv("INFINITE_PALETTETEST") != nullptr)
      {
         // A Ramp makes a deterministic, strongly coloured reference without
         // needing an image file on disk, so the check runs anywhere.
         SpawnNode("Ramp", "Source", 40.0f, 40.0f);       // 0 reference
         SpawnNode("Palette", "Modulators", 340.0f, 40.0f); // 1
         SpawnNode("Ramp", "Source", 640.0f, 40.0f);      // 2 target, driven by 1
         SpawnNode("Palette", "Modulators", 40.0f, 560.0f);  // 3 same input, same seed

         auto* reference = static_cast<RampNode*>(gNodes[0].node.get());
         reference->stopCount = 3;
         reference->stopPos[0] = 0.0f;
         reference->stopPos[1] = 0.5f;
         reference->stopPos[2] = 1.0f;
         const float bands[3][3] = {
            { 0.90f, 0.10f, 0.10f }, { 0.10f, 0.85f, 0.15f }, { 0.15f, 0.20f, 0.95f }
         };
         for (int i = 0; i < 3; i++)
            for (int c = 0; c < 3; c++)
               reference->stopColor[i][c] = bands[i][c];

         for (int n : { 1, 3 })
         {
            auto* palette = static_cast<PaletteNode*>(gNodes[n].node.get());
            palette->swatchCount = 3;
            CableFor(gNodes[n], 0)->Connect(gNodes[0].node.get());
         }

         // Params have to be drawn for colour pins to register, which is what
         // the binding pass walks - and a Ramp only draws as many swatches as
         // it has stops.
         static_cast<RampNode*>(gNodes[2].node.get())->stopCount = 3;
         gNodes[2].showParams = true;
         gNodes[1].showParams = true;
      }
      else if (getenv("INFINITE_TEXT3DTEST") != nullptr)
      {
         SpawnNode("Text 3D", "3D", 40.0f, 40.0f);
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);
         static_cast<Render3DNode*>(gNodes[1].node.get())->geometry[0] =
            static_cast<Text3DNode*>(gNodes[0].node.get());
      }
      // Nothing is pre-spawned in slash mode: the point of that check is that
      // pressing "/" on an empty canvas is what produces the comment.
      else if (getenv("INFINITE_COMMENTTEST") != nullptr &&
               std::string(getenv("INFINITE_COMMENTTEST")) != "slash")
      {
         GraphNode* gn = SpawnNode("Comment", "Compositing", 60.0f, 60.0f);
         auto* c = static_cast<CommentNode*>(gn->node.get());
         // Middle line deliberately too long for the box, so the check covers
         // wrapping and clipping and not just three short lines.
         c->text = "Phase G\nfloating cubes instanced on the drift field, "
                   "then graded\nTODO: add fog";
         c->width = 260.0f; c->height = 140.0f;
         c->color[0] = 0.95f; c->color[1] = 0.85f; c->color[2] = 0.45f;
         gn->showParams = true; // exercise DrawCommentParams too, not just the preview
      }
      else if (getenv("INFINITE_GROUPTEST") != nullptr)
      {
         // Three nodes in a row for the group auto-fit check driven below.
         SpawnNode("Shape", "Source", 100.0f, 100.0f);  // 0
         SpawnNode("Shape", "Source", 400.0f, 100.0f);  // 1
         SpawnNode("Shape", "Source", 700.0f, 100.0f);  // 2
      }
      else if (const char* samplePath = getenv("INFINITE_BUILDSAMPLE"))
      {
         // A demo patch: a source cube continuously reshaped by Resynthesize
         // 3D, instanced onto a slow-drifting particle field so many
         // independent "floating cubes" share one animated source shape, then
         // graded through a five-stage 2D chain before Output.
         SpawnNode("Cube", "3D", 40.0f, 40.0f);                    // 0 source shape
         SpawnNode("Resynthesize 3D", "3D", 320.0f, 40.0f);        // 1 continuous morph
         SpawnNode("Particle System", "3D", 40.0f, 420.0f);        // 2 drift field
         SpawnNode("Instance on Points", "3D", 320.0f, 420.0f);    // 3 stamp shape at each particle
         SpawnNode("Camera", "3D", 40.0f, 760.0f);                 // 4
         SpawnNode("Light", "3D", 40.0f, 920.0f);                  // 5 key
         SpawnNode("Light", "3D", 40.0f, 1080.0f);                 // 6 fill/rim
         SpawnNode("Render 3D", "3D", 620.0f, 420.0f);             // 7
         SpawnNode("vibrance", "Color", 900.0f, 420.0f);           // 8
         SpawnNode("colorbalance", "Color", 900.0f, 560.0f);       // 9
         SpawnNode("bloom", "Effects", 900.0f, 700.0f);            // 10
         SpawnNode("vignette", "Effects", 900.0f, 840.0f);         // 11
         SpawnNode("brightnesscontrast", "Color", 900.0f, 980.0f); // 12
         SpawnNode("Output", "Output", 1180.0f, 420.0f);           // 13

         auto* cube = static_cast<GeometryNode*>(gNodes[0].node.get());
         cube->detail = 32;

         auto* resynth = static_cast<MeshResynthNode*>(gNodes[1].node.get());
         resynth->input = cube;
         resynth->weight[MeshResynthNode::kDisplace] = 0.55f;
         resynth->weight[MeshResynthNode::kJitter] = 0.10f;
         resynth->weight[MeshResynthNode::kSmooth] = 0.40f;
         resynth->weight[MeshResynthNode::kTwist] = 0.30f;
         resynth->weight[MeshResynthNode::kBulge] = 0.40f;
         resynth->weight[MeshResynthNode::kExtrudeFaces] = 0.05f;
         resynth->weight[MeshResynthNode::kSubdivide] = 0.05f;
         resynth->weight[MeshResynthNode::kSquash] = 0.15f;
         resynth->chaos = 0.4f;
         resynth->autoStep = true;
         resynth->stepsPerBeat = 0.5f;
         resynth->seed = 17.0f;
         resynth->triangleBudget = 40000;

         auto* particles = static_cast<ParticleSystemNode*>(gNodes[2].node.get());
         particles->maxParticles = 10;
         particles->emitRate = 4.0f;
         particles->emitShape = ParticleSystemNode::kSphere;
         particles->emitRadius = 1.6f;
         particles->lifetime = 60.0f;
         particles->lifetimeRandom = 8.0f;
         particles->initialSpeed = 0.15f;
         particles->speedRandom = 0.10f;
         particles->spread = 1.0f; // omnidirectional - "floating", not "launched"
         particles->gravityX = 0.0f; particles->gravityY = -0.02f; particles->gravityZ = 0.0f;
         particles->drag = 0.15f;
         particles->turbulence = 0.35f;
         particles->turbulenceScale = 0.8f;
         particles->startSize = 1.0f;
         particles->endSize = 1.0f; // constant - reads as floating cubes, not dying particles
         particles->startColor[0] = 1.0f; particles->startColor[1] = 0.55f; particles->startColor[2] = 0.25f;
         particles->endColor[0] = 0.25f; particles->endColor[1] = 0.55f; particles->endColor[2] = 1.0f;
         particles->seed = 4.0f;

         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[3].node.get());
         inst->instanceShape = resynth;
         inst->cloudSource = particles;
         inst->instanceScale = 0.32f;
         inst->scaleRandom = 0.5f;
         inst->rotationRandom = 1.0f;
         inst->alignToNormal = true; // orients each cube along its drift direction
         inst->metallic = 0.35f;
         inst->roughness = 0.35f;
         inst->emissionColor[0] = 1.0f; inst->emissionColor[1] = 0.85f; inst->emissionColor[2] = 0.6f;
         inst->emission = 0.02f; // let lighting and bloom's own threshold define the highlights,
                                  // not a flat self-glow blowing out the whole surface
         inst->seed = 9.0f;

         auto* cam = static_cast<CameraNode*>(gNodes[4].node.get());
         cam->distance = 3.4f; cam->azimuth = 40.107f; cam->elevation = 20.0535f;
         cam->fov = 46.0f; cam->orbitPerBeat = 0.05f; // slow cinematic turntable, no keyframing needed

         auto* keyLight = static_cast<LightNode*>(gNodes[5].node.get());
         keyLight->azimuth = 51.5662f; keyLight->elevation = 57.2958f;
         keyLight->color[0] = 1.0f; keyLight->color[1] = 0.92f; keyLight->color[2] = 0.8f;
         keyLight->intensity = 1.4f; keyLight->orbitPerBeat = 0.02f;

         auto* fillLight = static_cast<LightNode*>(gNodes[6].node.get());
         fillLight->azimuth = -126.0507f; fillLight->elevation = 28.6479f;
         fillLight->color[0] = 0.55f; fillLight->color[1] = 0.7f; fillLight->color[2] = 1.0f;
         fillLight->intensity = 0.6f; fillLight->orbitPerBeat = -0.015f; // drifts the opposite way for parallax

         auto* render = static_cast<Render3DNode*>(gNodes[7].node.get());
         render->geometry[0] = inst;
         render->camera = cam;
         render->lights[0] = keyLight;
         render->lights[1] = fillLight;
         render->width = 1280.0f; render->height = 720.0f;
         render->samples = 2; render->tonemap = 1; render->exposure = 1.1f;
         // A dark teal, not pure black: the warm cube light/emission needs a cool
         // backdrop to read as graded contrast rather than glowing blobs on a void.
         render->bgColor[0] = 0.03f; render->bgColor[1] = 0.045f; render->bgColor[2] = 0.07f;
         render->envSky[0] = 0.14f; render->envSky[1] = 0.20f; render->envSky[2] = 0.32f;
         render->envHorizon[0] = 0.07f; render->envHorizon[1] = 0.09f; render->envHorizon[2] = 0.13f;
         render->envGround[0] = 0.02f; render->envGround[1] = 0.025f; render->envGround[2] = 0.035f;
         render->envIntensity = 0.9f;
         render->ambientColor[0] = 0.10f; render->ambientColor[1] = 0.16f; render->ambientColor[2] = 0.26f;
         render->rimIntensity = 0.5f;
         render->shadowsEnabled = true;
         render->shadowQuality = 1;
         render->shadowStrength = 0.5f;

         auto* vibrance = static_cast<FilterNode*>(gNodes[8].node.get());
         vibrance->Input().Connect(render);
         vibrance->SetParamValue(0, 0, 0.5f); // Amount

         auto* colorbalance = static_cast<FilterNode*>(gNodes[9].node.get());
         colorbalance->Input().Connect(vibrance);
         colorbalance->SetParamValue(0, 0, 0.05f);  // Cyan-Red: push warm
         colorbalance->SetParamValue(1, 0, 0.0f);   // Magenta-Green
         colorbalance->SetParamValue(2, 0, -0.05f); // Yellow-Blue: push toward yellow

         auto* bloom = static_cast<FilterNode*>(gNodes[10].node.get());
         bloom->Input().Connect(colorbalance);
         bloom->SetParamValue(0, 0, 0.72f); // Threshold - only genuine highlights bloom
         bloom->SetParamValue(1, 0, 0.9f);  // Intensity
         bloom->SetParamValue(2, 0, 4.0f);  // Radius

         auto* vignette = static_cast<FilterNode*>(gNodes[11].node.get());
         vignette->Input().Connect(bloom);

         auto* bc = static_cast<FilterNode*>(gNodes[12].node.get());
         bc->Input().Connect(vignette);
         bc->SetParamValue(0, 0, 0.02f); // Brightness
         bc->SetParamValue(1, 0, 0.15f); // Contrast

         auto* out = static_cast<OutputNode*>(gNodes[13].node.get());
         out->Input().Connect(bc);

         for (GraphNode& gn : gNodes)
            gn.showParams = false;

         // Fast-forward the transport so particles have already spread out and
         // the cube has already morphed a few generations before the first
         // frame anyone sees, rather than opening on an empty, unshaped scene.
         Transport::Instance().SetPlaying(true);
         Transport::Instance().Rewind();
         for (int i = 0; i < 240; i++)
         {
            out->CookIfNeeded(9600 + i);
            Transport::Instance().Tick(1.0f / 30.0f);
         }

         std::string error;
         if (!SavePatchTo(samplePath))
            fprintf(stderr, "sample patch: failed to write %s\n", samplePath);
         else
         {
            Patch::NoteRecent(samplePath);
            printf("sample patch written: %s\n", samplePath);
         }

         if (const char* pngPath = getenv("INFINITE_BUILDSAMPLE_PNG"))
         {
            ExportPng(out, pngPath);
            printf("sample preview written: %s\n", pngPath);
         }
      }
      else if (getenv("INFINITE_MODELTEST") != nullptr)
      {
         SpawnNode("Model 3D", "3D", 40.0f, 40.0f);
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);
         auto* model = static_cast<ModelSourceNode*>(gNodes[0].node.get());
         static_cast<Render3DNode*>(gNodes[1].node.get())->geometry[0] = model;
      }
      else if (getenv("INFINITE_MESHOPTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);
         SpawnNode("Render 3D", "3D", 700.0f, 40.0f);
      }
      else if (getenv("INFINITE_3DTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);
         SpawnNode("Geometry", "3D", 40.0f, 500.0f);
         SpawnNode("Render 3D", "3D", 360.0f, 40.0f);
         // A textured surface, so the mipmap/anisotropy path is exercised too.
         SpawnNode("Noise", "Source", 40.0f, 900.0f);
         auto* g0 = static_cast<GeometryNode*>(gNodes[0].node.get());
         g0->shape = 7; g0->color[0] = 1.0f; g0->color[1] = 0.45f; g0->color[2] = 0.2f;
         g0->uniformScale = 1.5f;
         auto* g1 = static_cast<GeometryNode*>(gNodes[1].node.get());
         g1->shape = 3; g1->posX = 0.9f; g1->posY = -0.35f; g1->uniformScale = 0.7f;
         g1->color[0] = 0.35f; g1->color[1] = 0.7f; g1->color[2] = 1.0f;
         auto* r = static_cast<Render3DNode*>(gNodes[2].node.get());
         r->geometry[0] = g0;
         r->geometry[1] = g1;
         g0->TextureInput().Connect(gNodes[3].node.get());
         r->width = 700.0f; r->height = 700.0f;
         r->samples = 0; // the antialias check below turns it on at frame 4
         if (getenv("INFINITE_NOCULL") != nullptr)
            r->backfaceCull = false;
         if (getenv("INFINITE_NODEPTH") != nullptr)
            r->depthTest = false;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[0].showParams = false;
         gNodes[1].showParams = false;
      }
      else if (getenv("INFINITE_PHASE1TEST") != nullptr)
      {
         // Points-render-as-points, phase 1: a point cloud reaches Render 3D
         // as points and draws as camera-facing sprites.
         //
         // Slot 0: Cube -> Mesh to Points -> Render 3D. Dual-interface source
         // (IGeometrySource + IPointCloudSource); the cloud must win the draw.
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);              // 0: cube
         SpawnNode("Mesh to Points", "3D", 300.0f, 40.0f);       // 1
         // Slot 1: Particle System -> Render 3D directly, no IGeometrySource
         // on either side - this connection could not exist before this
         // change, and the render must not freeze on its first frame.
         SpawnNode("Particle System", "3D", 40.0f, 300.0f);      // 2
         // Slot 2: Image to Points -> Render 3D - still shows image colours
         // per point once drawn as sprites rather than swatch quads.
         SpawnNode("Noise", "Source", 40.0f, 560.0f);            // 3
         SpawnNode("Image to Points", "3D", 300.0f, 560.0f);     // 4
         SpawnNode("Render 3D", "3D", 560.0f, 300.0f);           // 5

         auto* cube = static_cast<GeometryNode*>(gNodes[0].node.get());
         cube->shape = 1; // Cube
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[1].node.get());
         m2p->input = cube;
         m2p->pointSize = 0.3f; // large enough to be visible at the default camera distance
         auto* particles = static_cast<ParticleSystemNode*>(gNodes[2].node.get());
         particles->emitRate = 400.0f;
         particles->startSize = 0.5f; particles->endSize = 0.5f;
         auto* i2p = static_cast<ImageToPointsNode*>(gNodes[4].node.get());
         i2p->Input().Connect(gNodes[3].node.get());
         i2p->pointSize = 0.3f;
         auto* r = static_cast<Render3DNode*>(gNodes[5].node.get());
         r->geometry[0] = m2p; r->clouds[0] = m2p;
         r->clouds[1] = particles; // cloud-only: geometry[1] intentionally left null
         r->geometry[2] = i2p; r->clouds[2] = i2p;
         r->width = 400.0f; r->height = 400.0f;
         r->samples = 0;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (getenv("INFINITE_TEXTFIT") != nullptr)
      {
         SpawnNode("Text", "Text", 40.0f, 40.0f);
         auto* t = static_cast<TextNode*>(gNodes[0].node.get());
         t->text = "naman is a weirdo and this line is deliberately long enough to need several rows";
         t->fontName = "Verdana";
         t->fontSize = 300.0f;   // absurd on purpose: fitting must rein it in
         t->wordWrap = true;
         t->fitToBox = true;
         t->align = 3;
         t->scaleX = 1.4f;
         t->scaleY = 2.2f;
         gNodes[0].showParams = true;
         printf("TEXTFIT fixture: %zu nodes spawned\n", gNodes.size());
      }
      else if (getenv("INFINITE_SHOWCASE4") != nullptr)
      {
         SpawnNode("Reaction Diffusion", "Feedback", 40.0f, 40.0f);
         SpawnNode("Curves", "Color", 300.0f, 40.0f);
         SpawnNode("Shape", "Source", 560.0f, 40.0f);
         SpawnNode("Trails", "Feedback", 820.0f, 40.0f);
         SpawnNode("Resynthesize", "Resynth", 1080.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[2].node.get());
         CableFor(gNodes[4], 0)->Connect(gNodes[0].node.get());
         auto* cv = static_cast<CurvesNode*>(gNodes[1].node.get());
         cv->AddPoint(CurvesNode::kRGB, 0.35f, 0.75f);
         cv->AddPoint(CurvesNode::kRGB, 0.7f, 0.2f);
         auto* tr = static_cast<TrailsNode*>(gNodes[3].node.get());
         tr->zoom = 1.02f; tr->rotate = 0.5730f; tr->decay = 0.96f;
         auto* rd = static_cast<ReactionDiffusionNode*>(gNodes[0].node.get());
         rd->ApplyPreset(0);
         rd->stepsPerFrame = 24.0f;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[2].showParams = false;
      }
      else if (getenv("INFINITE_SHOWCASE3") != nullptr)
      {
         SpawnNode("Shape", "Source", 40.0f, 40.0f);
         SpawnNode("kaleidoscope", "Effects", 300.0f, 40.0f);
         SpawnNode("Macro Knob", "Modulators", 560.0f, 40.0f);
         SpawnNode("Macro XY", "Modulators", 820.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         gNodes[1].showParams = true;
         gNodes[2].showParams = true;
         gNodes[3].showParams = true;
         static_cast<MacroXYNode*>(gNodes[3].node.get())->padX = 0.32f;
         static_cast<MacroXYNode*>(gNodes[3].node.get())->padY = 0.68f;
      }
      else if (getenv("INFINITE_SHOWCASE2") != nullptr)
      {
         SpawnNode("Noise", "Source", 40.0f, 40.0f);
         SpawnNode("kaleidoscope", "Effects", 300.0f, 40.0f);
         SpawnNode("bloom", "Effects", 560.0f, 40.0f);
         SpawnNode("Switcher", "Compositing", 820.0f, 40.0f);
         SpawnNode("Pattern", "Modulators", 1080.0f, 40.0f);
         SpawnNode("Math", "Modulators", 1340.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[2], 0)->Connect(gNodes[1].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[2].node.get());
         CableFor(gNodes[3], 1)->Connect(gNodes[0].node.get());
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (getenv("INFINITE_SHOWCASE") != nullptr)
      {
         // dev-only: a representative patch, used to generate the README image
         SpawnNode("Shape", "Source", 40.0f, 40.0f);
         SpawnNode("glitch", "Effects", 320.0f, 40.0f);
         SpawnNode("Text", "Text", 600.0f, 40.0f);
         SpawnNode("Layer Stack", "Compositing", 880.0f, 40.0f);
         SpawnNode("Output", "Output", 1160.0f, 40.0f);
         SpawnNode("LFO", "Modulators", 320.0f, 560.0f);

         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         shape->shapeType = 6;
         shape->sides = 7;
         shape->size = 0.36f;
         shape->fillColor[0] = 1.0f; shape->fillColor[1] = 0.42f; shape->fillColor[2] = 0.2f;
         auto* txt = static_cast<TextNode*>(gNodes[2].node.get());
         txt->text = "INFINITE";
         txt->fontSize = 150.0f;

         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[1].node.get());
         CableFor(gNodes[3], 1)->Connect(gNodes[2].node.get());
         CableFor(gNodes[4], 0)->Connect(gNodes[3].node.get());

         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[2].showParams = false;
         gNodes[4].showParams = false;
      }
      else if (wantsFixture)
      {
         SpawnNode("Shape", "Source", 60.0f, 60.0f);
         if (getenv("INFINITE_RESYNTHTEST") != nullptr)
         {
            SpawnNode("Resynthesize", "Resynth", 380.0f, 60.0f);
            CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
            gNodes[1].showParams = true;
         }
         else
            SpawnNode("Output", "Output", 380.0f, 60.0f);
         if (getenv("INFINITE_RECTEST") != nullptr)
            CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         if (getenv("INFINITE_HIDETEST") != nullptr)
         {
            SpawnNode("LFO", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true;
         }
         if (getenv("INFINITE_MODTEST") != nullptr)
         {
            SpawnNode("LFO", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true; // params must be drawn for them to register
         }
         if (getenv("INFINITE_MACROTEST") != nullptr)
         {
            SpawnNode("Macro XY", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true;
         }
      }
   }

   // Cocoa chdir's a bundled app to Contents/Resources, so a bare relative path
   // would silently write inside the .app. Default somewhere the user can find.
   char exportPath[512] = "";
   if (const char* home = getenv("HOME"))
      snprintf(exportPath, sizeof(exportPath), "%s/Desktop/infinite_output.png", home);
   else
      snprintf(exportPath, sizeof(exportPath), "infinite_output.png");

   char recordPath[512] = "";
   if (const char* home = getenv("HOME"))
      snprintf(recordPath, sizeof(recordPath), "%s/Desktop/infinite_output.mov", home);
   else
      snprintf(recordPath, sizeof(recordPath), "infinite_output.mov");

   char searchBuf[128] = "";
   bool searchJustOpened = false;
   std::vector<std::string> clipboard;      // typeNames copied
   std::vector<INode*> clipboardSources;    // live sources to copy params from
   std::vector<int> clipboardOrigIndex;     // gNodes index each item had at copy time
   std::vector<int> clipboardOrigGroup;     // that item's owning group's index, or -1
   int frameId = 0;

   while (!glfwWindowShouldClose(window))
   {
      gFrameStart = glfwGetTime();
      glfwPollEvents();

      // Actually tear down anything retired by RemoveNodeByIndex last frame.
      // Safe here: the frame that queued draw commands referencing these
      // textures has already been submitted and presented.
      gRetiredNodes.clear();
      gRetiredViewports.clear();

      // dev-only: drive copy/paste/delete with synthetic key events so the
      // shortcuts can be verified without a human at the keyboard
      if (getenv("INFINITE_INPUTTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         auto key = [&tio](ImGuiKey k, bool down) { tio.AddKeyEvent(k, down); };
         switch (frameId)
         {
            case 3: key(ImGuiMod_Super, true); key(ImGuiKey_C, true); break;
            case 4: key(ImGuiKey_C, false); break;
            case 5: key(ImGuiKey_V, true); break;
            case 6: key(ImGuiKey_V, false); key(ImGuiMod_Super, false); break;
            case 8: key(ImGuiKey_Backspace, true); break;
            case 9: key(ImGuiKey_Backspace, false); break;
            default: break;
         }
      }

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();

      // dev-only: click inside the colour picker to prove it is interactive now
      if (getenv("INFINITE_COLORTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true); // headless runs are never OS-focused; ImGui drops input otherwise
         if (frameId >= 8 && gColorPickerRect.z > 0.0f)
            tio.AddMousePosEvent(gColorPickerRect.x + gColorPickerRect.z * 0.25f,
                                 gColorPickerRect.y + gColorPickerRect.w * 0.30f);
         if (frameId == 9)
            tio.AddMouseButtonEvent(0, true);
         if (frameId == 11)
            tio.AddMouseButtonEvent(0, false);
      }

      // dev-only: the whole "/" flow with nothing else touched - press slash on
      // an empty canvas and type. This is the path that has to stay one
      // keystroke, so it is driven exactly as a person would drive it.
      if (const char* cmode = getenv("INFINITE_COMMENTTEST"))
      {
         if (std::string(cmode) == "slash")
         {
            ImGuiIO& tio = ImGui::GetIO();
            tio.ConfigInputTrickleEventQueue = false;
            tio.AddFocusEvent(true); // headless runs are never OS-focused
            if (frameId == 4)
            {
               tio.AddKeyEvent(ImGuiKey_Slash, true);
               tio.AddInputCharacter('/'); // a real keyboard sends both
            }
            if (frameId == 5)
               tio.AddKeyEvent(ImGuiKey_Slash, false);
            // Typing starts immediately after: no click anywhere in between.
            if (frameId == 7)
            {
               for (char ch : std::string("lighting"))
                  tio.AddInputCharacter(ch);
            }
            if (frameId == 9)
               tio.AddKeyEvent(ImGuiKey_Enter, true);
            if (frameId == 10)
               tio.AddKeyEvent(ImGuiKey_Enter, false);
            if (frameId == 12)
            {
               for (char ch : std::string("rim light too hot"))
                  tio.AddInputCharacter(ch);
            }
         }
      }

      // dev-only: double-click an existing note, the way an edit (rather than a
      // fresh comment) starts.
      if (const char* cmode = getenv("INFINITE_COMMENTTEST"))
      {
         if (std::string(cmode) == "edit")
         {
            ImGuiIO& tio = ImGui::GetIO();
            tio.ConfigInputTrickleEventQueue = false;
            tio.AddFocusEvent(true); // headless runs are never OS-focused
            if (frameId >= 4 && gCommentBodyRect.z > 0.0f)
               tio.AddMousePosEvent(gCommentBodyRect.x + gCommentBodyRect.z * 0.5f,
                                    gCommentBodyRect.y + gCommentBodyRect.w * 0.5f);
            // Two clicks a few frames apart: at 60fps that is well inside
            // ImGui's double-click window.
            if (frameId == 5 || frameId == 7)
               tio.AddMouseButtonEvent(0, true);
            if (frameId == 6 || frameId == 8)
               tio.AddMouseButtonEvent(0, false);
            // Then type, including a Return: a note whose editor cannot add a
            // line is no better than the single-line field this replaced. Left
            // a few frames after the click so the mouse has finished with the
            // node - while the button is still down the editor holds the active
            // item and the text field cannot take the keyboard.
            if (frameId == 14)
               tio.AddInputCharacter('!');
            if (frameId == 16)
               tio.AddKeyEvent(ImGuiKey_Enter, true);
            if (frameId == 17)
               tio.AddKeyEvent(ImGuiKey_Enter, false);
         }
      }

      // dev-only: synthetic mouse drags to prove empty-canvas drag pans the view
      // while a drag that starts on a node still moves that node. The position is
      // re-asserted every frame, otherwise the GLFW backend's real cursor wins on
      // frames we don't touch and the gesture breaks up.
      if (getenv("INFINITE_DRAGTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         // ImGui normally spreads queued input across frames to preserve click
         // positions; that splits a synthetic gesture apart, so disable it here.
         tio.ConfigInputTrickleEventQueue = false;
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };
         // Hardcoded absolute pixels (e.g. 1400,800) assumed the requested
         // 1600x1000 window; the actual window can come up smaller than
         // requested (display-clamped), which put the drag start and later
         // points off-screen entirely - ImGui never saw the canvas as
         // hovered, the window lost focus, and NavigateAction never armed.
         // Anchor to the real display size instead.
         const ImVec2 dispSize = tio.DisplaySize;
         const ImVec2 dragBase(dispSize.x * 0.75f, dispSize.y * 0.70f);
         switch (frameId)
         {
            // --- phase 1: drag empty canvas (should pan, not move nodes) ---
            case 3: gTestMouse = dragBase; break;
            case 4: btn(true); break;
            case 5: gTestMouse = ImVec2(dragBase.x + 40.0f, dragBase.y + 30.0f); break;
            case 6: gTestMouse = ImVec2(dragBase.x + 100.0f, dragBase.y + 80.0f); break;
            case 7: gTestMouse = ImVec2(dragBase.x + 160.0f, dragBase.y + 120.0f); break;
            case 8: btn(false); break;
            // --- phase 2: drag the node's title row (should move the node) ---
            case 12: gTestMouse = gDragTestNodeScreen; break;
            case 13: btn(true); break;
            case 14: gTestMouse = ImVec2(gDragTestNodeScreen.x + 40.0f, gDragTestNodeScreen.y + 30.0f); break;
            case 15: gTestMouse = ImVec2(gDragTestNodeScreen.x + 90.0f, gDragTestNodeScreen.y + 70.0f); break;
            case 16: gTestMouse = ImVec2(gDragTestNodeScreen.x + 140.0f, gDragTestNodeScreen.y + 110.0f); break;
            case 17: btn(false); break;
            default: break;
         }
         if (frameId >= 3)
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
      }

      ImGui::NewFrame();

      Transport::Instance().Tick(ImGui::GetIO().DeltaTime);
      Modulation::Instance().ClearFrameParams();
      PaletteBinding::Instance().ClearFrameColors();
      gDrawnParamPins.clear();
      gDrawnColorPins.clear();
      gParamRightClickConsumedThisFrame = false;
      {
         Modulation& modulation = Modulation::Instance();
         for (GraphNode& gn : gNodes)
         {
            gn.hasModulatedParams = false;
            gn.hasPaletteColors = false;
         }
         for (const auto& link : modulation.Links())
         {
            if (GraphNode* target = FindNodeByIndex(link.first.first))
               target->hasModulatedParams = true;
         }
         for (const auto& link : PaletteBinding::Instance().Links())
         {
            if (GraphNode* target = FindNodeByIndex(link.first.first))
               target->hasPaletteColors = true;
         }
      }

      // ---------------- node editor ----------------
      const ImGuiViewport* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(vp->WorkPos);
      ImGui::SetNextWindowSize(vp->WorkSize);
      // NoScrollbar/NoScrollWithMouse: this window is a fixed full-screen
      // shell (also NoResize/NoMove) whose every region is meant to be
      // divided exactly among the menu bar, canvas and docked panels, never
      // scrolled as a whole. Without this flag, being even a few pixels over
      // budget in that division - e.g. the item spacing ImGui inserts between
      // a top/bottom-docked viewport panel and the canvas row below/above it,
      // easy to undercount by hand - grows a scrollbar on THIS window, which
      // reads as "a slider that moves the entire app" rather than as a
      // rounding error in one panel's layout.
      ImGui::Begin("Infinite", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);

      if (ImGui::BeginMenuBar())
      {
         if (ImGui::BeginMenu("File"))
         {
            if (ImGui::MenuItem("New", "Cmd+N"))
               NewPatch();
            if (ImGui::MenuItem("Open...", "Cmd+O"))
            {
               const std::string path = Platform::OpenPatchDialog();
               if (!path.empty())
                  LoadPatchFrom(path);
            }

            if (ImGui::BeginMenu("Open Recent", !Patch::Recents().empty()))
            {
               // Copied before iterating: opening one calls NoteRecent, which
               // reorders the very list being walked.
               const std::vector<std::string> recents = Patch::Recents();
               for (const std::string& entry : recents)
               {
                  const size_t slash = entry.find_last_of('/');
                  const std::string name =
                     (slash == std::string::npos) ? entry : entry.substr(slash + 1);
                  if (ImGui::MenuItem(name.c_str()))
                     LoadPatchFrom(entry);
                  if (ImGui::IsItemHovered())
                     ImGui::SetTooltip("%s", entry.c_str());
               }
               ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Cmd+S"))
               SavePatchInteractive(false);
            if (ImGui::MenuItem("Save As...", "Cmd+Shift+S"))
               SavePatchInteractive(true);

            if (!gPatchPath.empty() || !gPatchStatus.empty())
            {
               ImGui::Separator();
               if (!gPatchPath.empty())
               {
                  const size_t slash = gPatchPath.find_last_of('/');
                  ImGui::TextDisabled("%s", (slash == std::string::npos)
                                               ? gPatchPath.c_str()
                                               : gPatchPath.c_str() + slash + 1);
               }
               if (!gPatchStatus.empty())
                  ImGui::TextDisabled("%s", gPatchStatus.c_str());
            }
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("Edit"))
         {
            if (ImGui::MenuItem("Undo", "Cmd+Z", false, !gUndoStack.empty()))
               Undo();
            if (ImGui::MenuItem("Redo", "Cmd+Shift+Z", false, !gRedoStack.empty()))
               Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Group selection", "Cmd+G"))
               gRequestGroup = true;
            if (ImGui::MenuItem("Ungroup", "Cmd+Shift+G"))
               gRequestUngroup = true;
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("Menu"))
         {
            ImGui::SeparatorText("Canvas");
            ImGui::Checkbox("Snap to grid", &gSnapToGrid);
            ImGui::SetNextItemWidth(170);
            ImGui::SliderFloat("Grid size", &gGridSnap, 5.0f, 100.0f, "%.0f px");
            ImGui::SetNextItemWidth(170);
            ImGui::SliderFloat("Zoom speed", &gZoomSensitivity, 0.05f, 1.5f, "%.2f");
            if (ImGui::MenuItem("Fit view to content"))
               gRequestFitView = true;

            ImGui::SeparatorText("Minimap");
            ImGui::Checkbox("Show minimap", &gMinimapEnabled);
            if (gMinimapEnabled)
            {
               static const char* kCorners[] = {
                  "Top left", "Top right", "Bottom left", "Bottom right"
               };
               ImGui::SetNextItemWidth(170);
               if (ImGui::BeginCombo("Position", kCorners[gMinimapCorner]))
               {
                  for (int i = 0; i < 4; i++)
                     if (ImGui::Selectable(kCorners[i], i == gMinimapCorner))
                        gMinimapCorner = i;
                  ImGui::EndCombo();
               }
               ImGui::SetNextItemWidth(170);
               ImGui::SliderFloat("Size", &gMinimapSize, 120.0f, 360.0f, "%.0f px");
               ImGui::SetNextItemWidth(170);
               ImGui::SliderFloat("Opacity", &gMinimapOpacity, 0.2f, 1.0f, "%.2f");
            }

            // The whole section only exists once something is open - with
            // nothing open there is nothing here to configure.
            if (!gViewportPanelNodes.empty())
            {
               ImGui::SeparatorText("Viewport panel");
               ImGui::SetNextItemWidth(170);
               ViewportPanelDockCombo();
               ImGui::SetNextItemWidth(170);
               // Only the axis the current dock actually reserves - the other
               // one has no effect from here, and a dead slider reads as broken.
               if (gViewportPanelDock == 1 || gViewportPanelDock == 2)
                  ImGui::SliderFloat("Width", &gViewportPanelWidth,
                                     kViewportPanelMinWidth, 900.0f, "%.0f px");
               else
                  ImGui::SliderFloat("Height", &gViewportPanelHeight,
                                     kViewportPanelMinHeight, 800.0f, "%.0f px");
               if (ImGui::MenuItem("Close viewport panel"))
                  gViewportPanelNodes.clear();
            }

            ImGui::SeparatorText("Performance");
            {
               // A cap is useful in both directions: it stops a light patch
               // spinning the GPU at 400fps for no reason, and it gives a
               // predictable frame budget to judge a heavy one against.
               static const char* kFpsLabels[] = { "Unlimited", "30", "60", "120" };
               static const int kFpsValues[] = { 0, 30, 60, 120 };
               int current = 0;
               for (int i = 0; i < 4; i++)
                  if (kFpsValues[i] == gTargetFps)
                     current = i;

               ImGui::SetNextItemWidth(170);
               if (ImGui::BeginCombo("Target FPS", kFpsLabels[current]))
               {
                  for (int i = 0; i < 4; i++)
                  {
                     if (ImGui::Selectable(kFpsLabels[i], current == i))
                        gTargetFps = kFpsValues[i];
                  }
                  ImGui::EndCombo();
               }

               if (ImGui::Checkbox("Vsync", &gVsync))
                  glfwSwapInterval(gVsync ? 1 : 0);
            }

            ImGui::SeparatorText("Theme");
            {
               const std::vector<std::string>& presets = CategoryColors::PresetNames();
               const int current = CategoryColors::CurrentPreset();
               ImGui::SetNextItemWidth(170);
               if (ImGui::BeginCombo("Theme", presets[current].c_str()))
               {
                  for (int i = 0; i < (int)presets.size(); i++)
                     if (ImGui::Selectable(presets[i].c_str(), current == i))
                     {
                        CategoryColors::SetPreset(i);
                        ApplyTheme();
                     }
                  ImGui::EndCombo();
               }
            }

            ImGui::SeparatorText("Nodes");
            if (ImGui::MenuItem("Show all params"))
            {
               for (GraphNode& gn : gNodes)
                  gn.showParams = true;
            }
            if (ImGui::MenuItem("Hide all params"))
            {
               for (GraphNode& gn : gNodes)
                  gn.showParams = false;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Help / module reference"))
               gHelpOpen = true;

            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
               RequestClose(window);
            ImGui::EndMenu();
         }
         ImGui::Separator();

         Transport& transport = Transport::Instance();
         ImGui::PushStyleColor(ImGuiCol_Button, transport.IsPlaying()
                                                   ? ImVec4(0.16f, 0.52f, 0.28f, 1.0f)
                                                   : ImVec4(0.30f, 0.30f, 0.34f, 1.0f));
         if (ImGui::Button(transport.IsPlaying() ? "Pause" : "Play", ImVec2(64, 0)))
            transport.TogglePlay();
         ImGui::PopStyleColor();

         if (ImGui::Button("Rewind"))
            transport.Rewind();

         float bpm = transport.Tempo();
         ImGui::SetNextItemWidth(130);
         if (ImGui::SliderFloat("BPM", &bpm, 20.0f, 300.0f, "%.1f"))
            transport.SetTempo(bpm);

         ImGui::TextDisabled("bar %d  beat %.2f",
                             1 + (int)(transport.Beats() / 4.0),
                             std::fmod(transport.Beats(), 4.0) + 1.0);

         // Frame cost, pinned right. Measured from the swap-to-swap wall clock
         // rather than ImGui's smoothed rate, so a heavy patch shows its real
         // cost immediately instead of easing into it over a second.
         static double sSmoothedMs = 0.0;
         // A gentle EMA: raw frame times jitter too much to read, but the
         // window is short enough that dragging a slider shows up at once.
         sSmoothedMs = (sSmoothedMs <= 0.0)
                          ? gLastFrameMs
                          : sSmoothedMs * 0.9 + gLastFrameMs * 0.1;
         const double fps = sSmoothedMs > 0.0001 ? 1000.0 / sSmoothedMs : 0.0;

         char readout[80];
         if (gTargetFps > 0)
            snprintf(readout, sizeof(readout), "%.1f / %d fps   %.1f ms",
                     fps, gTargetFps, sSmoothedMs);
         else
            snprintf(readout, sizeof(readout), "%.1f fps   %.1f ms", fps, sSmoothedMs);

         // Reserve space using a worst-case template rather than the live
         // string's width, so the search button doesn't jitter as the digit
         // count of the fps/ms readout changes frame to frame.
         char readoutTemplate[80];
         if (gTargetFps > 0)
            snprintf(readoutTemplate, sizeof(readoutTemplate), "888.8 / %d fps   888.8 ms", gTargetFps);
         else
            snprintf(readoutTemplate, sizeof(readoutTemplate), "888.8 fps   888.8 ms");
         const float reservedWidth = ImGui::CalcTextSize(readoutTemplate).x;
         const float readoutX = ImGui::GetWindowWidth() - reservedWidth - ImGui::GetStyle().WindowPadding.x * 2.0f;

         {
            // Sits left of the frame readout with a bit of breathing room,
            // so it reads as its own control rather than glued to the fps text.
            const char* label = "search";
            const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SameLine(readoutX - w - ImGui::GetStyle().ItemSpacing.x * 4.0f);
            if (ImGui::Button(label))
               gNodePanelOpen = !gNodePanelOpen;
         }

         ImGui::SameLine(readoutX);

         // Judged against the target when there is one, so hitting a
         // deliberate 30fps cap reads as green rather than as a problem.
         // Otherwise against 50/25, where dragging a slider stops feeling live.
         const double good = gTargetFps > 0 ? gTargetFps * 0.95 : 50.0;
         const double poor = gTargetFps > 0 ? gTargetFps * 0.5 : 25.0;
         const ImVec4 color = (fps >= good) ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f)
                            : (fps >= poor) ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                                            : ImVec4(0.95f, 0.45f, 0.4f, 1.0f);
         ImGui::TextColored(color, "%s", readout);

         ImGui::EndMenuBar();
      }

      // The canvas zoomed wildly on a trackpad because it consumes raw wheel
      // deltas; damp them for the duration of the editor, then restore.
      ImGuiIO& io = ImGui::GetIO();
      const float savedWheel = io.MouseWheel;
      const float savedWheelH = io.MouseWheelH;
      io.MouseWheel *= gZoomSensitivity;
      io.MouseWheelH *= gZoomSensitivity;

      ed::SetCurrentEditor(gEditor);

      // Dragging empty canvas should pan, but dragging a node should move it.
      // NavigateAction claims any drag on its configured button regardless of
      // what is underneath, so flip the button per-gesture using last frame's
      // hover state. Shift+drag still gives a rubber-band selection.
      {
         ed::Config& liveCfg = const_cast<ed::Config&>(ed::GetConfig(gEditor));
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gHoveringItem && !io.KeyShift)
            gPanWithLeft = true;
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            gPanWithLeft = false;
         liveCfg.NavigateButtonIndex = gPanWithLeft ? 0 : 1;
      }

      const float kNodePanelWidth = 270.0f;
      const bool viewportPanelOpen = !gViewportPanelNodes.empty();
      const bool viewportBottom = viewportPanelOpen && gViewportPanelDock == 0;
      const bool viewportRight = viewportPanelOpen && gViewportPanelDock == 1;
      const bool viewportLeft = viewportPanelOpen && gViewportPanelDock == 2;
      const bool viewportTop = viewportPanelOpen && gViewportPanelDock == 3;

      // Drop the 3D render state of any node no longer in the panel. Done
      // here, at the top of the next frame, rather than at the moment its
      // card was closed: that card had already submitted its texture to that
      // frame's draw list, and destroying the FBO first would leave the draw
      // list blitting a deleted texture.
      for (auto it = gPanelViewports.begin(); it != gPanelViewports.end(); )
      {
         const bool stillShown = std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(),
                                           it->first) != gViewportPanelNodes.end();
         it = stillShown ? std::next(it) : gPanelViewports.erase(it);
      }

      // Clamp the (drag-resizable) panel against the window before anything
      // reserves space from it, so dragging the grip can never squeeze the
      // canvas out of existence or push a panel off the far edge. Runs every
      // frame regardless of whether a drag is in progress - previously the
      // floor (kViewportPanelMin*) was only re-applied inside the grip's own
      // active-drag branch, so this upper-bound-only clamp could hold the
      // panel below its minimum indefinitely once something pushed it there.
      {
         const ImVec2 room = ImGui::GetContentRegionAvail();
         const float maxHeight = std::max(kViewportPanelMinHeight, room.y - 150.0f);
         const float maxWidth = std::max(kViewportPanelMinWidth,
                                         room.x - 200.0f - (gNodePanelOpen ? kNodePanelWidth : 0.0f));
         gViewportPanelHeight = std::min(std::max(gViewportPanelHeight, kViewportPanelMinHeight), maxHeight);
         gViewportPanelWidth = std::min(std::max(gViewportPanelWidth, kViewportPanelMinWidth), maxWidth);
      }

      // Measured before the top/left panels below consume any of it, so the
      // canvas gets what is left after every reservation rather than after
      // only the ones that happen to draw first.
      //
      // A top/bottom-docked panel is its own row, separate from the canvas
      // row below/above it - unlike right/left, which share the canvas's row
      // via SameLine() - so it costs one extra ItemSpacing.y that a same-line
      // dock never does. That gap is easy to forget in this budget; forgetting
      // it is exactly what grows the outer window's own scrollbar (see the
      // NoScrollbar comment on its Begin() call above).
      const float topBottomGap = (viewportTop || viewportBottom) ? ImGui::GetStyle().ItemSpacing.y : 0.0f;
      const float graphHeight =
         std::max(150.0f, ImGui::GetContentRegionAvail().y -
                             ((viewportTop || viewportBottom) ? gViewportPanelHeight + topBottomGap : 0.0f));

      // Top- and left-docked viewport panels draw before the canvas: nothing
      // else in this window reserves space above or left of it, so each has
      // to consume its own room here, before the canvas cursor position (and
      // gGraphScreenTL below) reflect it.
      if (viewportTop)
         DrawViewportPanelDocked("##viewportpanel_top", ImVec2(0, gViewportPanelHeight));
      if (viewportLeft)
      {
         DrawViewportPanelDocked("##viewportpanel_left", ImVec2(gViewportPanelWidth, graphHeight));
         ImGui::SameLine();
      }

      // One combined reservation for both right-docked panels, computed
      // together so ImGui's SameLine() chaining after ed::End() lays them out
      // side by side instead of one clipping the other or the two overlapping.
      float rightReserved = 0.0f;
      if (gNodePanelOpen) rightReserved += kNodePanelWidth;
      if (viewportRight) rightReserved += gViewportPanelWidth;
      const float graphWidth = rightReserved > 0.0f
                                  ? std::max(200.0f, ImGui::GetContentRegionAvail().x - rightReserved)
                                  : 0.0f;

      // The canvas rect, captured here rather than from inside the editor:
      // ed::Begin does not open a child window (ImGuiEx::Canvas draws straight
      // into the current one), so GetWindowPos/GetWindowSize in there report
      // the whole app window - menu bar and docked module panel included.
      // Anything positioned against that, the minimap especially, would sit
      // outside the graph and over a panel - which is also why the left panel
      // above has to draw before this capture rather than after.
      gGraphScreenTL = ImGui::GetCursorScreenPos();
      gGraphScreenSize = ImVec2(graphWidth > 0.0f ? graphWidth : ImGui::GetContentRegionAvail().x,
                                graphHeight);

      ed::Begin("graph", ImVec2(graphWidth, graphHeight));

      if (!gPendingSelect.empty())
      {
         bool first = true;
         for (int nodeId : gPendingSelect)
         {
            ed::SelectNode(nodeId, !first);
            first = false;
         }
         gPendingSelect.clear();
      }

      // Where a panel-spawned node should land. ScreenToCanvas is only valid
      // inside the editor, so it is captured here and used after ed::End().
      gViewCenterCanvas = ed::ScreenToCanvas(
         ImVec2(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f,
                gGraphScreenTL.y + gGraphScreenSize.y * 0.5f));

      // Dropping a file on the canvas spawns the matching source node, already
      // loaded, at the drop point.
      if (!gDroppedFiles.empty())
      {
         static const std::vector<std::string> kVideoExt = {
            "mov", "mp4", "m4v", "avi", "mkv", "webm", "mpg", "mpeg", "wmv", "flv", "hevc"
         };
         // Everything ModelIO reads. Checked before video because "usdz" and
         // "abc" would otherwise fall through to the image branch and fail.
         static const std::vector<std::string> kAudioExt = {
            "wav", "aif", "aiff", "mp3", "m4a", "aac", "caf", "flac", "ogg"
         };
         static const std::vector<std::string> kModelExt = {
            "obj", "ply", "stl", "usd", "usda", "usdc", "usdz", "abc"
         };
         ImVec2 canvasPos = ed::ScreenToCanvas(gDropPos);
         float offset = 0.0f;
         for (const std::string& path : gDroppedFiles)
         {
            GraphNode* spawned = nullptr;
            if (HasExtension(path, kAudioExt))
            {
               spawned = SpawnNode("Audio File", "Modulators", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<AudioFileNode*>(spawned->node.get())->Open(path);
            }
            else if (HasExtension(path, kModelExt))
            {
               spawned = SpawnNode("Model 3D", "3D", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ModelSourceNode*>(spawned->node.get())->Load(path);
            }
            else if (HasExtension(path, kVideoExt))
            {
               spawned = SpawnNode("Video", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<VideoSourceNode*>(spawned->node.get())->Open(path);
            }
            else
            {
               spawned = SpawnNode("Image Source", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ImageSourceNode*>(spawned->node.get())->Load(path);
            }
            if (spawned != nullptr)
               spawned->showParams = true;
            offset += 240.0f;
         }
         gDroppedFiles.clear();
      }

      if (getenv("INFINITE_COLORTEST") != nullptr)
      {
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         if (frameId == 7)
            printf("before click: fill=(%.2f, %.2f, %.2f)\n", sh->fillColor[0], sh->fillColor[1], sh->fillColor[2]);
         if (frameId == 11)
         {
            printf("after  click: fill=(%.2f, %.2f, %.2f)  %s\n",
                   sh->fillColor[0], sh->fillColor[1], sh->fillColor[2],
                   (std::fabs(sh->fillColor[0] - 0.9f) > 0.02f ||
                    std::fabs(sh->fillColor[1] - 0.35f) > 0.02f ||
                    std::fabs(sh->fillColor[2] - 0.2f) > 0.02f) ? "PICKER RESPONDED OK" : "NO CHANGE - BUG");
         }
      }

      if (getenv("INFINITE_RECTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            out->recordFps = 30;
            bool started = out->StartRecording("/tmp/infinite_rectest.mov");
            printf("start recording: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 5)
         {
            // pause mid-take: the clock must freeze, but frames keep encoding
            Transport::Instance().SetPlaying(false);
            printf("paused at beats=%.4f\n", Transport::Instance().Beats());
         }
         if (frameId == 12)
            printf("beats while paused=%.4f (should be unchanged)\n", Transport::Instance().Beats());
         if (frameId == 13)
            Transport::Instance().SetPlaying(true);
         if (frameId == 40)
         {
            printf("frames captured: %d\n", out->RecordedFrames());
            out->StopRecording();
            printf("stop: %s\n", out->RecordStatus().c_str());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_RESYNTHTEST") != nullptr)
      {
         auto* rs = static_cast<ResynthNode*>(gNodes[1].node.get());
         auto sample = [](INode* n, unsigned char* out)
         {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n->GetOutputTexture(), 0);
            glReadPixels(n->GetOutputWidth() / 3, n->GetOutputHeight() / 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
         };
         static unsigned char prev[4] = { 0, 0, 0, 0 };
         if (frameId == 2)
         {
            rs->chaos = 0.8f; rs->mutation = 0.8f; rs->sourcePull = 0.02f;
            rs->Randomise();
            sample(rs, prev);
            printf("gen %-3d pixel=(%d,%d,%d)\n", rs->Generation(), prev[0], prev[1], prev[2]);
         }
         if (frameId >= 3 && frameId <= 12)
         {
            rs->StepOnce();
            unsigned char now[4];
            sample(rs, now);
            int drift = abs(now[0]-prev[0]) + abs(now[1]-prev[1]) + abs(now[2]-prev[2]);
            printf("gen %-3d pixel=(%d,%d,%d) drift=%d\n", rs->Generation(), now[0], now[1], now[2], drift);
            memcpy(prev, now, 4);
         }
         if (frameId == 14)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (getenv("INFINITE_BYPASSTEST") != nullptr)
      {
         auto sample = [](INode* n, unsigned char* out)
         {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n->GetOutputTexture(), 0);
            glReadPixels(n->GetOutputWidth()/2, n->GetOutputHeight()/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
         };
         unsigned char px[4];
         if (frameId == 3)
         {
            sample(gNodes[2].node.get(), px);
            printf("invert active:   output=(%d,%d,%d) %s\n", px[0], px[1], px[2],
                   px[0] < 40 ? "inverted OK" : "UNEXPECTED");
            gNodes[1].node->bypassed = true;
         }
         if (frameId == 6)
         {
            sample(gNodes[2].node.get(), px);
            printf("invert bypassed: output=(%d,%d,%d) %s\n", px[0], px[1], px[2],
                   px[0] > 200 ? "PASSED THROUGH OK" : "STILL INVERTED - BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_PHASEATEST") != nullptr && frameId == 4)
      {
         // Every primitive and 2D shape must be spawnable by its own name and
         // come out preset to that shape, not to the class default.
         bool allShapes = true;
         for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
         {
            const std::string& name = GeometryNode::ShapeNames()[i];
            INode* made = NodeFactory::Instance().MakeNode(name);
            auto* geo = dynamic_cast<GeometryNode*>(made);
            // GetMesh() first: the mesh is built lazily, so TriangleCount is 0
            // until something asks for it.
            const bool ok = geo != nullptr && geo->shape == i &&
                            !geo->GetMesh().Empty();
            if (!ok) { allShapes = false; printf("  failed: %s\n", name.c_str()); }
            delete made;
         }
         bool allShapes2D = true;
         for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
         {
            INode* made = NodeFactory::Instance().MakeNode(ShapeNode::ShapeNames()[i]);
            auto* sh = dynamic_cast<ShapeNode*>(made);
            if (sh == nullptr || sh->shapeType != i) allShapes2D = false;
            delete made;
         }
         printf("primitives spawnable by name: 3D=%d 2D=%d\n", (int)allShapes, (int)allShapes2D);
         // Two nodes sharing a name is a silent bug: one of them becomes
         // unspawnable and any patch naming it loads the wrong node.
         const std::vector<std::string>& dupes = NodeFactory::Instance().DuplicateNames();
         for (const std::string& d : dupes)
            printf("  duplicate node name: %s\n", d.c_str());
         printf("unique node names: %s\n", dupes.empty() ? "OK" : "FAIL");
         allShapes = allShapes && dupes.empty();

         // Bevel must actually round a cube - more triangles, and a smaller
         // extent than the original since the corners get pulled in.
         const Mesh cube = Primitives::Cube(1);
         const Mesh rounded = MeshOps::Bevel(cube, 0.6f, 2);
         // Measured as the furthest vertex from the centre, not the bounding
         // box: a bevel rounds the corners while leaving the flat faces exactly
         // where they were, so the box does not shrink at all. The corners are
         // the only thing that moves, and they are what this catches.
         auto maxRadius = [](const Mesh& m) {
            float worst = 0.0f;
            for (const Vertex& v : m.vertices)
               worst = std::max(worst, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
            return worst;
         };
         const float r0 = maxRadius(cube), r1 = maxRadius(rounded);
         printf("bevel: %zu -> %zu tris, corner radius %.3f -> %.3f (faces stay put)\n",
                cube.indices.size() / 3, rounded.indices.size() / 3, r0, r1);
         const bool bevelled = rounded.indices.size() > cube.indices.size() &&
                               r1 < r0 - 0.02f && r1 > 0.3f;

         printf("%s\n", (allShapes && allShapes2D && bevelled) ? "PHASE A OK" : "SUSPECT");
      }

      if (getenv("INFINITE_SELECTTEST") != nullptr && frameId == 4)
      {
         const Mesh cube = Primitives::Cube(1);
         printf("cube: %zu faces, %zu selected by default\n",
                cube.FaceCount(), cube.SelectedCount());
         // An empty mask has to mean "everything", or every operator would need
         // to special-case a mesh that has never been through a Select.
         const bool defaultAll = cube.SelectedCount() == cube.FaceCount();

         // The top of a cube is two triangles: selecting by normal must find
         // exactly those, which is the check that the mode means what it says.
         const Mesh top = MeshOps::Select(cube, MeshOps::kSelectNormal,
                                          0.9f, 0.0f, 1.0f, 1, 0.0f, false, false);
         printf("faces pointing +Y: %zu of %zu  %s\n", top.SelectedCount(), top.FaceCount(),
                top.SelectedCount() == 2 ? "OK" : "FAIL");

         // Deleting them must remove exactly those two and nothing else.
         const Mesh cut = MeshOps::DeleteSelected(top, false);
         printf("delete the selection: %zu faces  %s\n", cut.FaceCount(),
                cut.FaceCount() == cube.FaceCount() - 2 ? "OK" : "FAIL");

         // Keeping instead of deleting is the exact complement.
         const Mesh kept = MeshOps::DeleteSelected(top, true);
         printf("keep instead: %zu faces  %s\n", kept.FaceCount(),
                kept.FaceCount() == 2 ? "OK" : "FAIL");

         // Moving the selection must move only the top, so the maximum y rises
         // while the minimum stays exactly where it was.
         auto rangeY = [](const Mesh& m) {
            float lo = 1e30f, hi = -1e30f;
            for (const Vertex& v : m.vertices) { lo = std::min(lo, v.py); hi = std::max(hi, v.py); }
            return std::pair<float,float>(lo, hi);
         };
         const Mesh moved = MeshOps::TransformSelected(top, Mat4::Identity(), true, 0.5f);
         const auto before = rangeY(cube);
         const auto after = rangeY(moved);
         printf("move top by 0.5: y %.2f..%.2f -> %.2f..%.2f  %s\n",
                before.first, before.second, after.first, after.second,
                (std::fabs(after.second - (before.second + 0.5f)) < 0.01f &&
                 std::fabs(after.first - before.first) < 0.01f) ? "OK" : "FAIL");
         const bool movedOk = std::fabs(after.second - (before.second + 0.5f)) < 0.01f &&
                              std::fabs(after.first - before.first) < 0.01f;

         // Extruding the selection adds a cap and walls but leaves the rest.
         const Mesh extruded = MeshOps::ExtrudeSelected(top, 0.4f, 0.2f);
         printf("extrude top: %zu -> %zu faces  %s\n", cube.FaceCount(), extruded.FaceCount(),
                extruded.FaceCount() > cube.FaceCount() ? "OK" : "FAIL");

         // Random selection has to be reproducible from its seed, or a patch
         // would look different every time it was opened.
         const Mesh r1 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 7.0f, false, false);
         const Mesh r2 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 7.0f, false, false);
         const Mesh r3 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 9.0f, false, false);
         const bool reproducible = r1.faceMask == r2.faceMask && r1.faceMask != r3.faceMask;
         printf("random selection reproducible from seed: %d\n", (int)reproducible);

         // Mesh to Points billboards each point as two triangles. Selecting at
         // ~50% must choose or skip both triangles of a point together - never
         // one triangle selected and its partner not, which is what tore
         // points in half before points carried a selectionGroup tag.
         const std::vector<MeshPoint> cloud = MeshOps::ToPoints(cube, 0, 10000);
         const Mesh billboards = MeshOps::PointsToFaces(cloud, 0.2f);
         const Mesh pointSel = MeshOps::Select(billboards, MeshOps::kSelectRandom,
                                                0.5f, 0, 0, 1, 3.0f, false, false);
         bool quadsIntact = !pointSel.faceMask.empty();
         for (size_t f = 0; f + 1 < pointSel.FaceCount(); f += 2)
            if (pointSel.faceMask[f] != pointSel.faceMask[f + 1])
               quadsIntact = false;
         printf("point selection keeps quads whole: %zu points, %zu tris  %s\n",
                cloud.size(), pointSel.FaceCount(), quadsIntact ? "OK" : "FAIL");

         const bool ok = defaultAll && top.SelectedCount() == 2 &&
                         cut.FaceCount() == cube.FaceCount() - 2 && kept.FaceCount() == 2 &&
                         movedOk && extruded.FaceCount() > cube.FaceCount() && reproducible &&
                         quadsIntact;
         printf("%s\n", ok ? "SELECTION OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PADPATHTEST") != nullptr && frameId == 4)
      {
         // A real recorded performance, not the generic mutator's mangled
         // text, round-tripping through both save/load and copy/paste.
         ShapeNode src;
         src.shapeType = 0;
         src.width = 64; src.height = 64;
         src.CookIfNeeded(9500);

         ResynthNode a;
         a.Input().Connect(&src);
         Transport::Instance().SetPlaying(true);
         Transport::Instance().Rewind();
         a.StartRecording();
         const float xs[] = { 0.1f, 0.3f, 0.6f, 0.9f, 0.2f };
         for (int i = 0; i < 5; i++)
         {
            a.padX = xs[i];
            a.padY = 1.0f - xs[i];
            a.CookIfNeeded(9510 + i);
            Transport::Instance().Tick(0.3f);
         }
         a.StopRecording();
         const size_t recorded = a.Path().size();

         auto pathsMatch = [](const std::vector<ResynthNode::PadPoint>& p,
                              const std::vector<ResynthNode::PadPoint>& q) {
            if (p.size() != q.size())
               return false;
            for (size_t i = 0; i < p.size(); i++)
               if (std::fabs(p[i].x - q[i].x) > 1e-5f || std::fabs(p[i].y - q[i].y) > 1e-5f ||
                   std::fabs(p[i].beat - q[i].beat) > 1e-6)
                  return false;
            return true;
         };

         ResynthNode b;
         CopyParams(&b, &a);
         const bool copyOk = pathsMatch(a.Path(), b.Path());
         printf("resynth pad path: %zu points recorded, copy/paste preserved %zu  %s\n",
                recorded, b.Path().size(), copyOk && recorded > 0 ? "OK" : "FAIL");

         std::vector<std::pair<std::string, std::string>> params;
         Patch::SaveParams(&a, params);
         ResynthNode c;
         Patch::LoadParams(&c, params);
         const bool loadOk = pathsMatch(a.Path(), c.Path());
         printf("resynth pad path: save/load preserved %zu  %s\n",
                c.Path().size(), loadOk ? "OK" : "FAIL");

         // Same check for Macro XY's pad, which records identically.
         MacroXYNode m;
         Transport::Instance().Rewind();
         m.StartRecording();
         for (int i = 0; i < 5; i++)
         {
            m.padX = xs[i];
            m.padY = 1.0f - xs[i];
            m.CookIfNeeded(9520 + i);
            Transport::Instance().Tick(0.3f);
         }
         m.StopRecording();
         const size_t mRecorded = m.Path().size();

         auto macroPathsMatch = [](const std::vector<MacroXYNode::PadPoint>& p,
                                   const std::vector<MacroXYNode::PadPoint>& q) {
            if (p.size() != q.size())
               return false;
            for (size_t i = 0; i < p.size(); i++)
               if (std::fabs(p[i].x - q[i].x) > 1e-5f || std::fabs(p[i].y - q[i].y) > 1e-5f ||
                   std::fabs(p[i].beat - q[i].beat) > 1e-6)
                  return false;
            return true;
         };
         MacroXYNode m2;
         CopyParams(&m2, &m);
         const bool mCopyOk = macroPathsMatch(m.Path(), m2.Path());
         printf("macro xy pad path: %zu points recorded, copy/paste preserved %zu  %s\n",
                mRecorded, m2.Path().size(), mCopyOk && mRecorded > 0 ? "OK" : "FAIL");

         const bool ok = copyOk && loadOk && recorded > 0 && mCopyOk && mRecorded > 0;
         printf("%s\n", ok ? "PAD PATH OK" : "SUSPECT");
      }

      if (getenv("INFINITE_UNDOTEST") != nullptr && frameId == 4)
      {
         NewPatch(); // also clears the undo/redo stacks - a clean baseline

         bool ok = true;

         // --- spawn / undo / redo ---
         GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
         const bool spawnedOne = gNodes.size() == 1 && cube != nullptr;
         Undo();
         const bool undoRemovedIt = gNodes.empty();
         Redo();
         const bool redoBroughtItBack = gNodes.size() == 1;
         printf("spawn: 1 node -> undo -> %zu nodes -> redo -> %zu nodes  %s\n",
                (size_t)0, gNodes.size(),
                (spawnedOne && undoRemovedIt && redoBroughtItBack) ? "OK" : "FAIL");
         ok = ok && spawnedOne && undoRemovedIt && redoBroughtItBack;

         // --- param edit / undo / redo, via the same checkpoint the UI widgets use ---
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         geo->detail = 24; // known starting value
         PushUndoCheckpoint(); // what ModSlider does on IsItemActivated, before the edit
         geo->detail = 91;
         Undo();
         const int afterUndo = geo->detail;
         const bool undoRestoredParam = afterUndo == 24;
         Redo();
         const int afterRedo = geo->detail;
         const bool redoReappliedParam = afterRedo == 91;
         printf("param edit: 24 -> 91 -> undo -> %d -> redo -> %d  %s\n",
                afterUndo, afterRedo,
                (undoRestoredParam && redoReappliedParam) ? "OK" : "FAIL");
         ok = ok && undoRestoredParam && redoReappliedParam;

         // --- delete (with a connection) / undo restores both the node and the wire ---
         GraphNode* smoothGn = SpawnNode("Smooth", "3D", 200.0f, 0.0f);
         auto* smooth = static_cast<GeometryOpNode*>(smoothGn->node.get());
         smooth->input = geo;
         const int smoothIndex = smoothGn->index;
         RemoveNodeByIndex(smoothIndex);
         const bool deleted = FindNodeByIndex(smoothIndex) == nullptr;
         Undo();
         GraphNode* restored = nullptr;
         for (GraphNode& gn : gNodes)
            if (gn.typeName == "Smooth")
               restored = &gn;
         const bool nodeRestored = restored != nullptr;
         const bool connectionRestored = nodeRestored &&
            static_cast<GeometryOpNode*>(restored->node.get())->input ==
               dynamic_cast<IGeometrySource*>(gNodes[0].node.get());
         printf("delete with connection: deleted=%d, undo restores node=%d and wire=%d  %s\n",
                (int)deleted, (int)nodeRestored, (int)connectionRestored,
                (deleted && nodeRestored && connectionRestored) ? "OK" : "FAIL");
         ok = ok && deleted && nodeRestored && connectionRestored;

         // --- a new action after an undo must drop the stale redo history ---
         Undo(); // back to just the cube, no Smooth
         const size_t redoDepthBeforeNewAction = gRedoStack.size();
         SpawnNode("Sphere", "3D", 400.0f, 0.0f);
         const bool redoClearedByNewAction = gRedoStack.empty();
         printf("redo stack: %zu entries before a new action -> %zu after  %s\n",
                redoDepthBeforeNewAction, gRedoStack.size(),
                (redoDepthBeforeNewAction > 0 && redoClearedByNewAction) ? "OK" : "FAIL");
         ok = ok && redoDepthBeforeNewAction > 0 && redoClearedByNewAction;

         // --- undo/redo themselves must not pollute the stacks they read from ---
         const size_t depthBefore = gUndoStack.size();
         Undo();
         Redo();
         const bool statOfSizeUnaffected = gUndoStack.size() == depthBefore;
         printf("undo/redo do not grow their own stacks: %zu -> %zu  %s\n",
                depthBefore, gUndoStack.size(), statOfSizeUnaffected ? "OK" : "FAIL");
         ok = ok && statOfSizeUnaffected;

         // --- opening a patch from disk clears history; undoing past it is not a thing ---
         SavePatchTo("/tmp/infinite_undotest.infinite");
         LoadPatchFrom("/tmp/infinite_undotest.infinite");
         const bool loadClearsUndo = gUndoStack.empty() && gRedoStack.empty();
         printf("loading a patch clears undo history: %zu undo, %zu redo  %s\n",
                gUndoStack.size(), gRedoStack.size(), loadClearsUndo ? "OK" : "FAIL");
         ok = ok && loadClearsUndo;

         printf("%s\n", ok ? "UNDO REDO OK" : "SUSPECT");
      }

      // Bring the comment into view for the screenshot check. Frame 2 rather
      // than frame 0 only so the node has settled at its spawn position and
      // measured its own size first; the fit itself runs at the end of this
      // same frame and lands on the frame after.
      if (getenv("INFINITE_COMMENTTEST") != nullptr && frameId == 2)
         gRequestFitView = true;

      // Slash mode: one keystroke had to produce a comment already taking the
      // keyboard, with no click of any kind in between, and the "/" itself must
      // not have ended up in the note.
      {
         const char* mode = getenv("INFINITE_COMMENTTEST");
         if (mode != nullptr && std::string(mode) == "slash" && frameId == 15)
         {
            CommentNode* c = gNodes.size() == 1
                                ? dynamic_cast<CommentNode*>(gNodes[0].node.get())
                                : nullptr;
            const bool spawned = c != nullptr;
            const bool typed = spawned && c->text == "lighting\nrim light too hot";
            printf("slash spawned a comment=%d, text=\"%s\"  %s\n", (int)spawned,
                   spawned ? c->text.c_str() : "(none)",
                   (spawned && typed) ? "SLASH COMMENT OK" : "FAIL");
            if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // In edit mode (see the synthetic double-click and typing above) report
      // whether the note's only way in works end to end.
      {
         const char* mode = getenv("INFINITE_COMMENTTEST");
         if (mode != nullptr && std::string(mode) == "edit" && frameId == 19)
         {
            auto* c = static_cast<CommentNode*>(gNodes[0].node.get());
            const bool opened = gCommentEdit.target == c;
            const bool typed = c->text.find('!') != std::string::npos;
            const size_t lines = (size_t)std::count(c->text.begin(), c->text.end(), '\n') + 1;
            printf("double-click opens editor=%d, typing reaches the note=%d, %zu lines  %s\n",
                   (int)opened, (int)typed, lines,
                   (opened && typed && lines == 4) ? "COMMENT EDIT OK" : "FAIL");
            if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // A note's line breaks are its content, and one param round trip backs
      // saving, undo/redo and copy/paste alike - so a comment that survives
      // being written to disk and read back survives all three. Checked here
      // rather than by eye because a screenshot cannot tell "kept the line
      // breaks" from "happens to be short enough to wrap the same way".
      //
      // Runs before the screenshot frame, and puts the note back the way it
      // found it, so the picture still shows the comment as authored. Skipped
      // in edit mode: reloading the patch would delete the node whose text is
      // being edited and close the popup under test.
      if (getenv("INFINITE_COMMENTTEST") != nullptr && frameId == 11 &&
          std::string(getenv("INFINITE_COMMENTTEST")) != "edit" &&
          std::string(getenv("INFINITE_COMMENTTEST")) != "slash")
      {
         auto findComment = []() -> CommentNode*
         {
            for (GraphNode& gn : gNodes)
            {
               if (auto* c = dynamic_cast<CommentNode*>(gn.node.get()))
                  return c;
            }
            return nullptr;
         };
         auto lineCount = [](const std::string& s)
         {
            return (size_t)std::count(s.begin(), s.end(), '\n') + 1;
         };

         bool ok = true;
         CommentNode* c = findComment();
         const std::string original = c != nullptr ? c->text : std::string();

         SavePatchTo("/tmp/infinite_commenttest.infinite");
         LoadPatchFrom("/tmp/infinite_commenttest.infinite");
         CommentNode* reloaded = findComment(); // load rebuilt every node
         const bool savedOk = reloaded != nullptr && reloaded->text == original;
         printf("comment save/load: %zu lines -> %zu lines  %s\n",
                lineCount(original),
                reloaded != nullptr ? lineCount(reloaded->text) : (size_t)0,
                savedOk ? "OK" : "FAIL");
         ok = ok && savedOk;

         // The same edit-then-undo the popup performs: the checkpoint is pushed
         // when the editor opens, the text changes while it is open.
         if (reloaded != nullptr)
         {
            PushUndoCheckpoint();
            reloaded->text = "scribbled over";
            Undo();
            CommentNode* undone = findComment();
            const bool undoOk = undone != nullptr && undone->text == original;
            printf("comment undo: text back to %zu lines  %s\n",
                   undone != nullptr ? lineCount(undone->text) : (size_t)0,
                   undoOk ? "OK" : "FAIL");
            ok = ok && undoOk;

            Redo();
            CommentNode* redone = findComment();
            const bool redoOk = redone != nullptr && redone->text == "scribbled over";
            printf("comment redo: %s  %s\n",
                   redone != nullptr ? redone->text.c_str() : "(gone)",
                   redoOk ? "OK" : "FAIL");
            ok = ok && redoOk;

            Undo(); // leave the authored note on screen for the screenshot
         }

         printf("%s\n", ok ? "COMMENT OK" : "SUSPECT");
         if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      // Group auto-fit: the box must track its members in BOTH directions, so
      // dragging one out stretches it and dragging that one back shrinks it
      // to the size it had before. Driven here rather than by hand because
      // the whole behaviour is a fixed point between our own fitting pass and
      // the editor's group geometry, and eyeballing a screenshot cannot tell
      // "shrank back exactly" from "shrank back nearly".
      if (getenv("INFINITE_GROUPTEST") != nullptr && gNodes.size() >= 3)
      {
         static float baselineW = 0.0f;
         GraphNode* group = nullptr;
         for (GraphNode& g : gNodes)
         {
            if (dynamic_cast<GroupNode*>(g.node.get()) != nullptr)
               group = &g;
         }

         if (frameId == 4)
         {
            GraphNode* gn = SpawnNode("Group", "Compositing", 60.0f, 60.0f);
            auto* g = static_cast<GroupNode*>(gn->node.get());
            gGroupMembers[g] = { gNodes[0].index, gNodes[1].index, gNodes[2].index };
         }
         else if (frameId == 8 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            baselineW = g->width;
            printf("baseline box: %.0f x %.0f\n", g->width, g->height);
            ed::SetNodePosition(gNodes[2].NodeId(), ImVec2(1600.0f, 100.0f));
         }
         else if (frameId == 12 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            printf("after dragging a member out: %.0f wide  %s\n", g->width,
                   g->width > baselineW + 500.0f ? "GREW" : "FAIL");
            ed::SetNodePosition(gNodes[2].NodeId(), ImVec2(700.0f, 100.0f));
         }
         else if (frameId == 16 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            printf("after dragging it back: %.0f wide  %s\n", g->width,
                   std::fabs(g->width - baselineW) < 1.0f ? "SHRANK BACK OK" : "FAIL");
            printf("members still owned: %zu\n", gGroupMembers[g].size());

            // A second group dropped right on top of the first must come up
            // empty: every node down there already belongs to group one, and
            // a group is never a member of a group. Both together are what
            // stop one group from swallowing another.
            GraphNode* second = SpawnNode("Group", "Compositing", 0.0f, 0.0f);
            auto* g2 = static_cast<GroupNode*>(second->node.get());
            g2->width = 2000.0f;
            g2->height = 800.0f;
         }
         else if (frameId == 20)
         {
            GroupNode* first = nullptr;
            GroupNode* second = nullptr;
            int secondIndex = -1;
            for (GraphNode& g : gNodes)
            {
               if (auto* asGroup = dynamic_cast<GroupNode*>(g.node.get()))
               {
                  if (first == nullptr)
                     first = asGroup;
                  else
                  {
                     second = asGroup;
                     secondIndex = g.index;
                  }
               }
            }
            printf("overlapping second group stole: %zu members  %s\n",
                   gGroupMembers[second].size(),
                   gGroupMembers[second].empty() && gGroupMembers[first].size() == 3
                      ? "NO STEALING OK" : "FAIL");

            // Ungroup is driven through the real path: select a *member*, not
            // the group's header, and let the shortcut handler find the owner.
            const int memberId = gNodes[0].NodeId();
            // The overlapping group goes first, otherwise it simply adopts the
            // nodes the moment ungroup frees them and the check below cannot
            // tell "freed" apart from "handed straight to the other group".
            ed::DeleteNode(ed::NodeId(secondIndex * GraphNode::kStride));
            RemoveNodeByIndex(secondIndex);
            ed::SelectNode(ed::NodeId(memberId));
         }
         else if (frameId == 22)
         {
            gRequestUngroup = true;
         }
         else if (frameId == 26)
         {
            size_t groupsLeft = 0;
            for (GraphNode& g : gNodes)
            {
               if (dynamic_cast<GroupNode*>(g.node.get()) != nullptr)
                  groupsLeft++;
            }
            // The group is gone, its three member nodes are untouched, and
            // they are unowned again rather than still bound to a dead group.
            const bool freed = GroupOwning(gNodes[0].index) == nullptr;
            printf("after ungroup: %zu groups left, %zu nodes, member freed=%d  %s\n",
                   groupsLeft, gNodes.size(), (int)freed,
                   (groupsLeft == 0 && gNodes.size() == 3 && freed) ? "UNGROUP OK" : "FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_LIVETEST") != nullptr && frameId == 4)
      {
         // Reproduces the reported bug exactly: Cube -> Select -> Delete
         // Selected, then reroll Select's random seed and confirm Delete
         // Selected's output actually changes without touching the graph.
         GeometryNode cube;
         cube.shape = 1; // cube
         cube.CookIfNeeded(9400);

         auto select = std::make_unique<GeometryOpNode>();
         select->op = GeometryOpNode::kSelect;
         select->input = &cube;
         select->selectMode = MeshOps::kSelectRandom;
         select->selectA = 0.5f;
         select->selectSeed = 1.0f;

         auto del = std::make_unique<GeometryOpNode>();
         del->op = GeometryOpNode::kDeleteSelected;
         del->input = select.get();

         // Copied by value: GetMesh() returns a reference into the node's own
         // cache, so holding two "const Mesh&" across the second call would
         // alias the same mutated object instead of comparing before/after.
         const Mesh firstOut = del->GetMesh();
         const size_t firstTris = firstOut.indices.size() / 3;

         select->selectSeed = 42.0f;
         const Mesh secondOut = del->GetMesh();
         const size_t secondTris = secondOut.indices.size() / 3;

         auto sameVertices = [](const Mesh& a, const Mesh& b) {
            if (a.vertices.size() != b.vertices.size() || a.indices.size() != b.indices.size())
               return false;
            for (size_t i = 0; i < a.vertices.size(); i++)
               if (std::fabs(a.vertices[i].px - b.vertices[i].px) > 1e-6f ||
                   std::fabs(a.vertices[i].py - b.vertices[i].py) > 1e-6f ||
                   std::fabs(a.vertices[i].pz - b.vertices[i].pz) > 1e-6f)
                  return false;
            return true;
         };
         const bool changed = !sameVertices(firstOut, secondOut);
         printf("delete selected: seed 1 -> %zu tris, seed 42 -> %zu tris, output changed=%d  %s\n",
                firstTris, secondTris, (int)changed, changed ? "OK" : "FAIL");

         // Same check one hop further downstream, through a second operator -
         // the bug would still be live if only the immediate child re-read the
         // revision and a grandchild did not.
         auto transform = std::make_unique<GeometryOpNode>();
         transform->op = GeometryOpNode::kTransformSelected;
         transform->input = select.get();
         select->selectSeed = 1.0f;
         const Mesh t1 = transform->GetMesh();
         select->selectSeed = 42.0f;
         const Mesh t2 = transform->GetMesh();
         const bool changed2 = !sameVertices(t1, t2);
         printf("transform selected also reacts to reselection  %s\n", changed2 ? "OK" : "FAIL");

         // And InstanceOnPointsNode, which had the identical bug on its own
         // point-source and instance-shape inputs.
         auto inst = std::make_unique<InstanceOnPointsNode>();
         inst->pointSource = select.get();
         inst->instanceShape = &cube;
         inst->pointMode = 2; // faces
         select->selectSeed = 1.0f;
         inst->CookIfNeeded(9401);
         const size_t instCount1 = inst->InstanceCount();
         select->selectSeed = 42.0f;
         inst->CookIfNeeded(9402);
         const size_t instCount2 = inst->InstanceCount();
         printf("instance on points: seed 1 -> %zu instances, seed 42 -> %zu instances  %s\n",
                instCount1, instCount2, instCount1 != instCount2 ? "OK" : "FAIL");

         const bool ok = changed && changed2 && instCount1 != instCount2;
         printf("%s\n", ok ? "LIVE UPDATE OK" : "SUSPECT");
      }

      if (getenv("INFINITE_ROUNDTRIPTEST") != nullptr && frameId == 4)
      {
         // Every node type that declares params must survive both paths that
         // restore a node from bare settings: copy/paste (CopyParams) and
         // patch load (Patch::LoadParams). This is what caught FormulaNode,
         // TextNode, NoiseNode and about eighteen others silently keeping the
         // base INode::VisitParams no-op, so save/load and copy/paste dropped
         // every value on them.
         struct MutateVisitor : public ParamVisitor
         {
            int index = 0;
            void Float(const char*, float& v) override { v += 1.0f + (float)(index++ % 5) * 0.1f; }
            void Int(const char*, int& v) override { v += 1 + (index++ % 3); }
            void Bool(const char*, bool& v) override { v = !v; index++; }
            void Text(const char*, std::string& v) override { v += "_x"; index++; }
            void Color(const char*, float rgb[3]) override
            {
               rgb[0] += 0.11f; rgb[1] += 0.07f; rgb[2] += 0.05f; index++;
            }
         };

         int typesTested = 0, typesSkipped = 0, copyFails = 0, loadFails = 0;
         std::vector<std::string> copyFailNames, loadFailNames;

         for (const std::string& category : NodeFactory::Instance().GetCategories())
         {
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
            {
               std::unique_ptr<INode> a(NodeFactory::Instance().MakeNode(name));
               if (a == nullptr)
                  continue;

               MutateVisitor mutator;
               a->VisitParams(mutator);
               if (mutator.index == 0)
               {
                  // Legitimately no params (Null, Null 3D, the boolean-mode
                  // Join Geometry variants) rather than a missed override -
                  // nothing to round-trip.
                  typesSkipped++;
                  continue;
               }
               typesTested++;

               std::vector<std::pair<std::string, std::string>> paramsA;
               Patch::SaveParams(a.get(), paramsA);

               std::unique_ptr<INode> b(NodeFactory::Instance().MakeNode(name));
               CopyParams(b.get(), a.get());
               std::vector<std::pair<std::string, std::string>> paramsB;
               Patch::SaveParams(b.get(), paramsB);
               if (paramsB != paramsA)
               {
                  copyFails++;
                  copyFailNames.push_back(name);
               }

               std::unique_ptr<INode> c(NodeFactory::Instance().MakeNode(name));
               Patch::LoadParams(c.get(), paramsA);
               std::vector<std::pair<std::string, std::string>> paramsC;
               Patch::SaveParams(c.get(), paramsC);
               if (paramsC != paramsA)
               {
                  loadFails++;
                  loadFailNames.push_back(name);
               }
            }
         }

         printf("round trip: %d types tested, %d with no params to test\n", typesTested, typesSkipped);
         for (const std::string& n : copyFailNames)
            printf("  copy/paste dropped values: %s\n", n.c_str());
         for (const std::string& n : loadFailNames)
            printf("  save/load dropped values: %s\n", n.c_str());
         printf("copy/paste: %d/%d types round trip  %s\n",
                typesTested - copyFails, typesTested, copyFails == 0 ? "OK" : "FAIL");
         printf("save/load:  %d/%d types round trip  %s\n",
                typesTested - loadFails, typesTested, loadFails == 0 ? "OK" : "FAIL");
         printf("%s\n", (copyFails == 0 && loadFails == 0) ? "ROUND TRIP OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASEFTEST") != nullptr && frameId == 4)
      {
         // Signed volume by the divergence theorem. A closed mesh whose
         // triangles all wind outward has positive volume; if any face is
         // flipped the contributions cancel and it collapses. This is the exact
         // check that caught the shattered metaballs, so every new closed
         // primitive gets it rather than a triangle count that proves nothing.
         auto volumeOf = [](const Mesh& m) {
            double vol = 0.0;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            {
               const Vertex& a = m.vertices[m.indices[t]];
               const Vertex& b = m.vertices[m.indices[t + 1]];
               const Vertex& c = m.vertices[m.indices[t + 2]];
               vol += ((double)a.px * ((double)b.py * c.pz - (double)c.py * b.pz)
                     - (double)a.py * ((double)b.px * c.pz - (double)c.px * b.pz)
                     + (double)a.pz * ((double)b.px * c.py - (double)c.px * b.py)) / 6.0;
            }
            return vol;
         };
         // Edges are matched by quantised *position*, not index: these solids are
         // flat-shaded, so adjacent faces never share a vertex index even when
         // they share an edge in space.
         auto openEdges = [](const Mesh& m) {
            auto key = [&](unsigned int i) {
               const Vertex& v = m.vertices[i];
               return std::to_string((long long)std::llround(v.px * 10000.0f)) + "," +
                      std::to_string((long long)std::llround(v.py * 10000.0f)) + "," +
                      std::to_string((long long)std::llround(v.pz * 10000.0f));
            };
            std::map<std::string, int> counts;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
               {
                  std::string a = key(m.indices[t + e]);
                  std::string b = key(m.indices[t + (e + 1) % 3]);
                  counts[a < b ? a + "|" + b : b + "|" + a]++;
               }
            int open = 0;
            for (const auto& kv : counts)
               if (kv.second != 2)
                  open++;
            return open;
         };

         bool ok = true;
         struct Solid { const char* name; Mesh mesh; size_t expectTris; };
         std::vector<Solid> solids = {
            { "tetrahedron", Primitives::Tetrahedron(), 12 },
            { "octahedron", Primitives::Octahedron(), 24 },
            // Twelve pentagons fanned from their centroids: 12 * 5.
            { "dodecahedron", Primitives::Dodecahedron(), 60 },
            { "rounded cube", Primitives::RoundedCube(8, 0.2f), 0 },
         };
         for (const Solid& s : solids)
         {
            const double vol = volumeOf(s.mesh);
            const int open = openEdges(s.mesh);
            const size_t tris = s.mesh.indices.size() / 3;
            const bool trisOk = s.expectTris == 0 || tris == s.expectTris;
            // Klein bottle is closed but non-orientable, so its signed volume
            // is meaningless - it is only required to be watertight.
            const bool volOk = (std::string(s.name) == "klein bottle") || vol > 1e-4;
            printf("%-14s %6zu tris, volume %+.4f, %d open edges  %s\n",
                   s.name, tris, vol, open,
                   (trisOk && volOk && open == 0) ? "OK" : "FAIL");
            ok = ok && trisOk && volOk && open == 0;
         }

         // The figure-8 Klein bottle is a closed surface, but its cross-section
         // passes through its own centre twice, so v = 0 and v = pi land on the
         // same point for every u. Position-keyed edge matching therefore sees
         // those rings as one and reports false tears. Index-keyed matching is
         // the correct test here: this surface is one shared grid, so equal
         // indices really do mean the same vertex.
         {
            const Mesh kb = Primitives::KleinBottle(24, 24);
            std::map<std::pair<unsigned int, unsigned int>, int> counts;
            for (size_t t = 0; t + 2 < kb.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
               {
                  unsigned int a = kb.indices[t + e];
                  unsigned int b = kb.indices[t + (e + 1) % 3];
                  counts[a < b ? std::make_pair(a, b) : std::make_pair(b, a)]++;
               }
            int open = 0;
            for (const auto& kv : counts)
               if (kv.second != 2)
                  open++;
            // And confirm the diagnosis rather than asserting it: exactly one
            // coincident partner per u ring, which is where the 24 phantom
            // open edges came from.
            std::set<std::string> distinct;
            for (const Vertex& v : kb.vertices)
               distinct.insert(std::to_string((long long)std::llround(v.px * 10000.0f)) + "," +
                               std::to_string((long long)std::llround(v.py * 10000.0f)) + "," +
                               std::to_string((long long)std::llround(v.pz * 10000.0f)));
            const size_t duplicates = kb.vertices.size() - distinct.size();
            // Index matching proves the seam is *joined*, not that it is joined
            // correctly - a naive wrap is equally closed but stitches mismatched
            // cross-sections together, which shows up as a few enormous quads
            // spanning the tube. Comparing the longest edge to the median is
            // what actually tests the (u + 2pi, v) = (u, -v) identification.
            double longest = 0.0;
            std::vector<double> lengths;
            for (const auto& kv : counts)
            {
               const Vertex& a = kb.vertices[kv.first.first];
               const Vertex& b = kb.vertices[kv.first.second];
               const double d = std::sqrt((double)(a.px-b.px)*(a.px-b.px) +
                                          (double)(a.py-b.py)*(a.py-b.py) +
                                          (double)(a.pz-b.pz)*(a.pz-b.pz));
               lengths.push_back(d);
               longest = std::max(longest, d);
            }
            std::sort(lengths.begin(), lengths.end());
            const double median = lengths[lengths.size() / 2];
            const bool seamOk = longest < median * 3.0;
            printf("klein bottle   %6zu tris, %d open edges by index, %zu self-touching vertices  %s\n",
                   kb.indices.size() / 3, open, duplicates,
                   (open == 0 && duplicates == 24) ? "OK" : "FAIL");
            printf("klein seam: longest edge %.4f vs median %.4f  %s\n",
                   longest, median, seamOk ? "OK" : "FAIL");
            ok = ok && open == 0 && duplicates == 24 && seamOk;
         }

         // A rounded cube must sit strictly between the cube it came from and
         // the sphere it would become - the one measurement that catches a
         // projection that rounds too much or not at all.
         const double rcVol = volumeOf(Primitives::RoundedCube(10, 0.2f));
         const bool rcOk = rcVol < 1.0 && rcVol > 0.5236;
         printf("rounded cube volume %.4f between sphere 0.5236 and cube 1.0  %s\n",
                rcVol, rcOk ? "OK" : "FAIL");
         ok = ok && rcOk;

         // The open surfaces are emitted with both windings, so their triangle
         // count is even and each is reachable from either side.
         const Mesh mob = Primitives::MobiusStrip(64, 4, 0.3f);
         const bool mobOk = !mob.Empty() && (mob.indices.size() / 3) % 2 == 0;
         printf("mobius strip %zu tris, doubled  %s\n", mob.indices.size() / 3,
                mobOk ? "OK" : "FAIL");
         ok = ok && mobOk;

         for (const auto& p : { std::make_pair("gear", Primitives::Gear(12, 0.3f, 0.25f, 0.15f)),
                                std::make_pair("star", Primitives::Star(5, 0.4f, 0.3f)),
                                std::make_pair("disc", Primitives::Disc(32, 0.0f)),
                                std::make_pair("arrow", Primitives::Arrow(16, 0.12f, 0.35f)) })
         {
            const bool nonEmpty = !p.second.Empty();
            printf("%-6s %zu tris  %s\n", p.first, p.second.indices.size() / 3,
                   nonEmpty ? "OK" : "FAIL");
            ok = ok && nonEmpty;
         }

         // --- 3D resynthesize ---
         GeometryNode src;
         src.shape = 2; // sphere
         src.CookIfNeeded(9001);

         auto evolve = [&](float seed, int steps) {
            auto node = std::make_unique<MeshResynthNode>();
            node->input = &src;
            node->seed = seed;
            node->chaos = 0.5f;
            for (int i = 0; i < steps; i++)
               node->StepOnce();
            node->CookIfNeeded(9002);
            return node;
         };
         auto a = evolve(3.0f, 5);
         auto b = evolve(3.0f, 5);
         auto c = evolve(11.0f, 5);

         auto samePositions = [](const Mesh& x, const Mesh& y) {
            if (x.vertices.size() != y.vertices.size())
               return false;
            for (size_t i = 0; i < x.vertices.size(); i++)
               if (std::fabs(x.vertices[i].px - y.vertices[i].px) > 1e-6f ||
                   std::fabs(x.vertices[i].py - y.vertices[i].py) > 1e-6f ||
                   std::fabs(x.vertices[i].pz - y.vertices[i].pz) > 1e-6f)
                  return false;
            return true;
         };

         // The whole promise of the node: a patch reopened tomorrow replays the
         // same evolution, and a different seed is genuinely a different one.
         const bool deterministic = samePositions(a->GetMesh(), b->GetMesh());
         const bool seedMatters = !samePositions(a->GetMesh(), c->GetMesh());
         printf("resynth generation %d, deterministic %d, seed changes result %d  %s\n",
                a->Generation(), (int)deterministic, (int)seedMatters,
                (deterministic && seedMatters && a->Generation() == 5) ? "OK" : "FAIL");
         ok = ok && deterministic && seedMatters && a->Generation() == 5;

         // And it must actually mutate: five generations that leave the sphere
         // untouched would pass every check above.
         const bool moved = !samePositions(a->GetMesh(), src.GetMesh());
         printf("resynth changed the mesh: %d  %s\n", (int)moved, moved ? "OK" : "FAIL");
         ok = ok && moved;

         // Reset must return to the source exactly, not approximately.
         a->Reset();
         a->CookIfNeeded(9003);
         const bool resetOk = samePositions(a->GetMesh(), src.GetMesh()) && a->Generation() == 0;
         printf("reset restores the input: %d  %s\n", (int)resetOk, resetOk ? "OK" : "FAIL");
         ok = ok && resetOk;

         // Subdivision at full weight, unattended, must stop at the budget.
         auto budget = std::make_unique<MeshResynthNode>();
         budget->input = &src;
         budget->weight[MeshResynthNode::kSubdivide] = 1.0f;
         budget->triangleBudget = 8000;
         for (int frame = 0; frame < 12; frame++)
         {
            for (int i = 0; i < 4; i++)
               budget->StepOnce();
            budget->CookIfNeeded(9100 + frame);
         }
         const size_t grown = budget->TriangleCount();
         const bool budgetOk = grown <= 8000;
         printf("subdivide budget: %zu tris, cap 8000  %s\n", grown, budgetOk ? "OK" : "FAIL");
         ok = ok && budgetOk;

         // --- image to point cloud ---
         ShapeNode circle;
         circle.shapeType = 0;
         circle.width = 256; circle.height = 256;
         circle.size = 0.25f;
         circle.posY = 0.75f;   // deliberately off-centre, see below
         circle.CookIfNeeded(9200);

         ImageToPointsNode i2p;
         i2p.Input().Connect(&circle);
         i2p.density = 64;
         i2p.threshold = 0.3f;
         i2p.height = 2.0f;
         i2p.CookIfNeeded(9201);

         const size_t pts = i2p.PointCount();
         const size_t grid = 64 * 64;
         // A circle of radius 0.25 covers pi*r^2 of the frame. If the threshold
         // were ignored we would get all 4096 points; if it rejected everything,
         // zero. Both failure modes are outside this band.
         const double coverage = (double)pts / (double)grid;
         const bool coverageOk = coverage > 0.10 && coverage < 0.30;
         printf("image to points: %zu of %zu (%.1f%% vs expected 19.6%%)  %s\n",
                pts, grid, coverage * 100.0, coverageOk ? "OK" : "FAIL");
         ok = ok && coverageOk;

         // The circle was placed at v = 0.75, so the cloud's centre of mass must
         // land at the matching height. This is what catches a vertical flip -
         // a centred test image would pass either way round.
         double meanY = 0.0;
         for (const Particle& p : i2p.GetPoints())
            meanY += p.py;
         if (pts > 0)
            meanY /= (double)pts;
         const double expectedY = (0.75 - 0.5) * 2.0;
         const bool orientOk = std::fabs(meanY - expectedY) < 0.15;
         printf("cloud centre y %.3f, image centre y %.3f  %s\n",
                meanY, expectedY, orientOk ? "OK" : "FAIL");
         ok = ok && orientOk;

         // Formula presets are compiled at runtime, so a typo in one ships as a
         // preset that silently does nothing. Compile every one.
         {
            FormulaNode fx;
            int failed = 0;
            for (int i = 0; i < (int)FormulaNode::PresetNames().size(); i++)
            {
               fx.LoadPreset(i);
               if (!fx.Apply())
               {
                  failed++;
                  printf("  preset \"%s\" failed: %s\n",
                         FormulaNode::PresetNames()[i].c_str(), fx.LastError().c_str());
               }
            }
            printf("formula presets: %zu compiled, %d failed  %s\n",
                   FormulaNode::PresetNames().size(), failed, failed == 0 ? "OK" : "FAIL");
            ok = ok && failed == 0;
         }

         // Same for the 2D shapes: they share one shader, but a new branch that
         // never runs would leave the shape rendering as whatever the fallback
         // is. Cook each and confirm it puts something on screen.
         {
            int blank = 0;
            for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
            {
               ShapeNode sh;
               sh.shapeType = i;
               sh.width = 64; sh.height = 64;
               sh.CookIfNeeded(9300 + i);
               unsigned char px[64 * 64 * 4] = { 0 };
               glBindTexture(GL_TEXTURE_2D, sh.GetOutputTexture());
               glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
               glBindTexture(GL_TEXTURE_2D, 0);
               int lit = 0;
               for (int q = 0; q < 64 * 64; q++)
                  if (px[q * 4] > 20 || px[q * 4 + 1] > 20 || px[q * 4 + 2] > 20)
                     lit++;
               // A shape that fills nothing is broken; one that fills the whole
               // frame means the distance field never went positive.
               const bool shapeOk = lit > 20 && lit < 64 * 64 - 20;
               if (!shapeOk)
               {
                  blank++;
                  printf("  2D shape \"%s\": %d of %d pixels lit  FAIL\n",
                         ShapeNode::ShapeNames()[i].c_str(), lit, 64 * 64);
               }
            }
            printf("2D shapes: %zu drawn, %d bad  %s\n",
                   ShapeNode::ShapeNames().size(), blank, blank == 0 ? "OK" : "FAIL");
            ok = ok && blank == 0;
         }

         printf("%s\n", ok ? "PHASE F OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASEETEST") != nullptr && frameId == 4)
      {
         // Signed volume via the divergence theorem. Triangle counts prove
         // nothing about a boolean - only the enclosed volume says whether the
         // operation actually did what it claims.
         auto volumeOf = [](const Mesh& m) {
            double vol = 0.0;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            {
               const Vertex& a = m.vertices[m.indices[t]];
               const Vertex& b = m.vertices[m.indices[t+1]];
               const Vertex& c = m.vertices[m.indices[t+2]];
               vol += ((double)a.px * ((double)b.py * c.pz - (double)b.pz * c.py) -
                       (double)a.py * ((double)b.px * c.pz - (double)b.pz * c.px) +
                       (double)a.pz * ((double)b.px * c.py - (double)b.py * c.px)) / 6.0;
            }
            return std::fabs(vol);
         };

         // Two unit cubes overlapping in exactly half their volume: the answers
         // are known in advance, which is what makes this a real check.
         const Mesh cubeA = Primitives::Cube(1);
         const Mesh cubeB = MeshOps::Transform(Primitives::Cube(1),
                                               Mat4::Translation(0.5f, 0.0f, 0.0f));
         const double vA = volumeOf(cubeA);
         const double overlap = 0.5;  // half of a unit cube

         const Mesh un = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanUnion);
         const Mesh inter = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanIntersect);
         const Mesh diff = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanDifference);

         const double vUnion = volumeOf(un);
         const double vInter = volumeOf(inter);
         const double vDiff = volumeOf(diff);

         printf("cube volume %.3f\n", vA);
         printf("  union      %.3f  (expect 1.500)  %s\n", vUnion,
                std::fabs(vUnion - 1.5) < 0.02 ? "OK" : "FAIL");
         printf("  intersect  %.3f  (expect 0.500)  %s\n", vInter,
                std::fabs(vInter - overlap) < 0.02 ? "OK" : "FAIL");
         printf("  difference %.3f  (expect 0.500)  %s\n", vDiff,
                std::fabs(vDiff - overlap) < 0.02 ? "OK" : "FAIL");

         // A difference must actually remove material, not just re-emit the
         // original, so its extent has to shrink on the side that was cut.
         float diffMax = -1e30f;
         for (const Vertex& v : diff.vertices)
            diffMax = std::max(diffMax, v.px);
         printf("  difference max x %.3f (cube was 0.500, cut from +x)  %s\n",
                diffMax, diffMax < 0.02f ? "OK" : "FAIL");

         const bool ok = std::fabs(vUnion - 1.5) < 0.02 &&
                         std::fabs(vInter - overlap) < 0.02 &&
                         std::fabs(vDiff - overlap) < 0.02 && diffMax < 0.02f;
         printf("%s\n", ok ? "PHASE E OK" : "SUSPECT");
      }

      if (getenv("INFINITE_WRAPTEST") != nullptr && frameId == 4)
      {
         // The whole point of the cylindrical mode is that it is a coordinate
         // remap, not a projection: it must preserve arc length exactly. A
         // strip of known width W bent around a target of known radius R has
         // an answer worked out in advance - every point at distance R from
         // the axis, spanning exactly W/R radians - so this is a real check
         // and not just "did something move".
         const float R = 2.0f;
         const float W = 3.0f;
         const float H = 0.4f;

         // Thin flat strip in the XY plane, W wide and H tall, at z = 0.
         Mesh strip;
         const int kCols = 40;
         for (int i = 0; i <= kCols; i++)
         {
            const float x = -W * 0.5f + W * (float)i / (float)kCols;
            Vertex a; a.px = x; a.py = -H * 0.5f; a.pz = 0.0f;
            Vertex b; b.px = x; b.py =  H * 0.5f; b.pz = 0.0f;
            strip.vertices.push_back(a);
            strip.vertices.push_back(b);
         }
         for (int i = 0; i < kCols; i++)
         {
            const unsigned int base = (unsigned int)(i * 2);
            strip.indices.push_back(base); strip.indices.push_back(base + 1); strip.indices.push_back(base + 2);
            strip.indices.push_back(base + 1); strip.indices.push_back(base + 3); strip.indices.push_back(base + 2);
         }

         // Target: a sphere of radius R centred at the origin, so the bend
         // radius is read from real geometry rather than the override. The
         // primitive is unit *diameter*, hence the 2R scale.
         const Mesh sphere = MeshOps::Transform(Primitives::Sphere(24, 32),
                                                Mat4::Scale(2.0f * R, 2.0f * R, 2.0f * R));

         auto spanOf = [](const Mesh& m, float& minR, float& maxR, float& minA, float& maxA) {
            minR = 1e30f; maxR = -1e30f; minA = 1e30f; maxA = -1e30f;
            for (const Vertex& v : m.vertices)
            {
               const float rad = std::sqrt(v.px * v.px + v.pz * v.pz);
               const float ang = std::atan2(v.px, v.pz);
               minR = std::min(minR, rad); maxR = std::max(maxR, rad);
               minA = std::min(minA, ang); maxA = std::max(maxA, ang);
            }
         };

         const Mesh cyl = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        false, false, false);
         float minR, maxR, minA, maxA;
         spanOf(cyl, minR, maxR, minA, maxA);
         const float radiusErr = std::max(std::fabs(minR - R), std::fabs(maxR - R));
         const float span = maxA - minA;
         const bool radiusOk = radiusErr < 1e-3f;
         const bool spanOk = std::fabs(span - W / R) < 1e-3f;
         printf("  cylindrical radius %.5f..%.5f (expect %.3f)  %s\n", minR, maxR, R,
                radiusOk ? "OK" : "FAIL");
         printf("  cylindrical span   %.5f rad (expect W/R = %.5f)  %s\n", span, W / R,
                spanOk ? "OK" : "FAIL");

         // fit around: the same strip must now close a full circle.
         const Mesh fit = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        true, false, false);
         // atan2 wraps at +/-pi, so a full turn cannot be read off min/max.
         // Walk the strip column by column instead and accumulate unwrapped
         // angle deltas - that measures the real total sweep.
         const float kPi = 3.14159265358979f;
         float fitSweep = 0.0f, prevAng = std::atan2(fit.vertices[0].px, fit.vertices[0].pz);
         float fitRadErr = 0.0f;
         for (int i = 1; i <= kCols; i++)
         {
            const Vertex& v = fit.vertices[(size_t)i * 2];
            const float ang = std::atan2(v.px, v.pz);
            float d = ang - prevAng;
            while (d >  kPi) d -= 2.0f * kPi;
            while (d < -kPi) d += 2.0f * kPi;
            fitSweep += d;
            prevAng = ang;
            fitRadErr = std::max(fitRadErr,
                                 std::fabs(std::sqrt(v.px * v.px + v.pz * v.pz) - R));
         }
         const bool fitOk = std::fabs(std::fabs(fitSweep) - 2.0f * kPi) < 1e-3f && fitRadErr < 1e-3f;
         printf("  fit around sweep   %.5f rad (expect 2pi = %.5f)  %s\n", fitSweep,
                2.0f * kPi, fitOk ? "OK" : "FAIL");

         // Spherical must not push anything outside the shell it bends onto:
         // the strip is flat (zero depth), so R is the ceiling.
         const Mesh sph = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapSpherical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        false, false, false);
         float sphMax = 0.0f;
         for (const Vertex& v : sph.vertices)
            sphMax = std::max(sphMax, std::sqrt(v.px * v.px + v.py * v.py + v.pz * v.pz));
         const bool sphOk = sphMax <= R + 1e-3f;
         printf("  spherical max |p|  %.5f (expect <= %.3f)  %s\n", sphMax, R,
                sphOk ? "OK" : "FAIL");

         // A radius override with no target at all still has to bend, and the
         // override is used verbatim as the radius (radiusScale is ignored -
         // there is no derived radius for it to scale).
         const Mesh noTarget = MeshOps::Wrap(strip, Mat4::Identity(), Mesh(), Mat4::Identity(),
                                             MeshOps::kWrapCylindrical, 0.0f, 1.0f, R, 0.25f, 1,
                                             false, false, false);
         float ntMinR, ntMaxR, ntMinA, ntMaxA;
         spanOf(noTarget, ntMinR, ntMaxR, ntMinA, ntMaxA);
         const bool ntOk = std::fabs(ntMaxR - R) < 1e-3f && std::fabs((ntMaxA - ntMinA) - W / R) < 1e-3f;
         printf("  no target, radius %.1f: span %.5f rad  %s\n", R, ntMaxA - ntMinA,
                ntOk ? "OK" : "FAIL");

         // Nearest surface still works, and (unlike the bend) collapses the
         // strip's width - the behaviour the bend modes exist to avoid.
         const Mesh nearest = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                            MeshOps::kWrapNearest, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                            false, false, false);
         const bool nearOk = nearest.vertices.size() == strip.vertices.size() && !nearest.Empty();
         printf("  nearest surface    %zu verts  %s\n", nearest.vertices.size(),
                nearOk ? "OK" : "FAIL");

         // The bug this guards: the radius must never stop tracking the
         // target. Scale the target's model matrix 2x with node params held
         // identical and the bend radius has to double on its own.
         const Mesh scaled = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Scale(2.0f, 2.0f, 2.0f),
                                           MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                           false, false, false);
         float scMinR, scMaxR, scMinA, scMaxA;
         spanOf(scaled, scMinR, scMaxR, scMinA, scMaxA);
         const bool trackOk = std::fabs(scMaxR - 2.0f * R) < 1e-3f && std::fabs(scMinR - 2.0f * R) < 1e-3f;
         printf("  target scaled 2x:  radius %.5f..%.5f (was %.5f, expect %.3f)  %s\n",
                scMinR, scMaxR, maxR, 2.0f * R, trackOk ? "OK" : "FAIL");

         // And radius scale is a multiplier on that derived radius, not a
         // replacement for it.
         const Mesh halfScale = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                              MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 0.5f, 1,
                                              false, false, false);
         float hsMinR, hsMaxR, hsMinA, hsMaxA;
         spanOf(halfScale, hsMinR, hsMaxR, hsMinA, hsMaxA);
         const bool scaleOk = std::fabs(hsMaxR - 0.5f * R) < 1e-3f && std::fabs(hsMinR - 0.5f * R) < 1e-3f;
         printf("  radius scale 0.5:  radius %.5f..%.5f (expect %.3f)  %s\n",
                hsMinR, hsMaxR, 0.5f * R, scaleOk ? "OK" : "FAIL");

         const bool ok = radiusOk && spanOk && fitOk && sphOk && ntOk && nearOk &&
                         trackOk && scaleOk;
         printf("%s\n", ok ? "WRAP OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASEDTEST") != nullptr && frameId == 4)
      {
         // Curves of every kind must produce a usable polyline and a tube.
         bool allKinds = true;
         for (int kind = 0; kind < 4; kind++)
         {
            std::vector<float> control = { -1,0,0,  -0.3f,0.8f,0,  0.3f,-0.8f,0,  1,0,0 };
            const Polyline line = MeshOps::BuildCurve(control, kind, 16, false);
            const Mesh tube = MeshOps::TubeAlong(line, 0.05f, 8, 0.0f);
            const bool ok = line.Count() >= 4 && !tube.Empty();
            printf("  curve %-12s %zu points, %zu tris  %s\n",
                   CurveNode::KindNames()[kind].c_str(), line.Count(),
                   tube.indices.size() / 3, ok ? "OK" : "FAIL");
            if (!ok) allKinds = false;
         }

         // Arc-length sampling: equal steps in t must cover roughly equal
         // distance. Sampling by index instead would crawl where control points
         // bunch and race where they spread.
         std::vector<float> control = { -1,0,0,  -0.9f,0.1f,0,  0.9f,0.1f,0,  1,0,0 };
         const Polyline line = MeshOps::BuildCurve(control, MeshOps::kCurveCatmullRom, 24, false);
         float prev[3], tangent[3], minStep = 1e30f, maxStep = 0.0f;
         MeshOps::SamplePolyline(line, 0.0f, prev, tangent);
         for (int i = 1; i <= 20; i++)
         {
            float pos[3];
            MeshOps::SamplePolyline(line, (float)i / 20.0f, pos, tangent);
            const float d = std::sqrt((pos[0]-prev[0])*(pos[0]-prev[0]) +
                                      (pos[1]-prev[1])*(pos[1]-prev[1]) +
                                      (pos[2]-prev[2])*(pos[2]-prev[2]));
            minStep = std::min(minStep, d);
            maxStep = std::max(maxStep, d);
            prev[0] = pos[0]; prev[1] = pos[1]; prev[2] = pos[2];
         }
         const float ratio = (minStep > 1e-6f) ? maxStep / minStep : 1e30f;
         printf("arc-length sampling: step ratio %.2f (1.0 is perfectly even)  %s\n",
                ratio, ratio < 1.6f ? "OK" : "FAIL");
         const bool evenSpeed = ratio < 1.6f;

         // Slicing a sphere through its centre must give one closed contour,
         // and every point on it must sit at the sphere's radius - that is what
         // proves it is really following the surface and not something else.
         const Mesh sphere = Primitives::Sphere(24, 32);
         const std::vector<Polyline> contours = MeshOps::SliceContours(sphere, 1, 0.0f);
         bool sliceOk = false;
         if (!contours.empty())
         {
            float minR = 1e30f, maxR = 0.0f;
            for (size_t i = 0; i < contours[0].Count(); i++)
            {
               const float x = contours[0].points[i*3];
               const float z = contours[0].points[i*3+2];
               const float r = std::sqrt(x*x + z*z);
               minR = std::min(minR, r); maxR = std::max(maxR, r);
            }
            sliceOk = contours[0].closed && contours[0].Count() > 16 &&
                      std::fabs(maxR - 0.5f) < 0.02f && std::fabs(minR - 0.5f) < 0.02f;
            printf("sphere slice: %zu contours, %zu points, closed=%d, radius %.3f..%.3f  %s\n",
                   contours.size(), contours[0].Count(), (int)contours[0].closed,
                   minR, maxR, sliceOk ? "OK" : "FAIL");
         }
         else
         {
            printf("sphere slice: no contours  FAIL\n");
         }

         // A plane has a real boundary, so that path needs no fallback.
         const std::vector<Polyline> edges = MeshOps::BoundaryLoops(Primitives::Plane(4));
         const bool boundaryOk = !edges.empty() && edges[0].closed && edges[0].Count() >= 16;
         printf("plane boundary: %zu loops, %zu points, closed=%d  %s\n",
                edges.size(), edges.empty() ? 0 : edges[0].Count(),
                edges.empty() ? 0 : (int)edges[0].closed, boundaryOk ? "OK" : "FAIL");

         printf("%s\n", (allKinds && evenSpeed && sliceOk && boundaryOk)
                            ? "PHASE D OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASECTEST") != nullptr && frameId == 4)
      {
         // Every new primitive must produce a finite, non-degenerate mesh.
         bool allPrims = true;
         for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
         {
            INode* made = NodeFactory::Instance().MakeNode(GeometryNode::ShapeNames()[i]);
            auto* geo = dynamic_cast<GeometryNode*>(made);
            bool ok = false;
            if (geo != nullptr)
            {
               const Mesh& m = geo->GetMesh();
               bool finite = true;
               float lo = 1e30f, hi = -1e30f;
               for (const Vertex& v : m.vertices)
               {
                  if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz))
                     finite = false;
                  lo = std::min(lo, v.px); hi = std::max(hi, v.px);
               }
               ok = finite && m.indices.size() >= 3 && (hi - lo) > 0.01f && (hi - lo) < 100.0f;
               printf("  %-12s %6zu tris  width %.2f  %s\n",
                      GeometryNode::ShapeNames()[i].c_str(), m.indices.size() / 3,
                      hi - lo, ok ? "OK" : "FAIL");
            }
            if (!ok) allPrims = false;
            delete made;
         }

         // Two balls far apart give two separate surfaces; brought together
         // they must merge into one. That merging is the entire reason to use
         // metaballs rather than joining two spheres, so it is what to test.
         std::vector<Primitives::MetaBall> apart = {
            { -1.1f, 0.0f, 0.0f, 0.25f }, { 1.1f, 0.0f, 0.0f, 0.25f }
         };
         std::vector<Primitives::MetaBall> together = {
            { -0.25f, 0.0f, 0.0f, 0.25f }, { 0.25f, 0.0f, 0.0f, 0.25f }
         };
         const Mesh mApart = Primitives::MetaBalls(apart, 40, 1.0f, 2.0f);
         const Mesh mTogether = Primitives::MetaBalls(together, 40, 1.0f, 2.0f);

         // Merged is detected by whether the surface spans the midpoint: apart,
         // nothing exists near x=0; merged, the bridge does.
         auto spansCentre = [](const Mesh& m) {
            for (const Vertex& v : m.vertices)
               if (std::fabs(v.px) < 0.05f)
                  return true;
            return false;
         };
         printf("metaballs: apart %zu tris (spans centre %d), together %zu tris (spans centre %d)\n",
                mApart.indices.size() / 3, (int)spansCentre(mApart),
                mTogether.indices.size() / 3, (int)spansCentre(mTogether));
         const bool merges = !mApart.Empty() && !mTogether.Empty() &&
                             !spansCentre(mApart) && spansCentre(mTogether);

         // Merging alone is not enough: the surface has to be closed and
         // consistently wound, or backface culling eats the wrongly-facing half
         // and the blob renders shattered. Every edge of a watertight, coherently
         // oriented mesh is traversed once in each direction, so a directed edge
         // seen twice the same way means two triangles disagree about facing.
         auto surfaceIntact = [](const Mesh& m, const char* label) {
            const std::vector<unsigned int> weld = MeshOps::BuildWeldMap(m);
            std::map<std::pair<unsigned int, unsigned int>, int> directed;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
                  directed[{ weld[m.indices[t + e]], weld[m.indices[t + (e + 1) % 3]] }]++;

            size_t boundary = 0, flipped = 0;
            for (const auto& entry : directed)
            {
               if (entry.second > 1)
                  flipped++;
               const auto opposite = directed.find({ entry.first.second, entry.first.first });
               if (opposite == directed.end())
                  boundary++;
            }
            const bool ok = boundary == 0 && flipped == 0;
            printf("  %s: %zu open edges, %zu inconsistently wound  %s\n",
                   label, boundary, flipped, ok ? "watertight" : "BROKEN");
            return ok;
         };
         const bool intact = surfaceIntact(mApart, "apart") &&
                             surfaceIntact(mTogether, "together");

         printf("%s\n", (allPrims && merges && intact) ? "PHASE C OK" : "SUSPECT");
      }

      if (getenv("INFINITE_MAPTEST") != nullptr && frameId >= 4 && frameId <= 16 && frameId % 4 == 0)
      {
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         auto* noise = gNodes[2].node.get();

         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         static std::vector<unsigned char> sBase;
         static int sPassed = 0;
         auto differs = [&](const char* label) {
            size_t changed = 0;
            for (size_t i = 0; i + 3 < px.size() && i < sBase.size(); i += 4)
               if (std::abs((int)px[i] - (int)sBase[i]) > 6) changed++;
            const bool ok = changed > 200;
            printf("  %-10s %zu px changed  %s\n", label, changed, ok ? "OK" : "FAIL");
            if (ok) sPassed++;
         };

         // Each channel is patched in turn against the same untouched baseline,
         // so a channel that silently did nothing shows up as zero change.
         if (frameId == 4)      { sBase = px; mat->MapInput(kMapRoughness).Connect(noise); }
         else if (frameId == 8) { differs("roughness"); mat->MapInput(kMapRoughness).Disconnect();
                                  mat->MapInput(kMapNormal).Connect(noise); }
         else if (frameId == 12){ differs("normal"); mat->MapInput(kMapNormal).Disconnect();
                                  mat->MapInput(kMapAmbientOcclusion).Connect(noise); }
         else if (frameId == 16){ differs("ao");
                                  printf("%s\n", sPassed == 3 ? "MATERIAL MAPS OK" : "SUSPECT"); }
      }

      if (getenv("INFINITE_SHADOWTEST") != nullptr && (frameId == 4 || frameId == 8))
      {
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         static std::vector<unsigned char> sNoShadow;
         if (frameId == 4)
         {
            sNoShadow = px;
            render->shadowsEnabled = true;
            printf("shadow map: %dx%d\n", render->ActiveShadowSize(), render->ActiveShadowSize());
         }
         else
         {
            // Only some pixels should change, and every one of them should get
            // *darker*. A shadow that brightened anything would mean the depth
            // comparison is inverted; a change everywhere would mean the whole
            // image dimmed rather than a shadow being cast.
            size_t darker = 0, brighter = 0;
            for (size_t i = 0; i + 3 < px.size() && i < sNoShadow.size(); i += 4)
            {
               const int before = sNoShadow[i] + sNoShadow[i+1] + sNoShadow[i+2];
               const int after = px[i] + px[i+1] + px[i+2];
               if (after < before - 12) darker++;
               else if (after > before + 12) brighter++;
            }
            const double pct = 100.0 * (double)darker / (double)(w * h);
            printf("shadow: %zu px darker (%.1f%%), %zu brighter, active=%d\n",
                   darker, pct, brighter, render->ActiveShadowSize() > 0);
            const bool ok = darker > 500 && pct < 60.0 && brighter < darker / 10 &&
                            render->ActiveShadowSize() > 0;
            printf("%s\n", ok ? "SHADOWS OK" : "SUSPECT");
         }
      }

      if (getenv("INFINITE_BUGTEST") != nullptr && frameId == 6)
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* arr = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* ja = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());

         // 1. Saving from outside the editor context must not crash. This is
         //    the exact path the File menu and Cmd+S take.
         const bool saved = SavePatchTo("/tmp/infinite_bugtest.infinite");
         printf("save from outside editor context: %d\n", (int)saved);

         // 2. Moving a Geometry node must move what an operator downstream
         //    renders. The operator forwards the transform rather than
         //    flattening it to identity.
         geo->posX = 0.0f;
         const float before = arr->GetModelMatrix().m[12];
         geo->posX = 2.5f;
         const float after = arr->GetModelMatrix().m[12];
         printf("array transform follows input: %.2f -> %.2f  %s\n", before, after,
                std::fabs(after - 2.5f) < 1e-4f ? "OK" : "FAIL");

         // 3. Moving an input of Join Geometry must change the merged mesh,
         //    since the transform is baked into its vertices.
         ja->posX = 0.0f;
         join->GetMesh();
         float joinBefore = -1e30f;
         for (const Vertex& v : join->GetMesh().vertices)
            joinBefore = std::max(joinBefore, v.px);
         ja->posX = 3.0f;
         join->GetMesh();
         float joinAfter = -1e30f;
         for (const Vertex& v : join->GetMesh().vertices)
            joinAfter = std::max(joinAfter, v.px);
         printf("join rebuilds on input move: max x %.2f -> %.2f  %s\n",
                joinBefore, joinAfter, joinAfter > joinBefore + 2.0f ? "OK" : "FAIL");

         // 4. Instance on Points must follow both the point source's and the
         //    shape's own transform - previously both were dropped, so
         //    moving/scaling either object silently did nothing to the render.
         GeometryNode pointsSrc;
         pointsSrc.shape = 0; // plane
         pointsSrc.detail = 4;
         GeometryNode shapeSrc;
         shapeSrc.shape = 2; // sphere

         InstanceOnPointsNode inst;
         inst.pointSource = &pointsSrc;
         inst.instanceShape = &shapeSrc;
         inst.pointMode = 0; // vertices
         inst.maxPoints = 50;
         inst.instanceScale = 1.0f;
         inst.scaleRandom = 0.0f;
         inst.rotationRandom = 0.0f;
         inst.alignToNormal = false;
         inst.CookIfNeeded(9500);
         const bool hadInstances = inst.InstanceCount() > 0;
         const Mat4 firstBefore = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();

         pointsSrc.posX = 5.0f;
         inst.CookIfNeeded(9501);
         const Mat4 firstAfterPointMove = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();
         const float pointDx = firstAfterPointMove.m[12] - firstBefore.m[12];

         shapeSrc.posY = 7.0f;
         inst.CookIfNeeded(9502);
         const Mat4 firstAfterShapeMove = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();
         const float shapeDy = firstAfterShapeMove.m[13] - firstAfterPointMove.m[13];

         const bool instancingFollowsTransforms = hadInstances &&
            std::fabs(pointDx - 5.0f) < 1e-3f && std::fabs(shapeDy - 7.0f) < 1e-3f;
         printf("instance on points follows source transforms: point dx=%.2f (want 5.00) "
                "shape dy=%.2f (want 7.00)  %s\n", pointDx, shapeDy,
                instancingFollowsTransforms ? "OK" : "FAIL");

         // 5. Cloth must drape from the input's transformed rest pose, not its
         //    raw object-space one - previously the input's own transform was
         //    dropped entirely when seeding the simulation.
         GeometryNode clothSrc;
         clothSrc.shape = 0; // plane
         clothSrc.detail = 4;
         ClothNode cloth;
         cloth.input = &clothSrc;
         cloth.pinMode = ClothNode::kPinNone;
         cloth.gravityX = cloth.gravityY = cloth.gravityZ = 0.0f;
         cloth.windX = cloth.windY = cloth.windZ = 0.0f;
         cloth.CookIfNeeded(9500);
         const bool hadClothVerts = !cloth.GetMesh().vertices.empty();
         const float clothVxBefore = hadClothVerts ? cloth.GetMesh().vertices[0].px : 0.0f;

         clothSrc.posX = 5.0f;
         cloth.CookIfNeeded(9501);
         const float clothVxAfter = hadClothVerts ? cloth.GetMesh().vertices[0].px : 0.0f;
         const bool clothFollowsTransform = hadClothVerts &&
            std::fabs((clothVxAfter - clothVxBefore) - 5.0f) < 1e-3f;
         printf("cloth rest pose follows input transform: dx=%.2f (want 5.00)  %s\n",
                clothVxAfter - clothVxBefore, clothFollowsTransform ? "OK" : "FAIL");

         // 6. A path following a mesh boundary must follow it in world space -
         //    previously the geometry source's transform was dropped, so the
         //    followed contour stayed put when the source moved.
         GeometryNode pathSrc;
         pathSrc.shape = 0; // plane, open so it has a boundary loop
         pathSrc.detail = 4;
         PathNode path;
         path.geometrySource = &pathSrc;
         path.followMode = PathNode::kFollowBoundary;
         path.speed = 0.0f;
         path.phase = 0.0f;
         path.pingPong = false;
         path.sizeX = path.sizeY = path.sizeZ = 1.0f;
         path.CookIfNeeded(9500);
         float pathBefore[3]; path.CurrentPoint(pathBefore);
         const bool pathHasFollow = path.IsFollowing();

         pathSrc.posX = 5.0f;
         path.CookIfNeeded(9501);
         float pathAfter[3]; path.CurrentPoint(pathAfter);
         const bool pathFollowsTransform = pathHasFollow &&
            std::fabs((pathAfter[0] - pathBefore[0]) - 5.0f) < 1e-3f;
         printf("path follow tracks geometry source transform: dx=%.2f (want 5.00)  %s\n",
                pathAfter[0] - pathBefore[0], pathFollowsTransform ? "OK" : "FAIL");

         const bool allOk = saved && std::fabs(after - 2.5f) < 1e-4f &&
                            joinAfter > joinBefore + 2.0f && instancingFollowsTransforms &&
                            clothFollowsTransform && pathFollowsTransform;
         printf("%s\n", allOk ? "BUGFIXES OK" : "SUSPECT");
      }

      // Sweeps every node type that consumes an IGeometrySource, checking one
      // thing: does moving/rotating/scaling its upstream source actually move
      // the final world-space result? BUGTEST above proves specific fixtures
      // stay fixed; this proves the same property for every node that takes a
      // geometry input, generically, so a newly added node type is covered
      // without anyone having to remember to hand-write a fixture for it.
      if (getenv("INFINITE_TRANSFORMSWEEPTEST") != nullptr && frameId == 6)
      {
         // Stands in for any upstream source: forwards mesh/material/etc to a
         // real node, but returns an arbitrary injected matrix from
         // GetModelMatrix(). This decouples "does the consumer apply/track its
         // input's transform" from needing to know that input's own field
         // names (posX vs pos[0] vs no transform at all) - the one thing every
         // IGeometrySource is required to answer is GetModelMatrix().
         struct TransformProbeSource : public IGeometrySource
         {
            IGeometrySource* wrapped = nullptr;
            Mat4 matrix;
            const Mesh& GetMesh() override { return wrapped->GetMesh(); }
            unsigned long long MeshRevision() override { return wrapped->MeshRevision(); }
            Mat4 GetModelMatrix() const override { return matrix; }
            Material GetMaterial() const override { return wrapped->GetMaterial(); }
            unsigned int GetSurfaceTexture() override { return wrapped->GetSurfaceTexture(); }
         };

         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube: closed, several vertices, no degenerate cases
         probeMesh.detail = 4;
         TransformProbeSource probe;
         probe.wrapped = &probeMesh;

         int frame = 20000;
         auto cook = [&](IGeometrySource* g) {
            if (auto* n = dynamic_cast<INode*>(g)) n->CookIfNeeded(frame);
            frame++;
         };

         struct Result { std::string name; bool ok; bool hadGeometry; float dx, dy, dz; };
         std::vector<Result> results;

         // The shared invariant: whatever a node's GetMesh() contains, once
         // combined with its own GetModelMatrix() the result is where the
         // object actually sits in the scene - that combination is exactly
         // what Render 3D uses to draw it. Nudging the probe's matrix by a
         // distinct, non-uniform amount per axis catches an axis swap or a
         // dropped component, not just "moved by something".
         auto checkGeneric = [&](const char* name, IGeometrySource* node)
         {
            probe.matrix = Mat4::Identity();
            cook(node);
            const Mesh before = MeshOps::Transform(node->GetMesh(), node->GetModelMatrix());

            probe.matrix = Mat4::Translation(5.0f, 7.0f, 3.0f);
            cook(node);
            const Mesh after = MeshOps::Transform(node->GetMesh(), node->GetModelMatrix());

            const bool hadGeometry = !before.vertices.empty() && !after.vertices.empty() &&
                                     before.vertices.size() == after.vertices.size();
            float dx = 0, dy = 0, dz = 0;
            bool ok = false;
            if (hadGeometry)
            {
               dx = after.vertices[0].px - before.vertices[0].px;
               dy = after.vertices[0].py - before.vertices[0].py;
               dz = after.vertices[0].pz - before.vertices[0].pz;
               ok = std::fabs(dx - 5.0f) < 1e-3f && std::fabs(dy - 7.0f) < 1e-3f &&
                    std::fabs(dz - 3.0f) < 1e-3f;
            }
            results.push_back({ name, ok, hadGeometry, dx, dy, dz });
         };

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &probe;
         checkGeneric("GeometryOpNode", &opNode);

         DisplacementNode dispNode; dispNode.input = &probe;
         checkGeneric("DisplacementNode", &dispNode);

         MeshResynthNode resynthNode; resynthNode.input = &probe;
         checkGeneric("MeshResynthNode", &resynthNode);

         MeshToPointsNode m2pNode; m2pNode.input = &probe; m2pNode.mode = 0;
         checkGeneric("MeshToPointsNode", &m2pNode);

         Null3DNode nullNode; nullNode.input = &probe;
         checkGeneric("Null3DNode", &nullNode);

         MaterialNode matNode; matNode.input = &probe;
         checkGeneric("MaterialNode", &matNode);

         MappingNode mapNode; mapNode.input = &probe;
         checkGeneric("MappingNode", &mapNode);

         JoinGeometryNode joinNode; joinNode.mode = JoinGeometryNode::kMerge; joinNode.inputs[0] = &probe;
         checkGeneric("JoinGeometryNode", &joinNode);

         // Wrap needs a real, non-degenerate target to project onto - a
         // stand-in sphere, fixed in place (not run through the probe matrix,
         // since the invariant under test is "does the *source* input's
         // transform reach the output", not the target's).
         GeometryNode probe2;
         probe2.shape = 2; // sphere: real surface for the source to snap onto
         probe2.detail = 4;
         WrapNode wrapNode; wrapNode.sourceInput = &probe; wrapNode.targetInput = &probe2;
         // Snapping onto a fixed target is inherently non-linear (the closest
         // point on the target can change discontinuously as the source
         // moves), so it would not reproduce the exact +5/+7/+3 shift this
         // check looks for even when working correctly. Blend 0 isolates the
         // one thing this sweep actually checks - that the source's own
         // transform reaches the output - from the wrap projection itself,
         // which has its own coverage.
         wrapNode.blend = 0.0f;
         checkGeneric("WrapNode", &wrapNode);

         ClothNode clothNode;
         clothNode.input = &probe;
         clothNode.pinMode = ClothNode::kPinNone;
         clothNode.gravityX = clothNode.gravityY = clothNode.gravityZ = 0.0f;
         clothNode.windX = clothNode.windY = clothNode.windZ = 0.0f;
         checkGeneric("ClothNode", &clothNode);

         // Pinned to the probe's slot so the sweep exercises the forwarding
         // path, not the clock-driven switching itself (covered separately).
         Switcher3DNode sw3Node;
         sw3Node.inputs[0] = &probe;
         sw3Node.manual = true;
         sw3Node.manualSlot = 0;
         checkGeneric("Switcher3DNode", &sw3Node);

         // Instance on Points draws through per-instance transforms rather
         // than GetMesh()+GetModelMatrix(), so it needs its own probe on each
         // of its two geometry slots instead of the generic check.
         auto checkInstancing = [&](const char* name, bool probeIsPointSource)
         {
            GeometryNode otherSide;
            otherSide.shape = 2; // sphere
            InstanceOnPointsNode inst;
            inst.pointSource = probeIsPointSource ? (IGeometrySource*)&probe : (IGeometrySource*)&otherSide;
            inst.instanceShape = probeIsPointSource ? (IGeometrySource*)&otherSide : (IGeometrySource*)&probe;
            inst.pointMode = 0; // vertices
            inst.maxPoints = 50;
            inst.instanceScale = 1.0f;
            inst.scaleRandom = 0.0f;
            inst.rotationRandom = 0.0f;
            inst.alignToNormal = false;

            probe.matrix = Mat4::Identity();
            cook(&inst);
            const bool hadBefore = inst.InstanceCount() > 0;
            const Mat4 before = hadBefore ? inst.InstanceTransforms()[0] : Mat4::Identity();

            probe.matrix = Mat4::Translation(5.0f, 7.0f, 3.0f);
            cook(&inst);
            const bool hadAfter = inst.InstanceCount() > 0;
            const Mat4 after = hadAfter ? inst.InstanceTransforms()[0] : Mat4::Identity();

            const bool hadGeometry = hadBefore && hadAfter;
            float dx = 0, dy = 0, dz = 0;
            bool ok = false;
            if (hadGeometry)
            {
               dx = after.m[12] - before.m[12];
               dy = after.m[13] - before.m[13];
               dz = after.m[14] - before.m[14];
               ok = std::fabs(dx - 5.0f) < 1e-3f && std::fabs(dy - 7.0f) < 1e-3f &&
                    std::fabs(dz - 3.0f) < 1e-3f;
            }
            results.push_back({ name, ok, hadGeometry, dx, dy, dz });
         };
         checkInstancing("InstanceOnPointsNode(pointSource)", true);
         checkInstancing("InstanceOnPointsNode(instanceShape)", false);

         // Path-follow drives a modulator output rather than a mesh, so its
         // check reads CurrentPoint() instead of GetMesh()+GetModelMatrix().
         // Boundary-follow needs an open mesh (a closed cube has no boundary
         // loop), so the probe is switched to wrap a plane for this one check
         // - nothing after this reuses probeMesh/probe.
         {
            probeMesh.shape = 0; // plane
            PathNode path;
            path.geometrySource = &probe;
            path.followMode = PathNode::kFollowBoundary;
            path.speed = 0.0f; path.phase = 0.0f; path.pingPong = false;
            path.sizeX = path.sizeY = path.sizeZ = 1.0f;

            probe.matrix = Mat4::Identity();
            path.CookIfNeeded(frame++);
            float before[3]; path.CurrentPoint(before);
            const bool hadBefore = path.IsFollowing();

            probe.matrix = Mat4::Translation(5.0f, 7.0f, 3.0f);
            path.CookIfNeeded(frame++);
            float after[3]; path.CurrentPoint(after);
            const bool hadAfter = path.IsFollowing();

            const bool hadGeometry = hadBefore && hadAfter;
            float dx = 0, dy = 0, dz = 0;
            bool ok = false;
            if (hadGeometry)
            {
               dx = after[0] - before[0]; dy = after[1] - before[1]; dz = after[2] - before[2];
               ok = std::fabs(dx - 5.0f) < 1e-3f && std::fabs(dy - 7.0f) < 1e-3f &&
                    std::fabs(dz - 3.0f) < 1e-3f;
            }
            results.push_back({ "PathNode(follow)", ok, hadGeometry, dx, dy, dz });
         }

         bool allOk = true;
         for (const Result& r : results)
         {
            if (!r.hadGeometry)
            {
               printf("  [SKIP] %-32s — produced no comparable geometry\n", r.name.c_str());
               continue;
            }
            printf("  [%s] %-32s dx=%.2f dy=%.2f dz=%.2f (want 5.00 7.00 3.00)\n",
                   r.ok ? "pass" : "FAIL", r.name.c_str(), r.dx, r.dy, r.dz);
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "TRANSFORM SWEEP OK" : "TRANSFORM SWEEP FAIL");
      }

      // Sibling of TRANSFORMSWEEPTEST, same generic-probe approach, checking a
      // different side-channel: does GetMappingTransform() reach a node's
      // output from its input? Found via a real bug: ClothNode, MeshResynthNode
      // and MeshToPointsNode forwarded every other side-channel (material,
      // textures, model matrix) from their single geo input but not this one,
      // so a Mapping node patched upstream of any of them had its space/
      // translate/rotate/scale silently dropped before Render 3D ever saw it.
      if (getenv("INFINITE_MAPPINGSWEEPTEST") != nullptr && frameId == 6)
      {
         struct MappingProbeSource : public IGeometrySource
         {
            IGeometrySource* wrapped = nullptr;
            MappingTransform mapping;
            const Mesh& GetMesh() override { return wrapped->GetMesh(); }
            unsigned long long MeshRevision() override { return wrapped->MeshRevision(); }
            Mat4 GetModelMatrix() const override { return wrapped->GetModelMatrix(); }
            Material GetMaterial() const override { return wrapped->GetMaterial(); }
            unsigned int GetSurfaceTexture() override { return wrapped->GetSurfaceTexture(); }
            MappingTransform GetMappingTransform() const override { return mapping; }
         };

         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube
         probeMesh.detail = 4;
         MappingProbeSource probe;
         probe.wrapped = &probeMesh;
         // Non-identity and non-uniform on every field, so a node that forwards
         // only part of MappingTransform (say space but not scale) still fails.
         probe.mapping.space = kMapSpaceGenerated;
         probe.mapping.translate[0] = 1.5f; probe.mapping.translate[1] = -2.5f; probe.mapping.translate[2] = 0.75f;
         probe.mapping.rotate[0] = 0.3f; probe.mapping.rotate[1] = 0.6f; probe.mapping.rotate[2] = 0.9f;
         probe.mapping.scale[0] = 2.0f; probe.mapping.scale[1] = 3.0f; probe.mapping.scale[2] = 4.0f;

         int frame = 21000;
         auto cook = [&](IGeometrySource* g) {
            if (auto* n = dynamic_cast<INode*>(g)) n->CookIfNeeded(frame);
            frame++;
         };
         auto matches = [&](const MappingTransform& a, const MappingTransform& b) {
            if (a.space != b.space)
               return false;
            for (int i = 0; i < 3; i++)
            {
               if (std::fabs(a.translate[i] - b.translate[i]) > 1e-4f) return false;
               if (std::fabs(a.rotate[i] - b.rotate[i]) > 1e-4f) return false;
               if (std::fabs(a.scale[i] - b.scale[i]) > 1e-4f) return false;
            }
            return true;
         };

         struct Result { std::string name; bool ok; };
         std::vector<Result> results;
         auto checkForwarding = [&](const char* name, IGeometrySource* node)
         {
            cook(node);
            results.push_back({ name, matches(node->GetMappingTransform(), probe.mapping) });
         };

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &probe;
         checkForwarding("GeometryOpNode", &opNode);

         DisplacementNode dispNode; dispNode.input = &probe;
         checkForwarding("DisplacementNode", &dispNode);

         MeshResynthNode resynthNode; resynthNode.input = &probe;
         checkForwarding("MeshResynthNode", &resynthNode);

         MeshToPointsNode m2pNode; m2pNode.input = &probe; m2pNode.mode = 0;
         checkForwarding("MeshToPointsNode", &m2pNode);

         Null3DNode nullNode; nullNode.input = &probe;
         checkForwarding("Null3DNode", &nullNode);

         MaterialNode matNode; matNode.input = &probe;
         checkForwarding("MaterialNode", &matNode);

         JoinGeometryNode joinNode; joinNode.mode = JoinGeometryNode::kMerge; joinNode.inputs[0] = &probe;
         checkForwarding("JoinGeometryNode", &joinNode);

         GeometryNode probe2;
         probe2.shape = 2; // sphere
         probe2.detail = 4;
         WrapNode wrapNode; wrapNode.sourceInput = &probe; wrapNode.targetInput = &probe2; wrapNode.blend = 0.0f;
         checkForwarding("WrapNode", &wrapNode);

         ClothNode clothNode;
         clothNode.input = &probe;
         clothNode.pinMode = ClothNode::kPinNone;
         clothNode.gravityX = clothNode.gravityY = clothNode.gravityZ = 0.0f;
         clothNode.windX = clothNode.windY = clothNode.windZ = 0.0f;
         checkForwarding("ClothNode", &clothNode);

         Switcher3DNode sw3Node;
         sw3Node.inputs[0] = &probe;
         sw3Node.manual = true;
         sw3Node.manualSlot = 0;
         checkForwarding("Switcher3DNode", &sw3Node);

         // MappingNode itself is excluded on purpose: it *sets* the mapping
         // transform from its own params rather than forwarding one, so it is
         // not a passthrough case this check applies to. InstanceOnPointsNode
         // and PathNode are excluded for the same reason TRANSFORMSWEEPTEST
         // gives them their own variant - neither reduces to a plain
         // GetMappingTransform() a caller would read.
         bool allOk = true;
         for (const Result& r : results)
         {
            printf("  [%s] %-24s\n", r.ok ? "pass" : "FAIL", r.name.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "MAPPING SWEEP OK" : "MAPPING SWEEP FAIL");
      }

      // A different bug class from the two sweeps above: not a dropped
      // side-channel, but a revision/generation stamp that bumps when nothing
      // actually changed. Found via a real bug: DisplacementNode bumped
      // mTexGeneration on every single cook while a texture was connected,
      // even when the texture's pixels were identical to last frame, which
      // made MeshRevision() change every frame and forced ClothNode
      // downstream to treat every frame as a topology change - the cloth sim
      // reset to rest pose continuously instead of ever draping.
      //
      // The check: cook the same node twice in a row with nothing about its
      // inputs changed, and assert MeshRevision() (or the equivalent stamp)
      // did not move between the two cooks. Any node with a texture input
      // gets a *connected, static* texture for the same reason the Displace
      // bug only showed up with one patched in - the bug is invisible with no
      // texture connected at all.
      if (getenv("INFINITE_REVISIONSWEEPTEST") != nullptr && frameId == 6)
      {
         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube
         probeMesh.detail = 4;

         int frame = 22000;

         struct Result { std::string name; bool ok; };
         std::vector<Result> results;

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &probeMesh;
         opNode.CookIfNeeded(frame++);
         const unsigned long long opFirst = opNode.MeshRevision();
         opNode.CookIfNeeded(frame++);
         results.push_back({ "GeometryOpNode", opFirst == opNode.MeshRevision() });

         // The case that actually caught the bug: Displacement with a real,
         // static (speed=0, so no transport-time animation) texture connected.
         NoiseNode noise;
         noise.speed = 0.0f;
         noise.width = 64.0f; noise.height = 64.0f;
         noise.CookIfNeeded(frame++);

         DisplacementNode dispNode;
         dispNode.input = &probeMesh;
         dispNode.TextureInput().Connect(&noise);
         dispNode.CookIfNeeded(frame++);
         const unsigned long long dispFirst = dispNode.MeshRevision();
         dispNode.CookIfNeeded(frame++);
         const unsigned long long dispSecond = dispNode.MeshRevision();
         dispNode.CookIfNeeded(frame++);
         const unsigned long long dispThird = dispNode.MeshRevision();
         results.push_back({ "DisplacementNode(static texture)", dispFirst == dispSecond && dispSecond == dispThird });

         MeshResynthNode resynthNode;
         resynthNode.input = &probeMesh;
         resynthNode.CookIfNeeded(frame++);
         const unsigned long long resynthFirst = resynthNode.MeshRevision();
         resynthNode.CookIfNeeded(frame++);
         results.push_back({ "MeshResynthNode", resynthFirst == resynthNode.MeshRevision() });

         // Cloth's stamp is its own mMesh revision, bumped by Step() every
         // physics tick even while draping correctly, so it is not expected to
         // stay put between two cooks - what actually matters for Cloth is
         // covered by the topology-vs-position distinction directly, not a
         // stable-revision check. It is included with the *input's* revision
         // instead: confirms a static input doesn't force a rebuild by proxy.
         ClothNode clothNode;
         clothNode.input = &probeMesh;
         clothNode.pinMode = ClothNode::kPinNone;
         clothNode.gravityX = clothNode.gravityY = clothNode.gravityZ = 0.0f;
         clothNode.windX = clothNode.windY = clothNode.windZ = 0.0f;
         clothNode.CookIfNeeded(frame++);
         const size_t clothConstraintsFirst = clothNode.ConstraintCount();
         clothNode.CookIfNeeded(frame++);
         clothNode.CookIfNeeded(frame++);
         results.push_back({ "ClothNode(constraint count stable)", clothConstraintsFirst == clothNode.ConstraintCount() && clothConstraintsFirst > 0 });

         // Switcher3DNode's own bug class to guard against: bumping its
         // revision on every cook just because it has a live clock, rather
         // than only when the active slot (or that slot's own mesh) actually
         // changes. Pinned via manual/manualSlot so the clock plays no part
         // in either half of this check.
         GeometryNode probeMeshB;
         probeMeshB.shape = 2; // sphere: distinct from probeMesh's cube
         probeMeshB.detail = 4;
         Switcher3DNode sw3Static;
         sw3Static.inputs[0] = &probeMesh;
         sw3Static.inputs[1] = &probeMeshB;
         sw3Static.manual = true;
         sw3Static.manualSlot = 0;
         sw3Static.CookIfNeeded(frame++);
         const unsigned long long sw3First = sw3Static.MeshRevision();
         sw3Static.CookIfNeeded(frame++);
         sw3Static.CookIfNeeded(frame++);
         results.push_back({ "Switcher3DNode(static slot stable)", sw3First == sw3Static.MeshRevision() });

         // The other half of the same invariant: a real switch must bump the
         // revision, so a downstream cache actually re-cooks when the input
         // it's reading from changes underneath it.
         const unsigned long long sw3BeforeSwitch = sw3Static.MeshRevision();
         sw3Static.manualSlot = 1;
         sw3Static.CookIfNeeded(frame++);
         results.push_back({ "Switcher3DNode(switch bumps revision)", sw3Static.MeshRevision() != sw3BeforeSwitch });

         // The actual bug-report scenario, not covered by the manual/
         // manualSlot checks above: manual=false, letting Transport's clock
         // itself drive the switch across repeated idle cooks (no param
         // touched between them, the same as a user just sitting there).
         // Confirms both halves of the idle path - a bump when the clock
         // crosses an interval boundary, and no bump on an idle cook that
         // doesn't cross one - since a switcher that never advances live and
         // one that free-runs every frame would both slip past the
         // manual-only checks above.
         {
            const bool savedPlaying = Transport::Instance().IsPlaying();
            const float savedBpm = Transport::Instance().Tempo();
            Transport::Instance().SetPlaying(true);
            Transport::Instance().SetTempo(120.0f);
            Transport::Instance().Rewind();

            Switcher3DNode sw3Clock;
            sw3Clock.inputs[0] = &probeMesh;
            sw3Clock.inputs[1] = &probeMeshB;
            sw3Clock.manual = false;
            sw3Clock.unit = 1; // seconds
            sw3Clock.interval = 0.05f;

            Transport::Instance().Tick(0.01f);
            sw3Clock.CookIfNeeded(frame++);
            const unsigned long long clockFirst = sw3Clock.MeshRevision();
            const int slotFirst = sw3Clock.ActiveSlot();

            // Several idle cooks within the same interval bucket: no clock
            // advance worth crossing a boundary, so the revision must hold.
            Transport::Instance().Tick(0.001f);
            sw3Clock.CookIfNeeded(frame++);
            Transport::Instance().Tick(0.001f);
            sw3Clock.CookIfNeeded(frame++);
            results.push_back({ "Switcher3DNode(clock idle, no boundary crossed -> stable)",
                                 sw3Clock.MeshRevision() == clockFirst });

            // Now tick past several interval boundaries with nothing else
            // touched, exactly like the app sitting idle while the clock
            // free-runs - the revision and active slot must both move.
            bool sawBump = false;
            bool sawSlotChange = false;
            for (int i = 0; i < 20; i++)
            {
               Transport::Instance().Tick(0.02f);
               sw3Clock.CookIfNeeded(frame++);
               if (sw3Clock.MeshRevision() != clockFirst)
                  sawBump = true;
               if (sw3Clock.ActiveSlot() != slotFirst)
                  sawSlotChange = true;
            }
            results.push_back({ "Switcher3DNode(clock idle, boundary crossed -> revision bumps)", sawBump });
            results.push_back({ "Switcher3DNode(clock idle, boundary crossed -> active slot moves)", sawSlotChange });

            Transport::Instance().SetTempo(savedBpm);
            Transport::Instance().SetPlaying(savedPlaying);
            Transport::Instance().Rewind();
         }

         bool allOk = true;
         for (const Result& r : results)
         {
            printf("  [%s] %-32s\n", r.ok ? "pass" : "FAIL", r.name.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "REVISION SWEEP OK" : "REVISION SWEEP FAIL");
      }

      if (getenv("INFINITE_FIXTEST") != nullptr && frameId == 6)
      {
         auto* r0 = static_cast<RandomNode*>(gNodes[0].node.get());
         auto* r1 = static_cast<RandomNode*>(gNodes[1].node.get());
         auto* r2 = static_cast<RandomNode*>(gNodes[2].node.get());

         // Three independently spawned Random nodes must not agree. Sampled
         // across several steps, since any two can coincide on a single one.
         int identical01 = 0, identical02 = 0;
         for (int step = 0; step < 32; step++)
         {
            r0->rateBeats = r1->rateBeats = r2->rateBeats = 0.25f;
            Transport::Instance().Tick(0.12f);
            const float v0 = r0->Value01();
            const float v1 = r1->Value01();
            const float v2 = r2->Value01();
            if (std::fabs(v0 - v1) < 1e-6f) identical01++;
            if (std::fabs(v0 - v2) < 1e-6f) identical02++;
         }
         printf("random: %d/32 samples identical between node 0 and 1, %d/32 between 0 and 2\n",
                identical01, identical02);
         printf("seeds: %.1f %.1f %.1f\n", r0->seed, r1->seed, r2->seed);
         const bool independent = identical01 < 4 && identical02 < 4;
         printf("%s\n", independent ? "RANDOM INDEPENDENT OK"
                                    : "SUSPECT - Random nodes still correlated");

         // Same seed must still reproduce the same sequence exactly.
         r1->seed = r0->seed;
         const bool reproducible = std::fabs(r0->Value01() - r1->Value01()) < 1e-6f;
         printf("same seed reproduces: %d\n", (int)reproducible);

         auto* a = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* b = static_cast<GeometryNode*>(gNodes[4].node.get());
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[6].node.get());

         const size_t expected = a->GetMesh().indices.size() / 3 + b->GetMesh().indices.size() / 3;
         // The merged mesh must span both parts' placements, or the transforms
         // were dropped and everything collapsed onto the origin.
         float lo = 1e30f, hi = -1e30f;
         for (const Vertex& v : join->GetMesh().vertices)
         {
            lo = std::min(lo, v.px);
            hi = std::max(hi, v.px);
         }
         printf("join: %zu tris (expected %zu), x span %.2f..%.2f, %d inputs\n",
                join->TriangleCount(), expected, lo, hi, join->ConnectedCount());
         const bool merged = join->TriangleCount() == expected && lo < -0.5f && hi > 1.0f;
         printf("%s\n", (merged && render->LastTriangleCount() > 0)
                           ? "JOIN GEOMETRY OK" : "SUSPECT");
      }

      if (getenv("INFINITE_CLOTHTEST") != nullptr)
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* cloth = static_cast<ClothNode*>(gNodes[1].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[2].node.get());

         // Longest edge relative to its rest length. A position-based solver
         // should hold this near 1; a force-based one at this stiffness would
         // be visibly stretched, and an unstable one runs away to infinity.
         auto maxStretch = [&]() -> float
         {
            const Mesh& m = cloth->GetMesh();
            const Mesh& rest = geo->GetMesh();
            if (m.vertices.size() != rest.vertices.size())
               return -1.0f;
            float worst = 0.0f;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            {
               for (int e = 0; e < 3; e++)
               {
                  const unsigned int a = m.indices[t + e];
                  const unsigned int b = m.indices[t + (e + 1) % 3];
                  auto len = [](const Vertex& p, const Vertex& q) {
                     const float dx = p.px-q.px, dy = p.py-q.py, dz = p.pz-q.pz;
                     return std::sqrt(dx*dx + dy*dy + dz*dz);
                  };
                  const float restLen = len(rest.vertices[a], rest.vertices[b]);
                  if (restLen < 1e-5f)
                     continue;
                  worst = std::max(worst, len(m.vertices[a], m.vertices[b]) / restLen);
               }
            }
            return worst;
         };

         static float sTopY = 0.0f;
         if (frameId == 3)
         {
            printf("cloth: %zu tris, %zu constraints\n",
                   cloth->TriangleCount(), cloth->ConstraintCount());
            float hi = -1e30f;
            for (const Vertex& v : cloth->GetMesh().vertices)
               hi = std::max(hi, v.py);
            sTopY = hi;
         }
         if (frameId == 60)
         {
            const Mesh& m = cloth->GetMesh();
            float lo = 1e30f, hi = -1e30f;
            bool finite = true;
            for (const Vertex& v : m.vertices)
            {
               if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz))
                  finite = false;
               lo = std::min(lo, v.py);
               hi = std::max(hi, v.py);
            }
            const float stretch = maxStretch();
            printf("after 1s: finite=%d  y range %.3f..%.3f (top was %.3f)  max stretch %.3f\n",
                   (int)finite, lo, hi, sTopY, stretch);
            printf("rendered %zu tris\n", render->LastTriangleCount());

            // Pinned top edge must not have fallen, the rest must have, and no
            // edge may be stretched more than a few percent.
            const bool pinnedHeld = std::fabs(hi - sTopY) < 0.05f;
            const bool draped = lo < sTopY - 0.05f;
            const bool stable = finite && stretch > 0.5f && stretch < 1.2f;
            printf("pinned held=%d draped=%d stable=%d\n",
                   (int)pinnedHeld, (int)draped, (int)stable);
            printf("%s\n", (pinnedHeld && draped && stable && render->LastTriangleCount() > 0)
                              ? "CLOTH OK" : "SUSPECT");
            Transport::Instance().Rewind();
         }
         if (frameId == 63)
         {
            const float stretch = maxStretch();
            printf("after rewind: max stretch %.3f  %s\n", stretch,
                   std::fabs(stretch - 1.0f) < 0.01f ? "CLOTH REWIND RESETS OK" : "SUSPECT");
         }
      }

      if (getenv("INFINITE_PARTICLETEST") != nullptr)
      {
         auto* ps = static_cast<ParticleSystemNode*>(gNodes[0].node.get());
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         static size_t sAtPause = 0;
         static float sPausedY = 0.0f;

         if (frameId == 30)
         {
            printf("running: %zu alive, %zu instances, %zu tris rendered\n",
                   ps->AliveCount(), inst->InstanceCount(), render->LastTriangleCount());
            const bool running = ps->AliveCount() > 50 &&
                                 inst->InstanceCount() == ps->AliveCount() &&
                                 render->LastTriangleCount() > 0;
            printf("%s\n", running ? "PARTICLES SIMULATING OK" : "SUSPECT - not simulating");

            // Every particle must be finite: one NaN propagates through the
            // instance transforms and takes the whole render with it.
            bool finite = true;
            for (const Particle& p : ps->GetPoints())
               if (p.alive && (!std::isfinite(p.px) || !std::isfinite(p.py) || !std::isfinite(p.pz)))
                  finite = false;
            printf("all finite: %d\n", (int)finite);

            Transport::Instance().SetPlaying(false);
         }
         if (frameId == 33)
         {
            // Baseline taken a few frames after pausing, not on the same frame:
            // this block runs before the node cooks, so on the pause frame
            // itself there is still one step's worth of already-advanced clock
            // left to consume. Sampling here measures the frozen state.
            sAtPause = ps->AliveCount();
            for (const Particle& p : ps->GetPoints())
               if (p.alive) { sPausedY = p.py; break; }
         }
         if (frameId == 50)
         {
            // Paused means frozen, not merely not-drawn.
            float nowY = 0.0f;
            for (const Particle& p : ps->GetPoints())
               if (p.alive) { nowY = p.py; break; }
            const bool frozen = ps->AliveCount() == sAtPause &&
                                std::fabs(nowY - sPausedY) < 1e-6f;
            printf("after 17 paused frames: %zu alive (was %zu), y %.5f -> %.5f  %s\n",
                   ps->AliveCount(), sAtPause, sPausedY, nowY,
                   frozen ? "PAUSE FREEZES OK" : "SUSPECT - simulating while paused");
            Transport::Instance().SetPlaying(true);
            Transport::Instance().Rewind();
         }
         if (frameId == 52)
         {
            printf("after rewind: %zu alive  %s\n", ps->AliveCount(),
                   ps->AliveCount() < 50 ? "REWIND RESETS OK" : "SUSPECT - state survived rewind");
         }
      }

      if (getenv("INFINITE_AUDIORECTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            printf("audio source loaded: %d (%s)\n", (int)static_cast<AudioFileNode*>(gNodes[2].node.get())->IsLoaded(),
                   static_cast<AudioFileNode*>(gNodes[2].node.get())->Status().c_str());
            const bool started = out->StartRecording("/tmp/infinite_audiorec.mov");
            printf("start: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 62) // ~2 seconds at 30fps
         {
            // The encoder occasionally reports not-ready and a frame is
            // skipped (pre-existing behaviour - the same fixture pattern in
            // INFINITE_RECTEST only ever printed its frame count, never
            // asserted it exactly), so the frame count is captured before
            // stopping rather than assumed, and duration is checked against
            // that real count rather than wall-clock elapsed time.
            const int frames = out->RecordedFrames();
            out->StopRecording();
            printf("recorded %d frames (of up to 60), status: %s\n", frames, out->RecordStatus().c_str());

            const Platform::MovieInfo withAudio = Platform::InspectMovie("/tmp/infinite_audiorec.mov");
            const double expectedDuration = (double)frames / 30.0;
            printf("with-audio movie: video=%d audio=%d duration=%.2fs (expected ~%.2fs)\n",
                   withAudio.hasVideo, withAudio.hasAudio, withAudio.duration, expectedDuration);

            // A control recording with includeAudio off, so the difference is
            // attributable to the checkbox and not to something environmental.
            out->includeAudio = false;
            out->StartRecording("/tmp/infinite_videoonly.mov");
         }
         if (frameId == 122)
         {
            const int frames = out->RecordedFrames();
            out->StopRecording();
            const Platform::MovieInfo videoOnly = Platform::InspectMovie("/tmp/infinite_videoonly.mov");
            printf("video-only movie: video=%d audio=%d duration=%.2fs (%d frames)\n",
                   videoOnly.hasVideo, videoOnly.hasAudio, videoOnly.duration, frames);

            const Platform::MovieInfo withAudio = Platform::InspectMovie("/tmp/infinite_audiorec.mov");
            const bool ok = withAudio.hasVideo && withAudio.hasAudio &&
                            videoOnly.hasVideo && !videoOnly.hasAudio &&
                            withAudio.duration > 0.5;
            printf("%s\n", ok ? "AUDIO RECORDING OK" : "SUSPECT");
         }
      }

      if (getenv("INFINITE_PATCHTEST") != nullptr && frameId == 4)
      {
         const std::string path = "/tmp/infinite_roundtrip.infinite";
         const size_t nodesBefore = gNodes.size();
         const bool saved = SavePatchTo(path);

         // Wiped between save and load, so anything that appears afterwards
         // genuinely came out of the file rather than surviving in memory.
         NewPatch();
         const bool cleared = gNodes.empty();
         const bool loaded = LoadPatchFrom(path);

         printf("saved=%d cleared=%d loaded=%d  nodes %zu -> %zu\n",
                saved, cleared, loaded, nodesBefore, gNodes.size());

         GeometryNode* geo = nullptr;
         GeometryOpNode* smooth = nullptr;
         MaterialNode* mat = nullptr;
         Render3DNode* render = nullptr;
         PathNode* path3d = nullptr;
         OutputNode* out = nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (!geo) geo = dynamic_cast<GeometryNode*>(gn.node.get());
            if (!smooth) smooth = dynamic_cast<GeometryOpNode*>(gn.node.get());
            if (!mat) mat = dynamic_cast<MaterialNode*>(gn.node.get());
            if (!render) render = dynamic_cast<Render3DNode*>(gn.node.get());
            if (!path3d) path3d = dynamic_cast<PathNode*>(gn.node.get());
            if (!out) out = dynamic_cast<OutputNode*>(gn.node.get());
         }

         const bool haveAll = geo && smooth && mat && render && path3d && out;
         bool params = false, wiring = false, mods = false;
         if (haveAll)
         {
            params = geo->shape == 4 && geo->detail == 33 &&
                     std::fabs(geo->posX - 1.25f) < 1e-5f &&
                     std::fabs(geo->color[1] - 0.22f) < 1e-5f &&
                     std::fabs(geo->emission - 2.5f) < 1e-5f &&
                     smooth->iterations == 7 && std::fabs(smooth->amount - 0.66f) < 1e-5f &&
                     std::fabs(mat->metallic - 0.77f) < 1e-5f &&
                     render->samples == 3 && std::fabs(render->exposure - 1.8f) < 1e-5f &&
                     std::fabs(render->width - 512.0f) < 1e-5f &&
                     path3d->shape == PathNode::kHelix &&
                     std::fabs(path3d->turns - 5.0f) < 1e-5f && path3d->pingPong &&
                     out->includeAudio;

            wiring = smooth->input == geo && mat->input == smooth &&
                     render->geometry[0] == mat && render->camera != nullptr &&
                     render->lights[0] != nullptr && out->Input().IsConnected() &&
                     out->audioSource != nullptr && out->audioSource->IsLoaded();

            mods = !Modulation::Instance().Links().empty();
         }

         printf("params=%d wiring=%d modulation=%d\n", params, wiring, mods);
         printf("%s\n", (saved && cleared && loaded && nodesBefore == gNodes.size() &&
                         haveAll && params && wiring && mods)
                           ? "PATCH ROUND TRIP OK" : "SUSPECT");
      }

      if (getenv("INFINITE_MATFRAMETEST") != nullptr && frameId == 4)
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[2].node.get());

         // Material overrides the surface but must leave the mesh untouched,
         // stamp included, or it would defeat the upload cache.
         geo->color[0] = 1.0f; geo->color[1] = 0.0f; geo->color[2] = 0.0f;
         mat->color[0] = 0.0f; mat->color[1] = 0.0f; mat->color[2] = 1.0f;
         mat->metallic = 0.9f;
         const Material through = mat->GetMaterial();
         const bool overrides = through.color[2] > 0.9f && through.color[0] < 0.1f &&
                                through.metallic > 0.8f;
         const bool meshUntouched = &mat->GetMesh() == &geo->GetMesh() &&
                                    mat->MeshRevision() == geo->MeshRevision();
         printf("material overrides=%d, mesh passed through=%d\n", overrides, meshUntouched);

         // The geometry sits at (4,2,0) scaled 2x, well off-centre, so framing
         // has to move the target as well as pull the camera back.
         render->camDistance = 3.0f;
         render->targetX = 0.0f; render->targetY = 0.0f; render->targetZ = 0.0f;
         FrameSceneInView(render);
         const bool centred = std::fabs(render->targetX - 4.0f) < 0.2f &&
                              std::fabs(render->targetY - 2.0f) < 0.2f;
         const bool pulledBack = render->camDistance > 3.0f && render->camDistance < 30.0f;
         printf("frame: target (%.2f, %.2f, %.2f) distance %.2f\n",
                render->targetX, render->targetY, render->targetZ, render->camDistance);
         printf("%s\n", (overrides && meshUntouched && centred && pulledBack)
                           ? "MATERIAL + FRAME OK" : "SUSPECT");
      }

      if (getenv("INFINITE_ENVTEST") != nullptr &&
          (frameId == 4 || frameId == 8 || frameId == 12))
      {
         auto* env = static_cast<EnvironmentNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         // Top-left corner, well away from the centred sphere: pure background.
         const size_t corner = ((size_t)(h - 4) * w + 4) * 4;
         const int bgR = px[corner], bgG = px[corner + 1], bgB = px[corner + 2];

         if (frameId == 4)
         {
            // HDRI patched in, used as background: the loaded sky (warm,
            // R>G>B) should read back, not the flat near-black default bgColor.
            const bool loaded = env->HasImage() && env->GetEnvironmentTexture() != 0;
            const bool sawSky = bgR > 60 && bgR > bgB;
            printf("env loaded=%d corner=(%d,%d,%d)  %s\n", loaded, bgR, bgG, bgB,
                   (loaded && sawSky) ? "HDRI BACKGROUND OK" : "SUSPECT");

            const GLenum err = glGetError();
            printf("gl error after env render: 0x%x  %s\n", err,
                   err == GL_NO_ERROR ? "CLEAN" : "SUSPECT");

            // A near-mirror sphere over a bright sky should read back brighter
            // than a flat grey ambient bake would ever give it - proof the
            // reflection actually sampled the HDRI rather than falling back to
            // the procedural gradient.
            const size_t centre = ((size_t)(h / 2) * w + w / 2) * 4;
            const int sphereR = px[centre], sphereG = px[centre + 1];
            printf("sphere centre=(%d,%d,%d)  %s\n", sphereR, sphereG, px[centre + 2],
                   (sphereR > 40) ? "REFLECTION SAMPLED HDRI OK" : "SUSPECT");

            // Every mip must be finite, not just mip 0. A single Inf/NaN texel
            // anywhere spreads under box-downsampling until the top of the
            // chain is entirely NaN, and the diffuse irradiance term reads
            // exactly that top mip - so a poisoned chain renders geometry
            // solid black or a flat garbage colour while the thumbnail and
            // background quad (both mip 0) still look perfect.
            int badMips = 0;
            glBindTexture(GL_TEXTURE_2D, env->GetEnvironmentTexture());
            for (int level = 0; level <= (int)env->MaxLod(); level++)
            {
               int lw = 0, lh = 0;
               glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &lw);
               glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &lh);
               if (lw <= 0 || lh <= 0) { badMips++; continue; }
               std::vector<float> mip((size_t)lw * lh * 4);
               glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, mip.data());
               for (float v : mip)
                  if (!std::isfinite(v)) { badMips++; break; }
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            printf("env mip chain: %d level(s) non-finite  %s\n", badMips,
                   badMips == 0 ? "MIP CHAIN FINITE OK" : "SUSPECT");

            render->envAsBackground = false; // checked at frame 8
         }
         else if (frameId == 8)
         {
            // Same HDRI still drives lighting, but the background toggle is
            // off: the corner should fall back to the flat bgColor clear.
            const bool flatBg = bgR < 20 && bgG < 20 && bgB < 30;
            printf("background off: corner=(%d,%d,%d)  %s\n", bgR, bgG, bgB,
                   flatBg ? "BACKGROUND TOGGLE OK" : "SUSPECT");
            render->envAsBackground = true;
            render->envInput.Disconnect();
         }
         else if (frameId == 12)
         {
            // Env input fully disconnected: no HDRI texture is bound at all
            // any more, so the background quad is skipped outright and this
            // is back to the original flat bgColor clear - proving the HDRI
            // path is additive rather than a rewrite that left state behind
            // once its source node goes away.
            const bool flatBg = bgR < 20 && bgG < 20 && bgB < 30;
            printf("disconnected: corner=(%d,%d,%d)  %s\n", bgR, bgG, bgB,
                   flatBg ? "DISCONNECT FALLBACK OK" : "SUSPECT");
         }
      }

      if (getenv("INFINITE_PATHOCEANTEST") != nullptr && frameId == 4)
      {
         auto* path = static_cast<PathNode*>(gNodes[0].node.get());
         auto* ocean = static_cast<OceanNode*>(gNodes[1].node.get());
         Transport::Instance().SetPlaying(true);

         // Every path shape must stay in range and actually move over time.
         bool allShapes = true;
         for (int shape = 0; shape < PathNode::kShapeCount; shape++)
         {
            path->shape = shape;
            path->speed = 0.25f;
            float lo[3] = { 2, 2, 2 }, hi[3] = { -2, -2, -2 };
            bool inRange = true, finite = true;
            for (int step = 0; step < 64; step++)
            {
               // Phase is swept directly rather than waiting on the transport,
               // so a whole lap is covered inside one frame.
               path->phase = (float)step / 64.0f;
               path->CookIfNeeded(1000 + shape * 100 + step);
               float p[3];
               path->CurrentPoint(p);
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) finite = false;
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
               for (int o = 0; o < 4; o++)
               {
                  IModulator* m = path->ModulatorOutput(o);
                  const float v = m ? m->Value01() : -1.0f;
                  if (v < 0.0f || v > 1.0f)
                     inRange = false;
               }
            }
            const float travel = std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            const bool ok = finite && inRange && travel > 0.1f;
            allShapes &= ok;
            printf("  path %-10s travel %.2f  outputs in 0..1: %d  %s\n",
                   PathNode::ShapeNames()[shape].c_str(), travel, inRange, ok ? "OK" : "FAIL");
         }
         printf("%s\n", allShapes ? "PATH OK" : "SUSPECT");

         // The ocean must be a real displaced surface, not a flat plane, and it
         // has to change as the transport advances.
         const Mesh& m0 = ocean->GetMesh();
         float lo = 1e30f, hi = -1e30f;
         bool finite = true;
         for (const Vertex& v : m0.vertices)
         {
            if (!std::isfinite(v.py)) { finite = false; continue; }
            lo = std::min(lo, v.py); hi = std::max(hi, v.py);
         }
         const float relief = hi - lo;
         const size_t tris = m0.indices.size() / 3;
         const unsigned long long stampBefore = ocean->MeshRevision();

         Transport::Instance().Tick(2.0f);
         ocean->GetMesh();
         const unsigned long long stampAfter = ocean->MeshRevision();

         printf("ocean: %zu tris, wave relief %.3f, finite=%d, animates=%d\n",
                tris, relief, finite, stampAfter != stampBefore);
         printf("%s\n", (tris > 100 && finite && relief > 0.02f && stampAfter != stampBefore)
                           ? "OCEAN OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 6)
      {
         auto* palette = static_cast<PaletteNode*>(gNodes[1].node.get());
         auto* twin = static_cast<PaletteNode*>(gNodes[3].node.get());
         auto* target = static_cast<RampNode*>(gNodes[2].node.get());

         float sw[3][3];
         for (int i = 0; i < 3; i++)
            palette->GetSwatch(i, sw[i]);
         printf("swatches: ");
         for (int i = 0; i < 3; i++)
            printf("(%.2f %.2f %.2f w=%.2f) ", sw[i][0], sw[i][1], sw[i][2],
                   palette->SwatchWeight(i));
         printf("\n");

         // 1. three genuinely different colours came out, not three shades of one
         float closest = 9.0f;
         for (int i = 0; i < 3; i++)
            for (int j = i + 1; j < 3; j++)
            {
               float d = 0.0f;
               for (int c = 0; c < 3; c++)
                  d += (sw[i][c] - sw[j][c]) * (sw[i][c] - sw[j][c]);
               closest = std::min(closest, std::sqrt(d));
            }
         const bool distinct = closest > 0.15f;

         // 2. the default order really is dark to light
         auto luma = [](const float* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; };
         const bool ordered = luma(sw[0]) <= luma(sw[1]) + 1e-4f &&
                              luma(sw[1]) <= luma(sw[2]) + 1e-4f;

         // 3. same reference and seed give the same palette, or every binding
         //    downstream would drift on its own
         float deterministic = 0.0f;
         for (int i = 0; i < 3; i++)
         {
            float other[3];
            twin->GetSwatch(i, other);
            for (int c = 0; c < 3; c++)
               deterministic = std::max(deterministic, std::fabs(other[c] - sw[i][c]));
         }

         // 4. the extracted colours actually reach a bound swatch
         PaletteBinding& binding = PaletteBinding::Instance();
         for (int i = 0; i < 3; i++)
            binding.Bind(gNodes[2].index, i, gNodes[1].index, i);
         gPaletteTestPending = true;

         printf("distinct=%d (closest %.3f) ordered=%d deterministic drift=%.4f\n",
                distinct, closest, ordered, deterministic);
         gPaletteTestOk = distinct && ordered && deterministic < 1e-5f;
         (void)target;
      }

      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 9)
      {
         auto* palette = static_cast<PaletteNode*>(gNodes[1].node.get());
         auto* target = static_cast<RampNode*>(gNodes[2].node.get());

         bool applied = true;
         for (int i = 0; i < 3; i++)
         {
            float expected[3];
            palette->GetSwatch(i, expected);
            for (int c = 0; c < 3; c++)
               if (std::fabs(target->stopColor[i][c] - expected[c]) > 1e-4f)
                  applied = false;
         }
         printf("bound stops: (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)  applied=%d\n",
                target->stopColor[0][0], target->stopColor[0][1], target->stopColor[0][2],
                target->stopColor[1][0], target->stopColor[1][1], target->stopColor[1][2],
                target->stopColor[2][0], target->stopColor[2][1], target->stopColor[2][2],
                applied);

         // 5. shaping re-grades without re-clustering: saturation 0 must leave
         //    swatches neutral but still ordered by lightness
         palette->saturation = 0.0f;
         palette->CookIfNeeded(frameId + 500);
         float maxChroma = 0.0f;
         for (int i = 0; i < 3; i++)
         {
            float c[3];
            palette->GetSwatch(i, c);
            maxChroma = std::max(maxChroma,
                                 std::max(c[0], std::max(c[1], c[2])) -
                                    std::min(c[0], std::min(c[1], c[2])));
         }
         palette->saturation = 1.0f;
         printf("saturation 0 -> max channel spread %.3f\n", maxChroma);

         // 6. the binding survives a patch round trip
         Patch::Data data = BuildPatchData();
         const bool saved = data.palette.size() == 3;
         PaletteBinding::Instance().UnbindAllFor(gNodes[2].index);
         const bool cleared = PaletteBinding::Instance().Links().empty();
         for (const Patch::PaletteRecord& r : data.palette)
            PaletteBinding::Instance().Bind(r.dstIndex, r.dstColor, r.srcIndex, r.srcSwatch);
         const bool restored = PaletteBinding::Instance().Links().size() == 3;
         printf("bindings: saved=%d cleared=%d restored=%d\n", saved, cleared, restored);

         // 7. the headline path: a photo loaded from disk, not a cabled node.
         //    Two known colours in, the same two colours out.
         const char* shotDir = getenv("TMPDIR");
         const std::string refPath =
            std::string(shotDir ? shotDir : "/tmp") + "/infinite_palette_ref.png";
         const int side = 64;
         std::vector<unsigned char> ref((size_t)side * side * 4, 255);
         for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
            {
               const size_t i = ((size_t)y * side + x) * 4;
               const bool left = x < side / 2;
               ref[i + 0] = left ? 230 : 30;
               ref[i + 1] = left ? 40 : 90;
               ref[i + 2] = left ? 60 : 210;
            }
         stbi_flip_vertically_on_write(0);
         stbi_write_png(refPath.c_str(), side, side, 4, ref.data(), side * 4);

         auto* fromFile = static_cast<PaletteNode*>(gNodes[3].node.get());
         gNodes[3].node->bypassed = false;
         fromFile->Input().Disconnect();
         fromFile->swatchCount = 2;
         const bool loaded = fromFile->Load(refPath);
         fromFile->CookIfNeeded(frameId + 900);
         float a[3], b[3];
         fromFile->GetSwatch(0, a);
         fromFile->GetSwatch(1, b);
         // Sorted dark-to-light, so the blue half comes first.
         const bool fileOk = loaded && b[0] > 0.7f && b[2] < 0.4f &&
                             a[2] > 0.6f && a[0] < 0.4f;
         printf("from file: loaded=%d (%.2f %.2f %.2f) (%.2f %.2f %.2f) %s\n",
                loaded, a[0], a[1], a[2], b[0], b[1], b[2], fileOk ? "OK" : "SUSPECT");

         const bool ok = gPaletteTestOk && applied && maxChroma < 0.02f &&
                         saved && cleared && restored && fileOk;
         printf("%s\n", ok ? "PALETTE OK" : "SUSPECT");
      }

      if (getenv("INFINITE_UTILTEST") != nullptr && (frameId == 4 || frameId == 10))
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* null3d = static_cast<Null3DNode*>(gNodes[1].node.get());
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         auto* null2d = static_cast<NullNode*>(gNodes[5].node.get());
         auto* shape = gNodes[4].node.get();
         auto* out = static_cast<OutputNode*>(gNodes[6].node.get());

         if (frameId == 4)
         {
            // Null 3D must forward, not copy: same mesh, same stamp. A stamp of
            // its own would make Render 3D re-upload every single frame.
            const bool sameMesh = &null3d->GetMesh() == &geo->GetMesh();
            const bool sameStamp = null3d->MeshRevision() == geo->MeshRevision();
            printf("null 3D forwards: mesh=%d stamp=%d (%llu)\n",
                   sameMesh, sameStamp, null3d->MeshRevision());

            // Null 2D must report the upstream texture as its own, with no
            // framebuffer of its own in between.
            const bool sameTexture = null2d->GetOutputTexture() == shape->GetOutputTexture();
            const bool sameSize = null2d->GetOutputWidth() == shape->GetOutputWidth();
            const bool outputFed = out->GetOutputWidth() > 0;
            printf("null 2D passes: tex=%d size=%d, output %dx%d\n",
                   sameTexture, sameSize, out->GetOutputWidth(), out->GetOutputHeight());

            printf("mesh to points: %zu points -> %zu triangles\n",
                   m2p->PointCount(), m2p->TriangleCount());
            const bool sampled = m2p->PointCount() > 10 && m2p->TriangleCount() > 10;

            printf("%s\n", (sameMesh && sameStamp && sameTexture && sameSize &&
                            outputFed && sampled) ? "NULL + MESH TO POINTS OK" : "SUSPECT");
         }
         else
         {
            // Nothing animates here, so a settled frame must upload nothing -
            // proving the pass-through did not defeat the cache.
            printf("steady frame: %zu tris, %zu uploads  %s\n",
                   render->LastTriangleCount(), render->LastUploads(),
                   render->LastUploads() == 0 ? "PASS-THROUGH CACHE OK"
                                              : "SUSPECT - re-uploading through Null");
         }
      }

      if (getenv("INFINITE_TEXT3DTEST") != nullptr && frameId == 4)
      {
         auto* t = static_cast<Text3DNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());

         // A square ring: one anticlockwise outline, one clockwise hole. If the
         // hole is filled in rather than cut, the area comes out as the full
         // square instead of the ring, so area is the thing worth measuring.
         {
            std::vector<MeshOps::Contour2D> c(2);
            c[0].points = { -1,-1,  1,-1,  1,1,  -1,1 };          // CCW outline
            c[1].points = { -0.5f,0.5f, 0.5f,0.5f, 0.5f,-0.5f, -0.5f,-0.5f }; // CW hole
            const Mesh ring = MeshOps::ExtrudeContours(c, 0.2f, 0.0f);

            // Front-facing area only, so the back face and walls do not count.
            double area = 0.0;
            for (size_t i = 0; i + 2 < ring.indices.size(); i += 3)
            {
               const Vertex& a = ring.vertices[ring.indices[i]];
               const Vertex& b = ring.vertices[ring.indices[i + 1]];
               const Vertex& v = ring.vertices[ring.indices[i + 2]];
               if (a.pz < 0.0f || b.pz < 0.0f || v.pz < 0.0f)
                  continue;
               area += std::fabs((b.px - a.px) * (v.py - a.py) -
                                 (v.px - a.px) * (b.py - a.py)) * 0.5;
            }
            // Outer 2x2 = 4, hole 1x1 = 1, so a correctly cut ring is 3.
            printf("ring: %zu tris, front area %.3f (expect 3.0, filled would be 4.0)  %s\n",
                   ring.indices.size() / 3, area,
                   std::fabs(area - 3.0) < 0.05 ? "HOLE CUT OK" : "SUSPECT");
         }

         // Then real glyphs. 'o' has a counter, so it must produce more than one
         // contour; a string of them must stay finite and bounded.
         const char* samples[] = { "o", "Infinite", "AWAY" };
         bool all = true;
         for (const char* sample : samples)
         {
            t->text = sample;
            const Mesh& mesh = t->GetMesh();
            bool finite = true;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]);
               }
            }
            const bool ok = finite && mesh.indices.size() >= 3 && (hi[0] - lo[0]) > 0.05f;
            all &= ok;
            printf("  %-10s %6zu tris  width %.2f height %.2f  %s\n", sample,
                   mesh.indices.size() / 3, hi[0] - lo[0], hi[1] - lo[1], ok ? "OK" : "FAIL");
            printf("     %s\n", t->Status().c_str());
         }

         t->text = "Infinite";
         render->CookIfNeeded(frameId);
         printf("rendered %zu tris\n", render->LastTriangleCount());
         printf("%s\n", (all && render->LastTriangleCount() > 0) ? "TEXT 3D OK" : "SUSPECT");
      }

      if (getenv("INFINITE_MODELTEST") != nullptr && frameId == 4)
      {
         auto* model = static_cast<ModelSourceNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         const char* files = getenv("INFINITE_MODELTEST");

         bool all = true;
         std::string list(files);
         size_t start = 0;
         while (start < list.size())
         {
            const size_t comma = list.find(',', start);
            const std::string path = list.substr(start, comma == std::string::npos
                                                          ? std::string::npos : comma - start);
            start = (comma == std::string::npos) ? list.size() : comma + 1;
            if (path.empty() || path == "1")
               continue;

            const bool loaded = model->Load(path);
            const Mesh& mesh = model->GetMesh();
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            bool finite = true;
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]);
               }
            }
            const float extent = mesh.vertices.empty() ? 0.0f
               : std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            // Normalisation should land every model on a unit box no matter
            // what scale it was authored at.
            const bool sane = loaded && finite && mesh.indices.size() >= 3 &&
                              std::fabs(extent - 1.0f) < 0.01f;
            printf("  %-28s %5zu tris  extent %.3f  %s\n",
                   path.c_str(), mesh.indices.size() / 3, extent, sane ? "OK" : "FAIL");
            printf("     status: %s\n", model->Status().c_str());
            all &= sane;
         }

         // And it must actually rasterise through the normal render path.
         render->CookIfNeeded(frameId);
         printf("rendered %zu tris in %zu draw calls\n",
                render->LastTriangleCount(), render->LastDrawCalls());
         printf("%s\n", (all && render->LastTriangleCount() > 0) ? "MODEL LOADING OK" : "SUSPECT");

         // A file that is not a model at all must fail cleanly, not crash.
         const bool rejected = !model->Load("/etc/hosts");
         printf("bad file rejected: %d (%s)\n", rejected, model->Status().c_str());
      }

      // Exercises every mesh operator directly, checking each produces a
      // non-degenerate mesh with finite coordinates and a sane bounding box.
      // A silently empty or NaN-riddled result is the failure mode that matters
      // here: the renderer draws nothing and says nothing.
      if (getenv("INFINITE_MESHOPTEST") != nullptr && frameId == 3)
      {
         auto check = [](const char* name, const Mesh& m, size_t minTris) {
            bool finite = true;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (const Vertex& v : m.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
            }
            const size_t tris = m.indices.size() / 3;
            const float extent = (m.vertices.empty() || !finite)
                                    ? 0.0f
                                    : std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            const bool ok = tris >= minTris && finite && extent > 0.001f && extent < 1000.0f;
            printf("  %-12s %7zu tris  extent %6.2f  %s\n", name, tris, extent,
                   ok ? "OK" : (!finite ? "FAIL non-finite" : "FAIL"));
            return ok;
         };

         printf("mesh operators:\n");
         const Mesh sphere = Primitives::Sphere(24, 32);
         const Mesh plane = Primitives::Plane(4);
         bool all = true;
         all &= check("source", sphere, 100);
         all &= check("subdivide", MeshOps::Subdivide(sphere, 1, 1.0f), 4 * (sphere.indices.size() / 3));
         all &= check("smooth", MeshOps::Smooth(sphere, 5, 0.8f), sphere.indices.size() / 3);
         all &= check("mirror", MeshOps::Mirror(sphere, 0, 1.0f, true, true), 2 * (sphere.indices.size() / 3));
         all &= check("screw", MeshOps::Screw(plane, 32, 1.0f, 0.4f, 0.6f, 1), 32);

         // Displace with a synthetic checkerboard buffer, standing in for a
         // readback of a Noise/Voronoi texture - exercises the bilinear
         // sampler and both modes without needing a live GL context here.
         {
            const int texW = 16, texH = 16;
            std::vector<float> tex((size_t)texW * texH * 4);
            for (int y = 0; y < texH; y++)
               for (int x = 0; x < texW; x++)
               {
                  const float v = ((x / 4 + y / 4) % 2 == 0) ? 1.0f : 0.0f;
                  const size_t i = ((size_t)y * texW + x) * 4;
                  tex[i + 0] = v; tex[i + 1] = 1.0f - v; tex[i + 2] = v * 0.5f; tex[i + 3] = 1.0f;
               }
            all &= check("displace scalar",
                         MeshOps::Displace(sphere, tex, texW, texH, 0, 0.3f, 0.5f, false, false),
                         sphere.indices.size() / 3);
            all &= check("displace vector",
                         MeshOps::Displace(sphere, tex, texW, texH, 1, 0.3f, 0.5f, false, false),
                         sphere.indices.size() / 3);

            // A hard-shaded primitive like Cube duplicates each corner once
            // per adjoining face - same position, different normal/UV - so
            // this checks that displacing it does not tear those duplicates
            // apart: every vertex that started at a shared position must
            // still be within epsilon of the others in its group afterward.
            const Mesh cube = Primitives::Cube(1);
            const Mesh displacedCube = MeshOps::Displace(cube, tex, texW, texH, 0, 0.8f, 0.5f, false, false);
            const std::vector<unsigned int> weld = MeshOps::BuildWeldMap(cube);
            std::map<unsigned int, std::array<float, 3>> first;
            float maxGap = 0.0f;
            for (size_t i = 0; i < displacedCube.vertices.size(); i++)
            {
               const unsigned int w = weld[i];
               const Vertex& v = displacedCube.vertices[i];
               auto it = first.find(w);
               if (it == first.end())
                  first[w] = { v.px, v.py, v.pz };
               else
               {
                  const float dx = v.px - it->second[0], dy = v.py - it->second[1], dz = v.pz - it->second[2];
                  maxGap = std::max(maxGap, std::sqrt(dx * dx + dy * dy + dz * dz));
               }
            }
            const bool seamsClosed = maxGap < 0.001f;
            printf("  %-12s max seam gap %.5f  %s\n", "displace cube", maxGap,
                   seamsClosed ? "OK" : "FAIL cracked at seams");
            all &= seamsClosed;
         }
         printf("%s\n", all ? "MESH OPERATORS OK" : "SUSPECT");

         // Taubin's whole point: repeated smoothing must not collapse the mesh.
         const Mesh heavy = MeshOps::Smooth(sphere, 20, 1.0f);
         float r0 = 0.0f, r1 = 0.0f;
         for (const Vertex& v : sphere.vertices)
            r0 = std::max(r0, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
         for (const Vertex& v : heavy.vertices)
            r1 = std::max(r1, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
         printf("smoothing 20x: radius %.4f -> %.4f (%.1f%% retained)\n", r0, r1, 100.0f * r1 / r0);
         printf("%s\n", (r1 > r0 * 0.9f) ? "TAUBIN NO-SHRINK OK"
                                         : "SUSPECT - smoothing is collapsing the mesh");

         // ToPoints weld/dissolve: pin detail=1 so unwelded duplicate corners
         // (24 verts / 30 tri-edges for Cube(1)) don't get mistaken for the
         // welded topology (8 verts / 12 real edges / 12 faces).
         {
            const Mesh cube1 = Primitives::Cube(1);
            const size_t unweldedVerts = MeshOps::ToPoints(cube1, 0, 100000, false).size();
            const size_t weldedVerts = MeshOps::ToPoints(cube1, 0, 100000, true).size();
            const size_t unweldedEdges = MeshOps::ToPoints(cube1, 1, 100000, false, 0.0f).size();
            const size_t weldedEdgesNoFilter = MeshOps::ToPoints(cube1, 1, 100000, true, 0.0f).size();
            const size_t weldedEdgesFiltered = MeshOps::ToPoints(cube1, 1, 100000, true, 1.0f).size();
            const size_t faceCentres = MeshOps::ToPoints(cube1, 2, 100000, true).size();
            const bool toPointsOk = unweldedVerts == 24 && weldedVerts == 8 &&
                                     unweldedEdges == 30 && weldedEdgesNoFilter == 18 &&
                                     weldedEdgesFiltered == 12 && faceCentres == 12;
            printf("to points (Cube(1)): verts %zu->%zu, edges %zu->%zu->%zu, faces %zu  %s\n",
                   unweldedVerts, weldedVerts, unweldedEdges, weldedEdgesNoFilter, weldedEdgesFiltered,
                   faceCentres, toPointsOk ? "OK" : "FAIL");
         }
      }

      // Frame limiter check: run uncapped, then at 30fps, and compare. Vsync is
      // off for this so the display refresh is not what is being measured.
      if (getenv("INFINITE_FPSTEST") != nullptr)
      {
         static double sUncapped = 0.0;
         if (frameId == 2) { gVsync = false; glfwSwapInterval(0); }
         if (frameId == 30) { sUncapped = gLastFrameMs; gTargetFps = 30; }
         if (frameId == 60)
         {
            printf("uncapped %.2f ms/frame -> capped %.2f ms/frame (target 30fps = 33.3 ms)\n",
                   sUncapped, gLastFrameMs);
            printf("%s\n", (gLastFrameMs > 30.0 && gLastFrameMs < 37.0)
                              ? "FRAME LIMITER OK" : "SUSPECT - limiter missed its budget");
         }
      }

      if (getenv("INFINITE_GEOTEST") != nullptr && (frameId == 4 || frameId == 10))
      {
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         auto* op = static_cast<GeometryOpNode*>(gNodes[3].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());
         printf("frame %d: instances=%zu  arrayTris=%zu  rendered=%zu tris in %zu draw calls, %zu uploads, %.2f ms/frame\n",
                frameId, inst->InstanceCount(), op->TriangleCount(),
                r->LastTriangleCount(), r->LastDrawCalls(), r->LastUploads(), gLastFrameMs);
         if (frameId == 4)
            printf("%s\n", (inst->InstanceCount() > 100 && op->TriangleCount() > 100 &&
                             r->LastDrawCalls() <= 2) ? "INSTANCING + OPS OK" : "SUSPECT");
         else
            // Nothing in this fixture animates, so a steady frame must re-upload
            // nothing at all; any upload here means the mesh stamps are churning.
            printf("%s\n", r->LastUploads() == 0 ? "MESH UPLOAD CACHING OK"
                                                 : "SUSPECT - re-uploading a static mesh");
      }

      if (getenv("INFINITE_DISPLACETEST") != nullptr && frameId == 6)
      {
         auto* sub = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* disp = static_cast<DisplacementNode*>(gNodes[2].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());

         const Mesh& before = sub->GetMesh();
         const Mesh& after = disp->GetMesh();

         auto bounds = [](const Mesh& m, float lo[3], float hi[3]) {
            lo[0] = lo[1] = lo[2] = 1e30f;
            hi[0] = hi[1] = hi[2] = -1e30f;
            bool finite = true;
            for (const Vertex& v : m.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
            }
            return finite;
         };

         float loB[3], hiB[3], loA[3], hiA[3];
         const bool finiteBefore = bounds(before, loB, hiB);
         const bool finiteAfter = bounds(after, loA, hiA);
         const float extentBefore = hiB[0] - loB[0];
         const float extentAfter = hiA[0] - loA[0];

         // Same triangle/vertex count - Displace moves points, it does not
         // remesh - but a different radius, since a unit sphere pushed by a
         // Noise texture must not still be a unit sphere.
         const bool sameTopology = before.indices.size() == after.indices.size() &&
                                   before.vertices.size() == after.vertices.size();
         const bool actuallyMoved = std::fabs(extentAfter - extentBefore) > 0.01f;

         r->CookIfNeeded(frameId);
         printf("before extent %.3f  after extent %.3f  tris=%zu  rendered=%zu tris\n",
                extentBefore, extentAfter, disp->TriangleCount(), r->LastTriangleCount());
         printf("%s\n", (finiteBefore && finiteAfter && sameTopology && actuallyMoved &&
                         r->LastTriangleCount() > 0)
                           ? "DISPLACEMENT OK" : "SUSPECT");
      }

      // NodeViewport (per-node mini viewport) exercised directly rather than
      // through the ImGui toggle/icon plumbing: this is the part that
      // actually uploads a mesh, renders it and draws the selection overlay,
      // so it is what is worth an automated check. gNodes[2] is the Select
      // node (has a face mask), gNodes[4] is the plain untouched cube (none).
      if (getenv("INFINITE_MINIVIEWPORTTEST") != nullptr && frameId == 10)
      {
         auto* select = dynamic_cast<IGeometrySource*>(gNodes[2].node.get());
         auto* plain = dynamic_cast<IGeometrySource*>(gNodes[4].node.get());

         static NodeViewport selectViewport;
         static NodeViewport plainViewport;
         const int vw = 256, vh = 256;
         const unsigned int selectTex = selectViewport.Render(select, vw, vh, true);
         const unsigned int plainTex = plainViewport.Render(plain, vw, vh, true);

         auto countTinted = [&](unsigned int tex) -> size_t {
            if (tex == 0)
               return 0;
            std::vector<unsigned char> px((size_t)vw * vh * 4);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, vw, vh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            size_t tinted = 0;
            for (size_t i = 0; i < px.size(); i += 4)
               if (px[i] > 90 && px[i] > px[i + 2] + 40)
                  tinted++;
            return tinted;
         };

         const size_t selectTinted = countTinted(selectTex);
         const size_t plainTinted = countTinted(plainTex);

         // A pos/rot/scale slider moves GetModelMatrix() without rebuilding
         // the mesh at all, so MeshRevision() alone would miss it - the exact
         // bug where the viewport froze on a live torus's transform sliders.
         // Confirmed here by moving the plain cube far off-frame with no
         // mesh rebuild and checking the render actually follows: mostly
         // background where it used to be mostly cube.
         auto countMesh = [&](unsigned int tex) -> size_t {
            if (tex == 0)
               return 0;
            std::vector<unsigned char> px((size_t)vw * vh * 4);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, vw, vh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            // Clear colour is (18,18,23)ish; anything meaningfully brighter is
            // the lit mesh rather than background.
            size_t mesh = 0;
            for (size_t i = 0; i < px.size(); i += 4)
               if (px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 45)
                  mesh++;
            return mesh;
         };
         const size_t plainMeshBefore = countMesh(plainTex);

         auto* plainGeo = static_cast<GeometryNode*>(gNodes[4].node.get());
         plainGeo->posX += 20.0f; // far outside any reasonable auto-framing
         const unsigned int plainTexAfter = plainViewport.Render(plain, vw, vh, false);
         const size_t plainMeshAfter = countMesh(plainTexAfter);

         const bool transformTracked = plainMeshBefore > 500 && plainMeshAfter < 50;
         const bool ok = selectTex != 0 && plainTex != 0 && selectTinted > 50 && plainTinted == 0 &&
                        transformTracked;
         printf("mini viewport: select tex=%u tinted=%zu, plain tex=%u tinted=%zu\n",
                selectTex, selectTinted, plainTex, plainTinted);
         printf("transform tracking: plain mesh px before move=%zu after move=%zu\n",
                plainMeshBefore, plainMeshAfter);
         printf("%s\n", ok ? "MINI VIEWPORT OK" : "SUSPECT");
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      // Points-render-as-points, phase 1 - see the INFINITE_PHASE1TEST spawn
      // block above for the scene. Checked at two points in time so the
      // Particle System check can prove the render keeps advancing rather
      // than freezing on its first frame (the exact bug a missing PointRevision()
      // in the scene cache signature would cause).
      static unsigned long long sPhase1FirstTextureRev = 0;
      static unsigned long long sPhase1FirstParticleRev = 0;
      if (getenv("INFINITE_PHASE1TEST") != nullptr && (frameId == 6 || frameId == 30))
      {
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[1].node.get());
         auto* particles = static_cast<ParticleSystemNode*>(gNodes[2].node.get());
         auto* i2p = static_cast<ImageToPointsNode*>(gNodes[4].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[5].node.get());

         const unsigned long long textureRev = r->TextureRevision();
         const unsigned long long particleRev = particles->PointRevision();

         if (frameId == 6)
         {
            sPhase1FirstTextureRev = textureRev;
            sPhase1FirstParticleRev = particleRev;

            const bool cloudsWired = r->clouds[0] == static_cast<IPointCloudSource*>(m2p) &&
                                     r->clouds[1] == static_cast<IPointCloudSource*>(particles) &&
                                     r->clouds[2] == static_cast<IPointCloudSource*>(i2p) &&
                                     r->geometry[1] == nullptr; // cloud-only slot stays ungeometried
            printf("phase1 clouds wired: slot0=%d slot1(cloud-only, no geometry)=%d slot2=%d  %s\n",
                   (int)(r->clouds[0] != nullptr), (int)(r->clouds[1] != nullptr && r->geometry[1] == nullptr),
                   (int)(r->clouds[2] != nullptr), cloudsWired ? "OK" : "FAIL");

            const size_t pointCount = m2p->GetPoints().size();
            printf("phase1 mesh to points sprites: %zu points, drawcalls=%zu triangles=%zu  %s\n",
                   pointCount, r->LastDrawCalls(), r->LastTriangleCount(),
                   (pointCount > 0 && r->LastDrawCalls() >= 3 && r->LastTriangleCount() > 0)
                      ? "OK" : "FAIL");

            printf("phase1 image to points: %zu points  %s\n", i2p->PointCount(),
                   i2p->PointCount() > 0 ? "OK" : "FAIL");
         }
         else // frameId == 30
         {
            const bool particleAdvanced = particleRev != sPhase1FirstParticleRev;
            const bool renderAdvanced = textureRev != sPhase1FirstTextureRev;
            printf("phase1 particle system animates through render: particleRev %llu->%llu, "
                   "renderRev %llu->%llu  %s\n",
                   sPhase1FirstParticleRev, particleRev, sPhase1FirstTextureRev, textureRev,
                   (particleAdvanced && renderAdvanced) ? "OK" : "FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // antialiased silhouette from ordinary shading gradients; differencing
      // two renders of an identical scene can, because only the edges move.
      if (getenv("INFINITE_3DTEST") != nullptr &&
          (frameId == 4 || frameId == 8 || frameId == 12 || frameId == 16 ||
           frameId == 20 || frameId == 24 || frameId == 28))
      {
         auto* r = static_cast<Render3DNode*>(gNodes[2].node.get());
         const int w = r->GetOutputWidth(), h = r->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         // Count coverage rather than sampling points: a torus knot has holes,
         // and an earlier point-sample test reported "nothing drawn" for a
         // render that was in fact perfectly correct.
         size_t lit = 0;
         for (size_t i = 0; i < px.size(); i += 4)
            if (px[i] + px[i + 1] + px[i + 2] > 60)
               lit++;
         const double coverage = (double)lit / (double)(w * h);
         printf("render %dx%d samples=%d coverage=%.1f%%  %s\n", w, h, r->ActiveSamples(),
                coverage * 100.0,
                (coverage > 0.03 && coverage < 0.95) ? "GEOMETRY RASTERISED OK" : "SUSPECT");

         // The textured geometry above runs the mipmap/anisotropy path; an
         // unsupported enum or an incomplete mip chain would surface here.
         GLenum err = glGetError();
         printf("gl error after render: 0x%x  %s\n", err,
                err == GL_NO_ERROR ? "CLEAN" : "SUSPECT");

         // Mean luminance, used below to prove the exposure/tonemap uniforms
         // reach the shader rather than being silently optimised away.
         double meanLum = 0.0;
         for (size_t i = 0; i < px.size(); i += 4)
            meanLum += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
         meanLum /= (double)(w * h);

         static std::vector<unsigned char> sAliased;
         static double sMeanAtDefault = 0.0;
         if (frameId == 8)
         {
            sMeanAtDefault = meanLum;
            r->exposure = 2.5f;
            r->tonemap = 0; // none
         }
         else if (frameId == 12)
         {
            printf("mean luminance %.2f -> %.2f after exposure 1.0/ACES -> 2.5/none\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", std::fabs(meanLum - sMeanAtDefault) > 2.0
                              ? "EXPOSURE + TONEMAP UNIFORMS LIVE"
                              : "SUSPECT - shader ignored exposure/tonemap");
            // Ask for a 4000px export at 8x and check the budget clamp steps it
            // down instead of trying to allocate a gigabyte of renderbuffer.
            r->width = 4000.0f; r->height = 4000.0f;
            r->samples = 3; // 8x
         }
         else if (frameId == 16)
         {
            printf("export %dx%d requested 8x -> active %dx\n", w, h, r->ActiveSamples());
            printf("%s\n", (w == 4000 && h == 4000 && r->ActiveSamples() >= 1 &&
                            r->ActiveSamples() <= 4)
                              ? "HIGH-RES EXPORT + MSAA BUDGET OK"
                              : "SUSPECT");
            // Back to a cheap size, then sweep the PBR material knobs.
            r->width = 700.0f; r->height = 700.0f;
            r->exposure = 1.0f; r->tonemap = 1;
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->metallic = 1.0f; g->roughness = 0.05f;
         }
         else if (frameId == 20)
         {
            sMeanAtDefault = meanLum; // polished metal
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->metallic = 0.0f; g->roughness = 1.0f;
         }
         else if (frameId == 24)
         {
            printf("mean luminance: polished metal %.2f -> rough dielectric %.2f\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", std::fabs(meanLum - sMeanAtDefault) > 2.0
                              ? "GGX METALLIC/ROUGHNESS RESPOND"
                              : "SUSPECT - BRDF ignored metallic/roughness");
            sMeanAtDefault = meanLum;
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->emission = 4.0f;
         }
         else if (frameId == 28)
         {
            printf("mean luminance: emission 0 %.2f -> emission 4 %.2f\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", meanLum > sMeanAtDefault + 5.0
                              ? "EMISSION OK" : "SUSPECT - emission had no effect");
         }

         if (frameId == 4)
         {
            sAliased = px;
            r->samples = 2; // index 2 == 4x
         }
         else if (frameId == 8 && sAliased.size() == px.size())
         {
            size_t changed = 0, maxDelta = 0;
            for (size_t i = 0; i < px.size(); i += 4)
            {
               const int d = std::abs((int)px[i] - (int)sAliased[i]);
               if (d > 8) changed++;
               if ((size_t)d > maxDelta) maxDelta = (size_t)d;
            }
            const double pct = 100.0 * (double)changed / (double)(w * h);
            printf("antialias diff: %zu px changed (%.2f%%), max delta %zu\n",
                   changed, pct, maxDelta);
            printf("%s\n", (r->ActiveSamples() > 1 && changed > 1000)
                              ? "MSAA OK" : "SUSPECT - multisampling had no effect");
         }
      }

      if (getenv("INFINITE_SIZETEST") != nullptr)
      {
         if (frameId >= 2 && frameId <= 14)
         {
            ImVec2 sz = ed::GetNodeSize(gNodes[0].NodeId());
            printf("f%-2d nodeSize=(%.1f, %.1f)\n", frameId, sz.x, sz.y);
         }
         if (frameId == 15)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (getenv("INFINITE_DRAGTEST") != nullptr)
      {
         if (frameId == 2)
         {
            gDragTestNodePos = ed::GetNodePosition(gNodes[0].NodeId());
            gDragTestViewAnchor = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            printf("start   node canvas pos = (%.0f, %.0f) viewAnchor=(%.0f,%.0f)\n",
                   gDragTestNodePos.x, gDragTestNodePos.y,
                   gDragTestViewAnchor.x, gDragTestViewAnchor.y);
         }
         if (frameId == 9)
         {
            ImVec2 now = ed::GetNodePosition(gNodes[0].NodeId());
            ImVec2 anchor = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            bool nodeStill = std::fabs(now.x - gDragTestNodePos.x) < 0.5f &&
                             std::fabs(now.y - gDragTestNodePos.y) < 0.5f;
            bool viewMoved = std::fabs(anchor.x - gDragTestViewAnchor.x) > 20.0f ||
                             std::fabs(anchor.y - gDragTestViewAnchor.y) > 20.0f;
            printf("after canvas drag: node pos=(%.0f,%.0f) viewAnchor=(%.0f,%.0f) -> node %s, view %s : %s\n",
                   now.x, now.y, anchor.x, anchor.y,
                   nodeStill ? "still" : "MOVED",
                   viewMoved ? "panned" : "STATIC",
                   (nodeStill && viewMoved) ? "PAN OK" : "BUG");
            // aim at the node's title row: below the pins, above the preview
            ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
            gDragTestNodeScreen = ed::CanvasToScreen(ImVec2(p.x + 60.0f, p.y + 42.0f));
            gDragTestNodePos = p;
         }
         if (frameId == 20)
         {
            ImVec2 now = ed::GetNodePosition(gNodes[0].NodeId());
            printf("after node drag:   node pos = (%.0f, %.0f)  %s\n", now.x, now.y,
                   (std::fabs(now.x - gDragTestNodePos.x) > 20.0f ||
                    std::fabs(now.y - gDragTestNodePos.y) > 20.0f) ? "MOVED OK" : "DID NOT MOVE - BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_INPUTTEST") != nullptr)
      {
         if (frameId == 2)
         {
            auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
            sh->shapeType = 6; // Star, so the paste can be checked for fidelity
            sh->sides = 7;
            ed::SelectNode(gNodes[0].NodeId(), false);
            printf("frame2: selected node0, nodes=%zu\n", gNodes.size());
         }
         if (frameId == 7)
         {
            printf("after paste: nodes=%zu\n", gNodes.size());
            for (GraphNode& gn : gNodes)
            {
               if (auto* sp = dynamic_cast<ShapeNode*>(gn.node.get()))
                  printf("  Shape idx=%d shapeType=%d sides=%d\n", gn.index, sp->shapeType, sp->sides);
            }
            ed::SelectNode(gNodes.back().NodeId(), false);
         }
         if (frameId == 10)
         {
            printf("after delete: nodes=%zu\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      PruneDeadGroups();

      for (GraphNode& gn : gNodes)
      {
         if (gn.needsPosition)
         {
            ed::SetNodePosition(gn.NodeId(), ImVec2(gn.spawnX, gn.spawnY));
            gn.needsPosition = false;
         }

         // Cached here, inside the editor context, for anything that needs a
         // position later in the frame when the context is gone.
         {
            const ImVec2 live = ed::GetNodePosition(gn.NodeId());
            gn.liveX = live.x;
            gn.liveY = live.y;
         }

         if (auto* group = dynamic_cast<GroupNode*>(gn.node.get()))
         {
            DrawGroupNode(gn, group);
            continue;
         }

         // Category tint: same idea as DrawGroupNode's stored colour, but from
         // the static per-category table since categories are a fixed
         // vocabulary, not something a user repicks per node. Blended into the
         // library's own default NodeBg rather than replacing it outright, so
         // a node still reads as "the same kind of card", just tinted.
         const CategoryColors::Color& catColor = CategoryColors::ColorFor(gn.category);
         const float kTintWeight = 0.16f;
         ed::PushStyleColor(ed::StyleColor_NodeBg,
                            ImColor(0.125f * (1.0f - kTintWeight) + catColor.r * kTintWeight,
                                    0.125f * (1.0f - kTintWeight) + catColor.g * kTintWeight,
                                    0.125f * (1.0f - kTintWeight) + catColor.b * kTintWeight,
                                    0.784f));
         ed::PushStyleColor(ed::StyleColor_NodeBorder,
                            ImColor(catColor.r, catColor.g, catColor.b, 0.55f));

         ed::BeginNode(gn.NodeId());
         ImGui::PushID(gn.index);
         const bool dimmed = gn.node->bypassed;
         if (dimmed)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);

         // --- inputs spread along the top edge ---
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            char label[24];
            if (const char* named = gn.node->InputLabel(slot))
               snprintf(label, sizeof(label), "%s", named);
            else if (inputs == 1)
               label[0] = '\0';
            else
               snprintf(label, sizeof(label), "%c", 'A' + slot);
            DrawPin(gn.InputPinId(slot), ed::PinKind::Input, label);
            if (slot + 1 < inputs)
               ImGui::SameLine(0.0f, 12.0f);
         }

         // Group the body so its measured width can right-align the out pin.
         // ed::GetNodeSize() is scaled by the current zoom, so feeding it back
         // into padding inflated the node a little more every frame until it
         // covered the canvas and swallowed every click.
         ImGui::BeginGroup();

         ImGui::TextUnformatted(NodeTitle(gn).c_str());
         // Lightened toward white so the label stays legible at small sizes,
         // the same blend DrawGroupNode uses for its own colour-tinted label.
         ImGui::PushStyleColor(ImGuiCol_Text,
                               ImVec4(catColor.r * 0.6f + 0.4f, catColor.g * 0.6f + 0.4f,
                                      catColor.b * 0.6f + 0.4f, 1.0f));
         ImGui::TextUnformatted(gn.category.c_str());
         ImGui::PopStyleColor();

         // --- preview: image for image nodes, a value meter for modulators ---
         const bool multiOutModulator =
            dynamic_cast<ImageAnalyzeNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioFileNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioAnalyzeNode*>(gn.node.get()) != nullptr;
         IGeometrySource* geoSourceForViewport = dynamic_cast<IGeometrySource*>(gn.node.get());
         if (multiOutModulator)
            ; // these draw their own meters in the params panel
         else if (auto* mod = dynamic_cast<IModulator*>(gn.node.get()))
            DrawModulatorMeter(mod, gn.index);
         else if (gn.showMiniViewport && geoSourceForViewport != nullptr)
            DrawMiniViewport(gn, geoSourceForViewport);
         else if (dynamic_cast<GeometryOpNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<InstanceOnPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ModelSourceNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Text3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MappingNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<OceanNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ParticleSystemNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ClothNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<WrapNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<CurveNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ImageToPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<CameraNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<LightNode*>(gn.node.get()) != nullptr)
         {
            ImVec2 origin = ImGui::GetCursorScreenPos();
            const float h = 52.0f;
            ImGui::Dummy(ImVec2(kPreviewSize, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 br(origin.x + kPreviewSize, origin.y + h);
            dl->AddRectFilled(origin, br, IM_COL32(18, 18, 24, 255), 4.0f);
            dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
            char line[64] = "";
            if (auto* o = dynamic_cast<GeometryOpNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", o->TriangleCount());
            else if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu instances", inst->InstanceCount());
            else if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", model->TriangleCount());
            else if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", t3d->TriangleCount());
            else if (auto* n3d = dynamic_cast<Null3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", n3d->TriangleCount());
            else if (auto* mapn = dynamic_cast<MappingNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", mapn->TriangleCount());
            else if (auto* m2p = dynamic_cast<MeshToPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points", m2p->PointCount());
            else if (auto* oc = dynamic_cast<OceanNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", oc->TriangleCount());
            else if (auto* mat = dynamic_cast<MaterialNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", mat->TriangleCount());
            else if (auto* disp = dynamic_cast<DisplacementNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", disp->TriangleCount());
            else if (auto* ps = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu particles", ps->AliveCount());
            else if (auto* cv = dynamic_cast<CurveNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points, %zu tris", cv->PointCount(), cv->TriangleCount());
            else if (auto* mb = dynamic_cast<MetaBallNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu balls, %zu tris", mb->BallCount(), mb->TriangleCount());
            else if (auto* jn = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%d inputs, %zu tris", jn->ConnectedCount(), jn->TriangleCount());
            else if (auto* wr = dynamic_cast<WrapNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu tris", wr->TriangleCount());
            else if (auto* sw3 = dynamic_cast<Switcher3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "showing input %c", 'A' + sw3->ActiveSlot());
            else if (auto* cl = dynamic_cast<ClothNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu tris, %zu links", cl->TriangleCount(), cl->ConstraintCount());
            else if (auto* mrs = dynamic_cast<MeshResynthNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "gen %d, %zu tris", mrs->Generation(), mrs->TriangleCount());
            else if (auto* i2p = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points", i2p->PointCount());
            else
               snprintf(line, sizeof(line), "scene node");
            dl->AddText(ImVec2(origin.x + 12, origin.y + 10), IM_COL32(200, 206, 226, 255),
                        NodeTitle(gn).c_str());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 28), IM_COL32(130, 136, 156, 255), line);
         }
         else if (dynamic_cast<GeometryNode*>(gn.node.get()) != nullptr)
         {
            // Geometry emits a mesh, not a picture: show what it is instead of
            // an empty preview box.
            auto* geo = static_cast<GeometryNode*>(gn.node.get());
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(kPreviewSize, kPreviewSize * 0.45f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 br(origin.x + kPreviewSize, origin.y + kPreviewSize * 0.45f);
            dl->AddRectFilled(origin, br, IM_COL32(18, 18, 24, 255), 4.0f);
            dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
            const std::string& name = GeometryNode::ShapeNames()[
               std::max(0, std::min(geo->shape, (int)GeometryNode::ShapeNames().size() - 1))];
            dl->AddText(ImVec2(origin.x + 12, origin.y + 14), IM_COL32(200, 206, 226, 255), name.c_str());
            char tris[48];
            snprintf(tris, sizeof(tris), "%zu triangles", geo->TriangleCount());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 34), IM_COL32(130, 136, 156, 255), tris);
            dl->AddText(ImVec2(origin.x + 12, origin.y + 54), IM_COL32(130, 136, 156, 255), "geometry -> Render 3D");
         }
         else if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
            DrawPaintablePreview(draw);
         else if (auto* comment = dynamic_cast<CommentNode*>(gn.node.get()))
            DrawCommentPreview(comment);
         else if (auto* palette = dynamic_cast<PaletteNode*>(gn.node.get()))
            DrawPalettePreview(palette);
         else
            DrawPreview(gn.node.get());

         // --- params (eye) ---
         // The bypass (power) toggle used to live here; removed because it was
         // confusing and unreliable. The underlying bypassed flag, its cable-walk
         // logic, and patch serialization are untouched, so patches saved with a
         // bypassed node still load and dim correctly - there just isn't a UI
         // control to set it anymore.
         const bool isComment = dynamic_cast<CommentNode*>(gn.node.get()) != nullptr;
         if (EyeToggle(gn.showParams))
            gn.showParams = !gn.showParams;
         // Mini viewport toggle, only for nodes that actually have a mesh to
         // show - excludes CameraNode/LightNode, which appear in the stat-box
         // branch above but implement no geometry interface.
         if (geoSourceForViewport != nullptr)
         {
            ImGui::SameLine();
            if (ViewportToggle(gn.showMiniViewport))
               gn.showMiniViewport = !gn.showMiniViewport;
         }
         ImVec2 modTagMin(0.0f, 0.0f), modTagMax(0.0f, 0.0f);
         ImVec2 palTagMin(0.0f, 0.0f), palTagMax(0.0f, 0.0f);
         const bool modTag = !gn.showParams && gn.hasModulatedParams;
         const bool palTag = !gn.showParams && gn.hasPaletteColors;
         if (modTag)
         {
            // make it obvious a collapsed node still has live modulation
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "mod");
            modTagMin = ImGui::GetItemRectMin();
            modTagMax = ImGui::GetItemRectMax();
         }
         if (palTag)
         {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.86f, 0.74f, 1.0f), "pal");
            palTagMin = ImGui::GetItemRectMin();
            palTagMax = ImGui::GetItemRectMax();
         }
         // Only once the whole row is laid out: the stubs move the cursor.
         if (modTag)
            CollapsedBindingPins(gn.index, modTagMin, modTagMax, false);
         if (palTag)
            CollapsedBindingPins(gn.index, palTagMin, palTagMax, true);

         BeginNodeParams(gn.index);
         if (gn.showParams)
         {
            if (auto* n = dynamic_cast<ImageSourceNode*>(gn.node.get()))
               DrawImageSourceParams(n);
            else if (auto* n = dynamic_cast<EnvironmentNode*>(gn.node.get()))
               DrawEnvironmentParams(n);
            else if (auto* n = dynamic_cast<VideoSourceNode*>(gn.node.get()))
               DrawVideoParams(n);
            else if (auto* n = dynamic_cast<FitNode*>(gn.node.get()))
               DrawFitParams(n);
            else if (auto* n = dynamic_cast<LFONode*>(gn.node.get()))
               DrawLFOParams(n);
            else if (auto* n = dynamic_cast<RandomNode*>(gn.node.get()))
               DrawRandomParams(n);
            else if (auto* n = dynamic_cast<PatternNode*>(gn.node.get()))
               DrawPatternParams(n);
            else if (auto* n = dynamic_cast<MathNode*>(gn.node.get()))
               DrawMathParams(n);
            else if (auto* n = dynamic_cast<MacroKnobNode*>(gn.node.get()))
               DrawMacroKnobParams(n);
            else if (auto* n = dynamic_cast<MacroXYNode*>(gn.node.get()))
               DrawMacroXYParams(n);
            else if (auto* n = dynamic_cast<NoiseNode*>(gn.node.get()))
               DrawNoiseParams(n);
            else if (auto* n = dynamic_cast<TextureNode*>(gn.node.get()))
               DrawTextureParams(n);
            else if (auto* n = dynamic_cast<RampNode*>(gn.node.get()))
               DrawRampParams(n);
            else if (auto* n = dynamic_cast<PaletteNode*>(gn.node.get()))
               DrawPaletteParams(n);
            else if (auto* n = dynamic_cast<GeometryNode*>(gn.node.get()))
               DrawGeometryParams(n);
            else if (auto* n = dynamic_cast<ModelSourceNode*>(gn.node.get()))
               DrawModelParams(n);
            else if (auto* n = dynamic_cast<Text3DNode*>(gn.node.get()))
               DrawText3DParams(n);
            else if (auto* n = dynamic_cast<MeshToPointsNode*>(gn.node.get()))
               DrawMeshToPointsParams(n);
            else if (auto* n = dynamic_cast<MeshResynthNode*>(gn.node.get()))
               DrawMeshResynthParams(n);
            else if (auto* n = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
               DrawImageToPointsParams(n);
            else if (auto* n = dynamic_cast<CommentNode*>(gn.node.get()))
               DrawCommentParams(n);
            else if (auto* n = dynamic_cast<PathNode*>(gn.node.get()))
               DrawPathParams(n);
            else if (auto* n = dynamic_cast<ConstantNode*>(gn.node.get()))
            {
               ModSlider("value", &n->value, 0.0f, 1.0f);
            }
            else if (auto* n = dynamic_cast<MaterialNode*>(gn.node.get()))
               DrawMaterialParams(n);
            else if (auto* n = dynamic_cast<MappingNode*>(gn.node.get()))
               DrawMappingParams(n);
            else if (auto* n = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
               DrawParticleSystemParams(n);
            else if (auto* n = dynamic_cast<ClothNode*>(gn.node.get()))
               DrawClothParams(n);
            else if (auto* n = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
               DrawJoinGeometryParams(n);
            else if (auto* n = dynamic_cast<MetaBallNode*>(gn.node.get()))
               DrawMetaBallParams(n);
            else if (auto* n = dynamic_cast<CurveNode*>(gn.node.get()))
               DrawCurveParams(n);
            else if (auto* n = dynamic_cast<OceanNode*>(gn.node.get()))
               DrawOceanParams(n);
            else if (dynamic_cast<NullNode*>(gn.node.get()) != nullptr ||
                     dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr)
               ImGui::TextDisabled("pass-through");
            else if (auto* n = dynamic_cast<GeometryOpNode*>(gn.node.get()))
               DrawGeometryOpParams(n);
            else if (auto* n = dynamic_cast<DisplacementNode*>(gn.node.get()))
               DrawDisplacementParams(n);
            else if (auto* n = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
               DrawInstanceParams(n);
            else if (auto* n = dynamic_cast<WrapNode*>(gn.node.get()))
               DrawWrapParams(n);
            else if (auto* n = dynamic_cast<Switcher3DNode*>(gn.node.get()))
               DrawSwitcher3DParams(n);
            else if (auto* n = dynamic_cast<CameraNode*>(gn.node.get()))
               DrawCameraParams(n);
            else if (auto* n = dynamic_cast<LightNode*>(gn.node.get()))
               DrawLightParams(n);
            else if (auto* n = dynamic_cast<Render3DNode*>(gn.node.get()))
               DrawRender3DParams(n);
            else if (auto* n = dynamic_cast<ImageAnalyzeNode*>(gn.node.get()))
               DrawImageAnalyzeParams(n);
            else if (auto* n = dynamic_cast<AudioFileNode*>(gn.node.get()))
               DrawAudioFileParams(n);
            else if (auto* n = dynamic_cast<AudioAnalyzeNode*>(gn.node.get()))
               DrawAudioAnalyzeParams(n);
            else if (auto* n = dynamic_cast<ResynthNode*>(gn.node.get()))
               DrawResynthParams(n);
            else if (auto* n = dynamic_cast<CurvesNode*>(gn.node.get()))
               DrawCurvesParams(n);
            else if (auto* n = dynamic_cast<ColorRampNode*>(gn.node.get()))
               DrawColorRampParams(n);
            else if (auto* n = dynamic_cast<RemoveBgNode*>(gn.node.get()))
               DrawRemoveBgParams(n);
            else if (auto* n = dynamic_cast<DrawNode*>(gn.node.get()))
               DrawDrawParams(n);
            else if (auto* n = dynamic_cast<FeedbackNode*>(gn.node.get()))
               DrawFeedbackParams(n);
            else if (auto* n = dynamic_cast<TrailsNode*>(gn.node.get()))
               DrawTrailsParams(n);
            else if (auto* n = dynamic_cast<ReactionDiffusionNode*>(gn.node.get()))
               DrawReactionDiffusionParams(n);
            else if (auto* n = dynamic_cast<SwitcherNode*>(gn.node.get()))
               DrawSwitcherParams(n);
            else if (auto* n = dynamic_cast<ShapeNode*>(gn.node.get()))
               DrawShapeParams(n);
            else if (auto* n = dynamic_cast<FormulaNode*>(gn.node.get()))
               DrawFormulaParams(n);
            else if (auto* n = dynamic_cast<TextNode*>(gn.node.get()))
               DrawTextParams(n);
            else if (auto* n = dynamic_cast<LayerStackNode*>(gn.node.get()))
               DrawLayerStackParams(n);
            else if (auto* n = dynamic_cast<BlendNode*>(gn.node.get()))
               DrawBlendParams(n);
            else if (auto* n = dynamic_cast<FilterNode*>(gn.node.get()))
               DrawFilterParams(n);
            else if (auto* n = dynamic_cast<OutputNode*>(gn.node.get()))
            {
               ImGui::SetNextItemWidth(kPreviewSize);
               ImGui::InputText("##png", exportPath, sizeof(exportPath));
               if (ImGui::Button("Export PNG", ImVec2(kPreviewSize, 0)))
                  ExportPng(n, exportPath);

               ImGui::Dummy(ImVec2(0, 4));
               ImGui::SetNextItemWidth(kPreviewSize);
               ImGui::InputText("##mov", recordPath, sizeof(recordPath));
               ImGui::SetNextItemWidth(kParamWidth);
               ImGui::SliderInt("fps", &n->recordFps, 1, 60);

               ImGui::Checkbox("include audio", &n->includeAudio);
               // Only the filename, and only when there is one: which file is
               // being muxed is worth knowing, the instruction to patch one is
               // not, since the pin is right there.
               if (n->includeAudio && n->audioSource != nullptr)
                  ImGui::TextDisabled("from: %s", n->audioSource->FileName().c_str());

               if (n->IsRecording())
               {
                  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
                  if (ImGui::Button("Stop recording", ImVec2(kPreviewSize, 0)))
                     n->StopRecording();
                  ImGui::PopStyleColor();
                  ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "REC  %d frames", n->RecordedFrames());
               }
               else
               {
                  if (ImGui::Button("Record video", ImVec2(kPreviewSize, 0)))
                     n->StartRecording(recordPath);
               }
               if (!n->RecordStatus().empty())
               {
                  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
                  ImGui::TextDisabled("%s", n->RecordStatus().c_str());
                  ImGui::PopTextWrapPos();
               }
            }
         }

         ImGui::EndGroup();
         const float contentW = ImGui::GetItemRectSize().x;

         // --- output dots, bottom-right: cables start here ---
         // A comment is not in the signal graph, and an out pin on one is worse
         // than useless: link validation only asks whether a source is an image
         // node, so a comment would happily patch into any image input and feed
         // it a blank texture. No pin, no way to make that mistake.
         if (dynamic_cast<OutputNode*>(gn.node.get()) == nullptr && !isComment)
         {
            const int outputs = std::max(1, gn.node->OutputCount());
            float itemW = 0.0f;
            for (int o = 0; o < outputs; o++)
               itemW += kPinHit + 4.0f + ImGui::CalcTextSize(gn.node->OutputLabel(o)).x + (o ? 10.0f : 0.0f);
            float pad = std::max(0.0f, contentW - itemW);
            ImGui::Dummy(ImVec2(pad, 1.0f));
            for (int o = 0; o < outputs; o++)
            {
               ImGui::SameLine(0.0f, o == 0 ? 0.0f : 10.0f);
               DrawPin(gn.OutputPinId(o), ed::PinKind::Output, gn.node->OutputLabel(o), true);
            }
         }

         if (dimmed)
            ImGui::PopStyleVar();
         ImGui::PopID();
         ed::EndNode();
         ed::PopStyleColor(2);
      }

      // ---- draw existing links ----
      // Link ids come from this table rather than arithmetic on pin ids, so image
      // cables and modulation cables share one collision-free id space.
      gLinks.clear();
      for (GraphNode& gn : gNodes)
      {
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(gn, slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;

            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == cable->GetSource())
               {
                  gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                                     src.OutputPinId(), gn.InputPinId(slot) });
                  break;
               }
            }
         }
      }
      for (GraphNode& gn : gNodes)
      {
         auto linkFromNode = [&](const void* wanted, int slot)
         {
            if (wanted == nullptr)
               return;
            for (GraphNode& src : gNodes)
            {
               // Compared against each interface separately: with multiple
               // inheritance an IPointCloudSource* and an INode* into the same
               // object are different addresses, so one comparison is not enough.
               const void* asGeo = dynamic_cast<IGeometrySource*>(src.node.get());
               const void* asCloud = dynamic_cast<IPointCloudSource*>(src.node.get());
               if (asGeo == wanted || asCloud == wanted || (const void*)src.node.get() == wanted)
               {
                  gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                                     src.OutputPinId(), gn.InputPinId(slot) });
                  return;
               }
            }
         };

         if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         {
            for (int slot = 0; slot < Render3DNode::kSlots; slot++)
               linkFromNode(render->geometry[slot], slot);
            linkFromNode(render->camera, Render3DNode::kSlots);
            for (int i = 0; i < Render3DNode::kLightSlots; i++)
               linkFromNode(render->lights[i], Render3DNode::kSlots + 1 + i);
         }
         if (auto* geoOp = dynamic_cast<GeometryOpNode*>(gn.node.get()))
            linkFromNode(geoOp->input, 0);
         if (auto* n3d = dynamic_cast<Null3DNode*>(gn.node.get()))
            linkFromNode(n3d->input, 0);
         if (auto* mapn = dynamic_cast<MappingNode*>(gn.node.get()))
            linkFromNode(mapn->input, 0);
         if (auto* m2p = dynamic_cast<MeshToPointsNode*>(gn.node.get()))
            linkFromNode(m2p->input, 0);
         if (auto* mrs = dynamic_cast<MeshResynthNode*>(gn.node.get()))
            linkFromNode(mrs->input, 0);
         if (auto* cloth = dynamic_cast<ClothNode*>(gn.node.get()))
            linkFromNode(cloth->input, 0);
         if (auto* join = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
            for (int i = 0; i < JoinGeometryNode::kSlots; i++)
               linkFromNode(join->inputs[i], i);
         if (auto* meta = dynamic_cast<MetaBallNode*>(gn.node.get()))
            linkFromNode(meta->cloudSource, 0);
         if (auto* path = dynamic_cast<PathNode*>(gn.node.get()))
         {
            linkFromNode(path->curveSource, 0);
            linkFromNode(path->geometrySource, 1);
         }
         if (auto* mat = dynamic_cast<MaterialNode*>(gn.node.get()))
            linkFromNode(mat->input, 0);
         if (auto* disp = dynamic_cast<DisplacementNode*>(gn.node.get()))
            linkFromNode(disp->input, 0);
         if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
         {
            linkFromNode(inst->pointSource, 0);
            linkFromNode(inst->instanceShape, 1);
            linkFromNode(inst->cloudSource, 2);
         }
         if (auto* w = dynamic_cast<WrapNode*>(gn.node.get()))
         {
            linkFromNode(w->sourceInput, 0);
            linkFromNode(w->targetInput, 1);
         }
         if (auto* sw3 = dynamic_cast<Switcher3DNode*>(gn.node.get()))
            for (int i = 0; i < Switcher3DNode::kSlots; i++)
               linkFromNode(sw3->inputs[i], i);
         if (auto* out = dynamic_cast<OutputNode*>(gn.node.get()))
            linkFromNode(out->audioSource, 1);

         auto* audio = dynamic_cast<AudioAnalyzeNode*>(gn.node.get());
         if (audio != nullptr && audio->fileSource != nullptr)
         {
            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == audio->fileSource)
               {
                  gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                                     src.OutputPinId(), gn.InputPinId(0) });
                  break;
               }
            }
         }

         auto* math = dynamic_cast<MathNode*>(gn.node.get());
         if (math == nullptr)
            continue;
         for (int slot = 0; slot < 2; slot++)
         {
            IModulator* wanted = (slot == 0) ? math->inputA : math->inputB;
            if (wanted == nullptr)
               continue;
            bool found = false;
            for (GraphNode& src : gNodes)
            {
               for (int o = 0; o < std::max(1, src.node->OutputCount()) && !found; o++)
               {
                  if (ModulatorForOutput(src.node.get(), o) == wanted)
                  {
                     gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                                        src.OutputPinId(o), gn.InputPinId(slot) });
                     found = true;
                  }
               }
               if (found)
                  break;
            }
         }
      }

      for (const auto& link : Modulation::Instance().Links())
      {
         GraphNode* target = FindNodeByIndex(link.first.first);
         GraphNode* source = FindNodeByIndex(link.second.nodeIndex);
         if (target == nullptr || source == nullptr)
            continue;
         const int paramPin = target->ParamPinId(link.first.second);
         if (gDrawnParamPins.count(paramPin) == 0)
            continue; // no pin declared this frame: emitting the link would kill it
         gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                            source->OutputPinId(link.second.outputIndex), paramPin });
      }

      for (const auto& link : PaletteBinding::Instance().Links())
      {
         GraphNode* target = FindNodeByIndex(link.first.first);
         GraphNode* source = FindNodeByIndex(link.second.nodeIndex);
         if (target == nullptr || source == nullptr)
            continue;
         const int colorPin = target->ColorPinId(link.first.second);
         if (gDrawnColorPins.count(colorPin) == 0)
            continue; // no pin declared this frame: emitting the link would kill it
         gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                            source->OutputPinId(0), colorPin });
      }

      // ---- drop a node on a cable to splice it in ----
      // While a single node is being dragged, find an image cable passing under
      // it. The link is approximated as a straight line between the two nodes'
      // facing edges rather than the bezier actually drawn: close enough to feel
      // right, and it avoids reaching into the editor's internal curve geometry.
      for (const LinkInfo& link : gLinks)
      {
         const bool isMod = GraphNode::IsParamPin(link.dstPin);
         if (isMod)
            ed::Link(link.id, link.srcPin, link.dstPin, ImColor(255, 190, 90), 1.8f);
         else
            ed::Link(link.id, link.srcPin, link.dstPin);
      }

      // ---- handle new connections ----
      if (ed::BeginCreate())
      {
         ed::PinId startPin, endPin;
         if (ed::QueryNewLink(&startPin, &endPin))
         {
            if (startPin && endPin)
            {
               int a = (int)startPin.Get();
               int b = (int)endPin.Get();

               // normalize so `a` is the output side
               if (!GraphNode::IsOutputPin(a))
                  std::swap(a, b);

               GraphNode* srcNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(a));
               GraphNode* dstNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(b));
               const bool differentNodes = GraphNode::NodeIndexFromPin(a) != GraphNode::NodeIndexFromPin(b);
               const bool srcIsModulator = srcNode != nullptr &&
                                           dynamic_cast<IModulator*>(srcNode->node.get()) != nullptr;
               auto* srcPalette = srcNode ? dynamic_cast<IPaletteSource*>(srcNode->node.get()) : nullptr;

               auto* dstMath = dstNode ? dynamic_cast<MathNode*>(dstNode->node.get()) : nullptr;
               auto* dstAudio = dstNode ? dynamic_cast<AudioAnalyzeNode*>(dstNode->node.get()) : nullptr;
               auto* srcAudioFile = srcNode ? dynamic_cast<AudioFileNode*>(srcNode->node.get()) : nullptr;
               auto* dstRender = dstNode ? dynamic_cast<Render3DNode*>(dstNode->node.get()) : nullptr;
               auto* dstGeoOp = dstNode ? dynamic_cast<GeometryOpNode*>(dstNode->node.get()) : nullptr;
               auto* dstInstance = dstNode ? dynamic_cast<InstanceOnPointsNode*>(dstNode->node.get()) : nullptr;
               auto* dstWrap = dstNode ? dynamic_cast<WrapNode*>(dstNode->node.get()) : nullptr;
               // Null 3D, Material and Mesh to Points all take a single
               // geometry input on slot 0 and are otherwise interchangeable
               // here, so they share one branch.
               auto* dstNull3D = dstNode ? dynamic_cast<Null3DNode*>(dstNode->node.get()) : nullptr;
               auto* dstMapping = dstNode ? dynamic_cast<MappingNode*>(dstNode->node.get()) : nullptr;
               auto* dstMaterial = dstNode ? dynamic_cast<MaterialNode*>(dstNode->node.get()) : nullptr;
               auto* dstDisplacement = dstNode ? dynamic_cast<DisplacementNode*>(dstNode->node.get()) : nullptr;
               auto* dstMeshPoints = dstNode ? dynamic_cast<MeshToPointsNode*>(dstNode->node.get()) : nullptr;
               auto* dstMeshResynth = dstNode ? dynamic_cast<MeshResynthNode*>(dstNode->node.get()) : nullptr;
               auto* dstCloth = dstNode ? dynamic_cast<ClothNode*>(dstNode->node.get()) : nullptr;
               auto* dstJoin = dstNode ? dynamic_cast<JoinGeometryNode*>(dstNode->node.get()) : nullptr;
               auto* dstSwitcher3D = dstNode ? dynamic_cast<Switcher3DNode*>(dstNode->node.get()) : nullptr;
               auto* dstMeta = dstNode ? dynamic_cast<MetaBallNode*>(dstNode->node.get()) : nullptr;
               auto* dstPath = dstNode ? dynamic_cast<PathNode*>(dstNode->node.get()) : nullptr;
               auto* srcCurve = srcNode ? dynamic_cast<ICurveSource*>(srcNode->node.get()) : nullptr;
               auto* dstOutput = dstNode ? dynamic_cast<OutputNode*>(dstNode->node.get()) : nullptr;
               auto* srcGeometry = srcNode ? dynamic_cast<IGeometrySource*>(srcNode->node.get()) : nullptr;
               auto* srcCloud = srcNode ? dynamic_cast<IPointCloudSource*>(srcNode->node.get()) : nullptr;
               auto* srcCamera = srcNode ? dynamic_cast<CameraNode*>(srcNode->node.get()) : nullptr;
               auto* srcLight = srcNode ? dynamic_cast<LightNode*>(srcNode->node.get()) : nullptr;

               bool valid = false;
               if (GraphNode::IsOutputPin(a) && srcNode != nullptr && dstNode != nullptr && differentNodes)
               {
                  // modulators patch into parameters and into Math's inputs;
                  // image nodes patch into image inputs
                  const bool dstWantsImage =
                     dynamic_cast<ImageAnalyzeNode*>(dstNode->node.get()) != nullptr;

                  if (GraphNode::IsParamPin(b))
                     valid = srcIsModulator;
                  else if (GraphNode::IsColorPin(b))
                     valid = srcPalette != nullptr;
                  else if (GraphNode::IsInputPin(b))
                  {
                     const int slot = GraphNode::InputSlotFromPin(b);
                     if (dstRender != nullptr)
                     {
                        if (slot < Render3DNode::kSlots)
                           valid = srcGeometry != nullptr || srcCloud != nullptr;
                        else if (slot == Render3DNode::kSlots)
                           valid = srcCamera != nullptr;
                        else if (slot == Render3DNode::kEnvSlot)
                           // Only an HDRI node: Render 3D reads this pin's
                           // rotation/intensity/mip-chain, which only an
                           // EnvironmentNode has. Any other image source would
                           // connect but be silently ignored at render time -
                           // rejecting it here instead means a mistaken drag
                           // (e.g. an Image Source, which clamps to 8-bit and
                           // cannot supply those extras anyway) shows as a
                           // refused link rather than a pin that does nothing.
                           valid = srcNode != nullptr &&
                                   dynamic_cast<EnvironmentNode*>(srcNode->node.get()) != nullptr;
                        else
                           valid = srcLight != nullptr;
                     }
                     else if (dstGeoOp != nullptr)
                        valid = srcGeometry != nullptr;
                     else if (dstInstance != nullptr)
                        // Slot 2 is the point-cloud pin; 0 and 1 take geometry.
                        valid = (slot == 2) ? (srcCloud != nullptr) : (srcGeometry != nullptr);
                     else if (dstNull3D != nullptr || dstMapping != nullptr || dstMeshPoints != nullptr ||
                              dstMeshResynth != nullptr || dstCloth != nullptr || dstJoin != nullptr ||
                              dstSwitcher3D != nullptr)
                        valid = srcGeometry != nullptr;
                     else if (dstWrap != nullptr)
                        valid = srcGeometry != nullptr;
                     else if (dstMeta != nullptr)
                        valid = srcCloud != nullptr;
                     else if (dstPath != nullptr)
                        valid = (slot == 0) ? (srcCurve != nullptr) : (srcGeometry != nullptr);
                     else if (dstMaterial != nullptr)
                        // Slot 0 takes geometry; the rest are ordinary images.
                        valid = (slot == 0) ? (srcGeometry != nullptr) : !srcIsModulator;
                     else if (dstDisplacement != nullptr)
                        // Slot 0 takes geometry; slot 1 the displacement texture.
                        valid = (slot == 0) ? (srcGeometry != nullptr) : !srcIsModulator;
                     else if (dstOutput != nullptr && slot == 1)
                        valid = srcAudioFile != nullptr; // slot 1 is the audio pin
                     else if (srcGeometry != nullptr || srcCloud != nullptr ||
                              srcCamera != nullptr || srcLight != nullptr)
                        valid = false; // 3D cables only go into 3D nodes
                     else if (dstAudio != nullptr)
                        valid = srcAudioFile != nullptr; // only an Audio File feeds Audio Analyze
                     else
                        valid = (dstMath != nullptr && !dstWantsImage) ? srcIsModulator : !srcIsModulator;
                  }
               }

               if (valid && ed::AcceptNewItem())
               {
                  PushUndoCheckpoint();
                  if (GraphNode::IsParamPin(b))
                  {
                     Modulation::Instance().Bind(dstNode->index,
                                                 GraphNode::ParamIndexFromPin(b),
                                                 srcNode->index,
                                                 GraphNode::OutputIndexFromPin(a));
                  }
                  else if (GraphNode::IsColorPin(b))
                  {
                     // Hand out a different swatch each time rather than the
                     // same one: dragging a palette onto a ramp's five stops in
                     // turn should lay the palette across the gradient, which
                     // is the whole point, not paint it a flat colour five
                     // times over.
                     PaletteBinding& palette = PaletteBinding::Instance();
                     const int used = palette.BindingCountFrom(srcNode->index, dstNode->index);
                     const int count = std::max(1, srcPalette->SwatchCount());
                     palette.Bind(dstNode->index, GraphNode::ColorIndexFromPin(b),
                                  srcNode->index, used % count);
                  }
                  else if (dstRender != nullptr)
                  {
                     const int slot = GraphNode::InputSlotFromPin(b);
                     if (slot < Render3DNode::kSlots)
                     {
                        dstRender->geometry[slot] = srcGeometry;
                        dstRender->clouds[slot] = srcCloud;
                     }
                     else if (slot == Render3DNode::kSlots)
                        dstRender->camera = srcCamera;
                     else if (slot == Render3DNode::kEnvSlot)
                        dstRender->envInput.Connect(srcNode->node.get());
                     else
                        dstRender->lights[slot - Render3DNode::kSlots - 1] = srcLight;
                  }
                  else if (dstGeoOp != nullptr)
                  {
                     dstGeoOp->input = srcGeometry;
                  }
                  else if (dstNull3D != nullptr)
                     dstNull3D->input = srcGeometry;
                  else if (dstMapping != nullptr)
                     dstMapping->input = srcGeometry;
                  else if (dstMeshPoints != nullptr)
                     dstMeshPoints->input = srcGeometry;
                  else if (dstMeshResynth != nullptr)
                     dstMeshResynth->input = srcGeometry;
                  else if (dstCloth != nullptr)
                     dstCloth->input = srcGeometry;
                  else if (dstMeta != nullptr)
                     dstMeta->cloudSource = srcCloud;
                  else if (dstPath != nullptr)
                  {
                     if (GraphNode::InputSlotFromPin(b) == 0)
                        dstPath->curveSource = srcCurve;
                     else
                        dstPath->geometrySource = srcGeometry;
                  }
                  else if (dstJoin != nullptr)
                  {
                     const int jslot = GraphNode::InputSlotFromPin(b);
                     if (jslot >= 0 && jslot < JoinGeometryNode::kSlots)
                        dstJoin->inputs[jslot] = srcGeometry;
                  }
                  else if (dstSwitcher3D != nullptr)
                  {
                     const int sslot = GraphNode::InputSlotFromPin(b);
                     if (sslot >= 0 && sslot < Switcher3DNode::kSlots)
                        dstSwitcher3D->inputs[sslot] = srcGeometry;
                  }
                  else if (dstWrap != nullptr)
                  {
                     const int wslot = GraphNode::InputSlotFromPin(b);
                     if (wslot == 0) dstWrap->sourceInput = srcGeometry;
                     else if (wslot == 1) dstWrap->targetInput = srcGeometry;
                  }
                  else if (dstMaterial != nullptr && GraphNode::InputSlotFromPin(b) == 0)
                     dstMaterial->input = srcGeometry;
                  else if (dstDisplacement != nullptr && GraphNode::InputSlotFromPin(b) == 0)
                     dstDisplacement->input = srcGeometry;
                  else if (dstInstance != nullptr)
                  {
                     const int islot = GraphNode::InputSlotFromPin(b);
                     if (islot == 0)
                        dstInstance->pointSource = srcGeometry;
                     else if (islot == 1)
                        dstInstance->instanceShape = srcGeometry;
                     else
                        dstInstance->cloudSource = srcCloud;
                  }
                  else if (dstAudio != nullptr)
                  {
                     dstAudio->fileSource = srcAudioFile;
                  }
                  else if (dstOutput != nullptr && GraphNode::InputSlotFromPin(b) == 1)
                  {
                     dstOutput->audioSource = srcAudioFile;
                  }
                  else if (dstMath != nullptr)
                  {
                     auto* mod = ModulatorForOutput(srcNode->node.get(), GraphNode::OutputIndexFromPin(a));
                     if (GraphNode::InputSlotFromPin(b) == 0)
                        dstMath->inputA = mod;
                     else
                        dstMath->inputB = mod;
                  }
                  else
                  {
                     ImageCable* cable = CableFor(*dstNode, GraphNode::InputSlotFromPin(b));
                     if (cable != nullptr)
                        cable->Connect(srcNode->node.get());
                  }
               }
               else if (!valid)
               {
                  ed::RejectNewItem(ImColor(255, 80, 80), 2.0f);
               }
            }
         }
      }
      ed::EndCreate();

      // ---- keyboard: delete + copy/paste ----
      const bool typing = io.WantTextInput;
      const bool cmdOrCtrl = io.KeyCtrl || io.KeySuper;

      // Shift+Cmd+Z is the Mac convention for redo; Ctrl+Y also works for
      // anyone used to the Windows/Linux binding.
      if (!typing && cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
         Undo();
      if (!typing && ((cmdOrCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
                      (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))))
         Redo();

      // Shift+A selects every node on the canvas.
      if (!typing && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false))
      {
         ed::ClearSelection();
         for (GraphNode& gn : gNodes)
            ed::SelectNode(gn.NodeId(), true);
      }

      // "/" drops a comment under the pointer and puts the caret straight into
      // it, so annotating a patch is one keystroke and then typing. Not gated on
      // Shift, so "?" does not leave a stray comment behind, and not on a
      // modifier, so Cmd-/ stays free for a binding later.
      if (!typing && !cmdOrCtrl && !io.KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_Slash, false))
      {
         // The pointer is only meaningful over the canvas; anywhere else (the
         // node panel, off the window entirely) the middle of the view is the
         // only sensible place for it.
         const ImVec2 mouse = ImGui::GetMousePos();
         const bool overGraph = mouse.x >= gGraphScreenTL.x &&
                                mouse.y >= gGraphScreenTL.y &&
                                mouse.x <= gGraphScreenTL.x + gGraphScreenSize.x &&
                                mouse.y <= gGraphScreenTL.y + gGraphScreenSize.y;
         const ImVec2 at = overGraph ? ed::ScreenToCanvas(mouse) : gViewCenterCanvas;
         if (GraphNode* gn = SpawnNode("Comment", "Compositing", at.x, at.y))
         {
            gCommentEdit.target = static_cast<CommentNode*>(gn->node.get());
            gCommentEdit.justOpened = true;
            // The '/' itself is already in the queue for this frame; without
            // this it lands in the note that is about to take the keyboard and
            // every comment starts with a slash.
            io.InputQueueCharacters.resize(0);
         }
      }

      if (!typing && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
      {
         int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            std::vector<ed::LinkId> selLinks(count);
            int linkCount = ed::GetSelectedLinks(selLinks.data(), count);

            // A selected group takes its members with it - otherwise "delete"
            // on a group would silently do no more than an ungroup.
            std::set<int> toDelete;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toDelete.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toDelete.insert(it->second.begin(), it->second.end());
               }
            }

            // One checkpoint for the whole batch: RemoveNodeByIndex (and the
            // per-link path below) each push their own by default, which
            // would otherwise turn "delete this group" into a checkpoint per
            // node - so a single Undo only clawed back the last one removed
            // instead of the whole cluster.
            if (linkCount > 0 || !toDelete.empty())
               PushUndoCheckpoint();
            gSuppressUndoCheckpoints = true;

            for (int i = 0; i < linkCount; i++)
            {
               DisconnectLinkById((int)selLinks[i].Get());
               ed::DeleteLink(selLinks[i]);
            }
            for (int index : toDelete)
            {
               ed::DeleteNode(ed::NodeId(index * GraphNode::kStride));
               RemoveNodeByIndex(index);
            }

            gSuppressUndoCheckpoints = false;
            ed::ClearSelection();
         }
      }

      // Shift+D duplicates whatever is selected without touching the clipboard.
      if (!typing && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false))
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // A selected group brings its members along, even if they are not
            // individually part of the editor's own selection set.
            std::set<int> toDup;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toDup.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toDup.insert(it->second.begin(), it->second.end());
               }
            }

            // Resolve everything first: SpawnNode can reallocate gNodes.
            const ImVec2 off = ClusterOffset(toDup);
            struct DupItem
            {
               std::string type; std::string category; INode* src; ImVec2 pos; bool params;
               bool miniViewport;
               int origIndex; int origGroup;
            };
            std::vector<DupItem> items;
            for (int index : toDup)
            {
               if (GraphNode* gn = FindNodeByIndex(index))
               {
                  const ImVec2 p = ed::GetNodePosition(gn->NodeId());
                  items.push_back({ gn->typeName, gn->category, gn->node.get(),
                                    ImVec2(p.x + off.x, p.y + off.y), gn->showParams,
                                    gn->showMiniViewport,
                                    gn->index, IndexOfGroupNode(GroupOwning(gn->index)) });
               }
            }

            ed::ClearSelection();
            // One checkpoint for the whole duplicate: SpawnNode pushes its
            // own by default, which would otherwise scatter a multi-node
            // duplicate across several undo steps instead of one.
            if (!items.empty())
               PushUndoCheckpoint();
            gSuppressUndoCheckpoints = true;
            std::map<int, GraphNode*> newByOrig;
            for (const DupItem& item : items)
            {
               if (GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y))
               {
                  CopyParams(copy->node.get(), item.src);
                  copy->showParams = item.params;
                  copy->showMiniViewport = item.miniViewport;
                  newByOrig[item.origIndex] = copy;
                  gPendingSelect.push_back(copy->NodeId());
               }
            }
            // Re-establish group membership among the duplicates.
            for (const DupItem& item : items)
            {
               if (item.origGroup < 0)
                  continue;
               auto groupIt = newByOrig.find(item.origGroup);
               auto memberIt = newByOrig.find(item.origIndex);
               if (groupIt == newByOrig.end() || memberIt == newByOrig.end())
                  continue;
               if (auto* g = dynamic_cast<GroupNode*>(groupIt->second->node.get()))
                  gGroupMembers[g].insert(memberIt->second->index);
            }
            gSuppressUndoCheckpoints = false;
         }
      }

      const bool doGroup =
         gRequestGroup ||
         (!typing && cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false));
      const bool doUngroup =
         gRequestUngroup ||
         (!typing && cmdOrCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false));
      gRequestGroup = false;
      gRequestUngroup = false;

      // Cmd/Ctrl+Shift+G dissolves a group, leaving its nodes exactly where
      // they sit. Only the group node goes away - RemoveNodeByIndex drops the
      // membership set with it, so the freed nodes are immediately available
      // for another group to adopt.
      //
      // Selecting a member counts as selecting its group: having to click the
      // header first would mean the one gesture that says "this cluster" is
      // not the gesture that can dissolve it.
      if (doUngroup)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // Resolved up front: RemoveNodeByIndex erases from gNodes, which
            // invalidates every GraphNode* taken before it.
            std::set<int> doomed;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* sel = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (sel == nullptr)
                  continue;
               if (dynamic_cast<GroupNode*>(sel->node.get()) != nullptr)
               {
                  doomed.insert(sel->index);
                  continue;
               }
               if (GroupNode* owner = GroupOwning(sel->index))
               {
                  for (GraphNode& g : gNodes)
                  {
                     if (g.node.get() == owner)
                        doomed.insert(g.index);
                  }
               }
            }

            // One checkpoint for the whole batch - see the Delete-key handler
            // above for why (multiple groups dissolved at once would
            // otherwise leave Undo only able to claw back the last one).
            if (!doomed.empty())
               PushUndoCheckpoint();
            gSuppressUndoCheckpoints = true;
            for (int index : doomed)
            {
               ed::DeleteNode(ed::NodeId(index * GraphNode::kStride));
               RemoveNodeByIndex(index);
            }
            gSuppressUndoCheckpoints = false;
            if (!doomed.empty())
               ed::ClearSelection();
         }
      }

      // Cmd/Ctrl+G wraps the current selection in a Group node sized to its
      // bounding box, so a cluster of existing nodes sticks together and
      // drags as one without having to hand-drag the group's edges around
      // them first.
      if (doGroup)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            bool any = false;
            ImVec2 bmin(0.0f, 0.0f), bmax(0.0f, 0.0f);
            std::set<int> picked;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* member = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (member == nullptr || dynamic_cast<GroupNode*>(member->node.get()) != nullptr)
                  continue; // grouping a group isn't supported
               // Membership is exclusive, so a node that already belongs
               // somewhere stays where it is rather than being pulled into a
               // second group that would then fight the first one for it.
               if (GroupOwning(member->index) != nullptr)
                  continue;
               picked.insert(member->index);
               const ImVec2 p = ed::GetNodePosition(member->NodeId());
               const ImVec2 s = ed::GetNodeSize(member->NodeId());
               if (!any)
               {
                  bmin = p;
                  bmax = ImVec2(p.x + s.x, p.y + s.y);
                  any = true;
               }
               else
               {
                  bmin.x = std::min(bmin.x, p.x);
                  bmin.y = std::min(bmin.y, p.y);
                  bmax.x = std::max(bmax.x, p.x + s.x);
                  bmax.y = std::max(bmax.y, p.y + s.y);
               }
            }

            if (any)
            {
               // Space for the label row above the box - AutoFitGroupToMembers
               // corrects both this and the size from the real measured header
               // on the group's first drawn frame, so it only has to be close.
               const float headerAllowance = 34.0f;
               if (GraphNode* gn = SpawnNode("Group", "Compositing",
                                             bmin.x - kGroupPadding,
                                             bmin.y - kGroupPadding - headerAllowance))
               {
                  auto* g = static_cast<GroupNode*>(gn->node.get());
                  g->width = (bmax.x - bmin.x) + kGroupPadding * 2.0f;
                  g->height = (bmax.y - bmin.y) + kGroupPadding * 2.0f;
                  // Claim the selection explicitly rather than leaving it to
                  // the geometric adoption pass: these are the nodes the user
                  // pointed at, whether or not the initial box happens to
                  // cover every one of them exactly.
                  gGroupMembers[g] = picked;
                  ed::ClearSelection();
                  gPendingSelect.push_back(gn->NodeId());
               }
            }
         }
      }

      if (!typing && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
      {
         clipboard.clear();
         clipboardSources.clear();
         clipboardOrigIndex.clear();
         clipboardOrigGroup.clear();
         int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // A selected group brings its members along, even if they are not
            // individually part of the editor's own selection set.
            std::set<int> toCopy;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toCopy.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toCopy.insert(it->second.begin(), it->second.end());
               }
            }

            for (int index : toCopy)
            {
               GraphNode* gn = FindNodeByIndex(index);
               if (gn == nullptr)
                  continue;
               clipboard.push_back(gn->typeName);
               clipboardSources.push_back(gn->node.get());
               clipboardOrigIndex.push_back(gn->index);
               clipboardOrigGroup.push_back(IndexOfGroupNode(GroupOwning(gn->index)));
            }
         }
      }

      if (!typing && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !clipboard.empty())
      {
         // Recomputed fresh against the canvas as it stands right now, so a
         // second Cmd+V (which already sees the first paste sitting on the
         // canvas) lands clear of that too, not on top of it.
         const ImVec2 off =
            ClusterOffset(std::set<int>(clipboardOrigIndex.begin(), clipboardOrigIndex.end()));

         // resolve sources first: SpawnNode can reallocate gNodes and invalidate pointers
         struct PasteItem
         {
            std::string type; std::string category; INode* src; ImVec2 pos;
            int origIndex; int origGroup;
         };
         std::vector<PasteItem> items;
         for (size_t i = 0; i < clipboard.size(); i++)
         {
            for (GraphNode& gn : gNodes)
            {
               if (gn.node.get() == clipboardSources[i])
               {
                  ImVec2 p = ed::GetNodePosition(gn.NodeId());
                  items.push_back({ clipboard[i], gn.category, gn.node.get(),
                                    ImVec2(p.x + off.x, p.y + off.y),
                                    clipboardOrigIndex[i], clipboardOrigGroup[i] });
                  break;
               }
            }
         }
         // One checkpoint for the whole paste: SpawnNode pushes its own by
         // default, which would otherwise scatter a multi-node paste across
         // several undo steps instead of one.
         if (!items.empty())
            PushUndoCheckpoint();
         gSuppressUndoCheckpoints = true;
         std::map<int, GraphNode*> newByOrig;
         for (const PasteItem& item : items)
         {
            GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y);
            if (copy != nullptr)
            {
               CopyParams(copy->node.get(), item.src);
               newByOrig[item.origIndex] = copy;
            }
         }
         // Re-establish group membership among the pasted copies.
         for (const PasteItem& item : items)
         {
            if (item.origGroup < 0)
               continue;
            auto groupIt = newByOrig.find(item.origGroup);
            auto memberIt = newByOrig.find(item.origIndex);
            if (groupIt == newByOrig.end() || memberIt == newByOrig.end())
               continue;
            if (auto* g = dynamic_cast<GroupNode*>(groupIt->second->node.get()))
               gGroupMembers[g].insert(memberIt->second->index);
         }
         gSuppressUndoCheckpoints = false;
      }

      // ---- handle deletions raised by the editor itself ----
      if (ed::BeginDelete())
      {
         ed::LinkId linkId;
         while (ed::QueryDeletedLink(&linkId))
         {
            if (ed::AcceptDeletedItem())
            {
               PushUndoCheckpoint();
               DisconnectLinkById((int)linkId.Get());
            }
         }

         ed::NodeId nodeId;
         while (ed::QueryDeletedNode(&nodeId))
         {
            if (ed::AcceptDeletedItem())
               RemoveNodeByIndex((int)nodeId.Get() / GraphNode::kStride);
         }
      }
      ed::EndDelete();

      // ---- undo checkpoint for node drags ----
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         gDragStartSnapshot = BuildPatchData();
         gDragSnapshotValid = true;
         gDragSnapshotPushed = false;
      }
      if (gDragSnapshotValid && !gDragSnapshotPushed &&
          ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
      {
         bool moved = false;
         for (const Patch::NodeRecord& rec : gDragStartSnapshot.nodes)
         {
            GraphNode* gn = FindNodeByIndex(rec.index);
            if (gn != nullptr &&
                (std::fabs(gn->liveX - rec.x) > 0.5f || std::fabs(gn->liveY - rec.y) > 0.5f))
            {
               moved = true;
               break;
            }
         }
         if (moved)
         {
            PushUndoSnapshot(gDragStartSnapshot);
            gDragSnapshotPushed = true;
         }
      }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         gDragSnapshotValid = false;

      // ---- snap to grid once the drag finishes ----
      if (gSnapToGrid && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
         for (GraphNode& gn : gNodes)
         {
            ImVec2 p = ed::GetNodePosition(gn.NodeId());
            ImVec2 snapped(std::round(p.x / gGridSnap) * gGridSnap,
                           std::round(p.y / gGridSnap) * gGridSnap);
            if (std::fabs(snapped.x - p.x) > 0.01f || std::fabs(snapped.y - p.y) > 0.01f)
               ed::SetNodePosition(gn.NodeId(), snapped);
         }
      }

      // ---- popups: search, spawn menu, dropdown ----
      ed::Suspend();

      DrawMinimap();

      // Right-click (two-finger click on a Mac trackpad) opens the same
      // type-to-filter picker as double-click, so the keyboard works either way.
      if (ed::ShowBackgroundContextMenu())
      {
         gSpawnPos = ed::ScreenToCanvas(ImGui::GetMousePos());
         searchBuf[0] = '\0';
         searchJustOpened = true;
         ImGui::OpenPopup("search");
      }

      // Right-click (two-finger click on a Mac trackpad) a node or group ->
      // a menu of actions specific to what got clicked, rather than only the
      // menu-bar/shortcut routes to the same operations.
      {
         ed::NodeId contextNodeId = 0;
         // A right-click already claimed by a param this frame (see
         // gParamRightClickConsumedThisFrame) opens that param's text field
         // instead - node editor still reports the click as a node context
         // menu request, so it has to be swallowed here rather than upstream.
         if (ed::ShowNodeContextMenu(&contextNodeId) && !gParamRightClickConsumedThisFrame)
         {
            gContextMenuNodeIndex = (int)contextNodeId.Get() / GraphNode::kStride;
            ImGui::OpenPopup("##nodecontext");
         }
      }
      if (ImGui::BeginPopup("##nodecontext"))
      {
         GraphNode* gn = FindNodeByIndex(gContextMenuNodeIndex);
         if (gn == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
         {
            if (ImGui::MenuItem("Rename"))
            {
               PushUndoCheckpoint();
               g->renaming = true;
               g->renameJustStarted = true;
            }
            if (ImGui::MenuItem("Ungroup"))
            {
               ed::ClearSelection();
               ed::SelectNode(gn->NodeId());
               gRequestUngroup = true;
            }
         }
         else
         {
            if (gn->showParams)
            {
               if (ImGui::MenuItem("Hide params"))
                  gn->showParams = false;
            }
            else
            {
               if (ImGui::MenuItem("Show params"))
                  gn->showParams = true;
            }
            if (dynamic_cast<IGeometrySource*>(gn->node.get()) != nullptr)
            {
               if (gn->showMiniViewport)
               {
                  if (ImGui::MenuItem("Hide viewport"))
                     gn->showMiniViewport = false;
               }
               else
               {
                  if (ImGui::MenuItem("Show viewport"))
                     gn->showMiniViewport = true;
               }
            }
            if (CanShowInViewportPanel(*gn))
            {
               // Adds a card rather than replacing whatever is already shown -
               // the panel holds any number of nodes at once, each closable
               // on its own. A no-op if this node already has a card open.
               if (ImGui::MenuItem("Open in viewport panel"))
               {
                  if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), gn->index) ==
                      gViewportPanelNodes.end())
                     gViewportPanelNodes.push_back(gn->index);
               }
            }
            if (ImGui::MenuItem("Help"))
            {
               gHelpPopupNodeIndex = gn->index;
               ImGui::CloseCurrentPopup();
               gOpenNodeHelpPopup = true;
            }
            if (GroupNode* owner = GroupOwning(gn->index))
            {
               // Unlike the group's own "Ungroup" (which dissolves the whole
               // cluster), this detaches just the one node that was
               // right-clicked - its groupmates stay put.
               if (ImGui::MenuItem("Ungroup"))
               {
                  PushUndoCheckpoint();
                  gGroupMembers[owner].erase(gn->index);
                  // Membership here is purely geometric - anything fully
                  // inside the group's box gets adopted right back in next
                  // frame. Nudging the node just past the box's bottom edge
                  // is what makes removing it actually stick.
                  if (int ownerIndex = IndexOfGroupNode(owner); ownerIndex >= 0)
                  {
                     if (GraphNode* ownerGn = FindNodeByIndex(ownerIndex))
                     {
                        const ImVec2 gp = ed::GetNodePosition(ownerGn->NodeId());
                        const ImVec2 gs = ed::GetNodeSize(ownerGn->NodeId());
                        const ImVec2 mp = ed::GetNodePosition(gn->NodeId());
                        ed::SetNodePosition(gn->NodeId(), ImVec2(mp.x, gp.y + gs.y + 40.0f));
                     }
                  }
               }
            }
         }
         ImGui::EndPopup();
      }

      // Node "Help" popup, opened via the context menu above. A separate
      // OpenPopup call (rather than nesting it inside "##nodecontext") because
      // that popup already closed itself this frame - reopening a still-being-
      // closed popup by the same ID silently no-ops in Dear ImGui.
      if (gOpenNodeHelpPopup)
      {
         ImGui::OpenPopup("##nodehelp");
         gOpenNodeHelpPopup = false;
      }
      ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(420, FLT_MAX));
      if (ImGui::BeginPopup("##nodehelp"))
      {
         GraphNode* gn = FindNodeByIndex(gHelpPopupNodeIndex);
         if (gn == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            ImGui::TextUnformatted(NodeTitle(*gn).c_str());
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
            ImGui::TextWrapped("%s", NodeHelpText(*gn));
            ImGui::PopTextWrapPos();
         }
         ImGui::EndPopup();
      }

      // double-click empty canvas -> searchable spawner
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
          !ed::GetHoveredNode() && !ed::GetHoveredPin() && !ed::GetHoveredLink())
      {
         gSpawnPos = ed::ScreenToCanvas(ImGui::GetMousePos());
         searchBuf[0] = '\0';
         searchJustOpened = true;
         ImGui::OpenPopup("search");
      }

      // dev-only: force the picker open (optionally with a query) to verify layout
      if (const char* pk = getenv("INFINITE_PICKERTEST"))
      {
         if (frameId == 6)
         {
            gSpawnPos = ImVec2(200.0f, 200.0f);
            snprintf(searchBuf, sizeof(searchBuf), "%s", std::string(pk) == "empty" ? "" : pk);
            searchJustOpened = (std::string(pk) == "empty");
            ImGui::OpenPopup("search");
         }
      }

      if (gDropdown.justOpened)
      {
         ImGui::OpenPopup("##dropdown");
         gDropdown.justOpened = false;
      }

      if (getenv("INFINITE_COLORTEST") != nullptr && frameId == 6)
      {
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         sh->fillColor[0] = 0.9f; sh->fillColor[1] = 0.35f; sh->fillColor[2] = 0.2f;
         gColor.target = sh->fillColor;
         gColor.owner = sh;
         gColor.label = "fill";
         gColor.justOpened = true;
      }

      if (gCommentEdit.justOpened)
      {
         ImGui::OpenPopup("##commentedit");
         gCommentEdit.justOpened = false;
      }

      // the comment may have been deleted while its editor was open
      if (gCommentEdit.target != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gCommentEdit.target)
               alive = true;
         }
         if (!alive)
            gCommentEdit.target = nullptr;
      }

      // Pinned to the node's own on-screen box (refreshed every frame in
      // DrawCommentPreview) so the editor lands exactly over the note instead
      // of opening as a separate window elsewhere on screen - typing is meant
      // to read as happening straight into the box you double-clicked.
      if (gCommentEdit.target != nullptr && gCommentEditRect.z > 0.0f)
      {
         ImGui::SetNextWindowPos(ImVec2(gCommentEditRect.x, gCommentEditRect.y));
         ImGui::SetNextWindowSize(ImVec2(gCommentEditRect.z, gCommentEditRect.w));
      }
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
      if (gCommentEdit.target != nullptr)
      {
         const float* col = gCommentEdit.target->color;
         ImGui::PushStyleColor(ImGuiCol_PopupBg,
                               ImVec4(col[0] * 0.16f, col[1] * 0.16f, col[2] * 0.16f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(col[0], col[1], col[2], 0.8f));
      }
      if (ImGui::BeginPopup("##commentedit", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
      {
         if (gCommentEdit.target != nullptr)
         {
            CommentNode* c = gCommentEdit.target;
            gCommentEdit.framesOpen++;
            if (gCommentEdit.framesOpen <= 4) // see CommentEditRequest::framesOpen
            {
               ImGui::SetWindowFocus();
               ImGui::SetKeyboardFocusHere();
            }
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            // Filling the whole popup rather than a fixed size: the popup is
            // already pinned to the node's box, so the field just fills it.
            ImGui::InputTextMultiline("##commenttext", &c->text, ImVec2(-FLT_MIN, -FLT_MIN));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            // The checkpoint was pushed when the editor opened, so every
            // keystroke here is part of that one undo step; all that is left is
            // to keep the patch marked unsaved.
            if (ImGui::IsItemEdited())
               gPatchDirty = true;
            // Esc finishes the same way clicking outside does - no separate
            // "done" step needed for a note this small.
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
               ImGui::CloseCurrentPopup();
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }
      if (gCommentEdit.target != nullptr)
         ImGui::PopStyleColor(2);
      ImGui::PopStyleVar(3);
      if (!ImGui::IsPopupOpen("##commentedit"))
      {
         gCommentEdit.target = nullptr;
         gCommentEdit.framesOpen = 0;
      }

      if (gColor.justOpened)
      {
         ImGui::OpenPopup("##colorpick");
         gColor.justOpened = false;
      }

      // the owning node may have been deleted while the picker was open
      if (gColor.owner != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gColor.owner)
               alive = true;
         }
         if (!alive)
         {
            gColor.owner = nullptr;
            gColor.target = nullptr;
         }
      }

      if (ImGui::BeginPopup("##colorpick"))
      {
         if (gColor.target != nullptr)
         {
            ImGui::TextDisabled("%s", gColor.label.c_str());
            gColorPickerRect = ImVec4(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y,
                                      0.0f, 0.0f);
            ImGui::ColorPicker3("##pick", gColor.target,
                                ImGuiColorEditFlags_PickerHueBar |
                                ImGuiColorEditFlags_DisplayRGB |
                                ImGuiColorEditFlags_DisplayHSV |
                                ImGuiColorEditFlags_DisplayHex);
            gColorPickerRect.z = ImGui::GetItemRectSize().x;
            gColorPickerRect.w = ImGui::GetItemRectSize().y;
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(400, 440));
      if (ImGui::BeginPopup("search"))
      {
         if (searchJustOpened)
         {
            ImGui::SetKeyboardFocusHere();
            searchJustOpened = false;
         }
         ImGui::SetNextItemWidth(280);
         ImGui::InputTextWithHint("##q", "search nodes...", searchBuf, sizeof(searchBuf));
         ImGui::Separator();

         std::string q(searchBuf);
         std::transform(q.begin(), q.end(), q.begin(), ::tolower);

         const bool pickFirst = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
         std::string spawnName, spawnCategory;
         int shown = 0;

         if (q.empty())
         {
            // no query yet: browse by category
            for (const std::string& category : NodeFactory::Instance().GetCategories())
            {
               ImGui::SeparatorText(DisplayName(category).c_str());
               for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
               {
                  ++shown;
                  if (ImGui::Selectable(DisplayName(name).c_str()))
                  {
                     spawnName = name;
                     spawnCategory = category;
                  }
               }
            }
         }
         else
         {
            for (const auto& t : allTypes)
            {
               std::string hay = t.first + " " + t.second;
               std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
               if (hay.find(q) == std::string::npos)
                  continue;
               ++shown;
               std::string entry = DisplayName(t.first) + "   (" + DisplayName(t.second) + ")";
               bool activate = ImGui::Selectable(entry.c_str());
               if (shown == 1 && pickFirst)
                  activate = true;
               if (activate)
               {
                  spawnName = t.first;
                  spawnCategory = t.second;
               }
            }
            if (shown == 0)
               ImGui::TextDisabled("no matches");
         }

         if (!spawnName.empty())
         {
            SpawnNode(spawnName, spawnCategory, gSpawnPos.x, gSpawnPos.y);
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      ImGui::SetNextWindowSizeConstraints(ImVec2(220, 0), ImVec2(420, 400));
      if (ImGui::BeginPopup("##dropdown"))
      {
         if (gDropdown.options != nullptr)
         {
            for (int i = 0; i < (int)gDropdown.options->size(); i++)
            {
               bool selected = (i == gDropdown.current);
               if (ImGui::Selectable((*gDropdown.options)[i].c_str(), selected))
               {
                  if (gDropdown.onSelect && i != gDropdown.current)
                  {
                     PushUndoCheckpoint();
                     gDropdown.onSelect(i);
                  }
                  ImGui::CloseCurrentPopup();
               }
               if (selected && ImGui::IsWindowAppearing())
                  ImGui::SetScrollHereY(0.5f);
            }
         }
         ImGui::EndPopup();
      }

      gHoveringItem = ed::GetHoveredNode() || ed::GetHoveredPin() || ed::GetHoveredLink();

      ed::Resume();

      // Fit-to-content has to happen down here, after every node has been
      // submitted this frame. ed::Begin() marks all nodes not-live and only
      // drawing them marks them live again, and NavigateToContent() measures
      // live nodes only - so called up next to ed::Begin() it always fit an
      // empty rectangle and silently did nothing at all. The new view is
      // picked up by the next ed::Begin(), one frame later.
      if (gRequestFitView)
      {
         ed::NavigateToContent(0.0f);
         gRequestFitView = false;
      }
      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture
      if (getenv("INFINITE_HIDETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture

      ed::End();
      ed::SetCurrentEditor(nullptr);

      io.MouseWheel = savedWheel;
      io.MouseWheelH = savedWheelH;

      // ---- node browser panel ----
      // The same catalogue as the canvas popup, but persistent: it can be left
      // open while building a patch, which the popup cannot.
      //
      // Drawn at an explicit width AND height (rather than the (0,0) "fill
      // remaining" it used before the viewport panel existed). Width, because
      // with the right-docked viewport panel also open, "remaining" would
      // include its space too - see the combined rightReserved calc above
      // ed::Begin(). Height, because "remaining" runs to the bottom of the
      // window, which swallows the row a bottom-docked viewport panel is
      // about to be drawn into and leaves that panel clipped out of sight.
      if (gNodePanelOpen)
      {
         ImGui::SameLine();
         ImGui::BeginChild("##nodepanel", ImVec2(kNodePanelWidth, graphHeight), true);

         static char panelSearch[128] = "";
         ImGui::SetNextItemWidth(-1.0f);
         ImGui::InputTextWithHint("##panelsearch", "search modules...", panelSearch, sizeof(panelSearch));

         std::string q = panelSearch;
         std::transform(q.begin(), q.end(), q.begin(), ::tolower);

         std::string spawnName, spawnCategory;
         ImGui::Separator();
         ImGui::BeginChild("##nodepanellist", ImVec2(0, 0), false);
         for (const std::string& category : NodeFactory::Instance().GetCategories())
         {
            // With a query the categories are only drawn when something in them
            // matches, so an empty heading never sits there on its own.
            std::vector<std::string> matches;
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
            {
               if (q.empty())
               {
                  matches.push_back(name);
                  continue;
               }
               std::string hay = name + " " + category;
               std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
               if (hay.find(q) != std::string::npos)
                  matches.push_back(name);
            }
            if (matches.empty())
               continue;

            ImGui::SeparatorText(DisplayName(category).c_str());
            for (const std::string& name : matches)
            {
               if (ImGui::Selectable(DisplayName(name).c_str()))
               {
                  spawnName = name;
                  spawnCategory = category;
               }
            }
         }
         ImGui::EndChild();

         if (!spawnName.empty())
         {
            // Aimed at the middle of the view rather than at the mouse: the
            // click happened over the panel, not over the canvas. Landing
            // exactly on the center every time would stack repeated clicks on
            // top of each other, so nudge to the nearest spot around the
            // center that isn't already covered by another node.
            ImVec2 pos = FindFreeSpawnPosition(gViewCenterCanvas);
            SpawnNode(spawnName, spawnCategory, pos.x, pos.y);
            gPatchDirty = true;
         }

         ImGui::EndChild();
      }

      // Right-docked viewport panel, chained via SameLine right after the
      // node browser panel above (whether or not that one is open) so the
      // two sit side by side instead of overlapping.
      if (viewportRight)
      {
         ImGui::SameLine();
         DrawViewportPanelDocked("##viewportpanel_right", ImVec2(gViewportPanelWidth, graphHeight));
      }

      // Bottom-docked viewport panel: a fresh, full-width row below the
      // canvas (and below the row above, if that one drew anything) rather
      // than same-line - see the graphHeight calc above ed::Begin(), which
      // already reserved this space.
      if (viewportBottom)
         DrawViewportPanelDocked("##viewportpanel_bottom", ImVec2(0, gViewportPanelHeight));

      ImGui::End();

      // ---- windows that must live outside the node canvas ----
      if (gFormulaEditorOpen && gFormulaEditor != nullptr)
      {
         // guard against the node being deleted while its editor is open
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFormulaEditor)
               alive = true;
         }
         if (!alive)
         {
            gFormulaEditor = nullptr;
            gFormulaEditorOpen = false;
         }
      }

      if (gFormulaEditorOpen && gFormulaEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
         if (ImGui::Begin("Formula editor", &gFormulaEditorOpen))
         {
            ImGui::TextDisabled("body of  vec4 shape(vec2 uv, vec2 p, float t)");
            ImGui::TextDisabled("p is centred (-0.5..0.5), t is transport seconds, uA-uD are the knobs");
            ImGui::Separator();

            static char editBuf[8192];
            static FormulaNode* lastEdited = nullptr;
            if (lastEdited != gFormulaEditor)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFormulaEditor->formula.c_str());
               lastEdited = gFormulaEditor;
            }

            ImGui::InputTextMultiline("##glsl", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 70));

            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               gFormulaEditor->formula = editBuf;
               gFormulaEditor->Apply();
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFormulaEditor->formula.c_str());

            if (!gFormulaEditor->LastError().empty())
            {
               ImGui::TextWrapped("%s", gFormulaEditor->LastError().c_str());
            }
         }
         ImGui::End();
      }

      if (gHelpOpen)
         DrawHelpWindow(&gHelpOpen);

      if (gShowUnsavedChangesModal)
      {
         ImGui::OpenPopup("Unsaved Changes");
         gShowUnsavedChangesModal = false;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::Text("This patch has unsaved changes.");
         ImGui::Text("Save before closing?");
         ImGui::Separator();
         if (ImGui::Button("Save", ImVec2(100, 0)))
         {
            SavePatchInteractive(false);
            // Only proceed if the save actually went through - a cancelled
            // Save As dialog or a write failure leaves gPatchDirty set, and
            // the modal should stay up so the user can try again.
            if (!gPatchDirty)
            {
               glfwSetWindowShouldClose(window, GLFW_TRUE);
               ImGui::CloseCurrentPopup();
            }
         }
         ImGui::SameLine();
         if (ImGui::Button("Don't Save", ImVec2(100, 0)))
         {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         if (ImGui::Button("Cancel", ImVec2(100, 0)))
            ImGui::CloseCurrentPopup();
         ImGui::EndPopup();
      }

      // Keep the title bar in sync with the open document. GLFW has no
      // native "dirty dot" hook, so the bullet is just part of the string -
      // the same convention every other non-native-document-model app uses.
      {
         static std::string sLastTitle;
         std::string base = gPatchPath.empty()
            ? std::string("Untitled")
            : gPatchPath.substr(gPatchPath.find_last_of('/') + 1);
         std::string title = (gPatchDirty ? std::string("\xE2\x80\xA2 ") : std::string()) +
            base + " \xE2\x80\x94 Infinite";
         if (title != sLastTitle)
         {
            glfwSetWindowTitle(window, title.c_str());
            sLastTitle = title;
         }
      }

      // ---- apply modulation and expressions, then cook ----
      // Deliberately after the UI: the parameter registry is rebuilt every frame
      // while nodes draw, so every pointer here belongs to a node that still
      // exists. Cooking before the UI would mean writing through last frame's
      // pointers, which dangle the moment a node is deleted.
      {
         Modulation& modulation = Modulation::Instance();
         // Snapshot every registered parameter's current value, keyed by node,
         // before any of this frame's writes land. This is what lets an
         // expression reference a sibling parameter by name ("width * 0.5")
         // without depending on the order params happened to draw in - and it
         // means a cycle between two expressions just settles a frame late
         // rather than reading half-updated state mid-pass.
         std::map<int, std::map<std::string, float>> paramSnapshot;
         for (const ParamRef& ref : modulation.FrameParams())
            if (ref.value != nullptr)
               paramSnapshot[ref.nodeIndex][ref.name] = *ref.value;

         const double t = Transport::Instance().Seconds();
         for (const ParamRef& ref : modulation.FrameParams())
         {
            if (ref.value == nullptr)
               continue;
            const Modulation::Source src = modulation.ModulatorFor(ref.nodeIndex, ref.paramIndex);
            if (src.nodeIndex >= 0)
            {
               // A wired modulator always wins over a typed expression - see
               // Modulation::SetExpression.
               GraphNode* modNode = FindNodeByIndex(src.nodeIndex);
               if (modNode == nullptr)
                  continue;
               auto* modulator = ModulatorForOutput(modNode->node.get(), src.outputIndex);
               if (modulator == nullptr)
                  continue;
               const float v01 = modulator->Value01();
               *ref.value = ref.minValue + (ref.maxValue - ref.minValue) * v01;
               continue;
            }
            const std::string* expr = modulation.ExpressionFor(ref.nodeIndex, ref.paramIndex);
            if (expr == nullptr)
               continue;
            float result = 0.0f;
            std::string error;
            if (Expression::Evaluate(*expr, t, &paramSnapshot[ref.nodeIndex], result, error))
            {
               *ref.value = std::min(std::max(result, ref.minValue), ref.maxValue);
               modulation.SetExpressionError(ref.nodeIndex, ref.paramIndex, std::string());
            }
            else
            {
               // Leave the last good value in place rather than snapping to 0 -
               // a typo mid-edit should not blank out the render.
               modulation.SetExpressionError(ref.nodeIndex, ref.paramIndex, error);
            }
         }
      }

      // A Palette is not an Output, so nothing downstream pulls it, and both its
      // preview and its bindings need this frame's swatches. Cooking here - after
      // modulation has been applied - is what lets a modulator drive the shaping
      // controls and have it land the same frame.
      for (GraphNode& gn : gNodes)
      {
         if (dynamic_cast<IPaletteSource*>(gn.node.get()) != nullptr)
            gn.node->CookIfNeeded(frameId);
      }

      // Colours, the same way and for the same reason: the registry was rebuilt
      // while the nodes drew, so every pointer here belongs to a node that
      // still exists. A palette has to cook before it can be read, and it is
      // not an Output so nothing else would pull it.
      {
         PaletteBinding& palette = PaletteBinding::Instance();
         for (const ColorRef& ref : palette.FrameColors())
         {
            const PaletteBinding::Source src = palette.SourceFor(ref.nodeIndex, ref.colorIndex);
            if (src.nodeIndex < 0 || ref.value == nullptr)
               continue;
            GraphNode* palNode = FindNodeByIndex(src.nodeIndex);
            if (palNode == nullptr)
               continue;
            auto* source = dynamic_cast<IPaletteSource*>(palNode->node.get());
            if (source == nullptr)
               continue;
            source->GetSwatch(src.swatchIndex, ref.value);
         }
      }

      for (GraphNode& gn : gNodes)
      {
         if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr)
            gn.node->CookIfNeeded(frameId);
      }
      if (getenv("INFINITE_SHOWCASE") != nullptr && frameId == 1)
      {
         for (const ParamRef& ref : Modulation::Instance().FrameParams())
         {
            if (ref.nodeIndex == gNodes[1].index && ref.name == "Amount")
               Modulation::Instance().Bind(ref.nodeIndex, ref.paramIndex, gNodes[5].index);
         }
      }

      if (getenv("INFINITE_MACROTEST") != nullptr)
      {
         auto& mod = Modulation::Instance();
         if (gNodes.size() < 3)
         {
            printf("MACROTEST fixture missing (%zu nodes)\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return 1;
         }
         auto* xy = static_cast<MacroXYNode*>(gNodes[2].node.get());
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         if (frameId == 2)
         {
            int sizeParam = -1, rotParam = -1;
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex != gNodes[0].index) continue;
               if (ref.name == "size") sizeParam = ref.paramIndex;
               if (ref.name == "rotation") rotParam = ref.paramIndex;
            }
            // X drives size, Y drives rotation - one pad, two destinations
            printf("sizeParam=%d rotParam=%d node0=%d node2=%d frameParams=%zu\n",
                   sizeParam, rotParam, gNodes[0].index, gNodes[2].index, mod.FrameParams().size());
            mod.Bind(gNodes[0].index, sizeParam, gNodes[2].index, 0);
            mod.Bind(gNodes[0].index, rotParam, gNodes[2].index, 1);
            printf("isModulated(size)=%d isModulated(rot)=%d\n",
                   (int)mod.IsModulated(gNodes[0].index, sizeParam),
                   (int)mod.IsModulated(gNodes[0].index, rotParam));
            xy->padX = 0.25f;
            xy->padY = 0.75f;
            printf("bound X->size Y->rotation\n");
         }
         if (frameId == 5)
         {
            printf("padX=%.2f -> size=%.4f (expect %.4f)\n", xy->padX, sh->size, 0.01f + 0.49f * 0.25f);
            printf("padY=%.2f -> rotation=%.4f (expect %.4f)\n", xy->padY, sh->rotation, -180.0f + 360.0f * 0.75f);
            xy->padX = 0.9f; xy->padY = 0.1f;
         }
         if (frameId == 8)
         {
            printf("after move: size=%.4f rotation=%.4f  %s\n", sh->size, sh->rotation,
                   (std::fabs(sh->size - (0.01f + 0.49f * 0.9f)) < 0.01f &&
                    std::fabs(sh->rotation - (-180.0f + 360.0f * 0.1f)) < 3.0f)
                      ? "INDEPENDENT OUTPUTS OK" : "MISMATCH");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_HIDETEST") != nullptr)
      {
         auto& mod = Modulation::Instance();
         if (frameId == 2)
         {
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "size")
                  mod.Bind(ref.nodeIndex, ref.paramIndex, gNodes[2].index);
            }
            printf("bound: links=%zu (frameParams=%zu)\n", mod.Links().size(), mod.FrameParams().size());
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index)
                  printf("   node0 param %d = %s\n", ref.paramIndex, ref.name.c_str());
            }
         }
         if (frameId == 4)
         {
            gNodes[0].showParams = false; // collapse the modulated node
            printf("params hidden\n");
         }
         if (frameId == 8)
         {
            printf("after hide: links=%zu %s\n", mod.Links().size(),
                   mod.Links().empty() ? "LOST - BUG" : "SURVIVED OK");
            // The binding surviving is not enough: a collapsed node still has to
            // show the cable, landing on its "mod" tag.
            int drawn = 0;
            for (const LinkInfo& link : gLinks)
               if (GraphNode::IsParamPin(link.dstPin) &&
                   GraphNode::NodeIndexFromPin(link.dstPin) == gNodes[0].index)
                  drawn++;
            printf("cable while hidden: %d %s\n", drawn,
                   drawn > 0 ? "DRAWN OK" : "MISSING - BUG");
         }
         if (frameId == 9)
         {
            gNodes[0].showParams = true; // reopen
         }
         if (frameId == 12)
         {
            printf("after reopen: links=%zu %s\n", mod.Links().size(),
                   mod.Links().empty() ? "LOST - BUG" : "SURVIVED OK");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_MODTEST") != nullptr)
      {
         if (frameId == 1)
         {
            // Shape.size is param index 4 (shape dropdown isn't a ModSlider):
            // width, height, size, aspect... resolve it by name instead of guessing.
            int sizeParam = -1;
            for (const ParamRef& ref : Modulation::Instance().FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "size")
                  sizeParam = ref.paramIndex;
            }
            printf("size param index = %d\n", sizeParam);
            if (sizeParam >= 0)
               Modulation::Instance().Bind(gNodes[0].index, sizeParam, gNodes[2].index);
            Transport::Instance().SetTempo(240.0f);
         }
         if (frameId >= 2 && frameId <= 40 && (frameId % 8) == 0)
         {
            auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
            auto* lfo = static_cast<LFONode*>(gNodes[2].node.get());
            printf("f%-3d beats=%.3f lfo=%.3f  shape.size=%.4f\n",
                   frameId, Transport::Instance().Beats(), lfo->Value01(), sh->size);
         }
         if (frameId == 44)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      for (GraphNode& gn : gNodes)
         gn.node->CookIfNeeded(frameId);

      // Top-level idle gate: NodeWorkCounter() only advances when some node
      // actually redid real work this frame (FilterNode's RunShaderPass,
      // Render3DNode's draw passes, ...) - a cache hit leaves it alone. A
      // patch with nothing left to compute settles into idle==true every
      // frame within a couple frames of its last real edit, the same way a
      // static Blender viewport stops re-rendering. This only observes that;
      // it deliberately does not skip glfwSwapBuffers/ImGui's own redraw,
      // since those still have to run for UI responsiveness (hover states,
      // cursor, animated widgets) regardless of whether the node graph is idle.
      if (getenv("INFINITE_CACHETEST") != nullptr)
      {
         static unsigned long long sPrevWork = 0;
         static int sIdleStreak = 0;
         const unsigned long long work = NodeWorkCounter();
         const bool idle = (work == sPrevWork);
         sIdleStreak = idle ? sIdleStreak + 1 : 0;
         sPrevWork = work;
         printf("CACHETEST frame=%d work=%llu idle=%d idleStreak=%d\n",
                frameId, work, idle ? 1 : 0, sIdleStreak);
      }

      if (getenv("INFINITE_TEXTFIT") != nullptr && frameId == 4 && !gNodes.empty())
      {
         if (auto* t = dynamic_cast<TextNode*>(gNodes[0].node.get()))
            printf("requested %.0f pt -> fitted %.1f pt (box %.0fx%.0f of %dx%d)\n",
                   t->fontSize, t->FittedSize(),
                   t->width * t->wrapWidth, t->height * t->wrapHeight,
                   t->GetOutputWidth(), t->GetOutputHeight());
      }

      if (selfTest && frameId >= 1)
      {
         int failures = 0;
         for (GraphNode& gn : gNodes)
         {
            // modulators emit a value, not a texture, so they are checked
            // differently - including nodes that expose taps rather than being
            // modulators themselves (Image/Audio Analyze).
            if (dynamic_cast<CameraNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<LightNode*>(gn.node.get()) != nullptr)
            {
               printf("%-22s [%-12s] scene node     OK\n", gn.typeName.c_str(), gn.category.c_str());
               continue;
            }
            if (auto* op = dynamic_cast<GeometryOpNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), op->TriangleCount());
               continue;
            }
            if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu instances  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), inst->InstanceCount());
               continue;
            }
            if (auto* ps = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu particles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), ps->AliveCount());
               continue;
            }
            if (dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MappingNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<ClothNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<WrapNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<CurveNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr)
            {
               // Pass-throughs and samplers with nothing patched in are empty
               // by definition, so that is not a failure.
               printf("%-22s [%-12s] geometry pass  OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }
            if (auto* oc = dynamic_cast<OceanNode*>(gn.node.get()))
            {
               const bool ok = oc->TriangleCount() > 0;
               if (!ok) ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), oc->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }
            if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), t3d->TriangleCount());
               continue;
            }
            if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
            {
               // A freshly spawned Model 3D has no file yet, so an empty mesh
               // here is correct rather than a failure - same as a Geometry Op
               // with nothing patched into it.
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), model->TriangleCount());
               continue;
            }
            if (auto* geo = dynamic_cast<GeometryNode*>(gn.node.get()))
            {
               // geometry emits a mesh, not a texture
               const bool ok = geo->TriangleCount() > 0;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), geo->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }

            if (dynamic_cast<AudioFileNode*>(gn.node.get()) != nullptr)
            {
               // an audio source has neither a texture nor a modulator tap
               printf("%-22s [%-12s] audio source   OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }

            IModulator* mod = dynamic_cast<IModulator*>(gn.node.get());
            if (mod == nullptr)
               mod = gn.node->ModulatorOutput(0);
            if (mod != nullptr)
            {
               const float v = mod->Value01();
               const bool ok = v >= 0.0f && v <= 1.0f;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] value=%.3f      %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), v, ok ? "OK" : "FAIL");
               continue;
            }

            bool ok = gn.node->GetOutputWidth() > 0 && gn.node->GetOutputTexture() != 0;
            if (!ok)
               ++failures;
            printf("%-22s [%-12s] %dx%d tex=%-3u %s\n",
                   gn.typeName.c_str(), gn.category.c_str(),
                   gn.node->GetOutputWidth(), gn.node->GetOutputHeight(),
                   gn.node->GetOutputTexture(), ok ? "OK" : "FAIL");
         }
         printf("\n%zu node types, %d failures\n", gNodes.size(), failures);
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }



      ImGui::Render();
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      glViewport(0, 0, fbW, fbH);
      glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

      if (const char* shotPath = getenv("IMAGERESYNTH_SCREENSHOT"))
      {
         if (frameId == 12)
         {
            std::vector<unsigned char> px(fbW * fbH * 4);
            glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            stbi_flip_vertically_on_write(1);
            stbi_write_png(shotPath, fbW, fbH, 4, px.data(), fbW * 4);
            printf("wrote %s (%dx%d)\n", shotPath, fbW, fbH);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      glfwSwapBuffers(window);
      ++frameId;

      // Patch shortcuts. Handled outside the node editor so its own Cmd-key
      // bindings do not swallow them, and gated on no text field having focus
      // so typing an 'S' into a Text node does not save the patch.
      if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeySuper)
      {
         if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            SavePatchInteractive(ImGui::GetIO().KeyShift);
         else if (ImGui::IsKeyPressed(ImGuiKey_O, false))
         {
            const std::string path = Platform::OpenPatchDialog();
            if (!path.empty())
               LoadPatchFrom(path);
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_N, false))
            NewPatch();
      }

      // Frame limiter. Sleeping most of the way there and spinning the last
      // sliver keeps the cap accurate without burning a core: sleep_for is only
      // accurate to a millisecond or two, which at 120fps is most of the budget.
      if (gTargetFps > 0)
      {
         const double budget = 1.0 / (double)gTargetFps;
         const double deadline = gFrameStart + budget;
         const double slack = deadline - glfwGetTime();
         if (slack > 0.002)
            std::this_thread::sleep_for(std::chrono::duration<double>(slack - 0.001));
         while (glfwGetTime() < deadline)
            std::this_thread::yield();
      }

      {
         // Measured after the swap so the number includes GPU work the driver
         // blocks on there, which is where a heavy 3D render actually lands.
         static double sPrevTime = 0.0;
         const double now = glfwGetTime();
         if (sPrevTime > 0.0)
            gLastFrameMs = (now - sPrevTime) * 1000.0;
         sPrevTime = now;
      }

      // Dev harness: quit after N frames. The self-tests above printf their
      // verdict and then run forever, so a scripted run has to kill the app -
      // which throws away stdout still sitting in the block buffer when it is
      // redirected to a file. Exiting normally lets it flush.
      if (const char* exitAfter = getenv("INFINITE_EXITAFTER"))
      {
         if (frameId >= atoi(exitAfter))
         {
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
   }

   gNodes.clear();
   ed::DestroyEditor(gEditor);
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();
   glfwDestroyWindow(window);
   glfwTerminate();
   return 0;
}
