#include "ArrangeModel.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Arrange
{
namespace
{
   void ClampFades(Clip& c)
   {
      if (c.fadeIn < 0) c.fadeIn = 0;
      if (c.fadeOut < 0) c.fadeOut = 0;
      if (c.fadeIn > c.length) c.fadeIn = c.length;
      if (c.fadeOut > c.length) c.fadeOut = c.length;
   }

   const char* LaneTypeName(int type)
   {
      return type == kLaneAudio ? "Audio" : type == kLaneMidi ? "MIDI" : "Video";
   }

   bool NoteLess(const Note& a, const Note& b)
   {
      return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
   }

   void ClampExpr(std::vector<ExprPoint>& pts, Tick len, float lo, float hi)
   {
      for (ExprPoint& p : pts)
      {
         p.at = std::clamp<Tick>(p.at, 0, len);
         if (!std::isfinite(p.v)) p.v = 0.0f;
         p.v = std::clamp(p.v, lo, hi);
      }
      std::stable_sort(pts.begin(), pts.end(), [](const ExprPoint& a, const ExprPoint& b) { return a.at < b.at; });
   }

   // Sorts, and cuts a note where the next note on its pitch starts (a note
   // fully covered at the same start is dropped). Leaves the invariants of
   // README §5 true. Returns true if anything changed.
   bool ResolveNoteOverlaps(Clip& c)
   {
      bool changed = false;
      if (!std::is_sorted(c.notes.begin(), c.notes.end(), NoteLess))
      {
         std::stable_sort(c.notes.begin(), c.notes.end(), NoteLess);
         changed = true;
      }
      int lastOnPitch[128];
      std::fill(std::begin(lastOnPitch), std::end(lastOnPitch), -1);
      std::vector<bool> drop(c.notes.size(), false);
      for (int i = 0; i < (int)c.notes.size(); i++)
      {
         Note& n = c.notes[i];
         const int prev = lastOnPitch[n.pitch];
         if (prev >= 0)
         {
            Note& p = c.notes[prev];
            if (p.start == n.start) { drop[prev] = true; changed = true; }
            else if (p.End() > n.start) { p.length = n.start - p.start; changed = true; }
         }
         lastOnPitch[n.pitch] = i;
      }
      if (changed)
      {
         std::vector<Note> kept;
         kept.reserve(c.notes.size());
         for (size_t i = 0; i < c.notes.size(); i++)
            if (!drop[i]) kept.push_back(std::move(c.notes[i]));
         c.notes.swap(kept);
      }
      for (Note& n : c.notes)
      {
         ClampExpr(n.bend, n.length, -48.0f, 48.0f);
         ClampExpr(n.pressure, n.length, 0.0f, 1.0f);
         ClampExpr(n.timbre, n.length, 0.0f, 1.0f);
      }
      return changed;
   }

   void ClampNote(Note& n)
   {
      if (n.start < 0) n.start = 0;
      if (n.start > kMaxTick - 1) n.start = kMaxTick - 1;
      n.length = std::clamp<Tick>(n.length, 1, kMaxTick - n.start);
      if (n.pitch > 127) n.pitch = 127;
      n.vel = (uint8_t)std::clamp<int>(n.vel, 1, 127);
      if (n.relVel > 127) n.relVel = 127;
      if (!std::isfinite(n.chance)) n.chance = 1.0f;
      n.chance = std::clamp(n.chance, 0.0f, 1.0f);
      if (!std::isfinite(n.gainDb)) n.gainDb = 0.0f;
      n.gainDb = std::clamp(n.gainDb, -60.0f, 12.0f);
      if (!std::isfinite(n.pan)) n.pan = 0.0f;
      n.pan = std::clamp(n.pan, -1.0f, 1.0f);
   }

   void ClampMidiClip(Clip& c)
   {
      if (c.contentOffset < 0) c.contentOffset = 0;
      if (c.loopStart < 0) c.loopStart = 0;
      if (c.loopLength < 0) c.loopLength = 0;
      if (c.midiLoop && c.loopLength <= 0) c.midiLoop = false;
      c.transpose = std::clamp(c.transpose, -48, 48);
      if (!std::isfinite(c.velScale)) c.velScale = 1.0f;
      c.velScale = std::clamp(c.velScale, 0.0f, 2.0f);
      if (!std::isfinite(c.swing)) c.swing = 0.0f;
      c.swing = std::clamp(c.swing, 0.0f, 1.0f);
   }

   void NormalizeNotes(const Lane& l, Clip& c)
   {
      if (l.type != kLaneMidi)
      {
         c.notes.clear();
         return;
      }
      ClampMidiClip(c);
      std::unordered_set<uint32_t> seen;
      uint32_t maxId = 0;
      for (Note& n : c.notes)
      {
         ClampNote(n);
         if (n.id == 0 || !seen.insert(n.id).second) n.id = 0; // re-minted below
         else maxId = std::max(maxId, n.id);
      }
      if (c.nextNoteId <= maxId) c.nextNoteId = maxId + 1;
      if (c.nextNoteId == 0) c.nextNoteId = 1;
      for (Note& n : c.notes)
         if (n.id == 0) n.id = c.nextNoteId++;
      ResolveNoteOverlaps(c);
   }

   bool ValidateNotes(const Lane& l, const Clip& c, std::string& why)
   {
      if (l.type != kLaneMidi)
      {
         if (!c.notes.empty()) { why = "notes on a non-MIDI lane"; return false; }
         return true;
      }
      if (c.contentOffset < 0) { why = "negative content offset"; return false; }
      if (c.midiLoop && c.loopLength <= 0) { why = "loop with no length"; return false; }
      std::unordered_set<uint32_t> ids;
      Tick lastEnd[128];
      std::fill(std::begin(lastEnd), std::end(lastEnd), (Tick)-1);
      const Note* prev = nullptr;
      for (const Note& n : c.notes)
      {
         if (n.id == 0 || n.id >= c.nextNoteId) { why = "note id 0 or >= nextNoteId"; return false; }
         if (!ids.insert(n.id).second) { why = "duplicate note id " + std::to_string(n.id); return false; }
         if (n.start < 0 || n.length <= 0) { why = "note start < 0 or length <= 0"; return false; }
         if (n.pitch > 127 || n.vel < 1 || n.vel > 127 || n.relVel > 127) { why = "note pitch or velocity out of range"; return false; }
         if (!(n.chance >= 0.0f && n.chance <= 1.0f)) { why = "note chance out of range"; return false; }
         if (prev && NoteLess(n, *prev)) { why = "notes unsorted"; return false; }
         if (lastEnd[n.pitch] > n.start) { why = "notes overlap on pitch " + std::to_string(n.pitch); return false; }
         lastEnd[n.pitch] = n.End();
         for (const std::vector<ExprPoint>* pts : { &n.bend, &n.pressure, &n.timbre })
            for (size_t k = 0; k < pts->size(); k++)
               if ((*pts)[k].at < 0 || (*pts)[k].at > n.length || (k > 0 && (*pts)[k].at < (*pts)[k - 1].at))
               {
                  why = "note expression outside the note or unsorted";
                  return false;
               }
         prev = &n;
      }
      return true;
   }

   void SortLane(Lane& l)
   {
      std::stable_sort(l.clips.begin(), l.clips.end(),
                       [](const Clip& a, const Clip& b) { return a.start < b.start; });
   }

   // Removes [a, b) from every clip on the lane, splitting a clip that strictly
   // contains the range. Returns true if anything changed. `m` is only needed
   // for fresh ids on a split. Assumes the lane is sorted; leaves it sorted.
   bool CarveRange(Model& m, Lane& lane, Tick a, Tick b)
   {
      if (b <= a) return false;
      bool changed = false;
      std::vector<Clip> out;
      out.reserve(lane.clips.size() + 1);
      for (Clip c : lane.clips)
      {
         const Tick s = c.start, e = c.End();
         if (e <= a || s >= b) { out.push_back(c); continue; }   // no overlap
         changed = true;
         if (s >= a && e <= b) continue;                         // fully eaten
         if (s < a && e > b)                                     // strictly contains
         {
            Clip left = c;
            left.length = a - s;
            ClampFades(left);
            out.push_back(left);

            // Same shape as Split() (this IS an implicit split, carved out
            // by an overwrite/duplicate landing in the middle of an
            // existing clip): the right piece continues reading the source
            // file from where the carved-out middle left off, not from the
            // original clip's own start again - see
            // Clip::sourceOffsetSeconds's and Split's own comments.
            Clip right = c;
            right.id = m.NewId();
            right.start = b;
            right.length = e - b;
            right.sourceOffsetSeconds = c.sourceOffsetSeconds + (float)ClipSourceAdvance(c, b - s);
            right.contentOffset = c.contentOffset + (b - s);
            ClampFades(right);
            out.push_back(right);
            continue;
         }
         if (s < a)                                              // overlaps our left edge
         {
            c.length = a - s;
            ClampFades(c);
            out.push_back(c);
            continue;
         }
         // Overlaps our right edge: the surviving piece's own start moves
         // forward to `b`, so (like TrimEdge's start-edge case) it now
         // starts further into the source file too.
         c.sourceOffsetSeconds = c.sourceOffsetSeconds + (float)ClipSourceAdvance(c, b - s);
         c.contentOffset += b - s;
         c.start = b;
         c.length = e - b;
         ClampFades(c);
         out.push_back(c);
      }
      if (changed)
      {
         lane.clips.swap(out);
         SortLane(lane);
      }
      return changed;
   }

   // A group with fewer than two live members is not a group. Dissolving here
   // rather than at every delete site is what keeps Validate's group rule true
   // no matter which op removed the member.
   bool DissolveSingletonGroups(Model& m)
   {
      std::unordered_map<uint64_t, int> counts;
      for (const Lane& l : m.lanes)
         for (const Clip& c : l.clips)
            if (c.groupId != 0) counts[c.groupId]++;
      bool changed = false;
      for (Lane& l : m.lanes)
         for (Clip& c : l.clips)
            if (c.groupId != 0 && counts[c.groupId] < 2) { c.groupId = 0; changed = true; }
      return changed;
   }

   // Prunes `groupId` if it has no direct lanes and no child groups, then
   // repeats for its (former) parent, since removing the last child can make
   // the parent newly empty too. Track groups (unlike clip groups) don't get
   // a Normalize pass after every op that can empty one, so this has to
   // happen right at the point of removal - see RemoveLane's comment on why.
   void PruneEmptyTrackGroupChain(Model& m, uint64_t groupId)
   {
      while (groupId != 0)
      {
         auto it = std::find_if(m.trackGroups.begin(), m.trackGroups.end(),
                                [&](const TrackGroup& g) { return g.id == groupId; });
         if (it == m.trackGroups.end()) return;
         bool anyLane = false;
         for (const Lane& l : m.lanes) if (l.groupId == groupId) { anyLane = true; break; }
         bool anyChild = false;
         if (!anyLane)
            for (const TrackGroup& g : m.trackGroups) if (g.parentGroupId == groupId) { anyChild = true; break; }
         if (anyLane || anyChild) return;
         const uint64_t parent = it->parentGroupId;
         m.trackGroups.erase(it);
         groupId = parent;
      }
   }
}

namespace
{
   // The one invariant Normalize cannot get by sorting alone. Anything that
   // reaches the model without going through an edit op - the legacy UI bridge
   // (PlaceClipTrimmingOverlap appends without sorting), a hand-edited patch,
   // a file from a build with a bug - can carry overlaps, and Validate would
   // then fail on data no op produced. Later clip wins, the earlier one is
   // trimmed off its tail: the same overwrite convention PlaceOverwrite uses,
   // so a clip never silently disappears under one that starts after it.
   bool ResolveOverlaps(Lane& l)
   {
      bool changed = false;
      for (size_t i = 0; i + 1 < l.clips.size(); i++)
      {
         Clip& a = l.clips[i];
         const Clip& b = l.clips[i + 1];
         if (a.End() <= b.start)
            continue;
         if (b.start <= a.start)
         {
            // Same start: nothing can be trimmed, one of the two has to go.
            // Keep the longer one - dropping "the earlier one" is arbitrary
            // when both start at the same tick, and this is the only place in
            // the model that destroys user data, so it should at least keep
            // the bigger piece.
            const size_t drop = (b.length > a.length) ? i : (i + 1);
            l.clips.erase(l.clips.begin() + (long)drop);
            i--;
            changed = true;
            continue;
         }
         a.length = b.start - a.start;
         ClampFades(a);
         changed = true;
      }
      return changed;
   }
}

void Normalize(Model& m)
{
   for (Lane& l : m.lanes)
   {
      for (Clip& clip : l.clips)
      {
         if (clip.start < 0) clip.start = 0;
         if (clip.length < 1) clip.length = 1;
         if (clip.srcOutput < 0) clip.srcOutput = 0;
         if (!std::isfinite(clip.gainDb)) clip.gainDb = 0.0f;
         clip.gainDb = std::clamp(clip.gainDb, -60.0f, 12.0f); // the gain slider's range
         if (clip.blendMode < 0 || clip.blendMode > 31) clip.blendMode = 0; // BlendModes::Names() range
         if (!std::isfinite(clip.pan)) clip.pan = 0.0f;
         clip.pan = std::clamp(clip.pan, -1.0f, 1.0f);
         if (!std::isfinite(clip.pitch)) clip.pitch = 0.0f;
         clip.pitch = std::clamp(clip.pitch, -24.0f, 24.0f);
         if (!std::isfinite(clip.opacity)) clip.opacity = 1.0f;
         clip.opacity = std::clamp(clip.opacity, 0.0f, 1.0f);
         if (!std::isfinite(clip.colorBrightness)) clip.colorBrightness = 0.0f;
         clip.colorBrightness = std::clamp(clip.colorBrightness, -1.0f, 1.0f);
         if (!std::isfinite(clip.colorContrast)) clip.colorContrast = 0.0f;
         clip.colorContrast = std::clamp(clip.colorContrast, -1.0f, 1.0f);
         if (!std::isfinite(clip.colorSaturation)) clip.colorSaturation = 1.0f;
         clip.colorSaturation = std::clamp(clip.colorSaturation, 0.0f, 2.0f);
         ClampFades(clip);
         if (clip.id == 0) clip.id = m.NewId();
      }
      SortLane(l);
      ResolveOverlaps(l);
      if (l.id == 0) l.id = m.NewId();
      if (l.type != kLaneMidi) l.noteNodeUid = 0;
      for (Clip& clip : l.clips) NormalizeNotes(l, clip);
      for (SessionSlot& sl : l.slots) NormalizeNotes(l, sl.clip);
      // Session slots: a slot clip has no timeline position and no clip group.
      for (SessionSlot& sl : l.slots)
      {
         sl.clip.start = 0;
         sl.clip.groupId = 0;
         if (sl.clip.length < 0) sl.clip.length = 0;
         if (sl.clip.srcOutput < 0) sl.clip.srcOutput = 0;
         if (!std::isfinite(sl.clip.gainDb)) sl.clip.gainDb = 0.0f;
         sl.clip.gainDb = std::clamp(sl.clip.gainDb, -60.0f, 12.0f);
         if (sl.clip.length > 0) ClampFades(sl.clip);
         sl.followAction = std::clamp(sl.followAction, 0, (int)kFollowCount - 1);
         sl.followBars = std::clamp(sl.followBars, kFollowBarsMin, kFollowBarsMax);
      }
      while (m.scenes.size() < l.slots.size())
         m.scenes.push_back(Scene());
   }
   std::stable_sort(m.markers.begin(), m.markers.end(),
                    [](const Marker& a, const Marker& b) { return a.pos < b.pos; });
   for (Marker& mk : m.markers)
      if (mk.id == 0) mk.id = m.NewId();

   for (TrackGroup& g : m.trackGroups)
      if (g.id == 0) g.id = m.NewId();

   // Re-mint collisions. Ids arriving from outside an edit op are not
   // trustworthy: the legacy UI's copy/paste/duplicate/split paths clone a
   // whole clip record, id included, and a hand-edited patch can do the same.
   // A duplicate id is worse than a lost one - Find() resolves to the first
   // match, so Delete/Group/MoveClips would silently hit the wrong clip and
   // every id-keyed cache (selection, waveform, thumbnail) would attach to
   // it. The clip that already sat at that id keeps it; later ones move.
   {
      std::unordered_set<uint64_t> seen;
      auto claim = [&](uint64_t& id) {
         if (id != 0 && seen.insert(id).second)
            return;
         do { id = m.NewId(); } while (!seen.insert(id).second);
      };
      for (Lane& l : m.lanes)
      {
         claim(l.id);
         for (Clip& c : l.clips) claim(c.id);
         for (SessionSlot& sl : l.slots)
            if (sl.Has()) claim(sl.clip.id);
      }
      for (Marker& mk : m.markers) claim(mk.id);
      for (TrackGroup& g : m.trackGroups) claim(g.id);
   }

   DissolveSingletonGroups(m);

   // Track groups do NOT dissolve at 1 member (unlike clip groups above) -
   // only a group with nothing left anywhere in its subtree (no direct lane,
   // no live child group) is dead weight in the registry, so only that case
   // gets pruned here, from the leaves up. A lane or group pointing at a
   // group id that no longer exists (a hand-edited patch, a bug) is promoted
   // to root rather than left dangling, since Validate rejects that. A
   // group whose parent chain cycles back to itself (also only reachable via
   // a hand-edited patch) is likewise reset to root, so nothing that walks
   // parentGroupId - the row-layout tree walk, GroupAncestors - can loop.
   {
      std::unordered_set<uint64_t> liveGroups;
      for (const TrackGroup& g : m.trackGroups) liveGroups.insert(g.id);
      for (Lane& l : m.lanes)
         if (l.groupId != 0 && !liveGroups.count(l.groupId)) l.groupId = 0;
      // One page per node: a second lane claiming the same node lets go.
      std::unordered_set<uint64_t> linked;
      for (Lane& l : m.lanes)
         if (l.noteNodeUid != 0 && !linked.insert(l.noteNodeUid).second) l.noteNodeUid = 0;
      for (TrackGroup& g : m.trackGroups)
         if (g.parentGroupId != 0 && !liveGroups.count(g.parentGroupId)) g.parentGroupId = 0;

      for (TrackGroup& g : m.trackGroups)
      {
         uint64_t cur = g.parentGroupId;
         size_t hops = 0;
         bool ok = true;
         while (cur != 0)
         {
            if (cur == g.id || hops++ > m.trackGroups.size()) { ok = false; break; }
            const TrackGroup* p = nullptr;
            for (const TrackGroup& gg : m.trackGroups) if (gg.id == cur) { p = &gg; break; }
            if (!p) break;
            cur = p->parentGroupId;
         }
         if (!ok) g.parentGroupId = 0;
      }

      // Prune bottom-up: a group with zero direct lanes and zero child
      // groups is dead weight. Repeat until a pass removes nothing, since
      // pruning a now-childless leaf can make its parent prunable too.
      bool prunedAny = true;
      while (prunedAny)
      {
         std::unordered_map<uint64_t, int> laneCounts, childCounts;
         for (const Lane& l : m.lanes)
            if (l.groupId != 0) laneCounts[l.groupId]++;
         for (const TrackGroup& g : m.trackGroups)
            if (g.parentGroupId != 0) childCounts[g.parentGroupId]++;
         const size_t before = m.trackGroups.size();
         m.trackGroups.erase(std::remove_if(m.trackGroups.begin(), m.trackGroups.end(),
                                            [&](const TrackGroup& g) { return laneCounts[g.id] == 0 && childCounts[g.id] == 0; }),
                             m.trackGroups.end());
         prunedAny = m.trackGroups.size() != before;
      }
   }

   // nextId must sit above everything already handed out. A patch whose saved
   // nextId was stale (hand-edited, or written by a build with the bug) would
   // otherwise hand out a duplicate on the next edit.
   uint64_t maxId = 0;
   for (const Lane& l : m.lanes)
   {
      maxId = std::max(maxId, l.id);
      maxId = std::max(maxId, l.groupId);
      for (const Clip& c : l.clips) { maxId = std::max(maxId, c.id); maxId = std::max(maxId, c.groupId); }
   }
   for (const Marker& mk : m.markers) maxId = std::max(maxId, mk.id);
   for (const TrackGroup& g : m.trackGroups) maxId = std::max(maxId, g.id);
   if (m.nextId <= maxId) m.nextId = maxId + 1;

   // Normalize is the funnel every un-vetted model passes through, so this is
   // the one place worth paying for the check: if it can't produce a valid
   // model, the repair above has a hole and the fixtures should say so loudly
   // rather than letting the bad model reach disk.
#ifndef NDEBUG
   {
      std::string why;
      assert(Validate(m, &why) && "Arrange::Normalize left the model invalid");
      (void)why;
   }
#endif
}

Loc Find(const Model& m, uint64_t clipId)
{
   if (clipId == 0) return Loc{};
   for (size_t li = 0; li < m.lanes.size(); li++)
      for (size_t ci = 0; ci < m.lanes[li].clips.size(); ci++)
         if (m.lanes[li].clips[ci].id == clipId) return Loc{(int)li, (int)ci};
   return Loc{};
}

const Clip* FindClip(const Model& m, uint64_t clipId)
{
   const Loc loc = Find(m, clipId);
   if (loc.Valid())
      return &m.lanes[loc.lane].clips[loc.index];
   const Loc sl = FindSlot(m, clipId);
   return sl.lane >= 0 ? &m.lanes[sl.lane].slots[sl.slot].clip : nullptr;
}

Clip* FindClip(Model& m, uint64_t clipId)
{
   return const_cast<Clip*>(FindClip(static_cast<const Model&>(m), clipId));
}

Loc FindSlot(const Model& m, uint64_t clipId)
{
   if (clipId == 0) return Loc{};
   for (size_t li = 0; li < m.lanes.size(); li++)
      for (size_t si = 0; si < m.lanes[li].slots.size(); si++)
         if (m.lanes[li].slots[si].Has() && m.lanes[li].slots[si].clip.id == clipId)
         {
            Loc l;
            l.lane = (int)li;
            l.slot = (int)si;
            return l;
         }
   return Loc{};
}

const Lane* LaneOfClip(const Model& m, uint64_t clipId)
{
   const Loc loc = Find(m, clipId);
   if (loc.Valid())
      return &m.lanes[loc.lane];
   const Loc sl = FindSlot(m, clipId);
   return sl.lane >= 0 ? &m.lanes[sl.lane] : nullptr;
}

Clip* FindClipOrSlot(Model& m, uint64_t clipId)
{
   if (Clip* c = FindClip(m, clipId))
      return c;
   if (clipId == 0)
      return nullptr;
   for (Lane& lane : m.lanes)
      for (SessionSlot& s : lane.slots)
         if (s.Has() && s.clip.id == clipId)
            return &s.clip;
   return nullptr;
}

int LaneIndex(const Model& m, uint64_t laneId)
{
   for (size_t i = 0; i < m.lanes.size(); i++)
      if (m.lanes[i].id == laneId) return (int)i;
   return -1;
}

const Lane* FindLane(const Model& m, uint64_t laneId)
{
   const int i = LaneIndex(m, laneId);
   return i >= 0 ? &m.lanes[i] : nullptr;
}

Lane* FindLane(Model& m, uint64_t laneId)
{
   const int i = LaneIndex(m, laneId);
   return i >= 0 ? &m.lanes[i] : nullptr;
}

bool Validate(const Model& m, std::string* why)
{
   auto fail = [&](const std::string& msg) { if (why) *why = msg; return false; };

   std::unordered_set<uint64_t> ids;
   std::unordered_set<uint64_t> linkedNodes;
   std::unordered_map<uint64_t, int> groupCounts;
   for (const Lane& l : m.lanes)
   {
      if (l.id == 0) return fail("lane with id 0");
      if (!ids.insert(l.id).second) return fail("duplicate lane id " + std::to_string(l.id));
      if (l.id >= m.nextId) return fail("lane id " + std::to_string(l.id) + " >= nextId");
      if (l.type != kLaneVideo && l.type != kLaneAudio && l.type != kLaneMidi) return fail("bad lane type");
      if (l.type != kLaneMidi && l.noteNodeUid != 0) return fail("note link on a non-MIDI lane");
      if (l.noteNodeUid != 0 && !linkedNodes.insert(l.noteNodeUid).second)
         return fail("node " + std::to_string(l.noteNodeUid) + " linked to two lanes");
      {
         std::string noteWhy;
         for (const Clip& c : l.clips)
            if (!ValidateNotes(l, c, noteWhy)) return fail(noteWhy);
         for (const SessionSlot& sl : l.slots)
            if (!ValidateNotes(l, sl.clip, noteWhy)) return fail(noteWhy);
      }

      Tick prevEnd = 0;
      bool first = true;
      for (const Clip& c : l.clips)
      {
         if (c.id == 0) return fail("clip with id 0");
         if (!ids.insert(c.id).second) return fail("duplicate clip id " + std::to_string(c.id));
         if (c.id >= m.nextId) return fail("clip id " + std::to_string(c.id) + " >= nextId");
         if (c.start < 0) return fail("negative clip start");
         if (c.length <= 0) return fail("clip length <= 0");
         if (c.fadeIn < 0 || c.fadeOut < 0 || c.fadeIn > c.length || c.fadeOut > c.length)
            return fail("fade outside clip");
         if (!first && c.start < prevEnd)
            return fail("clips overlap or unsorted on lane " + std::to_string(l.id));
         prevEnd = c.End();
         first = false;
         if (c.groupId != 0)
         {
            if (c.groupId >= m.nextId) return fail("group id >= nextId");
            groupCounts[c.groupId]++;
         }
      }
   }
   for (const auto& g : groupCounts)
      if (g.second < 2) return fail("group " + std::to_string(g.first) + " has one member");

   std::unordered_set<uint64_t> trackGroupIds;
   for (const TrackGroup& g : m.trackGroups)
   {
      if (g.id == 0) return fail("track group with id 0");
      if (!trackGroupIds.insert(g.id).second) return fail("duplicate track group id " + std::to_string(g.id));
      if (g.id >= m.nextId) return fail("track group id " + std::to_string(g.id) + " >= nextId");
   }
   for (const Lane& l : m.lanes)
      if (l.groupId != 0 && !trackGroupIds.count(l.groupId))
         return fail("lane " + std::to_string(l.id) + " points at missing track group " + std::to_string(l.groupId));
   for (const TrackGroup& g : m.trackGroups)
      if (g.parentGroupId != 0 && !trackGroupIds.count(g.parentGroupId))
         return fail("track group " + std::to_string(g.id) + " points at missing parent " + std::to_string(g.parentGroupId));
   for (const TrackGroup& g : m.trackGroups)
   {
      uint64_t cur = g.parentGroupId;
      size_t hops = 0;
      while (cur != 0)
      {
         if (cur == g.id) return fail("track group " + std::to_string(g.id) + " is its own ancestor");
         if (hops++ > m.trackGroups.size()) return fail("track group " + std::to_string(g.id) + " has a cyclic or too-deep parent chain");
         const TrackGroup* p = nullptr;
         for (const TrackGroup& gg : m.trackGroups) if (gg.id == cur) { p = &gg; break; }
         if (!p) break; // already reported as a dangling parent above
         cur = p->parentGroupId;
      }
   }

   Tick prev = 0;
   bool firstMarker = true;
   for (const Marker& mk : m.markers)
   {
      if (mk.id == 0) return fail("marker with id 0");
      if (!ids.insert(mk.id).second) return fail("duplicate marker id " + std::to_string(mk.id));
      if (mk.id >= m.nextId) return fail("marker id >= nextId");
      if (mk.pos < 0) return fail("negative marker position");
      if (!firstMarker && mk.pos < prev) return fail("markers unsorted");
      prev = mk.pos;
      firstMarker = false;
   }
   if (why) why->clear();
   return true;
}

Tick ArrangementEnd(const Model& m)
{
   Tick end = 0;
   for (const Lane& l : m.lanes)
      if (!l.clips.empty()) end = std::max(end, l.clips.back().End());
   return end;
}

bool PlaceOverwrite(Model& m, uint64_t laneId, Clip clip, uint64_t* outId)
{
   Lane* lane = FindLane(m, laneId);
   if (!lane) return false;
   if (clip.length <= 0) return false;
   if (clip.start < 0) clip.start = 0;
   ClampFades(clip);
   if (clip.id == 0) clip.id = m.NewId();

   CarveRange(m, *lane, clip.start, clip.End());
   lane->clips.push_back(clip);
   SortLane(*lane);
   DissolveSingletonGroups(m);
   m.revision++;
   if (outId) *outId = clip.id;
   return true;
}

bool MoveClips(Model& m, const std::vector<uint64_t>& ids, Tick deltaTick, int laneDelta)
{
   if (ids.empty()) return false;
   if (deltaTick == 0 && laneDelta == 0) return false;

   // Gather, checking every id resolves and every destination lane exists and
   // matches the clip's current lane type. Bail before touching anything.
   struct Moving { Clip clip; int dstLane; };
   std::vector<Moving> moving;
   std::unordered_set<uint64_t> movingIds;
   Tick minStart = 0;
   bool haveMin = false;
   for (uint64_t id : ids)
   {
      const Loc loc = Find(m, id);
      if (!loc.Valid()) return false;
      if (!movingIds.insert(id).second) continue;
      const Clip& c = m.lanes[loc.lane].clips[loc.index];
      const int dst = loc.lane + laneDelta;
      if (dst < 0 || dst >= (int)m.lanes.size()) return false;
      if (m.lanes[dst].type != m.lanes[loc.lane].type) return false;
      if (!haveMin || c.start < minStart) { minStart = c.start; haveMin = true; }
      moving.push_back(Moving{c, dst});
   }
   if (moving.empty()) return false;
   // The whole block stops at 0 rather than each clip clamping individually,
   // which would silently squash the block's internal spacing.
   if (minStart + deltaTick < 0) deltaTick = -minStart;
   if (deltaTick == 0 && laneDelta == 0) return false;

   for (Moving& mv : moving) mv.clip.start += deltaTick;

   // Moved clips must not collide with each other (two clips from different
   // lanes can land on the same one).
   std::map<int, std::vector<const Clip*>> byDst;
   for (const Moving& mv : moving) byDst[mv.dstLane].push_back(&mv.clip);
   for (auto& kv : byDst)
   {
      std::vector<const Clip*>& v = kv.second;
      std::sort(v.begin(), v.end(), [](const Clip* a, const Clip* b) { return a->start < b->start; });
      for (size_t i = 1; i < v.size(); i++)
         if (v[i]->start < v[i - 1]->End()) return false;
   }

   // Lift the movers out, carve their destinations, drop them back in.
   for (Lane& l : m.lanes)
      l.clips.erase(std::remove_if(l.clips.begin(), l.clips.end(),
                                   [&](const Clip& c) { return movingIds.count(c.id) != 0; }),
                    l.clips.end());
   for (const Moving& mv : moving)
      CarveRange(m, m.lanes[mv.dstLane], mv.clip.start, mv.clip.End());
   for (const Moving& mv : moving)
      m.lanes[mv.dstLane].clips.push_back(mv.clip);
   for (Lane& l : m.lanes) SortLane(l);
   DissolveSingletonGroups(m);
   m.revision++;
   return true;
}

bool TrimEdge(Model& m, uint64_t id, int edge, Tick tick)
{
   const Loc loc = Find(m, id);
   if (!loc.Valid()) return false;
   Lane& lane = m.lanes[loc.lane];
   Clip& c = lane.clips[loc.index];

   if (edge == kEdgeStart)
   {
      const Tick lo = (loc.index > 0) ? lane.clips[loc.index - 1].End() : 0;
      const Tick hi = c.End() - 1;
      tick = std::clamp(tick, lo, hi);
      if (tick == c.start) return false;
      const Tick end = c.End();
      // Audio-Sample-only: the trimmed-off portion is no longer played, so
      // the source read-point must skip forward by the same amount, in
      // source seconds (see SampleSourceBpm) - same formula Split uses.
      c.sourceOffsetSeconds += (float)ClipSourceAdvance(c, tick - c.start);
      // MIDI: the notes stay put in content time; the left edge moves into them.
      c.contentOffset += tick - c.start;
      c.start = tick;
      c.length = end - tick;
   }
   else
   {
      const Tick lo = c.start + 1;
      // No neighbour to the right: cap at kMaxTick rather than a raw int64
      // sentinel, so a runaway drag can't produce a clip whose end converts to
      // ~10^11 seconds and takes ArrangementEnd (and the ruler) with it.
      const Tick hi = (loc.index + 1 < (int)lane.clips.size())
                          ? lane.clips[loc.index + 1].start
                          : kMaxTick;
      tick = std::clamp(tick, lo, hi);
      if (tick == c.End()) return false;
      c.length = tick - c.start;
   }
   ClampFades(c);
   SortLane(lane);
   m.revision++;
   return true;
}

bool Split(Model& m, uint64_t id, Tick tick, uint64_t* outRightId)
{
   const Loc loc = Find(m, id);
   if (!loc.Valid()) return false;
   Lane& lane = m.lanes[loc.lane];
   Clip& c = lane.clips[loc.index];
   if (tick <= c.start || tick >= c.End()) return false;

   Clip right = c;
   right.id = m.NewId();
   right.start = tick;
   right.length = c.End() - tick;
   right.fadeIn = 0;                    // the cut is not a fade
   ClampFades(right);
   // Audio-Sample-only: the right half continues reading the source file
   // exactly where the cut sits, in source seconds (see SampleSourceBpm), so
   // the two halves play back as one uninterrupted take.
   right.sourceOffsetSeconds = c.sourceOffsetSeconds + (float)ClipSourceAdvance(c, tick - c.start);
   // MIDI: both halves keep every note; each shows its own part of the content.
   right.contentOffset = c.contentOffset + (tick - c.start);

   c.length = tick - c.start;
   c.fadeOut = 0;
   ClampFades(c);

   lane.clips.insert(lane.clips.begin() + loc.index + 1, right);
   SortLane(lane);
   m.revision++;
   if (outRightId) *outRightId = right.id;
   return true;
}

bool DuplicateBlock(Model& m, const std::vector<uint64_t>& ids, std::vector<uint64_t>* outNew)
{
   if (ids.empty()) return false;
   struct Src { Clip clip; int lane; };
   std::vector<Src> src;
   std::unordered_set<uint64_t> seen;
   Tick minStart = 0, maxEnd = 0;
   bool have = false;
   for (uint64_t id : ids)
   {
      const Loc loc = Find(m, id);
      if (!loc.Valid()) continue;
      if (!seen.insert(id).second) continue;
      const Clip& c = m.lanes[loc.lane].clips[loc.index];
      if (!have) { minStart = c.start; maxEnd = c.End(); have = true; }
      else { minStart = std::min(minStart, c.start); maxEnd = std::max(maxEnd, c.End()); }
      src.push_back(Src{c, loc.lane});
   }
   if (src.empty()) return false;
   // A copy that would push a MIDI lane past its note cap is refused whole.
   {
      std::unordered_map<int, int> added;
      for (const Src& s : src) added[s.lane] += (int)s.clip.notes.size();
      for (const auto& kv : added)
         if (kv.second > 0 && LaneNoteCount(m.lanes[kv.first]) + kv.second > kMaxNotesPerLane) return false;
   }

   // One block delta for every copy - copying each clip to just after itself
   // is what made the first copy overwrite the second original (WP5).
   const Tick delta = maxEnd - minStart;
   std::unordered_map<uint64_t, uint64_t> groupRemap;
   std::vector<uint64_t> made;
   for (const Src& s : src)
   {
      Clip c = s.clip;
      c.id = m.NewId();
      c.start += delta;
      if (c.groupId != 0)
      {
         auto it = groupRemap.find(c.groupId);
         if (it == groupRemap.end()) it = groupRemap.emplace(c.groupId, m.NewId()).first;
         c.groupId = it->second;
      }
      CarveRange(m, m.lanes[s.lane], c.start, c.End());
      m.lanes[s.lane].clips.push_back(c);
      made.push_back(c.id);
   }
   for (Lane& l : m.lanes) SortLane(l);
   DissolveSingletonGroups(m);
   m.revision++;
   if (outNew) *outNew = made;
   return true;
}

bool Delete(Model& m, const std::vector<uint64_t>& ids)
{
   if (ids.empty()) return false;
   std::unordered_set<uint64_t> kill(ids.begin(), ids.end());
   bool changed = false;
   for (Lane& l : m.lanes)
   {
      const size_t before = l.clips.size();
      l.clips.erase(std::remove_if(l.clips.begin(), l.clips.end(),
                                   [&](const Clip& c) { return kill.count(c.id) != 0; }),
                    l.clips.end());
      changed = changed || l.clips.size() != before;
   }
   if (!changed) return false;
   DissolveSingletonGroups(m);
   m.revision++;
   return true;
}

bool SetEnabled(Model& m, const std::vector<uint64_t>& ids, int mode)
{
   if (ids.empty()) return false;
   std::unordered_set<uint64_t> want(ids.begin(), ids.end());
   bool changed = false;
   for (Lane& l : m.lanes)
      for (Clip& c : l.clips)
      {
         if (!want.count(c.id)) continue;
         const bool next = (mode == kToggle) ? !c.enabled : (mode == kEnable);
         if (next != c.enabled) { c.enabled = next; changed = true; }
      }
   if (changed) m.revision++;
   return changed;
}

std::vector<uint64_t> ClipsInGroup(const Model& m, uint64_t groupId)
{
   std::vector<uint64_t> out;
   if (groupId == 0) return out;
   for (const Lane& l : m.lanes)
      for (const Clip& c : l.clips)
         if (c.groupId == groupId) out.push_back(c.id);
   return out;
}

std::vector<uint64_t> ExpandSelectionToGroups(const Model& m, const std::vector<uint64_t>& ids)
{
   std::unordered_set<uint64_t> groups;
   for (uint64_t id : ids)
      if (const Clip* c = FindClip(m, id))
         if (c->groupId != 0) groups.insert(c->groupId);
   std::unordered_set<uint64_t> out(ids.begin(), ids.end());
   for (const Lane& l : m.lanes)
      for (const Clip& c : l.clips)
         if (c.groupId != 0 && groups.count(c.groupId)) out.insert(c.id);
   std::vector<uint64_t> v(out.begin(), out.end());
   std::sort(v.begin(), v.end());
   return v;
}

bool Group(Model& m, const std::vector<uint64_t>& ids, uint64_t* outGroupId)
{
   // Groups are whole: any member drags its entire group in, so grouping
   // merges (two groups, or a group plus loose clips, become one) and can
   // never split a group by stealing some of its members.
   std::unordered_set<uint64_t> uniq;
   for (uint64_t id : ExpandSelectionToGroups(m, ids))
      if (Find(m, id).Valid()) uniq.insert(id);
   if (uniq.size() < 2) return false;
   // Already exactly one group: nothing to do (a fresh id would only
   // recolour it and cost an undo entry).
   uint64_t only = 0;
   bool oneGroup = true;
   for (uint64_t id : uniq)
   {
      const uint64_t g = FindClip(m, id)->groupId;
      if (g == 0 || (only != 0 && g != only)) { oneGroup = false; break; }
      only = g;
   }
   if (oneGroup)
   {
      if (outGroupId) *outGroupId = only;
      return false;
   }
   const uint64_t gid = m.NewId();
   for (Lane& l : m.lanes)
      for (Clip& c : l.clips)
         if (uniq.count(c.id)) c.groupId = gid;
   DissolveSingletonGroups(m);
   m.revision++;
   if (outGroupId) *outGroupId = gid;
   return true;
}

bool Ungroup(Model& m, const std::vector<uint64_t>& groupIds)
{
   std::unordered_set<uint64_t> g(groupIds.begin(), groupIds.end());
   g.erase(0);
   if (g.empty()) return false;
   bool changed = false;
   for (Lane& l : m.lanes)
      for (Clip& c : l.clips)
         if (g.count(c.groupId)) { c.groupId = 0; changed = true; }
   if (changed) m.revision++;
   return changed;
}

bool RemoveFromGroup(Model& m, const std::vector<uint64_t>& ids)
{
   std::unordered_set<uint64_t> want(ids.begin(), ids.end());
   bool changed = false;
   for (Lane& l : m.lanes)
      for (Clip& c : l.clips)
         if (want.count(c.id) && c.groupId != 0) { c.groupId = 0; changed = true; }
   if (!changed) return false;
   DissolveSingletonGroups(m);
   m.revision++;
   return true;
}

bool TrimGroupEdge(Model& m, uint64_t groupId, int edge, Tick tick)
{
   // Only the members flush with the dragged edge move; interior clips keep
   // their own bounds (Ableton's group-edge behaviour, WP5).
   //
   // Runs under a live drag, once per frame, so it is one pass over the lanes
   // with no per-member Find (the old version was O(members x clips)). The
   // in-place trim is exact: a start-edge trim never moves any clip's end,
   // and an end-edge trim never moves any clip's start, so the neighbour each
   // member clamps against is the same whether or not it is itself a member.
   if (groupId == 0) return false;
   Tick bound = 0;
   bool have = false;
   for (const Lane& l : m.lanes)
      for (const Clip& c : l.clips)
      {
         if (c.groupId != groupId) continue;
         const Tick v = (edge == kEdgeStart) ? c.start : c.End();
         if (!have) { bound = v; have = true; }
         else bound = (edge == kEdgeStart) ? std::min(bound, v) : std::max(bound, v);
      }
   if (!have) return false;

   bool changed = false;
   for (Lane& lane : m.lanes)
   {
      for (size_t i = 0; i < lane.clips.size(); i++)
      {
         Clip& c = lane.clips[i];
         if (c.groupId != groupId) continue;
         if (edge == kEdgeStart)
         {
            if (c.start != bound) continue;
            const Tick lo = (i > 0) ? lane.clips[i - 1].End() : 0;
            const Tick hi = c.End() - 1;
            const Tick t = std::clamp(tick, lo, hi);
            if (t == c.start) continue;
            const Tick end = c.End();
            // Same source-offset adjustment as TrimEdge's kEdgeStart branch.
            c.sourceOffsetSeconds += (float)ClipSourceAdvance(c, t - c.start);
            c.start = t;
            c.length = end - t;
         }
         else
         {
            if (c.End() != bound) continue;
            const Tick lo = c.start + 1;
            const Tick hi = (i + 1 < lane.clips.size()) ? lane.clips[i + 1].start : kMaxTick;
            const Tick t = std::clamp(tick, lo, hi);
            if (t == c.End()) continue;
            c.length = t - c.start;
         }
         ClampFades(c);
         changed = true;
      }
   }
   // One bump per op, like every other op (it used to bump once per member).
   if (changed) m.revision++;
   return changed;
}

bool ScaleGroup(Model& m, uint64_t groupId, int edge, Tick tick)
{
   if (groupId == 0) return false;
   Tick lo = 0, hi = 0;
   int count = 0;
   for (const Lane& l : m.lanes)
      for (const Clip& c : l.clips)
      {
         if (c.groupId != groupId) continue;
         if (count == 0) { lo = c.start; hi = c.End(); }
         else { lo = std::min(lo, c.start); hi = std::max(hi, c.End()); }
         count++;
      }
   if (count < 2 || hi <= lo) return false;

   // Scale about the opposite edge; a factor <= 0 would invert the block.
   const Tick pivot = (edge == kEdgeStart) ? hi : lo;
   const double oldSpan = (double)(hi - lo);
   const double newSpan = (edge == kEdgeStart) ? (double)(pivot - tick) : (double)(tick - pivot);
   if (!(newSpan > 0.0)) return false;
   const double f = newSpan / oldSpan;

   auto scaledSpan = [&](const Clip& c, Tick& outStart, Tick& outLen)
   {
      const Tick s = pivot + (Tick)llround((double)(c.start - pivot) * f);
      const Tick e = pivot + (Tick)llround((double)(c.End() - pivot) * f);
      outStart = std::max<Tick>(0, std::min(s, e));
      outLen = std::max<Tick>(1, std::llabs(e - s));
   };

   // Check first, lane by lane, touching only lanes that hold a member: each
   // lane's clips as (start, end) with members at their scaled span, sorted,
   // must not overlap. Scaling can push a member into a non-member neighbour;
   // rather than silently eating an untouched clip, refuse the gesture. (This
   // used to copy the whole Model and Validate it - per frame, under a drag.)
   bool any = false;
   std::vector<std::pair<Tick, Tick>> spans;
   for (const Lane& l : m.lanes)
   {
      bool laneHasMember = false;
      for (const Clip& c : l.clips)
         if (c.groupId == groupId) { laneHasMember = true; break; }
      if (!laneHasMember) continue;
      spans.clear();
      for (const Clip& c : l.clips)
      {
         if (c.groupId == groupId)
         {
            Tick s = 0, len = 0;
            scaledSpan(c, s, len);
            if (s != c.start || len != c.length) any = true;
            spans.emplace_back(s, s + len);
         }
         else
            spans.emplace_back(c.start, c.End());
      }
      std::sort(spans.begin(), spans.end());
      for (size_t i = 1; i < spans.size(); i++)
         if (spans[i].first < spans[i - 1].second) return false;
   }
   if (!any) return false;

   for (Lane& l : m.lanes)
   {
      bool touched = false;
      for (Clip& c : l.clips)
      {
         if (c.groupId != groupId) continue;
         Tick s = 0, len = 0;
         scaledSpan(c, s, len);
         c.start = s;
         c.length = len;
         ClampFades(c);
         touched = true;
      }
      if (touched) SortLane(l);
   }
   m.revision++;
   return true;
}

uint64_t AddLane(Model& m, int type, int atIndex)
{
   Lane l;
   l.id = m.NewId();
   l.type = (type == kLaneAudio || type == kLaneMidi) ? type : kLaneVideo;
   l.name = UniqueLaneName(m, LaneTypeName(l.type), l.type);
   const int at = (atIndex < 0 || atIndex > (int)m.lanes.size()) ? (int)m.lanes.size() : atIndex;
   m.lanes.insert(m.lanes.begin() + at, l);
   m.revision++;
   return l.id;
}

bool RemoveLane(Model& m, uint64_t laneId)
{
   const int i = LaneIndex(m, laneId);
   if (i < 0) return false;
   const uint64_t trackGroupId = m.lanes[i].groupId;
   m.lanes.erase(m.lanes.begin() + i);
   DissolveSingletonGroups(m);
   // A track group left with nothing in its subtree is dead weight - prune
   // it (and cascade up through now-empty ancestors) immediately rather than
   // waiting for Normalize, since ops here don't rely on Normalize running
   // after them (see ArrangeEdit in main.cpp).
   if (trackGroupId != 0) PruneEmptyTrackGroupChain(m, trackGroupId);
   m.revision++;
   return true;
}

std::string UniqueLaneName(const Model& m, const std::string& baseName, int type)
{
   std::string stem = baseName;
   int startNum = 1;
   if (stem.empty())
   {
      stem = LaneTypeName(type);
      startNum = 1;
   }
   else
   {
      size_t lastSpace = stem.find_last_of(' ');
      if (lastSpace != std::string::npos && lastSpace + 1 < stem.size())
      {
         bool allDigits = true;
         for (size_t i = lastSpace + 1; i < stem.size(); i++)
         {
            if (!std::isdigit(static_cast<unsigned char>(stem[i]))) { allDigits = false; break; }
         }
         if (allDigits)
         {
            try {
               int num = std::stoi(stem.substr(lastSpace + 1));
               startNum = num + 1;
               stem = stem.substr(0, lastSpace);
            } catch (...) {
               startNum = 2;
            }
         }
         else
         {
            startNum = 2;
         }
      }
      else
      {
         startNum = 2;
      }
   }

   auto exists = [&](const std::string& cand) {
      for (const Lane& l : m.lanes)
         if (l.name == cand) return true;
      return false;
   };

   int num = startNum;
   std::string cand = stem + " " + std::to_string(num);
   while (exists(cand))
   {
      num++;
      cand = stem + " " + std::to_string(num);
   }
   return cand;
}

std::string UniqueTrackGroupName(const Model& m, const std::string& baseName)
{
   std::string stem = baseName;
   int startNum = 1;
   if (stem.empty())
   {
      stem = "Group";
      startNum = 1;
   }
   else
   {
      size_t lastSpace = stem.find_last_of(' ');
      if (lastSpace != std::string::npos && lastSpace + 1 < stem.size())
      {
         bool allDigits = true;
         for (size_t i = lastSpace + 1; i < stem.size(); i++)
         {
            if (!std::isdigit(static_cast<unsigned char>(stem[i]))) { allDigits = false; break; }
         }
         if (allDigits)
         {
            try {
               int num = std::stoi(stem.substr(lastSpace + 1));
               startNum = num + 1;
               stem = stem.substr(0, lastSpace);
            } catch (...) {
               startNum = 2;
            }
         }
         else
         {
            startNum = 2;
         }
      }
      else
      {
         startNum = 2;
      }
   }

   auto exists = [&](const std::string& cand) {
      for (const TrackGroup& g : m.trackGroups)
         if (g.name == cand) return true;
      return false;
   };

   int num = startNum;
   std::string cand = stem + " " + std::to_string(num);
   while (exists(cand))
   {
      num++;
      cand = stem + " " + std::to_string(num);
   }
   return cand;
}

bool DuplicateLane(Model& m, uint64_t laneId, uint64_t* outLaneId)
{
   const int i = LaneIndex(m, laneId);
   if (i < 0) return false;

   Lane nl = m.lanes[i];
   nl.id = m.NewId();
   nl.noteNodeUid = 0; // a Piano Roll node plays one page; the copy comes out unlinked
   nl.name = UniqueLaneName(m, nl.name.empty() ? LaneTypeName(nl.type) : nl.name, nl.type);
   std::unordered_map<uint64_t, uint64_t> clipGroupRemap;
   for (Clip& c : nl.clips)
   {
      c.id = m.NewId();
      if (c.groupId != 0)
      {
         auto it = clipGroupRemap.find(c.groupId);
         if (it == clipGroupRemap.end()) it = clipGroupRemap.emplace(c.groupId, m.NewId()).first;
         c.groupId = it->second;
      }
   }
   for (SessionSlot& sl : nl.slots)
      if (sl.Has())
         sl.clip.id = m.NewId();
   m.lanes.insert(m.lanes.begin() + i + 1, nl);
   m.revision++;
   if (outLaneId) *outLaneId = nl.id;
   return true;
}

bool ReorderLane(Model& m, uint64_t laneId, int newIndex)
{
   const int i = LaneIndex(m, laneId);
   if (i < 0) return false;
   newIndex = std::clamp(newIndex, 0, (int)m.lanes.size() - 1);
   if (newIndex == i) return false;
   Lane l = m.lanes[i];
   m.lanes.erase(m.lanes.begin() + i);
   m.lanes.insert(m.lanes.begin() + newIndex, l);
   m.revision++;
   return true;
}

void MoveLanesBefore(Model& m, const std::vector<uint64_t>& laneIds, size_t beforeLaneIndex)
{
   if (laneIds.empty()) return;
   const std::unordered_set<uint64_t> moveSet(laneIds.begin(), laneIds.end());

   // Once the moved lanes are pulled out, every index at or after
   // beforeLaneIndex shifts left by however many of them sat before it.
   size_t shift = 0;
   for (size_t i = 0; i < m.lanes.size() && i < beforeLaneIndex; i++)
      if (moveSet.count(m.lanes[i].id))
         shift++;

   std::vector<Lane> moved, rest;
   moved.reserve(laneIds.size());
   rest.reserve(m.lanes.size());
   for (Lane& l : m.lanes)
   {
      if (moveSet.count(l.id)) moved.push_back(std::move(l));
      else rest.push_back(std::move(l));
   }
   if (moved.empty()) return; // none of laneIds matched a real lane

   const size_t insertAt = std::min(beforeLaneIndex - std::min(beforeLaneIndex, shift), rest.size());
   rest.insert(rest.begin() + (std::ptrdiff_t)insertAt, moved.begin(), moved.end());
   m.lanes = std::move(rest);
   m.revision++;
}

bool SetLaneEnabled(Model& m, uint64_t laneId, int mode)
{
   Lane* l = FindLane(m, laneId);
   if (!l) return false;
   const bool next = (mode == kToggle) ? !l->enabled : (mode == kEnable);
   if (next == l->enabled) return false;
   l->enabled = next;
   m.revision++;
   return true;
}

bool ClearSource(Model& m, uint64_t uid)
{
   if (uid == 0) return false;
   bool changed = false;
   for (Lane& l : m.lanes)
   {
      for (Clip& c : l.clips)
         if (c.srcUid == uid) { c.srcUid = 0; changed = true; }
      for (SessionSlot& sl : l.slots)
         if (sl.clip.srcUid == uid) { sl.clip.srcUid = 0; changed = true; }
   }
   if (changed) m.revision++;
   return changed;
}

// --- track groups --------------------------------------------------------

uint64_t AddTrackGroup(Model& m, const std::vector<uint64_t>& laneIds, const std::string& name, uint64_t parentGroupId)
{
   TrackGroup g;
   g.id = m.NewId();
   g.name = UniqueTrackGroupName(m, name.empty() ? "Group" : name);
   g.parentGroupId = (parentGroupId != 0 && FindTrackGroup(m, parentGroupId)) ? parentGroupId : 0;
   m.trackGroups.push_back(g);
   for (uint64_t laneId : laneIds)
      if (Lane* l = FindLane(m, laneId))
         l->groupId = g.id;
   m.revision++;
   return g.id;
}

bool RemoveTrackGroup(Model& m, uint64_t groupId, bool deleteLanes)
{
   if (groupId == 0) return false;
   auto it = std::find_if(m.trackGroups.begin(), m.trackGroups.end(),
                          [&](const TrackGroup& g) { return g.id == groupId; });
   if (it == m.trackGroups.end()) return false;

   if (deleteLanes)
   {
      // Delete the whole subtree: every lane nested at any depth, and every
      // nested child group record. Erase lanes first so no lane is ever
      // left pointing at a group record that no longer exists, even
      // transiently within this op - Validate() checks that invariant and
      // could run mid-frame.
      const std::vector<uint64_t> subtreeLanes = LanesInTrackGroupRecursive(m, groupId);
      const std::unordered_set<uint64_t> killLanes(subtreeLanes.begin(), subtreeLanes.end());
      m.lanes.erase(std::remove_if(m.lanes.begin(), m.lanes.end(),
                                   [&](const Lane& l) { return killLanes.count(l.id) != 0; }),
                    m.lanes.end());
      DissolveSingletonGroups(m);

      std::unordered_set<uint64_t> killGroups;
      killGroups.insert(groupId);
      bool grew = true;
      while (grew)
      {
         grew = false;
         for (const TrackGroup& g : m.trackGroups)
            if (killGroups.count(g.parentGroupId) && !killGroups.count(g.id)) { killGroups.insert(g.id); grew = true; }
      }
      m.trackGroups.erase(std::remove_if(m.trackGroups.begin(), m.trackGroups.end(),
                                         [&](const TrackGroup& g) { return killGroups.count(g.id) != 0; }),
                          m.trackGroups.end());
   }
   else
   {
      // Ungroup: promote every direct member - lanes and child groups alike
      // - to this group's own parent, then remove just this one record. One
      // level of promotion, not a jump straight to the top, so ungrouping a
      // nested group leaves its contents nested exactly where the group was.
      const uint64_t newParent = it->parentGroupId;
      for (Lane& l : m.lanes)
         if (l.groupId == groupId) l.groupId = newParent;
      for (TrackGroup& g : m.trackGroups)
         if (g.parentGroupId == groupId) g.parentGroupId = newParent;
      m.trackGroups.erase(std::find_if(m.trackGroups.begin(), m.trackGroups.end(),
                                       [&](const TrackGroup& g) { return g.id == groupId; }));
   }
   m.revision++;
   return true;
}

bool SetLaneTrackGroup(Model& m, uint64_t laneId, uint64_t groupId)
{
   Lane* l = FindLane(m, laneId);
   if (!l) return false;
   if (groupId != 0 && !FindTrackGroup(m, groupId)) return false;
   if (l->groupId == groupId) return false;
   const uint64_t prevGroup = l->groupId;
   l->groupId = groupId;
   if (prevGroup != 0) PruneEmptyTrackGroupChain(m, prevGroup);
   m.revision++;
   return true;
}

bool SetTrackGroupParent(Model& m, uint64_t groupId, uint64_t newParentGroupId)
{
   if (groupId == 0) return false;
   TrackGroup* g = nullptr;
   for (TrackGroup& gg : m.trackGroups) if (gg.id == groupId) { g = &gg; break; }
   if (!g) return false;
   if (newParentGroupId == groupId) return false;
   if (newParentGroupId == g->parentGroupId) return false;
   if (newParentGroupId != 0)
   {
      if (!FindTrackGroup(m, newParentGroupId)) return false;
      // Reject moving groupId under its own descendant: if groupId appears
      // in newParentGroupId's own ancestor chain, newParentGroupId is
      // already nested inside groupId, and reparenting would create a loop.
      for (uint64_t a : GroupAncestors(m, newParentGroupId))
         if (a == groupId) return false;
   }
   const uint64_t oldParent = g->parentGroupId;
   g->parentGroupId = newParentGroupId;
   if (oldParent != 0) PruneEmptyTrackGroupChain(m, oldParent);
   m.revision++;
   return true;
}

uint64_t GroupSelectedLanes(Model& m, const std::vector<uint64_t>& laneIds, uint64_t parentGroupId)
{
   if (laneIds.empty()) return 0;
   return AddTrackGroup(m, laneIds, std::string(), parentGroupId);
}

bool RenameTrackGroup(Model& m, uint64_t groupId, const std::string& name)
{
   for (TrackGroup& g : m.trackGroups)
      if (g.id == groupId)
      {
         if (g.name == name) return false;
         g.name = name;
         m.revision++;
         return true;
      }
   return false;
}

bool RecolorTrackGroup(Model& m, uint64_t groupId, uint32_t color)
{
   for (TrackGroup& g : m.trackGroups)
      if (g.id == groupId)
      {
         if (g.color == color) return false;
         g.color = color;
         m.revision++;
         return true;
      }
   return false;
}

bool SetTrackGroupEnabled(Model& m, uint64_t groupId, int mode)
{
   for (TrackGroup& g : m.trackGroups)
      if (g.id == groupId)
      {
         const bool next = (mode == kToggle) ? !g.enabled : (mode == kEnable);
         if (next == g.enabled) return false;
         g.enabled = next;
         m.revision++;
         return true;
      }
   return false;
}

bool SetTrackGroupCollapsed(Model& m, uint64_t groupId, bool collapsed)
{
   for (TrackGroup& g : m.trackGroups)
      if (g.id == groupId)
      {
         if (g.collapsed == collapsed) return false;
         g.collapsed = collapsed;
         m.revision++;
         return true;
      }
   return false;
}

const TrackGroup* FindTrackGroup(const Model& m, uint64_t groupId)
{
   if (groupId == 0) return nullptr;
   for (const TrackGroup& g : m.trackGroups)
      if (g.id == groupId) return &g;
   return nullptr;
}

std::vector<uint64_t> LanesInTrackGroup(const Model& m, uint64_t groupId)
{
   std::vector<uint64_t> out;
   if (groupId == 0) return out;
   for (const Lane& l : m.lanes)
      if (l.groupId == groupId) out.push_back(l.id);
   return out;
}

std::vector<uint64_t> LanesInTrackGroupRecursive(const Model& m, uint64_t groupId)
{
   std::vector<uint64_t> out;
   if (groupId == 0) return out;
   std::unordered_set<uint64_t> subtreeGroups;
   subtreeGroups.insert(groupId);
   bool grew = true;
   while (grew)
   {
      grew = false;
      for (const TrackGroup& g : m.trackGroups)
         if (subtreeGroups.count(g.parentGroupId) && !subtreeGroups.count(g.id)) { subtreeGroups.insert(g.id); grew = true; }
   }
   for (const Lane& l : m.lanes)
      if (l.groupId != 0 && subtreeGroups.count(l.groupId)) out.push_back(l.id);
   return out;
}

std::vector<uint64_t> GroupAncestors(const Model& m, uint64_t groupId)
{
   std::vector<uint64_t> out;
   const TrackGroup* g = FindTrackGroup(m, groupId);
   uint64_t cur = g ? g->parentGroupId : 0;
   size_t hops = 0;
   while (cur != 0 && hops++ <= m.trackGroups.size())
   {
      out.push_back(cur);
      const TrackGroup* p = FindTrackGroup(m, cur);
      if (!p) break;
      cur = p->parentGroupId;
   }
   return out;
}

int GroupDepth(const Model& m, uint64_t groupId)
{
   return (int)GroupAncestors(m, groupId).size();
}

bool DuplicateTrackGroup(Model& m, uint64_t groupId, uint64_t* outGroupId)
{
   const TrackGroup* src = FindTrackGroup(m, groupId);
   if (!src) return false;
   if (LanesInTrackGroupRecursive(m, groupId).empty()) return false;

   // Collect the whole subtree of group ids (groupId plus every descendant).
   std::vector<uint64_t> subtreeGroups;
   subtreeGroups.push_back(groupId);
   for (size_t i = 0; i < subtreeGroups.size(); i++)
   {
      const uint64_t gid = subtreeGroups[i];
      for (const TrackGroup& g : m.trackGroups)
         if (g.parentGroupId == gid) subtreeGroups.push_back(g.id);
   }

   // Fresh id for every group in the subtree up front, so lane and
   // child-group parent links can be rewritten to point at the duplicates
   // in a single pass below.
   std::unordered_map<uint64_t, uint64_t> groupRemap;
   for (uint64_t gid : subtreeGroups) groupRemap[gid] = m.NewId();

   // New records, parent links rewired to point at the new duplicates - the
   // root of the subtree keeps the SOURCE's original parent, so the
   // duplicate lands as a sibling of the source, not nested inside it.
   std::vector<TrackGroup> newGroups;
   newGroups.reserve(subtreeGroups.size());
   for (uint64_t gid : subtreeGroups)
   {
      const TrackGroup* g = FindTrackGroup(m, gid);
      if (!g) continue;
      TrackGroup ng = *g;
      ng.id = groupRemap[gid];
      ng.parentGroupId = (gid == groupId) ? g->parentGroupId : groupRemap[g->parentGroupId];
      ng.name = UniqueTrackGroupName(m, ng.name.empty() ? "Group" : ng.name);
      newGroups.push_back(ng);
   }

   // Where the duplicated lanes land in m.lanes: TrackGroupChildren orders
   // sibling rows by their lanes' position in this vector, so appending at
   // the very end (the old behaviour) made every duplicate render as the
   // LAST group in the whole arrangement instead of right below its
   // source. Insert directly after the source subtree's own last lane
   // instead, so the duplicate comes out as an immediate sibling.
   size_t insertAt = m.lanes.size();
   {
      std::unordered_set<uint64_t> subtreeGroupSet(subtreeGroups.begin(), subtreeGroups.end());
      for (size_t i = 0; i < m.lanes.size(); i++)
         if (subtreeGroupSet.count(m.lanes[i].groupId))
            insertAt = i + 1;
   }

   // Every lane anywhere in the subtree, with fresh ids and clip-groupId
   // remapping so a duplicated subtree never shares a clip group with its
   // source - same convention DuplicateBlock uses for a plain clip
   // duplicate. Snapshot m.lanes first: inserting into m.lanes below would
   // invalidate a live range-based iteration over it.
   const std::vector<Lane> lanesSnapshot = m.lanes;
   std::unordered_map<uint64_t, uint64_t> clipGroupRemap;
   std::vector<Lane> newLanes;
   for (const Lane& srcLane : lanesSnapshot)
   {
      if (srcLane.groupId == 0 || !groupRemap.count(srcLane.groupId)) continue;
      Lane nl = srcLane;
      nl.id = m.NewId();
      nl.noteNodeUid = 0;
      nl.groupId = groupRemap[srcLane.groupId];
      nl.name = UniqueLaneName(m, nl.name.empty() ? LaneTypeName(nl.type) : nl.name, nl.type);
      for (Clip& c : nl.clips)
      {
         c.id = m.NewId();
         if (c.groupId != 0)
         {
            auto it = clipGroupRemap.find(c.groupId);
            if (it == clipGroupRemap.end()) it = clipGroupRemap.emplace(c.groupId, m.NewId()).first;
            c.groupId = it->second;
         }
      }
      newLanes.push_back(std::move(nl));
   }
   m.lanes.insert(m.lanes.begin() + (std::ptrdiff_t)insertAt, newLanes.begin(), newLanes.end());

   for (TrackGroup& ng : newGroups) m.trackGroups.push_back(ng);
   m.revision++;
   if (outGroupId) *outGroupId = groupRemap[groupId];
   return true;
}

bool LaneEffectivelyEnabled(const Model& m, const Lane& lane)
{
   if (!lane.enabled) return false;
   uint64_t g = lane.groupId;
   size_t hops = 0;
   while (g != 0 && hops++ <= m.trackGroups.size())
   {
      const TrackGroup* grp = FindTrackGroup(m, g);
      if (!grp) return true; // dangling groupId (shouldn't happen) reads as enabled
      if (!grp->enabled) return false;
      g = grp->parentGroupId;
   }
   return true;
}

std::vector<RowSlot> TrackGroupChildren(const Model& m, uint64_t parentGroupId)
{
   std::vector<RowSlot> out;
   std::unordered_set<uint64_t> emitted;
   for (const Lane& l : m.lanes)
   {
      if (l.groupId == parentGroupId) { out.push_back(RowSlot{false, l.id}); continue; }
      // Walk this lane's group chain up to find the ancestor that is a
      // direct child of parentGroupId - that's this lane's slot at this
      // level (the lane itself might be several levels deeper). A lane
      // whose chain never reaches parentGroupId lives under a different
      // branch and contributes nothing here.
      uint64_t g = l.groupId;
      uint64_t slot = 0;
      size_t hops = 0;
      while (g != 0 && hops++ <= m.trackGroups.size())
      {
         const TrackGroup* grp = FindTrackGroup(m, g);
         if (!grp) break;
         if (grp->parentGroupId == parentGroupId) { slot = g; break; }
         g = grp->parentGroupId;
      }
      if (slot != 0 && emitted.insert(slot).second)
         out.push_back(RowSlot{true, slot});
   }
   // Child groups with nothing under them anywhere (no lane's chain reaches
   // them by the walk above) still need to draw - append in trackGroups
   // storage order, same fallback the old flat model used implicitly.
   for (const TrackGroup& g : m.trackGroups)
      if (g.parentGroupId == parentGroupId && emitted.insert(g.id).second)
         out.push_back(RowSlot{true, g.id});
   return out;
}

uint64_t AddMarker(Model& m, Tick pos, const std::string& name, uint32_t color)
{
   Marker mk;
   mk.id = m.NewId();
   mk.pos = std::clamp<Tick>(pos, 0, kMaxTick);
   mk.name = name;
   mk.color = color;
   m.markers.push_back(mk);
   std::stable_sort(m.markers.begin(), m.markers.end(),
                    [](const Marker& a, const Marker& b) { return a.pos < b.pos; });
   m.revision++;
   return mk.id;
}

bool MoveMarker(Model& m, uint64_t id, Tick pos)
{
   pos = std::clamp<Tick>(pos, 0, kMaxTick);
   for (Marker& mk : m.markers)
      if (mk.id == id)
      {
         if (mk.pos == pos) return false;
         mk.pos = pos;
         std::stable_sort(m.markers.begin(), m.markers.end(),
                          [](const Marker& a, const Marker& b) { return a.pos < b.pos; });
         m.revision++;
         return true;
      }
   return false;
}

bool RenameMarker(Model& m, uint64_t id, const std::string& name)
{
   for (Marker& mk : m.markers)
      if (mk.id == id)
      {
         if (mk.name == name) return false;
         mk.name = name;
         m.revision++;
         return true;
      }
   return false;
}

bool RecolorMarker(Model& m, uint64_t id, uint32_t color)
{
   for (Marker& mk : m.markers)
      if (mk.id == id)
      {
         if (mk.color == color) return false;
         mk.color = color;
         m.revision++;
         return true;
      }
   return false;
}

bool DeleteMarker(Model& m, uint64_t id)
{
   const size_t before = m.markers.size();
   m.markers.erase(std::remove_if(m.markers.begin(), m.markers.end(),
                                  [&](const Marker& mk) { return mk.id == id; }),
                   m.markers.end());
   if (m.markers.size() == before) return false;
   m.revision++;
   return true;
}

Tick SnapGridTicks(int division, bool triplet, double beatsPerBar)
{
   if (division <= 0) return 0;
   if (division == 1)
   {
      if (!(beatsPerBar > 0.0)) beatsPerBar = 4.0;
      return std::max<Tick>(1, BeatsToTicks(beatsPerBar));
   }
   const double whole = (double)(kPPQ * 4);
   const double step = whole / (double)division * (triplet ? 2.0 / 3.0 : 1.0);
   return std::max<Tick>(1, (Tick)llround(step));
}

Tick GridFloor(Tick t, Tick grid)
{
   if (grid <= 0) return t;
   Tick q = t / grid;
   if (t < 0 && q * grid != t) --q;
   return q * grid;
}

Tick GridCeil(Tick t, Tick grid)
{
   if (grid <= 0) return t;
   const Tick f = GridFloor(t, grid);
   return f == t ? t : f + grid;
}

Tick SnapToGrid(Tick t, Tick grid)
{
   if (grid <= 0) return t;
   const Tick f = GridFloor(t, grid);
   return (t - f) * 2 >= grid ? f + grid : f;
}

const Marker* PrevMarker(const Model& m, Tick t, Tick tolerance)
{
   const Marker* best = nullptr;
   for (const Marker& mk : m.markers)
   {
      if (mk.pos < t - tolerance) best = &mk;
      else break;
   }
   return best;
}

const Marker* NextMarker(const Model& m, Tick t, Tick tolerance)
{
   for (const Marker& mk : m.markers)
      if (mk.pos > t + tolerance) return &mk;
   return nullptr;
}
}

