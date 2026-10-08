// Spatial Mixer node body (docs/plans/spatial/README.md "Node spec"): a top-view
// stage with the listener's head at the centre and one draggable dot per
// connected input, a source strip, and a knob row for the selected object.
#include "app/AppShared.h"

namespace app
{
   namespace
   {
      constexpr float kEdgeMetres = 10.0f; // stage edge; radius mapping is sqrt so 1-2 m stay grabbable

      float RadiusFracFromDist(float d) { return std::sqrt(std::clamp(d, 0.0f, kEdgeMetres) / kEdgeMetres); }
      float DistFromRadiusFrac(float f) { return std::clamp(f * f * kEdgeMetres, 0.2f, kEdgeMetres); }

      ImU32 DotColor(int slot, float alpha)
      {
         const ImColor c = ImColor::HSV(std::fmod((float)slot * 0.137f + 0.02f, 1.0f), 0.55f, IsThemeLight() ? 0.75f : 0.95f);
         return IM_COL32((int)(c.Value.x * 255), (int)(c.Value.y * 255), (int)(c.Value.z * 255), (int)(alpha * 255));
      }
   }

   void DrawSpatialMixerBody(GraphNode& gn, SpatialMixerNode* n)
   {
      const bool light = IsThemeLight();
      const int connected = n->ConnectedCount();
      const int pins = n->PinCount();
      n->selected = std::clamp(n->selected, 0, SpatialMixerNode::kMaxSlots - 1);

      char stat[96];
      if (n->IsRecording())
         snprintf(stat, sizeof(stat), "REC %s  %.1fs", n->formatIndex == 1 ? "FLAC" : "WAV", n->ElapsedSeconds());
      else if (connected > 0)
         snprintf(stat, sizeof(stat), "%d in -> %s   %+.1f dB", connected, n->renderMode == 1 ? "stereo" : "binaural",
                  DspMath::LinearToDb(std::max(n->Level(), 1e-5f)));
      else
         snprintf(stat, sizeof(stat), "0 in -> binaural (idle)");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // ---- stage ---------------------------------------------------------
      const float stageW = gAudioContentW, stageH = 280.0f;
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const ImVec2 centre(p0.x + stageW * 0.5f, p0.y + stageH * 0.5f);
      const float R = std::min(stageW, stageH) * 0.5f - 14.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const ImU32 line = light ? IM_COL32(0, 0, 0, 45) : IM_COL32(255, 255, 255, 40);
      const ImU32 text = light ? IM_COL32(0, 0, 0, 150) : IM_COL32(255, 255, 255, 150);
      dl->AddRectFilled(p0, ImVec2(p0.x + stageW, p0.y + stageH), light ? IM_COL32(0, 0, 0, 14) : IM_COL32(255, 255, 255, 10), 4.0f);
      dl->AddCircle(centre, R, line, 64);
      for (float m : { 1.0f, 2.0f })
         dl->AddCircle(centre, R * RadiusFracFromDist(m), line, 48);
      dl->AddLine(ImVec2(centre.x - R, centre.y), ImVec2(centre.x + R, centre.y), line);
      dl->AddLine(ImVec2(centre.x, centre.y - R), ImVec2(centre.x, centre.y + R), line);
      dl->AddText(ImVec2(centre.x - 3.0f, p0.y + 2.0f), text, "F");
      dl->AddText(ImVec2(centre.x - 3.0f, p0.y + stageH - ImGui::GetTextLineHeight() - 2.0f), text, "B");
      dl->AddText(ImVec2(p0.x + 5.0f, centre.y - 7.0f), text, "L");
      dl->AddText(ImVec2(p0.x + stageW - 12.0f, centre.y - 7.0f), text, "R");

      // Head, facing up: oval slightly longer front-to-back, nose, two ears.
      {
         const ImU32 fill = light ? IM_COL32(235, 235, 238, 255) : IM_COL32(60, 62, 70, 255);
         const ImU32 edge = light ? IM_COL32(40, 40, 46, 255) : IM_COL32(215, 215, 225, 255);
         dl->AddEllipseFilled(centre, ImVec2(13.0f, 16.0f), fill, 0.0f, 32);
         dl->AddEllipse(centre, ImVec2(13.0f, 16.0f), edge, 0.0f, 32, 1.5f);
         dl->AddTriangleFilled(ImVec2(centre.x - 4.0f, centre.y - 15.0f), ImVec2(centre.x + 4.0f, centre.y - 15.0f),
                               ImVec2(centre.x, centre.y - 22.0f), edge);
         dl->AddEllipseFilled(ImVec2(centre.x - 14.0f, centre.y + 1.0f), ImVec2(2.5f, 5.0f), edge, 0.0f, 12);
         dl->AddEllipseFilled(ImVec2(centre.x + 14.0f, centre.y + 1.0f), ImVec2(2.5f, 5.0f), edge, 0.0f, 12);
      }

      // ---- dots ----------------------------------------------------------
      auto dotPos = [&](int s) {
         const float rad = R * RadiusFracFromDist(n->distance[s]);
         const float a = n->azimuth[s] * 0.017453292f;
         return ImVec2(centre.x + std::sin(a) * rad, centre.y - std::cos(a) * rad);
      };

      ImGui::SetCursorScreenPos(p0);
      ImGui::InvisibleButton("##stage", ImVec2(stageW, stageH),
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      const bool stageHovered = ImGui::IsItemHovered();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      // Topmost dot under the cursor (later slots draw on top).
      int hot = -1;
      for (int s = 0; s < SpatialMixerNode::kMaxSlots; s++)
      {
         if (!n->inputs[s].IsConnected())
            continue;
         const ImVec2 d = dotPos(s);
         if ((mouse.x - d.x) * (mouse.x - d.x) + (mouse.y - d.y) * (mouse.y - d.y) <= 11.0f * 11.0f)
            hot = s;
      }

      static int sDragNode = -1, sDragSlot = -1;
      if (stageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hot >= 0)
      {
         n->selected = hot;
         sDragNode = gn.index;
         sDragSlot = hot;
         PushUndoCheckpoint(); // one checkpoint per drag, not per frame
      }
      if (stageHovered && hot >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         n->azimuth[hot] = SpatialMixerNode::DefaultAzimuth(hot);
         n->elevation[hot] = 0.0f;
         n->distance[hot] = 2.0f;
      }
      if (sDragNode == gn.index && sDragSlot >= 0)
      {
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            const int s = sDragSlot;
            if (ImGui::GetIO().KeyAlt)
            {
               n->gainDb[s] = std::clamp(n->gainDb[s] - ImGui::GetIO().MouseDelta.y * 0.25f, -60.0f, 12.0f);
            }
            else
            {
               const float dx = mouse.x - centre.x, dy = mouse.y - centre.y;
               float az = std::atan2(dx, -dy) * 57.29578f;
               if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper)
                  az = std::round(az / 15.0f) * 15.0f;
               n->azimuth[s] = az;
               n->distance[s] = DistFromRadiusFrac(std::sqrt(dx * dx + dy * dy) / R);
            }
         }
         else
            sDragNode = sDragSlot = -1;
      }
      if (hot >= 0 && ImGui::GetIO().MouseWheel != 0.0f)
         n->elevation[hot] = std::clamp(n->elevation[hot] + ImGui::GetIO().MouseWheel * 5.0f, -90.0f, 90.0f);

