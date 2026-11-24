// Oscillator, wavetable, ADSR, equation and spectral-synth bodies (moved verbatim from main.cpp).
#include "app/ui/design/components/EmptyState.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/Thumb.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/AppShared.h"
#include "core/ModShapes.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/UiFmt.h"

namespace app
{
   void DrawOscillatorScope(OscillatorNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / tok::motion_hz_scope)
      {
         float buf[OscillatorNode::kScopeCacheCapacity];
         const int count = n->ReadScope(buf, OscillatorNode::kScopeCacheCapacity);
         if (count > 0)
         {
            std::copy(buf, buf + count, n->scopeCache);
            n->scopeCacheCount = count;
         }
         n->scopeCacheTime = now;
      }

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      for (int i = 1; i < 8; i++)
         dl->AddLine(ImVec2(origin.x + w * i / tok::osc_oscillator_scope_ratio, origin.y), ImVec2(origin.x + w * i / tok::osc_oscillator_scope_ratio, br.y),
                     ScopeGridCol(), tok::osc_oscillator_scope_stroke);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), tok::osc_oscillator_scope_stroke);

      if (n->scopeCacheCount > 1)
      {
         const bool isLight = IsThemeLight();
         const int count = n->scopeCacheCount;
         for (int pass = 0; pass < 2; pass++)
         {
            dl->PathClear();
            for (int i = 0; i < count; i++)
            {
               const float t = (float)i / (float)(count - 1);
               const float v = std::max(-1.0f, std::min(1.0f, n->scopeCache[i]));
               dl->PathLineTo(ImVec2(origin.x + t * w, midY - v * h * tok::osc_path_line_to_y_lg));
            }
            const ImU32 strokeCol = isLight
               ? (pass == 0 ? tok::Base(tok::col::accent, 50) : tok::Base(tok::col::accent))
               : (pass == 0 ? tok::Role(tok::col::accent_pale_muted_warm, 46) : tok::Role(tok::col::accent_alt, 245));
            dl->PathStroke(strokeCol, 0, pass == 0 ? tok::osc_oscillator_scope_path_stroke_lg : tok::osc_oscillator_scope_path_stroke_sm);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + tok::space_2, origin.y + tok::space_1), ScopeTextCol(), "idle");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawMetallicScope(MetallicNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / tok::motion_hz_scope)
      {
         float buf[MetallicNode::kScopeCacheCapacity];
         const int count = n->ReadScope(buf, MetallicNode::kScopeCacheCapacity);
         if (count > 0)
         {
            std::copy(buf, buf + count, n->scopeCache);
            n->scopeCacheCount = count;
         }
         n->scopeCacheTime = now;
      }

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      for (int i = 1; i < 8; i++)
         dl->AddLine(ImVec2(origin.x + w * i / tok::osc_metallic_scope_ratio, origin.y), ImVec2(origin.x + w * i / tok::osc_metallic_scope_ratio, br.y),
                     ScopeGridCol(), tok::osc_metallic_scope_stroke);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), tok::osc_metallic_scope_stroke);

      if (n->scopeCacheCount > 1)
      {
         const bool isLight = IsThemeLight();
         const int count = n->scopeCacheCount;
         for (int pass = 0; pass < 2; pass++)
         {
            dl->PathClear();
            for (int i = 0; i < count; i++)
            {
               const float t = (float)i / (float)(count - 1);
               const float v = std::max(-1.0f, std::min(1.0f, n->scopeCache[i]));
               dl->PathLineTo(ImVec2(origin.x + t * w, midY - v * h * tok::osc_path_line_to_y_sm));
            }
            const ImU32 strokeCol = isLight
               ? (pass == 0 ? tok::Base(tok::col::state_warn, 50) : tok::Base(tok::col::state_warn))
               : (pass == 0 ? tok::Role(tok::col::role_modulation, 48) : tok::Role(tok::col::role_modulation, 240));
            dl->PathStroke(strokeCol, 0, pass == 0 ? tok::osc_metallic_scope_path_stroke_lg : tok::osc_metallic_scope_path_stroke_sm);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + tok::space_2, origin.y + tok::space_1), ScopeTextCol(), "strike / idle");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   // Sampler's waveform + playhead: the shared trimmed waveform (DrawTrimWave
   // over SamplerNode::wavePeaks, built once at load, so nothing is
   // re-decimated per frame) with the playhead and a cursor per note voice.
   // Interactive: clicking anywhere on the body auditions the sample from
   // that point (SamplerNode::TriggerPreview, independent of any note
   // cable), and the two edge handles drag the loop range (start/end)
   // directly on the picture rather than through separate numeric fields -
   // "where does this loop start/end" is a question the waveform answers
   // immediately and a 0..1 fraction doesn't.
   void DrawSamplerWaveform(SamplerNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const auto& snap = n->VisualSnapshot();
      WaveCursor cursors[SamplerVoiceSnapshot::kMaxVisualVoices];
      int numCursors = 0;
      for (int v = 0; v < snap.count && numCursors < SamplerVoiceSnapshot::kMaxVisualVoices; v++)
      {
         if (snap.voices[v].amp < 0.002f)
            continue;
         cursors[numCursors++] = { snap.voices[v].position, snap.voices[v].amp };
      }
      // The playhead tracks the audition / free-running voice while it plays, and otherwise
      // parks at `position`, where the next audition starts.
      const float parked = std::clamp(n->position, n->start, n->end);
      const float playhead = (snap.selfActive && snap.selfPos >= 0.0f) ? snap.selfPos : parked;
      const TrimWaveInput in = DrawTrimWave("##samplerwave", n->wavePeaks, &n->start, &n->end, w, h, cursors,
                                            numCursors, playhead, T("No sample · drop or click"));
      if (!n->wavePeaks.Empty() && (in.activated || in.active))
      {
         const float target = std::clamp(in.frac, n->start, n->end);
         n->position = target;
         if (in.activated)
            n->TriggerPreview(target);
      }
   }



   // Waveform + slice markers for the Slicer. Clicking inside a slice's band
   // auditions that slice; in onsets mode each marker (except the pinned one
   // at 0) can be dragged. Grid boundaries are derived from the transport, so
   // dragging them is disabled rather than silently undone on the next
   // recompute.
   void DrawSlicerWaveform(SlicerNode* n, float h, float width)
   {
      static const char* kPitchClass[12] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;
      const std::vector<float>& slices = n->Slices();
      const int sliceCount = (int)slices.size();

      // Body click-catcher first, so it owns hover/active by default; the
      // marker grab-zones are added afterwards at the same screen position.
      // ImGui overlap resolution is NOT "last submitted wins" - a later item
      // is blocked while an earlier one already holds g.HoveredId unless that
      // earlier item opted in via SetNextItemAllowOverlap() *before* it.
      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      UiInteract::Hit("##slicerwavebody", ImVec2(w, h));
      if (hasSample && sliceCount > 0 && ImGui::IsItemActivated())
      {
         const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, 1.0f);
         int hit = 0;
         for (int i = 0; i < sliceCount; i++)
         {
            if (slices[i] <= frac)
               hit = i;
         }
         n->TriggerSlicePreview(hit);
      }
      if (hasSample && sliceCount > 0 && ImGui::IsItemHovered())
      {
         const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, 1.0f);
         int hit = 0;
         for (int i = 0; i < sliceCount; i++)
         {
            if (slices[i] <= frac)
               hit = i;
         }
         const int note = SlicerNode::kBaseNote + hit;
         char val[96];
         snprintf(val, sizeof(val), "slice %d - %s%d - click to audition", hit + 1,
                  kPitchClass[note % 12], note / 12 - 1);
         SetAudioReadout("#", val);
      }

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), tok::osc_slicer_waveform_stroke);

      if (hasSample)
      {
         // Alternating band shading behind the waveform, so "where does one
         // slice end" reads without counting marker lines.
         for (int i = 0; i < sliceCount; i += 2)
         {
            const float x0 = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            const float x1 = origin.x + w * ((i + 1 < sliceCount) ? std::clamp(slices[i + 1], 0.0f, 1.0f) : 1.0f);
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y),
                              isLight ? tok::Base(tok::col::ink_black, tok::osc_slicer_waveform_fill_alpha_lg) : tok::Base(tok::col::ink_white, tok::osc_slicer_waveform_fill_alpha_sm));
         }

         AudioViz::Peaks(dl, n->wavePeaks, origin, w, h);

         // Voices in flight, faded by their own amplitude.
         const SlicerVoiceSnapshot& snap = n->VisualSnapshot();
         for (int v = 0; v < snap.count; v++)
         {
            if (snap.voices[v].amp < 0.002f)
               continue;
            const float px = origin.x + w * std::clamp(snap.voices[v].position, 0.0f, 1.0f);
            const int alpha = (int)(std::clamp(snap.voices[v].amp, 0.0f, 1.0f) * 235.0f) + 20;
            dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y),
                        tok::U32(tok::osc_line_shade, isLight, alpha), tok::osc_slicer_waveform_line_stroke_lg);
         }

         // Slice markers, plus the note each slice answers to when it fits.
         const ImU32 markerCol = isLight ? tok::Base(tok::col::state_ok_deep_lo, 230) : tok::Base(tok::col::viz_teal_light, 220);
         for (int i = 0; i < sliceCount; i++)
         {
            const float x = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), markerCol, i == 0 ? tok::osc_slicer_waveform_stroke : tok::osc_slicer_waveform_line_stroke_sm);

            const float nextX = origin.x + w * ((i + 1 < sliceCount) ? std::clamp(slices[i + 1], 0.0f, 1.0f) : 1.0f);
            const int note = SlicerNode::kBaseNote + i;
            char label[24];
            snprintf(label, sizeof(label), "%s%d", kPitchClass[note % 12], note / 12 - 1);
            const ImVec2 ts = ImGui::CalcTextSize(label);
            if (nextX - x > ts.x + tok::space_1_5)
               dl->AddText(ImVec2(x + tok::space_0_75, origin.y + tok::space_0_5), ScopeTextCol(), label);
         }
      }
      else
      {
         EmptyState::DrawCaption(origin, br, T("No sample loaded"));
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);

      if (hasSample && n->MarkersAreEditable())
      {
         // Narrow grab-zones centred on each marker, submitted after the body
         // button above. Marker 0 is pinned at the sample's start.
         const float handleW = tok::osc_handle_w;
         for (int i = 1; i < sliceCount; i++)
         {
            const float x = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            const float btnX = std::clamp(x - handleW * 0.5f, origin.x, br.x - handleW);
            char id[32];
            snprintf(id, sizeof(id), "##slicermk%d", i);
            ImGui::SetCursorScreenPos(ImVec2(btnX, origin.y));
            UiInteract::Hit(id, ImVec2(handleW, h));
            if (ImGui::IsItemActivated())
               PushUndoCheckpoint();
            if (ImGui::IsItemActive())
               n->MoveSliceMarker(i, (ImGui::GetIO().MousePos.x - origin.x) / w);
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
               ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
         }
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }


   // Waveform and playhead renderer for PaulStretch extreme time-stretcher
   void DrawPaulStretchWaveform(PaulStretchNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;

      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      UiInteract::Hit("##paulstretchwavebody", ImVec2(w, h));
      if (hasSample && (ImGui::IsItemActivated() || (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))))
      {
         const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, 1.0f);
         const float target = (frac < n->start || frac > n->end) ? n->start : frac;
         n->position = target;
         n->Seek(target);
         if (!n->IsPlaying())
            n->TriggerPreview(target);
      }

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), tok::osc_paul_stretch_waveform_stroke);

      if (hasSample)
      {
         AudioViz::Peaks(dl, n->wavePeaks, origin, w, h, tok::osc_peaks_alpha_lg,
                         isLight ? tok::Base(tok::col::accent, tok::osc_audio_viz_alpha_lg) : tok::Base(tok::col::accent_pale_muted_cool, tok::osc_audio_viz_alpha_lg));

         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);
         const ImU32 dimCol = isLight ? tok::Base(tok::col::ink_white, 140) : tok::Base(tok::col::ink_black, 130);
         if (startX > origin.x)
            dl->AddRectFilled(origin, ImVec2(startX, br.y), dimCol);
         if (endX < br.x)
            dl->AddRectFilled(ImVec2(endX, origin.y), br, dimCol);

         const float px = origin.x + w * std::clamp(n->Playhead(), 0.0f, 1.0f);
         dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y),
                     isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation, tok::osc_paul_stretch_waveform_line_alpha_sm), tok::osc_paul_stretch_waveform_line_stroke);

         dl->AddLine(ImVec2(startX, origin.y), ImVec2(startX, br.y),
                     isLight ? tok::Base(tok::col::state_ok_deep_vivid) : tok::Base(tok::col::state_ok, tok::osc_paul_stretch_waveform_line_alpha_lg), tok::osc_paul_stretch_waveform_line_stroke);
         dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, br.y),
                     isLight ? tok::Base(tok::col::state_err) : tok::Base(tok::col::viz_pink, tok::osc_paul_stretch_waveform_line_alpha_lg), tok::osc_paul_stretch_waveform_line_stroke);
      }
      else
      {
         EmptyState::DrawCaption(origin, br, T("No sample loaded"));
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);

      if (hasSample)
      {
         const float handleW = tok::osc_paul_stretch_waveform_w;
         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);

         const float grip = tok::osc_grip;
         const float startGripX = std::clamp(startX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const float endGripX = std::clamp(endX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const ImU32 startCol = isLight ? tok::Base(tok::col::state_ok_deep_vivid) : tok::Base(tok::col::state_ok);
         const ImU32 endCol = isLight ? tok::Base(tok::col::state_err) : tok::Base(tok::col::viz_pink);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, origin.y), ImVec2(startGripX + grip * 0.5f, origin.y), ImVec2(startGripX, origin.y + grip), startCol);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, br.y), ImVec2(startGripX + grip * 0.5f, br.y), ImVec2(startGripX, br.y - grip), startCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, origin.y), ImVec2(endGripX + grip * 0.5f, origin.y), ImVec2(endGripX, origin.y + grip), endCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, br.y), ImVec2(endGripX + grip * 0.5f, br.y), ImVec2(endGripX, br.y - grip), endCol);

         const float startBtnX = std::clamp(startX - handleW * 0.5f, origin.x, br.x - handleW);
         const float endBtnX = std::clamp(endX - handleW * 0.5f, origin.x, br.x - handleW);

         ImGui::SetCursorScreenPos(ImVec2(startBtnX, origin.y));
         UiInteract::Hit("##paulstretchstarthandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
         {
            n->start = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, n->end - 0.01f);
            n->position = std::clamp(n->position, n->start, n->end);
         }
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

         ImGui::SetCursorScreenPos(ImVec2(endBtnX, origin.y));
         UiInteract::Hit("##paulstretchendhandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
         {
            n->end = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, n->start + 0.01f, 1.0f);
            n->position = std::clamp(n->position, n->start, n->end);
         }
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }


   // Waveform and real-time grain particle renderer for Granular synthesis node
   void DrawGranularWaveform(GranularNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;

      const float s = std::clamp(n->start, 0.0f, 0.99f);
      const float e = std::clamp(n->end, s + 0.01f, 1.0f);

      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      UiInteract::Hit("##granularwavebody", ImVec2(w, h));
      if (hasSample && (ImGui::IsItemActivated() || (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))))
      {
         const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, s, e);
         n->Seek(frac);
      }

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), tok::osc_granular_waveform_stroke);

      if (hasSample)
      {
         AudioViz::Peaks(dl, n->wavePeaks, origin, w, h, tok::osc_peaks_alpha_sm,
                         isLight ? tok::Base(tok::col::accent, tok::osc_audio_viz_alpha_md) : tok::Base(tok::col::accent_pale_muted_lo, tok::osc_audio_viz_alpha_sm));

         const float startX = origin.x + w * s;
         const float endX = origin.x + w * e;
         const ImU32 dimCol = isLight ? tok::Base(tok::col::ink_white, 140) : tok::Base(tok::col::ink_black, 140);
         if (startX > origin.x)
            dl->AddRectFilled(origin, ImVec2(startX, br.y), dimCol);
         if (endX < br.x)
            dl->AddRectFilled(ImVec2(endX, origin.y), br, dimCol);

         // Render active real-time grain particles/dots
         const auto& snap = n->VisualSnapshot();
         for (int g = 0; g < snap.count; ++g)
         {
            const auto& gr = snap.grains[g];
            if (gr.amp < 0.01f)
               continue;

            const float gx = origin.x + w * std::clamp(gr.position, s, e);
            const float gy = midY + (gr.pan * tok::osc_granular_waveform_ratio * h);
            const float radius = tok::space_0_5 + gr.amp * tok::osc_radius_ratio;

            // Outer glow
            const int glowAlpha = (int)(gr.amp * 80.0f);
            dl->AddCircleFilled(ImVec2(gx, gy), radius + tok::osc_granular_waveform_gap,
                                tok::U32(tok::osc_dot_shade_sm, isLight, glowAlpha));

            // Core grain dot
            const int coreAlpha = std::min(255, (int)(gr.amp * 255.0f));
            dl->AddCircleFilled(ImVec2(gx, gy), radius,
                                tok::U32(tok::osc_dot_shade_lg, isLight, coreAlpha));

            // Small direction indicator / velocity streak
            const float dirLen = (gr.pitchRatio > 1.05f ? 4.0f : (gr.pitchRatio < 0.95f ? -4.0f : 0.0f));
            if (std::abs(dirLen) > 0.1f)
            {
               dl->AddLine(ImVec2(gx - dirLen, gy), ImVec2(gx + dirLen, gy),
                           isLight ? tok::U32(tok::osc_line_shade_sm, (int)(gr.amp * tok::osc_granular_waveform_shade_lg)) : tok::U32(tok::osc_line_shade_md, (int)(gr.amp * tok::osc_granular_waveform_shade_sm)), tok::osc_granular_waveform_line_stroke_sm);
            }
         }

         // Current playing playhead (pos) strictly within [s, e]
         const float px = origin.x + w * std::clamp(n->Playhead(), s, e);
         dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y),
                     isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation, tok::osc_granular_waveform_line_alpha), tok::osc_granular_waveform_line_stroke_lg);
         const float pGrip = tok::osc_p_grip;
         const ImU32 pCol = isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation);
         dl->AddTriangleFilled(ImVec2(px - pGrip * 0.5f, origin.y), ImVec2(px + pGrip * 0.5f, origin.y), ImVec2(px, origin.y + pGrip), pCol);
         dl->AddTriangleFilled(ImVec2(px - pGrip * 0.5f, br.y), ImVec2(px + pGrip * 0.5f, br.y), ImVec2(px, br.y - pGrip), pCol);

         dl->AddLine(ImVec2(startX, origin.y), ImVec2(startX, br.y),
                     isLight ? tok::Base(tok::col::state_ok_deep_vivid) : tok::Base(tok::col::state_ok, tok::osc_granular_waveform_line_alpha), tok::osc_granular_waveform_line_stroke_lg);
         dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, br.y),
                     isLight ? tok::Base(tok::col::state_err) : tok::Base(tok::col::viz_pink, tok::osc_granular_waveform_line_alpha), tok::osc_granular_waveform_line_stroke_lg);
      }
      else
      {
         EmptyState::DrawCaption(origin, br, T("No sample loaded"));
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);

      if (hasSample)
      {
         const float handleW = tok::osc_paul_stretch_waveform_w;
         const float startX = origin.x + w * s;
         const float endX = origin.x + w * e;

         const float grip = tok::osc_grip;
         const float startGripX = std::clamp(startX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const float endGripX = std::clamp(endX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const ImU32 startCol = isLight ? tok::Base(tok::col::state_ok_deep_vivid) : tok::Base(tok::col::state_ok);
         const ImU32 endCol = isLight ? tok::Base(tok::col::state_err) : tok::Base(tok::col::viz_pink);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, origin.y), ImVec2(startGripX + grip * 0.5f, origin.y), ImVec2(startGripX, origin.y + grip), startCol);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, br.y), ImVec2(startGripX + grip * 0.5f, br.y), ImVec2(startGripX, br.y - grip), startCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, origin.y), ImVec2(endGripX + grip * 0.5f, origin.y), ImVec2(endGripX, origin.y + grip), endCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, br.y), ImVec2(endGripX + grip * 0.5f, br.y), ImVec2(endGripX, br.y - grip), endCol);

         const float startBtnX = std::clamp(startX - handleW * 0.5f, origin.x, br.x - handleW);
         const float endBtnX = std::clamp(endX - handleW * 0.5f, origin.x, br.x - handleW);

         ImGui::SetCursorScreenPos(ImVec2(startBtnX, origin.y));
         UiInteract::Hit("##granularstarthandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
         {
            n->start = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, n->end - 0.01f);
            if (n->position < n->start)
               n->Seek(n->start);
         }
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

         ImGui::SetCursorScreenPos(ImVec2(endBtnX, origin.y));
         UiInteract::Hit("##granularendhandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
         {
            n->end = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, n->start + 0.01f, 1.0f);
            if (n->position > n->end)
               n->Seek(n->end);
         }
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }


   // Narrow vertical meter drawn beside a channel fader. Same traffic-light
   // reading as a horizontal meter, rotated - a mixer strip without one is
   // just a row of numbers you can't check against the sound.
   void DrawStripMeter(float x, float y, float w, float h, float level)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 tl(x, y), br(x + w, y + h);
      AudioViz::Fill(dl, tl, br);

      // Calibrated to a fixed -60..0 dBFS window rather than linear
      // amplitude - a linear meter crushes the bottom 40 dB of useful range
      // into the lowest 6% of pixels (a comfortably-audible -24 dBFS signal
      // used to light only 6% of the strip).
      auto dBToFrac = [](float db) { return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f); };
      auto levelToDb = [](float lvl) { return lvl > 1e-4f ? 20.0f * log10f(lvl) : -120.0f; };

      // Ticks at -24/-12/-6/0 dBFS, so an unlit meter still reads as a
      // calibrated scale rather than an empty slot (§3f).
      static const float kTicksDb[] = { -24.0f, -12.0f, -6.0f, 0.0f };
      for (float db : kTicksDb)
      {
         const float t = dBToFrac(db);
         dl->AddLine(ImVec2(x, br.y - h * t), ImVec2(br.x, br.y - h * t), ScopeMidLineCol(), tok::osc_strip_meter_stroke);
      }
      const float db = levelToDb(std::max(0.0f, level));
      const float lvl = dBToFrac(db);
      if (lvl > 0.0f)
      {
         // Thresholds are the same -6 / -1 dBFS the old linear constants
         // (0.501, 0.891) encoded, just expressed directly in dB now.
         const ImU32 col = db > -1.0f   ? tok::Role(tok::col::text_err, 245)
                           : db > -6.0f ? tok::Role(tok::col::role_modulation, 240)
                                        : tok::Role(tok::col::state_ok, 235);
         dl->AddRectFilled(ImVec2(x + 1.0f, br.y - 1.0f - (h - tok::space_0_5) * lvl),
                           ImVec2(br.x - 1.0f, br.y - 1.0f), col, tok::radius_cell);
      }
      AudioViz::Border(dl, tl, br);
   }


   // The wavetable itself, drawn as a receding stack of single-cycle traces
   // with the frame the `position` knob currently sits on picked out, and
   // draggable: dragging left/right across it scrubs `position` directly.
   //
   // This is the node's primary visualizer, and its primary control, for the
   // same reason every wavetable synth makes it one: `position` is the
   // parameter the whole node is about, and a number between 0 and 1 says
   // nothing about what it will sound like while the shape it lands on says it
   // immediately. Setting it anywhere but on the picture is indirection for
   // its own sake.
   //
   // Everything here is computed on the main thread from the immutable bank
   // and the node's own params - no audio-thread data is read at all, so it
   // redraws per frame like the ADSR shape does (§3, RT-safety).
   void PublishWtTestRect()
   {
      const ImVec2 mn = ImGui::GetItemRectMin();
      const ImVec2 mx = ImGui::GetItemRectMax();
      gWtTestRects.push_back(ImVec4(mn.x, mn.y, mx.x, mx.y));
   }


   void DrawWavetableFrames(WavetableEngine& eng, const char* id, float h, float width, bool dim)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      // ImGui's style alpha does not reach ImDrawList calls, so an engine
      // switched off has to be dimmed here rather than by wrapping the call in
      // PushStyleVar(ImGuiStyleVar_Alpha) - which is what shipped first, and
      // left an off engine's table looking exactly as live as an on one.
      const int fade = dim ? 90 : 255;
      auto Fade = [fade](ImU32 c) {
         const unsigned a = (c >> IM_COL32_A_SHIFT) & 0xFF;
         return (c & ~IM_COL32_A_MASK) | ((a * (unsigned)fade / 255u) << IM_COL32_A_SHIFT);
      };
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);

      UiInteract::Hit(id, ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();
      if (ImGui::IsItemActive())
      {
         const float x = std::clamp((ImGui::GetIO().MousePos.x - origin.x - tok::space_2_5) / std::max(1.0f, w - tok::osc_wavetable_frames_gap),
                                    0.0f, 1.0f);
         eng.position = x;
      }

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      // Perspective: each successive frame steps right and up by a fixed
      // amount, so the stack reads as depth rather than as overlaid curves.
      const float headroom = 20.0f;
      const float stepX = (w * tok::osc_wavetable_frames_ratio) / (float)(Wavetable::kFrames - 1);
      const float stepY = (h - headroom - tok::osc_wavetable_frames_step_y) / (float)(Wavetable::kFrames - 1);
      const float traceW = w - stepX * (float)(Wavetable::kFrames - 1) - tok::osc_trace_w_gap;
      const float amp = (h - headroom) * 0.17f;
      const float selected = std::clamp(eng.position, 0.0f, 1.0f) * (float)(Wavetable::kFrames - 1);

      const int kSteps = 96;
      for (int f = Wavetable::kFrames - 1; f >= 0; f--)
      {
         const float baseX = origin.x + tok::space_2_5 + stepX * (float)f;
         const float baseY = br.y - tok::space_3_5 - stepY * (float)f;
         const float distance = fabsf((float)f - selected);
         const bool isCurrent = distance < 0.5f;
         const float focus = std::max(0.0f, 1.0f - distance * 0.55f);

         const float* frame = Wavetable::Frame(eng.table, f, 0);
         dl->PathClear();
         for (int i = 0; i <= kSteps; i++)
         {
            const float t = (float)i / (float)kSteps;
            const int idx = std::min(Wavetable::kFrameSize - 1, (int)(t * (float)Wavetable::kFrameSize));
            dl->PathLineTo(ImVec2(baseX + t * traceW, baseY - frame[idx] * amp));
         }
         const int alpha = isCurrent ? 255 : (int)(40.0f + focus * 120.0f);
         const ImU32 frameStroke = isLight
            ? (isCurrent ? tok::Base(tok::col::accent) : tok::U32(tok::osc_frame_stroke_shade_sm, alpha))
            : (isCurrent ? tok::Role(tok::col::accent_alt) : tok::U32(tok::osc_frame_stroke_shade_lg, alpha));
         dl->PathStroke(Fade(frameStroke), 0, isCurrent ? tok::osc_wavetable_frames_stroke : 1.0f);
      }

      // Scrub handle along the bottom: without it the display looks like a
      // picture, and nothing says it can be dragged.
      const float handleX = origin.x + tok::space_2_5 + std::clamp(eng.position, 0.0f, 1.0f) * (w - tok::osc_wavetable_frames_gap);
      dl->AddLine(ImVec2(handleX, origin.y + tok::space_0_75), ImVec2(handleX, br.y - tok::space_0_75),
                  isLight ? tok::U32(tok::osc_line_shade_xs, hovered ? tok::osc_wavetable_frames_col_7_alpha : tok::osc_wavetable_frames_line_alpha_lg) : tok::U32(tok::osc_line_shade_lg, hovered ? tok::osc_wavetable_frames_col_8_alpha : tok::osc_wavetable_frames_line_alpha_sm), tok::osc_wavetable_frames_line_stroke);
      const ImU32 triCol = isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt);
      dl->AddTriangleFilled(ImVec2(handleX - tok::space_1_25, br.y - tok::space_0_5), ImVec2(handleX + tok::space_1_25, br.y - tok::space_0_5),
                            ImVec2(handleX, br.y - tok::osc_text_x_sm), Fade(triCol));

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br, hovered ? tok::Role(tok::col::accent_hover) : 0);
      dl->AddText(ImVec2(origin.x + tok::osc_text_x_sm, origin.y + tok::space_1_25),
                  Fade(isLight ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::accent_pale_muted_hi)),
                  Wavetable::TableName(eng.table));
      char posText[24];
      snprintf(posText, sizeof(posText), "%s", UiFmt::F3(eng.position).c);
      const ImVec2 sz = ImGui::CalcTextSize(posText);
      dl->AddText(ImVec2(br.x - tok::osc_text_x_sm - sz.x, origin.y + tok::space_1_25),
                  isLight ? tok::Base(tok::col::edge_lo_light) : tok::Base(tok::col::text_secondary), posText);
      if (hovered)
         SetAudioReadout("position", posText);
   }


   ADSRLayout ComputeADSRLayout(ImVec2 origin, float w, float h, float attackMs, float decayMs,
                                       float sustain, float releaseMs, float maxTimeMs)
   {
      ADSRLayout l;
      const float padX = tok::osc_pad_x;
      const float padY = tok::space_1_25;
      l.x0 = origin.x + padX;
      const float xEnd = origin.x + w - padX;
      const float usableW = std::max(tok::osc_usable_w_min, xEnd - l.x0);
      l.topY = origin.y + padY;
      l.baseY = origin.y + h - padY;
      l.spanY = std::max(1.0f, l.baseY - l.topY);

      const float s01 = std::clamp(sustain, 0.0f, 1.0f);
      const float susY = l.topY + (1.0f - s01) * l.spanY;

      // Proportional sustain plateau representing key-held state (18% of usable width)
      l.wShelf = std::clamp(usableW * tok::osc_compute_adsr_layout_ratio, 14.0f, 36.0f);
      const float timeW = usableW - l.wShelf;
      l.timeW = timeW;

      // Perceptual duration power curve (t^0.45): provides balanced visual proportion across 0..4000ms
      auto PowWeight = [&](float ms) {
         const float t01 = std::clamp(ms / maxTimeMs, 0.0f, 1.0f);
         return std::pow(t01, 0.45f);
      };

      const float wA_raw = PowWeight(attackMs);
      const float wD_raw = PowWeight(decayMs);
      const float wR_raw = PowWeight(releaseMs);
      const float totalW = wA_raw + wD_raw + wR_raw;

      if (totalW <= 0.0f)
      {
         // A=D=R=0: nothing to distribute. Lay the shape out as a vertical
         // rise, the shelf, and a vertical fall instead of dividing by zero.
         l.wA = 0.0f;
         l.wD = 0.0f;
         l.wR = 0.0f;
      }
      else
      {
         // A 0 ms stage gets exactly 0 width - no floor, so it reads as a
         // true vertical edge instead of a fake sliver of slope.
         l.wA = attackMs > 0.0f ? (wA_raw / totalW) * timeW : 0.0f;
         l.wD = decayMs > 0.0f ? (wD_raw / totalW) * timeW : 0.0f;
         l.wR = releaseMs > 0.0f ? (wR_raw / totalW) * timeW : 0.0f;

         // Per-stage pixel floor so a short-but-nonzero stage stays
         // grabbable, applied only to the nonzero stages, then rescaled so
         // the three still sum to timeW.
         const float kMinPx = tok::osc_min_px;
         if (attackMs > 0.0f) l.wA = std::max(l.wA, kMinPx);
         if (decayMs > 0.0f) l.wD = std::max(l.wD, kMinPx);
         if (releaseMs > 0.0f) l.wR = std::max(l.wR, kMinPx);

         const float flooredSum = l.wA + l.wD + l.wR;
         if (flooredSum > 0.0f)
         {
            const float scale = timeW / flooredSum;
            l.wA *= scale;
            l.wD *= scale;
            l.wR *= scale;
         }
      }

      const float ax = l.x0 + l.wA;
      const float dx = ax + l.wD;
      const float sx = dx + l.wShelf;
      const float rx = sx + l.wR;

      l.p0 = ImVec2(l.x0, l.baseY);
      l.pA = ImVec2(ax, l.topY);
      l.pD = ImVec2(dx, susY);
      l.pS = ImVec2(sx, susY);
      l.pR = ImVec2(rx, l.baseY);

      return l;
   }


   // Directly editable ADSR: interactive handles with proportional relative-time
   // display, analog exponential decay/release curves, and smooth responsive dragging.
   void DrawEditableADSR(const char* id, float* attackMs, float* decayMs, float* sustain,
                         float* releaseMs, float maxTimeMs, float h, ImU32 color, float width,
                         const ::ModShapes::Ahdsr* shape = nullptr)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      UiInteract::Hit(id, ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();

      const bool isLight = IsThemeLight();
      // Sleek rounded background
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      // Subtle horizontal level guidelines at 25%, 50%, 75%
      for (int i = 1; i < 4; i++)
      {
         const float gy = origin.y + h * (i / 4.0f);
         dl->AddLine(ImVec2(origin.x + tok::space_0_5, gy), ImVec2(br.x - tok::space_0_5, gy),
                     ScopeGridCol(), tok::osc_editable_adsr_stroke);
      }

      ADSRLayout layout = ComputeADSRLayout(origin, w, h, *attackMs, *decayMs, *sustain, *releaseMs, maxTimeMs);

      // Subtle sustain gate vertical zone highlight
      dl->AddRectFilled(ImVec2(layout.pD.x, layout.topY), ImVec2(layout.pS.x, layout.baseY),
                        isLight ? tok::Base(tok::col::ink_black, tok::osc_editable_adsr_fill_alpha_lg) : tok::Base(tok::col::ink_white, tok::osc_editable_adsr_fill_alpha_sm));

      // Latch nearest handle on press and support smooth dragging
      static int sHeld = -1;
      static ImGuiID sHeldItem = 0;
      static ImVec2 sPress(0.0f, 0.0f);
      static float sInitialVal = 0.0f;
      static float sInitialSus = 0.0f;
      const ImGuiID thisItem = ImGui::GetItemID();
      const ImVec2 m = ImGui::GetIO().MousePos;

      int hoveredHandle = -1;
      if (hovered)
      {
         const float d0 = ImLengthSqr(ImVec2(layout.pA.x - m.x, layout.pA.y - m.y));
         const float d1 = ImLengthSqr(ImVec2(layout.pD.x - m.x, layout.pD.y - m.y));
         const float d3 = ImLengthSqr(ImVec2(layout.pR.x - m.x, layout.pR.y - m.y));
         if (d0 < 140.0f) hoveredHandle = 0;
         else if (d1 < 140.0f) hoveredHandle = 1;
         else if (d3 < 140.0f) hoveredHandle = 3;
         else if (m.x >= layout.pD.x - tok::space_0_5 && m.x <= layout.pS.x + tok::space_0_5 && fabsf(m.y - layout.pD.y) < tok::osc_editable_adsr_limit)
            hoveredHandle = 2;
      }

      if (active)
      {
         if (ImGui::IsItemActivated() || sHeldItem != thisItem)
         {
            sHeldItem = thisItem;
            sPress = m;
            const float d0 = ImLengthSqr(ImVec2(layout.pA.x - m.x, layout.pA.y - m.y));
            const float d1 = ImLengthSqr(ImVec2(layout.pD.x - m.x, layout.pD.y - m.y));
            const float d3 = ImLengthSqr(ImVec2(layout.pR.x - m.x, layout.pR.y - m.y));
            // Zero-width stages can land two handles on the same point (or
            // within a couple px of each other once the floor in
            // ComputeADSRLayout stops padding them apart). Plain nearest-wins
            // with a strict "<" always resolves those ties to attack, which
            // makes the decay handle permanently ungrabbable. Break ties by
            // preferring whichever candidate's current time value is
            // smaller, so a collapsed stack hands you the zero-length stage
            // first and dragging right immediately grows it.
            struct Candidate { float dist; int handle; float timeVal; };
            Candidate cands[3] = {
               { std::sqrt(d0), 0, *attackMs },
               { std::sqrt(d1), 1, *decayMs },
               { std::sqrt(d3), 3, *releaseMs },
            };
            int bestIdx = 0;
            for (int i = 1; i < 3; i++)
            {
               const float kTieEpsPx = tok::osc_tie_eps_px;
               if (cands[i].dist < cands[bestIdx].dist - kTieEpsPx)
                  bestIdx = i;
               else if (cands[i].dist < cands[bestIdx].dist + kTieEpsPx &&
                        cands[i].timeVal < cands[bestIdx].timeVal)
                  bestIdx = i;
            }
            sHeld = cands[bestIdx].handle;
            float best = cands[bestIdx].dist * cands[bestIdx].dist;
            if (best > 160.0f && m.x >= layout.pD.x && m.x <= layout.pS.x)
            {
               sHeld = 2; // sustain plateau
            }
            if (sHeld == 0) sInitialVal = *attackMs;
            else if (sHeld == 1) { sInitialVal = *decayMs; sInitialSus = *sustain; }
            else if (sHeld == 2) sInitialSus = *sustain;
            else if (sHeld == 3) sInitialVal = *releaseMs;
         }

         const float dx = m.x - sPress.x;
         const float dy = m.y - sPress.y;

         auto AdjustTime = [&](float initialVal, float deltaX) -> float {
            float curWeight = std::pow(std::clamp(initialVal / maxTimeMs, 0.0f, 1.0f), 0.45f);
            // Scale the drag by the width actually available to the three
            // stages (timeW), not the whole widget width - w also includes
            // the padding and the sustain shelf, so a drag was previously
            // undershooting the pixels the handle is actually moving across.
            curWeight = std::clamp(curWeight + deltaX / (layout.timeW * tok::osc_editable_adsr_ratio), 0.0f, 1.0f);
            return std::pow(curWeight, 2.222f) * maxTimeMs;
         };

         switch (sHeld)
         {
            case 0:
               *attackMs = AdjustTime(sInitialVal, dx);
               break;
            case 1:
               *decayMs = AdjustTime(sInitialVal, dx);
               *sustain = std::clamp(sInitialSus - dy / layout.spanY, 0.0f, 1.0f);
               break;
            case 2:
               *sustain = std::clamp(sInitialSus - dy / layout.spanY, 0.0f, 1.0f);
               break;
            case 3:
               *releaseMs = AdjustTime(sInitialVal, dx);
               break;
         }

         // Recompute layout for updated values so visual matches drag immediately
         layout = ComputeADSRLayout(origin, w, h, *attackMs, *decayMs, *sustain, *releaseMs, maxTimeMs);
      }

      // Build multi-segment curve points (Attack rise, Decay, Sustain shelf,
      // Release). Linear segments unless `shape` is given: the synth
      // envelopes (AudioVoice.h) step linearly, so a curved display there
      // would be drawing a shape the audio never makes. The Envelope node
      // passes its hold and curves, drawn with the same ModShapes maths
      // its output uses; hold takes the front of the decay's width.
      const float aCurve = shape ? shape->attackCurve : 0.0f;
      const float dCurve = shape ? shape->decayCurve : 0.0f;
      const float rCurve = shape ? shape->releaseCurve : 0.0f;
      const float holdMs = shape ? std::max(0.0f, shape->holdMs) : 0.0f;
      const float holdFrac = holdMs > 0.0f ? holdMs / (holdMs + std::max(0.0f, *decayMs)) : 0.0f;
      const float s01 = std::clamp(*sustain, 0.0f, 1.0f);
      std::vector<ImVec2> curvePts;
      curvePts.reserve(24);

      // 1. Attack. A zero-width stage emits just its two endpoints - a
      // stack of coincident interpolated points at the same x antialiases
      // into a fuzzy vertical bar instead of a crisp vertical edge.
      curvePts.push_back(ImVec2(layout.x0, layout.baseY));
      if (layout.pA.x - layout.x0 >= tok::osc_editable_adsr_limit_sm)
      {
         const int kAttSteps = shape ? 16 : 6;
         for (int i = 1; i < kAttSteps; i++)
         {
            const float u = (float)i / (float)kAttSteps;
            const float px = layout.x0 + u * (layout.pA.x - layout.x0);
            const float py = layout.baseY - ::ModShapes::MsegSegment(0.0f, 1.0f, aCurve, u) * layout.spanY;
            curvePts.push_back(ImVec2(px, py));
         }
      }
      curvePts.push_back(layout.pA);

      // 2. Decay.
      if (layout.pD.x - layout.pA.x >= tok::osc_editable_adsr_limit_sm)
      {
         const int kDecSteps = shape ? 20 : 10;
         for (int i = 1; i < kDecSteps; i++)
         {
            const float u = (float)i / (float)kDecSteps;
            const float px = layout.pA.x + u * (layout.pD.x - layout.pA.x);
            const float k = holdFrac >= 1.0f ? 0.0f : std::max(0.0f, (u - holdFrac) / (1.0f - holdFrac));
            const float py = layout.baseY - ::ModShapes::MsegSegment(1.0f, s01, dCurve, k) * layout.spanY;
            curvePts.push_back(ImVec2(px, py));
         }
      }
      curvePts.push_back(layout.pD);

      // 3. Sustain shelf
      curvePts.push_back(layout.pS);

      // 4. Release.
      if (layout.pR.x - layout.pS.x >= tok::osc_editable_adsr_limit_sm)
      {
         const int kRelSteps = shape ? 16 : 10;
         for (int i = 1; i < kRelSteps; i++)
         {
            const float u = (float)i / (float)kRelSteps;
            const float px = layout.pS.x + u * (layout.pR.x - layout.pS.x);
            const float py = layout.baseY - ::ModShapes::MsegSegment(s01, 0.0f, rCurve, u) * layout.spanY;
            curvePts.push_back(ImVec2(px, py));
         }
      }
      curvePts.push_back(layout.pR);

      // Draw glowing shaded fill under curve
      dl->PathClear();
      dl->PathLineTo(ImVec2(layout.x0, layout.baseY));
      for (const ImVec2& pt : curvePts)
         dl->PathLineTo(pt);
      dl->PathLineTo(ImVec2(layout.pR.x, layout.baseY));
      const ImU32 fillAlpha = isLight ? 0x4A000000u : 0x2E000000u;
      const ImU32 fillCol = (color & 0x00FFFFFFu) | fillAlpha;
      dl->PathFillConvex(fillCol);

      // Draw antialiased curve stroke
      dl->PathClear();
      for (const ImVec2& pt : curvePts)
         dl->PathLineTo(pt);
      dl->PathStroke(color, 0, tok::osc_editable_adsr_path_stroke);

      // Draw interactive handles
      const ImVec2 handlePts[4] = { layout.pA, layout.pD, layout.pS, layout.pR };
      const int activeHandle = active ? (sHeld == 2 ? 1 : sHeld) : hoveredHandle;

      for (int i = 0; i < 4; i++)
      {
         const bool isHot = (active && (sHeld == i || (sHeld == 2 && (i == 1 || i == 2)))) ||
                            (!active && (hoveredHandle == i || (hoveredHandle == 2 && (i == 1 || i == 2))));
         const float radius = isHot ? tok::osc_radius : tok::osc_editable_adsr_radius;

         // Outer halo on hover/drag
         if (isHot)
            dl->AddCircleFilled(handlePts[i], radius + tok::space_0_75, (color & 0x00FFFFFFu) | 0x40000000u, tok::osc_editable_adsr_segments);  // ui-audit-ok: channel mask / bit packing, not a colour value

         dl->AddCircleFilled(handlePts[i], radius, isLight ? tok::Base(tok::col::ink_white) : tok::Base(tok::col::ink_white), tok::osc_editable_adsr_segments);
         dl->AddCircle(handlePts[i], radius, isLight ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::edge_deep, tok::osc_ring_alpha), tok::osc_editable_adsr_segments, tok::osc_ring_stroke);
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br, (hovered || active) ? tok::Role(tok::col::accent_hover) : 0);

      // State the panel's timebase so the fit-to-width layout is honest
      // instead of hidden: without this, a 200 ms envelope and an 8 s
      // envelope draw identically.
      {
         const float totalMs = *attackMs + *decayMs + *releaseMs;
         char timeText[24];
         if (totalMs >= 1000.0f)
            snprintf(timeText, sizeof(timeText), "%s", UiFmt::Sec(totalMs / 1000.0f).c);
         else
            snprintf(timeText, sizeof(timeText), "%s", UiFmt::Ms(totalMs).c);
         const ImVec2 tsz = ImGui::CalcTextSize(timeText);
         // Top-right normally; bottom-centre (under the plateau) when a high sustain puts the plateau along the top. The release always ends at the bottom-right corner and the attack starts bottom-left, so neither bottom corner is free.
         const bool highSustain = *sustain > 0.6f;
         const ImVec2 labelPos = highSustain ? ImVec2(origin.x + (br.x - origin.x - tsz.x) * tok::osc_editable_adsr_ratio, br.y - tok::space_1 - tsz.y)
                                             : ImVec2(br.x - tok::space_1_5 - tsz.x, origin.y + tok::space_1);
         dl->AddText(labelPos,
                     isLight ? tok::Base(tok::col::edge_lo_light, tok::osc_editable_adsr_text_alpha_lg) : tok::Base(tok::col::text_secondary, tok::osc_editable_adsr_text_alpha_sm), timeText);
      }

      // Rich Readout & Status info
      if (hovered || active)
      {
         char buf[96];
         if (activeHandle == 0)
            snprintf(buf, sizeof(buf), "Attack: %s", UiFmt::Ms(*attackMs).c);
         else if (activeHandle == 1 || activeHandle == 2)
            snprintf(buf, sizeof(buf), "Decay: %s | Sustain: %s", UiFmt::Ms(*decayMs).c, UiFmt::F2(*sustain).c);
         else if (activeHandle == 3)
            snprintf(buf, sizeof(buf), "Release: %s", UiFmt::Ms(*releaseMs).c);
         else
            snprintf(buf, sizeof(buf), "A:%s  D:%s  S:%s  R:%s", UiFmt::Ms(*attackMs).c, UiFmt::Ms(*decayMs).c, UiFmt::F2(*sustain).c, UiFmt::Ms(*releaseMs).c);

         SetAudioReadout("ADSR", buf);
      }
   }


   // ---- Wavetable --------------------------------------------------------
   const std::vector<std::string>& WavetableNames()
   {
      static std::vector<std::string> names;
      if (names.empty())
      {
         Wavetable::EnsureBuilt();
         for (int i = 0; i < Wavetable::NumTables(); i++)
            names.push_back(Wavetable::TableName(i));
      }
      return names;
   }


   const std::vector<std::string>& WavetableCategories()
   {
      static std::vector<std::string> cats;
      if (cats.empty())
      {
         Wavetable::EnsureBuilt();
         for (int i = 0; i < Wavetable::NumTables(); i++)
            cats.push_back(Wavetable::TableCategoryName(i));
      }
      return cats;
   }


   // Header tuning dropdowns. The values carry their own prefix inside the
   // button ("oct +1", not "+1") so the header needs no caption row under it -
   // a second line of captions is most of a header's height for four controls
   // that already say what they are.
   const std::vector<std::string>& OctaveNames()
   {
      static std::vector<std::string> names;
      if (names.empty())
      {
         for (int i = -4; i <= 4; i++)
         {
            char b[16];
            snprintf(b, sizeof(b), "oct %s", UiFmt::SInt(i).c);
            names.push_back(b);
         }
      }
      return names;
   }


   const std::vector<std::string>& SemiNames()
   {
      static std::vector<std::string> names;
      if (names.empty())
      {
         for (int i = -12; i <= 12; i++)
         {
            char b[16];
            snprintf(b, sizeof(b), "semi %s", UiFmt::SInt(i).c);
            names.push_back(b);
         }
      }
      return names;
   }


   // A dropdown button with no caption of its own, for header rows where the
   // button's own text is the label.
   void AudioBareDropdown(const char* id, const std::vector<std::string>& options, int current,
                          std::function<void(int)> onSelect, float width,
                          const std::vector<std::string>& categories,
                          bool focusSearch)
   {
      if (options.empty())
         return;
      const int lastIndex = (int)options.size() - 1;
      int safe = std::max(0, std::min(current, lastIndex));

      // Header selectors (octave, semitone, wavetable, waveform) are enum
      // params like any other - without this they were the one dropdown shape
      // left that a cable could not reach, which is why octave and semitone
      // could not be modulated.
      const DiscreteParamHandle h =
         RegisterDiscreteParam(id, (float)safe, (float)lastIndex, /*isBool=*/false, &options);
      if (h.registered)
      {
         if (h.driven)
         {
            const int drivenIdx = std::clamp((int)lroundf(h.value), 0, lastIndex);
            if (drivenIdx != current && onSelect)
            {
               // Modulation never creates an undo entry or dirties the patch:
               // most onSelect lambdas open with PushUndoCheckpoint() for the
               // user-click path, so suppress it here. The param write still runs.
               const bool wasSuppressed = gSuppressUndoCheckpoints;
               gSuppressUndoCheckpoints = true;
               onSelect(drivenIdx);
               gSuppressUndoCheckpoints = wasSuppressed;
            }
            safe = drivenIdx;
         }
         if (!h.draw)
            return;
         DrawDiscreteParamPin(h, id, width, ImGui::GetFrameHeight());
         // The pin consumes pin_box + 4; the field takes the rest so pin + field fill exactly the cell and the next cell on the row starts where a slider's would.
         width = std::max(tok::osc_audio_bare_dropdown_min, width - (tok::pin_box + tok::space_1));
      }

      const std::string caption = options[safe] + "##" + id;
      if (h.modulated)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, IsThemeLight() ? tok::BaseV4(tok::col::text_modulation)
                                                             : tok::BaseV4(tok::col::role_modulation));
         ImGui::BeginDisabled();
         NodeDropdownField(caption.c_str(), ImVec2(width, 0));
         ImGui::EndDisabled();
         ImGui::PopStyleColor();
         DrawModulationBindingMenu(h.nodeIndex, h.paramIndex,
                                   ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(),
                                                              ImGui::GetItemRectMax()));
         return;
      }
      if (NodeDropdownField(caption.c_str(), ImVec2(width, 0)))
      {
         gDropdown.options = options;
         gDropdown.categories = categories;
         gDropdown.onSelect = std::move(onSelect);
         gDropdown.current = safe;
         gDropdown.justOpened = true;
         gDropdown.focusSearch = focusSearch;
         gDropdown.filterBuf[0] = '\0';
      }
      if (h.registered)
         DrawModulationBindingMenu(h.nodeIndex, h.paramIndex, ImGui::IsItemHovered());
   }


   // One envelope panel: the editable curve on the left, its four (or five)
   // fields stacked to the right of it, which is the arrangement the reference
   // sketch asks for. Putting the fields *under* the curve - what shipped
   // first - made each envelope 150px tall and pushed the third one off the
   // bottom of any reasonable node.
   //
   // `amount` is optional: the amp envelope has no depth control (its depth is
   // the note), the pitch and filter envelopes do.
   void DrawEnvelopePanel(const char* title, const char* curveId, float* attackMs, float* decayMs,
                          float* sustain, float* releaseMs, float* amount, float amountLo,
                          float amountHi, const char* amountFmt, ImU32 color,
                          const ::ModShapes::Ahdsr* shape);

   // The curve-and-fields block with no section chrome, so a node that wants
   // its own controls in the same section (FM's ratio and fixed switch) draws
   // the very same component.
   void DrawEnvelopeFields(const char* curveId, float* attackMs, float* decayMs, float* sustain,
                           float* releaseMs, float* amount, float amountLo, float amountHi,
                           const char* amountFmt, ImU32 color, const ::ModShapes::Ahdsr* shape)
   {
      const float full = gAudioContentW;
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      // Wide enough that the widest label/value pair a field holds in practice
      // ("release" + "2249 ms") fits its *track*. The trap: ModSlider spends
      // the first 18px of whatever width it is given on the modulation pin, so
      // the track is 18px narrower than the cell - sizing the cell against the
      // text alone (what 0.44, then 0.52, did) leaves the text overlapping
      // even though the arithmetic looked right.
      const float fieldsW = std::clamp(full * tok::osc_envelope_fields_ratio, tok::osc_fields_w_lo, tok::osc_fields_w_hi);
      const float curveW = full - fieldsW - gap;
      const float halfW = (fieldsW - gap) * 0.5f;
      const float rowH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
      // Every envelope viewer on the node is the same size, so the three read
      // as three views of one kind of thing rather than as three differently
      // important panels. Sizing each curve to its own field stack instead
      // (which is what `amount != nullptr ? 3 : 2` did) made the amp envelope
      // a row shorter than the pitch and filter ones purely because it has no
      // depth control - a difference in the *picture* caused by a difference
      // in the controls, which is exactly the wrong thing to encode visually.
      const float kFieldRows = tok::osc_field_rows;
      const float curveH = rowH * kFieldRows - ImGui::GetStyle().ItemSpacing.y;

      const ImVec2 top = ImGui::GetCursorScreenPos();
      DrawEditableADSR(curveId, attackMs, decayMs, sustain, releaseMs, 4000.0f, curveH, color, curveW, shape);
      PublishWtTestRect();

      // Every field is positioned explicitly: ImGui would otherwise wrap each
      // row back to the column's left edge, under the curve.
      const float fieldsX = top.x + curveW + gap;
      // A two-row stack (no depth control) is centred against the fixed-height
      // curve rather than hung from its top, so the panel reads as one block.
      float y = top.y + (amount != nullptr ? 0.0f : rowH * 0.5f);
      if (amount != nullptr)
      {
         ImGui::SetCursorScreenPos(ImVec2(fieldsX, y));
         AudioSlider("amount", amount, amountLo, amountHi, amountFmt, fieldsW);
         y += rowH;
      }
      ImGui::SetCursorScreenPos(ImVec2(fieldsX, y));
      AudioSlider("attack", attackMs, params::osc::attack_lo, params::osc::attack_hi, "%.0f ms", halfW, SkewAttack100Taper::PosToValue, SkewAttack100Taper::ValueToPos);
      ImGui::SameLine();
      AudioSlider("decay", decayMs, params::osc::decay_lo, params::osc::decay_hi, "%.0f ms", halfW, SkewDecay300Taper::PosToValue, SkewDecay300Taper::ValueToPos);
      y += rowH;
      ImGui::SetCursorScreenPos(ImVec2(fieldsX, y));
      AudioSlider("sustain", sustain, 0.0f, 1.0f, "%.3f", halfW);
      ImGui::SameLine();
      AudioSlider("release", releaseMs, params::osc::release_lo, params::osc::release_hi, "%.0f ms", halfW, SkewRelease400Taper::PosToValue, SkewRelease400Taper::ValueToPos);

      // Close the row at whichever side is taller, so EndAudioSection measures
      // the panel rather than just the field stack.
      ImGui::SetCursorScreenPos(ImVec2(top.x, std::max(top.y + curveH, y + rowH)));
      ImGui::Dummy(ImVec2(full, 0.0f));
   }


   void DrawEnvelopePanel(const char* title, const char* curveId, float* attackMs, float* decayMs,
                          float* sustain, float* releaseMs, float* amount, float amountLo,
                          float amountHi, const char* amountFmt, ImU32 color,
                          const ::ModShapes::Ahdsr* shape)
   {
      BeginAudioSection(title);
      DrawEnvelopeFields(curveId, attackMs, decayMs, sustain, releaseMs, amount, amountLo, amountHi,
                         amountFmt, color, shape);
      EndAudioSection();
   }


   // One engine's whole column. Both engines draw this every frame - there is
   // no hidden half - which is what keeps their parameter indices distinct and
   // stops a modulator patched to one engine's attack from following a tab
   // switch onto the other's.
   void DrawWavetableEngineColumn(WavetableNode* n, int e)
   {
      WavetableEngine& eng = n->engines[e];

      // Engine B's dropdowns and checkboxes carry the same labels as engine
      // A's ("wtTable", "wtOct", "wtSemi", "wtFilter", "wtWarp", "##wtOn"),
      // which without this puts both engines' copy of each on one discrete
      // slot and one pin id - see DiscreteSlotScope.
      DiscreteSlotScope discreteScope(e);

      // Every widget below exists twice on this node, once per engine, and
      // ImGui identifies widgets by their label. Without this the two columns'
      // "attack" sliders are the same widget as far as ImGui is concerned:
      // hovering or dragging one activates the other, which is most of why the
      // envelopes read as dead.
      ImGui::PushID(e);

      // Header, one line: on/off, table, fine tune, octave, semitone. The
      // sketch's arrangement - the four things that decide *what this engine
      // is* before any shaping, on the line above the picture of it.
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float onW = tok::space_3_5 + tok::space_1 + ImGui::GetFrameHeight();
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float tableW = std::max(tok::osc_table_w_min, w - onW - octW - semiW - fineW - gap * tok::osc_wavetable_engine_column_ratio);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         bool on = eng.on;
         bool onUserChanged = false;
         if (ModCheckbox("##wtOn", &on, &onUserChanged))
         {
            if (onUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            eng.on = on;
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout(e == 0 ? "engine a" : "engine b", eng.on ? "on" : "off");

         ImGui::SetCursorScreenPos(ImVec2(x0 + onW + gap, y));
         AudioBareDropdown("wtTable", WavetableNames(), eng.table,
                           [&eng](int i) { PushUndoCheckpoint(); eng.table = i; }, tableW,
                           /*categories=*/{}, /*focusSearch=*/true);
         if (ImGui::IsItemHovered())
            SetAudioReadout("wavetable", Wavetable::TableName(eng.table));

         ImGui::SetCursorScreenPos(ImVec2(x0 + onW + gap + tableW + gap, y));
         AudioSlider("fine", &eng.fine, params::osc::fine_lo, params::osc::fine_hi, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("wtOct", OctaveNames(), eng.octave - params::osc::octave_lo,
                           [&eng](int i) { PushUndoCheckpoint(); eng.octave = i + params::osc::octave_lo; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("wtSemi", SemiNames(), eng.semi - params::osc::semi_lo,
                           [&eng](int i) { PushUndoCheckpoint(); eng.semi = i + params::osc::semi_lo; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // The engine's picture, and its primary control: dragging across it
      // scrubs `position`, and the `position` knob below moves the highlight
      // the same way. Dimmed rather than hidden when the engine is off, so the
      // column never changes height.
      DrawWavetableFrames(eng, "##wtFrames", 150.0f, gAudioContentW, !eng.on);
      PublishWtTestRect();
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));

      // Two rows of four, per the sketch. Row two's first two cells pair a
      // mode dropdown with the knob that mode reads.
      {
         AudioKnobRow row(tok::osc_wavetable_engine_column_audio_knob_row_layout);
         row.Knob("volume", &eng.volume, 0.0f, 1.0f, "%.2f");
         row.Knob("position", &eng.position, 0.0f, 1.0f, "%.3f");
         row.KnobInt("unison", &eng.unison, 1, WavetableNode::kMaxUnison);
         ImGui::BeginDisabled((eng.unison <= 1) && !gParamRegisterOnly);
         row.Knob("detune", &eng.detune, params::osc::detune_lo, params::osc::detune_hi, "%.1f c");
         ImGui::EndDisabled();
         row.End();
      }
      {
         const bool filterOff = !SynthModes::FilterUsesCutoff(eng.filterType);
         const bool warpOff = eng.warpMode == SynthModes::kWarpOff;
         AudioKnobRow row(tok::osc_wavetable_engine_column_audio_knob_row_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);

         row.DropdownKnob("wtFilter", SynthModes::FilterTypeList(), eng.filterType,
                          [&eng](int i) { PushUndoCheckpoint(); eng.filterType = i; },
                          "filter", &eng.cutoff, params::osc::cutoff_lo, params::osc::cutoff_hi, "%.0f Hz", filterOff);
         row.DropdownKnob("wtWarp", SynthModes::WarpModeList(e), eng.warpMode,
                          [&eng](int i) { PushUndoCheckpoint(); eng.warpMode = i; },
                          "warp", &eng.warpAmount, 0.0f, 1.0f, "%.3f", warpOff);
         row.Knob("phase", &eng.phase, 0.0f, 1.0f, "%.3f");
         row.Knob("pan", &eng.pan, -1.0f, 1.0f, "%.2f");
         row.End();
      }

      // The two knobs' second parameters, plus the unison stack's spread.
      // These are settings you reach for once while designing a sound and then
      // leave alone, which is what a Tier 2 panel is for (§5) - putting them
      // in the grid would have cost the sketch's 4x2 shape for no gain.
      BeginAudioSection("filter / warp / voice");
      {
         const bool ratioLive = SynthModes::WarpUsesRatio(eng.warpMode);
         AudioSlider("resonance", &eng.resonance, 0.0f, 1.0f, "%.3f", AudioHalfWidth());
         ImGui::SameLine();
         if (!ratioLive)
            ImGui::BeginDisabled();
         AudioSlider("warp ratio", &eng.warpRatio, params::osc::warp_ratio_lo, params::osc::warp_ratio_hi, "%.2f", AudioHalfWidth());
         if (!ratioLive)
            ImGui::EndDisabled();
         AudioSlider("stereo width", &eng.stereoWidth, 0.0f, 1.0f, "%.3f", AudioHalfWidth());
         ImGui::SameLine();
         AudioSlider("phase random", &eng.phaseRandomize, 0.0f, 1.0f, "%.3f", AudioHalfWidth());
      }
      EndAudioSection();

      // Three envelopes, three trace colours, all three draggable.
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope  -  drag the handles", "##wtAmpEnv", &eng.ampAttack,
                        &eng.ampDecay, &eng.ampSustain, &eng.ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::Role(tok::col::accent_alt, tok::osc_envelope_alpha_lg));
      ImGui::PopID();

      ImGui::PushID("pitch");
      DrawEnvelopePanel("pitch envelope  -  drag the handles", "##wtPitchEnv", &eng.pitchAttack,
                        &eng.pitchDecay, &eng.pitchSustain, &eng.pitchRelease, &eng.pitchAmount,
                        -tok::osc_envelope_amount_lo_lg, tok::osc_envelope_amount_lo_lg, "%.1f st", tok::Role(tok::col::role_modulation, tok::osc_envelope_alpha_sm));
      ImGui::PopID();

      ImGui::PushID("filter");
      DrawEnvelopePanel("filter envelope  -  drag the handles", "##wtFiltEnv", &eng.filterAttack,
                        &eng.filterDecay, &eng.filterSustain, &eng.filterRelease, &eng.filterAmount,
                        -tok::osc_envelope_amount_lo_sm, tok::osc_envelope_amount_lo_sm, "%.2f oct", tok::Role(tok::col::viz_teal_light, tok::osc_envelope_alpha_sm));
      ImGui::PopID();

      ImGui::PopID();
   }


   void DrawWavetableBody(GraphNode& gn, WavetableNode* n)
   {
      const bool noteDriven = n->noteInput.GetSource() != nullptr;
      const int voices = n->ActiveVoices();

      char stat[110];
      if (noteDriven)
         snprintf(stat, sizeof(stat), "%s + %s  -  %d voice%s", Wavetable::TableName(n->engines[0].table),
                  n->engines[1].on ? Wavetable::TableName(n->engines[1].table) : "-", voices,
                  voices == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "%s + %s  -  free run %s",
                  Wavetable::TableName(n->engines[0].table),
                  n->engines[1].on ? Wavetable::TableName(n->engines[1].table) : "-", UiFmt::Hz(n->frequency).c);

      gWtTestRects.clear();
      BeginAudioBody(gn.index, gn.category, kAudioWideWidth, stat);

      // The engines first, then the node-wide controls under them.
      //
      // They used to sit on top, which put four knobs and a scope between the
      // node's title and the first thing the node is actually about - and
      // read, fairly, as clutter nobody asked for. They are not removable:
      // `mix` is the whole reason there are two engines, `volume` is the
      // node's output level, `glide` is per-node because it is per-voice
      // pitch, and `freq` is the pitch of the free-running voice when no note
      // cable is attached. What was wrong was their prominence, not their
      // existence, so they moved below the pair they apply to.
      BeginAudioColumns(WavetableNode::kEngines);
      for (int e = 0; e < WavetableNode::kEngines; e++)
      {
         BeginAudioColumn(e);
         DrawWavetableEngineColumn(n, e);
         EndAudioColumn();
      }
      EndAudioColumns();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));
      BeginAudioSection("master");
      DrawWavetableScope(n, 48.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_md));
      {
         AudioKnobRow row(tok::osc_wavetable_body_audio_knob_row_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);
         row.Knob("mix  a-b", &n->mix, 0.0f, 1.0f, "%.2f");
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f");
         // Free-running pitch is meaningless once notes drive pitch - greyed
         // rather than hidden so the row never reflows (§5).
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->frequency, params::Wavetable::frequency_lo, params::Wavetable::frequency_hi, "%.0f Hz");
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("glide", &n->glide, params::Wavetable::glide_lo, params::Wavetable::glide_hi, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
         // This knob is the manual/global bend - a Pitch Bend node wired
         // into the note chain upstream doesn't touch it at all, it rides
         // in on each note's own bendSemitones field instead (see
         // NoteEvent.h) and adds additively with whatever this knob is set
         // to. Drag this one directly for a bend with nothing patched.
         row.Knob("bend", &n->pitchBend, params::Wavetable::pitch_bend_lo, params::Wavetable::pitch_bend_hi, "%+.2f st");
         // fmMode picks phase modulation (cheap, always-stable sideband FM)
         // vs linear through-zero FM (DX7/Serum/Operator-style, can self-
         // modulate harder and run the carrier backward at high depth) - the
         // depth knob's meaning depends on it, so they read as one control.
         row.DropdownKnob("wtFmMode", { "pm", "fm" }, n->fmMode,
                          [n](int i) { PushUndoCheckpoint(); n->fmMode = i; },
                          "fm depth", &n->fmDepth, params::Wavetable::fm_depth_lo, params::Wavetable::fm_depth_hi, "%.2f");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }


   void DrawOscillatorWaveform(int waveform, float phase, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      const float amp = (h * 0.5f) - tok::space_3;

      // Horizontal center line
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), tok::osc_oscillator_waveform_stroke);

      // Vertical grid ticks
      for (int i = 1; i < 4; i++)
      {
         const float x = origin.x + w * ((float)i / tok::osc_oscillator_waveform_ratio);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), tok::osc_oscillator_waveform_stroke);
      }

      std::vector<ImVec2> pts;
      pts.reserve(256);

      const float cycles = tok::osc_cycles;

      if (waveform == OscillatorNode::kSquare)
      {
         const float cycleW = w / cycles;
         const float startX = origin.x - phase * cycleW;
         for (int c = 0; c < 3; c++)
         {
            const float x0 = startX + c * cycleW;
            const float xMid = x0 + cycleW * 0.5f;
            const float x1 = x0 + cycleW;

            if (c == 0)
               pts.push_back(ImVec2(x0, midY - amp));
            else
            {
               pts.push_back(ImVec2(x0, midY + amp));
               pts.push_back(ImVec2(x0, midY - amp));
            }
            pts.push_back(ImVec2(xMid, midY - amp));
            pts.push_back(ImVec2(xMid, midY + amp));
            pts.push_back(ImVec2(x1, midY + amp));
         }
      }
      else if (waveform == OscillatorNode::kSaw)
      {
         const float cycleW = w / cycles;
         const float startX = origin.x - phase * cycleW;
         for (int c = 0; c < 3; c++)
         {
            const float x0 = startX + c * cycleW;
            const float x1 = x0 + cycleW;
            pts.push_back(ImVec2(x0, midY + amp));
            pts.push_back(ImVec2(x1, midY - amp));
            if (c == 0)
               pts.push_back(ImVec2(x1, midY + amp));
         }
      }
      else if (waveform == OscillatorNode::kTriangle)
      {
         const float cycleW = w / cycles;
         const float startX = origin.x - phase * cycleW;
         for (int c = 0; c < 3; c++)
         {
            const float x0 = startX + c * cycleW;
            const float x1 = x0 + cycleW * tok::osc_x1_ratio;
            const float x2 = x0 + cycleW * tok::osc_x2_ratio;
            const float x3 = x0 + cycleW;

            if (c == 0)
               pts.push_back(ImVec2(x0, midY));
            pts.push_back(ImVec2(x1, midY - amp));
            pts.push_back(ImVec2(x2, midY + amp));
            pts.push_back(ImVec2(x3, midY));
         }
      }
      else // Sine
      {
         constexpr int kSteps = 120;
         for (int i = 0; i <= kSteps; i++)
         {
            const float t = (float)i / (float)kSteps;
            const float y = sinf(tok::osc_sinf_pulse_phase * (float)M_PI * (cycles * t + phase));
            pts.push_back(ImVec2(origin.x + t * w, midY - y * amp));
         }
      }

      // Glow path
      dl->PathClear();
      for (const auto& p : pts)
         dl->PathLineTo(p);
      dl->PathStroke(isLight ? tok::Base(tok::col::accent, tok::osc_oscillator_waveform_path_alpha_sm) : tok::Base(tok::col::accent_pale_muted_warm, tok::osc_oscillator_waveform_path_alpha_md), 0, tok::osc_oscillator_waveform_path_stroke_lg);

      // Core trace
      dl->PathClear();
      for (const auto& p : pts)
         dl->PathLineTo(p);
      dl->PathStroke(isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, tok::osc_oscillator_waveform_path_alpha_lg), 0, tok::osc_oscillator_waveform_path_stroke_sm);

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawOscillatorBody(GraphNode& gn, OscillatorNode* n)
   {
      const bool noteDriven = n->noteInput.GetSource() != nullptr;
      const int voices = n->ActiveVoices();

      static const char* const kWaveNames[] = { "sine", "triangle", "saw", "square" };
      const int safeWave = std::clamp(n->waveform, 0, 3);
      const char* waveName = kWaveNames[safeWave];

      char stat[110];
      if (noteDriven)
         snprintf(stat, sizeof(stat), "%s  -  %d voice%s", waveName,
                  voices, voices == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "%s  -  free run %s",
                  waveName, UiFmt::Hz(n->frequency).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      WavetableEngine& eng = n->engine;

      // Header: waveform dropdown, fine tuning, octave, semitone.
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = tok::space_1_25;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float waveW = std::max(tok::osc_wave_w_min, w - octW - semiW - fineW - gap * tok::osc_oscillator_body_ratio);
         static const std::vector<std::string> kOscWaveNames = SynthModes::WaveformTypeSubset(
            { SynthModes::kWaveSine, SynthModes::kWaveTriangle, SynthModes::kWaveSaw, SynthModes::kWaveSquare });

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("oscWave", kOscWaveNames, n->waveform,
                           [n](int i) { PushUndoCheckpoint(); n->waveform = i; }, waveW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + waveW + gap, y));
         AudioSlider("fine", &eng.fine, params::osc::fine_lo, params::osc::fine_hi, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("oscOct", OctaveNames(), eng.octave - params::osc::octave_lo,
                           [&eng](int i) { PushUndoCheckpoint(); eng.octave = i + params::osc::octave_lo; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("oscSemi", SemiNames(), eng.semi - params::osc::semi_lo,
                           [&eng](int i) { PushUndoCheckpoint(); eng.semi = i + params::osc::semi_lo; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Waveform display
      DrawOscillatorWaveform(n->waveform, n->engine.phase, 100.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));

      // Two knob rows of four
      {
         AudioKnobRow row(tok::osc_oscillator_body_audio_knob_row_layout);
         row.Knob("volume", &eng.volume, 0.0f, 1.0f, "%.2f");
         row.Knob("phase", &eng.phase, 0.0f, 1.0f, "%.3f");
         row.KnobInt("unison", &eng.unison, 1, OscillatorNode::kMaxUnison);
         ImGui::BeginDisabled((eng.unison <= 1) && !gParamRegisterOnly);
         row.Knob("detune", &eng.detune, params::osc::detune_lo, params::osc::detune_hi, "%.1f c");
         ImGui::EndDisabled();
         row.End();
      }
      {
         const bool filterOff = !SynthModes::FilterUsesCutoff(eng.filterType);
         AudioKnobRow row(tok::osc_oscillator_body_audio_knob_row_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);

         row.DropdownKnob("oscFilter", SynthModes::FilterTypeList(), eng.filterType,
                          [&eng](int i) { PushUndoCheckpoint(); eng.filterType = i; },
                          "filter", &eng.cutoff, params::osc::cutoff_lo, params::osc::cutoff_hi, "%.0f Hz", filterOff);
         ImGui::BeginDisabled((filterOff) && !gParamRegisterOnly);
         row.Knob("reso", &eng.resonance, 0.0f, 1.0f, "%.3f");
         ImGui::EndDisabled();
         row.Knob("sync", &eng.warpRatio, 1.0f, params::osc::warp_ratio_hi, "%.2f");
         row.Knob("pan", &eng.pan, -1.0f, 1.0f, "%.2f");
         row.End();
      }

      // Amplitude envelope (interactive ADSR).
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope  -  drag the handles", "##oscAmpEnv", &eng.ampAttack,
                        &eng.ampDecay, &eng.ampSustain, &eng.ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::Role(tok::col::accent_alt, tok::osc_envelope_alpha_lg));
      ImGui::PopID();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));
      BeginAudioSection("master");
      DrawOscillatorScope(n, 48.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_md));
      {
         AudioKnobRow row(tok::osc_oscillator_body_audio_knob_row_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->frequency, params::Oscillator::frequency_lo, params::Oscillator::frequency_hi, "%.0f Hz");
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("glide", &n->glide, params::Oscillator::glide_lo, params::Oscillator::glide_hi, "%.2f s", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row.Knob("bend", &n->pitchBend, params::Oscillator::pitch_bend_lo, params::Oscillator::pitch_bend_hi, "%+.2f st");
         // fmMode picks phase modulation vs linear through-zero FM - see the
         // Wavetable master row's identical control for why they're paired.
         row.DropdownKnob("oscFmMode", { "pm", "fm" }, n->fmMode,
                          [n](int i) { PushUndoCheckpoint(); n->fmMode = i; },
                          "fm depth", &n->fmDepth, params::Oscillator::fm_depth_lo, params::Oscillator::fm_depth_hi, "%.2f");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }


   void DrawMetallicBody(GraphNode& gn, MetallicNode* n)
   {
      const bool noteDriven = n->noteInput.GetSource() != nullptr;
      const int voices = n->ActiveVoices();
      const char* matName = MetallicDsp::MaterialName(n->material);

      char stat[128];
      if (noteDriven)
         snprintf(stat, sizeof(stat), "%s  -  %d voice%s", matName, voices, voices == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "%s  -  free run %s", matName, UiFmt::Hz(n->frequency).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Header: Material preset dropdown, Fine tuning, Octave, Semitone
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = tok::space_1_25;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float matW = std::max(tok::osc_mat_w_min, w - octW - semiW - fineW - gap * tok::osc_metallic_body_ratio);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("metalMat", MetallicDsp::MaterialList(), n->material,
                           [n](int i) {
                              PushUndoCheckpoint();
                              n->SetMaterialPreset(i);
                           }, matW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + matW + gap, y));
         AudioSlider("fine", &n->fine, params::Metallic::fine_lo, params::Metallic::fine_hi, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("metalOct", OctaveNames(), n->octave - params::osc::octave_lo,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i + params::osc::octave_lo; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("metalSemi", SemiNames(), n->semi - params::osc::semi_lo,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i + params::osc::semi_lo; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Scope Visualizer with Strike Trigger Button
      {
         const float w = gAudioContentW;
         const float strikeBtnW = tok::osc_strike_btn_w;
         const float scopeW = w - strikeBtnW - tok::space_1_5;
         const float scopeH = tok::osc_scope_h;

         const ImVec2 pos = ImGui::GetCursorScreenPos();
         DrawMetallicScope(n, scopeH, scopeW);

         ImGui::SetCursorScreenPos(ImVec2(pos.x + scopeW + tok::space_1_5, pos.y));
         if (ActionButton::Draw("Strike", ImVec2(strikeBtnW, scopeH)))
         {
            n->TriggerStrike();
         }
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));

      // Section 1: Physical Acoustic Properties (Transient, Decay, Stiffness, Width)
      BeginAudioSection("physical acoustics");
      {
         AudioKnobRow row(tok::osc_metallic_body_audio_knob_row_layout);
         row.Knob("transient", &n->transient, params::Metallic::transient_lo, params::Metallic::transient_hi, "%.2f");
         row.Knob("decay", &n->decay, params::Metallic::decay_lo, params::Metallic::decay_hi, "%.2f s");
         row.Knob("stiffness", &n->stiffness, params::Metallic::stiffness_lo, params::Metallic::stiffness_hi, "%.2f");
         row.Knob("width", &n->width, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Section 2: Resonator Filter & Body Drive
      BeginAudioSection("filter & drive");
      {
         const bool filterOff = (n->filterType == MetallicDsp::kFilterOff);
         AudioKnobRow row(tok::osc_metallic_body_audio_knob_row_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);

         row.DropdownKnob("metalFilter", MetallicDsp::FilterModeDisplayList(),
                          MetallicDsp::FilterModeToDisplayIndex(n->filterType),
                          [n](int displayIdx) { PushUndoCheckpoint(); n->filterType = MetallicDsp::DisplayIndexToFilterMode(displayIdx); },
                          "filter", &n->filterCutoff, params::Metallic::filter_cutoff_lo, params::Metallic::filter_cutoff_hi, "%.0f Hz", filterOff);
         ImGui::BeginDisabled((filterOff) && !gParamRegisterOnly);
         row.Knob("reso", &n->filterResonance, 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         row.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row.Knob("volume", &n->volume, params::Metallic::volume_lo, params::Metallic::volume_hi, "%.2f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Section 3: Master Pitch / Portamento
      BeginAudioSection("pitch & glide");
      {
         AudioKnobRow row(tok::osc_row_layout_sm);
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->frequency, params::Metallic::frequency_lo, params::Metallic::frequency_hi, "%.0f Hz");
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("glide", &n->glide, 0.0f, 1.0f, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }


   void DrawFMScope(FMNode* n, float h)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / tok::motion_hz_scope)
      {
         float buf[FMNode::kScopeCacheCapacity];
         const int count = n->ReadScope(buf, FMNode::kScopeCacheCapacity);
         if (count > 0)
         {
            std::copy(buf, buf + count, n->scopeCache);
            n->scopeCacheCount = count;
         }
         n->scopeCacheTime = now;
      }

      const float w = gAudioContentW;
      const AudioViz::Frame f = AudioViz::Begin(w, h);
      const float midY = f.origin.y + h * 0.5f;
      f.dl->AddLine(ImVec2(f.origin.x, midY), ImVec2(f.br.x, midY), ScopeMidLineCol(), tok::osc_fm_scope_stroke);
      if (n->scopeCacheCount > 1 && n->ActiveVoices() > 0)
      {
         const int count = n->scopeCacheCount;
         f.dl->PathClear();
         for (int i = 0; i < count; i++)
         {
            const float t = (float)i / (float)(count - 1);
            const float v = std::max(-1.0f, std::min(1.0f, n->scopeCache[i]));
            f.dl->PathLineTo(ImVec2(f.origin.x + t * w, midY - v * h * tok::osc_fm_scope_ratio));
         }
         f.dl->PathStroke(IsThemeLight() ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation, tok::osc_fm_scope_path_alpha), 0, tok::osc_fm_scope_path_stroke);
      }
      else
      {
         AudioViz::IdleLabel(f, "play notes");
      }
      AudioViz::End(f);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawFMBody(GraphNode& gn, FMNode* n)
   {
      const int voices = n->ActiveVoices();
      const FMDsp::Algorithm& alg = FMDsp::AlgorithmAt(n->algorithm);
      char stat[96];
      snprintf(stat, sizeof(stat), "%s  -  %d voice%s", alg.name, voices, voices == 1 ? "" : "s");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFMScope(n, 52.0f);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      {
         AudioKnobRow row(tok::osc_fm_body_audio_knob_row_layout);
         row.Dropdown("algorithm", FMDsp::AlgorithmList(), n->algorithm, [n](int i) {
            PushUndoCheckpoint();
            n->algorithm = i;
         });
         row.Knob("feedback", &n->feedback, 0.0f, 1.0f, "%.2f");
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      // One section per operator; a carrier's level is its loudness, a
      // modulator's is how deep it bends what it feeds.
      static const char* kTitle[FMDsp::kNumOps][2] = {
         { "operator 1  -  carrier", "operator 1  -  modulator" },
         { "operator 2  -  carrier", "operator 2  -  modulator" },
         { "operator 3  -  carrier", "operator 3  -  modulator" },
         { "operator 4  -  carrier", "operator 4  -  modulator" },
      };
      for (int o = 0; o < FMDsp::kNumOps; o++)
      {
         FMNode::Operator& op = n->ops[o];
         const bool carrier = ((alg.carriers >> o) & 1) != 0;
         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
         ImGui::PushID(o);
         // Each operator's "fixed" checkbox is a discrete param: without a
         // scope all four hash to one pin id (PINDUPTEST).
         DiscreteSlotScope discreteScope(o);
         BeginAudioSection(kTitle[o][carrier ? 0 : 1]);
         {
            // Pitch row: ratio (or frequency when fixed), the fixed switch, then level, so the
            // envelope below is the same component, at the same size, as Wavetable's.
            const float full = gAudioContentW;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float checkW = tok::osc_check_w;
            const float sliderW = (full - checkW - gap * tok::osc_fm_body_ratio) * 0.5f;
            const ImVec2 rowTop = ImGui::GetCursorScreenPos();
            const float levelX = rowTop.x + sliderW + gap + checkW + gap;
            if (op.fixed)
               AudioSlider("freq", &op.freq, params::osc::freq_lo, params::osc::freq_hi, "%.0f Hz", sliderW, FrequencyTaper::PosToValue,
                           FrequencyTaper::ValueToPos);
            else
               AudioSlider("ratio", &op.ratio, params::osc::ratio_lo, params::osc::ratio_hi, "%.2f", sliderW);
            // Every cell is placed from the row's own origin, so each operator lines up exactly.
            ImGui::SetCursorScreenPos(ImVec2(rowTop.x + sliderW + gap, rowTop.y));
            ModCheckbox("fixed", &op.fixed);
            ImGui::SetCursorScreenPos(ImVec2(levelX, rowTop.y));
            AudioSlider("level", &op.level, 0.0f, 1.0f, "%.2f", rowTop.x + full - levelX);
            ImGui::SetCursorScreenPos(ImVec2(rowTop.x, rowTop.y + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y));
            DrawEnvelopeFields("##fmEnv", &op.attack, &op.decay, &op.sustain, &op.release, nullptr, 0.0f,
                               1.0f, "%.2f", tok::Role(tok::col::accent_alt, tok::osc_envelope_fields_alpha), nullptr);
            op.decay = std::max(1.0f, op.decay);
            op.release = std::max(1.0f, op.release);
         }
         EndAudioSection();
         ImGui::PopID();
      }

      EndAudioBody();
   }


   // The main sample's partial levels at its loudest frame, dB-scaled; the
   // bars past the partials knob are dimmed, since they are not heard.
   void DrawPartialsBars(PartialsNode* n, float h)
   {
      const float w = gAudioContentW;
      const AudioViz::Frame f = AudioViz::Begin(w, h);
      const int count = n->barCount;
      if (count > 0)
      {
         float peak = 1e-6f;
         for (int i = 0; i < count; i++)
            peak = std::max(peak, n->bars[i]);
         const bool light = IsThemeLight();
         const ImU32 on = light ? tok::Base(tok::col::role_expression, 200) : tok::Base(tok::col::role_expression_pale_muted_mid, 200);
         const ImU32 off = light ? tok::Base(tok::col::ink_white, 140) : tok::Base(tok::col::ink_black, 130);
         const float barW = w / (float)PartialsNode::kBarCount;
         for (int i = 0; i < count; i++)
         {
            const float db = 20.0f * std::log10(std::max(1e-6f, n->bars[i] / peak));
            const float frac = std::clamp(1.0f + db / 60.0f, 0.0f, 1.0f);
            const float x = f.origin.x + barW * (float)i;
            const ImVec2 top(x, f.br.y - frac * h);
            const ImVec2 bot(x + std::max(1.0f, barW - 1.0f), f.br.y);
            f.dl->AddRectFilled(top, bot, on);
            if ((float)i >= n->partials)
               f.dl->AddRectFilled(top, bot, off);
         }
      }
      else
      {
         AudioViz::IdleLabel(f, n->IsAnalyzing() ? "analyzing..." : "drop a sample here");
      }
      AudioViz::End(f);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawPartialsBody(GraphNode& gn, PartialsNode* n)
   {
      const int voices = n->ActiveVoices();
      char stat[160];
      if (n->HasSample(0))
         snprintf(stat, sizeof(stat), "%s  -  %d voice%s", n->Status().c_str(), voices, voices == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      // One strip: the two loaders and the loop switch, edge to edge like every row below it.
      {
         const float w = gAudioContentW;
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float loopW = tok::osc_loop_w;
         const float btnW = (w - loopW - gap * tok::osc_partials_body_ratio) * 0.5f;
         const float y = ImGui::GetCursorScreenPos().y;
         for (int which = 0; which < 2; which++)
         {
            ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + (float)which * (btnW + gap), y));
            ImGui::PushID(which);
            const std::string& name = n->FileName(which);
            const char* label = which == 0 ? "sample" : "morph to";
            char text[96];
            snprintf(text, sizeof(text), "%s: %s", label, name.empty() ? "load..." : name.c_str());
            if (ActionButton::Draw(text, ImVec2(btnW, 0)))
            {
               const std::string path = Platform::OpenAudioDialog();
               if (!path.empty())
               {
                  PushUndoCheckpoint();
                  n->LoadFile(which, path);
               }
            }
            ImGui::PopID();
         }
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + w - loopW, y));
         ModCheckbox("loop##partialsLoop", &n->loop);
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      DrawPartialsBars(n, 52.0f);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      {
         AudioKnobRow row(tok::osc_partials_body_audio_knob_row_layout);
         row.Knob("pitch", &n->pitch, params::Partials::pitch_lo, params::Partials::pitch_hi, "%+.1f st");
         row.Knob("formant", &n->formant, params::Partials::formant_lo, params::Partials::formant_hi, "%+.1f st");
         row.Knob("partials", &n->partials, params::Partials::partials_lo, params::Partials::partials_hi, "%.0f");
         row.Knob("morph", &n->morph, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(tok::osc_partials_body_audio_knob_row_layout);
         row.Knob("speed", &n->speed, params::Partials::speed_lo, params::Partials::speed_hi, "%.2fx");
         row.Knob("noise", &n->noise, params::Partials::noise_lo, params::Partials::noise_hi, "%.2f");
         row.Knob("release", &n->release, params::Partials::release_lo, params::Partials::release_hi, "%.0f ms", kKnobSmall, false, false,
                  AudioWidgetStyle::KnobSkewRelease400);
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      EndAudioBody();
   }


   // The string from nut (left) to bridge (right) with the exciter's point on it.
   void DrawStringDiagram(StringNode* n, float h)
   {
      const float w = gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      const float pad = tok::space_2_5;
      const float midY = origin.y + h * 0.5f;
      const float x0 = origin.x + pad, x1 = br.x - pad;
      const ImU32 line = ScopeMidLineCol();
      const ImU32 trace = IsThemeLight() ? tok::Base(tok::col::accent_light_muted_warm, 240) : tok::Base(tok::col::accent_alt_vivid_warm_hi, 240);
      dl->AddLine(ImVec2(x0, origin.y + tok::space_1), ImVec2(x0, br.y - tok::space_1), line, tok::osc_string_diagram_stroke);
      dl->AddLine(ImVec2(x1, origin.y + tok::space_1), ImVec2(x1, br.y - tok::space_1), line, tok::osc_string_diagram_stroke);
      dl->AddLine(ImVec2(x0, midY), ImVec2(x1, midY), trace, tok::osc_string_diagram_line_stroke);
      const float px = x1 - std::clamp(n->position, 0.0f, 1.0f) * (x1 - x0);
      dl->AddCircleFilled(ImVec2(px, midY), tok::osc_string_diagram_r, trace);
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawStringBody(GraphNode& gn, StringNode* n)
   {
      const int voices = n->ActiveVoices();
      char stat[64];
      snprintf(stat, sizeof(stat), "%d voice%s", voices, voices == 1 ? "" : "s");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawStringDiagram(n, 28.0f);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      // 3 x 3: selectors stacked in the left column (exciter, body type), the
      // string's knobs beside the exciter, the body's beside its type.
      {
         AudioKnobRow row(tok::osc_string_body_audio_knob_row_layout);
         row.Dropdown("exciter##stringExciter", StringNode::ExciterNames(), n->exciter, [n](int i) {
            PushUndoCheckpoint();
            n->exciter = i;
         });
         row.Knob("position", &n->position, params::String::position_lo, params::String::position_hi, "%.2f");
         row.Knob("stiffness", &n->stiffness, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(tok::osc_string_body_audio_knob_row_layout);
         row.Dropdown("body type##stringBodyType", StringNode::BodyTypeNames(), n->bodyType, [n](int i) {
            PushUndoCheckpoint();
            n->bodyType = i;
         });
         row.Knob("body", &n->body, 0.0f, 1.0f, "%.2f");
         row.Knob("tone", &n->tone, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(tok::osc_string_body_audio_knob_row_layout);
         row.Knob("decay", &n->decay, params::String::decay_lo, params::String::decay_hi, "%.2f s");
         row.Knob("release", &n->release, params::String::release_lo, params::String::release_hi, "%.0f ms", kKnobSmall, false, false,
                  AudioWidgetStyle::KnobSkewRelease400);
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         // The exciter's own: the pick's hardness (pluck only: a strike's pulse follows velocity), the bow's force and
         // noise (bow only). The ones the chosen exciter ignores fade out; they stay patchable, they just do nothing.
         const bool bowed = n->exciter == StringNode::kBow;
         AudioKnobRow row(tok::osc_string_body_audio_knob_row_layout);
         ImGui::BeginDisabled((bowed || n->exciter == StringNode::kStrike) && !gParamRegisterOnly);
         row.Knob("hardness", &n->hardness, 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         ImGui::BeginDisabled(!bowed && !gParamRegisterOnly);
         row.Knob("pressure", &n->pressure, 0.0f, 1.0f, "%.2f");
         row.Knob("noise", &n->noise, 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         row.End();
      }
      EndAudioBody();
   }


   // The bore from mouthpiece (left) to bell (right): straight for the
   // clarinet and flute, a cone for the sax, a flare at the end for brass.
   void DrawWindDiagram(WindNode* n, float h)
   {
      const float w = gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      const float pad = tok::space_2_5;
      const float midY = origin.y + h * 0.5f;
      const float x0 = origin.x + pad, x1 = br.x - pad;
      const ImU32 line = ScopeMidLineCol();
      const ImU32 trace = IsThemeLight() ? tok::Base(tok::col::accent_light_muted_warm, 240) : tok::Base(tok::col::accent_alt_vivid_warm_hi, 240);
      const float r0 = tok::osc_r0, rMax = h * 0.5f - 3.0f;
      ImVec2 top[17], bot[17];
      for (int k = 0; k <= 16; k++)
      {
         const float t = (float)k / 16.0f;
         float r = r0;
         if (n->instrument == WindNode::kSax)
            r = r0 + (rMax * tok::osc_wind_diagram_ratio - r0) * t;
         else if (n->instrument == WindNode::kBrass)
            r = r0 + (rMax - r0) * std::pow(t, tok::osc_wind_diagram);
         else if (n->instrument == WindNode::kClarinet)
            r = t > tok::osc_r_limit ? r0 + (rMax * 0.5f - r0) * (t - tok::osc_wind_diagram_inset) / tok::osc_r_ratio : r0;
         const float x = x0 + (x1 - x0) * t;
         top[k] = ImVec2(x, midY - r);
         bot[k] = ImVec2(x, midY + r);
      }
      dl->AddPolyline(top, tok::osc_line_num_points, trace, 0, tok::osc_wind_diagram_stroke);
      dl->AddPolyline(bot, tok::osc_line_num_points, trace, 0, tok::osc_wind_diagram_stroke);
      dl->AddLine(ImVec2(x0, midY - r0 - tok::space_0_75), ImVec2(x0, midY + r0 + tok::space_0_75), line, tok::osc_wind_diagram_line_stroke);
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawWindBody(GraphNode& gn, WindNode* n)
   {
      const int voices = n->ActiveVoices();
      char stat[64];
      snprintf(stat, sizeof(stat), "%d voice%s", voices, voices == 1 ? "" : "s");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawWindDiagram(n, 28.0f);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      // 2 x 4: the instrument in the left column, then how it is blown;
      // the breath's shape in time and the level below.
      {
         AudioKnobRow row(tok::osc_wind_body_audio_knob_row_layout);
         row.Dropdown("instrument##windInstrument", WindNode::InstrumentNames(), n->instrument, [n](int i) {
            PushUndoCheckpoint();
            n->instrument = i;
         });
         row.Knob("breath", &n->breath, 0.0f, 1.0f, "%.2f");
         row.Knob("tone", &n->tone, 0.0f, 1.0f, "%.2f");
         row.Knob("noise", &n->noise, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(tok::osc_wind_body_audio_knob_row_layout);
         row.Knob("vibrato", &n->vibrato, 0.0f, 1.0f, "%.2f");
         row.Knob("attack", &n->attack, params::Wind::attack_lo, params::Wind::attack_hi, "%.0f ms");
         row.Knob("release", &n->release, params::Wind::release_lo, params::Wind::release_hi, "%.0f ms", kKnobSmall, false, false,
                  AudioWidgetStyle::KnobSkewRelease400);
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      EndAudioBody();
   }


   // Three unison strings from the bridge pin (left) and the hammer under
   // them at an eighth of their length, lifted while a note is sounding.
   void DrawPianoDiagram(PianoNode* n, float h)
   {
      const float w = gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      const float pad = tok::space_2_5;
      const float x0 = origin.x + pad, x1 = br.x - pad;
      const ImU32 line = ScopeMidLineCol();
      const ImU32 trace = IsThemeLight() ? tok::Base(tok::col::accent_light_muted_warm, 240) : tok::Base(tok::col::accent_alt_vivid_warm_hi, 240);
      const float spread = tok::space_0_75 + tok::osc_spread_ratio * n->detune;
      const float y0 = origin.y + h * tok::osc_y0_ratio;
      for (int k = -1; k <= 1; k++)
         dl->AddLine(ImVec2(x0, y0 + k * spread), ImVec2(x1, y0 + k * spread), trace, tok::osc_piano_diagram_stroke);
      dl->AddLine(ImVec2(x0, y0 - tok::osc_piano_diagram_ratio * spread), ImVec2(x0, y0 + tok::osc_piano_diagram_ratio * spread), line, tok::osc_piano_diagram_line_stroke);
      const float hx = x0 + (x1 - x0) * tok::osc_hx_ratio;
      const float lift = n->ActiveVoices() > 0 ? 2.0f : 6.0f;
      const float hw = tok::space_1 + tok::osc_hw_ratio * n->hardness;
      dl->AddRectFilled(ImVec2(hx - hw, y0 + spread + lift), ImVec2(hx + hw, br.y - tok::space_0_75), trace, tok::radius_cell);
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawPianoBody(GraphNode& gn, PianoNode* n)
   {
      const int voices = n->ActiveVoices();
      char stat[64];
      snprintf(stat, sizeof(stat), "%d voice%s", voices, voices == 1 ? "" : "s");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawPianoDiagram(n, 28.0f);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      // 2 x 4 under the model: the hammer and the strings' sound, then their
      // tuning, the soundboard, the dampers and the level. An electric reads
      // the same knobs as its pickup, fork and speaker.
      const bool electric = n->model != PianoNode::kGrand;
      {
         AudioKnobRow row(electric ? tok::osc_piano_body_audio_knob_row_layout : 1);
         row.Dropdown("model##pianoModel", PianoNode::ModelNames(), n->model, [n](int i) {
            PushUndoCheckpoint();
            n->model = i;
         });
         if (electric)
            row.Knob("tremolo", &n->tremolo, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(tok::osc_row_layout_lg);
         row.Knob("hardness", &n->hardness, 0.0f, 1.0f, "%.2f");
         row.Knob(electric ? "pickup" : "tone", &n->tone, 0.0f, 1.0f, "%.2f", kKnobSmall, false, false, AudioWidgetStyle::Knob, nullptr, nullptr, -1, "tone");
         row.Knob("decay", &n->decay, 0.0f, 1.0f, "%.2f");
         row.Knob(electric ? "fork" : "detune", &n->detune, 0.0f, 1.0f, "%.2f", kKnobSmall, false, false, AudioWidgetStyle::Knob, nullptr, nullptr, -1, "detune");
         row.End();
      }
      {
         AudioKnobRow row(tok::osc_row_layout_lg);
         row.Knob(electric ? "bell" : "stiffness", &n->stiffness, 0.0f, 1.0f, "%.2f", kKnobSmall, false, false, AudioWidgetStyle::Knob, nullptr, nullptr, -1, "stiffness");
         row.Knob(electric ? "speaker" : "body", &n->body, 0.0f, 1.0f, "%.2f", kKnobSmall, false, false, AudioWidgetStyle::Knob, nullptr, nullptr, -1, "body");
         row.Knob("release", &n->release, params::Piano::release_lo, params::Piano::release_hi, "%.0f ms", kKnobSmall, false, false,
                  AudioWidgetStyle::KnobSkewRelease400);
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      EndAudioBody();
   }


   void DrawWaveTerrainScope(WaveTerrainNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / tok::motion_hz_scope)
      {
         float buf[WaveTerrainNode::kScopeCapacity];
         const int count = n->ReadScope(buf, WaveTerrainNode::kScopeCapacity);
         if (count > 0)
         {
            std::copy(buf, buf + count, n->scopeCache);
            n->scopeCacheCount = count;
         }
         n->scopeCacheTime = now;
      }

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      for (int i = 1; i < 8; i++)
         dl->AddLine(ImVec2(origin.x + w * i / tok::osc_wave_terrain_scope_ratio, origin.y), ImVec2(origin.x + w * i / tok::osc_wave_terrain_scope_ratio, br.y),
                     ScopeGridCol(), tok::osc_wave_terrain_scope_stroke);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), tok::osc_wave_terrain_scope_stroke);

      if (n->scopeCacheCount > 1)
      {
         const int count = n->scopeCacheCount;
         for (int pass = 0; pass < 2; pass++)
         {
            dl->PathClear();
            for (int i = 0; i < count; i++)
            {
               const float t = (float)i / (float)(count - 1);
               const float y = midY - n->scopeCache[i] * (h * tok::osc_wave_terrain_scope_scale);
               dl->PathLineTo(ImVec2(origin.x + t * w, y));
            }
            if (pass == 0)
               dl->PathStroke(isLight ? tok::Base(tok::col::accent_alt_deep, tok::osc_wave_terrain_scope_path_alpha_sm) : tok::Base(tok::col::accent_alt_vivid_warm_lo, tok::osc_wave_terrain_scope_path_alpha_sm), 0, tok::osc_wave_terrain_scope_path_stroke_lg);
            else
               dl->PathStroke(isLight ? tok::Base(tok::col::accent_light_muted_warm, tok::osc_wave_terrain_scope_path_alpha_lg) : tok::Base(tok::col::accent_alt_vivid_warm_hi, tok::osc_wave_terrain_scope_path_alpha_lg), 0, tok::osc_wave_terrain_scope_path_stroke_sm);
         }
      }
      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawWaveTerrainBody(GraphNode& gn, WaveTerrainNode* n)
   {
      const bool noteDriven = n->NoteInput().GetSource() != nullptr;
      const int voices = n->ActiveVoices();
      const std::string& orbitName = WaveTerrainNode::OrbitTypeNames()[std::clamp(n->orbitType, 0, (int)WaveTerrainNode::OrbitTypeNames().size() - 1)];

      char stat[128];
      if (noteDriven)
         snprintf(stat, sizeof(stat), "%s  -  %d voice%s", orbitName.c_str(), voices, voices == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "%s  -  free run %s", orbitName.c_str(), UiFmt::Hz(n->frequency).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Header: Orbit dropdown, Channel dropdown, Octave, Semi, Fine
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = tok::space_1;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float orbW = (w - gap) * 0.5f;
         const float chanW = w - gap - orbW;

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("wtOrbit", WaveTerrainNode::OrbitTypeNames(), n->orbitType,
                           [n](int i) { PushUndoCheckpoint(); n->orbitType = i; }, orbW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + orbW + gap, y));
         AudioBareDropdown("wtChan", WaveTerrainNode::ChannelModeNames(), n->channel,
                           [n](int i) { PushUndoCheckpoint(); n->channel = i; }, chanW);

         // Selectors on their own row; tuning row below is the same size as every synth header.
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
         const float y2 = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(x0, y2));
         AudioSlider("fine", &n->fine, params::WaveTerrain::fine_lo, params::WaveTerrain::fine_hi, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y2));
         AudioBareDropdown("wtOct", OctaveNames(), n->octave - params::osc::octave_lo,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i + params::osc::octave_lo; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y2));
         AudioBareDropdown("wtSemi", SemiNames(), n->semi - params::osc::semi_lo,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i + params::osc::semi_lo; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y2));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Baked Wavetable Stack - the node bakes the incoming texture into an
      // 8-frame band-limited wavetable bank every rebuild (orbit-sampled ->
      // FFT -> mip pyramid, see WaveTerrainNode::RenderPreview); this shows
      // those baked cycles directly (stacked morph frames, à la a classic
      // wavetable editor) instead of the raw texture + orbit path, so the
      // display matches what the audio thread is actually playing back.
      {
         const float containerW = gAudioContentW;
         const float previewH = tok::osc_preview_h;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 containerBr(origin.x + containerW, origin.y + previewH);

         dl->AddRectFilled(origin, containerBr, tok::Role(tok::col::surface_deep), tok::radius_control);
         dl->PushClipRect(origin, containerBr, true);

         const int frameCount = WaveTerrainNode::kDisplayFrameCount;
         const float framePos = std::clamp(n->position, 0.0f, 1.0f) * (float)(frameCount - 1);

         // Stack offset: each frame further from the current morph position
         // recedes up-and-right, like looking down a row of wavetable cycles.
         const float stackDx = tok::osc_stack_dx;
         const float stackDy = tok::osc_stack_dy;
         const float traceW = containerW - stackDx * (frameCount - 1) - tok::space_3;
         const float traceH = (previewH - stackDy * (frameCount - 1) - tok::osc_wave_terrain_trace_h) * tok::osc_trace_h_ratio;
         constexpr int kPlotPoints = tok::osc_plot_points;

         auto sampleFrame = [n](int frame, float t) -> float {
            const float x = t * (float)WaveTerrainNode::kDisplayFrameSize;
            const int i0 = (int)x & (WaveTerrainNode::kDisplayFrameSize - 1);
            const int i1 = (i0 + 1) & (WaveTerrainNode::kDisplayFrameSize - 1);
            const float fx = x - floorf(x);
            const float* d = n->DisplayFrame(frame);
            return d[i0] + (d[i1] - d[i0]) * fx;
         };

         // Back-to-front so nearer (closer to the current morph position)
         // traces draw on top of farther ones.
         int order[WaveTerrainNode::kDisplayFrameCount];
         for (int i = 0; i < frameCount; i++)
            order[i] = i;
         std::sort(order, order + frameCount, [framePos](int a, int b) {
            return fabsf((float)a - framePos) > fabsf((float)b - framePos);
         });

         for (int oi = 0; oi < frameCount; oi++)
         {
            const int f = order[oi];
            const float dist = fabsf((float)f - framePos) / (float)std::max(1, frameCount - 1);
            const float prox = 1.0f - std::clamp(dist, 0.0f, 1.0f); // 1 = current frame

            const float baseX = origin.x + tok::space_1_5 + stackDx * (float)f;
            const float baseY = origin.y + previewH - tok::space_2 - stackDy * (float)f;

            ImVec2 pts[kPlotPoints];
            for (int i = 0; i < kPlotPoints; i++)
            {
               const float t = (float)i / (float)(kPlotPoints - 1);
               const float s = sampleFrame(f, t);
               pts[i] = ImVec2(baseX + t * traceW, baseY - traceH * 0.5f - s * traceH * 0.5f);
            }

            const ImU32 glow = tok::U32(tok::osc_glow_shade, (int)(30 + 40 * prox));
            const bool isLight = IsThemeLight();
            const ImU32 coreLo = isLight ? tok::U32(tok::osc_core_lo_shade_sm, (int)(70 + 120 * prox))
                                          : tok::U32(tok::osc_core_lo_shade_lg, (int)(70 + 130 * prox));
            const ImU32 coreHi = isLight ? tok::Base(tok::col::accent_dark_muted_hi) : tok::Base(tok::col::accent_alt_light_warm_fixed);
            const ImU32 core = prox > 0.97f ? coreHi : coreLo;

            dl->AddPolyline(pts, kPlotPoints, glow, 0, prox > tok::osc_wave_terrain_body_stroke ? tok::osc_wave_terrain_line_stroke_lg : tok::osc_wave_terrain_line_stroke_md);
            dl->AddPolyline(pts, kPlotPoints, core, 0, prox > tok::osc_wave_terrain_body_stroke ? tok::osc_wave_terrain_line_stroke_sm : 1.0f);
         }

         dl->PopClipRect();
         dl->AddRect(origin, containerBr, tok::Role(tok::col::edge_lo, tok::osc_outline_alpha_lg), tok::osc_wave_terrain_body_radius, 0, tok::osc_outline_stroke);

         char label[64];
         snprintf(label, sizeof(label), "wavetable  -  frame %s / %d", UiFmt::F2(n->position * (frameCount - 1)).c, frameCount - 1);
         dl->AddText(ImVec2(origin.x + tok::space_2, origin.y + tok::space_1_5), tok::Role(tok::col::text_secondary, tok::osc_wave_terrain_text_alpha), label);

         ImGui::Dummy(ImVec2(containerW, previewH));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));

      // Section 1: Terrain Trajectory & Geometry
      BeginAudioSection("orbit & terrain geometry");
      {
         AudioKnobRow row1(tok::osc_wave_terrain_body_audio_knob_row_layout);
         row1.Knob("centerX", &n->centerX, 0.0f, 1.0f, "%.2f");
         row1.Knob("centerY", &n->centerY, 0.0f, 1.0f, "%.2f");
         row1.Knob("radiusX", &n->radiusX, params::WaveTerrain::radius_x_lo, params::WaveTerrain::radius_x_hi, "%.2f");
         row1.Knob("radiusY", &n->radiusY, params::WaveTerrain::radius_y_lo, params::WaveTerrain::radius_y_hi, "%.2f");
         row1.End();

         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_xs));
         AudioKnobRow row2(tok::osc_wave_terrain_body_audio_knob_row_layout);
         row2.Knob("ratioA", &n->ratioA, params::WaveTerrain::ratio_a_lo, params::WaveTerrain::ratio_a_hi, "%.0f");
         row2.Knob("ratioB", &n->ratioB, params::WaveTerrain::ratio_b_lo, params::WaveTerrain::ratio_b_hi, "%.0f");
         row2.Knob("rotation", &n->rotation, params::WaveTerrain::rotation_lo, params::WaveTerrain::rotation_hi, "%.0f deg");
         row2.Knob("speed", &n->scanSpeed, params::WaveTerrain::scan_speed_lo, params::WaveTerrain::scan_speed_hi, "%.2f");
         row2.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Section 2: Synth Voice & Timbre
      BeginAudioSection("voice & filter");
      {
         AudioKnobRow row1(tok::osc_wave_terrain_body_audio_knob_row_layout);
         row1.Knob("volume", &n->volume, params::WaveTerrain::volume_lo, params::WaveTerrain::volume_hi, "%.2f");
         row1.Knob("pan", &n->pan, -1.0f, 1.0f, "%.2f");
         row1.KnobInt("unison", &n->unison, 1, WaveTerrainNode::kMaxUnison);
         ImGui::BeginDisabled((n->unison <= 1) && !gParamRegisterOnly);
         row1.Knob("detune", &n->detune, params::WaveTerrain::detune_lo, params::WaveTerrain::detune_hi, "%.1f c");
         ImGui::EndDisabled();
         row1.End();

         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_xs));
         const bool filterOff = (n->filterType == 0);
         AudioKnobRow row2(tok::osc_wave_terrain_body_audio_knob_row_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);
         row2.DropdownKnob("wtFilter", WaveTerrainNode::FilterTypeNames(), n->filterType,
                          [n](int i) { PushUndoCheckpoint(); n->filterType = i; },
                          "cutoff", &n->cutoff, params::WaveTerrain::cutoff_lo, params::WaveTerrain::cutoff_hi, "%.0f Hz", filterOff);
         ImGui::BeginDisabled((filterOff) && !gParamRegisterOnly);
         row2.Knob("reso", &n->resonance, 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         row2.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row2.Knob("morph", &n->position, 0.0f, 1.0f, "%.2f");
         row2.End();
      }
      EndAudioSection();

      // Interactive ADSR Envelopes
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope", "##wtAmpEnv", &n->ampAttack,
                        &n->ampDecay, &n->ampSustain, &n->ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::Role(tok::col::accent_alt_vivid_warm_lo, tok::osc_envelope_alpha_md));
      ImGui::PopID();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));
      BeginAudioSection("master & scope");
      DrawWaveTerrainScope(n, 48.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      {
         AudioKnobRow row(tok::osc_row_layout_sm);
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->frequency, params::WaveTerrain::frequency_lo, params::WaveTerrain::frequency_hi, "%.0f Hz");
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("glide", &n->glide, params::WaveTerrain::glide_lo, params::WaveTerrain::glide_hi, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row.End();
      }
      EndAudioSection();

       EndAudioBody();
    }


   void DrawEquationVisualizer(EquationNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();

      const ImU32 gridCol = isLight ? tok::Base(tok::col::surface_hi, 180) : tok::Base(tok::col::edge_lo, 180);
      const ImU32 axisCol = isLight ? tok::Base(tok::col::text_secondary, 240) : tok::Base(tok::col::surface_lo_pale_fixed, 240);
      const ImU32 textCol = isLight ? tok::Base(tok::col::edge_mid, 200) : tok::Base(tok::col::edge_mid, 200);

      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float midY = origin.y + h * 0.5f;
      float originX = origin.x;
      if (n->domainMode == EquationDsp::kDomainZeroToOne)
         originX = origin.x + tok::space_2;
      else
         originX = origin.x + w * 0.5f;

      const float scaleY = (h * 0.5f - tok::space_2);
      dl->AddLine(ImVec2(origin.x, midY - scaleY), ImVec2(br.x, midY - scaleY), gridCol, tok::osc_equation_visualizer_stroke);
      dl->AddLine(ImVec2(origin.x, midY - scaleY * 0.5f), ImVec2(br.x, midY - scaleY * 0.5f), gridCol, tok::osc_equation_visualizer_stroke);
      dl->AddLine(ImVec2(origin.x, midY + scaleY * 0.5f), ImVec2(br.x, midY + scaleY * 0.5f), gridCol, tok::osc_equation_visualizer_stroke);
      dl->AddLine(ImVec2(origin.x, midY + scaleY), ImVec2(br.x, midY + scaleY), gridCol, tok::osc_equation_visualizer_stroke);

      for (int i = 1; i < 8; i++)
      {
         const float gx = origin.x + w * (float)i / 8.0f;
         dl->AddLine(ImVec2(gx, origin.y), ImVec2(gx, br.y), gridCol, tok::osc_equation_visualizer_stroke);
      }

      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), axisCol, tok::osc_equation_visualizer_line_stroke);
      dl->AddLine(ImVec2(originX, origin.y), ImVec2(originX, br.y), axisCol, tok::osc_equation_visualizer_line_stroke);

      dl->AddText(ImVec2(origin.x + tok::space_1, midY - scaleY - 1.0f), textCol, "+1");
      dl->AddText(ImVec2(origin.x + tok::space_1, midY + scaleY - tok::osc_equation_visualizer_gap), textCol, "-1");
      if (n->domainMode == EquationDsp::kDomainZeroToOne)
      {
         dl->AddText(ImVec2(origin.x + tok::space_5, br.y - tok::osc_equation_visualizer_gap), textCol, "0");
         dl->AddText(ImVec2(br.x - tok::space_3_5, br.y - tok::osc_equation_visualizer_gap), textCol, "1");
      }
      else if (n->domainMode == EquationDsp::kDomainNegPiToPi)
      {
         dl->AddText(ImVec2(origin.x + tok::space_5, br.y - tok::osc_equation_visualizer_gap), textCol, "-pi");
         dl->AddText(ImVec2(originX + tok::space_0_75, br.y - tok::osc_equation_visualizer_gap), textCol, "0");
         dl->AddText(ImVec2(br.x - tok::osc_text_x_lg, br.y - tok::osc_equation_visualizer_gap), textCol, "+pi");
      }
      else
      {
         dl->AddText(ImVec2(origin.x + tok::space_5, br.y - tok::osc_equation_visualizer_gap), textCol, "-1");
         dl->AddText(ImVec2(originX + tok::space_0_75, br.y - tok::osc_equation_visualizer_gap), textCol, "0");
         dl->AddText(ImVec2(br.x - tok::space_3_5, br.y - tok::osc_equation_visualizer_gap), textCol, "+1");
      }

      if (n->scopeCacheCount > 1 && n->ActiveVoices() > 0)
      {
         const int sc = n->scopeCacheCount;
         dl->PathClear();
         for (int i = 0; i < sc; i++)
         {
            const float t = (float)i / (float)(sc - 1);
            const float sy = midY - n->scopeCache[i] * scaleY * tok::osc_sy_ratio;
            dl->PathLineTo(ImVec2(origin.x + t * w, sy));
         }
         dl->PathStroke(isLight ? tok::Base(tok::col::accent_alt_deep, tok::osc_equation_visualizer_path_alpha_xs) : tok::Base(tok::col::accent_alt_vivid_warm_hi, tok::osc_equation_visualizer_path_alpha_sm), 0, tok::osc_equation_visualizer_path_stroke_md);
      }

      const auto& curve = n->PreviewCurve();
      if (!curve.empty())
      {
         const int nPts = (int)curve.size();
         dl->PathClear();
         for (int i = 0; i < nPts; i++)
         {
            const float t = (float)i / (float)(nPts - 1);
            const float cy = midY - std::clamp(curve[i], -tok::osc_cy_lo, tok::osc_cy_lo) * scaleY;
            dl->PathLineTo(ImVec2(origin.x + t * w, cy));
         }
         dl->PathStroke(isLight ? tok::Base(tok::col::accent, tok::osc_equation_visualizer_path_alpha_sm) : tok::Base(tok::col::accent_alt_dark_vivid, tok::osc_equation_visualizer_path_alpha_md), 0, tok::osc_equation_visualizer_path_stroke_lg);

         dl->PathClear();
         for (int i = 0; i < nPts; i++)
         {
            const float t = (float)i / (float)(nPts - 1);
            const float cy = midY - std::clamp(curve[i], -tok::osc_cy_lo, tok::osc_cy_lo) * scaleY;
            dl->PathLineTo(ImVec2(origin.x + t * w, cy));
         }
         dl->PathStroke(isLight ? tok::Base(tok::col::accent, tok::osc_equation_visualizer_path_alpha_lg) : tok::Base(tok::col::accent_alt_vivid_warm_hi, tok::osc_equation_visualizer_path_alpha_xl), 0, tok::osc_equation_visualizer_path_stroke_sm);
      }

      if (!n->LastError().empty())
      {
         const std::string errText = "[!] " + n->LastError();
         const ImVec2 txtSz = ImGui::CalcTextSize(errText.c_str());
         const ImVec2 pillBr(br.x - tok::space_1_5, origin.y + tok::space_1_5 + txtSz.y + tok::space_1);
         const ImVec2 pillTl(pillBr.x - txtSz.x - tok::space_2_5, origin.y + tok::space_1_5);
         dl->AddRectFilled(pillTl, pillBr, tok::Role(tok::col::state_err_dark, tok::osc_equation_visualizer_fill_alpha_lg), tok::osc_equation_visualizer_radius);
         dl->AddText(ImVec2(pillTl.x + tok::space_1_25, pillTl.y + tok::space_0_5), tok::Role(tok::col::edge_hi), errText.c_str());
      }
      else
      {
         const char* tag = "y = f(x)";
         const ImVec2 txtSz = ImGui::CalcTextSize(tag);
         const ImVec2 pillBr(br.x - tok::space_1_5, origin.y + tok::space_1_5 + txtSz.y + tok::space_1);
         const ImVec2 pillTl(pillBr.x - txtSz.x - tok::space_2, origin.y + tok::space_1_5);
         dl->AddRectFilled(pillTl, pillBr, isLight ? tok::Base(tok::col::edge_hi, tok::osc_equation_visualizer_fill_alpha_sm) : tok::Base(tok::col::surface_lo, tok::osc_equation_visualizer_fill_alpha_sm), tok::osc_equation_visualizer_radius);
         dl->AddText(ImVec2(pillTl.x + tok::space_1, pillTl.y + tok::space_0_5), isLight ? tok::Base(tok::col::text_accent, tok::osc_equation_visualizer_text_alpha) : tok::Base(tok::col::accent_alt_vivid_warm_lo, tok::osc_equation_visualizer_text_alpha), tag);
      }

      AudioViz::End(vizFrame);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawEquationBody(GraphNode& gn, EquationNode* n)
   {
      const bool noteDriven = n->NoteInput().GetSource() != nullptr;
      const int voices = n->ActiveVoices();

      char stat[128];
      if (noteDriven)
         snprintf(stat, sizeof(stat), "Equation  -  %d voice%s", voices, voices == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "Equation  -  free run %s", UiFmt::Hz(n->frequency).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Header: Preset dropdown, Domain dropdown, Octave, Semi, Fine
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = tok::space_1;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float preW = (w - gap) * tok::osc_pre_w_ratio;
         const float domW = w - gap - preW;

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("eqPreset", EquationNode::PresetNames(), n->presetIndex,
                           [n](int i) { PushUndoCheckpoint(); n->LoadPreset(i); }, preW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + preW + gap, y));
         AudioBareDropdown("eqDom", EquationNode::DomainNames(), n->domainMode,
                           [n](int i) { PushUndoCheckpoint(); n->domainMode = i; n->CompileEquation(); }, domW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
         const float y2 = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(x0, y2));
         AudioSlider("fine", &n->fine, params::Equation::fine_lo, params::Equation::fine_hi, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y2));
         AudioBareDropdown("eqOct", OctaveNames(), n->octave - params::osc::octave_lo,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i + params::osc::octave_lo; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y2));
         AudioBareDropdown("eqSemi", SemiNames(), n->semi - params::osc::semi_lo,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i + params::osc::semi_lo; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y2));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Interactive Cartesian X-Y Visualizer
      DrawEquationVisualizer(n, 128.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Formula Input Field
      {
         char buf[512];
         std::strncpy(buf, n->formula.c_str(), sizeof(buf));
         buf[sizeof(buf) - 1] = '\0';
         ImGui::SetNextItemWidth(gAudioContentW);
         if (FieldWell::InputTextWithHint("##formula", "y = f(x, a, b, c, d, t)", buf, sizeof(buf)))
         {
            PushUndoCheckpoint();
            n->formula = buf;
            n->CompileEquation();
         }
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Section 1: Function Knobs (A, B, C, D)
      BeginAudioSection("function parameters");
      {
         AudioKnobRow row(tok::osc_equation_body_audio_knob_row_layout, kKnobLarge);
         row.Knob("A", &n->knobA, 0.0f, 1.0f, "%.3f");
         row.Knob("B", &n->knobB, 0.0f, 1.0f, "%.3f");
         row.Knob("C", &n->knobC, 0.0f, 1.0f, "%.3f");
         row.Knob("D", &n->knobD, 0.0f, 1.0f, "%.3f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Section 2: Synth Voice & Filter
      BeginAudioSection("voice & filter");
      {
         AudioKnobRow row1(tok::osc_equation_body_audio_knob_row_layout);
         row1.Knob("volume", &n->volume, params::Equation::volume_lo, params::Equation::volume_hi, "%.2f");
         row1.Knob("pan", &n->pan, -1.0f, 1.0f, "%.2f");
         row1.KnobInt("unison", &n->unison, 1, EquationNode::kMaxUnison);
         ImGui::BeginDisabled((n->unison <= 1) && !gParamRegisterOnly);
         row1.Knob("detune", &n->detune, params::Equation::detune_lo, params::Equation::detune_hi, "%.1f c");
         ImGui::EndDisabled();
         row1.End();

         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_xs));
         const bool filterOff = (n->filterType == 0);
         AudioKnobRow row2(tok::osc_equation_body_audio_knob_row_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);
         row2.DropdownKnob("eqFilter", EquationNode::FilterTypeNames(), n->filterType,
                           [n](int i) { PushUndoCheckpoint(); n->filterType = i; },
                           "cutoff", &n->cutoff, params::Equation::cutoff_lo, params::Equation::cutoff_hi, "%.0f Hz", filterOff);
         ImGui::BeginDisabled((filterOff) && !gParamRegisterOnly);
         row2.Knob("reso", &n->resonance, 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         row2.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row2.Knob("glide", &n->glide, 0.0f, 1.0f, "%.3f s", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row2.End();
      }
      EndAudioSection();

      // Section 3: Amp Envelope
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope", "##eqAmpEnv", &n->ampAttack,
                        &n->ampDecay, &n->ampSustain, &n->ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::Role(tok::col::accent_alt_vivid_warm_lo, tok::osc_envelope_alpha_md));
      ImGui::PopID();

      EndAudioBody();
   }


   void DrawImageSpectralSynthScope(ImageSpectralSynthNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / tok::motion_hz_scope)
      {
         float buf[ImageSpectralSynthNode::kScopeCapacity];
         const int count = n->ReadScope(buf, ImageSpectralSynthNode::kScopeCapacity);
         if (count > 0)
         {
            std::copy(buf, buf + count, n->scopeCache);
            n->scopeCacheCount = count;
         }
         n->scopeCacheTime = now;
      }

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      for (int i = 1; i < 8; i++)
         dl->AddLine(ImVec2(origin.x + w * i / tok::osc_image_spectral_synth_scope_ratio, origin.y), ImVec2(origin.x + w * i / tok::osc_image_spectral_synth_scope_ratio, br.y),
                     ScopeGridCol(), tok::osc_image_spectral_synth_scope_stroke);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), tok::osc_image_spectral_synth_scope_stroke);

      if (n->scopeCacheCount > 1)
      {
         const int count = n->scopeCacheCount;
         for (int pass = 0; pass < 2; pass++)
         {
            dl->PathClear();
            for (int i = 0; i < count; i++)
            {
               const float t = (float)i / (float)(count - 1);
               const float y = midY - n->scopeCache[i] * (h * tok::osc_image_spectral_synth_scope_scale);
               dl->PathLineTo(ImVec2(origin.x + t * w, y));
            }
            if (pass == 0)
               dl->PathStroke(isLight ? tok::Base(tok::col::viz_teal, tok::osc_image_spectral_synth_scope_path_alpha_sm) : tok::Base(tok::col::viz_teal_pale_cool_lo, tok::osc_image_spectral_synth_scope_path_alpha_sm), 0, tok::osc_image_spectral_synth_scope_path_stroke_lg);
            else
               dl->PathStroke(isLight ? tok::Base(tok::col::viz_teal, tok::osc_image_spectral_synth_scope_path_alpha_lg) : tok::Base(tok::col::viz_teal_pale_cool_hi, tok::osc_image_spectral_synth_scope_path_alpha_lg), 0, tok::osc_image_spectral_synth_scope_path_stroke_sm);
         }
      }
      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawImageSpectralSynthBody(GraphNode& gn, ImageSpectralSynthNode* n)
   {
      const bool noteDriven = n->NoteInput().GetSource() != nullptr;
      const int voices = n->ActiveVoices();
      const float playhead = n->Playhead();
      const std::string& modeName = ImageSpectralSynthNode::ScanModeNames()[std::clamp(n->scanMode, 0, (int)ImageSpectralSynthNode::ScanModeNames().size() - 1)];

      char stat[128];
      if (noteDriven)
         snprintf(stat, sizeof(stat), "%s  -  %d voice%s  (%s)", modeName.c_str(), voices, voices == 1 ? "" : "s", UiFmt::PctOf(playhead).c);
      else
         snprintf(stat, sizeof(stat), "%s  -  drone  (%s)", modeName.c_str(), UiFmt::PctOf(playhead).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Top Tuning Bar: Fine, Octave, Semi
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = tok::space_1;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioSlider("fine", &n->fine, params::ImageSpectralSynth::fine_lo, params::ImageSpectralSynth::fine_hi, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("specOct", OctaveNames(), n->octave - params::osc::octave_lo,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i + params::osc::octave_lo; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("specSemi", SemiNames(), n->semi - params::osc::semi_lo,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i + params::osc::semi_lo; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Live Spectrogram Viewport with Neon Laser Scanhead Overlay
      {
         const float containerW = gAudioContentW;
         const float previewH = tok::osc_preview_h;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 containerBr(origin.x + containerW, origin.y + previewH);

         // Background panel
         dl->AddRectFilled(origin, containerBr, tok::Role(tok::col::surface_deep), tok::radius_control);
         dl->AddRect(origin, containerBr, tok::Role(tok::col::edge_lo, tok::osc_outline_alpha_lg), tok::osc_image_spectral_synth_body_radius, 0, tok::osc_image_spectral_synth_body_stroke);

         const float imgSize = previewH - tok::space_2;
         const float imgX = origin.x + (containerW - imgSize) * 0.5f;
         const float imgY = origin.y + tok::space_1;
         const ImVec2 imgTl(imgX, imgY);
         const ImVec2 imgBr(imgX + imgSize, imgY + imgSize);

         if (n->GetOutputTexture() != 0)
         {
            Thumb::Add(dl, (ImTextureID)(intptr_t)n->GetOutputTexture(), imgTl, imgBr, ImVec2(0, 1), ImVec2(1, 0));
            dl->AddRect(imgTl, imgBr, tok::Role(tok::col::viz_teal_pale_cool_lo, tok::osc_outline_alpha_sm), tok::osc_outline_radius, 0, tok::osc_image_spectral_synth_body_stroke);
         }

         // Dynamic Glowing Neon Laser Scanhead Overlay (drawn cleanly on top of image every frame)
         const float playhead = n->Playhead();
         const float laserX = imgTl.x + std::clamp(playhead, 0.0f, 1.0f) * imgSize;

         // Outer cyan glow
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), tok::Role(tok::col::viz_teal_pale_cool_lo, tok::osc_image_spectral_synth_line_alpha_sm), tok::osc_image_spectral_synth_line_stroke_lg);
         // Mid cyan glow
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), tok::Role(tok::col::viz_teal_pale_cool_hi, tok::osc_image_spectral_synth_line_alpha_lg), tok::osc_image_spectral_synth_line_stroke_md);
         // Sharp core laser line
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), tok::Role(tok::col::ink_white), tok::osc_image_spectral_synth_line_stroke_sm);

         // Top & Bottom laser pointer diamond markers
         const float triSize = tok::osc_tri_size;
         dl->AddTriangleFilled(ImVec2(laserX - triSize, imgTl.y), ImVec2(laserX + triSize, imgTl.y), ImVec2(laserX, imgTl.y + triSize * tok::osc_image_spectral_synth_body_ratio), tok::Role(tok::col::viz_teal_pale_cool_hi));
         dl->AddTriangleFilled(ImVec2(laserX - triSize, imgBr.y), ImVec2(laserX + triSize, imgBr.y), ImVec2(laserX, imgBr.y - triSize * tok::osc_image_spectral_synth_body_ratio), tok::Role(tok::col::viz_teal_pale_cool_hi));

         // Frequency ticks along left margin
         const char* freqLabels[5] = { "16k", "4k", "1k", "250", "40" };
         for (int i = 0; i < 5; i++)
         {
            const float ty = imgTl.y + (float)i * (imgSize / tok::osc_ty_scale) - tok::space_1_5;
            dl->AddText(ImVec2(origin.x + tok::space_1, ty), tok::Role(tok::col::text_secondary, tok::osc_image_spectral_synth_text_alpha), freqLabels[i]);
         }

         // Invisible button for interactive mouse scrubbing across spectrogram
         ImGui::SetCursorScreenPos(imgTl);
         UiInteract::Hit("##spectrogramScrub", ImVec2(imgSize, imgSize));
         if (ImGui::IsItemActive())
         {
            const ImVec2 m = ImGui::GetMousePos();
            const float scrubNorm = std::clamp((m.x - imgTl.x) / imgSize, 0.0f, 1.0f);
            n->position = scrubNorm;
            if (n->scanMode == SpectralAdditiveDsp::kScanOneShot)
               n->TriggerScan();
         }

         ImGui::SetCursorScreenPos(origin);
         ImGui::Dummy(ImVec2(containerW, previewH));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));

      // Section 1: Transport & Scanning
      BeginAudioSection("transport & scanning");
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = tok::space_1;
         const bool isBpm = (n->scanMode == SpectralAdditiveDsp::kScanBpmSync);
         const bool isManual = (n->scanMode == SpectralAdditiveDsp::kScanManual);
         const float modeW = isBpm ? (w * tok::osc_image_spectral_synth_mode_w_sm) : (isManual ? (w * tok::osc_image_spectral_synth_mode_w_lg) : (w * tok::osc_image_spectral_synth_mode_w_md));
         const float rateW = isBpm ? (w * tok::osc_image_spectral_synth_rate_w) : 0.0f;
         const bool isOneShot = (n->scanMode == SpectralAdditiveDsp::kScanOneShot);
         const float trigW = isOneShot ? tok::osc_trig_w : 0.0f;
         const float dirW = isManual ? 0.0f : std::max(tok::osc_dir_w_min, (w - modeW - (isBpm ? (rateW + gap) : 0.0f) - (isOneShot ? (trigW + gap) : 0.0f) - gap));

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("specMode", ImageSpectralSynthNode::ScanModeNames(), n->scanMode,
                           [n](int i) { PushUndoCheckpoint(); n->scanMode = i; }, modeW);

         if (isBpm)
         {
            ImGui::SetCursorScreenPos(ImVec2(x0 + modeW + gap, y));
            AudioBareDropdown("specRate", MusicTime::RateDivisionList(), n->rate,
                              [n](int i) { PushUndoCheckpoint(); n->rate = i; }, rateW);
         }

         if (isOneShot)
         {
            ImGui::SetCursorScreenPos(ImVec2(x0 + w - dirW - gap - trigW, y));
            if (ActionButton::Draw("Trig", ImVec2(trigW, ImGui::GetFrameHeight())))
            {
               n->TriggerScan();
            }
         }

         if (!isManual)
         {
            ImGui::SetCursorScreenPos(ImVec2(x0 + w - dirW, y));
            const char* dirLabel = (n->direction == 0) ? "Fwd" : "Rev";
            if (ActionButton::Draw(dirLabel, ImVec2(dirW, ImGui::GetFrameHeight())))
            {
               PushUndoCheckpoint();
               n->direction = 1 - n->direction;
            }
         }

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));

         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_xs));
         AudioKnobRow row(tok::osc_image_spectral_synth_body_audio_knob_row_layout);
         if (isManual)
         {
            row.Knob("pos", &n->position, 0.0f, 1.0f, "%.2f");
         }
         else
         {
            row.Knob("speed", &n->scanSpeed, params::ImageSpectralSynth::scan_speed_lo, params::ImageSpectralSynth::scan_speed_hi, "%.2fx");
         }
         row.Knob("glide", &n->glide, params::ImageSpectralSynth::glide_lo, params::ImageSpectralSynth::glide_hi, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row.Knob("width", &n->stereoWidth, params::ImageSpectralSynth::stereo_width_lo, params::ImageSpectralSynth::stereo_width_hi, "%.2f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Section 2: Frequency & Partials Spectrum
      BeginAudioSection("frequency & partials");
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float gap = tok::space_1;

         // Row 1 Dropdowns: Partials + Frequency Scale
         float y = ImGui::GetCursorScreenPos().y;
         const float partW = w * tok::osc_part_w_ratio;
         const float scaleW = w * tok::osc_scale_w_ratio - gap;

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("specPart", ImageSpectralSynthNode::PartialsCountNames(), n->partialsChoice,
                           [n](int i) { PushUndoCheckpoint(); n->partialsChoice = i; }, partW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + partW + gap, y));
         AudioBareDropdown("specScale", ImageSpectralSynthNode::FrequencyScaleNames(), n->freqScale,
                           [n](int i) { PushUndoCheckpoint(); n->freqScale = i; }, scaleW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));

         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_xs));

         // Row 2 Dropdown + Invert Button: Color Mode + Invert Toggle
         y = ImGui::GetCursorScreenPos().y;
         const float invW = tok::osc_inv_w;
         const float colW = w - invW - gap;

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("specColor", ImageSpectralSynthNode::ColorModeNames(), n->colorMode,
                           [n](int i) { PushUndoCheckpoint(); n->colorMode = i; }, colW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + colW + gap, y));
         const char* invLabel = n->invert ? "Invert: ON" : "Invert: OFF";
         if (ActionButton::Draw(invLabel, ImVec2(invW, ImGui::GetFrameHeight())))
         {
            PushUndoCheckpoint();
            n->invert = 1 - n->invert;
         }

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));

         // Row 3 Knobs: Frequency Boundaries & Pitch
         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
         AudioKnobRow row1(tok::osc_row1_layout);
         row1.Knob("minHz", &n->minFreq, params::ImageSpectralSynth::min_freq_lo, params::ImageSpectralSynth::min_freq_hi, "%.0f Hz");
         row1.Knob("maxHz", &n->maxFreq, params::ImageSpectralSynth::max_freq_lo, params::ImageSpectralSynth::max_freq_hi, "%.0f Hz");
         row1.Knob("rootHz", &n->rootFreq, params::ImageSpectralSynth::root_freq_lo, params::ImageSpectralSynth::root_freq_hi, "%.0f Hz");
         row1.KnobInt("unison", &n->unison, 1, ImageSpectralSynthNode::kMaxUnison);
         row1.End();

         // Row 4 Knobs: Conditioning & Gain
         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_xs));
         AudioKnobRow row2(tok::osc_row1_layout);
         row2.Knob("gain", &n->brightness, params::ImageSpectralSynth::brightness_lo, params::ImageSpectralSynth::brightness_hi, "%.2f");
         row2.Knob("thresh", &n->threshold, params::ImageSpectralSynth::threshold_lo, params::ImageSpectralSynth::threshold_hi, "%.2f");
         row2.Knob("contrast", &n->contrast, params::ImageSpectralSynth::contrast_lo, params::ImageSpectralSynth::contrast_hi, "%.2f");
         ImGui::BeginDisabled((n->unison <= 1) && !gParamRegisterOnly);
         row2.Knob("detune", &n->detune, params::ImageSpectralSynth::detune_lo, params::ImageSpectralSynth::detune_hi, "%.1f c");
         ImGui::EndDisabled();
         row2.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));

      // Section 3: Voice & Filter
      BeginAudioSection("voice & filter");
      {
         AudioKnobRow row1(tok::osc_row1_layout);
         row1.Knob("volume", &n->volume, params::ImageSpectralSynth::volume_lo, params::ImageSpectralSynth::volume_hi, "%.2f");
         row1.Knob("pan", &n->pan, -1.0f, 1.0f, "%.2f");
         row1.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row1.Knob("fine", &n->fine, params::ImageSpectralSynth::fine_lo, params::ImageSpectralSynth::fine_hi, "%.1f c");
         row1.End();

         ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_xs));
         const bool filterOff = (n->filterType == 0);
         AudioKnobRow row2(tok::osc_row1_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);
         row2.DropdownKnob("specFilter", ImageSpectralSynthNode::FilterTypeNames(), n->filterType,
                          [n](int i) { PushUndoCheckpoint(); n->filterType = i; },
                          "cutoff", &n->cutoff, params::ImageSpectralSynth::cutoff_lo, params::ImageSpectralSynth::cutoff_hi, "%.0f Hz", filterOff);
         ImGui::BeginDisabled((filterOff) && !gParamRegisterOnly);
         row2.Knob("reso", &n->resonance, 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         row2.End();
      }
      EndAudioSection();

      // Interactive ADSR Envelopes
      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_sm));
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope", "##specAmpEnv", &n->ampAttack,
                        &n->ampDecay, &n->ampSustain, &n->ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::Role(tok::col::viz_teal_pale_cool_lo, tok::osc_envelope_alpha_md));
      ImGui::PopID();

      ImGui::Dummy(ImVec2(0.0f, tok::osc_spacer_h_lg));
      BeginAudioSection("master & scope");
      DrawImageSpectralSynthScope(n, 48.0f, gAudioContentW);
      EndAudioSection();

      EndAudioBody();
   }
}
