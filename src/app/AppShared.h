#pragma once
// Shared declarations for the code split out of main.cpp.
#include "app/AppCommon.h"

namespace app
{
   // Every node renders its output at this size, square, above its params.
   inline const float kPreviewSize = 190.0f;


   // Render 3D's preview is a viewport you actually work in - orbiting and
   // framing a scene through a thumbnail is not workable.
   inline const float kViewportSize = 340.0f;

extern float gViewportPanelWidth;

extern float gViewportPanelHeight;


   inline const float kViewportPanelMinWidth = 160.0f;


   // Taller than the width floor's equivalent margin: a top/bottom card gives
   // up two full rows (the dock combo/close row, then its own title row)
   // before the image even starts, so a floor sized like the width one left
   // next to nothing for the image itself - see the "so small" screenshot.
   inline const float kViewportPanelMinHeight = 190.0f;


   inline const float kParamWidth = 168.0f;


   // Horizontal audio-node layout (docs/plans/audio/audio-node-ui-system.md
   // v3): audio nodes lay their params out in a wide rack-style strip rather
   // than a narrow vertical stack. v2 declared kAudioNodeWidth but nothing
   // enforced it - the visualizer drew at 440, NodeSeparator drew at
   // kPreviewSize (190), and the knob row grew to whatever it added up to
   // (~556 for Oscillator), so one node body contained three different
   // widths and the node took the widest. v3 makes the width a *scope*
   // (BeginAudioBody/EndAudioBody) that every audio helper reads from, and
   // lays rows out to fit it rather than letting them set it.
   //
   // Two sanctioned widths, nothing else: a full node, and a narrow one for
   // a node with <=2 params (Gain, Splitter, Audio Out) so §7's "a one-param
   // node is visually smaller" is true instead of aspirational.
   inline const float kAudioNodeWidth = 440.0f;


   inline const float kAudioNarrowWidth = 200.0f;


   // Two-column layout width for large 3D nodes (Render 3D, Material)
   inline const float kWideNodeWidth = 476.0f;


   // A third width, for a node that is genuinely two parallel instruments
   // side by side (Wavetable's engine pair). Stacking them vertically at 440
   // put the node near 1600px tall - unreadable, and it forced an A/B tab whose
   // hidden half silently shared its parent's parameter indices, so a
   // modulator patched to engine A's attack drove engine B's the moment the
   // tab flipped. Two columns fixes the height and the aliasing at once,
   // because every param of both engines is now drawn every frame.
   inline const float kAudioWideWidth = 960.0f;


   // Knobs come in two sizes so a row has a visual hierarchy: large for the
   // one or two params that define what the node is doing, small for the
   // rest. A row of identical dials reads as a spreadsheet, which is what v2
   // shipped.
   // One knob size for every audio-node knob. The v3 grammar used two
   // (kKnobLarge for "what the node is", kKnobSmall for the rest) on the
   // theory that size carries hierarchy; in practice it just made every row
   // look unfinished, because the mixed sizes read as an inconsistency rather
   // than as a ranking. Hierarchy comes from grouping and ordering instead.
   inline const float kKnobStd = 56.0f;


   // ---- macro (front-panel control) node body metrics ----------------
   // A macro node's body IS its control - there is no image preview to size
   // it against, so it must not borrow kPreviewSize (190px). It used to: a
   // 56px knob centred in a 190px cell, and flat controls centred vertically
   // in a 190px-tall band they never filled, which is exactly the ring of
   // dead space around every macro node. These three numbers are the whole
   // grammar: one cell width for single controls, one wider cell for the
   // 8-segment ones (radio selector, step gate) that genuinely need it, and
   // one row height every flat control shares so a toggle, a number box, a
   // selector and a step grid all read as the same family.
   inline const float kMacroCell = 112.0f;


   inline const float kMacroWideCell = 176.0f;


   inline const float kMacroRowH = 26.0f;


   inline const float kMacroFaderH = 96.0f;


   inline const float kKnobLarge = kKnobStd;


   inline const float kKnobSmall = kKnobStd;


   inline const float kKnobDiameter = kKnobSmall;


   // Shared height for a time-domain effect graph (Dynamics' transfer
   // curve, Delay's tap graph, Reverb's decay envelope) - one number so the
   // three read as the same instrument family instead of each picking its
   // own. AudioFilter's frequency response keeps its own taller 190px (a
   // curve read for precision at a glance needs more vertical resolution
   // than these three, which are read for overall shape).
   inline const float kAudioTimeVizH = 150.0f;


   // Inset padding inside a section panel (AudioSection).
   inline const float kAudioSectionPad = 8.0f;


   inline const float kPinRadius = 7.0f;


   inline const float kPinHit = 20.0f;

bool IsThemeLight();

ImU32 ScopeBgCol();

ImU32 ScopeBorderCol();

ImU32 ScopeGridCol();

ImU32 ScopeMidLineCol();

ImU32 ScopeTextCol();

extern bool gCheckerboardBackdrop;

extern bool gMetronomeOn;

extern float gMetronomeVolume;

extern bool gMetronomeAccent;

extern bool gMetronomeDirty;

void DrawCheckerboardBackdrop(ImDrawList* dl, ImVec2 origin, ImVec2 br, float rounding = 4.0f);

void DrawCheckerboardBackdrop(ImDrawList* dl, ImVec2 origin, float size, float rounding = 4.0f);

bool TextFocusClaimed();

bool ApplyViewHotkeys(float& azimuth, float& elevation, const std::function<void()>& onWillChange = nullptr);

bool NodeSearchMatches(std::string hay, std::string q);

std::string DisplayName(const std::string& name);

bool IsUserSpawnable(const std::string& name);

std::string NodeTitle(const GraphNode& gn);

void PushUndoCheckpoint();

extern bool gSuppressUndoCheckpoints;

void PushArrangeUndo();

void PushArrangeUndoSnapshot(const Arrange::Model& before);

GraphNode* FindNodeByIndex(int index);

void RemapFieldGraphOwnership(const std::map<int, int>& remap);

INode* FindHardwareDrivenNode();

void StartOfflineRenderSession(OutputNode* n, int width = 0, int height = 0, bool isArrange = false);

void DrawOfflineRenderProgressWindow();

void DrawArrangeWavRenderProgressWindow();

void DrawArrangeClipSettingsChild(float panelW);

void ArrangeCollectClipModBindings(const GraphNode& node,
                                      std::vector<std::pair<int, std::string>>& out);

bool StartAudioEngine(std::string& outError);

extern std::vector<GraphNode> gNodes;

int GetNodeInstanceIndexScan(const GraphNode& targetNode, int* outTotalCount);



   // Cache behind GetNodeInstanceIndex: one entry per gNodes slot, holding the
   // rank/total the scan above would return for that node. Kept exact, not
   // just per-frame, because some callers run outside the canvas draw (the
   // projector window title, the clip inspector, tooltips) right after a
   // spawn/delete/load:
   //  - Rebuilt when gNodes' storage or size moved, when InvalidateNodeByUid
   //    fires (every erase/clear site, patch load, and once per frame from
   //    the main loop), or when a lookup's own slot no longer matches.
   //  - Every node whose title follows a live field (NodeTitleLiveField) is
   //    re-checked on every lookup - a dropdown or modulation changing a
   //    Geometry node's shape mid-frame renames it, which can change the
   //    rank of every other node sharing either the old or the new title.
   //    That check is a few loads per such node, not a NodeTitle() call.
   // typeName is the only other title input and is written once, at spawn.
   struct NodeTitleInstanceEntry
   {
      const INode* node = nullptr;
      uint64_t uid = 0;
      int index = 0;
      const int* liveField = nullptr;
      int liveValue = 0;
      int rank = 0;
      int total = 0;
   };

extern std::vector<NodeTitleInstanceEntry> gTitleInstance;

extern std::vector<size_t> gTitleInstanceLive;

extern bool gTitleInstanceDirty;

extern const GraphNode* gTitleInstanceData;

extern size_t gTitleInstanceSize;

extern int gTitleInstanceRebuilds;

void InvalidateNodeTitleInstances();

int GetNodeInstanceIndex(const GraphNode& targetNode, int* outTotalCount = nullptr);

std::string NodeTitleWithInstance(const GraphNode& gn);

extern std::map<GroupNode*, std::set<int>> gGroupMembers;

extern int gNextIndex;

extern ed::EditorContext* gEditor;

extern GraphNode* gSelfTestFeeder;

extern bool gPaletteTestOk;

extern bool gPaletteTestPending;

extern ImVec2 gSpawnPos;

extern bool gSampleDragActive;


   // Which panel mode started the drag, and therefore what the canvas-release
   // handler should resolve it against. Was a plain bool (Sampler vs Media)
   // until the Plugins mode arrived; a third value in the same variable keeps
   // the release handler a single ladder rather than a bool plus a parallel
   // "is it actually a plugin" flag that could disagree with it.
   enum class LibraryDragKind
   {
      Sample,
      Media,
      Plugin,
      FieldPreset
   };

extern LibraryDragKind gSampleDragKind;

extern std::string gSampleDragPath;

extern std::string gSampleDragName;

extern std::string gFieldDragPresetName;

extern std::string gFieldDragNodeType;

extern std::string gFieldDragNodeCategory;

extern int gFieldDragIndex;

extern std::string gPreviewingSamplePath;

extern std::string gPreviewErrorPath;

extern std::string gPreviewErrorMessage;

extern Platform::PluginDesc gPluginDragDesc;

extern SampleScanner gSampleScanner;

extern SampleScanner gMediaScanner;

extern PluginScanner gPluginScanner;

extern int gSearchPanelMode;

 // 0 = Modules, 1 = Samples, 2 = Media, 3 = Plugins, 4 = Field

   // Shared sort/filter state for the docked node-browser panel's control
   // strip (DrawBrowserFilterStrip, below DropdownButton). One instance per
   // mode, not one shared instance - same reasoning as the four independent
   // `static char` search buffers this replaces (see the comment at
   // DrawLibrarySearchPanel's cache setup): each mode keeps its own sort,
   // filter and in-progress query when the user switches tabs and back.
   // `sortMode` and `typeFilter` are indices into that mode's own option
   // list (see the per-mode DrawBrowserFilterStrip call sites) - their
   // meaning is per-mode, not shared.
   struct BrowserFilterState
   {
      char query[128] = "";
      int  sortMode = 0;
      int  typeFilter = 0;
      bool descending = false;
   };

extern BrowserFilterState gModulesFilter;

extern BrowserFilterState gSampleFilter;

extern BrowserFilterState gMediaFilter;

extern BrowserFilterState gPluginFilter;

extern BrowserFilterState gFieldFilter;

extern ImVec4 gSamplerDragTestRowRect;

extern ImVec2 gSamplerDragTestTargetScreen;

extern bool gSamplerDragTestTargetValid;

extern ImVec4 gMediaDragTestRowRect;

extern ImVec2 gMediaDragTestTargetScreen;

extern bool gMediaDragTestTargetValid;

extern ImVec4 gPluginDragTestRowRect;

extern std::string gPluginDragTestRowId;

extern ImVec2 gPluginDragTestTargetScreen;

extern bool gPluginDragTestTargetValid;

extern int gPluginDragTestPhase;

extern int gSamplerDragTestPhase;

extern int gMediaDragTestPhase;

extern int gLinkDragSourcePin;

extern std::vector<std::pair<std::string, std::string>> gLinkDragSuggestions;



   // Drop-target picker for audio samples dropped on empty canvas (from OS or
   // sample browser). Instead of auto-spawning one hardcoded node, prompts the
   // user with a context menu of all sample-accepting nodes.
   struct AudioDropPickerState
   {
      bool justOpened = false;
      ImVec2 canvasPos{ 0.0f, 0.0f };
      ImVec2 screenPos{ 0.0f, 0.0f };
      std::vector<std::string> paths;
   };

extern AudioDropPickerState gAudioDropPicker;



   struct LinkInfo
   {
      int id = 0;
      int srcPin = 0;
      int dstPin = 0;
   };


   inline const int kLinkIdBase = 4000000;

extern std::vector<LinkInfo> gLinks;

const LinkInfo* FindLink(int id);

bool FieldOutputPinHasLiveCable(int nodeIndex, int outputIndex);

bool CheckFieldLiveCableBridge(int nodeIndex, int slot, bool isOutput);

void DisconnectLinkById(int id);

void DisconnectFieldPinBridge(int nodeIndex, int slot, bool isOutput);

extern bool gSnapToGrid;

extern float gGridSnap;

extern uint32_t gAudioOutputDeviceId;

extern uint32_t gAudioInputDeviceId;

extern double gAudioSampleRate;

extern int gAudioBufferFrames;

extern int gAudioOutputMode;

extern float gAudioOversample;

extern std::string gAudioStartError;



   // ---- Offline Render (non-realtime export) state ----
   // One take at a time, across the whole patch (not per-OutputNode) - the
   // graph and AudioEngine are only ever driven by one clock at once, so two
   // Output nodes each trying to run their own offline session concurrently
   // would just race each other over the same Transport/AudioEngine calls.
   struct OfflineRenderState
   {
      bool active = false;         // a take is either rendering or finalizing
      OutputNode* node = nullptr;  // which OutputNode owns this take
      bool includeAudio = false;   // latched at start, mirrors node->includeAudio
      bool deviceWasRunning = false; // AudioEngine::Instance().SampleRate() > 0 before this take
      bool wasPlaying = true;        // Transport::Instance().IsPlaying() before this take
      bool vsyncWasOn = true;        // gVsync before this take; restored when it ends
      double startTime = 0.0;        // glfwGetTime() when the take was armed
      double startSeconds = 0.0;     // Transport::Instance().Seconds() when take was armed
      // Set by the pump whenever it yields because the encoder queue is full
      // rather than because it ran out of time budget. The two look identical
      // from outside - the frame counter stops either way - so the progress
      // window says which one is happening instead of leaving the user to
      // guess whether a render is slow or wedged.
      bool waitingOnEncoder = false;
      double lastProgressTime = 0.0; // when OfflineFramesDone last changed
      int lastFramesDone = -1;

      // Set only for a render started from the Arrangement Timeline's own
      // Render button - switches on two behaviors that would be wrong for
      // an ordinary manually-wired OutputNode take: (1) each frame, every
      // video lane's active clip is composited onto the node's FBO after
      // the cook (CompositeArrangeTimelineVideo - bottom lane first, so the
      // top lane is frontmost, each lane's blend mode and opacity applied),
      // replacing whatever the node's own Input produced; (2) RebuildAudioTopology's Timeline Strict terminals also
      // write into this node's capture ring (see arrangeAudioCapture
      // below), so the take's audio is the live sum of every active
      // timeline audio clip rather than whatever's cabled into AudioInput.
      bool arrangeDriven = false;
      // WP7 #6b splits arrangeDriven's two behaviours apart, because a job
      // can now take its picture from one place and its sound from another
      // ("video-only timeline over live canvas music" is a first-class job).
      // Both are only read while arrangeDriven.
      //   timelineVideo - composite the video lanes onto the take's FBO,
      //                   replacing whatever its own Input produced.
      //   timelineAudio - RebuildAudioTopology builds the Timeline Strict
      //                   terminals rather than the canvas Audio Outs
      //                   (ArrangeTimelineRoutingActive).
      // Audio always reaches the file through the take node's capture ring
      // either way: the timeline's export node has nothing cabled into its
      // AudioInput, so both sources are the master sum, and only the
      // terminals feeding that sum differ.
      bool timelineVideo = false;
      bool timelineAudio = false;
      // The job's range in ticks. StartOfflineRenderSession scopes its
      // hardware-source refusal (WP7 #4) to the clips that actually play
      // inside it, so it has to be set before the take is armed - same
      // discipline as arrangeDriven and endSeconds.
      int64_t rangeStartTick = 0;
      int64_t rangeEndTick = 0;
      // Only meaningful when arrangeDriven: the timeline second the render
      // should end on, so the render-finish cleanup can park the playhead
      // there instead of leaving it wherever Transport::SetOfflineMode(false)
      // happens to revert to (whatever mSeconds was before the take started).
      double endSeconds = 0.0;
   };

extern OfflineRenderState gOfflineRender;

extern Headless::Job gHeadlessJob;

extern int gHeadlessExitCode;

extern double gHeadlessAudioRate;

extern std::vector<Headless::Issue> gHeadlessPreWarnings;

extern bool gHeadlessNeedProbe;

extern std::vector<Headless::Issue> gHeadlessPromoted;

extern Patch::Data gHeadlessPatch;

extern std::map<std::string, int> gModulatableMax;

extern bool gHeadlessProbeAll;

extern std::set<int> gHeadlessDrawn;

bool HeadlessJobActive();

bool IsHeadlessProcess();



   // ---- Arrangement render jobs (WP7) --------------------------------------
   // One job = one file. The timeline's Render popup fills one in, and either
   // runs it immediately or parks it in the queue; both paths go through
   // ArrangeRenderBeginJob, so the hardware-source refusal, the overwrite
   // check and the source routing can't be skipped by one of them.
   enum ArrangeRenderAudioSource { kArrangeAudioTimeline = 0, kArrangeAudioCanvas = 1, kArrangeAudioNone = 2 };


   enum ArrangeRenderVideoSource { kArrangeVideoTimeline = 0, kArrangeVideoCanvas = 1, kArrangeVideoNone = 2 };


   enum ArrangeRenderRangeKind { kArrangeRangeWhole = 0, kArrangeRangeLoop = 1, kArrangeRangeMarkers = 2, kArrangeRangeCustom = 3 };


   enum ArrangeRenderStatus
   {
      kArrangeJobQueued = 0,
      kArrangeJobRendering,
      kArrangeJobFinalizing,
      kArrangeJobDone,
      kArrangeJobFailed,
      kArrangeJobCancelled
   };



   struct ArrangeRenderJob
   {
      uint64_t id = 0;
      int rangeKind = kArrangeRangeWhole;
      int64_t startTick = 0;
      int64_t endTick = 0;
      int audioSource = kArrangeAudioTimeline;
      int videoSource = kArrangeVideoTimeline;
      uint64_t canvasVideoUid = 0; // only when videoSource == kArrangeVideoCanvas
      int width = 1920, height = 1080, fps = 60;
      int sampleRate = 48000;
      int format = 0;              // 0 = mp4, 1 = mov, 2 = wav
      std::string path;
      int status = kArrangeJobQueued;
      std::string message;         // failure reason, or a note about the take
      int framesDone = 0, framesTotal = 0;
      double startedTime = 0.0;    // glfwGetTime() when it began, for the ETA
      // Empty = whole project (every lane), matching the original behavior.
      // Non-empty scopes the take to just these lane ids ("Render Track" /
      // "Render Group"). Only meaningful when videoSource is Timeline or
      // None - a canvas take isn't a lane concept, so it always ignores this.
      std::vector<uint64_t> laneScope;
   };

extern std::unordered_set<uint64_t> gArrangeRenderActiveLaneScope;

extern std::unique_ptr<OutputNode> gArrangeTimelineExportNode;

extern std::vector<ArrangeRenderJob> gArrangeRenderQueue;

extern uint64_t gArrangeRenderNextJobId;

extern uint64_t gArrangeRenderActiveJobId;

extern bool gArrangeRenderQueueRunning;



   // An audio-only take (Video source = None). It has no OutputNode, no
   // encoder and no frames - just AudioEngine::ProcessOffline block by block
   // into a WAV file - so it can't ride on gOfflineRender, which is built
   // around a node's video take. Only one of the two ever runs at a time.
   struct ArrangeWavRenderState
   {
      bool active = false;
      bool timelineAudio = true;   // same meaning as gOfflineRender.timelineAudio
      bool cancelRequested = false;
      AudioFileWriter writer;
      long long framesTotal = 0, framesDone = 0;
      double sampleRate = 0.0;
      double startSeconds = 0.0, endSeconds = 0.0;
      bool deviceWasRunning = false, wasPlaying = true, vsyncWasOn = true;
      double startedTime = 0.0;
   };

extern ArrangeWavRenderState gArrangeWavRender;

bool ArrangeRenderBeginJob(ArrangeRenderJob& job);

void ArrangeRenderQueueTick();

void ArrangeRenderCancelActive();

void ArrangeRenderCancelAll();

void ArrangeRenderQueuePosition(int& outIndex, int& outTotal);

bool ArrangeRenderBusy();



   // ---- device-change / sleep-wake recovery state (PollAudioRecovery) ----
   // docs/plans/optimization/prompts/02-device-change-and-wake-recovery.md.
   // Bounds recovery to one attempt per kAudioRecoveryMinIntervalMs and no
   // more than kAudioRecoveryMaxAttemptsPerWindow inside any
   // kAudioRecoveryWindowMs rolling window, so a flapping device or a burst
   // of notifications on wake can't spawn overlapping restarts or loop
   // forever - see PollAudioRecovery's comment for how these are used.
   inline constexpr double kAudioRecoveryMinIntervalMs = 500.0;


   inline constexpr double kAudioRecoveryWindowMs = 10000.0;


   inline constexpr int kAudioRecoveryMaxAttemptsPerWindow = 5;

extern double gLastAudioRecoveryAttemptMs;

extern double gAudioRecoveryWindowStartMs;

extern int gAudioRecoveryAttemptsInWindow;

void ResetAudioRecoveryState();

extern double gLastFrameMs;

extern double gFrameStart;

extern int gTargetFps;

extern bool gVsync;

extern bool gRequestFitView;

extern int gRequestFitViewNodeIndex;

extern bool gRequestGroup;

extern bool gRequestUngroup;

extern bool gRequestBypass;

extern bool gRequestCopy;

extern bool gRequestPaste;

extern bool gRequestDuplicate;

extern bool gRequestDelete;

extern bool gRequestSelectAll;

extern int gKbFocusNode;

extern int gKbFocusParam;

extern int gKbNudge;

        // signed value steps queued for the focused param, applied in its widget
   struct KbParamEntry { int node; int param; };

extern std::vector<KbParamEntry> gKbParams;

 // params drawn this frame, in draw order (Tab order)
   inline constexpr ImGuiID kKbTabOwner = 0x4B425441u;

extern bool gKbOwnTab;

extern bool gKbZoomed;

extern ImVec2 gKbSavedScroll;

extern float gKbSavedZoom;

extern bool gKbViewRestore;

extern ImVec2 gKbPan;

extern bool gComputerKeyboardHot;

extern bool gRequestAddNode;

extern bool gRequestAddComment;

extern int gContextMenuNodeIndex;

extern int gHelpPopupNodeIndex;

extern bool gNodeHelpShown;

extern bool gCloseNodeHelp;

extern bool gOpenNodeHelpPopup;

extern int gModBindingMenuNode;

extern int gModBindingMenuParam;

extern bool gOpenModBindingMenu;

extern int gModRangeTypedField;

extern std::string gModRangeTypedText;

extern bool gModRangeTypedJustOpened;

extern bool gModRangeTypedPendingInit;

extern bool gModRangeTypedNoAutoSelect;

extern bool gNodePanelOpen;

extern bool gViewportPanelOpen;

extern std::vector<int> gViewportPanelNodes;

extern int gViewportPanelDock;

extern bool  gModMatrixOpen;

extern int   gModMatrixDock;

extern int    gModMatrixHighlightNode;

extern int    gModMatrixHighlightParam;

extern double gModMatrixHighlightUntil;

extern bool   gModMatrixScrollPending;

extern int   gModMatrixFillRows;

extern float gModMatrixScrollMax;

extern float gModMatrixWidth;

extern float gModMatrixHeight;


   inline const float kModMatrixMinWidth = 300.0f;

   // eight columns need real room
   inline const float kModMatrixMinHeight = 140.0f;

extern bool  gPerfPanelOpen;

extern int   gPerfPanelDock;

extern float gPerfPanelWidth;

extern float gPerfPanelHeight;


   inline const float kPerfPanelMinWidth = 240.0f;


   inline const float kPerfPanelMinHeight = 160.0f;

extern bool  gArrangePanelOpen;

extern float gArrangePanelWidth;

extern float gArrangePanelHeight;


   inline const float kArrangePanelMinWidth = 300.0f;


   inline const float kArrangePanelMinHeight = 140.0f;

extern float gArrangePixelsPerBeat;

extern double gArrangeScrollBeats;


   inline const float kArrangeMinPixelsPerBeat = 5.0f;


   inline const float kArrangeMaxPixelsPerBeat = 500.0f;

extern bool  gArrangeViewportOnRight;

extern bool  gArrangeShowViewport;

extern int   gArrangeLastSnapDivision;

extern bool  gArrangeScrubbing;

extern int64_t gArrangeScrubTick;

extern ImGuiMouseButton gArrangeScrubButton;

extern uint64_t gArrangeMarkerDragId;

extern int64_t gArrangeMarkerDragGrabTick;

extern int64_t gArrangeMarkerDragOrigPos;

extern uint64_t gArrangeRenamingMarkerId;

extern char gArrangeRenameMarkerBuffer[64];

extern uint64_t gArrangeCtxMarkerId;

extern bool   gArrangeShiftDraggingLoop;

extern int64_t gArrangeLoopDragAnchorTick;


