// fork: Inspector panel - the params a node shows behind its eye, drawn in a side panel for the selected node.
// See docs/plans/ux-roadmap.md (stage 3) and docs/fork-changes.md. Own header so AppShared.h stays untouched.
#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include <cstdint>
#include <ostream>
#include <string>

struct GraphNode;
struct GLFWwindow;
namespace app
{

// Panel state (saved in Infinite.workspace-settings, keys "inspector*").
extern bool  gInspectorOpen;
extern int   gInspectorDock;      // 1 right (default), 2 left, kDockFloating (4) floating
extern float gInspectorWidth;
extern bool  gParamsInInspector;  // Settings: params in the inspector (true) or in the node (classic)
// True only while the inspector draws a params body: pin wrappers below skip every ed:: call.
extern bool  gInspectorDrawing;

constexpr float kInspectorMinWidth = 260.0f;

// Polls the canvas selection (call inside ed::Begin, before node bodies).
void InspectorFollowSelection();
// The inspector draws this node's params this frame; the canvas must then run its register-only pass.
bool InspectorOwnsParams(const GraphNode& gn);
// Settings row (Canvas & Workspace).
void InspectorSettingsRow();

void DrawInspectorDocked(const char* id, const ImVec2& size);
void DrawInspectorFloating();

void InspectorSaveSettings(std::ostream& file);
bool InspectorLoadSetting(const std::string& key, const std::string& val);
uint64_t InspectorLayoutHash();

void InspectorSelfTestFrame(GLFWwindow* window, int frameId);

// Top bar group: show all params / hide all params / inspector toggle (tiles on the centre line).
void DrawTopBarParamsTools(float centreY);

// Shared params dispatcher (moved out of DrawNodeBodies).
void DrawNodeParamsBody(GraphNode& gn);

// Pin wrappers: no-ops while the inspector draws (ed::BeginPin is only legal inside the canvas).
namespace insp
{
   inline void BeginPin(ax::NodeEditor::PinId id, ax::NodeEditor::PinKind kind)
   {
      if (!gInspectorDrawing)
         ax::NodeEditor::BeginPin(id, kind);
   }
   inline void EndPin()
   {
      if (!gInspectorDrawing)
         ax::NodeEditor::EndPin();
   }
   inline void PinPivotAlignment(const ImVec2& a)
   {
      if (!gInspectorDrawing)
         ax::NodeEditor::PinPivotAlignment(a);
   }
   inline void PinRect(const ImVec2& a, const ImVec2& b)
   {
      if (!gInspectorDrawing)
         ax::NodeEditor::PinRect(a, b);
   }
}
}
