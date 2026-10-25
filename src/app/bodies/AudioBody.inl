// Part of src/main.cpp, included there in order (one translation unit).
// DrawAudioNodeBody: the audio body dispatcher

   void DrawAudioNodeBody(GraphNode& gn)
   {
      if (auto* n = dynamic_cast<LooperNode*>(gn.node.get()))
         DrawLooperBody(gn, n);
      else if (auto* n = dynamic_cast<TransportControlNode*>(gn.node.get()))
         DrawTransportControlBody(gn, n);
      else if (auto* n = dynamic_cast<ChordProgressionNode*>(gn.node.get()))
         DrawChordProgressionBody(gn, n);
      else if (auto* n = dynamic_cast<MidiFileNode*>(gn.node.get()))
         DrawMidiFileBody(gn, n); // Turbo 0.48
      else if (auto* n = dynamic_cast<GrainMolderNode*>(gn.node.get()))
         DrawGrainMolderBody(gn, n);
      else if (auto* n = dynamic_cast<MolderNode*>(gn.node.get()))
         DrawMolderBody(gn, n);
      else if (auto* n = dynamic_cast<SlicerNode*>(gn.node.get()))
         DrawSlicerBody(gn, n);
      else if (auto* n = dynamic_cast<AnalogNode*>(gn.node.get()))
         DrawAnalogBody(gn, n);
      else if (auto* n = dynamic_cast<MpcNode*>(gn.node.get()))
         DrawMpcBody(gn, n);
      else if (auto* n = dynamic_cast<MpcOutNode*>(gn.node.get()))
         DrawMpcOutBody(gn, n);
      else if (auto* n = dynamic_cast<SuperMixerNode*>(gn.node.get()))
         DrawSuperMixerBody(gn, n);
      else if (auto* n = dynamic_cast<OscillatorNode*>(gn.node.get()))
         DrawOscillatorBody(gn, n);
      else if (auto* n = dynamic_cast<WavetableNode*>(gn.node.get()))
         DrawWavetableBody(gn, n);
      else if (auto* n = dynamic_cast<WaveTerrainNode*>(gn.node.get()))
         DrawWaveTerrainBody(gn, n);
      else if (auto* n = dynamic_cast<EquationNode*>(gn.node.get()))
         DrawEquationBody(gn, n);
      else if (auto* n = dynamic_cast<ImageSpectralSynthNode*>(gn.node.get()))
         DrawImageSpectralSynthBody(gn, n);
      else if (auto* n = dynamic_cast<MetallicNode*>(gn.node.get()))
         DrawMetallicBody(gn, n);
      else if (auto* n = dynamic_cast<CircuitSynthNode*>(gn.node.get()))
         DrawCircuitSynthBody(gn, n); // Turbo 0.73
      else if (auto* n = dynamic_cast<FMNode*>(gn.node.get()))
         DrawFMBody(gn, n);
      else if (auto* n = dynamic_cast<PartialsNode*>(gn.node.get()))
         DrawPartialsBody(gn, n);
      else if (auto* n = dynamic_cast<StringNode*>(gn.node.get()))
         DrawStringBody(gn, n);
      else if (auto* n = dynamic_cast<AudioPluginNode*>(gn.node.get()))
         DrawPluginBody(gn, n);
      else if (auto* n = dynamic_cast<SamplerNode*>(gn.node.get()))
         DrawSamplerBody(gn, n);
      else if (auto* n = dynamic_cast<PaulStretchNode*>(gn.node.get()))
         DrawPaulStretchBody(gn, n);
      else if (auto* n = dynamic_cast<GranularNode*>(gn.node.get()))
         DrawGranularBody(gn, n);
      else if (auto* n = dynamic_cast<DrumSequencerNode*>(gn.node.get()))
         DrawDrumSequencerBody(gn, n);
      else if (auto* n = dynamic_cast<GainNode*>(gn.node.get()))
         DrawGainBody(gn, n);
#if INFINITE_GPL
      else if (auto* n = dynamic_cast<PunchFxNode*>(gn.node.get()))
         DrawPunchFxBody(gn, n);
      else if (auto* n = dynamic_cast<SynthRefNode*>(gn.node.get()))
         DrawSynthRefBody(gn, n);
#endif
      else if (auto* n = dynamic_cast<AudioMeterNode*>(gn.node.get()))
         DrawAudioMeterBody(gn, n);
      else if (auto* n = dynamic_cast<MixerNode*>(gn.node.get()))
         DrawMixerBody(gn, n);
      else if (auto* n = dynamic_cast<SplitterNode*>(gn.node.get()))
         DrawSplitterBody(gn, n);
      else if (auto* n = dynamic_cast<BlendAudioNode*>(gn.node.get()))
         DrawBlendAudioBody(gn, n);
      else if (auto* n = dynamic_cast<AudioInputNode*>(gn.node.get()))
         DrawAudioInBody(gn, n);
      else if (auto* n = dynamic_cast<AudioOutputNode*>(gn.node.get()))
         DrawAudioOutBody(gn, n);
      else if (auto* n = dynamic_cast<MidiNotesNode*>(gn.node.get()))
         DrawMidiNotesBody(gn, n);
      else if (auto* n = dynamic_cast<NoteFilterNode*>(gn.node.get()))
         DrawNoteFilterBody(gn, n);
      else if (auto* n = dynamic_cast<NoteTransposeNode*>(gn.node.get()))
         DrawNoteTransposeBody(gn, n);
      else if (auto* n = dynamic_cast<PitchBendNode*>(gn.node.get()))
         DrawPitchBendBody(gn, n);
      else if (auto* n = dynamic_cast<VelocityCurveNode*>(gn.node.get()))
         DrawVelocityCurveBody(gn, n);
      else if (auto* n = dynamic_cast<GateNode*>(gn.node.get()))
         DrawGateBody(gn, n);
      else if (auto* n = dynamic_cast<HumanizerNode*>(gn.node.get()))
         DrawHumanizerBody(gn, n);
      else if (auto* n = dynamic_cast<QuantizerNode*>(gn.node.get()))
         DrawQuantizerBody(gn, n);
      else if (auto* n = dynamic_cast<GlideNode*>(gn.node.get()))
         DrawGlideBody(gn, n);
      else if (auto* n = dynamic_cast<VibratoNode*>(gn.node.get()))
         DrawVibratoBody(gn, n);
      else if (auto* n = dynamic_cast<NoteEchoNode*>(gn.node.get()))
         DrawNoteEchoBody(gn, n);
      else if (auto* n = dynamic_cast<PredictiveNotesNode*>(gn.node.get()))
         DrawPredictiveNotesBody(gn, n);
      else if (auto* n = dynamic_cast<PredictiveQuantizeNode*>(gn.node.get()))
         DrawPredictiveQuantizeBody(gn, n);
      else if (auto* n = dynamic_cast<PredictiveVelocityNode*>(gn.node.get()))
         DrawPredictiveVelocityBody(gn, n);
      else if (auto* n = dynamic_cast<PredictiveRhythmNode*>(gn.node.get()))
         DrawPredictiveRhythmBody(gn, n);
      else if (auto* n = dynamic_cast<NoteRouterNode*>(gn.node.get()))
         DrawNoteRouterBody(gn, n);
      else if (auto* n = dynamic_cast<NoteMergeNode*>(gn.node.get()))
         DrawNoteMergeBody(gn, n);
      else if (auto* n = dynamic_cast<ArpeggiatorNode*>(gn.node.get()))
         DrawArpeggiatorBody(gn, n);
      else if (auto* n = dynamic_cast<NoteSequencerNode*>(gn.node.get()))
         DrawNoteSequencerBody(gn, n);
      else if (auto* n = dynamic_cast<RandomNoteGeneratorNode*>(gn.node.get()))
         DrawRandomNoteGeneratorBody(gn, n);
      else if (auto* n = dynamic_cast<ChorderNode*>(gn.node.get()))
         DrawChorderBody(gn, n);
      else if (auto* n = dynamic_cast<NoteStackNode*>(gn.node.get()))
         DrawNoteStackBody(gn, n);
      else if (auto* n = dynamic_cast<NoteCapturerNode*>(gn.node.get()))
         DrawNoteCapturerBody(gn, n);
      else if (auto* n = dynamic_cast<BouncingBallsNode*>(gn.node.get()))
         DrawBouncingBallsBody(gn, n);
      else if (auto* n = dynamic_cast<NoteStrumNode*>(gn.node.get()))
         DrawStrumBody(gn, n);
      else if (auto* n = dynamic_cast<AudioToCVNode*>(gn.node.get()))
         DrawAudioToCVBody(gn, n);
      else if (auto* n = dynamic_cast<EnvelopeNode*>(gn.node.get()))
         DrawEnvelopeBody(gn, n);
      else if (auto* n = dynamic_cast<NoteToCVNode*>(gn.node.get()))
         DrawNoteToCVBody(gn, n);
      else if (auto* n = dynamic_cast<MidiOutNode*>(gn.node.get()))
         DrawMidiOutBody(gn, n); // Turbo 0.57
      else if (auto* n = dynamic_cast<SpatialMixerNode*>(gn.node.get()))
         DrawSpatialMixerBody(gn, n); // Turbo 0.57
      else if (auto* n = dynamic_cast<PatternPlayerNode*>(gn.node.get()))
         DrawPatternPlayerBody(gn, n); // Turbo 0.57
      else if (auto* n = dynamic_cast<NoteLooperNode*>(gn.node.get()))
         DrawNoteLooperBody(gn, n); // Turbo 0.57
      else if (auto* n = dynamic_cast<VelocityToCVNode*>(gn.node.get()))
         DrawVelocityToCVBody(gn, n);
      else if (auto* n = dynamic_cast<KeyboardNode*>(gn.node.get()))
         DrawKeyboardBody(gn, n);
      else if (auto* n = dynamic_cast<NoteSwitcherNode*>(gn.node.get()))
         DrawNoteSwitcherBody(gn, n);
      else if (auto* n = dynamic_cast<AudioEffectNode*>(gn.node.get()))
      {
         // Every EffectDefs.cpp entry shares this one C++ class (§0.4), so
         // this dispatches a second time on which effect it actually is -
         // the same reason DrawAudioNodeBody itself exists one level up.
         // Dispatched by EffectDef::visualizerId (set once per table entry
         // in EffectDefs.cpp), not by a second string comparison against
         // Def().name - a name ladder is exactly what would turn into a
         // seven-way chain as the remaining P3c effects land.
         switch (n->Def().visualizerId)
         {
         case EffectVisualizerId::kFilterResponse:
            DrawAudioFilterBody(gn, n);
            break;
         case EffectVisualizerId::kEqCurve:
            DrawEqBody(gn, n);
            break;
         case EffectVisualizerId::kDynamicsTransfer:
            DrawDynamicsBody(gn, n);
            break;
         case EffectVisualizerId::kLimiterMeter:
            DrawLimiterBody(gn, n);
            break;
         case EffectVisualizerId::kResonatorBankSpectrum:
            DrawResonatorBankBody(gn, n);
            break;
         case EffectVisualizerId::kCycleShaperWave:
            DrawCycleShaperBody(gn, n);
            break;
         case EffectVisualizerId::kSpecBlurSpectrum:
            DrawSpecBlurBody(gn, n);
            break;
         case EffectVisualizerId::kKeySnapScale:
            DrawKeySnapBody(gn, n);
            break;
         case EffectVisualizerId::kSpectrumSlide:
            DrawSpectrumSlideBody(gn, n);
            break;
         // Turbo 0.60, from upstream
         case EffectVisualizerId::kSaturatorCurves:
            DrawSaturatorBody(gn, n);
            break;
         case EffectVisualizerId::kShapeResonator:
            DrawShapeResonatorBody(gn, n);
            break;
         case EffectVisualizerId::kConvolutionSpace:
            DrawConvolutionReverbBody(gn, n);
            break;
         case EffectVisualizerId::kDelayTaps:
            DrawDelayBody(gn, n);
            break;
         case EffectVisualizerId::kReverbDecay:
            DrawReverbBody(gn, n);
            break;
         case EffectVisualizerId::kDriveCurve:
            DrawDriveBody(gn, n);
            break;
         case EffectVisualizerId::kStereoGoniometer:
            DrawStereoBody(gn, n);
            break;
         case EffectVisualizerId::kPitchShiftDisplay:
            DrawPitchShiftBody(gn, n);
            break;
         case EffectVisualizerId::kChorusScatter:
            DrawChorusBody(gn, n);
            break;
         case EffectVisualizerId::kFlangerScatter:
            DrawFlangerBody(gn, n);
            break;
         case EffectVisualizerId::kPhaserScatter:
            DrawPhaserBody(gn, n);
            break;
         case EffectVisualizerId::kBitcrushWave:
            DrawBitcrushBody(gn, n);
            break;
         case EffectVisualizerId::kTransientEnvelope:
            DrawTransientShaperBody(gn, n);
            break;
         case EffectVisualizerId::kStutterGrid:
            DrawStutterBody(gn, n);
            break;
         case EffectVisualizerId::kRingModWave:
            DrawRingModBody(gn, n);
            break;
         case EffectVisualizerId::kFrequencyShiftSpectrum:
            DrawFrequencyShifterBody(gn, n);
            break;
         case EffectVisualizerId::kTremoloWave:
            DrawTremoloBody(gn, n);
            break;
         case EffectVisualizerId::kFormantVowel:
            DrawFormantFilterBody(gn, n);
            break;
         case EffectVisualizerId::kWavetableShaperCurve:
            DrawWavetableShaperBody(gn, n);
            break;
         default:
            break;
         }
      }
   }
