   // Turbo 0.60: upstream's Drift / Moves / Predictive Modulator bodies (src/app/bodies/
   // AudioBodyKit.cpp), verbatim.

   // Upstream AppShared.h / AppState.cpp: the Drift meter's per-destination
   // trace, keyed by the Drift node's uid and the destination (uid, not index).
   inline constexpr int kDriftTraceLen = 128;
   struct DriftTrace
   {
      float v[kDriftTraceLen] = {};
      int head = 0;
      int filled = 0;
      int lastFrame = -1;
   };
   std::map<std::pair<uint64_t, ParamKey>, DriftTrace> gDriftTraces;

   uint64_t UidForIndex(int nodeIndex)
   {
      const GraphNode* gn = FindNodeByIndex(nodeIndex);
      return gn != nullptr ? gn->uid : 0;
   }

   ImU32 DriftLineColor(int i, int alpha)
   {
      static const ImU32 kRGB[] = {
         tok::U32(tok::pal::c_34D39900),  // green
         tok::U32(tok::pal::c_60A5FA00),  // blue
         tok::U32(tok::pal::c_FBBF2400),  // amber
         tok::U32(tok::pal::c_F472B600), // pink
         tok::U32(tok::pal::c_A78BFA00), // violet
         tok::U32(tok::pal::c_2DD4BF00),  // teal
         tok::U32(tok::pal::c_F8717100), // red
         tok::U32(tok::pal::c_A3E63500),  // lime
      };
      const ImU32 c = kRGB[i % (int)(sizeof(kRGB) / sizeof(kRGB[0]))];
      return (c & 0x00FFFFFFu) | ((ImU32)std::clamp(alpha, 0, 255) << IM_COL32_A_SHIFT);
   }


   // "Filter 2 - cutoff", falling back to whatever is knowable. Short, because it is drawn at the
   // width of a node body next to a number.
   std::string DriftDestinationLabel(const ParamKey& key)
   {
      GraphNode* owner = FindNodeByUid(key.uid);
      if (owner == nullptr)
         return "(deleted)";
      std::string out = NodeTitleWithInstance(*owner);
      const ParamRef* known = Modulation::Instance().KnownParam(owner->index, key.paramIndex);
      if (known != nullptr && !known->name.empty())
         out += " - " + known->name;
      return out;
   }


   // Legend: one row per line, the destination it drives and that destination's own live value.
   // Deliberately no aggregate - no overall value, no min/max - because this node has no single
   // output for one to describe.
   void DrawDriftLegend(DriftNode* n)
   {
      const float w = kPreviewSize;
      for (int i = 0; i < n->SlotCount(); i++)
      {
         float pos = 0.0f;
         ParamKey key;
         if (!n->ReadSlotForUI(i, pos, key))
            continue;
         char value[16];
         snprintf(value, sizeof(value), "%.2f", pos);
         const float valueW = ImGui::CalcTextSize(value).x;
         const float lh = ImGui::GetTextLineHeight();
         const ImVec2 p = ImGui::GetCursorScreenPos();
         ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + lh * 0.5f - 1.5f),
                                                   ImVec2(p.x + 8.0f, p.y + lh * 0.5f + 1.5f),
                                                   DriftLineColor(i, 230), 1.0f);
         ImGui::Dummy(ImVec2(11.0f, lh));
         ImGui::SameLine(0.0f, 0.0f);
         // Truncate the name by hand rather than clipping or wrapping: a wrapped label would
         // push the value onto its own line and desync the swatch column from the row it names.
         std::string label = DriftDestinationLabel(key);
         const float avail = w - 11.0f - valueW - 6.0f;
         if (ImGui::CalcTextSize(label.c_str()).x > avail)
         {
            while (label.size() > 1 && ImGui::CalcTextSize((label + "..").c_str()).x > avail)
               label.pop_back();
            label += "..";
         }
         ImGui::TextDisabled("%s", label.c_str());
         ImGui::SameLine(0.0f, 0.0f);
         ImGui::SetCursorScreenPos(ImVec2(p.x + w - valueW, p.y));
         ImGui::TextDisabled("%s", value);
      }
   }


   // The node's main meter, in place of DrawModulatorMeter. Same box, same geometry, same 0..1
   // axis as every other modulator's preview - but N lines instead of one, because this node has
   // no single output: each destination gets its own slot and its own value, so the one number
   // DrawModulatorMeter prints under the box does not exist here and is not drawn.
   void DrawDriftMeter(DriftNode* n, int nodeIndex)
   {
      const float w = kPreviewSize;
      const float h = 90.0f; // same as DrawModulatorMeter, so the node header keeps its height
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      AudioViz::Fill(dl, origin, ImVec2(origin.x + w, origin.y + h));
      // Fixed 0..1 axis with quarter guides, exactly as the generic meter draws them: overlaid
      // destinations are only comparable at all because they share one scale.
      for (int q = 1; q < 4; q++)
      {
         const float gy = origin.y + h - (q * 0.25f) * h;
         dl->AddLine(ImVec2(origin.x, gy), ImVec2(origin.x + w, gy), ScopeMidLineCol(), q == 2 ? 1.0f : 0.5f);
      }

      const uint64_t sourceUid = UidForIndex(nodeIndex);
      const int count = n->SlotCount();
      if (count == 0)
      {
         AudioViz::IdleLabel(AudioViz::Frame{dl, origin, ImVec2(origin.x + w, origin.y + h)}, "no targets");
         AudioViz::Border(dl, origin, ImVec2(origin.x + w, origin.y + h));
         ImGui::Dummy(ImVec2(w, h));
         return;
      }

      // Sample once per frame per destination, even if the body is drawn twice (node body and
      // params panel), so the trace scrolls at a rate the user can read rather than at draw rate.
      const int frame = ImGui::GetFrameCount();
      std::vector<std::pair<ParamKey, float>> live;
      live.reserve((size_t)count);
      for (int i = 0; i < count; i++)
      {
         float pos = 0.0f;
         ParamKey key;
         if (!n->ReadSlotForUI(i, pos, key))
            continue;
         DriftTrace& tr = gDriftTraces[{sourceUid, key}];
         if (tr.lastFrame != frame)
         {
            tr.lastFrame = frame;
            tr.v[tr.head] = pos;
            tr.head = (tr.head + 1) % kDriftTraceLen;
            tr.filled = std::min(tr.filled + 1, kDriftTraceLen);
         }
         live.emplace_back(key, pos);
      }
      if (live.empty())
      {
         AudioViz::IdleLabel(AudioViz::Frame{dl, origin, ImVec2(origin.x + w, origin.y + h)}, "no targets");
         AudioViz::Border(dl, origin, ImVec2(origin.x + w, origin.y + h));
         ImGui::Dummy(ImVec2(w, h));
         return;
      }

      // Drop traces for destinations (and nodes) that stopped being drawn. Without this the map
      // only ever grows over a session, one entry per knob a Drift was ever dragged onto.
      for (auto it = gDriftTraces.begin(); it != gDriftTraces.end();)
         it = (frame - it->second.lastFrame > 600) ? gDriftTraces.erase(it) : std::next(it);

      dl->PushClipRect(origin, ImVec2(origin.x + w, origin.y + h), true);
      for (size_t li = 0; li < live.size(); li++)
      {
         const DriftTrace& tr = gDriftTraces[{sourceUid, live[li].first}];
         if (tr.filled < 2)
            continue;
         const ImU32 col = DriftLineColor((int)li, 210);
         ImVec2 prev;
         for (int i = 0; i < tr.filled; i++)
         {
            // Oldest first: head is the next write slot, so the ring starts `filled` behind it.
            const int idx = (tr.head - tr.filled + i + kDriftTraceLen * 2) % kDriftTraceLen;
            const float x = origin.x + w * (float)i / (float)(kDriftTraceLen - 1);
            const float y = origin.y + h - 2.0f - std::clamp(tr.v[idx], 0.0f, 1.0f) * (h - 4.0f);
            const ImVec2 p(x, y);
            if (i > 0)
               dl->AddLine(prev, p, col, 1.4f);
            prev = p;
         }
         dl->AddCircleFilled(prev, 2.0f, col);
      }
      dl->PopClipRect();
      AudioViz::Border(dl, origin, ImVec2(origin.x + w, origin.y + h));
      ImGui::Dummy(ImVec2(w, h));
      DrawDriftLegend(n);
   }


   void DrawDriftParams(GraphNode& gn, DriftNode* n)
   {
      ModSlider("speed", &n->speed, 0.1f, 4.0f, "%.2fx");
      ModSlider("stray", &n->stray, 0.25f, 4.0f, "%.2f");
      ModSlider("momentum", &n->momentum, 0.0f, 4.0f, "%.2fs");
      ModSlider("smoothing", &n->smoothness, 0.0f, 0.95f, "%.2f");
      ModSlider("depth", &n->depth, 0.0f, 1.0f, "%.2f");
      // `low`/`high` used to sit here, writing rangeLo/rangeHi/rangeOverride: ONE range clamped
      // onto every destination at once. That is meaningless on this node - each destination has
      // its own slot and its own learned range, so a single pair of sliders either did nothing or
      // squashed every knob into the same window. Retired: the save keys stay in VisitParams so
      // older patches still load, but the override is forced off rather than left silently
      // active with no UI to see or undo it.
      n->rangeOverride = false;
      n->rangeLo = 0.0f;
      n->rangeHi = 1.0f;

      const int count = n->SlotCount();
      if (count == 0)
      {
         ImGui::TextDisabled("drag onto a knob to drive it");
         return;
      }
      // Who is actually authoring the model right now. This is measured, not asserted: each
      // source pool is graded on how well it predicts where the hand goes next (prequentially,
      // before the sample is absorbed), and the share is that skill times the pool's maturity.
      {
         MovementStats::PoolShares prof;
         MovementStats::Live().GetGlobalPoolShares(prof);
         const float human = (prof.share[MovementStats::kPoolDeliberate] +
                              prof.share[MovementStats::kPoolExploratory]) * 100.0f;
         const float autoPct = prof.share[MovementStats::kPoolAuto] * 100.0f;
         const float replay = prof.share[MovementStats::kPoolReplay] * 100.0f;
         if (prof.valid)
            ImGui::TextDisabled("profile: %.0f%% you, %.0f%% automation, %.0f%% replay", human, autoPct, replay);
         else
            ImGui::TextDisabled("profile: no data yet - perform a knob to teach it");
      }
      // No graph here: the traces and their legend ARE the node's main meter now, drawn once in
      // the header by DrawDriftMeter. A second copy down here was the duplicate the graph was
      // supposed to replace.
      for (int i = 0; i < count; i++)
      {
         float pos = 0.0f;
         ParamKey key;
         if (!n->ReadSlotForUI(i, pos, key))
            continue;
         const float confidence = std::clamp(n->Confidence01(key), 0.0f, 1.0f) * 100.0f;
         // The INDEPENDENT move count, not raw n_eff. Raw n_eff counts every 10 Hz dwell sample,
         // so a knob that was merely left alone while the transport ran reported thousands of
         // "samples" beside a confidence percentage derived from almost none of them.
         const double moves = n->IndependentSamplesForUI(i);
         ImGui::TextDisabled("%s: %.0f moves, %.0f%% learned", DriftDestinationLabel(key).c_str(), moves,
                             confidence);
      }
   }


   // Motion (Prediction) node body - Step 8 refinement round 3: no params, same as Drift. It
   // rides the strongest learned move across every knob it's coupled to and wanders on its own
   // via the same OU walk DriftNode uses; `gesture`/`amount`/the extra PCA-axis faders are all
   // fixed, tuned constants now (see feedback_audio_node_minimalism) rather than user-facing
   // knobs, so this is a read-only readout, matching DrawDriftParams.
   void DrawMovesParams(MovesNode* n)
   {
      if (n->SlotCount() == 0)
      {
         ImGui::TextDisabled("drag onto knobs to drive gesture");
         return;
      }
      ImGui::TextDisabled("gesture %.2f", n->gesture);
      // Real explained variance of the axis this node rides, floor to ceiling. The old
      // clamp(.., 10, 99) invented a 10% floor for a patch with no recorded movement at all,
      // which read as "it knows something" when it knew nothing.
      ImGui::TextDisabled("%d coupled  |  Focus: %.0f%% energy", n->SlotCount(),
                          std::clamp(n->ExplainedVariance(0), 0.0f, 1.0f) * 100.0f);
   }


   // Predictive Modulator node body (Step 8 item 5): Learn/Stop + a progress meter, rank
   // dropdown, an honest read-only spectral-radius readout, and the constantIn fallback shown
   // only when nothing is patched in (mirrors SmoothNode/RangeToRangeNode's own "(no cable)"
   // convention rather than inventing a new one).
   void DrawPredictiveModulatorParams(PredictiveModulatorNode* n)
   {
      const bool learning = n->IsLearning();
      const float w = kPreviewSize;
      const float h = ImGui::GetFrameHeight();
      if (ActionButton::Draw(learning ? "Stop##predModLearn" : "Learn##predModLearn", ImVec2(w * 0.3f, h)))
      {
         PushUndoCheckpoint();
         n->SetLearning(!learning);
      }
      ImGui::SameLine();
      {
         // Progress meter: a real Learning % while learning (or still short of a fit), driven
         // by the engine's own history length - what FitDMD actually requires - not a counter
         // that can look "done" while the fit is still rejected. Once a fit exists, show its
         // spectral radius instead, since "how close to unstable" is the one number worth
         // surfacing about a finished fit.
         const ImVec2 p0 = ImGui::GetCursorScreenPos();
         const float mw = w - w * 0.3f - ImGui::GetStyle().ItemSpacing.x;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(p0, ImVec2(p0.x + mw, p0.y + h), tok::U32(tok::pal::c_FFFFFF0E), 3.0f);
         char label[64];
         const int pct = n->LearningPercent();
         if (learning)
            snprintf(label, sizeof(label), "learning - %d%%", pct);
         else if (n->HasLearnAttempt() && !n->HasFit())
         {
            // Stopped, enough history, and FitDMD still rejected it. Showing "learning - 100%"
            // here (which is what the single combined branch did) claims a finished model that
            // does not exist; say what actually happened and what to do about it.
            if (pct >= 100)
               snprintf(label, sizeof(label), "no usable fit - play more, Learn again");
            else
               snprintf(label, sizeof(label), "stopped early - %d%%, Learn again", pct);
         }
         else if (n->HasFit())
            snprintf(label, sizeof(label), "fit - radius %.2f", n->SpectralRadius());
         else
            snprintf(label, sizeof(label), "wire input, press Learn");
         // Clip to the meter's own box - AddText doesn't wrap or clip on its own, so a longer
         // label would otherwise draw straight past the node's edge instead of just past mw.
         dl->PushClipRect(p0, ImVec2(p0.x + mw, p0.y + h), true);
         const ImVec2 lsz = ImGui::CalcTextSize(label);
         dl->AddText(ImVec2(p0.x + 6.0f, p0.y + (h - lsz.y) * 0.5f), tok::U32(tok::pal::c_C8C8C8C8), label);
         dl->PopClipRect();
         ImGui::Dummy(ImVec2(mw, h));
      }

      // No rank dropdown - Step 8 refinement round 3: rank was the delay-embedding dimension
      // (how many lagged copies of the one input signal DMD gets to fit a transition matrix
      // against), an implementation detail of "how", not a UI/UX decision - it stays fixed at
      // n->rank's own default from here on, the same "no params" treatment Drift got.
      ModSlider("speed", &n->speed, 0.1f, 10.0f, "%.2fx");
      ModSlider("low", &n->low, 0.0f, 1.0f, "%.2f");
      ModSlider("high", &n->high, 0.0f, 1.0f, "%.2f");
      if (n->input == nullptr)
         ModSlider("constant", &n->constantIn, 0.0f, 1.0f, "%.2f");
   }
