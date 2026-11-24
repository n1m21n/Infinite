// Spatial Mixer node body (docs/plans/spatial/README.md "Node spec"): a top-view
// stage with the listener's head at the centre and one draggable dot per
// connected input, a source strip, and a knob row for the selected object.
#include "app/ui/design/TokenColors.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/ListRow.h"
#include "app/ui/design/UiInteract.h"
#include "app/AppShared.h"
#include "platform/HeadTracker.h"
#include "app/ui/design/UiFmt.h"

namespace app
{
   namespace
   {
      constexpr float kEdgeMetres = tok::spatial_mixer_edge_metres; // stage edge; radius mapping is sqrt so 1-2 m stay grabbable

      float RadiusFracFromDist(float d) { return std::sqrt(std::clamp(d, 0.0f, kEdgeMetres) / kEdgeMetres); }
      float DistFromRadiusFrac(float f) { return std::clamp(f * f * kEdgeMetres, 0.2f, kEdgeMetres); }

      ImU32 DotColor(int slot, float alpha)
      {
         const ImColor c = ImColor::HSV(std::fmod((float)slot * 0.137f + 0.02f, 1.0f), 0.55f, IsThemeLight() ? 0.75f : 0.95f);
         return IM_COL32((int)(c.Value.x * tok::spatial_mixer_dot_color_ratio), (int)(c.Value.y * tok::spatial_mixer_dot_color_ratio), (int)(c.Value.z * 255), (int)(alpha * 255));
      }
   }

   namespace
   {
      // On = accent fill (no star suffix). Reads the state once so Push and Pop
      // always pair, even though the click flips it in between.
      bool LaneToggle(const char* label, bool on, float w)
      {
         if (on)
            PushSelectedButtonColors();
         const bool clicked = ActionButton::Draw(label, ImVec2(w, 0.0f));
         if (on)
            PopSelectedButtonColors();
         return clicked;
      }
   }

