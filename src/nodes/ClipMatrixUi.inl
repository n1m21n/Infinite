// ===========================================================================
// Clip Matrix UI + recording to the Arrangement (Infinite-Turbo 0.44)
// Included once inside main.cpp's anonymous namespace, after ArrangeUi.inl.
// ===========================================================================

ClipMatrixNode* gClipMatrixMenuNode = nullptr;
bool gClipMatrixMenuOpen = false;

ImU32 ClipMatrixKindColor(const ClipMatrixNode::Cell& c)
{
   if (c.color > 0 && c.color < 10)
      return kArrangePalette[c.color].col;
   switch (c.kind)
   {
      case ClipMatrixNode::kAudio: return IM_COL32(70, 180, 120, 255);
      case ClipMatrixNode::kVideo: return IM_COL32(80, 140, 230, 255);
      case ClipMatrixNode::kImage: return IM_COL32(170, 110, 220, 255);
      default: return IM_COL32(60, 64, 78, 255);
   }
}

const char* ClipMatrixModeName(int m)
{
   static const char* k[3] = { "loop", "once", "gate" };
   return k[std::clamp(m, 0, 2)];
}

// One cell: a pad with its own CV pin (gate: high = held), coloured by its
// content, lit while playing, blinking while waiting for the grid.
void DrawClipMatrixCell(ClipMatrixNode* n, int r, int c, ImVec2 size)
{
   ClipMatrixNode::Cell& cell = n->CellAt(r, c);
   char label[64];
   snprintf(label, sizeof(label), "%s##cm_%d_%d", cell.kind != ClipMatrixNode::kEmpty ? cell.name.c_str() : "", r, c);
   DiscreteParamRef ref = BeginDiscreteParam(label, 0.0f, 0.0f, 1.0f);
   const bool cvHigh = ref.valid && ref.modulated && *ref.value >= 0.5f;
   const ImVec2 tl = ImGui::GetCursorScreenPos();
   const float w = std::max(8.0f, size.x - (ref.valid ? 18.0f : 0.0f));
   ImGui::InvisibleButton("##cell", ImVec2(w, size.y));
   const bool mouseHeld = ImGui::IsItemActive();
   const bool hovered = ImGui::IsItemHovered();
   const ImVec2 br(tl.x + w, tl.y + size.y);

   // Drag a clip onto another cell: move (Ctrl: copy).
   if (cell.kind != ClipMatrixNode::kEmpty && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
   {
      const int payload[2] = { r, c };
      ImGui::SetDragDropPayload("CM_CELL", payload, sizeof(payload));
      ImGui::Text("%s  (Ctrl: copy)", cell.name.c_str());
      ImGui::EndDragDropSource();
   }
   if (ImGui::BeginDragDropTarget())
   {
      if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("CM_CELL"))
      {
         const int* src = (const int*)pl->Data;
         PushUndoCheckpoint();
         if (ImGui::GetIO().KeyCtrl)
            n->CopyCell(src[0], src[1], r, c);
         else
            n->SwapCells(src[0], src[1], r, c);
      }
      ImGui::EndDragDropTarget();
   }

   const bool held = mouseHeld || cvHigh;
   if (held != n->uiHeld[r][c])
   {
      n->uiHeld[r][c] = held;
      if (held)
      {
         n->selRow = r;
         n->selCol = c;
         n->Launch(r, c); // an empty cell stops the row, like Live
      }
      else
         n->Release(r, c);
   }
   if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
   {
      n->selRow = r;
      n->selCol = c;
      gClipMatrixMenuNode = n; // opened outside the node editor (see DrawClipMatrixDeferredPopups)
      gClipMatrixMenuOpen = true;
   }

   ImDrawList* dl = ImGui::GetWindowDrawList();
   const bool playing = n->PlayingCol(r) == c;
   const bool queued = n->QueuedCol(r) == c;
   const bool selected = n->selRow == r && n->selCol == c;
   const ImU32 base = ClipMatrixKindColor(cell);
   const bool blink = std::fmod(ImGui::GetTime(), 0.4) < 0.2;
   ImU32 fill = cell.kind == ClipMatrixNode::kEmpty ? IM_COL32(34, 37, 47, 255) : ArrangeScaleCol(base, 0.45f);
   if (playing)
      fill = ArrangeScaleCol(base, 1.0f);
   else if (queued && blink)
      fill = ArrangeScaleCol(base, 0.8f);
   dl->AddRectFilled(tl, br, fill, 4.0f);
   if (playing)
   {
      const float p = n->Progress(r);
      dl->AddRectFilled(ImVec2(tl.x + 2.0f, br.y - 4.0f), ImVec2(tl.x + 2.0f + (w - 4.0f) * p, br.y - 2.0f),
                        IM_COL32(255, 255, 255, 220), 1.0f);
   }
   dl->AddRect(tl, br, selected ? IM_COL32(255, 255, 255, 230) : IM_COL32(15, 17, 22, 255), 4.0f, 0,
               selected ? 2.0f : 1.0f);
   if (cell.kind != ClipMatrixNode::kEmpty)
   {
      // Kind / mode marks and the name.
      const char* kindTag = cell.kind == ClipMatrixNode::kAudio ? "A" : (cell.kind == ClipMatrixNode::kVideo ? "V" : "I");
      const ImU32 tc = playing ? IM_COL32(15, 15, 20, 255) : IM_COL32(230, 233, 242, 255);
      dl->PushClipRect(tl, br, true);
      dl->AddText(ImVec2(tl.x + 4.0f, tl.y + 2.0f), tc, cell.name.c_str());
      char sub[48];
      snprintf(sub, sizeof(sub), "%s %s%s%s", kindTag, ClipMatrixModeName(cell.mode), cell.sync ? " sync" : "",
               cell.followOn ? " >" : "");
      dl->AddText(ImVec2(tl.x + 4.0f, tl.y + size.y * 0.45f), ArrangeScaleCol(tc, playing ? 1.0f : 0.75f), sub);
      dl->PopClipRect();
   }
   else if (!cell.path.empty())
      dl->AddText(ImVec2(tl.x + 4.0f, tl.y + 2.0f), IM_COL32(230, 110, 100, 255), "missing");
   else if (hovered)
      dl->AddText(ImVec2(tl.x + 4.0f, tl.y + 2.0f), IM_COL32(110, 115, 135, 255), "stop");
   if (ref.valid)
      EndDiscreteParam(ref, hovered);
}

