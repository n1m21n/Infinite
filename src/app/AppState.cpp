// Application state: every non-const namespace variable, in the original definition order (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // The dockable viewport panel (right-click a node -> "Open in viewport
   // panel"), separate from both the inline preview above and the inline
   // per-node mini 3D viewport toggle. Not const: the panel's canvas-facing
   // edge is a drag handle, so these are starting sizes, not fixed ones.
   // Clamped against the window every frame - see the layout section.
   float gViewportPanelWidth = 320.0f;

   float gViewportPanelHeight = 260.0f;


   // Backdrop for any node preview about to blit a texture: a flat dark rect
   // reads as solid black wherever that texture is actually transparent, so
   // this paints a checkerboard instead, matching image editors' convention.
   // Some users find the checker pattern distracting at small preview sizes,
   // so it can be swapped for a flat neutral fill via Settings > Appearance.
   bool gCheckerboardBackdrop = true;


   // Metronome (top bar, next to the time signature). Volume and the accent
   // choice are machine settings; "on" is deliberately not saved, so the app
   // never opens clicking.
   bool gMetronomeOn = false;

   float gMetronomeVolume = 0.5f;

   bool gMetronomeAccent = true;

   bool gMetronomeDirty = false;

   // Defined beside the undo stack below; declared here because
   // ArrangeMakeClipSourceUnique (also far above it) has to hold a
   // suppression region open across a spawn.
   extern bool gSuppressUndoCheckpoints;


   std::vector<GraphNode> gNodes;

   std::vector<NodeTitleInstanceEntry> gTitleInstance;

   std::vector<size_t> gTitleInstanceLive;
 // slots whose title follows a live field
   bool gTitleInstanceDirty = true;

   const GraphNode* gTitleInstanceData = nullptr;

   size_t gTitleInstanceSize = 0;

   int gTitleInstanceRebuilds = 0;


   // Which node indices each Group considers its own, once-in-always-in. Kept
   // outside GroupNode because it is keyed by GraphNode::index, not anything
   // the node itself knows about. Membership only grows (see DrawGroupNode) -
   // a stale index (its node deleted, or undo/redo having rewound past it) is
   // simply skipped wherever this is read, never treated as an error.
   std::map<GroupNode*, std::set<int>> gGroupMembers;

   // Starts at 1, not 0: node index 0 would give NodeId 0, and the node editor
   // reserves 0 as its Invalid id. A node with that id still draws, but every
   // `if (ed::NodeId n = ed::GetHoveredNode())` silently reads false for it, so
   // the first node spawned in a patch could not be hovered, selected or
   // dragged the way every other node could.
   int gNextIndex = 1;

   ed::EditorContext* gEditor = nullptr;

   GraphNode* gSelfTestFeeder = nullptr;

   bool gPaletteTestOk = false;
    // dev test only
   bool gPaletteTestPending = false;

   ImVec2 gSpawnPos(0.0f, 0.0f);


   // Manual drag-and-drop for the Samples search panel: the panel is a
   // plain ImGui child window, but the node canvas it drops onto is managed
   // internally by imgui_node_editor (ed::), which has no public "accept a
   // dropped item anywhere on empty canvas" API the way a normal ImGui
   // window does. Tracking the drag by hand and resolving the drop with
   // ed::GetHoveredNode()/ScreenToCanvas - the same primitives the existing
   // "double-click empty canvas" search popup already uses - avoids fighting
   // ed::'s own item/hover system with an invisible full-canvas button.
   bool gSampleDragActive = false;

   LibraryDragKind gSampleDragKind = LibraryDragKind::Sample;

   std::string gSampleDragPath;
 // Sample/Media only: the file being dragged
   std::string gSampleDragName;

   std::string gFieldDragPresetName;

   std::string gFieldDragNodeType;

   std::string gFieldDragNodeCategory;

   int gFieldDragIndex = -1;


   // Path of the sample currently auditioning from the Samples panel, or
   // empty. Only one preview plays at a time - clicking a row's play button
   // stops whatever else is auditioning and starts this one - see
   // local-prompts/05-sample-preview-in-search-panel.md. The preview player
   // itself lives on AudioEngine, outside the node topology entirely; this
   // is just the UI's record of which row's button should read "latched".
   std::string gPreviewingSamplePath;

   // Set when the last preview attempt's decode failed, so that row's
   // tooltip can report the error instead of just the file path. Cleared on
   // the next successful play attempt (of any row).
   std::string gPreviewErrorPath;

   std::string gPreviewErrorMessage;

   Platform::PluginDesc gPluginDragDesc;
 // Plugin drags only - identity, not a path
   SampleScanner gSampleScanner;

   SampleScanner gMediaScanner(SampleScanner::Kind::Media);

   PluginScanner gPluginScanner;

   int gSearchPanelMode = 0;

   BrowserFilterState gModulesFilter;

   BrowserFilterState gSampleFilter;

   BrowserFilterState gMediaFilter;

   BrowserFilterState gPluginFilter;

   BrowserFilterState gFieldFilter;

   // INFINITE_SAMPLERDRAGTEST only: the Samples panel's result row screen
   // rect, captured live each frame it's drawn so the synthetic drag driver
   // can aim at the real widget rather than a guessed position.
   ImVec4 gSamplerDragTestRowRect(-1.0f, -1.0f, -1.0f, -1.0f);

   // INFINITE_SAMPLERDRAGTEST only: the target Sampler's screen-space
   // centre, recomputed once per frame from ed::GetNodePosition/GetNodeSize
   // *after* ed::End() (those calls need a live editor context - calling
   // them from the pre-ed::Begin() input driver, like the row rect above
   // is read from, segfaults on a null current-editor pointer).
   ImVec2 gSamplerDragTestTargetScreen(-1.0f, -1.0f);

   bool gSamplerDragTestTargetValid = false;

   // INFINITE_MEDIADRAGTEST only: the Media panel's result row screen rect,
   // same role as gSamplerDragTestRowRect above but for the Media mode.
   ImVec4 gMediaDragTestRowRect(-1.0f, -1.0f, -1.0f, -1.0f);

   ImVec2 gMediaDragTestTargetScreen(-1.0f, -1.0f);

   bool gMediaDragTestTargetValid = false;

   // INFINITE_PLUGINDRAGTEST only: same two roles again, for the Plugins mode.
   ImVec4 gPluginDragTestRowRect(-1.0f, -1.0f, -1.0f, -1.0f);

   // Which plugin that captured row actually is. The Plugins panel lists every
   // installed effect, so the fixture can't assume a particular row - it aims
   // at whichever one it captured and asserts against that one's identifier.
   std::string gPluginDragTestRowId;

   ImVec2 gPluginDragTestTargetScreen(-1.0f, -1.0f);

   bool gPluginDragTestTargetValid = false;

   int gPluginDragTestPhase = 0;

   // Mirrors of the sPhase locals in the two drag-test input drivers below,
   // promoted to globals purely so the INFINITE_EXITAFTER handler can print
   // a diagnostic if a run times out stuck at phase 0 (never found a usable
   // row) instead of exiting with no verdict line at all.
   int gSamplerDragTestPhase = 0;

   int gMediaDragTestPhase = 0;


   // Set when a cable drag is released on empty canvas (link-drag search):
   // the output pin the drag started from, and the node types the search
   // popup should suggest first because they're compatible with it.
   int gLinkDragSourcePin = -1;

   std::vector<std::pair<std::string, std::string>> gLinkDragSuggestions;

   AudioDropPickerState gAudioDropPicker;
 // far above any node/pin id
   std::vector<LinkInfo> gLinks;

   bool gSnapToGrid = true;
   bool gShowCanvasGrid = false;   // the faint line grid behind the nodes; off by default, Settings > Canvas & Workspace

   float gGridSnap = 20.0f;

   // Audio device/rate/buffer selection, applied to AudioEngine on next
   // Start(). 0 / 0.0 mean "system default" - see
   // Platform::AudioDeviceOpen's doc comment in Platform.h. Session-only,
   // same as every other setting in the "Menu" menu (no settings
   // persistence exists anywhere in this codebase yet).
   uint32_t gAudioOutputDeviceId = 0;

   uint32_t gAudioInputDeviceId = 0;

   double gAudioSampleRate = 0.0;

   int gAudioBufferFrames = 512;

   // Windows-only output stream mode (Platform::AudioSetOutputMode): 0 standard
   // shared (default, today's behaviour), 1 low-latency shared, 2 exclusive.
   // Persisted, but a no-op on macOS/Linux (their UI never shows it).
   int gAudioOutputMode = 0;

   // Placeholder only - no DSP node reads this yet. P3c's Drive/AudioFilter
   // kernels are the intended future consumer (see README.md's Effects
   // table); this phase just makes the setting exist and be visible.
   float gAudioOversample = 1.0f;

   // Set whenever a Start() call from the toolbar toggle or "Apply audio
   // settings" fails, so the toggle button can surface why on hover instead
   // of just silently staying off. Cleared on a successful Start().
   std::string gAudioStartError;

   OfflineRenderState gOfflineRender;


   // ---- headless jobs (--render / --frame, see core/HeadlessJob.h) ----
   // One place decides "this process is a batch job, not a session": the job
   // itself, the INFINITE_EXITAFTER dev harness and screenshot mode all mean
   // no autosave, no control server, no update check, no audio device.
   Headless::Job gHeadlessJob;

   int gHeadlessExitCode = 0;

   // Sample rate an offline take runs the graph at when no device is open.
   // Read by RebuildAudioTopology and StartOfflineRenderSession as the last
   // fallback after the device rate; 0 outside a headless job.
   double gHeadlessAudioRate = 0.0;

   // Validator warnings from the load, folded into the job's status JSON.
   std::vector<Headless::Issue> gHeadlessPreWarnings;