      // Dashed-less elevation cue: dot grows above ear level, shrinks below.
      for (int s = 0; s < SpatialMixerNode::kMaxSlots; s++)
      {
         const bool on = n->inputs[s].IsConnected();
         if (!on)
            continue;
         const ImVec2 d = dotPos(s);
         const bool silent = n->mute[s];
         const float elScale = 1.0f + 0.4f * (n->elevation[s] / 90.0f);
         const float r = 7.0f * elScale;
         const float lvl = std::clamp(n->ChannelLevel(s), 0.0f, 1.0f);
         if (hot == s || n->selected == s)
            dl->AddCircle(d, r + 5.0f, text, 24, 1.5f);
         if (lvl > 0.002f)
            dl->AddCircle(d, r + 2.0f + lvl * 4.0f, DotColor(s, 0.5f), 24, 2.0f);
         dl->AddCircleFilled(d, r, DotColor(s, silent ? 0.25f : 1.0f), 24);
         dl->AddCircle(d, r, light ? IM_COL32(0, 0, 0, 120) : IM_COL32(255, 255, 255, 120), 24, 1.0f);
         char lab[4];
         snprintf(lab, sizeof(lab), "%d", s + 1);
         const ImVec2 ts = ImGui::CalcTextSize(lab);
         dl->AddText(ImVec2(d.x - ts.x * 0.5f, d.y - ts.y * 0.5f), IM_COL32(0, 0, 0, 220), lab);
      }

