// ===========================================================================
// Arrangement Timeline (Infinite-Turbo 0.43)
//
// Port of upstream Infinite's Arrangement Timeline (src/arrange/ArrangeModel
// is upstream's verbatim, and so is the patch format), adapted to this fork's
// audio engine, undo and panels. Included once, inside main.cpp's anonymous
// namespace, so it sees the graph (gNodes), the undo stack and the UI helpers.
//
//   model      gArrange (Arrange::Model): lanes (video / audio), clips in
//              ticks (960 per quarter note), markers, loop, view settings.
//   audio      Timeline mode: each audio lane's clips become windows on an
//              ArrangeTerminal (AudioEngine) - gated, faded and panned per
//              sample; canvas Audio Outs are muted (live ones excepted).
//   video      every video lane at the playhead is composited (blend mode,
//              opacity, fades, grade) for the panel monitor and the
//              Timeline node.
//   undo       every edit snapshots the whole patch (this fork's undo), the
//              arrangement included; gestures push once, at the end.
// ===========================================================================

// ---- node lookup / classification ------------------------------------------

GraphNode* FindNodeByUid(uint64_t uid)
{
   if (uid == 0)
      return nullptr;
   for (GraphNode& gn : gNodes)
      if (gn.uid == uid)
         return &gn;
   return nullptr;
}

bool ArrangeNodeAudioCompatible(const GraphNode& gn)
{
   return gn.node != nullptr && dynamic_cast<IAudioSource*>(gn.node.get()) != nullptr;
}

bool ArrangeNodeVideoCompatible(const GraphNode& gn)
{
   if (gn.node == nullptr)
      return false;
   // Turbo 0.43.1: 3D nodes are allowed (rendered like their mini viewport).
   if (dynamic_cast<TimelineNode*>(gn.node.get()) != nullptr)
      return false; // the timeline cannot contain itself
   return CanShowInViewportPanel(gn);
}

bool ArrangeNodeFitsLane(const GraphNode& gn, int laneType)
{
   return laneType == Arrange::kLaneAudio ? ArrangeNodeAudioCompatible(gn) : ArrangeNodeVideoCompatible(gn);
}

int ArrangeLaneTypeForNode(const GraphNode& gn)
{
   return ArrangeNodeVideoCompatible(gn) ? Arrange::kLaneVideo : Arrange::kLaneAudio;
}

// ---- model <-> patch (upstream's straight field copies) ------------------

void ArrangeModelToPatchData(const Arrange::Model& m, Patch::Data& data)
{
   data.streams.clear();
   data.streams.reserve(m.lanes.size());
   for (const Arrange::Lane& lane : m.lanes)
   {
      Patch::StreamRecord s;
      s.id = lane.id;
      s.type = lane.type;
      s.blendMode = 0;
      s.opacity = lane.opacity;
      s.gainDb = lane.gainDb;
      s.pan = lane.pan;
      s.enabled = lane.enabled;
      s.groupId = lane.groupId;
      s.mute = lane.mute;
      s.solo = lane.solo;
      s.name = lane.name;
      s.colorR = lane.colorR;
      s.colorG = lane.colorG;
      s.colorB = lane.colorB;
      s.rowHeight = lane.rowHeight;
      for (const Arrange::Clip& c : lane.clips)
      {
         Patch::ClipRecord r;
         r.id = c.id;
         r.startTick = c.start;
         r.lengthTick = c.length;
         r.srcUid = c.srcUid;
         r.srcOutput = c.srcOutput;
         r.fadeInTick = c.fadeIn;
         r.fadeOutTick = c.fadeOut;
         r.gainDb = c.gainDb;
         r.pan = c.pan;
         r.enabled = c.enabled;
         r.groupId = c.groupId;
         r.name = c.name;
         r.colorR = c.colorR;
         r.colorG = c.colorG;
         r.colorB = c.colorB;
         r.blendMode = c.blendMode;
         r.pitch = c.pitch;
         r.syncToTempo = c.syncToTempo;
         r.opacity = c.opacity;
         r.colorBrightness = c.colorBrightness;
         r.colorContrast = c.colorContrast;
         r.colorSaturation = c.colorSaturation;
         r.retrigger = c.retrigger;
         r.sampleDropped = c.sampleDropped;
         r.sampleBpm = c.sampleBpm;
         r.origBpm = c.origBpm;
         r.sourceDurationSeconds = c.sourceDurationSeconds;
         r.sourceOffsetSeconds = c.sourceOffsetSeconds;
         r.bypassedModParams = c.bypassedModParams;
         s.clips.push_back(std::move(r));
      }
      data.streams.push_back(std::move(s));
   }

   data.markers.clear();
   for (const Arrange::Marker& mk : m.markers)
   {
      Patch::MarkerRecord r;
      r.id = mk.id;
      r.posTick = mk.pos;
      r.color = mk.color;
      r.name = mk.name;
      data.markers.push_back(std::move(r));
   }

   data.trackGroups.clear();
   for (const Arrange::TrackGroup& g : m.trackGroups)
   {
      Patch::TrackGroupRecord r;
      r.id = g.id;
      r.color = g.color;
      r.enabled = g.enabled;
      r.collapsed = g.collapsed;
      r.parentGroupId = g.parentGroupId;
      r.name = g.name;
      data.trackGroups.push_back(std::move(r));
   }

   Patch::ArrangeSettingsRecord& a = data.arrangeSettings;
   a.nextId = m.nextId;
   a.timeDisplay = m.settings.timeDisplay;
   a.snapDivision = m.settings.snapDivision;
   a.snapTriplet = m.settings.snapTriplet;
   a.zoom = m.settings.zoom;
   a.scroll = m.settings.scroll;
   a.loopEnabled = m.settings.loop.enabled;
   a.loopStart = m.settings.loop.start;
   a.loopEnd = m.settings.loop.end;
   a.dockSide = m.settings.dockSide;
   a.renderWidth = m.settings.renderWidth;
   a.renderHeight = m.settings.renderHeight;
   a.renderFps = m.settings.renderFps;
   a.renderSampleRate = m.settings.renderSampleRate;
   a.renderFormat = m.settings.renderFormat;
   a.renderRangeKind = m.settings.renderRangeKind;
   a.renderRangeStart = m.settings.renderRangeStart;
   a.renderRangeEnd = m.settings.renderRangeEnd;
   a.renderAudioSource = m.settings.renderAudioSource;
   a.renderVideoSource = m.settings.renderVideoSource;
   a.renderFolder = m.settings.renderFolder;
   a.importSyncToTempo = m.settings.importSyncToTempo;
   data.hasArrange = true;
}

void PatchDataToArrangeModel(const Patch::Data& data, Arrange::Model& m)
{
   m = Arrange::Model();
   m.nextId = std::max<uint64_t>(1, data.arrangeSettings.nextId);
   for (const Patch::StreamRecord& s : data.streams)
   {
      Arrange::Lane lane;
      lane.id = s.id;
      lane.type = (s.type == Patch::kStreamAudio) ? Arrange::kLaneAudio : Arrange::kLaneVideo;
      lane.blendMode = 0;
      lane.opacity = s.opacity;
      lane.gainDb = s.gainDb;
      lane.pan = s.pan;
      lane.enabled = s.enabled;
      lane.groupId = s.groupId;
      lane.mute = s.mute;
      lane.solo = s.solo;
      lane.name = s.name;
      lane.colorR = s.colorR;
      lane.colorG = s.colorG;
      lane.colorB = s.colorB;
      lane.rowHeight = s.rowHeight;
      for (const Patch::ClipRecord& c : s.clips)
      {
         Arrange::Clip clip;
         clip.id = c.id;
         clip.start = c.startTick;
         clip.length = c.lengthTick;
         clip.srcUid = c.srcUid;
         clip.srcOutput = c.srcOutput;
         clip.fadeIn = c.fadeInTick;
         clip.fadeOut = c.fadeOutTick;
         clip.gainDb = c.gainDb;
         clip.enabled = c.enabled;
         clip.groupId = c.groupId;
         clip.name = c.name;
         clip.colorR = c.colorR;
         clip.colorG = c.colorG;
         clip.colorB = c.colorB;
         clip.blendMode = (c.blendMode >= 0) ? c.blendMode : s.blendMode;
         clip.pan = c.pan;
         clip.pitch = c.pitch;
         clip.syncToTempo = c.syncToTempo;
         clip.opacity = c.opacity;
         clip.colorBrightness = c.colorBrightness;
         clip.colorContrast = c.colorContrast;
         clip.colorSaturation = c.colorSaturation;
         clip.retrigger = c.retrigger;
         clip.sampleDropped = c.sampleDropped;
         clip.sampleBpm = c.sampleBpm;
         clip.origBpm = (c.origBpm > 0.0f) ? c.origBpm : 0.0f;
         clip.sourceDurationSeconds = c.sourceDurationSeconds;
         clip.sourceOffsetSeconds = c.sourceOffsetSeconds;
         clip.bypassedModParams = c.bypassedModParams;
         lane.clips.push_back(std::move(clip));
      }
      m.lanes.push_back(std::move(lane));
   }
   for (const Patch::MarkerRecord& r : data.markers)
   {
      Arrange::Marker mk;
      mk.id = r.id;
      mk.pos = r.posTick;
      mk.color = r.color;
      mk.name = r.name;
      m.markers.push_back(std::move(mk));
   }
   for (const Patch::TrackGroupRecord& r : data.trackGroups)
   {
      Arrange::TrackGroup g;
      g.id = r.id;
      g.color = r.color;
      g.enabled = r.enabled;
      g.collapsed = r.collapsed;
      g.parentGroupId = r.parentGroupId;
      g.name = r.name;
      m.trackGroups.push_back(std::move(g));
   }
   const Patch::ArrangeSettingsRecord& a = data.arrangeSettings;
   m.settings.timeDisplay = a.timeDisplay;
   m.settings.snapDivision = a.snapDivision;
   m.settings.snapTriplet = a.snapTriplet;
   m.settings.zoom = a.zoom;
   m.settings.scroll = a.scroll;
   m.settings.loop.enabled = a.loopEnabled;
   m.settings.loop.start = a.loopStart;
   m.settings.loop.end = a.loopEnd;
   m.settings.dockSide = a.dockSide;
   m.settings.renderWidth = a.renderWidth;
   m.settings.renderHeight = a.renderHeight;
   m.settings.renderFps = a.renderFps;
   m.settings.renderSampleRate = a.renderSampleRate;
   m.settings.renderFormat = a.renderFormat;
   m.settings.renderRangeKind = a.renderRangeKind;
   m.settings.renderRangeStart = a.renderRangeStart;
   m.settings.renderRangeEnd = a.renderRangeEnd;
   m.settings.renderAudioSource = a.renderAudioSource;
   m.settings.renderVideoSource = a.renderVideoSource;
   m.settings.renderFolder = a.renderFolder;
   m.settings.importSyncToTempo = a.importSyncToTempo;
   Arrange::Normalize(m);
}

// ---- session state ---------------------------------------------------------

std::vector<uint64_t> gArrangeSel;      // selected clip ids
uint64_t gArrangeSelLane = 0;           // last clicked lane (paste target)
uint64_t gArrangeSettingsClip = 0;      // clip shown in the Clip settings window
bool gArrangeSettingsOpen = false;
bool gArrangeBlade = false;
bool gArrangeFollow = true;
bool gArrangeMonitor = true;
bool gArrangeRenderWindowOpen = false;
uint64_t gArrangeAudioSig = 0;
struct ArrangeClipboardEntry { int laneOffset = 0; Arrange::Clip clip; };
std::vector<ArrangeClipboardEntry> gArrangeClipboard;
Arrange::Tick gArrangeClipboardStart = 0;

bool gArrangeGestureOpen = false;
Arrange::Model gArrangeGestureBefore;

void PublishArrangeLoop()
{
   const Arrange::LoopRange& loop = gArrange.settings.loop;
   Transport::Instance().SetLoop(loop.enabled, Arrange::TicksToBeats(loop.start), Arrange::TicksToBeats(loop.end));
}

void ArrangePruneSelection()
{
   gArrangeSel.erase(std::remove_if(gArrangeSel.begin(), gArrangeSel.end(),
                                    [](uint64_t id) { return Arrange::FindClip(gArrange, id) == nullptr; }),
                     gArrangeSel.end());
   if (gArrangeSettingsClip != 0 && Arrange::FindClip(gArrange, gArrangeSettingsClip) == nullptr)
   {
      gArrangeSettingsClip = 0;
      gArrangeSettingsOpen = false;
   }
}

void ArrangeResetSession()
{
   gArrange = Arrange::Model();
   gArrangeSel.clear();
   gArrangeSettingsClip = 0;
   gArrangeSettingsOpen = false;
   gArrangeTimelineMode = false;
   gArrangeGestureOpen = false;
   PublishArrangeLoop();
}

void ArrangeLoadFromPatchData(const Patch::Data& data, bool fileLoad)
{
   const Arrange::Settings keep = gArrange.settings;
   const uint64_t prevRevision = gArrange.revision;
   Arrange::Model m;
   PatchDataToArrangeModel(data, m);
   if (!fileLoad)
   {
      // Undo/redo rewinds the document, not the view: dock, display unit,
      // snap, zoom and scroll stay where the user has them (upstream rule).
      m.settings.dockSide = keep.dockSide;
      m.settings.timeDisplay = keep.timeDisplay;
      m.settings.snapDivision = keep.snapDivision;
      m.settings.snapTriplet = keep.snapTriplet;
      m.settings.zoom = keep.zoom;
      m.settings.scroll = keep.scroll;
   }
   m.revision = prevRevision + 1;
   gArrange = std::move(m);
   gArrangeGestureOpen = false;
   if (fileLoad)
   {
      gArrangeSel.clear();
      gArrangeTimelineMode = false;
      if (!gArrange.lanes.empty())
         gArrangePanelOpen = true;
   }
   ArrangePruneSelection();
   PublishArrangeLoop();
}

void ArrangeOnNodeRemoved(uint64_t uid)
{
   // The clip stays, offline (upstream rule): undo brings the node back.
   Arrange::ClearSource(gArrange, uid);
}

// ---- edits and undo --------------------------------------------------------

// This fork's undo is whole-patch snapshots; an arrangement edit pushes the
// patch as it was before the edit (the model swapped back in for the
// snapshot, then forward again).
void PushArrangeUndoSnapshot(const Arrange::Model& before)
{
   Arrange::Model now = std::move(gArrange);
   gArrange = before;
   PushUndoCheckpoint();
   gArrange = std::move(now);
   gPatchDirty = true;
}

template <class Op>
bool ArrangeEdit(Op&& op)
{
   Arrange::Model before = gArrange;
   op();
   if (gArrange.revision == before.revision)
      return false;
   PushArrangeUndoSnapshot(before);
   ArrangePruneSelection();
   return true;
}

bool ArrangeContentEqual(const Arrange::Model& a, const Arrange::Model& b)
{
   if (a.lanes.size() != b.lanes.size() || a.markers.size() != b.markers.size() ||
       a.trackGroups.size() != b.trackGroups.size())
      return false;
   for (size_t i = 0; i < a.lanes.size(); i++)
   {
      const Arrange::Lane& la = a.lanes[i];
      const Arrange::Lane& lb = b.lanes[i];
      if (la.id != lb.id || la.type != lb.type || la.opacity != lb.opacity || la.gainDb != lb.gainDb ||
          la.pan != lb.pan || la.enabled != lb.enabled || la.groupId != lb.groupId || la.mute != lb.mute ||
          la.solo != lb.solo || la.rowHeight != lb.rowHeight || la.name != lb.name ||
          la.colorR != lb.colorR || la.colorG != lb.colorG || la.colorB != lb.colorB ||
          la.clips.size() != lb.clips.size())
         return false;
      for (size_t k = 0; k < la.clips.size(); k++)
      {
         const Arrange::Clip& ca = la.clips[k];
         const Arrange::Clip& cb = lb.clips[k];
         if (ca.id != cb.id || ca.start != cb.start || ca.length != cb.length || ca.srcUid != cb.srcUid ||
             ca.srcOutput != cb.srcOutput || ca.fadeIn != cb.fadeIn || ca.fadeOut != cb.fadeOut ||
             ca.gainDb != cb.gainDb || ca.enabled != cb.enabled || ca.groupId != cb.groupId || ca.name != cb.name ||
             ca.colorR != cb.colorR || ca.colorG != cb.colorG || ca.colorB != cb.colorB ||
             ca.blendMode != cb.blendMode || ca.pan != cb.pan || ca.pitch != cb.pitch ||
             ca.opacity != cb.opacity || ca.colorBrightness != cb.colorBrightness ||
             ca.colorContrast != cb.colorContrast || ca.colorSaturation != cb.colorSaturation ||
             ca.retrigger != cb.retrigger || ca.sampleDropped != cb.sampleDropped ||
             ca.syncToTempo != cb.syncToTempo || ca.sampleBpm != cb.sampleBpm ||
             ca.sourceDurationSeconds != cb.sourceDurationSeconds ||
             ca.sourceOffsetSeconds != cb.sourceOffsetSeconds)
            return false;
      }
   }
   for (size_t i = 0; i < a.markers.size(); i++)
      if (a.markers[i].id != b.markers[i].id || a.markers[i].pos != b.markers[i].pos ||
          a.markers[i].name != b.markers[i].name || a.markers[i].color != b.markers[i].color)
         return false;
   return a.settings.loop.enabled == b.settings.loop.enabled && a.settings.loop.start == b.settings.loop.start &&
          a.settings.loop.end == b.settings.loop.end;
}

bool ArrangeGestureEnd()
{
   if (!gArrangeGestureOpen)
      return false;
   gArrangeGestureOpen = false;
   if (gArrangeGestureBefore.revision == gArrange.revision || ArrangeContentEqual(gArrangeGestureBefore, gArrange))
      return false;
   PushArrangeUndoSnapshot(gArrangeGestureBefore);
   ArrangePruneSelection();
   return true;
}

void ArrangeGestureBegin()
{
   if (gArrangeGestureOpen)
      ArrangeGestureEnd();
   gArrangeGestureBefore = gArrange;
   gArrangeGestureOpen = true;
}

// Popup / settings widgets: snapshot on activation, push on deactivation.
void ArrangeTrackGesture()
{
   if (ImGui::IsItemActivated())
      ArrangeGestureBegin();
   if (ImGui::IsItemDeactivated())
      ArrangeGestureEnd();
}

void ArrangeSetLoop(bool enabled, Arrange::Tick start, Arrange::Tick end)
{
   start = std::max<Arrange::Tick>(0, start);
   end = std::max(end, start);
   Arrange::LoopRange& loop = gArrange.settings.loop;
   if (loop.enabled == enabled && loop.start == start && loop.end == end)
      return;
   loop.enabled = enabled;
   loop.start = start;
   loop.end = end;
   gArrange.revision++;
   gPatchDirty = true;
   PublishArrangeLoop();
}

Arrange::Tick ArrangeSnapGridTicks()
{
   return Arrange::SnapGridTicks(gArrange.settings.snapDivision, gArrange.settings.snapTriplet,
                                 std::max(1.0, Transport::Instance().BeatsPerBar()));
}

Arrange::Tick ArrangeNudgeStepTicks()
{
   const Arrange::Tick g = ArrangeSnapGridTicks();
   return g > 0 ? g : Arrange::kPPQ / 4;
}

Arrange::Tick ArrangePlayTick()
{
   return std::clamp<Arrange::Tick>(Arrange::BeatsToTicks(Transport::Instance().Beats()), 0, Arrange::kMaxTick);
}

void ArrangeSeekTick(Arrange::Tick t)
{
   Transport::Instance().SeekBeats(Arrange::TicksToBeats(std::clamp<Arrange::Tick>(t, 0, Arrange::kMaxTick)));
}

Arrange::Tick ArrangeTicksPerBar()
{
   return Arrange::BeatsToTicks(std::max(0.25, Transport::Instance().BeatsPerBar()));
}

// ---- formatting ------------------------------------------------------------

std::string ArrangeFormatPos(Arrange::Tick t, bool forceTime = false)
{
   char buf[48];
   if (gArrange.settings.timeDisplay == 1 || forceTime)
   {
      const double sec = Arrange::TicksToSeconds(t, std::max(1.0f, Transport::Instance().Tempo()));
      const int m = (int)(sec / 60.0);
      snprintf(buf, sizeof(buf), "%d:%06.3f", m, sec - 60.0 * m);
   }
   else
   {
      const double bpb = std::max(0.25, Transport::Instance().BeatsPerBar());
      const double beats = Arrange::TicksToBeats(t);
      const int bar = (int)std::floor(beats / bpb + 1e-9);
      const double inBar = beats - bar * bpb;
      const int beat = (int)std::floor(inBar + 1e-9);
      const int six = (int)std::floor((inBar - beat) * 4.0 + 1e-9);
      snprintf(buf, sizeof(buf), "%d.%d.%d", bar + 1, beat + 1, six + 1);
   }
   return buf;
}

std::string ArrangeFormatLen(Arrange::Tick t)
{
   char buf[48];
   if (gArrange.settings.timeDisplay == 1)
   {
      snprintf(buf, sizeof(buf), "%.2f s", Arrange::TicksToSeconds(t, std::max(1.0f, Transport::Instance().Tempo())));
      return buf;
   }
   const double bpb = std::max(0.25, Transport::Instance().BeatsPerBar());
   const double beats = Arrange::TicksToBeats(t);
   const int bars = (int)std::floor(beats / bpb + 1e-9);
   const double rest = beats - bars * bpb;
   if (bars > 0 && rest < 1e-6)
      snprintf(buf, sizeof(buf), "%d bar%s", bars, bars == 1 ? "" : "s");
   else if (bars > 0)
      snprintf(buf, sizeof(buf), "%d bar%s + %.2f beats", bars, bars == 1 ? "" : "s", rest);
   else
      snprintf(buf, sizeof(buf), "%.2f beats", rest);
   return buf;
}

std::string ArrangeLaneLabel(const Arrange::Model& m, int laneIndex)
{
   const Arrange::Lane& lane = m.lanes[(size_t)laneIndex];
   if (!lane.name.empty())
      return lane.name;
   int n = 0;
   for (int i = 0; i <= laneIndex; i++)
      if (m.lanes[(size_t)i].type == lane.type)
         n++;
   return std::string(lane.type == Arrange::kLaneAudio ? "A" : "V") + std::to_string(n);
}

std::string ArrangeClipLabel(const Arrange::Clip& c)
{
   if (!c.name.empty())
      return c.name;
   if (c.srcUid == 0)
      return "(no source)";
   GraphNode* gn = FindNodeByUid(c.srcUid);
   if (gn == nullptr)
      return "(offline)";
   return NodeTitleWithInstance(*gn);
}

// ---- colours ---------------------------------------------------------------

struct ArrangePaletteEntry { const char* name; ImU32 col; };
const ArrangePaletteEntry kArrangePalette[10] = {
   { "Default", IM_COL32(110, 120, 140, 255) }, { "Crimson", IM_COL32(239, 68, 68, 255) },
   { "Orange", IM_COL32(249, 115, 22, 255) },   { "Amber", IM_COL32(245, 158, 11, 255) },
   { "Emerald", IM_COL32(16, 185, 129, 255) },  { "Cyan", IM_COL32(6, 182, 212, 255) },
   { "Blue", IM_COL32(59, 130, 246, 255) },     { "Purple", IM_COL32(139, 92, 246, 255) },
   { "Magenta", IM_COL32(217, 70, 239, 255) },  { "Rose", IM_COL32(244, 63, 94, 255) }
};
constexpr uint32_t kArrangeDefaultMarkerRGBA = 0xF59E0BFFu; // amber

ImU32 ArrangeMarkerColU32(uint32_t rgba)
{
   return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}
