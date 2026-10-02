// Infinite-Turbo 0.46: Performance Mode, ported from upstream Infinite
// (n1m21n/Infinite, main.cpp "Performance Matrix"). A dockable grid of
// controls (knobs, faders, sliders, toggles, XY pads, triggers, number boxes,
// selectors, bipolar knobs, step gates) on pages, each bound to any node
// parameter (or several), with MIDI learn per control. Saved with the patch
// (perfui / perfname / perf / perftarget / perfmidi lines, as upstream).
//
// Included by main.cpp inside its anonymous namespace. Adaptations: Turbo's
// ParamRef carries a `taper` id instead of function pointers (TaperP2V /
// TaperV2P map it back), and MIDI device ids are already stable on Windows.

FaderPosToValueFn TaperP2V(int taper)
{
   return taper == 1 ? &ConsoleFaderTaper::PosToValue : taper == 2 ? &FrequencyTaper::PosToValue : nullptr;
}
FaderValueToPosFn TaperV2P(int taper)
{
   return taper == 1 ? &ConsoleFaderTaper::ValueToPos : taper == 2 ? &FrequencyTaper::ValueToPos : nullptr;
}

   // ---- Performance Matrix ----
   void PerfPanelDockCombo()
   {
      static const char* kDockLabels[] = { "Bottom", "Right", "Left", "Top" };
      if (ImGui::BeginCombo("##perfpaneldock", kDockLabels[gPerfPanelDock]))
      {
         for (int i = 0; i < 4; i++)
            if (ImGui::Selectable(kDockLabels[i], i == gPerfPanelDock))
               gPerfPanelDock = i;
         ImGui::EndCombo();
      }
   }

   void MidiLearnCancelAll();

   // Small style helpers upstream's panel uses (same values as upstream).
   inline ImVec4 PerfAccentTint(float amount)
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      return ImVec4(t.panelBg.r + (t.accent.r - t.panelBg.r) * amount,
                    t.panelBg.g + (t.accent.g - t.panelBg.g) * amount,
                    t.panelBg.b + (t.accent.b - t.panelBg.b) * amount, 1.0f);
   }
   inline ImVec4 AccentEmphasisSelected() { return PerfAccentTint(0.60f); }
   inline ImVec4 AccentEmphasisPressed() { return PerfAccentTint(0.78f); }

   inline void PushSliderStyle()
   {
      const bool isLight = IsThemeLight();
      ImGui::PushStyleColor(ImGuiCol_FrameBg, isLight ? ImVec4(0.86f, 0.88f, 0.94f, 1.0f) : ImVec4(0.16f, 0.18f, 0.24f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, isLight ? ImVec4(0.80f, 0.84f, 0.92f, 1.0f) : ImVec4(0.25f, 0.28f, 0.38f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, isLight ? ImVec4(0.74f, 0.78f, 0.88f, 1.0f) : ImVec4(0.32f, 0.36f, 0.48f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_SliderGrab, isLight ? ImVec4(0.20f, 0.55f, 0.95f, 1.0f) : ImVec4(0.55f, 0.82f, 1.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, isLight ? ImVec4(0.14f, 0.45f, 0.85f, 1.0f) : ImVec4(0.70f, 0.90f, 1.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border, isLight ? ImVec4(0.70f, 0.74f, 0.84f, 1.0f) : ImVec4(0.22f, 0.235f, 0.278f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, isLight ? ImVec4(0.15f, 0.18f, 0.24f, 1.0f) : ImVec4(0.88f, 0.92f, 0.98f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, isLight ? 1.0f : 0.0f);
   }
   inline void PopSliderStyle()
   {
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(7);
   }

   inline void PushDockedPanelStyle(bool isChild)
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const ImVec4 bg(t.panelBg.r, t.panelBg.g, t.panelBg.b, IsThemeLight() ? 0.99f : 0.97f);
      ImGui::PushStyleColor(isChild ? ImGuiCol_ChildBg : ImGuiCol_WindowBg, bg);
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
   }
   inline void PopDockedPanelStyle()
   {
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);
   }

   inline void DrawPanelSeam(bool vertical, bool facesStart)
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const bool isLight = IsThemeLight();
      auto step = [isLight](float c) { return isLight ? c * 0.86f : c + (1.0f - c) * 0.10f; };
      const ImU32 col = ImGui::GetColorU32(ImVec4(step(t.windowBg.r), step(t.windowBg.g), step(t.windowBg.b), 1.0f));
      const ImVec2 a = ImGui::GetItemRectMin();
      const ImVec2 b = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (vertical)
      {
         const float x = facesStart ? a.x + 0.5f : b.x - 0.5f;
         dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), col, 1.0f);
      }
      else
      {
         const float y = facesStart ? a.y + 0.5f : b.y - 0.5f;
         dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col, 1.0f);
      }
   }

   // Turbo: the bipolar knob is Turbo's knob on a -1..+1 range.
   inline bool BipolarKnobFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                                float diameter, ImU32 fillColor, bool readOnly, float cellW = 0.0f)
   {
      return KnobFloat(label, value, minV, maxV, fmt, diameter, fillColor, readOnly, cellW);
   }

   ImVec2 GetPerfElementCellSpan(int kind)
   {
      switch (kind)
      {
         case 0: return ImVec2(1.0f, 1.0f); // Knob
         case 1: return ImVec2(1.0f, 2.0f); // VFader
         case 2: return ImVec2(2.0f, 1.0f); // HSlider
         case 3: return ImVec2(1.0f, 1.0f); // Toggle
         case 4: return ImVec2(2.0f, 2.0f); // XYPad
         case 5: return ImVec2(1.0f, 1.0f); // Trigger / Bang
         case 6: return ImVec2(1.0f, 1.0f); // Digital Number Box
         case 7: return ImVec2(2.0f, 1.0f); // Radio Selector
         case 8: return ImVec2(1.0f, 1.0f); // Bipolar Pan Knob
         case 9: return ImVec2(3.0f, 1.0f); // Step Gate Ribbon
         default: return ImVec2(1.0f, 1.0f);
      }
   }

   bool ReadNodeBool(GraphNode* gn, const std::string& boolName, int channelIdx = 0)
   {
      if (gn == nullptr || gn->node == nullptr) return false;
      if (boolName == "bypassed") return gn->node->bypassed;
      struct BoolFinder : public ParamVisitor
      {
         std::string target;
         bool found = false;
         bool val = false;
         void Float(const char*, float&) override {}
         void Int(const char*, int&) override {}
         void Bool(const char* name, bool& v) override { if (target == name) { found = true; val = v; } }
         void Text(const char*, std::string&) override {}
         void Color(const char*, float[3]) override {}
      } finder;
      finder.target = boolName;
      gn->node->VisitParams(finder);
      return finder.found ? finder.val : false;
   }

   void WriteNodeBool(GraphNode* gn, const std::string& boolName, bool newVal, int channelIdx = 0)
   {
      if (gn == nullptr || gn->node == nullptr) return;
      // Turbo: nothing to do (and no undo step) when it already has that value -
      // a held trigger or a MIDI pad would otherwise push one every frame.
      if (ReadNodeBool(gn, boolName, channelIdx) == newVal)
         return;
      PushUndoCheckpoint();
      if (boolName == "bypassed")
      {
         gn->node->bypassed = newVal;
         // ResolvedAudioSource follows `bypassed`, so this changes what an
         // Audio Out cable - and every arrangement clip terminal - actually
         // resolves to. The two ImGui bypass toggles already rebuild; this
         // one (Performance panel bindings, RPC) did not, which made it the
         // one path that could change the audio graph invisibly.
         RebuildAudioTopology();
         return;
      }
      struct BoolMutator : public ParamVisitor
      {
         std::string target;
         bool val = false;
         void Float(const char*, float&) override {}
         void Int(const char*, int&) override {}
         void Bool(const char* name, bool& v) override { if (target == name) v = val; }
         void Text(const char*, std::string&) override {}
         void Color(const char*, float[3]) override {}
      } mutator;
      mutator.target = boolName;
      mutator.val = newVal;
      gn->node->VisitParams(mutator);
   }

   ImU32 GetPerfElementColor(const Patch::PerfRecord& elem, GraphNode* dstNode, bool isLight)
   {
      if (elem.colorR > 0.001f || elem.colorG > 0.001f || elem.colorB > 0.001f)
      {
         return IM_COL32((int)(std::clamp(elem.colorR, 0.0f, 1.0f) * 255.0f),
                         (int)(std::clamp(elem.colorG, 0.0f, 1.0f) * 255.0f),
                         (int)(std::clamp(elem.colorB, 0.0f, 1.0f) * 255.0f), 255);
      }
      if (dstNode != nullptr)
      {
         const CategoryColors::Color& c = CategoryColors::ColorFor(dstNode->category);
         return IM_COL32((int)(c.r * 255.0f), (int)(c.g * 255.0f), (int)(c.b * 255.0f), 255);
      }
      return isLight ? IM_COL32(80, 90, 110, 255) : IM_COL32(140, 150, 175, 255);
   }

   void ReorderPerfPages(int src, int dst)
   {
      if (src == dst || src < 0 || dst < 0 || src >= gPerfLayout.pageCount || dst >= gPerfLayout.pageCount)
         return;
      PushUndoCheckpoint();
      while ((int)gPerfLayout.pageNames.size() < gPerfLayout.pageCount)
         gPerfLayout.pageNames.push_back("Page " + std::to_string((int)gPerfLayout.pageNames.size() + 1));

      std::string movingName = gPerfLayout.pageNames[src];
      gPerfLayout.pageNames.erase(gPerfLayout.pageNames.begin() + src);
      gPerfLayout.pageNames.insert(gPerfLayout.pageNames.begin() + dst, movingName);

      for (auto& el : gPerfElements)
      {
         if (el.page == src)
            el.page = dst;
         else if (src < dst && el.page > src && el.page <= dst)
            el.page--;
         else if (src > dst && el.page >= dst && el.page < src)
            el.page++;
      }
      gPerfActivePage = dst;
   }

   std::pair<int, int> FindAdjustedNonOverlappingCell(int page, int elemIdx, int targetX, int targetY, int spanX, int spanY)
   {
      std::set<std::pair<int, int>> occupied;
      for (size_t i = 0; i < gPerfElements.size(); i++)
      {
         if ((int)i == elemIdx || gPerfElements[i].page != page) continue;
         ImVec2 s = GetPerfElementCellSpan(gPerfElements[i].kind);
         for (int dx = 0; dx < (int)s.x; dx++)
            for (int dy = 0; dy < (int)s.y; dy++)
               occupied.insert({ gPerfElements[i].cellX + dx, gPerfElements[i].cellY + dy });
      }

      auto testFit = [&](int x, int y) -> bool {
         if (x < 0 || y < 0) return false;
         for (int dx = 0; dx < spanX; dx++)
            for (int dy = 0; dy < spanY; dy++)
               if (occupied.count({ x + dx, y + dy }) > 0)
                  return false;
         return true;
      };

      if (testFit(targetX, targetY))
         return { targetX, targetY };

      // Search radiating outwards from (targetX, targetY) for the nearest free cell
      int bestX = targetX, bestY = targetY;
      float bestDistSq = 1e9f;

      for (int dist = 1; dist < 32; dist++)
      {
         for (int dy = -dist; dy <= dist; dy++)
         {
            for (int dx = -dist; dx <= dist; dx++)
            {
               int x = targetX + dx;
               int y = targetY + dy;
               if (x < 0 || y < 0) continue;
               if (testFit(x, y))
               {
                  float d2 = (float)(dx * dx + dy * dy);
                  if (d2 < bestDistSq)
                  {
                     bestDistSq = d2;
                     bestX = x;
                     bestY = y;
                  }
               }
            }
         }
         if (bestDistSq < 1e8f) break;
      }
      return { bestX, bestY };
   }

   void AddToPerformanceMatrix(int nodeIndex, int paramIndex, int kind = 0, const std::string& customLabel = "",
                                int paramIndex2 = -1, const std::string& boolName = "")
   {
      PushUndoCheckpoint();
      Patch::PerfRecord rec;
      rec.kind = kind;
      rec.dstIndex = nodeIndex;
      rec.dstParam = paramIndex;
      rec.dstParam2 = paramIndex2;
      rec.page = gPerfActivePage;
      rec.label = customLabel;
      rec.boolName = boolName;

      // Default label if empty
      if (rec.label.empty())
      {
         if (nodeIndex >= 0 && paramIndex >= 0)
         {
            const ParamRef* pKp = Modulation::Instance().KnownParam(nodeIndex, paramIndex);
            if (pKp != nullptr && !pKp->name.empty())
               rec.label = pKp->name;
         }
         if (rec.label.empty())
         {
            if (kind == 0) rec.label = "Knob";
            else if (kind == 1) rec.label = "Fader";
            else if (kind == 2) rec.label = "Slider";
            else if (kind == 3) rec.label = boolName.empty() ? "Toggle" : boolName;
            else if (kind == 4) rec.label = "XY Pad";
            else if (kind == 5) rec.label = "Trigger";
            else if (kind == 6) rec.label = "NumBox";
            else if (kind == 7) rec.label = "Selector";
            else if (kind == 8) rec.label = "Pan/Detent";
            else if (kind == 9) rec.label = "Step Gate";
         }
      }

      ImVec2 targetSpan = GetPerfElementCellSpan(kind);
      auto [freeX, freeY] = FindAdjustedNonOverlappingCell(gPerfActivePage, -1, 0, 0, (int)targetSpan.x, (int)targetSpan.y);
      rec.cellX = freeX;
      rec.cellY = freeY;

      gPerfElements.push_back(rec);
      gPerfPanelOpen = true;
   }

   void AddPerfElementToCurrentPage(int kind)
   {
      AddToPerformanceMatrix(-1, -1, kind, "", -1, "");
   }

   std::vector<Patch::PerfRecord> PerfSelectedRecords()
   {
      std::vector<Patch::PerfRecord> out;
      for (const size_t i : gPerfSelection)
         if (i < gPerfElements.size())
            out.push_back(gPerfElements[i]);
      return out;
   }

   // Drops copies onto the active page, each one landing on the nearest free
   // cell to where it came from - so a duplicate appears beside its original
   // rather than on top of it, and a paste keeps the shape of what was copied.
   // The new copies become the selection, which is what makes "duplicate,
   // drag, duplicate again" work.
   void PerfPasteRecords(const std::vector<Patch::PerfRecord>& src)
   {
      if (src.empty())
         return;
      PushUndoCheckpoint();
      gPerfSelection.clear();
      for (const Patch::PerfRecord& r : src)
      {
         Patch::PerfRecord copy = r;
         copy.page = gPerfActivePage;
         const ImVec2 span = GetPerfElementCellSpan(copy.kind);
         auto [x, y] = FindAdjustedNonOverlappingCell(gPerfActivePage, -1, r.cellX, r.cellY,
                                                      (int)span.x, (int)span.y);
         copy.cellX = x;
         copy.cellY = y;
         gPerfElements.push_back(copy);
         gPerfSelection.insert(gPerfElements.size() - 1);
      }
   }

   void PerfCopySelection()
   {
      std::vector<Patch::PerfRecord> picked = PerfSelectedRecords();
      if (!picked.empty())
         gPerfClipboard = picked;
   }

   void PerfDuplicateSelection()
   {
      PerfPasteRecords(PerfSelectedRecords());
   }

   // Turbo: element indices shift on any erase, so every index-keyed piece of
   // state is dropped (a pending learn / assign would otherwise hit another control).
   void PerfResetIndexState()
   {
      gPerfMidiLearnIdx = -1;
      gPerfAssigningElemIdx = -1;
      gPerfDragIdx = -1;
      gPerfRenamingElementIdx = -1;
      gPerfMidiRuntimeStates.clear();
      gPerfBangFlash.clear();
      gPerfSelection.clear();
   }

   void PerfDeleteSelection()
   {
      if (gPerfSelection.empty())
         return;
      PushUndoCheckpoint();
      // Back to front, so an erase never shifts an index still to be removed.
      for (auto it = gPerfSelection.rbegin(); it != gPerfSelection.rend(); ++it)
         if (*it < gPerfElements.size())
            gPerfElements.erase(gPerfElements.begin() + (long)*it);
      PerfResetIndexState();
   }

   void PerfSelectAllOnPage()
   {
      gPerfSelection.clear();
      for (size_t i = 0; i < gPerfElements.size(); i++)
         if (gPerfElements[i].page == gPerfActivePage)
            gPerfSelection.insert(i);
   }

   void DrawPerfElement(size_t elemIdx, const ImVec2& gridOrigin, float cellSize, bool& outMouseHovered)
   {
      if (elemIdx >= gPerfElements.size()) return;
      Patch::PerfRecord& elem = gPerfElements[elemIdx];
      GraphNode* dstNode = FindNodeByIndex(elem.dstIndex);
      const bool isLight = IsThemeLight();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      ImVec2 span = GetPerfElementCellSpan(elem.kind);
      const float gap = 8.0f;
      ImVec2 cellPos(gridOrigin.x + elem.cellX * (cellSize + gap) + gap * 0.5f,
                     gridOrigin.y + elem.cellY * (cellSize + gap) + gap * 0.5f);
      ImVec2 cardSize(span.x * cellSize + (span.x - 1.0f) * gap,
                      span.y * cellSize + (span.y - 1.0f) * gap);

      // Snap calculation & live drag position with overlap prevention
      int snapX = elem.cellX;
      int snapY = elem.cellY;
      if (gPerfDragIdx == (int)elemIdx)
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         int rawSnapX = std::max(0, (int)std::round((m.x - gridOrigin.x - gap * 0.5f - cardSize.x * 0.5f) / (cellSize + gap)));
         int rawSnapY = std::max(0, (int)std::round((m.y - gridOrigin.y - gap * 0.5f - 10.0f) / (cellSize + gap)));

         auto [adjX, adjY] = FindAdjustedNonOverlappingCell(elem.page, (int)elemIdx, rawSnapX, rawSnapY, (int)span.x, (int)span.y);
         snapX = adjX;
         snapY = adjY;

         // Draw live snap grid highlight at the non-overlapping target cell
         ImVec2 snapTL(gridOrigin.x + snapX * (cellSize + gap) + gap * 0.5f, gridOrigin.y + snapY * (cellSize + gap) + gap * 0.5f);
         ImVec2 snapBR(snapTL.x + cardSize.x, snapTL.y + cardSize.y);
         dl->AddRectFilled(snapTL, snapBR, IM_COL32(70, 140, 255, 45), 6.0f);
         dl->AddRect(snapTL, snapBR, IM_COL32(90, 180, 255, 220), 6.0f, 0, 2.0f);

         // Floating live card position
         cellPos = ImVec2(gridOrigin.x + gPerfDragOriginCellX * (cellSize + gap) + gap * 0.5f + (m.x - gPerfDragMouseStart.x),
                          gridOrigin.y + gPerfDragOriginCellY * (cellSize + gap) + gap * 0.5f + (m.y - gPerfDragMouseStart.y));
      }

      ImVec2 cardBR(cellPos.x + cardSize.x, cellPos.y + cardSize.y);
      ImGui::PushID((int)elemIdx + 77000);

      // Check mouse hovering card
      if (ImGui::IsMouseHoveringRect(cellPos, cardBR))
         outMouseHovered = true;

      // Card background & theme tint
      ImU32 themeTint = GetPerfElementColor(elem, dstNode, isLight);
      ImU32 cardBg = isLight ? IM_COL32(245, 247, 252, 235) : IM_COL32(22, 25, 33, 240);

      dl->AddRectFilled(cellPos, cardBR, cardBg, 6.0f);
      if (gPerfEditMode)
         dl->AddRect(cellPos, cardBR, dstNode != nullptr ? themeTint : (isLight ? IM_COL32(180, 190, 205, 200) : IM_COL32(65, 72, 88, 200)), 6.0f, 0, 1.2f);
      if (gPerfMidiLearnIdx == (int)elemIdx)
      {
         float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 8.0f);
         dl->AddRect(ImVec2(cellPos.x - 2.0f, cellPos.y - 2.0f), ImVec2(cardBR.x + 2.0f, cardBR.y + 2.0f),
                     IM_COL32(255, (int)(160 + 50 * pulse), 30, 255), 7.0f, 0, 2.5f);
      }
      else if (gPerfEditMode && gPerfSelection.count(elemIdx) > 0)
         dl->AddRect(ImVec2(cellPos.x - 2.0f, cellPos.y - 2.0f), ImVec2(cardBR.x + 2.0f, cardBR.y + 2.0f),
                     isLight ? IM_COL32(30, 110, 220, 255) : IM_COL32(95, 165, 255, 255), 7.0f, 0, 2.0f);
      else
         dl->AddRect(cellPos, cardBR, isLight ? IM_COL32(215, 222, 235, 180) : IM_COL32(42, 46, 58, 180), 6.0f, 0, 1.0f);

      // Title/Label Header
      std::string displayLabel = elem.label;
      if (displayLabel.empty())
      {
         if (elem.kind == 3 && !elem.boolName.empty()) displayLabel = elem.boolName;
         else if (dstNode != nullptr && elem.dstParam >= 0)
         {
            const ParamRef* pKp = Modulation::Instance().KnownParam(elem.dstIndex, elem.dstParam);
            displayLabel = (pKp != nullptr && !pKp->name.empty()) ? pKp->name : ("Param " + std::to_string(elem.dstParam));
         }
         else
         {
            if (elem.kind == 0) displayLabel = "Knob";
            else if (elem.kind == 1) displayLabel = "Fader";
            else if (elem.kind == 2) displayLabel = "Slider";
            else if (elem.kind == 3) displayLabel = "Toggle";
            else if (elem.kind == 4) displayLabel = "XY Pad";
            else if (elem.kind == 5) displayLabel = "Trigger";
            else if (elem.kind == 6) displayLabel = "NumBox";
            else if (elem.kind == 7) displayLabel = "Selector";
            else if (elem.kind == 8) displayLabel = "Pan/Detent";
            else if (elem.kind == 9) displayLabel = "Step Gate";
         }
      }

      // Header background badge
      dl->AddRectFilled(cellPos, ImVec2(cardBR.x, cellPos.y + 18.0f),
                        (themeTint & 0x00FFFFFF) | 0x28000000, 6.0f, ImDrawFlags_RoundCornersTop);
      ImVec2 titlePos(cellPos.x + 6.0f, cellPos.y + 2.0f);
      ImU32 textCol = isLight ? IM_COL32(30, 35, 48, 255) : IM_COL32(225, 230, 245, 255);

      // In Edit Mode, full card invisible button handles right-click popup and dragging
      if (gPerfEditMode)
      {
         ImGui::SetCursorScreenPos(cellPos);
         ImGui::InvisibleButton("##cardbodybtn", cardSize);
         if (ImGui::IsItemHovered())
            outMouseHovered = true;

         // Shift-click extends the selection; a plain click on something
         // already selected leaves the selection alone, so clicking one card
         // of a multi-selection doesn't collapse it before a Copy/Duplicate.
         if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
         {
            if (ImGui::GetIO().KeyShift)
            {
               if (gPerfSelection.count(elemIdx) > 0)
                  gPerfSelection.erase(elemIdx);
               else
                  gPerfSelection.insert(elemIdx);
            }
            else if (gPerfSelection.count(elemIdx) == 0)
            {
               gPerfSelection.clear();
               gPerfSelection.insert(elemIdx);
            }
         }
         if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && gPerfSelection.count(elemIdx) == 0)
         {
            gPerfSelection.clear();
            gPerfSelection.insert(elemIdx);
         }

         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
         {
            if (gPerfDragIdx != (int)elemIdx)
            {
               gPerfDragIdx = (int)elemIdx;
               gPerfDragOriginCellX = elem.cellX;
               gPerfDragOriginCellY = elem.cellY;
               gPerfDragMouseStart = ImGui::GetIO().MousePos;
            }
         }
         if (gPerfDragIdx == (int)elemIdx && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         {
            PushUndoCheckpoint();
            elem.cellX = snapX;
            elem.cellY = snapY;
            gPerfDragIdx = -1;
         }

         ImGui::SetNextWindowSizeConstraints(ImVec2(180.0f, 0.0f), ImVec2(240.0f, 480.0f));
         if (ImGui::BeginPopupContextItem("##elemcontext", ImGuiPopupFlags_MouseButtonRight))
         {
            if (ImGui::MenuItem("Rename"))
            {
               gPerfRenamingElementIdx = (int)elemIdx;
               snprintf(gPerfRenameElementBuffer, sizeof(gPerfRenameElementBuffer), "%s", displayLabel.c_str());
            }

            if (ImGui::BeginMenu("Type"))
            {
               if (ImGui::MenuItem("Knob (1x1)", nullptr, elem.kind == 0)) { PushUndoCheckpoint(); elem.kind = 0; }
               if (ImGui::MenuItem("Vertical Fader (1x2)", nullptr, elem.kind == 1)) { PushUndoCheckpoint(); elem.kind = 1; }
               if (ImGui::MenuItem("Horizontal Slider (2x1)", nullptr, elem.kind == 2)) { PushUndoCheckpoint(); elem.kind = 2; }
               if (ImGui::MenuItem("Toggle (1x1)", nullptr, elem.kind == 3)) { PushUndoCheckpoint(); elem.kind = 3; }
               if (ImGui::MenuItem("XY Pad (2x2)", nullptr, elem.kind == 4)) { PushUndoCheckpoint(); elem.kind = 4; }
               if (ImGui::MenuItem("Trigger / Bang (1x1)", nullptr, elem.kind == 5)) { PushUndoCheckpoint(); elem.kind = 5; }
               if (ImGui::MenuItem("Number Box (1x1)", nullptr, elem.kind == 6)) { PushUndoCheckpoint(); elem.kind = 6; }
               if (ImGui::MenuItem("Radio Selector (2x1)", nullptr, elem.kind == 7)) { PushUndoCheckpoint(); elem.kind = 7; }
               if (ImGui::MenuItem("Bipolar Knob (1x1)", nullptr, elem.kind == 8)) { PushUndoCheckpoint(); elem.kind = 8; }
               if (ImGui::MenuItem("Step Gate (3x1)", nullptr, elem.kind == 9)) { PushUndoCheckpoint(); elem.kind = 9; }
               ImGui::EndMenu();
            }

            ImGui::Separator();
            if (elem.kind == 4) // XY Pad
            {
               if (ImGui::MenuItem("Assign X Axis..."))
               {
                  gPerfAssigningElemIdx = (int)elemIdx;
                  gPerfAssigningAxis = 0;
                  ImGui::CloseCurrentPopup();
               }
               if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               {
                  GraphNode* gnX = FindNodeByIndex(elem.dstIndex);
                  const ParamRef* pKpX = Modulation::Instance().KnownParam(elem.dstIndex, elem.dstParam);
                  std::string xStr = "X: " + (gnX ? gnX->typeName : "Node") + " - " + (pKpX ? pKpX->name : std::to_string(elem.dstParam));
                  ImGui::TextDisabled("%s", xStr.c_str());
               }

               if (ImGui::MenuItem("Assign Y Axis..."))
               {
                  gPerfAssigningElemIdx = (int)elemIdx;
                  gPerfAssigningAxis = 1;
                  ImGui::CloseCurrentPopup();
               }
               int pY = elem.dstParam2 >= 0 ? elem.dstParam2 : -1;
               if (elem.dstIndex >= 0 && pY >= 0)
               {
                  GraphNode* gnY = FindNodeByIndex(elem.dstIndex);
                  const ParamRef* pKpY = Modulation::Instance().KnownParam(elem.dstIndex, pY);
                  std::string yStr = "Y: " + (gnY ? gnY->typeName : "Node") + " - " + (pKpY ? pKpY->name : std::to_string(pY));
                  ImGui::TextDisabled("%s", yStr.c_str());
               }

               if (elem.dstIndex >= 0 && ImGui::MenuItem("Clear Destinations"))
               {
                  PushUndoCheckpoint();
                  elem.dstIndex = -1;
                  elem.dstParam = -1;
                  elem.dstParam2 = -1;
                  elem.targets.clear();
                  elem.targetsY.clear();
               }

               ImGui::Separator();
               // MIDI Learn for XY Pad (X Axis and Y Axis)
               const bool isLearningX = (gPerfMidiLearnIdx == (int)elemIdx && gPerfMidiLearnAxis == 0);
               const bool isLearningY = (gPerfMidiLearnIdx == (int)elemIdx && gPerfMidiLearnAxis == 1);
               if (isLearningX)
               {
                  if (ImGui::MenuItem("Listening X... (Move MIDI CC)"))
                     gPerfMidiLearnIdx = -1;
               }
               else
               {
                  std::string xLabel = (elem.midiDevice != 0) ? "Re-learn MIDI X Axis..." : "MIDI Learn X Axis...";
                  if (ImGui::MenuItem(xLabel.c_str()))
                  {
                     std::string err;
                     Platform::MidiStart(err);
                     Platform::MidiCCValue flush;
                     while (Platform::MidiPollLastTouched(flush)) {}
                     MidiLearnCancelAll();
                     gPerfMidiLearnIdx = (int)elemIdx;
                     gPerfMidiLearnAxis = 0;
                     ImGui::CloseCurrentPopup();
                  }
               }
               if (elem.midiDevice != 0)
               {
                  std::string devName = Platform::MidiDeviceName((Platform::MidiDeviceId)elem.midiDevice);
                  std::string bindStr = "MIDI X: " + (devName.empty() ? "" : devName + " \xC2\xB7 ") + "Ch " + std::to_string(elem.midiChannel + 1) + " \xC2\xB7 " + (elem.midiIsNote ? "Note " : "CC ") + std::to_string(elem.midiController);
                  ImGui::TextDisabled("%s", bindStr.c_str());
               }

               if (isLearningY)
               {
                  if (ImGui::MenuItem("Listening Y... (Move MIDI CC)"))
                     gPerfMidiLearnIdx = -1;
               }
               else
               {
                  std::string yLabel = (elem.midiDeviceY != 0) ? "Re-learn MIDI Y Axis..." : "MIDI Learn Y Axis...";
                  if (ImGui::MenuItem(yLabel.c_str()))
                  {
                     std::string err;
                     Platform::MidiStart(err);
                     Platform::MidiCCValue flush;
                     while (Platform::MidiPollLastTouched(flush)) {}
                     MidiLearnCancelAll();
                     gPerfMidiLearnIdx = (int)elemIdx;
                     gPerfMidiLearnAxis = 1;
                     ImGui::CloseCurrentPopup();
                  }
               }
               if (elem.midiDeviceY != 0)
               {
                  std::string devName = Platform::MidiDeviceName((Platform::MidiDeviceId)elem.midiDeviceY);
                  std::string bindStr = "MIDI Y: " + (devName.empty() ? "" : devName + " \xC2\xB7 ") + "Ch " + std::to_string(elem.midiChannelY + 1) + " \xC2\xB7 " + (elem.midiIsNoteY ? "Note " : "CC ") + std::to_string(elem.midiControllerY);
                  ImGui::TextDisabled("%s", bindStr.c_str());
               }

               if ((elem.midiDevice != 0 || elem.midiDeviceY != 0) && ImGui::MenuItem("Clear MIDI Bindings"))
               {
                  PushUndoCheckpoint();
                  elem.midiDevice = 0; elem.midiChannel = -1; elem.midiController = -1; elem.midiIsNote = false;
                  elem.midiDeviceY = 0; elem.midiChannelY = -1; elem.midiControllerY = -1; elem.midiIsNoteY = false;
                  if (gPerfMidiLearnIdx == (int)elemIdx) gPerfMidiLearnIdx = -1;
               }
            }
            else // Regular single-axis control
            {
               if (ImGui::MenuItem("Assign Parameter..."))
               {
                  gPerfAssigningElemIdx = (int)elemIdx;
                  gPerfAssigningAxis = 0;
                  ImGui::CloseCurrentPopup();
               }
               if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               {
                  GraphNode* gn = FindNodeByIndex(elem.dstIndex);
                  const ParamRef* pKp = Modulation::Instance().KnownParam(elem.dstIndex, elem.dstParam);
                  std::string dStr = (gn ? gn->typeName : "Node") + " - " + (pKp ? pKp->name : std::to_string(elem.dstParam));
                  ImGui::TextDisabled("%s", dStr.c_str());
               }

               if (elem.dstIndex >= 0 && ImGui::MenuItem("Clear Destination"))
               {
                  PushUndoCheckpoint();
                  elem.dstIndex = -1;
                  elem.dstParam = -1;
                  elem.boolName.clear();
                  elem.targets.clear();
               }

               if (elem.kind != 9) // Step Gate does not use MIDI Learn
               {
                  ImGui::Separator();
                  const bool isLearning = (gPerfMidiLearnIdx == (int)elemIdx && gPerfMidiLearnAxis == 0);
                  if (isLearning)
                  {
                     std::string learnPrompt = (elem.kind == 3 || elem.kind == 5 || elem.kind == 7)
                        ? "Listening... (Move CC or Hit Pad)"
                        : "Listening... (Move MIDI CC)";
                     if (ImGui::MenuItem(learnPrompt.c_str()))
                        gPerfMidiLearnIdx = -1;
                  }
                  else
                  {
                     std::string midiLabel = (elem.midiDevice != 0) ? "Re-learn MIDI CC" : "MIDI CC Learn";
                     if (elem.kind == 3 || elem.kind == 5 || elem.kind == 7)
                        midiLabel = (elem.midiDevice != 0) ? "Re-learn MIDI (CC / Trigger)" : "MIDI Learn (CC / Trigger)";
                     if (ImGui::MenuItem(midiLabel.c_str()))
                     {
                        std::string err;
                        Platform::MidiStart(err);
                        Platform::MidiCCValue flush;
                        while (Platform::MidiPollLastTouched(flush)) {}
                        MidiLearnCancelAll();
                        gPerfMidiLearnIdx = (int)elemIdx;
                        gPerfMidiLearnAxis = 0;
                        ImGui::CloseCurrentPopup();
                     }
                  }

                  if (elem.midiDevice != 0)
                  {
                     std::string devName = Platform::MidiDeviceName((Platform::MidiDeviceId)elem.midiDevice);
                     std::string bindStr = "MIDI: " + (devName.empty() ? "" : devName + " \xC2\xB7 ") + "Ch " + std::to_string(elem.midiChannel + 1) + " \xC2\xB7 " + (elem.midiIsNote ? "Note " : "CC ") + std::to_string(elem.midiController);
                     ImGui::TextDisabled("%s", bindStr.c_str());

                     if (ImGui::MenuItem("Clear MIDI Binding"))
                     {
                        PushUndoCheckpoint();
                        elem.midiDevice = 0;
                        elem.midiChannel = -1;
                        elem.midiController = -1;
                        elem.midiIsNote = false;
                        if (gPerfMidiLearnIdx == (int)elemIdx) gPerfMidiLearnIdx = -1;
                     }
                  }
               }
            }

            ImGui::Separator();
            if (ImGui::BeginMenu("Color Tint"))
            {
               static const struct { const char* name; ImU32 col; } kPaletteColors[10] = {
                  { "Default", IM_COL32(110, 120, 140, 255) },
                  { "Crimson", IM_COL32(239, 68, 68, 255) },
                  { "Orange",  IM_COL32(249, 115, 22, 255) },
                  { "Amber",   IM_COL32(245, 158, 11, 255) },
                  { "Emerald", IM_COL32(16, 185, 129, 255) },
                  { "Cyan",    IM_COL32(6, 182, 212, 255) },
                  { "Blue",    IM_COL32(59, 130, 246, 255) },
                  { "Purple",  IM_COL32(139, 92, 246, 255) },
                  { "Magenta", IM_COL32(217, 70, 239, 255) },
                  { "Rose",    IM_COL32(244, 63, 94, 255) }
               };

               for (int ci = 0; ci < 10; ci++)
               {
                  if (ci % 5 != 0) ImGui::SameLine();
                  ImGui::PushID(ci + 500);
                  ImVec4 cVec = ImGui::ColorConvertU32ToFloat4(kPaletteColors[ci].col);
                  if (ImGui::ColorButton(kPaletteColors[ci].name, cVec, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24)))
                  {
                     PushUndoCheckpoint();
                     if (ci == 0)
                     {
                        elem.colorR = elem.colorG = elem.colorB = 0.0f;
                     }
                     else
                     {
                        elem.colorR = cVec.x; elem.colorG = cVec.y; elem.colorB = cVec.z;
                     }
                  }
                  if (ImGui::IsItemHovered())
                     ImGui::SetTooltip("%s", kPaletteColors[ci].name);
                  ImGui::PopID();
               }
               ImGui::EndMenu();
            }

            ImGui::EndPopup();
         }
      }

      // Inline renaming or title text
      if (gPerfRenamingElementIdx == (int)elemIdx)
      {
         ImGui::SetCursorScreenPos(ImVec2(cellPos.x + 4.0f, cellPos.y + 1.0f));
         ImGui::SetNextItemWidth(cardSize.x - 8.0f);
         ImGui::SetKeyboardFocusHere();
         if (ImGui::InputText("##renamingelemfield", gPerfRenameElementBuffer, sizeof(gPerfRenameElementBuffer),
                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
         {
            PushUndoCheckpoint();
            elem.label = gPerfRenameElementBuffer;
            gPerfRenamingElementIdx = -1;
         }
         if (ImGui::IsItemDeactivated() && gPerfRenamingElementIdx == (int)elemIdx)
         {
            elem.label = gPerfRenameElementBuffer;
            gPerfRenamingElementIdx = -1;
         }
      }
      else
      {
         dl->PushClipRect(ImVec2(cellPos.x + 4.0f, cellPos.y), ImVec2(cardBR.x - 4.0f, cellPos.y + 18.0f), true);
         dl->AddText(titlePos, textCol, displayLabel.c_str());
         dl->PopClipRect();
      }

      // Header double-click to rename & tooltip when label is truncated
      ImVec2 headerTL = cellPos;
      ImVec2 headerBR(cardBR.x, cellPos.y + 18.0f);
      if (ImGui::IsMouseHoveringRect(headerTL, headerBR))
      {
         if (gPerfRenamingElementIdx != (int)elemIdx && ImGui::CalcTextSize(displayLabel.c_str()).x > (cardSize.x - 12.0f))
            ImGui::SetTooltip("%s", displayLabel.c_str());
         if (gPerfEditMode && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            gPerfRenamingElementIdx = (int)elemIdx;
            snprintf(gPerfRenameElementBuffer, sizeof(gPerfRenameElementBuffer), "%s", displayLabel.c_str());
         }
      }

      // Helper to read current param value
      auto getParamRef = [&](int nIdx, int pIdx, float defaultFallback) -> std::pair<float, ParamRef>
      {
         if (nIdx < 0 || pIdx < 0)
         {
            ParamRef dummy;
            dummy.minValue = 0.0f;
            dummy.maxValue = 1.0f;
            return { defaultFallback, dummy };
         }
         const ParamRef* pKp = Modulation::Instance().KnownParam(nIdx, pIdx);
         ParamRef kp = pKp ? *pKp : ParamRef();
         float currentVal = defaultFallback;
         auto itWrite = gPerfPendingWrites.find({nIdx, pIdx});
         if (itWrite != gPerfPendingWrites.end())
            currentVal = itWrite->second;
         else
         {
            for (const ParamRef& ref : Modulation::Instance().FrameParams())
               if (ref.nodeIndex == nIdx && ref.paramIndex == pIdx && ref.value != nullptr)
               {
                  currentVal = *ref.value;
                  break;
               }
         }
         return { currentVal, kp };
      };

      // A discrete destination - a mode, a checkbox, an int - snaps whatever
      // we write to its own grid, so re-seeding the widget from the
      // destination every frame threw the sub-step part of the drag away. On
      // an 0..7 mode that meant the knob had to travel a full eighth of its
      // arc before the read-back moved at all, which is why a matrix knob
      // driving a dropdown felt stuck or laggy while the macro knob node
      // driving the same param felt smooth: the node keeps its own float and
      // only the destination rounds. These two keep the un-snapped value for
      // as long as the widget is held, and resync from the destination the
      // moment it is released.
      static std::map<size_t, float> sPerfDragRaw;
      auto dragSeed = [&](float liveVal) -> float
      {
         const auto it = sPerfDragRaw.find(elemIdx);
         return it != sPerfDragRaw.end() ? it->second : liveVal;
      };
      auto dragHold = [&](float v)
      {
         if (ImGui::IsItemActive())
            sPerfDragRaw[elemIdx] = v;
         else
            sPerfDragRaw.erase(elemIdx);
      };

      const bool isModulated = elem.dstIndex >= 0 && Modulation::Instance().IsModulated(elem.dstIndex, elem.dstParam);

      if (elem.kind == 0) // Knob (1x1)
      {
         auto [val, kp] = getParamRef(elem.dstIndex, elem.dstParam, elem.value);
         float minV = kp.minValue, maxV = kp.maxValue;
         if (minV >= maxV) { minV = 0.0f; maxV = 1.0f; }
         float diameter = std::max(28.0f, std::min(cellSize * 0.52f, 44.0f));
         float centerX = cellPos.x + cardSize.x * 0.5f;
         float centerY = cellPos.y + 20.0f + (cardSize.y - 20.0f) * 0.44f;
         ImGui::SetCursorScreenPos(ImVec2(centerX - diameter * 0.5f, centerY - diameter * 0.5f));

         FaderPosToValueFn p2v = TaperP2V(kp.taper);
         FaderValueToPosFn v2p = TaperV2P(kp.taper);
         ImU32 fillCol = isModulated ? (isLight ? IM_COL32(215, 125, 20, 255) : IM_COL32(255, 190, 90, 255)) : themeTint;

         float v = dragSeed(val);
         const bool knobMoved = KnobFloat("##knob", &v, minV, maxV, "%.2f", diameter, fillCol, isModulated, diameter, p2v, v2p);
         dragHold(v);
         if (knobMoved)
         {
            elem.value = v;
            if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = v;
            for (const auto& t : elem.targets)
               if (t.dstIndex >= 0 && t.dstParam >= 0)
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = v;
         }
      }
      else if (elem.kind == 1) // VFader (1x2)
      {
         auto [val, kp] = getParamRef(elem.dstIndex, elem.dstParam, elem.value);
         float minV = kp.minValue, maxV = kp.maxValue;
         if (minV >= maxV) { minV = 0.0f; maxV = 1.0f; }
         float faderH = cardSize.y - 30.0f;
         float centerX = cellPos.x + cardSize.x * 0.5f;
         ImGui::SetCursorScreenPos(ImVec2(centerX - 16.0f, cellPos.y + 22.0f));

         const bool isDb = (minV < 0.0f && maxV <= 12.0f && TaperP2V(kp.taper) == ConsoleFaderTaper::PosToValue);
         FaderPosToValueFn p2v = TaperP2V(kp.taper);
         FaderValueToPosFn v2p = TaperV2P(kp.taper);
         ImU32 fillCol = isModulated ? (isLight ? IM_COL32(215, 125, 20, 255) : IM_COL32(255, 190, 90, 255)) : themeTint;

         float v = dragSeed(val);
         const bool faderMoved = VFaderFloat("##vfader", &v, minV, maxV, isDb ? "%.1f dB" : "%.2f", faderH, fillCol, isModulated,
                                             30.0f, p2v, v2p);
         dragHold(v);
         if (faderMoved)
         {
            elem.value = v;
            if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = v;
            for (const auto& t : elem.targets)
               if (t.dstIndex >= 0 && t.dstParam >= 0)
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = v;
         }
      }
      else if (elem.kind == 2) // HSlider (2x1)
      {
         auto [val, kp] = getParamRef(elem.dstIndex, elem.dstParam, elem.value);
         float minV = kp.minValue, maxV = kp.maxValue;
         if (minV >= maxV) { minV = 0.0f; maxV = 1.0f; }
         float sliderW = cardSize.x - 18.0f;
         ImGui::SetCursorScreenPos(ImVec2(cellPos.x + 9.0f, cellPos.y + 20.0f + (cardSize.y - 38.0f) * 0.5f));

         ImU32 fillCol = isModulated ? (isLight ? IM_COL32(215, 125, 20, 255) : IM_COL32(255, 190, 90, 255)) : themeTint;
         float v = dragSeed(val);
         FaderPosToValueFn p2v = TaperP2V(kp.taper);
         FaderValueToPosFn v2p = TaperV2P(kp.taper);
         const bool sliderMoved = AudioSliderFloat("##hslider", &v, minV, maxV, "%.2f", sliderW, fillCol, isModulated);
         dragHold(v);
         if (sliderMoved)
         {
            elem.value = v;
            if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = v;
            for (const auto& t : elem.targets)
               if (t.dstIndex >= 0 && t.dstParam >= 0)
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = v;
         }
      }
      else if (elem.kind == 3) // Toggle (1x1)
      {
         bool curVal = false;
         if (dstNode != nullptr && !elem.boolName.empty())
            curVal = ReadNodeBool(dstNode, elem.boolName, elem.dstParam);
         else if (elem.dstIndex >= 0 && elem.dstParam >= 0)
         {
            // A bool assigned through the pin picker is an ordinary
            // (nodeIndex, paramIndex) param now, so read it back the same way
            // every other element does - the button then follows the node when
            // the node's own checkbox is clicked, instead of drifting.
            auto [boolVal, boolKp] = getParamRef(elem.dstIndex, elem.dstParam, elem.value);
            (void)boolKp;
            curVal = boolVal > 0.5f;
         }
         else
            curVal = elem.value > 0.5f;

         // Sized like the Knob/Bipolar knob's diameter cap (cellSize * 0.52,
         // clamped 28..44) rather than nearly filling the cell - the old
         // cardSize-18/-28 formula left almost no breathing room and read as
         // oversized next to every other control's cell padding.
         float btnSize = std::max(28.0f, std::min(cellSize * 0.52f, 44.0f));
         float centerX = cellPos.x + (cardSize.x - btnSize) * 0.5f;
         float centerY = cellPos.y + 20.0f + (cardSize.y - 20.0f - btnSize) * 0.5f;
         ImGui::SetCursorScreenPos(ImVec2(centerX, centerY));

         ImVec4 btnCol = curVal ? ImVec4((themeTint & 0xFF) / 255.0f,
                                         ((themeTint >> 8) & 0xFF) / 255.0f,
                                         ((themeTint >> 16) & 0xFF) / 255.0f, 1.0f)
                                : (isLight ? ImVec4(0.85f, 0.88f, 0.92f, 1.0f) : ImVec4(0.16f, 0.18f, 0.24f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
         if (ImGui::Button(curVal ? "ON" : "OFF", ImVec2(btnSize, btnSize)))
         {
            bool newVal = !curVal;
            elem.value = newVal ? 1.0f : 0.0f;
            if (dstNode != nullptr && !elem.boolName.empty())
               WriteNodeBool(dstNode, elem.boolName, newVal, elem.dstParam);
            else if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = elem.value;
            for (const auto& t : elem.targets)
            {
               if (t.dstIndex >= 0)
               {
                  GraphNode* gn = FindNodeByIndex(t.dstIndex);
                  if (gn != nullptr && !t.boolName.empty())
                     WriteNodeBool(gn, t.boolName, newVal, t.dstParam);
                  else if (t.dstParam >= 0)
                     gPerfPendingWrites[{t.dstIndex, t.dstParam}] = elem.value;
               }
            }
         }
         ImGui::PopStyleColor();
      }
      else if (elem.kind == 4) // XY Pad (2x2)
      {
         auto [valX, kpX] = getParamRef(elem.dstIndex, elem.dstParam, elem.value);
         int p2 = elem.dstParam2 >= 0 ? elem.dstParam2 : (elem.dstParam >= 0 ? elem.dstParam + 1 : -1);
         auto [valY, kpY] = getParamRef(elem.dstIndex, p2, elem.value2);

         float padSize = std::min(cardSize.x - 16.0f, cardSize.y - 30.0f);
         ImVec2 origin(cellPos.x + (cardSize.x - padSize) * 0.5f, cellPos.y + 22.0f);
         ImVec2 padBR(origin.x + padSize, origin.y + padSize);

         ImGui::SetCursorScreenPos(origin);
         ImGui::InvisibleButton("##xypad", ImVec2(padSize, padSize));
         const bool active = ImGui::IsItemActive();

         float minX = kpX.minValue, maxX = kpX.maxValue;
         if (minX >= maxX) { minX = 0.0f; maxX = 1.0f; }
         float minY = kpY.minValue, maxY = kpY.maxValue;
         if (minY >= maxY) { minY = 0.0f; maxY = 1.0f; }

         if (active)
         {
            ImVec2 m = ImGui::GetIO().MousePos;
            float normX = std::clamp((m.x - origin.x) / padSize, 0.0f, 1.0f);
            float normY = std::clamp(1.0f - (m.y - origin.y) / padSize, 0.0f, 1.0f);
            float newX = TaperP2V(kpX.taper) ? TaperP2V(kpX.taper)(normX, minX, maxX) : (minX + (maxX - minX) * normX);
            float newY = TaperP2V(kpY.taper) ? TaperP2V(kpY.taper)(normY, minY, maxY) : (minY + (maxY - minY) * normY);
            elem.value = newX;
            elem.value2 = newY;
            if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = newX;
            if (elem.dstIndex >= 0 && p2 >= 0)
               gPerfPendingWrites[{elem.dstIndex, p2}] = newY;
            for (const auto& t : elem.targets)
            {
               if (t.dstIndex >= 0 && t.dstParam >= 0)
               {
                  auto [tVal, tKp] = getParamRef(t.dstIndex, t.dstParam, elem.value);
                  float tNew = TaperP2V(tKp.taper) ? TaperP2V(tKp.taper)(normX, tKp.minValue, tKp.maxValue) : (tKp.minValue + (tKp.maxValue - tKp.minValue) * normX);
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = tNew;
               }
            }
            for (const auto& t : elem.targetsY)
            {
               if (t.dstIndex >= 0 && t.dstParam >= 0)
               {
                  auto [tVal, tKp] = getParamRef(t.dstIndex, t.dstParam, elem.value2);
                  float tNew = TaperP2V(tKp.taper) ? TaperP2V(tKp.taper)(normY, tKp.minValue, tKp.maxValue) : (tKp.minValue + (tKp.maxValue - tKp.minValue) * normY);
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = tNew;
               }
            }
            valX = newX;
            valY = newY;
         }

         dl->AddRectFilled(origin, padBR, ScopeBgCol(), 4.0f);
         dl->AddLine(ImVec2(origin.x + padSize * 0.5f, origin.y), ImVec2(origin.x + padSize * 0.5f, padBR.y), ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + padSize * 0.5f), ImVec2(padBR.x, origin.y + padSize * 0.5f), ScopeGridCol());
         dl->AddRect(origin, padBR, ScopeBorderCol(), 4.0f);

         float normX = TaperV2P(kpX.taper) ? TaperV2P(kpX.taper)(valX, minX, maxX) : ((maxX > minX) ? std::clamp((valX - minX) / (maxX - minX), 0.0f, 1.0f) : 0.0f);
         float normY = TaperV2P(kpY.taper) ? TaperV2P(kpY.taper)(valY, minY, maxY) : ((maxY > minY) ? std::clamp((valY - minY) / (maxY - minY), 0.0f, 1.0f) : 0.0f);
         ImVec2 orbPos(origin.x + normX * padSize, origin.y + (1.0f - normY) * padSize);
         dl->AddCircleFilled(orbPos, 6.0f, themeTint);
         dl->AddCircle(orbPos, 6.0f, isLight ? IM_COL32(240, 240, 240, 255) : IM_COL32(20, 20, 28, 255), 0, 1.5f);
      }
      else if (elem.kind == 5) // Momentary Trigger / Bang (1x1)
      {
         float padSize = std::min(cardSize.x - 18.0f, cardSize.y - 28.0f);
         float centerX = cellPos.x + (cardSize.x - padSize) * 0.5f;
         float centerY = cellPos.y + 20.0f + (cardSize.y - 20.0f - padSize) * 0.5f;
         ImVec2 bTL(centerX, centerY);
         ImVec2 center(centerX + padSize * 0.5f, centerY + padSize * 0.5f);

         ImGui::SetCursorScreenPos(bTL);
         ImGui::InvisibleButton("##bangbtn", ImVec2(padSize, padSize));
         const bool active = ImGui::IsItemActive();
         const bool hovered = ImGui::IsItemHovered();
         // A trigger wired to a multi-state selector or toggle advances one step per
         // press and wraps, instead of slamming the param to its maximum and
         // back. A plain 0..1 continuous float keeps the momentary 1 / 0 behaviour.
         auto stepSpan = [&](int dstIndex, int dstParam, const std::string& boolName) -> int {
            if (dstIndex < 0)
               return 0;
            if (!boolName.empty())
               return 1;
            if (dstParam < 0)
               return 0;
            const ParamRef* kp = Modulation::Instance().KnownParam(dstIndex, dstParam);
            if (kp == nullptr)
               return 0;
            if (kp->momentary)
               return 0; // a gate button takes the plain 1-while-held / 0-on-release write
            const int span = (int)std::lround(kp->maxValue - kp->minValue);
            if (kp->isBool || (kp->step == 1.0f && span == 1))
               return 1;
            if (kp->isEnum)
               return span >= 1 ? span : 0;
            return (kp->step == 1.0f && span >= 2) ? span : 0;
         };
         auto advance = [&](int dstIndex, int dstParam, const std::string& boolName) {
            if (!boolName.empty())
            {
               if (GraphNode* gn = FindNodeByIndex(dstIndex))
               {
                  bool curB = ReadNodeBool(gn, boolName, dstParam);
                  WriteNodeBool(gn, boolName, !curB, dstParam);
               }
               return;
            }
            auto [cur, kp] = getParamRef(dstIndex, dstParam, 0.0f);
            const int span = (int)std::lround(kp.maxValue - kp.minValue);
            if (span < 1)
               return;
            const int idx = std::clamp((int)std::lround(cur - kp.minValue), 0, span);
            gPerfPendingWrites[{ dstIndex, dstParam }] = kp.minValue + (float)((idx + 1) % (span + 1));
         };
         const bool stepping = stepSpan(elem.dstIndex, elem.dstParam, elem.boolName) > 0;

         if (stepping)
         {
            if (ImGui::IsItemActivated())
            {
               gPerfBangFlash[elemIdx] = 1.0f;
               advance(elem.dstIndex, elem.dstParam, elem.boolName);
               for (const auto& t : elem.targets)
                  if (t.dstIndex >= 0 && stepSpan(t.dstIndex, t.dstParam, t.boolName) > 0)
                     advance(t.dstIndex, t.dstParam, t.boolName);
                  else if (t.dstIndex >= 0 && t.dstParam >= 0)
                     gPerfPendingWrites[{ t.dstIndex, t.dstParam }] = 1.0f;
            }
            elem.value = 0.0f;
         }
         else if (active)
         {
            gPerfBangFlash[elemIdx] = 1.0f;
            elem.value = 1.0f;
            if (dstNode != nullptr && !elem.boolName.empty())
               WriteNodeBool(dstNode, elem.boolName, true, elem.dstParam);
            else if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = 1.0f;
            for (const auto& t : elem.targets)
            {
               if (t.dstIndex >= 0 && !t.boolName.empty())
               {
                  if (GraphNode* gn = FindNodeByIndex(t.dstIndex))
                     WriteNodeBool(gn, t.boolName, true, t.dstParam);
               }
               else if (t.dstIndex >= 0 && t.dstParam >= 0)
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = 1.0f;
            }
         }
         else
         {
            if (elem.value > 0.0f)
            {
               elem.value = 0.0f;
               if (dstNode != nullptr && !elem.boolName.empty())
                  WriteNodeBool(dstNode, elem.boolName, false, elem.dstParam);
               else if (elem.dstIndex >= 0 && elem.dstParam >= 0)
                  gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = 0.0f;
               for (const auto& t : elem.targets)
               {
                  if (t.dstIndex >= 0 && !t.boolName.empty())
                  {
                     if (GraphNode* gn = FindNodeByIndex(t.dstIndex))
                        WriteNodeBool(gn, t.boolName, false, t.dstParam);
                  }
                  else if (t.dstIndex >= 0 && t.dstParam >= 0)
                     gPerfPendingWrites[{t.dstIndex, t.dstParam}] = 0.0f;
               }
            }
         }

         float flash = gPerfBangFlash[elemIdx];
         if (flash > 0.0f)
         {
            flash -= ImGui::GetIO().DeltaTime * 4.0f;
            if (flash < 0.0f) flash = 0.0f;
            gPerfBangFlash[elemIdx] = flash;
         }

         float r = padSize * 0.44f;
         ImU32 baseCol = isLight ? IM_COL32(215, 222, 235, 255) : IM_COL32(32, 36, 48, 255);
         dl->AddCircleFilled(center, r, baseCol, 32);

         if (flash > 0.0f)
         {
            ImU32 flashCol = (themeTint & 0x00FFFFFF) | ((ImU32)(flash * 220.0f) << 24);
            dl->AddCircleFilled(center, r * (0.6f + 0.4f * flash), flashCol, 32);
            dl->AddCircle(center, r + 2.0f * (1.0f - flash), IM_COL32(255, 225, 80, (int)(flash * 255.0f)), 32, 2.0f);
         }
         else
         {
            dl->AddCircleFilled(center, r * 0.55f, (themeTint & 0x00FFFFFF) | 0x88000000, 32);
         }
         dl->AddCircle(center, r, isLight ? IM_COL32(170, 180, 195, 255) : IM_COL32(60, 66, 82, 255), 32, 1.5f);
         if (hovered)
            dl->AddCircle(center, r + 2.0f, IM_COL32(255, 255, 255, 80), 32, 1.2f);
      }
      else if (elem.kind == 6) // Digital Number Box (1x1)
      {
         auto [val, kp] = getParamRef(elem.dstIndex, elem.dstParam, elem.value);
         float minV = kp.minValue, maxV = kp.maxValue;
         if (minV >= maxV) { minV = 0.0f; maxV = 1.0f; }

         float boxW = cardSize.x - 16.0f;
         float boxH = 26.0f;
         float centerX = cellPos.x + 8.0f;
         float centerY = cellPos.y + 20.0f + (cardSize.y - 20.0f - boxH) * 0.5f;

         ImVec2 bTL(centerX, centerY);
         ImVec2 bBR(centerX + boxW, centerY + boxH);

         dl->AddRectFilled(bTL, bBR, isLight ? IM_COL32(230, 235, 245, 255) : IM_COL32(15, 17, 24, 255), 4.0f);
         dl->AddRect(bTL, bBR, isLight ? IM_COL32(180, 190, 205, 255) : IM_COL32(50, 56, 72, 255), 4.0f);

         ImGui::SetCursorScreenPos(bTL);
         float v = dragSeed(val);
         float speed = (maxV - minV) * 0.005f;
         if (speed <= 0.0f) speed = 0.01f;
         ImGui::SetNextItemWidth(boxW);
         PushSliderStyle();
         // The box background/border were already hand-drawn above (bTL/bBR)
         // so only PushSliderStyle's Text color should carry through here -
         // its FrameBg/Border pushes would otherwise paint a second frame on
         // top of the manual one, which is the "double box" the user sees
         // (loudest in light mode, where the border is opaque).
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
         const bool numMoved = ImGui::DragFloat("##numbox", &v, speed, minV, maxV, (std::abs(maxV - minV) > 10.0f) ? "%.1f" : "%.3f");
         ImGui::PopStyleVar();
         ImGui::PopStyleColor(4);
         PopSliderStyle();
         dragHold(v);
         if (numMoved)
         {
            elem.value = v;
            if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = v;
            for (const auto& t : elem.targets)
               if (t.dstIndex >= 0 && t.dstParam >= 0)
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = v;
         }
      }
      else if (elem.kind == 7) // Radio Selector (2x1)
      {
         auto [val, kp] = getParamRef(elem.dstIndex, elem.dstParam, elem.value);
         float minV = kp.minValue, maxV = kp.maxValue;
         if (minV >= maxV) { minV = 0.0f; maxV = 1.0f; }

         int count = 8;
         bool isDiscreteEnum = kp.isEnum || (kp.step == 1.0f && (maxV - minV) <= 16.0f && (maxV - minV) >= 1.0f);
         if (isDiscreteEnum)
            count = std::clamp((int)(maxV - minV + 1.0f), 2, 8);

         int curIndex = 0;
         if (isDiscreteEnum)
            curIndex = std::clamp((int)std::round(val - minV), 0, count - 1);
         else
            curIndex = std::clamp((int)std::round((val - minV) / (maxV - minV) * (float)(count - 1)), 0, count - 1);

         float areaW = cardSize.x - 16.0f;
         float btnH = 24.0f;
         float startY = cellPos.y + 20.0f + (cardSize.y - 20.0f - btnH) * 0.5f;
         float btnW = (areaW - (count - 1) * 3.0f) / (float)count;

         // Names only if the whole row can carry them: one long name among
         // short ones would leave a mix of names and numbers.
         bool useNames = isDiscreteEnum && (int)kp.enumOptions.size() >= count;
         if (useNames)
         {
            const float room = btnW - 6.0f;
            for (int b = 0; b < count && useNames; b++)
               if (ImGui::CalcTextSize(kp.enumOptions[b].c_str()).x > room)
                  useNames = false;
         }

         for (int b = 0; b < count; b++)
         {
            if (b > 0) ImGui::SameLine();
            float bx = cellPos.x + 8.0f + b * (btnW + 3.0f);
            ImGui::SetCursorScreenPos(ImVec2(bx, startY));
            ImGui::PushID(b + 300);

            const bool isSelected = (b == curIndex);
            if (isSelected)
            {
               ImVec4 activeCol((themeTint & 0xFF) / 255.0f,
                                ((themeTint >> 8) & 0xFF) / 255.0f,
                                ((themeTint >> 16) & 0xFF) / 255.0f, 1.0f);
               ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            }
            else
            {
               ImGui::PushStyleColor(ImGuiCol_Button, isLight ? ImVec4(0.88f, 0.90f, 0.94f, 1.0f) : ImVec4(0.18f, 0.20f, 0.26f, 1.0f));
               ImGui::PushStyleColor(ImGuiCol_Text, isLight ? ImVec4(0.3f, 0.35f, 0.45f, 1.0f) : ImVec4(0.7f, 0.75f, 0.85f, 1.0f));
            }

            // Either every button shows its option name or none of them do.
            // The old rule was per-button ("use the name if it is <= 4 chars"),
            // which for a list like {Flat, Sphere, Cylinder, ...} labelled the
            // first button "Flat" - clipped to "Fla" - and the rest 2..7,
            // reading as a broken 1..8 row. useNames is decided once, above
            // the loop.
            std::string btnText = useNames ? kp.enumOptions[b] : std::to_string(b + 1);
            if (ImGui::Button(btnText.c_str(), ImVec2(btnW, btnH)))
            {
               float newVal = 0.0f;
               if (isDiscreteEnum)
                  newVal = minV + (float)b;
               else
                  newVal = minV + (maxV - minV) * ((float)b / (float)(count - 1));

               elem.value = newVal;
               if (elem.dstIndex >= 0 && elem.dstParam >= 0)
                  gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = newVal;
               for (const auto& t : elem.targets)
                  if (t.dstIndex >= 0 && t.dstParam >= 0)
                     gPerfPendingWrites[{t.dstIndex, t.dstParam}] = newVal;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopID();
         }
      }
      else if (elem.kind == 8) // Bipolar Knob (1x1)
      {
         auto [val, kp] = getParamRef(elem.dstIndex, elem.dstParam, elem.value);
         float minV = kp.minValue, maxV = kp.maxValue;
         if (minV >= maxV) { minV = -1.0f; maxV = 1.0f; }

         // Compute bipolar knob range [-1.0, +1.0] representing swing around midpoint
         float midV = (minV + maxV) * 0.5f;
         float halfSpan = (maxV - minV) * 0.5f;
         float bipVal = (halfSpan > 0.0001f) ? std::clamp((val - midV) / halfSpan, -1.0f, 1.0f) : elem.value;

         float diameter = std::max(28.0f, std::min(cellSize * 0.52f, 44.0f));
         float centerX = cellPos.x + cardSize.x * 0.5f;
         float centerY = cellPos.y + 20.0f + (cardSize.y - 20.0f) * 0.44f;
         ImGui::SetCursorScreenPos(ImVec2(centerX - diameter * 0.5f, centerY - diameter * 0.5f));

         ImU32 fillCol = isModulated ? (isLight ? IM_COL32(215, 125, 20, 255) : IM_COL32(255, 190, 90, 255)) : themeTint;
         float v = dragSeed(bipVal);
         const bool bipMoved = BipolarKnobFloat("##bipolarknob", &v, -1.0f, 1.0f, "%.2f", diameter, fillCol, isModulated, diameter);
         dragHold(v);
         if (bipMoved)
         {
            elem.value = v;
            float actualParamVal = midV + v * halfSpan;
            if (kp.step == 1.0f)
               actualParamVal = std::round(actualParamVal);

            if (elem.dstIndex >= 0 && elem.dstParam >= 0)
               gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = actualParamVal;
            for (const auto& t : elem.targets)
               if (t.dstIndex >= 0 && t.dstParam >= 0)
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = actualParamVal;
         }
      }
      else if (elem.kind == 9) // Step Gate Ribbon (3x1)
      {
         uint8_t pattern = (uint8_t)elem.value2;
         if (elem.value2 == 0.0f && elem.value == 0.0f)
         {
            pattern = 0b10011001; // Default 8-step pattern
            elem.value2 = (float)pattern;
         }

         float areaW = cardSize.x - 16.0f;
         float stepH = cardSize.y - 30.0f;
         float stepW = (areaW - 7.0f * 3.0f) / 8.0f;
         float startY = cellPos.y + 22.0f;

         static int sCurrentStep = 0;
         if (Transport::Instance().IsPlaying())
         {
            double beats = Transport::Instance().Beats();
            long long step = (long long)std::floor(beats / 0.25); // 16th note steps
            sCurrentStep = (int)(((step % 8) + 8) % 8);
         }
         int playStep = sCurrentStep;

         bool currentStepOn = (pattern & (1 << playStep)) != 0;
         float gateVal = (currentStepOn && Transport::Instance().IsPlaying()) ? 1.0f : 0.0f;
         elem.value = gateVal;

         if (elem.dstIndex >= 0 && elem.dstParam >= 0)
            gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = gateVal;
         for (const auto& t : elem.targets)
            if (t.dstIndex >= 0 && t.dstParam >= 0)
               gPerfPendingWrites[{t.dstIndex, t.dstParam}] = gateVal;

         for (int s = 0; s < 8; s++)
         {
            float sx = cellPos.x + 8.0f + s * (stepW + 3.0f);
            ImVec2 sTL(sx, startY);
            ImVec2 sBR(sx + stepW, startY + stepH);

            ImGui::SetCursorScreenPos(sTL);
            ImGui::PushID(s + 400);
            ImGui::InvisibleButton("##stepbtn", ImVec2(stepW, stepH));
            if (ImGui::IsItemClicked())
            {
               PushUndoCheckpoint();
               pattern ^= (1 << s);
               elem.value2 = (float)pattern;
            }
            ImGui::PopID();

            const bool isOn = (pattern & (1 << s)) != 0;
            const bool isCurrent = (s == playStep);

            ImU32 stepBg = isOn
               ? themeTint
               : (isLight ? IM_COL32(210, 216, 228, 255) : IM_COL32(28, 31, 40, 255));

            dl->AddRectFilled(sTL, sBR, stepBg, 3.0f);

            if (isCurrent && Transport::Instance().IsPlaying())
            {
               dl->AddRect(sTL, sBR, IM_COL32(255, 230, 80, 255), 3.0f, 0, 2.0f);
               dl->AddCircleFilled(ImVec2(sx + stepW * 0.5f, startY + 4.0f), 2.5f, IM_COL32(255, 240, 100, 255));
            }
            else
            {
               dl->AddRect(sTL, sBR, isLight ? IM_COL32(180, 190, 205, 200) : IM_COL32(48, 52, 65, 200), 3.0f);
            }
         }
      }

      // A control whose node is bypassed still works - the value is stored
      // and applies the moment the node is un-bypassed - but it moves nothing
      // you can hear or see right now. Say so: dim the card and tag it, so a
      // dead knob in a live set is never a mystery. Only when *every* node it
      // drives is bypassed (a macro reaching a live node is still live), and
      // never on the card that is itself the bypass toggle - that one is how
      // you bring the node back.
      if (elem.boolName != "bypassed")
      {
         bool anyTarget = false;
         bool allBypassed = true;
         auto consider = [&](int nodeIndex)
         {
            GraphNode* t = FindNodeByIndex(nodeIndex);
            if (t == nullptr || t->node == nullptr)
               return;
            anyTarget = true;
            allBypassed = allBypassed && t->node->bypassed;
         };
         consider(elem.dstIndex);
         for (const Patch::PerfTarget& t : elem.targets)
            consider(t.dstIndex);
         for (const Patch::PerfTarget& t : elem.targetsY)
            consider(t.dstIndex);
         if (anyTarget && allBypassed)
         {
            const ImVec2 bodyTL(cellPos.x, cellPos.y + 18.0f);
            dl->AddRectFilled(bodyTL, cardBR, isLight ? IM_COL32(245, 247, 252, 150) : IM_COL32(22, 25, 33, 160),
                              6.0f, ImDrawFlags_RoundCornersBottom);
            const char* tag = "bypassed";
            const ImVec2 tagSize = ImGui::CalcTextSize(tag);
            const ImVec2 tagTL(cardBR.x - tagSize.x - 10.0f, cellPos.y + 2.0f);
            const ImVec2 tagBR(cardBR.x - 4.0f, cellPos.y + 2.0f + tagSize.y);
            dl->AddRectFilled(ImVec2(tagTL.x - 3.0f, tagTL.y), tagBR, cardBg, 3.0f);
            dl->AddText(tagTL, isLight ? IM_COL32(190, 110, 30, 255) : IM_COL32(240, 170, 70, 255), tag);
            if (ImGui::IsMouseHoveringRect(ImVec2(tagTL.x - 3.0f, tagTL.y), tagBR))
               ImGui::SetTooltip("This node is bypassed - the control still stores its value, which applies when the node is back in the chain");
         }
      }

      ImGui::PopID();
   }

   void UpdatePerformanceMatrixMIDI()
   {
      // 1. Process MIDI Learn if active
      if (gPerfMidiLearnIdx >= 0)
      {
         if (!Platform::MidiIsRunning())
         {
            std::string err;
            Platform::MidiStart(err);
         }
         Platform::MidiCCValue last;
         if (Platform::MidiPollLastTouched(last))
         {
            if (gPerfMidiLearnIdx < (int)gPerfElements.size())
            {
               PushUndoCheckpoint();
               auto& elem = gPerfElements[gPerfMidiLearnIdx];
               if (gPerfMidiLearnAxis == 1)
               {
                  elem.midiDeviceY = (int)last.device;
                  elem.midiChannelY = last.channel;
                  elem.midiControllerY = last.controller;
                  elem.midiIsNoteY = last.isNote;
               }
               else
               {
                  elem.midiDevice = (int)last.device;
                  elem.midiChannel = last.channel;
                  elem.midiController = last.controller;
                  elem.midiIsNote = last.isNote;
               }
               gPerfMidiRuntimeStates[gPerfMidiLearnIdx] = PerfMidiRuntimeState{};
            }
            gPerfMidiLearnIdx = -1;
         }
      }

      // 2. Process all MIDI-bound elements
      for (size_t i = 0; i < gPerfElements.size(); i++)
      {
         auto& elem = gPerfElements[i];
         if (elem.midiDevice == 0 && elem.midiDeviceY == 0)
            continue;

         if (!Platform::MidiIsRunning())
         {
            std::string err;
            Platform::MidiStart(err);
         }

         PerfMidiRuntimeState& st = gPerfMidiRuntimeStates[i];


         // Handle primary / X axis
         if (elem.midiDevice != 0)
         {
            if (elem.kind == 3) // Toggle
            {
               if (elem.midiIsNote)
               {
                  unsigned int hitSeq = Platform::MidiNoteHitCount(elem.midiDevice, elem.midiChannel, elem.midiController);
                  if (st.lastHitSeq == 0) st.lastHitSeq = hitSeq;
                  if (hitSeq > st.lastHitSeq)
                  {
                     st.lastHitSeq = hitSeq;
                     bool curVal = elem.value > 0.5f;
                     bool newVal = !curVal;
                     elem.value = newVal ? 1.0f : 0.0f;
                     if (elem.dstIndex >= 0 && !elem.boolName.empty())
                     {
                        if (GraphNode* gn = FindNodeByIndex(elem.dstIndex))
                           WriteNodeBool(gn, elem.boolName, newVal, elem.dstParam);
                     }
                     else if (elem.dstIndex >= 0 && elem.dstParam >= 0)
                        gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = elem.value;
                     for (const auto& t : elem.targets)
                     {
                        if (t.dstIndex >= 0 && !t.boolName.empty())
                        {
                           if (GraphNode* gn = FindNodeByIndex(t.dstIndex))
                              WriteNodeBool(gn, t.boolName, newVal, t.dstParam);
                        }
                        else if (t.dstIndex >= 0 && t.dstParam >= 0)
                           gPerfPendingWrites[{t.dstIndex, t.dstParam}] = elem.value;
                     }
                  }
               }
               else // CC
               {
                  float rawVal = 0.0f;
                  if (Platform::MidiRead(elem.midiDevice, elem.midiChannel, elem.midiController, false, rawVal))
                  {
                     if (st.lastVal < 0.0f || std::abs(rawVal - st.lastVal) > 0.001f)
                     {
                        st.lastVal = rawVal;
                        bool newVal = rawVal > 0.5f;
                        elem.value = newVal ? 1.0f : 0.0f;
                        if (elem.dstIndex >= 0 && !elem.boolName.empty())
                        {
                           if (GraphNode* gn = FindNodeByIndex(elem.dstIndex))
                              WriteNodeBool(gn, elem.boolName, newVal, elem.dstParam);
                        }
                        else if (elem.dstIndex >= 0 && elem.dstParam >= 0)
                           gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = elem.value;
                        for (const auto& t : elem.targets)
                        {
                           if (t.dstIndex >= 0 && !t.boolName.empty())
                           {
                              if (GraphNode* gn = FindNodeByIndex(t.dstIndex))
                                 WriteNodeBool(gn, t.boolName, newVal, t.dstParam);
                           }
                           else if (t.dstIndex >= 0 && t.dstParam >= 0)
                              gPerfPendingWrites[{t.dstIndex, t.dstParam}] = elem.value;
                        }
                     }
                  }
               }
            }
            else if (elem.kind == 5) // Momentary Trigger / Bang
            {
               auto stepSpan = [&](int dstIndex, int dstParam, const std::string& boolName) -> int {
                  if (dstIndex < 0) return 0;
                  if (!boolName.empty()) return 1;
                  if (dstParam < 0) return 0;
                  const ParamRef* kp = Modulation::Instance().KnownParam(dstIndex, dstParam);
                  if (kp == nullptr) return 0;
                  if (kp->momentary) return 0; // a gate button takes the plain 1-while-held / 0-on-release write
                  const int span = (int)std::lround(kp->maxValue - kp->minValue);
                  if (kp->isBool || (kp->step == 1.0f && span == 1)) return 1;
                  if (kp->isEnum) return span >= 1 ? span : 0;
                  return (kp->step == 1.0f && span >= 2) ? span : 0;
               };
               auto advance = [&](int dstIndex, int dstParam, const std::string& boolName) {
                  if (!boolName.empty())
                  {
                     if (GraphNode* gn = FindNodeByIndex(dstIndex))
                     {
                        bool curB = ReadNodeBool(gn, boolName, dstParam);
                        WriteNodeBool(gn, boolName, !curB, dstParam);
                     }
                     return;
                  }
                  float cur = 0.0f;
                  const ParamRef* pKp = Modulation::Instance().KnownParam(dstIndex, dstParam);
                  if (!pKp) return;
                  for (const ParamRef& ref : Modulation::Instance().FrameParams())
                     if (ref.nodeIndex == dstIndex && ref.paramIndex == dstParam && ref.value) { cur = *ref.value; break; }
                  const int span = (int)std::lround(pKp->maxValue - pKp->minValue);
                  if (span < 1) return;
                  const int idx = std::clamp((int)std::lround(cur - pKp->minValue), 0, span);
                  gPerfPendingWrites[{ dstIndex, dstParam }] = pKp->minValue + (float)((idx + 1) % (span + 1));
               };

               const bool stepping = stepSpan(elem.dstIndex, elem.dstParam, elem.boolName) > 0;
               if (elem.midiIsNote)
               {
                  float rawVal = 0.0f;
                  Platform::MidiRead(elem.midiDevice, elem.midiChannel, elem.midiController, true, rawVal);
                  unsigned int hitSeq = Platform::MidiNoteHitCount(elem.midiDevice, elem.midiChannel, elem.midiController);
                  bool newHit = false;
                  if (st.lastHitSeq == 0) st.lastHitSeq = hitSeq;
                  if (hitSeq > st.lastHitSeq)
                  {
                     st.lastHitSeq = hitSeq;
                     newHit = true;
                  }
                  if (newHit || rawVal > 0.0f)
                  {
                     elem.value = 1.0f;
                     gPerfBangFlash[i] = 1.0f;
                     if (newHit)
                     {
                        if (stepping)
                        {
                           advance(elem.dstIndex, elem.dstParam, elem.boolName);
                           for (const auto& t : elem.targets)
                              if (t.dstIndex >= 0 && stepSpan(t.dstIndex, t.dstParam, t.boolName) > 0)
                                 advance(t.dstIndex, t.dstParam, t.boolName);
                              else if (t.dstIndex >= 0 && t.dstParam >= 0)
                                 gPerfPendingWrites[{ t.dstIndex, t.dstParam }] = 1.0f;
                        }
                        else
                        {
                           if (elem.dstIndex >= 0 && !elem.boolName.empty()) { if (GraphNode* gn = FindNodeByIndex(elem.dstIndex)) WriteNodeBool(gn, elem.boolName, true, elem.dstParam); }
                           else if (elem.dstIndex >= 0 && elem.dstParam >= 0) gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = 1.0f;
                           for (const auto& t : elem.targets) { if (t.dstIndex >= 0 && !t.boolName.empty()) { if (GraphNode* gn = FindNodeByIndex(t.dstIndex)) WriteNodeBool(gn, t.boolName, true, t.dstParam); } else if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = 1.0f; }
                        }
                     }
                  }
                  else if (!stepping && elem.value > 0.0f)
                  {
                     elem.value = 0.0f;
                     if (elem.dstIndex >= 0 && !elem.boolName.empty()) { if (GraphNode* gn = FindNodeByIndex(elem.dstIndex)) WriteNodeBool(gn, elem.boolName, false, elem.dstParam); }
                     else if (elem.dstIndex >= 0 && elem.dstParam >= 0) gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = 0.0f;
                     for (const auto& t : elem.targets) { if (t.dstIndex >= 0 && !t.boolName.empty()) { if (GraphNode* gn = FindNodeByIndex(t.dstIndex)) WriteNodeBool(gn, t.boolName, false, t.dstParam); } else if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = 0.0f; }
                  }
               }
               else // CC
               {
                  float rawVal = 0.0f;
                  if (Platform::MidiRead(elem.midiDevice, elem.midiChannel, elem.midiController, false, rawVal))
                  {
                     if (st.lastVal < 0.0f || std::abs(rawVal - st.lastVal) > 0.001f)
                     {
                        bool wasOn = st.lastVal > 0.5f;
                        bool isOn = rawVal > 0.5f;
                        st.lastVal = rawVal;
                        if (isOn && !wasOn)
                        {
                           elem.value = 1.0f;
                           gPerfBangFlash[i] = 1.0f;
                           if (stepping)
                           {
                              advance(elem.dstIndex, elem.dstParam, elem.boolName);
                              for (const auto& t : elem.targets)
                                 if (t.dstIndex >= 0 && stepSpan(t.dstIndex, t.dstParam, t.boolName) > 0)
                                    advance(t.dstIndex, t.dstParam, t.boolName);
                                 else if (t.dstIndex >= 0 && t.dstParam >= 0)
                                    gPerfPendingWrites[{ t.dstIndex, t.dstParam }] = 1.0f;
                           }
                           else
                           {
                              if (elem.dstIndex >= 0 && !elem.boolName.empty()) { if (GraphNode* gn = FindNodeByIndex(elem.dstIndex)) WriteNodeBool(gn, elem.boolName, true, elem.dstParam); }
                              else if (elem.dstIndex >= 0 && elem.dstParam >= 0) gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = 1.0f;
                              for (const auto& t : elem.targets) { if (t.dstIndex >= 0 && !t.boolName.empty()) { if (GraphNode* gn = FindNodeByIndex(t.dstIndex)) WriteNodeBool(gn, t.boolName, true, t.dstParam); } else if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = 1.0f; }
                           }
                        }
                        else if (!isOn && wasOn && !stepping)
                        {
                           elem.value = 0.0f;
                           if (elem.dstIndex >= 0 && !elem.boolName.empty()) { if (GraphNode* gn = FindNodeByIndex(elem.dstIndex)) WriteNodeBool(gn, elem.boolName, false, elem.dstParam); }
                           else if (elem.dstIndex >= 0 && elem.dstParam >= 0) gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = 0.0f;
                           for (const auto& t : elem.targets) { if (t.dstIndex >= 0 && !t.boolName.empty()) { if (GraphNode* gn = FindNodeByIndex(t.dstIndex)) WriteNodeBool(gn, t.boolName, false, t.dstParam); } else if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = 0.0f; }
                        }
                     }
                  }
               }
            }
            else if (elem.kind == 8) // Bipolar Knob
            {
               float rawVal = 0.0f;
               if (Platform::MidiRead(elem.midiDevice, elem.midiChannel, elem.midiController, elem.midiIsNote, rawVal))
               {
                  if (st.lastVal < 0.0f || std::abs(rawVal - st.lastVal) > 0.001f)
                  {
                     st.lastVal = rawVal;
                     const ParamRef* pKp = (elem.dstIndex >= 0 && elem.dstParam >= 0) ? Modulation::Instance().KnownParam(elem.dstIndex, elem.dstParam) : nullptr;
                     float minV = pKp ? pKp->minValue : -1.0f, maxV = pKp ? pKp->maxValue : 1.0f;
                     if (minV >= maxV) { minV = -1.0f; maxV = 1.0f; }
                     float midV = (minV + maxV) * 0.5f;
                     float halfSpan = (maxV - minV) * 0.5f;
                     float bipVal = -1.0f + 2.0f * rawVal;
                     elem.value = bipVal;
                     float actualVal = midV + bipVal * halfSpan;
                     if (pKp && pKp->step == 1.0f) actualVal = std::round(actualVal);
                     if (elem.dstIndex >= 0 && elem.dstParam >= 0) gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = actualVal;
                     for (const auto& t : elem.targets) if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = actualVal;
                  }
               }
            }
            else if (elem.kind == 7) // Radio Selector
            {
               const ParamRef* pKp = (elem.dstIndex >= 0 && elem.dstParam >= 0) ? Modulation::Instance().KnownParam(elem.dstIndex, elem.dstParam) : nullptr;
               float minV = pKp ? pKp->minValue : 0.0f, maxV = pKp ? pKp->maxValue : 1.0f;
               if (minV >= maxV) { minV = 0.0f; maxV = 1.0f; }
               int count = 8;
               bool isDiscreteEnum = pKp && (pKp->isEnum || (pKp->step == 1.0f && (maxV - minV) <= 16.0f && (maxV - minV) >= 1.0f));
               if (isDiscreteEnum) count = std::clamp((int)(maxV - minV + 1.0f), 2, 8);

               if (elem.midiIsNote)
               {
                  unsigned int hitSeq = Platform::MidiNoteHitCount(elem.midiDevice, elem.midiChannel, elem.midiController);
                  if (st.lastHitSeq == 0) st.lastHitSeq = hitSeq;
                  if (hitSeq > st.lastHitSeq)
                  {
                     st.lastHitSeq = hitSeq;
                     int curIndex = 0;
                     if (isDiscreteEnum)
                        curIndex = std::clamp((int)std::round(elem.value - minV), 0, count - 1);
                     else
                        curIndex = std::clamp((int)std::round((elem.value - minV) / (maxV - minV) * (float)(count - 1)), 0, count - 1);
                     int nextIndex = (curIndex + 1) % count;
                     float newVal = isDiscreteEnum ? (minV + (float)nextIndex) : (minV + (maxV - minV) * ((float)nextIndex / (float)(count - 1)));
                     elem.value = newVal;
                     if (elem.dstIndex >= 0 && elem.dstParam >= 0) gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = newVal;
                     for (const auto& t : elem.targets) if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = newVal;
                  }
               }
               else // CC
               {
                  float rawVal = 0.0f;
                  if (Platform::MidiRead(elem.midiDevice, elem.midiChannel, elem.midiController, false, rawVal))
                  {
                     if (st.lastVal < 0.0f || std::abs(rawVal - st.lastVal) > 0.001f)
                     {
                        st.lastVal = rawVal;
                        int b = std::clamp((int)std::floor(rawVal * (float)count), 0, count - 1);
                        float newVal = isDiscreteEnum ? (minV + (float)b) : (minV + (maxV - minV) * ((float)b / (float)(count - 1)));
                        elem.value = newVal;
                        if (elem.dstIndex >= 0 && elem.dstParam >= 0) gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = newVal;
                        for (const auto& t : elem.targets) if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = newVal;
                     }
                  }
               }
            }
            else // Knob (0), VFader (1), HSlider (2), XY Pad X (4), NumBox (6), Step Gate (9)
            {
               float rawVal = 0.0f;
               if (Platform::MidiRead(elem.midiDevice, elem.midiChannel, elem.midiController, elem.midiIsNote, rawVal))
               {
                  if (st.lastVal < 0.0f || std::abs(rawVal - st.lastVal) > 0.001f)
                  {
                     st.lastVal = rawVal;
                     const ParamRef* pKp = (elem.dstIndex >= 0 && elem.dstParam >= 0) ? Modulation::Instance().KnownParam(elem.dstIndex, elem.dstParam) : nullptr;
                     float minV = pKp ? pKp->minValue : 0.0f, maxV = pKp ? pKp->maxValue : 1.0f;
                     if (minV >= maxV) { minV = 0.0f; maxV = 1.0f; }
                     float newV = minV + (maxV - minV) * rawVal;
                     if (pKp && pKp->step > 0.0f)
                        newV = std::round((newV - minV) / pKp->step) * pKp->step + minV;
                     elem.value = newV;
                     if (elem.dstIndex >= 0 && elem.dstParam >= 0) gPerfPendingWrites[{elem.dstIndex, elem.dstParam}] = newV;
                     for (const auto& t : elem.targets) if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = newV;
                  }
               }
            }
         }

         // Handle XY Pad Y axis
         if (elem.kind == 4 && elem.midiDeviceY != 0)
         {
            float rawValY = 0.0f;
            if (Platform::MidiRead(elem.midiDeviceY, elem.midiChannelY, elem.midiControllerY, elem.midiIsNoteY, rawValY))
            {
               if (st.lastValY < 0.0f || std::abs(rawValY - st.lastValY) > 0.001f)
               {
                  st.lastValY = rawValY;
                  int p2 = elem.dstParam2 >= 0 ? elem.dstParam2 : (elem.dstParam >= 0 ? elem.dstParam + 1 : -1);
                  const ParamRef* pKpY = (elem.dstIndex >= 0 && p2 >= 0) ? Modulation::Instance().KnownParam(elem.dstIndex, p2) : nullptr;
                  float minY = pKpY ? pKpY->minValue : 0.0f, maxY = pKpY ? pKpY->maxValue : 1.0f;
                  if (minY >= maxY) { minY = 0.0f; maxY = 1.0f; }
                  float newY = minY + (maxY - minY) * rawValY;
                  if (pKpY && pKpY->step > 0.0f)
                     newY = std::round((newY - minY) / pKpY->step) * pKpY->step + minY;
                  elem.value2 = newY;
                  if (elem.dstIndex >= 0 && p2 >= 0) gPerfPendingWrites[{elem.dstIndex, p2}] = newY;
                  for (const auto& t : elem.targetsY) if (t.dstIndex >= 0 && t.dstParam >= 0) gPerfPendingWrites[{t.dstIndex, t.dstParam}] = newY;
               }
            }
         }
      }
   }

   // Turbo: one live MIDI learn at a time between the matrix and Turbo's own
   // MIDI learn mode; also flushes the depth-1 "last touched" queue so a new
   // learner never grabs a stale event.
   void MidiLearnCancelAll()
   {
      gPerfMidiLearnIdx = -1;
      if (Platform::MidiIsRunning())
      {
         Platform::MidiCCValue flush;
         while (Platform::MidiPollLastTouched(flush)) {}
      }
   }

   void DrawPerfPanelContent()
   {
      if (gPerfLayout.pageCount < 1) gPerfLayout.pageCount = 1;
      if (gPerfLayout.cellSize < 40) gPerfLayout.cellSize = 76;
      while ((int)gPerfLayout.pageNames.size() < gPerfLayout.pageCount)
         gPerfLayout.pageNames.push_back("Page " + std::to_string((int)gPerfLayout.pageNames.size() + 1));

      if (gPerfMidiLearnIdx >= 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
         gPerfMidiLearnIdx = -1;

      // MIDI Learn Alert Banner
      if (gPerfMidiLearnIdx >= 0 && gPerfMidiLearnIdx < (int)gPerfElements.size())
      {
         ImGui::Spacing();
         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 185, 45, 255));
         const auto& elem = gPerfElements[gPerfMidiLearnIdx];
         std::string axisStr = (gPerfMidiLearnAxis == 1) ? " (Y Axis)" : (elem.kind == 4 ? " (X Axis)" : "");
         std::string prompt = (elem.kind == 3 || elem.kind == 5 || elem.kind == 7)
            ? "MIDI Learn for '" + elem.label + "'" + axisStr + ": Move any CC knob/fader or hit a pad on your MIDI controller (Esc to cancel)..."
            : "MIDI CC Learn for '" + elem.label + "'" + axisStr + ": Move any CC knob, fader, or wheel on your MIDI controller (Esc to cancel)...";
         ImGui::Text("%s", prompt.c_str());
         ImGui::SameLine();
         if (ImGui::SmallButton("Cancel##cancelmidilearn"))
            gPerfMidiLearnIdx = -1;
         ImGui::PopStyleColor();
      }

      // ---- Sticky Header Toolbar ----
      // No ImGui::Spacing() here: this content child now carries the real
      // WindowPadding again (ImGuiChildFlags_AlwaysUseWindowPadding, see
      // DrawPerfPanelDocked), so an extra Spacing() on top of that padding
      // doubled the gap above "Edit Mode" - the Modulation Matrix panel
      // right above this one starts its content flush against the same
      // WindowPadding with no added Spacing(), which is the gap this now
      // matches.
      // Mode Switch Button (Edit / Perform)
      if (gPerfEditMode)
      {
         ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentEmphasisPressed());
         if (ImGui::Button("Edit Mode", ImVec2(86, 24)))
            gPerfEditMode = false;
         ImGui::PopStyleColor(2);
      }
      else
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.58f, 0.32f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.68f, 0.38f, 1.0f));
         if (ImGui::Button("Perform", ImVec2(86, 24)))
            gPerfEditMode = true;
         ImGui::PopStyleColor(2);
      }

      // Add Control button in Edit Mode
      if (gPerfEditMode)
      {
         ImGui::SameLine();
         if (ImGui::Button("+ Add Control", ImVec2(100, 24)))
            ImGui::OpenPopup("##perfaddcontrolmenu");

         if (ImGui::BeginPopup("##perfaddcontrolmenu"))
         {
            if (ImGui::MenuItem("Knob (1x1)")) AddPerfElementToCurrentPage(0);
            if (ImGui::MenuItem("Vertical Fader (1x2)")) AddPerfElementToCurrentPage(1);
            if (ImGui::MenuItem("Horizontal Slider (2x1)")) AddPerfElementToCurrentPage(2);
            if (ImGui::MenuItem("Toggle (1x1)")) AddPerfElementToCurrentPage(3);
            if (ImGui::MenuItem("XY Pad (2x2)")) AddPerfElementToCurrentPage(4);
            ImGui::Separator();
            if (ImGui::MenuItem("Momentary Trigger / Bang (1x1)")) AddPerfElementToCurrentPage(5);
            if (ImGui::MenuItem("Digital Number Box (1x1)")) AddPerfElementToCurrentPage(6);
            if (ImGui::MenuItem("Radio Selector (2x1)")) AddPerfElementToCurrentPage(7);
            if (ImGui::MenuItem("Bipolar Pan Knob (1x1)")) AddPerfElementToCurrentPage(8);
            if (ImGui::MenuItem("Step Gate Ribbon (3x1)")) AddPerfElementToCurrentPage(9);
            ImGui::EndPopup();
         }
      }

      // Page Tabs
      ImGui::SameLine();
      // No cursor-Y fudge: the page-tab and "+" buttons below now take an
      // explicit 24px height, matching "Edit Mode"/"Perform"/"+ Add
      // Control" exactly, so they already share the same baseline without
      // a manual nudge (the -2.0f here was compensating for those buttons
      // defaulting to ImGui's shorter auto-fit height, which is also why
      // they never actually lined up against the fixed-height buttons
      // beside them).
      const bool isLight = IsThemeLight();

      for (int p = 0; p < gPerfLayout.pageCount; p++)
      {
         std::string pageTitle = (p < (int)gPerfLayout.pageNames.size() && !gPerfLayout.pageNames[p].empty())
                                    ? gPerfLayout.pageNames[p]
                                    : ("Page " + std::to_string(p + 1));
         ImGui::PushID(p + 99000);
         ImGui::SameLine();

         if (gPerfRenamingPage == p)
         {
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##renamingpagetab", gPerfRenamePageBuffer, sizeof(gPerfRenamePageBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
               PushUndoCheckpoint();
               if (p < (int)gPerfLayout.pageNames.size())
                  gPerfLayout.pageNames[p] = gPerfRenamePageBuffer;
               gPerfRenamingPage = -1;
            }
            if (ImGui::IsItemDeactivated() && gPerfRenamingPage == p)
            {
               if (p < (int)gPerfLayout.pageNames.size())
                  gPerfLayout.pageNames[p] = gPerfRenamePageBuffer;
               gPerfRenamingPage = -1;
            }
         }
         else
         {
            const bool isSelected = (gPerfActivePage == p);
            if (isSelected)
            {
               ImGui::PushStyleColor(ImGuiCol_Button, isLight ? ImVec4(0.80f, 0.85f, 0.94f, 1.0f) : ImVec4(0.25f, 0.28f, 0.38f, 1.0f));
               ImGui::PushStyleColor(ImGuiCol_Text, isLight ? ImVec4(0.1f, 0.15f, 0.3f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            else
            {
               ImGui::PushStyleColor(ImGuiCol_Button, isLight ? ImVec4(0.92f, 0.94f, 0.96f, 0.7f) : ImVec4(0.14f, 0.16f, 0.22f, 0.7f));
               ImGui::PushStyleColor(ImGuiCol_Text, isLight ? ImVec4(0.35f, 0.40f, 0.50f, 1.0f) : ImVec4(0.65f, 0.70f, 0.80f, 1.0f));
            }

            // Explicit height matches "Edit Mode"/"Perform"/"+ Add Control"
            // (all 24px) - this used to auto-fit to ImGui's default frame
            // height, a few px shorter, which is what the -2.0f cursor nudge
            // above the loop was trying (and failing) to paper over. Width
            // gets its own explicit floor too: a plain auto-fit button is
            // only text-width + 2*FramePadding.x (8px total) wide, so the
            // selected page's highlight fill hugged its own label tighter
            // than every other chip-style control in the app (page tab or
            // not) - moving between pages by clicking through the highlight
            // is what made that cramped fit visible.
            const float tabW = std::max(60.0f, ImGui::CalcTextSize(pageTitle.c_str()).x + 24.0f);
            if (ImGui::Button(pageTitle.c_str(), ImVec2(tabW, 24)))
            {
               gPerfActivePage = p;
            }
            ImGui::PopStyleColor(2);

            // Double click to rename page
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               gPerfRenamingPage = p;
               snprintf(gPerfRenamePageBuffer, sizeof(gPerfRenamePageBuffer), "%s", pageTitle.c_str());
            }

            // Drag and drop to reorder pages
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
               ImGui::SetDragDropPayload("PERF_PAGE_TAB", &p, sizeof(int));
               ImGui::Text("Move %s", pageTitle.c_str());
               ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
               if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PERF_PAGE_TAB"))
               {
                  int srcPage = *(const int*)payload->Data;
                  ReorderPerfPages(srcPage, p);
               }
               ImGui::EndDragDropTarget();
            }

            // Right click context menu on page tab
            if (ImGui::BeginPopupContextItem("##pagetabcontext"))
            {
               if (ImGui::MenuItem("Rename Page"))
               {
                  gPerfRenamingPage = p;
                  snprintf(gPerfRenamePageBuffer, sizeof(gPerfRenamePageBuffer), "%s", pageTitle.c_str());
               }
               if (ImGui::MenuItem("Duplicate Page"))
               {
                  PushUndoCheckpoint();
                  int newP = gPerfLayout.pageCount++;
                  gPerfLayout.pageNames.push_back(pageTitle + " Copy");
                  // Copied first: push_back while iterating would reallocate under the loop.
                  std::vector<Patch::PerfRecord> copies;
                  for (const auto& el : gPerfElements)
                     if (el.page == p)
                     {
                        copies.push_back(el);
                        copies.back().page = newP;
                     }
                  gPerfElements.insert(gPerfElements.end(), copies.begin(), copies.end());
                  gPerfActivePage = newP;
               }
               if (gPerfLayout.pageCount > 1 && ImGui::MenuItem("Delete Page"))
               {
                  PushUndoCheckpoint();
                  PerfResetIndexState();
                  gPerfElements.erase(std::remove_if(gPerfElements.begin(), gPerfElements.end(),
                                                     [p](const Patch::PerfRecord& r) { return r.page == p; }),
                                      gPerfElements.end());
                  for (auto& r : gPerfElements)
                     if (r.page > p) r.page--;
                  gPerfLayout.pageCount--;
                  if (p < (int)gPerfLayout.pageNames.size())
                     gPerfLayout.pageNames.erase(gPerfLayout.pageNames.begin() + p);
                  if (gPerfActivePage >= gPerfLayout.pageCount)
                     gPerfActivePage = gPerfLayout.pageCount - 1;
               }
               ImGui::EndPopup();
            }
         }
         ImGui::PopID();
      }

      // Add new page '+' button
      if (gPerfLayout.pageCount < 12)
      {
         ImGui::SameLine();
         if (ImGui::Button("+##addpagebtn", ImVec2(24, 24)))
         {
            PushUndoCheckpoint();
            int newP = gPerfLayout.pageCount++;
            gPerfLayout.pageNames.push_back("Page " + std::to_string(newP + 1));
            gPerfActivePage = newP;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add New Page");
      }

      // Calculate dynamic content bounding box on active page
      const float cellSize = (float)gPerfLayout.cellSize;
      const float gap = 8.0f;
      float maxElemX = 0.0f;
      float maxElemY = 0.0f;
      for (const auto& el : gPerfElements)
      {
         if (el.page == gPerfActivePage)
         {
            ImVec2 s = GetPerfElementCellSpan(el.kind);
            maxElemX = std::max(maxElemX, (el.cellX + s.x) * (cellSize + gap) + 32.0f);
            maxElemY = std::max(maxElemY, (el.cellY + s.y) * (cellSize + gap) + 32.0f);
         }
      }

      // ---- Canvas Grid View ----
      ImGui::BeginChild("##perfgridcanvas", ImVec2(0, 0), false, ImGuiWindowFlags_None);
      ImVec2 gridOrigin = ImGui::GetCursorScreenPos();

      // Draw background grid lines in Edit mode
      if (gPerfEditMode)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         ImU32 gridCol = isLight ? IM_COL32(215, 220, 230, 80) : IM_COL32(48, 52, 65, 80);
         float gridMaxX = std::max(maxElemX + 200.0f, ImGui::GetWindowWidth());
         float gridMaxY = std::max(maxElemY + 200.0f, ImGui::GetWindowHeight());
         int numCols = (int)std::ceil(gridMaxX / (cellSize + gap)) + 1;
         int numRows = (int)std::ceil(gridMaxY / (cellSize + gap)) + 1;
         for (int x = 0; x < numCols; x++)
         {
            float px = gridOrigin.x + x * (cellSize + gap);
            dl->AddLine(ImVec2(px, gridOrigin.y), ImVec2(px, gridOrigin.y + gridMaxY), gridCol);
         }
         for (int y = 0; y < numRows; y++)
         {
            float py = gridOrigin.y + y * (cellSize + gap);
            dl->AddLine(ImVec2(gridOrigin.x, py), ImVec2(gridOrigin.x + gridMaxX, py), gridCol);
         }
      }

      // Draw elements belonging to active page
      bool mouseOverAnyElement = false;
      for (size_t i = 0; i < gPerfElements.size(); i++)
      {
         if (gPerfElements[i].page == gPerfActivePage)
            DrawPerfElement(i, gridOrigin, cellSize, mouseOverAnyElement);
      }

      // ---- Edit-mode selection shortcuts ----
      // The node editor's own copy/paste/duplicate/delete keys are global, so
      // gPerfMatrixFocused (read there) is what stops one keystroke acting on
      // both the graph and the matrix.
      if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                 ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
          (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
         gPerfMatrixClaimedKeys = true;
      gPerfMatrixFocused = gPerfMatrixClaimedKeys && gPerfEditMode;
      if (gPerfEditMode)
      {
         if (!mouseOverAnyElement && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
             ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyShift)
            gPerfSelection.clear();

         if (gPerfMatrixFocused && !ImGui::GetIO().WantTextInput)
         {
            const ImGuiIO& kio = ImGui::GetIO();
            const bool cmd = kio.KeySuper || kio.KeyCtrl;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
               PerfDeleteSelection();
            else if (cmd && ImGui::IsKeyPressed(ImGuiKey_C, false))
               PerfCopySelection();
            else if (cmd && ImGui::IsKeyPressed(ImGuiKey_X, false))
            {
               PerfCopySelection();
               PerfDeleteSelection();
            }
            else if (cmd && ImGui::IsKeyPressed(ImGuiKey_V, false))
               PerfPasteRecords(gPerfClipboard);
            else if (kio.KeyShift && !cmd && ImGui::IsKeyPressed(ImGuiKey_D, false))
               PerfDuplicateSelection();
            else if (cmd && ImGui::IsKeyPressed(ImGuiKey_A, false))
               PerfSelectAllOnPage();
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
               gPerfSelection.clear();
         }
      }
      else
      {
         gPerfSelection.clear();
      }

      // Context menu anywhere on blank canvas background (only if not hovering any control)
      if (!mouseOverAnyElement && ImGui::BeginPopupContextWindow("##perfcanvascontext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
      {
         if (ImGui::BeginMenu("Dock Position"))
         {
            if (ImGui::MenuItem("Bottom", nullptr, gPerfPanelDock == 0)) gPerfPanelDock = 0;
            if (ImGui::MenuItem("Right", nullptr, gPerfPanelDock == 1)) gPerfPanelDock = 1;
            if (ImGui::MenuItem("Left", nullptr, gPerfPanelDock == 2)) gPerfPanelDock = 2;
            if (ImGui::MenuItem("Top", nullptr, gPerfPanelDock == 3)) gPerfPanelDock = 3;
            ImGui::EndMenu();
         }
         ImGui::Separator();
         if (ImGui::BeginMenu("+ Add Control"))
         {
            if (ImGui::MenuItem("Knob (1x1)")) AddPerfElementToCurrentPage(0);
            if (ImGui::MenuItem("Vertical Fader (1x2)")) AddPerfElementToCurrentPage(1);
            if (ImGui::MenuItem("Horizontal Slider (2x1)")) AddPerfElementToCurrentPage(2);
            if (ImGui::MenuItem("Toggle (1x1)")) AddPerfElementToCurrentPage(3);
            if (ImGui::MenuItem("XY Pad (2x2)")) AddPerfElementToCurrentPage(4);
            ImGui::Separator();
            if (ImGui::MenuItem("Momentary Trigger / Bang (1x1)")) AddPerfElementToCurrentPage(5);
            if (ImGui::MenuItem("Digital Number Box (1x1)")) AddPerfElementToCurrentPage(6);
            if (ImGui::MenuItem("Radio Selector (2x1)")) AddPerfElementToCurrentPage(7);
            if (ImGui::MenuItem("Bipolar Pan Knob (1x1)")) AddPerfElementToCurrentPage(8);
            if (ImGui::MenuItem("Step Gate Ribbon (3x1)")) AddPerfElementToCurrentPage(9);
            ImGui::EndMenu();
         }
         if (ImGui::MenuItem("Clear Page Controls"))
         {
            PushUndoCheckpoint();
            PerfResetIndexState();
            gPerfElements.erase(std::remove_if(gPerfElements.begin(), gPerfElements.end(),
                                               [](const Patch::PerfRecord& r) { return r.page == gPerfActivePage; }),
                                gPerfElements.end());
            gPerfSelection.clear();
         }
         ImGui::Separator();
         if (ImGui::MenuItem("Close Performance Matrix"))
         {
            gPerfPanelOpen = false;
         }
         ImGui::EndPopup();
      }

      // Dynamic canvas dummy that only expands if elements actually overflow window
      if (maxElemX > 0.0f || maxElemY > 0.0f)
      {
         ImGui::Dummy(ImVec2(maxElemX, maxElemY));
      }
      ImGui::EndChild();
   }

   void DrawPerfPanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = 6.0f;
      const int dock = gPerfPanelDock;
      const bool vertical = (dock == 1 || dock == 2);
      const bool gripFirst = (dock == 0 || dock == 1);

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
      gPerfPanelRectMin = ImGui::GetWindowPos();
      gPerfPanelRectMax = ImVec2(gPerfPanelRectMin.x + ImGui::GetWindowSize().x,
                                 gPerfPanelRectMin.y + ImGui::GetWindowSize().y);
      const ImVec2 inner = ImGui::GetContentRegionAvail();

      auto grip = [&]()
      {
         ImGui::InvisibleButton("##perfpanelgrip",
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
               case 0: gPerfPanelHeight -= d.y; break;
               case 1: gPerfPanelWidth -= d.x; break;
               case 2: gPerfPanelWidth += d.x; break;
               default: gPerfPanelHeight += d.y; break;
            }
            gPerfPanelWidth = std::max(kPerfPanelMinWidth, gPerfPanelWidth);
            gPerfPanelHeight = std::max(kPerfPanelMinHeight, gPerfPanelHeight);
         }
      };

      if (gripFirst)
      {
         grip();
         if (vertical)
            ImGui::SameLine();
      }

      const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
      PushDockedPanelStyle(/*isChild=*/true);
      // Same padding-loss trap as ModMatrix's content child above - this is
      // the exact cause of "Edit Mode / Perform" sitting flush against the
      // panel edge with no breathing room.
      ImGui::BeginChild("##perfpanelinnercontent",
                        vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), inner.y)
                                 : ImVec2(0, std::max(1.0f, inner.y - kGrip - gap.y)),
                        ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawPerfPanelContent();
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