   void DrawSpatialMixerBody(GraphNode& gn, SpatialMixerNode* n)
   {
      const bool light = IsThemeLight();
      const int connected = n->ConnectedCount();
      const int pins = n->PinCount();
      n->selected = std::clamp(n->selected, 0, SpatialMixerNode::kMaxSlots - 1);

      char stat[112];
      const bool tracked = n->trackMode == 1 && n->HeadTracked();
      if (n->IsRecording())
         snprintf(stat, sizeof(stat), "REC %s %d-bit  %s", n->formatIndex == 1 ? "FLAC" : "WAV", n->bit24 ? 24 : 16,  // ui-audit-ok: bit depth
                  UiFmt::Sec(n->ElapsedSeconds()).c);
      else if (connected > 0)
         snprintf(stat, sizeof(stat), "%d in -> %s%s  %s", connected, n->renderMode == 1 ? "stereo" : "binaural",
                  tracked ? " - tracked" : "", UiFmt::Lufs(std::max(n->LufsShort(), -99.0f)).c);
      else
         snprintf(stat, sizeof(stat), "0 in \xE2\x86\x92 binaural (idle)");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, tok::spatial_mixer_spacer_h));

      // ---- stage ---------------------------------------------------------
      const float stageW = gAudioContentW, stageH = tok::spatial_mixer_stage_h;
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const ImVec2 centre(p0.x + stageW * 0.5f, p0.y + stageH * 0.5f);
      const float R = std::min(stageW, stageH) * 0.5f - 14.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const ImU32 line = light ? tok::Base(tok::col::ink_black, tok::spatial_mixer_line_alpha_lg) : tok::Base(tok::col::ink_white, tok::spatial_mixer_line_alpha_sm);
      const ImU32 text = light ? tok::Base(tok::col::ink_black, tok::spatial_mixer_body_ink_black_alpha) : tok::Base(tok::col::ink_white, tok::spatial_mixer_body_ink_white_alpha);
      dl->AddRectFilled(p0, ImVec2(p0.x + stageW, p0.y + stageH), light ? tok::Base(tok::col::ink_black, tok::spatial_mixer_fill_alpha_lg) : tok::Base(tok::col::ink_white, tok::spatial_mixer_fill_alpha_sm), tok::radius_control);
      dl->AddCircle(centre, R, line, tok::spatial_mixer_body_segments);
      for (float m : { 1.0f, 2.0f })
         dl->AddCircle(centre, R * RadiusFracFromDist(m), line, tok::spatial_mixer_ring_segments_lg);
      dl->AddLine(ImVec2(centre.x - R, centre.y), ImVec2(centre.x + R, centre.y), line);
      dl->AddLine(ImVec2(centre.x, centre.y - R), ImVec2(centre.x, centre.y + R), line);
      dl->AddText(ImVec2(centre.x - tok::space_0_75, p0.y + tok::space_0_5), text, "F");
      dl->AddText(ImVec2(centre.x - tok::space_0_75, p0.y + stageH - ImGui::GetTextLineHeight() - tok::space_0_5), text, "B");
      dl->AddText(ImVec2(p0.x + tok::space_1_25, centre.y - tok::spatial_mixer_body_gap), text, "L");
      dl->AddText(ImVec2(p0.x + stageW - tok::space_3, centre.y - tok::spatial_mixer_body_gap), text, "R");

      // Listener seen from above, facing up (towards F): shoulders, neck, head
      // and ears - the same view as the reference body mesh. Facing is shown by F.
      {
         const ImU32 skin = light ? tok::Base(tok::col::text_secondary) : tok::Base(tok::col::edge_lo_pale);
         const ImU32 skinHi = light ? tok::Base(tok::col::edge_mid_light_fixed) : tok::Base(tok::col::accent_hover);
         const ImU32 shade = light ? tok::Base(tok::col::surface_mid_deep) : tok::Base(tok::col::accent_dark_muted_lo);
         const ImU32 rim = light ? tok::Base(tok::col::edge_lo_light) : tok::Base(tok::col::surface_mid_alt);
         const float cx = centre.x, cy = centre.y;
         // shoulders: wide rounded slab behind the head
         dl->AddEllipseFilled(ImVec2(cx, cy + tok::spatial_mixer_ellipse_filled_y_xl), ImVec2(tok::spatial_mixer_body_add_ellipse_filled_x, tok::spatial_mixer_body_add_ellipse_filled_y), skin, 0.0f, tok::spatial_mixer_ellipse_filled_rot_lg);
         dl->AddEllipse(ImVec2(cx, cy + tok::spatial_mixer_ellipse_filled_y_xl), ImVec2(tok::spatial_mixer_body_add_ellipse_x, tok::spatial_mixer_body_add_ellipse_y), rim, 0.0f, tok::spatial_mixer_ellipse_filled_rot_lg, 1.0f);
         // neck
         dl->AddEllipseFilled(ImVec2(cx, cy + tok::space_2), ImVec2(tok::spatial_mixer_ellipse_filled_x_sm, tok::spatial_mixer_ellipse_filled_y_sm), shade, 0.0f, tok::spatial_mixer_ellipse_filled_rot_md);
         // ears (behind the head outline)
         dl->AddEllipseFilled(ImVec2(cx - tok::spatial_mixer_ellipse_filled_x_xl, cy + 1.0f), ImVec2(tok::spatial_mixer_ellipse_filled_x_xs, tok::spatial_mixer_ellipse_filled_y_xs), skin, 0.0f, tok::spatial_mixer_ellipse_filled_rot_sm);
         dl->AddEllipseFilled(ImVec2(cx + tok::spatial_mixer_ellipse_filled_x_xl, cy + 1.0f), ImVec2(tok::spatial_mixer_ellipse_filled_x_xs, tok::spatial_mixer_ellipse_filled_y_xs), skin, 0.0f, tok::spatial_mixer_ellipse_filled_rot_sm);
         dl->AddEllipse(ImVec2(cx - tok::spatial_mixer_ellipse_filled_x_xl, cy + 1.0f), ImVec2(tok::spatial_mixer_ellipse_x_sm, tok::spatial_mixer_ellipse_y_sm), rim, 0.0f, tok::spatial_mixer_ellipse_filled_rot_sm, 1.0f);
         dl->AddEllipse(ImVec2(cx + tok::spatial_mixer_ellipse_filled_x_xl, cy + 1.0f), ImVec2(tok::spatial_mixer_ellipse_x_sm, tok::spatial_mixer_ellipse_y_sm), rim, 0.0f, tok::spatial_mixer_ellipse_filled_rot_sm, 1.0f);
         // head: oval, lit from the front
         dl->AddEllipseFilled(ImVec2(cx, cy), ImVec2(tok::spatial_mixer_ellipse_filled_x_lg, tok::spatial_mixer_ellipse_filled_y_lg), skin, 0.0f, tok::spatial_mixer_ellipse_filled_rot_lg);
         dl->AddEllipseFilled(ImVec2(cx, cy - tok::space_0_75), ImVec2(tok::spatial_mixer_ellipse_filled_x_md, tok::spatial_mixer_ellipse_filled_y_md), skinHi, 0.0f, tok::spatial_mixer_ring_segments_sm);
         dl->AddEllipse(ImVec2(cx, cy), ImVec2(tok::spatial_mixer_ellipse_x_lg, tok::spatial_mixer_ellipse_y_lg), rim, 0.0f, tok::spatial_mixer_ellipse_filled_rot_lg, tok::spatial_mixer_ellipse_segments);
      }

      // ---- dots ----------------------------------------------------------
      auto dotPos = [&](int s) {
         const float rad = R * RadiusFracFromDist(n->distance[s]);
         const float a = n->azimuth[s] * tok::spatial_mixer_a_ratio;
         return ImVec2(centre.x + std::sin(a) * rad, centre.y - std::cos(a) * rad);
      };

      ImGui::SetCursorScreenPos(p0);
      UiInteract::Hit("##stage", ImVec2(stageW, stageH),
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
         if ((mouse.x - d.x) * (mouse.x - d.x) + (mouse.y - d.y) * (mouse.y - d.y) <= tok::spatial_mixer_body_ratio * 11.0f)
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
               n->gainDb[s] = std::clamp(n->gainDb[s] - ImGui::GetIO().MouseDelta.y * tok::spatial_mixer_v, -60.0f, 12.0f);
            }
            else
            {
               const float dx = mouse.x - centre.x, dy = mouse.y - centre.y;
               float az = std::atan2(dx, -dy) * tok::spatial_mixer_az_ratio;
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
         const float r = tok::spatial_mixer_r_ratio * elScale;
         const float lvl = std::clamp(n->ChannelLevel(s), 0.0f, 1.0f);
         if (hot == s || n->selected == s)
            dl->AddCircle(d, r + tok::space_1_25, text, tok::spatial_mixer_ring_segments_sm, tok::spatial_mixer_body_stroke);
         if (lvl > 0.002f)
            dl->AddCircle(d, r + tok::space_0_5 + lvl * tok::spatial_mixer_ring_r, DotColor(s, tok::spatial_mixer_dot_color_alpha_lg), tok::spatial_mixer_ring_segments_sm, tok::spatial_mixer_ring_stroke);
         dl->AddCircleFilled(d, r, DotColor(s, silent ? tok::spatial_mixer_dot_color_alpha_sm : 1.0f), tok::spatial_mixer_ring_segments_sm);
         dl->AddCircle(d, r, light ? tok::Base(tok::col::ink_black, tok::spatial_mixer_ring_alpha) : tok::Base(tok::col::ink_white, tok::spatial_mixer_ring_alpha), tok::spatial_mixer_ring_segments_sm, 1.0f);
         char lab[16];
         snprintf(lab, sizeof(lab), "%d", s + 1);
         const ImVec2 ts = ImGui::CalcTextSize(lab);
         dl->AddText(ImVec2(d.x - ts.x * 0.5f, d.y - ts.y * 0.5f), tok::Role(tok::col::ink_black, tok::spatial_mixer_text_alpha), lab);
      }

      // Readout: hover or selected object.
      {
         const int s = hot >= 0 ? hot : n->selected;
         if (n->inputs[s].IsConnected() && (hot >= 0 || sDragNode == gn.index))
         {
            char r[96];
            snprintf(r, sizeof(r), "in %d  az %s  el %s  %s m  %s", s + 1, UiFmt::F0(n->azimuth[s]).c,
                     UiFmt::F0(n->elevation[s]).c, UiFmt::F1(n->distance[s]).c, UiFmt::DbOffset(n->gainDb[s]).c);
            gAudioReadout[gn.index] = r;
         }
      }

      ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + stageH + tok::space_1_5));

      // ---- source strip --------------------------------------------------
      for (int s = 0; s < pins; s++)
      {
         if (!n->inputs[s].IsConnected())
            continue;
         ImGui::PushID(s);
         const ImVec2 rp = ImGui::GetCursorScreenPos();
         for (int k = 0; k < 5; k++)   // lane param ordinals: see laneParam below
            NoteHiddenParamAnchor(gn.index, 3 + s * 5 + k, ImVec2(rp.x + tok::space_2, rp.y + ImGui::GetFrameHeight() * 0.5f));
         const float ty = ImGui::GetStyle().FramePadding.y;   // centre text against the taller H/M/S buttons
         dl->AddCircleFilled(ImVec2(rp.x + tok::space_2, rp.y + ty + ImGui::GetTextLineHeight() * 0.5f), tok::spatial_mixer_body_r, DotColor(s, 1.0f), tok::spatial_mixer_ellipse_filled_rot_md);
         ImGui::SetCursorScreenPos(ImVec2(rp.x + tok::spatial_mixer_x, rp.y + ty));
         const bool sel = (n->selected == s);
         if (ListRow::Selectable(("in " + std::to_string(s + 1)).c_str(), sel, 0, ImVec2(tok::spatial_mixer_body_selectable_w, 0.0f)))
            n->selected = s;
         // Right-aligned cluster in screen space: dB readout, then H M S as equal
         // fixed-width buttons ending on the content edge. Window-relative
         // SameLine offsets drift inside the node canvas and widen the node.
         {
            // ActionButton widens a button to its label plus 2 x space_2, so size the slot from the widest label
            // (M) or the three tiles overlap at larger UI scales.
            const float bw = std::max(22.0f, ImGui::CalcTextSize("M").x + tok::spatial_mixer_m_min * tok::space_2), gap = tok::space_0_75;
            const float right = gAudioContentX + gAudioContentW;
            const float x0 = right - tok::spatial_mixer_x0_ratio * bw - tok::spatial_mixer_x0_scale * gap;
            const float y = rp.y;
            char db[24];
            snprintf(db, sizeof(db), "%s", UiFmt::DbOffset(n->gainDb[s]).c);
            const float tw = ImGui::CalcTextSize(db).x;
            ImGui::SetCursorScreenPos(ImVec2(x0 - tok::space_2 - tw, y + ty));
            ImGui::TextUnformatted(db);
            ImGui::SetCursorScreenPos(ImVec2(x0, y));
            if (LaneToggle("H", n->headLocked[s], bw))
            {
               PushUndoCheckpoint();
               n->headLocked[s] = !n->headLocked[s];
            }
            if (ImGui::IsItemHovered())
               SetAudioReadout("head lock", n->headLocked[s] ? "stays in front of your head" : "fixed in the room");
            ImGui::SetCursorScreenPos(ImVec2(x0 + bw + gap, y));
            if (LaneToggle("M", n->mute[s], bw))
            {
               PushUndoCheckpoint();
               n->mute[s] = !n->mute[s];
            }
            ImGui::SetCursorScreenPos(ImVec2(x0 + tok::spatial_mixer_m_min * (bw + gap), y));
            if (LaneToggle("S", n->solo[s], bw))
            {
               PushUndoCheckpoint();
               n->solo[s] = !n->solo[s];
            }
         }
         ImGui::PopID();
      }
      ImGui::Dummy(ImVec2(0.0f, tok::spatial_mixer_spacer_h));

      // ---- knobs: selected object, then master ---------------------------
      const int sel = n->selected;
      // Modulation addresses are per lane, not per knob position: a cable into
      // "in 3 azimuth" stays on in 3 whichever lane is selected. Ordinals
      // 0..2 are the master knobs, then five per lane (kLaneBase + lane * 5 + k).
      // Every lane registers every frame (register-only when it is not the
      // drawn one), so a cable keeps driving a lane that is off screen - the
      // same mechanism as the EQ's hidden bands.
      static constexpr int kLaneBase = 3;
      auto laneParam = [](int lane, int k) { return kLaneBase + lane * tok::spatial_mixer_scale + k; };
      auto laneName = [](int lane, const char* what) { return "in " + std::to_string(lane + 1) + " " + what; };
      {
         const bool savedRegisterOnly = gParamRegisterOnly;
         gParamRegisterOnly = true;
         for (int lane = 0; lane < SpatialMixerNode::kMaxSlots; lane++)
         {
            if (lane == sel)
               continue;
            ModKnob("azimuth", &n->azimuth[lane], -180.0f, 180.0f, "%.0f deg", kKnobLarge, 0.0f, AudioWidgetStyle::Knob,
                    0.0f, nullptr, nullptr, laneParam(lane, 0), laneName(lane, "azimuth").c_str());
            ModKnob("elevation", &n->elevation[lane], -90.0f, 90.0f, "%.0f deg", kKnobLarge, 0.0f,
                    AudioWidgetStyle::Knob, 0.0f, nullptr, nullptr, laneParam(lane, 1), laneName(lane, "elevation").c_str());
            ModKnob("distance", &n->distance[lane], 0.2f, kEdgeMetres, "%.1f m", kKnobLarge, 0.0f,
                    AudioWidgetStyle::Knob, 0.0f, nullptr, nullptr, laneParam(lane, 2), laneName(lane, "distance").c_str());
            ModKnob("width", &n->width[lane], 0.0f, 1.0f, "%.2f", kKnobLarge, 0.0f, AudioWidgetStyle::Knob, 0.0f,
                    nullptr, nullptr, laneParam(lane, 3), laneName(lane, "width").c_str());
            ModKnob("level", &n->gainDb[lane], -60.0f, 12.0f, "%.1f dB", kKnobSmall, 0.0f, AudioWidgetStyle::KnobDb,
                    0.0f, nullptr, nullptr, laneParam(lane, 4), laneName(lane, "level").c_str());
         }
         gParamRegisterOnly = savedRegisterOnly;
      }
      {
         AudioKnobRow row(tok::spatial_mixer_body_audio_knob_row_layout, kKnobLarge);
         const auto az = laneName(sel, "azimuth"), el = laneName(sel, "elevation"), di = laneName(sel, "distance"),
                    wi = laneName(sel, "width"), lv = laneName(sel, "level");
         row.Knob("azimuth", &n->azimuth[sel], params::SpatialMixer::azimuth_lo, params::SpatialMixer::azimuth_hi, "%.0f deg", kKnobLarge, false, false,
                  AudioWidgetStyle::Knob, nullptr, nullptr, laneParam(sel, 0), az.c_str());
         row.Knob("elevation", &n->elevation[sel], params::SpatialMixer::elevation_lo, params::SpatialMixer::elevation_hi, "%.0f deg", kKnobLarge, false, false,
                  AudioWidgetStyle::Knob, nullptr, nullptr, laneParam(sel, 1), el.c_str());
         row.Knob("distance", &n->distance[sel], params::SpatialMixer::distance_lo, kEdgeMetres, "%.1f m", kKnobLarge, false, false,
                  AudioWidgetStyle::Knob, nullptr, nullptr, laneParam(sel, 2), di.c_str());
         row.Knob("width", &n->width[sel], 0.0f, 1.0f, "%.2f", kKnobLarge, false, false, AudioWidgetStyle::Knob,
                  nullptr, nullptr, laneParam(sel, 3), wi.c_str());
         row.End();
         AudioKnobRow row2(tok::spatial_mixer_body_audio_knob_row_layout, kKnobSmall);
         row2.Knob("level", &n->gainDb[sel], params::SpatialMixer::gain_db_lo, params::SpatialMixer::gain_db_hi, "%.1f dB", kKnobSmall, /*dbTaper=*/true, false,
                   AudioWidgetStyle::Knob, nullptr, nullptr, laneParam(sel, 4), lv.c_str());
         row2.Knob("room", &n->room, 0.0f, 1.0f, "%.2f", kKnobSmall, false, false, AudioWidgetStyle::Knob, nullptr,
                   nullptr, 0);
         row2.Knob("bass mono", &n->bassHz, params::SpatialMixer::bass_hz_lo, params::SpatialMixer::bass_hz_hi, "%.0f Hz", kKnobSmall, false, false,
                   AudioWidgetStyle::Knob, nullptr, nullptr, 1);
         row2.Knob("out", &n->outDb, params::SpatialMixer::out_db_lo, params::SpatialMixer::out_db_hi, "%.1f dB", kKnobSmall, /*dbTaper=*/true, false,
                   AudioWidgetStyle::Knob, nullptr, nullptr, params::SpatialMixer::out_db_def);
         row2.End();
      }
      gParamCounter = kLaneBase + SpatialMixerNode::kMaxSlots * 5;

      // ---- master: head tracking, limiter, hrtf, meter --------------------
      {
         const float half = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x) / tok::spatial_mixer_half_ratio;
         const bool trackOk = HeadTracker::Supported(HeadTracker::kHeadphones);
         const std::vector<std::string> trackNames = { "head: off", trackOk ? "head: AirPods" : "head: AirPods (macOS)" };
         AudioBareDropdown("##spatialTrack", trackNames, std::clamp(n->trackMode, 0, 1),
                           [n](int i) {
                              PushUndoCheckpoint();
                              n->trackMode = i;
                              gPatchDirty = true;
                           },
                           half);
         ImGui::SameLine();
         ImGui::BeginDisabled(n->trackMode == 0);
         if (ActionButton::Draw("recenter##spatialRecenter", ImVec2(half, 0)))
            n->RecenterHead();
         ImGui::EndDisabled();
         if (n->trackMode != 0 && ImGui::IsItemHovered())
            SetAudioReadout("head", HeadTracker::Status());

         AudioToggleButton("limit##spatialLimit", &n->limiter, half);
         if (ImGui::IsItemHovered())
            SetAudioReadout("limiter", n->limiter ? "true-peak ceiling -1 dB on the output" : "off: output can clip");
         ImGui::SameLine();
         bool model = n->hrtf == 1;
         if (AudioToggleButton(model ? "model##spatialHrtf" : "KEMAR##spatialHrtf", &model, half))
         {
            PushUndoCheckpoint();
            n->hrtf = model ? 1 : 0;
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout("ears", model ? "spherical-head model, no measured data" : "measured dummy-head (MIT KEMAR)");

         char meter[96];
         snprintf(meter, sizeof(meter), "%s   peak %s   gr %s", UiFmt::Lufs(std::max(n->LufsShort(), -99.0f)).c,
                  UiFmt::Db(std::max(n->TruePeakDb(), -99.0f)).c, UiFmt::Db(n->ReductionDb()).c);
         ImGui::TextUnformatted(meter);
         if (ImGui::IsItemClicked())
            n->ResetPeak();
         if (ImGui::IsItemHovered())
            SetAudioReadout("meter", "short-term loudness, true peak (click to reset), limiter reduction");
      }
      ImGui::Dummy(ImVec2(0.0f, tok::spatial_mixer_spacer_h));
      DrawSpatialExportPanel(n);
      EndAudioBody();
   }
}
