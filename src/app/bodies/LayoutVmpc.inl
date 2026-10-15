// Part of src/main.cpp, included there in order (one translation unit).
// Layout and VMPC node bodies

   // ---- Layout (Infinite-Turbo) --------------------------------------------
   // Miniature of the canvas: every connected layer as its rectangle, click
   // to select (topmost wins), drag to move it in canvas pixels.
   void DrawLayoutCanvasEditor(LayoutNode* n, float width)
   {
      const float aspect = (float)std::max(1, n->canvasH) / (float)std::max(1, n->canvasW);
      const float height = std::clamp(width * aspect, 40.0f, width * 1.5f);
      const float k = std::min(width / (float)std::max(1, n->canvasW), height / (float)std::max(1, n->canvasH));
      const float mapW = (float)n->canvasW * k;
      const float mapH = (float)n->canvasH * k;
      const ImVec2 o = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(o, ImVec2(o.x + mapW, o.y + mapH),
                        IM_COL32((int)(n->bgColor[0] * 255.0f), (int)(n->bgColor[1] * 255.0f),
                                 (int)(n->bgColor[2] * 255.0f), 255));
      static const ImU32 kLayerCols[LayoutNode::kSlots] = {
         IM_COL32(90, 170, 255, 255), IM_COL32(255, 150, 70, 255), IM_COL32(120, 220, 130, 255),
         IM_COL32(230, 100, 200, 255), IM_COL32(250, 220, 90, 255), IM_COL32(100, 220, 220, 255),
         IM_COL32(200, 130, 255, 255), IM_COL32(240, 90, 90, 255) };
      dl->PushClipRect(o, ImVec2(o.x + mapW, o.y + mapH), true);
      for (int i = 0; i < LayoutNode::kSlots; i++)
      {
         if (n->SourceWidth(i) <= 0 || !n->visible[i])
            continue;
         float lx, ly, lw, lh;
         n->LayerRect(i, lx, ly, lw, lh);
         const ImVec2 a(o.x + lx * k, o.y + ly * k);
         const ImVec2 b(a.x + lw * k, a.y + lh * k);
         dl->AddRectFilled(a, b, (kLayerCols[i] & 0x00FFFFFF) | 0x40000000);
         dl->AddRect(a, b, kLayerCols[i], 0.0f, 0, i == n->selectedLayer ? 2.5f : 1.0f);
         char label[4];
         snprintf(label, sizeof(label), "%d", i + 1);
         dl->AddText(ImVec2(a.x + 3.0f, a.y + 2.0f), kLayerCols[i], label);
      }
      dl->PopClipRect();
      dl->AddRect(o, ImVec2(o.x + mapW, o.y + mapH), IM_COL32(150, 156, 176, 255));

      ImGui::SetCursorScreenPos(o);
      ImGui::InvisibleButton("##layoutMap", ImVec2(std::max(1.0f, mapW), std::max(1.0f, mapH)));
      if (ImGui::IsItemActivated())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const float cx = (m.x - o.x) / k, cy = (m.y - o.y) / k;
         for (int i = LayoutNode::kSlots - 1; i >= 0; i--)
         {
            if (n->SourceWidth(i) <= 0 || !n->visible[i])
               continue;
            float lx, ly, lw, lh;
            n->LayerRect(i, lx, ly, lw, lh);
            if (cx >= lx && cx <= lx + lw && cy >= ly && cy <= ly + lh)
            {
               n->selectedLayer = i;
               break;
            }
         }
         PushUndoCheckpoint();
      }
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
      {
         static float sAccumX = 0.0f, sAccumY = 0.0f;
         const ImVec2 d = ImGui::GetIO().MouseDelta;
         sAccumX += d.x / k;
         sAccumY += d.y / k;
         const float stepX = std::trunc(sAccumX), stepY = std::trunc(sAccumY);
         const int s = std::clamp(n->selectedLayer, 0, LayoutNode::kSlots - 1);
         n->x[s] += stepX;
         n->y[s] += stepY;
         sAccumX -= stepX;
         sAccumY -= stepY;
         gPatchDirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("click a layer to select it, drag to move it (canvas pixels)");
   }

   void DrawLayoutParams(GraphNode& gn, LayoutNode* n)
   {
      const float half = (kPreviewSize - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
      ImGui::TextDisabled("canvas: %d x %d px", n->canvasW, n->canvasH);
      ImGui::SetNextItemWidth(half);
      if (ImGui::InputInt("##layoutW", &n->canvasW, 0, 0))
         gPatchDirty = true;
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(half);
      if (ImGui::InputInt("##layoutH", &n->canvasH, 0, 0))
         gPatchDirty = true;
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      n->canvasW = std::clamp(n->canvasW, 1, 16384);
      n->canvasH = std::clamp(n->canvasH, 1, 16384);
      static const std::vector<std::string> kCanvasPresets = { "canvas preset...", "1920 x 1080", "1280 x 720",
                                                                "3840 x 2160", "2560 x 1440", "1080 x 1920",
                                                                "1080 x 1080", "1024 x 768", "5760 x 1080 (3 x HD)",
                                                                "3840 x 1080 (2 x HD)" };
      static const int kCW[] = { 0, 1920, 1280, 3840, 2560, 1080, 1080, 1024, 5760, 3840 };
      static const int kCH[] = { 0, 1080, 720, 2160, 1440, 1920, 1080, 768, 1080, 1080 };
      DropdownButton("##layoutPreset", kCanvasPresets, 0, [n](int i) {
         if (i <= 0)
            return;
         PushUndoCheckpoint();
         n->canvasW = kCW[i];
         n->canvasH = kCH[i];
      }, kPreviewSize, /*modulatable=*/false);
      ColorSwatch("background", n->bgColor, n);
      ModSlider("bg alpha", &n->bgAlpha, 0.0f, 1.0f, "%.2f");

      ImGui::Dummy(ImVec2(0, 3));
      DrawLayoutCanvasEditor(n, kPreviewSize);
      ImGui::Dummy(ImVec2(0, 3));

      // Selected layer: size mode, exact pixels, quick placements. Plain
      // drag fields (double-click to type) since they follow the selection.
      {
         const int s = std::clamp(n->selectedLayer, 0, LayoutNode::kSlots - 1);
         ImGui::PushID(7000 + s);
         {
            std::vector<std::string> items;
            for (int i = 0; i < LayoutNode::kSlots; i++)
            {
               char item[64];
               if (n->SourceWidth(i) > 0)
                  snprintf(item, sizeof(item), "layer %d  (%d x %d)", i + 1, n->SourceWidth(i), n->SourceHeight(i));
               else
                  snprintf(item, sizeof(item), "layer %d  (not connected)", i + 1);
               items.push_back(item);
            }
            NodeDropdown("layoutSel", items, s, [n](int i) { n->selectedLayer = i; }, kPreviewSize);
         }
         if (n->SourceWidth(s) > 0)
            ImGui::TextDisabled("source %d x %d px", n->SourceWidth(s), n->SourceHeight(s));
         else
            ImGui::TextDisabled("input %d not connected", s + 1);
         float lx, ly, lw, lh;
         n->LayerRect(s, lx, ly, lw, lh);
         ImGui::TextDisabled("on canvas %.0f x %.0f at %.0f, %.0f", lw, lh, lx, ly);

         bool vis = n->visible[s];
         if (ImGui::Checkbox("visible", &vis)) { PushUndoCheckpoint(); n->visible[s] = vis; }
         ImGui::SameLine();
         bool custom = n->sizeMode[s] == LayoutNode::kSizeCustom;
         if (ImGui::Checkbox("custom size", &custom))
         {
            PushUndoCheckpoint();
            n->sizeMode[s] = custom ? LayoutNode::kSizeCustom : LayoutNode::kSizeReal;
            if (custom && (n->customW[s] <= 0 || n->customH[s] <= 0))
            {
               float bw = 0.0f, bh = 0.0f;
               n->BaseSize(s, bw, bh);
               n->customW[s] = std::max(1, (int)std::lround(bw));
               n->customH[s] = std::max(1, (int)std::lround(bh));
            }
         }
         if (custom)
         {
            ImGui::SetNextItemWidth(half);
            ImGui::DragInt("##layoutCW", &n->customW[s], 1.0f, 1, 16384, "w %d px", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemActivated()) PushUndoCheckpoint();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(half);
            ImGui::DragInt("##layoutCH", &n->customH[s], 1.0f, 1, 16384, "h %d px", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemActivated()) PushUndoCheckpoint();
         }
         ImGui::SetNextItemWidth(half);
         ImGui::DragFloat("##layoutX", &n->x[s], 1.0f, -16384.0f, 16384.0f, "x %.0f px");
         if (ImGui::IsItemActivated()) PushUndoCheckpoint();
         ImGui::SameLine();
         ImGui::SetNextItemWidth(half);
         ImGui::DragFloat("##layoutY", &n->y[s], 1.0f, -16384.0f, 16384.0f, "y %.0f px");
         if (ImGui::IsItemActivated()) PushUndoCheckpoint();

         const float quarter = (kPreviewSize - 3.0f * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
         if (ImGui::Button("1:1", ImVec2(quarter, 0))) { PushUndoCheckpoint(); n->PlaceRealSize(s); }
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("real pixel size (scale 1)");
         ImGui::SameLine();
         if (ImGui::Button("fit", ImVec2(quarter, 0))) { PushUndoCheckpoint(); n->PlaceFit(s); }
         ImGui::SameLine();
         if (ImGui::Button("fill", ImVec2(quarter, 0))) { PushUndoCheckpoint(); n->PlaceFill(s); }
         ImGui::SameLine();
         if (ImGui::Button("ctr", ImVec2(quarter, 0))) { PushUndoCheckpoint(); n->PlaceCenter(s); }
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("centre on the canvas");
         ImGui::PopID();
      }

      // Every layer's animatable controls, always drawn in the same order so
      // their CV pins keep stable indices (modulate x/y/scale/opacity with an
      // LFO, a MIDI CC...).
      ImGui::Dummy(ImVec2(0, 3));
      ImGui::TextDisabled("layers (CV-controllable)");
      for (int i = 0; i < LayoutNode::kSlots; i++)
      {
         ImGui::PushID(7100 + i);
         char label[24];
         if (n->SourceWidth(i) > 0)
            ImGui::TextColored(ImVec4(0.75f, 0.82f, 1.0f, 1.0f), "%d: %d x %d", i + 1, n->SourceWidth(i), n->SourceHeight(i));
         else
            ImGui::TextDisabled("%d: -", i + 1);
         snprintf(label, sizeof(label), "x %d", i + 1);
         ModSlider(label, &n->x[i], -(float)n->canvasW, (float)n->canvasW, "%.0f px");
         snprintf(label, sizeof(label), "y %d", i + 1);
         ModSlider(label, &n->y[i], -(float)n->canvasH, (float)n->canvasH, "%.0f px");
         snprintf(label, sizeof(label), "scale %d", i + 1);
         ModSlider(label, &n->scale[i], 0.0f, 4.0f, "%.3f");
         snprintf(label, sizeof(label), "opacity %d", i + 1);
         ModSlider(label, &n->opacity[i], 0.0f, 1.0f, "%.2f");
         ImGui::PopID();
      }

      DrawOutputWindowPanel(gn);
   }

   // ---- VMPC (Infinite-Turbo) ----------------------------------------------
   void DrawVmpcParams(GraphNode& gn, VmpcNode* n)
   {
      const float W = 260.0f;
      int loaded = 0;
      for (int p = 0; p < VmpcNode::kPads; p++)
         loaded += n->PadLoaded(p) ? 1 : 0;
      ImGui::TextDisabled("%d/16 clips - notes %d-%d", loaded, n->baseNote, n->baseNote + VmpcNode::kPads - 1);

      // 4x4 pads, pad 1 bottom-left like the MPC. Each pad is a gate button
      // with its own CV pin: gate high = pad held.
      {
         static const char* kModeTag[3] = { "one shot", "gate", "loop" };
         const float gap = 4.0f;
         const float padW = (W - gap * 3.0f) / 4.0f;
         const float padH = 46.0f;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         for (int row = 0; row < 4; row++)
         {
            for (int col = 0; col < 4; col++)
            {
               const int p = (3 - row) * 4 + col;
               ImGui::SetCursorScreenPos(ImVec2(origin.x + (float)col * (padW + gap), origin.y + (float)row * (padH + gap)));
               ImGui::PushID(8800 + p);
               char label[32];
               snprintf(label, sizeof(label), "%d##vmpcPad%d", p + 1, p + 1);
               std::string sub = n->PadLoaded(p) ? n->PadName(p) : std::string("empty");
               if (sub.size() > 10)
                  sub = sub.substr(0, 9) + ".";
               sub += std::string("\n") + kModeTag[std::clamp(n->padMode[p], 0, 2)];
               bool clicked = false;
               const bool held = ModGateButton(label, ImVec2(padW, padH), n->PadPlaying(p), p == n->selectedPad,
                                               n->PadLoaded(p), sub.c_str(), clicked);
               if (clicked)
                  n->selectedPad = p;
               if (held != n->uiHeld[p])
               {
                  n->uiHeld[p] = held;
                  n->PadEvent(p, held, 1.0f);
               }
               ImGui::PopID();
            }
         }
         ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 4.0f * (padH + gap)));
         ImGui::Dummy(ImVec2(W, 2.0f));
      }

      // Playhead of the clip on air.
      {
         const ImVec2 o = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(o, ImVec2(o.x + W, o.y + 4.0f), IM_COL32(40, 44, 56, 255), 2.0f);
         if (n->ActivePad() >= 0)
            dl->AddRectFilled(o, ImVec2(o.x + W * n->ActivePosition01(), o.y + 4.0f), IM_COL32(120, 190, 255, 255), 2.0f);
         ImGui::Dummy(ImVec2(W, 6.0f));
      }

      // Selected pad editor. Plain widgets: they follow the selection (the
      // pads themselves carry the CV pins).
      const int p = std::clamp(n->selectedPad, 0, VmpcNode::kPads - 1);
      ImGui::PushID(8900 + p);
      if (n->PadLoaded(p))
         ImGui::TextColored(ImVec4(0.75f, 0.82f, 1.0f, 1.0f), "pad %d - %s (%d x %d, %.1f s, %s)", p + 1,
                            n->PadName(p).c_str(), n->PadWidth(p), n->PadHeight(p), n->PadDuration(p),
                            n->PadHasAudio(p) ? "audio" : "no audio");
      else
         ImGui::TextDisabled("pad %d - empty", p + 1);
      const float third = (W - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
      if (ImGui::Button("Load video...", ImVec2(third, 0)))
      {
         StartNodeFileDialog(n, Platform::OpenVideoDialog, [p](auto* m, const std::string& path) {
            PushUndoCheckpoint();
            if (m->LoadPad(p, path))
            {
               m->padStart[p] = 0.0f;
               m->padEnd[p] = 1.0f;
            }
         });
      }
      ImGui::SameLine();
      if (ImGui::Button("Load folder...", ImVec2(third, 0)))
      {
         StartNodeFileDialog(n, [] { return Platform::OpenFolderDialog("Load a folder of videos into the 16 pads"); },
                             [](auto* m, const std::string& folder) {
                                PushUndoCheckpoint();
                                m->LoadFolder(folder);
                             });
      }
      ImGui::SameLine();
      if (ImGui::Button("Clear pad", ImVec2(third, 0)))
      {
         PushUndoCheckpoint();
         n->ClearPad(p);
      }
      if (n->PadStatus(p) != "loaded" && n->PadStatus(p) != "empty")
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
         ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", n->PadStatus(p).c_str());
         ImGui::PopTextWrapPos();
      }

      int mode = std::clamp(n->padMode[p], 0, 2);
      if (ImGui::RadioButton("one shot", mode == VmpcNode::kOneShot)) { PushUndoCheckpoint(); n->padMode[p] = VmpcNode::kOneShot; }
      ImGui::SameLine();
      if (ImGui::RadioButton("gate", mode == VmpcNode::kGate)) { PushUndoCheckpoint(); n->padMode[p] = VmpcNode::kGate; }
      ImGui::SameLine();
      if (ImGui::RadioButton("loop", mode == VmpcNode::kLoopToggle)) { PushUndoCheckpoint(); n->padMode[p] = VmpcNode::kLoopToggle; }

      // Drag fields: drag to change, double-click (or Ctrl+click) to type.
      const ImGuiSliderFlags clampFlags = ImGuiSliderFlags_AlwaysClamp;
      ImGui::SetNextItemWidth(third);
      ImGui::DragFloat("##vmpcStart", &n->padStart[p], 0.001f, 0.0f, 1.0f, "in %.3f", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(third);
      ImGui::DragFloat("##vmpcEnd", &n->padEnd[p], 0.001f, 0.0f, 1.0f, "out %.3f", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      n->padEnd[p] = std::max(n->padEnd[p], n->padStart[p] + 0.001f);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(third);
      ImGui::DragFloat("##vmpcSpeed", &n->padSpeed[p], 0.01f, -4.0f, 4.0f, "speed %.2f", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::PopID();

      ImGui::SetNextItemWidth(third);
      ImGui::DragInt("##vmpcBase", &n->baseNote, 0.2f, 0, 112, "base note %d", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      if (ModCheckbox("hold last frame", &n->holdLastFrame))
         PushUndoCheckpoint();
      // The clip's own soundtrack on the "audio" output (wire it to an Audio
      // Out or a mixer). Clips without an audio track stay silent.
      if (ModCheckbox("play audio", &n->playAudio))
         PushUndoCheckpoint();
      ModSlider("audio volume", &n->audioVolume, 0.0f, 1.5f, "%.2f");
      // Turbo 0.50: how a hit on another pad replaces the clip on air (the
      // Slideshow's transitions). Slider added after "audio volume" so the
      // existing modulation pins keep their numbers.
      DropdownButton("transition##vmpcTransition", VmpcNode::TransitionNames(), n->transitionStyle,
                     [n](int i) { n->transitionStyle = i; });
      ModSlider("transition time", &n->transitionTime, 0.0f, 10.0f, "%.2f s");
      if (ModTriggerButton("STOP##vmpcStop", ImVec2(W, 0)))
         n->StopAll();

      DrawOutputWindowPanel(gn);
   }

   // Turbo 0.43: Arrangement Timeline (model glue, audio, video, panel).
#include "arrange/ArrangeUi.inl"
   // Turbo 0.44: Clip Matrix (session view) UI and recording.
#include "nodes/ClipMatrixUi.inl"
