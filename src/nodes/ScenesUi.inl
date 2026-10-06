// ===========================================================================
// Turbo 0.50: Scenes node UI and helpers (see nodes/ScenesNode.h).
// Included once inside main.cpp's anonymous namespace, after PerfPanel.inl
// (the RPC tool in RpcTurboTools.inl uses the helpers below).
// ===========================================================================

// What each output of a Scenes node is cabled to: the first destination param
// (nullptr when uncabled), how many cables leave it, and whether that first
// binding remaps the value (range mapper / bipolar), which turns auto steps off.
struct ScenesTarget
{
   const ParamRef* param = nullptr;
   int nodeIndex = -1;
   int count = 0;
   bool remapped = false;
};

void ScenesFindTargets(int nodeIndex, ScenesTarget out[ScenesNode::kMax])
{
   for (const auto& link : Modulation::Instance().Links())
   {
      const Modulation::Source& src = link.second;
      if (src.nodeIndex != nodeIndex || src.outputIndex < 0 || src.outputIndex >= ScenesNode::kMax)
         continue;
      ScenesTarget& t = out[src.outputIndex];
      t.count++;
      if (t.param == nullptr)
      {
         t.param = Modulation::Instance().KnownParam(link.first.first, link.first.second);
         t.nodeIndex = link.first.first;
         t.remapped = src.HasRange() || src.polarity != Modulation::Source::kAbsolute;
      }
   }
}

// Number of choices of the cabled param: dropdown options, 2 for a checkbox
// or trigger, (max - min) / step + 1 for other discrete params, else 0.
int ScenesAutoSteps(const ScenesTarget& t)
{
   if (t.param == nullptr || t.remapped)
      return 0;
   const ParamRef& p = *t.param;
   if (p.isEnum && p.enumOptions.size() >= 2)
      return (int)p.enumOptions.size();
   if (p.isBool || p.momentary)
      return 2;
   if (p.step > 0.0f)
   {
      const float span = std::fabs(p.maxValue - p.minValue) / p.step;
      if (span >= 1.0f && span <= 127.0f)
         return (int)std::lround(span) + 1;
   }
   return 0;
}

std::string ScenesParamName(const ParamRef* p)
{
   if (p == nullptr)
      return std::string();
   std::string name = p->name;
   const size_t hidden = name.find("##");
   if (hidden != std::string::npos)
      name = name.substr(0, hidden);
   return name;
}

std::string ScenesTargetText(const ScenesTarget& t)
{
   if (t.param == nullptr)
      return "not cabled";
   GraphNode* gn = FindNodeByIndex(t.nodeIndex);
   std::string s = (gn ? NodeTitleWithInstance(*gn) : std::string("node")) + " / " + ScenesParamName(t.param);
   if (t.count > 1)
      s += " (+" + std::to_string(t.count - 1) + " more)";
   return s;
}

// Short target for the column header: the param name, "+N" for more cables.
std::string ScenesTargetShort(const ScenesTarget& t)
{
   if (t.param == nullptr)
      return "no cable";
   std::string s = ScenesParamName(t.param);
   if (t.count > 1)
      s += " +" + std::to_string(t.count - 1);
   return s;
}

// Resolves the auto choice counts from the cables; called every frame by the
// body and before the RPC tool reads or writes cells.
void ScenesResolve(ScenesNode* n, int nodeIndex, ScenesTarget targets[ScenesNode::kMax])
{
   ScenesFindTargets(nodeIndex, targets);
   for (int o = 0; o < ScenesNode::kMax; o++)
      n->SetResolvedSteps(o, ScenesAutoSteps(targets[o]));
}

std::string ScenesSceneName(const ScenesNode* n, int s)
{
   if (s < 0)
      return "off";
   return n->sceneName[s].empty() ? std::to_string(s + 1) : n->sceneName[s];
}

// Cell caption: ON / off, the dropdown option or 1-based choice, the 0..1 level.
std::string ScenesCellText(ScenesNode* n, int s, int o, const ScenesTarget& t)
{
   char buf[48];
   switch (n->mode[o])
   {
      case ScenesNode::kChoice:
      {
         const int idx = (int)n->CellUser(s, o);
         if (t.param != nullptr && n->ChoiceAutoResolved(o) && t.param->isEnum && idx < (int)t.param->enumOptions.size())
            return t.param->enumOptions[(size_t)idx];
         snprintf(buf, sizeof(buf), "%d", idx + 1);
         return buf;
      }
      case ScenesNode::kLevel:
         snprintf(buf, sizeof(buf), "%.2f", n->CellUser(s, o));
         return buf;
      default:
         return n->CellOn(s, o) ? "ON" : "off";
   }
}