uint32_t ArrangeMarkerRGBA(ImU32 col)
{
   const uint32_t r = (col >> IM_COL32_R_SHIFT) & 0xFF;
   const uint32_t g = (col >> IM_COL32_G_SHIFT) & 0xFF;
   const uint32_t b = (col >> IM_COL32_B_SHIFT) & 0xFF;
   return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

ImU32 ArrangeLaneAccent(const Arrange::Lane& lane)
{
   if (lane.colorR + lane.colorG + lane.colorB > 0.0f)
      return ImGui::ColorConvertFloat4ToU32(ImVec4(lane.colorR, lane.colorG, lane.colorB, 1.0f));
   return lane.type == Arrange::kLaneAudio ? IM_COL32(70, 180, 120, 255) : IM_COL32(80, 140, 230, 255);
}

ImU32 ArrangeClipColor(const Arrange::Lane& lane, const Arrange::Clip& c)
{
   if (c.colorR + c.colorG + c.colorB > 0.0f)
      return ImGui::ColorConvertFloat4ToU32(ImVec4(c.colorR, c.colorG, c.colorB, 1.0f));
   return ArrangeLaneAccent(lane);
}

ImU32 ArrangeScaleCol(ImU32 col, float k, int alpha = 255)
{
   ImVec4 v = ImGui::ColorConvertU32ToFloat4(col);
   v.x = std::clamp(v.x * k, 0.0f, 1.0f);
   v.y = std::clamp(v.y * k, 0.0f, 1.0f);
   v.z = std::clamp(v.z * k, 0.0f, 1.0f);
   v.w = alpha / 255.0f;
   return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 ArrangeGroupColor(uint64_t groupId)
{
   uint64_t h = groupId * 0x9E3779B97F4A7C15ull;
   h ^= h >> 29;
   const float hue = (float)(h % 360) / 360.0f;
   float r, g, b;
   ImGui::ColorConvertHSVtoRGB(hue, 0.65f, 0.95f, r, g, b);
   return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
}

// ---- audio routing ---------------------------------------------------------

// Hash of what a clip's waveform depends on (upstream's ArrangeClipShape): a
// cached waveform measured under another shape is discarded.
uint64_t ArrangeClipShape(const Arrange::Clip& c)
{
   uint64_t h = 1469598103934665603ull;
   auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
   mix(c.srcUid);
   mix((uint64_t)c.srcOutput);
   mix((uint64_t)c.start);
   mix((uint64_t)c.length);
   uint32_t f;
   std::memcpy(&f, &c.sourceOffsetSeconds, 4); mix(f);
   std::memcpy(&f, &c.sampleBpm, 4); mix(f);
   std::memcpy(&f, &c.pitch, 4); mix(f);
   mix(c.syncToTempo ? 1 : 0);
   return h;
}

bool ArrangeClipIsAudioSample(const Arrange::Clip& c)
{
   if (!c.sampleDropped)
      return false;
   GraphNode* gn = FindNodeByUid(c.srcUid);
   return gn != nullptr && dynamic_cast<AudioFileNode*>(gn->node.get()) != nullptr;
}

// The audio node an audio clip plays (null when offline / not audio).
AudioNode* ArrangeClipAudioNode(const Arrange::Clip& c)
{
   GraphNode* gn = FindNodeByUid(c.srcUid);
   if (gn == nullptr || !ArrangeNodeAudioCompatible(*gn))
      return nullptr;
   return AudioNodeOfAny(gn->node.get());
}

bool ArrangeLaneAudible(const Arrange::Lane& lane)
{
   return lane.type == Arrange::kLaneAudio && Arrange::LaneEffectivelyEnabled(gArrange, lane);
}

void ArrangeSeedAudio(std::set<INode*>& visited, std::vector<AudioTopologyEntry>& order,
                      std::unordered_map<AudioNode*, int>& bufferIndexOf)
{
   if (!gArrangeTimelineMode)
      return;
   for (const Arrange::Lane& lane : gArrange.lanes)
   {
      if (!ArrangeLaneAudible(lane))
         continue;
      for (const Arrange::Clip& c : lane.clips)
      {
         if (!c.enabled)
            continue;
         GraphNode* gn = FindNodeByUid(c.srcUid);
         if (gn != nullptr && ArrangeNodeAudioCompatible(*gn))
            CollectAudioChain(gn->node.get(), visited, order, bufferIndexOf);
      }
   }
}

void ArrangeBuildAudioTerminals(const std::unordered_map<AudioNode*, int>& bufferIndexOf, AudioTopology& topology)
{
   topology.arrangeWindows.clear();
   topology.arrangeTerminals.clear();
   if (!gArrangeTimelineMode)
      return;
   const int laneCount = std::min((int)gArrange.lanes.size(), AudioEngine::kMaxArrangeLanes);
   for (int li = 0; li < laneCount; li++)
   {
      const Arrange::Lane& lane = gArrange.lanes[(size_t)li];
      if (!ArrangeLaneAudible(lane))
         continue;
      // One terminal per distinct source node on this lane.
      std::vector<AudioNode*> sources;
      for (const Arrange::Clip& c : lane.clips)
         if (c.enabled)
            if (AudioNode* an = ArrangeClipAudioNode(c))
               if (std::find(sources.begin(), sources.end(), an) == sources.end())
                  sources.push_back(an);
      for (AudioNode* an : sources)
      {
         auto it = bufferIndexOf.find(an);
         if (it == bufferIndexOf.end())
            continue;
         ArrangeTerminal t;
         t.bufferIndex = it->second;
         t.source = an;
         t.laneSlot = li;
         t.windowOffset = (int)topology.arrangeWindows.size();
         const Arrange::Clip* prev = nullptr;
         for (const Arrange::Clip& c : lane.clips)
         {
            if (!c.enabled || ArrangeClipAudioNode(c) != an)
               continue;
            ArrangeClipWindow w;
            w.startBeat = Arrange::TicksToBeats(c.start);
            w.endBeat = Arrange::TicksToBeats(c.End());
            w.fadeInBeats = Arrange::TicksToBeats(c.fadeIn);
            w.fadeOutBeats = Arrange::TicksToBeats(c.fadeOut);
            w.gain = DspMath::DbToLinear(c.gainDb);
            float l = 1.0f, r = 1.0f;
            if (c.pan != 0.0f)
            {
               DspMath::EqualPowerPan(c.pan, l, r);
               l *= 1.41421356f;
               r *= 1.41421356f;
            }
            w.panL = l;
            w.panR = r;
            w.seekSource = ArrangeClipIsAudioSample(c);
            w.sourceOffsetSeconds = c.sourceOffsetSeconds;
            w.syncToTempo = c.syncToTempo;
            w.sampleBpm = c.sampleBpm;
            w.pitch = c.pitch;
            w.clipId = c.id;
            w.shape = ArrangeClipShape(c);
            if (prev != nullptr && prev->End() == c.start)
            {
               w.abutsPrev = true;
               topology.arrangeWindows.back().abutsNext = true;
            }
            topology.arrangeWindows.push_back(w);
            prev = &c;
         }
         t.numWindows = (int)topology.arrangeWindows.size() - t.windowOffset;
         if (t.numWindows > 0)
            topology.arrangeTerminals.push_back(t);
      }
   }
}

// What the audio schedule depends on, so a rebuild happens exactly when it
// changes (not on a view change or a fader move).
uint64_t ArrangeAudioSignature()
{
   uint64_t h = 1469598103934665603ull;
   auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
   auto mixd = [&mix](double d) { uint64_t v; std::memcpy(&v, &d, sizeof(v)); mix(v); };
   mix(gArrangeTimelineMode ? 1 : 0);
   if (!gArrangeTimelineMode)
      return h;
   for (const Arrange::Lane& lane : gArrange.lanes)
   {
      mix(lane.id);
      mix(ArrangeLaneAudible(lane) ? 1 : 0);
      if (lane.type != Arrange::kLaneAudio)
         continue;
      for (const Arrange::Clip& c : lane.clips)
      {
         mix(c.id);
         mix((uint64_t)c.start);
         mix((uint64_t)c.length);
         mix(c.srcUid);
         mix((uint64_t)(uintptr_t)ArrangeClipAudioNode(c));
         mix((uint64_t)c.fadeIn);
         mix((uint64_t)c.fadeOut);
         mixd(c.gainDb);
         mixd(c.pan);
         mix(c.enabled ? 1 : 0);
         mix(c.sampleDropped ? 1 : 0);
         mixd(c.sourceOffsetSeconds);
         mix(ArrangeClipShape(c));
      }
   }
   return h;
}

// Once per frame, before the cook: faders to the engine, schedule rebuild
// when the arrangement changed, Video Sample seeks.
std::map<uint64_t, uint64_t> gArrangeVideoActiveClip; // node uid -> clip id under the playhead
uint32_t gArrangeVideoSeekSerial = 0;

void ArrangeDrainPeaks();
void ArrangeCollectModBypass();
void ArrangeFrameUpdate()
{
   ArrangeCollectModBypass();
   ArrangeDrainPeaks();
   AudioEngine& engine = AudioEngine::Instance();
   engine.SetTimelineMode(gArrangeTimelineMode);
   Arrange::gSampleLiveTempoBpm = std::max(1.0f, Transport::Instance().Tempo());

   bool anySolo = false;
   for (const Arrange::Lane& lane : gArrange.lanes)
      if (lane.type == Arrange::kLaneAudio && lane.solo)
         anySolo = true;
   const int laneCount = std::min((int)gArrange.lanes.size(), AudioEngine::kMaxArrangeLanes);
   for (int li = 0; li < laneCount; li++)
   {
      const Arrange::Lane& lane = gArrange.lanes[(size_t)li];
      if (lane.type != Arrange::kLaneAudio)
         continue;
      const bool silent = lane.mute || (anySolo && !lane.solo);
      float l = 1.0f, r = 1.0f;
      if (lane.pan != 0.0f)
      {
         DspMath::EqualPowerPan(lane.pan, l, r);
         l *= 1.41421356f;
         r *= 1.41421356f;
      }
      engine.SetArrangeLaneMix(li, silent ? 0.0f : DspMath::DbToLinear(lane.gainDb), l, r);
   }

   const uint64_t sig = ArrangeAudioSignature();
   if (sig != gArrangeAudioSig && !gDeferAudioRebuild)
   {
      gArrangeAudioSig = sig;
      RebuildAudioTopology();
   }

   // Video Samples (a video file dropped on the timeline) are locked to the
   // timeline: entering the clip, a seek, or drifting more than 0.3 s puts
   // the video where the playhead says.
   const double beat = Transport::Instance().Beats();
   const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
   const uint32_t serial = Transport::Instance().SeekSerial();
   const bool jumped = serial != gArrangeVideoSeekSerial;
   gArrangeVideoSeekSerial = serial;
   std::map<uint64_t, uint64_t> active;
   for (const Arrange::Lane& lane : gArrange.lanes)
   {
      if (!Arrange::LaneEffectivelyEnabled(gArrange, lane))
         continue;
      for (const Arrange::Clip& c : lane.clips)
      {
         const double s = Arrange::TicksToBeats(c.start);
         if (s > beat)
            break;
         // Turbo 0.43.1: every Video clip is locked to the timeline (a jump
         // moves the picture with the sound), unless the clip is set to
         // run free (retrigger off).
         if (!(beat < Arrange::TicksToBeats(c.End())) || !c.enabled || !c.retrigger)
            continue;
         GraphNode* gn = FindNodeByUid(c.srcUid);
         auto* video = gn != nullptr ? dynamic_cast<VideoSourceNode*>(gn->node.get()) : nullptr;
         if (video == nullptr || video->Duration() <= 0.0 || active.count(c.srcUid))
            continue;
         active[c.srcUid] = c.id;
         double want = c.sourceOffsetSeconds + (beat - s) * 60.0 / bpm;
         // A clip longer than the movie: loop inside the trimmed range if
         // the node loops, else hold the last frame.
         const double t0 = (double)video->trimStart * video->Duration();
         const double span = std::max(1e-3, ((double)video->trimEnd - (double)video->trimStart) * video->Duration());
         want = video->loop ? std::fmod(want, span) : std::min(want, span - 1e-3);
         want += t0;
         const auto prevIt = gArrangeVideoActiveClip.find(c.srcUid);
         const bool entered = prevIt == gArrangeVideoActiveClip.end() || prevIt->second != c.id;
         if (entered || jumped || std::fabs(video->Position() - want) > 0.3)
            video->SeekNormalized((float)std::clamp(want / video->Duration(), 0.0, 1.0));
      }
   }
   gArrangeVideoActiveClip = std::move(active);
}

// ---- waveforms ----------------------------------------------------------------

float ArrangePxPerBeat();
double ArrangeSampleEffBpm(const Arrange::Clip& c);

// Live: what each audio clip actually played, peak per 1/16 beat, measured
// by the audio thread (upstream's approach - works for synths, plugins,
// anything). Static: for file-backed clips the whole file is analysed once
// in the background, so the waveform is there before the first play.
struct ArrangeLiveWave
{
   uint64_t shape = 0;
   std::vector<float> peaks; // -1 = not measured yet
};
std::map<uint64_t, ArrangeLiveWave> gArrangeLiveWaves;

void ArrangeDrainPeaks()
{
   AudioEngine::Instance().DrainArrangePeaks([](const ArrangePeak& p)
   {
      const Arrange::Clip* c = Arrange::FindClip(gArrange, p.clipId);
      if (c == nullptr || p.bucket < 0)
         return;
      const uint64_t shape = ArrangeClipShape(*c);
      if (p.shape != shape)
         return; // measured under an older shape (the audio thread lags an edit)
      ArrangeLiveWave& w = gArrangeLiveWaves[p.clipId];
      const size_t n = (size_t)std::max<int64_t>(1, (int64_t)std::ceil(Arrange::TicksToBeats(c->length) * 4.0));
      if (w.shape != shape || w.peaks.size() != n)
      {
         w.shape = shape;
         w.peaks.assign(n, -1.0f);
      }
      if ((size_t)p.bucket < n)
         w.peaks[(size_t)p.bucket] = p.peak;
   });
   static int sPrune = 0;
   if (++sPrune % 300 == 0)
      for (auto it = gArrangeLiveWaves.begin(); it != gArrangeLiveWaves.end();)
         it = Arrange::FindClip(gArrange, it->first) == nullptr ? gArrangeLiveWaves.erase(it) : std::next(it);
}

struct ArrangeFileWave
{
   std::atomic<bool> ready { false };
   double bucketSeconds = 0.01;
   std::vector<float> minV, maxV;
};
std::map<std::string, std::shared_ptr<ArrangeFileWave>> gArrangeFileWaves;

// The file's min/max per 10 ms, decoded on a worker thread the first time a
// clip of that file is drawn. nullptr until ready.
const ArrangeFileWave* ArrangeFileWaveFor(const std::string& path)
{
   if (path.empty())
      return nullptr;
   auto it = gArrangeFileWaves.find(path);
   if (it == gArrangeFileWaves.end())
   {
      auto wave = std::make_shared<ArrangeFileWave>();
      gArrangeFileWaves[path] = wave;
      std::thread([wave, path]()
      {
         Platform::SampleBuffer buf;
         std::string err;
         if (Platform::DecodeAudioFileToBuffer(path, buf, err) && buf.numFrames > 0 && buf.channels > 0)
         {
            const double sr = buf.sampleRate > 0.0 ? buf.sampleRate : 48000.0;
            const int per = std::max(1, (int)(sr * wave->bucketSeconds));
            const int buckets = (buf.numFrames + per - 1) / per;
            wave->minV.assign((size_t)buckets, 0.0f);
            wave->maxV.assign((size_t)buckets, 0.0f);
            for (int ch = 0; ch < std::min(2, buf.channels); ch++)
            {
               const float* d = buf.channelData.data() + (size_t)ch * (size_t)buf.numFrames;
               for (int i = 0; i < buf.numFrames; i++)
               {
                  const size_t b = (size_t)(i / per);
                  wave->minV[b] = std::min(wave->minV[b], d[i]);
                  wave->maxV[b] = std::max(wave->maxV[b], d[i]);
               }
            }
            wave->bucketSeconds = (double)per / sr;
         }
         wave->ready.store(true, std::memory_order_release);
      }).detach();
      return nullptr;
   }
   return it->second->ready.load(std::memory_order_acquire) ? it->second.get() : nullptr;
}

// The file a clip plays in step with the timeline (Audio Sample, or a
// locked Video clip's soundtrack), else "".
std::string ArrangeClipFilePath(const Arrange::Clip& c)
{
   GraphNode* gn = FindNodeByUid(c.srcUid);
   if (gn == nullptr)
      return std::string();
   if (auto* af = dynamic_cast<AudioFileNode*>(gn->node.get()))
      return c.sampleDropped ? af->FilePath() : std::string();
   if (auto* vid = dynamic_cast<VideoSourceNode*>(gn->node.get()))
      return (c.retrigger && vid->HasAudio()) ? vid->LoadedPath() : std::string();
   return std::string();
}

void DrawArrangeClipWave(ImDrawList* dl, const Arrange::Clip& c, float x0, float x1, float y0, float y1,
                         float clipX0, float laneX0, float laneX1, ImU32 col)
{
   const float mid = 0.5f * (y0 + y1);
   const float half = 0.5f * (y1 - y0) * 0.92f;
   const float vx0 = std::max(x0, laneX0), vx1 = std::min(x1, laneX1);
   if (vx1 <= vx0 || half < 2.0f)
      return;
   const float ppb = ArrangePxPerBeat();
   // Static file waveform first.
   const std::string path = ArrangeClipFilePath(c);
   if (const ArrangeFileWave* fw = ArrangeFileWaveFor(path))
   {
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      const double eff = c.sampleDropped ? ArrangeSampleEffBpm(c) : bpm;
      for (float x = vx0; x < vx1; x += 1.0f)
      {
         const double b0 = (double)(x - clipX0) / ppb, b1 = (double)(x + 1.0f - clipX0) / ppb;
         const double s0 = c.sourceOffsetSeconds + b0 * 60.0 / eff, s1 = c.sourceOffsetSeconds + b1 * 60.0 / eff;
         int i0 = (int)(s0 / fw->bucketSeconds), i1 = (int)(s1 / fw->bucketSeconds);
         if (i0 >= (int)fw->minV.size() || i1 < 0)
            continue;
         i0 = std::max(0, i0);
         i1 = std::clamp(i1, i0, (int)fw->minV.size() - 1);
         float lo = 0.0f, hi = 0.0f;
         for (int i = i0; i <= i1; i++)
         {
            lo = std::min(lo, fw->minV[(size_t)i]);
            hi = std::max(hi, fw->maxV[(size_t)i]);
         }
         const float g = DspMath::DbToLinear(c.gainDb);
         dl->AddLine(ImVec2(x, mid - std::min(1.0f, hi * g) * half), ImVec2(x, mid - std::max(-1.0f, lo * g) * half + 1.0f), col);
      }
      return;
   }
   // Live peaks.
   auto it = gArrangeLiveWaves.find(c.id);
   if (it == gArrangeLiveWaves.end() || it->second.shape != ArrangeClipShape(c))
      return;
   const std::vector<float>& pk = it->second.peaks;
   const float bucketPx = ppb * 0.25f;
   for (size_t b = 0; b < pk.size(); b++)
   {
      if (pk[b] < 0.0f)
         continue;
      const float bx0 = clipX0 + bucketPx * (float)b, bx1 = bx0 + std::max(1.0f, bucketPx - (bucketPx > 3.0f ? 1.0f : 0.0f));
      if (bx1 < vx0 || bx0 > vx1)
         continue;
      const float a = std::min(1.0f, pk[b]) * half;
      dl->AddRectFilled(ImVec2(std::max(bx0, vx0), mid - a), ImVec2(std::min(bx1, vx1), mid + a + 1.0f), col);
   }
}

// ---- video compositing -----------------------------------------------------

struct ArrangeVideoLayer
{
   GraphNode* gn = nullptr;
   uint64_t clipId = 0;
   float clipPos01 = 0.0f; // where in the clip the playhead is
   int blendMode = 0;
   float opacity = 1.0f;
   float brightness = 0.0f, contrast = 0.0f, saturation = 1.0f;
};

// Bottom lane first, so the lane drawn at the top of the panel is in front
// (the NLE convention, upstream spec).
void CollectArrangeVideoLayers(double beat, std::vector<ArrangeVideoLayer>& out)
{
   out.clear();
   for (size_t li = gArrange.lanes.size(); li-- > 0;)
   {
      const Arrange::Lane& lane = gArrange.lanes[li];
      if (lane.type != Arrange::kLaneVideo || !Arrange::LaneEffectivelyEnabled(gArrange, lane))
         continue;
      for (const Arrange::Clip& c : lane.clips)
      {
         const double startBeat = Arrange::TicksToBeats(c.start);
         if (startBeat > beat)
            break;
         const double endBeat = Arrange::TicksToBeats(c.End());
         if (!(beat < endBeat))
            continue;
         if (c.enabled && c.srcUid != 0)
         {
            GraphNode* gn = FindNodeByUid(c.srcUid);
            if (gn != nullptr && gn->node != nullptr && !gn->node->bypassed)
            {
               // Turbo: fades shape video too (opacity), so clips crossfade.
               float fade = 1.0f;
               const double in = beat - startBeat, outB = endBeat - beat;
               const double fi = Arrange::TicksToBeats(c.fadeIn), fo = Arrange::TicksToBeats(c.fadeOut);
               if (fi > 0.0 && in < fi)
                  fade *= (float)(in / fi);
               if (fo > 0.0 && outB < fo)
                  fade *= (float)(outB / fo);
               ArrangeVideoLayer layer;
               layer.gn = gn;
               layer.clipId = c.id;
               layer.clipPos01 = (float)std::clamp(in / std::max(1e-9, endBeat - startBeat), 0.0, 1.0);
               layer.blendMode = std::clamp(c.blendMode, 0, 31);
               layer.opacity = std::clamp(lane.opacity * c.opacity * fade, 0.0f, 1.0f);
               layer.brightness = c.colorBrightness;
               layer.contrast = c.colorContrast;
               layer.saturation = c.colorSaturation;
               out.push_back(layer);
            }
         }
         break; // lanes never overlap: one candidate per lane
      }
   }
}

// 3D clips: one solo viewport per (node, size).
struct ArrangeGeomView
{
   NodeViewport viewport;
   int lastUsedFrame = 0;
};
std::map<std::pair<uint64_t, int>, ArrangeGeomView> gArrangeGeomViews;

// ---- captured thumbnails (film strip) ------------------------------------------

// Each clip keeps up to kArrangeThumbSlots small frames, one per equal part
// of the clip, captured from what the compositor actually showed there.
constexpr int kArrangeThumbSlots = 6;
constexpr int kArrangeThumbW = 160, kArrangeThumbH = 90;
struct ArrangeThumbSlot
{
   GLUtil::Fbo fbo;
   double capturedAt = -1.0;
   float aspect = 16.0f / 9.0f;
};
struct ArrangeClipThumbs
{
   ArrangeThumbSlot slot[kArrangeThumbSlots];
   uint64_t shape = 0;
};
std::map<uint64_t, ArrangeClipThumbs> gArrangeThumbs;

void ArrangeCaptureThumb(uint64_t clipId, float pos01, unsigned int tex, int w, int h)
{
   if (clipId == 0 || tex == 0)
      return;
   ArrangeClipThumbs& t = gArrangeThumbs[clipId];
   const int k = std::clamp((int)(pos01 * kArrangeThumbSlots), 0, kArrangeThumbSlots - 1);
   ArrangeThumbSlot& slot = t.slot[k];
   const double now = ImGui::GetTime();
   if (slot.capturedAt >= 0.0 && now - slot.capturedAt < 1.0)
      return; // refreshed at most once a second
   if (!GLUtil::EnsureFbo(slot.fbo, kArrangeThumbW, kArrangeThumbH))
      return;
   static GLuint sReadFbo = 0;
   if (sReadFbo == 0)
      glGenFramebuffers(1, &sReadFbo);
   GLint prevRead = 0, prevDraw = 0;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
   glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, sReadFbo);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot.fbo.fbo);
   glBlitFramebuffer(0, 0, w, h, 0, 0, kArrangeThumbW, kArrangeThumbH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, sReadFbo);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
   slot.capturedAt = now;
   slot.aspect = (float)w / (float)std::max(1, h);
}

// Drops thumbnails of clips that no longer exist.
void ArrangePruneThumbs()
{
   for (auto it = gArrangeThumbs.begin(); it != gArrangeThumbs.end();)
   {
      if (Arrange::FindClip(gArrange, it->first) == nullptr)
      {
         for (ArrangeThumbSlot& sl : it->second.slot)
            GLUtil::DestroyFbo(sl.fbo);
         it = gArrangeThumbs.erase(it);
      }
      else
         ++it;
   }
}

struct ArrangeComposeProgram
{
   unsigned int program = 0;
   int uTexBase = -1, uTexTop = -1, uMode = -1, uOpacity = -1, uTopFit = -1, uGrade = -1;
};

