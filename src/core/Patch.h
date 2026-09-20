#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class INode;

// Patch files: the whole graph written to disk and read back.
//
// The format is line-based text rather than JSON, for two reasons. It stays
// readable and diffable, which matters when a patch is the user's actual work;
// and it degrades gracefully - an unknown key or a node type that no longer
// exists is skipped with a warning instead of failing the whole load.
//
//   infinite-patch 1
//   node <index> <category> <type name to end of line>
//     pos <x> <y>
//     flags <showParams> <bypassed> <showMiniViewport> <showAdvancedParams>
//     f <name> <value>          float
//     i <name> <value>          int
//     b <name> <0|1>            bool
//     c <name> <r> <g> <b>      colour
//     s <name> <text to end of line>
//   end
//   cable <dstIndex> <dstSlot> <srcIndex>
//   geo <dstIndex> <dstSlot> <srcIndex>
//   aud <dstIndex> <dstSlot> <srcIndex>
//   note <dstIndex> <dstSlot> <srcIndex>
//   mod <dstIndex> <dstParam> <srcIndex> <srcOutput> <polarity> <depth> <centre> [<lo> <hi> [<enabled>]]
//     polarity/depth/centre are trailing additions (per-binding modulation
//     polarity - see Modulation::Source): missing on older patches, where
//     >>'s failed-extraction behaviour leaves them at their defaults
//     (polarity 0 = absolute, depth 1.0, centre 0.0), i.e. today's override
//     behaviour, unchanged.
//     lo/hi are a further trailing addition (the destination-units range a
//     binding writes into its target - see Modulation::Source::lo/hi):
//     missing on any patch saved before this field existed, in which case
//     ModRecord::hasRange is left false and the range is derived from
//     polarity/depth/centre the first time the destination is drawn (see
//     Modulation::ResolvedSourceFor) rather than read from the file.
//     enabled is the last, and only ever written alongside lo/hi (never on
//     its own, since the tokens are positional and a lone enabled token
//     would decode as lo) - missing on any older patch, where it defaults
//     to true, matching a binding that has always been written.
//   pal <dstIndex> <dstColor> <srcIndex> <srcSwatch>
//   expr <dstIndex> <dstParam> <expression text to end of line>
//   glob <name> <expression text to end of line>
//   perfui <cellSize> <pageCount>
//   perfname <pageIndex> <page name to end of line>
//   perf <kind> <dstIndex> <dstParam> <dstParam2> <cellX> <cellY> <page> <colorR> <colorG> <colorB> <value> <value2> <boolName> <label to end of line>
//   perftarget <perfIndex> <dstIndex> <dstParam> <axis> <boolName>
//   transport <bpm> <tsNum> <tsDen> <key> <scale>
//   gesture <dstIndex> <dstParam> <speed> <hasRangeOverride> <rangeLo> <rangeHi> <sampleCount> <value0> <time0> <startsNewGrab0> ...
//     One shift-drag/armed-recording loop (see core/GestureRecorder.h),
//     trailing sample triples repeated sampleCount times. Missing entirely
//     on any patch saved before this line existed - gestures were session-
//     only state then and simply don't come back on load, same as any other
//     unrecognised tag.
//   stream <type> <blendMode> <opacity> <gainDb> <pan> <name to end of line>
//   streamid <id>
//     The preceding `stream` line's stable lane id. Its own line rather than a
//     field on `stream` so that builds predating it still read the stream.
//   cliptick <streamIndex> <id> <startTick> <lengthTick> <srcUid> <srcOutput>
//            <fadeInTick> <fadeOutTick> <gainDb> <enabled> <groupId> <r> <g> <b> <name>
//   streammix <streamIndex> <mute> <solo>
//     Audio lane mute/solo. Only written when either is set.
//   clipblend <streamIndex> <clipId> <blendMode>
//     A video clip's compositing mode, only written when non-Normal. Files
//     without it inherit the stream line's (legacy, lane-wide) blendMode.
//   clipaudio <streamIndex> <clipId> <pan> <pitch> <syncToTempo>
//     Sample-dropped audio clip fields, only written when any differs from
//     default (pan 0, pitch 0, syncToTempo true). Missing entirely on a
//     patch saved before these fields existed, or on a clip that never set
//     them - ClipRecord's in-struct defaults cover both.
//   clipgrade <streamIndex> <clipId> <brightness> <contrast> <saturation>
//     Sample-dropped video/image clip basic color grade, only written when
//     any differs from default (brightness 0, contrast 0, saturation 1).
//   clipmodbypass <streamIndex> <clipId> <paramIndex> [<paramIndex> ...]
//     Which of the source node's modulated params this clip does NOT want
//     modulated (Arrange::Clip::bypassedModParams). Variable length, to end
//     of line, so it is the last thing on its own line like `cliptick`'s
//     name. Only written when the list is non-empty; a patch without it
//     loads with nothing bypassed, i.e. exactly the behaviour that predates
//     the field.
//   marker <id> <posTick> <colorRGBA8> <name to end of line>
//   arrange <nextId> <timeDisplay> <snap> <triplet> <zoom> <scroll> <loopOn>
//           <loopStart> <loopEnd> <dock> <w> <h> <fps> <sr> <format>
//           <rangeKind> <rangeStart> <rangeEnd> <audioSrc> <videoSrc> <folder>
//   clip <streamIndex> <start> <length> <srcIndex> <srcOutput> <triggerMode> <fadeIn> <fadeOut> <gainDb> <speed> <loop>
//     LEGACY, read-only. Seconds and node indices, written by builds before
//     the tick model. Converted to `cliptick` form after the whole file is
//     parsed (the conversion needs the file's own bpm, which the transport
//     line may carry after the clips). triggerMode/speed/loop are accepted
//     and ignored - the engine never used them.
//
//     Arrangement timeline (docs/plans/arrangement/README.md). A clip
//     back-references the stream line it belongs to by position, like
//     perftarget -> perf. A clip with an out-of-range stream index or a
//     length <= 0 is dropped; a malformed stream line is kept with defaults
//     so later clip indices still line up.
//
// Names may contain spaces, so anything free-form is always last on its line.
namespace Patch
{
   struct NodeRecord
   {
      int index = 0;
      // Stable identity, unlike `index`, which RemoveNodeByIndex reuses.
      // Arrangement clips reference this, so a clip survives node delete +
      // undo and a full reload. Written on its own `uid` line rather than as
      // a generic `s uid` param because FieldGraphNode already writes an
      // unrelated `s uid <hex>` param that would collide. 0 in a patch saved
      // before this existed - ApplyPatchData mints a fresh uid for those.
      uint64_t uid = 0;
      std::string category;
      std::string typeName;
      float x = 0.0f, y = 0.0f;
      bool showParams = false;
      bool bypassed = false;
      bool showMiniViewport = false;
      bool showAdvancedParams = false; // audio nodes only, see GraphNode.h
      // Raw key/value lines, replayed into the node through its ParamVisitor.
      std::vector<std::pair<std::string, std::string>> params;
   };

