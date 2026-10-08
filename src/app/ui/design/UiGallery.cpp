#include "app/ui/design/UiGallery.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/UiLayout.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/Divider.h"
#include "app/ui/design/components/IconTile.h"
#include "app/ui/design/components/PillGroup.h"
#include "app/ui/design/components/Readout.h"
#include "app/ui/design/components/TextButton.h"

namespace UiGallery
{
   namespace
   {
      void Heading(UiLayout::Rect r, const char* text)
      {
         UiType::Scope ts(UiType::Size::Caption, UiType::Weight::Semibold);
         ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         c.w *= 0.6f;
         ImGui::GetWindowDrawList()->AddText(ImVec2(r.x, r.y), ImGui::GetColorU32(c), text);
      }
   }

   void Draw()
   {
      const ImVec2 top(0.0f, ImGui::GetFrameHeight() + 8.0f);   // below the menu bar
      const ImVec2 ds(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y - top.y);
      ImGui::SetNextWindowPos(top);
      ImGui::SetNextWindowSize(ds);
      ImGui::SetNextWindowBgAlpha(1.0f);
      ImGui::SetNextWindowFocus();  // above the canvas and top bar
      ImGui::Begin("##uigallery", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoMove);
      ImGui::PushID("gallery");
      const float pad = tok::space_4 * 2.0f, rowH = 28.0f, gap = tok::space_3;
      const UiLayout::Rect page = UiLayout::Rect{ top.x, top.y, ds.x, ds.y }.Inset(pad);
      // Sections stacked in one column; each is a heading line then a row of samples.
      const std::vector<UiLayout::Rect> sec = UiLayout::Column(page, { UiLayout::Fixed(14), UiLayout::Fixed(rowH), UiLayout::Fixed(14), UiLayout::Fixed(rowH), UiLayout::Fixed(14), UiLayout::Fixed(rowH),
                                                   UiLayout::Fixed(14), UiLayout::Fixed(rowH), UiLayout::Fixed(14), UiLayout::Fixed(rowH), UiLayout::Fixed(14), UiLayout::Fixed(100) },
                                           gap);

      Heading(sec[0], "TEXT BUTTON - quiet, primary, disabled");
      {
         const float w1 = TextButton::WidthFor("Export"), w2 = TextButton::WidthFor("Save patch");
         const auto c = UiLayout::Row(sec[1], { UiLayout::Fixed(w1), UiLayout::Fixed(w2), UiLayout::Fixed(w1) }, gap, UiLayout::Align::Center, rowH);
         TextButton::Draw("g.tb.quiet", c[0], "Export");
         TextButton::Draw("g.tb.primary", c[1], "Save patch", TextButton::Kind::Primary);
         TextButton::Draw("g.tb.off", c[2], "Export", TextButton::Kind::Quiet, false);
      }
      Heading(sec[2], "ICON TILE - off, on");
      {
         const auto c = UiLayout::Row(sec[3], { UiLayout::Fixed(28), UiLayout::Fixed(28), UiLayout::Fixed(28), UiLayout::Fixed(28) }, gap);
         ImGui::SetCursorScreenPos(ImVec2(c[0].x, c[0].y));
         IconTile::Draw("g.it.cube", IconsInfinite::Cube, nullptr, false, 28.0f, rowH);
         ImGui::SetCursorScreenPos(ImVec2(c[1].x, c[1].y));
         IconTile::Draw("g.it.cube.on", IconsInfinite::Cube, nullptr, true, 28.0f, rowH);
         ImGui::SetCursorScreenPos(ImVec2(c[2].x, c[2].y));
         IconTile::Draw("g.it.view", IconsInfinite::Viewport, IconsInfinite::ViewportFill, false, 28.0f, rowH);
         ImGui::SetCursorScreenPos(ImVec2(c[3].x, c[3].y));
         IconTile::Draw("g.it.view.on", IconsInfinite::Viewport, IconsInfinite::ViewportFill, true, 28.0f, rowH);
      }
      Heading(sec[4], "PILL GROUP - first, middle, last selected");
      {
         static const PillGroup::Segment segs[] = { { "g.pg.a", "Live" }, { "g.pg.b", "Arrange" }, { "g.pg.c", "Mix" } };
         static const PillGroup::Segment segs2[] = { { "g.pg2.a", "Live" }, { "g.pg2.b", "Arrange" }, { "g.pg2.c", "Mix" } };
         static const PillGroup::Segment segs3[] = { { "g.pg3.a", "Live" }, { "g.pg3.b", "Arrange" }, { "g.pg3.c", "Mix" } };
         const auto c = UiLayout::Row(sec[5], { UiLayout::Fixed(240), UiLayout::Fixed(240), UiLayout::Fixed(240) }, gap);
         PillGroup::Draw("g.pg1", c[0], segs, 3, 0);
         PillGroup::Draw("g.pg2", c[1], segs2, 3, 1);
         PillGroup::Draw("g.pg3", c[2], segs3, 3, 2);
      }
      Heading(sec[6], "READOUT - tabular digits (same width for 1111 and 0000)");
      {
         const auto c = UiLayout::Row(sec[7], { UiLayout::Fixed(90), UiLayout::Fixed(90), UiLayout::Fixed(90), UiLayout::Fixed(90) }, gap);
         Readout::Draw("g.ro.a", c[0], "1111", "A", UiType::Size::Title, UiType::Weight::Medium, Readout::Align::Left);
         Readout::Draw("g.ro.b", c[1], "0000", "B", UiType::Size::Title, UiType::Weight::Medium, Readout::Align::Left);
         Readout::Draw("g.ro.c", c[2], "120.0", "BPM", UiType::Size::Title, UiType::Weight::Medium, Readout::Align::Left);
         Readout::Draw("g.ro.d", c[3], "-12.5 dB", "Gain", UiType::Size::Title, UiType::Weight::Medium, Readout::Align::Left);
      }
      Heading(sec[8], "DIVIDER - vertical, horizontal");
      {
         const auto c = UiLayout::Row(sec[9], { UiLayout::Fixed(16), UiLayout::Fixed(160) }, gap);
         Divider::Vertical(c[0]);
         Divider::Horizontal(c[1]);
      }
      Heading(sec[10], "TYPE - caption, body, title, display x regular, medium, semibold");
      {
         const auto rows = UiLayout::Column(sec[11], { UiLayout::Fixed(14), UiLayout::Fixed(18), UiLayout::Fixed(22), UiLayout::Fixed(34) }, 2.0f);
         const UiType::Size sizes[] = { UiType::Size::Caption, UiType::Size::Body, UiType::Size::Title, UiType::Size::Display };
         const char* names[] = { "Caption", "Body", "Title", "Display" };
         for (int i = 0; i < 4; ++i)
         {
            const auto cells = UiLayout::Row(rows[static_cast<size_t>(i)], { UiLayout::Fixed(220), UiLayout::Fixed(220), UiLayout::Fixed(220) }, gap);
            const UiType::Weight ws[] = { UiType::Weight::Regular, UiType::Weight::Medium, UiType::Weight::Semibold };
            for (int w = 0; w < 3; ++w)
            {
               UiType::Scope ts(sizes[i], ws[w]);
               ImGui::GetWindowDrawList()->AddText(ImVec2(cells[static_cast<size_t>(w)].x, cells[static_cast<size_t>(w)].y),
                                                   ImGui::GetColorU32(ImGuiCol_Text), names[i]);
            }
         }
      }
      ImGui::PopID();
      ImGui::End();
   }
}