const ArrangeComposeProgram& ArrangeComposeShader()
{
   static ArrangeComposeProgram sProg;
   static bool sTried = false;
   if (sTried)
      return sProg;
   sTried = true;
   const std::string src =
      std::string(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTexBase;\n"
         "uniform sampler2D uTexTop;\n"
         "uniform int uMode;\n"
         "uniform float uOpacity;\n"
         "uniform vec4 uTopFit;\n"
         "uniform vec3 uGrade;\n")
      + BlendModes::kBlendGLSL
      + "void main() {\n"
        "   vec2 topUv = (vUv - uTopFit.zw) / uTopFit.xy;\n"
        "   vec4 top = vec4(0.0);\n"
        "   if (topUv.x >= 0.0 && topUv.x <= 1.0 && topUv.y >= 0.0 && topUv.y <= 1.0) {\n"
        "      top = texture(uTexTop, topUv);\n"
        "      vec3 graded = (top.rgb - 0.5) * (1.0 + uGrade.y) + 0.5 + uGrade.x;\n"
        "      float luma = dot(graded, vec3(0.299, 0.587, 0.114));\n"
        "      top.rgb = clamp(mix(vec3(luma), graded, uGrade.z), 0.0, 1.0);\n"
        "   }\n"
        "   vec4 base = texture(uTexBase, vUv);\n"
        "   float as = top.a * uOpacity;\n"
        "   if (as <= 1e-6) { fragColor = base; return; }\n"
        "   if (uMode == 30) { fragColor = vec4(base.rgb, base.a * (1.0 - as)); return; }\n"
        "   if (uMode == 31) { fragColor = vec4(base.rgb, base.a * (1.0 - (1.0 - top.a) * uOpacity)); return; }\n"
        "   vec3 blended = blendMode(uMode, base.rgb, top.rgb);\n"
        "   vec3 cs = mix(top.rgb, blended, base.a);\n"
        "   float ar = as + base.a * (1.0 - as);\n"
        "   vec3 cr = (ar > 1e-5) ? (cs * as + base.rgb * base.a * (1.0 - as)) / ar : vec3(0.0);\n"
        "   fragColor = vec4(cr, ar);\n"
        "}\n";
   sProg.program = GLUtil::CompileProgram(src.c_str());
   if (sProg.program != 0)
   {
      sProg.uTexBase = glGetUniformLocation(sProg.program, "uTexBase");
      sProg.uTexTop = glGetUniformLocation(sProg.program, "uTexTop");
      sProg.uMode = glGetUniformLocation(sProg.program, "uMode");
      sProg.uOpacity = glGetUniformLocation(sProg.program, "uOpacity");
      sProg.uTopFit = glGetUniformLocation(sProg.program, "uTopFit");
      sProg.uGrade = glGetUniformLocation(sProg.program, "uGrade");
   }
   return sProg;
}

struct ArrangeCompositeTarget
{
   GLUtil::Fbo scratch[2];
   GLUtil::Fbo result;
   int frameId = -1;
   int lastUsedFrame = -1;
};
// One target per requested size (the monitor and each Timeline node size).
std::map<std::pair<int, int>, ArrangeCompositeTarget> gArrangeTargets;

unsigned int ArrangeComposite(int frameId, int targetW, int targetH)
{
   if (targetW <= 1 || targetH <= 1)
      return 0;
   ArrangeCompositeTarget& target = gArrangeTargets[{ targetW, targetH }];
   target.lastUsedFrame = frameId;
   if (target.frameId == frameId && target.result.tex != 0)
      return target.result.tex; // memoized per frame
   target.frameId = frameId;
   if (!GLUtil::EnsureFbo(target.result, targetW, targetH))
      return 0;

   static std::vector<ArrangeVideoLayer> sLayers;
   CollectArrangeVideoLayers(Transport::Instance().Beats(), sLayers);
   struct Resolved
   {
      unsigned int tex;
      int w, h;
      ArrangeVideoLayer layer;
   };
   static std::vector<Resolved> sResolved;
   sResolved.clear();
   for (const ArrangeVideoLayer& layer : sLayers)
   {
      layer.gn->node->CookIfNeeded(frameId);
      unsigned int tex = 0;
      int w = 0, h = 0;
      if (auto* geo = dynamic_cast<IGeometrySource*>(layer.gn->node.get()))
      {
         // A 3D node renders its own solo view at the target size, with the
         // node's mini-viewport camera (orbit it on the canvas).
         ArrangeGeomView& gv = gArrangeGeomViews[{ layer.gn->uid, targetW * 65536 + targetH }];
         gv.lastUsedFrame = frameId;
         tex = gv.viewport.Render(geo, gNodeCameras[layer.gn->index], targetW, targetH);
         w = targetW;
         h = targetH;
      }
      else
      {
         tex = layer.gn->node->GetOutputTexture();
         w = layer.gn->node->GetOutputWidth();
         h = layer.gn->node->GetOutputHeight();
      }
      if (tex == 0 || w <= 0 || h <= 0)
         continue;
      ArrangeCaptureThumb(layer.clipId, layer.clipPos01, tex, w, h);
      if (layer.opacity > 0.0f)
         sResolved.push_back({ tex, w, h, layer });
   }

   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   GLint prevVp[4];
   glGetIntegerv(GL_VIEWPORT, prevVp);
   auto clearToBlack = [&](const GLUtil::Fbo& f)
   {
      glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
      glViewport(0, 0, targetW, targetH);
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
   };
   auto restore = [&]()
   {
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
      glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
   };

   const ArrangeComposeProgram& prog = ArrangeComposeShader();
   const size_t n = sResolved.size();
   if (n == 0 || prog.program == 0 || !GLUtil::EnsureFbo(target.scratch[0], targetW, targetH) ||
       (n >= 2 && !GLUtil::EnsureFbo(target.scratch[1], targetW, targetH)))
   {
      clearToBlack(target.result);
      restore();
      return target.result.tex;
   }
   clearToBlack(target.scratch[0]);
   int baseIdx = 0;
   const float targetAspect = (float)targetW / (float)targetH;
   for (size_t k = 0; k < n; k++)
   {
      const Resolved& r = sResolved[k];
      const float srcAspect = (float)r.w / (float)r.h;
      float scaleX = 1.0f, scaleY = 1.0f, offX = 0.0f, offY = 0.0f;
      if (srcAspect > targetAspect)
      {
         scaleY = targetAspect / srcAspect;
         offY = (1.0f - scaleY) * 0.5f;
      }
      else
      {
         scaleX = srcAspect / targetAspect;
         offX = (1.0f - scaleX) * 0.5f;
      }
      const bool last = (k + 1 == n);
      const GLUtil::Fbo& out = last ? target.result : target.scratch[1 - baseIdx];
      const unsigned int baseTex = target.scratch[baseIdx].tex;
      GLUtil::RunShaderPass(out, prog.program, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, baseTex);
         glUniform1i(prog.uTexBase, 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, r.tex);
         glUniform1i(prog.uTexTop, 1);
         glUniform1i(prog.uMode, r.layer.blendMode);
         glUniform1f(prog.uOpacity, r.layer.opacity);
         glUniform4f(prog.uTopFit, scaleX, scaleY, offX, offY);
         glUniform3f(prog.uGrade, r.layer.brightness, r.layer.contrast, r.layer.saturation);
      });
      baseIdx = 1 - baseIdx;
   }
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, 0);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, 0);
   restore();
   return target.result.tex;
}

unsigned int ArrangeCompositeForNode(int frameId, int w, int h)
{
   return ArrangeComposite(frameId, w, h);
}

// The panel monitor's composite runs after the frame's cook (so it shows
// this frame's textures); the panel only asks for it.
bool gArrangeMonitorRequested = false;
void ArrangeCompositeMonitorIfRequested(int frameId)
{
   if (gArrangeMonitorRequested)
      ArrangeComposite(frameId, gArrange.settings.renderWidth, gArrange.settings.renderHeight);
   gArrangeMonitorRequested = false;
   for (auto it = gArrangeGeomViews.begin(); it != gArrangeGeomViews.end();)
      it = (frameId - it->second.lastUsedFrame > 600) ? gArrangeGeomViews.erase(it) : std::next(it);
   if ((frameId % 120) == 0)
      ArrangePruneThumbs();
   // Free targets nobody asked for in a while (a Timeline node resized).
   for (auto it = gArrangeTargets.begin(); it != gArrangeTargets.end();)
   {
      if (frameId - it->second.lastUsedFrame > 600)
      {
         GLUtil::DestroyFbo(it->second.scratch[0]);
         GLUtil::DestroyFbo(it->second.scratch[1]);
         GLUtil::DestroyFbo(it->second.result);
         it = gArrangeTargets.erase(it);
      }
      else
         ++it;
   }
}

unsigned int ArrangeMonitorTexture()
{
   auto it = gArrangeTargets.find({ gArrange.settings.renderWidth, gArrange.settings.renderHeight });
   return it != gArrangeTargets.end() ? it->second.result.tex : 0;
}

struct ArrangeStaticInit
{
   ArrangeStaticInit() { TimelineNode::sComposite = &ArrangeCompositeForNode; }
};
ArrangeStaticInit gArrangeStaticInit;

// ---- clip operations (keys, menus) -----------------------------------------

int ArrangeFirstLaneOfType(int type)
{
   for (int i = 0; i < (int)gArrange.lanes.size(); i++)
      if (gArrange.lanes[(size_t)i].type == type)
         return i;
   return -1;
}

uint64_t ArrangeAddTrack(int type, int atIndex = -1)
{
   uint64_t id = 0;
   ArrangeEdit([&]() { id = Arrange::AddLane(gArrange, type, atIndex); });
   gArrangePanelOpen = true;
   return id;
}

// The first one-bar gap at or after `start` on lane `lane`.
Arrange::Tick ArrangeFirstGap(int lane, Arrange::Tick start, Arrange::Tick length)
{
   for (const Arrange::Clip& lc : gArrange.lanes[(size_t)lane].clips)
   {
      if (lc.End() <= start)
         continue;
      if (lc.start >= start + length)
         break;
      start = lc.End();
   }
   return start;
}

// Node context menu "Add to Timeline": a one-bar clip of the node on the
// first lane of its type (a new one if needed), in the first free bar at or
// after the playhead.
uint64_t ArrangeAddNodeToTimeline(int nodeIndex, int laneType = -1)
{
   GraphNode* gn = FindNodeByIndex(nodeIndex);
   if (gn == nullptr || gn->node == nullptr)
      return 0;
   if (laneType < 0)
      laneType = ArrangeLaneTypeForNode(*gn);
   if (!ArrangeNodeFitsLane(*gn, laneType))
      return 0;
   const uint64_t uid = gn->uid;
   const int srcOutput = (laneType == Arrange::kLaneAudio && dynamic_cast<VideoSourceNode*>(gn->node.get())) ? 1 : 0;
   uint64_t made = 0;
   ArrangeEdit([&]()
   {
      int lane = ArrangeFirstLaneOfType(laneType);
      if (lane < 0)
         lane = Arrange::LaneIndex(gArrange, Arrange::AddLane(gArrange, laneType));
      const Arrange::Tick bar = ArrangeTicksPerBar();
      Arrange::Clip c;
      c.start = ArrangeFirstGap(lane, Arrange::GridFloor(ArrangePlayTick(), bar), bar);
      c.length = bar;
      c.srcUid = uid;
      c.srcOutput = srcOutput;
      Arrange::PlaceOverwrite(gArrange, gArrange.lanes[(size_t)lane].id, c, &made);
   });
   if (made != 0)
   {
      gArrangePanelOpen = true;
      gArrangeSel = { made };
   }
   return made;
}

bool ArrangeAssignClipSource(uint64_t clipId, uint64_t uid)
{
   const Arrange::Loc loc = Arrange::Find(gArrange, clipId);
   GraphNode* gn = FindNodeByUid(uid);
   if (!loc.Valid() || gn == nullptr)
      return false;
   const int laneType = gArrange.lanes[(size_t)loc.lane].type;
   if (!ArrangeNodeFitsLane(*gn, laneType))
      return false;
   const int out = (laneType == Arrange::kLaneAudio && dynamic_cast<VideoSourceNode*>(gn->node.get())) ? 1 : 0;
   return ArrangeEdit([&]()
   {
      Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
      if (c == nullptr || c->sampleDropped || (c->srcUid == uid && c->srcOutput == out))
         return;
      c->srcUid = uid;
      c->srcOutput = out;
      gArrange.revision++;
   });
}

std::vector<uint64_t> ArrangeSelectionIds()
{
   return Arrange::ExpandSelectionToGroups(gArrange, gArrangeSel);
}

void ArrangeDeleteSelection()
{
   const std::vector<uint64_t> ids = ArrangeSelectionIds();
   if (ids.empty())
      return;
   ArrangeEdit([&]() { Arrange::Delete(gArrange, ids); });
   gArrangeSel.clear();
}

void ArrangeDuplicateSelection()
{
   const std::vector<uint64_t> ids = ArrangeSelectionIds();
   if (ids.empty())
      return;
   std::vector<uint64_t> made;
   ArrangeEdit([&]() { Arrange::DuplicateBlock(gArrange, ids, &made); });
   if (!made.empty())
      gArrangeSel = made;
}

void ArrangeSplitSelectionAt(Arrange::Tick t)
{
   std::vector<uint64_t> ids = ArrangeSelectionIds();
   if (ids.empty())
   {
      // Nothing selected: cut every clip under the playhead.
      for (const Arrange::Lane& lane : gArrange.lanes)
         for (const Arrange::Clip& c : lane.clips)
            if (c.start < t && t < c.End())
               ids.push_back(c.id);
   }
   ArrangeEdit([&]()
   {
      for (uint64_t id : ids)
      {
         const Arrange::Clip* c = Arrange::FindClip(gArrange, id);
         if (c != nullptr && c->start < t && t < c->End())
            Arrange::Split(gArrange, id, t);
      }
   });
}

void ArrangeToggleEnabledSelection()
{
   const std::vector<uint64_t> ids = ArrangeSelectionIds();
   if (!ids.empty())
      ArrangeEdit([&]() { Arrange::SetEnabled(gArrange, ids, Arrange::kToggle); });
}

void ArrangeCopySelection()
{
   const std::vector<uint64_t> ids = ArrangeSelectionIds();
   if (ids.empty())
      return;
   gArrangeClipboard.clear();
   int minLane = INT_MAX;
   Arrange::Tick minStart = Arrange::kMaxTick;
   for (uint64_t id : ids)
   {
      const Arrange::Loc loc = Arrange::Find(gArrange, id);
      if (!loc.Valid())
         continue;
      minLane = std::min(minLane, loc.lane);
      minStart = std::min(minStart, gArrange.lanes[(size_t)loc.lane].clips[(size_t)loc.index].start);
   }
   for (uint64_t id : ids)
   {
      const Arrange::Loc loc = Arrange::Find(gArrange, id);
      if (!loc.Valid())
         continue;
      ArrangeClipboardEntry e;
      e.laneOffset = loc.lane - minLane;
      e.clip = gArrange.lanes[(size_t)loc.lane].clips[(size_t)loc.index];
      gArrangeClipboard.push_back(e);
   }
   gArrangeClipboardStart = minStart;
}

// Pastes at `at` onto `laneIndex` (+ each entry's lane offset); a clip whose
// target lane is missing or of the other type is skipped. Groups are
// re-created fresh so the copy is its own group.
void ArrangePasteAt(int laneIndex, Arrange::Tick at)
{
   if (gArrangeClipboard.empty() || laneIndex < 0)
      return;
   std::vector<uint64_t> made;
   ArrangeEdit([&]()
   {
      std::map<uint64_t, uint64_t> groupMap;
      for (const ArrangeClipboardEntry& e : gArrangeClipboard)
      {
         const int lane = laneIndex + e.laneOffset;
         if (lane < 0 || lane >= (int)gArrange.lanes.size())
            continue;
         Arrange::Lane& target = gArrange.lanes[(size_t)lane];
         Arrange::Clip c = e.clip;
         GraphNode* gn = FindNodeByUid(c.srcUid);
         if (gn != nullptr && !ArrangeNodeFitsLane(*gn, target.type))
            continue;
         c.id = 0;
         c.start = std::max<Arrange::Tick>(0, at + (e.clip.start - gArrangeClipboardStart));
         if (c.groupId != 0)
         {
            auto it = groupMap.find(c.groupId);
            if (it == groupMap.end())
               it = groupMap.emplace(c.groupId, gArrange.NewId()).first;
            c.groupId = it->second;
         }
         uint64_t id = 0;
         if (Arrange::PlaceOverwrite(gArrange, target.id, c, &id))
            made.push_back(id);
      }
      Arrange::Normalize(gArrange);
   });
   if (!made.empty())
      gArrangeSel = made;
}

void ArrangeNudgeSelection(int dir)
{
   const std::vector<uint64_t> ids = ArrangeSelectionIds();
   if (ids.empty())
      return;
   const Arrange::Tick step = ArrangeNudgeStepTicks() * dir;
   ArrangeEdit([&]() { Arrange::MoveClips(gArrange, ids, step, 0); });
}

uint64_t ArrangeAddMarkerAt(Arrange::Tick at)
{
   const Arrange::Tick g = ArrangeSnapGridTicks();
   if (g > 0)
      at = Arrange::SnapToGrid(at, g);
   uint64_t made = 0;
   ArrangeEdit([&]()
   {
      made = Arrange::AddMarker(gArrange, at, "Marker " + std::to_string(gArrange.markers.size() + 1),
                                kArrangeDefaultMarkerRGBA);
   });
   return made;
}

bool ArrangeJumpToMarker(int dir)
{
   const Arrange::Tick play = ArrangePlayTick();
   const Arrange::Marker* mk = dir < 0
      ? Arrange::PrevMarker(gArrange, play, Transport::Instance().IsPlaying() ? Arrange::kPPQ / 2 : 0)
      : Arrange::NextMarker(gArrange, play, 0);
   if (mk == nullptr)
      return false;
   ArrangeSeekTick(mk->pos);
   return true;
}

// ---- media drop (Audio / Video Sample) --------------------------------------

// A file dropped on the timeline gets its own source node (Audio File,
// Video or Image Source) and a Sample clip on the lane under the drop (or
// the first lane of its type). Audio/Video Samples are locked to the
// timeline: the file plays from the clip's start wherever the playhead
// enters it.
float gArrangeDropLaneTopY = 0.0f, gArrangeDropLaneX0 = 0.0f;
std::vector<std::pair<float, float>> gArrangeRowY; // per lane: top, bottom (screen, last frame)

int ArrangeLaneAtScreenY(float y)
{
   for (int i = 0; i < (int)gArrangeRowY.size() && i < (int)gArrange.lanes.size(); i++)
      if (y >= gArrangeRowY[(size_t)i].first && y < gArrangeRowY[(size_t)i].second)
         return i;
   return -1;
}

double ArrangeBeatAtScreenX(float x);

// Upstream's sample tempo estimate: a "120bpm" tag in the name, a length that
// is a whole number of bars, or onset spacing.
float ArrangeEstimateSampleBpm(const Platform::SampleBuffer* buf, const std::string& path, float fallbackBpm,
                               bool* detected = nullptr)
{
   if (detected) *detected = true;
   // Strategy 1: Explicit BPM tags in filename or path
   if (!path.empty())
   {
      std::string lowerPath = path;
      for (char& ch : lowerPath)
         ch = (char)std::tolower((unsigned char)ch);

      size_t pos = 0;
      while ((pos = lowerPath.find("bpm", pos)) != std::string::npos)
      {
         // Backward search for number before "bpm"
         int endDigit = (int)pos - 1;
         while (endDigit >= 0 && (lowerPath[endDigit] == ' ' || lowerPath[endDigit] == '_' || lowerPath[endDigit] == '-'))
            endDigit--;
         if (endDigit >= 0 && std::isdigit((unsigned char)lowerPath[endDigit]))
         {
            int startDigit = endDigit;
            while (startDigit > 0 && (std::isdigit((unsigned char)lowerPath[startDigit - 1]) || lowerPath[startDigit - 1] == '.'))
               startDigit--;
            std::string numStr = lowerPath.substr(startDigit, endDigit - startDigit + 1);
            try {
               float val = std::stof(numStr);
               if (val >= 40.0f && val <= 300.0f)
                  return val;
            } catch (...) {}
         }

         // Forward search for number after "bpm"
         size_t startAfter = pos + 3;
         while (startAfter < lowerPath.size() && (lowerPath[startAfter] == ' ' || lowerPath[startAfter] == '_' || lowerPath[startAfter] == '-'))
            startAfter++;
         if (startAfter < lowerPath.size() && std::isdigit((unsigned char)lowerPath[startAfter]))
         {
            size_t endAfter = startAfter;
            while (endAfter < lowerPath.size() && (std::isdigit((unsigned char)lowerPath[endAfter]) || lowerPath[endAfter] == '.'))
               endAfter++;
            std::string numStr = lowerPath.substr(startAfter, endAfter - startAfter);
            try {
               float val = std::stof(numStr);
               if (val >= 40.0f && val <= 300.0f)
                  return val;
            } catch (...) {}
         }
         pos += 3;
      }
   }

   if (buf == nullptr || buf->numFrames <= 0 || buf->channels <= 0 || buf->channelData.empty())
      { if (detected) *detected = false; return fallbackBpm > 0.0f ? fallbackBpm : 120.0f; }

   const double sr = buf->sampleRate > 0.0 ? buf->sampleRate : 44100.0;
   const int numFrames = buf->numFrames;
   const double durationSec = (double)numFrames / sr;

   // Strategy 2: Exact musical loop duration matching (1, 2, 4, 8, 16 bars)
   float bestLoopBpm = 0.0f;
   float bestLoopDiff = 999.0f;
   for (int bars : { 1, 2, 4, 8, 16, 32 })
   {
      const double candidateBpm = (double)(bars * 240) / durationSec;
      if (candidateBpm >= 60.0 && candidateBpm <= 200.0)
      {
         const float rounded = std::roundf((float)candidateBpm);
         const float diff = std::abs((float)candidateBpm - rounded);
         if (diff < 0.15f && diff < bestLoopDiff)
         {
            bestLoopDiff = diff;
            bestLoopBpm = rounded;
         }
      }
   }

   // Strategy 3: Transient Onset Detection & IOI clustering via SlicerDsp
   const int maxAnalyzeFrames = std::min(numFrames, (int)(sr * 30.0));
   std::vector<float> mono(maxAnalyzeFrames);
   const float* ch0 = buf->channelData.data();
   if (buf->channels == 1)
   {
      std::copy(ch0, ch0 + maxAnalyzeFrames, mono.begin());
   }
   else
   {
      const float* ch1 = ch0 + numFrames;
      for (int i = 0; i < maxAnalyzeFrames; i++)
         mono[i] = 0.5f * (ch0[i] + ch1[i]);
   }

   SlicerDsp::Params params;
   params.sensitivity = 70.0f;
   params.maxSlices = 128;
   std::vector<int> onsets;
   std::vector<float> strengths;
   std::atomic<bool> abortFlag{false};
   SlicerDsp::Detect(mono.data(), maxAnalyzeFrames, sr, params, onsets, strengths, &abortFlag);

   if (onsets.size() >= 4)
   {
      std::vector<double> onsetTimes(onsets.size());
      for (size_t i = 0; i < onsets.size(); i++)
         onsetTimes[i] = (double)onsets[i] / sr;

      float bestScore = -1.0f;
      float bestBpm = 0.0f;
      const double minIoiSec = 0.15; // 400 BPM
      const double maxIoiSec = 2.0;  // 30 BPM

      for (float candidateBpm = 60.0f; candidateBpm <= 200.0f; candidateBpm += 0.5f)
      {
         const double beatPeriod = 60.0 / (double)candidateBpm;
         float score = 0.0f;

         for (size_t i = 0; i < onsets.size(); i++)
         {
            for (size_t j = i + 1; j < onsets.size() && j < i + 16; j++)
            {
               const double delta = onsetTimes[j] - onsetTimes[i];
               if (delta < minIoiSec) continue;
               if (delta > maxIoiSec) break;

               for (double mult : { 0.25, 0.333333, 0.5, 0.666667, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 })
               {
                  const double targetDelta = beatPeriod * mult;
                  const double err = std::abs(delta - targetDelta);
                  if (err < 0.025 * mult)
                  {
                     const float weight = (mult == 1.0 || mult == 0.5 || mult == 2.0) ? 2.0f : 1.0f;
                     score += weight * (1.0f - (float)(err / (0.025 * mult)));
                  }
               }
            }
         }

         if (candidateBpm >= 85.0f && candidateBpm <= 145.0f)
            score *= 1.15f;

         if (score > bestScore)
         {
            bestScore = score;
            bestBpm = candidateBpm;
         }
      }

      if (bestScore > 5.0f && bestBpm > 0.0f)
      {
         if (bestLoopBpm > 0.0f)
         {
            if (std::abs(bestLoopBpm - bestBpm) < 2.0f ||
                std::abs(bestLoopBpm * 2.0f - bestBpm) < 2.0f ||
                std::abs(bestLoopBpm * 0.5f - bestBpm) < 2.0f)
            {
               return bestLoopBpm;
            }
         }
         if (std::abs(bestBpm - std::roundf(bestBpm)) < 0.12f)
            return std::roundf(bestBpm);
         return bestBpm;
      }
   }

   if (bestLoopBpm > 0.0f && bestLoopDiff < 0.1f)
      return bestLoopBpm;

   { if (detected) *detected = false; return fallbackBpm > 0.0f ? fallbackBpm : 120.0f; }
}


