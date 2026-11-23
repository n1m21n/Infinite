// Keymap: the one table of keyboard shortcuts (owner 2026-11-24, internal/plans/ui-control-catalogue.md section 7).
// Every shortcut is an Act with one chord. A handler asks Keymap::Pressed(Act::X); who may act (typing in a field,
// a docked panel holding the keys) stays at the call site, so panel shortcuts win over app-wide ones exactly as
// before. Not in the map: text editing inside a field and modal Esc / Enter (they go through the focus helpers).
// Keymap::Text(Act::X) gives the chord as menu text ("⌘C" on macOS, "Ctrl+C" elsewhere).
// Ratchet rule key_checks counts raw IsKeyPressed(ImGuiKey_...) outside this file; it only goes down.
// Panel call sites moved by tools/design/keymap_migrate.py use KeyPressed and keep their own modifier gating.
#pragma once
#include <cstdio>
#include "imgui.h"
#include "imgui_internal.h" // IsKeyPressed with an owner (NextField)

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
      Plain,      // no Cmd, no Shift, no Alt
      ShiftPlain, // Shift, no Cmd, no Alt
      CmdPlain,   // Cmd, no Shift, no Alt
      AltNoCmd,   // Alt without Cmd
      NoCmdNoAlt, // no Cmd, no Alt, Shift either way
      CmdXorShift,// Cmd or Shift but not both, no Alt
      Ctrl,       // the physical Ctrl key (Ctrl+Y redo)
      ChordCmd,   // ImGui's exact chord ImGuiMod_Ctrl + key (nothing else held)
   };

   enum class Act : int
   {
      // app-wide (StageKeyboard)
      AddComment, TogglePlay, ShowLock, Panic, ShowNext, ShowPrev,
      ViewportPanel, ModMatrixPanel, MixerPanel, PerfPanel, ArrangePanel, SessionPanel, HistoryPanel, SampleStudioPanel,
      Terminal, PianoRollPanel, DrumPanel, CurveEditorPanel, FieldPatcherPanel, View3DPanel, View2DPanel,
      FitView, NodePicker, ShowParams, AudioEngine, Mute, Delete, DeleteAlt, DeleteShift, Duplicate, Group, Ungroup,
      Bypass, Cut, Copy, Paste,
      // shared by the canvas and the panels (each panel gates its own call, so the focused panel wins)
      Undo, Redo, RedoAlt, SelectAll, SelectAllCmd, Split, Rename, FitViewF, ZoomIn, ZoomInPad, ZoomOut, ZoomOutPad,
      DeleteX, ToolSelect, ToolBlade, ToolHand,
      // app (StagePresent, FindOverlay, Musical Typing, Chat)
      Settings, UiScaleUp, UiScaleDown, Save, Open, New, Find, OctaveDown, OctaveUp, CopyText,
      // canvas keyboard focus (StageLinks)
      NodeHelp, UngroupU,
      // Timeline (ArrangePanel)
      TlAddVideoTrack, TlAddAudioTrack, TlToggleEnabled, TlToggleEnabledPad, TlMarker, TlToolTrim, TlToolRange,
      TlToolZoom, TlToolPencil, TlLoop, TlLoopIn, TlLoopOut, TlPrevMarker, TlNextMarker, TlNudgeLeft, TlNudgeRight,
      TlStart, TlEnd,
      // Sample Studio
      SsZoomSelection, SsPrevSlice, SsNextSlice, SsJoin, SsAudition, SsMute, SsSolo, SsReverse, SsRecord,
      // Curve Editor
      CeSnap, CeQuantise, CeNext, CeInterp1, CeInterp2, CeInterp3, CeInterp4, CeInterp5,
      Count
   };

   struct Bind
   {
      Act act;
      ImGuiKey key;
      Mod mod;
      const char* area;   // the kShortcuts category in HelpWindows.cpp
      const char* name;   // the action as the help table names it
   };

   inline const Bind& Of(Act a)
   {
      static const Bind kRows[] = {
         { Act::AddComment, ImGuiKey_Slash, Mod::None, "Canvas", "Add Comment" },
         { Act::TogglePlay, ImGuiKey_Space, Mod::None, "Transport", "Play / Pause" },
         { Act::ShowLock, ImGuiKey_L, Mod::CmdShift, "Performance", "Show Lock" },
         { Act::Panic, ImGuiKey_Period, Mod::Cmd, "Performance", "Panic" },
         { Act::ShowNext, ImGuiKey_PageDown, Mod::NoCmd, "Performance", "Next / Previous Show Patch" },
         { Act::ShowPrev, ImGuiKey_PageUp, Mod::NoCmd, "Performance", "Next / Previous Show Patch" },
         { Act::ViewportPanel, ImGuiKey_V, Mod::Shift, "Panels", "Viewport" },
         { Act::ModMatrixPanel, ImGuiKey_M, Mod::Shift, "Panels", "Modulation Matrix" },
         { Act::MixerPanel, ImGuiKey_L, Mod::Shift, "Panels", "Mixer" },
         { Act::PerfPanel, ImGuiKey_P, Mod::Shift, "Panels", "Performance Matrix" },
         { Act::ArrangePanel, ImGuiKey_T, Mod::Shift, "Panels", "Timeline" },
         { Act::SessionPanel, ImGuiKey_S, Mod::Shift, "Panels", "Session" },
         { Act::HistoryPanel, ImGuiKey_U, Mod::Shift, "Panels", "History" },
         { Act::SampleStudioPanel, ImGuiKey_W, Mod::Shift, "Panels", "Sample Studio" },
         { Act::Terminal, ImGuiKey_GraveAccent, Mod::Term, "Panels", "Terminal" },
         { Act::PianoRollPanel, ImGuiKey_R, Mod::Shift, "Panels", "Piano Roll" },
         { Act::DrumPanel, ImGuiKey_J, Mod::Shift, "Panels", "Drums" },
         { Act::CurveEditorPanel, ImGuiKey_E, Mod::Shift, "Panels", "Curve Editor" },
         { Act::FieldPatcherPanel, ImGuiKey_F, Mod::Shift, "Panels", "Field Patcher" },
         { Act::View3DPanel, ImGuiKey_3, Mod::Shift, "Panels", "3D View" },
         { Act::View2DPanel, ImGuiKey_2, Mod::Shift, "Panels", "2D View" },
         { Act::FitView, ImGuiKey_Y, Mod::Shift, "Canvas", "Fit View" },
         { Act::NodePicker, ImGuiKey_N, Mod::Shift, "Canvas", "Node Picker" },
         { Act::ShowParams, ImGuiKey_H, Mod::Shift, "Canvas", "Show / Hide Params" },
         { Act::AudioEngine, ImGuiKey_K, Mod::Shift, "Transport", "Start / Stop Audio" },
         { Act::Mute, ImGuiKey_X, Mod::None, "Canvas", "Mute" },
         { Act::Delete, ImGuiKey_Delete, Mod::Any, "Canvas", "Delete" },
         { Act::DeleteAlt, ImGuiKey_Backspace, Mod::Any, "Canvas", "Delete" },
         { Act::DeleteShift, ImGuiKey_X, Mod::Shift, "Canvas", "Delete" },
         { Act::Duplicate, ImGuiKey_D, Mod::ShiftOrCmd, "Canvas", "Duplicate" },
         { Act::Group, ImGuiKey_G, Mod::Cmd, "Canvas", "Group" },
         { Act::Ungroup, ImGuiKey_G, Mod::CmdShift, "Canvas", "Ungroup" },
         { Act::Bypass, ImGuiKey_B, Mod::None, "Canvas", "Bypass" },
         { Act::Cut, ImGuiKey_X, Mod::CmdAny, "Canvas", "Cut" },
         { Act::Copy, ImGuiKey_C, Mod::CmdAny, "Canvas", "Copy" },
         { Act::Paste, ImGuiKey_V, Mod::CmdAny, "Canvas", "Paste" },
         { Act::Undo, ImGuiKey_Z, Mod::Cmd, "App", "Undo" },
         { Act::Redo, ImGuiKey_Z, Mod::CmdShift, "App", "Redo" },
         { Act::RedoAlt, ImGuiKey_Y, Mod::Ctrl, "App", "Redo" },
         { Act::SelectAll, ImGuiKey_A, Mod::ShiftOrCmd, "Canvas", "Select All" },
         { Act::SelectAllCmd, ImGuiKey_A, Mod::CmdAny, "App", "Select All" },
         { Act::Split, ImGuiKey_E, Mod::CmdAny, "Timeline", "Split" },
         { Act::Rename, ImGuiKey_R, Mod::Cmd, "App", "Rename" },
         { Act::FitViewF, ImGuiKey_F, Mod::Plain, "Canvas", "Fit View" },
         { Act::ZoomIn, ImGuiKey_Equal, Mod::Plain, "App", "Zoom In" },
         { Act::ZoomInPad, ImGuiKey_KeypadAdd, Mod::None, "App", "Zoom In" },
         { Act::ZoomOut, ImGuiKey_Minus, Mod::Plain, "App", "Zoom Out" },
         { Act::ZoomOutPad, ImGuiKey_KeypadSubtract, Mod::None, "App", "Zoom Out" },
         { Act::DeleteX, ImGuiKey_X, Mod::NoCmd, "3D View", "Delete" },
         { Act::ToolSelect, ImGuiKey_A, Mod::Plain, "Timeline", "Select Tool" },
         { Act::ToolBlade, ImGuiKey_B, Mod::Plain, "Timeline", "Blade Tool" },
         { Act::ToolHand, ImGuiKey_H, Mod::Plain, "Timeline", "Hand Tool" },
         { Act::Settings, ImGuiKey_0, Mod::Cmd, "App", "Settings" },
         { Act::UiScaleUp, ImGuiKey_Equal, Mod::Cmd, "App", "UI Scale Up" },
         { Act::UiScaleDown, ImGuiKey_Minus, Mod::Cmd, "App", "UI Scale Down" },
         { Act::Save, ImGuiKey_S, Mod::CmdAny, "File", "Save / Save As" },
         { Act::Open, ImGuiKey_O, Mod::Cmd, "File", "Open" },
         { Act::New, ImGuiKey_N, Mod::Cmd, "File", "New" },
         { Act::Find, ImGuiKey_F, Mod::CmdPlain, "Canvas", "Find" },
         { Act::OctaveDown, ImGuiKey_Minus, Mod::None, "Musical Typing", "Octave Down" },
         { Act::OctaveUp, ImGuiKey_Equal, Mod::None, "Musical Typing", "Octave Up" },
         { Act::CopyText, ImGuiKey_C, Mod::ChordCmd, "Chat", "Copy" },
         { Act::NodeHelp, ImGuiKey_H, Mod::Plain, "Canvas", "Node Help" },
         { Act::UngroupU, ImGuiKey_U, Mod::CmdPlain, "Canvas", "Ungroup" },
         { Act::TlAddVideoTrack, ImGuiKey_J, Mod::ShiftPlain, "Timeline", "Add Video Track" },
         { Act::TlAddAudioTrack, ImGuiKey_K, Mod::ShiftPlain, "Timeline", "Add Audio Track" },
         { Act::TlToggleEnabled, ImGuiKey_0, Mod::NoCmdNoAlt, "Timeline", "Enable / Disable" },
         { Act::TlToggleEnabledPad, ImGuiKey_Keypad0, Mod::NoCmdNoAlt, "Timeline", "Enable / Disable" },
         { Act::TlMarker, ImGuiKey_M, Mod::Plain, "Timeline", "Add Marker" },
         { Act::TlToolTrim, ImGuiKey_T, Mod::Plain, "Timeline", "Trim Tool" },
         { Act::TlToolRange, ImGuiKey_R, Mod::Plain, "Timeline", "Range Tool" },
         { Act::TlToolZoom, ImGuiKey_Z, Mod::Plain, "Timeline", "Zoom Tool" },
         { Act::TlToolPencil, ImGuiKey_P, Mod::Plain, "Timeline", "Pencil Tool" },
         { Act::TlLoop, ImGuiKey_L, Mod::CmdPlain, "Timeline", "Loop On / Off" },
         { Act::TlLoopIn, ImGuiKey_I, Mod::Plain, "Timeline", "Loop Start" },
         { Act::TlLoopOut, ImGuiKey_O, Mod::Plain, "Timeline", "Loop End" },
         { Act::TlPrevMarker, ImGuiKey_LeftArrow, Mod::AltNoCmd, "Timeline", "Previous Marker" },
         { Act::TlNextMarker, ImGuiKey_RightArrow, Mod::AltNoCmd, "Timeline", "Next Marker" },
         { Act::TlNudgeLeft, ImGuiKey_LeftArrow, Mod::Plain, "Timeline", "Nudge Left" },
         { Act::TlNudgeRight, ImGuiKey_RightArrow, Mod::Plain, "Timeline", "Nudge Right" },
         { Act::TlStart, ImGuiKey_Home, Mod::Plain, "Timeline", "Go to Start" },
         { Act::TlEnd, ImGuiKey_End, Mod::Plain, "Timeline", "Go to End" },
         { Act::SsZoomSelection, ImGuiKey_Z, Mod::NoCmd, "Sample Studio", "Zoom to Selection" },
         { Act::SsPrevSlice, ImGuiKey_LeftArrow, Mod::NoCmd, "Sample Studio", "Previous Slice" },
         { Act::SsNextSlice, ImGuiKey_RightArrow, Mod::NoCmd, "Sample Studio", "Next Slice" },
         { Act::SsJoin, ImGuiKey_J, Mod::Plain, "Sample Studio", "Join" },
         { Act::SsAudition, ImGuiKey_Q, Mod::Plain, "Sample Studio", "Audition" },
         { Act::SsMute, ImGuiKey_M, Mod::Plain, "Sample Studio", "Mute" },
         { Act::SsSolo, ImGuiKey_S, Mod::Plain, "Sample Studio", "Solo" },
         { Act::SsReverse, ImGuiKey_V, Mod::Plain, "Sample Studio", "Reverse" },
         { Act::SsRecord, ImGuiKey_R, Mod::Plain, "Sample Studio", "Record" },
         { Act::CeSnap, ImGuiKey_G, Mod::Plain, "Curve Editor", "Snap" },
         { Act::CeQuantise, ImGuiKey_Q, Mod::Plain, "Curve Editor", "Quantise" },
         { Act::CeNext, ImGuiKey_N, Mod::Plain, "Curve Editor", "Next Preset / Side Panel" },
         { Act::CeInterp1, ImGuiKey_1, Mod::NoCmdNoAlt, "Curve Editor", "Interpolation 1" },
         { Act::CeInterp2, ImGuiKey_2, Mod::NoCmdNoAlt, "Curve Editor", "Interpolation 2" },
         { Act::CeInterp3, ImGuiKey_3, Mod::NoCmdNoAlt, "Curve Editor", "Interpolation 3" },
         { Act::CeInterp4, ImGuiKey_4, Mod::NoCmdNoAlt, "Curve Editor", "Interpolation 4" },
         { Act::CeInterp5, ImGuiKey_5, Mod::NoCmdNoAlt, "Curve Editor", "Interpolation 5" },
      };
      static_assert(sizeof kRows / sizeof kRows[0] == (size_t)Act::Count, "Keymap: one row per Act");
      // Rows can be in any order: look each Act up once.
      struct Index
      {
         const Bind* at[(int)Act::Count] = {};
         Index()
         {
            for (const Bind& b : kRows)
               at[(int)b.act] = &b;
         }
      };
      static const Index kIndex;
      return *kIndex.at[(int)a];
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
      case Mod::Plain: return !cmd && !io.KeyShift && !io.KeyAlt;
      case Mod::ShiftPlain: return !cmd && io.KeyShift && !io.KeyAlt;
      case Mod::CmdPlain: return cmd && !io.KeyShift && !io.KeyAlt;
      case Mod::AltNoCmd: return io.KeyAlt && !cmd;
      case Mod::NoCmdNoAlt: return !cmd && !io.KeyAlt;
      case Mod::CmdXorShift: return (cmd ? !io.KeyShift : io.KeyShift) && !io.KeyAlt;
      case Mod::Ctrl: return io.KeyCtrl;
      case Mod::ChordCmd: return true; // Pressed() asks ImGui for the exact chord
      }
      return false;
   }

   // The chord went down this frame (no key repeat unless asked).
   inline bool Pressed(Act a, bool repeat = false)
   {
      const Bind& b = Of(a);
      if (b.mod == Mod::ChordCmd)
         return ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | b.key); // no repeat, as ImGui's chord check
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
      const bool cmd = b.mod == Mod::Cmd || b.mod == Mod::CmdShift || b.mod == Mod::CmdAny || b.mod == Mod::Term ||
                       b.mod == Mod::CmdPlain || b.mod == Mod::ChordCmd;
      const bool shift = b.mod == Mod::Shift || b.mod == Mod::CmdShift || b.mod == Mod::ShiftPlain;
      const bool alt = b.mod == Mod::AltNoCmd;
      const bool ctrl = b.mod == Mod::Ctrl;
