// Scrollbar: the one scrollbar look (owner 2026-11-24, ui-control-catalogue.md section 4). A flat track and a quiet
// pill tinted from the surface's ink that deepens on hover and drag. The theme applies it to every window; a
// surface with its own ink (a comment note on the canvas) pushes the same look with that ink.
#pragma once
#include "imgui.h"
#include "app/ui/design/Tokens.gen.h"

namespace Scrollbar
{
   // Grab alpha over the ink: at rest, hovered, dragged.
   inline constexpr float kRest = tok::scrollbar_rest;
   inline constexpr float kHover = tok::scrollbar_hover;
   inline constexpr float kDrag = tok::scrollbar_drag;

   inline ImVec4 Ink(const ImVec4& ink, float a) { return ImVec4(ink.x, ink.y, ink.z, a); }

   // The theme's style (Theme.cpp): track in the window colour.
   inline void Apply(ImGuiStyle& style, const ImVec4& track, const ImVec4& ink)
   {
      style.Colors[ImGuiCol_ScrollbarBg] = track;
      style.Colors[ImGuiCol_ScrollbarGrab] = Ink(ink, kRest);
      style.Colors[ImGuiCol_ScrollbarGrabHovered] = Ink(ink, kHover);
      style.Colors[ImGuiCol_ScrollbarGrabActive] = Ink(ink, kDrag);
   }

   // The same look on a surface of its own: transparent track, pill from `ink`, `size` wide.
   // Pushes 4 colours and 1 style var; Pop() takes them off (or count them in the caller's own pops).
   inline void Push(const ImVec4& ink, float size)
   {
      ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Ink(ink, kRest));
      ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, Ink(ink, kHover));
      ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, Ink(ink, kDrag));
      ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, size);
   }
   // Black ink on a light surface, white on a dark one.
   inline void Push(bool blackInk, float size) { Push(blackInk ? ImVec4(0, 0, 0, 1) : ImVec4(1, 1, 1, 1), size); }
   inline void Pop()
   {
      ImGui::PopStyleVar(1);
      ImGui::PopStyleColor(4);
   }
}