void DrawScenesBody(ScenesNode* n, int nodeIndex)
{
   n->Tick(ImGui::GetFrameCount());
   ScenesTarget targets[ScenesNode::kMax];
   ScenesResolve(n, nodeIndex, targets);

   const int scenes = n->Scenes(), outputs = n->Outputs();
   const float rowHeadW = 92.0f, cellW = 60.0f, cellH = 22.0f, gap = 2.0f;
   const float lineH = ImGui::GetTextLineHeight();
   const float headH = lineH * 3.0f + 4.0f;
   const float gridW = rowHeadW + 4.0f + outputs * (cellW + gap);
   const bool isLight = IsThemeLight();
   ImDrawList* dl = ImGui::GetWindowDrawList();
   const ImU32 textCol = isLight ? IM_COL32(35, 40, 52, 255) : IM_COL32(222, 228, 240, 255);
   const ImU32 dimText = isLight ? IM_COL32(100, 108, 125, 255) : IM_COL32(140, 146, 165, 255);
   const ImU32 cellBg = isLight ? IM_COL32(222, 226, 236, 255) : IM_COL32(32, 35, 46, 255);
   const ImU32 cellBorder = isLight ? IM_COL32(180, 190, 205, 200) : IM_COL32(52, 56, 70, 200);
   // ON cells: bright on the playing row (that is what the outputs send),
   // muted on the other rows.
   const ImU32 onLive = isLight ? IM_COL32(34, 170, 80, 255) : IM_COL32(56, 200, 100, 255);
   const ImU32 onIdle = isLight ? IM_COL32(150, 205, 165, 255) : IM_COL32(40, 92, 60, 255);
   const ImU32 levelFill = isLight ? IM_COL32(90, 150, 240, 255) : IM_COL32(80, 130, 220, 255);
   const ImU32 pulseFill = IM_COL32(235, 150, 60, 255);
   const ImU32 curRowBg = isLight ? IM_COL32(34, 197, 94, 60) : IM_COL32(74, 222, 128, 40);
   const ImU32 pendingCol = IM_COL32(235, 150, 60, 220);
   const double blink = std::fmod(ImGui::GetTime() * 3.0, 1.0);

   // One-line help, wrapped to the grid.
   ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + gridW);
   ImGui::TextDisabled("Press a scene: its row turns on, everything else follows the row. Press it again: off row.");
   ImGui::PopTextWrapPos();

   const ImVec2 origin = ImGui::GetCursorScreenPos();
   const float cellsX = origin.x + rowHeadW + 4.0f;

   // Column headers: label, mode, target (full text in the tooltip).
   for (int o = 0; o < outputs; o++)
   {
      const ImVec2 tl(cellsX + o * (cellW + gap), origin.y), br(tl.x + cellW, tl.y + headH);
      ImGui::SetCursorScreenPos(tl);
      ImGui::PushID(o + 9100);
      ImGui::InvisibleButton("##scenesHead", ImVec2(cellW, headH));
      if (ImGui::IsItemHovered())
      {
         std::string what = ScenesNode::ModeName(n->mode[o]);
         switch (n->mode[o])
         {
            case ScenesNode::kChoice:
               what += ": a stepped value, " + std::to_string(n->ChoiceCount(o)) + " choices" +
                       (n->steps[o] < 0 ? (n->ChoiceAutoResolved(o) ? " (from the cable)" : " (auto, nothing discrete cabled)") : "");
               break;
            case ScenesNode::kLevel: what += ": any value 0..1"; break;
            case ScenesNode::kPulse: what += ": a short trigger when a scene with this cell ON starts"; break;
            default: what += ": 1 while the playing row's cell is ON, else 0"; break;
         }
         ImGui::SetTooltip("output %d \"%s\"\n%s\n-> %s\n(mode and name: params panel)", o + 1, n->OutputLabel(o),
                           what.c_str(), ScenesTargetText(targets[o]).c_str());
      }
      ImGui::PopID();
      const ImU32 modeCol = n->mode[o] == ScenesNode::kPulse ? IM_COL32(235, 150, 60, 70)
                          : n->mode[o] == ScenesNode::kLevel ? IM_COL32(90, 150, 240, 60)
                          : n->mode[o] == ScenesNode::kChoice ? IM_COL32(160, 110, 230, 60)
                                                              : IM_COL32(56, 200, 100, 55);
      dl->AddRectFilled(tl, br, modeCol, 3.0f);
      dl->PushClipRect(tl, br, true);
      const std::string modeTxt = n->mode[o] == ScenesNode::kChoice
                                     ? "choice " + std::to_string(n->ChoiceCount(o))
                                     : std::string(ScenesNode::ModeName(n->mode[o]));
      const std::string tgt = ScenesTargetShort(targets[o]);
      const char* lines[3] = { n->OutputLabel(o), modeTxt.c_str(), tgt.c_str() };
      for (int l = 0; l < 3; l++)
      {
         const ImVec2 ts = ImGui::CalcTextSize(lines[l]);
         dl->AddText(ImVec2(tl.x + std::max(2.0f, (cellW - ts.x) * 0.5f), tl.y + 2.0f + l * lineH),
                     l == 0 ? textCol : dimText, lines[l]);
      }
      dl->PopClipRect();
   }

   // Rows: the off row on top (the base state), then one row per scene.
   // Each row head is a mappable trigger: "all off", "scene 1".."scene 8".
   const int current = n->Current();
   for (int s = ScenesNode::kOff; s < scenes; s++)
   {
      const int rowIdx = s + 1; // 0 = off row
      const float y = origin.y + headH + gap + rowIdx * (cellH + gap) + (s >= 0 ? 4.0f : 0.0f);
      const bool isCur = s == current;
      const bool isPending = n->HasPending() && s == n->Pending();
      if (isCur)
         dl->AddRectFilled(ImVec2(origin.x - 2.0f, y - 1.0f), ImVec2(origin.x + gridW, y + cellH + 1.0f), curRowBg, 4.0f);
      if (s == 0)
         dl->AddLine(ImVec2(origin.x, y - 3.0f), ImVec2(origin.x + gridW, y - 3.0f), cellBorder, 1.0f);

      ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
      char label[16], caption[64];
      if (s < 0)
      {
         snprintf(label, sizeof(label), "all off");
         snprintf(caption, sizeof(caption), "OFF##scenesOff");
      }
      else
      {
         snprintf(label, sizeof(label), "scene %d", s + 1);
         snprintf(caption, sizeof(caption), "%s##scenesGo%d", ScenesSceneName(n, s).c_str(), s);
      }
      const bool colored = isCur || (isPending && blink < 0.5);
      if (colored)
         ImGui::PushStyleColor(ImGuiCol_Button, isCur ? ImVec4(0.18f, 0.62f, 0.32f, 1.0f) : ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
      int paramIndex = -1;
      const bool pressed = ModTriggerButton(label, ImVec2(rowHeadW, cellH), caption, &paramIndex);
      if (colored)
         ImGui::PopStyleColor();
      if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
         ImGui::SetTooltip(s < 0 ? "all off: go to the off row (every on/off output OFF)"
                                 : n->pressAgainOff ? "press: this row turns on, the rest follows it\npress again: off row"
                                                    : "press: this row turns on, the rest follows it\npress again: restart (pulses fire)");
      n->SetSceneParam(s, paramIndex);
      if (pressed)
      {
         if (s < 0)
            n->AllOff();
         else
            n->Press(s);
      }

      for (int o = 0; o < outputs; o++)
      {
         const ImVec2 tl(cellsX + o * (cellW + gap), y), br(tl.x + cellW, y + cellH);
         ImGui::SetCursorScreenPos(tl);
         ImGui::PushID((s + 1) * 16 + o + 9200);
         const bool clicked = ImGui::InvisibleButton("##scenesCell", ImVec2(cellW, cellH));
         const bool hovered = ImGui::IsItemHovered();
         const int mode = n->mode[o];
         if (mode == ScenesNode::kLevel)
         {
            // Drag sideways, double-click flips 0 / 1.
            if (ImGui::IsItemActivated())
               PushUndoCheckpoint();
            if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0.0f)
            {
               const float speed = ImGui::GetIO().KeyShift ? 0.002f : 0.01f;
               n->SetCellUser(s, o, n->CellUser(s, o) + ImGui::GetIO().MouseDelta.x * speed);
               gPatchDirty = true;
            }
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               n->SetCellUser(s, o, n->CellUser(s, o) >= 0.5f ? 0.0f : 1.0f);
               gPatchDirty = true;
            }
         }
         else if (clicked)
         {
            PushUndoCheckpoint();
            if (mode == ScenesNode::kChoice)
            {
               // Click steps forward, Shift+click back (wrapping).
               const int steps = n->ChoiceCount(o);
               const int dir = ImGui::GetIO().KeyShift ? -1 : 1;
               n->SetCellUser(s, o, (float)((((int)n->CellUser(s, o) + dir) % steps + steps) % steps));
            }
            else
               n->SetCellUser(s, o, n->CellOn(s, o) ? 0.0f : 1.0f);
            gPatchDirty = true;
         }
         if (hovered)
            ImGui::SetTooltip("%s", mode == ScenesNode::kPulse ? "pulse: click ON / off (ON fires when this row starts)" :
                                    mode == ScenesNode::kChoice ? "choice: click next, Shift+click previous" :
                                    mode == ScenesNode::kLevel ? "level: drag sideways (Shift: fine), double-click 0 / 1" :
                                                                 "on/off: click ON / off");
         ImGui::PopID();

         dl->AddRectFilled(tl, br, cellBg, 3.0f);
         const bool on = n->CellOn(s, o);
         if (mode == ScenesNode::kPulse)
         {
            const ImVec2 c((tl.x + br.x) * 0.5f, (tl.y + br.y) * 0.5f);
            if (on)
               dl->AddCircleFilled(c, 6.0f, (isCur && n->PulseHigh(o)) ? IM_COL32(255, 230, 80, 255) : pulseFill);
            else
               dl->AddCircle(c, 5.0f, dimText, 0, 1.0f);
         }
         else
         {
            if (mode == ScenesNode::kOnOff && on)
               dl->AddRectFilled(tl, br, isCur ? onLive : onIdle, 3.0f);
            else if (mode == ScenesNode::kLevel)
               dl->AddRectFilled(tl, ImVec2(tl.x + cellW * n->CellUser(s, o), br.y), levelFill, 3.0f);
            const std::string txt = ScenesCellText(n, s, o, targets[o]);
            const bool dim = mode == ScenesNode::kOnOff ? !on : (mode == ScenesNode::kChoice && !isCur);
            dl->PushClipRect(tl, br, true);
            const ImVec2 ts = ImGui::CalcTextSize(txt.c_str());
            dl->AddText(ImVec2(tl.x + std::max(2.0f, (cellW - ts.x) * 0.5f), tl.y + (cellH - ts.y) * 0.5f),
                        mode == ScenesNode::kOnOff && on ? IM_COL32(255, 255, 255, 255) : dim ? dimText : textCol,
                        txt.c_str());
            dl->PopClipRect();
         }
         dl->AddRect(tl, br, isPending ? pendingCol : cellBorder, 3.0f);
      }
   }

   // Under the grid: prev / next, the scene selector and the status line.
   ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + headH + gap + (scenes + 1) * (cellH + gap) + 10.0f));
   if (ModTriggerButton("prev scene", ImVec2(44.0f, 0.0f), "<##scenesPrev"))
      n->Step(-1);
   ImGui::SameLine(0.0f, 4.0f);
   if (ModTriggerButton("next scene", ImVec2(44.0f, 0.0f), ">##scenesNext"))
      n->Step(1);
   ImGui::SameLine(0.0f, 8.0f);
   // Option 0 is off, then the scenes.
   std::vector<std::string> names;
   names.push_back("off");
   for (int s = 0; s < scenes; s++)
      names.push_back(ScenesSceneName(n, s));
   const int shown = n->HasPending() ? n->Pending() : current;
   DropdownButton("scene", names, std::min(shown + 1, scenes),
                  [n](int i) { n->Request(i - 1, false); }, 120.0f, true);

   char status[160];
   if (n->HasPending())
   {
      const double left = std::max(0.0, n->PendingBeat() - Transport::Instance().Beats());
      snprintf(status, sizeof(status), "playing %s   next %s in %.1f beats", ScenesSceneName(n, current).c_str(),
               ScenesSceneName(n, n->Pending()).c_str(), left);
   }
   else
      snprintf(status, sizeof(status), "playing %s   (%s%s)", ScenesSceneName(n, current).c_str(),
               ScenesNode::QuantName(n->quantize), n->pressAgainOff ? ", press again = off" : "");
   ImGui::TextDisabled("%s", status);
}

