// fork: Inspector panel - see InspectorPanel.h and docs/fork-changes.md.
//
// One owner per frame: when the inspector shows a node whose eye is closed, the canvas runs that node's params
// body in register-only mode (it registers every ParamRef inside the canvas, exactly like a collapsed node with a
// binding), and the inspector runs the full, visible pass. The inspector's own registrations are then dropped
// (TruncateFrameParams / TruncateFrameColors), so modulation sees each param once, from the canvas, no matter
// where the panel is docked (a left dock draws before ClearFrameParams, a right dock after node bodies).
#include "app/panels/InspectorPanel.h"
#include "app/AppShared.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/IconTile.h"
#include "app/ui/design/components/TopBarParts.h"
#include "imgui_internal.h"

#include <algorithm>
#include <vector>

namespace app
{
bool  gInspectorOpen = false;
int   gInspectorDock = 1;
float gInspectorWidth = 360.0f;
bool  gParamsInInspector = true;
bool  gInspectorDrawing = false;

namespace
{
   std::vector<uint64_t> sSelected;   // canvas selection, gNodes order
   uint64_t sPinned = 0;              // 0 = follow selection
   uint64_t sShown = 0;               // the node whose params the inspector draws this frame
   // Counters the canvas had for sShown right before its params dispatcher (the preview area may draw
   // widgets first), so the inspector's pass gets the same param ordinals.
   int sCounterParam = 0, sCounterColor = 0, sCounterDiscrete = 0;
   bool sCountersValid = false;
   int sDrewNode = -1;    // self-test: last node index the inspector drew params for
   int sDrewFrames = 0;

   GraphNode* NodeByUid(uint64_t uid)
   {
      if (uid == 0)
         return nullptr;
      for (GraphNode& gn : gNodes)
         if (gn.uid == uid)
            return &gn;
      return nullptr;
   }

   bool IsMacro(INode* n)
   {
      return dynamic_cast<MacroKnobNode*>(n) || dynamic_cast<MacroSliderNode*>(n) ||
             dynamic_cast<MacroBipolarKnobNode*>(n) || dynamic_cast<MacroXYNode*>(n) ||
             dynamic_cast<MacroToggleNode*>(n) || dynamic_cast<MacroTriggerNode*>(n) ||
             dynamic_cast<MacroNumBoxNode*>(n) || dynamic_cast<MacroRadioSelectorNode*>(n) ||
             dynamic_cast<MacroStepGateNode*>(n);
   }

   // Nodes whose params body cannot leave the canvas in this version: macros draw their control in the preview
   // area, GeometryTable declares its row output pins inside the params grid, comments/groups have no params.
   bool CanTakeParams(const GraphNode& gn)
   {
      INode* n = gn.node.get();
      if (n == nullptr || IsAudioBodyNode(n) || IsMacro(n))
         return false;
      return dynamic_cast<CommentNode*>(n) == nullptr && dynamic_cast<GeometryTableNode*>(n) == nullptr &&
             dynamic_cast<GroupNode*>(n) == nullptr;
   }

   void SelectOnCanvas(GraphNode& gn)
   {
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      ed::SelectNode(gn.NodeId(), false);
      ed::SetCurrentEditor(prev);
   }

   void Hint(const char* text)
   {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", text);
      ImGui::PopTextWrapPos();
   }

