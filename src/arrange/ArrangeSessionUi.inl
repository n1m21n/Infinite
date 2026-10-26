// Part of src/main.cpp (included at the end of ArrangeUi.inl, after the
// Session engine): the Session view grid (Turbo 0.72). Upstream's Session
// panel (src/app/panels/ArrangeSessionGrid.cpp) drawn on this fork's toolbar
// design system: tracks are columns, scenes are rows, each cell is a slot
// clip that loops on the launch grid. Click a clip to launch it (again to
// stop), the row's play button launches the scene, the square under a column
// stops that track, double-click an empty cell to give it a source node,
// right-click a clip for its launch settings. Turbo 0.72: drag a clip to
// another cell (Ctrl: a copy), the selected clip shows in the inspector, and
// a clip or a scene can be sent to the timeline at the playhead.

namespace ArrSess
{
   constexpr float kSceneColW = 132.0f;
   constexpr float kColW = 138.0f;
   constexpr float kRowH = 34.0f;
   constexpr float kHeadH = 26.0f;
   constexpr int kMinRows = 8;

   // A slot clip's length when it is first given a source: a file's own
   // length, else four bars.
   Arrange::Tick DefaultSlotLength(uint64_t srcUid)
   {
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      if (GraphNode* gn = FindNodeByUid(srcUid))
      {
         if (auto* af = dynamic_cast<AudioFileNode*>(gn->node.get()))
            if (af->Duration() > 0.0)
               return std::max<Arrange::Tick>(Arrange::kPPQ, Arrange::SecondsToTicks(af->Duration(), bpm));
         if (auto* vid = dynamic_cast<VideoSourceNode*>(gn->node.get()))
            if (vid->Duration() > 0.0)
               return std::max<Arrange::Tick>(Arrange::kPPQ, Arrange::SecondsToTicks(vid->Duration(), bpm));
      }
      return Arrange::kTicksPerBar * 4;
   }

   void SetSlotSource(uint64_t laneId, int row, uint64_t srcUid)
   {
      ArrangeEdit([&]()
      {
         Arrange::Lane* l = Arrange::FindLane(gArrange, laneId);
         if (l == nullptr || row < 0)
            return;
         if ((int)l->slots.size() <= row)
            l->slots.resize((size_t)row + 1);
         Arrange::SessionSlot& s = l->slots[(size_t)row];
         if (s.Has())
            s.clip.srcUid = srcUid; // keep its settings, swap the source
         else
         {
            s = Arrange::SessionSlot();
            s.clip.id = gArrange.NewId();
            s.clip.srcUid = srcUid;
            s.clip.length = DefaultSlotLength(srcUid);
         }
         while (gArrange.scenes.size() < l->slots.size())
            gArrange.scenes.push_back(Arrange::Scene());
         gArrange.revision++;
      });
   }

   void ClearSlot(uint64_t laneId, int row)
   {
      ArrangeSessionStopLane(laneId);
      ArrangeEdit([&]()
      {
         Arrange::Lane* l = Arrange::FindLane(gArrange, laneId);
         if (l != nullptr && row >= 0 && row < (int)l->slots.size() && l->slots[(size_t)row].Has())
         {
            l->slots[(size_t)row] = Arrange::SessionSlot();
            gArrange.revision++;
         }
      });
   }

   // Edits a slot's launch settings in place, one undo step per change.
   template <class Fn>
   void EditSlot(uint64_t laneId, int row, Fn&& fn)
   {
      ArrangeEdit([&]()
      {
         Arrange::Lane* l = Arrange::FindLane(gArrange, laneId);
         if (l != nullptr && row >= 0 && row < (int)l->slots.size() && l->slots[(size_t)row].Has())
         {
            fn(l->slots[(size_t)row]);
            gArrange.revision++;
         }
      });
   }

   void SourceMenu(uint64_t laneId, int row, int laneType)
   {
      int listed = 0;
      for (GraphNode& gn : gNodes)
      {
         if (!ArrangeNodeFitsLane(gn, laneType))
            continue;
         listed++;
         if (ImGui::MenuItem(NodeTitleWithInstance(gn).c_str()))
            SetSlotSource(laneId, row, gn.uid);
      }
      if (listed == 0)
         ImGui::TextDisabled(laneType == Arrange::kLaneAudio ? "no audio node on the canvas" : "no image / video / 3D node on the canvas");
   }