bool gHeadlessNeedProbe = false;
 // the patch names controls/options: draw one node per type first
   // Strict mode: the warnings of the pre-load pass that were promoted to errors.
   // They wait here so the Warm phase can report them together with E_BAD_PARAM,
   // which needs drawn nodes - one round of fixes clears everything.
   std::vector<Headless::Issue> gHeadlessPromoted;

   // The authored patch of a --render/--frame job, kept so the checks that need
   // drawn nodes (E_BAD_PARAM) can run once the graph is loaded.
   Patch::Data gHeadlessPatch;

   // Highest `mod`/`expr` parameter index per node type, read off drawn nodes
   // (-1 = registers none). Absent = not measured, so E_BAD_PARAM stays quiet.
   std::map<std::string, int> gModulatableMax;

   // While a --validate / --describe probe is waiting for spawned nodes to
   // register their parameters, the off-screen cull is switched off and every
   // node that gets past it is noted here, so the probe waits for "each probed
   // node has been drawn" rather than a fixed number of ticks.
   bool gHeadlessProbeAll = false;

   std::set<int> gHeadlessDrawn;


   // Read by RebuildAudioTopology's arrangement lane loop and by
   // CollectArrangeVideoLayers while a scoped render job is in flight; empty
   // otherwise. A global flag rather than a threaded parameter because both
   // are also called every frame for live playback, far from any render job.
   std::unordered_set<uint64_t> gArrangeRenderActiveLaneScope;


   // The timeline's own render target. Never a canvas node: an arrangement
   // take composites its lanes onto this node's FBO and nothing else, so it
   // stays off the graph, out of the patch and out of every node list.
   std::unique_ptr<OutputNode> gArrangeTimelineExportNode;


   std::vector<ArrangeRenderJob> gArrangeRenderQueue;

   uint64_t gArrangeRenderNextJobId = 1;

   uint64_t gArrangeRenderActiveJobId = 0;
 // 0 = nothing in flight
   bool gArrangeRenderQueueRunning = false;

   ArrangeWavRenderState gArrangeWavRender;

   double gLastAudioRecoveryAttemptMs = -1.0;

   double gAudioRecoveryWindowStartMs = -1.0;

   int gAudioRecoveryAttemptsInWindow = 0;

   double gLastFrameMs = 0.0;
 // wall clock across the previous whole frame
   double gFrameStart = 0.0;
  // when the current frame began, for the limiter
   // 60 by default: a light patch spinning the GPU at 400fps costs power for
   // nothing, and a predictable budget makes the cost of a setting readable.
   int gTargetFps = 60;
       // 0 = uncapped; otherwise the frame limiter's budget
   bool gVsync = true;

   bool gRequestFitView = false;

   // Set by the "fit_view_node" RPC method (documentation screenshot tooling):
   // navigate to one node's own bounds via selection rather than the whole
   // canvas, so a feeder spawned off-screen for auto_wire_inputs never pulls
   // the view out to include it. -1 = no request pending.
   int gRequestFitViewNodeIndex = -1;

   // Set by the Edit menu, consumed next to the matching keyboard shortcuts.
   // The menu bar draws outside ed::Begin/End, and grouping needs the live
   // selection, so both routes meet in one place inside the editor frame
   // rather than the menu reaching into the editor from outside it.
   bool gRequestGroup = false;

   bool gRequestUngroup = false;

   bool gRequestBypass = false;

   bool gRequestCopy = false;

   bool gRequestPaste = false;

   bool gRequestDuplicate = false;

   bool gRequestDelete = false;

   bool gRequestSelectAll = false;

   // Keyboard model (R571): the active node is simply the one selected node
   // (click it). Tab walks that node's params; arrows move it on the grid;
   // runtime only, never saved.
   int gKbFocusNode = -1;
   // GraphNode::index of the Tab-focused param's node
   int gKbFocusParam = -1;
  // its paramIndex; -1 = no param focused
   int gKbNudge = 0;

   std::vector<KbParamEntry> gKbParams;

   bool gKbOwnTab = false;
  // an active node exists: Tab belongs to the param walker, not ImGui nav
   bool gKbZoomed = false;
  // Shift+Enter zoomed into a node; Enter restores the saved view
   ImVec2 gKbSavedScroll(0.0f, 0.0f);

   float gKbSavedZoom = 1.0f;

   bool gKbViewRestore = false;
       // applied just before ed::Begin: the editor can't take a view change mid-frame
   ImVec2 gKbPan(0.0f, 0.0f);
         // queued WASD pan, canvas units, applied the same way
   bool gComputerKeyboardHot = false;
 // a hovered audio keyboard is using letter keys this frame
   bool gRequestAddNode = false;

   bool gRequestAddComment = false;

   int gContextMenuNodeIndex = -1;
 // node the right-click context menu is open for
   int gHelpPopupNodeIndex = -1;
 // node the per-node "Help" popup is open for
   bool gNodeHelpShown = false;
    // the help popup was drawn last frame (H toggles it shut)
   bool gCloseNodeHelp = false;

   bool gOpenNodeHelpPopup = false;
 // set for one frame to open it (can't OpenPopup from inside another popup's Begin/End and have it show the same frame)
   // Which param's modulation-binding menu (Absolute/Bipolar/depth/Unbind) is
   // open. Same reason as gContextMenuNodeIndex/gOpenNodeHelpPopup above: the
   // param slider/knob is drawn deep inside ed::Begin()/ed::End() for that
   // node, which runs under the node editor's own canvas transform - an
   // ImGui::OpenPopup() called there lands at whatever screen position the
   // canvas transform happens to leave the cursor at (and, worse, doesn't
   // reliably receive its own clicks), same as any other popup in this file.
   // So the widget only *records* nodeIndex/paramIndex and a "please open"
   // flag; the popup itself is opened and drawn once, later, inside the
   // ed::Suspend() block alongside "##nodecontext"/"##nodehelp".
   int gModBindingMenuNode = -1;

   int gModBindingMenuParam = -1;

   bool gOpenModBindingMenu = false;

   // Hover-and-type for the ##modbind popup's lo/hi fields, matching the
   // hover+type convention every other param field in this file uses (see
   // gTypedParam) rather than relying on DragFloat/DragInt's own
   // double-click/Ctrl+click text-entry, which is easy to miss since nothing
   // in the popup hints it exists. field: 0=lo, 1=hi, -1=none active.
   int gModRangeTypedField = -1;

   std::string gModRangeTypedText;

   bool gModRangeTypedJustOpened = false;

   // Seeded-digit entry (hover + press a digit key) needs the field to open
   // one frame *after* the keypress rather than the same frame - see
   // gTypedParamPendingInit's comment at BeginTypedEditFromCurrent for why:
   // InputText's own select-all only takes effect once it's confirmed active,
   // which can land a frame after SetKeyboardFocusHere, so driving selection
   // off a flag races with that and produces exactly the double-char/can't-
   // replace-the-seed behaviour this is avoiding.
   bool gModRangeTypedPendingInit = false;

   bool gModRangeTypedNoAutoSelect = false;

   // The node browser lives in a docked panel rather than only the canvas popup,
   // so modules can be found without knowing the double-click gesture exists.
   bool gNodePanelOpen = false;

   // The dockable viewport panel: every node index in this list gets its own
   // card, stacked left-to-right when bottom-docked or top-to-bottom when
   // right/left-docked (see DrawViewportPanelContainer). Serialized to
   // Patch::Data and restored with node index remapping in ApplyPatchData.
   bool gViewportPanelOpen = false;

   std::vector<int> gViewportPanelNodes;

   int gViewportPanelDock = 1;
        // 0 = bottom, 1 = right, 2 = left
   // The dockable modulation matrix panel: a spreadsheet-style table of every
   // active modulation binding. Session UI state only, like gNodePanelOpen
   // above - not serialized to patch data or tracked by undo.
   bool  gModMatrixOpen = false;

   int   gModMatrixDock = 0;
              // 0 = bottom, 1 = right, 2 = left, 3 = top
   // "View in Modulation Matrix" (the param right-click menu): which row to scroll to and
   // flash when the matrix next draws. gModMatrixScrollPending is one-shot - the row loop
   // clears it the first time it finds the match, so re-opening/scrolling the panel by hand
   // afterward doesn't keep snapping back. The highlight itself lingers a little longer
   // (gModMatrixHighlightUntil, an ImGui::GetTime() deadline) so the row is still findable by
   // eye a moment after the one-shot scroll already landed on it.
   int    gModMatrixHighlightNode = -1;

   int    gModMatrixHighlightParam = -1;

   double gModMatrixHighlightUntil = 0.0;

   bool   gModMatrixScrollPending = false;

   int   gModMatrixFillRows = -1;
         // INFINITE_MODMATRIXGEOM probe only
   float gModMatrixScrollMax = -1.0f;
     // INFINITE_MODMATRIXGEOM probe only
   float gModMatrixWidth = 420.0f;

   float gModMatrixHeight = 240.0f;

   // The dockable performance matrix: custom control surface for live VJing.
   bool  gPerfPanelOpen = false;

   int   gPerfPanelDock = 0;
              // 0 = bottom, 1 = right, 2 = left, 3 = top
   float gPerfPanelWidth = 460.0f;

   float gPerfPanelHeight = 280.0f;

   // The dockable arrangement timeline panel: DAW/video-editor style lanes and clips.
   bool  gArrangePanelOpen = false;

   float gArrangePanelWidth = 480.0f;

   float gArrangePanelHeight = 240.0f;

   // The view axis is musical (WP6): pixels per quarter-note beat and the
   // leftmost visible beat, so clips (stored in ticks) hold still on screen
   // when the tempo changes. 40 px/beat is the old 80 px/s default at 120 bpm.
   // View state only - deliberately not written into Settings::zoom/scroll,
   // since every model write bumps revision and wakes the audio rebuild.
   float gArrangePixelsPerBeat = 40.0f;

   double gArrangeScrollBeats = 0.0;

   // Which side of the timeline lanes the global viewport monitor docks to -
   // toggled via right-click on the monitor itself.
   bool  gArrangeViewportOnRight = false;

   bool  gArrangeShowViewport = false;

   // The snap grid itself is model state (Settings::snapDivision, 0 = off,
   // and snapTriplet - WP6). The magnet button toggles off <-> the last
   // division that was on, remembered here (view state, not saved).
   int   gArrangeLastSnapDivision = 4;

   // Ruler scrub (WP6): a drag on the ruler moves a ghost playhead only, and
   // the transport seeks once, on release (ArrangeScrubEnd). Seeking every
   // drag frame bumped Transport's reset epoch every frame, which reset every
   // Field/stateful node's history for the whole drag.
   bool  gArrangeScrubbing = false;

   int64_t gArrangeScrubTick = 0;

   // Which mouse button is driving the current scrub (step 2: a Sample's
   // body scrubs on middle-click-drag, deliberately not left-click, so it
   // can never collide with the left-click select/drag/trim/blade hit-
   // testing every clip already has - see the per-clip loop in
   // DrawArrangePanelContent). The ruler's own release-check below reads
   // this instead of a hardcoded left button so either source ends cleanly.
   ImGuiMouseButton gArrangeScrubButton = ImGuiMouseButton_Left;

   // Marker flag being dragged on the ruler (0 = none), and the inline rename.
   uint64_t gArrangeMarkerDragId = 0;

   int64_t gArrangeMarkerDragGrabTick = 0;

   int64_t gArrangeMarkerDragOrigPos = 0;

   uint64_t gArrangeRenamingMarkerId = 0;

   char gArrangeRenameMarkerBuffer[64] = {};

   uint64_t gArrangeCtxMarkerId = 0;

   // Loop region: Shift+drag on the ruler sets [start,end) and arms it;
   // right-clicking the ruler while armed disarms it. The loop itself is
   // gArrange.settings.loop, in ticks (WP5b) - these are only the drag's
   // transient state. The anchor is a tick so the band does not slide when
   // the tempo changes mid-drag.
   bool   gArrangeShiftDraggingLoop = false;

   int64_t gArrangeLoopDragAnchorTick = 0;

   ArrangeLoopDragMode gArrangeLoopDragMode = kArrangeLoopDragNone;

   Arrange::Tick gArrangeLoopDragOrigStart = 0;

   Arrange::Tick gArrangeLoopDragOrigEnd = 0;

   Arrange::Tick gArrangeLoopDragGrabTick = 0;

   ArrangeTool gArrangeTool = ArrangeTool::Select;

   bool  gArrangeBladeOn = false;
 // Kept in sync with gArrangeTool == ArrangeTool::Blade
   bool  gArrangeHandDragging = false;

   bool  gArrangeZoomDragging = false;

   ImVec2 gArrangeZoomDragStart(0.0f, 0.0f);

   float  gArrangeZoomDragStartPpb = 40.0f;

   double gArrangeZoomDragStartBeats = 0.0;

   // A clip Add to Timeline just made: the panel scrolls it into view once
   // (it can land past the right edge, or on a lane below the fold) and
   // pulses its outline so it is found at a glance.
   uint64_t gArrangeRevealClipId = 0;

   int      gArrangeRevealFrames = 0;

   uint64_t gArrangeFlashClipId = 0;

   double   gArrangeFlashStart = 0.0;

   bool  gArrangeClaimedKeys = false;

   bool  gArrangeFocused = false;

   AudioMode gAudioMode = AudioMode::Canvas;

   // Where the per-row "+" (or the empty-state one when there are no
   // tracks yet) should insert a new track: -1 appends at the end,
   // otherwise inserts right after that stream index. Set when the "+" is
   // clicked, read back when the popup it opens is actually filled in.
   int   gArrangeAddTrackInsertAfter = -1;

   ImVec2 gArrangePanelRectMin(0.0f, 0.0f);

   ImVec2 gArrangePanelRectMax(0.0f, 0.0f);

   bool  gPerfEditMode = true;
            // true = Edit (rearrange/customize/cable drag), false = Perform (live perform)
   int   gPerfActivePage = 0;

   int   gPerfRenamingPage = -1;

   char  gPerfRenamePageBuffer[64] = "";

   int   gPerfAssigningElemIdx = -1;

   int   gPerfAssigningAxis = 0;
 // 0 = X or primary, 1 = Y
   // Predictive Drift's "Pick Leader" (Follow mode): the uid of the Drift node currently
   // armed to bind its leader from a canvas click, or 0 when nothing is picking. Keyed by
   // uid rather than node index so it survives a delete/undo elsewhere on the canvas while
   // the picker is still armed. Same click-to-assign UX as gPerfAssigningElemIdx below.
   uint64_t gDriftFollowPickingUid = 0;

   int   gPerfMidiLearnIdx = -1;

   int   gPerfMidiLearnAxis = 0;
 // 0 = X or primary, 1 = Y
   // Per-parameter MIDI learn (right-click a param > "MIDI learn"). Keyed by the
   // node's uid, not its index, because undo/load renumber indices. Exactly one
   // MIDI learn may be live at a time across ALL learners (this, the perf
   // matrix above, and the MIDI CC / MIDI Trigger node buttons): every start
   // goes through MidiLearnCancelAll() first. Defined after
   // UpdatePerformanceMatrixMIDI.
   uint64_t gParamMidiLearnUid = 0;

   int      gParamMidiLearnParam = -1;

   std::map<size_t, PerfMidiRuntimeState> gPerfMidiRuntimeStates;

   std::map<size_t, float> gPerfBangFlash;

   std::vector<ParamPinScreenInfo> gParamPinScreenList;

   int   gPerfRenamingElementIdx = -1;

   char  gPerfRenameElementBuffer[64] = "";

   Patch::PerfLayoutRecord gPerfLayout;

   std::vector<Patch::PerfRecord> gPerfElements;

   // Arrangement timeline (docs/plans/arrangement/overhaul-prompt.md).
   //
   // gArrange is the source of truth: ticks, stable clip ids, markers and
   // settings, and the only thing that is saved, loaded, undone or edited -
   // every panel edit goes through an Arrange:: op (WP5a). The audio
   // schedule, the video layer walk and the panel all read it directly
   // (WP5b deleted the seconds mirror), and gArrange.revision is the one
   // change signal: every op and every direct field edit bumps it, and the
   // audio topology rebuilds when it moves (ArrangeAudioRebuildIfStale).
   Arrange::Model gArrange;

   // Bumped on every new-document boundary (File > New, File > Open) and never
   // by undo. Anything that must not outlive the document it was made in -
   // the clip clipboard, the selection - carries the generation it was made
   // under and is dropped when it no longer matches.
   uint64_t gArrangePatchGeneration = 1;


   // Selection is by clip id, never by (lane, index): an index is only valid
   // until the next edit, an id for the life of the clip. Ids that stop
   // resolving (deleted, undone away) are pruned every frame, so a stale id
   // clears rather than landing on a different clip.
   std::set<uint64_t> gArrangeSel;

   uint64_t gArrangeSelAnchor = 0;
          // last plain-clicked clip; paste lands on its lane
   uint64_t gArrangeSelGeneration = 1;
      // gArrangePatchGeneration the selection belongs to

   // Multi-row selection over the arrange header column: tracks and group
   // headers share one id space (Model::nextId mints both), so a plain id
   // set covers either without a tagged key. Global (not function-local) so
   // the keyboard-shortcut block, which runs before the row layout prepass
   // in DrawArrangePanelContent, can read it to decide whether Shift+D,
   // Delete and Cmd+G act on rows or on the clip selection (gArrangeSel) -
   // rows win when non-empty, matching "selection is the context" for every
   // one of these shortcuts.
   std::set<uint64_t> gArrangeRowSel;

   uint64_t gArrangeRowSelAnchor = 0;
 ArrangeClipboardData gArrangeClipboard;


   // One gesture's pre-edit snapshot (a clip drag, a popup DragFloat, a
   // rename). Pushed as a single undo entry at the gesture's end, and only if
   // the model's revision moved - a click that changed nothing pushes nothing.
   Arrange::Model gArrangeGestureBefore;

   bool gArrangeGestureOpen = false;
 ArrangeDragState gArrangeDrag;
 ArrangeMarqueeState gArrangeMarquee;


   // Rename, context-menu and Assign Node... targets, all by id.
   uint64_t gArrangeRenamingClipId = 0;

   char     gArrangeRenameClipBuffer[64] = "";

   uint64_t gArrangeRenamingLaneId = 0;
     // row (lane or group) name field currently being edited
   bool     gArrangeRenameJustStarted = false;
 // one-shot: focus the rename field the frame it opens
   bool     gArrangeClipSettingsPanelOpen = false;
   ImVec2   gArrangeInspectorMin(0, 0), gArrangeInspectorMax(0, 0);   // last drawn Clip Settings rect (wheel/pinch must not reach the timeline)
 // docked per-clip inspector panel, toggled from the toolbar
   // Id (clip, lane or group) the inspector was last opened for via a
   // double-click, so a second double-click on the SAME row/clip closes it
   // again instead of just re-opening on the same content (toggle behavior).
   uint64_t gArrangeSettingsPanelTarget = 0;

   float sArrangeLastRulerStartX = 0.0f;

   uint64_t gArrangeMixGestureLaneId = 0;
   // lane whose header S/M/pan/gain/opacity control is mid-gesture
   uint64_t gArrangeCtxClipId = 0;

   uint64_t gArrangeAssigningClipId = 0;

   // When a Rename or Assign Node... is started from a multi-selection, the
   // single gArrangeRenamingClipId/gArrangeAssigningClipId above still names
   // just the anchor clip the inline field/picker is shown against - these
   // hold every other selected id (same lane type only) that should receive
   // the same new name/source on commit. Empty for an ordinary single-clip
   // rename/assign.
   std::vector<uint64_t> gArrangeRenameTargetIds;

   std::vector<uint64_t> gArrangeAssignTargetIds;

   std::vector<ArrangePendingImport> gArrangePendingImports;

   ArrangePendingBrowserDrop gArrangePendingBrowserDrop;

   // Stable node identity, handed out at spawn and never reused, unlike
   // GraphNode::index. Arrangement clips reference it, so a clip survives its
   // node being deleted and undone back. Persisted per node in the patch;
   // ApplyPatchData clamps this above every restored uid so a reload can never
   // mint a duplicate.
   uint64_t gNextNodeUid = 1;

   // Edit-mode selection, by index into gPerfElements. Indices move when the
   // vector is mutated, so every operation that erases or appends clears or
   // rebuilds the selection rather than trying to patch it up.
   std::set<size_t> gPerfSelection;

   std::vector<Patch::PerfRecord> gPerfClipboard;

   // Set from the matrix window each frame. The node editor's copy / paste /
   // duplicate / delete shortcuts are global (they only check for a focused
   // text field), so without this a Shift+D inside the matrix would duplicate
   // the selected controls *and* the selected nodes.
   //
   // ImGui window focus can't decide this on its own: the matrix is a child
   // region of the *same* root window the node editor canvas lives in, so
   // IsWindowFocused(RootAndChildWindows) is true for both surfaces at once.
   // Instead we track which surface the user last clicked in - the matrix
   // claims the keys when clicked inside it, and gives them back the moment a
   // click lands anywhere else (the canvas, the toolbar, a node).
   bool gPerfMatrixFocused = false;

   bool gPerfMatrixClaimedKeys = false;

   ImVec2 gPerfPanelRectMin(0.0f, 0.0f);

   ImVec2 gPerfPanelRectMax(0.0f, 0.0f);

   std::map<std::pair<int, int>, float> gPerfPendingWrites;

   int   gPerfDragIdx = -1;

   int   gPerfDragOriginCellX = 0;

   int   gPerfDragOriginCellY = 0;

   ImVec2 gPerfDragMouseStart(0.0f, 0.0f);

   int   gCableVisibilityMask = 0x7;
      // bit 0 (0x1) = Image, bit 1 (0x2) = Audio/Note, bit 2 (0x4) = Mod
   ImVec2 gViewCenterCanvas(0.0f, 0.0f);
 // captured inside the editor for spawning
   ImVec2 gGraphScreenTL(0.0f, 0.0f);
    // graph canvas's screen-space rect,
   ImVec2 gGraphScreenSize(0.0f, 0.0f);
  // captured the same way, for the minimap overlay
   bool gMinimapEnabled = false;

   std::vector<ProjectorWindow> gProjectorWindows;


   // Who paces the frame loop: one frame clock. Unless an offline render is
   // running, the loop waits on a display's refresh clock once per frame,
   // after the canvas swap and just before the projectors present (see
   // PaceProjectorPresent), and every context presents at swap interval 0.
   // The display is the primary Output window's while a projector is open,
   // else the canvas window's when Vsync is on. With Vsync off and no
   // projector (and always during an export) nothing waits and the loop runs
   // unpaced or on the Target FPS limiter. The canvas's own blocking swap no
   // longer paces anything: macOS skips it for an occluded window and on
   // this machine returns on a 120 Hz grid on a 60 Hz display.
   // gCanvasSwapInterval is what the canvas asks for (Vsync setting, 0 during
   // an export); every site that used to call glfwSwapInterval on the main
   // context goes through SetCanvasSwapInterval, and ApplyCanvasSwapInterval
   // runs at the top of every frame, so no frame waits twice.
   int gCanvasSwapInterval = 1;

   int gAppliedCanvasSwapInterval = -1;

   // Bottom-left by default: the module browser docks on the right, and the
   // minimap draws (and takes its clicks) on the foreground draw list, so a
   // right-hand corner would sit on top of the panel and swallow clicks meant
   // for the module list.
   int gMinimapCorner = 2;
 // 0=TL, 1=TR, 2=BL, 3=BR
   float gMinimapSize = 190.0f;

   float gMinimapOpacity = 0.85f;

   float gZoomSensitivity = 0.5f;

   bool gHoveringItem = false;
   // last frame: cursor over a node/pin/link
   bool gPanWithLeft = false;
    // current left-drag is a canvas pan, not a select
   ImVec2 gDragTestNodeScreen(0.0f, 0.0f);

   ImVec2 gDragTestNodePos(0.0f, 0.0f);

   ImVec2 gTestMouse(0.0f, 0.0f);

   ImVec4 gPredTestSliderScreen(0, 0, 0, 0);

   ImVec4 gPredTestSliderCanvas(0, 0, 0, 0);
 // canvas space, captured while the slider draws
   int gPredTestNodeIndex = -1;

   int gPredTestSizeXParam = -1;
 // INFINITE_PREDBINDTEST: the Shape "size x" param the mouse is aimed at
   // Screen rects of the Wavetable node's draggable visualizers, republished
   // every frame the node draws: (frames, amp, pitch, filter) per engine, in
   // column order. Only INFINITE_WTDRAGTEST reads them - it is the one thing
   // that cannot be checked from a screenshot, because "the picture is in the
   // right place" and "dragging the picture moves this engine's parameter"
   // are different claims, and the second is the one that broke.
   std::vector<ImVec4> gWtTestRects;

   bool gWtDragOk = true;

   // Same rects converted to screen space. The conversion cannot happen at
   // capture time: ed::CanvasToScreen hangs when called from inside
   // ed::Begin/End (and from before ed::Begin), and the one place it is known
   // to be safe is the post-editor block, where DRAGTEST already calls it. So
   // the capture stays canvas-space and the block below republishes it each
   // frame; the synthetic mouse then aims with the previous frame's rects,
   // which is fine for a static node.
   std::vector<ImVec4> gWtTestScreen;

   ImVec2 gDragTestViewAnchor(0.0f, 0.0f);


   // EQ's own drag-verification rect, same reasoning as gWtTestRects/
   // gWtTestScreen above: captured in canvas space inside DrawEqVisualizer
   // (cheap, so published unconditionally rather than only under the test
   // flag), converted to screen space once per frame in the post-editor
   // block (the only place ed::CanvasToScreen is safe to call), and read by
   // INFINITE_EQDRAGTEST's synthetic mouse a frame later.
   ImVec4 gEqTestRect(0.0f, 0.0f, 0.0f, 0.0f);

   ImVec4 gEqTestScreen(0.0f, 0.0f, 0.0f, 0.0f);

   DropdownRequest gDropdown;


   // Self-test hook (INFINITE_MODDROPDOWNUNDOTEST only): the (node, discrete
   // param) whose dropdown should act as if its button were clicked on the
   // next draw, so the test gets the caller's real onSelect lambda into
   // gDropdown without aiming a synthetic mouse through the canvas transform.
   // Consumed on first match; {-1, -1} (the default) never matches.
   std::pair<int, int> gDropdownTestOpenKey(-1, -1);


   // True only between ed::BeginNode/ed::EndNode for the node currently being
   // drawn - i.e. while ImGui coordinates are in the node editor's local
   // canvas space and need ed::CanvasToScreen() to become real screen pixels.
   // DropdownButton is also called from plain ImGui windows outside any node
   // (e.g. the docked browser panel's sort/type filter, DrawBrowserFilterStrip)
   // where GetItemRectMin/Max() is already real screen space and running it
   // through CanvasToScreen would silently apply a stale/unrelated pan+zoom.
   bool gInsideNodeCanvas = false;

   ColorRequest gColor;

   ImVec4 gColorPickerRect(0, 0, 0, 0);

   CommentEditRequest gCommentEdit;

   // Screen-space rect of the last comment body drawn, so the dev test can aim
   // a synthetic double-click at it. Inside a node ImGui draws in canvas space,
   // hence the explicit conversion where this is filled in.
   ImVec4 gCommentBodyRect(0, 0, 0, 0);

   FieldDeviceSaveRequest gFieldDeviceSave;

   std::map<std::string, FieldDeviceLibraryCache> gFieldDeviceLibrary;

   // Screen-space rect of whichever comment is currently open for editing,
   // refreshed every frame so the edit popup can be pinned exactly on top of
   // it - the point being that typing looks like it happens straight into the
   // node's own box rather than in a separate window somewhere else on screen.
   ImVec4 gCommentEditRect(0, 0, 0, 0);
 // x, y, w, h
   float gCommentEditZoom = 1.0f;

   FormulaNode* gFormulaEditor = nullptr;

   bool gFormulaEditorOpen = false;

   FieldElementNode* gFieldElementEditor = nullptr;

   bool gFieldElementEditorOpen = false;

   FieldPrimitiveNode* gFieldPrimitiveEditor = nullptr;

   bool gFieldPrimitiveEditorOpen = false;

   FieldPixelNode* gFieldPixelEditor = nullptr;
   SketchNode* gSketchEditor = nullptr;
   bool gSketchEditorOpen = false;

   bool gFieldPixelEditorOpen = false;

   FieldSampleNode* gFieldSampleEditor = nullptr;

   bool gFieldSampleEditorOpen = false;

   FieldSynthNode* gFieldSynthEditor = nullptr;

   bool gFieldSynthEditorOpen = false;

   // Live-preview state for the Field editor windows. One editor of each
   // kind can be open at a time (singleton gField*Editor above), so a single
   // dedicated instance is enough - no per-node map needed like the node
   // body's own mini-viewports (gNodeViewports/gNodeCameras).
   NodeViewport gFieldElementEditorViewport;

   SharedViewportCamera gFieldElementEditorCamera;

   FieldGraphNode* gFieldGraphEditor = nullptr;

   bool gFieldGraphEditorOpen = false;

   // Set by a "Regenerate" button click inside DrawFieldGraphParams (nested
   // in ed::Begin()/ed::End()); drained once, outside that pass, after
   // ed::End() returns - see trap T14 and RunFieldGraphRegenerate.
   FieldGraphNode* gFieldGraphPendingRegenerate = nullptr;


   // Build step 16 ("Unpack to Canvas"). Same trap-T14 shape as
   // gFieldGraphPendingRegenerate just above: set by the "Unpack to Canvas"
   // button click inside DrawFieldGraphParams (nested in ed::Begin()/
   // ed::End()), drained once after ed::End() returns by
   // RunFieldGraphUnpackPhase1 - see that function's comment.
   FieldGraphNode* gFieldGraphPendingUnpack = nullptr;

   FieldGraphUnpackPhase2State gFieldGraphUnpackPhase2;

   bool gHelpOpen = false;

   bool gShortcutsOpen = false;

   bool gNavOwnsKeys = false;
