// Audio node body building blocks: columns, sections, knob rows, drift meters, gates (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/components/AudioViz.h"

namespace app
{
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
                        isLight ? tok::U32(tok::pal::c_0000000C) : tok::U32(tok::pal::c_FFFFFF0A), 3.0f);
      const float textY = p.y + 3.0f;
      // Both texts are fitted to the strip: a long status string (a device
      // error, a file name) used to run out past the node's right edge. The
      // hovered-param readout wins its side; the idle stat gets what's left
      // and is ellipsised rather than clipped mid-glyph.
      auto fitText = [](const char* text, float maxW) {
         std::string out = text;
         if (maxW <= 0.0f)
            return std::string();
         if (ImGui::CalcTextSize(out.c_str()).x <= maxW)
            return out;
         while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > maxW)
         {
            // Drop whole UTF-8 sequences, not bytes.
            do { out.pop_back(); } while (!out.empty() && (out.back() & 0xC0) == 0x80);
         }
         return out + "...";
      };
      float readoutW = 0.0f;
      std::string readoutFit;
      if (!readout.empty())
      {
         readoutFit = fitText(readout.c_str(), width - 16.0f);
         readoutW = ImGui::CalcTextSize(readoutFit.c_str()).x;
      }
      // One status grammar (R10): "<what> · <state>". Legacy " - " / "  -  " separators become the middle dot,
      // ASCII "->" becomes an arrow, and a trailing separator is dropped.
      auto normaliseStat = [](const char* in) {
         std::string s = in;
         auto replaceAll = [&](const std::string& a, const std::string& b) {
            for (size_t pos = 0; (pos = s.find(a, pos)) != std::string::npos; pos += b.size())
               s.replace(pos, a.size(), b);
         };
         replaceAll("->", "\xE2\x86\x92");
         replaceAll("  -  ", " \xC2\xB7 ");
         replaceAll(" - ", " \xC2\xB7 ");
         for (;;)
         {
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (s.size() >= 2 && s.compare(s.size() - 2, 2, "\xC2\xB7") == 0) { s.resize(s.size() - 2); continue; }
            break;
         }
         return s;
      };
      if (idleStat != nullptr && idleStat[0] != '\0')
      {
         const std::string idleNorm = normaliseStat(idleStat);
         const float idleMax = width - 14.0f - (readoutW > 0.0f ? readoutW + 14.0f : 0.0f);
         const std::string idleFit = fitText(idleNorm.c_str(), idleMax);
         if (!idleFit.empty())
            dl->AddText(ImVec2(p.x + 7.0f, textY),
                        isLight ? tok::U32(tok::pal::c_50586CFF) : tok::U32(tok::pal::c_848A9EFF),
                        idleFit.c_str());
      }
      if (!readoutFit.empty())
         dl->AddText(ImVec2(p.x + width - 8.0f - readoutW, textY),
                     isLight ? tok::U32(tok::pal::c_1E2434FF) : tok::U32(tok::pal::c_E2E8F4FF),
                     readoutFit.c_str());
      ImGui::Dummy(ImVec2(width, h));
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
   }


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
      ImGui::PushID(index);
      ImGui::BeginGroup();
   }


   void EndAudioColumn()
   {
      // Read the cursor before EndGroup: EndGroup emits the group's own
      // ItemSize, which moves it.
      gAudioColumn.maxBottom = std::max(gAudioColumn.maxBottom, ImGui::GetCursorScreenPos().y);
      ImGui::EndGroup();
      ImGui::PopID();
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
                           isLight ? tok::U32(tok::pal::c_0000000A) : tok::U32(tok::pal::c_FFFFFF09), 5.0f);
         dl->AddRect(ImVec2(gAudioBodyX, p.y), ImVec2(gAudioBodyX + gAudioBodyW, p.y + cached),
                     isLight ? tok::U32(tok::pal::c_00000014) : tok::U32(tok::pal::c_FFFFFF10), 5.0f);
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
                  isLight ? tok::U32(tok::pal::c_00000016) : tok::U32(tok::pal::c_FFFFFF12), 1.0f);
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


   // Field build step 18: the four Field*Params bodies (element/pixel/
   // sample/graph) aren't IsAudioBodyNode() participants - they're
   // dispatched through the same generic DrawXxxParams path as Render3D or
   // Camera, never through BeginAudioBody() (see DrawAudioNodeBody's own
   // comment on what that dispatch covers). AudioKnobRow's Place() math
   // still reads gAudioBodyX/W and gAudioContentX/W though, so seed just
   // those four geometry globals - not gAudioTint/gAudioSectionIndex,
   // which nothing in these bodies reads - to the same kPreviewSize column
   // every other control in these bodies already wraps its text to. Safe to
   // leave un-restored afterward: every other consumer of these globals
   // (an audio node's own BeginAudioBody, or another Field body) reseeds
   // them itself before reading them.
   void BeginFieldKnobGrid()
   {
      // gParamRegisterOnly can mean no ImGui context exists at all (see
      // AudioKnobRow's constructor comment) - gAudioContentX is only ever
      // read for real widget placement, so 0 is a safe stand-in and callers
      // downstream (AudioKnobRow, ModKnob) already no-op before using it.
      gAudioBodyX = gAudioContentX = gParamRegisterOnly ? 0.0f : ImGui::GetCursorScreenPos().x;
      gAudioBodyW = gAudioContentW = kPreviewSize;
   }


   // Per-line colour. Distinct hues rather than shades of the prediction green: the point of the
   // graph is telling the lines apart, and the node's category tint already says "prediction".
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


   template <typename NodeT>
   void DrawFieldParamKnobGrid(NodeT* n)
   {
      DrawFieldParamSliders(n);
   }


   // Full- and half-width audio sliders, sized from the content column so
   // two halves plus the gutter are exactly one full width.
   float AudioFullWidth() { return gAudioContentW; }

   float AudioHalfWidth() { return (gAudioContentW - ImGui::GetStyle().ItemSpacing.x) * 0.5f; }


   bool AudioSlider(const char* label, float* v, float lo, float hi, const char* fmt, float width,
                    FaderPosToValueFn posToValue, FaderValueToPosFn valueToPos,
                    int explicitParamIndex, const char* nameOverride)
   {
      return ModSlider(label, v, lo, hi, fmt, width, /*audioStyle=*/true, /*step=*/0.0f, posToValue, valueToPos,
                       explicitParamIndex, nameOverride);
   }


   bool AudioSliderInt(const char* label, int* v, int lo, int hi, float width)
   {
      return ModSliderInt(label, v, lo, hi, width, /*audioStyle=*/true);
   }


   // A small pill toggle for boolean mods (loop/reverse/ping-pong) - not
   // ImGui::Checkbox, whose frame draws in the theme's FrameBg, and not a
   // plain ImGui::Button either. Pixel-sampling the render showed *both*
   // are only a handful of RGB values different from the node body
   // background (theme default, ~45,50,62 vs ~48,50,67) - visible on a wide
   // 90px "Load..." button by sheer surface area, imperceptible on a
   // narrow ~40px "loop" pill. Rather than lean on that subtle theme
   // contrast at all, this picks explicit, clearly-different fill colours
   // and adds a real border stroke - a guaranteed-visible backstop the way
   // the waveform box and slider fields already draw their own explicit
   // AddRect borders instead of trusting a themed background.
   bool AudioToggleButtonEx(const char* label, bool* value, const ImVec2& size, ActionButton::Kind onKind)
   {
      const bool clicked = ActionButton::Draw(label, size, *value ? onKind : ActionButton::Kind::Plain);
      if (clicked)
         *value = !*value;
      return clicked;
   }


   bool AudioToggleButton(const char* label, bool* value, float width, float height)
   {
      return AudioToggleButtonEx(label, value, ImVec2(width, height), ActionButton::Kind::Selected);
   }

   bool DrawGateButton(const char* id, float totalW, float height, const GatePainter& paint,
                       bool* activated)
   {
      if (activated != nullptr)
         *activated = false;
      const DiscreteParamHandle h = RegisterDiscreteParam(id, 0.0f, 1.0f, /*isBool=*/true, nullptr,
                                                          /*momentary=*/true);
      if (h.registered && !h.draw)
         return h.driven && h.value >= 0.5f;
      const float pinW = 16.0f;
      const ImVec2 start = ImGui::GetCursorScreenPos();
      float btnW = totalW;
      if (h.registered)
      {
         DrawDiscreteParamPin(h, id, pinW, height);
         ImGui::SetCursorScreenPos(ImVec2(start.x + pinW, start.y));
         btnW = std::max(20.0f, totalW - pinW);
      }
      ImGui::PushID(id);
      if (h.modulated)
         ImGui::BeginDisabled();
      ImGui::InvisibleButton("##gate", ImVec2(btnW, height));
      const bool held = ImGui::IsItemActive();
      const bool hovered = ImGui::IsItemHovered();
      if (activated != nullptr)
         *activated = ImGui::IsItemActivated();
      const ImVec2 mn = ImGui::GetItemRectMin();
      const ImVec2 mx = ImGui::GetItemRectMax();
      if (h.modulated)
         ImGui::EndDisabled();
      ImGui::PopID();
      const bool level = h.driven ? (h.value >= 0.5f) : held;
      paint(ImGui::GetWindowDrawList(), mn, mx, hovered && !h.modulated, level);
      if (h.registered)
         DrawModulationBindingMenu(h.nodeIndex, h.paramIndex, ImGui::IsMouseHoveringRect(mn, mx));
      return level;
   }


   // A Sampler-standard button that is also a CV-gate destination: same
   // ImGui::Button drawing, height and width rules as the Sampler's buttons, a
   // 16 px modulation pin to its left. Returns the LEVEL (CV high when a cable
   // drives it, else the mouse held on it); the node turns level changes into
   // edges. style: 0 plain, 1 red (a stop/active action), 2 blue toggle, `lit`
   // being its on state.
   bool DrawGateControl(const char* id, const char* label, float btnW, int style, bool lit)
   {
      const DiscreteParamHandle h = RegisterDiscreteParam(id, 0.0f, 1.0f, /*isBool=*/true, nullptr,
                                                          /*momentary=*/true);
      if (h.registered && !h.draw)
         return h.driven && h.value >= 0.5f;
      const float pinW = 16.0f;
      const float height = ImGui::GetFrameHeight();
      const ImVec2 start = ImGui::GetCursorScreenPos();
      if (h.registered)
      {
         DrawDiscreteParamPin(h, id, pinW, height);
         ImGui::SetCursorScreenPos(ImVec2(start.x + pinW, start.y));
      }
      ImGui::PushID(id);
      if (h.modulated)
         ImGui::BeginDisabled();
      if (style == 2)
      {
         bool v = lit;
         AudioToggleButton(label, &v, btnW);
      }
      else
      {
         ActionButton::Draw(label, ImVec2(btnW, 0), (style == 1) ? ActionButton::Kind::Record : ActionButton::Kind::Plain);
      }
      const bool held = ImGui::IsItemActive();
      const ImVec2 mn = ImGui::GetItemRectMin();
      const ImVec2 mx = ImGui::GetItemRectMax();
      if (h.modulated)
         ImGui::EndDisabled();
      ImGui::PopID();
      if (h.registered)
         DrawModulationBindingMenu(h.nodeIndex, h.paramIndex, ImGui::IsMouseHoveringRect(mn, mx));
      return h.driven ? (h.value >= 0.5f) : held;
   }


   bool AudioSoloButton(const char* label, bool* value, float width, float height)
   {
      return AudioToggleButtonEx(label, value, ImVec2(width, height), ActionButton::Kind::Solo);
   }


   bool AudioMuteButton(const char* label, bool* value, float width, float height)
   {
      return AudioToggleButtonEx(label, value, ImVec2(width, height), ActionButton::Kind::Record);
   }


   bool AudioSmallButton(const char* label, float width, float height)
   {
      return ActionButton::Draw(label, ImVec2(width, height));
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
      // AudioAnalyzeNode needs the identical carve-out for the identical
      // reason: broadening its input from an AudioFileNode* pointer to a real
      // AudioCable (so it accepts any audio source) gave it an AudioInputSlot(0),
      // which silently flipped this gate true. That both suppressed its eye
      // toggle and handed it to DrawAudioNodeBody, which has no case for it -
      // so the node rendered as a bare pin column with no params at all. It
      // keeps DrawAudioAnalyzeParams behind the eye like the other analyzers.
      // VideoSourceNode needs the identical carve-out: it became an
      // IAudioSource so its own audio track can flow into the DSP graph, but
      // it keeps its DrawVideoParams body (file picker, loop/speed, preview)
      // rather than the generic v3 audio body, which has no case for it and
      // would otherwise drop path/loop/speed/audioEnabled/volume entirely.
      // FieldSampleNode needs the same carve-out: it's an IAudioSource with
      // an AudioCable, so it trips this gate, but DrawAudioNodeBody has no
      // case for it either - it rendered as a bare pin column with no "Edit
      // Field..." button and no way to reach DrawFieldSampleParams at all.
      if (dynamic_cast<AudioTextureNode*>(node) != nullptr || dynamic_cast<AudioFileNode*>(node) != nullptr ||
          dynamic_cast<AudioRibbonNode*>(node) != nullptr ||
          dynamic_cast<AudioColorRampNode*>(node) != nullptr ||
          dynamic_cast<AudioAnalyzeNode*>(node) != nullptr || dynamic_cast<VideoSourceNode*>(node) != nullptr ||
          dynamic_cast<FieldSampleNode*>(node) != nullptr ||
          dynamic_cast<FieldSynthNode*>(node) != nullptr ||
          dynamic_cast<FieldNotesNode*>(node) != nullptr ||
          dynamic_cast<NoteToCVNode*>(node) != nullptr ||
          dynamic_cast<VelocityToCVNode*>(node) != nullptr ||
          dynamic_cast<CVRecorderNode*>(node) != nullptr)
         return false;
      return dynamic_cast<IAudioSource*>(node) != nullptr || node->AudioInputSlot(0) != nullptr ||
             dynamic_cast<INoteSource*>(node) != nullptr || node->NoteInputSlot(0) != nullptr ||
             dynamic_cast<VibratoNode*>(node) != nullptr || dynamic_cast<EnvelopeNode*>(node) != nullptr;
   }

   float MpcNodeWidth()
   {
      return 4.0f * (kMpcPinGutter + kMpcPadSide) + 3.0f * ImGui::GetStyle().ItemSpacing.x;
   }


   float AudioNodeWidth(INode* node)
   {
      if (dynamic_cast<MpcNode*>(node) != nullptr)
         return MpcNodeWidth();
      if (auto* mixer = dynamic_cast<MixerNode*>(node))
         return std::max(280.0f, (float)mixer->numChannels * 80.0f);
      if (dynamic_cast<SpatialMixerNode*>(node) != nullptr)
         return kAudioNodeWidth;
      if (dynamic_cast<WavetableNode*>(node) != nullptr ||
          dynamic_cast<DrumSequencerNode*>(node) != nullptr)
         return kAudioWideWidth;
      if (dynamic_cast<GainNode*>(node) != nullptr ||
          dynamic_cast<AudioMeterNode*>(node) != nullptr ||
          dynamic_cast<BlendAudioNode*>(node) != nullptr ||
          dynamic_cast<AudioInputNode*>(node) != nullptr ||
          dynamic_cast<SplitterNode*>(node) != nullptr ||
          dynamic_cast<NoteTransposeNode*>(node) != nullptr ||
          dynamic_cast<PitchBendNode*>(node) != nullptr ||
          dynamic_cast<GateNode*>(node) != nullptr ||
          dynamic_cast<GlideNode*>(node) != nullptr ||
          dynamic_cast<VibratoNode*>(node) != nullptr ||
          dynamic_cast<VelocityCurveNode*>(node) != nullptr ||
          dynamic_cast<HumanizerNode*>(node) != nullptr ||
          dynamic_cast<QuantizerNode*>(node) != nullptr ||
          dynamic_cast<PredictiveQuantizeNode*>(node) != nullptr ||
          dynamic_cast<PredictiveVelocityNode*>(node) != nullptr ||
          dynamic_cast<PredictiveRhythmNode*>(node) != nullptr ||
          dynamic_cast<NoteEchoNode*>(node) != nullptr ||
          dynamic_cast<NoteMergeNode*>(node) != nullptr ||
          dynamic_cast<NoteSwitcherNode*>(node) != nullptr ||
          dynamic_cast<NoteRouterNode*>(node) != nullptr ||
          dynamic_cast<NoteStrumNode*>(node) != nullptr ||
          dynamic_cast<AudioToCVNode*>(node) != nullptr ||
          dynamic_cast<AudioOutputNode*>(node) != nullptr)
         return kAudioNarrowWidth;
      return kAudioNodeWidth;
   }
}
