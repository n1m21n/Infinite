// Thumb: every texture drawn into the UI (previews, thumbnails, the viewport image) goes through here (owner
// 2026-11-24, ui-control-catalogue.md section 4), so their rounding, border and empty look have one home.
// Today each is the call it replaced, argument for argument.
#pragma once
#include <utility>
#include "imgui.h"

namespace Thumb
{
   template <class... A> inline void Image(A&&... a) { ImGui::Image(std::forward<A>(a)...); }
   template <class... A> inline void Add(ImDrawList* dl, A&&... a) { dl->AddImage(std::forward<A>(a)...); }
   template <class... A> inline void AddRounded(ImDrawList* dl, A&&... a) { dl->AddImageRounded(std::forward<A>(a)...); }
   template <class... A> inline void AddQuad(ImDrawList* dl, A&&... a) { dl->AddImageQuad(std::forward<A>(a)...); }
}
