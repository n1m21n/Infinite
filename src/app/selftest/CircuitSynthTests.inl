// Part of src/main.cpp, included there in order (one translation unit).
//
// Turbo 0.73: INFINITE_CIRCUITSYNTHTEST - headless checks of the Circuit
// Synth (no audio device, no window). Every line ends "ok" or contains FAIL;
// the last line is the verdict. The numbers printed are the measurements, so
// a run doubles as the physics / stereo / CPU report (docs/CIRCUIT-SYNTH.md),
// and it renders one WAV per preset to build\circuit-synth-wavs for listening.
namespace CircuitSynthTest
{
   using namespace CircuitDsp;

   int failures = 0;

   void Check(bool ok, const char* what, const char* detail = "")
   {
      std::printf("CIRCUITSYNTHTEST %-34s %s %s\n", what, detail, ok ? "ok" : "FAIL");
      std::fflush(stdout);
      if (!ok)
         ++failures;
   }

   // A netlist by hand, for the physics checks.
   struct Net
   {
      Preset p;
      int Node() { return ++p.numNodes; }
      int Add(PartKind kind, int a, int b, double value, double p1 = 0.0)
      {
         PartDef& d = p.parts[p.numParts];
         d.kind = kind;
         d.a = (int8_t)a;
         d.b = (int8_t)b;
         d.value = value;
         d.p1 = p1;
         return p.numParts++;
      }
   };

   // An engine is ~175 KB (8 voices x 2 circuits): never on the stack.
   std::unique_ptr<Engine> NewEngine()
   {
      auto e = std::make_unique<Engine>();
      e->Prepare(48000.0);
      return e;
   }

   Params Defaults(int preset)
   {
      const Preset& pr = GetPreset(preset);
      Params p;
      p.preset = preset;
      p.wave = pr.wave;
      p.trigger = pr.trigger;
      p.cFollow = pr.cFollow;
      p.lFollow = pr.lFollow;
      p.contactMs = pr.contactMs;
      p.attack = pr.attack;
      p.release = pr.release;
      p.spread = pr.spread;
      p.probeA = pr.probeA;
      p.probeB = pr.probeB;
      return p;
   }

   struct Ev
   {
      double t;
      int note;
      bool on;
   };

   // The engine alone, 16-frame chunks like the node.
   double RenderEngine(Engine& e, const Params& p, const std::vector<Ev>& evs, double secs, std::vector<float>& L,
                       std::vector<float>& R)
   {
      const int sr = 48000, chunk = 16;
      const long frames = (long)(secs * sr);
      L.assign((size_t)frames, 0.0f);
      R.assign((size_t)frames, 0.0f);
      size_t ei = 0;
      e.Apply(p);
      const auto t0 = std::chrono::steady_clock::now();
      for (long f = 0; f < frames; f += chunk)
      {
         while (ei < evs.size() && evs[ei].t * sr <= f)
         {
            if (evs[ei].on)
               e.NoteOn(evs[ei].note, 0.8f, (int)ei + 1);
            else
               e.NoteOff(evs[ei].note, 0);
            ei++;
         }
         e.Render(&L[(size_t)f], &R[(size_t)f], (int)std::min<long>(chunk, frames - f), p);
      }
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
   }

