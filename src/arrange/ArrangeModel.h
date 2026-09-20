#pragma once

// Arrangement timeline data model (docs/plans/arrangement/overhaul-prompt.md,
// WP1). Pure data plus edit operations: no ImGui, no OpenGL, no audio, no
// dependency on main.cpp. Everything here is headless-testable, which is the
// whole point - the arrangement's invariants used to live inside 74k lines of
// UI code where nothing could check them.
//
// Two invariants hold everywhere outside the body of an edit op:
//   1. A lane's clips are sorted by `start` and never overlap.
//   2. Every clip id is unique within the model, and so is every lane and
//      marker id. Ids come from Model::nextId, which is persisted, so a
//      deleted clip's id is never handed out again even across a reload.
// `Validate()` checks both (plus more); debug builds call it after every op.
//
// Time is ticks, never seconds. kPPQ = 960 divides by 2, 3, 4, 5, 6, 8 and 16,
// so triplets and quintuplets land exactly on integers and abutting clip edges
// compare equal. A tempo change therefore keeps every clip on its bar/beat and
// only changes what those ticks mean in seconds.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Arrange
{
   using Tick = int64_t;
   constexpr Tick kPPQ = 960;            // ticks per quarter note
   constexpr Tick kTicksPerBar = kPPQ * 4;
   // Upper bound for any tick position or clip end. 100,000 bars - about 55
   // hours at 120 bpm, and still ~24 bits of headroom inside int64 so tick
   // arithmetic (sums, differences, llround of a beat multiply) cannot wrap.
   // Every unbounded edge - a trim with no neighbour to the right, a drag
   // past the end of the timeline - clamps here rather than at int64 max, so
   // ArrangementEnd stays a number the ruler can draw.
   constexpr Tick kMaxTick = kTicksPerBar * 100000; // 4/4 only - no meter map yet

   inline double TicksToBeats(Tick t) { return (double)t / (double)kPPQ; }
   inline Tick   BeatsToTicks(double beats) { return (Tick)llround(beats * (double)kPPQ); }
   inline double TicksToSeconds(Tick t, double bpm)
   {
      if (!(bpm > 0.0)) bpm = 120.0;
      return TicksToBeats(t) * 60.0 / bpm;
   }
   inline Tick SecondsToTicks(double sec, double bpm)
   {
      if (!(bpm > 0.0)) bpm = 120.0;
      return BeatsToTicks(sec * bpm / 60.0);
   }

   // Audio Sample timing, one rule for audio, waveform and every edit:
   //
   //   source seconds covered by N ticks = TicksToSeconds(N, effBpm)
   //   effBpm = syncToTempo ? sampleBpm : live project tempo
   //
   // Synced: the box is in beats of the SAMPLE's tempo, and playback
   // time-stretches by projectTempo / sampleBpm, so a tempo change keeps the
   // same audio in the same box. Unsynced: audio plays at native speed, so
   // the box covers (lengthBeats * 60 / projectTempo) seconds of the file and
   // a tempo change alone reveals or hides audio at the box's end - the box
   // itself never moves. Sample BPM only matters while synced.
   //
   // The live tempo is not part of the model, so edits that convert ticks to
   // source seconds (Split, TrimEdge, PlaceOverwrite) read it from here.
   // main.cpp sets it every frame from Transport::Tempo(); tests set it
   // directly.
   inline double gSampleLiveTempoBpm = 120.0;

   inline double SampleSourceBpm(bool syncToTempo, float sampleBpm, double liveBpm)
   {
      if (syncToTempo && sampleBpm > 0.0f)
         return (double)sampleBpm;
      return liveBpm > 0.0 ? liveBpm : 120.0;
   }

   // Ticks that `seconds` of source audio occupy at effBpm.
   inline Tick SampleClipLengthTicks(double seconds, double effBpm)
   {
      if (!(seconds > 0.0)) return 1;
      if (!(effBpm > 0.0)) effBpm = 120.0;
      return std::max<Tick>(1, (Tick)llround(seconds * (effBpm / 60.0) * (double)kPPQ));
   }

   enum LaneType { kLaneVideo = 0, kLaneAudio = 1 };
   enum Edge { kEdgeStart = 0, kEdgeEnd = 1 };
   // SetEnabled's third argument.
   enum EnableMode { kDisable = 0, kEnable = 1, kToggle = 2 };

   struct Clip
   {
      uint64_t id       = 0;   // unique in the model, never reused
      Tick     start    = 0;   // >= 0
      Tick     length   = 0;   // > 0
      uint64_t srcUid   = 0;   // GraphNode::uid, 0 = unassigned ("offline")
      int      srcOutput = 0;  // which output of srcUid, as CableRecord::srcOutput
      Tick     fadeIn   = 0;   // 0..length
      Tick     fadeOut  = 0;   // 0..length
      float    gainDb   = 0.0f;
      bool     enabled  = true; // the `0` key (WP5)
      uint64_t groupId  = 0;   // 0 = not grouped; a live group has >= 2 members
      std::string name;        // empty = auto (the source node's own title)
      float    colorR = 0.0f, colorG = 0.0f, colorB = 0.0f; // 0,0,0 = no tint
      int      blendMode = 0;  // video only, index into BlendModes::Names()

      // --- sample-dropped media clips (audio drop / video-image drop) ----
      float    pan     = 0.0f;  // audio only, -1..1, independent of the lane's own pan
      float    pitch   = 0.0f;  // audio only, semitones, +/-24 - mirrors SamplerNode::pitch
      // Audio-Sample-only - see SampleSourceBpm above for what these mean.
      bool     syncToTempo = true;
      // The sample's own tempo: estimated at import, user-editable. Drives
      // playback only while syncToTempo is on.
      float    sampleBpm = 120.0f;
      // The tempo the import-time analysis detected (0 or negative = nothing
      // detected). Display only - shown next to Sample BPM so a user edit can
      // always be compared against, or reset to, the analysis.
      float    origBpm = 120.0f;
      // The decoded file's natural duration in seconds, captured at import.
      float    sourceDurationSeconds = 0.0f;
      // Audio-Sample-only. How far into the decoded source buffer this
      // clip's audio starts, in seconds. 0 for a freshly imported/bounced
      // Sample (it starts at the file's own beginning); a Split() gives the
      // right half a non-zero value derived from how far the cut point sits
      // past the original clip's own offset (see Split's own comment) so a
      // split Sample keeps reading the right part of the file instead of
      // restarting at the beginning. Never touched afterward outside of
      // another Split - a Sample BPM edit changes `length` but not where in
      // the file playback starts.
      float    sourceOffsetSeconds = 0.0f;
      // Video/image only. Basic color grade, consumed by the compositor as a
      // per-clip shader pass. Defaults are a no-op so every clip that
      // predates this field renders identically.
      float    opacity         = 1.0f;  // video only, 0..1, 1 = fully opaque
      float    colorBrightness = 0.0f;  // -1..1, 0 = no change
      float    colorContrast   = 0.0f;  // -1..1, 0 = no change
      float    colorSaturation = 1.0f;  // 0..2, 1 = no change
      bool     retrigger       = true;  // true = retrigger source on playhead/marker enter, false = timeline continuous

      // True only for a clip created by ArrangeImportMediaFile (dropping a
      // media file onto the timeline, which spawns its own private source
      // node) - "Audio/Video Sample" in the Clip Settings panel and the only
      // category allowed to retrigger. False for a clip whose srcUid points
      // at a node the user patched in manually ("Audio/Video Clip"), even if
      // that node happens to be an AudioFileNode/VideoSourceNode.
      bool     sampleDropped   = false;

      // Runtime-only: true while a dropped media file's async decode
      // (ArrangeMediaImport.h) hasn't landed yet. Deliberately not read or
      // written by Patch.cpp/PatchJson.cpp - a save always sees this false
      // (imports resolve in well under a save's timeframe), so it never
      // round-trips and needs no persistence format of its own.
      bool     importPending = false;

      // Per-clip modulation bypass. Each entry is a paramIndex on the source
      // node (srcUid) whose modulation this clip does NOT want applied - the
      // param sits at its own base value for as long as this clip is the one
      // playing, while every other clip on the same source still hears the
      // modulator. Empty on every clip that predates this field, which is
      // exactly today's behaviour (nothing bypassed).
      //
      // paramIndex is the same positional index Modulation binds on
      // (src/core/Modulation.h's ParamKey) - it only exists once the node has
      // drawn a frame, and it shifts if a node's param layout changes between
      // versions. That is the identical contract modulation bindings already
      // live under, deliberately: a clip's bypass list is only ever meaningful
      // alongside the binding it refers to, so if one goes stale the other
      // does too and they stay consistent with each other.
      //
      // Sorted and deduplicated by SetModBypassed so equality (the undo
      // comparator in main.cpp) is a plain vector compare and the patch line
      // is stable across saves.
      std::vector<int> bypassedModParams;

      bool IsModBypassed(int paramIndex) const
      {
         return std::find(bypassedModParams.begin(), bypassedModParams.end(), paramIndex) !=
                bypassedModParams.end();
      }

      void SetModBypassed(int paramIndex, bool on)
      {
         auto it = std::lower_bound(bypassedModParams.begin(), bypassedModParams.end(), paramIndex);
         const bool present = (it != bypassedModParams.end() && *it == paramIndex);
         if (on && !present)
            bypassedModParams.insert(it, paramIndex);
         else if (!on && present)
            bypassedModParams.erase(it);
      }

      Tick End() const { return start + length; }
   };

   struct Lane
   {
      uint64_t id        = 0;
      int      type      = kLaneVideo;
      int      blendMode = 0;     // legacy: pre-clip-blend files; migrated onto clips at load, always 0 after
      float    opacity   = 1.0f;  // video only, 0..1
      float    gainDb    = 0.0f;  // audio only
      float    pan       = 0.0f;  // audio only, -1..1
      bool     enabled   = true;  // track active/inactive
      uint64_t groupId   = 0;     // 0 = not in a track group
      bool     mute      = false; // audio only
      bool     solo      = false; // audio only; any soloed audio lane silences the unsoloed ones
      std::string name;           // empty = auto ("V1", "A2", ...)
      float    colorR = 0.0f, colorG = 0.0f, colorB = 0.0f; // 0,0,0 = default type accent
      float    rowHeight = 0.0f;  // 0 = default row height; drag-resized via the row's bottom border
      std::vector<Clip> clips;    // always sorted by start, never overlapping
   };

   // A track group: a named, colored container for lanes AND other track
   // groups (unlimited nesting, Bitwig/Ableton/Logic folder-track style).
   // Unlike a clip group (Clip::groupId), a track group does NOT dissolve at
   // 1 or 0 members - Normalize prunes only fully-empty ones (no lanes and no
   // child groups left anywhere in its subtree), never a singleton. A group
   // with one track left in it is still a meaningful, user-named container
   // the user may be about to add more tracks to. There is no collapsed
   // state: every group's full subtree always renders.
   struct TrackGroup
   {
      uint64_t    id            = 0;
      std::string name;           // empty = auto ("Group 1", ...)
      uint32_t    color         = 0xFF808080u; // RGBA8
      bool        enabled       = true;
      bool        collapsed     = false;
      uint64_t    parentGroupId = 0; // 0 = top-level, sits directly under the root
   };

   struct Marker
   {
      uint64_t id  = 0;
      Tick     pos = 0;
      std::string name;
      uint32_t color = 0xFFFFFFFFu; // RGBA8
   };

   struct LoopRange
   {
      bool enabled = false;
      Tick start   = 0;
      Tick end     = 0;
   };

   // Per-patch view and render state. Audio mode is deliberately NOT here: the
   // app always starts in Canvas mode (WP3 owner decision), so it must not be
   // saved.
   struct Settings
   {
      int   timeDisplay  = 0;     // 0 = Bars, 1 = Time
      int   snapDivision = 4;     // grid denominator: 0 = off, 1 = bar, 4 = 1/4, 16 = 1/16, ... (WP6)
      bool  snapTriplet  = false; // x2/3 on divisions >= 2; ignored for bar/off
      float zoom         = 1.0f;  // pixels per beat multiplier
      float scroll       = 0.0f;  // leftmost visible beat
      LoopRange loop;
      int   dockSide     = 0;     // 0 = bottom, 1 = top (WP5)
      // Render defaults (WP7).
      int   renderWidth  = 1920;
      int   renderHeight = 1080;
      int   renderFps    = 60;
      // Legacy: a render is always written at the rate the audio graph was
      // prepared at (Settings -> Audio), never at a per-arrangement choice,
      // so nothing reads this. Kept because it is in the patch format.
      int   renderSampleRate = 48000;
      int   renderFormat = 0;     // 0 = mp4, 1 = mov, 2 = wav
      int   renderRangeKind = 0;  // 0 = whole, 1 = loop, 2 = markers, 3 = custom
      Tick  renderRangeStart = 0;
      Tick  renderRangeEnd   = 0;
      // Deprecated (kept only so old patches keep round-tripping, and so the
      // positional binary stream in Patch.cpp keeps its field order). The
      // timeline's Render dialog no longer offers a source choice: it renders
      // the timeline, with video when the range holds video clips and a WAV
      // when it does not. Nothing reads these any more - do not reintroduce a
      // reader without also restoring the UI that sets them.
      int   renderAudioSource = -1;
      int   renderVideoSource = -1;
      std::string renderFolder;
      // Bitwig/Ableton-style import default: the Sync to Tempo value a new
      // Sample clip is stamped with the moment it's created - drag-drop
      // import (ArrangeImportMediaFile) and Bounce to Sample
      // (ArrangeApplyClipBounceResult) both read this instead of hardcoding
      // true, so the decision is made once, up front, rather than requiring
      // a trip to Clip Settings' checkbox after the fact for every drop.
      // Still just a per-clip default - Clip::syncToTempo remains freely
      // editable per clip afterward.
      bool  importSyncToTempo = true;
   };

   struct Model
   {
      std::vector<Lane>       lanes;
      std::vector<Marker>     markers;   // sorted by pos
      std::vector<TrackGroup> trackGroups;
      Settings                settings;
      uint64_t            nextId  = 1; // persisted; never recomputed as max+1
      uint64_t            revision = 0; // bumped by every op that changed data

      uint64_t NewId() { return nextId++; }
   };

   // Where a clip lives right now. Indices are only valid until the next edit.
   struct Loc
   {
      int lane = -1;
      int index = -1;
      bool Valid() const { return lane >= 0 && index >= 0; }
   };

   Loc Find(const Model& m, uint64_t clipId);
   const Clip* FindClip(const Model& m, uint64_t clipId);
   Clip*       FindClip(Model& m, uint64_t clipId);
   const Lane* FindLane(const Model& m, uint64_t laneId);
   Lane*       FindLane(Model& m, uint64_t laneId);
   int         LaneIndex(const Model& m, uint64_t laneId);

   // Every invariant this model promises, in one place. `why` (optional) gets
   // a human-readable first failure.
   bool Validate(const Model& m, std::string* why = nullptr);

   // End of the last clip on any lane, 0 when empty.
   Tick ArrangementEnd(const Model& m);

   // --- clip edit ops --------------------------------------------------
   // Each returns true if the model actually changed, and bumps `revision`
   // exactly once when it does. A false return leaves the model untouched.

   // Overwrite-style insert: trims or splits whatever `clip` lands on, then
   // inserts it in sorted position. `clip.id` is assigned if it is 0.
   bool PlaceOverwrite(Model& m, uint64_t laneId, Clip clip, uint64_t* outId = nullptr);

   // Moves every id by the same tick delta and the same lane delta. Moved
   // clips never trim each other - if they would overlap after the move, or
   // any lands on a lane of the wrong type, nothing moves and this returns
   // false. Stationary clips in the target lanes are overwritten.
   bool MoveClips(Model& m, const std::vector<uint64_t>& ids, Tick deltaTick, int laneDelta);

   // Drags one edge to `tick`. Clamped so length stays > 0 and the clip never
   // crosses its neighbours (the neighbour wins; use PlaceOverwrite to eat it).
   bool TrimEdge(Model& m, uint64_t id, int edge, Tick tick);

   // Cuts at an absolute tick strictly inside the clip. The right half gets a
   // fresh id (returned through outRightId) and keeps the group membership.
   bool Split(Model& m, uint64_t id, Tick tick, uint64_t* outRightId = nullptr);

   // Copies the selection as one block landing immediately after its own max
   // end, preserving relative lane and tick offsets. Groups are re-created
   // with fresh group ids so the copy is its own group.
   bool DuplicateBlock(Model& m, const std::vector<uint64_t>& ids, std::vector<uint64_t>* outNew = nullptr);

   bool Delete(Model& m, const std::vector<uint64_t>& ids);
   bool SetEnabled(Model& m, const std::vector<uint64_t>& ids, int mode);

   // --- groups ---------------------------------------------------------
   // A group needs >= 2 members; grouping fewer does nothing. Dropping to one
   // member (delete, ungroup-one) dissolves the group rather than leaving a
   // singleton, which Validate would reject. Group works on whole groups: a
   // member brings its group along, so mixed selections merge into one new
   // group, and a selection that already is exactly one group is a no-op.
   bool Group(Model& m, const std::vector<uint64_t>& ids, uint64_t* outGroupId = nullptr);
   bool Ungroup(Model& m, const std::vector<uint64_t>& groupIds);
   bool RemoveFromGroup(Model& m, const std::vector<uint64_t>& ids);
   std::vector<uint64_t> ClipsInGroup(const Model& m, uint64_t groupId);
   // Expands a selection so that every grouped member's whole group is in it.
   std::vector<uint64_t> ExpandSelectionToGroups(const Model& m, const std::vector<uint64_t>& ids);

   // WP5 fills these in; declared now so the UI can be written against the
   // final shape. TrimGroupEdge trims only the members touching `edge`;
   // ScaleGroup scales every member proportionally about the opposite edge.
   bool TrimGroupEdge(Model& m, uint64_t groupId, int edge, Tick tick);
   bool ScaleGroup(Model& m, uint64_t groupId, int edge, Tick tick);

   // --- lanes ----------------------------------------------------------
   uint64_t AddLane(Model& m, int type, int atIndex = -1);
   bool RemoveLane(Model& m, uint64_t laneId);
   bool ReorderLane(Model& m, uint64_t laneId, int newIndex);
   // Repositions every lane in `laneIds` (their current relative order
   // preserved) to sit contiguously in m.lanes, immediately before
   // `beforeLaneIndex` (an index into m.lanes as it stood before this call).
   // Row display order for both lanes and group headers is entirely derived
   // from each lane's position in m.lanes (see TrackGroupChildren) - this is
   // the primitive that actually moves a row (or a whole group's lanes)
   // within that order. SetLaneTrackGroup/SetTrackGroupParent only change
   // which group a row belongs to, not where it sits, so the Arrange
   // panel's drag-and-drop calls both: reparent first, then this.
   void MoveLanesBefore(Model& m, const std::vector<uint64_t>& laneIds, size_t beforeLaneIndex);
   // Copies a lane (settings + every clip, all fresh ids; a clip group on the
   // source gets a fresh clip-group id too, same convention as DuplicateBlock
   // and DuplicateTrackGroup) and inserts it immediately after the source,
   // in the same track group. New lane's id is returned through outLaneId.
   bool DuplicateLane(Model& m, uint64_t laneId, uint64_t* outLaneId = nullptr);
   bool SetLaneEnabled(Model& m, uint64_t laneId, int mode); // EnableMode
   // Clears srcUid on every clip pointing at `uid` instead of deleting the
   // clip: deleting a node leaves its clips offline, not gone (WP5).
   bool ClearSource(Model& m, uint64_t uid);

   // --- track groups -----------------------------------------------------
   // A container for lanes AND other track groups, distinct from a clip Group
   // above: it does not auto-dissolve at 1 or 0 members (Normalize prunes
   // only groups with nothing left anywhere in their subtree).
   //
   // `parentGroupId` nests a new group under an existing one (0 = top level).
   uint64_t AddTrackGroup(Model& m, const std::vector<uint64_t>& laneIds, const std::string& name = std::string(), uint64_t parentGroupId = 0);
   // Removes the group record. `deleteLanes` also deletes every member lane
   // (and their clips), including nested child groups' lanes; otherwise
   // members (lanes and child groups alike) are promoted to the dissolved
   // group's own parent, kept in place - "Ungroup" one level, not to root.
   bool RemoveTrackGroup(Model& m, uint64_t groupId, bool deleteLanes);
   bool SetLaneTrackGroup(Model& m, uint64_t laneId, uint64_t groupId); // 0 = ungroup
   // Reparents a group under another group (0 = top level). Rejects making a
   // group its own ancestor or descendant (would create a cycle) and a
   // dangling target group id.
   bool SetTrackGroupParent(Model& m, uint64_t groupId, uint64_t newParentGroupId);
   // Creates one new group containing exactly the given lanes, nested under
   // `parentGroupId` (0 = top level). Used by "Group Selected".
   uint64_t GroupSelectedLanes(Model& m, const std::vector<uint64_t>& laneIds, uint64_t parentGroupId = 0);
   bool RenameTrackGroup(Model& m, uint64_t groupId, const std::string& name);
   bool RecolorTrackGroup(Model& m, uint64_t groupId, uint32_t color);
   bool SetTrackGroupEnabled(Model& m, uint64_t groupId, int mode); // EnableMode
   bool SetTrackGroupCollapsed(Model& m, uint64_t groupId, bool collapsed);
   std::string UniqueLaneName(const Model& m, const std::string& baseName, int type);
   std::string UniqueTrackGroupName(const Model& m, const std::string& baseName);
   const TrackGroup* FindTrackGroup(const Model& m, uint64_t groupId);
   // Direct member lanes only (does not recurse into nested child groups).
   std::vector<uint64_t> LanesInTrackGroup(const Model& m, uint64_t groupId);
   // Every lane in the group's subtree, including lanes nested inside child
   // groups at any depth. Used by "Render Group" and subtree drag/duplicate.
   std::vector<uint64_t> LanesInTrackGroupRecursive(const Model& m, uint64_t groupId);
   // groupId's parent, its parent's parent, ... up to (not including) the
   // root. Empty if groupId is already top-level or unknown.
   std::vector<uint64_t> GroupAncestors(const Model& m, uint64_t groupId);
   // Nesting depth: 0 for a top-level group, 1 for a group directly under a
   // top-level group, etc.
   int GroupDepth(const Model& m, uint64_t groupId);
   // Duplicates a track group and its entire subtree: every member lane (with
   // its clips, fresh ids) at every depth, every nested child group (fresh
   // ids, parent links remapped to point at the new duplicates), and a fresh
   // record for the group itself. Clip-level groupIds inside duplicated clips
   // are remapped to fresh ids too, so the duplicate never shares a clip
   // group with its source. New group's id is returned through outGroupId.
   bool DuplicateTrackGroup(Model& m, uint64_t groupId, uint64_t* outGroupId = nullptr);
   // True if the lane should actually run: lane.enabled and every ancestor
   // group (its own group, that group's parent, ... up to the root) is
   // enabled. The one place the whole ancestor chain is read together - do
   // not hand-check `lane.enabled` or `group->enabled` directly anywhere
   // else; a lane or group disabled by an ancestor must read as disabled.
   bool LaneEffectivelyEnabled(const Model& m, const Lane& lane);
   // Row slot at one level of the tree: either a lane or a child group,
   // interleaved in the order they should draw. Used by row layout, the
   // flattened selection order, and "Group With" menus.
   struct RowSlot { bool isGroup = false; uint64_t id = 0; };
   // Direct children of `parentGroupId` (0 = root), in draw order: lanes and
   // child groups interleaved by where their earliest member lane sits in
   // `m.lanes`, with wholly-empty child groups appended last in
   // `m.trackGroups` order. Does not recurse - callers walk depth-first by
   // calling this again with each returned group's id.
   std::vector<RowSlot> TrackGroupChildren(const Model& m, uint64_t parentGroupId);

   // --- markers --------------------------------------------------------
   uint64_t AddMarker(Model& m, Tick pos, const std::string& name = std::string(), uint32_t color = 0xFFFFFFFFu);
   bool MoveMarker(Model& m, uint64_t id, Tick pos);
   bool RenameMarker(Model& m, uint64_t id, const std::string& name);
   bool RecolorMarker(Model& m, uint64_t id, uint32_t color);
   bool DeleteMarker(Model& m, uint64_t id);

   // --- grid and marker navigation (WP6) -------------------------------
   // The snap grid's step in ticks. `division` is Settings::snapDivision:
   // 0 = off (returns 0), 1 = one bar of `beatsPerBar` quarter notes (triplet
   // ignored), d >= 2 = 1/d of a whole note, x2/3 when `triplet`. At kPPQ 960
   // every power-of-two division down to 1/256 and its triplet is exact.
   Tick SnapGridTicks(int division, bool triplet, double beatsPerBar = 4.0);
   // Nearest / floor / ceiling multiple of `grid`; grid <= 0 returns t.
   Tick SnapToGrid(Tick t, Tick grid);
   Tick GridFloor(Tick t, Tick grid);
   Tick GridCeil(Tick t, Tick grid);
   // Last marker with pos < t - tolerance / first with pos > t + tolerance,
   // nullptr when there is none. Markers are sorted, so these are the
   // Alt+Left / Alt+Right jump targets. The tolerance lets a playing
   // playhead that has just passed a marker still jump to the one before it.
   const Marker* PrevMarker(const Model& m, Tick t, Tick tolerance = 0);
   const Marker* NextMarker(const Model& m, Tick t, Tick tolerance = 0);

   // Re-sorts, clamps and dissolves singleton groups. Called at the end of
   // every op and after deserialization; safe to call on anything.
   void Normalize(Model& m);
}
