// Wire: the one cable look on a node canvas (owner 2026-11-24, ui-control-catalogue.md section 4). Every cable on
// the patch canvas and in the Field Patcher is drawn through here: one thickness (tok::stroke_wire), the colour
// chosen by the caller from the cable's role (pin type, modulation, selection).
#pragma once
#include "app/ui/design/Tokens.gen.h"
#include "imgui.h"
#include "imgui_node_editor.h"

namespace Wire
{
   inline bool Draw(ax::NodeEditor::LinkId id, ax::NodeEditor::PinId from, ax::NodeEditor::PinId to, const ImVec4& col)
   {
      return ax::NodeEditor::Link(id, from, to, col, tok::stroke_wire);
   }
}
