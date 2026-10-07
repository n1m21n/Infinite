// Cable / slot compatibility, connect, schema probe, cluster links, spawn (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // Arrange::Model <-> Patch::Data. Straight field copies in both directions:
   // both sides are already ticks, so nothing here is lossy and nothing here
   // needs the transport.
   void ArrangeModelToPatchData(const Arrange::Model& m, Patch::Data& data)
   {
      data.streams.clear();
      data.streams.reserve(m.lanes.size());
      for (const Arrange::Lane& lane : m.lanes)
      {
         Patch::StreamRecord s;
         s.id = lane.id;
         s.type = lane.type;
         s.blendMode = 0; // per clip now (clipblend); the stream field is legacy input only
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
      // Saved, not recomputed as max-id + 1: a clip deleted after taking a
      // high id would otherwise let the next session hand that id out again,
      // and every cache keyed on clip id (selection, waveform, thumbnail)
      // would silently attach to the wrong clip.
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
   }


   // `resolveLegacy` maps a pre-uid patch's saved node index to the uid of the
   // node ApplyPatchData just spawned for it. Null when there is no graph to
   // resolve against (the headless fixtures), in which case a legacy clip
   // simply comes back offline.
   void PatchDataToArrangeModel(const Patch::Data& data, Arrange::Model& m,
                                const std::function<uint64_t(int)>& resolveLegacy)
   {
      m = Arrange::Model();
      m.nextId = std::max<uint64_t>(1, data.arrangeSettings.nextId);
      for (const Patch::StreamRecord& s : data.streams)
      {
         Arrange::Lane lane;
         lane.id = s.id;
         lane.type = (s.type == Patch::kStreamAudio) ? Arrange::kLaneAudio : Arrange::kLaneVideo;
         lane.blendMode = 0; // legacy lane-wide mode migrates onto each clip below
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
            if (clip.srcUid == 0 && c.legacySrcIndex >= 0 && resolveLegacy)
               clip.srcUid = resolveLegacy(c.legacySrcIndex);
            clip.srcOutput = c.srcOutput;
            clip.fadeIn = c.fadeInTick;
            clip.fadeOut = c.fadeOutTick;
            clip.gainDb = c.gainDb;
            clip.pan = c.pan;
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
            // origBpm is the detected tempo, display only. It is written only
            // when > 0, so a missing line (-1, the load sentinel) means "none
            // detected" or a patch that predates detection - never a tempo.
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

      // Legacy patches carry no ids at all; Normalize mints them and, either
      // way, re-seats nextId above everything present. Without that clamp a
      // file whose saved nextId was stale would hand out a duplicate id on
      // the very first edit after loading.
      Arrange::Normalize(m);
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
      if (dynamic_cast<SwitcherNode*>(gn.node.get()) != nullptr)
         return SwitcherNode::kSlots;
      // Modulator input nodes (Math, Range to Range, Smooth, ...) take modulator
      // cables rather than images, but they are still ordinary input pins as far
      // as the editor is concerned.
      if (int c = gn.node->ModulatorInputCount())
         return c;
      if (dynamic_cast<BlendNode*>(gn.node.get()) != nullptr)
         return 2;
      if (auto* filter = dynamic_cast<FilterNode*>(gn.node.get()))
         return filter->Def().inputs;
      if (dynamic_cast<FitNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<SyphonOutNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<NdiOutNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ProjectionNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ResynthNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<CurvesNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<PredictiveColoringNode*>(gn.node.get()) != nullptr)
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
      if (auto* fp = dynamic_cast<FieldPixelNode*>(gn.node.get()))
         // Dynamic pins, Phase 2b (build step 13, §5.7): no native input pin
         // (device-catalog simplification) - one slot per currently-declared
         // `input image` pin.
         return fp->DeclaredImageInputCount();
      if (dynamic_cast<FieldElementNode*>(gn.node.get()) != nullptr)
         return 1; // the kernel's optional "geo" input (generator when unwired)
      // (Audio Analyze used to need an entry here for its fileSource pin; it
      // has a real AudioInputSlot now and is counted by the generic audio/note
      // probe below, like every other audio consumer.)
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
      if (auto* fx = dynamic_cast<AudioEffectNode*>(gn.node.get()))
         if (fx->Def().hasGeometryInput)
            return 2; // audio (slot 0) + shape (slot 1), Shape Resonator
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
      if (dynamic_cast<FeedbackNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<TrailsNode*>(gn.node.get()) != nullptr)
         return 1;
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


   // Bypass means "this node is not in the chain", which only has one honest
   // answer when there is exactly one thing to fall back to. A node with two
   // or more input pins of any kind (Blend, Mixer, Switcher, Join, Material,
   // a sidechained Dynamics, Math...) has no input that is obviously "the"
   // pass-through, so it cannot be bypassed at all: no power button, B skips
   // it, and EnforceBypassRule clears a bypass flag a load or a pin-count
   // change left behind. Nodes with zero or one input keep bypass.
   // Bypass means "this node is not in the chain", so it only exists where
   // that has one obvious meaning: a node with at most one input passes that
   // input through. The one exception is an instrument (category "Synths"):
   // its note/texture/sidechain pins are control, not signal, and bypassing
   // it means silence - its BypassSource() stays null, so nothing is passed.
   bool CanBypass(const GraphNode& gn)
   {
      if (gn.node == nullptr || dynamic_cast<CommentNode*>(gn.node.get()) != nullptr)
         return false;
      if (gn.category == "Synths")
         return gn.node->BypassSource() == nullptr;
      return InputCountFor(gn) <= 1;
   }


   ImageCable* CableFor(GraphNode& gn, int slot)
   {
      // Every other Render3D input (geometry/camera/light) is a raw pointer
      // handled by ConnectGeometrySlot; only the env slot is an ImageCable.
      if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         return (slot == Render3DNode::kEnvSlot) ? &render->envInput : nullptr;
      if (auto* stack = dynamic_cast<LayerStackNode*>(gn.node.get()))
         return (slot >= 0 && slot < LayerStackNode::kSlots) ? &stack->Input(slot) : nullptr;
      if (auto* sw = dynamic_cast<SwitcherNode*>(gn.node.get()))
         return (slot >= 0 && slot < SwitcherNode::kSlots) ? &sw->Input(slot) : nullptr;
      if (auto* blend = dynamic_cast<BlendNode*>(gn.node.get()))
         return slot == 0 ? &blend->InputA() : (slot == 1 ? &blend->InputB() : nullptr);
      if (auto* filter = dynamic_cast<FilterNode*>(gn.node.get()))
      {
         if (slot == 0)
            return &filter->Input();
         return (slot == 1 && filter->Def().inputs > 1) ? &filter->Input2() : nullptr;
      }
      if (auto* fit = dynamic_cast<FitNode*>(gn.node.get()))
         return slot == 0 ? &fit->Input() : nullptr;
      if (auto* resynth = dynamic_cast<ResynthNode*>(gn.node.get()))
         return slot == 0 ? &resynth->Input() : nullptr;
      if (auto* i2p = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
         return slot == 0 ? &i2p->Input() : nullptr;
      if (auto* dp = dynamic_cast<DepthProjectionNode*>(gn.node.get()))
         return (slot == 0) ? &dp->DepthInput() : ((slot == 1) ? &dp->ColorInput() : nullptr);
      if (auto* curves = dynamic_cast<CurvesNode*>(gn.node.get()))
         return slot == 0 ? &curves->Input() : nullptr;
      if (auto* pc = dynamic_cast<PredictiveColoringNode*>(gn.node.get()))
         return slot == 0 ? &pc->Input() : nullptr;
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
      if (auto* fp = dynamic_cast<FieldPixelNode*>(gn.node.get()))
         // Dynamic pins, Phase 2b (build step 13, §5.7): no native input pin
         // (device-catalog simplification) - slot 0..N-1 route to the
         // currently-declared `input image` pins in PinTable order.
         return fp->DeclaredImageInput(slot);
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
      if (auto* ndiOut = dynamic_cast<NdiOutNode*>(gn.node.get()))
         return slot == 0 ? &ndiOut->Input() : nullptr;
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


   // Defined near DisconnectAllTo/RemoveNodeByIndex, below; forward-declared
   // here since DisconnectLinkById (earlier in the file) needs to call it too.
   void RebuildAudioTopology();

   void ForceAudioRepare();
 // next RebuildAudioTopology calls PrepareToPlay on every audio node again

   // Forward-declared here since ArrangeImportMediaFile (Arrange media-drop
   // import, earlier in the file) needs to clean up a just-spawned node when
   // the clip placement it was for fails.
   void RemoveNodeByIndex(int index);


   // Defined near ArrangePollMediaImports, below; forward-declared here since
   // ArrangePasteAt/ArrangeDuplicateSelection (earlier in the file) need to
   // call it for every newly-made Sample clip.
   void ArrangeRespawnCloneNode(uint64_t clipId);


   // Which geometry-ish pin a node exposes at a given slot, and how to set it.
   // Geometry, camera, light and modulator connections are raw pointers rather
   // than ImageCables, so saving and loading them needs this one place that
   // knows the mapping - the same knowledge the link-drawing and connect paths
   // use. srcOutput picks which of a multi-output source's pins is meant; it
   // only matters for modulator pins today (geometry/camera/light sources all
   // have a single output), which is why it defaults to 0.
   void ConnectGeometrySlot(GraphNode& dst, int slot, GraphNode& src, int srcOutput)
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
      // Every modulator input pin (Range to Range, Smooth, Mod Depth, Compare,
      // Envelope, CV to Pitch, Invert, Mod Curve, Math, OSC Send, ...) goes
      // through the generic ModulatorInputSlot() accessor, exactly like the
      // live drag-to-connect path does. This used to special-case MathNode
      // only, so every *other* modulator-into-modulator cable was written to
      // the patch by BuildPatchData and then silently dropped on the way back
      // in - which made an undo, a redo or a file open detach the cable.
      if (IModulator** modField = dst.node->ModulatorInputSlot(slot))
      {
         *modField = ModulatorForOutput(src.node.get(), srcOutput);
         return;
      }
      if (auto* setColor = dynamic_cast<SetColorNode*>(dst.node.get()))
      {
         if (slot == 2)
            setColor->paletteInput = dynamic_cast<IPaletteSource*>(src.node.get());
         return;
      }
      // Legacy patches only. Audio Analyze's source used to be a bare
      // AudioFileNode* recorded in the geometry link table; it is now an
      // ordinary AudioCable saved and restored by the generic audio-cable
      // passes. Restoring an old entry as a cable is what migrates those
      // patches - the next save writes it through the audio path instead.
      if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(dst.node.get()))
      {
         if (dynamic_cast<IAudioSource*>(src.node.get()) != nullptr)
            audio->input.Connect(src.node.get());
         return;
      }
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
                               LightNode* srcLight,
                               bool srcIsEnvironment, bool srcIsAudioNode, bool srcIsNoteSource,
                               bool srcIsPredictor)
   {
      if (dstNode == nullptr || dstNode->node == nullptr)
         return false;

      // A Predictive LFO / Macro (IPredictor) writes straight into the
      // parameters it is bound to - it publishes no signal on its output for
      // another node to read, so a cable from one into any input slot would
      // feed a permanently dead value. Param pins never reach this function
      // (the UI binds those through Modulation), so refusing every input slot
      // here is exactly the UI's rule and nothing more.
      //
      // This lived only in the ed::QueryNewLink handler until now, which meant
      // ConnectNodes() (the RemoteControl `connect` RPC and cluster paste),
      // the drag-to-empty-canvas auto-wire, and RecommendedNodeTypesForOutput
      // all happily made a link the UI refuses. `srcIsPredictor` is a required
      // parameter rather than a defaulted one precisely so a new call site
      // cannot silently reopen that hole - the compiler names it instead.
      if (srcIsPredictor)
         return false;

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
      // Audio Analyze used to need a branch of its own here, accepting an
      // AudioFileNode and nothing else, because it read that node's published
      // AudioLevels through a bare pointer instead of taking audio. It now
      // owns a real AudioNode and a real AudioInputSlot(0), so the generic
      // audio-pin branch above already accepts any IAudioSource for it -
      // exactly the broadening that special case was blocking.
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
   void WireInputSlot(GraphNode& srcNode, GraphNode& dstNode, int slot, int srcOutputIndex)
   {
      auto* dstRender = dynamic_cast<Render3DNode*>(dstNode.node.get());
      auto* dstSetColor = dynamic_cast<SetColorNode*>(dstNode.node.get());

      auto* srcGeometry = dynamic_cast<IGeometrySource*>(srcNode.node.get());
      // A geometry-source node can have non-geometry outputs too (e.g. Field
      // Modifier's "chime"/"glow" declared pins alongside its primary "geo"
      // mesh output) - only wire the real mesh through if the dragged output
      // index is actually the geometry one. Without this check, every caller
      // that reaches this function (including the drag-to-empty-canvas spawn
      // flow) silently treats any output of a geometry node as if it were the
      // mesh, regardless of which pin was actually dragged.
      if (srcGeometry != nullptr && !srcGeometry->IsGeometryOutputIndex(srcOutputIndex))
         srcGeometry = nullptr;
      auto* srcCamera = dynamic_cast<CameraNode*>(srcNode.node.get());
      auto* srcLight = dynamic_cast<LightNode*>(srcNode.node.get());
      auto* srcPalette = dynamic_cast<IPaletteSource*>(srcNode.node.get());

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


   // What a source node's output can be plugged into - the flags
   // IsInputSlotCompatible wants, gathered once so ConnectNodes and the patch
   // validator ask the same rule the same way.
   struct SrcCaps
   {
      bool modulator = false;
      IPaletteSource* palette = nullptr;
      IGeometrySource* geometry = nullptr;
      CameraNode* camera = nullptr;
      LightNode* light = nullptr;
      bool environment = false;
      bool audio = false;
      bool note = false;
      bool predictor = false;
   };


   SrcCaps CapsOf(GraphNode* src, int srcOutputIndex)
   {
      SrcCaps c;
      INode* n = src->node.get();
      c.modulator = dynamic_cast<IModulator*>(n) != nullptr || ModulatorForOutput(n, srcOutputIndex) != nullptr;
      c.palette = dynamic_cast<IPaletteSource*>(n);
      c.geometry = dynamic_cast<IGeometrySource*>(n);
      if (c.geometry != nullptr && !c.geometry->IsGeometryOutputIndex(srcOutputIndex))
         c.geometry = nullptr;
      c.camera = dynamic_cast<CameraNode*>(n);
      c.light = dynamic_cast<LightNode*>(n);
      c.environment = dynamic_cast<EnvironmentNode*>(n) != nullptr;
      auto* audioSource = dynamic_cast<IAudioSource*>(n);
      c.audio = audioSource != nullptr && audioSource->IsAudioOutputIndex(srcOutputIndex);
      c.note = dynamic_cast<INoteSource*>(n) != nullptr;
      c.predictor = dynamic_cast<IPredictor*>(n) != nullptr;
      return c;
   }


   bool CapsAcceptedBy(const SrcCaps& c, GraphNode* dst, int slot)
   {
      return IsInputSlotCompatible(dst, slot, c.modulator, c.palette, c.geometry, c.camera, c.light,
                                   c.environment, c.audio, c.note, c.predictor);
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

      const SrcCaps caps = CapsOf(srcNode, srcOutputIndex);
      const bool srcIsAudioNode = caps.audio;
      const bool srcIsNoteSource = caps.note;
      const bool srcIsPredictor = caps.predictor;
      if (!CapsAcceptedBy(caps, dstNode, dstSlot))
      {
         // Same verdict either way; only the wording differs, so the RPC
         // caller learns which rule refused it rather than a generic "no".
         outError = srcIsPredictor
            ? "a Predictive LFO / Macro can only drive a parameter, not another node"
            : "incompatible source/destination for this slot";
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

      PushUndoCheckpoint();
      WireInputSlot(*srcNode, *dstNode, dstSlot, srcOutputIndex);
      if (srcIsAudioNode || srcIsNoteSource ||
          dstNode->node->AudioInputSlot(dstSlot) != nullptr ||
          dstNode->node->NoteInputSlot(dstSlot) != nullptr)
         RebuildAudioTopology();
      return true;
   }

   static void CaptureProbeButton(const char* label)
   {
      if (!gHeadlessProbeAll || gCurrentNodeIndex < 0 || label == nullptr)
         return;
      std::string l(label);
      const size_t hashes = l.find("##");
      if (hashes != std::string::npos)
         l.erase(hashes);
      if (l.empty())
         return;
      std::vector<std::string>& v = gProbeButtons[gCurrentNodeIndex];
      if (v.size() < 64 && std::find(v.begin(), v.end(), l) == v.end())
         v.push_back(std::move(l));
   }

   static const bool gProbeButtonHookInstalled = (ImGui::ButtonLabelHook = &CaptureProbeButton, true);


   // ---- patch schema (Infinite --describe / --validate) ----
   // Records what a node's VisitParams declares: tag letter, key, default.
   class SchemaParamRecorder : public ParamVisitor
   {
   public:
      std::vector<PatchSchema::ParamInfo>& out;
      explicit SchemaParamRecorder(std::vector<PatchSchema::ParamInfo>& o) : out(o) {}
      static std::string F(float v)
      {
         char b[48];
         std::snprintf(b, sizeof(b), "%.9g", v);
         return b;
      }
      void Float(const char* n, float& v) override { out.push_back({ n, 'f', F(v) }); }
      void Int(const char* n, int& v) override { out.push_back({ n, 'i', std::to_string(v) }); }
      void Bool(const char* n, bool& v) override { out.push_back({ n, 'b', v ? "1" : "0" }); }
      void Text(const char* n, std::string& v) override { out.push_back({ n, 's', v }); }
      void Color(const char* n, float rgb[3]) override { out.push_back({ n, 'c', F(rgb[0]) + " " + F(rgb[1]) + " " + F(rgb[2]) }); }
   };


   const char* KindOfOutput(const SrcCaps& c)
   {
      if (c.predictor) return "predictor";
      if (c.audio) return "audio";
      if (c.note) return "note";
      if (c.geometry != nullptr) return "geometry";
      if (c.camera != nullptr) return "camera";
      if (c.light != nullptr) return "light";
      if (c.palette != nullptr) return "palette";
      if (c.modulator) return "modulator";
      if (c.environment) return "environment";
      return "image";
   }


   bool SlotKindOf(GraphNode& gn, int slot, std::string& kind)
   {
      INode* n = gn.node.get();
      // Camera, light and palette pins are raw pointers with no generic
      // accessor; they are saved as `geo` lines like the geometry pins.
      if (dynamic_cast<Render3DNode*>(n) != nullptr && slot == Render3DNode::kSlots) kind = "camera";
      else if (dynamic_cast<Render3DNode*>(n) != nullptr && slot > Render3DNode::kSlots &&
               slot - Render3DNode::kSlots - 1 < Render3DNode::kLightSlots) kind = "light";
      else if (dynamic_cast<SetColorNode*>(n) != nullptr && slot == 2) kind = "palette";
      else if (n->AudioInputSlot(slot) != nullptr) kind = "audio";
      else if (n->NoteInputSlot(slot) != nullptr) kind = "note";
      else if (n->GeometryInputSlot(slot) != nullptr) kind = "geometry";
      else if (n->ModulatorInputSlot(slot) != nullptr) kind = "modulator";
      else if (slot < InputCountFor(gn) && CableFor(gn, slot) != nullptr)
         // Render 3D's env pin is an ImageCable, but IsInputSlotCompatible only
         // lets an Environment (HDRI) node plug into it, not a general image.
         kind = dynamic_cast<Render3DNode*>(n) != nullptr ? "environment" : "image";
      else return false;
      return true;
   }


   // A free-standing instance (never on the canvas) used to answer schema and
   // connection-rule questions about a node type. Cached for the process.
   GraphNode* SchemaProbe(const std::string& typeName)
   {
      static std::map<std::string, std::unique_ptr<GraphNode>> sProbes;
      auto it = sProbes.find(typeName);
      if (it != sProbes.end())
         return it->second.get();
      std::unique_ptr<GraphNode> gn;
      if (INode* made = NodeFactory::Instance().MakeNode(typeName))
      {
         gn = std::make_unique<GraphNode>();
         gn->node.reset(made);
         gn->typeName = typeName;
         gn->category = NodeFactory::Instance().CategoryOf(typeName);
      }
      GraphNode* raw = gn.get();
      sProbes[typeName] = std::move(gn);
      return raw;
   }


   const PatchSchema::TypeSchema* SchemaFor(const std::string& typeName)
   {
      static std::map<std::string, std::unique_ptr<PatchSchema::TypeSchema>> sSchemas;
      auto it = sSchemas.find(typeName);
      if (it != sSchemas.end())
         return it->second.get();
      GraphNode* gn = SchemaProbe(typeName);
      std::unique_ptr<PatchSchema::TypeSchema> t;
      if (gn != nullptr)
      {
         t = std::make_unique<PatchSchema::TypeSchema>();
         t->name = typeName;
         t->category = gn->category;
         SchemaParamRecorder rec(t->params);
         gn->node->VisitParams(rec);
         for (int slot = 0; slot < 64; slot++)
         {
            std::string kind;
            if (!SlotKindOf(*gn, slot, kind))
               continue;
            const char* label = gn->node->InputLabel(slot);
            t->inputs.push_back({ slot, kind, label != nullptr ? label : "" });
         }
         const int outs = std::max(1, gn->node->OutputCount());
         for (int o = 0; o < outs; o++)
         {
            const SrcCaps caps = CapsOf(gn, o);
            t->outputs.push_back({ gn->node->OutputLabel(o) != nullptr ? gn->node->OutputLabel(o) : "out",
                                   KindOfOutput(caps), caps.modulator || caps.predictor });
         }
         t->hardwareDriven = gn->node->IsHardwareDriven();
         t->canBypass = CanBypass(*gn);
      }
      const PatchSchema::TypeSchema* raw = t.get();
      sSchemas[typeName] = std::move(t);
      return raw;
   }


   PatchSchema::Env MakeSchemaEnv(bool forRender)
   {
      PatchSchema::Env env;
      env.schema = [](const std::string& n) { return SchemaFor(n); };
      for (const std::string& cat : NodeFactory::Instance().GetCategories())
         for (const std::string& n : NodeFactory::Instance().GetNodesInCategory(cat))
            env.allTypes.push_back(n);
      env.link = [](const std::string& srcType, int srcOut, const std::string& dstType, int dstSlot)
      {
         GraphNode* src = SchemaProbe(srcType);
         GraphNode* dst = SchemaProbe(dstType);
         if (src == nullptr || dst == nullptr)
            return PatchSchema::Link::Ok;
         std::string kind;
         if (!SlotKindOf(*dst, dstSlot, kind))
            return PatchSchema::Link::BadSlot;
         return CapsAcceptedBy(CapsOf(src, srcOut), dst, dstSlot) ? PatchSchema::Link::Ok : PatchSchema::Link::KindMismatch;
      };
      env.maxParamIndex = [](const std::string& type)
      {
         auto it = gModulatableMax.find(type);
         return it == gModulatableMax.end() ? -2 : it->second;
      };
      env.paramIndexOfKey = [](const std::string& type, const std::string& key)
      {
         auto it = gParamJoin.find(type);
         if (it == gParamJoin.end() || !it->second.done)
            return -2;
         auto k = it->second.paramOfKey.find(key);
         return k == it->second.paramOfKey.end() ? -1 : k->second;
      };
      env.modulatableKeys = [](const std::string& type)
      {
         std::vector<std::string> keys;
         auto it = gParamJoin.find(type);
         if (it != gParamJoin.end())
            for (const auto& kv : it->second.paramOfKey)
               keys.push_back(kv.first);
         return keys;
      };
      env.optionsOf = [](const std::string& type, const std::string& key)
      {
         auto it = gParamJoin.find(type);
         if (it == gParamJoin.end())
            return std::vector<std::string>();
         auto o = it->second.optionsOfKey.find(key);
         return o == it->second.optionsOfKey.end() ? std::vector<std::string>() : o->second;
      };
      env.forRender = forRender;
      return env;
   }


   // Snapshots every connection landing on a node in `indices`, split into the
   // three storage kinds a connection can live in (see docs/plans - plain
   // input slots, modulator->param bindings, palette->swatch bindings). Reads
   // gLinks for the first (safe: gLinks is rebuilt earlier in this same frame,
   // before any of copy/paste/duplicate run) and the live binding maps for the
   // other two, since those are gated on collapsed-param/color-pin visibility
   // and would silently miss a collapsed node's bindings if read from gLinks.
   //
   // Outbound-only connections (a cluster node feeding something outside the
   // cluster) are deliberately not captured: only links whose *destination*
   // is in `indices` are kept, since re-wiring an original's sole consumer
   // onto the copy instead would silently steal it.
   void CaptureClusterLinks(const std::set<int>& indices, ClusterClipboard& out)
   {
      std::vector<ClusterLink>& outLinks = out.links;
      std::vector<ClusterModLink>& outModLinks = out.modLinks;
      std::vector<ClusterPaletteLink>& outPaletteLinks = out.paletteLinks;
      outLinks.clear();
      outModLinks.clear();
      outPaletteLinks.clear();
      out.exprs.clear();
      out.gestures.clear();

      for (const LinkInfo& link : gLinks)
      {
         if (!GraphNode::IsInputPin(link.dstPin) || !GraphNode::IsOutputPin(link.srcPin))
            continue; // param/colour-pin links are read from the binding maps below instead
         const int dstIndex = GraphNode::NodeIndexFromPin(link.dstPin);
         if (!indices.count(dstIndex))
            continue;
         outLinks.push_back({ GraphNode::NodeIndexFromPin(link.srcPin),
                               GraphNode::OutputIndexFromPin(link.srcPin),
                               dstIndex, GraphNode::InputSlotFromPin(link.dstPin) });
      }
      for (const auto& entry : Modulation::Instance().Links())
      {
         if (!indices.count(entry.first.first))
            continue;
         outModLinks.push_back({ entry.first.first, entry.first.second, entry.second });
      }
      for (const auto& entry : PaletteBinding::Instance().Links())
      {
         if (!indices.count(entry.first.first))
            continue;
         outPaletteLinks.push_back({ entry.first.first, entry.first.second,
                                      entry.second.nodeIndex, entry.second.swatchIndex });
      }
      for (const auto& entry : Modulation::Instance().Expressions())
      {
         if (!indices.count(entry.first.first))
            continue;
         out.exprs.push_back({ entry.first.first, entry.first.second, entry.second });
      }
      for (const auto& entry : GestureRecorder::Instance().Playbacks())
      {
         if (!indices.count(entry.first.first))
            continue;
         out.gestures.push_back({ entry.first.first, entry.first.second, entry.second });
      }
   }


   // Rewires captured links onto the fresh copies. `newByOrig` maps each
   // original cluster index to its copy. A source that was itself copied is
   // rewired to the copy (internal topology preserved); a source outside the
   // cluster is rewired to the *original* external node, re-validated via
   // FindNodeByIndex since it may have been deleted since capture. Caller is
   // expected to already be inside a gSuppressUndoCheckpoints region so this
   // reads as one undo step alongside the spawn.
   void ApplyClusterLinks(const std::map<int, GraphNode*>& newByOrig, const ClusterClipboard& clip)
   {
      const std::vector<ClusterLink>& links = clip.links;
      const std::vector<ClusterModLink>& modLinks = clip.modLinks;
      const std::vector<ClusterPaletteLink>& paletteLinks = clip.paletteLinks;
      for (const ClusterLink& link : links)
      {
         auto dstIt = newByOrig.find(link.dstIndex);
         if (dstIt == newByOrig.end())
            continue;
         int resolvedSrcIndex = link.srcIndex;
         auto srcIt = newByOrig.find(link.srcIndex);
         if (srcIt != newByOrig.end())
            resolvedSrcIndex = srcIt->second->index;
         else if (FindNodeByIndex(link.srcIndex) == nullptr)
            continue;
         std::string err;
         ConnectNodes(resolvedSrcIndex, link.srcOutputIndex, dstIt->second->index, link.dstSlot, err);
      }
      for (const ClusterModLink& modLink : modLinks)
      {
         auto dstIt = newByOrig.find(modLink.dstIndex);
         if (dstIt == newByOrig.end())
            continue;
         Modulation::Source source = modLink.source;
         auto srcIt = newByOrig.find(source.nodeIndex);
         if (srcIt != newByOrig.end())
            source.nodeIndex = srcIt->second->index;
         else if (FindNodeByIndex(source.nodeIndex) == nullptr)
            continue;
         Modulation::Instance().RestoreLink(dstIt->second->index, modLink.paramIndex, source);
      }
      for (const ClusterPaletteLink& paletteLink : paletteLinks)
      {
         auto dstIt = newByOrig.find(paletteLink.dstIndex);
         if (dstIt == newByOrig.end())
            continue;
         int resolvedPaletteIndex = paletteLink.paletteOrigIndex;
         auto srcIt = newByOrig.find(paletteLink.paletteOrigIndex);
         if (srcIt != newByOrig.end())
            resolvedPaletteIndex = srcIt->second->index;
         else if (FindNodeByIndex(paletteLink.paletteOrigIndex) == nullptr)
            continue;
         PaletteBinding::Instance().Bind(dstIt->second->index, paletteLink.colorIndex,
                                          resolvedPaletteIndex, paletteLink.swatchIndex);
      }
      for (const ClusterExprLink& expr : clip.exprs)
      {
         auto dstIt = newByOrig.find(expr.dstIndex);
         if (dstIt != newByOrig.end())
            Modulation::Instance().SetExpression(dstIt->second->index, expr.paramIndex, expr.text);
      }
      for (const ClusterGestureLink& gesture : clip.gestures)
      {
         auto dstIt = newByOrig.find(gesture.dstIndex);
         if (dstIt != newByOrig.end())
            GestureRecorder::Instance().SetPlayback(dstIt->second->index, gesture.paramIndex,
                                                    gesture.playback);
      }
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
   std::vector<std::pair<std::string, std::string>> RecommendedNodeTypesForOutput(GraphNode* srcNode,
                                                                                    int srcOutputIndex)
   {
      std::vector<std::pair<std::string, std::string>> result;
      if (srcNode == nullptr)
         return result;

      const bool srcIsModulator =
         dynamic_cast<IModulator*>(srcNode->node.get()) != nullptr ||
         (srcNode->node->OutputCount() > 0 && srcNode->node->ModulatorOutput(0) != nullptr);
      auto* srcPalette = dynamic_cast<IPaletteSource*>(srcNode->node.get());
      auto* srcGeometry = dynamic_cast<IGeometrySource*>(srcNode->node.get());
      if (srcGeometry != nullptr && !srcGeometry->IsGeometryOutputIndex(srcOutputIndex))
         srcGeometry = nullptr;
      auto* srcCamera = dynamic_cast<CameraNode*>(srcNode->node.get());
      auto* srcLight = dynamic_cast<LightNode*>(srcNode->node.get());
      const bool srcIsEnvironment = dynamic_cast<EnvironmentNode*>(srcNode->node.get()) != nullptr;
      auto* srcAudioSource = dynamic_cast<IAudioSource*>(srcNode->node.get());
      const bool srcIsAudioNode = srcAudioSource != nullptr && srcAudioSource->IsAudioOutputIndex(srcOutputIndex);
      const bool srcIsNoteSource = dynamic_cast<INoteSource*>(srcNode->node.get()) != nullptr;
      // A Predictive LFO / Macro drives parameters only, so nothing in the
      // catalogue is a legal drop target for it - suggesting anything here
      // would offer a link IsInputSlotCompatible then refuses.
      const bool srcIsPredictor = dynamic_cast<IPredictor*>(srcNode->node.get()) != nullptr;

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
                                          srcCamera, srcLight, srcIsEnvironment,
                                          srcIsAudioNode, srcIsNoteSource, srcIsPredictor))
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


   GraphNode* SpawnNode(const std::string& typeName, const std::string& category,
                        float x, float y)
   {
      INode* node = NodeFactory::Instance().MakeNode(typeName);
      if (node == nullptr)
         return nullptr;

      PushUndoCheckpoint();

      GraphNode gn;
      gn.node.reset(node);
      gn.typeName = typeName;
      gn.category = category;
      gn.index = gNextIndex++;
      // Every node gets one, not just the ones an arrangement clip happens to
      // point at: a node can be added to the timeline at any later moment, and
      // a uid minted then would not be the one an older patch recorded.
      gn.uid = gNextNodeUid++;
      gn.spawnX = x;
      gn.spawnY = y;
      gNodes.push_back(std::move(gn));
      NoteNodeAppended();
      return &gNodes.back();
   }


   // File-backed and compiled-from-text nodes keep derived state (a loaded
   // texture, a compiled GL program) that VisitParams deliberately does not
   // touch - it declares settings, not the runtime side effects of restoring
   // them. Both patch load and copy/paste restore a node from bare settings,
   // so both call this afterwards rather than duplicating the same dynamic
   // casts in two places.
   //
   // Every undo/redo and multi-node delete respawns nodes from scratch and
   // routes through here, so the file-backed ReloadFromPath() calls below
   // (image/environment/model/audio) are what pays for re-decoding a source
   // file on every such respawn. See docs/plans/undo-delete-perf-prompt.md
   // Part B - AssetCache.h now sits behind those loaders so a respawn that
   // points at a file already decoded this session skips the decode, not
   // this dispatch.
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
      if (auto* slicer = dynamic_cast<SlicerNode*>(node))
         slicer->ReloadFromPath();
      if (auto* paul = dynamic_cast<PaulStretchNode*>(node))
         paul->ReloadFromPath();
      if (auto* molder = dynamic_cast<MolderNode*>(node))
         molder->ReloadFromPath();
      if (auto* gm = dynamic_cast<GrainMolderNode*>(node))
         gm->ReloadFromPath();
      if (auto* gran = dynamic_cast<GranularNode*>(node))
         gran->ReloadFromPath();
      if (auto* drum = dynamic_cast<DrumSequencerNode*>(node))
         drum->ReloadFromPaths();
      if (auto* mpc = dynamic_cast<MpcNode*>(node))
         mpc->ReloadFromPaths();
      if (auto* video = dynamic_cast<VideoSourceNode*>(node))
         video->ReloadFromPath();
      if (auto* palette = dynamic_cast<PaletteNode*>(node))
         palette->ReloadFromPath();
      if (auto* formula = dynamic_cast<FormulaNode*>(node))
         formula->Apply();
      if (auto* fe = dynamic_cast<FieldElementNode*>(node))
         fe->Apply();
      if (auto* fpn = dynamic_cast<FieldPrimitiveNode*>(node))
         fpn->Apply();
      if (auto* fgn = dynamic_cast<FieldGraphNode*>(node))
         fgn->Apply(); // compile-only (T11) - never Regenerate() from here
      if (auto* fp = dynamic_cast<FieldPixelNode*>(node))
         fp->Apply();
      if (auto* fs = dynamic_cast<FieldSampleNode*>(node))
         fs->Apply();
      if (auto* fsn = dynamic_cast<FieldSynthNode*>(node))
         fsn->Apply();
      // Re-instantiates the plugin from the identity VisitParams just restored
      // and queues its saved fullState; the actual load finishes
      // asynchronously, a frame or two later, in CookIfNeeded.
      if (auto* plugin = dynamic_cast<AudioPluginNode*>(node))
         plugin->ReloadFromIdentity();
   }
}