void ClipMatrixLoadDialog(ClipMatrixNode* n, int r, int c)
{
   StartNodeFileDialog(n, []() { return Platform::OpenMediaDialog(); },
                       [r, c](ClipMatrixNode* m, const std::string& path)
                       {
                          PushUndoCheckpoint();
                          m->LoadCell(r, c, path);
                       });
}

void DrawClipMatrixCellMenu(ClipMatrixNode* n)
{
   if (!ImGui::BeginPopup("##cmcellmenu"))
      return;
   bool alive = false;
   for (GraphNode& g : gNodes)
      alive = alive || g.node.get() == n;
   if (!alive)
   {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return;
   }
   const int r = n->selRow, c = n->selCol;
   ClipMatrixNode::Cell& cell = n->CellAt(r, c);
   ImGui::TextDisabled("row %d, scene %d%s%s", r + 1, c + 1, cell.kind != ClipMatrixNode::kEmpty ? " - " : "",
                       cell.name.c_str());
   ImGui::Separator();
   if (ImGui::MenuItem(cell.kind == ClipMatrixNode::kEmpty ? "Load clip..." : "Replace clip..."))
      ClipMatrixLoadDialog(n, r, c);
   if (cell.kind != ClipMatrixNode::kEmpty)
   {
      for (int m = 0; m < 3; m++)
         if (ImGui::MenuItem(ClipMatrixModeName(m), nullptr, cell.mode == m))
         {
            PushUndoCheckpoint();
            cell.mode = m;
         }
      if (ImGui::BeginMenu("Quantize"))
      {
         if (ImGui::MenuItem("global", nullptr, cell.quant < 0))
         {
            PushUndoCheckpoint();
            cell.quant = -1;
         }
         for (int q = 0; q < ClipMatrixNode::kQuantCount; q++)
            if (ImGui::MenuItem(ClipMatrixNode::QuantName(q), nullptr, cell.quant == q))
            {
               PushUndoCheckpoint();
               cell.quant = q;
            }
         ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Color"))
      {
         for (int i = 0; i < 10; i++)
            if (ImGui::MenuItem(i == 0 ? "by kind" : kArrangePalette[i].name, nullptr, cell.color == i))
            {
               PushUndoCheckpoint();
               cell.color = i;
            }
         ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Launch"))
         n->Launch(r, c);
      ImGui::Separator();
      if (ImGui::MenuItem("Clear"))
      {
         PushUndoCheckpoint();
         n->ClearCell(r, c);
      }
   }
   ImGui::EndPopup();
}

// The selected cell / its scene / its row, under the grid.
void DrawClipMatrixEditor(ClipMatrixNode* n, float W)
{
   const int r = std::clamp(n->selRow, 0, n->rows - 1);
   const int c = std::clamp(n->selCol, 0, n->cols - 1);
   ClipMatrixNode::Cell& cell = n->CellAt(r, c);
   auto check = [](bool changed) { if (changed) PushUndoCheckpoint(); };
   const float fw = std::min(220.0f, W * 0.5f);

   {
      // -- clip --
      ImGui::BeginGroup();
      ImGui::PushID("cmclip");
      ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "clip  r%d s%d", r + 1, c + 1);
      if (cell.kind == ClipMatrixNode::kEmpty)
      {
         ImGui::TextDisabled(cell.path.empty() ? "empty - drop a file or load" : "file missing");
         if (!cell.status.empty())
            ImGui::TextDisabled("%s", cell.status.c_str());
         if (ImGui::Button("Load...", ImVec2(fw * 0.6f, 0)))
            ClipMatrixLoadDialog(n, r, c);
      }
      else
      {
         char nameBuf[128];
         snprintf(nameBuf, sizeof(nameBuf), "%s", cell.name.c_str());
         ImGui::SetNextItemWidth(fw);
         if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
         {
            PushUndoCheckpoint();
            cell.name = nameBuf;
         }
         ImGui::TextDisabled("%.2f s%s", cell.duration, cell.hasAudio ? "" : (cell.kind == ClipMatrixNode::kAudio ? "" : ", no sound"));
         for (int m = 0; m < 3; m++)
         {
            if (m)
               ImGui::SameLine();
            check(ImGui::RadioButton(ClipMatrixModeName(m), &cell.mode, m));
         }
         {
            std::vector<std::string> q = { "global" };
            for (int i = 0; i < ClipMatrixNode::kQuantCount; i++)
               q.push_back(ClipMatrixNode::QuantName(i));
            ClipMatrixNode::Cell* cp = &cell;
            DropdownButton("quantize##cmcq", q, cell.quant + 1,
                           [cp](int i) { PushUndoCheckpoint(); cp->quant = i - 1; }, fw * 0.7f, false);
         }
         ImGui::SetNextItemWidth(fw * 0.7f);
         ImGui::SliderFloat("gain", &cell.gainDb, -60.0f, 12.0f, "%.1f dB");
         if (ImGui::IsItemDeactivatedAfterEdit())
            PushUndoCheckpoint();
         if (cell.kind != ClipMatrixNode::kImage)
         {
            check(ImGui::Checkbox("sync to tempo", &cell.sync));
            ImGui::SetNextItemWidth(fw * 0.5f);
            ImGui::DragFloat("clip bpm", &cell.sampleBpm, 0.1f, 20.0f, 999.0f, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
               PushUndoCheckpoint();
            ImGui::SameLine();
            if (ImGui::SmallButton("/2")) { PushUndoCheckpoint(); cell.sampleBpm = std::max(20.0f, cell.sampleBpm * 0.5f); }
            ImGui::SameLine();
            if (ImGui::SmallButton("x2")) { PushUndoCheckpoint(); cell.sampleBpm = std::min(999.0f, cell.sampleBpm * 2.0f); }
            if (cell.origBpm > 0.0f)
               ImGui::TextDisabled("detected %.1f bpm", cell.origBpm);
         }
         if (cell.kind == ClipMatrixNode::kAudio)
         {
            ImGui::SetNextItemWidth(fw * 0.7f);
            ImGui::SliderFloat("pitch", &cell.pitch, -24.0f, 24.0f, "%+.1f st");
            if (ImGui::IsItemDeactivatedAfterEdit())
               PushUndoCheckpoint();
         }
         // Follow action.
         check(ImGui::Checkbox("follow action", &cell.followOn));
         if (cell.followOn)
         {
            auto followCombo = [&](const char* label, int& v)
            {
               std::vector<std::string> names;
               for (int i = 1; i < ClipMatrixNode::kFollowCount; i++)
                  names.push_back(ClipMatrixNode::FollowName(i));
               int* pv = &v;
               DropdownButton(label, names, std::max(0, v - 1), [pv](int i) { PushUndoCheckpoint(); *pv = i + 1; },
                              fw * 0.45f, false);
            };
            ImGui::SetNextItemWidth(fw * 0.45f);
            ImGui::DragFloat("after bars", &cell.followBars, 0.05f, 0.0f, 64.0f, cell.followBars <= 0.0f ? "clip length" : "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
               PushUndoCheckpoint();
            followCombo("action A##cmfa", cell.followA);
            followCombo("action B##cmfb", cell.followB);
            float pct = cell.followChanceB * 100.0f;
            ImGui::SetNextItemWidth(fw * 0.45f);
            if (ImGui::SliderFloat("chance B", &pct, 0.0f, 100.0f, "%.0f%%"))
               cell.followChanceB = pct / 100.0f;
            if (ImGui::IsItemDeactivatedAfterEdit())
               PushUndoCheckpoint();
         }
      }
      ImGui::PopID();
      ImGui::EndGroup();

      // -- scene --
      ImGui::SameLine(0.0f, 24.0f);
      ImGui::BeginGroup();
      ImGui::PushID("cmscene");
      ClipMatrixNode::Scene& sc = n->SceneAt(c);
      ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "scene %d", c + 1);
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%s", sc.name.c_str());
         ImGui::SetNextItemWidth(fw * 0.8f);
         if (ImGui::InputTextWithHint("##sname", "name", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
         {
            PushUndoCheckpoint();
            sc.name = buf;
         }
      }
      bool hasTempo = sc.tempo > 0.0f;
      if (ImGui::Checkbox("set tempo", &hasTempo))
      {
         PushUndoCheckpoint();
         sc.tempo = hasTempo ? std::max(1.0f, Transport::Instance().Tempo()) : 0.0f;
      }
      if (hasTempo)
      {
         ImGui::SetNextItemWidth(fw * 0.5f);
         ImGui::DragFloat("bpm##scene", &sc.tempo, 0.1f, 20.0f, 400.0f, "%.1f");
         if (ImGui::IsItemDeactivatedAfterEdit())
            PushUndoCheckpoint();
      }
      bool hasSig = sc.sigNum > 0;
      if (ImGui::Checkbox("set time signature", &hasSig))
      {
         PushUndoCheckpoint();
         sc.sigNum = hasSig ? Transport::Instance().TimeSigNumerator() : 0;
         sc.sigDen = Transport::Instance().TimeSigDenominator();
      }
      if (hasSig)
      {
         int sig[2] = { sc.sigNum, sc.sigDen };
         ImGui::SetNextItemWidth(fw * 0.5f);
         if (ImGui::InputInt2("##sig", sig, ImGuiInputTextFlags_EnterReturnsTrue))
         {
            PushUndoCheckpoint();
            sc.sigNum = std::clamp(sig[0], 1, 16);
            sc.sigDen = (sig[1] == 2 || sig[1] == 8 || sig[1] == 16) ? sig[1] : 4;
         }
      }
      ImGui::PopID();
      ImGui::EndGroup();

      // -- row --
      ImGui::SameLine(0.0f, 24.0f);
      ImGui::BeginGroup();
      ImGui::PushID("cmrow");
      ClipMatrixNode::Row& rp = n->RowAt(r);
      ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "row %d", r + 1);
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%s", rp.name.c_str());
         ImGui::SetNextItemWidth(fw * 0.8f);
         if (ImGui::InputTextWithHint("##rname", "name", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
         {
            PushUndoCheckpoint();
            rp.name = buf;
         }
      }
      ImGui::SetNextItemWidth(fw * 0.7f);
      ImGui::SliderFloat("gain##row", &rp.gainDb, -60.0f, 12.0f, "%.1f dB");
      if (ImGui::IsItemDeactivatedAfterEdit())
         PushUndoCheckpoint();
      check(ImGui::Checkbox("mute", &rp.mute));
      ImGui::SetNextItemWidth(fw * 0.7f);
      ImGui::SliderFloat("opacity", &rp.opacity, 0.0f, 1.0f, "%.2f");
      if (ImGui::IsItemDeactivatedAfterEdit())
         PushUndoCheckpoint();
      {
         ClipMatrixNode::Row* prp = &rp;
         DropdownButton("blend##cmblend", BlendModes::Names(), rp.blendMode,
                        [prp](int i) { PushUndoCheckpoint(); prp->blendMode = i; }, fw * 0.7f, false);
      }
      ImGui::PopID();
      ImGui::EndGroup();
   }
}

// Popups cannot open inside the node editor's canvas transform; the cell
// menu is drawn here, in main.cpp's ed::Suspend() block.
void DrawClipMatrixDeferredPopups()
{
   if (gClipMatrixMenuOpen)
   {
      ImGui::OpenPopup("##cmcellmenu");
      gClipMatrixMenuOpen = false;
   }
   if (gClipMatrixMenuNode != nullptr)
      DrawClipMatrixCellMenu(gClipMatrixMenuNode);
}

void DrawClipMatrixParams(GraphNode& gn, ClipMatrixNode* n)
{
   (void)gn;
   const float cellW = 82.0f, cellH = 40.0f, gap = 4.0f, labelW = 92.0f;
   const float W = labelW + (float)n->cols * (cellW + gap);

   // Top bar.
   {
      {
         std::vector<std::string> q;
         for (int i = 0; i < ClipMatrixNode::kQuantCount; i++)
            q.push_back(ClipMatrixNode::QuantName(i));
         DropdownButton("quantize##cmquant", q, n->quantize, [n](int i) { PushUndoCheckpoint(); n->quantize = i; },
                        120.0f, true);
      }
      ImGui::SameLine();
      if (ModTriggerButton("STOP ALL##cmStopAll", ImVec2(98.0f, 0.0f)))
         n->StopAll();
      ImGui::SameLine();
      {
         const bool rec = n->recordToArrangement;
         if (rec)
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 60, 60, 255));
         // Turbo 0.49: mappable (CV / MIDI / Performance).
         bool requested = rec;
         if (ModStateButton("REC > ARR##cmRecArr", rec, requested, ImVec2(100.0f, 0.0f)))
         {
            PushUndoCheckpoint();
            n->recordToArrangement = requested;
         }
         if (rec)
            ImGui::PopStyleColor();
      }
      ImGui::SameLine();
      ImGui::TextDisabled("rows");
      ImGui::SameLine();
      if (ImGui::SmallButton("-##cmr") && n->rows > 1) { PushUndoCheckpoint(); n->rows--; }
      ImGui::SameLine();
      ImGui::Text("%d", n->rows);
      ImGui::SameLine();
      if (ImGui::SmallButton("+##cmr") && n->rows < ClipMatrixNode::kMaxRows) { PushUndoCheckpoint(); n->rows++; }
      ImGui::SameLine();
      ImGui::TextDisabled("scenes");
      ImGui::SameLine();
      if (ImGui::SmallButton("-##cmc") && n->cols > 1) { PushUndoCheckpoint(); n->cols--; }
      ImGui::SameLine();
      ImGui::Text("%d", n->cols);
      ImGui::SameLine();
      if (ImGui::SmallButton("+##cmc") && n->cols < ClipMatrixNode::kMaxCols) { PushUndoCheckpoint(); n->cols++; }
   }

   const ImVec2 origin = ImGui::GetCursorScreenPos();
   // Scene launch row.
   for (int c = 0; c < n->cols; c++)
   {
      ImGui::SetCursorScreenPos(ImVec2(origin.x + labelW + (float)c * (cellW + gap), origin.y));
      const ClipMatrixNode::Scene& sc = n->SceneAt(c);
      char label[64];
      if (!sc.name.empty())
         snprintf(label, sizeof(label), "> %s##cmScene%d", sc.name.c_str(), c);
      else if (sc.tempo > 0.0f)
         snprintf(label, sizeof(label), "> %d  %.0f##cmScene%d", c + 1, sc.tempo, c);
      else
         snprintf(label, sizeof(label), "> %d##cmScene%d", c + 1, c);
      ImGui::PushID(c);
      if (ModTriggerButton(label, ImVec2(cellW, 22.0f)))
      {
         n->selCol = c;
         n->LaunchScene(c);
      }
      ImGui::PopID();
   }
   const float rowsY = origin.y + 26.0f;
   for (int r = 0; r < n->rows; r++)
   {
      const float y = rowsY + (float)r * (cellH + gap);
      ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
      ImGui::PushID(1000 + r);
      // Row label + stop.
      {
         const ClipMatrixNode::Row& rp = n->RowAt(r);
         char label[64];
         snprintf(label, sizeof(label), "STOP##cmStop%d", r);
         const bool stopQueued = n->QueuedCol(r) == -2 && std::fmod(ImGui::GetTime(), 0.4) < 0.2;
         if (stopQueued)
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 120, 40, 255));
         if (ModTriggerButton(label, ImVec2(labelW - gap, 20.0f)))
            n->StopRow(r);
         if (stopQueued)
            ImGui::PopStyleColor();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const std::string nm = rp.name.empty() ? ("row " + std::to_string(r + 1)) : rp.name;
         dl->AddText(ImVec2(origin.x + 2.0f, y + 22.0f), rp.mute ? IM_COL32(200, 90, 80, 255) : IM_COL32(170, 175, 195, 255), nm.c_str());
         const float lvl = std::clamp(n->RowLevel(r), 0.0f, 1.0f);
         dl->AddRectFilled(ImVec2(origin.x + labelW - 10.0f, y + 22.0f), ImVec2(origin.x + labelW - 6.0f, y + cellH),
                           IM_COL32(22, 24, 30, 255));
         dl->AddRectFilled(ImVec2(origin.x + labelW - 10.0f, y + cellH - (cellH - 22.0f) * lvl),
                           ImVec2(origin.x + labelW - 6.0f, y + cellH), IM_COL32(80, 200, 120, 255));
      }
      for (int c = 0; c < n->cols; c++)
      {
         ImGui::SetCursorScreenPos(ImVec2(origin.x + labelW + (float)c * (cellW + gap), y));
         ImGui::PushID(c);
         DrawClipMatrixCell(n, r, c, ImVec2(cellW, cellH));
         // Remember where the cell is (canvas space) for file drops.
         {
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            n->cellRect[r][c][0] = a.x;
            n->cellRect[r][c][1] = a.y;
            n->cellRect[r][c][2] = b.x;
            n->cellRect[r][c][3] = b.y;
         }
         ImGui::PopID();
      }
      ImGui::PopID();
   }
   ImGui::SetCursorScreenPos(ImVec2(origin.x, rowsY + (float)n->rows * (cellH + gap)));
   ImGui::Dummy(ImVec2(W, 4.0f));
   ImGui::TextDisabled("notes %d-%d launch row by row  -  drop files on cells  -  right-click a cell for options",
                       n->baseNote, n->baseNote + n->rows * n->cols - 1);
   DrawClipMatrixEditor(n, W);
   if (ImGui::TreeNode("output##cm"))
   {
      int wh[2] = { n->width, n->height };
      ImGui::SetNextItemWidth(160.0f);
      if (ImGui::InputInt2("video size", wh, ImGuiInputTextFlags_EnterReturnsTrue))
      {
         PushUndoCheckpoint();
         n->width = std::clamp(wh[0], 16, 8192);
         n->height = std::clamp(wh[1], 16, 8192);
      }
      ImGui::SetNextItemWidth(160.0f);
      ImGui::SliderFloat("volume", &n->volume, 0.0f, 1.5f, "%.2f");
      if (ImGui::IsItemDeactivatedAfterEdit())
         PushUndoCheckpoint();
      ImGui::SetNextItemWidth(160.0f);
      if (ImGui::InputInt("base note", &n->baseNote))
         n->baseNote = std::clamp(n->baseNote, 0, 127);
      ImGui::TreePop();
   }
}

