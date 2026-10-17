// Part of src/main.cpp, included there in order (one translation unit).
// Turbo 0.59: small vector icons for the top bar, the Set list and block
// headers. Drawn with the ImDrawList at any size, in the current text colour
// by default, so they follow the theme and the interface scale.

   enum class UiIcon
   {
      Play, Pause, Rewind, Power, Chat, Perf, Panel, Cpu, Audio, Gpu, Vram, Fps, SetList, Next, Prev, Import
   };

   void DrawUiIcon(ImDrawList* dl, UiIcon icon, ImVec2 c, float s, ImU32 col)
   {
      const float h = s * 0.5f;                     // half extent
      const float th = std::max(1.2f, s * 0.085f);  // stroke
      switch (icon)
      {
         case UiIcon::Play:
            dl->AddTriangleFilled(ImVec2(c.x - h * 0.55f, c.y - h * 0.75f), ImVec2(c.x - h * 0.55f, c.y + h * 0.75f),
                                  ImVec2(c.x + h * 0.75f, c.y), col);
            break;
         case UiIcon::Pause:
            dl->AddRectFilled(ImVec2(c.x - h * 0.6f, c.y - h * 0.7f), ImVec2(c.x - h * 0.15f, c.y + h * 0.7f), col, 1.0f);
            dl->AddRectFilled(ImVec2(c.x + h * 0.15f, c.y - h * 0.7f), ImVec2(c.x + h * 0.6f, c.y + h * 0.7f), col, 1.0f);
            break;
         case UiIcon::Rewind:
            dl->AddRectFilled(ImVec2(c.x - h * 0.75f, c.y - h * 0.65f), ImVec2(c.x - h * 0.5f, c.y + h * 0.65f), col);
            dl->AddTriangleFilled(ImVec2(c.x + h * 0.7f, c.y - h * 0.65f), ImVec2(c.x + h * 0.7f, c.y + h * 0.65f),
                                  ImVec2(c.x - h * 0.4f, c.y), col);
            break;
         case UiIcon::Power:
            // Ring open at the top, with the stem through the gap.
            dl->PathArcTo(c, h * 0.62f, -3.14159f * 0.5f + 0.75f, -3.14159f * 0.5f + 6.2832f - 0.75f, 24);
            dl->PathStroke(col, 0, th);
            dl->AddLine(ImVec2(c.x, c.y - h * 0.82f), ImVec2(c.x, c.y - h * 0.05f), col, th);
            break;
         case UiIcon::Chat:
         {
            // Speech bubble with a four-point spark inside.
            const ImVec2 a(c.x - h * 0.8f, c.y - h * 0.62f), b(c.x + h * 0.8f, c.y + h * 0.42f);
            dl->AddRect(a, b, col, s * 0.12f, 0, th);
            dl->AddTriangleFilled(ImVec2(c.x - h * 0.42f, b.y - th * 0.5f), ImVec2(c.x - h * 0.05f, b.y - th * 0.5f),
                                  ImVec2(c.x - h * 0.5f, c.y + h * 0.82f), col);
            const ImVec2 m(c.x, (a.y + b.y) * 0.5f);
            const float r = h * 0.3f, w = h * 0.08f;
            dl->AddQuadFilled(ImVec2(m.x, m.y - r), ImVec2(m.x + w, m.y), ImVec2(m.x, m.y + r), ImVec2(m.x - w, m.y), col);
            dl->AddQuadFilled(ImVec2(m.x - r, m.y), ImVec2(m.x, m.y - w), ImVec2(m.x + r, m.y), ImVec2(m.x, m.y + w), col);
            break;
         }
         case UiIcon::Perf:
            // Three faders.
            for (int i = 0; i < 3; i++)
            {
               const float x = c.x + (i - 1) * h * 0.6f;
               dl->AddLine(ImVec2(x, c.y - h * 0.75f), ImVec2(x, c.y + h * 0.75f), col, th * 0.8f);
               const float ky = c.y + (i == 0 ? 0.35f : i == 1 ? -0.4f : 0.1f) * h;
               dl->AddRectFilled(ImVec2(x - h * 0.22f, ky - h * 0.13f), ImVec2(x + h * 0.22f, ky + h * 0.13f), col, 1.0f);
            }
            break;
         case UiIcon::Panel:
            dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.65f), ImVec2(c.x + h * 0.8f, c.y + h * 0.65f), col, 2.0f, 0, th);
            dl->AddRectFilled(ImVec2(c.x + h * 0.2f, c.y - h * 0.65f), ImVec2(c.x + h * 0.8f, c.y + h * 0.65f), col, 2.0f);
            break;
         case UiIcon::Cpu:
         {
            dl->AddRect(ImVec2(c.x - h * 0.5f, c.y - h * 0.5f), ImVec2(c.x + h * 0.5f, c.y + h * 0.5f), col, 1.5f, 0, th);
            dl->AddRectFilled(ImVec2(c.x - h * 0.2f, c.y - h * 0.2f), ImVec2(c.x + h * 0.2f, c.y + h * 0.2f), col);
            for (int i = -1; i <= 1; i++)
            {
               const float o = i * h * 0.3f;
               dl->AddLine(ImVec2(c.x + o, c.y - h * 0.5f), ImVec2(c.x + o, c.y - h * 0.8f), col, th * 0.8f);
               dl->AddLine(ImVec2(c.x + o, c.y + h * 0.5f), ImVec2(c.x + o, c.y + h * 0.8f), col, th * 0.8f);
               dl->AddLine(ImVec2(c.x - h * 0.5f, c.y + o), ImVec2(c.x - h * 0.8f, c.y + o), col, th * 0.8f);
               dl->AddLine(ImVec2(c.x + h * 0.5f, c.y + o), ImVec2(c.x + h * 0.8f, c.y + o), col, th * 0.8f);
            }
            break;
         }
         case UiIcon::Audio:
         {
            // A waveform: five bars of different heights.
            static const float kH[5] = { 0.35f, 0.75f, 0.5f, 0.9f, 0.4f };
            for (int i = 0; i < 5; i++)
            {
               const float x = c.x + (i - 2) * h * 0.36f;
               dl->AddLine(ImVec2(x, c.y - h * kH[i]), ImVec2(x, c.y + h * kH[i]), col, th);
            }
            break;
         }
         case UiIcon::Gpu:
            // A card with a fan.
            dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.5f), ImVec2(c.x + h * 0.85f, c.y + h * 0.5f), col, 1.5f, 0, th);
            dl->AddCircle(ImVec2(c.x - h * 0.2f, c.y), h * 0.3f, col, 12, th * 0.9f);
            dl->AddLine(ImVec2(c.x + h * 0.35f, c.y - h * 0.25f), ImVec2(c.x + h * 0.6f, c.y - h * 0.25f), col, th * 0.8f);
            dl->AddLine(ImVec2(c.x + h * 0.35f, c.y + h * 0.15f), ImVec2(c.x + h * 0.6f, c.y + h * 0.15f), col, th * 0.8f);
            break;
         case UiIcon::Vram:
            // A memory stick: a bar with chips and contacts.
            dl->AddRect(ImVec2(c.x - h * 0.9f, c.y - h * 0.4f), ImVec2(c.x + h * 0.9f, c.y + h * 0.25f), col, 1.0f, 0, th);
            for (int i = 0; i < 3; i++)
            {
               const float x = c.x + (i - 1) * h * 0.55f;
               dl->AddRectFilled(ImVec2(x - h * 0.18f, c.y - h * 0.25f), ImVec2(x + h * 0.18f, c.y + h * 0.08f), col);
            }
            for (int i = 0; i < 5; i++)
            {
               const float x = c.x + (i - 2) * h * 0.34f;
               dl->AddLine(ImVec2(x, c.y + h * 0.25f), ImVec2(x, c.y + h * 0.55f), col, th * 0.7f);
            }
            break;
         case UiIcon::Fps:
         {
            // A gauge: an arc and a needle.
            const ImVec2 o(c.x, c.y + h * 0.35f);
            dl->PathArcTo(o, h * 0.8f, 3.14159f, 6.2832f, 20);
            dl->PathStroke(col, 0, th);
            dl->AddLine(o, ImVec2(o.x + h * 0.45f, o.y - h * 0.55f), col, th);
            dl->AddCircleFilled(o, th * 1.1f, col);
            break;
         }
         case UiIcon::SetList:
            for (int i = 0; i < 3; i++)
            {
               const float y = c.y + (i - 1) * h * 0.55f;
               dl->AddCircleFilled(ImVec2(c.x - h * 0.65f, y), th * 0.95f, col);
               dl->AddLine(ImVec2(c.x - h * 0.35f, y), ImVec2(c.x + h * 0.8f, y), col, th);
            }
            break;
         case UiIcon::Next:
         case UiIcon::Prev:
         {
            const float d = icon == UiIcon::Next ? 1.0f : -1.0f;
            dl->AddTriangleFilled(ImVec2(c.x - d * h * 0.6f, c.y - h * 0.6f), ImVec2(c.x - d * h * 0.6f, c.y + h * 0.6f),
                                  ImVec2(c.x + d * h * 0.35f, c.y), col);
            dl->AddRectFilled(ImVec2(c.x + d * h * 0.4f - h * 0.12f, c.y - h * 0.6f),
                              ImVec2(c.x + d * h * 0.4f + h * 0.12f, c.y + h * 0.6f), col);
            break;
         }
         case UiIcon::Import:
            // Arrow into a tray.
            dl->AddLine(ImVec2(c.x, c.y - h * 0.8f), ImVec2(c.x, c.y + h * 0.15f), col, th);
            dl->AddTriangleFilled(ImVec2(c.x - h * 0.4f, c.y - h * 0.05f), ImVec2(c.x + h * 0.4f, c.y - h * 0.05f),
                                  ImVec2(c.x, c.y + h * 0.4f), col);
            dl->AddLine(ImVec2(c.x - h * 0.8f, c.y + h * 0.35f), ImVec2(c.x - h * 0.8f, c.y + h * 0.75f), col, th);
            dl->AddLine(ImVec2(c.x - h * 0.8f, c.y + h * 0.75f), ImVec2(c.x + h * 0.8f, c.y + h * 0.75f), col, th);
            dl->AddLine(ImVec2(c.x + h * 0.8f, c.y + h * 0.35f), ImVec2(c.x + h * 0.8f, c.y + h * 0.75f), col, th);
            break;
      }
   }

   // A square (or `width`-wide) button showing an icon. `active` paints it
   // with `activeColor` (or the theme's active button colour). Returns clicked.
   bool UiIconButton(const char* id, UiIcon icon, bool active, const char* tooltip,
                     ImVec4 activeColor = ImVec4(0, 0, 0, 0), float width = 0.0f)
   {
      const float hgt = ImGui::GetFrameHeight();
      const float w = width > 0.0f ? width : hgt * 1.15f;
      const bool customActive = active && activeColor.w > 0.0f;
      if (active)
         ImGui::PushStyleColor(ImGuiCol_Button, customActive ? activeColor : ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
      const bool clicked = ImGui::Button(id, ImVec2(w, hgt));
      if (active)
         ImGui::PopStyleColor();
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      DrawUiIcon(ImGui::GetWindowDrawList(), icon, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f),
                 hgt * 0.62f, ImGui::GetColorU32(ImGuiCol_Text));
      if (tooltip != nullptr && tooltip[0] != '\0' && ImGui::IsItemHovered())
         ImGui::SetTooltip("%s", tooltip);
      return clicked;
   }
