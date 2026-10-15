// Part of src/main.cpp, included there in order (one translation unit).
// sampler, oscillators, wavetable, synths, meters, gain

   void DrawSamplerWaveform(SamplerNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;
      // Turbo 0.49: shared view (zoom-to-trim, per-pixel peaks, cursors).
      WaveView view = WaveViewBegin("##samplerwave", origin, w, h, n->start, n->end, n->peaks.NumFrames());

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
      if (hasSample && ImGui::IsItemActivated())
      {
         const float frac = view.MouseFrac();
         // A click outside the start/end range starts from the range's
         // start rather than from the click point - clicking past the red
         // marker used to trigger a voice already past its own end bound,
         // which either played silently for a frame or two before stopping
         // (reads as "unresponsive") or, for a click left of the green
         // marker in reverse/pingpong, played audibly outside the range.
         const float target = (frac < n->start || frac > n->end) ? n->start : frac;
         n->TriggerPreview(target);
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      if (hasSample)
      {
         DrawWavePeaks(dl, view, n->peaks, n->start, n->end,
                       isLight ? IM_COL32(30, 110, 230, 230) : IM_COL32(150, 214, 255, 220));

         // Turbo 0.49: one cursor per sounding voice (self lane and every
         // note voice), not just the most recent one.
         float pos[SamplerNode::kMaxVoicePositions + 1]; // note voices + the self voice
         const int count = n->VoicePositions(pos, SamplerNode::kMaxVoicePositions + 1);
         for (int i = 0; i < count; i++)
            DrawPlayCursor(dl, view, pos[i], PlayCursorColor());
      }
      else
      {
         const float midY = origin.y + h * 0.5f;
         dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "no sample loaded");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      // Triangle grips at top and bottom of each marker, anchored a few
      // pixels in from the true edge so they stay visible/grabbable even at
      // the default start=0/end=1 (see WaveTrimHandles).
      if (hasSample)
         WaveTrimHandles(view, "##samplertrim", &n->start, &n->end, std::max(0.001f, 0.01f * view.Span()), &n->peaks, TrimStartCol(),
                         TrimEndCol());

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }

   // Waveform and playhead renderer for PaulStretch extreme time-stretcher
   // (Turbo 0.49: shared WaveView helpers).
   void DrawPaulStretchWaveform(PaulStretchNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;
      WaveView view = WaveViewBegin("##paulstretchwave", origin, w, h, n->start, n->end, n->peaks.NumFrames());

      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##paulstretchwavebody", ImVec2(w, h));
      if (hasSample && (ImGui::IsItemActivated() || (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))))
      {
         const float frac = view.MouseFrac();
         const float target = (frac < n->start || frac > n->end) ? n->start : frac;
         n->position = target;
         n->Seek(target);
         if (!n->IsPlaying())
            n->TriggerPreview(target);
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      if (hasSample)
      {
         DrawWavePeaks(dl, view, n->peaks, n->start, n->end,
                       isLight ? IM_COL32(40, 100, 230, 230) : IM_COL32(165, 180, 255, 225));
         DrawPlayCursor(dl, view, std::clamp(n->Playhead(), 0.0f, 1.0f), PlayCursorColor(n->IsPlaying() ? 255 : 120), 2.0f);
      }
      else
      {
         const float midY = origin.y + h * 0.5f;
         dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "no sample loaded");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample && WaveTrimHandles(view, "##paulstretchtrim", &n->start, &n->end,
                                       std::max(0.001f, 0.01f * view.Span()), &n->peaks, TrimStartCol(),
                                       TrimEndCol()) != 0)
         n->position = std::clamp(n->position, n->start, n->end);

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }

   // Waveform and real-time grain particle renderer for Granular synthesis node
   // (Turbo 0.49: shared WaveView helpers).
   void DrawGranularWaveform(GranularNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;
      WaveView view = WaveViewBegin("##granularwave", origin, w, h, n->start, n->end, n->peaks.NumFrames());

      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##granularwavebody", ImVec2(w, h));
      if (hasSample && (ImGui::IsItemActivated() || (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))))
         n->Seek(view.MouseFrac());

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      if (hasSample)
      {
         DrawWavePeaks(dl, view, n->peaks, n->start, n->end,
                       isLight ? IM_COL32(40, 90, 200, 220) : IM_COL32(140, 160, 220, 200), 0.44f);

         // Render active real-time grain particles/dots
         const auto& snap = n->VisualSnapshot();
         for (int g = 0; g < snap.count; ++g)
         {
            const auto& gr = snap.grains[g];
            if (gr.amp < 0.01f || !view.Visible(gr.position))
               continue;

            const float gx = view.X(std::clamp(gr.position, 0.0f, 1.0f));
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

         // Current playing playhead (pos), with a grip at the bottom too
         // (it is also the scrub target).
         const float ph = std::clamp(n->Playhead(), 0.0f, 1.0f);
         DrawPlayCursor(dl, view, ph, PlayCursorColor(), 2.0f);
         if (view.Visible(ph))
         {
            const float px = view.X(ph);
            const float pGrip = 8.0f;
            dl->AddTriangleFilled(ImVec2(px - pGrip * 0.5f, br.y), ImVec2(px + pGrip * 0.5f, br.y), ImVec2(px, br.y - pGrip),
                                  PlayCursorColor());
         }
      }
      else
      {
         dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "no sample loaded");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample)
         WaveTrimHandles(view, "##granulartrim", &n->start, &n->end, std::max(0.001f, 0.01f * view.Span()), &n->peaks,
                         TrimStartCol(), TrimEndCol());

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
      dl->AddRectFilled(tl, br, ScopeBgCol(), 2.0f);

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
         const ImU32 col = db > -1.0f   ? IM_COL32(255, 96, 86, 245)
                           : db > -6.0f ? IM_COL32(255, 190, 90, 240)
                                        : IM_COL32(120, 210, 160, 235);
         dl->AddRectFilled(ImVec2(x + 1.0f, br.y - 1.0f - (h - 2.0f) * lvl),
                           ImVec2(br.x - 1.0f, br.y - 1.0f), col, 1.0f);
      }
      dl->AddRect(tl, br, ScopeBorderCol(), 2.0f);
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
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
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
            ? (isCurrent ? IM_COL32(20, 100, 230, 255) : IM_COL32(70, 115, 175, alpha))
            : (isCurrent ? IM_COL32(150, 214, 255, 255) : IM_COL32(96, 150, 205, alpha));
         dl->PathStroke(Fade(frameStroke), 0, isCurrent ? 2.2f : 1.0f);
      }

      // Scrub handle along the bottom: without it the display looks like a
      // picture, and nothing says it can be dragged.
      const float handleX = origin.x + 10.0f + std::clamp(eng.position, 0.0f, 1.0f) * (w - 20.0f);
      dl->AddLine(ImVec2(handleX, origin.y + 3.0f), ImVec2(handleX, br.y - 3.0f),
                  isLight ? IM_COL32(0, 0, 0, hovered ? 120 : 60) : IM_COL32(255, 255, 255, hovered ? 90 : 42), 1.0f);
      const ImU32 triCol = isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(190, 224, 255, 255);
      dl->AddTriangleFilled(ImVec2(handleX - 5.0f, br.y - 2.0f), ImVec2(handleX + 5.0f, br.y - 2.0f),
                            ImVec2(handleX, br.y - 9.0f), Fade(triCol));

      dl->PopClipRect();
      dl->AddRect(origin, br, hovered ? IM_COL32(110, 140, 180, 255) : ScopeBorderCol(), 4.0f);
      dl->AddText(ImVec2(origin.x + 9.0f, origin.y + 5.0f),
                  Fade(isLight ? IM_COL32(40, 48, 65, 255) : IM_COL32(160, 172, 196, 255)),
                  Wavetable::TableName(eng.table));
      char posText[24];
      snprintf(posText, sizeof(posText), "%.3f", eng.position);
      const ImVec2 sz = ImGui::CalcTextSize(posText);
      dl->AddText(ImVec2(br.x - 9.0f - sz.x, origin.y + 5.0f),
                  isLight ? IM_COL32(60, 68, 85, 255) : IM_COL32(140, 150, 172, 255), posText);
      if (hovered)
         SetAudioReadout("position", posText);
   }

   // Directly editable ADSR: four draggable handles on the curve itself.
   // Attack and decay are the x of their own corner, sustain the y of the
   // sustain shelf, release the x of the tail - which is the mapping every
   // envelope editor uses, so it needs no explanation to anyone who has seen
   // ADSR Layout Geometry: computes relative, adaptive timebase points
   // so envelopes always span across the visualizer width with proper visual weight
   struct ADSRLayout
   {
      ImVec2 p0; // start (0, 0)
      ImVec2 pA; // attack peak (Ax, 1.0)
      ImVec2 pD; // decay end / sustain start (Dx, S)
      ImVec2 pS; // sustain end / release start (Sx, S)
      ImVec2 pR; // release end (Rx, 0.0)
      float x0, topY, baseY, spanY;
      float wA, wD, wShelf, wR;
   };

   inline ADSRLayout ComputeADSRLayout(ImVec2 origin, float w, float h, float attackMs, float decayMs,
                                       float sustain, float releaseMs, float maxTimeMs = 4000.0f)
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

      // Perceptual duration power curve (t^0.45): provides balanced visual proportion across 0..4000ms
      auto PowWeight = [&](float ms) {
         const float t01 = std::clamp(ms / maxTimeMs, 0.0f, 1.0f);
         return std::pow(t01, 0.45f);
      };

      const float wA_raw = PowWeight(attackMs) + 0.08f;
      const float wD_raw = PowWeight(decayMs) + 0.08f;
      const float wR_raw = PowWeight(releaseMs) + 0.08f;
      const float totalW = wA_raw + wD_raw + wR_raw;

      l.wA = (wA_raw / totalW) * timeW;
      l.wD = (wD_raw / totalW) * timeW;
      l.wR = (wR_raw / totalW) * timeW;

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
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
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
                        isLight ? IM_COL32(0, 0, 0, 8) : IM_COL32(255, 255, 255, 6));

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
            float best = d0; sHeld = 0;
            if (d1 < best) { best = d1; sHeld = 1; }
            if (d3 < best) { best = d3; sHeld = 3; }
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
            curWeight = std::clamp(curWeight + deltaX / (w * 0.45f), 0.0f, 1.0f);
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

      // Build smooth multi-segment curve points (Attack rise, Exponential Decay, Sustain shelf, Exponential Release)
      std::vector<ImVec2> curvePts;
      curvePts.reserve(36);

      // 1. Attack curve (smooth rise)
      const int kAttSteps = 6;
      for (int i = 0; i <= kAttSteps; i++)
      {
         const float u = (float)i / (float)kAttSteps;
         const float px = layout.x0 + u * (layout.pA.x - layout.x0);
         const float py = layout.baseY - std::pow(u, 0.90f) * layout.spanY;
         curvePts.push_back(ImVec2(px, py));
      }

      // 2. Decay curve (analog exponential decay)
      const int kDecSteps = 10;
      for (int i = 1; i <= kDecSteps; i++)
      {
         const float u = (float)i / (float)kDecSteps;
         const float px = layout.pA.x + u * (layout.pD.x - layout.pA.x);
         const float expFactor = (1.0f - std::exp(-2.8f * u)) / (1.0f - std::exp(-2.8f));
         const float py = layout.topY + expFactor * (layout.pD.y - layout.topY);
         curvePts.push_back(ImVec2(px, py));
      }

      // 3. Sustain shelf
      curvePts.push_back(layout.pS);

      // 4. Release curve (analog exponential decay to zero)
      const int kRelSteps = 10;
      for (int i = 1; i <= kRelSteps; i++)
      {
         const float u = (float)i / (float)kRelSteps;
         const float px = layout.pS.x + u * (layout.pR.x - layout.pS.x);
         const float expFactor = (1.0f - std::exp(-2.8f * u)) / (1.0f - std::exp(-2.8f));
         const float py = layout.pS.y + expFactor * (layout.baseY - layout.pS.y);
         curvePts.push_back(ImVec2(px, py));
      }

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

         dl->AddCircleFilled(handlePts[i], radius, isLight ? IM_COL32(255, 255, 255, 255) : IM_COL32(240, 248, 255, 255), 14);
         dl->AddCircle(handlePts[i], radius, isLight ? IM_COL32(40, 48, 65, 255) : IM_COL32(20, 24, 34, 230), 14, 1.4f);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, (hovered || active) ? IM_COL32(110, 140, 180, 255) : ScopeBorderCol(), 4.0f);

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
                          const std::vector<std::string>& categories = {}, bool keepOrder = false)
   {
      if (options.empty())
         return;
      DiscreteParamRef ref = BeginDiscreteParam(id, (float)current, 0.0f, (float)options.size() - 1.0f);
      int safe = std::max(0, std::min(current, (int)options.size() - 1));
      if (ref.valid && ref.modulated)
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
      const std::string caption = options[safe] + "##" + id;
      bool ddClicked = false;
      {
         DesignDropdownFace face;
         ddClicked = ImGui::Button(caption.c_str(), ImVec2(std::max(24.0f, width - 18.0f), 0));
      }
      if (ddClicked)
      {
         PrepareDropdown(options, categories, safe, std::move(onSelect), keepOrder);
      }
      const bool hovered = ImGui::IsItemHovered(ref.valid && ref.modulated
                                                   ? ImGuiHoveredFlags_AllowWhenDisabled
                                                   : ImGuiHoveredFlags_None);
      if (ref.valid && ref.modulated)
         ImGui::EndDisabled();
      EndDiscreteParam(ref, hovered);
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
      const float fieldsW = std::clamp(full * 0.58f, 230.0f, 300.0f);
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
      AudioSlider("attack", attackMs, 0.0f, 4000.0f, "%.0f ms", halfW);
      ImGui::SameLine();
      AudioSlider("decay", decayMs, 0.0f, 4000.0f, "%.0f ms", halfW);
      y += rowH;
      ImGui::SetCursorScreenPos(ImVec2(fieldsX, y));
      AudioSlider("sustain", sustain, 0.0f, 1.0f, "%.3f", halfW);
      ImGui::SameLine();
      AudioSlider("release", releaseMs, 0.0f, 4000.0f, "%.0f ms", halfW);

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
         const float boxW = 20.0f;
         const float stepBtnW = 16.0f;
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float octW = 74.0f, semiW = 82.0f, fineW = 104.0f;
         const float tableW = std::max(60.0f, w - boxW - (stepBtnW * 2.0f + 4.0f) - octW - semiW - fineW - gap * 4.0f);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         bool on = eng.on;
         if (ModCheckbox("##wtOn", &on))
         {
            PushUndoCheckpoint();
            eng.on = on;
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout(e == 0 ? "engine a" : "engine b", eng.on ? "on" : "off");

         float curX = x0 + boxW + gap;
         ImGui::SetCursorScreenPos(ImVec2(curX, y));
         char prevId[32], nextId[32];
         snprintf(prevId, sizeof(prevId), "<##wtPrev%d", e);
         snprintf(nextId, sizeof(nextId), ">##wtNext%d", e);
         if (ImGui::Button(prevId, ImVec2(stepBtnW, 0)))
         {
            PushUndoCheckpoint();
            eng.table = (eng.table - 1 + Wavetable::NumTables()) % Wavetable::NumTables();
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout("wavetable", "previous table");

         curX += stepBtnW + 2.0f;
         ImGui::SetCursorScreenPos(ImVec2(curX, y));
         AudioBareDropdown("wtTable", WavetableNames(), eng.table,
                           [&eng](int i) { PushUndoCheckpoint(); eng.table = i; }, tableW,
                           WavetableCategories());
         if (ImGui::IsItemHovered())
         {
            char buf[64];
            snprintf(buf, sizeof(buf), "[%s] %s", Wavetable::TableCategoryName(eng.table),
                     Wavetable::TableName(eng.table));
            SetAudioReadout("wavetable", buf);
         }

         curX += tableW + 2.0f;
         ImGui::SetCursorScreenPos(ImVec2(curX, y));
         if (ImGui::Button(nextId, ImVec2(stepBtnW, 0)))
         {
            PushUndoCheckpoint();
            eng.table = (eng.table + 1) % Wavetable::NumTables();
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout("wavetable", "next table");

         ImGui::SetCursorScreenPos(ImVec2(x0 + boxW + gap + (stepBtnW * 2.0f + 4.0f) + tableW + gap, y));
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
                        IM_COL32(150, 214, 255, 245));
      ImGui::PopID();

      ImGui::PushID("pitch");
      DrawEnvelopePanel("pitch envelope  -  drag the handles", "##wtPitchEnv", &eng.pitchAttack,
                        &eng.pitchDecay, &eng.pitchSustain, &eng.pitchRelease, &eng.pitchAmount,
                        -48.0f, 48.0f, "%.1f st", IM_COL32(255, 190, 120, 235));
      ImGui::PopID();

      ImGui::PushID("filter");
      DrawEnvelopePanel("filter envelope  -  drag the handles", "##wtFiltEnv", &eng.filterAttack,
                        &eng.filterDecay, &eng.filterSustain, &eng.filterRelease, &eng.filterAmount,
                        -8.0f, 8.0f, "%.2f oct", IM_COL32(150, 230, 180, 235));
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
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s");
         // This knob is the manual/global bend - a Pitch Bend node wired
         // into the note chain upstream doesn't touch it at all, it rides
         // in on each note's own bendSemitones field instead (see
         // NoteEvent.h) and adds additively with whatever this knob is set
         // to. Drag this one directly for a bend with nothing patched.
         row.Knob("bend", &n->pitchBend, -2.0f, 2.0f, "%+.2f st");
         // fmMode picks phase modulation (cheap, always-stable sideband FM)
         // vs exponential FM (pitch-tracking, can self-modulate harder) - the
         // depth knob's meaning depends on it, so they read as one control.
         row.DropdownKnob("wtFmMode", { "pm", "fm" }, n->fmMode,
                          [n](int i) { PushUndoCheckpoint(); n->fmMode = i; },
                          "fm depth", &n->fmDepth, 0.0f, 2.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }

   const std::vector<std::string>& OscillatorWaveformNames()
   {
      static const std::vector<std::string> names = { "sine", "triangle", "saw", "square" };
      return names;
   }

   void DrawOscillatorWaveform(int waveform, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
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
         for (int c = 0; c < 2; c++)
         {
            const float x0 = origin.x + c * cycleW;
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
         for (int c = 0; c < 2; c++)
         {
            const float x0 = origin.x + c * cycleW;
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
         for (int c = 0; c < 2; c++)
         {
            const float x0 = origin.x + c * cycleW;
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
            const float y = sinf(2.0f * (float)M_PI * cycles * t);
            pts.push_back(ImVec2(origin.x + t * w, midY - y * amp));
         }
      }

      // Glow path
      dl->PathClear();
      for (const auto& p : pts)
         dl->PathLineTo(p);
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 45) : IM_COL32(100, 190, 255, 50), 0, 4.5f);

      // Core trace
      dl->PathClear();
      for (const auto& p : pts)
         dl->PathLineTo(p);
      dl->PathStroke(isLight ? IM_COL32(20, 100, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
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

      gWtTestRects.clear();
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      WavetableEngine& eng = n->engine;

      // Header: waveform dropdown, fine tuning, octave, semitone.
      {
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 5.0f;
         const float octW = 74.0f, semiW = 82.0f, fineW = 104.0f;
         const float waveW = std::max(70.0f, w - octW - semiW - fineW - gap * 3.0f);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("oscWave", OscillatorWaveformNames(), n->waveform,
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
      DrawOscillatorWaveform(n->waveform, 100.0f, gAudioContentW);
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
                        IM_COL32(150, 214, 255, 245));
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
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s");
         row.Knob("bend", &n->pitchBend, -2.0f, 2.0f, "%+.2f st");
         // fmMode picks phase modulation vs exponential FM - see the
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
         const float octW = 74.0f, semiW = 82.0f, fineW = 104.0f;
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
         if (ModTriggerButton("Strike##physicalModelStrike", ImVec2(strikeBtnW, scopeH)))
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
         row.Knob("glide", &n->glide, 0.0f, 1.0f, "%.2f s");
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
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
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
               dl->PathStroke(isLight ? IM_COL32(0, 160, 200, 45) : IM_COL32(0, 220, 255, 45), 0, 4.0f);
            else
               dl->PathStroke(isLight ? IM_COL32(0, 140, 210, 240) : IM_COL32(50, 240, 255, 240), 0, 1.6f);
         }
      }
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
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
         const float octW = 60.0f, semiW = 68.0f, fineW = 84.0f;
         const float remW = std::max(120.0f, w - octW - semiW - fineW - gap * 4.0f);
         const float orbW = remW * 0.55f;
         const float chanW = remW * 0.45f;

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("wtOrbit", WaveTerrainNode::OrbitTypeNames(), n->orbitType,
                           [n](int i) { PushUndoCheckpoint(); n->orbitType = i; }, orbW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + orbW + gap, y));
         AudioBareDropdown("wtChan", WaveTerrainNode::ChannelModeNames(), n->channel,
                           [n](int i) { PushUndoCheckpoint(); n->channel = i; }, chanW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + orbW + chanW + gap * 2.0f, y));
         AudioSlider("fine", &n->fine, -50.0f, 50.0f, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("wtOct", OctaveNames(), n->octave + 4,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i - 4; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("wtSemi", SemiNames(), n->semi + 12,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i - 12; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
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

         dl->AddRectFilled(origin, containerBr, IM_COL32(14, 16, 20, 255), 4.0f);
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
            const ImU32 coreHi = isLight ? IM_COL32(0, 90, 150, 255) : IM_COL32(190, 250, 255, 255);
            const ImU32 core = prox > 0.97f ? coreHi : coreLo;

            dl->AddPolyline(pts, kPlotPoints, glow, 0, prox > 0.97f ? 3.5f : 2.0f);
            dl->AddPolyline(pts, kPlotPoints, core, 0, prox > 0.97f ? 1.6f : 1.0f);
         }

         dl->PopClipRect();
         dl->AddRect(origin, containerBr, IM_COL32(40, 44, 56, 180), 4.0f, 0, 1.0f);

         char label[64];
         snprintf(label, sizeof(label), "wavetable  -  frame %.2f / %d", n->position * (frameCount - 1), frameCount - 1);
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), IM_COL32(140, 150, 165, 200), label);

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
                        IM_COL32(50, 220, 255, 240));
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
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s");
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

      const ImU32 bgCol = isLight ? IM_COL32(245, 247, 250, 255) : IM_COL32(16, 18, 24, 255);
      const ImU32 borderCol = isLight ? IM_COL32(200, 205, 215, 255) : IM_COL32(40, 46, 60, 255);
      const ImU32 gridCol = isLight ? IM_COL32(215, 220, 230, 180) : IM_COL32(32, 38, 52, 180);
      const ImU32 axisCol = isLight ? IM_COL32(140, 150, 170, 240) : IM_COL32(70, 85, 115, 240);
      const ImU32 textCol = isLight ? IM_COL32(120, 130, 145, 200) : IM_COL32(130, 145, 170, 200);

      dl->AddRectFilled(origin, br, bgCol, 4.0f);
      dl->PushClipRect(origin, br, true);

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
         dl->AddText(ImVec2(origin.x + 4.0f, midY + 2.0f), textCol, "0");
         dl->AddText(ImVec2(br.x - 14.0f, midY + 2.0f), textCol, "1");
      }
      else if (n->domainMode == EquationDsp::kDomainNegPiToPi)
      {
         dl->AddText(ImVec2(origin.x + 4.0f, midY + 2.0f), textCol, "-pi");
         dl->AddText(ImVec2(originX + 3.0f, midY + 2.0f), textCol, "0");
         dl->AddText(ImVec2(br.x - 22.0f, midY + 2.0f), textCol, "+pi");
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 4.0f, midY + 2.0f), textCol, "-1");
         dl->AddText(ImVec2(originX + 3.0f, midY + 2.0f), textCol, "0");
         dl->AddText(ImVec2(br.x - 14.0f, midY + 2.0f), textCol, "+1");
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
         dl->PathStroke(isLight ? IM_COL32(0, 160, 220, 50) : IM_COL32(0, 240, 255, 60), 0, 3.5f);
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
         dl->PathStroke(isLight ? IM_COL32(0, 140, 240, 60) : IM_COL32(0, 200, 255, 75), 0, 4.0f);

         dl->PathClear();
         for (int i = 0; i < nPts; i++)
         {
            const float t = (float)i / (float)(nPts - 1);
            const float cy = midY - std::clamp(curve[i], -1.5f, 1.5f) * scaleY;
            dl->PathLineTo(ImVec2(origin.x + t * w, cy));
         }
         dl->PathStroke(isLight ? IM_COL32(0, 120, 220, 240) : IM_COL32(100, 240, 255, 250), 0, 1.8f);
      }

      if (!n->LastError().empty())
      {
         const std::string errText = "[!] " + n->LastError();
         const ImVec2 txtSz = ImGui::CalcTextSize(errText.c_str());
         const ImVec2 pillBr(br.x - 6.0f, origin.y + 6.0f + txtSz.y + 4.0f);
         const ImVec2 pillTl(pillBr.x - txtSz.x - 10.0f, origin.y + 6.0f);
         dl->AddRectFilled(pillTl, pillBr, IM_COL32(180, 40, 40, 230), 3.0f);
         dl->AddText(ImVec2(pillTl.x + 5.0f, pillTl.y + 2.0f), IM_COL32(255, 240, 240, 255), errText.c_str());
      }
      else
      {
         const char* tag = "y = f(x)";
         const ImVec2 txtSz = ImGui::CalcTextSize(tag);
         const ImVec2 pillBr(br.x - 6.0f, origin.y + 6.0f + txtSz.y + 4.0f);
         const ImVec2 pillTl(pillBr.x - txtSz.x - 8.0f, origin.y + 6.0f);
         dl->AddRectFilled(pillTl, pillBr, isLight ? IM_COL32(220, 230, 240, 200) : IM_COL32(28, 36, 50, 200), 3.0f);
         dl->AddText(ImVec2(pillTl.x + 4.0f, pillTl.y + 2.0f), isLight ? IM_COL32(0, 120, 200, 240) : IM_COL32(80, 210, 255, 240), tag);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, borderCol, 4.0f);
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
         const float octW = 56.0f, semiW = 64.0f, fineW = 76.0f;
         const float remW = std::max(140.0f, w - octW - semiW - fineW - gap * 4.0f);
         const float preW = remW * 0.58f;
         const float domW = remW * 0.42f;

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("eqPreset", EquationNode::PresetNames(), n->presetIndex,
                           [n](int i) { PushUndoCheckpoint(); n->LoadPreset(i); }, preW,
                           EquationNode::PresetCategories(), true); // Turbo 0.50: grouped, file order

         ImGui::SetCursorScreenPos(ImVec2(x0 + preW + gap, y));
         AudioBareDropdown("eqDom", EquationNode::DomainNames(), n->domainMode,
                           [n](int i) { PushUndoCheckpoint(); n->domainMode = i; n->CompileEquation(); }, domW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + preW + domW + gap * 2.0f, y));
         AudioSlider("fine", &n->fine, -50.0f, 50.0f, "%.1f c", fineW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown("eqOct", OctaveNames(), n->octave + 4,
                           [n](int i) { PushUndoCheckpoint(); n->octave = i - 4; }, octW);

         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown("eqSemi", SemiNames(), n->semi + 12,
                           [n](int i) { PushUndoCheckpoint(); n->semi = i - 12; }, semiW);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
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
         if (ImGui::InputTextWithHint("##formula", "y = f(x, a, b, c, d, t)", buf, sizeof(buf)))
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
         row2.Knob("glide", &n->glide, 0.0f, 1.0f, "%.3f s");
         row2.End();
      }
      EndAudioSection();

      // Section 3: Amp Envelope
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope", "##eqAmpEnv", &n->ampAttack,
                        &n->ampDecay, &n->ampSustain, &n->ampRelease, nullptr, 0.0f, 0.0f, nullptr,
                        IM_COL32(50, 220, 255, 240));
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
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
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
               dl->PathStroke(isLight ? IM_COL32(0, 170, 150, 45) : IM_COL32(0, 240, 220, 45), 0, 4.0f);
            else
               dl->PathStroke(isLight ? IM_COL32(0, 160, 140, 240) : IM_COL32(50, 255, 230, 240), 0, 1.6f);
         }
      }
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
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
         const float octW = 60.0f, semiW = 68.0f, fineW = 84.0f;
         const float remW = std::max(120.0f, w - octW - semiW - fineW - gap * 3.0f);

         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.85f, 1.0f), "SPECTROGRAM SYNTH");

         ImGui::SetCursorScreenPos(ImVec2(x0 + remW, y));
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
         dl->AddRectFilled(origin, containerBr, IM_COL32(10, 12, 16, 255), 4.0f);
         dl->AddRect(origin, containerBr, IM_COL32(36, 42, 54, 180), 4.0f, 0, 1.0f);

         const float imgSize = previewH - 8.0f;
         const float imgX = origin.x + (containerW - imgSize) * 0.5f;
         const float imgY = origin.y + 4.0f;
         const ImVec2 imgTl(imgX, imgY);
         const ImVec2 imgBr(imgX + imgSize, imgY + imgSize);

         if (n->GetOutputTexture() != 0)
         {
            dl->AddImage((ImTextureID)(intptr_t)n->GetOutputTexture(), imgTl, imgBr, ImVec2(0, 1), ImVec2(1, 0));
            dl->AddRect(imgTl, imgBr, IM_COL32(0, 240, 220, 140), 2.0f, 0, 1.0f);
         }

         // Dynamic Glowing Neon Laser Scanhead Overlay (drawn cleanly on top of image every frame)
         const float playhead = n->Playhead();
         const float laserX = imgTl.x + std::clamp(playhead, 0.0f, 1.0f) * imgSize;

         // Outer cyan glow
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), IM_COL32(0, 240, 220, 50), 6.0f);
         // Mid cyan glow
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), IM_COL32(0, 255, 235, 130), 3.0f);
         // Sharp core laser line
         dl->AddLine(ImVec2(laserX, imgTl.y), ImVec2(laserX, imgBr.y), IM_COL32(235, 255, 255, 255), 1.5f);

         // Top & Bottom laser pointer diamond markers
         const float triSize = 4.0f;
         dl->AddTriangleFilled(ImVec2(laserX - triSize, imgTl.y), ImVec2(laserX + triSize, imgTl.y), ImVec2(laserX, imgTl.y + triSize * 1.5f), IM_COL32(0, 255, 240, 255));
         dl->AddTriangleFilled(ImVec2(laserX - triSize, imgBr.y), ImVec2(laserX + triSize, imgBr.y), ImVec2(laserX, imgBr.y - triSize * 1.5f), IM_COL32(0, 255, 240, 255));

         // Frequency ticks along left margin
         const char* freqLabels[5] = { "16k", "4k", "1k", "250", "40" };
         for (int i = 0; i < 5; i++)
         {
            const float ty = imgTl.y + (float)i * (imgSize / 4.0f) - 6.0f;
            dl->AddText(ImVec2(origin.x + 4.0f, ty), IM_COL32(140, 160, 190, 180), freqLabels[i]);
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
            if (ModTriggerButton("Trig##spectralTrigger", ImVec2(trigW, ImGui::GetFrameHeight())))
            {
               n->TriggerScan();
            }
         }

         if (!isManual)
         {
            ImGui::SetCursorScreenPos(ImVec2(x0 + w - dirW, y));
            bool reverse = n->direction != 0;
            if (AudioToggleButton("Rev##spectralDirection", &reverse, dirW))
            {
               PushUndoCheckpoint();
               n->direction = reverse ? 1 : 0;
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
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s");
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
         bool inverted = n->invert != 0;
         if (AudioToggleButton("Invert##spectralInvert", &inverted, invW))
         {
            PushUndoCheckpoint();
            n->invert = inverted ? 1 : 0;
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
                        IM_COL32(0, 240, 220, 240));
      ImGui::PopID();

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      BeginAudioSection("master & scope");
      DrawImageSpectralSynthScope(n, 48.0f, gAudioContentW);
      EndAudioSection();

      EndAudioBody();
   }

   // ---- Audio Meter (ported from upstream) ------------------------------
   namespace AudioMeterScale
   {
      struct Point { float db, frac; };
      static const Point kCurve[] = {
         { -60.0f, 0.00f }, { -48.0f, 0.10f }, { -36.0f, 0.22f }, { -24.0f, 0.38f },
         { -18.0f, 0.48f }, { -12.0f, 0.60f }, { -6.0f, 0.75f },  { -3.0f, 0.83f },
         { 0.0f, 0.92f },   { 3.0f, 1.00f },
      };

      inline float DbToFrac(float db)
      {
         const int n = (int)(sizeof(kCurve) / sizeof(kCurve[0]));
         if (db <= kCurve[0].db)
            return 0.0f;
         for (int i = 1; i < n; i++)
         {
            if (db <= kCurve[i].db)
            {
               const float t = (db - kCurve[i - 1].db) / (kCurve[i].db - kCurve[i - 1].db);
               return kCurve[i - 1].frac + t * (kCurve[i].frac - kCurve[i - 1].frac);
            }
         }
         return 1.0f;
      }

      inline float LevelToDb(float lvl) { return lvl > 1e-5f ? 20.0f * log10f(lvl) : -120.0f; }

      // "-inf" below the floor, explicit sign above 0 so an over reads as one.
      inline void FormatDb(char* buf, size_t size, float lvl)
      {
         const float db = LevelToDb(lvl);
         if (db <= -99.0f)
            snprintf(buf, size, "-inf");
         else if (db > 0.05f)
            snprintf(buf, size, "+%.1f", db);
         else
            snprintf(buf, size, "%.1f", db);
      }
   }

   void DrawAudioMeterVisualizer(AudioMeterNode* n, float x, float y, float w, float h)
   {
      using namespace AudioMeterScale;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin(x, y);
      const ImVec2 br(x + w, y + h);
      const bool isLight = IsThemeLight();
      // Every legend in the meter is drawn at a reduced size: at the body
      // font the scale numbers crowded the bars and each other on a 200px
      // body. The node's own caption/readout strip keep the normal size.
      ImFont* font = ImGui::GetFont();
      const float fs = ImGui::GetFontSize() * 0.72f;
      const float textH = fs;
      auto textSize = [&](const char* t) { return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t); };
      auto text = [&](ImVec2 p, ImU32 c, const char* t) { dl->AddText(font, fs, p, c, t); };

      dl->AddRectFilled(origin, br, ScopeBgCol(), 3.0f);

      // Zone colours, shared by bars, hold lines and readouts: green for
      // normal level, amber for the last 6 dB of headroom, red for overs.
      const ImU32 kGreen = isLight ? IM_COL32(40, 165, 90, 255) : IM_COL32(75, 215, 120, 255);
      const ImU32 kAmber = isLight ? IM_COL32(222, 150, 30, 255) : IM_COL32(255, 190, 90, 255);
      const ImU32 kRed = isLight ? IM_COL32(220, 55, 45, 255) : IM_COL32(255, 90, 80, 255);
      const ImU32 slotCol = isLight ? IM_COL32(0, 0, 0, 14) : IM_COL32(255, 255, 255, 10);
      auto zoneCol = [&](float db) { return db > 0.0f ? kRed : db > -6.0f ? kAmber : kGreen; };
      auto withAlpha = [](ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT); };

      // Geometry: [readout] row on top, bars in the middle, L/R captions
      // below; bars sit either side of a centred scale column.
      const float pad = 10.0f;
      const float scaleW = 34.0f;
      const float barW = std::min(36.0f, (w - 2.0f * pad - scaleW) * 0.5f);
      const float groupW = 2.0f * barW + scaleW;
      const float lx = x + (w - groupW) * 0.5f;
      const float rx = lx + barW + scaleW;
      const float scaleCx = lx + barW + scaleW * 0.5f;
      const float readTop = y + pad;
      const float readH = textH + 6.0f;
      const float barTop = readTop + readH + 10.0f;
      const float barBot = br.y - pad - textH - 6.0f;
      const float barH = barBot - barTop;
      auto yOf = [&](float db) { return barBot - DbToFrac(db) * barH; };

      // Scale: labelled ticks as short marks in the centre column plus a
      // faint line across each bar slot; unlabelled minor marks between.
      dl->AddRectFilled(ImVec2(lx, barTop), ImVec2(lx + barW, barBot), slotCol, 2.0f);
      dl->AddRectFilled(ImVec2(rx, barTop), ImVec2(rx + barW, barBot), slotCol, 2.0f);
      static const float kLabelled[] = { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f };
      static const float kMinor[] = { 3.0f, -3.0f, -9.0f, -18.0f, -30.0f, -42.0f, -54.0f };
      for (float db : kMinor)
      {
         const float ty = std::floor(yOf(db)) + 0.5f;
         dl->AddLine(ImVec2(lx + barW + 2.0f, ty), ImVec2(lx + barW + 5.0f, ty), ScopeMidLineCol(), 1.0f);
         dl->AddLine(ImVec2(rx - 5.0f, ty), ImVec2(rx - 2.0f, ty), ScopeMidLineCol(), 1.0f);
      }
      for (float db : kLabelled)
      {
         const float ty = std::floor(yOf(db)) + 0.5f;
         const ImU32 lineCol = db == 0.0f ? ScopeMidLineCol() : ScopeGridCol();
         dl->AddLine(ImVec2(lx, ty), ImVec2(lx + barW, ty), lineCol, 1.0f);
         dl->AddLine(ImVec2(rx, ty), ImVec2(rx + barW, ty), lineCol, 1.0f);
         char buf[8];
         snprintf(buf, sizeof(buf), "%.0f", db);
         const ImVec2 ts = textSize(buf);
         text(ImVec2(scaleCx - ts.x * 0.5f, ty - ts.y * 0.5f), ScopeTextCol(), buf);
      }

      // One channel: peak bar (translucent), RMS bar (solid) over it, both
      // split into colour zones so the bar reads like a hardware LED stack
      // rather than one block that changes colour all at once.
      static const float kZoneEdges[] = { -120.0f, -6.0f, 0.0f, 3.0f };
      const ImU32 kZoneCols[] = { kGreen, kAmber, kRed };
      auto fillZones = [&](float bx, float levelDb, int alpha) {
         if (levelDb <= -60.0f)
            return;
         for (int z = 0; z < 3; z++)
         {
            const float lo = kZoneEdges[z];
            const float hi = std::min(kZoneEdges[z + 1], levelDb);
            if (hi <= lo)
               break;
            const float y0 = yOf(hi);
            const float y1 = z == 0 ? barBot : yOf(lo);
            dl->AddRectFilled(ImVec2(bx + 1.0f, y0), ImVec2(bx + barW - 1.0f, y1),
                              withAlpha(kZoneCols[z], alpha));
         }
      };
      auto drawChannel = [&](float bx, float peak, float rms, float hold) {
         dl->PushClipRect(ImVec2(bx, barTop), ImVec2(bx + barW, barBot), true);
         fillZones(bx, LevelToDb(peak), isLight ? 90 : 80);
         fillZones(bx, LevelToDb(rms), isLight ? 235 : 225);
         // LED segmentation: a 1px gap every 3px, in the scope background.
         const ImU32 gapCol = withAlpha(ScopeBgCol(), 150);
         for (float gy = barBot - 3.0f; gy > barTop; gy -= 3.0f)
            dl->AddLine(ImVec2(bx, gy), ImVec2(bx + barW, gy), gapCol, 1.0f);
         const float holdDb = LevelToDb(hold);
         if (holdDb > -60.0f)
         {
            const float hy = yOf(holdDb);
            dl->AddRectFilled(ImVec2(bx + 1.0f, hy - 1.0f), ImVec2(bx + barW - 1.0f, hy + 1.0f), zoneCol(holdDb));
         }
         dl->PopClipRect();
      };
      drawChannel(lx, n->PeakL(), n->RmsL(), n->PeakHoldL());
      drawChannel(rx, n->PeakR(), n->RmsR(), n->PeakHoldR());

      // Max-peak readouts, one per bar. Red box once that channel clipped.
      auto drawReadout = [&](float bx, float maxPeak, bool clipped) {
         const ImVec2 a(bx, readTop), b(bx + barW, readTop + readH);
         dl->AddRectFilled(a, b, clipped ? kRed : slotCol, 2.0f);
         char buf[12];
         FormatDb(buf, sizeof(buf), maxPeak);
         const ImVec2 ts = textSize(buf);
         const ImU32 txt = clipped ? IM_COL32(255, 255, 255, 255)
                                   : maxPeak > 1e-5f ? ImGui::GetColorU32(ImGuiCol_Text) : ScopeTextCol();
         text(ImVec2(bx + (barW - ts.x) * 0.5f, readTop + (readH - ts.y) * 0.5f), txt, buf);
      };
      drawReadout(lx, n->MaxPeakL(), n->ClipL());
      drawReadout(rx, n->MaxPeakR(), n->ClipR());
      {
         const ImVec2 ts = textSize("pk");
         text(ImVec2(scaleCx - ts.x * 0.5f, readTop + (readH - ts.y) * 0.5f), ScopeTextCol(), "pk");
      }

      // Channel captions under the bars; "dB" under the scale.
      auto caption = [&](float cx, const char* s) {
         const ImVec2 ts = textSize(s);
         text(ImVec2(cx - ts.x * 0.5f, barBot + 5.0f), ScopeTextCol(), s);
      };
      caption(lx + barW * 0.5f, "L");
      caption(rx + barW * 0.5f, "R");
      caption(scaleCx, "dB");

      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##audiometer", ImVec2(w, h));
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
         n->ResetPeaks();
      if (ImGui::IsItemHovered())
      {
         char l[12], r[12], buf[64];
         FormatDb(l, sizeof(l), n->RmsL());
         FormatDb(r, sizeof(r), n->RmsR());
         snprintf(buf, sizeof(buf), "L %s  R %s", l, r);
         SetAudioReadout("rms", buf);
      }
   }

   void DrawAudioMeterBody(GraphNode& gn, AudioMeterNode* n)
   {
      using namespace AudioMeterScale;
      char stat[64];
      if (!n->HasInput())
         snprintf(stat, sizeof(stat), "no input");
      else if (n->ClipL() || n->ClipR())
         snprintf(stat, sizeof(stat), "CLIP %s%s - click to reset", n->ClipL() ? "L" : "", n->ClipR() ? "R" : "");
      else if (n->PeakL() > 1e-5f || n->PeakR() > 1e-5f)
      {
         char l[12], r[12];
         FormatDb(l, sizeof(l), n->PeakL());
         FormatDb(r, sizeof(r), n->PeakR());
         snprintf(stat, sizeof(stat), "L %s  R %s dB", l, r);
      }
      else
         snprintf(stat, sizeof(stat), "no signal");

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Three kKnobLarge knob rows tall - 1.5x Limiter's gain-reduction
      // meter (two rows, DrawLimiterBody). At the Limiter's height the
      // -48..0 span was too short to read a level against the ticks.
      const float rowH = kKnobLarge + 4.0f + ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
      const float meterH = 3.0f * rowH;
      DrawAudioMeterVisualizer(n, gAudioContentX, ImGui::GetCursorScreenPos().y, gAudioContentW, meterH);

      EndAudioBody();
   }

   void DrawGainBody(GraphNode& gn, GainNode* n)
   {
      // One param, so §7's "a one-param node is visually smaller" holds. A
      // knob, not the Mixer-strip fader this used to be: a lone control with
      // no neighbours to compare against gets none of the "read several
      // levels as a balance at a glance" benefit a fader row earns, so it
      // reverts to the app's default control.
      char stat[48];
      snprintf(stat, sizeof(stat), "%+.1f dB   %s", n->gainDb,
               n->Level() > 0.0005f ? "\xe2\x96\xb8" : "-");
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(1, kKnobLarge);
      row.Knob("gain", &n->gainDb, -60.0f, 12.0f, "%.1f dB", kKnobLarge, /*dbTaper=*/true);
      row.End();
      EndAudioBody();
   }

