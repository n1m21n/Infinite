// Param widget plumbing, audio sliders, taper maths, dropdown button, checkbox/slider styles (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   void BeginNodeParams(int nodeIndex)
   {
      gCurrentNodeIndex = nodeIndex;
      gParamCounter = 0;
      gColorCounter = 0;
      gDiscreteParamCounter = kDiscreteParamBase;
   }


   // Node drawing is over: anything that draws afterwards (right/bottom-docked
   // panels, settings dialogs, the node browser) is not a node's param block.
   // Without this, gCurrentNodeIndex kept pointing at whichever node happened
   // to draw last, and every checkbox and dropdown in those panels would
   // register itself as a param of that node.
   void EndNodeParams()
   {
      gCurrentNodeIndex = -1;
      gParamCounter = 0;
      gColorCounter = 0;
      gDiscreteParamCounter = kDiscreteParamBase;
   }


   // Help tooltips (what a control does, its shortcut) are opt-in in Settings. Diagnostics - errors,
   // rejection reasons, live value readouts - use ImGui::SetTooltip directly and always show.
   void HelpTip(const char* fmt, ...)
   {
      if (!CategoryColors::GetTooltips())
         return;
      // Inside a node the editor's canvas transform is live and would offset the tooltip from the cursor.
      if (gInsideNodeCanvas)
         ed::Suspend();
      va_list args;
      va_start(args, fmt);
      ImGui::SetTooltipV(fmt, args);
      va_end(args);
      if (gInsideNodeCanvas)
         ed::Resume();
   }


   // io.WantTextInput is computed at the end of the previous frame, so on the frame a text field takes
   // focus it is still false and hover-to-type would eat the first keystroke for a knob under the
   // pointer. This also reads the live ImGui state (an InputText or temp-input that already owns the
   // active id) and the app's own text owners, so every hover-to-type site shares one answer.
   bool TextFocusClaimed()
   {
      ImGuiContext& g = *ImGui::GetCurrentContext();
      if (g.IO.WantTextInput || !gTypedParam.empty() || gCommentEdit.target != nullptr || gNavOwnsKeys)
         return true;
      if (g.ActiveId != 0 && (g.InputTextState.ID == g.ActiveId || g.TempInputId == g.ActiveId))
         return true;
      return false;
   }


   // Opens the text field for a param, seeded either from its current numeric
   // value or (if it's already driven by an expression) from that expression
   // text with its '=' prefix - used by both double-click and right-click.
   void BeginTypedEditFromCurrent(const std::pair<int, int>& editKey, int nodeIndex, int paramIndex,
                                   float* value, const char* fmt, bool hasExpr)
   {
      if (hasExpr)
      {
         const std::string* expr = Modulation::Instance().ExpressionFor(nodeIndex, paramIndex);
         gTypedParamText[editKey] = std::string("=") + (expr != nullptr ? *expr : std::string());
      }
      else
      {
         char seed[64];
         snprintf(seed, sizeof(seed), fmt, *value);
         gTypedParamText[editKey] = seed;
         gTypedParamSeed[editKey] = seed;
      }
      gTypedParam.insert(editKey);
      gTypedParamJustOpened = editKey;
      gTypedParamPendingInit.insert(editKey);
   }


   // Hovering a param (whether it's a plain slider or already showing an
   // expression) and pressing a digit/'-'/'.'/'=' jumps straight into typing
   // mode with that keystroke seeded, without needing to double-click first.
   // '=' always starts a blank formula - even over an existing one, so typing
   // '=' is a reliable way back into formula mode - and a digit always starts
   // a blank numeric entry, overriding whatever mode the param was already in.
   void HandleParamTypeHotkeys(const std::pair<int, int>& editKey, float* value)
   {
      ImGuiIO& io = ImGui::GetIO();
      // A typed-entry field is already open (double-click) - every digit
      // belongs to it. Without this, a mouse resting over a different param
      // treated the same keystrokes as its own hover-to-type, stole the
      // focus, and the field the user actually double-clicked closed empty.
      if (TextFocusClaimed())
         return;
      for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
      {
         ImWchar ch = io.InputQueueCharacters[i];
         if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '=')
         {
            PushUndoCheckpoint();
            if (ch == '=')
               gTypedParamText[editKey] = "=";
            else if (ch >= '0' && ch <= '9')
            {
               *value = (float)(ch - '0');
               gTypedParamText[editKey] = std::string(1, (char)ch);
            }
            else if (ch == '-')
            {
               *value = -0.0f;
               gTypedParamText[editKey] = "-";
            }
            else
            {
               *value = 0.0f;
               gTypedParamText[editKey] = ".";
            }
            gTypedParam.insert(editKey);
            gTypedParamJustOpened = editKey;
            gTypedParamNoAutoSelect.insert(editKey);
            gTypedParamPendingInit.insert(editKey);
            break;
         }
      }
   }


   // Keyboard param focus (Tab). Called once at the end of ModSlider/ModKnob
   // (ints reach them through float slots, with step 1): records the param in
   // draw order for Tab, and for the focused one draws the ring, applies a
   // queued Left/Right nudge through the same min/max the widget clamps to,
   // and lets digits start typed entry as if it were hovered. Returns true
   // when the value changed so the caller reports it like a mouse edit.
   bool KbParamHook(int nodeIndex, int paramIndex, float* value, float minV, float maxV, float step,
                    const char* fmt, ImVec2 rmin, ImVec2 rmax, bool circle)
   {
      bool seen = false;
      for (const KbParamEntry& e : gKbParams)
         seen = seen || (e.node == nodeIndex && e.param == paramIndex);
      if (!seen)
         gKbParams.push_back({ nodeIndex, paramIndex });
      if (CategoryColors::GetTooltips() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
      {
         char lo[48], hi[48];
         snprintf(lo, sizeof(lo), fmt, minV);
         snprintf(hi, sizeof(hi), fmt, maxV);
         HelpTip(T("Range %s to %s\nDouble-click, or hover and type, to enter a value"), lo, hi);
      }
      if (nodeIndex != gKbFocusNode || paramIndex != gKbFocusParam)
         return false;

      // The ring hugs the control itself: the slider's own box, the knob's
      // circle, the fader's track. Never the pin dot or the caption.
      ImDrawList* kdl = ImGui::GetWindowDrawList();
      const ImU32 ringCol = ImGui::GetColorU32(ImGuiCol_NavHighlight);
      if (circle)
         kdl->AddCircle(ImVec2((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f), (rmax.x - rmin.x) * 0.5f + 1.0f,
                        ringCol, 48, 2.0f);
      else
         kdl->AddRect(ImVec2(rmin.x - 2.0f, rmin.y - 2.0f), ImVec2(rmax.x + 2.0f, rmax.y + 2.0f), ringCol, 4.0f, 0, 2.0f);
      bool changed = false;
      if (gKbNudge != 0 && !Modulation::Instance().IsModulated(nodeIndex, paramIndex))
      {
         float st = step;
         if (st <= 0.0f)
            st = (fmt != nullptr && strcmp(fmt, "%.0f") == 0) ? 1.0f : (maxV - minV) / 100.0f;
         const float before = *value;
         *value = std::clamp(*value + st * (float)gKbNudge, minV, maxV);
         changed = (*value != before);
      }
      gKbNudge = 0;
      HandleParamTypeHotkeys(std::make_pair(nodeIndex, paramIndex), value);
      return changed;
   }


   void SetAudioReadout(const char* label, const char* valueText)
   {
      if (gCurrentNodeIndex < 0)
         return;
      const char* name = label;
      if (name != nullptr && name[0] == '#')
         name = "";
      char buf[96];
      snprintf(buf, sizeof(buf), "%s   %s", name != nullptr ? name : "", valueText);
      gAudioReadout[gCurrentNodeIndex] = buf;
   }


   // Adaptive format helper: scales display precision at small values so
   // tapered parameters do not display a static "0 ms" or dead string across the bottom sweep.
   void FormatAudioParam(char* outBuf, size_t outSize, const char* fmt, float val)
   {
      if (!outBuf || outSize == 0)
         return;
      if (!fmt || fmt[0] == '\0')
      {
         outBuf[0] = '\0';
         return;
      }

      if (strstr(fmt, "ms") != nullptr)
      {
         const float absV = std::fabs(val);
         if (absV < 10.0f && (strstr(fmt, "%.0f") || strstr(fmt, "%.1f")))
         {
            snprintf(outBuf, outSize, "%.2f ms", val);
            return;
         }
         if (absV < 100.0f && strstr(fmt, "%.0f"))
         {
            snprintf(outBuf, outSize, "%.1f ms", val);
            return;
         }
      }
      else if (strstr(fmt, "Hz") != nullptr)
      {
         const float absV = std::fabs(val);
         if (absV < 10.0f && (strstr(fmt, "%.0f") || strstr(fmt, "%.1f")))
         {
            snprintf(outBuf, outSize, "%.2f Hz", val);
            return;
         }
         if (absV < 100.0f && strstr(fmt, "%.0f"))
         {
            snprintf(outBuf, outSize, "%.1f Hz", val);
            return;
         }
      }
      else if (strstr(fmt, "s") != nullptr && strstr(fmt, "st") == nullptr && strstr(fmt, "step") == nullptr && strstr(fmt, "semi") == nullptr)
      {
         const float absV = std::fabs(val);
         if (absV < 0.1f && (strstr(fmt, "%.2f") || strstr(fmt, "%.1f") || strstr(fmt, "%.0f")))
         {
            snprintf(outBuf, outSize, "%.3f s", val);
            return;
         }
         if (absV < 1.0f && (strstr(fmt, "%.1f") || strstr(fmt, "%.0f")))
         {
            snprintf(outBuf, outSize, "%.2f s", val);
            return;
         }
      }

      snprintf(outBuf, outSize, fmt, val);
   }


   // ---- horizontal audio slider -------------------------------------------
   // Label left, value right, both *inside* the track; a low-alpha fill from
   // the left edge to the current value instead of a grab handle. This is
   // the layout every plugin uses for a horizontal parameter, and it
   // replaces v2's ImGui-default look (label stranded in a ragged column to
   // the right of the widget, value floating mid-track, a lone purple grab
   // block) for audio nodes only - visual nodes keep plain ModSlider.
   //
   // Interaction is still ImGui::SliderFloat: the grab is styled fully
   // transparent and the format string emptied, so the widget behaves
   // exactly as it always has (drag, clamp, Ctrl+click, the
   // IsItemActivated/IsItemHovered surface ModSlider's surrounding logic
   // depends on) and only the pixels are ours.
   // The one text size of an audio node body's labels: the slider name and value
   // ("fine tune", "0 c") and any label drawn inside a widget (the MPC pads' number, mode
   // tag and file name) use it, so they cannot drift apart.
   float AudioLabelFontSize() { return ImGui::GetFontSize(); }

   void AudioLabelText(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text)
   {
      dl->AddText(ImGui::GetFont(), AudioLabelFontSize(), pos, col, text);
   }

   ImVec2 AudioLabelSize(const char* text)
   {
      return ImGui::GetFont()->CalcTextSizeA(AudioLabelFontSize(), FLT_MAX, 0.0f, text);
   }


   bool AudioSliderFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                         float width, ImU32 fillColor, bool readOnly,
                         FaderPosToValueFn posToValue, FaderValueToPosFn valueToPos,
                         bool vividState)
   {
      const bool isLight = IsThemeLight();
      if (isLight)
      {
         ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_880_890_920_1000));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tok::V4(tok::palf::v_830_850_890_1000));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tok::V4(tok::palf::v_780_810_860_1000));
      }
      else
      {
         ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_55_60_80_1000));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tok::V4(tok::palf::v_85_92_118_1000));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tok::V4(tok::palf::v_100_108_138_1000));
      }
      ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_0_0_0_0));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, tok::V4(tok::palf::v_0_0_0_0));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

      auto ValueToPos01 = [&](float v) -> float
      {
         return valueToPos ? std::clamp(valueToPos(v, minV, maxV), 0.0f, 1.0f)
                           : ((maxV > minV) ? std::clamp((v - minV) / (maxV - minV), 0.0f, 1.0f) : 0.0f);
      };
      auto Pos01ToValue = [&](float pos) -> float
      {
         return posToValue ? std::clamp(posToValue(pos, minV, maxV), minV, maxV)
                           : std::clamp(minV + (maxV - minV) * pos, minV, maxV);
      };

      std::string id = std::string("##as_") + label;
      ImGui::SetNextItemWidth(width);
      bool changed = false;
      float pos01 = ValueToPos01(*value);
      if (posToValue != nullptr)
      {
         if (ImGui::SliderFloat(id.c_str(), &pos01, 0.0f, 1.0f, "",
                                readOnly ? ImGuiSliderFlags_NoInput : ImGuiSliderFlags_None))
         {
            const float next = Pos01ToValue(pos01);
            if (next != *value)
            {
               *value = next;
               changed = true;
            }
         }
      }
      else
      {
         changed = ImGui::SliderFloat(id.c_str(), value, minV, maxV, "",
                                      readOnly ? ImGuiSliderFlags_NoInput : ImGuiSliderFlags_None);
         pos01 = ValueToPos01(*value);
      }
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(5);

      const ImVec2 r0 = ImGui::GetItemRectMin();
      const ImVec2 r1 = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float t = pos01;
      const float fillX = r0.x + (r1.x - r0.x) * t;
      if (t > 0.0f)
      {
         // Deliberately low alpha for the plain interactive fill: it's a
         // tint the label and value stay readable over, not a solid block
         // that hides them. modulated/expression/recording get the knob's
         // own full-strength ring color instead (see KnobFloat's fillColor
         // stroke) - those three states are exactly what the user pointed at
         // saying "match the knob's saturation", and a translucent wash of
         // the same RGB read as a different, washed-out color next to it.
         const ImU32 alpha = vividState ? (isLight ? 225 : 205) : (isLight ? 110 : 86);
         const ImU32 soft = (fillColor & 0x00FFFFFF) | (alpha << 24);
         dl->AddRectFilled(r0, ImVec2(fillX, r1.y), soft, 3.0f);
      }
      dl->AddRect(r0, r1, isLight ? tok::U32(tok::pal::c_B4B9C8FF) : tok::U32(tok::pal::c_484C5CFF), 3.0f);

      std::string name(label);
      const size_t hash = name.find("##");
      if (hash != std::string::npos)
         name = name.substr(0, hash);
      char valBuf[48];
      FormatAudioParam(valBuf, sizeof(valBuf), fmt, *value);
      const float textY = r0.y + (r1.y - r0.y - ImGui::GetTextLineHeight()) * 0.5f;
      const ImVec2 valSize = AudioLabelSize(valBuf);
      const float valX = r1.x - 7.0f - valSize.x;
      dl->PushClipRect(r0, r1, true);
      ImU32 valCol, nameCol;
      if (vividState)
      {
         // The name label sits at the *unfilled* left edge until the value is
         // high enough to cover it, so text color has to follow the track's
         // own base (near-black dark / near-white light), not the fill's -
         // picking by the fill's hue (tried once) put dark text over purple,
         // which is invisible against the near-black track behind it. Same
         // white-on-dark/black-on-light rule as every other themed text in
         // the app, just forced to full strength since it now sits over a
         // near-opaque accent once the fill does cover it.
         valCol = isLight ? tok::U32(tok::pal::c_0F0F12FF) : tok::U32(tok::pal::c_F5F6FAFF);
         nameCol = isLight ? tok::U32(tok::pal::c_232328DC) : tok::U32(tok::pal::c_E2E4ECDC);
      }
      else
      {
         // Light-mode readOnly was a mid grey that read as low-contrast/washed
         // out against the card, and the light non-readOnly *name* below was
         // accidentally reusing the dark theme's pale blue-grey outright - all
         // but invisible on a light card. Darkened/raised-opacity across the
         // board in both themes per feedback that every one of these read too
         // faint.
         valCol = isLight
            ? (readOnly ? tok::U32(tok::pal::c_4E5464FF) : tok::U32(tok::pal::c_141824FF))
            : (readOnly ? tok::U32(tok::pal::c_CDD0DCFF) : tok::U32(tok::pal::c_EEF1FAFF));
         nameCol = isLight
            ? (readOnly ? tok::U32(tok::pal::c_646A7AFF) : tok::U32(tok::pal::c_505666FF))
            : (readOnly ? tok::U32(tok::pal::c_B2B5C4FF) : tok::U32(tok::pal::c_C6CAD8FF));
      }
      AudioLabelText(dl, ImVec2(valX, textY), valCol, valBuf);

      dl->PushClipRect(r0, ImVec2(std::max(r0.x, valX - 6.0f), r1.y), true);
      AudioLabelText(dl, ImVec2(r0.x + 7.0f, textY), nameCol, name.c_str());
      dl->PopClipRect();
      dl->PopClipRect();

      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         SetAudioReadout(name.c_str(), valBuf);
      return changed;
   }


   // Right-click on a modulated (read-only) param requests this: a polarity
   // toggle plus, in Bipolar mode, a depth slider - and Unbind, so there's
   // still a way to detach a cable without dragging it off or selecting the
   // link itself. Deliberately a context menu, not a panel - see 00-
   // modulation-polarity.md's "keep it minimal".
   //
   // Only records the request here - see gModBindingMenuNode's comment for
   // why the popup itself has to be opened and drawn later, outside
   // ed::Begin()/ed::End(), rather than right where the param is hovered.
   void DrawModulationBindingMenu(int nodeIndex, int paramIndex, bool hovered)
   {
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
         gModBindingMenuNode = nodeIndex;
         gModBindingMenuParam = paramIndex;
         gOpenModBindingMenu = true;
         gParamRightClickConsumedThisFrame = true;
      }
   }


   // "mode##fitnode" -> "mode". The visible half of an ImGui label is what a
   // param is called in the matrix, the binding menu and the perf surface.
   std::string StripParamLabel(const char* label)
   {
      std::string shown(label != nullptr ? label : "");
      const size_t hash = shown.find("##");
      if (hash != std::string::npos)
         shown = shown.substr(0, hash);
      return shown;
   }

   const void* TakePendingSrcAddr(const void* fallback)
   {
      const void* a = gPendingSrcAddr != nullptr ? gPendingSrcAddr : fallback;
      gPendingSrcAddr = nullptr;
      return a;
   }


   // Keyboard focus for the discrete params (checkboxes, dropdowns): the same
   // Tab walk and ring as KbParamHook, called right after the widget is drawn
   // so the ring can hug its rect. Returns the queued Left/Right step (-1, 0,
   // +1) for the caller to apply the way its widget applies a click. A cable-
   // driven param is skipped: it ignores keys exactly as it ignores the mouse.
   int KbDiscreteHook(int nodeIndex, int paramIndex, bool modulated)
   {
      if (modulated)
         return 0;
      bool seen = false;
      for (const KbParamEntry& e : gKbParams)
         seen = seen || (e.node == nodeIndex && e.param == paramIndex);
      if (!seen)
         gKbParams.push_back({ nodeIndex, paramIndex });
      if (CategoryColors::GetTooltips() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
         HelpTip("%s", T("Click to change. With the node selected, Tab to focus it and Left/Right to step"));
      if (nodeIndex != gKbFocusNode || paramIndex != gKbFocusParam)
         return 0;
      const ImVec2 rmin = ImGui::GetItemRectMin();
      const ImVec2 rmax = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRect(ImVec2(rmin.x - 2.0f, rmin.y - 2.0f), ImVec2(rmax.x + 2.0f, rmax.y + 2.0f),
                                          ImGui::GetColorU32(ImGuiCol_NavHighlight), 4.0f, 0, 2.0f);
      const int step = gKbNudge > 0 ? 1 : (gKbNudge < 0 ? -1 : 0);
      gKbNudge = 0;
      return step;
   }


   DiscreteParamHandle RegisterDiscreteParam(const char* label, float current, float maxV,
                                             bool isBool, const std::vector<std::string>* options,
                                             bool momentary)
   {
      DiscreteParamHandle h;
      const void* srcAddr = TakePendingSrcAddr(nullptr);
      if (gCurrentNodeIndex < 0)
         return h; // a settings dialog / browser widget, not a node param
      h.registered = true;
      h.nodeIndex = gCurrentNodeIndex;
      h.paramIndex = DiscreteParamSlot(h.nodeIndex, label != nullptr ? label : "");

      const std::pair<int, int> key(h.nodeIndex, h.paramIndex);
      float& slot = gDiscreteParamStore[key];
      const auto lastWritten = gDiscreteParamLastWritten.find(key);
      const bool external = lastWritten != gDiscreteParamLastWritten.end() &&
                            lastWritten->second != slot;
      h.modulated = Modulation::Instance().IsModulated(h.nodeIndex, h.paramIndex);
      h.driven = h.modulated || external;
      if (!h.driven)
         slot = current; // the widget owns the value until a cable takes it over

      ParamRef ref;
      ref.nodeIndex = h.nodeIndex;
      ref.paramIndex = h.paramIndex;
      ref.value = &slot;
      ref.minValue = 0.0f;
      ref.maxValue = maxV;
      ref.step = 1.0f; // discrete: ShapeToParam must never leave it between two states
      ref.name = StripParamLabel(label);
      ref.isBool = isBool;
      ref.isEnum = !isBool;
      ref.momentary = momentary; // a gate button: triggers pulse it instead of flipping it
      ref.srcAddr = srcAddr;
      // Only hand the option list over when the sticky store doesn't already
      // hold it: copying a dozens-long list of std::strings every frame for
      // every visible dropdown is exactly the allocation churn that made a
      // macro driving a mode feel less smooth than one driving a knob.
      if (options != nullptr)
      {
         const ParamRef* known = Modulation::Instance().KnownParam(h.nodeIndex, h.paramIndex);
         if (known == nullptr || known->enumOptions.size() != options->size() ||
             (!options->empty() && known->enumOptions.front() != options->front()))
            ref.enumOptions = *options;
      }
      Modulation::Instance().RegisterParam(ref);

      h.value = slot;
      gDiscreteParamLastWritten[key] = slot;
      h.draw = !gParamRegisterOnly;
      return h;
   }


   // Same pin id scheme, colours and gParamPinScreenList entry as ModSlider's
   // pin - that list is what the performance matrix's "Assign Parameter" picker
   // hit-tests, so registering here is what makes a mode or a checkbox
   // assignable to a surface control. Leaves the cursor on the same line.
   // `controlHeight`, when non-zero, is the frame height of the control this
   // pin belongs to (a checkbox, a dropdown button, ...). The pin is drawn
   // vertically centred on that control instead of at the raw cursor Y -
   // fixed once here so no call site has to remember to offset it (P2). The
   // next widget still lands at the row's original, un-shifted Y: the cursor
   // is restored explicitly at the end rather than left to ImGui's SameLine
   // tracking, which would otherwise pick up the shifted line.
   void DrawDiscreteParamPin(const DiscreteParamHandle& h, const char* label, float width,
                             float controlHeight)
   {
      const int pinId = h.nodeIndex * GraphNode::kStride + GraphNode::kParamBase + h.paramIndex;
      gDrawnParamPins.insert(pinId);

      ed::BeginPin(pinId, ed::PinKind::Input);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float box = 12.0f;
      const ImVec2 p = controlHeight > 0.0f
         ? ImVec2(origin.x, origin.y + (controlHeight - box) * 0.5f)
         : origin;
      ImGui::SetCursorScreenPos(p);
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      const bool isLight = IsThemeLight();
      const ImU32 pinColor = h.modulated
         ? (isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF))
         : (isLight ? tok::U32(tok::pal::c_A5AFC3FF) : tok::U32(tok::pal::c_788096FF));
      dl->AddCircleFilled(c, 4.0f, isLight ? tok::U32(tok::pal::c_F0F3FAFF) : tok::U32(tok::pal::c_121319FF));
      dl->AddCircle(c, h.modulated ? 4.0f : 4.5f, pinColor, 12, 2.0f);
      if (h.modulated)
         dl->AddCircleFilled(c, 2.0f, pinColor);
      ExpandPinHit(c, p.x + box);
      ed::EndPin();

      GraphNode* curGn = FindNodeByIndex(h.nodeIndex);
      // rowMin starts after the pin box + its SameLine gap, not at the pin
      // itself - the assign-mode highlight traces the actual widget, and
      // starting it at the pin left the highlight overhanging past the
      // widget's real left edge by the pin's own width.
      gParamPinScreenList.push_back({ h.nodeIndex, h.paramIndex, curGn ? curGn->typeName : "",
                                      StripParamLabel(label), c, ImVec2(p.x + box + 4.0f, p.y),
                                      ImVec2(p.x + width, p.y + box + 4.0f) });
      ImGui::SetCursorScreenPos(ImVec2(origin.x + box + 4.0f, origin.y));
   }


   void DropdownButton(const char* label, const std::vector<std::string>& options,
                       int current, std::function<void(int)> onSelect, float width,
                       bool showCaption)
   {
      if (options.empty())
         return;
      const int lastIndex = (int)options.size() - 1;
      int safeCurrent = std::max(0, std::min(current, lastIndex));

      const DiscreteParamHandle h =
         RegisterDiscreteParam(label, (float)safeCurrent, (float)lastIndex, /*isBool=*/false, &options);
      if (h.registered)
      {
         // A cable wins over the button, exactly as it does on a slider. The
         // write goes back through onSelect rather than into a float, because
         // the node's enum is whatever that lambda assigns - some of them do
         // more than a plain store (reopen a device, reload a font).
         if (h.driven)
         {
            const int drivenIdx = std::clamp((int)lroundf(h.value), 0, lastIndex);
            if (drivenIdx != current && onSelect)
            {
               // Modulation never creates an undo entry or dirties the patch:
               // most onSelect lambdas open with PushUndoCheckpoint() for the
               // user-click path, so suppress it here. The param write still runs.
               const bool wasSuppressed = gSuppressUndoCheckpoints;
               gSuppressUndoCheckpoints = true;
               onSelect(drivenIdx);
               gSuppressUndoCheckpoints = wasSuppressed;
            }
            safeCurrent = drivenIdx;
         }
         if (!h.draw)
            return; // registered so the modulator keeps writing; just not drawn
         DrawDiscreteParamPin(h, label, width, ImGui::GetFrameHeight());
         width = std::max(24.0f, width - 16.0f); // the pin ate 12px + 4px of the row (DrawDiscreteParamPin's box)
      }

      const std::string caption = options[safeCurrent] + "##" + label;
      PushDropdownStyle();
      if (h.modulated)
      {
         // Read-only look, matching a modulated slider: the value still reads
         // live, the control just stops taking input.
         ImGui::PushStyleColor(ImGuiCol_Text, IsThemeLight() ? tok::V4(tok::palf::v_550_380_100_1000)
                                                             : tok::V4(tok::palf::v_1000_750_350_1000));
         ImGui::BeginDisabled();
         ImGui::Button(caption.c_str(), ImVec2(width, 0));
         ImGui::EndDisabled();
         ImGui::PopStyleColor();
         // BeginDisabled swallows hover, so ask the rect directly - otherwise
         // there is no way to right-click a modulated dropdown to unbind it.
         DrawModulationBindingMenu(h.nodeIndex, h.paramIndex,
                                   ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(),
                                                              ImGui::GetItemRectMax()));
      }
      else if (ImGui::Button(caption.c_str(), ImVec2(width, 0)))
      {
         gDropdown.options = options;
         gDropdown.categories.clear(); // this call site has no category grouping - drop whatever the last dropdown left behind
         gDropdown.onSelect = std::move(onSelect);
         gDropdown.current = safeCurrent;
         gDropdown.justOpened = true;
         gDropdown.focusSearch = false;
      }
      if (h.registered && h.draw)
      {
         // Tab + Left/Right on a focused dropdown steps to the previous / next option.
         const int step = KbDiscreteHook(h.nodeIndex, h.paramIndex, h.modulated);
         const int stepped = std::clamp(safeCurrent + step, 0, lastIndex);
         if (step != 0 && stepped != safeCurrent && onSelect) // empty if a click just moved it into gDropdown
            onSelect(stepped);
      }
      PopDropdownStyle();
      if (!showCaption)
         return;
      ImGui::SameLine();
      ImGui::TextDisabled("%s", StripParamLabel(label).c_str());
   }


   void PushCheckboxStyle()
   {
      const bool isLight = IsThemeLight();
      // Same budget as PushDropdownStyle, and deliberately the same dark fill
      // - both are recesses in the same body, so they should read as the
      // same depth of chrome. CheckMark is the one element allowed full
      // accent brightness (bumped here vs. the old value): once the frame
      // stops competing with it, the checked state can and should be the
      // loudest thing in an unchecked row of quiet frames. See P10.
      ImGui::PushStyleColor(ImGuiCol_FrameBg, isLight ? tok::V4(tok::palf::v_860_880_940_1000) : tok::V4(tok::palf::v_160_180_240_1000));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, isLight ? tok::V4(tok::palf::v_800_840_920_1000) : tok::V4(tok::palf::v_250_280_380_1000));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, isLight ? tok::V4(tok::palf::v_740_780_880_1000) : tok::V4(tok::palf::v_320_360_480_1000));
      ImGui::PushStyleColor(ImGuiCol_CheckMark, isLight ? tok::V4(tok::palf::v_200_550_950_1000) : tok::V4(tok::palf::v_550_820_1000_1000));
      ImGui::PushStyleColor(ImGuiCol_Border, isLight ? tok::V4(tok::palf::v_700_740_840_1000) : tok::V4(tok::palf::v_220_235_278_1000));
      ImGui::PushStyleColor(ImGuiCol_Text, isLight ? tok::V4(tok::palf::v_150_180_240_1000) : tok::V4(tok::palf::v_880_920_980_1000));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, isLight ? 1.0f : 0.0f);
   }


   void PopCheckboxStyle()
   {
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(6);
   }


   // Same P10 budget as PushCheckboxStyle/PushDropdownStyle, applied to a
   // plain ImGui::SliderFloat/SliderInt track: the track fill is a recess
   // within ~0.06 luminance of the node/panel body in dark, no border stroke
   // in dark (FrameBorderSize 0), and the grab is the one element allowed to
   // sit at accent brightness so it stays findable against a quiet track.
   // Light keeps a hairline border, same reasoning as the dropdown/checkbox
   // case: the panel there is bright enough that a recess needs a real edge.
   // Never hand-roll a one-off slider colour at a call site - this is the
   // one place it's defined (P11).
   void PushSliderStyle()
   {
      const bool isLight = IsThemeLight();
      ImGui::PushStyleColor(ImGuiCol_FrameBg, isLight ? tok::V4(tok::palf::v_860_880_940_1000) : tok::V4(tok::palf::v_160_180_240_1000));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, isLight ? tok::V4(tok::palf::v_800_840_920_1000) : tok::V4(tok::palf::v_250_280_380_1000));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, isLight ? tok::V4(tok::palf::v_740_780_880_1000) : tok::V4(tok::palf::v_320_360_480_1000));
      ImGui::PushStyleColor(ImGuiCol_SliderGrab, isLight ? tok::V4(tok::palf::v_200_550_950_1000) : tok::V4(tok::palf::v_550_820_1000_1000));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, isLight ? tok::V4(tok::palf::v_140_450_850_1000) : tok::V4(tok::palf::v_700_900_1000_1000));
      ImGui::PushStyleColor(ImGuiCol_Border, isLight ? tok::V4(tok::palf::v_700_740_840_1000) : tok::V4(tok::palf::v_220_235_278_1000));
      ImGui::PushStyleColor(ImGuiCol_Text, isLight ? tok::V4(tok::palf::v_150_180_240_1000) : tok::V4(tok::palf::v_880_920_980_1000));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, isLight ? 1.0f : 0.0f);
   }


   void PopSliderStyle()
   {
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(7);
   }


   uint64_t UidForIndex(int nodeIndex)
   {
      GraphNode* gn = FindNodeByIndex(nodeIndex);
      return gn != nullptr ? gn->uid : 0;
   }


   IPredictor* PredictorForParam(int nodeIndex, int paramIndex)
   {
      const Modulation::Source s = Modulation::Instance().ModulatorFor(nodeIndex, paramIndex);
      if (s.nodeIndex < 0)
         return nullptr;
      GraphNode* gn = FindNodeByIndex(s.nodeIndex);
      return (gn != nullptr && gn->node != nullptr) ? dynamic_cast<IPredictor*>(gn->node.get()) : nullptr;
   }


   // Why a predictor (green) source may not be bound to this destination, or nullptr if it may.
   // Every user-facing bind path calls this; patch load / paste / undo cannot (the destination has
   // not registered yet), so they rely on the apply loop's own discrete guard instead.
   const char* PredictorBindRefusal(INode* srcNode, int dstNodeIndex, int dstParamIndex)
   {
      if (srcNode == nullptr || dynamic_cast<IPredictor*>(srcNode) == nullptr)
         return nullptr;
      const ParamRef* known = Modulation::Instance().KnownParam(dstNodeIndex, dstParamIndex);
      if (known != nullptr && (known->isEnum || known->isBool))
         return "A predictor drives continuous parameters only, not switches or menus";
      return nullptr;
   }


   // A predictor bound to a discrete param is inert: the apply loop never writes it.
   bool IsInertPredictorBinding(int dstNodeIndex, int dstParamIndex)
   {
      return PredictorForParam(dstNodeIndex, dstParamIndex) != nullptr &&
             PredictorBindRefusal(FindNodeByIndex(Modulation::Instance().ModulatorFor(dstNodeIndex, dstParamIndex).nodeIndex)->node.get(),
                                  dstNodeIndex, dstParamIndex) != nullptr;
   }


   // Anything from the Prediction category that drives a parameter reads as "predicted"
   // (green): Predictive LFO / Macro (IPredictor) and Predictive Modulator alike. Only the
   // IPredictor ones also get the grab/ghost machinery above.
   bool IsPredictionSourceNode(const GraphNode* gn)
   {
      return gn != nullptr && gn->node != nullptr &&
             (gn->category == "Prediction" || dynamic_cast<IPredictor*>(gn->node.get()) != nullptr);
   }


   bool IsPredictionBinding(int nodeIndex, int paramIndex)
   {
      const Modulation::Source s = Modulation::Instance().ModulatorFor(nodeIndex, paramIndex);
      return s.nodeIndex >= 0 && IsPredictionSourceNode(FindNodeByIndex(s.nodeIndex));
   }


   // Fader position (0..1) <-> parameter value, honouring the widget's own taper when it has one.
   float ParamToPos(const ParamRef& r, float v)
   {
      if (r.valueToPos != nullptr)
         return std::clamp(r.valueToPos(v, r.minValue, r.maxValue), 0.0f, 1.0f);
      const float span = r.maxValue - r.minValue;
      return span != 0.0f ? std::clamp((v - r.minValue) / span, 0.0f, 1.0f) : 0.0f;
   }

   float PosToParam(const ParamRef& r, float pos)
   {
      if (r.posToValue != nullptr)
         return r.posToValue(pos, r.minValue, r.maxValue);
      return r.minValue + (r.maxValue - r.minValue) * pos;
   }


   PredictorGrabCtx BeginPredictorGrab(const ParamRef& ref)
   {
      PredictorGrabCtx c;
      c.pred = PredictorForParam(ref.nodeIndex, ref.paramIndex);
      if (c.pred == nullptr)
         return c;
      c.key = ParamKey{ UidForIndex(ref.nodeIndex), ref.paramIndex };
      c.editable = ImGui::GetIO().KeyShift || gPredictorGrabsPrev.count(c.key) > 0;
      return c;
   }


   // Predictor destination widgets: clean view with no overlaid dots.
   void DrawPredictorDecor(const ParamRef& ref, float laneMin, float laneMax, float laneY)
   {
      (void)ref; (void)laneMin; (void)laneMax; (void)laneY;
   }


   // Call right after the widget's item is drawn (so the IsItem* queries refer to it).
   void EndPredictorGrab(const PredictorGrabCtx& c, const ParamRef& ref)
   {
      if (c.pred == nullptr || !c.editable)
         return;
      // Release velocity from the last <=100 ms of this grab, in fader space. Computed here rather
      // than read from MovementStats: that engine lives on the log worker thread and sees the
      // release late, while OnRelease needs the number on the frame the hand lets go.
      struct Track { double t[8]; float pos[8]; int n = 0; };
      static std::map<ParamKey, Track> sTracks;
      Track& tr = sTracks[c.key];
      const double now = ImGui::GetTime();
      if (ImGui::IsItemActivated())
      {
         PushUndoCheckpoint();
         tr.n = 0;
         c.pred->OnGrab(c.key);
      }
      if (ImGui::IsItemActive())
      {
         gPredictorGrabs.insert(c.key);
         const float pos = ParamToPos(ref, *ref.value);
         if (tr.n == 8)
         {
            std::copy(tr.t + 1, tr.t + 8, tr.t);
            std::copy(tr.pos + 1, tr.pos + 8, tr.pos);
            tr.n = 7;
         }
         tr.t[tr.n] = now;
         tr.pos[tr.n] = pos;
         ++tr.n;
      }
      if (ImGui::IsItemDeactivated())
      {
         float vel = 0.0f;
         if (tr.n >= 2)
         {
            int i0 = tr.n - 1;
            while (i0 > 0 && tr.t[tr.n - 1] - tr.t[i0 - 1] <= 0.1)
               --i0;
            const double span = tr.t[tr.n - 1] - tr.t[i0];
            if (i0 < tr.n - 1 && span > 1e-3)
               vel = (float)((tr.pos[tr.n - 1] - tr.pos[i0]) / span);
         }
         c.pred->OnRelease(c.key, ParamToPos(ref, *ref.value), vel);
         tr.n = 0;
      }
   }
}