uint64_t ArrangeImportFile(const std::string& path, int laneHint, Arrange::Tick at)
{
   static const std::vector<std::string> kAudioExt = { "wav", "aif", "aiff", "mp3", "m4a", "aac", "caf", "flac", "ogg" };
   int laneType = -1;
   const char* nodeType = nullptr;
   bool isImage = false, isVideo = false;
   if (HasExtension(path, kAudioExt))
   {
      laneType = Arrange::kLaneAudio;
      nodeType = "Audio File";
   }
   else if (HasExtension(path, MediaExtensions::Video()))
   {
      laneType = Arrange::kLaneVideo;
      nodeType = "Video";
      isVideo = true;
   }
   else if (HasExtension(path, MediaExtensions::Image()))
   {
      laneType = Arrange::kLaneVideo;
      nodeType = "Image Source";
      isImage = true;
   }
   else
      return 0;
   const std::string category = NodeFactory::Instance().CategoryOf(nodeType);
   GraphNode* gn = SpawnNode(nodeType, category.empty() ? "Source" : category, gViewCenterCanvas.x, gViewCenterCanvas.y);
   if (gn == nullptr || gn->node == nullptr)
      return 0;
   const uint64_t uid = gn->uid;
   double seconds = 0.0;
   bool hasVideoAudio = false;
   if (auto* af = dynamic_cast<AudioFileNode*>(gn->node.get()))
   {
      af->loop = false;
      af->Open(path);
      seconds = af->Duration();
   }
   else if (auto* img = dynamic_cast<ImageSourceNode*>(gn->node.get()))
   {
      img->Load(path);
      seconds = 5.0;
   }
   else if (auto* vid = dynamic_cast<VideoSourceNode*>(gn->node.get()))
   {
      vid->loop = false;
      vid->Open(path);
      seconds = vid->Duration();
      hasVideoAudio = vid->HasAudio();
   }
   const double bpm = std::max(1.0f, Transport::Instance().Tempo());
   // Audio Samples: estimate the file's tempo; with "sync to tempo" the box
   // is measured in the sample's own beats and playback stretches to the
   // project tempo (upstream behaviour).
   float sampleBpm = (float)bpm;
   bool detected = false;
   const bool isAudio = laneType == Arrange::kLaneAudio;
   if (isAudio)
   {
      Platform::SampleBuffer buf;
      std::string err;
      if (Platform::DecodeAudioFileToBuffer(path, buf, err))
         sampleBpm = ArrangeEstimateSampleBpm(&buf, path, (float)bpm, &detected);
   }
   const bool sync = isAudio && gArrange.settings.importSyncToTempo;
   const double effBpm = Arrange::SampleSourceBpm(sync, sampleBpm, bpm);
   const Arrange::Tick length = seconds > 0.0 ? Arrange::SampleClipLengthTicks(seconds, effBpm) : ArrangeTicksPerBar();
   const size_t slash = path.find_last_of("/\\");
   std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
   const size_t dot = name.find_last_of('.');
   if (dot != std::string::npos)
      name = name.substr(0, dot);

   uint64_t made = 0;
   ArrangeEdit([&]()
   {
      int lane = (laneHint >= 0 && laneHint < (int)gArrange.lanes.size() &&
                  gArrange.lanes[(size_t)laneHint].type == laneType) ? laneHint : ArrangeFirstLaneOfType(laneType);
      if (lane < 0)
         lane = Arrange::LaneIndex(gArrange, Arrange::AddLane(gArrange, laneType));
      Arrange::Clip c;
      c.start = std::max<Arrange::Tick>(0, at);
      c.length = length;
      c.srcUid = uid;
      c.name = name;
      c.sampleDropped = !isImage;
      c.syncToTempo = sync;
      c.sampleBpm = sampleBpm;
      c.origBpm = detected ? sampleBpm : 0.0f;
      c.sourceDurationSeconds = (float)seconds;
      Arrange::PlaceOverwrite(gArrange, gArrange.lanes[(size_t)lane].id, c, &made);
      if (isVideo && hasVideoAudio)
      {
         // Its soundtrack goes on an audio lane, same box.
         int alane = ArrangeFirstLaneOfType(Arrange::kLaneAudio);
         if (alane < 0)
            alane = Arrange::LaneIndex(gArrange, Arrange::AddLane(gArrange, Arrange::kLaneAudio));
         Arrange::Clip a = c;
         a.id = 0;
         a.srcOutput = 1;
         a.sampleDropped = false; // the video node keeps its own sound in sync
         Arrange::PlaceOverwrite(gArrange, gArrange.lanes[(size_t)alane].id, a);
      }
   });
   if (made != 0)
   {
      gArrangePanelOpen = true;
      gArrangeSel = { made };
   }
   return made;
}

bool ArrangeHandleFileDrop(const std::vector<std::string>& paths, ImVec2 pos)
{
   if (!gArrangePanelOpen || pos.x < gArrangePanelMin.x || pos.y < gArrangePanelMin.y ||
       pos.x > gArrangePanelMax.x || pos.y > gArrangePanelMax.y)
      return false;
   const int lane = ArrangeLaneAtScreenY(pos.y);
   Arrange::Tick at = Arrange::BeatsToTicks(std::max(0.0, ArrangeBeatAtScreenX(pos.x)));
   const Arrange::Tick g = ArrangeSnapGridTicks();
   if (g > 0)
      at = Arrange::SnapToGrid(at, g);
   for (const std::string& p : paths)
   {
      const uint64_t id = ArrangeImportFile(p, lane, at);
      if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
         at = c->End(); // several files land one after another
   }
   return true;
}

// ---- the panel ---------------------------------------------------------------

constexpr float kArrangeHeaderW = 210.0f;
constexpr float kArrangeRulerH = 30.0f;
constexpr float kArrangeRowH = 58.0f;
constexpr float kArrangeRowMinH = 30.0f;
constexpr float kArrangeHScrollH = 12.0f;
constexpr float kArrangeEdgePx = 6.0f;
constexpr float kArrangeBasePxPerBeat = 28.0f;

float gArrangeVScroll = 0.0f;
constexpr float kArrangeGroupRowH = 24.0f;
std::vector<int> gArrangeLaneDepth; // nesting depth per lane (indent)
struct ArrangeGroupRow
{
   uint64_t id = 0;
   int depth = 0;
   float y0 = 0.0f, y1 = 0.0f;
};
uint64_t gArrangeRenameGroup = 0;
float gArrangeLaneX0 = 0.0f;        // screen x of beat == scroll (last frame)
uint64_t gArrangeRenameLane = 0;    // lane whose name is being edited
bool gArrangeRenameJustStarted = false;
uint64_t gArrangeRenameMarker = 0;
char gArrangeRenameBuf[128] = {};

float ArrangePxPerBeat()
{
   return kArrangeBasePxPerBeat * std::clamp(gArrange.settings.zoom, 0.02f, 40.0f);
}

double ArrangeBeatAtScreenX(float x)
{
   return (double)gArrange.settings.scroll + (double)(x - gArrangeLaneX0) / (double)ArrangePxPerBeat();
}

float ArrangeScreenXOfBeat(double beat)
{
   return gArrangeLaneX0 + (float)((beat - (double)gArrange.settings.scroll) * (double)ArrangePxPerBeat());
}

float ArrangeRowHeight(const Arrange::Lane& lane)
{
   return lane.rowHeight > 0.0f ? std::max(kArrangeRowMinH, lane.rowHeight) : kArrangeRowH;
}

void ArrangeSetZoomAround(float newZoom, float anchorX)
{
   newZoom = std::clamp(newZoom, 0.02f, 40.0f);
   const double anchorBeat = ArrangeBeatAtScreenX(anchorX);
   gArrange.settings.zoom = newZoom;
   gArrange.settings.scroll = (float)std::max(0.0, anchorBeat - (double)(anchorX - gArrangeLaneX0) / (double)ArrangePxPerBeat());
}

enum ArrangeDragMode
{
   kArrDragNone = 0, kArrDragMove, kArrDragTrimL, kArrDragTrimR, kArrDragFadeIn, kArrDragFadeOut,
   kArrDragMarquee, kArrDragScrub, kArrDragLoop, kArrDragMarker, kArrDragHScroll, kArrDragRowResize
};

struct ArrangeDragState
{
   int mode = kArrDragNone;
   ImVec2 startMouse;
   Arrange::Model base;
   uint64_t clipId = 0;
   uint64_t markerId = 0;
   uint64_t laneId = 0;
   int startLane = -1;
   int lastLane = -1;
   Arrange::Tick grabTick = 0;
   Arrange::Tick origStart = 0;
   Arrange::Tick loopAnchor = 0;
   std::vector<uint64_t> ids;
   std::vector<uint64_t> selBefore;
   bool moved = false;
   float scrollAtStart = 0.0f;
   float heightAtStart = 0.0f;
};
ArrangeDragState gArrDrag;

Arrange::Tick ArrangeSnapMaybe(Arrange::Tick t)
{
   if (ImGui::GetIO().KeyAlt)
      return std::max<Arrange::Tick>(0, t); // Alt: free, off the grid
   const Arrange::Tick g = ArrangeSnapGridTicks();
   return std::max<Arrange::Tick>(0, g > 0 ? Arrange::SnapToGrid(t, g) : t);
}

bool ArrangeIsSelected(uint64_t id)
{
   return std::find(gArrangeSel.begin(), gArrangeSel.end(), id) != gArrangeSel.end();
}

// Snap grid choices: (label, division, triplet).
struct ArrangeGridChoice { const char* label; int division; bool triplet; };
const ArrangeGridChoice kArrangeGridChoices[10] = {
   { "Off", 0, false }, { "Bar", 1, false }, { "1/2", 2, false }, { "1/2 T", 2, true },
   { "1/4", 4, false }, { "1/4 T", 4, true }, { "1/8", 8, false }, { "1/8 T", 8, true },
   { "1/16", 16, false }, { "1/16 T", 16, true }
};

