// CodeBlock: a recessed well holding code or a command, with a Copy button on its right. The text wraps
// before the button, so a long line never pushes the page sideways. `id` must be unique on the page.
#pragma once
#include <cstdio>
#include "imgui.h"
#include "app/ui/design/components/FormParts.h"

namespace CodeBlock
{
   inline void Draw(const char* code, const char* id)
   {
      const float copyW = 56.0f;
      const ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(tx.x, tx.y, tx.z, 0.06f));
      ImGui::PushStyleColor(ImGuiCol_Border, FormParts::EdgeCol());
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, tok::radius_tile);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
      const float outer = ImGui::GetContentRegionAvail().x;
      const float textW = std::max(40.0f, outer - 2.0f * tok::space_2 - copyW - tok::space_2);
      const float h = std::max(ImGui::CalcTextSize(code, nullptr, false, textW).y, FormParts::kRowH) + 2.0f * tok::space_2;
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
      ImGui::BeginChild(id, ImVec2(0, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
      ImGui::PopStyleVar();
      const float textH = ImGui::CalcTextSize(code, nullptr, false, textW).y;
      ImGui::SetCursorPosY(std::max(tok::space_2, (h - textH) * 0.5f));   // one line sits centred beside the button
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
      ImGui::TextUnformatted(code);
      ImGui::PopTextWrapPos();
      ImGui::SameLine(ImGui::GetWindowWidth() - copyW - tok::space_2);
      ImGui::SetCursorPosY(tok::space_2);
      char label[96];
      std::snprintf(label, sizeof(label), "Copy##%s", id);
      if (FormParts::Button(label, ImVec2(copyW, 0)))
         ImGui::SetClipboardText(code);
      ImGui::EndChild();
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);
      ImGui::Spacing();
   }
}