#if INFINITE_GPL
   // Punch FX (src/gpl): 16 pads, each with its own effect and MIDI note.
   // Click a pad to play it (hold, or toggle with latch) and to select it for
   // the editor row below (effect, note, learn). Lit = its effect is playing.
   void DrawPunchFxBody(GraphNode& gn, PunchFxNode* n)
   {
      const uint32_t active = n->ActiveMask();
      char stat[160] = "hold a pad";
      if (active != 0)
      {
         stat[0] = 0;
         for (int fx = 0; fx < PunchFxNode::kNumEffects; ++fx)
            if (active & (1u << fx))
            {
               if (stat[0] != 0) strncat(stat, " + ", sizeof(stat) - strlen(stat) - 1);
               strncat(stat, PunchFxNode::EffectName(fx), sizeof(stat) - strlen(stat) - 1);
            }
      }
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // 4x4 pads, pad 1 bottom-left like the MPC. Each is a gate button with
      // its own CV pin (MIDI learn it to a controller pad from the pin);
      // click selects the pad for the effect dropdown below.
      {
         const float gap = 6.0f;
         const float padW = (gAudioContentW - gap * 3.0f) / 4.0f;
         const float padH = 44.0f;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         uint32_t heldMask = 0;
         for (int row = 0; row < 4; row++)
            for (int col = 0; col < 4; col++)
            {
               const int p = (3 - row) * 4 + col;
               ImGui::SetCursorScreenPos(ImVec2(origin.x + (float)col * (padW + gap), origin.y + (float)row * (padH + gap)));
               ImGui::PushID(p);
               char label[32];
               snprintf(label, sizeof(label), "%d##punchPad%d", p + 1, p + 1);
               const bool lit = ((active >> n->padFx[p]) & 1u) != 0 || (n->latch && n->PadLatched(p));
               bool clicked = false;
               const bool held = ModGateButton(label, ImVec2(padW, padH), lit, p == n->selectedPad, true,
                                               PunchFxNode::EffectName(n->padFx[p]), clicked);
               if (clicked)
                  n->selectedPad = p;
               if (held && !n->uiHeld[p] && n->latch)
                  n->TogglePadLatch(p);
               n->uiHeld[p] = held;
               if (held)
                  heldMask |= 1u << p;
               ImGui::PopID();
            }
         n->heldPads = heldMask;
         ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 4.0f * (padH + gap)));
         ImGui::Dummy(ImVec2(gAudioContentW, 2.0f));
      }

      // The selected pad's effect.
      const int sp = std::clamp(n->selectedPad, 0, PunchFxNode::kNumPads - 1);
      std::vector<std::string> names;
      names.reserve(PunchFxNode::kNumEffects);
      for (int i = 0; i < PunchFxNode::kNumEffects; ++i)
         names.push_back(PunchFxNode::EffectName(i));
      ImGui::Text("pad %d", sp + 1);
      ImGui::SameLine();
      DropdownButton("##padfx", names, n->padFx[sp], [n, sp](int idx) { PushUndoCheckpoint(); n->padFx[sp] = idx; },
                     160.0f, /*modulatable=*/false);

      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      ModCheckbox("latch", &n->latch);
      ImGui::SameLine();
      DropdownButton("effect", names, n->effect, [n](int idx) { n->effect = idx; }, 120.0f);
      ImGui::SameLine();
      ModCheckbox("engage", &n->engage);
      EndAudioBody();
   }
