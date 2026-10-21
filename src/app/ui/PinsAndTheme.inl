// Part of src/main.cpp, included there in order (one translation unit).
// palette-bindable colours, pins, themes (incl. Infinite Dark)

   // ---- palette-bindable colours ------------------------------------------
   // The colour counterpart of ModSlider. Every swatch in the app already goes
   // through this one function, so giving it a pin here is what makes every
   // colour in every node bindable to a Palette at once - the alternative was
   // adding a pin by hand at fifty call sites and missing some.
   //
   // Colour pins are counted separately from parameter pins (see
   // GraphNode::kColorBase) so that adding one to a node cannot renumber the
   // sliders around it and repoint existing patches' modulation.
   void ColorSwatch(const char* label, float* col, const INode* owner)
   {
      const int nodeIndex = gCurrentNodeIndex;
      const int colorIndex = gColorCounter++;

      ColorRef ref;
      ref.nodeIndex = nodeIndex;
      ref.colorIndex = colorIndex;
      ref.value = col;
      ref.name = label;
      PaletteBinding::Instance().RegisterColor(ref);

      const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kColorBase + colorIndex;
      const PaletteBinding::Source bound =
         PaletteBinding::Instance().SourceFor(nodeIndex, colorIndex);
      const bool isBound = bound.nodeIndex >= 0;
      gDrawnColorPins.insert(pinId);

      ImGui::PushID(label);
      ImGui::PushID(colorIndex + 9000);

      ed::BeginPin(pinId, ed::PinKind::Input);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float box = 14.0f;
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      // Square where a modulation pin is round: the two accept different cables
      // and sit right next to each other, so they should not look alike.
      dl->AddRectFilled(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f),
                        isBound ? IM_COL32(130, 220, 190, 255) : IM_COL32(95, 100, 120, 255),
                        1.0f);
      ed::EndPin();
      ImGui::SameLine(0.0f, 4.0f);

      if (ImGui::ColorButton(label, ImVec4(col[0], col[1], col[2], 1.0f),
                             ImGuiColorEditFlags_NoTooltip, ImVec2(38, 0)))
      {
         if (isBound)
         {
            // A bound colour is rewritten from the palette every frame, so
            // opening the picker on it would show an edit that vanishes on the
            // next tick. Step to the next swatch instead, which is the edit
            // someone is actually reaching for here.
            PushUndoCheckpoint();
            int count = 1;
            if (IPaletteSource* source = PaletteSourceByIndex(bound.nodeIndex))
               count = std::max(1, source->SwatchCount());
            PaletteBinding::Instance().Bind(nodeIndex, colorIndex, bound.nodeIndex,
                                            (bound.swatchIndex + 1) % count);
         }
         else
         {
            // Captured once, at the moment the picker opens rather than per-frame
            // while it's being dragged - the picker writes into *col continuously,
            // so anywhere later would already see the edited value.
            PushUndoCheckpoint();
            gColor.target = col;
            gColor.owner = owner;
            gColor.label = label;
            gColor.justOpened = true;
         }
      }
      ImGui::SameLine();
      if (isBound)
         ImGui::TextColored(ImVec4(0.5f, 0.86f, 0.74f, 1.0f), "%s  #%d",
                            label, bound.swatchIndex + 1);
      else
         ImGui::TextDisabled("%s", label);

      ImGui::PopID();
      ImGui::PopID();
   }

   // A node with its params collapsed draws no param or colour pins, and a link
   // pointing at an undeclared pin is dead to the editor - so every cable feeding
   // a collapsed node used to vanish the moment the eye was closed, even though
   // the binding was still live and driving the value.
   //
   // Declare a stub pin for each bound parameter/colour instead, all stacked on
   // the collapsed "mod"/"pal" tag. The pins are 1px and invisible; they exist
   // purely so the cable has somewhere to land, and they make the tag itself the
   // node's collapsed landing point.
   void CollapsedBindingPins(int nodeIndex, const ImVec2& tagMin, const ImVec2& tagMax, bool colors)
   {
      const ImVec2 anchor((tagMin.x + tagMax.x) * 0.5f, (tagMin.y + tagMax.y) * 0.5f);
      const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();

      auto stub = [&](int pinId)
      {
         ImGui::SetCursorScreenPos(ImVec2(anchor.x - 0.5f, anchor.y - 0.5f));
         ed::BeginPin(pinId, ed::PinKind::Input);
         ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
         ImGui::Dummy(ImVec2(1.0f, 1.0f));
         ed::EndPin();
      };

      if (colors)
      {
         for (const auto& link : PaletteBinding::Instance().Links())
         {
            if (link.first.first != nodeIndex)
               continue;
            const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kColorBase + link.first.second;
            gDrawnColorPins.insert(pinId);
            stub(pinId);
         }
      }
      else
      {
         for (const auto& link : Modulation::Instance().Links())
         {
            if (link.first.first != nodeIndex)
               continue;
            const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kParamBase + link.first.second;
            gDrawnParamPins.insert(pinId);
            stub(pinId);
         }
      }

      // The stubs are placed out of layout order, so hand the cursor back where
      // the tag row left it. Call this only once the whole row is drawn: nothing
      // after it may rely on SameLine().
      ImGui::SetCursorScreenPos(restoreCursor);
   }


   // ImGui's Separator / SeparatorText span the available content width, and
   // inside a node that width is unbounded - the rule shot off across the whole
   // canvas. Draw our own, clamped to the node's preview width.
   // `width` defaults to kPreviewSize so every existing visual-node caller is
   // unchanged; audio nodes pass their body width so the rule actually spans
   // the node instead of stopping a third of the way across (v3 §1b).
   void NodeSeparator(const char* label = nullptr, float width = kPreviewSize)
   {
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float y = origin.y + 6.0f;
      const ImU32 col = IM_COL32(78, 82, 100, 255);

      if (label != nullptr && label[0] != '\0')
      {
         const ImVec2 textSize = ImGui::CalcTextSize(label);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + 10.0f, y), col);
         dl->AddText(ImVec2(origin.x + 16.0f, origin.y), IM_COL32(150, 156, 180, 255), label);
         const float lineStart = origin.x + 22.0f + textSize.x;
         if (lineStart < origin.x + width)
            dl->AddLine(ImVec2(lineStart, y), ImVec2(origin.x + width, y), col);
         ImGui::Dummy(ImVec2(width, textSize.y));
      }
      else
      {
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y), col);
         ImGui::Dummy(ImVec2(width, 8.0f));
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
   }

   const std::vector<std::string>& AlignOptions()
   {
      static const std::vector<std::string> kAlign = { "Left", "Center", "Right", "Justified" };
      return kAlign;
   }

   // Small eye toggle. Drawn rather than typed: the UI font has no eye glyph,
   // and an emoji would not render in a non-emoji face.
   bool EyeToggle(bool shown)
   {
      const float w = 26.0f;
      const float h = 18.0f;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const bool pressed = ImGui::InvisibleButton("##eye", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(origin.x + w * 0.5f, origin.y + h * 0.5f);
      ImU32 col = hovered ? IM_COL32(235, 240, 255, 255)
                          : (shown ? IM_COL32(150, 190, 255, 255) : IM_COL32(120, 124, 140, 255));

      // almond outline: two arcs meeting at the corners
      const float rx = 9.0f, ry = 5.5f;
      dl->PathClear();
      for (int i = 0; i <= 16; i++)
      {
         float t = (float)i / 16.0f;
         float x = -rx + 2.0f * rx * t;
         float y = -ry * (float)sin(3.14159f * t);
         dl->PathLineTo(ImVec2(c.x + x, c.y + y));
      }
      for (int i = 0; i <= 16; i++)
      {
         float t = (float)i / 16.0f;
         float x = rx - 2.0f * rx * t;
         float y = ry * (float)sin(3.14159f * t);
         dl->PathLineTo(ImVec2(c.x + x, c.y + y));
      }
      dl->PathStroke(col, ImDrawFlags_Closed, 1.6f);

      if (shown)
         dl->AddCircleFilled(c, 2.6f, col);
      else
         dl->AddLine(ImVec2(c.x - rx, c.y + ry * 0.9f), ImVec2(c.x + rx, c.y - ry * 0.9f), col, 1.6f);

      return pressed;
   }

   // Small monitor/screen toggle for a node's own mini 3D viewport. Deliberately
   // distinct from EyeToggle (params visibility) - this switches on a live
   // render pass, not just a UI panel, so it gets its own affordance rather
   // than overloading the eye icon.
   bool ViewportToggle(bool shown)
   {
      const float w = 22.0f;
      const float h = 18.0f;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const bool pressed = ImGui::InvisibleButton("##miniviewport", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(origin.x + w * 0.5f, origin.y + h * 0.5f);
      ImU32 col = hovered ? IM_COL32(235, 240, 255, 255)
                          : (shown ? IM_COL32(150, 190, 255, 255) : IM_COL32(120, 124, 140, 255));

      // A little screen/monitor glyph: rounded rect body plus a stand, filled
      // when the viewport is on so it reads at a glance in a busy graph.
      const float bw = 13.0f, bh = 9.0f;
      ImVec2 tl(c.x - bw * 0.5f, c.y - bh * 0.5f - 1.0f);
      ImVec2 br(c.x + bw * 0.5f, c.y + bh * 0.5f - 1.0f);
      if (shown)
         dl->AddRectFilled(tl, br, col, 1.5f);
      else
         dl->AddRect(tl, br, col, 1.5f, 0, 1.4f);
      dl->AddLine(ImVec2(c.x, br.y), ImVec2(c.x, br.y + 2.5f), col, 1.4f);
      dl->AddLine(ImVec2(c.x - 3.5f, br.y + 2.5f), ImVec2(c.x + 3.5f, br.y + 2.5f), col, 1.4f);

      return pressed;
   }

   bool BypassToggle(bool bypassed, bool& requestedBypass, bool& changedFromCv)
   {
      changedFromCv = false;
      DiscreteParamRef ref = BeginDiscreteParam("BYPASS##moduleBypass", bypassed ? 1.0f : 0.0f,
                                                 0.0f, 1.0f);
      bool changedByCv = false;
      if (ref.valid && ref.modulated)
      {
         requestedBypass = *ref.value >= 0.5f;
         changedByCv = requestedBypass != bypassed;
         changedFromCv = changedByCv;
         ImGui::BeginDisabled();
      }

      const float w = 25.0f, h = 18.0f;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool pressed = ImGui::InvisibleButton("##bypass", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered(ref.valid && ref.modulated
                                                   ? ImGuiHoveredFlags_AllowWhenDisabled
                                                   : ImGuiHoveredFlags_None);
      if (ref.valid && ref.modulated)
         ImGui::EndDisabled();
      const ImU32 col = hovered ? IM_COL32(245, 245, 255, 255)
                         : (bypassed ? IM_COL32(255, 185, 70, 255) : IM_COL32(120, 124, 140, 255));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 textSize = ImGui::CalcTextSize("BP");
      const ImVec2 textPos(p.x + (w - textSize.x) * 0.5f,
                           p.y + (h - textSize.y) * 0.5f);
      dl->AddText(textPos, col, "BP");
      if (bypassed)
      {
         const float pad = 1.5f;
         dl->AddLine(ImVec2(textPos.x - pad, textPos.y + textSize.y + pad),
                     ImVec2(textPos.x + textSize.x + pad, textPos.y - pad),
                     col, 1.6f);
      }
      EndDiscreteParam(ref, hovered);
      if (pressed)
      {
         requestedBypass = !bypassed;
         return true;
      }
      return changedByCv;
   }

   // ---- pins --------------------------------------------------------------
   // Drawn inside a kPinHit-wide box so the clickable area is far larger than
   // the visible dot; connecting used to require pixel-perfect aim.
   void DrawPin(int pinId, ed::PinKind kind, const char* label, bool labelFirst = false)
   {
      ed::BeginPin(pinId, kind);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));

      if (labelFirst && label != nullptr && label[0] != '\0')
      {
         ImGui::TextDisabled("%s", label);
         ImGui::SameLine(0.0f, 4.0f);
      }

      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(kPinHit, kPinHit));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(p.x + kPinHit * 0.5f, p.y + kPinHit * 0.5f);
      const bool isLight = IsThemeLight();
      dl->AddCircleFilled(c, kPinRadius, isLight ? IM_COL32(50, 120, 240, 255) : IM_COL32(150, 190, 255, 255));
      dl->AddCircle(c, kPinRadius, isLight ? IM_COL32(40, 48, 65, 255) : IM_COL32(20, 22, 30, 255), 0, 1.5f);

      if (!labelFirst && label != nullptr && label[0] != '\0')
      {
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::TextDisabled("%s", label);
      }
      ed::EndPin();
   }

   void RegisterNodes()
   {
      REGISTER_NODE(ImageSourceNode, Image Source, "Source");
      REGISTER_NODE(SlideshowNode, Slideshow, "Source");
      REGISTER_NODE(ShapeNode, Shape, "Source");
      for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
      {
         NodeFactory::Instance().Register(
            ShapeNode::ShapeNames()[i],
            [i]() -> INode* { return ShapeNode::CreateFor(i); }, "Shapes"); // Turbo 0.55 (was Source)
      }
      REGISTER_NODE(FormulaNode, Formula, "Source");
      REGISTER_NODE(TextNode, Text, "Text");
      REGISTER_NODE(VideoSourceNode, Video, "Source");
      REGISTER_NODE(TimelineNode, Timeline, "Source");
      REGISTER_NODE(VmpcNode, VMPC, "Source");
      REGISTER_NODE(ClipMatrixNode, Clip Matrix, "Source");
      REGISTER_NODE(VideoInNode, Video In, "Source");
      REGISTER_NODE(SyphonInNode, Spout In, "Source");
      REGISTER_NODE(NoiseNode, Noise, "Source");
      REGISTER_NODE(TextureNode, Texture, "Source");
      REGISTER_NODE(RampNode, Ramp, "Source");
      REGISTER_NODE(GeometryNode, Geometry, "3D");
      // Every primitive as its own searchable node, sharing one class.
      for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
      {
         NodeFactory::Instance().Register(
            GeometryNode::ShapeNames()[i],
            [i]() -> INode* { return GeometryNode::CreateFor(i); }, "3DSource"); // Turbo 0.55 (was 3D)
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
      REGISTER_NODE(DistributePointsOnFacesNode, Distribute Points on Faces, "3D");
      REGISTER_NODE(PointsToVerticesNode, Points to Vertices, "3D");
      REGISTER_NODE(DistributePointsInGridNode, Distribute Points in Grid, "3D");
      REGISTER_NODE(MergeByDistanceNode, Merge by Distance, "3D");
      REGISTER_NODE(Switcher3DNode, Switcher 3D, "3D");
      REGISTER_NODE(Group3DNode, Group 3D, "3D");
      REGISTER_NODE(DelaunayMeshNode, Delaunay Mesh, "3D");
      REGISTER_NODE(VoronoiCellsNode, Voronoi Cells, "3D");
      REGISTER_NODE(CurveOpsNode, Curve Ops, "3D");
      // The PointZ family (src/pop): one PopNode class runs every operator in the
      // table, so the whole family registers from it.
      for (const pop::PopDef& def : pop::Registry())
      {
         const std::string popName = def.name;
         NodeFactory::Instance().Register(popName, [popName]() -> INode* { return PopNode::CreateFor(popName); }, "PointZ");
      }
      REGISTER_NODE(CameraNode, Camera, "3D");
      REGISTER_NODE(LightNode, Light, "3D");
      REGISTER_NODE(EnvironmentNode, HDRI, "3D");
      REGISTER_NODE(Render3DNode, Render 3D, "3D");
      REGISTER_NODE(DrawNode, Draw, "Source");
      REGISTER_NODE(ResynthNode, Resynthesize, "Resynth");
      REGISTER_NODE(FitNode, Fit, "Compositing");
      // Turbo 0.60: ported from upstream (Naman's Infinite after v0.6)
      REGISTER_NODE(TimeMachineNode, Time Machine, "Feedback");
      REGISTER_NODE(OpticalFlowNode, Optical Flow, "Effects");
      REGISTER_NODE(FluidNode, Fluid, "Feedback");
      REGISTER_NODE(VolumeNode, Volume, "Effects");
      REGISTER_NODE(NdiInNode, NDI In, "Video");
      REGISTER_NODE(NdiOutNode, NDI Out, "Output");
      REGISTER_NODE(SweepNode, Sweep, "3D");
      REGISTER_NODE(RevolveNode, Revolve, "3D");
      REGISTER_NODE(LSystemNode, L-System, "3D");
      REGISTER_NODE(ProximityLinesNode, Proximity Lines, "3D");
      REGISTER_NODE(RigidBodyNode, Rigid Bodies, "3D");
      REGISTER_NODE(CommentNode, Comment, "Compositing");
      REGISTER_NODE(GroupNode, Group, "Compositing");
      REGISTER_NODE(NullNode, Null, "Compositing");
      REGISTER_NODE(ViewportNode, Viewport, "Compositing");
      REGISTER_NODE(CurvesNode, Curves, "Color");
      REGISTER_NODE(ColorRampNode, Color Ramp, "Color");
      REGISTER_NODE(RemoveBgNode, Remove Background, "Mask");
      REGISTER_NODE(FeedbackNode, Feedback, "Feedback");
      REGISTER_NODE(TrailsNode, Trails, "Feedback");
      REGISTER_NODE(DatamoshNode, Datamosh, "Effects"); // Turbo 0.51:
      REGISTER_NODE(ReactionDiffusionNode, Reaction Diffusion, "Feedback");
      REGISTER_NODE(BlendNode, Blend, "Compositing");
      REGISTER_NODE(LayerStackNode, Layer Stack, "Compositing");
      REGISTER_NODE(LayoutNode, Layout, "Compositing");
      REGISTER_NODE(SwitcherNode, Switcher, "Compositing");
      REGISTER_NODE(OutputNode, Output, "Output");
      REGISTER_NODE(SyphonOutNode, Spout Out, "Output");
      REGISTER_NODE(ProjectionNode, Projection, "Output");
      REGISTER_NODE(LFONode, LFO, "Modulators");
      REGISTER_NODE(RandomNode, Random, "Modulators");
      REGISTER_NODE(MSEGNode, MSEG, "Modulators");                   // Turbo 0.60, from upstream
      REGISTER_NODE(VoiceSpreadNode, Voice Spread, "Modulators");
      REGISTER_NODE(NoteExpressionNode, Note Expression, "Modulators");
      REGISTER_NODE(PredictiveColoringNode, Predictive Coloring, "Prediction"); // Turbo 0.60, from upstream
      REGISTER_NODE(PatternNode, Pattern, "Modulators");
      REGISTER_NODE(MathNode, Math, "Modulators");
      REGISTER_NODE(GeometryTableNode, Geometry Table, "Modulators");
      REGISTER_NODE(CompareNode, Compare, "Modulators");
      REGISTER_NODE(RangeToRangeNode, Range to Range, "Modulators");
      REGISTER_NODE(SmoothNode, Smoothing, "Modulators");
      REGISTER_NODE(CVRecorderNode, CV Recorder, "Modulators");
      REGISTER_NODE(InvertNode, Invert, "Modulators");
      REGISTER_NODE(ModDepthNode, Mod Depth, "Modulators");
      REGISTER_NODE(ModCurveNode, Mod Curve, "Modulators");
      REGISTER_NODE(CVToPitchNode, CV to Pitch, "Modulators");
      REGISTER_NODE(MacroKnobNode, Macro Knob, "Modulators");
      REGISTER_NODE(MacroXYNode, Macro XY, "Modulators");
      REGISTER_NODE(MacroSliderNode, Macro Slider, "Macros");
      REGISTER_NODE(MacroBipolarKnobNode, Macro Bipolar Knob, "Macros");
      REGISTER_NODE(MacroToggleNode, Macro Toggle, "Macros");
      REGISTER_NODE(MacroTriggerNode, Macro Trigger, "Macros");
      REGISTER_NODE(MacroNumBoxNode, Macro NumBox, "Macros");
      REGISTER_NODE(MacroRadioSelectorNode, Macro Radio Selector, "Macros");
      REGISTER_NODE(MacroStepGateNode, Macro Step Gate, "Macros");
      REGISTER_NODE(ScenesNode, Scenes, "Macros"); // Turbo 0.50
      REGISTER_NODE(DriftNode, Drift, "Modulators");
      NodeFactory::Instance().Register("Button", &UIButtonNode::Create, "UI");
      NodeFactory::Instance().Register("Horizontal Slider", &UISliderNode::CreateHorizontal, "UI");
      NodeFactory::Instance().Register("Vertical Slider", &UISliderNode::CreateVertical, "UI");
      NodeFactory::Instance().Register("XY Pad", &UIXYNode::Create, "UI");
      REGISTER_NODE(MidiCCNode, MIDI CC, "Modulators");
      REGISTER_NODE(MidiTriggerNode, MIDI Trigger, "Modulators");
      REGISTER_NODE(PathNode, Path, "Modulators");
      REGISTER_NODE(ConstantNode, Constant, "Modulators");
      REGISTER_NODE(NullModulatorNode, Null Modulator, "Modulators");
      REGISTER_NODE(ImageAnalyzeNode, Image Analyze, "Modulators");
      REGISTER_NODE(PaletteNode, Palette, "Modulators");
      REGISTER_NODE(AudioFileNode, Audio File, "Modulators");
      REGISTER_NODE(AudioAnalyzeNode, Audio Analyze, "Modulators");
      REGISTER_NODE(OscReceiveNode, OSC Receive, "OSC");
      REGISTER_NODE(OscSendNode, OSC Send, "OSC");
      REGISTER_NODE(MidiOutNode, MIDI Out, "Control"); // Turbo 0.57 (upstream)
      // Turbo: Pure Data style wireless send/receive, one pair per cable kind.
      REGISTER_NODE(SendImageNode, Send Image, "Utility");
      REGISTER_NODE(ReceiveImageNode, Receive Image, "Utility");
      REGISTER_NODE(SendCVNode, Send CV, "Utility");
      REGISTER_NODE(ReceiveCVNode, Receive CV, "Utility");
      REGISTER_NODE(SendNotesNode, Send Notes, "Utility");
      REGISTER_NODE(ReceiveNotesNode, Receive Notes, "Utility");
      REGISTER_NODE(SendAudioNode, Send Audio, "Utility");
      REGISTER_NODE(ReceiveAudioNode, Receive Audio, "Utility");
      REGISTER_NODE(SendGeometryNode, Send Geometry, "Utility");
      REGISTER_NODE(ReceiveGeometryNode, Receive Geometry, "Utility");
      REGISTER_NODE(OscToCvNode, OSC to CV, "Control");
      // Ported from upstream Infinite: the note-prediction family.
      REGISTER_NODE(PredictiveNotesNode, Predictive Notes, "Prediction");
      REGISTER_NODE(PredictiveQuantizeNode, Predictive Quantize, "Prediction");
      REGISTER_NODE(PredictiveVelocityNode, Predictive Velocity, "Prediction");
      REGISTER_NODE(PredictiveRhythmNode, Predictive Rhythm, "Prediction");

      // P2 audio-graph proof nodes - see docs/plans/audio/README.md P2.
      // Category deliberately "AudioUtility" not "Audio Utility": Patch.cpp's
      // "node <index> <category> <typeName>" line reads category with `>>`
      // (a single whitespace-delimited token) - every existing category is
      // one word for exactly that reason, and a space in a new one corrupts
      // the save format (confirmed: it silently ate the type name on load).
      REGISTER_NODE(OscillatorNode, Oscillator, "Synths");
      REGISTER_NODE(WavetableNode, Wavetable, "Synths");
      REGISTER_NODE(AnalogNode, Analog, "Synths");
      REGISTER_NODE(WaveTerrainNode, Wave Terrain, "Synths");
      REGISTER_NODE(EquationNode, Equation Synth, "Synths");
      REGISTER_NODE(ImageSpectralSynthNode, Spectral Synth, "Synths");
      REGISTER_NODE(MetallicNode, Metallic, "Synths");
      REGISTER_NODE(FMNode, FM, "Synths");           // Turbo 0.60, from upstream
      REGISTER_NODE(PartialsNode, Partials, "Synths");
      REGISTER_NODE(StringNode, String, "Synths");
      REGISTER_NODE(SamplerNode, Sampler, "Synths");
      REGISTER_NODE(PaulStretchNode, PaulStretch, "Synths");
      REGISTER_NODE(GranularNode, Granular, "Synths");
      REGISTER_NODE(SlicerNode, Slicer, "Synths");
      REGISTER_NODE(MolderNode, Molder, "Synths");
      REGISTER_NODE(GrainMolderNode, Grain Molder, "Synths");
      REGISTER_NODE(DrumSequencerNode, Drum Sequencer, "Synths");
      // Infinite-Turbo
      REGISTER_NODE(MpcNode, MPC, "Synths");
      REGISTER_NODE(MpcOutNode, MPC Out, "AudioUtility");
      REGISTER_NODE(LooperNode, Looper, "AudioUtility");
      REGISTER_NODE(ClipMatrixOutNode, Clip Matrix Out, "AudioUtility");
      REGISTER_NODE(SuperMixerNode, Super Mixer, "AudioUtility");
      REGISTER_NODE(SpatialMixerNode, Spatial Mixer, "AudioUtility"); // Turbo 0.57 (upstream)
      // Third-party plugin hosting (Audio Units). Its params reach the plugin
      // directly rather than through ParamMailbox - see AudioPluginNode.h.
      REGISTER_NODE(AudioPluginNode, Plugin, "AudioEffects");
      REGISTER_NODE(GainNode, Gain, "AudioUtility");
#if INFINITE_GPL
      REGISTER_NODE(PunchFxNode, Punch FX, "AudioEffects");
#endif
      REGISTER_NODE(AudioMeterNode, Audio Meter, "AudioUtility");
      REGISTER_NODE(AudioInputNode, Audio In, "AudioUtility");
      REGISTER_NODE(AudioOutputNode, Audio Out, "AudioUtility");
      // P2.8 routing nodes - the system's only summing/fan-out points, see
      // docs/plans/audio/audio-graph-semantics.md §1/§2.
      REGISTER_NODE(MixerNode, Mixer, "AudioUtility");
      REGISTER_NODE(SplitterNode, Splitter, "AudioUtility");
      // "Blend Audio" not "Blend" - that name is taken by the image
      // compositing node (REGISTER_NODE(BlendNode, Blend, "Compositing") above).
      REGISTER_NODE(BlendAudioNode, Blend Audio, "AudioUtility");
      REGISTER_NODE(AudioTextureNode, Audio Texture, "Audio");
      REGISTER_NODE(AudioColorRampNode, Audio Color Ramp, "Audio");

      // P3a Part 1 - note-transport proving nodes. See
      // docs/plans/audio/P3a-notes-prompt.md; Part 2 adds Note Filter/
      // Modify/Echo/Router/Display and the Arpeggiator.
      REGISTER_NODE(MidiNotesNode, MIDI Notes, "Notes");
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
      REGISTER_NODE(NoteRouterNode, Note Router, "Notes");
      REGISTER_NODE(NoteMergeNode, Note Merge, "Notes");
      REGISTER_NODE(ArpeggiatorNode, Arpeggiator, "Notes");
      REGISTER_NODE(NoteSequencerNode, Note Sequencer, "Notes");
      REGISTER_NODE(RandomNoteGeneratorNode, Random Note Generator, "Notes");
      REGISTER_NODE(ChorderNode, Chorder, "Notes");
      REGISTER_NODE(NoteStackNode, Note Stack, "Notes");
      REGISTER_NODE(NoteCapturerNode, Note Capturer, "Notes");
      REGISTER_NODE(BouncingBallsNode, Bouncing Balls, "Notes");
      REGISTER_NODE(NoteStrumNode, Note Strum, "Notes");
      REGISTER_NODE(EnvelopeNode, Envelope, "Modulators");
      REGISTER_NODE(NoteToCVNode, Note to CV, "Modulators");
      REGISTER_NODE(VelocityToCVNode, Velocity to CV, "Modulators");
      REGISTER_NODE(KeyboardNode, Keyboard, "Notes");
      REGISTER_NODE(ChordProgressionNode, Chord Progression, "Notes");
      REGISTER_NODE(MidiFileNode, MIDI File, "Notes"); // Turbo 0.48
      REGISTER_NODE(PatternPlayerNode, Pattern Player, "Notes"); // Turbo 0.57
      REGISTER_NODE(NoteLooperNode, Note Looper, "Notes"); // Turbo 0.57
      REGISTER_NODE(TransportControlNode, Transport Control, "Control");
      REGISTER_NODE(NoteSwitcherNode, Note Switcher, "Notes");
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

   // Restyles ImGui's whole palette plus the node-editor canvas from the
   // active CategoryColors preset, so switching presets re-themes the app
   // rather than just the graph. Mutates ImGuiStyle/ed::Style directly
   // (like StyleColorsDark() itself), not a push/pop - it's meant to stick
   // until the user picks a different preset. Node header/border tinting
   // is separate, applied per-node from the same preset's category table.
   void ApplyDesignTheme(); // Turbo 0.57, below

   void ApplyTheme()
   {
      // Turbo 0.57: switching to / from the design theme changes the font
      // (Inter, mixed case), rebuilt between frames like a scale change.
      {
         static int sLastDesign = -1;
         const int design = CategoryColors::IsDesignTheme() ? 1 : 0;
         if (sLastDesign != -1 && sLastDesign != design)
            gUiRescaleRequested = true;
         sLastDesign = design;
      }
      if (CategoryColors::IsDesignTheme())
      {
         ApplyDesignTheme();
         return;
      }
      {
         // Turbo classic metrics (the design theme changes these).
         ImGuiStyle& st = ImGui::GetStyle();
         st.PopupRounding = 0.0f;
         st.ScrollbarRounding = 9.0f;
         st.ScrollbarSize = 14.0f;
         st.WindowBorderSize = 1.0f;
         st.PopupBorderSize = 1.0f;
         st.ChildBorderSize = 1.0f;
         st.TabBarBorderSize = 1.0f;
      }
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      auto vec = [](const CategoryColors::Color& c, float a = 1.0f) {
         return ImVec4(c.r, c.g, c.b, a);
      };
      const float lum = 0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b;
      const bool isLight = (lum > 0.5f);

      auto shade = [isLight](const CategoryColors::Color& c, float amt) {
         if (isLight)
            return ImVec4(c.r * (1.0f - amt * 0.45f), c.g * (1.0f - amt * 0.45f),
                          c.b * (1.0f - amt * 0.45f), 1.0f);
         else
            return ImVec4(c.r + (1.0f - c.r) * amt, c.g + (1.0f - c.g) * amt,
                          c.b + (1.0f - c.b) * amt, 1.0f);
      };

      ImGuiStyle& style = ImGui::GetStyle();
      style.Colors[ImGuiCol_Text] = vec(t.text);
      style.Colors[ImGuiCol_TextDisabled] = vec(t.textDim);
      style.Colors[ImGuiCol_WindowBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_ChildBg] = vec(t.panelBg, 0.0f);
      style.Colors[ImGuiCol_PopupBg] = vec(t.panelBg, 0.98f);
      style.Colors[ImGuiCol_Border] = vec(t.border);
      style.Colors[ImGuiCol_FrameBg] = vec(t.panelBg);
      style.Colors[ImGuiCol_FrameBgHovered] = shade(t.panelBg, 0.14f);
      style.Colors[ImGuiCol_FrameBgActive] = shade(t.panelBg, 0.24f);
      style.Colors[ImGuiCol_TitleBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_TitleBgActive] = vec(t.windowBg);
      style.Colors[ImGuiCol_TitleBgCollapsed] = vec(t.windowBg, 0.75f);
      style.Colors[ImGuiCol_MenuBarBg] = vec(t.panelBg);
      style.Colors[ImGuiCol_ScrollbarBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_ScrollbarGrab] = vec(t.border);
      style.Colors[ImGuiCol_ScrollbarGrabHovered] = shade(t.border, 0.25f);
      style.Colors[ImGuiCol_ScrollbarGrabActive] = vec(t.accent);
      style.Colors[ImGuiCol_CheckMark] = vec(t.accent);
      style.Colors[ImGuiCol_SliderGrab] = vec(t.accent, 0.85f);
      style.Colors[ImGuiCol_SliderGrabActive] = vec(t.accent);
      style.Colors[ImGuiCol_Button] = vec(t.panelBg);
      style.Colors[ImGuiCol_ButtonHovered] = shade(t.panelBg, 0.18f);
      style.Colors[ImGuiCol_ButtonActive] = isLight ? shade(t.panelBg, 0.32f) : vec(t.accent, 0.65f);
      style.Colors[ImGuiCol_Header] = vec(t.accent, isLight ? 0.20f : 0.30f);
      style.Colors[ImGuiCol_HeaderHovered] = vec(t.accent, isLight ? 0.35f : 0.45f);
      style.Colors[ImGuiCol_HeaderActive] = vec(t.accent, isLight ? 0.50f : 0.60f);
      style.Colors[ImGuiCol_Separator] = vec(t.border);
      style.Colors[ImGuiCol_SeparatorHovered] = vec(t.accent, 0.6f);
      style.Colors[ImGuiCol_SeparatorActive] = vec(t.accent);
      style.Colors[ImGuiCol_ResizeGrip] = vec(t.border, 0.4f);
      style.Colors[ImGuiCol_ResizeGripHovered] = vec(t.accent, 0.6f);
      style.Colors[ImGuiCol_ResizeGripActive] = vec(t.accent);
      style.Colors[ImGuiCol_Tab] = vec(t.panelBg);
      style.Colors[ImGuiCol_TabHovered] = vec(t.accent, 0.5f);
      style.Colors[ImGuiCol_TabActive] = shade(t.panelBg, 0.15f);
      style.Colors[ImGuiCol_TabUnfocused] = vec(t.windowBg);
      style.Colors[ImGuiCol_TabUnfocusedActive] = vec(t.panelBg);
      style.Colors[ImGuiCol_TextSelectedBg] = vec(t.accent, 0.35f);
      style.Colors[ImGuiCol_DragDropTarget] = vec(t.accent);
      style.Colors[ImGuiCol_NavHighlight] = vec(t.accent);

      // The node-graph canvas itself is drawn by imgui-node-editor from its
      // own style table, not ImGui's - Bg/Grid are the two slots visible
      // behind every node regardless of category. ed::GetStyle() reads
      // through the *current* editor context, which is null here when this
      // runs from the menu bar (that frame's ed::SetCurrentEditor(gEditor)
      // happens later, after the menu bar) - set/restore it explicitly
      // rather than relying on caller state, since the startup call and the
      // menu-bar call have different current-editor state at the time.
      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      ed::Style& edStyle = ed::GetStyle();
      edStyle.Colors[ed::StyleColor_Bg] = vec(t.windowBg, isLight ? 1.0f : 0.784f);
      edStyle.Colors[ed::StyleColor_Grid] = vec(t.border, isLight ? 0.50f : 0.35f);
      edStyle.Colors[ed::StyleColor_NodeBg] = vec(t.panelBg, isLight ? 0.95f : 0.80f);
      edStyle.Colors[ed::StyleColor_NodeBorder] = vec(t.border, isLight ? 0.70f : 0.40f);
      edStyle.GridSpacing = gGridSnap;
      ed::SetCurrentEditor(prevEditor);
   }

   // ---- Turbo 0.57: "Infinite Dark (2026)", upstream's design-system theme ----
   // Ported from upstream src/app/ui/Theme.cpp ApplyTheme: dim text and the
   // control accent pulled to WCAG contrast, one accent emphasis ladder for
   // rows / tabs / selection, no outline on popups and windows, quiet
   // scrollbars, the canvas selection in accent. Widgets ask
   // CategoryColors::IsDesignTheme() for their faces (field wells, ring pins).
   inline ImVec4 DesignAccentTint(float amount)
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      return ImVec4(t.panelBg.r + (t.accent.r - t.panelBg.r) * amount,
                    t.panelBg.g + (t.accent.g - t.panelBg.g) * amount,
                    t.panelBg.b + (t.accent.b - t.panelBg.b) * amount, 1.0f);
   }

   void ApplyDesignTheme()
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      auto vec = [](const CategoryColors::Color& c, float a = 1.0f) { return ImVec4(c.r, c.g, c.b, a); };
      const float lumBg = 0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b;
      const bool isLight = lumBg > 0.5f;
      auto shade = [isLight](const CategoryColors::Color& c, float amt) {
         if (isLight)
            return ImVec4(c.r * (1.0f - amt * 0.45f), c.g * (1.0f - amt * 0.45f), c.b * (1.0f - amt * 0.45f), 1.0f);
         return ImVec4(c.r + (1.0f - c.r) * amt, c.g + (1.0f - c.g) * amt, c.b + (1.0f - c.b) * amt, 1.0f);
      };
      auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
      auto lum = [&](float r, float g, float b) { return 0.2126f * lin(r) + 0.7152f * lin(g) + 0.0722f * lin(b); };
      auto ratio = [](float a, float b) { return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f); };
      const float panelL = lum(t.panelBg.r, t.panelBg.g, t.panelBg.b);
      const ImVec4 hover = DesignAccentTint(tok::accent_hover);
      const ImVec4 selected = DesignAccentTint(tok::accent_selected);
      const ImVec4 pressed = DesignAccentTint(tok::accent_pressed);

      ImGuiStyle& style = ImGui::GetStyle();
      style.FrameRounding = 3.0f;
      style.GrabRounding = 3.0f;
      style.WindowRounding = 4.0f;
      style.PopupRounding = 12.0f;
      style.ScrollbarRounding = 10.0f;
      style.ScrollbarSize = 12.0f;
      style.TabBarBorderSize = 0.0f;
      style.WindowBorderSize = 0.0f;
      style.PopupBorderSize = 0.0f;
      style.ChildBorderSize = 0.0f;

      style.Colors[ImGuiCol_Text] = vec(t.text);
      {
         ImVec4 dim = vec(t.textDim);
         for (float k = 0.0f; k < 1.0f; k += 0.05f)
         {
            dim = ImVec4(t.textDim.r + (t.text.r - t.textDim.r) * k, t.textDim.g + (t.text.g - t.textDim.g) * k,
                         t.textDim.b + (t.text.b - t.textDim.b) * k, 1.0f);
            if (ratio(lum(dim.x, dim.y, dim.z), panelL) >= 4.5f)
               break;
         }
         style.Colors[ImGuiCol_TextDisabled] = dim;
      }
      style.Colors[ImGuiCol_WindowBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_ChildBg] = vec(t.panelBg, 0.0f);
      style.Colors[ImGuiCol_PopupBg] = vec(t.panelBg, 0.98f);
      style.Colors[ImGuiCol_Border] = vec(t.border);
      style.Colors[ImGuiCol_FrameBg] = shade(t.panelBg, 0.08f);
      style.Colors[ImGuiCol_FrameBgHovered] = shade(t.panelBg, 0.14f);
      style.Colors[ImGuiCol_FrameBgActive] = shade(t.panelBg, 0.24f);
      style.Colors[ImGuiCol_TitleBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_TitleBgActive] = vec(t.windowBg);
      style.Colors[ImGuiCol_TitleBgCollapsed] = vec(t.windowBg, 0.75f);
      style.Colors[ImGuiCol_MenuBarBg] = vec(t.windowBg); // one surface with the canvas, no seam
      style.Colors[ImGuiCol_ScrollbarBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_ScrollbarGrab] = vec(t.text, 0.22f);
      style.Colors[ImGuiCol_ScrollbarGrabHovered] = vec(t.text, 0.40f);
      style.Colors[ImGuiCol_ScrollbarGrabActive] = vec(t.text, 0.55f);
      ImVec4 ctrlAccent = vec(t.accent);
      for (float k = 0.0f; k < 1.0f; k += 0.05f)
      {
         ctrlAccent = ImVec4(t.accent.r + (t.text.r - t.accent.r) * k, t.accent.g + (t.text.g - t.accent.g) * k,
                             t.accent.b + (t.text.b - t.accent.b) * k, 1.0f);
         if (ratio(lum(ctrlAccent.x, ctrlAccent.y, ctrlAccent.z), panelL) >= 3.0f)
            break;
      }
      style.Colors[ImGuiCol_CheckMark] = ctrlAccent;
      style.Colors[ImGuiCol_SliderGrab] = ImVec4(ctrlAccent.x, ctrlAccent.y, ctrlAccent.z, 0.85f);
      style.Colors[ImGuiCol_SliderGrabActive] = ctrlAccent;
      style.Colors[ImGuiCol_Button] = shade(t.panelBg, 0.08f);
      style.Colors[ImGuiCol_ButtonHovered] = shade(t.panelBg, 0.18f);
      style.Colors[ImGuiCol_ButtonActive] = isLight ? shade(t.panelBg, 0.32f) : vec(t.accent, 0.65f);
      style.Colors[ImGuiCol_Header] = selected;
      style.Colors[ImGuiCol_HeaderHovered] = hover;
      style.Colors[ImGuiCol_HeaderActive] = pressed;
      style.Colors[ImGuiCol_Separator] = vec(t.border);
      style.Colors[ImGuiCol_SeparatorHovered] = vec(t.accent, 0.6f);
      style.Colors[ImGuiCol_SeparatorActive] = vec(t.accent);
      style.Colors[ImGuiCol_ResizeGrip] = vec(t.border, 0.4f);
      style.Colors[ImGuiCol_ResizeGripHovered] = vec(t.accent, 0.6f);
      style.Colors[ImGuiCol_ResizeGripActive] = vec(t.accent);
      style.Colors[ImGuiCol_Tab] = vec(t.panelBg);
      style.Colors[ImGuiCol_TabHovered] = hover;
      style.Colors[ImGuiCol_TabActive] = selected;
      style.Colors[ImGuiCol_TabUnfocused] = vec(t.windowBg);
      style.Colors[ImGuiCol_TabUnfocusedActive] = vec(t.panelBg);
      style.Colors[ImGuiCol_TextSelectedBg] = vec(t.accent, 0.35f);
      style.Colors[ImGuiCol_DragDropTarget] = vec(t.accent);
      style.Colors[ImGuiCol_NavHighlight] = vec(t.accent);
      style.Colors[ImGuiCol_TableHeaderBg] = shade(t.panelBg, 0.16f);
      style.Colors[ImGuiCol_TableBorderStrong] = vec(t.border);
      style.Colors[ImGuiCol_TableBorderLight] = vec(t.border, 0.5f);
      style.Colors[ImGuiCol_TableRowBg] = vec(t.windowBg, 0.0f);
      style.Colors[ImGuiCol_TableRowBgAlt] = vec(t.text, isLight ? 0.03f : 0.04f);

      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      ed::Style& edStyle = ed::GetStyle();
      edStyle.Colors[ed::StyleColor_Bg] = vec(t.windowBg, isLight ? 1.0f : 0.784f);
      edStyle.Colors[ed::StyleColor_Grid] = vec(t.border, isLight ? 0.16f : 0.12f);
      edStyle.Colors[ed::StyleColor_NodeBg] = vec(t.panelBg, isLight ? 0.95f : 0.80f);
      edStyle.Colors[ed::StyleColor_NodeBorder] = vec(t.border, isLight ? 0.70f : 0.40f);
      edStyle.GroupRounding = tok::radius_group;
      edStyle.Colors[ed::StyleColor_HovLinkBorder] = vec(t.accent, 0.90f);
      edStyle.Colors[ed::StyleColor_SelLinkBorder] = vec(t.accent, 1.0f);
      edStyle.Colors[ed::StyleColor_NodeSelRect] = vec(t.accent, 0.08f);
      edStyle.Colors[ed::StyleColor_NodeSelRectBorder] = vec(t.accent, 0.60f);
      edStyle.Colors[ed::StyleColor_LinkSelRect] = vec(t.accent, 0.08f);
      edStyle.Colors[ed::StyleColor_LinkSelRectBorder] = vec(t.accent, 0.60f);
      edStyle.GridSpacing = gGridSnap;
      ed::SetCurrentEditor(prevEditor);
   }

   IModulator* ModulatorForOutput(INode* node, int outputIndex)
   {
      if (node == nullptr)
         return nullptr;
      if (IModulator* specific = node->ModulatorOutput(outputIndex))
         return specific;
      return outputIndex == 0 ? dynamic_cast<IModulator*>(node) : nullptr;
   }

   // Turbo: O(1) lookups. gNodes/gLinks are mutated from many places, so the
   // position caches are hints, validated on every hit and repaired by a
   // linear scan on a miss - never trusted blindly, never stale.
   std::unordered_map<int, size_t> gNodePosHint;
   GraphNode* FindNodeByIndex(int index)
   {
      auto hint = gNodePosHint.find(index);
      if (hint != gNodePosHint.end() && hint->second < gNodes.size() &&
          gNodes[hint->second].index == index)
         return &gNodes[hint->second];
      for (size_t i = 0; i < gNodes.size(); ++i)
      {
         if (gNodes[i].index == index)
         {
            gNodePosHint[index] = i;
            return &gNodes[i];
         }
      }
      if (hint != gNodePosHint.end())
         gNodePosHint.erase(hint);
      return nullptr;
   }