   std::string SlotLabel(const Arrange::Clip& c)
   {
      if (!c.name.empty())
         return c.name;
      GraphNode* gn = FindNodeByUid(c.srcUid);
      return gn != nullptr ? NodeTitleWithInstance(*gn) : std::string("(offline)");
   }

   const Arrange::SessionSlot* FindSlot(uint64_t laneId, int row)
   {
      const Arrange::Lane* l = Arrange::FindLane(gArrange, laneId);
      if (l == nullptr || row < 0 || row >= (int)l->slots.size() || !l->slots[(size_t)row].Has())
         return nullptr;
      return &l->slots[(size_t)row];
   }

   // Drag and drop: a clip onto another cell of a track of its kind. A move
   // swaps with what the target held; a copy (Ctrl) overwrites it.
   struct DragPayload { uint64_t laneId; int row; };

   void MoveSlot(uint64_t fromLane, int fromRow, uint64_t toLane, int toRow, bool copy)
   {
      const Arrange::Lane* a = Arrange::FindLane(gArrange, fromLane);
      const Arrange::Lane* b = Arrange::FindLane(gArrange, toLane);
      if (a == nullptr || b == nullptr || a->type != b->type || toRow < 0 || FindSlot(fromLane, fromRow) == nullptr ||
          (fromLane == toLane && fromRow == toRow))
         return;
      // A clip that is playing stops (on the launch grid) before it moves.
      if (ArrangeSessionQuery(toLane, toRow).playing)
         ArrangeSessionStopLane(toLane);
      if (!copy && ArrangeSessionQuery(fromLane, fromRow).playing)
         ArrangeSessionStopLane(fromLane);
      ArrangeEdit([&]()
      {
         Arrange::Lane* to = Arrange::FindLane(gArrange, toLane);
         if ((int)to->slots.size() <= toRow)
            to->slots.resize((size_t)toRow + 1);
         Arrange::Lane* from = Arrange::FindLane(gArrange, fromLane); // after the resize (same lane)
         if (copy)
         {
            Arrange::SessionSlot dup = from->slots[(size_t)fromRow];
            dup.clip.id = gArrange.NewId();
            to->slots[(size_t)toRow] = dup;
         }
         else
            std::swap(from->slots[(size_t)fromRow], to->slots[(size_t)toRow]);
         while (gArrange.scenes.size() < to->slots.size())
            gArrange.scenes.push_back(Arrange::Scene());
         gArrange.revision++;
      });
      gArrangeSessSelLane = toLane;
      gArrangeSessSelRow = toRow;
   }

   // Puts a slot's clip on its track in the timeline at a tick (one loop
   // long); returns how long it is (0 = nothing placed). Call inside an edit.
   Arrange::Tick PlaceSlotOnTimeline(uint64_t laneId, int row, Arrange::Tick at)
   {
      const Arrange::SessionSlot* slot = FindSlot(laneId, row);
      if (slot == nullptr)
         return 0;
      Arrange::Clip c = slot->clip;
      c.id = 0;
      c.groupId = 0;
      c.start = at;
      c.length = slot->LoopLen();
      return Arrange::PlaceOverwrite(gArrange, laneId, c) ? c.length : 0;
   }

   Arrange::Tick SendAt()
   {
      // On the bar the playhead is in: a Session idea lands on the grid.
      const Arrange::Tick at = ArrangePlayTick();
      return at - at % Arrange::kTicksPerBar;
   }

   void SendSlotToTimeline(uint64_t laneId, int row)
   {
      const Arrange::Tick at = SendAt();
      ArrangeEdit([&]() { PlaceSlotOnTimeline(laneId, row, at); });
   }

   // Every clip of the row, side by side at the playhead; a stopped
   // transport's playhead moves past them, so sending scene after scene
   // writes the song out in order.
   void SendSceneToTimeline(int row)
   {
      const Arrange::Tick at = SendAt();
      Arrange::Tick longest = 0;
      ArrangeEdit([&]()
      {
         std::vector<uint64_t> ids;
         for (const Arrange::Lane& l : gArrange.lanes)
            ids.push_back(l.id);
         for (uint64_t id : ids)
            longest = std::max(longest, PlaceSlotOnTimeline(id, row, at));
      });
      if (longest > 0 && !Transport::Instance().IsPlaying())
         ArrangeSeekTick(at + longest);
   }
}