void DrawScenesParams(ScenesNode* n)
{
   int scenes = n->scenes, outputs = n->outputs;
   ImGui::SetNextItemWidth(kParamWidth - 60.0f);
   if (ImGui::SliderInt("scenes##scenesCount", &scenes, 1, ScenesNode::kMax))
      n->scenes = scenes, gPatchDirty = true;
   if (ImGui::IsItemActivated())
      PushUndoCheckpoint();
   ImGui::SetNextItemWidth(kParamWidth - 60.0f);
   if (ImGui::SliderInt("outputs##scenesOuts", &outputs, 1, ScenesNode::kMax))
      n->outputs = outputs, gPatchDirty = true;
   if (ImGui::IsItemActivated())
      PushUndoCheckpoint();
   static std::vector<std::string> kQ;
   if (kQ.empty())
      for (int q = 0; q < ScenesNode::kQuantCount; q++)
         kQ.push_back(ScenesNode::QuantName(q));
   DropdownButton("q", kQ, n->quantize,
                  [n](int i) { PushUndoCheckpoint(); n->quantize = i; gPatchDirty = true; }, kParamWidth, false);
   bool pressOff = n->pressAgainOff;
   if (ImGui::Checkbox("press again = off##scenesPressOff", &pressOff))
   {
      PushUndoCheckpoint();
      n->pressAgainOff = pressOff;
      gPatchDirty = true;
   }
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("on: pressing the playing scene goes to the off row\noff: it restarts the scene (pulses fire again)");

   ImGui::TextDisabled("outputs: name / mode / choices");
   for (int o = 0; o < n->Outputs(); o++)
   {
      ImGui::PushID(o + 9400);
      char buf[32];
      snprintf(buf, sizeof(buf), "%s", n->label[o].c_str());
      ImGui::SetNextItemWidth(64.0f);
      char hint[8];
      snprintf(hint, sizeof(hint), "%d", o + 1);
      if (ImGui::InputTextWithHint("##scenesLabel", hint, buf, sizeof(buf)))
      {
         n->label[o] = buf;
         gPatchDirty = true;
      }
      ImGui::SameLine(0.0f, 4.0f);
      // Cycles on/off -> choice -> level -> pulse (Shift goes back).
      char modeCap[32];
      snprintf(modeCap, sizeof(modeCap), "%s##scenesMode", ScenesNode::ModeName(n->mode[o]));
      if (ImGui::Button(modeCap, ImVec2(54.0f, 0.0f)))
      {
         PushUndoCheckpoint();
         static const int kOrder[] = { ScenesNode::kOnOff, ScenesNode::kChoice, ScenesNode::kLevel, ScenesNode::kPulse };
         int at = 0;
         for (int i = 0; i < 4; i++)
            if (kOrder[i] == n->mode[o])
               at = i;
         at = ((at + (ImGui::GetIO().KeyShift ? -1 : 1)) % 4 + 4) % 4;
         n->mode[o] = kOrder[at];
         gPatchDirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("on/off: 1 or 0 (mutes, toggles, enable switches)\n"
                           "choice: a stepped value (a switcher slot, a drum part, a dropdown)\n"
                           "level: any value 0..1\n"
                           "pulse: a short trigger when a scene with this cell ON starts (a restart)\n"
                           "click: next mode, Shift+click: previous");
      if (n->mode[o] == ScenesNode::kChoice)
      {
         ImGui::SameLine(0.0f, 4.0f);
         // Cycles auto -> 2..16 (Shift goes back).
         char cap[32];
         if (n->steps[o] < 0)
            snprintf(cap, sizeof(cap), "auto %d##scenesSteps", n->ChoiceCount(o));
         else
            snprintf(cap, sizeof(cap), "%d##scenesSteps", n->steps[o]);
         if (ImGui::Button(cap, ImVec2(54.0f, 0.0f)))
         {
            PushUndoCheckpoint();
            static const int kCycle[] = { -1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16 };
            constexpr int kN = (int)(sizeof(kCycle) / sizeof(kCycle[0]));
            int at = 0;
            for (int i = 0; i < kN; i++)
               if (kCycle[i] == n->steps[o])
                  at = i;
            at = ((at + (ImGui::GetIO().KeyShift ? -1 : 1)) % kN + kN) % kN;
            n->steps[o] = kCycle[at];
            gPatchDirty = true;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("auto: as many choices as the cabled param has (dropdown, part, switcher slot...)\n"
                              "N: N evenly spaced values (Shift+click goes back)");
      }
      ImGui::PopID();
   }
   ImGui::TextDisabled("scene names");
   for (int s = 0; s < n->Scenes(); s++)
   {
      ImGui::PushID(s + 9500);
      char buf[48], hint[16];
      snprintf(buf, sizeof(buf), "%s", n->sceneName[s].c_str());
      snprintf(hint, sizeof(hint), "scene %d", s + 1);
      ImGui::SetNextItemWidth(kParamWidth - 20.0f);
      if (ImGui::InputTextWithHint("##scenesName", hint, buf, sizeof(buf)))
      {
         n->sceneName[s] = buf;
         gPatchDirty = true;
      }
      ImGui::PopID();
   }
}
