// Oscillator, wavetable, ADSR, equation and spectral-synth bodies (moved verbatim from main.cpp).
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/AppShared.h"
#include "app/ui/design/components/AudioViz.h"

namespace app
{
   void DrawOscillatorScope(OscillatorNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / 30.0)
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
         dl->AddLine(ImVec2(origin.x + w * i / 8.0f, origin.y), ImVec2(origin.x + w * i / 8.0f, br.y),
                     ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

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
               dl->PathLineTo(ImVec2(origin.x + t * w, midY - v * h * 0.45f));
            }
            const ImU32 strokeCol = isLight
               ? (pass == 0 ? tok::U32(tok::pal::c_1E6EE632) : tok::U32(tok::pal::c_1464E6FF))
               : (pass == 0 ? tok::U32(tok::pal::c_78C8FF2E) : tok::U32(tok::pal::c_96D6FFF5));
            dl->PathStroke(strokeCol, 0, pass == 0 ? 4.5f : 1.6f);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "idle");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawMetallicScope(MetallicNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / 30.0)
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
         dl->AddLine(ImVec2(origin.x + w * i / 8.0f, origin.y), ImVec2(origin.x + w * i / 8.0f, br.y),
                     ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

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
               dl->PathLineTo(ImVec2(origin.x + t * w, midY - v * h * 0.44f));
            }
            const ImU32 strokeCol = isLight
               ? (pass == 0 ? tok::U32(tok::pal::c_E68C1432) : tok::U32(tok::pal::c_D2780AFF))
               : (pass == 0 ? tok::U32(tok::pal::c_FFBE5030) : tok::U32(tok::pal::c_FFD778F0));
            dl->PathStroke(strokeCol, 0, pass == 0 ? 4.5f : 1.6f);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "strike / idle");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   // Sampler's waveform + playhead: a static min/max envelope (the sample
   // data never changes during playback, so there's nothing to redecimate
   // per frame - unlike DrawWavetableScope's live trace) with a moving
   // playhead line drawn on top from SamplerNode::Playhead().
   // Interactive: clicking anywhere on the body auditions the sample from
   // that point (SamplerNode::TriggerPreview, independent of any note
   // cable), and the two edge handles drag the loop range (start/end)
   // directly on the picture rather than through separate numeric fields -
   // "where does this loop start/end" is a question the waveform answers
   // immediately and a 0..1 fraction doesn't.
   void DrawSamplerWaveform(SamplerNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;

      // Body click-catcher first, so it owns hover/active by default; the
      // handle grab-zones are added afterwards at the same screen position.
      // Overlap resolution in ImGui is NOT "last submitted wins" - a later
      // item is blocked from becoming hovered/active while an earlier item
      // already holds g.HoveredId, unless the earlier item opts in via
      // SetNextItemAllowOverlap() (called before it, not after). Without
      // this the two handle buttons below never receive mouse input at all.
      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##samplerwavebody", ImVec2(w, h));
      if (hasSample && (ImGui::IsItemActivated() || ImGui::IsItemActive()))
      {
         const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, 1.0f);
         const float target = std::clamp(frac, n->start, n->end);
         n->position = target;
         if (ImGui::IsItemActivated())
            n->TriggerPreview(target);
      }

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (hasSample)
      {
         const int count = n->waveformCacheCount;
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - n->waveformMax[i] * h * 0.45f;
            const float bottom = midY - n->waveformMin[i] * h * 0.45f;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::U32(tok::pal::c_1E6EE6D2) : tok::U32(tok::pal::c_96D6FFC8));
         }

         // Dim whatever the start/end range excludes, so the active loop
         // region reads at a glance rather than needing the two handles
         // read numerically against each other.
         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);
         const ImU32 dimCol = isLight ? tok::U32(tok::pal::c_FFFFFF8C) : tok::U32(tok::pal::c_00000082);
         if (startX > origin.x)
            dl->AddRectFilled(origin, ImVec2(startX, br.y), dimCol);
         if (endX < br.x)
            dl->AddRectFilled(ImVec2(endX, origin.y), br, dimCol);

         // Primary yellow playhead: tracks live playback position across the sample when active,
         // or parks at `position` as the starting anchor when idle.
         const auto& snap = n->VisualSnapshot();
         const float posClamped = std::clamp(n->position, n->start, n->end);
         const float activeFrac = (snap.selfActive && snap.selfPos >= 0.0f) ? snap.selfPos : posClamped;
         const float posX = origin.x + w * std::clamp(activeFrac, 0.0f, 1.0f);
         const ImU32 yellowCol = isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFC85AF0);
         dl->AddLine(ImVec2(posX, origin.y), ImVec2(posX, br.y), yellowCol, 2.0f);

         // Active note voices in flight: drawn as white playheads whose opacity
         // fades out in sync with each voice's envelope decay time (only when notes are connected/played).
         for (int v = 0; v < snap.count; v++)
         {
            const auto& voice = snap.voices[v];
            if (voice.amp < 0.002f)
               continue;
            const float px = origin.x + w * std::clamp(voice.position, 0.0f, 1.0f);
            const int alpha = (int)(voice.amp * 255.0f);
            const ImU32 whiteCol = isLight ? IM_COL32(40, 45, 55, alpha) : IM_COL32(255, 255, 255, alpha);
            dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y), whiteCol, 1.5f);
         }

         dl->AddLine(ImVec2(startX, origin.y), ImVec2(startX, br.y),
                     isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96EB), 2.0f);
         dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, br.y),
                     isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896EB), 2.0f);
      }
      else
      {
         EmptyState::DrawCaption(origin, br, "no sample loaded");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);

      if (hasSample)
      {
         // Narrow grab-zones centred on each marker, drawn (and therefore
         // hit-tested) after the body button above.
         const float handleW = 10.0f;
         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);

         // Triangle grips at top and bottom of each marker, anchored a few
         // pixels in from the true edge so they stay visible/grabbable even
         // at the default start=0/end=1 - at those values the marker line
         // sits flush on the border and is otherwise unreadable as a handle.
         const float grip = 8.0f;
         const float startGripX = std::clamp(startX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const float endGripX = std::clamp(endX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const ImU32 startCol = isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96FF);
         const ImU32 endCol = isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896FF);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, origin.y), ImVec2(startGripX + grip * 0.5f, origin.y), ImVec2(startGripX, origin.y + grip), startCol);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, br.y), ImVec2(startGripX + grip * 0.5f, br.y), ImVec2(startGripX, br.y - grip), startCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, origin.y), ImVec2(endGripX + grip * 0.5f, origin.y), ImVec2(endGripX, origin.y + grip), endCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, br.y), ImVec2(endGripX + grip * 0.5f, br.y), ImVec2(endGripX, br.y - grip), endCol);

         const float startBtnX = std::clamp(startX - handleW * 0.5f, origin.x, br.x - handleW);
         const float endBtnX = std::clamp(endX - handleW * 0.5f, origin.x, br.x - handleW);

         ImGui::SetCursorScreenPos(ImVec2(startBtnX, origin.y));
         ImGui::InvisibleButton("##samplerstarthandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
            n->start = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, n->end - 0.01f);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

         ImGui::SetCursorScreenPos(ImVec2(endBtnX, origin.y));
         ImGui::InvisibleButton("##samplerendhandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
            n->end = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, n->start + 0.01f, 1.0f);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
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
      ImGui::InvisibleButton("##slicerwavebody", ImVec2(w, h));
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
         char val[48];
         snprintf(val, sizeof(val), "slice %d - %s%d - click to audition", hit + 1,
                  kPitchClass[note % 12], note / 12 - 1);
         SetAudioReadout("#", val);
      }

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (hasSample)
      {
         // Alternating band shading behind the waveform, so "where does one
         // slice end" reads without counting marker lines.
         for (int i = 0; i < sliceCount; i += 2)
         {
            const float x0 = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            const float x1 = origin.x + w * ((i + 1 < sliceCount) ? std::clamp(slices[i + 1], 0.0f, 1.0f) : 1.0f);
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y),
                              isLight ? tok::U32(tok::pal::c_0000000E) : tok::U32(tok::pal::c_FFFFFF0C));
         }

         const int count = n->waveformCacheCount;
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - n->waveformMax[i] * h * 0.45f;
            const float bottom = midY - n->waveformMin[i] * h * 0.45f;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::U32(tok::pal::c_1E6EE6D2) : tok::U32(tok::pal::c_96D6FFC8));
         }

         // Voices in flight, faded by their own amplitude.
         const SlicerVoiceSnapshot& snap = n->VisualSnapshot();
         for (int v = 0; v < snap.count; v++)
         {
            if (snap.voices[v].amp < 0.002f)
               continue;
            const float px = origin.x + w * std::clamp(snap.voices[v].position, 0.0f, 1.0f);
            const int alpha = (int)(std::clamp(snap.voices[v].amp, 0.0f, 1.0f) * 235.0f) + 20;
            dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y),
                        isLight ? IM_COL32(230, 140, 20, alpha) : IM_COL32(255, 200, 90, alpha), 2.0f);
         }

         // Slice markers, plus the note each slice answers to when it fits.
         const ImU32 markerCol = isLight ? tok::U32(tok::pal::c_148C5AE6) : tok::U32(tok::pal::c_78E6AFDC);
         for (int i = 0; i < sliceCount; i++)
         {
            const float x = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), markerCol, i == 0 ? 1.0f : 1.5f);

            const float nextX = origin.x + w * ((i + 1 < sliceCount) ? std::clamp(slices[i + 1], 0.0f, 1.0f) : 1.0f);
            const int note = SlicerNode::kBaseNote + i;
            char label[8];
            snprintf(label, sizeof(label), "%s%d", kPitchClass[note % 12], note / 12 - 1);
            const ImVec2 ts = ImGui::CalcTextSize(label);
            if (nextX - x > ts.x + 6.0f)
               dl->AddText(ImVec2(x + 3.0f, origin.y + 2.0f), ScopeTextCol(), label);
         }
      }
      else
      {
         EmptyState::DrawCaption(origin, br, "no sample loaded");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);

      if (hasSample && n->MarkersAreEditable())
      {
         // Narrow grab-zones centred on each marker, submitted after the body
         // button above. Marker 0 is pinned at the sample's start.
         const float handleW = 9.0f;
         for (int i = 1; i < sliceCount; i++)
         {
            const float x = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            const float btnX = std::clamp(x - handleW * 0.5f, origin.x, br.x - handleW);
            char id[32];
            snprintf(id, sizeof(id), "##slicermk%d", i);
            ImGui::SetCursorScreenPos(ImVec2(btnX, origin.y));
            ImGui::InvisibleButton(id, ImVec2(handleW, h));
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
      ImGui::InvisibleButton("##paulstretchwavebody", ImVec2(w, h));
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
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (hasSample)
      {
         const int count = n->waveformCacheCount;
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - n->waveformMax[i] * h * 0.45f;
            const float bottom = midY - n->waveformMin[i] * h * 0.45f;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::U32(tok::pal::c_2864E6D2) : tok::U32(tok::pal::c_A5B4FFD2));
         }

         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);
         const ImU32 dimCol = isLight ? tok::U32(tok::pal::c_FFFFFF8C) : tok::U32(tok::pal::c_00000082);
         if (startX > origin.x)
            dl->AddRectFilled(origin, ImVec2(startX, br.y), dimCol);
         if (endX < br.x)
            dl->AddRectFilled(ImVec2(endX, origin.y), br, dimCol);

         const float px = origin.x + w * std::clamp(n->Playhead(), 0.0f, 1.0f);
         dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y),
                     isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFC85AE6), 2.0f);

         dl->AddLine(ImVec2(startX, origin.y), ImVec2(startX, br.y),
                     isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96EB), 2.0f);
         dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, br.y),
                     isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896EB), 2.0f);
      }
      else
      {
         EmptyState::DrawCaption(origin, br, "no sample loaded");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);

      if (hasSample)
      {
         const float handleW = 10.0f;
         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);

         const float grip = 8.0f;
         const float startGripX = std::clamp(startX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const float endGripX = std::clamp(endX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const ImU32 startCol = isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96FF);
         const ImU32 endCol = isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896FF);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, origin.y), ImVec2(startGripX + grip * 0.5f, origin.y), ImVec2(startGripX, origin.y + grip), startCol);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, br.y), ImVec2(startGripX + grip * 0.5f, br.y), ImVec2(startGripX, br.y - grip), startCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, origin.y), ImVec2(endGripX + grip * 0.5f, origin.y), ImVec2(endGripX, origin.y + grip), endCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, br.y), ImVec2(endGripX + grip * 0.5f, br.y), ImVec2(endGripX, br.y - grip), endCol);

         const float startBtnX = std::clamp(startX - handleW * 0.5f, origin.x, br.x - handleW);
         const float endBtnX = std::clamp(endX - handleW * 0.5f, origin.x, br.x - handleW);

         ImGui::SetCursorScreenPos(ImVec2(startBtnX, origin.y));
         ImGui::InvisibleButton("##paulstretchstarthandle", ImVec2(handleW, h));
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
         ImGui::InvisibleButton("##paulstretchendhandle", ImVec2(handleW, h));
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
      ImGui::InvisibleButton("##granularwavebody", ImVec2(w, h));
      if (hasSample && (ImGui::IsItemActivated() || (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))))
      {
         const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, s, e);
         n->Seek(frac);
      }

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (hasSample)
      {
         const int count = n->waveformCacheCount;
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - n->waveformMax[i] * h * 0.44f;
            const float bottom = midY - n->waveformMin[i] * h * 0.44f;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::U32(tok::pal::c_285AC8C8) : tok::U32(tok::pal::c_8CA0DCAF));
         }

         const float startX = origin.x + w * s;
         const float endX = origin.x + w * e;
         const ImU32 dimCol = isLight ? tok::U32(tok::pal::c_FFFFFF8C) : tok::U32(tok::pal::c_0000008C);
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
            const float gy = midY + (gr.pan * 0.38f * h);
            const float radius = 2.0f + gr.amp * 3.5f;

            // Outer glow
            const int glowAlpha = (int)(gr.amp * 80.0f);
            dl->AddCircleFilled(ImVec2(gx, gy), radius + 2.5f,
                                isLight ? IM_COL32(20, 120, 230, glowAlpha) : IM_COL32(100, 220, 255, glowAlpha));

            // Core grain dot
            const int coreAlpha = std::min(255, (int)(gr.amp * 255.0f));
            dl->AddCircleFilled(ImVec2(gx, gy), radius,
                                isLight ? IM_COL32(20, 80, 200, coreAlpha) : IM_COL32(230, 248, 255, coreAlpha));

            // Small direction indicator / velocity streak
            const float dirLen = (gr.pitchRatio > 1.05f ? 4.0f : (gr.pitchRatio < 0.95f ? -4.0f : 0.0f));
            if (std::abs(dirLen) > 0.1f)
            {
               dl->AddLine(ImVec2(gx - dirLen, gy), ImVec2(gx + dirLen, gy),
                           isLight ? IM_COL32(210, 130, 20, (int)(gr.amp * 180.0f)) : IM_COL32(255, 235, 160, (int)(gr.amp * 160.0f)), 1.5f);
            }
         }

         // Current playing playhead (pos) strictly within [s, e]
         const float px = origin.x + w * std::clamp(n->Playhead(), s, e);
         dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y),
                     isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFCD50EB), 2.0f);
         const float pGrip = 8.0f;
         const ImU32 pCol = isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFCD50FF);
         dl->AddTriangleFilled(ImVec2(px - pGrip * 0.5f, origin.y), ImVec2(px + pGrip * 0.5f, origin.y), ImVec2(px, origin.y + pGrip), pCol);
         dl->AddTriangleFilled(ImVec2(px - pGrip * 0.5f, br.y), ImVec2(px + pGrip * 0.5f, br.y), ImVec2(px, br.y - pGrip), pCol);

         dl->AddLine(ImVec2(startX, origin.y), ImVec2(startX, br.y),
                     isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96EB), 2.0f);
         dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, br.y),
                     isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896EB), 2.0f);
      }
      else
      {
         EmptyState::DrawCaption(origin, br, "no sample loaded");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);

      if (hasSample)
      {
         const float handleW = 10.0f;
         const float startX = origin.x + w * s;
         const float endX = origin.x + w * e;

         const float grip = 8.0f;
         const float startGripX = std::clamp(startX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const float endGripX = std::clamp(endX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const ImU32 startCol = isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96FF);
         const ImU32 endCol = isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896FF);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, origin.y), ImVec2(startGripX + grip * 0.5f, origin.y), ImVec2(startGripX, origin.y + grip), startCol);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, br.y), ImVec2(startGripX + grip * 0.5f, br.y), ImVec2(startGripX, br.y - grip), startCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, origin.y), ImVec2(endGripX + grip * 0.5f, origin.y), ImVec2(endGripX, origin.y + grip), endCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, br.y), ImVec2(endGripX + grip * 0.5f, br.y), ImVec2(endGripX, br.y - grip), endCol);

         const float startBtnX = std::clamp(startX - handleW * 0.5f, origin.x, br.x - handleW);
         const float endBtnX = std::clamp(endX - handleW * 0.5f, origin.x, br.x - handleW);

         ImGui::SetCursorScreenPos(ImVec2(startBtnX, origin.y));
         ImGui::InvisibleButton("##granularstarthandle", ImVec2(handleW, h));
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
         ImGui::InvisibleButton("##granularendhandle", ImVec2(handleW, h));
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
         dl->AddLine(ImVec2(x, br.y - h * t), ImVec2(br.x, br.y - h * t), ScopeMidLineCol(), 1.0f);
      }
      const float db = levelToDb(std::max(0.0f, level));
      const float lvl = dBToFrac(db);
      if (lvl > 0.0f)
      {
         // Thresholds are the same -6 / -1 dBFS the old linear constants
         // (0.501, 0.891) encoded, just expressed directly in dB now.
         const ImU32 col = db > -1.0f   ? tok::U32(tok::pal::c_FF6056F5)
                           : db > -6.0f ? tok::U32(tok::pal::c_FFBE5AF0)
                                        : tok::U32(tok::pal::c_78D2A0EB);
         dl->AddRectFilled(ImVec2(x + 1.0f, br.y - 1.0f - (h - 2.0f) * lvl),
                           ImVec2(br.x - 1.0f, br.y - 1.0f), col, 2.0f);
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

      ImGui::InvisibleButton(id, ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();
      if (ImGui::IsItemActive())
      {
         const float x = std::clamp((ImGui::GetIO().MousePos.x - origin.x - 10.0f) / std::max(1.0f, w - 20.0f),
                                    0.0f, 1.0f);
         eng.position = x;
      }

      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      // Perspective: each successive frame steps right and up by a fixed
      // amount, so the stack reads as depth rather than as overlaid curves.
      const float headroom = 20.0f;
      const float stepX = (w * 0.22f) / (float)(Wavetable::kFrames - 1);
      const float stepY = (h - headroom - 26.0f) / (float)(Wavetable::kFrames - 1);
      const float traceW = w - stepX * (float)(Wavetable::kFrames - 1) - 20.0f;
      const float amp = (h - headroom) * 0.17f;
      const float selected = std::clamp(eng.position, 0.0f, 1.0f) * (float)(Wavetable::kFrames - 1);

      const int kSteps = 96;
      for (int f = Wavetable::kFrames - 1; f >= 0; f--)
      {
         const float baseX = origin.x + 10.0f + stepX * (float)f;
         const float baseY = br.y - 14.0f - stepY * (float)f;
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
            ? (isCurrent ? tok::U32(tok::pal::c_1464E6FF) : IM_COL32(70, 115, 175, alpha))
            : (isCurrent ? tok::U32(tok::pal::c_96D6FFFF) : IM_COL32(96, 150, 205, alpha));
         dl->PathStroke(Fade(frameStroke), 0, isCurrent ? 2.2f : 1.0f);
      }

      // Scrub handle along the bottom: without it the display looks like a
      // picture, and nothing says it can be dragged.
      const float handleX = origin.x + 10.0f + std::clamp(eng.position, 0.0f, 1.0f) * (w - 20.0f);
      dl->AddLine(ImVec2(handleX, origin.y + 3.0f), ImVec2(handleX, br.y - 3.0f),
                  isLight ? IM_COL32(0, 0, 0, hovered ? 120 : 60) : IM_COL32(255, 255, 255, hovered ? 90 : 42), 1.0f);
      const ImU32 triCol = isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_BEE0FFFF);
      dl->AddTriangleFilled(ImVec2(handleX - 5.0f, br.y - 2.0f), ImVec2(handleX + 5.0f, br.y - 2.0f),
                            ImVec2(handleX, br.y - 9.0f), Fade(triCol));

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br, hovered ? tok::U32(tok::pal::c_6E8CB4FF) : 0);
      dl->AddText(ImVec2(origin.x + 9.0f, origin.y + 5.0f),
                  Fade(isLight ? tok::U32(tok::pal::c_283041FF) : tok::U32(tok::pal::c_A0ACC4FF)),
                  Wavetable::TableName(eng.table));
      char posText[24];
      snprintf(posText, sizeof(posText), "%.3f", eng.position);
      const ImVec2 sz = ImGui::CalcTextSize(posText);
      dl->AddText(ImVec2(br.x - 9.0f - sz.x, origin.y + 5.0f),
                  isLight ? tok::U32(tok::pal::c_3C4455FF) : tok::U32(tok::pal::c_8C96ACFF), posText);
      if (hovered)
         SetAudioReadout("position", posText);
   }


   ADSRLayout ComputeADSRLayout(ImVec2 origin, float w, float h, float attackMs, float decayMs,
                                       float sustain, float releaseMs, float maxTimeMs)
   {
      ADSRLayout l;
      const float padX = 7.0f;
      const float padY = 5.0f;
      l.x0 = origin.x + padX;
      const float xEnd = origin.x + w - padX;
      const float usableW = std::max(24.0f, xEnd - l.x0);
      l.topY = origin.y + padY;
      l.baseY = origin.y + h - padY;
      l.spanY = std::max(1.0f, l.baseY - l.topY);

      const float s01 = std::clamp(sustain, 0.0f, 1.0f);
      const float susY = l.topY + (1.0f - s01) * l.spanY;

      // Proportional sustain plateau representing key-held state (18% of usable width)
      l.wShelf = std::clamp(usableW * 0.18f, 14.0f, 36.0f);
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
         const float kMinPx = 2.0f;
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
                         float* releaseMs, float maxTimeMs, float h, ImU32 color, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      ImGui::InvisibleButton(id, ImVec2(w, h));
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
         dl->AddLine(ImVec2(origin.x + 2.0f, gy), ImVec2(br.x - 2.0f, gy),
                     ScopeGridCol(), 1.0f);
      }

      ADSRLayout layout = ComputeADSRLayout(origin, w, h, *attackMs, *decayMs, *sustain, *releaseMs, maxTimeMs);

      // Subtle sustain gate vertical zone highlight
      dl->AddRectFilled(ImVec2(layout.pD.x, layout.topY), ImVec2(layout.pS.x, layout.baseY),
                        isLight ? tok::U32(tok::pal::c_00000008) : tok::U32(tok::pal::c_FFFFFF06));

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
         else if (m.x >= layout.pD.x - 2.0f && m.x <= layout.pS.x + 2.0f && fabsf(m.y - layout.pD.y) < 14.0f)
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
               const float kTieEpsPx = 2.0f;
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
            curWeight = std::clamp(curWeight + deltaX / (layout.timeW * 0.45f), 0.0f, 1.0f);
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
      // Release). Linear segments - both Envelope::Process (AudioVoice.h)
      // and EnvelopeNode::Value01 (ModulatorNodes.cpp) step linearly, so a
      // curved display would be drawing a shape the audio never makes.
      std::vector<ImVec2> curvePts;
      curvePts.reserve(24);

      // 1. Attack. A zero-width stage emits just its two endpoints - a
      // stack of coincident interpolated points at the same x antialiases
      // into a fuzzy vertical bar instead of a crisp vertical edge.
      curvePts.push_back(ImVec2(layout.x0, layout.baseY));
      if (layout.pA.x - layout.x0 >= 0.5f)
      {
         const int kAttSteps = 6;
         for (int i = 1; i < kAttSteps; i++)
         {
            const float u = (float)i / (float)kAttSteps;
            const float px = layout.x0 + u * (layout.pA.x - layout.x0);
            const float py = layout.baseY - u * layout.spanY;
            curvePts.push_back(ImVec2(px, py));
         }
      }
      curvePts.push_back(layout.pA);

      // 2. Decay.
      if (layout.pD.x - layout.pA.x >= 0.5f)
      {
         const int kDecSteps = 10;
         for (int i = 1; i < kDecSteps; i++)
         {
            const float u = (float)i / (float)kDecSteps;
            const float px = layout.pA.x + u * (layout.pD.x - layout.pA.x);
            const float py = layout.topY + u * (layout.pD.y - layout.topY);
            curvePts.push_back(ImVec2(px, py));
         }
      }
      curvePts.push_back(layout.pD);

      // 3. Sustain shelf
      curvePts.push_back(layout.pS);

      // 4. Release.
      if (layout.pR.x - layout.pS.x >= 0.5f)
      {
         const int kRelSteps = 10;
         for (int i = 1; i < kRelSteps; i++)
         {
            const float u = (float)i / (float)kRelSteps;
            const float px = layout.pS.x + u * (layout.pR.x - layout.pS.x);
            const float py = layout.pS.y + u * (layout.baseY - layout.pS.y);
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
      dl->PathStroke(color, 0, 2.0f);

      // Draw interactive handles
      const ImVec2 handlePts[4] = { layout.pA, layout.pD, layout.pS, layout.pR };
      const int activeHandle = active ? (sHeld == 2 ? 1 : sHeld) : hoveredHandle;

      for (int i = 0; i < 4; i++)
      {
         const bool isHot = (active && (sHeld == i || (sHeld == 2 && (i == 1 || i == 2)))) ||
                            (!active && (hoveredHandle == i || (hoveredHandle == 2 && (i == 1 || i == 2))));
         const float radius = isHot ? 4.8f : 3.2f;

         // Outer halo on hover/drag
         if (isHot)
            dl->AddCircleFilled(handlePts[i], radius + 3.0f, (color & 0x00FFFFFFu) | 0x40000000u, 14);

         dl->AddCircleFilled(handlePts[i], radius, isLight ? tok::U32(tok::pal::c_FFFFFFFF) : tok::U32(tok::pal::c_F0F8FFFF), 14);
         dl->AddCircle(handlePts[i], radius, isLight ? tok::U32(tok::pal::c_283041FF) : tok::U32(tok::pal::c_141822E6), 14, 1.4f);
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br, (hovered || active) ? tok::U32(tok::pal::c_6E8CB4FF) : 0);

      // State the panel's timebase so the fit-to-width layout is honest
      // instead of hidden: without this, a 200 ms envelope and an 8 s
      // envelope draw identically.
      {
         const float totalMs = *attackMs + *decayMs + *releaseMs;
         char timeText[24];
         if (totalMs >= 1000.0f)
            snprintf(timeText, sizeof(timeText), "%.1f s", totalMs / 1000.0f);
         else
            snprintf(timeText, sizeof(timeText), "%.0f ms", totalMs);
         const ImVec2 tsz = ImGui::CalcTextSize(timeText);
         // Top-right normally; bottom-centre (under the plateau) when a high sustain puts the plateau along the top. The release always ends at the bottom-right corner and the attack starts bottom-left, so neither bottom corner is free.
         const bool highSustain = *sustain > 0.6f;
         const ImVec2 labelPos = highSustain ? ImVec2(origin.x + (br.x - origin.x - tsz.x) * 0.45f, br.y - 4.0f - tsz.y)
                                             : ImVec2(br.x - 6.0f - tsz.x, origin.y + 4.0f);
         dl->AddText(labelPos,
                     isLight ? tok::U32(tok::pal::c_3C4455A0) : tok::U32(tok::pal::c_8C96AC8C), timeText);
      }

      // Rich Readout & Status info
      if (hovered || active)
      {
         char buf[96];
         if (activeHandle == 0)
            snprintf(buf, sizeof(buf), "Attack: %.0f ms", *attackMs);
         else if (activeHandle == 1 || activeHandle == 2)
            snprintf(buf, sizeof(buf), "Decay: %.0f ms | Sustain: %.2f", *decayMs, *sustain);
         else if (activeHandle == 3)
            snprintf(buf, sizeof(buf), "Release: %.0f ms", *releaseMs);
         else
            snprintf(buf, sizeof(buf), "A:%.0fms  D:%.0fms  S:%.2f  R:%.0fms", *attackMs, *decayMs, *sustain, *releaseMs);

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
            snprintf(b, sizeof(b), "oct %+d", i);
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
            snprintf(b, sizeof(b), "semi %+d", i);
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
         width = std::max(24.0f, width - (tok::pin_box + 4.0f));
      }

      const std::string caption = options[safe] + "##" + id;
      if (h.modulated)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, IsThemeLight() ? tok::V4(tok::palf::v_550_380_100_1000)
                                                             : tok::V4(tok::palf::v_1000_750_350_1000));
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
                          float amountHi, const char* amountFmt, ImU32 color)
   {
      BeginAudioSection(title);

      const float full = gAudioContentW;
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      // Wide enough that the widest label/value pair a field holds in practice
      // ("release" + "2249 ms") fits its *track*. The trap: ModSlider spends
      // the first 18px of whatever width it is given on the modulation pin, so
      // the track is 18px narrower than the cell - sizing the cell against the
      // text alone (what 0.44, then 0.52, did) leaves the text overlapping
      // even though the arithmetic looked right.
      const float fieldsW = std::clamp(full * 0.66f, 260.0f, 320.0f);
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
      const float kFieldRows = 3.0f;
      const float curveH = rowH * kFieldRows - ImGui::GetStyle().ItemSpacing.y;

      const ImVec2 top = ImGui::GetCursorScreenPos();
      DrawEditableADSR(curveId, attackMs, decayMs, sustain, releaseMs, 4000.0f, curveH, color, curveW);
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
      AudioSlider("attack", attackMs, 0.0f, 4000.0f, "%.0f ms", halfW, SkewAttack100Taper::PosToValue, SkewAttack100Taper::ValueToPos);
      ImGui::SameLine();
      AudioSlider("decay", decayMs, 0.0f, 4000.0f, "%.0f ms", halfW, SkewDecay300Taper::PosToValue, SkewDecay300Taper::ValueToPos);
      y += rowH;
      ImGui::SetCursorScreenPos(ImVec2(fieldsX, y));
      AudioSlider("sustain", sustain, 0.0f, 1.0f, "%.3f", halfW);
      ImGui::SameLine();
      AudioSlider("release", releaseMs, 0.0f, 4000.0f, "%.0f ms", halfW, SkewRelease400Taper::PosToValue, SkewRelease400Taper::ValueToPos);

      // Close the row at whichever side is taller, so EndAudioSection measures
      // the panel rather than just the field stack.
      ImGui::SetCursorScreenPos(ImVec2(top.x, std::max(top.y + curveH, y + rowH)));
      ImGui::Dummy(ImVec2(full, 0.0f));

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
         const float onW = 14.0f + 4.0f + ImGui::GetFrameHeight();
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float tableW = std::max(60.0f, w - onW - octW - semiW - fineW - gap * 4.0f);

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
         AudioSlider("fine", &eng.fine, -50.0f, 50.0f, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("wtOct", OctaveNames(), eng.octave + 4,
                           [&eng](int i) { PushUndoCheckpoint(); eng.octave = i - 4; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("wtSemi", SemiNames(), eng.semi + 12,
                           [&eng](int i) { PushUndoCheckpoint(); eng.semi = i - 12; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // The engine's picture, and its primary control: dragging across it
      // scrubs `position`, and the `position` knob below moves the highlight
      // the same way. Dimmed rather than hidden when the engine is off, so the
      // column never changes height.
      DrawWavetableFrames(eng, "##wtFrames", 150.0f, gAudioContentW, !eng.on);
      PublishWtTestRect();
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Two rows of four, per the sketch. Row two's first two cells pair a
      // mode dropdown with the knob that mode reads.
      {
         AudioKnobRow row(4);
         row.Knob("volume", &eng.volume, 0.0f, 1.0f, "%.2f");
         row.Knob("position", &eng.position, 0.0f, 1.0f, "%.3f");
         row.KnobInt("unison", &eng.unison, 1, WavetableNode::kMaxUnison);
         row.Knob("detune", &eng.detune, 0.0f, 100.0f, "%.1f c");
         row.End();
      }
      {
         const bool filterOff = !SynthModes::FilterUsesCutoff(eng.filterType);
         const bool warpOff = eng.warpMode == SynthModes::kWarpOff;
         AudioKnobRow row(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);

         row.DropdownKnob("wtFilter", SynthModes::FilterTypeList(), eng.filterType,
                          [&eng](int i) { PushUndoCheckpoint(); eng.filterType = i; },
                          "filter", &eng.cutoff, 20.0f, 18000.0f, "%.0f Hz", filterOff);
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
         AudioSlider("warp ratio", &eng.warpRatio, 0.25f, 16.0f, "%.2f", AudioHalfWidth());
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
                        tok::U32(tok::pal::c_96D6FFF5));
      ImGui::PopID();

      ImGui::PushID("pitch");
      DrawEnvelopePanel("pitch envelope  -  drag the handles", "##wtPitchEnv", &eng.pitchAttack,
                        &eng.pitchDecay, &eng.pitchSustain, &eng.pitchRelease, &eng.pitchAmount,
                        -48.0f, 48.0f, "%.1f st", tok::U32(tok::pal::c_FFBE78EB));
      ImGui::PopID();

      ImGui::PushID("filter");
      DrawEnvelopePanel("filter envelope  -  drag the handles", "##wtFiltEnv", &eng.filterAttack,
                        &eng.filterDecay, &eng.filterSustain, &eng.filterRelease, &eng.filterAmount,
                        -8.0f, 8.0f, "%.2f oct", tok::U32(tok::pal::c_96E6B4EB));
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
         snprintf(stat, sizeof(stat), "%s + %s  -  free run %.0f Hz",
                  Wavetable::TableName(n->engines[0].table),
                  n->engines[1].on ? Wavetable::TableName(n->engines[1].table) : "-", n->frequency);

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

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      BeginAudioSection("master");
      DrawWavetableScope(n, 48.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, 5.0f));
      {
         AudioKnobRow row(6, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         row.Knob("mix  a-b", &n->mix, 0.0f, 1.0f, "%.2f");
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f");
         // Free-running pitch is meaningless once notes drive pitch - greyed
         // rather than hidden so the row never reflows (§5).
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->frequency, 20.0f, 8000.0f, "%.0f Hz");
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
         // This knob is the manual/global bend - a Pitch Bend node wired
         // into the note chain upstream doesn't touch it at all, it rides
         // in on each note's own bendSemitones field instead (see
         // NoteEvent.h) and adds additively with whatever this knob is set
         // to. Drag this one directly for a bend with nothing patched.
         row.Knob("bend", &n->pitchBend, -2.0f, 2.0f, "%+.2f st");
         // fmMode picks phase modulation (cheap, always-stable sideband FM)
         // vs linear through-zero FM (DX7/Serum/Operator-style, can self-
         // modulate harder and run the carrier backward at high depth) - the
         // depth knob's meaning depends on it, so they read as one control.
         row.DropdownKnob("wtFmMode", { "pm", "fm" }, n->fmMode,
                          [n](int i) { PushUndoCheckpoint(); n->fmMode = i; },
                          "fm depth", &n->fmDepth, 0.0f, 2.0f, "%.2f");
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
      const float amp = (h * 0.5f) - 12.0f;

      // Horizontal center line
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      // Vertical grid ticks
      for (int i = 1; i < 4; i++)
      {
         const float x = origin.x + w * ((float)i / 4.0f);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }

      std::vector<ImVec2> pts;
      pts.reserve(256);

      const float cycles = 2.0f;

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
            const float x1 = x0 + cycleW * 0.25f;
            const float x2 = x0 + cycleW * 0.75f;
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
            const float y = sinf(2.0f * (float)M_PI * (cycles * t + phase));
            pts.push_back(ImVec2(origin.x + t * w, midY - y * amp));
         }
      }

      // Glow path
      dl->PathClear();
      for (const auto& p : pts)
         dl->PathLineTo(p);
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE62D) : tok::U32(tok::pal::c_64BEFF32), 0, 4.5f);

      // Core trace
      dl->PathClear();
      for (const auto& p : pts)
         dl->PathLineTo(p);
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1464E6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

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
         snprintf(stat, sizeof(stat), "%s  -  free run %.0f Hz",
                  waveName, n->frequency);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      WavetableEngine& eng = n->engine;

      // Header: waveform dropdown, fine tuning, octave, semitone.
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 5.0f;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float waveW = std::max(70.0f, w - octW - semiW - fineW - gap * 3.0f);
         static const std::vector<std::string> kOscWaveNames = SynthModes::WaveformTypeSubset(
            { SynthModes::kWaveSine, SynthModes::kWaveTriangle, SynthModes::kWaveSaw, SynthModes::kWaveSquare });

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("oscWave", kOscWaveNames, n->waveform,
                           [n](int i) { PushUndoCheckpoint(); n->waveform = i; }, waveW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + waveW + gap, y));
         AudioSlider("fine", &eng.fine, -50.0f, 50.0f, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("oscOct", OctaveNames(), eng.octave + 4,
                           [&eng](int i) { PushUndoCheckpoint(); eng.octave = i - 4; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("oscSemi", SemiNames(), eng.semi + 12,
                           [&eng](int i) { PushUndoCheckpoint(); eng.semi = i - 12; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Waveform display
      DrawOscillatorWaveform(n->waveform, n->engine.phase, 100.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Two knob rows of four
      {
         AudioKnobRow row(4);
         row.Knob("volume", &eng.volume, 0.0f, 1.0f, "%.2f");
         row.Knob("phase", &eng.phase, 0.0f, 1.0f, "%.3f");
         row.KnobInt("unison", &eng.unison, 1, OscillatorNode::kMaxUnison);
         row.Knob("detune", &eng.detune, 0.0f, 100.0f, "%.1f c");
         row.End();
      }
      {
         const bool filterOff = !SynthModes::FilterUsesCutoff(eng.filterType);
         AudioKnobRow row(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);

         row.DropdownKnob("oscFilter", SynthModes::FilterTypeList(), eng.filterType,
                          [&eng](int i) { PushUndoCheckpoint(); eng.filterType = i; },
                          "filter", &eng.cutoff, 20.0f, 18000.0f, "%.0f Hz", filterOff);
         row.Knob("reso", &eng.resonance, 0.0f, 1.0f, "%.3f");
         row.Knob("sync", &eng.warpRatio, 1.0f, 16.0f, "%.2f");
         row.Knob("pan", &eng.pan, -1.0f, 1.0f, "%.2f");
         row.End();
      }

      // Amplitude envelope (interactive ADSR).
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope  -  drag the handles", "##oscAmpEnv", &eng.ampAttack,
                        &eng.ampDecay, &eng.ampSustain, &eng.ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::U32(tok::pal::c_96D6FFF5));
      ImGui::PopID();

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      BeginAudioSection("master");
      DrawOscillatorScope(n, 48.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, 5.0f));
      {
         AudioKnobRow row(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->frequency, 20.0f, 8000.0f, "%.0f Hz");
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row.Knob("bend", &n->pitchBend, -2.0f, 2.0f, "%+.2f st");
         // fmMode picks phase modulation vs linear through-zero FM - see the
         // Wavetable master row's identical control for why they're paired.
         row.DropdownKnob("oscFmMode", { "pm", "fm" }, n->fmMode,
                          [n](int i) { PushUndoCheckpoint(); n->fmMode = i; },
                          "fm depth", &n->fmDepth, 0.0f, 2.0f, "%.2f");
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
         snprintf(stat, sizeof(stat), "%s  -  free run %.0f Hz", matName, n->frequency);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Header: Material preset dropdown, Fine tuning, Octave, Semitone
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 5.0f;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float matW = std::max(80.0f, w - octW - semiW - fineW - gap * 3.0f);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("metalMat", MetallicDsp::MaterialList(), n->material,
                           [n](int i) {
                              PushUndoCheckpoint();
                              n->SetMaterialPreset(i);
                           }, matW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + matW + gap, y));
         AudioSlider("fine", &n->fine, -50.0f, 50.0f, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("metalOct", OctaveNames(), n->octave + 4,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i - 4; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("metalSemi", SemiNames(), n->semi + 12,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i - 12; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Scope Visualizer with Strike Trigger Button
      {
         const float w = gAudioContentW;
         const float strikeBtnW = 70.0f;
         const float scopeW = w - strikeBtnW - 6.0f;
         const float scopeH = 58.0f;

         const ImVec2 pos = ImGui::GetCursorScreenPos();
         DrawMetallicScope(n, scopeH, scopeW);

         ImGui::SetCursorScreenPos(ImVec2(pos.x + scopeW + 6.0f, pos.y));
         if (ActionButton::Draw("Strike", ImVec2(strikeBtnW, scopeH)))
         {
            n->TriggerStrike();
         }
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Section 1: Physical Acoustic Properties (Transient, Decay, Stiffness, Width)
      BeginAudioSection("physical acoustics");
      {
         AudioKnobRow row(4);
         row.Knob("transient", &n->transient, 0.05f, 2.0f, "%.2f");
         row.Knob("decay", &n->decay, 0.05f, 10.0f, "%.2f s");
         row.Knob("stiffness", &n->stiffness, 0.0f, 2.0f, "%.2f");
         row.Knob("width", &n->width, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Section 2: Resonator Filter & Body Drive
      BeginAudioSection("filter & drive");
      {
         const bool filterOff = (n->filterType == MetallicDsp::kFilterOff);
         AudioKnobRow row(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);

         row.DropdownKnob("metalFilter", MetallicDsp::FilterModeDisplayList(),
                          MetallicDsp::FilterModeToDisplayIndex(n->filterType),
                          [n](int displayIdx) { PushUndoCheckpoint(); n->filterType = MetallicDsp::DisplayIndexToFilterMode(displayIdx); },
                          "filter", &n->filterCutoff, 20.0f, 18000.0f, "%.0f Hz", filterOff);
         row.Knob("reso", &n->filterResonance, 0.0f, 1.0f, "%.2f");
         row.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row.Knob("volume", &n->volume, 0.0f, 2.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Section 3: Master Pitch / Portamento
      BeginAudioSection("pitch & glide");
      {
         AudioKnobRow row(2);
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->frequency, 20.0f, 4000.0f, "%.0f Hz");
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("glide", &n->glide, 0.0f, 1.0f, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }


   void DrawWaveTerrainScope(WaveTerrainNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / 30.0)
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
         dl->AddLine(ImVec2(origin.x + w * i / 8.0f, origin.y), ImVec2(origin.x + w * i / 8.0f, br.y),
                     ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (n->scopeCacheCount > 1)
      {
         const int count = n->scopeCacheCount;
         for (int pass = 0; pass < 2; pass++)
         {
            dl->PathClear();
            for (int i = 0; i < count; i++)
            {
               const float t = (float)i / (float)(count - 1);
               const float y = midY - n->scopeCache[i] * (h * 0.45f);
               dl->PathLineTo(ImVec2(origin.x + t * w, y));
            }
            if (pass == 0)
               dl->PathStroke(isLight ? tok::U32(tok::pal::c_00A0C82D) : tok::U32(tok::pal::c_00DCFF2D), 0, 4.0f);
            else
               dl->PathStroke(isLight ? tok::U32(tok::pal::c_008CD2F0) : tok::U32(tok::pal::c_32F0FFF0), 0, 1.6f);
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
         snprintf(stat, sizeof(stat), "%s  -  free run %.0f Hz", orbitName.c_str(), n->frequency);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Header: Orbit dropdown, Channel dropdown, Octave, Semi, Fine
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 4.0f;
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
         ImGui::Dummy(ImVec2(0.0f, 4.0f));
         const float y2 = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(x0, y2));
         AudioSlider("fine", &n->fine, -50.0f, 50.0f, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y2));
         AudioBareDropdown("wtOct", OctaveNames(), n->octave + 4,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i - 4; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y2));
         AudioBareDropdown("wtSemi", SemiNames(), n->semi + 12,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i - 12; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y2));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Baked Wavetable Stack - the node bakes the incoming texture into an
      // 8-frame band-limited wavetable bank every rebuild (orbit-sampled ->
      // FFT -> mip pyramid, see WaveTerrainNode::RenderPreview); this shows
      // those baked cycles directly (stacked morph frames, à la a classic
      // wavetable editor) instead of the raw texture + orbit path, so the
      // display matches what the audio thread is actually playing back.
      {
         const float containerW = gAudioContentW;
         const float previewH = 140.0f;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 containerBr(origin.x + containerW, origin.y + previewH);

         dl->AddRectFilled(origin, containerBr, tok::U32(tok::pal::c_0E1014FF), 4.0f);
         dl->PushClipRect(origin, containerBr, true);

         const int frameCount = WaveTerrainNode::kDisplayFrameCount;
         const int sampleCount = WaveTerrainNode::kDisplayFrameSize;
         const float framePos = std::clamp(n->position, 0.0f, 1.0f) * (float)(frameCount - 1);

         // Stack offset: each frame further from the current morph position
         // recedes up-and-right, like looking down a row of wavetable cycles.
         const float stackDx = 10.0f;
         const float stackDy = 8.0f;
         const float traceW = containerW - stackDx * (frameCount - 1) - 12.0f;
         const float traceH = (previewH - stackDy * (frameCount - 1) - 16.0f) * 0.62f;
         constexpr int kPlotPoints = 160;

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

            const float baseX = origin.x + 6.0f + stackDx * (float)f;
            const float baseY = origin.y + previewH - 8.0f - stackDy * (float)f;

            ImVec2 pts[kPlotPoints];
            for (int i = 0; i < kPlotPoints; i++)
            {
               const float t = (float)i / (float)(kPlotPoints - 1);
               const float s = sampleFrame(f, t);
               pts[i] = ImVec2(baseX + t * traceW, baseY - traceH * 0.5f - s * traceH * 0.5f);
            }

            const ImU32 glow = IM_COL32(40, 200, 220, (int)(30 + 40 * prox));
            const bool isLight = IsThemeLight();
            const ImU32 coreLo = isLight ? IM_COL32(0, 130, 190, (int)(70 + 120 * prox))
                                          : IM_COL32(60, 210, 255, (int)(70 + 130 * prox));
            const ImU32 coreHi = isLight ? tok::U32(tok::pal::c_005A96FF) : tok::U32(tok::pal::c_BEFAFFFF);
            const ImU32 core = prox > 0.97f ? coreHi : coreLo;

            dl->AddPolyline(pts, kPlotPoints, glow, 0, prox > 0.97f ? 3.5f : 2.0f);
            dl->AddPolyline(pts, kPlotPoints, core, 0, prox > 0.97f ? 1.6f : 1.0f);
         }

         dl->PopClipRect();
         dl->AddRect(origin, containerBr, tok::U32(tok::pal::c_282C38B4), 4.0f, 0, 1.0f);

         char label[64];
         snprintf(label, sizeof(label), "wavetable  -  frame %.2f / %d", n->position * (frameCount - 1), frameCount - 1);
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), tok::U32(tok::pal::c_8C96A5C8), label);

         ImGui::Dummy(ImVec2(containerW, previewH));
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Section 1: Terrain Trajectory & Geometry
      BeginAudioSection("orbit & terrain geometry");
      {
         AudioKnobRow row1(4);
         row1.Knob("centerX", &n->centerX, 0.0f, 1.0f, "%.2f");
         row1.Knob("centerY", &n->centerY, 0.0f, 1.0f, "%.2f");
         row1.Knob("radiusX", &n->radiusX, 0.01f, 1.0f, "%.2f");
         row1.Knob("radiusY", &n->radiusY, 0.01f, 1.0f, "%.2f");
         row1.End();

         ImGui::Dummy(ImVec2(0.0f, 3.0f));
         AudioKnobRow row2(4);
         row2.Knob("ratioA", &n->ratioA, 1.0f, 16.0f, "%.0f");
         row2.Knob("ratioB", &n->ratioB, 1.0f, 16.0f, "%.0f");
         row2.Knob("rotation", &n->rotation, 0.0f, 360.0f, "%.0f deg");
         row2.Knob("speed", &n->scanSpeed, -4.0f, 4.0f, "%.2f");
         row2.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Section 2: Synth Voice & Timbre
      BeginAudioSection("voice & filter");
      {
         AudioKnobRow row1(4);
         row1.Knob("volume", &n->volume, 0.0f, 2.0f, "%.2f");
         row1.Knob("pan", &n->pan, -1.0f, 1.0f, "%.2f");
         row1.KnobInt("unison", &n->unison, 1, WaveTerrainNode::kMaxUnison);
         row1.Knob("detune", &n->detune, 0.0f, 100.0f, "%.1f c");
         row1.End();

         ImGui::Dummy(ImVec2(0.0f, 3.0f));
         const bool filterOff = (n->filterType == 0);
         AudioKnobRow row2(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         row2.DropdownKnob("wtFilter", WaveTerrainNode::FilterTypeNames(), n->filterType,
                          [n](int i) { PushUndoCheckpoint(); n->filterType = i; },
                          "cutoff", &n->cutoff, 20.0f, 20000.0f, "%.0f Hz", filterOff);
         row2.Knob("reso", &n->resonance, 0.0f, 1.0f, "%.2f");
         row2.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row2.Knob("morph", &n->position, 0.0f, 1.0f, "%.2f");
         row2.End();
      }
      EndAudioSection();

      // Interactive ADSR Envelopes
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope", "##wtAmpEnv", &n->ampAttack,
                        &n->ampDecay, &n->ampSustain, &n->ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::U32(tok::pal::c_32DCFFF0));
      ImGui::PopID();

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      BeginAudioSection("master & scope");
      DrawWaveTerrainScope(n, 48.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(2);
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->frequency, 20.0f, 4000.0f, "%.0f Hz");
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
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

      const ImU32 gridCol = isLight ? tok::U32(tok::pal::c_D7DCE6B4) : tok::U32(tok::pal::c_202634B4);
      const ImU32 axisCol = isLight ? tok::U32(tok::pal::c_8C96AAF0) : tok::U32(tok::pal::c_465573F0);
      const ImU32 textCol = isLight ? tok::U32(tok::pal::c_788291C8) : tok::U32(tok::pal::c_8291AAC8);

      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float midY = origin.y + h * 0.5f;
      float originX = origin.x;
      if (n->domainMode == EquationDsp::kDomainZeroToOne)
         originX = origin.x + 8.0f;
      else
         originX = origin.x + w * 0.5f;

      const float scaleY = (h * 0.5f - 8.0f);
      dl->AddLine(ImVec2(origin.x, midY - scaleY), ImVec2(br.x, midY - scaleY), gridCol, 1.0f);
      dl->AddLine(ImVec2(origin.x, midY - scaleY * 0.5f), ImVec2(br.x, midY - scaleY * 0.5f), gridCol, 1.0f);
      dl->AddLine(ImVec2(origin.x, midY + scaleY * 0.5f), ImVec2(br.x, midY + scaleY * 0.5f), gridCol, 1.0f);
      dl->AddLine(ImVec2(origin.x, midY + scaleY), ImVec2(br.x, midY + scaleY), gridCol, 1.0f);

      for (int i = 1; i < 8; i++)
      {
         const float gx = origin.x + w * (float)i / 8.0f;
         dl->AddLine(ImVec2(gx, origin.y), ImVec2(gx, br.y), gridCol, 1.0f);
      }

      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), axisCol, 1.5f);
      dl->AddLine(ImVec2(originX, origin.y), ImVec2(originX, br.y), axisCol, 1.5f);

      dl->AddText(ImVec2(origin.x + 4.0f, midY - scaleY - 1.0f), textCol, "+1");
      dl->AddText(ImVec2(origin.x + 4.0f, midY + scaleY - 13.0f), textCol, "-1");
      if (n->domainMode == EquationDsp::kDomainZeroToOne)
      {
         dl->AddText(ImVec2(origin.x + 24.0f, br.y - 13.0f), textCol, "0");
         dl->AddText(ImVec2(br.x - 14.0f, br.y - 13.0f), textCol, "1");
      }
      else if (n->domainMode == EquationDsp::kDomainNegPiToPi)
      {
         dl->AddText(ImVec2(origin.x + 24.0f, br.y - 13.0f), textCol, "-pi");
         dl->AddText(ImVec2(originX + 3.0f, br.y - 13.0f), textCol, "0");
         dl->AddText(ImVec2(br.x - 22.0f, br.y - 13.0f), textCol, "+pi");
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 24.0f, br.y - 13.0f), textCol, "-1");
         dl->AddText(ImVec2(originX + 3.0f, br.y - 13.0f), textCol, "0");
         dl->AddText(ImVec2(br.x - 14.0f, br.y - 13.0f), textCol, "+1");
      }

      if (n->scopeCacheCount > 1 && n->ActiveVoices() > 0)
      {
         const int sc = n->scopeCacheCount;
         dl->PathClear();
         for (int i = 0; i < sc; i++)
         {
            const float t = (float)i / (float)(sc - 1);
            const float sy = midY - n->scopeCache[i] * scaleY * 0.95f;
            dl->PathLineTo(ImVec2(origin.x + t * w, sy));
         }
         dl->PathStroke(isLight ? tok::U32(tok::pal::c_00A0DC32) : tok::U32(tok::pal::c_00F0FF3C), 0, 3.5f);
      }

      const auto& curve = n->PreviewCurve();
      if (!curve.empty())
      {
         const int nPts = (int)curve.size();
         dl->PathClear();
         for (int i = 0; i < nPts; i++)
         {
            const float t = (float)i / (float)(nPts - 1);
            const float cy = midY - std::clamp(curve[i], -1.5f, 1.5f) * scaleY;
            dl->PathLineTo(ImVec2(origin.x + t * w, cy));
         }
         dl->PathStroke(isLight ? tok::U32(tok::pal::c_008CF03C) : tok::U32(tok::pal::c_00C8FF4B), 0, 4.0f);

         dl->PathClear();
         for (int i = 0; i < nPts; i++)
         {
            const float t = (float)i / (float)(nPts - 1);
            const float cy = midY - std::clamp(curve[i], -1.5f, 1.5f) * scaleY;
            dl->PathLineTo(ImVec2(origin.x + t * w, cy));
         }
         dl->PathStroke(isLight ? tok::U32(tok::pal::c_0078DCF0) : tok::U32(tok::pal::c_64F0FFFA), 0, 1.8f);
      }

      if (!n->LastError().empty())
      {
         const std::string errText = "[!] " + n->LastError();
         const ImVec2 txtSz = ImGui::CalcTextSize(errText.c_str());
         const ImVec2 pillBr(br.x - 6.0f, origin.y + 6.0f + txtSz.y + 4.0f);
         const ImVec2 pillTl(pillBr.x - txtSz.x - 10.0f, origin.y + 6.0f);
         dl->AddRectFilled(pillTl, pillBr, tok::U32(tok::pal::c_B42828E6), 3.0f);
         dl->AddText(ImVec2(pillTl.x + 5.0f, pillTl.y + 2.0f), tok::U32(tok::pal::c_FFF0F0FF), errText.c_str());
      }
      else
      {
         const char* tag = "y = f(x)";
         const ImVec2 txtSz = ImGui::CalcTextSize(tag);
         const ImVec2 pillBr(br.x - 6.0f, origin.y + 6.0f + txtSz.y + 4.0f);
         const ImVec2 pillTl(pillBr.x - txtSz.x - 8.0f, origin.y + 6.0f);
         dl->AddRectFilled(pillTl, pillBr, isLight ? tok::U32(tok::pal::c_DCE6F0C8) : tok::U32(tok::pal::c_1C2432C8), 3.0f);
         dl->AddText(ImVec2(pillTl.x + 4.0f, pillTl.y + 2.0f), isLight ? tok::U32(tok::pal::c_0078C8F0) : tok::U32(tok::pal::c_50D2FFF0), tag);
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
         snprintf(stat, sizeof(stat), "Equation  -  free run %.0f Hz", n->frequency);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Header: Preset dropdown, Domain dropdown, Octave, Semi, Fine
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 4.0f;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         const float preW = (w - gap) * 0.58f;
         const float domW = w - gap - preW;

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("eqPreset", EquationNode::PresetNames(), n->presetIndex,
                           [n](int i) { PushUndoCheckpoint(); n->LoadPreset(i); }, preW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + preW + gap, y));
         AudioBareDropdown("eqDom", EquationNode::DomainNames(), n->domainMode,
                           [n](int i) { PushUndoCheckpoint(); n->domainMode = i; n->CompileEquation(); }, domW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
         ImGui::Dummy(ImVec2(0.0f, 4.0f));
         const float y2 = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(x0, y2));
         AudioSlider("fine", &n->fine, -50.0f, 50.0f, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y2));
         AudioBareDropdown("eqOct", OctaveNames(), n->octave + 4,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i - 4; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y2));
         AudioBareDropdown("eqSemi", SemiNames(), n->semi + 12,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i - 12; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y2));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Interactive Cartesian X-Y Visualizer
      DrawEquationVisualizer(n, 128.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

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
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Section 1: Function Knobs (A, B, C, D)
      BeginAudioSection("function parameters");
      {
         AudioKnobRow row(4, kKnobLarge);
         row.Knob("A", &n->knobA, 0.0f, 1.0f, "%.3f");
         row.Knob("B", &n->knobB, 0.0f, 1.0f, "%.3f");
         row.Knob("C", &n->knobC, 0.0f, 1.0f, "%.3f");
         row.Knob("D", &n->knobD, 0.0f, 1.0f, "%.3f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Section 2: Synth Voice & Filter
      BeginAudioSection("voice & filter");
      {
         AudioKnobRow row1(4);
         row1.Knob("volume", &n->volume, 0.0f, 2.0f, "%.2f");
         row1.Knob("pan", &n->pan, -1.0f, 1.0f, "%.2f");
         row1.KnobInt("unison", &n->unison, 1, EquationNode::kMaxUnison);
         row1.Knob("detune", &n->detune, 0.0f, 100.0f, "%.1f c");
         row1.End();

         ImGui::Dummy(ImVec2(0.0f, 3.0f));
         const bool filterOff = (n->filterType == 0);
         AudioKnobRow row2(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         row2.DropdownKnob("eqFilter", EquationNode::FilterTypeNames(), n->filterType,
                           [n](int i) { PushUndoCheckpoint(); n->filterType = i; },
                           "cutoff", &n->cutoff, 20.0f, 20000.0f, "%.0f Hz", filterOff);
         row2.Knob("reso", &n->resonance, 0.0f, 1.0f, "%.2f");
         row2.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row2.Knob("glide", &n->glide, 0.0f, 1.0f, "%.3f s", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row2.End();
      }
      EndAudioSection();

      // Section 3: Amp Envelope
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope", "##eqAmpEnv", &n->ampAttack,
                        &n->ampDecay, &n->ampSustain, &n->ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::U32(tok::pal::c_32DCFFF0));
      ImGui::PopID();

      EndAudioBody();
   }


   void DrawImageSpectralSynthScope(ImageSpectralSynthNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / 30.0)
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
         dl->AddLine(ImVec2(origin.x + w * i / 8.0f, origin.y), ImVec2(origin.x + w * i / 8.0f, br.y),
                     ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (n->scopeCacheCount > 1)
      {
         const int count = n->scopeCacheCount;
         for (int pass = 0; pass < 2; pass++)
         {
            dl->PathClear();
            for (int i = 0; i < count; i++)
            {
               const float t = (float)i / (float)(count - 1);
               const float y = midY - n->scopeCache[i] * (h * 0.45f);
               dl->PathLineTo(ImVec2(origin.x + t * w, y));
            }
            if (pass == 0)
               dl->PathStroke(isLight ? tok::U32(tok::pal::c_00AA962D) : tok::U32(tok::pal::c_00F0DC2D), 0, 4.0f);
            else
               dl->PathStroke(isLight ? tok::U32(tok::pal::c_00A08CF0) : tok::U32(tok::pal::c_32FFE6F0), 0, 1.6f);
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
         snprintf(stat, sizeof(stat), "%s  -  %d voice%s  (%.0f%%)", modeName.c_str(), voices, voices == 1 ? "" : "s", playhead * 100.0f);
      else
         snprintf(stat, sizeof(stat), "%s  -  drone  (%.0f%%)", modeName.c_str(), playhead * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Top Tuning Bar: Fine, Octave, Semi
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 4.0f;
         const float octW = kTuneOctW, semiW = kTuneSemiW, fineW = kTuneFineW;
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioSlider("fine", &n->fine, -50.0f, 50.0f, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("specOct", OctaveNames(), n->octave + 4,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i - 4; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("specSemi", SemiNames(), n->semi + 12,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i - 12; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Live Spectrogram Viewport with Neon Laser Scanhead Overlay
      {
         const float containerW = gAudioContentW;
         const float previewH = 140.0f;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 containerBr(origin.x + containerW, origin.y + previewH);

         // Background panel
         dl->AddRectFilled(origin, containerBr, tok::U32(tok::pal::c_0A0C10FF), 4.0f);
         dl->AddRect(origin, containerBr, tok::U32(tok::pal::c_242A36B4), 4.0f, 0, 1.0f);

         const float imgSize = previewH - 8.0f;
         const float imgX = origin.x + (containerW - imgSize) * 0.5f;
         const float imgY = origin.y + 4.0f;
         const ImVec2 imgTl(imgX, imgY);
         const ImVec2 imgBr(imgX + imgSize, imgY + imgSize);

         if (n->GetOutputTexture() != 0)
         {
            dl->AddImage((ImTextureID)(intptr_t)n->GetOutputTexture(), imgTl, imgBr, ImVec2(0, 1), ImVec2(1, 0));
            dl->AddRect(imgTl, imgBr, tok::U32(tok::pal::c_00F0DC8C), 2.0f, 0, 1.0f);
         }

         // Dynamic Glowing Neon Laser Scanhead Overlay (drawn cleanly on top of image every frame)
         const float playhead = n->Playhead();
         const float laserX = imgTl.x + std::clamp(playhead, 0.0f, 1.0f) * imgSize;

         // Outer cyan glow
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), tok::U32(tok::pal::c_00F0DC32), 6.0f);
         // Mid cyan glow
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), tok::U32(tok::pal::c_00FFEB82), 3.0f);
         // Sharp core laser line
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), tok::U32(tok::pal::c_EBFFFFFF), 1.5f);

         // Top & Bottom laser pointer diamond markers
         const float triSize = 4.0f;
         dl->AddTriangleFilled(ImVec2(laserX - triSize, imgTl.y), ImVec2(laserX + triSize, imgTl.y), ImVec2(laserX, imgTl.y + triSize * 1.5f), tok::U32(tok::pal::c_00FFF0FF));
         dl->AddTriangleFilled(ImVec2(laserX - triSize, imgBr.y), ImVec2(laserX + triSize, imgBr.y), ImVec2(laserX, imgBr.y - triSize * 1.5f), tok::U32(tok::pal::c_00FFF0FF));

         // Frequency ticks along left margin
         const char* freqLabels[5] = { "16k", "4k", "1k", "250", "40" };
         for (int i = 0; i < 5; i++)
         {
            const float ty = imgTl.y + (float)i * (imgSize / 4.0f) - 6.0f;
            dl->AddText(ImVec2(origin.x + 4.0f, ty), tok::U32(tok::pal::c_8CA0BEB4), freqLabels[i]);
         }

         // Invisible button for interactive mouse scrubbing across spectrogram
         ImGui::SetCursorScreenPos(imgTl);
         ImGui::InvisibleButton("##spectrogramScrub", ImVec2(imgSize, imgSize));
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
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Section 1: Transport & Scanning
      BeginAudioSection("transport & scanning");
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 4.0f;
         const bool isBpm = (n->scanMode == SpectralAdditiveDsp::kScanBpmSync);
         const bool isManual = (n->scanMode == SpectralAdditiveDsp::kScanManual);
         const float modeW = isBpm ? (w * 0.45f) : (isManual ? (w * 0.65f) : (w * 0.60f));
         const float rateW = isBpm ? (w * 0.35f) : 0.0f;
         const bool isOneShot = (n->scanMode == SpectralAdditiveDsp::kScanOneShot);
         const float trigW = isOneShot ? 50.0f : 0.0f;
         const float dirW = isManual ? 0.0f : std::max(44.0f, (w - modeW - (isBpm ? (rateW + gap) : 0.0f) - (isOneShot ? (trigW + gap) : 0.0f) - gap));

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

         ImGui::Dummy(ImVec2(0.0f, 3.0f));
         AudioKnobRow row(3);
         if (isManual)
         {
            row.Knob("pos", &n->position, 0.0f, 1.0f, "%.2f");
         }
         else
         {
            row.Knob("speed", &n->scanSpeed, 0.05f, 10.0f, "%.2fx");
         }
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row.Knob("width", &n->stereoWidth, 0.0f, 2.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Section 2: Frequency & Partials Spectrum
      BeginAudioSection("frequency & partials");
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float gap = 4.0f;

         // Row 1 Dropdowns: Partials + Frequency Scale
         float y = ImGui::GetCursorScreenPos().y;
         const float partW = w * 0.40f;
         const float scaleW = w * 0.60f - gap;

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("specPart", ImageSpectralSynthNode::PartialsCountNames(), n->partialsChoice,
                           [n](int i) { PushUndoCheckpoint(); n->partialsChoice = i; }, partW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + partW + gap, y));
         AudioBareDropdown("specScale", ImageSpectralSynthNode::FrequencyScaleNames(), n->freqScale,
                           [n](int i) { PushUndoCheckpoint(); n->freqScale = i; }, scaleW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));

         ImGui::Dummy(ImVec2(0.0f, 3.0f));

         // Row 2 Dropdown + Invert Button: Color Mode + Invert Toggle
         y = ImGui::GetCursorScreenPos().y;
         const float invW = 75.0f;
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
         ImGui::Dummy(ImVec2(0.0f, 4.0f));
         AudioKnobRow row1(4);
         row1.Knob("minHz", &n->minFreq, 20.0f, 2000.0f, "%.0f Hz");
         row1.Knob("maxHz", &n->maxFreq, 500.0f, 20000.0f, "%.0f Hz");
         row1.Knob("rootHz", &n->rootFreq, 20.0f, 2000.0f, "%.0f Hz");
         row1.KnobInt("unison", &n->unison, 1, ImageSpectralSynthNode::kMaxUnison);
         row1.End();

         // Row 4 Knobs: Conditioning & Gain
         ImGui::Dummy(ImVec2(0.0f, 3.0f));
         AudioKnobRow row2(4);
         row2.Knob("gain", &n->brightness, 0.0f, 3.0f, "%.2f");
         row2.Knob("thresh", &n->threshold, 0.0f, 0.5f, "%.2f");
         row2.Knob("contrast", &n->contrast, 0.2f, 4.0f, "%.2f");
         row2.Knob("detune", &n->detune, 0.0f, 50.0f, "%.1f c");
         row2.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Section 3: Voice & Filter
      BeginAudioSection("voice & filter");
      {
         AudioKnobRow row1(4);
         row1.Knob("volume", &n->volume, 0.0f, 2.0f, "%.2f");
         row1.Knob("pan", &n->pan, -1.0f, 1.0f, "%.2f");
         row1.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row1.Knob("fine", &n->fine, -50.0f, 50.0f, "%.1f c");
         row1.End();

         ImGui::Dummy(ImVec2(0.0f, 3.0f));
         const bool filterOff = (n->filterType == 0);
         AudioKnobRow row2(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         row2.DropdownKnob("specFilter", ImageSpectralSynthNode::FilterTypeNames(), n->filterType,
                          [n](int i) { PushUndoCheckpoint(); n->filterType = i; },
                          "cutoff", &n->cutoff, 20.0f, 20000.0f, "%.0f Hz", filterOff);
         row2.Knob("reso", &n->resonance, 0.0f, 1.0f, "%.2f");
         row2.End();
      }
      EndAudioSection();

      // Interactive ADSR Envelopes
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope", "##specAmpEnv", &n->ampAttack,
                        &n->ampDecay, &n->ampSustain, &n->ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        tok::U32(tok::pal::c_00F0DCF0));
      ImGui::PopID();

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      BeginAudioSection("master & scope");
      DrawImageSpectralSynthScope(n, 48.0f, gAudioContentW);
      EndAudioSection();

      EndAudioBody();
   }
}
