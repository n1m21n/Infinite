// Session view grid: the Timeline panel's clip launcher. Tracks are columns,
// scenes are rows. Launch logic lives in ArrangeSession.cpp; this file draws
// the grid and turns clicks, menus and file drops into model edits.
//
// The idea comes from Ricardo Palmieri's Infinite-Turbo fork; this is a fresh
// implementation.
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/ColourMath.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/Thumb.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/Keymap.h"
#include "app/ui/design/components/PillGroup.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/UiFmt.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/SectionCard.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiScope.h"
#include "app/AppShared.h"

namespace app
{
   namespace
   {
      constexpr float kSceneColW = tok::arrange_session_grid_scene_col_w;
      constexpr float kColMinW = tok::arrange_session_grid_col_min_w;
      constexpr float kColMaxW = tok::arrange_session_grid_col_max_w;
      constexpr float kHeaderH = tok::row_h;
      constexpr float kRowH = tok::cell_h;   // a clip or scene cell: taller than a list row so a clip name and its state read
      constexpr float kFooterH = tok::row_h;
      constexpr float kGap = tok::space_0_5;
      constexpr int kMinRows = tok::arrange_session_grid_min_rows;

      const char* const kFollowNames[Arrange::kFollowCount] = {
         I18N_KEY("No Follow Action"), I18N_KEY("Next"), I18N_KEY("Previous"), I18N_KEY("Random"), I18N_KEY("Stop")
      };
      const int kFollowBarChoices[] = { 1, 2, 4, 8, 16, 32, 64 };
      // Index = quantize + 1: -1 follows the Session's own quantize (the toolbar's).
      const char* const kQuantNames[5] = {
         I18N_KEY("Session Default"), I18N_KEY("None"), I18N_KEY("1 Beat"), I18N_KEY("1 Bar"), I18N_KEY("2 Bars")
      };
      const char* const kModeNames[2] = { I18N_KEY("Trigger"), I18N_KEY("Gate") };
      // Scene "Then Play Next Scene" timing: 0 = the scene's length.
      const int kSceneFollowBars[] = { 0, 1, 2, 4, 8, 16 };
      struct LoopChoice { const char* name; double bars; };
      const LoopChoice kLoopChoices[] = {
         { I18N_KEY("Clip Length"), 0.0 }, { I18N_KEY("1/4 Bar"), 0.25 }, { I18N_KEY("1/2 Bar"), 0.5 },
         { I18N_KEY("1 Bar"), 1.0 }, { I18N_KEY("2 Bars"), 2.0 }, { I18N_KEY("4 Bars"), 4.0 }, { I18N_KEY("8 Bars"), 8.0 }
      };

      // The keyboard cursor: the last cell clicked or arrowed to (an empty cell has no clip id to select).
      uint64_t sCurLaneId = 0;
      int sCurRow = -1;
      // The selected scene (a click on the scene column); -1 = a cell has the keys.
      int sSelScene = -1;
      // Copy / cut buffer for slots (a slot keeps its launch settings; the lane type gates where it can land).
      struct SlotClipboard { bool has = false; int laneType = 0; Arrange::SessionSlot slot; };
      SlotClipboard sSlotClip;
      // A press on a clip: a Trigger clip launches on release unless the press became a drag, which moves the
      // clip to the cell it is dropped on (Option copies it).
      struct SlotDrag { uint64_t laneId = 0; int row = -1; bool dragging = false; bool launchOnRelease = false; };
      SlotDrag sDrag;

