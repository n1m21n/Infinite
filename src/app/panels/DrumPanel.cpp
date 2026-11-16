// Drum panel: the hardware face of the patch's Drum Sequencer nodes (internal/plans/drum-panel).
// The node on the canvas stays the source: every control here reads and writes the node's own fields,
// so save/load, undo, modulation and headless renders see no new state. Only open/dock/height are saved.
//
//  header : Drums | page chips (one per node) + page | Run | 1-16 17-32 | Fill        Lane Kit
//  body   : pad column (LED, pad, M S, level) | step grid | inspector (Lane or Kit)
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/SectionCard.h"
#include "app/ui/design/components/StepCell.h"
#include "audio/MusicTime.h"
#include "core/CategoryColors.h"
#include "core/Transport.h"
#include "nodes/DrumPatterns.h"
#include "nodes/DrumSequencerNode.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace app
{
   bool  gDrumPanelOpen = false;
   float gDrumPanelHeight = 340.0f;
   int   gDrumPanelDock = 0; // 0 bottom, 3 top
   bool  gDrumClaimedKeys = false;
   ImVec2 gDrumPanelRectMin(0.0f, 0.0f), gDrumPanelRectMax(0.0f, 0.0f);

   // The node body's groove helpers (SamplerBodies.cpp): the panel picks grooves the same way.
   void ApplyDrumGroove(DrumSequencerNode* n, int grooveIdx, int part);
   std::vector<std::string> ScanDrumGrooveFiles();
   std::string DrumGrooveStem(const std::string& path);
   void ApplyDrumGrooveFile(DrumSequencerNode* n, const std::string& path);
   void RevealDrumGrooveDir();

   namespace
   {
      constexpr int kLanes = DrumSequencerNode::kNumLanes;
      constexpr int kMaxSteps = DrumSequencerNode::kMaxSteps;
      constexpr int kPageSteps = DrumSequencerNode::kEditPageSteps;
      constexpr float kMinHeight = 240.0f;
      constexpr float kInspectorW = 268.0f;
      constexpr float kPadW = 216.0f;
      constexpr float kRulerH = 16.0f;
      constexpr float kRowGap = 3.0f;
      constexpr float kCellGap = 2.0f;
      constexpr float kDefaultVel = 0.8f;

      uint64_t sPageUid = 0;
      uint64_t sLastSelectedUid = 0;
      int sLane = 0;          // the lane the inspector shows
      bool sKit = false;      // inspector: false = Lane, true = Kit
      bool sFillAll = false;  // Fill menu scope
      float sPadFlash[kLanes] = {};

      // One gesture on the grid. Pending: pressed on a lit step, not moved yet (release = off,
      // vertical drag = velocity, drag onto another step = paint off).
      enum class Drag { None, Pending, Velocity, PaintOn, PaintOff, Erase };
      struct GridDrag
      {
         Drag mode = Drag::None;
         int lane = -1, step = -1;
         float startVel = 0.0f, startY = 0.0f;
      } sDrag;

      DrumSequencerNode* DrumOf(GraphNode* gn) { return gn ? dynamic_cast<DrumSequencerNode*>(gn->node.get()) : nullptr; }

      // D3: a page is a Drum Sequencer node, in canvas order.
      std::vector<GraphNode*> CollectPages()
      {
         std::vector<GraphNode*> out;
         for (GraphNode& gn : gNodes)
            if (DrumOf(&gn) != nullptr)
               out.push_back(&gn);
         return out;
      }

      int CurrentPage(const std::vector<GraphNode*>& pages)
      {
         for (int i = 0; i < (int)pages.size(); i++)
            if (pages[i]->uid == sPageUid)
               return i;
         if (pages.empty())
            return -1;
         sPageUid = pages[0]->uid;
         return 0;
      }

      uint64_t SelectedNodeUid()
      {
         if (gEditor == nullptr)
            return 0;
         ed::EditorContext* prev = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);
         uint64_t uid = 0;
         if (ed::GetSelectedObjectCount() == 1)
            for (GraphNode& gn : gNodes)
               if (ed::IsNodeSelected(gn.NodeId()))
               {
                  uid = gn.uid;
                  break;
               }
         ed::SetCurrentEditor(prev);
         return uid;
      }

      void SelectOnCanvas(GraphNode* gn)
      {
         if (gn == nullptr || gEditor == nullptr)
            return;
         ed::EditorContext* prev = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);
         ed::SelectNode(gn->NodeId(), false);
         ed::SetCurrentEditor(prev);
         sLastSelectedUid = gn->uid;
      }

      // Selecting a Drum Sequencer on the canvas brings its page up.
      void FollowCanvasSelection()
      {
         const uint64_t sel = SelectedNodeUid();
         if (sel == sLastSelectedUid)
            return;
         sLastSelectedUid = sel;
         if (DrumOf(sel ? FindNodeByUid(sel) : nullptr) != nullptr)
            sPageUid = sel;
      }

      // D5: one undo step per drag or toggle; the checkpoint holds the value from before the change.
      template <typename V> void UndoOnActivate(V* v, V before)
      {
         if (!ImGui::IsItemActivated())
            return;
         const V now = *v;
         *v = before;
         PushUndoCheckpoint("Drums");
         *v = now;
      }
      void UndoOnToggle(bool* v)
      {
         *v = !*v;
         PushUndoCheckpoint("Drums");
         *v = !*v;
      }

      float TextW(const char* s) { return ImGui::CalcTextSize(s, nullptr, true).x + 2.0f * tok::space_2; }

      bool TextChip(const char* label, bool on)
      {
         ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
         const bool clicked = ActionButton::Draw(label, ImVec2(TextW(label), tok::tile));
         ActionButton::Scoped() = ActionButton::Kind::Plain;
         return clicked;
      }

      ImU32 NodeColor(const GraphNode* gn, float alpha = 1.0f)
      {
         const CategoryColors::Color& c = CategoryColors::ColorFor(gn->category);
         return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, alpha));
      }

      std::string LaneName(DrumSequencerNode* n, int lane)
      {
         const std::string& fn = n->FileName(lane);
         if (fn.empty())
            return DrumPatterns::LaneRole(lane);
         const size_t dot = fn.find_last_of('.');
         return dot != std::string::npos && dot > 0 ? fn.substr(0, dot) : fn;
      }

      int StepsPerBeat(const DrumSequencerNode* n)
      {
         const double bps = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)n->rate));
         return std::max(1, (int)std::lround(1.0 / bps));
      }

      int PlayStep(DrumSequencerNode* n) { return n->run && Transport::Instance().IsPlaying() ? n->CurrentStep() : -1; }

      void Hit(DrumSequencerNode* n, int lane, float vel)
      {
         n->Audition(lane, vel);
         sPadFlash[lane] = 1.0f;
      }

      GraphNode* AddPage()
      {
         float x = 0.0f, y = 0.0f;
         bool any = false;
         for (const GraphNode& gn : gNodes)
         {
            x = any ? std::max(x, gn.liveX) : gn.liveX;
            y = any ? std::min(y, gn.liveY) : gn.liveY;
            any = true;
         }
         if (any)
            x += 420.0f;
         PushUndoCheckpoint("New drum page");
         const bool wasSuppressed = gSuppressUndoCheckpoints;
         gSuppressUndoCheckpoints = true;
         GraphNode* drum = SpawnNode("Drum Sequencer", "Synths", x, y);
         const uint64_t drumUid = drum ? drum->uid : 0;
         GraphNode* out = SpawnNode("Audio Out", "Utility", x + 960.0f, y);
         drum = FindNodeByUid(drumUid); // the spawn may move gNodes
         if (drum && out)
            WireInputSlot(*drum, *out, 0, 0);
         gSuppressUndoCheckpoints = wasSuppressed;
         RebuildAudioTopology();
         if (drum)
            sPageUid = drum->uid;
         return drum;
      }

      // ---- Fill tools (plan §4): pure step edits, one undo step each -----------------------------------
      enum class Fill
      {
         Every1, Every2, Every4, Every8, Offbeats, Euclid3, Euclid5, Euclid7, RollUp, RollDown, RampVel, Humanize,
         ShiftLeft, ShiftRight, Reverse, Invert, Randomise, Clear
      };

      void FillLane(DrumSequencerNode* n, int lane, Fill f)
      {
         const int N = std::clamp(n->numSteps, 1, kMaxSteps);
         float* v = n->stepVel[lane];
         const int spb = StepsPerBeat(n);
         auto set = [&](auto pred) {
            for (int i = 0; i < N; i++)
               v[i] = pred(i) ? kDefaultVel : 0.0f;
         };
         auto euclid = [&](int k) { set([&](int i) { return (i * k) % N < k; }); };
         switch (f)
         {
         case Fill::Every1: set([](int) { return true; }); break;
         case Fill::Every2: set([](int i) { return i % 2 == 0; }); break;
         case Fill::Every4: set([](int i) { return i % 4 == 0; }); break;
         case Fill::Every8: set([](int i) { return i % 8 == 0; }); break;
         case Fill::Offbeats:
            set([&](int i) { return spb >= 2 ? i % spb == spb / 2 : i % 2 == 1; });
            break;
         case Fill::Euclid3: euclid(3); break;
         case Fill::Euclid5: euclid(5); break;
         case Fill::Euclid7: euclid(7); break;
         case Fill::RollUp:
         case Fill::RollDown:
         {
            const int len = std::min(N, std::max(2, spb));
            for (int k = 0; k < len; k++)
            {
               const float t = len > 1 ? (float)k / (float)(len - 1) : 1.0f;
               v[N - len + k] = 0.3f + 0.7f * (f == Fill::RollUp ? t : 1.0f - t);
            }
            break;
         }
         case Fill::RampVel:
         {
            int hits = 0, k = 0;
            for (int i = 0; i < N; i++)
               hits += v[i] > 0.0f;
            for (int i = 0; i < N; i++)
               if (v[i] > 0.0f)
               {
                  v[i] = hits > 1 ? 0.3f + 0.7f * (float)k / (float)(hits - 1) : 1.0f;
                  k++;
               }
            break;
         }
         case Fill::Humanize:
         {
            static std::mt19937 rng(std::random_device {}());
            std::uniform_real_distribution<float> d(-0.15f, 0.15f);
            for (int i = 0; i < N; i++)
               if (v[i] > 0.0f)
                  v[i] = std::clamp(v[i] + d(rng), 0.05f, 1.0f);
            break;
         }
         case Fill::ShiftLeft: std::rotate(v, v + 1, v + N); break;
         case Fill::ShiftRight: std::rotate(v, v + N - 1, v + N); break;
         case Fill::Reverse: std::reverse(v, v + N); break;
         case Fill::Invert:
            for (int i = 0; i < N; i++)
               v[i] = v[i] > 0.0f ? 0.0f : kDefaultVel;
            break;
         case Fill::Randomise: n->RandomizeLane(lane); break;
         case Fill::Clear: std::fill(v, v + N, 0.0f); break;
         }
      }

      void ApplyFill(DrumSequencerNode* n, Fill f, const char* label)
      {
         PushUndoCheckpoint((std::string("Drums ") + label).c_str());
         if (sFillAll)
            for (int l = 0; l < kLanes; l++)
               FillLane(n, l, f);
         else
            FillLane(n, sLane, f);
      }

      // Double: the pattern twice over (16 steps become 32), for a fill on the second half.
      void DoublePattern(DrumSequencerNode* n)
      {
         const int N = std::clamp(n->numSteps, 1, kMaxSteps);
         if (N * 2 > kMaxSteps)
            return;
         PushUndoCheckpoint("Drums double");
         for (int l = 0; l < kLanes; l++)
            for (int i = 0; i < N; i++)
               n->stepVel[l][N + i] = n->stepVel[l][i];
         n->numSteps = N * 2;
         n->editPage = N * 2 > kPageSteps ? 1 : 0;
      }

      void DrawFillMenu(DrumSequencerNode* n)
      {
         if (TextChip((std::string(T("Fill")) + "##fill").c_str(), ImGui::IsPopupOpen("##drumFill")))
            ImGui::OpenPopup("##drumFill");
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Fill tools for the selected lane"));
         if (!MenuParts::BeginPopup("##drumFill"))
            return;
         MenuParts::Check(L("All lanes"), &sFillAll);
         MenuParts::Separator();
         struct Item { const char* label; Fill f; };
         static const Item kPattern[] = {
            { I18N_KEY("Every step"), Fill::Every1 }, { I18N_KEY("Every 2nd"), Fill::Every2 },
            { I18N_KEY("Every 4th"), Fill::Every4 },  { I18N_KEY("Every 8th"), Fill::Every8 },
            { I18N_KEY("Offbeats"), Fill::Offbeats },
         };
         static const Item kEuclid[] = { { "Euclid 3", Fill::Euclid3 }, { "Euclid 5", Fill::Euclid5 }, { "Euclid 7", Fill::Euclid7 } };
         static const Item kShape[] = {
            { I18N_KEY("Roll up"), Fill::RollUp },        { I18N_KEY("Roll down"), Fill::RollDown },
            { I18N_KEY("Ramp velocity"), Fill::RampVel }, { I18N_KEY("Humanize velocity"), Fill::Humanize },
         };
         static const Item kMove[] = {
            { I18N_KEY("Shift left"), Fill::ShiftLeft }, { I18N_KEY("Shift right"), Fill::ShiftRight },
            { I18N_KEY("Reverse"), Fill::Reverse },      { I18N_KEY("Invert"), Fill::Invert },
         };
         auto group = [&](const Item* items, int count) {
            for (int i = 0; i < count; i++)
               if (MenuParts::Item(L(items[i].label)))
                  ApplyFill(n, items[i].f, items[i].label);
         };
         group(kPattern, 5);
         group(kEuclid, 3);
         MenuParts::Separator();
         group(kShape, 4);
         MenuParts::Separator();
         group(kMove, 4);
         MenuParts::Separator();
         if (MenuParts::Item(L("Double pattern"), nullptr, false, n->numSteps * 2 <= kMaxSteps))
            DoublePattern(n);
         if (MenuParts::Item(L("Randomise")))
            ApplyFill(n, Fill::Randomise, "Randomise");
         if (MenuParts::Item(L("Clear")))
            ApplyFill(n, Fill::Clear, "Clear");
         MenuParts::EndPopup();
      }

      // ---- header ------------------------------------------------------------------------------------
      void DrawDockMenu(ImVec2 mn, ImVec2 mx)
      {
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(mn, mx) && !ImGui::IsAnyItemHovered() &&
             ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            ImGui::OpenPopup("##drumDockMenu");
         if (MenuParts::BeginPopup("##drumDockMenu"))
         {
            if (MenuParts::SubMenu(L("Dock Position")))
            {
               const bool top = gDrumPanelDock == 3;
               if (MenuParts::Item(L("Drums at Bottom"), nullptr, !top) && top)
                  gDrumPanelDock = 0;
               if (MenuParts::Item(L("Drums at Top"), nullptr, top) && !top)
                  gDrumPanelDock = 3;
               ImGui::EndMenu();
            }
            ImGui::Dummy(ImVec2(0.0f, tok::space_1));
            if (MenuParts::Item(L("Close Drums")))
               gDrumPanelOpen = false;
            MenuParts::EndPopup();
         }
      }

      void DrawHeader(const std::vector<GraphNode*>& pages, int current, ImVec2 origin, float width)
      {
         ImGui::SetCursorScreenPos(origin);
         ImGui::AlignTextToFramePadding();
         {
            UiType::Scope hs(UiType::Size::Title, UiType::Weight::Semibold);
            ImGui::TextUnformatted(T("Drums"));
         }
         ImGui::SameLine(0.0f, tok::space_3);
         for (int i = 0; i < (int)pages.size(); i++)
         {
            const std::string label = NodeTitle(*pages[i]) + "##page" + std::to_string(i);
            if (ChipButton::Draw(label.c_str(), i == current, tok::tile))
            {
               sPageUid = pages[i]->uid;
               SelectOnCanvas(pages[i]);
            }
            ImGui::SameLine(0.0f, tok::space_1);
         }
         if (ChipButton::Draw((std::string("+ ") + T("page") + "##addpage").c_str(), false, tok::tile))
            AddPage();
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("New Drum Sequencer + Audio Out"));

         DrumSequencerNode* n = current >= 0 ? DrumOf(pages[current]) : nullptr;
         if (n != nullptr)
         {
            ImGui::SameLine(0.0f, tok::space_3);
            if (ChipButton::Draw((std::string(T("Run")) + "##run").c_str(), n->run, tok::tile))
            {
               n->run = !n->run;
               UndoOnToggle(&n->run);
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Run this sequencer's steps (the transport keeps going)"));

            // D4: step pages, a dot on the one the playhead is on.
            const bool paged = n->numSteps > kPageSteps;
            const int play = PlayStep(n);
            ImGui::SameLine(0.0f, tok::space_3);
            if (!paged)
               ImGui::BeginDisabled();
            static const char* kPages[2] = { "1-16##p0", "17-32##p1" };
            for (int p = 0; p < 2; p++)
            {
               if (p > 0)
                  ImGui::SameLine(0.0f, 1.0f);
               if (ChipButton::Draw(kPages[p], paged && n->editPage == p, tok::tile))
                  n->editPage = p;
               if (paged && play >= 0 && play / kPageSteps == p)
               {
                  const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
                  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(mx.x - 5.0f, mn.y + 5.0f), 2.5f,
                                                              IsThemeLight() ? tok::U32(tok::pal::c_F59B19FF) : tok::U32(tok::pal::c_FFC850FF));
               }
            }
            if (!paged)
               ImGui::EndDisabled();
            ImGui::SameLine(0.0f, tok::space_3);
            DrawFillMenu(n);

            // Inspector chips pinned right.
            const float laneW = TextW(T("Lane")), kitW = TextW(T("Kit"));
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, origin.x + width - laneW - kitW - tok::space_1), origin.y));
            if (TextChip(T("Lane"), !sKit))
               sKit = false;
            ImGui::SameLine(0.0f, tok::space_1);
            if (TextChip(T("Kit"), sKit))
               sKit = true;
         }
         DrawDockMenu(origin, ImVec2(origin.x + width, origin.y + tok::tile));
      }

      // ---- pad column --------------------------------------------------------------------------------
      void DrawPads(GraphNode* gn, DrumSequencerNode* n, ImVec2 p, float rowH)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool light = IsThemeLight();
         const float btnW = 22.0f, knobCell = 28.0f;
         const float btnH = std::min(rowH, 22.0f);
         const float padX0 = p.x + 14.0f;
         const float padX1 = p.x + kPadW - knobCell - 2.0f * btnW - 3.0f * kCellGap - 4.0f;
         const float dt = ImGui::GetIO().DeltaTime;
         for (int lane = 0; lane < kLanes; lane++)
         {
            ImGui::PushID(lane);
            const float y = p.y + kRulerH + (float)lane * (rowH + kRowGap);
            const float cy = y + rowH * 0.5f;

            // Trigger LED: the loudest voice of this lane, or a pad hit.
            float pos[DrumSequencerNode::kVoicesPerLane], amp[DrumSequencerNode::kVoicesPerLane];
            n->LaneCursors(lane, pos, amp);
            float lit = 0.0f;
            for (int v = 0; v < DrumSequencerNode::kVoicesPerLane; v++)
               if (pos[v] >= 0.0f)
                  lit = std::max(lit, amp[v]);
            sPadFlash[lane] = std::max(0.0f, sPadFlash[lane] - dt * 6.0f);
            lit = std::clamp(std::max(lit, sPadFlash[lane]), 0.0f, 1.0f);
            dl->AddCircleFilled(ImVec2(p.x + 6.0f, cy), 3.5f, light ? tok::U32(tok::pal::c_B4BCCCFF) : tok::U32(tok::pal::c_404454FF));
            if (lit > 0.02f)
               dl->AddCircleFilled(ImVec2(p.x + 6.0f, cy), 3.5f, NodeColor(gn, lit));

            // Pad: click hits it and selects the lane (Alt = accent velocity).
            const ImVec2 pmn(padX0, y), pmx(padX1, y + rowH);
            ImGui::SetCursorScreenPos(pmn);
            ImGui::InvisibleButton("##pad", ImVec2(pmx.x - pmn.x, rowH));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemActivated())
            {
               sLane = lane;
               sKit = false;
               Hit(n, lane, ImGui::GetIO().KeyAlt ? 1.0f : kDefaultVel);
            }
            const bool sel = lane == sLane;
            dl->AddRectFilled(pmn, pmx, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), tok::radius_field);
            if (lit > 0.02f)
               dl->AddRectFilled(pmn, pmx, NodeColor(gn, 0.28f * lit), tok::radius_field);
            dl->AddRectFilled(pmn, ImVec2(pmn.x + 3.0f, pmx.y), NodeColor(gn, sel ? 1.0f : 0.45f), tok::radius_field,
                              ImDrawFlags_RoundCornersLeft);
            if (sel)
               dl->AddRect(pmn, pmx, NodeColor(gn), tok::radius_field, 0, 1.5f);
            char num[4];
            snprintf(num, sizeof num, "%d", lane + 1);
            const float th = ImGui::GetTextLineHeight();
            dl->AddText(ImVec2(pmn.x + 8.0f, cy - th * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), num);
            const std::string name = LaneName(n, lane);
            dl->PushClipRect(ImVec2(pmn.x + 22.0f, pmn.y), ImVec2(pmx.x - 4.0f, pmx.y), true);
            dl->AddText(ImVec2(pmn.x + 22.0f, cy - th * 0.5f),
                        ImGui::GetColorU32(n->FileName(lane).empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text), name.c_str());
            dl->PopClipRect();
            if (hovered)
               HelpTip("%s %d: %s\n%s", T("Lane"), lane + 1, name.c_str(), T("Click to play and select (Alt = accent). Keys 1-8 too."));

            // M S, then the lane level knob.
            float x = padX1 + kCellGap + 2.0f;
            ImGui::SetCursorScreenPos(ImVec2(x, cy - btnH * 0.5f));
            if (AudioMuteButton("M##m", &n->laneMute[lane], btnW, btnH))
               UndoOnToggle(&n->laneMute[lane]);
            x += btnW + kCellGap;
            ImGui::SetCursorScreenPos(ImVec2(x, cy - btnH * 0.5f));
            if (AudioSoloButton("S##s", &n->laneSolo[lane], btnW, btnH))
               UndoOnToggle(&n->laneSolo[lane]);
            x += btnW + kCellGap;
            const float d = std::clamp(rowH - 2.0f, 16.0f, 24.0f);
            ImGui::SetCursorScreenPos(ImVec2(x, cy - d * 0.5f));
            const float before = n->laneVolume[lane];
            KnobFloat("##lvl", &n->laneVolume[lane], 0.0f, 1.0f, "%.2f", d, NodeColor(gn), false, knobCell);
            UndoOnActivate(&n->laneVolume[lane], before);
            if (ImGui::IsItemHovered())
               HelpTip("%s %.2f", T("Level"), n->laneVolume[lane]);
            ImGui::PopID();
         }
      }

      // ---- step grid ---------------------------------------------------------------------------------
      void DrawGrid(GraphNode* gn, DrumSequencerNode* n, ImVec2 p, float w, float rowH)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool light = IsThemeLight();
         const int N = std::clamp(n->numSteps, 1, kMaxSteps);
         const bool paged = N > kPageSteps;
         n->editPage = paged ? std::clamp(n->editPage, 0, 1) : 0;
         const int base = n->editPage * kPageSteps;
         const int cols = paged ? kPageSteps : N;
         const int spb = StepsPerBeat(n);
         const int play = PlayStep(n);
         const float cellW = std::max(6.0f, (w - kCellGap * (float)(cols - 1)) / (float)cols);
         const float gridY = p.y + kRulerH;
         const float gridH = (float)kLanes * (rowH + kRowGap) - kRowGap;

         // Beat groups: every other beat sits on a lighter band, like the 4-step colour groups on a TR.
         for (int c = 0; c < cols; c++)
         {
            const int s = base + c;
            const float x0 = p.x + (float)c * (cellW + kCellGap);
            if ((s / spb) % 2 == 1)
               dl->AddRectFilled(ImVec2(x0 - kCellGap * 0.5f, gridY - 2.0f), ImVec2(x0 + cellW + kCellGap * 0.5f, gridY + gridH + 2.0f),
                                 ImGui::GetColorU32(ImGuiCol_Text, light ? 0.045f : 0.035f));
            // Ruler: step numbers, the beat starts brighter.
            char num[4];
            snprintf(num, sizeof num, "%d", s + 1);
            const ImVec2 ts = ImGui::CalcTextSize(num);
            if (cellW >= ts.x + 2.0f || s % spb == 0)
               dl->AddText(ImVec2(x0 + (cellW - ts.x) * 0.5f, p.y), ImGui::GetColorU32(s % spb == 0 ? ImGuiCol_Text : ImGuiCol_TextDisabled,
                                                                                        s < N ? 1.0f : 0.35f), num);
            if (s == play)
               dl->AddRectFilled(ImVec2(x0, p.y + kRulerH - 3.0f), ImVec2(x0 + cellW, p.y + kRulerH - 1.0f),
                                 light ? tok::U32(tok::pal::c_F59B19FF) : tok::U32(tok::pal::c_FFC850FF), 1.0f);
         }

         // One hit target for the whole grid; the cell under the mouse is worked out here.
         ImGui::SetCursorScreenPos(ImVec2(p.x, gridY));
         ImGui::InvisibleButton("##drumgrid", ImVec2(std::max(1.0f, w), gridH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
         const bool gridHovered = ImGui::IsItemHovered();
         const ImVec2 m = ImGui::GetIO().MousePos;
         auto cellAt = [&](int& lane, int& step) {
            const int c = (int)std::floor((m.x - p.x) / (cellW + kCellGap));
            const int l = (int)std::floor((m.y - gridY) / (rowH + kRowGap));
            lane = std::clamp(l, 0, kLanes - 1);
            step = base + std::clamp(c, 0, cols - 1);
            return c >= 0 && c < cols && l >= 0 && l < kLanes && base + c < N;
         };
         int hl = -1, hs = -1;
         const bool overCell = (gridHovered || sDrag.mode != Drag::None) && cellAt(hl, hs);
         const ImGuiIO& io = ImGui::GetIO();

         if (ImGui::IsItemActivated() && overCell)
         {
            PushUndoCheckpoint("Drums steps");
            float& v = n->stepVel[hl][hs];
            sDrag = { Drag::None, hl, hs, v, m.y };
            sLane = hl;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
               sDrag.mode = Drag::Erase;
               v = 0.0f;
            }
            else if (io.KeyAlt) // accent toggle
               v = v >= 0.99f ? kDefaultVel : 1.0f;
            else if (v > 0.0f)
               sDrag.mode = Drag::Pending;
            else
            {
               sDrag.mode = Drag::PaintOn;
               v = kDefaultVel;
            }
         }
         if (ImGui::IsItemActive() && sDrag.mode != Drag::None)
         {
            const bool sameCell = overCell && hl == sDrag.lane && hs == sDrag.step;
            if (sDrag.mode == Drag::Pending)
            {
               if (sameCell && std::fabs(m.y - sDrag.startY) > 3.0f)
                  sDrag.mode = Drag::Velocity;
               else if (overCell && !sameCell)
               {
                  sDrag.mode = Drag::PaintOff;
                  n->stepVel[sDrag.lane][sDrag.step] = 0.0f;
               }
            }
            if (sDrag.mode == Drag::Velocity)
            {
               float& v = n->stepVel[sDrag.lane][sDrag.step];
               v = std::clamp(sDrag.startVel + (sDrag.startY - m.y) * 0.006f, 0.05f, 1.0f);
               ImGui::SetTooltip("%s %.2f%s", T("velocity"), v, v >= 0.99f ? "  (accent)" : "");
            }
            else if (overCell && (sDrag.mode == Drag::PaintOn || sDrag.mode == Drag::PaintOff || sDrag.mode == Drag::Erase))
            {
               float& v = n->stepVel[hl][hs];
               if (sDrag.mode == Drag::PaintOn)
                  v = v > 0.0f ? v : kDefaultVel;
               else
                  v = 0.0f;
            }
         }
         if (sDrag.mode != Drag::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
         {
            if (sDrag.mode == Drag::Pending)
               n->stepVel[sDrag.lane][sDrag.step] = 0.0f; // a plain click on a lit step
            sDrag = {};
         }
         if (gridHovered && sDrag.mode == Drag::None)
            HelpTip("%s", T("Click: step on/off. Drag: paint. Drag up/down on a lit step: velocity.\nAlt-click: accent. Right-drag: erase."));

         // Cells.
         for (int lane = 0; lane < kLanes; lane++)
         {
            const float y0 = gridY + (float)lane * (rowH + kRowGap);
            if (lane == sLane)
               dl->AddRectFilled(ImVec2(p.x - 3.0f, y0 - 1.5f), ImVec2(p.x + w + 3.0f, y0 + rowH + 1.5f), NodeColor(gn, 0.10f), tok::radius_field);
            for (int c = 0; c < cols; c++)
            {
               const int s = base + c;
               const float x0 = p.x + (float)c * (cellW + kCellGap);
               const ImVec2 cMin(x0, y0), cMax(x0 + cellW, y0 + rowH);
               if (s >= N)
               {
                  dl->AddRect(cMin, cMax, ImGui::GetColorU32(ImGuiCol_Border, 0.35f), tok::radius_field);
                  continue;
               }
               StepCell::Draw(dl, cMin, cMax, false, false, overCell && hl == lane && hs == s);
               const float vel = n->stepVel[lane][s];
               if (vel > 0.0f)
               {
                  const bool accent = vel >= 0.99f, now = s == play;
                  const float top = y0 + (1.0f - vel) * (rowH - 2.0f);
                  const ImU32 fill = now ? (light ? tok::U32(tok::pal::c_F59B19FF) : tok::U32(tok::pal::c_FFC864FF))
                                         : accent ? NodeColor(gn, 1.0f) : NodeColor(gn, light ? 0.75f : 0.62f);
                  dl->AddRectFilled(ImVec2(x0 + 1.0f, top), ImVec2(cMax.x - 1.0f, cMax.y - 1.0f), fill, 2.0f);
                  if (accent)
                     dl->AddRectFilled(ImVec2(x0 + 2.0f, y0 + 2.0f), ImVec2(cMax.x - 2.0f, y0 + 4.0f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.85f)), 1.0f);
               }
               if (s == play)
                  StepCell::PlayheadOutline(dl, cMin, cMax);
            }
         }
      }

      // ---- inspector ---------------------------------------------------------------------------------
      // Knobs in a row of `count` equal cells across the inspector, captions underneath.
      struct KnobRow
      {
         float x0, y, cell, d;
         int i = 0;
         KnobRow(int count)
         {
            x0 = ImGui::GetCursorScreenPos().x;
            y = ImGui::GetCursorScreenPos().y;
            cell = ImGui::GetContentRegionAvail().x / (float)count;
            d = std::min(30.0f, cell - 12.0f);
         }
         void Place() { ImGui::SetCursorScreenPos(ImVec2(x0 + cell * (float)i++, y)); }
         void Knob(const char* label, float* v, float lo, float hi, const char* fmt, ImU32 col, bool bipolar = false)
         {
            Place();
            const float before = *v;
            if (bipolar)
               BipolarKnobFloat(label, v, lo, hi, fmt, d, col, false, cell, -1, -1, false, 0.0f, 0.0f, false, true);
            else
               KnobFloat(label, v, lo, hi, fmt, d, col, false, cell);
            UndoOnActivate(v, before);
            if (ImGui::IsItemHovered())
            {
               char buf[32];
               snprintf(buf, sizeof buf, fmt, *v);
               HelpTip("%s %s", label, buf);
            }
         }
         void End()
         {
            ImGui::SetCursorScreenPos(ImVec2(x0, y + d + kKnobCaptionGap + ImGui::GetTextLineHeight() + tok::space_1));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
         }
      };

      void DrawLaneInspector(GraphNode* gn, DrumSequencerNode* n)
      {
         const int lane = std::clamp(sLane, 0, kLanes - 1);
         const ImU32 col = NodeColor(gn);
         ImGui::PushID(lane);
         char title[64];
         snprintf(title, sizeof title, "%s %d  %s", T("Lane"), lane + 1, LaneName(n, lane).c_str());
         SectionCard::Begin(title);
         const float w = ImGui::GetContentRegionAvail().x;
         float pos[DrumSequencerNode::kVoicesPerLane], amp[DrumSequencerNode::kVoicesPerLane];
         n->LaneCursors(lane, pos, amp);
         WaveCursor cursors[DrumSequencerNode::kVoicesPerLane];
         for (int v = 0; v < DrumSequencerNode::kVoicesPerLane; v++)
            cursors[v] = { pos[v], amp[v] };
         const float s0 = n->laneStart[lane], e0 = n->laneEnd[lane];
         const TrimWaveInput in = DrawTrimWave("##lanewave", n->lanePeaks[lane], &n->laneStart[lane], &n->laneEnd[lane], w, 84.0f, cursors,
                                               DrumSequencerNode::kVoicesPerLane, -1.0f, T("drop sample / click to load"));
         if (n->lanePeaks[lane].Empty() && in.activated)
         {
            const std::string path = Platform::OpenAudioDialog();
            if (!path.empty())
            {
               PushUndoCheckpoint("Drums load sample");
               n->LoadFileToLane(lane, path);
            }
         }
         else if (in.activated)
            Hit(n, lane, kDefaultVel);
         if ((n->laneStart[lane] != s0 || n->laneEnd[lane] != e0) && ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
         {
            const float s1 = n->laneStart[lane], e1 = n->laneEnd[lane];
            n->laneStart[lane] = s0;
            n->laneEnd[lane] = e0;
            PushUndoCheckpoint("Drums trim");
            n->laneStart[lane] = s1;
            n->laneEnd[lane] = e1;
         }
         const float bw = (w - 2.0f * tok::space_1) / 3.0f;
         if (ActionButton::Draw(T("Play"), ImVec2(bw, 0.0f)))
            Hit(n, lane, kDefaultVel);
         ImGui::SameLine(0.0f, tok::space_1);
         if (ActionButton::Draw(T("Load"), ImVec2(bw, 0.0f)))
         {
            const std::string path = Platform::OpenAudioDialog();
            if (!path.empty())
            {
               PushUndoCheckpoint("Drums load sample");
               n->LoadFileToLane(lane, path);
            }
         }
         ImGui::SameLine(0.0f, tok::space_1);
         if (ActionButton::Draw(T("Clear"), ImVec2(bw, 0.0f)))
         {
            PushUndoCheckpoint("Drums clear sample");
            n->ClearLane(lane);
         }
         const std::string& status = n->LaneStatus(lane);
         if (!status.empty())
            ImGui::TextDisabled("%s", status.c_str());

         SectionCard::Begin(T("Sound"));
         {
            KnobRow r(4);
            r.Knob("tune", &n->lanePitch[lane], -24.0f, 24.0f, "%+.1f st", col, true);
            r.Knob("fine", &n->laneFineTune[lane], -50.0f, 50.0f, "%+.0f c", col, true);
            r.Knob("decay", &n->laneDecay[lane], 0.0f, 1.0f, "%.2f", col);
            r.Knob("attack", &n->laneTransient[lane], -1.0f, 1.0f, "%+.2f", col, true);
            r.End();
         }
         {
            KnobRow r(4);
            r.Knob("level", &n->laneVolume[lane], 0.0f, 1.0f, "%.2f", col);
            r.Knob("pan", &n->lanePan[lane], -1.0f, 1.0f, "%+.2f", col, true);
            r.Knob("acc tune", &n->laneAccentPitch[lane], -24.0f, 24.0f, "%+.1f st", col, true);
            r.Knob("acc level", &n->laneAccentLevel[lane], 0.0f, 12.0f, "%+.1f dB", col);
            r.End();
         }

         SectionCard::Begin(T("Lane"));
         SectionCard::RowLabel(T("Choke"));
         static const char* kChoke[4] = { "off##c0", "1##c1", "2##c2", "3##c3" };
         for (int c = 0; c < 4; c++)
         {
            if (c > 0)
               ImGui::SameLine(0.0f, 1.0f);
            if (ChipButton::Draw(kChoke[c], n->laneChoke[lane] == c, tok::tile, 30.0f) && n->laneChoke[lane] != c)
            {
               PushUndoCheckpoint("Drums choke");
               n->laneChoke[lane] = c;
            }
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Lanes in one choke group cut each other off (closed hat stops the open hat)"));
         SectionCard::RowLabel(T("Mix"));
         if (AudioMuteButton("M##im", &n->laneMute[lane], 30.0f, tok::tile))
            UndoOnToggle(&n->laneMute[lane]);
         ImGui::SameLine(0.0f, 1.0f);
         if (AudioSoloButton("S##is", &n->laneSolo[lane], 30.0f, tok::tile))
            UndoOnToggle(&n->laneSolo[lane]);
         ImGui::PopID();
      }

      // The groove library by category, then My grooves (the .mid folder).
      void DrawGroovePicker(DrumSequencerNode* n, float w)
      {
         const char* face = n->patternName.empty() ? T("Pick a groove") : n->patternName.c_str();
         if (ActionButton::Draw((std::string(face) + "##groove").c_str(), ImVec2(w, tok::tile)))
            ImGui::OpenPopup("##drumGroove");
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Replaces the pattern, rate, swing and steps; fills empty lanes from the kit"));
         static std::vector<std::string> sFiles;
         if (ImGui::IsItemActivated())
            sFiles = ScanDrumGrooveFiles();
         if (!MenuParts::BeginPopup("##drumGroove"))
            return;
         int count = 0, nc = 0;
         const DrumPatterns::Groove* all = DrumPatterns::All(count);
         const char* const* cats = DrumPatterns::Categories(nc);
         for (int c = 0; c < nc; c++)
         {
            if (!MenuParts::SubMenu(cats[c]))
               continue;
            for (int i = 0; i < count; i++)
               if (std::string(all[i].category) == cats[c])
                  if (MenuParts::Item((std::string(all[i].name) + "##g" + std::to_string(i)).c_str(), nullptr, n->patternName == all[i].name))
                     ApplyDrumGroove(n, i, n->patternPart);
            ImGui::EndMenu();
         }
         MenuParts::Separator();
         if (MenuParts::SubMenu(L("My grooves")))
         {
            for (size_t f = 0; f < sFiles.size(); f++)
            {
               const std::string stem = DrumGrooveStem(sFiles[f]);
               if (MenuParts::Item((stem + "##f" + std::to_string(f)).c_str(), nullptr, n->patternName == stem))
                  ApplyDrumGrooveFile(n, sFiles[f]);
            }
            if (!sFiles.empty())
               MenuParts::Separator();
            if (MenuParts::Item(L("Open grooves folder")))
               RevealDrumGrooveDir();
            ImGui::EndMenu();
         }
         MenuParts::EndPopup();
      }

      void DrawKitInspector(GraphNode* gn, DrumSequencerNode* n)
      {
         const ImU32 col = NodeColor(gn);
         SectionCard::Begin(T("Groove"));
         const float w = ImGui::GetContentRegionAvail().x;
         DrawGroovePicker(n, w);
         int count = 0, current = -1;
         const DrumPatterns::Groove* all = DrumPatterns::All(count);
         for (int i = 0; i < count && current < 0; i++)
            if (n->patternName == all[i].name)
               current = i;
         SectionCard::RowLabel(T("Part"));
         if (current < 0)
            ImGui::BeginDisabled();
         static const char* kParts[3] = { "A##pa", "B##pb", "C##pc" };
         for (int p = 0; p < 3; p++)
         {
            if (p > 0)
               ImGui::SameLine(0.0f, 1.0f);
            if (ChipButton::Draw(kParts[p], current >= 0 && n->patternPart == p, tok::tile, 30.0f) && current >= 0)
               ApplyDrumGroove(n, current, p);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               HelpTip("%s", DrumPatterns::PartName(p));
         }
         if (current < 0)
            ImGui::EndDisabled();

         SectionCard::Begin(T("Pattern"));
         SectionCard::RowLabel(T("Rate"));
         if (ActionButton::Draw((std::string(MusicTime::RateDivisionName(n->rate)) + "##rate").c_str(), ImVec2(-1.0f, tok::tile)))
            ImGui::OpenPopup("##drumRate");
         if (MenuParts::BeginPopup("##drumRate"))
         {
            const std::vector<std::string>& rates = MusicTime::RateDivisionList();
            for (int i = 0; i < (int)rates.size(); i++)
               if (MenuParts::Item(rates[i].c_str(), nullptr, i == n->rate) && i != n->rate)
               {
                  PushUndoCheckpoint("Drums rate");
                  n->rate = i;
               }
            MenuParts::EndPopup();
         }
         {
            KnobRow r(3);
            r.Place();
            float steps = (float)n->numSteps;
            const int before = n->numSteps;
            KnobFloat("steps", &steps, 1.0f, (float)kMaxSteps, "%.0f", r.d, col, false, r.cell);
            n->numSteps = std::clamp((int)std::lround(steps), 1, kMaxSteps);
            UndoOnActivate(&n->numSteps, before);
            if (ImGui::IsItemHovered())
               HelpTip("%s %d", T("steps"), n->numSteps);
            r.Knob("swing", &n->swing, 0.0f, 1.0f, "%.2f", col);
            r.Knob("output", &n->volume, 0.0f, 1.0f, "%.2f", col);
            r.End();
         }

         SectionCard::Begin(T("All lanes"));
         {
            KnobRow r(4);
            r.Knob("attack", &n->globalTransient, -1.0f, 1.0f, "%+.2f", col, true);
            r.Knob("decay", &n->globalDecay, -1.0f, 1.0f, "%+.2f", col, true);
            r.Knob("tune", &n->globalPitch, -24.0f, 24.0f, "%+.1f st", col, true);
            r.Knob("pan", &n->globalPan, -1.0f, 1.0f, "%+.2f", col, true);
            r.End();
         }
         const float bw = (w - tok::space_1) * 0.5f;
         if (ActionButton::Draw(T("Randomise"), ImVec2(bw, 0.0f)))
         {
            PushUndoCheckpoint("Drums randomise");
            n->Randomize();
         }
         ImGui::SameLine(0.0f, tok::space_1);
         if (ActionButton::Draw(T("Clear"), ImVec2(bw, 0.0f)))
         {
            PushUndoCheckpoint("Drums clear");
            n->ClearPattern();
         }
      }

      void DrawInspector(GraphNode* gn, DrumSequencerNode* n, ImVec2 at, ImVec2 size)
      {
         ImGui::SetCursorScreenPos(at);
         ImGui::BeginChild("##drumInspector", size, ImGuiChildFlags_AlwaysUseWindowPadding, 0);
         SectionCard::BeginWindow();
         if (sKit)
            DrawKitInspector(gn, n);
         else
            DrawLaneInspector(gn, n);
         SectionCard::EndWindow();
         ImGui::EndChild();
      }

      void DrawEmpty(ImVec2 mn, ImVec2 mx)
      {
         EmptyState::DrawTo(ImGui::GetWindowDrawList(), mn, ImVec2(mx.x, mn.y + (mx.y - mn.y) * 0.6f), T("No Drum Sequencer yet"));
         const char* label = T("New Drum Sequencer");
         const float w = ImGui::CalcTextSize(label).x + 2.0f * tok::space_3;
         ImGui::SetCursorScreenPos(ImVec2((mn.x + mx.x - w) * 0.5f, mn.y + (mx.y - mn.y) * 0.6f));
         if (ActionButton::Draw(label, ImVec2(w, 0.0f), ActionButton::Kind::Primary))
            AddPage();
      }

      // Keys 1-8 play the pads while the pointer is over the panel.
      void DrawKeys(DrumSequencerNode* n)
      {
         if (n == nullptr || !gDrumClaimedKeys)
            return;
         const ImGuiIO& io = ImGui::GetIO();
         if (io.KeyCtrl || io.KeySuper)
            return;
         for (int l = 0; l < kLanes; l++)
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + l), false))
            {
               sLane = l;
               Hit(n, l, io.KeyShift ? 1.0f : kDefaultVel);
            }
      }

      void DrawPanel()
      {
         FollowCanvasSelection();
         const std::vector<GraphNode*> pages = CollectPages();
         const int current = CurrentPage(pages);
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 avail = ImGui::GetContentRegionAvail();
         DrawHeader(pages, current, origin, avail.x);

         const ImVec2 bodyMin(origin.x, origin.y + tok::tile + tok::space_2);
         const ImVec2 bodyMax(origin.x + avail.x, origin.y + avail.y);
         GraphNode* gn = current >= 0 ? pages[current] : nullptr;
         DrumSequencerNode* n = DrumOf(gn);
         if (n == nullptr)
         {
            if (bodyMax.y > bodyMin.y)
               DrawEmpty(bodyMin, bodyMax);
         }
         else if (bodyMax.y - bodyMin.y > 60.0f)
         {
            const float bodyH = bodyMax.y - bodyMin.y;
            const bool inspector = avail.x >= kPadW + kInspectorW + 240.0f;
            const float gridX0 = bodyMin.x + kPadW + tok::space_2;
            const float gridX1 = inspector ? bodyMax.x - kInspectorW - tok::space_2 : bodyMax.x;
            const float rowH = std::clamp((bodyH - kRulerH - (float)(kLanes - 1) * kRowGap) / (float)kLanes, 14.0f, 40.0f);
            DrawPads(gn, n, bodyMin, rowH);
            DrawGrid(gn, n, ImVec2(gridX0, bodyMin.y), gridX1 - gridX0, rowH);
            if (inspector)
               DrawInspector(gn, n, ImVec2(bodyMax.x - kInspectorW, bodyMin.y), ImVec2(kInspectorW, bodyH));
         }
         gDrumClaimedKeys = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput;
         DrawKeys(n);
      }
   }

   // D10: the canvas node's "Open in Drum panel".
   void DrumPanelShowNode(int nodeIndex)
   {
      gDrumPanelOpen = true;
      GraphNode* gn = FindNodeByIndex(nodeIndex);
      sLastSelectedUid = SelectedNodeUid(); // the menu's own selection must not move the page again
      if (DrumOf(gn) != nullptr)
         sPageUid = gn->uid;
   }

   void DrawDrumPanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = PanelFrame::kGap;
      const bool gripFirst = gDrumPanelDock != 3; // bottom: the grip sits on top
      ImGui::BeginChild(id, size, false);
      gDrumPanelRectMin = ImGui::GetWindowPos();
      gDrumPanelRectMax = ImVec2(gDrumPanelRectMin.x + ImGui::GetWindowSize().x, gDrumPanelRectMin.y + ImGui::GetWindowSize().y);
      gDrumClaimedKeys = false;
      const ImVec2 inner = ImGui::GetContentRegionAvail();
      auto grip = [&]()
      {
         ImGui::InvisibleButton("##drumgrip", ImVec2(std::max(1.0f, inner.x), kGrip));
         DrawPanelSeam(false, gripFirst);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsItemActive())
         {
            gDrumPanelHeight += gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y;
            gDrumPanelHeight = std::max(kMinHeight, gDrumPanelHeight);
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
      PanelFrame::BeginCard("##drumpanelcontent", cardSize, in);
      DrawPanel();
      PanelFrame::EndCard();
      if (!gripFirst)
         grip();
      const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
      ImGui::EndChild();
      ImGui::GetStyle().ItemSpacing = savedSpacing;
   }
}
