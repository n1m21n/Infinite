// Part of src/main.cpp, included there in order (one translation unit).
// note sources: keyboard, chords, Pattern Player, Note Looper, MIDI Out

   // ---- Note to CV ---------------------------------------------------
   void DrawNoteToCVBody(GraphNode& gn, NoteToCVNode* n)
   {
      char stat[64];
      const int last = n->LastNote();
      if (last < 0)
         snprintf(stat, sizeof(stat), "%d..%d -> 0..1", n->rangeLow, n->rangeHigh);
      else
         snprintf(stat, sizeof(stat), "last: %s%d -> %.2f", NoteNameList()[last % 12].c_str(), last / 12 - 1,
                  n->Value01());

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      AudioKnobRow row(3);
      row.KnobInt("range lo", &n->rangeLow, 0, 126);
      row.KnobInt("range hi", &n->rangeHigh, 1, 127);
      row.Knob("glide", &n->glideMs, 0.0f, 500.0f, "%.0f ms", kKnobLarge);
      row.End();

      EndAudioBody();
   }

   // ---- Keyboard (ported from upstream) -----------------------------------
   // Fixed lowNote on purpose: a widget that recentred under an active click
   // (like the MIDI Notes display) makes the pressed key slide away.
   // Turbo 0.51: marks[note] 1 = chord root, 2 = slash bass; octaveLabels
   // names every C ("C3", note/12-1 convention).
   int DrawInteractiveKeyboard(const bool held[128], int lowNote, int octaves,
                               const signed char* marks = nullptr, bool octaveLabels = false)
   {
      const float w = gAudioBodyW, h = 64.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
      static const int kBlackOffsets[5] = { 1, 3, 6, 8, 10 };
      static const float kBlackSlot[5] = { 0.0f, 1.0f, 3.0f, 4.0f, 5.0f };

      const int whiteCount = 7 * octaves;
      const float keyW = (w - 4.0f) / (float)whiteCount;

      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##keyboardHit", ImVec2(w, h));
      const bool active = ImGui::IsItemActive();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      int hitNote = -1;
      if (active && mouse.x >= origin.x && mouse.x < br.x && mouse.y >= origin.y && mouse.y < br.y)
      {
         // Black keys sit visually on top, so test them first.
         const float relY = mouse.y - origin.y;
         if (relY < h * 0.62f)
         {
            for (int o = 0; o < octaves && hitNote < 0; o++)
            {
               for (int k = 0; k < 5; k++)
               {
                  const int note = lowNote + o * 12 + kBlackOffsets[k];
                  const float x = origin.x + 2.0f + ((float)(o * 7) + kBlackSlot[k] + 1.0f) * keyW - keyW * 0.3f;
                  if (mouse.x >= x && mouse.x < x + keyW * 0.6f)
                  {
                     hitNote = note;
                     break;
                  }
               }
            }
         }
         if (hitNote < 0)
         {
            const int col = std::clamp((int)((mouse.x - origin.x - 2.0f) / keyW), 0, whiteCount - 1);
            hitNote = lowNote + (col / 7) * 12 + kWhiteOffsets[col % 7];
         }
      }

      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 7; k++)
         {
            const int note = lowNote + o * 12 + kWhiteOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + 2.0f + (float)(o * 7 + k) * keyW;
            const ImU32 whiteCol = on ? (isLight ? IM_COL32(50, 130, 245, 255) : IM_COL32(120, 200, 255, 245))
                                      : (isLight ? IM_COL32(250, 250, 255, 255) : IM_COL32(206, 210, 222, 255));
            dl->AddRectFilled(ImVec2(x + 0.5f, origin.y + 3.0f), ImVec2(x + keyW - 0.5f, br.y - 3.0f),
                              whiteCol, 2.0f);
         }
      }
      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 5; k++)
         {
            const int note = lowNote + o * 12 + kBlackOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + 2.0f + ((float)(o * 7) + kBlackSlot[k] + 1.0f) * keyW - keyW * 0.3f;
            const ImU32 blackCol = on ? (isLight ? IM_COL32(30, 100, 230, 255) : IM_COL32(90, 170, 235, 255))
                                      : (isLight ? IM_COL32(60, 65, 80, 255) : IM_COL32(26, 28, 36, 255));
            dl->AddRectFilled(ImVec2(x, origin.y + 3.0f), ImVec2(x + keyW * 0.6f, origin.y + h * 0.62f),
                              blackCol, 2.0f);
         }
      }

      if (octaveLabels)
      {
         const ImU32 labelCol = isLight ? IM_COL32(70, 76, 92, 255) : IM_COL32(70, 76, 92, 255);
         for (int o = 0; o < octaves; o++)
         {
            const int note = lowNote + o * 12;
            char lab[8];
            snprintf(lab, sizeof(lab), "C%d", note / 12 - 1);
            dl->AddText(ImVec2(origin.x + 3.0f + (float)(o * 7) * keyW, br.y - 17.0f), labelCol, lab);
         }
      }
      if (marks != nullptr)
      {
         for (int o = 0; o < octaves; o++)
            for (int semi = 0; semi < 12; semi++)
            {
               const int note = lowNote + o * 12 + semi;
               if (note < 0 || note > 127 || marks[note] == 0)
                  continue;
               float cx = 0.0f, cy = 0.0f;
               int slot = -1;
               for (int k = 0; k < 7; k++)
                  if (kWhiteOffsets[k] == semi)
                     slot = k;
               if (slot >= 0)
               {
                  cx = origin.x + 2.0f + (float)(o * 7 + slot) * keyW + keyW * 0.5f;
                  cy = origin.y + 46.0f;
               }
               else
               {
                  int b = 0;
                  for (int k = 0; k < 5; k++)
                     if (kBlackOffsets[k] == semi)
                        b = k;
                  cx = origin.x + 2.0f + ((float)(o * 7) + kBlackSlot[b] + 1.0f) * keyW;
                  cy = origin.y + h * 0.62f - 8.0f;
               }
               if (marks[note] == 1)
                  dl->AddCircleFilled(ImVec2(cx, cy), 3.2f, IM_COL32(240, 110, 60, 255));
               else
                  dl->AddCircle(ImVec2(cx, cy), 3.4f, IM_COL32(60, 200, 120, 255), 0, 2.0f);
            }
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      return hitNote;
   }

   struct TypingKey { ImGuiKey key; int semitone; };

   // Logic Pro / GarageBand's "Musical Typing" layout - the de facto standard
   // for playing a software instrument from a QWERTY keyboard. Two rows, each
   // shaped like one octave of a real piano (letter row = white keys, row
   // above = black keys sitting between the white keys they sharp), with the
   // upper row exactly one octave above the lower.
   static const TypingKey kTypingKeys[] = {
      { ImGuiKey_Z, 0 }, { ImGuiKey_S, 1 }, { ImGuiKey_X, 2 }, { ImGuiKey_D, 3 },
      { ImGuiKey_C, 4 }, { ImGuiKey_V, 5 }, { ImGuiKey_G, 6 }, { ImGuiKey_B, 7 },
      { ImGuiKey_H, 8 }, { ImGuiKey_N, 9 }, { ImGuiKey_J, 10 }, { ImGuiKey_M, 11 },
      { ImGuiKey_Comma, 12 }, { ImGuiKey_L, 13 }, { ImGuiKey_Period, 14 },
      { ImGuiKey_Semicolon, 15 }, // no Slash: "/" drops a comment on the canvas
      { ImGuiKey_Q, 12 }, { ImGuiKey_2, 13 }, { ImGuiKey_W, 14 }, { ImGuiKey_3, 15 },
      { ImGuiKey_E, 16 }, { ImGuiKey_R, 17 }, { ImGuiKey_5, 18 }, { ImGuiKey_T, 19 },
      { ImGuiKey_6, 20 }, { ImGuiKey_Y, 21 }, { ImGuiKey_7, 22 }, { ImGuiKey_U, 23 },
      { ImGuiKey_I, 24 }, { ImGuiKey_9, 25 }, { ImGuiKey_O, 26 }, { ImGuiKey_0, 27 },
      { ImGuiKey_P, 28 },
   };

   // ---- Chord Progression (Turbo) ------------------------------------------
   // Slot strip (click to select, the playing chord is lit with a progress
   // bar), a two-octave keyboard that toggles the selected chord's notes, the
   // selected chord's length in bars plus a root/quality builder, and the
   // progression-wide knobs. The per-chord length and the keys are plain
   // widgets, not modulation pins: a pin on "the selected chord" would change
   // meaning every time another slot is clicked.
   // Turbo 0.55: the chord slot menu (right-click on a chord) is opened and
   // drawn outside the node editor, in the ed::Suspend() block, like the Clip
   // Matrix cell menu. Opened inside the body it went through the canvas
   // transform and landed at the bottom-right corner of the screen. The body
   // only records the request; the picked item comes back as an op number
   // (the SlotOp values of DrawChordProgressionBody).
   ChordProgressionNode* gCpMenuNode = nullptr;
   int gCpMenuSlot = -1;
   int gCpMenuCount = 0;
   bool gCpMenuHasClip = false;
   bool gCpMenuOpen = false;
   ChordProgressionNode* gCpMenuResultNode = nullptr;
   int gCpMenuResultOp = 0;
   int gCpMenuResultSlot = -1;

   void DrawChordSlotDeferredMenu()
   {
      if (gCpMenuOpen)
      {
         ImGui::OpenPopup("##cpslotmenu");
         gCpMenuOpen = false;
      }
      if (gCpMenuNode == nullptr)
         return;
      if (!ImGui::BeginPopup("##cpslotmenu"))
      {
         gCpMenuNode = nullptr;
         return;
      }
      const int count = std::max(1, gCpMenuCount);
      const int ms = std::clamp(gCpMenuSlot, 0, count - 1);
      const bool full = count >= ChordProgressionNode::kMaxChords;
      int op = 0;
      if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !full)) op = 1;
      if (ImGui::MenuItem("Insert empty before", nullptr, false, !full)) op = 2;
      if (ImGui::MenuItem("Insert empty after", nullptr, false, !full)) op = 3;
      if (ImGui::MenuItem("Delete", "Del", false, count > 1)) op = 4;
      ImGui::Separator();
      if (ImGui::MenuItem("Move left", "Alt+Left", false, ms > 0)) op = 5;
      if (ImGui::MenuItem("Move right", "Alt+Right", false, ms < count - 1)) op = 6;
      ImGui::Separator();
      if (ImGui::MenuItem("Copy", "Ctrl+C")) op = 7;
      if (ImGui::MenuItem("Paste (replace)", "Ctrl+V", false, gCpMenuHasClip)) op = 8;
      if (ImGui::MenuItem("Paste (insert after)", "Ctrl+Shift+V", false, gCpMenuHasClip && !full)) op = 9;
      ImGui::Separator();
      if (ImGui::MenuItem("Clear")) op = 10;
      if (op != 0)
      {
         gCpMenuResultNode = gCpMenuNode;
         gCpMenuResultOp = op;
         gCpMenuResultSlot = ms;
         gCpMenuNode = nullptr;
      }
      ImGui::EndPopup();
   }

   void DrawChordProgressionBody(GraphNode& gn, ChordProgressionNode* n)
   {
      using CP = ChordProgressionNode;
      n->chordCount = std::clamp(n->chordCount, 1, CP::kMaxChords);
      int count = n->chordCount;
      n->selected = std::clamp(n->selected, 0, count - 1);
      int sel = n->selected;
      const int playing = n->PlayingIndex();

      const bool armed = n->RestartArmed();
      char stat[128];
      if (playing >= 0 && playing < count)
         snprintf(stat, sizeof(stat), "chord %d/%d  -  %s  -  %s%s", playing + 1, count, n->ChordName(playing).c_str(),
                  CP::PlayModeNames()[std::clamp(n->playMode, 0, CP::kNumPlayModes - 1)].c_str(),
                  armed ? "  -  restart armed" : "");
      else
         snprintf(stat, sizeof(stat), "%d chords, %.1f bars  -  %s", count, n->TotalBars(),
                  armed ? "restart armed" : "press play");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      const bool isLight = IsThemeLight();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImGuiIO& cio = ImGui::GetIO();
      const double beatsPerBar = Transport::Instance().BeatsPerBar();
      static CP::Slot sClip; // slot clipboard (Copy / Paste), shared by every node
      static bool sHasClip = false;

      // Keyboard audition (Turbo 0.51): the notes sound while the mouse is
      // down (keys) or ~400 ms (chips, voicings); the audio half also cuts
      // them after 1.5 s, so a missed mouse-up never leaves a stuck note.
      static ChordProgressionNode* sAudNode = nullptr;
      static double sAudStop = 0.0;
      static bool sAudHold = false;
      if (sAudNode == n && (ImGui::GetTime() >= sAudStop || (sAudHold && !ImGui::IsMouseDown(ImGuiMouseButton_Left))))
      {
         n->PreviewNotes(0, false);
         sAudNode = nullptr;
      }
      auto audition = [&](uint64_t m, bool hold) {
         if (m == 0)
            return;
         n->PreviewNotes(m, true);
         sAudNode = n;
         sAudHold = hold;
         sAudStop = ImGui::GetTime() + (hold ? 1.5 : 0.4);
      };

      // Slot strip: 8 per row, a "+" ghost cell after the last chord.
      // Click selects, drag reorders (insertion caret), right-click opens
      // the slot menu; Ctrl+D / Ctrl+C / Ctrl+V / Delete / Alt+arrows work
      // while a slot is hovered or was the last thing clicked.
      static ChordProgressionNode* sKeysNode = nullptr;
      static ChordProgressionNode* sDragNode = nullptr;
      static int sDragFrom = -1;
      static bool sDragLive = false;
      static ImVec2 sDragStart(0.0f, 0.0f);
      enum SlotOp { kOpNone, kOpDup, kOpInsBefore, kOpInsAfter, kOpDelete, kOpLeft, kOpRight, kOpCopy,
                    kOpPasteRep, kOpPasteIns, kOpClear, kOpAddGhost };
      SlotOp op = kOpNone;
      int opSlot = -1;
      {
         const float w = gAudioBodyW;
         const int perRow = 8;
         const int cells = count + (count < CP::kMaxChords ? 1 : 0);
         const int rows = (cells + perRow - 1) / perRow;
         const float gap = 4.0f;
         const float cellW = (w - gap * (float)(perRow - 1)) / (float)perRow;
         const float cellH = 46.0f; // Turbo 0.50: room for a two-line chord name
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImU32 idleCol = isLight ? IM_COL32(222, 226, 236, 255) : IM_COL32(40, 43, 54, 255);
         const ImU32 selCol = isLight ? IM_COL32(196, 210, 238, 255) : IM_COL32(58, 66, 90, 255);
         const ImU32 playCol = isLight ? IM_COL32(70, 140, 245, 255) : IM_COL32(70, 130, 210, 255);
         const ImU32 borderCol = isLight ? IM_COL32(40, 90, 200, 255) : IM_COL32(150, 200, 255, 255);
         const ImU32 textCol = isLight ? IM_COL32(30, 34, 44, 255) : IM_COL32(226, 230, 240, 255);
         const ImU32 subCol = isLight ? IM_COL32(80, 86, 100, 255) : IM_COL32(150, 156, 172, 255);
         auto cellMin = [&](int i) {
            return ImVec2(origin.x + (float)(i % perRow) * (cellW + gap), origin.y + (float)(i / perRow) * (cellH + gap));
         };
         bool anyHover = false;
         int hoverIdx = -1;
         bool openMenu = false;
         for (int i = 0; i < cells; i++)
         {
            const ImVec2 mn = cellMin(i);
            const ImVec2 mx(mn.x + cellW, mn.y + cellH);
            ImGui::SetCursorScreenPos(mn);
            ImGui::PushID(i);
            ImGui::InvisibleButton("##cpslot", ImVec2(cellW, cellH));
            const bool hovered = ImGui::IsItemHovered();
            const bool activated = ImGui::IsItemActivated();
            const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            ImGui::PopID();
            if (hovered)
            {
               anyHover = true;
               hoverIdx = i;
            }
            if (i >= count)
            {
               // Ghost cell: duplicates the last chord at the end.
               dl->AddRect(mn, mx, hovered ? borderCol : subCol, 4.0f, 0, 1.0f);
               const ImVec2 ts = ImGui::CalcTextSize("+");
               dl->AddText(ImVec2(mn.x + (cellW - ts.x) * 0.5f, mn.y + (cellH - ts.y) * 0.5f),
                           hovered ? borderCol : subCol, "+");
               if (hovered)
                  ImGui::SetTooltip("add a chord (copy of the last)");
               if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                  op = kOpAddGhost;
               continue;
            }
            if (activated)
            {
               n->selected = i;
               sKeysNode = n;
               sDragNode = n;
               sDragFrom = i;
               sDragLive = false;
               sDragStart = cio.MousePos;
            }
            if (rightClicked)
            {
               n->selected = i;
               sKeysNode = n;
               opSlot = i;
               openMenu = true;
            }
            ImU32 bg = (i == playing) ? playCol : (i == n->selected ? selCol : idleCol);
            dl->AddRectFilled(mn, mx, bg, 4.0f);
            if (i == playing)
            {
               const float p = std::clamp(n->PlayingProgress(), 0.0f, 1.0f);
               dl->AddRectFilled(ImVec2(mn.x + 2.0f, mx.y - 4.0f), ImVec2(mn.x + 2.0f + (cellW - 4.0f) * p, mx.y - 2.0f),
                                 IM_COL32(255, 255, 255, 170), 1.0f);
            }
            if (i == n->selected || hovered)
               dl->AddRect(mn, mx, i == n->selected ? borderCol : subCol, 4.0f, 0, i == n->selected ? 1.8f : 1.0f);
            // Turbo 0.50: a name too wide for the cell ("C# MIN7b5") wraps
            // at its first space: root on top, quality underneath.
            const std::string name = n->ChordName(i);
            const ImU32 nameCol = i == playing ? IM_COL32(255, 255, 255, 255) : textCol;
            ImVec2 ts = ImGui::CalcTextSize(name.c_str());
            dl->PushClipRect(mn, mx, true);
            const size_t space = name.find(' ');
            if (ts.x > cellW - 4.0f && space != std::string::npos)
            {
               const std::string top = name.substr(0, space), bottom = name.substr(space + 1);
               const ImVec2 t1 = ImGui::CalcTextSize(top.c_str()), t2 = ImGui::CalcTextSize(bottom.c_str());
               dl->AddText(ImVec2(mn.x + std::max(3.0f, (cellW - t1.x) * 0.5f), mn.y + 3.0f), nameCol, top.c_str());
               dl->AddText(ImVec2(mn.x + std::max(3.0f, (cellW - t2.x) * 0.5f), mn.y + 16.0f), nameCol, bottom.c_str());
            }
            else
            {
               dl->AddText(ImVec2(mn.x + std::max(3.0f, (cellW - ts.x) * 0.5f), mn.y + 9.0f), nameCol, name.c_str());
            }
            const std::string bars = CP::BarsLabel(n->chordBars[i], beatsPerBar);
            ts = ImGui::CalcTextSize(bars.c_str());
            dl->AddText(ImVec2(mn.x + (cellW - ts.x) * 0.5f, mn.y + 29.0f),
                        i == playing ? IM_COL32(235, 240, 255, 220) : subCol, bars.c_str());
            dl->PopClipRect();
         }

         // Manual drag (as the Layer Stack): past a small threshold the slot
         // follows the mouse and an insertion caret shows where it lands.
         if (sDragNode == n && sDragFrom >= 0 && sDragFrom < count)
         {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               const ImVec2 m = cio.MousePos;
               if (!sDragLive && std::fabs(m.x - sDragStart.x) + std::fabs(m.y - sDragStart.y) > 6.0f)
                  sDragLive = true;
               if (sDragLive)
               {
                  const int col = std::clamp((int)std::floor((m.x - origin.x) / (cellW + gap)), 0, perRow - 1);
                  const int row = std::clamp((int)std::floor((m.y - origin.y) / (cellH + gap)), 0, rows - 1);
                  const int idx = std::clamp(row * perRow + col, 0, count - 1);
                  const ImVec2 mn = cellMin(idx);
                  const bool before = m.x < mn.x + cellW * 0.5f;
                  const float cx = before ? mn.x - gap * 0.5f : mn.x + cellW + gap * 0.5f;
                  const ImVec2 fromMn = cellMin(sDragFrom);
                  dl->AddRectFilled(fromMn, ImVec2(fromMn.x + cellW, fromMn.y + cellH), IM_COL32(0, 0, 0, 70), 4.0f);
                  dl->AddLine(ImVec2(cx, mn.y - 1.0f), ImVec2(cx, mn.y + cellH + 1.0f), borderCol, 3.0f);
               }
            }
            else
            {
               if (sDragLive)
               {
                  const ImVec2 m = cio.MousePos;
                  const int col = std::clamp((int)std::floor((m.x - origin.x) / (cellW + gap)), 0, perRow - 1);
                  const int row = std::clamp((int)std::floor((m.y - origin.y) / (cellH + gap)), 0, rows - 1);
                  const int idx = std::clamp(row * perRow + col, 0, count - 1);
                  const ImVec2 mn = cellMin(idx);
                  const int pos = idx + (m.x < mn.x + cellW * 0.5f ? 0 : 1);
                  const int dest = pos > sDragFrom ? pos - 1 : pos;
                  if (dest != sDragFrom && dest >= 0 && dest < count)
                  {
                     PushUndoCheckpoint();
                     n->MoveSlot(sDragFrom, dest);
                     gPatchDirty = true;
                  }
               }
               sDragNode = nullptr;
               sDragFrom = -1;
               sDragLive = false;
            }
         }
         // Menu drawn by DrawChordSlotDeferredMenu (outside the node editor).
         static_assert(kOpDup == 1 && kOpClear == 10, "SlotOp numbers are shared with DrawChordSlotDeferredMenu");
         if (openMenu)
         {
            gCpMenuNode = n;
            gCpMenuSlot = opSlot;
            gCpMenuCount = count;
            gCpMenuHasClip = sHasClip;
            gCpMenuOpen = true;
            gParamRightClickConsumedThisFrame = true; // keep the node menu closed
         }
         if (gCpMenuResultNode == n)
         {
            op = (SlotOp)gCpMenuResultOp;
            opSlot = gCpMenuResultSlot;
            gCpMenuResultNode = nullptr;
         }

         // Keys: only while the pointer is over the strip or a slot was the
         // last thing clicked (a click anywhere else lets go).
         if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
             !anyHover && sKeysNode == n)
            sKeysNode = nullptr;
         if ((anyHover || sKeysNode == n || sDragLive) && !cio.WantTextInput)
         {
            gChordSlotsKeysOwned = true;
            const int target = (hoverIdx >= 0 && hoverIdx < count) ? hoverIdx : n->selected;
            if (op == kOpNone)
            {
               if (cio.KeyCtrl && !cio.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false)) { op = kOpDup; opSlot = target; }
               else if (cio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) { op = kOpCopy; opSlot = target; }
               else if (cio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
               {
                  op = cio.KeyShift ? kOpPasteIns : kOpPasteRep;
                  opSlot = target;
               }
               else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) { op = kOpDelete; opSlot = target; }
               else if (cio.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) { op = kOpLeft; opSlot = target; }
               else if (cio.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) { op = kOpRight; opSlot = target; }
            }
         }

         // Apply the chosen operation (one undo step each).
         if (op != kOpNone)
         {
            const int at = std::clamp(opSlot, 0, count - 1);
            bool changed = true;
            const bool pasting = op == kOpPasteRep || op == kOpPasteIns;
            if (op != kOpCopy && (!pasting || sHasClip))
               PushUndoCheckpoint();
            switch (op)
            {
               case kOpAddGhost: n->selected = count - 1; changed = n->DuplicateSlot(count - 1); break;
               case kOpDup: changed = n->DuplicateSlot(at); break;
               case kOpInsBefore:
               {
                  CP::Slot e;
                  e.bars = n->chordBars[at];
                  changed = n->InsertSlot(at, e);
                  if (changed)
                     n->selected = at;
                  break;
               }
               case kOpInsAfter:
               {
                  CP::Slot e;
                  e.bars = n->chordBars[at];
                  changed = n->InsertSlot(at + 1, e);
                  if (changed)
                     n->selected = at + 1;
                  break;
               }
               case kOpDelete: changed = n->DeleteSlot(at); break;
               case kOpLeft: changed = n->MoveSlot(at, at - 1); break;
               case kOpRight: changed = n->MoveSlot(at, at + 1); break;
               case kOpCopy: sClip = n->GetSlot(at); sHasClip = true; changed = false; break;
               case kOpPasteRep:
                  changed = sHasClip;
                  if (changed)
                     n->SetSlot(at, sClip);
                  break;
               case kOpPasteIns:
                  changed = sHasClip && n->InsertSlot(at + 1, sClip);
                  if (changed)
                     n->selected = at + 1;
                  break;
               case kOpClear:
               {
                  CP::Slot e = n->GetSlot(at);
                  e.lo = e.hi = 0;
                  e.slash = -1;
                  n->SetSlot(at, e);
                  break;
               }
               default: changed = false; break;
            }
            if (changed)
               gPatchDirty = true;
            count = std::clamp(n->chordCount, 1, CP::kMaxChords);
            n->selected = std::clamp(n->selected, 0, count - 1);
            sel = n->selected;
         }
         sel = n->selected = std::clamp(n->selected, 0, count - 1);
         ImGui::SetCursorScreenPos(origin);
         ImGui::Dummy(ImVec2(w, (float)rows * cellH + (float)(rows - 1) * gap));
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // The selected chord's full 4-octave mask, its root and slash bass.
      uint64_t selMask = n->FullMask(sel);
      int selRoot = 0, selQuality = -1;
      CP::AnalyseMask(selMask, selRoot, selQuality, n->slashBass[sel]);
      if (selMask == 0)
         selRoot = n->builderRoot;

      // Keyboard: the selected chord's keys, click to toggle and audition,
      // Shift+click sets the slash bass. It shows two of the four octaves
      // (the "<" ">" buttons below scroll it; independent of playback).
      {
         bool held[128] = {};
         signed char marks[128] = {};
         const int base = (n->baseOctave + 1) * 12;
         const int vo = std::clamp(n->viewOctave, 0, 2);
         const int low = base + vo * 12;
         int rootKey = -1;
         for (int k = 0; k < CP::kKeysTotal; k++)
            if (selMask & (1ull << k))
            {
               if (base + k >= 0 && base + k < 128)
                  held[base + k] = true;
               if (rootKey < 0 && k % 12 == selRoot)
                  rootKey = k;
            }
         if (rootKey >= 0 && base + rootKey < 128)
            marks[base + rootKey] = 1;
         if (n->slashBass[sel] >= 0)
            for (int k = vo * 12; k < vo * 12 + 24; k++)
               if (k % 12 == n->slashBass[sel] && base + k >= 0 && base + k < 128)
               {
                  if (marks[base + k] == 0)
                     marks[base + k] = 2;
                  break;
               }
         const int hit = DrawInteractiveKeyboard(held, low, 2, marks, true);
         if (ImGui::IsItemActivated() && hit >= low && hit < low + 24)
         {
            const int key = hit - base;
            PushUndoCheckpoint();
            if (cio.KeyShift)
            {
               n->slashBass[sel] = (n->slashBass[sel] == key % 12) ? -1 : key % 12; // click again clears
               audition(1ull << key, true);
            }
            else
            {
               selMask ^= 1ull << key;
               n->SetFullMask(sel, selMask);
               if (selMask & (1ull << key))
                  audition(1ull << key, true);
            }
            gPatchDirty = true;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("click: toggle a note\nShift+click: slash bass (again clears)\nwheel: scroll octaves");
         if (ImGui::IsItemHovered() && cio.MouseWheel != 0.0f)
            n->viewOctave = std::clamp(n->viewOctave + (cio.MouseWheel > 0.0f ? 1 : -1), 0, 2);
      }

      // View + voicing row.
      {
         const float x0 = gAudioContentX;
         const float gap = 4.0f;
         const float btnW = (gAudioContentW - gap * 7.0f) / 8.0f;
         const float y = ImGui::GetCursorScreenPos().y + 3.0f;
         float x = x0;
         auto btn = [&](const char* label, bool enabled) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            ImGui::BeginDisabled(!enabled);
            const bool pressed = ImGui::Button(label, ImVec2(btnW, 0.0f));
            ImGui::EndDisabled();
            x += btnW + gap;
            return pressed;
         };
         if (btn("<##cpview", n->viewOctave > 0))
            n->viewOctave--;
         if (btn(">##cpview", n->viewOctave < 2))
            n->viewOctave++;
         struct { const char* label; int op; } kVoicings[] = {
            { "close##cpv", 0 }, { "drop2##cpv", 1 }, { "spread##cpv", 2 }, { "8va+##cpv", 3 }, { "8va-##cpv", 4 }
         };
         for (const auto& v : kVoicings)
            if (btn(v.label, selMask != 0))
            {
               const uint64_t out = CP::Voice(selMask, v.op);
               if (out != selMask)
               {
                  PushUndoCheckpoint();
                  n->SetFullMask(sel, out);
                  gPatchDirty = true;
                  audition(out, false);
                  selMask = out;
               }
            }
         if (btn("lead##cp", selMask != 0))
         {
            // Nearest inversion / octave to the previous chord (cyclic).
            const int prevIdx = sel > 0 ? sel - 1 : (count > 1 ? count - 1 : -1);
            const uint64_t out = CP::VoiceLead(prevIdx >= 0 ? n->FullMask(prevIdx) : 0, selMask);
            if (out != selMask)
            {
               PushUndoCheckpoint();
               n->SetFullMask(sel, out);
               gPatchDirty = true;
               audition(out, false);
               selMask = out;
            }
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("voice-lead: the inversion and octave closest to the previous chord");
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(gAudioContentW, ImGui::GetFrameHeight()));
      }

      // Extension chips: add / remove intervals relative to the detected
      // root on the current notes (lit = present).
      {
         const float x0 = gAudioContentX;
         const float gap = 4.0f;
         const int perRow = 7;
         const float chipW = (gAudioContentW - gap * (float)(perRow - 1)) / (float)perRow;
         const float y0 = ImGui::GetCursorScreenPos().y + 2.0f;
         const ImVec4 litCol = ImGui::ColorConvertU32ToFloat4(isLight ? IM_COL32(70, 140, 245, 255) : IM_COL32(70, 130, 210, 255));
         for (int e = 0; e < CP::kNumExtensions; e++)
         {
            const bool lit = CP::HasExtension(selMask, selRoot, e);
            ImGui::SetCursorScreenPos(ImVec2(x0 + (float)(e % perRow) * (chipW + gap),
                                             y0 + (float)(e / perRow) * (ImGui::GetFrameHeight() + 3.0f)));
            if (lit)
            {
               ImGui::PushStyleColor(ImGuiCol_Button, litCol);
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            }
            char label[24];
            snprintf(label, sizeof(label), "%s##cpext%d", CP::ExtensionName(e), e);
            if (ImGui::Button(label, ImVec2(chipW, 0.0f)))
            {
               PushUndoCheckpoint();
               const uint64_t out = CP::ToggleExtension(selMask, selRoot, e);
               n->SetFullMask(sel, out);
               gPatchDirty = true;
               audition(out, false);
            }
            if (lit)
               ImGui::PopStyleColor(2);
         }
         ImGui::SetCursorScreenPos(ImVec2(x0, y0));
         ImGui::Dummy(ImVec2(gAudioContentW, 2.0f * ImGui::GetFrameHeight() + 3.0f));
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Selected chord: length and builder.
      {
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 5.0f;
         const float lenW = 96.0f, rootW = 58.0f, qualW = 82.0f;
         const float btnW = (gAudioContentW - lenW - rootW - qualW - gap * 5.0f) / 3.0f;
         float x = x0;

         // Length: a value field, not a slider. Drag snaps to 1 beat (Shift
         // 1/4 beat, Alt 1 bar); double-click, right-click or typing a digit
         // opens text entry ("2", "1.5", "3b", "1.2" = bar.beat, "2:2").
         // No modulation param: it is per slot, like the keys.
         {
            static bool sEdit = false;
            static bool sFocus = false;
            static int sEditFrames = 0;
            static char sBuf[32] = "";
            static float sDragBase = 0.0f;
            static float sDragPx = 0.0f;
            static bool sDragUndo = false;
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            const float fh = ImGui::GetFrameHeight();
            if (sEdit)
            {
               ImGui::SetNextItemWidth(lenW);
               if (sFocus)
               {
                  ImGui::SetKeyboardFocusHere();
                  sFocus = false;
               }
               const bool enter = ImGui::InputText("##cpbarsText", sBuf, sizeof(sBuf),
                                                   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               float v = 0.0f;
               if (enter && CP::ParseBarsText(sBuf, beatsPerBar, v))
               {
                  PushUndoCheckpoint();
                  n->chordBars[sel] = v;
                  gPatchDirty = true;
               }
               sEditFrames++;
               if (enter || ImGui::IsItemDeactivated() || (sEditFrames > 3 && !ImGui::IsItemActive()))
                  sEdit = false;
            }
            else
            {
               ImGui::InvisibleButton("##cpbars", ImVec2(lenW, fh));
               const bool hov = ImGui::IsItemHovered();
               const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
               if (ImGui::IsItemActivated())
               {
                  sDragBase = n->chordBars[sel];
                  sDragPx = 0.0f;
                  sDragUndo = false;
               }
               if (ImGui::IsItemActive())
               {
                  sDragPx += cio.MouseDelta.x;
                  const double beat = 1.0 / std::max(1.0, beatsPerBar);
                  const double step = cio.KeyAlt ? 1.0 : (cio.KeyShift ? beat * 0.25 : beat);
                  const double snapped = std::round((double)sDragBase / step) * step;
                  const double nv = std::clamp(snapped + std::round((double)sDragPx / 10.0) * step,
                                               (double)CP::kMinBars, 16.0);
                  if (std::fabs(nv - (double)n->chordBars[sel]) > 1.0e-6)
                  {
                     if (!sDragUndo)
                     {
                        PushUndoCheckpoint();
                        sDragUndo = true;
                     }
                     n->chordBars[sel] = (float)nv;
                     gPatchDirty = true;
                  }
               }
               bool open = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
               open = open || ImGui::IsItemClicked(ImGuiMouseButton_Right);
               bool typed = false;
               if (hov && !cio.WantTextInput && !cio.KeyCtrl)
                  for (int c = 0; c < cio.InputQueueCharacters.Size; c++)
                  {
                     const ImWchar ch = cio.InputQueueCharacters[c];
                     if ((ch >= '0' && ch <= '9') || ch == '.')
                        typed = true;
                  }
               if (open || typed)
               {
                  sEdit = true;
                  sFocus = true;
                  sEditFrames = 0;
                  sBuf[0] = '\0';
                  if (open)
                  {
                     const std::string cur = CP::BarsLabel(n->chordBars[sel], beatsPerBar);
                     const size_t sp = cur.find(' ');
                     snprintf(sBuf, sizeof(sBuf), "%s%s", cur.substr(0, sp).c_str(),
                              cur.compare(sp + 1, std::string::npos, "beat") == 0 ? "b" : "");
                  }
               }
               dl->AddRectFilled(mn, mx, ImGui::GetColorU32(hov ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 4.0f);
               const std::string label = CP::BarsLabel(n->chordBars[sel], beatsPerBar);
               const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
               dl->AddText(ImVec2(mn.x + (lenW - ts.x) * 0.5f, mn.y + (fh - ts.y) * 0.5f),
                           ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
               if (hov)
                  ImGui::SetTooltip("length: drag = 1 beat (Shift 1/4 beat, Alt 1 bar)\n"
                                    "double-click or type: 2, 1.5, 3b, 1.2 (bar.beat), 2:2");
            }
         }
         x += lenW + gap;

         ImGui::SetCursorScreenPos(ImVec2(x, y));
         AudioBareDropdown("cpRoot", NoteNameList(), n->builderRoot,
                           [n](int i) { n->builderRoot = i; }, rootW);
         x += rootW + gap;
         ImGui::SetCursorScreenPos(ImVec2(x, y));
         {
            // Turbo: grouped (triads, 6ths, 7ths, extended, altered, voicings),
            // listed in group order; the dropdown works on display positions.
            static std::vector<std::string> qNames, qCats;
            const std::vector<int>& qOrder = CP::QualityDisplayOrder();
            if (qNames.empty())
               for (int q : qOrder)
               {
                  qNames.push_back(CP::QualityNames()[q]);
                  qCats.push_back(CP::QualityCategories()[q]);
               }
            int shown = 0;
            for (int i = 0; i < (int)qOrder.size(); i++)
               if (qOrder[i] == n->builderQuality)
                  shown = i;
            AudioBareDropdown("cpQuality", qNames, shown,
                              [n, &qOrder](int i) {
                                 if (i >= 0 && i < (int)qOrder.size())
                                    n->builderQuality = qOrder[i];
                              }, qualW, qCats, true);
         }
         x += qualW + gap;

         ImGui::SetCursorScreenPos(ImVec2(x, y));
         if (ImGui::Button("set##cp", ImVec2(btnW, 0.0f)))
         {
            PushUndoCheckpoint();
            n->SetFullMask(sel, CP::BuildMask(n->builderRoot, n->builderQuality));
            n->slashBass[sel] = -1;
            gPatchDirty = true;
            audition(n->FullMask(sel), false);
         }
         x += btnW + gap;
         ImGui::SetCursorScreenPos(ImVec2(x, y));
         if (ImGui::Button("inv##cp", ImVec2(btnW, 0.0f)))
         {
            PushUndoCheckpoint();
            n->SetFullMask(sel, CP::InvertMask(n->FullMask(sel)));
            gPatchDirty = true;
            audition(n->FullMask(sel), false);
         }
         x += btnW + gap;
         ImGui::SetCursorScreenPos(ImVec2(x, y));
         if (ImGui::Button("clear##cp", ImVec2(btnW, 0.0f)))
         {
            PushUndoCheckpoint();
            n->SetFullMask(sel, 0);
            n->slashBass[sel] = -1;
            gPatchDirty = true;
         }
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(gAudioContentW, ImGui::GetFrameHeight()));
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Progression-wide knobs.
      {
         AudioKnobRow row(5, kKnobLarge);
         row.KnobInt("chords", &n->chordCount, 1, CP::kMaxChords, kKnobLarge);
         row.KnobInt("octave", &n->baseOctave, -1, 7, kKnobLarge);
         row.KnobInt("transpose", &n->transpose, -12, 12, kKnobLarge);
         row.Knob("velocity", &n->velocity, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("gate", &n->gate, 0.05f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      // How the chord is played. rate drives the arp / pulse / alberti /
      // bass + chord steps, strum the strum spread, octaves the arp range;
      // each greys out when the mode does not use it.
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         const int mode = std::clamp(n->playMode, 0, CP::kNumPlayModes - 1);
         const bool isStrum = mode == CP::kStrumUp || mode == CP::kStrumDown;
         const bool isArp = mode >= CP::kArpUp && mode <= CP::kArpRandom;
         AudioKnobRow row(4, kKnobSmall, ImGui::GetFrameHeight() + 5.0f);
         row.Dropdown("play", CP::PlayModeNames(), mode, [n](int i) { PushUndoCheckpoint(); n->playMode = i; });
         ImGui::BeginDisabled(!CP::IsStepMode(mode));
         row.Dropdown("rate", MusicTime::RateDivisionList(),
                      std::clamp(n->rateDiv, 0, MusicTime::kNumRateDivisions - 1),
                      [n](int i) { PushUndoCheckpoint(); n->rateDiv = i; });
         ImGui::EndDisabled();
         ImGui::BeginDisabled(!isStrum);
         row.Knob("strum", &n->strumMs, 0.0f, 250.0f, "%.0f ms");
         ImGui::EndDisabled();
         ImGui::BeginDisabled(!isArp);
         row.KnobInt("octaves", &n->arpOctaves, 1, 3);
         ImGui::EndDisabled();
         row.End();
      }
      AudioToggleButton("bass", &n->bass, 56.0f);
      ImGui::SameLine();
      if (AudioToggleButton("sets key", &n->setsKey, 76.0f) && n->setsKey)
         n->followKey = false;
      ImGui::SameLine();
      // Turbo 0.55: follow the global key (written in homeKey, played in the
      // transport's key). Turning it on takes the current key as home.
      {
         const bool was = n->followKey;
         if (AudioToggleButton("follows key", &n->followKey, 92.0f) && n->followKey)
         {
            n->setsKey = false;
            if (!was)
               n->homeKey = Transport::Instance().Key();
         }
         if (n->followKey)
         {
            static const char* const kPc[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            ImGui::SameLine();
            ImGui::TextDisabled("home %s, %+d", kPc[((n->homeKey % 12) + 12) % 12], n->KeyOffset());
         }
      }

      // Turbo 0.55: suggestions (NT ChordGen styles + famous progressions).
      // load = the picked progression, random = a new one from the style's
      // templates; both in the global key, one bar per chord, undoable.
      {
         const std::vector<std::string>& styles = CP::SuggestionStyles();
         if (!styles.empty())
         {
            n->suggestStyle = std::clamp(n->suggestStyle, 0, (int)styles.size() - 1);
            const std::vector<int> picks = CP::SuggestionsInStyle(styles[n->suggestStyle]);
            std::vector<std::string> pickNames;
            for (int idx : picks)
               pickNames.push_back(CP::SuggestionName(idx));
            n->suggestPick = std::clamp(n->suggestPick, 0, std::max(0, (int)picks.size() - 1));
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            // Sized to stay inside kAudioNodeWidth (the row used to overflow it).
            DropdownButton("##cpSuggestStyle", styles, n->suggestStyle,
                           [n](int i) { n->suggestStyle = i; n->suggestPick = 0; }, 115.0f, /*modulatable=*/false);
            ImGui::SameLine();
            DropdownButton("##cpSuggestPick", pickNames, n->suggestPick,
                           [n](int i) { n->suggestPick = i; }, 165.0f, /*modulatable=*/false);
            ImGui::SameLine();
            if (ImGui::SmallButton("load##cpSuggest") && !picks.empty())
            {
               PushUndoCheckpoint();
               n->LoadSuggestion(picks[n->suggestPick], Transport::Instance().Key());
               gPatchDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("random##cpSuggest"))
            {
               PushUndoCheckpoint();
               n->RandomFromStyle(styles[n->suggestStyle], Transport::Instance().Key(),
                                  (unsigned)(glfwGetTime() * 1000.0) ^ (unsigned)(gn.index * 2654435761u));
               gPatchDirty = true;
            }
         }
      }

      // Turbo 0.50: quantized restart.
      if (DrawRestartControls(&n->restartQuant, armed))
         n->RequestRestart();

      EndAudioBody();
   }

   // Turbo 0.48: MIDI File. Load, file tempo, a mini piano roll of the notes
   // that will play (track / channel filter applied, the rest faint) with
   // the loop end and the playhead, then the filter and playback controls.
   void DrawMidiFileBody(GraphNode& gn, MidiFileNode* n)
   {
      const MidiFile::Data* d = n->Data();
      char stat[200];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...##midiLoad", ImVec2(90, 0)))
         StartNodeFileDialog(n, Platform::OpenMidiDialog, [](auto* m, const std::string& path) {
            PushUndoCheckpoint();
            m->LoadFile(path);
            gPatchDirty = true;
         });
      if (d != nullptr)
      {
         ImGui::SameLine();
         char tempoLabel[64];
         snprintf(tempoLabel, sizeof(tempoLabel), "use file tempo (%.1f)##midiTempo", n->FileTempo());
         if (ImGui::Button(tempoLabel))
            Transport::Instance().SetTempo((float)std::clamp(n->FileTempo(), 20.0, 400.0));
         if (ImGui::IsItemHovered())
            SetAudioReadout("tempo", "sets the transport tempo to the file's first tempo");
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Piano roll.
      {
         const bool isLight = IsThemeLight();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float w = gAudioContentW;
         const float h = 96.0f;
         const ImVec2 mn(gAudioContentX, ImGui::GetCursorScreenPos().y);
         const ImVec2 mx(mn.x + w, mn.y + h);
         dl->AddRectFilled(mn, mx, isLight ? IM_COL32(226, 230, 238, 255) : IM_COL32(22, 24, 31, 255), 4.0f);
         if (d != nullptr && !d->notes.empty())
         {
            const double fileLen = std::max(d->LengthBeats(), 1.0e-3);
            const double playLen = std::max(n->PlayLengthBeats(), 1.0e-3);
            const double span = std::max(fileLen, playLen);
            const double bpb = std::max(0.25, Transport::Instance().BeatsPerBar());
            int lo = 127, hi = 0;
            for (const MidiFile::Note& note : d->notes)
            {
               lo = std::min(lo, (int)note.key);
               hi = std::max(hi, (int)note.key);
            }
            lo = std::max(0, lo - 1);
            hi = std::min(127, hi + 1);
            const float rowH = (h - 4.0f) / (float)(hi - lo + 1);
            auto xOf = [&](double beat) { return mn.x + 2.0f + (float)(beat / span) * (w - 4.0f); };
            dl->PushClipRect(mn, mx, true);
            const ImU32 barCol = isLight ? IM_COL32(190, 196, 210, 255) : IM_COL32(44, 48, 60, 255);
            const int bars = (int)std::min(512.0, std::ceil(span / bpb));
            for (int b = 1; b < bars; b++)
               dl->AddLine(ImVec2(xOf(b * bpb), mn.y), ImVec2(xOf(b * bpb), mx.y), barCol);
            const ImU32 onCol = isLight ? IM_COL32(60, 120, 230, 255) : IM_COL32(110, 170, 255, 255);
            const ImU32 offCol = isLight ? IM_COL32(170, 178, 196, 255) : IM_COL32(64, 70, 88, 255);
            int drawn = 0;
            for (const MidiFile::Note& note : d->notes)
            {
               if (++drawn > 20000)
                  break;
               const bool plays = (n->track == 0 || note.track + 1 == n->track) &&
                                  (n->channel == 0 || note.channel + 1 == n->channel);
               const double s0 = d->TicksToBeats(note.startTick);
               const double s1 = std::max(s0 + span * 0.002, d->TicksToBeats(note.endTick));
               const float y1 = mx.y - 2.0f - (float)(note.key - lo) * rowH;
               dl->AddRectFilled(ImVec2(xOf(s0), y1 - std::max(1.0f, rowH - 0.5f)), ImVec2(xOf(s1), y1),
                                 plays && s0 < playLen ? onCol : offCol);
            }
            if (playLen < span - 1.0e-6)
            {
               // Loop end: the part after it never plays.
               dl->AddRectFilled(ImVec2(xOf(playLen), mn.y), mx, isLight ? IM_COL32(0, 0, 0, 30) : IM_COL32(0, 0, 0, 90));
               dl->AddLine(ImVec2(xOf(playLen), mn.y), ImVec2(xOf(playLen), mx.y), IM_COL32(230, 160, 60, 255), 1.5f);
            }
            const double ph = n->PlayheadBeats();
            if (ph >= 0.0)
               dl->AddLine(ImVec2(xOf(ph), mn.y), ImVec2(xOf(ph), mx.y), isLight ? IM_COL32(20, 20, 20, 255) : IM_COL32(255, 255, 255, 230), 1.5f);
            dl->PopClipRect();
         }
         else
         {
            const char* hint = "load or drop a .mid file";
            const ImVec2 ts = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(mn.x + (w - ts.x) * 0.5f, mn.y + (h - ts.y) * 0.5f),
                        isLight ? IM_COL32(110, 116, 130, 255) : IM_COL32(120, 126, 142, 255), hint);
         }
         ImGui::SetCursorScreenPos(mn);
         ImGui::Dummy(ImVec2(w, h));
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Which notes: track, channel, and the grid playback starts on. The
      // track list always has an entry so the dropdown keeps its param slot.
      {
         static const std::vector<std::string> kNoTracks = { "all tracks" };
         static const std::vector<std::string> kChannels = [] {
            std::vector<std::string> v = { "all" };
            for (int c = 1; c <= 16; c++)
               v.push_back(std::to_string(c));
            return v;
         }();
         const std::vector<std::string>& tracks = n->TrackNames().empty() ? kNoTracks : n->TrackNames();
         AudioKnobRow row(3, kKnobSmall, ImGui::GetFrameHeight() + 5.0f);
         row.Dropdown("track", tracks, std::clamp(n->track, 0, (int)tracks.size() - 1),
                      [n](int i) { PushUndoCheckpoint(); n->track = i; });
         row.Dropdown("channel", kChannels, std::clamp(n->channel, 0, 16),
                      [n](int i) { PushUndoCheckpoint(); n->channel = i; });
         row.Dropdown("start", MusicTime::RateDivisionList(),
                      std::clamp(n->quantize, 0, MusicTime::kNumRateDivisions - 1),
                      [n](int i) { PushUndoCheckpoint(); n->quantize = i; });
         row.End();
      }
      {
         AudioKnobRow row(3, kKnobLarge);
         row.KnobInt("transpose", &n->transpose, -48, 48, kKnobLarge);
         row.Knob("velocity", &n->velocityScale, 0.0f, 2.0f, "%.2f", kKnobLarge);
         ImGui::BeginDisabled(!n->loop);
         row.KnobInt("loop bars", &n->loopBars, 0, 64, kKnobLarge);
         ImGui::EndDisabled();
         row.End();
      }
      AudioToggleButton("play##midiPlay", &n->play, 56.0f);
      ImGui::SameLine();
      AudioToggleButton("loop##midiLoop", &n->loop, 56.0f);
      if (n->loop && n->loopBars == 0)
      {
         ImGui::SameLine();
         ImGui::TextDisabled("loop = file length");
      }
      if (DrawRestartControls(&n->restartQuant, n->RestartArmed())) // Turbo 0.50
         n->RequestRestart();

      EndAudioBody();
   }

   void DrawKeyboardBody(GraphNode& gn, KeyboardNode* n)
   {
      bool held[128];
      n->HeldKeys(held);
      const int last = n->LastNote();
      char stat[64];
      if (last >= 0)
         snprintf(stat, sizeof(stat), "%s%d", NoteNameList()[((last % 12) + 12) % 12].c_str(), last / 12 - 1);
      else
         snprintf(stat, sizeof(stat), "%s", n->typingAlways && n->computerKeyboardEnabled ? "click, or type" : "click, or hover and type");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      const ImGuiIO& io = ImGui::GetIO();
      const bool isHovered = ed::GetHoveredNode() == ed::NodeId(gn.NodeId());
      const bool mods = io.KeyCtrl || io.KeyAlt || io.KeySuper || io.KeyShift;
      const bool typingLive = n->computerKeyboardEnabled && (isHovered || n->typingAlways) && !mods &&
                              !io.WantTextInput;

      // Turbo 0.55: - and = shift the octave while typing is live.
      if (typingLive && ImGui::IsKeyPressed(ImGuiKey_Minus, false))
         n->baseOctave = std::max(-1, n->baseOctave - 1);
      if (typingLive && ImGui::IsKeyPressed(ImGuiKey_Equal, false))
         n->baseOctave = std::min(7, n->baseOctave + 1);
      n->baseOctave = std::clamp(n->baseOctave, -1, 7);
      if (n->mUiOctave != n->baseOctave)
      {
         // knob, keys or a loaded patch moved the octave: let go of everything
         // first, so no note-off lands in the new octave
         for (int k = 0; k < 128; k++)
            n->SetKeyState(k, false);
         n->mMouseNote = -1;
         n->mUiOctave = n->baseOctave;
      }

      const int baseNote = (n->baseOctave + 1) * 12;
      const int lowNote = std::clamp(baseNote, 0, 103);
      const int hit = DrawInteractiveKeyboard(held, lowNote, 3);
      if (hit != n->mMouseNote)
      {
         if (n->mMouseNote >= 0)
            n->SetKeyState(n->mMouseNote, false);
         if (hit >= 0)
            n->SetKeyState(hit, true);
         n->mMouseNote = hit;
      }

      // Musical Typing (Z row = white keys, S row = black keys, Q row an octave
      // up) while the pointer is over this node, or always with "always" on
      // (Turbo 0.55). App shortcuts all use Ctrl or Shift, so plain letters are
      // free; Shift is excluded here so Shift+D/Shift+A never also play.
      // Releases are always processed so a key held while the pointer leaves
      // can't stick.
      for (const TypingKey& tk : kTypingKeys)
      {
         const int note = std::clamp(baseNote + tk.semitone, 0, 127);
         if (ImGui::IsKeyReleased(tk.key))
            n->SetKeyState(note, false);
         else if (typingLive && ImGui::IsKeyPressed(tk.key, false))
            n->SetKeyState(note, true);
      }

      ImGui::Dummy(ImVec2(0.0f, 5.0f));
      {
         AudioKnobRow row(4, kKnobLarge);
         row.KnobInt("octave", &n->baseOctave, -1, 7, kKnobLarge);
         row.KnobInt("transpose", &n->transpose, -24, 24, kKnobLarge);
         row.Knob("velocity", &n->velocityScale, 0.0f, 2.0f, "%.2f", kKnobLarge);
         row.Skip();
         row.End();
      }
      AudioToggleButton("typing", &n->computerKeyboardEnabled, 64.0f);
      ImGui::SameLine();
      ImGui::BeginDisabled(!n->computerKeyboardEnabled);
      AudioToggleButton("always", &n->typingAlways, 64.0f); // Turbo 0.55: no hover needed
      ImGui::EndDisabled();
      ImGui::SameLine();
      AudioToggleButton("global scale", &n->useGlobalScale, 96.0f);
      ImGui::SameLine();
      ImGui::TextDisabled("Z-M  Q-P   - = octave");

      EndAudioBody();
   }

   // ---- Spatial Mixer (Turbo 0.57, ported from upstream) ---------------------
   // Top-view stage (drag a dot: azimuth + distance, wheel: elevation, Alt-drag:
   // level, double-click: reset), a source strip with head-lock / mute / solo,
   // knobs for the selected object and the master. Turbo: no head tracking and
   // no own recorder (record from Audio Out / Output). Per-object knobs follow
   // the selected lane.
   namespace SpatialUi
   {
      constexpr float kEdgeMetres = 10.0f; // stage edge; radius mapping is sqrt so 1-2 m stay grabbable
      inline float RadiusFracFromDist(float d) { return std::sqrt(std::clamp(d, 0.0f, kEdgeMetres) / kEdgeMetres); }
      inline float DistFromRadiusFrac(float f) { return std::clamp(f * f * kEdgeMetres, 0.2f, kEdgeMetres); }
      inline ImU32 DotColor(int slot, float alpha)
      {
         const ImColor c = ImColor::HSV(std::fmod((float)slot * 0.137f + 0.02f, 1.0f), 0.55f, IsThemeLight() ? 0.75f : 0.95f);
         return IM_COL32((int)(c.Value.x * 255), (int)(c.Value.y * 255), (int)(c.Value.z * 255), (int)(alpha * 255));
      }
   }

   void DrawSpatialMixerBody(GraphNode& gn, SpatialMixerNode* n)
   {
      using namespace SpatialUi;
      const bool light = IsThemeLight();
      const int connected = n->ConnectedCount();
      const int pins = n->PinCount();
      n->selected = std::clamp(n->selected, 0, SpatialMixerNode::kMaxSlots - 1);
      char stat[112];
      if (connected > 0)
         snprintf(stat, sizeof(stat), "%d in -> %s  %.0f LUFS", connected, n->renderMode == 1 ? "stereo" : "binaural",
                  std::max(n->LufsShort(), -99.0f));
      else
         snprintf(stat, sizeof(stat), "0 in -> binaural (idle)");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      const float stageW = gAudioContentW, stageH = 280.0f;
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const ImVec2 centre(p0.x + stageW * 0.5f, p0.y + stageH * 0.5f);
      const float R = std::min(stageW, stageH) * 0.5f - 14.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const ImU32 line = light ? IM_COL32(0, 0, 0, 45) : IM_COL32(255, 255, 255, 40);
      const ImU32 text = light ? IM_COL32(0, 0, 0, 150) : IM_COL32(255, 255, 255, 150);
      dl->AddRectFilled(p0, ImVec2(p0.x + stageW, p0.y + stageH), light ? IM_COL32(0, 0, 0, 14) : IM_COL32(255, 255, 255, 10), 4.0f);
      dl->AddCircle(centre, R, line, 64);
      for (float m : { 1.0f, 2.0f })
         dl->AddCircle(centre, R * RadiusFracFromDist(m), line, 48);
      dl->AddLine(ImVec2(centre.x - R, centre.y), ImVec2(centre.x + R, centre.y), line);
      dl->AddLine(ImVec2(centre.x, centre.y - R), ImVec2(centre.x, centre.y + R), line);
      dl->AddText(ImVec2(centre.x - 3.0f, p0.y + 2.0f), text, "F");
      dl->AddText(ImVec2(centre.x - 3.0f, p0.y + stageH - ImGui::GetTextLineHeight() - 2.0f), text, "B");
      dl->AddText(ImVec2(p0.x + 5.0f, centre.y - 7.0f), text, "L");
      dl->AddText(ImVec2(p0.x + stageW - 12.0f, centre.y - 7.0f), text, "R");

      // Listener seen from above, facing up (towards F): shoulders, neck, head
      // and ears - the same view as the reference body mesh. Facing is shown by F.
      {
         const ImU32 skin = light ? IM_COL32(150, 165, 195, 255) : IM_COL32(86, 104, 140, 255);
         const ImU32 skinHi = light ? IM_COL32(178, 192, 218, 255) : IM_COL32(112, 132, 170, 255);
         const ImU32 shade = light ? IM_COL32(110, 124, 156, 255) : IM_COL32(62, 78, 112, 255);
         const ImU32 rim = light ? IM_COL32(60, 72, 104, 255) : IM_COL32(170, 190, 225, 255);
         const float cx = centre.x, cy = centre.y;
         // shoulders: wide rounded slab behind the head
         dl->AddEllipseFilled(ImVec2(cx, cy + 15.0f), ImVec2(30.0f, 10.0f), skin, 0.0f, 32);
         dl->AddEllipse(ImVec2(cx, cy + 15.0f), ImVec2(30.0f, 10.0f), rim, 0.0f, 32, 1.0f);
         // neck
         dl->AddEllipseFilled(ImVec2(cx, cy + 8.0f), ImVec2(6.0f, 7.0f), shade, 0.0f, 16);
         // ears (behind the head outline)
         dl->AddEllipseFilled(ImVec2(cx - 11.5f, cy + 1.0f), ImVec2(2.8f, 5.0f), skin, 0.0f, 12);
         dl->AddEllipseFilled(ImVec2(cx + 11.5f, cy + 1.0f), ImVec2(2.8f, 5.0f), skin, 0.0f, 12);
         dl->AddEllipse(ImVec2(cx - 11.5f, cy + 1.0f), ImVec2(2.8f, 5.0f), rim, 0.0f, 12, 1.0f);
         dl->AddEllipse(ImVec2(cx + 11.5f, cy + 1.0f), ImVec2(2.8f, 5.0f), rim, 0.0f, 12, 1.0f);
         // head: oval, lit from the front
         dl->AddEllipseFilled(ImVec2(cx, cy), ImVec2(11.0f, 14.0f), skin, 0.0f, 32);
         dl->AddEllipseFilled(ImVec2(cx, cy - 3.0f), ImVec2(7.0f, 8.0f), skinHi, 0.0f, 24);
         dl->AddEllipse(ImVec2(cx, cy), ImVec2(11.0f, 14.0f), rim, 0.0f, 32, 1.3f);
      }

      // ---- dots ----------------------------------------------------------
      auto dotPos = [&](int s) {
         const float rad = R * RadiusFracFromDist(n->distance[s]);
         const float a = n->azimuth[s] * 0.017453292f;
         return ImVec2(centre.x + std::sin(a) * rad, centre.y - std::cos(a) * rad);
      };

      ImGui::SetCursorScreenPos(p0);
      ImGui::InvisibleButton("##stage", ImVec2(stageW, stageH),
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      const bool stageHovered = ImGui::IsItemHovered();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      // Topmost dot under the cursor (later slots draw on top).
      int hot = -1;
      for (int s = 0; s < SpatialMixerNode::kMaxSlots; s++)
      {
         if (!n->inputs[s].IsConnected())
            continue;
         const ImVec2 d = dotPos(s);
         if ((mouse.x - d.x) * (mouse.x - d.x) + (mouse.y - d.y) * (mouse.y - d.y) <= 11.0f * 11.0f)
            hot = s;
      }

      static int sDragNode = -1, sDragSlot = -1;
      if (stageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hot >= 0)
      {
         n->selected = hot;
         sDragNode = gn.index;
         sDragSlot = hot;
         PushUndoCheckpoint(); // one checkpoint per drag, not per frame
      }
      if (stageHovered && hot >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         n->azimuth[hot] = SpatialMixerNode::DefaultAzimuth(hot);
         n->elevation[hot] = 0.0f;
         n->distance[hot] = 2.0f;
      }
      if (sDragNode == gn.index && sDragSlot >= 0)
      {
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            const int s = sDragSlot;
            if (ImGui::GetIO().KeyAlt)
            {
               n->gainDb[s] = std::clamp(n->gainDb[s] - ImGui::GetIO().MouseDelta.y * 0.25f, -60.0f, 12.0f);
            }
            else
            {
               const float dx = mouse.x - centre.x, dy = mouse.y - centre.y;
               float az = std::atan2(dx, -dy) * 57.29578f;
               if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper)
                  az = std::round(az / 15.0f) * 15.0f;
               n->azimuth[s] = az;
               n->distance[s] = DistFromRadiusFrac(std::sqrt(dx * dx + dy * dy) / R);
            }
         }
         else
            sDragNode = sDragSlot = -1;
      }
      if (hot >= 0 && ImGui::GetIO().MouseWheel != 0.0f)
         n->elevation[hot] = std::clamp(n->elevation[hot] + ImGui::GetIO().MouseWheel * 5.0f, -90.0f, 90.0f);

      // Dashed-less elevation cue: dot grows above ear level, shrinks below.
      for (int s = 0; s < SpatialMixerNode::kMaxSlots; s++)
      {
         const bool on = n->inputs[s].IsConnected();
         if (!on)
            continue;
         const ImVec2 d = dotPos(s);
         const bool silent = n->mute[s];
         const float elScale = 1.0f + 0.4f * (n->elevation[s] / 90.0f);
         const float r = 7.0f * elScale;
         const float lvl = std::clamp(n->ChannelLevel(s), 0.0f, 1.0f);
         if (hot == s || n->selected == s)
            dl->AddCircle(d, r + 5.0f, text, 24, 1.5f);
         if (lvl > 0.002f)
            dl->AddCircle(d, r + 2.0f + lvl * 4.0f, DotColor(s, 0.5f), 24, 2.0f);
         dl->AddCircleFilled(d, r, DotColor(s, silent ? 0.25f : 1.0f), 24);
         dl->AddCircle(d, r, light ? IM_COL32(0, 0, 0, 120) : IM_COL32(255, 255, 255, 120), 24, 1.0f);
         char lab[4];
         snprintf(lab, sizeof(lab), "%d", s + 1);
         const ImVec2 ts = ImGui::CalcTextSize(lab);
         dl->AddText(ImVec2(d.x - ts.x * 0.5f, d.y - ts.y * 0.5f), IM_COL32(0, 0, 0, 220), lab);
      }

      {
         const int s = hot >= 0 ? hot : n->selected;
         if (n->inputs[s].IsConnected() && (hot >= 0 || sDragNode == gn.index))
         {
            char r[96];
            snprintf(r, sizeof(r), "in %d  az %.0f  el %.0f  %.1f m  %+.1f dB", s + 1, n->azimuth[s],
                     n->elevation[s], n->distance[s], n->gainDb[s]);
            SetAudioReadout("object", r);
         }
      }
      ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + stageH + 6.0f));

      // Source strip: one row per connected input.
      for (int s = 0; s < pins; s++)
      {
         if (!n->inputs[s].IsConnected())
            continue;
         ImGui::PushID(s);
         const ImVec2 rp = ImGui::GetCursorScreenPos();
         const float ty = ImGui::GetStyle().FramePadding.y;
         dl->AddCircleFilled(ImVec2(rp.x + 8.0f, rp.y + ty + ImGui::GetTextLineHeight() * 0.5f), 5.0f, DotColor(s, 1.0f), 16);
         ImGui::SetCursorScreenPos(ImVec2(rp.x + 20.0f, rp.y + ty));
         if (ImGui::Selectable(("in " + std::to_string(s + 1)).c_str(), n->selected == s, 0, ImVec2(80.0f, 0.0f)))
            n->selected = s;
         const float bw = 26.0f, gap = 3.0f;
         const float right = gAudioContentX + gAudioContentW;
         const float x0 = right - 3.0f * bw - 2.0f * gap;
         char db[24];
         snprintf(db, sizeof(db), "%+.1f dB", n->gainDb[s]);
         ImGui::SetCursorScreenPos(ImVec2(x0 - 8.0f - ImGui::CalcTextSize(db).x, rp.y + ty));
         ImGui::TextUnformatted(db);
         bool* flags[3] = { &n->headLocked[s], &n->mute[s], &n->solo[s] };
         static const char* const kLbl[3] = { "H", "M", "S" };
         for (int k = 0; k < 3; k++)
         {
            ImGui::SetCursorScreenPos(ImVec2(x0 + (float)k * (bw + gap), rp.y));
            const bool on = *flags[k];
            if (on)
               ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(kLbl[k], ImVec2(bw, 0.0f)))
            {
               PushUndoCheckpoint();
               *flags[k] = !on;
            }
            if (on)
               ImGui::PopStyleColor();
            if (k == 0 && ImGui::IsItemHovered())
               SetAudioReadout("head lock", n->headLocked[s] ? "stays in front of your head" : "fixed in the room");
         }
         ImGui::PopID();
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      const int sel = n->selected;
      {
         AudioKnobRow row(4, kKnobLarge);
         row.Knob("azimuth", &n->azimuth[sel], -180.0f, 180.0f, "%.0f deg", kKnobLarge);
         row.Knob("elevation", &n->elevation[sel], -90.0f, 90.0f, "%.0f deg", kKnobLarge);
         row.Knob("distance", &n->distance[sel], 0.2f, kEdgeMetres, "%.1f m", kKnobLarge);
         row.Knob("width", &n->width[sel], 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(4, kKnobSmall);
         row.Knob("level", &n->gainDb[sel], -60.0f, 12.0f, "%.1f dB", kKnobSmall, true);
         row.Knob("room", &n->room, 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.Knob("bass mono", &n->bassHz, 0.0f, 300.0f, "%.0f Hz", kKnobSmall);
         row.Knob("out", &n->outDb, -60.0f, 12.0f, "%.1f dB", kKnobSmall, true);
         row.End();
      }
      {
         AudioToggleButton("limit##spatialLimit", &n->limiter, 72.0f);
         if (ImGui::IsItemHovered())
            SetAudioReadout("limiter", n->limiter ? "true-peak ceiling -1 dB on the output" : "off: output can clip");
         ImGui::SameLine();
         bool model = n->hrtf == 1;
         if (AudioToggleButton(model ? "model##spatialHrtf" : "KEMAR##spatialHrtf", &model, 72.0f))
         {
            PushUndoCheckpoint();
            n->hrtf = model ? 1 : 0;
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout("ears", model ? "spherical-head model, no measured data" : "measured dummy head (MIT KEMAR)");
         ImGui::SameLine();
         bool stereo = n->renderMode == 1;
         if (AudioToggleButton("speakers##spatialStereo", &stereo, 84.0f))
         {
            PushUndoCheckpoint();
            n->renderMode = stereo ? 1 : 0;
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout("render", stereo ? "speaker-safe stereo pan" : "binaural, for headphones");
         char meter[96];
         snprintf(meter, sizeof(meter), "%.1f LUFS   peak %.1f dB   gr %.1f dB", std::max(n->LufsShort(), -99.0f),
                  std::max(n->TruePeakDb(), -99.0f), n->ReductionDb());
         ImGui::TextUnformatted(meter);
         if (ImGui::IsItemClicked())
            n->ResetPeak();
         if (ImGui::IsItemHovered())
            SetAudioReadout("meter", "short-term loudness, true peak (click to reset), limiter reduction");
      }
      EndAudioBody();
   }

   // ---- Pattern Player (Turbo 0.57) ------------------------------------------
   // Pattern bank browser (category, pattern, prev / next / random, all
   // MIDI-learnable), a step view with the playhead, play / stop, and the
   // chord source: what is played into it, or the global key and scale.
   // Turbo 0.57: start / end trim handles over a step grid (Pattern Player
   // steps, Note Looper sixteenths). Values are whole cells, end exclusive;
   // the part outside the range is dimmed. `cellsPerBeat` > 0 shows beats in
   // the readout. Returns true when a handle moved this frame. The cursor is
   // left where it was.
   bool GridTrimHandles(const char* id, ImVec2 o, float w, float h, int cells, int* start, int* end, int cellsPerBeat = 0)
   {
      if (cells <= 0)
         return false;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float cw = w / (float)cells;
      *start = std::clamp(*start, 0, cells - 1);
      *end = std::clamp(*end, *start + 1, cells);
      const ImU32 dim = IsThemeLight() ? IM_COL32(255, 255, 255, 150) : IM_COL32(0, 0, 0, 140);
      if (*start > 0)
         dl->AddRectFilled(o, ImVec2(o.x + (float)*start * cw, o.y + h), dim, 3.0f);
      if (*end < cells)
         dl->AddRectFilled(ImVec2(o.x + (float)*end * cw, o.y), ImVec2(o.x + w, o.y + h), dim, 3.0f);
      const ImVec2 back = ImGui::GetCursorScreenPos();
      bool changed = false;
      ImGui::PushID(id);
      for (int which = 0; which < 2; which++)
      {
         int* val = which == 0 ? start : end;
         const float x = o.x + (float)*val * cw;
         const ImU32 col = which == 0 ? TrimStartCol() : TrimEndCol();
         dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + h), col, 2.0f);
         const float grip = 8.0f;
         const float gx = std::clamp(x, o.x + grip * 0.5f, o.x + w - grip * 0.5f);
         dl->AddTriangleFilled(ImVec2(gx - grip * 0.5f, o.y), ImVec2(gx + grip * 0.5f, o.y), ImVec2(gx, o.y + grip), col);
         dl->AddTriangleFilled(ImVec2(gx - grip * 0.5f, o.y + h), ImVec2(gx + grip * 0.5f, o.y + h),
                               ImVec2(gx, o.y + h - grip), col);
         const float hw = 10.0f;
         ImGui::SetCursorScreenPos(ImVec2(std::clamp(x - hw * 0.5f, o.x, o.x + w - hw), o.y));
         ImGui::InvisibleButton(which == 0 ? "##trimstart" : "##trimend", ImVec2(hw, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
         {
            int c = (int)std::lround((ImGui::GetIO().MousePos.x - o.x) / cw);
            c = which == 0 ? std::clamp(c, 0, *end - 1) : std::clamp(c, *start + 1, cells);
            if (c != *val)
            {
               *val = c;
               changed = true;
            }
         }
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            char line[96];
            if (cellsPerBeat > 0)
               snprintf(line, sizeof(line), "trim %s - plays beats %.2f..%.2f of %.2f", which == 0 ? "start" : "end",
                        (float)*start / cellsPerBeat, (float)*end / cellsPerBeat, (float)cells / cellsPerBeat);
            else
               snprintf(line, sizeof(line), "trim %s - plays steps %d..%d of %d", which == 0 ? "start" : "end", *start + 1, *end,
                        cells);
            SetAudioReadout("#", line);
         }
      }
      ImGui::PopID();
      ImGui::SetCursorScreenPos(back);
      return changed;
   }

   void DrawPatternPlayerBody(GraphNode& gn, PatternPlayerNode* n)
   {
      const NotePatterns::Library& lib = NotePatterns::Current();
      const NotePatterns::Pattern* pat = n->CurrentPattern();
      int tones[8];
      const int toneCount = n->CurrentTones(tones);
      const int step = n->PlayingStep();
      char stat[160];
      if (pat == nullptr)
         snprintf(stat, sizeof(stat), "pattern \"%s\" not found", n->patternName.c_str());
      else if (n->playing && step >= 0)
         snprintf(stat, sizeof(stat), "%s / %s - step %d/%d%s", pat->category.c_str(), pat->name.c_str(), step + 1, pat->steps,
                  n->trimStart > 0 || n->trimEnd > 0 ? " (trimmed)" : "");
      else if (n->playing)
         snprintf(stat, sizeof(stat), "%s / %s - starts on the next bar", pat->category.c_str(), pat->name.c_str());
      else
         snprintf(stat, sizeof(stat), "%s / %s - stopped", pat->category.c_str(), pat->name.c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // play / stop and browsing
      {
         const bool was = n->playing;
         AudioToggleButton("play##patternPlay", &n->playing, 72.0f);
         if (n->playing && !was && !Transport::Instance().IsPlaying())
            Transport::Instance().SetPlaying(true);
         ImGui::SameLine();
         if (ModTriggerButton("prev pattern", ImVec2(46.0f, 0.0f), "<##ppPrev"))
            n->Step(-1);
         ImGui::SameLine();
         if (ModTriggerButton("next pattern", ImVec2(46.0f, 0.0f), ">##ppNext"))
            n->Step(1);
         ImGui::SameLine();
         if (ModTriggerButton("random pattern", ImVec2(64.0f, 0.0f), "random##ppRnd"))
            n->RandomInCategory();
         ImGui::SameLine();
         if (ImGui::Button("rescan##ppRescan"))
            NotePatterns::Rescan();
         if (ImGui::IsItemHovered())
            SetAudioReadout("your patterns", NotePatterns::UserFolder().c_str());
         ImGui::SameLine();
         if (ImGui::Button("open .mid##ppOpen"))
            StartNodeFileDialog(n, Platform::OpenMidiDialog, [](auto* m, const std::string& path) {
               PushUndoCheckpoint();
               std::string err;
               if (!m->ImportMidiFile(path, err))
                  RuntimeLog::Write("pattern player: %s", err.c_str());
               gPatchDirty = true;
            });
         if (ImGui::IsItemHovered())
            SetAudioReadout("open .mid", "copies the file into the bank (category Imported) and plays it");
      }
      {
         int catIndex = 0;
         for (int i = 0; i < (int)lib.categories.size(); i++)
            if (lib.categories[i] == n->category)
               catIndex = i;
         DropdownButton("category##ppCategory", lib.categories, catIndex, [n](int i) {
            const NotePatterns::Library& l = NotePatterns::Current();
            if (i < 0 || i >= (int)l.categories.size())
               return;
            PushUndoCheckpoint();
            n->category = l.categories[i];
            const std::vector<int> in = l.InCategory(n->category);
            if (!in.empty())
               n->SelectPattern(in.front());
         }, 150.0f);
         ImGui::SameLine();
         const std::vector<int> in = lib.InCategory(n->category);
         std::vector<std::string> names;
         int cur = 0;
         for (int i = 0; i < (int)in.size(); i++)
         {
            names.push_back(lib.patterns[in[i]].name);
            if (in[i] == n->PatternIndex())
               cur = i;
         }
         if (!names.empty())
            DropdownButton("pattern##ppPattern", names, cur, [n](int i) {
               const std::vector<int> l = NotePatterns::Current().InCategory(n->category);
               if (i >= 0 && i < (int)l.size())
               {
                  PushUndoCheckpoint();
                  n->SelectPattern(l[i]);
               }
            }, 190.0f);
      }

      // step view: one column per step, rows by chord tone (chord = full column, bass = bottom)
      if (pat != nullptr && pat->steps > 0)
      {
         const float w = gAudioBodyW, h = 64.0f;
         const ImVec2 o = ImGui::GetCursorScreenPos();
         const ImVec2 br(o.x + w, o.y + h);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool light = IsThemeLight();
         dl->AddRectFilled(o, br, ScopeBgCol(), 4.0f);
         const float cw = w / (float)pat->steps;
         for (int sIdx = 0; sIdx < pat->steps; sIdx++)
         {
            const float x = o.x + (float)sIdx * cw;
            if (sIdx % 4 == 0)
               dl->AddLine(ImVec2(x, o.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
            if (sIdx == step)
               dl->AddRectFilled(ImVec2(x, o.y), ImVec2(x + cw, br.y), light ? IM_COL32(40, 90, 200, 40) : IM_COL32(120, 180, 255, 40));
         }
         const int rows = 10;
         const float rh = h / (float)rows;
         const ImU32 noteCol = light ? IM_COL32(60, 110, 220, 230) : IM_COL32(120, 190, 255, 230);
         for (const NotePatterns::PNote& pn : pat->notes)
         {
            const float x0 = o.x + pn.step * cw + 1.0f;
            const float x1 = std::min(br.x - 1.0f, x0 + std::max(2.0f, pn.len * cw - 2.0f));
            const float a = std::clamp(0.35f + pn.vel * 0.65f, 0.0f, 1.0f);
            const ImU32 c = (noteCol & 0x00FFFFFF) | ((ImU32)(a * 255.0f) << 24);
            if (pn.kind == NotePatterns::kChord)
            {
               dl->AddRectFilled(ImVec2(x0, o.y + rh * 2.0f), ImVec2(x1, br.y - rh * 2.0f), c, 2.0f);
               continue;
            }
            int row = 0;
            if (pn.kind == NotePatterns::kBass)
               row = 0;
            else if (pn.kind == NotePatterns::kSemitone)
               row = std::clamp(1 + pn.tone / 3, 1, rows - 1);
            else if (pn.kind == NotePatterns::kRandom)
               row = rows / 2;
            else
               row = std::clamp(1 + pn.tone + pn.oct * 3, 1, rows - 1);
            const float y1 = br.y - (float)row * rh - 1.0f;
            dl->AddRectFilled(ImVec2(x0, y1 - rh + 2.0f), ImVec2(x1, y1), c, 2.0f);
         }
         int ts = n->trimStart, te = n->trimEnd > 0 ? n->trimEnd : pat->steps;
         if (GridTrimHandles("##pptrim", o, w, h, pat->steps, &ts, &te))
         {
            n->trimStart = ts;
            n->trimEnd = te >= pat->steps ? 0 : te;
         }
         dl->AddRect(o, br, ScopeBorderCol(), 3.0f);
         ImGui::SetCursorScreenPos(o);
         ImGui::Dummy(ImVec2(w, h));
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(4, kKnobLarge);
         row.Knob("gate", &n->gate, 0.05f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("swing", &n->swing, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("velocity", &n->velocity, 0.0f, 2.0f, "%.2f", kKnobLarge);
         row.KnobInt("transpose", &n->transpose, -24, 24, kKnobLarge);
         row.End();
      }
      {
         static const std::vector<std::string> kSources = { "auto", "input", "global key" };
         static const std::vector<std::string> kSpeeds = { "half", "x1", "double" };
         AudioKnobRow row(5, kKnobSmall, ImGui::GetFrameHeight() + 5.0f);
         row.Dropdown("chord", kSources, std::clamp(n->source, 0, 2), [n](int i) { PushUndoCheckpoint(); n->source = i; });
         row.Dropdown("speed", kSpeeds, std::clamp(n->speed, 0, 2), [n](int i) { PushUndoCheckpoint(); n->speed = i; });
         row.KnobInt("degree", &n->degree, 1, 7);
         row.KnobInt("notes", &n->chordSize, 3, 5);
         row.KnobInt("octave", &n->octave, 0, 7);
         row.End();
      }
      AudioToggleButton("latch##patternLatch", &n->latch, 64.0f);
      ImGui::SameLine();
      AudioToggleButton("bar sync##patternSync", &n->syncBar, 76.0f);
      ImGui::SameLine();
      AudioToggleButton("thru##patternThru", &n->thru, 52.0f);
      ImGui::SameLine();
      AudioToggleButton("fit scale##patternFit", &n->fitScale, 72.0f);
      if (ImGui::IsItemHovered())
         SetAudioReadout("fit scale", ".mid patterns: snap their notes to the global scale (the degree knob then moves them in the key)");
      if (toneCount > 0)
      {
         std::string chord;
         for (int i = 0; i < toneCount; i++)
         {
            const int k = std::clamp(tones[i], 0, 127);
            chord += NoteNameList()[k % 12] + std::to_string(k / 12 - 1) + (i + 1 < toneCount ? " " : "");
         }
         ImGui::TextDisabled("chord: %s", chord.c_str());
      }
      EndAudioBody();
   }

   // ---- Note Looper (Turbo 0.57) ------------------------------------------------
   // Laid out like an MPC: a display, big REC / DUB / PLAY pads, UNDO / CLEAR /
   // ERASE, eight sequence pads (A-H), the piano roll of the active sequence
   // with trim handles, its layers, then length / quantize / switch settings.
   // Labels stay as they were so MIDI mappings keep working.
   void DrawNoteLooperBody(GraphNode& gn, NoteLooperNode* n)
   {
      using NL = NoteLooperNode;
      const NL::Mode mode = n->CurrentMode();
      const bool od = n->Overdubbing();
      const NL::Sequence& seq = n->seqs[std::clamp(n->activeSeq, 0, NL::kSeqs - 1)];
      const Transport& tr = Transport::Instance();
      const double bpb = std::max(1.0, tr.BeatsPerBar());
      const double len = n->LoopLengthBeats();
      static const char* kModes[] = { "IDLE", "ARMED", "RECORDING", "PLAYING" };
      char stat[160];
      snprintf(stat, sizeof(stat), "%c - %s%s - %s", 'A' + n->activeSeq, kModes[(int)mode], od ? " + DUB" : "",
               n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      const bool light = IsThemeLight();
      const float W = gAudioContentW;
      constexpr float gap = 4.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // display
      {
         const float h = 58.0f;
         const ImVec2 o = ImGui::GetCursorScreenPos();
         const ImVec2 br(o.x + W, o.y + h);
         const ImU32 bg = light ? IM_COL32(186, 214, 196, 255) : IM_COL32(16, 34, 30, 255);
         const ImU32 fg = light ? IM_COL32(16, 58, 38, 255) : IM_COL32(140, 245, 190, 255);
         const ImU32 dim = light ? IM_COL32(40, 90, 64, 200) : IM_COL32(90, 170, 130, 200);
         dl->AddRectFilled(o, br, bg, 6.0f);
         dl->AddRect(o, br, light ? IM_COL32(120, 150, 130, 255) : IM_COL32(40, 70, 60, 255), 6.0f, 0, 1.5f);
         ImFont* font = ImGui::GetFont();
         const float fs = ImGui::GetFontSize();
         char big[4] = { (char)('A' + n->activeSeq), 0 };
         dl->AddText(font, fs * 2.6f, ImVec2(o.x + 10.0f, o.y + (h - fs * 2.6f) * 0.5f), fg, big);
         const float tx = o.x + 10.0f + fs * 2.2f + 8.0f;
         char line[96];
         snprintf(line, sizeof(line), "%s%s", kModes[(int)mode], od ? "  +DUB" : "");
         const ImU32 modeCol = mode == NL::kRecording || mode == NL::kArmed ? IM_COL32(235, 80, 80, 255) : fg;
         dl->AddText(font, fs * 1.15f, ImVec2(tx, o.y + 6.0f), modeCol, line);
         const double ph = (mode == NL::kPlaying || mode == NL::kRecording) ? n->PlayheadBeats() : seq.TrimStart();
         snprintf(line, sizeof(line), "BAR %d.%d   %.0f BPM   %d/%d", (int)std::floor(ph / bpb) + 1,
                  (int)std::floor(std::fmod(ph, bpb)) + 1, tr.Tempo(), tr.TimeSigNumerator(), tr.TimeSigDenominator());
         dl->AddText(ImVec2(tx, o.y + 8.0f + fs * 1.2f), fg, line);
         if (len > 0.0)
         {
            const double pl = seq.lengthBeats > 0.0 ? seq.PlayLength() : len;
            if (pl < len - 1e-6)
               snprintf(line, sizeof(line), "LEN %.2f / %.2f BARS   %d LAYERS", pl / bpb, len / bpb, (int)seq.layers.size());
            else
               snprintf(line, sizeof(line), "LEN %.2f BARS   %d LAYERS", len / bpb, (int)seq.layers.size());
         }
         else
            snprintf(line, sizeof(line), "EMPTY   LEN %s", NL::LengthNames()[NL::LengthIndexFromBars(n->lengthBars)].c_str());
         dl->AddText(ImVec2(tx, o.y + 10.0f + fs * 2.2f), dim, line);
         snprintf(line, sizeof(line), "Q %s", NL::QuantizeNames()[std::clamp(n->quantize, 0, 6)].c_str());
         const ImVec2 qs = ImGui::CalcTextSize(line);
         dl->AddText(ImVec2(br.x - qs.x - 10.0f, o.y + 6.0f), dim, line);
         ImGui::Dummy(ImVec2(W, h));
      }

      auto lit = [](bool on, ImU32 col) {
         if (on)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 24, 255));
         }
         return on;
      };
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

      // transport: REC / DUB / PLAY-STOP
      {
         const float bw = (W - 2.0f * gap) / 3.0f;
         const ImVec2 sz(bw, 40.0f);
         const bool blink = mode == NL::kArmed && std::fmod(ImGui::GetTime(), 0.5) < 0.25;
         const bool recLit = lit(mode == NL::kRecording || (mode == NL::kArmed && !blink), IM_COL32(235, 70, 70, 255));
         if (ModTriggerButton("rec", sz, "REC##nlRec"))
            n->PressRec();
         if (recLit)
            ImGui::PopStyleColor(3);
         ImGui::SameLine(0.0f, gap);
         const bool odLit = lit(od, IM_COL32(245, 170, 60, 255));
         if (ModTriggerButton("overdub", sz, "OVERDUB##nlDub"))
            n->ToggleOverdub();
         if (odLit)
            ImGui::PopStyleColor(3);
         ImGui::SameLine(0.0f, gap);
         const bool playLit = lit(mode == NL::kPlaying, IM_COL32(90, 210, 120, 255));
         if (ModTriggerButton("play stop", sz, mode == NL::kIdle ? "PLAY##nlPlay" : "STOP##nlPlay"))
            n->PressPlayStop();
         if (playLit)
            ImGui::PopStyleColor(3);
      }
      // UNDO / CLEAR / ERASE
      {
         const float bw = (W - 2.0f * gap) / 3.0f;
         const ImVec2 sz(bw, 28.0f);
         if (ModTriggerButton("undo layer", sz, "UNDO##nlUndo"))
         {
            PushUndoCheckpoint();
            n->Undo();
         }
         ImGui::SameLine(0.0f, gap);
         if (ModTriggerButton("clear sequence", sz, "CLEAR##nlClear"))
         {
            PushUndoCheckpoint();
            n->Clear();
         }
         ImGui::SameLine(0.0f, gap);
         bool clicked = false;
         const bool erase = ModGateButton("ERASE##nlErase", sz, n->Erasing(), false, true, nullptr, clicked);
         if (erase && !n->Erasing())
            PushUndoCheckpoint();
         n->SetErase(erase);
         if (ImGui::IsItemHovered())
            SetAudioReadout("erase", "hold, and hold the keys to remove: their notes go as the playhead passes");
      }

      // sequence pads A-H (4 x 2)
      {
         const float pw = (W - 3.0f * gap) / 4.0f;
         const ImVec2 sz(pw, 34.0f);
         for (int i = 0; i < NL::kSeqs; i++)
         {
            if (i % 4 != 0)
               ImGui::SameLine(0.0f, gap);
            char label[16], caption[48];
            snprintf(label, sizeof(label), "sequence %c", 'A' + i);
            const bool sel = i == n->activeSeq;
            const bool has = n->seqs[i].lengthBeats > 0.0;
            if (has)
               snprintf(caption, sizeof(caption), "%c  %.0f##nlSeq%d", 'A' + i,
                        std::ceil(n->seqs[i].PlayLength() / bpb - 1e-6), i);
            else
               snprintf(caption, sizeof(caption), "%c##nlSeq%d", 'A' + i, i);
            int pushed = 0;
            if (sel)
            {
               ImGui::PushStyleColor(ImGuiCol_Button, light ? IM_COL32(60, 120, 230, 255) : IM_COL32(90, 150, 255, 255));
               ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
               pushed = 2;
            }
            else if (has)
            {
               ImGui::PushStyleColor(ImGuiCol_Button, light ? IM_COL32(170, 190, 225, 255) : IM_COL32(48, 62, 92, 255));
               pushed = 1;
            }
            else
            {
               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
               pushed = 1;
            }
            if (ModTriggerButton(label, sz, caption))
               n->SelectSeq(i);
            ImGui::PopStyleColor(pushed);
            if (ImGui::IsItemHovered())
               SetAudioReadout(label, has ? "click to switch (see 'switch')" : "empty: switch, then REC");
         }
      }
      ImGui::PopStyleVar();

      // piano roll with trim handles (1/16 grid)
      {
         const float w = gAudioBodyW, h = 96.0f;
         const ImVec2 o = ImGui::GetCursorScreenPos();
         const ImVec2 br(o.x + w, o.y + h);
         dl->AddRectFilled(o, br, ScopeBgCol(), 4.0f);
         if (len > 0.0)
         {
            int lo = 127, hi = 0;
            for (const NL::Layer& l : seq.layers)
               for (const NL::Note& note : l.notes)
               {
                  lo = std::min(lo, note.key);
                  hi = std::max(hi, note.key);
               }
            if (lo > hi)
            {
               lo = 48;
               hi = 72;
            }
            lo -= 2;
            hi += 2;
            const float kh = h / (float)std::max(1, hi - lo + 1);
            for (double b = 0.0; b < len; b += 1.0)
            {
               const float x = o.x + (float)(b / len) * w;
               dl->AddLine(ImVec2(x, o.y), ImVec2(x, br.y),
                           std::fmod(b, bpb) < 1e-6 ? ScopeBorderCol() : ScopeGridCol(), 1.0f);
            }
            for (int li = 0; li < (int)seq.layers.size(); li++)
            {
               const NL::Layer& l = seq.layers[li];
               const ImColor hue = ImColor::HSV(std::fmod(0.58f + (float)li * 0.13f, 1.0f), 0.55f, light ? 0.7f : 0.95f);
               const ImU32 c = IM_COL32((int)(hue.Value.x * 255), (int)(hue.Value.y * 255), (int)(hue.Value.z * 255),
                                        l.mute ? 50 : 220);
               for (const NL::Note& note : l.notes)
               {
                  const float x0 = o.x + (float)(note.beat / len) * w;
                  const float x1 = std::min(br.x, x0 + std::max(2.0f, (float)(note.dur / len) * w));
                  const float y = br.y - (float)(note.key - lo + 1) * kh;
                  dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + std::max(2.0f, kh - 1.0f)), c, 1.5f);
               }
            }
            if (mode == NL::kPlaying || mode == NL::kRecording)
            {
               const double ph = n->PlayheadBeats();
               const float x = o.x + (float)std::clamp(ph / len, 0.0, 1.0) * w;
               dl->AddLine(ImVec2(x, o.y), ImVec2(x, br.y), mode == NL::kRecording ? IM_COL32(235, 70, 70, 255)
                                                                                   : IM_COL32(255, 255, 255, 200), 1.5f);
            }
            if (mode != NL::kRecording && seq.lengthBeats > 0.0)
            {
               const int cells = std::max(1, (int)std::lround(seq.lengthBeats * 4.0));
               int ts = (int)std::lround(seq.TrimStart() * 4.0);
               int te = (int)std::lround(seq.TrimEnd() * 4.0);
               if (GridTrimHandles("##nltrim", o, w, h, cells, &ts, &te, 4))
                  n->SetTrim(ts / 4.0, te / 4.0);
            }
         }
         else
         {
            const char* msg = mode == NL::kRecording ? "recording..." : "empty: press REC";
            const ImVec2 tsz = ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2(o.x + (w - tsz.x) * 0.5f, o.y + (h - tsz.y) * 0.5f), ScopeTextCol(), msg);
         }
         dl->AddRect(o, br, ScopeBorderCol(), 3.0f);
         ImGui::SetCursorScreenPos(o);
         ImGui::Dummy(ImVec2(w, h));
      }

      // layers
      if (!seq.layers.empty())
      {
         for (int li = 0; li < (int)seq.layers.size(); li++)
         {
            if (li > 0)
               ImGui::SameLine();
            char lbl[32];
            snprintf(lbl, sizeof(lbl), "%d (%d)##nlLayer%d", li + 1, (int)seq.layers[li].notes.size(), li);
            const bool muted = seq.layers[li].mute;
            if (muted)
               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::SmallButton(lbl))
            {
               PushUndoCheckpoint();
               n->SetLayerMute(li, !muted);
            }
            if (muted)
               ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
               SetAudioReadout("layer", muted ? "muted: click to unmute" : "click to mute");
         }
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(5, kKnobSmall, ImGui::GetFrameHeight() + 5.0f);
         row.Dropdown("length", NL::LengthNames(), NL::LengthIndexFromBars(n->lengthBars),
                      [n](int i) { PushUndoCheckpoint(); n->lengthBars = NL::LengthBarsFromIndex(i); });
         row.Dropdown("quantize", NL::QuantizeNames(), std::clamp(n->quantize, 0, 6),
                      [n](int i) { PushUndoCheckpoint(); n->quantize = i; });
         row.Dropdown("switch", NL::SwitchNames(), std::clamp(n->switchMode, 0, 2),
                      [n](int i) { PushUndoCheckpoint(); n->switchMode = i; });
         row.Knob("strength", &n->strength, 0.0f, 1.0f, "%.2f");
         row.Knob("swing", &n->swing, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      AudioToggleButton("count-in##nlCount", &n->countIn, 72.0f);
      ImGui::SameLine();
      AudioToggleButton("thru##nlThru", &n->thru, 52.0f);
      ImGui::SameLine();
      if (ImGui::Button("save to bank##nlBank"))
      {
         std::string err;
         const std::string path = n->SaveToBank(err);
         SetAudioReadout("bank", path.empty() ? err.c_str() : path.c_str());
      }
      if (ImGui::IsItemHovered())
         SetAudioReadout("bank", "writes the sequence (as trimmed) as .mid into the Pattern Player's folder (Note Looper)");
      ImGui::SameLine();
      if (ImGui::Button("import .mid##nlImport"))
         StartNodeFileDialog(n, Platform::OpenMidiDialog, [](auto* m, const std::string& path) {
            PushUndoCheckpoint();
            std::string err;
            if (!m->ImportMidi(path, err))
               RuntimeLog::Write("note looper: %s", err.c_str());
            gPatchDirty = true;
         });
      EndAudioBody();
   }

   // ---- MIDI Out (Turbo 0.57, ported from upstream) -------------------------
   // Device (refreshed every 2 s), channel, offset, clock master, four CC rows
   // with modulatable values, panic. Labels are fixed so cables stay bound.
   void DrawMidiOutBody(GraphNode& gn, MidiOutNode* n)
   {
      if (ImGui::GetTime() - n->devicesListedAt > 2.0)
      {
         n->RefreshDeviceList();
         n->devicesListedAt = ImGui::GetTime();
      }
      char stat[160];
      if (!n->DeviceError().empty())
         snprintf(stat, sizeof(stat), "%s", n->DeviceError().c_str());
      else if (n->DeviceOpen())
         snprintf(stat, sizeof(stat), "%s - ch %d - sent %llu", n->device.c_str(), n->channel, n->SentCount());
      else
         snprintf(stat, sizeof(stat), "no device (loopMIDI makes a virtual port)");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      std::vector<std::string> options { "none" };
      int current = 0;
      for (const std::string& d : n->Devices())
      {
         options.push_back(d);
         if (d == n->device)
            current = (int)options.size() - 1;
      }
      // A saved device that is not plugged in stays selectable so the choice survives a restart.
      if (!n->device.empty() && current == 0)
      {
         options.push_back(n->device + " (not connected)");
         current = (int)options.size() - 1;
      }
      DropdownButton("device##midiOutDevice", options, current, [n](int i) {
         PushUndoCheckpoint();
         if (i <= 0)
            n->device.clear();
         else if ((size_t)i <= n->Devices().size()) // the "(not connected)" row keeps the saved name
            n->device = n->Devices()[i - 1];
      }, 260.0f, false);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(4, kKnobLarge);
         row.KnobInt("channel", &n->channel, 1, 16, kKnobLarge);
         row.Knob("offset", &n->offsetMs, -50.0f, 50.0f, "%.1fms", kKnobLarge);
         row.Checkbox("clock##midiOutClock", &n->clock);
         row.Skip();
         row.End();
      }
      static const char* const kOnLbl[MidiOutNode::kCcRows] = { "cc1##midiOutCc1", "cc2##midiOutCc2", "cc3##midiOutCc3", "cc4##midiOutCc4" };
      static const char* const kNumLbl[MidiOutNode::kCcRows] = { "cc1 #", "cc2 #", "cc3 #", "cc4 #" };
      static const char* const kValLbl[MidiOutNode::kCcRows] = { "cc1 value", "cc2 value", "cc3 value", "cc4 value" };
      for (int i = 0; i < MidiOutNode::kCcRows; i++)
      {
         AudioKnobRow row(4, kKnobSmall);
         row.Checkbox(kOnLbl[i], &n->ccOn[i]);
         row.KnobInt(kNumLbl[i], &n->ccNum[i], 0, 127);
         row.Knob(kValLbl[i], &n->ccVal[i], 0.0f, 1.0f, "%.2f");
         row.Skip();
         row.End();
      }
      if (ImGui::Button("panic##midiOutPanic", ImVec2(96.0f, 0.0f)))
         n->Panic();
      EndAudioBody();
   }

   // ---- Velocity to CV ------------------------------------------------------
   void DrawVelocityToCVBody(GraphNode& gn, VelocityToCVNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "velocity -> %.2f", n->Value01());
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      AudioKnobRow row(2);
      row.Knob("range lo", &n->rangeLow, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.Knob("range hi", &n->rangeHigh, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();
      EndAudioBody();
   }

   // ---- Note Switcher -------------------------------------------------------
   void DrawNoteSwitcherBody(GraphNode& gn, NoteSwitcherNode* n)
   {
      using NS = NoteSwitcherNode;
      const int active = n->ActiveSlot();
      const int pending = n->PendingSlot();
      const int shown = n->VisibleSlots();
      char stat[80];
      if (n->manual && pending >= 0)
         snprintf(stat, sizeof(stat), "manual - slot %d > %d", active + 1, pending + 1);
      else if (n->manual)
         snprintf(stat, sizeof(stat), "manual - slot %d", active + 1);
      else if (n->rateMode == 0)
         snprintf(stat, sizeof(stat), "every %s - slot %d",
                  MusicTime::RateDivisionName(NearestRateDivision(n->rateBeats)), active + 1);
      else
         snprintf(stat, sizeof(stat), "every %.2fs - slot %d", n->rateSeconds, active + 1);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      {
         // active slot lit, pending (Turbo 0.50) amber, connected dim,
         // unconnected darkest; only the visible pins get a dot.
         const float w = gAudioBodyW;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float dia = 14.0f;
         const float gap = (w - (float)shown * dia) / (float)(shown + 1);
         for (int i = 0; i < shown; i++)
         {
            const float cx = origin.x + gap * (float)(i + 1) + dia * (float)i + dia * 0.5f;
            ImU32 col = IM_COL32(50, 53, 64, 255);
            if (i == active)
               col = IM_COL32(120, 200, 255, 255);
            else if (i == pending)
               col = IM_COL32(255, 170, 60, 255);
            else if (n->noteInputs[i].IsConnected())
               col = IM_COL32(70, 120, 170, 255);
            dl->AddCircleFilled(ImVec2(cx, origin.y + dia * 0.5f), dia * 0.5f, col);
         }
         ImGui::Dummy(ImVec2(w, dia + 6.0f));
      }
      {
         AudioKnobRow row(2);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.End();
      }
      AudioToggleButton("manual", &n->manual, 64.0f);
      if (n->manual)
         ModSliderInt("slot", &n->manualSlot, 0, shown - 1, gAudioBodyW);

      // Turbo 0.50: per-slot triggers (discrete, keyed by label "slot N", so
      // Performance Mode / MIDI learn can pick a slot) plus the pin count and
      // the switch grid. A slot trigger also turns manual on.
      {
         const int perRow = 4;
         const float gapX = 4.0f;
         const float cellW = (gAudioContentW - gapX * (float)(perRow - 1)) / (float)perRow;
         for (int i = 0; i < shown; i++)
         {
            if (i % perRow != 0)
               ImGui::SameLine(0.0f, gapX);
            char label[16], caption[24];
            snprintf(label, sizeof(label), "slot %d", i + 1);
            snprintf(caption, sizeof(caption), "%d##nsSlotBtn%d", i + 1, i);
            if (ModTriggerButton(label, ImVec2(cellW, 0.0f), caption))
            {
               if (!n->manual || n->manualSlot != i)
                  PushUndoCheckpoint();
               n->manual = true;
               n->manualSlot = i;
               gPatchDirty = true;
            }
         }
      }
      {
         static const std::vector<std::string> kInputCounts = { "2", "3", "4", "5", "6", "7", "8" };
         AudioKnobRow row(2, kKnobSmall, ImGui::GetFrameHeight() + 5.0f);
         row.Dropdown("inputs", kInputCounts, std::clamp(n->inputs, 2, NS::kSlots) - 2,
                      [n](int i) {
                         if (!gApplyingDiscreteCv)
                            PushUndoCheckpoint();
                         n->inputs = std::clamp(i + 2, 2, NS::kSlots);
                         gPatchDirty = true;
                      });
         row.Dropdown("switch q", NS::SwitchQuantNames(),
                      std::clamp(n->switchQuant, 0, NS::kNumSwitchQuants - 1),
                      [n](int i) {
                         if (!gApplyingDiscreteCv)
                            PushUndoCheckpoint();
                         n->switchQuant = i;
                         gPatchDirty = true;
                      });
         row.End();
      }
      EndAudioBody();
   }