   enum ArrangeLoopDragMode {
      kArrangeLoopDragNone = 0,
      kArrangeLoopDragStart, // dragging left border
      kArrangeLoopDragEnd,   // dragging right border
      kArrangeLoopDragMove   // dragging header / entire loop
   };

extern ArrangeLoopDragMode gArrangeLoopDragMode;

extern Arrange::Tick gArrangeLoopDragOrigStart;

extern Arrange::Tick gArrangeLoopDragOrigEnd;

extern Arrange::Tick gArrangeLoopDragGrabTick;


   // Arrangement tools (Select A, Trim T, Range R, Blade B, Zoom Z, Hand H, Pencil P)
   enum class ArrangeTool {
      Select = 0, // A - Standard selection, move, and edge-trim
      Trim,       // T - Focused trim / slip tool
      Range,      // R - Range / marquee selection tool
      Blade,      // B - Cut / split tool
      Zoom,       // Z - Zoom tool (click in, Alt-click out, drag scrub)
      Hand,       // H - Hand / pan tool (drag canvas to pan & scroll)
      Pencil      // P - Pencil / draw tool (click or drag to spawn new unassigned clip)
   };

extern ArrangeTool gArrangeTool;

extern bool  gArrangeBladeOn;

extern bool  gArrangeHandDragging;

extern bool  gArrangeZoomDragging;

extern ImVec2 gArrangeZoomDragStart;

extern float  gArrangeZoomDragStartPpb;

extern double gArrangeZoomDragStartBeats;

extern uint64_t gArrangeRevealClipId;

extern int      gArrangeRevealFrames;

extern uint64_t gArrangeFlashClipId;

extern double   gArrangeFlashStart;

extern bool  gArrangeClaimedKeys;

extern bool  gArrangeFocused;


   // Which of the two mutually exclusive audio routings is live (overhaul
   // WP3). Canvas = the node graph's own Audio Out nodes feed the device, the
   // arrangement is silent. Timeline = the arrangement's audio clips feed the
   // device directly and every canvas Audio Out is bypassed.
   //
   // Deliberately NOT persisted and NOT part of a patch: it is a monitoring
   // choice, not a property of the work, and a patch that silently reopened
   // in Timeline mode would play nothing until the user found the button.
   // Reset to Canvas on launch (this initialiser), File > New and File > Open.
   //
   // Routing depends on this and nothing else. It used to also require the
   // arrangement panel to be open and the transport to be playing, which made
   // hiding the panel or hitting pause change what the topology contained -
   // a rebuild-driven mode leak rather than a routing rule.
   enum class AudioMode { Canvas, Timeline };

extern AudioMode gAudioMode;

extern int   gArrangeAddTrackInsertAfter;

extern ImVec2 gArrangePanelRectMin;

extern ImVec2 gArrangePanelRectMax;

extern bool  gPerfEditMode;

extern int   gPerfActivePage;

extern int   gPerfRenamingPage;

extern char  gPerfRenamePageBuffer[64];

extern int   gPerfAssigningElemIdx;

extern int   gPerfAssigningAxis;

extern uint64_t gDriftFollowPickingUid;

extern int   gPerfMidiLearnIdx;

extern int   gPerfMidiLearnAxis;

extern uint64_t gParamMidiLearnUid;

extern int      gParamMidiLearnParam;

void MidiLearnCancelAll();

void StartParamMidiLearn(int nodeIndex, int paramIndex);

bool ParamMidiLearnIsActiveFor(int nodeIndex, int paramIndex);

int  MidiLearnActiveCount();

bool ParamMidiLearnActive();

bool ParamMidiLearnable(int nodeIndex, int paramIndex);

bool ParamMidiLearnCommit(int nodeIndex, int paramIndex, const Platform::MidiCCValue& last);

void UpdateParamMidiLearn();

void DrawParamMidiLearnBanner();

void DrawParamMidiLearnMenuItem(int nodeIndex, int paramIndex);


   struct PerfMidiRuntimeState
   {
      float lastVal = -1.0f;
      float lastValY = -1.0f;
      unsigned int lastHitSeq = 0;
   };

extern std::map<size_t, PerfMidiRuntimeState> gPerfMidiRuntimeStates;

extern std::map<size_t, float> gPerfBangFlash;


   struct ParamPinScreenInfo
   {
      int nodeIndex;
      int paramIndex;
      std::string nodeTitle;
      std::string paramName;
      ImVec2 screenPos;
      ImVec2 rowMin;
      ImVec2 rowMax;
      // Round controls (ModKnob's Knob/KnobDb/KnobFreq styles) hit-test and
      // draw the same rowMin/rowMax box as everything else, but that box also
      // covers the caption text and pin dot below the cap - highlighting it
      // whole reads as "the entire cell", not "this knob". These let the
      // assign-mode hover highlight trace the actual knob circle instead.
      // Left at their defaults (false/zero) for every non-round pin, which is
      // why the other two push sites don't need to set them.
      bool isCircle = false;
      ImVec2 shapeCenter = ImVec2(0.0f, 0.0f);
      float shapeRadius = 0.0f;
   };

extern std::vector<ParamPinScreenInfo> gParamPinScreenList;

extern int   gPerfRenamingElementIdx;

extern char  gPerfRenameElementBuffer[64];

extern Patch::PerfLayoutRecord gPerfLayout;

extern std::vector<Patch::PerfRecord> gPerfElements;

extern Arrange::Model gArrange;

extern uint64_t gArrangePatchGeneration;

int ArrangePanelDock();

extern std::set<uint64_t> gArrangeSel;

extern uint64_t gArrangeSelAnchor;

extern uint64_t gArrangeSelGeneration;

extern std::set<uint64_t> gArrangeRowSel;

extern uint64_t gArrangeRowSelAnchor;



   // Copy/paste clipboard: clips by value, with each clip's lane and tick
   // offset relative to the copied block's first lane / earliest start.
   struct ArrangeClipboardItem
   {
      Arrange::Clip clip;
      int laneOffset = 0;
      int laneType = Arrange::kLaneVideo;
      Arrange::Tick tickOffset = 0;
   };


   struct ArrangeClipboardData
   {
      std::vector<ArrangeClipboardItem> items;
      uint64_t generation = 0;
   };

extern ArrangeClipboardData gArrangeClipboard;

extern Arrange::Model gArrangeGestureBefore;

extern bool gArrangeGestureOpen;



   // Live clip drag. Every frame the model is rebuilt from the gesture
   // snapshot plus the current (delta, laneDelta) through the same op the
   // release would run, so what is drawn is exactly what the drop will do.
   enum ArrangeDragMode
   {
      kArrangeDragNone = 0,
      kArrangeDragMove,
      kArrangeDragTrimStart,
      kArrangeDragTrimEnd,
      kArrangeDragGroupEdge,   // TrimGroupEdge: only members flush with the edge
      kArrangeDragGroupScale,  // ScaleGroup: Shift-drag on a group edge
   };


   struct ArrangeDragState
   {
      int mode = kArrangeDragNone;
      uint64_t clipId = 0;      // the clip under the mouse at mouse-down
      uint64_t groupId = 0;     // group edge / scale modes
      int edge = Arrange::kEdgeStart;
      std::vector<uint64_t> ids; // move mode: every clip that moves
      Arrange::Tick grabTick = 0; // mouse tick at mouse-down
      Arrange::Tick origStart = 0, origEnd = 0; // the grabbed clip (or group) at mouse-down
      int grabLane = 0;
      Arrange::Tick appliedDelta = 0; // what the model currently reflects
      int appliedLaneDelta = 0;
      Arrange::Tick appliedTick = -1;
      bool live = false;          // past the drag threshold; until then nothing moves
      bool collapseOnClick = false; // plain click on an already-selected clip: a
                                    // release without a drag narrows the selection to it
      bool singleMember = false;  // Alt held at mouse-down
   };

extern ArrangeDragState gArrangeDrag;



   // Shift+drag rectangle-select / Range Tool (R). Screen-space corners (updated
   // live as the mouse moves) plus the selection captured at mouse-down.
   struct ArrangeMarqueeState
   {
      bool active = false;
      bool allTracks = false; // Started from ruler/header -> spans all tracks vertically
      int  startLane = -1;    // -1 if all tracks or not track-bounded
      ImVec2 anchor{ 0, 0 };
      ImVec2 current{ 0, 0 };
      std::set<uint64_t> baseSel;
   };

extern ArrangeMarqueeState gArrangeMarquee;

extern uint64_t gArrangeRenamingClipId;

extern char     gArrangeRenameClipBuffer[64];

extern uint64_t gArrangeRenamingLaneId;

extern bool     gArrangeRenameJustStarted;

extern bool     gArrangeClipSettingsPanelOpen;

extern uint64_t gArrangeSettingsPanelTarget;

extern float sArrangeLastRulerStartX;

extern uint64_t gArrangeMixGestureLaneId;

extern uint64_t gArrangeCtxClipId;

extern uint64_t gArrangeAssigningClipId;

extern std::vector<uint64_t> gArrangeRenameTargetIds;

extern std::vector<uint64_t> gArrangeAssignTargetIds;



   // One dropped-media-file decode in flight (or about to be), tracking the
   // clip/node already placed in a "loading" state so ArrangePollMediaImports
   // can find them again once Arrange::MediaImportManager finishes decoding.
   // See ArrangeImportMediaFile/ArrangePollMediaImports below.
   struct ArrangePendingImport
   {
      uint64_t jobId = 0;
      uint64_t clipId = 0;
      uint64_t nodeUid = 0;
      Arrange::ImportMediaKind kind = Arrange::ImportMediaKind::Audio;
      // True for a re-decode kicked by ArrangeRespawnCloneNode (paste/
      // duplicate/split of an existing Sample clip) rather than a fresh
      // drop from ArrangeImportMediaFile - the clip already has correct
      // sampleBpm/origBpm/length/sourceDurationSeconds from the original,
      // so ArrangePollMediaImports must not re-estimate/overwrite them,
      // just attach the newly decoded buffer to the clone's own node.
      bool isClone = false;
   };

extern std::vector<ArrangePendingImport> gArrangePendingImports;



   // A browser-panel media drag (gSampleDragActive) released over the
   // Arrange panel rect - stashed here rather than resolved on the spot,
   // since the lane/tick under a screen point is only computable from
   // inside DrawArrangePanelContent's own layout state (scroll, zoom, lane
   // rows). Picked up and cleared on that function's next call, which is at
   // most one frame later - not perceptible for a released drag.
   struct ArrangePendingBrowserDrop
   {
      bool pending = false;
      ImVec2 screenPos { 0.0f, 0.0f };
      std::string path;
   };

extern ArrangePendingBrowserDrop gArrangePendingBrowserDrop;

extern uint64_t gNextNodeUid;

extern std::set<size_t> gPerfSelection;

extern std::vector<Patch::PerfRecord> gPerfClipboard;

extern bool gPerfMatrixFocused;

extern bool gPerfMatrixClaimedKeys;

extern ImVec2 gPerfPanelRectMin;

extern ImVec2 gPerfPanelRectMax;

extern std::map<std::pair<int, int>, float> gPerfPendingWrites;

extern int   gPerfDragIdx;

extern int   gPerfDragOriginCellX;

extern int   gPerfDragOriginCellY;

extern ImVec2 gPerfDragMouseStart;

extern int   gCableVisibilityMask;

extern ImVec2 gViewCenterCanvas;

extern ImVec2 gGraphScreenTL;

extern ImVec2 gGraphScreenSize;

extern bool gMinimapEnabled;



   // Projector output: extra ordinary GLFW windows (sharing the main context)
   // that each blit one node's texture live, for driving a projector or
   // second screen at a show while the editor stays on the laptop panel. The
   // user drags a window to whichever display and fullscreens it with the
   // OS's own controls - we don't pick a monitor or go fullscreen ourselves.
   // Opened/closed per-node via each node's right-click menu ("Open in new
   // window" / "Close window"), not from any settings panel. Any number of
   // nodes can have their own window open at once.
   struct ProjectorWindow
   {
      GLFWwindow* window = nullptr;
      int nodeIndex = -1; // GraphNode::index this window shows
      bool fullscreen = false;
      int monitorIndex = -1;   // display it was last placed on by us
      int windowedX = 100, windowedY = 100, windowedW = 1280, windowedH = 720; // restore box
   };

extern std::vector<ProjectorWindow> gProjectorWindows;

extern int gCanvasSwapInterval;

extern int gAppliedCanvasSwapInterval;

bool FrameClockActive();

void ApplyCanvasSwapInterval();

void SetCanvasSwapInterval(int interval);

extern int gMinimapCorner;

extern float gMinimapSize;

extern float gMinimapOpacity;

extern float gZoomSensitivity;

extern bool gHoveringItem;

extern bool gPanWithLeft;

extern ImVec2 gDragTestNodeScreen;

extern ImVec2 gDragTestNodePos;

extern ImVec2 gTestMouse;

extern ImVec4 gPredTestSliderScreen;

extern ImVec4 gPredTestSliderCanvas;

extern int gPredTestNodeIndex;

extern int gPredTestSizeXParam;

extern std::vector<ImVec4> gWtTestRects;

extern bool gWtDragOk;

extern std::vector<ImVec4> gWtTestScreen;

void PublishWtTestRect();

extern ImVec2 gDragTestViewAnchor;

extern ImVec4 gEqTestRect;

extern ImVec4 gEqTestScreen;



   // Interface fonts the preference offers. `file` is relative to the bundled fonts folder; the
   // first entry is the default and is what an unknown or missing saved choice falls back to.
   struct InterfaceFont
   {
      const char* id;    // saved in the appearance file ("" = default)
      const char* label; // shown in Preferences
      const char* file;
   };


   inline const InterfaceFont kInterfaceFonts[] = {
      { "", "Inter (default)", "fonts/Inter-Regular.ttf" },
      { "atkinson", "Atkinson Hyperlegible", "fonts/AtkinsonHyperlegible-Regular.ttf" },
   };

std::string FoldForSearch(const std::string& in);



   // Lists at least this long get a search box without the call site asking for one.
   inline constexpr size_t kDropdownAutoSearchMin = 12;



   // ---- deferred dropdown -------------------------------------------------
   // ImGui combos opened inside a node get clipped and mis-scaled by the node
   // editor's canvas transform. Instead a node draws a plain button, records
   // what it wants to show, and the popup is rendered once per frame outside
   // the canvas (inside ed::Suspend) as a normal, scrollable ImGui popup.
   struct DropdownRequest
   {
      // Stored BY VALUE, not as a pointer to the caller's vector: several call
      // sites pass an inline temporary (e.g. `{ "Free", "Synced" }`), which is
      // destroyed at the end of that statement. The popup itself doesn't
      // render until later - ImGui::OpenPopup here just flags it, and
      // BeginPopup happens further down this same frame - so a stored pointer
      // would already be dangling by then. This was the Stutter "Synced"
      // crash: its sync dropdown is exactly such a temporary.
      std::vector<std::string> options;
      std::vector<std::string> categories;
      std::function<void(int)> onSelect;
      int current = 0;
      bool justOpened = false;
      bool focusSearch = false;
      char filterBuf[64] = "";
   };

extern DropdownRequest gDropdown;

void CommitDropdownPick(int i);

extern std::pair<int, int> gDropdownTestOpenKey;

bool DropdownTestWantsOpen(bool registered, int nodeIndex, int paramIndex);

extern bool gInsideNodeCanvas;



   // Same story for ImGui's colour picker: opened inside a node it inherits the
   // canvas transform and the hue bar / sliders stop tracking the cursor. Route
   // it through a popup rendered outside the canvas instead.
   struct ColorRequest
   {
      float* target = nullptr;
      const INode* owner = nullptr; // revalidated each frame; nodes can be deleted
      std::string label;
      bool justOpened = false;
   };

extern ColorRequest gColor;

extern ImVec4 gColorPickerRect;

 // x, y, w, h of the picker widget on screen

   // And again for a comment's text. A multiline text field is an ImGui child
   // window, which the canvas cannot transform at all: drawn inside the node it
   // rendered the note at the canvas origin, nowhere near the comment it
   // belonged to. So the note is painted into the node by hand and typing into
   // it happens in a popup out here (see DrawCommentPreview).
   struct CommentEditRequest
   {
      CommentNode* target = nullptr; // revalidated each frame; nodes can be deleted
      bool justOpened = false;
      // The double-click that opens the editor is also a click on the canvas,
      // and the canvas takes the focus back when the button comes up - one
      // frame after the popup appeared. So the field asks for the keyboard for
      // the first few frames rather than only on the frame it appears, which
      // otherwise left it open but dead until it was clicked a third time.
      int framesOpen = 0;
   };

extern CommentEditRequest gCommentEdit;

extern ImVec4 gCommentBodyRect;

 // x, y, w, h

   // ---- Field build step 17: .infdev device Save name-prompt --------------
   // Same story as gDropdown/gCommentEdit above: a node draws a "Save"
   // button, records what it wants to do, and the actual popup renders once
   // per frame outside the canvas. `getDeviceFile` resolves the owning node
   // by index and dynamic_casts back to the concrete type each time it is
   // called (see DrawFieldDeviceControls below) rather than closing over a
   // raw node pointer, so a node deleted while this popup is open is simply
   // "not found" instead of a dangling read.
   struct FieldDeviceSaveRequest
   {
      std::function<bool(Field::DeviceFile&)> getDeviceFile;
      std::string domain;
      char nameBuf[128] = {};
      bool justOpened = false;
   };

extern FieldDeviceSaveRequest gFieldDeviceSave;



   // Per-domain cache of the user's saved .infdev files under
   // AppPaths::AppSupportDir() + "/Devices/<domain>/" - scanned once per
   // session on first draw, not re-scanned every frame (plan §6). Save
   // invalidates only its own domain's entry.
   struct FieldDeviceLibraryCache
   {
      bool scanned = false;
      std::vector<std::string> names; // file stem, for display
      std::vector<std::string> paths; // matching full path, same order
   };

extern std::map<std::string, FieldDeviceLibraryCache> gFieldDeviceLibrary;

const FieldDeviceLibraryCache& GetFieldDeviceLibrary(const std::string& domain);

void InvalidateFieldDeviceLibrary(const std::string& domain);

extern ImVec4 gCommentEditRect;

extern float gCommentEditZoom;

extern FormulaNode* gFormulaEditor;

extern bool gFormulaEditorOpen;

extern FieldElementNode* gFieldElementEditor;

extern bool gFieldElementEditorOpen;

extern FieldPrimitiveNode* gFieldPrimitiveEditor;

extern bool gFieldPrimitiveEditorOpen;

extern FieldPixelNode* gFieldPixelEditor;

extern bool gFieldPixelEditorOpen;

extern FieldSampleNode* gFieldSampleEditor;

extern bool gFieldSampleEditorOpen;

extern FieldSynthNode* gFieldSynthEditor;

extern bool gFieldSynthEditorOpen;

extern FieldGraphNode* gFieldGraphEditor;

extern bool gFieldGraphEditorOpen;

extern FieldGraphNode* gFieldGraphPendingRegenerate;

extern FieldGraphNode* gFieldGraphPendingUnpack;



   // Phase 2 of the unpack operation: a poll-and-validate tick driven once
   // per frame (also after ed::End(), alongside the drain above) while
   // `active` is true. Phase 1 reveals the mounted children at a provisional
   // layout and arms this; phase 2 waits for every revealed child's ed::
   // node size to read as a real measurement (not a freshly-spawned node's
   // sentinel/placeholder size - doc §3.3/trap 3) before finalizing row
   // heights and spawning the wrapping GroupNode. Capped at kMaxRetries
   // frames, after which it finalizes with the kUnpackDYMin-only fallback
   // stacking rather than waiting forever (doc's own defensive framing of
   // the "two-frame operation").
   struct FieldGraphUnpackPhase2State
   {
      bool active = false;
      FieldGraphNode* target = nullptr;
      std::vector<int> members;               // mounted indices, in layout order
      std::map<int, int> depthByIndex;        // member index -> topological depth
      std::map<int, float> columnX;           // depth -> this column's x
      int retriesLeft = 0;
      static constexpr int kMaxRetries = 10;
   };

extern FieldGraphUnpackPhase2State gFieldGraphUnpackPhase2;

extern bool gHelpOpen;

extern bool gShortcutsOpen;

extern bool gNavOwnsKeys;

#ifndef NDEBUG
extern bool gUiDebuggerOpen;
#endif

#ifndef NDEBUG
extern bool gUiStyleEditorOpen;
#endif

extern bool gSettingsOpen;

extern bool gShowUpdateCheckModal;

extern std::vector<std::string> gDroppedFiles;

extern ImVec2 gDropPos;

bool HasExtension(const std::string& path, const std::vector<std::string>& exts);

extern const std::vector<std::string>& kVideoExt;



   // Finds the first node under a canvas-space point whose node.get()
   // dynamic_casts to T - the Samples/Media drag-drop release handler's hit
   // test, factored out so it isn't copy-pasted once per draggable node
   // type (SamplerNode, VideoSourceNode, ImageSourceNode). Deliberately a
   // plain rect test against ed::GetNodePosition/GetNodeSize rather than
   // ed::GetHoveredNode() - see the release handler's own comment for why.
   template <typename T>
   T* FindNodeUnderCanvasPoint(const ImVec2& canvasPoint)
   {
      for (GraphNode& gn : gNodes)
      {
         auto* typed = gn.node ? dynamic_cast<T*>(gn.node.get()) : nullptr;
         if (typed == nullptr)
            continue;
         const ImVec2 p = ed::GetNodePosition(gn.NodeId());
         const ImVec2 s = ed::GetNodeSize(gn.NodeId());
         if (canvasPoint.x >= p.x && canvasPoint.x <= p.x + s.x && canvasPoint.y >= p.y &&
             canvasPoint.y <= p.y + s.y)
            return typed;
      }
      return nullptr;
   }

int DrumSequencerLaneForCanvasPos(DrumSequencerNode* n, float canvasX, float canvasY);

void OnFilesDropped(GLFWwindow* window, int count, const char** paths);

void DropdownButton(const char* label, const std::vector<std::string>& options,
                       int current, std::function<void(int)> onSelect, float width = kParamWidth,
                       bool showCaption = true);

bool DrawBrowserFilterStrip(BrowserFilterState& state,
                               const char* searchHint,
                               const std::vector<std::string>& sortNames,
                               const std::vector<std::string>& typeNames);

void LoadBrowserFilterPrefs();

void SaveBrowserFilterPrefs();



   // Persisted favourites for items across the browser panel modes:
   // modules (by name), samples (by file path), media (by file path),
   // plugins (by identifier), and field presets (by name). Saved to
   // AppPaths::AppSupportDir() + "/Infinite.browserfavorites".
   struct BrowserFavorites
   {
      std::unordered_set<std::string> modules;
      std::unordered_set<std::string> samples;
      std::unordered_set<std::string> media;
      std::unordered_set<std::string> plugins;
      std::unordered_set<std::string> fieldPresets;
      uint64_t version = 1;

      uint64_t Version() const { return version; }

      bool IsFavoriteModule(const std::string& name) const { return modules.count(name) > 0; }
      bool IsFavoriteSample(const std::string& path) const { return samples.count(path) > 0; }
      bool IsFavoriteMedia(const std::string& path) const { return media.count(path) > 0; }
      bool IsFavoritePlugin(const std::string& id) const { return plugins.count(id) > 0; }
      bool IsFavoriteFieldPreset(const std::string& name) const { return fieldPresets.count(name) > 0; }

      void ToggleModule(const std::string& name)
      {
         if (modules.count(name))
            modules.erase(name);
         else
            modules.insert(name);
         version++;
         Save();
      }

      void ToggleSample(const std::string& path)
      {
         if (samples.count(path))
            samples.erase(path);
         else
            samples.insert(path);
         version++;
         Save();
      }

      void ToggleMedia(const std::string& path)
      {
         if (media.count(path))
            media.erase(path);
         else
            media.insert(path);
         version++;
         Save();
      }

      void TogglePlugin(const std::string& id)
      {
         if (plugins.count(id))
            plugins.erase(id);
         else
            plugins.insert(id);
         version++;
         Save();
      }

      void ToggleFieldPreset(const std::string& name)
      {
         if (fieldPresets.count(name))
            fieldPresets.erase(name);
         else
            fieldPresets.insert(name);
         version++;
         Save();
      }

      std::string Path() const
      {
         const std::string dir = AppPaths::AppSupportDir();
         return dir.empty() ? std::string() : dir + "/Infinite.browserfavorites";
      }

      void Load()
      {
         const std::string path = Path();
         if (path.empty())
            return;
         std::ifstream file(path);
         if (!file)
            return;
         modules.clear();
         samples.clear();
         media.clear();
         plugins.clear();
         fieldPresets.clear();
         std::string line;
         std::string section;
         while (std::getline(file, line))
         {
            if (!line.empty() && line.back() == '\r')
               line.pop_back();
            if (line.empty() || line[0] == '#')
               continue;
            if (line.front() == '[' && line.back() == ']')
            {
               section = line.substr(1, line.size() - 2);
               continue;
            }
            if (section == "modules")
               modules.insert(line);
            else if (section == "samples")
               samples.insert(line);
            else if (section == "media")
               media.insert(line);
            else if (section == "plugins")
               plugins.insert(line);
            else if (section == "field")
               fieldPresets.insert(line);
         }
         version++;
      }

