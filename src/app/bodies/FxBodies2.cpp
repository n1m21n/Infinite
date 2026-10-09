// Modulation/spectral effect bodies + visualizers (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

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
   void DrawChorusVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

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
         const int alpha = std::clamp(150 - k * 40 + (int)(feedback * 90.0f), 60, 255);
         dl->PathClear();
         const int kNumPoints = 96;
         for (int i = 0; i < kNumPoints; i++)
         {
            const float t = (float)i / (float)(kNumPoints - 1);
            const float x = origin.x + t * w;
            const float y = midY - amp * sinf(2.0f * (float)M_PI * (t * 2.0f + phaseOffset));
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(isLight ? IM_COL32(30, 110, 230, alpha) : IM_COL32(150, 214, 255, alpha), 0, 1.5f + feedback * 1.5f);
      }

      char buf[24];
      snprintf(buf, sizeof(buf), "%.1f ms", delayMs);
      dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f), ScopeTextCol(), buf);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char hover[80];
         snprintf(hover, sizeof(hover), "%.1f ms +/- %.1f ms, %d taps, %.0f%% fb", delayMs, depthMs, taps,
                  feedback * 100.0f);
         SetAudioReadout("chorus", hover);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawChorusBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const bool analog = n->Param("analog") != 0.0f;
      char stat[112];
      if (sync)
         snprintf(stat, sizeof(stat), "%.0f taps - %s - %.0f%% wet%s", n->Param("taps"),
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), n->mix * 100.0f,
                  analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%.0f taps - %.2f Hz - %.0f%% wet%s", n->Param("taps"), n->Param("rate"),
                  n->mix * 100.0f, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawChorusVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("delay", n->ParamPtr("delay"), 1.0f, 30.0f, "%.1f ms", kKnobLarge);
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("depth", n->ParamPtr("depth"), 0.0f, 15.0f, "%.1f ms", kKnobLarge);
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
         AudioKnobRow row(3);
         row.index = 1;
         row.Knob("feedback", n->ParamPtr("feedback"), 0.0f, 0.9f, "%.2f", kKnobLarge);
         row.index = 2;
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
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
         AudioKnobRow row(3, 20.0f, 8.0f, false);
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
   void DrawFlangerVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      const float delayMs = n->Param("delay");
      const float depthMs = n->Param("depth");
      const float feedback = n->Param("feedback");
      const float amp = std::clamp(depthMs / 10.0f, 0.08f, 1.0f) * (h * 0.5f - 10.0f);
      const float rippleAmp = std::fabs(feedback) * (h * 0.16f);
      const float rippleFreq = 3.0f + std::fabs(feedback) * 5.0f;

      // Dry reference - a flat carrier ripple with no modulation envelope.
      dl->PathClear();
      for (int i = 0; i < 96; i++)
      {
         const float t = (float)i / 95.0f;
         const float x = origin.x + t * w;
         const float y = midY - (h * 0.1f) * sinf(2.0f * (float)M_PI * t * rippleFreq);
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
         dl->PathStroke(isLight ? IM_COL32(30, 110, 230, alpha) : IM_COL32(150, 214, 255, alpha), 0, 1.4f + std::fabs(feedback) * 2.2f);
      };
      drawWet(0.0f, 235);
      drawWet(n->Param("spread") * 0.5f, 120);

      char buf[24];
      snprintf(buf, sizeof(buf), "%.2f ms", delayMs);
      dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f), ScopeTextCol(), buf);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char hover[80];
         snprintf(hover, sizeof(hover), "%.2f ms +/- %.2f ms, %+.0f%% fb", delayMs, depthMs, feedback * 100.0f);
         SetAudioReadout("flanger", hover);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawFlangerBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const bool analog = n->Param("analog") != 0.0f;
      char stat[112];
      if (sync)
         snprintf(stat, sizeof(stat), "%s - %.0f%% fb - %.0f%% wet%s",
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), n->Param("feedback") * 100.0f,
                  n->mix * 100.0f, analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%.1f ms - %.0f%% fb - %.0f%% wet%s", n->Param("delay"),
                  n->Param("feedback") * 100.0f, n->mix * 100.0f, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFlangerVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("delay", n->ParamPtr("delay"), 0.2f, 15.0f, "%.2f ms", kKnobLarge);
         row.Knob("depth", n->ParamPtr("depth"), 0.0f, 10.0f, "%.2f ms", kKnobLarge);
         row.Knob("feedback", n->ParamPtr("feedback"), -0.95f, 0.95f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         // 3 cells: rate | spread | mix - mix stays bottom-right of the
         // last knob row (P4). row.index is set explicitly before each
         // call so the *visual* cell a control lands in can differ from
         // its *draw order* - see the matching comment in DrawChorusBody.
         // AddSyncedRateCell is called last, exactly as it was before (via
         // AddRateModeCells), so rate's ordinal is unaffected.
         AudioKnobRow row(3);
         row.index = 1;
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.index = 2;
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
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
         AudioKnobRow row(3, 20.0f, 8.0f, false);
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
   void DrawPhaserVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float logMin = log10f(50.0f), logMax = log10f(8000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(std::max(hz, 1.0f)) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };
      for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
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
            const float y = br.y - 6.0f - mag * (h - 16.0f);
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(isLight ? IM_COL32(30, 110, 230, alpha) : IM_COL32(150, 214, 255, alpha), 0, 1.6f);
      };
      drawTrace(1.0f, 235);
      if (spread > 0.01f)
         drawTrace(powf(2.0f, spread * 0.5f), 110);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[80];
         snprintf(buf, sizeof(buf), "%.0f Hz +/- %.1f oct, %d notches", cutoff, depth, notches);
         SetAudioReadout("phaser", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawPhaserBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const bool analog = n->Param("analog") != 0.0f;
      char stat[112];
      if (sync)
         snprintf(stat, sizeof(stat), "%.0f-stage - %s - %.0f%% wet%s", n->Param("order"),
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), n->mix * 100.0f,
                  analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%.0f-stage - %.0f Hz - %.0f%% wet%s", n->Param("order"), n->Param("cutoff"),
                  n->mix * 100.0f, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawPhaserVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("cutoff", n->ParamPtr("cutoff"), 100.0f, 4000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("depth", n->ParamPtr("depth"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("order", n->ParamPtr("order"), 2.0f, (float)PhaserKernel::kMaxStages, "%.0f", kKnobLarge);
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
         AudioKnobRow row(3);
         row.index = 1;
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.index = 2;
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
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
         AudioKnobRow row(3, 20.0f, 8.0f, false);
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
   void DrawBitcrushVisualizer(AudioEffectNode* n, double sampleRate)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float rateHz = std::max(100.0f, n->Param("rate"));
      const float bits = std::max(1.0f, n->Param("bits"));
      const float levels = powf(2.0f, bits) - 1.0f;
      const float holdSamples = std::max(1.0f, (float)sampleRate / rateHz);
      const float cycleSamples = (float)sampleRate; // one second of illustrative arch at 1 Hz
      const float top = origin.y + 8.0f;
      const float baseY = br.y - 4.0f;

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
                            isLight ? tok::U32(tok::pal::c_6E50D250) : tok::U32(tok::pal::c_966EE66E));

      dl->PathClear();
      for (int i = 0; i < kNumSteps; i++)
         dl->PathLineTo(pts[i]);
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_7850E6F5) : tok::U32(tok::pal::c_BEA0FFF5), 0, 1.8f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      char buf[48];
      snprintf(buf, sizeof(buf), "%.0f Hz - %.0f bit", rateHz, bits);
      if (ImGui::IsMouseHoveringRect(origin, br))
         SetAudioReadout("bitcrush", buf);

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawBitcrushBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[96];
      snprintf(stat, sizeof(stat), "%.0f Hz - %.0f bit - %.0f%% wet%s", n->Param("rate"), n->Param("bits"),
               n->mix * 100.0f, analog ? " - analog" : "");

      const double sampleRate =
         AudioEngine::Instance().SampleRate() > 0.0 ? AudioEngine::Instance().SampleRate() : 44100.0;

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawBitcrushVisualizer(n, sampleRate);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(3);
      // Labelled "downsample" (not "rate") to match the BLEASS-style
      // reference's own naming - this is the same sample-and-hold rate
      // param as before (BitcrushKernel::kRateHz), a cosmetic relabel only.
      row.Knob("downsample", n->ParamPtr("rate"), 200.0f, 44100.0f, "%.0f Hz", kKnobLarge);
      row.Knob("bits", n->ParamPtr("bits"), 1.0f, 16.0f, "%.0f", kKnobLarge);
      row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();

      {
         AudioKnobRow row(3, 20.0f, 8.0f, false);
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##bitcrushAnalog", &analogBool, &analogBoolUserChanged))
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


   void DrawTransientShaperVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float attackDb = n->Param("attack");
      const float sustainDb = n->Param("sustain");

      const float kWindowSec = 0.4f;
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
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE63C) : tok::U32(tok::pal::c_96D6FF3C), 0, 1.5f);

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
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      const float transientAmount = std::clamp(n->ExtraMeterValue(0), 0.0f, 1.0f);
      dl->AddCircleFilled(ImVec2(peakX, peakY), 3.0f + transientAmount * 4.0f,
                          isLight ? tok::U32(tok::pal::c_E67814E6) : tok::U32(tok::pal::c_FFC478E6));

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[80];
         snprintf(buf, sizeof(buf), "attack %+.1f dB - sustain %+.1f dB - peak %+.1f dB", attackDb, sustainDb, peakDb);
         SetAudioReadout("transient shaper", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawTransientShaperBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "atk %+.1f dB - sus %+.1f dB", n->Param("attack"), n->Param("sustain"));

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawTransientShaperVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("attack", n->ParamPtr("attack"), -24.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.Knob("sustain", n->ParamPtr("sustain"), -24.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
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
      const float h = 30.0f;
      const float gap = 3.0f;
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
         ImGui::InvisibleButton("step", ImVec2(cellW, h));
         if (ImGui::IsItemClicked())
         {
            PushUndoCheckpoint();
            *n->ParamPtr("gateMask") = (float)(mask ^ (1 << i));
         }
         const bool on = (mask & (1 << i)) != 0;
         const bool hovered = ImGui::IsItemHovered();
         const bool isLight = IsThemeLight();
         const ImU32 col = on ? (isLight ? IM_COL32(30, 110, 230, hovered ? 255 : 220) : IM_COL32(120, 200, 255, hovered ? 255 : 215))
                               : (isLight ? IM_COL32(220, 225, 235, hovered ? 245 : 215) : IM_COL32(56, 60, 74, hovered ? 210 : 160));
         dl->AddRectFilled(p0, p1, col, 2.0f);
         dl->AddRect(p0, p1, ScopeBorderCol(), 2.0f, 0, 1.0f);
         ImGui::PopID();
      }

      ImGui::SetCursorScreenPos(rowOrigin);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawStutterBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const int steps = std::clamp((int)(n->Param("steps") + 0.5f), 2, StutterKernel::kMaxGateSteps);

      char stat[80];
      if (sync)
         snprintf(stat, sizeof(stat), "%s - %d steps", MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)),
                  steps);
      else
         snprintf(stat, sizeof(stat), "%.0f ms - %d steps", n->Param("timeMs"), steps);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      DrawStutterGateGrid(n, steps);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         // "sync" folded into the row itself (was a standalone checkbox
         // line below the knobs) - one compact row instead of a row plus a
         // line, and rate/time already live right next to the toggle that
         // picks which of the two is showing.
         AudioKnobRow row(3);
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
            row.Knob("time", n->ParamPtr("timeMs"), 20.0f, 2000.0f, "%.0f ms", kKnobLarge);
         }
         row.Knob("steps", n->ParamPtr("steps"), 2.0f, (float)StutterKernel::kMaxGateSteps, "%.0f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }


   // ---- Ring Mod ---------------------------------------------------------
   // The modulator waveform itself, one static cycle - the ring-modding
   // oscillator's shape is the node's whole identity (its frequency and
   // waveform, not the input signal, define the sidebands).
   void DrawRingModVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

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
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[32];
         snprintf(buf, sizeof(buf), "%.0f Hz", n->Param("freq"));
         SetAudioReadout("ring mod", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawRingModBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[80];
      snprintf(stat, sizeof(stat), "%.0f Hz - %.0f%% wet%s", n->Param("freq"), n->mix * 100.0f,
               analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawRingModVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
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
         row.Knob("freq", n->ParamPtr("freq"), 1.0f, 5000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         // Same tight checkbox-row pattern as Pitch Shift above and as
         // Dynamics/SpecBlur/Switcher already use: maxDia 20 (checkbox frame
         // height) + hasCaptions=false, so the row doesn't reserve a full
         // knob-height-plus-caption-strip below a control whose label prints
         // inline rather than underneath it.
         AudioKnobRow row(3, 20.0f, 8.0f, false);
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
   void DrawFrequencyShifterVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float shift = n->Param("shift");
      const float spread = n->Param("spread");
      const float midX = origin.x + w * 0.5f;
      const float topY = origin.y + h * 0.30f;
      const float botY = origin.y + h * 0.72f;

      // Center frequency axis & rows
      dl->AddLine(ImVec2(origin.x + 12.0f, topY), ImVec2(br.x - 12.0f, topY), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(origin.x + 12.0f, botY), ImVec2(br.x - 12.0f, botY), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(midX, origin.y + 6.0f), ImVec2(midX, br.y - 6.0f), ScopeMidLineCol(), 1.0f);

      // Partials comb: 7 evenly spaced input partials
      const int kNumPartials = 7;
      const float kSpacing = w * 0.085f;
      const float kMaxShiftPix = w * 0.28f;
      const int range = std::clamp((int)(n->Param("range") + 0.5f), 0, 1);
      const float maxShiftSpan = (range == 0) ? 100.0f : 10000.0f;
      const float shiftFrac = std::clamp(shift / maxShiftSpan, -1.0f, 1.0f);
      const float dxL = shiftFrac * kMaxShiftPix;

      // Arrow & shifted partial color: amber for up-shift, coral/magenta for down-shift, blue-grey for zero
      const ImU32 arrowCol = (shift > 0.5f)
         ? (isLight ? tok::U32(tok::pal::c_D27814F0) : tok::U32(tok::pal::c_FFB946E6))
         : ((shift < -0.5f) ? (isLight ? tok::U32(tok::pal::c_DC2846F0) : tok::U32(tok::pal::c_FF6E82E6))
                            : (isLight ? tok::U32(tok::pal::c_5A6982C8) : tok::U32(tok::pal::c_8CA0C3B4)));

      for (int i = 0; i < kNumPartials; i++)
      {
         const float xIn = midX + (float)(i - kNumPartials / 2) * kSpacing;
         const float xOutL = xIn + dxL;

         // Input partial tick (upper row)
         dl->AddLine(ImVec2(xIn, topY - 7.0f), ImVec2(xIn, topY + 3.0f),
                     isLight ? tok::U32(tok::pal::c_465A82DC) : tok::U32(tok::pal::c_96AACDD2), 1.5f);

         // Shifted partial tick (lower row)
         dl->AddLine(ImVec2(xOutL, botY - 3.0f), ImVec2(xOutL, botY + 7.0f), arrowCol, 1.8f);

         // Connecting arrow from input to shifted partial
         dl->AddLine(ImVec2(xIn, topY + 3.0f), ImVec2(xOutL, botY - 3.0f), arrowCol, 1.2f);
         if (std::fabs(dxL) > 1.5f || std::fabs(topY - botY) > 5.0f)
         {
            const float arrowSz = 3.0f;
            dl->AddTriangleFilled(ImVec2(xOutL, botY - 2.0f),
                                  ImVec2(xOutL - arrowSz, botY - 2.0f - arrowSz * 1.5f),
                                  ImVec2(xOutL + arrowSz, botY - 2.0f - arrowSz * 1.5f),
                                  arrowCol);
         }

         if (spread > 0.5f)
         {
            const float shiftRFrac = std::clamp((shift + spread) / maxShiftSpan, -1.0f, 1.0f);
            const float dxR = shiftRFrac * kMaxShiftPix;
            const float xOutR = xIn + dxR;
            const ImU32 spreadCol = (shift + spread > 0.5f)
               ? (isLight ? tok::U32(tok::pal::c_D28C288C) : tok::U32(tok::pal::c_FFCD6E78))
               : ((shift + spread < -0.5f) ? (isLight ? tok::U32(tok::pal::c_D250648C) : tok::U32(tok::pal::c_FF8CA078))
                                           : (isLight ? tok::U32(tok::pal::c_5A698278) : tok::U32(tok::pal::c_8CA0C364)));
            dl->AddLine(ImVec2(xOutR, botY - 2.0f), ImVec2(xOutR, botY + 6.0f), spreadCol, 1.2f);
         }
      }

      char valBuf[64];
      auto FmtFreq = [](float f, char* b, size_t sz, bool withSign) {
         if (std::fabs(f) >= 1000.0f)
            snprintf(b, sz, withSign ? "%+.2f kHz" : "%.2f kHz", f / 1000.0f);
         else
            snprintf(b, sz, withSign ? "%+.0f Hz" : "%.0f Hz", f);
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
      dl->AddText(ImVec2(br.x - textSz.x - 10.0f, origin.y + 8.0f),
                  isLight ? tok::U32(tok::pal::c_283750F0) : tok::U32(tok::pal::c_E6EEFFF0), valBuf);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         SetAudioReadout("freq shift", valBuf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawFrequencyShifterBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[80];
      const float shiftVal = n->Param("shift");
      if (std::fabs(shiftVal) >= 1000.0f)
         snprintf(stat, sizeof(stat), "%+.2f kHz - %.0f%% fb - %.0f%% wet%s",
                  shiftVal / 1000.0f, n->Param("feedback") * 100.0f, n->mix * 100.0f, analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%+.0f Hz - %.0f%% fb - %.0f%% wet%s",
                  shiftVal, n->Param("feedback") * 100.0f, n->mix * 100.0f, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFrequencyShifterVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         const int range = std::clamp((int)(n->Param("range") + 0.5f), 0, 1);
         const float shiftLimit = (range == 0) ? 100.0f : 10000.0f;
         static const std::vector<std::string> kRangeOptions = { "fine +/-100", "wide +/-10k" };
         const char* fmt = (range == 0) ? "%+.1f Hz" : (std::fabs(shiftVal) >= 1000.0f ? "%+.2f kHz" : "%+.0f Hz");

         AudioKnobRow row(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         row.DropdownKnob("fsRange", kRangeOptions, range,
                          [n](int i) {
                             PushUndoCheckpoint();
                             *n->ParamPtr("range") = (float)i;
                          },
                          "shift", n->ParamPtr("shift"), -shiftLimit, shiftLimit, fmt, false, kKnobLarge);
         row.Knob("feedback", n->ParamPtr("feedback"), 0.0f, 0.95f, "%.2f", kKnobLarge);
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 100.0f, "%.1f Hz", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewSpread100);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         AudioKnobRow row(4, 20.0f, 8.0f, false);
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
   void DrawTremoloVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

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
                            isLight ? tok::U32(tok::pal::c_1E6EE62D) : tok::U32(tok::pal::c_96D6FF3C));

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
         dl->PathLineTo(envPts[i]);
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      if (stereoPhaseCycles > 0.0f)
      {
         dl->PathClear();
         for (int i = 0; i < kNumPoints; i++)
         {
            const double phase = (double)i * previewOsc.phaseInc + stereoPhaseCycles;
            const float x = origin.x + (float)i / (float)(kNumPoints - 1) * w;
            dl->PathLineTo(ImVec2(x, envelopeY(phase)));
         }
         dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE66E) : tok::U32(tok::pal::c_96D6FF6E), 0, 1.4f);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[32];
         const bool sync = n->Param("sync") != 0.0f;
         if (sync)
         {
            const int rateDiv = (int)(n->Param("rateDiv") + 0.5f);
            const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
            const double rateHz = MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, bpm);
            snprintf(buf, sizeof(buf), "%s (%.1f Hz)", MusicTime::RateDivisionName(rateDiv), rateHz);
         }
         else
            snprintf(buf, sizeof(buf), "%.1f Hz", n->Param("rate"));
         SetAudioReadout("tremolo", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawTremoloBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%.0f%% depth - %.0f%% wet", n->Param("depth") * 100.0f, n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawTremoloVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         // Selector column left (shape v), knobs right (depth, stereo).
         // The dropdown is a discrete param (numbered from a hash of its
         // label - DiscreteParamSlot) rather than a gParamCounter ordinal,
         // so calling it first here costs "depth"/"stereo" nothing - they
         // still get the same ordinals (0, 1) they always have.
         AudioKnobRow row(3);
         const int shape = (int)(n->Param("shape") + 0.5f);
         static const std::vector<std::string> kShapeNames = SynthModes::WaveformTypeSubset(
            { SynthModes::kWaveSine, SynthModes::kWaveTriangle, SynthModes::kWaveSquare, SynthModes::kWaveRampDown });
         row.Dropdown("shape", kShapeNames, shape, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("shape") = (float)i;
         });
         row.Knob("depth", n->ParamPtr("depth"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("stereo", n->ParamPtr("stereoPhase"), 0.0f, 180.0f, "%.0f deg", kKnobLarge);
         row.End();
      }
      {
         // Selector column left (mode v, rate), mix bottom-right (P4). Rate
         // is still called before mix, exactly as before, so mix's ordinal
         // doesn't move.
         AudioKnobRow row(3);
         AddRateModeCells(row, n, "sync to tempo##tremoloSync", 0.05f, 20.0f);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }


   // ---- Formant Filter -----------------------------------------------------
   // The three formant resonators' frequencies as markers on a log axis -
   // the picture a filter's own frequency-response curve would give, but
   // for three narrow bandpass peaks rather than one broad shape, so three
   // labelled markers reads more clearly than a busy triple-peaked curve.
   void DrawFormantFilterVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float logMin = log10f(100.0f), logMax = log10f(5000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(hz) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };

      for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }

      const FormantDsp::Formants f = FormantDsp::VowelFormants(n->Param("vowel"));
      const float formants[3] = { f.f1, f.f2, f.f3 };
      const float weights[3] = { 1.0f, 0.7f, 0.5f };
      static const char* kFormantLabels[3] = { "F1", "F2", "F3" };
      for (int i = 0; i < 3; i++)
      {
         const float x = freqToX(formants[i]);
         const float peakY = br.y - 6.0f - weights[i] * (h - 20.0f);
         dl->AddLine(ImVec2(x, br.y - 6.0f), ImVec2(x, peakY),
                     isLight ? tok::U32(tok::pal::c_1E6EE6E6) : tok::U32(tok::pal::c_96D6FFDC), 3.0f);
         dl->AddText(ImVec2(x - 7.0f, peakY - 14.0f), ScopeTextCol(), kFormantLabels[i]);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "F1 %.0f - F2 %.0f - F3 %.0f Hz", f.f1, f.f2, f.f3);
         SetAudioReadout("formant filter", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawFormantFilterBody(GraphNode& gn, AudioEffectNode* n)
   {
      static const char* kVowelNames[] = { "A", "E", "I", "O", "U" };
      const int vowelIdx = std::clamp((int)(n->Param("vowel") + 0.5f), 0, 4);

      char stat[64];
      snprintf(stat, sizeof(stat), "%s - Q %.1f - %.0f%% wet", kVowelNames[vowelIdx], n->Param("q"), n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFormantFilterVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(3);
      row.Knob("vowel", n->ParamPtr("vowel"), 0.0f, 4.0f, "%.2f", kKnobLarge);
      row.Knob("Q", n->ParamPtr("q"), 2.0f, 30.0f, "%.1f", kKnobLarge);
      row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();

      EndAudioBody();
   }


   // ---- Resonator Bank -------------------------------------------------
   // Log-frequency pole-distribution visualizer: stems showing each active
   // resonator's center frequency, Q height and stereo pan coloring.
   void DrawResonatorBankVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float logMin = log10f(20.0f), logMax = log10f(20000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(std::max(hz, 20.0f)) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };

      for (float hz : { 100.0f, 1000.0f, 10000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
         char hzStr[16];
         if (hz >= 1000.0f)
            snprintf(hzStr, sizeof(hzStr), "%.0fk", hz / 1000.0f);
         else
            snprintf(hzStr, sizeof(hzStr), "%.0f", hz);
         dl->AddText(ImVec2(x + 2.0f, br.y - 12.0f), ScopeTextCol(), hzStr);
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
         const float normH = std::clamp(log10f(q_i) / log10f(500.0f), 0.1f, 0.9f);
         const float peakY = br.y - 14.0f - normH * (h - 22.0f);
         // Damped partials fade as well as shrink, so raising damp visibly
         // tilts the whole spectrum down and out to the right.
         const float alphaScale = std::clamp(decay_i / std::max(decay, 0.0001f), 0.25f, 1.0f);

         const float pos = (numPoles > 1) ? ((float)i / (float)(numPoles - 1) - 0.5f) * spread + 0.5f : 0.5f;
         const ImU32 baseCol = (pos < 0.45f)
            ? (isLight ? tok::U32(tok::pal::c_1E6EE6DC) : tok::U32(tok::pal::c_6EC8FFDC))
            : ((pos > 0.55f)
               ? (isLight ? tok::U32(tok::pal::c_E65A28DC) : tok::U32(tok::pal::c_FF8C5ADC))
               : (isLight ? tok::U32(tok::pal::c_28B46EDC) : tok::U32(tok::pal::c_6EE6A0DC)));
         const unsigned baseAlpha = (baseCol >> IM_COL32_A_SHIFT) & 0xFF;
         const ImU32 stemCol = (baseCol & ~IM_COL32_A_MASK) |
            (((unsigned)(baseAlpha * alphaScale)) << IM_COL32_A_SHIFT);

         dl->AddLine(ImVec2(x, br.y - 14.0f), ImVec2(x, peakY), stemCol, 2.0f);
         dl->AddCircleFilled(ImVec2(x, peakY), 3.0f, stemCol);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         static const char* kStructNames[] = { "Harmonic", "Odd", "Chord", "Metallic" };
         char buf[80];
         if (damp > 0.001f)
            snprintf(buf, sizeof(buf), "root %.0f Hz - %s - %d poles - %.0f%% damp", rootFreq, kStructNames[structure], numPoles, damp * 100.0f);
         else
            snprintf(buf, sizeof(buf), "root %.0f Hz - %s - %d poles", rootFreq, kStructNames[structure], numPoles);
         SetAudioReadout("resonator bank", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawResonatorBankBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float rootFreq = n->Param("rootFreq");
      const int structure = std::clamp((int)std::round(n->Param("structure")), 0, 3);
      const int poles = std::clamp((int)std::round(n->Param("poles")), 1, 16);
      const float decay = n->Param("decay");
      const bool analog = n->Param("analog") != 0.0f;
      const float damp = std::clamp(n->Param("damp"), 0.0f, 1.0f);

      static const char* kStructNames[] = { "Harmonic", "Odd", "Chord", "Metallic" };
      char stat[96];
      if (damp > 0.001f)
         snprintf(stat, sizeof(stat), "%.0f Hz - %s - %d poles - %.0f%% damp%s", rootFreq, kStructNames[structure], poles, damp * 100.0f, analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%.0f Hz - %s - %d poles%s", rootFreq, kStructNames[structure], poles, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawResonatorBankVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4, kKnobLarge);
         static const std::vector<std::string> kStructs = { "harmonic", "odd", "chord", "metallic" };
         row.Dropdown("struct", kStructs, structure, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("structure") = (float)i;
         });
         row.Knob("root", n->ParamPtr("rootFreq"), 20.0f, 2000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("poles", n->ParamPtr("poles"), 1.0f, 16.0f, "%.0f", kKnobLarge);
         row.Knob("decay", n->ParamPtr("decay"), 0.05f, 10.0f, "%.2fs", kKnobLarge);
         row.End();
      }

      {
         AudioKnobRow row(4, kKnobLarge);
         row.Knob("scatter", n->ParamPtr("scatter"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("damp", n->ParamPtr("damp"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         AudioKnobRow row(4, 20.0f, 8.0f, false);
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
   void DrawCycleShaperVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeGridCol(), 1.0f);

      const int waveform = std::clamp((int)std::round(n->Param("waveform")), 0, 2);
      const float thresholdDb = std::clamp(n->Param("threshold"), -60.0f, -12.0f);
      const float threshLinear = powf(10.0f, thresholdDb / 20.0f);
      const float threshY1 = midY - threshLinear * (h * 0.45f);
      const float threshY2 = midY + threshLinear * (h * 0.45f);
      const ImU32 threshCol = isLight ? tok::U32(tok::pal::c_B4505064) : tok::U32(tok::pal::c_E65A5A64);
      dl->AddLine(ImVec2(origin.x, threshY1), ImVec2(br.x, threshY1), threshCol, 1.0f);
      dl->AddLine(ImVec2(origin.x, threshY2), ImVec2(br.x, threshY2), threshCol, 1.0f);

      const ImU32 waveCol = isLight ? tok::U32(tok::pal::c_148CC8F0) : tok::U32(tok::pal::c_50C8FFF0);
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
               y = (2.0f / (float)M_PI) * asinf(std::clamp(s, -1.0f, 1.0f));
               break;
         }

         pts[i] = ImVec2(origin.x + t * w, midY - y * (h * 0.42f));
      }

      for (int i = 0; i < kPlotPoints - 1; i++)
         dl->AddLine(pts[i], pts[i + 1], waveCol, 2.0f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         static const char* kWaves[] = { "sine", "square", "triangle" };
         char buf[64];
         snprintf(buf, sizeof(buf), "wave: %s - thresh: %.0fdB", kWaves[waveform], thresholdDb);
         SetAudioReadout("cycle shaper", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawCycleShaperBody(GraphNode& gn, AudioEffectNode* n)
   {
      const int waveform = std::clamp((int)std::round(n->Param("waveform")), 0, 2);
      const float thresholdDb = n->Param("threshold");
      const int smooth = std::clamp((int)std::round(n->Param("smooth")), 0, 32);
      const bool analog = n->Param("analog") != 0.0f;

      static const char* kWaves[] = { "sine", "square", "triangle" };
      char stat[80];
      snprintf(stat, sizeof(stat), "%s - %.0fdB - %dsmp%s", kWaves[waveform], thresholdDb, smooth, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawCycleShaperVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4);
         static const std::vector<std::string> kWaveNames = SynthModes::WaveformTypeSubset(
            { SynthModes::kWaveSine, SynthModes::kWaveSquare, SynthModes::kWaveTriangle });
         row.Dropdown("shape", kWaveNames, waveform, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("waveform") = (float)i;
         });
         row.Knob("thresh", n->ParamPtr("threshold"), -60.0f, -12.0f, "%.0fdB", kKnobLarge);
         row.Knob("smooth", n->ParamPtr("smooth"), 0.0f, 32.0f, "%.0fsmp", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         AudioKnobRow row(4, 20.0f, 8.0f, false);
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


   void DrawSpecBlurVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      // Grid lines: 100Hz, 1kHz, 10kHz
      const double sr = 48000.0;
      auto hzToX = [&](float hz) -> float {
         const float norm = (log10f(std::max(hz, 20.0f)) - log10f(20.0f)) / (log10f(20000.0f) - log10f(20.0f));
         return origin.x + std::clamp(norm, 0.0f, 1.0f) * w;
      };

      dl->AddLine(ImVec2(hzToX(100.0f), origin.y), ImVec2(hzToX(100.0f), br.y), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(hzToX(1000.0f), origin.y), ImVec2(hzToX(1000.0f), br.y), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(hzToX(10000.0f), origin.y), ImVec2(hzToX(10000.0f), br.y), ScopeGridCol(), 1.0f);

      SpecBlurSpectrumState& st = sSpecBlurSpectrums[n];
      const int winSize = 1024;
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
      for (int i = 0; i < kPlotPoints; i++)
      {
         const float frac = (float)i / (float)(kPlotPoints - 1);
         const float hz = 20.0f * powf(1000.0f, frac);
         const int bin = std::clamp((int)(hz * (float)winSize / (float)sr), 1, 511);
         const float mag = st.smoothed[bin];
         const float db = 20.0f * log10f(std::max(mag, 1e-4f));
         // Map -60 dB .. 0 dB to height
         const float normY = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
         pts[i] = ImVec2(origin.x + frac * w, br.y - normY * (h * 0.9f) - 2.0f);
      }

      const ImU32 fillCol = isLight ? tok::U32(tok::pal::c_8C46DC2D) : tok::U32(tok::pal::c_B464FF2D);
      const ImU32 lineCol = isLight ? tok::U32(tok::pal::c_963CE6F0) : tok::U32(tok::pal::c_C882FFF0);

      for (int i = 0; i < kPlotPoints - 1; i++)
      {
         ImVec2 quad[4] = {
            pts[i],
            pts[i + 1],
            ImVec2(pts[i + 1].x, br.y),
            ImVec2(pts[i].x, br.y)
         };
         dl->AddConvexPolyFilled(quad, 4, fillCol);
         dl->AddLine(pts[i], pts[i + 1], lineCol, 2.0f);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      const float blurTime = n->Param("blurTime");
      const float tilt = n->Param("tilt");
      const bool freeze = n->Param("freeze") > 0.5f;

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "blur: %.0fms - tilt: %+.2f%s", blurTime, tilt, freeze ? " (frozen)" : "");
         SetAudioReadout("spec blur", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawSpecBlurBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float blurTime = n->Param("blurTime");
      const float tilt = n->Param("tilt");
      const float diffusion = n->Param("diffusion");
      const bool freeze = n->Param("freeze") > 0.5f;
      const bool analog = n->Param("analog") > 0.5f;

      char stat[80];
      snprintf(stat, sizeof(stat), "%.0fms - diff: %.2f%s%s", blurTime, diffusion, freeze ? " - frozen" : "", analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawSpecBlurVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4, kKnobLarge);
         row.Knob("blur", n->ParamPtr("blurTime"), 10.0f, 5000.0f, "%.0fms", kKnobLarge);
         row.Knob("tilt", n->ParamPtr("tilt"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("diffuse", n->ParamPtr("diffusion"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         AudioKnobRow row(4, 20.0f, 0.0f, false);
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
   void DrawKeySnapVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float loHz = 50.0f, hiHz = 5000.0f;
      const float logLo = log10f(loHz), logSpan = log10f(hiHz) - logLo;
      auto hzToX = [&](float hz) { return origin.x + std::clamp((log10f(hz) - logLo) / logSpan, 0.0f, 1.0f) * w; };

      int scale = 0, root = 0;
      KeySnapScaleRoot(n, scale, root);
      const uint32_t mask = KeySnapKernel::ScaleMask(scale, root);
      // Three opacity tiers of one neutral (white on dark, ink on light), all fainter
      // than the purple spectrum: off-scale barely there, in-scale soft, root strongest.
      const ImU32 offCol = isLight ? tok::U32(tok::pal::c_28283C0E) : tok::U32(tok::pal::c_FFFFFF0A);
      const ImU32 noteCol = isLight ? tok::U32(tok::pal::c_28283C3C) : tok::U32(tok::pal::c_FFFFFF30);
      const ImU32 rootCol = isLight ? tok::U32(tok::pal::c_28283C82) : tok::U32(tok::pal::c_FFFFFF6E);
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
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), tier == 2 ? rootCol : (tier == 1 ? noteCol : offCol),
                        tier == 2 ? 1.5f : 1.0f);
         }
      }

      KeySnapSpectrumState& st = sKeySnapSpectrums[n];
      const int winSize = 1024;
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
      const int kPlotPoints = 160;
      ImVec2 pts[kPlotPoints];
      for (int i = 0; i < kPlotPoints; i++)
      {
         const float frac = (float)i / (float)(kPlotPoints - 1);
         const float hz = powf(10.0f, logLo + frac * logSpan);
         const int bin = std::clamp((int)(hz * (float)winSize / (float)sr), 1, 511);
         const float db = 20.0f * log10f(std::max(st.smoothed[bin], 1e-4f));
         const float normY = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
         pts[i] = ImVec2(origin.x + frac * w, br.y - normY * (h * 0.9f) - 2.0f);
      }
      const ImU32 fillCol = isLight ? tok::U32(tok::pal::c_8C46DC2D) : tok::U32(tok::pal::c_B464FF2D);
      const ImU32 lineCol = isLight ? tok::U32(tok::pal::c_963CE6F0) : tok::U32(tok::pal::c_C882FFF0);
      for (int i = 0; i < kPlotPoints - 1; i++)
      {
         ImVec2 quad[4] = { pts[i], pts[i + 1], ImVec2(pts[i + 1].x, br.y), ImVec2(pts[i].x, br.y) };
         dl->AddConvexPolyFilled(quad, 4, fillCol);
         dl->AddLine(pts[i], pts[i + 1], lineCol, 2.0f);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%s %s", NoteNameList()[root].c_str(), MusicTime::ScaleTable(scale).name);
         SetAudioReadout("key-snap", buf);
      }
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawKeySnapBody(GraphNode& gn, AudioEffectNode* n)
   {
      int scale = 0, root = 0;
      KeySnapScaleRoot(n, scale, root);
      const bool globalKey = n->Param("globalKey") > 0.5f;

      char stat[80];
      snprintf(stat, sizeof(stat), "%s %s - snap %.0f%%%s", NoteNameList()[root].c_str(),
               MusicTime::ScaleTable(scale).name, n->Param("snap") * 100.0f, globalKey ? " - global key" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawKeySnapVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3, kKnobLarge);
         row.Knob("snap", n->ParamPtr("snap"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("glide", n->ParamPtr("glide"), 0.0f, 500.0f, "%.0fms", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }


      {
         AudioKnobRow row(3, 20.0f, 0.0f, false);
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
   void DrawSpectrumSlideVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

      const float loHz = 50.0f, hiHz = 12000.0f;
      const float logLo = log10f(loHz), logSpan = log10f(hiHz) - logLo;
      const ImU32 gridCol = isLight ? tok::U32(tok::pal::c_28283C1C) : tok::U32(tok::pal::c_FFFFFF16);
      for (float hz : { 100.0f, 1000.0f, 10000.0f })
      {
         const float x = origin.x + (log10f(hz) - logLo) / logSpan * w;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), gridCol, 1.0f);
      }

      KeySnapSpectrumState& st = sSpectrumSlideSpectrums[n];
      const int winSize = 1024;
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
      const int kPlotPoints = 160;
      ImVec2 pts[kPlotPoints];
      for (int i = 0; i < kPlotPoints; i++)
      {
         const float frac = (float)i / (float)(kPlotPoints - 1);
         const float hz = powf(10.0f, logLo + frac * logSpan);
         const int bin = std::clamp((int)(hz * (float)winSize / (float)sr), 1, 511);
         const float db = 20.0f * log10f(std::max(st.smoothed[bin], 1e-4f));
         const float normY = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
         pts[i] = ImVec2(origin.x + frac * w, br.y - normY * (h * 0.9f) - 6.0f);
      }
      const ImU32 fillCol = isLight ? tok::U32(tok::pal::c_8C46DC2D) : tok::U32(tok::pal::c_B464FF2D);
      const ImU32 lineCol = isLight ? tok::U32(tok::pal::c_963CE6F0) : tok::U32(tok::pal::c_C882FFF0);
      for (int i = 0; i < kPlotPoints - 1; i++)
      {
         ImVec2 quad[4] = { pts[i], pts[i + 1], ImVec2(pts[i + 1].x, br.y), ImVec2(pts[i].x, br.y) };
         dl->AddConvexPolyFilled(quad, 4, fillCol);
         dl->AddLine(pts[i], pts[i + 1], lineCol, 2.0f);
      }

      // A -> B track along the bottom edge with the slide position on it.
      const float slide = std::clamp(n->Param("slide"), 0.0f, 1.0f);
      const float ty = br.y - 3.0f;
      const ImU32 trackCol = isLight ? tok::U32(tok::pal::c_28283C3C) : tok::U32(tok::pal::c_FFFFFF32);
      const ImU32 dotCol = isLight ? tok::U32(tok::pal::c_28283CC8) : tok::U32(tok::pal::c_FFFFFFBE);
      dl->AddLine(ImVec2(origin.x + 8.0f, ty), ImVec2(br.x - 8.0f, ty), trackCol, 1.0f);
      dl->AddCircleFilled(ImVec2(origin.x + 8.0f + slide * (w - 16.0f), ty), 2.5f, dotCol);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.0f%% of the way from input to 'to'", slide * 100.0f);
         SetAudioReadout("spectrum slide", buf);
      }
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawSpectrumSlideBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float slide = n->Param("slide");
      char stat[64];
      snprintf(stat, sizeof(stat), "slide %.0f%% - in -> to", slide * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawSpectrumSlideVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(2, kKnobLarge);
         row.Knob("slide", n->ParamPtr("slide"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }


   void DrawShapeResonatorVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), tok::radius_field);
      dl->PushClipRect(origin, br, true);

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

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);
      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%d modes, line height = ring time", count);
         SetAudioReadout("shape modes", buf);
      }
      ImGui::Dummy(ImVec2(w, h));
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
}
