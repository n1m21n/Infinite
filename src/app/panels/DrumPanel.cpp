// Drum panel: the hardware face of the patch's Drum Sequencer nodes (internal/plans/drum-panel).
// The node on the canvas stays the source: every control here reads and writes the node's own fields,
// so save/load, undo, modulation and headless renders see no new state. Only open/dock/height are saved.
//
//  header : Drums | page chips (one per node) + page | Run | < groove > | 1-16 17-32 | FILL (hold) | Tools
//           ...                                                                       Lane Step Kit
//           every control is a ChipButton at tok::control_h height, the Mixer header's own component.
//  body   : lane labels (Mixer name row: category bar, dot, name, M S) | step grid | inspector
//           (Lane, Step or Kit). Lanes are generic: the name is the loaded sample, "Empty" otherwise.
// Drawn in the Mixer's language: FrameBg wells, one SliderGrab accent, ring/dot LEDs, no bevels.
#include "app/AppShared.h"
#include "app/ui/design/UiDensity.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/DeskParts.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PagePicker.h"
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
   float gDrumPanelHeight = 400.0f;
   int   gDrumPanelDock = 0; // 0 bottom, 3 top
   bool  gDrumClaimedKeys = false;
   float gDrumPanelNaturalH = 400.0f; // the height that shows every lane at full row height and no more
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
      constexpr float kInspectorW = UiDensity::kInspectorW + 2.0f * tok::space_2;   // one compact strip
      constexpr float kRowH = tok::row_h;  // a lane row: fixed, so the grid never squashes as the panel shrinks
      constexpr float kLabelW = 178.0f;  // lane name + M S, left of the grid
      constexpr float kRulerH = tok::row_h;
      constexpr float kGroupGap = 4.0f; // extra space between four-step key groups
      constexpr float kRowGap = tok::row_gap;
      constexpr float kCellGap = 2.0f;
      constexpr float kDefaultVel = 0.8f;

      uint64_t sPageUid = 0;
      uint64_t sLastSelectedUid = 0;
      int sLane = 0;          // the lane the inspector shows
      enum Tab { kTabLane, kTabStep, kTabKit };
      int sTab = kTabLane;    // inspector page
      int sStep = 0;          // the step the Step page shows (lane = sLane)
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
         // The page strip's own order (Move Left/Right, drag); ties keep canvas order.
         std::stable_sort(out.begin(), out.end(), [](GraphNode* a, GraphNode* b) { return DrumOf(a)->pageOrder < DrumOf(b)->pageOrder; });
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

      ImU32 NodeColor(const GraphNode* gn, float alpha = 1.0f)
      {
         const CategoryColors::Color& c = CategoryColors::ColorFor(gn->category);
         return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, alpha));
      }

      // One accent for lit keys, LEDs and strip bars: the Mixer's fader colour.
      ImU32 Accent(float alpha = 1.0f) { return ImGui::GetColorU32(ImGuiCol_SliderGrab, alpha); }

      // The rhythm-composer step keys: four groups of four in red, orange, yellow and cream, a red LED on each.
      // Off keys sit dim in their group colour; a set step lights its key and LED, brightness = velocity.
      ImU32 TrKey(int step, float k)
      {
         static const ImU32 kGroup[4] = { IM_COL32(196, 58, 44, 255), IM_COL32(226, 120, 38, 255), IM_COL32(230, 192, 64, 255),
                                          IM_COL32(226, 220, 200, 255) };
         return DeskParts::Tint(kGroup[(step / 4) & 3], k);
      }
      constexpr ImU32 kTrLed = IM_COL32(255, 52, 36, 255);

      std::string LaneName(DrumSequencerNode* n, int lane)
      {
         const std::string& fn = n->FileName(lane);
         if (fn.empty())
            return T("Empty");
         const size_t dot = fn.find_last_of('.');
         return dot != std::string::npos && dot > 0 ? fn.substr(0, dot) : fn;
      }

      int StepsPerBeat(const DrumSequencerNode* n)
      {
         const double bps = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)n->rate));
         return std::max(1, (int)std::lround(1.0 / bps));
      }

      int PlayStep(DrumSequencerNode* n) { return n->run && Transport::Instance().IsPlaying() ? n->CurrentStep() : -1; }
      int PlayStep(DrumSequencerNode* n, int lane)
      {
         return n->run && Transport::Instance().IsPlaying() ? n->LaneCurrentStep(lane) : -1;
      }

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
         const int N = n->LaneStepCount(lane);
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
         case Fill::Clear:
            std::fill(v, v + N, 0.0f);
            n->ResetTrigs(lane);
            break;
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
            {
               n->stepVel[l][N + i] = n->stepVel[l][i];
               n->stepProb[l][N + i] = n->stepProb[l][i];
               n->stepCond[l][N + i] = n->stepCond[l][i];
               n->stepNudge[l][N + i] = n->stepNudge[l][i];
            }
         n->numSteps = N * 2;
         n->editPage = N * 2 > kPageSteps ? 1 : 0;
      }

      void DrawFillMenu(DrumSequencerNode* n)
      {
         if (ChipButton::Draw((std::string(T("Tools")) + "##fill").c_str(), ImGui::IsPopupOpen("##drumFill"), tok::control_h))
            ImGui::OpenPopup("##drumFill");
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Pattern tools for the selected lane"));
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

      int GrooveIndex(const DrumSequencerNode* n)
      {
         int count = 0;
         const DrumPatterns::Groove* all = DrumPatterns::All(count);
         for (int i = 0; i < count; i++)
            if (n->patternName == all[i].name)
               return i;
         return -1;
      }

      // FILL: held = fill-conditioned steps play (TR / Elektron). Shift-click latches it.
      void DrawFillChip(DrumSequencerNode* n)
      {
         static bool sLatched = false;
         const bool clicked = ChipButton::Draw("FILL##fillkey", n->fill, tok::control_h);
         const bool held = ImGui::IsItemActive();
         if (clicked && ImGui::GetIO().KeyShift)
            sLatched = !sLatched;
         n->fill = sLatched || held;
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Hold: steps set to Fill play, !Fill rest. Shift-click latches."));
      }

      void DrawGrooveMenu(DrumSequencerNode* n);
      std::vector<std::string>& GrooveFiles();
      constexpr float kGrooveW = 240.0f; // fixed: a long groove name never moves the bar

      // < groove > : the arrows step the library like the node body's; the face is a dropdown of the whole
      // library and reads out part, rate, steps and swing.
      void DrawGrooveChips(DrumSequencerNode* n)
      {
         int count = 0;
         DrumPatterns::All(count);
         const int cur = GrooveIndex(n);
         for (int side = 0; side < 2; side++)
         {
            if (side == 1)
            {
               std::string name = n->patternName.empty() ? std::string(T("no groove")) : n->patternName;
               if (!n->patternName.empty() && cur >= 0)
                  name += std::string(" ") + "ABC"[std::clamp(n->patternPart, 0, 2)];
               char tail[48];
               snprintf(tail, sizeof tail, "%s  %d  %d%%", MusicTime::RateDivisionName(n->rate), std::clamp(n->numSteps, 1, kMaxSteps),
                        (int)std::lround(n->swing * 100.0f));
               const bool clicked = ActionButton::Draw("##grooveFace", ImVec2(kGrooveW, tok::control_h));
               if (ImGui::IsItemHovered())
                  HelpTip("%s", T("Groove: pick from the library"));
               if (ImGui::IsItemActivated())
                  GrooveFiles() = ScanDrumGrooveFiles();
               const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const float ty = (bmin.y + bmax.y - ImGui::GetFontSize()) * 0.5f;
               const float tailW = ImGui::CalcTextSize(tail).x;
               const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
               dl->AddText(ImVec2(bmax.x - tok::space_3 - tailW, ty), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, t.w * 0.6f)), tail);
               dl->PushClipRect(bmin, ImVec2(bmax.x - tok::space_3 - tailW - tok::space_2, bmax.y), true);
               dl->AddText(ImVec2(bmin.x + tok::space_3, ty), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
               dl->PopClipRect();
               if (clicked)
                  ImGui::OpenPopup("##drumGroove");
               ImGui::SetNextWindowPos(ImVec2(bmin.x, bmax.y + tok::space_1));
               DrawGrooveMenu(n);
               ImGui::SameLine(0.0f, tok::space_1);
            }
            if (ChipButton::Draw(side == 0 ? "<##groovePrev" : ">##grooveNext", false, tok::control_h) && count > 0)
               ApplyDrumGroove(n, cur < 0 ? 0 : (cur + (side == 0 ? count - 1 : 1)) % count, n->patternPart);
            if (ImGui::IsItemHovered())
               HelpTip("%s", side == 0 ? T("Previous groove") : T("Next groove"));
            if (side == 0)
               ImGui::SameLine(0.0f, tok::space_1);
         }
      }

      bool sPendingAdd = false;
      uint64_t sPendingDelete = 0;

      // Page adds and deletes from the header land at the top of the next frame, before pages are collected,
      // so no GraphNode* held this frame outlives a spawn or a removal.
      void ApplyPendingPageEdits()
      {
         if (sPendingAdd)
         {
            sPendingAdd = false;
            AddPage();
         }
         if (sPendingDelete != 0)
         {
            GraphNode* gn = FindNodeByUid(sPendingDelete);
            sPendingDelete = 0;
            if (gn != nullptr)
            {
               PushUndoCheckpoint("Delete drum page");
               sPageUid = 0;
               RemoveNodeByIndex(gn->index);
               RebuildAudioTopology();
            }
         }
      }

      std::vector<PagePicker::Entry> PageEntries(const std::vector<GraphNode*>& pages)
      {
         std::vector<PagePicker::Entry> out;
         for (GraphNode* gn : pages)
         {
            const DrumSequencerNode* d = DrumOf(gn);
            out.push_back({ d != nullptr && !d->pageName.empty() ? d->pageName : NodeTitle(*gn), d != nullptr ? d->pageColor : 0u });
         }
         return out;
      }

      void DrawPagePicker(const std::vector<GraphNode*>& pages, int current)
      {
         const std::vector<PagePicker::Entry> entries = PageEntries(pages);
         PagePicker::RenameKey("##drumPage", entries, current);
         PagePicker::Options opt;
         const PagePicker::Result r = PagePicker::Draw("##drumPage", entries, current, T("New drum page"), opt);
         GraphNode* gn = r.index >= 0 && r.index < (int)pages.size() ? pages[r.index] : nullptr;
         DrumSequencerNode* d = DrumOf(gn);
         switch (r.act)
         {
         case PagePicker::Act::Select:
            sPageUid = gn->uid;
            SelectOnCanvas(gn);
            break;
         case PagePicker::Act::Add: sPendingAdd = true; break;
         case PagePicker::Act::Rename:
            if (d != nullptr)
            {
               PushUndoCheckpoint("Rename drum page");
               d->pageName = r.name;
            }
            break;
         case PagePicker::Act::Recolor:
            if (d != nullptr)
            {
               PushUndoCheckpoint("Colour drum page");
               d->pageColor = r.color;
            }
            break;
         case PagePicker::Act::Delete:
            if (gn != nullptr)
               sPendingDelete = gn->uid; // next frame: the rest of this frame still draws the page
            break;
         case PagePicker::Act::Move:
         {
            std::vector<GraphNode*> order = pages;
            GraphNode* moving = order[r.index];
            order.erase(order.begin() + r.index);
            order.insert(order.begin() + r.to, moving);
            PushUndoCheckpoint("Reorder drum pages");
            for (size_t i = 0; i < order.size(); i++)
               if (DrumSequencerNode* d = DrumOf(order[i]))
                  d->pageOrder = (int)i;
            gPatchDirty = true;
            break;
         }
         case PagePicker::Act::Duplicate:
            if (gn != nullptr)
            {
               // The canvas duplicate (Shift+D) on this page's node; the copy is selected and its page comes up.
               ed::EditorContext* prev = ed::GetCurrentEditor();
               if (gEditor != nullptr)
               {
                  ed::SetCurrentEditor(gEditor);
                  ed::ClearSelection();
                  ed::SetCurrentEditor(prev);
               }
               SelectOnCanvas(gn);
               gRequestDuplicate = true;
            }
            break;
         default: break;
         }
      }

      void DrawHeader(const std::vector<GraphNode*>& pages, int current, ImVec2 origin, float width)
      {
         ImGui::SetCursorScreenPos(origin);
         // No title: the rail icon names the panel. The page dropdown leads, as in the Piano Roll.
         DrawPagePicker(pages, current);

         DrumSequencerNode* n = current >= 0 ? DrumOf(pages[current]) : nullptr;
         if (n != nullptr)
         {
            ImGui::SameLine(0.0f, tok::space_3);
            if (ChipButton::Draw((std::string(T("Run")) + "##run").c_str(), n->run, tok::control_h))
            {
               n->run = !n->run;
               UndoOnToggle(&n->run);
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Run this sequencer's steps (the transport keeps going)"));

            ImGui::SameLine(0.0f, tok::space_3);
            DrawGrooveChips(n);

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
                  ImGui::SameLine(0.0f, tok::space_1);
               if (ChipButton::Draw(kPages[p], paged && n->editPage == p, tok::control_h))
                  n->editPage = p;
               if (paged && play >= 0 && play / kPageSteps == p)
               {
                  const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
                  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(mx.x - 5.0f, mn.y + 5.0f), 2.5f, ImGui::GetColorU32(ImGuiCol_CheckMark));
               }
            }
            if (!paged)
               ImGui::EndDisabled();
            ImGui::SameLine(0.0f, tok::space_3);
            DrawFillChip(n);
            ImGui::SameLine(0.0f, tok::space_1);
            DrawFillMenu(n);

            // Inspector pages pinned right.
            const char* kTabs[3] = { T("Lane"), T("Step"), T("Kit") };
            const float tabsW = TextW(kTabs[0]) + TextW(kTabs[1]) + TextW(kTabs[2]) + 2.0f * tok::space_1;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, origin.x + width - tabsW), origin.y));
            for (int t = 0; t < 3; t++)
            {
               if (t > 0)
                  ImGui::SameLine(0.0f, tok::space_1);
               if (ChipButton::Draw(kTabs[t], sTab == t, tok::control_h))
                  sTab = t;
            }
         }
         DrawDockMenu(origin, ImVec2(origin.x + width, origin.y + tok::control_h));
      }

      // ---- pad column --------------------------------------------------------------------------------
      // How lit a lane's trigger LED is: its loudest sounding voice, or a pad hit.
      float LaneLit(DrumSequencerNode* n, int lane)
      {
         float pos[DrumSequencerNode::kVoicesPerLane], amp[DrumSequencerNode::kVoicesPerLane];
         n->LaneCursors(lane, pos, amp);
         float lit = sPadFlash[lane];
         for (int v = 0; v < DrumSequencerNode::kVoicesPerLane; v++)
            if (pos[v] >= 0.0f)
               lit = std::max(lit, amp[v]);
         return std::clamp(lit, 0.0f, 1.0f);
      }

      void PickLane(DrumSequencerNode* n, int lane)
      {
         sLane = lane;
         if (sTab == kTabKit)
            sTab = kTabLane;
         Hit(n, lane, ImGui::GetIO().KeyAlt ? 1.0f : kDefaultVel);
      }

      // ---- lane labels (left of the grid) -------------------------------------------------------------
      void DrawLabels(GraphNode* gn, DrumSequencerNode* n, ImVec2 p, float rowH, float x1)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float keyW = 22.0f, meterW = 14.0f, pad = 3.0f;
         const float keyH = std::min(rowH, 24.0f);
         const float scribbleX1 = p.x + kLabelW - (2.0f * keyW + meterW + 4.0f * pad);
         for (int lane = 0; lane < kLanes; lane++)
         {
            ImGui::PushID(lane);
            const float y = p.y + kRulerH + (float)lane * (rowH + kRowGap);
            const float cy = y + rowH * 0.5f;
            const bool loaded = !n->FileName(lane).empty();
            const bool sel = lane == sLane;
            // The lane is a horizontal channel strip: graphite housing, the colour cap down its left edge.
            DeskParts::FaceSide(dl, ImVec2(p.x, y - 2.0f), ImVec2(x1, y + rowH + 2.0f), NodeColor(gn, loaded ? 1.0f : 0.35f), sel);
            const ImVec2 mn(p.x + 9.0f, y), mx(scribbleX1, y + rowH);
            ImGui::SetCursorScreenPos(mn);
            ImGui::InvisibleButton("##label", ImVec2(mx.x - mn.x, rowH));
            if (ImGui::IsItemActivated())
               PickLane(n, lane);
            const bool hov = ImGui::IsItemHovered();
            const std::string name = LaneName(n, lane);
            DeskParts::ScribbleRow(dl, mn, mx, name.c_str(), lane + 1, NodeColor(gn, loaded ? 1.0f : 0.35f), !loaded);
            if (hov)
            {
               dl->AddRect(mn, mx, DeskParts::Ink(70), 3.0f);
               HelpTip("%s %d: %s\n%s", T("Lane"), lane + 1, name.c_str(), T("Click to play and select (Alt = accent). Keys 1-8 too."));
            }
            float x = scribbleX1 + pad;
            ImGui::SetCursorScreenPos(ImVec2(x, cy - keyH * 0.5f));
            if (DeskParts::Key("M##m", "M", n->laneMute[lane], tok::U32(tok::desk_led_red), ImVec2(keyW, keyH)))
            {
               n->laneMute[lane] = !n->laneMute[lane];
               UndoOnToggle(&n->laneMute[lane]);
            }
            x += keyW + pad;
            ImGui::SetCursorScreenPos(ImVec2(x, cy - keyH * 0.5f));
            if (DeskParts::Key("S##s", "S", n->laneSolo[lane], tok::U32(tok::desk_led_yellow), ImVec2(keyW, keyH)))
            {
               n->laneSolo[lane] = !n->laneSolo[lane];
               UndoOnToggle(&n->laneSolo[lane]);
            }
            x += keyW + pad;
            DeskParts::ConsoleMeter(x, y, meterW, rowH, LaneLit(n, lane));
            ImGui::PopID();
         }
      }

      // ---- step keys ---------------------------------------------------------------------------------
      struct GridGeom
      {
         float x0 = 0.0f, cellW = 0.0f;
         int cols = 0;
         float X(int c) const { return x0 + (float)c * (cellW + kCellGap) + (float)(c / 4) * kGroupGap; }
         int ColAt(float mx) const
         {
            for (int c = 0; c < cols; c++)
               if (mx < X(c) + cellW + kCellGap * 0.5f + ((c % 4) == 3 ? kGroupGap * 0.5f : 0.0f))
                  return c;
            return cols;
         }
      };

      void DrawGrid(GraphNode* gn, DrumSequencerNode* n, ImVec2 p, float w, float rowH)
      {
         (void)gn;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const int N = std::clamp(n->numSteps, 1, kMaxSteps);
         int maxLen = N;
         for (int l = 0; l < kLanes; l++)
            maxLen = std::max(maxLen, n->LaneStepCount(l));
         const bool paged = maxLen > kPageSteps;
         n->editPage = paged ? std::clamp(n->editPage, 0, 1) : 0;
         const int base = n->editPage * kPageSteps;
         GridGeom g;
         g.cols = paged ? kPageSteps : maxLen;
         g.x0 = p.x;
         const float gaps = kCellGap * (float)(g.cols - 1) + kGroupGap * (float)((g.cols - 1) / 4);
         g.cellW = std::max(6.0f, (w - gaps) / (float)g.cols);
         const int play = PlayStep(n);
         const float gridY = p.y + kRulerH;
         const float gridH = (float)kLanes * (rowH + kRowGap) - kRowGap;

         // Ruler: step numbers, beat starts brighter; the playing step gets an accent bar.
         for (int c = 0; c < g.cols; c++)
         {
            const int s = base + c;
            const float x0 = g.X(c);
            const bool inPat = s < maxLen, now = s == play, beat = s % 4 == 0;
            char num[4];
            snprintf(num, sizeof num, "%d", s + 1);
            const float rfs = tok::type_caption;
            const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(rfs, FLT_MAX, 0.0f, num);
            if (g.cellW >= ts.x + 2.0f || beat)
               dl->AddText(ImGui::GetFont(), rfs, ImVec2(x0 + (g.cellW - ts.x) * 0.5f, p.y + 2.0f), DeskParts::Ink((beat || now ? 215 : 120) * (inPat ? 255 : 70) / 255), num);
            // engraved tick under the number, longer on the beat (like the fader scale)
            dl->AddLine(ImVec2(x0 + g.cellW * 0.5f, p.y + 3.0f + rfs), ImVec2(x0 + g.cellW * 0.5f, p.y + 3.0f + rfs + (beat ? 4.0f : 2.0f)),
                        DeskParts::Ink(inPat ? (beat ? 200 : 110) : 50), 1.0f);
            // Chase light: one LED per step, the playing one lit (TR step LEDs).
            if (inPat)
            {
               const ImU32 chase = tok::U32(tok::desk_led_red);
               const ImVec2 lc(x0 + g.cellW * 0.5f, p.y + kRulerH - 4.0f);
               dl->AddCircleFilled(lc, 2.5f, DeskParts::Tint(chase, 0.22f), 10);
               if (now)
               {
                  dl->AddCircleFilled(lc, 4.5f, tok::U32(tok::desk_led_red, 60), 12);
                  dl->AddCircleFilled(lc, 2.5f, chase, 10);
               }
            }
         }

         // One hit target for the whole grid; the key under the mouse is worked out here.
         ImGui::SetCursorScreenPos(ImVec2(p.x, gridY));
         ImGui::InvisibleButton("##drumgrid", ImVec2(std::max(1.0f, w), gridH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
         const bool gridHovered = ImGui::IsItemHovered();
         const ImVec2 m = ImGui::GetIO().MousePos;
         auto cellAt = [&](int& lane, int& step) {
            const int c = g.ColAt(m.x);
            const int l = (int)std::floor((m.y - gridY) / (rowH + kRowGap));
            lane = std::clamp(l, 0, kLanes - 1);
            step = base + std::clamp(c, 0, g.cols - 1);
            return m.x >= p.x && c < g.cols && l >= 0 && l < kLanes && step < n->LaneStepCount(lane);
         };
         int hl = -1, hs = -1;
         const bool overCell = (gridHovered || sDrag.mode != Drag::None) && cellAt(hl, hs);
         const ImGuiIO& io = ImGui::GetIO();

         if (ImGui::IsItemActivated() && overCell && io.KeyShift && !ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            // Shift-click: open the key on the Step page (Elektron: hold TRIG), no edit.
            sLane = hl;
            sStep = hs;
            sTab = kTabStep;
         }
         else if (ImGui::IsItemActivated() && overCell)
         {
            PushUndoCheckpoint("Drums steps");
            float& v = n->stepVel[hl][hs];
            sDrag = { Drag::None, hl, hs, v, m.y };
            sLane = hl;
            sStep = hs;
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
            HelpTip("%s", T("Click: key on/off. Drag: paint. Drag up/down on a lit key: velocity.\nAlt-click: accent. Right-drag: erase. Shift-click: open on the Step page."));

         // Keys: raised caps on a recessed tray. A lit key glows in the accent, brightness = velocity;
         // a top line = accent; the bar under the cap shows nudge.
         const ImU32 badgeText = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.92f));
         for (int lane = 0; lane < kLanes; lane++)
         {
            const float y0 = gridY + (float)lane * (rowH + kRowGap);
            const int len = n->LaneStepCount(lane);
            const int lanePlay = PlayStep(n, lane);
            for (int c = 0; c < g.cols; c++)
            {
               const int s = base + c;
               const float x0 = g.X(c);
               const ImVec2 cMin(x0, y0), cMax(x0 + g.cellW, y0 + rowH);
               if (s >= len)
               {
                  dl->AddRectFilled(cMin, cMax, DeskParts::Lo(90), 3.0f);
                  continue;
               }
               const bool hov = overCell && hl == lane && hs == s;
               const bool now = s == lanePlay;
               const float vel = n->stepVel[lane][s];
               const bool pressed = hov && sDrag.mode != Drag::None;
               // Desk keys: a raised cap with an LED bar; the bar glows in the lane colour, brightness = velocity.
               if (vel > 0.0f)
               {
                  const bool accent = vel >= 0.99f;
                  const float prob = n->stepProb[lane][s];
                  const float lit = std::clamp((0.45f + 0.55f * vel) * (prob < 1.0f ? 0.5f + 0.5f * prob : 1.0f), 0.0f, 1.0f);
                  DeskParts::KeyCap(dl, cMin, cMax, hov, pressed, lit, kTrLed, nullptr, true, 0.0f, TrKey(s, 0.75f + 0.35f * lit));
                  if (now)
                  {
                     dl->AddRectFilled(cMin, cMax, DeskParts::Ink(90), 3.0f);
                     dl->AddRect(cMin, cMax, DeskParts::Ink(), 3.0f, 0, 1.5f);
                  }
                  const float nudge = n->stepNudge[lane][s];
                  if (std::fabs(nudge) > 0.001f)
                  {
                     const float cx = (x0 + cMax.x) * 0.5f + nudge * (g.cellW - 6.0f);
                     dl->AddRectFilled(ImVec2(cx - 2.0f, cMax.y - 4.5f), ImVec2(cx + 2.0f, cMax.y - 3.0f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9f)), 1.0f);
                  }
                  if (accent)
                     dl->AddRectFilled(ImVec2(x0 + 3.0f, cMax.y - 3.5f), ImVec2(cMax.x - 3.0f, cMax.y - 2.0f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9f)), 1.0f);
                  const int cond = n->stepCond[lane][s];
                  char badge[8] = "";
                  if (cond != 0)
                     snprintf(badge, sizeof badge, "%s", DrumSequencerNode::CondName(cond));
                  else if (prob < 1.0f)
                     snprintf(badge, sizeof badge, "%d%%", (int)std::lround(prob * 100.0f));
                  if (badge[0] != '\0')
                  {
                     UiType::Scope bs(UiType::Size::Caption, UiType::Weight::Semibold);
                     const ImVec2 bsz = ImGui::CalcTextSize(badge);
                     if (bsz.x + 4.0f <= g.cellW && bsz.y + 8.0f <= rowH)
                        dl->AddText(ImVec2(x0 + 3.0f, cMax.y - bsz.y - 3.0f), badgeText, badge);
                     else
                        dl->AddTriangleFilled(ImVec2(cMax.x - 6.0f, cMax.y - 1.0f), ImVec2(cMax.x - 1.0f, cMax.y - 1.0f),
                                              ImVec2(cMax.x - 1.0f, cMax.y - 6.0f), badgeText);
                  }
               }
               else
               {
                  DeskParts::KeyCap(dl, cMin, cMax, hov, pressed, 0.0f, kTrLed, nullptr, true, 0.0f, TrKey(s, 0.38f));
                  if (now)
                  {
                     dl->AddRectFilled(cMin, cMax, DeskParts::Ink(50), 3.0f);
                     dl->AddRect(cMin, cMax, DeskParts::Ink(200), 3.0f, 0, 1.5f);
                  }
               }
               if (sTab == kTabStep && lane == sLane && s == sStep)
                  dl->AddRect(ImVec2(cMin.x - 1.5f, cMin.y - 1.5f), ImVec2(cMax.x + 1.5f, cMax.y + 1.5f), ImGui::GetColorU32(ImGuiCol_Text), 2.0f, 0, 1.5f);
            }
         }
      }

      // ---- inspector ---------------------------------------------------------------------------------
      // Knobs in a row of `count` equal cells across the inspector, captions underneath.
      struct KnobRow
      {
         float x0, y, cell;
         int i = 0;
         KnobRow(int count)
         {
            x0 = ImGui::GetCursorScreenPos().x;
            y = ImGui::GetCursorScreenPos().y;
            cell = ImGui::GetContentRegionAvail().x / (float)count;
         }
         // A desk pot with its CAPS caption and a green readout plate under it. No undo step here.
         bool Raw(const char* label, float* v, float lo, float hi, const char* fmt, ImU32 col, bool bipolar = false)
         {
            const float cx = x0 + cell * (float)i++;
            ImGui::SetCursorScreenPos(ImVec2(cx, y));
            char tag[32], id[48], buf[32];
            snprintf(id, sizeof id, "##pot%s", label);
            size_t k = 0;
            for (; label[k] != '\0' && k + 1 < sizeof tag; k++)
               tag[k] = (char)toupper((unsigned char)label[k]);
            tag[k] = '\0';
            const bool ch = DeskParts::Pot(id, v, lo, hi, bipolar, cell, col, tag);
            snprintf(buf, sizeof buf, fmt, *v);
            const float pw = std::min(cell - 4.0f, 62.0f);
            DeskParts::Plate(ImGui::GetWindowDrawList(), ImVec2(cx + (cell - pw) * 0.5f, y + DeskParts::kPotRowH + 2.0f), pw, buf);
            return ch;
         }
         void Knob(const char* label, float* v, float lo, float hi, const char* fmt, ImU32 col, bool bipolar = false)
         {
            const float before = *v;
            Raw(label, v, lo, hi, fmt, col, bipolar);
            UndoOnActivate(v, before);
         }
         void End()
         {
            ImGui::SetCursorScreenPos(ImVec2(x0, y + DeskParts::kPotRowH + DeskParts::kPlateH + 2.0f + tok::space_1));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
         }
      };

      // Desk keys for the inspector: legend in CAPS, LED lit when `on`.
      bool DeskKey(const char* label, bool on, float minW, float h)
      {
         char legend[40];
         size_t k = 0;
         for (; label[k] != '\0' && !(label[k] == '#' && label[k + 1] == '#') && k + 1 < sizeof legend; k++)
            legend[k] = (char)toupper((unsigned char)label[k]);
         legend[k] = '\0';
         const float w = std::max(minW, ImGui::CalcTextSize(legend).x + 16.0f);
         return DeskParts::Key(label, legend, on, Accent(1.0f), ImVec2(w, h > 0.0f ? h : 24.0f));
      }

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
         // One undo step per handle drag: checkpoint the values from before its first move.
         static bool sTrimming = false;
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            sTrimming = false;
         if ((n->laneStart[lane] != s0 || n->laneEnd[lane] != e0) && !sTrimming)
         {
            sTrimming = true;
            const float s1 = n->laneStart[lane], e1 = n->laneEnd[lane];
            n->laneStart[lane] = s0;
            n->laneEnd[lane] = e0;
            PushUndoCheckpoint("Drums trim");
            n->laneStart[lane] = s1;
            n->laneEnd[lane] = e1;
         }
         const float bw = (w - 2.0f * tok::space_1) / 3.0f;
         if (DeskKey(T("Play"), false, bw, 0.0f))
            Hit(n, lane, kDefaultVel);
         ImGui::SameLine(0.0f, tok::space_1);
         if (DeskKey(T("Load"), false, bw, 0.0f))
         {
            const std::string path = Platform::OpenAudioDialog();
            if (!path.empty())
            {
               PushUndoCheckpoint("Drums load sample");
               n->LoadFileToLane(lane, path);
            }
         }
         ImGui::SameLine(0.0f, tok::space_1);
         if (DeskKey(T("Clear"), false, bw, 0.0f))
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

         SectionCard::Begin(T("Shape"));
         {
            KnobRow r(3);
            float len = (float)n->laneSteps[lane];
            const int before = n->laneSteps[lane];
            r.Raw("length", &len, 0.0f, (float)kMaxSteps, n->laneSteps[lane] == 0 ? "pattern" : "%.0f", col);
            n->laneSteps[lane] = std::clamp((int)std::lround(len), 0, kMaxSteps);
            UndoOnActivate(&n->laneSteps[lane], before);
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Lane length. 0 follows the pattern; other lengths loop on their own (polymeter)"));
            const bool filt = n->laneFilter[lane] != 0;
            if (!filt)
               ImGui::BeginDisabled();
            r.Knob("cutoff", &n->laneCutoff[lane], 0.0f, 1.0f, "%.2f", col);
            r.Knob("reso", &n->laneReso[lane], 0.0f, 1.0f, "%.2f", col);
            if (!filt)
               ImGui::EndDisabled();
            r.End();
         }
         SectionCard::RowLabel(T("Filter"));
         static const char* kFilt[3] = { "off##f0", "LP##f1", "HP##f2" };
         for (int f = 0; f < 3; f++)
         {
            if (f > 0)
               ImGui::SameLine(0.0f, 1.0f);
            if (DeskKey(kFilt[f], n->laneFilter[lane] == f, 34.0f, tok::control_h) && n->laneFilter[lane] != f)
            {
               PushUndoCheckpoint("Drums filter");
               n->laneFilter[lane] = f;
            }
         }
         SectionCard::RowLabel(T("Play"));
         if (DeskKey(T("Reverse"), n->laneReverse[lane], 70.0f, tok::control_h))
         {
            PushUndoCheckpoint("Drums reverse");
            n->laneReverse[lane] = !n->laneReverse[lane];
         }

         SectionCard::Begin(T("Lane"));
         SectionCard::RowLabel(T("Choke"));
         static const char* kChoke[4] = { "off##c0", "1##c1", "2##c2", "3##c3" };
         for (int c = 0; c < 4; c++)
         {
            if (c > 0)
               ImGui::SameLine(0.0f, 1.0f);
            if (DeskKey(kChoke[c], n->laneChoke[lane] == c, 30.0f, tok::control_h) && n->laneChoke[lane] != c)
            {
               PushUndoCheckpoint("Drums choke");
               n->laneChoke[lane] = c;
            }
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Lanes in one choke group cut each other off (closed hat stops the open hat)"));
         SectionCard::RowLabel(T("Mix"));
         if (DeskParts::Key("M##im", "M", n->laneMute[lane], tok::U32(tok::desk_led_red), ImVec2(tok::control_h, tok::control_h)) && (n->laneMute[lane] = !n->laneMute[lane], true))
            UndoOnToggle(&n->laneMute[lane]);
         ImGui::SameLine(0.0f, 1.0f);
         if (DeskParts::Key("S##is", "S", n->laneSolo[lane], tok::U32(tok::desk_led_yellow), ImVec2(tok::control_h, tok::control_h)) && (n->laneSolo[lane] = !n->laneSolo[lane], true))
            UndoOnToggle(&n->laneSolo[lane]);
         ImGui::PopID();
      }

      // The groove library by category, then My grooves (the .mid folder).
      std::vector<std::string>& GrooveFiles()
      {
         static std::vector<std::string> sFiles;
         return sFiles;
      }

      // The groove library menu, opened by the header face or the Kit page's picker.
      void DrawGrooveMenu(DrumSequencerNode* n)
      {
         if (!MenuParts::BeginPopup("##drumGroove"))
            return;
         const std::vector<std::string>& sFiles = GrooveFiles();
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

      void DrawGroovePicker(DrumSequencerNode* n, float w)
      {
         const char* face = n->patternName.empty() ? T("Pick a groove") : n->patternName.c_str();
         if (DeskKey((std::string(face) + "##groove").c_str(), false, w, tok::control_h))
            ImGui::OpenPopup("##drumGroove");
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Replaces the pattern, rate, swing and steps; fills empty lanes from the kit"));
         if (ImGui::IsItemActivated())
            GrooveFiles() = ScanDrumGrooveFiles();
         DrawGrooveMenu(n);
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
            if (DeskKey(kParts[p], current >= 0 && n->patternPart == p, 30.0f, tok::control_h) && current >= 0)
               ApplyDrumGroove(n, current, p);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               HelpTip("%s", DrumPatterns::PartName(p));
         }
         if (current < 0)
            ImGui::EndDisabled();

         SectionCard::Begin(T("Pattern"));
         SectionCard::RowLabel(T("Rate"));
         if (DeskKey((std::string(MusicTime::RateDivisionName(n->rate)) + "##rate").c_str(), false, -1.0f, tok::control_h))
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
            float steps = (float)n->numSteps;
            const int before = n->numSteps;
            r.Raw("steps", &steps, 1.0f, (float)kMaxSteps, "%.0f", col);
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
         if (DeskKey(T("Randomise"), false, bw, 0.0f))
         {
            PushUndoCheckpoint("Drums randomise");
            n->Randomize();
         }
         ImGui::SameLine(0.0f, tok::space_1);
         if (DeskKey(T("Clear"), false, bw, 0.0f))
         {
            PushUndoCheckpoint("Drums clear");
            n->ClearPattern();
         }
      }

      // The selected key's trig: velocity, chance, micro timing, condition (Elektron trig page).
      void DrawStepInspector(GraphNode* gn, DrumSequencerNode* n)
      {
         const int lane = std::clamp(sLane, 0, kLanes - 1);
         const int s = std::clamp(sStep, 0, kMaxSteps - 1);
         const ImU32 col = NodeColor(gn);
         ImGui::PushID(lane * kMaxSteps + s);
         char title[64];
         snprintf(title, sizeof title, "%s %d  %s %d", T("Step"), s + 1, T("lane"), lane + 1);
         SectionCard::Begin(title);
         const float w = ImGui::GetContentRegionAvail().x;
         float& vel = n->stepVel[lane][s];
         if (vel <= 0.0f)
         {
            if (DeskKey(T("Turn on"), false, w, tok::control_h))
            {
               PushUndoCheckpoint("Drums steps");
               vel = kDefaultVel;
            }
            ImGui::PopID();
            return;
         }
         {
            KnobRow r(3);
            r.Knob("velocity", &vel, 0.05f, 1.0f, "%.2f", col);
            float pct = n->stepProb[lane][s] * 100.0f;
            const float before = n->stepProb[lane][s];
            r.Raw("chance", &pct, 0.0f, 100.0f, "%.0f%%", col);
            n->stepProb[lane][s] = std::clamp(std::round(pct) / 100.0f, 0.0f, 1.0f);
            UndoOnActivate(&n->stepProb[lane][s], before);
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Chance the hit plays each time round"));
            r.Knob("nudge", &n->stepNudge[lane][s], -0.48f, 0.48f, "%+.2f", col, true);
            r.End();
         }
         SectionCard::Begin(T("Condition"));
         const float cw = (w - 4.0f * 1.0f) / 5.0f;
         for (int c = 0; c < DrumSequencerNode::kNumConds; c++)
         {
            // Rows: none + Fill pair + 1st pair | 1:2 2:2 1:3 2:3 3:3 | 1:4 .. 4:4
            const bool rowStart = c == 0 || c == 5 || c == 10;
            if (!rowStart)
               ImGui::SameLine(0.0f, 1.0f);
            char id[24];
            snprintf(id, sizeof id, "%s##cd%d", DrumSequencerNode::CondName(c), c);
            if (DeskKey(id, n->stepCond[lane][s] == c, cw, tok::control_h) && n->stepCond[lane][s] != c)
            {
               PushUndoCheckpoint("Drums condition");
               n->stepCond[lane][s] = c;
            }
         }
         if (DeskKey(T("Reset trig"), false, w, 0.0f))
         {
            PushUndoCheckpoint("Drums reset trig");
            n->stepProb[lane][s] = 1.0f;
            n->stepCond[lane][s] = 0;
            n->stepNudge[lane][s] = 0.0f;
         }
         ImGui::PopID();
      }

      void DrawInspector(GraphNode* gn, DrumSequencerNode* n, ImVec2 at, ImVec2 size)
      {
         ImGui::SetCursorScreenPos(at);
         ImDrawList* pdl = ImGui::GetWindowDrawList();
         DeskParts::Chassis(pdl, ImVec2(at.x - 4.0f, at.y - 4.0f), ImVec2(at.x + size.x + 4.0f, at.y + size.y));
         ImGui::BeginChild("##drumInspector", size, ImGuiChildFlags_AlwaysUseWindowPadding, 0);
         UiDensity::Compact compact;
         // Text and cards inside the desk use the desk tones (light desk in light mode).
         ImGui::PushStyleColor(ImGuiCol_Text, DeskParts::Ink(228));
         ImGui::PushStyleColor(ImGuiCol_TextDisabled, DeskParts::Ink(140));
         SectionCard::S().desk = true;
         SectionCard::S().deskCap = NodeColor(gn);
         SectionCard::BeginWindow();
         if (sTab == kTabKit)
            DrawKitInspector(gn, n);
         else if (sTab == kTabStep)
            DrawStepInspector(gn, n);
         else
            DrawLaneInspector(gn, n);
         SectionCard::EndWindow();
         SectionCard::S().desk = false;
         ImGui::PopStyleColor(2);
         compact.End(); // before the child closes: the pushes were made inside it
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
         ApplyPendingPageEdits();
         FollowCanvasSelection();
         const std::vector<GraphNode*> pages = CollectPages();
         const int current = CurrentPage(pages);
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 avail = ImGui::GetContentRegionAvail();
         DrawHeader(pages, current, origin, avail.x);

         const ImVec2 bodyMin(origin.x, origin.y + tok::control_h + tok::space_2);
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
            const float dt = ImGui::GetIO().DeltaTime;
            for (int l = 0; l < kLanes; l++)
               sPadFlash[l] = std::max(0.0f, sPadFlash[l] - dt * 6.0f);
            const bool inspector = avail.x >= kLabelW + kInspectorW + 240.0f;
            const float inspW = inspector ? kInspectorW : 0.0f;
            const float leftX1 = inspector ? bodyMax.x - inspW - tok::space_2 : bodyMax.x;
            const ImVec2 seqMin = bodyMin;
            const float seqH = bodyMax.y - seqMin.y;
            const float gridX0 = seqMin.x + kLabelW + tok::space_2;
            // Rows follow the panel height the grip sets.
            const float rowH = std::clamp((seqH - kRulerH - (float)(kLanes - 1) * kRowGap) / (float)kLanes, 14.0f, kRowH * 3.0f);
            // What the panel needs around the grid, measured, so the layout can stop the panel at the grid's foot.
            gDrumPanelNaturalH = (gDrumPanelHeight - seqH) + kRulerH + (float)kLanes * kRowH + (float)(kLanes - 1) * kRowGap + 6.0f;
            DeskParts::Chassis(ImGui::GetWindowDrawList(), ImVec2(seqMin.x - 4.0f, seqMin.y - 4.0f), ImVec2(leftX1 + 4.0f, bodyMax.y));
            DeskParts::Screw(ImGui::GetWindowDrawList(), ImVec2(seqMin.x + 1.0f, seqMin.y + 1.0f));
            DeskParts::Screw(ImGui::GetWindowDrawList(), ImVec2(leftX1 - 1.0f, seqMin.y + 1.0f));
            DrawLabels(gn, n, seqMin, rowH, leftX1);
            DrawGrid(gn, n, ImVec2(gridX0, seqMin.y), leftX1 - gridX0, rowH);
            if (inspector)
               DrawInspector(gn, n, ImVec2(bodyMax.x - inspW, bodyMin.y), ImVec2(inspW, bodyH));
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
            gDrumPanelHeight = std::max(160.0f, gDrumPanelHeight + (gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y));
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            gDrumPanelHeight = gDrumPanelNaturalH; // double-click: fit the lanes
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
      PushDockedPanelStyle(/*isChild=*/true);
      PanelFrame::BeginCard("##drumpanelcontent", cardSize, in);
      PopDockedPanelStyle();
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