void ArrangeColorMenu(float& r, float& g, float& b, bool allowNone)
{
   for (int i = allowNone ? 0 : 1; i < 10; i++)
   {
      ImGui::PushID(i);
      const ImVec4 c = ImGui::ColorConvertU32ToFloat4(kArrangePalette[i].col);
      ImGui::ColorButton("##sw", c, ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
      ImGui::SameLine();
      if (ImGui::MenuItem(i == 0 ? "None" : kArrangePalette[i].name))
      {
         if (i == 0)
            r = g = b = 0.0f;
         else
         {
            r = c.x;
            g = c.y;
            b = c.z;
         }
         gArrange.revision++;
      }
      ImGui::PopID();
   }
}

// "Assign source" submenu: every node that fits the lane type.
void ArrangeSourceMenu(uint64_t clipId, int laneType)
{
   int shown = 0;
   for (GraphNode& gn : gNodes)
   {
      if (!ArrangeNodeFitsLane(gn, laneType))
         continue;
      shown++;
      ImGui::PushID((int)gn.index);
      const Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
      const bool current = c != nullptr && c->srcUid == gn.uid;
      if (ImGui::MenuItem(NodeTitleWithInstance(gn).c_str(), nullptr, current))
         ArrangeAssignClipSource(clipId, gn.uid);
      ImGui::PopID();
   }
   if (shown == 0)
      ImGui::TextDisabled(laneType == Arrange::kLaneAudio ? "no audio nodes on the canvas" : "no image nodes on the canvas");
}

// ---- Sample tempo (upstream's helpers) --------------------------------------

double ArrangeSampleEffBpm(const Arrange::Clip& c)
{
   return Arrange::SampleSourceBpm(c.syncToTempo, c.sampleBpm, (double)Transport::Instance().Tempo());
}

// Keeps a Sample's box covering the same audio when its effective BPM
// changes: only the playback speed changes. TrimEdge clamps at neighbours.
void ArrangeRescaleSampleBox(uint64_t clipId, double oldEffBpm, double newEffBpm)
{
   const Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
   if (c == nullptr || !(oldEffBpm > 0.0) || !(newEffBpm > 0.0) || oldEffBpm == newEffBpm)
      return;
   const Arrange::Tick newLength =
      std::max<Arrange::Tick>(1, (Arrange::Tick)std::llround((double)c->length * newEffBpm / oldEffBpm));
   Arrange::TrimEdge(gArrange, clipId, Arrange::kEdgeEnd, c->start + newLength);
}

void ArrangeSetSampleSync(uint64_t clipId, bool sync)
{
   Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
   if (c == nullptr || c->syncToTempo == sync)
      return;
   const double oldEff = ArrangeSampleEffBpm(*c);
   c->syncToTempo = sync;
   const double newEff = ArrangeSampleEffBpm(*c);
   gArrange.revision++;
   ArrangeRescaleSampleBox(clipId, oldEff, newEff);
}

void ArrangeSetSampleBpm(uint64_t clipId, float bpm)
{
   Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
   if (c == nullptr)
      return;
   bpm = std::clamp(bpm, 20.0f, 999.0f);
   if (bpm == c->sampleBpm)
      return;
   const double oldEff = ArrangeSampleEffBpm(*c);
   c->sampleBpm = bpm;
   const double newEff = ArrangeSampleEffBpm(*c);
   gArrange.revision++;
   ArrangeRescaleSampleBox(clipId, oldEff, newEff);
}

// ---- per-clip modulation bypass ----------------------------------------------

// (nodeIndex, paramIndex) pairs whose modulation the clip under the
// playhead switches off this frame (main.cpp's modulation pass reads it).
std::set<std::pair<int, int>> gArrangeModBypass;

void ArrangeCollectModBypass()
{
   gArrangeModBypass.clear();
   const double beat = Transport::Instance().Beats();
   for (const Arrange::Lane& lane : gArrange.lanes)
   {
      if (!Arrange::LaneEffectivelyEnabled(gArrange, lane))
         continue;
      for (const Arrange::Clip& c : lane.clips)
      {
         const double s0 = Arrange::TicksToBeats(c.start);
         if (s0 > beat)
            break;
         if (!(beat < Arrange::TicksToBeats(c.End())) || !c.enabled || c.bypassedModParams.empty())
            continue;
         if (GraphNode* gn = FindNodeByUid(c.srcUid))
            for (int p : c.bypassedModParams)
               gArrangeModBypass.insert({ gn->index, p });
      }
   }
}

bool ArrangeModBypassed(int nodeIndex, int paramIndex)
{
   return !gArrangeModBypass.empty() && gArrangeModBypass.count({ nodeIndex, paramIndex }) != 0;
}

// Clip settings (double-click a clip, or its menu). Floating window: every
// widget edit is one undo step (gesture), like upstream's Clip Settings.
void DrawArrangeClipSettingsWindow()
{
   if (!gArrangeSettingsOpen || gArrangeSettingsClip == 0)
      return;
   const Arrange::Loc loc = Arrange::Find(gArrange, gArrangeSettingsClip);
   if (!loc.Valid())
   {
      gArrangeSettingsOpen = false;
      return;
   }
   ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
   if (!ImGui::Begin("Clip settings", &gArrangeSettingsOpen, ImGuiWindowFlags_AlwaysAutoResize))
   {
      ImGui::End();
      return;
   }
   Arrange::Lane& lane = gArrange.lanes[(size_t)loc.lane];
   Arrange::Clip* c = &lane.clips[(size_t)loc.index];
   const bool audio = lane.type == Arrange::kLaneAudio;
   ImGui::TextDisabled("%s clip on %s%s", c->sampleDropped ? (audio ? "Audio Sample" : "Video Sample")
                                                          : (audio ? "Audio" : "Video"),
                       ArrangeLaneLabel(gArrange, loc.lane).c_str(), c->enabled ? "" : "  (disabled)");

   char nameBuf[128];
   snprintf(nameBuf, sizeof(nameBuf), "%s", c->name.c_str());
   if (ImGui::InputTextWithHint("name", ArrangeClipLabel(*c).c_str(), nameBuf, sizeof(nameBuf)))
   {
      c->name = nameBuf;
      gArrange.revision++;
   }
   ArrangeTrackGesture();

   // Source
   {
      GraphNode* gn = FindNodeByUid(c->srcUid);
      const std::string cur = gn ? NodeTitleWithInstance(*gn) : (c->srcUid ? "(offline)" : "(none)");
      if (c->sampleDropped)
         ImGui::Text("source  %s", cur.c_str());
      else if (ImGui::BeginCombo("source", cur.c_str()))
      {
         const uint64_t id = c->id;
         ArrangeSourceMenu(id, lane.type);
         ImGui::EndCombo();
         const Arrange::Loc again = Arrange::Find(gArrange, id); // the menu may have edited
         if (!again.Valid())
         {
            ImGui::End();
            return;
         }
         c = &gArrange.lanes[(size_t)again.lane].clips[(size_t)again.index];
      }
   }

   // Position / length in beats (bars shown alongside).
   {
      float startBeats = (float)Arrange::TicksToBeats(c->start);
      if (ImGui::DragFloat("start (beats)", &startBeats, 0.05f, 0.0f, 100000.0f, "%.3f"))
      {
         const Arrange::Tick want = Arrange::BeatsToTicks(std::max(0.0f, startBeats));
         if (gArrangeGestureOpen)
         {
            const uint64_t id = c->id;
            gArrange = gArrangeGestureBefore;
            const Arrange::Clip* orig = Arrange::FindClip(gArrange, id);
            if (orig != nullptr)
               Arrange::MoveClips(gArrange, { id }, want - orig->start, 0);
            gArrange.revision = gArrangeGestureBefore.revision + 1;
         }
      }
      ArrangeTrackGesture();
      const Arrange::Loc l2 = Arrange::Find(gArrange, gArrangeSettingsClip);
      if (!l2.Valid())
      {
         ImGui::End();
         return;
      }
      c = &gArrange.lanes[(size_t)l2.lane].clips[(size_t)l2.index];
      ImGui::SameLine();
      ImGui::TextDisabled("%s", ArrangeFormatPos(c->start).c_str());
      float lenBeats = (float)Arrange::TicksToBeats(c->length);
      if (ImGui::DragFloat("length (beats)", &lenBeats, 0.05f, 0.01f, 100000.0f, "%.3f"))
         Arrange::TrimEdge(gArrange, c->id, Arrange::kEdgeEnd, c->start + Arrange::BeatsToTicks(std::max(0.01f, lenBeats)));
      ArrangeTrackGesture();
      const Arrange::Loc l3 = Arrange::Find(gArrange, gArrangeSettingsClip);
      if (!l3.Valid())
      {
         ImGui::End();
         return;
      }
      c = &gArrange.lanes[(size_t)l3.lane].clips[(size_t)l3.index];
      ImGui::SameLine();
      ImGui::TextDisabled("%s", ArrangeFormatLen(c->length).c_str());
   }

   float fadeIn = (float)Arrange::TicksToBeats(c->fadeIn);
   float fadeOut = (float)Arrange::TicksToBeats(c->fadeOut);
   const float maxFade = (float)Arrange::TicksToBeats(c->length);
   if (ImGui::DragFloat("fade in (beats)", &fadeIn, 0.02f, 0.0f, maxFade, "%.2f"))
   {
      c->fadeIn = std::clamp<Arrange::Tick>(Arrange::BeatsToTicks(fadeIn), 0, c->length - c->fadeOut);
      gArrange.revision++;
   }
   ArrangeTrackGesture();
   if (ImGui::DragFloat("fade out (beats)", &fadeOut, 0.02f, 0.0f, maxFade, "%.2f"))
   {
      c->fadeOut = std::clamp<Arrange::Tick>(Arrange::BeatsToTicks(fadeOut), 0, c->length - c->fadeIn);
      gArrange.revision++;
   }
   ArrangeTrackGesture();

   if (audio)
   {
      if (ImGui::SliderFloat("gain (dB)", &c->gainDb, -60.0f, 12.0f, "%.1f dB"))
         gArrange.revision++;
      ArrangeTrackGesture();
      if (ImGui::SliderFloat("pan", &c->pan, -1.0f, 1.0f, "%.2f"))
         gArrange.revision++;
      ArrangeTrackGesture();
   }
   else
   {
      if (ImGui::SliderFloat("opacity", &c->opacity, 0.0f, 1.0f, "%.2f"))
         gArrange.revision++;
      ArrangeTrackGesture();
      const std::vector<std::string>& modes = BlendModes::Names();
      const int bm = std::clamp(c->blendMode, 0, (int)modes.size() - 1);
      if (ImGui::BeginCombo("blend", modes[(size_t)bm].c_str()))
      {
         for (int i = 0; i < (int)modes.size(); i++)
            if (ImGui::Selectable(modes[(size_t)i].c_str(), i == bm))
            {
               const uint64_t id = c->id;
               ArrangeEdit([&]()
               {
                  if (Arrange::Clip* cc = Arrange::FindClip(gArrange, id))
                  {
                     cc->blendMode = i;
                     gArrange.revision++;
                  }
               });
               const Arrange::Loc l4 = Arrange::Find(gArrange, id);
               if (l4.Valid())
                  c = &gArrange.lanes[(size_t)l4.lane].clips[(size_t)l4.index];
            }
         ImGui::EndCombo();
      }
      if (ImGui::SliderFloat("brightness", &c->colorBrightness, -1.0f, 1.0f, "%.2f"))
         gArrange.revision++;
      ArrangeTrackGesture();
      if (ImGui::SliderFloat("contrast", &c->colorContrast, -1.0f, 1.0f, "%.2f"))
         gArrange.revision++;
      ArrangeTrackGesture();
      if (ImGui::SliderFloat("saturation", &c->colorSaturation, 0.0f, 2.0f, "%.2f"))
         gArrange.revision++;
      ArrangeTrackGesture();
   }

   if (c->sampleDropped)
   {
      if (ImGui::DragFloat("source offset (s)", &c->sourceOffsetSeconds, 0.01f, 0.0f, 36000.0f, "%.3f s"))
      {
         c->sourceOffsetSeconds = std::max(0.0f, c->sourceOffsetSeconds);
         gArrange.revision++;
      }
      ArrangeTrackGesture();
      if (c->sourceDurationSeconds > 0.0f)
         ImGui::TextDisabled("file length %.2f s", c->sourceDurationSeconds);
   }

   // Sample tempo / pitch (Audio Samples).
   if (c->sampleDropped && audio && ArrangeClipIsAudioSample(*c))
   {
      ImGui::SeparatorText("sample");
      const uint64_t id = c->id;
      bool sync = c->syncToTempo;
      if (ImGui::Checkbox("sync to tempo", &sync))
         ArrangeEdit([&]() { ArrangeSetSampleSync(id, sync); });
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("on: the box is in the sample's own beats and playback stretches to the project\n"
                           "tempo (pitch kept). off: plays at native speed; a tempo change shows more or less of it.");
      if (const Arrange::Clip* cc = Arrange::FindClip(gArrange, id))
      {
         float sbpm = cc->sampleBpm;
         ImGui::SetNextItemWidth(120.0f);
         if (ImGui::DragFloat("sample bpm", &sbpm, 0.1f, 20.0f, 999.0f, "%.2f"))
         {
            if (!gArrangeGestureOpen)
               ArrangeGestureBegin();
            ArrangeSetSampleBpm(id, sbpm);
         }
         ArrangeTrackGesture();
         ImGui::SameLine();
         if (ImGui::SmallButton("/2"))
            ArrangeEdit([&]() { if (auto* x = Arrange::FindClip(gArrange, id)) ArrangeSetSampleBpm(id, x->sampleBpm * 0.5f); });
         ImGui::SameLine();
         if (ImGui::SmallButton("x2"))
            ArrangeEdit([&]() { if (auto* x = Arrange::FindClip(gArrange, id)) ArrangeSetSampleBpm(id, x->sampleBpm * 2.0f); });
         if (const Arrange::Clip* c2 = Arrange::FindClip(gArrange, id))
         {
            if (c2->origBpm > 0.0f)
            {
               ImGui::TextDisabled("detected %.1f bpm", c2->origBpm);
               ImGui::SameLine();
               if (ImGui::SmallButton("reset"))
                  ArrangeEdit([&]() { if (auto* x = Arrange::FindClip(gArrange, id)) ArrangeSetSampleBpm(id, x->origBpm); });
            }
            else
               ImGui::TextDisabled("no tempo detected - set the sample bpm by ear");
            if (c2->syncToTempo)
               ImGui::TextDisabled("plays at %.0f%% speed", 100.0 * Transport::Instance().Tempo() / std::max(1.0f, c2->sampleBpm));
         }
      }
      if (Arrange::Clip* c3 = Arrange::FindClip(gArrange, id))
      {
         if (ImGui::SliderFloat("pitch (st)", &c3->pitch, -24.0f, 24.0f, "%+.1f st"))
            gArrange.revision++;
         ArrangeTrackGesture();
      }
      const Arrange::Loc l5 = Arrange::Find(gArrange, id);
      if (!l5.Valid())
      {
         ImGui::End();
         return;
      }
      c = &gArrange.lanes[(size_t)l5.lane].clips[(size_t)l5.index];
   }

   // Video clips: locked to the timeline, or free running.
   if (GraphNode* vsrc = FindNodeByUid(c->srcUid))
      if (dynamic_cast<VideoSourceNode*>(vsrc->node.get()) != nullptr)
      {
         bool locked = c->retrigger;
         if (ImGui::Checkbox("lock video to timeline", &locked))
         {
            const uint64_t id = c->id;
            ArrangeEdit([&]() { if (auto* x = Arrange::FindClip(gArrange, id)) { x->retrigger = locked; gArrange.revision++; } });
            const Arrange::Loc l6 = Arrange::Find(gArrange, id);
            if (!l6.Valid())
            {
               ImGui::End();
               return;
            }
            c = &gArrange.lanes[(size_t)l6.lane].clips[(size_t)l6.index];
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("on: the movie position follows the playhead (jumps, loops, scrubs)\n"
                              "off: the movie runs on its own while the clip is on");
         if (!c->sampleDropped)
         {
            if (ImGui::DragFloat("movie offset (s)", &c->sourceOffsetSeconds, 0.01f, 0.0f, 36000.0f, "%.3f s"))
            {
               c->sourceOffsetSeconds = std::max(0.0f, c->sourceOffsetSeconds);
               gArrange.revision++;
            }
            ArrangeTrackGesture();
         }
      }

   // Per-clip modulation bypass: the source's modulated params.
   if (GraphNode* msrc = FindNodeByUid(c->srcUid))
   {
      std::vector<const ParamRef*> modded;
      for (const ParamRef& r : Modulation::Instance().FrameParams())
         if (r.nodeIndex == msrc->index && Modulation::Instance().IsModulated(r.nodeIndex, r.paramIndex))
            modded.push_back(&r);
      if (!modded.empty() || !c->bypassedModParams.empty())
      {
         ImGui::SeparatorText("modulation while this clip plays");
         const uint64_t id = c->id;
         for (const ParamRef* r : modded)
         {
            bool on = !c->IsModBypassed(r->paramIndex);
            ImGui::PushID(r->paramIndex);
            if (ImGui::Checkbox(r->name.c_str(), &on))
            {
               const int pi = r->paramIndex;
               ArrangeEdit([&]()
               {
                  if (auto* x = Arrange::FindClip(gArrange, id))
                  {
                     x->SetModBypassed(pi, !on);
                     gArrange.revision++;
                  }
               });
            }
            ImGui::PopID();
            const Arrange::Loc l7 = Arrange::Find(gArrange, id);
            if (!l7.Valid())
            {
               ImGui::End();
               return;
            }
            c = &gArrange.lanes[(size_t)l7.lane].clips[(size_t)l7.index];
         }
         if (modded.empty())
            ImGui::TextDisabled("(the bypassed params are not modulated now)");
         else
            ImGui::TextDisabled("unchecked = the knob holds still while this clip is under the playhead");
      }
   }

   bool enabled = c->enabled;
   if (ImGui::Checkbox("enabled", &enabled))
   {
      const uint64_t id = c->id;
      ArrangeEdit([&]() { Arrange::SetEnabled(gArrange, { id }, enabled ? Arrange::kEnable : Arrange::kDisable); });
   }
   ImGui::End();
}

// Toolbar button that reads as a lit toggle.
bool ArrangeToggleButton(const char* label, bool on, ImU32 onCol = IM_COL32(60, 150, 100, 255), float w = 0.0f)
{
   if (on)
   {
      ImGui::PushStyleColor(ImGuiCol_Button, onCol);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ArrangeScaleCol(onCol, 1.15f));
   }
   const bool pressed = ImGui::Button(label, ImVec2(w, 0));
   if (on)
      ImGui::PopStyleColor(2);
   return pressed;
}

void DrawArrangeToolbar(float width)
{
   Transport& tr = Transport::Instance();
   if (ArrangeToggleButton(gArrangeTimelineMode ? "TIMELINE ON" : "TIMELINE OFF", gArrangeTimelineMode,
                           IM_COL32(200, 120, 40, 255)))
      gArrangeTimelineMode = !gArrangeTimelineMode;
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Timeline mode: the timeline owns the audio output.\n"
                        "Canvas Audio Outs go quiet (live ones stay), and only what the audio clips\n"
                        "play is heard. Off: the canvas plays as usual and the timeline only drives video.");
   ImGui::SameLine();
   if (ImGui::Button("|<"))
      ArrangeSeekTick(gArrange.settings.loop.enabled && ArrangePlayTick() > gArrange.settings.loop.start
                         ? gArrange.settings.loop.start : 0);
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("to the start (or the loop start)  -  Home");
   ImGui::SameLine();
   if (ArrangeToggleButton(tr.IsPlaying() ? "PAUSE" : "PLAY", tr.IsPlaying()))
      tr.TogglePlay();
   ImGui::SameLine();
   {
      const Arrange::Tick play = ArrangePlayTick();
      ImGui::AlignTextToFramePadding();
      const int saved = gArrange.settings.timeDisplay;
      gArrange.settings.timeDisplay = 0;
      const std::string bars = ArrangeFormatPos(play);
      gArrange.settings.timeDisplay = 1;
      const std::string time = ArrangeFormatPos(play);
      gArrange.settings.timeDisplay = saved;
      ImGui::Text("%s", saved == 0 ? bars.c_str() : time.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("%s  %.1f bpm", saved == 0 ? time.c_str() : bars.c_str(), tr.Tempo());
   }
   ImGui::SameLine(0.0f, 16.0f);
   ImGui::SetNextItemWidth(78.0f);
   {
      int cur = 0;
      for (int i = 0; i < 10; i++)
         if (kArrangeGridChoices[i].division == gArrange.settings.snapDivision &&
             kArrangeGridChoices[i].triplet == gArrange.settings.snapTriplet)
            cur = i;
      if (ImGui::BeginCombo("##arrsnap", kArrangeGridChoices[cur].label))
      {
         for (int i = 0; i < 10; i++)
            if (ImGui::Selectable(kArrangeGridChoices[i].label, i == cur))
            {
               gArrange.settings.snapDivision = kArrangeGridChoices[i].division;
               gArrange.settings.snapTriplet = kArrangeGridChoices[i].triplet;
               gPatchDirty = true;
            }
         ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("snap grid (hold Alt while dragging to place freely)");
   }
   ImGui::SameLine();
   if (ImGui::Button(gArrange.settings.timeDisplay == 0 ? "BARS" : "TIME"))
   {
      gArrange.settings.timeDisplay = gArrange.settings.timeDisplay == 0 ? 1 : 0;
      gPatchDirty = true;
   }
   ImGui::SameLine();
   if (ArrangeToggleButton("LOOP", gArrange.settings.loop.enabled, IM_COL32(70, 110, 200, 255)))
   {
      Arrange::LoopRange l = gArrange.settings.loop;
      if (!l.enabled && l.end <= l.start)
      {
         // No range yet: the bar under the playhead, 4 bars long.
         const Arrange::Tick bar = ArrangeTicksPerBar();
         l.start = Arrange::GridFloor(ArrangePlayTick(), bar);
         l.end = l.start + 4 * bar;
      }
      ArrangeSetLoop(!l.enabled, l.start, l.end);
   }
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("loop the range shown on the ruler\nShift+drag on the ruler sets it");
   ImGui::SameLine();
   if (ArrangeToggleButton("BLADE", gArrangeBlade, IM_COL32(200, 70, 70, 255)))
      gArrangeBlade = !gArrangeBlade;
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("blade (B): click a clip to cut it there  -  Esc to leave");
   ImGui::SameLine();
   if (ArrangeToggleButton("FOLLOW", gArrangeFollow, IM_COL32(90, 90, 140, 255)))
      gArrangeFollow = !gArrangeFollow;
   ImGui::SameLine(0.0f, 16.0f);
   if (ImGui::Button("+ VIDEO"))
      ArrangeAddTrack(Arrange::kLaneVideo);
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("add a video track  -  Shift+J");
   ImGui::SameLine();
   if (ImGui::Button("+ AUDIO"))
      ArrangeAddTrack(Arrange::kLaneAudio);
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("add an audio track  -  Shift+K");
   ImGui::SameLine();
   if (ImGui::Button("+ GROUP"))
   {
      const int li = Arrange::LaneIndex(gArrange, gArrangeSelLane);
      if (li >= 0)
      {
         const uint64_t id = gArrangeSelLane;
         const uint64_t cur = gArrange.lanes[(size_t)li].groupId;
         ArrangeEdit([&]() { Arrange::GroupSelectedLanes(gArrange, { id }, cur); });
      }
   }
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("put the selected track (click its header) in a new group / folder.\n"
                        "Right-click a track header > Group to move tracks between groups.");
   ImGui::SameLine(0.0f, 16.0f);
   if (ImGui::Button("-"))
      ArrangeSetZoomAround(gArrange.settings.zoom / 1.25f, gArrangeLaneX0 + 200.0f);
   ImGui::SameLine();
   if (ImGui::Button("+"))
      ArrangeSetZoomAround(gArrange.settings.zoom * 1.25f, gArrangeLaneX0 + 200.0f);
   ImGui::SameLine();
   if (ImGui::Button("FIT"))
   {
      const double endBeat = std::max(16.0, Arrange::TicksToBeats(Arrange::ArrangementEnd(gArrange)) + 4.0);
      const float visible = std::max(100.0f, width - kArrangeHeaderW - (gArrangeMonitor ? 0.0f : 0.0f) - 40.0f);
      gArrange.settings.zoom = std::clamp((float)(visible / endBeat) / kArrangeBasePxPerBeat, 0.02f, 40.0f);
      gArrange.settings.scroll = 0.0f;
   }
   ImGui::SameLine(0.0f, 16.0f);
   if (ImGui::Button("RENDER..."))
      gArrangeRenderWindowOpen = true;
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("render the timeline to MP4 / MOV (video + audio) or WAV, with a queue");
   ImGui::SameLine();
   if (ArrangeToggleButton("MONITOR", gArrangeMonitor, IM_COL32(90, 90, 140, 255)))
      gArrangeMonitor = !gArrangeMonitor;
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("video monitor: the composite of the video tracks at the playhead.\n"
                        "Add a Timeline node (Source) to send it to an Output.");
}

// Header for one lane (name, type, mute/solo/enable, gain/pan or opacity).
void DrawArrangeLaneHeader(int li, ImVec2 tl, float w, float h, bool& openHeaderMenu, int& headerMenuLane)
{
   Arrange::Lane& lane = gArrange.lanes[(size_t)li];
   ImDrawList* dl = ImGui::GetWindowDrawList();
   const ImVec2 br(tl.x + w, tl.y + h);
   const bool enabled = Arrange::LaneEffectivelyEnabled(gArrange, lane);
   const bool selectedLane = lane.id == gArrangeSelLane;
   dl->AddRectFilled(tl, br, selectedLane ? IM_COL32(48, 52, 66, 255) : IM_COL32(36, 38, 48, 255));
   dl->AddRectFilled(tl, ImVec2(tl.x + 5.0f, br.y), ArrangeLaneAccent(lane));
   dl->AddLine(ImVec2(tl.x, br.y - 1.0f), ImVec2(br.x, br.y - 1.0f), IM_COL32(22, 24, 30, 255));

   ImGui::PushID((int)lane.id);
   // name (double-click renames)
   ImGui::SetCursorScreenPos(ImVec2(tl.x + 10.0f, tl.y + 4.0f));
   if (gArrangeRenameLane == lane.id)
   {
      ImGui::SetNextItemWidth(w - 60.0f);
      if (gArrangeRenameJustStarted)
      {
         ImGui::SetKeyboardFocusHere();
         gArrangeRenameJustStarted = false;
      }
      if (ImGui::InputText("##rename", gArrangeRenameBuf, sizeof(gArrangeRenameBuf),
                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
          ImGui::IsItemDeactivated())
      {
         const std::string nm = gArrangeRenameBuf;
         const uint64_t id = lane.id;
         ArrangeEdit([&]()
         {
            if (Arrange::Lane* l = Arrange::FindLane(gArrange, id))
               if (l->name != nm)
               {
                  l->name = nm;
                  gArrange.revision++;
               }
         });
         gArrangeRenameLane = 0;
      }
   }
   else
   {
      const std::string label = ArrangeLaneLabel(gArrange, li);
      ImGui::PushStyleColor(ImGuiCol_Text, enabled ? IM_COL32(225, 230, 240, 255) : IM_COL32(120, 125, 140, 255));
      ImGui::TextUnformatted(label.c_str());
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         gArrangeRenameLane = lane.id;
         gArrangeRenameJustStarted = true;
         snprintf(gArrangeRenameBuf, sizeof(gArrangeRenameBuf), "%s", label.c_str());
      }
      ImGui::SameLine();
      ImGui::TextDisabled(lane.type == Arrange::kLaneAudio ? "audio" : "video");
   }

   // Row-wide hit: select lane / right-click menu.
   const ImVec2 mouse = ImGui::GetIO().MousePos;
   const bool hovered = mouse.x >= tl.x && mouse.x < br.x && mouse.y >= tl.y && mouse.y < br.y &&
                        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
   if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
   {
      openHeaderMenu = true;
      headerMenuLane = li;
   }
   if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
      gArrangeSelLane = lane.id;

   if (h >= 42.0f)
   {
      ImGui::SetCursorScreenPos(ImVec2(tl.x + 10.0f, tl.y + 24.0f));
      auto smallToggle = [&](const char* label, bool on, ImU32 col, const char* tip) -> bool
      {
         if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, col);
         const bool p = ImGui::SmallButton(label);
         if (on)
            ImGui::PopStyleColor();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
         return p;
      };
      const uint64_t id = lane.id;
      if (smallToggle(lane.enabled ? "ON" : "OFF", lane.enabled, IM_COL32(60, 140, 90, 255), "track on/off"))
         ArrangeEdit([&]() { Arrange::SetLaneEnabled(gArrange, id, Arrange::kToggle); });
      if (lane.type == Arrange::kLaneAudio)
      {
         ImGui::SameLine();
         if (smallToggle("M", lane.mute, IM_COL32(200, 120, 40, 255), "mute"))
            ArrangeEdit([&]() { if (Arrange::Lane* l = Arrange::FindLane(gArrange, id)) { l->mute = !l->mute; gArrange.revision++; } });
         ImGui::SameLine();
         if (smallToggle("S", lane.solo, IM_COL32(200, 180, 40, 255), "solo"))
            ArrangeEdit([&]() { if (Arrange::Lane* l = Arrange::FindLane(gArrange, id)) { l->solo = !l->solo; gArrange.revision++; } });
         Arrange::Lane* l = Arrange::FindLane(gArrange, id);
         if (l != nullptr)
         {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(56.0f);
            if (ImGui::DragFloat("##gain", &l->gainDb, 0.2f, -60.0f, 12.0f, "%.1fdB"))
               gArrange.revision++;
            ArrangeTrackGesture();
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("track gain (double-click to type)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(44.0f);
            if (ImGui::DragFloat("##pan", &l->pan, 0.01f, -1.0f, 1.0f, "p%.2f"))
               gArrange.revision++;
            ArrangeTrackGesture();
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("track pan");
            // level meter
            const float peak = AudioEngine::Instance().ArrangeLanePeak(li);
            const float mx = br.x - 8.0f;
            const float mh = (h - 8.0f) * std::clamp(peak, 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(mx, tl.y + 4.0f), ImVec2(mx + 4.0f, br.y - 4.0f), IM_COL32(20, 22, 28, 255));
            dl->AddRectFilled(ImVec2(mx, br.y - 4.0f - mh), ImVec2(mx + 4.0f, br.y - 4.0f),
                              peak > 0.98f ? IM_COL32(230, 70, 60, 255) : IM_COL32(80, 200, 120, 255));
         }
      }
      else
      {
         Arrange::Lane* l = Arrange::FindLane(gArrange, id);
         if (l != nullptr)
         {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::SliderFloat("##opacity", &l->opacity, 0.0f, 1.0f, "opacity %.2f"))
               gArrange.revision++;
            ArrangeTrackGesture();
         }
      }
   }
   ImGui::PopID();
}

void DrawArrangeHatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col)
{
   dl->PushClipRect(a, b, true);
   for (float x = a.x - (b.y - a.y); x < b.x; x += 7.0f)
      dl->AddLine(ImVec2(x, b.y), ImVec2(x + (b.y - a.y), a.y), col);
   dl->PopClipRect();
}