void DrawClipMatrixOutParams(ClipMatrixOutNode* n)
{
   if (ClipMatrixNode* m = n->Matrix())
      ImGui::TextDisabled("%d rows: video + audio out per row", m->rows);
   else
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "wire a Clip Matrix's audio output here");
   ImGui::Dummy(ImVec2(160.0f, 2.0f));
}

// A file dropped on the canvas over a Clip Matrix cell loads into it.
bool ClipMatrixHandleDrop(const std::string& path, ImVec2 canvasPos)
{
   for (GraphNode& gn : gNodes)
   {
      auto* m = dynamic_cast<ClipMatrixNode*>(gn.node.get());
      if (m == nullptr)
         continue;
      for (int r = 0; r < m->rows; r++)
         for (int c = 0; c < m->cols; c++)
            if (canvasPos.x >= m->cellRect[r][c][0] && canvasPos.x <= m->cellRect[r][c][2] &&
                canvasPos.y >= m->cellRect[r][c][1] && canvasPos.y <= m->cellRect[r][c][3])
            {
               PushUndoCheckpoint();
               m->LoadCell(r, c, path);
               m->selRow = r;
               m->selCol = c;
               return true;
            }
   }
   return false;
}

// ---- recording to the Arrangement Timeline ------------------------------------

struct ClipMatrixRecRow
{
   bool open = false;
   int col = -1;
   double startBeat = 0.0;
   double srcSec = 0.0;
};
struct ClipMatrixRec
{
   bool active = false;
   ClipMatrixRecRow rows[ClipMatrixNode::kMaxRows];
   uint64_t audioLane[ClipMatrixNode::kMaxRows] = {};
   uint64_t videoLane[ClipMatrixNode::kMaxRows] = {};
};
std::map<uint64_t, ClipMatrixRec> gClipMatrixRec; // by node uid
std::map<std::string, uint64_t> gClipMatrixSources; // file path -> source node uid