      uint64_t sCtxLaneId = 0;
      int sCtxRow = -1;
      bool sClipCtxRequest = false;
      uint64_t sTrackCtxLaneId = 0;
      uint64_t sTrackRenameLaneId = 0;   // the track header showing its name field
      char sTrackNameBuf[64] = {};
      constexpr float kAddColW = tok::arrange_session_grid_add_col_w;

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
         if (!gArrange.lanes.empty() && MenuParts::TestOpen("##sessiontrackctx"))
         {
            sTrackCtxLaneId = gArrange.lanes.front().id;
            ImGui::OpenPopup("##sessiontrackctx");
         }
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
            MenuParts::Separator();
            if (MenuParts::Item(L("Rename Track"), T("Double-click")))
            {
               sTrackRenameLaneId = lid;
               snprintf(sTrackNameBuf, sizeof(sTrackNameBuf), "%s", l->name.c_str());
            }
            if (MenuParts::Item(L("Duplicate Track")))
               ArrangeEdit([&]() { Arrange::DuplicateLane(gArrange, lid); });
            if (MenuParts::Item(L("Delete Track")))
            {
               ArrangeSessionStopLane(lid);
               ArrangeEdit([&]() { Arrange::RemoveLane(gArrange, lid); });
               gArrangeSel.clear();
            }
         }
         MenuParts::EndPopup();
      }
      int sSceneCtxRow = -1;
      // Row and slot menus are requested inside PushID scopes and opened next to their BeginPopup,
      // so the popup ID matches.
      bool sSceneCtxOpen = false;
      bool sSlotCtxOpen = false;
      int sTrackDragFrom = -1;   // track column being dragged to a new place
      int sSceneDragFrom = -1;   // scene row being dragged to a new place
      char sSceneNameBuf[64] = {};
      float sSceneTempoEdit = 0.0f;

      using tok::WithAlpha;

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
            return isVideo ? tok::Role(tok::col::role_expression) : tok::Role(tok::col::viz_teal);
         return isVideo ? tok::Role(tok::col::role_expression, tok::arrange_session_grid_slot_fill_alpha) : tok::Role(tok::col::state_ok_deep_mid, tok::arrange_session_grid_slot_fill_alpha);
      }

      ImU32 LabelOn(ImU32 fill)
      {
         const ImVec4 f = ImGui::ColorConvertU32ToFloat4(fill);
         const float lum = ColourMath::Luma601(f.x, f.y, f.z) * 255.0f;
         return lum > 150.0f ? tok::Role(tok::col::surface_deep) : tok::Role(tok::col::ink_white);
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
         dl->AddCircle(c, r, WithAlpha(col, tok::arrange_session_grid_ring_alpha), tok::arrange_session_grid_progress_ring_segments, tok::arrange_session_grid_progress_ring_stroke);
         const float a0 = -IM_PI * 0.5f;
         const float a1 = a0 + std::clamp(progress, 0.0f, 1.0f) * 2.0f * IM_PI;
         if (a1 > a0 + 0.02f)
         {
            dl->PathArcTo(c, r, a0, a1, tok::arrange_session_grid_progress_ring_segments);
            dl->PathStroke(col, 0, tok::arrange_session_grid_progress_ring_stroke);
         }
      }

      void DrawStopSquare(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
      {
         dl->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f), col, tok::arrange_session_grid_stop_square_radius);
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
         char num[16];
         snprintf(num, sizeof(num), "%d", ss.followBars);
         const ImVec2 ns = ImGui::CalcTextSize(num);
         const float gx = rightCentre.x - ns.x - tok::arrange_session_grid_gx_gap;
         const ImVec2 gc(gx, rightCentre.y);
         const float gs = tok::icon_control;
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
         if (lane == nullptr || lane->type == Arrange::kLaneMidi || !ArrangeMediaKindForPath(path, kind))
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

      // Pastes the copied slot (clip and launch settings) into a cell, replacing what is there.
      void PasteSlotAt(uint64_t laneId, int row)
      {
         const Arrange::Lane* target = Arrange::FindLane(gArrange, laneId);
         if (!sSlotClip.has || target == nullptr || sSlotClip.laneType != target->type || row < 0)
            return;
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
            gArrangeFlashClipId = made;
            gArrangeFlashStart = ImGui::GetTime();
         }
      }

      void DrawSlotMenu()
      {
         // Self-test: the first empty cell of the first track, as a right-click on it would.
         if (!gArrange.lanes.empty() && MenuParts::TestOpen("##sessionslotctx"))
         {
            const Arrange::Lane& l = gArrange.lanes.front();
            int row = 0;
            while (row < (int)l.slots.size() && l.slots[(size_t)row].Has())
               row++;
            sCtxLaneId = l.id;
            sCtxRow = row;
            sSlotCtxOpen = true;
         }
         if (sSlotCtxOpen)
            ImGui::OpenPopup("##sessionslotctx");
         sSlotCtxOpen = false;
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
            const bool canPaste = sSlotClip.has && sSlotClip.laneType == lane->type;
            if (MenuParts::Item(L("Paste"), MODKEY "+V", false, canPaste))
               PasteSlotAt(laneId, row);
            if (lane->groupId != 0 && MenuParts::Item(L("Launch Row in Group")))
               ArrangeSessionLaunchGroupRow(lane->groupId, row);
            const Arrange::Clip* sel = SelectedClipOfType(lane->type);
            if (MenuParts::Item(L("Paste Selected Timeline Clip"), nullptr, false, sel != nullptr) && sel != nullptr)
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

      void InsertSceneAt(int row)
      {
         ArrangeEdit([&]() {
            while ((int)gArrange.scenes.size() < row)
               gArrange.scenes.push_back(Arrange::Scene{});
            gArrange.scenes.insert(gArrange.scenes.begin() + row, Arrange::Scene{});
            for (Arrange::Lane& l : gArrange.lanes)
               if ((int)l.slots.size() > row)
                  l.slots.insert(l.slots.begin() + row, Arrange::SessionSlot{});
            gArrange.revision++;
         });
         ArrangeSessionShiftRows(row, 1);
      }

      void DuplicateScene(int row)
      {
         if (row < 0 || row >= (int)gArrange.scenes.size())
            return;
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
         ArrangeSessionShiftRows(row + 1, 1);
         sSelScene = row + 1;
      }

      // Drag a scene to a new row: its name, settings and every track's clip travel with it.
      void MoveScene(int from, int to)
      {
         if (from < 0 || to < 0 || from == to)
            return;
         ArrangeEdit([&]() {
            // Empty placeholder rows become real scenes so they can be dragged too.
            while ((int)gArrange.scenes.size() <= std::max(from, to))
               gArrange.scenes.push_back(Arrange::Scene{});
            auto move = [&](auto& v) {
               if ((int)v.size() <= std::max(from, to))
                  v.resize((size_t)std::max(from, to) + 1);
               auto item = std::move(v[(size_t)from]);
               v.erase(v.begin() + from);
               v.insert(v.begin() + to, std::move(item));
            };
            move(gArrange.scenes);
            for (Arrange::Lane& l : gArrange.lanes)
               if ((int)l.slots.size() > from)
                  move(l.slots);
            gArrange.revision++;
         });
         ArrangeSessionMoveRow(from, to);
         gArrangeSel.clear();
         sSelScene = to;
      }

      void DeleteScene(int row)
      {
         if (row < 0 || row >= (int)gArrange.scenes.size())
            return;
         ArrangeEdit([&]() {
            gArrange.scenes.erase(gArrange.scenes.begin() + row);
            for (Arrange::Lane& l : gArrange.lanes)
               if ((int)l.slots.size() > row)
                  l.slots.erase(l.slots.begin() + row);
            gArrange.revision++;
         });
         ArrangeSessionShiftRows(row, -1);
         gArrangeSel.clear();
         sSelScene = gArrange.scenes.empty() ? -1 : std::min(row, (int)gArrange.scenes.size() - 1);
      }

      void EditScene(int row, const std::function<void(Arrange::Scene&)>& fn)
      {
         ArrangeEdit([&]() {
            while ((int)gArrange.scenes.size() <= row)
               gArrange.scenes.push_back(Arrange::Scene{});
            fn(gArrange.scenes[(size_t)row]);
            gArrange.revision++;
         });
      }

      void DrawSceneMenu()
      {
         if (!gArrange.scenes.empty() && MenuParts::TestOpen("##sessionscenectx"))
         {
            sSelScene = sSceneCtxRow = 0;
            snprintf(sSceneNameBuf, sizeof(sSceneNameBuf), "%s", gArrange.scenes.front().name.c_str());
            sSceneTempoEdit = gArrange.scenes.front().tempo;
            sSceneCtxOpen = true;
         }
         if (sSceneCtxOpen)
            ImGui::OpenPopup("##sessionscenectx");
         sSceneCtxOpen = false;
         if (!MenuParts::BeginPopup("##sessionscenectx"))
            return;
         const int row = sSceneCtxRow;
         if (row < 0)
         {
            MenuParts::EndPopup();
            return;
         }
         ImGui::SetNextItemWidth(tok::arrange_session_grid_field_w);
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
         ImGui::SetNextItemWidth(tok::arrange_session_grid_field_w);
         char tempoFmt[64];
         if (sSceneTempoEdit > 0.0f)
         {
            char bpm[48];
            FormParts::UnitReadout(bpm, sizeof bpm, UiFmt::Bpm, sSceneTempoEdit);
            std::snprintf(tempoFmt, sizeof tempoFmt, T("%s on launch"), bpm);
         }
         else
            std::snprintf(tempoFmt, sizeof tempoFmt, "%s", T("Tempo: unchanged"));
         ImGui::DragFloat("##scenetempo", &sSceneTempoEdit, params::arrange_session_grid::scene_tempo_edit_speed, params::arrange_session_grid::scene_tempo_edit_lo, params::arrange_session_grid::scene_tempo_edit_hi, tempoFmt, ImGuiSliderFlags_NoRoundToFormat);
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
            EditScene(row, [&](Arrange::Scene& sc) { sc.additive = addOn; });
         // Scenes in order: when this one has played, the next one launches (Ableton's scene follow action).
         bool nextOn = row < (int)gArrange.scenes.size() && gArrange.scenes[(size_t)row].followNext;
         if (MenuParts::Check(L("Then Play Next Scene"), &nextOn))
            EditScene(row, [&](Arrange::Scene& sc) { sc.followNext = nextOn; });
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("When this scene has played, the next scene launches. Turn it on for every scene to play them in order."));
         if (MenuParts::SubMenu(L("Next Scene After"), nextOn))
         {
            const int cur = row < (int)gArrange.scenes.size() ? gArrange.scenes[(size_t)row].followBars : 0;
            for (int bars : kSceneFollowBars)
            {
               char label[32];
               if (bars == 0)
                  snprintf(label, sizeof(label), "%s", T("Scene Length"));
               else
                  snprintf(label, sizeof(label), bars == 1 ? T("%d bar") : T("%d bars"), bars);
               if (MenuParts::Choice(label, cur == bars) && cur != bars)
                  EditScene(row, [&](Arrange::Scene& sc) { sc.followBars = bars; });
            }
            ImGui::EndMenu();
         }
         MenuParts::Separator();
         const ArrangeSessionSceneState sceneSt = ArrangeSessionSceneQuery(row);
         if (MenuParts::Item(sceneSt.playing ? L("Stop Scene") : L("Launch Scene")))
            ArrangeSessionLaunchScene(row);
         if (MenuParts::Item(L("Send Scene to Timeline")))
         {
            ArrangeSessionSendSceneToTimeline(row);
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Copies this scene's clips onto the Timeline at the playhead. Render from the Timeline."));
         if (MenuParts::Item(L("Insert Scene Above")))
            InsertSceneAt(row);
         const bool real = row < (int)gArrange.scenes.size();
         if (MenuParts::Item(L("Duplicate Scene"), MODKEY "+D", false, real))
            DuplicateScene(row);
         if (MenuParts::Item(L("Delete Scene"), "Backspace", false, real))
            DeleteScene(row);
         MenuParts::EndPopup();
      }
   }


   void DrawArrangeSessionGrid(float width)
   {
      const bool isLight = IsThemeLight();
      ImVec2 avail = Ui::Avail();
      avail.x = std::min(avail.x, width);
      const int laneCount = (int)gArrange.lanes.size();

      if (laneCount == 0)
      {
         const ImVec2 o = ImGui::GetCursorScreenPos();
         EmptyState::Draw(o, ImVec2(o.x + avail.x, o.y + avail.y), T("No tracks yet"),
                          T("Add a track, then add clips to its cells or drop audio and video files onto them."));
         ImGui::SetCursorScreenPos(ImVec2(o.x + avail.x * 0.5f - tok::arrange_session_grid_gap, o.y + avail.y * 0.5f + tok::arrange_session_grid_y));
         if (ActionButton::Draw(L("+ Video Track##sessaddv"), ImVec2(tok::arrange_session_grid_video_track_x, 0)))
            AddTrackOfType(Arrange::kLaneVideo);
         ImGui::SetCursorScreenPos(ImVec2(o.x + avail.x * 0.5f + tok::space_1_25, o.y + avail.y * 0.5f + tok::arrange_session_grid_y));
         if (ActionButton::Draw(L("+ Audio Track##sessadda"), ImVec2(tok::arrange_session_grid_video_track_x, 0)))
            AddTrackOfType(Arrange::kLaneAudio);
         ImGui::SetCursorScreenPos(o);
         Ui::Claim(avail);
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

      // ---- corner: the scene column's header (Record and Stop All live in the toolbar) ----
      {
         const float th = ImGui::GetTextLineHeight();
         dl->AddText(ImVec2(o.x + kGap + tok::arrange_session_grid_text_x_sm, o.y + (kHeaderH - th) * 0.5f), TextCol(tok::arrange_session_grid_text_shade_md), T("Scenes"));
      }

      // ---- track headers ----
      for (int li = 0; li < laneCount; li++)
      {
         const Arrange::Lane& lane = gArrange.lanes[(size_t)li];
         const float x0 = o.x + kSceneColW + colW * (float)li;
         const ImVec2 a(x0 + kGap, o.y + kGap), b(x0 + colW - kGap, o.y + kHeaderH - kGap);
         dl->AddRectFilled(a, b, TextCol(tok::arrange_session_grid_fill_shade_md), tok::radius_control);
         const std::string name = lane.name.empty() ? (lane.type == Arrange::kLaneVideo ? T("Video") : T("Audio"))
                                                    : lane.name;
         const float th = ImGui::GetTextLineHeight();
         if (lane.id == sTrackRenameLaneId)
            continue;
         dl->PushClipRect(a, ImVec2(b.x - tok::space_1, b.y), true);
         dl->AddText(ImVec2(a.x + tok::arrange_session_grid_text_x_sm, (a.y + b.y - th) * 0.5f),
                     Arrange::LaneEffectivelyEnabled(gArrange, lane) ? TextCol(tok::arrange_session_grid_text_shade_xl) : TextCol(tok::arrange_session_grid_text_shade_xs), name.c_str());
         dl->PopClipRect();
      }

      for (int li = 0; li < laneCount; li++)
      {
         const float x0 = o.x + kSceneColW + colW * (float)li;
         ImGui::SetCursorScreenPos(ImVec2(x0 + kGap, o.y + kGap));
         ImGui::PushID(li);
         UiInteract::Hit("##hdr", ImVec2(colW - tok::arrange_session_grid_ratio * kGap, kHeaderH - tok::arrange_session_grid_ratio * kGap),
                                ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonLeft);
         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, tok::drag_threshold_tile))
            sTrackDragFrom = li;
         if (ImGui::IsItemHovered() && sTrackDragFrom < 0)
            HelpTip("%s", T("Drag to reorder tracks. Right-click for more."));
         ImGui::PopID();
         const uint64_t hdrLaneId = gArrange.lanes[(size_t)li].id;
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            sTrackRenameLaneId = hdrLaneId;
            snprintf(sTrackNameBuf, sizeof(sTrackNameBuf), "%s", gArrange.lanes[(size_t)li].name.c_str());
         }
         if (MenuParts::ContextClicked())
         {
            sTrackCtxLaneId = hdrLaneId;
            ImGui::OpenPopup("##sessiontrackctx");
         }
         if (hdrLaneId == sTrackRenameLaneId)
         {
            static uint64_t sFocused = 0;
            ImGui::SetCursorScreenPos(ImVec2(x0 + kGap + tok::space_1, o.y + kGap + tok::space_0_75));
            ImGui::SetNextItemWidth(std::max(tok::arrange_session_grid_min_sm, colW - tok::arrange_session_grid_ratio * kGap - tok::space_2));
            if (sFocused != hdrLaneId)
            {
               UiInteract::FocusHere();
               sFocused = hdrLaneId;
            }
            const bool commit = FieldWell::InputText("##sessrenametrack", sTrackNameBuf, sizeof(sTrackNameBuf),
                                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (commit || ImGui::IsItemDeactivated())
            {
               const std::string newName = sTrackNameBuf;
               if (!Keymap::Cancel(false))
                  ArrangeEdit([&]() {
                     if (Arrange::Lane* ln = Arrange::FindLane(gArrange, hdrLaneId))
                        if (ln->name != newName)
                        {
                           ln->name = newName;
                           gArrange.revision++;
                        }
                  });
               sTrackRenameLaneId = 0;
               sFocused = 0;
            }
         }
      }
      {
         ImGui::SetCursorScreenPos(ImVec2(o.x + kSceneColW + colW * (float)laneCount + kGap, o.y + kGap));
         if (ActionButton::Draw("+##sessaddtrack", ImVec2(kAddColW - tok::arrange_session_grid_ratio * kGap, kHeaderH - tok::arrange_session_grid_ratio * kGap)))
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
         if (ActionButton::Draw(L("+ Scene##sessaddscene"), ImVec2(kSceneColW - tok::arrange_session_grid_ratio * kGap, kRowH - tok::arrange_session_grid_ratio * kGap)))
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
         UiInteract::Hit("##scene", ImVec2(b.x - a.x, b.y - a.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
         const bool hov = ImGui::IsItemHovered();
         // A click selects the scene (Delete removes it, Cmd+D duplicates it) and launches it, or stops it
         // when it is already playing or waiting to launch. Cmd-click only selects.
         // Acts on release, so a press that becomes a reorder drag does not launch the scene.
         const ImVec2 sceneDragD(ImGui::GetMousePos().x - ImGui::GetIO().MouseClickedPos[0].x, ImGui::GetMousePos().y - ImGui::GetIO().MouseClickedPos[0].y);
         if (ImGui::IsItemDeactivated() && hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
             sceneDragD.x * sceneDragD.x + sceneDragD.y * sceneDragD.y < tok::arrange_session_grid_limit)
         {
            sSelScene = row;
            gArrangeSel.clear();
            if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeySuper && (rowHasClip || anyLaunched))
               ArrangeSessionLaunchScene(row);
         }
         if (MenuParts::ContextClicked())
         {
            sSelScene = row;
            sSceneCtxRow = row;
            const std::string nm = realScene ? gArrange.scenes[(size_t)row].name : std::string();
            snprintf(sSceneNameBuf, sizeof(sSceneNameBuf), "%s", nm.c_str());
            sSceneTempoEdit = realScene ? gArrange.scenes[(size_t)row].tempo : 0.0f;
            sSceneCtxOpen = true;
         }
         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, tok::drag_threshold_tile))
            sSceneDragFrom = row;
         if (hov && sSceneDragFrom < 0)
            HelpTip("%s", rowHasClip ? T("Click to launch this row on every track, click again to stop it. Drag to reorder. Right-click for its tempo and what plays next.")
                                     : T("Right-click to rename this scene or set its tempo"));
         ImGui::PopID();
         dl->AddRectFilled(a, b, TextCol(hov ? tok::arrange_session_grid_line_shade : tok::arrange_session_grid_fill_shade_sm), tok::radius_control);
         const float cy = (a.y + b.y) * 0.5f;
         // Scene state: playing = every filled cell of the row is running (a ring sweeps with the clip),
         // queued = the launch is waiting for the quantize point (the play glyph pulses on the beat).
         const ArrangeSessionSceneState sceneSt = ArrangeSessionSceneQuery(row);
         const bool rowPlaying = sceneSt.playing;
         const bool rowQueued = sceneSt.queued || sceneSt.next;
         float rowProgress = 0.0f;
         for (const Arrange::Lane& l : gArrange.lanes)
            if (rowPlaying && SlotOf(l, row) != nullptr)
            {
               rowProgress = ArrangeSessionQuery(l.id, row).progress;
               break;
            }
         if (sSelScene == row)
            dl->AddRect(a, b, TextCol(tok::arrange_session_grid_outline_shade_sm), tok::radius_control, 0, tok::arrange_session_grid_stroke);
         if (rowPlaying)
            dl->AddRectFilled(a, b, WithAlpha(tok::Role(tok::col::ink_white), tok::arrange_session_grid_fill_alpha), tok::radius_control);
         if (rowPlaying)
            DrawProgressRing(dl, ImVec2(a.x + tok::arrange_session_grid_progress_ring_x, cy), tok::arrange_session_grid_progress_ring_r, rowProgress, TextCol(tok::arrange_session_grid_progress_ring_shade));
         else
            glyph::DrawPlayerPlay(dl, ImVec2(a.x + tok::arrange_session_grid_progress_ring_x, cy), tok::icon_control,
                                  rowQueued ? WithAlpha(TextCol(tok::arrange_session_grid_progress_ring_shade), pulse) : (rowHasClip ? TextCol(tok::arrange_session_grid_text_shade_lg) : TextCol(tok::arrange_session_grid_shade_xs)), true);
         const std::string label = SceneLabel(row);
         const float th = ImGui::GetTextLineHeight();
         float rightEdge = b.x - tok::space_1_5;
         if (realScene && gArrange.scenes[(size_t)row].tempo > 0.0f)
         {
            char tbuf[24];
            snprintf(tbuf, sizeof(tbuf), "%s", UiFmt::F0(gArrange.scenes[(size_t)row].tempo).c);
            const ImVec2 ts = ImGui::CalcTextSize(tbuf);
            dl->AddText(ImVec2(rightEdge - ts.x, cy - th * 0.5f), TextCol(tok::arrange_session_grid_text_shade_md), tbuf);
            rightEdge -= ts.x + tok::space_1_5;
         }
         dl->PushClipRect(a, ImVec2(rightEdge, b.y), true);
         dl->AddText(ImVec2(a.x + tok::arrange_session_grid_text_x_lg, cy - th * 0.5f), realScene ? TextCol(tok::arrange_session_grid_text_shade_lg) : TextCol(tok::arrange_session_grid_text_shade_xxs), label.c_str());
         dl->PopClipRect();
      }

      // Reorder: a line marks the drop row while a scene is dragged; releasing moves it there.
      if (sSceneDragFrom >= 0)
      {
         const int target = std::clamp((int)std::floor((ImGui::GetMousePos().y - bodyTop) / kRowH), 0, std::max(0, rows - 1));
         const float ly = bodyTop + kRowH * (float)(target + (target > sSceneDragFrom ? 1 : 0));
         if (target != sSceneDragFrom)
            dl->AddRectFilled(ImVec2(o.x + kGap, ly - 1.0f), ImVec2(o.x + kSceneColW - kGap, ly + 1.0f), TextCol(tok::arrange_session_grid_fill_shade_xl), 1.0f);
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            MoveScene(sSceneDragFrom, target);
            sSceneDragFrom = -1;
         }
      }

      // Reorder tracks: a line marks the drop column while a header is dragged; releasing moves it there.
      if (sTrackDragFrom >= 0 && sTrackDragFrom < laneCount)
      {
         const int target = std::clamp((int)std::floor((ImGui::GetMousePos().x - o.x - kSceneColW) / colW), 0, laneCount - 1);
         const float lx = o.x + kSceneColW + colW * (float)(target + (target > sTrackDragFrom ? 1 : 0));
         if (target != sTrackDragFrom)
            dl->AddRectFilled(ImVec2(lx - 1.0f, o.y + kGap), ImVec2(lx + 1.0f, o.y + kHeaderH - kGap), TextCol(tok::arrange_session_grid_fill_shade_xl), 1.0f);
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            if (target != sTrackDragFrom)
            {
               const uint64_t id = gArrange.lanes[(size_t)sTrackDragFrom].id;
               const size_t anchor = (size_t)(target + (target > sTrackDragFrom ? 1 : 0));
               ArrangeEdit([&]() { Arrange::MoveLanesBefore(gArrange, { id }, anchor); });
            }
            sTrackDragFrom = -1;
         }
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
            UiInteract::Hit("##cell", ImVec2(b.x - a.x, b.y - a.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool hov = ImGui::IsItemHovered();
            const bool held = ImGui::IsItemActive();
            if (ss != nullptr && ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
               ArrangeSessionRelease(laneId, row);
               if (sDrag.launchOnRelease && !sDrag.dragging && sDrag.laneId == laneId && sDrag.row == row)
                  ArrangeSessionLaunch(laneId, row);
            }
            if (held && ss != nullptr && sDrag.laneId == laneId && sDrag.row == row && !sDrag.dragging &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, tok::drag_threshold_tile))
               sDrag.dragging = true;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
               sDrag = {};   // a double-click's second press must not launch (and so stop) the clip again
               sCurLaneId = laneId;
               sCurRow = row;
               // Clicking anywhere but the clip waiting for a node abandons the assign.
               if (gArrangeAssigningClipId != 0 && (ss == nullptr || ss->clip.id != gArrangeAssigningClipId))
               {
                  gArrangeAssigningClipId = 0;
                  gArrangeAssignTargetIds.clear();
               }
               sSelScene = -1;
               const bool selectOnly = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
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
                  // GarageBand: a click launches the cell, a click on a playing (or waiting) cell stops it
                  // at the next quantize point. Cmd-click only selects, for copy and paste. A Trigger clip
                  // acts on release so the press can become a drag; a Gate clip plays while held.
                  sDrag = { laneId, row, false, !selectOnly && ss->launchMode != 1 };
                  if (!selectOnly && ss->launchMode == 1)
                     ArrangeSessionLaunch(laneId, row);
                  gArrangeSel = { ss->clip.id };
                  gArrangeSelAnchor = ss->clip.id;
               }
               else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                  AddSlotClip(laneId, row);
               else
                  gArrangeSel.clear();   // an empty cell is selected as the paste target
            }
            if (MenuParts::ContextClicked())
            {
               sSelScene = -1;
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
                  sSlotCtxOpen = true;
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
                  fill = isLight ? tok::Base(tok::col::edge_mid_light_fixed, tok::arrange_session_grid_edge_mid_light_fixed_alpha) : tok::Base(tok::col::edge_lo_light, tok::arrange_session_grid_edge_lo_light_alpha);
               dl->AddRectFilled(a, b, fill, tok::radius_control);
               if (muted)
                  DrawArrangeHatch(dl, a, b, isLight ? tok::Base(tok::col::ink_black, tok::arrange_session_grid_hatch_alpha_lg) : tok::Base(tok::col::ink_white, tok::arrange_session_grid_hatch_alpha_sm));
               const ImU32 ink = muted ? TextCol(tok::arrange_session_grid_text_shade_md) : LabelOn(fill);
               if (st.playing)
                  dl->AddRect(a, b, tok::Role(tok::col::ink_white, tok::arrange_session_grid_outline_alpha_lg_u8), tok::radius_control, 0, tok::arrange_session_grid_stroke);
               else if (hov)
                  dl->AddRect(a, b, tok::Role(tok::col::edge_hi, tok::arrange_session_grid_outline_alpha_sm_u8), tok::radius_control, 0, tok::arrange_session_grid_outline_stroke_sm);
               const ImVec2 ic(a.x + tok::arrange_session_grid_progress_ring_x, cy);
               if (st.playing)
                  DrawProgressRing(dl, ic, tok::arrange_session_grid_progress_ring_r, st.progress, tok::Role(tok::col::ink_white));
               else
                  glyph::DrawPlayerPlay(dl, ic, tok::icon_control, WithAlpha(ink, st.queued ? pulse : tok::arrange_session_grid_glyph_alpha_sm), !st.queued);
               if (gArrangeAssigningClipId == ss->clip.id)
               {
                  // Waiting for a canvas node: the same cue as the Timeline's
                  // target - a pulsing outline, steady under Reduce Motion.
                  const float ap = UiAnim::ReduceMotion() ? 1.0f : tok::alpha_pulse_floor + tok::alpha_pulse_depth * std::sin((float)ImGui::GetTime() * tok::motion_rate_pulse);
                  dl->AddRect(ImVec2(a.x - tok::space_0_5, a.y - tok::space_0_5), ImVec2(b.x + tok::space_0_5, b.y + tok::space_0_5),
                              WithAlpha(tok::Role(tok::col::ink_white), ap), tok::radius_control, 0, tok::arrange_session_grid_outline_stroke_lg);
                  HelpTip("%s", T("Click a node on the canvas to assign it. Esc cancels."));
               }
               if (gArrangeFlashClipId == ss->clip.id)
               {
                  const float ft = (float)(ImGui::GetTime() - gArrangeFlashStart);
                  if (ft >= 1.2f)
                     gArrangeFlashClipId = 0;
                  else
                     dl->AddRect(ImVec2(a.x - tok::space_0_5, a.y - tok::space_0_5), ImVec2(b.x + tok::space_0_5, b.y + tok::space_0_5),
                                 tok::U32(tok::arrange_session_grid_outline_shade, (int)(tok::arrange_session_grid_shade_lg * (tok::arrange_session_grid_shade_sm - ft / tok::arrange_session_grid_shade_md))), tok::radius_control, 0, tok::arrange_session_grid_outline_stroke_lg);
               }
               if (st.queued)
                  dl->AddRect(a, b, WithAlpha(tok::Role(tok::col::ink_white), tok::arrange_session_grid_outline_alpha_lg * pulse), tok::radius_control, 0, tok::arrange_session_grid_stroke);
               if (st.upNext)
               {
                  // Up next (a follow action is about to start this clip): a steady inner outline and a chevron.
                  dl->AddRect(ImVec2(a.x + tok::space_0_5, a.y + tok::space_0_5), ImVec2(b.x - tok::space_0_5, b.y - tok::space_0_5),
                              WithAlpha(tok::Role(tok::col::ink_white), tok::arrange_session_grid_outline_alpha_md), tok::radius_control, 0, tok::arrange_session_grid_outline_stroke_sm);
                  glyph::DrawChevronDown(dl, ImVec2(b.x - tok::space_3_5, a.y + tok::space_2), tok::icon_control, WithAlpha(ink, tok::arrange_session_grid_glyph_alpha_lg));
               }
               const std::string label = SlotLabel(ss->clip);
               const float badgeW = ss->followAction != Arrange::kFollowNone ? tok::arrange_session_grid_badge_w : 0.0f;
               dl->PushClipRect(a, ImVec2(b.x - tok::space_1_5 - badgeW, b.y), true);
               dl->AddText(ImVec2(a.x + tok::arrange_session_grid_text_x_lg, cy - th * 0.5f), ink, label.c_str());
               dl->PopClipRect();
               DrawFollowBadge(dl, ImVec2(b.x - tok::space_1_5, cy), *ss, WithAlpha(ink, tok::arrange_session_grid_follow_badge_alpha));
               if (hov)
                  HelpTip("%s", muted ? (src == nullptr ? T("This slot's source is missing") : T("This slot is inactive"))
                                      : T("Click to launch, click again to stop. Cmd-click selects. Right-click for copy, paste and follow actions."));
            }
            else
            {
               // Empty: a quiet recess. While the track plays, the cell is a
               // stop button, and says so on hover.
               dl->AddRectFilled(a, b, TextCol(hov ? tok::arrange_session_grid_fill_shade_lg : tok::arrange_session_grid_fill_shade_xs), tok::radius_control);
               if (hov)
               {
                  // A plus: a double-click adds an unassigned clip to this cell.
                  const ImVec2 pc((a.x + b.x) * 0.5f, cy);
                  const ImU32 pcol = TextCol(tok::arrange_session_grid_text_shade_md);
                  dl->AddLine(ImVec2(pc.x - tok::space_1_5, pc.y), ImVec2(pc.x + tok::space_1_5, pc.y), pcol, tok::arrange_session_grid_stroke);
                  dl->AddLine(ImVec2(pc.x, pc.y - tok::space_1_5), ImVec2(pc.x, pc.y + tok::space_1_5), pcol, tok::arrange_session_grid_stroke);
                  HelpTip("%s", isVideo ? T("Double-click to add a clip. Click selects the cell to paste into. You can also drop a video or image file here.")
                                        : T("Double-click to add a clip. Click selects the cell to paste into. You can also drop an audio file here."));
               }
            }

            // The keyboard cursor / selected clip.
            if ((ss != nullptr && gArrangeSel.count(ss->clip.id) != 0) || (laneId == sCurLaneId && row == sCurRow))
               dl->AddRect(ImVec2(a.x - tok::space_0_25, a.y - tok::space_0_25), ImVec2(b.x + tok::space_0_25, b.y + tok::space_0_25),
                           WithAlpha(TextCol(tok::arrange_session_grid_outline_shade_lg), gSessionClaimedKeys ? tok::arrange_session_grid_outline_alpha_lg : tok::arrange_session_grid_outline_alpha_sm), tok::radius_control, 0, tok::arrange_session_grid_stroke);
            if (ss != nullptr && gArrangeRenamingClipId == ss->clip.id)
            {
               static uint64_t sRenameFocusedId = 0;
               ImGui::SetCursorScreenPos(ImVec2(a.x + tok::arrange_session_grid_x, a.y + tok::space_0_75));
               ImGui::SetNextItemWidth(std::max(tok::arrange_session_grid_min_sm, b.x - a.x - tok::arrange_session_grid_min_lg));
               if (sRenameFocusedId != ss->clip.id)
               {
                  UiInteract::FocusHere();
                  sRenameFocusedId = ss->clip.id;
               }
               const bool commit = FieldWell::InputText("##renamingslot", gArrangeRenameClipBuffer, sizeof(gArrangeRenameClipBuffer),
                                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               if (commit || ImGui::IsItemDeactivated())
               {
                  const std::string newName = gArrangeRenameClipBuffer;
                  const uint64_t rid = ss->clip.id;
                  if (!Keymap::Cancel(false))
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
            if (UiInteract::Hit("##stop", ImVec2(b.x - a.x, b.y - a.y)))
               ArrangeSessionStopLane(laneId);
            const bool hov = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            dl->AddRectFilled(a, b, TextCol(canStop ? (hov ? tok::arrange_session_grid_can_stop_shade_lg : tok::arrange_session_grid_can_stop_shade_sm) : tok::arrange_session_grid_fill_shade_xxs), tok::radius_control);
            const float alpha = !canStop ? tok::arrange_session_grid_alpha : (ls.stopQueued ? tok::arrange_session_grid_scale * pulse : tok::arrange_session_grid_value);
            DrawStopSquare(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 9.0f, TextCol(alpha));
            if (hov)
               HelpTip("%s", T("Stop this track at the next quantize point"));
         }
         ImGui::PopID();
      }

      // A clip being dragged: outline the cell it would land in and carry its name at the cursor.
      if (sDrag.dragging)
      {
         const int li = (int)std::floor((mouse.x - (o.x + kSceneColW)) / colW);
         const int row = (int)std::floor((mouse.y - bodyTop) / kRowH);
         const Arrange::Lane* srcLane = Arrange::FindLane(gArrange, sDrag.laneId);
         const bool inGrid = li >= 0 && li < laneCount && row >= 0 && row < rows;
         const bool fits = inGrid && srcLane != nullptr && gArrange.lanes[(size_t)li].type == srcLane->type &&
                           !(gArrange.lanes[(size_t)li].id == sDrag.laneId && row == sDrag.row);
         const bool copy = ImGui::GetIO().KeyAlt;
         if (fits)
         {
            const float x0 = o.x + kSceneColW + colW * (float)li, y0 = bodyTop + kRowH * (float)row;
            dl->AddRect(ImVec2(x0 + kGap - tok::space_0_25, y0 + kGap - tok::space_0_25), ImVec2(x0 + colW - kGap + tok::space_0_25, y0 + kRowH - kGap + tok::space_0_25),
                        TextCol(tok::arrange_session_grid_progress_ring_shade), tok::radius_control, 0, tok::arrange_session_grid_outline_stroke_md);
         }
         if (srcLane != nullptr && SlotOf(*srcLane, sDrag.row) != nullptr)
         {
            const std::string label = SlotLabel(srcLane->slots[(size_t)sDrag.row].clip);
            ImGui::GetForegroundDrawList()->AddText(ImVec2(mouse.x + tok::space_3_5, mouse.y + tok::space_1), TextCol(fits ? tok::arrange_session_grid_progress_ring_shade : tok::arrange_session_grid_text_shade_sm),
                                                    (copy ? "+ " + label : label).c_str());
         }
         ImGui::SetMouseCursor(fits ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_NotAllowed);
         if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         {
            if (fits)
               ArrangeSessionMoveSlot(sDrag.laneId, sDrag.row, gArrange.lanes[(size_t)li].id, row, copy);
            sDrag = {};
         }
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         sDrag = {};

      // Hairline under the headers and above the stop row: the grid's frame.
      dl->AddLine(ImVec2(o.x, bodyTop - tok::px_align), ImVec2(o.x + gridW, bodyTop - tok::px_align), TextCol(tok::arrange_session_grid_line_shade), tok::arrange_session_grid_outline_stroke_sm);
      dl->AddLine(ImVec2(o.x, footerTop + tok::px_align), ImVec2(o.x + gridW, footerTop + tok::px_align), TextCol(tok::arrange_session_grid_line_shade), tok::arrange_session_grid_outline_stroke_sm);

      DrawSlotMenu();
      DrawSceneMenu();
      DrawTrackMenu();

      ImGui::SetCursorScreenPos(o);
      Ui::Claim(ImVec2(gridW, footerTop + kFooterH - o.y));
      (void)mouse;
      (void)gridHovered;
      EndChildSafe();
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
      enum class MenuAction { None, PlaceOnTimeline, Duplicate, Clear, Copy, Cut, Paste };
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
      if (a.kind == MenuAction::Paste)   // the one action an empty cell takes
      {
         PasteSlotAt(a.laneId, a.row);
         return;
      }
      if (lane == nullptr || a.row < 0 || a.row >= (int)lane->slots.size() || !lane->slots[(size_t)a.row].Has())
         return;
      const uint64_t laneId = a.laneId;
      const int row = a.row;
      switch (a.kind)
      {
         case MenuAction::Copy:
         case MenuAction::Cut:
            sSlotClip = { true, lane->type, lane->slots[(size_t)row] };
            if (a.kind == MenuAction::Cut)
            {
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
                  {
                     l->slots[(size_t)row] = Arrange::SessionSlot{};
                     gArrange.revision++;
                  }
               });
               gArrangeSel.clear();
            }
            break;
         case MenuAction::Paste:
            break;
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
         {
            uint64_t made = 0;
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
               copy.clip.id = made = gArrange.NewId();
               l->slots[(size_t)dst] = copy;
               gArrange.revision++;
            });
            if (made != 0)
            {
               gArrangeSel = { made };
               gArrangeSelAnchor = made;
               gArrangeFlashClipId = made;
               gArrangeFlashStart = ImGui::GetTime();
            }
            break;
         }
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

   // Test hook: 0 = Place on Timeline, 1 = Duplicate, 2 = Clear, 3 = Copy, 4 = Paste, 5 = Cut, queued and applied
   // exactly as the menu does it.
   void ArrangeSessionRunActionForTest(int kind, uint64_t laneId, int row)
   {
      static const MenuAction kKinds[6] = { MenuAction::PlaceOnTimeline, MenuAction::Duplicate, MenuAction::Clear,
                                            MenuAction::Copy, MenuAction::Paste, MenuAction::Cut };
      sMenuAction = { kKinds[std::clamp(kind, 0, 5)], laneId, row };
      ArrangeSessionApplyMenuAction();
   }

   // The Scene menu and the empty cell's Add Clip, for the terminal.
   bool ArrangeSessionSceneAction(const std::string& action, int row)
   {
      if (row < 0 || (action != "insert" && row >= (int)gArrange.scenes.size()))
         return false;
      if (action == "insert")
         InsertSceneAt(row);
      else if (action == "duplicatescene")
         DuplicateScene(row);
      else
         DeleteScene(row);
      return true;
   }

   uint64_t ArrangeSessionAddSlotClip(uint64_t laneId, int row)
   {
      AddSlotClip(laneId, row);
      const Arrange::Lane* lane = Arrange::FindLane(gArrange, laneId);
      return lane != nullptr && row >= 0 && row < (int)lane->slots.size() ? lane->slots[(size_t)row].clip.id : 0;
   }

   // Moves a slot (clip and launch settings) to another cell of a same-type track, replacing what is there;
   // `copy` leaves the original in place. False when the move cannot land.
   bool ArrangeSessionMoveSlot(uint64_t srcLaneId, int srcRow, uint64_t dstLaneId, int dstRow, bool copy)
   {
      const Arrange::Lane* src = Arrange::FindLane(gArrange, srcLaneId);
      const Arrange::Lane* dst = Arrange::FindLane(gArrange, dstLaneId);
      if (src == nullptr || dst == nullptr || src->type != dst->type || dstRow < 0 || SlotOf(*src, srcRow) == nullptr ||
          (srcLaneId == dstLaneId && srcRow == dstRow))
         return false;
      Arrange::SessionSlot moved = src->slots[(size_t)srcRow];
      ArrangeEdit([&]() {
         Arrange::Lane* s = Arrange::FindLane(gArrange, srcLaneId);
         Arrange::Lane* d = Arrange::FindLane(gArrange, dstLaneId);
         if (s == nullptr || d == nullptr)
            return;
         if (copy)
            moved.clip.id = gArrange.NewId();
         else
            s->slots[(size_t)srcRow] = Arrange::SessionSlot{};
         EnsureRow(*d, dstRow);
         d->slots[(size_t)dstRow] = moved;
         gArrange.revision++;
      });
      ArrangeSessionMoveScheduled(srcLaneId, copy ? -1 : srcRow, dstLaneId, dstRow);
      gArrangeSel = { moved.clip.id };
      gArrangeSelAnchor = moved.clip.id;
      gArrangeFlashClipId = moved.clip.id;
      gArrangeFlashStart = ImGui::GetTime();
      sCurLaneId = dstLaneId;
      sCurRow = dstRow;
      return true;
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
      const ArrangeSessionSlotState slotSt = ArrangeSessionQuery(laneId, row);
      if (MenuParts::Item((slotSt.playing || slotSt.queued) && ss.launchMode != 1 ? L("Stop") : L("Launch")))
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
         for (int m = 0; m < 2; m++)
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
   }

   // The slot's edit rows (Cut, Copy, Paste, Duplicate), drawn after Rename in the shared clip menu.
   // Rows are queued, not run: the shared menu still holds pointers into the slot and lane vectors and keeps reading
   // them for the rest of this frame. ArrangeSessionApplyMenuAction() runs them after it closes.
   void DrawArrangeSessionSlotEditItems(uint64_t clipId)
   {
      const Arrange::Loc loc = Arrange::FindSlot(gArrange, clipId);
      if (loc.lane < 0 || loc.slot < 0)
         return;
      const uint64_t laneId = gArrange.lanes[(size_t)loc.lane].id;
      const int row = loc.slot;
      if (MenuParts::Item(L("Cut"), MODKEY "+X"))
         sMenuAction = {MenuAction::Cut, laneId, row};
      if (MenuParts::Item(L("Copy"), MODKEY "+C"))
         sMenuAction = {MenuAction::Copy, laneId, row};
      const bool canPaste = sSlotClip.has && sSlotClip.laneType == gArrange.lanes[(size_t)loc.lane].type;
      if (MenuParts::Item(L("Paste"), MODKEY "+V", false, canPaste))
         sMenuAction = {MenuAction::Paste, laneId, row};
      if (MenuParts::Item(L("Duplicate"), MODKEY "+D"))
         sMenuAction = {MenuAction::Duplicate, laneId, row};
   }

   // The slot's Place on Timeline row (an "other action", after the toggles in the shared clip menu).
   void DrawArrangeSessionSlotPlaceItem(uint64_t clipId)
   {
      const Arrange::Loc loc = Arrange::FindSlot(gArrange, clipId);
      if (loc.lane < 0 || loc.slot < 0)
         return;
      if (MenuParts::Item(L("Place on Timeline at Playhead")))
         sMenuAction = {MenuAction::PlaceOnTimeline, gArrange.lanes[(size_t)loc.lane].id, loc.slot};
   }

   // The slot's Delete row, last in the shared clip menu (queued like the edit rows).
   void DrawArrangeSessionSlotDeleteItem(uint64_t clipId)
   {
      const Arrange::Loc loc = Arrange::FindSlot(gArrange, clipId);
      if (loc.lane < 0 || loc.slot < 0)
         return;
      if (MenuParts::Item(L("Delete"), "Backspace"))
         sMenuAction = {MenuAction::Clear, gArrange.lanes[(size_t)loc.lane].id, loc.slot};
   }

   // The inspector's Launch card for a session slot: how a click plays it, where it lands, how long it runs and
   // what plays after it. Returns false (draws nothing) for a Timeline clip.
   bool DrawArrangeSessionSlotInspector(uint64_t clipId, float fieldW)
   {
      const Arrange::Loc loc = Arrange::FindSlot(gArrange, clipId);
      if (loc.lane < 0 || loc.slot < 0)
         return false;
      const uint64_t laneId = gArrange.lanes[(size_t)loc.lane].id;
      const int row = loc.slot;
      const Arrange::SessionSlot ss = gArrange.lanes[(size_t)loc.lane].slots[(size_t)row];
      auto edit = [&](const std::function<void(Arrange::SessionSlot&)>& fn) {
         ArrangeEdit([&]() {
            if (Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
               if (row < (int)l->slots.size())
               {
                  fn(l->slots[(size_t)row]);
                  gArrange.revision++;
               }
         });
      };
      // A field-well dropdown, value only (no chevron).
      auto combo = [&](const char* label, const char* id, const char* cur, const std::function<void()>& items) {
         SectionCard::RowLabel(label);
         ImGui::SetNextItemWidth(fieldW);
         FieldWell::PushStyle();
         const bool open = MenuParts::BeginCombo(id, cur, ImGuiComboFlags_NoArrowButton);
         FieldWell::PopStyle();
         if (open)
         {
            items();
            MenuParts::EndCombo();
         }
      };
      char buf[48];
      SectionCard::Begin(T("Launch"));
      combo(T("Mode"), "##slotmode", L(kModeNames[ss.launchMode == 1 ? 1 : 0]), [&]() {
         for (int m = 0; m < 2; m++)
            if (MenuParts::Choice(L(kModeNames[m]), ss.launchMode == m) && ss.launchMode != m)
               edit([&](Arrange::SessionSlot& s) { s.launchMode = m; });
      });
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Trigger: a click starts it, a click while it plays stops it. Gate: it plays while you hold the mouse or key."));
      combo(T("Quantize"), "##slotquant", L(kQuantNames[std::clamp(ss.quantize, -1, 3) + 1]), [&]() {
         for (int q = -1; q <= 3; q++)
            if (MenuParts::Choice(L(kQuantNames[q + 1]), ss.quantize == q) && ss.quantize != q)
               edit([&](Arrange::SessionSlot& s) { s.quantize = q; });
      });
      SectionCard::RowLabel(T("Loop"));
      bool loopOn = ss.loop;
      if (ChipButton::Draw(loopOn ? L("On##slotloop") : L("Off##slotloop"), loopOn, tok::control_h, fieldW))
         edit([&](Arrange::SessionSlot& s) { s.loop = !loopOn; });
      const char* lenName = T("Clip Length");
      for (const LoopChoice& lc : kLoopChoices)
         if (ss.loopTicks == (Arrange::Tick)(lc.bars * (double)Arrange::kTicksPerBar))
            lenName = L(lc.name);
      combo(T("Length"), "##slotlooplen", lenName, [&]() {
         for (const LoopChoice& lc : kLoopChoices)
         {
            const Arrange::Tick want = (Arrange::Tick)(lc.bars * (double)Arrange::kTicksPerBar);
            if (MenuParts::Choice(L(lc.name), ss.loopTicks == want) && ss.loopTicks != want)
               edit([&](Arrange::SessionSlot& s) { s.loopTicks = want; });
         }
      });
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("How long one pass of the clip runs before it loops (or stops, with Loop off)"));
      combo(T("Then"), "##slotfollow", L(kFollowNames[std::clamp(ss.followAction, 0, Arrange::kFollowCount - 1)]), [&]() {
         for (int a = 0; a < Arrange::kFollowCount; a++)
            if (MenuParts::Choice(L(kFollowNames[a]), ss.followAction == a) && ss.followAction != a)
               edit([&](Arrange::SessionSlot& s) { s.followAction = a; });
      });
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Follow Action: what this track plays once this clip has run for Follow After"));
      ImGui::BeginDisabled(ss.followAction == Arrange::kFollowNone);
      snprintf(buf, sizeof(buf), ss.followBars == 1 ? T("%d bar") : T("%d bars"), ss.followBars);
      combo(T("After"), "##slotfollowafter", buf, [&]() {
         for (int bars : kFollowBarChoices)
         {
            char label[32];
            snprintf(label, sizeof(label), bars == 1 ? T("%d bar") : T("%d bars"), bars);
            if (MenuParts::Choice(label, ss.followBars == bars) && ss.followBars != bars)
               edit([&](Arrange::SessionSlot& s) { s.followBars = bars; });
         }
      });
      ImGui::EndDisabled();
      return true;
   }

   namespace
   {
      bool sSessionShowMonitor = true;

      ImU32 RecordDotCol()
      {
         const tok::Rgba8& c = IsThemeLight() ? tok::action_record.light : tok::action_record.dark;
         return IM_COL32(c.r, c.g, c.b, tok::arrange_session_grid_record_dot_col_alpha);
      }

      // Session Audio on: the launched clips own the output (starting the device if it is off). Off hands audio
      // back to the canvas and leaves the device running, as the Timeline's chip does.
      void SetSessionAudio(bool on)
      {
         if (!on)
         {
            gAudioMode = AudioMode::Canvas;
            return;
         }
         gAudioMode = AudioMode::Session;
         if (AudioEngine::Instance().SampleRate() <= 0.0)
         {
            gAudioStartError.clear();
            if (!StartAudioEngine(gAudioStartError))
               fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
         }
      }

      // The video the launched clips produce, drawn at the panel's own size. The composite itself runs after the cook
      // loop into gArrangeMonitorTarget; this only asks for a size and shows the texture.
      void DrawSessionMonitor(float width)
      {
         PushDockedPanelStyle(/*isChild=*/true);
         ImGui::BeginChild("##sessionmonitor", ImVec2(width, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
         PopDockedPanelStyle();
         const ImVec2 avail = Ui::Avail();
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 br(origin.x + avail.x, origin.y + avail.y);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         DrawCheckerboardBackdrop(dl, origin, br);
         // Outside Session audio mode the playback model is the Timeline's own: the Session monitor stays empty.
         if (gAudioMode == AudioMode::Session && CountActiveArrangeVideoClips(Transport::Instance().Beats()) > 0)
         {
            gArrangeMonitorTarget.requestW = std::max(tok::arrange_session_grid_request_w_min, (int)avail.x);
            gArrangeMonitorTarget.requestH = std::max(tok::arrange_session_grid_request_w_min, (int)avail.y);
            const unsigned int tex = gArrangeMonitorTarget.result.tex;
            if (tex != 0)
               Thumb::Add(dl, (ImTextureID)(intptr_t)tex, origin, br, ImVec2(0, 1), ImVec2(1, 0));
         }
         Ui::Claim(avail);
         EndChildSafe();
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
         // A selected scene takes the keys: Delete removes it, Cmd+D duplicates it, Enter launches it, Up/Down move.
         if (sSelScene >= 0 && gArrangeSel.empty())
         {
            const int maxScene = std::max(kMinRows, (int)gArrange.scenes.size()) - 1;
            sSelScene = std::min(sSelScene, maxScene);
            if (!cmd && (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)))
               DeleteScene(sSelScene);
            else if (cmd && pressed(ImGuiKey_D))
               DuplicateScene(sSelScene);
            else if (!cmd && !io.KeyAlt && (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)))
               ArrangeSessionLaunchScene(sSelScene);
            else if (!cmd && pressed(ImGuiKey_UpArrow))
               sSelScene = std::max(0, sSelScene - 1);
            else if (!cmd && pressed(ImGuiKey_DownArrow))
               sSelScene = std::min(maxScene, sSelScene + 1);
            else if (!cmd && !io.KeyShift && !io.KeyAlt && pressed(ImGuiKey_Period))
               ArrangeSessionStopAll();
            else if (!cmd && pressed(ImGuiKey_Escape))
               sSelScene = -1;
            else if (!cmd && (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_LeftArrow)))
            {
               // Step from the scene into its row's first cell.
               sCurRow = sSelScene;
               sCurLaneId = gArrange.lanes[0].id;
               sSelScene = -1;
               SelectCursorCell();
            }
            return;
         }
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
            sMenuAction = { MenuAction::Copy, sCurLaneId, sCurRow };
            ArrangeSessionApplyMenuAction();
         }
         else if (cmd && pressed(ImGuiKey_X) && ss != nullptr)
         {
            sMenuAction = { MenuAction::Cut, sCurLaneId, sCurRow };
            ArrangeSessionApplyMenuAction();
         }
         else if (cmd && pressed(ImGuiKey_V))
         {
            sMenuAction = { MenuAction::Paste, sCurLaneId, sCurRow };
            ArrangeSessionApplyMenuAction();
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
         else if (!cmd && !io.KeyShift && !io.KeyAlt && pressed(ImGuiKey_Period))   // Cmd+. is the global Panic
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
         const ImVec2 panelSize = Ui::Avail();

         // Toolbar, laid out like the Timeline's: Monitor | Play, Record, Stop All, Quantize | Inspector, and pinned
         // right the audio mode chip (and Render Take once a take exists). Right-click it for Close.
         const float toolbarTop = ImGui::GetCursorScreenPos().y;
         const ImVec2 toolbarOrigin = ImGui::GetCursorScreenPos();
         const ImU32 iconCol = ImGui::GetColorU32(ImGuiCol_Text);
         auto itemCentre = []() {
            return ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                          (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
         };
         auto nextTo = [](float gap) { ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + gap, ImGui::GetItemRectMin().y)); };
         ActionButton::Scoped() = sSessionShowMonitor ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
         if (ActionButton::Draw("##sessionmonitortoggle", ImVec2(tok::control_h, tok::control_h)))
            sSessionShowMonitor = !sSessionShowMonitor;
         ActionButton::Scoped() = ActionButton::Kind::Plain;
         if (ImGui::IsItemHovered())
            HelpTip("%s", sSessionShowMonitor ? T("Monitor: Visible. Shows the video the launched clips produce.") : T("Show the video the launched clips produce."));
         {
            const ImVec2 c((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
            ImDrawList* tdl = ImGui::GetWindowDrawList();
            const ImU32 icol = sSessionShowMonitor ? tok::Role(tok::col::ink_white) : iconCol;
            const ImVec2 tl(c.x - tok::arrange_session_grid_panel_content_gap, c.y - tok::arrange_session_grid_tl_inset), br(c.x + tok::arrange_session_grid_panel_content_gap, c.y + tok::arrange_session_grid_line_x);
            if (sSessionShowMonitor)
               tdl->AddRectFilled(tl, br, icol, tok::arrange_session_grid_panel_content_radius);
            else
               tdl->AddRect(tl, br, icol, tok::arrange_session_grid_panel_content_radius, 0, tok::arrange_session_grid_panel_content_stroke);
            tdl->AddLine(ImVec2(c.x, br.y), ImVec2(c.x, br.y + tok::arrange_session_grid_line_y), icol, tok::arrange_session_grid_panel_content_stroke);
            tdl->AddLine(ImVec2(c.x - tok::arrange_session_grid_line_x, br.y + tok::arrange_session_grid_line_y), ImVec2(c.x + tok::arrange_session_grid_line_x, br.y + tok::arrange_session_grid_line_y), icol, tok::arrange_session_grid_panel_content_stroke);
         }
         nextTo(tok::space_4);

         // Play / Pause: the shared transport. Playing from here hands audio to the Session.
         // Inside a DAW the host's transport plays: no button here.
         if (!Platform::PluginModeActive())
         {
         Transport& tr = Transport::Instance();
         const bool playing = tr.IsPlaying();
         ActionButton::Scoped() = playing ? ActionButton::Kind::Go : ActionButton::Kind::Plain;
         if (ActionButton::Draw("##sessionplay", ImVec2(tok::control_h, tok::control_h)))
         {
            if (!playing && gAudioMode != AudioMode::Session)
               SetSessionAudio(true);
            LaunchRequestTogglePlay();
         }
         ActionButton::Scoped() = ActionButton::Kind::Plain;
         if (ImGui::IsItemHovered())
            HelpTip(playing ? T("Pause (Space)") : T("Play (Space)"));
         if (playing)
            glyph::DrawPlayerPause(ImGui::GetWindowDrawList(), itemCentre(), tok::icon_control, tok::Role(tok::col::ink_white));
         else
            glyph::DrawPlayerPlay(ImGui::GetWindowDrawList(), itemCentre(), tok::icon_control, iconCol);
         nextTo(tok::space_1);
         }

         // Record: what you launch is written into the Timeline as clips; Render Take turns it into a movie.
         const bool rec = ArrangeSessionRecording();
         ActionButton::Scoped() = rec ? ActionButton::Kind::Record : ActionButton::Kind::Plain;
         if (ActionButton::Draw("##sessionrecord", ImVec2(tok::control_h, tok::control_h)))
            ArrangeSessionSetRecording(!rec);
         ActionButton::Scoped() = ActionButton::Kind::Plain;
         if (ImGui::IsItemHovered())
            HelpTip("%s", rec ? T("Recording: every clip and scene you launch is written into the Timeline. Click to stop.")
                              : Platform::PluginModeActive() // Render Take is the DAW's bounce here
                                    ? T("Record your performance: the clips and scenes you launch are written into the Timeline.")
                                    : T("Record your performance: the clips and scenes you launch are written into the Timeline, ready to render with Render Take."));
         glyph::Draw(ImGui::GetWindowDrawList(), itemCentre(), tok::icon_control,
                     rec ? tok::Role(tok::col::ink_white) : RecordDotCol(), IconsInfinite::RecordFill);
         nextTo(tok::space_1);

         // Stop All: every track stops at the next quantize point.
         const bool anyLaunched = ArrangeSessionAnyLaunched();
         ImGui::BeginDisabled(!anyLaunched);
         if (ActionButton::Draw("##sessionstopall", ImVec2(tok::control_h, tok::control_h)))
            ArrangeSessionStopAll();
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            HelpTip("%s", T("Stop All (.): every track stops at the next quantize point"));
         glyph::Draw(ImGui::GetWindowDrawList(), itemCentre(), tok::icon_control, WithAlpha(iconCol, anyLaunched ? 1.0f : tok::arrange_session_grid_icon_col_alpha),
                     IconsInfinite::StopFill);
         nextTo(tok::space_1);

         // Quantize: where launches and stops land, for every clip and scene left on Session Default.
         {
            const int q = std::clamp(gArrange.sessionQuantize, 0, 3);
            char qlabel[48];
            snprintf(qlabel, sizeof(qlabel), "%s##sessionquant", L(kQuantNames[q + 1]));
            if (ChipButton::Draw(qlabel, false, tok::control_h))
               ImGui::OpenPopup("##sessionquantpop");
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Launch Quantize: launches and stops wait for this grid line, so everything stays in time"));
            if (MenuParts::BeginPopup("##sessionquantpop"))
            {
               for (int v = 0; v <= 3; v++)
                  if (MenuParts::Choice(L(kQuantNames[v + 1]), q == v) && q != v)
                     ArrangeEdit([&]() {
                        gArrange.sessionQuantize = v;
                        gArrange.revision++;
                     });
               MenuParts::EndPopup();
            }
         }
         const float toolbarBottom = ImGui::GetItemRectMax().y;

         // Pinned right, the Timeline's order: Render Take, Audio, then the inspector toggle in the corner.
         {
            const ImVec2 saved = ImGui::GetCursorScreenPos();
            const bool sessionMode = gAudioMode == AudioMode::Session;
            const float right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x;
            const bool inspWasOpen = gArrangeClipSettingsPanelOpen;
            ImGui::SetCursorScreenPos(ImVec2(right - tok::control_h, toolbarOrigin.y));
            ActionButton::Scoped() = inspWasOpen ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
            if (ActionButton::Draw("##sessioninspectortoggle", ImVec2(tok::control_h, tok::control_h)))
               gArrangeClipSettingsPanelOpen = !gArrangeClipSettingsPanelOpen;
            ActionButton::Scoped() = ActionButton::Kind::Plain;
            if (ImGui::IsItemHovered())
               HelpTip("%s", inspWasOpen ? T("Clip Inspector: Open") : T("Settings of the selected clip. Double-click a clip to open it."));
            glyph::DrawEdit(ImGui::GetWindowDrawList(), itemCentre(), tok::icon_control, inspWasOpen ? tok::Role(tok::col::ink_white) : iconCol);
            const char* audioLabel = T("Audio");
            const float audioW = ImGui::CalcTextSize(audioLabel).x + tok::arrange_session_grid_audio_w_ratio * tok::space_2;
            const float audioX = right - tok::control_h - tok::space_1 - audioW;
            ImGui::SetCursorScreenPos(ImVec2(audioX, toolbarOrigin.y));
            if (ChipButton::Draw(audioLabel, sessionMode, tok::control_h))
               SetSessionAudio(!sessionMode);
            if (ImGui::IsItemHovered())
               HelpTip("%s", !gAudioStartError.empty() && AudioEngine::Instance().SampleRate() <= 0.0
                                ? gAudioStartError.c_str()
                                : T("Session Audio: you hear and see the clips you launch here; the Timeline's clips stay silent"));
            Arrange::Tick from = 0, to = 0;
            if (ArrangeSessionLastTake(from, to))
            {
               const char* takeLabel = T("Render Take");
               const float takeW = ImGui::CalcTextSize(takeLabel).x + tok::arrange_session_grid_take_w_ratio * tok::space_2;
               ImGui::SetCursorScreenPos(ImVec2(audioX - tok::space_2 - takeW, toolbarOrigin.y));
               if (ChipButton::Draw(takeLabel, false, tok::control_h))
               {
                  // Record wrote the take onto the Timeline; the render dialog opens right here with its span
                  // set (audio and video together, saved to the folder you pick).
                  ArrangeEdit([&]() {
                     gArrange.settings.renderRangeKind = kArrangeRangeCustom;
                     gArrange.settings.renderRangeStart = from;
                     gArrange.settings.renderRangeEnd = to;
                  });
                  gArrangeRenderPopupRequest = true;
               }
               if (ImGui::IsItemHovered())
                  HelpTip("%s", T("Render the last recording, audio and video, to a movie file in a folder you choose"));
            }
            ImGui::SetCursorScreenPos(saved);
         }
         {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const ImVec2 o = ImGui::GetWindowPos();
            if (m.y >= toolbarTop && m.y < toolbarBottom && m.x >= o.x && m.x < o.x + panelSize.x &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered() &&
                MenuParts::RightPressed() && !ImGui::IsPopupOpen("##sessiondockctx"))
               ImGui::OpenPopup("##sessiondockctx");
            if (MenuParts::TestOpen("##sessiondockctx"))
               ImGui::OpenPopup("##sessiondockctx");
         }
         DrawArrangeRenderDialogs();
         if (MenuParts::BeginPopup("##sessiondockctx"))
         {
            // Session shares the Timeline's dock edge, so either panel's menu moves both.
            if (MenuParts::SubMenu(L("Dock Position")))
            {
               const bool panelTop = gArrange.settings.dockSide == 1;
               if (MenuParts::Item(L("Session at Bottom"), nullptr, !panelTop) && panelTop)
               {
                  gArrange.settings.dockSide = 0;
                  gArrange.revision++;
                  gPatchDirty = true;
               }
               if (MenuParts::Item(L("Session at Top"), nullptr, panelTop) && !panelTop)
               {
                  gArrange.settings.dockSide = 1;
                  gArrange.revision++;
                  gPatchDirty = true;
               }
               ImGui::EndMenu();
            }
            MenuParts::Separator();
            if (MenuParts::Item(L("Close Session")))
               gSessionPanelOpen = false;
            MenuParts::EndPopup();
         }
         {
            // Same gaps as the Timeline: its toolbar ends one item spacing above an ImGui::Separator.
            const float gapY = ImGui::GetStyle().ItemSpacing.y;
            const float ly = toolbarBottom + gapY;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(ImGui::GetWindowPos().x, ly), ImVec2(ImGui::GetWindowPos().x + panelSize.x, ly),
                                                ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, ly + 1.0f + gapY));
         }

         const bool isWide = panelSize.x >= tok::arrange_session_grid_is_wide_limit;
         const bool showMon = isWide && sSessionShowMonitor;
         const float monW = isWide ? std::clamp(panelSize.x * tok::arrange_session_grid_panel_content_ratio, tok::arrange_session_grid_mon_w_lo, tok::arrange_session_grid_mon_w_hi) : 0.0f;
         const float kInspectorW = ArrangeClipSettingsWidth(std::max(tok::arrange_session_grid_inspector_w_min, Ui::AvailW() * 0.5f));
         // The inspector shows the selected slot clip; it opens from a cell (double-click) and closes with its own X.
         const bool showInspector = gArrangeClipSettingsPanelOpen && ArrangeSelectedSlotId() != 0;
         const float spacing = ImGui::GetStyle().ItemSpacing.x;
         const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
         const float rowW = Ui::AvailW();
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
         bool clipCtx = ArrangeSessionTakeClipCtxRequest();
         // Self-test: the first filled cell, selected, as a right-click on it would.
         if (MenuParts::TestOpen("##arrangeclipctx"))
            for (const Arrange::Lane& l : gArrange.lanes)
               for (size_t r = 0; r < l.slots.size() && !clipCtx; r++)
                  if (l.slots[r].Has())
                  {
                     gArrangeCtxClipId = l.slots[r].clip.id;
                     gArrangeSel = { gArrangeCtxClipId };
                     gArrangeSelAnchor = gArrangeCtxClipId;
                     clipCtx = true;
                  }
         DrawArrangeClipContextMenu(clipCtx);
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
      const ImVec2 inner = Ui::Avail();
      auto grip = [&]()
      {
         UiInteract::Hit("##sessionpanelgrip", ImVec2(std::max(1.0f, inner.x), kGrip));
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
      EndChildSafe();
      ImGui::GetStyle().ItemSpacing = savedSpacing;
   }

// MENUALIVETEST: the slot clipboard is state a person sees (Paste lights up), but the patch does not hold it.
std::string ArrangeSessionTestFingerprint()
{
   return (sSlotClip.has ? "slotclip:" + std::to_string((int)sSlotClip.laneType) : std::string("slotclip:-")) +
          " timelineclip:" + std::to_string(gArrangeClipboard.items.size()) + " trackrename:" + std::to_string(sTrackRenameLaneId);
}
void ArrangeSessionTestReset()
{
   sSlotClip = {};
   sTrackRenameLaneId = 0;
   gArrangeClipboard = {};
   gArrangeSel.clear();
}
}
