// UiDensity: how big an inspector's parts are (owner, 2026-11-17). Settings panels are one narrow vertical strip at
// Photoshop / Affinity Properties size: text, rows, fields, chips and checks all step down toward the 16 pt icon,
// and the padding stays, scaled with them. A panel opts in with `const UiDensity::Compact c;` at the top of its
// settings child; every shared part (SectionCard, PanelKit, ChipButton, CheckBox, UiType) reads M() instead of a
// fixed token, so the whole strip changes size in one place. Nodes and canvases keep the regular size.
#pragma once
#include "imgui.h"
#include "app/ui/design/Tokens.gen.h"

namespace UiDensity
{
   struct Metrics
   {
      float text;      // body text (captions one smaller)
      float row;       // a control: field, slider, chip, check row
      float gap;       // between rows
      float label;     // the label column left of a field
      float pad;       // card edge to its content
      float check;     // checkbox square
   };
   constexpr Metrics kRegular { tok::type_body, tok::control_h, tok::row_gap, tok::label_w_regular, tok::space_2, tok::check_box };
   // Compact sizes live in tokens.json "compact" (tok::compact_*): change them there, never here.
   constexpr Metrics kCompactMetrics { tok::compact_text, tok::compact_row, tok::compact_gap, tok::compact_label,
                                       tok::compact_pad, tok::compact_check };

   // Every docked settings strip is this wide (Photoshop's Properties panel is ~240 px with its scrollbar).
   constexpr float kInspectorW = tok::compact_inspector_w;
   // A More.. popover is one compact strip as wide as the docked settings strip.
   constexpr float kPopoverW = tok::compact_popover_w;

   inline const Metrics*& Cur()
   {
      static const Metrics* m = &kRegular;
      return m;
   }
   inline const Metrics& M() { return *Cur(); }
   inline bool IsCompact() { return Cur() == &kCompactMetrics; }

   struct Compact
   {
      const Metrics* keep;
      Compact()
      {
         keep = Cur();
         Cur() = &kCompactMetrics;
         const Metrics& m = M();
         ImGui::PushFont(nullptr, m.text);
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::compact_frame_pad_x, (m.row - m.text) * 0.5f));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(tok::compact_item_gap_x, m.gap));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(tok::compact_item_gap_x, tok::compact_item_gap_x));
      }
      // Pops in the same ImGui window that pushed: call End() before an EndChild that closes
      // the child this scope was opened in (ImGui aborts on a push left open across EndChild).
      void End()
      {
         if (ended)
            return;
         ended = true;
         ImGui::PopStyleVar(3);
         ImGui::PopFont();
         Cur() = keep;
      }
      ~Compact() { End(); }
      bool ended = false;
      Compact(const Compact&) = delete;
      Compact& operator=(const Compact&) = delete;
   };
}
