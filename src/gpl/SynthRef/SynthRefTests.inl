// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
// Part of src/main.cpp, included there inside #if INFINITE_GPL.
//
// INFINITE_SYNTHREFTEST: headless checks of Synth REF against its real
// firmware core (no audio device, no window). Every line ends "ok" or
// contains FAIL; the last line is the verdict. Numbers printed are the
// measurements, so a run doubles as the quality / CPU report
// (docs/SYNTH-REF.md, "Test status").
namespace SynthRefTest
{
   struct Stats
   {
      double rms = 0.0, peak = 0.0, dc = 0.0;
      bool finite = true;
   };

   Stats Measure(const std::vector<float>& l, const std::vector<float>& r, size_t from, size_t to)
   {
      Stats s;
      double sum = 0.0, sq = 0.0;
      size_t n = 0;
      to = std::min(to, l.size());
      for (size_t i = from; i < to; ++i)
         for (float v : { l[i], r[i] })
         {
            if (!std::isfinite(v))
               s.finite = false;
            sum += v;
            sq += (double)v * v;
            s.peak = std::max(s.peak, (double)std::fabs(v));
            ++n;
         }
      if (n)
      {
         s.rms = std::sqrt(sq / n);
         s.dc = sum / n;
      }
      return s;
   }

   // Fundamental by normalised autocorrelation over 40..2000 Hz.
   double PitchHz(const std::vector<float>& x, size_t from, size_t n, double sr)
   {
      if (from + n * 2 > x.size())
         return 0.0;
      const int minLag = (int)(sr / 2000.0), maxLag = (int)(sr / 40.0);
      double best = 0.0;
      int bestLag = 0;
      std::vector<double> c((size_t)maxLag + 2, 0.0);
      for (int lag = minLag; lag <= maxLag + 1; ++lag)
      {
         double num = 0.0, e0 = 0.0, e1 = 0.0;
         for (size_t i = 0; i < n; ++i)
         {
            const double a = x[from + i], b = x[from + i + (size_t)lag];
            num += a * b;
            e0 += a * a;
            e1 += b * b;
         }
         c[(size_t)lag] = num / std::sqrt(e0 * e1 + 1e-20);
      }
      // First peak above 0.9 of the global maximum (avoids octave-down picks).
      double global = 0.0;
      for (int lag = minLag; lag <= maxLag; ++lag)
         global = std::max(global, c[(size_t)lag]);
      for (int lag = minLag + 1; lag <= maxLag; ++lag)
         if (c[(size_t)lag] >= 0.9 * global && c[(size_t)lag] >= c[(size_t)lag - 1] && c[(size_t)lag] >= c[(size_t)lag + 1])
         {
            best = c[(size_t)lag];
            bestLag = lag;
            break;
         }
      if (bestLag == 0)
         return 0.0;
      // Parabolic refinement.
      const double y0 = c[(size_t)bestLag - 1], y1 = best, y2 = c[(size_t)bestLag + 1];
      const double den = y0 - 2.0 * y1 + y2;
      const double shift = std::fabs(den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
      return sr / (bestLag + shift);
   }

   bool Render(SynthRefNode& n, std::vector<float>& L, std::vector<float>& R, int frames, double sr, int block = 512)
   {
      const size_t at = L.size();
      L.resize(at + (size_t)frames);
      R.resize(at + (size_t)frames);
      return n.RenderForTest(L.data() + at, R.data() + at, frames, sr, block);
   }

   void Midi(SynthRefNode& n, int status, int a, int b)
   {
      const int ch = std::max(0, n.Query(SR_Q_KEY_CHANNEL));
      const uint8_t m[3] = { (uint8_t)(status | ch), (uint8_t)a, (uint8_t)b };
      n.SendMidi(m, 3);
   }

   int failures = 0;

   void Check(bool ok, const char* what, const char* detail = "")
   {
      std::printf("SYNTHREFTEST %-34s %s %s\n", what, detail, ok ? "ok" : "FAIL");
      std::fflush(stdout);
      if (!ok)
         ++failures;
   }
}

namespace SynthRefTest
{
   // The checks every selectable core must pass, whatever the instrument.
   void RunCoreGeneric(const SynthRefFirmware& fw)
   {
      char buf[320], what[96];
      auto make = [&]() {
         auto n = std::make_unique<SynthRefNode>();
         n->coreId = fw.coreId;
         return n;
      };
      auto a = make();
      std::vector<float> l, r;
      const bool loaded = Render(*a, l, r, 4800, 48000.0);
      snprintf(what, sizeof(what), "[%s] core loads", fw.coreId);
      snprintf(buf, sizeof(buf), "(%s)", loaded && a->Info() ? a->Info()->coreName : a->CoreError().c_str());
      Check(loaded && a->Info() != nullptr, what, buf);
      if (!loaded || a->Info() == nullptr)
         return;
      const SynthRefCoreInfo* info = a->Info();

      l.clear();
      r.clear();
      Render(*a, l, r, 48000, 48000.0);
      const Stats quiet = Measure(l, r, 0, l.size());
      snprintf(what, sizeof(what), "[%s] silence with no notes", fw.coreId);
      snprintf(buf, sizeof(buf), "(peak %.2e)", quiet.peak);
      Check(quiet.finite && quiet.peak < 1e-4, what, buf);

      // A note on the keyboard track's channel sounds and releases.
      Midi(*a, 0x90, 60, 100);
      l.clear();
      r.clear();
      Render(*a, l, r, 24000, 48000.0);
      const Stats on = Measure(l, r, 0, l.size());
      Midi(*a, 0x80, 60, 0);
      l.clear();
      r.clear();
      Render(*a, l, r, 48000 * 5, 48000.0);
      const double tail = Measure(l, r, l.size() - 4800, l.size()).rms;
      snprintf(what, sizeof(what), "[%s] note sounds and releases", fw.coreId);
      snprintf(buf, sizeof(buf), "(rms %.4f, peak %.3f, dc %.1e, after 5 s %.1e)", on.rms, on.peak, on.dc, tail);
      Check(on.finite && on.rms > 0.001 && on.peak < 1.0 && std::fabs(on.dc) < 0.02 && tail < 1e-3, what, buf);

      // The instrument's own keyboard (panel key events) plays too.
      a->SendControl(SR_CTL_KEY, 12, 110);
      l.clear();
      r.clear();
      Render(*a, l, r, 24000, 48000.0);
      const double keyRms = Measure(l, r, 0, l.size()).rms;
      a->SendControl(SR_CTL_KEY, 12, 0);
      Render(*a, l, r, 48000 * 3, 48000.0);
      snprintf(what, sizeof(what), "[%s] panel key plays", fw.coreId);
      snprintf(buf, sizeof(buf), "(rms %.4f)", keyRms);
      Check(keyRms > 0.001, what, buf);

      // Block size independence.
      std::vector<float> outs[2];
      for (int k = 0; k < 2; ++k)
      {
         auto n = make();
         std::vector<float> nl, nr;
         Render(*n, nl, nr, 2048, 48000.0, k ? 333 : 512);
         Midi(*n, 0x90, 64, 90);
         nl.clear();
         nr.clear();
         Render(*n, nl, nr, 24000, 48000.0, k ? 333 : 512);
         outs[k] = nl;
      }
      snprintf(what, sizeof(what), "[%s] block size independence", fw.coreId);
      Check(outs[0] == outs[1], what, outs[0] == outs[1] ? "(512 vs 333: identical)" : "(512 vs 333 differ)");

      // Two instances, state save / load reproducible.
      {
         auto x = make();
         auto y = make();
         std::vector<float> xl, xr, yl, yr;
         Render(*x, xl, xr, 256, 48000.0);
         Render(*y, yl, yr, 256, 48000.0);
         Midi(*x, 0x90, 60, 100);
         xl.clear(); xr.clear(); yl.clear(); yr.clear();
         Render(*x, xl, xr, 12000, 48000.0);
         Render(*y, yl, yr, 12000, 48000.0);
         snprintf(what, sizeof(what), "[%s] two instances independent", fw.coreId);
         snprintf(buf, sizeof(buf), "(A rms %.4f, B peak %.1e)", Measure(xl, xr, 0, xl.size()).rms, Measure(yl, yr, 0, yl.size()).peak);
         Check(Measure(xl, xr, 0, xl.size()).rms > 0.001 && Measure(yl, yr, 0, yl.size()).peak < 1e-4, what, buf);

         x->SendControl(SR_CTL_ENCODER, 2, 3); // a panel edit, whatever it does on this firmware
         Render(*x, xl, xr, 4800, 48000.0);
         const bool saved = x->SaveStateNow();
         const std::string blob = x->state;
         std::vector<float> o2[2];
         for (int k = 0; k < 2; ++k)
         {
            auto m = make();
            m->state = blob;
            std::vector<float> ml, mr;
            Render(*m, ml, mr, 256, 48000.0);
            Midi(*m, 0x90, 57, 100);
            ml.clear();
            mr.clear();
            Render(*m, ml, mr, 12000, 48000.0);
            o2[k] = ml;
            if (!m->CoreError().empty())
               o2[k].clear();
         }
         snprintf(what, sizeof(what), "[%s] state save / load reproducible", fw.coreId);
         snprintf(buf, sizeof(buf), "(%zu chars, identical %d)", blob.size(), (int)(o2[0] == o2[1] && !o2[0].empty()));
         Check(saved && !blob.empty() && !o2[0].empty() && o2[0] == o2[1] && Measure(o2[0], o2[0], 0, o2[0].size()).rms > 0.001,
               what, buf);
      }

      // The firmware's screen.
      uint32_t rev = 0;
      const uint16_t* px = a->LcdPixels(rev);
      size_t lit = 0;
      for (size_t i = 0; px && i < (size_t)info->lcdWidth * info->lcdHeight; ++i)
         lit += px[i] != 0;
      snprintf(what, sizeof(what), "[%s] firmware screen", fw.coreId);
      snprintf(buf, sizeof(buf), "(%zu lit pixels, revision %u)", lit, rev);
      Check(info->lcdFormat == SR_LCD_NONE || (px != nullptr && lit > 500), what, buf);

      // Fast notes from the notes pin: 40 notes of 2 ms (note-on and note-off
      // inside one 512-frame host block), 60 ms apart. Each must sound.
      {
         auto n = make();
         std::vector<float> nl, nr;
         Render(*n, nl, nr, 2048, 48000.0, 512);
         NoteEventQueue queue;
         const int cursor = queue.RegisterConsumer();
         n->GetAudioNode()->SetNoteInbox(&queue, cursor);
         int heard = 0;
         for (int k = 0; k < 40; ++k)
         {
            NoteEvent on;
            on.note = 60 + (k % 12);
            on.velocity = 0.9f;
            on.isNoteOn = true;
            on.frameOffset = 10;
            NoteEvent off = on;
            off.isNoteOn = false;
            off.velocity = 0.0f;
            off.frameOffset = 106;
            queue.Push(on);
            queue.Push(off);
            nl.clear();
            nr.clear();
            Render(*n, nl, nr, 512, 48000.0, 512);
            Render(*n, nl, nr, 2880 - 512, 48000.0, 512);
            if (Measure(nl, nr, 0, nl.size()).rms > 0.001)
               ++heard;
         }
         n->GetAudioNode()->SetNoteInbox(nullptr, -1);
         snprintf(what, sizeof(what), "[%s] fast notes from the pin", fw.coreId);
         snprintf(buf, sizeof(buf), "(%d/40 sounded, 2 ms notes)", heard);
         Check(heard == 40, what, buf);
      }

      // CPU: 5 s of a held chord at 48 kHz.
      {
         auto n = make();
         std::vector<float> nl, nr;
         Render(*n, nl, nr, 256, 48000.0);
         for (int note : { 48, 55, 60, 64, 67, 72 })
            Midi(*n, 0x90, note, 100);
         nl.reserve(48000 * 5 + 256);
         nr.reserve(48000 * 5 + 256);
         const auto t0 = std::chrono::steady_clock::now();
         Render(*n, nl, nr, 48000 * 5, 48000.0, 256);
         const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
         snprintf(what, sizeof(what), "[%s] CPU per instance", fw.coreId);
         snprintf(buf, sizeof(buf), "(%.2f%% of one core)", secs * 20.0);
         Check(secs < 2.5, what, buf);
      }
   }
}

static int RunSynthRefTest()
{
   using namespace SynthRefTest;
   char buf[256];
   failures = 0;

   // 1. Core loads from <exe>\synthref\cores, one private copy per node.
   auto a = std::make_unique<SynthRefNode>();
   std::vector<float> L, R;
   const bool rendered = Render(*a, L, R, 4410, 44100.0);
   const SynthRefCoreInfo* info = a->Info();
   snprintf(buf, sizeof(buf), "(%s)", a->CoreError().c_str());
   Check(rendered && info != nullptr, "core loads", a->CoreReady() ? "" : buf);
   if (info == nullptr)
   {
      std::printf("SYNTHREFTEST FAIL: no core, remaining checks skipped\n");
      return 1;
   }
   snprintf(buf, sizeof(buf), "(%s %s, %u Hz, %u frames, %u ch, %u controls)", info->coreName, info->coreVersion,
            info->sampleRate, info->blockFrames, info->channels, info->controlCount);
   Check(info->sampleRate == 44100 && info->blockFrames == 128 && info->channels == 2, "core info", buf);

   // 2. Silence when nothing plays (fresh factory state).
   L.clear();
   R.clear();
   Render(*a, L, R, 44100, 44100.0);
   Stats quiet = Measure(L, R, 0, L.size());
   snprintf(buf, sizeof(buf), "(peak %.2e)", quiet.peak);
   Check(quiet.finite && quiet.peak < 1e-6, "silence with no notes", buf);

   // 3. Bit-exact with the bare core at the core's own rate (resampler bypassed).
   {
      std::string err;
      auto ref = SynthRefCore::Open("smk37_open", err);
      auto b = std::make_unique<SynthRefNode>();
      std::vector<float> bl, br;
      Render(*b, bl, br, 128, 44100.0); // boots; publishes the keyboard channel
      bool same = ref != nullptr;
      if (ref)
      {
         const SynthRefCoreApi& api = ref->Api();
         api.boot(ref->Inst(), SR_BOOT_CLEAR);
         // Advance the reference by the same 128 frames the node rendered.
         std::vector<float> rl, rr;
         const float* blk = api.render(ref->Inst());
         (void)blk;
         const int ch = std::max(0, b->Query(SR_Q_KEY_CHANNEL));
         const uint8_t on[3] = { (uint8_t)(0x90 | ch), 60, 100 };
         api.midi(ref->Inst(), on, 3);
         Midi(*b, 0x90, 60, 100);
         bl.clear();
         br.clear();
         Render(*b, bl, br, 44100, 44100.0);
         for (int blkN = 0; blkN < 44100 / 128 + 1; ++blkN)
         {
            const float* p = api.render(ref->Inst());
            for (int i = 0; i < 128; ++i)
            {
               rl.push_back(p[i * 2]);
               rr.push_back(p[i * 2 + 1]);
            }
         }
         size_t diff = 0;
         for (size_t i = 0; i < bl.size(); ++i)
            if (bl[i] != rl[i] || br[i] != rr[i])
               ++diff;
         same = diff == 0;
         Stats s = Measure(bl, br, 0, bl.size());
         snprintf(buf, sizeof(buf), "(%zu samples, %zu differ, rms %.4f)", bl.size(), diff, s.rms);
      }
      else
         snprintf(buf, sizeof(buf), "(%s)", err.c_str());
      Check(same, "bit-exact vs bare core @44.1k", buf);
   }

   // 4. A4 at 44.1 and 48 kHz: same pitch (resampler), level, DC, no clipping.
   double pitch441 = 0.0, pitch48 = 0.0, rmsOn = 0.0;
   for (double sr : { 44100.0, 48000.0 })
   {
      auto n = std::make_unique<SynthRefNode>();
      std::vector<float> l, r;
      Render(*n, l, r, 256, sr);
      Midi(*n, 0x90, 69, 100);
      l.clear();
      r.clear();
      Render(*n, l, r, (int)(sr * 1.0), sr);
      const Stats s = Measure(l, r, (size_t)(sr * 0.2), l.size());
      const double hz = PitchHz(l, (size_t)(sr * 0.3), (size_t)(sr * 0.1), sr);
      (sr == 44100.0 ? pitch441 : pitch48) = hz;
      if (sr == 48000.0)
         rmsOn = s.rms;
      snprintf(buf, sizeof(buf), "(@%.0f: %.2f Hz, rms %.4f, peak %.4f, dc %.2e)", sr, hz, s.rms, s.peak, s.dc);
      Check(s.finite && s.peak < 1.0 && std::fabs(s.dc) < 0.01 && s.rms > 0.002, "note A4 level / dc / clip", buf);
   }
   {
      const double octaves = std::log2(pitch48 / 440.0);
      const double cents = std::fabs(octaves - std::round(octaves)) * 1200.0;
      const double rateCents = std::fabs(std::log2(pitch48 / std::max(pitch441, 1.0))) * 1200.0;
      snprintf(buf, sizeof(buf), "(%.2f Hz = 440 x 2^%.0f %+.1f cents; 44.1k vs 48k %.2f cents)", pitch48, std::round(octaves),
               (octaves - std::round(octaves)) * 1200.0, rateCents);
      Check(pitch48 > 0.0 && cents < 15.0 && rateCents < 3.0, "tuning A4 (octave of 440 Hz)", buf);
   }

   // 5. Velocity, polyphony, note-off + release, panic.
   {
      auto n = std::make_unique<SynthRefNode>();
      std::vector<float> l, r;
      Render(*n, l, r, 256, 48000.0);
      double rmsVel[2] = {};
      for (int k = 0; k < 2; ++k)
      {
         n->SendControl(SR_CTL_KEY, 21, k == 0 ? 20 : 120); // the panel key, firmware keyboard path
         l.clear();
         r.clear();
         Render(*n, l, r, 24000, 48000.0);
         rmsVel[k] = Measure(l, r, 4800, l.size()).rms;
         n->SendControl(SR_CTL_KEY, 21, 0);
         l.clear();
         r.clear();
         Render(*n, l, r, 48000 * 4, 48000.0);
      }
      snprintf(buf, sizeof(buf), "(vel 20 rms %.4f, vel 120 rms %.4f)", rmsVel[0], rmsVel[1]);
      Check(rmsVel[1] > rmsVel[0] * 1.05, "velocity response (panel key)", buf);

      for (int note : { 60, 64, 67, 71 })
         Midi(*n, 0x90, note, 100);
      l.clear();
      r.clear();
      Render(*n, l, r, 24000, 48000.0);
      const double chord = Measure(l, r, 4800, l.size()).rms;
      snprintf(buf, sizeof(buf), "(4-note rms %.4f vs 1-note %.4f)", chord, rmsOn);
      Check(chord > rmsOn * 1.2, "polyphony", buf);

      for (int note : { 60, 64, 67, 71 })
         Midi(*n, 0x80, note, 0);
      l.clear();
      r.clear();
      Render(*n, l, r, 48000 * 5, 48000.0);
      const double tail = Measure(l, r, l.size() - 4800, l.size()).rms;
      snprintf(buf, sizeof(buf), "(rms of the last 100 ms after 5 s: %.2e)", tail);
      Check(tail < 1e-4, "note-off and release to silence", buf);

      for (int note = 40; note < 80; note += 3)
         Midi(*n, 0x90, note, 110);
      Render(*n, l, r, 4800, 48000.0);
      n->Panic();
      l.clear();
      r.clear();
      Render(*n, l, r, 48000 * 3, 48000.0);
      const double afterPanic = Measure(l, r, l.size() - 4800, l.size()).rms;
      snprintf(buf, sizeof(buf), "(rms 3 s after panic: %.2e)", afterPanic);
      Check(afterPanic < 1e-4, "panic / all notes off", buf);
   }

   // 6. Block size independence and sample rates.
   {
      std::vector<std::vector<float>> outs;
      for (int block : { 64, 333, 512, 2048 })
      {
         auto n = std::make_unique<SynthRefNode>();
         std::vector<float> l, r;
         Render(*n, l, r, 2048, 48000.0, block);
         Midi(*n, 0x90, 62, 90);
         l.clear();
         r.clear();
         Render(*n, l, r, 48000, 48000.0, block);
         outs.push_back(l);
      }
      size_t diff = 0;
      for (size_t k = 1; k < outs.size(); ++k)
         for (size_t i = 0; i < outs[0].size(); ++i)
            if (outs[k][i] != outs[0][i])
               ++diff;
      snprintf(buf, sizeof(buf), "(blocks 64/333/512/2048: %zu samples differ)", diff);
      Check(diff == 0, "block size independence", buf);
      for (double sr : { 22050.0, 32000.0, 88200.0, 96000.0 })
      {
         auto n = std::make_unique<SynthRefNode>();
         std::vector<float> l, r;
         Render(*n, l, r, 256, sr, 256);
         Midi(*n, 0x90, 69, 100);
         l.clear();
         r.clear();
         Render(*n, l, r, (int)(sr * 0.6), sr, 256);
         const Stats s = Measure(l, r, (size_t)(sr * 0.1), l.size());
         const double hz = PitchHz(l, (size_t)(sr * 0.3), (size_t)(sr * 0.1), sr);
         const double rateCents = std::fabs(std::log2(hz / std::max(pitch48, 1.0))) * 1200.0;
         snprintf(buf, sizeof(buf), "(@%.0f: rms %.4f, %.2f Hz, %.2f cents from 48k)", sr, s.rms, hz, rateCents);
         Check(s.finite && s.rms > 0.002 && s.peak < 1.0 && rateCents < 3.0, "sample rate", buf);
      }
   }

   // 7. Two instances never share state (each has its own DLL copy).
   {
      auto x = std::make_unique<SynthRefNode>();
      auto y = std::make_unique<SynthRefNode>();
      std::vector<float> xl, xr, yl, yr;
      Render(*x, xl, xr, 256, 48000.0);
      Render(*y, yl, yr, 256, 48000.0);
      x->LoadProgram(17);
      Midi(*x, 0x90, 60, 100);
      xl.clear(); xr.clear(); yl.clear(); yr.clear();
      Render(*x, xl, xr, 24000, 48000.0);
      Render(*y, yl, yr, 24000, 48000.0);
      const Stats sx = Measure(xl, xr, 0, xl.size()), sy = Measure(yl, yr, 0, yl.size());
      const int px = x->Query(SR_Q_PROGRAM), py = y->Query(SR_Q_PROGRAM);
      snprintf(buf, sizeof(buf), "(A rms %.4f program %d, B rms %.2e program %d)", sx.rms, px, sy.peak, py);
      Check(sx.rms > 0.002 && sy.peak < 1e-6 && px == 17 && py != 17, "two instances independent", buf);
   }

   // 8. Presets: every factory project and every DX7 program while playing.
   {
      auto n = std::make_unique<SynthRefNode>();
      std::vector<float> l, r;
      Render(*n, l, r, 256, 48000.0);
      const int factories = (int)n->FactoryNames().size(), programs = (int)n->ProgramNames().size();
      int loaded = 0, ok = 0;
      for (int i = 0; i < factories; ++i)
      {
         ok += n->LoadFactory(i) == 0;
         ++loaded;
         Midi(*n, 0x90, 60, 100);
         Render(*n, l, r, 2400, 48000.0);
         Midi(*n, 0x80, 60, 0);
      }
      n->LoadFactory(0);
      int progOk = 0;
      // Program loads need a DX7 track; the factory state's track 1 is one.
      n->ResetFirmware();
      Render(*n, l, r, 256, 48000.0);
      for (int p = 0; p < programs; ++p)
      {
         progOk += n->LoadProgram(p) == 0;
         Midi(*n, 0x90, 48 + p % 24, 90);
         Render(*n, l, r, 480, 48000.0);
         Midi(*n, 0x80, 48 + p % 24, 0);
      }
      const Stats s = Measure(l, r, 0, l.size());
      snprintf(buf, sizeof(buf), "(%d/%d factory projects, %d/%d DX7 programs, finite %d, peak %.3f)", ok, loaded, progOk,
               programs, (int)s.finite, s.peak);
      Check(factories > 0 && ok == loaded && programs == 128 && progOk == programs && s.finite, "preset changes while playing", buf);
      std::printf("SYNTHREFTEST   first programs: %s, %s, %s, %s\n", n->ProgramNames()[0].c_str(), n->ProgramNames()[1].c_str(),
                  n->ProgramNames()[2].c_str(), n->ProgramNames()[3].c_str());
   }

   // 9. State save -> load reproduces the sound exactly; core swap; missing core.
   {
      auto n = std::make_unique<SynthRefNode>();
      std::vector<float> l, r;
      Render(*n, l, r, 256, 48000.0);
      n->LoadProgram(42);
      n->SendControl(SR_CTL_ENCODER, 7, -20); // master volume down 20 detents
      Render(*n, l, r, 4800, 48000.0);
      const bool saved = n->SaveStateNow();
      const std::string blob = n->state;
      std::vector<float> outs[2];
      int programs[2] = {};
      for (int k = 0; k < 2; ++k)
      {
         auto m = std::make_unique<SynthRefNode>();
         m->state = blob;
         std::vector<float> ml, mr;
         Render(*m, ml, mr, 256, 48000.0);
         programs[k] = m->Query(SR_Q_PROGRAM);
         Midi(*m, 0x90, 57, 100);
         ml.clear();
         mr.clear();
         Render(*m, ml, mr, 24000, 48000.0);
         outs[k] = ml;
      }
      snprintf(buf, sizeof(buf), "(%zu chars, programs %d/%d, identical %d)", blob.size(), programs[0], programs[1],
               (int)(outs[0] == outs[1]));
      Check(saved && !blob.empty() && programs[0] == 42 && programs[1] == 42 && outs[0] == outs[1] &&
                Measure(outs[0], outs[0], 0, outs[0].size()).rms > 0.001,
            "state save / load reproducible", buf);

      n->coreId = "smk37_stock"; // registry entry without a core
      Render(*n, l, r, 256, 48000.0);
      const bool silentMissing = !n->CoreReady() && !n->CoreError().empty() && n->state == blob;
      snprintf(buf, sizeof(buf), "(%s)", n->CoreError().c_str());
      Check(silentMissing, "missing core: silent, state kept", buf);
      n->coreId = "smk37_open";
      Render(*n, l, r, 2400, 48000.0);
      Check(n->CoreReady() && n->Query(SR_Q_PROGRAM) == 42, "core comes back with its state");
   }

   // 10. Panel: screen, LEDs and buttons are the firmware's.
   {
      auto n = std::make_unique<SynthRefNode>();
      std::vector<float> l, r;
      Render(*n, l, r, 4800, 48000.0);
      uint32_t rev0 = 0, rev1 = 0;
      const uint16_t* px = n->LcdPixels(rev0);
      size_t lit = 0;
      for (size_t i = 0; px && i < (size_t)info->lcdWidth * info->lcdHeight; ++i)
         lit += px[i] != 0;
      n->SendControl(SR_CTL_BUTTON, 14, 1); // FX
      Render(*n, l, r, 2400, 48000.0);
      n->SendControl(SR_CTL_BUTTON, 14, 0);
      Render(*n, l, r, 4800, 48000.0);
      n->LcdPixels(rev1);
      snprintf(buf, sizeof(buf), "(%zu lit pixels, screen revision %u -> %u)", lit, rev0, rev1);
      Check(px != nullptr && lit > 1000 && rev1 != rev0, "firmware screen reacts to FX button", buf);
   }

   // 11. Notes pin: NoteEvents from a cabled source play the keyboard track.
   {
      auto n = std::make_unique<SynthRefNode>();
      std::vector<float> l, r;
      Render(*n, l, r, 256, 48000.0);
      NoteEventQueue queue;
      const int cursor = queue.RegisterConsumer();
      n->GetAudioNode()->SetNoteInbox(&queue, cursor);
      NoteEvent on;
      on.note = 64;
      on.velocity = 0.8f;
      on.isNoteOn = true;
      queue.Push(on);
      l.clear();
      r.clear();
      Render(*n, l, r, 24000, 48000.0);
      const double rms = Measure(l, r, 4800, l.size()).rms;
      NoteEvent off = on;
      off.isNoteOn = false;
      off.velocity = 0.0f;
      queue.Push(off);
      l.clear();
      r.clear();
      Render(*n, l, r, 48000 * 4, 48000.0);
      const double tail = Measure(l, r, l.size() - 4800, l.size()).rms;
      n->GetAudioNode()->SetNoteInbox(nullptr, -1);
      snprintf(buf, sizeof(buf), "(held rms %.4f, after release %.2e)", rms, tail);
      Check(rms > 0.002 && tail < 1e-4, "notes pin plays and releases", buf);
   }

   // 12. AUTO MAP profile for the MiniLab 3 loads and maps what it says.
   {
      std::vector<std::string> errors;
      const auto profiles = SrLoadProfiles(errors);
      const SrProfile* p = SrFindProfile(profiles, "Minilab3 MIDI", "smk37_open", "");
      const SrProfile* mcu = SrFindProfile(profiles, "MIDIIN2 (Minilab3 MCU)", "smk37_open", "");
      bool ok = p != nullptr && errors.empty();
      if (p)
      {
         const SrMapTable& t = p->table;
         ok = ok && t.cc[0][74].target == kSrEncoder && t.cc[0][74].id == 0 && t.cc[0][16].id == 7 &&
              t.cc[0][82].target == kSrFader && t.cc[0][17].id == 3 && t.cc[0][114].input == kSrRel64 &&
              t.note[9][36].target == kSrPad && t.note[9][51].id == 15 && t.keyboardChannel == -1 &&
              t.cc[14][74].target == kSrEncoder && t.note[14][60].target == kSrNone; // keys / knobs on channel 15 too
      }
      ok = ok && SrRelativeDelta(kSrRel64, 66) == 2 && SrRelativeDelta(kSrRel2c, 126) == -2 && SrRelativeDelta(kSrRel16, 13) == -3;
      snprintf(buf, sizeof(buf), "(%zu profiles, %zu errors, MCU port %s)", profiles.size(), errors.size(), mcu ? "MATCHED (wrong)" : "excluded");
      Check(ok && mcu == nullptr, "MiniLab 3 profile", buf);
   }

   // 12b. The controller path end to end (as the MiniLab 3 reported it on a
   // real unit: keys on channel 15): notes play the keyboard track, an
   // absolute encoder turns into detents, pads drive the firmware's pads.
   {
      auto n = std::make_unique<SynthRefNode>();
      n->deviceKey = "Minilab3 MIDI";
      n->deviceLock = true;
      int frame = 1;
      std::vector<float> l, r;
      Render(*n, l, r, 256, 48000.0);
      n->CookIfNeeded(frame++); // device policy, AUTO MAP table
      const uint8_t on[3] = { 0x9E, 60, 100 }, off[3] = { 0x8E, 60, 0 };
      n->InjectDeviceMidiForTest(on, 3);
      l.clear();
      r.clear();
      Render(*n, l, r, 24000, 48000.0);
      const double held = Measure(l, r, 4800, l.size()).rms;
      n->InjectDeviceMidiForTest(off, 3);
      l.clear();
      r.clear();
      Render(*n, l, r, 48000 * 4, 48000.0);
      const double tail = Measure(l, r, l.size() - 4800, l.size()).rms;
      const uint8_t k1a[3] = { 0xBE, 74, 64 }, k1b[3] = { 0xBE, 74, 70 }, k1c[3] = { 0xBE, 74, 67 };
      n->InjectDeviceMidiForTest(k1a, 3); // first value only sets the position
      n->InjectDeviceMidiForTest(k1b, 3);
      n->InjectDeviceMidiForTest(k1c, 3);
      Render(*n, l, r, 2400, 48000.0);
      n->CookIfNeeded(frame++); // drains the diagnostics into the panel state
      snprintf(buf, sizeof(buf), "(ch15 note rms %.4f, after off %.2e, K1 turned %d detents, active %d)", held, tail,
               n->encoderTurns[0], (int)n->DeviceActive());
      Check(held > 0.002 && tail < 1e-4 && n->encoderTurns[0] == 3 && n->ActiveProfile() != nullptr,
            "controller: keys on ch 15, encoder detents", buf);
   }

   // 13. CPU: one instance, 10 s of a held chord at 48 kHz.
   {
      auto n = std::make_unique<SynthRefNode>();
      std::vector<float> l, r;
      Render(*n, l, r, 256, 48000.0);
      for (int note : { 48, 55, 60, 64, 67, 72 })
         Midi(*n, 0x90, note, 100);
      l.reserve(48000 * 10 + 256);
      r.reserve(48000 * 10 + 256);
      const auto t0 = std::chrono::steady_clock::now();
      Render(*n, l, r, 48000 * 10, 48000.0, 256);
      const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      snprintf(buf, sizeof(buf), "(%.1f ms per audio second = %.2f%% of one core)", secs * 100.0, secs * 10.0);
      Check(secs < 5.0, "CPU per instance", buf);
   }

   // 14. Every selectable firmware (Supported and Experimental) passes the
   // generic checks; unavailable ones are listed but never selectable.
   for (const SynthRefFirmware& fw : SynthRefFirmwares())
      if (fw.status != SynthRefStatus::Unavailable)
         RunCoreGeneric(fw);
      else
      {
         auto n = std::make_unique<SynthRefNode>();
         n->coreId = fw.coreId;
         std::vector<float> l, r;
         const bool rendered = Render(*n, l, r, 256, 48000.0);
         snprintf(buf, sizeof(buf), "[%s] unavailable: refused", fw.coreId);
         Check(!rendered && !n->CoreReady() && !n->CoreError().empty(), buf);
      }

   std::printf("SYNTHREFTEST %s: %d failure(s)%s\n", failures ? "FAIL" : "all checks", failures, failures ? "" : " OK");
   std::fflush(stdout);
   return failures ? 1 : 0;
}
