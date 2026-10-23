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
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/UiAnim.h"
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
      // Index = quantize + 1: -1 follows the transport's setting.
      const char* const kQuantNames[5] = {
         I18N_KEY("Global"), I18N_KEY("None"), I18N_KEY("1 Beat"), I18N_KEY("1 Bar"), I18N_KEY("2 Bars")
      };
      const char* const kModeNames[3] = { I18N_KEY("Trigger"), I18N_KEY("Gate"), I18N_KEY("Toggle") };
      struct LoopChoice { const char* name; double bars; };
      const LoopChoice kLoopChoices[] = {
         { I18N_KEY("Clip Length"), 0.0 }, { I18N_KEY("1/4 Bar"), 0.25 }, { I18N_KEY("1/2 Bar"), 0.5 },
         { I18N_KEY("1 Bar"), 1.0 }, { I18N_KEY("2 Bars"), 2.0 }, { I18N_KEY("4 Bars"), 4.0 }, { I18N_KEY("8 Bars"), 8.0 }
      };

      // The keyboard cursor: the last cell clicked or arrowed to (an empty cell has no clip id to select).
      uint64_t sCurLaneId = 0;
      int sCurRow = -1;
      // Copy / cut buffer for slots (a slot keeps its launch settings; the lane type gates where it can land).
      struct SlotClipboard { bool has = false; int laneType = 0; Arrange::SessionSlot slot; };
      SlotClipboard sSlotClip;

      uint64_t sCtxLaneId = 0;
      int sCtxRow = -1;
      bool sClipCtxRequest = false;
      uint64_t sTrackCtxLaneId = 0;
      constexpr float kAddColW = 40.0f;

      void AddTrackOfType(int type)
      {
         ArrangeEdit([&]() {
            const uint64_t id = Arrange::AddLane(gArrange, type);
            if (Arrange::Lane* l = Arrange::FindLane(gArrange, id))
               l->name = Arrange::UniqueLaneName(gArrange, type == Arrange::kLaneVideo ? "Video" : "Audio", type);
         });
      }

      // One menu for the "+" column and a track header's right-click.
      void DrawTrackMenu()
      {
         if (!MenuParts::BeginPopup("##sessiontrackctx"))
            return;
         if (MenuParts::Item(L("Add Video Track")))
            AddTrackOfType(Arrange::kLaneVideo);
         if (MenuParts::Item(L("Add Audio Track")))
            AddTrackOfType(Arrange::kLaneAudio);
         const Arrange::Lane* l = Arrange::FindLane(gArrange, sTrackCtxLaneId);
         if (l != nullptr)
         {
            MenuParts::Separator();
            const uint64_t lid = l->id;
            const uint64_t gid = l->groupId;
            if (gid == 0)
            {
               if (MenuParts::Item(L("Group This Track")))
                  ArrangeEdit([&]() { Arrange::AddTrackGroup(gArrange, { lid }, "Group"); });
            }
            else if (MenuParts::Item(L("Ungroup Track")))
               ArrangeEdit([&]() { Arrange::RemoveTrackGroup(gArrange, gid, false); });
            if (MenuParts::Item(L("Stop Track"), nullptr, false, ArrangeSessionLaneQuery(lid).playingSlot >= 0))
               ArrangeSessionStopLane(lid);
         }
         MenuParts::EndPopup();
      }
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

      // An unassigned clip in an empty cell, then the click-to-assign flow
      // the Timeline's Add Clip uses.
      void AddSlotClip(uint64_t laneId, int row)
      {
         uint64_t made = 0;
         ArrangeEdit([&]() {
            if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
            {
               EnsureRow(*l, row);
               Arrange::Clip c;
               c.id = made = gArrange.NewId();
               c.start = 0;
               c.length = Arrange::kTicksPerBar;
               l->slots[(size_t)row].clip = c;
               gArrange.revision++;
            }
         });
         if (made != 0)
         {
            gArrangeSel = { made };
            gArrangeSelAnchor = made;
            gArrangeAssigningClipId = made;
            gArrangeFlashClipId = made;
            gArrangeFlashStart = ImGui::GetTime();
         }
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
            // Filled slots use the shared clip menu (DrawArrangeClipContextMenu).
         }
         else
         {
            if (MenuParts::Item(L("Add Clip")))
               AddSlotClip(laneId, row);
            if (lane->groupId != 0 && MenuParts::Item(L("Launch Row in Group")))
               ArrangeSessionLaunchGroupRow(lane->groupId, row);
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
               HelpTip("%s", T("Select one clip on this kind of track first."));
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
         if (MenuParts::SubMenu(L("Launch Quantize")))
         {
            const int cur = row < (int)gArrange.scenes.size() ? gArrange.scenes[(size_t)row].quantize : -1;
            for (int q = -1; q <= 3; q++)
               if (MenuParts::Choice(L(kQuantNames[q + 1]), cur == q) && cur != q)
                  ArrangeEdit([&]() {
                     while ((int)gArrange.scenes.size() <= row)
                        gArrange.scenes.push_back(Arrange::Scene{});
                     gArrange.scenes[(size_t)row].quantize = q;
                     gArrange.revision++;
                  });
            ImGui::EndMenu();
         }
         bool addOn = row < (int)gArrange.scenes.size() && gArrange.scenes[(size_t)row].additive;
         if (MenuParts::Check(L("Keep Other Tracks Playing"), &addOn))
            ArrangeEdit([&]() {
               while ((int)gArrange.scenes.size() <= row)
                  gArrange.scenes.push_back(Arrange::Scene{});
               gArrange.scenes[(size_t)row].additive = addOn;
               gArrange.revision++;
            });
         MenuParts::Separator();
         if (MenuParts::Item(L("Launch Scene")))
            ArrangeSessionLaunchScene(row);
         if (MenuParts::Item(L("Send Scene to Timeline")))
         {
            ArrangeSessionSendSceneToTimeline(row);
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Copies this scene's clips onto the Timeline at the playhead. Render from the Timeline."));
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


   void DrawArrangeSessionGrid(float width)
   {
      const bool isLight = IsThemeLight();
      ImVec2 avail = ImGui::GetContentRegionAvail();
      avail.x = std::min(avail.x, width);
      const int laneCount = (int)gArrange.lanes.size();

      if (laneCount == 0)
      {
         const ImVec2 o = ImGui::GetCursorScreenPos();
         EmptyState::Draw(o, ImVec2(o.x + avail.x, o.y + avail.y), T("No tracks yet"),
                          T("Add a track, then add clips to its cells or drop audio and video files onto them."));
         ImGui::SetCursorScreenPos(ImVec2(o.x + avail.x * 0.5f - 130.0f, o.y + avail.y * 0.5f + 40.0f));
         if (ActionButton::Draw(L("+ Video Track##sessaddv"), ImVec2(125, 0)))
            AddTrackOfType(Arrange::kLaneVideo);
         ImGui::SetCursorScreenPos(ImVec2(o.x + avail.x * 0.5f + 5.0f, o.y + avail.y * 0.5f + 40.0f));
         if (ActionButton::Draw(L("+ Audio Track##sessadda"), ImVec2(125, 0)))
            AddTrackOfType(Arrange::kLaneAudio);
         ImGui::SetCursorScreenPos(o);
         ImGui::Dummy(avail);
         gArrangePendingBrowserDrop.pending = false;
         return;
      }

      int rows = std::max(kMinRows, (int)gArrange.scenes.size());
      for (const Arrange::Lane& l : gArrange.lanes)
         rows = std::max(rows, (int)l.slots.size());
      const float colW = std::clamp((avail.x - kSceneColW - kAddColW) / (float)laneCount, kColMinW, kColMaxW);

      ImGui::BeginChild("##sessiongrid", avail, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 o = ImGui::GetCursorScreenPos();
      const float gridW = kSceneColW + colW * (float)laneCount + kAddColW;
      const float bodyTop = o.y + kHeaderH;
      const float footerTop = bodyTop + kRowH * (float)(rows + 1);   // +1: the Add Scene row
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
         const std::string name = lane.name.empty() ? (lane.type == Arrange::kLaneVideo ? T("Video") : T("Audio"))
                                                    : lane.name;
         const float th = ImGui::GetTextLineHeight();
         dl->PushClipRect(a, ImVec2(b.x - 4.0f, b.y), true);
         dl->AddText(ImVec2(a.x + 9.0f, (a.y + b.y - th) * 0.5f),
                     Arrange::LaneEffectivelyEnabled(gArrange, lane) ? TextCol(0.92f) : TextCol(0.40f), name.c_str());
         dl->PopClipRect();
      }

      for (int li = 0; li < laneCount; li++)
      {
         const float x0 = o.x + kSceneColW + colW * (float)li;
         ImGui::SetCursorScreenPos(ImVec2(x0 + kGap, o.y + kGap));
         ImGui::PushID(li);
         ImGui::InvisibleButton("##hdr", ImVec2(colW - 2.0f * kGap, kHeaderH - 2.0f * kGap), ImGuiButtonFlags_MouseButtonRight);
         ImGui::PopID();
         if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
         {
            sTrackCtxLaneId = gArrange.lanes[(size_t)li].id;
            ImGui::OpenPopup("##sessiontrackctx");
         }
      }
      {
         ImGui::SetCursorScreenPos(ImVec2(o.x + kSceneColW + colW * (float)laneCount + kGap, o.y + kGap));
         if (ActionButton::Draw("+##sessaddtrack", ImVec2(kAddColW - 2.0f * kGap, kHeaderH - 2.0f * kGap)))
         {
            sTrackCtxLaneId = 0;
            ImGui::OpenPopup("##sessiontrackctx");
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Add a video or audio track"));
      }

      // ---- add scene row ----
      {
         ImGui::SetCursorScreenPos(ImVec2(o.x + kGap, bodyTop + kRowH * (float)rows + kGap));
         if (ActionButton::Draw("+ Scene##sessaddscene", ImVec2(kSceneColW - 2.0f * kGap, kRowH - 2.0f * kGap)))
            ArrangeEdit([&]() {
               while ((int)gArrange.scenes.size() < rows + 1)
                  gArrange.scenes.push_back(Arrange::Scene{});
               gArrange.revision++;
            });
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Add a scene (a row of clips that launch together)"));
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
         // Scene state: playing = every filled cell of the row is running (a ring sweeps with the clip),
         // queued = the launch is waiting for the quantize point (the play glyph pulses on the beat).
         bool rowPlaying = false, rowQueued = false;
         float rowProgress = 0.0f;
         for (const Arrange::Lane& l : gArrange.lanes)
            if (SlotOf(l, row) != nullptr)
            {
               const ArrangeSessionSlotState sst = ArrangeSessionQuery(l.id, row);
               if (sst.playing && !rowPlaying)
               {
                  rowPlaying = true;
                  rowProgress = sst.progress;
               }
               rowQueued = rowQueued || sst.queued;
            }
         if (rowPlaying)
            dl->AddRectFilled(a, b, WithAlpha(tok::U32(tok::pal::c_FFFFFFFF), 0.10f), tok::radius_field);
         if (rowPlaying)
            DrawProgressRing(dl, ImVec2(a.x + 13.0f, cy), 7.0f, rowProgress, TextCol(0.95f));
         else
            glyph::DrawPlayerPlay(dl, ImVec2(a.x + 13.0f, cy), tok::icon_sm,
                                  rowQueued ? WithAlpha(TextCol(0.95f), pulse) : (rowHasClip ? TextCol(0.85f) : TextCol(0.25f)), true);
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
            if (ss != nullptr && ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
               ArrangeSessionRelease(laneId, row);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
               sCurLaneId = laneId;
               sCurRow = row;
               // Clicking anywhere but the clip waiting for a node abandons the assign.
               if (gArrangeAssigningClipId != 0 && (ss == nullptr || ss->clip.id != gArrangeAssigningClipId))
               {
                  gArrangeAssigningClipId = 0;
                  gArrangeAssignTargetIds.clear();
               }
               if (ss != nullptr && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
               {
                  // Double-click opens (or closes) the inspector on this clip, as in the Timeline.
                  gArrangeSelAnchor = ss->clip.id;
                  gArrangeSel = { ss->clip.id };
                  gArrangeSettingsPanelTarget = ss->clip.id;
                  gArrangeClipSettingsPanelOpen = !gArrangeClipSettingsPanelOpen;
               }
               else if (ss != nullptr)
               {
                  ArrangeSessionLaunch(laneId, row);
                  gArrangeSel = { ss->clip.id };
                  gArrangeSelAnchor = ss->clip.id;
               }
               else
                  AddSlotClip(laneId, row);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
               sCurLaneId = laneId;
               sCurRow = row;
               sCtxLaneId = laneId;
               sCtxRow = row;
               if (ss != nullptr)
               {
                  gArrangeSel = { ss->clip.id };
                  gArrangeSelAnchor = ss->clip.id;
                  gArrangeCtxClipId = ss->clip.id;
                  sClipCtxRequest = true;
               }
               else
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
               if (gArrangeAssigningClipId == ss->clip.id)
               {
                  // Waiting for a canvas node: the same cue as the Timeline's
                  // target - a pulsing outline, steady under Reduce Motion.
                  const float ap = UiAnim::ReduceMotion() ? 1.0f : 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 5.0f);
                  dl->AddRect(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f),
                              WithAlpha(tok::U32(tok::pal::c_FFFFFFFF), ap), tok::radius_field, 0, 2.5f);
                  HelpTip("%s", T("Click a node on the canvas to assign it. Esc cancels."));
               }
               if (gArrangeFlashClipId == ss->clip.id)
               {
                  const float ft = (float)(ImGui::GetTime() - gArrangeFlashStart);
                  if (ft >= 1.2f)
                     gArrangeFlashClipId = 0;
                  else
                     dl->AddRect(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f),
                                 IM_COL32(255, 255, 255, (int)(255.0f * (1.0f - ft / 1.2f))), tok::radius_field, 0, 2.5f);
               }
               if (st.queued)
                  dl->AddRect(a, b, WithAlpha(tok::U32(tok::pal::c_FFFFFFFF), 0.9f * pulse), tok::radius_field, 0, 1.5f);
               if (st.upNext)
               {
                  // Up next (a follow action is about to start this clip): a steady inner outline and a chevron.
                  dl->AddRect(ImVec2(a.x + 2.0f, a.y + 2.0f), ImVec2(b.x - 2.0f, b.y - 2.0f),
                              WithAlpha(tok::U32(tok::pal::c_FFFFFFFF), 0.75f), tok::radius_field, 0, 1.0f);
                  glyph::DrawChevronDown(dl, ImVec2(b.x - 14.0f, a.y + 8.0f), tok::icon_sm, WithAlpha(ink, 0.9f));
               }
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
               if (hov)
               {
                  // A plus: click adds an unassigned clip to this cell.
                  const ImVec2 pc((a.x + b.x) * 0.5f, cy);
                  const ImU32 pcol = TextCol(0.55f);
                  dl->AddLine(ImVec2(pc.x - 6.0f, pc.y), ImVec2(pc.x + 6.0f, pc.y), pcol, 1.5f);
                  dl->AddLine(ImVec2(pc.x, pc.y - 6.0f), ImVec2(pc.x, pc.y + 6.0f), pcol, 1.5f);
                  HelpTip("%s", isVideo ? T("Click to add a clip. You can also drop a video or image file here.")
                                        : T("Click to add a clip. You can also drop an audio file here."));
               }
            }

            // The keyboard cursor / selected clip.
            if ((ss != nullptr && gArrangeSel.count(ss->clip.id) != 0) || (laneId == sCurLaneId && row == sCurRow))
               dl->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f),
                           WithAlpha(TextCol(1.0f), gSessionClaimedKeys ? 0.9f : 0.45f), tok::radius_field, 0, 1.5f);
            if (ss != nullptr && gArrangeRenamingClipId == ss->clip.id)
            {
               static uint64_t sRenameFocusedId = 0;
               ImGui::SetCursorScreenPos(ImVec2(a.x + 22.0f, a.y + 3.0f));
               ImGui::SetNextItemWidth(std::max(20.0f, b.x - a.x - 28.0f));
               if (sRenameFocusedId != ss->clip.id)
               {
                  ImGui::SetKeyboardFocusHere();
                  sRenameFocusedId = ss->clip.id;
               }
               const bool commit = FieldWell::InputText("##renamingslot", gArrangeRenameClipBuffer, sizeof(gArrangeRenameClipBuffer),
                                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               if (commit || ImGui::IsItemDeactivated())
               {
                  const std::string newName = gArrangeRenameClipBuffer;
                  const uint64_t rid = ss->clip.id;
                  if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                     ArrangeEdit([&]() {
                        if (Arrange::Clip* c = Arrange::FindClip(gArrange, rid))
                           if (c->name != newName)
                           {
                              c->name = newName;
                              gArrange.revision++;
                           }
                     });
                  gArrangeRenamingClipId = 0;
                  gArrangeRenameTargetIds.clear();
                  sRenameFocusedId = 0;
               }
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
      DrawTrackMenu();

      ImGui::SetCursorScreenPos(o);
      ImGui::Dummy(ImVec2(gridW, footerTop + kFooterH - o.y));
      (void)mouse;
      (void)gridHovered;
      ImGui::EndChild();
      // A browser drop that missed every cell is done with either way.
      gArrangePendingBrowserDrop.pending = false;
   }

   bool ArrangeSessionTakeClipCtxRequest()
   {
      const bool r = sClipCtxRequest;
      sClipCtxRequest = false;
      return r;
   }

   namespace
   {
      enum class MenuAction { None, PlaceOnTimeline, Duplicate, Clear };
      struct PendingMenuAction
      {
         MenuAction kind = MenuAction::None;
         uint64_t laneId = 0;
         int row = 0;
      };
      PendingMenuAction sMenuAction;
   }

   // Runs the slot action a menu row queued. Call once the clip menu has ended so nothing still points into the model.
   void ArrangeSessionApplyMenuAction()
   {
      const PendingMenuAction a = sMenuAction;
      sMenuAction = {};
      if (a.kind == MenuAction::None)
         return;
      Arrange::Lane* lane = Arrange::FindLane(gArrange, a.laneId);
      if (lane == nullptr || a.row < 0 || a.row >= (int)lane->slots.size() || !lane->slots[(size_t)a.row].Has())
         return;
      const uint64_t laneId = a.laneId;
      const int row = a.row;
      switch (a.kind)
      {
         case MenuAction::PlaceOnTimeline:
         {
            const Arrange::Tick at = std::max<Arrange::Tick>(0, Arrange::BeatsToTicks(Transport::Instance().Beats()));
            Arrange::Clip c = lane->slots[(size_t)row].clip;
            ArrangeEdit([&]() {
               c.id = 0;
               c.start = at;
               Arrange::PlaceOverwrite(gArrange, laneId, c, nullptr);
            });
            break;
         }
         case MenuAction::Duplicate:
            ArrangeEdit([&]() {
               Arrange::Lane* l = Arrange::FindLane(gArrange, laneId);
               if (l == nullptr)
                  return;
               int dst = row + 1;
               while (dst < (int)l->slots.size() && l->slots[(size_t)dst].Has())
                  dst++;
               if ((int)l->slots.size() <= dst)
                  l->slots.resize((size_t)dst + 1);
               while ((int)gArrange.scenes.size() <= dst)
                  gArrange.scenes.push_back(Arrange::Scene{});
               Arrange::SessionSlot copy = l->slots[(size_t)row];
               copy.clip.id = gArrange.NewId();
               l->slots[(size_t)dst] = copy;
               gArrange.revision++;
            });
            break;
         case MenuAction::Clear:
            ArrangeEdit([&]() {
               if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
               {
                  l->slots[(size_t)row] = Arrange::SessionSlot{};
                  gArrange.revision++;
               }
            });
            gArrangeSel.clear();
            break;
         case MenuAction::None:
            break;
      }
   }

   // One copy of each track's clip for scene `row`, at the playhead, so the scene can be edited and rendered.
   void ArrangeSessionSendSceneToTimeline(int row)
   {
      const Arrange::Tick at = std::max<Arrange::Tick>(0, Arrange::BeatsToTicks(Transport::Instance().Beats()));
      ArrangeEdit([&]() {
         for (size_t i = 0; i < gArrange.lanes.size(); i++)
         {
            const uint64_t laneId = gArrange.lanes[i].id;
            if (row < 0 || (int)gArrange.lanes[i].slots.size() <= row || !gArrange.lanes[i].slots[(size_t)row].Has())
               continue;
            Arrange::Clip c = gArrange.lanes[i].slots[(size_t)row].clip;
            c.id = 0;
            c.start = at;
            c.groupId = 0;
            Arrange::PlaceOverwrite(gArrange, laneId, c, nullptr);
         }
      });
   }

   // Test hook: 0 = Place on Timeline, 1 = Duplicate, 2 = Clear, queued and applied exactly as the menu does it.
   void ArrangeSessionRunActionForTest(int kind, uint64_t laneId, int row)
   {
      sMenuAction = { kind == 0 ? MenuAction::PlaceOnTimeline : (kind == 1 ? MenuAction::Duplicate : MenuAction::Clear), laneId, row };
      ArrangeSessionApplyMenuAction();
   }

   // The rows only a session slot has, appended to the shared clip menu.
   void DrawArrangeSessionSlotMenuItems(uint64_t clipId)
   {
      const Arrange::Loc loc = Arrange::FindSlot(gArrange, clipId);
      if (loc.lane < 0 || loc.slot < 0)
         return;
      const uint64_t laneId = gArrange.lanes[(size_t)loc.lane].id;
      const int row = loc.slot;
      const Arrange::SessionSlot ss = gArrange.lanes[(size_t)loc.lane].slots[(size_t)row];
      if (MenuParts::Item(L("Launch")))
         ArrangeSessionLaunch(laneId, row);
      const uint64_t grp = gArrange.lanes[(size_t)loc.lane].groupId;
      if (grp != 0 && MenuParts::Item(L("Launch Row in Group")))
         ArrangeSessionLaunchGroupRow(grp, row);
      if (MenuParts::SubMenu(L("Follow Action")))
      {
         for (int a = 0; a < Arrange::kFollowCount; a++)
            if (MenuParts::Choice(L(kFollowNames[a]), ss.followAction == a) && ss.followAction != a)
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
      if (MenuParts::SubMenu(L("Follow After"), ss.followAction != Arrange::kFollowNone))
      {
         for (int bars : kFollowBarChoices)
         {
            char label[32];
            snprintf(label, sizeof(label), bars == 1 ? T("%d bar") : T("%d bars"), bars);
            if (MenuParts::Choice(label, ss.followBars == bars) && ss.followBars != bars)
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
      MenuParts::Separator();
      bool loopOn = ss.loop;
      if (MenuParts::Check(L("Loop"), &loopOn))
         ArrangeEdit([&]() {
            if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
            {
               l->slots[(size_t)row].loop = loopOn;
               gArrange.revision++;
            }
         });
      if (MenuParts::SubMenu(L("Loop Length")))
      {
         for (const LoopChoice& lc : kLoopChoices)
         {
            const Arrange::Tick want = (Arrange::Tick)(lc.bars * (double)Arrange::kTicksPerBar);
            if (MenuParts::Choice(L(lc.name), ss.loopTicks == want) && ss.loopTicks != want)
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                  {
                     l->slots[(size_t)row].loopTicks = want;
                     gArrange.revision++;
                  }
               });
         }
         ImGui::EndMenu();
      }
      if (MenuParts::SubMenu(L("Launch Quantize")))
      {
         for (int q = -1; q <= 3; q++)
            if (MenuParts::Choice(L(kQuantNames[q + 1]), ss.quantize == q) && ss.quantize != q)
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                  {
                     l->slots[(size_t)row].quantize = q;
                     gArrange.revision++;
                  }
               });
         ImGui::EndMenu();
      }
      if (MenuParts::SubMenu(L("Launch Mode")))
      {
         for (int m = 0; m < 3; m++)
            if (MenuParts::Choice(L(kModeNames[m]), ss.launchMode == m) && ss.launchMode != m)
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                  {
                     l->slots[(size_t)row].launchMode = m;
                     gArrange.revision++;
                  }
               });
         ImGui::EndMenu();
      }
      MenuParts::Separator();
      // Destructive rows are queued, not run: the shared menu still holds pointers into the slot and lane vectors
      // and keeps reading them for the rest of this frame. ArrangeSessionApplyMenuAction() runs them after it closes.
      if (MenuParts::Item(L("Place on Timeline at Playhead")))
         sMenuAction = {MenuAction::PlaceOnTimeline, laneId, row};
      if (MenuParts::Item(L("Duplicate Slot")))
         sMenuAction = {MenuAction::Duplicate, laneId, row};
      if (MenuParts::Item(L("Clear Slot")))
         sMenuAction = {MenuAction::Clear, laneId, row};
   }

   namespace
   {
      bool sSessionShowMonitor = true;

      // The video the launched clips produce, drawn at the panel's own size. The composite itself runs after the cook
      // loop into gArrangeMonitorTarget; this only asks for a size and shows the texture.
      void DrawSessionMonitor(float width)
      {
         PushDockedPanelStyle(/*isChild=*/true);
         ImGui::BeginChild("##sessionmonitor", ImVec2(width, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
         PopDockedPanelStyle();
         const ImVec2 avail = ImGui::GetContentRegionAvail();
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 br(origin.x + avail.x, origin.y + avail.y);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         DrawCheckerboardBackdrop(dl, origin, br);
         if (CountActiveArrangeVideoClips(Transport::Instance().Beats()) > 0)
         {
            gArrangeMonitorTarget.requestW = std::max(16, (int)avail.x);
            gArrangeMonitorTarget.requestH = std::max(16, (int)avail.y);
            const unsigned int tex = gArrangeMonitorTarget.result.tex;
            if (tex != 0)
               dl->AddImage((ImTextureID)(intptr_t)tex, origin, br, ImVec2(0, 1), ImVec2(1, 0));
         }
         ImGui::Dummy(avail);
         ImGui::EndChild();
      }

      // ---- keyboard: the Session panel's shortcuts, live only while it owns the keys (a click inside it claims
      // them; a click in the Timeline or on the canvas releases them). Every destructive one goes through the same
      // queued action the menus use, so a menu or inspector showing the clip never holds a stale pointer.
      void SessionCursorFromSelection()
      {
         const uint64_t sid = ArrangeSelectedSlotId();
         if (sid == 0)
            return;
         const Arrange::Loc loc = Arrange::FindSlot(gArrange, sid);
         if (loc.lane >= 0 && loc.slot >= 0)
         {
            sCurLaneId = gArrange.lanes[(size_t)loc.lane].id;
            sCurRow = loc.slot;
         }
      }

      void SelectCursorCell()
      {
         const Arrange::Lane* l = Arrange::FindLane(gArrange, sCurLaneId);
         const Arrange::SessionSlot* ss = l != nullptr ? SlotOf(*l, sCurRow) : nullptr;
         gArrangeSel.clear();
         if (ss != nullptr)
         {
            gArrangeSel = { ss->clip.id };
            gArrangeSelAnchor = ss->clip.id;
         }
      }

      void HandleSessionKeys()
      {
         const ImGuiIO& io = ImGui::GetIO();
         if (!gSessionClaimedKeys || io.WantTextInput || ArrangeFieldHot() || ArrangeRenderBusy() || gArrangeRenamingClipId != 0 ||
             gArrangeGestureOpen || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            return;
         if (gArrange.lanes.empty())
            return;
         const bool cmd = io.KeySuper || io.KeyCtrl;
         auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
         SessionCursorFromSelection();
         int laneIdx = -1;
         for (size_t i = 0; i < gArrange.lanes.size(); i++)
            if (gArrange.lanes[i].id == sCurLaneId)
               laneIdx = (int)i;
         if (laneIdx < 0)
         {
            laneIdx = 0;
            sCurLaneId = gArrange.lanes[0].id;
            if (sCurRow < 0)
               sCurRow = 0;
         }
         const int maxRow = std::max(kMinRows, (int)gArrange.scenes.size()) - 1;
         sCurRow = std::clamp(sCurRow, 0, maxRow);
         const Arrange::Lane& lane = gArrange.lanes[(size_t)laneIdx];
         const Arrange::SessionSlot* ss = SlotOf(lane, sCurRow);

         if (!cmd && !io.KeyAlt && (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_RightArrow) ||
                                    pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow)))
         {
            const int dx = (pressed(ImGuiKey_RightArrow) ? 1 : 0) - (pressed(ImGuiKey_LeftArrow) ? 1 : 0);
            const int dy = (pressed(ImGuiKey_DownArrow) ? 1 : 0) - (pressed(ImGuiKey_UpArrow) ? 1 : 0);
            const int nl = std::clamp(laneIdx + dx, 0, (int)gArrange.lanes.size() - 1);
            sCurLaneId = gArrange.lanes[(size_t)nl].id;
            sCurRow = std::clamp(sCurRow + dy, 0, maxRow);
            SelectCursorCell();
         }
         else if (!cmd && !io.KeyAlt && (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)))
         {
            if (io.KeyShift)
               ArrangeSessionLaunchScene(sCurRow);
            else if (ss != nullptr)
               ArrangeSessionLaunch(sCurLaneId, sCurRow);
            else
               AddSlotClip(sCurLaneId, sCurRow);
         }
         else if (cmd && pressed(ImGuiKey_C) && ss != nullptr)
         {
            sSlotClip = { true, lane.type, *ss };
         }
         else if (cmd && pressed(ImGuiKey_X) && ss != nullptr)
         {
            sSlotClip = { true, lane.type, *ss };
            sMenuAction = { MenuAction::Clear, sCurLaneId, sCurRow };
            ArrangeSessionApplyMenuAction();
         }
         else if (cmd && pressed(ImGuiKey_V) && sSlotClip.has && sSlotClip.laneType == lane.type)
         {
            const uint64_t laneId = sCurLaneId;
            const int row = sCurRow;
            Arrange::SessionSlot copy = sSlotClip.slot;
            uint64_t made = 0;
            ArrangeEdit([&]() {
               if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
               {
                  EnsureRow(*l, row);
                  copy.clip.id = made = gArrange.NewId();
                  l->slots[(size_t)row] = copy;
                  gArrange.revision++;
               }
            });
            if (made != 0)
            {
               gArrangeSel = { made };
               gArrangeSelAnchor = made;
            }
         }
         else if (cmd && pressed(ImGuiKey_D) && ss != nullptr)
         {
            sMenuAction = { MenuAction::Duplicate, sCurLaneId, sCurRow };
            ArrangeSessionApplyMenuAction();
         }
         else if (!cmd && (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)) && ss != nullptr)
         {
            sMenuAction = { MenuAction::Clear, sCurLaneId, sCurRow };
            ArrangeSessionApplyMenuAction();
         }
         else if (cmd && !io.KeyShift && pressed(ImGuiKey_R) && ss != nullptr)
         {
            gArrangeRenamingClipId = ss->clip.id;
            gArrangeRenameTargetIds.clear();
            snprintf(gArrangeRenameClipBuffer, sizeof(gArrangeRenameClipBuffer), "%s", SlotLabel(ss->clip).c_str());
         }
         else if (!cmd && !io.KeyShift && !io.KeyAlt && (pressed(ImGuiKey_0) || pressed(ImGuiKey_Keypad0)) && ss != nullptr)
         {
            const uint64_t cid = ss->clip.id;
            ArrangeEdit([&]() {
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, cid))
               {
                  c->enabled = !c->enabled;
                  gArrange.revision++;
               }
            });
         }
         else if (cmd && io.KeyShift && pressed(ImGuiKey_I))
         {
            ArrangeEdit([&]() {
               gArrange.scenes.push_back(Arrange::Scene{});
               gArrange.revision++;
            });
         }
         else if (cmd && pressed(ImGuiKey_Period))
            ArrangeSessionStopAll();
         else if (!cmd && pressed(ImGuiKey_Escape))
         {
            if (gArrangeAssigningClipId != 0)
            {
               gArrangeAssigningClipId = 0;
               gArrangeAssignTargetIds.clear();
            }
            else
            {
               gArrangeSel.clear();
               sCurRow = -1;
            }
         }
      }

      void DrawSessionPanelContent()
      {
         HandleSessionKeys();
         ArrangePruneSelection();
         ArrangePollMediaImports();
         const ImVec2 panelSize = ImGui::GetContentRegionAvail();

         // Toolbar: what only Session needs. Right-click it for the dock side and Close.
         const float toolbarTop = ImGui::GetCursorScreenPos().y;
         ImGui::AlignTextToFramePadding();
         ImGui::TextUnformatted(T("Session"));
         ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + tok::space_4, ImGui::GetItemRectMin().y));
         if (ChipButton::Draw(sSessionShowMonitor ? "Monitor##sessmon" : "Monitor Off##sessmon", sSessionShowMonitor, tok::tile))
            sSessionShowMonitor = !sSessionShowMonitor;
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Show the video the launched clips produce."));
         ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + tok::space_1, ImGui::GetItemRectMin().y));
         if (ChipButton::Draw(gArrangeClipSettingsPanelOpen ? "Inspector##sessinsp" : "Inspector Off##sessinsp", gArrangeClipSettingsPanelOpen, tok::tile))
            gArrangeClipSettingsPanelOpen = !gArrangeClipSettingsPanelOpen;
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Settings of the selected clip. Double-click a clip to open it."));
         const float toolbarBottom = ImGui::GetItemRectMax().y;
         {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const ImVec2 o = ImGui::GetWindowPos();
            if (m.y >= toolbarTop && m.y < toolbarBottom && m.x >= o.x && m.x < o.x + panelSize.x &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                !ImGui::IsPopupOpen("##sessiondockctx"))
               ImGui::OpenPopup("##sessiondockctx");
         }
         if (MenuParts::BeginPopup("##sessiondockctx"))
         {
            if (MenuParts::Item(L("Close Session")))
               gSessionPanelOpen = false;
            MenuParts::EndPopup();
         }
         {
            const float ly = toolbarBottom + 2.0f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(ImGui::GetWindowPos().x, ly), ImVec2(ImGui::GetWindowPos().x + panelSize.x, ly),
                                                ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, ly + 4.0f));
         }

         const bool isWide = panelSize.x >= 720.0f;
         const bool showMon = isWide && sSessionShowMonitor;
         const float monW = isWide ? std::clamp(panelSize.x * 0.28f, 180.0f, 320.0f) : 0.0f;
         constexpr float kInspectorW = 244.0f;
         // The inspector shows the selected slot clip; it opens from a cell (double-click) and closes with its own X.
         const bool showInspector = gArrangeClipSettingsPanelOpen && ArrangeSelectedSlotId() != 0;
         const float spacing = ImGui::GetStyle().ItemSpacing.x;
         const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
         const float rowW = ImGui::GetContentRegionAvail().x;
         if (showMon)
            DrawSessionMonitor(monW);
         const float gridX = rowOrigin.x + (showMon ? monW + spacing : 0.0f);
         ImGui::SetCursorScreenPos(ImVec2(gridX, rowOrigin.y));
         const float gridW = std::max(1.0f, rowW - (gridX - rowOrigin.x) - (showInspector ? kInspectorW + spacing : 0.0f));
         DrawArrangeSessionGrid(gridW);
         if (showInspector)
         {
            ImGui::SetCursorScreenPos(ImVec2(gridX + gridW + spacing, rowOrigin.y));
            DrawArrangeClipSettingsChild(kInspectorW);
         }
         DrawArrangeClipContextMenu(ArrangeSessionTakeClipCtxRequest());
      }
   }

   void DrawSessionPanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = PanelFrame::kGap;
      const int dock = ArrangePanelDock();   // 0 = bottom, 3 = top
      const bool gripFirst = dock == 0;
      ImGui::BeginChild(id, size, false);
      gSessionPanelRectMin = ImGui::GetWindowPos();
      gSessionPanelRectMax = ImVec2(gSessionPanelRectMin.x + ImGui::GetWindowSize().x, gSessionPanelRectMin.y + ImGui::GetWindowSize().y);
      const ImVec2 inner = ImGui::GetContentRegionAvail();
      auto grip = [&]()
      {
         ImGui::InvisibleButton("##sessionpanelgrip", ImVec2(std::max(1.0f, inner.x), kGrip));
         DrawPanelSeam(false, gripFirst);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsItemActive())
         {
            gSessionPanelHeight += gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y;
            gSessionPanelHeight = std::max(kSessionPanelMinHeight, gSessionPanelHeight);
         }
      };
      if (gripFirst)
         grip();
      const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
      const ImVec2 cardSize(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
      PanelFrame::Insets in;
      if (gripFirst)
         in.t = 0.0f;
      else
         in.b = 0.0f;
      PanelFrame::BeginCard("##sessionpanelinnercontent", cardSize, in);
      DrawSessionPanelContent();
      PanelFrame::EndCard();
      if (!gripFirst)
         grip();
      const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
      ImGui::EndChild();
      ImGui::GetStyle().ItemSpacing = savedSpacing;
   }
}
