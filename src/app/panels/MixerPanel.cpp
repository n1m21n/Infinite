// Mixer panel: the console view of the patch's Mixer nodes (internal/plans/mixer-panel).
// A3 is the shell (five docks, persist, the "Enable Mixer Audio" chip). A4 is the console: page
// tabs, strips (name, trim, pan, fader | meter, M S Ã), the master strip, group strips that fold
// open, canvas-selection follow, and the empty state.
#include "app/ui/design/UiFmt.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/Keymap.h"
#include "app/ui/design/components/PanelKit.h"
#include "app/ui/design/components/ReorderDrag.h"
#include "app/ui/design/components/ColourChip.h"
#include "app/frame/CanvasLink.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PagePicker.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/UiType.h"
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/DeskParts.h"
#include "app/ui/design/UiScope.h"
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
   float gMixerPanelWidth = tok::mixer_panel_width;
   float gMixerPanelHeight = tok::mixer_panel_height;
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
   bool gMixerRenderOpen = false;
   ImVec2 gMixerRenderMin, gMixerRenderMax;

   // D4: master inserts are the effects between the top Mixer's main output and its Audio Out, so the
   // walk goes up input 0 until it meets a Mixer. A chain that starts at a send pin is a return, not a page.
   INode* MixerPageTop(INode* resolvedSource)
   {
      INode* n = resolvedSource;
      for (int hop = 0; n != nullptr && hop < 64; hop++)
      {
         if (dynamic_cast<MixerNode*>(n) != nullptr)
            return n;
         AudioCable* in = n->AudioInputSlot(0);
         if (in == nullptr || !in->IsConnected())
            return nullptr;
         INode* up = ResolvedAudioSource(in->GetSource());
         if (dynamic_cast<MixerNode*>(up) != nullptr && in->GetOutputSlot() != 0)
            return nullptr;
         n = up;
      }
      return nullptr;
   }

   bool IsMixerPageTerminal(const INode* terminal, const INode* resolvedSource)
   {
      return dynamic_cast<const AudioOutputNode*>(terminal) != nullptr &&
             MixerPageTop(const_cast<INode*>(resolvedSource)) != nullptr;
   }

   namespace
   {
      using DeskParts::Lerp;
      using DeskParts::Tint;
      using DeskParts::Hi;
      using DeskParts::Lo;
      using DeskParts::Ink;
      using DeskParts::Role;
      // Strip geometry. The fader takes whatever height is left after the fixed rows.
      constexpr float kStripW = tok::mixer_strip_w; // control width inside a strip
      constexpr float kStripFaceW = tok::mixer_strip_face_w; // a strip's full width: wide, fixed, the console scrolls
      constexpr float kStripGap = tok::space_1;
                  constexpr float kButtonsH = tok::control_h;
      constexpr float kRowGap = tok::space_1;
      constexpr float kMinFaderH = tok::mixer_min_fader_h;
      constexpr int kMaxGroupDepth = 4;
      constexpr float kSendH = tok::mixer_send_h;     // §4.2 sends row: four mini knobs between name and trim
      constexpr float kSendKnobD = tok::mixer_send_knob_d;
      constexpr int kInsertRows = tok::mixer_insert_rows;      // §4.2 inserts: chips, the last row holds "+"
      constexpr float kInsertRowH = tok::mixer_insert_row_h;

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
      bool ChainAt(int mixerIndex, int slot, Chain& ch); // defined with the insert edits below

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

      MixerNode* PageMixer(const Page& p); // defined with the page picker

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
            GraphNode* top = GraphNodeOf(MixerPageTop(resolved));
            if (top == nullptr)
               continue;
            bool dup = false;
            for (const Page& p : pages)
               dup = dup || p.topIndex == top->index;
            if (!dup)
               pages.push_back({ gn.index, top->index, top->uid });
         }
         // A Mixer nothing else takes in (no Audio Out yet, not a group of another Mixer) is a page too: a new
         // page is just a Mixer, wired wherever the owner wants it to sound.
         for (GraphNode& gn : gNodes)
         {
            if (dynamic_cast<MixerNode*>(gn.node.get()) == nullptr)
               continue;
            bool dup = false;
            for (const Page& p : pages)
               dup = dup || p.topIndex == gn.index;
            bool fedOn = false;
            for (GraphNode& other : gNodes)
               for (int k = 0; !fedOn && other.node && k < 64; k++)
               {
                  AudioCable* c = other.node->AudioInputSlot(k);
                  if (c == nullptr)
                     break;
                  fedOn = c->IsConnected() && ResolvedAudioSource(c->GetSource()) == gn.node.get();
               }
            if (!dup && !fedOn)
               pages.push_back({ -1, gn.index, gn.uid });
         }
         // The page strip's own order (Move Left/Right, drag); ties keep canvas order.
         std::stable_sort(pages.begin(), pages.end(), [](const Page& a, const Page& b) {
            const MixerNode* ma = PageMixer(a);
            const MixerNode* mb = PageMixer(b);
            return (ma != nullptr ? ma->pageOrder : 0) < (mb != nullptr ? mb->pageOrder : 0);
         });
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

      ImU32 CategoryColorOf(const GraphNode* gn)
      {
         if (gn == nullptr)
            return ImGui::GetColorU32(ImGuiCol_Border);
         const CategoryColors::Color& c = CategoryColors::ColorFor(gn->category);
         return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, 1.0f));
      }

      constexpr unsigned kPalette[12] = { 0xE5484D, 0xF76B15, 0xFFB224, 0xE5D52B, 0x8BD450, 0x30A46C,
                                          0x12A594, 0x05A2C2, 0x3E63DD, 0x8E4EC6, 0xD6409F, 0x889096 };

      // Rename: one strip at a time, double-click the name or right-click > Rename.
      uint64_t sEditUid = 0;
      int sEditSlot = -1;
      char sEditBuf[40] = {};
      bool sEditFocus = false;

      void BeginRename(const MixerNode& m, uint64_t uid, int slot, const std::string& shown)
      {
         sEditUid = uid;
         sEditSlot = slot;
         sEditFocus = true;
         const std::string cur = m.chName[slot].empty() ? shown : m.chName[slot];
         snprintf(sEditBuf, sizeof(sEditBuf), "%s", cur.c_str());
      }

      // The name's right-click menu and drag-to-reorder. Returns true when the click should not select the node.
      bool StripNameActions(ImDrawList* dl, ImVec2 p, float w, const char* name, const StripRef& r, bool clicked)
      {
         GraphNode* mGn = FindNodeByIndex(r.mixerIndex);
         MixerNode* m = MixerAt(r.mixerIndex);
         if (mGn == nullptr || m == nullptr)
            return false;
         (void)dl; (void)p; (void)w; (void)clicked;
         bool picked = false;
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            BeginRename(*m, mGn->uid, r.slot, name);
            picked = true;
         }
         {
            const int payload[2] = { r.mixerIndex, r.slot };
            ReorderDrag::Source("MIXER_STRIP", payload, sizeof(payload), name);
            const ImGuiPayload* pk = ImGui::GetDragDropPayload();
            const int from = pk != nullptr && pk->IsDataType("MIXER_STRIP") ? static_cast<const int*>(pk->Data)[1] : r.slot;
            if (const ImGuiPayload* pl = ReorderDrag::Target("MIXER_STRIP", ReorderDrag::SlotEdge(from, r.slot), false))
            {
               const int* d = static_cast<const int*>(pl->Data);
               if (d[0] == r.mixerIndex && d[1] != r.slot)
                  MixerPanelStripMove(r.mixerIndex, d[1], r.slot);
            }
         }
         if (MenuParts::BeginContextItem("##stripctx"))
         {
            const int n = m->numChannels;
            if (MenuParts::Item(T("Rename")))
               BeginRename(*m, mGn->uid, r.slot, name);
            if (MenuParts::Item(T("Reset name"), nullptr, false, !m->chName[r.slot].empty()))
            {
               PushUndoCheckpoint("Rename channel");
               m->chName[r.slot].clear();
            }
            if (MenuParts::SubMenu(T("Colour")))
            {
               ImU32 dots[12];
               int cur = -1;
               for (int i = 0; i < 12; i++)
               {
                  const unsigned c = kPalette[i];
                  dots[i] = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, tok::mixer_strip_name_actions_alpha_hex);  // ui-audit-ok: mask (channel mask / bit packing, not a colour value)
                  if (m->chColor[r.slot] == c)
                     cur = i;
               }
               if (const int pick = ColourChip::Dots(dots, nullptr, 12, cur, 6); pick >= 0)
               {
                  PushUndoCheckpoint("Channel colour");
                  m->chColor[r.slot] = kPalette[pick];
                  ImGui::CloseCurrentPopup();
               }
               if (MenuParts::Item(T("Auto"), nullptr, m->chColor[r.slot] == 0))
               {
                  PushUndoCheckpoint("Channel colour");
                  m->chColor[r.slot] = 0;
               }
               ImGui::EndMenu();
            }
            MenuParts::Separator();
            if (MenuParts::Item(T("Solo safe"), nullptr, m->soloSafe[r.slot]))
            {
               PushUndoCheckpoint("Mixer strip");
               m->soloSafe[r.slot] = !m->soloSafe[r.slot];
            }
            if (m->SendBuses() > 0 && MenuParts::Item(T("Pre-fader sends"), nullptr, m->sendPre[r.slot]))
            {
               PushUndoCheckpoint("Mixer strip");
               m->sendPre[r.slot] = !m->sendPre[r.slot];
            }
            if (MenuParts::Item(T("Reset strip")))
            {
               PushUndoCheckpoint("Mixer strip");
               m->gainDb[r.slot] = 0.0f;
               m->trimDb[r.slot] = 0.0f;
               m->pan[r.slot] = 0.0f;
               m->mute[r.slot] = m->solo[r.slot] = m->phase[r.slot] = false;
            }
            MenuParts::Separator();
            if (MenuParts::Item(T("Move Left"), nullptr, false, r.slot > 0))
               MixerPanelStripMove(r.mixerIndex, r.slot, r.slot - 1);
            if (MenuParts::Item(T("Move Right"), nullptr, false, r.slot < n - 1))
               MixerPanelStripMove(r.mixerIndex, r.slot, r.slot + 1);
            MenuParts::EndPopup();
         }
         return picked;
      }


      // §4.2: one mini knob per send bus of this strip's Mixer (right-click: pre/post), then "+" for a new send FX.
      [[maybe_unused]] void DrawSends(ImVec2 p, MixerNode* m, const StripRef& r)
      {
         const int s = r.slot;
         const float cell = (kStripW - tok::space_0_5) / (float)MixerNode::kSendBuses;
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
            if (MenuParts::ContextClicked())
            {
               PushUndoCheckpoint("Mixer strip");
               m->sendPre[s] = !m->sendPre[s];
            }
            if (ImGui::IsItemHovered())
            {
               if (m->sendDb[b][s] <= MixerNode::kSendOffDb)
                  HelpTip("%s %d  %s  %s", T("Send"), b + 1, T("off"), m->sendPre[s] ? T("pre") : T("post"));
               else
                  HelpTip(T("%s %d  %s  %s"), T("Send"), b + 1, UiFmt::Tr(UiFmt::DbOffset(m->sendDb[b][s])).c, m->sendPre[s] ? T("pre") : T("post"));
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
      [[maybe_unused]] bool InsertMenuItems(const StripRef& r, int i)
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
      [[maybe_unused]] bool DrawInsertChip(ImDrawList* dl, ImVec2 p, float w, const StripRef& r, int i)
      {
         GraphNode* g = FindNodeByIndex(r.inserts[i]);
         if (g == nullptr)
            return false;
         bool changed = false;
         ImGui::PushID(i);
         const bool off = g->node->bypassed;
         const ImVec2 q(p.x + w, p.y + kInsertRowH);
         const bool lit = sHighlightUid != 0 && g->uid == sHighlightUid;
         dl->AddRectFilled(p, q, ImGui::GetColorU32(ImGuiCol_FrameBg), tok::mixer_insert_chip_radius);
         if (lit)
            dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), tok::mixer_insert_chip_radius);
         const ImVec2 dot(p.x + tok::space_1_5, p.y + kInsertRowH * 0.5f);
         if (off)
            dl->AddCircle(dot, tok::mixer_insert_chip_r, ImGui::GetColorU32(ImGuiCol_TextDisabled), tok::mixer_insert_chip_segments, 1.0f);
         else
            dl->AddCircleFilled(dot, tok::mixer_insert_chip_r, ImGui::GetColorU32(ImGuiCol_CheckMark), tok::mixer_insert_chip_segments);
         ImGui::SetCursorScreenPos(p);
         if (UiInteract::Hit("##byp", ImVec2(tok::mixer_insert_chip_hit_x, kInsertRowH)))
            changed = MixerPanelInsertBypass(r.mixerIndex, r.slot, i);
         if (ImGui::IsItemHovered())
            HelpTip("%s", off ? T("Enable") : T("Bypass"));
         const std::string name = NodeTitle(*g);
         ImGui::SetCursorScreenPos(ImVec2(p.x + tok::space_3, p.y));
         if (UiInteract::Hit("##chip", ImVec2(std::max(1.0f, w - tok::space_3), kInsertRowH)))
            SelectOnCanvas(g->index);
         if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            HelpTip("%s", name.c_str());
         if (!changed)
         {
            const int payload[3] = { r.mixerIndex, r.slot, i };
            ReorderDrag::Source("MIXER_INSERT", payload, sizeof(payload), name.c_str(), ImGuiDragDropFlags_SourceAllowNullID);
            const ImGuiPayload* pk = ImGui::GetDragDropPayload();
            const int from = pk != nullptr && pk->IsDataType("MIXER_INSERT") ? static_cast<const int*>(pk->Data)[2] : i;
            if (const ImGuiPayload* pl = ReorderDrag::Target("MIXER_INSERT", ReorderDrag::SlotEdge(from, i)))
            {
               const int* d = static_cast<const int*>(pl->Data);
               if (d[0] == r.mixerIndex && d[1] == r.slot && d[2] != i)
                  changed = MixerPanelInsertMove(r.mixerIndex, r.slot, d[2], i);
            }
         }
         if (!changed && MenuParts::BeginContextItem("##insctx"))
         {
            changed = InsertMenuItems(r, i);
            MenuParts::EndPopup();
         }
         dl->PushClipRect(ImVec2(p.x + tok::mixer_insert_chip_gap, p.y), ImVec2(q.x - tok::space_0_5, q.y), true);
         dl->AddText(ImGui::GetFont(), UiType::Px(UiType::Size::Caption), ImVec2(p.x + tok::space_3, p.y + 1.0f),
                     ImGui::GetColorU32(off ? ImGuiCol_TextDisabled : ImGuiCol_Text), name.c_str());
         dl->PopClipRect();
         ImGui::PopID();
         return changed;
      }

      // §4.2 / D4: the strip's insert chain as chips, then "+". A shared chain is edited on the canvas.
      [[maybe_unused]] void DrawInserts(ImDrawList* dl, ImVec2 p, const StripRef& r, int rows = kInsertRows)
      {
         const float w = kStripW - tok::space_0_5;
         const float x = p.x + 1.0f;
         if (r.shared)
         {
            ImGui::SetCursorScreenPos(ImVec2(x, p.y));
            UiInteract::Hit("##shared", ImVec2(w, kInsertRowH));
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Shared chain: edit it on the canvas"));
            dl->AddText(ImGui::GetFont(), UiType::Px(UiType::Size::Caption), ImVec2(x + tok::space_0_75, p.y + 1.0f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), T("Shared"));
            return;
         }
         const int n = (int)r.inserts.size();
         const bool overflow = n > rows - 1;
         const int shown = overflow ? rows - 1 : n;
         for (int i = 0; i < shown; i++)
            if (DrawInsertChip(dl, ImVec2(x, p.y + (float)i * (kInsertRowH + 1.0f)), w, r, i))
               return;
         const float ry = p.y + (float)shown * (kInsertRowH + 1.0f);
         float plusX = x, plusW = w;
         if (overflow)
         {
            plusW = tok::mixer_plus_w;
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


      // ---- Hardware desk look -------------------------------------------------------------------------------
      // References: SSL 4000 G (colour cap, scribble name, trim/pan pots, M/S keys with LEDs, long-throw fader with
      // an LED ladder beside it), Yamaha CL/QL and Allen & Heath SQ (a colour per channel, a readout under the
      // fader). Tones are the desk_* roles (DeskParts): graphite in dark mode, a light desk in light mode.
      constexpr float kCardPad = tok::space_2;
      constexpr float kContentMaxW = tok::mixer_content_max_w;
      constexpr float kMinStripW = tok::mixer_min_strip_w;
      constexpr float kScribbleH = tok::mixer_scribble_h;
      using DeskParts::kPotD;
      using DeskParts::kPotRowH;
      using DeskParts::kPlateH;

      ImU32 RgbToU32(unsigned rgb) { return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, tok::mixer_strip_name_actions_alpha_u8); }  // ui-audit-ok: mask (channel mask / bit packing, not a colour value)


      float StripFaderH(float h, bool sends)
      {
         return std::max(kMinFaderH, h - tok::mixer_strip_fader_h_ratio * kCardPad - kScribbleH - kPotRowH - (sends ? kSendH + tok::space_0_5 : 0.0f) - kButtonsH -
                                         tok::mixer_strip_fader_h_ratio * kPlateH - tok::mixer_strip_fader_h_min * kRowGap);
      }

      void DrawScrew(ImDrawList* dl, ImVec2 c) { DeskParts::Screw(dl, c); }

      // The console chassis behind every strip: a dark steel bed with a lit top rail, so the strips sit in it.
      void DrawChassis(ImDrawList* dl, ImVec2 p, ImVec2 q) { DeskParts::Chassis(dl, p, q); }

      // A strip face: graphite plate, fine vertical brushing (a fixed hash, so nothing swims), a bevelled edge and a
      // colour cap on top. `lit` is the canvas selection.
      void DrawFace(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 cap, bool lit) { DeskParts::Face(dl, p, q, cap, lit); }

      // A rotary pot: knurled skirt, domed cap, a pointer, and an arc in the strip colour for the value. Drag up or
      // down; Shift is fine; double-click resets. Returns true while the value changes.
      bool Pot(const char* id, float* v, float lo, float hi, float def, bool bipolar, float cellW, ImU32 col, const char* tag)
      {
         const ImVec2 p = ImGui::GetCursorScreenPos();
         UiInteract::Hit(id, ImVec2(cellW, kPotRowH));
         const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
         bool changed = false;
         if (active && ImGui::GetIO().MouseDelta.y != 0.0f)
         {
            const float speed = ImGui::GetIO().KeyShift ? 0.002f : 0.007f;
            const float nv = std::clamp(*v - ImGui::GetIO().MouseDelta.y * speed * (hi - lo), lo, hi);
            changed = nv != *v;
            *v = nv;
         }
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            PushUndoCheckpoint("Mixer strip");
            *v = def;
            changed = true;
         }
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float R = kPotD * 0.5f;
         const ImVec2 c(p.x + cellW * 0.5f, p.y + R + 1.0f);
         const float a0 = tok::mixer_a0_ratio * IM_PI, a1 = tok::mixer_a1_ratio * IM_PI; // 270 degrees, gap at the bottom
         const float t = hi > lo ? (*v - lo) / (hi - lo) : 0.0f;
         const float ang = Lerp(a0, a1, t);
         // value arc, from the left stop (unipolar) or from the centre (bipolar)
         const float from = bipolar ? Lerp(a0, a1, 0.5f) : a0;   // ui-audit-ok: model (midpoint of a bipolar range)
         dl->PathArcTo(c, R + tok::mixer_pot_gap, a0, a1, tok::mixer_pot_segments);
         dl->PathStroke(Lo(tok::mixer_pot_lo_alpha_md), 0, tok::mixer_pot_stroke);
         if (std::fabs(ang - from) > 0.01f)
         {
            dl->PathArcTo(c, R + tok::mixer_pot_gap, std::min(from, ang), std::max(from, ang), tok::mixer_pot_segments);
            dl->PathStroke(col, 0, tok::mixer_pot_stroke);
         }
         dl->AddCircleFilled(ImVec2(c.x, c.y + tok::mixer_dot_y), R, Lo(tok::mixer_pot_lo_alpha_sm), tok::mixer_dot_segments_lg);        // drop shadow
         dl->AddCircleFilled(c, R, DeskParts::Role(tok::desk_knurl), tok::mixer_dot_segments_lg);                                   // skirt
         for (int i = 0; i < 24; i++)                                               // knurl
         {
            const float a = (float)i / tok::mixer_a_ratio * tok::mixer_pot_scale * IM_PI;
            dl->AddLine(ImVec2(c.x + cosf(a) * (R - tok::mixer_pot_inset_lg), c.y + sinf(a) * (R - tok::mixer_pot_inset_lg)), ImVec2(c.x + cosf(a) * (R - tok::mixer_pot_inset_sm), c.y + sinf(a) * (R - tok::mixer_pot_inset_sm)),
                        Hi(hovered || active ? tok::mixer_pot_hi_alpha_lg : tok::mixer_pot_hi_alpha_sm), tok::mixer_line_stroke_sm);
         }
         const float cr = R - 4.0f;
         const ImU32 pot = tok::U32(tok::desk_pot);
         dl->AddCircleFilled(c, cr, pot, tok::mixer_pot_segments);                                       // cap
         dl->AddCircleFilled(ImVec2(c.x - 1.0f, c.y - tok::mixer_dot_y), cr - tok::mixer_dot_y, Tint(pot, tok::mixer_dot_alpha_sm), tok::mixer_pot_segments);
         dl->AddCircleFilled(ImVec2(c.x - tok::mixer_dot_y, c.y - tok::mixer_pot_gap), cr - tok::space_1, Tint(pot, tok::mixer_dot_alpha_lg), tok::mixer_dot_segments_sm);
         dl->AddCircle(c, cr, Lo(tok::mixer_pot_lo_alpha_lg), tok::mixer_pot_segments, 1.0f);
         dl->AddLine(ImVec2(c.x + cosf(ang) * tok::mixer_pot_ratio, c.y + sinf(ang) * tok::mixer_pot_ratio), ImVec2(c.x + cosf(ang) * (cr - tok::mixer_pot_inset_sm), c.y + sinf(ang) * (cr - tok::mixer_pot_inset_sm)),
                     col, tok::mixer_line_stroke_lg);
         const float fs = UiType::Px(UiType::Size::Caption);
         const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, tag).x;
         dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (cellW - tw) * 0.5f, p.y + kPotD + tok::space_0_75), Ink(tok::mixer_text_shade_md), tag);
         return changed;
      }

      // A desk key: a raised cap with an LED bar above the legend, lit when on. Returns true on click.
      bool Key(const char* id, const char* legend, bool on, ImU32 led, ImVec2 size) { return DeskParts::Key(id, legend, on, led, size); }

      // A long-throw fader: engraved scale left of a dark slot, a metal cap with grips and a white index line.
      // The ladder meter takes the right-hand side. Drag follows the cap; double-click returns to 0 dB.
      void DeskFader(ImVec2 p, float w, float h, float* db, float level, ImU32 col)
      {
         const float meterW = tok::mixer_meter_w, scaleW = tok::mixer_scale_w;
         const float slotX = p.x + scaleW + (w - scaleW - meterW - tok::space_1) * 0.5f;
         const float capW = tok::mixer_cap_w, capH = tok::mixer_cap_h;
         const float top = p.y + capH * 0.5f, bottom = p.y + h - capH * 0.5f;
         auto pos01 = [&](float d) { return std::clamp(ConsoleFaderTaper::ValueToPos(d, -60.0f, 12.0f), 0.0f, 1.0f); };
         ImGui::SetCursorScreenPos(ImVec2(slotX - capW * 0.5f, p.y));
         const float before = *db;
         UiInteract::Hit("##fader", ImVec2(capW, h));
         const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
         UndoOnActivate(db, before);
         if (active && ImGui::GetIO().MouseDelta.y != 0.0f)
         {
            const float next = pos01(*db) - ImGui::GetIO().MouseDelta.y * (ImGui::GetIO().KeyShift ? 0.25f : 1.0f) / (bottom - top);
            *db = std::clamp(ConsoleFaderTaper::PosToValue(std::clamp(next, 0.0f, 1.0f), -60.0f, 12.0f), -60.0f, 12.0f);
         }
         if (hovered)
         {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               PushUndoCheckpoint("Mixer strip");
               *db = 0.0f;
            }
            if (*db <= -60.0f)
               HelpTip("%s", T("-inf dB"));
            else
               HelpTip("%s", UiFmt::Tr(UiFmt::DbOffset(*db)).c);
         }
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float capY = bottom - pos01(*db) * (bottom - top);
         // scale
         static const float kMarks[] = { 12.0f, 0.0f, -10.0f, -20.0f, -30.0f, -60.0f };
         static const char* kLabels[] = { "+12", "0", "10", "20", "30", "-60" };
         const float fs = UiType::Px(UiType::Size::Caption);
         for (int i = 0; i < 6; i++)
         {
            const float y = bottom - pos01(kMarks[i]) * (bottom - top);
            const bool zero = kMarks[i] == 0.0f;
            dl->AddLine(ImVec2(p.x + scaleW - tok::space_1_25, y), ImVec2(p.x + scaleW - tok::space_0_25, y), Ink(zero ? tok::mixer_line_shade_lg : tok::mixer_line_shade_md), tok::mixer_desk_fader_stroke);
            const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, kLabels[i]).x;
            dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + scaleW - tok::mixer_desk_fader_gap - tw, y - fs * 0.5f), Ink(zero ? tok::mixer_text_shade_lg : tok::mixer_text_shade_sm), kLabels[i]);
         }
         // slot, with a colour fill below the cap
         dl->AddRectFilled(ImVec2(slotX - tok::space_0_75, top - tok::space_1), ImVec2(slotX + tok::space_0_75, bottom + tok::space_1), Lo(tok::mixer_desk_fader_lo_alpha_lg), tok::mixer_desk_fader_radius);
         dl->AddRect(ImVec2(slotX - tok::space_0_75, top - tok::space_1), ImVec2(slotX + tok::space_0_75, bottom + tok::space_1), Hi(tok::mixer_desk_fader_hi_alpha_xs), tok::mixer_desk_fader_radius);
         dl->AddRectFilled(ImVec2(slotX - 1.0f, capY), ImVec2(slotX + 1.0f, bottom + tok::space_0_75), Tint(col, tok::mixer_fill_alpha_md, tok::mixer_fill_alpha_sm_u8), 1.0f);
         const float y0 = bottom - pos01(0.0f) * (bottom - top);
         dl->AddLine(ImVec2(slotX - tok::space_1_5, y0), ImVec2(slotX + tok::space_1_5, y0), Ink(tok::mixer_line_shade_sm), tok::mixer_desk_fader_stroke);
         // cap
         const ImVec2 a(slotX - capW * 0.5f, capY - capH * 0.5f), b(slotX + capW * 0.5f, capY + capH * 0.5f);
         dl->AddRectFilled(ImVec2(a.x, a.y + tok::mixer_fill_y), ImVec2(b.x, b.y + tok::mixer_fill_y), Lo(tok::mixer_desk_fader_lo_alpha_sm), tok::mixer_desk_fader_radius);
         const ImU32 metal = tok::U32(tok::desk_metal), capTop = Tint(metal, active ? tok::mixer_metal_alpha_lg : hovered ? tok::mixer_metal_alpha_sm : 1.0f);
         dl->AddRectFilledMultiColor(a, b, capTop, capTop, Tint(metal, tok::mixer_fill_alpha_sm), Tint(metal, tok::mixer_fill_alpha_sm));
         dl->AddRect(a, b, Lo(tok::mixer_desk_fader_lo_alpha_md), tok::mixer_desk_fader_radius, 0, tok::mixer_desk_fader_stroke);
         dl->AddLine(ImVec2(a.x + tok::mixer_line_x, a.y + tok::mixer_line_x), ImVec2(b.x - tok::mixer_line_x, a.y + tok::mixer_line_x), Hi(tok::mixer_desk_fader_hi_alpha_md), tok::mixer_desk_fader_stroke);
         for (int i = -2; i <= 2; i++) // grip ridges
         {
            const float gy = capY + (float)i * 3.0f + (i > 0 ? 1.5f : -1.5f) * 0.0f;
            if (i == 0)
               continue;
            dl->AddLine(ImVec2(a.x + tok::space_0_75, gy), ImVec2(b.x - tok::space_0_75, gy), Lo(tok::mixer_desk_fader_lo_alpha_xs), tok::mixer_desk_fader_stroke);
            dl->AddLine(ImVec2(a.x + tok::space_0_75, gy + tok::space_0_25), ImVec2(b.x - tok::space_0_75, gy + tok::space_0_25), Hi(tok::mixer_desk_fader_hi_alpha_sm), tok::mixer_desk_fader_stroke);
         }
         dl->AddLine(ImVec2(a.x + tok::space_0_5, capY), ImVec2(b.x - tok::space_0_5, capY), Hi(tok::mixer_desk_fader_hi_alpha_lg), tok::mixer_line_stroke_md);
         // meter
         ImGui::PushID("mtr");
         DeskParts::ConsoleMeter(p.x + w - meterW, p.y, meterW, h, level, true);
         ImGui::PopID();
      }

      // The readout plate under a fader: dark glass with the value in console green.
      void Plate(ImDrawList* dl, ImVec2 p, float w, const char* text, ImU32 ink) { DeskParts::Plate(dl, p, w, text, ink); }

      // The scribble strip: the channel number and name on a dark inset under the colour cap. Click selects the
      // source on the canvas; double-click renames; right-click opens the strip menu. Returns true on a plain click.
      bool Scribble(ImDrawList* dl, ImVec2 p, float w, const char* name, int number, ImU32 col, bool dim, const StripRef* strip)
      {
         const ImVec2 q(p.x + w, p.y + kScribbleH);
         dl->AddRectFilled(p, q, DeskParts::Role(tok::desk_scribble_bg), tok::mixer_scribble_radius);
         dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + tok::space_1_25), Tint(col, tok::mixer_fill_alpha_lg, tok::mixer_fill_alpha_lg_u8), tok::mixer_fill_radius, ImDrawFlags_RoundCornersTop);
         dl->AddRect(p, q, Ink(tok::mixer_outline_shade_sm), tok::mixer_scribble_radius);
         ImGui::SetCursorScreenPos(p);
         bool clicked = UiInteract::Hit("##name", ImVec2(w, kScribbleH));
         const bool hovered = ImGui::IsItemHovered();
         if (strip != nullptr && StripNameActions(dl, p, w, name, *strip, clicked))
            clicked = false;
         if (hovered)
            dl->AddRect(p, q, Ink(tok::mixer_outline_shade_lg), tok::mixer_scribble_radius);
         char num[16];
         snprintf(num, sizeof(num), "%d", number);
         const float nfs = UiType::Px(UiType::Size::Caption);
         if (number > 0)
            dl->AddText(ImGui::GetFont(), nfs, ImVec2(p.x + tok::space_1, p.y + tok::mixer_scribble_gap), Tint(col, app::IsThemeLight() ? tok::mixer_text_alpha_sm : tok::mixer_text_alpha_lg), num);
         const ImVec2 ts = ImGui::CalcTextSize(name);
         const float tx = p.x + std::max(4.0f, (w - ts.x) * 0.5f);
         dl->PushClipRect(ImVec2(p.x + tok::space_0_75, p.y), ImVec2(q.x - tok::space_0_75, q.y), true);
         dl->AddText(ImVec2(tx, p.y + tok::mixer_text_y), DeskParts::Role(tok::desk_scribble_text, dim ? tok::mixer_text_alpha_sm_u8 : tok::mixer_text_alpha_lg_u8), name);
         dl->PopClipRect();
         if (hovered && ts.x > w - tok::space_2 && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            HelpTip("%s", name);
         return clicked;
      }

      void DrawStrip(ImDrawList* dl, ImVec2 p, float w, float h, const StripRef& r)
      {
         MixerNode* m = MixerAt(r.mixerIndex);
         GraphNode* mGn = FindNodeByIndex(r.mixerIndex);
         if (m == nullptr || mGn == nullptr)
            return;
         GraphNode* src = r.srcIndex >= 0 ? FindNodeByIndex(r.srcIndex) : nullptr;
         GraphNode* grp = r.groupIndex >= 0 ? FindNodeByIndex(r.groupIndex) : nullptr;
         const int s = r.slot;
         ImGui::PushID(r.mixerIndex);
         ImGui::PushID(s);

         GraphNode* head = r.headIndex >= 0 ? FindNodeByIndex(r.headIndex) : nullptr;
         GraphNode* named = grp ? grp : (head && !r.isReturn) ? head : src;
         const ImU32 col = m->chColor[s] ? RgbToU32(m->chColor[s]) : CategoryColorOf(named);
         const bool lit = sHighlightUid != 0 && ((src && src->uid == sHighlightUid) || (grp && grp->uid == sHighlightUid));
         DrawFace(dl, p, ImVec2(p.x + w, p.y + h), col, lit);

         const float cw = std::min(kContentMaxW, w - tok::mixer_strip_ratio * kCardPad);
         const float cx = p.x + (w - cw) * 0.5f;
         float y = p.y + kCardPad + tok::space_1;

         std::string shown = named ? NodeTitle(*named) : std::string(T("Ch")) + " " + std::to_string(s + 1);
         const std::string name = m->chName[s].empty() ? shown : m->chName[s];
         const float nameW = w - tok::mixer_name_w_ratio * kCardPad;
         const float nameX = p.x + kCardPad;
         if (sEditUid == mGn->uid && sEditSlot == s)
         {
            ImGui::SetCursorScreenPos(ImVec2(nameX, y + tok::space_2_5));
            ImGui::SetNextItemWidth(nameW);
            if (sEditFocus)
            {
               UiInteract::FocusHere();
               sEditFocus = false;
            }
            const bool done = ImGui::InputText("##rename", sEditBuf, sizeof(sEditBuf),
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (done || ImGui::IsItemDeactivatedAfterEdit())
            {
               if (m->chName[s] != sEditBuf)
               {
                  PushUndoCheckpoint("Rename channel");
                  m->chName[s] = sEditBuf;
               }
               sEditUid = 0;
            }
            else if (ImGui::IsItemDeactivated() || (!ImGui::IsItemActive() && !sEditFocus && ImGui::GetFrameCount() > 0 &&
                                                    ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
               sEditUid = 0;
         }
         else if (Scribble(dl, ImVec2(nameX, y), nameW, name.c_str(), s + 1, col, src == nullptr && m->chName[s].empty(), &r) && named)
            SelectOnCanvas(named->index);
         if (sStripsStale)
         {
            ImGui::PopID();
            ImGui::PopID();
            return;
         }
         y += kScribbleH + kRowGap;

         // Trim and pan pots side by side.
         const float cell = cw * 0.5f;
         ImGui::SetCursorScreenPos(ImVec2(cx, y));
         float before = m->trimDb[s];
         Pot("##trim", &m->trimDb[s], params::Mixer::trim_db_lo, params::Mixer::trim_db_hi, 0.0f, true, cell, col, T("TRIM"));
         UndoOnActivate(&m->trimDb[s], before);
         if (ImGui::IsItemHovered())
            HelpTip(T("%s %s"), T("Trim"), UiFmt::Tr(UiFmt::DbOffset(m->trimDb[s])).c);
         ImGui::SetCursorScreenPos(ImVec2(cx + cell, y));
         before = m->pan[s];
         Pot("##pan", &m->pan[s], -1.0f, 1.0f, 0.0f, true, cell, col, T("PAN"));
         UndoOnActivate(&m->pan[s], before);
         if (ImGui::IsItemHovered())
            HelpTip("%s %s", T("Pan"), UiFmt::SF0(m->pan[s] * 100.0f).c);
         y += kPotRowH + kRowGap;

         // Aux sends, only once the console has send buses.
         const int buses = m->SendBuses();
         if (buses > 0)
         {
            DrawSends(ImVec2(cx - 1.0f, y), m, r);
            y += kSendH + tok::space_0_5 + kRowGap;
         }

         // Keys: mute, solo, polarity.
         const float bw = (cw - 2.0f * 2.0f) / 3.0f;
         ImGui::SetCursorScreenPos(ImVec2(cx, y));
         if (Key("##m", "M", m->mute[s], tok::U32(tok::desk_led_red), ImVec2(bw, kButtonsH)))
         {
            m->mute[s] = !m->mute[s];
            PanelKit::UndoOnToggle(&m->mute[s], "Mixer strip");
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Mute"));
         ImGui::SetCursorScreenPos(ImVec2(cx + bw + tok::space_0_5, y));
         if (Key("##s", "S", m->solo[s], tok::U32(tok::desk_led_yellow), ImVec2(bw, kButtonsH)))
         {
            if (ImGui::GetIO().KeyAlt) // Alt-click S clears every solo on the console
            {
               m->solo[s] = !m->solo[s];
               MixerPanelClearSolos(MixerPanelCurrentPageTop());
            }
            else
            {
               m->solo[s] = !m->solo[s];
               PanelKit::UndoOnToggle(&m->solo[s], "Mixer strip");
            }
         }
         if (MenuParts::ContextClicked())
         {
            PushUndoCheckpoint("Mixer strip");
            m->soloSafe[s] = !m->soloSafe[s];
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", m->soloSafe[s] ? T("Solo safe (right-click)") : T("Solo (Alt-click: clear all, right-click: solo safe)"));
         ImGui::SetCursorScreenPos(ImVec2(cx + tok::mixer_strip_ratio * (bw + tok::mixer_ph_inset), y));
         if (Key("##ph", "\xC3\x98", m->phase[s], tok::U32(tok::desk_led_blue), ImVec2(bw, kButtonsH)))
         {
            PushUndoCheckpoint("Mixer strip");
            m->phase[s] = !m->phase[s];
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Invert polarity"));
         y += kButtonsH + kRowGap;

         // Fader and ladder meter fill what is left.
         const float faderH = StripFaderH(h, buses > 0);
         gMixerPanelFaderH = faderH;
         DeskFader(ImVec2(cx, y), cw, faderH, &m->gainDb[s], m->ChannelLevel(s), col);
         y += faderH + kRowGap;

         char val[16];
         if (m->gainDb[s] <= -60.0f)
            snprintf(val, sizeof(val), "-inf");
         else
            snprintf(val, sizeof(val), "%s", UiFmt::SF1(m->gainDb[s]).c);
         Plate(dl, ImVec2(cx, y), cw, val, tok::U32(m->mute[s] ? tok::desk_readout_over : tok::desk_readout));

         ImGui::PopID();
         ImGui::PopID();
      }

      void DrawMaster(ImDrawList* dl, ImVec2 p, float w, float h, int topIndex)
      {
         MixerNode* m = MixerAt(topIndex);
         GraphNode* gn = FindNodeByIndex(topIndex);
         if (m == nullptr || gn == nullptr)
            return;
         ImGui::PushID("master");
         const ImU32 cap = ImGui::GetColorU32(ImGuiCol_CheckMark);
         DrawFace(dl, p, ImVec2(p.x + w, p.y + h), cap, false);
         DrawScrew(dl, ImVec2(p.x + tok::space_1_5, p.y + tok::space_3));
         DrawScrew(dl, ImVec2(p.x + w - tok::space_1_5, p.y + tok::space_3));
         DrawScrew(dl, ImVec2(p.x + tok::space_1_5, p.y + h - tok::space_1_5));
         DrawScrew(dl, ImVec2(p.x + w - tok::space_1_5, p.y + h - tok::space_1_5));
         const float cw = std::min(kContentMaxW, w - tok::mixer_master_ratio * kCardPad - tok::space_1_5);
         const float cx = p.x + (w - cw) * 0.5f;
         float y = p.y + kCardPad + tok::space_1;
         if (Scribble(dl, ImVec2(p.x + kCardPad, y), w - tok::mixer_master_ratio * kCardPad, T("Master"), 0, cap, false, nullptr))
            SelectOnCanvas(topIndex);
         y += kScribbleH + kRowGap;

         // True peak and LUFS M / S / I on a dark display. Asking each frame keeps the meter running.
         m->RequestMeters();
         {
            const float dispH = kPotRowH;
            const ImVec2 dp(cx, y);
            dl->AddRectFilled(dp, ImVec2(dp.x + cw, dp.y + dispH), DeskParts::Role(tok::desk_well), tok::radius_cell);
            dl->AddRect(dp, ImVec2(dp.x + cw, dp.y + dispH), Hi(tok::mixer_master_hi_alpha_sm), tok::radius_cell);
            ImGui::SetCursorScreenPos(dp);
            if (UiInteract::Hit("##loud", ImVec2(cw, dispH)))
               m->ResetMeters();
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("True peak (dBTP) and loudness (LUFS): momentary, short-term, integrated. Click to reset."));
            const char* tags[4] = { "TP", "M", "S", "I" };
            const float vals[4] = { m->TruePeakDb(), m->LoudnessMomentary(), m->LoudnessShort(), m->LoudnessIntegrated() };
            const float fs = UiType::Px(UiType::Size::Caption);
            const float rowH = dispH * 0.5f;
            for (int i = 0; i < 4; i++)
            {
               char txt[16];
               if (vals[i] <= -99.0f)
                  snprintf(txt, sizeof(txt), "-inf");
               else
                  snprintf(txt, sizeof(txt), "%s", UiFmt::F1(vals[i]).c);
               const float colX = dp.x + (i % tok::mixer_col_x_period) * (cw * 0.5f);
               const float ry = dp.y + (i / 2) * rowH + (rowH - fs) * 0.5f;
               const bool over = i == 0 && vals[0] > -1.0f; // above -1 dBTP: likely to clip once encoded
               dl->AddText(ImGui::GetFont(), fs, ImVec2(colX + tok::space_0_75, ry), Hi(tok::mixer_master_hi_alpha_lg), tags[i]);
               const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, txt).x;
               dl->AddText(ImGui::GetFont(), fs, ImVec2(colX + cw * 0.5f - tok::space_0_75 - tw, ry),
                           tok::U32(over ? tok::desk_readout_over : tok::desk_readout), txt);
            }
         }
         y += kPotRowH + kRowGap;

         // Mono check and dim, monitor-only (AudioEngine::ApplyMonitor); never in a render or capture.
         {
            AudioEngine& eng = AudioEngine::Instance();
            const bool mono = eng.MonitorMono(), dim = eng.MonitorDim();
            const float bw = (cw - 2.0f) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(cx, y));
            if (Key("##mono", T("Mono"), mono, tok::U32(tok::desk_led_blue), ImVec2(bw, kButtonsH)))
               eng.SetMonitor(!mono, dim);
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Mono check. Speakers only: never in a render."));
            ImGui::SetCursorScreenPos(ImVec2(cx + bw + tok::space_0_5, y));
            if (Key("##dim", T("Dim"), dim, tok::U32(tok::desk_led_yellow), ImVec2(bw, kButtonsH)))
               eng.SetMonitor(mono, !dim);
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Dim -20 dB. Speakers only: never in a render."));
         }
         y += kButtonsH + kRowGap;

         const float faderH = StripFaderH(h, false);
         DeskFader(ImVec2(cx, y), cw, faderH, &m->masterDb, m->Level(), cap);
         y += faderH + kRowGap;
         char val[16];
         if (m->masterDb <= -60.0f)
            snprintf(val, sizeof(val), "-inf");
         else
            snprintf(val, sizeof(val), "%s", UiFmt::SF1(m->masterDb).c);
         Plate(dl, ImVec2(cx, y), cw, val, tok::U32(tok::desk_readout));
         ImGui::PopID();
         gMixerMasterMin = p;
         gMixerMasterMax = ImVec2(p.x + w, p.y + h);
      }

      [[maybe_unused]] int SpawnAt(const char* type, float x, float y)
      {
         GraphNode* gn = SpawnNode(type, "Utility", x, y);
         return gn ? gn->index : -1;
      }

      // Right of everything on the canvas, so a new pair never lands on a node.
      [[maybe_unused]] ImVec2 FreeCanvasSpot()
      {
         float x = 0.0f, y = 0.0f;
         bool any = false;
         for (const GraphNode& gn : gNodes)
         {
            x = any ? std::max(x, gn.liveX) : gn.liveX;
            y = any ? std::min(y, gn.liveY) : gn.liveY;
            any = true;
         }
         return any ? ImVec2(x + tok::mixer_free_canvas_spot_gap, y) : ImVec2(0.0f, 0.0f);
      }

      bool sPendingAdd = false;   // header "+" and the empty page's New Mixer land at the top of the next frame

      void DrawEmpty(ImVec2 mn, ImVec2 mx)
      {
         const bool noMixer = CountMixers() == 0;
         EmptyState::DrawTo(ImGui::GetWindowDrawList(), mn, ImVec2(mx.x, mn.y + (mx.y - mn.y) * tok::mixer_empty_ratio),
                            noMixer ? T("No Mixer yet") : T("No Mixer feeds an Audio Out"));
         const char* label = T("New Mixer");
         const float w = ImGui::CalcTextSize(label).x + tok::mixer_w_ratio * tok::space_3;
         ImGui::SetCursorScreenPos(ImVec2((mn.x + mx.x - w) * 0.5f, mn.y + (mx.y - mn.y) * tok::mixer_empty_ratio));
         if (ActionButton::Draw(label, ImVec2(w, 0.0f), ActionButton::Kind::Primary))
            sPendingAdd = true;
      }

      uint64_t sPendingDelete = 0;

      // Page adds and deletes from the header land at the top of the next frame.
      void ApplyPendingPageEdits()
      {
         if (sPendingAdd)
         {
            sPendingAdd = false;
            MixerPanelAddPage();
         }
         if (sPendingDelete != 0)
         {
            GraphNode* gn = FindNodeByUid(sPendingDelete);
            sPendingDelete = 0;
            if (gn != nullptr)
            {
               PushUndoCheckpoint("Delete Mixer Page");
               sPageUid = 0;
               RemoveNodeByIndex(gn->index);
               RebuildAudioTopology();
            }
         }
      }

      MixerNode* PageMixer(const Page& p)
      {
         GraphNode* top = FindNodeByIndex(p.topIndex);
         return top != nullptr ? dynamic_cast<MixerNode*>(top->node.get()) : nullptr;
      }

      void DrawPagePicker(const std::vector<Page>& pages, int current)
      {
         std::vector<PagePicker::Entry> entries;
         for (const Page& p : pages)
         {
            GraphNode* top = FindNodeByIndex(p.topIndex);
            const MixerNode* m = PageMixer(p);
            entries.push_back({ m != nullptr && !m->pageName.empty() ? m->pageName : (top ? NodeTitle(*top) : std::string(T("Page"))),
                                m != nullptr ? m->pageColor : 0u });
         }
         const ImGuiIO& io = ImGui::GetIO();
         if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !io.WantTextInput && (io.KeySuper || io.KeyCtrl) && !io.KeyShift &&
             Keymap::KeyPressed(Keymap::Act::Rename) && current >= 0)
            PagePicker::BeginRename("##mixerPage", entries, current);
         PagePicker::Options opt;
         const PagePicker::Result r = PagePicker::Draw("##mixerPage", entries, current, T("New mixer page"), opt);
         const Page* pg = r.index >= 0 && r.index < (int)pages.size() ? &pages[r.index] : nullptr;
         MixerNode* m = pg != nullptr ? PageMixer(*pg) : nullptr;
         switch (r.act)
         {
         case PagePicker::Act::Select: sPageUid = pg->topUid; break;
         case PagePicker::Act::Add: sPendingAdd = true; break;
         case PagePicker::Act::Rename:
            if (m != nullptr)
            {
               PushUndoCheckpoint("Rename Mixer Page");
               m->pageName = r.name;
            }
            break;
         case PagePicker::Act::Recolor:
            if (m != nullptr)
            {
               PushUndoCheckpoint("Colour Mixer Page");
               m->pageColor = r.color;
            }
            break;
         case PagePicker::Act::Delete:
            if (pg != nullptr)
            {
               // The page is its top Mixer; the sources and the Audio Out stay on the canvas. Next frame, so
               // nothing drawn after the header this frame holds a removed node.
               sPendingDelete = pg->topUid;
            }
            break;
         case PagePicker::Act::Move:
         {
            std::vector<Page> order = pages;
            const Page moving = order[r.index];
            order.erase(order.begin() + r.index);
            order.insert(order.begin() + r.to, moving);
            PushUndoCheckpoint("Reorder Mixer Pages");
            for (size_t i = 0; i < order.size(); i++)
               if (MixerNode* om = PageMixer(order[i]))
                  om->pageOrder = (int)i;
            break;
         }
         case PagePicker::Act::Duplicate:
            if (GraphNode* top = pg != nullptr ? FindNodeByIndex(pg->topIndex) : nullptr)
            {
               // The canvas duplicate (Shift+D) on this page's Mixer and everything from it to its output: a page is
               // a Mixer that reaches an output, so a copy of the Mixer alone would not be a page.
               if (gEditor != nullptr)
               {
                  ed::EditorContext* prev = ed::GetCurrentEditor();
                  ed::SetCurrentEditor(gEditor);
                  ed::ClearSelection();
                  GraphNode* at = pg->outIndex >= 0 ? FindNodeByIndex(pg->outIndex) : top;
                  for (int guard = 0; at != nullptr && guard < 64; guard++)
                  {
                     ed::SelectNode(at->NodeId(), true);
                     if (at == top)
                        break;
                     AudioCable* in = at->node ? at->node->AudioInputSlot(0) : nullptr;
                     at = in != nullptr && in->IsConnected() ? GraphNodeOf(in->GetSource()) : nullptr;
                  }
                  ed::SelectNode(top->NodeId(), true);
                  ed::SetCurrentEditor(prev);
                  sLastSelectedUid = sHighlightUid = top->uid;
                  gRequestDuplicate = true;
               }
            }
            break;
         default: break;
         }
      }

      void DrawMixerPanelContent()
      {
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 avail = ImGui::GetContentRegionAvail();
         ApplyPendingPageEdits();
         const std::vector<Page> pages = CollectPages();
         FollowCanvasSelection(pages);
         const int current = CurrentPage(pages);

         // Header, as the other panels: the page dropdown left (no title: the rail icon names the panel). The console
         // has no audio engine or render of its own: it sounds and renders through the canvas like any node.
         DrawPagePicker(pages, current);

         const ImVec2 bodyMin(origin.x, ImGui::GetCursorScreenPos().y + tok::space_2);
         ImVec2 bodyMax(origin.x + avail.x, origin.y + avail.y);
         gMixerPanelStripCount = 0;
         gMixerRenderMin = gMixerRenderMax = ImVec2(0.0f, 0.0f);
         if (current < 0)
         {
            if (bodyMax.y > bodyMin.y)
               DrawEmpty(bodyMin, bodyMax);
         }
         else if (bodyMax.y - bodyMin.y > tok::mixer_panel_content_limit)
         {
            const Page& page = pages[current];
            // A fixed console: one strip per channel of the top Mixer, in slot order, no groups, no returns row.
            std::vector<StripRef> strips = Strips(page.topIndex, false);
            strips.erase(std::remove_if(strips.begin(), strips.end(), [](const StripRef& r) { return r.depth > 0; }), strips.end());
            std::stable_sort(strips.begin(), strips.end(), [](const StripRef& a, const StripRef& b) { return a.slot < b.slot; });

            const float bodyW = bodyMax.x - bodyMin.x;
            const float bodyH = bodyMax.y - bodyMin.y;
            const int n = std::max(1, (int)strips.size());

            // Strips and the master are wide, fixed-width desk strips; a long console scrolls sideways.
            const float scrollH = ImGui::GetStyle().ScrollbarSize;
            const float stripW = kStripFaceW;
            const float stripsW = std::max(kMinStripW, bodyW - stripW - kStripGap);
            const float contentW = (float)n * stripW + (float)(n - 1) * kStripGap;
            const bool scrolls = contentW > stripsW + tok::px_align;
            const float colH = scrolls ? std::max(1.0f, bodyH - scrollH) : bodyH;
            DrawChassis(ImGui::GetWindowDrawList(), bodyMin, ImVec2(bodyMax.x, bodyMin.y + colH));

            ImGui::SetCursorScreenPos(bodyMin);
            ImGui::BeginChild("##mixerstrips", ImVec2(stripsW, bodyH), false, ImGuiWindowFlags_HorizontalScrollbar);
            {
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImVec2 base = ImGui::GetCursorScreenPos();
               const ImVec2 wmn = ImGui::GetWindowPos();
               const float visL = wmn.x, visR = wmn.x + ImGui::GetWindowWidth();
               float x = base.x;
               sStripsStale = false;
               for (const StripRef& r : strips)
               {
                  if (sStripsStale)
                     break;
                  if (x + stripW >= visL && x <= visR)
                     DrawStrip(dl, ImVec2(x, base.y), stripW, colH, r);
                  x += stripW + kStripGap;
               }
               ImGui::SetCursorScreenPos(base);
               ImGui::Dummy(ImVec2(std::max(tok::mixer_min, x - base.x - kStripGap), colH));
               gMixerStripsMin = wmn;
               gMixerStripsMax = ImVec2(wmn.x + ImGui::GetWindowWidth(), wmn.y + ImGui::GetWindowHeight());
            }
            EndChildSafe();
            gMixerPanelStripCount = (int)strips.size();
            const ImVec2 masterAt(bodyMax.x - stripW, bodyMin.y);
            DrawMaster(ImGui::GetWindowDrawList(), masterAt, stripW, colH, page.topIndex);
         }

         // Right-click empty panel space to move or close it (rect test, as the Mod Matrix does).
         const ImVec2 mouse = ImGui::GetIO().MousePos;
         const bool overPanel = mouse.x >= origin.x && mouse.x < origin.x + avail.x &&
                                mouse.y >= origin.y && mouse.y < origin.y + avail.y;
         gMixerClaimedKeys = overPanel && !ImGui::GetIO().WantTextInput;
         if (overPanel && MenuParts::RightPressed() && !ImGui::IsAnyItemHovered() &&
             !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))   // a page/strip menu opened on the click
            ImGui::OpenPopup("##mixerpanelctx");
         if (MenuParts::TestOpen("##mixerpanelctx"))
            ImGui::OpenPopup("##mixerpanelctx");
         if (MenuParts::BeginPopup("##mixerpanelctx"))
         {
            static const char* kDockLabels[] = { I18N_KEY("Mixer at Bottom"), I18N_KEY("Mixer at Top") };
            static const int kDocks[] = { 0, 3 };
            if (MenuParts::SubMenu(L("Dock Position")))
            {
               for (int i = 0; i < 2; i++)
                  if (MenuParts::Item(L(kDockLabels[i]), nullptr, kDocks[i] == gMixerPanelDock))
                     gMixerPanelDock = kDocks[i];
               ImGui::EndMenu();
            }
            MenuParts::Separator();
            if (MenuParts::Item(L("Close Mixer")))
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

   // ---- B6: snapshots A-H and solo clear. A snapshot covers the whole console: the top and every group. ----

   namespace
   {
   std::vector<MixerNode*> MixerPanelConsoleMixers(int topIndex)
   {
      std::vector<MixerNode*> out;
      if (MixerNode* top = MixerAt(topIndex))
         out.push_back(top);
      for (const StripRef& r : Strips(topIndex, true))
         if (r.groupIndex >= 0)
            if (MixerNode* g = MixerAt(r.groupIndex))
               if (std::find(out.begin(), out.end(), g) == out.end())
                  out.push_back(g);
      return out;
   }
   }

   bool MixerPanelSnapshotStore(int topIndex, int k)
   {
      const std::vector<MixerNode*> ms = MixerPanelConsoleMixers(topIndex);
      if (ms.empty() || k < 0 || k >= MixerNode::kSnapshots)
         return false;
      PushUndoCheckpoint("Store snapshot");
      for (MixerNode* m : ms)
         m->SnapshotStore(k);
      return true;
   }

   bool MixerPanelSnapshotRecall(int topIndex, int k)
   {
      const std::vector<MixerNode*> ms = MixerPanelConsoleMixers(topIndex);
      if (ms.empty() || !ms[0]->SnapshotHas(k))
         return false;
      PushUndoCheckpoint("Recall snapshot");
      const float fade = (float)ms[0]->snapFadeMs;
      for (MixerNode* m : ms)
         m->SnapshotRecall(k, fade);
      return true;
   }

   bool MixerPanelAnySolo(int topIndex)
   {
      for (MixerNode* m : MixerPanelConsoleMixers(topIndex))
         for (int s = 0; s < m->numChannels; s++)
            if (m->solo[s])
               return true;
      return false;
   }

   void MixerPanelClearSolos(int topIndex)
   {
      if (!MixerPanelAnySolo(topIndex))
         return;
      PushUndoCheckpoint("Clear solo");
      for (MixerNode* m : MixerPanelConsoleMixers(topIndex))
         for (int s = 0; s < MixerNode::kMaxSlots; s++)
            m->solo[s] = false;
   }

   void MixerPanelSelectPage(int page)
   {
      const std::vector<Page> pages = CollectPages();
      if (page >= 0 && page < (int)pages.size())
         sPageUid = pages[page].topUid;
   }

   // G8: a page is a Mixer; no Audio Out of its own (the owner wires it where it should sound). One undo step.
   int MixerPanelAddPage()
   {
      PushUndoCheckpoint("New Mixer Page");
      GraphNode* mixGn = CanvasLink::Add("Mixer", "Utility");
      const int mix = mixGn != nullptr ? mixGn->index : -1;
      if (mixGn)
         sPageUid = mixGn->uid;
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
            const int newTop = SpawnAt("Mixer", mGn->liveX + tok::mixer_panel_new_send_fx_gap, mGn->liveY);
            GraphNode* outGn = FindNodeByIndex(outIndex);
            GraphNode* topGn = FindNodeByIndex(newTop);
            mGn = FindNodeByIndex(mixerIndex);
            if (topGn == nullptr)
               break;
            auto* nt = static_cast<MixerNode*>(topGn->node.get());
            nt->numChannels = 1;
            nt->masterDb = m->masterDb; // the console's master fader moves up with the console
            m->masterDb = 0.0f;
            WireInputSlot(*mGn, *topGn, 0, 0);
            if (outGn != nullptr) // a page with no Audio Out has nothing downstream to move
            {
               DisconnectDstPin(outGn->InputPinId(0));
               WireInputSlot(*topGn, *outGn, 0, 0);
            }
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
      // The cable a chain hangs from: a Mixer slot, or for kMixerMasterSlot the Audio Out this Mixer
      // feeds (master inserts, D4).
      AudioCable* ChainCable(int mixerIndex, int slot)
      {
         MixerNode* m = MixerAt(mixerIndex);
         if (m == nullptr)
            return nullptr;
         if (slot == kMixerMasterSlot)
         {
            for (GraphNode& gn : gNodes)
            {
               auto* out = dynamic_cast<AudioOutputNode*>(gn.node.get());
               if (out != nullptr && out->input.IsConnected() && MixerPageTop(ResolvedAudioSource(out->input.GetSource())) == m)
                  return &out->input;
            }
            return nullptr;
         }
         return slot >= 0 && slot < m->numChannels ? &m->inputs[slot] : nullptr;
      }

      bool ChainAt(int mixerIndex, int slot, Chain& ch)
      {
         AudioCable* c = ChainCable(mixerIndex, slot);
         if (c == nullptr)
            return false;
         ch = WalkInserts(*c, CountConsumers());
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
         if (AudioCable* c = ChainCable(mixerIndex, slot))
         {
            if (prev != nullptr)
               c->Connect(prev, prevOut);
            else
               c->Disconnect();
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
      if (!ChainAt(mixerIndex, slot, ch) || RefuseShared(ch) || !ChainCable(mixerIndex, slot)->IsConnected())
         return -1;
      GraphNode* mGn = FindNodeByIndex(mixerIndex);
      if (mGn == nullptr)
         return -1;
      const bool master = slot == kMixerMasterSlot;
      const float x = mGn->liveX + (master ? tok::mixer_panel_insert_add_gap + tok::mixer_panel_insert_add_ratio * (float)ch.fx.size() : -tok::mixer_insert_inset);
      const float y = mGn->liveY + (master ? 0.0f : tok::mixer_insert_scale * (float)slot);
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
         std::string name;
         StripState() { std::fill(std::begin(send), std::end(send), MixerNode::kSendOffDb); }
      };

      StripState GetStrip(const MixerNode& m, int s)
      {
         StripState st;
         st.gain = m.gainDb[s]; st.pan = m.pan[s]; st.trim = m.trimDb[s];
         st.name = m.chName[s];
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
         m.chName[s] = st.name;
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
      const float x = mGn->liveX - tok::mixer_x_gap, y = mGn->liveY + tok::mixer_y_ratio * (float)*sel.begin();
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
            if (out->input.IsConnected() && MixerPageTop(ResolvedAudioSource(out->input.GetSource())) == nullptr)
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
      const ImVec2 at(first->liveX - tok::mixer_panel_create_from_audio_outs_gap, first->liveY);

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
      static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Top") };
      static const int kDocks[] = { 0, 3 };
      gMixerPanelDock = gMixerPanelDock == 3 ? 3 : 0;
      if (MenuParts::BeginCombo("##mixerpaneldock", T(kDockLabels[gMixerPanelDock == 3 ? 1 : 0])))
      {
         for (int i = 0; i < 2; i++)
            if (MenuParts::Choice(L(kDockLabels[i]), kDocks[i] == gMixerPanelDock))
               gMixerPanelDock = kDocks[i];
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
         UiInteract::Hit("##mixerpanelgrip",
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
            Ui::SameRow();
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
            Ui::SameRow();
         grip();
      }
      // Zeroed only around EndChild so no ItemSpacing strip shows between this panel and the next row.
      const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
      EndChildSafe();
      ImGui::GetStyle().ItemSpacing = savedSpacing;
   }

   void DrawMixerPanelFloating()
   {
      bool open = true;
      if (BeginFloatingPanel((std::string(T("Mixer")) + "###mixerpanelfloat").c_str(), &open, ImVec2(tok::mixer_panel_floating_begin_floating_panel_x, tok::mixer_panel_floating_begin_floating_panel_y)))
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
