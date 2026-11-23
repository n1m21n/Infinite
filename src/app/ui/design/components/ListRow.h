// ListRow: the one list row and table (owner 2026-11-24, ui-control-catalogue.md section 4). Selectable rows,
// tables and list boxes go through here so row height, hover and the selected wash have one home.
// Today each is the call it replaced, argument for argument (EndTable / EndListBox stay ImGui's).
#pragma once
#include <utility>
#include "imgui.h"

namespace ListRow
{
   template <class... A> inline bool Selectable(A&&... a) { return ImGui::Selectable(std::forward<A>(a)...); }
   template <class... A> inline bool BeginTable(A&&... a) { return ImGui::BeginTable(std::forward<A>(a)...); }
   template <class... A> inline bool BeginListBox(A&&... a) { return ImGui::BeginListBox(std::forward<A>(a)...); }
}