   // The real path: the node's AudioNode, its mailbox, a note queue with
   // events at their frame offsets, 512-frame blocks.
   void RenderNode(CircuitSynthNode& n, const std::vector<Ev>& evs, double secs, std::vector<float>& L, std::vector<float>& R)
   {
      const int sr = 48000, block = 512;
      AudioNode* a = n.GetAudioNode();
      a->PrepareToPlay(sr, block);
      NoteEventQueue queue;
      const int cursor = queue.RegisterConsumer();
      a->SetNoteInbox(&queue, cursor);
      const long frames = (long)(secs * sr);
      L.assign((size_t)frames + block, 0.0f);
      R.assign((size_t)frames + block, 0.0f);
      size_t ei = 0;
      int cook = 1;
      for (long f = 0; f < frames; f += block)
      {
         n.CookIfNeeded(cook++);
         while (ei < evs.size() && (long)(evs[ei].t * sr) < f + block)
         {
            NoteEvent e;
            e.note = evs[ei].note;
            e.isNoteOn = evs[ei].on;
            e.velocity = evs[ei].on ? 0.8f : 0.0f;
            e.frameOffset = (int)std::max<long>(0, (long)(evs[ei].t * sr) - f);
            e.voiceId = 1000 + evs[ei].note;
            queue.Push(e);
            ei++;
         }
         float* ch[2] = { &L[(size_t)f], &R[(size_t)f] };
         AudioBuffer out;
         out.channels = ch;
         out.numChannels = 2;
         out.numFrames = block;
         a->ProcessBlock(nullptr, 0, out);
      }
      L.resize((size_t)frames);
      R.resize((size_t)frames);
   }

   struct Stereo
   {
      double rmsL = 0.0, rmsR = 0.0, corr = 1.0, monoDb = 0.0, peak = 0.0;
      bool finite = true;
   };

   Stereo Measure(const std::vector<float>& L, const std::vector<float>& R, size_t from = 0)
   {
      Stereo s;
      double sl = 0.0, sr = 0.0, slr = 0.0, sm = 0.0;
      size_t n = 0;
      for (size_t i = from; i < L.size(); i++)
      {
         const double l = L[i], r = R[i];
         if (!std::isfinite(l) || !std::isfinite(r))
         {
            s.finite = false;
            continue;
         }
         sl += l * l;
         sr += r * r;
         slr += l * r;
         sm += 0.25 * (l + r) * (l + r);
         s.peak = std::max(s.peak, std::max(std::fabs(l), std::fabs(r)));
         n++;
      }
      if (n == 0)
         return s;
      s.rmsL = std::sqrt(sl / n);
      s.rmsR = std::sqrt(sr / n);
      s.corr = slr / std::sqrt(sl * sr + 1e-30);
      // The mono sum (L + R) / 2 against the sides' average level.
      s.monoDb = 20.0 * std::log10(std::sqrt(sm / n) / (0.5 * (s.rmsL + s.rmsR) + 1e-30) + 1e-30);
      return s;
   }

   // Fundamental by normalised autocorrelation over 30..2000 Hz.
   double PitchHz(const std::vector<float>& x, size_t from, size_t n, double sr)
   {
      const int minLag = (int)(sr / 2000.0), maxLag = (int)(sr / 30.0);
      if (from + n + (size_t)maxLag + 2 > x.size())
         return 0.0;
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
      double global = 0.0;
      for (int lag = minLag; lag <= maxLag; ++lag)
         global = std::max(global, c[(size_t)lag]);
      for (int lag = minLag + 1; lag <= maxLag; ++lag)
         if (c[(size_t)lag] >= 0.9 * global && c[(size_t)lag] >= c[(size_t)lag - 1] && c[(size_t)lag] >= c[(size_t)lag + 1])
         {
            const double y0 = c[(size_t)lag - 1], y1 = c[(size_t)lag], y2 = c[(size_t)lag + 1];
            const double den = y0 - 2.0 * y1 + y2;
            return sr / (lag + (std::fabs(den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0));
         }
      return 0.0;
   }

   // Frequency from upward zero crossings (linear interpolation), over a span.
   double CrossingHz(const std::vector<double>& x, double fs, size_t from)
   {
      double first = -1.0, last = -1.0;
      int count = 0;
      for (size_t i = from + 1; i < x.size(); i++)
         if (x[i - 1] < 0.0 && x[i] >= 0.0)
         {
            const double t = (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0)
               first = t;
            else
               count++;
            last = t;
         }
      return count > 0 ? fs * count / (last - first) : 0.0;
   }

   void WriteWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R)
   {
      FILE* f = std::fopen(path.c_str(), "wb");
      if (!f)
         return;
      const uint32_t data = (uint32_t)(L.size() * 8);
      uint32_t u;
      uint16_t s;
      std::fwrite("RIFF", 1, 4, f);
      u = 36 + data;
      std::fwrite(&u, 4, 1, f);
      std::fwrite("WAVEfmt ", 1, 8, f);
      u = 16;
      std::fwrite(&u, 4, 1, f);
      s = 3; // IEEE float
      std::fwrite(&s, 2, 1, f);
      s = 2;
      std::fwrite(&s, 2, 1, f);
      u = 48000;
      std::fwrite(&u, 4, 1, f);
      u = 48000 * 8;
      std::fwrite(&u, 4, 1, f);
      s = 8;
      std::fwrite(&s, 2, 1, f);
      s = 32;
      std::fwrite(&s, 2, 1, f);
      std::fwrite("data", 1, 4, f);
      std::fwrite(&data, 4, 1, f);
      for (size_t i = 0; i < L.size(); i++)
      {
         std::fwrite(&L[i], 4, 1, f);
         std::fwrite(&R[i], 4, 1, f);
      }
      std::fclose(f);
   }

