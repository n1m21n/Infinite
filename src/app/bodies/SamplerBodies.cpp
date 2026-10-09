// Sampler, slicer, molder, granular, drum sequencer, looper and MPC bodies (moved verbatim from main.cpp).
#include "app/ui/design/components/PinDot.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   // ---- Audio Meter ------------------------------------------------------
   // A stereo bridge meter: L and R bars either side of one shared dB scale,
   // each showing RMS (solid) inside peak (translucent), a peak-hold line,
   // and a max-peak readout above that turns red once the channel has hit
   // 0 dBFS. Click anywhere on it to clear the readouts, holds and clip.
   //
   // The scale is piecewise rather than linear in dB so the working range
   // (-24..0) gets most of the pixels - a linear -60..0 scale gives -60..-40,
   // which nobody mixes at, as much height as the top 20 dB. Breakpoints are
   // our own, picked so every labelled tick stays at least one text line
   // apart at the meter's fixed height.
   namespace AudioMeterScale
   {
      struct Point { float db, frac; };
      static const Point kCurve[] = {
         { -60.0f, 0.00f }, { -48.0f, 0.10f }, { -36.0f, 0.22f }, { -24.0f, 0.38f },
         { -18.0f, 0.48f }, { -12.0f, 0.60f }, { -6.0f, 0.75f },  { -3.0f, 0.83f },
         { 0.0f, 0.92f },   { 3.0f, 1.00f },
      };

      inline float DbToFrac(float db)
      {
         const int n = (int)(sizeof(kCurve) / sizeof(kCurve[0]));
         if (db <= kCurve[0].db)
            return 0.0f;
         for (int i = 1; i < n; i++)
         {
            if (db <= kCurve[i].db)
            {
               const float t = (db - kCurve[i - 1].db) / (kCurve[i].db - kCurve[i - 1].db);
               return kCurve[i - 1].frac + t * (kCurve[i].frac - kCurve[i - 1].frac);
            }
         }
         return 1.0f;
      }

      inline float LevelToDb(float lvl) { return lvl > 1e-5f ? 20.0f * log10f(lvl) : -120.0f; }

      // "-inf" below the floor, explicit sign above 0 so an over reads as one.
      inline void FormatDb(char* buf, size_t size, float lvl)
      {
         const float db = LevelToDb(lvl);
         if (db <= -99.0f)
            snprintf(buf, size, "-inf");
         else if (db > 0.05f)
            snprintf(buf, size, "+%.1f", db);
         else
            snprintf(buf, size, "%.1f", db);
      }
   }


   void DrawAudioMeterVisualizer(AudioMeterNode* n, float x, float y, float w, float h)
   {
      using namespace AudioMeterScale;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin(x, y);
      const ImVec2 br(x + w, y + h);
      const bool isLight = IsThemeLight();
      // Every legend in the meter is drawn at a reduced size: at the body
      // font the scale numbers crowded the bars and each other on a 200px
      // body. The node's own caption/readout strip keep the normal size.
      ImFont* font = ImGui::GetFont();
      const float fs = tok::type_caption;
      const float textH = fs;
      auto textSize = [&](const char* t) { return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t); };
      auto text = [&](ImVec2 p, ImU32 c, const char* t) { dl->AddText(font, fs, p, c, t); };

      dl->AddRectFilled(origin, br, ScopeBgCol(), 3.0f);

      // Zone colours, shared by bars, hold lines and readouts: green for
      // normal level, amber for the last 6 dB of headroom, red for overs.
      const ImU32 kGreen = isLight ? tok::U32(tok::pal::c_28A55AFF) : tok::U32(tok::pal::c_4BD778FF);
      const ImU32 kAmber = isLight ? tok::U32(tok::pal::c_DE961EFF) : tok::U32(tok::pal::c_FFBE5AFF);
      const ImU32 kRed = isLight ? tok::U32(tok::pal::c_DC372DFF) : tok::U32(tok::pal::c_FF5A50FF);
      const ImU32 slotCol = isLight ? tok::U32(tok::pal::c_0000000E) : tok::U32(tok::pal::c_FFFFFF0A);
      auto zoneCol = [&](float db) { return db > 0.0f ? kRed : db > -6.0f ? kAmber : kGreen; };
      auto withAlpha = [](ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT); };

      // Geometry: [readout] row on top, bars in the middle, L/R captions
      // below; bars sit either side of a centred scale column.
      const float pad = 10.0f;
      const float scaleW = 34.0f;
      const float barW = std::min(36.0f, (w - 2.0f * pad - scaleW) * 0.5f);
      const float groupW = 2.0f * barW + scaleW;
      const float lx = x + (w - groupW) * 0.5f;
      const float rx = lx + barW + scaleW;
      const float scaleCx = lx + barW + scaleW * 0.5f;
      const float readTop = y + pad;
      const float readH = textH + 6.0f;
      const float barTop = readTop + readH + 10.0f;
      const float barBot = br.y - pad - textH - 6.0f;
      const float barH = barBot - barTop;
      auto yOf = [&](float db) { return barBot - DbToFrac(db) * barH; };

      // Scale: labelled ticks as short marks in the centre column plus a
      // faint line across each bar slot; unlabelled minor marks between.
      dl->AddRectFilled(ImVec2(lx, barTop), ImVec2(lx + barW, barBot), slotCol, 2.0f);
      dl->AddRectFilled(ImVec2(rx, barTop), ImVec2(rx + barW, barBot), slotCol, 2.0f);
      static const float kLabelled[] = { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f };
      static const float kMinor[] = { 3.0f, -3.0f, -9.0f, -18.0f, -30.0f, -42.0f, -54.0f };
      for (float db : kMinor)
      {
         const float ty = std::floor(yOf(db)) + 0.5f;
         dl->AddLine(ImVec2(lx + barW + 2.0f, ty), ImVec2(lx + barW + 5.0f, ty), ScopeMidLineCol(), 1.0f);
         dl->AddLine(ImVec2(rx - 5.0f, ty), ImVec2(rx - 2.0f, ty), ScopeMidLineCol(), 1.0f);
      }
      for (float db : kLabelled)
      {
         const float ty = std::floor(yOf(db)) + 0.5f;
         const ImU32 lineCol = db == 0.0f ? ScopeMidLineCol() : ScopeGridCol();
         dl->AddLine(ImVec2(lx, ty), ImVec2(lx + barW, ty), lineCol, 1.0f);
         dl->AddLine(ImVec2(rx, ty), ImVec2(rx + barW, ty), lineCol, 1.0f);
         char buf[8];
         snprintf(buf, sizeof(buf), "%.0f", db);
         const ImVec2 ts = textSize(buf);
         text(ImVec2(scaleCx - ts.x * 0.5f, ty - ts.y * 0.5f), ScopeTextCol(), buf);
      }

      // One channel: peak bar (translucent), RMS bar (solid) over it, both
      // split into colour zones so the bar reads like a hardware LED stack
      // rather than one block that changes colour all at once.
      static const float kZoneEdges[] = { -120.0f, -6.0f, 0.0f, 3.0f };
      const ImU32 kZoneCols[] = { kGreen, kAmber, kRed };
      auto fillZones = [&](float bx, float levelDb, int alpha) {
         if (levelDb <= -60.0f)
            return;
         for (int z = 0; z < 3; z++)
         {
            const float lo = kZoneEdges[z];
            const float hi = std::min(kZoneEdges[z + 1], levelDb);
            if (hi <= lo)
               break;
            const float y0 = yOf(hi);
            const float y1 = z == 0 ? barBot : yOf(lo);
            dl->AddRectFilled(ImVec2(bx + 1.0f, y0), ImVec2(bx + barW - 1.0f, y1),
                              withAlpha(kZoneCols[z], alpha));
         }
      };
      auto drawChannel = [&](float bx, float peak, float rms, float hold) {
         dl->PushClipRect(ImVec2(bx, barTop), ImVec2(bx + barW, barBot), true);
         fillZones(bx, LevelToDb(peak), isLight ? 90 : 80);
         fillZones(bx, LevelToDb(rms), isLight ? 235 : 225);
         // LED segmentation: a 1px gap every 3px, in the scope background.
         const ImU32 gapCol = withAlpha(ScopeBgCol(), 150);
         for (float gy = barBot - 3.0f; gy > barTop; gy -= 3.0f)
            dl->AddLine(ImVec2(bx, gy), ImVec2(bx + barW, gy), gapCol, 1.0f);
         const float holdDb = LevelToDb(hold);
         if (holdDb > -60.0f)
         {
            const float hy = yOf(holdDb);
            dl->AddRectFilled(ImVec2(bx + 1.0f, hy - 1.0f), ImVec2(bx + barW - 1.0f, hy + 1.0f), zoneCol(holdDb));
         }
         dl->PopClipRect();
      };
      drawChannel(lx, n->PeakL(), n->RmsL(), n->PeakHoldL());
      drawChannel(rx, n->PeakR(), n->RmsR(), n->PeakHoldR());

      // Max-peak readouts, one per bar. Red box once that channel clipped.
      auto drawReadout = [&](float bx, float maxPeak, bool clipped) {
         const ImVec2 a(bx, readTop), b(bx + barW, readTop + readH);
         dl->AddRectFilled(a, b, clipped ? kRed : slotCol, 2.0f);
         char buf[12];
         FormatDb(buf, sizeof(buf), maxPeak);
         const ImVec2 ts = textSize(buf);
         const ImU32 txt = clipped ? tok::U32(tok::pal::c_FFFFFFFF)
                                   : maxPeak > 1e-5f ? ImGui::GetColorU32(ImGuiCol_Text) : ScopeTextCol();
         text(ImVec2(bx + (barW - ts.x) * 0.5f, readTop + (readH - ts.y) * 0.5f), txt, buf);
      };
      drawReadout(lx, n->MaxPeakL(), n->ClipL());
      drawReadout(rx, n->MaxPeakR(), n->ClipR());
      {
         const ImVec2 ts = textSize("pk");
         text(ImVec2(scaleCx - ts.x * 0.5f, readTop + (readH - ts.y) * 0.5f), ScopeTextCol(), "pk");
      }

      // Channel captions under the bars; "dB" under the scale.
      auto caption = [&](float cx, const char* s) {
         const ImVec2 ts = textSize(s);
         text(ImVec2(cx - ts.x * 0.5f, barBot + 5.0f), ScopeTextCol(), s);
      };
      caption(lx + barW * 0.5f, "L");
      caption(rx + barW * 0.5f, "R");
      caption(scaleCx, "dB");

      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##audiometer", ImVec2(w, h));
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
         n->ResetPeaks();
      if (ImGui::IsItemHovered())
      {
         char l[12], r[12], buf[64];
         FormatDb(l, sizeof(l), n->RmsL());
         FormatDb(r, sizeof(r), n->RmsR());
         snprintf(buf, sizeof(buf), "L %s  R %s", l, r);
         SetAudioReadout("rms", buf);
      }
   }


   void DrawAudioMeterBody(GraphNode& gn, AudioMeterNode* n)
   {
      using namespace AudioMeterScale;
      char stat[64];
      if (!n->HasInput())
         snprintf(stat, sizeof(stat), "no input");
      else if (n->ClipL() || n->ClipR())
         snprintf(stat, sizeof(stat), "CLIP %s%s - click to reset", n->ClipL() ? "L" : "", n->ClipR() ? "R" : "");
      else if (n->PeakL() > 1e-5f || n->PeakR() > 1e-5f)
      {
         char l[12], r[12];
         FormatDb(l, sizeof(l), n->PeakL());
         FormatDb(r, sizeof(r), n->PeakR());
         snprintf(stat, sizeof(stat), "L %s  R %s dB", l, r);
      }
      else
         snprintf(stat, sizeof(stat), "no signal");

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Three kKnobLarge knob rows tall - 1.5x Limiter's gain-reduction
      // meter (two rows, DrawLimiterBody). At the Limiter's height the
      // -48..0 span was too short to read a level against the ticks.
      const float rowH = kKnobLarge + 4.0f + ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
      const float meterH = 3.0f * rowH;
      DrawAudioMeterVisualizer(n, gAudioContentX, ImGui::GetCursorScreenPos().y, gAudioContentW, meterH);

      EndAudioBody();
   }


   void DrawGainBody(GraphNode& gn, GainNode* n)
   {
      // One param, so §7's "a one-param node is visually smaller" holds. A
      // knob, not the Mixer-strip fader this used to be: a lone control with
      // no neighbours to compare against gets none of the "read several
      // levels as a balance at a glance" benefit a fader row earns, so it
      // reverts to the app's default control.
      char stat[48];
      snprintf(stat, sizeof(stat), "%+.1f dB   %s", n->gainDb,
               n->Level() > 0.0005f ? "\xe2\x96\xb8" : "-");
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(1, kKnobLarge);
      row.Knob("gain", &n->gainDb, -60.0f, 12.0f, "%.1f dB", kKnobLarge, /*dbTaper=*/true);
      row.End();
      EndAudioBody();
   }


   void DrawBlendAudioBody(GraphNode& gn, BlendAudioNode* n)
   {
      char stat[48];
      const int pctB = (int)std::lround(n->blend * 100.0f);
      snprintf(stat, sizeof(stat), "A <-> B   %d%%", pctB);
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(1, kKnobLarge);
      row.Knob("blend", &n->blend, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();
      EndAudioBody();
   }


   // A sample player: load a file, record from the audio input pin, scrub/
   // audition by clicking the waveform, drag its two edge handles to trim
   // the playback/loop range, and play back one-shot or looping (forward,
   // reversed, or ping-ponging) with independent pitch/finetune/speed/
   // volume. Also the drop target when a sample is dragged onto this node's
   // body from the Samples search panel (see gSampleDragActive) - same
   // LoadFile() either way.
   //
   // Sliders rather than knobs (per reference: Amigo/Bespoke-style sample
   // players read as one flat control strip, not a cluster of dials), and a
   // waveform tall enough to actually judge a transient against - a sampler
   // lives or dies on "where does this sample start", and a 64px strip was
   // too short to place that by eye.
   void DrawSamplerBody(GraphNode& gn, SamplerNode* n)
   {
      char stat[160];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...", ImVec2(90, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      // Dropping a sample from the Samples search panel directly onto this
      // node's body is handled at the canvas level (see gSampleDragActive),
      // not through ImGui's native drag-drop payload API - the drag source
      // is a manually-tracked mouse drag, not a BeginDragDropSource/
      // SetDragDropPayload pair, since the drop target here is really
      // "whichever node the drop lands on", resolved once in one place
      // rather than duplicated as a per-node-type accept.
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(recording ? "Stop" : "Record", ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (recording)
            n->StopRecording();
         else
            n->StartRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      // Audition control, independent of the transport and any note cable -
      // it only ever touches the self lane's dedicated voice (see
      // SamplerNode.h), never a note-lane voice, so it can't be cut off by
      // or cut off an incoming note. With loop (or free-running/no note
      // cable) a triggered voice sounds forever, and clicking the waveform
      // again only retriggers from a new point rather than silencing it, so
      // there needs to be a dedicated way to stop it.
      ImGui::SameLine();
      const bool playing = n->IsPlaying();
      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(playing ? "Stop" : "Audition", ImVec2(90, 0)))
      {
         if (playing)
            n->StopPreview();
         else
            n->TriggerPreview(n->start);
      }
      if (playing)
         ImGui::PopStyleColor();
      // A plain ImGui tooltip here would land offset from the cursor, proportional
      // to the node editor's zoom/pan - see gAudioReadout's comment for why every
      // hover explanation in this file goes through the fixed readout strip instead.
      if (ImGui::IsItemHovered())
         SetAudioReadout("#", "previews this node only - space starts the patch");

      // loop/reverse/ping-pong right-aligned on the same row, so the top of
      // the body reads as one bar rather than stacked controls fighting for
      // the same strip of height. Pill toggles (AudioToggleButton), not
      // checkboxes - see its comment for why.
      //
      // Anchored off gAudioContentX/gAudioContentW (the node's fixed
      // declared width from BeginAudioBody), not GetContentRegionAvail() -
      // avail tracks the node's *live* current width, which sits flush
      // against imgui-node-editor's own resize-drag hit-zone at the node's
      // right edge. A row right-justified against that live edge overlaps
      // that hit-zone, so a drag starting on/near these buttons got read as
      // a node-resize instead of a click, and the row (laid out from the
      // stale pre-resize width) visibly lagged behind it. Every other
      // control in this file already anchors off the fixed content width
      // for exactly this reason.
      ImGui::SameLine();
      const float toggleW = 44.0f;
      const float modsW = toggleW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
      const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - modsW, rowScreenPos.y));
      bool loopBool = n->loop;
      if (AudioToggleButton("loop##samplerLoop", &loopBool))
      {
         PushUndoCheckpoint();
         n->loop = loopBool;
      }
      ImGui::SameLine();
      bool reverseBool = n->reverse;
      if (AudioToggleButton("rev##samplerReverse", &reverseBool))
      {
         PushUndoCheckpoint();
         n->reverse = reverseBool;
      }
      ImGui::SameLine();
      bool pingpongBool = n->pingpong;
      if (AudioToggleButton("p-p##samplerPingpong", &pingpongBool))
      {
         PushUndoCheckpoint();
         n->pingpong = pingpongBool;
      }

      // Read-only status word, right-aligned on its own line: what's
      // actually making this node sound right now. A note cable takes
      // priority in the label since that's the more specific/interesting
      // state; "auditioning" only shows when nothing's patched into notes
      // and the self voice is user- rather than transport-owned (see
      // SamplerNode.h's IsAuditioning()). No new toggle or param - purely a
      // reflection of change 3's published state.
      {
         const bool noteConnected = n->noteInput.IsConnected();
         char status[32];
         if (noteConnected)
            snprintf(status, sizeof(status), "notes \xc2\xb7 %d", n->ActiveNoteCount());
         else if (n->IsAuditioning())
            snprintf(status, sizeof(status), "auditioning");
         else
            snprintf(status, sizeof(status), "%s",
                     Transport::Instance().IsPlaying() ? "auto \xc2\xb7 running" : "auto \xc2\xb7 press space");

         const ImVec2 textSize = ImGui::CalcTextSize(status);
         const ImVec2 rowPos = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - textSize.x, rowPos.y));
         ImGui::TextColored(tok::V4(tok::palf::v_600_630_720_1000), "%s", status);
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawSamplerWaveform(n, 140.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // finetune/pitch and speed/volume pair up at half-width each, the
      // same two-per-row rhythm every other slider body in this file uses.
      // start/end duplicate the two waveform handles as exact numeric
      // fields - the picture is for "roughly where", these are for "exactly
      // where", and each stays in sync with the other since both write the
      // same n->start/n->end.
      AudioSlider("finetune", &n->finetune, -50.0f, 50.0f, "%.0f c", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      AudioSlider("speed", &n->speed, -2.0f, 2.0f, "%.2fx", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->start = std::min(n->start, n->end - 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->end = std::max(n->end, n->start + 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }
      if (AudioSlider("position", &n->position, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->position = std::clamp(n->position, n->start, n->end);
      ImGui::SameLine();
      AudioSlider("decay", &n->decay, 0.05f, 10.0f, "%.2f s", AudioHalfWidth(), LogTaper::PosToValue, LogTaper::ValueToPos);

      // fade in / fade out close the grid as a pair. They are ms lengths
      // applied at the start and end of every pass through the range (loop
      // lap, ping-pong leg, one-shot), so they are always live - no greying.
      // Draw order is the pin numbering: they take the ordinals the old
      // single "loop xfade" row used (8) and the next (9).
      AudioSlider("fade in", &n->fadeIn, 0.0f, 250.0f, "%.0f ms", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("fade out", &n->fadeOut, 0.0f, 250.0f, "%.0f ms", AudioHalfWidth());

      EndAudioBody();
   }



   // Custom node body for the Slicer.
   //
   // Draw order below IS the modulation-pin numbering (ModSlider and
   // RegisterDiscreteParam both take paramIndex = gParamCounter++), so new
   // params are appended at the end forever - reordering silently rewires
   // every saved patch's modulation cables.
   //
   // Row order (all two-column, each cell exactly AudioHalfWidth()):
   //    1: slice by (dropdown) | onsets
   //    2: division (dropdown) | sensitivity
   //    3: pitch               | finetune
   //    4: attack              | decay        <- the envelope pair, adjacent
   //    5: speed               | volume
   //    6: crossthrough (cbox) | (deliberately empty)
   //
   // Pillars: P1 every control sits on the two-column grid (the button strip
   // is the one deliberate full-width row); P2 the crossthrough checkbox uses
   // ModCheckbox, which passes ImGui::GetFrameHeight() to
   // DrawDiscreteParamPin and so centres its modulation dot with no per-node
   // offset code (same idiom as DrawWavetableBody's engine on/off); P3 both
   // selectors and the checkbox occupy the left column of their rows, so the
   // left edge reads as one non-slider column; P5 the mode swap greys the
   // inactive control instead of removing its cell, so no cell ever moves;
   // P6 eleven controls is odd, so exactly one cell must be spare - it is the
   // far-right of the last row, P6's sanctioned position, rather than the
   // ragged full-width `volume` row this used to end on; P10 the dropdowns
   // and the checkbox go through the shared AudioBareDropdown /
   // PushCheckboxStyle theming, never a hand-rolled colour; P11 everything
   // numeric goes to the readout strip. P4 does not apply - a Synths node
   // has no `mix`.
   //
   // Discrete params (the two dropdowns and the checkbox) are allocated from
   // kDiscreteParamBase and keyed by label hash, NOT from gParamCounter, so
   // adding the checkbox shifts no float pin ordinal.
   void DrawSlicerBody(GraphNode& gn, SlicerNode* n)
   {
      static const std::vector<std::string> kSliceByNames = { "onsets", "grid" };
      static const std::vector<std::string> kDivisionNames = [] {
         std::vector<std::string> v;
         for (int i = 0; i < kSlicerNumDivisions; i++)
            v.push_back(kSlicerDivisionNames[i]);
         return v;
      }();

      const int sliceCount = n->SliceCount();
      char stat[192];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %d slice%s  -  %s", n->FileName().c_str(), sliceCount,
                  sliceCount == 1 ? "" : "s", n->sliceBy == 0 ? "onsets" : "grid");
      else
         snprintf(stat, sizeof(stat), "%s  -  %s", n->Status().c_str(),
                  n->sliceBy == 0 ? "onsets" : "grid");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Button strip - the one deliberate full-width row (P1's exception).
      if (ImGui::Button("Load...", ImVec2(90, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(recording ? "Stop" : "Record", ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (recording)
            n->StopRecording();
         else
            n->StartRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      const bool playing = n->IsPlaying();
      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(playing ? "Stop" : "Audition", ImVec2(90, 0)))
      {
         if (playing)
            n->StopPreview();
         else
            n->TriggerSlicePreview(-1);
      }
      if (playing)
         ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
         SetAudioReadout("#", "plays the whole sample on this node's own voice");

      ImGui::SameLine();
      ImGui::BeginDisabled(n->FileName().empty() || n->sliceBy != 0);
      if (ImGui::Button("re-slice", ImVec2(90, 0)))
      {
         PushUndoCheckpoint();
         n->ReSlice();
      }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered())
         SetAudioReadout("#", "re-runs transient detection at the current sensitivity");

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawSlicerWaveform(n, 140.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      const bool onsetMode = (n->sliceBy == 0);
      const float halfW = AudioHalfWidth();
      const float gap = ImGui::GetStyle().ItemSpacing.x;

      // Row 1: slice by | onsets. Row 2: division | sensitivity.
      // Both selectors sit in the left column of consecutive rows (P3), and
      // the two mode-specific controls keep their own permanent cells so
      // switching modes never moves the grid (P5).
      {
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("slice by##slicerSliceBy", kSliceByNames, n->sliceBy,
                           [n](int i) { PushUndoCheckpoint(); n->sliceBy = i; }, halfW);
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(halfW, ImGui::GetFrameHeight()));
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!onsetMode);
      if (AudioSliderInt("onsets", &n->onsets, 1, SlicerNode::kMaxSlices, halfW))
         n->onsets = std::clamp(n->onsets, 1, SlicerNode::kMaxSlices);
      ImGui::EndDisabled();

      {
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::BeginDisabled(onsetMode);
         AudioBareDropdown("division##slicerDivision", kDivisionNames, n->division,
                           [n](int i) { PushUndoCheckpoint(); n->division = i; }, halfW);
         ImGui::EndDisabled();
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(halfW, ImGui::GetFrameHeight()));
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!onsetMode);
      AudioSlider("sensitivity", &n->sensitivity, 0.0f, 100.0f, "%.0f %%", halfW);
      ImGui::EndDisabled();

      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", halfW);
      ImGui::SameLine();
      AudioSlider("finetune", &n->finetune, -100.0f, 100.0f, "%.0f c", halfW);

      // attack extends the hidden 2 ms de-click ramp rather than adding a
      // second envelope, so 0 is exactly the old (instant) behaviour. The
      // skew puts 100 ms at 12 o'clock over the 0..500 throw.
      AudioSlider("attack", &n->attack, 0.0f, 500.0f, "%.1f ms", halfW,
                  SkewAttack100Taper::PosToValue, SkewAttack100Taper::ValueToPos);
      ImGui::SameLine();
      // decay owns the ENVELOPE only. The top of its throw is a no-decay
      // detent - the slice holds at full level after its attack. Where it
      // stops is `crossthrough`'s business, not decay's.
      const char* decayFmt = (n->decay >= SlicerNode::kDecayInfinite) ? "hold" : "%.0f ms";
      AudioSlider("decay", &n->decay, 5.0f, 5000.0f, decayFmt, halfW, LogTaper::PosToValue,
                  LogTaper::ValueToPos);

      // speed is exponential (linear in log2) so 1.0x sits at the middle of
      // the 0.25..4 throw instead of a quarter of the way along it.
      AudioSlider("speed", &n->speed, 0.25f, 4.0f, "%.2fx", halfW, LogTaper::PosToValue,
                  LogTaper::ValueToPos);
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 1.0f, "%.2f", halfW);

      // Row 6, left cell only: the right cell is the one deliberate spare
      // (P6). The `bool tmp` dance is required - ModCheckbox reports a
      // modulator-driven flip only through its return value.
      {
         const float y = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, y));
         bool cross = n->crossthrough;
         bool crossUserChanged = false;
         if (ModCheckbox("crossthrough##slicerCrossthrough", &cross, &crossUserChanged))
         {
            if (crossUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            n->crossthrough = cross;
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout("crossthrough",
                            n->crossthrough ? "slices run past their own boundary"
                                            : "each slice stops at the next onset");
      }
      (void)gap;

      EndAudioBody();
   }


   // Custom node body for PaulStretch
   void DrawPaulStretchBody(GraphNode& gn, PaulStretchNode* n)
   {
      char stat[160];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...", ImVec2(90, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(recording ? "Stop" : "Record", ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (recording)
            n->StopRecording();
         else
            n->StartRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      const bool playing = n->IsPlaying();
      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(playing ? "Stop" : "Audition", ImVec2(90, 0)))
      {
         if (playing)
            n->StopPreview();
         else
            n->TriggerPreview(n->start);
      }
      if (playing)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      const float toggleW = 44.0f;
      const float modsW = toggleW;
      const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - modsW, rowScreenPos.y));
      bool loopBool = n->loop;
      if (AudioToggleButton("loop##paulstretchLoop", &loopBool))
      {
         PushUndoCheckpoint();
         n->loop = loopBool;
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawPaulStretchWaveform(n, 130.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      AudioSlider("stretch", &n->stretch, 1.0f, 1000.0f, "%.1fx", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("phase rand", &n->phaseRand, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      static const std::vector<std::string> sWinSizeNames = {
         "2048 (46ms)", "4096 (93ms)", "8192 (186ms)", "16384 (372ms)",
         "32768 (743ms)", "65536 (1.49s)", "131072 (2.97s)"
      };
      AudioBareDropdown("paulWinSize", sWinSizeNames, n->windowSizeIndex,
                        [n](int idx) {
                           PushUndoCheckpoint();
                           n->windowSizeIndex = idx;
                        }, AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      AudioSlider("pitch", &n->pitchShift, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("finetune", &n->fineTune, -100.0f, 100.0f, "%.0f c", AudioHalfWidth());

      AudioSlider("freq shift", &n->freqShift, -1000.0f, 1000.0f, "%.0f Hz", AudioHalfWidth());
      ImGui::SameLine();
      float unisonF = (float)n->unison;
      if (AudioSlider("unison", &unisonF, 1.0f, 8.0f, "%.0f v", AudioHalfWidth()))
         n->unison = std::clamp((int)(unisonF + 0.5f), 1, 8);

      AudioSlider("detune", &n->detune, 0.0f, 100.0f, "%.1f c", AudioHalfWidth());
      ImGui::SameLine();
      if (AudioSlider("position", &n->position, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->position = std::clamp(n->position, std::min(n->start, n->end), std::max(n->start, n->end));
         n->Seek(n->position);
      }

      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->start = std::min(n->start, n->end - 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->end = std::max(n->end, n->start + 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }

      EndAudioBody();
   }


   // Decimated waveform + partial-spectrum strip for Molder, cached on the
   // node (rebuilt only when a new render/analysis lands - see
   // MolderNode::RebuildWaveformCache/RebuildPartialCache), never
   // recomputed per frame.
   void DrawMolderWaveform(MolderNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;

      // Body click-catcher first (see DrawSamplerWaveform's comment on
      // overlap ordering) - the two range-handle grab-zones are added after,
      // at the same screen position, and only receive input because this
      // opts in via SetNextItemAllowOverlap() before being submitted.
      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##molderwavebody", ImVec2(w, h));
      if (hasSample && ImGui::IsItemActivated())
      {
         const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, 1.0f);
         const float target = (frac < n->start || frac > n->end) ? n->start : frac;
         n->TriggerPreview(target);
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (hasSample)
      {
         const int count = n->waveformCacheCount;
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - n->waveformMax[i] * h * 0.45f;
            const float bottom = midY - n->waveformMin[i] * h * 0.45f;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::U32(tok::pal::c_2864E6D2) : tok::U32(tok::pal::c_A5B4FFD2));
         }

         // Dim whatever the start/end range excludes.
         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);
         const ImU32 dimCol = isLight ? tok::U32(tok::pal::c_FFFFFF8C) : tok::U32(tok::pal::c_00000082);
         if (startX > origin.x)
            dl->AddRectFilled(origin, ImVec2(startX, br.y), dimCol);
         if (endX < br.x)
            dl->AddRectFilled(ImVec2(endX, origin.y), br, dimCol);

         const float px = origin.x + w * std::clamp(n->Playhead(), 0.0f, 1.0f);
         dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y),
                     isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFC85AE6), 2.0f);

         dl->AddLine(ImVec2(startX, origin.y), ImVec2(startX, br.y),
                     isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96EB), 2.0f);
         dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, br.y),
                     isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896EB), 2.0f);
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(),
                     n->IsAnalyzing() ? "analyzing..." : "no sample loaded");
      }
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample)
      {
         const float handleW = 10.0f;
         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);

         const float grip = 8.0f;
         const float startGripX = std::clamp(startX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const float endGripX = std::clamp(endX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const ImU32 startCol = isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96FF);
         const ImU32 endCol = isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896FF);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, origin.y), ImVec2(startGripX + grip * 0.5f, origin.y), ImVec2(startGripX, origin.y + grip), startCol);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, br.y), ImVec2(startGripX + grip * 0.5f, br.y), ImVec2(startGripX, br.y - grip), startCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, origin.y), ImVec2(endGripX + grip * 0.5f, origin.y), ImVec2(endGripX, origin.y + grip), endCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, br.y), ImVec2(endGripX + grip * 0.5f, br.y), ImVec2(endGripX, br.y - grip), endCol);

         const float startBtnX = std::clamp(startX - handleW * 0.5f, origin.x, br.x - handleW);
         const float endBtnX = std::clamp(endX - handleW * 0.5f, origin.x, br.x - handleW);

         ImGui::SetCursorScreenPos(ImVec2(startBtnX, origin.y));
         ImGui::InvisibleButton("##molderstarthandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
            n->start = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, n->end - 0.01f);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

         ImGui::SetCursorScreenPos(ImVec2(endBtnX, origin.y));
         ImGui::InvisibleButton("##molderendhandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
            n->end = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, n->start + 0.01f, 1.0f);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawMolderPartialBars(MolderNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();

      dl->AddRectFilled(origin, br, ScopeBgCol(), 3.0f);
      dl->PushClipRect(origin, br, true);

      const int count = n->partialBarCount;
      if (count > 0)
      {
         float maxAmp = 1e-6f;
         for (int i = 0; i < count; i++)
            maxAmp = std::max(maxAmp, n->partialBars[i]);
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count - 1.0f);
            const float frac = std::clamp(n->partialBars[i] / maxAmp, 0.0f, 1.0f);
            const float top = br.y - frac * h;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, br.y),
                              isLight ? tok::U32(tok::pal::c_5A46C8C8) : tok::U32(tok::pal::c_B4A0FFC8));
         }
      }
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      // Draws into the draw list only, above - without this the cursor never
      // advances past `origin`, so everything drawn after this call (the
      // Dummy spacer and the seed/gen/f0/harm readout) renders on top of it
      // instead of below it.
      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawMolderBody(GraphNode& gn, MolderNode* n)
   {
      char stat[192];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...", ImVec2(80, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(recording ? "Stop##molderRec" : "Record##molderRec", ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (recording)
            n->StopRecording();
         else
            n->StartRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      if (ImGui::Button("Roll", ImVec2(60, 0)))
      {
         PushUndoCheckpoint();
         n->Roll();
      }
      ImGui::SameLine();
      if (ImGui::Button("Iterate", ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         n->Iterate();
      }
      ImGui::SameLine();
      if (ImGui::Button("Reset##molder", ImVec2(60, 0)))
      {
         PushUndoCheckpoint();
         n->Reset();
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      DrawMolderWaveform(n, 90.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
      DrawMolderPartialBars(n, 36.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // loop/reverse/ping-pong right-aligned on their own row, anchored off
      // the fixed declared content width rather than GetContentRegionAvail()
      // - see DrawSamplerBody's comment on why (it overlaps the node-editor
      // resize hit-zone otherwise).
      {
         const float toggleW = 44.0f;
         const float modsW = toggleW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
         const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - modsW, rowScreenPos.y));
         bool loopBool = n->loop;
         if (AudioToggleButton("loop##molderLoop", &loopBool))
         {
            PushUndoCheckpoint();
            n->loop = loopBool;
         }
         ImGui::SameLine();
         bool reverseBool = n->reverse;
         if (AudioToggleButton("rev##molderReverse", &reverseBool))
         {
            PushUndoCheckpoint();
            n->reverse = reverseBool;
         }
         ImGui::SameLine();
         bool pingpongBool = n->pingpong;
         if (AudioToggleButton("p-p##molderPingpong", &pingpongBool))
         {
            PushUndoCheckpoint();
            n->pingpong = pingpongBool;
         }
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioSlider("chaos", &n->chaos, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());

      AudioSlider("tone", &n->tone, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("stretch", &n->stretch, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      AudioSlider("air", &n->air, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("snap", &n->snap, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      AudioSlider("time", &n->time, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("level", &n->level, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->start = std::min(n->start, n->end - 0.01f);
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->end = std::max(n->end, n->start + 0.01f);

      EndAudioBody();
   }


   // ----------------------------------------------------------- Grain Molder
   void DrawGrainMolderWaveform(GrainMolderNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;

      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##grainmolderwavebody", ImVec2(w, h));
      if (hasSample && (ImGui::IsItemActivated() || ImGui::IsItemActive()))
      {
         const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, 1.0f);
         const float target = std::clamp(frac, n->start, n->end);
         n->position = target;
         if (ImGui::IsItemActivated())
            n->TriggerPreview(target);
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (hasSample)
      {
         const int count = n->waveformCacheCount;
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - n->waveformMax[i] * h * 0.45f;
            const float bottom = midY - n->waveformMin[i] * h * 0.45f;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::U32(tok::pal::c_32A0BED2) : tok::U32(tok::pal::c_6ED2F0D2));
         }

         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);
         const ImU32 dimCol = isLight ? tok::U32(tok::pal::c_FFFFFF8C) : tok::U32(tok::pal::c_00000082);
         if (startX > origin.x)
            dl->AddRectFilled(origin, ImVec2(startX, br.y), dimCol);
         if (endX < br.x)
            dl->AddRectFilled(ImVec2(endX, origin.y), br, dimCol);

         // Primary yellow playhead: stays between start and end musically and UI-wise
         const auto& snap = n->VisualSnapshot();
         const float posClamped = std::clamp(n->position, n->start, n->end);
         const float activeFrac = (snap.selfActive && snap.selfPos >= 0.0f) ? snap.selfPos : posClamped;
         const float posX = origin.x + w * std::clamp(activeFrac, 0.0f, 1.0f);
         const ImU32 yellowCol = isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFC85AF0);
         dl->AddLine(ImVec2(posX, origin.y), ImVec2(posX, br.y), yellowCol, 2.0f);

         // Polyphonic white playheads moving at different speeds according to incoming pitch
         for (int v = 0; v < snap.count; v++)
         {
            const auto& voice = snap.voices[v];
            if (voice.amp < 0.002f)
               continue;
            const float px = origin.x + w * std::clamp(voice.position, 0.0f, 1.0f);
            const int alpha = (int)(voice.amp * 255.0f);
            const ImU32 whiteCol = isLight ? IM_COL32(40, 45, 55, alpha) : IM_COL32(255, 255, 255, alpha);
            dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y), whiteCol, 1.5f);
         }

         dl->AddLine(ImVec2(startX, origin.y), ImVec2(startX, br.y),
                     isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96EB), 2.0f);
         dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, br.y),
                     isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896EB), 2.0f);
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(),
                     n->IsRendering() ? "molding..." : "no sample loaded");
      }
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample)
      {
         const float handleW = 10.0f;
         const float startX = origin.x + w * std::clamp(n->start, 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->end, 0.0f, 1.0f);

         const float grip = 8.0f;
         const float startGripX = std::clamp(startX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const float endGripX = std::clamp(endX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const ImU32 startCol = isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96FF);
         const ImU32 endCol = isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896FF);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, origin.y), ImVec2(startGripX + grip * 0.5f, origin.y), ImVec2(startGripX, origin.y + grip), startCol);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, br.y), ImVec2(startGripX + grip * 0.5f, br.y), ImVec2(startGripX, br.y - grip), startCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, origin.y), ImVec2(endGripX + grip * 0.5f, origin.y), ImVec2(endGripX, origin.y + grip), endCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, br.y), ImVec2(endGripX + grip * 0.5f, br.y), ImVec2(endGripX, br.y - grip), endCol);

         const float startBtnX = std::clamp(startX - handleW * 0.5f, origin.x, br.x - handleW);
         const float endBtnX = std::clamp(endX - handleW * 0.5f, origin.x, br.x - handleW);

         ImGui::SetCursorScreenPos(ImVec2(startBtnX, origin.y));
         ImGui::InvisibleButton("##gmstarthandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
            n->start = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, n->end - 0.01f);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

         ImGui::SetCursorScreenPos(ImVec2(endBtnX, origin.y));
         ImGui::InvisibleButton("##gmendhandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
            n->end = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, n->start + 0.01f, 1.0f);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawGrainMolderBody(GraphNode& gn, GrainMolderNode* n)
   {
      char stat[192];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...##grainMolder", ImVec2(80, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(recording ? "Stop##gmRec" : "Record##gmRec", ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (recording)
            n->StopRecording();
         else
            n->StartRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      const bool playing = n->IsPlaying();
      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(playing ? "Stop##gmAud" : "Audition##gmAud", ImVec2(80, 0)))
      {
         if (playing)
            n->StopPreview();
         else
            n->TriggerPreview(n->start);
      }
      if (playing)
         ImGui::PopStyleColor();

      // loop/rev/p-p toggles right-aligned on header row
      ImGui::SameLine();
      const float toggleW = 44.0f;
      const float modsW = toggleW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
      const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - modsW, rowScreenPos.y));
      bool loopBool = n->loop;
      if (AudioToggleButton("loop##gmLoop", &loopBool))
      {
         PushUndoCheckpoint();
         n->loop = loopBool;
      }
      ImGui::SameLine();
      bool reverseBool = n->reverse;
      if (AudioToggleButton("rev##gmReverse", &reverseBool))
      {
         PushUndoCheckpoint();
         n->reverse = reverseBool;
      }
      ImGui::SameLine();
      bool pingpongBool = n->pingpong;
      if (AudioToggleButton("p-p##gmPingpong", &pingpongBool))
      {
         PushUndoCheckpoint();
         n->pingpong = pingpongBool;
      }

      // Read-only status word right-aligned
      {
         const bool noteConnected = n->noteInput.IsConnected();
         char status[32];
         if (noteConnected)
            snprintf(status, sizeof(status), "notes \xc2\xb7 %d", n->ActiveNoteCount());
         else if (n->IsAuditioning())
            snprintf(status, sizeof(status), "auditioning");
         else
            snprintf(status, sizeof(status), "%s",
                     Transport::Instance().IsPlaying() ? "auto \xc2\xb7 running" : "auto \xc2\xb7 press space");

         const ImVec2 textSize = ImGui::CalcTextSize(status);
         const ImVec2 rowPos = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - textSize.x, rowPos.y));
         ImGui::TextColored(tok::V4(tok::palf::v_600_630_720_1000), "%s", status);
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawGrainMolderWaveform(n, 130.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Row 1: grain | amount
      AudioSlider("grain", &n->grain, 10.0f, 500.0f, "%.0f ms", AudioHalfWidth(), LogTaper::PosToValue, LogTaper::ValueToPos);
      ImGui::SameLine();
      AudioSlider("amount", &n->amount, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      // Row 2: key | order (or seed if random)
      {
         const float pinIndent = 18.0f; // 14px pin + 4px spacing matching ModSlider
         const float halfTrackW = AudioHalfWidth() - pinIndent;

         ImGui::Dummy(ImVec2(14.0f, 14.0f));
         ImGui::SameLine(0.0f, 4.0f);
         static const std::vector<std::string> kKeyNames = { "Level", "Bright", "Random" };
         AudioBareDropdown("gmKey", kKeyNames, n->key, [n](int i) {
            PushUndoCheckpoint();
            n->key = i;
         }, halfTrackW);
         ImGui::SameLine();

         if (n->key == 2)
         {
            float seedFloat = (float)n->seed;
            if (AudioSlider("seed", &seedFloat, 1.0f, 9999.0f, "%.0f", AudioHalfWidth()))
            {
               PushUndoCheckpoint();
               n->seed = std::max(1, (int)seedFloat);
            }
         }
         else
         {
            ImGui::Dummy(ImVec2(14.0f, 14.0f));
            ImGui::SameLine(0.0f, 4.0f);
            static const std::vector<std::string> kOrderNames = { "Ascending", "Descending" };
            AudioBareDropdown("gmOrder", kOrderNames, n->descending ? 1 : 0, [n](int i) {
               PushUndoCheckpoint();
               n->descending = (i == 1);
            }, halfTrackW);
         }
      }

      // Row 3: start | end
      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->start = std::min(n->start, n->end - 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->end = std::max(n->end, n->start + 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }

      // Row 4: position | level
      if (AudioSlider("position", &n->position, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->position = std::clamp(n->position, n->start, n->end);
      ImGui::SameLine();
      AudioSlider("level", &n->level, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      // Row 5: pitch | decay
      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("decay", &n->decay, 0.05f, 10.0f, "%.2f s", AudioHalfWidth(), LogTaper::PosToValue, LogTaper::ValueToPos);

      EndAudioBody();
   }


   // Custom node body for Granular Synthesizer
   void DrawGranularBody(GraphNode& gn, GranularNode* n)
   {
      char stat[160];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...", ImVec2(80, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_BE3C3CFF));
      if (ImGui::Button(recording ? "Stop##granRec" : "Record##granRec", ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (recording)
            n->StopRecording();
         else
            n->StartRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      bool freezeBool = n->freeze;
      if (AudioToggleButton("freeze##granFreeze", &freezeBool))
      {
         PushUndoCheckpoint();
         n->freeze = freezeBool;
      }

      ImGui::SameLine();
      bool loopBool = n->loop;
      if (AudioToggleButton("loop##granLoop", &loopBool))
      {
         PushUndoCheckpoint();
         n->loop = loopBool;
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      DrawGranularWaveform(n, 130.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Row 1: Position & Scan
      if (AudioSlider("pos", &n->position, n->start, n->end, "%.3f", AudioHalfWidth()))
      {
         PushUndoCheckpoint();
         n->Seek(n->position);
      }
      ImGui::SameLine();
      AudioSlider("scan", &n->scan, -4.0f, 4.0f, "%.2fx", AudioHalfWidth());

      // Row 2: Grain Size & Density
      AudioSlider("size", &n->grainLength, 5.0f, 1000.0f, "%.0f ms", AudioHalfWidth(), LogTaper::PosToValue, LogTaper::ValueToPos);
      ImGui::SameLine();
      AudioSlider("density", &n->density, 1.0f, 100.0f, "%.0f Hz", AudioHalfWidth(), LogTaper::PosToValue, LogTaper::ValueToPos);

      // Row 3: Random Position (Spray) & Random Length
      AudioSlider("spray", &n->randomPos, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("rnd size", &n->randomLength, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      // Row 4: Random Pitch & Pitch Shift
      AudioSlider("pitch", &n->pitchShift, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("rnd pitch", &n->randomPitch, 0.0f, 24.0f, "%.1f st", AudioHalfWidth());

      // Row 5: Fine Tune & Reverse Probability
      AudioSlider("finetune", &n->fineTune, -100.0f, 100.0f, "%.0f c", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("reverse", &n->reverseProb, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      // Row 6: Window Envelope Shape & Pan Spread
      static const std::vector<std::string> sWinShapes = {
         "Hann", "Gaussian", "Triangle", "Exp Decay", "Trapezoid"
      };
      AudioBareDropdown("granShape", sWinShapes, n->windowShape,
                        [n](int idx) {
                           PushUndoCheckpoint();
                           n->windowShape = idx;
                        }, AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("pan spread", &n->randomPan, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      // Row 7: Master Pan & Width
      AudioSlider("pan", &n->pan, -1.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("width", &n->width, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      // Row 8: Dry/Wet Mix & Volume
      AudioSlider("mix", &n->mix, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      // Row 9: Trim Start & End
      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->start = std::min(n->start, n->end - 0.01f);
         if (n->position < n->start)
            n->Seek(n->start);
      }
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->end = std::max(n->end, n->start + 0.01f);
         if (n->position > n->end)
            n->Seek(n->end);
      }

      EndAudioBody();
   }


   // One lane's waveform view: a drag-&-drop/click-to-load target when
   // empty, or start/end range handles over the decimated waveform once a
   // sample is loaded - same overlap-ordering trick as DrawSamplerWaveform
   // (body click-catcher first via SetNextItemAllowOverlap, then the
   // handles), just reading DrumSequencerNode's per-lane waveform cache
   // instead of SamplerNode's single one.
   void DrawDrumLaneWaveform(DrumSequencerNode* n, int lane, float h)
   {
      const float w = gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->laneWaveCount[lane] > 0;

      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##drumlanewavebody", ImVec2(w, h));
      if (!hasSample && ImGui::IsItemActivated())
      {
         PushUndoCheckpoint();
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
            n->LoadFileToLane(lane, path);
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (hasSample)
      {
         const int count = n->laneWaveCount[lane];
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - n->laneWaveMax[lane][i] * h * 0.45f;
            const float bottom = midY - n->laneWaveMin[lane][i] * h * 0.45f;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::U32(tok::pal::c_1E6EE6D2) : tok::U32(tok::pal::c_96D6FFC8));
         }

         const float startX = origin.x + w * std::clamp(n->laneStart[lane], 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->laneEnd[lane], 0.0f, 1.0f);
         const ImU32 dimCol = isLight ? tok::U32(tok::pal::c_FFFFFF8C) : tok::U32(tok::pal::c_00000082);
         if (startX > origin.x)
            dl->AddRectFilled(origin, ImVec2(startX, br.y), dimCol);
         if (endX < br.x)
            dl->AddRectFilled(ImVec2(endX, origin.y), br, dimCol);

         dl->AddLine(ImVec2(startX, origin.y), ImVec2(startX, br.y),
                     isLight ? tok::U32(tok::pal::c_14A03CFF) : tok::U32(tok::pal::c_78DC96EB), 2.0f);
         dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, br.y),
                     isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_DC7896EB), 2.0f);
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "drop sample / click");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample)
      {
         const float handleW = 10.0f;
         const float startX = origin.x + w * std::clamp(n->laneStart[lane], 0.0f, 1.0f);
         const float endX = origin.x + w * std::clamp(n->laneEnd[lane], 0.0f, 1.0f);
         const float grip = 8.0f;
         const float startGripX = std::clamp(startX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const float endGripX = std::clamp(endX, origin.x + grip * 0.5f, br.x - grip * 0.5f);
         const ImU32 startCol = tok::U32(tok::pal::c_78DC96FF);
         const ImU32 endCol = tok::U32(tok::pal::c_DC7896FF);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, origin.y), ImVec2(startGripX + grip * 0.5f, origin.y),
                               ImVec2(startGripX, origin.y + grip), startCol);
         dl->AddTriangleFilled(ImVec2(startGripX - grip * 0.5f, br.y), ImVec2(startGripX + grip * 0.5f, br.y),
                               ImVec2(startGripX, br.y - grip), startCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, origin.y), ImVec2(endGripX + grip * 0.5f, origin.y),
                               ImVec2(endGripX, origin.y + grip), endCol);
         dl->AddTriangleFilled(ImVec2(endGripX - grip * 0.5f, br.y), ImVec2(endGripX + grip * 0.5f, br.y),
                               ImVec2(endGripX, br.y - grip), endCol);

         const float startBtnX = std::clamp(startX - handleW * 0.5f, origin.x, br.x - handleW);
         const float endBtnX = std::clamp(endX - handleW * 0.5f, origin.x, br.x - handleW);

         ImGui::SetCursorScreenPos(ImVec2(startBtnX, origin.y));
         ImGui::InvisibleButton("##drumlanestarthandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
            n->laneStart[lane] = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, n->laneEnd[lane] - 0.01f);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

         ImGui::SetCursorScreenPos(ImVec2(endBtnX, origin.y));
         ImGui::InvisibleButton("##drumlaneendhandle", ImVec2(handleW, h));
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (ImGui::IsItemActive())
            n->laneEnd[lane] = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, n->laneStart[lane] + 0.01f, 1.0f);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }


   // One lane's card: waveform + clear/choke row + the five per-lane knobs.
   // Called once per lane inside each of the two BeginAudioColumn scopes in
   // DrawDrumSequencerBody, so gAudioBodyW/gAudioContentW already read as
   // the column's width - the section panel and knob row size themselves
   // from that with no extra plumbing (the "width is a scope" rule).
   void DrawDrumLaneCard(DrumSequencerNode* n, int lane)
   {
      ImGui::PushID(lane);
      // Cached for drag-drop lane resolution, covering the *whole* card
      // (header, buttons, waveform, sliders) rather than just the waveform
      // strip - a drop anywhere on the card should land on this lane, not
      // only inside the narrow black waveform box. GetCursorScreenPos()
      // here is canvas space, not real screen space - see the fields'
      // comment on DrumSequencerNode and DrumSequencerLaneForCanvasPos.
      const ImVec2 cardTop = ImGui::GetCursorScreenPos();
      n->laneCardCanvasX0[lane] = cardTop.x;
      n->laneCardCanvasY0[lane] = cardTop.y;
      n->laneCardCanvasX1[lane] = cardTop.x + gAudioContentW;
      char header[48];
      const std::string& fn = n->FileName(lane);
      if (fn.empty())
         snprintf(header, sizeof(header), "lane %d - %s", lane + 1, DrumPatterns::LaneRole(lane)); // role the groove library gives this lane
      else
      {
         std::string trimmed = fn.size() > 20 ? fn.substr(0, 19) + "." : fn;
         snprintf(header, sizeof(header), "lane %d - %s", lane + 1, trimmed.c_str());
      }
      BeginAudioSection(header);

      {
         const float btnW = 20.0f;
         const float rowY = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - btnW, rowY));
         const bool clearClicked = ImGui::Button("##clearlane", ImVec2(btnW, 0));
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.65f;
            const ImU32 col = ImGui::IsItemHovered() ? tok::U32(tok::pal::c_E63C3CFF) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            glyph::DrawX(dl, center, iconSize, col);
         }
         if (clearClicked)
         {
            PushUndoCheckpoint();
            n->ClearLane(lane);
         }
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, rowY));
         char chokeLabel[8];
         snprintf(chokeLabel, sizeof(chokeLabel), n->laneChoke[lane] == 0 ? "choke -" : "choke %d", n->laneChoke[lane]);
         if (ImGui::Button(chokeLabel, ImVec2(70.0f, 0)))
         {
            PushUndoCheckpoint();
            n->laneChoke[lane] = (n->laneChoke[lane] + 1) % 4;
         }
         ImGui::Dummy(ImVec2(gAudioContentW, ImGui::GetFrameHeight()));
      }

      DrawDrumLaneWaveform(n, lane, 84.0f);
      ImGui::Dummy(ImVec2(0.0f, 3.0f));

      // Horizontal sliders, two per row, rather than a knob row - each is
      // shorter than a knob-plus-caption, and the height that frees up goes
      // to the waveform above instead (same two-per-row rhythm Sampler's
      // body uses). pitch pairs with fine tune (mirrors Sampler's own
      // pitch/finetune pair) rather than leaving pan alone on an odd row.
      {
         const float half = AudioHalfWidth();
         AudioSlider("transient", &n->laneTransient[lane], -1.0f, 1.0f, "%.2f", half);
         ImGui::SameLine();
         AudioSlider("decay", &n->laneDecay[lane], 0.0f, 1.0f, "%.2f", half);
         AudioSlider("pitch", &n->lanePitch[lane], -24.0f, 24.0f, "%.1f st", half);
         ImGui::SameLine();
         AudioSlider("fine tune", &n->laneFineTune[lane], -50.0f, 50.0f, "%.0f c", half);
         AudioSlider("volume", &n->laneVolume[lane], 0.0f, 1.0f, "%.2f", half);
         ImGui::SameLine();
         AudioSlider("pan", &n->lanePan[lane], -1.0f, 1.0f, "%.2f", half);
         // Fourth row, full width: semitones added on full-velocity steps
         // only (two-tone bells). Explicit param index 300 + lane keeps it out
         // of the gParamCounter sequence, so the six sliders above (and every
         // saved modulation binding on them) keep their ordinals; 300..307 sit
         // between the float ordinals (< ~60) and kDiscreteParamBase (400).
         char accentName[24];
         snprintf(accentName, sizeof(accentName), "lane %d accent", lane + 1);
         AudioSlider("accent", &n->laneAccentPitch[lane], -24.0f, 24.0f, "%+.1f st", AudioFullWidth(), nullptr,
                     nullptr, 300 + lane, accentName);
      }

      EndAudioSection();
      n->laneCardCanvasY1[lane] = ImGui::GetCursorScreenPos().y;
      ImGui::PopID();
   }


   // ---- groove picker (library in nodes/DrumPatterns.h) -------------------
   // All 141 grooves as one flat list with a parallel category column, so the
   // existing dropdown popup shows category headers and its filter box.
   struct DrumGrooveLists
   {
      std::vector<std::string> names;
      std::vector<std::string> cats; // per groove, for gDropdown.categories
      std::vector<std::string> catNames;
      std::vector<int> catFirst; // first groove index of each category
      int count = 0;
      DrumGrooveLists()
      {
         const DrumPatterns::Groove* g = DrumPatterns::All(count);
         int nc = 0;
         const char* const* cn = DrumPatterns::Categories(nc);
         for (int c = 0; c < nc; c++)
         {
            catNames.push_back(cn[c]);
            catFirst.push_back(-1);
         }
         for (int i = 0; i < count; i++)
         {
            names.push_back(g[i].name);
            cats.push_back(g[i].category);
            for (int c = 0; c < nc; c++)
               if (catNames[(size_t)c] == g[i].category && catFirst[(size_t)c] < 0)
                  catFirst[(size_t)c] = i;
         }
      }
      int IndexOfName(const std::string& name) const
      {
         for (int i = 0; i < count; i++)
            if (names[(size_t)i] == name)
               return i;
         return -1;
      }
      int IndexOfCategory(const std::string& cat) const
      {
         for (size_t c = 0; c < catNames.size(); c++)
            if (catNames[c] == cat)
               return (int)c;
         return -1;
      }
   };

   const DrumGrooveLists& DrumGrooves()
   {
      static const DrumGrooveLists lists;
      return lists;
   }


   // One undo step for the whole pick (pattern, rate, swing, steps, accents,
   // kit fill). Inside a dropdown pick the checkpoint is already taken and this
   // one is suppressed, so it never doubles.
   void ApplyDrumGroove(DrumSequencerNode* n, int grooveIdx, int part)
   {
      int count = 0;
      const DrumPatterns::Groove* all = DrumPatterns::All(count);
      if (grooveIdx < 0 || grooveIdx >= count)
         return;
      PushUndoCheckpoint();
      n->ApplyPattern(all[grooveIdx], std::clamp(part, 0, 2));
   }


   // Picker dropdown. Deliberately NOT AudioBareDropdown: that registers the
   // button as a modulatable enum param, and a cable on it would re-apply a
   // whole groove (and push an undo step) on every change of the driven value.
   // This opens the shared popup directly and never registers a param.
   void DrumPickerDropdown(const char* id, const char* caption, const std::vector<std::string>& options,
                           const std::vector<std::string>& categories, int current, float width,
                           std::function<void(int)> onSelect, bool focusSearch)
   {
      const std::string label = std::string(caption) + "##" + id;
      if (ImGui::Button(label.c_str(), ImVec2(width, 0)))
      {
         gDropdown.options = options;
         gDropdown.categories = categories;
         gDropdown.onSelect = std::move(onSelect);
         gDropdown.current = current;
         gDropdown.justOpened = true;
         gDropdown.focusSearch = focusSearch;
         gDropdown.filterBuf[0] = '\0';
      }
   }


   // Compact segmented pill: equal cells, 1px apart, frame-height tall (the same
   // height as every other strip button). Cell width fits the widest label with
   // the bundled font at the current UI scale, never below minW. Returns the
   // clicked cell or -1. markCell draws the playhead dot on that cell.
   float DrumPillCellW(const char* const* labels, int count, float minW)
   {
      float w = minW;
      for (int i = 0; i < count; i++)
         w = std::max(w, ImGui::CalcTextSize(labels[i]).x + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f);
      return std::ceil(w);
   }


   int DrumSegmentedPill(const char* id, const char* const* labels, int count, int selected, float cellW, int markCell = -1)
   {
      int clicked = -1;
      for (int i = 0; i < count; i++)
      {
         if (i > 0)
            ImGui::SameLine(0.0f, 1.0f);
         bool on = (selected == i);
         char lbl[48];
         snprintf(lbl, sizeof(lbl), "%s##%s%d", labels[i], id, i);
         if (AudioToggleButton(lbl, &on, cellW))
            clicked = i;
         if (i == markCell)
         {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(bmax.x - 5.0f, bmin.y + 5.0f), 2.5f,
                                                        tok::U32(tok::pal::c_FFC864FF));
         }
      }
      return clicked;
   }


   void DrawDrumSequencerBody(GraphNode& gn, DrumSequencerNode* n)
   {
      const DrumGrooveLists& gl = DrumGrooves();
      const int grooveIdx = gl.IndexOfName(n->patternName);
      char grooveLabel[96];
      if (grooveIdx >= 0)
         snprintf(grooveLabel, sizeof(grooveLabel), "%s - %c", n->patternName.c_str(), 'A' + std::clamp(n->patternPart, 0, 2));
      else
         snprintf(grooveLabel, sizeof(grooveLabel), "custom pattern");
      char stat[160];
      if (n->run)
         snprintf(stat, sizeof(stat), "%s - %d steps - %s - %d loaded", grooveLabel, std::clamp(n->numSteps, 1, DrumSequencerNode::kMaxSteps),
                  MusicTime::RateDivisionName(n->rate), n->LoadedLaneCount());
      else
         snprintf(stat, sizeof(stat), "stopped - %s - %d steps - %s", grooveLabel, std::clamp(n->numSteps, 1, DrumSequencerNode::kMaxSteps),
                  MusicTime::RateDivisionName(n->rate));

      BeginAudioBody(gn.index, gn.category, kAudioWideWidth, stat);

      // ---- lane cards: 4 rows x 2 columns, left column lanes 1-4, right
      // column lanes 5-8, matching the mockup. BeginAudioColumns is the
      // §1k mechanism for a node wider than kAudioNodeWidth - unlike
      // Wavetable's A/B engines, both columns here are the *same* kind of
      // content (four stacked lane cards each), not two parallel modes.
      {
         BeginAudioColumns(2);
         BeginAudioColumn(0);
         for (int lane = 0; lane < 4; lane++)
         {
            DrawDrumLaneCard(n, lane);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
         }
         EndAudioColumn();
         BeginAudioColumn(1);
         for (int lane = 4; lane < DrumSequencerNode::kNumLanes; lane++)
         {
            DrawDrumLaneCard(n, lane);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
         }
         EndAudioColumn();
         EndAudioColumns();
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // ---- groove picker: ONE row. category | groove | < > | A B C pill.
      // The dropdowns share the remaining width, the nav buttons and the part
      // pill cells share one cell width, so the row spans the node edge to
      // edge. Applying a groove replaces the pattern, rate, swing and step
      // count and fills empty lanes from the bundled kit (one undo step).
      {
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         static const char* const kParts[3] = { "A", "B", "C" };
         const float pcw = DrumPillCellW(kParts, 3, 24.0f);
         const float pillW = pcw * 3.0f + 2.0f;
         const float navW = pcw;
         const float dropW = AudioFullWidth() - pillW - navW * 2.0f - gap * 4.0f;
         const float catW = std::floor(dropW * 0.34f);
         const float grvW = dropW - catW;
         const int curCat = grooveIdx >= 0 ? gl.IndexOfCategory(gl.cats[(size_t)grooveIdx]) : -1;
         const int part = std::clamp(n->patternPart, 0, 2);

         DrumPickerDropdown("drumcat", curCat >= 0 ? gl.catNames[(size_t)curCat].c_str() : "category", gl.catNames, {}, curCat,
                            catW,
                            [n, part](int c) {
                               const DrumGrooveLists& l = DrumGrooves();
                               if (c >= 0 && c < (int)l.catFirst.size() && l.catFirst[(size_t)c] >= 0)
                                  ApplyDrumGroove(n, l.catFirst[(size_t)c], part);
                            },
                            /*focusSearch=*/false);
         ImGui::SameLine();
         DrumPickerDropdown("drumgroove", grooveIdx >= 0 ? gl.names[(size_t)grooveIdx].c_str() : "pick a groove", gl.names, gl.cats,
                            grooveIdx, grvW, [n, part](int i) { ApplyDrumGroove(n, i, part); }, /*focusSearch=*/true);
         ImGui::SameLine();
         const bool prev = ImGui::Button("<##drumgrvprev", ImVec2(navW, 0));
         ImGui::SameLine();
         const bool next = ImGui::Button(">##drumgrvnext", ImVec2(navW, 0));
         if ((prev || next) && gl.count > 0)
         {
            const int target = grooveIdx < 0 ? (next ? 0 : gl.count - 1)
                                             : (grooveIdx + (next ? 1 : gl.count - 1)) % gl.count;
            ApplyDrumGroove(n, target, part);
         }
         ImGui::SameLine();
         // Parts: greyed out until a groove has been picked (nothing to switch).
         if (grooveIdx < 0)
            ImGui::BeginDisabled();
         const int pc = DrumSegmentedPill("drumpart", kParts, 3, grooveIdx >= 0 ? part : -1, pcw);
         if (pc >= 0 && grooveIdx >= 0)
            ApplyDrumGroove(n, grooveIdx, pc);
         if (grooveIdx < 0)
            ImGui::EndDisabled();
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // ---- step grid: the visualizer, and the primary editable control ----
      {
         const int steps = std::clamp(n->numSteps, 1, DrumSequencerNode::kMaxSteps);
         // Paging: columns per page = min(steps, 16); cell width is fixed by
         // that so the grid doesn't change shape between pages. A short last
         // page (e.g. steps 17-20) draws only its real steps.
         const int pageSteps = DrumSequencerNode::kEditPageSteps;
         const int gridCols = std::min(steps, pageSteps);
         const int page = std::clamp(n->editPage, 0, (steps - 1) / pageSteps);
         const int pageFirst = page * pageSteps;
         const int pageCols = std::min(gridCols, steps - pageFirst);
         const float toggleW = 20.0f;
         const float gutterW = toggleW * 3.0f + 2.0f * 2.0f + 6.0f; // R | M | S (70px)
         const float rightGutterW = 32.0f; // Output pin gutter on the right
         const float rowH = 28.0f;
         const float rowGap = 2.0f;
         const float cellGap = 1.0f;
         const float stepsW = gAudioBodyW - gutterW - rightGutterW;
         const float cellW = (stepsW - cellGap * (float)(gridCols - 1)) / (float)gridCols;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         n->gridCanvasTopY = origin.y;
         n->gridCanvasRowH = rowH + rowGap;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool isLight = IsThemeLight();
         const int curStep = n->CurrentStep();
         const double beatsPerBar = Transport::Instance().BeatsPerBar();
         const double beatsPerStep = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)n->rate));
         const int stepsPerBar = std::max(1, (int)std::lround(beatsPerBar / beatsPerStep));

         for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
         {
            const float y0 = origin.y + (float)lane * (rowH + rowGap);
            ImGui::PushID(lane);

            // ---- gutter: randomise, mute, solo ------------------------
            ImGui::SetCursorScreenPos(ImVec2(origin.x, y0));
            if (AudioSmallButton("R##randlane", toggleW, rowH))
            {
               PushUndoCheckpoint();
               n->RandomizeLane(lane);
            }
            ImGui::SameLine(0.0f, 2.0f);
            bool muteBool = n->laneMute[lane];
            if (AudioMuteButton("M##mute", &muteBool, toggleW, rowH))
            {
               PushUndoCheckpoint();
               n->laneMute[lane] = muteBool;
            }
            ImGui::SameLine(0.0f, 2.0f);
            bool soloBool = n->laneSolo[lane];
            if (AudioSoloButton("S##solo", &soloBool, toggleW, rowH))
            {
               PushUndoCheckpoint();
               n->laneSolo[lane] = soloBool;
            }

            // ---- steps -----------------------------------------------------
            for (int sc = 0; sc < pageCols; sc++)
            {
               const int s = pageFirst + sc; // absolute step index
               const float x0 = origin.x + gutterW + (float)sc * (cellW + cellGap);
               ImGui::PushID(s);
               ImGui::SetCursorScreenPos(ImVec2(x0, y0));
               ImGui::InvisibleButton("cell", ImVec2(cellW, rowH));
               float& vel = n->stepVel[lane][s];

               if (ImGui::IsItemActivated())
               {
                  PushUndoCheckpoint();
                  const bool wasOn = vel > 0.0f;
                  vel = wasOn ? 0.0f : 0.8f;
                  gDrumGridDrag.active = true;
                  gDrumGridDrag.paintOn = !wasOn;
                  gDrumGridDrag.originLane = lane;
                  gDrumGridDrag.originStep = s;
               }
               const bool isOrigin = (gDrumGridDrag.originLane == lane && gDrumGridDrag.originStep == s);
               if (isOrigin && ImGui::IsItemActive() && vel > 0.0f &&
                   ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
               {
                  const float my = ImGui::GetIO().MousePos.y;
                  const float t = 1.0f - std::clamp((my - y0) / rowH, 0.0f, 1.0f);
                  vel = std::clamp(0.05f + t * 0.95f, 0.05f, 1.0f);
                  char buf[32];
                  snprintf(buf, sizeof(buf), "%.2f", vel);
                  SetAudioReadout("velocity", buf);
               }
               else if (gDrumGridDrag.active && !isOrigin && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
               {
                  vel = gDrumGridDrag.paintOn ? 0.8f : 0.0f;
               }

               const bool isBar = (s % stepsPerBar) == 0;
               // Was a hardcoded near-black cell fill regardless of theme -
               // this is the drum sequencer's own step grid (distinct from
               // PatternNode's already theme-aware DrawPatternStepGrid), and
               // was the source of the "black boxes" seen in light presets.
               // Colors mirror DrawPatternStepGrid's light-mode track-lane
               // palette (isGroupStart ? 212/218/230 : 224/228/238).
               const ImU32 frameCol = isLight
                  ? (isBar ? tok::U32(tok::pal::c_D2D7E4FF) : tok::U32(tok::pal::c_E0E4EEFF))
                  : (isBar ? tok::U32(tok::pal::c_3C404EFF) : tok::U32(tok::pal::c_282B35FF));
               dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cellW - cellGap, y0 + rowH), frameCol, 2.0f);
               if (vel > 0.0f)
               {
                  const float fillTop = y0 + (1.0f - vel) * rowH;
                  const ImU32 fillCol = (s == curStep && n->run) ? tok::U32(tok::pal::c_FFC864FF)
                                                                 : tok::U32(tok::pal::c_78C8FFDC);
                  dl->AddRectFilled(ImVec2(x0, fillTop), ImVec2(x0 + cellW - cellGap, y0 + rowH), fillCol, 2.0f);
               }
               if (s == curStep)
                  dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + cellW - cellGap, y0 + rowH), tok::U32(tok::pal::c_FFFFFF82),
                              2.0f);

               ImGui::PopID();
            }

            // ---- lane output pin: right gutter ----------------------------
            const float pinX = origin.x + gutterW + stepsW + rightGutterW * 0.5f;
            const float pinY = y0 + rowH * 0.5f;
            const int pinId = gn.OutputPinId(1 + lane);

            ed::BeginPin(pinId, ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
            const ImVec2 pinCenter(pinX, pinY);
            const ImVec2 pinMin(pinCenter.x - kPinHit * 0.5f, pinCenter.y - kPinHit * 0.5f);
            const ImVec2 pinMax(pinCenter.x + kPinHit * 0.5f, pinCenter.y + kPinHit * 0.5f);
            ed::PinRect(pinMin, pinMax);

            PinDot::Cable(dl, pinCenter, /*prediction=*/false, isLight);
            ed::EndPin();

            ImGui::PopID();
         }
         if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            gDrumGridDrag.active = false;

         ImGui::SetCursorScreenPos(
            ImVec2(origin.x, origin.y + (float)DrumSequencerNode::kNumLanes * (rowH + rowGap) + 4.0f));
         ImGui::Dummy(ImVec2(gAudioBodyW, 1.0f));

         // ---- grid stat line: left readout, compact '1-16 | 17-32' pill
         // right-aligned. Always drawn (greyed via BeginDisabled at <= 16
         // steps) so the node never changes height; same frame height as the
         // other strip buttons. The dot marks the page the playhead is on.
         {
            static const char* const kPages[2] = { "1-16", "17-32" };
            const float pgw = DrumPillCellW(kPages, 2, 24.0f);
            const float pillW = pgw * 2.0f + 1.0f;
            const bool paged = steps > pageSteps;
            const int playPage = (n->run && paged) ? curStep / pageSteps : -1;
            const float startX = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d steps - %s", steps, MusicTime::RateDivisionName(n->rate));
            ImGui::SameLine();
            ImGui::SetCursorPosX(startX + AudioFullWidth() - pillW);
            if (!paged)
               ImGui::BeginDisabled();
            const int pc = DrumSegmentedPill("drumpage", kPages, 2, page, pgw, playPage);
            if (pc >= 0)
               n->editPage = pc;
            if (!paged)
               ImGui::EndDisabled();
         }
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // ---- global controls: rate + 3 pattern knobs on top, then the four
      // offset knobs that compose on top of every lane's own value
      // (DrumSequencerNode's class comment / PushDirtyParams) below -
      // symmetric 4-and-4 so neither row reads as an afterthought tacked
      // onto the other. "output" here is the node's own post-mix level;
      // there is no second "volume" knob in the offsets row below - a
      // per-lane multiplicative volume offset duplicated what this one
      // already does, so it was folded away rather than kept as a
      // confusable second control with the same effect.
      {
         AudioKnobRow row(4);
         row.Dropdown("rate", MusicTime::RateDivisionList(), n->rate,
                      [n](int i) { PushUndoCheckpoint(); n->rate = i; });
         row.KnobInt("steps", &n->numSteps, 1, DrumSequencerNode::kMaxSteps);
         row.Knob("swing", &n->swing, 0.0f, 1.0f, "%.2f");
         row.Knob("output", &n->volume, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.Knob("transient", &n->globalTransient, -1.0f, 1.0f, "%.2f");
         row.Knob("decay", &n->globalDecay, -1.0f, 1.0f, "%.2f");
         row.Knob("pitch", &n->globalPitch, -24.0f, 24.0f, "%.1f st");
         row.Knob("pan", &n->globalPan, -1.0f, 1.0f, "%.2f");
         row.End();
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      {
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float btnW = (AudioFullWidth() - gap * 2.0f) / 3.0f;
         AudioToggleButton(n->run ? "Stop##drumrun" : "Run##drumrun", &n->run, btnW);
         ImGui::SameLine();
         if (ImGui::Button("Randomise", ImVec2(btnW, 0)))
         {
            PushUndoCheckpoint();
            n->Randomize();
         }
         ImGui::SameLine();
         if (ImGui::Button("Clear", ImVec2(btnW, 0)))
         {
            PushUndoCheckpoint();
            n->ClearPattern();
         }
      }

      EndAudioBody();
   }


   // ---- Looper -----------------------------------------------------------
   // Status line: what the looper is doing right now and what a press will do.
   // Rate drift rule: at any playback rate other than exactly 1.0x the loop
   // plays in length / |rate| and drifts against the transport; overdub pauses.
   void LooperStatusText(LooperNode* n, char* out, size_t cap)
   {
      const Transport& tr = Transport::Instance();
      const double bpm = std::max(1.0f, tr.Tempo());
      const double bar = std::max(1e-6, tr.BeatsPerBar());
      const std::vector<std::string>& divs = MusicTime::RateDivisionList();
      const char* takeName = (n->take > 0 && n->take - 1 < (int)divs.size()) ? divs[(size_t)(n->take - 1)].c_str() : "free";
      char len[48];
      const double bars = (double)n->LengthSeconds() * bpm / 60.0 / bar;
      if (std::fabs(bars - std::round(bars)) < 0.02 && bars >= 0.98)
         snprintf(len, sizeof(len), "%d bar%s", (int)std::lround(bars), std::lround(bars) == 1 ? "" : "s");
      else
         snprintf(len, sizeof(len), "%.2f bars", bars);
      const bool unity = n->AtUnity();
      char drift[64] = "";
      if (!unity)
         snprintf(drift, sizeof(drift), " - %.2fx, drifts off the grid", std::fabs(n->Rate()));
      switch (n->CurrentState())
      {
         case LooperNode::kArmed:
         {
            double grid = bar;
            if (n->take > 0)
               grid = std::min(bar, MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(n->take - 1, 0, MusicTime::kNumRateDivisions - 1)));
            const double into = std::fmod(tr.Beats(), grid);
            snprintf(out, cap, "armed - starts in %.1f beats", grid - into);
            break;
         }
         case LooperNode::kRecording:
            if (n->TargetSeconds() > 0.0f)
               snprintf(out, cap, "recording - %.1f / %.1f s (%s)", n->LengthSeconds(), n->TargetSeconds(), takeName);
            else
               snprintf(out, cap, "recording - %.1f s - Rec ends it", n->LengthSeconds());
            break;
         case LooperNode::kPlaying:
            snprintf(out, cap, "playing - %s - %.2f s - comp %.0f ms%s", len, n->LengthSeconds(), n->CompensationMs(), drift);
            break;
         case LooperNode::kOverdubbing:
            snprintf(out, cap, "overdubbing - %s - %.2f s", len, n->LengthSeconds());
            break;
         case LooperNode::kStopped:
            snprintf(out, cap, "stopped - %s - Play resumes%s", len, drift);
            break;
         default:
            if (n->take > 0)
               snprintf(out, cap, "empty - Rec records %s (%s)", takeName, n->syncStart ? "on the grid" : "now");
            else
               snprintf(out, cap, "empty - Rec starts, Rec again ends the take");
            break;
      }
      if (n->CurrentState() == LooperNode::kOverdubbing && !unity)
      {
         // Overdub only writes at the recorded rate.
         const size_t used = strlen(out);
         if (used + 1 < cap)
            snprintf(out + used, cap - used, " (paused off 1.00x)");
      }
   }


   // Loop waveform, drawn like the Sampler's (same 140 px box, same columns,
   // colours, playhead and border) with the Looper's extras: beat and bar ticks
   // when the loop is a whole number of beats, and the recorded extent against
   // the take window while a fixed take records. No start/end triangles: the
   // Looper has no start/end.
   void DrawLooperWave(LooperNode* n, float h, float w)
   {
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const int st = n->CurrentState();
      const Transport& tr = Transport::Instance();
      const double bpm = std::max(1.0f, tr.Tempo());
      const double bar = std::max(1e-6, tr.BeatsPerBar());

      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);
      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      const float len = n->LengthSeconds();
      const float tgt = n->TargetSeconds();
      float span = (st == LooperNode::kRecording && tgt > 0.0f) ? tgt : len;
      if (span <= 0.0f && n->take > 0)
      {
         const double beats = MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(n->take - 1, 0, MusicTime::kNumRateDivisions - 1));
         span = (float)(beats * 60.0 / bpm); // the take window it would record
      }
      const ImU32 recCol = isLight ? tok::U32(tok::pal::c_CD3737FF) : tok::U32(tok::pal::c_EB5A5AFF);

      if (span > 0.0f)
      {
         const double beats = (double)span * bpm / 60.0;
         if (std::fabs(beats - std::round(beats)) < std::max(0.03, beats * 0.01) && beats >= 1.0 && beats <= 128.0)
         {
            const int nb = (int)std::lround(beats);
            for (int b = 0; b <= nb; b++)
            {
               const float x = origin.x + w * (float)b / (float)nb;
               const bool isBar = std::fmod((double)b, bar) < 1e-6;
               const ImU32 tc = isLight ? IM_COL32(60, 70, 100, isBar ? 90 : 55) : IM_COL32(200, 210, 235, isBar ? 70 : 38);
               if (isBar)
                  dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), tc, 1.0f);
               else
               {
                  dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + 6.0f), tc, 1.0f);
                  dl->AddLine(ImVec2(x, br.y - 6.0f), ImVec2(x, br.y), tc, 1.0f);
               }
            }
         }
      }

      if (len > 0.0f)
      {
         const float total = std::max(span, len);
         const float colW = w / (float)LooperNode::kWaveCols;
         const ImU32 col = isLight ? tok::U32(tok::pal::c_1E6EE6D2) : tok::U32(tok::pal::c_96D6FFC8);
         for (int c = 0; c < LooperNode::kWaveCols; c++)
         {
            if (total * (float)c / (float)LooperNode::kWaveCols >= len)
               break;
            const float x = origin.x + colW * (float)c;
            const float top = midY - n->waveMax[c] * h * 0.45f;
            const float bot = midY - n->waveMin[c] * h * 0.45f;
            dl->AddRectFilled(ImVec2(x, std::min(top, midY - 0.5f)), ImVec2(x + std::max(1.0f, colW), std::max(bot, midY + 0.5f)), col);
         }
      }

      if (st == LooperNode::kPlaying || st == LooperNode::kOverdubbing || st == LooperNode::kStopped)
      {
         const float x = origin.x + w * std::clamp(n->Position01(), 0.0f, 1.0f);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y),
                     isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFC85AF0), 2.0f);
      }
      else if (st == LooperNode::kRecording)
      {
         const float f = tgt > 0.0f ? std::clamp(len / tgt, 0.0f, 1.0f) : 1.0f;
         const float x = origin.x + w * f;
         dl->AddRectFilled(origin, ImVec2(x, br.y), IM_COL32(220, 70, 70, isLight ? 26 : 34));
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), recCol, 2.0f);
      }

      if (st == LooperNode::kEmpty)
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "no loop - press Rec");
      else if (st == LooperNode::kArmed)
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "waiting for the next grid line");
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }


   void DrawLooperBody(GraphNode& gn, LooperNode* n)
   {
      char stat[128];
      LooperStatusText(n, stat, sizeof(stat));
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Rec / Play / Dub / Clear / Undo: the Sampler's button height and
      // drawing, five across the body, each with a CV gate pin. Rec is a plain
      // button that goes red as "Stop" while a take is armed or recording;
      // Play and Dub are the blue toggles; Clear and Undo are momentary. Click
      // or CV rising edge presses.
      {
         static const char* kIds[LooperNode::kNumButtons] = { "rec", "play", "dub", "clear", "undo" };
         const int st = n->CurrentState();
         const bool recActive = st == LooperNode::kRecording || st == LooperNode::kArmed;
         const bool playActive = st == LooperNode::kPlaying || st == LooperNode::kOverdubbing;
         const bool dubActive = st == LooperNode::kOverdubbing;
         const float gapX = ImGui::GetStyle().ItemSpacing.x;
         const float cellW = (gAudioContentW + gapX) / (float)LooperNode::kNumButtons;
         const float btnW = std::min(70.0f, cellW - 16.0f - gapX);
         const float x0 = gAudioContentX;
         const float y0 = ImGui::GetCursorScreenPos().y;
         const float rowH = ImGui::GetFrameHeight();
         for (int b = 0; b < LooperNode::kNumButtons; b++)
         {
            ImGui::SetCursorScreenPos(ImVec2(x0 + (float)b * cellW, y0));
            const char* label = b == LooperNode::kRec ? (recActive ? "Stop" : "Rec")
                              : b == LooperNode::kPlay ? (playActive ? "Stop" : "Play")
                              : b == LooperNode::kDub ? "Dub" : b == LooperNode::kClear ? "Clear" : "Undo";
            const int style = b == LooperNode::kRec ? (recActive ? 1 : 0)
                            : b == LooperNode::kPlay ? 2 : b == LooperNode::kDub ? 2 : 0;
            const bool lit = b == LooperNode::kPlay ? playActive : b == LooperNode::kDub ? dubActive : false;
            n->SetButtonLevel(b, DrawGateControl(kIds[b], label, btnW, style, lit));
         }
         ImGui::SetCursorScreenPos(ImVec2(x0, y0));
         ImGui::Dummy(ImVec2(gAudioContentW, rowH));
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawLooperWave(n, 140.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Same rows, ranges and formats as the Sampler. Draw order is the pin
      // numbering: append only.
      AudioSlider("finetune", &n->finetune, -50.0f, 50.0f, "%.0f c", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      AudioSlider("speed", &n->speed, -2.0f, 2.0f, "%.2fx", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      AudioSlider("fade in", &n->fadeIn, 0.0f, 250.0f, "%.0f ms", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("fade out", &n->fadeOut, 0.0f, 250.0f, "%.0f ms", AudioHalfWidth());

      {
         AudioKnobRow row(3);
         std::vector<std::string> takeOptions;
         takeOptions.push_back("free");
         for (const std::string& s : MusicTime::RateDivisionList())
            takeOptions.push_back(s);
         row.Dropdown("take", takeOptions, n->take, [n](int i) { PushUndoCheckpoint(); n->take = i; });
         bool changed = false;
         row.Checkbox("sync", &n->syncStart, &changed);
         if (changed)
            PushUndoCheckpoint();
         row.Checkbox("thru", &n->thru, &changed);
         if (changed)
            PushUndoCheckpoint();
         row.End();
      }
      EndAudioBody();
   }


   // ---- MPC --------------------------------------------------------------
   // ---- MPC modulation addressing ------------------------------------------
   // Every pad's every param is a modulation destination with an address that is
   // a function of (pad, param) ONLY: floats at explicit ids
   // MpcNode::ParamId(pad, k) (100 + pad * 5 + k), the mode at a label-hashed
   // discrete slot whose label carries the pad ("pad N mode##mpcmodeN"). The
   // selected pad only decides which pad's widgets are DRAWN; the other fifteen
   // register through exactly the same calls under gParamRegisterOnly (the EQ's
   // hidden-band mechanism), so a cable never rebinds, retargets or stops
   // driving when the selection moves.
   const std::vector<std::string>& MpcModeList()
   {
      static const std::vector<std::string> kModes = { "one shot", "gate", "loop" };
      return kModes;
   }

   std::string MpcModeLabel(int pad)
   {
      char buf[48];
      snprintf(buf, sizeof(buf), "pad %d mode##mpcmode%d", pad + 1, pad);
      return buf;
   }

   std::string MpcParamName(int pad, int k)
   {
      char buf[48];
      snprintf(buf, sizeof(buf), "pad %d %s", pad + 1, MpcNode::Info(k).name);
      return buf;
   }

   // Sync: Pattern A of the rhythmic-quantization-standard ("Synced"/"Free" then
   // the canonical division list), one pair per pad. Index 0 is Synced.
   const std::vector<std::string>& MpcSyncList()
   {
      static const std::vector<std::string> kSync = { "Synced", "Free" };
      return kSync;
   }

   std::string MpcDiscreteLabel(int pad, int which)
   {
      char buf[48];
      if (which == kMpcMode)
         return MpcModeLabel(pad);
      snprintf(buf, sizeof(buf), which == kMpcSync ? "pad %d sync##mpcsync%d" : "pad %d rate##mpcdiv%d", pad + 1, pad);
      return buf;
   }

   int* MpcDiscreteValue(MpcNode* n, int pad, int which)
   {
      pad = MpcNode::Clamp(pad);
      return which == kMpcMode ? &n->padMode[pad] : (which == kMpcSync ? &n->padSync[pad] : &n->padDiv[pad]);
   }

   const std::vector<std::string>& MpcDiscreteList(int which)
   {
      return which == kMpcMode ? MpcModeList() : (which == kMpcSync ? MpcSyncList() : MusicTime::RateDivisionList());
   }

   int MpcDiscreteMax(int which)
   {
      return which == kMpcMode ? 2 : (which == kMpcSync ? 1 : (int)MusicTime::kNumRateDivisions - 1);
   }

   // Registers (never draws) one of `pad`'s dropdown params; applies a driven value.
   void RegisterMpcPadDiscrete(MpcNode* n, int pad, int which)
   {
      const std::string label = MpcDiscreteLabel(pad, which);
      int* value = MpcDiscreteValue(n, pad, which);
      const int hi = MpcDiscreteMax(which);
      const DiscreteParamHandle h = RegisterDiscreteParam(label.c_str(), (float)*value, (float)hi,
                                                          /*isBool=*/false, &MpcDiscreteList(which));
      if (h.driven)
         *value = std::clamp((int)lroundf(h.value), 0, hi);
   }

   // Registers (never draws) `pad`'s float params (the drawn pad's are the real widgets).
   void RegisterMpcPadFloats(MpcNode* n, int pad)
   {
      const bool saved = gParamRegisterOnly;
      gParamRegisterOnly = true;
      for (int k = 0; k < MpcNode::kNumPadParams; k++)
      {
         const MpcNode::ParamInfo& info = MpcNode::Info(k);
         const std::string name = MpcParamName(pad, k);
         ModSlider(info.name, n->PadParamPtr(pad, k), info.lo, info.hi, info.fmt, 100.0f, /*audioStyle=*/true, 0.0f,
                   nullptr, nullptr, MpcNode::ParamId(pad, k), name.c_str());
      }
      gParamRegisterOnly = saved;
   }

   // Register every pad's params in fixed order, whatever is selected:
   // DiscreteParamSlot probes on a hash collision in first-seen order, so the
   // order the dropdowns first register in must not depend on the selection. The
   // 16 mode dropdowns go first (as before sync existed), then the 16 sync, then
   // the 16 division ones. `drawnPad` (-1: none) keeps its float sliders for the
   // real widgets (its dropdowns are still registered here, then re-registered,
   // same slot, by the real dropdowns).
   void RegisterMpcParams(MpcNode* n, int drawnPad)
   {
      for (int p = 0; p < MpcNode::kPads; p++)
         if (p != drawnPad)
            RegisterMpcPadFloats(n, p);
      for (int which = 0; which < kMpcNumDiscrete; which++)
         for (int p = 0; p < MpcNode::kPads; p++)
            RegisterMpcPadDiscrete(n, p, which);
   }

   // Pin ids currently bound on `pad` (floats then the dropdown slots).
   std::vector<int> MpcPadBoundPins(MpcNode* n, int pad, int nodeIndex)
   {
      (void)n;
      std::vector<int> pins;
      for (int k = 0; k < MpcNode::kNumPadParams; k++)
         if (Modulation::Instance().IsModulated(nodeIndex, MpcNode::ParamId(pad, k)))
            pins.push_back(MpcNode::ParamId(pad, k));
      for (int which = 0; which < kMpcNumDiscrete; which++)
      {
         const int slot = DiscreteParamSlot(nodeIndex, MpcDiscreteLabel(pad, which));
         if (Modulation::Instance().IsModulated(nodeIndex, slot))
            pins.push_back(slot);
      }
      return pins;
   }


   // Routes a dropped audio file to a pad: the pad under the drop, else the
   // selected-pad waveform's box (the selected pad), else the first empty pad,
   // else the selected pad. Further files in one drop fill the next empty pads.
   void MpcDropFiles(MpcNode* n, float cx, float cy, const std::vector<std::string>& paths)
   {
      int pad = n->PadAtCanvas(cx, cy);
      if (pad < 0 && cx >= n->waveRect[0] && cx <= n->waveRect[2] && cy >= n->waveRect[1] && cy <= n->waveRect[3])
         pad = MpcNode::Clamp(n->selectedPad);
      if (pad < 0)
      {
         pad = n->NextEmptyPad(0);
         if (pad < 0)
            pad = MpcNode::Clamp(n->selectedPad);
      }
      for (const std::string& path : paths)
      {
         if (pad < 0)
            break;
         if (n->LoadPad(pad, path))
            n->selectedPad = pad;
         const int next = n->NextEmptyPad(pad + 1);
         pad = (next >= 0 && next != pad) ? next : -1;
      }
   }


   void DrawMpcBody(GraphNode& gn, MpcNode* n)
   {
      int loaded = 0;
      for (int p = 0; p < MpcNode::kPads; p++)
         loaded += n->PadLoaded(p) ? 1 : 0;
      char stat[112];
      if (loaded == 0)
         snprintf(stat, sizeof(stat), "empty - drop samples here, then click a pad or send notes 36-51");
      else
         snprintf(stat, sizeof(stat), "%d/16 loaded - click a pad or send notes 36-51", loaded);
      BeginAudioBody(gn.index, gn.category, MpcNodeWidth(), stat);
      const bool isLight = IsThemeLight();
      const double now = ImGui::GetTime();
      int openLoadPad = -1;
      const int sel = MpcNode::Clamp(n->selectedPad);
      n->selectedPad = sel;

      // The 15 pads that are not drawn this frame register under
      // gParamRegisterOnly (see the addressing note above), the selected one
      // through its real widgets below. Same fixed pad order every frame.
      RegisterMpcParams(n, sel);
      std::vector<int> boundPins[MpcNode::kPads];
      for (int p = 0; p < MpcNode::kPads; p++)
         boundPins[p] = MpcPadBoundPins(n, p, gn.index);

      // 4x4 pad grid, pad 1 bottom-left like a hardware MPC. Every pad is a
      // perfect square (kMpcPadSide, and the node is widened to fit them: see
      // MpcNodeWidth), with a gate (CV trigger) pin in a 16 px gutter to its left.
      {
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float pinW = kMpcPinGutter;
         const float side = kMpcPadSide;
         const float cellW = pinW + side;
         const float x0 = gAudioContentX;
         const float y0 = ImGui::GetCursorScreenPos().y;
         static const char* kModeTag[3] = { "shot", "gate", "loop" };
         for (int row = 0; row < 4; row++)
         {
            for (int col = 0; col < 4; col++)
            {
               const int pad = (3 - row) * 4 + col;
               char id[16];
               snprintf(id, sizeof(id), "pad%d", pad + 1);
               ImGui::SetCursorScreenPos(ImVec2(x0 + (float)col * (cellW + gap), y0 + (float)row * (side + gap)));
               const bool isLoaded = n->PadLoaded(pad);
               const bool isSel = pad == sel;
               const bool playing = n->PadPlaying(pad);
               const bool anyMod = !boundPins[pad].empty();
               ImVec2 modDot(0.0f, 0.0f);
               bool activated = false;
               const bool level = DrawGateButton(id, cellW, side,
                  [&](ImDrawList* dl, ImVec2 mn, ImVec2 mx, bool hovered, bool lvl)
                  {
                     n->padRect[pad][0] = mn.x; n->padRect[pad][1] = mn.y;
                     n->padRect[pad][2] = mx.x; n->padRect[pad][3] = mx.y;
                     if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        n->selectedPad = pad;
                     const bool hit = lvl || (now - n->padFlash[pad]) < 0.14;
                     const bool lit = playing || hit;
                     const ImU32 green = isLight ? tok::U32(tok::pal::c_239650FF) : tok::U32(tok::pal::c_3CAF64FF);
                     ImU32 fill = isLoaded ? (isLight ? tok::U32(tok::pal::c_D6DDECFF) : tok::U32(tok::pal::c_282E40FF))
                                           : (isLight ? tok::U32(tok::pal::c_E8EBF2FF) : tok::U32(tok::pal::c_1A1C25FF));
                     if (hovered && !lit)
                        fill = isLight ? tok::U32(tok::pal::c_CCD4E6FF) : tok::U32(tok::pal::c_343B50FF);
                     if (lit)
                        fill = green;
                     dl->AddRectFilled(mn, mx, fill, 6.0f);
                     const ImU32 selCol = isLight ? tok::U32(tok::pal::c_3264E6FF) : tok::U32(tok::pal::c_6EA0FFFF);
                     const ImU32 edge = isLight ? tok::U32(tok::pal::c_AAB2C3FF) : tok::U32(tok::pal::c_555C73C8);
                     dl->AddRect(mn, mx, isSel ? selCol : edge, 6.0f, 0, isSel ? 2.0f : 1.0f);

                     const ImU32 ink = lit ? tok::U32(tok::pal::c_FFFFFFFF)
                                           : (isLoaded ? (isLight ? tok::U32(tok::pal::c_282D3CFF) : tok::U32(tok::pal::c_D7DCEBFF))
                                                       : tok::U32(tok::pal::c_788096FF));
                     const ImU32 dim = lit ? tok::U32(tok::pal::c_FFFFFFBE) : (isLight ? tok::U32(tok::pal::c_5F6982FF) : tok::U32(tok::pal::c_969EB4FF));
                     dl->PushClipRect(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 1.0f, mx.y - 1.0f), true);
                     // All text in the pad is the size of a slider's label
                     // (AudioLabelText): number and mode tag on top, name below.
                     const float lh = AudioLabelFontSize();
                     char num[8];
                     snprintf(num, sizeof(num), "%d", pad + 1);
                     AudioLabelText(dl, ImVec2(mn.x + 5.0f, mn.y + 3.0f), ink, num);
                     // Mode tag; a synced pad also shows its division ("loop 1/8").
                     char tag[32];
                     snprintf(tag, sizeof(tag), "%s", kModeTag[std::clamp(n->padMode[pad], 0, 2)]);
                     if (n->padSync[pad] == MpcNode::kSynced)
                        snprintf(tag, sizeof(tag), "%s %s", kModeTag[std::clamp(n->padMode[pad], 0, 2)],
                                 MusicTime::RateDivisionName(n->padDiv[pad]));
                     const ImVec2 ts = AudioLabelSize(tag);
                     AudioLabelText(dl, ImVec2(mx.x - ts.x - 5.0f, mn.y + 3.0f), dim, tag);
                     if (isLoaded)
                     {
                        // Waveform, drawn as the Sampler draws its own: bars
                        // at 0.45 of the box either side of the mid line, no
                        // auto-gain.
                        const int cnt = n->padWaveCount[pad];
                        const float wx0 = mn.x + 5.0f, wx1 = mx.x - 5.0f;
                        const float wTop = mn.y + lh + 7.0f, wBot = mx.y - lh - 7.0f;
                        const float wy = (wTop + wBot) * 0.5f;
                        const float wh = wBot - wTop;
                        const ImU32 wc = lit ? tok::U32(tok::pal::c_FFFFFFE6) : (isLight ? tok::U32(tok::pal::c_1E6EE6D2) : tok::U32(tok::pal::c_96D6FFC8));
                        for (int i = 0; i < cnt; i++)
                        {
                           const float x = wx0 + (wx1 - wx0) * (float)i / (float)cnt;
                           const float bw = std::max(1.0f, (wx1 - wx0) / (float)cnt);
                           const float t = wy - n->padWaveMax[pad][i] * wh * 0.45f;
                           const float b = wy - n->padWaveMin[pad][i] * wh * 0.45f;
                           dl->AddRectFilled(ImVec2(x, std::min(t, wy - 0.5f)), ImVec2(x + bw, std::max(b, wy + 0.5f)), wc);
                        }
                        std::string nm = n->PadName(pad);
                        const size_t dot = nm.find_last_of('.');
                        if (dot != std::string::npos && dot > 0)
                           nm.resize(dot);
                        dl->PushClipRect(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 16.0f, mx.y - 1.0f), true);
                        AudioLabelText(dl, ImVec2(mn.x + 5.0f, mx.y - lh - 4.0f), dim, nm.c_str());
                        dl->PopClipRect();
                     }
                     else
                     {
                        const char* hint = "+ load";
                        const ImVec2 hs = AudioLabelSize(hint);
                        AudioLabelText(dl, ImVec2((mn.x + mx.x - hs.x) * 0.5f, (mn.y + mx.y - hs.y) * 0.5f), dim, hint);
                     }
                     // Modulation indicator: an orange dot at the bottom right
                     // of any pad with a bound param (any of its nine).
                     modDot = ImVec2(mx.x - 8.0f, mx.y - 8.0f);
                     if (anyMod)
                     {
                        dl->AddCircleFilled(modDot, 4.0f, isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF));
                        dl->AddCircle(modDot, 4.0f, isLight ? tok::U32(tok::pal::c_6E7382FF) : tok::U32(tok::pal::c_1E2028FF), 12, 1.0f);
                     }
                     dl->PopClipRect();
                  },
                  &activated);
               if (activated)
               {
                  n->selectedPad = pad;
                  if (!isLoaded)
                     openLoadPad = pad; // an empty pad has nothing to play: a click means "put a sample here"
               }
               if (level)
                  n->padFlash[pad] = now;
               n->SetPadHeld(pad, level, 1.0f);

               // A cable to an unselected pad's param has no slider on screen
               // to land on: give each bound one a 1 px invisible pin on the
               // tile's dot (as a collapsed node does), so the cable stays
               // attached and visible. The selected pad's cables land on its
               // real sliders below.
               if (!isSel && anyMod && modDot.x != 0.0f)
               {
                  const ImVec2 restore = ImGui::GetCursorScreenPos();
                  for (int slot : boundPins[pad])
                  {
                     const int pinId = gn.index * GraphNode::kStride + GraphNode::kParamBase + slot;
                     gDrawnParamPins.insert(pinId);
                     ImGui::SetCursorScreenPos(ImVec2(modDot.x - 0.5f, modDot.y - 0.5f));
                     ed::BeginPin(pinId, ed::PinKind::Input);
                     ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
                     ImGui::Dummy(ImVec2(1.0f, 1.0f));
                     ed::EndPin();
                  }
                  ImGui::SetCursorScreenPos(restore);
               }
            }
         }
         ImGui::SetCursorScreenPos(ImVec2(x0, y0));
         ImGui::Dummy(ImVec2(gAudioContentW, 4.0f * side + 3.0f * gap));
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Selected pad: header, waveform (also the drop target), Load / Folder /
      // Clear, then Sampler-style slider rows for that pad's params.
      {
         const int cur = MpcNode::Clamp(n->selectedPad); // a right-click above may have moved it
         char header[96];
         if (n->PadLoaded(cur))
            snprintf(header, sizeof(header), "pad %d - %s", cur + 1, n->PadName(cur).c_str());
         else
            snprintf(header, sizeof(header), "pad %d - empty", cur + 1);
         BeginAudioSection(header);

         {
            const float w = AudioFullWidth();
            const float h = 90.0f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 br(origin.x + w, origin.y + h);
            n->waveRect[0] = origin.x; n->waveRect[1] = origin.y; n->waveRect[2] = br.x; n->waveRect[3] = br.y;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool has = n->PadLoaded(cur);
            ImGui::InvisibleButton("##mpcwave", ImVec2(w, h));
            const bool hov = ImGui::IsItemHovered();
            if (!has && ImGui::IsItemClicked(ImGuiMouseButton_Left))
               openLoadPad = cur;
            // Same drawing as DrawSamplerWaveform.
            dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
            dl->PushClipRect(origin, br, true);
            const float midY = origin.y + h * 0.5f;
            dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);
            if (has)
            {
               const int cnt = n->padWaveCount[cur];
               const ImU32 wc = isLight ? tok::U32(tok::pal::c_1E6EE6D2) : tok::U32(tok::pal::c_96D6FFC8);
               for (int i = 0; i < cnt; i++)
               {
                  const float x = origin.x + w * (float)i / (float)cnt;
                  const float bw = std::max(1.0f, w / (float)cnt);
                  const float t = midY - n->padWaveMax[cur][i] * h * 0.45f;
                  const float b = midY - n->padWaveMin[cur][i] * h * 0.45f;
                  dl->AddRectFilled(ImVec2(x, std::min(t, midY - 0.5f)), ImVec2(x + bw, std::max(b, midY + 0.5f)), wc);
               }
            }
            else
               dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "no sample loaded");
            dl->PopClipRect();
            dl->AddRect(origin, br, (!has && hov) ? (isLight ? tok::U32(tok::pal::c_3264E6FF) : tok::U32(tok::pal::c_6EA0FFFF)) : ScopeBorderCol(), 4.0f);
            if (!has && hov)
               ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         }
         ImGui::Dummy(ImVec2(0.0f, 6.0f));

         if (ImGui::Button("Load...", ImVec2(90, 0)))
            openLoadPad = cur;
         ImGui::SameLine();
         if (ImGui::Button("Folder...", ImVec2(90, 0)))
         {
            const std::string folder = Platform::OpenFolderDialog("Load a folder into the 16 pads");
            if (!folder.empty())
            {
               PushUndoCheckpoint();
               n->LoadFolder(folder);
            }
         }
         ImGui::SameLine();
         ImGui::BeginDisabled(!n->PadLoaded(cur));
         if (ImGui::Button("Clear", ImVec2(70, 0)))
         {
            PushUndoCheckpoint();
            n->ClearPad(cur);
         }
         ImGui::EndDisabled();
         ImGui::Dummy(ImVec2(0.0f, 6.0f));

         // Same rows, ranges and formats as the Sampler; float ids are fixed
         // per (pad, param) so the drawn pad can change without moving a cable.
         auto slider = [&](int k, float width)
         {
            const MpcNode::ParamInfo& info = MpcNode::Info(k);
            const std::string name = MpcParamName(cur, k);
            AudioSlider(info.name, n->PadParamPtr(cur, k), info.lo, info.hi, info.fmt, width, nullptr, nullptr,
                        MpcNode::ParamId(cur, k), name.c_str());
         };
         // Two-column grid, five rows: timing (sync | rate), tuning, level,
         // pan | mode, fades. The rate (division) dropdown is greyed while the pad
         // is Free; it stays registered, so a cable on it survives.
         auto dropdown = [&](int which, float width)
         {
            const std::string label = MpcDiscreteLabel(cur, which);
            int* value = MpcDiscreteValue(n, cur, which);
            DropdownButton(label.c_str(), MpcDiscreteList(which), *value,
                           [value](int i) { PushUndoCheckpoint(); *value = i; }, width, /*showCaption=*/false);
         };
         dropdown(kMpcSync, AudioHalfWidth());
         ImGui::SameLine();
         ImGui::BeginDisabled(n->padSync[cur] != MpcNode::kSynced);
         dropdown(kMpcDiv, AudioHalfWidth());
         ImGui::EndDisabled();
         slider(MpcNode::kFine, AudioHalfWidth());
         ImGui::SameLine();
         slider(MpcNode::kPitch, AudioHalfWidth());
         slider(MpcNode::kSpeed, AudioHalfWidth());
         ImGui::SameLine();
         slider(MpcNode::kVolume, AudioHalfWidth());
         slider(MpcNode::kPan, AudioHalfWidth());
         ImGui::SameLine();
         dropdown(kMpcMode, AudioHalfWidth());
         slider(MpcNode::kFadeIn, AudioHalfWidth());
         ImGui::SameLine();
         slider(MpcNode::kFadeOut, AudioHalfWidth());
         EndAudioSection();
      }
      EndAudioBody();

      if (openLoadPad >= 0)
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            if (n->LoadPad(openLoadPad, path))
               n->selectedPad = openLoadPad;
         }
      }
   }
}
