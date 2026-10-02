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
//     flags <showParams> <bypassed> <showMiniViewport> <showAdvancedParams> <showPreview>
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
//   mod <dstIndex> <dstParam> <srcIndex> <srcOutput> <polarity> <depth> <centre>
//     polarity/depth/centre are trailing additions (per-binding modulation
//     polarity - see Modulation::Source): missing on older patches, where
//     >>'s failed-extraction behaviour leaves them at their defaults
//     (polarity 0 = absolute, depth 1.0, centre 0.0), i.e. today's override
//     behaviour, unchanged.
//   pal <dstIndex> <dstColor> <srcIndex> <srcSwatch>
//   expr <dstIndex> <dstParam> <expression text to end of line>
//   glob <name> <expression text to end of line>
//   (node block) uid <stable id>
//   stream / streamid / cliptick / streammix / streamrowheight / clipblend /
//   clipaudio / clipgrade / clipopacity / clipmodbypass / clipretrigger /
//   clipsample / clipbpm / cliporigbpm / clipsrcoffset / marker /
//   trackgroup / arrange / arrangeimportsync
//     Arrangement Timeline, upstream's grammar verbatim (see upstream
//     Patch.h for each line's fields).
//
// Names may contain spaces, so anything free-form is always last on its line.
namespace Patch
{
   struct NodeRecord
   {
      int index = 0;
      // Stable identity (upstream `uid` line): arrangement clips reference
      // it, so a clip survives undo and reload. 0 = not in the file.
      uint64_t uid = 0;
      std::string category;
      std::string typeName;
      float x = 0.0f, y = 0.0f;
      bool hasPos = true; // Turbo 0.45: false = no `pos` line in the file (auto layout)
      bool showParams = false;
      bool bypassed = false;
      bool showMiniViewport = false;
      bool showAdvancedParams = false; // audio nodes only, see GraphNode.h
      bool showPreview = true;
      int colorTag = 0; // Turbo: 6th token of the flags line
      // Raw key/value lines, replayed into the node through its ParamVisitor.
      std::vector<std::pair<std::string, std::string>> params;
   };

   struct CableRecord
   {
      int dstIndex = 0;
      int dstSlot = 0;
      int srcIndex = 0;
      // Which of the source's note outputs this cable reads (NoteCable::
      // GetOutputSlot()). Unused (always 0) for cable/geo/aud records - only
      // Note Router has more than one note output.
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
      // Turbo range mapper (trailing tokens, defaults = no remap).
      float inMin = 0.0f, inMax = 1.0f, outMin = 0.0f, outMax = 1.0f;
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
   };

   // One patch-wide named value an expression can read - see
   // core/ExprGlobals.h. Order is meaningful (a global may reference the ones
   // declared before it), so these are written and read as a list.
   // Turbo MIDI learn mapping (see core/MidiMap.h).
   //   midimap <dst> <param> <isNote> <channel> <number> <mode> <soft> <invert>
   //           <outMin> <outMax> <deviceKey|*> <param name to end of line>
   struct MidiMapRecord
   {
      int dstIndex = 0;
      int dstParam = 0;
      bool isNote = false;
      int channel = -1;
      int number = 0;
      int mode = 0;
      bool soft = false;
      bool invert = false;
      float outMin = 0.0f;
      float outMax = 1.0f;
      std::string deviceKey; // "" = any device
      std::string paramName;
      float smoothMs = 25.0f; // "midismooth <dst> <param> <ms>" line (0.39+)
   };

   struct GlobalRecord
   {
      std::string name;
      std::string expr;
   };

