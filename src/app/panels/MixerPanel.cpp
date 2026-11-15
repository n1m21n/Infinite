// Mixer panel: the console view of the patch's Mixer nodes (internal/plans/mixer-panel).
// A3 is the shell (five docks, persist, the "Enable Mixer Audio" chip). A4 is the console: page
// tabs, strips (name, trim, pan, fader | meter, M S Ã), the master strip, group strips that fold
// open, canvas-selection follow, and the empty state.
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/UiType.h"
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "audio/AudioEngine.h"
#include "core/CategoryColors.h"

#include <set>
#include <unordered_map>
#include <string>
#include <vector>
#include "nodes/AudioNodes.h"
#include "nodes/AudioEffectNode.h"
#include "core/Notices.h"
#include <algorithm>

namespace app
{
   bool  gMixerPanelOpen = false;
   int   gMixerPanelDock = 0;   // 0 bottom, 1 right, 2 left, 3 top, kDockFloating
   float gMixerPanelWidth = 520.0f;
   float gMixerPanelHeight = 300.0f;
   bool  gMixerClaimedKeys = false;
   ImVec2 gMixerPanelRectMin, gMixerPanelRectMax;

   // Mixer Audio: only the console reaches the device, the way Session audio takes it from the canvas.
   void SetMixerAudio(bool on)
   {
      if (on == (gAudioMode == AudioMode::Mixer))
         return;
      gAudioMode = on ? AudioMode::Mixer : AudioMode::Canvas;
      if (on && AudioEngine::Instance().SampleRate() <= 0.0)
      {
         gAudioStartError.clear();
         if (!StartAudioEngine(gAudioStartError))
            fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
      }
      RebuildAudioTopology();
   }

   int gMixerPanelStripCount = 0;
   float gMixerPanelFaderH = 0.0f;
   ImVec2 gMixerStripsMin, gMixerStripsMax, gMixerMasterMin, gMixerMasterMax;

   bool IsMixerPageTerminal(const INode* terminal, const INode* resolvedSource)
   {
      return dynamic_cast<const AudioOutputNode*>(terminal) != nullptr &&
             dynamic_cast<const MixerNode*>(resolvedSource) != nullptr;
   }

   namespace
   {
      // Strip geometry. The fader takes whatever height is left after the fixed rows.
      constexpr float kStripW = 58.0f;
      constexpr float kStripGap = 4.0f;
      constexpr float kNameH = 22.0f;
      constexpr float kKnobD = 24.0f;
      constexpr float kButtonsH = 20.0f;
      constexpr float kRowGap = 4.0f;
      constexpr float kStripMaxH = 360.0f;
      constexpr float kMinFaderH = 40.0f;
      constexpr int kMaxGroupDepth = 4;
      constexpr float kSendH = 16.0f;     // §4.2 sends row: four mini knobs between name and trim
      constexpr float kSendKnobD = 13.0f;
      constexpr float kReturnGap = 14.0f; // returns sit in their own section right of the strips
      constexpr int kInsertRows = 3;      // §4.2 inserts: chips, the last row holds "+"
      constexpr float kInsertRowH = 14.0f;
      constexpr float kInsertsH = kInsertRows * kInsertRowH + (kInsertRows - 1) * 1.0f;

      struct Page { int outIndex; int topIndex; uint64_t topUid; };

      struct StripRef
      {
         int mixerIndex = 0;  // the Mixer this slot belongs to
         int slot = 0;
         int srcIndex = -1;   // node on the cable, -1 when empty
         int groupIndex = -1; // a nested Mixer behind the cable (D3), -1 when not a group
         int depth = 0;
         bool isReturn = false; // a top strip fed by some Mixer's send bus (G1)
         bool sends = false;    // shows the sends row
         int headIndex = -1;    // the true source above the inserts, -1 when off canvas or none
         std::vector<int> inserts; // D4: single-input effects between the source and the slot, signal order
         bool shared = false;   // G4: the walk hit fan-out or a 2+ input node; not editable inline
      };

      // D4: the insert chain of one Mixer slot, walked up from its cable.
      struct Chain
      {
         std::vector<int> fx;    // node indices, signal order
         std::vector<int> fxOut; // the output slot each fx feeds downstream with
         INode* head = nullptr;  // the source above the chain, nullptr when the top fx is unplugged
         int headOut = 0;
         bool shared = false;
      };

      using Consumers = std::unordered_map<const INode*, int>;

      Consumers CountConsumers()
      {
         Consumers uses;
         for (GraphNode& gn : gNodes)
            for (int slot = 0; gn.node && slot < kMaxAudioSlots; slot++)
               if (AudioCable* a = gn.node->AudioInputSlot(slot); a != nullptr && a->IsConnected())
                  uses[a->GetSource()]++;
         return uses;
      }

      uint64_t sPageUid = 0;            // top Mixer of the page shown
      std::set<uint64_t> sOpenGroups;   // folded-open group Mixers
      uint64_t sHighlightUid = 0;       // canvas selection, shown on its strip
      uint64_t sLastSelectedUid = 0;
      // B3: strips picked with Cmd/Shift-click on the name, as (Mixer uid, slot). Cleared by any regroup.
      std::set<std::pair<uint64_t, int>> sSelStrips;
      bool sStripsStale = false; // a group/ungroup/move ran mid-draw: the frame's strip list is out of date

      // A return: the strip's source, followed up through single-input nodes, starts at a send pin.
      bool IsReturnSource(INode* n)
      {
         for (int hop = 0; n != nullptr && hop < 4; hop++)
         {
            AudioCable* in = n->AudioInputSlot(0);
            if (in == nullptr || !in->IsConnected())
               return false;
            if (dynamic_cast<MixerNode*>(in->GetSource()) != nullptr)
               return in->GetOutputSlot() >= 1;
            n = in->GetSource();
         }
         return false;
      }

      GraphNode* GraphNodeOf(const INode* n)
      {
         if (n == nullptr)
            return nullptr;
         for (GraphNode& gn : gNodes)
            if (gn.node.get() == n)
               return &gn;
         return nullptr;
      }

      // An insert is a non-Mixer node fed on input 0 only, feeding nothing else. The first node that
      // is not one is the head; fan-out or a second live input stops the walk as shared (G4).
      Chain WalkInserts(const AudioCable& c, const Consumers& uses)
      {
         Chain ch;
         if (!c.IsConnected())
            return ch;
         INode* n = c.GetSource();
         int out = c.GetOutputSlot();
         for (int hop = 0; n != nullptr && hop < 64; hop++)
         {
            AudioCable* in0 = dynamic_cast<MixerNode*>(n) != nullptr ? nullptr : n->AudioInputSlot(0);
            if (in0 == nullptr || !in0->IsConnected())
            {
               ch.head = n;
               ch.headOut = out;
               break;
            }
            bool other = false;
            for (int s = 1; s < kMaxAudioSlots && !other; s++)
               if (AudioCable* a = n->AudioInputSlot(s); a != nullptr && a->IsConnected())
                  other = true;
            const auto it = uses.find(n);
            GraphNode* gn = GraphNodeOf(n);
            if (other || (it != uses.end() && it->second > 1) || gn == nullptr)
            {
               ch.shared = true;
               ch.fx.clear();
               ch.fxOut.clear();
               return ch;
            }
            ch.fx.push_back(gn->index);
            ch.fxOut.push_back(out);
            out = in0->GetOutputSlot();
            n = in0->GetSource();
         }
         std::reverse(ch.fx.begin(), ch.fx.end());
         std::reverse(ch.fxOut.begin(), ch.fxOut.end());
         return ch;
      }

      MixerNode* MixerAt(int index)
      {
         GraphNode* gn = FindNodeByIndex(index);
         return gn ? dynamic_cast<MixerNode*>(gn->node.get()) : nullptr;
      }

      int CountMixers()
      {
         int n = 0;
         for (const auto& gn : gNodes)
            if (dynamic_cast<MixerNode*>(gn.node.get()) != nullptr)
               n++;
         return n;
      }

      std::vector<Page> CollectPages()
      {
         std::vector<Page> pages;
         for (GraphNode& gn : gNodes)
         {
            AudioCable* in = gn.node ? gn.node->AudioInputSlot(0) : nullptr;
            if (in == nullptr || !in->IsConnected())
               continue;
            INode* resolved = ResolvedAudioSource(in->GetSource());
            if (!IsMixerPageTerminal(gn.node.get(), resolved))
               continue;
            GraphNode* top = GraphNodeOf(resolved);
            if (top == nullptr)
               continue;
            bool dup = false;
            for (const Page& p : pages)
               dup = dup || p.topIndex == top->index;
            if (!dup)
               pages.push_back({ gn.index, top->index, top->uid });
         }
         return pages;
      }

      int CurrentPage(const std::vector<Page>& pages)
      {
         for (int i = 0; i < (int)pages.size(); i++)
            if (pages[i].topUid == sPageUid)
               return i;
         if (!pages.empty())
            sPageUid = pages[0].topUid;
         return pages.empty() ? -1 : 0;
      }