   // The node's params, full visible pass, outside the canvas.
   void DrawParamsHere(GraphNode& gn, bool audio)
   {
      Modulation& mod = Modulation::Instance();
      const size_t paramsBefore = mod.FrameParams().size();
      const size_t colorsBefore = PaletteBinding::Instance().FrameColors().size();
      const int savedNode = gCurrentNodeIndex;
      const bool savedInside = gInsideNodeCanvas;
      const float savedWidth = gParamWidthLive;

      gInsideNodeCanvas = false;
      gInspectorDrawing = true;
      gParamWidthLive = std::max(kParamWidthBase, ImGui::GetContentRegionAvail().x - 24.0f);
      BeginNodeParams(gn.index);
      if (!audio && sCountersValid)
      {
         gParamCounter = sCounterParam;
         gColorCounter = sCounterColor;
         gDiscreteParamCounter = sCounterDiscrete;
      }
      sDrewNode = gn.index;
      sDrewFrames++;
      ImGui::PushID((int)gn.uid);
      if (audio)
         DrawAudioNodeBody(gn);
      else
         DrawNodeParamsBody(gn);
      ImGui::PopID();
      EndNodeParams();

      gParamWidthLive = savedWidth;
      gInsideNodeCanvas = savedInside;
      gInspectorDrawing = false;
      gCurrentNodeIndex = savedNode;
      mod.TruncateFrameParams(paramsBefore);
      PaletteBinding::Instance().TruncateFrameColors(colorsBefore);
   }

   void DrawContent()
   {
      GraphNode* gn = NodeByUid(sShown);
      // Header: title + pin.
      if (gn != nullptr)
         ImGui::TextUnformatted(NodeTitleWithInstance(*gn).c_str());
      else
         ImGui::TextUnformatted(T("Inspector"));
      {
         const char* pinLabel = sPinned != 0 ? T("Unpin") : T("Pin");
         const float w = ImGui::CalcTextSize(pinLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
         ImGui::SameLine(std::max(0.0f, ImGui::GetContentRegionMax().x - w));
         ImGui::BeginDisabled(sPinned == 0 && gn == nullptr);
         if (ImGui::SmallButton((std::string(pinLabel) + "##inspin").c_str()))
            sPinned = sPinned != 0 ? 0 : (gn != nullptr ? gn->uid : 0);
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", T("Keep showing this node while selecting others"));
      }
      ImGui::Separator();
      ImGui::BeginChild("##inspbody", ImVec2(0, 0), false);

      if (gn == nullptr)
      {
         if (sSelected.size() > 1)
         {
            ImGui::TextDisabled("%s (%d)", T("Selected nodes"), (int)sSelected.size());
            for (uint64_t uid : sSelected)
               if (GraphNode* s = NodeByUid(uid))
               {
                  ImGui::PushID((int)uid);
                  if (ImGui::Selectable(NodeTitleWithInstance(*s).c_str()))
                     SelectOnCanvas(*s);
                  ImGui::PopID();
               }
         }
         else
            Hint(T("Select a node to see its parameters."));
      }
      else if (IsAudioBodyNode(gn->node.get()))
      {
         DrawParamsHere(*gn, true);
      }
      else if (!CanTakeParams(*gn))
         Hint(T("This node's controls stay on the node."));
      else if (!gParamsInInspector)
      {
         Hint(T("Classic mode: parameters open in the node (eye). Change in Settings > Canvas & Workspace."));
      }
      else if (gn->showParams)
      {
         Hint(T("Parameters are open in the node."));
         if (ImGui::Button(T("Collapse and show here")))
            gn->showParams = false;
      }
      else if (!sCountersValid)
         Hint(T("..."));   // the canvas has not drawn this node yet this frame (left dock, first frame)
      else
         DrawParamsHere(*gn, false);

      ImGui::EndChild();
   }
}

void InspectorFollowSelection()
{
   sSelected.clear();
   for (GraphNode& gn : gNodes)
      if (ed::IsNodeSelected(gn.NodeId()))
         sSelected.push_back(gn.uid);
   if (sPinned != 0 && NodeByUid(sPinned) == nullptr)
      sPinned = 0;
   const uint64_t shown = sPinned != 0 ? sPinned : (sSelected.size() == 1 ? sSelected.front() : 0);
   if (shown != sShown)
      sCountersValid = false;
   sShown = shown;
}

bool InspectorOwnsParams(const GraphNode& gn)
{
   if (!gInspectorOpen || !gParamsInInspector || gn.uid != sShown || gn.showParams || !CanTakeParams(gn))
      return false;
   // Called by the canvas right before its params dispatcher: remember the ordinals it starts from.
   sCounterParam = gParamCounter;
   sCounterColor = gColorCounter;
   sCounterDiscrete = gDiscreteParamCounter;
   sCountersValid = true;
   return true;
}

void InspectorSettingsRow()
{
   static const char* kModes[] = { I18N_KEY("In the inspector"), I18N_KEY("In the node (classic)") };
   int mode = gParamsInInspector ? 0 : 1;
   ImGui::TextUnformatted(T("Node parameters"));
   for (int i = 0; i < 2; i++)
   {
      ImGui::SameLine();
      if (ImGui::RadioButton(L(kModes[i]), mode == i) && mode != i)
      {
         gParamsInInspector = (i == 0);
         SaveWorkspaceSettings();
      }
   }
}

void DrawInspectorDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const bool right = gInspectorDock != 2;
   ImGui::BeginChild(id, size, false);
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]()
   {
      ImGui::InvisibleButton("##inspgrip", ImVec2(kGrip, std::max(1.0f, inner.y)));
      DrawPanelSeam(true, right);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      if (ImGui::IsItemActive())
      {
         const float dx = ImGui::GetIO().MouseDelta.x;
         gInspectorWidth = std::max(kInspectorMinWidth, gInspectorWidth + (right ? -dx : dx));
      }
   };
   if (right)
   {
      grip();
      ImGui::SameLine();
   }
   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   PanelFrame::Insets in;
   if (right)
      in.l = 0.0f;
   else
      in.r = 0.0f;
   PushDockedPanelStyle(/*isChild=*/true);
   PanelFrame::BeginCard("##inspcard", ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), inner.y), in);
   PopDockedPanelStyle();
   DrawContent();
   PanelFrame::EndCard();

   const ImVec2 mouse = ImGui::GetIO().MousePos;
   const ImVec2 o = ImGui::GetWindowPos();
   if (mouse.x >= o.x && mouse.x < o.x + size.x && mouse.y >= o.y && mouse.y < o.y + 24.0f &&
       ImGui::IsMouseReleased(ImGuiMouseButton_Right))
      ImGui::OpenPopup("##inspctx");
   if (MenuParts::BeginPopup("##inspctx"))
   {
      if (MenuParts::Item(L("Right"), nullptr, gInspectorDock == 1)) gInspectorDock = 1;
      if (MenuParts::Item(L("Left"), nullptr, gInspectorDock == 2)) gInspectorDock = 2;
      if (MenuParts::Item(L("Floating Window"), nullptr, gInspectorDock == kDockFloating)) gInspectorDock = kDockFloating;
      MenuParts::Separator();
      if (MenuParts::Item(L("Close panel")))
         gInspectorOpen = false;
      MenuParts::EndPopup();
   }

   if (!right)
   {
      ImGui::SameLine();
      grip();
   }
   const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
   ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
   ImGui::EndChild();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}