uint64_t ClipMatrixSourceFor(const ClipMatrixNode::Cell& cell, const GraphNode& matrixGn)
{
   auto it = gClipMatrixSources.find(cell.path);
   if (it != gClipMatrixSources.end() && FindNodeByUid(it->second) != nullptr)
      return it->second;
   const char* type = cell.kind == ClipMatrixNode::kAudio ? "Audio File"
                    : cell.kind == ClipMatrixNode::kVideo ? "Video" : "Image Source";
   const std::string category = NodeFactory::Instance().CategoryOf(type);
   const float x = matrixGn.liveX - 260.0f;
   const float y = matrixGn.liveY + 60.0f * (float)gClipMatrixSources.size();
   GraphNode* gn = SpawnNode(type, category.empty() ? "Source" : category, x, y);
   if (gn == nullptr)
      return 0;
   if (auto* af = dynamic_cast<AudioFileNode*>(gn->node.get()))
   {
      af->loop = false;
      af->Open(cell.path);
   }
   else if (auto* vid = dynamic_cast<VideoSourceNode*>(gn->node.get()))
   {
      vid->loop = true;
      vid->Open(cell.path);
   }
   else if (auto* img = dynamic_cast<ImageSourceNode*>(gn->node.get()))
      img->Load(cell.path);
   gClipMatrixSources[cell.path] = gn->uid;
   return gn->uid;
}