#if defined(__APPLE__)
      std::snprintf(s, 32, "%s%s%s%s%s", ctrl ? "\xE2\x8C\x83" : "", alt ? "\xE2\x8C\xA5" : "", cmd ? "\xE2\x8C\x98" : "",
                    shift ? "\xE2\x87\xA7" : "", key);
#else
      std::snprintf(s, 32, "%s%s%s%s", (cmd || ctrl) ? "Ctrl+" : "", alt ? "Alt+" : "", shift ? "Shift+" : "", key);
#endif
      return s;
   }

   // Focus helpers: the keys that move focus or close something, outside the table (owner 2026-11-24): modal
   // Esc / Enter, list arrows, Tab between fields. One place, so every dialog and list reads them the same way.
   // repeat defaults to ImGui's own default (true), as the call sites relied on.
   inline bool Cancel(bool repeat = true) { return ImGui::IsKeyPressed(ImGuiKey_Escape, repeat); }
   inline bool Enter(bool repeat = true) { return ImGui::IsKeyPressed(ImGuiKey_Enter, repeat); }
   // Enter on either keyboard (main or keypad).
   inline bool Confirm(bool repeat = true) { return Enter(repeat) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, repeat); }
   inline bool Up(bool repeat = true) { return ImGui::IsKeyPressed(ImGuiKey_UpArrow, repeat); }
   inline bool Down(bool repeat = true) { return ImGui::IsKeyPressed(ImGuiKey_DownArrow, repeat); }
   inline bool Left(bool repeat = true) { return ImGui::IsKeyPressed(ImGuiKey_LeftArrow, repeat); }
   inline bool Right(bool repeat = true) { return ImGui::IsKeyPressed(ImGuiKey_RightArrow, repeat); }
   // Tab to the next field, with the caller's key owner (so a panel can claim Tab from ImGui's own navigation).
   inline bool NextField(ImGuiInputFlags flags, ImGuiID owner) { return ImGui::IsKeyPressed(ImGuiKey_Tab, flags, owner); }
}