void DrawInspectorFloating()
{
   bool open = true;
   if (BeginFloatingPanel((std::string(T("Inspector")) + "###inspectorfloat").c_str(), &open, ImVec2(380.0f, 520.0f)))
      DrawContent();
   EndFloatingPanel();
   if (!open)
      gInspectorOpen = false;
}

void InspectorSaveSettings(std::ostream& file)
{
   file << "inspectorOpen=" << (gInspectorOpen ? 1 : 0) << "\n";
   file << "inspectorDock=" << gInspectorDock << "\n";
   file << "inspectorW=" << gInspectorWidth << "\n";
   file << "inspectorParams=" << (gParamsInInspector ? 1 : 0) << "\n";
}

bool InspectorLoadSetting(const std::string& key, const std::string& val)
{
   if (key == "inspectorOpen")
      gInspectorOpen = (val != "0");
   else if (key == "inspectorDock")
   {
      const int d = atoi(val.c_str());
      gInspectorDock = (d == 2 || d == kDockFloating) ? d : 1;
   }
   else if (key == "inspectorW")
      gInspectorWidth = std::max(kInspectorMinWidth, std::strtof(val.c_str(), nullptr));
   else if (key == "inspectorParams")
      gParamsInInspector = (val != "0");
   else
      return false;
   return true;
}