   struct CableRecord
   {
      int dstIndex = 0;
      int dstSlot = 0;
      int srcIndex = 0;
      // Which of the source's note/audio outputs this cable reads
      // (NoteCable::GetOutputSlot() / AudioCable::GetOutputSlot()). Unused
      // (always 0) for plain image cable records - only Note Router has more
      // than one note output, and only a node like VideoSourceNode (image +
      // audio on separate outputs) has more than one audio output.
      int srcOutput = 0;
   };

   struct ModRecord
   {
      int dstIndex = 0;
      int dstParam = 0;
      int srcIndex = 0;
      int srcOutput = 0;
      // See Modulation::Source::Polarity. 0 = absolute (default, today's
      // override behaviour), 1 = bipolar.
      int polarity = 0;
      float depth = 1.0f;
      float centre = 0.0f;
      // Destination-units range this binding writes into - see
      // Modulation::Source::lo/hi. hasRange is false only when this record
      // was read from a patch saved before lo/hi existed (no tokens present
      // on the "mod" line), in which case lo/hi below are left at their
      // defaults and must NOT be trusted - the range is derived from
      // polarity/depth/centre lazily instead. See the format comment above.
      float lo = 0.0f;
      float hi = 0.0f;
      bool hasRange = false;
      // See Modulation::Source::enabled. Only meaningful (and only ever
      // written) alongside lo/hi - see the format comment above.
      bool enabled = true;
      float curve = 0.0f; // in [-1.0, 1.0], 0 = linear
   };

