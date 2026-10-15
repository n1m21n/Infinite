// Part of src/main.cpp, included there in order (one translation unit).
// colour tags, MIDI mapping and modulation range editors

   const char* NodeTagName(int tag)
   {
      static const char* kNames[kNodeTagCount] = { "none", "red", "orange", "yellow", "green",
                                                  "cyan", "blue", "purple", "pink" };
      return kNames[std::clamp(tag, 0, kNodeTagCount - 1)];
   }
   ImU32 NodeTagColor(int tag)
   {
      static const ImU32 kColors[kNodeTagCount] = {
         IM_COL32(0, 0, 0, 0), IM_COL32(239, 68, 68, 255), IM_COL32(249, 140, 40, 255),
         IM_COL32(245, 205, 50, 255), IM_COL32(60, 200, 100, 255), IM_COL32(40, 200, 220, 255),
         IM_COL32(70, 130, 245, 255), IM_COL32(160, 100, 240, 255), IM_COL32(240, 100, 180, 255) };
      return kColors[std::clamp(tag, 0, kNodeTagCount - 1)];
   }
   // Applies to the right-clicked node and, when it is part of the selection,
   // to every selected node.
   void SetNodeColorTag(GraphNode& target, int tag)
   {
      PushUndoCheckpoint();
      target.colorTag = tag;
      if (gEditor != nullptr && ed::IsNodeSelected(target.NodeId()))
      {
         const int count = ed::GetSelectedObjectCount();
         std::vector<ed::NodeId> sel(std::max(1, count));
         const int n = ed::GetSelectedNodes(sel.data(), count);
         for (int i = 0; i < n; i++)
            if (GraphNode* g = FindNodeByIndex((int)sel[i].Get() / GraphNode::kStride))
               g->colorTag = tag;
      }
      gPatchDirty = true;
   }

   const ParamRef* FindFrameParam(int nodeIndex, int paramIndex)
   {
      for (const ParamRef& ref : Modulation::Instance().FrameParams())
         if (ref.nodeIndex == nodeIndex && ref.paramIndex == paramIndex)
            return &ref;
      return nullptr;
   }

   // Turbo MIDI learn: edits one mapping (source + response + range).
   void DrawMidiMappingEditor(MidiMap::Mapping& m)
   {
      bool changed = false;
      const std::vector<std::string> devices = MidiMap::DeviceKeys();
      const std::string devLabel = m.deviceKey.empty() ? std::string("any device") : m.deviceKey;
      const bool connected = m.deviceKey.empty() ||
                             std::find(devices.begin(), devices.end(), m.deviceKey) != devices.end();
      ImGui::SetNextItemWidth(230.0f);
      if (ImGui::BeginCombo("device##midiDev", connected ? devLabel.c_str() : (devLabel + " (not connected)").c_str()))
      {
         if (ImGui::Selectable("any device", m.deviceKey.empty()))
         {
            m.deviceKey.clear();
            changed = true;
         }
         for (const std::string& d : devices)
            if (ImGui::Selectable(d.c_str(), d == m.deviceKey))
            {
               m.deviceKey = d;
               changed = true;
            }
         ImGui::EndCombo();
      }
      char chLabel[16];
      if (m.channel < 0)
         snprintf(chLabel, sizeof(chLabel), "any");
      else
         snprintf(chLabel, sizeof(chLabel), "%d", m.channel + 1);
      ImGui::SetNextItemWidth(80.0f);
      if (ImGui::BeginCombo("channel##midiCh", chLabel))
      {
         if (ImGui::Selectable("any", m.channel < 0))
         {
            m.channel = -1;
            changed = true;
         }
         for (int ch = 0; ch < 16; ch++)
         {
            char item[8];
            snprintf(item, sizeof(item), "%d", ch + 1);
            if (ImGui::Selectable(item, m.channel == ch))
            {
               m.channel = ch;
               changed = true;
            }
         }
         ImGui::EndCombo();
      }
      ImGui::SameLine();
      int type = m.isNote ? 1 : 0;
      if (ImGui::RadioButton("CC", type == 0)) { m.isNote = false; changed = true; }
      ImGui::SameLine();
      if (ImGui::RadioButton("note", type == 1)) { m.isNote = true; changed = true; }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70.0f);
      if (ImGui::DragInt("##midiNum", &m.number, 0.2f, 0, 127, m.isNote ? "note %d" : "CC %d", ImGuiSliderFlags_AlwaysClamp))
         changed = true;

      static const char* kModes[3] = { "continuous (knob / fader)", "momentary (hold)", "toggle (press on / off)" };
      ImGui::SetNextItemWidth(230.0f);
      if (ImGui::BeginCombo("mode##midiMode", kModes[std::clamp(m.mode, 0, 2)]))
      {
         for (int i = 0; i < 3; i++)
            if (ImGui::Selectable(kModes[i], m.mode == i))
            {
               m.mode = i;
               changed = true;
            }
         ImGui::EndCombo();
      }
      if (ImGui::Checkbox("invert##midiInv", &m.invert))
         changed = true;
      ImGui::SameLine();
      if (ImGui::Checkbox("soft takeover##midiSoft", &m.softTakeover))
      {
         m.pickedUp = !m.softTakeover;
         changed = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("the parameter only follows the control once the control reaches its current value (no jumps)");
      if (m.mode == MidiMap::kContinuous)
      {
         ImGui::SetNextItemWidth(113.0f);
         if (ImGui::DragFloat("##midiSmooth", &m.smoothMs, 0.5f, 0.0f, 500.0f, "smooth %.0f ms", ImGuiSliderFlags_AlwaysClamp))
            changed = true;
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("glides to each new MIDI value; 0 = jump straight to it");
      }

      // Range in the parameter's own units when it is on screen (along the
      // widget's taper, so a dB fader's min/max read in dB).
      const ParamRef* ref = FindFrameParam(m.nodeIndex, m.paramIndex);
      const float lo = ref ? ref->minValue : 0.0f;
      const float hi = ref ? ref->maxValue : 1.0f;
      const int taper = ref ? ref->taper : 0;
      const float span = hi - lo;
      float outLo = MidiMap::PosToValue(taper, m.outMin, lo, hi), outHi = MidiMap::PosToValue(taper, m.outMax, lo, hi);
      const float speed = std::max(0.0001f, std::fabs(span) * 0.002f);
      ImGui::SetNextItemWidth(113.0f);
      if (ImGui::DragFloat("##midiMin", &outLo, speed, std::min(lo, hi), std::max(lo, hi), "min %.3f"))
         changed = true;
      ImGui::SameLine();
      ImGui::SetNextItemWidth(113.0f);
      if (ImGui::DragFloat("##midiMax", &outHi, speed, std::min(lo, hi), std::max(lo, hi), "max %.3f"))
         changed = true;
      ImGui::SameLine();
      if (ImGui::SmallButton("full"))
      {
         outLo = lo;
         outHi = hi;
         changed = true;
      }
      if (changed)
      {
         if (std::fabs(span) > 1e-9f)
         {
            m.outMin = MidiMap::ValueToPos(taper, outLo, lo, hi);
            m.outMax = MidiMap::ValueToPos(taper, outHi, lo, hi);
         }
         m.lastRaw = -1.0f;
         gPatchDirty = true;
      }
   }

   void DrawModulationRangeEditor(int nodeIndex, int paramIndex)
   {
      Modulation& mod = Modulation::Instance();
      const Modulation::Source src = mod.ModulatorFor(nodeIndex, paramIndex);
      if (src.nodeIndex < 0)
         return;
      float lo = 0.0f, hi = 1.0f;
      for (const ParamRef& ref : mod.FrameParams())
         if (ref.nodeIndex == nodeIndex && ref.paramIndex == paramIndex)
         {
            lo = ref.minValue;
            hi = ref.maxValue;
            break;
         }
      float incoming = 0.0f;
      if (GraphNode* g = FindNodeByIndex(src.nodeIndex))
         if (IModulator* m = ModulatorForOutput(g->node.get(), src.outputIndex))
            incoming = m->Value01();

      ImGui::SeparatorText("Range");
      ImGui::TextDisabled("incoming %.3f", incoming);
      float inMin = src.inMin, inMax = src.inMax;
      ImGui::SetNextItemWidth(220.0f);
      bool changed = ImGui::DragFloatRange2("in##modIn", &inMin, &inMax, 0.002f, 0.0f, 1.0f,
                                            "in min %.3f", "in max %.3f", ImGuiSliderFlags_AlwaysClamp);
      // Output edited in the parameter's own units.
      const float span = hi - lo;
      float outLo = lo + span * src.outMin, outHi = lo + span * src.outMax;
      const float speed = std::max(0.0001f, std::fabs(span) * 0.002f);
      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat("out min##modOutLo", &outLo, speed, lo, hi, "out min %.3f"))
         changed = true;
      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat("out max##modOutHi", &outHi, speed, lo, hi, "out max %.3f"))
         changed = true;
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();
      if (ImGui::Button("capture in min"))
      {
         inMin = incoming;
         changed = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("capture in max"))
      {
         inMax = incoming;
         changed = true;
      }
      if (ImGui::Button("invert"))
      {
         std::swap(outLo, outHi);
         changed = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("reset range"))
      {
         PushUndoCheckpoint();
         mod.SetRange(nodeIndex, paramIndex, 0.0f, 1.0f, 0.0f, 1.0f);
         return;
      }
      if (changed)
      {
         const float outMin = std::fabs(span) > 1e-9f ? (outLo - lo) / span : 0.0f;
         const float outMax = std::fabs(span) > 1e-9f ? (outHi - lo) / span : 1.0f;
         mod.SetRange(nodeIndex, paramIndex, std::clamp(inMin, 0.0f, 1.0f), std::clamp(inMax, 0.0f, 1.0f),
                      outMin, outMax);
         gPatchDirty = true;
      }
   }

   void DrawModulatorMeter(IModulator* mod, int nodeIndex)
   {
      const float value = mod->Value01();
      std::vector<float>& history = gModHistory[nodeIndex];
      history.push_back(value);
      if (history.size() > 160)
         history.erase(history.begin());

      // Autoscale to the window's actual range rather than assuming 0..1: an
      // unclamped Math node can sit at 60 or -12, and a fixed 0..1 mapping
      // either pins the line off the box (if clipped) or off the visible edge
      // (if just clamped) - neither shows the wiggle the meter exists to show.
      float lo = history.empty() ? 0.0f : history[0];
      float hi = lo;
      for (float v : history) { lo = std::min(lo, v); hi = std::max(hi, v); }
      const float range = std::max(1e-4f, hi - lo); // epsilon floor: flat signal shouldn't divide by ~0

      ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 90.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                        IM_COL32(18, 18, 24, 255), 4.0f);

      dl->PushClipRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h), true); // backstop, not the primary fix
      for (size_t i = 1; i < history.size(); i++)
      {
         float x0 = origin.x + kPreviewSize * (float)(i - 1) / 160.0f;
         float x1 = origin.x + kPreviewSize * (float)i / 160.0f;
         float y0 = origin.y + h - (history[i - 1] - lo) / range * h;
         float y1 = origin.y + h - (history[i] - lo) / range * h;
         dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 190, 90, 255), 1.6f);
      }
      dl->PopClipRect();

      // Value01() is contractually 0..1, but InvertNode and RangeToRangeNode
      // (clampOutput=false) deliberately go outside it so a kAbsolute binding
      // can extrapolate past the destination's nominal range instead of
      // being flattened at the edge - see 00-modulation-polarity.md. The
      // autoscale above makes that look identical to a well-behaved source,
      // so flag it: an amber border, plus hairlines marking where 0 and 1
      // actually fall on the autoscaled box.
      const bool outOfContract = lo < -1e-4f || hi > 1.0f + 1e-4f;
      if (outOfContract)
      {
         const float y0line = origin.y + h - (0.0f - lo) / range * h;
         const float y1line = origin.y + h - (1.0f - lo) / range * h;
         const ImU32 hairlineCol = IM_COL32(255, 255, 255, 90);
         if (y0line >= origin.y && y0line <= origin.y + h)
            dl->AddLine(ImVec2(origin.x, y0line), ImVec2(origin.x + kPreviewSize, y0line), hairlineCol, 1.0f);
         if (y1line >= origin.y && y1line <= origin.y + h)
            dl->AddLine(ImVec2(origin.x, y1line), ImVec2(origin.x + kPreviewSize, y1line), hairlineCol, 1.0f);
      }
      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                  outOfContract ? IM_COL32(255, 190, 90, 255) : IM_COL32(70, 74, 90, 255), 4.0f);
      ImGui::Dummy(ImVec2(kPreviewSize, h));
      ImGui::Text("%.3f", value);
   }

   void DisconnectLinkById(int id)
   {
      const LinkInfo* link = FindLink(id);
      if (link == nullptr)
         return;

      const int dstPin = link->dstPin;
      GraphNode* dst = FindNodeByIndex(GraphNode::NodeIndexFromPin(dstPin));
      if (dst == nullptr)
         return;

      if (GraphNode::IsParamPin(dstPin))
      {
         Modulation::Instance().Unbind(dst->index, GraphNode::ParamIndexFromPin(dstPin));
      }
      else if (GraphNode::IsColorPin(dstPin))
      {
         PaletteBinding::Instance().Unbind(dst->index, GraphNode::ColorIndexFromPin(dstPin));
      }
      else if (auto* render = dynamic_cast<Render3DNode*>(dst->node.get()))
      {
         const int slot = GraphNode::InputSlotFromPin(dstPin);
         if (slot >= 0 && slot < Render3DNode::kSlots)
            render->geometry[slot] = nullptr;
         else if (slot == Render3DNode::kSlots)
            render->camera = nullptr;
         else if (slot == Render3DNode::kEnvSlot)
            render->envInput.Disconnect();
         else if (slot > Render3DNode::kSlots && slot < Render3DNode::kEnvSlot)
            render->lights[slot - Render3DNode::kSlots - 1] = nullptr;
      }
      else if (auto* geoOp = dynamic_cast<GeometryOpNode*>(dst->node.get()))
      {
         geoOp->input = nullptr;
      }
      else if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(dst->node.get()))
      {
         if (GraphNode::InputSlotFromPin(dstPin) == 0)
            inst->pointSource = nullptr;
         else
            inst->instanceShape = nullptr;
      }
      else if (auto* sw3 = dynamic_cast<Switcher3DNode*>(dst->node.get()))
      {
         const int slot = GraphNode::InputSlotFromPin(dstPin);
         if (slot >= 0 && slot < Switcher3DNode::kSlots)
            sw3->inputs[slot] = nullptr;
      }
      else if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(dst->node.get()))
      {
         audio->fileSource = nullptr;
         audio->audioInput.Disconnect();
      }
      else if (IModulator** slotField = dst->node->ModulatorInputSlot(GraphNode::InputSlotFromPin(dstPin)))
      {
         *slotField = nullptr;
      }
      else if (auto* setColor = dynamic_cast<SetColorNode*>(dst->node.get());
               setColor != nullptr && GraphNode::InputSlotFromPin(dstPin) == 2)
      {
         setColor->paletteInput = nullptr;
      }
      else if (AudioCable* audioCable = dst->node->AudioInputSlot(GraphNode::InputSlotFromPin(dstPin)))
      {
         audioCable->Disconnect();
      }
      else if (NoteCable* noteCable = dst->node->NoteInputSlot(GraphNode::InputSlotFromPin(dstPin)))
      {
         noteCable->Disconnect();
      }
      else if (ImageCable* cable = CableFor(*dst, GraphNode::InputSlotFromPin(dstPin)))
      {
         cable->Disconnect();
      }
      RebuildAudioTopology();
   }

   // Per-node "Help" popup text, keyed by the exact registered typeName (the
   // same string GraphNode::typeName holds - see REGISTER_NODE / FilterDef::name).
   // This is where usage notes, wiring conventions and gotchas live now, rather
   // than as permanent text drawn on the node body: a node you already know how
   // to use shouldn't have to carry its own manual around forever.
