// Patch load, auto layout, field graph regenerate, undo/redo, copy/paste (moved verbatim from main.cpp).
#include "app/AppShared.h"
#include "core/UndoDescribe.h"
#include "core/Notices.h"

namespace app
{
 // R30, defined with the live-issue state below

   // Node types a patch named that this build does not have, collected by ApplyPatchData and reported once by
   // the open that asked for them (undo and paste go through ApplyPatchData too and stay quiet).
   std::vector<std::string> gSkippedNodeTypes;

   std::string PatchFileName(const std::string& path)
   {
      const size_t slash = path.find_last_of("/\\");
      return slash == std::string::npos ? path : path.substr(slash + 1);
   }

   // Opening failed before anything was replaced: say so, say the open patch is untouched, say what to try.
   void PostOpenFailed(const std::string& path, const std::string& error)
   {
      const std::string name = path.empty() ? std::string("the patch") : PatchFileName(path);
      if (error.find("newer version") != std::string::npos)
         Notices::Post(Notices::Level::Error, "patch.open", name + " was made with a newer Infinite",
                       "Your current patch is untouched. Update Infinite to open this one.");
      else if (error.find("not an Infinite patch") != std::string::npos || error.find("file is empty") != std::string::npos)
         Notices::Post(Notices::Level::Error, "patch.open", name + " isn't a patch Infinite can read",
                       "Your current patch is untouched. If you expected a patch, the file may be damaged - try a backup or the autosave.");
      else
         Notices::Post(Notices::Level::Error, "patch.open", "Couldn't open " + name,
                       error + ". Your current patch is untouched.");
   }

   // Opened, but some nodes could not be created. The rest is intact; saving over the file would drop them.
   void PostSkippedTypes(const std::string& path)
   {
      if (gSkippedNodeTypes.empty())
         return;
      std::string names;
      for (size_t i = 0; i < gSkippedNodeTypes.size() && i < 4; ++i)
         names += (i ? ", " : "") + gSkippedNodeTypes[i];
      if (gSkippedNodeTypes.size() > 4)
         names += ", ...";
      Notices::Post(Notices::Level::Warning, "patch.skipped",
                    "Opened " + PatchFileName(path) + " without " + std::to_string(gSkippedNodeTypes.size()) +
                       (gSkippedNodeTypes.size() == 1 ? " node" : " nodes"),
                    "This version doesn't have: " + names + ". The rest of the patch is intact. Use Save As if you want to keep the original file whole.");
      gSkippedNodeTypes.clear();
   }

