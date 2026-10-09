// Performance matrix panel, MIDI learn, modulator meter (moved verbatim from main.cpp).
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/StateRing.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   // ---- Performance Matrix ----
   void PerfPanelDockCombo()
   {
      static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top") };
      MenuParts::PushPopupPad();
      const bool dockOpen = ImGui::BeginCombo("##perfpaneldock", T(kDockLabels[gPerfPanelDock]));
      ImGui::PopStyleVar();
      if (dockOpen)
      {
         MenuParts::BeginContent();
         for (int i = 0; i < 4; i++)
            if (MenuParts::Choice(L(kDockLabels[i]), i == gPerfPanelDock))
               gPerfPanelDock = i;
         MenuParts::EndContent();
         ImGui::EndCombo();
      }
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
      if (auto* mixer = dynamic_cast<MixerNode*>(gn->node.get()))
      {
         if (boolName == "mute" && channelIdx >= 0 && channelIdx < MixerNode::kMaxSlots)
            return mixer->mute[channelIdx];
         if (boolName == "solo" && channelIdx >= 0 && channelIdx < MixerNode::kMaxSlots)
            return mixer->solo[channelIdx];
      }
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
      if (boolName == "bypassed" && !CanBypass(*gn))
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
      if (auto* mixer = dynamic_cast<MixerNode*>(gn->node.get()))
      {
         if (boolName == "mute" && channelIdx >= 0 && channelIdx < MixerNode::kMaxSlots)
         {
            mixer->mute[channelIdx] = newVal;
            return;
         }
         if (boolName == "solo" && channelIdx >= 0 && channelIdx < MixerNode::kMaxSlots)
         {
            mixer->solo[channelIdx] = newVal;
            return;
         }
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
      return isLight ? tok::U32(tok::pal::c_505A6EFF) : tok::U32(tok::pal::c_8C96AFFF);
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


   void AddToPerformanceMatrix(int nodeIndex, int paramIndex, int kind, const std::string& customLabel,
                                int paramIndex2, const std::string& boolName)
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


   void PerfDeleteSelection()
   {
      if (gPerfSelection.empty())
         return;
      PushUndoCheckpoint();
      // Back to front, so an erase never shifts an index still to be removed.
      for (auto it = gPerfSelection.rbegin(); it != gPerfSelection.rend(); ++it)
         if (*it < gPerfElements.size())
            gPerfElements.erase(gPerfElements.begin() + (long)*it);
      gPerfSelection.clear();
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
         StateRing::Draw(dl, snapTL, snapBR, StateRing::Kind::Target, isLight, tok::radius_pill);

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
      const float kCardR = tok::radius_pill;
      const ImVec4 uiText = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImU32 cardBg = isLight ? ImGui::GetColorU32(ImVec4(1, 1, 1, 0.62f))
                                   : ImGui::GetColorU32(ImVec4(uiText.x, uiText.y, uiText.z, 0.06f));
      const ImU32 cardHair = isLight ? ImGui::GetColorU32(ImVec4(0, 0, 0, 0.07f))
                                     : ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f));

      // One recessed-well fill for every field-like control (NumBox, XY pad, step off, selector off, toggle off).
      auto wellCol = [&](float a) { return ImGui::GetColorU32(ImVec4(uiText.x, uiText.y, uiText.z, a)); };

      dl->AddRectFilled(cellPos, cardBR, cardBg, kCardR);
      if (gPerfEditMode)
         dl->AddRect(cellPos, cardBR, dstNode != nullptr ? themeTint : ImGui::GetColorU32(ImVec4(uiText.x, uiText.y, uiText.z, 0.22f)), kCardR, 0, 1.2f);
      if (gPerfMidiLearnIdx == (int)elemIdx)
         StateRing::Draw(dl, cellPos, cardBR, StateRing::Kind::Learn, isLight, kCardR);
      else if (gPerfEditMode && gPerfSelection.count(elemIdx) > 0)
         StateRing::Draw(dl, cellPos, cardBR, StateRing::Kind::Select, isLight, kCardR);
      else
         dl->AddRect(cellPos, cardBR, cardHair, kCardR, 0, 1.0f);

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
      // No bar and no dot: a quiet caption, like the Library section headers.
      const float titleX = cellPos.x + tok::space_2;
      ImVec2 titlePos(titleX, cellPos.y + 3.0f);
      ImU32 textCol = ImGui::GetColorU32(ImVec4(uiText.x, uiText.y, uiText.z, 0.65f));

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
         if (MenuParts::BeginContextItem("##elemcontext", ImGuiPopupFlags_MouseButtonRight))
         {
            if (MenuParts::Item(L("Rename")))
            {
               gPerfRenamingElementIdx = (int)elemIdx;
               snprintf(gPerfRenameElementBuffer, sizeof(gPerfRenameElementBuffer), "%s", displayLabel.c_str());
            }

            if (MenuParts::SubMenu(L("Type")))
            {
               if (MenuParts::Item(L("Knob (1x1)"), nullptr, elem.kind == 0)) { PushUndoCheckpoint(); elem.kind = 0; }
               if (MenuParts::Item(L("Vertical Fader (1x2)"), nullptr, elem.kind == 1)) { PushUndoCheckpoint(); elem.kind = 1; }
               if (MenuParts::Item(L("Horizontal Slider (2x1)"), nullptr, elem.kind == 2)) { PushUndoCheckpoint(); elem.kind = 2; }
               if (MenuParts::Item(L("Toggle (1x1)"), nullptr, elem.kind == 3)) { PushUndoCheckpoint(); elem.kind = 3; }
               if (MenuParts::Item(L("XY Pad (2x2)"), nullptr, elem.kind == 4)) { PushUndoCheckpoint(); elem.kind = 4; }
               if (MenuParts::Item(L("Trigger / Bang (1x1)"), nullptr, elem.kind == 5)) { PushUndoCheckpoint(); elem.kind = 5; }
               if (MenuParts::Item(L("Number Box (1x1)"), nullptr, elem.kind == 6)) { PushUndoCheckpoint(); elem.kind = 6; }
               if (MenuParts::Item(L("Radio Selector (2x1)"), nullptr, elem.kind == 7)) { PushUndoCheckpoint(); elem.kind = 7; }
               if (MenuParts::Item(L("Bipolar Knob (1x1)"), nullptr, elem.kind == 8)) { PushUndoCheckpoint(); elem.kind = 8; }
               if (MenuParts::Item(L("Step Gate (3x1)"), nullptr, elem.kind == 9)) { PushUndoCheckpoint(); elem.kind = 9; }
               ImGui::EndMenu();
            }

            MenuParts::Separator();
            if (elem.kind == 4) // XY Pad
            {
               if (MenuParts::Item(L("Assign X Axis...")))
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

               if (MenuParts::Item(L("Assign Y Axis...")))
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

               if (elem.dstIndex >= 0 && MenuParts::Item(L("Clear Destinations")))
               {
                  PushUndoCheckpoint();
                  elem.dstIndex = -1;
                  elem.dstParam = -1;
                  elem.dstParam2 = -1;
                  elem.targets.clear();
                  elem.targetsY.clear();
               }

               MenuParts::Separator();
               // MIDI Learn for XY Pad (X Axis and Y Axis)
               const bool isLearningX = (gPerfMidiLearnIdx == (int)elemIdx && gPerfMidiLearnAxis == 0);
               const bool isLearningY = (gPerfMidiLearnIdx == (int)elemIdx && gPerfMidiLearnAxis == 1);
               if (isLearningX)
               {
                  if (MenuParts::Item(L("Listening X... (Move MIDI CC)")))
                     gPerfMidiLearnIdx = -1;
               }
               else
               {
                  std::string xLabel = (elem.midiDevice != 0) ? "Re-learn MIDI X Axis..." : "MIDI Learn X Axis...";
                  if (MenuParts::Item(xLabel.c_str()))
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
                  std::string bindStr = "MIDI X: " + (devName.empty() ? "" : devName + " \xC2\xB7 ") + "Ch " + std::to_string(elem.midiChannel + 1) + " \xC2\xB7 " + Platform::MidiBindingName(elem.midiIsNote, elem.midiController);
                  ImGui::TextDisabled("%s", bindStr.c_str());
               }

               if (isLearningY)
               {
                  if (MenuParts::Item(L("Listening Y... (Move MIDI CC)")))
                     gPerfMidiLearnIdx = -1;
               }
               else
               {
                  std::string yLabel = (elem.midiDeviceY != 0) ? "Re-learn MIDI Y Axis..." : "MIDI Learn Y Axis...";
                  if (MenuParts::Item(yLabel.c_str()))
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
                  std::string bindStr = "MIDI Y: " + (devName.empty() ? "" : devName + " \xC2\xB7 ") + "Ch " + std::to_string(elem.midiChannelY + 1) + " \xC2\xB7 " + Platform::MidiBindingName(elem.midiIsNoteY, elem.midiControllerY);
                  ImGui::TextDisabled("%s", bindStr.c_str());
               }

               if ((elem.midiDevice != 0 || elem.midiDeviceY != 0) && MenuParts::Item(L("Clear MIDI Bindings")))
               {
                  PushUndoCheckpoint();
                  elem.midiDevice = 0; elem.midiChannel = -1; elem.midiController = -1; elem.midiIsNote = false;
                  elem.midiDeviceY = 0; elem.midiChannelY = -1; elem.midiControllerY = -1; elem.midiIsNoteY = false;
                  if (gPerfMidiLearnIdx == (int)elemIdx) gPerfMidiLearnIdx = -1;
               }
            }
            else // Regular single-axis control
            {
               if (MenuParts::Item(L("Assign Parameter...")))
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

               if (elem.dstIndex >= 0 && MenuParts::Item(L("Clear Destination")))
               {
                  PushUndoCheckpoint();
                  elem.dstIndex = -1;
                  elem.dstParam = -1;
                  elem.boolName.clear();
                  elem.targets.clear();
               }

               if (elem.kind != 9) // Step Gate does not use MIDI Learn
               {
                  MenuParts::Separator();
                  const bool isLearning = (gPerfMidiLearnIdx == (int)elemIdx && gPerfMidiLearnAxis == 0);
                  if (isLearning)
                  {
                     std::string learnPrompt = (elem.kind == 3 || elem.kind == 5 || elem.kind == 7)
                        ? "Listening... (Move CC or Hit Pad)"
                        : "Listening... (Move MIDI CC)";
                     if (MenuParts::Item(learnPrompt.c_str()))
                        gPerfMidiLearnIdx = -1;
                  }
                  else
                  {
                     std::string midiLabel = (elem.midiDevice != 0) ? "Re-learn MIDI CC" : "MIDI CC Learn";
                     if (elem.kind == 3 || elem.kind == 5 || elem.kind == 7)
                        midiLabel = (elem.midiDevice != 0) ? "Re-learn MIDI (CC / Trigger)" : "MIDI Learn (CC / Trigger)";
                     if (MenuParts::Item(midiLabel.c_str()))
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
                     std::string bindStr = "MIDI: " + (devName.empty() ? "" : devName + " \xC2\xB7 ") + "Ch " + std::to_string(elem.midiChannel + 1) + " \xC2\xB7 " + Platform::MidiBindingName(elem.midiIsNote, elem.midiController);
                     ImGui::TextDisabled("%s", bindStr.c_str());

                     if (MenuParts::Item(L("Clear MIDI Binding")))
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

            MenuParts::Separator();
            if (MenuParts::SubMenu(L("Color Tint")))
            {
               static const struct { const char* name; ImU32 col; } kPaletteColors[10] = {
                  { "Default", tok::U32(tok::pal::c_6E788CFF) },
                  { "Crimson", tok::U32(tok::pal::c_EF4444FF) },
                  { "Orange",  tok::U32(tok::pal::c_F97316FF) },
                  { "Amber",   tok::U32(tok::pal::c_F59E0BFF) },
                  { "Emerald", tok::U32(tok::pal::c_10B981FF) },
                  { "Cyan",    tok::U32(tok::pal::c_06B6D4FF) },
                  { "Blue",    tok::U32(tok::pal::c_3B82F6FF) },
                  { "Purple",  tok::U32(tok::pal::c_8B5CF6FF) },
                  { "Magenta", tok::U32(tok::pal::c_D946EFFF) },
                  { "Rose",    tok::U32(tok::pal::c_F43F5EFF) }
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

            MenuParts::EndPopup();
         }
      }

      // Inline renaming or title text
      if (gPerfRenamingElementIdx == (int)elemIdx)
      {
         ImGui::SetCursorScreenPos(ImVec2(cellPos.x + 4.0f, cellPos.y + 1.0f));
         ImGui::SetNextItemWidth(cardSize.x - 8.0f);
         ImGui::SetKeyboardFocusHere();
         if (FieldWell::InputText("##renamingelemfield", gPerfRenameElementBuffer, sizeof(gPerfRenameElementBuffer),
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
         {
            UiType::Scope ts(UiType::Size::Caption, UiType::Weight::Semibold);
            dl->AddText(titlePos, textCol, displayLabel.c_str());
         }
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

         FaderPosToValueFn p2v = kp.posToValue;
         FaderValueToPosFn v2p = kp.valueToPos;
         ImU32 fillCol = isModulated ? (isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF)) : themeTint;

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

         const bool isDb = (minV < 0.0f && maxV <= 12.0f && kp.posToValue == ConsoleFaderTaper::PosToValue);
         FaderPosToValueFn p2v = kp.posToValue;
         FaderValueToPosFn v2p = kp.valueToPos;
         ImU32 fillCol = isModulated ? (isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF)) : themeTint;

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

         ImU32 fillCol = isModulated ? (isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF)) : themeTint;
         float v = dragSeed(val);
         FaderPosToValueFn p2v = kp.posToValue;
         FaderValueToPosFn v2p = kp.valueToPos;
         // Same anatomy as the vertical fader, turned on its side: a thin recessed track, a fill in the
         // control's colour, tick marks above and below, and a rectangular cap with a centre line.
         auto toPos = [&](float x) { return v2p ? std::clamp(v2p(x, minV, maxV), 0.0f, 1.0f) : std::clamp((x - minV) / (maxV - minV), 0.0f, 1.0f); };
         const ImVec2 sTL = ImGui::GetCursorScreenPos();
         const float sH = 30.0f;
         ImGui::InvisibleButton("##hslider", ImVec2(sliderW, sH));
         const bool sActive = !isModulated && ImGui::IsItemActive();
         const bool sHover = ImGui::IsItemHovered();
         bool sliderMoved = false;
         const float left = sTL.x + 10.0f, right = sTL.x + sliderW - 10.0f, cy = sTL.y + sH * 0.5f;
         if (sActive)
         {
            const float pos = std::clamp((ImGui::GetIO().MousePos.x - left) / (right - left), 0.0f, 1.0f);
            const float next = p2v ? std::clamp(p2v(pos, minV, maxV), minV, maxV) : std::clamp(minV + (maxV - minV) * pos, minV, maxV);
            if (next != v) { v = next; sliderMoved = true; }
         }
         const float capX = left + toPos(v) * (right - left);
         const ImU32 tickCol = wellCol(0.2f);
         for (int i = 0; i <= 4; i++)
         {
            const float x = left + (float)i * 0.25f * (right - left);
            dl->AddLine(ImVec2(x, cy - 12.0f), ImVec2(x, cy - 9.0f), tickCol, 1.0f);
            dl->AddLine(ImVec2(x, cy + 9.0f), ImVec2(x, cy + 12.0f), tickCol, 1.0f);
         }
         dl->AddRectFilled(ImVec2(left - 4.0f, cy - 3.0f), ImVec2(right + 4.0f, cy + 3.0f), wellCol(0.12f), 3.0f);
         if (capX > left)
            dl->AddRectFilled(ImVec2(left - 4.0f, cy - 2.0f), ImVec2(capX, cy + 2.0f), fillCol, 2.0f);
         const ImVec2 cTL(capX - 6.0f, cy - 9.0f), cBR(capX + 6.0f, cy + 9.0f);
         dl->AddRectFilled(cTL, cBR, isLight ? ImGui::GetColorU32(ImVec4(1, 1, 1, 0.95f)) : wellCol(0.28f), 3.0f);
         dl->AddRect(cTL, cBR, wellCol(sActive ? 0.5f : (sHover ? 0.3f : 0.15f)), 3.0f);
         dl->AddLine(ImVec2(capX, cy - 7.0f), ImVec2(capX, cy + 7.0f), wellCol(0.8f), 1.6f);
         if (sHover || sActive)
         {
            char valBuf[48];
            FormatAudioParam(valBuf, sizeof(valBuf), "%.2f", v);
            SetAudioReadout(displayLabel.c_str(), valBuf);
         }
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

         const bool toggleClicked = ActionButton::Draw(curVal ? "ON" : "OFF", ImVec2(btnSize, btnSize),
                                                       curVal ? ActionButton::Kind::Selected : ActionButton::Kind::Plain);
         if (toggleClicked)
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
            float newX = kpX.posToValue ? kpX.posToValue(normX, minX, maxX) : (minX + (maxX - minX) * normX);
            float newY = kpY.posToValue ? kpY.posToValue(normY, minY, maxY) : (minY + (maxY - minY) * normY);
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
                  float tNew = tKp.posToValue ? tKp.posToValue(normX, tKp.minValue, tKp.maxValue) : (tKp.minValue + (tKp.maxValue - tKp.minValue) * normX);
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = tNew;
               }
            }
            for (const auto& t : elem.targetsY)
            {
               if (t.dstIndex >= 0 && t.dstParam >= 0)
               {
                  auto [tVal, tKp] = getParamRef(t.dstIndex, t.dstParam, elem.value2);
                  float tNew = tKp.posToValue ? tKp.posToValue(normY, tKp.minValue, tKp.maxValue) : (tKp.minValue + (tKp.maxValue - tKp.minValue) * normY);
                  gPerfPendingWrites[{t.dstIndex, t.dstParam}] = tNew;
               }
            }
            valX = newX;
            valY = newY;
         }

         dl->AddRectFilled(origin, padBR, wellCol(0.07f), tok::radius_tile);
         dl->AddLine(ImVec2(origin.x + padSize * 0.5f, origin.y + 4.0f), ImVec2(origin.x + padSize * 0.5f, padBR.y - 4.0f), wellCol(0.10f));
         dl->AddLine(ImVec2(origin.x + 4.0f, origin.y + padSize * 0.5f), ImVec2(padBR.x - 4.0f, origin.y + padSize * 0.5f), wellCol(0.10f));

         float normX = kpX.valueToPos ? kpX.valueToPos(valX, minX, maxX) : ((maxX > minX) ? std::clamp((valX - minX) / (maxX - minX), 0.0f, 1.0f) : 0.0f);
         float normY = kpY.valueToPos ? kpY.valueToPos(valY, minY, maxY) : ((maxY > minY) ? std::clamp((valY - minY) / (maxY - minY), 0.0f, 1.0f) : 0.0f);
         ImVec2 orbPos(origin.x + normX * padSize, origin.y + (1.0f - normY) * padSize);
         dl->AddCircleFilled(orbPos, 9.0f, (themeTint & 0x00FFFFFF) | 0x33000000);
         dl->AddCircleFilled(orbPos, 5.5f, themeTint);
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
         ImU32 baseCol = wellCol(0.08f);
         dl->AddCircleFilled(center, r, baseCol, 32);

         if (flash > 0.0f)
         {
            ImU32 flashCol = (themeTint & 0x00FFFFFF) | ((ImU32)(flash * 220.0f) << 24);
            dl->AddCircleFilled(center, r * (0.6f + 0.4f * flash), flashCol, 32);
            dl->AddCircle(center, r + 2.0f * (1.0f - flash), (themeTint & 0x00FFFFFF) | ((ImU32)(flash * 255.0f) << 24), 32, 2.0f);
         }
         else
         {
            dl->AddCircleFilled(center, r * 0.55f, (themeTint & 0x00FFFFFF) | 0x88000000, 32);
         }
         if (hovered)
            dl->AddCircle(center, r + 2.0f, wellCol(0.25f), 32, 1.2f);
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

         dl->AddRectFilled(bTL, bBR, wellCol(0.07f), tok::radius_tile);

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

            // Either every button shows its option name or none of them do.
            // The old rule was per-button ("use the name if it is <= 4 chars"),
            // which for a list like {Flat, Sphere, Cylinder, ...} labelled the
            // first button "Flat" - clipped to "Fla" - and the rest 2..7,
            // reading as a broken 1..8 row. useNames is decided once, above
            // the loop.
            std::string btnText = useNames ? kp.enumOptions[b] : std::to_string(b + 1);
            if (ActionButton::Draw(btnText.c_str(), ImVec2(btnW, btnH),
                                   isSelected ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
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

         ImU32 fillCol = isModulated ? (isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF)) : themeTint;
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
               : wellCol(0.08f);

            dl->AddRectFilled(sTL, sBR, stepBg, tok::radius_tile);

            if (isCurrent && Transport::Instance().IsPlaying())
               dl->AddRectFilled(ImVec2(sx + 2.0f, sBR.y + 3.0f), ImVec2(sx + stepW - 2.0f, sBR.y + 5.0f), wellCol(0.85f), 1.0f);
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
            dl->AddRectFilled(bodyTL, cardBR, isLight ? tok::U32(tok::pal::c_F5F7FC96) : tok::U32(tok::pal::c_161921A0),
                              kCardR, ImDrawFlags_RoundCornersBottom);
            const char* tag = "bypassed";
            const ImVec2 tagSize = ImGui::CalcTextSize(tag);
            const ImVec2 tagTL(cardBR.x - tagSize.x - 10.0f, cellPos.y + 2.0f);
            const ImVec2 tagBR(cardBR.x - 4.0f, cellPos.y + 2.0f + tagSize.y);
            dl->AddRectFilled(ImVec2(tagTL.x - 3.0f, tagTL.y), tagBR, cardBg, 3.0f);
            dl->AddText(tagTL, isLight ? tok::U32(tok::pal::c_BE6E1EFF) : tok::U32(tok::pal::c_F0AA46FF), tag);
            if (ImGui::IsMouseHoveringRect(ImVec2(tagTL.x - 3.0f, tagTL.y), tagBR))
               ImGui::SetTooltip("%s", T("This node is bypassed - the control still stores its value, which applies when the node is back in the chain"));
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

         // A binding saved with a device id that isn't connected (older
         // macOS builds saved a per-launch handle, so every reopened or
         // crash-recovered patch lost its MIDI mappings) reattaches to the
         // connected device that sends the same control.
         auto rebind = [](int& dev, int ch, int ctrl, bool note)
         {
            if (dev == 0)
               return;
            Platform::MidiDeviceId id = (Platform::MidiDeviceId)dev;
            if (Platform::MidiRebindStaleDevice(id, ch, ctrl, note))
            {
               dev = (int)id;
               gPatchDirty = true;
            }
         };
         rebind(elem.midiDevice, elem.midiChannel, elem.midiController, elem.midiIsNote);
         rebind(elem.midiDeviceY, elem.midiChannelY, elem.midiControllerY, elem.midiIsNoteY);

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


   // ---- Per-parameter MIDI learn ------------------------------------------
   // Right-click a parameter > "MIDI learn", then move a hardware control. The
   // captured (device, channel, controller/note) becomes a MIDI CC modulator
   // node bound to that parameter through the ordinary Modulation binding, so
   // it is visible on the canvas, removable via Unbind / node delete, undoable
   // and saved by the existing node + "mod" line paths. Nothing here is a
   // second mapping system.
   //
   // One live learn at a time: MidiLearnCancelAll() is the single choke point
   // every learner (this, Performance Matrix, MIDI CC node, MIDI Trigger node)
   // calls before it starts, and it also runs on patch reset/load/undo/redo.
   // It also flushes the depth-1 Platform::MidiPollLastTouched queue so the
   // new learner never captures a stale event.

   bool ParamMidiLearnActive()
   {
      return gParamMidiLearnUid != 0 && gParamMidiLearnParam >= 0;
   }


   int MidiLearnActiveCount()
   {
      int count = 0;
      if (gPerfMidiLearnIdx >= 0)
         count++;
      if (ParamMidiLearnActive())
         count++;
      for (GraphNode& gn : gNodes)
      {
         if (auto* cc = dynamic_cast<MidiCCNode*>(gn.node.get()))
         {
            if (cc->IsLearning())
               count++;
         }
         else if (auto* trig = dynamic_cast<MidiTriggerNode*>(gn.node.get()))
         {
            if (trig->IsLearning())
               count++;
         }
      }
      return count;
   }


   void MidiLearnCancelAll()
   {
      gPerfMidiLearnIdx = -1;
      gParamMidiLearnUid = 0;
      gParamMidiLearnParam = -1;
      for (GraphNode& gn : gNodes)
      {
         if (auto* cc = dynamic_cast<MidiCCNode*>(gn.node.get()))
            cc->CancelLearn();
         else if (auto* trig = dynamic_cast<MidiTriggerNode*>(gn.node.get()))
            trig->CancelLearn();
      }
      if (Platform::MidiIsRunning())
      {
         Platform::MidiCCValue flush;
         while (Platform::MidiPollLastTouched(flush)) {}
      }
   }


   bool ParamMidiLearnIsActiveFor(int nodeIndex, int paramIndex)
   {
      if (!ParamMidiLearnActive() || paramIndex != gParamMidiLearnParam)
         return false;
      return UidForIndex(nodeIndex) == gParamMidiLearnUid;
   }


   void StartParamMidiLearn(int nodeIndex, int paramIndex)
   {
      const uint64_t uid = UidForIndex(nodeIndex);
      MidiLearnCancelAll();
      if (uid == 0 || paramIndex < 0)
         return;
      std::string err;
      if (!Platform::MidiIsRunning() && !Platform::MidiStart(err))
         return; // no MIDI input available: nothing to listen to
      gParamMidiLearnUid = uid;
      gParamMidiLearnParam = paramIndex;
      // The start may have raced a stale event in between the flush above and
      // MidiStart bringing the queue up.
      Platform::MidiCCValue flush;
      while (Platform::MidiPollLastTouched(flush)) {}
   }


   // A learnable target is a param nothing else drives. A binding from a MIDI
   // CC node is fine (that is "re-learn"); a binding from any other modulator,
   // an expression, or a gesture recording is refused rather than silently
   // replaced. Also refuses a MIDI CC node's own params (learning a CC node's
   // "low" from the CC would be a feedback trap the user did not ask for).
   bool ParamMidiLearnable(int nodeIndex, int paramIndex)
   {
      Modulation& mod = Modulation::Instance();
      if (mod.HasExpression(nodeIndex, paramIndex))
         return false;
      GestureRecorder& rec = GestureRecorder::Instance();
      if (rec.IsArmed(nodeIndex, paramIndex) || rec.Playbacks().count(GestureRecorder::Key(nodeIndex, paramIndex)) > 0)
         return false;
      const Modulation::Source cur = mod.ModulatorFor(nodeIndex, paramIndex);
      if (cur.nodeIndex >= 0)
      {
         GraphNode* srcNode = FindNodeByIndex(cur.nodeIndex);
         if (srcNode == nullptr || dynamic_cast<MidiCCNode*>(srcNode->node.get()) == nullptr)
            return false;
      }
      return true;
   }


   // Core of the capture, separated from the polling so the headless test can
   // drive it. Returns true when the binding was made.
   bool ParamMidiLearnCommit(int nodeIndex, int paramIndex, const Platform::MidiCCValue& last)
   {
      if (!ParamMidiLearnable(nodeIndex, paramIndex))
         return false;
      GraphNode* dest = FindNodeByIndex(nodeIndex);
      if (dest == nullptr)
         return false;
      Modulation& mod = Modulation::Instance();

      // One undo step for the whole gesture (spawn + bind).
      PushUndoCheckpoint();
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      gSuppressUndoCheckpoints = true;

      // Reuse a live modulator already on this exact control so one knob can
      // drive several params. Bypassed ones are skipped: a bypassed modulator
      // neither cooks nor applies. Non-default range/invert are the user's
      // own tuning, so those nodes are not shared.
      int srcIndex = -1;
      for (GraphNode& gn : gNodes)
      {
         auto* cc = dynamic_cast<MidiCCNode*>(gn.node.get());
         if (cc == nullptr || cc->bypassed)
            continue;
         if (cc->device == (int)last.device && cc->channel == last.channel &&
             cc->controller == last.controller && cc->isNote == last.isNote &&
             cc->low == 0.0f && cc->high == 1.0f && !cc->invert)
         {
            srcIndex = gn.index;
            break;
         }
      }
      if (srcIndex < 0)
      {
         float sx = dest->spawnX - 320.0f;
         float sy = dest->spawnY;
         if (gEditor != nullptr)
         {
            ed::EditorContext* prev = ed::GetCurrentEditor();
            ed::SetCurrentEditor(gEditor);
            const ImVec2 p = ed::GetNodePosition(dest->NodeId());
            ed::SetCurrentEditor(prev);
            const ImVec2 spot = FindFreeSpawnPosition(ImVec2(p.x - 320.0f, p.y));
            sx = spot.x;
            sy = spot.y;
         }
         GraphNode* spawned = SpawnNode("MIDI CC", "Modulators", sx, sy);
         auto* cc = spawned != nullptr ? dynamic_cast<MidiCCNode*>(spawned->node.get()) : nullptr;
         if (cc == nullptr)
         {
            gSuppressUndoCheckpoints = wasSuppressed;
            return false;
         }
         cc->device = (int)last.device;
         cc->channel = last.channel;
         cc->controller = last.controller;
         cc->isNote = last.isNote;
         srcIndex = spawned->index;
      }

      // Re-resolve after the spawn: gNodes may have reallocated.
      mod.Bind(nodeIndex, paramIndex, srcIndex, 0);
      const Modulation::Source bound = mod.ModulatorFor(nodeIndex, paramIndex);
      if (!bound.hasRange)
      {
         // Param was not in this frame's FrameParams (collapsed node): fall
         // back to the sticky record of its declared span.
         if (const ParamRef* known = mod.KnownParam(nodeIndex, paramIndex))
            mod.SetRange(nodeIndex, paramIndex, known->minValue, known->maxValue);
      }
      gSuppressUndoCheckpoints = wasSuppressed;
      return true;
   }


   // Main thread, once per frame, outside ed::Begin/End and before the
   // modulation apply (which needs this frame's FrameParams for Bind).
   void UpdateParamMidiLearn()
   {
      if (!ParamMidiLearnActive())
         return;
      ImGuiIO& lio = ImGui::GetIO();
      if (ImGui::GetCurrentContext() != nullptr && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !lio.WantTextInput)
      {
         MidiLearnCancelAll();
         return;
      }
      GraphNode* dest = FindNodeByUid(gParamMidiLearnUid);
      if (dest == nullptr)
      {
         MidiLearnCancelAll(); // target deleted while listening
         return;
      }
      Platform::MidiCCValue last;
      if (!Platform::MidiPollLastTouched(last))
         return;
      const int nodeIndex = dest->index;
      const int paramIndex = gParamMidiLearnParam;
      gParamMidiLearnUid = 0;
      gParamMidiLearnParam = -1;
      ParamMidiLearnCommit(nodeIndex, paramIndex, last);
   }


   // Orange strip along the top of the canvas while listening. Drawn on the
   // foreground list so it needs no window of its own.
   void DrawParamMidiLearnBanner()
   {
      if (!ParamMidiLearnActive())
         return;
      GraphNode* dest = FindNodeByUid(gParamMidiLearnUid);
      if (dest == nullptr)
         return;
      const ParamRef* known = Modulation::Instance().KnownParam(dest->index, gParamMidiLearnParam);
      char text[192];
      snprintf(text, sizeof(text), "MIDI learn: %s > %s - move a knob, fader or pad (Esc to cancel)",
               dest->typeName.c_str(), (known != nullptr && !known->name.empty()) ? known->name.c_str() : "parameter");
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      const bool isLightT = CategoryColors::IsThemeLight();
      const ImVec4 uiTxt = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      UiType::Scope ts(UiType::Size::Title);
      const ImVec2 tsz = ImGui::CalcTextSize(text);
      const ImGuiViewport* vp = ImGui::GetMainViewport();
      // Same neutral strip as the performance panel's listening banner: recessed well, hairline, plain text.
      const ImVec2 a(vp->Pos.x + (vp->Size.x - tsz.x) * 0.5f - tok::space_3, vp->Pos.y + 44.0f);
      const ImVec2 b(a.x + tsz.x + 2.0f * tok::space_3, a.y + 30.0f);
      dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_PopupBg), tok::radius_pill);
      dl->AddRect(a, b, ImGui::GetColorU32(isLightT ? ImVec4(0, 0, 0, 0.055f) : ImVec4(1, 1, 1, 0.04f)), tok::radius_pill);
      dl->AddText(ImVec2(a.x + tok::space_3, std::round(a.y + (30.0f - tsz.y) * 0.5f)), ImGui::GetColorU32(ImVec4(uiTxt.x, uiTxt.y, uiTxt.z, 0.9f)), text);
   }


   // The right-click entry. Inside the ##modbind popup.
   void DrawParamMidiLearnMenuItem(int nodeIndex, int paramIndex)
   {
      if (ParamMidiLearnIsActiveFor(nodeIndex, paramIndex))
      {
         if (MenuParts::Item(L("Listening... (click or Esc to cancel)")))
            MidiLearnCancelAll();
         return;
      }
      if (!ParamMidiLearnable(nodeIndex, paramIndex))
      {
         ImGui::BeginDisabled();
         MenuParts::Item(L("MIDI learn (already driven by something else)"));
         ImGui::EndDisabled();
         return;
      }
      const Modulation::Source cur = Modulation::Instance().ModulatorFor(nodeIndex, paramIndex);
      if (MenuParts::Item(cur.nodeIndex >= 0 ? "Re-learn MIDI" : "MIDI learn"))
         StartParamMidiLearn(nodeIndex, paramIndex);
   }


   constexpr float kPerfChipH = 28.0f;   // toolbar chips: one tile high

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
         // A slim status strip: what is being learned, what to do, and Cancel as a chip. One text size throughout.
         const auto& elem = gPerfElements[gPerfMidiLearnIdx];
         const bool isLightT = CategoryColors::IsThemeLight();
         const ImVec4 uiTxt = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         const float stripH = 30.0f;
         const ImVec2 sp = ImGui::GetCursorScreenPos();
         const float stripW = ImGui::GetContentRegionAvail().x;
         ImDrawList* sdl = ImGui::GetWindowDrawList();
         // Same recessed well as the Library list: neutral, no accent colour.
         sdl->AddRectFilled(sp, ImVec2(sp.x + stripW, sp.y + stripH), ImGui::GetColorU32(ImVec4(0, 0, 0, isLightT ? 0.04f : 0.27f)), tok::radius_pill);
         sdl->AddRect(sp, ImVec2(sp.x + stripW, sp.y + stripH), ImGui::GetColorU32(isLightT ? ImVec4(0, 0, 0, 0.055f) : ImVec4(1, 1, 1, 0.04f)), tok::radius_pill);
         const float cy = sp.y + stripH * 0.5f;
         const std::string axisStr = (gPerfMidiLearnAxis == 1) ? " \xC2\xB7 Y axis" : (elem.kind == 4 ? " \xC2\xB7 X axis" : "");
         const bool pad = (elem.kind == 3 || elem.kind == 5 || elem.kind == 7);
         const std::string head = "Listening for MIDI";
         const std::string target = (elem.label.empty() ? std::string() : elem.label) + axisStr;
         const char* hint = pad ? "Move a control or hit a pad on your controller. Esc cancels."
                                : "Move a knob, fader or wheel on your controller. Esc cancels.";
         float tx = sp.x + tok::space_3;
         {
            UiType::Scope ts(UiType::Size::Title, UiType::Weight::Semibold);
            sdl->AddText(ImVec2(tx, std::round(cy - ImGui::GetFontSize() * 0.5f)), ImGui::GetColorU32(ImVec4(uiTxt.x, uiTxt.y, uiTxt.z, 0.6f)), head.c_str());
            tx += ImGui::CalcTextSize(head.c_str()).x + tok::space_2;
            if (!target.empty())
            {
               sdl->AddText(ImVec2(tx, std::round(cy - ImGui::GetFontSize() * 0.5f)), ImGui::GetColorU32(ImVec4(uiTxt.x, uiTxt.y, uiTxt.z, 0.9f)), target.c_str());
               tx += ImGui::CalcTextSize(target.c_str()).x;
            }
            tx += tok::space_3;
         }
         {
            UiType::Scope ts(UiType::Size::Title);
            const float room = sp.x + stripW - 84.0f - tx;
            if (ImGui::CalcTextSize(hint).x <= room)
               sdl->AddText(ImVec2(tx, std::round(cy - ImGui::GetFontSize() * 0.5f)), ImGui::GetColorU32(ImVec4(uiTxt.x, uiTxt.y, uiTxt.z, 0.4f)), hint);
         }
         ImGui::SetCursorScreenPos(ImVec2(sp.x + stripW - 76.0f, sp.y + (stripH - ChipButton::kHeight) * 0.5f));
         if (ChipButton::Draw(L("Cancel##cancelmidilearn"), false, ChipButton::kHeight, 68.0f))
            gPerfMidiLearnIdx = -1;
         ImGui::SetCursorScreenPos(ImVec2(sp.x, sp.y));
         ImGui::Dummy(ImVec2(stripW, stripH + tok::space_2));
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
      if (ChipButton::Draw(gPerfEditMode ? L("Edit Mode") : L("Perform"), gPerfEditMode, kPerfChipH, 86.0f))
         gPerfEditMode = !gPerfEditMode;

      // Add Control button in Edit Mode
      if (gPerfEditMode)
      {
         ImGui::SameLine();
         if (ChipButton::Draw(L("+ Add Control"), false, kPerfChipH, 100.0f))
            ImGui::OpenPopup("##perfaddcontrolmenu");

         if (MenuParts::BeginPopup("##perfaddcontrolmenu"))
         {
            if (MenuParts::Item(L("Knob (1x1)"))) AddPerfElementToCurrentPage(0);
            if (MenuParts::Item(L("Vertical Fader (1x2)"))) AddPerfElementToCurrentPage(1);
            if (MenuParts::Item(L("Horizontal Slider (2x1)"))) AddPerfElementToCurrentPage(2);
            if (MenuParts::Item(L("Toggle (1x1)"))) AddPerfElementToCurrentPage(3);
            if (MenuParts::Item(L("XY Pad (2x2)"))) AddPerfElementToCurrentPage(4);
            MenuParts::Separator();
            if (MenuParts::Item(L("Momentary Trigger / Bang (1x1)"))) AddPerfElementToCurrentPage(5);
            if (MenuParts::Item(L("Digital Number Box (1x1)"))) AddPerfElementToCurrentPage(6);
            if (MenuParts::Item(L("Radio Selector (2x1)"))) AddPerfElementToCurrentPage(7);
            if (MenuParts::Item(L("Bipolar Pan Knob (1x1)"))) AddPerfElementToCurrentPage(8);
            if (MenuParts::Item(L("Step Gate Ribbon (3x1)"))) AddPerfElementToCurrentPage(9);
            MenuParts::EndPopup();
         }
      }

      // Page Tabs (a wider gap separates them from the mode controls)
      ImGui::SameLine(0.0f, tok::space_4);
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
            if (FieldWell::InputText("##renamingpagetab", gPerfRenamePageBuffer, sizeof(gPerfRenamePageBuffer),
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
            if (ChipButton::Draw(pageTitle.c_str(), isSelected, kPerfChipH, 60.0f, /*soft=*/true))
               gPerfActivePage = p;

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
               ImGui::Text(T("Move %s"), pageTitle.c_str());
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
            if (MenuParts::BeginContextItem("##pagetabcontext"))
            {
               if (MenuParts::Item(L("Rename Page")))
               {
                  gPerfRenamingPage = p;
                  snprintf(gPerfRenamePageBuffer, sizeof(gPerfRenamePageBuffer), "%s", pageTitle.c_str());
               }
               if (MenuParts::Item(L("Duplicate Page")))
               {
                  PushUndoCheckpoint();
                  int newP = gPerfLayout.pageCount++;
                  gPerfLayout.pageNames.push_back(pageTitle + " Copy");
                  for (const auto& el : gPerfElements)
                  {
                     if (el.page == p)
                     {
                        Patch::PerfRecord copyEl = el;
                        copyEl.page = newP;
                        gPerfElements.push_back(copyEl);
                     }
                  }
                  gPerfActivePage = newP;
               }
               if (gPerfLayout.pageCount > 1 && MenuParts::Item(L("Delete Page")))
               {
                  PushUndoCheckpoint();
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
               MenuParts::EndPopup();
            }
         }
         ImGui::PopID();
      }

      // Add new page '+' button
      if (gPerfLayout.pageCount < 12)
      {
         ImGui::SameLine();
         if (ChipButton::Draw(L("+##addpagebtn"), false, kPerfChipH, kPerfChipH))
         {
            PushUndoCheckpoint();
            int newP = gPerfLayout.pageCount++;
            gPerfLayout.pageNames.push_back("Page " + std::to_string(newP + 1));
            gPerfActivePage = newP;
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Add New Page"));
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
         ImU32 gridCol = isLight ? tok::U32(tok::pal::c_D7DCE650) : tok::U32(tok::pal::c_30344150);
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
      {
         bool pageHasElements = false;
         for (const auto& el : gPerfElements)
            pageHasElements = pageHasElements || el.page == gPerfActivePage;
         if (!pageHasElements)
            EmptyState::DrawInWindow(T("Nothing on this page"),
                                     gPerfEditMode ? T("Use + Add Control to place a knob, pad or switch")
                                                   : T("Switch to Edit Mode to add controls"));
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
      if (!mouseOverAnyElement && MenuParts::BeginContextWindow("##perfcanvascontext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
      {
         if (MenuParts::SubMenu(L("Dock Position")))
         {
            if (MenuParts::Item(L("Bottom"), nullptr, gPerfPanelDock == 0)) gPerfPanelDock = 0;
            if (MenuParts::Item(L("Right"), nullptr, gPerfPanelDock == 1)) gPerfPanelDock = 1;
            if (MenuParts::Item(L("Left"), nullptr, gPerfPanelDock == 2)) gPerfPanelDock = 2;
            if (MenuParts::Item(L("Top"), nullptr, gPerfPanelDock == 3)) gPerfPanelDock = 3;
            ImGui::EndMenu();
         }
         MenuParts::Separator();
         if (MenuParts::SubMenu(L("+ Add Control")))
         {
            if (MenuParts::Item(L("Knob (1x1)"))) AddPerfElementToCurrentPage(0);
            if (MenuParts::Item(L("Vertical Fader (1x2)"))) AddPerfElementToCurrentPage(1);
            if (MenuParts::Item(L("Horizontal Slider (2x1)"))) AddPerfElementToCurrentPage(2);
            if (MenuParts::Item(L("Toggle (1x1)"))) AddPerfElementToCurrentPage(3);
            if (MenuParts::Item(L("XY Pad (2x2)"))) AddPerfElementToCurrentPage(4);
            MenuParts::Separator();
            if (MenuParts::Item(L("Momentary Trigger / Bang (1x1)"))) AddPerfElementToCurrentPage(5);
            if (MenuParts::Item(L("Digital Number Box (1x1)"))) AddPerfElementToCurrentPage(6);
            if (MenuParts::Item(L("Radio Selector (2x1)"))) AddPerfElementToCurrentPage(7);
            if (MenuParts::Item(L("Bipolar Pan Knob (1x1)"))) AddPerfElementToCurrentPage(8);
            if (MenuParts::Item(L("Step Gate Ribbon (3x1)"))) AddPerfElementToCurrentPage(9);
            ImGui::EndMenu();
         }
         if (MenuParts::Item(L("Clear Page Controls")))
         {
            PushUndoCheckpoint();
            gPerfElements.erase(std::remove_if(gPerfElements.begin(), gPerfElements.end(),
                                               [](const Patch::PerfRecord& r) { return r.page == gPerfActivePage; }),
                                gPerfElements.end());
            gPerfSelection.clear();
         }
         MenuParts::Separator();
         if (MenuParts::Item(L("Close Performance Matrix")))
         {
            gPerfPanelOpen = false;
         }
         MenuParts::EndPopup();
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
      const float kGrip = PanelFrame::kGap;   // the grip strip is the gap on the canvas-facing side
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
      // Outer child is transparent: the panel floats as a card on the canvas colour (PanelFrame).
      ImGui::BeginChild(id, size, false);
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
      const ImVec2 cardSize = vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), inner.y)
                                       : ImVec2(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
      PanelFrame::Insets in;
      switch (dock)   // the grip side needs no inset of its own
      {
         case 0: in.t = 0.0f; break;
         case 1: in.l = 0.0f; break;
         case 2: in.r = 0.0f; break;
         default: in.b = 0.0f; break;
      }
      PushDockedPanelStyle(/*isChild=*/true);
      PanelFrame::BeginCard("##perfpanelinnercontent", cardSize, in);
      PopDockedPanelStyle();
      DrawPerfPanelContent();
      PanelFrame::EndCard();

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
      // Set directly, not pushed: a push made in the child and popped in the parent unbalances both stacks.
      const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
      ImGui::EndChild();
      ImGui::GetStyle().ItemSpacing = savedSpacing;

      // No divider line along the canvas-facing edge, in either theme. This
      // hairline was a fixed dark constant, then a theme-derived one, and was
      // reported as a wrong-coloured seam both times - like the dialog border
      // above, it straddles two different backgrounds (panel fill on one side,
      // canvas on the other), so no single colour is right against both and
      // every theme change re-breaks it. The panel already reads as separate
      // because its opaque panelBg fill differs from the canvas' windowBg;
      // the line added no information. Removed rather than re-tuned.
   }


   void DrawModulatorMeter(IModulator* mod, int nodeIndex)
   {
      const bool isLight = IsThemeLight();
      const float value = mod->Value01();
      std::vector<float>& history = gModHistory[nodeIndex];
      history.push_back(value);
      if (history.size() > 160)
         history.erase(history.begin());

      // Fixed 0..1 axis: the box's bottom is 0 and its top is 1, always, so
      // a flat 0.5 sits mid-box and a line touching the bottom means the
      // value is really 0. (This used to autoscale to the visible min/max,
      // which drew any constant - 0.5 included - flat on the floor.) Only when
      // a modulator leaves 0..1 (Invert / Range-to-Range unclamped) does the
      // axis widen, and then the 0 and 1 hairlines below say where they fall.
      float lo = 0.0f, hi = 1.0f;
      for (float v : history) { lo = std::min(lo, v); hi = std::max(hi, v); }
      const float range = hi - lo;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 90.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      // Was a hardcoded near-black fill regardless of theme - on a light
      // preset (e.g. GitHub Light) this read as a solid black box in every
      // modulator node's (LFO, Pattern, ...) header. Reuse the same
      // theme-aware scope palette DrawCurveEditor already uses.
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                        ScopeBgCol(), tok::radius_field);

      const bool isPredictor = dynamic_cast<IPredictor*>(mod) != nullptr ||
                               dynamic_cast<PredictiveModulatorNode*>(mod) != nullptr;
      const ImU32 lineCol = isPredictor ? tok::U32(tok::pal::c_22C55EFF)
                                        : (isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_FFBE5AFF));

      dl->PushClipRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h), true); // backstop, not the primary fix
      for (size_t i = 1; i < history.size(); i++)
      {
         float x0 = origin.x + kPreviewSize * (float)(i - 1) / 160.0f;
         float x1 = origin.x + kPreviewSize * (float)i / 160.0f;
         float y0 = origin.y + h - (history[i - 1] - lo) / range * h;
         float y1 = origin.y + h - (history[i] - lo) / range * h;
         dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), lineCol, 1.6f);
      }
      dl->PopClipRect();

      // Value01() is contractually 0..1, but InvertNode and RangeToRangeNode
      // (clampOutput=false) deliberately go outside it so a kAbsolute binding
      // can extrapolate past the destination's nominal range instead of
      // being flattened at the edge - see 00-modulation-polarity.md. The
      // autoscale above makes that look identical to a well-behaved source,
      // so flag it: an amber border, plus hairlines marking where 0 and 1
      // actually fall on the autoscaled box.
      const bool outOfContract = lo < -1e-4f || hi > 1.0f + 1e-4f;
      {
         // faint quarter guides so the axis reads as a scale, not a blank box
         for (int q = 1; q < 4; q++)
         {
            const float gy = origin.y + h - ((q * 0.25f) - lo) / range * h;
            dl->AddLine(ImVec2(origin.x, gy), ImVec2(origin.x + kPreviewSize, gy),
                        ScopeMidLineCol(), q == 2 ? 1.0f : 0.5f);
         }
         if (!history.empty())
         {
            const float cy = origin.y + h - (history.back() - lo) / range * h;
            dl->AddCircleFilled(ImVec2(origin.x + kPreviewSize * (float)(history.size() - 1) / 160.0f, cy),
                                3.0f, lineCol);
         }
      }
      if (outOfContract)
      {
         const float y0line = origin.y + h - (0.0f - lo) / range * h;
         const float y1line = origin.y + h - (1.0f - lo) / range * h;
         const ImU32 hairlineCol = ScopeMidLineCol();
         if (y0line >= origin.y && y0line <= origin.y + h)
            dl->AddLine(ImVec2(origin.x, y0line), ImVec2(origin.x + kPreviewSize, y0line), hairlineCol, 1.0f);
         if (y1line >= origin.y && y1line <= origin.y + h)
            dl->AddLine(ImVec2(origin.x, y1line), ImVec2(origin.x + kPreviewSize, y1line), hairlineCol, 1.0f);
      }
      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                  outOfContract ? lineCol : ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(kPreviewSize, h));
      ImGui::Text("%.3f", value);
   }
}
