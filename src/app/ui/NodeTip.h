#pragma once
// fork: one entry point for diagnostic tooltips (always shown, unlike HelpTip which the Tooltips setting gates).
// Inside a node body (gInsideNodeCanvas) the text is parked in gNodeHoverTip and drawn after the node loop, outside
// the editor's canvas transform; elsewhere it is drawn at once. Never call ImGui::SetTooltip / BeginTooltip from code
// that can run between ed::BeginNode and ed::EndNode - use this or HelpTip.
namespace app { void NodeTip(const char* fmt, ...); }