   // A palette node driving one colour swatch on another node.
   struct PaletteRecord
   {
      int dstIndex = 0;
      int dstColor = 0;
      int srcIndex = 0;
      int srcSwatch = 0;
   };

   // A typed algebraic expression driving one parameter directly, with no
   // modulator node involved - see Modulation::SetExpression.
   struct ExprRecord
   {
      int dstIndex = 0;
      int dstParam = 0;
      std::string text;
      float curve = 0.0f; // in [-1.0, 1.0], 0 = linear
   };

   // One patch-wide named value an expression can read - see
   // core/ExprGlobals.h. Order is meaningful (a global may reference the ones
   // declared before it), so these are written and read as a list.
   struct GlobalRecord
   {
      std::string name;
      std::string expr;
   };

   // One control on the performance matrix. `kind` is 0=Knob, 1=VFader, 2=HSlider,
   // 3=Toggle, 4=XYPad.
   // dstIndex/dstParam address a destination node parameter (-1 if unbound macro).
   // dstParam2 is used by the XY pad for its Y axis.
   struct PerfTarget
   {
      int dstIndex = -1;
      int dstParam = -1;
      std::string boolName;
   };

   struct PerfRecord
   {
      int   kind      = 0;
      int   dstIndex  = -1;
      int   dstParam  = -1;
      int   dstParam2 = -1;
      int   cellX = 0, cellY = 0;
      int   page  = 0;
      float colorR = 0.0f, colorG = 0.0f, colorB = 0.0f;
      float value = 0.0f;
      float value2 = 0.0f;
      std::string boolName;   // toggle only
      std::string label;      // empty = inherit the source param's own name
      std::vector<PerfTarget> targets;
      std::vector<PerfTarget> targetsY;

      // MIDI mapping (0 = unbound device, channel 0-15, controller/note 0-127)
      int  midiDevice     = 0;
      int  midiChannel    = -1;
      int  midiController = -1;
      bool midiIsNote     = false;
      int  midiDeviceY    = 0;
      int  midiChannelY   = -1;
      int  midiControllerY = -1;
      bool midiIsNoteY    = false;
   };

   struct PerfLayoutRecord
   {
      int cellSize = 76;
      int pageCount = 1;
      std::vector<std::string> pageNames;
   };

   // Global transport state (docs/plans/audio/P3c-P3a2-design.md §0.2/§0.3) -
   // BPM, time signature, and key/scale all live on Transport rather than any
   // node, so without this record they silently reset to their defaults on
   // every load. Defaults here match Transport's own field initialisers, so
   // a patch saved before this record existed (or a `transport` line missing
   // some tokens, via >>'s failed-extraction behaviour) reads back unchanged.
   struct TransportRecord
   {
      float bpm = 120.0f;
      int timeSigNum = 4;
      int timeSigDen = 4;
      int key = 0;
      int scale = 0;
   };

   // One sample of a recorded gesture trace - mirrors GestureRecorder::Sample
   // field for field (see core/GestureRecorder.h).
   struct GestureSample
   {
      float value = 0.0f;
      double timeSec = 0.0;
      bool startsNewGrab = false;
   };

   // A shift-drag/armed-recording loop, looping back into one param - mirrors
   // GestureRecorder::Playback. Previously this lived only in
   // GestureRecorder's in-memory singleton (and, for undo/redo, in a
   // parallel snapshot outside Patch::Data - see UndoEntry in main.cpp);
   // it is now also part of the saved patch, so a recording set up while
   // working on a patch is still looping next time it's opened.
   struct GestureRecord
   {
      int dstIndex = 0;
      int dstParam = 0;
      float speed = 1.0f;
      bool hasRangeOverride = false;
      float rangeLo = 0.0f, rangeHi = 0.0f;
      std::vector<GestureSample> samples; // >= 2 entries, timeSec strictly increasing
      float curve = 0.0f; // in [-1.0, 1.0], 0 = linear
   };

