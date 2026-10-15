// Part of src/main.cpp, included there in order (one translation unit).
// macro and UI control node bodies

   // ---- macro family bodies ----------------------------------------------
   // The control sits in the node body (not behind the params toggle) so it
   // can be played and MIDI-learned directly; the caption is centred below.
   void MacroCaption(const ImVec2& origin, float cellW, float contentH, const std::string& caption)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 capCol = IsThemeLight() ? IM_COL32(50, 55, 70, 255) : IM_COL32(176, 182, 198, 255);
      const float textH = ImGui::GetTextLineHeight();
      const float capY = origin.y + contentH + 4.0f;
      if (!caption.empty())
      {
         const ImVec2 ts = ImGui::CalcTextSize(caption.c_str());
         dl->PushClipRect(ImVec2(origin.x, capY), ImVec2(origin.x + cellW, capY + textH), true);
         dl->AddText(ImVec2(origin.x + std::max(0.0f, (cellW - ts.x) * 0.5f), capY), capCol, caption.c_str());
         dl->PopClipRect();
      }
      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(cellW, contentH + 4.0f + textH));
   }

   void MacroNameField(std::string& label, float cellW)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "%s", label.c_str());
      ImGui::SetNextItemWidth(cellW);
      if (ImGui::InputTextWithHint("##macroname", "name", buf, sizeof(buf)))
      {
         label = buf;
         gPatchDirty = true;
      }
   }

   void DrawMacroSliderBody(MacroSliderNode* n)
   {
      const std::string caption = n->label.empty() ? std::string("slider") : n->label;
      ModKnob(caption.c_str(), &n->value, 0.0f, 1.0f, "%.2f", kMacroFaderH, kMacroCell, AudioWidgetStyle::VFader);
   }

   void DrawMacroBipolarKnobBody(MacroBipolarKnobNode* n)
   {
      const std::string caption = n->label.empty() ? std::string("bipolar") : n->label;
      // double-click types a value (0 recentres)
      ModKnob(caption.c_str(), &n->value, -1.0f, 1.0f, "%+.2f", kKnobStd, kMacroCell);
   }

   void DrawMacroToggleBody(MacroToggleNode* n)
   {
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      DiscreteParamRef ref = BeginDiscreteParam("state", n->state ? 1.0f : 0.0f, 0.0f, 1.0f);
      if (ref.valid && ref.modulated)
         n->state = *ref.value >= 0.5f;
      const float btnH = kMacroRowH + 4.0f, btnW = 72.0f;
      const ImVec2 bTL = ImGui::GetCursorScreenPos();
      const ImVec2 bBR(bTL.x + btnW, bTL.y + btnH);
      if (ImGui::InvisibleButton("##macrotogglebtn", ImVec2(btnW, btnH)) && !(ref.valid && ref.modulated))
      {
         PushUndoCheckpoint();
         n->state = !n->state;
         gPatchDirty = true;
      }
      const bool hovered = ImGui::IsItemHovered();
      EndDiscreteParam(ref, hovered);

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const float rad = btnH * 0.5f;
      dl->AddRectFilled(bTL, bBR, n->state ? (isLight ? IM_COL32(34, 197, 94, 255) : IM_COL32(22, 163, 74, 255))
                                           : (isLight ? IM_COL32(215, 222, 235, 255) : IM_COL32(36, 40, 52, 255)), rad);
      dl->AddRect(bTL, bBR, hovered ? IM_COL32(255, 255, 255, 120)
                                    : (isLight ? IM_COL32(180, 190, 205, 255) : IM_COL32(55, 62, 78, 255)), rad, 0, 1.2f);
      const float thumbX = n->state ? (bBR.x - rad) : (bTL.x + rad);
      dl->AddCircleFilled(ImVec2(thumbX, bTL.y + rad), rad - 4.0f, IM_COL32(255, 255, 255, 255));
      const char* text = n->state ? "ON" : "OFF";
      const ImVec2 ts = ImGui::CalcTextSize(text);
      const float tx = n->state ? bTL.x + (rad * 2.0f - ts.x) * 0.5f : bBR.x - rad * 2.0f + (rad * 2.0f - ts.x) * 0.5f;
      dl->AddText(ImVec2(tx, bTL.y + (btnH - ts.y) * 0.5f),
                  n->state ? IM_COL32(255, 255, 255, 255) : (isLight ? IM_COL32(90, 100, 120, 255) : IM_COL32(160, 170, 190, 255)),
                  text);
      MacroCaption(origin, kMacroCell, btnH, n->label.empty() ? std::string("toggle") : n->label);
   }

   void DrawMacroTriggerBody(MacroTriggerNode* n)
   {
      const float r = 22.0f, bezel = 3.0f, contentH = (r + bezel) * 2.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      // Turbo 0.48: momentary, so a Performance Mode bang presses the trigger
      // for as long as it is held instead of latching it on every second tap.
      DiscreteParamRef ref = BeginDiscreteParam("trigger", n->pressed ? 1.0f : 0.0f, 0.0f, 1.0f, 2);
      const ImVec2 padTL = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##macrotriggerbtn", ImVec2(contentH, contentH));
      const bool hovered = ImGui::IsItemHovered();
      n->pressed = ImGui::IsItemActive() || (ref.valid && ref.modulated && *ref.value >= 0.5f);
      EndDiscreteParam(ref, hovered);
      if (n->pressed)
         n->flash = 1.0f;
      else
         n->flash = std::max(0.0f, n->flash - ImGui::GetIO().DeltaTime * 4.0f);

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const ImVec2 center(padTL.x + contentH * 0.5f, padTL.y + contentH * 0.5f);
      dl->AddCircleFilled(center, r + bezel, isLight ? IM_COL32(210, 218, 230, 255) : IM_COL32(30, 34, 44, 255), 32);
      dl->AddCircle(center, r + bezel, isLight ? IM_COL32(175, 185, 200, 255) : IM_COL32(50, 56, 70, 255), 32, 1.2f);
      if (n->flash > 0.0f)
      {
         dl->AddCircleFilled(center, r, IM_COL32(245, 158, 11, (int)(n->flash * 255.0f)), 32);
         dl->AddCircle(center, r, IM_COL32(255, 230, 100, 255), 32, 2.0f);
      }
      else
      {
         dl->AddCircleFilled(center, r, isLight ? IM_COL32(235, 240, 250, 255) : IM_COL32(42, 48, 62, 255), 32);
         dl->AddCircle(center, r, isLight ? IM_COL32(190, 200, 215, 255) : IM_COL32(60, 68, 85, 255), 32, 1.0f);
      }
      if (hovered)
         dl->AddCircle(center, r + 2.0f, IM_COL32(255, 255, 255, 80), 32, 1.0f);
      dl->AddCircleFilled(center, 4.5f, n->flash > 0.0f ? IM_COL32(255, 255, 255, 255)
                                                         : (isLight ? IM_COL32(140, 150, 170, 255) : IM_COL32(80, 90, 110, 255)), 16);
      MacroCaption(origin, kMacroCell, contentH, n->label.empty() ? std::string("bang") : n->label);
   }

   void DrawMacroNumBoxBody(MacroNumBoxNode* n)
   {
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      if (n->maxVal <= n->minVal)
         n->maxVal = n->minVal + 1.0f;
      const float absMax = std::max(std::abs(n->minVal), std::abs(n->maxVal));
      const char* fmt = n->step >= 1.0f ? "%.0f" : (absMax >= 10.0f ? "%.1f" : "%.3f");
      ModSlider("##numbox", &n->value, n->minVal, n->maxVal, fmt, kMacroCell - 22.0f);
      if (n->step > 0.0f)
         n->value = std::clamp(n->minVal + std::round((n->value - n->minVal) / n->step) * n->step, n->minVal, n->maxVal);
      const float h = ImGui::GetCursorScreenPos().y - origin.y;
      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + h));
      MacroCaption(ImVec2(origin.x, origin.y), kMacroCell, h, n->label.empty() ? std::string("value") : n->label);
   }

   void DrawMacroRadioSelectorBody(MacroRadioSelectorNode* n)
   {
      const int count = std::clamp(n->count, 2, 8);
      n->selected = std::clamp(n->selected, 0, count - 1);
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      DiscreteParamRef ref = BeginDiscreteParam("selected", (float)n->selected, 0.0f, (float)(count - 1));
      if (ref.valid && ref.modulated)
         n->selected = std::clamp((int)lroundf(*ref.value), 0, count - 1);
      const float gap = 2.0f, btnH = kMacroRowH;
      const float btnW = (kMacroWideCell - (count - 1) * gap) / (float)count;
      const ImVec2 rowTL = ImGui::GetCursorScreenPos();
      const bool isLight = IsThemeLight();
      bool anyHovered = false;
      for (int b = 0; b < count; b++)
      {
         ImGui::SetCursorScreenPos(ImVec2(rowTL.x + b * (btnW + gap), rowTL.y));
         ImGui::PushID(b + 700);
         const bool sel = (b == n->selected);
         ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                                    : (isLight ? ImVec4(0.88f, 0.90f, 0.94f, 1.0f) : ImVec4(0.18f, 0.20f, 0.26f, 1.0f)));
         ImGui::PushStyleColor(ImGuiCol_Text, sel ? ImVec4(1, 1, 1, 1)
                                                  : (isLight ? ImVec4(0.3f, 0.35f, 0.45f, 1.0f) : ImVec4(0.7f, 0.75f, 0.85f, 1.0f)));
         if (ImGui::Button(std::to_string(b + 1).c_str(), ImVec2(btnW, btnH)) && !(ref.valid && ref.modulated))
         {
            PushUndoCheckpoint();
            n->selected = b;
            gPatchDirty = true;
         }
         anyHovered |= ImGui::IsItemHovered();
         ImGui::PopStyleColor(2);
         ImGui::PopID();
      }
      EndDiscreteParam(ref, anyHovered);
      MacroCaption(origin, kMacroWideCell + 18.0f, btnH, n->label.empty() ? std::string("selector") : n->label);
   }

   void DrawMacroStepGateBody(MacroStepGateNode* n)
   {
      const float gap = 2.0f, stepH = kMacroRowH;
      const float stepW = (kMacroWideCell - 7.0f * gap) / 8.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const bool playing = Transport::Instance().IsPlaying();
      const int playStep = n->CurrentStep();
      for (int st = 0; st < 8; st++)
      {
         const float sx = origin.x + st * (stepW + gap);
         const ImVec2 sTL(sx, origin.y), sBR(sx + stepW, origin.y + stepH);
         ImGui::SetCursorScreenPos(sTL);
         ImGui::PushID(st + 800);
         if (ImGui::InvisibleButton("##stepgatebtn", ImVec2(stepW, stepH)))
         {
            PushUndoCheckpoint();
            n->pattern ^= (uint8_t)(1u << st);
            gPatchDirty = true;
         }
         ImGui::PopID();
         const bool on = (n->pattern & (1u << st)) != 0;
         dl->AddRectFilled(sTL, sBR, on ? (isLight ? IM_COL32(34, 197, 94, 255) : IM_COL32(74, 222, 128, 255))
                                        : (isLight ? IM_COL32(210, 216, 228, 255) : IM_COL32(28, 31, 40, 255)), 2.5f);
         if (playing && st == playStep)
            dl->AddRect(sTL, sBR, IM_COL32(255, 230, 80, 255), 2.5f, 0, 2.0f);
         else
            dl->AddRect(sTL, sBR, isLight ? IM_COL32(180, 190, 205, 200) : IM_COL32(48, 52, 65, 200), 2.5f);
      }
      MacroCaption(origin, kMacroWideCell, stepH, n->label.empty() ? std::string("step gate") : n->label);
   }

   void DrawDriftParams(DriftNode* n)
   {
      ModSlider("speed", &n->speed, 0.1f, 4.0f, "%.2f");
      ModSlider("stray", &n->stray, 0.25f, 4.0f, "%.2f");
      ModSlider("momentum", &n->momentum, 0.0f, 4.0f, "%.2f s");
      ModSlider("home", &n->home, 0.0f, 1.0f, "%.2f");
      ModSlider("range lo", &n->rangeLo, 0.0f, 1.0f, "%.2f");
      ModSlider("range hi", &n->rangeHi, 0.0f, 1.0f, "%.2f");
      ModSlider("depth", &n->depth, 0.0f, 1.0f, "%.2f");
      static std::vector<std::string> kQuant;
      if (kQuant.empty())
      {
         kQuant.push_back("off");
         for (const std::string& d : MusicTime::RateDivisionList())
            kQuant.push_back(d);
      }
      DropdownButton("quantize", kQuant, n->quantizeRate, [n](int i) { n->quantizeRate = i; });
      ModSlider("smoothing", &n->smoothness, 0.0f, 0.95f, "%.2f");
      ModSliderInt("seed", &n->seed, 1, 999);
   }

   // Slideshow (upstream, originally contributed from this fork).
   void DrawCVRecorderParams(CVRecorderNode* n)
   {
      // One button: rec -> stop, and stopping starts the loop immediately.
      // Fixed width (the preview's), never derived from
      // GetContentRegionAvail: inside an auto-sizing node that feeds back on
      // itself and collapsed the old three buttons into slivers.
      const bool recording = n->IsRecording();
      char label[48];
      if (recording)
         snprintf(label, sizeof(label), "stop  (%.1f beats)###cvRec", n->LengthBeats());
      else
         snprintf(label, sizeof(label), "record###cvRec");
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
      // Turbo 0.49: mappable (CV / MIDI / Performance): high records, low stops.
      bool requestedRecording = recording;
      if (ModStateButton(label, recording, requestedRecording, ImVec2(kPreviewSize, 0)))
      {
         if (!requestedRecording)
            n->StopRecording();
         else
         {
            PushUndoCheckpoint();
            // Recording runs on the beat clock; a stopped transport would
            // capture a single sample.
            if (!Transport::Instance().IsPlaying())
               Transport::Instance().SetPlaying(true);
            n->StartRecording();
         }
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::BeginDisabled(n->input != nullptr);
      ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      ImGui::EndDisabled();

      ModSlider("speed", &n->speed, 0.05f, 4.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }

   void DrawSlideshowParams(SlideshowNode* n)
   {
      if (ImGui::Button("Choose folder...", ImVec2(kPreviewSize, 0)))
         StartNodeFileDialog(n, [start = n->FolderPath()]() { return Platform::OpenFolderDialog("Choose slideshow folder", start); },
                             [](auto* m, const std::string& path) { m->LoadFolder(path); });
      if (!n->FolderPath().empty())
      {
         if (ImGui::Button("Refresh", ImVec2(kPreviewSize, 0)))
            n->ReloadFromFolder();
      }
      // Turbo 0.50: mappable stepping (MIDI learn, Performance Mode, CV pins).
      // The param names are the label before "##"; the captions are short.
      {
         const float third = (kPreviewSize - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
         if (ModTriggerButton("restart sequence##slideshowRestart", ImVec2(third, 0), "|<##slideshowRestartBtn"))
            n->RequestRestart();
         ImGui::SameLine();
         if (ModTriggerButton("prev image##slideshowPrev", ImVec2(third, 0), "<##slideshowPrevBtn"))
            n->RequestPrev();
         ImGui::SameLine();
         if (ModTriggerButton("next image##slideshowNext", ImVec2(third, 0), ">##slideshowNextBtn"))
            n->RequestNext();
      }
      DropdownButton("transition", SlideshowNode::TransitionNames(), n->transition,
                     [n](int i) { n->transition = i; });
      ModSlider("hold", &n->holdDuration, 0.05f, 60.0f, "%.2f s");
      ModSlider("transition time", &n->transitionDuration, 0.0f, 15.0f, "%.2f s");
      DropdownButton("fit", SlideshowNode::FitModeNames(), n->fitMode,
                     [n](int i) { n->fitMode = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (!n->LastError().empty())
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
      if (n->ImageCount() > 0)
      {
         const std::string file = n->CurrentFileName();
         ImGui::TextDisabled("%d/%d  %s", n->CurrentImageNumber(), n->ImageCount(), file.c_str());
      }
      else
         ImGui::TextDisabled("Choose a folder containing images.");
      ImGui::PopTextWrapPos();
   }

   void DrawMacroSliderParams(MacroSliderNode* n) { MacroNameField(n->label, kMacroCell); }
   void DrawMacroBipolarKnobParams(MacroBipolarKnobNode* n) { MacroNameField(n->label, kMacroCell); }
   void DrawMacroToggleParams(MacroToggleNode* n) { MacroNameField(n->label, kMacroCell); }
   void DrawMacroTriggerParams(MacroTriggerNode* n) { MacroNameField(n->label, kMacroCell); }
   void DrawMacroNumBoxParams(MacroNumBoxNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      ImGui::SetNextItemWidth(kMacroCell);
      if (ImGui::DragFloat("min", &n->minVal, 0.1f)) gPatchDirty = true;
      ImGui::SetNextItemWidth(kMacroCell);
      if (ImGui::DragFloat("max", &n->maxVal, 0.1f)) gPatchDirty = true;
      ImGui::SetNextItemWidth(kMacroCell);
      if (ImGui::DragFloat("step", &n->step, 0.01f, 0.0f, 1000.0f, "%.3f")) gPatchDirty = true;
      ImGui::TextDisabled("step 0 = continuous");
   }
   void DrawMacroRadioSelectorParams(MacroRadioSelectorNode* n)
   {
      MacroNameField(n->label, kMacroWideCell);
      ModSliderInt("count", &n->count, 2, 8);
   }
   void DrawMacroStepGateParams(MacroStepGateNode* n)
   {
      MacroNameField(n->label, kMacroWideCell);
      ModSlider("rate (beats)", &n->rateBeats, 0.0625f, 4.0f);
   }

   void AppendUiHistory(std::map<int, std::vector<float>>& histories, int nodeIndex, float value)
   {
      std::vector<float>& history = histories[nodeIndex];
      history.push_back(std::clamp(value, 0.0f, 1.0f));
      if (history.size() > 96)
         history.erase(history.begin(), history.begin() + (history.size() - 96));
   }

   void DrawUiHistoryCurve(const std::vector<float>& history, const ImVec2& min,
                           const ImVec2& max, ImU32 color)
   {
      if (history.size() < 2)
         return;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->PushClipRect(min, max, true);
      const float width = std::max(1.0f, max.x - min.x);
      const float height = std::max(1.0f, max.y - min.y);
      for (size_t i = 1; i < history.size(); ++i)
      {
         const float x0 = min.x + width * (float)(i - 1) / (float)(history.size() - 1);
         const float x1 = min.x + width * (float)i / (float)(history.size() - 1);
         const float y0 = max.y - height * history[i - 1];
         const float y1 = max.y - height * history[i];
         dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 1.5f);
      }
      dl->PopClipRect();
   }

   void DrawUIButtonBody(UIButtonNode* n, int nodeIndex, int frameId, bool showPreview)
   {
      n->CookIfNeeded(frameId);
      const float previousValue = n->Value01();
      const bool wasLit = previousValue > 0.5f;
      if (wasLit)
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.55f, 0.86f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.64f, 0.98f, 1.0f));
      }
      ImGui::Button(wasLit ? "ON##uiButton" : "BUTTON##uiButton", ImVec2(kPreviewSize, 64.0f));
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      const bool pressedNow = ImGui::IsItemHovered() &&
                              ImGui::IsMouseClicked(ImGuiMouseButton_Left);
      // imgui-node-editor can release ImGui's active item ownership while the
      // physical mouse button is still down. UIButtonNode owns the capture
      // from this press until the global mouse-up, including outside releases.
      n->HandlePointerFrame(pressedNow, ImGui::IsMouseDown(ImGuiMouseButton_Left));
      const float value = n->Value01();
      AppendUiHistory(gUiHistoryX, nodeIndex, value);
      if (wasLit) ImGui::PopStyleColor(2);
      if (showPreview)
         DrawUiHistoryCurve(gUiHistoryX[nodeIndex], ImVec2(min.x + 5.0f, min.y + 34.0f),
                            ImVec2(max.x - 5.0f, max.y - 5.0f), IM_COL32(160, 220, 255, 190));
   }

   void DrawUIButtonOptions(UIButtonNode* n)
   {
      static const std::vector<std::string> modes = { "MOMENTARY", "PULSE", "TOGGLE" };
      DropdownButton("MODE", modes, n->mode, [n](int i) { n->SetMode(i); });
      if (n->mode == 1)
      {
         ImGui::SetNextItemWidth(kParamWidth);
         ImGui::SliderInt("PULSE FRAMES", &n->pulseFrames, 1, 120);
      }
      ImGui::TextDisabled("CV INPUT ACCEPTS MIDI, OSC OR ANY MODULATOR");
   }

   void DrawUISliderBody(UISliderNode* n, int nodeIndex, bool showPreview)
   {
      const float output = n->Value01();
      AppendUiHistory(gUiHistoryX, nodeIndex, output);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const ImVec2 size(kPreviewSize, n->isVertical ? 150.0f : 70.0f);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ScopeBgCol(), 4.0f);
      dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ScopeBorderCol(), 4.0f);
      if (n->input) ImGui::BeginDisabled();
      if (n->isVertical)
      {
         ImGui::SetCursorScreenPos(ImVec2(p.x + (size.x - 32.0f) * 0.5f, p.y + 8.0f));
         ImGui::VSliderFloat("##uiv", ImVec2(32.0f, size.y - 16.0f), &n->value, 0.0f, 1.0f, "%.3f");
      }
      else
      {
         ImGui::SetCursorScreenPos(ImVec2(p.x + 8.0f, p.y + 20.0f));
         ImGui::SetNextItemWidth(size.x - 16.0f);
         ImGui::SliderFloat("##uih", &n->value, 0.0f, 1.0f, "%.3f");
      }
      if (n->input) ImGui::EndDisabled();
      if (showPreview)
         DrawUiHistoryCurve(gUiHistoryX[nodeIndex], ImVec2(p.x + 5.0f, p.y + 5.0f),
                            ImVec2(p.x + size.x - 5.0f, p.y + size.y - 5.0f),
                            IM_COL32(100, 194, 255, 150));
      char valueText[32];
      snprintf(valueText, sizeof(valueText), "%.3f", output);
      dl->AddText(ImVec2(p.x + 8.0f, p.y + size.y - ImGui::GetTextLineHeight() - 5.0f),
                  IM_COL32(205, 215, 235, 220), valueText);
      ImGui::SetCursorScreenPos(p);
      ImGui::Dummy(size);
   }

   void DrawUISliderOptions(UISliderNode* n)
   {
      ImGui::TextDisabled("orientation: %s", n->isVertical ? "vertical" : "horizontal");
      ImGui::TextDisabled("a connected CV overrides the manual value");
   }

   void DrawUIXYBody(UIXYNode* n, int nodeIndex, bool showPreview)
   {
      const float size = kPreviewSize;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##uixy", ImVec2(size, size));
      if (ImGui::IsItemActive())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         if (!n->inputX) n->x = std::clamp((m.x - p.x) / size, 0.0f, 1.0f);
         if (!n->inputY) n->y = 1.0f - std::clamp((m.y - p.y) / size, 0.0f, 1.0f);
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), ScopeBgCol(), 4.0f);
      for (int i = 1; i < 4; ++i)
      {
         const float f = i / 4.0f;
         dl->AddLine(ImVec2(p.x + f * size, p.y), ImVec2(p.x + f * size, p.y + size), ScopeGridCol());
         dl->AddLine(ImVec2(p.x, p.y + f * size), ImVec2(p.x + size, p.y + f * size), ScopeGridCol());
      }
      const float x = n->Value01();
      const float y = n->inputY ? n->inputY->Value01() : n->y;
      AppendUiHistory(gUiHistoryX, nodeIndex, x);
      AppendUiHistory(gUiHistoryY, nodeIndex, y);
      if (showPreview)
      {
         DrawUiHistoryCurve(gUiHistoryX[nodeIndex], ImVec2(p.x + 3.0f, p.y + 3.0f),
                            ImVec2(p.x + size - 3.0f, p.y + size - 3.0f),
                            IM_COL32(75, 190, 255, 220));
         DrawUiHistoryCurve(gUiHistoryY[nodeIndex], ImVec2(p.x + 3.0f, p.y + 3.0f),
                            ImVec2(p.x + size - 3.0f, p.y + size - 3.0f),
                            IM_COL32(255, 95, 205, 220));
         dl->AddText(ImVec2(p.x + 7.0f, p.y + 5.0f), IM_COL32(75, 190, 255, 240), "X");
         dl->AddText(ImVec2(p.x + 24.0f, p.y + 5.0f), IM_COL32(255, 95, 205, 240), "Y");
      }
      dl->AddCircleFilled(ImVec2(p.x + x * size, p.y + (1.0f - y) * size), 8.0f,
                          IM_COL32(235, 235, 250, 255));
      dl->AddCircle(ImVec2(p.x + x * size, p.y + (1.0f - y) * size), 9.0f,
                    IM_COL32(110, 185, 255, 255), 20, 2.0f);
      dl->AddRect(p, ImVec2(p.x + size, p.y + size), ScopeBorderCol(), 4.0f);
      ImGui::TextDisabled("x %.3f   y %.3f", x, y);
   }

   void DrawUIXYOptions(UIXYNode*)
   {
      ImGui::TextDisabled("X and Y accept independent CV inputs");
      ImGui::TextDisabled("blue curve: X   magenta curve: Y");
   }

   void DrawMidiCCParams(MidiCCNode* n)
   {
      if (n->IsLearning())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.1f, 1.0f));
         if (ImGui::Button("Listening... move a control", ImVec2(kPreviewSize, 0)))
            n->CancelLearn();
         ImGui::PopStyleColor();
      }
      else if (ImGui::Button(n->IsBound() ? "Re-learn" : "MIDI Learn", ImVec2(kPreviewSize, 0)))
      {
         n->StartLearn();
      }
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      ImGui::Text("bound: %s", n->BindingLabel().c_str());
      // Turbo 0.50: the learned channel can be changed without re-learning
      // (same controller layout on another channel). Bindings are always one
      // exact channel, so no omni here.
      if (n->IsBound())
         MidiChannelDropdown("channel##midiCCCh", &n->channel, false);
      ImGui::ProgressBar(n->Value01(), ImVec2(kPreviewSize * 0.6f, 0), "");

      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
      ModCheckbox("invert", &n->invert);
   }

   void DrawMidiTriggerParams(MidiTriggerNode* n)
   {
      DropdownButton("device type", MidiTriggerNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });

      if (n->IsLearning())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.1f, 1.0f));
         if (ImGui::Button("Listening... hit a pad", ImVec2(kPreviewSize, 0)))
            n->CancelLearn();
         ImGui::PopStyleColor();
      }
      else
      {
         const bool lit = n->Value01() > 0.01f;
         if (lit)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.85f, 0.35f, 1.0f));
         if (ImGui::Button(n->IsBound() ? "Re-learn" : "MIDI Learn", ImVec2(kPreviewSize, 0)))
            n->StartLearn();
         if (lit)
            ImGui::PopStyleColor();
      }
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      ImGui::Text("bound: %s", n->BindingLabel().c_str());
      if (n->IsBound()) // Turbo 0.50: see DrawMidiCCParams
         MidiChannelDropdown("channel##midiTrigCh", &n->channel, false);
      ImGui::ProgressBar(n->Value01(), ImVec2(kPreviewSize * 0.6f, 0), "");

      if (n->mode == MidiTriggerNode::kKeyboard)
      {
         ImGui::TextDisabled("value = note number / 127, held until the next key");
      }
      else
      {
         ModSlider("hold", &n->hold, 0.02f, 2.0f);
         ModCheckbox("velocity sensitive", &n->velocitySensitive);
      }
   }

   void DrawMacroXYParams(MacroXYNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##macroxy", ImVec2(size, size));
      if (ImGui::IsItemActive())
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y), ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f), ScopeGridCol());
      }
      const std::vector<MacroXYNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, isLight ? IM_COL32(30, 120, 230, 220) : IM_COL32(120, 200, 255, 170), 1.6f);
      }
      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? IM_COL32(255, 90, 90, 255)
                     : n->IsPlayingPath()   ? (isLight ? IM_COL32(30, 180, 80, 255) : IM_COL32(120, 235, 150, 255))
                                            : (isLight ? IM_COL32(230, 140, 20, 255) : IM_COL32(255, 190, 90, 255));
      dl->AddCircleFilled(orb, 9.0f, orbColor);
      dl->AddCircle(orb, 9.0f, isLight ? IM_COL32(240, 240, 240, 255) : IM_COL32(20, 20, 28, 255), 0, 2.0f);
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      ImGui::TextDisabled("x %.3f   y %.3f", n->padX, n->padY);

      const bool recordingPath = n->IsRecordingPath();
      bool requestedRecordingPath = recordingPath;
      if (recordingPath)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
      if (ModStateButton(recordingPath ? "Stop rec##macroRecordPath" : "Rec path##macroRecordPath",
                         recordingPath, requestedRecordingPath, ImVec2(kPreviewSize * 0.48f, 0)))
      {
         if (requestedRecordingPath)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recordingPath)
         ImGui::PopStyleColor();
      ImGui::SameLine();
      const bool playingPath = n->IsPlayingPath();
      bool requestedPlayingPath = playingPath;
      if (ModStateButton(playingPath ? "Stop##macroPlayPath" : "Play path##macroPlayPath",
                         playingPath, requestedPlayingPath, ImVec2(kPreviewSize * 0.48f, 0)))
      {
         if (requestedPlayingPath)
            n->PlayPath();
         else
            n->StopPath();
      }
      ModCheckbox("loop", &n->loopPath);
      ImGui::SameLine();
      if (ModTriggerButton("clear##macroClearPath", ImVec2(50.0f, 0)))
         n->ClearPath();
      ModSlider("speed", &n->speed, 0.05f, 4.0f);
   }

   // Interactive curve editor shared by CurvesNode (image tone curve) and
   // ModCurveNode (modulator transfer curve). Drag points, click empty space
   // to add one, right-click a point to remove it. When resetOnEmptyRightClick
   // is set, right-clicking empty space resets the whole curve to a straight
   // line instead of doing nothing - ModCurveNode wants this (there is no
   // per-channel "Reset channel" button for it), CurvesNode does not.
   // liveX >= 0 draws a dot on the curve at that input position; crosshair
   // draws a 0.5/0.5 gridline, which is where "no modulation" lives under a
   // bipolar binding.
   void DrawCurveEditor(CurveShape& shape, ImU32 lineCol, bool resetOnEmptyRightClick = false,
                        bool crosshair = false, float liveX = -1.0f)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::PushID(&shape);
      ImGui::InvisibleButton("##curve", ImVec2(size, size));
      ImGui::PopID();
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      auto toScreen = [&](float x, float y) {
         return ImVec2(origin.x + x * size, origin.y + (1.0f - y) * size);
      };
      auto toCurve = [&](ImVec2 p) {
         return ImVec2(std::min(1.0f, std::max(0.0f, (p.x - origin.x) / size)),
                       std::min(1.0f, std::max(0.0f, 1.0f - (p.y - origin.y) / size)));
      };

      std::vector<CurveShape::Point>& pts = shape.points;

      static int sDragIndex = -1;
      static CurveShape* sDragShape = nullptr;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      int nearest = -1;
      float nearestDist = 12.0f;
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         float d = std::sqrt((sp.x - mouse.x) * (sp.x - mouse.x) + (sp.y - mouse.y) * (sp.y - mouse.y));
         if (d < nearestDist)
         {
            nearestDist = d;
            nearest = i;
         }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         if (nearest >= 0)
         {
            sDragIndex = nearest;
            sDragShape = &shape;
         }
         else
         {
            ImVec2 c = toCurve(mouse);
            sDragIndex = shape.AddPoint(c.x, c.y);
            sDragShape = &shape;
         }
      }
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
         if (nearest >= 0)
            shape.RemovePoint(nearest);
         else if (resetOnEmptyRightClick)
            shape.Reset();
      }

      if (active && sDragShape == &shape && sDragIndex >= 0)
      {
         ImVec2 c = toCurve(mouse);
         shape.MovePoint(sDragIndex, c.x, c.y);
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragIndex = -1;
         sDragShape = nullptr;
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y), ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f), ScopeGridCol());
      }
      dl->AddLine(origin, ImVec2(br.x, br.y), ScopeMidLineCol()); // identity reference
      if (crosshair)
      {
         ImVec2 mid = toScreen(0.5f, 0.5f);
         dl->AddLine(ImVec2(mid.x, origin.y), ImVec2(mid.x, br.y), isLight ? IM_COL32(100, 110, 130, 200) : IM_COL32(120, 124, 140, 200));
         dl->AddLine(ImVec2(origin.x, mid.y), ImVec2(br.x, mid.y), isLight ? IM_COL32(100, 110, 130, 200) : IM_COL32(120, 124, 140, 200));
      }

      const int kSegments = 64;
      for (int i = 1; i <= kSegments; i++)
      {
         float x0 = (float)(i - 1) / kSegments;
         float x1 = (float)i / kSegments;
         dl->AddLine(toScreen(x0, shape.Evaluate(x0)),
                     toScreen(x1, shape.Evaluate(x1)), lineCol, 1.8f);
      }
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         dl->AddCircleFilled(sp, i == nearest ? 6.0f : 4.5f, lineCol);
         dl->AddCircle(sp, i == nearest ? 6.0f : 4.5f, isLight ? IM_COL32(240, 242, 248, 255) : IM_COL32(18, 18, 26, 255), 0, 1.5f);
      }
      if (liveX >= 0.0f)
      {
         float clampedX = std::min(1.0f, std::max(0.0f, liveX));
         ImVec2 dot = toScreen(clampedX, shape.Evaluate(clampedX));
         dl->AddCircleFilled(dot, 5.0f, isLight ? IM_COL32(220, 130, 20, 255) : IM_COL32(255, 200, 60, 255));
         dl->AddCircle(dot, 5.0f, isLight ? IM_COL32(240, 242, 248, 255) : IM_COL32(18, 18, 26, 255), 0, 1.5f);
      }
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
   }

   void DrawCurvesParams(CurvesNode* n)
   {
      DropdownButton("channel", CurvesNode::ChannelNames(), n->activeChannel,
                     [n](int i) { n->activeChannel = i; });
      const bool isLight = IsThemeLight();
      ImU32 lineCol = isLight ? IM_COL32(30, 40, 60, 255) : IM_COL32(230, 235, 250, 255);
      if (n->activeChannel == CurvesNode::kRed)   lineCol = isLight ? IM_COL32(220, 40, 40, 255) : IM_COL32(255, 110, 110, 255);
      if (n->activeChannel == CurvesNode::kGreen) lineCol = isLight ? IM_COL32(25, 160, 60, 255) : IM_COL32(120, 230, 130, 255);
      if (n->activeChannel == CurvesNode::kBlue)  lineCol = isLight ? IM_COL32(30, 100, 230, 255) : IM_COL32(120, 170, 255, 255);
      DrawCurveEditor(n->Shape(n->activeChannel), lineCol);
      if (ModTriggerButton("Reset channel##curvesReset", ImVec2(kPreviewSize, 0)))
         n->ResetChannel(n->activeChannel);
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
   }

   void DrawModCurveParams(ModCurveNode* n)
   {
      const bool isLight = IsThemeLight();
      const float in = n->input ? n->input->Value01() : n->constantIn;
      DrawCurveEditor(n->curve, isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(230, 235, 250, 255), /*resetOnEmptyRightClick=*/true,
                     /*crosshair=*/true, /*liveX=*/in);
      if (n->input == nullptr)
         ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("in: patched");
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
   }

   void DrawRemoveBgParams(RemoveBgNode* n)
   {
      DropdownButton("detect", RemoveBgNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      DropdownButton("backend", RemoveBgNode::BackendNames(), n->backend,
                     [n](int i) { n->backend = i; n->RequestMask(); });
      DropdownButton("output", RemoveBgNode::OutputModeNames(), n->outputMode,
                     [n](int i) { n->outputMode = i; });

      if (ModTriggerButton("Remove Background##removeBgCapture", ImVec2(kPreviewSize, 0)))
         n->RequestMask();

      const std::string status = n->Status().size() > 38 ? n->Status().substr(0, 35) + "..." : n->Status();
      ImGui::BeginChild("##maskstatus", ImVec2(kPreviewSize, ImGui::GetTextLineHeight() + 3.0f), false,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::TextDisabled("%s", status.c_str());
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", n->Status().c_str());
      ImGui::EndChild();

      ModSlider("feather", &n->feather, 0.0f, 4.0f);
      ModSlider("threshold", &n->threshold, 0.0f, 1.0f);
      ModSlider("edge contrast", &n->contrast, 0.5f, 8.0f);
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);

      ImGui::TextDisabled("live at composition FPS");
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("frame cache", &n->frameCache, 1, 16);
   }

   void DrawFeedbackParams(FeedbackNode* n)
   {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("One-frame loop memory. Use it inside a feedback cycle; use Trails for a ready-made visual echo.");
      ImGui::PopTextWrapPos();
      ImGui::TextDisabled("delay: 1 frame");
      if (ModTriggerButton("Clear loop memory##feedbackClear", ImVec2(kPreviewSize, 0)))
         n->Clear();
      DropdownButton("pixel format", FeedbackNode::FormatNames(), n->format, [n](int i) {
         if (n->format == i)
            return;
         if (!gApplyingDiscreteCv)
            PushUndoCheckpoint();
         n->format = i;
      });
   }

   void DrawTrailsParams(TrailsNode* n)
   {
      static const std::vector<std::string> kBlends = { "Max", "Add", "Screen" };
      DropdownButton("blend", kBlends, n->blendMode, [n](int i) { n->blendMode = i; });
      ModSlider("decay", &n->decay, 0.5f, 0.999f);
      ModSlider("zoom", &n->zoom, 0.9f, 1.1f);
      ModSlider("rotate", &n->rotate, -5.7296f, 5.7296f, "%.1f\xC2\xB0");
      ModSlider("drift x", &n->driftX, -0.02f, 0.02f);
      ModSlider("drift y", &n->driftY, -0.02f, 0.02f);
      ModSlider("hue shift", &n->hueShift, -0.05f, 0.05f);
      if (ModTriggerButton("Clear##trailsClear", ImVec2(kPreviewSize, 0)))
         n->Clear();
   }

   // Turbo 0.51: codec-style datamosh (block motion estimation + held blocks).
   void DrawDatamoshParams(DatamoshNode* n)
   {
      ModSlider("mosh", &n->mosh, 0.0f, 1.0f);
      ModSlider("gain", &n->gain, 0.0f, 4.0f);
      ModSlider("bloom", &n->bloom, 0.0f, 0.98f);
      ModSlider("threshold", &n->threshold, 0.0f, 1.0f);
      DropdownButton("block", DatamoshNode::BlockNames(), n->block, [n](int i) { n->block = i; });
      ModSlider("block var", &n->blockVar, 0.0f, 1.0f);
      ModSlider("leak", &n->leak, 0.0f, 1.0f);
      DropdownButton("refresh", DatamoshNode::RefreshNames(), n->refresh, [n](int i) { n->refresh = i; });
      ModSlider("refresh chance", &n->refreshChance, 0.0f, 1.0f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f");
      if (ModTriggerButton("Refresh##datamoshRefresh", ImVec2(kPreviewSize, 0)))
         n->Refresh();
   }

   void DrawReactionDiffusionParams(ReactionDiffusionNode* n)
   {
      DropdownButton("preset", ReactionDiffusionNode::PresetNames(), n->preset,
                     [n](int i) { n->ApplyPreset(i); });
      ModSlider("feed", &n->feed, 0.01f, 0.09f, "%.4f");
      ModSlider("kill", &n->kill, 0.03f, 0.08f, "%.4f");
      ModSlider("diffuse A", &n->diffuseA, 0.2f, 1.5f);
      ModSlider("diffuse B", &n->diffuseB, 0.1f, 1.0f);
      ModSlider("steps / frame", &n->stepsPerFrame, 1.0f, 32.0f, "%.0f");
      ImGui::TextDisabled("with an input connected:");
      ModSlider("source influence", &n->sourceInfluence, 0.0f, 1.0f);
      ModSlider("width", &n->width, 64.0f, 2048.0f, "%.0f");
      ModSlider("height", &n->height, 64.0f, 2048.0f, "%.0f");
      ColorSwatch("low", n->lowColor, n);
      ColorSwatch("high", n->highColor, n);
      if (ModTriggerButton("Reseed##reactionReseed", ImVec2(kPreviewSize, 0)))
         n->Reseed();
   }

   // --- Palette from Image ---------------------------------------------
   // Preview is the palette itself, not the strip texture: the strip is a
   // by-product, while the swatches are what the rest of the graph binds to,
   // and they need to be readable and countable at a glance.
   void DrawPalettePreview(PaletteNode* n)
   {
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 118.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                        IM_COL32(18, 18, 24, 255), 4.0f);

      const int count = std::max(1, n->SwatchCount());
      const float pad = 6.0f;
      const float chipW = (kPreviewSize - pad * 2.0f) / (float)count;
      const float chipH = 60.0f;
      for (int i = 0; i < count; i++)
      {
         float rgb[3];
         n->GetSwatch(i, rgb);
         const ImVec2 tl(origin.x + pad + chipW * (float)i, origin.y + pad);
         const ImVec2 br(tl.x + chipW - 2.0f, tl.y + chipH);
         dl->AddRectFilled(tl, br,
                           IM_COL32((int)(rgb[0] * 255.0f), (int)(rgb[1] * 255.0f),
                                    (int)(rgb[2] * 255.0f), 255),
                           3.0f);

         // How much of the reference each swatch actually covers. Without it a
         // one-percent accent looks exactly as important as the colour half the
         // photo is made of, which is the wrong thing to build a look on.
         const float weight = n->SwatchWeight(i);
         dl->AddRectFilled(ImVec2(tl.x, br.y + 4.0f),
                           ImVec2(tl.x + (chipW - 2.0f) * std::min(1.0f, weight), br.y + 7.0f),
                           IM_COL32(150, 156, 176, 255));

         char idx[8];
         snprintf(idx, sizeof(idx), "%d", i + 1);
         dl->AddText(ImVec2(tl.x + 2.0f, br.y + 9.0f), IM_COL32(118, 124, 144, 255), idx);
      }

      const char* status;
      if (n->Input().IsConnected())
         status = n->live ? "live from cable" : "from cable";
      else if (!n->LoadedPath().empty())
         status = "from file";
      else
         status = "choose a photo, or cable one to 'ref'";
      dl->AddText(ImVec2(origin.x + pad, origin.y + h - 18.0f),
                  IM_COL32(126, 132, 152, 255), status);

      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                  IM_COL32(70, 74, 90, 255), 4.0f);
      ImGui::Dummy(ImVec2(kPreviewSize, h));
   }

   void DrawPaletteParams(PaletteNode* n)
   {
      if (ImGui::Button("Choose reference...", ImVec2(kPreviewSize, 0)))
         StartNodeFileDialog(n, Platform::OpenImageDialog, [](auto* m, const std::string& path) { m->Load(path); });

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         const size_t slash = file.find_last_of("/\\");
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         ImGui::PopTextWrapPos();
      }
      ModSliderInt("swatches", &n->swatchCount, 2, PaletteNode::kMaxSwatches);
      DropdownButton("order", PaletteNode::SortNames(), n->sortMode,
                     [n](int i) { PushUndoCheckpoint(); n->sortMode = i; });

      NodeSeparator("extract");
      ModSlider("min chroma", &n->minChroma, 0.0f, 0.2f);
      ModCheckbox("include neutrals", &n->includeNeutrals);
      ModSlider("seed", &n->seed, 0.0f, 64.0f, "%.1f");
      ModSliderInt("sample", &n->sampleSize, 16, 256);
      ModCheckbox("live", &n->live);
      // Drawn whether or not Live is on: hiding a modulatable slider behind a
      // checkbox renumbers every pin after it, which silently repoints this
      // node's modulation the moment the box is ticked.
      ModSlider("rate", &n->sampleRate, 1.0f, 60.0f, "%.0f");
      if (ModTriggerButton("Re-extract##paletteExtract", ImVec2(kPreviewSize, 0)))
         n->RequestExtract();

      // Shaping runs on the stored cluster centres, so these are live: they
      // re-grade the palette without re-clustering the photo behind it.
      NodeSeparator("shape");
      ModSlider("hue", &n->hueShift, -0.5f, 0.5f);
      ModSlider("saturation", &n->saturation, 0.0f, 2.0f);
      ModSlider("brightness", &n->brightness, -0.4f, 0.4f);
      ModSlider("spread", &n->spread, 0.0f, 2.0f);

      NodeSeparator("strip output");
      DropdownButton("blend", PaletteNode::StripNames(), n->stripMode,
                     [n](int i) { PushUndoCheckpoint(); n->stripMode = i; });
      ImGui::TextDisabled("out is a gradient of the palette");
   }

   void DrawRampParams(RampNode* n)
   {
      DropdownButton("type", RampNode::TypeNames(), n->type, [n](int i) { n->type = i; });
      DropdownButton("repeat", RampNode::RepeatNames(), n->repeat, [n](int i) { n->repeat = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("angle", &n->angle, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("center x", &n->centerX, -0.5f, 1.5f);
      ModSlider("center y", &n->centerY, -0.5f, 1.5f);
      ModSlider("scale", &n->scale, 0.05f, 8.0f);
      ModSlider("offset", &n->offset, -1.0f, 1.0f);
      ModSlider("gamma", &n->gamma, 0.1f, 4.0f);
      ModSlider("dither", &n->dither, 0.0f, 1.0f);

      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("stops", &n->stopCount, 2, RampNode::kStops);
      for (int i = 0; i < n->stopCount; i++)
      {
         ImGui::PushID(i);
         char label[24];
         snprintf(label, sizeof(label), "stop %d", i + 1);
         ColorSwatch(label, n->stopColor[i], n);
         ModSlider("at", &n->stopPos[i], 0.0f, 1.0f);
         ImGui::PopID();
      }
   }

   // Gradient-tool-style stop editor: drag a marker to reposition, click empty
   // track to add a stop (seeded with the color already showing there), right
   // click a marker to remove it. Position isn't run through ModSlider like
   // RampNode's - dragging on the bar is the primary interaction, and it would
   // otherwise fight the drag for control of the same float every frame.
   void DrawColorRampEditor(ColorRampNode* n)
   {
      const float size = kPreviewSize;
      const float barH = 26.0f;
      const float trackH = 22.0f;
      const float gap = 4.0f;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // gradient preview strip
      const int kSegments = 96;
      for (int i = 0; i < kSegments; i++)
      {
         float t0 = (float)i / kSegments;
         float t1 = (float)(i + 1) / kSegments;
         float c0[3], c1[3];
         n->Evaluate(t0, c0);
         n->Evaluate(t1, c1);
         ImVec2 tl(origin.x + t0 * size, origin.y);
         ImVec2 br(origin.x + t1 * size + 1.0f, origin.y + barH);
         ImU32 col0 = IM_COL32((int)(c0[0] * 255), (int)(c0[1] * 255), (int)(c0[2] * 255), 255);
         ImU32 col1 = IM_COL32((int)(c1[0] * 255), (int)(c1[1] * 255), (int)(c1[2] * 255), 255);
         dl->AddRectFilledMultiColor(tl, br, col0, col1, col1, col0);
      }
      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + barH), IM_COL32(70, 74, 90, 255), 3.0f);

      // stop track
      ImVec2 trackOrigin(origin.x, origin.y + barH + gap);
      ImVec2 trackBr(origin.x + size, trackOrigin.y + trackH);
      dl->AddRectFilled(trackOrigin, trackBr, IM_COL32(16, 16, 22, 255), 3.0f);
      dl->AddRect(trackOrigin, trackBr, IM_COL32(70, 74, 90, 255), 3.0f);

      ImGui::SetCursorScreenPos(trackOrigin);
      ImGui::InvisibleButton("##colorramp", ImVec2(size, trackH));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      auto toScreenX = [&](float x) { return trackOrigin.x + x * size; };
      auto toX = [&](float screenX) {
         return std::min(1.0f, std::max(0.0f, (screenX - trackOrigin.x) / size));
      };

      static ColorRampNode* sDragNode = nullptr;
      static int sDragIndex = -1;
      static ColorRampNode* sSelNode = nullptr;
      static int sSelIndex = -1;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      int nearest = -1;
      float nearestDist = 10.0f;
      for (int i = 0; i < n->stopCount; i++)
      {
         float d = std::fabs(toScreenX(n->stopPos[i]) - mouse.x);
         if (d < nearestDist)
         {
            nearestDist = d;
            nearest = i;
         }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         if (nearest >= 0)
         {
            sDragIndex = nearest;
         }
         else
         {
            float x = toX(mouse.x);
            float seedColor[3];
            n->Evaluate(x, seedColor);
            sDragIndex = n->AddStop(x, seedColor);
         }
         sDragNode = n;
         sSelNode = n;
         sSelIndex = sDragIndex;
      }
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && nearest >= 0)
      {
         PushUndoCheckpoint();
         n->RemoveStop(nearest);
         if (sSelNode == n && sSelIndex == nearest)
            sSelIndex = -1;
      }
      if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && nearest >= 0)
      {
         PushUndoCheckpoint();
         n->RemoveStop(nearest);
         if (sSelNode == n && sSelIndex == nearest)
            sSelIndex = -1;
         sDragIndex = -1;
         sDragNode = nullptr;
      }
      if (active && sDragNode == n && sDragIndex >= 0)
         n->MoveStop(sDragIndex, toX(mouse.x));
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragIndex = -1;
         sDragNode = nullptr;
      }
      if (sSelNode == n && sSelIndex >= n->stopCount)
         sSelIndex = -1;

      for (int i = 0; i < n->stopCount; i++)
      {
         float x = toScreenX(n->stopPos[i]);
         const bool isSel = (sSelNode == n && sSelIndex == i);
         ImU32 fill = IM_COL32((int)(n->stopColor[i][0] * 255), (int)(n->stopColor[i][1] * 255),
                               (int)(n->stopColor[i][2] * 255), 255);
         float r = (i == nearest || isSel) ? 7.0f : 5.5f;
         ImVec2 tip(x, trackOrigin.y + 2.0f);
         dl->AddTriangleFilled(ImVec2(x - r, tip.y + r * 1.6f), ImVec2(x + r, tip.y + r * 1.6f), tip,
                               isSel ? IM_COL32(255, 220, 120, 255) : IM_COL32(220, 224, 236, 255));
         ImVec2 chipTl(x - r * 0.6f, tip.y + r * 1.6f + 1.0f);
         dl->AddRectFilled(chipTl, ImVec2(chipTl.x + r * 1.2f, chipTl.y + 5.0f), fill);
      }

      ImGui::SetCursorScreenPos(ImVec2(origin.x, trackBr.y + gap));

      // One row per stop, left-to-right by position along the ramp - not by
      // raw array index, since RemoveStop's compaction reassigns indices and
      // a raw-index label would relabel unrelated stops out from under the
      // user every time one is deleted.
      int order[ColorRampNode::kMaxStops];
      for (int i = 0; i < n->stopCount; i++)
         order[i] = i;
      for (int i = 0; i < n->stopCount; i++)
         for (int j = i + 1; j < n->stopCount; j++)
            if (n->stopPos[order[j]] < n->stopPos[order[i]])
               std::swap(order[i], order[j]);

      for (int rank = 0; rank < n->stopCount; rank++)
      {
         const int idx = order[rank];
         ImGui::PushID(idx + 20000);
         char label[16];
         snprintf(label, sizeof(label), "stop %d", rank + 1);
         ColorSwatch(label, n->stopColor[idx], n);
         ImGui::SameLine(size - 18.0f);
         ImGui::BeginDisabled(n->stopCount <= 2);
         if (ImGui::SmallButton("x"))
         {
            PushUndoCheckpoint();
            n->RemoveStop(idx);
            if (sSelNode == n && sSelIndex == idx)
               sSelIndex = -1;
         }
         ImGui::EndDisabled();
         ImGui::PopID();
      }
      n->MarkDirty();

      ImGui::BeginDisabled(n->stopCount >= ColorRampNode::kMaxStops);
      if (ModTriggerButton("+ stop##colorRampAddStop", ImVec2(size, 0)) &&
          n->stopCount < ColorRampNode::kMaxStops)
      {
         PushUndoCheckpoint();
         float x = n->stopCount > 0 ? std::min(1.0f, n->stopPos[order[n->stopCount - 1]] + 0.1f) : 0.5f;
         float seedColor[3];
         n->Evaluate(x, seedColor);
         n->AddStop(x, seedColor);
      }
      ImGui::EndDisabled();
   }

   void DrawColorRampParams(ColorRampNode* n)
   {
      DrawColorRampEditor(n);
      DropdownButton("interpolation", ColorRampNode::InterpNames(), n->interpMode,
                     [n](int i) { PushUndoCheckpoint(); n->interpMode = i; n->MarkDirty(); });
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
   }

   void DrawImageAnalyzeParams(ImageAnalyzeNode* n)
   {
      const float r = n->RawR();
      const float g = n->RawG();
      const float b = n->RawB();
      const float res = n->Value(ImageAnalyzeNode::kResult);

      // Color swatch + Result meter
      ImVec4 col(r, g, b, 1.0f);
      ImGui::ColorButton("##ImageAnalyzeSwatch", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(24, 24));
      ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::Text("Result: %.3f", res);
      ImGui::ProgressBar(std::clamp(res, 0.0f, 1.0f), ImVec2(kPreviewSize * 0.55f, 0), "");
      ImGui::EndGroup();

      ImGui::Separator();

      // Output taps readout
      for (int i = 0; i < ImageAnalyzeNode::kOutputCount; i++)
      {
         const float v = n->Value(i);
         ImGui::Text("%-9s", n->OutputLabel(i));
         ImGui::SameLine();
         ImGui::ProgressBar(std::clamp(v, 0.0f, 1.0f), ImVec2(kPreviewSize * 0.55f, 0), "");
      }

      ImGui::Separator();

      DropdownButton("sample mode", ImageAnalyzeNode::SampleModeNames(), n->sampleMode,
                     [n](int i) { PushUndoCheckpoint(); n->sampleMode = i; });

      if (n->sampleMode == ImageAnalyzeNode::kPointProbe ||
          n->sampleMode == ImageAnalyzeNode::kBoxRegion ||
          n->sampleMode == ImageAnalyzeNode::kCenterWeighted)
      {
         ModSlider("probe U", &n->probeU, 0.0f, 1.0f);
         ModSlider("probe V", &n->probeV, 0.0f, 1.0f);
         if (n->sampleMode == ImageAnalyzeNode::kBoxRegion)
            ModSlider("radius", &n->probeRadius, 0.01f, 0.5f);
      }

      DropdownButton("operation", ImageAnalyzeNode::MathOpNames(), n->mathOp,
                     [n](int i) { PushUndoCheckpoint(); n->mathOp = i; });

      if (n->mathOp == ImageAnalyzeNode::kCustomExpression)
      {
         char buf[256];
         snprintf(buf, sizeof(buf), "%s", n->customFormula.c_str());
         ImGui::SetNextItemWidth(kParamWidth);
         if (ImGui::InputText("formula", buf, sizeof(buf)))
         {
            n->customFormula = buf;
         }
         if (!n->ExpressionError().empty())
         {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "err: %s", n->ExpressionError().c_str());
         }
         else
         {
            ImGui::TextDisabled("vars: r, g, b, a, lum, sat, hue, delta, t");
         }
      }

      ModSlider("gain", &n->gain, 0.0f, 8.0f);
      ModSlider("offset", &n->offset, -1.0f, 1.0f);
      ModSlider("power", &n->power, 0.1f, 5.0f);
      ModCheckbox("invert", &n->invert);
      ModCheckbox("clamp 0..1", &n->clamp01);

      ImGui::Separator();
      ModSlider("smoothing", &n->smoothing, 0.0f, 0.99f);
      ModSlider("samples / sec", &n->sampleRate, 1.0f, 60.0f, "%.0f");
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("sample res", &n->sampleSize, 8, 256);
   }

   void DrawNullModulatorParams(NullModulatorNode* n)
   {
      const float v = n->Value01();
      ImGui::Text("Value: %.3f", v);
      ImGui::ProgressBar(std::clamp(v, 0.0f, 1.0f), ImVec2(kPreviewSize * 0.85f, 0), "");
      if (!n->input)
      {
         ModSlider("constant in", &n->constantIn, 0.0f, 1.0f);
      }
      else
      {
         ImGui::TextDisabled("driven by input modulator");
      }
   }
