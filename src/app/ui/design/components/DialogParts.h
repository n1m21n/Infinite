// DialogParts: the one modal dialog layout. No title bar; a medium-weight title, dimmed message lines, then a
// right-aligned button row where the last button is the primary (accent) action. Same elevated panel as Settings.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "app/AppShared.h"
#include "core/SplashScreen.h"
#include "core/CategoryColors.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FormParts.h"

namespace DialogParts
{
   constexpr float kButtonMinW = 72.0f;   // buttons hug their label, never stretch
   constexpr float kButtonH = 24.0f;
   constexpr float kMaxW = 480.0f;        // widest a dialog grows; longer text wraps (other languages, bigger fonts)

   // Call after OpenPopup; pair a true result with End().
   inline bool Begin(const char* name)
   {
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 0.0f), ImVec2(kMaxW, 4000.0f));
      app::PushElevatedPanelStyle(false);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_4, tok::space_4));
      const bool open = ImGui::BeginPopupModal(name, nullptr,
         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
      ImGui::PopStyleVar();
      if (!open)
         app::PopElevatedPanelStyle();
      return open;
   }

   inline void Title(const char* text)
   {
      UiType::Scope s(UiType::Size::Title, UiType::Weight::Medium);
      ImGui::PushTextWrapPos(kMaxW - tok::space_4);
      ImGui::TextUnformatted(text);
      ImGui::PopTextWrapPos();
      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
   }

   inline void Message(const char* text)
   {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::PushTextWrapPos(kMaxW - tok::space_4);
      ImGui::TextUnformatted(text);
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
   }

   // The Infinite mark as a progress meter, drawn the way the launch screen draws it: the lemniscate strokes itself
   // in the brand gradient over a faint track, and a bead rides its head. `progress` < 0 (size unknown) draws the
   // whole mark with the bead looping it, as the launch screen does once it is drawn.
   inline void Progress(const char* what, float progress)
   {
      constexpr float kTwoPi = 6.28318530718f;
      const float A = 58.0f, sw = 0.257f * A * 0.62f;       // half-width of the mark, stroke
      const float h = A * 0.42f;                             // half-height of a lemniscate of half-width A
      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float rowH = h * 2.0f + sw + 6.0f;
      const ImVec2 c(p0.x + A + sw * 0.5f + 2.0f, p0.y + rowH * 0.5f);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const bool still = CategoryColors::GetReduceMotion();
      const float known = progress >= 0.0f ? std::min(progress, 1.0f) : 1.0f;
      constexpr int kSeg = 160;
      auto at = [&](float u) { const ImVec2 q = Splash::Lemniscate(u * kTwoPi); return ImVec2(c.x + q.x * A, c.y + q.y * A); };
      // track: the whole mark, quiet
      dl->PathClear();
      for (int i = 0; i <= kSeg; ++i)
         dl->PathLineTo(at((float)i / kSeg));
      dl->PathStroke(ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.10f)), ImDrawFlags_None, sw);
      // stroke: the drawn part, coloured by x as on the logo
      if (known > 0.0f)
      {
         // One triangle strip with a 1-unit antialiased fringe, as the launch screen draws it.
         const int samples = std::max(2, (int)std::ceil(known * (float)kSeg) + 1);
         const float hw = sw * 0.5f, aa = 1.0f;
         const ImVec2 uv = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
         dl->PrimReserve((samples - 1) * 18, samples * 4);
         const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
         for (int i = 0; i < samples; ++i)
         {
            const float p = kTwoPi * known * (float)i / (float)(samples - 1);
            const ImVec2 q0 = Splash::Lemniscate(p - 0.0005f), q1 = Splash::Lemniscate(p + 0.0005f), q = Splash::Lemniscate(p);
            float tx = q1.x - q0.x, ty = q1.y - q0.y;
            const float tl = std::sqrt(tx * tx + ty * ty);
            tx = tl > 0.0f ? tx / tl : 1.0f;
            ty = tl > 0.0f ? ty / tl : 0.0f;
            const float nx = -ty, ny = tx;
            const ImVec2 pc(c.x + q.x * A, c.y + q.y * A);
            const ImU32 on = Splash::BrandColor((q.x + 1.0f) * 0.5f, 1.0f);
            const ImU32 off = on & ~IM_COL32_A_MASK;
            dl->PrimWriteVtx(ImVec2(pc.x + nx * (hw + aa), pc.y + ny * (hw + aa)), uv, off);
            dl->PrimWriteVtx(ImVec2(pc.x + nx * hw, pc.y + ny * hw), uv, on);
            dl->PrimWriteVtx(ImVec2(pc.x - nx * hw, pc.y - ny * hw), uv, on);
            dl->PrimWriteVtx(ImVec2(pc.x - nx * (hw + aa), pc.y - ny * (hw + aa)), uv, off);
         }
         for (int i = 0; i + 1 < samples; ++i)
         {
            const ImDrawIdx r0 = (ImDrawIdx)(base + i * 4), r1 = (ImDrawIdx)(base + (i + 1) * 4);
            for (int k = 0; k < 3; ++k)
            {
               dl->PrimWriteIdx((ImDrawIdx)(r0 + k));
               dl->PrimWriteIdx((ImDrawIdx)(r1 + k));
               dl->PrimWriteIdx((ImDrawIdx)(r1 + k + 1));
               dl->PrimWriteIdx((ImDrawIdx)(r0 + k));
               dl->PrimWriteIdx((ImDrawIdx)(r1 + k + 1));
               dl->PrimWriteIdx((ImDrawIdx)(r0 + k + 1));
            }
         }
      }
      dl->AddCircleFilled(at(known), sw * 0.5f, Splash::BrandColor((at(known).x - c.x) / A * 0.5f + 0.5f, 1.0f), 24);
      // bead: rides the head while the size is known, loops the mark when it is not
      float bu = known;
      if (progress < 0.0f && !still)
      {
         const float u = std::fmod((float)ImGui::GetTime() / 4.0f, 1.0f);
         bu = u + 0.035f * std::sin(u * 3.14159265f * 4.0f);
         bu -= std::floor(bu);
      }
      const ImVec2 bp = at(bu);
      const float r = sw * 0.42f;
      dl->AddCircleFilled(bp, r, IM_COL32(0xC2, 0x59, 0x3F, 255), 24);
      dl->AddCircleFilled(ImVec2(bp.x - r * 0.18f, bp.y - r * 0.2f), r * 0.78f, IM_COL32(0xF9, 0xA5, 0x8F, 255), 24);
      dl->AddCircleFilled(ImVec2(bp.x - r * 0.34f, bp.y - r * 0.38f), r * 0.28f, IM_COL32(255, 255, 255, 230), 16);
      // words beside the mark
      const float tx = p0.x + A * 2.0f + sw + tok::space_4;
      const float lh = ImGui::GetTextLineHeight();
      if (progress >= 0.0f)
      {
         char pct[16];
         snprintf(pct, sizeof(pct), "%d%%", (int)(known * 100.0f + 0.5f));
         dl->AddText(ImVec2(tx, c.y - lh - 1.0f), ImGui::GetColorU32(ImGuiCol_Text), pct);
         dl->AddText(ImVec2(tx, c.y + 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), what);
      }
      else
         dl->AddText(ImVec2(tx, c.y - lh * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), what);
      ImGui::Dummy(ImVec2(A * 2.0f + sw + tok::space_4 + 110.0f, rowH));
   }

   // Right-aligned row; returns the clicked index or -1. The last label is the primary action.
   inline int Buttons(std::initializer_list<const char*> labels)
   {
      ImGui::Dummy(ImVec2(0.0f, tok::space_2));
      const int n = (int)labels.size();
      // Labels may carry a "##id" suffix (L()); measure only what is shown.
      const auto widthOf = [](const char* l)
      {
         const char* end = std::strstr(l, "##");
         return std::max(kButtonMinW, ImGui::CalcTextSize(l, end).x + 2.0f * tok::space_3);
      };
      float total = tok::space_2 * (n - 1);
      for (const char* l : labels)
         total += widthOf(l);
      const float avail = ImGui::GetContentRegionAvail().x;
      if (avail > total)
         ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - total);
      int clicked = -1, i = 0;
      for (const char* l : labels)
      {
         if (i > 0)
            ImGui::SameLine(0.0f, tok::space_2);
         if (ChipButton::Draw(l, i == n - 1, kButtonH, widthOf(l)))
            clicked = i;
         ++i;
      }
      return clicked;
   }

   inline void End()
   {
      // A modal floats over a same-coloured canvas, so its edge is stronger than a docked surface's hairline.
      {
         const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
         const ImVec4 e = CategoryColors::IsThemeLight() ? ImVec4(0, 0, 0, 0.16f) : ImVec4(1, 1, 1, 0.08f);
         ImGui::GetWindowDrawList()->AddRect(ImVec2(wp.x + 0.5f, wp.y + 0.5f), ImVec2(wp.x + ws.x - 0.5f, wp.y + ws.y - 0.5f),
                                             ImGui::GetColorU32(e), 12.0f);
      }
      ImGui::EndPopup();
      app::PopElevatedPanelStyle();
   }
}
