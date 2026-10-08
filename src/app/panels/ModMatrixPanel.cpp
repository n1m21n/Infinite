// Modulation matrix panel (moved verbatim from main.cpp).
#include "app/ui/design/GlyphDraw.h"
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"

namespace app
{
   // The modulation matrix's table body: one row per active binding (see
   // Modulation::Links), additive to the per-param ##modbind popup above -
   // that popup stays the only way to edit a single binding from its own
   // knob; this is an overview of all of them at once.
   // A DragFloat with the same hover-and-type-a-digit / double-click-to-type
   // interaction every other param field gets from ModSlider, reusing that
   // same gTypedParam/HandleParamTypeHotkeys machinery. Lo/Hi cells aren't
   // real registered ParamRefs though - no pin, no expression - so they get
   // their own synthetic editKey namespace (negative paramIndex) rather than
   // the caller's real (nodeIndex, paramIndex), which is always >= 0 (see
   // gParamCounter) and would otherwise collide with that same destination
   // node's own knob elsewhere in the graph.
   bool TypableRangeField(const char* strId, const std::pair<int, int>& editKey, float* value,
                          float step, float minV, float maxV, const char* fmt,
                          bool noBorder = false)
   {
      bool changed = false;
      if (gTypedParam.count(editKey) > 0)
      {
         if (gTypedParamJustOpened == editKey)
         {
            ImGui::SetKeyboardFocusHere();
            gTypedParamJustOpened = std::pair<int, int>(-1, -1);
         }
         char buf[64];
         snprintf(buf, sizeof(buf), "%s", gTypedParamText[editKey].c_str());
         const std::string typedId = std::string("##typed") + strId;
         const bool entered = ImGui::InputText(typedId.c_str(), buf, sizeof(buf),
                                               ImGuiInputTextFlags_EnterReturnsTrue);
         gTypedParamText[editKey] = buf;
         if (gTypedParamPendingInit.count(editKey) && ImGui::IsItemActive())
         {
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID()))
            {
               if (gTypedParamNoAutoSelect.count(editKey))
               {
                  state->Stb.cursor = state->CurLenW;
                  state->ClearSelection();
               }
               else
               {
                  state->SelectAll();
               }
            }
            gTypedParamPendingInit.erase(editKey);
         }
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (entered || ImGui::IsItemDeactivated())
         {
            const std::string trimmed = TrimCopy(gTypedParamText[editKey]);
            if (!trimmed.empty() && !TypedTextIsUntouchedSeed(editKey, trimmed))
            {
               char* end = nullptr;
               float parsed = strtof(trimmed.c_str(), &end);
               if (end != trimmed.c_str())
               {
                  *value = std::clamp(parsed, std::min(minV, maxV), std::max(minV, maxV));
                  changed = true;
               }
            }
            gTypedParam.erase(editKey);
            gTypedParamText.erase(editKey);
            gTypedParamSeed.erase(editKey);
            gTypedParamNoAutoSelect.erase(editKey);
            gTypedParamPendingInit.erase(editKey);
         }
      }
      else
      {
         PushSliderStyle();
         // The Lo/Hi cells in the modulation matrix table already sit inside
         // the table's own BordersInnerH/V grid lines - PushSliderStyle's
         // light-mode hairline border around each field on top of that read
         // as a box-within-a-box, doubled-up rather than a single grid.
         if (noBorder)
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
         changed = ImGui::DragFloat(strId, value, step, minV, maxV, fmt);
         if (noBorder)
            ImGui::PopStyleVar();
         PopSliderStyle();
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, editKey.first, editKey.second, value, fmt, /*hasExpr=*/false);
         if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }
      return changed;
   }


   float ApplyModulationCurve(float v, float curve)
   {
      v = std::clamp(v, 0.0f, 1.0f);
      if (std::abs(curve) < 0.0001f)
         return v;
      return std::pow(v, std::exp2(curve * 3.0f));
   }


   bool DrawMiniCurveWidget(const char* strId, float* curve, float liveInput01 = -1.0f, float width = 50.0f)
   {
      bool changed = false;
      if (curve == nullptr)
         return false;

      const float h = ImGui::GetFrameHeight();
      const float w = width;
      const ImVec2 pos = ImGui::GetCursorScreenPos();

      ImGui::PushID(strId);
      ImGui::InvisibleButton(strId, ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();
      const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

      if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
      {
         const float deltaY = ImGui::GetIO().MouseDelta.y;
         if (deltaY != 0.0f)
         {
            *curve = std::clamp(*curve + deltaY * 0.015f, -1.0f, 1.0f);
            if (std::abs(*curve) < 0.02f)
               *curve = 0.0f;
            changed = true;
         }
      }
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();

      if (doubleClicked)
      {
         PushUndoCheckpoint();
         *curve = 0.0f;
         changed = true;
      }

      if (ImGui::BeginPopupContextItem("##curve_ctx"))
      {
         ImGui::TextDisabled("%s", T("Modulation Curve"));
         ImGui::Separator();
         if (ImGui::MenuItem(L("Linear (Reset)"), nullptr, std::abs(*curve) < 0.001f))
         {
            PushUndoCheckpoint();
            *curve = 0.0f;
            changed = true;
         }
         if (ImGui::MenuItem(L("Ease In (+0.50)"), nullptr, std::abs(*curve - 0.5f) < 0.05f))
         {
            PushUndoCheckpoint();
            *curve = 0.5f;
            changed = true;
         }
         if (ImGui::MenuItem(L("Ease Out (-0.50)"), nullptr, std::abs(*curve - (-0.5f)) < 0.05f))
         {
            PushUndoCheckpoint();
            *curve = -0.5f;
            changed = true;
         }
         if (ImGui::MenuItem(L("Steep Exp (+0.85)"), nullptr, std::abs(*curve - 0.85f) < 0.05f))
         {
            PushUndoCheckpoint();
            *curve = 0.85f;
            changed = true;
         }
         if (ImGui::MenuItem(L("Steep Log (-0.85)"), nullptr, std::abs(*curve - (-0.85f)) < 0.05f))
         {
            PushUndoCheckpoint();
            *curve = -0.85f;
            changed = true;
         }
         if (ImGui::MenuItem(L("Invert Curve"), nullptr, false, std::abs(*curve) > 0.001f))
         {
            PushUndoCheckpoint();
            *curve = -*curve;
            changed = true;
         }
         ImGui::EndPopup();
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const ImVec2 maxPos(pos.x + w, pos.y + h);

      const ImU32 bgCol = tok::U32(hovered ? tok::curvebox_bg_hover : tok::curvebox_bg, isLight);
      const ImU32 borderCol = tok::U32(active ? tok::curvebox_border_active : hovered ? tok::curvebox_border_hover : tok::curvebox_border, isLight);
      dl->AddRectFilled(pos, maxPos, bgCol, 3.0f);
      dl->AddRect(pos, maxPos, borderCol, 3.0f, 0, 1.0f);

      const float padX = 4.0f;
      const float padY = 3.0f;
      const float plotW = w - padX * 2.0f;
      const float plotH = h - padY * 2.0f;
      const ImVec2 plotMin(pos.x + padX, pos.y + padY);

      const ImU32 refCol = tok::U32(tok::curvebox_ref, isLight);
      dl->AddLine(ImVec2(plotMin.x, plotMin.y + plotH), ImVec2(plotMin.x + plotW, plotMin.y), refCol, 1.0f);

      auto evalPt = [&](float xNorm) -> ImVec2 {
         const float yNorm = ApplyModulationCurve(xNorm, *curve);
         return ImVec2(plotMin.x + xNorm * plotW, plotMin.y + (1.0f - yNorm) * plotH);
      };

      const bool isCurved = std::abs(*curve) > 0.001f;
      const ImU32 curveCol = tok::U32(active ? tok::curve_active : isCurved ? tok::curve_bent : tok::curve_idle, isLight);
      const float lineThickness = (active || hovered) ? 2.0f : 1.5f;

      const int kSegments = 20;
      ImVec2 prevPt = evalPt(0.0f);
      for (int i = 1; i <= kSegments; i++)
      {
         const float xNorm = (float)i / (float)kSegments;
         const ImVec2 nextPt = evalPt(xNorm);
         dl->AddLine(prevPt, nextPt, curveCol, lineThickness);
         prevPt = nextPt;
      }

      const ImVec2 midPt = evalPt(0.5f);
      const ImU32 dotCol = tok::U32(isCurved ? tok::curve_dot_bent : tok::curve_dot_idle, isLight);
      dl->AddCircleFilled(midPt, (hovered || active) ? 3.0f : 2.0f, dotCol);

      if (liveInput01 >= 0.0f && liveInput01 <= 1.0f)
      {
         const ImVec2 livePt = evalPt(liveInput01);
         const ImU32 liveCol = tok::U32(tok::curve_live, isLight);
         dl->AddCircleFilled(livePt, 3.5f, liveCol);
         dl->AddCircle(livePt, 3.5f, tok::U32(tok::curve_live_ring, isLight), 0, 1.0f);
      }

      if (isCurved)
      {
         char valBuf[16];
         snprintf(valBuf, sizeof(valBuf), "%+.2f", *curve);
         ImFont* font = ImGui::GetFont();
         const float tinySize = ImGui::GetFontSize() * 0.62f;
         const ImU32 valCol = tok::U32(tok::curve_value_text, isLight);
         dl->AddText(font, tinySize, ImVec2(pos.x + 2.0f, pos.y + 1.0f), valCol, valBuf);
      }

      ImGui::PopID();
      return changed;
   }


   void DrawSparklineMiniGraph(const char* strId, const SparklineHistory& hist, ImU32 lineCol, float width, float height)
   {
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const ImU32 bgCol = tok::U32(tok::sparkline_bg, isLight);
      const ImU32 borderCol = tok::U32(tok::sparkline_border, isLight);
      dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bgCol, 2.0f);
      dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), borderCol, 2.0f);

      if (hist.count >= 2)
      {
         const float padX = 2.0f;
         const float padY = 2.0f;
         const float plotW = width - padX * 2.0f;
         const float plotH = height - padY * 2.0f;

         const int N = hist.count;
         const int start = (hist.head - N + SparklineHistory::kCap) % SparklineHistory::kCap;

         ImVec2 prevPt;
         for (int i = 0; i < N; i++)
         {
            int idx = (start + i) % SparklineHistory::kCap;
            float val = std::clamp(hist.samples[idx], 0.0f, 1.0f);
            float x = pos.x + padX + ((float)i / (float)(N - 1)) * plotW;
            float y = pos.y + padY + (1.0f - val) * plotH;
            ImVec2 pt(x, y);
            if (i > 0)
               dl->AddLine(prevPt, pt, lineCol, 1.5f);
            prevPt = pt;
         }
         dl->AddCircleFilled(prevPt, 2.0f, lineCol);
      }
      ImGui::Dummy(ImVec2(width, height));
   }


   void DrawModMatrixTable()
   {
      Modulation& mod = Modulation::Instance();

      const ImVec2 panelOrigin = ImGui::GetCursorScreenPos();
      const ImVec2 panelSize = ImGui::GetContentRegionAvail();

      GestureRecorder& rec = GestureRecorder::Instance();
      if (mod.Links().empty() && mod.Expressions().empty() && rec.Playbacks().empty())
      {
         ImGui::TextDisabled("%s", T("No active modulations."));
         ImGui::TextDisabled("%s", T("Patch a modulator, type a formula, or record a gesture to see it here."));
      }
      else
      {
         // Inner grid lines only - no BordersOuterH/V. This is a docked
         // panel, not a floating dialog: DrawModMatrixDocked's own comment
         // (below, "No divider line along the canvas-facing edge") already
         // settled that this panel reads as separate from its opaque
         // panelBg fill alone, no edge needed. A full ImGuiTableFlags_Borders
         // draws the theme's full-strength border colour as a solid rule
         // around the whole table regardless, which put exactly the heavy
         // edge that comment argues against back at the top of the panel -
         // the one thing here that read thicker than every other divider in
         // the app (all of which are the single subtle PanelSeamColor
         // hairline). The inner rules stay: this is a dense multi-column
         // matrix and losing row/column separation would hurt readability.
         const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV |
                                       ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;
         // An explicit, fixed outer_size.y - with ScrollY and the default
         // (0,0), ImGui auto-extends the table's own height to fit its
         // content instead of clipping/scrolling at a fixed size (see the
         // outer_size comment atop imgui_tables.cpp: "outer_size.y = 0.0f
         // -> will auto extend"). That auto-extension is what actually
         // drove the fill-to-bottom loop below to runaway - each filler
         // row grew the table, which grew what GetWindowHeight() reported
         // for it, so the loop's own target kept receding. Pinning the
         // height to this panel's available height (captured above, before
         // the table exists) gives the loop a stable target to fill to.
         if (ImGui::BeginTable("##modmatrixtable", 12, flags, ImVec2(0.0f, panelSize.y)))
         {
            // Fixed, non-resizable widths rather than the stretch/drag
            // behaviour ImGui tables default to - dragging columns around
            // just to read a value was the user's actual complaint. Two
            // presets, keyed off the panel's own dock orientation (see the
            // identical `vertical` convention in DrawModMatrixDocked): the
            // left/right dock is narrow, so its columns run tighter than
            // the top/bottom dock's.
            // One shared width for every data column (Source through Hi) -
            // Destination/Parameter's own width, picked as the size that
            // still fits the longest destination/param names comfortably -
            // rather than each column sized to its own content, so the
            // table reads as a symmetrical grid instead of a ragged one.
            const bool vertical = gModMatrixDock == 1 || gModMatrixDock == 2;
            const float wCol = vertical ? 57.0f : 130.0f; // vertical is 130 * 0.6 - 40% narrower
            const float wCurve = vertical ? 42.0f : 50.0f;

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##en", ImGuiTableColumnFlags_WidthFixed, 18.0f);
            ImGui::TableSetupColumn(L("Source"), ImGuiTableColumnFlags_WidthFixed, wCol);
            ImGui::TableSetupColumn(L("Destination"), ImGuiTableColumnFlags_WidthFixed, wCol);
            ImGui::TableSetupColumn(L("Parameter"), ImGuiTableColumnFlags_WidthFixed, wCol);
            ImGui::TableSetupColumn(L("Value"), ImGuiTableColumnFlags_WidthFixed, wCol);
            ImGui::TableSetupColumn(L("Confidence"), ImGuiTableColumnFlags_WidthFixed, vertical ? 45.0f : 75.0f);
            ImGui::TableSetupColumn(L("Lo"), ImGuiTableColumnFlags_WidthFixed, wCol);
            ImGui::TableSetupColumn(L("Hi"), ImGuiTableColumnFlags_WidthFixed, wCol);
            ImGui::TableSetupColumn("##inv", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn(L("Curve"), ImGuiTableColumnFlags_WidthFixed, wCurve);
            ImGui::TableSetupColumn(L("Signal"), ImGuiTableColumnFlags_WidthFixed, vertical ? 42.0f : 55.0f);
            ImGui::TableSetupColumn("##unbind", ImGuiTableColumnFlags_WidthFixed, 20.0f);
            ImGui::TableHeadersRow();

            // Row actions mutate mLinks. SetEnabled/SetRange only mutate a
            // Source in place, but Unbind erases it - break immediately
            // after that fires rather than continuing to iterate a map
            // that just lost the element the for-loop machinery is on.
            for (const auto& link : mod.Links())
            {
               const int dstIndex = link.first.first;
               const int dstParam = link.first.second;

               GraphNode* dstNode = FindNodeByIndex(dstIndex);
               GraphNode* srcNode = FindNodeByIndex(link.second.nodeIndex);
               if (dstNode == nullptr || srcNode == nullptr)
                  continue; // stale - deleted node, undo/redo rewound past it

               ImGui::PushID(dstIndex * 1000 + dstParam);
               ImGui::TableNextRow();

               // "View in Modulation Matrix" landed here: scroll it into view once, and flash
               // its row for a moment so it's findable by eye even after the scroll already
               // happened.
               const bool isHighlightTarget =
                  dstIndex == gModMatrixHighlightNode && dstParam == gModMatrixHighlightParam;
               if (isHighlightTarget && gModMatrixScrollPending)
               {
                  ImGui::SetScrollHereY(0.35f);
                  gModMatrixScrollPending = false;
               }
               if (isHighlightTarget && ImGui::GetTime() < gModMatrixHighlightUntil)
                  ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, tok::U32(tok::modulation_row));

               // Resolve against this frame's ParamRef when the destination
               // drew this frame (the same lazy legacy-conversion pass the
               // knob itself uses via ResolvedSourceFor); otherwise fall
               // back to the plain lookup, which may still show an
               // un-derived lo/hi (0, 0) for a just-loaded legacy binding
               // whose destination hasn't drawn a single frame since.
               const ParamRef* frameRef = nullptr;
               for (const ParamRef& r : mod.FrameParams())
               {
                  if (r.nodeIndex == dstIndex && r.paramIndex == dstParam)
                  {
                     frameRef = &r;
                     break;
                  }
               }
               const Modulation::Source src = frameRef != nullptr
                                                  ? mod.ResolvedSourceFor(*frameRef)
                                                  : mod.ModulatorFor(dstIndex, dstParam);

               const ParamRef* known = mod.KnownParam(dstIndex, dstParam);
               const bool isInt = known != nullptr && known->step > 0.0f;

               bool unbound = false;

               // Enable toggle
               ImGui::TableNextColumn();
               // A predictor bound to a discrete param (only reachable via a patch file, paste or
               // undo - the cable drop refuses it) is inert: the apply loop never writes it.
               const bool inert = IsInertPredictorBinding(dstIndex, dstParam);
               const bool isPred = IsPredictionSourceNode(srcNode);
               const ImU32 dotColour = (src.enabled && !inert)
                                          ? (isPred ? tok::U32(tok::prediction) : tok::U32(tok::modulation))
                                          : tok::U32(tok::inert);
               const ImVec2 dotCursor = ImGui::GetCursorScreenPos();
               const float dotH = ImGui::GetTextLineHeight();
               ImGui::Dummy(ImVec2(dotH, dotH));
               if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
               {
                  PushUndoCheckpoint();
                  mod.SetEnabled(dstIndex, dstParam, !src.enabled);
               }
               ImGui::GetWindowDrawList()->AddCircleFilled(
                  ImVec2(dotCursor.x + dotH * 0.5f, dotCursor.y + dotH * 0.5f), dotH * 0.35f, dotColour);
               if (inert && ImGui::IsItemHovered())
                  ImGui::SetTooltip("%s", T("A predictor drives continuous parameters only - this binding is inactive."));

               const ImVec4 textColour = (src.enabled && !inert) ? ImGui::GetStyle().Colors[ImGuiCol_Text]
                                                     : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
               ImGui::PushStyleColor(ImGuiCol_Text, textColour);

               // Source
               ImGui::TableNextColumn();
               if (ImGui::Selectable(NodeTitleWithInstance(*srcNode).c_str(), false))
                  gPendingSelect.push_back(srcNode->NodeId());

               // Destination
               ImGui::TableNextColumn();
               if (ImGui::Selectable(NodeTitleWithInstance(*dstNode).c_str(), false))
                  gPendingSelect.push_back(dstNode->NodeId());

               // Parameter
               ImGui::TableNextColumn();
               if (known != nullptr)
                  ImGui::TextUnformatted(known->name.c_str());
               else
                  ImGui::Text(T("param %d"), dstParam);

               // Value - this frame's live value only; a collapsed node's
               // destination hasn't registered a float* to read this frame.
               ImGui::TableNextColumn();
               if (frameRef != nullptr && frameRef->value != nullptr)
                  ImGui::Text(isInt ? "%.0f" : "%.3f", *frameRef->value);
               else
                  ImGui::TextUnformatted("--");

               ImGui::PopStyleColor();

               // Confidence
               ImGui::TableNextColumn();
               float conf = -1.0f;
               if (srcNode != nullptr && srcNode->node != nullptr)
               {
                  if (auto* pred = dynamic_cast<IPredictor*>(srcNode->node.get()))
                  {
                     const ParamKey pk{UidForIndex(dstIndex), dstParam};
                     conf = pred->Confidence01(pk);
                  }
               }
               if (conf >= 0.0f)
               {
                  // Tint by the value, not a flat green. Since the predictors stopped inventing
                  // floors, 0% is a real and common answer ("this key has no data of its own"),
                  // and a confident-looking green 0% is exactly the misread this column is for.
                  const float t = std::clamp(conf, 0.0f, 1.0f);
                  const ImU32 col = IM_COL32((int)std::round(148.0f + (34.0f - 148.0f) * t),
                                             (int)std::round(163.0f + (197.0f - 163.0f) * t),
                                             (int)std::round(184.0f + (94.0f - 184.0f) * t), 255);
                  ImGui::PushStyleColor(ImGuiCol_Text, col);
                  ImGui::Text("%d%%", (int)std::round(conf * 100.0f));
                  ImGui::PopStyleColor();
               }
               else
               {
                  ImGui::TextUnformatted("--");
               }

               // Lo / Hi
               float lo = src.lo, hi = src.hi;
               const float minV = known != nullptr ? known->minValue : lo;
               const float maxV = known != nullptr ? known->maxValue : hi;
               const float step = isInt ? 1.0f : std::max(0.0001f, (maxV - minV) * 0.01f);
               bool rangeChanged = false;

               const std::pair<int, int> loKey(dstIndex, -(dstParam * 2 + 1));
               const std::pair<int, int> hiKey(dstIndex, -(dstParam * 2 + 2));

               ImGui::TableNextColumn();
               ImGui::SetNextItemWidth(-FLT_MIN);
               rangeChanged |= TypableRangeField("##lo", loKey, &lo, step, minV, maxV,
                                                 isInt ? "%.0f" : "%.3f", /*noBorder=*/true);

               ImGui::TableNextColumn();
               ImGui::SetNextItemWidth(-FLT_MIN);
               rangeChanged |= TypableRangeField("##hi", hiKey, &hi, step, minV, maxV,
                                                 isInt ? "%.0f" : "%.3f", /*noBorder=*/true);

               if (rangeChanged)
               {
                  lo = std::clamp(lo, minV, maxV);
                  hi = std::clamp(hi, minV, maxV);
                  if (isInt)
                  {
                     lo = std::round(lo);
                     hi = std::round(hi);
                  }
                  mod.SetRange(dstIndex, dstParam, lo, hi);
               }

               // Invert - matches the ##modbind popup's own Invert, which
               // doesn't checkpoint either (a range edit, not a structural
               // change like Unbind or the enable toggle above).
               ImGui::TableNextColumn();
               if (ImGui::SmallButton(L("Inv")))
                  mod.SetRange(dstIndex, dstParam, src.hi, src.lo);

               // Curve
               ImGui::TableNextColumn();
               float curveVal = src.curve;
               float liveIn01 = -1.0f;
               if (src.nodeIndex >= 0 && srcNode != nullptr && srcNode->node != nullptr)
               {
                  if (IModulator* modulator = ModulatorForOutput(srcNode->node.get(), src.outputIndex))
                     liveIn01 = std::clamp(modulator->Value01(), 0.0f, 1.0f);
               }
               if (DrawMiniCurveWidget("##modcurve", &curveVal, liveIn01, wCurve))
                  mod.SetCurve(dstIndex, dstParam, curveVal);

               // Real-time Sparkline
               ImGui::TableNextColumn();
               float liveSig01 = (liveIn01 >= 0.0f) ? liveIn01 : 0.5f;
               if (liveIn01 < 0.0f && frameRef != nullptr && frameRef->value != nullptr && hi != lo)
                  liveSig01 = std::clamp((*frameRef->value - lo) / (hi - lo), 0.0f, 1.0f);
               auto& hist = gModMatrixSparklines[{dstIndex, dstParam}];
               hist.Push(liveSig01);
               DrawSparklineMiniGraph("##sigspark", hist, isPred ? tok::U32(tok::prediction) : tok::U32(tok::modulation),
                                      vertical ? 42.0f : 55.0f, ImGui::GetFrameHeight());

               // Unbind
               ImGui::TableNextColumn();
               {
                  // At-rest fill is transparent rather than the theme's
                  // opaque Button colour (t.panelBg): this button sits inside
                  // a table with TableRowBgAlt zebra striping (added this
                  // same HIG pass), and an opaque fill that happens to match
                  // the base row exactly turns into a visible "chip" outline
                  // on every alt row only, once the row bg tints away from
                  // panelBg - the exact row-to-row inconsistency this was
                  // reported for. Transparent means the button always reads
                  // as a bare icon, matching the "icon-only" intent this
                  // control was converted to, regardless of which stripe or
                  // theme it's drawn over. Hover/active keep the theme's
                  // usual button colours (free per P10, and how every other
                  // icon-button in the app already behaves).
                  ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::transparent));
                  const float btnW = ImGui::GetFrameHeight();
                  if (ImGui::Button("##unbindmod", ImVec2(btnW, 0)))
                  {
                     PushUndoCheckpoint();
                     mod.Unbind(dstIndex, dstParam);
                     unbound = true;
                  }
                  ImGui::PopStyleColor();
                  ImDrawList* dl = ImGui::GetWindowDrawList();
                  const ImVec2 bmin = ImGui::GetItemRectMin();
                  const ImVec2 bmax = ImGui::GetItemRectMax();
                  const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
                  const float iconSize = (bmax.y - bmin.y) * 0.6f;
                  // Unhovered colour blends TextDisabled toward Text rather
                  // than using TextDisabled at full strength: the zebra alt
                  // row (t.text at low alpha) sits closer to TextDisabled's
                  // own luminance than the base row does in some presets, so
                  // a pure TextDisabled X could read fine on one stripe and
                  // wash out on the other. The 60/40 blend keeps the same
                  // "quiet until scanned for" budget on both.
                  const ImVec4 disabled4 = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                  const ImVec4 text4 = ImGui::GetStyle().Colors[ImGuiCol_Text];
                  const ImU32 idleCol = IM_COL32(
                     (int)((disabled4.x * 0.6f + text4.x * 0.4f) * 255.0f),
                     (int)((disabled4.y * 0.6f + text4.y * 0.4f) * 255.0f),
                     (int)((disabled4.z * 0.6f + text4.z * 0.4f) * 255.0f),
                     255);
                  const ImU32 col = ImGui::IsItemHovered() ? tok::U32(tok::danger_hover) : idleCol;
                  glyph::DrawX(dl, center, iconSize, col);
               }

               ImGui::PopID();

               if (unbound)
                  break; // just erased from the map this loop is iterating
            }

            // Expression bindings - same table, driven by a typed formula
            // instead of a wired modulator. A param carrying both (a wired
            // modulator always wins the apply pass - see Modulation.h) has
            // its own row above already and is skipped here to avoid
            // listing it twice.
            for (const auto& exprEntry : mod.Expressions())
            {
               const int dstIndex = exprEntry.first.first;
               const int dstParam = exprEntry.first.second;
               if (mod.IsModulated(dstIndex, dstParam))
                  continue;
               GraphNode* dstNode = FindNodeByIndex(dstIndex);
               if (dstNode == nullptr)
                  continue; // stale - deleted node, undo/redo rewound past it

               ImGui::PushID(dstIndex * 1000 + dstParam + 2000000);
               ImGui::TableNextRow();

               // Enable dot: purple for expression
               ImGui::TableNextColumn();
               const ImVec2 dotCursor = ImGui::GetCursorScreenPos();
               const float dotH = ImGui::GetTextLineHeight();
               ImGui::Dummy(ImVec2(dotH, dotH));
               ImGui::GetWindowDrawList()->AddCircleFilled(
                  ImVec2(dotCursor.x + dotH * 0.5f, dotCursor.y + dotH * 0.5f), dotH * 0.35f, tok::U32(tok::expression));

               ImGui::TableNextColumn();
               ImGui::TextUnformatted("Expression");

               ImGui::TableNextColumn();
               if (ImGui::Selectable(NodeTitleWithInstance(*dstNode).c_str(), false))
                  gPendingSelect.push_back(dstNode->NodeId());

               const ParamRef* known = mod.KnownParam(dstIndex, dstParam);
               const bool isIntE = known != nullptr && known->step > 0.0f;
               ImGui::TableNextColumn();
               ImGui::TextUnformatted(known != nullptr ? known->name.c_str() : "?");

               const ParamRef* frameRef = nullptr;
               for (const ParamRef& r : mod.FrameParams())
               {
                  if (r.nodeIndex == dstIndex && r.paramIndex == dstParam)
                  {
                     frameRef = &r;
                     break;
                  }
               }
               ImGui::TableNextColumn();
               if (frameRef != nullptr && frameRef->value != nullptr)
                  ImGui::Text(isIntE ? "%.0f" : "%.3f", *frameRef->value);
               else
                  ImGui::TextUnformatted("--");

               ImGui::TableNextColumn();
               ImGui::TextUnformatted("--");

               const float minVE = known != nullptr ? known->minValue : 0.0f;
               const float maxVE = known != nullptr ? known->maxValue : 1.0f;
               float loE, hiE;
               if (!mod.ExpressionRangeFor(dstIndex, dstParam, loE, hiE))
               {
                  loE = minVE;
                  hiE = maxVE;
               }
               const float stepE = isIntE ? 1.0f : std::max(0.0001f, (maxVE - minVE) * 0.01f);
               bool rangeChangedE = false;
               const std::pair<int, int> loKeyE(dstIndex, -(dstParam * 2 + 1) - 3000000);
               const std::pair<int, int> hiKeyE(dstIndex, -(dstParam * 2 + 2) - 3000000);

               ImGui::TableNextColumn();
               ImGui::SetNextItemWidth(-FLT_MIN);
               rangeChangedE |= TypableRangeField("##elo", loKeyE, &loE, stepE, minVE, maxVE,
                                                  isIntE ? "%.0f" : "%.3f", /*noBorder=*/true);
               ImGui::TableNextColumn();
               ImGui::SetNextItemWidth(-FLT_MIN);
               rangeChangedE |= TypableRangeField("##ehi", hiKeyE, &hiE, stepE, minVE, maxVE,
                                                  isIntE ? "%.0f" : "%.3f", /*noBorder=*/true);
               if (rangeChangedE)
               {
                  loE = std::clamp(loE, minVE, maxVE);
                  hiE = std::clamp(hiE, minVE, maxVE);
                  mod.SetExpressionRange(dstIndex, dstParam, loE, hiE);
               }

               ImGui::TableNextColumn(); // invert - not meaningful for an expression's range

               // Curve
               ImGui::TableNextColumn();
               float exprCurveVal = mod.ExpressionCurveFor(dstIndex, dstParam);
               float liveExpr01 = -1.0f;
               if (frameRef != nullptr && frameRef->value != nullptr && hiE != loE)
                  liveExpr01 = std::clamp((*frameRef->value - loE) / (hiE - loE), 0.0f, 1.0f);
               if (DrawMiniCurveWidget("##exprcurve", &exprCurveVal, liveExpr01, wCurve))
                  mod.SetExpressionCurve(dstIndex, dstParam, exprCurveVal);

               // Real-time Sparkline
               ImGui::TableNextColumn();
               float liveExprSig01 = (liveExpr01 >= 0.0f) ? liveExpr01 : 0.5f;
               auto& histE = gModMatrixSparklines[{dstIndex, dstParam + 2000000}];
               histE.Push(liveExprSig01);
               DrawSparklineMiniGraph("##exprspark", histE, tok::U32(tok::expression),
                                      vertical ? 42.0f : 55.0f, ImGui::GetFrameHeight());

               bool unboundExpr = false;
               ImGui::TableNextColumn();
               {
                  ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::transparent));
                  const float btnW = ImGui::GetFrameHeight();
                  if (ImGui::Button("##unbindexpr", ImVec2(btnW, 0)))
                  {
                     PushUndoCheckpoint();
                     mod.ClearExpression(dstIndex, dstParam);
                     unboundExpr = true;
                  }
                  ImGui::PopStyleColor();
                  ImDrawList* dl = ImGui::GetWindowDrawList();
                  const ImVec2 bmin = ImGui::GetItemRectMin();
                  const ImVec2 bmax = ImGui::GetItemRectMax();
                  const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
                  const float iconSize = (bmax.y - bmin.y) * 0.6f;
                  const ImU32 col = ImGui::IsItemHovered() ? tok::U32(tok::danger_hover)
                                                            : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                  glyph::DrawX(dl, center, iconSize, col);
               }

               ImGui::PopID();
               if (unboundExpr)
                  break; // just erased from the map this loop is iterating
            }

            // Recorded (gesture-looped) bindings - a param armed via "Start
            // Recording" or still looping a finished gesture. A param whose
            // recording is only armed but hasn't produced a loop yet has
            // nothing here to show a Value/Range for, so it's skipped - the
            // param's own right-click menu already surfaces "Waiting for
            // movement.../Cancel Recording" for that transient state.
            for (const auto& pbEntry : rec.Playbacks())
            {
               const int dstIndex = pbEntry.first.first;
               const int dstParam = pbEntry.first.second;
               if (mod.IsModulated(dstIndex, dstParam) || mod.HasExpression(dstIndex, dstParam))
                  continue; // one of the two above already wins the apply pass and has its own row
               GraphNode* dstNode = FindNodeByIndex(dstIndex);
               if (dstNode == nullptr)
                  continue; // stale - deleted node, undo/redo rewound past it

               ImGui::PushID(dstIndex * 1000 + dstParam + 4000000);
               ImGui::TableNextRow();

               // Enable dot: red for recording
               ImGui::TableNextColumn();
               const ImVec2 dotCursor = ImGui::GetCursorScreenPos();
               const float dotH = ImGui::GetTextLineHeight();
               ImGui::Dummy(ImVec2(dotH, dotH));
               ImGui::GetWindowDrawList()->AddCircleFilled(
                  ImVec2(dotCursor.x + dotH * 0.5f, dotCursor.y + dotH * 0.5f), dotH * 0.35f, tok::U32(tok::record));

               ImGui::TableNextColumn();
               ImGui::TextUnformatted("Recording");

               ImGui::TableNextColumn();
               if (ImGui::Selectable(NodeTitleWithInstance(*dstNode).c_str(), false))
                  gPendingSelect.push_back(dstNode->NodeId());

               const ParamRef* known = mod.KnownParam(dstIndex, dstParam);
               const bool isIntR = known != nullptr && known->step > 0.0f;
               ImGui::TableNextColumn();
               ImGui::TextUnformatted(known != nullptr ? known->name.c_str() : "?");

               const ParamRef* frameRef = nullptr;
               for (const ParamRef& r : mod.FrameParams())
               {
                  if (r.nodeIndex == dstIndex && r.paramIndex == dstParam)
                  {
                     frameRef = &r;
                     break;
                  }
               }
               ImGui::TableNextColumn();
               if (frameRef != nullptr && frameRef->value != nullptr)
                  ImGui::Text(isIntR ? T("%.0f (%.2fx)") : T("%.3f (%.2fx)"), *frameRef->value,
                              rec.PlaybackSpeedFor(dstIndex, dstParam));
               else
                  ImGui::TextUnformatted("--");

               ImGui::TableNextColumn();
               ImGui::TextUnformatted("--");

               const float minVR = known != nullptr ? known->minValue : pbEntry.second.recordedMin;
               const float maxVR = known != nullptr ? known->maxValue : pbEntry.second.recordedMax;
               float loR, hiR;
               if (!rec.PlaybackRangeFor(dstIndex, dstParam, loR, hiR))
               {
                  loR = pbEntry.second.recordedMin;
                  hiR = pbEntry.second.recordedMax;
               }
               const float stepR = isIntR ? 1.0f : std::max(0.0001f, (maxVR - minVR) * 0.01f);
               bool rangeChangedR = false;
               const std::pair<int, int> loKeyR(dstIndex, -(dstParam * 2 + 1) - 5000000);
               const std::pair<int, int> hiKeyR(dstIndex, -(dstParam * 2 + 2) - 5000000);

               ImGui::TableNextColumn();
               ImGui::SetNextItemWidth(-FLT_MIN);
               rangeChangedR |= TypableRangeField("##rlo", loKeyR, &loR, stepR, minVR, maxVR,
                                                  isIntR ? "%.0f" : "%.3f", /*noBorder=*/true);
               ImGui::TableNextColumn();
               ImGui::SetNextItemWidth(-FLT_MIN);
               rangeChangedR |= TypableRangeField("##rhi", hiKeyR, &hiR, stepR, minVR, maxVR,
                                                  isIntR ? "%.0f" : "%.3f", /*noBorder=*/true);
               if (rangeChangedR)
               {
                  loR = std::clamp(loR, minVR, maxVR);
                  hiR = std::clamp(hiR, minVR, maxVR);
                  rec.SetPlaybackRange(dstIndex, dstParam, loR, hiR);
               }

               ImGui::TableNextColumn();
               if (ImGui::SmallButton(L("Full")))
                  rec.ClearPlaybackRange(dstIndex, dstParam);

               // Curve
               ImGui::TableNextColumn();
               float recCurveVal = rec.PlaybackCurveFor(dstIndex, dstParam);
               float liveRec01 = -1.0f;
               if (frameRef != nullptr && frameRef->value != nullptr && hiR != loR)
                  liveRec01 = std::clamp((*frameRef->value - loR) / (hiR - loR), 0.0f, 1.0f);
               if (DrawMiniCurveWidget("##reccurve", &recCurveVal, liveRec01, wCurve))
                  rec.SetPlaybackCurve(dstIndex, dstParam, recCurveVal);

               // Real-time Sparkline
               ImGui::TableNextColumn();
               float liveRecSig01 = (liveRec01 >= 0.0f) ? liveRec01 : 0.5f;
               auto& histR = gModMatrixSparklines[{dstIndex, dstParam + 4000000}];
               histR.Push(liveRecSig01);
               DrawSparklineMiniGraph("##recspark", histR, tok::U32(tok::record),
                                      vertical ? 42.0f : 55.0f, ImGui::GetFrameHeight());

               bool unboundRec = false;
               ImGui::TableNextColumn();
               {
                  ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::transparent));
                  const float btnW = ImGui::GetFrameHeight();
                  if (ImGui::Button("##unbindrec", ImVec2(btnW, 0)))
                  {
                     rec.StopPlayback(dstIndex, dstParam);
                     unboundRec = true;
                  }
                  ImGui::PopStyleColor();
                  ImDrawList* dl = ImGui::GetWindowDrawList();
                  const ImVec2 bmin = ImGui::GetItemRectMin();
                  const ImVec2 bmax = ImGui::GetItemRectMax();
                  const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
                  const float iconSize = (bmax.y - bmin.y) * 0.6f;
                  const ImU32 col = ImGui::IsItemHovered() ? tok::U32(tok::danger_hover)
                                                            : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                  glyph::DrawX(dl, center, iconSize, col);
               }

               ImGui::PopID();
               if (unboundRec)
                  break; // just erased from the map this loop is iterating
            }

            // Grid lines only run as far down as there are real rows to draw
            // them between - the user wants the horizontal lines filled in
            // the rest of the way regardless, like a spreadsheet, rather
            // than having the ruled area grow every time a binding is added.
            //
            // Everything here is in window-LOCAL coordinates (GetCursorPosY
            // adds the window's own scroll back in), never screen ones. A
            // screen-space target makes the row count a function of how far
            // the user has scrolled: scrolling down slides the cursor up the
            // screen while the target stays pinned, so the loop appends one
            // more row per scroll step, which lengthens the scroll range,
            // which lets the user scroll further - an endlessly growing
            // table, which is exactly the "infinite scroll" bug this
            // replaced. In local coordinates the count depends only on the
            // panel's fixed height, so it is identical at every scroll
            // offset.
            //
            // Row height is measured from the rows actually emitted rather
            // than estimated - a real data row (carrying a DragFloat and a
            // SmallButton) is taller than a plain empty one, so a static
            // estimate undershot and left an unruled gap at the bottom.
            const float scrollbarH =
               ImGui::GetScrollMaxX() > 0.0f ? ImGui::GetStyle().ScrollbarSize : 0.0f;
            const float visibleH = ImGui::GetWindowHeight() - scrollbarH;
            float rowH = ImGui::GetTextLineHeightWithSpacing() +
                         ImGui::GetStyle().CellPadding.y * 2.0f;
            int guard = 0;
            while (ImGui::GetCursorPosY() + rowH <= visibleH && guard++ < 500)
            {
               const float before = ImGui::GetCursorPosY();
               ImGui::TableNextRow();
               for (int col = 0; col < 12; ++col)
               {
                  ImGui::TableNextColumn();
                  ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
               }
               const float after = ImGui::GetCursorPosY();
               if (after <= before)
                  break; // cursor stalled - never spin
               rowH = after - before;
            }

            // Geometry probe for INFINITE_MODMATRIXGEOM (see the assertions
            // near the other frame-driven fixtures): the filler count and
            // the resulting scroll range must not depend on scroll offset.
            if (getenv("INFINITE_MODMATRIXGEOM") != nullptr)
            {
               gModMatrixFillRows = guard;
               gModMatrixScrollMax = ImGui::GetScrollMaxY();
               ImGui::SetScrollY(ImGui::GetScrollMaxY()); // pin to the bottom
            }

            ImGui::EndTable();
         }
      }

      // Right-click anywhere on the panel - empty space, the table, or a
      // row - to reposition or close it. A plain rect test against the
      // mouse, not BeginPopupContextWindow: a scrolling table owns a nested
      // child window, so a context-window helper attached here would only
      // see hover in the gaps around it, not over the table itself - see
      // DrawViewportPanelContainer's identical comment.
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const bool overPanel = mouse.x >= panelOrigin.x && mouse.x < panelOrigin.x + panelSize.x &&
                             mouse.y >= panelOrigin.y && mouse.y < panelOrigin.y + panelSize.y;
      if (overPanel && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
         ImGui::OpenPopup("##modmatrixctx");

      if (ImGui::BeginPopup("##modmatrixctx"))
      {
         static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top") };
         for (int i = 0; i < 4; i++)
            if (ImGui::MenuItem(L(kDockLabels[i]), nullptr, i == gModMatrixDock))
               gModMatrixDock = i;
         ImGui::Separator();
         if (ImGui::MenuItem(L("Close panel")))
            gModMatrixOpen = false;
         ImGui::EndPopup();
      }
   }


   // The panel's outer frame at every dock position - identical grip/border
   // structure to DrawViewportPanelDocked, just keyed off the matrix's own
   // dock/size globals and calling DrawModMatrixTable for its content.
   void DrawModMatrixDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = 6.0f;
      const int dock = gModMatrixDock;
      const bool vertical = (dock == 1 || dock == 2);  // grip is a column, not a row
      const bool gripFirst = (dock == 0 || dock == 1); // canvas is above / to the left

      // The outer child is what actually holds the resize-grip strip and the
      // spacing between it and the content child - and ImGuiCol_ChildBg is
      // alpha 0 by default (see ApplyTheme), so that ~11px band was showing
      // straight THROUGH the UI to whatever happens to be painted behind it.
      // That is the "black in light mode, grey in dark mode" band between
      // viewports: it was never a coloured divider, it was a hole. Give the
      // outer child the same opaque panelBg fill the content child already
      // has so the panel is solid edge to edge.
      PushDockedPanelStyle(/*isChild=*/true);
      ImGui::BeginChild(id, size, false);
      PopDockedPanelStyle();
      const ImVec2 inner = ImGui::GetContentRegionAvail();

      auto grip = [&]()
      {
         ImGui::InvisibleButton("##modmatrixgrip",
                                vertical ? ImVec2(kGrip, std::max(1.0f, inner.y))
                                         : ImVec2(std::max(1.0f, inner.x), kGrip));
         // The panel's canvas-facing boundary, and the only thing that marks
         // it: one hairline, same colour and same 1px weight at every dock
         // edge (see DrawPanelSeam).
         DrawPanelSeam(vertical, gripFirst);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsItemActive())
         {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            switch (dock)
            {
               case 0: gModMatrixHeight -= d.y; break;
               case 1: gModMatrixWidth -= d.x; break;
               case 2: gModMatrixWidth += d.x; break;
               default: gModMatrixHeight += d.y; break;
            }
            gModMatrixWidth = std::max(kModMatrixMinWidth, gModMatrixWidth);
            gModMatrixHeight = std::max(kModMatrixMinHeight, gModMatrixHeight);
         }
      };

      if (gripFirst)
      {
         grip();
         if (vertical)
            ImGui::SameLine();
      }

      // Both branches now pass an explicit, fixed height rather than 0
      // ("auto-fit to content") for the vertical case: an auto-height
      // child here feeds into DrawModMatrixTable's ScrollY table, which
      // itself auto-sizes to "available" space when given no outer_size.y -
      // the two auto-sizes compound as filler rows are added (each row
      // grows the child, which grows the table's available region, which
      // lets it add more rows), spinning the fill-to-bottom loop out to
      // its guard cap and producing a vastly oversized scroll area. A
      // fixed height for both orientations keeps that loop's target
      // stable.
      const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
      PushDockedPanelStyle(/*isChild=*/true);
      // ChildBorderSize is 0 app-wide now (submenus need it, see ApplyTheme),
      // and ImGui auto-zeroes a bordered child's WindowPadding whenever its
      // resolved border size is 0 - so plain `true` here silently lost this
      // panel's inner padding along with the border it no longer draws.
      // AlwaysUseWindowPadding opts back into the real padding regardless.
      ImGui::BeginChild("##modmatrixpanelcontent",
                        vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), inner.y)
                                 : ImVec2(0, std::max(1.0f, inner.y - kGrip - gap.y)),
                        ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawModMatrixTable();
      ImGui::EndChild();
      PopDockedPanelStyle();

      if (!gripFirst)
      {
         if (vertical)
            ImGui::SameLine();
         grip();
      }
      // Zeroed only around EndChild, which is where ImGui charges the gap
      // that follows this panel - the panel's own contents above keep normal
      // spacing. Without this, the ItemSpacing between the panel and whatever
      // is laid out next shows a strip of the shell window's windowBg, which
      // reads as a bar separating the two viewports.
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
      ImGui::EndChild();
      ImGui::PopStyleVar();

      // No divider line along the canvas-facing edge, in either theme. This
      // hairline was a fixed dark constant, then a theme-derived one, and was
      // reported as a wrong-coloured seam both times - like the dialog border
      // above, it straddles two different backgrounds (panel fill on one side,
      // canvas on the other), so no single colour is right against both and
      // every theme change re-breaks it. The panel already reads as separate
      // because its opaque panelBg fill differs from the canvas' windowBg;
      // the line added no information. Removed rather than re-tuned.
   }
}