   void ApplyPatchData(const Patch::Data& data, std::map<int, int>* outRemap, bool keepIndices)
   {
      NoteGraphEditedForLiveIssues();
      ScopedPerfTimer perfTimer("ApplyPatchData");
      gSuppressUndoCheckpoints = true;
      gSkippedNodeTypes.clear();
      NewPatch();

      // Saved indices are remapped rather than reused: they only have to be
      // internally consistent, and reusing them would collide with the running
      // counter and with the editor's own per-id state.
      // keepIndices (a headless job): the file's own indices are kept instead,
      // because everything the job reports or looks up afterwards (--explain,
      // `id` names, --node, --stems) speaks the file's numbering. Only when they
      // are all usable; PatchSchema::Validate has already rejected the rest.
      if (keepIndices)
      {
         std::set<int> seen;
         for (const Patch::NodeRecord& rec : data.nodes)
            if (rec.index < 1 || rec.index > Patch::kMaxNodeIndex || !seen.insert(rec.index).second)
               keepIndices = false;
      }
      int topIndex = 0;
      std::map<int, int> remap;
      for (const Patch::NodeRecord& rec : data.nodes)
      {
         if (keepIndices)
            gNextIndex = rec.index;
         GraphNode* spawned = SpawnNode(rec.typeName, rec.category, rec.x, rec.y);
         if (spawned == nullptr)
         {
            // A patch naming a node type this build does not have still opens;
            // it just comes back missing that node.
            fprintf(stderr, "patch: unknown node type '%s', skipped\n", rec.typeName.c_str());
            if (std::find(gSkippedNodeTypes.begin(), gSkippedNodeTypes.end(), rec.typeName) == gSkippedNodeTypes.end())
               gSkippedNodeTypes.push_back(rec.typeName);
            continue;
         }
         remap[rec.index] = spawned->index;
         topIndex = std::max(topIndex, spawned->index);
         // SpawnNode already minted a fresh uid; a patch that carries one
         // overrides it, which is what lets a clip's srcUid still resolve
         // after a full undo or a reload. A patch saved before uids existed
         // (rec.uid == 0) keeps the fresh one. gNextNodeUid is clamped past
         // every restored value below, so no later spawn can collide.
         // ...unless something else already answers to it. A patch that mixes
         // uid-bearing and uid-less node lines (a hand edit, a bad merge) can
         // name the same uid twice; FindNodeByUid returns the first, so the
         // second node would shadow it and every clip bound to it would
         // resolve to the wrong node. Keeping the freshly minted uid is the
         // only lossless option - the clip goes offline rather than silently
         // attaching to a different node.
         if (rec.uid != 0 && FindNodeByUid(rec.uid) == nullptr)
         {
            const uint64_t mintedUid = spawned->uid;
            spawned->uid = rec.uid;
            NoteNodeUidChanged(mintedUid, spawned);
         }
         spawned->showParams = rec.showParams;
         spawned->node->bypassed = rec.bypassed;
         spawned->showMiniViewport = rec.showMiniViewport;
         spawned->showAdvancedParams = rec.showAdvancedParams;
         // Build step 15 §6 / trap 5: a FieldGraphNode's `encapsulated`
         // compile-time default (true) is correct for a brand-new node from
         // the node picker, which never goes through LoadParams - but a
         // patch saved before this field existed has no "b encapsulated"
         // line, and ParamVisitor::Bool (Patch::Reader) leaves a field
         // untouched when its key is absent. Forcing false here first means
         // an old patch (no key at all) loads with its children visible on
         // canvas, exactly as they were before this step existed, while a
         // patch that DOES carry the key (any patch saved by this build or
         // later, including in-session Undo/Redo and copy/paste snapshots)
         // still gets overwritten to the saved value by LoadParams below.
         if (auto* fgn = dynamic_cast<FieldGraphNode*>(spawned->node.get()))
            fgn->encapsulated = false;
         Patch::LoadParams(spawned->node.get(), rec.params);
         ReloadDerivedState(spawned->node.get());
         // Phase 4 (selection as an input): rewrites a saved kDeleteSelected/
         // kTransformSelected/kExtrudeSelected `op` integer to its general
         // form + selectionOnly, in place on the live node. Deliberately not
         // in ReloadDerivedState (which CopyParams/paste also call) - once a
         // node is migrated here it stays migrated, so copy/paste of an
         // already-loaded node never needs to re-normalize it, and the
         // round-trip self-test's synthetic param fuzzing (which can drive
         // `op` into the deprecated range on a plain Select node) doesn't
         // mistake this normalization for dropped values.
         if (auto* geomOp = dynamic_cast<GeometryOpNode*>(spawned->node.get()))
            geomOp->MigrateDeprecatedOp();
      }
      if (keepIndices)
         gNextIndex = topIndex + 1;

      // `remap` is fully populated now (every node in `data.nodes` has been
      // spawned) - VisitParams/LoadParams above already parsed each
      // FieldGraphNode's ownershipText into old-index terms (see
      // FieldGraphNode::VisitParams), so this rewrites it to match the fresh
      // indices just assigned, exactly like cable/modulation resolution
      // below does via `resolve`.
      RemapFieldGraphOwnership(remap);

      auto resolve = [&](int savedIndex) -> GraphNode*
      {
         auto it = remap.find(savedIndex);
         return (it == remap.end()) ? nullptr : FindNodeByIndex(it->second);
      };

      for (const Patch::CableRecord& c : data.cables)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst == nullptr || src == nullptr)
            continue;
         if (ImageCable* cable = CableFor(*dst, c.dstSlot))
            cable->Connect(src->node.get(), c.srcOutput);
      }
      for (const Patch::CableRecord& c : data.geometry)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst != nullptr && src != nullptr)
            ConnectGeometrySlot(*dst, c.dstSlot, *src, c.srcOutput);
      }
      for (const Patch::CableRecord& c : data.audio)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst == nullptr || src == nullptr)
            continue;
         if (AudioCable* cable = dst->node->AudioInputSlot(c.dstSlot))
            cable->Connect(src->node.get(), c.srcOutput);
      }
      for (const Patch::CableRecord& c : data.notes)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst == nullptr || src == nullptr)
            continue;
         if (NoteCable* cable = dst->node->NoteInputSlot(c.dstSlot))
            cable->Connect(src->node.get(), c.srcOutput);
      }
      for (const Patch::ModRecord& m : data.modulation)
      {
         GraphNode* dst = resolve(m.dstIndex);
         GraphNode* src = resolve(m.srcIndex);
         if (dst != nullptr && src != nullptr)
         {
            // RestoreLink, not Bind(): the destination node hasn't drawn a
            // frame yet during a load, so Bind()'s FrameParams-based centre
            // capture would have nothing to read and would silently zero it
            // out. The centre saved with the patch is the correct one.
            Modulation::Source source;
            source.nodeIndex = src->index;
            source.outputIndex = m.srcOutput;
            source.polarity = m.polarity;
            source.depth = m.depth;
            source.centre = m.centre;
            // m.hasRange is only true when the file actually had lo/hi
            // tokens (see the "mod" tag parsing in Patch.cpp) - leaving it
            // false here is what makes ResolvedSourceFor derive the range
            // from polarity/depth/centre the first time this destination is
            // drawn, exactly reproducing the pre-lo/hi swing for an old patch.
            source.lo = m.lo;
            source.hi = m.hi;
            source.hasRange = m.hasRange;
            source.enabled = m.enabled;
            source.curve = m.curve;
            Modulation::Instance().RestoreLink(dst->index, m.dstParam, source);
         }
      }
      // Shift-drag/armed recordings saved with the patch (see BuildPatchData
      // above and GestureRecorder.h) - restored the same way Undo/Redo
      // restore theirs (RemapGestures + GestureClockNow), just sourced from
      // data.gestures instead of an UndoEntry's separate snapshot. Undo/Redo
      // still overwrite this right after with their own (equivalent) restore
      // - harmless, since both ultimately come from the same recorder state.
      {
         GestureRecorder::PlaybackMap loadedGestures;
         for (const Patch::GestureRecord& g : data.gestures)
         {
            GraphNode* dst = resolve(g.dstIndex);
            if (dst == nullptr || g.samples.size() < 2)
               continue;
            GestureRecorder::Playback pb;
            pb.speed = g.speed;
            pb.hasRangeOverride = g.hasRangeOverride;
            pb.rangeLo = g.rangeLo;
            pb.rangeHi = g.rangeHi;
            pb.curve = g.curve;
            pb.samples.reserve(g.samples.size());
            for (const Patch::GestureSample& s : g.samples)
               pb.samples.push_back({ s.value, s.timeSec, s.startsNewGrab });
            pb.recordedMin = pb.recordedMax = pb.samples.front().value;
            for (const GestureRecorder::Sample& s : pb.samples)
            {
               pb.recordedMin = std::min(pb.recordedMin, s.value);
               pb.recordedMax = std::max(pb.recordedMax, s.value);
            }
            loadedGestures[GestureRecorder::Key(dst->index, g.dstParam)] = std::move(pb);
         }
         GestureRecorder::Instance().Restore(std::move(loadedGestures), GestureClockNow());
      }
      for (const Patch::PaletteRecord& c : data.palette)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst != nullptr && src != nullptr)
            PaletteBinding::Instance().Bind(dst->index, c.dstColor, src->index, c.srcSwatch);
      }
      for (const Patch::ExprRecord& e : data.expressions)
      {
         GraphNode* dst = resolve(e.dstIndex);
         if (dst != nullptr)
         {
            Modulation::Instance().SetExpression(dst->index, e.dstParam, e.text);
            if (std::abs(e.curve) > 0.0001f)
               Modulation::Instance().SetExpressionCurve(dst->index, e.dstParam, e.curve);
         }
      }
      ExprGlobals::All().clear();
      for (const Patch::GlobalRecord& g : data.globals)
         ExprGlobals::All().push_back({ g.name, g.expr, 0.0f, std::string() });

      gPerfElements.clear();
      gPerfSelection.clear();
      for (const Patch::PerfRecord& p : data.performance)
      {
         Patch::PerfRecord mapped = p;
         if (p.dstIndex >= 0)
         {
            GraphNode* dst = resolve(p.dstIndex);
            mapped.dstIndex = (dst != nullptr) ? dst->index : -1;
         }
         mapped.targets.clear();
         for (const auto& t : p.targets)
         {
            if (t.dstIndex >= 0)
            {
               GraphNode* dst = resolve(t.dstIndex);
               if (dst != nullptr)
                  mapped.targets.push_back({ dst->index, t.dstParam, t.boolName });
            }
         }
         mapped.targetsY.clear();
         for (const auto& t : p.targetsY)
         {
            if (t.dstIndex >= 0)
            {
               GraphNode* dst = resolve(t.dstIndex);
               if (dst != nullptr)
                  mapped.targetsY.push_back({ dst->index, t.dstParam, t.boolName });
            }
         }
         // Populate legacy targets if empty
         if (mapped.targets.empty() && mapped.dstIndex >= 0 && mapped.dstParam >= 0)
            mapped.targets.push_back({ mapped.dstIndex, mapped.dstParam, mapped.boolName });
         if (mapped.targetsY.empty() && mapped.dstIndex >= 0 && mapped.dstParam2 >= 0)
            mapped.targetsY.push_back({ mapped.dstIndex, mapped.dstParam2, "" });

         gPerfElements.push_back(mapped);
      }
      gPerfLayout = data.perfLayout;
      if (gPerfLayout.cellSize < 40) gPerfLayout.cellSize = 76;
      if (gPerfLayout.pageCount < 1) gPerfLayout.pageCount = 1;
      if (gPerfActivePage >= gPerfLayout.pageCount) gPerfActivePage = gPerfLayout.pageCount - 1;

      // A clip whose node didn't survive (an unknown type in this build, or a
      // legacy patch whose saved index no longer resolves) is kept and goes
      // offline rather than being dropped - see RemoveNodeByIndex. Clips point
      // at uids now, so a node deleted and undone back re-attaches on its own.
      for (const GraphNode& gn : gNodes)
         if (gn.uid >= gNextNodeUid)
            gNextNodeUid = gn.uid + 1;
      const uint64_t priorArrangeNextId = gArrange.nextId;
      const uint64_t priorArrangeRevision = gArrange.revision;
      PatchDataToArrangeModel(data, gArrange, [&](int savedIndex) -> uint64_t {
         const GraphNode* src = resolve(savedIndex);
         return src ? src->uid : 0;
      });
      gArrange.revision = priorArrangeRevision + 1;
      // ApplyPatchData is both "open a file" and "restore an undo entry". For
      // the undo case nextId must only ever climb (see ApplyArrangeOnlyEntry);
      // for the file case carrying the previous document's mark forward just
      // starts the new document's ids higher, which costs nothing.
      gArrange.nextId = std::max(gArrange.nextId, priorArrangeNextId);

      // Transport before the rebuild (WP3 debt, closed in WP5b): anything the
      // rebuild reads off the transport must see the loaded document's tempo
      // and meter, not the previous document's. Clip windows themselves are
      // in beats and do not depend on it, but the ordering is the one that
      // cannot go stale when something that does is added.
      Transport::Instance().SetTempo(data.transport.bpm);
      Transport::Instance().SetTimeSignature(data.transport.timeSigNum, data.transport.timeSigDen);
      Transport::Instance().SetKey(data.transport.key);
      Transport::Instance().SetScale(data.transport.scale);
      PublishArrangeLoop();

      // Once, after every node and cable above is wired, not once per audio
      // cable while loading - a per-cable rebuild here could call
      // SetTopology with a half-wired graph, since load order (data.audio is
      // replayed in file order, not source-before-destination order) isn't
      // guaranteed.
      RebuildAudioTopology();

      gViewportPanelOpen = data.viewport.open;
      gViewportPanelDock = std::clamp(data.viewport.dock, 0, 3);
      if (std::isfinite(data.viewport.width) && data.viewport.width >= kViewportPanelMinWidth)
         gViewportPanelWidth = data.viewport.width;
      if (std::isfinite(data.viewport.height) && data.viewport.height >= kViewportPanelMinHeight)
         gViewportPanelHeight = data.viewport.height;
      gViewportPanelNodes.clear();
      for (int savedIdx : data.viewport.nodes)
      {
         if (GraphNode* node = resolve(savedIdx))
            gViewportPanelNodes.push_back(node->index);
      }

      if (outRemap != nullptr)
         *outRemap = remap;

      gSuppressUndoCheckpoints = false;
   }

   constexpr int kAutoLayoutMinFrames = 2;
   // let first-frame sizes settle
   constexpr int kAutoLayoutMaxFrames = 30;
  // then lay out with what we have

   void ScheduleAutoLayout(const Patch::Data& data, const std::map<int, int>& remap)
   {
      gPendingAutoLayout = PendingAutoLayout{};
      if (!PatchLayout::NeedsLayout(data))
         return;
      PendingAutoLayout& p = gPendingAutoLayout;
      p.active = true;
      p.data.nodes = data.nodes;
      p.data.cables = data.cables;
      p.data.geometry = data.geometry;
      p.data.audio = data.audio;
      p.data.notes = data.notes;
      p.remap = remap;
   }


   void RunAutoLayoutTick()
   {
      PendingAutoLayout& p = gPendingAutoLayout;
      if (!p.active)
         return;
      p.framesWaited++;
      if (p.framesWaited < kAutoLayoutMinFrames)
         return;

      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);

      std::map<int, PatchLayout::Size> measured;
      bool allMeasured = true;
      for (const Patch::NodeRecord& rec : p.data.nodes)
      {
         auto m = p.remap.find(rec.index);
         GraphNode* gn = m != p.remap.end() ? FindNodeByIndex(m->second) : nullptr;
         if (gn == nullptr || gn->hiddenFromCanvas)
            continue;
         const ImVec2 s = ed::GetNodeSize(gn->NodeId());
         if (s.x > 1.0f && s.x < 20000.0f && s.y > 1.0f && s.y < 20000.0f)
            measured[rec.index] = { s.x, s.y };
         else
            allMeasured = false;
      }
      if (!allMeasured && p.framesWaited < kAutoLayoutMaxFrames)
      {
         ed::SetCurrentEditor(prevEditor);
         return;
      }

      const std::map<int, PatchLayout::Pos> pos = PatchLayout::Compute(p.data, &measured);
      for (const auto& entry : pos)
      {
         auto m = p.remap.find(entry.first);
         GraphNode* gn = m != p.remap.end() ? FindNodeByIndex(m->second) : nullptr;
         if (gn == nullptr)
            continue;
         gn->spawnX = gn->liveX = entry.second.x;
         gn->spawnY = gn->liveY = entry.second.y;
         ed::SetNodePosition(gn->NodeId(), ImVec2(entry.second.x, entry.second.y));
      }
      ed::SetCurrentEditor(prevEditor);
      gRequestFitView = true;
      p = PendingAutoLayout{};
   }


   bool LoadPatchDataImpl(Patch::Data& data, const std::string& path, bool reload);


   // A template is read from the app bundle but opened as an untitled document: no path, no recents entry, no
   // file watch, so Save asks where to put it and the shipped file can't be overwritten.
   static bool gOpeningTemplate = false;

   bool LoadPatchFromImpl(const std::string& path, bool reload)
   {
      Patch::Data data;
      std::string error;
      if (!Patch::Read(path, data, error))
      {
         gPatchStatus = std::string(T("Open failed: ")) + error;
         PostOpenFailed(path, error);
         return false;
      }
      return LoadPatchDataImpl(data, path, reload);
   }

   void StashPendingKeyed(PendingKeyed& p)
   {
      p.active = (!p.mods.empty() || !p.exprs.empty()) && !HeadlessJobActive();
      gHeadlessProbeAll = p.active; // draw every body each frame until the keys are joined
      gPendingKeyed = std::move(p);
   }


   // path is empty for patch source that never was a file (the live RPC's
   // load_patch_text): applied like a reload, and the open file's path and
   // watch stamp stay as they were.
   // --set <node>.<param>=<value>: static per-shot overrides, written into the patch data the way a file
   // line would be, so validation and the loader treat them like authored values. `node` is an index or an
   // `id` word, `param` a saved key (see --describe). A binding that drives the same control wins every
   // frame, which is worth a warning. Safe to call twice (the second pass replaces the same lines).
   void ApplyHeadlessSets(Patch::Data& data, std::vector<Headless::Issue>* errors, std::vector<Headless::Issue>* warnings)
   {
      // --bpm: the shot's tempo wins over the patch's, so beat-synced modulation lands on the film's grid.
      if (gHeadlessJob.bpm > 0.0)
         data.transport.bpm = (float)gHeadlessJob.bpm;
      auto err = [&](const char* code, const std::string& msg, const std::string& hint, int node)
      {
         if (errors != nullptr)
         {
            Headless::Issue is{ code, msg, 0, node };
            is.hint = hint;
            errors->push_back(is);
         }
      };
      for (const std::string& spec : gHeadlessJob.sets)
      {
         const size_t dot = spec.find('.'), eq = spec.find('=');
         const std::string nodeRef = spec.substr(0, dot);
         const std::string key = spec.substr(dot + 1, eq - dot - 1);
         std::string value = spec.substr(eq + 1);
         Patch::NodeRecord* n = nullptr;
         char* end = nullptr;
         const long idx = std::strtol(nodeRef.c_str(), &end, 10);
         const bool numeric = end != nodeRef.c_str() && *end == '\0';
         for (Patch::NodeRecord& r : data.nodes)
            if ((numeric && r.index == (int)idx) || (!numeric && !r.id.empty() && r.id == nodeRef))
               n = &r;
         if (n == nullptr)
         {
            err("E_BAD_REF", "--set " + spec + ": the patch has no node '" + nodeRef + "'", "use a node index or an `id` word", -1);
            continue;
         }
         const PatchSchema::TypeSchema* ts = SchemaFor(n->typeName);
         char tag = 0;
         for (size_t i = 0; i < n->params.size(); i++)
            if (n->params[i].first.size() > 2 && n->params[i].first.substr(2) == key)
               tag = n->params[i].first[0];
         if (tag == 0 && ts != nullptr)
            for (const PatchSchema::ParamInfo& pi : ts->params)
               if (pi.key == key)
                  tag = pi.kind;
         if (tag == 0)
         {
            err("E_BAD_KEY", "--set " + spec + ": " + n->typeName + " has no saved parameter '" + key + "'",
                "list them with: Infinite --describe \"" + n->typeName + "\"", n->index);
            continue;
         }
         const bool isNum = !value.empty() && (std::strtod(value.c_str(), &end), end != value.c_str() && *end == '\0');
         if (tag == 'f' && !isNum)
         {
            err("E_BAD_VALUE", "--set " + spec + ": '" + key + "' is a number, got '" + value + "'", "", n->index);
            continue;
         }
         if (tag == 'b')
         {
            if (value == "true" || value == "on")
               value = "1";
            else if (value == "false" || value == "off")
               value = "0";
            else if (value != "0" && value != "1")
            {
               err("E_BAD_VALUE", "--set " + spec + ": '" + key + "' is a switch (0/1/true/false), got '" + value + "'", "", n->index);
               continue;
            }
         }
         if (tag == 'i' && !isNum)
            data.hasKeyRefs = true; // a dropdown option name, resolved once a node of the type is drawn
         bool replaced = false;
         for (auto& pr : n->params)
            if (pr.first.size() > 2 && pr.first.substr(2) == key)
            {
               pr.second = value;
               replaced = true;
            }
         if (!replaced)
         {
            n->params.push_back({ std::string(1, tag) + " " + key, value });
            n->paramLines.resize(n->params.size() - 1, 0);
            n->paramLines.push_back(0);
         }
         if (warnings != nullptr)
         {
            auto driven = [&](int dstIndex, int dstParam, const std::string& dstKey)
            {
               if (dstIndex != n->index)
                  return false;
               if (!dstKey.empty())
                  return dstKey == key;
               auto it = gParamJoin.find(n->typeName);
               if (it == gParamJoin.end())
                  return false;
               auto k = it->second.keyOfParam.find(dstParam);
               return k != it->second.keyOfParam.end() && k->second == key;
            };
            bool bound = false;
            for (const Patch::ModRecord& m : data.modulation)
               bound = bound || driven(m.dstIndex, m.dstParam, m.dstKey);
            for (const Patch::ExprRecord& e : data.expressions)
               bound = bound || driven(e.dstIndex, e.dstParam, e.dstKey);
            if (bound)
               warnings->push_back({ "W_OVERRIDDEN_BY_MODULATION",
                                     "--set " + spec + ": a mod or expr binding drives '" + key + "', so it overrides this value every frame",
                                     0, n->index, "remove the binding, or set the value it is centred on" });
         }
      }
   }


   bool LoadPatchDataImpl(Patch::Data& data, const std::string& path, bool reload)
   {
      if (HeadlessJobActive() && (!gHeadlessJob.sets.empty() || gHeadlessJob.bpm > 0.0))
         ApplyHeadlessSets(data, nullptr, nullptr);
      if (data.hasNamedRefs)
      {
         // A hand-written file that names nodes/slots: turn the words into indices first.
         std::vector<Headless::Issue> resolveErrors;
         PatchSchema::Resolve(data, MakeSchemaEnv(false), resolveErrors);
         if (!resolveErrors.empty())
         {
            gPatchStatus = std::string(T("Open failed: line "))  + std::to_string(resolveErrors.front().line) + ": " + resolveErrors.front().message;
            PostOpenFailed(path, "line " + std::to_string(resolveErrors.front().line) + ": " + resolveErrors.front().message);
            return false;
         }
      }

      std::string openNote;
      PendingKeyed pending;
      if (data.hasKeyRefs)
      {
         // Keys and option names need a drawn node of each type (a headless run has probed
         // them). The window app has not: mod/expr lines wait until the nodes have drawn
         // (PollPendingKeyed); dropdown option names cannot be resolved and are skipped.
         std::vector<Headless::Issue> keyErrors;
         PatchSchema::ResolveKeys(data, MakeSchemaEnv(false), keyErrors);
         if (!keyErrors.empty() || data.hasKeyRefs)
         {
            for (const Patch::ModRecord& m : data.modulation)
               if (!m.dstKey.empty())
                  pending.mods.push_back(m);
            for (const Patch::ExprRecord& e : data.expressions)
               if (!e.dstKey.empty())
                  pending.exprs.push_back(e);
            data.modulation.erase(std::remove_if(data.modulation.begin(), data.modulation.end(),
                                                 [](const Patch::ModRecord& m) { return !m.dstKey.empty(); }),
                                  data.modulation.end());
            data.expressions.erase(std::remove_if(data.expressions.begin(), data.expressions.end(),
                                                  [](const Patch::ExprRecord& e) { return !e.dstKey.empty(); }),
                                   data.expressions.end());
            for (Patch::NodeRecord& n : data.nodes)
               n.params.erase(std::remove_if(n.params.begin(), n.params.end(),
                                             [](const std::pair<std::string, std::string>& p)
                                             {
                                                char* end = nullptr;
                                                std::strtod(p.second.c_str(), &end);
                                                return p.first[0] == 'i' && !p.second.empty() && (end == p.second.c_str() || *end != '\0');
                                             }),
                              n.params.end());
            openNote = "Opened; " + std::to_string(pending.mods.size() + pending.exprs.size()) + " binding(s) by control name will attach once the nodes have drawn";
         }
      }

      MovementLog::NoteMark(MovementLog::Mark::PatchLoaded);
      if (reload)
      {
         PushUndoCheckpoint(); // Cmd+Z returns to the graph as it was before the reload
         const std::string keptPath = gPatchPath;
         ApplyPatchData(data, &pending.remap, HeadlessJobActive());
         PostSkippedTypes(path);
         ScheduleAutoLayout(data, pending.remap); // before the stash: it moves `pending`
         StashPendingKeyed(pending);
         gArrangePatchGeneration++;
         if (path.empty())
         {
            gPatchPath = keptPath; // NewPatch cleared it; the text is an edit of that file
            gPatchDirty = true;
            gPatchStatus = openNote.empty() ? T("Patch replaced over RPC") : openNote;
            return true;
         }
         gPatchPath = path;
         gPatchDirty = false;
         gPatchStatus = openNote.empty() ? T("Reloaded (file changed on disk)") : openNote;
         NotePatchFileStamp(path);
         return true;
      }
      ApplyPatchData(data, &pending.remap, HeadlessJobActive());
      PostSkippedTypes(path);
      ScheduleAutoLayout(data, pending.remap); // before the stash: it moves `pending`
      StashPendingKeyed(pending);
      // New document: drop the old one's clip clipboard and selection.
      gArrangePatchGeneration++;

      // Opening a file is a new-document boundary for the routing mode too.
      // ApplyPatchData runs NewPatch with undo checkpoints suppressed, so
      // NewPatch's own fresh-document branch deliberately did not fire.
      gAudioMode = AudioMode::Canvas;

      // A freshly opened file is a new-document boundary: undoing back into
      // whatever was open before this file is not a thing anyone wants.
      gUndoStack.clear();
      gRedoStack.clear();

      gPatchPath = gOpeningTemplate ? std::string() : path;
      gPatchDirty = false;
      gPatchStatus = gOpeningTemplate ? "Opened from a template" : (openNote.empty() ? "Opened" : openNote);
      gRequestFitView = true;
      if (!gOpeningTemplate)
         NotePatchFileStamp(path);
      if (HeadlessJobActive() || gOpeningTemplate)
         return true; // a batch job leaves recents and the real autosave alone
      Patch::NoteRecent(path);
      // The autosave from whatever was open before is no longer relevant
      // now that the user has deliberately loaded something else - see §3.
      DiscardAutosave();
      gLastAutosaveTime = 0.0;
      return true;
   }


   bool LoadPatchFrom(const std::string& path) { return LoadPatchFromImpl(path, false); }

   bool OpenTemplate(const std::string& path)
   {
      gOpeningTemplate = true;
      const bool ok = LoadPatchFromImpl(path, false);
      gOpeningTemplate = false;
      return ok;
   }


   // `Infinite --canonicalize in out`: data-level Read -> Write, so an authored
   // file comes out exactly as a GUI save would write it (numbers, no comments).
   // Second half of --canonicalize: strict check, then the writer.
   void FinishCanonicalize(Patch::Data& data, const Headless::Job& job, Headless::Status& st)
   {
      std::string error;
      const PatchSchema::Env env = MakeSchemaEnv(false);
      std::vector<Headless::Issue> warnings;
      PatchSchema::Validate(data, env, st.errors, warnings);
      if (!job.lenient)
         Headless::PromoteWarnings(warnings, st.errors);
      st.warnings = warnings;
      if (!st.errors.empty())
         return;
      if (!job.keepIds)
         for (Patch::NodeRecord& n : data.nodes)
            n.id.clear();
      if (!Patch::Write(job.out, data, error))
      {
         st.errors.push_back({ "E_LOAD", error, 0, -1 });
         return;
      }
      st.files.push_back(job.out);
      st.extraJson.push_back("\"nodes\":" + std::to_string(data.nodes.size()));
   }


   // `Infinite --canonicalize in out`: data-level Read -> Resolve -> Validate -> Write, so an
   // authored file comes out exactly as a GUI save would write it (numbers, no comments).
   // Returns true when the file names controls or options: those need a drawn node per type,
   // so the job continues in HeadlessTick (gHeadlessPatch holds the name-resolved data).
   bool RunCanonicalize(const Headless::Job& job, Headless::Status& st)
   {
      Patch::Data data;
      std::string error;
      if (!Patch::Read(job.patch, data, error))
      {
         st.errors.push_back({ "E_LOAD", error, 0, -1 });
         return false;
      }
      PatchSchema::Resolve(data, MakeSchemaEnv(false), st.errors);
      if (!st.errors.empty())
         return false;
      if (data.hasKeyRefs)
      {
         gHeadlessPatch = data;
         gHeadlessNeedProbe = true;
         return true;
      }
      FinishCanonicalize(data, job, st);
      return false;
   }


   void NoteGraphEditedForLiveIssues()
   {
      gLiveIssueSerial++;
      // Self-tests apply patches before any ImGui context exists (R496).
      gLiveIssueEditTime = ImGui::GetCurrentContext() != nullptr ? ImGui::GetTime() : 0.0;
   }


   void RefreshLiveIssues()
   {
      if (gLiveIssueDoneSerial == gLiveIssueSerial || ImGui::GetTime() - gLiveIssueEditTime < 0.3)
         return;
      gLiveIssueDoneSerial = gLiveIssueSerial;
      gLiveIssues.clear();

      Patch::Data data = BuildPatchData();
      std::vector<Headless::Issue> errors, warnings;
      const PatchSchema::Env env = MakeSchemaEnv(false);
      PatchSchema::Resolve(data, env, errors);
      if (!errors.empty())
         return; // a graph the resolver rejects is the load path's problem, not a live hint
      PatchSchema::Validate(data, env, errors, warnings);
      for (const Headless::Issue& w : warnings)
         if (w.node >= 0 && (w.code == "W_OPEN_INPUT" || w.code == "W_OUTPUT_EMPTY" || w.code == "W_IMAGE_CYCLE"))
            gLiveIssues[w.node].push_back(w);
   }


   void PushUndoSnapshot(Patch::Data snapshot, const char* label)
   {
      if (gSuppressUndoCheckpoints)
         return;
      // Gestures are captured here rather than passed in by the caller: both
      // callers push *before* the mutation they are checkpointing, and the
      // one that captures its Patch::Data early (the node drag) cannot change
      // a recording in between, so "now" is the pre-mutation state either way.
      gUndoStack.push_back({ std::move(snapshot), GestureRecorder::Instance().Playbacks() });
      if (label != nullptr)
         gUndoStack.back().label = label;
      if (gUndoStack.size() > kMaxUndoDepth)
         gUndoStack.pop_front();
      // A fresh action invalidates whatever redo history pointed at a future
      // that no longer follows from the graph's current state.
      gRedoStack.clear();
      NoteGraphEditedForLiveIssues();
      gPatchDirty = true;
   }


   // Captures the graph as it is RIGHT NOW, before the caller's mutation runs.
   // Undo restores this; Redo re-applies whatever the mutation was about to do.
   void PushUndoCheckpoint()
   {
      if (gSuppressUndoCheckpoints)
         return;
      PushUndoSnapshot(BuildPatchData());
   }


   void PushUndoCheckpoint(const char* label)
   {
      if (gSuppressUndoCheckpoints)
         return;
      PushUndoSnapshot(BuildPatchData(), label);
   }


   // Label of the i-th undo entry from the top (0 = the edit Undo would undo now). A call site that gave no label
   // gets one from comparing this entry's state with the next one (or the live graph, for the top), then keeps it.
   std::string UndoLabelAt(size_t i)
   {
      if (i >= gUndoStack.size())
         return std::string();
      UndoEntry& e = gUndoStack[gUndoStack.size() - 1 - i];
      if (e.label.empty())
      {
         if (e.arrangeOnly)
            return "Edit timeline";
         // The top entry's "after" is the live graph, which keeps changing: derive but do not cache it.
         const bool top = (i == 0);
         const std::string derived =
            top ? UndoDescribe::Change(e.patch, BuildPatchData())
                : (gUndoStack[gUndoStack.size() - i].arrangeOnly ? std::string("Edit")
                                                                 : UndoDescribe::Change(e.patch, gUndoStack[gUndoStack.size() - i].patch));
         if (!top)
            e.label = derived;
         return derived;
      }
      return e.label;
   }


   std::string RedoLabelAt(size_t i)
   {
      if (i >= gRedoStack.size())
         return std::string();
      const UndoEntry& e = gRedoStack[gRedoStack.size() - 1 - i];
      return e.label.empty() ? std::string("Edit") : e.label;
   }


   // Field 'graph' domain (build step 10): for the one piece of per-node state
   // that isn't part of Patch::Data proper but still keys off node index - a
   // FieldGraphNode's persisted key->index ownership map (doc §5.3.1,
   // §5.6.3). Walks gNodes rather than a global list. Called from
   // inside ApplyPatchData itself (shared by Undo, Redo and LoadPatchFrom)
   // right after `remap` is fully built, rather than separately from each
   // caller.
   void RemapFieldGraphOwnership(const std::map<int, int>& remap)
   {
      for (GraphNode& gn : gNodes)
      {
         if (auto* fgn = dynamic_cast<FieldGraphNode*>(gn.node.get()))
         {
            fgn->Ownership().Remap(remap);
            fgn->ownershipText = fgn->Ownership().ToText();
         }
      }
   }


   // Build step 15 follow-up (§5.1/§5.4): resolves the same "first terminal
   // with a texture" node the inline preview dispatch (main.cpp's node-body
   // draw loop) already computes every frame - this is also this
   // FieldGraphNode's single derived boundary output pin's target. Kept as
   // its own small free function rather than a FieldGraphNode member because
   // it needs FindNodeByIndex/gNodes, which FieldGraphNode.h/.cpp
   // deliberately have no access to (same discipline as TerminalIndices()'s
   // own doc comment).
   INode* ResolveFieldGraphBoundaryTerminal(FieldGraphNode* fgn)
   {
      for (int idx : fgn->TerminalIndices())
      {
         GraphNode* term = FindNodeByIndex(idx);
         if (term != nullptr && term->node && term->node->GetOutputTexture() != 0 &&
             term->node->GetOutputWidth() > 0)
            return term->node.get();
      }
      return nullptr;
   }


   // Runs a FieldGraphNode's Regenerate() as exactly one undo step (doc
   // §5.4): one checkpoint pushed up front, every SpawnNode/
   // RemoveNodeByIndex/ConnectNodes call inside Regenerate() suppresses its
   // own via the same gSuppressUndoCheckpoints region every other multi-step
   // mutation in this file uses (see the glTF-drop block above). Must only
   // be called outside the ed::Begin()/ed::End() node-editor pass (trap
   // T14) - see the deferred gFieldGraphPendingRegenerate drain after
   // ed::End() near the bottom of the main loop.
   void RunFieldGraphRegenerate(FieldGraphNode* target)
   {
      if (target == nullptr)
         return;
      PushUndoCheckpoint();
      gSuppressUndoCheckpoints = true;

      // Build step 15 follow-up (§5.4): capture what the boundary output pin
      // resolves to BEFORE this regenerate, so a terminal that disappears
      // (unmounted outright, or simply no longer a terminal because a new
      // connect() now consumes it - §5.1) can be told apart from the pin
      // just continuing to point at the same node it always did. Purely
      // local to this call - never held past it.
      INode* oldBoundaryTarget = ResolveFieldGraphBoundaryTerminal(target);

      // Build step 15: encapsulated (Instrument Mode, the default) mounts
      // through VirtualGraphHost, which hides every mounted child from the
      // canvas; false (step 16's "Unpack to Canvas", or a patch saved
      // before this field existed) mounts through the same MainGraphHost
      // this always used.
      if (target->encapsulated)
      {
         VirtualGraphHost host;
         host.owner = target;
         target->Regenerate(host);
      }
      else
      {
         MainGraphHost host;
         target->Regenerate(host);
      }

      // §5.4: this node's boundary output pin always re-resolves to
      // whichever terminal is now primary (nullptr if none), refreshed here
      // so a cook that lands before the next draw frame already reads the
      // right target - the draw dispatch (main.cpp's node-body loop) does
      // the same call every frame regardless, so this is belt-and-braces,
      // not the only place it happens.
      INode* newBoundaryTarget = ResolveFieldGraphBoundaryTerminal(target);
      target->SetBoundaryOutputTarget(newBoundaryTarget);

      // If the pin's old target is gone (the identity it backed is no
      // longer a terminal at all, whether unmounted or merely consumed by a
      // new internal connect()), any outer cable still plugged into this
      // node's own output pin is now stale and needs detaching - same
      // count-then-DisconnectAllTo shape, and the same "detached N cables"
      // notice vocabulary, MainGraphHost::Unmount already uses for an
      // internal node going away (doc trap 6) - extending that existing
      // accounting/notice path rather than building a second, competing
      // detached-cable mechanism. A single physical pin can only ever back
      // one terminal at a time in
      // this implementation (§5.1's multi-terminal case is not exposed as
      // multiple real output pins here), so any change to what the pin
      // resolves to - not only an outright disappearance - is treated as
      // the pin's identity changing and is handled the same conservative
      // way: detach rather than silently swap the picture under a
      // connected cable.
      int boundaryCablesDetached = 0;
      if (oldBoundaryTarget != nullptr && newBoundaryTarget != oldBoundaryTarget)
      {
         INode* boundarySource = target;
         for (GraphNode& gn : gNodes)
         {
            if (gn.node.get() == boundarySource)
               continue;
            int inputs = InputCountFor(gn);
            for (int slot = 0; slot < inputs; slot++)
            {
               ImageCable* cable = CableFor(gn, slot);
               if (cable && cable->IsConnected() && cable->GetSource() == boundarySource)
                  boundaryCablesDetached++;
            }
            for (int slot = 0; slot < kMaxAudioSlots; slot++)
            {
               AudioCable* cable = gn.node->AudioInputSlot(slot);
               if (cable && cable->IsConnected() && cable->GetSource() == boundarySource)
                  boundaryCablesDetached++;
            }
            for (int slot = 0; slot < kMaxNoteSlots; slot++)
            {
               NoteCable* cable = gn.node->NoteInputSlot(slot);
               if (cable && cable->IsConnected() && cable->GetSource() == boundarySource)
                  boundaryCablesDetached++;
            }
         }
         if (boundaryCablesDetached > 0)
            DisconnectAllTo(boundarySource);
      }
      if (boundaryCablesDetached > 0)
      {
         std::ostringstream oss;
         oss << "detached " << boundaryCablesDetached
             << " outer cable(s) from a boundary pin whose terminal disappeared";
         target->AppendNotice(oss.str());
      }

      gSuppressUndoCheckpoints = false;
      gPatchDirty = true;
   }


   // Build step 16 ("Unpack to Canvas"), §4.1-§4.2. Phase 1: flips
   // `encapsulated` off, computes each mounted child's topological-depth
   // column (Field::ComputeEmitDepths over target->LastPlan().connects) and
   // a provisional per-column row stack - nothing has been drawn at a real
   // position for these children yet (they have only ever been hidden), so
   // there is nothing to measure this frame; phase 2 (RunFieldGraphUnpackPhase2Tick,
   // below) refines row heights from real measured sizes once they're
   // available and spawns the wrapping GroupNode. One PushUndoCheckpoint()
   // covers both phases - gSuppressUndoCheckpoints stays on until phase 2
   // finishes (doc §4.1: "wrapped in exactly one ... pair"). Must only be
   // called outside ed::Begin()/ed::End() (trap T14) - see
   // gFieldGraphPendingUnpack's drain after ed::End(), same site as
   // gFieldGraphPendingRegenerate's.
   void RunFieldGraphUnpackPhase1(FieldGraphNode* target)
   {
      if (target == nullptr)
         return;
      // Precondition already gates the button (DrawFieldGraphParams), but
      // stay honest if this is ever driven directly (§7 assertion 6: a
      // FieldGraphNode with no mounted children is a documented no-op).
      if (!target->encapsulated || target->MountedIndices().empty())
         return;

      PushUndoCheckpoint();
      gSuppressUndoCheckpoints = true;

      target->encapsulated = false;
      // Also clear right now rather than waiting for ApplyModulationAndPalette's
      // per-frame sync (main.cpp's "sync each mounted child's hiddenFromCanvas
      // to encapsulated" loop) to get to it later this same frame - that loop
      // runs before this drain point in frame order, so without this the
      // children would stay hidden for one extra frame before the sync loop
      // catches up on the NEXT frame's pass.
      for (int idx : target->MountedIndices())
      {
         GraphNode* gn = FindNodeByIndex(idx);
         if (gn != nullptr)
            gn->hiddenFromCanvas = false;
      }

      constexpr float kUnpackOriginX = 60.0f;
      constexpr float kUnpackOriginY = 60.0f;
      constexpr float kUnpackDX = 580.0f;      // = FieldGraphNode.cpp's kAutoPlaceDX
      constexpr float kUnpackDXWide = 1080.0f; // = kAutoPlaceDXWide
      constexpr float kUnpackDYMin = 160.0f;   // owner's Δy, kept as a floor
      constexpr float kUnpackRowMargin = 40.0f;

      const Field::GraphPlan& plan = target->LastPlan();
      std::map<std::string, int> depthByKey = Field::ComputeEmitDepths(plan);

      // Group mounted indices by depth, preserving plan.emits order within a
      // column - deterministic, mirrors Regenerate()'s own call-site
      // ordering discipline (doc §3.2).
      std::map<int, std::vector<int>> indicesByDepth;
      std::map<int, int> depthByIndex;
      for (const auto& e : plan.emits)
      {
         int idx = target->Ownership().Get(e.key);
         if (idx < 0 || !target->OwnsMountedIndex(idx))
            continue;
         auto depthIt = depthByKey.find(e.key);
         int depth = depthIt != depthByKey.end() ? depthIt->second : 0;
         indicesByDepth[depth].push_back(idx);
         depthByIndex[idx] = depth;
      }

      // Column x: cumulative, each column's width the widest type mounted in
      // it (mirrors Regenerate()'s per-call-site accumulation, keyed by
      // depth instead of call site - doc §3.3). std::map iterates by
      // ascending depth already.
      std::map<int, float> columnX;
      float nextX = kUnpackOriginX;
      for (const auto& colEntry : indicesByDepth)
      {
         columnX[colEntry.first] = nextX;
         bool colWide = false;
         for (int idx : colEntry.second)
         {
            GraphNode* gn = FindNodeByIndex(idx);
            if (gn != nullptr && IsWideAutoPlaceType(gn->typeName))
               colWide = true;
         }
         nextX += colWide ? kUnpackDXWide : kUnpackDX;
      }

      // Provisional row stacking at the kUnpackDYMin floor (doc §3.3: "frame
      // 1 spawns everything at a provisional position") - phase 2 refines it
      // once real sizes are measurable.
      std::vector<int> members;
      for (const auto& colEntry : indicesByDepth)
      {
         float y = kUnpackOriginY;
         for (int idx : colEntry.second)
         {
            GraphNode* gn = FindNodeByIndex(idx);
            if (gn == nullptr)
               continue;
            gn->spawnX = columnX[colEntry.first];
            gn->spawnY = y;
            gn->liveX = gn->spawnX;
            gn->liveY = gn->spawnY;
            gn->needsPosition = true;
            y += kUnpackDYMin + kUnpackRowMargin;
            members.push_back(idx);
         }
      }

      gFieldGraphUnpackPhase2 = FieldGraphUnpackPhase2State{};
      gFieldGraphUnpackPhase2.active = true;
      gFieldGraphUnpackPhase2.target = target;
      gFieldGraphUnpackPhase2.members = members;
      gFieldGraphUnpackPhase2.depthByIndex = depthByIndex;
      gFieldGraphUnpackPhase2.columnX = columnX;
      gFieldGraphUnpackPhase2.retriesLeft = FieldGraphUnpackPhase2State::kMaxRetries;

      gPatchDirty = true;
   }


   // Build step 16, phase 2 - ticked once per frame (after ed::End(), same
   // site as the phase-1 drain) while gFieldGraphUnpackPhase2.active is
   // true. Waits for every revealed member's ed:: node size to read as a
   // real measurement rather than a freshly-drawn node's sentinel/
   // placeholder size, range-checked the same way INFINITE_SAMPLERDRAGTEST
   // already does (doc §3.3/trap 3) rather than a hardcoded frame-count gate
   // - a literal frameId+1 check does not fit here since a member may have
   // been hidden (and therefore never drawn even once) for an arbitrary
   // number of frames before this tick starts polling it. Once every member
   // validates, or kMaxRetries frames pass without that, finalizes row y
   // (from real measured heights, or the phase-1 floor-only stacking as a
   // fallback), spawns the wrapping GroupNode over the finalized bounding
   // box, and releases the shared undo suppression phase 1 armed.
   void RunFieldGraphUnpackPhase2Tick()
   {
      if (!gFieldGraphUnpackPhase2.active)
         return;

      FieldGraphUnpackPhase2State& st = gFieldGraphUnpackPhase2;

      // ed::GetNodePosition/GetNodeSize need a live editor context; this
      // tick runs after ed::End() has already cleared it for the frame
      // (ed::SetCurrentEditor(nullptr) right after ed::End()) - same
      // save/restore shape FindFreeSpawnPosition already uses.
      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);

      constexpr float kUnpackOriginY = 60.0f;
      constexpr float kUnpackDYMin = 160.0f;
      constexpr float kUnpackRowMargin = 40.0f;
      constexpr float kAudioNodeWidthFallback = 440.0f;
      constexpr float kAudioWideWidthFallback = 960.0f;

      std::map<int, ImVec2> sizeByIndex;
      std::map<int, ImVec2> posByIndex;
      bool allValid = true;
      for (int idx : st.members)
      {
         GraphNode* gn = FindNodeByIndex(idx);
         if (gn == nullptr)
            continue;
         ImVec2 p = ed::GetNodePosition(gn->NodeId());
         ImVec2 s = ed::GetNodeSize(gn->NodeId());
         posByIndex[idx] = p;
         sizeByIndex[idx] = s;
         bool valid = s.x > 1.0f && s.x < 5000.0f && s.y > 1.0f && s.y < 5000.0f;
         if (!valid)
            allValid = false;
      }

      st.retriesLeft--;
      if (!allValid && st.retriesLeft >= 0)
      {
         ed::SetCurrentEditor(prevEditor);
         return; // keep polling next frame
      }

      if (allValid)
      {
         std::map<int, std::vector<int>> byDepth;
         for (int idx : st.members)
            byDepth[st.depthByIndex[idx]].push_back(idx);

         for (auto& colEntry : byDepth)
         {
            float y = kUnpackOriginY;
            for (int idx : colEntry.second)
            {
               GraphNode* gn = FindNodeByIndex(idx);
               if (gn == nullptr)
                  continue;
               const float h = std::max(kUnpackDYMin, sizeByIndex[idx].y);
               gn->spawnY = y;
               gn->liveY = y;
               ed::SetNodePosition(gn->NodeId(), ImVec2(gn->spawnX, y));
               y += h + kUnpackRowMargin;
            }
         }
      }
      // else: retries exhausted before every size validated - fall back to
      // phase 1's kUnpackDYMin-only stacking (already applied to spawnY/the
      // node-editor position), no measured-height refinement (doc §7's
      // stated defensive fallback rather than looping forever).

      // Bounding box over the now-finalized positions - same accumulation
      // shape as the Cmd/Ctrl+G selection-wrap handler (main.cpp's
      // "doGroup" block), sourced from this unpack's members instead of the
      // live selection. Uses a nominal per-type size instead of a possibly-
      // still-sentinel measured one for any member whose size never
      // validated (the fallback-stacking path above), so the group's box
      // isn't sized off garbage.
      bool any = false;
      ImVec2 bmin(0.0f, 0.0f), bmax(0.0f, 0.0f);
      for (int idx : st.members)
      {
         GraphNode* gn = FindNodeByIndex(idx);
         if (gn == nullptr)
            continue;
         ImVec2 p = ed::GetNodePosition(gn->NodeId());
         ImVec2 s = sizeByIndex[idx];
         if (!(s.x > 1.0f && s.x < 5000.0f && s.y > 1.0f && s.y < 5000.0f))
            s = ImVec2(IsWideAutoPlaceType(gn->typeName) ? kAudioWideWidthFallback : kAudioNodeWidthFallback,
                       kUnpackDYMin);
         if (!any)
         {
            bmin = p;
            bmax = ImVec2(p.x + s.x, p.y + s.y);
            any = true;
         }
         else
         {
            bmin.x = std::min(bmin.x, p.x);
            bmin.y = std::min(bmin.y, p.y);
            bmax.x = std::max(bmax.x, p.x + s.x);
            bmax.y = std::max(bmax.y, p.y + s.y);
         }
      }

      if (any)
      {
         // Same kPad/kHeader padding numbers as the selection-wrap handler
         // (doc §4.2 step 6: reuse, don't invent new padding).
         const float kPad = 32.0f;
         const float kHeader = 24.0f;
         const float gx = bmin.x - kPad;
         const float gy = bmin.y - kPad - kHeader;
         const float gw = (bmax.x - bmin.x) + kPad * 2.0f;
         const float gh = (bmax.y - bmin.y) + kPad * 2.0f + kHeader;

         if (GraphNode* ggn = SpawnNode("Group", "Compositing", gx, gy))
         {
            if (auto* grp = dynamic_cast<GroupNode*>(ggn->node.get()))
            {
               // No generic user-assigned display name exists for a
               // FieldGraphNode instance (checked - only GroupNode itself
               // has a rename-in-place `label`; there is no node-title
               // system to borrow from) - short, unique, not pretty, per
               // doc §4.2 step 5.
               grp->label = "Unpacked: " + st.target->Uid().substr(0, 8);
               // Position only - AutoFitGroupToMembers (already runs every
               // frame) takes over sizing from the very next frame, same as
               // the selection-wrap handler already relies on (doc trap 4).
               grp->width = gw;
               grp->height = gh;
               gGroupMembers[grp] = st.target->MountedIndices();
            }
         }
      }

      ed::SetCurrentEditor(prevEditor);

      st.active = false;
      st.target = nullptr;
      gSuppressUndoCheckpoints = false;
      gPatchDirty = true;
   }


   void PerformCopyPaste(const std::set<int>& toCopy)
   {
      if (toCopy.empty()) return;
      std::vector<std::string> clipboard;
      std::vector<INode*> clipboardSources;
      std::vector<int> clipboardOrigIndex;
      std::vector<int> clipboardOrigGroup;
      ClusterClipboard clipboardCluster;

      for (int index : toCopy)
      {
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
            continue;
         clipboard.push_back(gn->typeName);
         clipboardSources.push_back(gn->node.get());
         clipboardOrigIndex.push_back(gn->index);
         clipboardOrigGroup.push_back(IndexOfGroupNode(GroupOwning(gn->index)));
      }
      CaptureClusterLinks(toCopy, clipboardCluster);

      const ImVec2 off =
         ClusterOffset(std::set<int>(clipboardOrigIndex.begin(), clipboardOrigIndex.end()));

      struct PasteItem
      {
         std::string type; std::string category; INode* src; ImVec2 pos;
         int origIndex; int origGroup;
      };
      std::vector<PasteItem> items;
      for (size_t i = 0; i < clipboard.size(); i++)
      {
         for (GraphNode& gn : gNodes)
         {
            if (gn.node.get() == clipboardSources[i])
            {
               items.push_back({ clipboard[i], gn.category, gn.node.get(),
                                 ImVec2(gn.liveX + off.x, gn.liveY + off.y),
                                 clipboardOrigIndex[i], clipboardOrigGroup[i] });
               break;
            }
         }
      }

      PushUndoCheckpoint();
      gSuppressUndoCheckpoints = true;
      std::map<int, int> newIndexByOrig;
      for (const PasteItem& item : items)
      {
         GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y);
         if (copy)
         {
            CopyParams(copy->node.get(), item.src);
            ReloadDerivedState(copy->node.get());
            if (auto* rn = dynamic_cast<RandomNode*>(copy->node.get()))
               rn->seed = RandomNode::NextSeed();
            if (auto* fgn = dynamic_cast<FieldGraphNode*>(copy->node.get()))
            {
               fgn->SetUid(FieldGraphNode::NewUid());
            }
            newIndexByOrig[item.origIndex] = copy->index;
         }
      }
      for (const auto& kv : newIndexByOrig)
      {
         GraphNode* copy = FindNodeByIndex(kv.second);
         if (copy && copy->node)
         {
            if (auto* fgn = dynamic_cast<FieldGraphNode*>(copy->node.get()))
            {
               fgn->Ownership().Remap(newIndexByOrig);
               fgn->ownershipText = fgn->Ownership().ToText();
            }
         }
      }
      for (const PasteItem& item : items)
      {
         if (item.origGroup < 0)
            continue;
         auto groupIt = newIndexByOrig.find(item.origGroup);
         auto memberIt = newIndexByOrig.find(item.origIndex);
         if (groupIt == newIndexByOrig.end() || memberIt == newIndexByOrig.end())
            continue;
         GraphNode* grpGn = FindNodeByIndex(groupIt->second);
         if (grpGn && grpGn->node)
         {
            if (auto* g = dynamic_cast<GroupNode*>(grpGn->node.get()))
               gGroupMembers[g].insert(memberIt->second);
         }
      }
      std::map<int, GraphNode*> newByOrig;
      for (const auto& kv : newIndexByOrig)
      {
         if (GraphNode* gn = FindNodeByIndex(kv.second))
            newByOrig[kv.first] = gn;
      }
      ApplyClusterLinks(newByOrig, clipboardCluster);
      gSuppressUndoCheckpoints = false;
   }


   // Snapshots the arrangement alone, for a gesture that changed nothing but
   // the timeline. Cheap enough to call per gesture (no graph walk, no node
   // serialization) and, more to the point, undoing it cannot disturb the
   // running graph.
   // Pushes `before` (the model as it was before the edit) as one
   // timeline-only entry. Skipped when the revision has not moved: a click,
   // a drag that ended where it started, a popup field left untouched - none
   // of those may leave an undo step that does nothing.
   void PushArrangeUndoSnapshot(const Arrange::Model& before)
   {
      if (gSuppressUndoCheckpoints)
         return;
      if (before.revision == gArrange.revision)
         return;
      UndoEntry e;
      e.arrangeOnly = true;
      e.arrange = before;
      gUndoStack.push_back(std::move(e));
      if (gUndoStack.size() > kMaxUndoDepth)
         gUndoStack.pop_front();
      gRedoStack.clear();
      gPatchDirty = true;
   }


   // Unconditional form: snapshots the model as it is now, for a caller that
   // is about to change it. Kept for callers outside the panel; the panel
   // itself uses ArrangeEdit / ArrangeGestureBegin+End, which only push when
   // something actually changed.
   void PushArrangeUndo()
   {
      if (gSuppressUndoCheckpoints)
         return;
      UndoEntry e;
      e.arrangeOnly = true;
      e.arrange = gArrange;
      gUndoStack.push_back(std::move(e));
      if (gUndoStack.size() > kMaxUndoDepth)
         gUndoStack.pop_front();
      gRedoStack.clear();
      gPatchDirty = true;
   }


   // Swaps gArrange for `e`'s snapshot and hands the current one back for the
   // opposite stack. Shared by Undo and Redo so the two can never disagree
   // about what a timeline-only entry means.
   void ApplyArrangeOnlyEntry(UndoEntry& e)
   {
      Arrange::Model current = gArrange;
      gArrange = e.arrange;
      // nextId is a high-water mark, not part of the snapshot. Restoring the
      // snapshot's value would hand out ids the undone edit already used:
      // duplicate a clip (id 10, nextId 11), undo (nextId back to 10), make a
      // different edit and a *different* clip gets id 10 - exactly the reuse
      // the persisted nextId exists to prevent. Same rule as gNextNodeUid.
      gArrange.nextId = std::max(gArrange.nextId, current.nextId);
      // revision is a change counter, not content: it only ever climbs, so
      // anything keyed on it (the audio rebuild, WP5b) sees
      // an undo as the change it is instead of a revision it already built.
      gArrange.revision = current.revision + 1;
      // Where the panel docks, the display unit and the snap grid are view
      // choices, not edits - undo leaves them (WP5, WP6).
      ArrangeRestoreViewSettings(gArrange, ArrangeKeepViewSettings(current));
      e.arrange = std::move(current);
      PublishArrangeLoop();
      // A clip drag or popup edit that was mid-gesture is now describing a
      // model that no longer exists.
      gArrangeGestureOpen = false;
      gArrangeDrag = ArrangeDragState();
      gArrangeMarkerDragId = 0; // a flag drag's gesture just closed too
   }


   void Undo()
   {
      if (gUndoStack.empty())
         return;
      const std::string what = UndoLabelAt(0);
      if (gUndoStack.back().arrangeOnly)
      {
         UndoEntry prev = std::move(gUndoStack.back());
         gUndoStack.pop_back();
         ApplyArrangeOnlyEntry(prev);
         prev.label = what;
         gRedoStack.push_back(std::move(prev));
         gPatchDirty = true;
         gPatchStatus = "Undo " + what;
         return;
      }
      MovementLog::NoteMark(MovementLog::Mark::Undo);
      gRedoStack.push_back({ BuildPatchData(), GestureRecorder::Instance().Playbacks() });
      gRedoStack.back().label = what;
      UndoEntry prev = std::move(gUndoStack.back());
      gUndoStack.pop_back();
      std::map<int, int> remap;
      const ArrangeViewSettings keepView = ArrangeKeepViewSettings(gArrange); // see ApplyArrangeOnlyEntry
      ApplyPatchData(prev.patch, &remap);
      ArrangeRestoreViewSettings(gArrange, keepView);
      gArrangeGestureOpen = false;
      gArrangeDrag = ArrangeDragState();
      gArrangeMarkerDragId = 0; // a flag drag's gesture just closed too
      // After ApplyPatchData, never before: NewPatch (its first step) clears
      // the recorder, so restoring earlier would just be wiped.
      GestureRecorder::Instance().Restore(RemapGestures(prev.gestures, remap), GestureClockNow());
      gPatchDirty = true;
      gPatchStatus = "Undo " + what;
   }


   void Redo()
   {
      if (gRedoStack.empty())
         return;
      const std::string what = RedoLabelAt(0);
      if (gRedoStack.back().arrangeOnly)
      {
         UndoEntry next = std::move(gRedoStack.back());
         gRedoStack.pop_back();
         ApplyArrangeOnlyEntry(next);
         next.label = what;
         gUndoStack.push_back(std::move(next));
         gPatchDirty = true;
         gPatchStatus = "Redo " + what;
         return;
      }
      MovementLog::NoteMark(MovementLog::Mark::Redo);
      gUndoStack.push_back({ BuildPatchData(), GestureRecorder::Instance().Playbacks() });
      gUndoStack.back().label = what;
      UndoEntry next = std::move(gRedoStack.back());
      gRedoStack.pop_back();
      std::map<int, int> remap;
      const ArrangeViewSettings keepView = ArrangeKeepViewSettings(gArrange);
      ApplyPatchData(next.patch, &remap);
      ArrangeRestoreViewSettings(gArrange, keepView);
      gArrangeGestureOpen = false;
      gArrangeDrag = ArrangeDragState();
      gArrangeMarkerDragId = 0; // a flag drag's gesture just closed too
      GestureRecorder::Instance().Restore(RemapGestures(next.gestures, remap), GestureClockNow());
      gPatchDirty = true;
      gPatchStatus = "Redo " + what;
   }


   void JumpInHistory(int undos, int redos)
   {
      for (int i = 0; i < undos && !gUndoStack.empty(); i++)
         Undo();
      for (int i = 0; i < redos && !gRedoStack.empty(); i++)
         Redo();
      gPatchStatus = "History: jumped " + std::to_string(undos > 0 ? undos : redos) + (undos > 0 ? " back" : " forward");
   }
}
