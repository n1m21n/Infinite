// Mixer panel: the console view of the patch's Mixer nodes (internal/plans/mixer-panel).
// A3 is the shell (five docks, persist, the "Enable Mixer Audio" chip). A4 is the console: page
// tabs, strips (name, trim, pan, fader | meter, M S Ã), the master strip, group strips that fold
// open, canvas-selection follow, and the empty state.
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PagePicker.h"
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
      // Strip geometry. The fader takes whatever height is left after the fixed rows.
      constexpr float kStripW = 92.0f; // control width inside a strip
      constexpr float kStripFaceW = 112.0f; // a strip's full width: wide, fixed, the console scrolls
      constexpr float kStripGap = 4.0f;
      constexpr float kRenderW = 196.0f;
                  constexpr float kButtonsH = 26.0f;
      constexpr float kRowGap = 4.0f;
      constexpr float kStripMaxH = 520.0f;
      constexpr float kMinFaderH = 40.0f;
      constexpr int kMaxGroupDepth = 4;
      constexpr float kSendH = 18.0f;     // §4.2 sends row: four mini knobs between name and trim
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
               for (int i = 0; i < 12; i++)
               {
                  ImGui::PushID(i);
                  if (i % 6 != 0)
                     ImGui::SameLine(0.0f, 4.0f);
                  const unsigned c = kPalette[i];
                  if (ImGui::ColorButton("##c", ImVec4((float)((c >> 16) & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f, (float)(c & 0xFF) / 255.0f, 1.0f),
                                         ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(18.0f, 18.0f)))
                  {
                     PushUndoCheckpoint("Channel colour");
                     m->chColor[r.slot] = c;
                     ImGui::CloseCurrentPopup();
                  }
                  ImGui::PopID();
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
            if (MenuParts::Item(T("Move left"), nullptr, false, r.slot > 0))
               MixerPanelStripMove(r.mixerIndex, r.slot, r.slot - 1);
            if (MenuParts::Item(T("Move right"), nullptr, false, r.slot < n - 1))
               MixerPanelStripMove(r.mixerIndex, r.slot, r.slot + 1);
            MenuParts::EndPopup();
         }
         return picked;
      }

      void DrawConsoleMeter(float x, float y, float w, float h, float level);

      // §4.2: one mini knob per send bus of this strip's Mixer (right-click: pre/post), then "+" for a new send FX.
      [[maybe_unused]] void DrawSends(ImVec2 p, MixerNode* m, const StripRef& r)
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
      [[maybe_unused]] void DrawInserts(ImDrawList* dl, ImVec2 p, const StripRef& r, int rows = kInsertRows)
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
         const bool overflow = n > rows - 1;
         const int shown = overflow ? rows - 1 : n;
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

      // A segmented level meter with a calibrated -60..0 dB scale, unlit segments always visible and a falling peak
      // marker held for a moment (state kept per meter in ImGui's storage).
      void DrawConsoleMeter(float x, float y, float w, float h, float level)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 tl(x, y), br(x + w, y + h);
         dl->AddRectFilled(tl, br, IM_COL32(0, 0, 0, 120), 2.0f);
         const float db = level > 1e-4f ? 20.0f * log10f(level) : -120.0f;
         auto frac = [](float d) { return std::clamp((d + 60.0f) / 60.0f, 0.0f, 1.0f); };
         const float seg = 3.0f, gap = 1.0f, step = seg + gap;
         const int n = std::max(1, (int)((h - 2.0f) / step));
         const float lit = frac(db) * (float)n;
         ImGuiStorage* st = ImGui::GetStateStorage();
         const ImGuiID id = ImGui::GetID("##mpk");
         float peak = st->GetFloat(id, 0.0f);
         const float dt = ImGui::GetIO().DeltaTime;
         peak = std::max(lit, peak - 14.0f * dt * (float)n / 30.0f);
         st->SetFloat(id, peak);
         for (int i = 0; i < n; i++)
         {
            const float topY = br.y - 1.0f - (float)(i + 1) * step + gap;
            const float segDb = -60.0f + 60.0f * ((float)i + 0.5f) / (float)n;
            const bool on = (float)i + 0.5f <= lit;
            const ImU32 base = segDb > -3.0f ? IM_COL32(255, 86, 80, 255) : segDb > -12.0f ? IM_COL32(236, 190, 70, 255)
                                                                                           : IM_COL32(84, 214, 140, 255);
            dl->AddRectFilled(ImVec2(x + 1.5f, topY), ImVec2(br.x - 1.5f, topY + seg), on ? base : (base & 0x00FFFFFF) | (38u << 24), 0.5f);
         }
         const int pi = std::clamp((int)peak, 0, n - 1);
         if (peak > 1.0f)
         {
            const float topY = br.y - 1.0f - (float)(pi + 1) * step + gap;
            dl->AddRectFilled(ImVec2(x + 1.5f, topY), ImVec2(br.x - 1.5f, topY + seg), IM_COL32(255, 255, 255, 200), 0.5f);
         }
         static const float kTicks[] = { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
         for (float t : kTicks)
            dl->AddLine(ImVec2(x - 3.0f, br.y - 1.0f - (h - 2.0f) * frac(t)), ImVec2(x - 1.0f, br.y - 1.0f - (h - 2.0f) * frac(t)),
                        IM_COL32(255, 255, 255, 70), 1.0f);
         dl->AddRect(tl, br, IM_COL32(255, 255, 255, 26), 2.0f);
      }

      // ---- Hardware desk look -------------------------------------------------------------------------------
      // References: SSL 4000 G (colour cap, scribble name, trim/pan pots, M/S keys with LEDs, long-throw fader with
      // an LED ladder beside it), Yamaha CL/QL and Allen & Heath SQ (a colour per channel, a readout under the
      // fader). A desk is dark in both themes, so these tones are fixed on purpose.
      constexpr float kCardPad = 8.0f;
      constexpr float kContentMaxW = 92.0f;
      constexpr float kMinStripW = 76.0f;
      constexpr float kScribbleH = 38.0f;
      constexpr float kPotD = 30.0f;
      constexpr float kPotRowH = kPotD + 12.0f;
      constexpr float kPlateH = 16.0f;

      float Lerp(float a, float b, float t) { return a + (b - a) * t; }
      ImU32 Grey(int v, int a = 255) { return IM_COL32(v, v, v, a); }
      ImU32 Tint(ImU32 c, float k, int a = 255) // k < 1 darkens, k > 1 lightens
      {
         const float r = std::min(255.0f, (float)(c & 0xFF) * k), g = std::min(255.0f, (float)((c >> 8) & 0xFF) * k),
                     b = std::min(255.0f, (float)((c >> 16) & 0xFF) * k);
         return IM_COL32((int)r, (int)g, (int)b, a);
      }
      ImU32 RgbToU32(unsigned rgb) { return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255); }


      float StripFaderH(float h, bool sends)
      {
         return std::max(kMinFaderH, h - 2.0f * kCardPad - kScribbleH - kPotRowH - (sends ? kSendH + 2.0f : 0.0f) - kButtonsH -
                                         2.0f * kPlateH - 6.0f * kRowGap);
      }

      void DrawScrew(ImDrawList* dl, ImVec2 c)
      {
         dl->AddCircleFilled(ImVec2(c.x, c.y + 0.6f), 3.0f, Grey(0, 120), 10);
         dl->AddCircleFilled(c, 2.6f, Grey(92), 10);
         dl->AddCircleFilled(ImVec2(c.x - 0.5f, c.y - 0.5f), 1.6f, Grey(138), 8);
         dl->AddLine(ImVec2(c.x - 1.6f, c.y + 1.0f), ImVec2(c.x + 1.6f, c.y - 1.0f), Grey(30), 1.0f);
      }

      // The console chassis behind every strip: a dark steel bed with a lit top rail, so the strips sit in it.
      void DrawChassis(ImDrawList* dl, ImVec2 p, ImVec2 q)
      {
         dl->AddRectFilled(p, q, IM_COL32(18, 19, 22, 255), 5.0f);
         dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 9), Grey(255, 22), Grey(255, 22), Grey(255, 0), Grey(255, 0));
         dl->AddRect(p, q, Grey(0, 200), 5.0f, 0, 1.0f);
      }

      // A strip face: graphite plate, fine vertical brushing (a fixed hash, so nothing swims), a bevelled edge and a
      // colour cap on top. `lit` is the canvas selection.
      void DrawFace(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 cap, bool lit)
      {
         const float r = 3.0f;
         dl->AddRectFilledMultiColor(p, q, IM_COL32(58, 61, 68, 255), IM_COL32(58, 61, 68, 255), IM_COL32(36, 38, 43, 255), IM_COL32(36, 38, 43, 255));
         dl->PushClipRect(p, q, true);
         for (float x = p.x + 1.0f; x < q.x; x += 2.0f)
         {
            const unsigned hsh = (unsigned)((int)(x * 7.0f) * 2654435761u) >> 24;
            dl->AddLine(ImVec2(x, p.y), ImVec2(x, q.y), hsh & 1 ? Grey(255, 5 + (int)(hsh % 7)) : Grey(0, 6 + (int)(hsh % 9)), 1.0f);
         }
         dl->PopClipRect();
         dl->AddRect(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, q.y - 1), Grey(255, 28), r, 0, 1.0f); // lit bevel
         dl->AddRect(p, q, Grey(0, 220), r, 0, 1.0f);
         dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 6), cap, r, ImDrawFlags_RoundCornersTop);
         dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 6), Grey(255, 70), Grey(255, 70), Grey(0, 50), Grey(0, 50));
         dl->AddLine(ImVec2(p.x + 1, p.y + 6.5f), ImVec2(q.x - 1, p.y + 6.5f), Grey(0, 160), 1.0f);
         if (lit)
            dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), r, 0, 1.5f);
      }

      // A rotary pot: knurled skirt, domed cap, a pointer, and an arc in the strip colour for the value. Drag up or
      // down; Shift is fine; double-click resets. Returns true while the value changes.
      bool Pot(const char* id, float* v, float lo, float hi, float def, bool bipolar, float cellW, ImU32 col, const char* tag)
      {
         const ImVec2 p = ImGui::GetCursorScreenPos();
         ImGui::InvisibleButton(id, ImVec2(cellW, kPotRowH));
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
         const float a0 = 0.75f * IM_PI, a1 = 2.25f * IM_PI; // 270 degrees, gap at the bottom
         const float t = hi > lo ? (*v - lo) / (hi - lo) : 0.0f;
         const float ang = Lerp(a0, a1, t);
         // value arc, from the left stop (unipolar) or from the centre (bipolar)
         const float from = bipolar ? Lerp(a0, a1, 0.5f) : a0;
         dl->PathArcTo(c, R + 2.5f, a0, a1, 24);
         dl->PathStroke(Grey(0, 150), 0, 2.5f);
         if (std::fabs(ang - from) > 0.01f)
         {
            dl->PathArcTo(c, R + 2.5f, std::min(from, ang), std::max(from, ang), 24);
            dl->PathStroke(col, 0, 2.5f);
         }
         dl->AddCircleFilled(ImVec2(c.x, c.y + 1.5f), R, Grey(0, 130), 28);        // drop shadow
         dl->AddCircleFilled(c, R, Grey(22), 28);                                   // skirt
         for (int i = 0; i < 24; i++)                                               // knurl
         {
            const float a = (float)i / 24.0f * 2.0f * IM_PI;
            dl->AddLine(ImVec2(c.x + cosf(a) * (R - 3.0f), c.y + sinf(a) * (R - 3.0f)), ImVec2(c.x + cosf(a) * (R - 0.5f), c.y + sinf(a) * (R - 0.5f)),
                        Grey(255, hovered || active ? 70 : 46), 1.0f);
         }
         const float cr = R - 4.0f;
         dl->AddCircleFilled(c, cr, IM_COL32(70, 73, 80, 255), 24);                 // cap
         dl->AddCircleFilled(ImVec2(c.x - 1.0f, c.y - 1.5f), cr - 1.5f, IM_COL32(98, 102, 111, 255), 24);
         dl->AddCircleFilled(ImVec2(c.x - 1.5f, c.y - 2.5f), cr - 4.0f, IM_COL32(112, 117, 126, 255), 20);
         dl->AddCircle(c, cr, Grey(0, 170), 24, 1.0f);
         dl->AddLine(ImVec2(c.x + cosf(ang) * 2.0f, c.y + sinf(ang) * 2.0f), ImVec2(c.x + cosf(ang) * (cr - 0.5f), c.y + sinf(ang) * (cr - 0.5f)),
                     col, 2.2f);
         const float fs = ImGui::GetFontSize() * 0.72f;
         const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, tag).x;
         dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (cellW - tw) * 0.5f, p.y + kPotD + 3.0f), Grey(255, 140), tag);
         return changed;
      }

      // A desk key: a raised cap with an LED bar above the legend, lit when on. Returns true on click.
      bool Key(const char* id, const char* legend, bool on, ImU32 led, ImVec2 size)
      {
         const ImVec2 p = ImGui::GetCursorScreenPos();
         const bool clicked = ImGui::InvisibleButton(id, size);
         const bool hovered = ImGui::IsItemHovered(), down = ImGui::IsItemActive();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 q(p.x + size.x, p.y + size.y);
         dl->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(q.x, q.y + 1), Grey(0, 150), 3.0f);
         const float dip = down ? 1.0f : 0.0f;
         const ImVec2 a(p.x, p.y + dip), b(q.x, q.y - 1.0f + dip);
         dl->AddRectFilledMultiColor(a, b, Grey(hovered ? 96 : 84), Grey(hovered ? 96 : 84), Grey(54), Grey(54));
         dl->AddRect(a, b, Grey(0, 220), 3.0f, 0, 1.0f);
         dl->AddLine(ImVec2(a.x + 2, a.y + 1.5f), ImVec2(b.x - 2, a.y + 1.5f), Grey(255, 60), 1.0f);
         const ImVec2 la(a.x + 4.0f, a.y + 3.0f), lb(b.x - 4.0f, a.y + 5.5f);
         dl->AddRectFilled(la, lb, on ? led : Tint(led, 0.22f), 1.0f);
         if (on)
            dl->AddRectFilled(ImVec2(la.x - 1, la.y - 1), ImVec2(lb.x + 1, lb.y + 1), (led & 0x00FFFFFF) | (60u << 24), 2.0f);
         const float fs = ImGui::GetFontSize() * 0.92f;
         const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, legend);
         dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + (size.x - ts.x) * 0.5f, a.y + 6.0f + (b.y - a.y - 6.0f - ts.y) * 0.5f),
                     on ? Grey(255) : Grey(255, 190), legend);
         return clicked;
      }

      // A long-throw fader: engraved scale left of a dark slot, a metal cap with grips and a white index line.
      // The ladder meter takes the right-hand side. Drag follows the cap; double-click returns to 0 dB.
      void DeskFader(ImVec2 p, float w, float h, float* db, float level, ImU32 col)
      {
         const float meterW = 12.0f, scaleW = 22.0f;
         const float slotX = p.x + scaleW + (w - scaleW - meterW - 4.0f) * 0.5f;
         const float capW = 24.0f, capH = 17.0f;
         const float top = p.y + capH * 0.5f, bottom = p.y + h - capH * 0.5f;
         auto pos01 = [&](float d) { return std::clamp(ConsoleFaderTaper::ValueToPos(d, -60.0f, 12.0f), 0.0f, 1.0f); };
         ImGui::SetCursorScreenPos(ImVec2(slotX - capW * 0.5f, p.y));
         const float before = *db;
         ImGui::InvisibleButton("##fader", ImVec2(capW, h));
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
               HelpTip("-inf dB");
            else
               HelpTip("%+.1f dB", *db);
         }
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float capY = bottom - pos01(*db) * (bottom - top);
         // scale
         static const float kMarks[] = { 12.0f, 0.0f, -10.0f, -20.0f, -30.0f, -60.0f };
         static const char* kLabels[] = { "+12", "0", "10", "20", "30", "-60" };
         const float fs = ImGui::GetFontSize() * 0.68f;
         for (int i = 0; i < 6; i++)
         {
            const float y = bottom - pos01(kMarks[i]) * (bottom - top);
            const bool zero = kMarks[i] == 0.0f;
            dl->AddLine(ImVec2(p.x + scaleW - 5.0f, y), ImVec2(p.x + scaleW - 1.0f, y), Grey(255, zero ? 200 : 110), 1.0f);
            const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, kLabels[i]).x;
            dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + scaleW - 7.0f - tw, y - fs * 0.5f), Grey(255, zero ? 210 : 120), kLabels[i]);
         }
         // slot, with a colour fill below the cap
         dl->AddRectFilled(ImVec2(slotX - 3.0f, top - 4.0f), ImVec2(slotX + 3.0f, bottom + 4.0f), Grey(0, 235), 3.0f);
         dl->AddRect(ImVec2(slotX - 3.0f, top - 4.0f), ImVec2(slotX + 3.0f, bottom + 4.0f), Grey(255, 26), 3.0f);
         dl->AddRectFilled(ImVec2(slotX - 1.0f, capY), ImVec2(slotX + 1.0f, bottom + 3.0f), Tint(col, 0.8f, 200), 1.0f);
         const float y0 = bottom - pos01(0.0f) * (bottom - top);
         dl->AddLine(ImVec2(slotX - 6.0f, y0), ImVec2(slotX + 6.0f, y0), Grey(255, 60), 1.0f);
         // cap
         const ImVec2 a(slotX - capW * 0.5f, capY - capH * 0.5f), b(slotX + capW * 0.5f, capY + capH * 0.5f);
         dl->AddRectFilled(ImVec2(a.x, a.y + 2.5f), ImVec2(b.x, b.y + 2.5f), Grey(0, 140), 3.0f);
         dl->AddRectFilledMultiColor(a, b, Grey(active ? 200 : hovered ? 188 : 176), Grey(active ? 200 : hovered ? 188 : 176), Grey(104), Grey(104));
         dl->AddRect(a, b, Grey(0, 230), 3.0f, 0, 1.0f);
         dl->AddLine(ImVec2(a.x + 1.5f, a.y + 1.5f), ImVec2(b.x - 1.5f, a.y + 1.5f), Grey(255, 150), 1.0f);
         for (int i = -2; i <= 2; i++) // grip ridges
         {
            const float gy = capY + (float)i * 3.0f + (i > 0 ? 1.5f : -1.5f) * 0.0f;
            if (i == 0)
               continue;
            dl->AddLine(ImVec2(a.x + 3.0f, gy), ImVec2(b.x - 3.0f, gy), Grey(0, 110), 1.0f);
            dl->AddLine(ImVec2(a.x + 3.0f, gy + 1.0f), ImVec2(b.x - 3.0f, gy + 1.0f), Grey(255, 70), 1.0f);
         }
         dl->AddLine(ImVec2(a.x + 2.0f, capY), ImVec2(b.x - 2.0f, capY), Grey(255), 2.0f);
         // meter
         ImGui::PushID("mtr");
         DrawConsoleMeter(p.x + w - meterW, p.y, meterW, h, level);
         ImGui::PopID();
      }

      // The readout plate under a fader: dark glass with the value in console green.
      void Plate(ImDrawList* dl, ImVec2 p, float w, const char* text, ImU32 ink)
      {
         const ImVec2 q(p.x + w, p.y + kPlateH);
         dl->AddRectFilled(p, q, IM_COL32(10, 14, 12, 255), 2.0f);
         dl->AddRect(p, q, Grey(255, 30), 2.0f);
         const float fs = ImGui::GetFontSize() * 0.86f;
         const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
         dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (kPlateH - ts.y) * 0.5f), ink, text);
      }

      // The scribble strip: the channel number and name on a dark inset under the colour cap. Click selects the
      // source on the canvas; double-click renames; right-click opens the strip menu. Returns true on a plain click.
      bool Scribble(ImDrawList* dl, ImVec2 p, float w, const char* name, int number, ImU32 col, bool dim, const StripRef* strip)
      {
         const ImVec2 q(p.x + w, p.y + kScribbleH);
         dl->AddRectFilled(p, q, IM_COL32(12, 13, 15, 255), 3.0f);
         dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 5), Tint(col, 0.95f, 255), 2.0f, ImDrawFlags_RoundCornersTop);
         dl->AddRect(p, q, Grey(255, 34), 3.0f);
         ImGui::SetCursorScreenPos(p);
         bool clicked = ImGui::InvisibleButton("##name", ImVec2(w, kScribbleH));
         const bool hovered = ImGui::IsItemHovered();
         if (strip != nullptr && StripNameActions(dl, p, w, name, *strip, clicked))
            clicked = false;
         if (hovered)
            dl->AddRect(p, q, Grey(255, 70), 3.0f);
         char num[8];
         snprintf(num, sizeof(num), "%d", number);
         const float nfs = ImGui::GetFontSize() * 0.78f;
         if (number > 0)
            dl->AddText(ImGui::GetFont(), nfs, ImVec2(p.x + 4.0f, p.y + 7.0f), Tint(col, 1.25f), num);
         const ImVec2 ts = ImGui::CalcTextSize(name);
         const float tx = p.x + std::max(4.0f, (w - ts.x) * 0.5f);
         dl->PushClipRect(ImVec2(p.x + 3.0f, p.y), ImVec2(q.x - 3.0f, q.y), true);
         dl->AddText(ImVec2(tx, p.y + 17.0f), dim ? Grey(255, 110) : Grey(250), name);
         dl->PopClipRect();
         if (hovered && ts.x > w - 8.0f && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
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

         const float cw = std::min(kContentMaxW, w - 2.0f * kCardPad);
         const float cx = p.x + (w - cw) * 0.5f;
         float y = p.y + kCardPad + 4.0f;

         std::string shown = named ? NodeTitle(*named) : std::string(T("Ch")) + " " + std::to_string(s + 1);
         const std::string name = m->chName[s].empty() ? shown : m->chName[s];
         const float nameW = w - 2.0f * kCardPad;
         const float nameX = p.x + kCardPad;
         if (sEditUid == mGn->uid && sEditSlot == s)
         {
            ImGui::SetCursorScreenPos(ImVec2(nameX, y + 10.0f));
            ImGui::SetNextItemWidth(nameW);
            if (sEditFocus)
            {
               ImGui::SetKeyboardFocusHere();
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
         Pot("##trim", &m->trimDb[s], -24.0f, 24.0f, 0.0f, true, cell, col, T("TRIM"));
         UndoOnActivate(&m->trimDb[s], before);
         if (ImGui::IsItemHovered())
            HelpTip("%s %+.1f dB", T("Trim"), m->trimDb[s]);
         ImGui::SetCursorScreenPos(ImVec2(cx + cell, y));
         before = m->pan[s];
         Pot("##pan", &m->pan[s], -1.0f, 1.0f, 0.0f, true, cell, col, T("PAN"));
         UndoOnActivate(&m->pan[s], before);
         if (ImGui::IsItemHovered())
            HelpTip("%s %+.0f", T("Pan"), m->pan[s] * 100.0f);
         y += kPotRowH + kRowGap;

         // Aux sends, only once the console has send buses.
         const int buses = m->SendBuses();
         if (buses > 0)
         {
            DrawSends(ImVec2(cx - 1.0f, y), m, r);
            y += kSendH + 2.0f + kRowGap;
         }

         // Keys: mute, solo, polarity.
         const float bw = (cw - 2.0f * 2.0f) / 3.0f;
         ImGui::SetCursorScreenPos(ImVec2(cx, y));
         if (Key("##m", "M", m->mute[s], IM_COL32(255, 86, 80, 255), ImVec2(bw, kButtonsH)))
         {
            m->mute[s] = !m->mute[s];
            UndoOnToggle(&m->mute[s]);
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Mute"));
         ImGui::SetCursorScreenPos(ImVec2(cx + bw + 2.0f, y));
         if (Key("##s", "S", m->solo[s], IM_COL32(255, 214, 64, 255), ImVec2(bw, kButtonsH)))
         {
            if (ImGui::IsKeyDown(ImGuiMod_Alt)) // Alt-click S clears every solo on the console
            {
               m->solo[s] = !m->solo[s];
               MixerPanelClearSolos(MixerPanelCurrentPageTop());
            }
            else
            {
               m->solo[s] = !m->solo[s];
               UndoOnToggle(&m->solo[s]);
            }
         }
         if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
         {
            PushUndoCheckpoint("Mixer strip");
            m->soloSafe[s] = !m->soloSafe[s];
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", m->soloSafe[s] ? T("Solo safe (right-click)") : T("Solo (Alt-click: clear all, right-click: solo safe)"));
         ImGui::SetCursorScreenPos(ImVec2(cx + 2.0f * (bw + 2.0f), y));
         if (Key("##ph", "\xC3\x98", m->phase[s], IM_COL32(120, 190, 255, 255), ImVec2(bw, kButtonsH)))
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
            snprintf(val, sizeof(val), "%+.1f", m->gainDb[s]);
         Plate(dl, ImVec2(cx, y), cw, val, m->mute[s] ? IM_COL32(255, 120, 110, 255) : IM_COL32(120, 235, 160, 255));

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
         DrawScrew(dl, ImVec2(p.x + 6.0f, p.y + 12.0f));
         DrawScrew(dl, ImVec2(p.x + w - 6.0f, p.y + 12.0f));
         DrawScrew(dl, ImVec2(p.x + 6.0f, p.y + h - 6.0f));
         DrawScrew(dl, ImVec2(p.x + w - 6.0f, p.y + h - 6.0f));
         const float cw = std::min(kContentMaxW, w - 2.0f * kCardPad - 6.0f);
         const float cx = p.x + (w - cw) * 0.5f;
         float y = p.y + kCardPad + 4.0f;
         if (Scribble(dl, ImVec2(p.x + kCardPad, y), w - 2.0f * kCardPad, T("Master"), 0, cap, false, nullptr))
            SelectOnCanvas(topIndex);
         y += kScribbleH + kRowGap;

         // True peak and LUFS M / S / I on a dark display. Asking each frame keeps the meter running.
         m->RequestMeters();
         {
            const float dispH = kPotRowH;
            const ImVec2 dp(cx, y);
            dl->AddRectFilled(dp, ImVec2(dp.x + cw, dp.y + dispH), IM_COL32(10, 14, 12, 255), 2.0f);
            dl->AddRect(dp, ImVec2(dp.x + cw, dp.y + dispH), Grey(255, 30), 2.0f);
            ImGui::SetCursorScreenPos(dp);
            if (ImGui::InvisibleButton("##loud", ImVec2(cw, dispH)))
               m->ResetMeters();
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("True peak (dBTP) and loudness (LUFS): momentary, short-term, integrated. Click to reset."));
            const char* tags[4] = { "TP", "M", "S", "I" };
            const float vals[4] = { m->TruePeakDb(), m->LoudnessMomentary(), m->LoudnessShort(), m->LoudnessIntegrated() };
            const float fs = ImGui::GetFontSize() * 0.78f;
            const float rowH = dispH * 0.5f;
            for (int i = 0; i < 4; i++)
            {
               char txt[16];
               if (vals[i] <= -99.0f)
                  snprintf(txt, sizeof(txt), "-inf");
               else
                  snprintf(txt, sizeof(txt), "%.1f", vals[i]);
               const float colX = dp.x + (i % 2) * (cw * 0.5f);
               const float ry = dp.y + (i / 2) * rowH + (rowH - fs) * 0.5f;
               const bool over = i == 0 && vals[0] > -1.0f; // above -1 dBTP: likely to clip once encoded
               dl->AddText(ImGui::GetFont(), fs, ImVec2(colX + 3.0f, ry), Grey(255, 110), tags[i]);
               const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, txt).x;
               dl->AddText(ImGui::GetFont(), fs, ImVec2(colX + cw * 0.5f - 3.0f - tw, ry),
                           over ? IM_COL32(255, 110, 100, 255) : IM_COL32(120, 235, 160, 255), txt);
            }
         }
         y += kPotRowH + kRowGap;

         // Mono check and dim, monitor-only (AudioEngine::ApplyMonitor); never in a render or capture.
         {
            AudioEngine& eng = AudioEngine::Instance();
            const bool mono = eng.MonitorMono(), dim = eng.MonitorDim();
            const float bw = (cw - 2.0f) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(cx, y));
            if (Key("##mono", T("Mono"), mono, IM_COL32(120, 190, 255, 255), ImVec2(bw, kButtonsH)))
               eng.SetMonitor(!mono, dim);
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Mono check. Speakers only: never in a render."));
            ImGui::SetCursorScreenPos(ImVec2(cx + bw + 2.0f, y));
            if (Key("##dim", T("Dim"), dim, IM_COL32(255, 214, 64, 255), ImVec2(bw, kButtonsH)))
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
            snprintf(val, sizeof(val), "%+.1f", m->masterDb);
         Plate(dl, ImVec2(cx, y), cw, val, IM_COL32(120, 235, 160, 255));
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
         return any ? ImVec2(x + 420.0f, y) : ImVec2(0.0f, 0.0f);
      }

      [[maybe_unused]] void DrawPageTabs(const std::vector<Page>& pages, int current)
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

      // §4.3 / D8: A-H. Click recalls, Shift-click stores, right-click sets the crossfade. Keys 1-8 (Shift
      // stores) while the panel claims keys (G5). Then "Clear solo" while any strip is soloed.
      [[maybe_unused]] void DrawSnapshots(int topIndex)
      {
         MixerNode* top = MixerAt(topIndex);
         if (top == nullptr)
            return;
         static const char* kNames[MixerNode::kSnapshots] = { "A", "B", "C", "D", "E", "F", "G", "H" };
         const bool shift = ImGui::GetIO().KeyShift;
         for (int k = 0; k < MixerNode::kSnapshots; k++)
         {
            const std::string label = std::string(kNames[k]) + "##snap" + std::to_string(k);
            const bool has = top->SnapshotHas(k);
            if (!has)
               ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            const bool clicked = ChipButton::Draw(label.c_str(), has && top->SnapshotLast() == k, tok::tile);
            if (!has)
               ImGui::PopStyleVar();
            if (clicked)
            {
               if (shift || !has)
                  MixerPanelSnapshotStore(topIndex, k);
               else
                  MixerPanelSnapshotRecall(topIndex, k);
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s %s", T("Snapshot"), has ? T("(click recalls, Shift-click stores)") : T("(click stores)"));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
               ImGui::OpenPopup("##snapfade");
            ImGui::SameLine(0.0f, 2.0f);
         }
         if (MenuParts::BeginPopup("##snapfade"))
         {
            static const int kFades[] = { 0, 50, 200, 500, 1000, 2000 };
            for (int ms : kFades)
            {
               const std::string item = ms == 0 ? std::string(T("No crossfade")) : std::to_string(ms) + " ms";
               if (MenuParts::Item(item.c_str(), nullptr, top->snapFadeMs == ms))
               {
                  PushUndoCheckpoint("Snapshot crossfade");
                  top->snapFadeMs = ms;
               }
            }
            MenuParts::EndPopup();
         }
         if (gMixerClaimedKeys && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeySuper && !ImGui::GetIO().KeyAlt)
         {
            static const ImGuiKey kKeys[MixerNode::kSnapshots] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
                                                                   ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8 };
            for (int k = 0; k < MixerNode::kSnapshots; k++)
               if (ImGui::IsKeyPressed(kKeys[k], false))
               {
                  if (shift)
                     MixerPanelSnapshotStore(topIndex, k);
                  else
                     MixerPanelSnapshotRecall(topIndex, k);
               }
         }
         if (MixerPanelAnySolo(topIndex))
         {
            ImGui::SameLine(0.0f, tok::space_2);
            if (ChipButton::Draw((std::string(T("Clear solo")) + "##clearsolo").c_str(), true, tok::tile))
               MixerPanelClearSolos(topIndex);
         }
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

      bool sPendingAdd = false;
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
               PushUndoCheckpoint("Delete mixer page");
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
             ImGui::IsKeyPressed(ImGuiKey_R, false) && current >= 0)
            PagePicker::BeginRename("##mixerPage", entries, current);
         const PagePicker::Result r = PagePicker::Draw("##mixerPage", entries, current, T("Pages: switch, add, rename, colour or delete one"));
         const Page* pg = r.index >= 0 && r.index < (int)pages.size() ? &pages[r.index] : nullptr;
         MixerNode* m = pg != nullptr ? PageMixer(*pg) : nullptr;
         switch (r.act)
         {
         case PagePicker::Act::Select: sPageUid = pg->topUid; break;
         case PagePicker::Act::Add: sPendingAdd = true; break;
         case PagePicker::Act::Rename:
            if (m != nullptr)
            {
               PushUndoCheckpoint("Rename mixer page");
               m->pageName = r.name;
            }
            break;
         case PagePicker::Act::Recolor:
            if (m != nullptr)
            {
               PushUndoCheckpoint("Colour mixer page");
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

         // Header, as the other panels: the page dropdown left (no title: the rail icon names the panel),
         // Render and Mixer Audio pinned right.
         DrawPagePicker(pages, current);
         {
            const bool mixerMode = gAudioMode == AudioMode::Mixer;
            const char* audioLabel = mixerMode ? T("Mixer Audio On") : T("Enable Mixer Audio");
            const float audioW = ImGui::CalcTextSize(audioLabel).x + 2.0f * tok::space_2;
            const char* renderLabel = T("Render");
            const float renderW = ImGui::CalcTextSize(renderLabel).x + 2.0f * tok::space_2;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, origin.x + avail.x - audioW - renderW - tok::space_2), origin.y));
            if (ChipButton::Draw(renderLabel, gMixerRenderOpen || MixerRenderActive(), tok::tile))
               gMixerRenderOpen = !gMixerRenderOpen;
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Record this console to a file."));
            ImGui::SameLine(0.0f, tok::space_2);
            ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, origin.x + avail.x - audioW), origin.y));
            if (ChipButton::Draw(audioLabel, mixerMode, tok::tile))
               SetMixerAudio(!mixerMode);
            if (ImGui::IsItemHovered())
               HelpTip("%s", mixerMode ? T("Only the console reaches the speakers. Click to hand audio back to the canvas.")
                                       : T("Hear only the console: other Audio Outs go silent."));
         }

         const ImVec2 bodyMin(origin.x, ImGui::GetCursorScreenPos().y + tok::space_2);
         ImVec2 bodyMax(origin.x + avail.x, origin.y + avail.y);
         gMixerPanelStripCount = 0;
         gMixerRenderMin = gMixerRenderMax = ImVec2(0.0f, 0.0f);
         if (current < 0)
         {
            if (bodyMax.y > bodyMin.y)
               DrawEmpty(bodyMin, bodyMax);
         }
         else if (bodyMax.y - bodyMin.y > 40.0f)
         {
            const Page& page = pages[current];
            // A fixed console: one strip per channel of the top Mixer, in slot order, no groups, no returns row.
            std::vector<StripRef> strips = Strips(page.topIndex, false);
            strips.erase(std::remove_if(strips.begin(), strips.end(), [](const StripRef& r) { return r.depth > 0; }), strips.end());
            std::stable_sort(strips.begin(), strips.end(), [](const StripRef& a, const StripRef& b) { return a.slot < b.slot; });

            // The render column takes the right edge when asked for and there is room.
            const bool wantRender = gMixerRenderOpen || MixerRenderActive();
            ImVec2 renderMin(0.0f, 0.0f), renderMax(0.0f, 0.0f);
            if (wantRender && bodyMax.x - bodyMin.x >= kRenderW + 3.0f * kMinStripW + 4.0f * kStripGap)
            {
               renderMin = ImVec2(bodyMax.x - kRenderW, bodyMin.y);
               renderMax = bodyMax;
               bodyMax.x -= kRenderW + 2.0f * kStripGap;
            }
            const float bodyW = bodyMax.x - bodyMin.x;
            const float bodyH = bodyMax.y - bodyMin.y;
            const int n = std::max(1, (int)strips.size());

            // Strips and the master are wide, fixed-width desk strips; a long console scrolls sideways.
            const float scrollH = ImGui::GetStyle().ScrollbarSize;
            const float stripW = kStripFaceW;
            const float stripsW = std::max(kMinStripW, bodyW - stripW - kStripGap);
            const float contentW = (float)n * stripW + (float)(n - 1) * kStripGap;
            const bool scrolls = contentW > stripsW + 0.5f;
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
               ImGui::Dummy(ImVec2(std::max(1.0f, x - base.x - kStripGap), colH));
               gMixerStripsMin = wmn;
               gMixerStripsMax = ImVec2(wmn.x + ImGui::GetWindowWidth(), wmn.y + ImGui::GetWindowHeight());
            }
            ImGui::EndChild();
            gMixerPanelStripCount = (int)strips.size();
            const ImVec2 masterAt(bodyMax.x - stripW, bodyMin.y);
            DrawMaster(ImGui::GetWindowDrawList(), masterAt, stripW, colH, page.topIndex);
            if (renderMax.x > renderMin.x && renderMax.y - renderMin.y > 40.0f)
            {
               std::vector<int> outs;
               for (const Page& pg : pages)
                  outs.push_back(pg.outIndex);
               ImGui::SetCursorScreenPos(renderMin);
               ImGui::BeginChild("##mixerrender", ImVec2(renderMax.x - renderMin.x, renderMax.y - renderMin.y), false);
               DrawMixerRenderColumn(outs, page.outIndex);
               ImGui::EndChild();
               gMixerRenderMin = renderMin;
               gMixerRenderMax = renderMax;
            }
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
            static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Top") };
            static const int kDocks[] = { 0, 3 };
            for (int i = 0; i < 2; i++)
               if (MenuParts::Item(L(kDockLabels[i]), nullptr, kDocks[i] == gMixerPanelDock))
                  gMixerPanelDock = kDocks[i];
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
      const float x = mGn->liveX + (master ? 260.0f + 200.0f * (float)ch.fx.size() : -220.0f);
      const float y = mGn->liveY + (master ? 0.0f : 60.0f * (float)slot);
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
