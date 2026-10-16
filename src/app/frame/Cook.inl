// Part of src/main.cpp, included there in order (one translation unit).
// expressions, modulation, cooking, rendering, swap

      // ---- expression globals ----
      // A flat, ordered list of name/expression rows. Deliberately not a node:
      // a global is read by name from anywhere in the patch, and giving it a
      // position on the canvas would imply a wire that doesn't exist. It is a
      // property of the document, so it lives in a document-level window and
      // is saved with the patch (core/ExprGlobals.h).
      if (gGlobalsOpen)
      {
         ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_FirstUseEver);
         if (ImGui::Begin("Expression globals", &gGlobalsOpen))
         {
            ImGui::TextDisabled("named values every '=' parameter expression can read");
            ImGui::TextDisabled("each row sees t and the rows above it:  beat = mod(t * 2, 1) < 0.5");
            if (ImGui::CollapsingHeader("language reference"))
            {
               ImGui::TextDisabled("operators   + - * / %% ^   < <= > >= == !=   && || !");
               ImGui::TextDisabled("functions   sin cos tan abs sign sqrt exp log pow");
               ImGui::TextDisabled("            floor ceil round mod min max clamp lerp");
               ImGui::TextDisabled("            step(edge,x) smoothstep(e0,e1,x) if(cond,a,b)");
               ImGui::TextDisabled("            rand(speed) rand(min,max,speed) sh(min,max,speed)");
               ImGui::TextDisabled("bound       t = transport seconds, pi");
               ImGui::TextDisabled("in a param  lo / hi = that param's own range, plus its siblings");
               ImGui::TextDisabled("            a sibling of the same name shadows a global");
            }
            ImGui::Separator();

            std::vector<ExprGlobals::Global>& globals = ExprGlobals::All();
            int removeAt = -1;
            for (size_t i = 0; i < globals.size(); i++)
            {
               ExprGlobals::Global& g = globals[i];
               ImGui::PushID((int)i);

               char nameBuf[64];
               snprintf(nameBuf, sizeof(nameBuf), "%s", g.name.c_str());
               ImGui::SetNextItemWidth(130.0f);
               if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
               {
                  PushUndoCheckpoint();
                  g.name = nameBuf;
                  gPatchDirty = true;
               }

               ImGui::SameLine();
               ImGui::TextUnformatted("=");
               ImGui::SameLine();

               char exprBuf[512];
               snprintf(exprBuf, sizeof(exprBuf), "%s", g.expr.c_str());
               ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150.0f);
               if (ImGui::InputText("##expr", exprBuf, sizeof(exprBuf)))
               {
                  PushUndoCheckpoint();
                  g.expr = exprBuf;
                  gPatchDirty = true;
               }

               // Live value, so a row can be checked without patching it into
               // something first - the same reason the readout strip exists on
               // the audio nodes.
               ImGui::SameLine();
               ImGui::TextDisabled("%.4f", g.value);
               ImGui::SameLine();
               if (ImGui::SmallButton("x"))
                  removeAt = (int)i;

               if (!g.error.empty())
                  ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.5f, 1.0f), "   %s", g.error.c_str());

               ImGui::PopID();
            }

            if (removeAt >= 0)
            {
               PushUndoCheckpoint();
               globals.erase(globals.begin() + removeAt);
               gPatchDirty = true;
            }

            ImGui::Separator();
            if (ImGui::Button("Add global", ImVec2(110, 0)))
            {
               PushUndoCheckpoint();
               char name[32];
               snprintf(name, sizeof(name), "g%d", (int)globals.size() + 1);
               globals.push_back({ name, "0", 0.0f, std::string() });
               gPatchDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Presets...", ImVec2(110, 0)))
            {
               ImGui::OpenPopup("ExprPresetsMenu");
            }

            if (ImGui::BeginPopup("ExprPresetsMenu"))
            {
               ImGui::TextDisabled("Click to insert preset global:");
               ImGui::Separator();
               std::string currentCategory;
               for (const ExprGlobals::Preset& p : ExprGlobals::Presets())
               {
                  if (p.category != currentCategory)
                  {
                     if (!currentCategory.empty())
                        ImGui::Separator();
                     currentCategory = p.category;
                     ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", currentCategory.c_str());
                  }

                  char itemLabel[256];
                  snprintf(itemLabel, sizeof(itemLabel), "%s = %s", p.name.c_str(), p.expr.c_str());
                  if (ImGui::MenuItem(itemLabel, nullptr))
                  {
                     PushUndoCheckpoint();
                     bool found = false;
                     for (ExprGlobals::Global& g : globals)
                     {
                        if (g.name == p.name)
                        {
                           g.expr = p.expr;
                           found = true;
                           break;
                        }
                     }
                     if (!found)
                     {
                        globals.push_back({ p.name, p.expr, 0.0f, std::string() });
                     }
                     gPatchDirty = true;
                  }
                  if (ImGui::IsItemHovered())
                  {
                     ImGui::SetTooltip("%s\nFormula: %s", p.description.c_str(), p.expr.c_str());
                  }
               }
               ImGui::EndPopup();
            }
         }
         ImGui::End();
      }

      if (gHelpOpen)
         DrawHelpWindow(&gHelpOpen);

      if (gShowUnsavedChangesModal)
      {
         ImGui::OpenPopup("Unsaved Changes");
         gShowUnsavedChangesModal = false;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::Text("This patch has unsaved changes.");
         ImGui::Text("Save before closing?");
         ImGui::Separator();
         // Proceeds only once the save actually went through - a cancelled
         // Save As dialog or a write failure never sets gUnsavedSaveDone, so
         // the modal stays up and the user can try again.
         if (gUnsavedSaveDone)
         {
            gUnsavedSaveDone = false;
            if (!gPatchDirty)
            {
               if (gPendingUnsavedAction)
                  gPendingUnsavedAction();
               gPendingUnsavedAction = nullptr;
               ImGui::CloseCurrentPopup();
            }
         }
         if (ImGui::Button("Save", ImVec2(100, 0)))
            SavePatchInteractive(false, []() { gUnsavedSaveDone = true; });
         ImGui::SameLine();
         if (ImGui::Button("Don't Save", ImVec2(100, 0)))
         {
            if (gPendingUnsavedAction)
               gPendingUnsavedAction();
            gPendingUnsavedAction = nullptr;
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         if (ImGui::Button("Cancel", ImVec2(100, 0)))
         {
            gPendingUnsavedAction = nullptr;
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      // Keep the title bar in sync with the open document. GLFW has no
      // native "dirty dot" hook, so the bullet is just part of the string -
      // the same convention every other non-native-document-model app uses.
      {
         static std::string sLastTitle;
         std::string base = gPatchPath.empty()
            ? std::string("Untitled")
            : gPatchPath.substr(gPatchPath.find_last_of("/\\") + 1);
         std::string title = (gPatchDirty ? std::string("\xE2\x80\xA2 ") : std::string()) +
            base + " \xE2\x80\x94 " INFINITE_TURBO_TITLE;
         if (title != sLastTitle)
         {
            glfwSetWindowTitle(window, title.c_str());
            sLastTitle = title;
         }
      }

      // A node (e.g. SamplerNode::StartRecording/StopRecording) asked for a
      // topology rebuild outside the usual connect/disconnect/spawn/delete
      // actions - see AudioTopologyRequest.h for why it can't just call
      // RebuildAudioTopology() itself.
      // Turbo 0.50: a watched node's latency changed (Super Mixer limiter
      // switched or its lookahead moved): re-run delay compensation.
      for (const LatencyWatch& w : gLatencyWatch)
         if (w.node->LatencySamples() != w.latency)
         {
            AudioTopologyRequest::Request();
            break;
         }
      // Turbo: a Send/Receive's channel, a Send's cable or the set of Sends
      // changed since last frame - re-point the hidden audio/note cables.
      if (UpdateWirelessCables())
         AudioTopologyRequest::Request();
      if (AudioTopologyRequest::PendingRebuild())
      {
         AudioTopologyRequest::PendingRebuild() = false;
         RebuildAudioTopology();
      }

      // ---- apply modulation and expressions, then cook ----
      // Deliberately after the UI: the parameter registry is rebuilt every frame
      // while nodes draw, so every pointer here belongs to a node that still
      // exists. Cooking before the UI would mean writing through last frame's
      // pointers, which dangle the moment a node is deleted.
      {
         Modulation& modulation = Modulation::Instance();
         // Snapshot every registered parameter's current value, keyed by node,
         // before any of this frame's writes land. This is what lets an
         // expression reference a sibling parameter by name ("width * 0.5")
         // without depending on the order params happened to draw in - and it
         // means a cycle between two expressions just settles a frame late
         // rather than reading half-updated state mid-pass.
         std::map<int, std::map<std::string, float>> paramSnapshot;
         for (const ParamRef& ref : modulation.FrameParams())
            if (ref.value != nullptr)
               paramSnapshot[ref.nodeIndex][ref.name] = *ref.value;

         // Fixture only: put one envelope field into the expression state, so
         // the UI fixture also covers the fx badge and the clear button - the
         // layout that spilled out of the panel and over the next column.
         // Parameter indices are positional and only exist once the node has
         // drawn, so this has to run here, where FrameParams is populated,
         // rather than beside the node's spawn.
         if (getenv("INFINITE_AUDIOUITEST") != nullptr && frameId == 2 && gNodes.size() > 1)
         {
            int found = 0, foundDecay = 0;
            for (const ParamRef& ref : modulation.FrameParams())
            {
               if (ref.nodeIndex != gNodes[1].index)
                  continue;
               if (ref.name == "release" && ++found == 3) // engine A's filter release
                  modulation.SetExpression(ref.nodeIndex, ref.paramIndex, "lerp(lo, hi, 0.5)");
               // ...and, under INFINITE_AUDIOUITEST_TYPING, leave engine A's
               // filter decay open in formula-entry mode - the other layout
               // that spilled, since the typed field used to draw its caption
               // outside the box to the right. Behind its own flag because a
               // fixture that opens with a focused text field swallows the
               // keyboard until you click away.
               if (ref.name == "decay" && ++foundDecay == 3 &&
                   getenv("INFINITE_AUDIOUITEST_TYPING") != nullptr)
               {
                  const std::pair<int, int> key(ref.nodeIndex, ref.paramIndex);
                  gTypedParamText[key] = "=lerp(lo, hi, 0.25)";
                  gTypedParam.insert(key);
                  gTypedParamJustOpened = key;
               }
            }
         }

         // Turbo 0.46: Performance Mode - MIDI-bound controls, then this
         // frame's writes from the matrix, before modulation reads anything.
         UpdatePerformanceMatrixMIDI();
         gPerfSlotWrites.clear();
         for (const ParamRef& ref : modulation.FrameParams())
         {
            if (ref.value == nullptr)
               continue;
            auto it = gPerfPendingWrites.find({ ref.nodeIndex, ref.paramIndex });
            if (it == gPerfPendingWrites.end())
               continue;
            const float lo = std::min(ref.minValue, ref.maxValue), hi = std::max(ref.minValue, ref.maxValue);
            *ref.value = std::clamp(it->second, lo, hi);
            gPerfSlotWrites[{ ref.nodeIndex, ref.paramIndex }] = *ref.value;
         }
         gPerfPendingWrites.clear();

         // Turbo 0.50: Scenes also ticks here (frame-guarded), so quantized
         // scene changes land even when the graph is hidden (Performance Mode).
         for (GraphNode& gn : gNodes)
            if (auto* sc = dynamic_cast<ScenesNode*>(gn.node.get()))
               sc->Tick(ImGui::GetFrameCount());

         const double t = Transport::Instance().Seconds();
         // Patch-wide named values, evaluated once before any parameter reads
         // them so every expression in the frame sees the same globals - see
         // core/ExprGlobals.h.
         ExprGlobals::EvaluateAll(t);
         const std::map<std::string, float>& globals = ExprGlobals::Values();
         const double midiNow = ImGui::GetTime();
         MidiMap::Update(midiNow);
         for (const ParamRef& ref : modulation.FrameParams())
         {
            if (ref.value == nullptr)
               continue;
            const Modulation::Source src = modulation.ModulatorFor(ref.nodeIndex, ref.paramIndex);
            // Turbo 0.43.1: a timeline clip can switch a param's modulation
            // off while it plays - the knob rests at the binding's centre.
            if (src.nodeIndex >= 0 && ArrangeModBypassed(ref.nodeIndex, ref.paramIndex))
            {
               *ref.value = std::min(std::max(src.centre, std::min(ref.minValue, ref.maxValue)),
                                     std::max(ref.minValue, ref.maxValue));
               continue;
            }
            // Turbo MIDI learn: a new value from the mapped control is
            // written here; a wired modulation cable always wins.
            if (src.nodeIndex < 0 && MidiMap::Find(ref.nodeIndex, ref.paramIndex) != nullptr)
               MidiMap::Apply(ref, midiNow);
            if (src.nodeIndex >= 0)
            {
               // A wired modulator always wins over a typed expression - see
               // Modulation::SetExpression.
               GraphNode* modNode = FindNodeByIndex(src.nodeIndex);
               if (modNode == nullptr)
                  continue;
               auto* modulator = ModulatorForOutput(modNode->node.get(), src.outputIndex);
               if (modulator == nullptr)
                  continue;
               // Value01() is documented to return 0..1, but InvertNode and
               // RangeToRangeNode (clampOutput=false) deliberately violate
               // that on purpose, and a kAbsolute binding lets them
               // extrapolate past the destination's declared range instead
               // of being silently flattened at the edge - see
               // docs/plans/modulators/00-modulation-polarity.md §4. So,
               // unlike before, v01 is not force-clamped here.
               float v01 = modulator->Value01();
               // Turbo range mapper (see Modulation::Source::HasRange).
               if (src.HasRange())
               {
                  const float span = src.inMax - src.inMin;
                  float t = std::fabs(span) < 1e-6f ? 0.0f : (v01 - src.inMin) / span;
                  t = std::clamp(t, 0.0f, 1.0f);
                  v01 = src.outMin + (src.outMax - src.outMin) * t;
               }
               if (src.polarity == Modulation::Source::kBipolar)
               {
                  // Swings around wherever the knob was sitting when the
                  // cable landed (src.centre, captured once at Bind()/patch-
                  // load time - see Modulation::Bind), rather than overriding
                  // the parameter outright the way kAbsolute does. Clamped to
                  // the destination's range: unlike kAbsolute, "push past the
                  // knob's own range" isn't a meaningful ask for a control
                  // that's still centred on a value inside it.
                  const float swung =
                     src.centre + (v01 - 0.5f) * 2.0f * src.depth * (ref.maxValue - ref.minValue);
                  *ref.value = std::min(ref.maxValue, std::max(ref.minValue, swung));
               }
               else
               {
                  *ref.value = ref.minValue + (ref.maxValue - ref.minValue) * v01;
               }
               continue;
            }
            const std::string* expr = modulation.ExpressionFor(ref.nodeIndex, ref.paramIndex);
            if (expr == nullptr)
               continue;
            float result = 0.0f;
            std::string error;
            // `lo`/`hi` bind this param's own range, so an expression can be
            // written in normalised terms the way a wired modulator already
            // works: `=lerp(lo, hi, sin(t) * 0.5 + 0.5)` sweeps the full range,
            // where the bare `=sin(t)` people reach for first lands in raw
            // units and clamps to nothing on a param measured in milliseconds.
            // Both spellings stay available - raw units are what you want for
            // `=250` or `=width * 0.5`.
            std::map<std::string, float>& siblings = paramSnapshot[ref.nodeIndex];
            const auto savedLo = siblings.find("lo");
            const auto savedHi = siblings.find("hi");
            const bool hadLo = savedLo != siblings.end(), hadHi = savedHi != siblings.end();
            const float prevLo = hadLo ? savedLo->second : 0.0f;
            const float prevHi = hadHi ? savedHi->second : 0.0f;
            siblings["lo"] = ref.minValue;
            siblings["hi"] = ref.maxValue;
            const bool evaluated = Expression::Evaluate(*expr, t, &siblings, &globals, result, error);
            if (hadLo) siblings["lo"] = prevLo; else siblings.erase("lo");
            if (hadHi) siblings["hi"] = prevHi; else siblings.erase("hi");
            if (evaluated)
            {
               *ref.value = std::min(std::max(result, ref.minValue), ref.maxValue);
               modulation.SetExpressionError(ref.nodeIndex, ref.paramIndex, std::string());
            }
            else
            {
               // Leave the last good value in place rather than snapping to 0 -
               // a typo mid-edit should not blank out the render.
               modulation.SetExpressionError(ref.nodeIndex, ref.paramIndex, error);
            }
         }
      }

      // Turbo: gesture loops play back into their param, unless a cable or an
      // expression already owns it (same precedence as upstream).
      GestureSyncClockAxis();
      const double gestureNow = GesturePlaybackClock();
      for (const ParamRef& ref : Modulation::Instance().FrameParams())
      {
         if (ref.value == nullptr || Modulation::Instance().IsModulated(ref.nodeIndex, ref.paramIndex) ||
             Modulation::Instance().HasExpression(ref.nodeIndex, ref.paramIndex))
            continue;
         float v = 0.0f;
         if (GestureRecorder::Instance().GetPlaybackValue(ref.nodeIndex, ref.paramIndex, gestureNow, v))
            *ref.value = std::clamp(v, std::min(ref.minValue, ref.maxValue), std::max(ref.minValue, ref.maxValue));
      }

      // A Palette is not an Output, so nothing downstream pulls it, and both its
      // preview and its bindings need this frame's swatches. Cooking here - after
      // modulation has been applied - is what lets a modulator drive the shaping
      // controls and have it land the same frame.
      for (GraphNode& gn : gNodes)
      {
         if (dynamic_cast<IPaletteSource*>(gn.node.get()) != nullptr)
            gn.node->CookIfNeeded(frameId);
      }

      // Colours, the same way and for the same reason: the registry was rebuilt
      // while the nodes drew, so every pointer here belongs to a node that
      // still exists. A palette has to cook before it can be read, and it is
      // not an Output so nothing else would pull it.
      {
         PaletteBinding& palette = PaletteBinding::Instance();
         for (const ColorRef& ref : palette.FrameColors())
         {
            const PaletteBinding::Source src = palette.SourceFor(ref.nodeIndex, ref.colorIndex);
            if (src.nodeIndex < 0 || ref.value == nullptr)
               continue;
            GraphNode* palNode = FindNodeByIndex(src.nodeIndex);
            if (palNode == nullptr)
               continue;
            auto* source = dynamic_cast<IPaletteSource*>(palNode->node.get());
            if (source == nullptr)
               continue;
            source->GetSwatch(src.swatchIndex, ref.value);
         }
      }

      for (GraphNode& gn : gNodes)
      {
         if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr ||
             dynamic_cast<SyphonOutNode*>(gn.node.get()) != nullptr ||
             dynamic_cast<OscSendNode*>(gn.node.get()) != nullptr ||
             dynamic_cast<MidiOutNode*>(gn.node.get()) != nullptr) // Turbo 0.57: a sink, nothing pulls it
            gn.node->CookIfNeeded(frameId);
      }
      if (getenv("INFINITE_SHOWCASE") != nullptr && frameId == 1)
      {
         for (const ParamRef& ref : Modulation::Instance().FrameParams())
         {
            if (ref.nodeIndex == gNodes[1].index && ref.name == "Amount")
               Modulation::Instance().Bind(ref.nodeIndex, ref.paramIndex, gNodes[5].index);
         }
      }

      if (getenv("INFINITE_MACROTEST") != nullptr)
      {
         auto& mod = Modulation::Instance();
         if (gNodes.size() < 3)
         {
            printf("MACROTEST fixture missing (%zu nodes)\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return 1;
         }
         auto* xy = static_cast<MacroXYNode*>(gNodes[2].node.get());
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         if (frameId == 2)
         {
            int sizeParam = -1, rotParam = -1;
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex != gNodes[0].index) continue;
               if (ref.name == "size") sizeParam = ref.paramIndex;
               if (ref.name == "rotation") rotParam = ref.paramIndex;
            }
            // X drives size, Y drives rotation - one pad, two destinations
            printf("sizeParam=%d rotParam=%d node0=%d node2=%d frameParams=%zu\n",
                   sizeParam, rotParam, gNodes[0].index, gNodes[2].index, mod.FrameParams().size());
            mod.Bind(gNodes[0].index, sizeParam, gNodes[2].index, 0);
            mod.Bind(gNodes[0].index, rotParam, gNodes[2].index, 1);
            printf("isModulated(size)=%d isModulated(rot)=%d\n",
                   (int)mod.IsModulated(gNodes[0].index, sizeParam),
                   (int)mod.IsModulated(gNodes[0].index, rotParam));
            xy->padX = 0.25f;
            xy->padY = 0.75f;
            printf("bound X->size Y->rotation\n");
         }
         if (frameId == 5)
         {
            printf("padX=%.2f -> size=%.4f (expect %.4f)\n", xy->padX, sh->size, 0.01f + 0.49f * 0.25f);
            printf("padY=%.2f -> rotation=%.4f (expect %.4f)\n", xy->padY, sh->rotation, -180.0f + 360.0f * 0.75f);
            xy->padX = 0.9f; xy->padY = 0.1f;
         }
         if (frameId == 8)
         {
            printf("after move: size=%.4f rotation=%.4f  %s\n", sh->size, sh->rotation,
                   (std::fabs(sh->size - (0.01f + 0.49f * 0.9f)) < 0.01f &&
                    std::fabs(sh->rotation - (-180.0f + 360.0f * 0.1f)) < 3.0f)
                      ? "INDEPENDENT OUTPUTS OK" : "MISMATCH");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_HIDETEST") != nullptr)
      {
         auto& mod = Modulation::Instance();
         if (frameId == 2)
         {
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "size x") // Shape size is split per axis now
                  mod.Bind(ref.nodeIndex, ref.paramIndex, gNodes[2].index);
            }
            printf("bound: links=%zu (frameParams=%zu)\n", mod.Links().size(), mod.FrameParams().size());
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index)
                  printf("   node0 param %d = %s\n", ref.paramIndex, ref.name.c_str());
            }
         }
         if (frameId == 4)
         {
            gNodes[0].showParams = false; // collapse the modulated node
            printf("params hidden\n");
         }
         if (frameId == 8)
         {
            printf("after hide: links=%zu %s\n", mod.Links().size(),
                   mod.Links().empty() ? "LOST - BUG" : "SURVIVED OK");
            // The binding surviving is not enough: a collapsed node still has to
            // show the cable, landing on its "mod" tag.
            int drawn = 0;
            for (const LinkInfo& link : gLinks)
               if (GraphNode::IsParamPin(link.dstPin) &&
                   GraphNode::NodeIndexFromPin(link.dstPin) == gNodes[0].index)
                  drawn++;
            printf("cable while hidden: %d %s\n", drawn,
                   drawn > 0 ? "DRAWN OK" : "MISSING - BUG");
         }
         if (frameId == 9)
         {
            gNodes[0].showParams = true; // reopen
         }
         if (frameId == 12)
         {
            printf("after reopen: links=%zu %s\n", mod.Links().size(),
                   mod.Links().empty() ? "LOST - BUG" : "SURVIVED OK");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_MODTEST") != nullptr)
      {
         if (frameId == 1)
         {
            // Shape.size is param index 4 (shape dropdown isn't a ModSlider):
            // width, height, size, aspect... resolve it by name instead of guessing.
            int sizeParam = -1;
            for (const ParamRef& ref : Modulation::Instance().FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "size")
                  sizeParam = ref.paramIndex;
            }
            printf("size param index = %d\n", sizeParam);
            if (sizeParam >= 0)
               Modulation::Instance().Bind(gNodes[0].index, sizeParam, gNodes[2].index);
            Transport::Instance().SetTempo(240.0f);
         }
         if (frameId >= 2 && frameId <= 40 && (frameId % 8) == 0)
         {
            auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
            auto* lfo = static_cast<LFONode*>(gNodes[2].node.get());
            printf("f%-3d beats=%.3f lfo=%.3f  shape.size=%.4f\n",
                   frameId, Transport::Instance().Beats(), lfo->Value01(), sh->size);
         }
         if (frameId == 44)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (getenv("INFINITE_OSCTEST") != nullptr)
      {
         if (gNodes.size() < 5)
         {
            printf("OSCTEST fixture missing (%zu nodes)\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return 1;
         }
         auto* constNode = static_cast<ConstantNode*>(gNodes[2].node.get());
         auto* send = static_cast<OscSendNode*>(gNodes[3].node.get());
         auto* recv = static_cast<OscReceiveNode*>(gNodes[4].node.get());
         if (frameId == 1)
         {
            // A dedicated port, not the 9000 default both nodes spawn with -
            // proves the port field itself (and the receive listener restart
            // it triggers) actually takes effect, not just the wiring.
            const int testPort = 9737;
            constNode->value = 0.73f;
            send->host = "127.0.0.1";
            send->port = testPort;
            send->address = "/infinite/osctest";
            recv->port = testPort;
            recv->address = "/infinite/osctest";
         }
         if (frameId == 60)
         {
            const float got = recv->Value01();
            const bool ok = std::fabs(got - constNode->value) < 0.02f;
            printf("osc sent=%.3f received=%.3f  %s\n", constNode->value, got,
                   ok ? "ROUND TRIP OK" : "BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      for (GraphNode& gn : gNodes)
         gn.node->CookIfNeeded(frameId);
      // Turbo 0.43: the timeline monitor shows this frame's textures.
      ArrangeCompositeMonitorIfRequested(frameId);
      ArrangeRenderPump(frameId); // Turbo 0.43.1: offline timeline render, one frame per iteration

      // The node cook loop above is the longest single stretch of the frame,
      // and it runs after glfwPollEvents() with the run loop otherwise
      // unserviced - see local-prompts/02-plugin-editor-lag.md. Pump it here
      // so a hosted plugin's editor window (the app's only real NSWindow)
      // doesn't sit starved for the length of a heavy cook.
      if (Platform::AnyPluginEditorOpen())
         Platform::PumpPluginEditorEvents();

      // Top-level idle gate: NodeWorkCounter() only advances when some node
      // actually redid real work this frame (FilterNode's RunShaderPass,
      // Render3DNode's draw passes, ...) - a cache hit leaves it alone. A
      // patch with nothing left to compute settles into idle==true every
      // frame within a couple frames of its last real edit, the same way a
      // static Blender viewport stops re-rendering. This only observes that;
      // it deliberately does not skip glfwSwapBuffers/ImGui's own redraw,
      // since those still have to run for UI responsiveness (hover states,
      // cursor, animated widgets) regardless of whether the node graph is idle.
      if (getenv("INFINITE_CACHETEST") != nullptr)
      {
         static unsigned long long sPrevWork = 0;
         static int sIdleStreak = 0;
         const unsigned long long work = NodeWorkCounter();
         const bool idle = (work == sPrevWork);
         sIdleStreak = idle ? sIdleStreak + 1 : 0;
         sPrevWork = work;
         printf("CACHETEST frame=%d work=%llu idle=%d idleStreak=%d\n",
                frameId, work, idle ? 1 : 0, sIdleStreak);
      }

      if (getenv("INFINITE_TEXTFIT") != nullptr && frameId == 4 && !gNodes.empty())
      {
         if (auto* t = dynamic_cast<TextNode*>(gNodes[0].node.get()))
            printf("requested %.0f pt -> fitted %.1f pt (box %.0fx%.0f of %dx%d)\n",
                   t->fontSize, t->FittedSize(),
                   t->width * t->wrapWidth, t->height * t->wrapHeight,
                   t->GetOutputWidth(), t->GetOutputHeight());
      }

      if (selfTest && frameId >= 1)
      {
         int failures = 0;
         for (GraphNode& gn : gNodes)
         {
            // modulators emit a value, not a texture, so they are checked
            // differently - including nodes that expose taps rather than being
            // modulators themselves (Image/Audio Analyze).
            if (dynamic_cast<CameraNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<LightNode*>(gn.node.get()) != nullptr)
            {
               printf("%-22s [%-12s] scene node     OK\n", gn.typeName.c_str(), gn.category.c_str());
               continue;
            }
            if (auto* op = dynamic_cast<GeometryOpNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), op->TriangleCount());
               continue;
            }
            if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu instances  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), inst->InstanceCount());
               continue;
            }
            if (auto* ps = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu particles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), ps->AliveCount());
               continue;
            }
            if (dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MappingNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<ClothNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<WrapNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<Group3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<DelaunayMeshNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<CurveOpsNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<PopNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<CurveNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr)
            {
               // Pass-throughs and samplers with nothing patched in are empty
               // by definition, so that is not a failure.
               printf("%-22s [%-12s] geometry pass  OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }
            if (auto* oc = dynamic_cast<OceanNode*>(gn.node.get()))
            {
               const bool ok = oc->TriangleCount() > 0;
               if (!ok) ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), oc->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }
            if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), t3d->TriangleCount());
               continue;
            }
            if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
            {
               // A freshly spawned Model 3D has no file yet, so an empty mesh
               // here is correct rather than a failure - same as a Geometry Op
               // with nothing patched into it.
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), model->TriangleCount());
               continue;
            }
            if (auto* geo = dynamic_cast<GeometryNode*>(gn.node.get()))
            {
               // geometry emits a mesh, not a texture
               const bool ok = geo->TriangleCount() > 0;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), geo->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }

            if (dynamic_cast<IAudioSource*>(gn.node.get()) != nullptr ||
                dynamic_cast<AudioOutputNode*>(gn.node.get()) != nullptr)
            {
               // P2's audio-graph nodes (AudioFileNode included, now that
               // it's a real IAudioSource) emit real-time samples, not a
               // texture or a 0-1 modulator value.
               printf("%-22s [%-12s] audio node     OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }

            IModulator* mod = dynamic_cast<IModulator*>(gn.node.get());
            if (mod == nullptr)
               mod = gn.node->ModulatorOutput(0);
            if (mod != nullptr)
            {
               const float v = mod->Value01();
               const bool ok = v >= 0.0f && v <= 1.0f;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] value=%.3f      %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), v, ok ? "OK" : "FAIL");
               continue;
            }

            bool ok = gn.node->GetOutputWidth() > 0 && gn.node->GetOutputTexture() != 0;
            if (!ok)
               ++failures;
            printf("%-22s [%-12s] %dx%d tex=%-3u %s\n",
                   gn.typeName.c_str(), gn.category.c_str(),
                   gn.node->GetOutputWidth(), gn.node->GetOutputHeight(),
                   gn.node->GetOutputTexture(), ok ? "OK" : "FAIL");
         }
         printf("\n%zu node types, %d failures\n", gNodes.size(), failures);
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }



      ImGui::Render();
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      glViewport(0, 0, fbW, fbH);
      glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

      if (const char* shotPath = getenv("IMAGERESYNTH_SCREENSHOT"))
      {
         const int targetFrame = getenv("INFINITE_SCREENSHOT_FRAME") ? atoi(getenv("INFINITE_SCREENSHOT_FRAME")) : 12;
         if (frameId == targetFrame)
         {
            std::vector<unsigned char> px(fbW * fbH * 4);
            glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            stbi_flip_vertically_on_write(1);
            stbi_write_png(shotPath, fbW, fbH, 4, px.data(), fbW * 4);
            printf("wrote %s (%dx%d)\n", shotPath, fbW, fbH);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      ApplyMainSwapInterval((gVsync && gProjectorWindows.empty()) ? 1 : 0);
      glfwSwapBuffers(window);

      // glfwSwapBuffers blocks on vsync - dead time for AppKit to service a
      // hosted plugin's editor window. See local-prompts/02-plugin-editor-lag.md.
      if (Platform::AnyPluginEditorOpen())
         Platform::PumpPluginEditorEvents();

      // Projector output: blit this frame's cooked result of each open
      // window's node into that window. Runs after the editor's own swap so
      // it never shows last frame's texture a frame behind the graph.
      // Iterate backwards since a dead source erases its window mid-loop.
      static double sLastProjectorTopmostRefresh = 0.0;
      const double projectorNow = glfwGetTime();
      const bool refreshProjectorTopmost =
         projectorNow - sLastProjectorTopmostRefresh >= 0.5;
      for (size_t i = gProjectorWindows.size(); i-- > 0; )
      {
         GraphNode* src = FindNodeByIndex(gProjectorWindows[i].nodeIndex);
         if (src == nullptr)
         {
            // Its node was deleted out from under it - close rather than sit
            // there showing a permanently blank window.
            CloseProjectorWindow(i);
            continue;
         }

         GLFWwindow* projWindow = gProjectorWindows[i].window;
         if (gProjectorWindows[i].fullscreen && refreshProjectorTopmost)
            Platform::ReassertOutputWindowTopmost(projWindow);
         glfwMakeContextCurrent(projWindow);
         {
            // Backwards loop: index 0 is presented last, so it is the one
            // that waits for VSync (a single wait per frame, see
            // ApplyMainSwapInterval).
            const int wantInterval = (gVsync && i == 0) ? 1 : 0;
            if (gProjectorWindows[i].swapInterval != wantInterval)
            {
               glfwSwapInterval(wantInterval);
               gProjectorWindows[i].swapInterval = wantInterval;
            }
         }
         int pw, ph;
         glfwGetFramebufferSize(projWindow, &pw, &ph);

         // A geometry node's own GetOutputTexture() isn't a real preview (it
         // produces a mesh, not pixels) - render it the same way its
         // mini-viewport/viewport-panel card does instead, sharing that
         // node's own orbit camera so all three agree on framing.
         unsigned int tex = 0;
         int texW = 0, texH = 0;
         if (auto* geo = dynamic_cast<IGeometrySource*>(src->node.get()))
         {
            NodeViewport& viewport = gProjectorViewports[src->index];
            SharedViewportCamera& cam = gNodeCameras[src->index];
            tex = viewport.Render(geo, cam, pw, ph);
         }
         else
         {
            tex = src->node->GetOutputTexture();
            texW = src->node->GetOutputWidth();
            texH = src->node->GetOutputHeight();
         }

         if (tex != 0)
         {
            if (dynamic_cast<ProjectionNode*>(src->node.get()) != nullptr)
               GLUtil::DrawTextureToScreen(tex, pw, ph, 0, 0, /*checkerBg=*/false);
            else
               GLUtil::DrawTextureToScreen(tex, pw, ph, texW, texH, /*checkerBg=*/false,
                                           gProjectorWindows[i].fitMode, nullptr,
                                           /*transparentWindow=*/!gProjectorWindows[i].fullscreen &&
                                              OutputNodeHasTransparentBg(src->node.get()));
         }
         else
         {
            glViewport(0, 0, pw, ph);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
         }
         glfwSwapBuffers(projWindow);
      }
      if (refreshProjectorTopmost && !gProjectorWindows.empty())
         sLastProjectorTopmostRefresh = projectorNow;
      if (!gProjectorWindows.empty())
         glfwMakeContextCurrent(window);

      // Captures settings changed through menus, dock controls or a loaded
      // patch. It writes only when values differ from the previous frame.
      PersistSceneSettingsIfChanged();
      PollAutosave();
      PollPredictionAutosave();

      GLUtil::EndFrameScratchFbos();
      ++frameId;

      // Patch shortcuts. Handled outside the node editor so its own Cmd-key
      // bindings do not swallow them, and gated on no text field having focus
      // so typing an 'S' into a Text node does not save the patch.
      if (!ImGui::GetIO().WantTextInput &&
          ImGui::GetIO().KeyCtrl)
      {
         if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            SavePatchInteractive(ImGui::GetIO().KeyShift);
         else if (ImGui::IsKeyPressed(ImGuiKey_O, false))
         {
            StartFileDialog(Platform::OpenPatchDialog, [](const std::string& path) {
               GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
            });
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_N, false))
            GuardUnsavedChanges([]() { NewPatch(); });
         else if (ImGui::IsKeyPressed(ImGuiKey_M, false))
         {
            MidiMap::LearnMode() = !MidiMap::LearnMode();
            if (!MidiMap::LearnMode())
               MidiMap::Disarm();
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_B, false))
         {
            if (ImGui::GetIO().KeyShift)
               ToggleCanvasBackgroundForSelection();
            else
               gNodePanelOpen = !gNodePanelOpen;
         }
      }
      // Turbo: F11 in the editor = borderless fullscreen UI (an output
      // window keeps its own F11, it has its own key callback).
      if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
         SetMainWindowFullscreen(!gMainFullscreen);
      if (MidiMap::LearnMode() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      {
         if (MidiMap::AnyArmed())
            MidiMap::Disarm();
         else
            MidiMap::LearnMode() = false;
      }

      // Absolute-deadline frame limiter. Carrying the deadline forward avoids
      // permanently losing cadence after one oversleep on Windows.
      if (gTargetFps > 0)
      {
         static double sNextFrameDeadline = 0.0;
         const double budget = 1.0 / (double)gTargetFps;
         const double now = glfwGetTime();
         if (sNextFrameDeadline <= 0.0 || now > sNextFrameDeadline + budget * 2.0)
            sNextFrameDeadline = gFrameStart + budget;
         else
            sNextFrameDeadline += budget;
         const double deadline = sNextFrameDeadline;
         if (Platform::AnyPluginEditorOpen())
         {
            // Spend the idle slack servicing the editor's run loop instead of
            // sleeping through it - see local-prompts/02-plugin-editor-lag.md.
            while (glfwGetTime() < deadline)
               if (!Platform::PumpPluginEditorEvents())
                  std::this_thread::sleep_for(std::chrono::microseconds(200));
         }
         else
         {
            // Turbo: high-resolution timer sleep, then at most ~1 ms of spin.
            const double slack = deadline - glfwGetTime();
            if (slack > 0.0015)
               Platform::PreciseSleep(slack - 0.001);
            while (glfwGetTime() < deadline)
               std::this_thread::yield();
         }
      }

      {
         // Measured after the swap so the number includes GPU work the driver
         // blocks on there, which is where a heavy 3D render actually lands.
         static double sPrevTime = 0.0;
         const double now = glfwGetTime();
         if (sPrevTime > 0.0)
            gLastFrameMs = (now - sPrevTime) * 1000.0;
         sPrevTime = now;

         static double sLastPerformanceLog = 0.0;
         if (RuntimeLog::Enabled() && gTargetFps > 0 && now - sLastPerformanceLog >= 5.0)
         {
            const double budgetMs = 1000.0 / (double)gTargetFps;
            if (gLastFrameMs > budgetMs * 1.08)
               RuntimeLog::Write("performance: target=%d fps frame=%.2f ms", gTargetFps, gLastFrameMs);
            sLastPerformanceLog = now;
         }
      }

      // Dev harness: quit after N frames. The self-tests above printf their
      // verdict and then run forever, so a scripted run has to kill the app -
      // which throws away stdout still sitting in the block buffer when it is
      // redirected to a file. Exiting normally lets it flush.
      if (const char* exitAfter = getenv("INFINITE_EXITAFTER"))
      {
         if (frameId >= atoi(exitAfter))
         {
            // Drag-test fixtures print their verdict from inside their own
            // phase machine; if the run times out before ever finding a
            // usable row (sPhase stuck at 0), that machine never gets to
            // print anything and the driver silently reads "no FAIL" as a
            // pass. Say so explicitly instead.
            if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr && gSamplerDragTestPhase == 0)
               printf("SAMPLERDRAGTEST FAIL (timed out waiting for a usable row)\n");
            if (getenv("INFINITE_MEDIADRAGTEST") != nullptr && gMediaDragTestPhase == 0)
               printf("MEDIADRAGTEST FAIL (timed out waiting for a usable row)\n");
            if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr && gPluginDragTestPhase == 0)
               printf("PLUGINDRAGTEST FAIL (timed out waiting for a usable row)\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
