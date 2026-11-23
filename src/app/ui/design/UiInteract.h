// Interaction model (plan L4): one way for a component to claim a rect and get hover/press/focus/click
// state, with an ID derived from a stable key (never from translated text), and a per-frame semantic tree
// (role, label, value, rect) that a future VoiceOver/Narrator/Orca bridge reads (plan L11).
#pragma once
#include "app/ui/design/UiLayout.h"
#include "imgui.h"
#include <string>
#include <vector>

namespace UiInteract
{
   enum class Role { Button, Toggle, Slider, Knob, Dropdown, Tab, Readout, Group, Label };

   struct State
   {
      bool hovered = false;
      bool down = false;      // held
      bool clicked = false;   // released over the item this frame
      bool focused = false;   // keyboard focus
      ImGuiID id = 0;
   };

   struct Node
   {
      ImGuiID id;
      std::string key;        // stable, language-independent
      std::string label;      // spoken text, may be translated
      std::string value;      // current value as text, empty if none
      Role role;
      bool enabled;
      UiLayout::Rect rect;
   };

   // Claims `rect` (screen coords), records a semantic node, returns the interaction state.
   // `key` must be stable across languages and sessions (e.g. "topbar.play"); `label` is for people.
   State Item(const char* key, const UiLayout::Rect& rect, Role role, const char* label,
              bool enabled = true, const char* value = nullptr);

   // Records a semantic node for something drawn but not interactive (a readout, a label).
   void Note(const char* key, const UiLayout::Rect& rect, Role role, const char* label, const char* value = nullptr);

   // The ID Item() uses for `key` in the current ID stack; for UiAnim slots that belong to the item.
   ImGuiID IdFor(const char* key);

   // Call right after ImGui::NewFrame(): the finished frame's tree becomes Tree(), a new one starts.
   void BeginFrame();
   const std::vector<Node>& Tree();               // last completed frame
   const Node* Find(const char* key);             // in the last completed frame
   int DuplicateKeys();                           // keys claimed twice in one window frame (a bug)

   // The one invisible hit area (owner 2026-11-24, ui-control-catalogue.md section 4): every custom-drawn control
   // claims its rect through here, then reads hover / press / drag from ImGui's item state as before.
   inline bool Hit(const char* id, const ImVec2& size, ImGuiButtonFlags flags = 0) { return ImGui::InvisibleButton(id, size, flags); }

   // Keyboard focus: the next item (or `offset` items on) takes the keys; whether a window has them.
   inline void FocusHere(int offset = 0) { ImGui::SetKeyboardFocusHere(offset); }
   inline bool WindowFocused(ImGuiFocusedFlags flags = 0) { return ImGui::IsWindowFocused(flags); }
}