#ifndef NDEBUG

   // ImGui's own inspector windows, wired in for exact-value UI review: the
   // Metrics/Debugger's "Tools > Item Picker" reports the ImGuiCol_*/rect of
   // whatever you click on, and the Style Editor lists every style colour
   // and size with live values and a live preview - point at a control here
   // instead of eyeballing a screenshot to pin down which knob to change.
   // Dev-only: excluded from Release builds (NDEBUG) so shipped/public
   // builds never expose these.
   bool gUiDebuggerOpen = false;

   bool gUiStyleEditorOpen = false;
#endif

   bool gSettingsOpen = false;

   bool gShowUpdateCheckModal = false;
   bool gShowAboutModal = false;


   // Files dropped on the window, consumed on the next frame so the spawn can
   // happen inside the editor where canvas coordinates are meaningful.
   std::vector<std::string> gDroppedFiles;

   ImVec2 gDropPos(0.0f, 0.0f);


   // Shared by the OS file-drop handler and the Media search panel's
   // canvas drag-and-drop resolution, so both classify a given path
   // identically (see MediaExtensions.h).
   const std::vector<std::string>& kVideoExt = MediaExtensions::Video();

   const std::vector<std::string>& kImageExt = MediaExtensions::Image();

   BrowserFavorites gBrowserFavorites;


   extern bool gPatchDirty;


   // ---- modulatable parameters --------------------------------------------
   // Every slider that can be driven by a modulator goes through ModSlider. It
   // draws an input pin beside the control, registers the parameter for this
   // frame so the modulator can write into it, and shows the live value (and
   // locks the slider) while something is patched in.
   int gCurrentNodeIndex = -1;

   int gParamCounter = 0;

   // Colour pins are counted separately from parameter pins (see
   // GraphNode::kColorBase): sharing the counter would renumber every slider
   // that follows a swatch and repoint existing patches' modulation.
   int gColorCounter = 0;
 // 400..799
   int gDiscreteParamCounter = kDiscreteParamBase;


   // Discrete params get their ordinal from their *label*, not from draw
   // order. A draw-order counter cannot survive a node that swaps one control
   // for another when its mode changes - the note sequencer draws `rate` as a
   // dropdown in sync mode and as a knob in free mode, so every discrete param
   // below it shifted by one the instant the mode moved, and a cable patched
   // into `rate` silently re-pointed itself at `Snap to Key`. Hashing the
   // label instead pins each control to a fixed address whether or not it drew
   // this frame, so a cable into a control that is currently hidden just goes
   // inactive (the link is skipped when its pin wasn't declared - see the
   // gDrawnParamPins check in the link pass) and reattaches to the same
   // control when the mode brings it back.
   std::map<std::pair<int, std::string>, int> gDiscreteSlotByLabel;

   std::map<std::pair<int, int>, std::string> gDiscreteLabelBySlot;


   // Set while a node draws a repeated sub-panel (a Wavetable engine column,
   // and anything else that draws the same set of controls more than once in
   // one node). Discrete params take their ordinal from a hash of their label
   // alone, so two sub-panels drawing the same label collapse onto one slot -
   // one pin id emitted twice in one frame. imgui-node-editor links a node's
   // pins into a list as they are emitted, so the second emission makes the
   // list circular and the next hit-test pass spins forever: a hard hang, on
   // any patch containing that node, as soon as the cursor is over the canvas.
   // (BeginPin also guards against this now - see the duplicate-id block in
   // imgui_node_editor.cpp - but that guard costs the second control its pin,
   // so the ids still have to be distinct here.)
   //
   // ImGui::PushID() does not help: it scopes widget ids, not these ordinals.
   //
   // The first sub-panel deliberately keeps the empty scope, so its slots hash
   // exactly as they did before this existed and every binding in an already-
   // saved patch still resolves to the same control.
   std::string gDiscreteSlotScope;

   std::set<int> gDrawnColorPins;

   std::set<std::pair<int, int>> gTypedParam;
                 // params showing a text field
   // Params that entered typing mode via hover+type rather than double-click;
   // these get the cursor placed at the end instead of a full selection, so
   // the seeded digit is appended to rather than replaced by the next keystroke.
   std::set<std::pair<int, int>> gTypedParamNoAutoSelect;

   // Params whose InputFloat selection/cursor state still needs to be forced
   // once the field is confirmed active (see the comment at its use site).
   std::set<std::pair<int, int>> gTypedParamPendingInit;

   // The exact text a double-click opened the field with. If the field closes
   // still holding it, nothing was typed, so the value must stay untouched:
   // the seed is the value rounded to the display format ("%.0f" turns 0.4
   // into "0"), and re-parsing it on close snapped the param to that rounded
   // number - most visibly to 0 - whenever the field lost focus straight away
   // (a stray click, or the second click of the double-click landing on it).
   std::map<std::pair<int, int>, std::string> gTypedParamSeed;

   // Live text for the field currently being typed into. A plain number
   // commits as a value and clears any expression; text starting with '='
   // commits as an expression (see ModSlider) - the same field doubles as
   // both, like a spreadsheet cell.
   std::map<std::pair<int, int>, std::string> gTypedParamText;

   // Param pins declared this frame. A node with its params collapsed declares
   // none, and emitting a link to an undeclared pin makes the editor treat the
   // link as dead and delete it - which silently dropped the modulation.
   std::set<int> gDrawnParamPins;
   std::map<int, ImVec2> gPinAnchors;
   std::map<int, int> gPinAlias;
   void NoteHiddenParamAlias(int nodeIndex, int hiddenParam, int shownParam)
   {
      const int base = nodeIndex * GraphNode::kStride + GraphNode::kParamBase;
      gPinAlias[base + hiddenParam] = base + shownParam;
   }
   void NoteHiddenParamAnchor(int nodeIndex, int paramIndex, const ImVec2& screenPos)
   {
      gPinAnchors[nodeIndex * GraphNode::kStride + GraphNode::kParamBase + paramIndex] = screenPos;
   }

   // Set while a collapsed node runs its parameter dispatch purely to
   // re-register its params (see the params block in the node loop). Every
   // Mod* widget still assigns its ordinal and registers its ParamRef, then
   // returns immediately: no pin, no widget, no drawing. A modulator writes
   // through the pointer RegisterParam hands out, so a param that never
   // registers is a param nothing can drive - which is why closing a node's
   // eye used to silently freeze every modulator patched into it. The pins a
   // collapsed node needs are declared separately by CollapsedBindingPins,
   // so the widgets must NOT declare them again here: ed::BeginPin twice on
   // one id in a frame is undefined.
   bool gParamRegisterOnly = false;

   // Nodes to select next frame; they do not exist in the editor until they
   // have been drawn once, so selection has to wait a frame.
   std::vector<int> gPendingSelect;

   std::pair<int, int> gTypedParamJustOpened(-1, -1);

   std::vector<int> gParamPinsThisFrame;

   // Set when a right-click on a param already opened its text field this
   // frame, so the node-level right-click context menu (checked later, after
   // ed::Suspend()) knows to stay closed instead of covering the field.
   bool gParamRightClickConsumedThisFrame = false;


   // ---- audio node value readout -----------------------------------------
   // v2 showed a hovered knob's value via ImGui::SetTooltip from inside
   // ed::Begin()/ed::End() with no ed::Suspend() around it, so the node
   // editor's canvas transform was applied to the tooltip and it landed
   // offset from the cursor by an amount proportional to zoom and pan (v3
   // §1a - the "hover value floats somewhere else" report).
   //
   // The fix is not a correctly-positioned tooltip: it's not having one.
   // Every plugin this UI is benchmarked against puts the hovered/dragged
   // param's name and value in ONE fixed readout location per panel, which
   // never occludes the control being adjusted and never moves. A control
   // writes here; BeginAudioBody draws last frame's string in the node's
   // readout strip. The one-frame lag is imperceptible, and it buys a
   // strictly top-down layout with no second pass.
   std::map<int, std::string> gAudioReadout;


   // ---- discrete (bool / enum) modulation --------------------------------
   // A mode dropdown and a checkbox are parameters like any other: the only
   // reason they were not modulatable is that the apply pass writes through a
   // float*, and neither an int enum index nor a bool has one to point at.
   // These three helpers supply the missing float (same stable-address map
   // trick as gIntParamStore) plus the pin half of ModSlider, so a cable can
   // be dragged into a dropdown or a checkbox and the performance matrix can
   // target one - both of which go through the exact same machinery as a
   // float param, keyed by (nodeIndex, paramIndex).
   std::map<std::pair<int, int>, float> gDiscreteParamStore;

   // What the widget itself last put in the slot. Anything else in there on
   // the next frame was written by someone outside the widget - today that
   // means the performance matrix's deferred write queue, which writes through
   // ParamRef::value and would otherwise be silently overwritten by the
   // widget re-seeding the slot from the node.
   std::map<std::pair<int, int>, float> gDiscreteParamLastWritten;


   // The member a wrapper widget (ModSliderInt/ModKnobInt/ModCheckbox, a dropdown
   // that knows its int) is editing, handed to the ModSlider/ModKnob/
   // RegisterDiscreteParam it wraps so the ParamRef can say which saved key it
   // is (ParamRef::srcAddr). Set immediately before that call, consumed by it.
   const void* gPendingSrcAddr = nullptr;


   // ---- prediction (green) bindings ------------------------------------------
   // A binding whose source node is an IPredictor. The apply loop writes it in fader space, and a
   // Shift-grab on the widget (only Shift: a plain click keeps the lock every other cable follows)
   // suspends that write for as long as the hand holds the control. State is per frame and cleared
   // beside ClearFrameParams(); `Prev` is last frame's set, which is how a widget knows a drag that
   // began under Shift may continue after Shift is released.
   std::set<ParamKey> gPredictorGrabs;

   std::set<ParamKey> gPredictorGrabsPrev;


   // Integer parameter that is still modulatable. The backing float lives in a
   // std::map so its address stays valid for the whole frame (node references are
   // stable), which matters because the modulation pass writes through that pointer.
   std::map<std::pair<int, int>, float> gIntParamStore;


   // The last integer this widget itself wrote back, per param. Every
   // ModXxxInt widget needs this, not just the ones that accumulate a drag
   // delta: IsModulated only sees a wired cable, so a value pushed by an
   // expression or a gesture/mod recorder's playback (main.cpp's UI draw runs
   // before ApplyModulationAndPalette applies those each frame) looks
   // unmodulated here and would otherwise get overwritten by the reseed on
   // the very next frame's draw.
   std::map<std::pair<int, int>, int> gIntParamLastWritten;


   // GlobalScaleToggle runs per-node, inside ed::Begin()/ed::End(), nested in
   // that node's own layout - not a safe place to call ed::Suspend()/Resume()
   // (that pairing is only ever used once, after the whole node-draw loop
   // finishes, for the popups/minimap/perf-matrix overlay block). Calling it
   // here corrupted ImGui's window/ID state for the rest of the frame and
   // made the app's top toolbar disappear while hovering the icon. So the
   // hover just records intent here; the tooltip itself is drawn later, in
   // that existing post-loop suspended block, where real screen-space
   // tooltips are already known to be safe.
   bool gGlobalScaleTooltipHovered = false;

   bool gGlobalScaleTooltipEnabled = false;


   // uid -> GraphNode*, backing FindNodeByUid (WP5b). The arrangement asks for
   // a node by uid per clip per frame (panel draw, video layer walk, audio
   // schedule), so the linear scan it replaced was O(clips x nodes) a frame.
   //
   // Kept honest three ways, so a mutation site that forgets to report in
   // costs a rebuild, never a wrong answer:
   //  - SpawnNode appends in place (NoteNodeAppended); erase, clear and a
   //    uid restore on load invalidate or patch it (InvalidateNodeByUid,
   //    NoteNodeUidChanged).
   //  - A lookup rebuilds whenever gNodes' storage or size moved since the
   //    map was built - a reallocation dangles every pointer in it.
   //  - The main loop invalidates it once a frame, and a hit is re-checked
   //    (uid still matches) before it is returned.
   // First uid wins on a duplicate, which is what the linear scan did.
   std::unordered_map<uint64_t, GraphNode*> gNodeByUid;

   bool gNodeByUidDirty = true;

   const GraphNode* gNodeByUidData = nullptr;

   size_t gNodeByUidSize = 0;

   std::map<std::string, ParamJoinType> gParamJoin;


   // Headless --describe: every button a node draws, by node index (ImGui::ButtonLabelHook).
   std::map<int, std::vector<std::string>> gProbeButtons;

   PatternGridDragState gPatternGridDrag;


   // ======================================================================
   // v3 audio node layout (docs/plans/audio/audio-node-ui-system.md v3)
   // ======================================================================
   //
   // The load-bearing idea: an audio node's width is a *scope*, not a
   // constant that helpers happen to also use. v2 declared kAudioNodeWidth
   // and then drew the visualizer at 440, NodeSeparator at kPreviewSize
   // (190) and the knob row at whatever N knobs added up to (~556), so one
   // body held three widths and the node grew to the widest of them -
   // nothing shared an edge with anything, which is the single biggest
   // reason the result read as unfinished.
   //
   // Here BeginAudioBody sets the width once and every helper below derives
   // from it. Rows are laid out to *fit* the body; they cannot set it.
   float gAudioBodyW = kAudioNodeWidth;
     // enforced body width
   float gAudioBodyX = 0.0f;
                // body's left edge, screen space
   float gAudioContentX = 0.0f;
             // left edge of the current column
   float gAudioContentW = kAudioNodeWidth;
  // width of that column
   CategoryColors::Color gAudioTint{ 0.5f, 0.6f, 0.8f };


   // Section panels measure their own height and reuse it next frame to draw
   // their background behind their content (a background has to be drawn
   // before the content it sits behind, and ImGui only knows the height
   // afterwards). Keyed by node so nodes never share a cache; the height
   // only changes when a node's layout changes, so the one-frame lag is
   // visible exactly once, on the frame a pattern/waveform switch resizes a
   // panel. Splitting the node editor's draw list to do it in one pass was
   // the alternative and is not worth the risk to ed's own channel usage.
   std::map<std::pair<int, int>, float> gAudioSectionHeight;

   int gAudioSectionIndex = 0;

   float gAudioSectionTop = 0.0f;


   // Per-node peak-hold state for the level meter. Main-thread only: it
   // decays a value already published by CookIfNeeded and never reaches into
   // an AudioNode, so it adds no thread-safety surface.
   std::map<int, float> gAudioMeterPeak;

   std::map<int, double> gAudioMeterPeakTime;

   AudioColumnState gAudioColumn {};

   std::map<std::pair<uint64_t, ParamKey>, DriftTrace> gDriftTraces;

   DrumGridDragState gDrumGridDrag;

   ArpGateDragState gArpGateDrag;

   std::map<int, FilterCurveCache> gFilterCurveCache;

   int gFilterCurveRecomputes = 0;

   std::map<int, AudioSpectrumState> gAudioSpectrumCache;

   std::map<int, EqCurveCache> gEqCurveCache;

   std::unordered_map<AudioEffectNode*, SpecBlurSpectrumState> sSpecBlurSpectrums;

   std::unordered_map<AudioEffectNode*, KeySnapSpectrumState> sKeySnapSpectrums;


   // ---- Spectrum Slide -------------------------------------------------
   std::unordered_map<AudioEffectNode*, KeySnapSpectrumState> sSpectrumSlideSpectrums;


   // Declared further down (with the rest of the patch-state globals); used
   // here to name a fresh recording after the open patch.
   extern std::string gPatchPath;

   extern bool gPatchDirty;


   // Rolling history so a modulator reads like a scope rather than a number.
   std::map<int, std::vector<float>> gModHistory;


   // Per-node mini 3D viewport GL state, keyed by GraphNode::index - same
   // per-index-map pattern as gModHistory above. Lazily created the first time
   // a node's viewport toggle is switched on; nothing is allocated for a node
   // that never opts in.
   std::map<int, NodeViewport> gNodeViewports;


   // Every node's own default-camera rotation (SharedViewportCamera.h),
   // keyed by GraphNode::index. A node's inline mini-viewport (above) and its
   // viewport-panel card (gPanelViewports below) both read/write the same
   // entry here, so the two agree on rotation even though they're separate
   // NodeViewport/FBO instances; two different nodes never share an entry.
   // Render3DNode keeps its own completely separate camAzimuth/camElevation
   // fields (Geometry3DNodes.h) - the final output's framing is a deliberate
   // setup, not something casual node-viewport orbiting should ever move.
   std::map<int, SharedViewportCamera> gNodeCameras;

   std::vector<RetiredNode> gRetiredNodes;

   std::vector<std::map<int, NodeViewport>::node_type> gRetiredViewports;


   // Viewport-panel 3D renders, kept in their own map rather than sharing
   // gNodeViewports with the inline per-node toggle above: the two draw at
   // different sizes, and one NodeViewport reallocates its FBO whenever the
   // requested size changes - sharing would make a node that is open in both
   // places thrash its FBO every frame.
   std::map<int, NodeViewport> gPanelViewports;


   // Same reasoning as gPanelViewports above, one more map for projector
   // windows: a geometry node has no meaningful GetOutputTexture() of its own
   // (see GeometryNode::GetOutputTexture's comment - "geometry itself
   // produces no image"), so a projector window showing one needs its own
   // solo render, same mechanism the viewport panel/mini-viewport use, sized
   // to that window rather than thrashing a shared FBO.
   std::map<int, NodeViewport> gProjectorViewports;

   std::map<std::pair<int, int>, SparklineHistory> gModMatrixSparklines;

   ArrangeCompositeTarget gArrangeMonitorTarget{ 0 };

   ArrangeCompositeTarget gArrangeRenderTarget{ 1 };

   std::map<std::pair<uint64_t, int>, ArrangeGeomViewport> gArrangeGeomViewports;

   uint64_t gArrangeGeomFrame = 0;

   std::unordered_map<uint64_t, ArrangeClipWave> gArrangeClipWaves;

   // Audio Sample static waveforms (step 2): computed once, from the fully-
   // decoded source buffer, in ArrangePollMediaImports right after
   // OpenFromDecoded/the bounce adopt path - never touched by playback.
   // Keyed by clip id like gArrangeClipWaves, but a wholly separate map: a
   // clip can only ever be in one of the two (sampleDropped picks which),
   // and mixing the two caches would let a stale live-fill bucket outlive a
   // clip that has since become (or stopped being) a Sample. Only cleared
   // when the clip is deleted or gets a new source (see the sweep at the
   // bottom of this pass, mirroring gArrangeClipWaves' own prune below).
   std::unordered_map<uint64_t, ArrangeClipWave> gArrangeSampleStaticWaves;

   // std::map for the same reason gArrangeGeomViewports is one: the value
   // owns a GL resource, and a node-based map never relocates it.
   std::map<uint64_t, ArrangeClipThumb> gArrangeClipThumbs;

   ArrangeTypedEditState gArrangeTypedEdit;


   // The last frame on which an inspector value field was hovered or open for
   // typing. The timeline's single-key shortcuts (0 bypasses the selection,
   // M drops a marker, A/T/R/B/Z/H pick a tool) are handled in
   // DrawArrangePanelContent, and ImGui's WantTextInput is still false on the
   // frame a hover+type OPENS a field - so typing "0.3" into Pan bypassed the
   // clip on the '0' before it ever reached the box. Recorded as a frame
   // number and compared with a one-frame slack so it holds whichever order
   // the panel and the inspector happen to draw in.
   int gArrangeFieldHotFrame = -1000;

   std::unordered_map<ImGuiID, ArrangeSliderPendingClick> sArrangeSliderPending;


   // Rebuilds the whole real-time audio topology (the DAG in AudioEngine.h's
   // AudioTopology, not a flat list) from scratch and publishes it. Called
   // after any connect/disconnect/spawn/delete that touches an
   // audio-relevant node or link. A full rebuild rather than an incremental
   // diff is deliberate - see docs/plans/audio/README.md P2: a graph this
   // size is cheap to walk fully, and SetTopology's one-generation-retire
   // design already makes a full rebuild (nodes AND its buffer pool) cheap
   // on the main thread.
   //
   // AudioOutputNode and OutputNode contribute no AudioNode of their own to
   // `order`, but each connected audio input's source buffer becomes a
   // terminal entry wired into that node's capture ring.
   //
   // Set around a loop that would otherwise call this once per node for what
   // is conceptually a single edit (e.g. deleting a multi-node selection), so
   // the caller can do exactly one rebuild after the loop. Declared here
   // rather than next to gSuppressUndoCheckpoints because it must be visible
   // to the early-out below, which runs long before that declaration.
   bool gDeferAudioRebuild = false;

   std::unordered_map<uint64_t, ArrangeTerminalComp> gArrangeTerminalComp;

   // What the currently published topology was built from (WP5b). The audio
   // schedule is a pure function of gArrange (lanes, clips - in ticks, so
   // tempo is not an input: windows are in beats and the audio thread turns
   // beats into samples at the live bpm) and of whether timeline routing is
   // on. RebuildAudioTopology records both every time it runs, whoever calls
   // it; ArrangeAudioRebuildIfStale compares them once a frame.
   //
   // UINT64_MAX is "never built": revision starts at 0 and only climbs, so
   // it can never equal the sentinel and the first frame always builds.
   uint64_t gArrangeAudioBuiltRevision = UINT64_MAX;

   bool gArrangeAudioBuiltRouting = false;

   // Clip ids whose Retrigger flag was suppressed at the last rebuild because
   // their source node is also used by a clip on another lane - one physical
   // node has only one playback position, so two lanes retriggering it
   // independently would stomp each other (see RebuildAudioTopology's
   // `lanesPerSrc` pass). Read by the Clip Settings panel to show an inline
   // warning next to the Trigger dropdown instead of silently no-op'ing.
   std::set<uint64_t> gArrangeRetriggerConflictClipIds;

   // Every RebuildAudioTopology that got past gDeferAudioRebuild. Fixtures
   // read it to prove an edit costs exactly one rebuild and an idle frame none.
   unsigned long long gAudioTopologyRebuildCount = 0;


   // The video counterpart of gArrangeRetriggerConflictClipIds, and the reason
   // it needs one of its own: a video clip does not drive its source node's
   // position at all - VideoSourceNode decodes from the global Transport time
   // and the clip only decides WHETHER that node's current frame is shown (see
   // CollectArrangeVideoLayers). So one node fed to clips on two video lanes
   // does not fight over a playback position the way the audio path does; it
   // does something quieter and more confusing, which is show the same frame
   // twice. Wherever the two clips overlap in time, the upper lane's copy
   // composites over the lower one's identical picture, so the lower clip's own
   // blend mode, opacity and grade appear to do nothing - the classic "I graded
   // the clip and nothing happened" report. Nothing is suppressed here (there
   // is no behavior to suppress, and a deliberate double-composite with
   // different blend modes is a legitimate effect); the clips are recorded so
   // the inspector can say why, exactly like the audio warning.
   //
   // Cached on gArrange.revision rather than recomputed per frame: unlike the
   // audio side there is no rebuild step to hang it off, and the inspector is
   // its only reader.
   std::set<uint64_t> gArrangeVideoSourceConflictClipIds;

   uint64_t gArrangeVideoConflictBuiltRevision = UINT64_MAX;


   // ---- the export queue window (WP7) --------------------------------------

   bool gArrangeShowRenderQueue = false;


   // The export queue window is gone, so a job that fails to start used to
   // leave no trace at all: Render Now closed the popup and nothing happened,
   // with the reason (a refused encoder, a locked file, a missing device)
   // sitting unread in job.message. The reason is held here until the user has
   // seen it in DrawArrangeRenderFailNotice.
   std::string gArrangeRenderFailNotice;

   bool gArrangeRenderFailNoticeOpen = false;


   std::string gPatchPath;
      // "" until the patch has been saved somewhere
   bool gPatchDirty = false;

   std::string gPatchStatus;


   // ---- autosave / crash recovery ----
   bool gAutosaveEnabled = true;

   int gAutosaveSeconds = 60;


   // Set when a periodic autosave write fails (unwritable settings directory,
   // full disk, revoked permissions). PollAutosave() used to discard
   // WriteAutosaveNow()'s result entirely, so autosave could no-op on every
   // frame for an entire session and the user found out only after a crash,
   // when the recovery prompt had nothing to offer. Surfaced in the Autosave
   // menu and logged once, so the failure is visible while there is still a
   // session left to save by hand.
   bool gAutosaveFailed = false;

   bool gAutosaveFailureLogged = false;

   double gLastAutosaveTime = 0.0;


   // Set once at startup (CheckAutosaveRecovery) when the previous run's
   // marker was present and its autosave parsed. The first frame's UI pops
   // the recovery modal; gPendingRecoveryData is what Recover applies.
   bool gShowAutosaveRecoveryModal = false;

   Patch::Data gPendingRecoveryData;

   std::string gAutosaveRecoveryTimestamp;

   // Non-empty only when the marker was present, an autosave file existed,
   // but Patch::Read failed on it - surfaced via gPatchStatus rather than a
   // second modal.
   std::string gAutosaveRecoveryError;


   // Set while ApplyPatchData is repopulating the graph from a snapshot, so
   // the SpawnNode/connect calls it makes don't themselves push new undo
   // checkpoints - that would corrupt the very stack an undo/redo is reading
   // from, and turn opening a 50-node patch into 50 checkpoints.
   bool gSuppressUndoCheckpoints = false;

   std::deque<UndoEntry> gUndoStack;

   std::deque<UndoEntry> gRedoStack;

   PendingAutoLayout gPendingAutoLayout;

   PendingKeyed gPendingKeyed;


   // Shared by PushUndoCheckpoint (freshly captured) and the node-drag
   // checkpoint (captured earlier, at mouse-down, before the drag moved
   // anything - by the time a drag is detected the live positions have
   // already changed, so that caller can't use BuildPatchData() at the point
   // it decides to push).
   // R30: the same schema pass `Infinite --validate` runs, shown on the node.
   // Only the warnings that say something while you are building are surfaced
   // (an empty merge input, a blank Output, a feedback loop); W_UNUSED_NODE and
   // the parameter warnings would fire on every half-wired node and read as
   // noise. The pass reruns 300 ms after the last undoable edit, so a drag or
   // a burst of edits costs one BuildPatchData, not one per frame.
   std::unordered_map<int, std::vector<Headless::Issue>> gLiveIssues;

   unsigned gLiveIssueSerial = 1;

   unsigned gLiveIssueDoneSerial = 0;

   double gLiveIssueEditTime = 0.0;


   // Node-drag checkpoint state. A drag gesture spans many frames of
   // liveX/liveY already having changed, so the pre-drag snapshot has to be
   // taken speculatively at mouse-down and only actually pushed once real
   // movement is confirmed - otherwise a plain click-to-select would push a
   // checkpoint identical to the current state.
   Patch::Data gDragStartSnapshot;

   bool gDragSnapshotValid = false;

   bool gDragSnapshotPushed = false;

   PatchFileStamp gPatchStamp;

   std::string gPatchWatchPath;
 // not gPatchPath: Undo/Redo run NewPatch, which clears that
   double gPatchWatchNextPoll = 0.0;

   bool gPatchChangedOnDisk = false;


   bool gShowUnsavedChangesModal = false;

   // The action to run once the "Unsaved Changes" modal is resolved with
   // something other than Cancel (Save or Don't Save).
   std::function<void()> gPendingUnsavedAction;

   ProjectorPacer gProjectorPacer;

   TailFence gTailCanvasFence;

   TailFence gTailProjFence;
}
