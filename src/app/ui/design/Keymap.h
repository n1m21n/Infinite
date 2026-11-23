// Keymap: the one table of keyboard shortcuts (owner 2026-11-24, internal/plans/ui-control-catalogue.md section 7).
// Every shortcut is an Act with one chord. A handler asks Keymap::Pressed(Act::X); who may act (typing in a field,
// a docked panel holding the keys) stays at the call site, so panel shortcuts win over app-wide ones exactly as
// before. Not in the map: text editing inside a field and modal Esc / Enter (they go through the focus helpers).
// Keymap::Text(Act::X) gives the chord as menu text ("⌘C" on macOS, "Ctrl+C" elsewhere).
// Ratchet rule key_checks counts raw IsKeyPressed(ImGuiKey_...) outside this file; it only goes down.
#pragma once
#include <cstdio>
#include "imgui.h"

namespace Keymap
{
   // Which modifiers a chord needs. "Cmd" is Cmd on macOS and Ctrl elsewhere (io.KeyCtrl || io.KeySuper, as the
   // handlers always read it). Alt is ignored unless named.
   enum class Mod : unsigned char
   {
      None,       // no Cmd, no Shift
      Shift,      // Shift without Cmd
      Cmd,        // Cmd without Shift
      CmdShift,   // Cmd and Shift
      CmdAny,     // Cmd, Shift either way
      ShiftOrCmd, // Shift or Cmd (either one)
      NoCmd,      // no Cmd, Shift either way
      Term,       // the terminal chord: physical Cmd on macOS, Ctrl elsewhere, nothing else held
      Any,        // modifiers not checked
   };

   enum class Act : int
   {
      // app-wide (StageKeyboard)
      AddComment, TogglePlay, ShowLock, Panic, ShowNext, ShowPrev,
      ViewportPanel, ModMatrixPanel, MixerPanel, PerfPanel, ArrangePanel, SessionPanel, HistoryPanel, SampleStudioPanel,
      Terminal, PianoRollPanel, DrumPanel, CurveEditorPanel, FieldPatcherPanel, View3DPanel, View2DPanel,
      FitView, NodePicker, ShowParams, AudioEngine, Mute, Delete, DeleteAlt, DeleteShift, Duplicate, Group, Ungroup,
      Bypass, Cut, Copy, Paste,
      Count
   };

   struct Bind
   {
      ImGuiKey key;
      Mod mod;
      const char* area;   // the kShortcuts category in HelpWindows.cpp
      const char* name;   // the action as the help table names it
   };