      // The console as a flat row: each slot of the top Mixer, and an open group's slots right after it.
      // allOpen walks every group (selection follow), ignoring the folds.
      void Flatten(int mixerIndex, int depth, bool allOpen, const Consumers& uses, std::vector<StripRef>& out, std::set<int>& seen)
      {
         MixerNode* m = MixerAt(mixerIndex);
         if (m == nullptr || !seen.insert(mixerIndex).second)
            return;
         for (int slot = 0; slot < m->numChannels; slot++)
         {
            StripRef r;
            r.mixerIndex = mixerIndex;
            r.slot = slot;
            r.depth = depth;
            const AudioCable& c = m->inputs[slot];
            if (c.IsConnected())
            {
               if (GraphNode* src = GraphNodeOf(c.GetSource()))
                  r.srcIndex = src->index;
               const Chain ch = WalkInserts(c, uses);
               r.inserts = ch.fx;
               r.shared = ch.shared;
               if (GraphNode* h = GraphNodeOf(ch.head))
                  r.headIndex = h->index;
               // A group is a Mixer's main out above the inserts; a Mixer's send pin there is a return.
               INode* head = ch.shared ? ResolvedAudioSource(c.GetSource()) : ch.head;
               const bool headMixer = dynamic_cast<MixerNode*>(head) != nullptr;
               if (headMixer && (ch.shared || ch.headOut == 0))
                  if (GraphNode* g = GraphNodeOf(head))
                     r.groupIndex = g->index;
               if (depth == 0 && r.groupIndex < 0)
                  r.isReturn = ch.shared ? IsReturnSource(c.GetSource()) : (headMixer && ch.headOut >= 1);
            }
            out.push_back(r);
            if (r.groupIndex >= 0 && depth < kMaxGroupDepth)
            {
               GraphNode* g = FindNodeByIndex(r.groupIndex);
               if (allOpen || (g && sOpenGroups.count(g->uid)))
                  Flatten(r.groupIndex, depth + 1, allOpen, uses, out, seen);
            }
         }
      }

      std::vector<StripRef> Strips(int topIndex, bool allOpen)
      {
         std::vector<StripRef> out;
         std::set<int> seen;
         Flatten(topIndex, 0, allOpen, CountConsumers(), out, seen);
         // G1: returns to the right; sends on every strip below the top. A console with no
         // returns yet is unwrapped, so its direct strips carry sends too (the first send wraps it).
         std::stable_partition(out.begin(), out.end(), [](const StripRef& r) { return !r.isReturn; });
         const bool wrapped = std::any_of(out.begin(), out.end(), [](const StripRef& r) { return r.isReturn; });
         for (StripRef& r : out)
            r.sends = r.depth > 0 || (!wrapped && r.groupIndex < 0 && !r.isReturn);
         return out;
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

      void SelectOnCanvas(int nodeIndex)
      {
         GraphNode* gn = FindNodeByIndex(nodeIndex);
         if (gn == nullptr || gEditor == nullptr)
            return;
         ed::EditorContext* prev = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);
         ed::SelectNode(gn->NodeId(), false);
         ed::SetCurrentEditor(prev);
         sLastSelectedUid = sHighlightUid = gn->uid;
      }

      // Brings up the page that holds g (a top Mixer, a group, or a strip's source) and opens its groups.
      // Returns false when g is on no page.
      bool ShowNode(const std::vector<Page>& pages, GraphNode* g)
      {
         sHighlightUid = g ? g->uid : 0;
         if (g == nullptr)
            return false;
         for (const Page& p : pages)
         {
            const std::vector<StripRef> all = Strips(p.topIndex, true);
            bool found = p.topIndex == g->index;
            int owner = -1;
            for (const StripRef& r : all)
               if (r.srcIndex == g->index || r.groupIndex == g->index || r.headIndex == g->index ||
                   std::find(r.inserts.begin(), r.inserts.end(), g->index) != r.inserts.end())
               {
                  found = true;
                  owner = r.mixerIndex;
                  break;
               }
            if (!found)
               continue;
            sPageUid = p.topUid;
            if (dynamic_cast<MixerNode*>(g->node.get()) != nullptr && g->index != p.topIndex)
               sOpenGroups.insert(g->uid); // a group itself opens to show its strips
            // Open every group on the way down to the owner.
            for (int guard = 0; owner >= 0 && owner != p.topIndex && guard < kMaxGroupDepth + 1; guard++)
            {
               GraphNode* og = FindNodeByIndex(owner);
               if (og == nullptr)
                  break;
               sOpenGroups.insert(og->uid);
               int parent = -1;
               for (const StripRef& r : all)
                  if (r.groupIndex == owner)
                     parent = r.mixerIndex;
               owner = parent;
            }
            return true;
         }
         return false;
      }

      // D2: selecting a Mixer or a strip's source on the canvas brings its page up and opens its groups.
      void FollowCanvasSelection(const std::vector<Page>& pages)
      {
         const uint64_t sel = SelectedNodeUid();
         if (sel == sLastSelectedUid)
            return;
         sLastSelectedUid = sel;
         ShowNode(pages, sel ? FindNodeByUid(sel) : nullptr);
      }

      // One undo step per drag or toggle: the checkpoint has to hold the value from before the change.
      void UndoOnActivate(float* v, float before)
      {
         if (!ImGui::IsItemActivated())
            return;
         const float now = *v;
         *v = before;
         PushUndoCheckpoint("Mixer strip");
         *v = now;
      }
      void UndoOnToggle(bool* v)
      {
         *v = !*v;
         PushUndoCheckpoint("Mixer strip");
         *v = !*v;
      }