   // A riff in the circuit's own register: short hits for struck circuits,
   // longer notes for the rest.
   std::vector<Ev> Riff(const Preset& pr, double spacing = 0.45)
   {
      const int base = std::clamp((int)std::lround(69.0 + 12.0 * std::log2(pr.refHz / 440.0)), 28, 62);
      static const int kSteps[8] = { 0, 0, 12, 0, 3, 0, 7, 5 };
      std::vector<Ev> evs;
      const double gate = pr.trigger == kTap ? 0.12 : 0.34;
      for (int k = 0; k < 8; k++)
      {
         evs.push_back({ k * spacing, base + kSteps[k], true });
         evs.push_back({ k * spacing + gate, base + kSteps[k], false });
      }
      return evs;
   }

   std::string WavDir()
   {
      namespace fs = std::filesystem;
      std::error_code ec;
      if (const char* env = getenv("INFINITE_CIRCUITSYNTH_WAVS"))
         return env;
      fs::path p = fs::current_path(ec);
      // The driver runs the exe from build\windows-vs2022\<Config>.
      if (p.filename() == "Release" || p.filename() == "Debug" || p.filename() == "RelWithDebInfo")
         p = p.parent_path().parent_path();
      p /= "circuit-synth-wavs";
      fs::create_directories(p, ec);
      return p.u8string();
   }
}

