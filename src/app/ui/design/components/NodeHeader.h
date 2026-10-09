#pragma once
#include <cstring>
#include "imgui.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/Glyphs.gen.h"
#include "app/ui/design/Tokens.gen.h"

// Node header: title row carries the category to the right of the title, dimmed, led by its family glyph.
namespace NodeHeader
{
   constexpr float kFamilyGlyph = 14.0f;

   // One glyph per node category (G12 decision: families, not one per node type). nullptr = none known.
   inline const char* FamilyGlyph(const char* category)
   {
      struct Row { const char* name; const char* glyph; };
      static const Row rows[] = {
         { "Source", IconsInfinite::FamilySource },         { "3D", IconsInfinite::Family3d },
         { "Compositing", IconsInfinite::FamilyCompositing }, { "Effects", IconsInfinite::FamilyEffects },
         { "Modulators", IconsInfinite::FamilyModulators },   { "Prediction", IconsInfinite::FamilyPrediction },
         { "Macros", IconsInfinite::FamilyMacros },           { "Utility", IconsInfinite::FamilyUtility },
         { "Notes", IconsInfinite::FamilyNotes },             { "Synths", IconsInfinite::FamilySynths },
         { "Synthesizers", IconsInfinite::FamilySynths },     { "AudioEffects", IconsInfinite::FamilyAudioEffects },
      };
      for (const Row& r : rows)
         if (std::strcmp(r.name, category) == 0) return r.glyph;
      return nullptr;
   }

   inline void Category(const char* text, const ImVec4& col)
   {
      ImGui::SameLine(0.0f, tok::space_2);
      if (const char* g = FamilyGlyph(text))
      {
         const ImVec2 p = ImGui::GetCursorScreenPos();
         const float h = ImGui::GetTextLineHeight();
         glyph::Draw(ImGui::GetWindowDrawList(), ImVec2(p.x + kFamilyGlyph * 0.5f, p.y + h * 0.5f), kFamilyGlyph,
                     ImGui::GetColorU32(col), g);
         ImGui::Dummy(ImVec2(kFamilyGlyph, h));
         ImGui::SameLine(0.0f, tok::space_1);
      }
      ImGui::PushStyleColor(ImGuiCol_Text, col);
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
   }
}