uint64_t InspectorLayoutHash()
{
   return (uint64_t)(gInspectorOpen ? 1 : 0) | ((uint64_t)gInspectorDock << 1) | ((uint64_t)gInspectorWidth << 8) |
          ((uint64_t)(gParamsInInspector ? 1 : 0) << 40);
}

// INFINITE_INSPECTORTEST: LFO -> blur while the blur's params live in the inspector (node collapsed),
// then Curves, a 3D node and the LFO itself through the same path. Checks: the inspector really draws the body,
// every param is registered exactly once per frame, and the modulated value moves.
void InspectorSelfTestFrame(GLFWwindow* window, int frameId)
{
   static const bool sOn = getenv("INFINITE_INSPECTORTEST") != nullptr;
   if (!sOn)
      return;
   static int lfo = -1, blur = -1, curves = -1, sphere = -1;
   static uint64_t uLfo = 0, uBlur = 0, uCurves = 0, uSphere = 0;
   static int blurParam = -1;
   static float vMin = 1e9f, vMax = -1e9f;
   static bool ok = true;
   Modulation& mod = Modulation::Instance();
   auto dupes = [&](int node) {
      std::vector<int> seen;
      int d = 0;
      for (const ParamRef& r : mod.FrameParams())
         if (r.nodeIndex == node)
         {
            if (std::find(seen.begin(), seen.end(), r.paramIndex) != seen.end())
               d++;
            seen.push_back(r.paramIndex);
         }
      return std::make_pair((int)seen.size() - d, d);
   };
   auto check = [&](const char* what, bool c) {
      printf("INSPECTORTEST %s: %s\n", what, c ? "OK" : "FAIL");
      ok = ok && c;
   };
   auto idxOf = [](uint64_t u) { GraphNode* g = NodeByUid(u); return g ? g->index : -1; };
   if (frameId > 2 && uLfo != 0)
   {
      lfo = idxOf(uLfo); blur = idxOf(uBlur); curves = idxOf(uCurves); sphere = idxOf(uSphere);
   }
   auto pinAndReset = [&](uint64_t uid) { sPinned = uid; sDrewFrames = 0; sDrewNode = -1; };
   if (frameId == 2)
   {
      gInspectorOpen = true;
      gInspectorDock = getenv("INFINITE_INSPECTORDOCK") ? atoi(getenv("INFINITE_INSPECTORDOCK")) : 1;
      gParamsInInspector = true;
      uLfo = SpawnNode("LFO", "Modulators", 40.0f, 420.0f)->uid;
      uBlur = SpawnNode("gaussianblur", "Effects", 320.0f, 420.0f)->uid;
      uCurves = SpawnNode("Curves", "Compositing", 600.0f, 420.0f)->uid;
      uSphere = SpawnNode("Sphere", "3D", 880.0f, 420.0f)->uid;
      for (uint64_t u : { uLfo, uBlur, uCurves, uSphere })
         if (GraphNode* g = NodeByUid(u))
            g->showParams = false;
      pinAndReset(uBlur);
   }
   if (frameId == 8)
   {
      const auto [n, d] = dupes(blur);
      for (const ParamRef& r : mod.FrameParams())
         if (r.nodeIndex == blur && !r.isEnum && !r.isBool && r.value != nullptr)
         {
            blurParam = r.paramIndex;
            break;
         }
      check("blur: inspector draws the body", sDrewNode == blur && sDrewFrames > 0);
      check("blur: params registered once (canvas register-only pass)", n > 0 && d == 0);
      printf("INSPECTORTEST blur params=%d dupes=%d param=%d drew=%d/%d idx lfo=%d blur=%d curves=%d sphere=%d\n", n, d,
             blurParam, sDrewNode, sDrewFrames, lfo, blur, curves, sphere);
      if (blurParam >= 0)
         mod.Bind(blur, blurParam, lfo);
   }
   if (frameId > 10 && frameId < 60 && blurParam >= 0)
      for (const ParamRef& r : mod.FrameParams())
         if (r.nodeIndex == blur && r.paramIndex == blurParam && r.value != nullptr)
         {
            vMin = std::min(vMin, *r.value);
            vMax = std::max(vMax, *r.value);
         }
   if (frameId == 60)
   {
      const auto [n, d] = dupes(blur);
      printf("INSPECTORTEST blur value range %.4f..%.4f\n", vMin, vMax);
      check("LFO -> blur moves the value while params are in the inspector", vMax - vMin > 1e-4f);
      check("blur still drawn by inspector, no dupes", sDrewNode == blur && d == 0 && n > 0);
      pinAndReset(uCurves);
   }
   if (frameId == 66)
   {
      const auto [n, d] = dupes(curves);
      check("curves: inspector draws, registered once", sDrewNode == curves && sDrewFrames > 0 && d == 0);
      pinAndReset(uSphere);
   }
   if (frameId == 72)
   {
      const auto [n, d] = dupes(sphere);
      check("sphere (3D): inspector draws, registered once", sDrewNode == sphere && sDrewFrames > 0 && d == 0 && n > 0);
      pinAndReset(uLfo);
   }
   if (frameId == 78)
   {
      const auto [n, d] = dupes(lfo);
      check("lfo: inspector draws, registered once", sDrewNode == lfo && sDrewFrames > 0 && d == 0 && n > 0);
      // Eye open: the node owns the params again, the inspector only shows the hint.
      if (GraphNode* g = NodeByUid(uLfo))
         g->showParams = true;
      sDrewFrames = 0;
   }
   if (frameId == 84)
   {
      const auto [n, d] = dupes(lfo);
      check("eye open: node draws, inspector does not, registered once", sDrewFrames == 0 && d == 0 && n > 0);
      printf("INSPECTORTEST %s\n", ok ? "OK" : "FAIL");
      if (getenv("INFINITE_INSPECTORKEEP") == nullptr)
         glfwSetWindowShouldClose(window, GLFW_TRUE);
   }
}