static int RunCircuitSynthTest()
{
   using namespace CircuitSynthTest;
   char buf[256];
   const double fs = 192000.0;

   // 1. An LC tank rings at 1 / (2 pi sqrt(LC)). Charge the coil through a
   // contact, let go, count zero crossings for half a second.
   {
      Net net;
      const int n1 = net.Node(), n2 = net.Node(), n3 = net.Node();
      net.Add(kGenerator, n1, 0, 1.0, 1.0);
      net.Add(kResistor, n1, n2, 1000.0);
      net.Add(kCapacitor, n2, 0, 1e-6);
      net.Add(kInductor, n2, n3, 0.25);
      net.Add(kResistor, n3, 0, 0.01);
      Circuit c;
      c.Load(net.p);
      c.dt = 1.0 / fs;
      std::vector<double> v;
      for (int i = 0; i < (int)(0.6 * fs); i++)
      {
         c.SetContact(i < (int)(0.002 * fs) ? 1.0 : 0.0);
         c.Step(1.0);
         v.push_back(c.x[n2]);
      }
      const double want = 1.0 / (2.0 * 3.14159265358979 * std::sqrt(0.25 * 1e-6));
      const double got = CrossingHz(v, fs, (size_t)(0.01 * fs));
      snprintf(buf, sizeof(buf), "(%.3f Hz, theory %.3f, %+.4f %%)", got, want, 100.0 * (got / want - 1.0));
      Check(std::fabs(got / want - 1.0) < 0.01, "LC tank frequency", buf);
   }

   // 2. RC lowpass: -3.01 dB and -45 degrees at 1 / (2 pi RC).
   {
      Net net;
      const int n1 = net.Node(), n2 = net.Node();
      net.Add(kGenerator, n1, 0, 1.0, 1e-3);
      net.Add(kResistor, n1, n2, 10000.0);
      const double C = 1.0 / (2.0 * 3.14159265358979 * 10000.0 * 1000.0);
      net.Add(kCapacitor, n2, 0, C);
      Circuit c;
      c.Load(net.p);
      c.dt = 1.0 / fs;
      c.SetContact(1.0);
      double sumSin = 0.0, sumCos = 0.0;
      const int settle = (int)(0.02 * fs), span = (int)(0.05 * fs); // whole periods of 1 kHz
      for (int i = 0; i < settle + span; i++)
      {
         const double ph = 2.0 * 3.14159265358979 * 1000.0 * i / fs;
         c.Step(std::sin(ph));
         if (i >= settle)
         {
            sumSin += c.x[n2] * std::sin(ph);
            sumCos += c.x[n2] * std::cos(ph);
         }
      }
      const double gainDb = 20.0 * std::log10(2.0 * std::sqrt(sumSin * sumSin + sumCos * sumCos) / span);
      const double phase = std::atan2(sumCos, sumSin) * 180.0 / 3.14159265358979;
      snprintf(buf, sizeof(buf), "(%.3f dB, %.2f deg at 1 kHz)", gainDb, phase);
      Check(std::fabs(gainDb + 3.0103) < 0.05 && std::fabs(phase + 45.0) < 0.5, "RC corner 1/(2 pi RC)", buf);
   }

   // 3. Diode clipper: a 10 V saw through 2.2k into antiparallel diodes
   // stays near one diode drop.
   {
      Net net;
      const int n1 = net.Node(), n2 = net.Node();
      net.Add(kGenerator, n1, 0, 10.0, 1.0);
      net.Add(kResistor, n1, n2, 2200.0);
      net.Add(kDiode, n2, 0, 2.5e-9, 1.75);
      net.Add(kDiode, 0, n2, 2.5e-9, 1.75);
      net.Add(kCapacitor, n2, 0, 56e-9);
      Circuit c;
      c.Load(net.p);
      c.dt = 1.0 / fs;
      c.SetContact(1.0);
      double vmax = 0.0, vmin = 0.0;
      double ph = 0.0;
      for (int i = 0; i < (int)(0.1 * fs); i++)
      {
         ph += 110.0 / fs;
         ph -= std::floor(ph);
         c.Step(2.0 * ph - 1.0);
         if (i > (int)(0.01 * fs))
         {
            vmax = std::max(vmax, c.x[n2]);
            vmin = std::min(vmin, c.x[n2]);
         }
      }
      snprintf(buf, sizeof(buf), "(%+.3f / %+.3f V, source +/-10 V)", vmax, vmin);
      Check(vmax > 0.45 && vmax < 0.9 && vmin < -0.45 && vmin > -0.9, "diode clipper bounded", buf);
   }

   // 4. Van der Pol self-oscillates: a held note, the generator only a DC
   // kick through 47k, still a steady tone at the note after a second.
   {
      const int vdp = FindPreset("Van der Pol");
      auto e = NewEngine();
      std::vector<float> L, R;
      RenderEngine(*e, Defaults(vdp), { { 0.0, 57, true } }, 1.6, L, R);
      const Stereo s = Measure(L, R, 48000);
      const double hz = PitchHz(L, 52800, 9600, 48000.0);
      snprintf(buf, sizeof(buf), "(rms %.3f, %.1f Hz for 220)", s.rmsL, hz);
      Check(s.finite && s.rmsL > 0.05 && std::fabs(hz / 220.0 - 1.0) < 0.03, "van der Pol self-oscillates", buf);
   }

   // 5. Key tracking: the circuit's pitch follows the note (C and L scale).
   {
      const char* tonal[] = { "Diode Clipper", "RC Lowpass", "RLC Lowpass", "LC Ladder", "LC Tank", "Van der Pol", "Neon Relax" };
      for (const char* name : tonal)
      {
         const int pi = FindPreset(name);
         double worst = 0.0;
         std::string got;
         for (int note : { 45, 57, 69 })
         {
            auto e = NewEngine();
            std::vector<float> L, R;
            RenderEngine(*e, Defaults(pi), { { 0.0, note, true } }, 0.6, L, R);
            const double want = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const double hz = PitchHz(L, 4800, 9600, 48000.0);
            worst = std::max(worst, std::fabs(hz / want - 1.0));
            char one[32];
            snprintf(one, sizeof(one), " %.1f", hz);
            got += one;
         }
         char what[64];
         snprintf(what, sizeof(what), "[%s] tracks the key", name);
         snprintf(buf, sizeof(buf), "(Hz for 110/220/440:%s)", got.c_str());
         Check(worst < 0.02, what, buf);
      }
   }

   // 6. Every preset, parts swept live: knobs random-walk and jump, spread,
   // pitch and the LFO move, three poly notes held. No NaN, no blow-up, no
   // denormal left in the companion states.
   {
      uint32_t seed = 12345u;
      auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
      int bad = 0;
      std::string badNames;
      for (int pi = 0; pi < NumPresets(); pi++)
      {
         auto e = NewEngine();
         Params p = Defaults(pi);
         p.mode = kPoly;
         p.lfoDepth = 0.5f;
         p.lfoRate = 3.0f;
         e->Apply(p);
         e->NoteOn(48, 0.9f, 1);
         e->NoteOn(55, 0.7f, 2);
         e->NoteOn(60, 0.5f, 3);
         float L[16], R[16];
         bool finite = true;
         for (int chunk = 0; chunk < 48000 * 2 / 16; chunk++)
         {
            for (float& k : p.knob)
               k = std::clamp(k + (rnd() - 0.5f) * 0.08f, -1.0f, 1.0f);
            if (chunk % 600 == 0)
               for (float& k : p.knob)
                  k = rnd() * 2.0f - 1.0f;
            p.spread = 2.5f + 2.5f * std::sin(chunk * 0.003f);
            p.pitch = 12.0f * std::sin(chunk * 0.002f);
            p.lfoTarget = (chunk / 1500) % kNumLfoTargets;
            if (chunk == 4500)
               e->AllNotesOff();
            e->Render(L, R, 16, p);
            for (int i = 0; i < 16; i++)
               if (!std::isfinite(L[i]) || !std::isfinite(R[i]))
                  finite = false;
         }
         int denormals = 0;
         for (int vi = 0; vi < kMaxVoices; vi++)
            for (int t = 0; t < 2; t++)
            {
               const Circuit& c = e->VoiceCircuit(vi, t);
               for (int k = 0; k < c.numParts; k++)
                  for (double s : { c.parts[k].v, c.parts[k].i })
                     if (!std::isfinite(s) || (s != 0.0 && std::fabs(s) < 1e-30))
                        denormals++;
            }
         if (!finite || denormals > 0)
         {
            bad++;
            badNames += std::string(" ") + GetPreset(pi).name;
         }
      }
      snprintf(buf, sizeof(buf), "(%d presets, %d bad%s)", NumPresets(), bad, badNames.c_str());
      Check(bad == 0, "parts swept live: stable", buf);
   }

   // 7. Stereo, the owner's requirement: the default preset (a riff and a
   // 4 s note) clearly decorrelated (< 0.9) and its mono sum never more than
   // 3 dB under the sides. The table shows every preset at its defaults.
   {
      const int def = DefaultPreset();
      std::vector<float> L, R;
      auto e = NewEngine();
      RenderEngine(*e, Defaults(def), Riff(GetPreset(def)), 4.0, L, R);
      const Stereo riff = Measure(L, R);
      auto e2 = NewEngine();
      RenderEngine(*e2, Defaults(def), { { 0.0, 45, true } }, 4.0, L, R);
      const Stereo held = Measure(L, R, 4800);
      Params mono = Defaults(def);
      mono.spread = 0.0f;
      mono.probeB = mono.probeA;
      auto e3 = NewEngine();
      RenderEngine(*e3, mono, Riff(GetPreset(def)), 2.0, L, R);
      const Stereo zero = Measure(L, R);
      snprintf(buf, sizeof(buf), "(riff corr %.2f mono %+.2f dB; 4 s note corr %.2f mono %+.2f dB)", riff.corr, riff.monoDb,
               held.corr, held.monoDb);
      Check(riff.corr < 0.9 && held.corr < 0.9 && riff.monoDb > -3.0 && held.monoDb > -3.0, "default preset is stereo", buf);
      snprintf(buf, sizeof(buf), "(corr %.4f)", zero.corr);
      Check(zero.corr > 0.9999, "spread 0, same probe: mono", buf);
      std::printf("CIRCUITSYNTHTEST stereo table (riff at the preset's defaults):\n");
      for (int pi = 0; pi < NumPresets(); pi++)
      {
         auto ep = NewEngine();
         RenderEngine(*ep, Defaults(pi), Riff(GetPreset(pi)), 3.8, L, R);
         const Stereo s = Measure(L, R);
         std::printf("CIRCUITSYNTHTEST    %-14s corr %5.2f  mono %+5.2f dB  rms %.3f / %.3f  peak %.2f\n", GetPreset(pi).name,
                     s.corr, s.monoDb, s.rmsL, s.rmsR, s.peak);
      }
   }

   // 8. CPU: one held voice (both twins) per preset, and the poly cap.
   {
      std::printf("CIRCUITSYNTHTEST cpu per voice (%% of one core) x poly cap:\n");
      double worstPoly = 0.0;
      for (int pi = 0; pi < NumPresets(); pi++)
      {
         auto e = NewEngine();
         std::vector<float> L, R;
         const double seconds = RenderEngine(*e, Defaults(pi), { { 0.0, 57, true } }, 2.0, L, R);
         const double percent = 100.0 * seconds / 2.0;
         worstPoly = std::max(worstPoly, percent * e->MaxVoices());
         std::printf("CIRCUITSYNTHTEST    %-14s %5.2f %% x %d  (%dx oversampled)\n", GetPreset(pi).name, percent, e->MaxVoices(),
                     e->Oversampling());
      }
      snprintf(buf, sizeof(buf), "(worst preset at its voice cap: %.1f %% of a core)", worstPoly);
      Check(worstPoly < 60.0, "poly voices capped by CPU", buf);
   }

   // 8b. The output filter (the shared SynthModes types): lp 24 at 400 Hz
   // takes most of the clipper's upper harmonics away (energy of the first
   // difference, a high-frequency measure), hp 24 the fundamental's share.
   {
      auto hfRatio = [&](int type, float cutoff)
      {
         auto e = NewEngine();
         Params p = Defaults(DefaultPreset());
         p.filterType = type;
         p.cutoff = cutoff;
         std::vector<float> L, R;
         RenderEngine(*e, p, { { 0.0, 45, true } }, 0.8, L, R);
         double all = 0.0, hf = 0.0;
         for (size_t i = 9600; i < L.size(); i++)
         {
            all += (double)L[i] * L[i];
            hf += (double)(L[i] - L[i - 1]) * (L[i] - L[i - 1]);
         }
         return hf / (all + 1e-30);
      };
      const double off = hfRatio(SynthModes::kFilterOff, 8000.0f);
      const double lp = hfRatio(SynthModes::kFilterLP24, 400.0f);
      const double hp = hfRatio(SynthModes::kFilterHP24, 2000.0f);
      snprintf(buf, sizeof(buf), "(hf share off %.4f, lp24 400 Hz %.4f, hp24 2 kHz %.4f)", off, lp, hp);
      Check(lp < 0.3 * off && hp > 2.0 * off, "output filter lp / hp", buf);
   }

   // 8c. Every schematic places each node on its own grid point.
   {
      int clashes = 0;
      std::string where;
      for (int pi = 0; pi < NumPresets(); pi++)
      {
         const CircuitDsp::Preset& pr = GetPreset(pi);
         for (int i = 1; i <= pr.numNodes; i++)
            for (int j = i + 1; j <= pr.numNodes; j++)
               if (std::fabs(pr.nodeX[i] - pr.nodeX[j]) < 0.05f && std::fabs(pr.nodeY[i] - pr.nodeY[j]) < 0.05f)
               {
                  clashes++;
                  where += std::string(" ") + pr.name;
               }
      }
      snprintf(buf, sizeof(buf), "(%d clashes%s)", clashes, where.c_str());
      Check(clashes == 0, "schematic layouts", buf);
   }

   // 9. The node path: params through the mailbox, a note at frame 300 of
   // the first block sounds from frame 300, not from the block start.
   {
      CircuitSynthNode n;
      std::vector<float> L, R;
      RenderNode(n, { { 300.0 / 48000.0, 57, true }, { 0.5, 57, false } }, 0.8, L, R);
      double before = 0.0, after = 0.0;
      for (int i = 0; i < 300; i++)
         before = std::max(before, (double)std::fabs(L[(size_t)i]));
      for (int i = 300; i < 1500; i++)
         after = std::max(after, (double)std::fabs(L[(size_t)i]));
      const Stereo s = Measure(L, R);
      snprintf(buf, sizeof(buf), "(peak before %.1e, after %.3f, corr %.2f)", before, after, s.corr);
      Check(before < 1e-6 && after > 0.02 && s.finite, "node: note lands on its frame", buf);
   }

   // 10. A WAV per preset through the node path, for listening.
   {
      const std::string dir = CircuitSynthTest::WavDir();
      int written = 0;
      for (int pi = 0; pi < NumPresets(); pi++)
      {
         CircuitSynthNode n;
         n.ApplyPreset(pi);
         std::vector<float> L, R;
         RenderNode(n, Riff(GetPreset(pi)), 4.6, L, R);
         std::string name = GetPreset(pi).name;
         for (char& c : name)
            if (c == ' ' || c == '-')
               c = '_';
         char file[64];
         snprintf(file, sizeof(file), "%02d_%s.wav", pi, name.c_str());
         WriteWav(dir + "/" + file, L, R);
         written++;
      }
      {
         // Poly: chords, voices panned across the field.
         CircuitSynthNode n;
         n.ApplyPreset(FindPreset("Tank Filter"));
         n.mode = kPoly;
         n.release = 1.2f;
         std::vector<Ev> evs;
         const int chords[3][3] = { { 45, 52, 57 }, { 43, 50, 55 }, { 41, 48, 57 } };
         for (int c = 0; c < 3; c++)
            for (int k = 0; k < 3; k++)
            {
               evs.push_back({ c * 1.4 + k * 0.05, chords[c][k], true });
               evs.push_back({ c * 1.4 + 1.1, chords[c][k], false });
            }
         std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
         std::vector<float> L, R;
         RenderNode(n, evs, 5.0, L, R);
         WriteWav(dir + "/poly_chords_Tank_Filter.wav", L, R);
         written++;
      }
      snprintf(buf, sizeof(buf), "(%d files in %s)", written, dir.c_str());
      Check(written == NumPresets() + 1, "WAVs rendered", buf);
   }

   std::printf("CIRCUITSYNTHTEST %s\n", failures == 0 ? "OK" : "FAIL");
   return 0;
}