   // Arrangement timeline (docs/plans/arrangement/README.md). A stream is one
   // lane; it owns its clips, same parent/child shape as PerfRecord::targets,
   // so reordering or deleting a stream can never leave a clip on the wrong
   // lane. Clips within one stream never overlap - enforced by the editor,
   // not here.
   enum StreamType { kStreamVideo = 0, kStreamAudio = 1 };

   struct ClipRecord
   {
      // Ticks, never seconds (Arrange::kPPQ per quarter note). A tempo change
      // therefore leaves every clip on its bar/beat. Legacy `clip` lines are
      // seconds; Read() converts them once the whole file (and so the file's
      // own bpm) has been parsed.
      uint64_t id        = 0;
      int64_t  startTick  = 0;   // >= 0
      int64_t  lengthTick = 0;   // > 0
      uint64_t srcUid    = 0;    // GraphNode::uid, 0 = unassigned
      // Only set by legacy `clip` lines, which predate uids. ApplyPatchData
      // resolves it to a uid and then ignores it. -1 once converted.
      int      legacySrcIndex = -1;
      int      srcOutput = 0;
      int64_t  fadeInTick  = 0;
      int64_t  fadeOutTick = 0;
      float    gainDb    = 0.0f;
      float    pan       = 0.0f;
      bool     enabled   = true;
      uint64_t groupId   = 0;    // 0 = not grouped
      std::string name;          // empty = auto (source node's own title)
      float  colorR = 0.0f, colorG = 0.0f, colorB = 0.0f; // 0,0,0 = no tint
      int    blendMode = -1;     // -1 = not in the file: inherit the stream's legacy blendMode
      // Sample-dropped media fields (own lines - see `clipaudio`/`clipgrade`
      // in the format comment above - since cliptick already ends in a
      // to-end-of-line name and nothing can be appended after it).
      float  pitch   = 0.0f;    // audio only, semitones, +/-24
      bool   syncToTempo = true; // audio only
      float  opacity = 1.0f;    // video/image only, 0..1, 1 = fully opaque
      float  colorBrightness = 0.0f; // video/image only, -1..1, 0 = no change
      float  colorContrast   = 0.0f; // video/image only, -1..1, 0 = no change
      float  colorSaturation = 1.0f; // video/image only, 0..2, 1 = no change
      bool   retrigger       = true;
      // True only for a clip created by dropping a media file onto the
      // timeline (as opposed to one whose srcUid was patched in manually) -
      // "Audio/Video Sample" in the Clip Settings panel, and the only
      // category the UI lets retrigger.
      bool   sampleDropped   = false;
      // Audio-Sample-only BPM sync (step 3 - see Arrange::Clip's own
      // comments). sourceDurationSeconds is what makes a later sampleBpm
      // edit able to recompute `length` losslessly without re-decoding.
      float  sampleBpm             = 120.0f;
      // The BPM detected at import, display only (see Arrange::Clip::origBpm).
      // Written only when > 0; -1 is the load-time sentinel for "no line",
      // which ApplyPatchData maps to 0 = none detected.
      float  origBpm               = -1.0f;
      float  sourceDurationSeconds = 0.0f;
      // Audio-Sample-only (see Arrange::Clip::sourceOffsetSeconds's own
      // comment). 0 default so a patch saved before this field existed
      // loads with every Sample reading from the file's own beginning,
      // exactly like it always has.
      float  sourceOffsetSeconds   = 0.0f;
      // Per-clip modulation bypass (see Arrange::Clip::bypassedModParams).
      // Sorted, deduplicated paramIndices on the clip's source node. Empty
      // on every clip saved before this field existed, which is the same as
      // "nothing bypassed" - so an old patch loads identically.
      std::vector<int> bypassedModParams;
   };

