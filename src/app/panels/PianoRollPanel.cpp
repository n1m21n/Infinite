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
#include "arrange/ArrangeModel.h"
#include "audio/MusicTime.h"
#include "core/Transport.h"
#include "nodes/PianoRollNode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
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
      a.x0 = origin.x + kKeysW;
      a.x1 = origin.x + avail.x;
      a.rulerY = origin.y;
      a.stripY = a.rulerY + kRulerH;
      a.rollY = a.stripY + kStripH;
      a.rollB = origin.y + avail.y;
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
      ImGui::InvisibleButton("##pianoRoll", ImVec2(avail.x, avail.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
      ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
      const bool hovered = ImGui::IsItemHovered();
      if (hovered)
         gPianoRollClaimedKeys = true;
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

      // Follow: the view pages along while playing.
      const Tick play = ArrangePlayTick();
      if (s.follow && Transport::Instance().IsPlaying())
      {
         const double span = (double)(a.x1 - a.x0) / s.pxPerTick;
         if ((double)play < s.viewTick || (double)play > s.viewTick + span * 0.95)
            s.viewTick = (double)play;
      }

      dl->PushClipRect(ImVec2(origin.x, origin.y), ImVec2(a.x1, a.rollB), true);
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
   }

   void DrawPanel()
   {
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
