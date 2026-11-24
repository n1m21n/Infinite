// Modulation/spectral effect bodies + visualizers (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/UiInteract.h"
#include "app/AppShared.h"
#include "app/ui/design/DrawParam.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/StepCell.h"
#include "audio/dsp/ConvolutionKernel.h"
#include "app/ui/design/UiFmt.h"
#include "app/ui/design/UiScope.h"

namespace app
{
   // ---- Chorus -------------------------------------------------------------
   // `taps` sine traces (one per voice), phase-offset by `spread` and
   // amplitude-scaled by `depth` - the same "layered waves" picture the
   // BLEASS/Ableton chorus references use, minus the color-per-voice (this
   // codebase's accent color is used throughout, differentiated by alpha
   // instead). `delay`/`feedback` show in the corner label/opacity; `rate`
   // and `mix` are deliberately not reflected here (an animated live LFO
   // costs more to draw than the effect it illustrates - README §1 - and
   // mix never changes a wet effect's own shape).
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawChorusVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float midY = origin.y + h * 0.5f;
      AudioViz::MidLine(dl, origin.x, br.x, midY);

      const int taps = n->Param("taps") >= 2.5f ? 3 : 2;
      const float spread = std::clamp(n->Param("spread"), 0.0f, 1.0f);
      const float depthMs = n->Param("depth");
      const float delayMs = n->Param("delay");
      const float feedback = std::clamp(n->Param("feedback"), 0.0f, 0.9f);
      const float amp = std::clamp(depthMs / 15.0f, 0.08f, 1.0f) * (h * 0.5f - 8.0f);

      for (int k = 0; k < taps; k++)
      {
         const float voiceOffset = (float)k / (float)taps;
         const float phaseOffset = voiceOffset + spread * 0.5f * ((float)k / (float)std::max(1, taps - 1));
         const int alpha = std::clamp(tok::fx_chorus_visualizer_v_lg - k * tok::fx_chorus_visualizer_v_sm + (int)(feedback * tok::fx_chorus_visualizer_scale), tok::fx_chorus_visualizer_lo, tok::fx_chorus_visualizer_hi);
         dl->PathClear();
         const int kNumPoints = 96;
         for (int i = 0; i < kNumPoints; i++)
         {
            const float t = (float)i / (float)(kNumPoints - 1);
            const float x = origin.x + t * w;
            const float y = midY - amp * sinf(tok::fx_chorus_visualizer_ratio * (float)M_PI * (t * tok::fx_chorus_visualizer_ratio + phaseOffset));
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(tok::U32(tok::fx_path_shade, isLight, alpha), 0, tok::fx_chorus_visualizer_gap + feedback * tok::fx_chorus_visualizer_path_stroke);
      }

      char buf[24];
      snprintf(buf, sizeof(buf), "%s", UiFmt::Ms(delayMs).c);
      dl->AddText(ImVec2(origin.x + tok::space_1_5, origin.y + tok::space_1_5), ScopeTextCol(), buf);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char hover[80];
         snprintf(hover, sizeof(hover), "%s +/- %s, %s taps, %s fb", UiFmt::Ms(delayMs).c, UiFmt::Ms(depthMs).c, UiFmt::Count((long long)(taps)).c,
                  UiFmt::PctOf(feedback).c);
         SetAudioReadout("chorus", hover);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using ChorusBodyRow = params::Row<AudioEffectNode>;
      const ChorusBodyRow kChorusBodyRows1[] = {
         ChorusBodyRow::Knob("delay", [](AudioEffectNode* n) { return n->ParamPtr("delay"); }, params::fx::delay_lo, params::fx::delay_hi).Fmt("%.1f ms").Size(kKnobLarge),
         ChorusBodyRow::Knob("spread", [](AudioEffectNode* n) { return n->ParamPtr("spread"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         ChorusBodyRow::Knob("depth", [](AudioEffectNode* n) { return n->ParamPtr("depth"); }, params::fx::depth_lo, params::fx::depth_hi).Fmt("%.1f ms").Size(kKnobLarge),
      };
      const ChorusBodyRow kChorusBodyRows2[] = {
         ChorusBodyRow::Knob("feedback", [](AudioEffectNode* n) { return n->ParamPtr("feedback"); }, params::fx::feedback_3_lo, params::fx::feedback_3_hi).Fmt("%.2f").Size(kKnobLarge).Cell(1),
         ChorusBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge).Cell(2),
      };
   }

   void DrawChorusBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const bool analog = n->Param("analog") != 0.0f;
      char stat[112];
      if (sync)
         snprintf(stat, sizeof(stat), "%s taps - %s - %s wet%s", UiFmt::F0(n->Param("taps")).c,
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), UiFmt::PctOf(n->mix).c,
                  analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%s taps - %s - %s wet%s", UiFmt::F0(n->Param("taps")).c, UiFmt::Hz(n->Param("rate")).c,
                  UiFmt::PctOf(n->mix).c, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawChorusVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_chorus_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kChorusBodyRows1);
         row.End();
      }
      {
         // 3 cells: rate | feedback | mix - mix stays bottom-right of the
         // last knob row (P4). `row.index` is set explicitly before each
         // call so the *visual* cell a control lands in can differ from
         // its *draw order* - feedback and mix are still called first and
         // second, exactly as before (see the old 4-cell row this
         // replaces), so their gParamCounter ordinals (and any existing
         // patch's modulation bindings on them) don't move; only where
         // they're drawn, and where the sync dropdown moved to (its own
         // row below), changes. AddSyncedRateCell is called last, exactly
         // as it was before (via AddRateModeCells), so rate's ordinal is
         // unaffected too.
         AudioKnobRow row(tok::fx_chorus_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kChorusBodyRows2);
         row.index = 0;
         AddSyncedRateCell(row, n);
         row.End();
      }

      {
         // 3 cells, all used: sync dropdown left (P3), then the two
         // checkboxes - a clean 3-3-3 grid (P6). The dropdown keeps the
         // exact label id ("sync to tempo##chorusSync") the old
         // AddRateModeCells call used here, matching the discrete-param
         // hash-based addressing AddRateModeCells' own comment documents -
         // any existing patch's modulation binding on the sync control
         // still resolves to this control even though it moved rows.
         AudioKnobRow row(tok::fx_chorus_body_audio_knob_row_layout, tok::fx_row_layout_lg, tok::fx_row_layout_sm, false);
         static const std::vector<std::string> kSyncModes = { "Synced", "Free" };
         row.Dropdown("sync to tempo##chorusSync", kSyncModes, sync ? 0 : 1, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("sync") = (i == 0) ? 1.0f : 0.0f;
         });
         bool taps3 = n->Param("taps") >= 2.5f;
         bool taps3UserChanged = false;
         if (row.Checkbox("3 taps##chorusTaps", &taps3, &taps3UserChanged))
         {
            if (taps3UserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("taps") = taps3 ? 3.0f : 2.0f;
         }
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##chorusAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.End();
      }