void DrawTopBarParamsTools(float centreY)
{
   int shown = 0, total = 0;
   for (const GraphNode& gn : gNodes)
   {
      ++total;
      shown += gn.showParams ? 1 : 0;
   }
   const bool allShown = total > 0 && shown == total;
   const bool allHidden = shown == 0;
   TopBarParts::SectionBreak();
   ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, centreY - tok::tile * 0.5f));
   if (IconTile::Draw("##tbShowParams", IconsInfinite::Eye, IconsInfinite::Eye, allShown, tok::tile, 0.0f, total > 0))
      for (GraphNode& gn : gNodes)
         gn.showParams = true;
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      HelpTip(T("Show all params (%d of %d open)"), shown, total);
   ImGui::SameLine(0.0f, 4.0f);
   if (IconTile::Draw("##tbHideParams", IconsInfinite::EyeOff, IconsInfinite::EyeOff, total > 0 && allHidden, tok::tile,
                      0.0f, total > 0))
      for (GraphNode& gn : gNodes)
         gn.showParams = false;
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      HelpTip(T("Hide all params (%d of %d open)"), shown, total);
   ImGui::SameLine(0.0f, 4.0f);
   if (IconTile::Draw("##tbInspector", IconsInfinite::Edit, IconsInfinite::Edit, gInspectorOpen, tok::tile))
      gInspectorOpen = !gInspectorOpen;
   if (ImGui::IsItemHovered())
      HelpTip("%s", T("Inspector - parameters of the selected node (Shift+I)"));
}
}