// The timeline proper: ruler, lane headers, clips, playhead and every mouse
// gesture on them.
void DrawArrangeTimeline(ImVec2 areaMin, ImVec2 areaMax)
{
   ImGuiIO& io = ImGui::GetIO();
   ImDrawList* dl = ImGui::GetWindowDrawList();
   const float laneX0 = areaMin.x + kArrangeHeaderW;
   const float laneX1 = areaMax.x;
   const float rulerY0 = areaMin.y;
   const float rulerY1 = areaMin.y + kArrangeRulerH;
   const float rowsY0 = rulerY1;
   const float rowsY1 = areaMax.y - kArrangeHScrollH;
   gArrangeLaneX0 = laneX0;
   const float ppb = ArrangePxPerBeat();
   const double bpb = std::max(0.25, Transport::Instance().BeatsPerBar());
   const double scroll = gArrange.settings.scroll;
   const double visBeats = (double)(laneX1 - laneX0) / (double)ppb;
   const Arrange::Tick grid = ArrangeSnapGridTicks();
   const Arrange::Tick playTick = ArrangePlayTick();
   const double playBeat = Arrange::TicksToBeats(playTick);

   // Follow: keep the playhead on screen while playing.
   if (gArrangeFollow && Transport::Instance().IsPlaying() && gArrDrag.mode == kArrDragNone)
   {
      if (playBeat > scroll + visBeats * 0.92 || playBeat < scroll)
         gArrange.settings.scroll = (float)std::max(0.0, playBeat - visBeats * 0.08);
   }

   // Row layout (screen y), kept for drops and marquee. Track groups
   // (folders) come from the model's tree: a group header row, then its
   // lanes and child groups, hidden while the group is collapsed.
   gArrangeRowY.assign(gArrange.lanes.size(), { -1e9f, -1e9f });
   gArrangeLaneDepth.assign(gArrange.lanes.size(), 0);
   std::vector<ArrangeGroupRow> groupRows;
   float contentH = 0.0f;
   {
      std::vector<std::pair<bool, uint64_t>> order; // (isGroup, id) in draw order
      std::vector<int> depth;
      std::vector<bool> hidden;
      std::function<void(uint64_t, int, bool)> walk = [&](uint64_t parent, int d, bool hide)
      {
         for (const Arrange::RowSlot& slot : Arrange::TrackGroupChildren(gArrange, parent))
         {
            order.push_back({ slot.isGroup, slot.id });
            depth.push_back(d);
            hidden.push_back(hide);
            if (slot.isGroup)
            {
               const Arrange::TrackGroup* g = Arrange::FindTrackGroup(gArrange, slot.id);
               walk(slot.id, d + 1, hide || (g != nullptr && g->collapsed));
            }
         }
      };
      walk(0, 0, false);
      // Lanes the tree did not reach (a dangling group id) still show.
      for (const Arrange::Lane& lane : gArrange.lanes)
         if (std::find(order.begin(), order.end(), std::make_pair(false, lane.id)) == order.end())
         {
            order.push_back({ false, lane.id });
            depth.push_back(0);
            hidden.push_back(false);
         }
      for (size_t k = 0; k < order.size(); k++)
         if (!hidden[k])
            contentH += order[k].first ? kArrangeGroupRowH
                                       : ArrangeRowHeight(gArrange.lanes[(size_t)std::max(0, Arrange::LaneIndex(gArrange, order[k].second))]);
      const float rowsVisibleNow = rowsY1 - rowsY0;
      gArrangeVScroll = std::clamp(gArrangeVScroll, 0.0f, std::max(0.0f, contentH - rowsVisibleNow));
      float y = rowsY0 - gArrangeVScroll;
      for (size_t k = 0; k < order.size(); k++)
      {
         if (hidden[k])
            continue;
         if (order[k].first)
         {
            groupRows.push_back({ order[k].second, depth[k], y, y + kArrangeGroupRowH });
            y += kArrangeGroupRowH;
         }
         else
         {
            const int li = Arrange::LaneIndex(gArrange, order[k].second);
            if (li < 0)
               continue;
            const float h = ArrangeRowHeight(gArrange.lanes[(size_t)li]);
            gArrangeRowY[(size_t)li] = { y, y + h };
            gArrangeLaneDepth[(size_t)li] = depth[k];
            y += h;
         }
      }
   }
   auto laneAtY = [&](float y) -> int
   {
      for (int i = 0; i < (int)gArrangeRowY.size(); i++)
         if (y >= gArrangeRowY[(size_t)i].first && y < gArrangeRowY[(size_t)i].second)
            return i;
      return -1;
   };
   auto tickAtX = [&](float x) -> Arrange::Tick
   {
      return std::clamp<Arrange::Tick>(Arrange::BeatsToTicks(ArrangeBeatAtScreenX(x)), 0, Arrange::kMaxTick);
   };

   // ---- backgrounds and grid ----
   dl->AddRectFilled(areaMin, areaMax, IM_COL32(26, 28, 36, 255));
   dl->AddRectFilled(ImVec2(laneX0, rulerY0), ImVec2(laneX1, rulerY1), IM_COL32(34, 36, 46, 255));
   dl->AddRectFilled(ImVec2(areaMin.x, rulerY0), ImVec2(laneX0, rulerY1), IM_COL32(30, 32, 40, 255));
   // Grid line spacing: beats, or bars when beats get too dense.
   double lineStep = 1.0;
   while (lineStep * ppb < 14.0)
      lineStep *= 2.0;
   const bool showBeats = ppb >= 10.0f;
   dl->PushClipRect(ImVec2(laneX0, rulerY0), ImVec2(laneX1, rowsY1), true);
   {
      // Rows background.
      for (int i = 0; i < (int)gArrange.lanes.size(); i++)
      {
         const auto& ry = gArrangeRowY[(size_t)i];
         if (ry.second < rowsY0 || ry.first > rowsY1)
            continue;
         const bool en = Arrange::LaneEffectivelyEnabled(gArrange, gArrange.lanes[(size_t)i]);
         dl->AddRectFilled(ImVec2(laneX0, ry.first), ImVec2(laneX1, ry.second),
                           (i & 1) ? IM_COL32(30, 32, 41, 255) : IM_COL32(33, 35, 45, 255));
         if (!en)
            DrawArrangeHatch(dl, ImVec2(laneX0, ry.first), ImVec2(laneX1, ry.second), IM_COL32(255, 255, 255, 10));
         dl->AddLine(ImVec2(laneX0, ry.second - 1.0f), ImVec2(laneX1, ry.second - 1.0f), IM_COL32(20, 22, 28, 255));
      }
      // Loop band.
      const Arrange::LoopRange& loop = gArrange.settings.loop;
      if (loop.end > loop.start)
      {
         const float x0 = ArrangeScreenXOfBeat(Arrange::TicksToBeats(loop.start));
         const float x1 = ArrangeScreenXOfBeat(Arrange::TicksToBeats(loop.end));
         dl->AddRectFilled(ImVec2(x0, rulerY0), ImVec2(x1, rulerY0 + 8.0f),
                           loop.enabled ? IM_COL32(80, 130, 230, 230) : IM_COL32(90, 100, 130, 140));
         if (loop.enabled)
            dl->AddRectFilled(ImVec2(x0, rowsY0), ImVec2(x1, rowsY1), IM_COL32(80, 130, 230, 18));
      }
      // Bar / beat lines and ruler labels.
      const double firstBeat = std::floor(scroll / lineStep) * lineStep;
      for (double b = firstBeat; b < scroll + visBeats + lineStep; b += lineStep)
      {
         if (b < 0.0)
            continue;
         const float x = ArrangeScreenXOfBeat(b);
         const double barF = b / bpb;
         const bool isBar = std::fabs(barF - std::round(barF)) < 1e-6;
         if (!isBar && !showBeats)
            continue;
         dl->AddLine(ImVec2(x, isBar ? rulerY0 + 10.0f : rulerY1 - 7.0f), ImVec2(x, rulerY1),
                     isBar ? IM_COL32(170, 175, 190, 255) : IM_COL32(100, 105, 120, 255));
         dl->AddLine(ImVec2(x, rowsY0), ImVec2(x, rowsY1), isBar ? IM_COL32(60, 64, 80, 255) : IM_COL32(42, 45, 57, 255));
         if (isBar)
         {
            const int barNum = (int)std::llround(barF) + 1;
            const int every = std::max(1, (int)std::ceil(48.0 / (ppb * bpb)));
            if ((barNum - 1) % every == 0)
            {
               char lbl[32];
               if (gArrange.settings.timeDisplay == 1)
                  snprintf(lbl, sizeof(lbl), "%s", ArrangeFormatPos(Arrange::BeatsToTicks(b)).c_str());
               else
                  snprintf(lbl, sizeof(lbl), "%d", barNum);
               dl->AddText(ImVec2(x + 3.0f, rulerY0 + 10.0f), IM_COL32(200, 205, 220, 255), lbl);
            }
         }
      }
      // Markers.
      for (const Arrange::Marker& mk : gArrange.markers)
      {
         const float x = ArrangeScreenXOfBeat(Arrange::TicksToBeats(mk.pos));
         if (x < laneX0 - 200.0f || x > laneX1)
            continue;
         const ImU32 col = ArrangeMarkerColU32(mk.color);
         dl->AddTriangleFilled(ImVec2(x, rulerY1 - 2.0f), ImVec2(x - 5.0f, rulerY1 - 11.0f), ImVec2(x + 5.0f, rulerY1 - 11.0f), col);
         dl->AddLine(ImVec2(x, rowsY0), ImVec2(x, rowsY1), ArrangeScaleCol(col, 1.0f, 90));
         dl->AddText(ImVec2(x + 6.0f, rulerY1 - 15.0f), col, mk.name.c_str());
      }
   }
   dl->PopClipRect();

   // ---- headers ----
   bool openHeaderMenu = false;
   static int sHeaderMenuLane = -1;
   ImGui::PushClipRect(ImVec2(areaMin.x, rowsY0), ImVec2(laneX0, rowsY1), true);
   for (int i = 0; i < (int)gArrange.lanes.size(); i++)
   {
      const auto& ry = gArrangeRowY[(size_t)i];
      if (ry.second < rowsY0 || ry.first > rowsY1)
         continue;
      const float indent = 10.0f * (float)(i < (int)gArrangeLaneDepth.size() ? gArrangeLaneDepth[(size_t)i] : 0);
      DrawArrangeLaneHeader(i, ImVec2(areaMin.x + indent, ry.first), kArrangeHeaderW - 2.0f - indent,
                            ry.second - ry.first, openHeaderMenu, sHeaderMenuLane);
      if (i >= (int)gArrange.lanes.size())
         break;
   }
   // Group header rows (folders).
   static uint64_t sGroupMenuId = 0;
   bool openGroupMenu = false;
   for (const ArrangeGroupRow& gr : groupRows)
   {
      if (gr.y1 < rowsY0 || gr.y0 > rowsY1)
         continue;
      const Arrange::TrackGroup* g = Arrange::FindTrackGroup(gArrange, gr.id);
      if (g == nullptr)
         continue;
      const float indent = 10.0f * (float)gr.depth;
      const ImVec2 tl(areaMin.x + indent, gr.y0), br(laneX0 - 2.0f, gr.y1);
      const ImU32 gcol = ArrangeMarkerColU32(g->color);
      dl->AddRectFilled(tl, br, ArrangeScaleCol(gcol, 0.35f));
      dl->AddRectFilled(tl, ImVec2(tl.x + 5.0f, br.y), gcol);
      ImGui::PushID((int)(gr.id * 7 + 3));
      ImGui::SetCursorScreenPos(ImVec2(tl.x + 8.0f, tl.y + 2.0f));
      if (ImGui::ArrowButton("##fold", g->collapsed ? ImGuiDir_Right : ImGuiDir_Down))
      {
         Arrange::SetTrackGroupCollapsed(gArrange, gr.id, !g->collapsed);
         gPatchDirty = true;
         g = Arrange::FindTrackGroup(gArrange, gr.id);
      }
      ImGui::SameLine();
      if (gArrangeRenameGroup == gr.id)
      {
         ImGui::SetNextItemWidth(br.x - ImGui::GetCursorScreenPos().x - 50.0f);
         if (gArrangeRenameJustStarted)
         {
            ImGui::SetKeyboardFocusHere();
            gArrangeRenameJustStarted = false;
         }
         if (ImGui::InputText("##grename", gArrangeRenameBuf, sizeof(gArrangeRenameBuf),
                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
             ImGui::IsItemDeactivated())
         {
            const std::string nm = gArrangeRenameBuf;
            const uint64_t id = gr.id;
            ArrangeEdit([&]() { Arrange::RenameTrackGroup(gArrange, id, nm); });
            gArrangeRenameGroup = 0;
         }
      }
      else if (g != nullptr)
      {
         ImGui::AlignTextToFramePadding();
         const std::string nm = g->name.empty() ? "Group" : g->name;
         ImGui::TextUnformatted(nm.c_str());
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            gArrangeRenameGroup = gr.id;
            gArrangeRenameJustStarted = true;
            snprintf(gArrangeRenameBuf, sizeof(gArrangeRenameBuf), "%s", nm.c_str());
         }
         ImGui::SameLine();
         ImGui::TextDisabled("(%d)", (int)Arrange::LanesInTrackGroupRecursive(gArrange, gr.id).size());
         ImGui::SameLine();
         ImGui::SetCursorScreenPos(ImVec2(br.x - 34.0f, tl.y + 3.0f));
         if (!g->enabled)
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(110, 50, 50, 255));
         if (ImGui::SmallButton(g->enabled ? "ON" : "OFF"))
         {
            const uint64_t id = gr.id;
            ArrangeEdit([&]() { Arrange::SetTrackGroupEnabled(gArrange, id, Arrange::kToggle); });
         }
         if (!g->enabled)
            ImGui::PopStyleColor();
      }
      ImGui::PopID();
      const ImVec2 mp = ImGui::GetIO().MousePos;
      if (mp.x >= tl.x && mp.x < br.x && mp.y >= tl.y && mp.y < br.y && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
      {
         sGroupMenuId = gr.id;
         openGroupMenu = true;
      }
   }
   ImGui::PopClipRect();

   // Group rows in the lane area: a band in the group colour with the
   // member tracks' clips sketched in; clicking it selects them all.
   dl->PushClipRect(ImVec2(laneX0, rowsY0), ImVec2(laneX1, rowsY1), true);
   for (const ArrangeGroupRow& gr : groupRows)
   {
      if (gr.y1 < rowsY0 || gr.y0 > rowsY1)
         continue;
      const Arrange::TrackGroup* g = Arrange::FindTrackGroup(gArrange, gr.id);
      if (g == nullptr)
         continue;
      const ImU32 gcol = ArrangeMarkerColU32(g->color);
      dl->AddRectFilled(ImVec2(laneX0, gr.y0), ImVec2(laneX1, gr.y1), ArrangeScaleCol(gcol, 0.22f));
      for (uint64_t laneId : Arrange::LanesInTrackGroupRecursive(gArrange, gr.id))
         if (const Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
            for (const Arrange::Clip& c : l->clips)
            {
               const float cx0 = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.start));
               const float cx1 = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.End()));
               if (cx1 < laneX0 || cx0 > laneX1)
                  continue;
               dl->AddRectFilled(ImVec2(cx0, gr.y0 + 6.0f), ImVec2(std::max(cx0 + 1.0f, cx1), gr.y1 - 6.0f),
                                 ArrangeScaleCol(gcol, c.enabled ? 0.8f : 0.4f, 200), 2.0f);
            }
      const ImVec2 mp = ImGui::GetIO().MousePos;
      if (mp.x >= laneX0 && mp.x < laneX1 && mp.y >= gr.y0 && mp.y < gr.y1 &&
          ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
      {
         if (!ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl)
            gArrangeSel.clear();
         for (uint64_t laneId : Arrange::LanesInTrackGroupRecursive(gArrange, gr.id))
            if (const Arrange::Lane* l = Arrange::FindLane(gArrange, laneId))
               for (const Arrange::Clip& c : l->clips)
                  if (!ArrangeIsSelected(c.id))
                     gArrangeSel.push_back(c.id);
      }
   }
   dl->PopClipRect();
   if (openGroupMenu)
      ImGui::OpenPopup("##arrgroupmenu");
   if (ImGui::BeginPopup("##arrgroupmenu"))
   {
      const Arrange::TrackGroup* g = Arrange::FindTrackGroup(gArrange, sGroupMenuId);
      if (g == nullptr)
         ImGui::CloseCurrentPopup();
      else
      {
         const uint64_t id = g->id;
         ImGui::TextDisabled("%s", g->name.empty() ? "Group" : g->name.c_str());
         ImGui::Separator();
         if (ImGui::MenuItem("Rename"))
         {
            gArrangeRenameGroup = id;
            gArrangeRenameJustStarted = true;
            snprintf(gArrangeRenameBuf, sizeof(gArrangeRenameBuf), "%s", g->name.c_str());
         }
         if (ImGui::BeginMenu("Color"))
         {
            for (int i = 1; i < 10; i++)
               if (ImGui::MenuItem(kArrangePalette[i].name))
               {
                  const uint32_t rgba = ArrangeMarkerRGBA(kArrangePalette[i].col);
                  ArrangeEdit([&]() { Arrange::RecolorTrackGroup(gArrange, id, rgba); });
               }
            ImGui::EndMenu();
         }
         if (ImGui::MenuItem(g->enabled ? "Disable" : "Enable"))
            ArrangeEdit([&]() { Arrange::SetTrackGroupEnabled(gArrange, id, Arrange::kToggle); });
         if (ImGui::MenuItem(g->collapsed ? "Expand" : "Collapse"))
         {
            Arrange::SetTrackGroupCollapsed(gArrange, id, !g->collapsed);
            gPatchDirty = true;
         }
         auto addInside = [&](int type)
         {
            ArrangeEdit([&]()
            {
               const std::vector<uint64_t> members = Arrange::LanesInTrackGroupRecursive(gArrange, id);
               int at = -1;
               for (uint64_t m : members)
                  at = std::max(at, Arrange::LaneIndex(gArrange, m));
               const uint64_t laneId = Arrange::AddLane(gArrange, type, at >= 0 ? at + 1 : -1);
               Arrange::SetLaneTrackGroup(gArrange, laneId, id);
            });
         };
         if (ImGui::MenuItem("Add video track inside"))
            addInside(Arrange::kLaneVideo);
         if (ImGui::MenuItem("Add audio track inside"))
            addInside(Arrange::kLaneAudio);
         if (ImGui::BeginMenu("Move into"))
         {
            if (ImGui::MenuItem("(top level)", nullptr, g->parentGroupId == 0))
               ArrangeEdit([&]() { Arrange::SetTrackGroupParent(gArrange, id, 0); });
            for (const Arrange::TrackGroup& other : gArrange.trackGroups)
               if (other.id != id)
               {
                  ImGui::PushID((int)other.id);
                  if (ImGui::MenuItem(other.name.empty() ? "Group" : other.name.c_str(), nullptr, g->parentGroupId == other.id))
                  {
                     const uint64_t target = other.id;
                     ArrangeEdit([&]() { Arrange::SetTrackGroupParent(gArrange, id, target); });
                  }
                  ImGui::PopID();
               }
            ImGui::EndMenu();
         }
         if (ImGui::MenuItem("Duplicate group"))
            ArrangeEdit([&]() { Arrange::DuplicateTrackGroup(gArrange, id); });
         ImGui::Separator();
         if (ImGui::MenuItem("Ungroup (keep tracks)"))
            ArrangeEdit([&]() { Arrange::RemoveTrackGroup(gArrange, id, false); });
         if (ImGui::MenuItem("Delete group and its tracks"))
            ArrangeEdit([&]() { Arrange::RemoveTrackGroup(gArrange, id, true); });
      }
      ImGui::EndPopup();
   }

   if (gArrange.lanes.empty())
   {
      dl->AddText(ImVec2(laneX0 + 16.0f, rowsY0 + 16.0f), IM_COL32(150, 155, 170, 255),
                  "Empty timeline.  + VIDEO / + AUDIO add tracks.  Right-click a node on the canvas -> Add to Timeline,");
      dl->AddText(ImVec2(laneX0 + 16.0f, rowsY0 + 34.0f), IM_COL32(150, 155, 170, 255),
                  "or drop audio / video / image files here.  Double-click an empty track spot to add a clip.");
   }

   // ---- clips ----
   struct ClipHit { uint64_t id = 0; int lane = -1; int zone = 0; }; // zone: 0 body, 1 left, 2 right, 3 fadeIn, 4 fadeOut
   ClipHit hit;
   const ImVec2 mouse = io.MousePos;
   const bool mouseInLanes = mouse.x >= laneX0 && mouse.x < laneX1 && mouse.y >= rowsY0 && mouse.y < rowsY1;
   dl->PushClipRect(ImVec2(laneX0, rowsY0), ImVec2(laneX1, rowsY1), true);
   for (int li = 0; li < (int)gArrange.lanes.size(); li++)
   {
      const Arrange::Lane& lane = gArrange.lanes[(size_t)li];
      const auto& ry = gArrangeRowY[(size_t)li];
      if (ry.second < rowsY0 || ry.first > rowsY1)
         continue;
      const bool laneOn = Arrange::LaneEffectivelyEnabled(gArrange, lane);
      for (const Arrange::Clip& c : lane.clips)
      {
         float x0 = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.start));
         float x1 = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.End()));
         if (x1 < laneX0 || x0 > laneX1)
            continue;
         const float y0 = ry.first + 2.0f, y1 = ry.second - 3.0f;
         const bool sel = ArrangeIsSelected(c.id);
         const ImU32 base = ArrangeClipColor(lane, c);
         const bool live = c.enabled && laneOn;
         GraphNode* src = FindNodeByUid(c.srcUid);
         dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), ArrangeScaleCol(base, live ? 0.55f : 0.3f, 255), 3.0f);
         dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + 14.0f), ArrangeScaleCol(base, live ? 0.85f : 0.4f, 255), 3.0f,
                           ImDrawFlags_RoundCornersTop);
         if (!c.enabled)
            DrawArrangeHatch(dl, ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 0, 0, 90));
         // Video: film strip of frames captured while it played (the
         // node's live picture until then). Audio: waveform.
         if (lane.type == Arrange::kLaneVideo && src != nullptr && x1 - x0 > 20.0f && y1 - y0 > 26.0f)
         {
            const float ih = y1 - y0 - 16.0f;
            const ImU32 tint = live ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90);
            auto thumbs = gArrangeThumbs.find(c.id);
            bool drewAny = false;
            if (thumbs != gArrangeThumbs.end())
            {
               const float slotW = (x1 - x0) / (float)kArrangeThumbSlots;
               for (int k = 0; k < kArrangeThumbSlots; k++)
               {
                  const ArrangeThumbSlot& sl = thumbs->second.slot[k];
                  if (sl.fbo.tex == 0 || sl.capturedAt < 0.0)
                     continue;
                  const float iw = std::min(slotW - 2.0f, ih * sl.aspect);
                  if (iw < 6.0f)
                     continue;
                  const float sx = x0 + slotW * (float)k + 1.0f;
                  dl->AddImage((ImTextureID)(intptr_t)sl.fbo.tex, ImVec2(sx, y0 + 15.0f), ImVec2(sx + iw, y1 - 1.0f),
                               ImVec2(0, 1), ImVec2(1, 0), tint);
                  drewAny = true;
               }
            }
            if (!drewAny)
            {
               const unsigned int tex = src->node->GetOutputTexture();
               const int tw = src->node->GetOutputWidth(), th = src->node->GetOutputHeight();
               if (tex != 0 && tw > 0 && th > 0)
               {
                  const float iw = std::min(x1 - x0 - 4.0f, ih * (float)tw / (float)th);
                  dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(x0 + 2.0f, y0 + 15.0f),
                               ImVec2(x0 + 2.0f + iw, y1 - 1.0f), ImVec2(0, 1), ImVec2(1, 0), tint);
               }
            }
         }
         else if (lane.type == Arrange::kLaneAudio && y1 - y0 > 22.0f)
            DrawArrangeClipWave(dl, c, x0, x1, y0 + 15.0f, y1 - 2.0f, x0, laneX0, laneX1,
                                ArrangeScaleCol(base, live ? 1.35f : 0.7f, live ? 220 : 120));
         // Fades.
         if (c.fadeIn > 0)
         {
            const float fx = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.start + c.fadeIn));
            dl->AddTriangleFilled(ImVec2(x0, y0), ImVec2(fx, y0), ImVec2(x0, y1), IM_COL32(0, 0, 0, 70));
            dl->AddLine(ImVec2(x0, y1), ImVec2(fx, y0), IM_COL32(255, 255, 255, 160), 1.5f);
         }
         if (c.fadeOut > 0)
         {
            const float fx = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.End() - c.fadeOut));
            dl->AddTriangleFilled(ImVec2(x1, y0), ImVec2(fx, y0), ImVec2(x1, y1), IM_COL32(0, 0, 0, 70));
            dl->AddLine(ImVec2(fx, y0), ImVec2(x1, y1), IM_COL32(255, 255, 255, 160), 1.5f);
         }
         if (c.groupId != 0)
            dl->AddRectFilled(ImVec2(x0 + 2.0f, y1 - 3.0f), ImVec2(x1 - 2.0f, y1), ArrangeGroupColor(c.groupId));
         dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), sel ? IM_COL32(255, 255, 255, 255) : ArrangeScaleCol(base, 0.35f),
                     3.0f, 0, sel ? 2.0f : 1.0f);
         // Label.
         {
            std::string label = ArrangeClipLabel(c);
            if (c.sampleDropped)
               label = "~ " + label;
            const ImU32 tc = (src == nullptr) ? IM_COL32(255, 120, 110, 255) : IM_COL32(240, 242, 248, 255);
            dl->PushClipRect(ImVec2(std::max(x0, laneX0), y0), ImVec2(std::min(x1, laneX1), y1), true);
            dl->AddText(ImVec2(std::max(x0, laneX0) + 4.0f, y0 + 1.0f), tc, label.c_str());
            dl->PopClipRect();
         }
         // Hit test (topmost wins - clips never overlap within a lane).
         if (mouseInLanes && mouse.y >= y0 && mouse.y < y1 && mouse.x >= x0 - 2.0f && mouse.x < x1 + 2.0f)
         {
            hit.id = c.id;
            hit.lane = li;
            const float fadeInX = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.start + c.fadeIn));
            const float fadeOutX = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.End() - c.fadeOut));
            if (mouse.y < y0 + 9.0f && std::fabs(mouse.x - fadeInX) < 6.0f && x1 - x0 > 24.0f)
               hit.zone = 3;
            else if (mouse.y < y0 + 9.0f && std::fabs(mouse.x - fadeOutX) < 6.0f && x1 - x0 > 24.0f)
               hit.zone = 4;
            else if (mouse.x < x0 + kArrangeEdgePx)
               hit.zone = 1;
            else if (mouse.x > x1 - kArrangeEdgePx)
               hit.zone = 2;
            else
               hit.zone = 0;
            if (sel && (hit.zone == 3 || hit.zone == 4 || mouse.y < y0 + 9.0f))
            {
               // fade handles drawn on the selected clip
               dl->AddRectFilled(ImVec2(fadeInX - 3.0f, y0), ImVec2(fadeInX + 3.0f, y0 + 6.0f), IM_COL32_WHITE);
               dl->AddRectFilled(ImVec2(fadeOutX - 3.0f, y0), ImVec2(fadeOutX + 3.0f, y0 + 6.0f), IM_COL32_WHITE);
            }
         }
      }
   }
   // Marquee.
   if (gArrDrag.mode == kArrDragMarquee)
   {
      const ImVec2 a(std::min(gArrDrag.startMouse.x, mouse.x), std::min(gArrDrag.startMouse.y, mouse.y));
      const ImVec2 b(std::max(gArrDrag.startMouse.x, mouse.x), std::max(gArrDrag.startMouse.y, mouse.y));
      dl->AddRectFilled(a, b, IM_COL32(120, 160, 255, 40));
      dl->AddRect(a, b, IM_COL32(120, 160, 255, 200));
   }
   // Blade guide.
   if (gArrangeBlade && mouseInLanes)
   {
      const float bx = ArrangeScreenXOfBeat(Arrange::TicksToBeats(ArrangeSnapMaybe(tickAtX(mouse.x))));
      dl->AddLine(ImVec2(bx, rowsY0), ImVec2(bx, rowsY1), IM_COL32(255, 90, 90, 220), 1.5f);
   }
   dl->PopClipRect();

   // Playhead.
   {
      const float px = ArrangeScreenXOfBeat(playBeat);
      if (px >= laneX0 && px <= laneX1)
      {
         dl->AddLine(ImVec2(px, rulerY0), ImVec2(px, rowsY1), IM_COL32(255, 80, 70, 255), 1.5f);
         dl->AddTriangleFilled(ImVec2(px - 6.0f, rulerY0), ImVec2(px + 6.0f, rulerY0), ImVec2(px, rulerY0 + 9.0f),
                               IM_COL32(255, 80, 70, 255));
      }
   }

   // Horizontal scrollbar.
   const double totalBeats = std::max(scroll + visBeats, Arrange::TicksToBeats(Arrange::ArrangementEnd(gArrange)) + visBeats * 0.5 + 8.0);
   const float sbY0 = rowsY1 + 2.0f, sbY1 = areaMax.y - 2.0f;
   const float trackW = laneX1 - laneX0;
   const float thumbW = std::max(24.0f, trackW * (float)(visBeats / totalBeats));
   const float thumbX = laneX0 + (trackW - thumbW) * (float)(scroll / std::max(1e-6, totalBeats - visBeats));
   dl->AddRectFilled(ImVec2(laneX0, sbY0), ImVec2(laneX1, sbY1), IM_COL32(22, 24, 30, 255), 3.0f);
   dl->AddRectFilled(ImVec2(thumbX, sbY0), ImVec2(thumbX + thumbW, sbY1),
                     gArrDrag.mode == kArrDragHScroll ? IM_COL32(140, 150, 180, 255) : IM_COL32(90, 96, 120, 255), 3.0f);

   // ---- input capture over ruler + lanes + scrollbar ----
   ImGui::SetCursorScreenPos(ImVec2(laneX0, rulerY0));
   ImGui::InvisibleButton("##arrtimeline", ImVec2(std::max(1.0f, laneX1 - laneX0), std::max(1.0f, areaMax.y - rulerY0)),
                          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
   const bool hovered = ImGui::IsItemHovered();
   const bool inRuler = mouse.y >= rulerY0 && mouse.y < rulerY1;
   const bool inScroll = mouse.y >= sbY0 - 2.0f;

   // Wheel: Ctrl zoom (around the mouse), Shift horizontal, else vertical.
   if (hovered && io.MouseWheel != 0.0f)
   {
      if (io.KeyCtrl)
         ArrangeSetZoomAround(gArrange.settings.zoom * std::pow(1.15f, io.MouseWheel), mouse.x);
      else if (io.KeyShift)
         gArrange.settings.scroll = (float)std::max(0.0, scroll - io.MouseWheel * visBeats * 0.1);
      else
         gArrangeVScroll -= io.MouseWheel * 40.0f;
   }
   if (hovered && io.MouseWheelH != 0.0f)
      gArrange.settings.scroll = (float)std::max(0.0, scroll - io.MouseWheelH * visBeats * 0.1);

   // Cursor feedback.
   if (hovered && gArrDrag.mode == kArrDragNone && !inRuler && !inScroll && hit.id != 0 && !gArrangeBlade)
      if (hit.zone == 1 || hit.zone == 2)
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

   static uint64_t sClipMenuId = 0;
   static int sLaneMenuLane = -1;
   static Arrange::Tick sLaneMenuTick = 0;
   static Arrange::Tick sRulerMenuTick = 0;
   static uint64_t sMarkerMenuId = 0;
   bool openClipMenu = false, openLaneMenu = false, openRulerMenu = false, openMarkerMenu = false;

   // Marker under the mouse in the ruler.
   uint64_t rulerMarker = 0;
   if (inRuler)
      for (const Arrange::Marker& mk : gArrange.markers)
         if (std::fabs(mouse.x - ArrangeScreenXOfBeat(Arrange::TicksToBeats(mk.pos))) < 6.0f && mouse.y > rulerY1 - 14.0f)
            rulerMarker = mk.id;

   // ---- mouse down ----
   if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
   {
      gArrDrag = ArrangeDragState();
      gArrDrag.startMouse = mouse;
      gArrDrag.base = gArrange;
      if (inScroll)
      {
         gArrDrag.mode = kArrDragHScroll;
         gArrDrag.scrollAtStart = gArrange.settings.scroll;
      }
      else if (inRuler)
      {
         if (rulerMarker != 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            gArrangeRenameMarker = rulerMarker;
            if (const Arrange::Marker* mk = [&]() -> const Arrange::Marker* { for (auto& m : gArrange.markers) if (m.id == rulerMarker) return &m; return nullptr; }())
               snprintf(gArrangeRenameBuf, sizeof(gArrangeRenameBuf), "%s", mk->name.c_str());
         }
         else if (rulerMarker != 0)
         {
            gArrDrag.mode = kArrDragMarker;
            gArrDrag.markerId = rulerMarker;
            ArrangeGestureBegin();
         }
         else if (io.KeyShift)
         {
            gArrDrag.mode = kArrDragLoop;
            gArrDrag.loopAnchor = ArrangeSnapMaybe(tickAtX(mouse.x));
         }
         else
         {
            gArrDrag.mode = kArrDragScrub;
            ArrangeSeekTick(tickAtX(mouse.x));
         }
      }
      else if (gArrangeBlade)
      {
         if (hit.id != 0)
         {
            const Arrange::Tick t = ArrangeSnapMaybe(tickAtX(mouse.x));
            std::vector<uint64_t> ids = Arrange::ExpandSelectionToGroups(gArrange, { hit.id });
            ArrangeEdit([&]()
            {
               for (uint64_t id : ids)
               {
                  const Arrange::Clip* c = Arrange::FindClip(gArrange, id);
                  if (c != nullptr && c->start < t && t < c->End())
                     Arrange::Split(gArrange, id, t);
               }
            });
         }
      }
      else if (hit.id != 0)
      {
         gArrangeSelLane = gArrange.lanes[(size_t)hit.lane].id;
         if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            gArrangeSettingsClip = hit.id;
            gArrangeSettingsOpen = true;
         }
         else
         {
            if (io.KeyCtrl)
            {
               if (ArrangeIsSelected(hit.id))
                  gArrangeSel.erase(std::remove(gArrangeSel.begin(), gArrangeSel.end(), hit.id), gArrangeSel.end());
               else
                  gArrangeSel.push_back(hit.id);
            }
            else if (io.KeyShift)
            {
               if (!ArrangeIsSelected(hit.id))
                  gArrangeSel.push_back(hit.id);
            }
            else if (!ArrangeIsSelected(hit.id))
               gArrangeSel = { hit.id };
            const Arrange::Clip* c = Arrange::FindClip(gArrange, hit.id);
            gArrDrag.clipId = hit.id;
            gArrDrag.startLane = hit.lane;
            gArrDrag.lastLane = hit.lane;
            gArrDrag.grabTick = tickAtX(mouse.x);
            gArrDrag.origStart = c ? c->start : 0;
            gArrDrag.ids = ArrangeSelectionIds();
            gArrDrag.base = gArrange;
            switch (hit.zone)
            {
               case 1: gArrDrag.mode = kArrDragTrimL; break;
               case 2: gArrDrag.mode = kArrDragTrimR; break;
               case 3: gArrDrag.mode = kArrDragFadeIn; break;
               case 4: gArrDrag.mode = kArrDragFadeOut; break;
               default: gArrDrag.mode = ArrangeIsSelected(hit.id) ? kArrDragMove : kArrDragNone; break;
            }
            if (gArrDrag.mode != kArrDragNone)
               ArrangeGestureBegin();
         }
      }
      else
      {
         const int lane = laneAtY(mouse.y);
         if (lane >= 0)
            gArrangeSelLane = gArrange.lanes[(size_t)lane].id;
         if (lane >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            // A one-bar empty clip; pick its source from the menu that opens.
            const Arrange::Tick bar = ArrangeTicksPerBar();
            Arrange::Tick at = tickAtX(mouse.x);
            at = grid > 0 ? Arrange::GridFloor(at, grid) : at;
            uint64_t made = 0;
            const uint64_t laneId = gArrange.lanes[(size_t)lane].id;
            ArrangeEdit([&]()
            {
               Arrange::Clip c;
               c.start = at;
               c.length = bar;
               Arrange::PlaceOverwrite(gArrange, laneId, c, &made);
            });
            if (made != 0)
            {
               gArrangeSel = { made };
               sClipMenuId = made;
               openClipMenu = true;
            }
         }
         else
         {
            gArrDrag.mode = kArrDragMarquee;
            gArrDrag.selBefore = io.KeyShift || io.KeyCtrl ? gArrangeSel : std::vector<uint64_t>();
            if (!io.KeyShift && !io.KeyCtrl)
               gArrangeSel.clear();
         }
      }
   }

   // Row resize: drag a lane's bottom border in the header column.
   {
      const bool inHeader = mouse.x >= areaMin.x && mouse.x < laneX0 && mouse.y >= rowsY0 && mouse.y < rowsY1;
      int border = -1;
      if (inHeader && gArrDrag.mode == kArrDragNone)
         for (int i = 0; i < (int)gArrangeRowY.size(); i++)
            if (std::fabs(mouse.y - gArrangeRowY[(size_t)i].second) < 3.0f)
               border = i;
      if (border >= 0)
      {
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         {
            gArrDrag = ArrangeDragState();
            gArrDrag.mode = kArrDragRowResize;
            gArrDrag.startMouse = mouse;
            gArrDrag.laneId = gArrange.lanes[(size_t)border].id;
            gArrDrag.heightAtStart = ArrangeRowHeight(gArrange.lanes[(size_t)border]);
         }
      }
   }

   // ---- drag update ----
   if (gArrDrag.mode != kArrDragNone && ImGui::IsMouseDown(ImGuiMouseButton_Left))
   {
      const float dx = mouse.x - gArrDrag.startMouse.x;
      const float dy = mouse.y - gArrDrag.startMouse.y;
      if (std::fabs(dx) > 2.0f || std::fabs(dy) > 2.0f)
         gArrDrag.moved = true;
      switch (gArrDrag.mode)
      {
         case kArrDragHScroll:
         {
            const double perPx = (totalBeats - visBeats) / std::max(1.0f, trackW - thumbW);
            gArrange.settings.scroll = (float)std::max(0.0, gArrDrag.scrollAtStart + dx * perPx);
            break;
         }
         case kArrDragScrub:
            if (std::fabs(io.MouseDelta.x) > 0.0f)
               ArrangeSeekTick(tickAtX(mouse.x));
            break;
         case kArrDragLoop:
         {
            const Arrange::Tick t = ArrangeSnapMaybe(tickAtX(mouse.x));
            if (t != gArrDrag.loopAnchor)
               ArrangeSetLoop(true, std::min(t, gArrDrag.loopAnchor), std::max(t, gArrDrag.loopAnchor));
            break;
         }
         case kArrDragMarker:
            if (gArrDrag.moved)
            {
               gArrange = gArrDrag.base;
               Arrange::MoveMarker(gArrange, gArrDrag.markerId, ArrangeSnapMaybe(tickAtX(mouse.x)));
            }
            break;
         case kArrDragMove:
            if (gArrDrag.moved)
            {
               const Arrange::Tick raw = tickAtX(mouse.x) - gArrDrag.grabTick;
               const Arrange::Tick newStart = ArrangeSnapMaybe(gArrDrag.origStart + raw);
               const Arrange::Tick dt = newStart - gArrDrag.origStart;
               const int laneNow = laneAtY(mouse.y);
               if (laneNow >= 0)
                  gArrDrag.lastLane = laneNow;
               const int dLane = gArrDrag.lastLane - gArrDrag.startLane;
               gArrange = gArrDrag.base;
               if (!Arrange::MoveClips(gArrange, gArrDrag.ids, dt, dLane))
               {
                  gArrange = gArrDrag.base;
                  Arrange::MoveClips(gArrange, gArrDrag.ids, dt, 0);
               }
            }
            break;
         case kArrDragTrimL:
         case kArrDragTrimR:
            if (gArrDrag.moved)
            {
               gArrange = gArrDrag.base;
               Arrange::TrimEdge(gArrange, gArrDrag.clipId,
                                 gArrDrag.mode == kArrDragTrimL ? Arrange::kEdgeStart : Arrange::kEdgeEnd,
                                 ArrangeSnapMaybe(tickAtX(mouse.x)));
            }
            break;
         case kArrDragFadeIn:
         case kArrDragFadeOut:
         {
            gArrange = gArrDrag.base;
            if (Arrange::Clip* c = Arrange::FindClip(gArrange, gArrDrag.clipId))
            {
               const Arrange::Tick t = ArrangeSnapMaybe(tickAtX(mouse.x));
               if (gArrDrag.mode == kArrDragFadeIn)
                  c->fadeIn = std::clamp<Arrange::Tick>(t - c->start, 0, c->length - c->fadeOut);
               else
                  c->fadeOut = std::clamp<Arrange::Tick>(c->End() - t, 0, c->length - c->fadeIn);
               gArrange.revision++;
            }
            break;
         }
         case kArrDragMarquee:
         {
            const float ax = std::min(gArrDrag.startMouse.x, mouse.x), bx = std::max(gArrDrag.startMouse.x, mouse.x);
            const float ay = std::min(gArrDrag.startMouse.y, mouse.y), by = std::max(gArrDrag.startMouse.y, mouse.y);
            std::vector<uint64_t> sel = gArrDrag.selBefore;
            if (gArrDrag.moved)
               for (int li = 0; li < (int)gArrange.lanes.size(); li++)
               {
                  const auto& ry = gArrangeRowY[(size_t)li];
                  if (ry.second < ay || ry.first > by)
                     continue;
                  for (const Arrange::Clip& c : gArrange.lanes[(size_t)li].clips)
                  {
                     const float x0 = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.start));
                     const float x1 = ArrangeScreenXOfBeat(Arrange::TicksToBeats(c.End()));
                     if (x1 >= ax && x0 <= bx && std::find(sel.begin(), sel.end(), c.id) == sel.end())
                        sel.push_back(c.id);
                  }
               }
            gArrangeSel = sel;
            break;
         }
         case kArrDragRowResize:
            if (Arrange::Lane* l = Arrange::FindLane(gArrange, gArrDrag.laneId))
               l->rowHeight = std::clamp(gArrDrag.heightAtStart + dy, kArrangeRowMinH, 400.0f);
            break;
         default:
            break;
      }
   }
   // ---- release ----
   if (gArrDrag.mode != kArrDragNone && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
   {
      if (gArrDrag.mode == kArrDragRowResize)
         gPatchDirty = true;
      ArrangeGestureEnd();
      gArrDrag = ArrangeDragState();
   }

   // ---- right click ----
   if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
   {
      if (inRuler)
      {
         if (rulerMarker != 0)
         {
            sMarkerMenuId = rulerMarker;
            openMarkerMenu = true;
         }
         else
         {
            sRulerMenuTick = ArrangeSnapMaybe(tickAtX(mouse.x));
            openRulerMenu = true;
         }
      }
      else if (hit.id != 0)
      {
         if (!ArrangeIsSelected(hit.id))
            gArrangeSel = { hit.id };
         sClipMenuId = hit.id;
         openClipMenu = true;
      }
      else if (laneAtY(mouse.y) >= 0)
      {
         sLaneMenuLane = laneAtY(mouse.y);
         sLaneMenuTick = ArrangeSnapMaybe(tickAtX(mouse.x));
         openLaneMenu = true;
      }
   }

   if (openClipMenu)
      ImGui::OpenPopup("##arrclipmenu");
   if (openLaneMenu)
      ImGui::OpenPopup("##arrlanemenu");
   if (openRulerMenu)
      ImGui::OpenPopup("##arrrulermenu");
   if (openMarkerMenu)
      ImGui::OpenPopup("##arrmarkermenu");
   if (openHeaderMenu)
      ImGui::OpenPopup("##arrheadermenu");

   // Clip menu.
   if (ImGui::BeginPopup("##arrclipmenu"))
   {
      const Arrange::Loc loc = Arrange::Find(gArrange, sClipMenuId);
      if (!loc.Valid())
         ImGui::CloseCurrentPopup();
      else
      {
         const Arrange::Lane& lane = gArrange.lanes[(size_t)loc.lane];
         const Arrange::Clip& c = lane.clips[(size_t)loc.index];
         ImGui::TextDisabled("%s", ArrangeClipLabel(c).c_str());
         ImGui::Separator();
         if (!c.sampleDropped && ImGui::BeginMenu("Source"))
         {
            ArrangeSourceMenu(sClipMenuId, lane.type);
            ImGui::EndMenu();
         }
         if (ImGui::MenuItem("Settings...", "double-click"))
         {
            gArrangeSettingsClip = sClipMenuId;
            gArrangeSettingsOpen = true;
         }
         if (ImGui::MenuItem("Split at playhead", "Ctrl+E"))
            ArrangeSplitSelectionAt(playTick);
         if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            ArrangeDuplicateSelection();
         if (ImGui::MenuItem("Copy", "Ctrl+C"))
            ArrangeCopySelection();
         if (ImGui::MenuItem(c.enabled ? "Disable" : "Enable", "0"))
            ArrangeToggleEnabledSelection();
         if (ImGui::MenuItem("Group", "Ctrl+G", false, ArrangeSelectionIds().size() >= 2))
         {
            const std::vector<uint64_t> ids = ArrangeSelectionIds();
            ArrangeEdit([&]() { Arrange::Group(gArrange, ids); });
         }
         if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G", false, c.groupId != 0))
         {
            std::vector<uint64_t> groups;
            for (uint64_t id : ArrangeSelectionIds())
               if (const Arrange::Clip* cc = Arrange::FindClip(gArrange, id))
                  if (cc->groupId != 0 && std::find(groups.begin(), groups.end(), cc->groupId) == groups.end())
                     groups.push_back(cc->groupId);
            ArrangeEdit([&]() { Arrange::Ungroup(gArrange, groups); });
         }
         if (ImGui::BeginMenu("Color"))
         {
            ArrangeGestureBegin();
            for (uint64_t id : ArrangeSelectionIds())
               if (Arrange::Clip* cc = Arrange::FindClip(gArrange, id))
               {
                  ImGui::PushID((int)id);
                  float r = cc->colorR, g = cc->colorG, b = cc->colorB;
                  ArrangeColorMenu(r, g, b, true);
                  ImGui::PopID();
                  if (r != cc->colorR || g != cc->colorG || b != cc->colorB)
                  {
                     for (uint64_t id2 : ArrangeSelectionIds())
                        if (Arrange::Clip* c2 = Arrange::FindClip(gArrange, id2))
                        {
                           c2->colorR = r;
                           c2->colorG = g;
                           c2->colorB = b;
                        }
                     gArrange.revision++;
                  }
                  break; // one palette for the whole selection
               }
            ArrangeGestureEnd();
            ImGui::EndMenu();
         }
         ImGui::Separator();
         if (ImGui::MenuItem("Delete", "Del"))
            ArrangeDeleteSelection();
      }
      ImGui::EndPopup();
   }

   // Empty-lane menu.
   if (ImGui::BeginPopup("##arrlanemenu"))
   {
      if (sLaneMenuLane < 0 || sLaneMenuLane >= (int)gArrange.lanes.size())
         ImGui::CloseCurrentPopup();
      else
      {
         const Arrange::Lane& lane = gArrange.lanes[(size_t)sLaneMenuLane];
         ImGui::TextDisabled("%s at %s", ArrangeLaneLabel(gArrange, sLaneMenuLane).c_str(),
                             ArrangeFormatPos(sLaneMenuTick).c_str());
         ImGui::Separator();
         if (ImGui::BeginMenu("Add clip of"))
         {
            for (GraphNode& gn : gNodes)
            {
               if (!ArrangeNodeFitsLane(gn, lane.type))
                  continue;
               ImGui::PushID(gn.index);
               if (ImGui::MenuItem(NodeTitleWithInstance(gn).c_str()))
               {
                  const uint64_t laneId = lane.id;
                  const uint64_t uid = gn.uid;
                  const int out = (lane.type == Arrange::kLaneAudio && dynamic_cast<VideoSourceNode*>(gn.node.get())) ? 1 : 0;
                  uint64_t made = 0;
                  ArrangeEdit([&]()
                  {
                     Arrange::Clip c;
                     c.start = sLaneMenuTick;
                     c.length = ArrangeTicksPerBar();
                     c.srcUid = uid;
                     c.srcOutput = out;
                     Arrange::PlaceOverwrite(gArrange, laneId, c, &made);
                  });
                  if (made)
                     gArrangeSel = { made };
               }
               ImGui::PopID();
            }
            ImGui::EndMenu();
         }
         if (ImGui::MenuItem("Paste here", "Ctrl+V", false, !gArrangeClipboard.empty()))
            ArrangePasteAt(sLaneMenuLane, sLaneMenuTick);
         if (ImGui::MenuItem("Add marker here"))
            ArrangeAddMarkerAt(sLaneMenuTick);
      }
      ImGui::EndPopup();
   }

   // Ruler menu.
   if (ImGui::BeginPopup("##arrrulermenu"))
   {
      ImGui::TextDisabled("%s", ArrangeFormatPos(sRulerMenuTick).c_str());
      ImGui::Separator();
      if (ImGui::MenuItem("Add marker here", "M at playhead"))
         ArrangeAddMarkerAt(sRulerMenuTick);
      const Arrange::LoopRange l = gArrange.settings.loop;
      if (ImGui::MenuItem("Loop starts here"))
         ArrangeSetLoop(true, sRulerMenuTick, std::max(l.end, sRulerMenuTick + ArrangeTicksPerBar()));
      if (ImGui::MenuItem("Loop ends here", nullptr, false, sRulerMenuTick > l.start))
         ArrangeSetLoop(true, l.start, sRulerMenuTick);
      if (ImGui::MenuItem("Loop on", nullptr, l.enabled, l.end > l.start))
         ArrangeSetLoop(!l.enabled, l.start, l.end);
      if (ImGui::MenuItem("Clear loop", nullptr, false, l.end > l.start))
         ArrangeSetLoop(false, 0, 0);
      if (ImGui::MenuItem("Play from here"))
      {
         ArrangeSeekTick(sRulerMenuTick);
         Transport::Instance().SetPlaying(true);
      }
      ImGui::EndPopup();
   }

   // Marker menu.
   if (ImGui::BeginPopup("##arrmarkermenu"))
   {
      Arrange::Marker* mk = nullptr;
      for (Arrange::Marker& m : gArrange.markers)
         if (m.id == sMarkerMenuId)
            mk = &m;
      if (mk == nullptr)
         ImGui::CloseCurrentPopup();
      else
      {
         ImGui::TextDisabled("%s", mk->name.c_str());
         ImGui::Separator();
         if (ImGui::MenuItem("Jump here"))
            ArrangeSeekTick(mk->pos);
         if (ImGui::MenuItem("Rename..."))
         {
            gArrangeRenameMarker = mk->id;
            snprintf(gArrangeRenameBuf, sizeof(gArrangeRenameBuf), "%s", mk->name.c_str());
         }
         if (ImGui::BeginMenu("Color"))
         {
            for (int i = 1; i < 10; i++)
               if (ImGui::MenuItem(kArrangePalette[i].name))
               {
                  const uint64_t id = mk->id;
                  const uint32_t rgba = ArrangeMarkerRGBA(kArrangePalette[i].col);
                  ArrangeEdit([&]() { Arrange::RecolorMarker(gArrange, id, rgba); });
               }
            ImGui::EndMenu();
         }
         if (ImGui::MenuItem("Delete"))
         {
            const uint64_t id = mk->id;
            ArrangeEdit([&]() { Arrange::DeleteMarker(gArrange, id); });
         }
      }
      ImGui::EndPopup();
   }

   // Marker rename (small popup at the mouse).
   if (gArrangeRenameMarker != 0)
   {
      ImGui::OpenPopup("##arrmarkerrename");
      if (ImGui::BeginPopup("##arrmarkerrename"))
      {
         ImGui::SetKeyboardFocusHere();
         if (ImGui::InputText("marker", gArrangeRenameBuf, sizeof(gArrangeRenameBuf),
                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
         {
            const uint64_t id = gArrangeRenameMarker;
            const std::string nm = gArrangeRenameBuf;
            ArrangeEdit([&]() { Arrange::RenameMarker(gArrange, id, nm); });
            gArrangeRenameMarker = 0;
            ImGui::CloseCurrentPopup();
         }
         if (ImGui::IsKeyPressed(ImGuiKey_Escape))
         {
            gArrangeRenameMarker = 0;
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }
      else
         gArrangeRenameMarker = 0;
   }

   // Track header menu.
   if (ImGui::BeginPopup("##arrheadermenu"))
   {
      const int li = sHeaderMenuLane;
      if (li < 0 || li >= (int)gArrange.lanes.size())
         ImGui::CloseCurrentPopup();
      else
      {
         const uint64_t id = gArrange.lanes[(size_t)li].id;
         const int type = gArrange.lanes[(size_t)li].type;
         ImGui::TextDisabled("%s", ArrangeLaneLabel(gArrange, li).c_str());
         ImGui::Separator();
         if (ImGui::MenuItem("Rename"))
         {
            gArrangeRenameLane = id;
            gArrangeRenameJustStarted = true;
            snprintf(gArrangeRenameBuf, sizeof(gArrangeRenameBuf), "%s", ArrangeLaneLabel(gArrange, li).c_str());
         }
         if (ImGui::BeginMenu("Color"))
         {
            Arrange::Lane* l = Arrange::FindLane(gArrange, id);
            float r = l->colorR, g = l->colorG, b = l->colorB;
            const uint64_t revBefore = gArrange.revision;
            ArrangeColorMenu(r, g, b, true);
            if (gArrange.revision != revBefore)
            {
               gArrange.revision = revBefore;
               ArrangeEdit([&]()
               {
                  if (Arrange::Lane* ll = Arrange::FindLane(gArrange, id))
                  {
                     ll->colorR = r;
                     ll->colorG = g;
                     ll->colorB = b;
                     gArrange.revision++;
                  }
               });
            }
            ImGui::EndMenu();
         }
         if (ImGui::MenuItem("Add track below"))
         {
            const uint64_t gid = gArrange.lanes[(size_t)li].groupId;
            ArrangeEdit([&]()
            {
               const uint64_t nl = Arrange::AddLane(gArrange, type, li + 1);
               if (gid != 0)
                  Arrange::SetLaneTrackGroup(gArrange, nl, gid);
            });
         }
         if (ImGui::BeginMenu("Group"))
         {
            const uint64_t cur = gArrange.lanes[(size_t)li].groupId;
            if (ImGui::MenuItem("New group with this track"))
               ArrangeEdit([&]() { Arrange::GroupSelectedLanes(gArrange, { id }, cur); });
            if (li + 1 < (int)gArrange.lanes.size() && ImGui::MenuItem("New group with this and the track below"))
            {
               const uint64_t below = gArrange.lanes[(size_t)li + 1].id;
               ArrangeEdit([&]() { Arrange::GroupSelectedLanes(gArrange, { id, below }, cur); });
            }
            if (!gArrange.trackGroups.empty())
            {
               ImGui::Separator();
               if (ImGui::MenuItem("(no group)", nullptr, cur == 0))
                  ArrangeEdit([&]() { Arrange::SetLaneTrackGroup(gArrange, id, 0); });
               for (const Arrange::TrackGroup& g : gArrange.trackGroups)
               {
                  ImGui::PushID((int)g.id);
                  if (ImGui::MenuItem(g.name.empty() ? "Group" : g.name.c_str(), nullptr, cur == g.id))
                  {
                     const uint64_t gid = g.id;
                     ArrangeEdit([&]() { Arrange::SetLaneTrackGroup(gArrange, id, gid); });
                  }
                  ImGui::PopID();
               }
            }
            ImGui::EndMenu();
         }
         if (ImGui::MenuItem("Duplicate track"))
            ArrangeEdit([&]() { Arrange::DuplicateLane(gArrange, id); });
         if (ImGui::MenuItem("Move up", nullptr, false, li > 0))
            ArrangeEdit([&]() { Arrange::ReorderLane(gArrange, id, li - 1); });
         if (ImGui::MenuItem("Move down", nullptr, false, li + 1 < (int)gArrange.lanes.size()))
            ArrangeEdit([&]() { Arrange::ReorderLane(gArrange, id, li + 1); });
         if (ImGui::BeginMenu("Height"))
         {
            const float heights[4] = { 34.0f, 58.0f, 90.0f, 140.0f };
            const char* names[4] = { "small", "normal", "large", "huge" };
            for (int i = 0; i < 4; i++)
               if (ImGui::MenuItem(names[i]))
               {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, id))
                     l->rowHeight = heights[i];
                  gPatchDirty = true;
               }
            ImGui::EndMenu();
         }
         ImGui::Separator();
         if (ImGui::MenuItem("Delete track"))
            ArrangeEdit([&]() { Arrange::RemoveLane(gArrange, id); });
      }
      ImGui::EndPopup();
   }
}

// Keys while the timeline has focus (upstream's set).
void ArrangeHandleKeys()
{
   ImGuiIO& io = ImGui::GetIO();
   if (!gArrangeKeysOwned || io.WantTextInput)
      return;
   const bool ctrl = io.KeyCtrl || io.KeySuper;
   auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
   if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace))
      ArrangeDeleteSelection();
   if ((ctrl && !io.KeyShift && pressed(ImGuiKey_D)) || (!ctrl && io.KeyShift && pressed(ImGuiKey_D)))
      ArrangeDuplicateSelection();
   if (ctrl && pressed(ImGuiKey_C))
      ArrangeCopySelection();
   if (ctrl && pressed(ImGuiKey_V))
   {
      int lane = Arrange::LaneIndex(gArrange, gArrangeSelLane);
      if (lane < 0 && !gArrange.lanes.empty())
         lane = 0;
      ArrangePasteAt(lane, ArrangeSnapMaybe(ArrangePlayTick()));
   }
   if (ctrl && pressed(ImGuiKey_E))
      ArrangeSplitSelectionAt(ArrangePlayTick());
   if (ctrl && pressed(ImGuiKey_A))
   {
      gArrangeSel.clear();
      for (const Arrange::Lane& lane : gArrange.lanes)
         for (const Arrange::Clip& c : lane.clips)
            gArrangeSel.push_back(c.id);
   }
   if (ctrl && !io.KeyShift && pressed(ImGuiKey_G))
   {
      const std::vector<uint64_t> ids = ArrangeSelectionIds();
      ArrangeEdit([&]() { Arrange::Group(gArrange, ids); });
   }
   if (ctrl && io.KeyShift && pressed(ImGuiKey_G))
   {
      std::vector<uint64_t> groups;
      for (uint64_t id : ArrangeSelectionIds())
         if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
            if (c->groupId != 0 && std::find(groups.begin(), groups.end(), c->groupId) == groups.end())
               groups.push_back(c->groupId);
      ArrangeEdit([&]() { Arrange::Ungroup(gArrange, groups); });
   }
   if (ctrl && pressed(ImGuiKey_R) && gArrangeSel.size() == 1)
   {
      gArrangeSettingsClip = gArrangeSel[0];
      gArrangeSettingsOpen = true;
   }
   if (!ctrl && (pressed(ImGuiKey_0) || pressed(ImGuiKey_Keypad0)))
      ArrangeToggleEnabledSelection();
   if (!ctrl && !io.KeyShift && pressed(ImGuiKey_B))
      gArrangeBlade = !gArrangeBlade;
   if (pressed(ImGuiKey_Escape))
   {
      if (gArrangeBlade)
         gArrangeBlade = false;
      else
         gArrangeSel.clear();
   }
   if (!ctrl && !io.KeyShift && !io.KeyAlt && pressed(ImGuiKey_M))
      ArrangeAddMarkerAt(ArrangePlayTick());
   if (io.KeyAlt && pressed(ImGuiKey_LeftArrow))
      ArrangeJumpToMarker(-1);
   else if (io.KeyAlt && pressed(ImGuiKey_RightArrow))
      ArrangeJumpToMarker(1);
   else if (!ctrl && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)))
   {
      const int dir = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? -1 : 1;
      if (!gArrangeSel.empty())
         ArrangeNudgeSelection(dir);
      else
         ArrangeSeekTick(std::max<Arrange::Tick>(0, ArrangePlayTick() + dir * ArrangeNudgeStepTicks()));
   }
   if (pressed(ImGuiKey_Home))
      ArrangeSeekTick(0);
   if (pressed(ImGuiKey_End))
      ArrangeSeekTick(Arrange::ArrangementEnd(gArrange));
   if (!ctrl && io.KeyShift && pressed(ImGuiKey_J))
      ArrangeAddTrack(Arrange::kLaneVideo, Arrange::LaneIndex(gArrange, gArrangeSelLane) + 1);
   if (!ctrl && io.KeyShift && pressed(ImGuiKey_K))
      ArrangeAddTrack(Arrange::kLaneAudio, Arrange::LaneIndex(gArrange, gArrangeSelLane) + 1);
}

