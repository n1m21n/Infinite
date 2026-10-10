// Node registration, lookup, arrange model and undo gesture helpers (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   void RegisterNodes()
   {
#ifndef NDEBUG
      if (getenv("INFINITE_PREDBINDTEST") != nullptr)
         REGISTER_NODE(StubPredictorNode, Stub Predictor, "Modulators");
#endif
      REGISTER_NODE(ImageSourceNode, Image Source, "Source");
      REGISTER_NODE(SlideshowNode, Slideshow, "Source");
      REGISTER_NODE(ShapeNode, Shape, "Source");
      for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
      {
         NodeFactory::Instance().Register(
            ShapeNode::ShapeNames()[i],
            [i]() -> INode* { return ShapeNode::CreateFor(i); }, "Source");
      }
      REGISTER_NODE(FormulaNode, Formula, "Source");
      REGISTER_NODE(FieldPixelNode, FieldPixel, "Source");
      REGISTER_NODE(SketchNode, Sketch, "Source");
      REGISTER_NODE(TextNode, Text, "Source");
      REGISTER_NODE(VideoSourceNode, Video, "Source");
      REGISTER_NODE(VideoInNode, Video In, "Source");
      // Syphon In is a source (an image feed into the patch), but grouped
      // with Utility rather than Source - alongside Syphon Out and the other
      // app-to-app/IO nodes, not the generators and loaders Source holds.
      REGISTER_NODE(SyphonInNode, Syphon In, "Utility");
      REGISTER_NODE(NdiInNode, NDI In, "Utility");
      REGISTER_NODE(NoiseNode, Noise, "Source");
      REGISTER_NODE(TextureNode, Texture, "Source");
      REGISTER_NODE(RampNode, Ramp, "Source");
      REGISTER_NODE(GeometryNode, Geometry, "3D");
      // Every primitive as its own searchable node, sharing one class.
      for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
      {
         NodeFactory::Instance().Register(
            GeometryNode::ShapeNames()[i],
            [i]() -> INode* { return GeometryNode::CreateFor(i); }, "3D");
      }
      REGISTER_NODE(ModelSourceNode, Model 3D, "3D");
      REGISTER_NODE(Text3DNode, Text 3D, "3D");
      REGISTER_NODE(Null3DNode, Null 3D, "3D");
      REGISTER_NODE(OceanNode, Ocean, "3D");
      REGISTER_NODE(MaterialNode, Material, "3D");
      REGISTER_NODE(DisplacementNode, Displacement, "3D");
      REGISTER_NODE(AudioDisplacementNode, Audio Displacement, "3D");
      REGISTER_NODE(AudioRibbonNode, Audio Ribbon, "3D");
      REGISTER_NODE(MappingNode, Mapping, "3D");
      REGISTER_NODE(ParticleSystemNode, Particle System, "3D");
      REGISTER_NODE(ClothNode, Cloth, "3D");
      REGISTER_NODE(JoinGeometryNode, Join Geometry, "3D");
      // The boolean modes as their own nodes: "difference" is what gets
      // searched for, not "join geometry with a dropdown set to difference".
      for (int i = 1; i < JoinGeometryNode::kModeCount; i++)
      {
         NodeFactory::Instance().Register(
            JoinGeometryNode::ModeNames()[i],
            [i]() -> INode* { return JoinGeometryNode::CreateFor(i); }, "3D");
      }
      REGISTER_NODE(MetaBallNode, Metaballs, "3D");
      REGISTER_NODE(MeshResynthNode, Resynthesize 3D, "3D");
      REGISTER_NODE(ImageToPointsNode, Image to Points, "3D");
      REGISTER_NODE(DepthProjectionNode, Depth Projection, "3D");
      REGISTER_NODE(CurveNode, Curve, "3D");
      // Three names, one class - Points/Edges/Faces are the same sampler.
      for (int i = 0; i < 3; i++)
      {
         static const char* kNames[] = { "Mesh to Points", "Mesh to Edges", "Mesh to Faces" };
         NodeFactory::Instance().Register(
            kNames[i], [i]() -> INode* { return MeshToPointsNode::CreateFor(i); }, "3D");
      }
      // Named operator nodes, all backed by GeometryOpNode. All kOpCount
      // ops stay registered with NodeFactory - old patches resolve
      // "Delete Selected" etc. by name, and internal test fixtures spawn
      // nodes the same way NodeFactory does. GeometryOpNode::IsSpawnable
      // instead filters the three deprecated `*Selected` ops out of the
      // interactive spawn popup and the operation dropdown (see the spawn
      // popup's category loop and DrawGeometryOpParams), so a user can no
      // longer choose one going forward while everything that names one
      // programmatically keeps working.
      for (int i = 0; i < GeometryOpNode::kOpCount; i++)
      {
         NodeFactory::Instance().Register(
            GeometryOpNode::OpNames()[i],
            [i]() -> INode* { return GeometryOpNode::CreateFor(i); },
            "3D");
      }
      REGISTER_NODE(InstanceOnPointsNode, Instance on Points, "3D");
      REGISTER_NODE(SetColorNode, Set Vertex Color, "3D");
      REGISTER_NODE(WrapNode, Wrap, "3D");
      REGISTER_NODE(FieldElementNode, Field Modifier, "3D");
      REGISTER_NODE(FieldPrimitiveNode, Field Primitive, "3D");
      REGISTER_NODE(Sketch3DNode, Sketch 3D, "3D");
      REGISTER_NODE(DistributePointsOnFacesNode, Distribute Points on Faces, "3D");
      REGISTER_NODE(PointsToVerticesNode, Points to Vertices, "3D");
      REGISTER_NODE(DistributePointsInGridNode, Distribute Points in Grid, "3D");
      REGISTER_NODE(MergeByDistanceNode, Merge by Distance, "3D");
      REGISTER_NODE(Switcher3DNode, Switcher 3D, "3D");
      REGISTER_NODE(Group3DNode, Group 3D, "3D");
      REGISTER_NODE(CameraNode, Camera, "3D");
      REGISTER_NODE(LightNode, Light, "3D");
      REGISTER_NODE(EnvironmentNode, HDRI, "3D");
      REGISTER_NODE(Render3DNode, Render 3D, "3D");
      REGISTER_NODE(DrawNode, Draw, "Source");
      // Resynth merged into Effects - see CategoryColors.h/.cpp; it was
      // always a single-node category ("a video resynthesis family"), and
      // Effects is what a user reaches for it under.
      REGISTER_NODE(ResynthNode, Resynthesize, "Effects");
      REGISTER_NODE(FitNode, Fit, "Compositing");
      REGISTER_NODE(CommentNode, Comment, "Compositing");
      REGISTER_NODE(GroupNode, Group, "Compositing");
      REGISTER_NODE(NullNode, Null, "Compositing");
      REGISTER_NODE(ViewportNode, Viewport, "Compositing");
      // Color, Mask and Feedback all merged into Compositing - each was a
      // small category whose nodes are, in practice, ways of combining or
      // managing image flow, same as Compositing's original members.
      REGISTER_NODE(CurvesNode, Curves, "Compositing");
      REGISTER_NODE(ColorRampNode, Color Ramp, "Compositing");
      REGISTER_NODE(RemoveBgNode, Remove Background, "Compositing");
      REGISTER_NODE(FeedbackNode, Feedback, "Compositing");
      REGISTER_NODE(TrailsNode, Trails, "Compositing");
      REGISTER_NODE(ReactionDiffusionNode, Reaction Diffusion, "Compositing");
      REGISTER_NODE(BlendNode, Blend, "Compositing");
      REGISTER_NODE(LayerStackNode, Layer Stack, "Compositing");
      REGISTER_NODE(SwitcherNode, Switcher, "Compositing");
      // Output/AudioUtility/OSC all merged into Utility below - the node's
      // own display name stays "Output" (not renamed to "Export") since
      // Patch.cpp looks nodes up by exact typeName on load; renaming it
      // would silently drop the node from any already-saved patch.
      REGISTER_NODE(OutputNode, Output, "Utility");
      REGISTER_NODE(SyphonOutNode, Syphon Out, "Utility");
      REGISTER_NODE(NdiOutNode, NDI Out, "Utility");
      REGISTER_NODE(ProjectionNode, Projection, "Utility");
      REGISTER_NODE(LFONode, LFO, "Modulators");
      REGISTER_NODE(RandomNode, Random, "Modulators");
      REGISTER_NODE(PatternNode, Pattern, "Modulators");
      REGISTER_NODE(MathNode, Math, "Modulators");
      REGISTER_NODE(CompareNode, Compare, "Modulators");
      REGISTER_NODE(RangeToRangeNode, Range to Range, "Modulators");
      REGISTER_NODE(SmoothNode, Smoothing, "Modulators");
      REGISTER_NODE(InvertNode, Invert, "Modulators");
      REGISTER_NODE(ModDepthNode, Mod Depth, "Modulators");
      REGISTER_NODE(ModCurveNode, Mod Curve, "Modulators");
      REGISTER_NODE(DriftNode, Drift, "Prediction");
      REGISTER_NODE(MovesNode, Moves, "Prediction");
      REGISTER_NODE(PredictiveModulatorNode, Predictive Modulator, "Prediction");
      REGISTER_NODE(PredictiveColoringNode, Predictive Coloring, "Prediction");
      REGISTER_NODE(CVToPitchNode, CV to Pitch, "Modulators");
      REGISTER_NODE(MacroKnobNode, Macro Knob, "Macros");
      REGISTER_NODE(MacroSliderNode, Macro Slider, "Macros");
      REGISTER_NODE(MacroBipolarKnobNode, Macro Bipolar Knob, "Macros");
      REGISTER_NODE(MacroXYNode, Macro XY, "Macros");
      REGISTER_NODE(MacroToggleNode, Macro Toggle, "Macros");
      REGISTER_NODE(MacroTriggerNode, Macro Trigger, "Macros");
      REGISTER_NODE(MacroNumBoxNode, Macro NumBox, "Macros");
      REGISTER_NODE(MacroRadioSelectorNode, Macro Radio Selector, "Macros");
      REGISTER_NODE(MacroStepGateNode, Macro Step Gate, "Macros");
      REGISTER_NODE(MidiCCNode, MIDI CC, "Modulators");
      REGISTER_NODE(MidiTriggerNode, MIDI Trigger, "Modulators");
      REGISTER_NODE(PathNode, Path, "Modulators");
      REGISTER_NODE(GeometryTableNode, Geometry Table, "Modulators");
      REGISTER_NODE(ConstantNode, Constant, "Modulators");
      REGISTER_NODE(NullModulatorNode, Null Modulator, "Modulators");
      REGISTER_NODE(ImageAnalyzeNode, Image Analyze, "Modulators");
      REGISTER_NODE(HandTrackNode, Hand Track, "Modulators");
      REGISTER_NODE(PaletteNode, Palette, "Modulators");
      REGISTER_NODE(AudioFileNode, Audio File, "Modulators");
      REGISTER_NODE(AudioAnalyzeNode, Audio Analyze, "Modulators");
      REGISTER_NODE(OscReceiveNode, OSC Receive, "Utility");
      REGISTER_NODE(OscSendNode, OSC Send, "Utility");

      // P2 audio-graph proof nodes - see docs/plans/audio/README.md P2.
      // Category names must stay one word: Patch.cpp's
      // "node <index> <category> <typeName>" line reads category with `>>`
      // (a single whitespace-delimited token), and a space in one corrupts
      // the save format (confirmed: it silently ate the type name on load).
      REGISTER_NODE(OscillatorNode, Oscillator, "Synths");
      REGISTER_NODE(WavetableNode, Wavetable, "Synths");
      REGISTER_NODE(AnalogNode, Analog, "Synths");
      REGISTER_NODE(WaveTerrainNode, Wave Terrain, "Synths");
      REGISTER_NODE(EquationNode, Equation Synth, "Synths");
      REGISTER_NODE(ImageSpectralSynthNode, Spectral Synth, "Synths");
      REGISTER_NODE(MetallicNode, Metallic, "Synths");
      REGISTER_NODE(SamplerNode, Sampler, "Synths");
      REGISTER_NODE(SlicerNode, Slicer, "Synths");
      REGISTER_NODE(PaulStretchNode, PaulStretch, "Synths");
      REGISTER_NODE(MolderNode, Molder, "Synths");
      REGISTER_NODE(GrainMolderNode, Grain Molder, "Synths");
      REGISTER_NODE(GranularNode, Granular, "Synths");
      REGISTER_NODE(FieldSynthNode, Field Synth, "Synths");
      REGISTER_NODE(FieldSampleNode, Field Effect, "AudioEffects");
      // Field Graph: registered so NodeFactory::MakeNode still resolves it
      // for loading a patch saved before this pass, but excluded from every
      // spawn-menu/search enumeration via IsUserSpawnable below - it can no
      // longer be newly created, per the device-catalog simplification.
      REGISTER_NODE(FieldGraphNode, Field Graph, "Utility");
      REGISTER_NODE(DrumSequencerNode, Drum Sequencer, "Synths");
      REGISTER_NODE(MpcNode, MPC, "Synths");
      REGISTER_NODE(LooperNode, Looper, "Synths");
      // Third-party plugin hosting (Audio Units). Its params reach the plugin
      // directly rather than through ParamMailbox - see AudioPluginNode.h.
      REGISTER_NODE(AudioPluginNode, Plugin, "AudioEffects");
      // AudioUtility folded into Utility, alongside Output/Projection/Syphon/
      // OSC above - see CategoryColors.h/.cpp.
      REGISTER_NODE(GainNode, Gain, "Utility");
      REGISTER_NODE(AudioMeterNode, Audio Meter, "Utility");
      REGISTER_NODE(AudioInputNode, Audio In, "Utility");
      REGISTER_NODE(AudioOutputNode, Audio Out, "Utility");
      // P2.8 routing nodes - the system's only summing/fan-out points, see
      // docs/plans/audio/audio-graph-semantics.md §1/§2.
      REGISTER_NODE(MixerNode, Mixer, "Utility");
      REGISTER_NODE(SpatialMixerNode, Spatial Mixer, "Utility");
      REGISTER_NODE(SplitterNode, Splitter, "Utility");
      // "Blend Audio" not "Blend" - that name is taken by the image
      // compositing node (REGISTER_NODE(BlendNode, Blend, "Compositing") above).
      REGISTER_NODE(BlendAudioNode, Blend Audio, "Utility");
      // The "Audio" category (just these two nodes) is gone - Audio Texture
      // is a source (a waveform/spectrum image generator), Audio Color Ramp
      // is a way of coloring based on audio, i.e. compositing.
      REGISTER_NODE(AudioTextureNode, Audio Texture, "Source");
      REGISTER_NODE(AudioColorRampNode, Audio Color Ramp, "Compositing");

      // P3a Part 1 - note-transport proving nodes. See
      // docs/plans/audio/P3a-notes-prompt.md; Part 2 adds Note Filter/
      // Modify/Echo/Router/Display and the Arpeggiator.
      REGISTER_NODE(MidiNotesNode, MIDI Notes, "Notes");
      // The hardware-free note source: on-screen piano plus a QWERTY typing
      // mapping, for playing/testing a patch with no MIDI controller at all.
      REGISTER_NODE(KeyboardNode, Keyboard, "Notes");
      REGISTER_NODE(NoteFilterNode, Note Filter, "Notes");
      // The note-modification surface, one concern per node - see the class
      // comments on their declarations in NoteNodes.h.
      REGISTER_NODE(NoteTransposeNode, Note Transpose, "Notes");
      REGISTER_NODE(PitchBendNode, Pitch Bend, "Notes");
      REGISTER_NODE(VelocityCurveNode, Velocity Curve, "Notes");
      REGISTER_NODE(GateNode, Gate, "Notes");
      REGISTER_NODE(HumanizerNode, Humanizer, "Notes");
      REGISTER_NODE(QuantizerNode, Quantizer, "Notes");
      REGISTER_NODE(GlideNode, Glide, "Notes");
      REGISTER_NODE(VibratoNode, Vibrato, "Modulators");
      REGISTER_NODE(NoteEchoNode, Note Echo, "Notes");
      REGISTER_NODE(PredictiveNotesNode, Predictive Notes, "Prediction");
      REGISTER_NODE(PredictiveQuantizeNode, Predictive Quantize, "Prediction");
      REGISTER_NODE(PredictiveVelocityNode, Predictive Velocity, "Prediction");
      REGISTER_NODE(PredictiveRhythmNode, Predictive Rhythm, "Prediction");
      REGISTER_NODE(NoteRouterNode, Note Router, "Notes");
      REGISTER_NODE(NoteMergeNode, Note Merge, "Notes");
      REGISTER_NODE(NoteSwitcherNode, Note Switcher, "Notes");
      REGISTER_NODE(ArpeggiatorNode, Arpeggiator, "Notes");
      REGISTER_NODE(NoteSequencerNode, Note Sequencer, "Notes");
      REGISTER_NODE(RandomNoteGeneratorNode, Random Note Generator, "Notes");
      REGISTER_NODE(MidiFileNode, MIDI File, "Notes");
      REGISTER_NODE(ChorderNode, Chorder, "Notes");
      REGISTER_NODE(NoteStackNode, Note Stack, "Notes");
      REGISTER_NODE(NoteCapturerNode, Note Capturer, "Notes");
      REGISTER_NODE(BouncingBallsNode, Bouncing Balls, "Notes");
      REGISTER_NODE(NoteStrumNode, Note Strum, "Notes");
      REGISTER_NODE(EnvelopeNode, Envelope, "Modulators");
      REGISTER_NODE(NoteToCVNode, Note to CV, "Modulators");
      REGISTER_NODE(VelocityToCVNode, Velocity to CV, "Modulators");
      REGISTER_NODE(CVRecorderNode, CV Recorder, "Modulators");
      REGISTER_NODE(AudioToCVNode, Audio to CV, "Modulators");

      // P3c effects - one AudioEffectNode class serves every EffectDef table
      // entry, the same pattern the filter-table loop above uses for
      // FilterNode/FilterDef. See docs/plans/audio/P3c-P3a2-design.md §0.4.
      for (const EffectDef& def : GetEffectDefs())
      {
         const EffectDef* defPtr = &def;
         NodeFactory::Instance().Register(
            def.name,
            [defPtr]() -> INode* { return AudioEffectNode::CreateFor(*defPtr); },
            def.category);
      }

      // Every entry in the filter table becomes its own spawnable node type,
      // all sharing FilterNode. `def` is a reference into the static table, so
      // capturing it by pointer is safe for the process lifetime.
      for (const FilterDef& def : GetFilterDefs())
      {
         const FilterDef* defPtr = &def;
         NodeFactory::Instance().Register(
            def.name,
            [defPtr]() -> INode* { return FilterNode::CreateFor(*defPtr); },
            def.category);
      }
   }


   IModulator* ModulatorForOutput(INode* node, int outputIndex)
   {
      if (node == nullptr)
         return nullptr;
      if (IModulator* specific = node->ModulatorOutput(outputIndex))
         return specific;
      return outputIndex == 0 ? dynamic_cast<IModulator*>(node) : nullptr;
   }


   GraphNode* FindNodeByIndex(int index)
   {
      for (GraphNode& gn : gNodes)
      {
         if (gn.index == index)
            return &gn;
      }
      return nullptr;
   }


   void InvalidateNodeByUid()
   {
      gNodeByUidDirty = true;
      // Every gNodes erase/clear/reload site already reports here, and so
      // does the main loop once a frame - the title-instance cache
      // (GetNodeInstanceIndex) wants exactly the same notifications.
      InvalidateNodeTitleInstances();
   }


   void RebuildNodeByUid()
   {
      gNodeByUid.clear();
      gNodeByUid.reserve(gNodes.size());
      for (GraphNode& gn : gNodes)
         if (gn.uid != 0)
            gNodeByUid.emplace(gn.uid, &gn);
      gNodeByUidData = gNodes.data();
      gNodeByUidSize = gNodes.size();
      gNodeByUidDirty = false;
   }


   // After gNodes.push_back: one emplace when nothing else moved, otherwise
   // leave it to the next lookup's rebuild (a reallocating push moved every
   // node).
   void NoteNodeAppended()
   {
      if (!gNodeByUidDirty && gNodes.data() == gNodeByUidData && gNodes.size() == gNodeByUidSize + 1)
      {
         GraphNode& gn = gNodes.back();
         if (gn.uid != 0)
            gNodeByUid.emplace(gn.uid, &gn);
         gNodeByUidSize = gNodes.size();
      }
      else
      {
         gNodeByUidDirty = true;
      }
   }


   void NoteNodeUidChanged(uint64_t oldUid, GraphNode* gn)
   {
      if (gNodeByUidDirty)
         return;
      auto it = gNodeByUid.find(oldUid);
      if (it != gNodeByUid.end() && it->second == gn)
         gNodeByUid.erase(it);
      if (gn->uid != 0)
         gNodeByUid.emplace(gn->uid, gn);
   }


   GraphNode* FindNodeByUid(uint64_t uid)
   {
      if (uid == 0)
         return nullptr;
      if (gNodeByUidDirty || gNodes.data() != gNodeByUidData || gNodes.size() != gNodeByUidSize)
         RebuildNodeByUid();
      auto it = gNodeByUid.find(uid);
      if (it == gNodeByUid.end())
         return nullptr;
      if (it->second->uid != uid)
      {
         // Something rewrote a uid in place without reporting it. Rebuild once
         // rather than hand back the wrong node.
         RebuildNodeByUid();
         it = gNodeByUid.find(uid);
         return it == gNodeByUid.end() ? nullptr : it->second;
      }
      return it->second;
   }


   // Every panel edit ends here: mark the document dirty. The undo push is the
   // caller's (see ArrangeEdit below). Nothing is republished - the audio
   // schedule, video layers and panel all read gArrange directly, and the
   // audio side notices the edit through gArrange.revision (WP5b).
   void ArrangeCommitEdit()
   {
      gPatchDirty = true;
   }


   void ArrangeGestureBegin()
   {
      // Two gestures never overlap (a lost deactivate would otherwise leave
      // the older snapshot open and fold the next edit into it).
      if (gArrangeGestureOpen)
         ArrangeGestureEnd();
      gArrangeGestureBefore = gArrange;
      gArrangeGestureOpen = true;
   }


   // Whether two models hold the same document content (lanes, clips,
   // markers, loop) - revision and nextId aside. A gesture that went away and
   // came back bumps revision without changing anything, and must not leave
   // an undo entry behind.
   bool ArrangeContentEqual(const Arrange::Model& a, const Arrange::Model& b)
   {
      if (a.lanes.size() != b.lanes.size() || a.markers.size() != b.markers.size() ||
          a.trackGroups.size() != b.trackGroups.size())
         return false;
      for (size_t i = 0; i < a.lanes.size(); i++)
      {
         const Arrange::Lane& la = a.lanes[i];
         const Arrange::Lane& lb = b.lanes[i];
         if (la.id != lb.id || la.type != lb.type || la.blendMode != lb.blendMode || la.opacity != lb.opacity ||
             la.gainDb != lb.gainDb || la.pan != lb.pan || la.enabled != lb.enabled || la.groupId != lb.groupId ||
             la.mute != lb.mute || la.solo != lb.solo || la.rowHeight != lb.rowHeight ||
             la.name != lb.name || la.clips.size() != lb.clips.size())
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
                ca.retrigger != cb.retrigger ||
                // Sample-dropped/BPM-sync fields (step 3) - previously
                // missing here entirely, which meant a gesture-based Sample
                // BPM/sync-toggle edit could be silently dropped as "no
                // change" by ArrangeGestureEnd instead of pushing an undo
                // step.
                ca.sampleDropped != cb.sampleDropped || ca.syncToTempo != cb.syncToTempo ||
                ca.sampleBpm != cb.sampleBpm || ca.origBpm != cb.origBpm ||
                ca.sourceDurationSeconds != cb.sourceDurationSeconds ||
                ca.sourceOffsetSeconds != cb.sourceOffsetSeconds ||
                // Per-clip modulation bypass: without this a bypass toggle
                // compares equal and ArrangeGestureEnd drops it as "no
                // change", so it would never reach the undo stack - the same
                // trap the Sample BPM fields above fell into.
                ca.bypassedModParams != cb.bypassedModParams)
               return false;
         }
      }
      for (size_t i = 0; i < a.markers.size(); i++)
         if (a.markers[i].id != b.markers[i].id || a.markers[i].pos != b.markers[i].pos ||
             a.markers[i].name != b.markers[i].name || a.markers[i].color != b.markers[i].color)
            return false;
      for (size_t i = 0; i < a.trackGroups.size(); i++)
      {
         const Arrange::TrackGroup& ga = a.trackGroups[i];
         const Arrange::TrackGroup& gb = b.trackGroups[i];
         if (ga.id != gb.id || ga.name != gb.name || ga.color != gb.color || ga.enabled != gb.enabled ||
             ga.collapsed != gb.collapsed || ga.parentGroupId != gb.parentGroupId)
            return false;
      }
      return a.settings.loop.enabled == b.settings.loop.enabled && a.settings.loop.start == b.settings.loop.start &&
             a.settings.loop.end == b.settings.loop.end;
   }


   // Returns whether an undo entry was pushed.
   bool ArrangeGestureEnd()
   {
      if (!gArrangeGestureOpen)
         return false;
      gArrangeGestureOpen = false;
      if (gArrangeGestureBefore.revision == gArrange.revision)
         return false;
      if (ArrangeContentEqual(gArrangeGestureBefore, gArrange))
         return false;
      PushArrangeUndoSnapshot(gArrangeGestureBefore);
      ArrangeCommitEdit();
      return true;
   }


   // Transport's loop <- gArrange.settings.loop. The model holds the loop in
   // ticks and the panel edits it there; Transport runs in beats, so this is
   // the one place the two meet. Called after every loop edit and after
   // anything replaces the model wholesale (load, undo, New) - otherwise the
   // loop the user saved comes back as a band on screen playback ignores.
   void PublishArrangeLoop()
   {
      const Arrange::LoopRange& loop = gArrange.settings.loop;
      Transport::Instance().SetLoop(loop.enabled, Arrange::TicksToBeats(loop.start),
                                    Arrange::TicksToBeats(loop.end));
   }


   // Every loop edit (toggle, the toolbar fields, ruler Shift-drag, ruler
   // right-click). The loop is model state, so a change bumps revision like
   // any other direct field edit - revision is the one change signal - and
   // dirties the document; then Transport hears about it. Not an undo step,
   // same as before WP5b.
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


   // The loop in seconds at the live tempo - for the seconds-native panel
   // geometry and the render range, which stays in seconds until WP7.
   double ArrangeLoopStartSec()
   {
      return Arrange::TicksToSeconds(gArrange.settings.loop.start, (double)Transport::Instance().Tempo());
   }

   double ArrangeLoopEndSec()
   {
      return Arrange::TicksToSeconds(gArrange.settings.loop.end, (double)Transport::Instance().Tempo());
   }

   ArrangeViewSettings ArrangeKeepViewSettings(const Arrange::Model& m)
   {
      ArrangeViewSettings v;
      v.dockSide = m.settings.dockSide;
      v.timeDisplay = m.settings.timeDisplay;
      v.snapDivision = m.settings.snapDivision;
      v.snapTriplet = m.settings.snapTriplet;
      return v;
   }

   void ArrangeRestoreViewSettings(Arrange::Model& m, const ArrangeViewSettings& v)
   {
      m.settings.dockSide = v.dockSide;
      m.settings.timeDisplay = v.timeDisplay;
      m.settings.snapDivision = v.snapDivision;
      m.settings.snapTriplet = v.snapTriplet;
   }


   // Bars | Time (Settings::timeDisplay). A view change only - every
   // position stays in ticks. Saved with the patch, so a direct model write:
   // revision++ (invariant 6) and dirty, never an undo step.
   void ArrangeSetTimeDisplay(int mode)
   {
      mode = mode == 1 ? 1 : 0;
      if (gArrange.settings.timeDisplay == mode)
         return;
      gArrange.settings.timeDisplay = mode;
      gArrange.revision++;
      gPatchDirty = true;
   }


   // Snap grid (Settings::snapDivision / snapTriplet, 0 = off). Same shape
   // as ArrangeSetTimeDisplay.
   void ArrangeSetSnap(int division, bool triplet)
   {
      division = std::clamp(division, 0, 64);
      if (division <= 1)
         triplet = false; // no bar or off triplet
      if (division > 0)
         gArrangeLastSnapDivision = division;
      Arrange::Settings& st = gArrange.settings;
      if (st.snapDivision == division && st.snapTriplet == triplet)
         return;
      st.snapDivision = division;
      st.snapTriplet = triplet;
      gArrange.revision++;
      gPatchDirty = true;
   }


   // The live snap step in ticks, 0 when snap is off. A bar follows the
   // transport's meter, same as the ruler's bar lines.
   Arrange::Tick ArrangeSnapGridTicks()
   {
      return Arrange::SnapGridTicks(gArrange.settings.snapDivision, gArrange.settings.snapTriplet,
                                    std::max(1.0, Transport::Instance().BeatsPerBar()));
   }


   // The step the arrow keys nudge by: the snap grid, or a sixteenth when
   // snap is off (so the keys never go dead).
   Arrange::Tick ArrangeNudgeStepTicks()
   {
      const Arrange::Tick g = ArrangeSnapGridTicks();
      return g > 0 ? g : Arrange::kPPQ / 4;
   }


   // The playhead in ticks - off Transport::Beats(), never Seconds(): the
   // beat clock is what the clips are laid out on, and seconds-to-ticks at
   // the live tempo drifts from it the moment a tempo change is staged.
   Arrange::Tick ArrangePlayTick()
   {
      return std::clamp<Arrange::Tick>(Arrange::BeatsToTicks(Transport::Instance().Beats()), 0, Arrange::kMaxTick);
   }


   void ArrangeSeekTick(Arrange::Tick t)
   {
      Transport::Instance().SeekBeats(Arrange::TicksToBeats(std::clamp<Arrange::Tick>(t, 0, Arrange::kMaxTick)));
   }


   // Ruler scrub (WP6). Begin/Update only move the ghost; End seeks exactly
   // once. Cancel drops the ghost without seeking.
   void ArrangeScrubBegin(Arrange::Tick t, ImGuiMouseButton button)
   {
      gArrangeScrubbing = true;
      gArrangeScrubButton = button;
      gArrangeScrubTick = std::clamp<Arrange::Tick>(t, 0, Arrange::kMaxTick);
   }

   void ArrangeScrubUpdate(Arrange::Tick t)
   {
      if (gArrangeScrubbing)
         gArrangeScrubTick = std::clamp<Arrange::Tick>(t, 0, Arrange::kMaxTick);
   }

   bool ArrangeScrubEnd()
   {
      if (!gArrangeScrubbing)
         return false;
      gArrangeScrubbing = false;
      ArrangeSeekTick(gArrangeScrubTick);
      return true;
   }

   void ArrangeScrubCancel()
   {
      gArrangeScrubbing = false;
   }


   // Where End sends the playhead: the end of the last clip on any lane.
   Arrange::Tick ArrangeEndKeyTargetTick()
   {
      return Arrange::ArrangementEnd(gArrange);
   }


   // Marker colours are RGBA8 packed 0xRRGGBBAA (Arrange::Marker::color).
   ImU32 ArrangeMarkerColU32(uint32_t rgba)
   {
      return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
   }

   uint32_t ArrangeMarkerRGBA(ImU32 col)
   {
      const uint32_t r = (col >> IM_COL32_R_SHIFT) & 0xFF;
      const uint32_t g = (col >> IM_COL32_G_SHIFT) & 0xFF;
      const uint32_t b = (col >> IM_COL32_B_SHIFT) & 0xFF;
      const uint32_t a = (col >> IM_COL32_A_SHIFT) & 0xFF;
      return (r << 24) | (g << 16) | (b << 8) | a;
   }


   // `M`: a marker at the playhead, on the snap grid when snap is on. One
   // undo entry. Returns the new id.
   uint64_t ArrangeAddMarkerAtPlayhead()
   {
      Arrange::Tick at = ArrangePlayTick();
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


   // Alt+Left / Alt+Right. While playing, a marker the playhead passed less
   // than half a beat ago counts as "here", so a Prev press still steps back
   // past it instead of landing on it again. Returns whether it seeked.
   bool ArrangeJumpToMarker(int dir)
   {
      const Arrange::Tick play = ArrangePlayTick();
      const Arrange::Marker* mk = nullptr;
      if (dir < 0)
         mk = Arrange::PrevMarker(gArrange, play, Transport::Instance().IsPlaying() ? Arrange::kPPQ / 2 : 0);
      else
         mk = Arrange::NextMarker(gArrange, play, 0);
      if (mk == nullptr)
         return false;
      ArrangeSeekTick(mk->pos);
      return true;
   }
}