bool ArrangeSessionSlotSelected()
{
   return gArrangeSessionView && ArrSess::FindSlot(gArrangeSessSelLane, gArrangeSessSelRow) != nullptr;
}

// Turbo 0.72: the inspector for the selected Session clip - its name,
// source, length and launch settings, and Send to Timeline.
void DrawArrangeSessionSlotInspector()
{
   using namespace ArrSess;
   const uint64_t laneId = gArrangeSessSelLane;
   const int r = gArrangeSessSelRow;
   Arrange::Lane* lane = Arrange::FindLane(gArrange, laneId);
   if (lane == nullptr || FindSlot(laneId, r) == nullptr)
      return;
   Arrange::SessionSlot& live = lane->slots[(size_t)r];
   char nameBuf[128];
   snprintf(nameBuf, sizeof(nameBuf), "%s", live.clip.name.c_str());
   GraphNode* src = FindNodeByUid(live.clip.srcUid);
   const std::string srcName = src != nullptr ? NodeTitleWithInstance(*src) : std::string("(offline)");
   if (ImGui::InputTextWithHint("name", srcName.c_str(), nameBuf, sizeof(nameBuf)))
   {
      live.clip.name = nameBuf;
      gArrange.revision++;
   }
   ArrangeTrackGesture();
   if (ImGui::BeginCombo("source", srcName.c_str()))
   {
      SourceMenu(laneId, r, lane->type);
      ImGui::EndCombo();
   }
   const Arrange::SessionSlot s = *FindSlot(laneId, r);
   const double bpb = std::max(0.25, Transport::Instance().BeatsPerBar());
   float lenBars = (float)(Arrange::TicksToBeats(s.clip.length) / bpb);
   if (ImGui::DragFloat("length", &lenBars, 0.05f, 0.25f, 1024.0f, "%.2f bars"))
   {
      const Arrange::Tick want = std::max<Arrange::Tick>(Arrange::kPPQ / 4, Arrange::BeatsToTicks(std::max(0.25f, lenBars) * bpb));
      EditSlot(laneId, r, [want](Arrange::SessionSlot& x) { x.clip.length = want; });
   }
   bool loop = s.loop;
   if (ImGui::Checkbox("loop", &loop))
      EditSlot(laneId, r, [loop](Arrange::SessionSlot& x) { x.loop = loop; });
   float bars = (float)(Arrange::TicksToBeats(s.LoopLen()) / bpb);
   if (ImGui::DragFloat("loop length", &bars, 0.05f, 0.25f, 256.0f, "%.2f bars"))
   {
      const Arrange::Tick want = Arrange::BeatsToTicks(std::max(0.25f, bars) * bpb);
      EditSlot(laneId, r, [want](Arrange::SessionSlot& x) { x.loopTicks = want >= x.clip.length ? 0 : want; });
   }
   static const char* kLaunch[] = { "trigger", "gate" };
   int lm = std::clamp(s.launchMode, 0, 1);
   if (ImGui::Combo("launch", &lm, kLaunch, 2))
      EditSlot(laneId, r, [lm](Arrange::SessionSlot& x) { x.launchMode = lm; });
   const std::vector<std::string>& qs = MusicTime::LaunchQuantizeList();
   const char* qName = s.quantize < 0 || s.quantize >= (int)qs.size() ? "session default" : qs[(size_t)s.quantize].c_str();
   if (ImGui::BeginCombo("quantize", qName))
   {
      if (ImGui::Selectable("session default", s.quantize < 0))
         EditSlot(laneId, r, [](Arrange::SessionSlot& x) { x.quantize = -1; });
      for (int qi = 0; qi < (int)qs.size(); qi++)
         if (ImGui::Selectable(qs[(size_t)qi].c_str(), s.quantize == qi))
            EditSlot(laneId, r, [qi](Arrange::SessionSlot& x) { x.quantize = qi; });
      ImGui::EndCombo();
   }
   static const char* kFollow[] = { "keep playing", "next clip", "previous clip", "random clip", "stop" };
   int fa = std::clamp(s.followAction, 0, (int)Arrange::kFollowCount - 1);
   if (ImGui::Combo("then", &fa, kFollow, (int)Arrange::kFollowCount))
      EditSlot(laneId, r, [fa](Arrange::SessionSlot& x) { x.followAction = fa; });
   if (s.followAction != Arrange::kFollowNone)
   {
      int fb = s.followBars;
      if (ImGui::SliderInt("after", &fb, Arrange::kFollowBarsMin, Arrange::kFollowBarsMax, "%d bars"))
         EditSlot(laneId, r, [fb](Arrange::SessionSlot& x) { x.followBars = fb; });
   }
   ImGui::Dummy(ImVec2(0.0f, 4.0f));
   if (ImGui::Button("Send to Timeline"))
      SendSlotToTimeline(laneId, r);
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("put this clip on its track in the timeline, at the playhead's bar");
   ImGui::SameLine();
   if (ImGui::Button("Clear"))
      ClearSlot(laneId, r);
}