      void Save() const
      {
         const std::string path = Path();
         if (path.empty())
            return;
         std::ofstream file(path);
         if (!file)
            return;
         file << "[modules]\n";
         for (const auto& m : modules)
            file << m << "\n";
         file << "[samples]\n";
         for (const auto& s : samples)
            file << s << "\n";
         file << "[media]\n";
         for (const auto& m : media)
            file << m << "\n";
         file << "[plugins]\n";
         for (const auto& p : plugins)
            file << p << "\n";
         file << "[field]\n";
         for (const auto& f : fieldPresets)
            file << f << "\n";
      }
   };

extern BrowserFavorites gBrowserFavorites;

extern bool gPatchDirty;

void DrawFavoriteBadge(const ImVec2& itemMin, const ImVec2& itemMax, bool isFav);

std::string TruncateWithEllipsis(const std::string& label, float maxWidth);

extern int gCurrentNodeIndex;

extern int gParamCounter;

extern int gColorCounter;


   // Discrete (bool / enum) params are numbered from their own base rather
   // than sharing gParamCounter. Giving a checkbox or a dropdown an ordinal
   // out of the float sequence would have shifted every float ordinal that
   // draws after it in the same node - repointing the modulation bindings and
   // performance-surface assignments in every patch already saved. Ordinals
   // 0..kDiscreteParamBase-1 stay exactly where they were; discrete params
   // live above that line, still inside the kParamBase..kColorBase pin block
   // (see GraphNode::kColorBase, which caps this at 800).
   inline const int kDiscreteParamBase = 400;

extern int gDiscreteParamCounter;

extern std::map<std::pair<int, std::string>, int> gDiscreteSlotByLabel;

extern std::map<std::pair<int, int>, std::string> gDiscreteLabelBySlot;

extern std::string gDiscreteSlotScope;



   struct DiscreteSlotScope
   {
      std::string saved;
      explicit DiscreteSlotScope(int subPanelIndex)
         : saved(gDiscreteSlotScope)
      {
         if (subPanelIndex > 0)
            gDiscreteSlotScope = saved + "#" + std::to_string(subPanelIndex);
      }
      ~DiscreteSlotScope() { gDiscreteSlotScope = saved; }
   };

int DiscreteParamSlot(int nodeIndex, const std::string& rawLabel);

void ForgetDiscreteSlots(int nodeIndex);

void ForgetAllDiscreteSlots();

extern std::set<int> gDrawnColorPins;

IPaletteSource* PaletteSourceByIndex(int nodeIndex);

extern std::set<std::pair<int, int>> gTypedParam;

extern std::set<std::pair<int, int>> gTypedParamNoAutoSelect;

extern std::set<std::pair<int, int>> gTypedParamPendingInit;

extern std::map<std::pair<int, int>, std::string> gTypedParamSeed;

std::string TrimCopy(const std::string& s);

bool TypedTextIsUntouchedSeed(const std::pair<int, int>& key, const std::string& trimmed);

extern std::map<std::pair<int, int>, std::string> gTypedParamText;

std::string TrimCopy(const std::string& s);

extern std::set<int> gDrawnParamPins;
// Screen position of pins/anchors this frame, for cables drawn by hand: every
// output pin (DrawPin) and, for a modulated param whose control is not drawn
// (an unselected lane/band), the on-screen thing that stands for it. See
// NoteHiddenParamAnchor and the dotted-cable pass in DrawLinks.
extern std::map<int, ImVec2> gPinAnchors;
void NoteHiddenParamAnchor(int nodeIndex, int paramIndex, const ImVec2& screenPos);
// A hidden param whose cable should land on another (drawn) param's pin: the EQ's
// unselected bands share one set of knobs, so their dotted cables end on those.
void NoteHiddenParamAlias(int nodeIndex, int hiddenParam, int shownParam);
extern std::map<int, int> gPinAlias;

extern bool gParamRegisterOnly;

extern std::vector<int> gPendingSelect;

extern std::pair<int, int> gTypedParamJustOpened;

extern bool gParamRightClickConsumedThisFrame;

void BeginNodeParams(int nodeIndex);

void EndNodeParams();

void HelpTip(const char* fmt, ...);

bool TextFocusClaimed();

void BeginTypedEditFromCurrent(const std::pair<int, int>& editKey, int nodeIndex, int paramIndex,
                                   float* value, const char* fmt, bool hasExpr);

void HandleParamTypeHotkeys(const std::pair<int, int>& editKey, float* value);

bool KbParamHook(int nodeIndex, int paramIndex, float* value, float minV, float maxV, float step,
                    const char* fmt, ImVec2 rmin, ImVec2 rmax, bool circle);

extern std::map<int, std::string> gAudioReadout;

void SetAudioReadout(const char* label, const char* valueText);



   // Widget styles for audio knobs and faders.
   enum class AudioWidgetStyle
   {
      Knob,
      KnobBipolar,
      VFader,
      // Same widget as VFader, routed through ConsoleFaderTaper - unity at
      // 75% of throw instead of a linear-in-dB mapping. Only meaningful for
      // a -60..+12 dB range; see the taper's own comment.
      VFaderDb,
      // Same widget as Knob, routed through ConsoleFaderTaper.
      KnobDb,
      // Logarithmic taper matching human ear frequency/pitch perception and visualizer response curves.
      KnobFreq,
      KnobLog,
      KnobSkewAttack100,
      KnobSkewDecay300,
      KnobSkewRelease400,
      KnobSkewDelay250,
      KnobSkewGlide150,
      KnobSkewGlide100,
      KnobSkewStrum30,
      KnobSkewStrum20,
      KnobSkewFreqShifter12,
      KnobSkewStereoBass120,
      KnobSkewBassMono = KnobSkewStereoBass120,
      KnobSkewSpread100 = KnobSkewFreqShifter12
   };

void FormatAudioParam(char* outBuf, size_t outSize, const char* fmt, float val);



   // Skew (power law) taper parameterized by target 12 o'clock centre value.
   // Follows JUCE's NormalisableRange::setSkewForCentre idiom:
   // k = ln((centre - minV) / (maxV - minV)) / ln(0.5)
   // PosToValue(p) = minV + (maxV - minV) * p^k
   // ValueToPos(v) = ((v - minV) / (maxV - minV))^(1/k)
   namespace SkewTaperMath
   {
      inline float PosToValue(float pos01, float minV, float maxV, float centre)
      {
         if (maxV <= minV || centre <= minV || centre >= maxV)
            return minV + (maxV - minV) * std::clamp(pos01, 0.0f, 1.0f);
         const float k = logf((centre - minV) / (maxV - minV)) / logf(0.5f);
         return minV + (maxV - minV) * powf(std::clamp(pos01, 0.0f, 1.0f), k);
      }

      inline float ValueToPos(float value, float minV, float maxV, float centre)
      {
         if (maxV <= minV || centre <= minV || centre >= maxV)
            return (maxV > minV) ? std::clamp((value - minV) / (maxV - minV), 0.0f, 1.0f) : 0.0f;
         const float k = logf((centre - minV) / (maxV - minV)) / logf(0.5f);
         const float norm = std::clamp((value - minV) / (maxV - minV), 0.0f, 1.0f);
         return std::clamp(powf(norm, 1.0f / k), 0.0f, 1.0f);
      }
   }



   // True logarithmic taper for ranges with minV > 0.
   // Maps 0..1 throw to an octave/ratio-linear scale between minV and maxV.
   namespace LogTaper
   {
      inline float PosToValue(float pos01, float minV, float maxV)
      {
         const float lo = std::max(1e-4f, minV);
         const float hi = std::max(lo + 1e-4f, maxV);
         return lo * powf(hi / lo, std::clamp(pos01, 0.0f, 1.0f));
      }

      inline float ValueToPos(float value, float minV, float maxV)
      {
         const float lo = std::max(1e-4f, minV);
         const float hi = std::max(lo + 1e-4f, maxV);
         const float clampedVal = std::clamp(value, lo, hi);
         return std::clamp((logf(clampedVal) - logf(lo)) / (logf(hi) - logf(lo)), 0.0f, 1.0f);
      }
   }



   namespace FrequencyTaper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return LogTaper::PosToValue(pos01, minV, maxV); }
      inline float ValueToPos(float value, float minV, float maxV) { return LogTaper::ValueToPos(value, minV, maxV); }
   }



   namespace SkewAttack100Taper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, 100.0f); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, 100.0f); }
   }



   namespace SkewDecay300Taper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, 300.0f); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, 300.0f); }
   }



   namespace SkewRelease400Taper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, 400.0f); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, 400.0f); }
   }



   namespace SkewDelay250Taper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, 250.0f); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, 250.0f); }
   }



   namespace SkewGlide150Taper
   {
      inline float CentreForRange(float maxV) { return maxV <= 10.0f ? 0.15f : 150.0f; }
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, CentreForRange(maxV)); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, CentreForRange(maxV)); }
   }



   namespace SkewGlide100Taper
   {
      inline float CentreForRange(float maxV) { return maxV <= 10.0f ? 0.10f : 100.0f; }
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, CentreForRange(maxV)); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, CentreForRange(maxV)); }
   }



   namespace SkewStrum30Taper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, 30.0f); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, 30.0f); }
   }



   namespace SkewStrum20Taper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, 20.0f); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, 20.0f); }
   }



   namespace SkewFreqShifter12Taper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, 12.0f); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, 12.0f); }
   }



   namespace SkewStereoBass120Taper
   {
      inline float PosToValue(float pos01, float minV, float maxV) { return SkewTaperMath::PosToValue(pos01, minV, maxV, 120.0f); }
      inline float ValueToPos(float value, float minV, float maxV) { return SkewTaperMath::ValueToPos(value, minV, maxV, 120.0f); }
   }



   // Piecewise-linear -60..+12 dB console taper: unity sits at 75% of throw
   // and the range below -30 dB (near-inaudible) is compressed, instead of
   // giving 0 -> -12 dB (a large, obvious level change) and -60 -> -30 dB
   // (barely audible) equal pixel budget the way a linear-in-dB fader does.
   // Only valid for a -60..+12 dB range - callers with a different minV/maxV
   // should not opt in.
   namespace ConsoleFaderTaper
   {
      struct Point { float pos, db; };
      inline const Point kPoints[] = {
         { 0.00f, -60.0f }, { 0.15f, -45.0f }, { 0.30f, -30.0f }, { 0.45f, -20.0f },
         { 0.60f, -10.0f }, { 0.75f,   0.0f }, { 1.00f,  12.0f },
      };
      constexpr int kCount = sizeof(kPoints) / sizeof(kPoints[0]);
      // Detents drawn on a tapered fader, in dB - meaningless as even quarters
      // of the throw once the taper is nonlinear.
      inline const float kDetentsDb[] = { 0.0f, -10.0f, -20.0f, -30.0f, -60.0f };
      constexpr int kNumDetents = sizeof(kDetentsDb) / sizeof(kDetentsDb[0]);

      inline float PosToDb(float pos)
      {
         pos = std::clamp(pos, 0.0f, 1.0f);
         for (int i = 0; i + 1 < kCount; i++)
         {
            if (pos <= kPoints[i + 1].pos)
            {
               const float t = (pos - kPoints[i].pos) / (kPoints[i + 1].pos - kPoints[i].pos);
               return kPoints[i].db + (kPoints[i + 1].db - kPoints[i].db) * t;
            }
         }
         return kPoints[kCount - 1].db;
      }

      inline float DbToPos(float db)
      {
         if (db <= kPoints[0].db)
            return kPoints[0].pos;
         if (db >= kPoints[kCount - 1].db)
            return kPoints[kCount - 1].pos;
         for (int i = 0; i + 1 < kCount; i++)
         {
            if (db <= kPoints[i + 1].db)
            {
               const float t = (db - kPoints[i].db) / (kPoints[i + 1].db - kPoints[i].db);
               return kPoints[i].pos + (kPoints[i + 1].pos - kPoints[i].pos) * t;
            }
         }
         return kPoints[kCount - 1].pos;
      }

      inline float PosToValue(float pos01, float /*minV*/, float /*maxV*/) { return PosToDb(pos01); }
      inline float ValueToPos(float value, float /*minV*/, float /*maxV*/) { return DbToPos(value); }
   }

float AudioLabelFontSize();

void AudioLabelText(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text);

ImVec2 AudioLabelSize(const char* text);

bool AudioSliderFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                         float width, ImU32 fillColor, bool readOnly,
                         FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr,
                         bool vividState = false);

void DrawModulationBindingMenu(int nodeIndex, int paramIndex, bool hovered);

extern std::map<std::pair<int, int>, float> gDiscreteParamStore;

extern std::map<std::pair<int, int>, float> gDiscreteParamLastWritten;



   struct DiscreteParamHandle
   {
      bool registered = false; // false = not inside a node's param block
      bool draw = true;        // false while gParamRegisterOnly is set
      bool modulated = false;  // a cable is patched in: the widget goes read-only
      bool driven = false;     // modulated OR written from outside; push value back
      int nodeIndex = -1;
      int paramIndex = -1;
      float value = 0.0f;      // slot contents, i.e. what the apply pass last wrote
   };

std::string StripParamLabel(const char* label);

extern const void* gPendingSrcAddr;

const void* TakePendingSrcAddr(const void* fallback);

int KbDiscreteHook(int nodeIndex, int paramIndex, bool modulated);

DiscreteParamHandle RegisterDiscreteParam(const char* label, float current, float maxV,
                                             bool isBool, const std::vector<std::string>* options,
                                             bool momentary = false);

ImVec4 AccentEmphasisHover();

ImVec4 AccentEmphasisSelected();

ImVec4 AccentEmphasisPressed();

void PushPrimaryButtonStyle();

void PopPrimaryButtonStyle();

void PushDropdownStyle();

void PopDropdownStyle();

void PushElevatedPanelStyle(bool isChild);

void PopElevatedPanelStyle();

void PushDockedPanelStyle(bool isChild);

void PopDockedPanelStyle();

ImU32 PanelSeamColor();

void DrawPanelSeam(bool vertical, bool facesStart);

void ExpandPinHit(const ImVec2& c, float boxRight);

void DrawDiscreteParamPin(const DiscreteParamHandle& h, const char* label, float width,
                             float controlHeight = 0.0f);

void DropdownButton(const char* label, const std::vector<std::string>& options,
                       int current, std::function<void(int)> onSelect, float width,
                       bool showCaption);

void PushCheckboxStyle();

void PopCheckboxStyle();

void PushSliderStyle();

void PopSliderStyle();

extern std::set<ParamKey> gPredictorGrabs;

extern std::set<ParamKey> gPredictorGrabsPrev;

uint64_t UidForIndex(int nodeIndex);

const char* PredictorBindRefusal(INode* srcNode, int dstNodeIndex, int dstParamIndex);

bool IsInertPredictorBinding(int dstNodeIndex, int dstParamIndex);

bool IsPredictionSourceNode(const GraphNode* gn);

bool IsPredictionBinding(int nodeIndex, int paramIndex);

float ParamToPos(const ParamRef& r, float v);

float PosToParam(const ParamRef& r, float pos);



   struct PredictorGrabCtx
   {
      IPredictor* pred = nullptr;
      ParamKey key;
      bool editable = false; // draw the editable path instead of the read-only one
   };

PredictorGrabCtx BeginPredictorGrab(const ParamRef& ref);

void DrawPredictorDecor(const ParamRef& ref, float laneMin, float laneMax, float laneY);

void EndPredictorGrab(const PredictorGrabCtx& c, const ParamRef& ref);



   // Prediction green: pin ring, and the track/fill colours of a green-bound slider.
   inline constexpr ImU32 kPredictionPinCol = IM_COL32(110, 215, 140, 255);

bool ModCheckbox(const char* label, bool* value, bool* outUserChanged = nullptr);

bool ModSlider(const char* label, float* value, float minV, float maxV, const char* fmt = "%.3f",
                  float width = kParamWidth, bool audioStyle = false, float step = 0.0f,
                  FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr,
                  int explicitParamIndex = -1, const char* nameOverride = nullptr);

extern std::map<std::pair<int, int>, float> gIntParamStore;

extern std::map<std::pair<int, int>, int> gIntParamLastWritten;

bool ModSliderInt(const char* label, int* value, int minV, int maxV, float width = kParamWidth,
                     bool audioStyle = false);



   // Optional non-linear position<->value mapping for VFaderFloat / KnobFloat. nullptr
   // (the default at every call site but the console strip/channel faders)
   // means plain linear, i.e. the original behaviour. A future non-dB taper
   // could plug in here the same way - this is a general fader capability,
   // not dB-specific plumbing hardcoded into the widget.
   using FaderPosToValueFn = float (*)(float pos01, float minV, float maxV);


   using FaderValueToPosFn = float (*)(float value, float minV, float maxV);

bool VFaderFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                    float height, ImU32 fillColor, bool readOnly, float cellW = 0.0f,
                    FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr,
                    bool hasRange = false, float rangeLo = 0.0f, float rangeHi = 0.0f,
                    int gestureNodeIndex = -1, int gestureParamIndex = -1);

bool KnobFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                  float diameter, ImU32 fillColor, bool readOnly, float cellW = 0.0f,
                  FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr,
                  bool hasRange = false, float rangeLo = 0.0f, float rangeHi = 0.0f,
                  bool activeTint = false);

bool BipolarKnobFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                         float diameter, ImU32 fillColor, bool readOnly, float cellW = 0.0f,
                         int gestureNodeIndex = -1, int gestureParamIndex = -1,
                         bool hasRange = false, float rangeLo = 0.0f, float rangeHi = 0.0f,
                         bool activeTint = false, bool resetOnDoubleClick = false);

bool ModKnob(const char* label, float* value, float minV, float maxV, const char* fmt = "%.3f",
                float diameter = kKnobDiameter, float cellW = 0.0f,
                AudioWidgetStyle style = AudioWidgetStyle::Knob, float step = 0.0f,
                FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr,
                int explicitParamIndex = -1, const char* nameOverride = nullptr);

bool ModKnobInt(const char* label, int* value, int minV, int maxV, float diameter = kKnobDiameter,
                   float cellW = 0.0f);

void ColorSwatch(const char* label, float* col, const INode* owner);

void CollapsedBindingPins(int nodeIndex, const ImVec2& tagMin, const ImVec2& tagMax, bool colors);

void NodeSeparator(const char* label = nullptr, float width = kPreviewSize);

const std::vector<std::string>& AlignOptions();

bool EyeToggle(bool shown);

bool ViewportToggle(bool shown);

extern bool gGlobalScaleTooltipHovered;

extern bool gGlobalScaleTooltipEnabled;

bool GlobalScaleToggle(bool enabled);

bool* GetNodeGlobalScaleFlag(INode* node);

bool BypassToggle(bool bypassed);

void DrawPin(int pinId, ed::PinKind kind, const char* label, bool labelFirst = false);

#ifndef NDEBUG

   // Test-only predictor for INFINITE_PREDBINDTEST. Never registered in a release build, and in a
   // debug build only when that fixture asks for it, so ROUNDTRIPTEST's every-type sweep never
   // meets it. Returns a settable fixed position, and records how it was driven.
   class StubPredictorNode : public INode, public IModulator, public IPredictor
   {
   public:
      static INode* Create() { return new StubPredictorNode(); }
      unsigned int GetOutputTexture() override { return 0; }
      int GetOutputWidth() const override { return 0; }
      int GetOutputHeight() const override { return 0; }
      void CookIfNeeded(int) override {}
      float Value01() override { return pos; }

      void Tick(int, double) override { ++tickCount; }
      float ValuePos01For(const ParamKey& k, float) override { readKeys.insert(k); return pos; }
      void OnGrab(const ParamKey& k) override { ++grabCount; lastGrab = k; }
      void OnRelease(const ParamKey& k, float p, float v) override { ++releaseCount; lastRelease = k; releasePos = p; releaseVel = v; }

      void VisitParams(ParamVisitor& v) override { v.Float("pos", pos); }

      float pos = 0.5f;
      int tickCount = 0, grabCount = 0, releaseCount = 0;
      ParamKey lastGrab, lastRelease;
      float releasePos = -1.0f, releaseVel = 0.0f;
      std::set<ParamKey> readKeys;
   };
#endif

void RegisterNodes();

void ApplyTheme();

IModulator* ModulatorForOutput(INode* node, int outputIndex);

GraphNode* FindNodeByIndex(int index);

extern std::unordered_map<uint64_t, GraphNode*> gNodeByUid;

extern bool gNodeByUidDirty;

extern const GraphNode* gNodeByUidData;

extern size_t gNodeByUidSize;

void InvalidateNodeByUid();

void NoteNodeAppended();

void NoteNodeUidChanged(uint64_t oldUid, GraphNode* gn);

GraphNode* FindNodeByUid(uint64_t uid);

void ArrangeCommitEdit();



   // Runs `op` against gArrange and, if it changed anything (the revision
   // moved), pushes one timeline undo entry holding the pre-edit model.
   // Returns whether anything changed. The one shape every discrete timeline
   // edit (key, menu item, button) goes through.
   template <class Op>
   bool ArrangeEdit(Op&& op)
   {
      Arrange::Model before = gArrange;
      op();
      if (gArrange.revision == before.revision)
         return false;
      PushArrangeUndoSnapshot(before);
      ArrangeCommitEdit();
      return true;
   }

   // Continuous-gesture undo (drag, popup DragFloat, rename): snapshot at the
   // gesture's start, push at its end only if the revision moved.
   bool ArrangeGestureEnd();

void ArrangeGestureBegin();

bool ArrangeGestureEnd();

void PublishArrangeLoop();

void ArrangeSetLoop(bool enabled, Arrange::Tick start, Arrange::Tick end);



   // View settings that live in the model (so they save with the patch) but
   // are not edits: undo, redo and a drag's snapshot restore all carry the
   // live values across instead of rewinding them (WP5 dockSide, WP6 the
   // display unit and the snap grid).
   struct ArrangeViewSettings
   {
      int  dockSide = 0;
      int  timeDisplay = 0;
      int  snapDivision = 16;
      bool snapTriplet = false;
   };

ArrangeViewSettings ArrangeKeepViewSettings(const Arrange::Model& m);

void ArrangeRestoreViewSettings(Arrange::Model& m, const ArrangeViewSettings& v);

void ArrangeSetTimeDisplay(int mode);

void ArrangeSetSnap(int division, bool triplet);

Arrange::Tick ArrangeSnapGridTicks();

Arrange::Tick ArrangeNudgeStepTicks();

Arrange::Tick ArrangePlayTick();

void ArrangeSeekTick(Arrange::Tick t);

void ArrangeScrubBegin(Arrange::Tick t, ImGuiMouseButton button = ImGuiMouseButton_Left);

void ArrangeScrubUpdate(Arrange::Tick t);

bool ArrangeScrubEnd();

void ArrangeScrubCancel();

Arrange::Tick ArrangeEndKeyTargetTick();

ImU32 ArrangeMarkerColU32(uint32_t rgba);

uint32_t ArrangeMarkerRGBA(ImU32 col);


   inline constexpr uint32_t kArrangeDefaultMarkerRGBA = 0xF59E0BFFu;

 // amber

   // The one colour list the timeline's Color Tint menu and the marker menu
   // both offer. Entry 0 is "no tint" for clips and the default for markers.
   struct ArrangePaletteEntry { const char* name; ImU32 col; };


   inline const ArrangePaletteEntry kArrangePalette[10] = {
      { "Default", IM_COL32(110, 120, 140, 255) },
      { "Crimson", IM_COL32(239, 68, 68, 255) },
      { "Orange",  IM_COL32(249, 115, 22, 255) },
      { "Amber",   IM_COL32(245, 158, 11, 255) },
      { "Emerald", IM_COL32(16, 185, 129, 255) },
      { "Cyan",    IM_COL32(6, 182, 212, 255) },
      { "Blue",    IM_COL32(59, 130, 246, 255) },
      { "Purple",  IM_COL32(139, 92, 246, 255) },
      { "Magenta", IM_COL32(217, 70, 239, 255) },
      { "Rose",    IM_COL32(244, 63, 94, 255) }
   };



   // The snap grids the timeline offers: MusicTime RateDivision entries
   // (names and lengths come from that one table - rhythmic-quantization-
   // standard) mapped onto Settings::snapDivision / snapTriplet. rd -1 = Off.
   // INFINITE_ARRANGEMARKERTEST checks every row against MusicTime::BeatsFor.
   struct ArrangeGridChoice { int rd; int division; bool triplet; };


   inline const ArrangeGridChoice kArrangeGridChoices[10] = {
      { -1, 0, false },
      { MusicTime::k1Bar, 1, false },
      { MusicTime::kHalf, 2, false },          { MusicTime::kHalfTrip, 2, true },
      { MusicTime::kQuarter, 4, false },       { MusicTime::kQuarterTrip, 4, true },
      { MusicTime::kEighth, 8, false },        { MusicTime::kEighthTrip, 8, true },
      { MusicTime::kSixteenth, 16, false },    { MusicTime::kSixteenthTrip, 16, true },
   };