// The whole docked panel: resize grip, toolbar, timeline and the monitor.
void DrawArrangePanelDocked(const ImVec2& size)
{
   const float kGrip = 6.0f;
   ImGui::BeginChild("##arrangepanel", size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
   gArrangePanelMin = ImGui::GetWindowPos();
   gArrangePanelMax = ImVec2(gArrangePanelMin.x + ImGui::GetWindowSize().x, gArrangePanelMin.y + ImGui::GetWindowSize().y);
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   ImGui::InvisibleButton("##arrgrip", ImVec2(std::max(1.0f, inner.x), kGrip));
   if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
   if (ImGui::IsItemActive())
      gArrangePanelHeight = std::max(140.0f, gArrangePanelHeight - ImGui::GetIO().MouseDelta.y);
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 g0 = ImGui::GetItemRectMin(), g1 = ImGui::GetItemRectMax();
      dl->AddLine(ImVec2(g0.x, g0.y + 2.0f), ImVec2(g1.x, g0.y + 2.0f), IM_COL32(70, 74, 90, 255));
   }

   DrawArrangeToolbar(inner.x);

   const ImVec2 bodyMin = ImGui::GetCursorScreenPos();
   const ImVec2 avail = ImGui::GetContentRegionAvail();
   const float bodyH = std::max(40.0f, avail.y);
   float monW = 0.0f;
   if (gArrangeMonitor)
   {
      const float aspect = (float)std::max(16, gArrange.settings.renderWidth) / (float)std::max(16, gArrange.settings.renderHeight);
      monW = std::min(avail.x * 0.4f, (bodyH - 22.0f) * aspect);
   }
   const ImVec2 tlMin = bodyMin;
   const ImVec2 tlMax(bodyMin.x + avail.x - (monW > 0.0f ? monW + 8.0f : 0.0f), bodyMin.y + bodyH);
   DrawArrangeTimeline(tlMin, tlMax);

   if (monW > 0.0f)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 m0(tlMax.x + 8.0f, bodyMin.y);
      const float monH = monW * (float)gArrange.settings.renderHeight / (float)std::max(16, gArrange.settings.renderWidth);
      const ImVec2 m1(m0.x + monW, m0.y + monH);
      dl->AddRectFilled(m0, m1, IM_COL32(0, 0, 0, 255));
      gArrangeMonitorRequested = true;
      const unsigned int tex = ArrangeMonitorTexture();
      if (tex != 0)
         dl->AddImage((ImTextureID)(intptr_t)tex, m0, m1, ImVec2(0, 1), ImVec2(1, 0));
      dl->AddRect(m0, m1, IM_COL32(70, 74, 90, 255));
      ImGui::SetCursorScreenPos(ImVec2(m0.x, m1.y + 3.0f));
      ImGui::SetNextItemWidth(monW * 0.5f);
      int wh[2] = { gArrange.settings.renderWidth, gArrange.settings.renderHeight };
      if (ImGui::InputInt2("##arrres", wh, ImGuiInputTextFlags_EnterReturnsTrue))
      {
         gArrange.settings.renderWidth = std::clamp(wh[0], 16, 8192);
         gArrange.settings.renderHeight = std::clamp(wh[1], 16, 8192);
         gPatchDirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("monitor size (Enter to apply)");
      ImGui::SameLine();
      int activeLayers = 0;
      {
         static std::vector<ArrangeVideoLayer> sLayers;
         CollectArrangeVideoLayers(Transport::Instance().Beats(), sLayers);
         activeLayers = (int)sLayers.size();
      }
      ImGui::TextDisabled("%d layer%s", activeLayers, activeLayers == 1 ? "" : "s");
   }

   gArrangeKeysOwned = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
   ArrangeHandleKeys();
   ImGui::EndChild();
}