      // Readout: hover or selected object.
      {
         const int s = hot >= 0 ? hot : n->selected;
         if (n->inputs[s].IsConnected() && (hot >= 0 || sDragNode == gn.index))
         {
            char r[96];
            snprintf(r, sizeof(r), "in %d  az %.0f  el %.0f  %.1f m  %+.1f dB", s + 1, n->azimuth[s],
                     n->elevation[s], n->distance[s], n->gainDb[s]);
            gAudioReadout[gn.index] = r;
         }
      }

      ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + stageH + 6.0f));

      // ---- source strip --------------------------------------------------
      for (int s = 0; s < pins; s++)
      {
         if (!n->inputs[s].IsConnected())
            continue;
         ImGui::PushID(s);
         const ImVec2 rp = ImGui::GetCursorScreenPos();
         dl->AddCircleFilled(ImVec2(rp.x + 8.0f, rp.y + ImGui::GetTextLineHeight() * 0.5f), 5.0f, DotColor(s, 1.0f), 16);
         ImGui::SetCursorScreenPos(ImVec2(rp.x + 20.0f, rp.y));
         const bool sel = (n->selected == s);
         if (ImGui::Selectable(("in " + std::to_string(s + 1)).c_str(), sel, 0, ImVec2(80.0f, 0.0f)))
            n->selected = s;
         ImGui::SameLine(gAudioContentX - ImGui::GetWindowPos().x + gAudioContentW - 110.0f);
         ImGui::Text("%+.1f dB", n->gainDb[s]);
         ImGui::SameLine();
         if (ImGui::SmallButton(n->mute[s] ? "M*" : "M"))
         {
            PushUndoCheckpoint();
            n->mute[s] = !n->mute[s];
         }
         ImGui::SameLine();
         if (ImGui::SmallButton(n->solo[s] ? "S*" : "S"))
         {
            PushUndoCheckpoint();
            n->solo[s] = !n->solo[s];
         }
         ImGui::PopID();
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // ---- knobs: selected object, then master ---------------------------
      const int sel = n->selected;
      {
         AudioKnobRow row(4, kKnobLarge);
         row.Knob("azimuth", &n->azimuth[sel], -180.0f, 180.0f, "%.0f deg", kKnobLarge);
         row.Knob("elevation", &n->elevation[sel], -90.0f, 90.0f, "%.0f deg", kKnobLarge);
         row.Knob("distance", &n->distance[sel], 0.2f, kEdgeMetres, "%.1f m", kKnobLarge);
         row.Knob("width", &n->width[sel], 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(2, kKnobSmall);
         row.Knob("level", &n->gainDb[sel], -60.0f, 12.0f, "%.1f dB", kKnobSmall, /*dbTaper=*/true);
         row.Knob("out", &n->outDb, -60.0f, 12.0f, "%.1f dB", kKnobSmall, /*dbTaper=*/true);
         row.End();
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      DrawSpatialExportPanel(n);
      EndAudioBody();
   }
}