uint64_t ArrangeAddMarkerAtPlayhead();

bool ArrangeJumpToMarker(int dir);

void ArrangeModelToPatchData(const Arrange::Model& m, Patch::Data& data);

void PatchDataToArrangeModel(const Patch::Data& data, Arrange::Model& m,
                                const std::function<uint64_t(int)>& resolveLegacy = {});

IPaletteSource* PaletteSourceByIndex(int nodeIndex);

int InputCountFor(const GraphNode& gn);

bool CanBypass(const GraphNode& gn);

ImageCable* CableFor(GraphNode& gn, int slot);



   // Upper bound on how many geometry-input slots any single node exposes via
   // GeometryInputSlot() - the widest today is Group3DNode at 8. Generic loops over
   // "every geometry slot this node might have" stop here rather than walking off into unrelated pins.
   inline const int kMaxGeometrySlots = 8;



   // Upper bound on how many audio/note-input slots any single node exposes.
   // The widest today is Mixer, at MixerNode::kMaxSlots (12).
   inline const int kMaxAudioSlots = 12;


   // Every note-consuming node before AudioPluginNode carried its one note
   // pin at unified slot 0 (Sampler, Envelope, ...), which is why this was 1
   // and the topology builder's wiring pass (RebuildAudioTopology) used to
   // just call NoteInputSlot(0) directly rather than loop. AudioPluginNode's
   // note pin lives at slot 1 instead - so audio stays at slot 0 and existing
   // patches keep loading unchanged - which needed this bumped to 2 and that
   // wiring pass turned into a real loop; see its comment. NoteMergeNode's
   // 4-way fan-in (mirroring NoteRouterNode's 4-way fan-out) needed this
   // bumped again to 4.
   //
   // Mirrors AudioNode::kMaxNoteSlots (src/audio/AudioNode.h), which needs
   // the same bound for its fixed appliedInbox/appliedCursor arrays - kept
   // as a separate local alias rather than replacing every call site below
   // with the qualified name.
   inline const int kMaxNoteSlots = AudioNode::kMaxNoteSlots;

void RebuildAudioTopology();

void ForceAudioRepare();

void RemoveNodeByIndex(int index);

void ArrangeRespawnCloneNode(uint64_t clipId);

void ConnectGeometrySlot(GraphNode& dst, int slot, GraphNode& src, int srcOutput = 0);

bool IsInputSlotCompatible(GraphNode* dstNode, int slot,
                               bool srcIsModulator, IPaletteSource* srcPalette,
                               IGeometrySource* srcGeometry, CameraNode* srcCamera,
                               LightNode* srcLight,
                               bool srcIsEnvironment, bool srcIsAudioNode, bool srcIsNoteSource,
                               bool srcIsPredictor);

void WireInputSlot(GraphNode& srcNode, GraphNode& dstNode, int slot, int srcOutputIndex = 0);

bool WouldCreateAudioCycle(INode* src, INode* dst);

bool WouldCreateNoteCycle(INode* src, INode* dst);

bool ConnectNodes(int srcIndex, int srcOutputIndex, int dstIndex, int dstSlot, std::string& outError);



   // ---- ParamRef.key join results (see ParamKeyJoiner further down) ----
   struct ParamJoinType
   {
      std::map<int, std::string> keyOfParam; // paramIndex -> saved key
      std::map<std::string, int> paramOfKey;
      std::set<int> unkeyed;                 // registered controls with no key found
      std::map<int, std::string> unkeyedName; // their labels
      std::set<std::string> plainKeys;       // saved f/i/b keys no control registered
      std::map<std::string, std::vector<std::string>> optionsOfKey; // dropdown names by key
      std::vector<std::string> buttons;      // button labels the probe node drew, in draw order
      int registered = 0;
      bool done = false;
   };

extern std::map<std::string, ParamJoinType> gParamJoin;

extern std::map<int, std::vector<std::string>> gProbeButtons;

const PatchSchema::TypeSchema* SchemaFor(const std::string& typeName);

PatchSchema::Env MakeSchemaEnv(bool forRender);



   // Connections captured for a copy/duplicate cluster, in terms of *original*
   // gNodes indices - resolved against the fresh copies (and, for external
   // sources, re-validated against the live graph) only at apply time, since
   // the graph can change between capture and apply (Cmd+C to Cmd+V) or even
   // within the same frame (a node in the cluster could reference another
   // that failed to spawn).
   struct ClusterLink
   {
      int srcIndex = -1;       // orig gNodes index of the plain-slot source
      int srcOutputIndex = 0;
      int dstIndex = -1;       // orig gNodes index of the destination (always in the cluster)
      int dstSlot = 0;
   };


   struct ClusterModLink
   {
      int dstIndex = -1;
      int paramIndex = 0;
      Modulation::Source source; // source.nodeIndex is the ORIGINAL modulator's gNodes index
   };


   struct ClusterPaletteLink
   {
      int dstIndex = -1;
      int colorIndex = 0;
      int paletteOrigIndex = -1;
      int swatchIndex = 0;
   };


   // A typed expression on one of the cluster's params. Unlike a mod link
   // this names no source node - the text is self-contained (patch-wide
   // named values it reads live in ExprGlobals, which the copy shares) - so
   // there is nothing to rewire, only to carry across.
   struct ClusterExprLink
   {
      int dstIndex = -1;
      int paramIndex = 0;
      std::string text;
   };


   // A Shift-drag recording looping on one of the cluster's params. Session
   // state rather than patch content (see UndoEntry), and carried for the
   // same reason: the user sees a red, moving knob, so a copy of that node
   // that came back still is a copy of something they aren't looking at.
   struct ClusterGestureLink
   {
      int dstIndex = -1;
      int paramIndex = 0;
      GestureRecorder::Playback playback;
   };



   // Everything about a set of nodes that is NOT stored on the nodes
   // themselves: it all keys off (nodeIndex, paramIndex) or off a pin, so it
   // has to be captured before the copies are spawned and rewired onto them
   // afterwards. One struct rather than parallel vectors because there are
   // five capture/apply sites, and every kind added as a separate out-param
   // was another edit at all ten - which is exactly how expressions and
   // recordings came to be silently dropped by duplicate and paste.
   struct ClusterClipboard
   {
      std::vector<ClusterLink> links;
      std::vector<ClusterModLink> modLinks;
      std::vector<ClusterPaletteLink> paletteLinks;
      std::vector<ClusterExprLink> exprs;
      std::vector<ClusterGestureLink> gestures;
   };

void CaptureClusterLinks(const std::set<int>& indices, ClusterClipboard& out);

void ApplyClusterLinks(const std::map<int, GraphNode*>& newByOrig, const ClusterClipboard& clip);

std::vector<std::pair<std::string, std::string>> RecommendedNodeTypesForOutput(GraphNode* srcNode,
                                                                                    int srcOutputIndex = 0);

ImVec2 FindFreeSpawnPosition(const ImVec2& center);

GraphNode* SpawnNode(const std::string& typeName, const std::string& category,
                        float x = 0.0f, float y = 0.0f);

void ReloadDerivedState(INode* node);

void CopyParams(INode* dstNode, INode* srcNode);

void DrawImageSourceParams(ImageSourceNode* n);

void DrawSlideshowParams(SlideshowNode* n);

void DrawSyphonOutParams(SyphonOutNode* n);

void DrawSyphonInParams(SyphonInNode* n);

void DrawNdiOutParams(NdiOutNode* n);

void DrawNdiInParams(NdiInNode* n);

void DrawOscReceiveParams(OscReceiveNode* n);

void DrawOscSendParams(OscSendNode* n);

void DrawEnvironmentParams(EnvironmentNode* n);

void DrawShapeParams(ShapeNode* n);



   // Field build step 17: the "Devices" dropdown + Save/Export/Import row
   // shared by all five Field*Params functions (plan §6). `factoryNames`
   // is nullptr for FieldSampleNode/FieldGraphNode, which have no factory
   // Presets() table (plan §0.2) - the dropdown then shows only user
   // devices, no factory section/separator.
   //
   // `onFactorySelect` is called with a freshly-resolved NodeT* (never the
   // pointer captured at draw time) so that FieldElementNode's existing
   // `n->presetIndex = i; n->LoadPreset(i);` idiom keeps working unchanged
   // even though the actual selection happens on a later frame, once the
   // deferred dropdown popup (gDropdown) is clicked - same lifetime
   // discipline as the Save popup above.
   template <typename NodeT>
   void DrawFieldDeviceControls(NodeT* n, const std::string& domain,
                                const std::vector<std::string>* factoryNames,
                                std::function<void(NodeT*, int)> onFactorySelect)
   {
      const int nodeIndex = gCurrentNodeIndex;
      const FieldDeviceLibraryCache& lib = GetFieldDeviceLibrary(domain);

      std::vector<std::string> options;
      std::vector<std::string> categories;
      if (factoryNames != nullptr)
      {
         for (const std::string& s : *factoryNames)
         {
            options.push_back(s);
            categories.push_back("Factory");
         }
      }
      for (const std::string& s : lib.names)
      {
         options.push_back(s);
         categories.push_back("Devices");
      }
      const size_t factoryCount = factoryNames ? factoryNames->size() : 0;
      const std::vector<std::string> userPaths = lib.paths;

      PushDropdownStyle();
      std::string btnLabel = "Load device...";
      if (factoryNames != nullptr && n->presetIndex >= 0 && (size_t)n->presetIndex < factoryCount)
         btnLabel = (*factoryNames)[n->presetIndex];
      const std::string dropdownId = btnLabel + "##fdd_" + domain;
      const float spacing = ImGui::GetStyle().ItemSpacing.x;

      auto openDropdownAction = [=, &options, &categories, &userPaths](bool focusSearch)
      {
         gDropdown.options = options;
         gDropdown.categories = categories;
         gDropdown.current = (factoryNames != nullptr && n->presetIndex >= 0 && (size_t)n->presetIndex < factoryCount) ? n->presetIndex : -1;
         gDropdown.onSelect = [nodeIndex, onFactorySelect, factoryCount, userPaths, domain](int idx)
         {
            GraphNode* gn = FindNodeByIndex(nodeIndex);
            NodeT* n2 = gn ? dynamic_cast<NodeT*>(gn->node.get()) : nullptr;
            if (n2 == nullptr)
               return;
            if ((size_t)idx < factoryCount)
            {
               if (onFactorySelect)
                  onFactorySelect(n2, idx);
               return;
            }
            const size_t userIdx = (size_t)idx - factoryCount;
            if (userIdx >= userPaths.size())
               return;
            Field::DeviceFile device;
            std::string err;
            if (Field::LoadFromFieldFile(userPaths[userIdx], device, err) && device.domain == domain)
               n2->LoadDeviceFile(device);
         };
         gDropdown.justOpened = true;
         gDropdown.focusSearch = focusSearch;
         gDropdown.filterBuf[0] = '\0';
      };

      if (options.empty())
      {
         ImGui::BeginDisabled();
         ImGui::Button(dropdownId.c_str(), ImVec2(kPreviewSize, 0));
         ImGui::EndDisabled();
      }
      else
      {
         if (ImGui::Button(dropdownId.c_str(), ImVec2(kPreviewSize, 0)))
            openDropdownAction(true);
      }
      PopDropdownStyle();

      const float btnW = (kPreviewSize - 2.0f * spacing) / 3.0f;
      if (ImGui::Button(("Save##fdd_" + domain).c_str(), ImVec2(btnW, 0)))
      {
         snprintf(gFieldDeviceSave.nameBuf, sizeof(gFieldDeviceSave.nameBuf), "Untitled");
         gFieldDeviceSave.domain = domain;
         gFieldDeviceSave.getDeviceFile = [nodeIndex](Field::DeviceFile& out) -> bool
         {
            GraphNode* gn = FindNodeByIndex(nodeIndex);
            NodeT* n2 = gn ? dynamic_cast<NodeT*>(gn->node.get()) : nullptr;
            if (n2 == nullptr)
               return false;
            out = n2->ToDeviceFile();
            return true;
         };
         gFieldDeviceSave.justOpened = true;
      }
      ImGui::SameLine();
      if (ImGui::Button(("Export##fdd_" + domain).c_str(), ImVec2(btnW, 0)))
      {
         const std::string path = Platform::SaveDeviceDialog("Untitled.field");
         if (!path.empty())
            Field::SaveToFieldFile(path, n->ToDeviceFile());
      }
      ImGui::SameLine();
      if (ImGui::Button(("Import##fdd_" + domain).c_str(), ImVec2(btnW, 0)))
      {
         const std::string path = Platform::OpenDeviceDialog();
         if (!path.empty())
         {
            Field::DeviceFile device;
            std::string err;
            if (Field::LoadFromFieldFile(path, device, err) && device.domain == domain)
               n->LoadDeviceFile(device);
         }
      }
   }

void DrawFormulaParams(FormulaNode* n);

void DrawTextParams(TextNode* n);

void DrawVideoParams(VideoSourceNode* n);

void DrawVideoInParams(VideoInNode* n);

void DrawFitParams(FitNode* n);

void DrawProjectionHandleOverlay(ProjectionNode* node, ImVec2 origin, ImVec2 imageSize, const char* btnIdSuffix);

void DrawProjectionPreview(ProjectionNode* node);

void DrawProjectionParams(ProjectionNode* n);

void DrawLFOParams(LFONode* n);

void DrawRandomParams(RandomNode* n);



   // Drag/paint state for the pattern step grid. A horizontal paint gesture
   // spans many frames and, unlike a click-to-toggle grid, needs to remember
   // which bar the mouse was over last frame - otherwise a fast drag leaves
   // gaps between samples instead of a continuous paint.
   struct PatternGridDragState
   {
      PatternNode* node = nullptr;
      int lastStep = -1;
   };

extern PatternGridDragState gPatternGridDrag;

void DrawPatternParams(PatternNode* n);

void DrawMathParams(MathNode* n);

void DrawCompareParams(CompareNode* n);

void DrawRangeToRangeParams(RangeToRangeNode* n);

void DrawSmoothParams(SmoothNode* n);

void DrawInvertParams(InvertNode* n);

void DrawModDepthParams(ModDepthNode* n);

void DrawNoiseParams(NoiseNode* n);

void DrawTextureParams(TextureNode* n);

void DrawSwitcherParams(SwitcherNode* n);

void DrawResynthParams(ResynthNode* n);

void DrawMacroKnobBody(MacroKnobNode* n);

void DrawMacroKnobParams(MacroKnobNode* n);

void DrawMacroSliderBody(MacroSliderNode* n);

void DrawMacroSliderParams(MacroSliderNode* n);

void DrawMacroBipolarKnobBody(MacroBipolarKnobNode* n);

void DrawMacroBipolarKnobParams(MacroBipolarKnobNode* n);

void DrawMacroToggleBody(MacroToggleNode* n);

void DrawMacroToggleParams(MacroToggleNode* n);

void DrawMacroTriggerBody(MacroTriggerNode* n);

void DrawMacroTriggerParams(MacroTriggerNode* n);

void DrawMacroNumBoxBody(MacroNumBoxNode* n);

void DrawMacroNumBoxParams(MacroNumBoxNode* n);

void DrawMacroRadioSelectorBody(MacroRadioSelectorNode* n);

void DrawMacroRadioSelectorParams(MacroRadioSelectorNode* n);

void DrawMacroStepGateBody(MacroStepGateNode* n);

void DrawMacroStepGateParams(MacroStepGateNode* n);

void DrawMidiCCParams(MidiCCNode* n);

void DrawMidiTriggerParams(MidiTriggerNode* n);

void DrawMacroXYBody(MacroXYNode* n);

void DrawMacroXYParams(MacroXYNode* n);

void DrawCurvesParams(CurvesNode* n);

void DrawPredictiveColoringParams(PredictiveColoringNode* n);

void DrawModCurveParams(ModCurveNode* n);

void DrawRemoveBgParams(RemoveBgNode* n);

void DrawFeedbackParams(FeedbackNode*);

void DrawTrailsParams(TrailsNode* n);

void DrawReactionDiffusionParams(ReactionDiffusionNode* n);

void DrawPalettePreview(PaletteNode* n);

void DrawPaletteParams(PaletteNode* n);

void DrawRampParams(RampNode* n);

void DrawColorRampParams(ColorRampNode* n);

void DrawImageAnalyzeParams(ImageAnalyzeNode* n);

void DrawNullModulatorParams(NullModulatorNode* n);

extern float gAudioBodyW;

extern float gAudioBodyX;

extern float gAudioContentX;

extern float gAudioContentW;

extern CategoryColors::Color gAudioTint;

extern std::map<std::pair<int, int>, float> gAudioSectionHeight;

extern int gAudioSectionIndex;

extern float gAudioSectionTop;

void BeginAudioBody(int nodeIndex, const std::string& category, float width, const char* idleStat);



   // ---- columns ----------------------------------------------------------
   // Splits the body into N side-by-side columns. Every helper below already
   // derives from gAudioBodyX/gAudioBodyW (panels) and gAudioContentX/
   // gAudioContentW (rows and sliders), so pointing those four at a column is
   // all it takes for sections, knob rows and sliders to lay out inside it -
   // the "width is a scope" rule doing exactly what it was written for.
   struct AudioColumnState
   {
      float bodyX, bodyW, contentX, contentW, top, maxBottom;
      int count;
   };

extern AudioColumnState gAudioColumn;

void BeginAudioColumns(int count);

void BeginAudioColumn(int index);

void EndAudioColumn();

void EndAudioColumns();

void EndAudioBody();

void BeginAudioSection(const char* label);