      EndAudioBody();
   }


   // ---- Flanger --------------------------------------------------------
   // A dry reference line plus a modulated wet trace whose thickness and
   // ripple sharpness scale with `feedback` - a flanger's resonant comb
   // narrowing as feedback climbs is exactly the "jet swoosh" character the
   // knob controls, so this is the one picture that reads that back
   // directly (the Ableton flanger reference's own dual-wave look, adapted
   // to this codebase's single accent color via a dim dry trace instead of
   // a second hue).
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawFlangerVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float midY = origin.y + h * 0.5f;
      const float delayMs = n->Param("delay");
      const float depthMs = n->Param("depth");
      const float feedback = n->Param("feedback");
      const float amp = std::clamp(depthMs / 10.0f, 0.08f, 1.0f) * (h * 0.5f - 10.0f);
      const float rippleAmp = std::fabs(feedback) * (h * tok::fx_flanger_visualizer_ratio);
      const float rippleFreq = 3.0f + std::fabs(feedback) * 5.0f;

      // Dry reference - a flat carrier ripple with no modulation envelope.
      dl->PathClear();
      for (int i = 0; i < 96; i++)
      {
         const float t = (float)i / 95.0f;
         const float x = origin.x + t * w;
         const float y = midY - (h * tok::fx_flanger_visualizer_scale) * sinf(tok::fx_sinf_pulse_phase * (float)M_PI * t * rippleFreq);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(ScopeMidLineCol(), 0, 1.0f);

      auto drawWet = [&](float phaseOffset, int alpha) {
         dl->PathClear();
         for (int i = 0; i < 96; i++)
         {
            const float t = (float)i / 95.0f;
            const float x = origin.x + t * w;
            const float envelope = amp * sinf(2.0f * (float)M_PI * (t * 2.0f + phaseOffset));
            const float ripple = rippleAmp * sinf(2.0f * (float)M_PI * t * rippleFreq);
            const float y = midY - envelope - ripple;
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(tok::U32(tok::fx_path_shade, isLight, alpha), 0, tok::fx_flanger_visualizer_gap + std::fabs(feedback) * tok::fx_flanger_visualizer_path_stroke);
      };
      drawWet(0.0f, 235);
      drawWet(n->Param("spread") * 0.5f, 120);

      char buf[24];
      snprintf(buf, sizeof(buf), "%s", UiFmt::Ms(delayMs).c);
      dl->AddText(ImVec2(origin.x + tok::space_1_5, origin.y + tok::space_1_5), ScopeTextCol(), buf);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char hover[80];
         snprintf(hover, sizeof(hover), "%s +/- %s, %s fb", UiFmt::Ms(delayMs).c, UiFmt::Ms(depthMs).c, UiFmt::PctOf(feedback, 0, true).c);
         SetAudioReadout("flanger", hover);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using FlangerBodyRow = params::Row<AudioEffectNode>;
      const FlangerBodyRow kFlangerBodyRows1[] = {
         FlangerBodyRow::Knob("delay", [](AudioEffectNode* n) { return n->ParamPtr("delay"); }, params::fx::delay_2_lo, params::fx::delay_2_hi).Fmt("%.2f ms").Size(kKnobLarge),
         FlangerBodyRow::Knob("depth", [](AudioEffectNode* n) { return n->ParamPtr("depth"); }, params::fx::depth_2_lo, params::fx::depth_2_hi).Fmt("%.2f ms").Size(kKnobLarge),
         FlangerBodyRow::Knob("feedback", [](AudioEffectNode* n) { return n->ParamPtr("feedback"); }, params::fx::feedback_4_lo, params::fx::feedback_4_hi).Fmt("%.2f").Size(kKnobLarge),
      };
      const FlangerBodyRow kFlangerBodyRows2[] = {
         FlangerBodyRow::Knob("spread", [](AudioEffectNode* n) { return n->ParamPtr("spread"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge).Cell(1),
         FlangerBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge).Cell(2),
      };
   }

   void DrawFlangerBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const bool analog = n->Param("analog") != 0.0f;
      char stat[112];
      if (sync)
         snprintf(stat, sizeof(stat), "%s - %s fb - %s wet%s",
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), UiFmt::PctOf(n->Param("feedback")).c,
                  UiFmt::PctOf(n->mix).c, analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%s - %s fb - %s wet%s", UiFmt::Ms(n->Param("delay")).c,
                  UiFmt::PctOf(n->Param("feedback")).c, UiFmt::PctOf(n->mix).c, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFlangerVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_flanger_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kFlangerBodyRows1);
         row.End();
      }
      {
         // 3 cells: rate | spread | mix - mix stays bottom-right of the
         // last knob row (P4). row.index is set explicitly before each
         // call so the *visual* cell a control lands in can differ from
         // its *draw order* - see the matching comment in DrawChorusBody.
         // AddSyncedRateCell is called last, exactly as it was before (via
         // AddRateModeCells), so rate's ordinal is unaffected.
         AudioKnobRow row(tok::fx_flanger_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kFlangerBodyRows2);
         row.index = 0;
         AddSyncedRateCell(row, n, 0.02f, 5.0f);
         row.End();
      }

      {
         // 3 cells, all used: sync dropdown left (P3), then the analog
         // checkbox, then a skip - matching Chorus/Phaser's row 3 (P6). The
         // dropdown keeps the exact label id ("sync to tempo##flangerSync")
         // the old AddRateModeCells call used here, matching the
         // discrete-param hash-based addressing AddRateModeCells' own
         // comment documents - any existing patch's modulation binding on
         // the sync control still resolves to this control even though it
         // moved rows.
         AudioKnobRow row(tok::fx_flanger_body_audio_knob_row_layout, tok::fx_row_layout_lg, tok::fx_row_layout_sm, false);
         static const std::vector<std::string> kSyncModes = { "Synced", "Free" };
         row.Dropdown("sync to tempo##flangerSync", kSyncModes, sync ? 0 : 1, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("sync") = (i == 0) ? 1.0f : 0.0f;
         });
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##flangerAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   // ---- Phaser -----------------------------------------------------------
   // An illustrative magnitude-response curve on a log-frequency axis with
   // `order/2` notches spaced between cutoff*2^-depth and cutoff*2^+depth -
   // the same sweep range PhaserKernel::ProcessBlock itself computes for
   // its allpass coefficients, just static rather than animated (rate is
   // deliberately not reflected - see DrawChorusVisualizer's comment). A
   // second, dimmer trace offset by `spread` shows the right channel's
   // widened sweep.
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawPhaserVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float logMin = log10f(50.0f), logMax = log10f(8000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(std::max(hz, 1.0f)) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };
      for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), tok::fx_phaser_visualizer_stroke);
      }

      const float cutoff = n->Param("cutoff");
      const float depth = std::clamp(n->Param("depth"), 0.0f, 1.0f);
      const int order = std::clamp((int)(n->Param("order") + 0.5f), 2, PhaserKernel::kMaxStages);
      const int notches = std::max(1, order / 2);
      const float spread = std::clamp(n->Param("spread"), 0.0f, 1.0f);
      const float fcLo = std::max(20.0f, cutoff * powf(2.0f, -depth));
      const float fcHi = cutoff * powf(2.0f, depth);
      const float notchWidthLog = std::max(0.05f, 0.55f / (float)notches);

      auto drawTrace = [&](float freqScale, int alpha) {
         dl->PathClear();
         const int kNumPoints = 128;
         for (int i = 0; i < kNumPoints; i++)
         {
            const float t = (float)i / (float)(kNumPoints - 1);
            const float logHz = logMin + t * (logMax - logMin);
            float mag = 1.0f;
            for (int k = 0; k < notches; k++)
            {
               const float frac = notches > 1 ? (float)k / (float)(notches - 1) : 0.5f;
               const float centerHz = std::clamp(fcLo * powf(fcHi / fcLo, frac) * freqScale, 20.0f, 19000.0f);
               const float logDist = (logHz - log10f(centerHz)) / notchWidthLog;
               mag *= 1.0f - 0.85f * expf(-logDist * logDist * 4.0f);
            }
            const float x = origin.x + t * w;
            const float y = br.y - tok::space_1_5 - mag * (h - tok::space_4);
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(tok::U32(tok::fx_path_shade, isLight, alpha), 0, tok::fx_phaser_visualizer_path_stroke);
      };
      drawTrace(1.0f, 235);
      if (spread > 0.01f)
         drawTrace(powf(2.0f, spread * 0.5f), 110);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[80];
         snprintf(buf, sizeof(buf), "%s +/- %s oct, %d notches", UiFmt::Hz(cutoff).c, UiFmt::F1(depth).c, notches);
         SetAudioReadout("phaser", buf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using PhaserBodyRow = params::Row<AudioEffectNode>;
      const PhaserBodyRow kPhaserBodyRows1[] = {
         PhaserBodyRow::Knob("cutoff", [](AudioEffectNode* n) { return n->ParamPtr("cutoff"); }, params::fx::cutoff_lo, params::fx::cutoff_hi).Fmt("%.0f Hz").Size(kKnobLarge),
         PhaserBodyRow::Knob("depth", [](AudioEffectNode* n) { return n->ParamPtr("depth"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
      const PhaserBodyRow kPhaserBodyRows2[] = {
         PhaserBodyRow::Knob("spread", [](AudioEffectNode* n) { return n->ParamPtr("spread"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge).Cell(1),
         PhaserBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge).Cell(2),
      };
   }

   void DrawPhaserBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const bool analog = n->Param("analog") != 0.0f;
      char stat[112];
      if (sync)
         snprintf(stat, sizeof(stat), "%s-stage - %s - %s wet%s", UiFmt::F0(n->Param("order")).c,
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), UiFmt::PctOf(n->mix).c,
                  analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%s-stage - %s - %s wet%s", UiFmt::F0(n->Param("order")).c, UiFmt::Hz(n->Param("cutoff")).c,
                  UiFmt::PctOf(n->mix).c, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawPhaserVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_phaser_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kPhaserBodyRows1);
         row.Knob("order", n->ParamPtr("order"), params::fx::order_lo, (float)PhaserKernel::kMaxStages, "%.0f", kKnobLarge);
         row.End();
      }
      {
         // 3 cells: rate | spread | mix - mix stays bottom-right of the
         // last knob row (P4). `row.index` is set explicitly before each
         // call so the *visual* cell a control lands in can differ from
         // its *draw order* - spread and mix are still called first and
         // second, exactly as before (see the old 4-cell row this
         // replaces), so their gParamCounter ordinals (and any existing
         // patch's modulation bindings on them) don't move; only where
         // they're drawn, and where the sync dropdown moved to (its own
         // row below), changes. AddSyncedRateCell is called last, exactly
         // as it was before (via AddRateModeCells), so rate's ordinal is
         // unaffected too - see the matching comment in DrawChorusBody.
         AudioKnobRow row(tok::fx_phaser_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kPhaserBodyRows2);
         row.index = 0;
         AddSyncedRateCell(row, n);
         row.End();
      }

      {
         // 3 cells: sync dropdown left (P3), then the analog checkbox - a
         // clean 3-3-3 grid matching DrawChorusBody's row 3 (P6). Phaser has
         // no "3 taps" equivalent, so the third cell is a deliberate
         // row.Skip() rather than a fabricated control. The dropdown keeps
         // the exact label id ("sync to tempo##phaserSync") the old
         // AddRateModeCells call used here, matching the discrete-param
         // hash-based addressing AddRateModeCells' own comment documents -
         // any existing patch's modulation binding on the sync control
         // still resolves to this control even though it moved rows.
         AudioKnobRow row(tok::fx_phaser_body_audio_knob_row_layout, tok::fx_row_layout_lg, tok::fx_row_layout_sm, false);
         static const std::vector<std::string> kSyncModes = { "Synced", "Free" };
         row.Dropdown("sync to tempo##phaserSync", kSyncModes, sync ? 0 : 1, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("sync") = (i == 0) ? 1.0f : 0.0f;
         });
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##phaserAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   // ---- Bitcrush -----------------------------------------------------------
   // A quantized, sample-and-held sine arch with a solid fill down to the
   // baseline, matching the reference bitcrusher's single-hump filled-
   // staircase look (rather than a centered oscillating line) - `bits`
   // sets the step height (levels on the vertical stairs), `rate` sets the
   // step width (how long each hold lasts).
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawBitcrushVisualizer(AudioEffectNode* n, double sampleRate)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float rateHz = std::max(100.0f, n->Param("rate"));
      const float bits = std::max(1.0f, n->Param("bits"));
      const float levels = powf(2.0f, bits) - 1.0f;
      const float holdSamples = std::max(1.0f, (float)sampleRate / rateHz);
      const float cycleSamples = (float)sampleRate; // one second of illustrative arch at 1 Hz
      const float top = origin.y + tok::space_2;
      const float baseY = br.y - tok::space_1;

      const int kNumSteps = 160;
      ImVec2 pts[kNumSteps];
      float lastHeld = 0.0f;
      for (int i = 0; i < kNumSteps; i++)
      {
         const float sampleIdx = (float)i / (float)(kNumSteps - 1) * cycleSamples;
         const bool tick = fmodf(sampleIdx, holdSamples) < (cycleSamples / (float)kNumSteps);
         float v = fabsf(sinf((float)M_PI * sampleIdx / cycleSamples));
         if (tick || i == 0)
         {
            v = roundf(v * levels) / levels;
            lastHeld = v;
         }
         else
            v = lastHeld;
         const float x = origin.x + (float)i / (float)(kNumSteps - 1) * w;
         const float y = baseY - v * (baseY - top);
         pts[i] = ImVec2(x, y);
      }

      for (int i = 0; i < kNumSteps - 1; i++)
         dl->AddQuadFilled(pts[i], pts[i + 1], ImVec2(pts[i + 1].x, baseY), ImVec2(pts[i].x, baseY),
                            isLight ? tok::Base(tok::col::role_expression_alt, tok::fx_bitcrush_visualizer_fill_alpha_sm) : tok::Base(tok::col::role_expression_light_muted_lo, tok::fx_bitcrush_visualizer_fill_alpha_lg));

      dl->PathClear();
      for (int i = 0; i < kNumSteps; i++)
         dl->PathLineTo(pts[i]);
      dl->PathStroke(isLight ? tok::Base(tok::col::role_expression, tok::fx_bitcrush_visualizer_path_alpha) : tok::Base(tok::col::role_expression_pale_muted_mid, tok::fx_bitcrush_visualizer_path_alpha), 0, tok::fx_bitcrush_visualizer_stroke);

      AudioViz::End(vizFrame);

      char buf[48];
      snprintf(buf, sizeof(buf), "%s - %s bit", UiFmt::Hz(rateHz).c, UiFmt::F0(bits).c);
      if (ImGui::IsMouseHoveringRect(origin, br))
         SetAudioReadout("bitcrush", buf);

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using BitcrushBodyRow = params::Row<AudioEffectNode>;
      const BitcrushBodyRow kBitcrushBodyRows[] = {
         BitcrushBodyRow::Knob("downsample", [](AudioEffectNode* n) { return n->ParamPtr("rate"); }, params::fx::downsample_lo, params::fx::downsample_hi).Fmt("%.0f Hz").Size(kKnobLarge),
         BitcrushBodyRow::Knob("bits", [](AudioEffectNode* n) { return n->ParamPtr("bits"); }, params::fx::bits_lo, params::fx::bits_hi).Fmt("%.0f").Size(kKnobLarge),
         BitcrushBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawBitcrushBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[96];
      snprintf(stat, sizeof(stat), "%s - %s bit - %s wet%s", UiFmt::Hz(n->Param("rate")).c, UiFmt::F0(n->Param("bits")).c,
               UiFmt::PctOf(n->mix).c, analog ? " - analog" : "");

      const double sampleRate =
         AudioEngine::Instance().SampleRate() > 0.0 ? AudioEngine::Instance().SampleRate() : 44100.0;

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawBitcrushVisualizer(n, sampleRate);
      Ui::Spacer(Ui::Space::S1);

      AudioKnobRow row(tok::fx_bitcrush_body_audio_knob_row_layout);
      // Labelled "downsample" (not "rate") to match the BLEASS-style
      // reference's own naming - this is the same sample-and-hold rate
      // param as before (BitcrushKernel::kRateHz), a cosmetic relabel only.
      Ui::DrawKnobs(row, n, kBitcrushBodyRows);
      row.End();

      {
         AudioKnobRow row(tok::fx_bitcrush_body_audio_knob_row_layout, tok::fx_row_layout_lg, tok::fx_row_layout_sm, false);
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##bitcrushAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         // TPDF dither: trades the crush's quantisation distortion for a
         // flat noise floor - a choice of character, so a toggle.
         bool ditherBool = n->Param("dither") != 0.0f;
         bool ditherUserChanged = false;
         if (row.Checkbox("dither##bitcrushDither", &ditherBool, &ditherUserChanged))
         {
            if (ditherUserChanged)
               PushUndoCheckpoint();
            *n->ParamPtr("dither") = ditherBool ? 1.0f : 0.0f;
         }
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   // ---- Transient Shaper -----------------------------------------------
   // A stylized transient spike (fast rise, slow decay) with the attack/
   // sustain knobs shown as gain bars either side, and the kernel's own
   // live transient-amount (ExtraMeterValue(0)) riding on the curve as a
   // dot - the same "picture from params, dot from the kernel" split
   // Dynamics' transfer curve uses.
   // Local dB range for this visualizer only - deliberately not shared with
   // Dynamics' kDynVizMinDb/kDynVizMaxDb (-60..+6), which would clip a
   // +24 dB attack boost off the top of the plot.
   const float kTsVizMinDb = -48.0f;

   const float kTsVizMaxDb = 24.0f;


   float TsVizDbToY(float db, float y0, float h)
   {
      const float t = (db - kTsVizMinDb) / (kTsVizMaxDb - kTsVizMinDb);
      return y0 + h - std::clamp(t, 0.0f, 1.0f) * h;
   }


   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawTransientShaperVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float attackDb = n->Param("attack");
      const float sustainDb = n->Param("sustain");

      const float kWindowSec = 0.4f;   // ui-audit-ok: timing (seconds of signal shown)
      const int kNumPoints = 96;

      auto envAt = [](float t) {
         return t < 0.08f ? (t / 0.08f) : expf(-(t - 0.08f) * 6.0f);
      };

      auto tauAt = [](float tSec) {
         return expf(-tSec / 0.030f);
      };

      float peakDb = -1000.0f;
      float peakX = origin.x, peakY = origin.y;

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
      {
         const float t = (float)i / (float)(kNumPoints - 1);
         const float env = envAt(t);
         const float ampDb = 20.0f * log10f(std::max(env, 1e-4f));
         const float x = origin.x + t * w;
         const float y = TsVizDbToY(ampDb, origin.y, h);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? tok::Base(tok::col::accent, tok::fx_transient_shaper_visualizer_path_alpha_sm) : tok::Base(tok::col::accent_alt, tok::fx_transient_shaper_visualizer_path_alpha_sm), 0, tok::fx_transient_shaper_visualizer_stroke);

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
      {
         const float t = (float)i / (float)(kNumPoints - 1);
         const float tSec = t * kWindowSec;
         const float env = envAt(t);
         const float tau = tauAt(tSec);
         const float gainDb = attackDb * tau + sustainDb * (1.0f - tau);
         const float ampDb = 20.0f * log10f(std::max(env, 1e-4f)) + gainDb;
         const float x = origin.x + t * w;
         const float y = TsVizDbToY(ampDb, origin.y, h);
         dl->PathLineTo(ImVec2(x, y));
         if (ampDb > peakDb)
         {
            peakDb = ampDb;
            peakX = x;
            peakY = y;
         }
      }
      dl->PathStroke(isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, tok::fx_transient_shaper_visualizer_path_alpha_lg), 0, tok::fx_transient_shaper_visualizer_path_stroke);

      const float transientAmount = std::clamp(n->ExtraMeterValue(0), 0.0f, 1.0f);
      dl->AddCircleFilled(ImVec2(peakX, peakY), tok::space_0_75 + transientAmount * tok::fx_transient_shaper_visualizer_ratio,
                          isLight ? tok::Base(tok::col::state_warn, tok::fx_dot_alpha_sm) : tok::Base(tok::col::role_modulation, tok::fx_dot_alpha_sm));

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[80];
         snprintf(buf, sizeof(buf), "attack %s - sustain %s - peak %s", UiFmt::DbOffset(attackDb).c, UiFmt::DbOffset(sustainDb).c, UiFmt::DbOffset(peakDb).c);
         SetAudioReadout("transient shaper", buf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using TransientShaperBodyRow = params::Row<AudioEffectNode>;
      const TransientShaperBodyRow kTransientShaperBodyRows[] = {
         TransientShaperBodyRow::Knob("attack", [](AudioEffectNode* n) { return n->ParamPtr("attack"); }, params::fx::attack_2_lo, params::fx::attack_2_hi).Fmt("%.1f dB").Size(kKnobLarge),
         TransientShaperBodyRow::Knob("sustain", [](AudioEffectNode* n) { return n->ParamPtr("sustain"); }, params::fx::sustain_lo, params::fx::sustain_hi).Fmt("%.1f dB").Size(kKnobLarge),
         TransientShaperBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawTransientShaperBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "atk %s - sus %s", UiFmt::DbOffset(n->Param("attack")).c, UiFmt::DbOffset(n->Param("sustain")).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawTransientShaperVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_transient_shaper_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kTransientShaperBodyRows);
         row.End();
      }

      EndAudioBody();
   }


   // ---- Stutter --------------------------------------------------------
   // One square per repeat in the cycle ("1/8 -> 8 steps"), click to mute
   // that repeat - a gate over the beat-repeat loop, same idea as Ableton
   // Beat Repeat's per-slice mute grid. Works at any rate/steps setting:
   // the grid always redraws at exactly `steps` squares.
   void DrawStutterGateGrid(AudioEffectNode* n, int steps)
   {
      const float w = gAudioBodyW;
      const float h = tok::fx_stutter_gate_grid_h;
      const float gap = tok::space_0_75;
      const float cellW = (w - gap * (float)(steps - 1)) / (float)steps;
      const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const int mask = (int)(n->Param("gateMask") + 0.5f);

      for (int i = 0; i < steps; i++)
      {
         const ImVec2 p0(rowOrigin.x + (float)i * (cellW + gap), rowOrigin.y);
         const ImVec2 p1(p0.x + cellW, p0.y + h);
         ImGui::PushID(i);
         ImGui::SetCursorScreenPos(p0);
         UiInteract::Hit("step", ImVec2(cellW, h));
         if (ImGui::IsItemClicked())
         {
            PushUndoCheckpoint();
            *n->ParamPtr("gateMask") = (float)(mask ^ (1 << i));
         }
         const bool on = (mask & (1 << i)) != 0;
         StepCell::Draw(dl, p0, p1, on, false, ImGui::IsItemHovered());
         ImGui::PopID();
      }

      ImGui::SetCursorScreenPos(rowOrigin);
      Ui::Claim(ImVec2(w, h));
   }


   namespace
   {
      using StutterBodyRow = params::Row<AudioEffectNode>;
   }

   void DrawStutterBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const int steps = std::clamp((int)(n->Param("steps") + 0.5f), 2, StutterKernel::kMaxGateSteps);

      char stat[80];
      if (sync)
         snprintf(stat, sizeof(stat), "%s - %s steps", MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)),
                  UiFmt::Count((long long)(steps)).c);
      else
         snprintf(stat, sizeof(stat), "%s - %s steps", UiFmt::Ms(n->Param("timeMs")).c, UiFmt::Count((long long)(steps)).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      DrawStutterGateGrid(n, steps);
      Ui::Spacer(Ui::Space::S1);

      {
         // "sync" folded into the row itself (was a standalone checkbox
         // line below the knobs) - one compact row instead of a row plus a
         // line, and rate/time already live right next to the toggle that
         // picks which of the two is showing.
         AudioKnobRow row(tok::fx_stutter_body_audio_knob_row_layout);
         row.Dropdown("sync", { "Synced", "Free" }, sync ? 0 : 1, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("sync") = (i == 0) ? 1.0f : 0.0f;
         });
         if (sync)
         {
            row.Dropdown("rate", MusicTime::RateDivisionList(), (int)(n->Param("rateDiv") + 0.5f), [n](int i) {
               PushUndoCheckpoint();
               *n->ParamPtr("rateDiv") = (float)i;
            });
         }
         else
         {
            Ui::DrawKnob(row, n, StutterBodyRow::Knob("time", [](AudioEffectNode* n) { return n->ParamPtr("timeMs"); }, params::fx::time_2_lo, params::fx::time_2_hi).Fmt("%.0f ms").Size(kKnobLarge));
         }
         row.Knob("steps", n->ParamPtr("steps"), params::fx::steps_lo, (float)StutterKernel::kMaxGateSteps, "%.0f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }


   // ---- Ring Mod ---------------------------------------------------------
   // The modulator waveform itself, one static cycle - the ring-modding
   // oscillator's shape is the node's whole identity (its frequency and
   // waveform, not the input signal, define the sidebands).
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawRingModVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float midY = origin.y + h * 0.5f;
      AudioViz::MidLine(dl, origin.x, br.x, midY);

      const int waveform = std::clamp((int)(n->Param("waveform") + 0.5f), 0, (int)DspMath::kWaveSquare);
      const float amp = h * 0.5f - 6.0f;

      dl->PathClear();
      const int kNumPoints = 128;
      DspMath::PolyBlepOsc previewOsc;
      previewOsc.phaseInc = 2.0 / (double)kNumPoints; // two static cycles across the panel
      for (int i = 0; i < kNumPoints; i++)
      {
         const double phase = (double)i * previewOsc.phaseInc;
         const float v = previewOsc.Generate(waveform, 0.5f, phase);
         const float x = origin.x + (float)i / (float)(kNumPoints - 1) * w;
         const float y = midY - v * amp;
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, tok::fx_ring_mod_visualizer_path_alpha), 0, tok::fx_ring_mod_visualizer_path_stroke);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[32];
         snprintf(buf, sizeof(buf), "%s", UiFmt::Hz(n->Param("freq")).c);
         SetAudioReadout("ring mod", buf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using RingModBodyRow = params::Row<AudioEffectNode>;
      const RingModBodyRow kRingModBodyRows[] = {
         RingModBodyRow::Knob("freq", [](AudioEffectNode* n) { return n->ParamPtr("freq"); }, params::fx::freq_2_lo, params::fx::freq_2_hi).Fmt("%.0f Hz").Size(kKnobLarge),
         RingModBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawRingModBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[80];
      snprintf(stat, sizeof(stat), "%s - %s wet%s", UiFmt::Hz(n->Param("freq")).c, UiFmt::PctOf(n->mix).c,
               analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawRingModVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_ring_mod_body_audio_knob_row_layout);
         const int waveform = (int)(n->Param("waveform") + 0.5f);
         static const std::vector<std::string> kWaveNames = SynthModes::WaveformTypeSubset(
            { SynthModes::kWaveSine, SynthModes::kWaveTriangle, SynthModes::kWaveSaw, SynthModes::kWaveSquare });
         row.Dropdown("wave", kWaveNames, waveform, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("waveform") = (float)i;
         });
         // Dropdown is a discrete param (RegisterDiscreteParam, keyed by
         // label hash) and never touches gParamCounter, so moving it ahead of
         // the two continuous knobs here does not renumber their ordinals -
         // freq and mix are still assigned gParamCounter in the same relative
         // order as before (see gParamCounter's comment above, ~line 1162).
         Ui::DrawKnobs(row, n, kRingModBodyRows);
         row.End();
      }

      {
         // Same tight checkbox-row pattern as Pitch Shift above and as
         // Dynamics/SpecBlur/Switcher already use: maxDia 20 (checkbox frame
         // height) + hasCaptions=false, so the row doesn't reserve a full
         // knob-height-plus-caption-strip below a control whose label prints
         // inline rather than underneath it.
         AudioKnobRow row(tok::fx_ring_mod_body_audio_knob_row_layout, tok::fx_row_layout_lg, tok::fx_row_layout_sm, false);
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##ringModAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.Skip();
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   // ---- Frequency Shifter ----------------------------------------------
   // Comb-displacement visualizer: input partials on the upper row, shifted
   // partials on the lower row with directional arrows showing rigid sideways
   // translation (the defining characteristic of linear frequency shifting vs
   // harmonic pitch scaling), plus a large in-panel numeric Hz readout.
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawFrequencyShifterVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float shift = n->Param("shift");
      const float spread = n->Param("spread");
      const float midX = origin.x + w * 0.5f;
      const float topY = origin.y + h * tok::fx_top_y_ratio;
      const float botY = origin.y + h * tok::fx_bot_y_ratio;

      // Center frequency axis & rows
      dl->AddLine(ImVec2(origin.x + tok::space_3, topY), ImVec2(br.x - tok::space_3, topY), ScopeGridCol(), tok::fx_frequency_shifter_visualizer_stroke);
      dl->AddLine(ImVec2(origin.x + tok::space_3, botY), ImVec2(br.x - tok::space_3, botY), ScopeGridCol(), tok::fx_frequency_shifter_visualizer_stroke);
      dl->AddLine(ImVec2(midX, origin.y + tok::space_1_5), ImVec2(midX, br.y - tok::space_1_5), ScopeMidLineCol(), tok::fx_frequency_shifter_visualizer_stroke);

      // Partials comb: 7 evenly spaced input partials
      const int kNumPartials = 7;
      const float kSpacing = w * tok::fx_spacing_ratio;
      const float kMaxShiftPix = w * tok::fx_max_shift_pix_ratio;
      const int range = std::clamp((int)(n->Param("range") + 0.5f), 0, 1);
      const float maxShiftSpan = (range == 0) ? 100.0f : 10000.0f;
      const float shiftFrac = std::clamp(shift / maxShiftSpan, -1.0f, 1.0f);
      const float dxL = shiftFrac * kMaxShiftPix;

      // Arrow & shifted partial color: amber for up-shift, coral/magenta for down-shift, blue-grey for zero
      const ImU32 arrowCol = (shift > tok::fx_arrow_col_limit)
         ? (isLight ? tok::Base(tok::col::state_warn, tok::fx_frequency_shifter_visualizer_state_warn_alpha) : tok::Base(tok::col::role_modulation, tok::fx_frequency_shifter_visualizer_role_modulation_alpha))
         : ((shift < -tok::fx_arrow_col_inset) ? (isLight ? tok::Base(tok::col::state_err, tok::fx_frequency_shifter_visualizer_state_err_alpha) : tok::Base(tok::col::text_err, tok::fx_frequency_shifter_visualizer_text_err_alpha))
                            : (isLight ? tok::Base(tok::col::edge_lo_pale, tok::fx_frequency_shifter_visualizer_edge_lo_pale_alpha) : tok::Base(tok::col::text_secondary, tok::fx_frequency_shifter_visualizer_text_secondary_alpha)));

      for (int i = 0; i < kNumPartials; i++)
      {
         const float xIn = midX + (float)(i - kNumPartials / 2) * kSpacing;
         const float xOutL = xIn + dxL;

         // Input partial tick (upper row)
         dl->AddLine(ImVec2(xIn, topY - tok::fx_frequency_shifter_visualizer_gap), ImVec2(xIn, topY + tok::space_0_75),
                     isLight ? tok::Base(tok::col::surface_lo_pale_fixed, tok::fx_frequency_shifter_visualizer_line_alpha_lg) : tok::Base(tok::col::accent_pale_muted_hi, tok::fx_frequency_shifter_visualizer_line_alpha_sm), tok::fx_frequency_shifter_visualizer_line_stroke_md);

         // Shifted partial tick (lower row)
         dl->AddLine(ImVec2(xOutL, botY - tok::space_0_75), ImVec2(xOutL, botY + tok::fx_frequency_shifter_visualizer_gap), arrowCol, tok::fx_frequency_shifter_visualizer_line_stroke_lg);

         // Connecting arrow from input to shifted partial
         dl->AddLine(ImVec2(xIn, topY + tok::space_0_75), ImVec2(xOutL, botY - tok::space_0_75), arrowCol, tok::fx_frequency_shifter_visualizer_line_stroke_sm);
         if (std::fabs(dxL) > 1.5f || std::fabs(topY - botY) > tok::fx_frequency_shifter_visualizer_limit)
         {
            const float arrowSz = tok::fx_arrow_sz;
            dl->AddTriangleFilled(ImVec2(xOutL, botY - tok::space_0_5),
                                  ImVec2(xOutL - arrowSz, botY - tok::space_0_5 - arrowSz * tok::fx_frequency_shifter_visualizer_ratio),
                                  ImVec2(xOutL + arrowSz, botY - tok::space_0_5 - arrowSz * tok::fx_frequency_shifter_visualizer_ratio),
                                  arrowCol);
         }

         if (spread > 0.5f)
         {
            const float shiftRFrac = std::clamp((shift + spread) / maxShiftSpan, -1.0f, 1.0f);
            const float dxR = shiftRFrac * kMaxShiftPix;
            const float xOutR = xIn + dxR;
            const ImU32 spreadCol = (shift + spread > tok::fx_arrow_col_limit)
               ? (isLight ? tok::Base(tok::col::state_warn, tok::fx_spread_col_alpha_lg) : tok::Base(tok::col::role_modulation, tok::fx_spread_col_alpha_md))
               : ((shift + spread < -tok::fx_arrow_col_inset) ? (isLight ? tok::Base(tok::col::viz_pink_dark, tok::fx_frequency_shifter_visualizer_viz_pink_dark_alpha) : tok::Base(tok::col::viz_pink_light, tok::fx_frequency_shifter_visualizer_viz_pink_light_alpha))
                                           : (isLight ? tok::Base(tok::col::edge_lo_pale, tok::fx_spread_col_alpha_md) : tok::Base(tok::col::text_secondary, tok::fx_spread_col_alpha_sm)));
            dl->AddLine(ImVec2(xOutR, botY - tok::space_0_5), ImVec2(xOutR, botY + tok::space_1_5), spreadCol, tok::fx_frequency_shifter_visualizer_line_stroke_sm);
         }
      }

      char valBuf[96];
      auto FmtFreq = [](float f, char* b, size_t sz, bool withSign) {
         snprintf(b, sz, "%s", UiFmt::Hz(f, withSign).c);
      };

      char sBuf[32], rBuf[32];
      FmtFreq(shift, sBuf, sizeof(sBuf), true);
      if (std::fabs(spread) > 0.5f)
      {
         FmtFreq(shift + spread, rBuf, sizeof(rBuf), true);
         snprintf(valBuf, sizeof(valBuf), "%s / %s", sBuf, rBuf);
      }
      else
      {
         snprintf(valBuf, sizeof(valBuf), "%s", sBuf);
      }

      const ImVec2 textSz = ImGui::CalcTextSize(valBuf);
      dl->AddText(ImVec2(br.x - textSz.x - tok::space_2_5, origin.y + tok::space_2),
                  isLight ? tok::Base(tok::col::edge_lo, tok::fx_frequency_shifter_visualizer_text_alpha) : tok::Base(tok::col::edge_hi, tok::fx_frequency_shifter_visualizer_text_alpha), valBuf);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         SetAudioReadout("freq shift", valBuf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using FrequencyShifterBodyRow = params::Row<AudioEffectNode>;
   }

   void DrawFrequencyShifterBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[80];
      const float shiftVal = n->Param("shift");
      if (std::fabs(shiftVal) >= 1000.0f)
         snprintf(stat, sizeof(stat), "%s - %s fb - %s wet%s",
                  UiFmt::Hz(shiftVal, true).c, UiFmt::PctOf(n->Param("feedback")).c, UiFmt::PctOf(n->mix).c, analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%s - %s fb - %s wet%s",
                  UiFmt::Hz(shiftVal, true).c, UiFmt::PctOf(n->Param("feedback")).c, UiFmt::PctOf(n->mix).c, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFrequencyShifterVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         const int range = std::clamp((int)(n->Param("range") + 0.5f), 0, 1);
         const float shiftLimit = (range == 0) ? 100.0f : 10000.0f;
         static const std::vector<std::string> kRangeOptions = { "fine +/-100", "wide +/-10k" };
         const char* fmt = (range == 0) ? "%+.1f Hz" : (std::fabs(shiftVal) >= 1000.0f ? "%+.2f kHz" : "%+.0f Hz");

         AudioKnobRow row(tok::fx_frequency_shifter_body_audio_knob_row_layout, kKnobLarge, ImGui::GetFrameHeight() + tok::space_1_25);
         row.DropdownKnob("fsRange", kRangeOptions, range,
                          [n](int i) {
                             PushUndoCheckpoint();
                             *n->ParamPtr("range") = (float)i;
                          },
                          "shift", n->ParamPtr("shift"), -shiftLimit, shiftLimit, fmt, false, kKnobLarge);
         Ui::DrawKnob(row, n, FrequencyShifterBodyRow::Knob("feedback", [](AudioEffectNode* n) { return n->ParamPtr("feedback"); }, params::fx::feedback_2_lo, params::fx::feedback_2_hi).Fmt("%.2f").Size(kKnobLarge));
         row.Knob("spread", n->ParamPtr("spread"), params::fx::spread_lo, params::fx::spread_hi, "%.1f Hz", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewSpread100);
         Ui::DrawKnob(row, n, FrequencyShifterBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge));
         row.End();
      }

      {
         AudioKnobRow row(tok::fx_frequency_shifter_body_audio_knob_row_layout, tok::fx_row_layout_lg, tok::fx_row_layout_sm, false);
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##freqShiftAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.Skip();
         row.Skip();
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   // ---- Tremolo --------------------------------------------------------
   // The resulting *gain envelope*, not the raw bipolar LFO - a filled area
   // from the panel top down to the curve so the eye reads "how much signal
   // survives" directly, exactly what a tremolo pedal's amplitude modulation
   // does and Ring Mod's raw-oscillator preview (above) deliberately does
   // not - see TremoloKernel.h's class comment on the distinction. The right
   // channel's envelope overlays as a second, dimmer stroke whenever
   // `stereoPhase` is non-zero, since that knob is otherwise invisible.
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawTremoloVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const int shape = std::clamp((int)(n->Param("shape") + 0.5f), 0, 3);
      const int dspWaveform = TremoloKernel::ShapeToDspWaveform(shape);
      const bool rampDown = shape == 3;
      const float depth = std::clamp(n->Param("depth"), 0.0f, 1.0f);
      const float stereoPhaseCycles = std::clamp(n->Param("stereoPhase"), 0.0f, 180.0f) / 360.0f;

      DspMath::PolyBlepOsc previewOsc;
      previewOsc.phaseInc = 2.0 / (double)128; // two static cycles across the panel

      auto envelopeY = [&](double phase) {
         float lfo = previewOsc.Generate(dspWaveform, 0.5f, phase);
         if (rampDown)
            lfo = -lfo;
         const float gain = TremoloKernel::GainFromLfo(lfo, depth);
         return br.y - gain * h;
      };

      const int kNumPoints = 128;
      ImVec2 envPts[kNumPoints];
      for (int i = 0; i < kNumPoints; i++)
      {
         const double phase = (double)i * previewOsc.phaseInc;
         const float x = origin.x + (float)i / (float)(kNumPoints - 1) * w;
         envPts[i] = ImVec2(x, envelopeY(phase));
      }
      for (int i = 0; i < kNumPoints - 1; i++)
         dl->AddQuadFilled(envPts[i], envPts[i + 1], ImVec2(envPts[i + 1].x, br.y), ImVec2(envPts[i].x, br.y),
                            isLight ? tok::Base(tok::col::accent, tok::fx_tremolo_visualizer_fill_alpha_sm) : tok::Base(tok::col::accent_alt, tok::fx_tremolo_visualizer_fill_alpha_lg));

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
         dl->PathLineTo(envPts[i]);
      dl->PathStroke(isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, tok::fx_tremolo_visualizer_path_alpha_lg), 0, tok::fx_tremolo_visualizer_stroke);

      if (stereoPhaseCycles > 0.0f)
      {
         dl->PathClear();
         for (int i = 0; i < kNumPoints; i++)
         {
            const double phase = (double)i * previewOsc.phaseInc + stereoPhaseCycles;
            const float x = origin.x + (float)i / (float)(kNumPoints - 1) * w;
            dl->PathLineTo(ImVec2(x, envelopeY(phase)));
         }
         dl->PathStroke(isLight ? tok::Base(tok::col::accent, tok::fx_tremolo_visualizer_path_alpha_sm) : tok::Base(tok::col::accent_alt, tok::fx_tremolo_visualizer_path_alpha_sm), 0, tok::fx_tremolo_visualizer_path_stroke);
      }

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[32];
         const bool sync = n->Param("sync") != 0.0f;
         if (sync)
         {
            const int rateDiv = (int)(n->Param("rateDiv") + 0.5f);
            const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
            const double rateHz = MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, bpm);
            snprintf(buf, sizeof(buf), "%s (%s)", MusicTime::RateDivisionName(rateDiv), UiFmt::Hz(rateHz).c);
         }
         else
            snprintf(buf, sizeof(buf), "%s", UiFmt::Hz(n->Param("rate")).c);
         SetAudioReadout("tremolo", buf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using TremoloBodyRow = params::Row<AudioEffectNode>;
      const TremoloBodyRow kTremoloBodyRows[] = {
         TremoloBodyRow::Knob("depth", [](AudioEffectNode* n) { return n->ParamPtr("depth"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         TremoloBodyRow::Knob("stereo", [](AudioEffectNode* n) { return n->ParamPtr("stereoPhase"); }, params::fx::stereo_lo, params::fx::stereo_hi).Fmt("%.0f\xC2\xB0").Size(kKnobLarge),
      };
   }

   void DrawTremoloBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%s depth - %s wet", UiFmt::PctOf(n->Param("depth")).c, UiFmt::PctOf(n->mix).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawTremoloVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         // Selector column left (shape v), knobs right (depth, stereo).
         // The dropdown is a discrete param (numbered from a hash of its
         // label - DiscreteParamSlot) rather than a gParamCounter ordinal,
         // so calling it first here costs "depth"/"stereo" nothing - they
         // still get the same ordinals (0, 1) they always have.
         AudioKnobRow row(tok::fx_tremolo_body_audio_knob_row_layout);
         const int shape = (int)(n->Param("shape") + 0.5f);
         static const std::vector<std::string> kShapeNames = SynthModes::WaveformTypeSubset(
            { SynthModes::kWaveSine, SynthModes::kWaveTriangle, SynthModes::kWaveSquare, SynthModes::kWaveRampDown });
         row.Dropdown("shape", kShapeNames, shape, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("shape") = (float)i;
         });
         Ui::DrawKnobs(row, n, kTremoloBodyRows);
         row.End();
      }
      {
         // Selector column left (mode v, rate), mix bottom-right (P4). Rate
         // is still called before mix, exactly as before, so mix's ordinal
         // doesn't move.
         AudioKnobRow row(tok::fx_tremolo_body_audio_knob_row_layout);
         AddRateModeCells(row, n, "sync to tempo##tremoloSync", 0.05f, 20.0f);
         Ui::DrawKnob(row, n, TremoloBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge));
         row.End();
      }

      EndAudioBody();
   }


   // ---- Formant Filter -----------------------------------------------------
   // The three formant resonators' frequencies as markers on a log axis -
   // the picture a filter's own frequency-response curve would give, but
   // for three narrow bandpass peaks rather than one broad shape, so three
   // labelled markers reads more clearly than a busy triple-peaked curve.
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawFormantFilterVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float logMin = log10f(100.0f), logMax = log10f(5000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(hz) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };

      for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), tok::fx_formant_filter_visualizer_stroke);
      }

      const FormantDsp::Formants f = FormantDsp::VowelFormants(n->Param("vowel"));
      const float formants[3] = { f.f1, f.f2, f.f3 };
      const float weights[3] = { 1.0f, 0.7f, 0.5f };
      static const char* kFormantLabels[3] = { "F1", "F2", "F3" };
      for (int i = 0; i < 3; i++)
      {
         const float x = freqToX(formants[i]);
         const float peakY = br.y - tok::space_1_5 - weights[i] * (h - tok::fx_formant_filter_visualizer_gap);
         dl->AddLine(ImVec2(x, br.y - tok::space_1_5), ImVec2(x, peakY),
                     isLight ? tok::Base(tok::col::accent, tok::fx_formant_filter_visualizer_line_alpha_lg) : tok::Base(tok::col::accent_alt, tok::fx_formant_filter_visualizer_line_alpha_sm), tok::fx_formant_filter_visualizer_line_stroke);
         dl->AddText(ImVec2(x - tok::fx_text_x, peakY - tok::space_3_5), ScopeTextCol(), kFormantLabels[i]);
      }

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "F1 %s - F2 %s - F3 %s", UiFmt::F0(f.f1).c, UiFmt::F0(f.f2).c, UiFmt::Hz(f.f3).c);
         SetAudioReadout("formant filter", buf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using FormantFilterBodyRow = params::Row<AudioEffectNode>;
      const FormantFilterBodyRow kFormantFilterBodyRows[] = {
         FormantFilterBodyRow::Knob("vowel", [](AudioEffectNode* n) { return n->ParamPtr("vowel"); }, params::fx::vowel_lo, params::fx::vowel_hi).Fmt("%.2f").Size(kKnobLarge),
         FormantFilterBodyRow::Knob("Q", [](AudioEffectNode* n) { return n->ParamPtr("q"); }, params::fx::q_2_lo, params::fx::q_2_hi).Fmt("%.1f").Size(kKnobLarge),
         FormantFilterBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawFormantFilterBody(GraphNode& gn, AudioEffectNode* n)
   {
      static const char* kVowelNames[] = { "A", "E", "I", "O", "U" };
      const int vowelIdx = std::clamp((int)(n->Param("vowel") + 0.5f), 0, 4);

      char stat[64];
      snprintf(stat, sizeof(stat), "%s - Q %s - %s wet", kVowelNames[vowelIdx], UiFmt::F1(n->Param("q")).c, UiFmt::PctOf(n->mix).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFormantFilterVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      AudioKnobRow row(tok::fx_formant_filter_body_audio_knob_row_layout);
      Ui::DrawKnobs(row, n, kFormantFilterBodyRows);
      row.End();

      EndAudioBody();
   }


   // ---- Resonator Bank -------------------------------------------------
   // Log-frequency pole-distribution visualizer: stems showing each active
   // resonator's center frequency, Q height and stereo pan coloring.
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawResonatorBankVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float logMin = log10f(20.0f), logMax = log10f(20000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(std::max(hz, 20.0f)) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };

      for (float hz : { 100.0f, 1000.0f, 10000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), tok::fx_resonator_bank_visualizer_stroke);
         char hzStr[16];
         if (hz >= 1000.0f)
            snprintf(hzStr, sizeof(hzStr), "%sk", UiFmt::F0(hz / 1000.0f).c);
         else
            snprintf(hzStr, sizeof(hzStr), "%s", UiFmt::F0(hz).c);
         dl->AddText(ImVec2(x + tok::space_0_5, br.y - tok::space_3), ScopeTextCol(), hzStr);
      }

      const float rootFreq = std::clamp(n->Param("rootFreq"), 20.0f, 2000.0f);
      const int structure = std::clamp((int)std::round(n->Param("structure")), 0, 3);
      const int numPoles = std::clamp((int)std::round(n->Param("poles")), 1, 16);
      const float decay = std::clamp(n->Param("decay"), 0.05f, 10.0f);
      const float spread = std::clamp(n->Param("spread"), 0.0f, 1.0f);
      const float damp = std::clamp(n->Param("damp"), 0.0f, 1.0f);

      for (int i = 0; i < numPoles; i++)
      {
         float f_i = rootFreq;
         switch (structure)
         {
            case 0: f_i = rootFreq * (float)(i + 1); break;
            case 1: f_i = rootFreq * (float)(2 * i + 1); break;
            case 2: {
               static const int kChordSet[6] = { 0, 7, 12, 16, 19, 24 };
               f_i = rootFreq * powf(2.0f, (float)(kChordSet[i % 6] + 12 * (i / 6)) / 12.0f);
               break;
            }
            case 3:
            default:
               f_i = rootFreq * (float)(i + 1) * sqrtf(1.0f + 0.001f * (float)((i + 1) * (i + 1)));
               break;
         }
         if (f_i > 20000.0f)
            continue;

         const float x = freqToX(f_i);
         // Mirrors ResonatorBankKernel::PushParams' decay_i - see the comment
         // there for the formula and why damp == 0 is a no-op.
         const float freqRatio = std::min(rootFreq / f_i, 1.0f);
         const float decay_i = std::clamp(decay * powf(freqRatio, damp), 0.05f, decay);
         const float q_i = std::clamp(0.4547f * f_i * decay_i, 0.5f, 500.0f);
         const float normH = std::clamp(log10f(q_i) / log10f(500.0f), tok::fx_norm_h_lo, tok::fx_norm_h_hi);
         const float peakY = br.y - tok::space_3_5 - normH * (h - tok::fx_resonator_bank_visualizer_gap);
         // Damped partials fade as well as shrink, so raising damp visibly
         // tilts the whole spectrum down and out to the right.
         const float alphaScale = std::clamp(decay_i / std::max(decay, 0.0001f), 0.25f, 1.0f);

         const float pos = (numPoles > 1) ? ((float)i / (float)(numPoles - 1) - tok::fx_resonator_bank_visualizer_inset) * spread + tok::px_align : tok::fx_pos;
         const ImU32 baseCol = (pos < tok::fx_resonator_bank_visualizer_limit)
            ? (isLight ? tok::Base(tok::col::accent, tok::fx_resonator_bank_visualizer_accent_alpha) : tok::Base(tok::col::accent_pale_muted_warm, tok::fx_resonator_bank_visualizer_accent_pale_muted_warm_alpha))
            : ((pos > tok::fx_base_col_limit)
               ? (isLight ? tok::Base(tok::col::state_err, tok::fx_resonator_bank_visualizer_state_err_alpha) : tok::Base(tok::col::state_warn, tok::fx_resonator_bank_visualizer_state_warn_alpha))
               : (isLight ? tok::Base(tok::col::viz_teal, tok::fx_resonator_bank_visualizer_viz_teal_alpha) : tok::Base(tok::col::state_ok, tok::fx_resonator_bank_visualizer_state_ok_alpha)));
         const unsigned baseAlpha = (baseCol >> IM_COL32_A_SHIFT) & 0xFF;
         const ImU32 stemCol = (baseCol & ~IM_COL32_A_MASK) |
            (((unsigned)(baseAlpha * alphaScale)) << IM_COL32_A_SHIFT);

         dl->AddLine(ImVec2(x, br.y - tok::space_3_5), ImVec2(x, peakY), stemCol, tok::fx_resonator_bank_visualizer_line_stroke);
         dl->AddCircleFilled(ImVec2(x, peakY), tok::fx_resonator_bank_visualizer_r, stemCol);
      }

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         static const char* kStructNames[] = { "Harmonic", "Odd", "Chord", "Metallic" };
         char buf[80];
         if (damp > 0.001f)
            snprintf(buf, sizeof(buf), "root %s - %s - %s poles - %s damp", UiFmt::Hz(rootFreq).c, kStructNames[structure], UiFmt::Count((long long)(numPoles)).c, UiFmt::PctOf(damp).c);
         else
            snprintf(buf, sizeof(buf), "root %s - %s - %s poles", UiFmt::Hz(rootFreq).c, kStructNames[structure], UiFmt::Count((long long)(numPoles)).c);
         SetAudioReadout("resonator bank", buf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using ResonatorBankBodyRow = params::Row<AudioEffectNode>;
      const ResonatorBankBodyRow kResonatorBankBodyRows1[] = {
         ResonatorBankBodyRow::Knob("root", [](AudioEffectNode* n) { return n->ParamPtr("rootFreq"); }, params::fx::root_lo, params::fx::root_hi).Fmt("%.0f Hz").Size(kKnobLarge),
         ResonatorBankBodyRow::Knob("poles", [](AudioEffectNode* n) { return n->ParamPtr("poles"); }, params::fx::poles_lo, params::fx::poles_hi).Fmt("%.0f").Size(kKnobLarge),
         ResonatorBankBodyRow::Knob("decay", [](AudioEffectNode* n) { return n->ParamPtr("decay"); }, params::fx::decay_2_lo, params::fx::decay_2_hi).Fmt("%.2fs").Size(kKnobLarge),
      };
      const ResonatorBankBodyRow kResonatorBankBodyRows2[] = {
         ResonatorBankBodyRow::Knob("scatter", [](AudioEffectNode* n) { return n->ParamPtr("scatter"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         ResonatorBankBodyRow::Knob("spread", [](AudioEffectNode* n) { return n->ParamPtr("spread"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         ResonatorBankBodyRow::Knob("damp", [](AudioEffectNode* n) { return n->ParamPtr("damp"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         ResonatorBankBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawResonatorBankBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float rootFreq = n->Param("rootFreq");
      const int structure = std::clamp((int)std::round(n->Param("structure")), 0, 3);
      const int poles = std::clamp((int)std::round(n->Param("poles")), 1, 16);
      const bool analog = n->Param("analog") != 0.0f;
      const float damp = std::clamp(n->Param("damp"), 0.0f, 1.0f);

      static const char* kStructNames[] = { "Harmonic", "Odd", "Chord", "Metallic" };
      char stat[96];
      if (damp > 0.001f)
         snprintf(stat, sizeof(stat), "%s - %s - %s poles - %s damp%s", UiFmt::Hz(rootFreq).c, kStructNames[structure], UiFmt::Count((long long)(poles)).c, UiFmt::PctOf(damp).c, analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%s - %s - %s poles%s", UiFmt::Hz(rootFreq).c, kStructNames[structure], UiFmt::Count((long long)(poles)).c, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawResonatorBankVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_resonator_bank_body_audio_knob_row_layout, kKnobLarge);
         static const std::vector<std::string> kStructs = { "harmonic", "odd", "chord", "metallic" };
         row.Dropdown("struct", kStructs, structure, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("structure") = (float)i;
         });
         Ui::DrawKnobs(row, n, kResonatorBankBodyRows1);
         row.End();
      }

      {
         AudioKnobRow row(tok::fx_resonator_bank_body_audio_knob_row_layout, kKnobLarge);
         Ui::DrawKnobs(row, n, kResonatorBankBodyRows2);
         row.End();
      }

      {
         AudioKnobRow row(tok::fx_resonator_bank_body_audio_knob_row_layout, tok::fx_row_layout_lg, tok::fx_row_layout_sm, false);
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##resonatorAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.Skip();
         row.Skip();
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   // ---- Cycle Shaper ---------------------------------------------------
   // Single-wavecycle replacement visualizer: shows the synthesized geometric
   // waveform (Sine / Square / Triangle) within the gate threshold bounds.
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawCycleShaperVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeGridCol(), tok::fx_cycle_shaper_visualizer_stroke);

      const int waveform = std::clamp((int)std::round(n->Param("waveform")), 0, 2);
      const float thresholdDb = std::clamp(n->Param("threshold"), -60.0f, -12.0f);
      const float threshLinear = powf(10.0f, thresholdDb / 20.0f);
      const float threshY1 = midY - threshLinear * (h * tok::fx_cycle_shaper_visualizer_ratio);
      const float threshY2 = midY + threshLinear * (h * tok::fx_cycle_shaper_visualizer_ratio);
      const ImU32 threshCol = isLight ? tok::Base(tok::col::state_err_muted, tok::fx_cycle_shaper_visualizer_state_err_muted_alpha) : tok::Base(tok::col::state_err_pressed, tok::fx_cycle_shaper_visualizer_state_err_pressed_alpha);
      dl->AddLine(ImVec2(origin.x, threshY1), ImVec2(br.x, threshY1), threshCol, tok::fx_cycle_shaper_visualizer_stroke);
      dl->AddLine(ImVec2(origin.x, threshY2), ImVec2(br.x, threshY2), threshCol, tok::fx_cycle_shaper_visualizer_stroke);

      const ImU32 waveCol = isLight ? tok::Base(tok::col::accent_light_muted_warm, tok::fx_cycle_shaper_visualizer_accent_light_muted_warm_alpha) : tok::Base(tok::col::accent_pale_muted_warm, tok::fx_cycle_shaper_visualizer_accent_pale_muted_warm_alpha);
      const int kPlotPoints = 128;
      ImVec2 pts[kPlotPoints];

      for (int i = 0; i < kPlotPoints; i++)
      {
         const float t = (float)i / (float)(kPlotPoints - 1);
         const float phase = t * (float)(2.0 * M_PI);
         const float s = sinf(phase);
         float y = 0.0f;

         switch (waveform)
         {
            case 0: y = s; break;
            case 1: y = (s >= 0.0f ? 1.0f : -1.0f); break;
            case 2:
            default:
               y = (tok::fx_cycle_shaper_visualizer_scale_lg / (float)M_PI) * asinf(std::clamp(s, -1.0f, 1.0f));
               break;
         }

         pts[i] = ImVec2(origin.x + t * w, midY - y * (h * tok::fx_cycle_shaper_visualizer_scale_sm));
      }

      for (int i = 0; i < kPlotPoints - 1; i++)
         dl->AddLine(pts[i], pts[i + 1], waveCol, tok::fx_cycle_shaper_visualizer_line_stroke);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         static const char* kWaves[] = { "sine", "square", "triangle" };
         char buf[64];
         snprintf(buf, sizeof(buf), "wave: %s - thresh: %s", kWaves[waveform], UiFmt::Db(thresholdDb).c);
         SetAudioReadout("cycle shaper", buf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using CycleShaperBodyRow = params::Row<AudioEffectNode>;
      const CycleShaperBodyRow kCycleShaperBodyRows[] = {
         CycleShaperBodyRow::Knob("thresh", [](AudioEffectNode* n) { return n->ParamPtr("threshold"); }, params::fx::thresh_lo, params::fx::thresh_hi).Fmt("%.0fdB").Size(kKnobLarge),
         CycleShaperBodyRow::Knob("smooth", [](AudioEffectNode* n) { return n->ParamPtr("smooth"); }, params::fx::smooth_lo, params::fx::smooth_hi).Fmt("%.0fsmp").Size(kKnobLarge),
         CycleShaperBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawCycleShaperBody(GraphNode& gn, AudioEffectNode* n)
   {
      const int waveform = std::clamp((int)std::round(n->Param("waveform")), 0, 2);
      const float thresholdDb = n->Param("threshold");
      const int smooth = std::clamp((int)std::round(n->Param("smooth")), 0, 32);
      const bool analog = n->Param("analog") != 0.0f;

      static const char* kWaves[] = { "sine", "square", "triangle" };
      char stat[80];
      snprintf(stat, sizeof(stat), "%s - %s - %dsmp%s", kWaves[waveform], UiFmt::Db(thresholdDb).c, smooth, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawCycleShaperVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_cycle_shaper_body_audio_knob_row_layout);
         static const std::vector<std::string> kWaveNames = SynthModes::WaveformTypeSubset(
            { SynthModes::kWaveSine, SynthModes::kWaveSquare, SynthModes::kWaveTriangle });
         row.Dropdown("shape", kWaveNames, waveform, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("waveform") = (float)i;
         });
         Ui::DrawKnobs(row, n, kCycleShaperBodyRows);
         row.End();
      }

      {
         AudioKnobRow row(tok::fx_cycle_shaper_body_audio_knob_row_layout, tok::fx_row_layout_lg, tok::fx_row_layout_sm, false);
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##cycleShaperAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.Skip();
         row.Skip();
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawSpecBlurVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      // Grid lines: 100Hz, 1kHz, 10kHz
      const double sr = 48000.0;
      auto hzToX = [&](float hz) -> float {
         const float norm = (log10f(std::max(hz, 20.0f)) - log10f(20.0f)) / (log10f(20000.0f) - log10f(20.0f));
         return origin.x + std::clamp(norm, 0.0f, 1.0f) * w;
      };

      dl->AddLine(ImVec2(hzToX(tok::fx_hz_to_x_sm), origin.y), ImVec2(hzToX(tok::fx_hz_to_x_sm), br.y), ScopeGridCol(), tok::fx_spec_blur_visualizer_stroke);
      dl->AddLine(ImVec2(hzToX(tok::fx_hz_to_x_md), origin.y), ImVec2(hzToX(tok::fx_hz_to_x_md), br.y), ScopeGridCol(), tok::fx_spec_blur_visualizer_stroke);
      dl->AddLine(ImVec2(hzToX(tok::fx_hz_to_x_lg), origin.y), ImVec2(hzToX(tok::fx_hz_to_x_lg), br.y), ScopeGridCol(), tok::fx_spec_blur_visualizer_stroke);

      SpecBlurSpectrumState& st = sSpecBlurSpectrums[n];
      const int winSize = tok::fx_win_size;
      float tempBuf[1024];
      const int readCount = n->ReadSpectrumSamples(tempBuf, winSize);
      if (readCount > 0)
      {
         if (readCount >= winSize)
         {
            st.window.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
         }
         else
         {
            const int keep = winSize - readCount;
            std::copy(st.window.begin() + readCount, st.window.end(), st.window.begin());
            std::copy(tempBuf, tempBuf + readCount, st.window.begin() + keep);
         }
      }

      float re[1024];
      float im[1024];
      for (int i = 0; i < winSize; i++)
      {
         const float wnd = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(winSize - 1)));
         re[i] = st.window[i] * wnd;
         im[i] = 0.0f;
      }
      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);

      for (int i = 1; i < 512; i++)
      {
         const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) * (2.0f / (float)winSize);
         st.smoothed[i] = st.smoothed[i] * 0.8f + mag * 0.2f;
      }

      const int kPlotPoints = 128;
      ImVec2 pts[kPlotPoints];
      float peak = 0.0f;
      for (int i = 0; i < kPlotPoints; i++)
      {
         const float frac = (float)i / (float)(kPlotPoints - 1);
         const float hz = 20.0f * powf(1000.0f, frac);
         const int bin = std::clamp((int)(hz * (float)winSize / (float)sr), 1, 511);
         const float mag = st.smoothed[bin];
         const float db = 20.0f * log10f(std::max(mag, 1e-4f));
         // Map -60 dB .. 0 dB to height
         const float normY = std::clamp((db + tok::fx_spec_blur_visualizer_norm_y) / tok::fx_spec_blur_visualizer_ratio, 0.0f, 1.0f);
         pts[i] = ImVec2(origin.x + frac * w, br.y - normY * (h * tok::fx_spec_blur_visualizer_scale) - tok::space_0_5);
         peak = std::max(peak, normY);
      }
      if (peak < 0.02f)
         AudioViz::IdleLabel(vizFrame);

      const ImU32 fillCol = isLight ? tok::Base(tok::col::role_expression, tok::fx_spec_blur_visualizer_role_expression_alpha) : tok::Base(tok::col::role_expression, tok::fx_spec_blur_visualizer_role_expression_alpha);
      const ImU32 lineCol = isLight ? tok::Base(tok::col::role_expression, tok::fx_line_col_alpha_lg) : tok::Base(tok::col::role_expression_pale, tok::fx_spec_blur_visualizer_role_expression_pale_alpha);

      for (int i = 0; i < kPlotPoints - 1; i++)
      {
         ImVec2 quad[4] = {
            pts[i],
            pts[i + 1],
            ImVec2(pts[i + 1].x, br.y),
            ImVec2(pts[i].x, br.y)
         };
         dl->AddConvexPolyFilled(quad, tok::fx_fill_num_points, fillCol);
         dl->AddLine(pts[i], pts[i + 1], lineCol, tok::fx_spec_blur_visualizer_line_stroke);
      }

      AudioViz::End(vizFrame);

      const float blurTime = n->Param("blurTime");
      const float tilt = n->Param("tilt");
      const bool freeze = n->Param("freeze") > 0.5f;

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "blur: %s - tilt: %s%s", UiFmt::Ms(blurTime).c, UiFmt::SF2(tilt).c, freeze ? " (frozen)" : "");
         SetAudioReadout("spec blur", buf);
      }

      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using SpecBlurBodyRow = params::Row<AudioEffectNode>;
      const SpecBlurBodyRow kSpecBlurBodyRows[] = {
         SpecBlurBodyRow::Knob("blur", [](AudioEffectNode* n) { return n->ParamPtr("blurTime"); }, params::fx::blur_lo, params::fx::blur_hi).Fmt("%.0fms").Size(kKnobLarge),
         SpecBlurBodyRow::Knob("tilt", [](AudioEffectNode* n) { return n->ParamPtr("tilt"); }, -1.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         SpecBlurBodyRow::Knob("diffuse", [](AudioEffectNode* n) { return n->ParamPtr("diffusion"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         SpecBlurBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawSpecBlurBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float blurTime = n->Param("blurTime");
      const float diffusion = n->Param("diffusion");
      const bool freeze = n->Param("freeze") > 0.5f;
      const bool analog = n->Param("analog") > 0.5f;

      char stat[80];
      snprintf(stat, sizeof(stat), "%s - diff: %s%s%s", UiFmt::Ms(blurTime).c, UiFmt::F2(diffusion).c, freeze ? " - frozen" : "", analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawSpecBlurVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_spec_blur_body_audio_knob_row_layout, kKnobLarge);
         Ui::DrawKnobs(row, n, kSpecBlurBodyRows);
         row.End();
      }

      {
         AudioKnobRow row(tok::fx_spec_blur_body_audio_knob_row_layout, tok::fx_row_layout_lg, 0.0f, false);
         bool freezeBool = freeze;
         bool freezeBoolUserChanged = false;
         if (row.Checkbox("freeze##specBlurFreeze", &freezeBool, &freezeBoolUserChanged))
         {
            if (freezeBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("freeze") = freezeBool ? 1.0f : 0.0f;
         }
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##specBlurAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.End();
      }

      EndAudioBody();
   }


   // The scale in use: the node's own, or the transport's when globalKey is on.
   void KeySnapScaleRoot(AudioEffectNode* n, int& scale, int& root)
   {
      scale = std::clamp((int)lroundf(n->Param("scale")), 0, (int)MusicTime::kNumScaleTypes - 1);
      root = std::clamp((int)lroundf(n->Param("root")), 0, 11);
      if (n->Param("globalKey") > 0.5f)
      {
         scale = std::clamp(Transport::Instance().Scale(), 0, (int)MusicTime::kNumScaleTypes - 1);
         root = ((Transport::Instance().Key() % 12) + 12) % 12;
      }
   }


   // Live output spectrum on a log axis with a line at every note of the
   // scale (root brighter) - the peaks should sit on the lines.
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawKeySnapVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float loHz = 50.0f, hiHz = 5000.0f;
      const float logLo = log10f(loHz), logSpan = log10f(hiHz) - logLo;
      auto hzToX = [&](float hz) { return origin.x + std::clamp((log10f(hz) - logLo) / logSpan, 0.0f, 1.0f) * w; };

      int scale = 0, root = 0;
      KeySnapScaleRoot(n, scale, root);
      const uint32_t mask = KeySnapKernel::ScaleMask(scale, root);
      // Three opacity tiers of one neutral (white on dark, ink on light), all fainter
      // than the purple spectrum: off-scale barely there, in-scale soft, root strongest.
      const ImU32 offCol = isLight ? tok::Base(tok::col::edge_lo, tok::fx_key_snap_visualizer_edge_lo_alpha) : tok::Base(tok::col::ink_white, tok::fx_key_snap_visualizer_ink_white_alpha);
      const ImU32 noteCol = isLight ? tok::Base(tok::col::edge_lo, tok::fx_note_col_alpha_lg) : tok::Base(tok::col::ink_white, tok::fx_note_col_alpha_sm);
      const ImU32 rootCol = isLight ? tok::Base(tok::col::edge_lo, tok::fx_root_col_alpha_lg) : tok::Base(tok::col::ink_white, tok::fx_root_col_alpha_sm);
      for (int pass = 0; pass < 3; pass++) // off-scale first, root last, so root is never overdrawn
      {
         for (int midi = 24; midi <= 120; midi++)
         {
            const int pc = midi % 12;
            const int tier = pc == root ? 2 : ((mask & (1u << pc)) ? 1 : 0);
            if (tier != pass)
               continue;
            const float hz = 440.0f * powf(2.0f, (float)(midi - 69) / 12.0f);
            if (hz < loHz || hz > hiHz)
               continue;
            const float x = hzToX(hz);
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), tier == tok::fx_line_limit ? rootCol : (tier == 1 ? noteCol : offCol),
                        tier == tok::fx_key_snap_visualizer_stroke ? tok::fx_key_snap_visualizer_line_stroke_lg : tok::fx_key_snap_visualizer_line_stroke_sm);
         }
      }

      KeySnapSpectrumState& st = sKeySnapSpectrums[n];
      const int winSize = tok::fx_win_size;
      float tempBuf[1024];
      const int readCount = n->ReadSpectrumSamples(tempBuf, winSize);
      if (readCount > 0)
      {
         if (readCount >= winSize)
            st.window.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
         else
         {
            std::copy(st.window.begin() + readCount, st.window.end(), st.window.begin());
            std::copy(tempBuf, tempBuf + readCount, st.window.begin() + (winSize - readCount));
         }
      }
      float re[1024];
      float im[1024];
      for (int i = 0; i < winSize; i++)
      {
         re[i] = st.window[i] * 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(winSize - 1)));
         im[i] = 0.0f;
      }
      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);
      for (int i = 1; i < 512; i++)
      {
         const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) * (2.0f / (float)winSize);
         st.smoothed[i] = st.smoothed[i] * 0.7f + mag * 0.3f;
      }

      const double sr = 48000.0;
      const int kPlotPoints = tok::fx_plot_points;
      ImVec2 pts[kPlotPoints];
      for (int i = 0; i < kPlotPoints; i++)
      {
         const float frac = (float)i / (float)(kPlotPoints - 1);
         const float hz = powf(10.0f, logLo + frac * logSpan);
         const int bin = std::clamp((int)(hz * (float)winSize / (float)sr), 1, 511);
         const float db = 20.0f * log10f(std::max(st.smoothed[bin], 1e-4f));
         const float normY = std::clamp((db + tok::fx_key_snap_visualizer_norm_y) / tok::fx_key_snap_visualizer_ratio, 0.0f, 1.0f);
         pts[i] = ImVec2(origin.x + frac * w, br.y - normY * (h * tok::fx_key_snap_visualizer_scale) - tok::space_0_5);
      }
      const ImU32 fillCol = isLight ? tok::Base(tok::col::role_expression, tok::fx_key_snap_visualizer_role_expression_alpha) : tok::Base(tok::col::role_expression, tok::fx_key_snap_visualizer_role_expression_alpha);
      const ImU32 lineCol = isLight ? tok::Base(tok::col::role_expression, tok::fx_line_col_alpha_lg) : tok::Base(tok::col::role_expression_pale, tok::fx_key_snap_visualizer_role_expression_pale_alpha);
      for (int i = 0; i < kPlotPoints - 1; i++)
      {
         ImVec2 quad[4] = { pts[i], pts[i + 1], ImVec2(pts[i + 1].x, br.y), ImVec2(pts[i].x, br.y) };
         dl->AddConvexPolyFilled(quad, tok::fx_fill_num_points, fillCol);
         dl->AddLine(pts[i], pts[i + 1], lineCol, tok::fx_key_snap_visualizer_stroke);
      }

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%s %s", NoteNameList()[root].c_str(), MusicTime::ScaleTable(scale).name);
         SetAudioReadout("key-snap", buf);
      }
      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using KeySnapBodyRow = params::Row<AudioEffectNode>;
      const KeySnapBodyRow kKeySnapBodyRows[] = {
         KeySnapBodyRow::Knob("snap", [](AudioEffectNode* n) { return n->ParamPtr("snap"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         KeySnapBodyRow::Knob("glide", [](AudioEffectNode* n) { return n->ParamPtr("glide"); }, params::fx::glide_lo, params::fx::glide_hi).Fmt("%.0fms").Size(kKnobLarge),
         KeySnapBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawKeySnapBody(GraphNode& gn, AudioEffectNode* n)
   {
      int scale = 0, root = 0;
      KeySnapScaleRoot(n, scale, root);
      const bool globalKey = n->Param("globalKey") > 0.5f;

      char stat[80];
      snprintf(stat, sizeof(stat), "%s %s - snap %s%s", NoteNameList()[root].c_str(),
               MusicTime::ScaleTable(scale).name, UiFmt::PctOf(n->Param("snap")).c, globalKey ? " - global key" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawKeySnapVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_key_snap_body_audio_knob_row_layout, kKnobLarge);
         Ui::DrawKnobs(row, n, kKeySnapBodyRows);
         row.End();
      }


      {
         AudioKnobRow row(tok::fx_key_snap_body_audio_knob_row_layout, tok::fx_row_layout_lg, 0.0f, false);
         if (globalKey)
            ImGui::BeginDisabled();
         row.Dropdown("scale", MusicTime::ScaleTypeList(), (int)lroundf(n->Param("scale")),
                      [n](int i) { PushUndoCheckpoint(); *n->ParamPtr("scale") = (float)i; });
         row.Dropdown("root", NoteNameList(), (int)lroundf(n->Param("root")),
                      [n](int i) { PushUndoCheckpoint(); *n->ParamPtr("root") = (float)i; });
         if (globalKey)
            ImGui::EndDisabled();
         bool globalBool = globalKey;
         bool globalChanged = false;
         if (row.Checkbox("global key##keySnapGlobal", &globalBool, &globalChanged))
         {
            if (globalChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("globalKey") = globalBool ? 1.0f : 0.0f;
         }
         row.End();
      }

      EndAudioBody();
   }


   // Live output spectrum on a log axis; a marker under it shows where the
   // slide sits between A (left end) and B (right end).
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawSpectrumSlideVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float loHz = 50.0f, hiHz = 12000.0f;
      const float logLo = log10f(loHz), logSpan = log10f(hiHz) - logLo;
      const ImU32 gridCol = isLight ? tok::Base(tok::col::edge_lo, tok::fx_spectrum_slide_visualizer_edge_lo_alpha) : tok::Base(tok::col::ink_white, tok::fx_spectrum_slide_visualizer_ink_white_alpha);
      for (float hz : { 100.0f, 1000.0f, 10000.0f })
      {
         const float x = origin.x + (log10f(hz) - logLo) / logSpan * w;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), gridCol, tok::fx_spectrum_slide_visualizer_stroke);
      }

      KeySnapSpectrumState& st = sSpectrumSlideSpectrums[n];
      const int winSize = tok::fx_win_size;
      float tempBuf[1024];
      const int readCount = n->ReadSpectrumSamples(tempBuf, winSize);
      if (readCount > 0)
      {
         if (readCount >= winSize)
            st.window.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
         else
         {
            std::copy(st.window.begin() + readCount, st.window.end(), st.window.begin());
            std::copy(tempBuf, tempBuf + readCount, st.window.begin() + (winSize - readCount));
         }
      }
      float re[1024];
      float im[1024];
      for (int i = 0; i < winSize; i++)
      {
         re[i] = st.window[i] * 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(winSize - 1)));
         im[i] = 0.0f;
      }
      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);
      for (int i = 1; i < 512; i++)
      {
         const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) * (2.0f / (float)winSize);
         st.smoothed[i] = st.smoothed[i] * 0.7f + mag * 0.3f;
      }

      const double sr = 48000.0;
      const int kPlotPoints = tok::fx_plot_points;
      ImVec2 pts[kPlotPoints];
      float peak = 0.0f;
      for (int i = 0; i < kPlotPoints; i++)
      {
         const float frac = (float)i / (float)(kPlotPoints - 1);
         const float hz = powf(10.0f, logLo + frac * logSpan);
         const int bin = std::clamp((int)(hz * (float)winSize / (float)sr), 1, 511);
         const float db = 20.0f * log10f(std::max(st.smoothed[bin], 1e-4f));
         const float normY = std::clamp((db + tok::fx_spectrum_slide_visualizer_norm_y) / tok::fx_spectrum_slide_visualizer_ratio, 0.0f, 1.0f);
         pts[i] = ImVec2(origin.x + frac * w, br.y - normY * (h * tok::fx_spectrum_slide_visualizer_scale) - tok::space_1_5);
         peak = std::max(peak, normY);
      }
      if (peak < 0.02f)
         AudioViz::IdleLabel(vizFrame);
      const ImU32 fillCol = isLight ? tok::Base(tok::col::role_expression, tok::fx_spectrum_slide_visualizer_role_expression_alpha) : tok::Base(tok::col::role_expression, tok::fx_spectrum_slide_visualizer_role_expression_alpha);
      const ImU32 lineCol = isLight ? tok::Base(tok::col::role_expression, tok::fx_line_col_alpha_lg) : tok::Base(tok::col::role_expression_pale, tok::fx_spectrum_slide_visualizer_role_expression_pale_alpha);
      for (int i = 0; i < kPlotPoints - 1; i++)
      {
         ImVec2 quad[4] = { pts[i], pts[i + 1], ImVec2(pts[i + 1].x, br.y), ImVec2(pts[i].x, br.y) };
         dl->AddConvexPolyFilled(quad, tok::fx_fill_num_points, fillCol);
         dl->AddLine(pts[i], pts[i + 1], lineCol, tok::fx_spectrum_slide_visualizer_line_stroke);
      }

      // A -> B track along the bottom edge with the slide position on it.
      const float slide = std::clamp(n->Param("slide"), 0.0f, 1.0f);
      const float ty = br.y - tok::space_0_75;
      const ImU32 trackCol = isLight ? tok::Base(tok::col::edge_lo, tok::fx_track_col_alpha_lg) : tok::Base(tok::col::ink_white, tok::fx_track_col_alpha_sm);
      const ImU32 dotCol = isLight ? tok::Base(tok::col::edge_lo, tok::fx_dot_col_alpha_lg) : tok::Base(tok::col::ink_white, tok::fx_dot_col_alpha_md);
      dl->AddLine(ImVec2(origin.x + tok::space_2, ty), ImVec2(br.x - tok::space_2, ty), trackCol, tok::fx_spectrum_slide_visualizer_stroke);
      dl->AddCircleFilled(ImVec2(origin.x + tok::space_2 + slide * (w - tok::space_4), ty), tok::fx_spectrum_slide_visualizer_r, dotCol);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%s of the way from input to 'to'", UiFmt::PctOf(slide).c);
         SetAudioReadout("spectrum slide", buf);
      }
      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using SpectrumSlideBodyRow = params::Row<AudioEffectNode>;
      const SpectrumSlideBodyRow kSpectrumSlideBodyRows[] = {
         SpectrumSlideBodyRow::Knob("slide", [](AudioEffectNode* n) { return n->ParamPtr("slide"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         SpectrumSlideBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawSpectrumSlideBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float slide = n->Param("slide");
      char stat[64];
      snprintf(stat, sizeof(stat), "slide %s - in \xE2\x86\x92 to", UiFmt::PctOf(slide).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawSpectrumSlideVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_spectrum_slide_body_audio_knob_row_layout, kKnobLarge);
         Ui::DrawKnobs(row, n, kSpectrumSlideBodyRows);
         row.End();
      }

      EndAudioBody();
   }


   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
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
      const ImU32 gridCol = isLight ? tok::Base(tok::col::edge_lo, tok::fx_shape_resonator_visualizer_edge_lo_alpha) : tok::Base(tok::col::ink_white, tok::fx_shape_resonator_visualizer_ink_white_alpha);
      for (float hz : { 100.0f, 1000.0f, 10000.0f })
      {
         const float x = origin.x + (log10f(hz) - logLo) / logSpan * w;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), gridCol, tok::fx_shape_resonator_visualizer_stroke);
      }

      float freqs[ShapeResonatorKernel::kMaxModes];
      bool solving = false;
      const int count = n->ReadModeFrequencies(freqs, ShapeResonatorKernel::kMaxModes, &solving);
      const float decay = std::clamp(n->Param("decay"), 0.02f, 20.0f);
      const float damping = std::clamp(n->Param("damping"), 0.0f, 1.0f);
      const float tune = std::max(n->Param("tune"), 20.0f);
      const ImU32 lineCol = isLight ? tok::Base(tok::col::role_expression, tok::fx_shape_resonator_visualizer_role_expression_alpha) : tok::Base(tok::col::role_expression_pale, tok::fx_shape_resonator_visualizer_role_expression_pale_alpha);
      for (int i = 0; i < count; i++)
      {
         // Line height = how long this mode rings relative to the longest.
         const float t60 = decay * powf(tune / freqs[i], damping * 1.5f);
         const float rel = std::clamp(log10f(t60 / 0.02f) / log10f(20.0f / 0.02f), 0.05f, 1.0f);
         const float x = origin.x + (log10f(freqs[i]) - logLo) / logSpan * w;
         dl->AddLine(ImVec2(x, br.y - tok::space_1), ImVec2(x, br.y - tok::space_1 - rel * (h - tok::space_3_5)), lineCol, tok::fx_shape_resonator_visualizer_line_stroke);
      }
      if (solving || count == 0)
         dl->AddText(ImVec2(origin.x + tok::space_2, origin.y + tok::space_1_5), isLight ? tok::Base(tok::col::edge_lo, tok::fx_shape_resonator_visualizer_text_alpha_lg) : tok::Base(tok::col::ink_white, tok::fx_shape_resonator_visualizer_text_alpha_sm),
                     solving ? "solving shape..." : "no modes");

      AudioViz::End(vizFrame);
      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%s modes, line height = ring time", UiFmt::Count((long long)(count)).c);
         SetAudioReadout("shape modes", buf);
      }
      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using ShapeResonatorBodyRow = params::Row<AudioEffectNode>;
      const ShapeResonatorBodyRow kShapeResonatorBodyRows1[] = {
         ShapeResonatorBodyRow::Knob("tune", [](AudioEffectNode* n) { return n->ParamPtr("tune"); }, params::fx::tune_lo, params::fx::tune_hi).Fmt("%.0f Hz").Size(kKnobLarge),
         ShapeResonatorBodyRow::Knob("decay", [](AudioEffectNode* n) { return n->ParamPtr("decay"); }, params::fx::decay_3_lo, params::fx::decay_3_hi).Fmt("%.2f s").Size(kKnobLarge),
         ShapeResonatorBodyRow::Knob("damping", [](AudioEffectNode* n) { return n->ParamPtr("damping"); }, 0.0f, 1.0f).Fmt("%.2f"),
      };
      const ShapeResonatorBodyRow kShapeResonatorBodyRows2[] = {
         ShapeResonatorBodyRow::Knob("pos", [](AudioEffectNode* n) { return n->ParamPtr("pos"); }, 0.0f, 1.0f).Fmt("%.2f"),
         ShapeResonatorBodyRow::Knob("modes", [](AudioEffectNode* n) { return n->ParamPtr("modes"); }, params::fx::modes_lo, params::fx::modes_hi).Fmt("%.0f"),
         ShapeResonatorBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f"),
      };
   }

   void DrawShapeResonatorBody(GraphNode& gn, AudioEffectNode* n)
   {
      float freqs[ShapeResonatorKernel::kMaxModes];
      bool solving = false;
      const int count = n->ReadModeFrequencies(freqs, ShapeResonatorKernel::kMaxModes, &solving);
      char stat[64];
      if (count > 0)
         snprintf(stat, sizeof(stat), "%s modes - %s", UiFmt::Count((long long)(count)).c, n->geometry ? "from shape" : "default plate");
      else if (n->geometry && !solving)
         snprintf(stat, sizeof(stat), "shape has no surface (needs a mesh)");
      else
         snprintf(stat, sizeof(stat), "%s", n->geometry ? "solving shape..." : "default plate");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawShapeResonatorVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::fx_shape_resonator_body_audio_knob_row_layout, kKnobLarge);
         Ui::DrawKnobs(row, n, kShapeResonatorBodyRows1);
         row.End();
      }
      {
         AudioKnobRow row(tok::fx_shape_resonator_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kShapeResonatorBodyRows2);
         row.End();
      }

      EndAudioBody();
   }


   // ---- Convolution Reverb -----------------------------------------------
   // The impulse response actually in use (designed or loaded), as its peak
   // level over time on a 0..-72 dB scale. Read from the kernel's main-thread
   // copy, so it is the built response, not a sketch of the settings.
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
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
         const float y = origin.y + tok::space_0_75 + (float)i * tok::fx_y_ratio * (h - tok::space_1_5);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), ScopeGridCol(), tok::fx_convolution_space_visualizer_stroke);
      }
      if (count > 1 && seconds > 0.0f)
      {
         const float tick = seconds > 4.0f ? 1.0f : seconds > 1.5f ? 0.5f : 0.1f;
         for (float t = tick; t < seconds; t += tick)
         {
            const float x = origin.x + t / seconds * w;
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), tok::fx_convolution_space_visualizer_stroke);
         }
         const ImU32 lineCol = isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, tok::fx_convolution_space_visualizer_accent_alt_alpha);
         dl->PathClear();
         for (int i = 0; i < count; i++)
         {
            const float level = std::clamp(1.0f - env[i] / kFloorDb, 0.0f, 1.0f);
            dl->PathLineTo(ImVec2(origin.x + ((float)i + tok::fx_convolution_space_visualizer_inset) / (float)count * w, br.y - tok::space_0_75 - level * (h - tok::space_1_5)));
         }
         dl->PathStroke(lineCol, 0, tok::fx_convolution_space_visualizer_path_stroke);
      }
      if (building || count == 0)
         dl->AddText(ImVec2(origin.x + tok::space_2, origin.y + tok::space_1_5), isLight ? tok::Base(tok::col::edge_lo, tok::fx_convolution_space_visualizer_text_alpha_lg) : tok::Base(tok::col::ink_white, tok::fx_convolution_space_visualizer_text_alpha_sm),
                     "building space...");

      AudioViz::End(vizFrame);
      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%s, 0 to -72 dB", UiFmt::Sec(seconds).c);
         SetAudioReadout("impulse response", buf);
      }
      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using ConvolutionReverbBodyRow = params::Row<AudioEffectNode>;
      const ConvolutionReverbBodyRow kConvolutionReverbBodyRows1[] = {
         ConvolutionReverbBodyRow::Knob("size", [](AudioEffectNode* n) { return n->ParamPtr("size"); }, 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge),
         ConvolutionReverbBodyRow::Knob("decay", [](AudioEffectNode* n) { return n->ParamPtr("decay"); }, params::fx::decay_lo, ConvolutionKernel::kMaxSeconds).Fmt("%.2f s").Size(kKnobLarge),
         ConvolutionReverbBodyRow::Knob("material", [](AudioEffectNode* n) { return n->ParamPtr("material"); }, 0.0f, 1.0f).Fmt("%.2f"),
      };
      const ConvolutionReverbBodyRow kConvolutionReverbBodyRows2[] = {
         ConvolutionReverbBodyRow::Knob("distance", [](AudioEffectNode* n) { return n->ParamPtr("distance"); }, 0.0f, 1.0f).Fmt("%.2f"),
         ConvolutionReverbBodyRow::Knob("spread", [](AudioEffectNode* n) { return n->ParamPtr("spread"); }, 0.0f, 1.0f).Fmt("%.2f"),
      };
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
         snprintf(stat, sizeof(stat), "%s - %s", n->filePath.substr(slash == std::string::npos ? 0 : slash + 1).c_str(), UiFmt::Sec(seconds).c);
      }
      else
         snprintf(stat, sizeof(stat), "designed %s - %s", ConvolutionKernel::ShapeName(shape), UiFmt::Sec(seconds).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawConvolutionSpaceVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      // A loaded file is used as it is: the design controls stay in place, greyed.
      const bool designOff = hasFile && !(fileError && !fileError->empty());
      {
         AudioKnobRow row(tok::fx_convolution_reverb_body_audio_knob_row_layout, kKnobLarge);
         ImGui::BeginDisabled(designOff);
         row.Dropdown("shape", { "Room", "Hall", "Chamber", "Tunnel" }, shape, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("shape") = (float)i;
         });
         Ui::DrawKnobs(row, n, kConvolutionReverbBodyRows1);
         ImGui::EndDisabled();
         row.End();
      }
      {
         AudioKnobRow row(tok::fx_convolution_reverb_body_audio_knob_row_layout);
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
         Ui::DrawKnobs(row, n, kConvolutionReverbBodyRows2);
         ImGui::EndDisabled();
         Ui::DrawKnob(row, n, ConvolutionReverbBodyRow::Knob<&AudioEffectNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f"));
         row.End();
      }

      EndAudioBody();
   }
}