// ---- render / export queue (Turbo 0.43.1) ---------------------------------------
//
// Renders the timeline offline: one movie frame per main-loop iteration,
// with the audio graph driven by the main thread for exactly that frame's
// samples (the device callback outputs silence meanwhile), so picture and
// sound are frame-accurate whatever the machine's speed. Video: the video
// tracks' composite at the render size, muxed with the timeline's audio
// (mp4 / mov). Audio only: a 32-bit float WAV of the timeline's audio.

struct ArrangeRenderJob
{
   std::string path;
   int format = 0; // 0 mp4, 1 mov, 2 wav
   Arrange::Tick start = 0, end = 0;
   int width = 1920, height = 1080, fps = 30;
   int status = 0; // 0 queued, 1 running, 2 done, 3 failed, 4 cancelled
   std::string message;
   int frame = 0, totalFrames = 0;
};
std::vector<ArrangeRenderJob> gArrangeRenderJobs;

struct ArrangeRenderState
{
   int job = -1;
   bool warm = false; // first iteration after Begin: let the graph cook at the start
   Platform::RecorderHandle* recorder = nullptr;
   FILE* wav = nullptr;
   int64_t wavFrames = 0;
   double sampleRate = 48000.0;
   int64_t samplesDone = 0;
   // restored afterwards
   bool wasPlaying = false;
   double wasBeat = 0.0;
   bool wasTimelineMode = false;
   bool wasVsync = true;
   std::vector<float> scratchL, scratchR, interleaved;
};
ArrangeRenderState gArrangeRender;

bool ArrangeRenderBusy() { return gArrangeRender.job >= 0; }

void ArrangeWavHeader(FILE* f, double sr, int64_t frames)
{
   auto u32 = [f](uint32_t v) { fwrite(&v, 4, 1, f); };
   auto u16 = [f](uint16_t v) { fwrite(&v, 2, 1, f); };
   const uint32_t dataBytes = (uint32_t)std::min<int64_t>(frames * 8, 0xFFFFFFF0ll);
   fseek(f, 0, SEEK_SET);
   fwrite("RIFF", 1, 4, f); u32(4 + 26 + 12 + 8 + dataBytes);
   fwrite("WAVEfmt ", 1, 8, f); u32(18); u16(3); u16(2); u32((uint32_t)std::lround(sr));
   u32((uint32_t)std::lround(sr) * 8); u16(8); u16(32); u16(0);
   fwrite("fact", 1, 4, f); u32(4); u32((uint32_t)frames);
   fwrite("data", 1, 4, f); u32(dataBytes);
}

FILE* ArrangeOpenFile(const std::string& path)
{
#ifdef _WIN32
   return _wfopen(std::filesystem::u8path(path).wstring().c_str(), L"wb");
#else
   return fopen(path.c_str(), "wb");
#endif
}

void ArrangeRenderFinish(int status, const std::string& message)
{
   ArrangeRenderState& r = gArrangeRender;
   if (r.job < 0)
      return;
   ArrangeRenderJob& job = gArrangeRenderJobs[(size_t)r.job];
   std::string err;
   if (r.recorder != nullptr)
   {
      if (!Platform::RecorderStop(r.recorder, err) && status == 2)
      {
         status = 3;
         job.message = err.empty() ? "encoder failed" : err;
      }
      r.recorder = nullptr;
   }
   if (r.wav != nullptr)
   {
      ArrangeWavHeader(r.wav, r.sampleRate, r.wavFrames);
      fclose(r.wav);
      r.wav = nullptr;
   }
   job.status = status;
   if (!message.empty())
      job.message = message;
   else if (status == 2 && job.message.empty())
      job.message = "done";
   // Back to live.
   AudioEngine::Instance().SetOfflineRender(false);
   Transport::Instance().SetPlaying(false);
   Transport::Instance().SeekBeats(r.wasBeat);
   gArrangeTimelineMode = r.wasTimelineMode;
   PublishArrangeLoop();
   gVsync = r.wasVsync;
   RuntimeLog::Write("timeline render %s: %s (%s)", status == 2 ? "done" : "stopped", job.path.c_str(), job.message.c_str());
   r = ArrangeRenderState();
}

bool ArrangeRenderBegin(int jobIndex)
{
   ArrangeRenderJob& job = gArrangeRenderJobs[(size_t)jobIndex];
   const double sr = AudioEngine::Instance().SampleRate();
   if (sr <= 0.0)
   {
      job.status = 3;
      job.message = "start the audio engine first (AUDIO button)";
      return false;
   }
   if (job.end <= job.start)
   {
      job.status = 3;
      job.message = "empty range";
      return false;
   }
   ArrangeRenderState& r = gArrangeRender;
   r = ArrangeRenderState();
   r.sampleRate = sr;
   const double seconds = Arrange::TicksToSeconds(job.end - job.start, std::max(1.0f, Transport::Instance().Tempo()));
   job.totalFrames = std::max(1, (int)std::ceil(seconds * job.fps));
   job.frame = 0;
   std::string err;
   if (job.format == 2)
   {
      r.wav = ArrangeOpenFile(job.path);
      if (r.wav == nullptr)
      {
         job.status = 3;
         job.message = "cannot write " + job.path;
         return false;
      }
      ArrangeWavHeader(r.wav, sr, 0);
   }
   else
   {
      r.recorder = Platform::RecorderStart(job.path, job.width, job.height, job.fps, err, std::string(), false, sr, 2);
      if (r.recorder == nullptr)
      {
         job.status = 3;
         job.message = err.empty() ? "could not start the encoder" : err;
         return false;
      }
   }
   r.job = jobIndex;
   r.warm = true;
   r.wasPlaying = Transport::Instance().IsPlaying();
   r.wasBeat = Transport::Instance().Beats();
   r.wasTimelineMode = gArrangeTimelineMode;
   r.wasVsync = gVsync;
   job.status = 1;
   job.message.clear();

   // Timeline owns the output, no loop, playhead at the start, graph driven
   // from here.
   gArrangeTimelineMode = true;
   Transport::Instance().SetLoop(false, 0.0, 0.0);
   AudioEngine::Instance().SetOfflineRender(true);
   Transport::Instance().SetPlaying(true);
   Transport::Instance().SeekBeats(Arrange::TicksToBeats(job.start));
   Transport::Instance().AdvanceAudioClock(0); // lands the seek now
   ArrangeFrameUpdate();                       // timeline-mode topology + video locks
   gVsync = false;
   RuntimeLog::Write("timeline render start: %s %dx%d %dfps", job.path.c_str(), job.width, job.height, job.fps);
   return true;
}

// Audio for one movie frame: exactly the samples between this frame and the
// next, run through the whole graph.
void ArrangeRenderAudioFrame(ArrangeRenderJob& job)
{
   ArrangeRenderState& r = gArrangeRender;
   const int64_t target = (int64_t)std::llround((double)(job.frame + 1) * r.sampleRate / (double)job.fps);
   while (r.samplesDone < target)
   {
      // Never bigger than the block the graph was prepared for.
      int block = (int)Platform::AudioDeviceBufferFrames(gAudioOutputDeviceId);
      if (block <= 0)
         block = gAudioBufferFrames;
      block = std::clamp(block, 32, 512);
      const int n = (int)std::min<int64_t>(block, target - r.samplesDone);
      r.scratchL.assign((size_t)n, 0.0f);
      r.scratchR.assign((size_t)n, 0.0f);
      float* chans[2] = { r.scratchL.data(), r.scratchR.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = n;
      AudioEngine::Instance().RenderOfflineBlock(buf);
      r.interleaved.resize((size_t)n * 2);
      for (int i = 0; i < n; i++)
      {
         r.interleaved[(size_t)i * 2] = r.scratchL[(size_t)i];
         r.interleaved[(size_t)i * 2 + 1] = r.scratchR[(size_t)i];
      }
      if (r.recorder != nullptr)
         Platform::RecorderAppendAudio(r.recorder, r.interleaved.data(), n);
      if (r.wav != nullptr)
      {
         fwrite(r.interleaved.data(), sizeof(float), (size_t)n * 2, r.wav);
         r.wavFrames += n;
      }
      r.samplesDone += n;
   }
}

// Once per main-loop iteration, after the cook.
void ArrangeRenderPump(int frameId)
{
   ArrangeRenderState& r = gArrangeRender;
   if (r.job < 0)
   {
      for (int i = 0; i < (int)gArrangeRenderJobs.size(); i++)
         if (gArrangeRenderJobs[(size_t)i].status == 0)
         {
            ArrangeRenderBegin(i);
            break;
         }
      return;
   }
   ArrangeRenderJob& job = gArrangeRenderJobs[(size_t)r.job];
   if (job.status == 4)
   {
      ArrangeRenderFinish(4, "cancelled");
      return;
   }
   Transport::Instance().SetPlaying(true); // a stray Space must not pause a take
   if (r.warm)
   {
      r.warm = false; // this iteration cooked the graph at the start position
      return;
   }
   if (job.format == 2)
   {
      // Audio only: no picture to wait for, so several frames per iteration
      // (modulation still updates every iteration).
      for (int k = 0; k < 8 && job.frame < job.totalFrames; k++)
      {
         ArrangeRenderAudioFrame(job);
         job.frame++;
      }
   }
   else
   {
      const unsigned int tex = ArrangeComposite(frameId, job.width, job.height);
      (void)tex;
      auto it = gArrangeTargets.find({ job.width, job.height });
      if (it != gArrangeTargets.end() && it->second.result.fbo != 0)
      {
         // Back-pressure: never drop a frame, wait for the encoder.
         for (int i = 0; i < 400 && Platform::RecorderPendingFrameCount(r.recorder) > 24; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
         std::vector<unsigned char> pixels = Platform::RecorderAcquireFrameBuffer(r.recorder);
         pixels.resize((size_t)job.width * (size_t)job.height * 4);
         GLint prevRead = 0;
         glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
         glBindFramebuffer(GL_READ_FRAMEBUFFER, it->second.result.fbo);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, job.width, job.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
         glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
         if (!Platform::RecorderAppend(r.recorder, std::move(pixels)))
         {
            ArrangeRenderFinish(3, "encoder rejected a frame");
            return;
         }
      }
      ArrangeRenderAudioFrame(job);
      job.frame++;
   }
   if (job.frame >= job.totalFrames)
      ArrangeRenderFinish(2, std::string());
}

// The render dialog + queue.
void DrawArrangeRenderWindow()
{
   if (!gArrangeRenderWindowOpen && !ArrangeRenderBusy())
      return;
   if (ArrangeRenderBusy())
      gArrangeRenderWindowOpen = true;
   ImGui::SetNextWindowSize(ImVec2(470, 0), ImGuiCond_FirstUseEver);
   if (!ImGui::Begin("Render timeline", &gArrangeRenderWindowOpen, ImGuiWindowFlags_AlwaysAutoResize))
   {
      ImGui::End();
      return;
   }
   Arrange::Settings& st = gArrange.settings;
   static const char* kRanges[] = { "Whole arrangement", "Loop range", "Selected clips", "Custom (bars)" };
   ImGui::SetNextItemWidth(200.0f);
   ImGui::Combo("range", &st.renderRangeKind, kRanges, 4);
   Arrange::Tick a = 0, b = Arrange::ArrangementEnd(gArrange);
   if (st.renderRangeKind == 1)
   {
      a = st.loop.start;
      b = st.loop.end;
   }
   else if (st.renderRangeKind == 2)
   {
      a = Arrange::kMaxTick;
      b = 0;
      for (uint64_t id : ArrangeSelectionIds())
         if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
         {
            a = std::min(a, c->start);
            b = std::max(b, c->End());
         }
      if (b <= a)
         a = b = 0;
   }
   else if (st.renderRangeKind == 3)
   {
      const double bpb = std::max(0.25, Transport::Instance().BeatsPerBar());
      float bars[2] = { (float)(Arrange::TicksToBeats(st.renderRangeStart) / bpb) + 1.0f,
                        (float)(Arrange::TicksToBeats(st.renderRangeEnd) / bpb) + 1.0f };
      ImGui::SetNextItemWidth(200.0f);
      if (ImGui::DragFloat2("from / to bar", bars, 0.05f, 1.0f, 10000.0f, "%.2f"))
      {
         st.renderRangeStart = Arrange::BeatsToTicks((std::max(1.0f, bars[0]) - 1.0f) * bpb);
         st.renderRangeEnd = Arrange::BeatsToTicks((std::max(1.0f, bars[1]) - 1.0f) * bpb);
         gPatchDirty = true;
      }
      a = st.renderRangeStart;
      b = st.renderRangeEnd;
   }
   ImGui::TextDisabled("%s  to  %s   (%s)", ArrangeFormatPos(a).c_str(), ArrangeFormatPos(b).c_str(),
                       ArrangeFormatLen(std::max<Arrange::Tick>(0, b - a)).c_str());

   static const char* kFormats[] = { "MP4 (video + audio)", "MOV (video + audio)", "WAV (audio only)" };
   ImGui::SetNextItemWidth(200.0f);
   ImGui::Combo("format", &st.renderFormat, kFormats, 3);
   if (st.renderFormat != 2)
   {
      int wh[2] = { st.renderWidth, st.renderHeight };
      ImGui::SetNextItemWidth(200.0f);
      if (ImGui::InputInt2("size", wh))
      {
         st.renderWidth = std::clamp(wh[0] & ~1, 16, 8192);
         st.renderHeight = std::clamp(wh[1] & ~1, 16, 8192);
      }
      ImGui::SetNextItemWidth(200.0f);
      ImGui::SliderInt("fps", &st.renderFps, 1, 120);
   }
   const bool canQueue = b > a;
   if (!canQueue)
      ImGui::BeginDisabled();
   if (ImGui::Button("Add to queue...", ImVec2(160, 0)))
   {
      ArrangeRenderJob job;
      job.format = st.renderFormat;
      job.start = a;
      job.end = b;
      job.width = st.renderWidth;
      job.height = st.renderHeight;
      job.fps = std::clamp(st.renderFps, 1, 120);
      const std::string base = gPatchPath.empty() ? "timeline" : gPatchPath.substr(gPatchPath.find_last_of("/\\") + 1);
      const std::string stem = base.substr(0, base.find_last_of('.'));
      if (job.format == 2)
         StartFileDialog([stem]() { return Platform::SaveAudioDialog(stem + ".wav"); },
                         [job](const std::string& path) mutable { job.path = path; gArrangeRenderJobs.push_back(job); });
      else
         StartFileDialog([stem, f = job.format]() { return Platform::SaveVideoDialog(stem + (f == 1 ? ".mov" : ".mp4")); },
                         [job](const std::string& path) mutable { job.path = path; gArrangeRenderJobs.push_back(job); });
   }
   if (!canQueue)
      ImGui::EndDisabled();
   ImGui::SameLine();
   ImGui::TextDisabled("renders start right away, one after another");

   if (!gArrangeRenderJobs.empty())
   {
      ImGui::SeparatorText("queue");
      static const char* kStatus[] = { "queued", "rendering", "done", "failed", "cancelled" };
      for (int i = 0; i < (int)gArrangeRenderJobs.size(); i++)
      {
         ArrangeRenderJob& j = gArrangeRenderJobs[(size_t)i];
         ImGui::PushID(i);
         const size_t slash = j.path.find_last_of("/\\");
         ImGui::Text("%s", slash == std::string::npos ? j.path.c_str() : j.path.c_str() + slash + 1);
         ImGui::SameLine();
         ImGui::TextDisabled("[%s]", kStatus[std::clamp(j.status, 0, 4)]);
         if (j.status == 1)
         {
            const float p = j.totalFrames > 0 ? (float)j.frame / (float)j.totalFrames : 0.0f;
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "%d / %d frames", j.frame, j.totalFrames);
            ImGui::ProgressBar(p, ImVec2(300, 0), lbl);
            ImGui::SameLine();
         }
         if ((j.status == 0 || j.status == 1) && ImGui::SmallButton("cancel"))
            j.status = 4;
         if (j.status >= 2)
         {
            if (!j.message.empty())
               ImGui::TextDisabled("  %s", j.message.c_str());
            if (j.status == 2)
            {
               ImGui::SameLine();
               if (ImGui::SmallButton("show file"))
                  Platform::RevealInFileManager(j.path);
            }
         }
         ImGui::PopID();
      }
      if (!ArrangeRenderBusy() && ImGui::SmallButton("clear finished"))
         gArrangeRenderJobs.erase(std::remove_if(gArrangeRenderJobs.begin(), gArrangeRenderJobs.end(),
                                                 [](const ArrangeRenderJob& j) { return j.status >= 2; }),
                                  gArrangeRenderJobs.end());
   }
   ImGui::End();
}
