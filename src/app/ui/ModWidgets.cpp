// Modulatable sliders, knobs, faders, toggles, pins (moved verbatim from main.cpp).
#include "app/ui/design/components/PinDot.h"
#include "app/ui/design/components/StateRing.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/GlyphToggle.h"
#include "app/ui/design/components/VFader.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   // outUserChanged (optional) is set true only when a real ImGui click flipped
   // the box - never for a cable-driven change. The return value still reports
   // both, because setter-style callers need it; a caller that pushes an undo
   // checkpoint must key it on outUserChanged instead, so modulation never
   // creates an undo entry or dirties the patch.
   bool ModCheckbox(const char* label, bool* value, bool* outUserChanged)
   {
      if (outUserChanged)
         *outUserChanged = false;
      if (value == nullptr)
         return false;

      gPendingSrcAddr = value;
      const DiscreteParamHandle h =
         RegisterDiscreteParam(label, *value ? 1.0f : 0.0f, 1.0f, /*isBool=*/true, nullptr);
      if (!h.registered)
      {
         PushCheckboxStyle();
         bool changed = NodeCheckbox(label, value);
         PopCheckboxStyle();
         if (outUserChanged)
            *outUserChanged = changed;
         return changed;
      }

      // Reported as "changed" the frame a cable flips it, because a lot of
      // call sites are `bool tmp = node->Get(); if (ModCheckbox(.., &tmp))
      // node->Set(tmp);` over a temporary - without this the modulator would
      // move the checkbox and nothing would ever reach the node.
      const bool incoming = *value;
      bool changed = false;
      if (h.driven)
      {
         *value = h.value >= 0.5f;
         changed = (*value != incoming);
      }
      if (!h.draw)
         return changed;

      DrawDiscreteParamPin(h, label, kParamWidth, ImGui::GetFrameHeight());

      PushCheckboxStyle();
      if (h.modulated)
      {
         bool shown = *value;
         ImGui::PushStyleColor(ImGuiCol_CheckMark, IsThemeLight() ? tok::V4(tok::palf::v_840_490_80_1000)
                                                                  : tok::V4(tok::palf::v_1000_750_350_1000));
         ImGui::BeginDisabled();
         NodeCheckbox(label, &shown);
         ImGui::EndDisabled();
         ImGui::PopStyleColor();
         DrawModulationBindingMenu(h.nodeIndex, h.paramIndex,
                                   ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(),
                                                              ImGui::GetItemRectMax()));
      }
      else
      {
         bool clicked = NodeCheckbox(label, value);
         const bool hovered = ImGui::IsItemHovered();
         // Tab + Left/Right on a focused checkbox: right = on, left = off.
         const int step = KbDiscreteHook(h.nodeIndex, h.paramIndex, false);
         if (step != 0 && *value != (step > 0))
         {
            *value = step > 0;
            clicked = true;
         }
         if (outUserChanged)
            *outUserChanged = clicked;
         changed = clicked || changed;
         DrawModulationBindingMenu(h.nodeIndex, h.paramIndex, hovered);
      }
      PopCheckboxStyle();
      return changed;
   }

   // `audioStyle` swaps the widget in the middle of this function for
   // AudioSliderFloat above. Everything around it - the pin, the typed-edit
   // field, the expression/modulation branches, undo, hotkeys - is shared,
   // so an audio slider is modulatable and expression-drivable on exactly
   // the same terms as every other param in the ap
   bool ModSlider(const char* label, float* value, float minV, float maxV, const char* fmt,
                  float width, bool audioStyle, float step,
                  FaderPosToValueFn posToValue, FaderValueToPosFn valueToPos,
                  int explicitParamIndex, const char* nameOverride)
   {
      // One slider look everywhere: accent fill behind the value, no grab square. The plain ImGui
      // branches below are kept only until they are deleted; nothing reaches them.
      audioStyle = true;
      const int nodeIndex = gCurrentNodeIndex;
      const int paramIndex = (explicitParamIndex >= 0) ? explicitParamIndex : gParamCounter++;
      // nameOverride: what the matrix / binding menu / perf picker call the
      // param, when the drawn caption alone is ambiguous (the MPC draws one
      // "volume" row that stands for whichever pad is selected).
      const char* paramName = nameOverride != nullptr ? nameOverride : label;

      ParamRef ref;
      ref.nodeIndex = nodeIndex;
      ref.paramIndex = paramIndex;
      ref.value = value;
      ref.minValue = minV;
      ref.maxValue = maxV;
      ref.step = step;
      ref.name = paramName;
      ref.posToValue = posToValue;
      ref.valueToPos = valueToPos;
      ref.srcAddr = TakePendingSrcAddr(value);
      Modulation::Instance().RegisterParam(ref);
      if (gParamRegisterOnly)
         return false; // registered, deliberately not drawn - see gParamRegisterOnly

      const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kParamBase + paramIndex;
      const bool modulated = Modulation::Instance().IsModulated(nodeIndex, paramIndex);
      // An expression only actually drives the field while nothing is wired
      // into its pin - see Modulation::SetExpression. It stays stored either
      // way, so unpatching the cable brings it straight back.
      const bool hasExpr = !modulated && Modulation::Instance().HasExpression(nodeIndex, paramIndex);
      // A stored expression that currently fails to parse/evaluate (bad
      // syntax, unknown function, division by zero) still counts as
      // "has an expression" - it stays queued for the moment it's fixed -
      // but nothing is overwriting *value while it's broken (see the apply
      // pass), so it's safe to just fall back to the plain, fully-interactive
      // slider look rather than a special locked/coloured state. The fx/x
      // controls stay so the stored formula can still be reopened or cleared.
      const std::string* hasExprErr = hasExpr ? Modulation::Instance().ExpressionErrorFor(nodeIndex, paramIndex)
                                               : nullptr;
      const bool exprErrored = hasExprErr != nullptr && !hasExprErr->empty();
      // Shift-held movement, not modulation or an expression - a distinct,
      // purely transient state, so it can coexist with exprErrored (still
      // editable) but never with modulated/hasExpr (both read-only already).
      const bool recording = !modulated && GestureRecorder::Instance().IsRecording(nodeIndex, paramIndex);
      gDrawnParamPins.insert(pinId);

      ImGui::PushID(paramIndex + 5000);

      ed::BeginPin(pinId, ed::PinKind::Input);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float box = tok::pin_box;
      // Centred on the slider track's frame height, same fix and same
      // restore-the-cursor-explicitly reasoning as DrawDiscreteParamPin - see
      // the comment there. The track itself is drawn later at `origin`'s Y.
      const float controlHeight = ImGui::GetFrameHeight();
      const ImVec2 p = ImVec2(origin.x, origin.y + (controlHeight - box) * 0.5f);
      ImGui::SetCursorScreenPos(p);
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      const bool isLight = IsThemeLight();
      const bool predicted = modulated && IsPredictionBinding(nodeIndex, paramIndex);
      PinDot::Param(dl, c,
                    predicted ? PinDot::State::Prediction
                    : modulated ? PinDot::State::Modulated
                    : hasExpr && !exprErrored ? PinDot::State::Expression
                    : PinDot::State::Idle,
                    isLight);
      ExpandPinHit(c, p.x + box);
      ed::EndPin();
      GraphNode* curGn = FindNodeByIndex(nodeIndex);
      // rowMin starts after the pin box + its SameLine gap, not at the pin
      // itself - see the matching comment on DrawDiscreteParamPin's push.
      gParamPinScreenList.push_back({ nodeIndex, paramIndex, curGn ? curGn->typeName : "", paramName ? paramName : "", c,
                                      ImVec2(p.x + box + 4.0f, p.y), ImVec2(p.x + width, p.y + box + 4.0f) });
      if (paramIndex == gPredTestSizeXParam && nodeIndex == gPredTestNodeIndex)
         gPredTestSliderCanvas = ImVec4(p.x + box + 4.0f, p.y, p.x + width, p.y + box + 4.0f);
      ImGui::SetCursorScreenPos(ImVec2(origin.x + box + 4.0f, origin.y));

      // Double-clicking swaps the slider for a text field so an exact value
      // (or, prefixed with '=', an expression) can be typed. ImGui's built-in
      // Ctrl+click does this too, but double-click is what people reach for.
      const std::pair<int, int> editKey(nodeIndex, paramIndex);
      bool typing = gTypedParam.count(editKey) > 0;

      bool changed = false;
      if (typing && !modulated)
      {
         ImGui::SetNextItemWidth(width - box - 4.0f);
         if (gTypedParamJustOpened == editKey)
         {
            ImGui::SetKeyboardFocusHere();
            gTypedParamJustOpened = std::pair<int, int>(-1, -1);
         }
         char buf[256];
         snprintf(buf, sizeof(buf), "%s", gTypedParamText[editKey].c_str());
         // A plain InputText rather than InputFloat: the field has to accept
         // letters too, so a typed expression like "sin(t)" is not filtered
         // out character-by-character the way InputFloat's numeric-only input
         // would filter it.
         //
         // ImGuiInputTextFlags_AutoSelectAll only selects on a *mouse* click;
         // this field is focused programmatically via SetKeyboardFocusHere,
         // which instead runs through ImGui's nav-activation path and forces
         // its own select-all the moment the field actually goes active
         // (which can land a frame after SetKeyboardFocusHere is called) -
         // regardless of any flag we pass. So don't rely on flags for
         // selection at all: drive it explicitly below once the field is
         // confirmed active, which also lets the hover+type path leave the
         // cursor at the end instead of selecting the seeded digit.
         // '##'-prefixed, like ModKnob's field. ImGui::InputText(label, ...)
         // draws the caption OUTSIDE the box to the right and adds its width
         // to the item, so a bare label both shrinks the field and spills the
         // caption across whatever is beside it - inside a column that means
         // over the neighbouring engine. This is the same defect the knob path
         // fixed; the slider path never got it.
         const std::string typedId = std::string("##typed") + label;
         FieldWell::PushTypedEditStyle();
         const bool entered = ImGui::InputText(typedId.c_str(), buf, sizeof(buf),
                                               ImGuiInputTextFlags_EnterReturnsTrue);
         FieldWell::PopTypedEditStyle();
         gTypedParamText[editKey] = buf;
         if (gTypedParamPendingInit.count(editKey) && ImGui::IsItemActive())
         {
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID()))
            {
               if (gTypedParamNoAutoSelect.count(editKey))
               {
                  state->ReloadUserBufAndMoveToEnd();
                  state->ClearSelection();
               }
               else
               {
                  state->SelectAll();
               }
            }
            gTypedParamPendingInit.erase(editKey);
         }
         // IsItemActivated() has to be queried right after its widget, and it
         // fires on the frame focus begins - before any edit is applied that
         // same frame - so the checkpoint still captures the pre-edit value
         // even though the check reads textually "after" InputText here.
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (entered || ImGui::IsItemDeactivated())
         {
            const std::string trimmed = TrimCopy(gTypedParamText[editKey]);
            if (!trimmed.empty() && trimmed[0] == '=')
            {
               const std::string exprText = TrimCopy(trimmed.substr(1));
               if (exprText.empty())
                  Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               else
               {
                  Modulation::Instance().SetExpression(nodeIndex, paramIndex, exprText);
                  // A typed expression fully replaces whatever was driving
                  // this param before, including a looping recording - left
                  // alive underneath, it would silently reactivate the moment
                  // the expression was later unbound (isRecordingState only
                  // requires !hasExpr), forcing a second Unbind to actually
                  // remove it.
                  GestureRecorder::Instance().CancelArm(nodeIndex, paramIndex);
                  GestureRecorder::Instance().StopPlayback(nodeIndex, paramIndex);
               }
            }
            else
            {
               Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               if (!trimmed.empty() && !TypedTextIsUntouchedSeed(editKey, trimmed))
               {
                  char* end = nullptr;
                  float parsed = strtof(trimmed.c_str(), &end);
                  if (end != trimmed.c_str())
                  {
                     *value = std::clamp(parsed, std::min(minV, maxV), std::max(minV, maxV));
                     changed = true;
                     // Typing a literal value is a static override - it should
                     // win outright, not sit underneath a gesture loop that
                     // would just overwrite it again next frame.
                     GestureRecorder::Instance().StopPlayback(nodeIndex, paramIndex);
                  }
               }
            }
            gTypedParam.erase(editKey);
            gTypedParamText.erase(editKey);
            gTypedParamSeed.erase(editKey);
            gTypedParamNoAutoSelect.erase(editKey);
            gTypedParamPendingInit.erase(editKey);
         }
      }
      else if (modulated)
      {
         // value is driven externally; show it read-only so it is obvious why
         // dragging does nothing
         float shown = *value;
         // A green (prediction) binding is read-only like any other, except under Shift: then the
         // editable path is drawn, the hand wins, and the predictor is told about the grab.
         ParamRef gref;
         gref.nodeIndex = nodeIndex; gref.paramIndex = paramIndex; gref.value = value;
         gref.minValue = minV; gref.maxValue = maxV; gref.posToValue = posToValue; gref.valueToPos = valueToPos;
         const PredictorGrabCtx grab = BeginPredictorGrab(gref);
         const bool ro = !grab.editable;
         if (audioStyle)
         {
            // Same muted amber as the plain-slider branch's FrameBg below
            // (0.32,0.24,0.08 dark / 0.98,0.86,0.58 light), not the knob's
            // full-bright ring color - the user tried the bright version
            // here and asked for this darker tone back, same as the Color
            // Adjustments node's params already use.
            const ImU32 trackCol = predicted ? (isLight ? tok::U32(tok::pal::c_B0E8C2FF) : tok::U32(tok::pal::c_144A28FF))
                                             : (isLight ? tok::U32(tok::pal::c_FADB94FF) : tok::U32(tok::pal::c_523D14FF));
            const bool moved = AudioSliderFloat(label, &shown, minV, maxV, fmt, width - box - 4.0f,
                             trackCol, /*readOnly=*/ro, posToValue, valueToPos, /*vividState=*/true);
            if (grab.editable && moved)
            {
               *value = shown;
               changed = true;
            }
         }
         else
         {
            // This amber track used to stay the same fixed dark color in both
            // themes with the value text forced light to stay readable on
            // it - technically legible, but it fought the rest of the light
            // theme's much paler surfaces. Track and text now both flip with
            // the theme, same as the pin dot's own modulated color a few
            // lines up: a light pastel amber with black text in light mode,
            // the original dark amber with white text in dark mode.
            if (predicted)
            {
               if (isLight)
               {
                  ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_690_910_760_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_100_550_250_1000));
                  ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_50_50_50_1000));
               }
               else
               {
                  ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_80_290_160_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_350_850_500_1000));
                  ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_920_980_940_1000));
               }
            }
            else if (isLight)
            {
               ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_980_860_580_1000));
               ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_800_520_100_1000));
               ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_50_50_50_1000));
            }
            else
            {
               ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_320_240_80_1000));
               ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_950_720_320_1000));
               ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_970_950_900_1000));
            }
            ImGui::SetNextItemWidth(width - box - 4.0f);
            const bool moved = ImGui::SliderFloat(label, &shown, minV, maxV, fmt,
                                                  ro ? ImGuiSliderFlags_NoInput : ImGuiSliderFlags_None);
            ImGui::PopStyleColor(3);
            if (grab.editable && moved)
            {
               *value = shown;
               changed = true;
            }
         }
         EndPredictorGrab(grab, gref);
         {
            const ImVec2 imin = ImGui::GetItemRectMin(), imax = ImGui::GetItemRectMax();
            DrawPredictorDecor(gref, imin.x, std::min(imax.x, imin.x + width - box - 4.0f), imax.y - 3.0f);
         }
         DrawModulationBindingMenu(nodeIndex, paramIndex, ImGui::IsItemHovered());
      }
      else if (hasExpr && !exprErrored)
      {
         // Driven by a typed expression, re-evaluated every frame by the
         // apply pass in the main loop. No fx/x badge any more - the purple
         // track/fill is the only signal, matching how the modulated (amber)
         // and recording (red) states each get one colour and nothing else.
         // A click/drag only ever jumps the local `shown` copy, which is
         // discarded and overwritten by next frame's expression re-apply -
         // dragging never clears the expression. The only way to remove it is
         // the right-click menu's Unbind, so an accidental drag can't destroy
         // a formula someone typed on purpose.
         float shown = *value;
         if (audioStyle)
         {
            // Same muted purple as the plain-slider branch's FrameBg below.
            AudioSliderFloat(label, &shown, minV, maxV, fmt, width - box - 4.0f,
                             isLight ? tok::U32(tok::pal::c_E0D1FAFF) : tok::U32(tok::pal::c_332652FF),
                             /*readOnly=*/false, posToValue, valueToPos, /*vividState=*/true);
         }
         else
         {
            // Same theme-flipped track/text pairing as the modulated (amber)
            // state above - see that comment.
            if (isLight)
            {
               ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_880_820_980_1000));
               ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_480_280_850_1000));
               ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_50_50_50_1000));
            }
            else
            {
               ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_200_150_320_1000));
               ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_660_510_980_1000));
               ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_950_930_990_1000));
            }
            ImGui::SetNextItemWidth(width - box - 4.0f);
            ImGui::SliderFloat(label, &shown, minV, maxV, fmt);
            ImGui::PopStyleColor(3);
         }
         const bool hovered = ImGui::IsItemHovered();
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
         DrawModulationBindingMenu(nodeIndex, paramIndex, hovered);
         if (hovered && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }
      else if (hasExpr) // exprErrored
      {
         // The stored expression is currently failing to evaluate. Nothing
         // is writing to *value while that's true (see the apply pass), so
         // there's nothing unsafe about just falling back to a plain,
         // fully-interactive slider on the real value - no special colour,
         // no lock, and no fx/x badge either: it looks exactly like a param
         // with no expression at all. The stored (broken) formula is still
         // reachable to fix or clear via double-click, right-click, or
         // hovering and typing '=' - see HandleParamTypeHotkeys - same as it
         // would be if this were a fresh, non-expression param.
         // Recording uses the same muted red as the plain-slider branch's
         // FrameBg a few lines down, not the knob's bright ring - see the
         // modulated/expression branches above for why.
         const ImU32 activeCol = recording ? (isLight ? tok::U32(tok::pal::c_FCCCCCFF) : tok::U32(tok::pal::c_571A1AFF))
                                            : tok::U32(tok::pal::c_78C8FFEB);
         if (audioStyle)
         {
            changed = AudioSliderFloat(label, value, minV, maxV, fmt, width - box - 4.0f,
                                       activeCol, /*readOnly=*/false, posToValue, valueToPos,
                                       /*vividState=*/recording);
         }
         else
         {
            if (recording)
            {
               // Hovered/Active too, not just the base FrameBg - leaving
               // those two on the theme default meant hovering (let alone
               // dragging) a recording slider flashed back to the ordinary
               // blue/grey the instant the mouse was over it. Track and text
               // both flip with the theme, same as the other colored states.
               if (isLight)
               {
                  ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_990_800_800_1000));
                  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tok::V4(tok::palf::v_990_720_720_1000));
                  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tok::V4(tok::palf::v_990_660_660_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_800_140_140_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, tok::V4(tok::palf::v_800_140_140_1000));
                  ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_50_50_50_1000));
               }
               else
               {
                  ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_340_100_100_1000));
                  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tok::V4(tok::palf::v_400_120_120_1000));
                  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tok::V4(tok::palf::v_460_140_140_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_920_300_300_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, tok::V4(tok::palf::v_920_300_300_1000));
                  ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_990_930_930_1000));
               }
            }
            ImGui::SetNextItemWidth(width - box - 4.0f);
            changed = ImGui::SliderFloat(label, value, minV, maxV, fmt);
            if (recording)
               ImGui::PopStyleColor(6);
         }
         const bool justActivated = ImGui::IsItemActivated();
         if (justActivated)
         {
            PushUndoCheckpoint();
            GestureRecorder::Instance().StopPlayback(nodeIndex, paramIndex);
         }
         if (ImGui::IsItemActive() && ((ImGui::GetIO().KeyShift && ImGui::IsMouseDown(0)) || GestureRecorder::Instance().IsArmed(nodeIndex, paramIndex)))
            GestureRecorder::Instance().NotifyMovement(nodeIndex, paramIndex, *value, GestureRecorder::Instance().RecordClockNow(), /*isNewGrab=*/justActivated);
         if (ImGui::IsItemDeactivated())
            GestureRecorder::Instance().MaybeFinishArmedRecording(nodeIndex, paramIndex, GestureRecorder::Instance().RecordClockNow());
         const bool hovered = ImGui::IsItemHovered();
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
         DrawModulationBindingMenu(nodeIndex, paramIndex, hovered);
         if (hovered && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }
      else
      {
         // Recording uses the same muted red as the plain-slider branch's
         // FrameBg a few lines down, not the knob's bright ring - see the
         // modulated/expression branches above for why.
         const ImU32 activeCol = recording ? (isLight ? tok::U32(tok::pal::c_FCCCCCFF) : tok::U32(tok::pal::c_571A1AFF))
                                            : tok::U32(tok::pal::c_78C8FFEB);
         // A finished, looping recording is locked against direct grabs, the
         // same way modulated/expression params are - only Shift (re-record
         // via a session) or an explicit "Record Again" arm may drive it
         // manually; a plain click/drag just moves a discarded local copy so
         // an accidental grab can't silently erase the loop. Right-click
         // Unbind is the only way to remove it.
         const bool hasPlayback = GestureRecorder::Instance().Playbacks().count(GestureRecorder::Key(nodeIndex, paramIndex)) > 0;
         const bool locked = hasPlayback && !ImGui::GetIO().KeyShift && !GestureRecorder::Instance().IsArmed(nodeIndex, paramIndex);
         float shown = *value;
         if (audioStyle)
         {
            const bool sliderChanged = AudioSliderFloat(label, &shown, minV, maxV, fmt, width - box - 4.0f,
                                       activeCol, /*readOnly=*/false, posToValue, valueToPos,
                                       /*vividState=*/recording);
            if (!locked)
               changed = sliderChanged;
         }
         else
         {
            if (recording)
            {
               // Hovered/Active too, not just the base FrameBg - leaving
               // those two on the theme default meant hovering (let alone
               // dragging) a recording slider flashed back to the ordinary
               // blue/grey the instant the mouse was over it. Track and text
               // both flip with the theme, same as the other colored states.
               if (isLight)
               {
                  ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_990_800_800_1000));
                  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tok::V4(tok::palf::v_990_720_720_1000));
                  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tok::V4(tok::palf::v_990_660_660_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_800_140_140_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, tok::V4(tok::palf::v_800_140_140_1000));
                  ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_50_50_50_1000));
               }
               else
               {
                  ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_340_100_100_1000));
                  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tok::V4(tok::palf::v_400_120_120_1000));
                  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tok::V4(tok::palf::v_460_140_140_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrab, tok::V4(tok::palf::v_920_300_300_1000));
                  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, tok::V4(tok::palf::v_920_300_300_1000));
                  ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_990_930_930_1000));
               }
            }
            ImGui::SetNextItemWidth(width - box - 4.0f);
            const bool sliderChanged = ImGui::SliderFloat(label, &shown, minV, maxV, fmt);
            if (!locked)
               changed = sliderChanged;
            if (recording)
               ImGui::PopStyleColor(6);
         }
         if (!locked)
            *value = shown;
         // Activation is the first frame of the drag, before that frame's own
         // delta is applied, so this is still the pre-drag value.
         const bool justActivated = ImGui::IsItemActivated();
         if (justActivated && !locked)
         {
            PushUndoCheckpoint();
            GestureRecorder::Instance().StopPlayback(nodeIndex, paramIndex);
         }
         if (ImGui::IsItemActive() && ((ImGui::GetIO().KeyShift && ImGui::IsMouseDown(0)) || GestureRecorder::Instance().IsArmed(nodeIndex, paramIndex)))
            GestureRecorder::Instance().NotifyMovement(nodeIndex, paramIndex, *value, GestureRecorder::Instance().RecordClockNow(), /*isNewGrab=*/justActivated);
         if (ImGui::IsItemDeactivated())
            GestureRecorder::Instance().MaybeFinishArmedRecording(nodeIndex, paramIndex, GestureRecorder::Instance().RecordClockNow());
         // Double-click (or hovering and typing a digit/'='/etc below) still
         // opens the typed-entry field even while locked - that's a deliberate
         // "replace this" action, not an accidental grab, and its own commit
         // path (above) already calls StopPlayback when a literal value wins;
         // typing '=' hands it to an expression instead, which then takes
         // precedence over the recording in the apply loop regardless. Only
         // the plain click-drag above stays locked.
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/false);
         // Right-click opens the param's context menu (Enter Value/Enter
         // Expression/Start Recording, or the recording/expression-specific
         // one - see the popup body near gOpenModBindingMenu) instead of
         // jumping straight into typing - and marks the click consumed so the
         // node-level right-click context menu doesn't also try to open over it.
         DrawModulationBindingMenu(nodeIndex, paramIndex, ImGui::IsItemHovered());
         // Hovering (not dragging) and pressing a digit/'-'/'.'/'=' starts a
         // fresh typed value (or expression) immediately, without needing to
         // double-click first. IsItemActive() guards against a mouse-drag
         // coinciding with a keypress from stealing the drag into typing mode.
         if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }

      if (ParamMidiLearnIsActiveFor(nodeIndex, paramIndex))
         StateRing::Draw(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), StateRing::Kind::Learn, isLight, tok::radius_field);
      changed = KbParamHook(nodeIndex, paramIndex, value, minV, maxV, step, fmt, ImGui::GetItemRectMin(),
                          ImVec2(std::min(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().x + (width - box - 4.0f)), ImGui::GetItemRectMax().y),
                          false) || changed;
      ImGui::PopID();
      return changed;
   }


   bool ModSliderInt(const char* label, int* value, int minV, int maxV, float width,
                     bool audioStyle)
   {
      const std::pair<int, int> key(gCurrentNodeIndex, gParamCounter);
      float& slot = gIntParamStore[key];
      if (!Modulation::Instance().IsModulated(key.first, key.second))
      {
         auto last = gIntParamLastWritten.find(key);
         if (last == gIntParamLastWritten.end() || last->second != *value)
            slot = (float)*value;
      }

      gPendingSrcAddr = value;
      bool changed = ModSlider(label, &slot, (float)minV, (float)maxV, "%.0f", width, audioStyle, /*step=*/1.0f);
      // lroundf, not (int)(x + 0.5f): the latter truncates toward zero, so
      // -3.7 lands on -3 instead of -4 and every negative-range param (octave,
      // semi, transpose) is biased upward by half a step. Clamp after
      // rounding, matching ModKnobInt below - a typed value or an unclamped
      // modulator source (see ShapeToParam) can otherwise land the backing
      // float past minV/maxV.
      slot = std::clamp(slot, (float)minV, (float)maxV);
      *value = (int)lroundf(slot);
      gIntParamLastWritten[key] = *value;
      return changed;
   }


   // ---- rotary knob (audio nodes' Tier 1 grid) ----------------------------
   // Raw draw+interaction primitive for a knob, built on InvisibleButton
   // rather than any ImGui slider. It still leaves the normal
   // IsItemHovered/IsItemActive/IsItemActivated query surface behind it,
   // which is what lets ModKnob below reuse ModSlider's pin/typing/
   // expression/hotkey logic completely unchanged - only the widget in the
   // middle of that logic is different.
   //
   // `cellW` is the width of the layout cell this knob sits in (see
   // AudioKnobRow): the knob and its caption are centred in that cell, so a
   // row of N knobs is exactly bodyWidth wide by construction rather than
   // however wide N knobs happen to add up to (v3 §3b). cellW <= 0 means
   // "size to the knob", the pre-v3 behaviour.
   // Which physical control a Tier-1 param is drawn as. The knob is the
   // default and covers most params; the fader exists because a mixer's
   // channel gains are the one place where *comparing* several values at a
   // glance matters more than setting any one of them precisely - which is
   // exactly why every mixing desk ever built uses faders there and knobs
   // everywhere else. A row of 8 knobs cannot be read as a balance; a row of
   // 8 faders is read as one by shape alone.

   // Visual width the fader occupies inside its cell (track plus the cap's
   // overhang). The interactive rect matches, for the same reason KnobFloat's
   // does: the cell's side margins belong to ModKnob's modulation pin.
   const float kFaderWidth = 22.0f;


   // Vertical fader with KnobFloat's exact contract - draws at the cursor,
   // reserves (cell, rowH), leaves IsItemHovered/IsItemActive queryable, fills
   // the readout strip on hover - so ModKnob can drive either one without
   // knowing which it has.
   bool VFaderFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                    float height, ImU32 fillColor, bool readOnly, float cellW,
                    FaderPosToValueFn posToValue, FaderValueToPosFn valueToPos,
                    bool hasRange, float rangeLo, float rangeHi,
                    int gestureNodeIndex, int gestureParamIndex)
   {
      const float cell = cellW > 0.0f ? cellW : kFaderWidth;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float textH = ImGui::GetTextLineHeight();
      const float rowH = height + 4.0f + textH;

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

      ImGui::SetCursorScreenPos(ImVec2(p.x + (cell - kFaderWidth) * 0.5f, p.y));
      ImGui::InvisibleButton(label, ImVec2(kFaderWidth, height));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = !readOnly && ImGui::IsItemActive();
      bool changed = false;
      const bool gestureJustActivated = ImGui::IsItemActivated();
      if (gestureNodeIndex >= 0 && gestureJustActivated)
         GestureRecorder::Instance().StopPlayback(gestureNodeIndex, gestureParamIndex);
      if (active && ImGui::GetIO().MouseDelta.y != 0.0f)
      {
         // Travel is the fader's own length, not a fixed 200px as on the
         // knob: a fader that doesn't track the cap under the cursor reads as
         // spongy, because the eye can compare the cap to the cursor in the
         // same visual axis.
         // Shift used to slow the drag down for precision; it now triggers
         // gesture recording instead (see GestureRecorder), and the two
         // fought each other - holding Shift to record made the drag feel
         // stuck. Always full speed.
         const float speed = 1.0f;
         const float posNow = ValueToPos01(*value);
         const float nextPos = std::clamp(posNow - ImGui::GetIO().MouseDelta.y * speed / height, 0.0f, 1.0f);
         const float next = Pos01ToValue(nextPos);
         if (next != *value)
         {
            *value = next;
            changed = true;
         }
      }
      if (active && gestureNodeIndex >= 0 && ((ImGui::GetIO().KeyShift && ImGui::IsMouseDown(0)) || GestureRecorder::Instance().IsArmed(gestureNodeIndex, gestureParamIndex)))
         GestureRecorder::Instance().NotifyMovement(gestureNodeIndex, gestureParamIndex, *value, GestureRecorder::Instance().RecordClockNow(), /*isNewGrab=*/gestureJustActivated);
      if (gestureNodeIndex >= 0 && ImGui::IsItemDeactivated())
         GestureRecorder::Instance().MaybeFinishArmedRecording(gestureNodeIndex, gestureParamIndex, GestureRecorder::Instance().RecordClockNow());
      if (gestureNodeIndex >= 0 && GestureRecorder::Instance().IsRecording(gestureNodeIndex, gestureParamIndex))
         fillColor = tok::U32(tok::pal::c_EB4646FF);

      const float cx = p.x + cell * 0.5f;
      const float top = p.y + 4.0f;
      const float bottom = p.y + height - 4.0f;
      const float t = ValueToPos01(*value);
      const float capY = bottom - t * (bottom - top);
      const float capYLo = hasRange ? bottom - ValueToPos01(rangeLo) * (bottom - top) : 0.0f;
      const float capYHi = hasRange ? bottom - ValueToPos01(rangeHi) * (bottom - top) : 0.0f;

      const bool isLight = IsThemeLight();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      float tickY[ConsoleFaderTaper::kNumDetents > 5 ? ConsoleFaderTaper::kNumDetents : 5];
      int nTicks = 0;
      if (valueToPos == nullptr)
         for (int i = 0; i <= 4; i++)
            tickY[nTicks++] = bottom - (float)i * 0.25f * (bottom - top);
      else
         for (int i = 0; i < ConsoleFaderTaper::kNumDetents; i++)
            tickY[nTicks++] = bottom - std::clamp(valueToPos(ConsoleFaderTaper::kDetentsDb[i], minV, maxV), 0.0f, 1.0f) * (bottom - top);
      VFader::Draw(dl, cx, top, bottom, capY, t > 0.0f, fillColor, tickY, nTicks, hasRange, capYLo, capYHi,
                   hovered, active, readOnly, isLight);

      // Caption below, same baseline rule as the knob's.
      const char* caption = label[0] == '#' ? "" : label;
      const ImVec2 textSize = ImGui::CalcTextSize(caption);
      const ImU32 capTextCol = isLight
         ? (readOnly ? tok::U32(tok::pal::c_7D8291FF) : tok::U32(tok::pal::c_323746FF))
         : (readOnly ? tok::U32(tok::pal::c_8C8C96FF) : tok::U32(tok::pal::c_B0B6C6FF));
      const float capTextY = p.y + height + 4.0f;
      dl->PushClipRect(ImVec2(p.x, capTextY), ImVec2(p.x + cell, capTextY + textH), true);
      dl->AddText(ImVec2(cx - textSize.x * 0.5f, capTextY), capTextCol, caption);
      dl->PopClipRect();

      if (hovered || active)
      {
         char buf[48];
         FormatAudioParam(buf, sizeof(buf), fmt, *value);
         SetAudioReadout(caption, buf);
      }

      ImGui::SetCursorScreenPos(p);
      // ItemSize, not Dummy: Dummy is ItemSize *plus* ItemAdd(bb, 0), and
      // that ItemAdd overwrites g.LastItemData with a zero id - so every
      // IsItemActivated/IsItemActive/IsItemHovered a caller runs after this
      // widget returns was querying the spacer, not the InvisibleButton
      // above. IsItemActive() in particular is false for an id-0 item even
      // mid-drag, which silently killed ModKnob's whole post-draw block:
      // no undo checkpoint on grab, no GestureRecorder::StopPlayback, and
      // no NotifyMovement - so Shift+drag never started a recording and the
      // knob never went red (the EQ band freq/Q/gain knobs, and every other
      // ModKnob/ModKnob-fader in the app). ImGui::SliderFloat leaves its own
      // item last, which is why ModSlider's identical block always worked.
      // ItemSize advances the layout cursor exactly as Dummy did and leaves
      // the InvisibleButton's item data standing.
      ImGui::ItemSize(ImVec2(cell, rowH));
      return changed;
   }


   bool KnobFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                  float diameter, ImU32 fillColor, bool readOnly, float cellW,
                  FaderPosToValueFn posToValue, FaderValueToPosFn valueToPos,
                  bool hasRange, float rangeLo, float rangeHi,
                  bool activeTint)
   {
      const float kTwoPi = 6.28318530717958647692f;
      const float aMin = 0.75f * kTwoPi * 0.5f; // 135 deg
      const float aMax = 2.25f * kTwoPi * 0.5f; // 405 deg (270 deg sweep)

      const float cell = cellW > 0.0f ? cellW : diameter;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const char* caption = label[0] == '#' ? "" : label;
      const float textH = (caption[0] == '\0') ? 0.0f : ImGui::GetTextLineHeight();
      const float rowH = (caption[0] == '\0') ? diameter : (diameter + 4.0f + textH);

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

      // The interactive rect is the knob itself, centred in the cell - not
      // the whole cell. The cell's side margins have to stay free of this
      // widget's drag handling because that is where ModKnob puts the
      // modulation pin (v3 §3b), and a drag starting on a pin belongs to the
      // node editor's link machinery, not to the knob.
      ImGui::SetCursorScreenPos(ImVec2(p.x + (cell - diameter) * 0.5f, p.y));
      ImGui::InvisibleButton(label, ImVec2(diameter, rowH));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = !readOnly && ImGui::IsItemActive();
      bool changed = false;
      if (active && ImGui::GetIO().MouseDelta.y != 0.0f)
      {
         // Shift used to slow the drag down for precision; it now triggers
         // gesture recording instead (see GestureRecorder), and the two
         // fought each other - holding Shift to record made the drag feel
         // stuck. Always full speed.
         const float speed = 1.0f;
         const float posNow = ValueToPos01(*value);
         const float nextPos = std::clamp(posNow - ImGui::GetIO().MouseDelta.y * speed / 200.0f, 0.0f, 1.0f);
         const float next = Pos01ToValue(nextPos);
         if (next != *value)
         {
            *value = next;
            changed = true;
         }
      }

      // Centred in the cell, not left-aligned to it - that centring is what
      // makes a row of mixed large/small knobs read as a rack strip instead
      // of a ragged left-packed list.
      const ImVec2 center(p.x + cell * 0.5f, p.y + diameter * 0.5f);
      const float radius = diameter * 0.5f - 2.0f;
      const float t = ValueToPos01(*value);
      const float angle = aMin + t * (aMax - aMin);
      // Modulation range band (item 6): the lo..hi span this knob is bound
      // to, drawn as a dim arc under the value fill - Bitwig/Ableton-style.
      // Only ever passed when the knob is read-only-modulated (see ModKnob).
      const float angleLo = hasRange ? aMin + ValueToPos01(rangeLo) * (aMax - aMin) : 0.0f;
      const float angleHi = hasRange ? aMin + ValueToPos01(rangeHi) * (aMax - aMin) : 0.0f;

      const bool isLight = IsThemeLight();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (isLight)
      {
         // Soft under-knob shadow: a couple of low-alpha, downward-offset
         // circles drawn before the bezel fill approximate a blurred drop
         // shadow (ImGui has no native blur) - same layering idiom as the
         // hover ring below, just larger radius / lower alpha / offset down.
         // Kept dark even in light mode so it still reads as a shadow, not
         // a glow.
         const ImVec2 shadowCenter(center.x, center.y + 1.5f);
         dl->AddCircleFilled(shadowCenter, radius + 2.0f, tok::U32(tok::pal::c_1E202810), 32);
         dl->AddCircleFilled(shadowCenter, radius + 0.75f, tok::U32(tok::pal::c_1E202816), 32);
         dl->AddCircleFilled(center, radius, tok::U32(tok::pal::c_DCE0EAFF), 32);
         dl->AddCircleFilled(ImVec2(center.x, center.y - 0.5f), radius - 3.0f, tok::U32(tok::pal::c_F2F5FAFF), 32);
         dl->PathArcTo(center, radius + 2.5f, aMin, aMax, 32);
         // The guide ring covers the knob's whole travel, not just the
         // value-proportional fill arc below - tinting it when recording is
         // what makes the state readable at a glance regardless of where the
         // value happens to sit (a knob near its low end barely shows any
         // fill arc at all otherwise).
         dl->PathStroke(activeTint ? tok::U32(tok::pal::c_DC5A5AFF) : tok::U32(tok::pal::c_C3C8D4FF), 0, 3.0f);
         if (hasRange && std::fabs(angleHi - angleLo) > 1e-4f)
         {
            const ImU32 rangeCol = (fillColor & 0x00FFFFFF) | 0x70000000;
            dl->PathArcTo(center, radius + 2.5f, std::min(angleLo, angleHi), std::max(angleLo, angleHi), 32);
            dl->PathStroke(rangeCol, 0, 5.0f);
         }
         if (t > 0.0f)
         {
            dl->PathArcTo(center, radius + 2.5f, aMin, angle, 32);
            dl->PathStroke(fillColor, 0, 3.0f);
         }
         const ImVec2 tipIn(center.x + cosf(angle) * (radius * 0.35f), center.y + sinf(angle) * (radius * 0.35f));
         const ImVec2 tipOut(center.x + cosf(angle) * (radius - 3.0f), center.y + sinf(angle) * (radius - 3.0f));
         dl->AddLine(tipIn, tipOut, readOnly ? tok::U32(tok::pal::c_8C91A0FF) : tok::U32(tok::pal::c_282D3CFF), 2.0f);
         dl->AddCircle(center, radius, activeTint ? tok::U32(tok::pal::c_DC5A5AFF) : tok::U32(tok::pal::c_AFB4C3FF), 32, 1.0f);
         if (hovered && !readOnly)
            dl->AddCircle(center, radius + 2.5f, tok::U32(tok::pal::c_0000001E), 32, 3.0f);
      }
      else
      {
         // Dark theme: the panel behind a knob is already near-black, so the
         // shadow needs a touch more alpha than the light-theme version to
         // still separate the bezel from the body - same two-layer fake-blur
         // idiom, tuned darker/stronger for this background.
         const ImVec2 shadowCenter(center.x, center.y + 1.5f);
         dl->AddCircleFilled(shadowCenter, radius + 2.0f, tok::U32(tok::pal::c_0000002D), 32);
         dl->AddCircleFilled(shadowCenter, radius + 0.75f, tok::U32(tok::pal::c_0000003C), 32);
         dl->AddCircleFilled(center, radius, tok::U32(tok::pal::c_242630FF), 32);
         dl->AddCircleFilled(ImVec2(center.x, center.y - 0.5f), radius - 3.0f, tok::U32(tok::pal::c_16171EFF), 32);
         dl->PathArcTo(center, radius + 2.5f, aMin, aMax, 32);
         dl->PathStroke(activeTint ? tok::U32(tok::pal::c_DC5A5AFF) : tok::U32(tok::pal::c_3A3E4CFF), 0, 3.0f);
         if (hasRange && std::fabs(angleHi - angleLo) > 1e-4f)
         {
            const ImU32 rangeCol = (fillColor & 0x00FFFFFF) | 0x82000000;
            dl->PathArcTo(center, radius + 2.5f, std::min(angleLo, angleHi), std::max(angleLo, angleHi), 32);
            dl->PathStroke(rangeCol, 0, 5.0f);
         }
         if (t > 0.0f)
         {
            dl->PathArcTo(center, radius + 2.5f, aMin, angle, 32);
            dl->PathStroke(fillColor, 0, 3.0f);
         }
         const ImVec2 tipIn(center.x + cosf(angle) * (radius * 0.35f), center.y + sinf(angle) * (radius * 0.35f));
         const ImVec2 tipOut(center.x + cosf(angle) * (radius - 3.0f), center.y + sinf(angle) * (radius - 3.0f));
         dl->AddLine(tipIn, tipOut, readOnly ? tok::U32(tok::pal::c_C8CAD4FF) : tok::U32(tok::pal::c_EEF0F8FF), 2.0f);
         dl->AddCircle(center, radius, activeTint ? tok::U32(tok::pal::c_DC5A5AFF) : tok::U32(tok::pal::c_4A4E5EFF), 32, 1.0f);
         if (hovered && !readOnly)
            dl->AddCircle(center, radius + 2.5f, tok::U32(tok::pal::c_FFFFFF28), 32, 3.0f);
      }

      // The knob's permanent caption is the param *name*, not its value - a
      // hardware knob doesn't print a live number on its cap either. The
      // exact value goes to the node's readout strip on hover/drag (see
      // SetAudioReadout), and is always reachable precisely via
      // double-click/right-click-to-type (see ModKnob).
      if (caption[0] != '\0')
      {
         // Clip rather than let a long caption widen the cell and break the
         // row's fit-to-body-width guarantee.
         ImVec2 textSize = ImGui::CalcTextSize(caption);
         const ImU32 capCol = isLight
            ? (readOnly ? tok::U32(tok::pal::c_7D8291FF) : tok::U32(tok::pal::c_323746FF))
            : (readOnly ? tok::U32(tok::pal::c_8C8C96FF) : tok::U32(tok::pal::c_B0B6C6FF));
         const float capY = p.y + diameter + 4.0f;
         if (textSize.x <= cell)
         {
            dl->AddText(ImVec2(center.x - textSize.x * 0.5f, capY), capCol, caption);
         }
         else
         {
            dl->PushClipRect(ImVec2(p.x, capY), ImVec2(p.x + cell, capY + textH), true);
            dl->AddText(ImVec2(p.x, capY), capCol, caption);
            dl->PopClipRect();
         }
      }

      if (hovered || active)
      {
         char buf[48];
         FormatAudioParam(buf, sizeof(buf), fmt, *value);
         SetAudioReadout(caption, buf);
      }

      // Reserve the full cell so a caller that just chains widgets normally
      // still advances by a cell, while a row layout (AudioKnobRow) that
      // positions each cell absolutely gets the correct row height from the
      // last cell it draws.
      ImGui::SetCursorScreenPos(p);
      // ItemSize, not Dummy: Dummy is ItemSize *plus* ItemAdd(bb, 0), and
      // that ItemAdd overwrites g.LastItemData with a zero id - so every
      // IsItemActivated/IsItemActive/IsItemHovered a caller runs after this
      // widget returns was querying the spacer, not the InvisibleButton
      // above. IsItemActive() in particular is false for an id-0 item even
      // mid-drag, which silently killed ModKnob's whole post-draw block:
      // no undo checkpoint on grab, no GestureRecorder::StopPlayback, and
      // no NotifyMovement - so Shift+drag never started a recording and the
      // knob never went red (the EQ band freq/Q/gain knobs, and every other
      // ModKnob/ModKnob-fader in the app). ImGui::SliderFloat leaves its own
      // item last, which is why ModSlider's identical block always worked.
      // ItemSize advances the layout cursor exactly as Dummy did and leaves
      // the InvisibleButton's item data standing.
      ImGui::ItemSize(ImVec2(cell, rowH));
      return changed;
   }


   bool BipolarKnobFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                         float diameter, ImU32 fillColor, bool readOnly, float cellW,
                         int gestureNodeIndex, int gestureParamIndex,
                         bool hasRange, float rangeLo, float rangeHi,
                         bool activeTint, bool resetOnDoubleClick)
   {
      const float kTwoPi = 6.28318530717958647692f;
      const float aMin = 0.75f * kTwoPi * 0.5f; // 135 deg
      const float aMax = 2.25f * kTwoPi * 0.5f; // 405 deg
      const float aMid = 1.50f * kTwoPi * 0.5f; // 270 deg (12 o'clock center)

      const float cell = cellW > 0.0f ? cellW : diameter;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const char* caption = label[0] == '#' ? "" : label;
      const float textH = (caption[0] == '\0') ? 0.0f : ImGui::GetTextLineHeight();
      const float rowH = (caption[0] == '\0') ? diameter : (diameter + 4.0f + textH);

      auto ValueToPos01 = [&](float v) -> float {
         return (maxV > minV) ? std::clamp((v - minV) / (maxV - minV), 0.0f, 1.0f) : 0.5f;
      };
      auto Pos01ToValue = [&](float pos) -> float {
         return std::clamp(minV + (maxV - minV) * pos, minV, maxV);
      };

      ImGui::SetCursorScreenPos(ImVec2(p.x + (cell - diameter) * 0.5f, p.y));
      ImGui::InvisibleButton(label, ImVec2(diameter, rowH));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = !readOnly && ImGui::IsItemActive();
      bool changed = false;
      const bool gestureJustActivated = ImGui::IsItemActivated();
      if (gestureNodeIndex >= 0 && gestureJustActivated)
         GestureRecorder::Instance().StopPlayback(gestureNodeIndex, gestureParamIndex);

      // Double-click resets to center when enabled
      if (resetOnDoubleClick && hovered && !readOnly && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         *value = (minV + maxV) * 0.5f;
         changed = true;
      }
      else if (active && ImGui::GetIO().MouseDelta.y != 0.0f)
      {
         // Shift used to slow the drag down for precision; it now triggers
         // gesture recording instead (see GestureRecorder), and the two
         // fought each other - holding Shift to record made the drag feel
         // stuck. Always full speed.
         const float speed = 1.0f;
         const float posNow = ValueToPos01(*value);
         const float nextPos = std::clamp(posNow - ImGui::GetIO().MouseDelta.y * speed / 200.0f, 0.0f, 1.0f);
         const float next = Pos01ToValue(nextPos);
         if (next != *value)
         {
            *value = next;
            changed = true;
         }
      }
      if (active && gestureNodeIndex >= 0 && ((ImGui::GetIO().KeyShift && ImGui::IsMouseDown(0)) || GestureRecorder::Instance().IsArmed(gestureNodeIndex, gestureParamIndex)))
         GestureRecorder::Instance().NotifyMovement(gestureNodeIndex, gestureParamIndex, *value, GestureRecorder::Instance().RecordClockNow(), /*isNewGrab=*/gestureJustActivated);
      if (gestureNodeIndex >= 0 && ImGui::IsItemDeactivated())
         GestureRecorder::Instance().MaybeFinishArmedRecording(gestureNodeIndex, gestureParamIndex, GestureRecorder::Instance().RecordClockNow());
      if (gestureNodeIndex >= 0 && GestureRecorder::Instance().IsRecording(gestureNodeIndex, gestureParamIndex))
         fillColor = tok::U32(tok::pal::c_EB4646FF);

      const ImVec2 center(p.x + cell * 0.5f, p.y + diameter * 0.5f);
      const float radius = diameter * 0.5f - 2.0f;
      const float t = ValueToPos01(*value);
      const float angle = aMin + t * (aMax - aMin);
      const float angleLo = hasRange ? aMin + ValueToPos01(rangeLo) * (aMax - aMin) : 0.0f;
      const float angleHi = hasRange ? aMin + ValueToPos01(rangeHi) * (aMax - aMin) : 0.0f;

      const bool isLight = IsThemeLight();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (isLight)
      {
         // Soft under-knob shadow - see KnobFloat's identical treatment
         // above for the rationale (fake-blur via layered low-alpha circles,
         // offset down so the bezel reads as sitting above the panel).
         const ImVec2 shadowCenter(center.x, center.y + 1.5f);
         dl->AddCircleFilled(shadowCenter, radius + 2.0f, tok::U32(tok::pal::c_1E202810), 32);
         dl->AddCircleFilled(shadowCenter, radius + 0.75f, tok::U32(tok::pal::c_1E202816), 32);
         dl->AddCircleFilled(center, radius, tok::U32(tok::pal::c_DCE0EAFF), 32);
         dl->AddCircleFilled(ImVec2(center.x, center.y - 0.5f), radius - 3.0f, tok::U32(tok::pal::c_F2F5FAFF), 32);
         dl->PathArcTo(center, radius + 2.5f, aMin, aMax, 32);
         dl->PathStroke(activeTint ? tok::U32(tok::pal::c_DC5A5AFF) : tok::U32(tok::pal::c_C3C8D4FF), 0, 3.0f);
         if (hasRange && std::fabs(angleHi - angleLo) > 1e-4f)
         {
            const ImU32 rangeCol = (fillColor & 0x00FFFFFF) | 0x70000000;
            dl->PathArcTo(center, radius + 2.5f, std::min(angleLo, angleHi), std::max(angleLo, angleHi), 32);
            dl->PathStroke(rangeCol, 0, 5.0f);
         }

         // Center tick
         dl->AddLine(ImVec2(center.x, center.y - radius - 5.0f), ImVec2(center.x, center.y - radius), tok::U32(tok::pal::c_969BAAFF), 1.5f);

         // Arc from center (12 o'clock) to current angle
         if (angle < aMid)
         {
            dl->PathArcTo(center, radius + 2.5f, angle, aMid, 32);
            dl->PathStroke(fillColor, 0, 3.0f);
         }
         else if (angle > aMid)
         {
            dl->PathArcTo(center, radius + 2.5f, aMid, angle, 32);
            dl->PathStroke(fillColor, 0, 3.0f);
         }

         const ImVec2 tipIn(center.x + cosf(angle) * (radius * 0.35f), center.y + sinf(angle) * (radius * 0.35f));
         const ImVec2 tipOut(center.x + cosf(angle) * (radius - 3.0f), center.y + sinf(angle) * (radius - 3.0f));
         dl->AddLine(tipIn, tipOut, readOnly ? tok::U32(tok::pal::c_8C91A0FF) : tok::U32(tok::pal::c_282D3CFF), 2.0f);
         dl->AddCircle(center, radius, activeTint ? tok::U32(tok::pal::c_DC5A5AFF) : tok::U32(tok::pal::c_AFB4C3FF), 32, 1.0f);
         if (hovered && !readOnly)
            dl->AddCircle(center, radius + 2.5f, tok::U32(tok::pal::c_0000001E), 32, 3.0f);
      }
      else
      {
         // Dark theme: slightly stronger alpha than the light version, same
         // rationale as KnobFloat's dark branch above.
         const ImVec2 shadowCenter(center.x, center.y + 1.5f);
         dl->AddCircleFilled(shadowCenter, radius + 2.0f, tok::U32(tok::pal::c_0000002D), 32);
         dl->AddCircleFilled(shadowCenter, radius + 0.75f, tok::U32(tok::pal::c_0000003C), 32);
         dl->AddCircleFilled(center, radius, tok::U32(tok::pal::c_242630FF), 32);
         dl->AddCircleFilled(ImVec2(center.x, center.y - 0.5f), radius - 3.0f, tok::U32(tok::pal::c_16171EFF), 32);
         dl->PathArcTo(center, radius + 2.5f, aMin, aMax, 32);
         dl->PathStroke(activeTint ? tok::U32(tok::pal::c_DC5A5AFF) : tok::U32(tok::pal::c_3A3E4CFF), 0, 3.0f);
         if (hasRange && std::fabs(angleHi - angleLo) > 1e-4f)
         {
            const ImU32 rangeCol = (fillColor & 0x00FFFFFF) | 0x82000000;
            dl->PathArcTo(center, radius + 2.5f, std::min(angleLo, angleHi), std::max(angleLo, angleHi), 32);
            dl->PathStroke(rangeCol, 0, 5.0f);
         }

         // Center tick
         dl->AddLine(ImVec2(center.x, center.y - radius - 5.0f), ImVec2(center.x, center.y - radius), tok::U32(tok::pal::c_646978FF), 1.5f);

         // Arc from center (12 o'clock) to current angle
         if (angle < aMid)
         {
            dl->PathArcTo(center, radius + 2.5f, angle, aMid, 32);
            dl->PathStroke(fillColor, 0, 3.0f);
         }
         else if (angle > aMid)
         {
            dl->PathArcTo(center, radius + 2.5f, aMid, angle, 32);
            dl->PathStroke(fillColor, 0, 3.0f);
         }

         const ImVec2 tipIn(center.x + cosf(angle) * (radius * 0.35f), center.y + sinf(angle) * (radius * 0.35f));
         const ImVec2 tipOut(center.x + cosf(angle) * (radius - 3.0f), center.y + sinf(angle) * (radius - 3.0f));
         dl->AddLine(tipIn, tipOut, readOnly ? tok::U32(tok::pal::c_787D8CFF) : tok::U32(tok::pal::c_E6EBF5FF), 2.0f);
         dl->AddCircle(center, radius, activeTint ? tok::U32(tok::pal::c_DC5A5AFF) : tok::U32(tok::pal::c_4A4E5EFF), 32, 1.0f);
         if (hovered && !readOnly)
            dl->AddCircle(center, radius + 2.5f, tok::U32(tok::pal::c_FFFFFF28), 32, 3.0f);
      }

      // rowH has always reserved room for a caption here; nothing ever drew
      // one, so a bipolar knob sat in a nameless gap while every plain knob
      // beside it was labelled. Same treatment as KnobFloat's caption.
      if (caption[0] != '\0')
      {
         const ImVec2 textSize = ImGui::CalcTextSize(caption);
         const ImU32 capCol = isLight
            ? (readOnly ? tok::U32(tok::pal::c_7D8291FF) : tok::U32(tok::pal::c_323746FF))
            : (readOnly ? tok::U32(tok::pal::c_8C8C96FF) : tok::U32(tok::pal::c_B0B6C6FF));
         const float capY = p.y + diameter + 4.0f;
         if (textSize.x <= cell)
         {
            dl->AddText(ImVec2(center.x - textSize.x * 0.5f, capY), capCol, caption);
         }
         else
         {
            dl->PushClipRect(ImVec2(p.x, capY), ImVec2(p.x + cell, capY + textH), true);
            dl->AddText(ImVec2(p.x, capY), capCol, caption);
            dl->PopClipRect();
         }
      }

      if (hovered || active)
      {
         char buf[48];
         snprintf(buf, sizeof(buf), fmt, *value);
         SetAudioReadout(caption, buf);
      }

      // ItemSize, not Dummy - see KnobFloat's identical comment above.
      // Dummy's ItemAdd(bb, 0) overwrites g.LastItemData with a zero id, so a
      // caller's IsItemActive()/IsItemHovered() right after this function
      // returns (DrawPerfElement's dragHold(), in particular) always saw a
      // dead id-0 item and erased the held raw drag value every frame the
      // knob was mid-drag - which read as the bipolar knob "not responding".
      ImGui::SetCursorScreenPos(p);
      ImGui::ItemSize(ImVec2(cell, rowH));
      return changed;
   }


   // Knob counterpart of ModSlider - same pin/typing/expression/undo/hotkey
   // behaviour (copied verbatim), just a KnobFloat in place of each
   // SliderFloat. Kept as a full copy rather than a shared helper because the
   // two widgets' read-only/interactive branches differ in exactly which
   // extra decorations (fx badge, x button) they draw around the control,
   // and threading a widget-drawing callback through that would obscure more
   // than it'd save.
   // `diameter` is the knob's diameter, or the fader's *height* when
   // style is VFader - in both cases the widget's vertical extent, which is
   // all the pin placement below actually needs from it.
   bool ModKnob(const char* label, float* value, float minV, float maxV, const char* fmt,
                float diameter, float cellW,
                AudioWidgetStyle style, float step,
                FaderPosToValueFn posToValue, FaderValueToPosFn valueToPos,
                int explicitParamIndex, const char* nameOverride)
   {
      FaderPosToValueFn p2v = posToValue;
      FaderValueToPosFn v2p = valueToPos;
      if (!p2v)
      {
         switch (style)
         {
         case AudioWidgetStyle::VFaderDb:
         case AudioWidgetStyle::KnobDb:
            p2v = ConsoleFaderTaper::PosToValue;
            v2p = ConsoleFaderTaper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobFreq:
         case AudioWidgetStyle::KnobLog:
            p2v = FrequencyTaper::PosToValue;
            v2p = FrequencyTaper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewAttack100:
            p2v = SkewAttack100Taper::PosToValue;
            v2p = SkewAttack100Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewDecay300:
            p2v = SkewDecay300Taper::PosToValue;
            v2p = SkewDecay300Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewRelease400:
            p2v = SkewRelease400Taper::PosToValue;
            v2p = SkewRelease400Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewDelay250:
            p2v = SkewDelay250Taper::PosToValue;
            v2p = SkewDelay250Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewGlide150:
            p2v = SkewGlide150Taper::PosToValue;
            v2p = SkewGlide150Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewGlide100:
            p2v = SkewGlide100Taper::PosToValue;
            v2p = SkewGlide100Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewStrum30:
            p2v = SkewStrum30Taper::PosToValue;
            v2p = SkewStrum30Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewStrum20:
            p2v = SkewStrum20Taper::PosToValue;
            v2p = SkewStrum20Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewFreqShifter12:
            p2v = SkewFreqShifter12Taper::PosToValue;
            v2p = SkewFreqShifter12Taper::ValueToPos;
            break;
         case AudioWidgetStyle::KnobSkewStereoBass120:
            p2v = SkewStereoBass120Taper::PosToValue;
            v2p = SkewStereoBass120Taper::ValueToPos;
            break;
         default:
            break;
         }
      }

      auto DrawWidget = [&](float* v, ImU32 col, bool readOnly, bool hasRange = false, float rangeLo = 0.0f, float rangeHi = 0.0f, bool activeTint = false) -> bool
      {
         const bool isVFader = (style == AudioWidgetStyle::VFader || style == AudioWidgetStyle::VFaderDb);
         const bool isBipolar = (style == AudioWidgetStyle::KnobBipolar);
         if (isVFader)
            return VFaderFloat(label, v, minV, maxV, fmt, diameter, col, readOnly, cellW > 0.0f ? cellW : 0.0f,
                               p2v, v2p, hasRange, rangeLo, rangeHi);
         if (isBipolar)
            return BipolarKnobFloat(label, v, minV, maxV, fmt, diameter, col, readOnly, cellW > 0.0f ? cellW : 0.0f,
                                    /*gestureNodeIndex=*/-1, /*gestureParamIndex=*/-1,
                                    hasRange, rangeLo, rangeHi, activeTint, /*resetOnDoubleClick=*/false);
         return KnobFloat(label, v, minV, maxV, fmt, diameter, col, readOnly, cellW > 0.0f ? cellW : 0.0f,
                          p2v, v2p, hasRange, rangeLo, rangeHi, activeTint);
      };
      const float widgetW = (style == AudioWidgetStyle::VFader || style == AudioWidgetStyle::VFaderDb)
                               ? kFaderWidth : diameter;

      const int nodeIndex = gCurrentNodeIndex;
      const int paramIndex = (explicitParamIndex >= 0) ? explicitParamIndex : gParamCounter++;

      ParamRef ref;
      ref.nodeIndex = nodeIndex;
      ref.paramIndex = paramIndex;
      ref.value = value;
      ref.minValue = minV;
      ref.maxValue = maxV;
      ref.step = step;
      // `label` is also this knob's drawn caption, so a node with several
      // instances of the same control (the EQ's five bands' `freq`) cannot
      // disambiguate them by widening the caption without breaking the row.
      // nameOverride names the *param* - what the modulation matrix and the
      // binding menu show - independently of what the cap says.
      ref.name = (nameOverride != nullptr) ? nameOverride : label;
      ref.posToValue = p2v;
      ref.valueToPos = v2p;
      ref.srcAddr = TakePendingSrcAddr(value);
      Modulation::Instance().RegisterParam(ref);
      if (gParamRegisterOnly)
         return false; // registered, deliberately not drawn - see gParamRegisterOnly

      const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kParamBase + paramIndex;
      const bool modulated = Modulation::Instance().IsModulated(nodeIndex, paramIndex);
      const bool hasExpr = !modulated && Modulation::Instance().HasExpression(nodeIndex, paramIndex);
      const std::string* hasExprErr = hasExpr ? Modulation::Instance().ExpressionErrorFor(nodeIndex, paramIndex)
                                               : nullptr;
      const bool exprErrored = hasExprErr != nullptr && !hasExprErr->empty();
      const bool recording = !modulated && GestureRecorder::Instance().IsRecording(nodeIndex, paramIndex);
      gDrawnParamPins.insert(pinId);

      ImGui::PushID(paramIndex + 5000);

      // v2 put this pin *before* the knob on the same line, costing 18px of
      // horizontal row budget per knob (108px on Oscillator's six-control
      // row - a third of why that row overflowed kAudioNodeWidth) and
      // leaving a line of dots floating above the knob caps because a 14px
      // pin and a 70px knob baseline-align to different centres (v3 §1g).
      // v3 draws the knob first and tucks the pin into the cell's own left
      // margin afterwards, so it costs no row width at all.
      const ImVec2 cellOrigin = ImGui::GetCursorScreenPos();
      const float cell = cellW > 0.0f ? cellW : diameter;
      const PinDot::State pinState = modulated && IsPredictionBinding(nodeIndex, paramIndex) ? PinDot::State::Prediction
                                    : modulated               ? PinDot::State::Modulated
                                    : hasExpr && !exprErrored ? PinDot::State::Expression
                                                              : PinDot::State::Idle;
      const ImU32 pinColor = PinDot::Colour(pinState, IsThemeLight());

      const std::pair<int, int> editKey(nodeIndex, paramIndex);
      bool typing = gTypedParam.count(editKey) > 0;

      bool changed = false;
      if (typing && !modulated)
      {
         // The field takes the widget's place inside the same cell, centred on
         // where the knob cap was, and the cell's full height is reserved
         // afterwards - so starting to type never reflows the row around it.
         // Before this it was a bare 60px InputText whose label ImGui drew
         // OUTSIDE the box to the right, which both shrank the field and
         // spilled the caption across the neighbouring cell: exactly the
         // "entering a formula breaks the UI" failure.
         const float fieldW = std::min(cell - 6.0f, 96.0f);
         const float fieldH = ImGui::GetFrameHeight();
         ImGui::SetCursorScreenPos(ImVec2(cellOrigin.x + (cell - fieldW) * 0.5f,
                                          cellOrigin.y + (diameter - fieldH) * 0.5f));
         ImGui::SetNextItemWidth(fieldW);
         if (gTypedParamJustOpened == editKey)
         {
            ImGui::SetKeyboardFocusHere();
            gTypedParamJustOpened = std::pair<int, int>(-1, -1);
         }
         char buf[256];
         snprintf(buf, sizeof(buf), "%s", gTypedParamText[editKey].c_str());
         const std::string typedId = std::string("##typed") + label;
         FieldWell::PushTypedEditStyle();
         const bool entered = ImGui::InputText(typedId.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
         FieldWell::PopTypedEditStyle();
         gTypedParamText[editKey] = buf;
         if (gTypedParamPendingInit.count(editKey) && ImGui::IsItemActive())
         {
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID()))
            {
               if (gTypedParamNoAutoSelect.count(editKey))
               {
                  state->ReloadUserBufAndMoveToEnd();
                  state->ClearSelection();
               }
               else
               {
                  state->SelectAll();
               }
            }
            gTypedParamPendingInit.erase(editKey);
         }
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (entered || ImGui::IsItemDeactivated())
         {
            const std::string trimmed = TrimCopy(gTypedParamText[editKey]);
            if (!trimmed.empty() && trimmed[0] == '=')
            {
               const std::string exprText = TrimCopy(trimmed.substr(1));
               if (exprText.empty())
                  Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               else
               {
                  Modulation::Instance().SetExpression(nodeIndex, paramIndex, exprText);
                  // A typed expression fully replaces whatever was driving
                  // this param before, including a looping recording - left
                  // alive underneath, it would silently reactivate the moment
                  // the expression was later unbound (isRecordingState only
                  // requires !hasExpr), forcing a second Unbind to actually
                  // remove it.
                  GestureRecorder::Instance().CancelArm(nodeIndex, paramIndex);
                  GestureRecorder::Instance().StopPlayback(nodeIndex, paramIndex);
               }
            }
            else
            {
               Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               if (!trimmed.empty() && !TypedTextIsUntouchedSeed(editKey, trimmed))
               {
                  char* end = nullptr;
                  float parsed = strtof(trimmed.c_str(), &end);
                  if (end != trimmed.c_str())
                  {
                     *value = std::clamp(parsed, std::min(minV, maxV), std::max(minV, maxV));
                     changed = true;
                     // Typing a literal value is a static override - it should
                     // win outright, not sit underneath a gesture loop that
                     // would just overwrite it again next frame.
                     GestureRecorder::Instance().StopPlayback(nodeIndex, paramIndex);
                  }
               }
            }
            gTypedParam.erase(editKey);
            gTypedParamText.erase(editKey);
            gTypedParamSeed.erase(editKey);
            gTypedParamNoAutoSelect.erase(editKey);
            gTypedParamPendingInit.erase(editKey);
         }
         // Same footprint the widget would have had, so the row is identical
         // whether or not one of its cells is being typed into.
         ImGui::SetCursorScreenPos(cellOrigin);
         ImGui::Dummy(ImVec2(cell, diameter + 4.0f + ImGui::GetTextLineHeight()));
      }
      else if (modulated)
      {
         float shown = *value;
         const Modulation::Source src = Modulation::Instance().ResolvedSourceFor(ref);
         // Green (prediction) binding: read-only unless Shift is held - see BeginPredictorGrab.
         const PredictorGrabCtx grab = BeginPredictorGrab(ref);
         const bool moved = DrawWidget(&shown, grab.pred != nullptr ? pinColor : tok::U32(tok::pal::c_FFBE5AFF),
                                       /*readOnly=*/!grab.editable, /*hasRange=*/true, src.lo, src.hi);
         if (grab.editable && moved)
         {
            *value = shown;
            changed = true;
         }
         EndPredictorGrab(grab, ref);
         DrawPredictorDecor(ref, ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y + 2.0f);
         DrawModulationBindingMenu(nodeIndex, paramIndex, ImGui::IsItemHovered());
      }
      else if (hasExpr && !exprErrored)
      {
         // No fx/x badge any more - the purple ring is the only signal. A
         // click/drag only ever moves the local `shown` copy, discarded and
         // overwritten by next frame's expression re-apply - dragging never
         // clears the expression. The only way to remove it is the
         // right-click menu's Unbind, so an accidental drag can't destroy a
         // formula someone typed on purpose.
         float shown = *value;
         DrawWidget(&shown, tok::U32(tok::pal::c_AA82FFFF), /*readOnly=*/false);
         const bool hovered = ImGui::IsItemHovered();
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
         DrawModulationBindingMenu(nodeIndex, paramIndex, hovered);
         if (hovered && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }
      else // plain interactive, including a currently-errored expression
      {
         // A finished, looping recording is locked against direct grabs, the
         // same way modulated/expression params are - only Shift (re-record
         // via a session) or an explicit "Record Again" arm may drive it
         // manually; a plain click/drag just moves a discarded local copy so
         // an accidental grab can't silently erase the loop. Right-click
         // Unbind is the only way to remove it.
         const bool hasPlayback = GestureRecorder::Instance().Playbacks().count(GestureRecorder::Key(nodeIndex, paramIndex)) > 0;
         const bool locked = hasPlayback && !ImGui::GetIO().KeyShift && !GestureRecorder::Instance().IsArmed(nodeIndex, paramIndex);
         float shown = *value;
         const bool widgetChanged = DrawWidget(&shown, recording ? tok::U32(tok::pal::c_EB4646FF) : tok::U32(tok::pal::c_78C8FFEB),
                              /*readOnly=*/false, /*hasRange=*/false, 0.0f, 0.0f, /*activeTint=*/recording);
         if (!locked)
         {
            changed = widgetChanged;
            *value = shown;
         }
         const bool justActivated = ImGui::IsItemActivated();
         if (justActivated && !locked)
         {
            PushUndoCheckpoint();
            GestureRecorder::Instance().StopPlayback(nodeIndex, paramIndex);
         }
         if (ImGui::IsItemActive() && ((ImGui::GetIO().KeyShift && ImGui::IsMouseDown(0)) || GestureRecorder::Instance().IsArmed(nodeIndex, paramIndex)))
            GestureRecorder::Instance().NotifyMovement(nodeIndex, paramIndex, *value, GestureRecorder::Instance().RecordClockNow(), /*isNewGrab=*/justActivated);
         if (ImGui::IsItemDeactivated())
            GestureRecorder::Instance().MaybeFinishArmedRecording(nodeIndex, paramIndex, GestureRecorder::Instance().RecordClockNow());
         const bool hovered = ImGui::IsItemHovered();
         // Double-click / hover-and-type below still opens the typed-entry
         // field even while locked - a deliberate "replace this" action, not
         // an accidental grab - see the matching comment in ModSlider.
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/false);
         DrawModulationBindingMenu(nodeIndex, paramIndex, hovered);
         if (hovered && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }

      // The modulation pin, tucked into the cell's left margin at the knob's
      // base - inside the cell the row already paid for, outside the knob's
      // own interactive rect so a drag from the pin starts a link instead of
      // turning the knob. Same pin id, same ed::BeginPin/EndPin pair, same
      // Modulation registration as before: only where it is drawn changed.
      {
         const ImVec2 cursorAfter = ImGui::GetCursorScreenPos();
         const float box = tok::pin_box;
         // Immediately to the left of the knob, not at the far edge of the
         // cell: in a wide cell a pin parked at the cell boundary reads as
         // belonging to the gap between two knobs rather than to either one.
         // 8px of clear space between the pin's ring and the widget's outer
         // arc. At the old 2px the two visually touched and read as one
         // control with a wart on it rather than as a knob and its pin.
         const float pinX = cellOrigin.x + std::max(0.0f, (cell - widgetW) * 0.5f - box - 8.0f);
         const ImVec2 pinTL(pinX, cellOrigin.y + (diameter - box) * 0.5f);  // centred on the knob (node-ui-pillars P2)
         ImGui::SetCursorScreenPos(pinTL);
         ed::BeginPin(pinId, ed::PinKind::Input);
         ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
         ImGui::Dummy(ImVec2(box, box));
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 c(pinTL.x + box * 0.5f, pinTL.y + box * 0.5f);
         PinDot::Param(dl, c, pinState, IsThemeLight());
         ExpandPinHit(c, pinTL.x + box);
         ed::EndPin();
         gPinAnchors[pinId] = c;
         GraphNode* curGn = FindNodeByIndex(nodeIndex);
         ParamPinScreenInfo pinInfo{ nodeIndex, paramIndex, curGn ? curGn->typeName : "", label ? label : "", c,
                                      cellOrigin, ImVec2(cellOrigin.x + cell, cellOrigin.y + diameter + 16.0f) };
         const bool roundWidget = (style != AudioWidgetStyle::VFader && style != AudioWidgetStyle::VFaderDb);
         if (roundWidget)
         {
            pinInfo.isCircle = true;
            pinInfo.shapeCenter = ImVec2(cellOrigin.x + cell * 0.5f, cellOrigin.y + diameter * 0.5f);
            pinInfo.shapeRadius = diameter * 0.5f + 2.0f;
         }
         gParamPinScreenList.push_back(pinInfo);
         ImGui::SetCursorScreenPos(cursorAfter);
      }

      {
         const bool kbCircle = (style != AudioWidgetStyle::VFader && style != AudioWidgetStyle::VFaderDb);
         const float kcx = cellOrigin.x + cell * 0.5f;
         const ImVec2 kmin = kbCircle ? ImVec2(kcx - diameter * 0.5f, cellOrigin.y) : ImVec2(kcx - 7.0f, cellOrigin.y);
         const ImVec2 kmax = kbCircle ? ImVec2(kcx + diameter * 0.5f, cellOrigin.y + diameter)
                                      : ImVec2(kcx + 7.0f, cellOrigin.y + diameter);
         if (ParamMidiLearnIsActiveFor(nodeIndex, paramIndex))
         {
            if (kbCircle)
               StateRing::DrawCircle(ImGui::GetWindowDrawList(), ImVec2((kmin.x + kmax.x) * 0.5f, (kmin.y + kmax.y) * 0.5f), diameter * 0.5f, StateRing::Kind::Learn, IsThemeLight());
            else
               StateRing::Draw(ImGui::GetWindowDrawList(), kmin, kmax, StateRing::Kind::Learn, IsThemeLight(), tok::radius_field);
         }
         changed = KbParamHook(nodeIndex, paramIndex, value, minV, maxV, step, fmt, kmin, kmax, kbCircle) || changed;
      }
      ImGui::PopID();
      return changed;
   }


   // Integer knob. Unlike ModSliderInt above, this one has to *accumulate*.
   //
   // A slider is absolute - the value is wherever the grab is, so re-deriving
   // the backing float from the int each frame costs nothing. A knob is
   // relative: KnobFloat adds this frame's mouse delta to the float and the
   // int is rounded off it. Re-syncing the float from the int every frame
   // therefore threw away every fraction of a step, so a drag only registered
   // when a *single frame's* delta crossed half a step - about 14 px at once
   // for a 1..8 unison knob, and completely impossible for a wide range. That
   // is why the integer knobs "didn't work perfectly".
   //
   // So the accumulator persists across frames, and is re-seeded from `*value`
   // only when the param moved for some reason other than this knob's own drag
   // (a patch load, an undo, a typed value, a macro). Comparing against what
   // this widget last wrote is what distinguishes the two cases.
   bool ModKnobInt(const char* label, int* value, int minV, int maxV, float diameter,
                   float cellW)
   {
      const std::pair<int, int> key(gCurrentNodeIndex, gParamCounter);
      float& slot = gIntParamStore[key];
      if (!Modulation::Instance().IsModulated(key.first, key.second))
      {
         auto last = gIntParamLastWritten.find(key);
         if (last == gIntParamLastWritten.end() || last->second != *value)
            slot = (float)*value;
      }

      gPendingSrcAddr = value;
      bool changed = ModKnob(label, &slot, (float)minV, (float)maxV, "%.0f", diameter, cellW,
                             AudioWidgetStyle::Knob, /*step=*/1.0f);
      slot = std::clamp(slot, (float)minV, (float)maxV);
      *value = (int)lroundf(slot);
      gIntParamLastWritten[key] = *value;
      return changed;
   }


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
      if (gParamRegisterOnly)
         return; // registered, deliberately not drawn - see gParamRegisterOnly

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
      const float box = tok::pin_box;
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      PinDot::Swatch(dl, c, isBound, IsThemeLight());
      ExpandPinHit(c, p.x + box);
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
         ImGui::TextColored(tok::V4(tok::palf::v_500_860_740_1000), "%s  #%d",
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
   void NodeSeparator(const char* label, float width)
   {
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float y = origin.y + 6.0f;
      const ImU32 col = tok::U32(tok::pal::c_4E5264FF);

      if (label != nullptr && label[0] != '\0')
      {
         const ImVec2 textSize = ImGui::CalcTextSize(label);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + 10.0f, y), col);
         dl->AddText(ImVec2(origin.x + 16.0f, origin.y), tok::U32(tok::pal::c_969CB4FF), label);
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
      return GlyphToggle::Draw("##eye", IconsInfinite::EyeOff, IconsInfinite::Eye, shown, 26.0f);
   }


   // Small monitor/screen toggle for a node's own mini 3D viewport. Deliberately
   // distinct from EyeToggle (params visibility) - this switches on a live
   // render pass, not just a UI panel, so it gets its own affordance rather
   // than overloading the eye icon.
   bool ViewportToggle(bool shown)
   {
      return GlyphToggle::Draw("##miniviewport", IconsInfinite::Viewport, IconsInfinite::ViewportFill, shown);
   }


   // Small toggle for Global Scale (snap to scale). Hand-drawn procedural vector glyph
   // rendering a high-precision beamed musical note (♫) symbol.
   bool GlobalScaleToggle(bool enabled)
   {
      const bool pressed = GlyphToggle::Draw("##globalscale", IconsInfinite::NoteSnap, IconsInfinite::NoteSnapFill, enabled);
      const bool hovered = ImGui::IsItemHovered();

      if (hovered)
      {
         // Deferred: see the comment on gGlobalScaleTooltipHovered above.
         // Drawing a real ed::Suspend()'d tooltip from here (mid per-node
         // ed::Begin()/ed::End()) corrupted ImGui's state for the rest of
         // the frame; the actual tooltip is drawn later from the post-loop
         // suspended block instead.
         gGlobalScaleTooltipHovered = true;
         gGlobalScaleTooltipEnabled = enabled;
      }

      return pressed;
   }


   bool* GetNodeGlobalScaleFlag(INode* node)
   {
      if (node == nullptr) return nullptr;
      if (auto* n = dynamic_cast<MidiNotesNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<NoteFilterNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<NoteTransposeNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<ArpeggiatorNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<NoteSequencerNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<RandomNoteGeneratorNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<ChorderNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<NoteStackNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<BouncingBallsNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<NoteCapturerNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<KeyboardNode*>(node)) return &n->useGlobalScale;
      if (auto* n = dynamic_cast<PredictiveNotesNode*>(node)) return &n->useGlobalScale;
      return nullptr;
   }


   // Small power / bypass toggle for a node. Hand-drawn procedural vector glyph
   // rendering an IEC 60417-5009 power symbol.
   bool BypassToggle(bool bypassed)
   {
      return GlyphToggle::Draw("##bypass", IconsInfinite::Power, IconsInfinite::PowerFill, !bypassed, 22.0f, true);
   }


   // ---- pins --------------------------------------------------------------
   // Drawn inside a kPinHit-wide box so the clickable area is far larger than
   // the visible dot; connecting used to require pixel-perfect aim.
   void DrawPin(int pinId, ed::PinKind kind, const char* label, bool labelFirst)
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
      GraphNode* curGn = FindNodeByIndex(GraphNode::NodeIndexFromPin(pinId));
      const bool isPredPin = curGn != nullptr && (curGn->category == "Prediction" || dynamic_cast<IPredictor*>(curGn->node.get()) != nullptr);
      if (kind == ed::PinKind::Output)
         gPinAnchors[pinId] = c;
      PinDot::Cable(dl, c, isPredPin, isLight);

      if (!labelFirst && label != nullptr && label[0] != '\0')
      {
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::TextDisabled("%s", label);
      }
      ed::EndPin();
   }
}