   struct StreamRecord
   {
      uint64_t id     = 0;
      int   type      = kStreamVideo;
      int   blendMode = 0;      // legacy lane-wide mode; now per clip (ClipRecord::blendMode)
      float opacity   = 1.0f;   // video only, 0..1
      float gainDb    = 0.0f;   // audio only
      float pan       = 0.0f;   // audio only, -1..1
      // Trailing fields (added after the original set): an older reader's
      // `stream` line has fewer tokens, so extraction fails and these keep
      // their in-struct defaults - `enabled` MUST default true here, not
      // rely on stream-extraction's zero-init, or every pre-groups patch
      // would load with every track silently disabled.
      bool     enabled = true;
      uint64_t groupId = 0;     // 0 = not in a track group
      bool  mute      = false;  // audio only; saved on its own `streammix` line
      bool  solo      = false;  // audio only
      std::string name;         // empty = auto ("V1", "A2", ...) - label derived by the UI
      float colorR = 0.0f, colorG = 0.0f, colorB = 0.0f;
      float rowHeight = 0.0f;   // 0 = default; new field, older patches load at the default height
      std::vector<ClipRecord> clips;
   };

   struct TrackGroupRecord
   {
      uint64_t    id            = 0;
      uint32_t    color         = 0xFF808080u;
      bool        enabled       = true;
      bool        collapsed     = false;
      uint64_t    parentGroupId = 0; // 0 = top-level; new field, appended after the old collapsed slot
      std::string name;
   };

   struct MarkerRecord
   {
      uint64_t id  = 0;
      int64_t  posTick = 0;
      uint32_t color = 0xFFFFFFFFu;
      std::string name;
   };

   // Arrange::Settings, flattened. Audio mode is deliberately absent: the app
   // always starts in Canvas mode, so it must never be saved.
   struct ArrangeSettingsRecord
   {
      uint64_t nextId = 1;     // persisted, not recomputed - see Arrange::Model
      int   timeDisplay = 0;
      int   snapDivision = 4;     // 0 = off, 1 = bar, d = 1/d (WP6)
      bool  snapTriplet = false;
      float zoom = 1.0f;
      float scroll = 0.0f;
      bool  loopEnabled = false;
      int64_t loopStart = 0, loopEnd = 0;
      int   dockSide = 0;
      int   renderWidth = 1920, renderHeight = 1080, renderFps = 60;
      int   renderSampleRate = 48000, renderFormat = 0;
      int   renderRangeKind = 0;
      int64_t renderRangeStart = 0, renderRangeEnd = 0;
      // Deprecated; see ArrangeModel.h. Serialized for file-format
      // compatibility only.
      int   renderAudioSource = -1, renderVideoSource = -1;
      std::string renderFolder;
      bool  importSyncToTempo = true; // see Arrange::Settings::importSyncToTempo
   };

   // Dockable viewport panel (see gViewportPanelNodes in main.cpp)
   struct ViewportRecord
   {
      bool  open = false;
      int   dock = 1; // 0 = bottom, 1 = right, 2 = left, 3 = top
      float width = 320.0f;
      float height = 260.0f;
      std::vector<int> nodes; // node indices displayed as viewport cards
   };

   struct Data
   {
      std::vector<NodeRecord> nodes;
      std::vector<CableRecord> cables;   // image cables
      std::vector<CableRecord> geometry; // geometry, camera and light pins
      std::vector<CableRecord> audio;    // audio cables
      std::vector<CableRecord> notes;    // note cables
      std::vector<ModRecord> modulation;
      std::vector<PaletteRecord> palette;
      std::vector<ExprRecord> expressions;
      std::vector<GlobalRecord> globals;
      std::vector<PerfRecord> performance;
      PerfLayoutRecord perfLayout;
      TransportRecord transport;
      std::vector<GestureRecord> gestures;
      std::vector<StreamRecord> streams; // arrangement timeline, clips nested
      std::vector<MarkerRecord> markers; // arrangement markers, sorted by pos
      std::vector<TrackGroupRecord> trackGroups;
      ArrangeSettingsRecord arrangeSettings;
      ViewportRecord viewport;
   };

   bool Write(const std::string& path, const Data& data, std::string& outError);
   bool Read(const std::string& path, Data& outData, std::string& outError);

   // Applies saved parameters to a node, and collects them from one.
   void SaveParams(INode* node, std::vector<std::pair<std::string, std::string>>& out);
   void LoadParams(INode* node, const std::vector<std::pair<std::string, std::string>>& in);

   // Most-recently-opened list, persisted next to the app's other preferences.
   const std::vector<std::string>& Recents();
   void NoteRecent(const std::string& path);
   void LoadRecents();
   void SaveRecents();
}