void EndAudioSection();



   // A row of N equal cells across the current content column. Cell width is
   // gAudioContentW / N, so a row is exactly the content width by
   // construction and the v2 overflow (a 556px row inside a 440px node) is
   // structurally impossible rather than something to remember to check.
   //
   // Cells bottom-align on the largest control in the row, so mixed
   // large/small knobs share one caption baseline - the thing that makes a
   // row read as a rack strip rather than as dials scattered at random
   // heights.
   struct AudioKnobRow
   {
      float x0, y0, cellW, maxDia, rowH, headerH;
      int count, index;

      // `headerRowH` reserves a strip above the knobs for cells that carry a
      // mode dropdown over their knob (see DropdownKnob). Plain knobs in the
      // same row drop below it, so every cap and every caption in the row
      // still shares one baseline - the alternative, letting the dropdown
      // cells be taller, is exactly the ragged row the bottom-align exists to
      // prevent.
      // gParamRegisterOnly (a collapsed node, or - see DrawFieldElementParams
      // et al. - a headless param-declaration test that runs before ImGui
      // even has a context) means every ImGui:: call below is unsafe: there
      // may be no current window, or no context at all. y0/rowH/Place() are
      // only ever read to position real widgets, so it is safe to leave them
      // at 0 in that mode - nothing downstream dereferences them before the
      // early-outs in Knob()/Checkbox()/KnobInt()/Dropdown() below skip the
      // draw.
      AudioKnobRow(int cellCount, float maxDiameter = kKnobSmall, float headerRowH = 0.0f, bool hasCaptions = true)
      {
         count = std::max(1, cellCount);
         maxDia = maxDiameter;
         headerH = headerRowH;
         x0 = gAudioContentX;
         y0 = gParamRegisterOnly ? 0.0f : ImGui::GetCursorScreenPos().y;
         cellW = gAudioContentW / (float)count;
         index = 0;
         rowH = headerH + maxDia + (hasCaptions && !gParamRegisterOnly ? (4.0f + ImGui::GetTextLineHeight()) : 0.0f);
      }

      void Place(float dia) const
      {
         if (gParamRegisterOnly)
            return;
         ImGui::SetCursorScreenPos(ImVec2(x0 + (float)index * cellW, y0 + headerH + (maxDia - dia)));
      }

      void Knob(const char* label, float* v, float lo, float hi, const char* fmt,
                float dia = kKnobSmall, bool dbTaper = false, bool freqTaper = false,
                AudioWidgetStyle explicitStyle = AudioWidgetStyle::Knob,
                FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr,
                int explicitParamIndex = -1, const char* nameOverride = nullptr)
      {
         Place(dia);
         AudioWidgetStyle style = explicitStyle;
         if (style == AudioWidgetStyle::Knob && posToValue == nullptr)
         {
            if (dbTaper)
            {
               style = AudioWidgetStyle::KnobDb;
            }
            else if (freqTaper || (lo > 0.0f && (hi / lo >= 9.0f) &&
                     (strstr(fmt, "Hz") != nullptr || strcmp(label, "freq") == 0 || strcmp(label, "cutoff") == 0 || strcmp(label, "filter") == 0)))
            {
               style = AudioWidgetStyle::KnobFreq;
            }
            else if (lo > 0.0f && (hi / lo >= 9.0f) &&
                     (strstr(fmt, "ms") != nullptr || (strstr(fmt, "s") != nullptr && strstr(fmt, "st") == nullptr && strstr(fmt, "step") == nullptr && strstr(fmt, "semi") == nullptr)))
            {
               style = AudioWidgetStyle::KnobLog;
            }
         }
         ModKnob(label, v, lo, hi, fmt, dia, cellW, style, 0.0f, posToValue, valueToPos, explicitParamIndex,
                 nameOverride);
         index++;
      }

      void KnobInt(const char* label, int* v, int lo, int hi, float dia = kKnobSmall)
      {
         Place(dia);
         ModKnobInt(label, v, lo, hi, dia, cellW);
         index++;
      }

      bool Button(const char* label)
      {
         if (gParamRegisterOnly)
         {
            index++;
            return false;
         }
         const float cellX0 = x0 + (float)index * cellW;
         const float btnH = ImGui::GetFrameHeight();
         const float btnY = y0 + headerH + (maxDia - btnH) * 0.5f;
         const float btnW = std::min(cellW - 8.0f, 96.0f);
         const float btnX = cellX0 + (cellW - btnW) * 0.5f;
         ImGui::SetCursorScreenPos(ImVec2(btnX, btnY));
         const bool clicked = ImGui::Button(label, ImVec2(btnW, 0));
         index++;
         return clicked;
      }

      // A vertical fader occupying one cell of the same row. `height` plays
      // the role `dia` does for a knob, so a row of faders bottom-aligns with
      // a row of knobs on the same caption baseline. `dbTaper` opts into the
      // console taper (unity at 75% of throw) - only meaningful for a
      // -60..+12 dB range, so a symmetric trim like Audio In leaves it off.
      void Fader(const char* label, float* v, float lo, float hi, const char* fmt, float height,
                 bool dbTaper = false)
      {
         Place(height);
         ModKnob(label, v, lo, hi, fmt, height, cellW, dbTaper ? AudioWidgetStyle::VFaderDb : AudioWidgetStyle::VFader);
         index++;
      }

      // An enum param as a cell of the same pitch: button on top, caption
      // underneath in the same style and on the same baseline as a knob's,
      // so it reads as one of the row. v2's DropdownButton put its label to
      // the *right* of the button, which both broke the row's rhythm and ate
      // ~60px of its width.
      void Dropdown(const char* label, const std::vector<std::string>& options, int current,
                    std::function<void(int)> onSelect,
                    const std::vector<std::string>& categories = {})
      {
         if (options.empty())
         {
            index++;
            return;
         }
         const float cellX0 = x0 + (float)index * cellW;
         const float btnH = ImGui::GetFrameHeight();
         const float btnY = y0 + headerH + (maxDia - btnH) * 0.5f;
         const int lastIndex = (int)options.size() - 1;
         int safe = std::max(0, std::min(current, lastIndex));

         const DiscreteParamHandle h =
            RegisterDiscreteParam(label, (float)safe, (float)lastIndex, /*isBool=*/false, &options);

         const float knobW = (maxDia >= kKnobLarge) ? kKnobLarge : kKnobSmall;
         const float pinX = cellX0 + std::max(2.0f, (cellW - knobW) * 0.5f - 12.0f - 8.0f);
         const float pinW = 16.0f;
         float btnX = cellX0 + 4.0f;
         float btnW = std::min(cellW - 8.0f, 118.0f);

         if (h.registered)
         {
            if (h.driven)
            {
               const int drivenIdx = std::clamp((int)lroundf(h.value), 0, lastIndex);
               if (drivenIdx != current && onSelect)
               {
                  // Modulation never creates an undo entry or dirties the patch:
                  // most onSelect lambdas open with PushUndoCheckpoint() for the
                  // user-click path, so suppress it here. The param write still runs.
                  const bool wasSuppressed = gSuppressUndoCheckpoints;
                  gSuppressUndoCheckpoints = true;
                  onSelect(drivenIdx);
                  gSuppressUndoCheckpoints = wasSuppressed;
               }
               safe = drivenIdx;
            }
            if (!h.draw)
            {
               index++;
               return; // registered so the modulator keeps writing; just not drawn
            }
            ImGui::SetCursorScreenPos(ImVec2(pinX, btnY + (btnH - 12.0f) * 0.5f));
            DrawDiscreteParamPin(h, label, cellW - (pinX - cellX0) - pinW);
            btnX = pinX + pinW;
            const float btnRight = cellX0 + cellW - std::max(4.0f, (pinX - cellX0));
            btnW = std::max(20.0f, std::min(btnRight - btnX, 118.0f));
         }

         ImGui::SetCursorScreenPos(ImVec2(btnX, btnY));

         const std::string caption = options[safe] + "##" + label;
         PushDropdownStyle();
         if (h.modulated)
         {
            ImGui::PushStyleColor(ImGuiCol_Text, IsThemeLight() ? ImVec4(0.55f, 0.38f, 0.10f, 1.0f)
                                                                : ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
            ImGui::BeginDisabled();
            ImGui::Button(caption.c_str(), ImVec2(btnW, 0));
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            DrawModulationBindingMenu(h.nodeIndex, h.paramIndex,
                                      ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(),
                                                                 ImGui::GetItemRectMax()));
         }
         else
         {
            if (ImGui::Button(caption.c_str(), ImVec2(btnW, 0)) ||
                DropdownTestWantsOpen(h.registered, h.nodeIndex, h.paramIndex))
            {
               gDropdown.options = options;
               gDropdown.categories = categories;
               gDropdown.onSelect = std::move(onSelect);
               gDropdown.current = safe;
               gDropdown.justOpened = true;
               gDropdown.focusSearch = false;
            }
            if (h.registered)
               DrawModulationBindingMenu(h.nodeIndex, h.paramIndex, ImGui::IsItemHovered());
         }
         PopDropdownStyle();

         index++;
      }

      // One cell holding a mode dropdown directly above its knob - the shape
      // the reference sketch asks for wherever a knob's meaning is set by a
      // selector ("which filter", "which warp"). Reading them as one control
      // is the point: the pair is a single decision, and splitting them across
      // two rows made the knob look like it belonged to whatever sat above it.
      // `knobDisabled` greys the knob only. The dropdown must stay live: the
      // knob is meaningless precisely when the mode is "off", and disabling
      // the whole cell would leave no way to select any other mode.
      void DropdownKnob(const char* dropId, const std::vector<std::string>& options, int current,
                        std::function<void(int)> onSelect, const char* knobLabel, float* v,
                        float lo, float hi, const char* fmt, bool knobDisabled = false,
                        float dia = kKnobSmall, bool freqTaper = false)
      {
         if (!options.empty())
         {
            const float cellX0 = x0 + (float)index * cellW;
            const float btnH = ImGui::GetFrameHeight();
            const int lastIndex = (int)options.size() - 1;
            int safe = std::max(0, std::min(current, lastIndex));

            // Modulatable, same as AudioKnobRow::Dropdown above.
            const DiscreteParamHandle h =
               RegisterDiscreteParam(dropId, (float)safe, (float)lastIndex, /*isBool=*/false, &options);
            bool drawIt = true;
            if (h.registered)
            {
               if (h.driven)
               {
                  const int drivenIdx = std::clamp((int)lroundf(h.value), 0, lastIndex);
                  if (drivenIdx != current && onSelect)
                  {
                     // Modulation never creates an undo entry or dirties the patch:
                     // most onSelect lambdas open with PushUndoCheckpoint() for the
                     // user-click path, so suppress it here. The param write still runs.
                     const bool wasSuppressed = gSuppressUndoCheckpoints;
                     gSuppressUndoCheckpoints = true;
                     onSelect(drivenIdx);
                     gSuppressUndoCheckpoints = wasSuppressed;
                  }
                  safe = drivenIdx;
               }
               drawIt = h.draw;
            }
            if (drawIt)
            {
               const float knobW = dia;
               const float pinX = cellX0 + std::max(2.0f, (cellW - knobW) * 0.5f - 12.0f - 8.0f);
               const float pinW = 16.0f;
               float btnX = cellX0 + 4.0f;
               float btnW = std::min(cellW - 8.0f, 112.0f);

               if (h.registered)
               {
                  ImGui::SetCursorScreenPos(ImVec2(pinX, y0 + (btnH - 12.0f) * 0.5f));
                  DrawDiscreteParamPin(h, dropId, cellW - (pinX - cellX0) - pinW);
                  btnX = pinX + pinW;
                  const float btnRight = cellX0 + cellW - std::max(4.0f, (pinX - cellX0));
                  btnW = std::max(20.0f, std::min(btnRight - btnX, 112.0f));
               }

               ImGui::SetCursorScreenPos(ImVec2(btnX, y0));

               const std::string caption = options[safe] + "##" + dropId;
               PushDropdownStyle();
               if (h.modulated)
               {
                  ImGui::PushStyleColor(ImGuiCol_Text, IsThemeLight() ? ImVec4(0.55f, 0.38f, 0.10f, 1.0f)
                                                                      : ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
                  ImGui::BeginDisabled();
                  ImGui::Button(caption.c_str(), ImVec2(btnW, 0));
                  ImGui::EndDisabled();
                  ImGui::PopStyleColor();
                  DrawModulationBindingMenu(h.nodeIndex, h.paramIndex,
                                            ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(),
                                                                       ImGui::GetItemRectMax()));
               }
               else
               {
                  if (ImGui::Button(caption.c_str(), ImVec2(btnW, 0)))
                  {
                     gDropdown.options = options;
                     gDropdown.categories.clear(); // this call site has no category grouping - drop whatever the last dropdown left behind
                     gDropdown.onSelect = std::move(onSelect);
                     gDropdown.current = safe;
                     gDropdown.justOpened = true;
                     gDropdown.focusSearch = false;
                  }
                  if (h.registered)
                     DrawModulationBindingMenu(h.nodeIndex, h.paramIndex, ImGui::IsItemHovered());
               }
               PopDropdownStyle();
            }
         }
         Place(dia);
         if (knobDisabled)
            ImGui::BeginDisabled();
         AudioWidgetStyle style = AudioWidgetStyle::Knob;
         if (freqTaper || (lo > 0.0f && (hi / lo >= 9.0f) && (strstr(fmt, "Hz") != nullptr || strcmp(knobLabel, "freq") == 0 || strcmp(knobLabel, "cutoff") == 0 || strcmp(knobLabel, "filter") == 0)))
            style = AudioWidgetStyle::KnobFreq;
         ModKnob(knobLabel, v, lo, hi, fmt, dia, cellW, style);
         if (knobDisabled)
            ImGui::EndDisabled();
         index++;
      }

      // Same contract as ModCheckbox: the return value reports a click OR a
      // cable-driven flip (callers copy it into node state), and the optional
      // outUserChanged is set only by a real click. A caller that pushes an
      // undo checkpoint must key it on outUserChanged, so modulation never
      // creates an undo entry or dirties the patch.
      bool Checkbox(const char* label, bool* value, bool* outUserChanged = nullptr)
      {
         if (outUserChanged)
            *outUserChanged = false;
         if (value == nullptr)
         {
            index++;
            return false;
         }
         const DiscreteParamHandle h =
            RegisterDiscreteParam(label, *value ? 1.0f : 0.0f, 1.0f, /*isBool=*/true, nullptr);

         bool changed = false;
         if (h.registered)
         {
            const bool incoming = *value;
            if (h.driven)
            {
               *value = h.value >= 0.5f;
               changed = (*value != incoming);
            }
            if (!h.draw)
            {
               index++;
               return changed;
            }
         }

         // Geometry math below is unsafe under gParamRegisterOnly (no ImGui
         // context guaranteed - see AudioKnobRow's constructor comment), but
         // h.draw is exactly !gParamRegisterOnly, so the `if (!h.draw) return`
         // above already exits before this point whenever that mode is on.
         const float cellX0 = x0 + (float)index * cellW;
         const float knobW = (maxDia >= kKnobLarge) ? kKnobLarge : kKnobSmall;
         const float pinX = cellX0 + std::max(2.0f, (cellW - knobW) * 0.5f - 12.0f - 8.0f);
         const float pinW = 16.0f;
         const float checkY = y0 + headerH + (maxDia - ImGui::GetFrameHeight()) * 0.5f;

         if (h.registered)
         {
            ImGui::SetCursorScreenPos(ImVec2(pinX, checkY + (ImGui::GetFrameHeight() - 12.0f) * 0.5f));
            DrawDiscreteParamPin(h, label, cellW - (pinX - cellX0) - pinW);
         }

         const float checkX = pinX + (h.registered ? pinW : 0.0f);
         ImGui::SetCursorScreenPos(ImVec2(checkX, checkY));

         PushCheckboxStyle();
         if (h.modulated)
         {
            bool shown = *value;
            ImGui::PushStyleColor(ImGuiCol_CheckMark, IsThemeLight() ? ImVec4(0.84f, 0.49f, 0.08f, 1.0f)
                                                                     : ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
            ImGui::BeginDisabled();
            ImGui::Checkbox(label, &shown);
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            DrawModulationBindingMenu(h.nodeIndex, h.paramIndex,
                                      ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(),
                                                                 ImGui::GetItemRectMax()));
         }
         else
         {
            const bool clicked = ImGui::Checkbox(label, value);
            if (outUserChanged)
               *outUserChanged = clicked;
            changed = clicked || changed;
            if (h.registered)
               DrawModulationBindingMenu(h.nodeIndex, h.paramIndex, ImGui::IsItemHovered());
         }
         PopCheckboxStyle();

         index++;
         return changed;
      }

      void Skip() { index++; }

      void End() const
      {
         if (gParamRegisterOnly)
            return;
         ImGui::SetCursorScreenPos(ImVec2(x0, y0));
         ImGui::Dummy(ImVec2(gAudioContentW, rowH));
      }
   };



   // The multi-destination trace. A Drift node writes a DIFFERENT value into every knob it is
   // dragged onto - independent slots, independent learned landscapes - so the old single
   // 64-bin histogram of slot 0, drawn as if it were "the node's" output, was showing one
   // destination's shape and implying it was all of them. One graph, one line per destination,
   // each line labelled and carrying its own live value; there is deliberately no aggregate
   // number anywhere, because no such number exists for this node.
   //
   // History lives here rather than in DriftNode because it is pure presentation: nothing in the
   // dynamics reads it, it must not be saved, and a node that is never drawn should not pay for
   // it. Keyed by (source node UID, destination) so two Drift nodes on the same knob stay
   // separate - uid, not index, because RemoveNodeByIndex reuses indices and a new node would
   // otherwise inherit a deleted one's trace (Modulation.h's uid stability rule).
   inline constexpr int kDriftTraceLen = 128;


   struct DriftTrace
   {
      float v[kDriftTraceLen] = {};
      int head = 0;
      int filled = 0;
      int lastFrame = -1;
   };

extern std::map<std::pair<uint64_t, ParamKey>, DriftTrace> gDriftTraces;

void DrawDriftMeter(DriftNode* n, int nodeIndex);

void DrawDriftParams(GraphNode& gn, DriftNode* n);

void DrawMovesParams(MovesNode* n);

void DrawPredictiveModulatorParams(PredictiveModulatorNode* n);



   // Field-declared params (`param float ...`) get explicit pin indices from
   // ParamTable's own id counter (starting at 1) so they stay stable across
   // recompiles even as native controls are added/removed above them in the
   // same node body. Offsetting them into their own sub-range keeps them from
   // colliding with gParamCounter-numbered native controls drawn earlier in
   // the same node (see PINDUPTEST / FieldPrimitiveNode's "count"+"max
   // elements" collision: two native int sliders claimed indices 0 and 1,
   // and the first declared param, with p.id == 1, collided with the second).
   inline const int kFieldDeclaredParamBase = 50;



   template <typename NodeT>
   void DrawFieldParamSliders(NodeT* n)
   {
      auto& allParams = n->GetParamTable().Params();
      for (auto& p : allParams)
      {
         if (p.isDeclared)
         {
            const int paramIndex = kFieldDeclaredParamBase + p.id;
            ModSlider(p.name.c_str(), &p.value, p.minValue, p.maxValue, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, paramIndex);
         }
      }
   }

float AudioFullWidth();

float AudioHalfWidth();

bool AudioSlider(const char* label, float* v, float lo, float hi, const char* fmt, float width,
                    FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr,
                    int explicitParamIndex = -1, const char* nameOverride = nullptr);

bool AudioSliderInt(const char* label, int* v, int lo, int hi, float width);

bool AudioToggleButton(const char* label, bool* value, float width = 44.0f, float height = 0.0f);



   // A momentary button that is also a CV-gate destination. Draws the
   // modulation pin (vertically centred on the control, P2), then an invisible
   // button that `paint` dresses; returns the button's LEVEL - CV high when a
   // cable drives it, else the mouse being held on it. The node turns level
   // changes into edges (Looper::SetButtonLevel / Mpc::SetPadHeld), so a held
   // mouse or a held CV presses exactly once. `paint(dl, min, max, hovered,
   // level)` draws the face; `activated` reports a fresh mouse press.
   using GatePainter = std::function<void(ImDrawList*, ImVec2, ImVec2, bool, bool)>;

bool DrawGateButton(const char* id, float totalW, float height, const GatePainter& paint,
                       bool* activated = nullptr);

bool DrawGateControl(const char* id, const char* label, float btnW, int style, bool lit);

bool AudioSoloButton(const char* label, bool* value, float width = 26.0f, float height = 0.0f);

bool AudioMuteButton(const char* label, bool* value, float width = 26.0f, float height = 0.0f);

bool AudioSmallButton(const char* label, float width = 26.0f, float height = 0.0f);

bool IsAudioBodyNode(INode* node);



   // MPC: four columns of (16 px gate pin gutter + a square pad), so the pads
   // can be 105 px a side (+25% on the 440 px node's ~84). The node is as wide
   // as that grid needs, not the shared 440.
   inline constexpr float kMpcPadSide = 105.0f;


   inline constexpr float kMpcPinGutter = 16.0f;

float MpcNodeWidth();

float AudioNodeWidth(INode* node);

void DrawWavetableScope(WavetableNode* n, float h, float width);

void DrawFieldSampleScope(FieldSampleNode* n, float h, float width);

void DrawFieldSynthScope(FieldSynthNode* n, float h, float width);

void DrawFieldElementParams(FieldElementNode* n);

void DrawFieldPrimitiveParams(FieldPrimitiveNode* n);

void DrawFieldSampleParams(FieldSampleNode* n);

void DrawFieldSynthParams(FieldSynthNode* n);

void DrawFieldGraphParams(FieldGraphNode* n);

void DrawFieldPixelParams(FieldPixelNode* n);

void DrawSamplerWaveform(SamplerNode* n, float h, float width);

void DrawSlicerWaveform(SlicerNode* n, float h, float width);

void DrawPaulStretchWaveform(PaulStretchNode* n, float h, float width);

void DrawGranularWaveform(GranularNode* n, float h, float width);

void DrawStripMeter(float x, float y, float w, float h, float level);

void PublishWtTestRect();



   // Directly editable ADSR: four draggable handles on the curve itself.
   // Attack and decay are the x of their own corner, sustain the y of the
   // sustain shelf, release the x of the tail - which is the mapping every
   // envelope editor uses, so it needs no explanation to anyone who has seen
   // ADSR Layout Geometry: computes relative, adaptive timebase points
   // so envelopes always span across the visualizer width with proper visual weight
   struct ADSRLayout
   {
      ImVec2 p0; // start (0, 0)
      ImVec2 pA; // attack peak (Ax, 1.0)
      ImVec2 pD; // decay end / sustain start (Dx, S)
      ImVec2 pS; // sustain end / release start (Sx, S)
      ImVec2 pR; // release end (Rx, 0.0)
      float x0, topY, baseY, spanY;
      float wA, wD, wShelf, wR;
      float timeW; // usable width minus the shelf - what wA/wD/wR are drawn from
   };

ADSRLayout ComputeADSRLayout(ImVec2 origin, float w, float h, float attackMs, float decayMs,
                                       float sustain, float releaseMs, float maxTimeMs = 4000.0f);

const std::vector<std::string>& WavetableNames();

const std::vector<std::string>& OctaveNames();

void AudioBareDropdown(const char* id, const std::vector<std::string>& options, int current,
                          std::function<void(int)> onSelect, float width,
                          const std::vector<std::string>& categories = {},
                          bool focusSearch = false);

void DrawEnvelopePanel(const char* title, const char* curveId, float* attackMs, float* decayMs,
                          float* sustain, float* releaseMs, float* amount, float amountLo,
                          float amountHi, const char* amountFmt, ImU32 color);

void DrawWavetableBody(GraphNode& gn, WavetableNode* n);

void DrawOscillatorBody(GraphNode& gn, OscillatorNode* n);

void DrawMetallicBody(GraphNode& gn, MetallicNode* n);

void DrawWaveTerrainBody(GraphNode& gn, WaveTerrainNode* n);

void DrawEquationBody(GraphNode& gn, EquationNode* n);

void DrawImageSpectralSynthBody(GraphNode& gn, ImageSpectralSynthNode* n);

void DrawAudioMeterBody(GraphNode& gn, AudioMeterNode* n);

void DrawGainBody(GraphNode& gn, GainNode* n);
void DrawSpatialMixerBody(GraphNode& gn, SpatialMixerNode* n);
void DrawSpatialExportPanel(SpatialMixerNode* n);

void DrawBlendAudioBody(GraphNode& gn, BlendAudioNode* n);

void DrawSamplerBody(GraphNode& gn, SamplerNode* n);

void DrawSlicerBody(GraphNode& gn, SlicerNode* n);

void DrawPaulStretchBody(GraphNode& gn, PaulStretchNode* n);

void DrawMolderBody(GraphNode& gn, MolderNode* n);

void DrawGrainMolderBody(GraphNode& gn, GrainMolderNode* n);

void DrawGranularBody(GraphNode& gn, GranularNode* n);



   // Drag state for the step grid's paint/velocity gestures - file-scope
   // like gSampleDragActive, since a drag spans many frames and many
   // per-cell InvisibleButton calls. See DrawDrumSequencerBody's grid loop.
   struct DrumGridDragState
   {
      bool active = false;
      bool paintOn = false;
      int originLane = -1;
      int originStep = -1;
   };

extern DrumGridDragState gDrumGridDrag;

void DrawDrumSequencerBody(GraphNode& gn, DrumSequencerNode* n);

void DrawLooperBody(GraphNode& gn, LooperNode* n);

std::string MpcModeLabel(int pad);

std::string MpcParamName(int pad, int k);


   enum MpcDiscreteKind { kMpcMode = 0, kMpcSync, kMpcDiv, kMpcNumDiscrete };

std::string MpcDiscreteLabel(int pad, int which);

void MpcDropFiles(MpcNode* n, float cx, float cy, const std::vector<std::string>& paths);

void DrawMpcBody(GraphNode& gn, MpcNode* n);

const std::vector<std::string>& MediaTypeFilterNames();

std::vector<const SampleScanner::Entry*> FilterAndSortSampleEntries(
      const std::vector<SampleScanner::Entry>& index, const std::string& lowerQuery,
      const BrowserFilterState& state, bool mediaKind);

void DrawLibrarySearchPanel(SampleScanner& scanner, const char* idPrefix, const char* searchHint, bool mediaKind);

bool ILess(const std::string& a, const std::string& b);

std::vector<const PluginScanner::Entry*> FilterAndSortPluginEntries(
      const std::vector<PluginScanner::Entry>& index, const std::string& lowerQuery, const BrowserFilterState& state);

void DrawPluginSearchPanel();



   struct FieldSearchEntry
   {
      std::string name;
      std::string category;     // "Synth", "Effects", "Modifiers", "3D Shapes", "2D Visuals"
      std::string nodeType;     // "Field Synth", "Field Effect", "Field Modifier", "Field Primitive", "FieldPixel"
      std::string nodeCategory; // "Synths", "AudioEffects", "3D", "Source"
      int presetIndex = 0;
   };

void SpawnFieldPresetNode(const FieldSearchEntry& entry, float x, float y);

void DrawFieldSearchPanel();

void DrawAudioInBody(GraphNode& gn, AudioInputNode* n);

void DrawMixerBody(GraphNode& gn, MixerNode* n);

void DrawSplitterBody(GraphNode& gn, SplitterNode*);

void DrawMidiNotesBody(GraphNode& gn, MidiNotesNode* n);

void DrawKeyboardBody(GraphNode& gn, KeyboardNode* n);

const std::vector<std::string>& NoteNameList();

void DrawCVToPitchParams(CVToPitchNode* n);

void DrawNoteToCVParams(NoteToCVNode* n);

void DrawVelocityToCVParams(VelocityToCVNode* n);

void DrawCVRecorderParams(CVRecorderNode* n);

void DrawNoteFilterBody(GraphNode& gn, NoteFilterNode* n);

void DrawNoteTransposeBody(GraphNode& gn, NoteTransposeNode* n);

void DrawPitchBendBody(GraphNode& gn, PitchBendNode* n);

void DrawGateBody(GraphNode& gn, GateNode* n);

void DrawGlideBody(GraphNode& gn, GlideNode* n);

void DrawVibratoBody(GraphNode& gn, VibratoNode* n);

void DrawVelocityCurveBody(GraphNode& gn, VelocityCurveNode* n);

void DrawHumanizerBody(GraphNode& gn, HumanizerNode* n);

void DrawQuantizerBody(GraphNode& gn, QuantizerNode* n);

void DrawPredictiveQuantizeBody(GraphNode& gn, PredictiveQuantizeNode* n);

void DrawPredictiveVelocityBody(GraphNode& gn, PredictiveVelocityNode* n);

void DrawPredictiveRhythmBody(GraphNode& gn, PredictiveRhythmNode* n);

void DrawNoteEchoBody(GraphNode& gn, NoteEchoNode* n);

void DrawPredictiveNotesBody(GraphNode& gn, PredictiveNotesNode* n);

void DrawNoteMergeBody(GraphNode& gn, NoteMergeNode* n);

void DrawNoteSwitcherBody(GraphNode& gn, NoteSwitcherNode* n);

void DrawNoteRouterBody(GraphNode& gn, NoteRouterNode* n);



   // Drag state for the gate grid's paint gesture - file-scope like
   // gDrumGridDrag, since a drag spans many frames and many per-cell
   // InvisibleButton calls. A distinct struct from the drum grid's (rather
   // than reused) since this one has no lane dimension, just a step origin.
   struct ArpGateDragState
   {
      bool active = false;
      bool paintOn = false;
      int originStep = -1;
   };

extern ArpGateDragState gArpGateDrag;

void DrawArpeggiatorBody(GraphNode& gn, ArpeggiatorNode* n);

void DrawNoteSequencerBody(GraphNode& gn, NoteSequencerNode* n);

void DrawMidiFileBody(GraphNode& gn, MidiFileNode* n);

void DrawRandomNoteGeneratorBody(GraphNode& gn, RandomNoteGeneratorNode* n);

void DrawChorderBody(GraphNode& gn, ChorderNode* n);

void DrawNoteStackBody(GraphNode& gn, NoteStackNode* n);

void DrawNoteCapturerBody(GraphNode& gn, NoteCapturerNode* n);

void DrawBouncingBallsBody(GraphNode& gn, BouncingBallsNode* n);

void DrawStrumBody(GraphNode& gn, NoteStrumNode* n);

void DrawAudioToCVBody(GraphNode& gn, AudioToCVNode* n);

void DrawEnvelopeBody(GraphNode& gn, EnvelopeNode* n);



   // ---- Audio Filter -----------------------------------------------------
   // Cached per-node response curve. MagnitudeDb (AudioFilterKernel.h) used
   // to be a settled-sine simulation of up to ~8000 samples per point (up to
   // ~1.28M simulated samples per 160-point curve), which is what the
   // throttling below was built around; it is now the closed-form transfer
   // function (~microseconds per curve), so the throttle only saves a little
   // draw-list churn. Recomputed only
   // when the signature (everything it depends on) actually changed (per
   // audio-node-ui-system.md §3f's "computed main-thread ... never by
   // calling into the live AudioNode" - this recomputes from a *scratch*
   // Biquad/TptSvf, same as the kernel's own PushParams, not the running
   // one) - AND, while a drag is actively changing the signature every
   // frame, throttled to at most once per kFilterCurveThrottleSec so a drag
   // doesn't force a full recompute at UI frame rate. The heuristic for "a
   // drag is happening" is deliberately the same one the task that added
   // this used: the signature changed on this exact frame AND the mouse
   // button is currently held (a typed edit or a single modulation step
   // changes the signature without the button held, and gets an immediate,
   // un-throttled, full-resolution recompute - so the *final* curve after
   // any change, drag or not, is always full 160-point resolution).
   //
   // Continuous motion that is NOT a drag - an LFO, expression, macro or
   // Drift moving freq/Q/gain every frame - used to fall through to the
   // un-throttled full recompute on every frame, for every modulated filter
   // on the canvas (~31% of the main thread with 72 modulated filters). A
   // signature that changes on two frames in a row is now "in motion": the
   // first change of a streak still recomputes at once (so a typed edit or a
   // single stepped-modulator jump is exact immediately), later ones are
   // capped per node to kFilterCurveMotionSec and staggered by node index
   // (FilterCurvePhase) so the filters on a canvas don't all land on one
   // frame. The first frame the signature holds still, the curve is
   // recomputed at full resolution - including after a drag or motion
   // streak whose last recompute was a coarse one (FilterCurveCache::coarse).
   struct FilterCurveCache
   {
      std::vector<float> curveDb; // one entry per x pixel column sampled
      std::vector<float> signature;    // signature that produced curveDb
      std::vector<float> lastSeenSignature; // signature observed last frame
      double lastRecomputeTime = -1.0;
      double nextDue = -1.0;          // earliest time a recompute during a motion streak may run
      bool changedLastFrame = false;  // signature also changed on the previous drawn frame
      bool coarse = false;            // curveDb holds a reduced-resolution recompute
      float lastOriginX = 0.0f;       // x origin / width curveDb was sampled at
      float lastWidth = 0.0f;
      bool dragIsQ = false; // which handle the current drag (if any) is grabbing
   };

