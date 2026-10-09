// Filter, EQ, dynamics, delay, reverb and drive bodies + visualizers (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/components/AudioViz.h"

namespace app
{
 // ~20 Hz cap per node while modulation moves the curve

   // Per-node offset in [0, 1) of a throttle interval, from the golden-ratio
   // sequence over the node index - spreads nodes that start moving on the
   // same frame (one LFO driving many filters, transport start) across the
   // interval instead of recomputing them all together.
   double FilterCurvePhase(int nodeIndex)
   {
      const double v = (double)(unsigned)nodeIndex * 0.6180339887498949;
      return v - std::floor(v);
   }


   // Throttling *how often* the recompute fires (below) caps it at ~12.5Hz.
   // kFilterCurveDragPoints drops the point count while a drag is live; it
   // dates from the settled-sine MagnitudeDb, when a full 160-point sweep
   // measured ~10-15ms at Q=18/48kHz - full kFilterCurveFullPoints
   // resolution always returns the instant the drag ends.
   const double kFilterCurveThrottleSec = 0.08;
 // ~12.5 Hz cap on the full recompute while dragging
   const int kFilterCurveDragPoints = 48;


   const float kFilterVizMinHz = 20.0f;

   const float kFilterVizMaxHz = 20000.0f;

   const float kFilterVizMinDb = -24.0f;

   const float kFilterVizMaxDb = 24.0f;


   float FilterVizFreqToX(float hz, float x0, float w)
   {
      const float t = (logf(std::max(1.0f, hz)) - logf(kFilterVizMinHz)) /
                      (logf(kFilterVizMaxHz) - logf(kFilterVizMinHz));
      return x0 + std::clamp(t, 0.0f, 1.0f) * w;
   }


   float FilterVizXToFreq(float x, float x0, float w)
   {
      const float t = std::clamp((x - x0) / std::max(1.0f, w), 0.0f, 1.0f);
      return kFilterVizMinHz * powf(kFilterVizMaxHz / kFilterVizMinHz, t);
   }


   float FilterVizDbToY(float db, float y0, float h)
   {
      const float t = (db - kFilterVizMinDb) / (kFilterVizMaxDb - kFilterVizMinDb);
      return y0 + h - std::clamp(t, 0.0f, 1.0f) * h;
   }


   float FilterVizYToDb(float y, float y0, float h)
   {
      const float t = std::clamp((y0 + h - y) / h, 0.0f, 1.0f);
      return kFilterVizMinDb + t * (kFilterVizMaxDb - kFilterVizMinDb);
   }


   // Shared graticule for Audio Filter and EQ - decade-anchored frequency
   // ticks plus dB ticks, both labeled. The two callers were previously
   // drawing identical unlabeled tick loops (confirmed-safe dedup).
   void DrawFilterGraticule(ImDrawList* dl, ImVec2 origin, float w, float h)
   {
      static const float kFreqTicks[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                          2000.0f, 5000.0f, 10000.0f, 20000.0f };
      const float textY = origin.y + h - ImGui::GetTextLineHeight() - 2.0f;
      for (float f : kFreqTicks)
      {
         const bool isEdge = (f <= kFilterVizMinHz || f >= kFilterVizMaxHz);
         const bool isDecadeAnchor = (f == 100.0f || f == 1000.0f || f == 10000.0f);
         const float x = FilterVizFreqToX(f, origin.x, w);
         if (!isEdge)
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + h),
                        isDecadeAnchor ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);

         char buf[8];
         if (f < 1000.0f)
            snprintf(buf, sizeof(buf), "%.0f", f);
         else
            snprintf(buf, sizeof(buf), "%.0fk", f / 1000.0f);