   inline const Bind& Of(Act a)
   {
      static const Bind kTable[(int)Act::Count] = {
         { ImGuiKey_Slash, Mod::None, "Canvas", "Add Comment" },
         { ImGuiKey_Space, Mod::None, "Transport", "Play / Pause" },
         { ImGuiKey_L, Mod::CmdShift, "Performance", "Show Lock" },
         { ImGuiKey_Period, Mod::Cmd, "Performance", "Panic" },
         { ImGuiKey_PageDown, Mod::NoCmd, "Performance", "Next / Previous Show Patch" },
         { ImGuiKey_PageUp, Mod::NoCmd, "Performance", "Next / Previous Show Patch" },
         { ImGuiKey_V, Mod::Shift, "Panels", "Viewport" },
         { ImGuiKey_M, Mod::Shift, "Panels", "Modulation Matrix" },
         { ImGuiKey_L, Mod::Shift, "Panels", "Mixer" },
         { ImGuiKey_P, Mod::Shift, "Panels", "Performance Matrix" },
         { ImGuiKey_T, Mod::Shift, "Panels", "Timeline" },
         { ImGuiKey_S, Mod::Shift, "Panels", "Session" },
         { ImGuiKey_U, Mod::Shift, "Panels", "History" },
         { ImGuiKey_W, Mod::Shift, "Panels", "Sample Studio" },
         { ImGuiKey_GraveAccent, Mod::Term, "Panels", "Terminal" },
         { ImGuiKey_R, Mod::Shift, "Panels", "Piano Roll" },
         { ImGuiKey_J, Mod::Shift, "Panels", "Drums" },
         { ImGuiKey_E, Mod::Shift, "Panels", "Curve Editor" },
         { ImGuiKey_F, Mod::Shift, "Panels", "Field Patcher" },
         { ImGuiKey_3, Mod::Shift, "Panels", "3D View" },
         { ImGuiKey_2, Mod::Shift, "Panels", "2D View" },
         { ImGuiKey_Y, Mod::Shift, "Canvas", "Fit View" },
         { ImGuiKey_N, Mod::Shift, "Canvas", "Node Picker" },
         { ImGuiKey_H, Mod::Shift, "Canvas", "Show / Hide Params" },
         { ImGuiKey_K, Mod::Shift, "Transport", "Start / Stop Audio" },
         { ImGuiKey_X, Mod::None, "Canvas", "Mute" },
         { ImGuiKey_Delete, Mod::Any, "Canvas", "Delete" },
         { ImGuiKey_Backspace, Mod::Any, "Canvas", "Delete" },
         { ImGuiKey_X, Mod::Shift, "Canvas", "Delete" },
         { ImGuiKey_D, Mod::ShiftOrCmd, "Canvas", "Duplicate" },
         { ImGuiKey_G, Mod::Cmd, "Canvas", "Group" },
         { ImGuiKey_G, Mod::CmdShift, "Canvas", "Ungroup" },
         { ImGuiKey_B, Mod::None, "Canvas", "Bypass" },
         { ImGuiKey_X, Mod::CmdAny, "Canvas", "Cut" },
         { ImGuiKey_C, Mod::CmdAny, "Canvas", "Copy" },
         { ImGuiKey_V, Mod::CmdAny, "Canvas", "Paste" },
      };
      return kTable[(int)a];
   }

   inline bool ModsMatch(Mod m)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const bool cmd = io.KeyCtrl || io.KeySuper;
      switch (m)
      {
      case Mod::None: return !cmd && !io.KeyShift;
      case Mod::Shift: return !cmd && io.KeyShift;
      case Mod::Cmd: return cmd && !io.KeyShift;
      case Mod::CmdShift: return cmd && io.KeyShift;
      case Mod::CmdAny: return cmd;
      case Mod::ShiftOrCmd: return io.KeyShift || cmd;
      case Mod::NoCmd: return !cmd;
#if defined(__APPLE__)
      case Mod::Term: return io.KeySuper && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
#else
      case Mod::Term: return io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
#endif
      case Mod::Any: return true;
      }
      return false;
   }

   // The chord went down this frame (no key repeat unless asked).
   inline bool Pressed(Act a, bool repeat = false)
   {
      const Bind& b = Of(a);
      return ModsMatch(b.mod) && ImGui::IsKeyPressed(b.key, repeat);
   }
   // Just the key, modifiers left to the caller (a handler whose modifier rule is part of its own gating).
   inline bool KeyPressed(Act a, bool repeat = false) { return ImGui::IsKeyPressed(Of(a).key, repeat); }

   // The chord as menu text: "⌘⇧L" on macOS, "Ctrl+Shift+L" elsewhere.
   inline const char* Text(Act a)
   {
      static char buf[(int)Act::Count][32];
      char* s = buf[(int)a];
      const Bind& b = Of(a);
      const char* key = ImGui::GetKeyName(b.key);
      const bool cmd = b.mod == Mod::Cmd || b.mod == Mod::CmdShift || b.mod == Mod::CmdAny || b.mod == Mod::Term;
      const bool shift = b.mod == Mod::Shift || b.mod == Mod::CmdShift;
#if defined(__APPLE__)
      std::snprintf(s, 32, "%s%s%s", cmd ? "\xE2\x8C\x98" : "", shift ? "\xE2\x87\xA7" : "", key);
#else
      std::snprintf(s, 32, "%s%s%s", cmd ? "Ctrl+" : "", shift ? "Shift+" : "", key);
#endif
      return s;
   }
}