extern std::map<int, FilterCurveCache> gFilterCurveCache;

extern int gFilterCurveRecomputes;

 // self-test visibility only
   inline const double kFilterCurveMotionSec = 0.05;


   inline const int kFilterCurveFullPoints = 160;

float FilterVizFreqToX(float hz, float x0, float w);

float FilterVizDbToY(float db, float y0, float h);



   // Live incoming-signal spectrum drawn behind the response curve (the
   // thing FabFilter Pro-Q shows) - shared by Audio Filter and EQ so both
   // visualizers show the same live analyzer, not two separate ones. Taps
   // the post-mix mono ring AudioEffectRuntime writes every audio block
   // (AudioEffectNode.cpp's mSpectrumRing) and runs it through the same
   // Hann-window + Radix2FFT recipe AudioColorRampNode::ProcessAudioFFT
   // already uses (AudioColorRampNode.cpp) - just plotted against this
   // visualizer's own log-frequency axis instead of feeding a color ramp.
   struct AudioSpectrumState
   {
      std::vector<float> window = std::vector<float>(1024, 0.0f);
      std::vector<float> smoothed = std::vector<float>(512, 0.0f);
   };

extern std::map<int, AudioSpectrumState> gAudioSpectrumCache;

void ComputeFilterCurve(std::vector<float>& out, int numPoints, int type, float freq, float q, float gain,
                           double sampleRate, float originX, float w);

void AddRateModeCells(AudioKnobRow& row, AudioEffectNode* n, const char* syncLabel,
                         float rateLo = 0.02f, float rateHi = 5.0f);

void DrawAudioFilterBody(GraphNode& gn, AudioEffectNode* n);



   // ---- EQ -------------------------------------------------------------
   // Five fixed bands, always present - docs/plans/audio/eq-node-prompt.md.
   // Reuses Audio Filter's log-frequency graticule/mapping helpers verbatim
   // (FilterVizFreqToX/XToFreq/DbToY/YToDb, kFilterViz*) rather than defining
   // a second copy - both nodes share the same 20 Hz-20 kHz / +-24 dB frame.
   struct EqCurveCache
   {
      std::vector<float> curveDb;       // composite, one entry per x column
      std::vector<float> bandCurveDb[5]; // per-band, only enabled ones drawn
      std::vector<float> signature;
      int dragBand = -1;
      bool dragInert = false; // this gesture is a select-only click or a double-click toggle
   };

extern std::map<int, EqCurveCache> gEqCurveCache;



   inline const char* const kEqTypeParam[5] = { "band1Type", "band2Type", "band3Type", "band4Type", "band5Type" };


   inline const char* const kEqFreqParam[5] = { "band1Freq", "band2Freq", "band3Freq", "band4Freq", "band5Freq" };


   inline const char* const kEqQParam[5] = { "band1Q", "band2Q", "band3Q", "band4Q", "band5Q" };


   inline const char* const kEqGainParam[5] = { "band1Gain", "band2Gain", "band3Gain", "band4Gain", "band5Gain" };


   inline const char* const kEqOnParam[5] = { "band1On", "band2On", "band3On", "band4On", "band5On" };

void DrawEqBody(GraphNode& gn, AudioEffectNode* n);

void DrawDynamicsBody(GraphNode& gn, AudioEffectNode* n);

void DrawLimiterBody(GraphNode& gn, AudioEffectNode* n);

void DrawDelayBody(GraphNode& gn, AudioEffectNode* n);

void DrawReverbBody(GraphNode& gn, AudioEffectNode* n);

void DrawDriveBody(GraphNode& gn, AudioEffectNode* n);

void DrawWavetableShaperBody(GraphNode& gn, AudioEffectNode* n);

void DrawStereoBody(GraphNode& gn, AudioEffectNode* n);

void DrawPitchShiftBody(GraphNode& gn, AudioEffectNode* n);

void AddSyncedRateCell(AudioKnobRow& row, AudioEffectNode* n, float rateLo = 0.02f, float rateHi = 5.0f);

void AddRateModeCells(AudioKnobRow& row, AudioEffectNode* n, const char* syncLabel,
                         float rateLo, float rateHi);

void DrawChorusBody(GraphNode& gn, AudioEffectNode* n);

void DrawFlangerBody(GraphNode& gn, AudioEffectNode* n);

void DrawPhaserBody(GraphNode& gn, AudioEffectNode* n);

void DrawBitcrushBody(GraphNode& gn, AudioEffectNode* n);

void DrawTransientShaperBody(GraphNode& gn, AudioEffectNode* n);

void DrawStutterBody(GraphNode& gn, AudioEffectNode* n);

void DrawRingModBody(GraphNode& gn, AudioEffectNode* n);

void DrawFrequencyShifterBody(GraphNode& gn, AudioEffectNode* n);

void DrawTremoloBody(GraphNode& gn, AudioEffectNode* n);

void DrawFormantFilterBody(GraphNode& gn, AudioEffectNode* n);

void DrawResonatorBankBody(GraphNode& gn, AudioEffectNode* n);

void DrawCycleShaperBody(GraphNode& gn, AudioEffectNode* n);



   // ---- Spec Blur ------------------------------------------------------
   struct SpecBlurSpectrumState
   {
      std::vector<float> window;
      float smoothed[512] = {};
      SpecBlurSpectrumState() { window.assign(1024, 0.0f); }
   };

extern std::unordered_map<AudioEffectNode*, SpecBlurSpectrumState> sSpecBlurSpectrums;

void DrawSpecBlurBody(GraphNode& gn, AudioEffectNode* n);



   // ---- Key-Snap -------------------------------------------------------
   struct KeySnapSpectrumState
   {
      std::vector<float> window;
      float smoothed[512] = {};
      KeySnapSpectrumState() { window.assign(1024, 0.0f); }
   };

extern std::unordered_map<AudioEffectNode*, KeySnapSpectrumState> sKeySnapSpectrums;

void DrawKeySnapBody(GraphNode& gn, AudioEffectNode* n);

extern std::unordered_map<AudioEffectNode*, KeySnapSpectrumState> sSpectrumSlideSpectrums;

void DrawSpectrumSlideBody(GraphNode& gn, AudioEffectNode* n);

void DrawShapeResonatorBody(GraphNode& gn, AudioEffectNode* n);

extern std::string gPatchPath;

extern bool gPatchDirty;

void DrawAudioNodeBody(GraphNode& gn);

void DrawAudioFileParams(AudioFileNode* n);

void DrawAudioAnalyzeParams(AudioAnalyzeNode* n);

void DrawPathParams(PathNode* n);

void DrawGeometryTableParams(GeometryTableNode* n);

void DrawOceanParams(OceanNode* n);

void DrawCurveParams(CurveNode* n);

void DrawMetaBallParams(MetaBallNode* n);

void DrawJoinGeometryParams(JoinGeometryNode* n);

void DrawSwitcher3DParams(Switcher3DNode* n);

void DrawWrapParams(WrapNode* n);

void DrawClothParams(ClothNode* n);

void DrawParticleSystemParams(ParticleSystemNode* n);

void DrawMaterialParams(MaterialNode* n);

void DrawMappingParams(MappingNode* n);

void DrawMeshResynthParams(MeshResynthNode* n);

void DrawImageToPointsParams(ImageToPointsNode* n);

void DrawDepthProjectionParams(DepthProjectionNode* n);

void DrawMeshToPointsParams(MeshToPointsNode* n);

void DrawDistributePointsOnFacesParams(DistributePointsOnFacesNode* n);

void DrawPointsToVerticesParams(PointsToVerticesNode* n);

void DrawDistributePointsInGridParams(DistributePointsInGridNode* n);

void DrawMergeByDistanceParams(MergeByDistanceNode* n);

void DrawText3DParams(Text3DNode* n);

void DrawModelParams(ModelSourceNode* n);

void DrawGeometryParams(GeometryNode* n);

void DrawGeometryOpParams(GeometryOpNode* n);

void DrawDisplacementParams(DisplacementNode* n);

void DrawAudioDisplacementParams(AudioDisplacementNode* n);

void DrawAudioTextureParams(AudioTextureNode* n);

void DrawAudioColorRampParams(AudioColorRampNode* n);

void DrawAudioRibbonParams(AudioRibbonNode* n);

void DrawSetColorParams(SetColorNode* n);

void DrawInstanceParams(InstanceOnPointsNode* n);

void DrawCameraParams(CameraNode* n);

void DrawLightParams(LightNode* n);

void FrameSceneInView(Render3DNode* n);

void DrawRender3DParams(Render3DNode* n);

void DrawBlendParams(BlendNode* n);

void DrawLayerStackParams(LayerStackNode* n);

void DrawFilterParams(FilterNode* n);

void ExportImage(INode* out, const std::string& path, int jpgQuality = 90,
                    std::vector<unsigned char>* keepPixels = nullptr, int outputIndex = 0);

bool ReadNodeImageRgba8(INode* node, int outputIndex, bool opaque, int& w, int& h, std::vector<unsigned char>& rgba);

bool WriteSrgbPng(const std::string& path, int w, int h, const unsigned char* rgba);

bool NodeImageIsFloat(INode* node, int outputIndex);

bool ReadNodeImageRgba16(INode* node, int outputIndex, bool opaque, int& w, int& h, std::vector<uint16_t>& rgba);

std::vector<unsigned char> ResizeRgba8(const unsigned char* src, int w, int h, int nw, int nh);

std::vector<uint8_t> EncodePng16(int w, int h, const uint16_t* rgba, int level);



   // --frames-dir encoder: readback stays on the GL thread, zlib runs on a few workers behind a
   // byte-budgeted queue so a slow disk or a big frame back-pressures instead of eating memory.
   class PngSequenceWriter
   {
   public:
      explicit PngSequenceWriter(int level, size_t budgetBytes = 256u << 20) : mBudget(budgetBytes)
      {
         stbi_write_png_compression_level = level; // set once, before any worker reads it
         const unsigned n = std::max(1u, std::min(4u, std::thread::hardware_concurrency() / 2));
         for (unsigned i = 0; i < n; i++)
            mThreads.emplace_back([this] { Run(); });
      }
      ~PngSequenceWriter() { Finish(); }
      void Submit(std::string path, int w, int h, std::vector<unsigned char> rgba8, std::vector<uint16_t> rgba16, int level)
      {
         const size_t bytes = rgba8.size() + rgba16.size() * 2;
         std::unique_lock<std::mutex> lock(mMutex);
         mSpace.wait(lock, [&] { return mQueued == 0 || mQueued + bytes <= mBudget; });
         mQueued += bytes;
         mJobs.push_back({ std::move(path), w, h, std::move(rgba8), std::move(rgba16), level, bytes });
         mWork.notify_one();
      }
      // Waits for every queued frame; returns the paths that did not reach the disk.
      std::vector<std::string> Finish()
      {
         {
            std::lock_guard<std::mutex> lock(mMutex);
            mStop = true;
         }
         mWork.notify_all();
         for (std::thread& t : mThreads)
            if (t.joinable())
               t.join();
         mThreads.clear();
         return mFailed;
      }

   private:
      struct Job
      {
         std::string path;
         int w, h;
         std::vector<unsigned char> rgba8;
         std::vector<uint16_t> rgba16;
         int level;
         size_t bytes;
      };
      void Run()
      {
         for (;;)
         {
            Job job;
            {
               std::unique_lock<std::mutex> lock(mMutex);
               mWork.wait(lock, [&] { return mStop || !mJobs.empty(); });
               if (mJobs.empty())
                  return;
               job = std::move(mJobs.front());
               mJobs.pop_front();
            }
            bool ok = false;
            std::vector<uint8_t> png;
            if (!job.rgba16.empty())
               png = EncodePng16(job.w, job.h, job.rgba16.data(), job.level);
            else
            {
               stbi_write_png_to_func(
                  [](void* ctx, void* data, int size)
                  {
                     auto* bytes = static_cast<std::vector<uint8_t>*>(ctx);
                     bytes->insert(bytes->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
                  },
                  &png, job.w, job.h, 4, job.rgba8.data(), job.w * 4);
               if (!png.empty())
                  png = ContactSheet::TagSrgb(png);
            }
            if (!png.empty())
               if (FILE* f = std::fopen(job.path.c_str(), "wb"))
               {
                  ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
                  std::fclose(f);
               }
            std::lock_guard<std::mutex> lock(mMutex);
            if (!ok)
               mFailed.push_back(job.path);
            mQueued -= job.bytes;
            mSpace.notify_all();
         }
      }
      std::mutex mMutex;
      std::condition_variable mWork, mSpace;
      std::deque<Job> mJobs;
      std::vector<std::thread> mThreads;
      std::vector<std::string> mFailed;
      size_t mBudget = 0, mQueued = 0;
      bool mStop = false;
   };

void ExportPng(OutputNode* out, const std::string& path);

void DrawPaintablePreview(DrawNode* node);

float CommentFontScale(int sizeIdx);

void DrawCommentPreview(CommentNode* n);

void DrawCommentParams(CommentNode*);

GroupNode* GroupOwning(int nodeIndex);

int IndexOfGroupNode(GroupNode* g);

ImVec2 ClusterOffset(const std::set<int>& indices);

void PruneDeadGroups();

void DrawGroupNode(GraphNode& gn, GroupNode* n);

void DrawDrawParams(DrawNode* n);

INode* DisplayNode(INode* node);

const char* EmptyPreviewLabel(INode* node, const char* fallback);

void DrawPreview(INode* node);

void DrawFieldGraphWaveform(FieldGraphNode* fgn, INode* audioTerminal);

extern std::map<int, std::vector<float>> gModHistory;

extern std::map<int, NodeViewport> gNodeViewports;

extern std::map<int, SharedViewportCamera> gNodeCameras;



   // Nodes/viewports retired by RemoveNodeByIndex/NewPatch, held past the GL
   // texture hazard (their own draw calls can't happen synchronously - see
   // RemoveNodeByIndex) AND past the audio hazard: a retired node's AudioNode
   // may still be reachable through AudioEngine's currently-published topology
   // for a little while after it leaves gNodes (RebuildAudioTopology hasn't
   // republished yet, or the audio thread hasn't finished a block against the
   // last topology that included it). safeAfterGeneration records
   // AudioEngine::CurrentGeneration() at the moment of retirement - "the last
   // topology generation that can still reach this node" - and the node is
   // only actually destroyed once AudioEngine::CompletedGeneration() confirms
   // the audio thread has moved past it. A plain one-video-frame delay isn't
   // enough here: nothing ties a video frame's length to the audio thread's
   // own progress (see the crash this replaced - Cmd+Z destroying a node
   // whose AudioNode the audio thread was still mid-ProcessBlock on).
   // NodeViewport has a user-declared destructor (which suppresses its
   // implicit move ctor), so it can't live in a vector<NodeViewport> without a
   // copy that would double-free its GL texture; map::node_type from
   // extract() moves the whole tree node instead of the mapped value,
   // sidestepping that.
   struct RetiredNode
   {
      std::unique_ptr<INode> node;
      uint64_t safeAfterGeneration = 0;
   };

extern std::vector<RetiredNode> gRetiredNodes;

extern std::vector<std::map<int, NodeViewport>::node_type> gRetiredViewports;

void DrawMiniViewport(GraphNode& gn, IGeometrySource* geo);

extern std::map<int, NodeViewport> gPanelViewports;

extern std::map<int, NodeViewport> gProjectorViewports;

bool HasUsefulMiniViewport(INode* n);

bool CanShowInViewportPanel(const GraphNode& gn);

void ViewportPanelDockCombo();

void DrawViewportPanelDocked(const char* id, const ImVec2& size);

void ModMatrixDockCombo();

float ApplyModulationCurve(float v, float curve);



   struct SparklineHistory
   {
      static constexpr int kCap = 32;
      float samples[kCap] = {};
      int head = 0;
      int count = 0;
      void Push(float val)
      {
         samples[head] = val;
         head = (head + 1) % kCap;
         if (count < kCap) count++;
      }
   };

extern std::map<std::pair<int, int>, SparklineHistory> gModMatrixSparklines;

void DrawModMatrixDocked(const char* id, const ImVec2& size);

bool IsNodeVideoCompatible(const GraphNode& gn);

bool IsNodeAudioCompatible(const GraphNode& gn);



   // ---- arrangement video compositing (overhaul WP4) ---------------------
   //
   // One pass per active video lane, bottom lane first, so the lane drawn at
   // the TOP of the panel lands in front - the NLE convention (spec §1). Each
   // caller owns an ArrangeCompositeTarget: the live monitor has one, the
   // offline render another. They used to share a single static scratch FBO,
   // which the two resized against each other every frame a render ran with
   // the panel open.

   // A render target's private GL state. `scratch` is the ping-pong pair the
   // lane passes alternate between; `result` is the stable output for a
   // caller that has no FBO of its own to land in (the monitor). `slot` keys
   // this target's own geometry viewports (gArrangeGeomViewports), so two
   // targets at different sizes never share - and thrash - one NodeViewport.
   struct ArrangeCompositeTarget
   {
      int slot = 0;
      GLUtil::Fbo scratch[2];
      GLUtil::Fbo result;
      // `result` from before its last resize. The monitor's texture id goes
      // into the ImGui draw list during the UI pass, and the composite runs
      // after the cook loop but before ImGui::Render - so a resize there
      // would leave the draw list sampling a deleted texture for one frame.
      // Kept one composite longer, then freed.
      GLUtil::Fbo retiredResult;
      // Set by the monitor during the UI pass; consumed by the post-cook
      // composite. 0 = the panel did not draw the monitor this frame.
      int requestW = 0;
      int requestH = 0;
   };

extern ArrangeCompositeTarget gArrangeMonitorTarget;

extern ArrangeCompositeTarget gArrangeRenderTarget;



   // Geometry clips' solo renders. A geometry node has no image of its own
   // (GeometryNode::GetOutputTexture), so a clip of one needs a NodeViewport
   // sized to the composite. These used to borrow gPanelViewports, which the
   // Viewport Panel erases every frame for any node it is not showing - so a
   // geometry clip allocated and freed a full-size FBO every frame (4K per
   // frame during a render). Keyed by (node uid, target slot); an entry
   // unused for kArrangeGeomEvictFrames main-loop frames is dropped by
   // ReapArrangeGeomViewports(). std::map, not unordered: NodeViewport is
   // neither copyable nor movable, and a node-based map never relocates it.
   struct ArrangeGeomViewport
   {
      NodeViewport viewport;
      uint64_t lastUsedFrame = 0;
   };

extern std::map<std::pair<uint64_t, int>, ArrangeGeomViewport> gArrangeGeomViewports;

extern uint64_t gArrangeGeomFrame;


   inline constexpr uint64_t kArrangeGeomEvictFrames = 120;

void ReapArrangeGeomViewports();



   // ---- Live clip waveforms (WP8) --------------------------------------
   // One position-indexed peak cache per audio clip, filled by the audio
   // thread as the clip plays (AudioEngine::ClipPeaks) and drained here.
   // Position-indexed, not streamed: bucket k always means the same slice of
   // the clip, so playing the same bar twice overwrites rather than appends,
   // and scrubbing backwards fills in what was skipped.
   //
   // Never saved and never pre-decoded: a clip's source is a live node, not a
   // file, so there is nothing to read ahead of the playhead. A clip that has
   // not been played yet draws a flat centre line, which is the honest
   // picture of "nothing has come out of this node here yet".
   struct ArrangeClipWave
   {
      // The clip shape this cache was sized and measured for. Any change to
      // these four means the buckets no longer describe what the clip holds,
      // so the cache is cleared - they are exactly the fields WP8 names
      // (src, output, start, length). `start` is in the list because a clip's
      // source is a live node rather than a file: moved two beats later, the
      // clip plays whatever the node emits two beats later, not the material
      // that was measured. Gains and fades are deliberately NOT here - the
      // audio thread measures pre-envelope, pre-gain, so they change how the
      // clip sounds without changing what the buckets describe.
      uint64_t srcUid = 0;
      int      srcOutput = 0;
      Arrange::Tick start = 0;
      Arrange::Tick length = 0;
      // The same four fields hashed, as ArrangeClipShape spells it. Carried
      // into every ClipWindow so a bucket measured under the previous shape
      // and still in flight when an edit lands is dropped on arrival rather
      // than written into the reshaped array at a coincidentally valid index.
      uint64_t shape = 0;
      // Audio Sample static waves only: the remaining inputs the slice
      // depends on, so ArrangeSyncClipVisuals can tell a stale entry from a
      // current one after ANY edit (trim, split, paste, undo, BPM or sync
      // change, a project-tempo change on an unsynced clip).
      double   sampleEffBpm = 0.0;
      float    sampleOffset = 0.0f;
      const Platform::SampleBuffer* sampleBuf = nullptr;
      std::vector<float>   minv;
      std::vector<float>   maxv;
      std::vector<uint8_t> filled;
   };

extern std::unordered_map<uint64_t, ArrangeClipWave> gArrangeClipWaves;

extern std::unordered_map<uint64_t, ArrangeClipWave> gArrangeSampleStaticWaves;


   // Ticks per bucket, the main-thread spelling of kClipPeakBucketsPerBeat.
   inline constexpr Arrange::Tick kArrangeWaveBucketTicks = Arrange::kPPQ / 16;


   // A clip longer than this many buckets (~1 hour at 120 bpm) stops being
   // cached rather than allocating without bound. Nothing in the model caps
   // clip length, so this is a guard, not a policy.
   inline constexpr int kArrangeWaveMaxBuckets = 128 * 1024;

uint64_t ArrangeClipShape(uint64_t srcUid, int srcOutput, Arrange::Tick start, Arrange::Tick length);

int ArrangeWaveBucketCount(Arrange::Tick length);

void ArrangeComputeSampleStaticWave(uint64_t clipId, uint64_t srcUid, int srcOutput,
                                        Arrange::Tick start, Arrange::Tick length,
                                        double effBpm, float sourceOffsetSeconds,
                                        const Platform::SampleBuffer* buf);

double ArrangeSampleEffBpm(const Arrange::Clip& c);

void ArrangeSetSampleSync(uint64_t clipId, bool sync);

void ArrangeSetSampleBpm(uint64_t clipId, float bpm);

void ArrangeDrawSampleTempoInfo(const Arrange::Clip& c);



   // ---- Clip thumbnails (WP8) ------------------------------------------
   // One 96x54 FBO per *video* clip id, blitted from whatever texture the
   // composite already resolved for that clip - so a thumbnail costs one
   // small aspect-fit pass and never a second decode or a second render of
   // the source. Refreshed at most once a second while the clip is active,
   // and immediately after a reassign (lastCapture reset below).
   struct ArrangeClipThumb
   {
      GLUtil::Fbo fbo;
      double   lastCapture = -1.0; // glfwGetTime(); < 0 means "never captured"
      uint64_t srcUid = 0;
      int      srcOutput = 0;
   };

extern std::map<uint64_t, ArrangeClipThumb> gArrangeClipThumbs;


   inline constexpr int kArrangeThumbW = 96;


   inline constexpr int kArrangeThumbH = 54;

void ArrangeSyncClipVisuals();

int CountActiveArrangeVideoClips(double beat, std::string* outFrontTitle = nullptr);

void ArrangeSeekVideoSampleSources(double beat);

unsigned int CompositeArrangeTimelineVideo(ArrangeCompositeTarget& target, GLUtil::Fbo* dest,
                                              double beat, int targetW, int targetH);

void CompositeArrangeMonitorIfRequested();

std::vector<int> ArrangeOutputsOfType(const GraphNode& gn, int laneType);

int ArrangeLaneTypeForNode(const GraphNode& gn);

void ArrangePruneSelection();

std::vector<uint64_t> ArrangeSelectionIds();

void ArrangeClickSelect(uint64_t clipId, bool toggle, bool singleMember);

bool ArrangeCopySelection();

bool ArrangePasteAt(Arrange::Tick atTick);

bool ArrangeDuplicateSelection();

bool ArrangeDeleteSelection();

bool ArrangeSplitSelectionAt(Arrange::Tick tick);

bool ArrangeBladeSplitAt(uint64_t clipId, Arrange::Tick tick);

bool ArrangeToggleEnabledSelection();

bool ArrangeCanGroupSelection();

bool ArrangeCanUngroupSelection();

bool ArrangeGroupSelection();

bool ArrangeUngroupSelection();

bool ArrangeGroupRowSelection();

bool ArrangeUngroupRowSelection();

bool ArrangeDuplicateRowSelection();

bool ArrangeDeleteRowSelection();

bool ArrangeAddTrackShortcut(bool isVideo);

bool ArrangeNudge(int dir);

bool ArrangeRenameSelection();

void ArrangeDragBegin(int mode, uint64_t clipId, int edge, Arrange::Tick grabTick);

bool ArrangeDragUpdate(Arrange::Tick value, int laneDelta);

bool ArrangeDragEnd();

uint64_t AddNodeToArrangeTimeline(int nodeIndex, int laneType = -1, int srcOutput = -1);

bool ArrangeAssignClipSource(uint64_t clipId, uint64_t uid);

ImU32 ArrangeGroupColor(uint64_t groupId, int alpha = 255);

void DrawArrangeHatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float spacing = 7.0f);