   // Patch-wide playback/editor settings. They are also mirrored to an app
   // preference file so a new session starts with the last setup. `present`
   // keeps old patches backward-compatible: opening one does not replace the
   // current machine setup with these default member values.
   struct SceneSettings
   {
      bool present = false;
      uint32_t audioOutputDeviceId = 0;
      uint32_t audioInputDeviceId = 0;
      double audioSampleRate = 0.0;
      int audioBufferFrames = 512;
      int audioDriver = 0; // Turbo: 0 WASAPI shared, 1 ASIO, 2 WASAPI exclusive, 3 WASAPI low latency, 4 DirectSound
      float audioOversample = 1.0f;
      int targetFps = 60;
      bool vsync = true;
      bool snapToGrid = true;
      float gridSnap = 20.0f;
      float zoomSensitivity = 0.5f;
      bool minimapEnabled = false;
      int minimapCorner = 2;
      float minimapSize = 190.0f;
      float minimapOpacity = 0.85f;
      bool nodePanelOpen = false;
      float nodePanelWidth = 300.0f;
      int viewportPanelDock = 1;
      float viewportPanelWidth = 320.0f;
      float viewportPanelHeight = 260.0f;
      int themePreset = 0;
      bool diagnosticLog = true;
      bool autosaveEnabled = true;
      int autosaveSeconds = 30;
      bool audioAutoStart = false; // Turbo: start the audio engine when the app opens (app-level only)
      bool startWithExample = true; // Turbo 0.46: open the bundled example at startup (app-level only)
      bool updateCheck = true; // Turbo 0.46: look for a newer release at startup (app-level only)
      float uiScale = 0.0f; // Turbo 0.46: 0 = follow Windows' display scale, else a fixed factor (app-level only)
   };

   // Turbo (upstream format): BPM, time signature and key/scale live on
   // Transport, not on any node, so without this record they reset on every
   // load. Defaults match Transport's own, so an older patch reads unchanged.
   //   transport <bpm> <tsNum> <tsDen> <key> <scale>
   // Turbo 0.46 (upstream's Performance Mode): one control on the performance
   // matrix. kind 0 Knob, 1 VFader, 2 HSlider, 3 Toggle, 4 XY Pad, 5 Trigger,
   // 6 Number Box, 7 Radio Selector, 8 Bipolar Knob, 9 Step Gate.
   // dstIndex/dstParam address a node parameter (-1 = unbound macro); the XY
   // pad uses dstParam2 for Y. targets / targetsY are extra destinations.
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
      std::string label;      // empty = the destination param's own name
      std::vector<PerfTarget> targets;
      std::vector<PerfTarget> targetsY;
      int  midiDevice     = 0;  // 0 = no MIDI binding
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

   struct TransportRecord
   {
      float bpm = 120.0f;
      int timeSigNum = 4;
      int timeSigDen = 4;
      int key = 0;
      int scale = 0;
   };

   // Turbo (upstream format): a Shift-drag gesture recording looping on a
   // param. One sample of the trace mirrors GestureRecorder::Sample.
   //   gesture <dst> <param> <speed> <hasRange> <lo> <hi> <count> (<value> <time> <newGrab>)*
   struct GestureSample
   {
      float value = 0.0f;
      double timeSec = 0.0;
      bool startsNewGrab = false;
   };

   struct GestureRecord
   {
      int dstIndex = 0;
      int dstParam = 0;
      float speed = 1.0f;
      bool hasRangeOverride = false;
      float rangeLo = 0.0f, rangeHi = 0.0f;
      std::vector<GestureSample> samples; // >= 2 entries
      float curve = 0.0f;
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
      std::vector<MidiMapRecord> midi;
      SceneSettings settings;
      TransportRecord transport;
      std::vector<GestureRecord> gestures;
      // Turbo 0.43: Arrangement Timeline, same records and file lines as
      // upstream so patches open in both.
      std::vector<StreamRecord> streams; // clips nested
      std::vector<MarkerRecord> markers; // sorted by pos
      std::vector<TrackGroupRecord> trackGroups;
      ArrangeSettingsRecord arrangeSettings;
      bool hasArrange = false; // an `arrange` line was read
      std::vector<PerfRecord> performance; // Turbo 0.46: Performance Mode
      PerfLayoutRecord perfLayout;
   };

   bool Write(const std::string& path, const Data& data, std::string& outError);
   bool Read(const std::string& path, Data& outData, std::string& outError);
   // Turbo 0.45: the same format as text in memory (RPC / MCP).
   bool WriteText(const Data& data, std::string& outText, std::string& outError);
   bool ReadText(const std::string& text, Data& outData, std::string& outError);

   // Applies saved parameters to a node, and collects them from one.
   void SaveParams(INode* node, std::vector<std::pair<std::string, std::string>>& out);
   void LoadParams(INode* node, const std::vector<std::pair<std::string, std::string>>& in);

   // Most-recently-opened list, persisted next to the app's other preferences.
   const std::vector<std::string>& Recents();
   void NoteRecent(const std::string& path);
   void LoadRecents();
   void SaveRecents();

   bool LoadAppSettings(SceneSettings& out);
   bool SaveAppSettings(const SceneSettings& settings, std::string& outError);
}
