// NoticeCard: the body of one notice (Notices::Notice): glyph (error/warning only), what happened, a close glyph,
// what is safe and what to do next, and one optional action. Drawn inside the floating window NoticeStack opens.
#pragma once
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/GlyphToggle.h"
#include "core/Notices.h"

namespace NoticeCard
{
   enum class Result { None, Dismiss, Action };

   inline Result Draw(const Notices::Notice& n, float width)
   {
      Result result = Result::None;
      const bool light = CategoryColors::IsThemeLight();
      const bool err = n.level == Notices::Level::Error;
      const bool warn = n.level == Notices::Level::Warning;
      if (err || warn)
      {
         const ImU32 col = tok::U32(err ? tok::action_record : tok::action_learn, light);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         const float h = ImGui::GetTextLineHeight();
         glyph::Draw(ImGui::GetWindowDrawList(), ImVec2(p.x + 7.0f, p.y + h * 0.5f), 14.0f, col,
                     err ? IconsInfinite::Error : IconsInfinite::Warning);
         ImGui::Dummy(ImVec2(14.0f, h));
         ImGui::SameLine(0.0f, tok::space_2);
      }
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 3.0f * tok::space_4);
      ImGui::TextUnformatted(n.title.c_str());
      ImGui::PopTextWrapPos();
      ImGui::SameLine(width - tok::space_4 - 14.0f);
      if (GlyphToggle::Draw("##dismiss", IconsInfinite::Close, IconsInfinite::Close, false, 18.0f))
         result = Result::Dismiss;
      if (!n.detail.empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 2.0f * tok::space_4);
         ImGui::TextDisabled("%s", n.detail.c_str());
         ImGui::PopTextWrapPos();
      }
      if (!n.actionLabel.empty() && n.action)
      {
         if (ActionButton::Draw(n.actionLabel.c_str(), ImVec2(0, 0)))
            result = Result::Action;
      }
      return result;
   }
}
