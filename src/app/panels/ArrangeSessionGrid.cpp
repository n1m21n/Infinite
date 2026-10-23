// Session view grid: the Timeline panel's clip launcher. Tracks are columns,
// scenes are rows. Launch logic lives in ArrangeSession.cpp; this file draws
// the grid and turns clicks, menus and file drops into model edits.
//
// The idea comes from Ricardo Palmieri's Infinite-Turbo fork; this is a fresh
// implementation.
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PillGroup.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   namespace
   {
      constexpr float kSceneColW = 136.0f;
      constexpr float kColMinW = 112.0f;
      constexpr float kColMaxW = 176.0f;
      constexpr float kHeaderH = 28.0f;
      constexpr float kRowH = 30.0f;
      constexpr float kFooterH = 30.0f;
      constexpr float kGap = 2.0f;
      constexpr int kMinRows = 6;

      const char* const kFollowNames[Arrange::kFollowCount] = {
         I18N_KEY("No Follow Action"), I18N_KEY("Next"), I18N_KEY("Previous"), I18N_KEY("Random"), I18N_KEY("Stop")
      };
      const int kFollowBarChoices[] = { 1, 2, 4, 8, 16, 32, 64 };

      uint64_t sCtxLaneId = 0;
      int sCtxRow = -1;
      int sSceneCtxRow = -1;
      char sSceneNameBuf[64] = {};
      float sSceneTempoEdit = 0.0f;

      ImU32 WithAlpha(ImU32 c, float a)
      {
         return (c & 0x00FFFFFFu) | ((ImU32)(std::clamp(a, 0.0f, 1.0f) * 255.0f) << IM_COL32_A_SHIFT);
      }

      ImU32 TextCol(float alpha)
      {
         const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         return ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, alpha));
      }

      bool ClipHasTint(const Arrange::Clip& c)
      {
         return c.colorR > 0.001f || c.colorG > 0.001f || c.colorB > 0.001f;
      }

      // The timeline's own clip palette, so a slot reads as the same object.
      ImU32 SlotFill(const Arrange::Clip& c, bool isVideo, bool active)
      {
         if (ClipHasTint(c))
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.colorR, c.colorG, c.colorB, 1.0f));
         if (active)
            return isVideo ? tok::U32(tok::pal::c_8B5CF6FF) : tok::U32(tok::pal::c_10B981FF);
         return isVideo ? tok::U32(tok::pal::c_6D28D9D2) : tok::U32(tok::pal::c_059669D2);
      }

      ImU32 LabelOn(ImU32 fill)
      {
         const ImVec4 f = ImGui::ColorConvertU32ToFloat4(fill);
         const float lum = (0.299f * f.x + 0.587f * f.y + 0.114f * f.z) * 255.0f;
         return lum > 150.0f ? tok::U32(tok::pal::c_141418FF) : tok::U32(tok::pal::c_FFFFFFFF);
      }

      std::string SlotLabel(const Arrange::Clip& c)
      {
         if (!c.name.empty())
            return c.name;
         if (GraphNode* gn = FindNodeByUid(c.srcUid))
            return NodeTitle(*gn);
         return T("Unassigned");
      }

      std::string SceneLabel(int row)
      {
         if (row < (int)gArrange.scenes.size() && !gArrange.scenes[(size_t)row].name.empty())
            return gArrange.scenes[(size_t)row].name;
         char buf[48];
         snprintf(buf, sizeof(buf), T("Scene %d"), row + 1);
         return buf;
      }

      const Arrange::SessionSlot* SlotOf(const Arrange::Lane& lane, int row)
      {
         if (row < 0 || row >= (int)lane.slots.size() || !lane.slots[(size_t)row].Has())
            return nullptr;
         return &lane.slots[(size_t)row];
      }

      // Rows past the last scene are drawn so there is always somewhere to
      // drop; touching one creates the scenes up to it.
      void EnsureRow(Arrange::Lane& lane, int row)
      {
         while ((int)gArrange.scenes.size() <= row)
            gArrange.scenes.push_back(Arrange::Scene{});
         if ((int)lane.slots.size() <= row)
            lane.slots.resize((size_t)row + 1);
      }

      // The single selected timeline clip, if it lives on a lane of `type`.
      const Arrange::Clip* SelectedClipOfType(int type)
      {
         if (gArrangeSel.size() != 1)
            return nullptr;
         const Arrange::Loc loc = Arrange::Find(gArrange, *gArrangeSel.begin());
         if (!loc.Valid() || gArrange.lanes[loc.lane].type != type)
            return nullptr;
         return &gArrange.lanes[loc.lane].clips[loc.index];
      }

      // Progress ring: a faint full circle with the elapsed part in white,
      // starting at twelve o'clock (the MPC pad ring).
      void DrawProgressRing(ImDrawList* dl, ImVec2 c, float r, float progress, ImU32 col)
      {
         dl->AddCircle(c, r, WithAlpha(col, 0.30f), 24, 2.0f);
         const float a0 = -IM_PI * 0.5f;
         const float a1 = a0 + std::clamp(progress, 0.0f, 1.0f) * 2.0f * IM_PI;
         if (a1 > a0 + 0.02f)
         {
            dl->PathArcTo(c, r, a0, a1, 24);
            dl->PathStroke(col, 0, 2.0f);
         }
      }

      void DrawStopSquare(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
      {
         dl->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f), col, 1.5f);
      }

      // Queued launches and stops pulse on the beat, so the wait reads in time
      // with the music; reduce-motion holds them steady.
      float QueuedPulse()
      {
         if (UiAnim::ReduceMotion())
            return 1.0f;
         const double b = Transport::Instance().Beats();
         const double frac = b - std::floor(b);
         return frac < 0.5 ? 1.0f : 0.45f;
      }

      void DrawFollowBadge(ImDrawList* dl, ImVec2 rightCentre, const Arrange::SessionSlot& ss, ImU32 col)
      {
         if (ss.followAction == Arrange::kFollowNone)
            return;
         char num[8];
         snprintf(num, sizeof(num), "%d", ss.followBars);
         const ImVec2 ns = ImGui::CalcTextSize(num);
         const float gx = rightCentre.x - ns.x - 9.0f;
         const ImVec2 gc(gx, rightCentre.y);
         const float gs = tok::icon_sm;
         switch (ss.followAction)
         {
            case Arrange::kFollowNext: glyph::DrawChevronDown(dl, gc, gs, col); break;
            case Arrange::kFollowPrev: glyph::DrawChevronUp(dl, gc, gs, col); break;
            case Arrange::kFollowRandom: glyph::DrawRefresh(dl, gc, gs, col); break;
            default: DrawStopSquare(dl, gc, 7.0f, col); break;
         }
         dl->AddText(ImVec2(rightCentre.x - ns.x, rightCentre.y - ns.y * 0.5f), col, num);
      }

      void ImportIntoSlot(const std::string& path, uint64_t laneId, int row, bool* consumed)
      {
         Arrange::ImportMediaKind kind;
         *consumed = false;
         const Arrange::Lane* lane = Arrange::FindLane(gArrange, laneId);
         if (lane == nullptr || !ArrangeMediaKindForPath(path, kind))
            return;
         const bool audioKind = kind == Arrange::ImportMediaKind::Audio;
         if (audioKind != (lane->type == Arrange::kLaneAudio))
            return;
         ArrangeImportMediaFile(path, laneId, 0, kind, row);
         *consumed = true;
      }

      void DrawSlotMenu()
      {
         if (!MenuParts::BeginPopup("##sessionslotctx"))
            return;
         Arrange::Lane* lane = Arrange::FindLane(gArrange, sCtxLaneId);
         if (lane == nullptr || sCtxRow < 0)
         {
            MenuParts::EndPopup();
            return;
         }
         const uint64_t laneId = lane->id;
         const int row = sCtxRow;
         const Arrange::SessionSlot* ss = SlotOf(*lane, row);
         if (ss != nullptr)
         {
            if (MenuParts::Item(L("Launch")))
               ArrangeSessionLaunch(laneId, row);
            if (MenuParts::SubMenu(L("Follow Action")))
            {
               for (int a = 0; a < Arrange::kFollowCount; a++)
                  if (MenuParts::Choice(L(kFollowNames[a]), ss->followAction == a) && ss->followAction != a)
                     ArrangeEdit([&]() {
                        if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                        {
                           l->slots[(size_t)row].followAction = a;
                           gArrange.revision++;
                        }
                     });
               ImGui::EndMenu();
            }
            // Greyed, not hidden, while there is no follow action to time.
            if (MenuParts::SubMenu(L("Follow After"), ss->followAction != Arrange::kFollowNone))
            {
               for (int bars : kFollowBarChoices)
               {
                  char label[32];
                  snprintf(label, sizeof(label), bars == 1 ? T("%d bar") : T("%d bars"), bars);
                  if (MenuParts::Choice(label, ss->followBars == bars) && ss->followBars != bars)
                     ArrangeEdit([&]() {
                        if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                        {
                           l->slots[(size_t)row].followBars = bars;
                           gArrange.revision++;
                        }
                     });
               }
               ImGui::EndMenu();
            }
            bool active = ss->clip.enabled;
            if (MenuParts::Check(L("Active"), &active))
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                  {
                     l->slots[(size_t)row].clip.enabled = active;
                     gArrange.revision++;
                  }
               });
            MenuParts::Separator();
            if (MenuParts::Item(L("Place on Timeline at Playhead")))
            {
               const Arrange::Tick at = std::max<Arrange::Tick>(0, Arrange::BeatsToTicks(Transport::Instance().Beats()));
               Arrange::Clip c = ss->clip;
               ArrangeEdit([&]() {
                  c.id = 0;
                  c.start = at;
                  Arrange::PlaceOverwrite(gArrange, laneId, c, nullptr);
               });
            }
            if (MenuParts::Item(L("Clear Slot")))
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                  {
                     l->slots[(size_t)row] = Arrange::SessionSlot{};
                     gArrange.revision++;
                  }
               });
         }
         else
         {
            const Arrange::Clip* sel = SelectedClipOfType(lane->type);
            if (MenuParts::Item(L("Paste Selected Clip"), nullptr, false, sel != nullptr) && sel != nullptr)
            {
               Arrange::Clip c = *sel;
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                  {
                     EnsureRow(*l, row);
                     c.id = gArrange.NewId();
                     c.start = 0;
                     c.groupId = 0;
                     l->slots[(size_t)row].clip = c;
                     gArrange.revision++;
                  }
               });
            }
            if (sel == nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               HelpTip("%s", T("Select one clip on this kind of track in Timeline view first."));
            MenuParts::Separator();
            if (MenuParts::Item(L("Stop Track"), nullptr, false, ArrangeSessionLaneQuery(laneId).playingSlot >= 0))
               ArrangeSessionStopLane(laneId);
         }
         MenuParts::EndPopup();
      }

      void DrawSceneMenu()
      {
         if (!MenuParts::BeginPopup("##sessionscenectx"))
            return;
         const int row = sSceneCtxRow;
         if (row < 0)
         {
            MenuParts::EndPopup();
            return;
         }
         ImGui::SetNextItemWidth(200.0f);
         if (ImGui::InputTextWithHint("##scenename", T("Scene name"), sSceneNameBuf, sizeof(sSceneNameBuf)))
         {
         }
         if (ImGui::IsItemDeactivatedAfterEdit())
         {
            const std::string name = sSceneNameBuf;
            ArrangeEdit([&]() {
               while ((int)gArrange.scenes.size() <= row)
                  gArrange.scenes.push_back(Arrange::Scene{});
               if (gArrange.scenes[(size_t)row].name != name)
               {
                  gArrange.scenes[(size_t)row].name = name;
                  gArrange.revision++;
               }
            });
         }
         ImGui::SetNextItemWidth(200.0f);
         ImGui::DragFloat("##scenetempo", &sSceneTempoEdit, 0.25f, 0.0f, 300.0f,
                          sSceneTempoEdit > 0.0f ? T("%.1f BPM on launch") : T("Tempo: unchanged"));
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Launching this scene sets the project tempo. 0 leaves the tempo alone."));
         if (ImGui::IsItemDeactivatedAfterEdit())
         {
            const float tempo = sSceneTempoEdit < 20.0f ? 0.0f : sSceneTempoEdit;
            sSceneTempoEdit = tempo;
            ArrangeEdit([&]() {
               while ((int)gArrange.scenes.size() <= row)
                  gArrange.scenes.push_back(Arrange::Scene{});
               if (gArrange.scenes[(size_t)row].tempo != tempo)
               {
                  gArrange.scenes[(size_t)row].tempo = tempo;
                  gArrange.revision++;
               }
            });
         }
         MenuParts::Separator();
         if (MenuParts::Item(L("Launch Scene")))
            ArrangeSessionLaunchScene(row);
         if (MenuParts::Item(L("Insert Scene Above")))
            ArrangeEdit([&]() {
               while ((int)gArrange.scenes.size() < row)
                  gArrange.scenes.push_back(Arrange::Scene{});
               gArrange.scenes.insert(gArrange.scenes.begin() + row, Arrange::Scene{});
               for (Arrange::Lane& l : gArrange.lanes)
                  if ((int)l.slots.size() > row)
                     l.slots.insert(l.slots.begin() + row, Arrange::SessionSlot{});
               gArrange.revision++;
            });
         const bool real = row < (int)gArrange.scenes.size();
         if (MenuParts::Item(L("Duplicate Scene"), nullptr, false, real))
            ArrangeEdit([&]() {
               gArrange.scenes.insert(gArrange.scenes.begin() + row + 1, gArrange.scenes[(size_t)row]);
               for (Arrange::Lane& l : gArrange.lanes)
               {
                  if ((int)l.slots.size() <= row)
                     continue;
                  Arrange::SessionSlot copy = l.slots[(size_t)row];
                  if (copy.Has())
                     copy.clip.id = gArrange.NewId();
                  l.slots.insert(l.slots.begin() + row + 1, copy);
               }
               gArrange.revision++;
            });
         if (MenuParts::Item(L("Delete Scene"), nullptr, false, real))
            ArrangeEdit([&]() {
               gArrange.scenes.erase(gArrange.scenes.begin() + row);
               for (Arrange::Lane& l : gArrange.lanes)
                  if ((int)l.slots.size() > row)
                     l.slots.erase(l.slots.begin() + row);
               gArrange.revision++;
            });
         MenuParts::EndPopup();
      }
   }


   void DrawArrangeSessionGrid()
   {
      const bool isLight = IsThemeLight();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const int laneCount = (int)gArrange.lanes.size();

      if (laneCount == 0)
      {
         const ImVec2 o = ImGui::GetCursorScreenPos();
         EmptyState::Draw(o, ImVec2(o.x + avail.x, o.y + avail.y), T("No tracks yet"),
                          T("Add a track in Timeline view, then drop audio or video files onto its cells here."));
         ImGui::Dummy(avail);
         gArrangePendingBrowserDrop.pending = false;
         return;
      }

      int rows = std::max(kMinRows, (int)gArrange.scenes.size());
      for (const Arrange::Lane& l : gArrange.lanes)
         rows = std::max(rows, (int)l.slots.size());
      const float colW = std::clamp((avail.x - kSceneColW) / (float)laneCount, kColMinW, kColMaxW);

      ImGui::BeginChild("##sessiongrid", avail, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 o = ImGui::GetCursorScreenPos();
      const float gridW = kSceneColW + colW * (float)laneCount;
      const float bodyTop = o.y + kHeaderH;
      const float footerTop = bodyTop + kRowH * (float)rows;
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const bool gridHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      const float pulse = QueuedPulse();

      // ---- corner: Record to Timeline ----
      {
         const bool rec = ArrangeSessionRecording();
         ImGui::SetCursorScreenPos(ImVec2(o.x + kGap, o.y + kGap));
         if (ActionButton::Draw(rec ? "Recording##sessrec" : "Record##sessrec",
                                ImVec2(kSceneColW - 2.0f * kGap, kHeaderH - 2.0f * kGap),
                                rec ? ActionButton::Kind::Record : ActionButton::Kind::Plain))
            ArrangeSessionSetRecording(!rec);
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Record to Timeline: what the launched clips play is written into the arrangement."));
      }

      // ---- track headers ----
      for (int li = 0; li < laneCount; li++)
      {
         const Arrange::Lane& lane = gArrange.lanes[(size_t)li];
         const float x0 = o.x + kSceneColW + colW * (float)li;
         const ImVec2 a(x0 + kGap, o.y + kGap), b(x0 + colW - kGap, o.y + kHeaderH - kGap);
         dl->AddRectFilled(a, b, TextCol(0.06f), tok::radius_field);
         const bool laneTint = lane.colorR > 0.001f || lane.colorG > 0.001f || lane.colorB > 0.001f;
         const ImU32 stripe = laneTint ? ImGui::ColorConvertFloat4ToU32(ImVec4(lane.colorR, lane.colorG, lane.colorB, 1.0f))
                                       : (lane.type == Arrange::kLaneVideo ? tok::U32(tok::pal::c_8B5CF6FF)
                                                                           : tok::U32(tok::pal::c_10B981FF));
         dl->AddRectFilled(a, ImVec2(a.x + 3.0f, b.y), stripe, tok::radius_field, ImDrawFlags_RoundCornersLeft);
         const std::string name = lane.name.empty() ? (lane.type == Arrange::kLaneVideo ? T("Video") : T("Audio"))
                                                    : lane.name;
         const float th = ImGui::GetTextLineHeight();
         dl->PushClipRect(a, ImVec2(b.x - 4.0f, b.y), true);
         dl->AddText(ImVec2(a.x + 9.0f, (a.y + b.y - th) * 0.5f),
                     Arrange::LaneEffectivelyEnabled(gArrange, lane) ? TextCol(0.92f) : TextCol(0.40f), name.c_str());
         dl->PopClipRect();
      }

      // ---- scene column ----
      bool anyLaunched = ArrangeSessionAnyLaunched();
      for (int row = 0; row < rows; row++)
      {
         const float y0 = bodyTop + kRowH * (float)row;
         const ImVec2 a(o.x + kGap, y0 + kGap), b(o.x + kSceneColW - kGap, y0 + kRowH - kGap);
         bool rowHasClip = false;
         for (const Arrange::Lane& l : gArrange.lanes)
            rowHasClip = rowHasClip || SlotOf(l, row) != nullptr;
         const bool realScene = row < (int)gArrange.scenes.size();
         ImGui::PushID(row);
         ImGui::SetCursorScreenPos(a);
         ImGui::InvisibleButton("##scene", ImVec2(b.x - a.x, b.y - a.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
         const bool hov = ImGui::IsItemHovered();
         if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && (rowHasClip || anyLaunched))
            ArrangeSessionLaunchScene(row);
         if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
         {
            sSceneCtxRow = row;
            const std::string nm = realScene ? gArrange.scenes[(size_t)row].name : std::string();
            snprintf(sSceneNameBuf, sizeof(sSceneNameBuf), "%s", nm.c_str());
            sSceneTempoEdit = realScene ? gArrange.scenes[(size_t)row].tempo : 0.0f;
            ImGui::OpenPopup("##sessionscenectx");
         }
         if (hov)
            HelpTip("%s", rowHasClip ? T("Launch this row on every track (right-click to rename or set a tempo)")
                                     : T("Right-click to rename this scene or set its tempo"));
         ImGui::PopID();
         dl->AddRectFilled(a, b, TextCol(hov ? 0.10f : 0.05f), tok::radius_field);
         const float cy = (a.y + b.y) * 0.5f;
         glyph::DrawPlayerPlay(dl, ImVec2(a.x + 13.0f, cy), tok::icon_sm, rowHasClip ? TextCol(0.85f) : TextCol(0.25f), true);
         const std::string label = SceneLabel(row);
         const float th = ImGui::GetTextLineHeight();
         float rightEdge = b.x - 6.0f;
         if (realScene && gArrange.scenes[(size_t)row].tempo > 0.0f)
         {
            char tbuf[24];
            snprintf(tbuf, sizeof(tbuf), "%.0f", gArrange.scenes[(size_t)row].tempo);
            const ImVec2 ts = ImGui::CalcTextSize(tbuf);
            dl->AddText(ImVec2(rightEdge - ts.x, cy - th * 0.5f), TextCol(0.55f), tbuf);
            rightEdge -= ts.x + 6.0f;
         }
         dl->PushClipRect(a, ImVec2(rightEdge, b.y), true);
         dl->AddText(ImVec2(a.x + 26.0f, cy - th * 0.5f), realScene ? TextCol(0.85f) : TextCol(0.35f), label.c_str());
         dl->PopClipRect();
      }

      // ---- cells ----
      for (int li = 0; li < laneCount; li++)
      {
         const Arrange::Lane& lane = gArrange.lanes[(size_t)li];
         const uint64_t laneId = lane.id;
         const bool isVideo = lane.type == Arrange::kLaneVideo;
         const bool laneOn = Arrange::LaneEffectivelyEnabled(gArrange, lane);
         const ArrangeSessionLaneState ls = ArrangeSessionLaneQuery(laneId);
         const float x0 = o.x + kSceneColW + colW * (float)li;
         ImGui::PushID((void*)(uintptr_t)laneId);
         for (int row = 0; row < rows; row++)
         {
            const float y0 = bodyTop + kRowH * (float)row;
            const ImVec2 a(x0 + kGap, y0 + kGap), b(x0 + colW - kGap, y0 + kRowH - kGap);
            const Arrange::SessionSlot* ss = SlotOf(lane, row);
            ImGui::PushID(row);
            ImGui::SetCursorScreenPos(a);
            ImGui::InvisibleButton("##cell", ImVec2(b.x - a.x, b.y - a.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool hov = ImGui::IsItemHovered();
            const bool held = ImGui::IsItemActive();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
               if (ss != nullptr)
                  ArrangeSessionLaunch(laneId, row);
               else if (ls.playingSlot >= 0)
                  ArrangeSessionStopLane(laneId);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
               sCtxLaneId = laneId;
               sCtxRow = row;
               ImGui::OpenPopup("##sessionslotctx");
            }
            const float cy = (a.y + b.y) * 0.5f;
            const float th = ImGui::GetTextLineHeight();
            if (ss != nullptr)
            {
               const ArrangeSessionSlotState st = ArrangeSessionQuery(laneId, row);
               GraphNode* src = FindNodeByUid(ss->clip.srcUid);
               const bool muted = !ss->clip.enabled || !laneOn || src == nullptr;
               ImU32 fill = SlotFill(ss->clip, isVideo, st.playing || held);
               if (muted)
                  fill = isLight ? tok::U32(tok::pal::c_B0B2BADC) : tok::U32(tok::pal::c_484850DC);
               dl->AddRectFilled(a, b, fill, tok::radius_field);
               if (muted)
                  DrawArrangeHatch(dl, a, b, isLight ? tok::U32(tok::pal::c_0000002D) : tok::U32(tok::pal::c_FFFFFF26));
               const ImU32 ink = muted ? TextCol(0.55f) : LabelOn(fill);
               if (st.playing)
                  dl->AddRect(a, b, tok::U32(tok::pal::c_FFFFFFF0), tok::radius_field, 0, 1.5f);
               else if (hov)
                  dl->AddRect(a, b, tok::U32(tok::pal::c_E6E6F0DC), tok::radius_field, 0, 1.0f);
               const ImVec2 ic(a.x + 13.0f, cy);
               if (st.playing)
                  DrawProgressRing(dl, ic, 7.0f, st.progress, tok::U32(tok::pal::c_FFFFFFFF));
               else
                  glyph::DrawPlayerPlay(dl, ic, tok::icon_sm, WithAlpha(ink, st.queued ? pulse : 0.85f), !st.queued);
               if (st.queued)
                  dl->AddRect(a, b, WithAlpha(tok::U32(tok::pal::c_FFFFFFFF), 0.9f * pulse), tok::radius_field, 0, 1.5f);
               const std::string label = SlotLabel(ss->clip);
               const float badgeW = ss->followAction != Arrange::kFollowNone ? 30.0f : 0.0f;
               dl->PushClipRect(a, ImVec2(b.x - 6.0f - badgeW, b.y), true);
               dl->AddText(ImVec2(a.x + 26.0f, cy - th * 0.5f), ink, label.c_str());
               dl->PopClipRect();
               DrawFollowBadge(dl, ImVec2(b.x - 6.0f, cy), *ss, WithAlpha(ink, 0.8f));
               if (hov)
                  HelpTip("%s", muted ? (src == nullptr ? T("This slot's source is missing") : T("This slot is inactive"))
                                      : T("Click to launch. Right-click for follow actions."));
            }
            else
            {
               // Empty: a quiet recess. While the track plays, the cell is a
               // stop button, and says so on hover.
               dl->AddRectFilled(a, b, TextCol(hov ? 0.08f : 0.035f), tok::radius_field);
               if (hov && ls.playingSlot >= 0)
               {
                  DrawStopSquare(dl, ImVec2((a.x + b.x) * 0.5f, cy), 8.0f, TextCol(0.6f));
                  HelpTip("%s", T("Stop this track"));
               }
               else if (hov)
                  HelpTip("%s", isVideo ? T("Empty slot: drop a video or image file here")
                                        : T("Empty slot: drop an audio file here"));
            }

            // File drops land on the cell under the drop point: a Finder drop
            // or a drag from the browser panel.
            if (!gDroppedFiles.empty() && gDropPos.x >= a.x && gDropPos.x < b.x && gDropPos.y >= a.y && gDropPos.y < b.y)
            {
               std::vector<std::string> remaining;
               int target = row;
               for (const std::string& p : gDroppedFiles)
               {
                  bool used = false;
                  ImportIntoSlot(p, laneId, target, &used);
                  if (used)
                     target++;
                  else
                     remaining.push_back(p);
               }
               gDroppedFiles = std::move(remaining);
            }
            if (gArrangePendingBrowserDrop.pending && gArrangePendingBrowserDrop.screenPos.x >= a.x &&
                gArrangePendingBrowserDrop.screenPos.x < b.x && gArrangePendingBrowserDrop.screenPos.y >= a.y &&
                gArrangePendingBrowserDrop.screenPos.y < b.y)
            {
               bool used = false;
               ImportIntoSlot(gArrangePendingBrowserDrop.path, laneId, row, &used);
               gArrangePendingBrowserDrop.pending = false;
            }
            ImGui::PopID();
         }

         // ---- footer: this track's stop button ----
         {
            const ImVec2 a(x0 + kGap, footerTop + kGap), b(x0 + colW - kGap, footerTop + kFooterH - kGap);
            const bool canStop = ls.playingSlot >= 0;
            ImGui::BeginDisabled(!canStop);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##stop", ImVec2(b.x - a.x, b.y - a.y)))
               ArrangeSessionStopLane(laneId);
            const bool hov = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            dl->AddRectFilled(a, b, TextCol(canStop ? (hov ? 0.12f : 0.07f) : 0.03f), tok::radius_field);
            const float alpha = !canStop ? 0.22f : (ls.stopQueued ? 0.9f * pulse : 0.85f);
            DrawStopSquare(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 9.0f, TextCol(alpha));
            if (hov)
               HelpTip("%s", T("Stop this track: it goes back to playing the Timeline"));
         }
         ImGui::PopID();
      }

      // ---- footer: Stop All ----
      {
         ImGui::SetCursorScreenPos(ImVec2(o.x + kGap, footerTop + kGap));
         ImGui::BeginDisabled(!anyLaunched);
         if (ActionButton::Draw("Stop All##sessstopall", ImVec2(kSceneColW - 2.0f * kGap, kFooterH - 2.0f * kGap)))
            ArrangeSessionStopAll();
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            HelpTip("%s", T("Stop every track: the whole panel goes back to playing the Timeline"));
      }

      // Hairline under the headers and above the stop row: the grid's frame.
      dl->AddLine(ImVec2(o.x, bodyTop - 0.5f), ImVec2(o.x + gridW, bodyTop - 0.5f), TextCol(0.10f), 1.0f);
      dl->AddLine(ImVec2(o.x, footerTop + 0.5f), ImVec2(o.x + gridW, footerTop + 0.5f), TextCol(0.10f), 1.0f);

      DrawSlotMenu();
      DrawSceneMenu();

      ImGui::SetCursorScreenPos(o);
      ImGui::Dummy(ImVec2(gridW, footerTop + kFooterH - o.y));
      (void)mouse;
      (void)gridHovered;
      ImGui::EndChild();
      // A browser drop that missed every cell is done with either way.
      gArrangePendingBrowserDrop.pending = false;
   }
}
