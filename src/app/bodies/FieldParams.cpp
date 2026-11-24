// Field node parameter bodies and scopes (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/Tip.h"
#include "app/AppShared.h"
#include "app/panels/FieldPatcherPanel.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/UiFmt.h"

namespace app
{
   // Drains WavetableNode's MeterRing into a small cache on the node itself
   // and redraws the trace from that cache at most 30x/sec (README.md §1's
   // scope-drawing cap), regardless of how often the app frame runs.
   void DrawWavetableScope(WavetableNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / tok::motion_hz_scope)
      {
         float buf[WavetableNode::kScopeCacheCapacity];
         const int count = n->ReadScope(buf, WavetableNode::kScopeCacheCapacity);
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
               ? (pass == 0 ? tok::Base(tok::col::accent, 50) : tok::Base(tok::col::accent))
               : (pass == 0 ? tok::Role(tok::col::accent_2, 46) : tok::Role(tok::col::accent_alt, 245));
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


   void DrawFieldSampleScope(FieldSampleNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / tok::motion_hz_scope)
      {
         float buf[FieldSampleNode::kScopeCacheCapacity];
         const int count = n->ReadScope(buf, FieldSampleNode::kScopeCacheCapacity);
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
               ? (pass == 0 ? tok::Base(tok::col::accent, 50) : tok::Base(tok::col::accent))
               : (pass == 0 ? tok::Role(tok::col::accent_2, 46) : tok::Role(tok::col::accent_alt, 245));
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


   // Spectral Field preview: 64 log bands, input as a faint fill, output as
   // the stroke. dB scale -90..0.
   void DrawSpectralFieldSpectrum(SpectralFieldNode* n, float h, float width)
   {
      constexpr int kB = SpectralFieldNode::kBands;
      float in[kB], out[kB];
      const bool valid = n->ReadSpectrum(in, out);

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);
      for (int i = 1; i < 4; i++)
         dl->AddLine(ImVec2(origin.x, origin.y + h * i / 4.0f), ImVec2(br.x, origin.y + h * i / 4.0f), ScopeGridCol(), 1.0f);

      if (valid)
      {
         auto yFor = [&](float mag) {
            const float db = 20.0f * std::log10(std::max(mag, 1e-6f));
            const float t = std::clamp((db + 90.0f) / 90.0f, 0.0f, 1.0f);
            return br.y - t * h;
         };
         const bool isLight = IsThemeLight();
         const ImU32 fillCol = isLight ? tok::Base(tok::col::accent, 50) : tok::Base(tok::col::accent_2, 46);
         const ImU32 strokeCol = isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, 245);
         const float bw = w / (float)kB;
         for (int i = 0; i < kB; i++)
            dl->AddRectFilled(ImVec2(origin.x + i * bw, yFor(in[i])), ImVec2(origin.x + (i + 1) * bw, br.y), fillCol);
         dl->PathClear();
         for (int i = 0; i < kB; i++)
            dl->PathLineTo(ImVec2(origin.x + (i + 0.5f) * bw, yFor(out[i])));
         dl->PathStroke(strokeCol, 0, 1.6f);
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "idle");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawFieldSynthScope(FieldSynthNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / tok::motion_hz_scope)
      {
         float buf[FieldSynthNode::kScopeCacheCapacity];
         const int count = n->ReadScope(buf, FieldSynthNode::kScopeCacheCapacity);
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
               ? (pass == 0 ? tok::Base(tok::col::accent, 50) : tok::Base(tok::col::accent))
               : (pass == 0 ? tok::Role(tok::col::accent_2, 46) : tok::Role(tok::col::accent_alt, 245));
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


   // Last two bars (8 beats) of what the script played: one bar per note,
   // pitch on the y axis (auto-ranged to what is on screen, never narrower
   // than an octave), brightness = velocity. Reads main-thread readouts only.
   void DrawFieldNotesRoll(FieldNotesNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      constexpr double kWindowBeats = 8.0;
      const double nowBeat = Transport::Instance().Beats();
      const double startBeat = nowBeat - kWindowBeats;

      for (int i = 1; i < 8; i++)
         dl->AddLine(ImVec2(origin.x + w * i / 8.0f, origin.y), ImVec2(origin.x + w * i / 8.0f, br.y),
                     ScopeGridCol(), 1.0f);

      float lo = 127.0f, hi = 0.0f;
      int visible = 0;
      for (int i = 0; i < n->RollCount(); i++)
      {
         const auto& r = n->RollAt(i);
         if (r.beat < startBeat || r.beat > nowBeat + 0.01) continue;
         lo = std::min(lo, r.note);
         hi = std::max(hi, r.note);
         visible++;
      }

      if (visible == 0)
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "idle");
      }
      else
      {
         if (hi - lo < 11.0f)
         {
            const float mid = 0.5f * (hi + lo);
            lo = mid - 5.5f;
            hi = mid + 5.5f;
         }
         const bool isLight = IsThemeLight();
         const float barH = std::max(2.0f, h / (hi - lo + 2.0f));
         const float barW = std::max(2.0f, w / (float)(kWindowBeats * 8.0));
         for (int i = 0; i < n->RollCount(); i++)
         {
            const auto& r = n->RollAt(i);
            if (r.beat < startBeat || r.beat > nowBeat + 0.01) continue;
            const float x = origin.x + (float)((r.beat - startBeat) / kWindowBeats) * w;
            const float y = br.y - barH - ((r.note - lo + 1.0f) / (hi - lo + 2.0f)) * (h - barH);
            const float v = std::max(0.15f, std::min(1.0f, r.vel));
            const ImU32 col = isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, 245);
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
            c.w *= v;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, y + barH), ImGui::ColorConvertFloat4ToU32(c), 1.0f);
         }
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawFieldElementParams(FieldElementNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);   // sliders as wide as the preview and buttons above them
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldElementNode>(n, "element", &FieldElementNode::PresetNames(),
                                                   [](FieldElementNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ActionButton::Draw("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldElementEditor = n;
            gFieldElementEditorOpen = true;
         }
         if (ActionButton::Draw(T("Boxes"), ImVec2(kPreviewSize, 0)) && gCurrentNodeIndex >= 0 && gCurrentNodeIndex < (int)gNodes.size())
            FieldPatcherOpenNode(gNodes[gCurrentNodeIndex].uid);

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (n->WasTruncated())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "Truncated to %s vertices", UiFmt::Count((long long)(n->ActualElementCount())).c);
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }

         if (n->State().CellCount() > 0)
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextDisabled("%s", n->CostReadout());
            PopTextWrapWidth();
         }
      }

      ModSliderInt("max elements", &n->maxElements, 100, 200000);
      if (!n->input)
         ModSliderInt("generate count", &n->generateCount, 1, 100000);

      {
         bool publish = n->publishScalarOutput;
         if (ModCheckbox("publish scalar output", &publish) && publish != n->publishScalarOutput)
         {
            if (!publish && FieldOutputPinHasLiveCable(gCurrentNodeIndex, 1))
            {
               n->pinRefusal = "refused: publish output is still wired";
            }
            else
            {
               n->publishScalarOutput = publish;
               n->pinRefusal.clear();
            }
         }
         if (!gParamRegisterOnly && !n->pinRefusal.empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->pinRefusal.c_str());
            PopTextWrapWidth();
         }
      }

      DrawFieldParamSliders(n);
   }


   void DrawFieldPrimitiveParams(FieldPrimitiveNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);   // sliders as wide as the preview and buttons above them
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldPrimitiveNode>(n, "primitive", &FieldPrimitiveNode::PresetNames(),
                                                     [](FieldPrimitiveNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ActionButton::Draw("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldPrimitiveEditor = n;
            gFieldPrimitiveEditorOpen = true;
         }
         if (ActionButton::Draw(T("Boxes"), ImVec2(kPreviewSize, 0)) && gCurrentNodeIndex >= 0 && gCurrentNodeIndex < (int)gNodes.size())
            FieldPatcherOpenNode(gNodes[gCurrentNodeIndex].uid);

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (n->WasTruncated())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "Truncated to %s vertices", UiFmt::Count((long long)(n->ActualElementCount())).c);
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }

         if (n->State().CellCount() > 0)
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextDisabled("%s", n->CostReadout());
            PopTextWrapWidth();
         }
      }

      {
         bool value = n->valueMode;
         if (ModCheckbox("value", &value) && value != n->valueMode)
         {
            n->valueMode = value;
            if (value)
               n->publishScalarOutput = true;
         }
      }
      static const std::vector<std::string> kTopologies = {
         "Points", "Plane (Grid)", "Sphere", "Cylinder", "Torus", "Disc"
      };
      if (!n->valueMode || gParamRegisterOnly)
      {
         DropdownButton("topology", kTopologies, n->topology, [n](int i) { n->topology = i; });
         ModSliderInt("count", &n->count, 1, 100000);
         ModSliderInt("max elements", &n->maxElements, 100, 200000);
      }

      if (!n->valueMode || gParamRegisterOnly)
      {
         bool publish = n->publishScalarOutput;
         if (ModCheckbox("publish scalar output", &publish) && publish != n->publishScalarOutput)
         {
            if (!publish && FieldOutputPinHasLiveCable(gCurrentNodeIndex, 1))
            {
               n->pinRefusal = "refused: publish output is still wired";
            }
            else
            {
               n->publishScalarOutput = publish;
               n->pinRefusal.clear();
            }
         }
         if (!gParamRegisterOnly && !n->pinRefusal.empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->pinRefusal.c_str());
            PopTextWrapWidth();
         }
      }

      DrawFieldParamSliders(n);
   }


   void DrawFieldSampleParams(FieldSampleNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);   // sliders as wide as the preview and buttons above them
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldSampleNode>(n, "sample", &FieldSampleNode::PresetNames(),
                                                  [](FieldSampleNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ActionButton::Draw("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldSampleEditor = n;
            gFieldSampleEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }

         uint64_t faults = n->FaultCount();
         if (faults > 0)
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%llu NaN/inf recovery event(s)", (unsigned long long)faults);
            PopTextWrapWidth();
         }
      }

      {
         bool expose = n->exposeRmsOutput;
         if (ModCheckbox("expose rms output", &expose) && expose != n->exposeRmsOutput)
         {
            if (!expose && FieldOutputPinHasLiveCable(gCurrentNodeIndex, 1))
            {
               n->pinRefusal = "refused: rms output is still wired";
            }
            else
            {
               n->exposeRmsOutput = expose;
               n->pinRefusal.clear();
            }
         }
         if (!gParamRegisterOnly && !n->pinRefusal.empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->pinRefusal.c_str());
            PopTextWrapWidth();
         }
      }

      DrawFieldParamSliders(n);
   }


   void DrawSpectralFieldParams(SpectralFieldNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<SpectralFieldNode>(n, "bin", &SpectralFieldNode::PresetNames(),
                                                    [](SpectralFieldNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ActionButton::Draw("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gSpectralFieldEditor = n;
            gSpectralFieldEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }
      }

      DropdownButton("size", SpectralFieldNode::SizeNames(), n->sizeIndex, [n](int i) { n->sizeIndex = i; });
      ModSlider("mix", &n->mix, 0.0f, 1.0f, "%.2f");

      DrawFieldParamSliders(n);
   }


   void DrawFieldSynthParams(FieldSynthNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);   // sliders as wide as the preview and buttons above them
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldSynthNode>(n, "synth", &FieldSynthNode::PresetNames(),
                                                 [](FieldSynthNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ActionButton::Draw("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldSynthEditor = n;
            gFieldSynthEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }

         uint64_t faults = n->FaultCount();
         if (faults > 0)
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%llu NaN/inf recovery event(s)", (unsigned long long)faults);
            PopTextWrapWidth();
         }
      }

      {
         int voices = n->maxVoices;
         if (ModSliderInt("voices", &voices, 1, 16))
         {
            n->maxVoices = voices;
         }
      }

      {
         bool expose = n->exposeRmsOutput;
         if (ModCheckbox("expose rms output", &expose) && expose != n->exposeRmsOutput)
         {
            if (!expose && FieldOutputPinHasLiveCable(gCurrentNodeIndex, 1))
            {
               n->pinRefusal = "refused: rms output is still wired";
            }
            else
            {
               n->exposeRmsOutput = expose;
               n->pinRefusal.clear();
            }
         }
         if (!gParamRegisterOnly && !n->pinRefusal.empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->pinRefusal.c_str());
            PopTextWrapWidth();
         }
      }

      DrawFieldParamSliders(n);
   }


   void DrawFieldNotesParams(FieldNotesNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldNotesNode>(n, "notes", &FieldNotesNode::PresetNames(),
                                                 [](FieldNotesNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ActionButton::Draw("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldNotesEditor = n;
            gFieldNotesEditorOpen = true;
         }
         if (ActionButton::Draw(T("Boxes"), ImVec2(kPreviewSize, 0)) && gCurrentNodeIndex >= 0 && gCurrentNodeIndex < (int)gNodes.size())
            FieldPatcherOpenNode(gNodes[gCurrentNodeIndex].uid);

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }

         if (n->DroppedTotal() > 0)
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%llu note(s) dropped", (unsigned long long)n->DroppedTotal());
            PopTextWrapWidth();
         }
      }

      {
         int root = n->root;
         if (ModSliderInt("root", &root, 0, 11))
            n->root = root;
         int scale = n->scale;
         if (ModSliderInt("scale", &scale, 0, MusicTime::kNumScaleTypes - 1))
            n->scale = scale;
      }

      {
         bool publish = n->publishScalarOutput;
         if (ModCheckbox("publish scalar output", &publish) && publish != n->publishScalarOutput)
         {
            if (!publish && FieldOutputPinHasLiveCable(gCurrentNodeIndex, 1))
               Tip::Show("%s", T("refused: publish output is still wired"));
            else
               n->publishScalarOutput = publish;
         }
      }

      DrawFieldParamSliders(n);
   }


   void DrawFieldGraphParams(FieldGraphNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);   // sliders as wide as the preview and buttons above them
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldGraphNode>(n, "graph", &FieldGraphNode::PresetNames(),
                                                 [](FieldGraphNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ActionButton::Draw("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldGraphEditor = n;
            gFieldGraphEditorOpen = true;
         }

         // Queues the actual mutation rather than calling it here: this runs
         // nested inside ed::Begin()/ed::End(), and Regenerate() spawns/
         // removes/reconnects real nodes - see gFieldGraphPendingRegenerate's
         // drain after ed::End() (trap T14).
         if (ActionButton::Draw("Regenerate", ImVec2(kPreviewSize, 0)))
            gFieldGraphPendingRegenerate = n;

         // Build step 16: only meaningful once there is something
         // encapsulated to unpack (doc §4.1's precondition). Same
         // queue-rather-than-mutate-here shape as "Regenerate" just above -
         // this also runs nested in ed::Begin()/ed::End(), and unpacking
         // reveals real gNodes entries and eventually spawns a GroupNode
         // (trap T14).
         const bool canUnpack = n->encapsulated && !n->MountedIndices().empty();
         if (!canUnpack)
            ImGui::BeginDisabled();
         if (ActionButton::Draw("Unpack to Canvas", ImVec2(kPreviewSize, 0)) && canUnpack)
            gFieldGraphPendingUnpack = n;
         if (!canUnpack)
            ImGui::EndDisabled();

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::role_modulation), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }
      }

      {
         bool addTrigger = n->addTriggerInput;
         if (ModCheckbox("trigger input", &addTrigger) && addTrigger != n->addTriggerInput)
         {
            if (!addTrigger && n->TriggerInputWired())
            {
               n->pinRefusal = "refused: trigger input is still wired";
            }
            else
            {
               n->addTriggerInput = addTrigger;
               n->pinRefusal.clear();
            }
         }
         if (!gParamRegisterOnly && !n->pinRefusal.empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->pinRefusal.c_str());
            PopTextWrapWidth();
         }
      }

      DrawFieldParamSliders(n);
   }


   void DrawFieldPixelParams(FieldPixelNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);   // sliders as wide as the preview and buttons above them
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldPixelNode>(n, "pixel", &FieldPixelNode::PresetNames(),
                                                 [](FieldPixelNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ActionButton::Draw("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldPixelEditor = n;
            gFieldPixelEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (n->BranchCount() > 0)
         {
            PushTextWrapWidth(kPreviewSize);
            char bBuf[64];
            snprintf(bBuf, sizeof(bBuf), "%d branch%s -> %d paths evaluated per pixel",
                     n->BranchCount(), n->BranchCount() == 1 ? "" : "es", 1 << n->BranchCount());
            ImGui::TextDisabled("%s", bBuf);
            PopTextWrapWidth();
         }

         if (n->StateCellCount() > 0)
         {
            PushTextWrapWidth(kPreviewSize);
            char sBuf[64];
            snprintf(sBuf, sizeof(sBuf), "%d state cell%s (RGBA16F ping-pong)",
                     n->StateCellCount(), n->StateCellCount() == 1 ? "" : "s");
            ImGui::TextDisabled("%s", sBuf);
            PopTextWrapWidth();
         }
      }

      DropdownButton("domain", FieldPixelNode::DomainNames(), n->domain, [n](int i)
      {
         if (i == n->domain) return;
         n->domain = i;
         n->Apply();
      });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModCheckbox("animate", &n->animate);

      {
         bool expose = n->exposeAuxTexture;
         if (ModCheckbox("expose state texture output", &expose) && expose != n->exposeAuxTexture)
         {
            if (!expose && FieldOutputPinHasLiveCable(gCurrentNodeIndex, 1))
            {
               n->pinRefusal = "refused: state output is still wired";
            }
            else
            {
               n->exposeAuxTexture = expose;
               n->pinRefusal.clear();
            }
         }
         if (!gParamRegisterOnly && !n->pinRefusal.empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->pinRefusal.c_str());
            PopTextWrapWidth();
         }
      }

      DrawFieldParamSliders(n);
   }
}
