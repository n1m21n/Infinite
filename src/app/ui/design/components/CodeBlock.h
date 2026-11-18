// CodeBlock: a recessed well holding code or a command, in Geist Mono, with a Copy glyph at its top right.
// The text wraps before the glyph, so a long line never pushes the page sideways. `id` must be unique on the page.
#pragma once
#include <cstdio>
#include <string>
#include "imgui.h"
#include "app/AppShared.h"
#include "app/ui/NodeTip.h"
#include "app/ui/design/Glyphs.gen.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/GlyphToggle.h"

namespace CodeBlock
{
   inline void Draw(const char* code, const char* id)
   {
      const float copyW = 22.0f;
      const ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(tx.x, tx.y, tx.z, 0.06f));
      ImGui::PushStyleColor(ImGuiCol_Border, FormParts::EdgeCol());
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, tok::radius_tile);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
      ImGui::PushFont(app::TerminalMonoFont(), 0.0f);  // nullptr keeps the current face when the install lacks it
      const float outer = ImGui::GetContentRegionAvail().x;
      const float textW = std::max(40.0f, outer - 2.0f * tok::space_2 - copyW - tok::space_2);
      const float textH = ImGui::CalcTextSize(code, nullptr, false, textW).y;
      const float h = std::max(textH, 18.0f) + 2.0f * tok::space_2;
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
      ImGui::BeginChild(id, ImVec2(0, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
      ImGui::PopStyleVar();
      ImGui::SetCursorPosY(std::max(tok::space_2, (h - textH) * 0.5f));  // one line sits centred beside the glyph
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
      ImGui::TextUnformatted(code);
      ImGui::PopTextWrapPos();
      ImGui::PopFont();  // the glyph lives in the UI face's merged icon range, not in the mono face
      ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - copyW - tok::space_1, tok::space_2 + (std::min(textH, 18.0f) - 18.0f) * 0.5f));
      static ImGuiID sCopied = 0;
      static double sCopiedAt = -10.0;
      const std::string glyphId = std::string("##copy") + id;
      if (GlyphToggle::Draw(glyphId.c_str(), IconsInfinite::Copy, IconsInfinite::Copy, false))
      {
         ImGui::SetClipboardText(code);
         sCopied = ImGui::GetItemID();
         sCopiedAt = ImGui::GetTime();
      }
      const char* tip = nullptr;
      if (ImGui::IsItemHovered())
         tip = sCopied == ImGui::GetItemID() && ImGui::GetTime() - sCopiedAt < 1.5 ? I18n::T("Copied") : I18n::T("Copy");
      ImGui::EndChild();
      if (tip != nullptr)
         app::NodeTip("%s", tip);
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);
      ImGui::Spacing();
   }
}
