// Node names, titles, links and field-bridge helpers (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
 // one answer to "is a text field about to take the keystrokes" - defined below


   // Blender-style numpad view hotkeys for any hovered orbit-camera viewport
   // item (mini-viewport, viewport-panel card, Render3D preview): 1/3/7 snap
   // to front/right/top, Ctrl+1/3/7 to back/left/bottom, 0 to a default
   // three-quarter view. Both the number row and the numeric keypad fire the
   // same view, matching Blender's own leniency. Must be called right after
   // the InvisibleButton/item whose hover state should gate it - like the
   // drag/wheel handling beside every call site, it reads
   // ImGui::IsItemHovered() for the *last* item, so no hover state needs to
   // be threaded through. Also refuses to fire while a text field has focus,
   // so typing "1" into a param field doesn't reframe a hovered-but-unrelated
   // viewport behind it. Returns true if it changed azimuth/elevation. An
   // optional onWillChange is invoked right before the write happens - the
   // two Render3D call sites use it to PushUndoCheckpoint() while the old
   // values are still live, matching that camera's "orbit"/"elevation"
   // sliders; the two gNodeCameras (mini-viewport/panel) sites pass nothing,
   // matching drag-orbit there, which also pushes no undo checkpoint.
   bool ApplyViewHotkeys(float& azimuth, float& elevation, const std::function<void()>& onWillChange)
   {
      if (!ImGui::IsItemHovered() || TextFocusClaimed())
         return false;

      const bool ctrl = ImGui::GetIO().KeyCtrl;
      // Pole clamp matches the drag-orbit elevation clamp used everywhere
      // else in this file (DrawPreview, the Render3D "elevation" slider):
      // exactly 90 makes the up vector parallel to the view direction and
      // the image rolls.
      const float kPole = 85.9437f;

      auto pressed = [](ImGuiKey rowKey, ImGuiKey padKey) {
         return ImGui::IsKeyPressed(rowKey, false) || ImGui::IsKeyPressed(padKey, false);
      };

      float newAzimuth, newElevation;
      if (pressed(ImGuiKey_1, ImGuiKey_Keypad1))
      {
         newAzimuth = ctrl ? 180.0f : 0.0f;      // front / back
         newElevation = 0.0f;
      }
      else if (pressed(ImGuiKey_3, ImGuiKey_Keypad3))
      {
         newAzimuth = ctrl ? -90.0f : 90.0f;     // right / left
         newElevation = 0.0f;
      }
      else if (pressed(ImGuiKey_7, ImGuiKey_Keypad7))
      {
         newAzimuth = 0.0f;
         newElevation = ctrl ? -kPole : kPole;   // top / bottom
      }
      else if (pressed(ImGuiKey_0, ImGuiKey_Keypad0))
      {
         newAzimuth = 34.3775f;                  // default three-quarter view
         newElevation = 22.9183f;
      }
      else
      {
         return false;
      }

      // Snapshot before the write, not after - an undo checkpoint taken
      // once the new values are already live would have nothing to restore.
      if (onWillChange)
         onWillChange();
      azimuth = newAzimuth;
      elevation = newElevation;
      return true;
   }


   // Node search: case-insensitive substring that treats '-' and '_' as
   // spaces, and also matches with spaces dropped, so "key snap", "key-snap"
   // and "keysnap" all find "Key-Snap". Both sides are folded (case, Latin accents, Cyrillic).
   bool NodeSearchMatches(std::string hay, std::string q)
   {
      auto norm = [](std::string& t) {
         t = FoldForSearch(t);
         for (char& c : t)
            if (c == '-' || c == '_')
               c = ' ';
      };
      auto strip = [](const std::string& t) {
         std::string o;
         for (char c : t)
            if (c != ' ')
               o += c;
         return o;
      };
      norm(hay);
      norm(q);
      if (hay.find(q) != std::string::npos)
         return true;
      const std::string qs = strip(q);
      return !qs.empty() && strip(hay).find(qs) != std::string::npos;
   }


   // Registered node names are the patch-file keys and must not change, so
   // casing is a display concern only - lowering it here keeps saved patches
   // loading while the UI reads the way the user asked for.
   std::string DisplayName(const std::string& name)
   {
      // "Dynamics" reads as "compressor" everywhere a user sees it - search,
      // spawn menu, node header, help popup - but the registered type key
      // (gn.typeName, NodeFactory's lookup, every saved patch's node-type
      // string) stays "Dynamics" so existing patches keep loading.
      if (name == "AudioEffects")
         return "audio effects";
      if (name == "Dynamics")
         return "compressor";
      // Registered type key stays "Sampler" (patches/undo/copy-paste key off
      // it), but it grew a record input, an interactive draggable loop
      // range, and reverse/ping-pong - "sample player" reads as what it now
      // is rather than as a bare one-shot playback node.
      if (name == "Sampler")
         return "sample player";
      if (name == "Slicer")
         return "slicer";
      if (name == "PaulStretch")
         return "paul stretch";
      if (name == "Granular")
         return "granular";
      // Registered type key stays "FieldPixel" (patches key off it, and it
      // was registered without a space unlike its Field Modifier/Primitive/
      // Synth/Effect/Graph siblings), but every sibling reads with a space
      // in the UI, so match that here rather than leave this one node an
      // unspaced outlier in search/spawn menus.
      if (name == "FieldPixel")
         return "field pixel";
      // Registered type key stays "transform" (patches key off it), but the
      // 3D geometry operator is *also* registered as "Transform" and both
      // fall through to the same lowercased "transform" here, making them
      // indistinguishable in search except for a dimmed category tag -
      // give the 2D/compositing one a distinct label.
      if (name == "transform")
         return "2d transform";
      // Registered type keys stay "Drift"/"Moves" (patches/undo/copy-paste/help-table lookups all key
      // off them), but both read as generic node names next to the rest of the Prediction category -
      // give them the descriptive names the design settled on without touching the saved token.
      if (name == "Drift")
         return "predictive lfo";
      if (name == "Moves")
         return "predictive macro";
#if defined(_WIN32)
      // Registered type key stays "Syphon In"/"Syphon Out" (patch files key
      // off it), but the Windows implementation is backed by Spout2, not
      // Syphon (which is macOS-only IOSurface/Mach-port tech) - see
      // src/platform/win/PlatformWinSyphon.cpp.
      if (name == "Syphon In")
         return "spout in";
      if (name == "Syphon Out")
         return "spout out";
#elif !defined(__APPLE__)
      if (name == "Syphon In")
         return "syphon in (unavailable on linux)";
      if (name == "Syphon Out")
         return "syphon out (unavailable on linux)";
#endif
      std::string out = name;
      std::transform(out.begin(), out.end(), out.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      return out;
   }


   // True for a NodeFactory-registered name a user should be able to spawn
   // going forward. Excludes GeometryOpNode's three deprecated `*Selected`
   // ops (see the Op enum comment in GeometryOpNodes.h) - they stay
   // registered so old patches and internal test fixtures can still name
   // them, but a fresh graph should reach for the general op + selectionOnly
   // instead. Also excludes "Group": groups are only ever created via Cmd+G
   // on a selection (SpawnNode("Group", ...) is still called from there, and
   // from patch load), never hand-placed from the palette - a bare Group
   // node has nothing inside it to auto-fit to, so palette-spawning one
   // would just place an empty, permanently-zero-size box. Every spawn-menu /
   // search-list enumeration site should filter through this rather than
   // NodeFactory's raw listing.
   bool IsUserSpawnable(const std::string& name)
   {
#if !defined(__APPLE__) && !defined(_WIN32)
      // Syphon/Spout texture sharing is unavailable on Linux; keep nodes registered
      // so existing patches load without error, but hide them from the Add menu and search.
      if (name == "Syphon In" || name == "Syphon Out")
         return false;
#endif
      // Field Graph stays registered (NodeFactory::MakeNode) so a patch saved
      // with one still loads, but is no longer creatable from the spawn menu
      // or search - see the device-catalog simplification's REGISTER_NODE
      // comment in main.cpp.
      return name != "Delete Selected" && name != "Transform Selected" &&
             name != "Extrude Selected" && name != "Group" && name != "Field Graph";
   }


   // GeometryOpNode shares one node type across every mesh operator, and its
   // dropdown lets the operator change after spawn - so unlike most other
   // nodes, its title can't be read from the type name frozen at spawn time.
   // GeometryNode and ShapeNode have the identical problem: each primitive is
   // registered as its own searchable spawn entry (typeName == "Torus Knot",
   // "Tube", ...) so the type name frozen at spawn matches the shape you
   // picked, but the node keeps a live "shape" dropdown that can change it
   // afterwards without ever updating typeName - so the title has to track
   // the live shape field the same way, or picking a new shape leaves the
   // header reading whatever primitive was spawned first.
   std::string NodeTitle(const GraphNode& gn)
   {
      if (auto* opNode = dynamic_cast<GeometryOpNode*>(gn.node.get()))
      {
         const auto& names = GeometryOpNode::OpNames();
         if (opNode->op >= 0 && opNode->op < (int)names.size())
            return DisplayName(names[opNode->op]);
      }
      if (auto* geoNode = dynamic_cast<GeometryNode*>(gn.node.get()))
      {
         const auto& names = GeometryNode::ShapeNames();
         if (geoNode->shape >= 0 && geoNode->shape < (int)names.size())
            return DisplayName(names[geoNode->shape]);
      }
      if (auto* shapeNode = dynamic_cast<ShapeNode*>(gn.node.get()))
      {
         const auto& names = ShapeNode::ShapeNames();
         if (shapeNode->shapeType >= 0 && shapeNode->shapeType < (int)names.size())
            return DisplayName(names[shapeNode->shapeType]);
      }
      return DisplayName(gn.typeName);
   }


   // Undo/redo. Snapshots are whole-graph Patch::Data - the same format a
   // patch file uses - captured just before a mutation rather than diffed
   // after one, so restoring one is exactly LoadPatchFrom's job. Defined near
   // SavePatchTo/LoadPatchFrom, far below; forward-declared here because
   // ModSlider and friends (the widgets that call it) are among the first
   // functions in the file.
   void PushUndoCheckpoint();

   // Timeline-only undo entry: see UndoEntry::arrangeOnly. Declared here
   // because the arrangement panel (far above the undo machinery in file
   // order) is its only caller.
   void PushArrangeUndo();

   // Pushes a timeline-only entry holding `before`, the model as it was
   // before the edit - but only if gArrange's revision has moved past it.
   void PushArrangeUndoSnapshot(const Arrange::Model& before);

   GraphNode* FindNodeByIndex(int index);


   // Field 'graph' domain (build step 10): forward-declared for the same
   // reason as the two above - ApplyPatchData (far below) needs to remap
   // every FieldGraphNode's persisted key->index ownership map to the fresh
   // indices it just assigned, and the definition lives after ApplyPatchData
   // in file order.
   void RemapFieldGraphOwnership(const std::map<int, int>& remap);


   // Offline Render (non-realtime export) - defined near RebuildAudioTopology/
   // StartAudioEngine below, forward-declared here since the node-params UI
   // (among the first drawing code in the file) and the main loop's pump
   // (near the very end) both need to call into it.
   INode* FindHardwareDrivenNode();

   void StartOfflineRenderSession(OutputNode* n, int width, int height, bool isArrange);

   void DrawOfflineRenderProgressWindow();

   void DrawArrangeWavRenderProgressWindow();

   void DrawArrangeClipSettingsChild(float panelW);

   // Defined beside the modulation apply loop below (the other caller), and
   // forward-declared here because the Clip Settings panel above builds its
   // Modulations list from it.
   void ArrangeCollectClipModBindings(const GraphNode& node,
                                      std::vector<std::pair<int, std::string>>& out);

   bool StartAudioEngine(std::string& outError);


   // Disambiguates nodes that share a title (e.g. three "predictive lfo"
   // nodes) so the Modulation Matrix and canvas headers can point at the
   // same node unambiguously. Ranked by `index` (monotonic spawn order,
   // never reused - see GraphNode.h) rather than gNodes' vector position,
   // so the numbering a user has already memorized doesn't shuffle when an
   // unrelated node elsewhere in the graph is deleted or undone.
   //
   // The reference definition is the linear scan below (kept, and used as the
   // fallback for a GraphNode that does not live in gNodes). Every drawn node
   // header asks for its own rank, so calling the scan directly made the
   // canvas O(N^2) NodeTitle() calls a frame - ~15% of the main thread on a
   // 289-node patch. GetNodeInstanceIndex answers from a cache instead.
   int GetNodeInstanceIndexScan(const GraphNode& targetNode, int* outTotalCount)
   {
      const std::string title = NodeTitle(targetNode);
      int rank = 0;
      int total = 0;
      for (const GraphNode& gn : gNodes)
      {
         if (NodeTitle(gn) != title)
            continue;
         ++total;
         if (gn.index <= targetNode.index)
            ++rank;
      }
      if (outTotalCount != nullptr)
         *outTotalCount = total;
      return rank;
   }


   // The one live field a node's title can follow after spawn (see
   // NodeTitle: GeometryOpNode::op, GeometryNode::shape, ShapeNode::shapeType)
   // or nullptr when the title is fixed by typeName for the node's lifetime.
   // Must name the same fields NodeTitle reads, in the same order.
   const int* NodeTitleLiveField(const GraphNode& gn)
   {
      if (auto* opNode = dynamic_cast<GeometryOpNode*>(gn.node.get()))
         return &opNode->op;
      if (auto* geoNode = dynamic_cast<GeometryNode*>(gn.node.get()))
         return &geoNode->shape;
      if (auto* shapeNode = dynamic_cast<ShapeNode*>(gn.node.get()))
         return &shapeNode->shapeType;
      return nullptr;
   }
 // self-test visibility only

   void InvalidateNodeTitleInstances()
   {
      gTitleInstanceDirty = true;
   }


   void RebuildNodeTitleInstances()
   {
      const size_t n = gNodes.size();
      gTitleInstance.assign(n, NodeTitleInstanceEntry{});
      gTitleInstanceLive.clear();
      std::unordered_map<std::string, std::vector<size_t>> byTitle;
      byTitle.reserve(n);
      for (size_t i = 0; i < n; i++)
      {
         const GraphNode& gn = gNodes[i];
         NodeTitleInstanceEntry& e = gTitleInstance[i];
         e.node = gn.node.get();
         e.uid = gn.uid;
         e.index = gn.index;
         e.liveField = NodeTitleLiveField(gn);
         if (e.liveField != nullptr)
         {
            e.liveValue = *e.liveField;
            gTitleInstanceLive.push_back(i);
         }
         byTitle[NodeTitle(gn)].push_back(i);
      }
      std::vector<int> sortedIdx;
      for (auto& kv : byTitle)
      {
         const std::vector<size_t>& slots = kv.second;
         sortedIdx.clear();
         for (size_t s : slots)
            sortedIdx.push_back(gTitleInstance[s].index);
         std::sort(sortedIdx.begin(), sortedIdx.end());
         const int total = (int)slots.size();
         for (size_t s : slots)
         {
            // rank = how many same-titled nodes have index <= this one's -
            // the scan's exact definition, duplicates included.
            NodeTitleInstanceEntry& e = gTitleInstance[s];
            e.rank = (int)(std::upper_bound(sortedIdx.begin(), sortedIdx.end(), e.index) - sortedIdx.begin());
            e.total = total;
         }
      }
      gTitleInstanceData = gNodes.data();
      gTitleInstanceSize = n;
      gTitleInstanceDirty = false;
      ++gTitleInstanceRebuilds;
   }


   bool NodeTitleInstanceSlotValid(size_t slot)
   {
      const NodeTitleInstanceEntry& e = gTitleInstance[slot];
      const GraphNode& gn = gNodes[slot];
      return gn.node.get() == e.node && gn.uid == e.uid && gn.index == e.index &&
             (e.liveField == nullptr || *e.liveField == e.liveValue);
   }


   bool NodeTitleInstancesValid()
   {
      if (gTitleInstanceDirty || gNodes.data() != gTitleInstanceData || gNodes.size() != gTitleInstanceSize)
         return false;
      for (size_t slot : gTitleInstanceLive)
         if (!NodeTitleInstanceSlotValid(slot))
            return false;
      return true;
   }


   int GetNodeInstanceIndex(const GraphNode& targetNode, int* outTotalCount)
   {
      const GraphNode* base = gNodes.data();
      const size_t count = gNodes.size();
      if (count == 0 || &targetNode < base || &targetNode >= base + count)
         return GetNodeInstanceIndexScan(targetNode, outTotalCount);
      const size_t slot = (size_t)(&targetNode - base);
      if (!NodeTitleInstancesValid() || !NodeTitleInstanceSlotValid(slot))
         RebuildNodeTitleInstances();
      const NodeTitleInstanceEntry& e = gTitleInstance[slot];
      if (outTotalCount != nullptr)
         *outTotalCount = e.total;
      return e.rank;
   }


   // NodeTitle() plus a " #N" suffix when another node on the canvas shares
   // the same title - omitted entirely for a node that is currently unique,
   // so the common case (one of each type) reads exactly as it always has.
   std::string NodeTitleWithInstance(const GraphNode& gn)
   {
      int total = 0;
      const int inst = GetNodeInstanceIndex(gn, &total);
      if (total <= 1)
         return NodeTitle(gn);
      return NodeTitle(gn) + " #" + std::to_string(inst);
   }


   const LinkInfo* FindLink(int id)
   {
      for (const LinkInfo& link : gLinks)
      {
         if (link.id == id)
            return &link;
      }
      return nullptr;
   }


   // Dynamic pins, Phase 1 (build step 11, decision 4): toggling a menu entry
   // off while a live cable is attached to the output pin it would remove
   // must be refused rather than silently orphaning the cable. gLinks is
   // rebuilt earlier in this same frame (see CaptureClusterLinks' comment
   // above), so this reads current topology, not last frame's.
   bool FieldOutputPinHasLiveCable(int nodeIndex, int outputIndex)
   {
      for (const LinkInfo& link : gLinks)
      {
         if (!GraphNode::IsOutputPin(link.srcPin))
            continue;
         if (GraphNode::NodeIndexFromPin(link.srcPin) == nodeIndex &&
             GraphNode::OutputIndexFromPin(link.srcPin) == outputIndex)
            return true;
      }
      return false;
   }


   // Dynamic pins, Phase 2b (build step 13, §5.1): symmetric to
   // FieldOutputPinHasLiveCable above, but scans destination (input) pins -
   // needed so a kernel edit that would retire a declared `input` pin (not
   // just a declared `output`) can refuse rather than silently orphan
   // whatever is wired into it.
   bool FieldInputPinHasLiveCable(int nodeIndex, int inputIndex)
   {
      for (const LinkInfo& link : gLinks)
      {
         if (!GraphNode::IsInputPin(link.dstPin))
            continue;
         if (GraphNode::NodeIndexFromPin(link.dstPin) == nodeIndex &&
             GraphNode::InputSlotFromPin(link.dstPin) == inputIndex)
            return true;
      }
      return false;
   }


   // Bridge installed into Field::gLiveCableChecker (see PinTable.h) so
   // Field*Node::Apply() - compiled in separate translation units, and
   // never able to see gLinks (private to this anonymous namespace) - can
   // still ask "does this pin's current compacted slot have a live cable".
   // Taking the address of an internal-linkage function and storing it in
   // an externally-linked function pointer is legal; only the pointer
   // itself needs external linkage.
   bool CheckFieldLiveCableBridge(int nodeIndex, int slot, bool isOutput)
   {
      return isOutput ? FieldOutputPinHasLiveCable(nodeIndex, slot) : FieldInputPinHasLiveCable(nodeIndex, slot);
   }


   void DisconnectLinkById(int id);
 // forward decl - defined below, used by the bridge just after it

   // Sibling bridge to CheckFieldLiveCableBridge, installed into
   // Field::gLiveCableDisconnector - severs every live cable on the given
   // pin at its real node-graph source (same DisconnectLinkById used by
   // interactive cable delete) instead of refusing the reconcile. gLinks is
   // rebuilt from live node pointers every frame (see its own comment), so
   // clearing the pointer here is enough - no separate ed:: bookkeeping
   // needed for the change to stick on the next frame.
   void DisconnectFieldPinBridge(int nodeIndex, int slot, bool isOutput)
   {
      std::vector<int> ids;
      for (const LinkInfo& link : gLinks)
      {
         if (isOutput)
         {
            if (GraphNode::IsOutputPin(link.srcPin) &&
                GraphNode::NodeIndexFromPin(link.srcPin) == nodeIndex &&
                GraphNode::OutputIndexFromPin(link.srcPin) == slot)
               ids.push_back(link.id);
         }
         else
         {
            if (GraphNode::IsInputPin(link.dstPin) &&
                GraphNode::NodeIndexFromPin(link.dstPin) == nodeIndex &&
                GraphNode::InputSlotFromPin(link.dstPin) == slot)
               ids.push_back(link.id);
         }
      }
      for (int id : ids)
         DisconnectLinkById(id);
   }


   bool HeadlessJobActive()
   {
      return gHeadlessJob.mode != Headless::Mode::None;
   }

   bool IsHeadlessProcess()
   {
      return HeadlessJobActive() || getenv("INFINITE_EXITAFTER") != nullptr ||
             getenv("IMAGERESYNTH_SCREENSHOT") != nullptr;
   }


   // Defined next to StartOfflineRenderSession; called from the timeline panel
   // (far above it in file order) and from the main loop's pump (far below).
   bool ArrangeRenderBeginJob(ArrangeRenderJob& job);

   void ArrangeRenderQueueTick();

   void ArrangeRenderCancelActive();

   void ArrangeRenderCancelAll();

   void ArrangeRenderQueuePosition(int& outIndex, int& outTotal);

   bool ArrangeRenderBusy();

   // Test-only reset so INFINITE_AUDIORECOVERYTEST can run more than one
   // scenario in the same process without an earlier scenario's backoff
   // state leaking into the next.
   void ResetAudioRecoveryState()
   {
      gLastAudioRecoveryAttemptMs = -1.0;
      gAudioRecoveryWindowStartMs = -1.0;
      gAudioRecoveryAttemptsInWindow = 0;
   }

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


   // The panel dock is model state (Settings::dockSide, 0 = bottom, 1 = top),
   // mapped onto the shared docked-panel slots (0 = bottom, 3 = top). The
   // side slots (1, 2) are never produced: a timeline's own horizontal axis
   // fights a side dock.
   int ArrangePanelDock() { return gArrange.settings.dockSide == 1 ? 3 : 0; }


   bool FrameClockActive()
   {
      if (gOfflineRender.active || gArrangeWavRender.active)
         return false;
      return !gProjectorWindows.empty() || gCanvasSwapInterval > 0;
   }


   // Main context must be current (true at every caller, as it was for the
   // glfwSwapInterval calls these replace).
   void ApplyCanvasSwapInterval()
   {
      const int want = FrameClockActive() ? 0 : gCanvasSwapInterval;
      if (want == gAppliedCanvasSwapInterval)
         return;
      glfwSwapInterval(want);
      gAppliedCanvasSwapInterval = want;
   }


   void SetCanvasSwapInterval(int interval)
   {
      gCanvasSwapInterval = interval;
      ApplyCanvasSwapInterval();
   }

   void PublishWtTestRect();
}