void DrawArrangeSessionGrid(ImVec2 areaMin, ImVec2 areaMax)
{
   using namespace ArrSess;
   ImGui::SetCursorScreenPos(areaMin);
   ImGui::BeginChild("##arrsessiongrid", ImVec2(areaMax.x - areaMin.x, areaMax.y - areaMin.y), false,
                     ImGuiWindowFlags_HorizontalScrollbar);
   ImDrawList* dl = ImGui::GetWindowDrawList();
   const ImVec2 origin = ImGui::GetCursorScreenPos();
   const int laneCount = (int)gArrange.lanes.size();
   int rows = std::max(kMinRows, (int)gArrange.scenes.size());
   for (const Arrange::Lane& l : gArrange.lanes)
      rows = std::max(rows, (int)l.slots.size());
   rows += 1; // always one empty row to launch into
   const ImU32 accent = ArrUi::ThemeU32(CategoryColors::CurrentUiTheme().accent);
   const ImU32 go = ArrUi::Semantic(tok::action_go);
   const float t = (float)ImGui::GetTime();

   if (laneCount == 0)
   {
      ImGui::TextDisabled("No tracks yet. + Audio / + Video add one; each track is a column of clips.");
      ImGui::EndChild();
      return;
   }

   // Column heads: track name and colour.
   for (int li = 0; li < laneCount; li++)
   {
      const Arrange::Lane& lane = gArrange.lanes[(size_t)li];
      const ImVec2 p(origin.x + kSceneColW + li * kColW, origin.y);
      dl->AddRectFilled(p, ImVec2(p.x + kColW - 4.0f, p.y + kHeadH - 4.0f), ArrUi::Tint(0.05f), tok::radius_field);
      dl->AddRectFilled(p, ImVec2(p.x + 4.0f, p.y + kHeadH - 4.0f), ArrangeLaneAccent(lane), tok::radius_field);
      dl->PushClipRect(p, ImVec2(p.x + kColW - 8.0f, p.y + kHeadH), true);
      dl->AddText(ImVec2(p.x + 10.0f, p.y + (kHeadH - 4.0f - ImGui::GetTextLineHeight()) * 0.5f), ArrUi::TextCol(),
                  ArrangeLaneLabel(gArrange, li).c_str());
      dl->PopClipRect();
   }

   // Rows: the scene button, then one cell per track.
   for (int r = 0; r < rows; r++)
   {
      const float y = origin.y + kHeadH + r * kRowH;
      ImGui::PushID(r);
      // Scene launcher.
      {
         const ArrangeSessionSceneState ss = ArrangeSessionSceneQuery(r);
         const ImVec2 p(origin.x, y);
         ImGui::SetCursorScreenPos(p);
         const bool pressed = ImGui::InvisibleButton("##scene", ImVec2(kSceneColW - 6.0f, kRowH - 4.0f));
         const bool hov = ImGui::IsItemHovered();
         ImU32 face = hov ? ArrUi::Tint(tok::accent_hover) : ArrUi::RecessCol();
         if (ss.playing)
            face = ArrangeScaleCol(go, 0.7f);
         else if (ss.queued && std::fmod(t, 0.5f) < 0.25f)
            face = ArrangeScaleCol(go, 0.45f);
         dl->AddRectFilled(p, ImVec2(p.x + kSceneColW - 6.0f, p.y + kRowH - 4.0f), face, tok::radius_field);
         Tabler::DrawPlayerPlay(dl, ImVec2(p.x + 14.0f, p.y + (kRowH - 4.0f) * 0.5f), 14.0f,
                                ss.playing || hov ? ArrUi::TextCol() : ArrUi::DimCol());
         const std::string name = r < (int)gArrange.scenes.size() && !gArrange.scenes[(size_t)r].name.empty()
                                     ? gArrange.scenes[(size_t)r].name : "Scene " + std::to_string(r + 1);
         dl->AddText(ImVec2(p.x + 28.0f, p.y + (kRowH - 4.0f - ImGui::GetTextLineHeight()) * 0.5f),
                     ss.next ? accent : ArrUi::TextCol(), name.c_str());
         if (pressed)
            ArrangeSessionLaunchScene(r);
         if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##scenemenu");
         if (hov && !ImGui::IsPopupOpen("##scenemenu"))
            ImGui::SetTooltip("launch every clip of this row (on the launch grid)\nright-click: send the row to the timeline");
         if (ImGui::BeginPopup("##scenemenu"))
         {
            if (ImGui::MenuItem("Send scene to Timeline (at the playhead)"))
               SendSceneToTimeline(r);
            ImGui::EndPopup();
         }
      }
      for (int li = 0; li < laneCount; li++)
      {
         const Arrange::Lane& lane = gArrange.lanes[(size_t)li];
         const uint64_t laneId = lane.id;
         const int laneType = lane.type;
         const bool has = r < (int)lane.slots.size() && lane.slots[(size_t)r].Has();
         const ImVec2 p(origin.x + kSceneColW + li * kColW, y);
         const ImVec2 q(p.x + kColW - 4.0f, p.y + kRowH - 4.0f);
         ImGui::PushID((int)laneId);
         ImGui::SetCursorScreenPos(p);
         ImGui::InvisibleButton("##cell", ImVec2(kColW - 4.0f, kRowH - 4.0f),
                                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
         const bool hov = ImGui::IsItemHovered();
         if (has)
         {
            const Arrange::SessionSlot& slot = lane.slots[(size_t)r];
            const ArrangeSessionSlotState st = ArrangeSessionQuery(laneId, r);
            const ImU32 base = ArrangeClipColor(lane, slot.clip);
            ImU32 face = ArrangeScaleCol(base, hov ? 0.75f : 0.55f);
            if (st.playing)
               face = ArrangeScaleCol(base, 0.95f);
            dl->AddRectFilled(p, q, face, tok::radius_field);
            if (st.queued && std::fmod(t, 0.5f) < 0.25f)
               dl->AddRect(p, q, IM_COL32(255, 255, 255, 220), tok::radius_field, 0, 2.0f);
            else if (st.upNext)
               dl->AddRect(p, q, accent, tok::radius_field, 0, 1.5f);
            // State glyph: playing / waiting / idle.
            const ImVec2 gc(p.x + 12.0f, p.y + (kRowH - 4.0f) * 0.5f);
            if (st.playing)
               Tabler::DrawPlayerPlay(dl, gc, 13.0f, IM_COL32(255, 255, 255, 255));
            else
               dl->AddRect(ImVec2(gc.x - 4.0f, gc.y - 4.0f), ImVec2(gc.x + 4.0f, gc.y + 4.0f), IM_COL32(255, 255, 255, 150), 1.5f);
            dl->PushClipRect(p, ImVec2(q.x - 4.0f, q.y), true);
            dl->AddText(ImVec2(p.x + 24.0f, p.y + (kRowH - 4.0f - ImGui::GetTextLineHeight()) * 0.5f),
                        IM_COL32(245, 247, 252, 255), SlotLabel(slot.clip).c_str());
            dl->PopClipRect();
            if (st.playing)
               dl->AddRectFilled(ImVec2(p.x + 2.0f, q.y - 3.0f),
                                 ImVec2(p.x + 2.0f + (q.x - p.x - 4.0f) * std::clamp(st.progress, 0.0f, 1.0f), q.y - 1.0f),
                                 IM_COL32(255, 255, 255, 200), 1.0f);
            if (gArrangeSessSelLane == laneId && gArrangeSessSelRow == r)
               dl->AddRect(ImVec2(p.x - 1.0f, p.y - 1.0f), ImVec2(q.x + 1.0f, q.y + 1.0f), ArrUi::TextCol(), tok::radius_field, 0, 1.0f);
            // Launch: the play glyph and gate clips on press (no delay); the
            // rest of a trigger clip on release, unless it was dragged away.
            static bool sLaunchedOnPress = false;
            static bool sDragged = false;
            static bool sLeftPress = false; // a right-click activates the cell too: it never launches
            if (ImGui::IsItemActivated())
               sLeftPress = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (ImGui::IsItemActivated() && sLeftPress)
            {
               gArrangeSessSelLane = laneId;
               gArrangeSessSelRow = r;
               sDragged = false;
               sLaunchedOnPress = slot.launchMode == 1 || ImGui::GetIO().MousePos.x < p.x + 24.0f;
               if (sLaunchedOnPress)
                  ArrangeSessionLaunch(laneId, r);
            }
            if (ImGui::BeginDragDropSource())
            {
               sDragged = true;
               const DragPayload pl { laneId, r };
               ImGui::SetDragDropPayload("ARR_SLOT", &pl, sizeof(pl));
               ImGui::Text("%s%s", ImGui::GetIO().KeyCtrl ? "copy: " : "", SlotLabel(slot.clip).c_str());
               ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemDeactivated())
            {
               if (slot.launchMode == 1)
                  ArrangeSessionRelease(laneId, r);
               else if (sLeftPress && !sLaunchedOnPress && !sDragged && hov)
                  ArrangeSessionLaunch(laneId, r);
            }
            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
               gArrangeSessSelLane = laneId;
               gArrangeSessSelRow = r;
               ImGui::OpenPopup("##slotmenu");
            }
            if (hov && !ImGui::IsPopupOpen("##slotmenu") && !ImGui::IsDragDropActive())
               ImGui::SetTooltip("%s\nclick: launch / stop  -  drag: move (Ctrl: copy)  -  right-click: launch settings",
                                 SlotLabel(slot.clip).c_str());
         }
         else
         {
            dl->AddRectFilled(p, q, hov ? ArrUi::Tint(tok::accent_hover * 0.6f) : ArrUi::RecessCol(), tok::radius_field);
            if (hov)
            {
               dl->AddText(ImVec2(p.x + (kColW - 4.0f) * 0.5f - 4.0f, p.y + (kRowH - 4.0f - ImGui::GetTextLineHeight()) * 0.5f),
                           ArrUi::DimCol(), "+");
               if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                  ImGui::OpenPopup("##slotsource");
               else if (!ImGui::IsDragDropActive())
                  ImGui::SetTooltip("double-click (or right-click) to put a clip here");
            }
         }
         if (ImGui::BeginDragDropTarget())
         {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ARR_SLOT"))
            {
               const DragPayload d = *(const DragPayload*)pl->Data;
               MoveSlot(d.laneId, d.row, laneId, r, ImGui::GetIO().KeyCtrl);
            }
            ImGui::EndDragDropTarget();
         }
         if (ImGui::BeginPopup("##slotsource"))
         {
            ImGui::TextDisabled("clip from");
            ImGui::Separator();
            SourceMenu(laneId, r, laneType);
            ImGui::EndPopup();
         }
         if (ImGui::BeginPopup("##slotmenu"))
         {
            const Arrange::Lane* ln = Arrange::FindLane(gArrange, laneId);
            if (ln == nullptr || r >= (int)ln->slots.size() || !ln->slots[(size_t)r].Has())
               ImGui::CloseCurrentPopup();
            else
            {
               const Arrange::SessionSlot s = ln->slots[(size_t)r];
               ImGui::TextDisabled("%s", SlotLabel(s.clip).c_str());
               ImGui::Separator();
               if (ImGui::MenuItem("Loop", nullptr, s.loop))
                  EditSlot(laneId, r, [](Arrange::SessionSlot& x) { x.loop = !x.loop; });
               float bars = (float)Arrange::TicksToBeats(s.LoopLen()) / (float)std::max(0.25, Transport::Instance().BeatsPerBar());
               ImGui::SetNextItemWidth(120.0f);
               if (ImGui::DragFloat("loop length (bars)", &bars, 0.05f, 0.25f, 256.0f, "%.2f"))
               {
                  const Arrange::Tick want = Arrange::BeatsToTicks(std::max(0.25f, bars) * std::max(0.25, Transport::Instance().BeatsPerBar()));
                  EditSlot(laneId, r, [want](Arrange::SessionSlot& x)
                  {
                     x.loopTicks = want >= x.clip.length ? 0 : want; // 0 = the clip's own length
                  });
               }
               if (ImGui::BeginMenu("Launch"))
               {
                  if (ImGui::MenuItem("Trigger (click starts, click again stops)", nullptr, s.launchMode == 0))
                     EditSlot(laneId, r, [](Arrange::SessionSlot& x) { x.launchMode = 0; });
                  if (ImGui::MenuItem("Gate (plays while held)", nullptr, s.launchMode == 1))
                     EditSlot(laneId, r, [](Arrange::SessionSlot& x) { x.launchMode = 1; });
                  ImGui::EndMenu();
               }
               if (ImGui::BeginMenu("Quantize"))
               {
                  if (ImGui::MenuItem("Session default", nullptr, s.quantize < 0))
                     EditSlot(laneId, r, [](Arrange::SessionSlot& x) { x.quantize = -1; });
                  const std::vector<std::string>& qs = MusicTime::LaunchQuantizeList();
                  for (int qi = 0; qi < (int)qs.size(); qi++)
                     if (ImGui::MenuItem(qs[(size_t)qi].c_str(), nullptr, s.quantize == qi))
                        EditSlot(laneId, r, [qi](Arrange::SessionSlot& x) { x.quantize = qi; });
                  ImGui::EndMenu();
               }
               if (ImGui::BeginMenu("Then"))
               {
                  static const char* kFollow[] = { "keep playing", "next clip", "previous clip", "random clip", "stop" };
                  for (int fa = 0; fa < (int)Arrange::kFollowCount; fa++)
                     if (ImGui::MenuItem(kFollow[fa], nullptr, s.followAction == fa))
                        EditSlot(laneId, r, [fa](Arrange::SessionSlot& x) { x.followAction = fa; });
                  int fb = s.followBars;
                  ImGui::SetNextItemWidth(90.0f);
                  if (ImGui::SliderInt("after (bars)", &fb, Arrange::kFollowBarsMin, Arrange::kFollowBarsMax))
                     EditSlot(laneId, r, [fb](Arrange::SessionSlot& x) { x.followBars = fb; });
                  ImGui::EndMenu();
               }
               if (ImGui::BeginMenu("Source"))
               {
                  SourceMenu(laneId, r, laneType);
                  ImGui::EndMenu();
               }
               ImGui::Separator();
               if (ImGui::MenuItem("Send to Timeline (at the playhead)"))
                  SendSlotToTimeline(laneId, r);
               if (ImGui::MenuItem("Clear"))
                  ClearSlot(laneId, r);
            }
            ImGui::EndPopup();
         }
         ImGui::PopID();
      }
      ImGui::PopID();
   }

   // Stop row: one square per track.
   {
      const float y = origin.y + kHeadH + rows * kRowH + 4.0f;
      for (int li = 0; li < laneCount; li++)
      {
         const uint64_t laneId = gArrange.lanes[(size_t)li].id;
         const ArrangeSessionLaneState ls = ArrangeSessionLaneQuery(laneId);
         ImGui::SetCursorScreenPos(ImVec2(origin.x + kSceneColW + li * kColW + (kColW - 4.0f) * 0.5f - ArrUi::Tile() * 0.5f, y));
         ImGui::PushID((int)laneId);
         if (ArrUi::IconTile("##stoplane",
                             [](ImDrawList* d, ImVec2 c, float s, ImU32 col)
                             { d->AddRectFilled(ImVec2(c.x - s * 0.22f, c.y - s * 0.22f), ImVec2(c.x + s * 0.22f, c.y + s * 0.22f), col, 1.5f); },
                             ls.stopQueued ? "stopping on the launch grid" : "stop this track (on the launch grid)",
                             ls.playingSlot >= 0))
            ArrangeSessionStopLane(laneId);
         ImGui::PopID();
      }
      ImGui::SetCursorScreenPos(ImVec2(origin.x, y + ArrUi::Tile() + 4.0f));
      ImGui::Dummy(ImVec2(kSceneColW + laneCount * kColW, 1.0f));
   }
   ImGui::EndChild();
}
