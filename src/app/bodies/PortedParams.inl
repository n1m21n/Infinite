   // Turbo 0.60: parameter UIs and bodies of the nodes ported from upstream
   // (Naman's Infinite), copied as they are there. Upstream-only helpers they
   // call come from app/ui/UpstreamCompat.inl.

   void DrawOpticalFlowParams(OpticalFlowNode* n)
   {
      static const std::vector<std::string> kViews = { "Vectors", "Colour" };
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      DropdownButton("view", kViews, n->view, [n](int i) { n->view = i; });
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      ModSlider("gain", &n->gain, 0.0f, 8.0f);
      ModSliderInt("levels", &n->levels, 1, OpticalFlowNode::kMaxLevels);
      ModSliderInt("window", &n->window, 1, 6);
      ModSliderInt("iterations", &n->iterations, 1, 6);
   }

   void DrawTimeMachineParams(TimeMachineNode* n)
   {
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      ModSliderInt("length", &n->length, 2, TimeMachineNode::kMaxLength);
      ModCheckbox("invert", &n->invert);
   }

   void DrawFluidParams(FluidNode* n)
   {
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      DropdownButton("view", FluidNode::ViewNames(), n->view, [n](int i) { n->view = i; });
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      DropdownButton("stirrer", FluidNode::StirrerNames(), n->stirrer, [n](int i) { n->stirrer = i; });
      ModSlider("stirrer strength", &n->stirrerStrength, 0.0f, 4.0f);
      ModSlider("stirrer size", &n->stirrerSize, 0.01f, 0.3f, "%.3f");
      ModSlider("force gain", &n->forceGain, 0.0f, 8.0f);
      ModSlider("swirl", &n->vorticity, 0.0f, 2.0f);
      ModSlider("viscosity", &n->viscosity, 0.0f, 0.2f, "%.3f");
      ModSlider("damping", &n->velocityDamping, 0.0f, 0.1f, "%.3f");
      ModSlider("dye fade", &n->dyeFade, 0.0f, 0.1f, "%.3f");
      ModSlider("image refresh", &n->refresh, 0.0f, 1.0f, "%.3f");
      ModSliderInt("pressure iterations", &n->pressureIterations, 1, FluidNode::kMaxPressureIterations);
      if (ActionButton::Draw("Reset", ImVec2(kPreviewSize, 0)))
         n->Reset();
   }

   void DrawVolumeParams(VolumeNode* n)
   {
      DropdownButton("shape", VolumeNode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      ModSlider("density", &n->density, 0.0f, 8.0f);
      ModSlider("size", &n->size, 0.05f, 6.0f);
      ModSlider("height", &n->height, 0.1f, 10.0f);
      ModSlider("center x", &n->centerX, -6.0f, 6.0f);
      ModSlider("center y", &n->centerY, -6.0f, 6.0f);
      ModSlider("center z", &n->centerZ, -6.0f, 6.0f);
      ModSlider("noise scale", &n->noiseScale, 0.1f, 6.0f);
      ModSlider("noise amount", &n->noiseAmount, 0.0f, 1.0f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f");
      ModSlider("wind x", &n->windX, -2.0f, 2.0f);
      ModSlider("wind y", &n->windY, -2.0f, 2.0f);
      ModSlider("wind z", &n->windZ, -2.0f, 2.0f);
      ModSlider("speed", &n->speed, 0.0f, 4.0f);
      ColorSwatch("colour", n->color, n);
      // Lighting, quality and the own-camera rows sit behind "show more" (GraphNode::showAdvancedParams, saved
      // with the patch). They are still registered in the same order, so indices, modulation, expressions and
      // the matrix are unchanged; a folded row with a cable, expression or palette binding opens the section.
      {
         constexpr int kFoldedParams = 14; // light azimuth .. fov
         GraphNode* gn = FindNodeByIndex(gCurrentNodeIndex);
         const bool savedRegisterOnly = gParamRegisterOnly;
         const bool pinned = gn != nullptr && gn->showAdvancedParams;
         bool inUse = PaletteBinding::Instance().IsBound(gCurrentNodeIndex, gColorCounter);
         for (int k = 0; k < kFoldedParams && !inUse; k++)
            inUse = Modulation::Instance().IsModulated(gCurrentNodeIndex, gParamCounter + k) ||
                    Modulation::Instance().HasExpression(gCurrentNodeIndex, gParamCounter + k);
         const bool open = pinned || inUse;
         ImGui::PushID("showMoreVolume");
         if (ActionButton::Column(inUse && !pinned ? "show more (in use)" : open ? "show less" : "show more",
                                  kParamWidth) &&
             gn != nullptr)
            gn->showAdvancedParams = !gn->showAdvancedParams;
         ImGui::PopID();
         gParamRegisterOnly = savedRegisterOnly || !open;
         ColorSwatch("light colour", n->lightColor, n);
         ModSlider("light azimuth", &n->lightAzimuth, -180.0f, 180.0f, "%.0f");
         ModSlider("light elevation", &n->lightElevation, -90.0f, 90.0f, "%.0f");
         ModSlider("light", &n->lightIntensity, 0.0f, 4.0f);
         ModSlider("ambient", &n->ambient, 0.0f, 2.0f);
         ModSlider("scatter", &n->scatter, 0.0f, 1.0f);
         ModSlider("anisotropy", &n->anisotropy, -0.9f, 0.9f);
         ModSliderInt("light steps", &n->lightSteps, 0, 16);
         ModSliderInt("steps", &n->steps, 8, 256);
         ModSliderInt("render steps", &n->renderSteps, 16, 512);
         ModSlider("max distance", &n->maxDistance, 1.0f, 100.0f, "%.0f");
         if (open)
            ImGui::TextDisabled("own camera (no Render 3D input):");
         ModSlider("cam azimuth", &n->camAzimuth, -180.0f, 180.0f, "%.0f");
         ModSlider("cam elevation", &n->camElevation, -89.0f, 89.0f, "%.0f");
         ModSlider("cam distance", &n->camDistance, 0.5f, 30.0f);
         ModSlider("fov", &n->fov, 10.0f, 120.0f, "%.0f");
         gParamRegisterOnly = savedRegisterOnly;
      }
   }

   void DrawNdiOutParams(NdiOutNode* n)
   {
      if (!Ndi::Available())
      {
         EmptyBlock("NDI runtime not found", "Install it from ndi.video");
         return;
      }

      ImGui::SetNextItemWidth(kPreviewSize);
      if (FieldWell::InputText("##ndi_name", &n->sourceNameInput, ImGuiInputTextFlags_EnterReturnsTrue))
         n->SetSourceName(n->sourceNameInput);
      if (ImGui::IsItemDeactivatedAfterEdit())
         n->SetSourceName(n->sourceNameInput);

      PushTextWrapWidth(kPreviewSize);
      if (n->IsPublishing())
      {
         ImGui::TextColored(tok::V4(tok::palf::v_400_1000_500_1000), "Broadcasting: %dx%d", n->PublishedWidth(), n->PublishedHeight());
         if (n->Connections() > 0)
            ImGui::TextColored(tok::V4(tok::palf::v_300_900_1000_1000), "Receivers: %d", n->Connections());
         else
            ImGui::TextDisabled("Receivers: waiting...");
      }
      else
         ImGui::TextDisabled("Connect an image input to publish");
      PopTextWrapWidth();
   }

   void DrawNdiInParams(NdiInNode* n)
   {
      PushTextWrapWidth(kPreviewSize);
      if (!Ndi::Available())
      {
         ImGui::TextDisabled("NDI runtime not found (install from ndi.video)");
         PopTextWrapWidth();
         return;
      }
      PopTextWrapWidth();

      const std::vector<std::string> sources = n->AvailableSources();
      if (sources.empty())
      {
         EmptyBlock("No NDI sources", "Sources on this network appear here");
         return;
      }

      int currentIdx = 0;
      for (size_t i = 0; i < sources.size(); i++)
         if (sources[i] == n->SourceName())
            currentIdx = (int)i;
      std::vector<std::string> labels;
      labels.reserve(sources.size());
      for (const std::string& s : sources)
         labels.push_back(s);
      DropdownButton("##ndi_source", labels, currentIdx, [n, sources](int idx) {
         n->SelectSource(sources[(size_t)idx]);
      }, kPreviewSize);

      PushTextWrapWidth(kPreviewSize);
      if (n->SourceName().empty())
         ImGui::TextDisabled("Pick a source");
      else if (n->IsReceiving())
         ImGui::TextColored(tok::V4(tok::palf::v_400_1000_500_1000), "Receiving: %dx%d", n->GetOutputWidth(), n->GetOutputHeight());
      else
         ImGui::TextColored(tok::V4(tok::palf::v_1000_600_200_1000), "Waiting for frames...");
      PopTextWrapWidth();
   }

   void DrawLSystemParams(LSystemNode* n)
   {
      ImGui::TextDisabled("%s", n->Status().c_str());
      char buf[512];
      snprintf(buf, sizeof(buf), "%s", n->axiom.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (FieldWell::InputText("axiom", buf, sizeof(buf)))
         n->axiom = buf;
      snprintf(buf, sizeof(buf), "%s", n->rules.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (FieldWell::InputText("rules", buf, sizeof(buf)))
         n->rules = buf;

      NodeSeparator("growth");
      ModSliderInt("generations", &n->iterations, 0, LSystemNode::kMaxIterations);
      ModSlider("angle", &n->angle, 0.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("length", &n->length, 0.005f, 1.0f);
      ModSlider("length / branch", &n->lengthScale, 0.1f, 1.5f);

      NodeSeparator("tube");
      ModSlider("radius", &n->radius, 0.001f, 0.2f);
      ModSlider("radius / branch", &n->radiusScale, 0.1f, 1.5f);
      ModSliderInt("sides", &n->sides, 3, 24);
      ModSlider("taper", &n->taper, 0.0f, 1.0f);

      if (!n->CookWarning().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextUnformatted(n->CookWarning().c_str());
         PopTextWrapWidth();
      }
   }

   void DrawRigidBodyParams(RigidBodyNode* n)
   {
      ImGui::TextDisabled("%zu bodies, %zu triangles", n->BodyCount(), n->TriangleCount());
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("bodies");
      DropdownButton("collider", RigidBodyNode::ColliderNames(), n->collider, [n](int i) { n->collider = i; });
      ModSlider("mass", &n->mass, 0.05f, 20.0f);
      ModSlider("friction", &n->friction, 0.0f, 1.0f);
      ModSlider("bounce", &n->bounce, 0.0f, 1.0f);
      ModSlider("linear damping", &n->linearDamping, 0.0f, 2.0f);
      ModSlider("angular damping", &n->angularDamping, 0.0f, 2.0f);
      ModSliderInt("iterations", &n->iterations, 1, 50);

      NodeSeparator("forces");
      ModSlider("gravity x", &n->gravityX, -30.0f, 30.0f);
      ModSlider("gravity y", &n->gravityY, -30.0f, 30.0f);
      ModSlider("gravity z", &n->gravityZ, -30.0f, 30.0f);

      bool rbSaved = false;
      const bool rbOpen = BeginAdvancedFold("showMoreRigid", 10, 0, rbSaved);   // start + kick sliders
      if (rbOpen)
         NodeSeparator("start");
      ModSlider("velocity x", &n->velX, -20.0f, 20.0f);
      ModSlider("velocity y", &n->velY, -20.0f, 20.0f);
      ModSlider("velocity z", &n->velZ, -20.0f, 20.0f);
      ModSlider("spin x", &n->spinX, -20.0f, 20.0f);
      ModSlider("spin y", &n->spinY, -20.0f, 20.0f);
      ModSlider("spin z", &n->spinZ, -20.0f, 20.0f);

      if (rbOpen)
         NodeSeparator("kick");
      ModSlider("kick time", &n->kickTime, 0.0f, 20.0f);
      ModSlider("kick x", &n->kickX, -20.0f, 20.0f);
      ModSlider("kick y", &n->kickY, -20.0f, 20.0f);
      ModSlider("kick z", &n->kickZ, -20.0f, 20.0f);
      gParamRegisterOnly = rbSaved;

      NodeSeparator("world");
      ModCheckbox("ground", &n->groundEnabled);
      if (n->groundEnabled)
         ModSlider("ground height", &n->groundHeight, -10.0f, 10.0f);
      ModCheckbox("walls", &n->wallsEnabled);
      if (n->wallsEnabled)
         ModSlider("wall distance", &n->wallSize, 0.5f, 20.0f);
   }

   void DrawProximityLinesParams(ProximityLinesNode* n)
   {
      ImGui::TextDisabled("%zu lines", n->LinkCount());
      ModSlider("radius", &n->radius, 0.0f, 5.0f, "%.3f");
      ModSliderInt("max per point", &n->maxPerPoint, 1, 16);
      ModSliderInt("max lines", &n->maxLinks, 1, 200000);
      ModSlider("thickness", &n->thickness, 0.0005f, 0.2f, "%.4f");
      ModCheckbox("alive only", &n->aliveOnly);
   }

   void DrawRevolveParams(RevolveNode* n)
   {
      DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; });
      ModSlider("angle", &n->angle, -360.0f, 360.0f, "%.0f");
      ModSliderInt("segments", &n->segments, 3, 256);
      ModCheckbox("caps", &n->caps);
   }

   void DrawSweepParams(SweepNode* n)
   {
      if (n->profile == nullptr)
      {
         ModSlider("profile radius", &n->profileRadius, 0.002f, 2.0f, "%.3f");
         ModSliderInt("profile sides", &n->profileSides, 3, 64);
      }
      ModSlider("start scale", &n->startScale, 0.0f, 4.0f);
      ModSlider("end scale", &n->endScale, 0.0f, 4.0f);
      ModSlider("twist", &n->twist, -1080.0f, 1080.0f, "%.0f");
      ModCheckbox("caps", &n->caps);
   }

   void DrawFMScope(FMNode* n, float h)
   {
      const double now = ImGui::GetTime();
      if (n->scopeCacheTime < 0.0 || now - n->scopeCacheTime > 1.0 / 30.0)
      {
         float buf[FMNode::kScopeCacheCapacity];
         const int count = n->ReadScope(buf, FMNode::kScopeCacheCapacity);
         if (count > 0)
         {
            std::copy(buf, buf + count, n->scopeCache);
            n->scopeCacheCount = count;
         }
         n->scopeCacheTime = now;
      }

      const float w = gAudioContentW;
      const AudioViz::Frame f = AudioViz::Begin(w, h);
      const float midY = f.origin.y + h * 0.5f;
      f.dl->AddLine(ImVec2(f.origin.x, midY), ImVec2(f.br.x, midY), ScopeMidLineCol(), 1.0f);
      if (n->scopeCacheCount > 1 && n->ActiveVoices() > 0)
      {
         const int count = n->scopeCacheCount;
         f.dl->PathClear();
         for (int i = 0; i < count; i++)
         {
            const float t = (float)i / (float)(count - 1);
            const float v = std::max(-1.0f, std::min(1.0f, n->scopeCache[i]));
            f.dl->PathLineTo(ImVec2(f.origin.x + t * w, midY - v * h * 0.44f));
         }
         f.dl->PathStroke(IsThemeLight() ? tok::U32(tok::pal::c_D2780AFF) : tok::U32(tok::pal::c_FFD778F0), 0, 1.6f);
      }
      else
      {
         AudioViz::IdleLabel(f, "play notes");
      }
      AudioViz::End(f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawFMBody(GraphNode& gn, FMNode* n)
   {
      const int voices = n->ActiveVoices();
      const FMDsp::Algorithm& alg = FMDsp::AlgorithmAt(n->algorithm);
      char stat[96];
      snprintf(stat, sizeof(stat), "%s  -  %d voice%s", alg.name, voices, voices == 1 ? "" : "s");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFMScope(n, 52.0f);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(3);
         row.Dropdown("algorithm", FMDsp::AlgorithmList(), n->algorithm, [n](int i) {
            PushUndoCheckpoint();
            n->algorithm = i;
         });
         row.Knob("feedback", &n->feedback, 0.0f, 1.0f, "%.2f");
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      // One section per operator; a carrier's level is its loudness, a
      // modulator's is how deep it bends what it feeds.
      static const char* kTitle[FMDsp::kNumOps][2] = {
         { "operator 1  -  carrier", "operator 1  -  modulator" },
         { "operator 2  -  carrier", "operator 2  -  modulator" },
         { "operator 3  -  carrier", "operator 3  -  modulator" },
         { "operator 4  -  carrier", "operator 4  -  modulator" },
      };
      for (int o = 0; o < FMDsp::kNumOps; o++)
      {
         FMNode::Operator& op = n->ops[o];
         const bool carrier = ((alg.carriers >> o) & 1) != 0;
         ImGui::Dummy(ImVec2(0.0f, 4.0f));
         ImGui::PushID(o);
         // Each operator's "fixed" checkbox is a discrete param: without a
         // scope all four hash to one pin id (PINDUPTEST).
         DiscreteSlotScope discreteScope(o);
         BeginAudioSection(kTitle[o][carrier ? 0 : 1]);
         {
            // Pitch row: ratio (or frequency when fixed), the fixed switch, then level, so the
            // envelope below is the same component, at the same size, as Wavetable's.
            const float full = gAudioContentW;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float checkW = 84.0f;
            const float sliderW = (full - checkW - gap * 2.0f) * 0.5f;
            const ImVec2 rowTop = ImGui::GetCursorScreenPos();
            const float levelX = rowTop.x + sliderW + gap + checkW + gap;
            if (op.fixed)
               AudioSlider("freq", &op.freq, 1.0f, 8000.0f, "%.0f Hz", sliderW, FrequencyTaper::PosToValue,
                           FrequencyTaper::ValueToPos);
            else
               AudioSlider("ratio", &op.ratio, 0.5f, 16.0f, "%.2f", sliderW);
            // Every cell is placed from the row's own origin, so each operator lines up exactly.
            ImGui::SetCursorScreenPos(ImVec2(rowTop.x + sliderW + gap, rowTop.y));
            ModCheckbox("fixed", &op.fixed);
            ImGui::SetCursorScreenPos(ImVec2(levelX, rowTop.y));
            AudioSlider("level", &op.level, 0.0f, 1.0f, "%.2f", rowTop.x + full - levelX);
            ImGui::SetCursorScreenPos(ImVec2(rowTop.x, rowTop.y + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y));
            DrawEnvelopeFields("##fmEnv", &op.attack, &op.decay, &op.sustain, &op.release, nullptr, 0.0f,
                               1.0f, "%.2f", tok::U32(tok::pal::c_96D6FFF5), nullptr);
            op.decay = std::max(1.0f, op.decay);
            op.release = std::max(1.0f, op.release);
         }
         EndAudioSection();
         ImGui::PopID();
      }

      EndAudioBody();
   }

   // The main sample's partial levels at its loudest frame, dB-scaled; the
   // bars past the partials knob are dimmed, since they are not heard.
   void DrawPartialsBars(PartialsNode* n, float h)
   {
      const float w = gAudioContentW;
      const AudioViz::Frame f = AudioViz::Begin(w, h);
      const int count = n->barCount;
      if (count > 0)
      {
         float peak = 1e-6f;
         for (int i = 0; i < count; i++)
            peak = std::max(peak, n->bars[i]);
         const bool light = IsThemeLight();
         const ImU32 on = light ? tok::U32(tok::pal::c_5A46C8C8) : tok::U32(tok::pal::c_B4A0FFC8);
         const ImU32 off = light ? tok::U32(tok::pal::c_FFFFFF8C) : tok::U32(tok::pal::c_00000082);
         const float barW = w / (float)PartialsNode::kBarCount;
         for (int i = 0; i < count; i++)
         {
            const float db = 20.0f * std::log10(std::max(1e-6f, n->bars[i] / peak));
            const float frac = std::clamp(1.0f + db / 60.0f, 0.0f, 1.0f);
            const float x = f.origin.x + barW * (float)i;
            const ImVec2 top(x, f.br.y - frac * h);
            const ImVec2 bot(x + std::max(1.0f, barW - 1.0f), f.br.y);
            f.dl->AddRectFilled(top, bot, on);
            if ((float)i >= n->partials)
               f.dl->AddRectFilled(top, bot, off);
         }
      }
      else
      {
         AudioViz::IdleLabel(f, n->IsAnalyzing() ? "analyzing..." : "drop a sample here");
      }
      AudioViz::End(f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawPartialsBody(GraphNode& gn, PartialsNode* n)
   {
      const int voices = n->ActiveVoices();
      char stat[160];
      if (n->HasSample(0))
         snprintf(stat, sizeof(stat), "%s  -  %d voice%s", n->Status().c_str(), voices, voices == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      // One strip: the two loaders and the loop switch, edge to edge like every row below it.
      {
         const float w = gAudioContentW;
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float loopW = 84.0f;
         const float btnW = (w - loopW - gap * 2.0f) * 0.5f;
         const float y = ImGui::GetCursorScreenPos().y;
         for (int which = 0; which < 2; which++)
         {
            ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + (float)which * (btnW + gap), y));
            ImGui::PushID(which);
            const std::string& name = n->FileName(which);
            const char* label = which == 0 ? "sample" : "morph to";
            char text[96];
            snprintf(text, sizeof(text), "%s: %s", label, name.empty() ? "load..." : name.c_str());
            if (ActionButton::Draw(text, ImVec2(btnW, 0)))
            {
               const std::string path = Platform::OpenAudioDialog();
               if (!path.empty())
               {
                  PushUndoCheckpoint();
                  n->LoadFile(which, path);
               }
            }
            ImGui::PopID();
         }
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + w - loopW, y));
         ModCheckbox("loop##partialsLoop", &n->loop);
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      DrawPartialsBars(n, 52.0f);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(4);
         row.Knob("pitch", &n->pitch, -24.0f, 24.0f, "%+.1f st");
         row.Knob("formant", &n->formant, -12.0f, 12.0f, "%+.1f st");
         row.Knob("partials", &n->partials, 1.0f, 48.0f, "%.0f");
         row.Knob("morph", &n->morph, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.Knob("speed", &n->speed, 0.1f, 4.0f, "%.2fx");
         row.Knob("noise", &n->noise, 0.0f, 2.0f, "%.2f");
         row.Knob("release", &n->release, 1.0f, 5000.0f, "%.0f ms", kKnobSmall, false, false,
                  AudioWidgetStyle::KnobSkewRelease400);
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      EndAudioBody();
   }

   // The string from nut (left) to bridge (right) with the exciter's point on it.
   void DrawStringDiagram(StringNode* n, float h)
   {
      const float w = gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      const float pad = 10.0f;
      const float midY = origin.y + h * 0.5f;
      const float x0 = origin.x + pad, x1 = br.x - pad;
      const ImU32 line = ScopeMidLineCol();
      const ImU32 trace = IsThemeLight() ? tok::U32(tok::pal::c_008CD2F0) : tok::U32(tok::pal::c_32F0FFF0);
      dl->AddLine(ImVec2(x0, origin.y + 4.0f), ImVec2(x0, br.y - 4.0f), line, 2.0f);
      dl->AddLine(ImVec2(x1, origin.y + 4.0f), ImVec2(x1, br.y - 4.0f), line, 2.0f);
      dl->AddLine(ImVec2(x0, midY), ImVec2(x1, midY), trace, 1.5f);
      const float px = x1 - std::clamp(n->position, 0.0f, 1.0f) * (x1 - x0);
      dl->AddCircleFilled(ImVec2(px, midY), 4.0f, trace);
      AudioViz::Border(dl, origin, br);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawStringBody(GraphNode& gn, StringNode* n)
   {
      const int voices = n->ActiveVoices();
      char stat[64];
      snprintf(stat, sizeof(stat), "%d voice%s", voices, voices == 1 ? "" : "s");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawStringDiagram(n, 28.0f);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(4);
         row.Dropdown("exciter##stringExciter", StringNode::ExciterNames(), n->exciter, [n](int i) {
            PushUndoCheckpoint();
            n->exciter = i;
         });
         row.Knob("position", &n->position, 0.02f, 0.5f, "%.2f");
         row.Knob("tone", &n->tone, 0.0f, 1.0f, "%.2f");
         row.Knob("stiffness", &n->stiffness, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.Knob("decay", &n->decay, 0.05f, 20.0f, "%.2f s");
         row.Knob("body", &n->body, 0.0f, 1.0f, "%.2f");
         row.Knob("release", &n->release, 1.0f, 5000.0f, "%.0f ms", kKnobSmall, false, false,
                  AudioWidgetStyle::KnobSkewRelease400);
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      EndAudioBody();
   }


   // ---- audio effects from upstream: Saturator, Shape Resonator, Convolution Reverb

   // ---- Saturator ----------------------------------------------------------
   // One transfer curve per stage, side by side, at the stage's amount - the
   // same main-thread-from-params rule as Drive's curve. A stage that is off
   // shows the dim 1:1 line alone.
   void DrawSaturatorVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      static const char* kModel[3] = { "model1", "model2", "model3" };
      static const char* kAmount[3] = { "amount1", "amount2", "amount3" };
      const float gap = 8.0f;
      const float panelW = (w - 4.0f * gap) / 3.0f;
      const float panelH = h - 2.0f * gap;
      const ImU32 curveCol = isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5);
      for (int s = 0; s < 3; s++)
      {
         const int model = std::clamp((int)(n->Param(kModel[s]) + 0.5f), 0, SaturatorDsp::kNumModels - 1);
         const float amount = n->Param(kAmount[s]);
         const ImVec2 p0(origin.x + gap + (float)s * (panelW + gap), origin.y + gap);
         const ImVec2 p1(p0.x + panelW, p0.y + panelH);
         dl->AddLine(ImVec2(0.5f * (p0.x + p1.x), p0.y), ImVec2(0.5f * (p0.x + p1.x), p1.y), ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(p0.x, 0.5f * (p0.y + p1.y)), ImVec2(p1.x, 0.5f * (p0.y + p1.y)), ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p0.y), ScopeMidLineCol(), 1.0f);
         if (model != SaturatorDsp::kOff)
         {
            dl->PathClear();
            const int kNumPoints = 64;
            for (int i = 0; i < kNumPoints; i++)
            {
               const float xIn = -1.0f + 2.0f * (float)i / (float)(kNumPoints - 1);
               const float yOut = std::clamp(SaturatorDsp::Transfer(model, amount, xIn), -1.0f, 1.0f);
               dl->PathLineTo(ImVec2(p0.x + (0.5f + 0.5f * xIn) * panelW, p0.y + (0.5f - 0.5f * yOut) * panelH));
            }
            dl->PathStroke(curveCol, 0, 1.6f);
         }
         char cap[24];
         snprintf(cap, sizeof(cap), "%d %s", s + 1, SaturatorDsp::ModelList()[(size_t)model].c_str());
         dl->AddText(ImVec2(p0.x + 3.0f, p0.y + 2.0f), ScopeTextCol(), cap);
      }

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         const int routing = std::clamp((int)(n->Param("routing") + 0.5f), 0, SaturatorDsp::kNumRoutings - 1);
         SetAudioReadout("routing", SaturatorDsp::RoutingList()[(size_t)routing].c_str());
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawShapeResonatorVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float loHz = 30.0f, hiHz = 12000.0f;
      const float logLo = log10f(loHz), logSpan = log10f(hiHz) - logLo;
      const ImU32 gridCol = isLight ? tok::U32(tok::pal::c_28283C1C) : tok::U32(tok::pal::c_FFFFFF16);
      for (float hz : { 100.0f, 1000.0f, 10000.0f })
      {
         const float x = origin.x + (log10f(hz) - logLo) / logSpan * w;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), gridCol, 1.0f);
      }

      float freqs[ShapeResonatorKernel::kMaxModes];
      bool solving = false;
      const int count = n->ReadModeFrequencies(freqs, ShapeResonatorKernel::kMaxModes, &solving);
      const float decay = std::clamp(n->Param("decay"), 0.02f, 20.0f);
      const float damping = std::clamp(n->Param("damping"), 0.0f, 1.0f);
      const float tune = std::max(n->Param("tune"), 20.0f);
      const ImU32 lineCol = isLight ? tok::U32(tok::pal::c_963CE6E6) : tok::U32(tok::pal::c_C882FFE6);
      for (int i = 0; i < count; i++)
      {
         // Line height = how long this mode rings relative to the longest.
         const float t60 = decay * powf(tune / freqs[i], damping * 1.5f);
         const float rel = std::clamp(log10f(t60 / 0.02f) / log10f(20.0f / 0.02f), 0.05f, 1.0f);
         const float x = origin.x + (log10f(freqs[i]) - logLo) / logSpan * w;
         dl->AddLine(ImVec2(x, br.y - 4.0f), ImVec2(x, br.y - 4.0f - rel * (h - 14.0f)), lineCol, 2.0f);
      }
      if (solving || count == 0)
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), isLight ? tok::U32(tok::pal::c_28283CA0) : tok::U32(tok::pal::c_FFFFFF96),
                     solving ? "solving shape..." : "no modes");

      AudioViz::End(vizFrame);
      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%d modes, line height = ring time", count);
         SetAudioReadout("shape modes", buf);
      }
      ImGui::Dummy(ImVec2(w, h));
   }

   // ---- Convolution Reverb -----------------------------------------------
   // The impulse response actually in use (designed or loaded), as its peak
   // level over time on a 0..-72 dB scale. Read from the kernel's main-thread
   // copy, so it is the built response, not a sketch of the settings.
   void DrawConvolutionSpaceVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      float env[ConvolutionKernel::kEnvelopePoints];
      float seconds = 0.0f;
      bool building = false;
      const int count = n->ReadImpulseEnvelope(env, ConvolutionKernel::kEnvelopePoints, &seconds, &building);

      const float kFloorDb = -72.0f;
      for (int i = 1; i < 4; i++)
      {
         const float y = origin.y + 3.0f + (float)i * 0.25f * (h - 6.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), ScopeGridCol(), 1.0f);
      }
      if (count > 1 && seconds > 0.0f)
      {
         const float tick = seconds > 4.0f ? 1.0f : seconds > 1.5f ? 0.5f : 0.1f;
         for (float t = tick; t < seconds; t += tick)
         {
            const float x = origin.x + t / seconds * w;
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
         }
         const ImU32 lineCol = isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5);
         dl->PathClear();
         for (int i = 0; i < count; i++)
         {
            const float level = std::clamp(1.0f - env[i] / kFloorDb, 0.0f, 1.0f);
            dl->PathLineTo(ImVec2(origin.x + ((float)i + 0.5f) / (float)count * w, br.y - 3.0f - level * (h - 6.0f)));
         }
         dl->PathStroke(lineCol, 0, 1.8f);
      }
      if (building || count == 0)
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), isLight ? tok::U32(tok::pal::c_28283CA0) : tok::U32(tok::pal::c_FFFFFF96),
                     "building space...");

      AudioViz::End(vizFrame);
      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.2f s, 0 to -72 dB", seconds);
         SetAudioReadout("impulse response", buf);
      }
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawSaturatorBody(GraphNode& gn, AudioEffectNode* n)
   {
      const int routing = std::clamp((int)(n->Param("routing") + 0.5f), 0, SaturatorDsp::kNumRoutings - 1);
      char stat[80];
      snprintf(stat, sizeof(stat), "%s - %.0f%% wet", SaturatorDsp::RoutingList()[(size_t)routing].c_str(),
               n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawSaturatorVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // One column per stage: its curve over its amount.
      static const char* kModel[3] = { "model1", "model2", "model3" };
      static const char* kAmount[3] = { "amount1", "amount2", "amount3" };
      static const char* kStageLabel[3] = { "stage 1", "stage 2", "stage 3" };
      static const char* kAmountLabel[3] = { "amount 1", "amount 2", "amount 3" };
      {
         AudioKnobRow row(3, 20.0f, 0.0f, false);
         for (int s = 0; s < 3; s++)
         {
            const char* key = kModel[s];
            const int model = std::clamp((int)(n->Param(key) + 0.5f), 0, SaturatorDsp::kNumModels - 1);
            row.Dropdown(kStageLabel[s], SaturatorDsp::ModelList(), model, [n, key](int i) {
               PushUndoCheckpoint();
               *n->ParamPtr(key) = (float)i;
            });
         }
         row.End();
      }
      {
         AudioKnobRow row(3);
         for (int s = 0; s < 3; s++)
         {
            const bool on = n->Param(kModel[s]) > 0.5f;
            if (!on)
               ImGui::BeginDisabled();
            row.Knob(kAmountLabel[s], n->ParamPtr(kAmount[s]), 0.0f, 1.0f, "%.2f", kKnobLarge);
            if (!on)
               ImGui::EndDisabled();
         }
         row.End();
      }

      BeginAudioSection("routing");
      {
         AudioKnobRow row(4);
         row.Dropdown("routing", SaturatorDsp::RoutingList(), routing, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("routing") = (float)i;
         });
         const bool fbLive = routing == SaturatorDsp::kFeedback;
         if (!fbLive)
            ImGui::BeginDisabled();
         row.Knob("feedback", n->ParamPtr("feedback"), 0.0f, 0.95f, "%.2f");
         if (!fbLive)
            ImGui::EndDisabled();
         row.Knob("output", n->ParamPtr("output"), -24.0f, 12.0f, "%.1f dB");
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }

   void DrawShapeResonatorBody(GraphNode& gn, AudioEffectNode* n)
   {
      float freqs[ShapeResonatorKernel::kMaxModes];
      bool solving = false;
      const int count = n->ReadModeFrequencies(freqs, ShapeResonatorKernel::kMaxModes, &solving);
      char stat[64];
      if (count > 0)
         snprintf(stat, sizeof(stat), "%d modes - %s", count, n->geometry ? "from shape" : "default plate");
      else if (n->geometry && !solving)
         snprintf(stat, sizeof(stat), "shape has no surface (needs a mesh)");
      else
         snprintf(stat, sizeof(stat), "%s", n->geometry ? "solving shape..." : "default plate");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawShapeResonatorVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3, kKnobLarge);
         row.Knob("tune", n->ParamPtr("tune"), 20.0f, 2000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("decay", n->ParamPtr("decay"), 0.02f, 20.0f, "%.2f s", kKnobLarge);
         row.Knob("damping", n->ParamPtr("damping"), 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("pos", n->ParamPtr("pos"), 0.0f, 1.0f, "%.2f");
         row.Knob("modes", n->ParamPtr("modes"), 1.0f, 32.0f, "%.0f");
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f");
         row.End();
      }

      EndAudioBody();
   }

   void DrawConvolutionReverbBody(GraphNode& gn, AudioEffectNode* n)
   {
      float env[ConvolutionKernel::kEnvelopePoints];
      float seconds = 0.0f;
      bool building = false, usingFile = false;
      const std::string* fileError = nullptr;
      n->ReadImpulseEnvelope(env, ConvolutionKernel::kEnvelopePoints, &seconds, &building, &usingFile, &fileError);
      const bool hasFile = !n->filePath.empty();
      const int shape = std::clamp((int)(n->Param("shape") + 0.5f), 0, ConvolutionKernel::kNumShapes - 1);

      char stat[96];
      if (hasFile && fileError && !fileError->empty())
         snprintf(stat, sizeof(stat), "file not loaded - designed %s", ConvolutionKernel::ShapeName(shape));
      else if (hasFile)
      {
         const size_t slash = n->filePath.find_last_of("/\\");
         snprintf(stat, sizeof(stat), "%s - %.1f s", n->filePath.substr(slash == std::string::npos ? 0 : slash + 1).c_str(), seconds);
      }
      else
         snprintf(stat, sizeof(stat), "designed %s - %.1f s", ConvolutionKernel::ShapeName(shape), seconds);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawConvolutionSpaceVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // A loaded file is used as it is: the design controls stay in place, greyed.
      const bool designOff = hasFile && !(fileError && !fileError->empty());
      {
         AudioKnobRow row(4, kKnobLarge);
         ImGui::BeginDisabled(designOff);
         row.Dropdown("shape", { "Room", "Hall", "Chamber", "Tunnel" }, shape, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("shape") = (float)i;
         });
         row.Knob("size", n->ParamPtr("size"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("decay", n->ParamPtr("decay"), 0.1f, ConvolutionKernel::kMaxSeconds, "%.2f s", kKnobLarge);
         row.Knob("material", n->ParamPtr("material"), 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         row.End();
      }
      {
         AudioKnobRow row(4);
         if (row.Button(hasFile ? "clear file" : "load file"))
         {
            if (hasFile)
            {
               PushUndoCheckpoint();
               n->filePath.clear();
            }
            else
            {
               const std::string path = Platform::OpenAudioDialog();
               if (!path.empty())
               {
                  PushUndoCheckpoint();
                  n->filePath = path;
               }
            }
         }
         ImGui::BeginDisabled(designOff);
         row.Knob("distance", n->ParamPtr("distance"), 0.0f, 1.0f, "%.2f");
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f");
         row.End();
      }

      EndAudioBody();
   }



   // ---- modulators from upstream: MSEG, Voice Spread, Note Expression

   // The MSEG's shape: drag a point, click empty space to add one, right-click
   // a point to remove it (empty space: reset). The small ring on each segment
   // bends it; drag it up or down. Loop on shades the looped part, and the
   // vertical line is where the shape is now.
   static void DrawMSEGEditor(MSEGNode* n, ImU32 lineCol)
   {
      const float width = kPreviewSize, height = 120.0f;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::PushID(n);
      ImGui::InvisibleButton("##mseg", ImVec2(width, height));
      ImGui::PopID();
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      const float pad = 6.0f;
      const float innerW = width - 2.0f * pad, innerH = height - 2.0f * pad;
      auto toScreen = [&](float x, float y) {
         return ImVec2(origin.x + pad + x * innerW, origin.y + pad + (1.0f - y) * innerH);
      };
      auto toShape = [&](ImVec2 p) {
         return ImVec2(std::clamp((p.x - origin.x - pad) / innerW, 0.0f, 1.0f),
                       std::clamp(1.0f - (p.y - origin.y - pad) / innerH, 0.0f, 1.0f));
      };
      std::vector<MSEGNode::Point>& pts = n->points;
      auto bendHandle = [&](int i) {
         const float xm = 0.5f * (pts[i].x + pts[i + 1].x);
         return toScreen(xm, MSEGNode::Segment(pts[i].y, pts[i + 1].y, pts[i].curve, 0.5f));
      };

      static MSEGNode* sDragNode = nullptr;
      static int sDragIndex = -1;
      static bool sDragBend = false;
      static float sDragStartY = 0.0f, sDragStartCurve = 0.0f;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      auto dist = [&](ImVec2 p) { return std::sqrt((p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y)); };
      int nearPoint = -1, nearBend = -1;
      float best = 10.0f;
      for (int i = 0; i < (int)pts.size(); i++)
         if (dist(toScreen(pts[i].x, pts[i].y)) < best)
         {
            best = dist(toScreen(pts[i].x, pts[i].y));
            nearPoint = i;
         }
      if (nearPoint < 0)
      {
         best = 8.0f;
         for (int i = 0; i + 1 < (int)pts.size(); i++)
            if (pts[i + 1].x - pts[i].x > 0.02f && dist(bendHandle(i)) < best)
            {
               best = dist(bendHandle(i));
               nearBend = i;
            }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         sDragNode = n;
         sDragBend = false;
         if (nearPoint >= 0)
            sDragIndex = nearPoint;
         else if (nearBend >= 0)
         {
            sDragIndex = nearBend;
            sDragBend = true;
            sDragStartY = mouse.y;
            sDragStartCurve = pts[nearBend].curve;
         }
         else
         {
            const ImVec2 c = toShape(mouse);
            sDragIndex = n->AddPoint(c.x, c.y);
         }
      }
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
         PushUndoCheckpoint();
         if (nearPoint >= 0)
            n->RemovePoint(nearPoint);
         else if (nearBend >= 0)
            pts[nearBend].curve = 0.0f;
         else
            n->Reset();
      }
      if (active && sDragNode == n && sDragIndex >= 0 && sDragIndex < (int)pts.size())
      {
         if (sDragBend && sDragIndex + 1 < (int)pts.size())
         {
            const float rising = pts[sDragIndex + 1].y >= pts[sDragIndex].y ? 1.0f : -1.0f;
            pts[sDragIndex].curve = std::clamp(sDragStartCurve + rising * (sDragStartY - mouse.y) / 50.0f, -1.0f, 1.0f);
         }
         else if (!sDragBend)
         {
            const ImVec2 c = toShape(mouse);
            n->MovePoint(sDragIndex, c.x, c.y);
         }
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragNode = nullptr;
         sDragIndex = -1;
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + width, origin.y + height);
      const bool isLight = IsThemeLight();
      const ImU32 ring = isLight ? tok::U32(tok::pal::c_F0F2F8FF) : tok::U32(tok::pal::c_12121AFF);
      AudioViz::Fill(dl, origin, br);
      for (int i = 1; i < 4; i++)
      {
         const float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + pad + innerW * f, origin.y), ImVec2(origin.x + pad + innerW * f, br.y), ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + pad + innerH * f), ImVec2(br.x, origin.y + pad + innerH * f), ScopeGridCol());
      }
      if (n->loop)
      {
         const float a = std::clamp(std::min(n->loopStart, n->loopEnd), 0.0f, 1.0f);
         const float b = std::clamp(std::max(n->loopStart, n->loopEnd), 0.0f, 1.0f);
         dl->AddRectFilled(ImVec2(toScreen(a, 0).x, origin.y), ImVec2(toScreen(b, 0).x, br.y), (lineCol & 0x00FFFFFF) | 0x1E000000);
         dl->AddLine(ImVec2(toScreen(a, 0).x, origin.y), ImVec2(toScreen(a, 0).x, br.y), ScopeMidLineCol());
         dl->AddLine(ImVec2(toScreen(b, 0).x, origin.y), ImVec2(toScreen(b, 0).x, br.y), ScopeMidLineCol());
      }
      const int kSteps = 96;
      for (int i = 1; i <= kSteps; i++)
      {
         const float x0 = (float)(i - 1) / kSteps, x1 = (float)i / kSteps;
         dl->AddLine(toScreen(x0, n->Evaluate(x0)), toScreen(x1, n->Evaluate(x1)), lineCol, 1.8f);
      }
      for (int i = 0; i + 1 < (int)pts.size(); i++)
         if (pts[i + 1].x - pts[i].x > 0.02f)
            dl->AddCircle(bendHandle(i), i == nearBend ? 4.5f : 3.0f, lineCol, 0, 1.2f);
      for (int i = 0; i < (int)pts.size(); i++)
      {
         const ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         dl->AddCircleFilled(sp, i == nearPoint ? 6.0f : 4.5f, lineCol);
         dl->AddCircle(sp, i == nearPoint ? 6.0f : 4.5f, ring, 0, 1.5f);
      }
      const float px = n->Position();
      const ImU32 liveCol = isLight ? tok::U32(tok::pal::c_DC8214FF) : tok::U32(tok::pal::c_FFC83CFF);
      dl->AddLine(ImVec2(toScreen(px, 0).x, origin.y), ImVec2(toScreen(px, 0).x, br.y), (liveCol & 0x00FFFFFF) | 0x80000000);
      dl->AddCircleFilled(toScreen(px, n->Evaluate(px)), 4.0f, liveCol);
      dl->AddCircle(toScreen(px, n->Evaluate(px)), 4.0f, ring, 0, 1.5f);
      AudioViz::Border(dl, origin, br);
   }

   void DrawMSEGParams(MSEGNode* n)
   {
      const bool isLight = IsThemeLight();
      DrawMSEGEditor(n, isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_E6EBFAFF));
      DrawModRateControls(&n->rateMode, &n->rateBeats, &n->rateHz, 3);
      PushCheckboxStyle();
      ModCheckbox("loop", &n->loop);
      PopCheckboxStyle();
      // Drawn either way so the two keep their slots; they do nothing with loop off.
      ImGui::BeginDisabled(!n->loop);
      ModSlider("loop start", &n->loopStart, 0.0f, 1.0f);
      ModSlider("loop end", &n->loopEnd, 0.0f, 1.0f);
      ImGui::EndDisabled();
      DrawPerVoiceCheckbox(&n->voice);
      DrawModRateHz(n->rateMode, &n->rateHz);
   }

   void DrawVoiceSpreadParams(VoiceSpreadNode* n)
   {
      DropdownButton("mode", VoiceSpreadNode::ModeNames(), n->mode,
                     [n](int i) { PushUndoCheckpoint(); n->mode = i; });
      ModSliderInt("count", &n->count, 1, ModShapes::kSpreadMaxCount);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }

   void DrawNoteExpressionParams(NoteExpressionNode* n)
   {
      DropdownButton("source", NoteExpressionNode::SourceNames(), n->source,
                     [n](int i) { PushUndoCheckpoint(); n->source = i; });
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }

   // ---- Prediction, from upstream

   void DrawPredictiveColoringParams(PredictiveColoringNode* n)
   {
      ImGui::PushID(n);
      const bool isLight = IsThemeLight();
      const bool learning = n->IsLearning();
      const float conf = n->Confidence01();
      const bool hasLearned = n->TotalSamples() > 0;

      char statusBuf[64];
      if (learning)
         snprintf(statusBuf, sizeof(statusBuf), "learning active footage...");
      else if (hasLearned)
         snprintf(statusBuf, sizeof(statusBuf), "target profile active");
      else
         snprintf(statusBuf, sizeof(statusBuf), "press Learn to profile footage");

      ImGui::TextDisabled("%s", statusBuf);

      const char* learnLabel = learning ? "Stop Learning" : (hasLearned ? "Learn Again" : "Learn");
      if (ActionButton::Draw(learnLabel, ImVec2(hasLearned && !learning ? 95 : 120, 0)))
      {
         PushUndoCheckpoint();
         n->SetLearning(!learning);
      }
      if (hasLearned && !learning)
      {
         ImGui::SameLine();
         if (ActionButton::Draw("Reset", ImVec2(50, 0)))
         {
            PushUndoCheckpoint();
            n->ResetProfile();
         }
      }
      // Confidence reads on its own status line under the buttons, not squeezed beside Learn.
      char confText[32];
      // While learning, show THIS take's own local progress - not the blended confidence, which
      // is mostly settled shared/house-style weight and barely moves as this instance learns.
      // Once stopped, show the blended confidence that actually governs the applied grade.
      const float badgeVal = learning ? n->LocalConfidence01() : conf;
      if (learning)
         snprintf(confText, sizeof(confText), "learning - %d%%", (int)std::round(badgeVal * 100.0f));
      else
         snprintf(confText, sizeof(confText), "%d%% conf", (int)std::round(badgeVal * 100.0f));
      ImGui::TextDisabled("%s", confText);

      PushCheckboxStyle();
      ModCheckbox("self normalize", &n->selfNormalize);
      PopCheckboxStyle();

      ModSlider("wander", &n->wander, 0.0f, 1.0f);
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
      ImGui::PopID();
   }


   // The params dispatch for the ported visual and 3D nodes (Nodes.inl calls
   // this once instead of growing its else-if chain).
   bool DrawPortedNodeParams(GraphNode& gn)
   {
      INode* node = gn.node.get();
      if (auto* n = dynamic_cast<OpticalFlowNode*>(node)) DrawOpticalFlowParams(n);
      else if (auto* n = dynamic_cast<TimeMachineNode*>(node)) DrawTimeMachineParams(n);
      else if (auto* n = dynamic_cast<FluidNode*>(node)) DrawFluidParams(n);
      else if (auto* n = dynamic_cast<VolumeNode*>(node)) DrawVolumeParams(n);
      else if (auto* n = dynamic_cast<NdiInNode*>(node)) DrawNdiInParams(n);
      else if (auto* n = dynamic_cast<NdiOutNode*>(node)) DrawNdiOutParams(n);
      else if (auto* n = dynamic_cast<RigidBodyNode*>(node)) DrawRigidBodyParams(n);
      else if (auto* n = dynamic_cast<LSystemNode*>(node)) DrawLSystemParams(n);
      else if (auto* n = dynamic_cast<ProximityLinesNode*>(node)) DrawProximityLinesParams(n);
      else if (auto* n = dynamic_cast<SweepNode*>(node)) DrawSweepParams(n);
      else if (auto* n = dynamic_cast<RevolveNode*>(node)) DrawRevolveParams(n);
      else if (auto* n = dynamic_cast<MSEGNode*>(node)) DrawMSEGParams(n);
      else if (auto* n = dynamic_cast<VoiceSpreadNode*>(node)) DrawVoiceSpreadParams(n);
      else if (auto* n = dynamic_cast<NoteExpressionNode*>(node)) DrawNoteExpressionParams(n);
      else if (auto* n = dynamic_cast<PredictiveColoringNode*>(node)) DrawPredictiveColoringParams(n);
      else
         return false;
      return true;
   }