         if (f <= kFilterVizMinHz)
            dl->AddText(ImVec2(origin.x + 3.0f, textY), ScopeTextCol(), buf);
         else if (f >= kFilterVizMaxHz)
            dl->AddText(ImVec2(origin.x + w - 3.0f - ImGui::CalcTextSize(buf).x, textY), ScopeTextCol(), buf);
         else
            dl->AddText(ImVec2(x - ImGui::CalcTextSize(buf).x * 0.5f, textY), ScopeTextCol(), buf);
      }

      static const float kDbTicks[] = { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f };
      static const bool kDbLabeled[] = { true, true, false, true, false, true, true };
      for (int i = 0; i < 7; i++)
      {
         const float db = kDbTicks[i];
         const float y = FilterVizDbToY(db, origin.y, h);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + w, y),
                     db == 0.0f ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         if (kDbLabeled[i])
         {
            char buf[8];
            if (db == 0.0f)
               snprintf(buf, sizeof(buf), "0");
            else
               snprintf(buf, sizeof(buf), "%+.0f", db);
            dl->AddText(ImVec2(origin.x + 3.0f, y - (db == 0.0f ? 14.0f : 1.0f)), ScopeTextCol(), buf);
         }
      }
   }


   // Spectrum level has its own dB scale, independent of the +-24 dB gain
   // axis the response curve uses - same relationship as Pro-Q, where the
   // analyzer trace and the response curve share an x-axis but not a y-axis.
   const float kAudioSpectrumFloorDb = -72.0f;

   const float kAudioSpectrumCeilDb = 0.0f;


   float AudioSpectrumDbToY(float db, float y0, float h)
   {
      const float t = (db - kAudioSpectrumFloorDb) / (kAudioSpectrumCeilDb - kAudioSpectrumFloorDb);
      return y0 + h - std::clamp(t, 0.0f, 1.0f) * h;
   }


   void UpdateAudioSpectrum(AudioEffectNode* n, AudioSpectrumState& st)
   {
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
      // Hann window, built once with the exact expression it used to be
      // evaluated with per sample, per node, per frame (1024 cosf each).
      static const std::vector<float> sHann = [] {
         std::vector<float> w(1024);
         for (int i = 0; i < 1024; i++)
            w[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979323846f * (float)i / (float)(1024 - 1)));
         return w;
      }();
      float re[1024];
      float im[1024];
      for (int i = 0; i < winSize; i++)
      {
         re[i] = st.window[i] * sHann[i];
         im[i] = 0.0f;
      }
      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);

      for (int i = 1; i < 512; i++)
      {
         const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) * (2.0f / (float)winSize);
         st.smoothed[i] = st.smoothed[i] * 0.7f + mag * 0.3f;
      }
   }


   void DrawAudioSpectrum(ImDrawList* dl, ImVec2 origin, float w, float h, double sampleRate, const AudioSpectrumState& st,
                       bool isLight)
   {
      const float binWidth = (float)(sampleRate > 0.0 ? sampleRate : 44100.0) / 1024.0f;
      const float baselineY = origin.y + h;
      const ImU32 fillCol = isLight ? tok::U32(tok::pal::c_465A7828) : tok::U32(tok::pal::c_D2DCEB28);
      const ImU32 lineCol = isLight ? tok::U32(tok::pal::c_465A7882) : tok::U32(tok::pal::c_D2DCEB82);

      float prevX = FilterVizFreqToX(binWidth, origin.x, w);
      float prevY = AudioSpectrumDbToY(20.0f * log10f(std::max(st.smoothed[1], 1.0e-6f)), origin.y, h);
      dl->PathClear();
      dl->PathLineTo(ImVec2(prevX, prevY));
      for (int i = 2; i < 512; i++)
      {
         const float freq = (float)i * binWidth;
         const float db = 20.0f * log10f(std::max(st.smoothed[i], 1.0e-6f));
         const float x = FilterVizFreqToX(freq, origin.x, w);
         const float y = AudioSpectrumDbToY(db, origin.y, h);
         dl->AddQuadFilled(ImVec2(prevX, baselineY), ImVec2(x, baselineY), ImVec2(x, y), ImVec2(prevX, prevY), fillCol);
         dl->PathLineTo(ImVec2(x, y));
         prevX = x;
         prevY = y;
      }
      dl->PathStroke(lineCol, 0, 1.2f);
   }


   // Full-resolution response curve, exactly as the visualizer computes it -
   // the one definition both the cache and FILTERCURVECACHETEST use.
   void ComputeFilterCurve(std::vector<float>& out, int numPoints, int type, float freq, float q, float gain,
                           double sampleRate, float originX, float w)
   {
      out.resize(numPoints);
      for (int i = 0; i < numPoints; i++)
      {
         const float x = originX + (float)i * (w / (float)(numPoints - 1));
         const float f = FilterVizXToFreq(x, originX, w);
         out[i] = AudioFilterDsp::MagnitudeDb(type, freq, q, gain, f, sampleRate);
      }
   }


   // Full-width log-frequency response curve with a draggable handle per
   // band - the reason Audio Filter is built first (§1.1): X = freq,
   // Y = gain, Shift-drag = Q, double-click = enable/disable. This *is* the
   // Tier 1 control surface's picture, per audio-node-ui-system.md §3g.
   void DrawAudioFilterVisualizer(AudioEffectNode* n, double sampleRate)
   {
      const float w = gAudioBodyW;
      const float h = 190.0f; // fills the space the old per-band strip used to occupy
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      ImGui::InvisibleButton("##filterCurve", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      // Graticule that keeps this from reading as blank at rest (§3f).
      DrawFilterGraticule(dl, origin, w, h);

      // Live incoming-signal spectrum, same analyzer trace the EQ's
      // visualizer draws (DrawAudioSpectrum) - so what "shows frequencies"
      // means is identical on both nodes rather than the Filter having a
      // response curve with nothing behind it.
      AudioSpectrumState& specState = gAudioSpectrumCache[gCurrentNodeIndex];
      UpdateAudioSpectrum(n, specState);
      DrawAudioSpectrum(dl, origin, w, h, sampleRate, specState, isLight);

      const float type = n->Param("type");
      const float freq = n->Param("freq");
      const float q = n->Param("q");
      const float gain = n->Param("gain");

      std::vector<float> sig{ (float)sampleRate, type, freq, q, gain };
      FilterCurveCache& cache = gFilterCurveCache[gCurrentNodeIndex];

      // Throttle the recompute while a drag or modulation is actively changing the
      // signature every frame - see the FilterCurveCache comment above for
      // the full reasoning. `sigChanged` catches continuous changes;
      // `cache.signature != sig` is what actually needs a recompute (true
      // the first frame after any change, drag or modulation).
      const bool sigChanged = (cache.lastSeenSignature != sig);
      cache.lastSeenSignature = sig;
      const bool dragging = sigChanged && ImGui::IsMouseDown(ImGuiMouseButton_Left);
      // A modulation/expression/macro streak: changing again, with no drag.
      const bool inMotion = sigChanged && !dragging && cache.changedLastFrame;
      cache.changedLastFrame = sigChanged;
      const bool continuousChange = dragging || inMotion;
      const double now = ImGui::GetTime();
      const bool throttled = continuousChange && cache.lastRecomputeTime >= 0.0 &&
                             (now - cache.lastRecomputeTime) < kFilterCurveThrottleSec;
      const bool motionThrottled = inMotion && now < cache.nextDue;
      // Coarser while a drag or a motion streak is live (see
      // kFilterCurveDragPoints comment above). The first frame the signature
      // holds still has neither, so it recomputes at full resolution - also
      // when the streak's last recompute already used the final signature
      // but only at the coarse point count (`cache.coarse`).
      const bool coarseNow = dragging || inMotion;
      const int kNumPoints = coarseNow ? kFilterCurveDragPoints : kFilterCurveFullPoints;
      const bool stale = cache.signature != sig || (cache.coarse && !coarseNow);
      if (stale && !throttled && !motionThrottled)
      {
         cache.signature = sig;
         cache.lastRecomputeTime = now;
         cache.coarse = coarseNow;
         cache.lastOriginX = origin.x;
         cache.lastWidth = w;
         ComputeFilterCurve(cache.curveDb, kNumPoints, (int)(type + 0.5f), freq, q, gain, sampleRate, origin.x, w);
         ++gFilterCurveRecomputes;
         // Next slot this node may recompute in during a motion streak: on a
         // fixed kFilterCurveMotionSec grid whose phase is set per node when
         // the streak starts, so staggered nodes stay staggered.
         if (inMotion)
         {
            const double behind = now - cache.nextDue;
            cache.nextDue += kFilterCurveMotionSec * (std::floor(behind / kFilterCurveMotionSec) + 1.0);
         }
         else
         {
            cache.nextDue = now + kFilterCurveMotionSec * FilterCurvePhase(gCurrentNodeIndex);
         }
      }

      // The base curve and its handles below always reflect the actual
      // knob-set freq/Q/gain and never move on their own - a curve that
      // silently shifts out from under a value the knobs still show is a P9
      // truthfulness violation. `cache.curveDb.size()` (not `kNumPoints`,
      // which reflects *this frame's* drag state and can be one frame stale
      // relative to the cache on the exact mouse-release frame) is the
      // source of truth for how many points are actually cached.
      const int cachedPoints = (int)cache.curveDb.size();
      if (cachedPoints > 1)
      {
         const float yBottom = origin.y + h;
         const ImU32 fillCol = isLight ? tok::U32(tok::pal::c_1E6EE620) : tok::U32(tok::pal::c_64B4FF24);
         for (int i = 0; i < cachedPoints - 1; i++)
         {
            const float x0 = origin.x + (float)i * (w / (float)(cachedPoints - 1));
            const float x1 = origin.x + (float)(i + 1) * (w / (float)(cachedPoints - 1));
            const float y0 = FilterVizDbToY(cache.curveDb[i], origin.y, h);
            const float y1 = FilterVizDbToY(cache.curveDb[i + 1], origin.y, h);
            dl->AddQuadFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x1, yBottom), ImVec2(x0, yBottom), fillCol);
         }
      }
      dl->PathClear();
      for (int i = 0; i < cachedPoints; i++)
      {
         const float x = origin.x + (float)i * (w / (float)(cachedPoints - 1));
         const float y = FilterVizDbToY(cache.curveDb[i], origin.y, h);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      // Live LFO sweep indicator (envAmount != 0): a second curve, in yellow,
      // overlaid on top of the static base curve above - it never replaces
      // or moves the base curve/handles, purely an animated readout of where
      // the internal LFO currently has the cutoff swept to. Reuses the same
      // cached curveDb array (translated in x), so this costs nothing extra
      // in MagnitudeDb calls - octaves of cutoff shift are a constant
      // pixel-space offset along the log-frequency axis, since the response
      // of these filter types depends only on the ratio of the eval
      // frequency to the cutoff, not the cutoff's absolute position (exact
      // for SVF LP/HP/BP/notch, a close approximation for the shelf/peak
      // biquad types this close to Nyquist). `lfoOut` is read from the
      // kernel's own audio-thread LFO (AudioFilterKernel::mLfoMeter) via the
      // existing ExtraMeterValue MeterRing readback, same discipline as
      // Dynamics' GR-bar dot - no new cross-thread mechanism.
      const float envAmount = n->Param("envAmount");
      const float lfoOut = envAmount != 0.0f ? std::clamp(n->ExtraMeterValue(0), -1.0f, 1.0f) : 0.0f;
      const float totalOctaves = envAmount * lfoOut * 4.0f;
      const float pixelsPerOctave = w * logf(2.0f) / (logf(kFilterVizMaxHz) - logf(kFilterVizMinHz));
      const float deltaX = totalOctaves * pixelsPerOctave;
      if (envAmount != 0.0f)
      {
         dl->PathClear();
         for (int i = 0; i < cachedPoints; i++)
         {
            const float x = origin.x + (float)i * (w / (float)(cachedPoints - 1)) + deltaX;
            const float y = FilterVizDbToY(cache.curveDb[i], origin.y, h);
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(tok::U32(tok::pal::c_FFCD3CD2), 0, 1.5f);
      }

      const float hx = FilterVizFreqToX(freq, origin.x, w);
      const float hy = FilterVizDbToY(AudioFilterDsp::UsesGain((int)(type + 0.5f)) ? gain : 0.0f, origin.y, h);

      const bool isNear = hovered || active;

      if (active)
      {
         if (ImGui::GetIO().KeyShift)
         {
            const float dy = ImGui::GetIO().MouseDelta.y;
            if (dy != 0.0f)
            {
               const float curQ = n->Param("q");
               const float logQ = logf(std::clamp(curQ, 0.1f, 18.0f)) - dy * (5.19f / 200.0f);
               *n->ParamPtr("q") = std::clamp(expf(logQ), 0.1f, 18.0f);
            }
         }
         else
         {
            const ImVec2 m = ImGui::GetIO().MousePos;
            *n->ParamPtr("freq") = std::clamp(FilterVizXToFreq(m.x, origin.x, w), kFilterVizMinHz, kFilterVizMaxHz);
            if (AudioFilterDsp::UsesGain((int)(type + 0.5f)))
               *n->ParamPtr("gain") = FilterVizYToDb(m.y, origin.y, h);
         }
      }

      // Handle stays at the real (non-LFO-shifted) freq/gain - it's the
      // knob-set value, and only the yellow overlay above should visually
      // sweep. Shift-drag on the handle adjusts Q / resonance.
      dl->AddCircleFilled(ImVec2(hx, hy), isNear ? 5.0f : 3.6f, tok::U32(tok::pal::c_EBF5FFFF), 12);
      dl->AddCircle(ImVec2(hx, hy), isNear ? 5.0f : 3.6f, tok::U32(tok::pal::c_141820DC), 12, 1.5f);

      AudioViz::End(vizFrame, hovered ? (isLight ? tok::U32(tok::pal::c_3278DCFF) : tok::U32(tok::pal::c_6E8CB4FF)) : 0);

      if (isNear)
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.0f Hz  %+.1f dB  Q %.2f", freq, gain, q);
         SetAudioReadout("filter", buf);
      }
      // No trailing Dummy here: the InvisibleButton above already reserved
      // this (w, h) layout space - a second Dummy of the same size would
      // double-reserve it and leave a blank gap the height of the graph
      // between it and whatever's drawn next.
   }


   // Forward declaration: the shared sync/rate-mode cell pair Chorus/
   // Flanger/Phaser/Tremolo all use is defined further down (with the rest
   // of their shared helpers), but Audio Filter's own body is defined here,
   // earlier in the file, and now uses it too for its "env" LFO's rate.
   // Default rateLo/rateHi live only on this first declaration - a default
   // argument can only be specified once across all declarations seen by
   // the translation unit, so the real definition below no longer repeats
   // them.
   void AddRateModeCells(AudioKnobRow& row, AudioEffectNode* n, const char* syncLabel,
                         float rateLo, float rateHi);


   void DrawAudioFilterBody(GraphNode& gn, AudioEffectNode* n)
   {
      const int type = (int)(n->Param("type") + 0.5f);
      const float freq = n->Param("freq");
      const float q = n->Param("q");

      char stat[80];
      snprintf(stat, sizeof(stat), "%s - %.1f kHz - Q %.2f", AudioFilterDsp::TypeName(type),
               freq / 1000.0f, q);

      const double sampleRate = AudioEngine::Instance().SampleRate() > 0.0
                                   ? AudioEngine::Instance().SampleRate()
                                   : 44100.0;

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawAudioFilterVisualizer(n, sampleRate);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4);
         row.Dropdown("type", AudioFilterDsp::TypeList(), type, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("type") = (float)i;
         });
         row.Knob("freq", n->ParamPtr("freq"), 20.0f, 20000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("Q", n->ParamPtr("q"), 0.1f, 18.0f, "%.2f", kKnobLarge);

         const bool gainLive = AudioFilterDsp::UsesGain(type);
         if (!gainLive)
            ImGui::BeginDisabled();
         row.Knob("gain", n->ParamPtr("gain"), -24.0f, 24.0f, "%.1f dB", kKnobLarge);
         if (!gainLive)
            ImGui::EndDisabled();
         row.End();
      }

      BeginAudioSection("output");
      {
         // 4 cells: selector column left (sync v, rate), knobs right
         // (env, mix) - mix stays bottom-right (P4). `row.index` is set
         // explicitly before each call so the *visual* cell a control lands
         // in can differ from its *draw order* - mix and env are still
         // called first and second, exactly as before "output gain" was
         // removed (it's redundant with the filter's own `gain` knob above),
         // so their gParamCounter ordinals stay in the same relative order
         // to each other and to sync/rate. Deleting "output gain"'s knob
         // outright (not just repositioning it) does still shift every
         // later-drawn param's ordinal down by one relative to old patches -
         // unavoidable when removing a param's control entirely, same
         // reasoning DrawChorusBody uses for its near-identical layout
         // problem.
         AudioKnobRow row(4);
         row.index = 3;
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.index = 2;
         row.Knob("env", n->ParamPtr("envAmount"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.index = 0;
         AddRateModeCells(row, n, "sync to tempo##filterSync");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }


   // ---- per-band modulation addressing ---------------------------------
   // Only the selected band's controls are on screen, but a modulation
   // cable, an expression or a Shift-drag gesture recording belongs to the
   // *band* it was made on - not to "whichever band happens to be selected
   // when it next draws". Every band therefore needs its own pin address,
   // which means neither of the two automatic numbering schemes can be left
   // to do it: gParamCounter numbers by draw order (so all five bands' freq
   // knobs collapsed onto ordinal 0) and DiscreteParamSlot numbers by label
   // hash (so all five type dropdowns collapsed onto hash("type")).
   //
   // Knobs get an explicit ordinal, band-major: band b's freq/Q/gain are
   // 3b+0 / 3b+1 / 3b+2. Band 1 therefore keeps 0/1/2 - exactly the ordinals
   // gParamCounter used to hand out - so a `mod`/`expr` line saved before
   // this split still lands on a real control, and on the band a freshly
   // created EQ is selected to. Ordinals 3..14 were never issued by this
   // node before, so nothing else in an old patch can collide with them.
   inline int EqBandKnobParam(int band, int slot) { return band * 3 + slot; }

   const int kEqBandKnobParamSpan = 15;
 // 5 bands x 3 knobs, reserved

   // Dropdowns get a per-band label instead, since that is what
   // DiscreteParamSlot hashes. Band 1's is aliased back to the bare "type"
   // in DiscreteParamSlot's kRenameAliases, for the same
   // old-patch-compatibility reason as the knob ordinals above.
   static const char* const kEqTypeLabel[5] = { "band 1 type##eqType1", "band 2 type##eqType2",
                                                "band 3 type##eqType3", "band 4 type##eqType4",
                                                "band 5 type##eqType5" };

   // Param names for the modulation matrix and the binding menu. The knob
   // caps still read "freq"/"Q"/"gain" - a four-cell row has no space for
   // "band 3 freq", and the band is already named by the dropdown beside
   // them and by the visualizer's own caption - but five destinations all
   // called "freq" on one node would be unreadable in the matrix, which is
   // the one place all five are visible at once.
   static const char* const kEqBandKnobName[5][3] = {
      { "band 1 freq", "band 1 Q", "band 1 gain" }, { "band 2 freq", "band 2 Q", "band 2 gain" },
      { "band 3 freq", "band 3 Q", "band 3 gain" }, { "band 4 freq", "band 4 Q", "band 4 gain" },
      { "band 5 freq", "band 5 Q", "band 5 gain" } };


   struct EqBandValues
   {
      int type;
      float freq, q, gain;
      bool on;
   };


   void ReadEqBands(AudioEffectNode* n, EqBandValues bands[5])
   {
      for (int b = 0; b < 5; b++)
      {
         bands[b].type = EqDsp::SanitizeType(n->Param(kEqTypeParam[b]));
         bands[b].freq = n->Param(kEqFreqParam[b]);
         bands[b].q = n->Param(kEqQParam[b]);
         bands[b].gain = n->Param(kEqGainParam[b]);
         bands[b].on = n->Param(kEqOnParam[b]) >= 0.5f;
      }
   }


   void DrawEqVisualizer(AudioEffectNode* n, double sampleRate)
   {
      const float w = gAudioBodyW;
      const float h = 190.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      gEqTestRect = ImVec4(origin.x, origin.y, br.x, br.y);

      ImGui::InvisibleButton("##eqCurve", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      DrawFilterGraticule(dl, origin, w, h);

      AudioSpectrumState& specState = gAudioSpectrumCache[gCurrentNodeIndex];
      UpdateAudioSpectrum(n, specState);
      DrawAudioSpectrum(dl, origin, w, h, sampleRate, specState, isLight);

      EqBandValues bands[5];
      ReadEqBands(n, bands);
      int selected = std::clamp((int)(n->Param("selectedBand") + 0.5f), 0, 4);

      std::vector<float> sig;
      sig.reserve(26);
      sig.push_back((float)sampleRate);
      for (int b = 0; b < 5; b++)
      {
         sig.push_back((float)bands[b].type);
         sig.push_back(bands[b].freq);
         sig.push_back(bands[b].q);
         sig.push_back(bands[b].gain);
         sig.push_back(bands[b].on ? 1.0f : 0.0f);
      }

      EqCurveCache& cache = gEqCurveCache[gCurrentNodeIndex];
      const int kNumPoints = 160;
      if (cache.signature != sig)
      {
         cache.signature = sig;
         cache.curveDb.resize(kNumPoints);
         for (int b = 0; b < 5; b++)
            cache.bandCurveDb[b].resize(kNumPoints);
         // Coefficients once per band and trig once per point - the same
         // floats EqDsp::BandMagnitudeDb gives per call (see EqKernel.h),
         // without re-configuring five biquads at every one of 160 points.
         EqDsp::BandEval evals[5];
         for (int b = 0; b < 5; b++)
            evals[b] = EqDsp::PrepareBand(bands[b].type, bands[b].freq, bands[b].q, bands[b].gain,
                                          bands[b].on, sampleRate);
         for (int i = 0; i < kNumPoints; i++)
         {
            const float x = origin.x + (float)i * (w / (float)(kNumPoints - 1));
            const float f = FilterVizXToFreq(x, origin.x, w);
            const EqDsp::EvalTrig trig = EqDsp::EvalTrigFor(f, sampleRate);
            float composite = 0.0f;
            for (int b = 0; b < 5; b++)
            {
               const float bandDb = EqDsp::BandMagnitudeDbAt(evals[b], trig);
               cache.bandCurveDb[b][i] = bandDb;
               composite += bandDb;
            }
            cache.curveDb[i] = composite;
         }
      }

      // Per-band curves at low alpha, only for enabled bands - a disabled
      // band draws a hollow dot and no curve (§3f, "never blank" cuts both
      // ways: absence is shown, not hidden).
      for (int b = 0; b < 5; b++)
      {
         if (!bands[b].on)
            continue;
         dl->PathClear();
         for (int i = 0; i < kNumPoints; i++)
         {
            const float x = origin.x + (float)i * (w / (float)(kNumPoints - 1));
            const float y = FilterVizDbToY(cache.bandCurveDb[b][i], origin.y, h);
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(tok::U32(tok::pal::c_96D6FF3C), 0, 1.0f);
      }

      // Composite curve, bright, with a soft fill down to the 0 dB baseline -
      // built from per-segment quads rather than AddConvexPolyFilled, which
      // assumes convexity a response curve doesn't guarantee.
      const float baselineY = FilterVizDbToY(0.0f, origin.y, h);
      for (int i = 0; i + 1 < kNumPoints; i++)
      {
         const float x0 = origin.x + (float)i * (w / (float)(kNumPoints - 1));
         const float x1 = origin.x + (float)(i + 1) * (w / (float)(kNumPoints - 1));
         const float y0 = FilterVizDbToY(cache.curveDb[i], origin.y, h);
         const float y1 = FilterVizDbToY(cache.curveDb[i + 1], origin.y, h);
         dl->AddQuadFilled(ImVec2(x0, baselineY), ImVec2(x1, baselineY), ImVec2(x1, y1), ImVec2(x0, y0),
                           isLight ? tok::U32(tok::pal::c_1E6EE61E) : tok::U32(tok::pal::c_96D6FF16));
      }

      // Persistent selected-band caption - unlike the hover readout below,
      // this is visible at rest so the user always knows what's selected.
      {
         char freqBuf[16];
         if (bands[selected].freq < 1000.0f)
            snprintf(freqBuf, sizeof(freqBuf), "%.0f Hz", bands[selected].freq);
         else
            snprintf(freqBuf, sizeof(freqBuf), "%.2f kHz", bands[selected].freq / 1000.0f);
         char capBuf[80];
         snprintf(capBuf, sizeof(capBuf), "band %d \xC2\xB7 %s \xC2\xB7 %+.1f dB \xC2\xB7 Q %.2f", selected + 1,
                  freqBuf, bands[selected].gain, bands[selected].q);
         dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f), ScopeTextCol(), capBuf);
      }

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
      {
         const float x = origin.x + (float)i * (w / (float)(kNumPoints - 1));
         const float y = FilterVizDbToY(cache.curveDb[i], origin.y, h);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      // Handle positions for every band - one white dot each on the curve.
      float hx[5], hy[5];
      for (int b = 0; b < 5; b++)
      {
         hx[b] = FilterVizFreqToX(bands[b].freq, origin.x, w);
         hy[b] = FilterVizDbToY(EqDsp::UsesGain(bands[b].type) ? bands[b].gain : 0.0f, origin.y, h);
      }

      if (ImGui::IsItemActivated())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         float bestDotDist2 = 1.0e30f;
         int bestDotBand = selected;
         for (int b = 0; b < 5; b++)
         {
            const float dDot = (m.x - hx[b]) * (m.x - hx[b]) + (m.y - hy[b]) * (m.y - hy[b]);
            if (dDot < bestDotDist2) { bestDotDist2 = dDot; bestDotBand = b; }
         }
         const float kHandleGrabRadius2 = 20.0f * 20.0f;
         cache.dragInert = false;
         if (bestDotDist2 <= kHandleGrabRadius2)
         {
            cache.dragBand = bestDotBand;
         }
         else
         {
            // Click landed away from every handle - select the nearest band
            // by its dot, but this gesture does not move anything.
            cache.dragBand = bestDotBand;
            cache.dragInert = true;
         }
         *n->ParamPtr("selectedBand") = (float)cache.dragBand;
         selected = cache.dragBand;
         // Grabbing a handle is grabbing the params it writes, so it has to
         // cancel their gesture loops exactly the way grabbing the knob does
         // (ModKnob's IsItemActivated branch) - otherwise the drag and the
         // loop write the same param on the same frame and the loop wins.
         // Only the params this drag will actually move: Shift-drag moves Q,
         // regular drag moves freq/gain, and a click away from every handle
         // (dragInert) moves nothing at all.
         if (!cache.dragInert)
         {
            GestureRecorder& gr = GestureRecorder::Instance();
            const int b = cache.dragBand;
            if (ImGui::GetIO().KeyShift)
               gr.StopPlayback(gCurrentNodeIndex, EqBandKnobParam(b, 1));
            else
            {
               gr.StopPlayback(gCurrentNodeIndex, EqBandKnobParam(b, 0));
               gr.StopPlayback(gCurrentNodeIndex, EqBandKnobParam(b, 2));
            }
         }
      }

      // Double-click a band's dot toggles its bandOn, and cancels this
      // gesture's drag so the toggle doesn't also nudge freq/gain.
      if (hovered && ImGui::IsMouseDoubleClicked(0))
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         float bestDotDist2 = 1.0e30f;
         int bestDotBand = -1;
         for (int b = 0; b < 5; b++)
         {
            const float dDot = (m.x - hx[b]) * (m.x - hx[b]) + (m.y - hy[b]) * (m.y - hy[b]);
            if (dDot < bestDotDist2) { bestDotDist2 = dDot; bestDotBand = b; }
         }
         const float kToggleRadius2 = 20.0f * 20.0f;
         if (bestDotBand >= 0 && bestDotDist2 <= kToggleRadius2)
         {
            *n->ParamPtr(kEqOnParam[bestDotBand]) = bands[bestDotBand].on ? 0.0f : 1.0f;
            cache.dragInert = true;
         }
      }

      const int dragBand = std::clamp(cache.dragBand < 0 ? selected : cache.dragBand, 0, 4);
      if (active && !cache.dragInert)
      {
         if (ImGui::GetIO().KeyShift)
         {
            const float dy = ImGui::GetIO().MouseDelta.y;
            if (dy != 0.0f)
            {
               const float curQ = bands[dragBand].q;
               const float logQ = logf(std::clamp(curQ, 0.1f, 18.0f)) - dy * (5.19f / 200.0f);
               *n->ParamPtr(kEqQParam[dragBand]) = std::clamp(expf(logQ), 0.1f, 18.0f);
            }
         }
         else
         {
            const ImVec2 m = ImGui::GetIO().MousePos;
            *n->ParamPtr(kEqFreqParam[dragBand]) =
               std::clamp(FilterVizXToFreq(m.x, origin.x, w), kFilterVizMinHz, kFilterVizMaxHz);
            if (EqDsp::UsesGain(bands[dragBand].type))
               *n->ParamPtr(kEqGainParam[dragBand]) = std::clamp(FilterVizYToDb(m.y, origin.y, h), -24.0f, 24.0f);
         }
      }

      // Draw every non-selected band's handles first, then the selected
      // band's larger and last, so it is never occluded by a neighbour.
      for (int pass = 0; pass < 2; pass++)
      {
         for (int b = 0; b < 5; b++)
         {
            const bool isSelected = (b == selected);
            if ((pass == 0) == isSelected)
               continue;

            const bool isDragTarget = active && !cache.dragInert && dragBand == b;
            const bool dotActiveHere = isDragTarget;

            // Only ONE band's knobs are on screen at a time, but the curve is
            // drawn from all five - so a gesture loop running on a band the
            // user has switched away from moves the curve with nothing
            // visible to explain it and nothing to grab to stop it. These
            // handles are that band's only on-screen representation, so they
            // carry the same red the knob would (ModKnob's `recording`).
            const GestureRecorder& gr = GestureRecorder::Instance();
            const bool dotRec = gr.IsRecording(gCurrentNodeIndex, EqBandKnobParam(b, 0)) ||
                                gr.IsRecording(gCurrentNodeIndex, EqBandKnobParam(b, 1)) ||
                                gr.IsRecording(gCurrentNodeIndex, EqBandKnobParam(b, 2));
            const ImU32 recCol = IM_COL32(235, 70, 70, isSelected ? 255 : 190);

            if (!isSelected)
               for (int k = 0; k < 3; k++)
                  NoteHiddenParamAlias(gCurrentNodeIndex, EqBandKnobParam(b, k), EqBandKnobParam(selected, k));

            const float dotR = isSelected ? (dotActiveHere ? 6.5f : 4.8f) : (dotActiveHere ? 5.0f : 3.2f);
            const ImU32 dotCol = dotRec ? recCol : IM_COL32(235, 245, 255, isSelected ? 255 : 170);
            if (bands[b].on)
            {
               dl->AddCircleFilled(ImVec2(hx[b], hy[b]), dotR, dotCol, 12);
            }
            else
            {
               dl->AddCircle(ImVec2(hx[b], hy[b]), dotR,
                             dotRec ? recCol : IM_COL32(235, 245, 255, isSelected ? 220 : 120), 12, 1.5f);
            }
            dl->AddCircle(ImVec2(hx[b], hy[b]), dotR, tok::U32(tok::pal::c_141820DC), 12, 1.2f);
         }
      }

      AudioViz::End(vizFrame, hovered ? (isLight ? tok::U32(tok::pal::c_3278DCFF) : tok::U32(tok::pal::c_6E8CB4FF)) : 0);

      if (hovered || active)
      {
         const int b = dragBand;
         char buf[80];
         snprintf(buf, sizeof(buf), "band %d  %.0f Hz  %+.1f dB  Q %.2f  %s", b + 1, bands[b].freq,
                  bands[b].gain, bands[b].q, bands[b].on ? "on" : "off");
         SetAudioReadout("eq", buf);
      }
      // No trailing Dummy: the InvisibleButton above already reserved this
      // (w, h) layout space.
   }


   // One band's four cells - [type v][freq][Q][gain] - at that band's own
   // pin addresses (see kEqTypeLabel / EqBandKnobParam). Called once per band
   // per frame: the selected band draws, the other four run under
   // gParamRegisterOnly and only register. See DrawEqBody for why.
   void EmitEqBandCells(AudioKnobRow& row, AudioEffectNode* n, int b, int type)
   {
      row.index = 0;
      row.Dropdown(kEqTypeLabel[b], EqDsp::TypeList(), type, [n, b](int i) {
         PushUndoCheckpoint();
         *n->ParamPtr(kEqTypeParam[b]) = (float)i;
      });
      row.Knob("freq", n->ParamPtr(kEqFreqParam[b]), 20.0f, 20000.0f, "%.0f Hz", kKnobLarge,
               /*dbTaper=*/false, /*freqTaper=*/false, AudioWidgetStyle::Knob, nullptr, nullptr,
               EqBandKnobParam(b, 0), kEqBandKnobName[b][0]);
      row.Knob("Q", n->ParamPtr(kEqQParam[b]), 0.1f, 18.0f, "%.2f", kKnobLarge,
               /*dbTaper=*/false, /*freqTaper=*/false, AudioWidgetStyle::Knob, nullptr, nullptr,
               EqBandKnobParam(b, 1), kEqBandKnobName[b][1]);

      // A band type with no gain term greys its gain knob rather than
      // dropping the cell (P5). Only meaningful while drawing - BeginDisabled
      // under gParamRegisterOnly would push style onto whatever ImGui state
      // the caller is in, and the knob it wraps isn't drawn anyway.
      const bool greyGain = !EqDsp::UsesGain(type) && !gParamRegisterOnly;
      if (greyGain)
         ImGui::BeginDisabled();
      row.Knob("gain", n->ParamPtr(kEqGainParam[b]), -24.0f, 24.0f, "%.1f dB", kKnobLarge,
               /*dbTaper=*/false, /*freqTaper=*/false, AudioWidgetStyle::Knob, nullptr, nullptr,
               EqBandKnobParam(b, 2), kEqBandKnobName[b][2]);
      if (greyGain)
         ImGui::EndDisabled();
   }


   void DrawEqBody(GraphNode& gn, AudioEffectNode* n)
   {
      EqBandValues bands[5];
      ReadEqBands(n, bands);

      int active = 0;
      float peakDb = 0.0f;
      for (int b = 0; b < 5; b++)
         if (bands[b].on)
            active++;
      {
         // Coarse scan for the idle-stat peak, independent of the
         // visualizer's own (denser, cached) curve.
         const double sampleRate = AudioEngine::Instance().SampleRate() > 0.0
                                      ? AudioEngine::Instance().SampleRate()
                                      : 44100.0;
         const int kScanPoints = 48;
         float bestAbs = -1.0f;
         // Runs every drawn frame, so configure each band once rather than
         // once per scan point (same floats - see EqDsp::BandEval).
         EqDsp::BandEval evals[5];
         for (int b = 0; b < 5; b++)
            evals[b] = EqDsp::PrepareBand(bands[b].type, bands[b].freq, bands[b].q, bands[b].gain,
                                          bands[b].on, sampleRate);
         for (int i = 0; i < kScanPoints; i++)
         {
            const float t = (float)i / (float)(kScanPoints - 1);
            const float f = kFilterVizMinHz * powf(kFilterVizMaxHz / kFilterVizMinHz, t);
            const EqDsp::EvalTrig trig = EqDsp::EvalTrigFor(f, sampleRate);
            float composite = 0.0f;
            for (int b = 0; b < 5; b++)
               composite += EqDsp::BandMagnitudeDbAt(evals[b], trig);
            if (std::fabs(composite) > bestAbs)
            {
               bestAbs = std::fabs(composite);
               peakDb = composite;
            }
         }
      }

      char stat[80];
      snprintf(stat, sizeof(stat), "5 bands - %d active - %+.1f dB peak", active, peakDb);

      const double sampleRate = AudioEngine::Instance().SampleRate() > 0.0
                                   ? AudioEngine::Instance().SampleRate()
                                   : 44100.0;

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawEqVisualizer(n, sampleRate);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      const int selBand = std::clamp((int)(n->Param("selectedBand") + 0.5f), 0, 4);

      {
         AudioKnobRow row(4);
         // Every band emits its four cells every frame, not just the visible
         // one. Both the modulation apply pass and the gesture playback pass
         // walk Modulation::FrameParams(), so a param that stops registering
         // stops being written - without this, patching an LFO into band 3's
         // freq and then selecting band 1 would freeze it mid-sweep. Same
         // reasoning, and the same gParamRegisterOnly mechanism, as
         // AddSyncedRateCell's hidden `rate` slot.
         //
         // Going through the identical AudioKnobRow calls rather than
         // hand-rolled ParamRef registrations is deliberate: the hidden
         // bands then inherit the drawn band's taper, min/max and step
         // automatically and cannot drift from it. A cable into a hidden
         // band still isn't *drawn* (its pin isn't emitted, so the link pass
         // skips it - see gDrawnParamPins) but it keeps modulating, and it
         // reappears when that band is selected again.
         //
         // Fixed band order, never "selected band first": DiscreteParamSlot
         // assigns a dropdown's slot the first time it sees the label and
         // probes linearly on collision, so the order the five type
         // dropdowns register in has to be the same on every frame and in
         // every run, whichever band happens to be selected.
         const bool savedRegisterOnly = gParamRegisterOnly;
         for (int b = 0; b < 5; b++)
         {
            gParamRegisterOnly = savedRegisterOnly || (b != selBand);
            EmitEqBandCells(row, n, b, bands[b].type);
         }
         gParamRegisterOnly = savedRegisterOnly;
         row.End();
      }
      // Ordinals 0..14 belong to the five bands' knobs whether or not their
      // band drew this frame, so anything added to this body later has to
      // number itself from here rather than from 0 - the knobs above take
      // explicit indices and never touch the counter themselves.
      gParamCounter = kEqBandKnobParamSpan;

      EndAudioBody();
   }


   // ---- Dynamics -----------------------------------------------------
   // Transfer curve (input dB -> output dB) with the live operating point
   // riding on it and a gain-reduction bar down the right edge - §1.2's
   // visualizer. The curve is main-thread from params (DynamicsDsp::
   // GainComputerDb, the same pure function the kernel's own per-sample gain
   // computer uses - never the live running kernel, per audio-node-ui-
   // system.md §3f); the dot and bar are the kernel's two published
   // ExtraMeterValue()s.
   const float kDynVizMinDb = -60.0f;

   const float kDynVizMaxDb = 6.0f;


   float DynVizXToDb(float x, float x0, float w)
   {
      const float t = std::clamp((x - x0) / std::max(1.0f, w), 0.0f, 1.0f);
      return kDynVizMinDb + t * (kDynVizMaxDb - kDynVizMinDb);
   }


   float DynVizDbToX(float db, float x0, float w)
   {
      const float t = (db - kDynVizMinDb) / (kDynVizMaxDb - kDynVizMinDb);
      return x0 + std::clamp(t, 0.0f, 1.0f) * w;
   }


   float DynVizDbToY(float db, float y0, float h)
   {
      const float t = (db - kDynVizMinDb) / (kDynVizMaxDb - kDynVizMinDb);
      return y0 + h - std::clamp(t, 0.0f, 1.0f) * h;
   }


   constexpr float kLimiterVizRatio = 1000.0f; // a brick-wall: slope past threshold is 1/1000

   struct GainVizLive
   {
      float inDb;  // raw input level, dB
      float grDb;  // gain reduction, dB (>= 0)
   };

   // The gain-computer picture shared by Dynamics and Limiter (a limiter is the same curve with an
   // infinite ratio): transfer curve + threshold marker + live operating point + slim GR meter.
   void DrawGainComputerVisualizer(const char* readoutKey, float threshold, float ratio, float makeupDb,
                                   float inGainDb, const GainVizLive& live)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const float grMeterW = 10.0f;
      const float grMeterGap = 6.0f;
      const float curveW = w - grMeterW - grMeterGap;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      const ImVec2 curveBr(origin.x + curveW, br.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, curveW, h);

      static const float kDbTicks[] = { -48.0f, -36.0f, -24.0f, -12.0f, 0.0f };
      for (float db : kDbTicks)
      {
         const float x = DynVizDbToX(db, origin.x, curveW);
         const float y = DynVizDbToY(db, origin.y, h);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, curveBr.y), db == 0.0f ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(curveBr.x, y),
                     db == 0.0f ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         char tickBuf[8];
         snprintf(tickBuf, sizeof(tickBuf), "%.0f", db);
         dl->AddText(ImVec2(origin.x + 3.0f, y - (db == 0.0f ? 14.0f : 1.0f)), ScopeTextCol(), tickBuf);
      }

      // 1:1 reference diagonal (unprocessed signal), dim.
      dl->AddLine(ImVec2(DynVizDbToX(kDynVizMinDb, origin.x, curveW), DynVizDbToY(kDynVizMinDb, origin.y, h)),
                  ImVec2(DynVizDbToX(kDynVizMaxDb, origin.x, curveW), DynVizDbToY(kDynVizMaxDb, origin.y, h)),
                  tok::U32(tok::pal::c_FFFFFF18), 1.0f);

      // Threshold marker - a vertical line at the knee so the point the
      // curve bends at reads as a control, not just an inflection the eye
      // has to find. Amber to stay distinct from the curve (blue) and the
      // live operating point (white) drawn below.
      const ImU32 thresholdCol = isLight ? tok::U32(tok::pal::c_D28214C8) : tok::U32(tok::pal::c_FFB446BE);
      const float threshX = DynVizDbToX(threshold, origin.x, curveW);
      dl->AddLine(ImVec2(threshX, origin.y), ImVec2(threshX, curveBr.y), thresholdCol, 1.5f);

      // The gain-computer curve itself, with makeup gain folded in so the
      // picture matches what actually comes out. The curve's bend at
      // threshold and its slope past it (1/ratio) ARE threshold and ratio -
      // this is the static shape of the compressor, independent of program
      // material.
      dl->PathClear();
      const int kNumPoints = 96;
      for (int i = 0; i < kNumPoints; i++)
      {
         const float x = origin.x + (float)i * (curveW / (float)(kNumPoints - 1));
         const float xDb = DynVizXToDb(x, origin.x, curveW);
         const float yDb = DynamicsDsp::GainComputerDb(xDb + inGainDb, threshold, ratio) + makeupDb;
         dl->PathLineTo(ImVec2(x, DynVizDbToY(yDb, origin.y, h)));
      }
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      // Live operating point - where the signal actually is right now, read
      // straight off the kernel's own smoothed envelope (ExtraMeterValue),
      // never recomputed from the static curve. That's what makes attack and
      // release visible: the dot lags behind a sudden input jump and eases
      // back down exactly on the kernel's own timing, not an idealized one.
      const float liveInDb = std::clamp(live.inDb, kDynVizMinDb, kDynVizMaxDb);
      const float liveGrDb = std::clamp(live.grDb, 0.0f, 40.0f);
      const bool hasSignal = liveInDb > kDynVizMinDb + 0.5f;
      if (hasSignal)
      {
         const float liveOutDb = liveInDb + inGainDb - liveGrDb + makeupDb;
         const float dotX = DynVizDbToX(liveInDb, origin.x, curveW);
         const float unprocessedY = DynVizDbToY(liveInDb, origin.y, h);
         const float dotY = DynVizDbToY(liveOutDb, origin.y, h);

         // Fill the gap between the unprocessed diagonal and the live output
         // - the visible "how much" of the reduction happening right now.
         if (liveGrDb > 0.05f)
         {
            dl->AddLine(ImVec2(dotX, unprocessedY), ImVec2(dotX, dotY),
                        isLight ? tok::U32(tok::pal::c_E6781496) : tok::U32(tok::pal::c_FFA03C96), 2.5f);
         }

         const ImU32 dotCol = isLight ? tok::U32(tok::pal::c_141414FF) : tok::U32(tok::pal::c_FFFFFFFF);
         dl->AddCircleFilled(ImVec2(dotX, dotY), 4.0f, dotCol);
         dl->AddCircle(ImVec2(dotX, dotY), 4.0f, ScopeBgCol(), 0, 1.5f);
      }

      AudioViz::End(vizFrame);

      // Gain-reduction meter, a slim bar riding the curve's right edge -
      // release shows here as the bar's own fall time, since it reads the
      // same smoothed value as the dot rather than the instantaneous one.
      {
         const ImVec2 meterOrigin(curveBr.x + grMeterGap, origin.y);
         const ImVec2 meterBr(br.x, br.y);
         const AudioViz::Frame meterFrame = AudioViz::Begin(meterOrigin, grMeterW, h);
         const float grT = std::clamp(liveGrDb / 24.0f, 0.0f, 1.0f);
         const float barTop = meterOrigin.y + (meterBr.y - meterOrigin.y) * (1.0f - grT);
         if (grT > 0.005f)
         {
            dl->AddRectFilled(ImVec2(meterOrigin.x + 1.0f, barTop), ImVec2(meterBr.x - 1.0f, meterBr.y),
                              isLight ? tok::U32(tok::pal::c_E67814E6) : tok::U32(tok::pal::c_FFA03CDC), 1.5f);
         }
         AudioViz::End(meterFrame);
      }

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         if (hasSignal)
         {
            if (ratio >= kLimiterVizRatio)
               snprintf(buf, sizeof(buf), "%.1f dB in, -%.1f dB GR, limit", liveInDb, liveGrDb);
            else
               snprintf(buf, sizeof(buf), "%.1f dB in, -%.1f dB GR, %.0f:1", liveInDb, liveGrDb, ratio);
         }
         else if (ratio >= kLimiterVizRatio)
            snprintf(buf, sizeof(buf), "ceiling %.0f dB", threshold + makeupDb);
         else
            snprintf(buf, sizeof(buf), "%.0f dB in -> %.0f dB out, %.0f:1", threshold, threshold + makeupDb, ratio);
         SetAudioReadout(readoutKey, buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawDynamicsBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[96];
      snprintf(stat, sizeof(stat), "compress %.0f:1 @ %.0f dB%s", n->Param("ratio"), n->Param("threshold"),
               analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawGainComputerVisualizer("dynamics", n->Param("threshold"), n->Param("ratio"), n->Param("makeup"), 0.0f,
                                 { n->ExtraMeterValue(0), n->ExtraMeterValue(1) });
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // KHS Audio Compressor's control surface: threshold, ratio, attack,
      // release, makeup, mix - two rows of three so every knob (mix
      // included) reads as one of the same row instead of mix living apart
      // in its own slider section.
      {
         AudioKnobRow row(3, kKnobLarge);
         row.Knob("threshold", n->ParamPtr("threshold"), -60.0f, 0.0f, "%.1f dB", kKnobLarge);
         row.Knob("ratio", n->ParamPtr("ratio"), 1.0f, 20.0f, "%.1f:1", kKnobLarge);
         row.Knob("attack", n->ParamPtr("attack"), 0.05f, 200.0f, "%.2f ms", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3, kKnobLarge);
         row.Knob("release", n->ParamPtr("release"), 5.0f, 2000.0f, "%.0f ms", kKnobLarge);
         row.Knob("makeup", n->ParamPtr("makeup"), -12.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3, 20.0f, 0.0f, false);
         bool rms = n->Param("detectorRms") != 0.0f;
         bool rmsUserChanged = false;
         if (row.Checkbox("RMS##dynRms", &rms, &rmsUserChanged))
         {
            if (rmsUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("detectorRms") = rms ? 1.0f : 0.0f;
         }
         bool external = n->Param("sidechainExternal") != 0.0f;
         bool externalUserChanged = false;
         if (row.Checkbox("sidechain##dynSidechain", &external, &externalUserChanged))
         {
            if (externalUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("sidechainExternal") = external ? 1.0f : 0.0f;
         }
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##dynAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.End();
      }

      EndAudioBody();
   }


   // ---- Limiter ----------------------------------------------------------
   // A vertical gain-reduction meter, 0/-10/-20 dB scale, bar growing
   // downward from the 0 dB line by the live reduction amount - the shape
   // the reference brief asked for, and distinct enough from
   // DrawStripMeter's plain level bar (no scale, grows up) to warrant its
   // own small ImDrawList routine rather than reusing that primitive.
   void DrawLimiterBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%.0f dB thresh, -%.1f dB GR", n->Param("threshold"),
               std::clamp(n->ExtraMeterValue(0), 0.0f, 20.0f));

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // ExtraMeterValue(0) = GR, (1) = output level; the curve wants the raw input level back.
      const float inGain = n->Param("inGain");
      const float outGain = n->Param("outGain");
      const float gr = std::clamp(n->ExtraMeterValue(0), 0.0f, 20.0f);
      const float outDb = n->ExtraMeterValue(1);
      const float inDb = outDb - inGain + gr - outGain;
      DrawGainComputerVisualizer("limiter", n->Param("threshold"), kLimiterVizRatio, outGain, inGain, { inDb, gr });
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4);
         row.Knob("threshold", n->ParamPtr("threshold"), -30.0f, 0.0f, "%.1f dB", kKnobLarge);
         row.Knob("release", n->ParamPtr("release"), 5.0f, 1000.0f, "%.0f ms", kKnobLarge);
         row.Knob("in gain", n->ParamPtr("inGain"), -12.0f, 24.0f, "%.1f dB");
         row.Knob("out gain", n->ParamPtr("outGain"), -24.0f, 12.0f, "%.1f dB");
         row.End();
      }

      EndAudioBody();
   }


   // ---- Delay ----------------------------------------------------------
   // Decaying tap bars on a time axis, synthesized from feedback (amplitude
   // = feedback^n at n * delay time) - there's only ever one discrete delay
   // path now, so this is exact, not illustrative. Deliberately not a full
   // IR view (README §1's drawing-cost warning): main-thread from params
   // only, except the two meter values for the level pair beneath.
   void DrawDelayVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const float barsH = h - 30.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      // Amplitude gridlines, same 25%-step reading aid Dynamics/Reverb use.
      for (int i = 1; i < 4; i++)
      {
         const float y = origin.y + barsH - (float)i * 0.25f * barsH;
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), ScopeGridCol(), 1.0f);
      }

      const bool sync = n->Param("sync") != 0.0f;
      const int rateDiv = std::clamp((int)(n->Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1);
      float baseSeconds;
      if (sync)
      {
         const double bpm = std::max(1.0f, Transport::Instance().Tempo());
         baseSeconds = (float)(MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv) * 60.0 / bpm);
      }
      else
         baseSeconds = n->Param("timeMs") * 0.001f;

      const float barHalfW = 2.5f;
      const float feedback = std::min(1.0f, n->Param("feedback") / 100.0f); // visualize decay only
      const float windowSeconds = std::max(0.006f, baseSeconds * 6.0f);
      float amp = 1.0f;
      float t = baseSeconds;
      for (int tapIdx = 0; tapIdx < 24 && amp > 0.02f && t < windowSeconds; tapIdx++)
      {
         const float x = origin.x + std::clamp(t / windowSeconds, 0.0f, 1.0f) * w;
         const float barTopY = origin.y + barsH - amp * barsH;
         dl->AddRectFilled(ImVec2(x - barHalfW, barTopY), ImVec2(x + barHalfW, origin.y + barsH),
                           isLight ? tok::U32(tok::pal::c_1E6EE6E6) : tok::U32(tok::pal::c_96D6FFDC));
         amp *= feedback;
         t += baseSeconds;
      }

      dl->AddLine(ImVec2(origin.x, origin.y + barsH), ImVec2(br.x, origin.y + barsH), ScopeMidLineCol(),
                  1.0f);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.0f ms  fb %.0f%%", baseSeconds * 1000.0f, feedback * 100.0f);
         SetAudioReadout("delay", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawDelayBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const bool bounce = n->Param("bounce") != 0.0f;
      const bool analog = n->Param("analog") != 0.0f;

      char stat[112];
      if (sync)
         snprintf(stat, sizeof(stat), "%s - %.0f%% fb - %.0f%% wet%s",
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), n->Param("feedback"),
                  n->mix * 100.0f, analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%.0f ms - %.0f%% fb - %.0f%% wet%s", n->Param("timeMs"), n->Param("feedback"),
                  n->mix * 100.0f, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawDelayVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // KHS Audio Delay's control surface: Time, Tone, Feedback, Pan, Duck,
      // Mix - two rows of three (Bounce is the checkbox below, matching the
      // reference's inline on/off switch). A single row of 6 spread the
      // knobs across the full 440px body with wide, sparse gaps and left the
      // node much wider than it was tall; matching Dynamics' two-row grammar
      // fixes both at once. Rate is a dropdown when synced, a knob when
      // free.
      {
         AudioKnobRow row(3);
         if (sync)
         {
            row.Dropdown("rate", MusicTime::RateDivisionList(), (int)(n->Param("rateDiv") + 0.5f), [n](int i) {
               PushUndoCheckpoint();
               *n->ParamPtr("rateDiv") = (float)i;
            });
         }
         else
         {
            row.Knob("time", n->ParamPtr("timeMs"), 1.0f, 2000.0f, "%.0f ms", kKnobLarge);
         }
         row.Knob("tone", n->ParamPtr("tone"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("feedback", n->ParamPtr("feedback"), 0.0f, 110.0f, "%.0f%%", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("pan", n->ParamPtr("pan"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("duck", n->ParamPtr("ducking"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         // Tight checkbox-only row (maxDia 20 = checkbox frame height,
         // hasCaptions=false) with a small 8px headerRowH gap so it doesn't
         // read as too compacted against the knob row above - same pattern
         // as Pitch Shift/Ring Mod/Flanger/Phaser/Bitcrush/etc.'s trailing
         // analog rows.
         AudioKnobRow row(3, 20.0f, 8.0f, false);
         bool syncBool = sync;
         bool syncBoolUserChanged = false;
         if (row.Checkbox("sync to tempo##delaySync", &syncBool, &syncBoolUserChanged))
         {
            if (syncBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("sync") = syncBool ? 1.0f : 0.0f;
         }
         bool bounceBool = bounce;
         bool bounceBoolUserChanged = false;
         if (row.Checkbox("bounce##delayBounce", &bounceBool, &bounceBoolUserChanged))
         {
            if (bounceBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("bounce") = bounceBool ? 1.0f : 0.0f;
         }
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##delayAnalog", &analogBool, &analogBoolUserChanged))
         {
            if (analogBoolUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
         }
         row.End();
      }

      EndAudioBody();
   }


   // ---- Reverb -----------------------------------------------------------
   // RT60 decay envelope on a time axis, with the predelay gap drawn before
   // the tail begins - a straight line from full amplitude at t=predelay
   // down to -60dB at t=predelay+decay, main-thread from params only (the
   // FDN's actual impulse response is not something worth computing here -
   // README §1's drawing-cost warning - the envelope shape is what a user
   // tweaking size/decay/damping actually needs to see).
   void DrawReverbVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float predelaySeconds = n->Param("predelay") * 0.001f;
      const float decaySeconds = std::max(0.05f, n->Param("decay"));
      const float dampFrac = std::clamp(n->Param("damping"), 0.0f, 1.0f);
      const float windowSeconds = 5.0f;

      for (int sec = 1; sec < (int)windowSeconds; sec++)
      {
         const float x = origin.x + ((float)sec / windowSeconds) * w;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }
      for (int i = 1; i < 4; i++)
      {
         const float y = br.y - (float)i * 0.25f * (h - 6.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), ScopeGridCol(), 1.0f);
      }

      const float predelayX = origin.x + std::clamp(predelaySeconds / windowSeconds, 0.0f, 1.0f) * w;
      dl->AddLine(ImVec2(predelayX, origin.y), ImVec2(predelayX, br.y), ScopeMidLineCol(), 1.0f);

      dl->PathClear();
      const int kNumPoints = 64;
      for (int i = 0; i < kNumPoints; i++)
      {
         const float t = (float)i / (float)(kNumPoints - 1) * windowSeconds;
         float amp;
         if (t < predelaySeconds)
            amp = 1.0f;
         else
         {
            const float tailT = t - predelaySeconds;
            const float rt60Frac = std::clamp(tailT / decaySeconds, 0.0f, 4.0f);
            const float shapedFrac = rt60Frac * (1.0f + dampFrac * 0.5f);
            amp = std::pow(10.0f, -3.0f * shapedFrac);
         }
         const float x = origin.x + (t / windowSeconds) * w;
         const float y = br.y - amp * (h - 6.0f);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.1f s RT60", decaySeconds);
         SetAudioReadout("reverb", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawReverbBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[96];
      snprintf(stat, sizeof(stat), "%.1f s - %.0f%% wet%s", n->Param("decay"), n->mix * 100.0f,
               analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawReverbVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Tier 1: size, decay, damping, predelay, width, mix - one mode
      // (algorithmic FDN, no engine dropdown). Two rows of 3, matching
      // Dynamics and Delay's grammar. Mix is always the last cell of the
      // last row across every AudioEffects node - the standard bottom-right
      // spot, so it reads the same in every effect regardless of how many
      // params sit ahead of it.
      {
         AudioKnobRow row(3);
         row.Knob("size", n->ParamPtr("size"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("decay", n->ParamPtr("decay"), 0.1f, 20.0f, "%.2f s", kKnobLarge);
         row.Knob("damping", n->ParamPtr("damping"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("predelay", n->ParamPtr("predelay"), 0.0f, 500.0f, "%.0f ms", kKnobLarge);
         row.Knob("width", n->ParamPtr("width"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         // Tight checkbox-only row (maxDia 20 = checkbox frame height,
         // hasCaptions=false) with a small 8px headerRowH gap so it doesn't
         // read as too compacted against the knob row above - same pattern
         // as Pitch Shift/Ring Mod/Flanger/Phaser/Bitcrush/etc.'s trailing
         // analog rows.
         AudioKnobRow row(3, 20.0f, 8.0f, false);
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##reverbAnalog", &analogBool, &analogBoolUserChanged))
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


   // ---- Drive ------------------------------------------------------------
   // The transfer curve itself, input -1..+1 -> output -1..+1 - the node's
   // identity, and (per §1.5's spec) main-thread from drive/bias alone, not
   // the live running kernel - same discipline as Dynamics' curve. Tone is
   // a post-shape filter, not part of the static curve, so it's left out
   // here exactly as Delay's tone tilt is left out of the tap-bar view.
   void DrawDriveVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      // Center gridlines (x=0, y=0) plus quarter-amplitude ticks, the same
      // reading aid Dynamics/Reverb give their curves.
      for (int i = -2; i <= 2; i++)
      {
         const float x = origin.x + (0.5f + 0.25f * (float)i) * w;
         const float y = origin.y + (0.5f - 0.25f * (float)i) * h;
         const bool center = (i == 0);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), center ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), center ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
      }

      // 1:1 reference diagonal (unshaped signal), dim.
      dl->AddLine(ImVec2(origin.x, br.y), ImVec2(br.x, origin.y), ScopeMidLineCol(), 1.0f);

      const float driveDb = n->Param("drive");
      const float bias = n->Param("bias");
      const float color = n->Param("color");

      dl->PathClear();
      const int kNumPoints = 96;
      for (int i = 0; i < kNumPoints; i++)
      {
         const float xIn = -1.0f + 2.0f * (float)i / (float)(kNumPoints - 1);
         const float yOut = std::clamp(DriveDsp::Shape(xIn, driveDb, bias, color), -1.0f, 1.0f);
         const float x = origin.x + (0.5f + 0.5f * xIn) * w;
         const float y = origin.y + (0.5f - 0.5f * yOut) * h;
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.1f dB drive, bias %.2f", driveDb, bias);
         SetAudioReadout("drive", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawDriveBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "%.1f dB drive - %.0f%% wet", n->Param("drive"), n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawDriveVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Tier 1 only: drive, bias, tone, color, output, mix - one processing
      // mode (tanh blended toward arctan via `color`), no curve dropdown.
      // Two rows of three so mix lands in its standard bottom-right spot,
      // matching Dynamics/Delay/Reverb's grammar.
      {
         AudioKnobRow row(3);
         row.Knob("drive", n->ParamPtr("drive"), 0.0f, 40.0f, "%.1f dB", kKnobLarge);
         row.Knob("bias", n->ParamPtr("bias"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("tone", n->ParamPtr("tone"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("color", n->ParamPtr("color"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("output", n->ParamPtr("output"), -24.0f, 12.0f, "%.1f dB", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }


   // ---- Wavetable Shaper ---------------------------------------------------
   // The transfer curve itself, input -1..+1 -> output -1..+1, exactly like
   // Drive's - main-thread from params alone via WavetableShaperDsp::Shape,
   // never the live kernel, so it's always meaningful with no audio running.
   // The one addition Drive's curve doesn't have: a live operating-point dot
   // at the kernel's last input sample (ExtraMeterValue(0)), since this curve
   // is otherwise entirely static and the dot is what makes it read as an
   // instrument rather than a diagram.
   void DrawWavetableShaperVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      for (int i = -2; i <= 2; i++)
      {
         const float x = origin.x + (0.5f + 0.25f * (float)i) * w;
         const float y = origin.y + (0.5f - 0.25f * (float)i) * h;
         const bool center = (i == 0);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), center ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), center ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
      }

      // 1:1 reference diagonal (unshaped signal), dim.
      dl->AddLine(ImVec2(origin.x, br.y), ImVec2(br.x, origin.y), ScopeMidLineCol(), 1.0f);

      const int table = (int)(n->Param("table") + 0.5f);
      const float position = n->Param("position");
      const float driveDb = n->Param("drive");
      const float bias = n->Param("bias");
      const float smooth = n->Param("smooth");
      const float stereo = std::clamp(n->Param("stereo"), 0.0f, 1.0f);
      const float positionL = position - stereo * 0.5f;
      const float positionR = position + stereo * 0.5f;

      // At stereo=0 positionL==positionR==position, so the two traces below
      // draw on top of each other and it looks exactly like the single-curve
      // original - same alpha-layering technique as DrawChorusVisualizer's
      // taps, not a second visualizer mode.
      const int kNumPoints = 96;
      const float centerY = origin.y + 0.5f * h;
      for (int pass = 0; pass < (stereo > 0.0f ? 2 : 1); pass++)
      {
         const float tracePosition = (pass == 1) ? positionR : positionL;
         const int alpha = (stereo > 0.0f) ? (pass == 0 ? 200 : 150) : 255;
         const ImU32 fillCol = isLight ? IM_COL32(30, 110, 230, (int)(alpha * 0.12f)) : IM_COL32(100, 180, 255, (int)(alpha * 0.14f));
         for (int i = 0; i < kNumPoints - 1; i++)
         {
            const float xIn0 = -1.0f + 2.0f * (float)i / (float)(kNumPoints - 1);
            const float xIn1 = -1.0f + 2.0f * (float)(i + 1) / (float)(kNumPoints - 1);
            const float yOut0 =
               std::clamp(WavetableShaperDsp::Shape(xIn0, table, tracePosition, driveDb, bias, smooth), -1.0f, 1.0f);
            const float yOut1 =
               std::clamp(WavetableShaperDsp::Shape(xIn1, table, tracePosition, driveDb, bias, smooth), -1.0f, 1.0f);
            const float x0 = origin.x + (0.5f + 0.5f * xIn0) * w;
            const float x1 = origin.x + (0.5f + 0.5f * xIn1) * w;
            const float y0 = origin.y + (0.5f - 0.5f * yOut0) * h;
            const float y1 = origin.y + (0.5f - 0.5f * yOut1) * h;
            dl->AddQuadFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x1, centerY), ImVec2(x0, centerY), fillCol);
         }
         dl->PathClear();
         for (int i = 0; i < kNumPoints; i++)
         {
            const float xIn = -1.0f + 2.0f * (float)i / (float)(kNumPoints - 1);
            const float yOut =
               std::clamp(WavetableShaperDsp::Shape(xIn, table, tracePosition, driveDb, bias, smooth), -1.0f, 1.0f);
            const float x = origin.x + (0.5f + 0.5f * xIn) * w;
            const float y = origin.y + (0.5f - 0.5f * yOut) * h;
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(isLight ? IM_COL32(30, 110, 230, alpha) : IM_COL32(150, 214, 255, alpha), 0, 1.8f);
      }

      // Live operating-point dot at the kernel's last input sample.
      const float lastIn = std::clamp(n->ExtraMeterValue(0), -1.0f, 1.0f);
      const float lastOut =
         std::clamp(WavetableShaperDsp::Shape(lastIn, table, position, driveDb, bias, smooth), -1.0f, 1.0f);
      const ImVec2 dot(origin.x + (0.5f + 0.5f * lastIn) * w, origin.y + (0.5f - 0.5f * lastOut) * h);
      dl->AddCircleFilled(dot, 3.5f, isLight ? tok::U32(tok::pal::c_E67814E6) : tok::U32(tok::pal::c_FFD678E6));

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[80];
         snprintf(buf, sizeof(buf), "%s, position %.2f, %.1f dB drive", Wavetable::TableName(table), position,
                   driveDb);
         SetAudioReadout("wavetable shaper", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawWavetableShaperBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float stereo = n->Param("stereo");
      char stat[112];
      if (stereo > 0.0f)
         snprintf(stat, sizeof(stat), "%s - %.1f dB drive - %.0f%% stereo - %.0f%% wet",
                  Wavetable::TableName((int)(n->Param("table") + 0.5f)), n->Param("drive"), stereo * 100.0f,
                  n->mix * 100.0f);
      else
         snprintf(stat, sizeof(stat), "%s - %.1f dB drive - %.0f%% wet",
                  Wavetable::TableName((int)(n->Param("table") + 0.5f)), n->Param("drive"), n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawWavetableShaperVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Tier 1 only: table, position, drive, stereo, bias, smooth, output,
      // mix - one processing mode. `table` is the curve's identity (a
      // dropdown, same carve-out as Audio Filter's `type`/Ring Mod's
      // `waveform`), not a second mode selector. Both rows are 4 cells so
      // they share column edges (P1); row 1 is table/position/drive/stereo,
      // row 2 is bias/smooth/output/mix so mix still lands in the standard
      // bottom-right spot every AudioEffects node's grammar puts it in.
      {
         AudioKnobRow row(4);
         const int table = (int)(n->Param("table") + 0.5f);
         row.Dropdown("table", WavetableNames(), table, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("table") = (float)i;
         });
         row.Knob("position", n->ParamPtr("position"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("drive", n->ParamPtr("drive"), 0.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.Knob("stereo", n->ParamPtr("stereo"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.Knob("bias", n->ParamPtr("bias"), -1.0f, 1.0f, "%.2f", kKnobSmall);
         row.Knob("smooth", n->ParamPtr("smooth"), 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.Knob("output", n->ParamPtr("output"), -24.0f, 12.0f, "%.1f dB", kKnobSmall);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.End();
      }

      EndAudioBody();
   }


   // ---- Stereo -------------------------------------------------------------
   // A polar stereo-field imager matching the classic hardware-plugin
   // goniometer layout: concentric dB-ring arcs fanning up from a bottom-
   // center apex, a dB scale along the baseline (loud at the outer edges,
   // quiet toward center), and a filled wedge whose two edges swing left/
   // right of vertical - `width` opens the wedge, `pan` shifts it - the
   // same picture the reference plugin's stereo field display uses.
   // `bassMono` is a frequency-domain effect a polar field can't depict, so
   // it's shown as a text label instead.
   void DrawStereoVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 apex(origin.x + w * 0.5f, br.y - 20.0f);
      const float radius = std::min(w * 0.48f, h - 26.0f);

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      // Concentric dB rings, apex-centered, sweeping only the upper half.
      const int kNumRings = 5;
      for (int i = 1; i <= kNumRings; i++)
      {
         const float r = radius * (float)i / (float)kNumRings;
         dl->PathClear();
         dl->PathArcTo(apex, r, (float)M_PI, 2.0f * (float)M_PI, 32);
         dl->PathStroke(ScopeGridCol(), 0, 1.0f);
      }
      dl->AddLine(ImVec2(apex.x, apex.y), ImVec2(apex.x, apex.y - radius), ScopeMidLineCol(), 1.0f);
      dl->AddLine(ImVec2(apex.x - radius, apex.y), ImVec2(apex.x + radius, apex.y), ScopeMidLineCol(), 1.0f);

      static const char* kDbLabels[] = { "0", "-6", "-12", "-18", "-24" };
      for (int i = 0; i < kNumRings; i++)
      {
         const float t = (float)i / (float)kNumRings;
         const float x = apex.x - radius + t * radius;
         dl->AddText(ImVec2(x - 6.0f, apex.y + 4.0f), ScopeTextCol(), kDbLabels[kNumRings - 1 - i]);
         const float xr = apex.x + radius - t * radius;
         dl->AddText(ImVec2(xr - 6.0f, apex.y + 4.0f), ScopeTextCol(), kDbLabels[kNumRings - 1 - i]);
      }

      const float width = std::clamp(n->Param("width"), 0.0f, 2.0f);
      const float pan = std::clamp(n->Param("pan"), -1.0f, 1.0f);
      const float bassMonoHz = std::max(0.0f, n->Param("bassMono"));

      const float widthNorm = width * 0.5f;
      const float maxHalfAngle = 75.0f * (float)M_PI / 180.0f;
      const float panShift = pan * 30.0f * (float)M_PI / 180.0f;
      const float leftAngle = panShift - maxHalfAngle * widthNorm;
      const float rightAngle = panShift + maxHalfAngle * widthNorm;
      const float wedgeR = radius * 0.92f;

      auto polar = [&](float angle) {
         return ImVec2(apex.x + wedgeR * sinf(angle), apex.y - wedgeR * cosf(angle));
      };
      const ImVec2 leftPt = polar(leftAngle);
      const ImVec2 rightPt = polar(rightAngle);

      dl->PathClear();
      dl->PathLineTo(apex);
      dl->PathLineTo(leftPt);
      dl->PathLineTo(rightPt);
      dl->PathFillConvex(isLight ? tok::U32(tok::pal::c_6E5AD250) : tok::U32(tok::pal::c_9682E65A));

      dl->PathClear();
      dl->PathLineTo(leftPt);
      dl->PathLineTo(apex);
      dl->PathLineTo(rightPt);
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_7850E6EB) : tok::U32(tok::pal::c_BEAAFFEB), 0, 1.8f);

      if (bassMonoHz > 0.0f)
      {
         char blo[24];
         snprintf(blo, sizeof(blo), "mono <%.0f Hz", bassMonoHz);
         dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f),
                     isLight ? tok::U32(tok::pal::c_D26414E6) : tok::U32(tok::pal::c_FFC478D2), blo);
      }

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[48];
         const float corr = std::clamp(n->ExtraMeterValue(0), -1.0f, 1.0f);
         snprintf(buf, sizeof(buf), "width %.2f - pan %+.2f - corr %.2f", width, pan, corr);
         SetAudioReadout("stereo", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawStereoBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "width %.2f - pan %.2f", n->Param("width"), n->Param("pan"));

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawStereoVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(3);
      row.Knob("width", n->ParamPtr("width"), 0.0f, 2.0f, "%.2f", kKnobLarge);
      row.Knob("pan", n->ParamPtr("pan"), -1.0f, 1.0f, "%.2f", kKnobLarge);
      row.Knob("bass mono", n->ParamPtr("bassMono"), 0.0f, 500.0f, "%.0f Hz", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewBassMono);
      row.End();

      EndAudioBody();
   }


   // ---- Pitch Shifter --------------------------------------------------
   // A simple bipolar meter (-24..+24 semitones) rather than a spectrogram -
   // the shift amount is the node's whole identity and a spectrogram would
   // cost far more to draw than this effect's DSP itself (README §1).
   void DrawPitchShiftVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      const AudioViz::Frame vizFrame = AudioViz::Begin(origin, w, h);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeGridCol(), 1.0f);
      const float centerX = origin.x + 0.5f * w;
      dl->AddLine(ImVec2(centerX, origin.y), ImVec2(centerX, br.y), ScopeMidLineCol(), 1.0f);

      const float pitch = std::clamp(n->Param("pitch"), -24.0f, 24.0f);
      const float t = 0.5f + 0.5f * (pitch / 24.0f);
      const float markX = origin.x + t * w;
      dl->AddCircleFilled(ImVec2(markX, midY), 6.0f, isLight ? tok::U32(tok::pal::c_1E6EE6F5) : tok::U32(tok::pal::c_96D6FFF5));

      char buf[32];
      snprintf(buf, sizeof(buf), "%+.1f st", pitch);
      dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f), ScopeTextCol(), buf);

      AudioViz::End(vizFrame);

      if (ImGui::IsMouseHoveringRect(origin, br))
         SetAudioReadout("pitch shifter", buf);

      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawPitchShiftBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool analog = n->Param("analog") != 0.0f;
      char stat[96];
      snprintf(stat, sizeof(stat), "%+.1f st - %.0f ms grain%s", n->Param("pitch"), n->Param("grain"),
               analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawPitchShiftVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(3);
      row.Knob("pitch", n->ParamPtr("pitch"), -24.0f, 24.0f, "%+.1f st", kKnobLarge);
      row.Knob("grain", n->ParamPtr("grain"), 10.0f, 250.0f, "%.0f ms", kKnobLarge);
      row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();

      {
         // maxDia 20 (checkbox frame height) + hasCaptions=false: a
         // checkbox-only row draws its label inline, never below the control
         // the way a knob's caption does, so leaving this at the default
         // AudioKnobRow(3) (maxDia kKnobSmall=40, hasCaptions=true) reserved a
         // full knob-height-plus-caption-strip that nothing ever drew into -
         // the dead gap between this checkbox and the node's bottom border.
         // Same tight-row pattern already used by Dynamics/SpecBlur/Switcher's
         // trailing checkbox rows.
         AudioKnobRow row(3, 20.0f, 8.0f, false);
         bool analogBool = analog;
         bool analogBoolUserChanged = false;
         if (row.Checkbox("analog##pitchShiftAnalog", &analogBool, &analogBoolUserChanged))
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


   // Shared rate control for Tremolo/Chorus/Flanger/Phaser - a rateDiv
   // dropdown when synced, a free-Hz knob when not - added as the next cell
   // of an already-open AudioKnobRow so it sits alongside the effect's other
   // knobs instead of claiming a row of its own. `rateLo`/`rateHi` match
   // each effect's own EffectDefs "rate" range (Tremolo's is 0.05-20 Hz;
   // Chorus/Flanger/Phaser share 0.02-5 Hz, the default here).
   void AddSyncedRateCell(AudioKnobRow& row, AudioEffectNode* n, float rateLo, float rateHi)
   {
      const bool sync = n->Param("sync") != 0.0f;
      if (sync)
      {
         row.Dropdown("rate", MusicTime::RateDivisionList(), (int)(n->Param("rateDiv") + 0.5f), [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("rateDiv") = (float)i;
         });
         // The free-Hz knob isn't drawn while synced, but it still must
         // consume a gParamCounter ordinal - same trick, same reason, as
         // DrawRateModeControls' reserved-slot comment above: ordinals are
         // assigned by draw order, and the dropdown just drawn is a
         // *discrete* param (numbered from a hash of its label, see
         // DiscreteParamSlot) that doesn't touch gParamCounter at all. Skipping
         // the knob call here would leave this cell consuming zero ordinals
         // while synced versus one while free, shifting every later
         // gParamCounter-numbered control in the body (this effect's own
         // "mix") down by one the instant sync flips, silently repointing
         // whatever cable was patched into it.
         ParamRef rateSlot;
         rateSlot.nodeIndex = gCurrentNodeIndex;
         rateSlot.paramIndex = gParamCounter++;
         rateSlot.value = n->ParamPtr("rate");
         rateSlot.minValue = rateLo;
         rateSlot.maxValue = rateHi;
         rateSlot.name = "rate";
         Modulation::Instance().RegisterParam(rateSlot);
      }
      else
      {
         row.Knob("rate", n->ParamPtr("rate"), rateLo, rateHi, "%.2f Hz", kKnobLarge);
      }
   }


   // Shared [rate-mode v][rate] pair for the four AudioEffects with a
   // tempo-sync toggle (Tremolo, Chorus, Flanger, Phaser) - the effect-side
   // equivalent of the note generators' DrawRateModeControls. Cell 1 is a
   // Synced/Free dropdown replacing the old free-floating/row "sync to
   // tempo" checkbox (see node-ui-pillars P1/P3); cell 2 is
   // AddSyncedRateCell's existing rate-division-dropdown-or-Hz-knob cell.
   //
   // `syncLabel` must be the exact label id the node's old sync checkbox
   // used (e.g. "sync to tempo##chorusSync"). Discrete params are numbered
   // from a hash of their label, not from draw order (DiscreteParamSlot), so
   // reusing the old label keeps this dropdown on the same pin address the
   // checkbox occupied - an already-saved patch's modulation binding on the
   // sync toggle, if any, still resolves to this control rather than going
   // permanently inactive. Note this does flip that binding's polarity: a
   // cable driving the old checkbox toward 1.0 meant "synced"; driving this
   // 2-option dropdown ("Synced" first, matching the note-generator
   // convention this mirrors) toward 1.0 now selects "Free" instead. That
   // narrow case - a cable bound specifically to the sync toggle itself - is
   // an accepted, documented consequence of the checkbox-to-dropdown
   // conversion; `depth`/`rate`/`mix` bindings are unaffected.
   void AddRateModeCells(AudioKnobRow& row, AudioEffectNode* n, const char* syncLabel,
                         float rateLo, float rateHi)
   {
      static const std::vector<std::string> kSyncModes = { "Synced", "Free" };
      const bool sync = n->Param("sync") != 0.0f;
      row.Dropdown(syncLabel, kSyncModes, sync ? 0 : 1, [n](int i) {
         PushUndoCheckpoint();
         *n->ParamPtr("sync") = (i == 0) ? 1.0f : 0.0f;
      });
      AddSyncedRateCell(row, n, rateLo, rateHi);
   }
}