uint64_t ClipMatrixLane(ClipMatrixRec& rec, int row, int type, const ClipMatrixNode* m)
{
   uint64_t& id = type == Arrange::kLaneAudio ? rec.audioLane[row] : rec.videoLane[row];
   if (id != 0 && Arrange::FindLane(gArrange, id) != nullptr)
      return id;
   id = Arrange::AddLane(gArrange, type);
   if (Arrange::Lane* l = Arrange::FindLane(gArrange, id))
   {
      const ClipMatrixNode::Row& rp = const_cast<ClipMatrixNode*>(m)->RowAt(row);
      l->name = (rp.name.empty() ? ("Matrix " + std::to_string(row + 1)) : rp.name) +
                (type == Arrange::kLaneAudio ? " audio" : " video");
   }
   return id;
}

void ClipMatrixCloseClip(ClipMatrixNode* m, const GraphNode& gn, ClipMatrixRec& rec, int row, double endBeat)
{
   ClipMatrixRecRow& o = rec.rows[row];
   if (!o.open)
      return;
   o.open = false;
   if (endBeat - o.startBeat < 1.0 / 64.0 || o.col < 0)
      return;
   const ClipMatrixNode::Cell& cell = m->CellAt(row, o.col);
   if (cell.kind == ClipMatrixNode::kEmpty)
      return;
   const uint64_t src = ClipMatrixSourceFor(cell, gn);
   if (src == 0)
      return;
   const ClipMatrixNode::Row& rp = m->RowAt(row);
   Arrange::Clip c;
   c.start = std::max<Arrange::Tick>(0, Arrange::BeatsToTicks(o.startBeat));
   c.length = std::max<Arrange::Tick>(1, Arrange::BeatsToTicks(endBeat) - c.start);
   c.srcUid = src;
   c.name = cell.name;
   c.gainDb = cell.gainDb + rp.gainDb;
   c.sourceOffsetSeconds = (float)o.srcSec;
   if (cell.kind == ClipMatrixNode::kAudio)
   {
      c.sampleDropped = true;
      c.syncToTempo = cell.sync;
      c.sampleBpm = cell.sampleBpm;
      c.origBpm = cell.origBpm;
      c.pitch = cell.pitch;
      c.sourceDurationSeconds = (float)cell.duration;
      Arrange::PlaceOverwrite(gArrange, ClipMatrixLane(rec, row, Arrange::kLaneAudio, m), c);
   }
   else
   {
      c.sampleDropped = cell.kind == ClipMatrixNode::kVideo;
      c.retrigger = true;
      c.blendMode = rp.blendMode;
      c.opacity = rp.opacity;
      Arrange::PlaceOverwrite(gArrange, ClipMatrixLane(rec, row, Arrange::kLaneVideo, m), c);
      if (cell.kind == ClipMatrixNode::kVideo && cell.hasAudio)
      {
         Arrange::Clip a = c;
         a.id = 0;
         a.srcOutput = 1;
         a.sampleDropped = false;
         a.blendMode = 0;
         a.opacity = 1.0f;
         Arrange::PlaceOverwrite(gArrange, ClipMatrixLane(rec, row, Arrange::kLaneAudio, m), a);
      }
   }
   gArrangePanelOpen = true;
}