#endif

   void DrawBlendAudioBody(GraphNode& gn, BlendAudioNode* n)
   {
      char stat[48];
      const int pctB = (int)std::lround(n->blend * 100.0f);
      snprintf(stat, sizeof(stat), "A <-> B   %d%%", pctB);
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(1, kKnobLarge);
      row.Knob("blend", &n->blend, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();
      EndAudioBody();
   }

   // A sample player: load a file, record from the audio input pin, scrub/
   // audition by clicking the waveform, drag its two edge handles to trim
   // the playback/loop range, and play back one-shot or looping (forward,
   // reversed, or ping-ponging) with independent pitch/finetune/speed/
   // volume. Also the drop target when a sample is dragged onto this node's
   // body from the Samples search panel (see gSampleDragActive) - same
   // LoadFile() either way.
   //
   // Sliders rather than knobs (per reference: Amigo/Bespoke-style sample
   // players read as one flat control strip, not a cluster of dials), and a
   // waveform tall enough to actually judge a transient against - a sampler
   // lives or dies on "where does this sample start", and a 64px strip was
   // too short to place that by eye.
   void DrawSamplerBody(GraphNode& gn, SamplerNode* n)
   {
      char stat[160];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...", ImVec2(90, 0)))
         StartNodeFileDialog(n, Platform::OpenAudioDialog, [](auto* m, const std::string& path) {
            PushUndoCheckpoint();
            m->LoadFile(path);
         });
      // Dropping a sample from the Samples search panel directly onto this
      // node's body is handled at the canvas level (see gSampleDragActive),
      // not through ImGui's native drag-drop payload API - the drag source
      // is a manually-tracked mouse drag, not a BeginDragDropSource/
      // SetDragDropPayload pair, since the drop target here is really
      // "whichever node the drop lands on", resolved once in one place
      // rather than duplicated as a per-node-type accept.
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      bool requestedRecording = recording;
      if (ModStateButton(recording ? "Stop##samplerRecord" : "Record##samplerRecord",
                         recording, requestedRecording, ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (requestedRecording)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      // Audition control, independent of the transport and any note cable -
      // it only ever touches the self lane's dedicated voice (see
      // SamplerNode.h), never a note-lane voice, so it can't be cut off by
      // or cut off an incoming note. With loop (or free-running/no note
      // cable) a triggered voice sounds forever, and clicking the waveform
      // again only retriggers from a new point rather than silencing it, so
      // there needs to be a dedicated way to stop it.
      ImGui::SameLine();
      const bool playing = n->IsPlaying();
      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      bool requestedPlaying = playing;
      if (ModStateButton(playing ? "Stop##samplerAudition" : "Audition##samplerAudition",
                         playing, requestedPlaying, ImVec2(90, 0)))
      {
         if (requestedPlaying)
            n->TriggerPreview(n->start);
         else
            n->StopPreview();
      }
      if (playing)
         ImGui::PopStyleColor();
      // A plain ImGui tooltip here would land offset from the cursor, proportional
      // to the node editor's zoom/pan - see gAudioReadout's comment for why every
      // hover explanation in this file goes through the fixed readout strip instead.
      if (ImGui::IsItemHovered())
         SetAudioReadout("#", "previews this node only - space starts the patch");

      // loop/reverse/ping-pong right-aligned on the same row, so the top of
      // the body reads as one bar rather than stacked controls fighting for
      // the same strip of height. Pill toggles (AudioToggleButton), not
      // checkboxes - see its comment for why.
      //
      // Anchored off gAudioContentX/gAudioContentW (the node's fixed
      // declared width from BeginAudioBody), not GetContentRegionAvail() -
      // avail tracks the node's *live* current width, which sits flush
      // against imgui-node-editor's own resize-drag hit-zone at the node's
      // right edge. A row right-justified against that live edge overlaps
      // that hit-zone, so a drag starting on/near these buttons got read as
      // a node-resize instead of a click, and the row (laid out from the
      // stale pre-resize width) visibly lagged behind it. Every other
      // control in this file already anchors off the fixed content width
      // for exactly this reason.
      ImGui::SameLine();
      const float toggleW = 44.0f;
      const float modsW = toggleW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
      const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - modsW, rowScreenPos.y));
      bool loopBool = n->loop;
      if (AudioToggleButton("loop##samplerLoop", &loopBool))
      {
         PushUndoCheckpoint();
         n->loop = loopBool;
      }
      ImGui::SameLine();
      bool reverseBool = n->reverse;
      if (AudioToggleButton("rev##samplerReverse", &reverseBool))
      {
         PushUndoCheckpoint();
         n->reverse = reverseBool;
      }
      ImGui::SameLine();
      bool pingpongBool = n->pingpong;
      if (AudioToggleButton("p-p##samplerPingpong", &pingpongBool))
      {
         PushUndoCheckpoint();
         n->pingpong = pingpongBool;
      }

      // Read-only status word, right-aligned on its own line: what's
      // actually making this node sound right now. A note cable takes
      // priority in the label since that's the more specific/interesting
      // state; "auditioning" only shows when nothing's patched into notes
      // and the self voice is user- rather than transport-owned (see
      // SamplerNode.h's IsAuditioning()). No new toggle or param - purely a
      // reflection of change 3's published state.
      {
         const bool noteConnected = n->noteInput.IsConnected();
         char status[32];
         if (noteConnected)
            snprintf(status, sizeof(status), "notes \xc2\xb7 %d", n->ActiveNoteCount());
         else if (n->IsAuditioning())
            snprintf(status, sizeof(status), "auditioning");
         else
            snprintf(status, sizeof(status), "%s",
                     Transport::Instance().IsPlaying() ? "auto \xc2\xb7 running" : "auto \xc2\xb7 press space");

         const ImVec2 textSize = ImGui::CalcTextSize(status);
         const ImVec2 rowPos = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - textSize.x, rowPos.y));
         ImGui::TextColored(ImVec4(0.6f, 0.63f, 0.72f, 1.0f), "%s", status);
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawSamplerWaveform(n, 140.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // finetune/pitch and speed/volume pair up at half-width each, the
      // same two-per-row rhythm every other slider body in this file uses.
      // start/end duplicate the two waveform handles as exact numeric
      // fields - the picture is for "roughly where", these are for "exactly
      // where", and each stays in sync with the other since both write the
      // same n->start/n->end.
      AudioSlider("finetune", &n->finetune, -50.0f, 50.0f, "%.0f c", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      AudioSlider("speed", &n->speed, -2.0f, 2.0f, "%.2fx", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->start = std::min(n->start, n->end - 0.01f);
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->end = std::max(n->end, n->start + 0.01f);
      // Per-pass fades: every pass through start..end opens and closes with
      // these (a loop dips at its seam instead of clicking).
      AudioSlider("fade in", &n->fadeIn, 0.0f, 250.0f, "%.0f ms", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("fade out", &n->fadeOut, 0.0f, 250.0f, "%.0f ms", AudioHalfWidth());
      // Turbo 0.48 (upstream port): playback start within the range, and note-voice
      // decay (0 = held). Drawn last so existing modulation pin ordinals stay put.
      if (AudioSlider("position", &n->position, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->position = std::clamp(n->position, n->start, std::max(n->start, n->end)); // start > end via set_param
      ImGui::SameLine();
      AudioSlider("decay", &n->decay, 0.0f, 10.0f, n->decay <= 0.0f ? "held" : "%.2f s", AudioHalfWidth());

      EndAudioBody();
   }

   // Custom node body for PaulStretch
   void DrawPaulStretchBody(GraphNode& gn, PaulStretchNode* n)
   {
      char stat[160];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...", ImVec2(90, 0)))
         StartNodeFileDialog(n, Platform::OpenAudioDialog, [](auto* m, const std::string& path) {
            PushUndoCheckpoint();
            m->LoadFile(path);
         });
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      bool requestedRecording = recording;
      if (ModStateButton(recording ? "Stop##paulstretchRecord" : "Record##paulstretchRecord",
                         recording, requestedRecording, ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (requestedRecording)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      const bool playing = n->IsPlaying();
      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      bool requestedPlaying = playing;
      if (ModStateButton(playing ? "Stop##paulstretchAudition" : "Audition##paulstretchAudition",
                         playing, requestedPlaying, ImVec2(90, 0)))
      {
         if (requestedPlaying)
            n->TriggerPreview(n->start);
         else
            n->StopPreview();
      }
      if (playing)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      const float toggleW = 44.0f;
      const float modsW = toggleW;
      const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - modsW, rowScreenPos.y));
      bool loopBool = n->loop;
      if (AudioToggleButton("loop##paulstretchLoop", &loopBool))
      {
         PushUndoCheckpoint();
         n->loop = loopBool;
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawPaulStretchWaveform(n, 130.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      AudioSlider("stretch", &n->stretch, 1.0f, 1000.0f, "%.1fx", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("phase rand", &n->phaseRand, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      static const std::vector<std::string> sWinSizeNames = {
         "2048 (46ms)", "4096 (93ms)", "8192 (186ms)", "16384 (372ms)",
         "32768 (743ms)", "65536 (1.49s)", "131072 (2.97s)"
      };
      AudioBareDropdown("paulWinSize", sWinSizeNames, n->windowSizeIndex,
                        [n](int idx) {
                           PushUndoCheckpoint();
                           n->windowSizeIndex = idx;
                        }, AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      AudioSlider("pitch", &n->pitchShift, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("finetune", &n->fineTune, -100.0f, 100.0f, "%.0f c", AudioHalfWidth());

      AudioSlider("freq shift", &n->freqShift, -1000.0f, 1000.0f, "%.0f Hz", AudioHalfWidth());
      ImGui::SameLine();
      float unisonF = (float)n->unison;
      if (AudioSlider("unison", &unisonF, 1.0f, 8.0f, "%.0f v", AudioHalfWidth()))
         n->unison = std::clamp((int)(unisonF + 0.5f), 1, 8);

      AudioSlider("detune", &n->detune, 0.0f, 100.0f, "%.1f c", AudioHalfWidth());
      ImGui::SameLine();
      if (AudioSlider("position", &n->position, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->position = std::clamp(n->position, std::min(n->start, n->end), std::max(n->start, n->end));
         n->Seek(n->position);
      }

      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->start = std::min(n->start, n->end - 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->end = std::max(n->end, n->start + 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }

      EndAudioBody();
   }

   // Custom node body for Granular Synthesizer
   void DrawGranularBody(GraphNode& gn, GranularNode* n)
   {
      char stat[160];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...", ImVec2(80, 0)))
         StartNodeFileDialog(n, Platform::OpenAudioDialog, [](auto* m, const std::string& path) {
            PushUndoCheckpoint();
            m->LoadFile(path);
         });
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      bool requestedRecording = recording;
      if (ModStateButton(recording ? "Stop##granRec" : "Record##granRec",
                         recording, requestedRecording, ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (requestedRecording)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      bool freezeBool = n->freeze;
      if (AudioToggleButton("freeze##granFreeze", &freezeBool))
      {
         PushUndoCheckpoint();
         n->freeze = freezeBool;
      }

      ImGui::SameLine();
      bool loopBool = n->loop;
      if (AudioToggleButton("loop##granLoop", &loopBool))
      {
         PushUndoCheckpoint();
         n->loop = loopBool;
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      DrawGranularWaveform(n, 130.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Row 1: Position & Scan
      if (AudioSlider("pos", &n->position, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         PushUndoCheckpoint();
         n->Seek(n->position);
      }
      ImGui::SameLine();
      AudioSlider("scan", &n->scan, -4.0f, 4.0f, "%.2fx", AudioHalfWidth());

      // Row 2: Grain Size & Density
      AudioSlider("size", &n->grainLength, 5.0f, 1000.0f, "%.0f ms", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("density", &n->density, 1.0f, 100.0f, "%.0f Hz", AudioHalfWidth());

      // Row 3: Random Position (Spray) & Random Length
      AudioSlider("spray", &n->randomPos, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("rnd size", &n->randomLength, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      // Row 4: Random Pitch & Pitch Shift
      AudioSlider("pitch", &n->pitchShift, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("rnd pitch", &n->randomPitch, 0.0f, 24.0f, "%.1f st", AudioHalfWidth());

      // Row 5: Fine Tune & Reverse Probability
      AudioSlider("finetune", &n->fineTune, -100.0f, 100.0f, "%.0f c", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("reverse", &n->reverseProb, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      // Row 6: Window Envelope Shape & Pan Spread
      static const std::vector<std::string> sWinShapes = {
         "Hann", "Gaussian", "Triangle", "Exp Decay", "Trapezoid"
      };
      AudioBareDropdown("granShape", sWinShapes, n->windowShape,
                        [n](int idx) {
                           PushUndoCheckpoint();
                           n->windowShape = idx;
                        }, AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("pan spread", &n->randomPan, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      // Row 7: Master Pan & Width
      AudioSlider("pan", &n->pan, -1.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("width", &n->width, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      // Row 8: Dry/Wet Mix & Volume
      AudioSlider("mix", &n->mix, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      // Row 9: Trim Start & End
      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth() * 0.5f - 4.0f))
         n->start = std::min(n->start, n->end - 0.01f);
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth() * 0.5f - 4.0f))
         n->end = std::max(n->end, n->start + 0.01f);

      EndAudioBody();
   }

   // Drag state for the step grid's paint/velocity gestures - file-scope
   // like gSampleDragActive, since a drag spans many frames and many
   // per-cell InvisibleButton calls. See DrawDrumSequencerBody's grid loop.
   struct DrumGridDragState
   {
      bool active = false;
      bool paintOn = false;
      int originLane = -1;
      int originStep = -1;
   };
   DrumGridDragState gDrumGridDrag;

   // One lane's waveform view: a drag-&-drop/click-to-load target when
   // empty, or start/end range handles over the decimated waveform once a
   // sample is loaded - same overlap-ordering trick as DrawSamplerWaveform
   // (body click-catcher first via SetNextItemAllowOverlap, then the
   // handles), just reading DrumSequencerNode's per-lane waveform cache
   // instead of SamplerNode's single one.