// --- notes (piano-roll README §5) ---------------------------------------
namespace Arrange
{
namespace
{
   // The MIDI clip with this id (a Timeline clip or a session slot) and its lane.
   Clip* FindMidiClip(Model& m, uint64_t clipId, Lane** outLane)
   {
      if (clipId == 0) return nullptr;
      for (Lane& l : m.lanes)
      {
         if (l.type != kLaneMidi) continue;
         for (Clip& c : l.clips)
            if (c.id == clipId) { *outLane = &l; return &c; }
         for (SessionSlot& sl : l.slots)
            if (sl.Has() && sl.clip.id == clipId) { *outLane = &l; return &sl.clip; }
      }
      return nullptr;
   }

   int NoteIndex(const Clip& c, uint32_t id)
   {
      for (int i = 0; i < (int)c.notes.size(); i++)
         if (c.notes[i].id == id) return i;
      return -1;
   }

   // Snaps a pitch to the nearest pitch class in mask; up first on a tie.
   int SnapPitchToMask(int p, uint16_t mask)
   {
      if ((mask & 0x0FFF) == 0) return p;
      for (int d = 0; d < 12; d++)
      {
         if (p + d <= 127 && (mask >> ((p + d) % 12)) & 1) return p + d;
         if (p - d >= 0 && (mask >> ((p - d) % 12)) & 1) return p - d;
      }
      return p;
   }
}

const Note* FindNote(const Clip& c, uint32_t noteId)
{
   const int i = NoteIndex(c, noteId);
   return i >= 0 ? &c.notes[i] : nullptr;
}

int LaneNoteCount(const Lane& l)
{
   size_t n = 0;
   for (const Clip& c : l.clips) n += c.notes.size();
   for (const SessionSlot& sl : l.slots) n += sl.clip.notes.size();
   return (int)n;
}

bool AddNotes(Model& m, uint64_t clipId, const std::vector<Note>& notes, std::vector<uint32_t>* outIds)
{
   if (outIds) outIds->clear();
   Lane* lane = nullptr;
   Clip* c = FindMidiClip(m, clipId, &lane);
   if (!c || notes.empty()) return false;
   if (LaneNoteCount(*lane) + (int)notes.size() > kMaxNotesPerLane) return false;
   for (Note n : notes)
   {
      ClampNote(n);
      n.id = c->nextNoteId++;
      if (outIds) outIds->push_back(n.id);
      c->notes.push_back(std::move(n));
   }
   ResolveNoteOverlaps(*c);
   m.revision++;
   return true;
}

bool EditNotes(Model& m, uint64_t clipId, const std::vector<Note>& notes)
{
   Lane* lane = nullptr;
   Clip* c = FindMidiClip(m, clipId, &lane);
   if (!c) return false;
   bool changed = false;
   for (Note n : notes)
   {
      const int i = NoteIndex(*c, n.id);
      if (i < 0) continue;
      ClampNote(n);
      if (c->notes[i] == n) continue;
      c->notes[i] = std::move(n);
      changed = true;
   }
   if (!changed) return false;
   ResolveNoteOverlaps(*c);
   m.revision++;
   return true;
}

bool DeleteNotes(Model& m, uint64_t clipId, const std::vector<uint32_t>& ids)
{
   Lane* lane = nullptr;
   Clip* c = FindMidiClip(m, clipId, &lane);
   if (!c || ids.empty()) return false;
   std::unordered_set<uint32_t> kill(ids.begin(), ids.end());
   const size_t before = c->notes.size();
   c->notes.erase(std::remove_if(c->notes.begin(), c->notes.end(),
                                 [&](const Note& n) { return kill.count(n.id) != 0; }),
                  c->notes.end());
   if (c->notes.size() == before) return false;
   m.revision++;
   return true;
}

bool MoveNotes(Model& m, uint64_t clipId, const std::vector<uint32_t>& ids, Tick dTick, int dPitch, uint16_t scaleMask)
{
   Lane* lane = nullptr;
   Clip* c = FindMidiClip(m, clipId, &lane);
   if (!c || ids.empty()) return false;
   std::unordered_set<uint32_t> want(ids.begin(), ids.end());
   Tick minStart = kMaxTick;
   int lo = 127, hi = 0;
   for (const Note& n : c->notes)
      if (want.count(n.id))
      {
         minStart = std::min(minStart, n.start);
         lo = std::min(lo, (int)n.pitch);
         hi = std::max(hi, (int)n.pitch);
      }
   if (minStart == kMaxTick) return false;
   // The block moves as one piece: clamp the deltas, not each note.
   if (minStart + dTick < 0) dTick = -minStart;
   dPitch = std::clamp(dPitch, -lo, 127 - hi);
   if (dTick == 0 && dPitch == 0 && scaleMask == 0) return false;
   bool changed = false;
   for (Note& n : c->notes)
   {
      if (!want.count(n.id)) continue;
      int p = (int)n.pitch + dPitch;
      if (scaleMask != 0) p = SnapPitchToMask(p, scaleMask);
      const Tick s = n.start + dTick;
      if (s != n.start || p != n.pitch) changed = true;
      n.start = s;
      n.pitch = (uint8_t)std::clamp(p, 0, 127);
   }
   if (!changed) return false;
   ResolveNoteOverlaps(*c);
   m.revision++;
   return true;
}

bool SplitNotes(Model& m, uint64_t clipId, const std::vector<uint32_t>& ids, Tick at)
{
   Lane* lane = nullptr;
   Clip* c = FindMidiClip(m, clipId, &lane);
   if (!c || ids.empty()) return false;
   std::unordered_set<uint32_t> want(ids.begin(), ids.end());
   std::vector<Note> rights;
   for (Note& n : c->notes)
   {
      if (!want.count(n.id) || at <= n.start || at >= n.End()) continue;
      Note r = n;
      r.id = c->nextNoteId++;
      r.start = at;
      r.length = n.End() - at;
      const Tick cut = at - n.start;
      // Expression: each half keeps its own points, the right one re-based.
      for (auto member : { &Note::bend, &Note::pressure, &Note::timbre })
      {
         std::vector<ExprPoint> left, right;
         for (const ExprPoint& p : n.*member)
            if (p.at <= cut) left.push_back(p);
            else right.push_back({ p.at - cut, p.v });
         n.*member = left;
         r.*member = right;
      }
      n.length = cut;
      rights.push_back(std::move(r));
   }
   if (rights.empty()) return false;
   for (Note& r : rights) c->notes.push_back(std::move(r));
   ResolveNoteOverlaps(*c);
   m.revision++;
   return true;
}

bool JoinNotes(Model& m, uint64_t clipId, const std::vector<uint32_t>& ids)
{
   Lane* lane = nullptr;
   Clip* c = FindMidiClip(m, clipId, &lane);
   if (!c || ids.size() < 2) return false;
   std::unordered_set<uint32_t> want(ids.begin(), ids.end());
   std::map<int, std::vector<int>> byPitch; // indices, already in start order
   for (int i = 0; i < (int)c->notes.size(); i++)
      if (want.count(c->notes[i].id)) byPitch[c->notes[i].pitch].push_back(i);
   std::unordered_set<uint32_t> kill;
   for (auto& kv : byPitch)
   {
      if (kv.second.size() < 2) continue;
      Note& first = c->notes[kv.second.front()];
      Tick end = first.End();
      for (size_t k = 1; k < kv.second.size(); k++)
      {
         const Note& n = c->notes[kv.second[k]];
         end = std::max(end, n.End());
         kill.insert(n.id);
      }
      first.length = end - first.start;
   }
   if (kill.empty()) return false;
   c->notes.erase(std::remove_if(c->notes.begin(), c->notes.end(),
                                 [&](const Note& n) { return kill.count(n.id) != 0; }),
                  c->notes.end());
   ResolveNoteOverlaps(*c);
   m.revision++;
   return true;
}

bool QuantizeNotes(Model& m, uint64_t clipId, const std::vector<uint32_t>& ids, const QuantizeSpec& q)
{
   Lane* lane = nullptr;
   Clip* c = FindMidiClip(m, clipId, &lane);
   if (!c || ids.empty() || q.grid <= 0) return false;
   const float strength = std::clamp(q.strength, 0.0f, 1.0f);
   const float swing = std::clamp(q.swing, 0.0f, 1.0f);
   // Grid line k sits at k*grid, every odd line pushed by swing * grid/2.
   auto target = [&](Tick t) -> Tick {
      const Tick k = (t + q.grid / 2) / q.grid;
      Tick g = k * q.grid;
      if (k % 2 == 1) g += (Tick)llround((double)swing * (double)q.grid * 0.5);
      return g;
   };
   std::unordered_set<uint32_t> want(ids.begin(), ids.end());
   bool changed = false;
   for (Note& n : c->notes)
   {
      if (!want.count(n.id)) continue;
      const Tick end = n.End();
      const Tick s = n.start + (Tick)llround((double)(target(n.start) - n.start) * strength);
      Tick e = end;
      if (q.ends)
         e = end + (Tick)llround((double)(target(end) - end) * strength);
      else
         e = s + n.length;
      if (e <= s) e = s + std::max<Tick>(1, q.grid / 4);
      if (s != n.start || e != end) changed = true;
      n.start = std::max<Tick>(0, s);
      n.length = e - n.start;
   }
   if (!changed) return false;
   ResolveNoteOverlaps(*c);
   m.revision++;
   return true;
}

bool LinkLane(Model& m, uint64_t laneId, uint64_t nodeUid)
{
   Lane* target = FindLane(m, laneId);
   if (!target || target->type != kLaneMidi) return false;
   if (target->noteNodeUid == nodeUid) return false;
   if (nodeUid != 0)
      for (Lane& l : m.lanes)
         if (l.noteNodeUid == nodeUid) l.noteNodeUid = 0;
   target->noteNodeUid = nodeUid;
   m.revision++;
   return true;
}
}
