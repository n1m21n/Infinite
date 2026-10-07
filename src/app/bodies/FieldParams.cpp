// Field node parameter bodies and scopes (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // Drains WavetableNode's MeterRing into a small cache on the node itself
   // and redraws the trace from that cache at most 30x/sec (README.md §1's
   // scope-drawing cap), regardless of how often the app frame runs.
   void DrawWavetableScope(WavetableNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / 30.0)
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
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
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
               ? (pass == 0 ? IM_COL32(30, 110, 230, 50) : IM_COL32(20, 100, 230, 255))
               : (pass == 0 ? IM_COL32(120, 200, 255, 46) : IM_COL32(150, 214, 255, 245));
            dl->PathStroke(strokeCol, 0, pass == 0 ? 4.5f : 1.6f);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "idle");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawFieldSampleScope(FieldSampleNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / 30.0)
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
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
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
               ? (pass == 0 ? IM_COL32(30, 110, 230, 50) : IM_COL32(20, 100, 230, 255))
               : (pass == 0 ? IM_COL32(120, 200, 255, 46) : IM_COL32(150, 214, 255, 245));
            dl->PathStroke(strokeCol, 0, pass == 0 ? 4.5f : 1.6f);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "idle");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawFieldSynthScope(FieldSynthNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / 30.0)
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
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
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
               ? (pass == 0 ? IM_COL32(30, 110, 230, 50) : IM_COL32(20, 100, 230, 255))
               : (pass == 0 ? IM_COL32(120, 200, 255, 46) : IM_COL32(150, 214, 255, 245));
            dl->PathStroke(strokeCol, 0, pass == 0 ? 4.5f : 1.6f);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "idle");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawFieldElementParams(FieldElementNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldElementNode>(n, "element", &FieldElementNode::PresetNames(),
                                                   [](FieldElementNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ImGui::Button("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldElementEditor = n;
            gFieldElementEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            ImGui::PopTextWrapPos();
         }

         if (n->WasTruncated())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Truncated to %d vertices", n->ActualElementCount());
            ImGui::PopTextWrapPos();
         }

         if (!n->Notice().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", n->Notice().c_str());
            ImGui::PopTextWrapPos();
         }

         if (n->State().CellCount() > 0)
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextDisabled("%s", n->CostReadout());
            ImGui::PopTextWrapPos();
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
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", n->pinRefusal.c_str());
            ImGui::PopTextWrapPos();
         }
      }

      DrawFieldParamSliders(n);
   }

   void DrawFieldPrimitiveParams(FieldPrimitiveNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldPrimitiveNode>(n, "primitive", &FieldPrimitiveNode::PresetNames(),
                                                     [](FieldPrimitiveNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ImGui::Button("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldPrimitiveEditor = n;
            gFieldPrimitiveEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            ImGui::PopTextWrapPos();
         }

         if (n->WasTruncated())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Truncated to %d vertices", n->ActualElementCount());
            ImGui::PopTextWrapPos();
         }

         if (!n->Notice().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", n->Notice().c_str());
            ImGui::PopTextWrapPos();
         }

         if (n->State().CellCount() > 0)
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextDisabled("%s", n->CostReadout());
            ImGui::PopTextWrapPos();
         }
      }

      static const std::vector<std::string> kTopologies = {
         "Points", "Plane (Grid)", "Sphere", "Cylinder", "Torus", "Disc"
      };
      DropdownButton("topology", kTopologies, n->topology, [n](int i) { n->topology = i; });

      ModSliderInt("count", &n->count, 1, 100000);
      ModSliderInt("max elements", &n->maxElements, 100, 200000);

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
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", n->pinRefusal.c_str());
            ImGui::PopTextWrapPos();
         }
      }

      DrawFieldParamSliders(n);
   }

   void DrawFieldSampleParams(FieldSampleNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldSampleNode>(n, "sample", &FieldSampleNode::PresetNames(),
                                                  [](FieldSampleNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ImGui::Button("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldSampleEditor = n;
            gFieldSampleEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            ImGui::PopTextWrapPos();
         }

         if (!n->Notice().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", n->Notice().c_str());
            ImGui::PopTextWrapPos();
         }

         uint64_t faults = n->FaultCount();
         if (faults > 0)
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%llu NaN/inf recovery event(s)", (unsigned long long)faults);
            ImGui::PopTextWrapPos();
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
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", n->pinRefusal.c_str());
            ImGui::PopTextWrapPos();
         }
      }

      DrawFieldParamSliders(n);
   }

   void DrawFieldSynthParams(FieldSynthNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldSynthNode>(n, "synth", &FieldSynthNode::PresetNames(),
                                                 [](FieldSynthNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ImGui::Button("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldSynthEditor = n;
            gFieldSynthEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            ImGui::PopTextWrapPos();
         }

         if (!n->Notice().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", n->Notice().c_str());
            ImGui::PopTextWrapPos();
         }

         uint64_t faults = n->FaultCount();
         if (faults > 0)
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%llu NaN/inf recovery event(s)", (unsigned long long)faults);
            ImGui::PopTextWrapPos();
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
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", n->pinRefusal.c_str());
            ImGui::PopTextWrapPos();
         }
      }

      DrawFieldParamSliders(n);
   }

   void DrawFieldGraphParams(FieldGraphNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldGraphNode>(n, "graph", &FieldGraphNode::PresetNames(),
                                                 [](FieldGraphNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ImGui::Button("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldGraphEditor = n;
            gFieldGraphEditorOpen = true;
         }

         // Queues the actual mutation rather than calling it here: this runs
         // nested inside ed::Begin()/ed::End(), and Regenerate() spawns/
         // removes/reconnects real nodes - see gFieldGraphPendingRegenerate's
         // drain after ed::End() (trap T14).
         if (ImGui::Button("Regenerate", ImVec2(kPreviewSize, 0)))
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
         if (ImGui::Button("Unpack to Canvas", ImVec2(kPreviewSize, 0)) && canUnpack)
            gFieldGraphPendingUnpack = n;
         if (!canUnpack)
            ImGui::EndDisabled();

         if (!n->LastError().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            ImGui::PopTextWrapPos();
         }

         if (!n->Notice().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", n->Notice().c_str());
            ImGui::PopTextWrapPos();
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
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", n->pinRefusal.c_str());
            ImGui::PopTextWrapPos();
         }
      }

      DrawFieldParamSliders(n);
   }

   void DrawFieldPixelParams(FieldPixelNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         DrawFieldDeviceControls<FieldPixelNode>(n, "pixel", &FieldPixelNode::PresetNames(),
                                                 [](FieldPixelNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

         if (ImGui::Button("Edit Field...", ImVec2(kPreviewSize, 0)))
         {
            gFieldPixelEditor = n;
            gFieldPixelEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            ImGui::PopTextWrapPos();
         }

         if (n->BranchCount() > 0)
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            char bBuf[64];
            snprintf(bBuf, sizeof(bBuf), "%d branch%s -> %d paths evaluated per pixel",
                     n->BranchCount(), n->BranchCount() == 1 ? "" : "es", 1 << n->BranchCount());
            ImGui::TextDisabled("%s", bBuf);
            ImGui::PopTextWrapPos();
         }

         if (n->StateCellCount() > 0)
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            char sBuf[64];
            snprintf(sBuf, sizeof(sBuf), "%d state cell%s (RGBA16F ping-pong)",
                     n->StateCellCount(), n->StateCellCount() == 1 ? "" : "s");
            ImGui::TextDisabled("%s", sBuf);
            ImGui::PopTextWrapPos();
         }
      }

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
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", n->pinRefusal.c_str());
            ImGui::PopTextWrapPos();
         }
      }

      DrawFieldParamSliders(n);
   }
}
