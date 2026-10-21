// Part of src/main.cpp, included there in order (one translation unit).
// patch save / load / autosave / recovery, undo

   // ---- saving ----
   // Everything SavePatchTo used to build in place, minus the file write -
   // shared with undo/redo, which snapshots this same in-memory shape rather
   // than round-tripping through disk.
   Patch::Data BuildPatchData()
   {
      Patch::Data data;
      data.settings = CaptureSceneSettings();

      // Reverse lookups built once (ported from upstream): every cable
      // endpoint below is a map lookup instead of a scan of gNodes with a
      // dynamic_cast inside - this used to be O(N^2) and ran on every undo
      // checkpoint and autosave.
      std::unordered_map<const void*, int> addrToIndex;
      addrToIndex.reserve(gNodes.size() * 2);
      for (GraphNode& gn : gNodes)
      {
         addrToIndex.emplace((const void*)gn.node.get(), gn.index);
         // multiple inheritance: IGeometrySource*/IPaletteSource* are other addresses
         if (auto* asGeo = dynamic_cast<IGeometrySource*>(gn.node.get()))
            addrToIndex.emplace((const void*)asGeo, gn.index);
         if (auto* asPal = dynamic_cast<IPaletteSource*>(gn.node.get()))
            addrToIndex.emplace((const void*)asPal, gn.index);
      }
      // modulator address -> (node index, output index); first match wins, like the old scan
      std::unordered_map<const void*, std::pair<int, int>> modAddrToIndex;
      for (GraphNode& src : gNodes)
      {
         const int outputs = std::max(1, src.node->OutputCount());
         for (int o = 0; o < outputs; o++)
            if (IModulator* mod = ModulatorForOutput(src.node.get(), o))
               modAddrToIndex.emplace((const void*)mod, std::make_pair(src.index, o));
      }
      auto indexOf = [&addrToIndex](const void* p) -> int {
         auto it = addrToIndex.find(p);
         return it != addrToIndex.end() ? it->second : -1;
      };
      for (GraphNode& gn : gNodes)
      {
         Patch::NodeRecord rec;
         rec.index = gn.index;
         rec.uid = gn.uid;
         rec.category = gn.category;
         rec.typeName = gn.typeName;
         // The cached live position, not the spawn position: the node has almost
         // certainly been dragged since it was created. Read from the cache
         // rather than the editor, since saving runs outside the editor context.
         rec.x = gn.liveX;
         rec.y = gn.liveY;
         rec.showParams = gn.showParams;
         rec.bypassed = gn.node->bypassed;
         rec.showMiniViewport = gn.showMiniViewport;
         rec.showAdvancedParams = gn.showAdvancedParams;
         rec.showPreview = gn.showPreview;
         rec.colorTag = gn.colorTag;
         Patch::SaveParams(gn.node.get(), rec.params);
         data.nodes.push_back(std::move(rec));

         for (int slot = 0; slot < InputCountFor(gn); slot++)
         {
            if (ImageCable* cable = CableFor(gn, slot))
            {
               if (!cable->IsConnected())
                  continue;
               const int srcIndex = indexOf((const void*)cable->GetSource());
               if (srcIndex >= 0)
                  data.cables.push_back({ gn.index, slot, srcIndex, cable->GetOutputSlot() });
            }
         }
         // Audio/note cables are typed like image cables (a plain
         // IsConnected()/GetSource()), not raw-pointer like geometry, so they
         // mirror the image-cable loop above rather than the pointer-
         // comparison `record` lambda below.
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = gn.node->AudioInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            const int srcIndex = indexOf((const void*)cable->GetSource());
            if (srcIndex >= 0)
               data.audio.push_back({ gn.index, slot, srcIndex, cable->GetOutputSlot() });
         }
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = gn.node->NoteInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            const int srcIndex = indexOf((const void*)cable->GetSource());
            if (srcIndex >= 0)
               data.notes.push_back({ gn.index, slot, srcIndex, cable->GetOutputSlot() });
         }
      }

      // Geometry, camera, light, audio and modulator-input pins, found by
      // comparing against each candidate source rather than stored by index.
      for (GraphNode& gn : gNodes)
      {
         auto record = [&](const void* wanted, int slot)
         {
            if (wanted == nullptr)
               return;
            const int srcIndex = indexOf(wanted);
            if (srcIndex >= 0)
               data.geometry.push_back({ gn.index, slot, srcIndex });
         };

         // Render3DNode's geometry slots are found generically below via
         // GeometryInputSlot() too - only its camera/light pins, which aren't
         // geometry, need special-casing here.
         if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         {
            record(render->camera, Render3DNode::kSlots);
            for (int i = 0; i < Render3DNode::kLightSlots; i++)
               record(render->lights[i], Render3DNode::kSlots + 1 + i);
         }
         for (int slot = 0; slot < kMaxGeometrySlots; slot++)
            if (IGeometrySource** field = gn.node->GeometryInputSlot(slot))
               record(*field, slot);
         if (auto* setColor = dynamic_cast<SetColorNode*>(gn.node.get()))
         {
            // paletteInput is an IPaletteSource*, not an IGeometrySource*, so
            // it needs its own comparison the same way camera/light do above.
            if (setColor->paletteInput != nullptr)
            {
               const int srcIndex = indexOf((const void*)setColor->paletteInput);
               if (srcIndex >= 0)
                  data.geometry.push_back({ gn.index, 2, srcIndex });
            }
         }
         if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(gn.node.get()))
            record(audio->fileSource, 0);
         if (int count = gn.node->ModulatorInputCount())
         {
            for (int slot = 0; slot < count; slot++)
            {
               IModulator* wanted = *gn.node->ModulatorInputSlot(slot);
               if (wanted == nullptr)
                  continue;
               auto it = modAddrToIndex.find((const void*)wanted);
               if (it != modAddrToIndex.end())
                  data.geometry.push_back({ gn.index, slot, it->second.first, it->second.second });
            }
         }
      }

      for (const auto& [key, playback] : GestureRecorder::Instance().Playbacks())
      {
         Patch::GestureRecord g;
         g.dstIndex = key.first;
         g.dstParam = key.second;
         g.speed = playback.speed;
         g.hasRangeOverride = playback.hasRangeOverride;
         g.rangeLo = playback.rangeLo;
         g.rangeHi = playback.rangeHi;
         g.curve = playback.curve;
         for (const GestureRecorder::Sample& smp : playback.samples)
            g.samples.push_back({ smp.value, smp.timeSec, smp.startsNewGrab });
         data.gestures.push_back(std::move(g));
      }
      for (const auto& link : Modulation::Instance().Links())
         data.modulation.push_back({ link.first.first, link.first.second,
                                     link.second.nodeIndex, link.second.outputIndex,
                                     link.second.polarity, link.second.depth, link.second.centre,
                                     link.second.inMin, link.second.inMax,
                                     link.second.outMin, link.second.outMax });
      for (const auto& link : PaletteBinding::Instance().Links())
         data.palette.push_back({ link.first.first, link.first.second,
                                  link.second.nodeIndex, link.second.swatchIndex });
      for (const auto& expr : Modulation::Instance().Expressions())
         data.expressions.push_back({ expr.first.first, expr.first.second, expr.second });
      for (const MidiMap::Mapping& m : MidiMap::All())
      {
         Patch::MidiMapRecord rec;
         rec.dstIndex = m.nodeIndex;
         rec.dstParam = m.paramIndex;
         rec.isNote = m.isNote;
         rec.channel = m.channel;
         rec.number = m.number;
         rec.mode = m.mode;
         rec.soft = m.softTakeover;
         rec.invert = m.invert;
         rec.outMin = m.outMin;
         rec.outMax = m.outMax;
         rec.deviceKey = m.deviceKey;
         rec.paramName = m.paramName;
         rec.smoothMs = m.smoothMs;
         data.midi.push_back(rec);
      }
      // Written in list order: a global may reference the ones above it, so
      // the order is part of the meaning, not just presentation.
      for (const ExprGlobals::Global& g : ExprGlobals::All())
         data.globals.push_back({ g.name, g.expr });
      // Turbo: the transport travels with the patch (and with undo entries).
      data.transport.bpm = Transport::Instance().Tempo();
      data.transport.timeSigNum = Transport::Instance().TimeSigNumerator();
      data.transport.timeSigDen = Transport::Instance().TimeSigDenominator();
      data.transport.key = Transport::Instance().Key();
      data.transport.scale = Transport::Instance().Scale();
      // Turbo 0.43: the arrangement travels with the patch and with undo.
      ArrangeModelToPatchData(gArrange, data);
      data.performance = gPerfElements; // Turbo 0.46
      data.perfLayout = gPerfLayout;
      return data;
   }

   std::atomic<bool> gAutosaveWriting { false }; // an autosave worker is writing

   std::string RecoveryAutosavePath()
   {
      const std::string dir = InfiniteSettingsDirectory();
      return dir.empty() ? std::string("Infinite.autosave.inf") : dir + "/Infinite.autosave.inf";
   }

   std::string SessionMarkerPath()
   {
      const std::string dir = InfiniteSettingsDirectory();
      return dir.empty() ? std::string("Infinite.session-active") : dir + "/Infinite.session-active";
   }

   void DiscardRecoveryAutosave()
   {
      // An autosave still being written on its worker would recreate the
      // file right after this removes it: give it a moment to finish.
      for (int i = 0; i < 200 && gAutosaveWriting.load(); i++)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      std::error_code ec;
      std::filesystem::remove(std::filesystem::u8path(RecoveryAutosavePath()), ec);
      gRecoveryAvailable = false;
   }

   // Ported from upstream: the Predictive nodes' shared, cross-session
   // learning pools live in <settings>/prediction.
   std::string PredictionDir() { return InfiniteSettingsDirectory() + "/prediction"; }
   void SavePredictionProfiles()
   {
      if (PredictiveQuantizeProfile::HasLearnedData())
         PredictiveQuantizeProfile::Save(PredictionDir());
      if (PredictiveVelocityProfile::HasLearnedData())
         PredictiveVelocityProfile::Save(PredictionDir());
      if (PredictiveNotesStyle::HasLearnedData())
         PredictiveNotesStyle::Save(PredictionDir());
      if (PredictiveRhythmStyle::HasLearnedData())
         PredictiveRhythmStyle::Save(PredictionDir());
   }
   void PollPredictionAutosave()
   {
      static double sLast = 0.0;
      const double now = glfwGetTime();
      if (sLast > 0.0 && now - sLast < (double)gAutosaveSeconds)
         return;
      sLast = now;
      SavePredictionProfiles();
   }

   void PollAutosave()
   {
      // Do not depend on gPatchDirty here. Several continuous controls update
      // directly while dragged and intentionally create their undo checkpoint
      // only at gesture end; a crash during that gesture must still recover the
      // live values the user was hearing or seeing.
      if (!gAutosaveEnabled || gNodes.empty())
         return;
      const double now = glfwGetTime();
      if (gLastAutosaveTime > 0.0 && now - gLastAutosaveTime < (double)gAutosaveSeconds)
         return;
      gLastAutosaveTime = now;

      // Turbo: the snapshot is taken here (it reads live node state), the
      // serialisation and disk write run on a worker so a large patch never
      // hitches a frame every autosave period. One write in flight at a time.
      std::atomic<bool>& sWriting = gAutosaveWriting;
      if (sWriting.load())
         return;
      sWriting.store(true);
      std::thread([data = BuildPatchData(), target = RecoveryAutosavePath(), &sWriting]() {
         const std::string temp = target + ".tmp";
         std::string error;
         if (!Patch::Write(temp, data, error))
         {
            RuntimeLog::Write("autosave failed: %s", error.c_str());
            sWriting.store(false);
            return;
         }
         std::error_code ec;
         std::filesystem::rename(std::filesystem::u8path(temp), std::filesystem::u8path(target), ec);
         if (ec)
         {
            ec.clear();
            std::filesystem::remove(std::filesystem::u8path(target), ec);
            ec.clear();
            std::filesystem::rename(std::filesystem::u8path(temp), std::filesystem::u8path(target), ec);
         }
         if (ec)
            RuntimeLog::Write("autosave replace failed: %s", ec.message().c_str());
         sWriting.store(false);
      }).detach();
   }

   bool SavePatchTo(const std::string& path)
   {
      Patch::Data data = BuildPatchData();
      std::string error;
      if (!Patch::Write(path, data, error))
      {
         gPatchStatus = "Save failed: " + error;
         return false;
      }

      gPatchPath = path;
      gPatchDirty = false;
      gPatchStatus = "Saved";
      gWatchedPath = path; // Turbo 0.45: our own write is not an outside change
      gWatchedStamp = PatchFileStamp(path);
      gWatchPendingStamp = 0;
      gReloadBanner = false;
      Patch::NoteRecent(path);
      DiscardRecoveryAutosave();
      gLastAutosaveTime = glfwGetTime();
      RuntimeLog::Write("patch saved: %s", path.c_str());
      return true;
   }

   // Set while ApplyPatchData is repopulating the graph from a snapshot, so
   // the SpawnNode/connect calls it makes don't themselves push new undo
   // checkpoints - that would corrupt the very stack an undo/redo is reading
   // from, and turn opening a 50-node patch into 50 checkpoints.
   bool gSuppressUndoCheckpoints = false;
   // deque: dropping the oldest snapshot at the depth cap is O(1) instead of
   // shifting 200 full Patch::Data copies (ported from upstream).
   std::deque<Patch::Data> gUndoStack;
   std::deque<Patch::Data> gRedoStack;
   const size_t kMaxUndoDepth = 200;

   void NewPatch()
   {
      // Retire rather than destroy outright: NewPatch can run mid-frame (it's
      // the first step of ApplyPatchData, which Undo/Redo call), after this
      // frame's ImGui draw list has already queued AddImage() calls
      // referencing these nodes' GL output textures. Destroying them
      // synchronously here - same hazard RemoveNodeByIndex guards against -
      // would free a GL name pending draw commands still reference, flashing
      // garbage/reused-texture content for one frame right before the node
      // disappears. Actual teardown happens next frame (see
      // gRetiredNodes/gRetiredViewports drain next to glfwPollEvents()).
      for (GraphNode& gn : gNodes)
         gRetiredNodes.push_back(std::move(gn.node));
      while (!gNodeViewports.empty())
         gRetiredViewports.push_back(gNodeViewports.extract(gNodeViewports.begin()));
      gNodes.clear();
      gLinks.clear();
      gModHistory.clear();
      Modulation::Instance().Clear();
      PaletteBinding::Instance().Clear();
      GestureRecorder::Instance().Clear();
      MidiMap::Clear();
      ExprGlobals::Clear();
      gNextIndex = 1;
      // Only for a genuine "start a fresh document" - not when NewPatch is
      // called from inside ApplyPatchData as the first step of restoring a
      // snapshot, which must leave the stacks alone.
      if (!gSuppressUndoCheckpoints)
      {
         // Turbo 0.45: the file name too - an undo used to forget which file
         // was open (title lost its name, Ctrl+S asked for a new one).
         gPatchPath.clear();
         gPatchDirty = false;
         gPatchStatus = "New patch";
         gUndoStack.clear();
         gRedoStack.clear();
         ArrangeResetSession(); // a fresh document starts with an empty timeline
         // A fresh document also resets the transport; a loaded patch sets
         // its own right after this (ApplyPatchData).
         Transport::Instance().SetTempo(120.0f);
         Transport::Instance().SetTimeSignature(4, 4);
         Transport::Instance().SetKey(0);
         Transport::Instance().SetScale(0);
         // Turbo 0.47: drop the old nodes from the audio thread's process list
         // now; ApplyPatchData rebuilds it itself after restoring a snapshot.
         RebuildAudioTopology();
         gPerfElements.clear();
         gPerfSelection.clear();
         gPerfAssigningElemIdx = -1;
         gPerfMidiLearnIdx = -1;
         gPerfMidiRuntimeStates.clear();
         gPerfPendingWrites.clear();
      }
   }

   // Rebuilds the live graph from a snapshot - shared by LoadPatchFrom (from
   // disk) and Undo/Redo (from the in-memory stacks). Callers decide what
   // happens to gPatchPath/gUndoStack/gRedoStack afterwards; a loaded file is
   // a new document boundary, an undo is not.
   void ApplyPatchData(const Patch::Data& data, bool applySceneSettings = false)
   {
      gSuppressUndoCheckpoints = true;
      // Loopers respawned here re-adopt their loop audio by id (Turbo).
      LooperNode::SetRestoringPatch(true);
      NewPatch();

      // Saved indices are remapped rather than reused: they only have to be
      // internally consistent, and reusing them would collide with the running
      // counter and with the editor's own per-id state.
      std::map<int, int> remap;
      // Turbo 0.43: fresh uids start above every saved one, so a node minted
      // here can never take a uid another record is about to restore.
      for (const Patch::NodeRecord& rec : data.nodes)
         if (rec.uid >= gNextNodeUid)
            gNextNodeUid = rec.uid + 1;
      // Turbo 0.45: auto-layout for nodes the file gives no position.
      gAutoLayoutPending.clear();
      int autoRow = 0;
      for (const Patch::NodeRecord& rec : data.nodes)
      {
         // Turbo 0.45: keep the saved index when it is free and ascending
         // (every file Infinite writes), so undo and RPC callers see the same
         // numbers; anything else still gets the next free one.
         if (rec.index >= gNextIndex && rec.index < 100000) // pin ids are index * 1050
            gNextIndex = rec.index;
         const float spawnX = rec.hasPos ? rec.x : 0.0f;
         const float spawnY = rec.hasPos ? rec.y : 260.0f * (float)autoRow;
         GraphNode* spawned = SpawnNode(rec.typeName, rec.category, spawnX, spawnY);
         if (spawned != nullptr && !rec.hasPos)
         {
            gAutoLayoutPending.push_back(spawned->index);
            autoRow++;
         }
         if (spawned == nullptr)
         {
            // A patch naming a node type this build does not have still opens;
            // it just comes back missing that node.
            fprintf(stderr, "patch: unknown node type '%s', skipped\n", rec.typeName.c_str());
            gSkippedNodeTypes.push_back(rec.typeName); // Turbo 0.53: MIT build opening a full-build patch
            continue;
         }
         remap[rec.index] = spawned->index;
         // Turbo 0.43: keep the saved uid unless something already took it
         // (a hand-edited file naming one twice) - a clip then goes offline
         // rather than attaching to the wrong node.
         if (rec.uid != 0 && FindNodeByUid(rec.uid) == nullptr)
            spawned->uid = rec.uid;
         if (spawned->uid >= gNextNodeUid)
            gNextNodeUid = spawned->uid + 1;
         spawned->showParams = rec.showParams;
         spawned->node->bypassed = rec.bypassed;
         spawned->showMiniViewport = rec.showMiniViewport;
         spawned->showAdvancedParams = rec.showAdvancedParams;
         spawned->showPreview = rec.showPreview;
         spawned->colorTag = rec.colorTag;
         spawned->node->BeforeLoadParams(); // Turbo 0.60 (upstream): missing keys = the older behaviour
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

      if (!gAutoLayoutPending.empty())
      {
         gAutoLayoutWait = 3; // let them draw once so their sizes are known
         gAutoLayoutFit = true;
      }
      NoteGraphEditedForLiveIssues(); // Turbo 0.45

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
            source.inMin = m.inMin;
            source.inMax = m.inMax;
            source.outMin = m.outMin;
            source.outMax = m.outMax;
            Modulation::Instance().RestoreLink(dst->index, m.dstParam, source);
         }
      }
      {
         GestureRecorder::PlaybackMap loaded;
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
            for (const Patch::GestureSample& smp : g.samples)
               pb.samples.push_back({ smp.value, smp.timeSec, smp.startsNewGrab });
            pb.recordedMin = pb.recordedMax = pb.samples.front().value;
            for (const GestureRecorder::Sample& smp : pb.samples)
            {
               pb.recordedMin = std::min(pb.recordedMin, smp.value);
               pb.recordedMax = std::max(pb.recordedMax, smp.value);
            }
            loaded[GestureRecorder::Key(dst->index, g.dstParam)] = std::move(pb);
         }
         GestureRecorder::Instance().Restore(std::move(loaded), GesturePlaybackClock());
      }
      for (const Patch::PaletteRecord& c : data.palette)
      {
         GraphNode* dst = resolve(c.dstIndex);
         GraphNode* src = resolve(c.srcIndex);
         if (dst != nullptr && src != nullptr)
            PaletteBinding::Instance().Bind(dst->index, c.dstColor, src->index, c.srcSwatch);
      }
      MidiMap::Clear();
      for (const Patch::MidiMapRecord& rec : data.midi)
      {
         GraphNode* dst = resolve(rec.dstIndex);
         if (dst == nullptr)
            continue;
         MidiMap::Mapping m;
         m.nodeIndex = dst->index;
         m.paramIndex = rec.dstParam;
         m.isNote = rec.isNote;
         m.channel = rec.channel;
         m.number = rec.number;
         m.mode = rec.mode;
         m.softTakeover = rec.soft;
         m.pickedUp = !rec.soft;
         m.invert = rec.invert;
         m.outMin = rec.outMin;
         m.outMax = rec.outMax;
         m.deviceKey = rec.deviceKey;
         m.paramName = rec.paramName;
         m.smoothMs = rec.smoothMs;
         MidiMap::Set(m);
      }
      for (const Patch::ExprRecord& e : data.expressions)
      {
         GraphNode* dst = resolve(e.dstIndex);
         if (dst != nullptr)
            Modulation::Instance().SetExpression(dst->index, e.dstParam, e.text);
      }
      ExprGlobals::All().clear();
      for (const Patch::GlobalRecord& g : data.globals)
         ExprGlobals::All().push_back({ g.name, g.expr, 0.0f, std::string() });

      // Turbo 0.46: Performance Mode controls, re-pointed at the new indices.
      gPerfElements.clear();
      gPerfSelection.clear();
      gPerfAssigningElemIdx = -1;
      gPerfMidiLearnIdx = -1;
      gPerfMidiRuntimeStates.clear();
      for (const Patch::PerfRecord& p : data.performance)
      {
         Patch::PerfRecord mapped = p;
         if (p.dstIndex >= 0)
         {
            GraphNode* dst = resolve(p.dstIndex);
            mapped.dstIndex = dst != nullptr ? dst->index : -1;
         }
         mapped.targets.clear();
         for (const auto& t : p.targets)
            if (GraphNode* dst = t.dstIndex >= 0 ? resolve(t.dstIndex) : nullptr)
               mapped.targets.push_back({ dst->index, t.dstParam, t.boolName });
         mapped.targetsY.clear();
         for (const auto& t : p.targetsY)
            if (GraphNode* dst = t.dstIndex >= 0 ? resolve(t.dstIndex) : nullptr)
               mapped.targetsY.push_back({ dst->index, t.dstParam, t.boolName });
         // Turbo: `targets` holds only the extra destinations. Upstream files
         // repeat the primary there too, which wrote (and stepped) it twice.
         auto stripPrimary = [](std::vector<Patch::PerfTarget>& list, int idx, int param)
         {
            list.erase(std::remove_if(list.begin(), list.end(), [idx, param](const Patch::PerfTarget& t)
                       { return t.dstIndex == idx && t.dstParam == param; }), list.end());
         };
         stripPrimary(mapped.targets, mapped.dstIndex, mapped.dstParam);
         stripPrimary(mapped.targetsY, mapped.dstIndex, mapped.dstParam2);
         gPerfElements.push_back(mapped);
      }
      gPerfLayout = data.perfLayout;
      if (gPerfLayout.cellSize < 40) gPerfLayout.cellSize = 76;
      if (gPerfLayout.pageCount < 1) gPerfLayout.pageCount = 1;
      if (gPerfActivePage >= gPerfLayout.pageCount) gPerfActivePage = gPerfLayout.pageCount - 1;

      // Turbo: transport before the audio rebuild, so anything it reads sees
      // the loaded document's tempo and meter. Only when opening a file:
      // undo/redo leave the live tempo alone, so an undo of a node edit never
      // snaps back a tempo that was changed (or is being driven) since.
      if (applySceneSettings)
      {
         Transport::Instance().SetTempo(data.transport.bpm);
         Transport::Instance().SetTimeSignature(data.transport.timeSigNum, data.transport.timeSigDen);
         Transport::Instance().SetKey(data.transport.key);
         Transport::Instance().SetScale(data.transport.scale);
      }

      // Once, after every node and cable above is wired, not once per audio
      // cable while loading - a per-cable rebuild here could call
      // SetTopology with a half-wired graph, since load order (data.audio is
      // replayed in file order, not source-before-destination order) isn't
      // guaranteed.
      RebuildAudioTopology();

      if (applySceneSettings)
      {
         ApplySceneSettings(data.settings, true);
         gSuppressSettingsDirtyOnce = true;
      }

      // Turbo 0.43: after the nodes, so clip sources resolve by uid.
      ArrangeLoadFromPatchData(data, applySceneSettings);

      LooperNode::SetRestoringPatch(false);
      gSuppressUndoCheckpoints = false;
      // Turbo 0.50: a restored graph keeps its own order; only nodes the
      // user adds afterwards are lifted to the front.
      gBringToFrontQueue.clear();
   }

   bool LoadPatchFrom(const std::string& path)
   {
      gSkippedNodeTypes.clear();
      Patch::Data data;
      std::string error;
      if (!Patch::Read(path, data, error))
      {
         gPatchStatus = "Open failed: " + error;
         return false;
      }

      ApplyPatchData(data, true);
      // Turbo: a Transport Control with "audio on open" starts the engine.
      if (AudioEngine::Instance().SampleRate() <= 0.0)
         for (GraphNode& gn : gNodes)
            if (auto* tc = dynamic_cast<TransportControlNode*>(gn.node.get()); tc != nullptr && tc->audioOnOpen)
            {
               gAudioStartError.clear();
               if (!StartAudioEngine(gAudioStartError))
                  fprintf(stderr, "audio on open: %s\n", gAudioStartError.c_str());
               break;
            }

      // A freshly opened file is a new-document boundary: undoing back into
      // whatever was open before this file is not a thing anyone wants.
      gUndoStack.clear();
      gRedoStack.clear();

      gPatchPath = path;
      gPatchDirty = false;
      gPatchStatus = "Opened";
      if (!gSkippedNodeTypes.empty())
      {
         // Turbo 0.53: a patch made with the full (GPL) build opened in the
         // MIT build comes back without those nodes; say which ones.
         std::string list;
         for (const std::string& t : gSkippedNodeTypes)
            if (list.find(t) == std::string::npos)
               list += (list.empty() ? "" : ", ") + t;
         gPatchStatus = "Opened without " + std::to_string(gSkippedNodeTypes.size()) +
                        " node(s) this build lacks: " + list;
         RuntimeLog::Write("patch: %s", gPatchStatus.c_str());
      }
      gWatchedPath = path; // Turbo 0.45
      gWatchedStamp = PatchFileStamp(path);
      gWatchPendingStamp = 0;
      gReloadBanner = false;
      Patch::NoteRecent(path);
      DiscardRecoveryAutosave();
      gLastAutosaveTime = glfwGetTime();
      RuntimeLog::Write("patch opened: %s", path.c_str());
      gRequestFitView = true;
      return true;
   }

   // Turbo 0.45 (upstream R39): re-reads the open file after an outside
   // change, as one undo step, keeping the file name and the audio device.
   bool ReloadPatchFromDisk()
   {
      Patch::Data data;
      std::string error;
      if (!Patch::Read(gPatchPath, data, error))
      {
         // Probably caught mid-write: try again on the next poll, a few times.
         if (++gWatchReadFailures >= 6)
         {
            gWatchReadFailures = 0;
            gWatchedStamp = PatchFileStamp(gPatchPath);
            gPatchStatus = "Reload failed: " + error;
         }
         return false;
      }
      gWatchReadFailures = 0;
      PushUndoCheckpoint();
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      ApplyPatchData(data, false);
      gSuppressUndoCheckpoints = wasSuppressed;
      gPatchDirty = false;
      gPatchStatus = "Reloaded: the file changed on disk (Ctrl+Z goes back)";
      gWatchedPath = gPatchPath;
      gWatchedStamp = PatchFileStamp(gPatchPath);
      gWatchPendingStamp = 0;
      gReloadBanner = false;
      RuntimeLog::Write("patch reloaded after an outside change: %s", gPatchPath.c_str());
      return true;
   }

   void PollPatchFileWatch()
   {
      if (gPatchPath.empty())
      {
         gReloadBanner = false;
         return;
      }
      if (gPatchPath != gWatchedPath)
      {
         gWatchedPath = gPatchPath;
         gWatchedStamp = PatchFileStamp(gPatchPath);
         gWatchPendingStamp = 0;
         return;
      }
      const double now = glfwGetTime();
      if (now < gWatchNextPoll || gReloadBanner)
         return;
      gWatchNextPoll = now + 0.4;
      const long long stamp = PatchFileStamp(gPatchPath);
      if (stamp == 0 || stamp == gWatchedStamp)
      {
         gWatchPendingStamp = 0;
         return;
      }
      if (stamp != gWatchPendingStamp)
      {
         gWatchPendingStamp = stamp; // wait one more poll: the writer may not be done
         return;
      }
      if (gPatchDirty)
         gReloadBanner = true; // unsaved edits here: ask
      else
         ReloadPatchFromDisk();
   }

   void DrawHandoverBanner()
   {
      if (gHandoverPath.empty())
         return;
      ImGui::SetNextWindowPos(ImVec2(gGraphScreenTL.x + 14.0f, gGraphScreenTL.y + (gReloadBanner ? 110.0f : 14.0f)));
      ImGui::SetNextWindowBgAlpha(0.95f);
      if (ImGui::Begin("##handoverbanner", nullptr,
                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing))
      {
         const std::string name = std::filesystem::u8path(gHandoverPath).filename().u8string();
         ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.25f, 1.0f), "Open %s?", name.c_str());
         ImGui::TextDisabled("This patch has unsaved edits.");
         if (ImGui::Button("Save, then open"))
         {
            const std::string path = gHandoverPath;
            gHandoverPath.clear();
            if (!gPatchPath.empty() && SavePatchTo(gPatchPath))
            {
               LoadPatchFrom(path);
               gRequestFitView = true;
            }
            else
               gHandoverPath = path; // no file yet (Save As first) or save failed
         }
         ImGui::SameLine();
         if (ImGui::Button("Open, discard edits"))
         {
            const std::string path = gHandoverPath;
            gHandoverPath.clear();
            LoadPatchFrom(path);
            gRequestFitView = true;
         }
         ImGui::SameLine();
         if (ImGui::Button("Cancel"))
            gHandoverPath.clear();
      }
      ImGui::End();
   }

   void DrawReloadBanner()
   {
      if (!gReloadBanner)
         return;
      ImGui::SetNextWindowPos(ImVec2(gGraphScreenTL.x + 14.0f, gGraphScreenTL.y + 14.0f));
      ImGui::SetNextWindowBgAlpha(0.95f);
      if (ImGui::Begin("##reloadbanner", nullptr,
                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing))
      {
         ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.25f, 1.0f), "This patch changed on disk.");
         ImGui::TextDisabled("You have unsaved edits here.");
         if (ImGui::Button("Reload from disk"))
            ReloadPatchFromDisk();
         ImGui::SameLine();
         if (ImGui::Button("Keep mine"))
         {
            gWatchedStamp = PatchFileStamp(gPatchPath);
            gWatchPendingStamp = 0;
            gReloadBanner = false;
         }
      }
      ImGui::End();
   }

   // Turbo 0.46: writes the patch-building skill (AISkillContent.h) as
   // <folder>/infinite-turbo-patching/SKILL.md; an empty folder means Claude
   // Code's personal skills folder (%USERPROFILE%\.claude\skills).
   std::string InstallPatchSkill(const std::string& folder)
   {
      std::filesystem::path base;
      if (folder.empty())
      {
         const std::filesystem::path profile = InfiniteEnvPath("USERPROFILE");
         if (profile.empty())
            return "AI skill: USERPROFILE is not set";
         base = profile / ".claude" / "skills";
      }
      else
         base = std::filesystem::u8path(folder);
      const std::filesystem::path dir = base / "infinite-turbo-patching";
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      std::ofstream file(dir / "SKILL.md", std::ios::binary | std::ios::trunc);
      if (!file)
         return "AI skill: could not write " + (dir / "SKILL.md").u8string();
      file << AISkillContent::kPatchSkillMarkdown;
      if (!file.good())
         return "AI skill: write failed";
      return "AI skill saved: " + (dir / "SKILL.md").u8string();
   }

   // Turbo 0.46: the bundled example (assets/examples/superSynthMCP.inf), the
   // patch Infinite-Turbo opens with. Loaded as an untitled document so a
   // Save never overwrites the example; `play` starts audio and the transport.
   bool LoadExamplePatch(bool play)
   {
      const std::filesystem::path exeDir =
         std::filesystem::u8path(Platform::ExecutablePath()).parent_path();
      const std::filesystem::path candidates[] = {
         exeDir / "assets" / "examples" / "superSynthMCP.inf",
         std::filesystem::u8path("assets/examples/superSynthMCP.inf"),
      };
      Patch::Data data;
      std::string error = "example not found";
      bool ok = false;
      std::error_code ec;
      for (const std::filesystem::path& p : candidates)
         if (std::filesystem::exists(p, ec) && Patch::Read(p.u8string(), data, error))
         {
            ok = true;
            break;
         }
      if (!ok)
      {
         RuntimeLog::Write("example patch: %s", error.c_str());
         return false;
      }
      ApplyPatchData(data, false); // keeps this machine's audio device and settings
      Transport::Instance().SetTempo(data.transport.bpm);
      Transport::Instance().SetTimeSignature(data.transport.timeSigNum, data.transport.timeSigDen);
      Transport::Instance().SetKey(data.transport.key);
      Transport::Instance().SetScale(data.transport.scale);
      gUndoStack.clear();
      gRedoStack.clear();
      gPatchPath.clear();
      gPatchDirty = false;
      gPatchStatus = "Example: superSynthMCP (Save As to keep your changes)";
      gRequestFitView = true;
      if (play)
      {
         gAudioAutoStartPending = true; // the engine starts a couple of frames in
         Transport::Instance().SetPlaying(true);
      }
      return true;
   }

   // Turbo 0.59: FILE > Examples. Every .inf in <app>/examples (and the
   // bundled superSynthMCP), by name.
   std::vector<std::pair<std::string, std::string>> ListExamplePatches()
   {
      std::vector<std::pair<std::string, std::string>> out; // menu name, path
      const std::filesystem::path exeDir = std::filesystem::u8path(Platform::ExecutablePath()).parent_path();
      const std::filesystem::path dirs[] = { exeDir / "examples", exeDir / "assets" / "examples",
                                             std::filesystem::u8path("examples") };
      std::error_code ec;
      for (const std::filesystem::path& dir : dirs)
      {
         if (!std::filesystem::is_directory(dir, ec))
            continue;
         for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
         {
            if (!entry.is_regular_file(ec) || !HasExtension(entry.path().u8string(), std::vector<std::string> { "inf" }))
               continue;
            const std::string name = entry.path().stem().u8string();
            if (dir.filename() == "examples" && dir.parent_path().filename() == "assets" && name != "superSynthMCP")
               continue; // assets/examples holds test material besides the startup example
            bool dup = false;
            for (const auto& e : out)
               dup = dup || e.first == name;
            if (!dup)
               out.push_back({ name, entry.path().u8string() });
         }
      }
      std::sort(out.begin(), out.end());
      return out;
   }

   // Opens a patch as an untitled document (examples: Save never overwrites them).
   bool LoadPatchAsUntitled(const std::string& path)
   {
      Patch::Data data;
      std::string error;
      if (!Patch::Read(path, data, error))
      {
         gPatchStatus = "Could not open " + path + ": " + error;
         return false;
      }
      ApplyPatchData(data, false);
      Transport::Instance().SetTempo(data.transport.bpm);
      Transport::Instance().SetTimeSignature(data.transport.timeSigNum, data.transport.timeSigDen);
      Transport::Instance().SetKey(data.transport.key);
      Transport::Instance().SetScale(data.transport.scale);
      gUndoStack.clear();
      gRedoStack.clear();
      gPatchPath.clear();
      gPatchDirty = false;
      gPatchStatus = "Example: " + std::filesystem::u8path(path).stem().u8string() + " (Save As to keep your changes)";
      gRequestFitView = true;
      return true;
   }

   bool RecoverAutosave()
   {
      Patch::Data data;
      std::string error;
      const std::string path = RecoveryAutosavePath();
      if (!Patch::Read(path, data, error))
      {
         gPatchStatus = "Recovery failed: " + error;
         RuntimeLog::Write("recovery failed: %s", error.c_str());
         return false;
      }
      ApplyPatchData(data, true);
      gUndoStack.clear();
      gRedoStack.clear();
      gPatchPath.clear();
      gPatchDirty = true;
      gPatchStatus = "Recovered autosave. Save the project to keep it.";
      gRequestFitView = true;
      DiscardRecoveryAutosave();
      RuntimeLog::Write("autosave recovered");
      return true;
   }

   // Shared by PushUndoCheckpoint (freshly captured) and the node-drag
   // checkpoint (captured earlier, at mouse-down, before the drag moved
   // anything - by the time a drag is detected the live positions have
   // already changed, so that caller can't use BuildPatchData() at the point
   // it decides to push).
   void PushUndoSnapshot(Patch::Data snapshot)
   {
      if (gSuppressUndoCheckpoints)
         return;
      NoteGraphEditedForLiveIssues(); // Turbo 0.45
      gUndoStack.push_back(std::move(snapshot));
      if (gUndoStack.size() > kMaxUndoDepth)
         gUndoStack.pop_front();
      // A fresh action invalidates whatever redo history pointed at a future
      // that no longer follows from the graph's current state.
      gRedoStack.clear();
      gPatchDirty = true;
   }

   // Captures the graph as it is RIGHT NOW, before the caller's mutation runs.
   // Undo restores this; Redo re-applies whatever the mutation was about to do.
   void PushUndoCheckpoint()
   {
      if (gSuppressUndoCheckpoints || gApplyingDiscreteCv)
         return;
      PushUndoSnapshot(BuildPatchData());
   }

   // Node-drag checkpoint state. A drag gesture spans many frames of
   // liveX/liveY already having changed, so the pre-drag snapshot has to be
   // taken speculatively at mouse-down and only actually pushed once real
   // movement is confirmed - otherwise a plain click-to-select would push a
   // checkpoint identical to the current state.
   Patch::Data gDragStartSnapshot;
   bool gDragSnapshotValid = false;
   bool gDragSnapshotPushed = false;

   void Undo()
   {
      if (gUndoStack.empty())
         return;
      gRedoStack.push_back(BuildPatchData());
      Patch::Data prev = std::move(gUndoStack.back());
      gUndoStack.pop_back();
      ApplyPatchData(prev);
      gPatchDirty = true;
      gPatchStatus = "Undo";
   }

   void Redo()
   {
      if (gRedoStack.empty())
         return;
      gUndoStack.push_back(BuildPatchData());
      Patch::Data next = std::move(gRedoStack.back());
      gRedoStack.pop_back();
      ApplyPatchData(next);
      gPatchDirty = true;
      gPatchStatus = "Redo";
   }

   // Dispatches one RemoteControl JSON-RPC request. Runs on the main thread,
   // once per frame per pending command (see RemoteControl::DrainPending's
   // call site, right after glfwPollEvents()) - so every case here is free to
   // touch gNodes/gEditor/etc exactly as the normal UI code does. Returns
   // false with outError set on any failure; RemoteControl wraps that into a
   // JSON-RPC error reply.
   // Turbo 0.45: describe / explain / batch / patch text / modulate / live
   // hints / auto layout (see the file).
#include "core/RpcTools.inl"
   // Turbo 0.46: Performance Mode (upstream's Performance Matrix).
#include "core/PerfPanel.inl"
   // Turbo 0.50: Scenes node (one button drives several params).
#include "nodes/ScenesUi.inl"
   // Turbo 0.46: RPC / MCP tools for Turbo-only nodes and Performance Mode.
#include "core/RpcTurboTools.inl"
#include "core/ClaudeChatPanel.inl"

   // Sets one saved setting by its patch key name (what get_params / explain
   // "settings" list), replaying the right type letter.
   // The saved key ("f radius") for a setting name, case / spaces /
   // underscores ignored; empty if the node has no such setting.