      ImU32 CategoryColorOf(const GraphNode* gn)
      {
         if (gn == nullptr)
            return ImGui::GetColorU32(ImGuiCol_Border);
         const CategoryColors::Color& c = CategoryColors::ColorFor(gn->category);
         return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, 1.0f));
      }

      float FaderHeightFor(float stripH) { return std::max(kMinFaderH, stripH - kNameH - kInsertsH - kSendH - kKnobD - kButtonsH - 5.0f * kRowGap); }

      // B3 on a strip's name: Cmd/Shift-click picks it, drag reorders within its Mixer, right-click groups.
      // Returns true when the click was a pick (so it does not also select the node on the canvas).
      bool StripNameActions(ImDrawList* dl, ImVec2 p, float w, const char* name, const StripRef& r, bool clicked)
      {
         GraphNode* mGn = FindNodeByIndex(r.mixerIndex);
         if (mGn == nullptr)
            return false;
         const std::pair<uint64_t, int> key(mGn->uid, r.slot);
         const ImGuiIO& io = ImGui::GetIO();
         bool picked = false;
         if (clicked && (io.KeyShift || io.KeyCtrl || io.KeySuper))
         {
            if (!sSelStrips.erase(key))
               sSelStrips.insert(key);
            picked = true;
         }
         else if (clicked)
            sSelStrips.clear();
         if (sSelStrips.count(key))
            dl->AddRectFilled(ImVec2(p.x, p.y + 3.0f), ImVec2(p.x + w, p.y + kNameH), ImGui::GetColorU32(ImGuiCol_Header), 2.0f);
         if (ImGui::BeginDragDropSource())
         {
            const int payload[2] = { r.mixerIndex, r.slot };
            ImGui::SetDragDropPayload("MIXER_STRIP", payload, sizeof(payload));
            ImGui::TextUnformatted(name);
            ImGui::EndDragDropSource();
         }
         if (ImGui::BeginDragDropTarget())
         {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("MIXER_STRIP"))
            {
               const int* d = static_cast<const int*>(pl->Data);
               if (d[0] == r.mixerIndex && d[1] != r.slot)
                  MixerPanelStripMove(r.mixerIndex, d[1], r.slot);
            }
            ImGui::EndDragDropTarget();
         }
         if (MenuParts::BeginContextItem("##stripctx"))
         {
            const MixerNode* m = MixerAt(r.mixerIndex);
            const int n = m ? m->numChannels : 0;
            if (MenuParts::Item(T("Group")))
            {
               std::vector<int> slots { r.slot };
               for (const auto& k : sSelStrips)
                  if (k.first == mGn->uid)
                     slots.push_back(k.second);
               MixerPanelGroupStrips(r.mixerIndex, slots);
            }
            if (MenuParts::Item(T("Ungroup"), nullptr, false, r.groupIndex >= 0))
               MixerPanelUngroup(r.mixerIndex, r.slot);
            MenuParts::Separator();
            if (MenuParts::Item(T("Move left"), nullptr, false, r.slot > 0))
               MixerPanelStripMove(r.mixerIndex, r.slot, r.slot - 1);
            if (MenuParts::Item(T("Move right"), nullptr, false, r.slot < n - 1))
               MixerPanelStripMove(r.mixerIndex, r.slot, r.slot + 1);
            MenuParts::EndPopup();
         }
         return picked;
      }

      // A clipped one-line name; returns true on click.
      bool NameRow(ImDrawList* dl, ImVec2 p, float w, const char* name, ImU32 bar, bool dim, const StripRef* strip = nullptr)
      {
         dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 3.0f), bar, 1.5f);
         ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 3.0f));
         bool clicked = ImGui::InvisibleButton("##name", ImVec2(w, kNameH - 3.0f));
         const bool hovered = ImGui::IsItemHovered();
         if (strip != nullptr && StripNameActions(dl, p, w, name, *strip, clicked))
            clicked = false;
         const ImVec2 ts = ImGui::CalcTextSize(name);
         const float tx = p.x + std::max(2.0f, (w - ts.x) * 0.5f);
         dl->PushClipRect(ImVec2(p.x + 2.0f, p.y), ImVec2(p.x + w - 2.0f, p.y + kNameH), true);
         dl->AddText(ImVec2(tx, p.y + 5.0f), ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text), name);
         dl->PopClipRect();
         if (hovered && ts.x > w - 4.0f && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            HelpTip("%s", name);
         return clicked;
      }

      // Fader with its meter on the right; double-click returns to 0 dB.
      void FaderWithMeter(ImVec2 p, float w, float h, float* db, float level)
      {
         const float meterW = kFaderMeterW;
         const float faderCell = w - meterW - 4.0f;
         ImGui::SetCursorScreenPos(p);
         const float before = *db;
         VFaderFloat("##fader", db, -60.0f, 12.0f, "%.1f dB", h, ImGui::GetColorU32(ImGuiCol_SliderGrab), false,
                     faderCell, ConsoleFaderTaper::PosToValue, ConsoleFaderTaper::ValueToPos);
         UndoOnActivate(db, before);
         if (ImGui::IsItemHovered())
         {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               PushUndoCheckpoint("Mixer strip");
               *db = 0.0f;
            }
            if (*db <= -60.0f)
               HelpTip("-inf dB");
            else
               HelpTip("%+.1f dB", *db);
         }
         DrawStripMeter(p.x + faderCell + 2.0f, p.y, meterW, h, level);
      }

      // §4.2: one mini knob per send bus of this strip's Mixer (right-click: pre/post), then "+" for a new send FX.
      void DrawSends(ImVec2 p, MixerNode* m, const StripRef& r)
      {
         const int s = r.slot;
         const float cell = (kStripW - 2.0f) / (float)MixerNode::kSendBuses;
         const int buses = m->SendBuses();
         const ImU32 post = ImGui::GetColorU32(ImGuiCol_SliderGrab);
         const ImU32 pre = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
         for (int b = 0; b < buses; b++)
         {
            ImGui::PushID(b);
            ImGui::SetCursorScreenPos(ImVec2(p.x + 1.0f + cell * (float)b, p.y));
            const float before = m->sendDb[b][s];
            KnobFloat("##send", &m->sendDb[b][s], MixerNode::kSendOffDb, 12.0f, "%.1f dB", kSendKnobD,
                      m->sendPre[s] ? pre : post, false, cell);
            UndoOnActivate(&m->sendDb[b][s], before);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
               PushUndoCheckpoint("Mixer strip");
               m->sendPre[s] = !m->sendPre[s];
            }
            if (ImGui::IsItemHovered())
            {
               if (m->sendDb[b][s] <= MixerNode::kSendOffDb)
                  HelpTip("%s %d  %s  %s", T("Send"), b + 1, T("off"), m->sendPre[s] ? T("pre") : T("post"));
               else
                  HelpTip("%s %d  %+.1f dB  %s", T("Send"), b + 1, m->sendDb[b][s], m->sendPre[s] ? T("pre") : T("post"));
            }
            ImGui::PopID();
         }
         if (buses >= MixerNode::kSendBuses)
            return;
         ImGui::SetCursorScreenPos(ImVec2(p.x + 1.0f + cell * (float)buses, p.y));
         if (ActionButton::Draw("+##newsend", ImVec2(cell - 1.0f, kSendH)))
            ImGui::OpenPopup("##newsendfx");
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("New send FX"));
         if (MenuParts::BeginPopup("##newsendfx"))
         {
            static const char* kFx[] = { "Reverb", "Delay", "Chorus", "Flanger", "Phaser" };
            for (const char* fx : kFx)
               if (MenuParts::Item(L(fx)))
                  MixerPanelNewSendFx(r.mixerIndex, s, fx);
            MenuParts::EndPopup();
         }
      }

      // Bypass, move and remove for chip i; true when the graph changed.
      bool InsertMenuItems(const StripRef& r, int i)
      {
         const int n = (int)r.inserts.size();
         GraphNode* g = FindNodeByIndex(r.inserts[i]);
         if (g == nullptr)
            return false;
         if (MenuParts::Item(g->node->bypassed ? T("Enable") : T("Bypass")))
            return MixerPanelInsertBypass(r.mixerIndex, r.slot, i);
         if (MenuParts::Item(T("Move up"), nullptr, false, i > 0))
            return MixerPanelInsertMove(r.mixerIndex, r.slot, i, i - 1);
         if (MenuParts::Item(T("Move down"), nullptr, false, i < n - 1))
            return MixerPanelInsertMove(r.mixerIndex, r.slot, i, i + 1);
         MenuParts::Separator();
         if (MenuParts::Item(T("Remove")))
            return MixerPanelInsertRemove(r.mixerIndex, r.slot, i);
         return false;
      }

      // One insert chip: bypass dot, name; click selects, drag reorders, right-click for the rest.
      // Returns true when the graph changed, so the caller stops drawing this strip's stale chain.
      bool DrawInsertChip(ImDrawList* dl, ImVec2 p, float w, const StripRef& r, int i)
      {
         GraphNode* g = FindNodeByIndex(r.inserts[i]);
         if (g == nullptr)
            return false;
         bool changed = false;
         ImGui::PushID(i);
         const bool off = g->node->bypassed;
         const ImVec2 q(p.x + w, p.y + kInsertRowH);
         const bool lit = sHighlightUid != 0 && g->uid == sHighlightUid;
         dl->AddRectFilled(p, q, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
         if (lit)
            dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), 3.0f);
         const ImVec2 dot(p.x + 6.0f, p.y + kInsertRowH * 0.5f);
         if (off)
            dl->AddCircle(dot, 2.5f, ImGui::GetColorU32(ImGuiCol_TextDisabled), 8, 1.0f);
         else
            dl->AddCircleFilled(dot, 2.5f, ImGui::GetColorU32(ImGuiCol_CheckMark), 8);
         ImGui::SetCursorScreenPos(p);
         if (ImGui::InvisibleButton("##byp", ImVec2(12.0f, kInsertRowH)))
            changed = MixerPanelInsertBypass(r.mixerIndex, r.slot, i);
         if (ImGui::IsItemHovered())
            HelpTip("%s", off ? T("Enable") : T("Bypass"));
         const std::string name = NodeTitle(*g);
         ImGui::SetCursorScreenPos(ImVec2(p.x + 12.0f, p.y));
         if (ImGui::InvisibleButton("##chip", ImVec2(std::max(1.0f, w - 12.0f), kInsertRowH)))
            SelectOnCanvas(g->index);
         if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            HelpTip("%s", name.c_str());
         if (!changed && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
         {
            const int payload[3] = { r.mixerIndex, r.slot, i };
            ImGui::SetDragDropPayload("MIXER_INSERT", payload, sizeof(payload));
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
         }
         if (!changed && ImGui::BeginDragDropTarget())
         {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("MIXER_INSERT"))
            {
               const int* d = static_cast<const int*>(pl->Data);
               if (d[0] == r.mixerIndex && d[1] == r.slot && d[2] != i)
                  changed = MixerPanelInsertMove(r.mixerIndex, r.slot, d[2], i);
            }
            ImGui::EndDragDropTarget();
         }
         if (!changed && MenuParts::BeginContextItem("##insctx"))
         {
            changed = InsertMenuItems(r, i);
            MenuParts::EndPopup();
         }
         dl->PushClipRect(ImVec2(p.x + 11.0f, p.y), ImVec2(q.x - 2.0f, q.y), true);
         dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.85f, ImVec2(p.x + 12.0f, p.y + 1.0f),
                     ImGui::GetColorU32(off ? ImGuiCol_TextDisabled : ImGuiCol_Text), name.c_str());
         dl->PopClipRect();
         ImGui::PopID();
         return changed;
      }

      // §4.2 / D4: the strip's insert chain as chips, then "+". A shared chain is edited on the canvas.
      void DrawInserts(ImDrawList* dl, ImVec2 p, const StripRef& r)
      {
         const float w = kStripW - 2.0f;
         const float x = p.x + 1.0f;
         if (r.shared)
         {
            ImGui::SetCursorScreenPos(ImVec2(x, p.y));
            ImGui::InvisibleButton("##shared", ImVec2(w, kInsertRowH));
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Shared chain: edit it on the canvas"));
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.85f, ImVec2(x + 3.0f, p.y + 1.0f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), T("Shared"));
            return;
         }
         const int n = (int)r.inserts.size();
         const bool overflow = n > kInsertRows - 1;
         const int shown = overflow ? kInsertRows - 1 : n;
         for (int i = 0; i < shown; i++)
            if (DrawInsertChip(dl, ImVec2(x, p.y + (float)i * (kInsertRowH + 1.0f)), w, r, i))
               return;
         const float ry = p.y + (float)shown * (kInsertRowH + 1.0f);
         float plusX = x, plusW = w;
         if (overflow)
         {
            plusW = 16.0f;
            plusX = x + w - plusW;
            const std::string more = std::to_string(n - shown) + " " + T("more") + "##moreins";
            ImGui::SetCursorScreenPos(ImVec2(x, ry));
            if (ActionButton::Draw(more.c_str(), ImVec2(w - plusW - 1.0f, kInsertRowH)))
               ImGui::OpenPopup("##moreinsfx");
            if (MenuParts::BeginPopup("##moreinsfx"))
            {
               for (int i = shown; i < n; i++)
               {
                  GraphNode* g = FindNodeByIndex(r.inserts[i]);
                  if (g == nullptr)
                     continue;
                  ImGui::PushID(i);
                  bool changed = false;
                  if (MenuParts::SubMenu(NodeTitle(*g).c_str()))
                  {
                     changed = InsertMenuItems(r, i);
                     ImGui::EndMenu();
                  }
                  ImGui::PopID();
                  if (changed)
                     break;
               }
               MenuParts::EndPopup();
            }
         }
         ImGui::SetCursorScreenPos(ImVec2(plusX, ry));
         if (ActionButton::Draw("+##newinsert", ImVec2(plusW, kInsertRowH)))
            ImGui::OpenPopup("##newinsertfx");
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Add insert"));
         if (MenuParts::BeginPopup("##newinsertfx"))
         {
            static const char* kFx[] = { "EQ", "Dynamics", "Limiter", "Audio Filter", "Drive", "Saturator", "Delay",
                                         "Reverb", "Chorus", "Flanger", "Phaser", "Stereo", "Tremolo", "Bitcrush" };
            for (const char* fx : kFx)
               if (MenuParts::Item(L(fx)))
                  MixerPanelInsertAdd(r.mixerIndex, r.slot, fx);
            MenuParts::EndPopup();
         }
      }

      void DrawStrip(ImDrawList* dl, ImVec2 p, float h, const StripRef& r)
      {
         MixerNode* m = MixerAt(r.mixerIndex);
         if (m == nullptr)
            return;
         GraphNode* src = r.srcIndex >= 0 ? FindNodeByIndex(r.srcIndex) : nullptr;
         GraphNode* grp = r.groupIndex >= 0 ? FindNodeByIndex(r.groupIndex) : nullptr;
         const int s = r.slot;
         ImGui::PushID(r.mixerIndex);
         ImGui::PushID(s);

         const ImVec2 q(p.x + kStripW, p.y + h);
         if (r.depth > 0)
            dl->AddRectFilled(p, q, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.35f + 0.15f * (float)std::min(r.depth, 3)), 4.0f);
         const bool lit = sHighlightUid != 0 && ((src && src->uid == sHighlightUid) || (grp && grp->uid == sHighlightUid));
         if (lit)
            dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f, 0, 1.5f);

         // Name: the node on the cable. A group strip carries a fold arrow and its Mixer's name.
         // With inserts the name is the source above them; a return is named by its first fx.
         GraphNode* head = r.headIndex >= 0 ? FindNodeByIndex(r.headIndex) : nullptr;
         GraphNode* first = !r.inserts.empty() ? FindNodeByIndex(r.inserts.front()) : nullptr;
         GraphNode* named = grp ? grp : (r.isReturn && first) ? first : (head && !r.isReturn) ? head : src;
         std::string name = named ? NodeTitle(*named) : std::string(T("Empty"));
         float nameX = p.x, nameW = kStripW;
         if (grp)
         {
            const bool open = sOpenGroups.count(grp->uid) != 0;
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 3.0f));
            if (ImGui::InvisibleButton("##fold", ImVec2(14.0f, kNameH - 3.0f)))
            {
               if (open)
                  sOpenGroups.erase(grp->uid);
               else
                  sOpenGroups.insert(grp->uid);
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s", open ? T("Fold group") : T("Open group"));
            const ImVec2 c(p.x + 7.0f, p.y + 3.0f + (kNameH - 3.0f) * 0.5f);
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            if (open)
               dl->AddTriangleFilled(ImVec2(c.x - 4.0f, c.y - 2.0f), ImVec2(c.x + 4.0f, c.y - 2.0f), ImVec2(c.x, c.y + 3.0f), col);
            else
               dl->AddTriangleFilled(ImVec2(c.x - 2.0f, c.y - 4.0f), ImVec2(c.x - 2.0f, c.y + 4.0f), ImVec2(c.x + 3.0f, c.y), col);
            nameX += 12.0f;
            nameW -= 12.0f;
         }
         if (NameRow(dl, ImVec2(nameX, p.y), nameW, name.c_str(), CategoryColorOf(named), src == nullptr, &r) && named)
            SelectOnCanvas(named->index);
         if (sStripsStale)
         {
            ImGui::PopID();
            ImGui::PopID();
            return;
         }

         float y = p.y + kNameH + kRowGap;
         if (src != nullptr)
            DrawInserts(dl, ImVec2(p.x, y), r);
         y += kInsertsH + kRowGap;
         if (r.sends)
            DrawSends(ImVec2(p.x, y), m, r);
         y += kSendH + kRowGap;

         // Trim and pan side by side.
         const float cell = kStripW * 0.5f;
         ImGui::SetCursorScreenPos(ImVec2(p.x, y));
         float before = m->trimDb[s];
         BipolarKnobFloat("##trim", &m->trimDb[s], -24.0f, 24.0f, "%.1f dB", kKnobD, ImGui::GetColorU32(ImGuiCol_SliderGrab), false, cell,
                          -1, -1, false, 0.0f, 0.0f, false, true);
         UndoOnActivate(&m->trimDb[s], before);
         if (ImGui::IsItemHovered())
            HelpTip("%s %+.1f dB", T("Trim"), m->trimDb[s]);
         ImGui::SetCursorScreenPos(ImVec2(p.x + cell, y));
         before = m->pan[s];
         BipolarKnobFloat("##pan", &m->pan[s], -1.0f, 1.0f, "%.2f", kKnobD, ImGui::GetColorU32(ImGuiCol_SliderGrab), false, cell,
                          -1, -1, false, 0.0f, 0.0f, false, true);
         UndoOnActivate(&m->pan[s], before);
         if (ImGui::IsItemHovered())
            HelpTip("%s %+.0f", T("Pan"), m->pan[s] * 100.0f);

         // Fader | meter.
         y += kKnobD + kRowGap;
         const float faderH = FaderHeightFor(h);
         gMixerPanelFaderH = faderH;
         FaderWithMeter(ImVec2(p.x + 2.0f, y), kStripW - 4.0f, faderH, &m->gainDb[s], m->ChannelLevel(s));

         // M S Ø.
         y += faderH + kRowGap;
         const float bw = (kStripW - 4.0f) / 3.0f;
         ImGui::SetCursorScreenPos(ImVec2(p.x + 1.0f, y));
         if (AudioMuteButton("M##m", &m->mute[s], bw, kButtonsH))
            UndoOnToggle(&m->mute[s]);
         ImGui::SetCursorScreenPos(ImVec2(p.x + 2.0f + bw, y));
         if (AudioSoloButton("S##s", &m->solo[s], bw, kButtonsH))
            UndoOnToggle(&m->solo[s]);
         if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
         {
            PushUndoCheckpoint("Mixer strip");
            m->soloSafe[s] = !m->soloSafe[s];
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", m->soloSafe[s] ? T("Solo safe (right-click)") : T("Solo (right-click: solo safe)"));
         ImGui::SetCursorScreenPos(ImVec2(p.x + 3.0f + 2.0f * bw, y));
         if (ActionButton::Draw("\xC3\x98##ph", ImVec2(bw, kButtonsH), m->phase[s] ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
         {
            PushUndoCheckpoint("Mixer strip");
            m->phase[s] = !m->phase[s];
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Invert polarity"));

         ImGui::PopID();
         ImGui::PopID();
      }

      void DrawMaster(ImDrawList* dl, ImVec2 p, float h, int topIndex)
      {
         MixerNode* m = MixerAt(topIndex);
         GraphNode* gn = FindNodeByIndex(topIndex);
         if (m == nullptr || gn == nullptr)
            return;
         ImGui::PushID("master");
         dl->AddRectFilled(p, ImVec2(p.x + kStripW, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg, 0.6f), 4.0f);
         if (NameRow(dl, p, kStripW, T("Master"), CategoryColorOf(gn), false))
            SelectOnCanvas(topIndex);
         const float y = p.y + kNameH + kRowGap + kInsertsH + kRowGap + kSendH + kRowGap + kKnobD + kRowGap;
         // B4: true peak and LUFS M / S / I above the fader. Asking each frame is what keeps the meter running.
         m->RequestMeters();
         {
            const float top = p.y + kNameH + kRowGap, rowH = 15.0f;
            ImGui::SetCursorScreenPos(ImVec2(p.x, top));
            if (ImGui::InvisibleButton("##loud", ImVec2(kStripW, 4.0f * rowH)))
               m->ResetMeters();
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("True peak (dBTP) and loudness (LUFS): momentary, short-term, integrated. Click to reset."));
            const char* tags[4] = { "TP", "M", "S", "I" };
            const float vals[4] = { m->TruePeakDb(), m->LoudnessMomentary(), m->LoudnessShort(), m->LoudnessIntegrated() };
            const float fs = ImGui::GetFontSize() * 0.85f;
            for (int i = 0; i < 4; i++)
            {
               char txt[16];
               if (vals[i] <= -99.0f)
                  snprintf(txt, sizeof(txt), "-inf");
               else
                  snprintf(txt, sizeof(txt), "%.1f", vals[i]);
               const float ry = top + (float)i * rowH;
               const bool over = i == 0 && vals[0] > -1.0f; // above -1 dBTP: likely to clip once encoded
               dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + 4.0f, ry), ImGui::GetColorU32(ImGuiCol_TextDisabled), tags[i]);
               const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, txt).x;
               dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + kStripW - 4.0f - tw, ry),
                           ImGui::GetColorU32(over ? ImGuiCol_PlotHistogram : ImGuiCol_Text), txt);
            }
         }
         FaderWithMeter(ImVec2(p.x + 2.0f, y), kStripW - 4.0f, FaderHeightFor(h), &m->masterDb, m->Level());
         ImGui::PopID();
         gMixerMasterMin = p;
         gMixerMasterMax = ImVec2(p.x + kStripW, p.y + h);
      }

      int SpawnAt(const char* type, float x, float y)
      {
         GraphNode* gn = SpawnNode(type, "Utility", x, y);
         return gn ? gn->index : -1;
      }

      // Right of everything on the canvas, so a new pair never lands on a node.
      ImVec2 FreeCanvasSpot()
      {
         float x = 0.0f, y = 0.0f;
         bool any = false;
         for (const GraphNode& gn : gNodes)
         {
            x = any ? std::max(x, gn.liveX) : gn.liveX;
            y = any ? std::min(y, gn.liveY) : gn.liveY;
            any = true;
         }
         return any ? ImVec2(x + 420.0f, y) : ImVec2(0.0f, 0.0f);
      }

      void DrawPageTabs(const std::vector<Page>& pages, int current)
      {
         for (int i = 0; i < (int)pages.size(); i++)
         {
            GraphNode* top = FindNodeByIndex(pages[i].topIndex);
            const std::string label = (top ? NodeTitle(*top) : std::string(T("Page"))) + "##page" + std::to_string(i);
            if (ChipButton::Draw(label.c_str(), i == current, tok::tile))
               sPageUid = pages[i].topUid;
            ImGui::SameLine(0.0f, tok::space_1);
         }
         if (ChipButton::Draw((std::string("+ ") + T("page") + "##addpage").c_str(), false, tok::tile))
            MixerPanelAddPage();
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("New Mixer + Audio Out"));
      }

      void DrawEmpty(ImVec2 mn, ImVec2 mx)
      {
         const bool noMixer = CountMixers() == 0;
         bool anyOut = false;
         for (GraphNode& gn : gNodes)
            if (auto* out = dynamic_cast<AudioOutputNode*>(gn.node.get()))
               anyOut = anyOut || out->input.IsConnected();
         EmptyState::DrawTo(ImGui::GetWindowDrawList(), mn, ImVec2(mx.x, mn.y + (mx.y - mn.y) * 0.6f),
                            noMixer ? T("No Mixer yet") : T("No Mixer feeds an Audio Out"));
         if (!anyOut)
            return;
         const char* label = T("Create from Audio Outs");
         const float w = ImGui::CalcTextSize(label).x + 2.0f * tok::space_3;
         ImGui::SetCursorScreenPos(ImVec2((mn.x + mx.x - w) * 0.5f, mn.y + (mx.y - mn.y) * 0.6f));
         if (ActionButton::Draw(label, ImVec2(w, 0.0f), ActionButton::Kind::Primary))
            MixerPanelCreateFromAudioOuts();
      }

      void DrawMixerPanelContent()
      {
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 avail = ImGui::GetContentRegionAvail();
         const std::vector<Page> pages = CollectPages();
         FollowCanvasSelection(pages);
         const int current = CurrentPage(pages);

         // Header: the title and page tabs left, Mixer Audio pinned right (the twin of Session's chip).
         ImGui::AlignTextToFramePadding();
         {
            UiType::Scope hs(UiType::Size::Title, UiType::Weight::Semibold);
            ImGui::TextUnformatted(T("Mixer"));
         }
         ImGui::SameLine(0.0f, tok::space_3);
         DrawPageTabs(pages, current);
         {
            const bool mixerMode = gAudioMode == AudioMode::Mixer;
            const char* audioLabel = mixerMode ? T("Mixer Audio On") : T("Enable Mixer Audio");
            const float audioW = ImGui::CalcTextSize(audioLabel).x + 2.0f * tok::space_2;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, origin.x + avail.x - audioW), origin.y));
            if (ChipButton::Draw(audioLabel, mixerMode, tok::tile))
               SetMixerAudio(!mixerMode);
            if (ImGui::IsItemHovered())
               HelpTip("%s", mixerMode ? T("Only the console reaches the speakers. Click to hand audio back to the canvas.")
                                       : T("Hear only the console: other Audio Outs go silent."));
         }

         const ImVec2 bodyMin(origin.x, ImGui::GetCursorScreenPos().y + tok::space_2);
         const ImVec2 bodyMax(origin.x + avail.x, origin.y + avail.y);
         gMixerPanelStripCount = 0;
         if (current < 0)
         {
            if (bodyMax.y > bodyMin.y)
               DrawEmpty(bodyMin, bodyMax);
         }
         else if (bodyMax.y - bodyMin.y > 40.0f)
         {
            const Page& page = pages[current];
            const std::vector<StripRef> strips = Strips(page.topIndex, false);
            MixerNode* top = MixerAt(page.topIndex);
            const bool canAdd = top && top->numChannels < MixerNode::kMaxSlots;

            // §4.4: master at the right edge on a wide dock, under the strips on a tall one.
            const float bodyW = bodyMax.x - bodyMin.x, bodyH = bodyMax.y - bodyMin.y;
            const float scrollH = ImGui::GetStyle().ScrollbarSize;
            const float minStripH = kNameH + kInsertsH + kSendH + kKnobD + kButtonsH + 5.0f * kRowGap + kMinFaderH;
            const bool tall = (gMixerPanelDock == 1 || gMixerPanelDock == 2) && bodyH >= 2.0f * minStripH + scrollH + kStripGap;
            float stripsW, stripsH, masterH;
            ImVec2 masterAt;
            if (tall)
            {
               stripsW = bodyW;
               stripsH = std::min(kStripMaxH + scrollH, (bodyH - kStripGap) * 0.5f);
               masterH = std::min(kStripMaxH, bodyH - stripsH - kStripGap);
               masterAt = ImVec2(bodyMin.x, bodyMin.y + stripsH + kStripGap);
            }
            else
            {
               stripsW = std::max(kStripW, bodyW - kStripW - kStripGap * 2.0f);
               stripsH = std::min(kStripMaxH + scrollH, bodyH);
               masterH = stripsH - scrollH;
               masterAt = ImVec2(bodyMax.x - kStripW, bodyMin.y);
            }

            ImGui::SetCursorScreenPos(bodyMin);
            ImGui::BeginChild("##mixerstrips", ImVec2(stripsW, stripsH), false, ImGuiWindowFlags_HorizontalScrollbar);
            {
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImVec2 base = ImGui::GetCursorScreenPos();
               const float h = stripsH - scrollH;
               const ImVec2 wmn = ImGui::GetWindowPos();
               const float visL = wmn.x, visR = wmn.x + ImGui::GetWindowWidth();
               float x = base.x;
               bool inReturns = false;
               sStripsStale = false;
               for (const StripRef& r : strips)
               {
                  if (sStripsStale)
                     break;
                  if (r.isReturn && !inReturns)
                  {
                     inReturns = true;
                     const float mid = x + (kReturnGap - kStripGap) * 0.5f - kStripGap * 0.5f;
                     dl->AddLine(ImVec2(mid, base.y + 4.0f), ImVec2(mid, base.y + h - 4.0f), ImGui::GetColorU32(ImGuiCol_Separator));
                     x += kReturnGap - kStripGap;
                  }
                  if (x + kStripW >= visL && x <= visR)
                     DrawStrip(dl, ImVec2(x, base.y), h, r);
                  x += kStripW + kStripGap;
               }
               if (canAdd)
               {
                  ImGui::SetCursorScreenPos(ImVec2(x, base.y + kNameH));
                  if (ActionButton::Draw("+##addstrip", ImVec2(24.0f, 24.0f)))
                  {
                     PushUndoCheckpoint("Add strip");
                     top->numChannels = std::min(top->numChannels + 1, (int)MixerNode::kMaxSlots);
                     RebuildAudioTopology();
                  }
                  if (ImGui::IsItemHovered())
                     HelpTip("%s", T("Add strip"));
                  x += 24.0f + kStripGap;
               }
               ImGui::SetCursorScreenPos(base);
               ImGui::Dummy(ImVec2(std::max(1.0f, x - base.x), h));
               gMixerStripsMin = wmn;
               gMixerStripsMax = ImVec2(wmn.x + ImGui::GetWindowWidth(), wmn.y + ImGui::GetWindowHeight());
            }
            ImGui::EndChild();
            gMixerPanelStripCount = (int)strips.size();
            DrawMaster(ImGui::GetWindowDrawList(), masterAt, masterH, page.topIndex);
         }

         // Right-click empty panel space to move or close it (rect test, as the Mod Matrix does).
         const ImVec2 mouse = ImGui::GetIO().MousePos;
         const bool overPanel = mouse.x >= origin.x && mouse.x < origin.x + avail.x &&
                                mouse.y >= origin.y && mouse.y < origin.y + avail.y;
         gMixerClaimedKeys = overPanel && !ImGui::GetIO().WantTextInput;
         if (overPanel && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
            ImGui::OpenPopup("##mixerpanelctx");
         if (MenuParts::BeginPopup("##mixerpanelctx"))
         {
            static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top"), I18N_KEY("Floating Window") };
            for (int i = 0; i < 5; i++)
               if (MenuParts::Item(L(kDockLabels[i]), nullptr, i == gMixerPanelDock))
                  gMixerPanelDock = i;
            MenuParts::Separator();
            if (MenuParts::Item(L("Close panel")))
               gMixerPanelOpen = false;
            MenuParts::EndPopup();
         }
      }
   }

   int MixerPanelPageCount() { return (int)CollectPages().size(); }

   // A5: the canvas Mixer's "Open in Mixer panel". Opens the panel on the page that holds this Mixer.
   bool MixerPanelShowMixer(int nodeIndex)
   {
      gMixerPanelOpen = true;
      GraphNode* gn = FindNodeByIndex(nodeIndex);
      sLastSelectedUid = SelectedNodeUid(); // the menu's own selection must not move the page again
      return ShowNode(CollectPages(), gn);
   }

   int MixerPanelCurrentPageTop()
   {
      const std::vector<Page> pages = CollectPages();
      const int i = CurrentPage(pages);
      return i < 0 ? -1 : pages[i].topIndex;
   }

   bool MixerPanelGroupOpen(int mixerIndex)
   {
      GraphNode* gn = FindNodeByIndex(mixerIndex);
      return gn != nullptr && sOpenGroups.count(gn->uid) != 0;
   }

   void MixerPanelSelectPage(int page)
   {
      const std::vector<Page> pages = CollectPages();
      if (page >= 0 && page < (int)pages.size())
         sPageUid = pages[page].topUid;
   }

   // G8: a page is a Mixer feeding its own Audio Out. One undo step for the pair and its cable.
   int MixerPanelAddPage()
   {
      const ImVec2 at = FreeCanvasSpot();
      PushUndoCheckpoint("New mixer page");
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      gSuppressUndoCheckpoints = true;
      const int mix = SpawnAt("Mixer", at.x, at.y);
      const int out = SpawnAt("Audio Out", at.x + 320.0f, at.y);
      GraphNode* mixGn = FindNodeByIndex(mix);
      GraphNode* outGn = FindNodeByIndex(out);
      if (mixGn && outGn)
      {
         WireInputSlot(*mixGn, *outGn, 0, 0);
         sPageUid = mixGn->uid;
      }
      gSuppressUndoCheckpoints = wasSuppressed;
      RebuildAudioTopology();
      return mix;
   }

   // B1 / G1: a new effect on send bus N of the strip's Mixer, returned to the page's top Mixer.
   // A console whose top still holds its strips directly is wrapped first: the old top becomes a
   // group under a new top, so the return lands downstream of every strip that can send to it.
   // One undo step. Returns the effect's node index, or -1 with a notice when refused.
   int MixerPanelNewSendFx(int mixerIndex, int slot, const char* fxType)
   {
      MixerNode* m = MixerAt(mixerIndex);
      GraphNode* mGn = FindNodeByIndex(mixerIndex);
      if (m == nullptr || mGn == nullptr || slot < 0 || slot >= m->numChannels)
         return -1;
      if (m->SendBuses() >= MixerNode::kSendBuses)
      {
         Notices::Post(Notices::Level::Warning, "mixer.send", T("All four sends are in use"),
                       T("Use an existing send knob on this strip."));
         return -1;
      }
      const std::vector<Page> pages = CollectPages();
      const Page* page = nullptr;
      for (const Page& p : pages)
      {
         if (p.topIndex == mixerIndex)
            page = &p;
         else
            for (const StripRef& r : Strips(p.topIndex, true))
               if (r.mixerIndex == mixerIndex)
                  page = &p;
         if (page != nullptr)
            break;
      }
      if (page == nullptr)
         return -1;
      const int outIndex = page->outIndex;
      int topIndex = page->topIndex;
      MixerNode* top = MixerAt(topIndex);
      if (top == nullptr)
         return -1;
      const bool wrap = topIndex == mixerIndex;
      if (!wrap && top->numChannels >= MixerNode::kMaxSlots)
      {
         Notices::Post(Notices::Level::Warning, "mixer.send", T("No free strip for the return"),
                       T("The top Mixer already has 32 strips."));
         return -1;
      }

      // The return must land downstream of the sending Mixer; a top that feeds it would loop.
      if (!wrap && WouldCreateAudioCycle(m, top))
      {
         Notices::Post(Notices::Level::Warning, "audio.cycle", T("Can't return there"),
                       T("That would feed the send back into itself."));
         return -1;
      }
      PushUndoCheckpoint("New send FX");
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      gSuppressUndoCheckpoints = true;
      int fxIndex = -1;
      do
      {
         if (wrap)
         {
            const int newTop = SpawnAt("Mixer", mGn->liveX + 320.0f, mGn->liveY);
            GraphNode* outGn = FindNodeByIndex(outIndex);
            GraphNode* topGn = FindNodeByIndex(newTop);
            mGn = FindNodeByIndex(mixerIndex);
            if (outGn == nullptr || topGn == nullptr)
               break;
            auto* nt = static_cast<MixerNode*>(topGn->node.get());
            nt->numChannels = 1;
            nt->masterDb = m->masterDb; // the console's master fader moves up with the console
            m->masterDb = 0.0f;
            WireInputSlot(*mGn, *topGn, 0, 0);
            DisconnectDstPin(outGn->InputPinId(0));
            WireInputSlot(*topGn, *outGn, 0, 0);
            sOpenGroups.insert(mGn->uid);
            sPageUid = topGn->uid;
            topIndex = newTop;
            top = nt;
         }
         mGn = FindNodeByIndex(mixerIndex); // spawning may move gNodes
         GraphNode* topGn = FindNodeByIndex(topIndex);
         GraphNode* fxGn = SpawnNode(fxType, "AudioEffects", mGn->liveX + 160.0f, mGn->liveY + 260.0f);
         mGn = FindNodeByIndex(mixerIndex); // and spawning the fx too
         topGn = FindNodeByIndex(topIndex);
         if (fxGn == nullptr || topGn == nullptr || mGn == nullptr)
            break;
         if (auto* fx = dynamic_cast<AudioEffectNode*>(fxGn->node.get()))
            fx->mix = 1.0f; // a return is all wet; the strip's dry signal stays on its own path
         const int bus = m->SendBuses();
         m->sendBuses = bus + 1;
         m->sendDb[bus][slot] = 0.0f;
         WireInputSlot(*mGn, *fxGn, 0, 1 + bus);
         const int ret = top->numChannels;
         top->numChannels = ret + 1;
         WireInputSlot(*fxGn, *topGn, ret, 0);
         fxIndex = fxGn->index;
      } while (false);
      gSuppressUndoCheckpoints = wasSuppressed;
      RebuildAudioTopology();
      return fxIndex;
   }

   // ---- D4 inserts: walk, splice, bypass, reorder. Each edit is one undo step. ----

   namespace
   {
      bool ChainAt(int mixerIndex, int slot, Chain& ch)
      {
         MixerNode* m = MixerAt(mixerIndex);
         if (m == nullptr || slot < 0 || slot >= m->numChannels)
            return false;
         ch = WalkInserts(m->inputs[slot], CountConsumers());
         return true;
      }

      bool RefuseShared(const Chain& ch)
      {
         if (!ch.shared)
            return false;
         Notices::Post(Notices::Level::Warning, "mixer.insert", T("Shared chain"), T("Edit it on the canvas."));
         return true;
      }

      // Rewires head -> order... -> the slot. Nodes left out of order keep their own input; the caller removes them.
      void Splice(int mixerIndex, int slot, const Chain& ch, const std::vector<int>& order, const std::vector<int>& orderOut)
      {
         INode* prev = ch.head;
         int prevOut = ch.headOut;
         for (size_t k = 0; k < order.size(); k++)
         {
            GraphNode* g = FindNodeByIndex(order[k]);
            AudioCable* in = g ? g->node->AudioInputSlot(0) : nullptr;
            if (in == nullptr)
               continue;
            if (prev != nullptr)
               in->Connect(prev, prevOut);
            else
               in->Disconnect();
            prev = g->node.get();
            prevOut = orderOut[k];
         }
         if (MixerNode* m = MixerAt(mixerIndex))
         {
            if (prev != nullptr)
               m->inputs[slot].Connect(prev, prevOut);
            else
               m->inputs[slot].Disconnect();
         }
      }

      struct UndoScope
      {
         bool was;
         explicit UndoScope(const char* label) : was(gSuppressUndoCheckpoints)
         {
            PushUndoCheckpoint(label);
            gSuppressUndoCheckpoints = true;
         }
         ~UndoScope()
         {
            gSuppressUndoCheckpoints = was;
            RebuildAudioTopology();
         }
      };
   }

   std::vector<int> MixerPanelInsertChain(int mixerIndex, int slot, bool* shared)
   {
      Chain ch;
      const bool ok = ChainAt(mixerIndex, slot, ch);
      if (shared != nullptr)
         *shared = ok && ch.shared;
      return ch.fx;
   }

   int MixerPanelInsertAdd(int mixerIndex, int slot, const char* fxType)
   {
      Chain ch;
      if (!ChainAt(mixerIndex, slot, ch) || RefuseShared(ch) || !MixerAt(mixerIndex)->inputs[slot].IsConnected())
         return -1;
      GraphNode* mGn = FindNodeByIndex(mixerIndex);
      if (mGn == nullptr)
         return -1;
      const float x = mGn->liveX - 220.0f, y = mGn->liveY + 60.0f * (float)slot;
      UndoScope undo("Add insert");
      GraphNode* fx = SpawnNode(fxType, "AudioEffects", x, y);
      if (fx == nullptr || fx->node->AudioInputSlot(0) == nullptr)
         return -1;
      std::vector<int> order = ch.fx, orderOut = ch.fxOut;
      order.push_back(fx->index); // nearest the fader
      orderOut.push_back(0);
      const int fxIndex = fx->index;
      Splice(mixerIndex, slot, ch, order, orderOut);
      return fxIndex;
   }

   bool MixerPanelInsertRemove(int mixerIndex, int slot, int pos)
   {
      Chain ch;
      if (!ChainAt(mixerIndex, slot, ch) || RefuseShared(ch) || pos < 0 || pos >= (int)ch.fx.size())
         return false;
      UndoScope undo("Remove insert");
      const int victim = ch.fx[pos];
      std::vector<int> order = ch.fx, orderOut = ch.fxOut;
      order.erase(order.begin() + pos);
      orderOut.erase(orderOut.begin() + pos);
      Splice(mixerIndex, slot, ch, order, orderOut);
      RemoveNodeByIndex(victim);
      return true;
   }

   bool MixerPanelInsertMove(int mixerIndex, int slot, int from, int to)
   {
      Chain ch;
      if (!ChainAt(mixerIndex, slot, ch) || RefuseShared(ch))
         return false;
      if (from < 0 || to < 0 || from >= (int)ch.fx.size() || to >= (int)ch.fx.size() || from == to)
         return false;
      UndoScope undo("Move insert");
      std::vector<int> order = ch.fx, orderOut = ch.fxOut;
      const int id = order[from], o = orderOut[from];
      order.erase(order.begin() + from);
      orderOut.erase(orderOut.begin() + from);
      order.insert(order.begin() + to, id);
      orderOut.insert(orderOut.begin() + to, o);
      Splice(mixerIndex, slot, ch, order, orderOut);
      return true;
   }

   bool MixerPanelInsertBypass(int mixerIndex, int slot, int pos)
   {
      Chain ch;
      if (!ChainAt(mixerIndex, slot, ch) || RefuseShared(ch) || pos < 0 || pos >= (int)ch.fx.size())
         return false;
      GraphNode* g = FindNodeByIndex(ch.fx[pos]);
      if (g == nullptr)
         return false;
      UndoScope undo("Bypass insert");
      g->node->bypassed = !g->node->bypassed;
      return true;
   }

   // ---- B3: group, ungroup, reorder. A strip's whole state moves with it; one undo step each. ----

   namespace
   {
      struct StripState
      {
         float gain = 0.0f, pan = 0.0f, trim = 0.0f;
         bool mute = false, solo = false, phase = false, safe = false, pre = false;
         float send[MixerNode::kSendBuses];
         INode* src = nullptr;
         int out = 0;
         StripState() { std::fill(std::begin(send), std::end(send), MixerNode::kSendOffDb); }
      };

      StripState GetStrip(const MixerNode& m, int s)
      {
         StripState st;
         st.gain = m.gainDb[s]; st.pan = m.pan[s]; st.trim = m.trimDb[s];
         st.mute = m.mute[s]; st.solo = m.solo[s]; st.phase = m.phase[s]; st.safe = m.soloSafe[s]; st.pre = m.sendPre[s];
         for (int b = 0; b < MixerNode::kSendBuses; b++)
            st.send[b] = m.sendDb[b][s];
         if (m.inputs[s].IsConnected())
         {
            st.src = m.inputs[s].GetSource();
            st.out = m.inputs[s].GetOutputSlot();
         }
         return st;
      }

      void SetStrip(MixerNode& m, int s, const StripState& st)
      {
         m.gainDb[s] = st.gain; m.pan[s] = st.pan; m.trimDb[s] = st.trim;
         m.mute[s] = st.mute; m.solo[s] = st.solo; m.phase[s] = st.phase; m.soloSafe[s] = st.safe; m.sendPre[s] = st.pre;
         for (int b = 0; b < MixerNode::kSendBuses; b++)
            m.sendDb[b][s] = st.send[b];
         if (st.src != nullptr)
            m.inputs[s].Connect(st.src, st.out);
         else
            m.inputs[s].Disconnect();
      }

      std::vector<StripState> ReadStrips(const MixerNode& m)
      {
         std::vector<StripState> v;
         for (int s = 0; s < m.numChannels; s++)
            v.push_back(GetStrip(m, s));
         return v;
      }

      // Slots past the list go back to defaults, unplugged.
      void WriteStrips(MixerNode& m, const std::vector<StripState>& v)
      {
         const int n = std::min((int)v.size(), (int)MixerNode::kMaxSlots);
         for (int s = 0; s < MixerNode::kMaxSlots; s++)
            SetStrip(m, s, s < n ? v[s] : StripState());
         m.numChannels = n;
      }

      bool HasLiveSend(const MixerNode& m, int s)
      {
         for (int b = 0; b < m.SendBuses(); b++)
            if (m.sendDb[b][s] > MixerNode::kSendOffDb)
               return true;
         return false;
      }

      void RegroupDone()
      {
         sSelStrips.clear();
         sStripsStale = true;
      }
   }

   // The picked strips move into a new Mixer, which takes the first one's place at unity.
   // Returns the group's node index, or -1 with a notice when refused.
   int MixerPanelGroupStrips(int mixerIndex, const std::vector<int>& slots)
   {
      MixerNode* m = MixerAt(mixerIndex);
      GraphNode* mGn = FindNodeByIndex(mixerIndex);
      if (m == nullptr || mGn == nullptr)
         return -1;
      std::set<int> sel;
      for (int s : slots)
         if (s >= 0 && s < m->numChannels)
            sel.insert(s);
      if (sel.empty())
         return -1;
      for (int s : sel)
         if (HasLiveSend(*m, s))
         {
            Notices::Post(Notices::Level::Warning, "mixer.group", T("Can't group a strip that sends"),
                          T("Turn its sends off first."));
            return -1;
         }
      const float x = mGn->liveX - 320.0f, y = mGn->liveY + 60.0f * (float)*sel.begin();
      UndoScope undo("Group strips");
      GraphNode* gGn = SpawnNode("Mixer", "Utility", x, y);
      m = MixerAt(mixerIndex); // spawning may move gNodes
      MixerNode* g = gGn ? dynamic_cast<MixerNode*>(gGn->node.get()) : nullptr;
      if (g == nullptr || m == nullptr)
         return -1;
      const std::vector<StripState> all = ReadStrips(*m);
      std::vector<StripState> inner, outer;
      for (int s = 0; s < (int)all.size(); s++)
      {
         if (!sel.count(s))
         {
            outer.push_back(all[s]);
            continue;
         }
         if (s == *sel.begin())
         {
            StripState bus;
            bus.src = g;
            outer.push_back(bus);
         }
         inner.push_back(all[s]);
      }
      g->panLaw = m->panLaw;
      g->masterDb = 0.0f;
      WriteStrips(*m, outer); // unplugs the moved cables before the group takes them
      WriteStrips(*g, inner);
      sOpenGroups.insert(gGn->uid);
      RegroupDone();
      return gGn->index;
   }

   // The group's strips return to the slot it held, carrying its fader, trim and master; the group Mixer is removed.
   bool MixerPanelUngroup(int mixerIndex, int slot)
   {
      MixerNode* m = MixerAt(mixerIndex);
      if (m == nullptr || slot < 0 || slot >= m->numChannels)
         return false;
      const Consumers uses = CountConsumers();
      const Chain ch = WalkInserts(m->inputs[slot], uses);
      MixerNode* g = dynamic_cast<MixerNode*>(ch.head);
      GraphNode* gGn = GraphNodeOf(g);
      if (g == nullptr || gGn == nullptr || ch.shared || ch.headOut != 0)
         return false;
      const auto it = uses.find(g);
      if (!ch.fx.empty() || g->SendBuses() > 0 || (it != uses.end() && it->second > 1))
      {
         Notices::Post(Notices::Level::Warning, "mixer.group", T("Can't ungroup"),
                       T("The group has inserts, sends or a second cable. Edit it on the canvas."));
         return false;
      }
      if (m->numChannels - 1 + g->numChannels > MixerNode::kMaxSlots)
      {
         Notices::Post(Notices::Level::Warning, "mixer.group", T("Can't ungroup"), T("The Mixer would pass 32 strips."));
         return false;
      }
      UndoScope undo("Ungroup strips");
      const std::vector<StripState> all = ReadStrips(*m);
      const StripState bus = all[slot];
      const float busDb = bus.gain + bus.trim + g->masterDb;
      std::vector<StripState> flat(all.begin(), all.begin() + slot);
      for (StripState st : ReadStrips(*g))
      {
         st.gain = std::clamp(st.gain + busDb, -60.0f, 12.0f);
         st.mute = st.mute || bus.mute;
         st.solo = st.solo || bus.solo;
         st.safe = st.safe || bus.safe;
         st.phase = st.phase != bus.phase;
         st.pre = false;
         std::fill(std::begin(st.send), std::end(st.send), MixerNode::kSendOffDb);
         flat.push_back(st);
      }
      flat.insert(flat.end(), all.begin() + slot + 1, all.end());
      const int victim = gGn->index;
      sOpenGroups.erase(gGn->uid);
      WriteStrips(*g, {});
      WriteStrips(*m, flat);
      RemoveNodeByIndex(victim);
      RegroupDone();
      return true;
   }

   bool MixerPanelStripMove(int mixerIndex, int from, int to)
   {
      MixerNode* m = MixerAt(mixerIndex);
      if (m == nullptr || from < 0 || to < 0 || from >= m->numChannels || to >= m->numChannels || from == to)
         return false;
      UndoScope undo("Move strip");
      std::vector<StripState> all = ReadStrips(*m);
      const StripState st = all[from];
      all.erase(all.begin() + from);
      all.insert(all.begin() + to, st);
      WriteStrips(*m, all);
      RegroupDone();
      return true;
   }

   // D13: every Audio Out's source goes into one new Mixer, which feeds the first of those Audio Outs.
   // The others are unplugged so nothing plays twice.
   bool MixerPanelCreateFromAudioOuts()
   {
      std::vector<int> outs;
      for (GraphNode& gn : gNodes)
         if (auto* out = dynamic_cast<AudioOutputNode*>(gn.node.get()))
            if (out->input.IsConnected() && dynamic_cast<MixerNode*>(ResolvedAudioSource(out->input.GetSource())) == nullptr)
               outs.push_back(gn.index);
      if (outs.empty())
         return false;
      struct Feed { int src; int outSlot; };
      std::vector<Feed> feeds;
      for (int oi : outs)
      {
         auto* out = dynamic_cast<AudioOutputNode*>(FindNodeByIndex(oi)->node.get());
         if (GraphNode* src = GraphNodeOf(out->input.GetSource()))
            feeds.push_back({ src->index, out->input.GetOutputSlot() });
      }
      if (feeds.empty() || feeds.size() > (size_t)MixerNode::kMaxSlots)
         return false;
      GraphNode* first = FindNodeByIndex(outs[0]);
      const ImVec2 at(first->liveX - 300.0f, first->liveY);

      PushUndoCheckpoint("Create Mixer from Audio Outs");
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      gSuppressUndoCheckpoints = true;
      const int mix = SpawnAt("Mixer", at.x, at.y);
      if (MixerNode* m = MixerAt(mix))
         m->numChannels = std::max(m->numChannels, (int)feeds.size());
      for (size_t k = 0; k < feeds.size(); k++)
      {
         GraphNode* src = FindNodeByIndex(feeds[k].src);
         GraphNode* mixGn = FindNodeByIndex(mix);
         if (src && mixGn)
            WireInputSlot(*src, *mixGn, (int)k, feeds[k].outSlot);
      }
      for (size_t i = 1; i < outs.size(); i++)
         if (GraphNode* o = FindNodeByIndex(outs[i]))
            DisconnectDstPin(o->InputPinId(0));
      GraphNode* mixGn = FindNodeByIndex(mix);
      GraphNode* firstOut = FindNodeByIndex(outs[0]);
      if (mixGn && firstOut)
      {
         WireInputSlot(*mixGn, *firstOut, 0, 0);
         sPageUid = mixGn->uid;
      }
      gSuppressUndoCheckpoints = wasSuppressed;
      RebuildAudioTopology();
      return mixGn != nullptr;
   }

   void MixerPanelDockCombo()
   {
      static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top"), I18N_KEY("Floating Window") };
      gMixerPanelDock = std::clamp(gMixerPanelDock, 0, kDockFloating);
      if (MenuParts::BeginCombo("##mixerpaneldock", T(kDockLabels[gMixerPanelDock])))
      {
         for (int i = 0; i < 5; i++)
            if (MenuParts::Choice(L(kDockLabels[i]), i == gMixerPanelDock))
               gMixerPanelDock = i;
         MenuParts::EndCombo();
      }
   }

   // The outer frame at every dock: the grip strip on the canvas side, then the card (as DrawModMatrixDocked).
   void DrawMixerPanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = PanelFrame::kGap;
      const int dock = gMixerPanelDock;
      const bool vertical = (dock == 1 || dock == 2);
      const bool gripFirst = (dock == 0 || dock == 1);

      ImGui::BeginChild(id, size, false);
      const ImVec2 inner = ImGui::GetContentRegionAvail();

      auto grip = [&]()
      {
         ImGui::InvisibleButton("##mixerpanelgrip",
                                vertical ? ImVec2(kGrip, std::max(1.0f, inner.y))
                                         : ImVec2(std::max(1.0f, inner.x), kGrip));
         DrawPanelSeam(vertical, gripFirst);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsItemActive())
         {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            switch (dock)
            {
               case 0: gMixerPanelHeight -= d.y; break;
               case 1: gMixerPanelWidth -= d.x; break;
               case 2: gMixerPanelWidth += d.x; break;
               default: gMixerPanelHeight += d.y; break;
            }
            gMixerPanelWidth = std::max(kMixerPanelMinWidth, gMixerPanelWidth);
            gMixerPanelHeight = std::max(kMixerPanelMinHeight, gMixerPanelHeight);
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
      switch (dock)
      {
         case 0: in.t = 0.0f; break;
         case 1: in.l = 0.0f; break;
         case 2: in.r = 0.0f; break;
         default: in.b = 0.0f; break;
      }
      PushDockedPanelStyle(/*isChild=*/true);
      PanelFrame::BeginCard("##mixerpanelcontent", cardSize, in);
      PopDockedPanelStyle();
      gMixerPanelRectMin = ImGui::GetWindowPos();
      gMixerPanelRectMax = ImVec2(gMixerPanelRectMin.x + ImGui::GetWindowWidth(), gMixerPanelRectMin.y + ImGui::GetWindowHeight());
      DrawMixerPanelContent();
      PanelFrame::EndCard();

      if (!gripFirst)
      {
         if (vertical)
            ImGui::SameLine();
         grip();
      }
      // Zeroed only around EndChild so no ItemSpacing strip shows between this panel and the next row.
      const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
      ImGui::EndChild();
      ImGui::GetStyle().ItemSpacing = savedSpacing;
   }

   void DrawMixerPanelFloating()
   {
      bool open = true;
      if (BeginFloatingPanel((std::string(T("Mixer")) + "###mixerpanelfloat").c_str(), &open, ImVec2(760.0f, 360.0f)))
      {
         gMixerPanelRectMin = ImGui::GetWindowPos();
         gMixerPanelRectMax = ImVec2(gMixerPanelRectMin.x + ImGui::GetWindowWidth(), gMixerPanelRectMin.y + ImGui::GetWindowHeight());
         DrawMixerPanelContent();
      }
      EndFloatingPanel();
      if (!open)
         gMixerPanelOpen = false;
   }
}