// Once per frame: drain every matrix's events; while REC > ARR is on and the
// transport runs, each played stretch becomes a timeline clip.
void ClipMatrixFrameUpdate()
{
   const bool playing = Transport::Instance().IsPlaying();
   const double now = Transport::Instance().Beats();
   for (GraphNode& gn : gNodes)
   {
      auto* m = dynamic_cast<ClipMatrixNode*>(gn.node.get());
      if (m == nullptr)
         continue;
      ClipMatrixRec& rec = gClipMatrixRec[gn.uid];
      const bool recording = m->recordToArrangement && playing && !ArrangeRenderBusy();
      if (recording && !rec.active)
      {
         rec.active = true;
         ArrangeGestureBegin(); // the whole take is one undo step
         // Clips already playing when recording starts begin here.
         for (int r = 0; r < m->rows; r++)
            if (m->PlayingCol(r) >= 0)
               rec.rows[r] = { true, m->PlayingCol(r), now, 0.0 };
      }
      ClipMatrixNode::Event e;
      while (m->PopEvent(e))
      {
         if (!rec.active || e.row < 0 || e.row >= ClipMatrixNode::kMaxRows)
            continue;
         if (e.type == ClipMatrixNode::Event::kStart)
         {
            ClipMatrixCloseClip(m, gn, rec, e.row, e.beat);
            rec.rows[e.row] = { true, e.col, e.beat, e.sourceSeconds };
         }
         else if (e.type == ClipMatrixNode::Event::kWrap)
         {
            ClipMatrixCloseClip(m, gn, rec, e.row, e.beat);
            rec.rows[e.row] = { true, e.col, e.beat, e.sourceSeconds };
         }
         else
            ClipMatrixCloseClip(m, gn, rec, e.row, e.beat);
      }
      if (rec.active && !recording)
      {
         for (int r = 0; r < ClipMatrixNode::kMaxRows; r++)
            ClipMatrixCloseClip(m, gn, rec, r, now);
         rec.active = false;
         ArrangeGestureEnd();
      }
   }
   for (auto it = gClipMatrixRec.begin(); it != gClipMatrixRec.end();)
      it = FindNodeByUid(it->first) == nullptr ? gClipMatrixRec.erase(it) : std::next(it);
}

struct ClipMatrixStaticInit
{
   ClipMatrixStaticInit() { ClipMatrixNode::sEstimateBpm = &ArrangeEstimateSampleBpm; }
};
ClipMatrixStaticInit gClipMatrixStaticInit;