std::string ArrangeFormatBBT(Arrange::Tick t);

std::string ArrangeFormatBBTLength(Arrange::Tick t);

std::string ArrangeFormatSeconds(double sec, bool centis = true);

std::string ArrangeFormatTickSeconds(Arrange::Tick t);

std::string ArrangeFormatPos(Arrange::Tick t);

std::string ArrangeFormatLength(Arrange::Tick t);

Arrange::Tick ArrangeParsePos(const char* buf);

Arrange::Tick ArrangeParseLen(const char* buf);



   // ---- inspector typed-value editing --------------------------------------
   // The canvas param widgets have had hover-and-type for a long time
   // (HandleParamTypeHotkeys / gTypedParam*): hover a slider, press a digit,
   // and you are typing, with that digit already in the box. The Arrange
   // inspector's fields did not behave the same way, and the difference was
   // the complaint - ImGui's own TempInput path opens the box on the OLD
   // text, and whether the keystroke that opened it replaces that text or
   // lands beside it depends on frame ordering, which is why typing "0.3"
   // over "0.80" needed a backspace first.
   //
   // So: same approach as the canvas. Seed the box with exactly the typed
   // character and put the cursor after it. One edit can be open at a time,
   // which is true of an inspector by construction.
   struct ArrangeTypedEditState
   {
      ImGuiID id = 0;
      std::string text;
      bool justOpened = false;
      bool pendingInit = false;
      bool noAutoSelect = false;
   };

extern ArrangeTypedEditState gArrangeTypedEdit;

extern int gArrangeFieldHotFrame;

void ArrangeMarkFieldHot();

bool ArrangeFieldHot();



   // Arrangement slider with smooth dragging, double-click to edit text,
   // hover-to-type (starts editing immediately upon typing any number/sign/dot),
   // and standard Ctrl+Click.
   //
   // A plain click's *first* frame can't tell whether a second click is about
   // to follow (ImGui only reports IsMouseDoubleClicked() on the second
   // click), so a single click can't be allowed to immediately SetActiveID +
   // snap the value the way vanilla SliderBehavior does - that snap would
   // commit a spurious edit a frame before the double-click is recognized and
   // opens the text box. Instead a fresh click is held as "pending" until
   // either the mouse drags past the threshold (genuine drag - activate now,
   // from the current position), the double-click window elapses (genuine
   // single click - activate now), or a second click arrives first (genuine
   // double-click - go straight to text input, no drag ever activated).
   struct ArrangeSliderPendingClick
   {
      bool waiting = false;
      double downTime = 0.0;
      ImVec2 downPos = ImVec2(0, 0);
   };

extern std::unordered_map<ImGuiID, ArrangeSliderPendingClick> sArrangeSliderPending;

bool ArrangeSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f", ImGuiSliderFlags flags = 0);




   // A clip tick value, in whichever unit that value is actually spoken in.
   //
   //   Position / Length  follow Settings::timeDisplay - bar.beat.sixteenth
   //                      ("9.3.3" for a position, "1.0.0" for a length) in
   //                      Bars, seconds in Time.
   //   FadeMs             is always milliseconds, whatever the display mode.
   //                      A fade is an envelope, not a place in the song: it
   //                      is chosen by ear in the tens of milliseconds, and
   //                      "0.0.0" gave no way to say 20 of them.
   //
   // Dragging, double-clicking and hover-and-type all work in that same unit,
   // and the typed text is parsed in it - so "9.3.3" in the Start box means
   // bar 9, beat 3, sixteenth 3, and "20" in a Fade box means 20 ms.
   enum class ArrangeTickUnit { Position, Length, FadeMs };

bool ArrangeTickField(const char* label, Arrange::Tick cur, Arrange::Tick lo, Arrange::Tick hi,
                         ArrangeTickUnit unit, float width, Arrange::Tick* out);

bool ArrangeDragFloat(const char* label, float* v, float speed, float v_min, float v_max,
                         const char* format, float width);

Arrange::Tick ArrangeRenderableEndTick();

void ArrangeRenderDetectClipSize(int& outW, int& outH);

int ArrangeRenderVideoClipsInRange(Arrange::Tick a, Arrange::Tick b);

int ArrangeRenderEffectiveAudioSource();

int ArrangeRenderEffectiveVideoSource(Arrange::Tick a, Arrange::Tick b);

bool ArrangeRenderPathQueued(const std::string& path, uint64_t exceptJobId);

void ArrangeRenderResolveRange(int kind, int markerA, int markerB, Arrange::Tick customStart,
                                  Arrange::Tick customEnd, Arrange::Tick& outA, Arrange::Tick& outB);

int ArrangeRenderFrameBudget(double durSec, int fps);

long long ArrangeRenderSampleBudget(double durSec, double rate);

double ArrangeRenderActiveSampleRate();



   // The block size an offline take should pump in. Node behaviour is
   // block-granular - MixerNode latches pan/mute/solo once per block before
   // its per-sample loop, for one - so a take pumped in kAudioMaxBlockFrames
   // slabs does not sound like what the user heard through their configured
   // buffer size. Follow the device's real period where the platform reports
   // one, the Settings -> Audio value otherwise, and never exceed the
   // capacity every node was prepared with.
   //
   // A headless job is the exception: it must sound the same on every machine
   // and in CI, so it pumps a fixed block and ignores both the device period
   // and the Settings value (R478).
   inline constexpr int kHeadlessAudioBlockFrames = 512;

int OfflineAudioBlockFrames();

bool ArrangeMediaKindForPath(const std::string& path, Arrange::ImportMediaKind& outKind);

void ArrangeImportMediaFile(const std::string& path, uint64_t laneId, Arrange::Tick atTick,
                               Arrange::ImportMediaKind kind);

void ArrangeRespawnCloneNode(uint64_t clipId);

void ArrangeSharedSourceTooltip(const char* body);

bool ArrangeMakeClipSourceUnique(uint64_t clipId);

void ArrangePollMediaImports();

std::string ArrangeRenderUniquePath(const std::string& path);

ArrangeRenderJob ArrangeBuildLaneScopedRenderJob(const std::vector<uint64_t>& laneIds, const std::string& baseName);

void ArrangeCommitLaneScopedRenderJob(ArrangeRenderJob job);

void DrawArrangePanelDocked(const char* id, const ImVec2& size);

void PerfPanelDockCombo();

ImVec2 GetPerfElementCellSpan(int kind);

void ReorderPerfPages(int src, int dst);

void AddToPerformanceMatrix(int nodeIndex, int paramIndex, int kind = 0, const std::string& customLabel = "",
                                int paramIndex2 = -1, const std::string& boolName = "");

void UpdatePerformanceMatrixMIDI();

bool ParamMidiLearnActive();

int MidiLearnActiveCount();

void MidiLearnCancelAll();

bool ParamMidiLearnIsActiveFor(int nodeIndex, int paramIndex);

void StartParamMidiLearn(int nodeIndex, int paramIndex);

bool ParamMidiLearnable(int nodeIndex, int paramIndex);

bool ParamMidiLearnCommit(int nodeIndex, int paramIndex, const Platform::MidiCCValue& last);

void UpdateParamMidiLearn();

void DrawParamMidiLearnBanner();

void DrawParamMidiLearnMenuItem(int nodeIndex, int paramIndex);

void DrawPerfPanelDocked(const char* id, const ImVec2& size);

void DrawModulatorMeter(IModulator* mod, int nodeIndex);

void DisconnectLinkById(int id);

const char* NodeHelpText(const GraphNode& gn);
const char* NodeHelpTextFor(const std::string& typeName, const std::string& category);
// typeName + category, plus the help prose (English and translated) when the UI is not English.
std::string NodeSearchHaystack(const std::string& typeName, const std::string& category);
// Translated help text; "{mod}" in the key expands to Cmd / Ctrl.
const char* HelpT(const char* key);

void DrawSettingsWindow(bool* open);

void DrawShortcutsWindow(bool* open);

void DrawHelpWindow(bool* open);

void DisconnectAllTo(INode* dying);

AudioNode* AudioNodeOfAny(INode* node);

int AudioBufferIndexOf(INode* node, int pinOutputSlot, const std::unordered_map<AudioNode*, int>& bufferIndexOf);

INode* ResolvedAudioSource(INode* source, bool* didHop = nullptr);

extern bool gDeferAudioRebuild;



   // Timing for the undo/delete performance work in
   // docs/plans/undo-delete-perf-prompt.md - off unless INFINITE_PERFTIMING
   // is set, matching every other getenv("INFINITE_...") harness in this
   // file. Declared here (this function's own definition is the earliest of
   // the three it instruments) so it's visible everywhere it's used below.
   // Checked once per construction rather than cached, so toggling the env
   // var takes effect on the next call without a restart.
   struct ScopedPerfTimer
   {
      const char* label;
      std::chrono::steady_clock::time_point start;
      bool enabled;
      explicit ScopedPerfTimer(const char* l)
         : label(l), enabled(getenv("INFINITE_PERFTIMING") != nullptr)
      {
         if (enabled)
            start = std::chrono::steady_clock::now();
      }
      ~ScopedPerfTimer()
      {
         if (enabled)
         {
            double ms = std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start).count();
            fprintf(stderr, "[perf] %s: %.3f ms\n", label, ms);
         }
      }
   };



   // tailStage: also add this stage's time to the current frame's slow-frame
   // record (Bench::Tail(), B3/B8 only) even when `sink` isn't sampling.
   struct ConditionalStageTimer
   {
      Bench::PercentileRing* mSink;
      int mTailStage;
      double mStart;
      bool mStopped = false;
      explicit ConditionalStageTimer(Bench::PercentileRing* sink, int tailStage = -1)
         : mSink(sink), mTailStage(Bench::Tail().active ? tailStage : -1),
           mStart((sink || mTailStage >= 0) ? Bench::ScopedStageTimer::NowMs() : 0.0)
      {
      }
      void Stop()
      {
         if ((mSink || mTailStage >= 0) && !mStopped)
         {
            const double ms = Bench::ScopedStageTimer::NowMs() - mStart;
            if (mSink)
               mSink->Push(ms);
            if (mTailStage >= 0)
               Bench::Tail().AddStage(mTailStage, ms);
            mStopped = true;
         }
      }
      ~ConditionalStageTimer()
      {
         Stop();
      }
   };



   // ---- timeline terminal PDC (overhaul WP3) -----------------------------
   //
   // A timeline clip terminal has no AudioCaptureRing to hang its delay-
   // compensation state on, and a value living in the disposable terminal
   // vector is rebuilt from zero every generation - which clicked on every
   // rebuild, and rebuilds used to happen at every clip boundary. So the
   // state lives here instead, keyed by the same (laneId, srcUid, srcOutput)
   // that identifies the terminal, and survives any number of rebuilds.
   //
   // unique_ptr, not a value: the audio thread holds a raw pointer to the
   // CompensationDelay for the life of a generation, so it must not move when
   // the map rehashes. Entries are dropped only once CompletedGeneration()
   // confirms the audio thread has finished with the last topology that
   // referenced them - the same rule gRetiredNodes uses.
   struct ArrangeTerminalComp
   {
      std::unique_ptr<CompensationDelay> delay;
      uint64_t lastUsedGeneration = 0;
      bool usedThisRebuild = false;
   };

extern std::unordered_map<uint64_t, ArrangeTerminalComp> gArrangeTerminalComp;

extern uint64_t gArrangeAudioBuiltRevision;

extern bool gArrangeAudioBuiltRouting;

extern std::set<uint64_t> gArrangeRetriggerConflictClipIds;

extern unsigned long long gAudioTopologyRebuildCount;

extern std::set<uint64_t> gArrangeVideoSourceConflictClipIds;

extern uint64_t gArrangeVideoConflictBuiltRevision;

const std::set<uint64_t>& ArrangeVideoSourceConflictClips();

void ArrangeCollectClipModBindings(const GraphNode& node,
                                      std::vector<std::pair<int, std::string>>& out);

bool ArrangeTimelineRoutingActive();

void ForceAudioRepare();

void RebuildAudioTopology();

bool ArrangeAudioRebuildIfStale();

bool StartAudioEngine(std::string& outError);

void PollAudioRecovery();

INode* FindHardwareDrivenNode();

INode* FindHardwareDrivenNodeInArrangeRange(int64_t startTick, int64_t endTick,
                                               bool wantVideo, bool wantAudio);

void StartOfflineRenderSession(OutputNode* n, int width, int height, bool isArrange);

void DrawOfflineRenderProgressWindow();

void DrawArrangeWavRenderProgressWindow();

void ArrangeRenderCancelAll();

void DrawArrangeClipSettingsChild(float panelW);

void ArrangeRenderQueuePosition(int& outIndex, int& outTotal);

bool ArrangeRenderBusy();

ArrangeRenderJob* ArrangeRenderFindJob(uint64_t id);

extern std::string gArrangeRenderFailNotice;

extern bool gArrangeRenderFailNoticeOpen;

void DrawArrangeRenderFailNotice();

bool ArrangeRenderBeginJob(ArrangeRenderJob& job);

void ArrangeRenderCancelActive();

void ArrangeRenderQueueTick();

