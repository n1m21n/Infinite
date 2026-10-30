// Piano Roll panel (docs/plans/piano-roll/README.md §3, §4, §9): docked bottom or top like the Timeline, one page per
// MIDI lane. A page edits the lane's clips and notes through the Arrange note ops; the linked Piano Roll node plays
// them. Edits are timeline undo entries; page add and delete touch a node too, so they are patch undo steps.
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/DropdownField.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/SectionCard.h"
#include "arrange/ArrangeModel.h"
#include "audio/MusicTime.h"
#include "core/Transport.h"
#include "nodes/PianoRollNode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app
{

namespace
{
   constexpr float kMinHeight = 240.0f;
   constexpr float kIconW = 30.0f;
   constexpr float kKeysW = 44.0f, kRulerH = 18.0f, kStripH = 24.0f;
   constexpr float kInspectorW = 268.0f, kSplitH = 6.0f, kMinExprH = 40.0f, kMaxExprH = 240.0f;
   constexpr float kMinKeyH = 5.0f, kMaxKeyH = 32.0f;
   constexpr double kMinPxPerTick = 0.002, kMaxPxPerTick = 2.0;
   constexpr int kPaletteSize = 12;
   using Arrange::Tick;

   // One note as it lands on the timeline: looped clips show every pass, the first pass is the note itself.
   struct Placed
   {
      const Arrange::Clip* clip = nullptr;
      const Arrange::Note* note = nullptr;
      Tick start = 0, end = 0;
      bool repeat = false;
   };

   enum class Expr
   {
      Velocity,
      Release,
      Chance,
      Pitch,
      Pressure,
      Timbre
   };

   enum class Tool
   {
      Select,
      Draw,
      Erase,
      Blade,
      Mute
   };

   enum class Drag
   {
      None,
      Marquee,
      MoveNotes,
      SizeStart,
      SizeEnd,
      Paint,   // the draw tool's run of notes
      Rub,     // erase and mute strokes
      MoveClips,
      TrimStart,
      TrimEnd,
      Range,
      LoopStart,
      LoopEnd,
      Keys
   };

   struct State
   {
      uint64_t lane = 0;         // the page on show
      uint64_t viewedLane = 0;   // the page the view was last fitted to
      double pxPerTick = 0.06;
      double viewTick = 0.0;     // tick at the roll's left edge
      float keyH = 12.0f;
      float topRow = 0.0f;       // row at the roll's top edge
      bool follow = false;
      bool fold = false;
      bool audition = true;
      Expr expr = Expr::Velocity;
      float exprH = 72.0f;
      bool exprDrag = false;
      ImVec2 exprFrom {};
      bool inspectorHovered = false;
      Arrange::QuantizeSpec quant;
      float strumTicks = 40.0f;
      float humanizeTime = 15.0f, humanizeVel = 8.0f;
      uint32_t humanizeSeed = 1;
      std::vector<Arrange::Note> clipboard; // starts relative to the first note
      bool openQuantize = false, openFunctions = false;
      Arrange::Lane fieldBase;   // the page as an inspector drag found it
      float fieldFrom = 0.0f;    // the field's value there
      Tool tool = Tool::Select;
      Drag drag = Drag::None;
      Arrange::Lane base;        // the page as the drag found it; every drag step re-runs from here
      ImVec2 dragFrom {};
      Tick dragTick = 0;         // tick under the press
      int dragPitch = 0;         // pitch under the press
      Tick anchor = 0;           // the dragged note or clip's edge, for snapping
      Tick lastDeltaT = 0;
      int lastDeltaP = 0;
      uint64_t paintClip = 0;
      uint32_t paintNote = 0;
      int paintRow = -1;
      std::set<std::pair<uint64_t, uint32_t>> rubbed;
      std::map<uint64_t, std::vector<uint32_t>> dragNotes; // the notes in the base being dragged
      bool dragCopies = false;   // Alt: the originals stay, copies travel
      Tick lastLength = Arrange::kPPQ / 4;
      int lastVelocity = 100;
      int lastDivision = 16;
      Tick rangeStart = 0, rangeEnd = 0; // the ruler selection (empty = none)
      int soundingPitch = -1;
      double soundingUntil = 0.0;
      uint64_t prunedRevision = 0;
      std::vector<int> rows;     // the pitches on show, high to low
      std::array<int, 128> rowOf {};
      std::set<uint64_t> clips;  // clips being edited; none = all
      std::set<std::pair<uint64_t, uint32_t>> notes; // selected notes (clip, note)
      uint64_t renameLane = 0;   // a tab being renamed
      char renameBuf[64] = {};
      bool renameFocus = false;
      bool pickOnCanvas = false; // the link chip's "Pick on canvas"
      uint64_t lastSelectedNode = 0;
   };

   State& S()
   {
      static State s;
      return s;
   }

   std::vector<Arrange::Lane*> Pages()
   {
      std::vector<Arrange::Lane*> out;
      for (Arrange::Lane& l : gArrange.lanes)
         if (l.type == Arrange::kLaneMidi)
            out.push_back(&l);
      return out;
   }

   Arrange::Lane* Page()
   {
      Arrange::Lane* l = S().lane != 0 ? Arrange::FindLane(gArrange, S().lane) : nullptr;
      return l != nullptr && l->type == Arrange::kLaneMidi ? l : nullptr;
   }

   GraphNode* LinkedNode(const Arrange::Lane& l)
   {
      return l.noteNodeUid != 0 ? FindNodeByUid(l.noteNodeUid) : nullptr;
   }

   std::string NodeLabel(const GraphNode& gn)
   {
      auto* n = dynamic_cast<PianoRollNode*>(gn.node.get());
      if (n != nullptr && !n->laneName.empty())
         return n->laneName + "##" + std::to_string(gn.uid);
      return std::string(T("Piano Roll")) + " " + std::to_string(gn.index) + "##" + std::to_string(gn.uid);
   }

   // A new page: the lane plus a linked node at the canvas view's centre, one patch undo step.
   void AddPage()
   {
      PushUndoCheckpoint("Add Piano Roll Page");
      GraphNode* fresh = SpawnNode("Piano Roll", "Notes", gViewCenterCanvas.x, gViewCenterCanvas.y);
      if (fresh == nullptr)
         return;
      const uint64_t lane = Arrange::AddLane(gArrange, Arrange::kLaneMidi);
      if (Arrange::Lane* l = Arrange::FindLane(gArrange, lane))
         l->name = Arrange::UniqueLaneName(gArrange, "Part", Arrange::kLaneMidi);
      Arrange::LinkLane(gArrange, lane, fresh->uid);
      ArrangeCommitEdit();
      S().lane = lane;
   }

   void DeletePage(uint64_t lane)
   {
      PushUndoCheckpoint("Delete Piano Roll Page");
      Arrange::RemoveLane(gArrange, lane);
      ArrangeCommitEdit();
      if (S().lane == lane)
         S().lane = 0;
   }

   ImU32 Tint(float alpha) { return ImGui::GetColorU32(ImGuiCol_Text, alpha); }

   ImU32 AccentU32(float alpha)
   {
      const ImVec4 a = AccentEmphasisSelected();
      return ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, a.w * alpha));
   }

   int PageIndex(const Arrange::Lane& page)
   {
      int i = 0;
      for (const Arrange::Lane& l : gArrange.lanes)
         if (l.type == Arrange::kLaneMidi)
         {
            if (l.id == page.id)
               return i;
            i++;
         }
      return 0;
   }

   // The clip's own colour, or the page's, or the palette by page.
   ImVec4 ClipColor(const Arrange::Clip& c, const Arrange::Lane& page)
   {
      if (c.colorR + c.colorG + c.colorB > 0.0f)
         return ImVec4(c.colorR, c.colorG, c.colorB, 1.0f);
      if (page.colorR + page.colorG + page.colorB > 0.0f)
         return ImVec4(page.colorR, page.colorG, page.colorB, 1.0f);
      float r, g, b;
      ImGui::ColorConvertHSVtoRGB((float)(PageIndex(page) % kPaletteSize) / (float)kPaletteSize, 0.55f, 0.88f, r, g, b);
      return ImVec4(r, g, b, 1.0f);
   }

   ImU32 WithAlpha(ImVec4 c, float alpha)
   {
      c.w = alpha;
      return ImGui::GetColorU32(c);
   }

   bool IsBlackKey(int pitch)
   {
      const int pc = pitch % 12;
      return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
   }

   bool InScale(int pitch)
   {
      Transport& tr = Transport::Instance();
      return MusicTime::ScaleContainsPitchClass(tr.Scale(), (pitch - tr.Key() + 120) % 12);
   }

   std::string PitchName(int pitch)
   {
      static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
      return std::string(kNames[pitch % 12]) + std::to_string(pitch / 12 - 2);
   }

   // Every note of the clip as it plays in its box, the schedule's walk.
   template <class Fn>
   void ForEachPlaced(const Arrange::Clip& c, Fn&& fn)
   {
      const bool loop = c.midiLoop && c.loopLength > 0;
      const Tick loopEnd = c.loopStart + c.loopLength;
      Tick dt = 0;
      bool first = true;
      while (dt < c.length)
      {
         const Tick q = Arrange::MidiContentTick(c, dt);
         const Tick seg = loop ? std::min(c.length - dt, std::max<Tick>(1, loopEnd - q)) : c.length - dt;
         const Tick segStart = c.start + dt;
         auto it = std::lower_bound(c.notes.begin(), c.notes.end(), q, [](const Arrange::Note& n, Tick t) { return n.start < t; });
         for (; it != c.notes.end() && it->start < q + seg; ++it)
         {
            const Tick on = segStart + (it->start - q);
            fn(Placed { &c, &*it, on, std::min(segStart + seg, on + it->length), !first });
         }
         dt += seg;
         first = false;
      }
   }

   // The rows on show: every key, or with fold the keys that hold notes (with scale snap, the scale's keys).
   void BuildRows(const Arrange::Lane& page)
   {
      State& s = S();
      s.rows.clear();
      s.rowOf.fill(-1);
      std::array<bool, 128> used {};
      if (s.fold && !page.scaleSnap)
         for (const Arrange::Clip& c : page.clips)
            for (const Arrange::Note& n : c.notes)
               used[n.pitch] = true;
      for (int p = 127; p >= 0; p--)
      {
         if (s.fold && !(page.scaleSnap ? InScale(p) : used[(size_t)p]))
            continue;
         s.rowOf[(size_t)p] = (int)s.rows.size();
         s.rows.push_back(p);
      }
      if (s.rows.empty()) // fold on a page with no notes
      {
         s.rows.push_back(60);
         s.rowOf[60] = 0;
      }
   }

   void FitView(const Arrange::Lane& page, float rollW, float rollH)
   {
      State& s = S();
      Tick t0 = Arrange::kMaxTick, t1 = 0;
      int lo = 127, hi = 0;
      for (const Arrange::Clip& c : page.clips)
      {
         t0 = std::min(t0, c.start);
         t1 = std::max(t1, c.start + c.length);
         for (const Arrange::Note& n : c.notes)
         {
            lo = std::min(lo, (int)n.pitch);
            hi = std::max(hi, (int)n.pitch);
         }
      }
      if (t1 <= t0)
      {
         t0 = 0;
         t1 = 4 * Arrange::kTicksPerBar;
      }
      s.pxPerTick = std::clamp((double)rollW * 0.95 / (double)(t1 - t0), kMinPxPerTick, kMaxPxPerTick);
      s.viewTick = (double)t0;
      if (hi < lo)
         lo = hi = 60;
      const int mid = std::max(0, s.rowOf[(size_t)((lo + hi) / 2)]);
      s.topRow = (float)mid - rollH / s.keyH * 0.5f;
   }

   void ZoomX(double factor, float anchorX, float rollX)
   {
      State& s = S();
      const double tick = s.viewTick + (double)(anchorX - rollX) / s.pxPerTick;
      s.pxPerTick = std::clamp(s.pxPerTick * factor, kMinPxPerTick, kMaxPxPerTick);
      s.viewTick = std::max(0.0, tick - (double)(anchorX - rollX) / s.pxPerTick);
   }

   void ClampView(float rollH)
   {
      State& s = S();
      s.viewTick = std::max(0.0, s.viewTick);
      s.topRow = std::clamp(s.topRow, 0.0f, std::max(0.0f, (float)s.rows.size() - rollH / s.keyH));
   }

   // The page's scale as a MoveNotes mask, or 0 with scale snap off.
   uint16_t ScaleMask(const Arrange::Lane& page)
   {
      if (!page.scaleSnap)
         return 0;
      uint16_t mask = 0;
      for (int pc = 0; pc < 12; pc++)
         if (InScale(pc + 60))
            mask = (uint16_t)(mask | (1u << ((pc + 60) % 12)));
      return mask;
   }

   int SnapPitch(const Arrange::Lane& page, int pitch)
   {
      Transport& tr = Transport::Instance();
      return page.scaleSnap ? std::clamp(MusicTime::SnapToScale(pitch, tr.Key(), tr.Scale(), MusicTime::kSnapNearest), 0, 127) : pitch;
   }

   // Snap to the grid; Cmd or Ctrl held frees it.
   Tick SnapTick(Tick t)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const Tick grid = ArrangeSnapGridTicks();
      return grid <= 0 || io.KeyCtrl || io.KeySuper ? t : Arrange::SnapToGrid(t, grid);
   }

   Tick FloorTick(Tick t)
   {
      const ImGuiIO& io = ImGui::GetIO();
      const Tick grid = ArrangeSnapGridTicks();
      return grid <= 0 || io.KeyCtrl || io.KeySuper ? t : Arrange::GridFloor(t, grid);
   }

   Tick GridOrBeatSixteenth()
   {
      const Tick grid = ArrangeSnapGridTicks();
      return grid > 0 ? grid : Arrange::kPPQ / 4;
   }

   void StopSound(const Arrange::Lane& page)
   {
      State& s = S();
      if (s.soundingPitch < 0)
         return;
      if (GraphNode* gn = LinkedNode(page))
         if (auto* n = dynamic_cast<PianoRollNode*>(gn->node.get()))
            n->Audition(s.soundingPitch, 0.0f, false);
      s.soundingPitch = -1;
   }

   // Sounds one key; the last one ends. `holdFor` > 0 ends it by itself, 0 waits for StopSound.
   void Sound(const Arrange::Lane& page, int pitch, int velocity, double holdFor = 0.0)
   {
      State& s = S();
      if (!s.audition || pitch == s.soundingPitch)
         return;
      StopSound(page);
      if (GraphNode* gn = LinkedNode(page))
         if (auto* n = dynamic_cast<PianoRollNode*>(gn->node.get()))
         {
            n->Audition(pitch, (float)velocity / 127.0f, true);
            s.soundingPitch = pitch;
            s.soundingUntil = holdFor > 0.0 ? ImGui::GetTime() + holdFor : 0.0;
         }
   }

   std::map<uint64_t, std::vector<uint32_t>> SelectionByClip()
   {
      std::map<uint64_t, std::vector<uint32_t>> out;
      for (const auto& [clip, note] : S().notes)
         out[clip].push_back(note);
      return out;
   }

   // Drops selected notes and clips that an undo or an edit took away.
   void PruneSelection(const Arrange::Lane& page)
   {
      State& s = S();
      if (s.prunedRevision == gArrange.revision)
         return;
      s.prunedRevision = gArrange.revision;
      std::set<std::pair<uint64_t, uint32_t>> notes;
      std::set<uint64_t> clips;
      for (const Arrange::Clip& c : page.clips)
      {
         if (s.clips.count(c.id) != 0)
            clips.insert(c.id);
         for (const Arrange::Note& n : c.notes)
            if (s.notes.count({ c.id, n.id }) != 0)
               notes.insert({ c.id, n.id });
      }
      s.notes.swap(notes);
      s.clips.swap(clips);
   }

   void DrawTabs()
   {
      State& s = S();
      const std::vector<Arrange::Lane*> pages = Pages();
      if (s.lane == 0 && !pages.empty())
         s.lane = pages.front()->id;
      for (size_t i = 0; i < pages.size(); i++)
      {
         Arrange::Lane* l = pages[i];
         const uint64_t id = l->id;
         ImGui::PushID((int)id);
         if (i > 0)
            FormParts::Inline(0.0f, tok::space_1);
         if (s.renameLane == id)
         {
            if (s.renameFocus)
               ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(120.0f);
            const bool done = FieldWell::InputText("##tabrename", s.renameBuf, sizeof s.renameBuf,
                                                   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool lost = !s.renameFocus && !ImGui::IsItemActive();
            s.renameFocus = false;
            if (done || lost)
            {
               const std::string name = s.renameBuf;
               s.renameLane = 0;
               if (!name.empty() && name != l->name)
                  ArrangeEdit([&] {
                     l->name = name;
                     gArrange.revision++;
                  });
            }
            ImGui::PopID();
            continue;
         }
         const std::string label = l->name + "##tab";
         if (ChipButton::Draw(label.c_str(), s.lane == id, tok::tile))
            s.lane = id;
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            s.renameLane = id;
            s.renameFocus = true;
            std::snprintf(s.renameBuf, sizeof s.renameBuf, "%s", l->name.c_str());
         }
         // Drag a tab past its neighbour: the lane order is the tab order.
         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, tok::space_2))
         {
            const float mx = ImGui::GetIO().MousePos.x;
            const float x0 = ImGui::GetItemRectMin().x, x1 = ImGui::GetItemRectMax().x;
            const int dir = mx > x1 ? 1 : mx < x0 ? -1 : 0;
            const int to = (int)i + dir;
            if (dir != 0 && to >= 0 && to < (int)pages.size())
            {
               int index = 0;
               for (int k = 0; k < (int)gArrange.lanes.size(); k++)
                  if (gArrange.lanes[(size_t)k].id == pages[(size_t)to]->id)
                     index = k;
               ArrangeEdit([&] { Arrange::ReorderLane(gArrange, id, index); });
               ImGui::PopID();
               break; // the lane pointers moved
            }
         }
         if (ImGui::BeginPopupContextItem("##tabmenu"))
         {
            if (MenuParts::Item(L("Rename")))
            {
               s.renameLane = id;
               s.renameFocus = true;
               std::snprintf(s.renameBuf, sizeof s.renameBuf, "%s", l->name.c_str());
            }
            if (MenuParts::Item(L("Delete Page")))
            {
               DeletePage(id);
               ImGui::EndPopup();
               ImGui::PopID();
               break;
            }
            ImGui::EndPopup();
         }
         ImGui::PopID();
      }
      if (!pages.empty())
         FormParts::Inline(0.0f, tok::space_1);
      if (ActionButton::Draw("##pageAdd", ImVec2(kIconW, tok::tile)))
         AddPage();
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Add a page and a Piano Roll node"));
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), tok::icon_md,
                  ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::Plus);
   }

   void DrawLinkChip()
   {
      State& s = S();
      Arrange::Lane* page = Page();
      if (page == nullptr)
         return;
      const GraphNode* linked = LinkedNode(*page);
      std::string face;
      if (s.pickOnCanvas)
         face = T("Click a Piano Roll node");
      else if (linked != nullptr)
         face = NodeLabel(*linked);
      else
         face = T("Link a Piano Roll node");
      const float w = 190.0f;
      ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - w));
      const std::string caption = face.substr(0, face.find("##")) + "##pageLink";
      if (DropdownField::Draw(caption.c_str(), ImVec2(w, 0.0f)))
         ImGui::OpenPopup("##pageLinkMenu");
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("The Piano Roll node that plays this page"));
      if (!MenuParts::BeginPopup("##pageLinkMenu"))
         return;
      for (GraphNode& gn : gNodes)
      {
         if (dynamic_cast<PianoRollNode*>(gn.node.get()) == nullptr || gn.uid == page->noteNodeUid)
            continue;
         bool elsewhere = false;
         for (const Arrange::Lane& l : gArrange.lanes)
            elsewhere = elsewhere || (l.type == Arrange::kLaneMidi && l.noteNodeUid == gn.uid);
         std::string label = NodeLabel(gn);
         if (elsewhere)
            label = std::string(T("Move the link here:")) + " " + label;
         if (MenuParts::Item(label.c_str()))
         {
            const uint64_t uid = gn.uid, lane = page->id;
            ArrangeEdit([&] { Arrange::LinkLane(gArrange, lane, uid); });
         }
      }
      if (MenuParts::Item(L("Pick on canvas")))
         s.pickOnCanvas = true;
      if (linked != nullptr && MenuParts::Item(L("Unlink")))
      {
         const uint64_t lane = page->id;
         ArrangeEdit([&] { Arrange::LinkLane(gArrange, lane, 0); });
      }
      MenuParts::EndPopup();
   }

   // Selecting a Piano Roll node jumps to its page, or finishes "Pick on canvas".
   void FollowCanvasSelection()
   {
      State& s = S();
      uint64_t uid = 0;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<PianoRollNode*>(gn.node.get()) != nullptr && ed::IsNodeSelected(gn.NodeId()))
         {
            uid = gn.uid;
            break;
         }
      if (uid == s.lastSelectedNode)
         return;
      s.lastSelectedNode = uid;
      if (uid == 0)
         return;
      if (s.pickOnCanvas)
      {
         s.pickOnCanvas = false;
         if (Page() != nullptr)
         {
            const uint64_t lane = s.lane;
            ArrangeEdit([&] { Arrange::LinkLane(gArrange, lane, uid); });
         }
         return;
      }
      for (const Arrange::Lane& l : gArrange.lanes)
         if (l.type == Arrange::kLaneMidi && l.noteNodeUid == uid)
            s.lane = l.id;
   }

   void DrawDockMenu()
   {
      const ImVec2 mn = ImGui::GetWindowPos(), mx(mn.x + ImGui::GetWindowSize().x, mn.y + 2.0f * tok::tile + tok::space_3);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(mn, mx) && !ImGui::IsAnyItemHovered() &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
         ImGui::OpenPopup("##pianoDockMenu");
      if (MenuParts::BeginPopup("##pianoDockMenu"))
      {
         if (MenuParts::SubMenu(L("Dock Position")))
         {
            const bool top = gPianoRollDock == 3;
            if (MenuParts::Item(L("Piano Roll at Bottom"), nullptr, !top) && top)
               gPianoRollDock = 0;
            if (MenuParts::Item(L("Piano Roll at Top"), nullptr, top) && !top)
               gPianoRollDock = 3;
            ImGui::EndMenu();
         }
         ImGui::Dummy(ImVec2(0.0f, tok::space_1));
         if (MenuParts::Item(L("Close Piano Roll")))
            gPianoRollOpen = false;
         MenuParts::EndPopup();
      }
   }

   // The areas of a page, screen space, for this frame.
   struct Areas
   {
      float x0, x1;          // the timeline's left and right (the key column sits left of x0)
      float rulerY, stripY, rollY, rollB;
      float TickX(double t) const { return x0 + (float)((t - S().viewTick) * S().pxPerTick); }
      double XTick(float x) const { return S().viewTick + (double)(x - x0) / S().pxPerTick; }
      float RowY(float row) const { return rollY + (row - S().topRow) * S().keyH; }
   };

   void DrawRuler(ImDrawList* dl, const Areas& a)
   {
      const float y0 = a.rulerY, y1 = y0 + kRulerH;
      dl->AddRectFilled(ImVec2(a.x0, y0), ImVec2(a.x1, y1), Tint(0.04f));
      const Arrange::LoopRange& loop = gArrange.settings.loop;
      if (loop.end > loop.start)
         dl->AddRectFilled(ImVec2(a.TickX((double)loop.start), y0), ImVec2(a.TickX((double)loop.end), y1),
                           loop.enabled ? AccentU32(0.35f) : Tint(0.08f));
      if (S().rangeEnd > S().rangeStart)
         dl->AddRectFilled(ImVec2(a.TickX((double)S().rangeStart), y0), ImVec2(a.TickX((double)S().rangeEnd), y1), Tint(0.22f));
      const double barPx = (double)Arrange::kTicksPerBar * S().pxPerTick;
      int every = 1;
      while ((double)every * barPx < 40.0)
         every *= 2;
      const int first = (int)(a.XTick(a.x0) / (double)Arrange::kTicksPerBar) / every * every;
      for (int b = first;; b += every)
      {
         const float x = a.TickX((double)b * (double)Arrange::kTicksPerBar);
         if (x > a.x1)
            break;
         dl->AddLine(ImVec2(x, y1 - 5.0f), ImVec2(x, y1), Tint(0.4f));
         char label[16];
         std::snprintf(label, sizeof label, "%d", b + 1);
         dl->AddText(ImVec2(x + 3.0f, y0 + 1.0f), Tint(0.7f), label);
      }
   }

   void DrawClipStrip(ImDrawList* dl, const Areas& a, const Arrange::Lane& page)
   {
      const State& s = S();
      const float y0 = a.stripY + 1.0f, y1 = a.stripY + kStripH - 1.0f;
      for (const Arrange::Clip& c : page.clips)
      {
         const float cx0 = std::max(a.x0, a.TickX((double)c.start)), cx1 = std::min(a.x1, a.TickX((double)(c.start + c.length)));
         if (cx1 <= cx0)
            continue;
         const ImVec4 col = ClipColor(c, page);
         const bool on = s.clips.count(c.id) != 0;
         dl->AddRectFilled(ImVec2(cx0, y0), ImVec2(cx1, y1), WithAlpha(col, c.enabled ? (on ? 0.85f : 0.55f) : 0.2f), tok::radius_tile);
         if (on)
            dl->AddRect(ImVec2(cx0, y0), ImVec2(cx1, y1), Tint(0.9f), tok::radius_tile);
         if (!c.enabled || LinkedNode(page) == nullptr)
            DrawArrangeHatch(dl, ImVec2(cx0, y0), ImVec2(cx1, y1), Tint(0.25f));
         if (!c.name.empty())
         {
            dl->PushClipRect(ImVec2(cx0, y0), ImVec2(cx1, y1), true);
            dl->AddText(ImVec2(cx0 + tok::space_1, y0 + 2.0f), Tint(0.95f), c.name.c_str());
            dl->PopClipRect();
         }
      }
   }

   void DrawKeys(ImDrawList* dl, const Areas& a)
   {
      const State& s = S();
      const float kx0 = a.x0 - kKeysW, kx1 = a.x0 - 1.0f;
      dl->PushClipRect(ImVec2(kx0, a.rollY), ImVec2(kx1 + 1.0f, a.rollB), true);
      for (int r = (int)s.topRow; r < (int)s.rows.size(); r++)
      {
         const float y = a.RowY((float)r), y1 = y + s.keyH;
         if (y > a.rollB)
            break;
         const int p = s.rows[(size_t)r];
         const bool black = IsBlackKey(p);
         dl->AddRectFilled(ImVec2(kx0, y), ImVec2(black ? kx0 + (kx1 - kx0) * 0.62f : kx1, y1 - 1.0f), Tint(black ? 0.42f : 0.1f));
         if (p % 12 == 0 && s.keyH >= 9.0f)
            dl->AddText(ImVec2(kx1 - 24.0f, y + (s.keyH - ImGui::GetFontSize()) * 0.5f), Tint(0.8f), PitchName(p).c_str());
      }
      dl->PopClipRect();
   }

   void DrawGrid(ImDrawList* dl, const Areas& a)
   {
      const State& s = S();
      const int root = Transport::Instance().Key();
      for (int r = (int)s.topRow; r < (int)s.rows.size(); r++)
      {
         const float y = a.RowY((float)r);
         if (y > a.rollB)
            break;
         const int p = s.rows[(size_t)r];
         if ((p - root + 120) % 12 == 0)
            dl->AddRectFilled(ImVec2(a.x0, y), ImVec2(a.x1, y + s.keyH), AccentU32(0.12f));
         else if (InScale(p))
            dl->AddRectFilled(ImVec2(a.x0, y), ImVec2(a.x1, y + s.keyH), Tint(0.045f));
         dl->AddLine(ImVec2(a.x0, y), ImVec2(a.x1, y), Tint(p % 12 == 11 ? 0.16f : 0.04f));
      }
      Tick grid = ArrangeSnapGridTicks();
      if (grid <= 0)
         grid = Arrange::kPPQ / 4;
      while ((double)grid * s.pxPerTick < 8.0)
         grid *= 2;
      const Tick from = Arrange::GridFloor((Tick)a.XTick(a.x0), grid);
      for (Tick t = from;; t += grid)
      {
         const float x = a.TickX((double)t);
         if (x > a.x1)
            break;
         const float alpha = t % Arrange::kTicksPerBar == 0 ? 0.22f : t % Arrange::kPPQ == 0 ? 0.11f : 0.05f;
         dl->AddLine(ImVec2(x, a.rollY), ImVec2(x, a.rollB), Tint(alpha));
      }
   }

   void DrawClipsAndNotes(ImDrawList* dl, const Areas& a, const Arrange::Lane& page)
   {
      const State& s = S();
      const bool offline = LinkedNode(page) == nullptr;
      for (const Arrange::Clip& c : page.clips)
      {
         const float cx0 = a.TickX((double)c.start), cx1 = a.TickX((double)(c.start + c.length));
         if (cx1 < a.x0 || cx0 > a.x1)
            continue;
         const bool editing = s.clips.empty() || s.clips.count(c.id) != 0;
         const ImVec4 col = ClipColor(c, page);
         dl->AddRectFilled(ImVec2(std::max(a.x0, cx0), a.rollY), ImVec2(std::min(a.x1, cx1), a.rollB), WithAlpha(col, editing ? 0.09f : 0.04f));
         if (offline)
            DrawArrangeHatch(dl, ImVec2(std::max(a.x0, cx0), a.rollY), ImVec2(std::min(a.x1, cx1), a.rollB), Tint(0.07f));
         ForEachPlaced(c, [&](const Placed& pl) {
            const int row = s.rowOf[(size_t)std::clamp((int)pl.note->pitch + c.transpose, 0, 127)];
            if (row < 0)
               return;
            const float y = a.RowY((float)row), x0 = a.TickX((double)pl.start), x1 = std::max(x0 + 2.0f, a.TickX((double)pl.end));
            if (x1 < a.x0 || x0 > a.x1 || y + s.keyH < a.rollY || y > a.rollB)
               return;
            const bool sel = s.notes.count({ c.id, pl.note->id }) != 0;
            float depth = 0.3f + 0.7f * (float)pl.note->vel / 127.0f;
            if (!editing || pl.repeat)
               depth *= 0.35f;
            const ImVec2 p0(x0, y + 0.5f), p1(x1, y + s.keyH - 0.5f);
            const float round = std::min(tok::radius_tile * 0.5f, s.keyH * 0.25f);
            dl->AddRectFilled(p0, p1, WithAlpha(col, depth), round);
            dl->AddRect(p0, p1, sel ? Tint(1.0f) : WithAlpha(col, editing && !pl.repeat ? 1.0f : 0.45f), round);
            if (pl.note->mute || !c.enabled)
               DrawArrangeHatch(dl, p0, p1, Tint(0.45f), 4.0f);
            if (page.scaleSnap && !InScale(pl.note->pitch))
               dl->AddCircleFilled(ImVec2(x0 + 3.0f, y + s.keyH * 0.5f), 1.5f, Tint(0.9f));
         });
      }
   }

   bool IconChip(const char* id, const char* glyphStr, bool on, const char* tip)
   {
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const bool clicked = ActionButton::Draw(id, ImVec2(kIconW, tok::tile));
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (ImGui::IsItemHovered())
         HelpTip("%s", tip);
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), tok::icon_md,
                  on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text), glyphStr);
      return clicked;
   }

   // A magnifier with a plus or a minus, drawn on the last item.
   void DrawMagnifier(bool plus)
   {
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      const ImVec2 c((bmin.x + bmax.x) * 0.5f - 1.5f, (bmin.y + bmax.y) * 0.5f - 1.5f);
      const float r = tok::icon_md * 0.3f, d = r * 0.7071f, a = r * 0.5f;
      const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddCircle(c, r, col, 0, 1.5f);
      dl->AddLine(ImVec2(c.x + d, c.y + d), ImVec2(c.x + d + r * 0.75f, c.y + d + r * 0.75f), col, 2.0f);
      dl->AddLine(ImVec2(c.x - a, c.y), ImVec2(c.x + a, c.y), col, 1.5f);
      if (plus)
         dl->AddLine(ImVec2(c.x, c.y - a), ImVec2(c.x, c.y + a), col, 1.5f);
   }

   const char* GridLabel()
   {
      const Arrange::Settings& st = gArrange.settings;
      static char label[16];
      if (st.snapDivision == 0)
         return T("Off");
      if (st.snapDivision == 1)
         return T("Bar");
      std::snprintf(label, sizeof label, "1/%d%s", st.snapDivision, st.snapTriplet ? "T" : "");
      return label;
   }

   void DrawGridChip()
   {
      State& s = S();
      const std::string caption = std::string(GridLabel()) + "##pianoGrid";
      if (DropdownField::Draw(caption.c_str(), ImVec2(64.0f, 0.0f)))
         ImGui::OpenPopup("##pianoGridMenu");
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Grid, shared with the Timeline"));
      if (!MenuParts::BeginPopup("##pianoGridMenu"))
         return;
      const Arrange::Settings& st = gArrange.settings;
      if (MenuParts::Item(L("Bar"), nullptr, st.snapDivision == 1))
         ArrangeSetSnap(1, false);
      for (int div : { 2, 4, 8, 16, 32, 64 })
      {
         char label[16];
         std::snprintf(label, sizeof label, "1/%d", div);
         if (MenuParts::Item(label, nullptr, st.snapDivision == div))
         {
            s.lastDivision = div;
            ArrangeSetSnap(div, st.snapTriplet);
         }
      }
      MenuParts::Separator();
      if (MenuParts::Item(L("Triplet"), nullptr, st.snapTriplet))
         ArrangeSetSnap(st.snapDivision, !st.snapTriplet);
      MenuParts::EndPopup();
   }

   Arrange::Clip* ClipAt(Arrange::Lane& page, Tick t);
   Arrange::Clip* NewClipAt(Arrange::Lane& page, Tick tick);

   // The notes an edit works on: the selection, or with none every note of the clips being edited.
   std::map<uint64_t, std::vector<uint32_t>> TargetNotes(const Arrange::Lane& page)
   {
      const State& s = S();
      if (!s.notes.empty())
         return SelectionByClip();
      std::map<uint64_t, std::vector<uint32_t>> out;
      for (const Arrange::Clip& c : page.clips)
         if (s.clips.empty() || s.clips.count(c.id) != 0)
            for (const Arrange::Note& n : c.notes)
               out[c.id].push_back(n.id);
      return out;
   }

   // One undo step that hands every target clip's notes to `fn` and writes back what it leaves in the vector.
   template <class Fn>
   void TransformNotes(const Arrange::Lane& page, Fn&& fn)
   {
      const auto targets = TargetNotes(page);
      ArrangeEdit([&] {
         for (const auto& [clip, ids] : targets)
         {
            const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
            if (c == nullptr)
               continue;
            std::vector<Arrange::Note> notes;
            for (uint32_t id : ids)
               if (const Arrange::Note* n = Arrange::FindNote(*c, id))
                  notes.push_back(*n);
            std::sort(notes.begin(), notes.end(), [](const Arrange::Note& x, const Arrange::Note& y) { return x.start < y.start; });
            fn(notes);
            Arrange::EditNotes(gArrange, clip, notes);
         }
      });
   }

   void Quantize(const Arrange::Lane& page)
   {
      State& s = S();
      s.quant.grid = GridOrBeatSixteenth();
      const auto targets = TargetNotes(page);
      ArrangeEdit([&] {
         for (const auto& [clip, ids] : targets)
            Arrange::QuantizeNotes(gArrange, clip, ids, s.quant);
      });
   }

   void Legato(std::vector<Arrange::Note>& notes)
   {
      for (size_t i = 0; i + 1 < notes.size(); i++)
      {
         size_t next = i + 1;
         while (next < notes.size() && notes[next].start == notes[i].start)
            next++;
         if (next < notes.size())
            notes[i].length = notes[next].start - notes[i].start;
      }
   }

   void Humanize(std::vector<Arrange::Note>& notes)
   {
      State& s = S();
      std::mt19937 rng(s.humanizeSeed++);
      std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
      for (Arrange::Note& n : notes)
      {
         n.start = std::max<Tick>(0, n.start + (Tick)std::lround(unit(rng) * s.humanizeTime));
         n.vel = (uint8_t)std::clamp((int)n.vel + (int)std::lround(unit(rng) * s.humanizeVel), 1, 127);
      }
   }

   void Reverse(std::vector<Arrange::Note>& notes)
   {
      if (notes.empty())
         return;
      Tick lo = notes[0].start, hi = 0;
      for (const Arrange::Note& n : notes)
      {
         lo = std::min(lo, n.start);
         hi = std::max(hi, n.End());
      }
      for (Arrange::Note& n : notes)
         n.start = lo + hi - n.End();
   }

   void Invert(std::vector<Arrange::Note>& notes)
   {
      int lo = 127, hi = 0;
      for (const Arrange::Note& n : notes)
      {
         lo = std::min(lo, (int)n.pitch);
         hi = std::max(hi, (int)n.pitch);
      }
      for (Arrange::Note& n : notes)
         n.pitch = (uint8_t)(lo + hi - n.pitch);
   }

   void Stretch(std::vector<Arrange::Note>& notes, double factor)
   {
      if (notes.empty())
         return;
      const Tick from = notes[0].start;
      for (Arrange::Note& n : notes)
      {
         n.start = from + (Tick)std::llround((double)(n.start - from) * factor);
         n.length = std::max<Tick>(1, (Tick)std::llround((double)n.length * factor));
      }
   }

   void Strum(std::vector<Arrange::Note>& notes)
   {
      const State& s = S();
      for (size_t i = 0; i < notes.size();)
      {
         size_t end = i;
         while (end < notes.size() && notes[end].start == notes[i].start)
            end++;
         std::sort(notes.begin() + (long)i, notes.begin() + (long)end,
                   [](const Arrange::Note& x, const Arrange::Note& y) { return x.pitch < y.pitch; });
         const Tick at = notes[i].start;
         for (size_t k = i; k < end; k++)
         {
            const Tick d = (Tick)std::lround((float)(k - i) * s.strumTicks);
            notes[k].start = at + d;
            notes[k].length = std::max<Tick>(1, notes[k].length - d);
         }
         i = end;
      }
   }

   void VelocityRamp(std::vector<Arrange::Note>& notes)
   {
      if (notes.size() < 2)
         return;
      const float from = notes.front().vel, to = notes.back().vel;
      const float span = (float)(notes.back().start - notes.front().start);
      for (Arrange::Note& n : notes)
         n.vel = (uint8_t)std::clamp((int)std::lround(from + (to - from) * (span > 0.0f ? (float)(n.start - notes.front().start) / span : 0.0f)), 1, 127);
   }

   void FitToScale(std::vector<Arrange::Note>& notes)
   {
      Transport& tr = Transport::Instance();
      for (Arrange::Note& n : notes)
         n.pitch = (uint8_t)std::clamp(MusicTime::SnapToScale(n.pitch, tr.Key(), tr.Scale(), MusicTime::kSnapNearest), 0, 127);
   }

   // Cuts the target notes along the grid.
   void ChopToGrid(const Arrange::Lane& page)
   {
      const Tick grid = GridOrBeatSixteenth();
      const auto targets = TargetNotes(page);
      ArrangeEdit([&] {
         for (const auto& [clip, ids] : targets)
         {
            const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
            if (c == nullptr)
               continue;
            Tick lo = Arrange::kMaxTick, hi = 0;
            for (uint32_t id : ids)
               if (const Arrange::Note* n = Arrange::FindNote(*c, id))
               {
                  lo = std::min(lo, n->start);
                  hi = std::max(hi, n->End());
               }
            // New pieces get new ids: cut only the listed notes and the pieces cut from them.
            std::vector<uint32_t> sorted = ids;
            std::sort(sorted.begin(), sorted.end());
            const uint32_t firstNew = c->nextNoteId;
            for (Tick t = Arrange::GridCeil(lo + 1, grid); t < hi; t += grid)
            {
               std::vector<uint32_t> cut;
               for (const Arrange::Note& n : Arrange::FindClip(gArrange, clip)->notes)
                  if (n.id >= firstNew || std::binary_search(sorted.begin(), sorted.end(), n.id))
                     cut.push_back(n.id);
               Arrange::SplitNotes(gArrange, clip, cut, t);
            }
         }
      });
   }

   void DrawFunctionItems(const Arrange::Lane& page)
   {
      State& s = S();
      if (MenuParts::Item(L("Legato")))
         TransformNotes(page, Legato);
      if (MenuParts::Item(L("Humanize")))
         TransformNotes(page, Humanize);
      if (MenuParts::Item(L("Reverse")))
         TransformNotes(page, Reverse);
      if (MenuParts::Item(L("Invert")))
         TransformNotes(page, Invert);
      if (MenuParts::Item(L("Double Speed")))
         TransformNotes(page, [](std::vector<Arrange::Note>& n) { Stretch(n, 0.5); });
      if (MenuParts::Item(L("Half Speed")))
         TransformNotes(page, [](std::vector<Arrange::Note>& n) { Stretch(n, 2.0); });
      if (MenuParts::Item(L("Strum")))
         TransformNotes(page, Strum);
      if (MenuParts::Item(L("Chop to Grid")))
         ChopToGrid(page);
      if (MenuParts::Item(L("Velocity Ramp")))
         TransformNotes(page, VelocityRamp);
      if (MenuParts::Item(L("Fit to Scale")))
         TransformNotes(page, FitToScale);
      MenuParts::Separator();
      FormParts::SliderFloat(T("Strum"), &s.strumTicks, 0.0f, 240.0f, "%.0f ticks");
      FormParts::SliderFloat(T("Time"), &s.humanizeTime, 0.0f, 120.0f, "%.0f ticks");
      FormParts::SliderFloat(T("Velocity"), &s.humanizeVel, 0.0f, 40.0f, "%.0f");
   }

   void DrawQuantizeChips(const Arrange::Lane& page)
   {
      State& s = S();
      if (ChipButton::Draw("Q##pianoQ", false, tok::tile, kIconW))
         Quantize(page);
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Quantize to the grid (Q). Right-click or Shift+Q for strength, swing and ends"));
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || s.openQuantize)
         ImGui::OpenPopup("##pianoQuantize");
      s.openQuantize = false;
      if (MenuParts::BeginPopup("##pianoQuantize"))
      {
         FormParts::SliderFloat(T("Strength"), &s.quant.strength, 0.0f, 1.0f, "%.2f");
         FormParts::SliderFloat(T("Swing"), &s.quant.swing, 0.0f, 1.0f, "%.2f");
         FormParts::Switch(T("Ends"), &s.quant.ends);
         if (MenuParts::Item(L("Quantize")))
            Quantize(page);
         MenuParts::EndPopup();
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (ChipButton::Draw("fx##pianoFx", false, tok::tile, kIconW) || s.openFunctions)
         ImGui::OpenPopup("##pianoFunctions");
      s.openFunctions = false;
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Note functions"));
      if (MenuParts::BeginPopup("##pianoFunctions"))
      {
         DrawFunctionItems(page);
         MenuParts::EndPopup();
      }
   }

   void CopyNotes()
   {
      State& s = S();
      s.clipboard.clear();
      Tick first = Arrange::kMaxTick;
      for (const auto& [clip, ids] : SelectionByClip())
         if (const Arrange::Clip* c = Arrange::FindClip(gArrange, clip))
            for (uint32_t id : ids)
               if (const Arrange::Note* n = Arrange::FindNote(*c, id))
               {
                  s.clipboard.push_back(*n);
                  first = std::min(first, n->start);
               }
      for (Arrange::Note& n : s.clipboard)
         n.start -= first;
   }

   void DeleteSelection()
   {
      State& s = S();
      const auto sel = SelectionByClip();
      ArrangeEdit([&] {
         for (const auto& [clip, ids] : sel)
            Arrange::DeleteNotes(gArrange, clip, ids);
      });
      s.notes.clear();
   }

   void PasteNotes(Arrange::Lane& page, Tick at)
   {
      State& s = S();
      if (s.clipboard.empty())
         return;
      ArrangeEdit([&] {
         Arrange::Clip* c = ClipAt(page, at);
         if (c == nullptr)
            c = NewClipAt(page, at);
         if (c == nullptr)
            return;
         const Tick to = Arrange::MidiContentTick(*c, at - c->start);
         std::vector<Arrange::Note> notes = s.clipboard;
         for (Arrange::Note& n : notes)
            n.start += to;
         std::vector<uint32_t> ids;
         const uint64_t id = c->id;
         Arrange::AddNotes(gArrange, id, notes, &ids);
         s.notes.clear();
         for (uint32_t note : ids)
            s.notes.insert({ id, note });
      });
   }

   // Cmd+D: the selection again, right after itself.
   void DuplicateSelection()
   {
      State& s = S();
      std::map<uint64_t, std::vector<Arrange::Note>> copies;
      for (const auto& [clip, ids] : SelectionByClip())
         if (const Arrange::Clip* c = Arrange::FindClip(gArrange, clip))
         {
            Tick lo = Arrange::kMaxTick, hi = 0;
            for (uint32_t id : ids)
               if (const Arrange::Note* n = Arrange::FindNote(*c, id))
               {
                  copies[clip].push_back(*n);
                  lo = std::min(lo, n->start);
                  hi = std::max(hi, n->End());
               }
            for (Arrange::Note& n : copies[clip])
               n.start += hi - lo;
         }
      ArrangeEdit([&] {
         s.notes.clear();
         for (const auto& [clip, notes] : copies)
         {
            std::vector<uint32_t> ids;
            Arrange::AddNotes(gArrange, clip, notes, &ids);
            for (uint32_t id : ids)
               s.notes.insert({ clip, id });
         }
      });
   }

   void NudgePitch(const Arrange::Lane& page, int steps, bool octave)
   {
      const auto sel = SelectionByClip();
      const uint16_t mask = ScaleMask(page);
      ArrangeEdit([&] {
         for (const auto& [clip, ids] : sel)
         {
            if (octave)
            {
               Arrange::MoveNotes(gArrange, clip, ids, 0, steps * 12);
               continue;
            }
            if (mask == 0)
            {
               Arrange::MoveNotes(gArrange, clip, ids, 0, steps);
               continue;
            }
            // One scale degree: walk each note on to the next key of the scale.
            const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
            std::vector<Arrange::Note> edits;
            for (uint32_t id : ids)
               if (const Arrange::Note* found = Arrange::FindNote(*c, id))
               {
                  Arrange::Note n = *found;
                  int p = n.pitch + steps;
                  while (p >= 0 && p <= 127 && (mask & (1u << (p % 12))) == 0)
                     p += steps;
                  if (p >= 0 && p <= 127)
                     n.pitch = (uint8_t)p;
                  edits.push_back(n);
               }
            Arrange::EditNotes(gArrange, clip, edits);
         }
      });
   }

   void NudgeTime(int steps)
   {
      const auto sel = SelectionByClip();
      const Tick d = steps * GridOrBeatSixteenth();
      ArrangeEdit([&] {
         for (const auto& [clip, ids] : sel)
            Arrange::MoveNotes(gArrange, clip, ids, d, 0);
      });
   }

   void SelectAll(const Arrange::Lane& page)
   {
      State& s = S();
      s.notes.clear();
      for (const Arrange::Clip& c : page.clips)
         if (s.clips.empty() || s.clips.count(c.id) != 0)
            for (const Arrange::Note& n : c.notes)
               s.notes.insert({ c.id, n.id });
   }

   void JumpPage(const Arrange::Lane& page, int dir)
   {
      const std::vector<Arrange::Lane*> pages = Pages();
      const int n = (int)pages.size();
      for (int i = 0; i < n; i++)
         if (pages[(size_t)i]->id == page.id)
         {
            S().lane = pages[(size_t)((i + dir + n) % n)]->id;
            return;
         }
   }

   // Zooms to the selected notes, or the selected clips.
   void ZoomToSelection(const Arrange::Lane& page, float rollW)
   {
      State& s = S();
      Tick lo = Arrange::kMaxTick, hi = 0;
      for (const Arrange::Clip& c : page.clips)
      {
         if (s.notes.empty() && s.clips.count(c.id) != 0)
         {
            lo = std::min(lo, c.start);
            hi = std::max(hi, c.start + c.length);
         }
         ForEachPlaced(c, [&](const Placed& pl) {
            if (!pl.repeat && s.notes.count({ c.id, pl.note->id }) != 0)
            {
               lo = std::min(lo, pl.start);
               hi = std::max(hi, pl.end);
            }
         });
      }
      if (hi <= lo)
         return;
      s.pxPerTick = std::clamp((double)rollW * 0.9 / (double)(hi - lo), kMinPxPerTick, kMaxPxPerTick);
      s.viewTick = std::max(0.0, (double)lo - (double)rollW * 0.05 / s.pxPerTick);
   }

   void SetLoopFromSelection(const Arrange::Lane& page)
   {
      State& s = S();
      Tick lo = s.rangeStart, hi = s.rangeEnd;
      if (hi <= lo)
      {
         lo = Arrange::kMaxTick;
         hi = 0;
         for (const Arrange::Clip& c : page.clips)
            ForEachPlaced(c, [&](const Placed& pl) {
               if (!pl.repeat && s.notes.count({ c.id, pl.note->id }) != 0)
               {
                  lo = std::min(lo, pl.start);
                  hi = std::max(hi, pl.end);
               }
            });
      }
      const Arrange::LoopRange& loop = gArrange.settings.loop;
      if (hi > lo)
         ArrangeSetLoop(true, lo, hi);
      else
         ArrangeSetLoop(!loop.enabled, loop.start, loop.end);
   }

   void DrawToolbar(Arrange::Lane& page, float rollW, float rollH)
   {
      State& s = S();
      const auto tool = [&](const char* id, const char* g, Tool t, const char* tip) {
         if (IconChip(id, g, s.tool == t, tip))
            s.tool = t;
         FormParts::Inline(0.0f, tok::space_1);
      };
      tool("##toolSelect", IconsInfinite::Pointer, Tool::Select, T("Select (Esc)"));
      tool("##toolDraw", IconsInfinite::Pencil, Tool::Draw, T("Draw (D)"));
      tool("##toolErase", IconsInfinite::Erase, Tool::Erase, T("Erase (E)"));
      tool("##toolBlade", IconsInfinite::Scissors, Tool::Blade, T("Blade (B)"));
      tool("##toolMute", IconsInfinite::Close, Tool::Mute, T("Mute (0)"));
      FormParts::Inline(0.0f, tok::space_3);
      DrawGridChip();
      FormParts::Inline(0.0f, tok::space_1);
      const Arrange::Settings& st = gArrange.settings;
      if (IconChip("##pianoSnap", IconsInfinite::Snap, st.snapDivision != 0, T("Snap to the grid")))
      {
         if (st.snapDivision != 0)
            s.lastDivision = st.snapDivision;
         ArrangeSetSnap(st.snapDivision != 0 ? 0 : s.lastDivision, st.snapTriplet);
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##pianoScale", page.scaleSnap ? IconsInfinite::NoteSnapFill : IconsInfinite::NoteSnap, page.scaleSnap,
                   T("Snap drawn and moved notes to the scale")))
      {
         const uint64_t lane = page.id;
         ArrangeEdit([&] {
            Arrange::Lane* l = Arrange::FindLane(gArrange, lane);
            l->scaleSnap = !l->scaleSnap;
            gArrange.revision++;
         });
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##pianoFold", IconsInfinite::Fold, s.fold, T("Fold to the keys in use")))
         s.fold = !s.fold;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##pianoAudition", IconsInfinite::Audition, s.audition, T("Hear notes as you touch them")))
         s.audition = !s.audition;
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##pianoFollow", IconsInfinite::Follow, s.follow, T("Follow the playhead")))
         s.follow = !s.follow;
      FormParts::Inline(0.0f, tok::space_3);
      DrawQuantizeChips(page);
      const float zoomW = 3.0f * kIconW + 2.0f * tok::space_1;
      ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - zoomW));
      if (IconChip("##pianoZoomOut", IconsInfinite::Zoom, false, T("Zoom out (-)")))
         ZoomX(1.0 / 1.25, ImGui::GetWindowPos().x + rollW * 0.5f, ImGui::GetWindowPos().x);
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##pianoZoomIn", IconsInfinite::Zoom, false, T("Zoom in (=)")))
         ZoomX(1.25, ImGui::GetWindowPos().x + rollW * 0.5f, ImGui::GetWindowPos().x);
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##pianoZoomFit", IconsInfinite::Range, false, T("Fit the page (F)")))
         FitView(page, rollW, rollH);
   }

   struct NoteHit
   {
      uint64_t clip = 0;
      Arrange::Note note;
      Tick start = 0, end = 0; // on the timeline
      int edge = 0;            // -1 start, 1 end, 0 body
   };

   int RowAtY(const Areas& a, float y)
   {
      const State& s = S();
      return std::clamp((int)std::floor(s.topRow + (y - a.rollY) / s.keyH), 0, (int)s.rows.size() - 1);
   }

   bool HitNote(const Arrange::Lane& page, const Areas& a, ImVec2 m, NoteHit& hit)
   {
      const State& s = S();
      bool found = false;
      for (const Arrange::Clip& c : page.clips)
      {
         if (!s.clips.empty() && s.clips.count(c.id) == 0)
            continue;
         ForEachPlaced(c, [&](const Placed& pl) {
            const int row = s.rowOf[(size_t)std::clamp((int)pl.note->pitch + c.transpose, 0, 127)];
            if (row < 0 || m.y < a.RowY((float)row) || m.y >= a.RowY((float)row + 1.0f))
               return;
            const float x0 = a.TickX((double)pl.start), x1 = std::max(x0 + 2.0f, a.TickX((double)pl.end));
            if (m.x < x0 || m.x > x1)
               return;
            hit.clip = c.id;
            hit.note = *pl.note;
            hit.start = pl.start;
            hit.end = pl.end;
            const float margin = std::min(5.0f, (x1 - x0) * 0.3f);
            hit.edge = m.x > x1 - margin ? 1 : m.x < x0 + margin ? -1 : 0;
            found = true;
         });
      }
      return found;
   }

   Arrange::Clip* ClipAt(Arrange::Lane& page, Tick t)
   {
      for (Arrange::Clip& c : page.clips)
         if (t >= c.start && t < c.start + c.length)
            return &c;
      return nullptr;
   }

   void BeginDrag(Drag d, const Arrange::Lane& page, ImVec2 mouse, Tick tick, int pitch, Tick anchor)
   {
      State& s = S();
      ArrangeGestureBegin();
      s.drag = d;
      s.base = page;
      s.dragFrom = mouse;
      s.dragTick = tick;
      s.dragPitch = pitch;
      s.anchor = anchor;
      s.lastDeltaT = 0;
      s.lastDeltaP = 0;
      s.rubbed.clear();
   }

   // A one-bar clip at the bar under `tick`, for drawing on an empty stretch.
   Arrange::Clip* NewClipAt(Arrange::Lane& page, Tick tick)
   {
      Arrange::Clip c;
      c.start = Arrange::GridFloor(tick, Arrange::kTicksPerBar);
      c.length = Arrange::kTicksPerBar;
      uint64_t id = 0;
      if (!Arrange::PlaceOverwrite(gArrange, page.id, c, &id) || id == 0)
         return nullptr;
      return Arrange::FindClip(gArrange, id);
   }

   // Draws one note under the pointer and makes it the run's current note.
   void PaintNote(Arrange::Lane& page, Tick tick, int pitch)
   {
      State& s = S();
      Arrange::Clip* c = ClipAt(page, tick);
      if (c == nullptr)
         c = NewClipAt(page, tick);
      if (c == nullptr)
         return;
      const Tick at = FloorTick(tick);
      Arrange::Note n;
      n.start = Arrange::MidiContentTick(*c, std::max<Tick>(0, at - c->start));
      n.length = s.lastLength;
      n.pitch = (decltype(n.pitch))pitch;
      n.vel = (decltype(n.vel))s.lastVelocity;
      std::vector<uint32_t> ids;
      if (!Arrange::AddNotes(gArrange, c->id, { n }, &ids) || ids.empty())
         return;
      s.paintClip = c->id;
      s.paintNote = ids[0];
      s.anchor = at;
      Sound(page, pitch, s.lastVelocity, 0.25);
   }

   void PressRoll(Arrange::Lane& page, const Areas& a, ImVec2 m)
   {
      State& s = S();
      const ImGuiIO& io = ImGui::GetIO();
      const Tick tick = (Tick)std::max(0.0, a.XTick(m.x));
      const int row = RowAtY(a, m.y), pitch = s.rows[(size_t)row];
      NoteHit hit;
      const bool onNote = HitNote(page, a, m, hit);
      const bool cmd = io.KeyCtrl || io.KeySuper;
      switch (s.tool)
      {
      case Tool::Draw:
      {
         BeginDrag(Drag::Paint, page, m, tick, pitch, tick);
         s.paintRow = row;
         PaintNote(page, tick, SnapPitch(page, pitch));
         return;
      }
      case Tool::Erase:
      case Tool::Mute:
         BeginDrag(Drag::Rub, page, m, tick, pitch, tick);
         return;
      case Tool::Blade:
         if (onNote)
            ArrangeEdit([&] {
               const Arrange::Clip* c = Arrange::FindClip(gArrange, hit.clip);
               if (c != nullptr)
                  Arrange::SplitNotes(gArrange, hit.clip, { hit.note.id }, hit.note.start + FloorTick(tick) - hit.start);
            });
         return;
      case Tool::Select:
         break;
      }
      if (!onNote)
      {
         if (!io.KeyShift && !cmd)
            s.notes.clear();
         s.drag = Drag::Marquee;
         s.dragFrom = m;
         return;
      }
      const std::pair<uint64_t, uint32_t> key { hit.clip, hit.note.id };
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         ArrangeEdit([&] { Arrange::DeleteNotes(gArrange, hit.clip, { hit.note.id }); });
         s.notes.erase(key);
         return;
      }
      if (cmd && s.notes.count(key) != 0)
      {
         s.notes.erase(key);
         return;
      }
      if (s.notes.count(key) == 0)
      {
         if (!io.KeyShift && !cmd)
            s.notes.clear();
         s.notes.insert(key);
      }
      Sound(page, hit.note.pitch, hit.note.vel);
      s.lastLength = hit.note.length;
      s.lastVelocity = hit.note.vel;
      const Drag d = hit.edge < 0 ? Drag::SizeStart : hit.edge > 0 ? Drag::SizeEnd : Drag::MoveNotes;
      BeginDrag(d, page, m, tick, pitch, hit.edge > 0 ? hit.end : hit.start);
      s.dragNotes = SelectionByClip();
      s.dragCopies = d == Drag::MoveNotes && io.KeyAlt;
   }

   // Alt-drag: the originals stay, and each step lays fresh copies at the offset. Copies wait for a real offset so
   // they never land on their originals (overlap resolving would eat one).
   void LayCopies(const Arrange::Lane& page, Tick dT, int dP)
   {
      State& s = S();
      s.notes.clear();
      for (const auto& [clip, ids] : s.dragNotes)
      {
         const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
         if (c == nullptr)
            continue;
         std::vector<Arrange::Note> dup;
         for (uint32_t id : ids)
            if (const Arrange::Note* found = Arrange::FindNote(*c, id))
            {
               Arrange::Note n = *found;
               n.start = std::max<Tick>(0, n.start + dT);
               n.pitch = (decltype(n.pitch))SnapPitch(page, std::clamp((int)n.pitch + dP, 0, 127));
               dup.push_back(n);
            }
         std::vector<uint32_t> out;
         Arrange::AddNotes(gArrange, clip, dup, &out);
         for (uint32_t id : out)
            s.notes.insert({ clip, id });
      }
   }

   void StepDrag(Arrange::Lane& page, const Areas& a, ImVec2 m)
   {
      State& s = S();
      const Tick tick = (Tick)std::max(0.0, a.XTick(m.x));
      const int row = RowAtY(a, m.y), pitch = s.rows[(size_t)row];
      const auto restore = [&] {
         const uint64_t lane = page.id;
         page = s.base;
         page.id = lane;
         gArrange.revision++;
      };
      switch (s.drag)
      {
      case Drag::MoveNotes:
      {
         const Tick dT = SnapTick(s.anchor + tick - s.dragTick) - s.anchor;
         const int dP = pitch - s.dragPitch;
         if (dT == s.lastDeltaT && dP == s.lastDeltaP)
            break;
         s.lastDeltaT = dT;
         s.lastDeltaP = dP;
         restore();
         if (!s.dragCopies)
            for (const auto& [clip, ids] : s.dragNotes)
               Arrange::MoveNotes(gArrange, clip, ids, dT, dP, ScaleMask(page));
         else if (dT != 0 || dP != 0)
            LayCopies(page, dT, dP);
         else
         {
            s.notes.clear();
            for (const auto& [clip, ids] : s.dragNotes)
               for (uint32_t id : ids)
                  s.notes.insert({ clip, id });
         }
         Sound(page, SnapPitch(page, s.dragPitch + dP), s.lastVelocity);
         break;
      }
      case Drag::SizeStart:
      case Drag::SizeEnd:
      {
         const Tick dT = SnapTick(s.anchor + tick - s.dragTick) - s.anchor;
         if (dT == s.lastDeltaT)
            break;
         s.lastDeltaT = dT;
         restore();
         const Tick minLen = std::max<Tick>(1, GridOrBeatSixteenth() / 4);
         for (const auto& [clip, ids] : s.dragNotes)
         {
            const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
            if (c == nullptr)
               continue;
            std::vector<Arrange::Note> edits;
            for (uint32_t id : ids)
               if (const Arrange::Note* found = Arrange::FindNote(*c, id))
               {
                  Arrange::Note n = *found;
                  if (s.drag == Drag::SizeEnd)
                     n.length = std::max(minLen, n.length + dT);
                  else
                  {
                     const Tick d = std::clamp(dT, -n.start, n.length - minLen);
                     n.start += d;
                     n.length -= d;
                  }
                  edits.push_back(n);
               }
            Arrange::EditNotes(gArrange, clip, edits);
         }
         break;
      }
      case Drag::Paint:
      {
         if (row != s.paintRow)
         {
            s.paintRow = row;
            PaintNote(page, tick, SnapPitch(page, pitch));
            break;
         }
         const Arrange::Clip* c = Arrange::FindClip(gArrange, s.paintClip);
         const Arrange::Note* n = c != nullptr ? Arrange::FindNote(*c, s.paintNote) : nullptr;
         if (n == nullptr)
            break;
         const Tick grid = GridOrBeatSixteenth();
         const Tick length = std::max(grid, Arrange::GridCeil(std::max<Tick>(1, tick - s.anchor), grid));
         if (length != n->length && tick > s.anchor + grid)
         {
            Arrange::Note edit = *n;
            edit.length = length;
            s.lastLength = length;
            Arrange::EditNotes(gArrange, s.paintClip, { edit });
         }
         break;
      }
      case Drag::Rub:
      {
         NoteHit hit;
         if (!HitNote(page, a, m, hit) || !s.rubbed.insert({ hit.clip, hit.note.id }).second)
            break;
         if (s.tool == Tool::Erase)
            Arrange::DeleteNotes(gArrange, hit.clip, { hit.note.id });
         else
         {
            hit.note.mute = !hit.note.mute;
            Arrange::EditNotes(gArrange, hit.clip, { hit.note });
         }
         break;
      }
      case Drag::MoveClips:
      {
         const Tick dT = SnapTick(s.anchor + tick - s.dragTick) - s.anchor;
         if (dT == s.lastDeltaT)
            break;
         s.lastDeltaT = dT;
         restore();
         Arrange::MoveClips(gArrange, std::vector<uint64_t>(s.clips.begin(), s.clips.end()), dT, 0);
         break;
      }
      case Drag::TrimStart:
      case Drag::TrimEnd:
      {
         const Tick at = SnapTick(s.anchor + tick - s.dragTick);
         if (at == s.lastDeltaT)
            break;
         s.lastDeltaT = at;
         restore();
         Arrange::TrimEdge(gArrange, s.paintClip, s.drag == Drag::TrimStart ? Arrange::kEdgeStart : Arrange::kEdgeEnd, at);
         break;
      }
      case Drag::Range:
         s.rangeStart = std::min(s.dragTick, SnapTick(tick));
         s.rangeEnd = std::max(s.dragTick, SnapTick(tick));
         break;
      case Drag::LoopStart:
      case Drag::LoopEnd:
      {
         const Arrange::LoopRange loop = gArrange.settings.loop;
         const Tick at = SnapTick(tick);
         if (s.drag == Drag::LoopStart && at < loop.end)
            ArrangeSetLoop(loop.enabled, at, loop.end);
         else if (s.drag == Drag::LoopEnd && at > loop.start)
            ArrangeSetLoop(loop.enabled, loop.start, at);
         break;
      }
      case Drag::Keys:
         Sound(page, pitch, 100);
         break;
      default:
         break;
      }
   }

   void EndDrag(Arrange::Lane& page, const Areas& a, ImVec2 m)
   {
      State& s = S();
      if (s.drag == Drag::Marquee)
      {
         const float x0 = std::min(m.x, s.dragFrom.x), x1 = std::max(m.x, s.dragFrom.x);
         const float y0 = std::min(m.y, s.dragFrom.y), y1 = std::max(m.y, s.dragFrom.y);
         for (const Arrange::Clip& c : page.clips)
         {
            if (!s.clips.empty() && s.clips.count(c.id) == 0)
               continue;
            ForEachPlaced(c, [&](const Placed& pl) {
               const int row = s.rowOf[(size_t)std::clamp((int)pl.note->pitch + c.transpose, 0, 127)];
               if (row < 0)
                  return;
               const float y = a.RowY((float)row);
               if (a.TickX((double)pl.end) >= x0 && a.TickX((double)pl.start) <= x1 && y + s.keyH >= y0 && y <= y1)
                  s.notes.insert({ c.id, pl.note->id });
            });
         }
      }
      else if (s.drag == Drag::Range)
      {
         if (std::fabs(m.x - s.dragFrom.x) < 3.0f)
         {
            s.rangeStart = s.rangeEnd = 0;
            ArrangeSeekTick(SnapTick((Tick)std::max(0.0, a.XTick(m.x))));
         }
      }
      else if (s.drag != Drag::None)
      {
         // Keep the last-used length for the next drawn note.
         if (s.drag == Drag::SizeEnd || s.drag == Drag::SizeStart)
            if (!s.notes.empty())
            {
               const auto [clip, id] = *s.notes.begin();
               const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
               if (const Arrange::Note* n = c != nullptr ? Arrange::FindNote(*c, id) : nullptr)
                  s.lastLength = n->length;
            }
         ArrangeGestureEnd();
      }
      s.drag = Drag::None;
      s.base = Arrange::Lane {};
      if (s.soundingUntil <= 0.0)
         StopSound(page);
   }

   void PressStrip(Arrange::Lane& page, const Areas& a, ImVec2 m)
   {
      State& s = S();
      const ImGuiIO& io = ImGui::GetIO();
      const Tick tick = (Tick)std::max(0.0, a.XTick(m.x));
      Arrange::Clip* c = ClipAt(page, tick);
      if (c == nullptr)
      {
         if (s.tool == Tool::Draw)
         {
            ArrangeEdit([&] { NewClipAt(page, tick); });
            return;
         }
         s.clips.clear();
         return;
      }
      if (s.tool == Tool::Blade)
      {
         const uint64_t id = c->id;
         ArrangeEdit([&] { Arrange::Split(gArrange, id, SnapTick(tick), nullptr); });
         return;
      }
      if (s.tool == Tool::Erase)
      {
         const uint64_t id = c->id;
         ArrangeEdit([&] { Arrange::Delete(gArrange, { id }); });
         return;
      }
      if (s.tool == Tool::Mute)
      {
         const uint64_t id = c->id;
         ArrangeEdit([&] { Arrange::SetEnabled(gArrange, { id }, Arrange::kToggle); });
         return;
      }
      if (io.KeyShift || io.KeyCtrl || io.KeySuper)
      {
         if (!s.clips.insert(c->id).second)
         {
            s.clips.erase(c->id);
            return;
         }
      }
      else if (s.clips.count(c->id) == 0)
      {
         s.clips.clear();
         s.clips.insert(c->id);
      }
      const float x0 = a.TickX((double)c->start), x1 = a.TickX((double)(c->start + c->length));
      const bool left = m.x < x0 + 5.0f, right = m.x > x1 - 5.0f;
      const Tick anchor = right ? c->start + c->length : c->start;
      s.paintClip = c->id;
      BeginDrag(left ? Drag::TrimStart : right ? Drag::TrimEnd : Drag::MoveClips, page, m, tick, 0, anchor);
      if (!left && !right && io.KeyAlt)
      {
         std::vector<uint64_t> fresh;
         Arrange::DuplicateBlock(gArrange, std::vector<uint64_t>(s.clips.begin(), s.clips.end()), &fresh);
         s.clips = std::set<uint64_t>(fresh.begin(), fresh.end());
         s.base = page;
      }
      s.lastDeltaT = left || right ? anchor : 0;
   }

   void PressRuler(const Arrange::Lane& page, const Areas& a, ImVec2 m)
   {
      State& s = S();
      const Tick tick = (Tick)std::max(0.0, a.XTick(m.x));
      const Arrange::LoopRange& loop = gArrange.settings.loop;
      if (loop.end > loop.start && std::fabs(m.x - a.TickX((double)loop.start)) < 5.0f)
         BeginDrag(Drag::LoopStart, page, m, tick, 0, loop.start);
      else if (loop.end > loop.start && std::fabs(m.x - a.TickX((double)loop.end)) < 5.0f)
         BeginDrag(Drag::LoopEnd, page, m, tick, 0, loop.end);
      else
      {
         s.drag = Drag::Range;
         s.dragFrom = m;
         s.dragTick = SnapTick(tick);
      }
   }

   void HandlePointer(Arrange::Lane& page, const Areas& a, bool hovered, ImVec2 m)
   {
      State& s = S();
      if (hovered && s.drag == Drag::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         if (m.x < a.x0 && m.y >= a.rollY)
         {
            s.drag = Drag::Keys;
            s.dragFrom = m;
            Sound(page, s.rows[(size_t)RowAtY(a, m.y)], 100);
         }
         else if (m.y < a.stripY)
            PressRuler(page, a, m);
         else if (m.y < a.rollY)
            PressStrip(page, a, m);
         else
            PressRoll(page, a, m);
      }
      if (s.drag != Drag::None)
      {
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            StepDrag(page, a, m);
            if (m.x > a.x1)
               s.viewTick += 8.0 / s.pxPerTick;
            else if (m.x < a.x0 && s.drag != Drag::Keys)
               s.viewTick -= 8.0 / s.pxPerTick;
         }
         else
            EndDrag(page, a, m);
      }
      if (s.soundingUntil > 0.0 && ImGui::GetTime() > s.soundingUntil)
         StopSound(page);
      if (s.drag == Drag::None && hovered && m.y >= a.rollY && m.x >= a.x0 && s.tool == Tool::Select)
      {
         NoteHit hit;
         if (HitNote(page, a, m, hit) && hit.edge != 0)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }
   }

   bool IsCurve(Expr e) { return e == Expr::Pitch || e == Expr::Pressure || e == Expr::Timbre; }

   std::vector<Arrange::ExprPoint>* Curve(Arrange::Note& n, Expr e)
   {
      return e == Expr::Pitch ? &n.bend : e == Expr::Pressure ? &n.pressure : &n.timbre;
   }

   // 0..1 up the lane. Bend spans an octave each way.
   float LaneValue(float v, Expr e) { return e == Expr::Pitch ? v / 24.0f + 0.5f : v; }
   float ModelValue(float t, Expr e) { return e == Expr::Pitch ? (t - 0.5f) * 24.0f : t; }

   float BarValue(const Arrange::Note& n, Expr e)
   {
      return e == Expr::Velocity ? (float)n.vel / 127.0f : e == Expr::Release ? (float)n.relVel / 127.0f : n.chance;
   }

   void SetBarValue(Arrange::Note& n, Expr e, float t)
   {
      if (e == Expr::Velocity)
         n.vel = (uint8_t)std::clamp((int)std::lround(t * 127.0f), 1, 127);
      else if (e == Expr::Release)
         n.relVel = (uint8_t)std::clamp((int)std::lround(t * 127.0f), 0, 127);
      else
         n.chance = std::clamp(t, 0.0f, 1.0f);
   }

   // Calls fn(clip, note copy, x of the note) for the notes the lane may touch: the selection, or with none every
   // note of the clips being edited. Edits fn makes are written back.
   template <class Fn>
   void EditLaneNotes(const Arrange::Lane& page, const Areas& a, Fn&& fn)
   {
      const State& s = S();
      for (const Arrange::Clip& c : page.clips)
      {
         if (!s.clips.empty() && s.clips.count(c.id) == 0)
            continue;
         std::vector<Arrange::Note> edits;
         ForEachPlaced(c, [&](const Placed& pl) {
            if (pl.repeat || (!s.notes.empty() && s.notes.count({ c.id, pl.note->id }) == 0))
               return;
            Arrange::Note n = *pl.note;
            if (fn(n, a.TickX((double)pl.start), pl.start))
               edits.push_back(n);
         });
         if (!edits.empty())
            Arrange::EditNotes(gArrange, c.id, edits);
      }
   }

   void DrawExpressionMarks(ImDrawList* dl, const Areas& a, const Arrange::Lane& page, float y0, float y1)
   {
      const State& s = S();
      const auto yOf = [&](float t) { return y1 - 2.0f - std::clamp(t, 0.0f, 1.0f) * (y1 - y0 - 4.0f); };
      if (s.expr == Expr::Pitch)
         dl->AddLine(ImVec2(a.x0, yOf(0.5f)), ImVec2(a.x1, yOf(0.5f)), Tint(0.14f));
      for (const Arrange::Clip& c : page.clips)
      {
         const ImVec4 col = ClipColor(c, page);
         const bool editing = s.clips.empty() || s.clips.count(c.id) != 0;
         ForEachPlaced(c, [&](const Placed& pl) {
            const float x = a.TickX((double)pl.start);
            const bool sel = s.notes.count({ c.id, pl.note->id }) != 0;
            const float alpha = !editing ? 0.25f : pl.repeat ? 0.35f : 1.0f;
            if (a.TickX((double)pl.end) < a.x0 || x > a.x1)
               return;
            if (!IsCurve(s.expr))
            {
               const float y = yOf(BarValue(*pl.note, s.expr));
               dl->AddLine(ImVec2(x, y1), ImVec2(x, y), WithAlpha(col, 0.7f * alpha), 1.5f);
               dl->AddCircleFilled(ImVec2(x, y), 3.0f, sel ? Tint(1.0f) : WithAlpha(col, alpha));
               return;
            }
            Arrange::Note n = *pl.note;
            const std::vector<Arrange::ExprPoint>& pts = *Curve(n, s.expr);
            const ImU32 line = sel ? Tint(1.0f) : WithAlpha(col, alpha);
            if (pts.empty())
            {
               const float y = yOf(LaneValue(0.0f, s.expr));
               dl->AddLine(ImVec2(x, y), ImVec2(a.TickX((double)pl.end), y), WithAlpha(col, 0.3f * alpha));
               return;
            }
            ImVec2 last(x, yOf(LaneValue(pts[0].v, s.expr)));
            for (const Arrange::ExprPoint& q : pts)
            {
               const ImVec2 p(a.TickX((double)(pl.start + q.at)), yOf(LaneValue(q.v, s.expr)));
               dl->AddLine(last, p, line, 1.5f);
               last = p;
            }
            dl->AddLine(last, ImVec2(a.TickX((double)pl.end), last.y), line, 1.5f);
         });
      }
   }

   void StepExpression(Arrange::Lane& page, const Areas& a, ImVec2 m, float y0, float y1)
   {
      State& s = S();
      const ImGuiIO& io = ImGui::GetIO();
      const auto valueAt = [&](float y) { return std::clamp((y1 - 2.0f - y) / (y1 - y0 - 4.0f), 0.0f, 1.0f); };
      if (IsCurve(s.expr))
      {
         const Tick tick = (Tick)std::max(0.0, a.XTick(m.x));
         EditLaneNotes(page, a, [&](Arrange::Note& n, float, Tick at) {
            if (tick < at || tick >= at + n.length)
               return false;
            std::vector<Arrange::ExprPoint>& pts = *Curve(n, s.expr);
            if (io.KeyAlt)
            {
               pts.clear();
               return true;
            }
            const Tick local = tick - at, near = std::max<Tick>(1, n.length / 64);
            pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const Arrange::ExprPoint& q) { return std::llabs(q.at - local) <= near; }),
                      pts.end());
            pts.insert(std::lower_bound(pts.begin(), pts.end(), local, [](const Arrange::ExprPoint& q, Tick t) { return q.at < t; }),
                       Arrange::ExprPoint { local, ModelValue(valueAt(m.y), s.expr) });
            return true;
         });
         return;
      }
      if (s.tool == Tool::Draw) // a ramp, from the press to the pointer
      {
         page = s.base;
         gArrange.revision++;
         const float x0 = std::min(s.exprFrom.x, m.x), x1 = std::max(s.exprFrom.x, m.x);
         const float v0 = valueAt(m.x >= s.exprFrom.x ? s.exprFrom.y : m.y), v1 = valueAt(m.x >= s.exprFrom.x ? m.y : s.exprFrom.y);
         EditLaneNotes(page, a, [&](Arrange::Note& n, float x, Tick) {
            if (x < x0 || x > x1)
               return false;
            SetBarValue(n, s.expr, v0 + (v1 - v0) * (x1 > x0 ? (x - x0) / (x1 - x0) : 0.0f));
            return true;
         });
         return;
      }
      EditLaneNotes(page, a, [&](Arrange::Note& n, float x, Tick) {
         if (std::fabs(x - m.x) > 4.0f)
            return false;
         SetBarValue(n, s.expr, valueAt(m.y));
         return true;
      });
   }

   void DrawExpression(Arrange::Lane& page, const Areas& a, float top)
   {
      State& s = S();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float left = a.x0 - kKeysW, width = a.x1 - left;
      ImGui::SetCursorScreenPos(ImVec2(left, top));
      ImGui::InvisibleButton("##exprSplit", ImVec2(width, kSplitH));
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
         s.exprH = std::clamp(s.exprH - ImGui::GetIO().MouseDelta.y, kMinExprH, kMaxExprH);
      static const char* const kNames[] = { "Velocity", "Release", "Chance", "Pitch", "Pressure", "Timbre" };
      ImGui::SetCursorScreenPos(ImVec2(left, top + kSplitH));
      for (int i = 0; i < 6; i++)
      {
         const std::string label = std::string(T(kNames[i])) + "##expr" + std::to_string(i);
         if (ChipButton::Draw(label.c_str(), (int)s.expr == i, tok::tile))
            s.expr = (Expr)i;
         if (i < 5)
            FormParts::Inline(0.0f, tok::space_1);
      }
      const float y0 = top + kSplitH + tok::tile + tok::space_1, y1 = y0 + s.exprH;
      ImGui::SetCursorScreenPos(ImVec2(left, y0));
      ImGui::InvisibleButton("##exprLane", ImVec2(width, s.exprH));
      const ImVec2 m = ImGui::GetIO().MousePos;
      if (ImGui::IsItemActivated())
      {
         ArrangeGestureBegin();
         s.exprDrag = true;
         s.exprFrom = m;
         s.base = page;
      }
      if (s.exprDrag)
      {
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            StepExpression(page, a, m, y0, y1);
         else
         {
            s.exprDrag = false;
            s.base = Arrange::Lane {};
            ArrangeGestureEnd();
         }
      }
      dl->PushClipRect(ImVec2(a.x0, y0), ImVec2(a.x1, y1), true);
      dl->AddRectFilled(ImVec2(a.x0, y0), ImVec2(a.x1, y1), Tint(0.04f));
      DrawExpressionMarks(dl, a, page, y0, y1);
      if (s.exprDrag && s.tool == Tool::Draw && !IsCurve(s.expr))
         dl->AddLine(s.exprFrom, m, AccentU32(0.9f), 1.5f);
      dl->PopClipRect();
   }

   // One value over the selected notes. A drag moves every note by the same amount; a typed value sets them all.
   template <class Get, class Set>
   void NoteField(Arrange::Lane& page, const char* label, float lo, float hi, const char* fmt, Get get, Set set, ImGuiSliderFlags flags = 0)
   {
      State& s = S();
      bool any = false, mixed = false;
      float first = 0.0f;
      for (const auto& [clip, id] : s.notes)
      {
         const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
         const Arrange::Note* n = c != nullptr ? Arrange::FindNote(*c, id) : nullptr;
         if (n == nullptr)
            continue;
         const float v = get(*n);
         mixed = mixed || (any && v != first);
         first = any ? first : v;
         any = true;
      }
      float v = first;
      const bool changed = FormParts::SliderFloat(label, &v, lo, hi, mixed ? T("mixed") : fmt, flags);
      if (ImGui::IsItemActivated())
      {
         ArrangeGestureBegin();
         s.fieldBase = page;
         s.fieldFrom = first;
      }
      if (changed)
      {
         page = s.fieldBase;
         gArrange.revision++;
         const bool typed = ImGui::GetIO().WantTextInput;
         for (const auto& [clip, ids] : SelectionByClip())
         {
            const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
            if (c == nullptr)
               continue;
            std::vector<Arrange::Note> edits;
            for (uint32_t id : ids)
               if (const Arrange::Note* found = Arrange::FindNote(*c, id))
               {
                  Arrange::Note n = *found;
                  set(n, std::clamp(typed ? v : get(n) + v - s.fieldFrom, lo, hi));
                  edits.push_back(n);
               }
            Arrange::EditNotes(gArrange, clip, edits);
         }
      }
      if (ImGui::IsItemDeactivated())
      {
         s.fieldBase = Arrange::Lane {};
         ArrangeGestureEnd();
      }
   }

   // A clip field, the same value on every selected clip.
   template <class Field>
   void ClipField(const char* label, float lo, float hi, const char* fmt, Field field)
   {
      const State& s = S();
      Arrange::Clip* c0 = Arrange::FindClip(gArrange, *s.clips.begin());
      if (c0 == nullptr)
         return;
      float v = field(*c0);
      const bool changed = FormParts::SliderFloat(label, &v, lo, hi, fmt);
      if (ImGui::IsItemActivated())
         ArrangeGestureBegin();
      if (changed)
      {
         for (uint64_t id : s.clips)
            if (Arrange::Clip* c = Arrange::FindClip(gArrange, id))
               field(*c) = v;
         gArrange.revision++;
      }
      if (ImGui::IsItemDeactivated())
         ArrangeGestureEnd();
   }

   float ExprAtStart(const std::vector<Arrange::ExprPoint>& pts) { return pts.empty() ? 0.0f : pts[0].v; }

   void SetExprAtStart(std::vector<Arrange::ExprPoint>& pts, float v)
   {
      if (!pts.empty() && pts[0].at == 0)
         pts[0].v = v;
      else
         pts.insert(pts.begin(), Arrange::ExprPoint { 0, v });
   }

   void DrawNoteInspector(Arrange::Lane& page)
   {
      State& s = S();
      using Note = Arrange::Note;
      SectionCard::Begin(s.notes.size() == 1 ? T("Note") : T("Notes"));
      const auto beats = [](Tick t) { return (float)t / (float)Arrange::kPPQ; };
      const auto ticks = [](float b) { return (Tick)std::lround(b * (float)Arrange::kPPQ); };
      NoteField(page, T("Pitch"), 0.0f, 127.0f, "%.0f", [](const Note& n) { return (float)n.pitch; },
                [](Note& n, float v) { n.pitch = (uint8_t)std::lround(v); });
      NoteField(page, T("Start"), 0.0f, 256.0f, "%.3f", [&](const Note& n) { return beats(n.start); },
                [&](Note& n, float v) { n.start = ticks(v); }, ImGuiSliderFlags_Logarithmic);
      NoteField(page, T("Length"), 0.015f, 64.0f, "%.3f", [&](const Note& n) { return beats(n.length); },
                [&](Note& n, float v) { n.length = std::max<Tick>(1, ticks(v)); }, ImGuiSliderFlags_Logarithmic);
      NoteField(page, T("Velocity"), 1.0f, 127.0f, "%.0f", [](const Note& n) { return (float)n.vel; },
                [](Note& n, float v) { n.vel = (uint8_t)std::lround(v); });
      NoteField(page, T("Release"), 0.0f, 127.0f, "%.0f", [](const Note& n) { return (float)n.relVel; },
                [](Note& n, float v) { n.relVel = (uint8_t)std::lround(v); });
      NoteField(page, T("Chance"), 0.0f, 1.0f, "%.2f", [](const Note& n) { return n.chance; }, [](Note& n, float v) { n.chance = v; });
      bool allMuted = true;
      for (const auto& [clip, id] : s.notes)
      {
         const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
         const Arrange::Note* n = c != nullptr ? Arrange::FindNote(*c, id) : nullptr;
         allMuted = allMuted && n != nullptr && n->mute;
      }
      bool mute = allMuted;
      if (FormParts::Switch(T("Mute"), &mute))
         ArrangeEdit([&] {
            for (const auto& [clip, ids] : SelectionByClip())
            {
               const Arrange::Clip* c = Arrange::FindClip(gArrange, clip);
               if (c == nullptr)
                  continue;
               std::vector<Note> edits;
               for (uint32_t id : ids)
                  if (const Note* found = Arrange::FindNote(*c, id))
                  {
                     edits.push_back(*found);
                     edits.back().mute = mute;
                  }
               Arrange::EditNotes(gArrange, clip, edits);
            }
         });
      SectionCard::Begin(T("Expression at start"));
      NoteField(page, T("Bend"), -12.0f, 12.0f, "%.2f st", [&](const Note& n) { return ExprAtStart(n.bend); },
                [&](Note& n, float v) { SetExprAtStart(n.bend, v); });
      NoteField(page, T("Pressure"), 0.0f, 1.0f, "%.2f", [&](const Note& n) { return ExprAtStart(n.pressure); },
                [&](Note& n, float v) { SetExprAtStart(n.pressure, v); });
      NoteField(page, T("Timbre"), 0.0f, 1.0f, "%.2f", [&](const Note& n) { return ExprAtStart(n.timbre); },
                [&](Note& n, float v) { SetExprAtStart(n.timbre, v); });
   }

   void DrawClipInspector(Arrange::Lane& page)
   {
      State& s = S();
      using Clip = Arrange::Clip;
      Clip* c0 = Arrange::FindClip(gArrange, *s.clips.begin());
      if (c0 == nullptr)
         return;
      SectionCard::Begin(s.clips.size() == 1 ? T("Clip") : T("Clips"));
      if (s.clips.size() == 1)
      {
         SectionCard::RowLabel(T("Name"));
         ImGui::SetNextItemWidth(-1.0f);
         const bool changed = FieldWell::InputText("##clipName", &c0->name);
         if (ImGui::IsItemActivated())
            ArrangeGestureBegin();
         if (changed)
            gArrange.revision++;
         if (ImGui::IsItemDeactivated())
            ArrangeGestureEnd();
      }
      SectionCard::RowLabel(T("Colour"));
      ImVec4 shown = ClipColor(*c0, page);
      float rgb[3] = { shown.x, shown.y, shown.z };
      if (FormParts::ColorDot("##clipColour", rgb))
      {
         if (!gArrangeGestureOpen)
            ArrangeGestureBegin();
         for (uint64_t id : s.clips)
            if (Clip* c = Arrange::FindClip(gArrange, id))
            {
               c->colorR = rgb[0];
               c->colorG = rgb[1];
               c->colorB = rgb[2];
            }
         gArrange.revision++;
      }
      else if (gArrangeGestureOpen && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && s.drag == Drag::None && !s.exprDrag &&
               !ImGui::IsAnyItemActive())
         ArrangeGestureEnd();
      SectionCard::Begin(T("Play"));
      ClipField(T("Gain"), -60.0f, 12.0f, "%.1f dB", [](Clip& c) -> float& { return c.gainDb; });
      const auto intField = [&](const char* label, int lo, int hi, auto field) {
         int v = (int)field(*c0);
         const bool changed = FormParts::SliderInt(label, &v, lo, hi);
         if (ImGui::IsItemActivated())
            ArrangeGestureBegin();
         if (changed)
         {
            for (uint64_t id : s.clips)
               if (Clip* c = Arrange::FindClip(gArrange, id))
                  field(*c) = v;
            gArrange.revision++;
         }
         if (ImGui::IsItemDeactivated())
            ArrangeGestureEnd();
      };
      intField(T("Transpose"), -48, 48, [](Clip& c) -> int& { return c.transpose; });
      ClipField(T("Velocity"), 0.0f, 2.0f, "%.2f", [](Clip& c) -> float& { return c.velScale; });
      ClipField(T("Swing"), 0.0f, 1.0f, "%.2f", [](Clip& c) -> float& { return c.swing; });
      SectionCard::Begin(T("Loop"));
      bool loop = c0->midiLoop;
      if (FormParts::Switch(T("Loop"), &loop))
         ArrangeEdit([&] {
            for (uint64_t id : s.clips)
               if (Clip* c = Arrange::FindClip(gArrange, id))
               {
                  c->midiLoop = loop;
                  if (loop && c->loopLength <= 0)
                  {
                     c->loopStart = c->contentOffset;
                     c->loopLength = c->length;
                  }
               }
            gArrange.revision++;
         });
      if (loop)
      {
         float beats = (float)c0->loopLength / (float)Arrange::kPPQ;
         const bool changed = FormParts::SliderFloat(T("Length"), &beats, 0.25f, 64.0f, "%.2f beats", ImGuiSliderFlags_Logarithmic);
         if (ImGui::IsItemActivated())
            ArrangeGestureBegin();
         if (changed)
         {
            for (uint64_t id : s.clips)
               if (Clip* c = Arrange::FindClip(gArrange, id))
                  c->loopLength = std::max<Tick>(1, (Tick)std::lround(beats * (float)Arrange::kPPQ));
            gArrange.revision++;
         }
         if (ImGui::IsItemDeactivated())
            ArrangeGestureEnd();
      }
   }

   void DrawPageInspector(Arrange::Lane& page)
   {
      State& s = S();
      SectionCard::Begin(T("Page"));
      SectionCard::RowLabel(T("Colour"));
      const ImVec4 shown = page.colorR + page.colorG + page.colorB > 0.0f ? ImVec4(page.colorR, page.colorG, page.colorB, 1.0f)
                                                                         : ClipColor(Arrange::Clip {}, page);
      float rgb[3] = { shown.x, shown.y, shown.z };
      if (FormParts::ColorDot("##pageColour", rgb))
      {
         page.colorR = rgb[0];
         page.colorG = rgb[1];
         page.colorB = rgb[2];
         ArrangeCommitEdit();
      }
      bool snap = page.scaleSnap;
      if (FormParts::Switch(T("Scale snap"), &snap))
      {
         const uint64_t lane = page.id;
         ArrangeEdit([&] {
            Arrange::FindLane(gArrange, lane)->scaleSnap = snap;
            gArrange.revision++;
         });
      }
      FormParts::Switch(T("Audition"), &s.audition);
      SectionCard::RowLabel(T("Grid"));
      DrawGridChip();
   }

   void DrawInspector(Arrange::Lane& page, ImVec2 size)
   {
      State& s = S();
      ImGui::BeginChild("##pianoInspector", size, ImGuiChildFlags_AlwaysUseWindowPadding, 0);
      SectionCard::BeginWindow();
      if (!s.notes.empty())
         DrawNoteInspector(page);
      else if (!s.clips.empty())
         DrawClipInspector(page);
      else
         DrawPageInspector(page);
      SectionCard::EndWindow();
      s.inspectorHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      ImGui::EndChild();
   }

   // The panel's keys, while the pointer is over it (the Timeline's rule; the canvas's are off then).
   void HandleKeys(Arrange::Lane& page, const Areas& a, float rollW, float rollH)
   {
      State& s = S();
      const ImGuiIO& io = ImGui::GetIO();
      if (!gPianoRollClaimedKeys || s.inspectorHovered || io.WantTextInput || s.drag != Drag::None || s.exprDrag || gArrangeGestureOpen)
         return;
      const bool cmd = io.KeyCtrl || io.KeySuper, shift = io.KeyShift;
      const bool noMods = !cmd && !shift && !io.KeyAlt;
      const bool any = !s.notes.empty();
      const auto key = [](ImGuiKey k, bool repeat = false) { return ImGui::IsKeyPressed(k, repeat); };
      const auto clipIds = [&] { return std::vector<uint64_t>(s.clips.begin(), s.clips.end()); };
      if (noMods && key(ImGuiKey_Escape))
      {
         if (s.tool != Tool::Select)
            s.tool = Tool::Select;
         else
         {
            s.notes.clear();
            s.clips.clear();
            s.rangeStart = s.rangeEnd = 0;
         }
      }
      else if (noMods && key(ImGuiKey_D))
         s.tool = Tool::Draw;
      else if (noMods && key(ImGuiKey_E))
         s.tool = Tool::Erase;
      else if (noMods && key(ImGuiKey_B))
         s.tool = Tool::Blade;
      else if (noMods && (key(ImGuiKey_0) || key(ImGuiKey_Keypad0)))
      {
         if (any)
            TransformNotes(page, [](std::vector<Arrange::Note>& notes) {
               for (Arrange::Note& n : notes)
                  n.mute = !n.mute;
            });
         else if (!s.clips.empty())
            ArrangeEdit([&] { Arrange::SetEnabled(gArrange, clipIds(), Arrange::kToggle); });
      }
      else if (cmd && !shift && key(ImGuiKey_C) && any)
         CopyNotes();
      else if (cmd && !shift && key(ImGuiKey_X) && any)
      {
         CopyNotes();
         DeleteSelection();
      }
      else if (cmd && !shift && key(ImGuiKey_V))
         PasteNotes(page, ArrangePlayTick());
      else if (cmd && !shift && key(ImGuiKey_D))
      {
         if (any)
            DuplicateSelection();
         else if (!s.clips.empty())
            ArrangeEdit([&] { Arrange::DuplicateBlock(gArrange, clipIds()); });
      }
      else if (key(ImGuiKey_Delete) || key(ImGuiKey_Backspace))
      {
         if (any)
            DeleteSelection();
         else if (!s.clips.empty())
         {
            ArrangeEdit([&] { Arrange::Delete(gArrange, clipIds()); });
            s.clips.clear();
         }
      }
      else if (cmd && !shift && key(ImGuiKey_E))
      {
         const Tick at = ArrangePlayTick();
         if (any)
            ArrangeEdit([&] {
               for (const auto& [clip, ids] : SelectionByClip())
                  if (const Arrange::Clip* c = Arrange::FindClip(gArrange, clip))
                     if (at >= c->start && at < c->start + c->length)
                        Arrange::SplitNotes(gArrange, clip, ids, Arrange::MidiContentTick(*c, at - c->start));
            });
         else
            ArrangeEdit([&] {
               for (uint64_t id : s.clips)
                  Arrange::Split(gArrange, id, at);
            });
      }
      else if (cmd && !shift && key(ImGuiKey_J) && any)
         ArrangeEdit([&] {
            for (const auto& [clip, ids] : SelectionByClip())
               Arrange::JoinNotes(gArrange, clip, ids);
         });
      else if (cmd && !shift && key(ImGuiKey_A))
         SelectAll(page);
      else if (cmd && !shift && key(ImGuiKey_R))
      {
         s.renameLane = page.id;
         std::snprintf(s.renameBuf, sizeof s.renameBuf, "%s", page.name.c_str());
         s.renameFocus = true;
      }
      else if (cmd && shift && key(ImGuiKey_G))
      {
         std::vector<uint64_t> groups;
         for (const Arrange::Clip& c : page.clips)
            if (s.clips.count(c.id) != 0 && c.groupId != 0)
               groups.push_back(c.groupId);
         ArrangeEdit([&] { Arrange::Ungroup(gArrange, groups); });
      }
      else if (cmd && !shift && key(ImGuiKey_G))
         ArrangeEdit([&] { Arrange::Group(gArrange, clipIds()); });
      else if (noMods && key(ImGuiKey_Q))
         Quantize(page);
      else if (!cmd && shift && key(ImGuiKey_Q))
         s.openQuantize = true;
      else if (any && key(ImGuiKey_UpArrow, true))
         NudgePitch(page, 1, shift);
      else if (any && key(ImGuiKey_DownArrow, true))
         NudgePitch(page, -1, shift);
      else if (noMods && key(ImGuiKey_LeftArrow, true))
      {
         if (any)
            NudgeTime(-1);
         else
            ArrangeSeekTick(std::max<Tick>(0, ArrangePlayTick() - ArrangeNudgeStepTicks()));
      }
      else if (noMods && key(ImGuiKey_RightArrow, true))
      {
         if (any)
            NudgeTime(1);
         else
            ArrangeSeekTick(ArrangePlayTick() + ArrangeNudgeStepTicks());
      }
      else if (noMods && key(ImGuiKey_Equal, true))
         ZoomX(1.25, (a.x0 + a.x1) * 0.5f, a.x0);
      else if (noMods && key(ImGuiKey_Minus, true))
         ZoomX(1.0 / 1.25, (a.x0 + a.x1) * 0.5f, a.x0);
      else if (noMods && key(ImGuiKey_F))
         FitView(page, rollW, rollH);
      else if (noMods && key(ImGuiKey_Z))
         ZoomToSelection(page, rollW);
      else if (noMods && key(ImGuiKey_LeftBracket))
         JumpPage(page, -1);
      else if (noMods && key(ImGuiKey_RightBracket))
         JumpPage(page, 1);
      else if (cmd && !shift && key(ImGuiKey_L))
         SetLoopFromSelection(page);
      else if (noMods && key(ImGuiKey_I))
         ArrangeSetLoopEdgeAtPlayhead(true);
      else if (noMods && key(ImGuiKey_O))
         ArrangeSetLoopEdgeAtPlayhead(false);
   }

   void DrawPage(Arrange::Lane& page)
   {
      State& s = S();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float toolbarH = tok::tile + ImGui::GetStyle().ItemSpacing.y;
      {
         const ImVec2 room = ImGui::GetContentRegionAvail();
         DrawToolbar(page, room.x - kKeysW, room.y - toolbarH - kRulerH - kStripH);
      }
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      Areas a;
      const float inspectorW = std::min(kInspectorW, avail.x * 0.4f);
      const float exprRoom = kSplitH + tok::tile + tok::space_1 + s.exprH;
      a.x0 = origin.x + kKeysW;
      a.x1 = origin.x + avail.x - inspectorW - tok::space_2;
      a.rulerY = origin.y;
      a.stripY = a.rulerY + kRulerH;
      a.rollY = a.stripY + kStripH;
      a.rollB = origin.y + avail.y - exprRoom;
      if (a.rollB - a.rollY < 8.0f || a.x1 - a.x0 < 8.0f)
         return;
      if (s.drag == Drag::None) // rows stay put under a drag: fold would pull them out from under the pointer
         BuildRows(page);
      if (s.viewedLane != page.id)
      {
         s.viewedLane = page.id;
         s.notes.clear();
         s.clips.clear();
         s.rangeStart = s.rangeEnd = 0;
         FitView(page, a.x1 - a.x0, a.rollB - a.rollY);
      }
      PruneSelection(page);

      // Scroll and zoom anywhere over the page. Read every frame: the pinch must not pile up while the pointer is away.
      const double pinch = TrackpadPinchThisFrame();
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
      ImGui::InvisibleButton("##pianoRoll", ImVec2(a.x1 - origin.x, a.rollB - origin.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
      ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
      const bool hovered = ImGui::IsItemHovered();
      const ImGuiIO& io = ImGui::GetIO();
      const bool cmdOrCtrl = io.KeyCtrl || io.KeySuper;
      if (hovered)
      {
         const float wheelY = io.MouseWheel, wheelX = io.MouseWheelH;
         if (pinch != 0.0)
         {
            if (io.KeyAlt)
               s.keyH = std::clamp(s.keyH * (float)(1.0 + pinch), kMinKeyH, kMaxKeyH);
            else
               ZoomX(1.0 + pinch, mouse.x, a.x0);
         }
         else if (cmdOrCtrl && wheelY != 0.0f)
         {
            if (io.KeyAlt)
               s.keyH = std::clamp(s.keyH * std::pow(1.1f, wheelY), kMinKeyH, kMaxKeyH);
            else
               ZoomX(std::pow(1.15, (double)wheelY), mouse.x, a.x0);
         }
         else
         {
            s.topRow -= wheelY * 3.0f;
            s.viewTick -= (double)wheelX * 30.0 / s.pxPerTick;
         }
      }
      ClampView(a.rollB - a.rollY);
      HandlePointer(page, a, hovered, mouse);
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouse.y >= a.rollY && mouse.x >= a.x0)
         s.openFunctions = true;
      HandleKeys(page, a, a.x1 - a.x0, a.rollB - a.rollY);

      // Follow: the view pages along while playing.
      const Tick play = ArrangePlayTick();
      if (s.follow && Transport::Instance().IsPlaying())
      {
         const double span = (double)(a.x1 - a.x0) / s.pxPerTick;
         if ((double)play < s.viewTick || (double)play > s.viewTick + span * 0.95)
            s.viewTick = (double)play;
      }

      dl->PushClipRect(origin, ImVec2(a.x1, a.rollB), true);
      DrawRuler(dl, a);
      dl->PushClipRect(ImVec2(a.x0, a.rollY), ImVec2(a.x1, a.rollB), true);
      DrawGrid(dl, a);
      DrawClipsAndNotes(dl, a, page);
      if (s.drag == Drag::Marquee)
      {
         const ImVec2 p0(std::min(mouse.x, s.dragFrom.x), std::min(mouse.y, s.dragFrom.y));
         const ImVec2 p1(std::max(mouse.x, s.dragFrom.x), std::max(mouse.y, s.dragFrom.y));
         dl->AddRectFilled(p0, p1, AccentU32(0.15f));
         dl->AddRect(p0, p1, AccentU32(0.8f));
      }
      dl->PopClipRect();
      DrawKeys(dl, a);
      dl->PushClipRect(ImVec2(a.x0, a.stripY), ImVec2(a.x1, a.rollY), true);
      DrawClipStrip(dl, a, page);
      dl->PopClipRect();
      const float px = a.TickX((double)play);
      if (px >= a.x0 && px <= a.x1)
         dl->AddLine(ImVec2(px, a.rulerY), ImVec2(px, a.rollB), AccentU32(1.0f), 1.5f);
      if (page.clips.empty())
      {
         const char* text = T("Draw a clip, drop a .mid, or record");
         const ImVec2 ts = ImGui::CalcTextSize(text);
         dl->AddText(ImVec2((a.x0 + a.x1 - ts.x) * 0.5f, (a.rollY + a.rollB - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
      }
      dl->PopClipRect();
      DrawExpression(page, a, a.rollB);
      ImGui::SetCursorScreenPos(ImVec2(a.x1 + tok::space_2, origin.y));
      DrawInspector(page, ImVec2(inspectorW, avail.y));
   }

   void DrawPanel()
   {
      gPianoRollClaimedKeys = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput;
      FollowCanvasSelection();
      DrawTabs();
      DrawLinkChip();
      DrawDockMenu();
      Arrange::Lane* page = Page();
      if (page != nullptr)
      {
         DrawPage(*page);
         return;
      }
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const char* text = T("Add a page to start writing notes");
      const ImVec2 ts = ImGui::CalcTextSize(text);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (avail.x - ts.x) * 0.5f, p.y + (avail.y - ts.y) * 0.5f),
                                          ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
   }
}

void PianoRollOpenPage(uint64_t laneId)
{
   gPianoRollOpen = true;
   S().lane = laneId;
}

void DrawPianoRollDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const bool gripFirst = gPianoRollDock != 3; // bottom: the grip sits on top
   ImGui::BeginChild(id, size, false);
   gPianoRollRectMin = ImGui::GetWindowPos();
   gPianoRollRectMax = ImVec2(gPianoRollRectMin.x + ImGui::GetWindowSize().x, gPianoRollRectMin.y + ImGui::GetWindowSize().y);
   gPianoRollClaimedKeys = false;
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]()
   {
      ImGui::InvisibleButton("##pianogrip", ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(false, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         gPianoRollHeight += gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y;
         gPianoRollHeight = std::max(kMinHeight, gPianoRollHeight);
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
   PanelFrame::BeginCard("##pianopanelcontent", cardSize, in);
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
