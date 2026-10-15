// Part of src/main.cpp, included there in order (one translation unit).
// audio node body layout (v3), columns, waveform view

   // ======================================================================
   // v3 audio node layout (docs/plans/audio/audio-node-ui-system.md v3)
   // ======================================================================
   //
   // The load-bearing idea: an audio node's width is a *scope*, not a
   // constant that helpers happen to also use. v2 declared kAudioNodeWidth
   // and then drew the visualizer at 440, NodeSeparator at kPreviewSize
   // (190) and the knob row at whatever N knobs added up to (~556), so one
   // body held three widths and the node grew to the widest of them -
   // nothing shared an edge with anything, which is the single biggest
   // reason the result read as unfinished.
   //
   // Here BeginAudioBody sets the width once and every helper below derives
   // from it. Rows are laid out to *fit* the body; they cannot set it.
   float gAudioBodyW = kAudioNodeWidth;     // enforced body width
   float gAudioBodyX = 0.0f;                // body's left edge, screen space
   float gAudioContentX = 0.0f;             // left edge of the current column
   float gAudioContentW = kAudioNodeWidth;  // width of that column
   CategoryColors::Color gAudioTint{ 0.5f, 0.6f, 0.8f };

   // Section panels measure their own height and reuse it next frame to draw
   // their background behind their content (a background has to be drawn
   // before the content it sits behind, and ImGui only knows the height
   // afterwards). Keyed by node so nodes never share a cache; the height
   // only changes when a node's layout changes, so the one-frame lag is
   // visible exactly once, on the frame a pattern/waveform switch resizes a
   // panel. Splitting the node editor's draw list to do it in one pass was
   // the alternative and is not worth the risk to ed's own channel usage.
   std::map<std::pair<int, int>, float> gAudioSectionHeight;
   int gAudioSectionIndex = 0;
   float gAudioSectionTop = 0.0f;

   // Per-node peak-hold state for the level meter. Main-thread only: it
   // decays a value already published by CookIfNeeded and never reaches into
   // an AudioNode, so it adds no thread-safety surface.
   std::map<int, float> gAudioMeterPeak;
   std::map<int, double> gAudioMeterPeakTime;

   // The node's fixed readout strip: idle stat on the left, the
   // hovered/dragged param's name and value on the right. One location, it
   // never moves, and it never covers the control being adjusted - which is
   // the whole reason the knob tooltip is gone (see SetAudioReadout).
   void BeginAudioBody(int nodeIndex, const std::string& category, float width, const char* idleStat)
   {
      gAudioBodyW = width;
      gAudioBodyX = ImGui::GetCursorScreenPos().x;
      gAudioContentX = gAudioBodyX;
      gAudioContentW = width;
      gAudioTint = CategoryColors::ColorFor(category);
      gAudioSectionIndex = 0;

      std::string readout;
      auto it = gAudioReadout.find(nodeIndex);
      if (it != gAudioReadout.end())
      {
         readout = it->second;
         it->second.clear(); // cleared every frame; controls re-write it while hovered
      }

      const bool isLight = IsThemeLight();
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float h = ImGui::GetTextLineHeight() + 6.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h),
                        isLight ? IM_COL32(0, 0, 0, 12) : IM_COL32(255, 255, 255, 10), 3.0f);
      const float textY = p.y + 3.0f;
      if (idleStat != nullptr && idleStat[0] != '\0')
         dl->AddText(ImVec2(p.x + 7.0f, textY),
                     isLight ? IM_COL32(80, 88, 108, 255) : IM_COL32(132, 138, 158, 255),
                     idleStat);
      if (!readout.empty())
      {
         const ImVec2 sz = ImGui::CalcTextSize(readout.c_str());
         dl->PushClipRect(p, ImVec2(p.x + width, p.y + h), true);
         dl->AddText(ImVec2(p.x + width - 8.0f - sz.x, textY),
                     isLight ? IM_COL32(30, 36, 52, 255) : IM_COL32(226, 232, 244, 255),
                     readout.c_str());
         dl->PopClipRect();
      }
      ImGui::Dummy(ImVec2(width, h));
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
   }

   // ---- columns ----------------------------------------------------------
   // Splits the body into N side-by-side columns. Every helper below already
   // derives from gAudioBodyX/gAudioBodyW (panels) and gAudioContentX/
   // gAudioContentW (rows and sliders), so pointing those four at a column is
   // all it takes for sections, knob rows and sliders to lay out inside it -
   // the "width is a scope" rule doing exactly what it was written for.
   struct AudioColumnState
   {
      float bodyX, bodyW, contentX, contentW, top, maxBottom;
      int count;
   };
   AudioColumnState gAudioColumn {};

   void BeginAudioColumns(int count)
   {
      gAudioColumn.bodyX = gAudioBodyX;
      gAudioColumn.bodyW = gAudioBodyW;
      gAudioColumn.contentX = gAudioContentX;
      gAudioColumn.contentW = gAudioContentW;
      gAudioColumn.top = ImGui::GetCursorScreenPos().y;
      gAudioColumn.maxBottom = gAudioColumn.top;
      gAudioColumn.count = std::max(1, count);
   }

   // Gutter between columns, so two panels never share an edge.
   const float kAudioColumnGap = 12.0f;

   void BeginAudioColumn(int index)
   {
      const float colW =
         (gAudioColumn.contentW - kAudioColumnGap * (float)(gAudioColumn.count - 1)) /
         (float)gAudioColumn.count;
      const float x = gAudioColumn.contentX + (float)index * (colW + kAudioColumnGap);
      gAudioBodyX = x;
      gAudioBodyW = colW;
      gAudioContentX = x;
      gAudioContentW = colW;
      ImGui::SetCursorScreenPos(ImVec2(x, gAudioColumn.top));
      // BeginGroup is what makes the column an actual layout scope and not
      // just four globals pointed somewhere new.
      //
      // ImGui returns the cursor to the *window's* content origin after every
      // item, and ImGui::Indent (which BeginAudioSection uses) is measured
      // from that same window origin - neither knows a column exists. So only
      // the first widget after SetCursorScreenPos landed in column 1;
      // everything after it wrapped back to column 0 and drew on top of it.
      // Widgets that position themselves explicitly from gAudioContentX
      // (AudioKnobRow, the section panel backgrounds) were unaffected, which
      // is why the columns looked correct apart from every visualizer and
      // every slider inside a section - engine B's were painted over engine
      // A's, and engine B's invisible buttons swallowed engine A's drags.
      //
      // BeginGroup sets DC.GroupOffset/DC.Indent to the current cursor x, so
      // wrapping *and* Indent are both relative to the column from here on.
      ImGui::BeginGroup();
   }

   void EndAudioColumn()
   {
      // Read the cursor before EndGroup: EndGroup emits the group's own
      // ItemSize, which moves it.
      gAudioColumn.maxBottom = std::max(gAudioColumn.maxBottom, ImGui::GetCursorScreenPos().y);
      ImGui::EndGroup();
      gAudioBodyX = gAudioColumn.bodyX;
      gAudioBodyW = gAudioColumn.bodyW;
      gAudioContentX = gAudioColumn.contentX;
      gAudioContentW = gAudioColumn.contentW;
   }

   // Reserves the tallest column's height in one go, so whatever follows the
   // column block starts below all of them.
   void EndAudioColumns()
   {
      ImGui::SetCursorScreenPos(ImVec2(gAudioColumn.contentX, gAudioColumn.top));
      ImGui::Dummy(ImVec2(gAudioColumn.contentW, gAudioColumn.maxBottom - gAudioColumn.top));
   }

   void EndAudioBody()
   {
      gAudioContentX = gAudioBodyX;
      gAudioContentW = gAudioBodyW;
   }

   // A labelled, inset sub-panel spanning the full body width - what v2's
   // NodeSeparator hairline was standing in for. Grouping is what turns a
   // 17-row wall into four readable clusters, and it is the change with the
   // highest visual return per line in the whole of v3.
   void BeginAudioSection(const char* label)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      gAudioSectionTop = p.y;
      const float cached = gAudioSectionHeight[std::pair<int, int>(gCurrentNodeIndex, gAudioSectionIndex)];
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      if (cached > 0.0f)
      {
         dl->AddRectFilled(ImVec2(gAudioBodyX, p.y), ImVec2(gAudioBodyX + gAudioBodyW, p.y + cached),
                           isLight ? IM_COL32(0, 0, 0, 10) : IM_COL32(255, 255, 255, 9), 5.0f);
         dl->AddRect(ImVec2(gAudioBodyX, p.y), ImVec2(gAudioBodyX + gAudioBodyW, p.y + cached),
                     isLight ? IM_COL32(0, 0, 0, 20) : IM_COL32(255, 255, 255, 16), 5.0f);
      }

      const float headerH = ImGui::GetTextLineHeight() + 4.0f;
      dl->AddText(ImVec2(gAudioBodyX + kAudioSectionPad, p.y + 3.0f),
                  isLight ? IM_COL32((int)(gAudioTint.r * 180.0f), (int)(gAudioTint.g * 180.0f),
                                     (int)(gAudioTint.b * 180.0f), 255)
                          : IM_COL32((int)(gAudioTint.r * 255.0f), (int)(gAudioTint.g * 255.0f),
                                     (int)(gAudioTint.b * 255.0f), 210),
                  label);
      const float ruleY = p.y + headerH + 2.0f;
      dl->AddLine(ImVec2(gAudioBodyX + kAudioSectionPad, ruleY),
                  ImVec2(gAudioBodyX + gAudioBodyW - kAudioSectionPad, ruleY),
                  isLight ? IM_COL32(0, 0, 0, 22) : IM_COL32(255, 255, 255, 18), 1.0f);
      ImGui::Dummy(ImVec2(gAudioBodyW, headerH + 4.0f));

      ImGui::Indent(kAudioSectionPad);
      gAudioContentX = gAudioBodyX + kAudioSectionPad;
      gAudioContentW = gAudioBodyW - 2.0f * kAudioSectionPad;
   }

   void EndAudioSection()
   {
      ImGui::Unindent(kAudioSectionPad);
      ImGui::Dummy(ImVec2(gAudioBodyW, kAudioSectionPad * 0.5f));
      const float h = ImGui::GetCursorScreenPos().y - gAudioSectionTop;
      gAudioSectionHeight[std::pair<int, int>(gCurrentNodeIndex, gAudioSectionIndex)] = h;
      gAudioSectionIndex++;
      gAudioContentX = gAudioBodyX;
      gAudioContentW = gAudioBodyW;
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
   }

   // A row of N equal cells across the current content column. Cell width is
   // gAudioContentW / N, so a row is exactly the content width by
   // construction and the v2 overflow (a 556px row inside a 440px node) is
   // structurally impossible rather than something to remember to check.
   //
   // Cells bottom-align on the largest control in the row, so mixed
   // large/small knobs share one caption baseline - the thing that makes a
   // row read as a rack strip rather than as dials scattered at random
   // heights.
   struct AudioKnobRow
   {
      float x0, y0, cellW, maxDia, rowH, headerH;
      int count, index;

      // `headerRowH` reserves a strip above the knobs for cells that carry a
      // mode dropdown over their knob (see DropdownKnob). Plain knobs in the
      // same row drop below it, so every cap and every caption in the row
      // still shares one baseline - the alternative, letting the dropdown
      // cells be taller, is exactly the ragged row the bottom-align exists to
      // prevent.
      AudioKnobRow(int cellCount, float maxDiameter = kKnobLarge, float headerRowH = 0.0f, bool hasCaptions = true)
      {
         count = std::max(1, cellCount);
         maxDia = maxDiameter;
         headerH = headerRowH;
         x0 = gAudioContentX;
         y0 = ImGui::GetCursorScreenPos().y;
         cellW = gAudioContentW / (float)count;
         index = 0;
         rowH = headerH + maxDia + (hasCaptions ? (4.0f + ImGui::GetTextLineHeight()) : 0.0f);
      }

      void Place(float dia) const
      {
         ImGui::SetCursorScreenPos(ImVec2(x0 + (float)index * cellW, y0 + headerH + (maxDia - dia)));
      }

      void Knob(const char* label, float* v, float lo, float hi, const char* fmt,
                float dia = kKnobSmall, bool dbTaper = false, bool freqTaper = false)
      {
         Place(dia);
         AudioWidgetStyle style = AudioWidgetStyle::Knob;
         if (dbTaper)
            style = AudioWidgetStyle::KnobDb;
         else if (freqTaper || (lo >= 1.0f && hi >= 1000.0f && (strstr(fmt, "Hz") != nullptr || strcmp(label, "freq") == 0 || strcmp(label, "cutoff") == 0)))
            style = AudioWidgetStyle::KnobFreq;
         ModKnob(label, v, lo, hi, fmt, dia, cellW, style);
         index++;
      }

      void KnobInt(const char* label, int* v, int lo, int hi, float dia = kKnobSmall)
      {
         Place(dia);
         ModKnobInt(label, v, lo, hi, dia, cellW);
         index++;
      }

      // A vertical fader occupying one cell of the same row. `height` plays
      // the role `dia` does for a knob, so a row of faders bottom-aligns with
      // a row of knobs on the same caption baseline. `dbTaper` opts into the
      // console taper (unity at 75% of throw) - only meaningful for a
      // -60..+12 dB range, so a symmetric trim like Audio In leaves it off.
      void Fader(const char* label, float* v, float lo, float hi, const char* fmt, float height,
                 bool dbTaper = false)
      {
         Place(height);
         ModKnob(label, v, lo, hi, fmt, height, cellW, dbTaper ? AudioWidgetStyle::VFaderDb : AudioWidgetStyle::VFader);
         index++;
      }

      // An enum param as a cell of the same pitch: button on top, caption
      // underneath in the same style and on the same baseline as a knob's,
      // so it reads as one of the row. v2's DropdownButton put its label to
      // the *right* of the button, which both broke the row's rhythm and ate
      // ~60px of its width.
      // modCount > 0: only the first modCount options are reachable by CV /
      // MIDI / Performance (Turbo 0.48: an option appended later keeps the old
      // modulation range, so existing patches map as before).
      void Dropdown(const char* label, const std::vector<std::string>& options, int current,
                    std::function<void(int)> onSelect,
                    const std::vector<std::string>& categories = {}, int modCount = 0)
      {
         if (options.empty())
         {
            index++;
            return;
         }
         const float btnW = std::min(cellW - 8.0f, 118.0f);
         const float btnH = ImGui::GetFrameHeight();
         const float cx = x0 + (float)index * cellW + cellW * 0.5f;
         // Vertically centred in the knob-height cell, not bottom-aligned -
         // bottom-aligning left a tall empty gap above the button and made
         // it read as sitting low relative to the knobs sharing its row.
         ImGui::SetCursorScreenPos(ImVec2(cx - btnW * 0.5f, y0 + (maxDia - btnH) * 0.5f));
         const int modMax = (modCount > 0 ? std::min(modCount, (int)options.size()) : (int)options.size()) - 1;
         DiscreteParamRef ref = BeginDiscreteParam(label, (float)current, 0.0f, (float)modMax);
         int safe = std::clamp(current, 0, (int)options.size() - 1);
         if (ref.modulated)
         {
            const int driven = std::clamp((int)lroundf(*ref.value), 0, modMax);
            if (driven != safe)
            {
               safe = driven;
               gApplyingDiscreteCv = true;
               onSelect(driven);
               gApplyingDiscreteCv = false;
            }
            ImGui::BeginDisabled();
         }
         const std::string caption = options[safe] + "##" + label;
         bool ddClicked = false;
         {
            DesignDropdownFace face;
            ddClicked = ImGui::Button(caption.c_str(), ImVec2(std::max(24.0f, btnW - 18.0f), 0));
         }
         if (ddClicked)
         {
            PrepareDropdown(options, categories, safe, onSelect);
         }
         const bool hovered = ImGui::IsItemHovered(ref.modulated ? ImGuiHoveredFlags_AllowWhenDisabled
                                                                 : ImGuiHoveredFlags_None);
         if (ref.modulated)
            ImGui::EndDisabled();
         EndDiscreteParam(ref, hovered);
         const bool isLight = IsThemeLight();
         const ImVec2 sz = ImGui::CalcTextSize(label);
         ImGui::GetWindowDrawList()->AddText(ImVec2(cx - sz.x * 0.5f, y0 + headerH + maxDia + 4.0f),
                                             isLight ? IM_COL32(50, 55, 70, 255) : IM_COL32(176, 182, 198, 255), label);
         index++;
      }

      // One cell holding a mode dropdown directly above its knob - the shape
      // the reference sketch asks for wherever a knob's meaning is set by a
      // selector ("which filter", "which warp"). Reading them as one control
      // is the point: the pair is a single decision, and splitting them across
      // two rows made the knob look like it belonged to whatever sat above it.
      // `knobDisabled` greys the knob only. The dropdown must stay live: the
      // knob is meaningless precisely when the mode is "off", and disabling
      // the whole cell would leave no way to select any other mode.
      void DropdownKnob(const char* dropId, const std::vector<std::string>& options, int current,
                        std::function<void(int)> onSelect, const char* knobLabel, float* v,
                        float lo, float hi, const char* fmt, bool knobDisabled = false,
                        float dia = kKnobSmall, bool freqTaper = false)
      {
         if (!options.empty())
         {
            const float btnW = std::min(cellW - 8.0f, 112.0f);
            const float cx = x0 + (float)index * cellW + cellW * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(cx - btnW * 0.5f, y0));
            DiscreteParamRef ref = BeginDiscreteParam(dropId, (float)current, 0.0f, (float)options.size() - 1.0f);
            int safe = std::clamp(current, 0, (int)options.size() - 1);
            if (ref.modulated)
            {
               const int driven = std::clamp((int)lroundf(*ref.value), 0, (int)options.size() - 1);
               if (driven != safe)
               {
                  safe = driven;
                  gApplyingDiscreteCv = true;
                  onSelect(driven);
                  gApplyingDiscreteCv = false;
               }
               ImGui::BeginDisabled();
            }
            const std::string caption = options[safe] + "##" + dropId;
            bool ddClicked = false;
            {
               DesignDropdownFace face;
               ddClicked = ImGui::Button(caption.c_str(), ImVec2(std::max(24.0f, btnW - 18.0f), 0));
            }
            if (ddClicked)
            {
               PrepareDropdown(options, {}, safe, onSelect);
            }
            const bool hovered = ImGui::IsItemHovered(ref.modulated ? ImGuiHoveredFlags_AllowWhenDisabled
                                                                    : ImGuiHoveredFlags_None);
            if (ref.modulated)
               ImGui::EndDisabled();
            EndDiscreteParam(ref, hovered);
         }
         Place(dia);
         if (knobDisabled)
            ImGui::BeginDisabled();
         AudioWidgetStyle style = AudioWidgetStyle::Knob;
         if (freqTaper || (lo >= 1.0f && hi >= 1000.0f && (strstr(fmt, "Hz") != nullptr || strcmp(knobLabel, "freq") == 0 || strcmp(knobLabel, "cutoff") == 0 || strcmp(knobLabel, "filter") == 0)))
            style = AudioWidgetStyle::KnobFreq;
         ModKnob(knobLabel, v, lo, hi, fmt, dia, cellW, style);
         if (knobDisabled)
            ImGui::EndDisabled();
         index++;
      }

      // Turbo: toggle cell (ported for the upstream Analog body). Centred in
      // its cell at knob height; ModCheckbox keeps it modulatable.
      bool Checkbox(const char* label, bool* value)
      {
         if (value == nullptr)
         {
            index++;
            return false;
         }
         const float cellX0 = x0 + (float)index * cellW;
         const float checkY = y0 + headerH + (maxDia - ImGui::GetFrameHeight()) * 0.5f;
         const float textW = ImGui::CalcTextSize(label).x + ImGui::GetFrameHeight() + 4.0f;
         ImGui::SetCursorScreenPos(ImVec2(cellX0 + std::max(2.0f, (cellW - textW) * 0.5f), checkY));
         const bool changed = ModCheckbox(label, value);
         index++;
         return changed;
      }

      void Skip() { index++; }

      void End() const
      {
         ImGui::SetCursorScreenPos(ImVec2(x0, y0));
         ImGui::Dummy(ImVec2(gAudioContentW, rowH));
      }
   };

   // Full- and half-width audio sliders, sized from the content column so
   // two halves plus the gutter are exactly one full width.
   float AudioFullWidth() { return gAudioContentW; }
   float AudioHalfWidth() { return (gAudioContentW - ImGui::GetStyle().ItemSpacing.x) * 0.5f; }

   bool AudioSlider(const char* label, float* v, float lo, float hi, const char* fmt, float width)
   {
      return ModSlider(label, v, lo, hi, fmt, width, /*audioStyle=*/true);
   }

   bool AudioSliderInt(const char* label, int* v, int lo, int hi, float width)
   {
      return ModSliderInt(label, v, lo, hi, width, /*audioStyle=*/true);
   }

   // A small pill toggle for boolean mods (loop/reverse/ping-pong) - not
   // ModCheckbox, whose frame draws in the theme's FrameBg, and not a
   // plain ImGui::Button either. Pixel-sampling the render showed *both*
   // are only a handful of RGB values different from the node body
   // background (theme default, ~45,50,62 vs ~48,50,67) - visible on a wide
   // 90px "Load..." button by sheer surface area, imperceptible on a
   // narrow ~40px "loop" pill. Rather than lean on that subtle theme
   // contrast at all, this picks explicit, clearly-different fill colours
   // and adds a real border stroke - a guaranteed-visible backstop the way
   // the waveform box and slider fields already draw their own explicit
   // AddRect borders instead of trusting a themed background.
   bool AudioToggleButton(const char* label, bool* value, float width = 44.0f)
   {
      DiscreteParamRef ref = BeginDiscreteParam(label, *value ? 1.0f : 0.0f, 0.0f, 1.0f);
      bool changedByCv = false;
      if (ref.modulated)
      {
         const bool driven = *ref.value >= 0.5f;
         if (*value != driven)
         {
            *value = driven;
            changedByCv = true;
         }
         ImGui::BeginDisabled();
      }
      const bool isLight = IsThemeLight();
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Border, isLight ? IM_COL32(170, 178, 195, 255) : IM_COL32(130, 138, 160, 200));
      ImGui::PushStyleColor(ImGuiCol_Button, *value ? (isLight ? IM_COL32(55, 115, 235, 255) : IM_COL32(90, 115, 205, 255))
                                                    : (isLight ? IM_COL32(220, 225, 235, 255) : IM_COL32(58, 64, 82, 255)));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            *value ? (isLight ? IM_COL32(75, 135, 245, 255) : IM_COL32(108, 132, 220, 255))
                                   : (isLight ? IM_COL32(208, 214, 225, 255) : IM_COL32(76, 83, 104, 255)));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            *value ? (isLight ? IM_COL32(40, 95, 215, 255) : IM_COL32(120, 144, 230, 255))
                                   : (isLight ? IM_COL32(195, 202, 215, 255) : IM_COL32(88, 96, 118, 255)));
      ImGui::PushStyleColor(ImGuiCol_Text, *value ? IM_COL32(255, 255, 255, 255)
                                                  : (isLight ? IM_COL32(40, 45, 60, 255) : IM_COL32(210, 215, 230, 255)));
      const bool clicked = ImGui::Button(label, ImVec2(std::max(8.0f, width - 18.0f), 0));
      const bool hovered = ImGui::IsItemHovered(ref.modulated ? ImGuiHoveredFlags_AllowWhenDisabled
                                                              : ImGuiHoveredFlags_None);
      ImGui::PopStyleColor(5);
      ImGui::PopStyleVar();
      if (ref.modulated)
         ImGui::EndDisabled();
      EndDiscreteParam(ref, hovered);
      if (clicked)
         *value = !*value;
      return clicked || changedByCv;
   }

   // ---- audio node body (docs/plans/audio/audio-node-ui-system.md) -------
   // Whether `node` should draw through DrawAudioNodeBody rather than the
   // generic DrawPreview/DrawXxxParams path every visual node uses - mirrors
   // CanShowInViewportPanel's audio gate exactly, so the two never disagree
   // about what counts as an audio node.
   bool IsAudioBodyNode(INode* node)
   {
      // Note-only nodes (Note Sequencer: INoteSource, no audio pin at all;
      // Envelope: a note consumer with no audio pin either) draw through
      // this same body path - see audio-node-ui-system.md, extended for
      // P3a's note nodes. A node whose only input is a note pin would
      // otherwise fall through to DrawPreview and try to show a blank
      // GetOutputTexture()==0 image.
      // VibratoNode is a pin-less exception: a free-running modulator
      // (like LFONode) with no note/audio pin at all, drawn through this
      // same v3 body so it looks like the rest of the one-knob note-editor
      // family it ships alongside rather than dropping to the older
      // DrawXParams path every other pin-less modulator (LFO, Constant,
      // Random, ...) still uses. PitchBendNode used to need the same
      // carve-out (it was briefly a pin-less IModulator), but it's back to
      // being an INoteSource with a real NoteCable input, so it's covered by
      // the ordinary dynamic_cast<INoteSource*> check below like every other
      // note-chain node. EnvelopeNode needs the same carve-out since
      // 03-envelope-to-shaper.md: it moved from a note consumer to a
      // ModulatorInputSlot-only shaper, so it no longer has a note/audio pin
      // at slot 0 either, but it keeps its interactive-ADSR body rather than
      // falling back to the generic modulator meter. Any future pin-less
      // node that wants a bespoke body needs the same explicit addition.
      // AudioFileNode became an IAudioSource so its samples flow through the
      // DSP graph (see docs/plans/audio-file-into-dsp-graph.md), but it still
      // wants its own file-picker/transport/level body (DrawAudioFileParams)
      // rather than the generic v3 audio body, which has no case for it and
      // would otherwise render an empty shell with just the pin - same
      // reasoning as the AudioTextureNode carve-out just below.
      if (dynamic_cast<AudioTextureNode*>(node) != nullptr || dynamic_cast<AudioFileNode*>(node) != nullptr ||
          dynamic_cast<AudioColorRampNode*>(node) != nullptr || dynamic_cast<AudioAnalyzeNode*>(node) != nullptr ||
          dynamic_cast<VmpcNode*>(node) != nullptr ||
          dynamic_cast<ClipMatrixNode*>(node) != nullptr || dynamic_cast<ClipMatrixOutNode*>(node) != nullptr ||
          dynamic_cast<VideoSourceNode*>(node) != nullptr)
         return false;
      return dynamic_cast<IAudioSource*>(node) != nullptr || node->AudioInputSlot(0) != nullptr ||
             dynamic_cast<INoteSource*>(node) != nullptr || node->NoteInputSlot(0) != nullptr ||
             dynamic_cast<VibratoNode*>(node) != nullptr || dynamic_cast<EnvelopeNode*>(node) != nullptr ||
             dynamic_cast<TransportControlNode*>(node) != nullptr; // Turbo: pin-less, bespoke body
   }

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
               dl->PathLineTo(ImVec2(origin.x + t * w, midY - v * h * 0.44f));
            }
            const ImU32 strokeCol = isLight
               ? (pass == 0 ? IM_COL32(230, 140, 20, 50) : IM_COL32(210, 120, 10, 255))
               : (pass == 0 ? IM_COL32(255, 190, 80, 48) : IM_COL32(255, 215, 120, 240));
            dl->PathStroke(strokeCol, 0, pass == 0 ? 4.5f : 1.6f);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "strike / idle");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   // ======================================================================
   // Turbo 0.49: shared waveform view (peaks, play cursors, trim handles)
   // ======================================================================
   // Every sample node's waveform strip goes through these helpers so they
   // all read the same: WavePeaks (audio/WavePeaks.h) drawn one column per
   // real screen pixel, RMS brighter inside the min/max envelope, play
   // cursors with a small head, and trim handles with a time label while
   // dragged and Shift for fine adjustment.
   //
   // Zoom (the one solution for "node zoomed in / trim range tiny"): the
   // column-per-pixel drawing picks a finer peak level whenever the node
   // editor zooms in, and a small magnifier chip in the strip's bottom-right
   // corner toggles "zoom to trim": the view then spans the start..end range
   // plus a little margin on each side, so both handles stay grabbable and
   // the trimmed audio fills the strip. A thin bar along the bottom shows
   // where that view sits in the whole sample. While a handle is dragged the
   // view stays frozen (otherwise it would chase the handle); it re-fits on
   // release. The toggle is UI state only (ImGui storage), never saved.
   struct WaveView
   {
      ImVec2 o { 0.0f, 0.0f };
      ImVec2 br { 0.0f, 0.0f };
      float w = 1.0f;
      float h = 1.0f;
      float v0 = 0.0f; // visible fraction of the whole buffer
      float v1 = 1.0f;
      bool zoomed = false;
      bool canZoom = false;
      ImGuiID keyZoom = 0, keyV0 = 0, keyV1 = 0, keyDrag = 0;

      float Span() const { return std::max(1e-7f, v1 - v0); }
      float X(float f) const { return o.x + w * (f - v0) / Span(); }
      float Frac(float x) const { return v0 + (x - o.x) / w * Span(); }
      float MouseFrac() const { return std::clamp(Frac(ImGui::GetIO().MousePos.x), 0.0f, 1.0f); }
      bool Visible(float f) const { return f >= v0 - 1e-6f && f <= v1 + 1e-6f; }
   };

   // Pixels per canvas unit: node bodies draw in canvas space and the node
   // editor scales them, so a "one column per pixel" waveform needs the zoom.
   // ed::GetCurrentZoom() returns the view's InvScale (canvas units per pixel),
   // so the pixels per unit is its reciprocal.
   float WaveViewPixelScale()
   {
      const float inv = ed::GetCurrentZoom();
      if (!(inv > 0.0f) || !std::isfinite(inv))
         return 1.0f;
      return std::clamp(1.0f / inv, 0.1f, 8.0f);
   }

   // Sets up the mapping for one strip. `trimStart/trimEnd` < 0 = the node
   // has no trim range (no zoom chip). Call before the body click-catcher so
   // its click can use MouseFrac().
   WaveView WaveViewBegin(const char* id, ImVec2 origin, float w, float h, float trimStart, float trimEnd,
                          int numFrames)
   {
      WaveView v;
      v.o = origin;
      v.w = std::max(1.0f, w);
      v.h = h;
      v.br = ImVec2(origin.x + v.w, origin.y + h);
      v.canZoom = trimStart >= 0.0f && trimEnd > trimStart;
      ImGui::PushID(id);
      v.keyZoom = ImGui::GetID("zoom");
      v.keyV0 = ImGui::GetID("v0");
      v.keyV1 = ImGui::GetID("v1");
      v.keyDrag = ImGui::GetID("drag");
      ImGui::PopID();
      ImGuiStorage* st = ImGui::GetStateStorage();
      v.zoomed = v.canZoom && st->GetBool(v.keyZoom, false);
      if (v.zoomed)
      {
         if (st->GetBool(v.keyDrag, false))
         {
            v.v0 = st->GetFloat(v.keyV0, 0.0f);
            v.v1 = st->GetFloat(v.keyV1, 1.0f);
         }
         else
         {
            // At least ~64 frames visible, so a sample-accurate trim still
            // has some context around it.
            const float minSpan = numFrames > 0 ? std::min(1.0f, 64.0f / (float)numFrames) : 1e-4f;
            const float s = std::clamp(trimStart, 0.0f, 1.0f);
            const float e = std::clamp(trimEnd, s, 1.0f);
            const float pad = std::max((e - s) * 0.08f, minSpan * 0.5f);
            v.v0 = std::max(0.0f, s - pad);
            v.v1 = std::min(1.0f, e + pad);
            if (v.v1 - v.v0 < minSpan)
            {
               const float c = 0.5f * (v.v0 + v.v1);
               v.v0 = std::clamp(c - minSpan * 0.5f, 0.0f, 1.0f - minSpan);
               v.v1 = v.v0 + minSpan;
            }
            st->SetFloat(v.keyV0, v.v0);
            st->SetFloat(v.keyV1, v.v1);
         }
         if (!(v.v1 > v.v0))
         {
            v.v0 = 0.0f;
            v.v1 = 1.0f;
         }
      }
      return v;
   }

   // Brighter (dark theme) / deeper (light theme) shade of a waveform colour
   // for the RMS body.
   ImU32 WaveRmsColor(ImU32 base, bool isLight)
   {
      ImVec4 c = ImGui::ColorConvertU32ToFloat4(base);
      const float k = isLight ? -0.30f : 0.40f;
      auto mix = [k](float x) { return k >= 0.0f ? x + (1.0f - x) * k : x * (1.0f + k); };
      return ImGui::ColorConvertFloat4ToU32(ImVec4(mix(c.x), mix(c.y), mix(c.z), 1.0f));
   }

   // The waveform itself: min/max envelope in `col` (at reduced alpha), RMS
   // brighter on top, dimming outside [trimStart, trimEnd] (pass 0..1 for no
   // trim). Caller has drawn the background and pushed a clip rect.
   void DrawWavePeaks(ImDrawList* dl, const WaveView& v, const WavePeaks& peaks, float trimStart, float trimEnd,
                      ImU32 col, float amp = 0.45f)
   {
      const bool isLight = IsThemeLight();
      const float midY = v.o.y + v.h * 0.5f;
      dl->AddLine(ImVec2(v.o.x, midY), ImVec2(v.br.x, midY), ScopeMidLineCol(), 1.0f);
      if (!peaks.Empty())
      {
         const float scale = WaveViewPixelScale();
         const int columns = std::clamp((int)std::ceil(v.w * scale), 1, 8192);
         const float colW = v.w / (float)columns;
         const float span = v.Span();
         const int level = peaks.PickLevel(span, (float)columns);
         ImVec4 ec = ImGui::ColorConvertU32ToFloat4(col);
         ec.w *= 0.55f;
         const ImU32 envCol = ImGui::ColorConvertFloat4ToU32(ec);
         const ImU32 rmsCol = WaveRmsColor(col, isLight);
         const float minH = 1.0f / scale; // keep silence visible as a 1 px line
         for (int c = 0; c < columns; c++)
         {
            const float f0 = v.v0 + span * (float)c / (float)columns;
            const float f1 = v.v0 + span * (float)(c + 1) / (float)columns;
            float mn = 0.0f, mx = 0.0f, rms = 0.0f;
            if (!peaks.Range(f0, f1, level, mn, mx, rms))
               break;
            const float x0 = v.o.x + colW * (float)c;
            const float x1 = x0 + colW;
            float top = midY - mx * v.h * amp;
            float bot = midY - mn * v.h * amp;
            if (bot - top < minH)
            {
               top = midY - minH * 0.5f;
               bot = midY + minH * 0.5f;
            }
            dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, bot), envCol);
            const float r = std::min(rms, std::max(mx, -mn)) * v.h * amp;
            if (r * scale >= 0.5f)
               dl->AddRectFilled(ImVec2(x0, midY - r), ImVec2(x1, midY + r), rmsCol);
         }
      }

      const ImU32 dimCol = isLight ? IM_COL32(255, 255, 255, 140) : IM_COL32(0, 0, 0, 130);
      const float sx = std::clamp(v.X(std::clamp(trimStart, 0.0f, 1.0f)), v.o.x, v.br.x);
      const float ex = std::clamp(v.X(std::clamp(trimEnd, 0.0f, 1.0f)), v.o.x, v.br.x);
      if (sx > v.o.x)
         dl->AddRectFilled(v.o, ImVec2(sx, v.br.y), dimCol);
      if (ex < v.br.x)
         dl->AddRectFilled(ImVec2(ex, v.o.y), v.br, dimCol);

      // Zoomed: where the visible window sits in the whole sample.
      if (v.zoomed)
      {
         const float y0 = v.br.y - 3.0f;
         dl->AddRectFilled(ImVec2(v.o.x, y0), v.br, isLight ? IM_COL32(0, 0, 0, 30) : IM_COL32(255, 255, 255, 26));
         dl->AddRectFilled(ImVec2(v.o.x + v.w * v.v0, y0), ImVec2(v.o.x + v.w * std::max(v.v1, v.v0 + 0.004f), v.br.y),
                           isLight ? IM_COL32(230, 140, 20, 220) : IM_COL32(255, 200, 90, 200));
      }
   }

   ImU32 PlayCursorColor(int alpha = 255)
   {
      alpha = std::clamp(alpha, 0, 255);
      return IsThemeLight() ? IM_COL32(230, 120, 0, alpha) : IM_COL32(255, 226, 120, alpha);
   }

   // One play cursor: a thin bright line with a small downward triangle head.
   void DrawPlayCursor(ImDrawList* dl, const WaveView& v, float frac, ImU32 col, float thickness = 1.5f)
   {
      if (frac < 0.0f || !v.Visible(frac))
         return;
      const float x = v.X(frac);
      dl->AddLine(ImVec2(x, v.o.y), ImVec2(x, v.br.y), col, thickness);
      const float head = 5.0f;
      dl->AddTriangleFilled(ImVec2(x - head, v.o.y), ImVec2(x + head, v.o.y), ImVec2(x, v.o.y + head * 1.2f), col);
   }

   // "1.234 s" below a minute, "1:02.345" above; a percentage when the
   // sample rate is unknown.
   void FormatTrimTime(char* buf, size_t size, float frac, const WavePeaks* peaks)
   {
      const double secs = peaks != nullptr ? peaks->Seconds() : 0.0;
      if (secs <= 0.0)
      {
         snprintf(buf, size, "%.2f%%", frac * 100.0f);
         return;
      }
      const double t = std::clamp((double)frac, 0.0, 1.0) * secs;
      if (t < 60.0)
         snprintf(buf, size, "%.3f s", t);
      else
         snprintf(buf, size, "%d:%06.3f", (int)(t / 60.0), std::fmod(t, 60.0));
   }

   // Small boxed label next to a handle (flips to its left near the right
   // edge), drawn inside the strip.
   void DrawTrimTimeLabel(ImDrawList* dl, const WaveView& v, float frac, const WavePeaks* peaks, ImU32 col)
   {
      char buf[32];
      FormatTrimTime(buf, sizeof(buf), frac, peaks);
      const ImVec2 ts = ImGui::CalcTextSize(buf);
      const float x = v.X(frac);
      float lx = x + 5.0f;
      if (lx + ts.x + 4.0f > v.br.x)
         lx = x - 5.0f - ts.x - 4.0f;
      lx = std::clamp(lx, v.o.x + 1.0f, std::max(v.o.x + 1.0f, v.br.x - ts.x - 5.0f));
      const float ly = v.o.y + std::max(2.0f, v.h * 0.5f - ts.y - 4.0f);
      dl->AddRectFilled(ImVec2(lx - 2.0f, ly - 1.0f), ImVec2(lx + ts.x + 2.0f, ly + ts.y + 1.0f),
                        IsThemeLight() ? IM_COL32(255, 255, 255, 225) : IM_COL32(12, 14, 20, 225), 3.0f);
      dl->AddRect(ImVec2(lx - 2.0f, ly - 1.0f), ImVec2(lx + ts.x + 2.0f, ly + ts.y + 1.0f), col, 3.0f);
      dl->AddText(ImVec2(lx, ly), ScopeTextCol(), buf);
   }

   // Relative drag for a trim value: no jump on grab (unless `jumpToMouse`),
   // one view-pixel per pixel, a tenth of that with Shift. Re-anchors when
   // Shift changes so toggling it mid-drag never jumps.
   struct TrimDragState
   {
      ImGuiID owner = 0;
      float anchorVal = 0.0f;
      float anchorX = 0.0f;
      bool fine = false;
   };
   TrimDragState gTrimDrag;

   float TrimDragValue(ImGuiID item, const WaveView& v, float current, bool activated, bool jumpToMouse)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const bool fine = io.KeyShift;
      if (activated || gTrimDrag.owner != item || fine != gTrimDrag.fine)
      {
         gTrimDrag.owner = item;
         gTrimDrag.anchorVal = (activated && jumpToMouse) ? v.MouseFrac() : current;
         gTrimDrag.anchorX = io.MousePos.x;
         gTrimDrag.fine = fine;
      }
      const float perPx = v.Span() / v.w;
      return gTrimDrag.anchorVal + (io.MousePos.x - gTrimDrag.anchorX) * perPx * (fine ? 0.1f : 1.0f);
   }

   // Magnifier chip, bottom-right: toggles zoom-to-trim. Submitted before
   // the handles so it keeps the click where they overlap.
   void WaveZoomChip(WaveView& v)
   {
      if (!v.canZoom || v.w < 60.0f || v.h < 24.0f)
         return;
      const ImVec2 sz(18.0f, 13.0f);
      const ImVec2 p(v.br.x - 12.0f - sz.x, v.br.y - sz.y - 5.0f);
      ImGui::SetCursorScreenPos(p);
      ImGui::InvisibleButton("##wavezoom", sz);
      const bool hov = ImGui::IsItemHovered();
      if (ImGui::IsItemClicked())
      {
         v.zoomed = !v.zoomed;
         ImGui::GetStateStorage()->SetBool(v.keyZoom, v.zoomed);
      }
      if (hov)
         SetAudioReadout("#", v.zoomed ? "zoomed to the trim range - click for the whole sample"
                                       : "zoom the waveform to the trim range");
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const ImU32 bg = v.zoomed ? (isLight ? IM_COL32(230, 140, 20, 230) : IM_COL32(255, 200, 90, 220))
                                : (isLight ? IM_COL32(255, 255, 255, hov ? 230 : 170) : IM_COL32(20, 22, 30, hov ? 235 : 175));
      const ImU32 fg = v.zoomed ? IM_COL32(20, 20, 24, 255) : ScopeTextCol();
      dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), bg, 3.0f);
      dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), ScopeBorderCol(), 3.0f);
      const ImVec2 c(p.x + 7.5f, p.y + 5.8f);
      dl->AddCircle(c, 3.6f, fg, 12, 1.3f);
      dl->AddLine(ImVec2(c.x + 2.6f, c.y + 2.6f), ImVec2(c.x + 6.5f, c.y + 6.0f), fg, 1.6f);
   }

   // Start/end trim handles over a WaveView: lines, triangle grips,
   // grab-zones (submitted after the body button, see DrawSamplerWaveform's
   // overlap note), relative drag with Shift = fine, a time label while
   // dragging and the value in the readout strip. Pushes an undo checkpoint
   // on grab. Returns 1 / 2 while the start / end handle is held, else 0.
   // A handle scrolled out of a zoomed view is not drawn (its button stays
   // alive only while it is being dragged).
   int WaveTrimHandles(WaveView& v, const char* id, float* start, float* end, float minGap,
                       const WavePeaks* peaks, ImU32 startCol, ImU32 endCol)
   {
      WaveZoomChip(v);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImGui::PushID(id);
      int held = 0;
      for (int which = 0; which < 2; which++)
      {
         float* val = which == 0 ? start : end;
         const ImU32 col = which == 0 ? startCol : endCol;
         const float f = std::clamp(*val, 0.0f, 1.0f);
         const char* btnId = which == 0 ? "##trimstart" : "##trimend";
         // Turbo 0.49: the handle being dragged keeps its button even when it
         // leaves a zoomed view (x clamped into the view), otherwise the drag
         // would drop the moment it scrolls out; only its drawing is skipped.
         const bool dragging = ImGui::GetActiveID() != 0 && ImGui::GetActiveID() == ImGui::GetID(btnId);
         const bool visible = v.Visible(f);
         if (!visible && !dragging)
            continue;
         const float x = std::clamp(v.X(f), v.o.x, v.br.x);
         if (visible)
         {
            dl->AddLine(ImVec2(x, v.o.y), ImVec2(x, v.br.y), col, 2.0f);
            const float grip = 8.0f;
            const float gx = std::clamp(x, v.o.x + grip * 0.5f, v.br.x - grip * 0.5f);
            dl->AddTriangleFilled(ImVec2(gx - grip * 0.5f, v.o.y), ImVec2(gx + grip * 0.5f, v.o.y),
                                  ImVec2(gx, v.o.y + grip), col);
            dl->AddTriangleFilled(ImVec2(gx - grip * 0.5f, v.br.y), ImVec2(gx + grip * 0.5f, v.br.y),
                                  ImVec2(gx, v.br.y - grip), col);
         }

         const float handleW = 10.0f;
         const float bx = std::clamp(x - handleW * 0.5f, v.o.x, v.br.x - handleW);
         ImGui::SetCursorScreenPos(ImVec2(bx, v.o.y));
         ImGui::InvisibleButton(btnId, ImVec2(handleW, v.h));
         const bool activated = ImGui::IsItemActivated();
         if (activated)
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
         {
            const float raw = TrimDragValue(ImGui::GetItemID(), v, *val, activated, false);
            if (which == 0)
               *start = std::clamp(raw, 0.0f, std::max(0.0f, *end - minGap));
            else
               *end = std::clamp(raw, std::min(1.0f, *start + minGap), 1.0f);
            held = which + 1;
         }
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            char t0[32], t1[32], line[96];
            FormatTrimTime(t0, sizeof(t0), *start, peaks);
            FormatTrimTime(t1, sizeof(t1), *end, peaks);
            snprintf(line, sizeof(line), "%s %s - range %s..%s - shift: fine", which == 0 ? "start" : "end",
                     which == 0 ? t0 : t1, t0, t1);
            SetAudioReadout("#", line);
         }
      }
      ImGui::PopID();
      if (held != 0)
         DrawTrimTimeLabel(dl, v, held == 1 ? *start : *end, peaks, held == 1 ? startCol : endCol);
      ImGui::GetStateStorage()->SetBool(v.keyDrag, held != 0);
      return held;
   }

   // Default trim colours (the existing green start / red end pair).
   ImU32 TrimStartCol() { return IsThemeLight() ? IM_COL32(20, 160, 60, 255) : IM_COL32(120, 220, 150, 255); }
   ImU32 TrimEndCol() { return IsThemeLight() ? IM_COL32(220, 40, 40, 255) : IM_COL32(220, 120, 150, 255); }
   // ======================================================================

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