void RemoveNodeByIndex(int index);



   // Field 'graph' domain (build step 10): thin forwarder from
   // Field::IFieldGraphHost onto the real graph (gNodes/SpawnNode/
   // RemoveNodeByIndex/ConnectNodes above). No policy here - the reconciler
   // (FieldGraphReconciler.cpp) owns every decision about what to mount/
   // update/unmount/connect; this only carries the calls out.
   struct MainGraphHost final : public Field::IFieldGraphHost
   {
      int mDroppedModCount = 0;
      int mDetachedCableCount = 0;
      int DroppedModCount() const override { return mDroppedModCount; }
      int DetachedCableCount() const override { return mDetachedCableCount; }

      int Mount(const std::string& typeName) override
      {
         if (!Spawnable(typeName))
            return -1;
         std::string category;
         for (const auto& cat : NodeFactory::Instance().GetCategories())
         {
            const auto& names = NodeFactory::Instance().GetNodesInCategory(cat);
            if (std::find(names.begin(), names.end(), typeName) != names.end())
            {
               category = cat;
               break;
            }
         }
         GraphNode* gn = SpawnNode(typeName, category, 0.0f, 0.0f);
         return gn != nullptr ? gn->index : -1;
      }

      void Unmount(int id) override
      {
         for (const auto& entry : Modulation::Instance().Links())
            if (entry.first.first == id) mDroppedModCount++;

         GraphNode* unmounting = FindNodeByIndex(id);
         if (unmounting && unmounting->node)
         {
            INode* targetSrc = unmounting->node.get();
            for (GraphNode& gn : gNodes)
            {
               if (gn.index == id) continue;
               int inputs = InputCountFor(gn);
               for (int slot = 0; slot < inputs; slot++)
               {
                  ImageCable* cable = CableFor(gn, slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     mDetachedCableCount++;
               }
               for (int slot = 0; slot < kMaxAudioSlots; slot++)
               {
                  AudioCable* cable = gn.node->AudioInputSlot(slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     mDetachedCableCount++;
               }
               for (int slot = 0; slot < kMaxNoteSlots; slot++)
               {
                  NoteCable* cable = gn.node->NoteInputSlot(slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     mDetachedCableCount++;
               }
            }
         }
         RemoveNodeByIndex(id);
      }

      int Remount(int existing, const std::string& typeName) override
      {
         // 1. Group membership rescue (doc §5.7 Case 2)
         GroupNode* ownerGroup = nullptr;
         for (auto& entry : gGroupMembers)
         {
            if (entry.second.count(existing))
            {
               ownerGroup = entry.first;
               break;
            }
         }

         // 2. Modulation / inbound cluster link rescue (doc §5.7 Case 4 & 5)
         std::set<int> dying = { existing };
         ClusterClipboard rescued;
         CaptureClusterLinks(dying, rescued);

         // 3. Outbound cable rescue (generated node feeding external node, doc §5.7 Case 5)
         struct OutboundLink {
            int srcSlot;
            int dstIndex;
            int dstSlot;
         };
         std::vector<OutboundLink> rescuedOutbound;
         GraphNode* dyingGn = FindNodeByIndex(existing);
         if (dyingGn && dyingGn->node)
         {
            INode* targetSrc = dyingGn->node.get();
            for (GraphNode& gn : gNodes)
            {
               if (gn.index == existing) continue;
               int inputs = InputCountFor(gn);
               for (int slot = 0; slot < inputs; slot++)
               {
                  ImageCable* cable = CableFor(gn, slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     rescuedOutbound.push_back({ 0, gn.index, slot });
               }
               for (int slot = 0; slot < kMaxAudioSlots; slot++)
               {
                  AudioCable* cable = gn.node->AudioInputSlot(slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     rescuedOutbound.push_back({ cable->GetOutputSlot(), gn.index, slot });
               }
               for (int slot = 0; slot < kMaxNoteSlots; slot++)
               {
                  NoteCable* cable = gn.node->NoteInputSlot(slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     rescuedOutbound.push_back({ 0, gn.index, slot });
               }
            }
         }

         RemoveNodeByIndex(existing);
         int fresh = Mount(typeName);
         if (fresh >= 0)
         {
            if (ownerGroup)
               gGroupMembers[ownerGroup].insert(fresh);

            GraphNode* freshGn = FindNodeByIndex(fresh);
            if (freshGn)
            {
               std::map<int, GraphNode*> newByOrig;
               newByOrig[existing] = freshGn;
               ApplyClusterLinks(newByOrig, rescued);
            }

            for (const auto& ob : rescuedOutbound)
            {
               std::string connErr;
               ConnectNodes(fresh, ob.srcSlot, ob.dstIndex, ob.dstSlot, connErr);
            }
         }
         return fresh;
      }

      void SetParam(int id, const std::string& paramName, float value) override
      {
         GraphNode* gn = FindNodeByIndex(id);
         if (gn == nullptr)
            return;

         struct SetFloatVisitor : public ParamVisitor
         {
            const std::string& targetName;
            float targetValue;
            explicit SetFloatVisitor(const std::string& name, float v) : targetName(name), targetValue(v) {}
            void Float(const char* name, float& v) override { if (targetName == name) v = targetValue; }
            void Int(const char* name, int& v) override { if (targetName == name) v = (int)std::lround((double)targetValue); }
            void Bool(const char* name, bool& v) override { if (targetName == name) v = targetValue != 0.0f; }
            void Text(const char*, std::string&) override {}
            void Color(const char*, float[3]) override {}
         } visitor(paramName, value);
         gn->node->VisitParams(visitor);
      }

      void Connect(int srcId, int srcSlot, int dstId, int dstSlot) override
      {
         std::string err;
         ConnectNodes(srcId, srcSlot, dstId, dstSlot, err);
      }

      void Place(int id, float x, float y) override
      {
         GraphNode* gn = FindNodeByIndex(id);
         if (gn == nullptr)
            return;
         gn->spawnX = x;
         gn->spawnY = y;
         gn->liveX = x;
         gn->liveY = y;
         // Do NOT clear needsPosition here: for a freshly-mounted node it is
         // still true, and it's the only signal that tells the main.cpp:~53053
         // per-frame tick to push spawnX/spawnY into the node-editor library
         // via ed::SetNodePosition on the next frame. Clearing it here (as
         // this used to) stomped that pending push before it ever fired, so
         // every place()'d node kept whatever default position the editor
         // library assigns unpositioned nodes - producing the fully-stacked
         // cluster instead of the requested layout.
      }

      bool Alive(int id) const override { return FindNodeByIndex(id) != nullptr; }

      std::string TypeNameOf(int id) const override
      {
         GraphNode* gn = FindNodeByIndex(id);
         return gn != nullptr ? gn->typeName : std::string();
      }

      bool Spawnable(const std::string& typeName) const override
      {
         if (typeName == "Field Graph" || !IsUserSpawnable(typeName))
            return false;
         for (const auto& cat : NodeFactory::Instance().GetCategories())
         {
            const auto& names = NodeFactory::Instance().GetNodesInCategory(cat);
            if (std::find(names.begin(), names.end(), typeName) != names.end())
               return true;
         }
         return false;
      }
   };



   // Build step 15 ("Instrument Mode"): identical to MainGraphHost in every
   // respect except Mount flags the spawned node hiddenFromCanvas and adds
   // it to the owning FieldGraphNode's mMountedIndices, Unmount removes it
   // from that set, and Place() is a deliberate no-op (an encapsulated
   // child's canvas position is meaningless - see doc §3.3). Deliberately
   // NOT refactored to share a base with MainGraphHost (doc trap 4) - step
   // 16 needs its own third variant, and unifying three not-yet-fully-
   // understood shapes now would fossilize the wrong abstraction.
   struct VirtualGraphHost final : public Field::IFieldGraphHost
   {
      FieldGraphNode* owner = nullptr;
      int mDroppedModCount = 0;
      int mDetachedCableCount = 0;
      int DroppedModCount() const override { return mDroppedModCount; }
      int DetachedCableCount() const override { return mDetachedCableCount; }

      int Mount(const std::string& typeName) override
      {
         if (!Spawnable(typeName))
            return -1;
         std::string category;
         for (const auto& cat : NodeFactory::Instance().GetCategories())
         {
            const auto& names = NodeFactory::Instance().GetNodesInCategory(cat);
            if (std::find(names.begin(), names.end(), typeName) != names.end())
            {
               category = cat;
               break;
            }
         }
         GraphNode* gn = SpawnNode(typeName, category, 0.0f, 0.0f);
         if (gn == nullptr)
            return -1;
         gn->hiddenFromCanvas = true;
         return gn->index;
      }

      void Unmount(int id) override
      {
         for (const auto& entry : Modulation::Instance().Links())
            if (entry.first.first == id) mDroppedModCount++;

         GraphNode* unmounting = FindNodeByIndex(id);
         if (unmounting && unmounting->node)
         {
            INode* targetSrc = unmounting->node.get();
            for (GraphNode& gn : gNodes)
            {
               if (gn.index == id) continue;
               int inputs = InputCountFor(gn);
               for (int slot = 0; slot < inputs; slot++)
               {
                  ImageCable* cable = CableFor(gn, slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     mDetachedCableCount++;
               }
               for (int slot = 0; slot < kMaxAudioSlots; slot++)
               {
                  AudioCable* cable = gn.node->AudioInputSlot(slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     mDetachedCableCount++;
               }
               for (int slot = 0; slot < kMaxNoteSlots; slot++)
               {
                  NoteCable* cable = gn.node->NoteInputSlot(slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     mDetachedCableCount++;
               }
            }
         }
         RemoveNodeByIndex(id);
      }

      int Remount(int existing, const std::string& typeName) override
      {
         // Same rescue steps as MainGraphHost::Remount (group membership,
         // modulation/cluster links, outbound cable rescue) - copied rather
         // than shared, see this struct's header comment.
         GroupNode* ownerGroup = nullptr;
         for (auto& entry : gGroupMembers)
         {
            if (entry.second.count(existing))
            {
               ownerGroup = entry.first;
               break;
            }
         }

         std::set<int> dying = { existing };
         ClusterClipboard rescued;
         CaptureClusterLinks(dying, rescued);

         struct OutboundLink {
            int srcSlot;
            int dstIndex;
            int dstSlot;
         };
         std::vector<OutboundLink> rescuedOutbound;
         GraphNode* dyingGn = FindNodeByIndex(existing);
         if (dyingGn && dyingGn->node)
         {
            INode* targetSrc = dyingGn->node.get();
            for (GraphNode& gn : gNodes)
            {
               if (gn.index == existing) continue;
               int inputs = InputCountFor(gn);
               for (int slot = 0; slot < inputs; slot++)
               {
                  ImageCable* cable = CableFor(gn, slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     rescuedOutbound.push_back({ 0, gn.index, slot });
               }
               for (int slot = 0; slot < kMaxAudioSlots; slot++)
               {
                  AudioCable* cable = gn.node->AudioInputSlot(slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     rescuedOutbound.push_back({ cable->GetOutputSlot(), gn.index, slot });
               }
               for (int slot = 0; slot < kMaxNoteSlots; slot++)
               {
                  NoteCable* cable = gn.node->NoteInputSlot(slot);
                  if (cable && cable->IsConnected() && cable->GetSource() == targetSrc)
                     rescuedOutbound.push_back({ 0, gn.index, slot });
               }
            }
         }

         Unmount(existing);
         int fresh = Mount(typeName);
         if (fresh >= 0)
         {
            if (ownerGroup)
               gGroupMembers[ownerGroup].insert(fresh);

            GraphNode* freshGn = FindNodeByIndex(fresh);
            if (freshGn)
            {
               std::map<int, GraphNode*> newByOrig;
               newByOrig[existing] = freshGn;
               ApplyClusterLinks(newByOrig, rescued);
            }

            for (const auto& ob : rescuedOutbound)
            {
               std::string connErr;
               ConnectNodes(fresh, ob.srcSlot, ob.dstIndex, ob.dstSlot, connErr);
            }
         }
         return fresh;
      }

      void SetParam(int id, const std::string& paramName, float value) override
      {
         GraphNode* gn = FindNodeByIndex(id);
         if (gn == nullptr)
            return;

         struct SetFloatVisitor : public ParamVisitor
         {
            const std::string& targetName;
            float targetValue;
            explicit SetFloatVisitor(const std::string& name, float v) : targetName(name), targetValue(v) {}
            void Float(const char* name, float& v) override { if (targetName == name) v = targetValue; }
            void Int(const char* name, int& v) override { if (targetName == name) v = (int)std::lround((double)targetValue); }
            void Bool(const char* name, bool& v) override { if (targetName == name) v = targetValue != 0.0f; }
            void Text(const char*, std::string&) override {}
            void Color(const char*, float[3]) override {}
         } visitor(paramName, value);
         gn->node->VisitParams(visitor);
      }

      void Connect(int srcId, int srcSlot, int dstId, int dstSlot) override
      {
         std::string err;
         ConnectNodes(srcId, srcSlot, dstId, dstSlot, err);
      }

      // Deliberately a no-op, not an error - an encapsulated child's
      // position is meaningless (nothing ever draws it at (x,y)) but
      // place() is still syntactically valid to call, e.g. code shared
      // between a still-encapsulated and an already-unpacked (step 16)
      // instance. See doc §3.3.
      void Place(int /*id*/, float /*x*/, float /*y*/) override {}

      bool Alive(int id) const override { return FindNodeByIndex(id) != nullptr; }

      std::string TypeNameOf(int id) const override
      {
         GraphNode* gn = FindNodeByIndex(id);
         return gn != nullptr ? gn->typeName : std::string();
      }

      bool Spawnable(const std::string& typeName) const override
      {
         if (typeName == "Field Graph" || !IsUserSpawnable(typeName))
            return false;
         for (const auto& cat : NodeFactory::Instance().GetCategories())
         {
            const auto& names = NodeFactory::Instance().GetNodesInCategory(cat);
            if (std::find(names.begin(), names.end(), typeName) != names.end())
               return true;
         }
         return false;
      }
   };

bool IsKernelDrivenParam(int nodeIndex, int paramIndex);

bool CanBindModulation(int dstNodeIndex, int paramIndex);

extern std::string gPatchPath;

extern bool gPatchDirty;

extern std::string gPatchStatus;

extern bool gAutosaveEnabled;

extern int gAutosaveSeconds;

extern bool gAutosaveFailed;

extern bool gAutosaveFailureLogged;

extern double gLastAutosaveTime;

extern bool gShowAutosaveRecoveryModal;

extern Patch::Data gPendingRecoveryData;

extern std::string gAutosaveRecoveryTimestamp;

extern std::string gAutosaveRecoveryError;

Patch::Data BuildPatchData();

std::string AutosavePath();

std::string AutosaveMarkerPath();

void LoadGeneralSettings();

void SaveGeneralSettings();

void LoadWorkspaceSettings();

void SaveWorkspaceSettings();

void LoadAudioSettings();

void SaveAudioSettings();

void LoadDefaultExprGlobals();

void SaveDefaultExprGlobals();

void DiscardAutosave();

bool WriteAutosaveNow();

void PollAutosave();

void CheckAutosaveRecovery();

void NotePatchFileStamp(const std::string& path);

bool SavePatchTo(const std::string& path);

extern bool gSuppressUndoCheckpoints;


   // deque, not vector: erase(begin()) at the depth cap below shifts every
   // remaining element, and each element is a full Patch::Data (two
   // heap-allocated strings per param per node) - expensive to shift at a
   // 200-deep cap on a large patch. pop_front() is O(1) on a deque.
   // One point in history. `patch` is the graph; `gestures` is the Shift-drag
   // recordings looping at that moment (see GestureRecorder). Recordings are
   // session state rather than patch content - they are not in Patch::Data
   // and never reach a saved file - but undo still has to make one appear and
   // disappear at the right point in history, exactly the way a typed
   // expression (which IS in Patch::Data) already does. Snapshotting them
   // alongside the graph is what gives that: the checkpoint pushed when the
   // user grabbed the knob predates the recording, so undoing to it removes
   // the recording, and redo brings it back. Carried across ApplyPatchData's
   // respawn through the same old-index -> new-index remap as RemapGestures.
   struct UndoEntry
   {
      Patch::Data patch;
      GestureRecorder::PlaybackMap gestures;
      // Set only for timeline-only gestures (move a clip, trim, split, add a
      // marker). Undoing one of these swaps the arrangement back and touches
      // nothing else - it must NOT go through ApplyPatchData, which tears down
      // and respawns the whole graph. That respawn is why dragging a clip used
      // to reset every node's internal state, drop audio, and rebuild every
      // FBO. Clips reference node uids, which the graph side never changes
      // here, so the two entry kinds coexist with no remapping.
      bool arrangeOnly = false;
      Arrange::Model arrange;
   };

extern std::deque<UndoEntry> gUndoStack;

extern std::deque<UndoEntry> gRedoStack;


   inline const size_t kMaxUndoDepth = 200;

double GesturePlaybackClock();

double GestureClockNow();

void GestureSyncClockAxis();

GestureRecorder::PlaybackMap RemapGestures(const GestureRecorder::PlaybackMap& gestures,
                                              const std::map<int, int>& remap);

void SeedDefaultArrangeStreams();

void ClearPatchWatch();

void NewPatch();

std::string SaveAISkillFile(const std::string& folder, const char* filename, const char* content);

void DrawSettingsWindow(bool* open);

void NoteGraphEditedForLiveIssues();

void ApplyPatchData(const Patch::Data& data, std::map<int, int>* outRemap = nullptr, bool keepIndices = false);



   // reload = the file watcher re-reading the open file (R39): same read and
   // resolve, but applied like an undo step - one checkpoint first, the stacks,
   // view and routing mode kept, no recents/autosave bookkeeping.
   // A patch with no `pos` records (hand-written / headless-authored) loads with
   // every node at 0,0. Real sizes only exist once the nodes have drawn, so the
   // layout waits for them (same shape as RunFieldGraphUnpackPhase2Tick): the
   // load stashes the data it needs, the per-frame tick measures, places, and
   // asks for a fit-to-content. Nodes without a measurement after the retry
   // budget use PatchLayout::EstimateSize.
   struct PendingAutoLayout
   {
      bool active = false;
      Patch::Data data;          // nodes + cables only
      std::map<int, int> remap;  // NodeRecord::index -> live GraphNode index
      int framesWaited = 0;
   };

extern PendingAutoLayout gPendingAutoLayout;

void RunAutoLayoutTick();

bool LoadPatchDataImpl(Patch::Data& data, const std::string& path, bool reload);

bool LoadPatchFromImpl(const std::string& path, bool reload);



   // R501: `mod`/`expr` lines that name a control by key cannot be resolved while loading, because the
   // window app has not drawn a node of that type yet. They wait here (saved-file indices, plus the
   // file->live node map) until the nodes have drawn, then PollPendingKeyed resolves and applies them.
   struct PendingKeyed
   {
      std::vector<Patch::ModRecord> mods;
      std::vector<Patch::ExprRecord> exprs;
      std::map<int, int> remap;
      int waited = 0;
      bool active = false;
   };

extern PendingKeyed gPendingKeyed;

void ApplyHeadlessSets(Patch::Data& data, std::vector<Headless::Issue>* errors, std::vector<Headless::Issue>* warnings);

bool LoadPatchDataImpl(Patch::Data& data, const std::string& path, bool reload);

bool LoadPatchFrom(const std::string& path);

void FinishCanonicalize(Patch::Data& data, const Headless::Job& job, Headless::Status& st);

bool RunCanonicalize(const Headless::Job& job, Headless::Status& st);

extern std::unordered_map<int, std::vector<Headless::Issue>> gLiveIssues;

extern unsigned gLiveIssueSerial;

extern unsigned gLiveIssueDoneSerial;

extern double gLiveIssueEditTime;

void NoteGraphEditedForLiveIssues();

void RefreshLiveIssues();

void PushUndoSnapshot(Patch::Data snapshot);

void PushUndoCheckpoint();

extern Patch::Data gDragStartSnapshot;

extern bool gDragSnapshotValid;

extern bool gDragSnapshotPushed;

void RemapFieldGraphOwnership(const std::map<int, int>& remap);

INode* ResolveFieldGraphBoundaryTerminal(FieldGraphNode* fgn);

void RunFieldGraphRegenerate(FieldGraphNode* target);

void RunFieldGraphUnpackPhase1(FieldGraphNode* target);

void RunFieldGraphUnpackPhase2Tick();

void PerformCopyPaste(const std::set<int>& toCopy);

void PushArrangeUndoSnapshot(const Arrange::Model& before);

void PushArrangeUndo();

void Undo();

void Redo();

bool HandleRpcCommand(const std::string& method, const nlohmann::json& params,
                         nlohmann::json& outResult, std::string& outError);

void DrawMinimap();

std::string BundledResourcePath(const char* relPath);

void SetWindowIcon(GLFWwindow* window);

void SavePatchInteractive(bool forceDialog);



   // Set for one frame when an action was deferred because the patch has
   // unsaved changes, so the UI pass knows to pop the confirmation modal.
   // ---- R39: watch the open patch file ------------------------------------
   // An AI (or any editor) rewrites the open file; the canvas follows. Polled
   // once a second on the main thread: mtime + size, no watcher thread and no
   // Platform:: call. Clean canvas -> reload as one undo step; unsaved edits ->
   // a banner instead of clobbering them.
   struct PatchFileStamp
   {
      bool valid = false;
      long long mtime = 0;
      unsigned long long size = 0;
      bool operator==(const PatchFileStamp& o) const { return valid == o.valid && mtime == o.mtime && size == o.size; }
   };

extern PatchFileStamp gPatchStamp;

extern std::string gPatchWatchPath;

extern double gPatchWatchNextPoll;

extern bool gPatchChangedOnDisk;

void NotePatchFileStamp(const std::string& path);

void ClearPatchWatch();

void PollPendingKeyed();

void PollPatchFileWatch(bool force = false);

extern bool gShowUnsavedChangesModal;

extern std::function<void()> gPendingUnsavedAction;

void GuardUnsavedChanges(std::function<void()> action);

void RequestClose(GLFWwindow* window);

void CloseProjectorWindow(size_t i);

void CloseProjectorWindowFor(int nodeIndex);

void CloseAllProjectorWindows();

ProjectorWindow* FindProjectorWindow(int nodeIndex);

int ProjectorMonitorIndex(GLFWwindow* w);



   // Frame clock rate policy (FrameClockActive says when it applies).
   // The primary Output is the first fullscreen projector window, else the
   // first one opened; with two Outputs on displays of different refresh,
   // only the primary's is presented on its refresh grid. With no projector
   // open it is the canvas window's display.
   //
   // Rate: a whole divisor of that display's refresh R, never an uneven
   // rate. The base divisor is the largest that still gives >= 60 fps
   // (60 Hz -> every refresh, 120 -> every 2nd = 60 fps, 144 -> 72 fps).
   // Native R only with headroom: the frame's own work (everything but the
   // wait) must fit in 55% of a refresh at p95 over a 2 s window, and it drops
   // back to the base divisor as soon as p95 crosses 85%.
   struct ProjectorPacer
   {
      static constexpr int kWindow = 120;
      double refreshHz = 0.0;
      int baseIntervals = 1;
      int intervals = 1;
      int monitorX = 0, monitorY = 0;
      bool haveMonitor = false;
      double lastMonitorCheck = -1.0;
      double lastPresent = -1.0; // glfwGetTime() when the last wait returned
      std::vector<double> workMs;

      void SetDisplay(int x, int y, double hz)
      {
         haveMonitor = true;
         monitorX = x;
         monitorY = y;
         if (hz == refreshHz)
            return;
         refreshHz = hz;
         baseIntervals = hz > 0.0 ? std::max(1, (int)std::floor(hz / 60.0 + 0.02)) : 1;
         intervals = baseIntervals;
         workMs.clear();
      }

      void AddWork(double ms)
      {
         if (baseIntervals <= 1 || refreshHz <= 0.0)
            return;
         workMs.push_back(ms);
         if ((int)workMs.size() < kWindow)
            return;
         std::sort(workMs.begin(), workMs.end());
         const double p95 = workMs[(size_t)(0.95 * (double)(workMs.size() - 1))];
         const double periodMs = 1000.0 / refreshHz;
         if (intervals == baseIntervals && p95 < 0.55 * periodMs)
            intervals = 1;
         else if (intervals == 1 && p95 > 0.85 * periodMs)
            intervals = baseIntervals;
         workMs.clear();
      }

      void Reset()
      {
         *this = ProjectorPacer{};
      }
   };

extern ProjectorPacer gProjectorPacer;



   // Slow-frame attribution (Bench::Tail, B3/B8 only): when the GPU finished
   // a frame's work. A fence goes in after the canvas swap and after the last
   // projector swap; it is polled without blocking at a few points in the
   // following loop, and the first poll that sees it signaled writes "fence
   // -> signaled" ms into the record of the frame that placed it. An upper
   // bound, at the resolution of the poll points. Sync objects are shared
   // across the share group, so any context may poll.
   struct TailFence
   {
      GLsync sync = nullptr;
      double placedMs = 0.0;
      size_t record = 0; // Bench::Tail().all index of the frame that placed it
      bool canvas = false;

      void Place(bool isCanvas)
      {
         Bench::FrameTail& tail = Bench::Tail();
         // Opt-in (INFINITE_BENCH_TAILFENCE=1): the fence's glFlush right after
         // an interval-0 swap blocks ~10 ms on macOS, which turned B3 from
         // 0.2% missed vsync into 1.6-13.8%. Off, the probe costs nothing.
         static const bool sFencesOff = [] { const char* e = getenv("INFINITE_BENCH_TAILFENCE"); return !(e && e[0] == '1'); }();
         if (!tail.active || sFencesOff)
            return;
         Drop();
         sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
         glFlush();
         placedMs = Bench::ScopedStageTimer::NowMs();
         record = tail.all.size(); // `cur` is pushed at this index by EndFrame
         canvas = isCanvas;
      }
      // True while the fence is still in flight.
      bool Poll()
      {
         if (sync == nullptr)
            return false;
         const GLenum r = glClientWaitSync(sync, 0, 0);
         if (r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED)
            return r == GL_TIMEOUT_EXPIRED;
         const double ms = Bench::ScopedStageTimer::NowMs() - placedMs;
         Bench::FrameTail& tail = Bench::Tail();
         Bench::FrameTail::Record* rec = record < tail.all.size() ? &tail.all[record]
                                         : (record == tail.all.size() ? &tail.cur : nullptr);
         if (rec != nullptr)
            (canvas ? rec->canvasGpuMs : rec->projGpuMs) = ms;
         Drop();
         return false;
      }
      void Drop()
      {
         if (sync != nullptr)
            glDeleteSync(sync);
         sync = nullptr;
      }
   };

extern TailFence gTailCanvasFence;

extern TailFence gTailProjFence;

void PollTailFences();

void PaceProjectorPresent(GLFWwindow* canvas);

void MoveProjectorToMonitor(ProjectorWindow& pw, int monitorIndex);

void ToggleProjectorFullscreen(ProjectorWindow& pw);

void OpenProjectorWindow(GLFWwindow* mainWindow, GraphNode& gn);


// ================================================== Audio node sweep discovery
//
// Shared by INFINITE_AUDIOPARAMSWEEPTEST and INFINITE_AUDIOTEARDOWNSWEEPTEST:
// every node type is read out of NodeFactory - the same registry
// RegisterNodes() populates - rather than a hand-maintained list, so a node
// added later (any remaining row of docs/plans/audio/README.md §3) is
// covered by both sweeps without anyone editing this file. Which of a node's
// interfaces it answers (IAudioSource, INoteSource, AudioNodeForNotePorts(),
// AudioInputSlot/NoteInputSlot) is probed the same way InputCountFor already
// probes AudioInputSlot/NoteInputSlot generically for pin counts - see
// .claude/skills/new-audio-node/SKILL.md §3's step 9 note that audio and note
// pins share one slot index space.
struct AudioNodeShape
{
   bool isAudioSource = false;
   bool isNoteSource = false;
   bool isModulator = false;
   bool hasNotePorts = false; // AudioNodeForNotePorts() != nullptr (NoteToCVNode)
   int audioInputSlots = 0;   // highest AudioInputSlot(i) != nullptr index + 1
   int noteInputSlots = 0;    // highest NoteInputSlot(i) != nullptr index + 1
   int firstNoteInputSlot = -1; // actual slot index of the first live NoteInputSlot(); -1 if none.
                                 // Nodes with note-only input (no AudioInputSlot at all) can have
                                 // their note pin start at a nonzero slot - e.g. WaveTerrainNode and
                                 // ImageSpectralSynthNode both only answer NoteInputSlot(1). Callers
                                 // that need to actually connect a cable must use this, not slot 0.

   // Does this node own (or reach) an AudioNode at all? AUDIOPARAMSWEEPTEST's
   // "reaches the audio thread" check needs one to drive; a node with none
   // (Audio Out - a pure topology terminal, no ProcessBlock of its own) has
   // nothing to test there.
   bool HasAudioNode() const { return isAudioSource || isNoteSource || hasNotePorts; }

   // Does this node touch the audio/note cable graph at all, as either side
   // of a cable? AUDIOTEARDOWNSWEEPTEST's teardown invariant applies to every
   // one of these, including pure terminals like Audio Out.
   bool ParticipatesInAudioGraph() const
   {
      return isAudioSource || isNoteSource || hasNotePorts || audioInputSlots > 0 || noteInputSlots > 0;
   }
};



struct AudioSweepCandidate
{
   std::string name;
   std::string category;
   AudioNodeShape shape;
};

std::vector<AudioSweepCandidate> DiscoverAudioSweepCandidates(
   const std::function<bool(const AudioNodeShape&)>& include);

bool RunGainFixture();

bool RunFilterFixture();

bool RunOscWaveformFixture();

bool RunWavetableFixture();

bool RunEnvelopeFixture();

bool RunVoiceStealFixture();

bool RunMusicTimeFixture();

bool RunAudioFilterFixture();

bool RunDynamicsFixture();



// Delay's exit criterion, same rig shape as DynamicsTest above (real
// AudioEffectNode -> AudioEffectRuntime -> DelayKernel pipeline, not the
// kernel's internals in isolation).
namespace DelayTest
{
   inline std::unique_ptr<AudioEffectNode> MakeDelayNode(bool bounce, double sampleRate)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Delay")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      *node->ParamPtr("bounce") = bounce ? 1.0f : 0.0f;
      node->mix = 1.0f; // 100% wet so the fixture measures the delayed tap directly, not a dry/wet blend
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      return node;
   }

   // Runs `numSamples` frames of `numChannels`, one sample at a time (block
   // size 1) through the node, capturing every channel's output.
   template <typename SampleFn>
   void RunSamples(AudioEffectNode& node, int numSamples, int numChannels, SampleFn getSample,
                    std::vector<std::vector<float>>* outSamples)
   {
      AudioNode* audioNode = node.GetAudioNode();
      float inVal[2] = { 0.0f, 0.0f };
      float outVal[2] = { 0.0f, 0.0f };
      float* inPtrs[2] = { &inVal[0], &inVal[1] };
      float* outPtrs[2] = { &outVal[0], &outVal[1] };
      AudioBuffer inBuffer;
      inBuffer.channels = inPtrs;
      inBuffer.numChannels = numChannels;
      inBuffer.numFrames = 1;
      AudioBuffer outBuffer;
      outBuffer.channels = outPtrs;
      outBuffer.numChannels = numChannels;
      outBuffer.numFrames = 1;
      const AudioBuffer* inputs[1] = { &inBuffer };
      if (outSamples != nullptr)
         outSamples->assign(numChannels, {});
      for (int i = 0; i < numSamples; i++)
      {
         for (int ch = 0; ch < numChannels; ch++)
            inVal[ch] = getSample(i, ch);
         audioNode->ProcessBlock(inputs, 1, outBuffer);
         if (outSamples != nullptr)
            for (int ch = 0; ch < numChannels; ch++)
               (*outSamples)[ch].push_back(outVal[ch]);
      }
   }
}

bool RunDelayFixture();

bool RunReverbFixture();

bool RunWavetableShaperFixture();

bool RunSamplerFixture();

bool RunPaulStretchFixture();

bool RunResonatorFixture();

bool RunMetallicDecayFixture();

bool RunCycleShaperFixture();

bool RunMidiFileFixture();

bool RunMpeFixture();

bool RunShapeResonatorFixture();
bool RunSpatialFixture();

bool RunSpectrumSlideFixture();

bool RunKeySnapFixture();

bool RunSpecBlurFixture();

bool RunGrainMolderFixture();

bool RunMolderFixture();

bool RunSpoutLoopTest();

bool RunGranularFixture();

int RunDspTest();

int RunFieldTest();

int RunFieldElementTest();

int RunFieldParamTest();

int RunFieldStateTest();

int RunFieldTransferTest();

int RunFieldSampleTest();

int RunFieldPinDeclTest();

void RunRecSyncTest();

void RunAudioRingTest();

void RunVideoExactTest();

void RunRecExportTest(int width, int height, bool starved, const char* label);

bool RunAudioPdcTest();
bool RunI18nTest();

#if defined(__linux__)
int RunMidiParseTest();
#endif

int RunCVRecorderTest();

int RunMidiCC14Test();
int RunNdiTest();

int RunAudioParamSweepTest();

bool RunFmModeDebugCheck();

bool RunFmRenderCheck();

bool RunBrowserSortTest();

bool RunAppearanceSelfTest();

int RunPluginScanTest();

int RunMidiBendTest();

int RunPluginNodeHandleTest();

#if INFINITE_ENABLE_VST3
int RunVST3ScanTest();
#endif

#if INFINITE_ENABLE_VST3
#if defined(__linux__)
int RunVST3EditorShotTest();
#endif
#endif

#if INFINITE_ENABLE_VST3
#if defined(__linux__)
int RunVST3BlocklistTest();
#endif
#endif

void RunRpcBatchTest();

void RunPatchWatchTest();

int RunAutosaveMarkerTest();

int RunRemoveBgTest();

int RunNetworkTest();

float ShapeToParam(const ParamRef& ref, float v);

bool RunPerfPanelSelfTest();

void ArrangeRefreshActiveClipModBypass();

bool ArrangeClipBypassesMod(int nodeIndex, int paramIndex);

float ArrangeClipBypassBaseValue(const ParamRef& ref, const Modulation::Source& src);

void RefreshParamDriverFlags();

void ApplyModulationAndPalette(int frameId, bool isNormalFrame = false);

#if defined(__linux__)
int RunCameraConvTest();
#endif

#if defined(__linux__)
int RunHostEnvTest();
#endif

int RunSyphonPatchTest();

int RunPatchLayoutTest();

void BenchFillXruns(Bench::BenchReport& report, const AudioEngine::XrunCounts& base);

void BuildBenchB1Audio(long numVoices, int bufferFrames = 256, float yOffset = 0.0f);

void BuildBenchB2Scene(const std::string& scaleStr, bool isAnim, int& outRender3DIdx, int& outOutputIdx,
                              int* outTwistIdx = nullptr, int* outMatIdx = nullptr, int* outCamIdx = nullptr,
                              bool useEmbossForGlitch = false);

void BuildBenchB4Scene(const std::string& scaleStr, const std::string& shadowStr, bool isAnim, int& outRender3DIdx, int& outOutputIdx, int& outCamIdx, int& outLfoIdx);

void BuildBenchB6Scene(int n, bool collapsed, float& outMaxX, float& outMaxY, int& outDragNodeIdx);

bool BuildBenchB8Scene(const std::vector<std::string>& clipPaths, bool withSyphon, bool withCamera, int windows,
                              std::vector<int>& outClipIdx, std::vector<int>& outOutputIdx,
                              int& outSyphonIdx, int& outCameraIdx, std::string& outError);

std::string ExplainLive(bool json, bool all);

void JoinLiveTier1();

void HeadlessTick(int& frameId, GLFWwindow* window);
}
