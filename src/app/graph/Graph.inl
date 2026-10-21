// Part of src/main.cpp, included there in order (one translation unit).
// file dialogs, node lookup, pin counts, cable rules

   // ---- Turbo: non-blocking file dialogs --------------------------------
   // A native dialog used to run on the render thread and block it: the
   // transport, sequencers, video and every output window froze until it
   // closed. Now the dialog runs on its own thread (owned by the editor
   // window, so it opens in front of it even in fullscreen and the editor
   // ignores input meanwhile) and the result is delivered on the main
   // thread at the start of a later frame. One dialog at a time.
   struct PendingFileDialog
   {
      std::thread worker;
      std::shared_ptr<std::atomic<bool>> done;
      std::shared_ptr<std::string> result;
      std::function<void(const std::string&)> onDone;
   };
   std::vector<PendingFileDialog> gPendingFileDialogs;

   bool FileDialogOpen() { return !gPendingFileDialogs.empty(); }

   void StartFileDialog(std::function<std::string()> dialog, std::function<void(const std::string&)> onDone)
   {
      if (!gPendingFileDialogs.empty() || !dialog)
         return;
      PendingFileDialog pending;
      pending.done = std::make_shared<std::atomic<bool>>(false);
      pending.result = std::make_shared<std::string>();
      pending.onDone = std::move(onDone);
      auto done = pending.done;
      auto result = pending.result;
      pending.worker = std::thread([dialog = std::move(dialog), done, result]() {
         *result = dialog();
         done->store(true, std::memory_order_release);
      });
      gPendingFileDialogs.push_back(std::move(pending));
   }

   // Main thread, once per frame after ImGui::NewFrame.
   void PollFileDialogs()
   {
      for (size_t i = 0; i < gPendingFileDialogs.size(); )
      {
         PendingFileDialog& d = gPendingFileDialogs[i];
         if (!d.done->load(std::memory_order_acquire))
         {
            ++i;
            continue;
         }
         if (d.worker.joinable())
            d.worker.join();
         const std::string path = *d.result;
         std::function<void(const std::string&)> cb = std::move(d.onDone);
         gPendingFileDialogs.erase(gPendingFileDialogs.begin() + (ptrdiff_t)i);
         if (!path.empty() && cb)
            cb(path);
      }
   }

   // Node-bound variant: the callback only runs if the node still exists.
   template <typename T>
   void StartNodeFileDialog(T* node, std::function<std::string()> dialog,
                            std::type_identity_t<std::function<void(T*, const std::string&)>> onDone)
   {
      INode* raw = node;
      StartFileDialog(std::move(dialog), [raw, onDone](const std::string& path) {
         for (const GraphNode& gn : gNodes)
            if (gn.node.get() == raw)
            {
               onDone(static_cast<T*>(raw), path);
               return;
            }
      });
   }

   IPaletteSource* PaletteSourceByIndex(int nodeIndex)
   {
      GraphNode* gn = FindNodeByIndex(nodeIndex);
      return gn ? dynamic_cast<IPaletteSource*>(gn->node.get()) : nullptr;
   }

   // How many image inputs a node exposes (drives pin count + link routing).
   int InputCountFor(const GraphNode& gn)
   {
      if (dynamic_cast<LayerStackNode*>(gn.node.get()) != nullptr)
         return LayerStackNode::kSlots;
      if (dynamic_cast<LayoutNode*>(gn.node.get()) != nullptr)
         return LayoutNode::kSlots;
      if (dynamic_cast<SwitcherNode*>(gn.node.get()) != nullptr)
         return SwitcherNode::kSlots;
      // Turbo 0.50: Note Switcher answers all 8 note slots (so cables always
      // load) but shows only its `inputs` count plus any connected pin past it.
      if (auto* ns = dynamic_cast<NoteSwitcherNode*>(gn.node.get()))
         return ns->VisibleSlots();
      // Turbo 0.57: Spatial Mixer grows a pin per connection (+1 empty), up to 12.
      if (auto* sm = dynamic_cast<SpatialMixerNode*>(gn.node.get()))
         return sm->PinCount();
      // Modulator input nodes (Math, Range to Range, Smooth, ...) take modulator
      // cables rather than images, but they are still ordinary input pins as far
      // as the editor is concerned.
      if (int c = gn.node->ModulatorInputCount())
         return c;
      // Generic pins (PointZ family and any node that answers InputPinCount).
      if (int c = gn.node->InputPinCount())
         return c;
      if (dynamic_cast<BlendNode*>(gn.node.get()) != nullptr)
         return 2;
      if (auto* filter = dynamic_cast<FilterNode*>(gn.node.get()))
         return filter->Def().inputs;
      if (dynamic_cast<FitNode*>(gn.node.get()) != nullptr)
         return 1;
      // Turbo 0.60: ported from upstream
      if (dynamic_cast<OpticalFlowNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<TimeMachineNode*>(gn.node.get()) != nullptr)
         return 2; // slot 0 is the image, slot 1 the delay map
      if (dynamic_cast<FluidNode*>(gn.node.get()) != nullptr)
         return 3; // image (the dye), force, obstacles
      if (dynamic_cast<VolumeNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<NdiOutNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<PredictiveColoringNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<RigidBodyNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ProximityLinesNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<SweepNode*>(gn.node.get()) != nullptr)
         return 2;
      if (dynamic_cast<RevolveNode*>(gn.node.get()) != nullptr)
         return 1;
      if (auto* fx = dynamic_cast<AudioEffectNode*>(gn.node.get()))
         if (fx->Def().hasGeometryInput)
            return 2; // audio (slot 0) + shape (slot 1), Shape Resonator
      if (dynamic_cast<FormulaNode*>(gn.node.get()) != nullptr)
         return FormulaNode::kInputs;
      if (dynamic_cast<SendImageNode*>(gn.node.get()) != nullptr ||
          dynamic_cast<SendGeometryNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<SyphonOutNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ProjectionNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ResynthNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<CurvesNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ColorRampNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<AudioColorRampNode*>(gn.node.get()) != nullptr)
         return 2; // img (slot 0) + audio (slot 1)
      if (dynamic_cast<RemoveBgNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<DrawNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ImageAnalyzeNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<PaletteNode*>(gn.node.get()) != nullptr)
         return 1; // the reference image, when it comes from the graph
      if (dynamic_cast<AudioAnalyzeNode*>(gn.node.get()) != nullptr)
         return 1; // one "audio" pin (any audio cable, Audio File included)
      if (dynamic_cast<GeometryNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ModelSourceNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<Text3DNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<NullNode*>(gn.node.get()) != nullptr)
         return 1; // also covers Viewport, which derives from it
      if (dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MappingNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ImageToPointsNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<DepthProjectionNode*>(gn.node.get()) != nullptr)
         return 2; // depth and optional color
      if (dynamic_cast<ClothNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr)
         return JoinGeometryNode::kSlots;
      if (dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr)
         return 1; // an optional point cloud to surface
      if (dynamic_cast<PathNode*>(gn.node.get()) != nullptr)
         return 2; // an optional curve, or geometry to travel around
      if (dynamic_cast<GeometryTableNode*>(gn.node.get()) != nullptr)
         return 1; // geometry to sample
      if (dynamic_cast<OceanNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr)
         return 1 + kMapCount; // geometry, then one pin per material channel
      if (dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr)
         return Render3DNode::kEnvSlot + 1; // geo, camera, lights, env
      if (dynamic_cast<GeometryOpNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr)
         return 2; // geometry, then the displacement texture
      if (dynamic_cast<AudioDisplacementNode*>(gn.node.get()) != nullptr)
         return 2; // geometry, then the audio input
      if (dynamic_cast<SetColorNode*>(gn.node.get()) != nullptr)
         return 3; // geometry, texture, palette
      if (dynamic_cast<InstanceOnPointsNode*>(gn.node.get()) != nullptr)
         return 3; // points, shape, cloud
      if (dynamic_cast<WrapNode*>(gn.node.get()) != nullptr)
         return 2; // source, target
      if (dynamic_cast<DistributePointsOnFacesNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<PointsToVerticesNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<MergeByDistanceNode*>(gn.node.get()) != nullptr)
         return 1;
      // DistributePointsInGridNode has no inputs - falls through to 0 below.
      if (dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr)
         return Switcher3DNode::kSlots;
      if (dynamic_cast<Group3DNode*>(gn.node.get()) != nullptr)
         return Group3DNode::kSlots;
      if (dynamic_cast<DelaunayMeshNode*>(gn.node.get()) != nullptr)
         return 1; // also covers VoronoiCellsNode
      if (dynamic_cast<CurveOpsNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<FeedbackNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<TrailsNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<DatamoshNode*>(gn.node.get()) != nullptr)
         return 2; // image, optional motion source
      if (dynamic_cast<ReactionDiffusionNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<WaveTerrainNode*>(gn.node.get()) != nullptr)
         return 2; // slot 0 is texture image, slot 1 is note
      if (dynamic_cast<ImageSpectralSynthNode*>(gn.node.get()) != nullptr)
         return 2; // slot 0 is texture image, slot 1 is note
      if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr)
         return 2; // slot 0 is the image, slot 1 an optional audio input for recording
      // Audio and note nodes are counted generically, by probing the same
      // AudioInputSlot()/NoteInputSlot() virtuals (INode.h) that connect,
      // disconnect and the topology builder already go through - so adding a
      // node type never needs an entry here. This deliberately replaces the
      // per-type ladder the first six audio nodes used: a missing entry there
      // gives a node zero pins, which reads as "the cable type is broken"
      // rather than as a forgotten registration, and every remaining node in
      // docs/plans/audio/README.md §3 would have had to remember it.
      //
      // Placed after every image/geometry branch above so those keep their
      // own counts (OutputNode has its own count of 2 above since slot 0 is image).
      //
      // Audio and note pins share one slot index space (the connect path at
      // IsInputSlotCompatible tries AudioInputSlot(slot) then
      // NoteInputSlot(slot) on the same index), so this counts their union,
      // not each in turn - a node with a note pin at slot 0 and an audio pin
      // at slot 1 has two pins. Slots are contiguous from 0 by convention, so
      // the first index neither virtual answers ends the count.
      {
         int slots = 0;
         while (gn.node->AudioInputSlot(slots) != nullptr || gn.node->NoteInputSlot(slots) != nullptr)
            slots++;
         if (slots > 0)
            return slots;
      }

      return 0; // sources and modulators have no image inputs
   }

   ImageCable* CableFor(GraphNode& gn, int slot)
   {
      // Generic image pins (PointZ family): no per-type entry needed below.
      if (ImageCable* generic = gn.node->ImageInputSlot(slot))
         return generic;
      // Every other Render3D input (geometry/camera/light) is a raw pointer
      // handled by ConnectGeometrySlot; only the env slot is an ImageCable.
      if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         return (slot == Render3DNode::kEnvSlot) ? &render->envInput : nullptr;
      if (auto* stack = dynamic_cast<LayerStackNode*>(gn.node.get()))
         return (slot >= 0 && slot < LayerStackNode::kSlots) ? &stack->Input(slot) : nullptr;
      if (auto* lay = dynamic_cast<LayoutNode*>(gn.node.get()))
         return (slot >= 0 && slot < LayoutNode::kSlots) ? &lay->Input(slot) : nullptr;
      if (auto* sw = dynamic_cast<SwitcherNode*>(gn.node.get()))
         return (slot >= 0 && slot < SwitcherNode::kSlots) ? &sw->Input(slot) : nullptr;
      if (auto* blend = dynamic_cast<BlendNode*>(gn.node.get()))
         return slot == 0 ? &blend->InputA() : &blend->InputB();
      if (auto* filter = dynamic_cast<FilterNode*>(gn.node.get()))
      {
         if (slot == 0)
            return &filter->Input();
         return (slot == 1 && filter->Def().inputs > 1) ? &filter->Input2() : nullptr;
      }
      if (auto* fit = dynamic_cast<FitNode*>(gn.node.get()))
         return slot == 0 ? &fit->Input() : nullptr;
      if (auto* flow = dynamic_cast<OpticalFlowNode*>(gn.node.get()))
         return slot == 0 ? &flow->Input() : nullptr;
      if (auto* tm = dynamic_cast<TimeMachineNode*>(gn.node.get()))
         return slot == 0 ? &tm->Input() : (slot == 1 ? &tm->DelayMap() : nullptr);
      if (auto* fluid = dynamic_cast<FluidNode*>(gn.node.get()))
         return slot == 0 ? &fluid->Input() : (slot == 1 ? &fluid->Force() : (slot == 2 ? &fluid->Obstacles() : nullptr));
      if (auto* vol = dynamic_cast<VolumeNode*>(gn.node.get()))
         return slot == 0 ? &vol->Input() : nullptr;
      if (auto* ndiOut = dynamic_cast<NdiOutNode*>(gn.node.get()))
         return slot == 0 ? &ndiOut->Input() : nullptr;
      if (auto* pc = dynamic_cast<PredictiveColoringNode*>(gn.node.get()))
         return slot == 0 ? &pc->Input() : nullptr;
      if (auto* formula = dynamic_cast<FormulaNode*>(gn.node.get()))
         return (slot >= 0 && slot < FormulaNode::kInputs) ? &formula->Input(slot) : nullptr;
      if (auto* sendImage = dynamic_cast<SendImageNode*>(gn.node.get()))
         return slot == 0 ? &sendImage->Input() : nullptr;
      if (auto* resynth = dynamic_cast<ResynthNode*>(gn.node.get()))
         return slot == 0 ? &resynth->Input() : nullptr;
      if (auto* i2p = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
         return slot == 0 ? &i2p->Input() : nullptr;
      if (auto* dp = dynamic_cast<DepthProjectionNode*>(gn.node.get()))
         return (slot == 0) ? &dp->DepthInput() : ((slot == 1) ? &dp->ColorInput() : nullptr);
      if (auto* curves = dynamic_cast<CurvesNode*>(gn.node.get()))
         return slot == 0 ? &curves->Input() : nullptr;
      if (auto* cramp = dynamic_cast<ColorRampNode*>(gn.node.get()))
         return slot == 0 ? &cramp->Input() : nullptr;
      if (auto* acr = dynamic_cast<AudioColorRampNode*>(gn.node.get()))
         return slot == 0 ? &acr->Input() : nullptr;
      if (auto* rbg = dynamic_cast<RemoveBgNode*>(gn.node.get()))
         return slot == 0 ? &rbg->Input() : nullptr;
      if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
         return slot == 0 ? &draw->Input() : nullptr;
      if (auto* an = dynamic_cast<ImageAnalyzeNode*>(gn.node.get()))
         return slot == 0 ? &an->Input() : nullptr;
      if (auto* pal = dynamic_cast<PaletteNode*>(gn.node.get()))
         return slot == 0 ? &pal->Input() : nullptr;
      if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
         return slot == 0 ? &model->TextureInput() : nullptr;
      if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
         return slot == 0 ? &t3d->TextureInput() : nullptr;
      if (auto* nul = dynamic_cast<NullNode*>(gn.node.get()))
         return slot == 0 ? &nul->Input() : nullptr;
      if (auto* ocean = dynamic_cast<OceanNode*>(gn.node.get()))
         return slot == 0 ? &ocean->TextureInput() : nullptr;
      // Slot 0 is a geometry pin, wired by pointer, not an image cable; the
      // rest are the material channels in MaterialMap order.
      if (auto* mat = dynamic_cast<MaterialNode*>(gn.node.get()))
         return (slot >= 1 && slot <= kMapCount) ? &mat->MapInput(slot - 1) : nullptr;
      // Slot 0 is the geometry pin, wired by pointer in ConnectGeometrySlot;
      // slot 1 is the height/vector displacement texture.
      if (auto* disp = dynamic_cast<DisplacementNode*>(gn.node.get()))
         return slot == 1 ? &disp->TextureInput() : nullptr;
      // Slot 0 is the geometry pin, wired by pointer in ConnectGeometrySlot;
      // slot 1 is the colour source texture; slot 2 (palette) is a raw
      // IPaletteSource* pointer, also wired in ConnectGeometrySlot.
      if (auto* setColor = dynamic_cast<SetColorNode*>(gn.node.get()))
         return slot == 1 ? &setColor->TextureInput() : nullptr;
      if (auto* geo = dynamic_cast<GeometryNode*>(gn.node.get()))
         return slot == 0 ? &geo->TextureInput() : nullptr;
      if (auto* fb = dynamic_cast<FeedbackNode*>(gn.node.get()))
         return slot == 0 ? &fb->Input() : nullptr;
      if (auto* trails = dynamic_cast<TrailsNode*>(gn.node.get()))
         return slot == 0 ? &trails->Input() : nullptr;
      if (auto* dm = dynamic_cast<DatamoshNode*>(gn.node.get()))
         return slot == 0 ? &dm->Input() : (slot == 1 ? &dm->MotionInput() : nullptr);
      if (auto* rd = dynamic_cast<ReactionDiffusionNode*>(gn.node.get()))
         return slot == 0 ? &rd->Input() : nullptr;
      if (auto* wt = dynamic_cast<WaveTerrainNode*>(gn.node.get()))
         return slot == 0 ? &wt->TextureInput() : nullptr;
      if (auto* spec = dynamic_cast<ImageSpectralSynthNode*>(gn.node.get()))
         return slot == 0 ? &spec->TextureInput() : nullptr;
      if (auto* out = dynamic_cast<OutputNode*>(gn.node.get()))
         return slot == 0 ? &out->Input() : nullptr;
      if (auto* syphonOut = dynamic_cast<SyphonOutNode*>(gn.node.get()))
         return slot == 0 ? &syphonOut->Input() : nullptr;
      if (auto* proj = dynamic_cast<ProjectionNode*>(gn.node.get()))
         return slot == 0 ? &proj->Input() : nullptr;
      return nullptr;
   }

   // Sibling of CableFor for the audio/note-typed pins - just forwards to the
   // generic AudioInputSlot()/NoteInputSlot() virtuals (see INode.h) rather
   // than a per-node dynamic_cast chain, since every audio/note-consuming
   // node already has to implement that virtual for DisconnectAllTo et al.
   // to find it generically.
   AudioCable* AudioCableFor(GraphNode& gn, int slot)
   {
      return gn.node->AudioInputSlot(slot);
   }

   // No P2 node has a note pin yet - this exists so P3a's note nodes land on
   // scaffolding that's already wired through every dispatch site.
   NoteCable* NoteCableFor(GraphNode& gn, int slot)
   {
      return gn.node->NoteInputSlot(slot);
   }

   // Upper bound on how many geometry-input slots any single node exposes via
   // GeometryInputSlot() - the widest today is Group3DNode at 8. Generic loops
   // over "every geometry slot this node might have" stop here rather than
   // walking off into unrelated pins.
   const int kMaxGeometrySlots = 8;

   // Upper bound on how many audio/note-input slots any single node exposes.
   // Turbo: the widest is Super Mixer, at SuperMixerNode::kChannels (16).
   const int kMaxAudioSlots = 16;
   // Every note-consuming node before AudioPluginNode carried its one note
   // pin at unified slot 0 (Sampler, Envelope, ...), which is why this was 1
   // and the topology builder's wiring pass (RebuildAudioTopology) used to
   // just call NoteInputSlot(0) directly rather than loop. AudioPluginNode's
   // note pin lives at slot 1 instead - so audio stays at slot 0 and existing
   // patches keep loading unchanged - which needed this bumped to 2 and that
   // wiring pass turned into a real loop; see its comment. NoteMergeNode's
   // 4-way fan-in (mirroring NoteRouterNode's 4-way fan-out) needed this
   // bumped again to 4. Turbo 0.50: Note Switcher has 8 note inputs.
   const int kMaxNoteSlots = 8;

   // Most IAudioSource nodes have one audio-only output. Video Player is a
   // mixed media source: pin 0 is its image and pin 1 is its soundtrack.
   // Keeping this distinction here prevents its image cable from being
   // rejected as audio while preserving generic audio topology wiring.
   bool IsAudioOutput(INode* node, int outputIndex)
   {
      if (node == nullptr || dynamic_cast<IAudioSource*>(node) == nullptr)
         return false;
      if (dynamic_cast<VideoSourceNode*>(node) != nullptr || dynamic_cast<VmpcNode*>(node) != nullptr)
         return outputIndex == 1;
      if (dynamic_cast<TransportControlNode*>(node) != nullptr)
         return outputIndex == 4; // 0..3 are modulators, 4 is the metronome
      if (dynamic_cast<ClipMatrixNode*>(node) != nullptr)
         return outputIndex == 1;
      if (dynamic_cast<ClipMatrixOutNode*>(node) != nullptr)
         return (outputIndex & 1) == 1; // video, audio per row
      if (dynamic_cast<AudioPluginNode*>(node) != nullptr)
         return outputIndex == 0; // Turbo 0.59: output 1 is its "midi" pin
      return true;
   }

   // Whether a node's output `outputIndex` carries note events. Every
   // INoteSource's outputs do, except the plugin node's: its audio stays on
   // output 0 and the plugin's own MIDI leaves through output 1 ("midi"),
   // which only exists for a plugin that declares a MIDI output.
   bool IsNoteOutput(INode* node, int outputIndex)
   {
      if (node == nullptr || dynamic_cast<INoteSource*>(node) == nullptr)
         return false;
      if (auto* plugin = dynamic_cast<AudioPluginNode*>(node))
         return outputIndex == 1 && plugin->ProducesMidi();
      return true;
   }

   // Defined near DisconnectAllTo/RemoveNodeByIndex, below; forward-declared
   // here since DisconnectLinkById (earlier in the file) needs to call it too.
   void RebuildAudioTopology();
   // Set around a batch of removals (a multi-node delete) so the audio graph
   // is rebuilt once after the batch instead of once per node.
   bool gDeferAudioRebuild = false;

   // Which geometry-ish pin a node exposes at a given slot, and how to set it.
   // Geometry, camera and light connections are raw pointers rather than
   // ImageCables, so saving and loading them needs this one place that knows
   // the mapping - the same knowledge the link-drawing and connect paths use.
   void ConnectGeometrySlot(GraphNode& dst, int slot, GraphNode& src, int srcOutput = 0)
   {
      auto* geo = dynamic_cast<IGeometrySource*>(src.node.get());
      auto* cam = dynamic_cast<CameraNode*>(src.node.get());
      auto* light = dynamic_cast<LightNode*>(src.node.get());

      if (auto* render = dynamic_cast<Render3DNode*>(dst.node.get()))
      {
         if (slot < Render3DNode::kSlots)
            render->geometry[slot] = geo;
         else if (slot == Render3DNode::kSlots)
            render->camera = cam;
         else if (slot - Render3DNode::kSlots - 1 < Render3DNode::kLightSlots)
            render->lights[slot - Render3DNode::kSlots - 1] = light;
         return;
      }
      if (IGeometrySource** field = dst.node->GeometryInputSlot(slot))
      {
         *field = geo;
         return;
      }
      if (auto* setColor = dynamic_cast<SetColorNode*>(dst.node.get()))
      {
         if (slot == 2)
            setColor->paletteInput = dynamic_cast<IPaletteSource*>(src.node.get());
         return;
      }
      if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(dst.node.get()))
      {
         audio->fileSource = dynamic_cast<AudioFileNode*>(src.node.get());
         return;
      }
      // Every modulator-input pin (Math, Range to Range, Smoothing, Send CV,
      // ...), not just Math's: the patch stores them in the same `geo`
      // records, with the source's modulator output as the 4th token.
      if (IModulator** field = dst.node->ModulatorInputSlot(slot))
         *field = ModulatorForOutput(src.node.get(), srcOutput);
   }

   // Whether an output pin carrying the given src-side facts may connect into
   // `dstNode`'s input `slot`. Mirrors the geometry/camera/light/audio/render
   // input-pin rules every drag validation and drop-to-spawn suggestion needs,
   // in one place, so the two stay in sync. Param pins (IsParamPin) and colour
   // pins (IsColorPin) are handled by the caller before this is ever reached -
   // this only covers ordinary input pins.
   bool IsInputSlotCompatible(GraphNode* dstNode, int slot,
                               bool srcIsModulator, IPaletteSource* srcPalette,
                               IGeometrySource* srcGeometry, CameraNode* srcCamera,
                               LightNode* srcLight, AudioFileNode* srcAudioFile,
                               bool srcIsEnvironment, bool srcIsAudioNode, bool srcIsNoteSource)
   {
      if (dstNode == nullptr || dstNode->node == nullptr)
         return false;

      auto* dstAudio = dynamic_cast<AudioAnalyzeNode*>(dstNode->node.get());
      auto* dstRender = dynamic_cast<Render3DNode*>(dstNode->node.get());
      auto* dstMaterial = dynamic_cast<MaterialNode*>(dstNode->node.get());
      auto* dstDisplacement = dynamic_cast<DisplacementNode*>(dstNode->node.get());
      auto* dstSetColor = dynamic_cast<SetColorNode*>(dstNode->node.get());
      const bool dstWantsImage = dynamic_cast<ImageAnalyzeNode*>(dstNode->node.get()) != nullptr;

      const bool srcIsImage = !srcIsModulator && srcPalette == nullptr &&
                              srcGeometry == nullptr && srcCamera == nullptr &&
                              srcLight == nullptr && !srcIsAudioNode && !srcIsNoteSource;

      // Audio/note pins are found generically via AudioInputSlot()/NoteInputSlot().
      // AudioFileNode is a real IAudioSource (its samples flow through the
      // DSP graph like any other audio node - see AnalyzeNodes.h), so
      // srcIsAudioNode alone covers it here; the special-case
      // "srcAudioFile != nullptr" carve-out this branch used to also accept
      // was scoped too broadly (every AudioInputSlot(), not just
      // OutputNode's recording slot) and let a cable land on Audio
      // Displacement/Mixer/effects/Audio Out that read as permanent silence -
      // see docs/plans/audio-file-into-dsp-graph.md.
      if (dstNode->node->AudioInputSlot(slot) != nullptr)
         return srcIsAudioNode;
      else if (dstNode->node->NoteInputSlot(slot) != nullptr)
         return srcIsNoteSource;
      // dstAudio (Audio Analyze) is checked ahead of the srcIsAudioNode/
      // srcIsNoteSource gate below because it needs to accept an
      // AudioFileNode source specifically, and that source is now ALSO an
      // IAudioSource (srcIsAudioNode true) - the gate would otherwise reject
      // it before this branch is ever reached. This is the "fileSource"
      // pointer mechanism (WireInputSlot), not an AudioCable - deliberately
      // out of scope for this fix, see AudioAnalyzeNode::fileSource's comment.
      else if (dstAudio != nullptr)
         return srcAudioFile != nullptr; // only an Audio File feeds Audio Analyze
      else if (srcIsAudioNode || srcIsNoteSource)
         return false;
      else if (dstRender != nullptr)
      {
         if (slot < Render3DNode::kSlots)
            return srcGeometry != nullptr && srcCamera == nullptr && srcLight == nullptr;
         else if (slot == Render3DNode::kSlots)
            return srcCamera != nullptr;
         else if (slot == Render3DNode::kEnvSlot)
            return srcIsEnvironment;
         else if (slot > Render3DNode::kSlots && slot - Render3DNode::kSlots - 1 < Render3DNode::kLightSlots)
            return srcLight != nullptr;
         else
            return false;
      }
      else if (dstNode->node->GeometryInputSlot(slot) != nullptr)
         return srcGeometry != nullptr && srcCamera == nullptr && srcLight == nullptr;
      else if (dstMaterial != nullptr)
         return srcIsImage;
      else if (dstDisplacement != nullptr)
         return srcIsImage;
      else if (dstSetColor != nullptr && slot == 2)
         return srcPalette != nullptr;
      else if (dstSetColor != nullptr)
         return srcIsImage;
      else if (srcGeometry != nullptr || srcCamera != nullptr || srcLight != nullptr)
         return false; // 3D cables only go into 3D nodes
      else if (dstNode->node->ModulatorInputSlot(slot) != nullptr && !dstWantsImage)
         return srcIsModulator;
      else
         return srcIsImage;
   }

   // Performs the connection once IsInputSlotCompatible (or the caller's own
   // equivalent check) has already said `slot` on `dstNode` accepts whatever
   // `srcNode`'s output produces. `srcOutputIndex` only matters for the
   // modulator-cable-into-Math case, where a src node with more than one
   // modulator output (Path, Audio/Image Analyze, Macro) needs the specific
   // one that was actually dragged from.
   void WireInputSlot(GraphNode& srcNode, GraphNode& dstNode, int slot, int srcOutputIndex = 0)
   {
      auto* dstAudio = dynamic_cast<AudioAnalyzeNode*>(dstNode.node.get());
      auto* dstRender = dynamic_cast<Render3DNode*>(dstNode.node.get());
      auto* dstSetColor = dynamic_cast<SetColorNode*>(dstNode.node.get());

      auto* srcGeometry = dynamic_cast<IGeometrySource*>(srcNode.node.get());
      auto* srcCamera = dynamic_cast<CameraNode*>(srcNode.node.get());
      auto* srcLight = dynamic_cast<LightNode*>(srcNode.node.get());
      auto* srcPalette = dynamic_cast<IPaletteSource*>(srcNode.node.get());
      auto* srcAudioFile = dynamic_cast<AudioFileNode*>(srcNode.node.get());

      if (dstRender != nullptr)
      {
         if (slot < Render3DNode::kSlots)
            dstRender->geometry[slot] = srcGeometry;
         else if (slot == Render3DNode::kSlots)
            dstRender->camera = srcCamera;
         else if (slot == Render3DNode::kEnvSlot)
            dstRender->envInput.Connect(srcNode.node.get());
         else
            dstRender->lights[slot - Render3DNode::kSlots - 1] = srcLight;
      }
      else if (IGeometrySource** field = dstNode.node->GeometryInputSlot(slot))
      {
         // Whatever this slot's field is called internally, set it directly -
         // covers dstGeoOp, dstInstance, dstNull3D, dstMapping, dstMeshPoints,
         // dstMeshResynth, dstCloth, dstJoin, dstSwitcher3D, dstWrap, dstMeta,
         // dstPath, and slot 0 of dstMaterial/dstDisplacement.
         *field = srcGeometry;
      }
      else if (dstSetColor != nullptr && slot == 2)
      {
         dstSetColor->paletteInput = srcPalette;
         dstSetColor->source = SetColorNode::kPalette;
      }
      else if (dstSetColor != nullptr && slot == 1)
      {
         dstSetColor->TextureInput().Connect(srcNode.node.get());
         dstSetColor->source = SetColorNode::kTexture;
      }
      else if (dstAudio != nullptr)
      {
         // An Audio File keeps the fileSource path (analysed even with its
         // monitor off); anything else is a plain audio cable.
         dstAudio->fileSource = srcAudioFile;
         if (srcAudioFile != nullptr)
            dstAudio->audioInput.Disconnect();
         else
            dstAudio->audioInput.Connect(srcNode.node.get());
      }
      else if (IModulator** slotField = dstNode.node->ModulatorInputSlot(slot))
      {
         *slotField = ModulatorForOutput(srcNode.node.get(), srcOutputIndex);
      }
      else if (AudioCable* audioCable = dstNode.node->AudioInputSlot(slot))
      {
         audioCable->Connect(srcNode.node.get(), srcOutputIndex);
      }
      else if (NoteCable* noteCable = dstNode.node->NoteInputSlot(slot))
      {
         noteCable->Connect(srcNode.node.get(), srcOutputIndex);
      }
      else
      {
         ImageCable* cable = CableFor(dstNode, slot);
         if (cable != nullptr)
            cable->Connect(srcNode.node.get(), srcOutputIndex);
      }
   }

   // Walks forward from `dst` through whatever audio-input pins other nodes
   // wire to it, to see whether `src` is reachable downstream of `dst` - i.e.
   // whether connecting src -> dst would close a cycle. The visual graph
   // tolerates cycles via FeedbackNode's one-frame delay; the audio graph has
   // no such delay stage, so a cycle here would make the topological sort in
   // RebuildAudioTopology loop forever. Three nodes in a straight line can't
   // actually form one - this exists for P3, where a Delay-shaped effect
   // node could otherwise wire one in without any test catching it until the
   // sort hangs at connect time instead of failing loudly.
   // Turbo 0.60: the geometry graph has no delay stage either - a mesh pulled
   // through Join -> Transform -> Join recursed until the stack ran out (a
   // patch could load one; nothing refused it). Same walk as below, over the
   // geometry pins: does anything downstream of dst already feed src?
   bool WouldCreateGeometryCycle(INode* src, INode* dst)
   {
      if (src == dst)
         return true;
      std::vector<INode*> stack{ dst };
      std::set<INode*> visited;
      while (!stack.empty())
      {
         INode* cur = stack.back();
         stack.pop_back();
         if (!visited.insert(cur).second)
            continue;
         auto* curGeo = dynamic_cast<IGeometrySource*>(cur);
         if (curGeo == nullptr)
            continue;
         for (GraphNode& gn : gNodes)
         {
            for (int slot = 0; slot < 64; slot++)
            {
               IGeometrySource** in = gn.node->GeometryInputSlot(slot);
               if (in != nullptr && *in == curGeo)
               {
                  if (gn.node.get() == src)
                     return true;
                  stack.push_back(gn.node.get());
               }
            }
         }
      }
      return false;
   }

   bool WouldCreateAudioCycle(INode* src, INode* dst)
   {
      if (src == dst)
         return true;
      std::vector<INode*> stack{ dst };
      std::set<INode*> visited;
      while (!stack.empty())
      {
         INode* cur = stack.back();
         stack.pop_back();
         if (!visited.insert(cur).second)
            continue;
         for (GraphNode& gn : gNodes)
         {
            for (int slot = 0; slot < kMaxAudioSlots; slot++)
            {
               AudioCable* cable = gn.node->AudioInputSlot(slot);
               if (cable != nullptr && cable->GetSource() == cur)
               {
                  if (gn.node.get() == src)
                     return true;
                  stack.push_back(gn.node.get());
               }
            }
         }
      }
      return false;
   }

   // Note-cable equivalent of WouldCreateAudioCycle just above - same walk,
   // over NoteInputSlot() instead of AudioInputSlot(). audio-graph-
   // semantics.md §5: "Note cycles must be rejected on the same basis [as
   // audio cycles]" - a Note Echo feeding itself (Part 2) would otherwise be
   // an unbounded event storm on the audio thread, same failure mode a
   // Delay-shaped audio cycle would hang the topological sort on. No Part 1
   // node can actually trigger this (none forwards notes), but the check
   // costs nothing to have in place now rather than after Part 2 adds one.
   bool WouldCreateNoteCycle(INode* src, INode* dst)
   {
      if (src == dst)
         return true;
      std::vector<INode*> stack{ dst };
      std::set<INode*> visited;
      while (!stack.empty())
      {
         INode* cur = stack.back();
         stack.pop_back();
         if (!visited.insert(cur).second)
            continue;
         for (GraphNode& gn : gNodes)
         {
            for (int slot = 0; slot < kMaxNoteSlots; slot++)
            {
               NoteCable* cable = gn.node->NoteInputSlot(slot);
               if (cable != nullptr && cable->GetSource() == cur)
               {
                  if (gn.node.get() == src)
                     return true;
                  stack.push_back(gn.node.get());
               }
            }
         }
      }
      return false;
   }

   // Headless equivalent of the ed::QueryNewLink handler's connect-acceptance
   // path (below, in the main editor draw loop) - same validity checks and
   // wiring calls, extracted so RemoteControl's `connect` RPC can wire an
   // ordinary input slot (image/geometry/audio/note/modulator/palette-color)
   // without going through the imgui-node-editor UI at all. Deliberately
   // scoped to plain input-slot connections only, matching the RPC's
   // (srcIndex, srcOutputIndex, dstIndex, dstSlot) shape - it does not cover
   // binding a modulator into a param pin or a palette into a swatch pin,
   // since those need a param/color pin index rather than a plain input
   // slot; that's left for a future RPC method if it's ever needed.
   bool ConnectNodes(int srcIndex, int srcOutputIndex, int dstIndex, int dstSlot, std::string& outError)
   {
      GraphNode* srcNode = FindNodeByIndex(srcIndex);
      GraphNode* dstNode = FindNodeByIndex(dstIndex);
      if (srcNode == nullptr || dstNode == nullptr)
      {
         outError = "unknown node index";
         return false;
      }
      if (srcNode == dstNode)
      {
         outError = "cannot connect a node to itself";
         return false;
      }

      const bool srcIsModulator = dynamic_cast<IModulator*>(srcNode->node.get()) != nullptr ||
                                   ModulatorForOutput(srcNode->node.get(), srcOutputIndex) != nullptr;
      auto* srcPalette = dynamic_cast<IPaletteSource*>(srcNode->node.get());
      auto* srcAudioFile = dynamic_cast<AudioFileNode*>(srcNode->node.get());
      auto* srcGeometry = dynamic_cast<IGeometrySource*>(srcNode->node.get());
      auto* srcCamera = dynamic_cast<CameraNode*>(srcNode->node.get());
      auto* srcLight = dynamic_cast<LightNode*>(srcNode->node.get());
      const bool srcIsEnvironment = dynamic_cast<EnvironmentNode*>(srcNode->node.get()) != nullptr;
      const bool srcIsAudioNode = IsAudioOutput(srcNode->node.get(), srcOutputIndex);
      const bool srcIsNoteSource = IsNoteOutput(srcNode->node.get(), srcOutputIndex);

      if (!IsInputSlotCompatible(dstNode, dstSlot, srcIsModulator, srcPalette, srcGeometry, srcCamera,
                                  srcLight, srcAudioFile, srcIsEnvironment, srcIsAudioNode, srcIsNoteSource))
      {
         outError = "incompatible source/destination for this slot";
         return false;
      }
      if (srcIsAudioNode && WouldCreateAudioCycle(srcNode->node.get(), dstNode->node.get()))
      {
         outError = "would create an audio feedback loop";
         return false;
      }
      if (srcIsNoteSource && WouldCreateNoteCycle(srcNode->node.get(), dstNode->node.get()))
      {
         outError = "would create a note feedback loop";
         return false;
      }
      if (dynamic_cast<IGeometrySource*>(srcNode->node.get()) != nullptr &&
          dstNode->node->GeometryInputSlot(dstSlot) != nullptr &&
          WouldCreateGeometryCycle(srcNode->node.get(), dstNode->node.get()))
      {
         outError = "would create a geometry loop";
         return false;
      }

      PushUndoCheckpoint();
      WireInputSlot(*srcNode, *dstNode, dstSlot, srcOutputIndex);
      if (srcIsAudioNode || srcIsNoteSource ||
          dstNode->node->AudioInputSlot(dstSlot) != nullptr ||
          dstNode->node->NoteInputSlot(dstSlot) != nullptr)
         RebuildAudioTopology();
      return true;
   }

   // Node types worth suggesting first in the search popup when a cable was
   // dragged out of `srcNode`'s output and dropped on empty canvas: every
   // registered, user-spawnable type that has at least one input slot
   // IsInputSlotCompatible would accept from this source. Only input-pin
   // compatibility drives this (not param/colour pins, which nearly every
   // node accepts and so wouldn't narrow anything down) - a plain image
   // source correctly yields no geometry/camera/light/audio matches here,
   // since those pins still show unfiltered below the suggestions. Builds a
   // throwaway, unregistered GraphNode per candidate type to probe its input
   // slots; this runs once per drag-to-empty-space, not per frame.
   std::vector<std::pair<std::string, std::string>> RecommendedNodeTypesForOutput(GraphNode* srcNode)
   {
      std::vector<std::pair<std::string, std::string>> result;
      if (srcNode == nullptr)
         return result;

      const bool srcIsModulator =
         dynamic_cast<IModulator*>(srcNode->node.get()) != nullptr ||
         (srcNode->node->OutputCount() > 0 && srcNode->node->ModulatorOutput(0) != nullptr);
      auto* srcPalette = dynamic_cast<IPaletteSource*>(srcNode->node.get());
      auto* srcGeometry = dynamic_cast<IGeometrySource*>(srcNode->node.get());
      auto* srcCamera = dynamic_cast<CameraNode*>(srcNode->node.get());
      auto* srcLight = dynamic_cast<LightNode*>(srcNode->node.get());
      auto* srcAudioFile = dynamic_cast<AudioFileNode*>(srcNode->node.get());
      const bool srcIsEnvironment = dynamic_cast<EnvironmentNode*>(srcNode->node.get()) != nullptr;
      const bool srcIsAudioNode = IsAudioOutput(srcNode->node.get(), 0);
      const bool srcIsNoteSource = IsNoteOutput(srcNode->node.get(), 0);

      for (const std::string& category : NodeFactory::Instance().GetCategories())
      {
         for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
         {
            if (!IsUserSpawnable(name))
               continue;

            GraphNode probe;
            probe.node.reset(NodeFactory::Instance().MakeNode(name));
            if (!probe.node)
               continue;

            const int slotCount = InputCountFor(probe);
            bool compatible = false;
            for (int slot = 0; slot < slotCount; ++slot)
            {
               if (IsInputSlotCompatible(&probe, slot, srcIsModulator, srcPalette, srcGeometry,
                                          srcCamera, srcLight, srcAudioFile, srcIsEnvironment,
                                          srcIsAudioNode, srcIsNoteSource))
               {
                  compatible = true;
                  break;
               }
            }
            if (compatible)
               result.emplace_back(name, category);
         }
      }
      return result;
   }

   // Finds a spawn position near `center` that doesn't land on top of any
   // existing node. Tries the exact center first (the common case: an empty
   // canvas, or a center that has scrolled clear), then walks outward ring by
   // ring over a grid of node-sized slots until it finds one whose padded box
   // clears every current node - so the first free slot returned is always
   // the one closest to where the user was actually looking.
   //
   // Reads node boxes via ed::GetNodePosition/GetNodeSize, which need a
   // current editor context; this runs from the node panel, after that
   // frame's ed::End() has already cleared it (see ed::SetCurrentEditor(nullptr)
   // at the bottom of the graph draw), so the context is set/restored
   // explicitly here rather than relying on caller state - same pattern as
   // ApplyTheme's ed::GetStyle() call above.
   ImVec2 FindFreeSpawnPosition(const ImVec2& center)
   {
      const float kFootprintW = 260.0f;
      const float kFootprintH = 300.0f;
      const float kMargin = 24.0f;

      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);

      std::vector<std::pair<ImVec2, ImVec2>> occupied;
      occupied.reserve(gNodes.size());
      for (GraphNode& gn : gNodes)
      {
         ImVec2 p = ed::GetNodePosition(gn.NodeId());
         ImVec2 s = ed::GetNodeSize(gn.NodeId());
         if (s.x <= 0.0f || s.y <= 0.0f)
            continue; // never laid out (e.g. this frame's spawn) - nothing to avoid yet
         occupied.emplace_back(ImVec2(p.x - kMargin, p.y - kMargin),
                                ImVec2(p.x + s.x + kMargin, p.y + s.y + kMargin));
      }

      ed::SetCurrentEditor(prevEditor);

      auto overlapsAny = [&](const ImVec2& candMin, const ImVec2& candMax)
      {
         for (const auto& box : occupied)
         {
            if (candMax.x > box.first.x && candMin.x < box.second.x &&
                candMax.y > box.first.y && candMin.y < box.second.y)
               return true;
         }
         return false;
      };

      for (int ring = 0; ring < 12; ++ring)
      {
         if (ring == 0)
         {
            if (!overlapsAny(center, ImVec2(center.x + kFootprintW, center.y + kFootprintH)))
               return center;
            continue;
         }
         for (int gx = -ring; gx <= ring; ++gx)
         {
            for (int gy = -ring; gy <= ring; ++gy)
            {
               // Only the new outer ring of the square - the interior was
               // already tried by earlier, smaller rings.
               if (std::abs(gx) != ring && std::abs(gy) != ring)
                  continue;
               ImVec2 cand(center.x + gx * (kFootprintW + kMargin),
                           center.y + gy * (kFootprintH + kMargin));
               ImVec2 candMax(cand.x + kFootprintW, cand.y + kFootprintH);
               if (!overlapsAny(cand, candMax))
                  return cand;
            }
         }
      }
      return center; // exhausted the search area - stack rather than fail
   }

   // Turbo 0.50: new nodes draw in front. node-editor reuses its per-id
   // state, and editor ids are index * kStride, so after a patch load or undo
   // (indices restart) a new node can inherit a stale entry from early in the
   // draw list and render behind existing nodes. ApplyBringToFront (inside
   // the editor frame) lifts each queued node with a one-frame z of 1, which
   // the editor's stable z sort moves to the end of the draw list; the next
   // frame puts z back to 0 and the order stays, so click-to-front still works.
   std::vector<int> gBringToFrontQueue;
   std::vector<int> gBringToFrontReset;

   void ApplyBringToFront()
   {
      // GetNodeZPosition returns 0 for an unknown id, so this never recreates
      // a node deleted in between (SetNodeZPosition would).
      for (int index : gBringToFrontReset)
      {
         const ed::NodeId id(index * GraphNode::kStride);
         if (ed::GetNodeZPosition(id) != 0.0f)
            ed::SetNodeZPosition(id, 0.0f);
      }
      gBringToFrontReset.clear();
      for (int index : gBringToFrontQueue)
      {
         if (FindNodeByIndex(index) == nullptr)
            continue;
         ed::SetNodeZPosition(ed::NodeId(index * GraphNode::kStride), 1.0f);
         gBringToFrontReset.push_back(index);
      }
      gBringToFrontQueue.clear();
   }

   GraphNode* SpawnNode(const std::string& requestedTypeName, const std::string& category,
                        float x = 0.0f, float y = 0.0f)
   {
      // Turbo: an old patch's renamed type comes back under its current name.
      const std::string typeName = NodeFactory::CanonicalName(requestedTypeName);
      INode* node = NodeFactory::Instance().MakeNode(typeName);
      if (node == nullptr)
         return nullptr;

      PushUndoCheckpoint();

      GraphNode gn;
      gn.node.reset(node);
      gn.typeName = typeName;
      // Turbo: the registry's category wins over the caller's (a patch saved
      // before the menu reorganisation names the old one).
      const std::string registered = NodeFactory::Instance().CategoryOf(typeName);
      gn.category = registered.empty() ? category : registered;
      gn.index = gNextIndex++;
      gn.uid = gNextNodeUid++;
      gn.spawnX = x;
      gn.spawnY = y;
      // File/camera sources need an explicit first action. Open their standard
      // parameter area on spawn so the chooser and camera controls are never
      // hidden behind the small eye toggle.
      if (dynamic_cast<VideoSourceNode*>(node) != nullptr || dynamic_cast<VideoInNode*>(node) != nullptr ||
          dynamic_cast<FeedbackNode*>(node) != nullptr || dynamic_cast<OutputNode*>(node) != nullptr ||
          dynamic_cast<WirelessNode*>(node) != nullptr ||
          dynamic_cast<ClipMatrixNode*>(node) != nullptr || dynamic_cast<ClipMatrixOutNode*>(node) != nullptr)
         gn.showParams = true;
      gBringToFrontQueue.push_back(gn.index); // Turbo 0.50
      gNodes.push_back(std::move(gn));
      return &gNodes.back();
   }

   // File-backed and compiled-from-text nodes keep derived state (a loaded
   // texture, a compiled GL program) that VisitParams deliberately does not
   // touch - it declares settings, not the runtime side effects of restoring
   // them. Both patch load and copy/paste restore a node from bare settings,
   // so both call this afterwards rather than duplicating the same dynamic
   // casts in two places.
   void ReloadDerivedState(INode* node)
   {
      if (auto* img = dynamic_cast<ImageSourceNode*>(node))
         img->ReloadFromPath();
      if (auto* slideshow = dynamic_cast<SlideshowNode*>(node))
         slideshow->ReloadFromFolder();
      if (auto* env = dynamic_cast<EnvironmentNode*>(node))
         env->ReloadFromPath();
      if (auto* model = dynamic_cast<ModelSourceNode*>(node))
         model->ReloadFromPath();
      if (auto* audio = dynamic_cast<AudioFileNode*>(node))
         audio->ReloadFromPath();
      if (auto* sampler = dynamic_cast<SamplerNode*>(node))
         sampler->ReloadFromPath();
      if (auto* looper = dynamic_cast<LooperNode*>(node))
         looper->ReloadFromState();
      if (auto* slicer = dynamic_cast<SlicerNode*>(node))
         slicer->ReloadFromPath();
      if (auto* molder = dynamic_cast<MolderNode*>(node))
         molder->ReloadFromPath();
      if (auto* partials = dynamic_cast<PartialsNode*>(node)) // Turbo 0.60 (upstream)
         partials->ReloadFromPath();
      if (auto* gm = dynamic_cast<GrainMolderNode*>(node))
         gm->ReloadFromPath();
      if (auto* paul = dynamic_cast<PaulStretchNode*>(node))
         paul->ReloadFromPath();
      if (auto* gran = dynamic_cast<GranularNode*>(node))
         gran->ReloadFromPath();
      if (auto* drum = dynamic_cast<DrumSequencerNode*>(node))
         drum->ReloadFromPaths(); // Turbo 0.50: keeps the saved lane trims itself
      if (auto* mpc = dynamic_cast<MpcNode*>(node))
         mpc->ReloadFromPaths();
      if (auto* vmpc = dynamic_cast<VmpcNode*>(node))
         vmpc->ReloadFromPaths();
      if (auto* cm = dynamic_cast<ClipMatrixNode*>(node))
         cm->ReloadFromPaths();
      if (auto* video = dynamic_cast<VideoSourceNode*>(node))
         video->ReloadFromPath();
      if (auto* palette = dynamic_cast<PaletteNode*>(node))
         palette->ReloadFromPath();
      if (auto* midi = dynamic_cast<MidiFileNode*>(node))
         midi->ReloadFromPath(); // Turbo 0.48
      if (auto* formula = dynamic_cast<FormulaNode*>(node))
         formula->Apply();
      // Re-instantiates the plugin from the identity VisitParams just restored
      // and queues its saved fullState; the actual load finishes
      // asynchronously, a frame or two later, in CookIfNeeded.
      if (auto* plugin = dynamic_cast<AudioPluginNode*>(node))
         plugin->ReloadFromIdentity();
   }

   // Copies every parameter VisitParams declares, for any node type, by
   // routing through the same save/load format used for patch files: write src
   // to an in-memory param list, then read it back into dst. This used to be a
   // hand-maintained field-by-field switch covering 7 of roughly 50 node types;
   // every type it didn't cover pasted a node whose values silently stayed at
   // spawn defaults. Now paste and patch load share one source of truth, so a
   // node that round-trips through Save also round-trips through Copy.
   void CopyParams(INode* dstNode, INode* srcNode)
   {
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(srcNode, params);
      dstNode->BeforeLoadParams(); // Turbo 0.60 (upstream)
      Patch::LoadParams(dstNode, params);
      ReloadDerivedState(dstNode);
   }
