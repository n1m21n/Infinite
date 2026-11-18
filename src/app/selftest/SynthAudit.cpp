// INFINITE_SYNTHAUDIT: a measurement audit of the pitched free-running synths.
//
// The oscillator twin of FXAUDIT and MODAUDIT (docs/plans/node-audits/README.md,
// block 1). Each synth runs through its real node -> audio node ProcessBlock at
// 256-frame blocks, free-running (no note cable) at a set frequency, and gets
// the same battery of numbers:
//
//   PITCH   fundamental error in cents (parabolic peak of a Blackman-Harris
//           FFT) at 110 / 440 / 1760 / 7040 Hz, at 44.1 / 48 / 96 kHz
//   ALIAS   energy at non-harmonic bins below 20 kHz re the fundamental, dB,
//           at 1.75 kHz and 5 kHz (the metric of ALIASTEST)
//   LEVEL   RMS and peak (dBFS) at defaults, 440 Hz
//   RATE    RMS spread (dB) of the 440 Hz level across the three rates
//   DET     two fresh nodes, same settings: bit-identical output
//   CPU     ns per stereo frame at defaults
//   UNISON  4 voices at 100 cents total detune: spectral peak count and outer
//           spread in cents (Analog, Wavetable)
//   PARAM   worst second difference within 50 ms of an instant volume step,
//           dB re steady state (zipper; FXAUDIT threshold 12 dB)
//
// and, driven through a real NoteEventQueue (note cable) at defaults:
//
//   NOTE    note 69 pitch error in cents, quiet and at true defaults (drift on)
//   ENV     attack (10..90% of the held peak) and release (note-off to -60 dB
//           re the held peak), ms
//   CLICK   worst second difference within 5 ms of note-on and of note-off,
//           dB re the steady-state worst (note-on: re the larger of steady
//           state and the note's 5-50 ms body) (FXAUDIT threshold 12 dB)
//   VEL     RMS at velocity 0.25 / 0.5 re velocity 1, dB
//   SAMPLE  sample synths (fixture WAV loaded): pitch and length of a held
//           note at 44.1 / 48 / 96 kHz, spread across the rates
//   POLY    1 / 4 / 8 / 32 simultaneous notes (flag: 4-note chord over 0 dBFS): peak, ns per frame, then all-off
//           and the level of the last 0.5 s of 10 s (stuck voices; -100 dBFS)
//
// Only hard invariants fail the run (non-finite output, silence at defaults).
// Everything else is reported, with FLAG lines past the thresholds below.
#include "app/AppShared.h"
#include "audio/AudioBuffer.h"
#include "audio/NoteEventQueue.h"
#include "audio/dsp/PortableFft.h"
#include "nodes/AnalogNode.h"
#include "nodes/DrumSequencerNode.h"
#include "nodes/EquationNode.h"
#include "nodes/FieldSynthNode.h"
#include "nodes/GrainMolderNode.h"
#include "nodes/GranularNode.h"
#include "nodes/ImageSpectralSynthNode.h"
#include "nodes/LooperNode.h"
#include "nodes/MolderNode.h"
#include "nodes/MpcNode.h"
#include "nodes/PaulStretchNode.h"
#include "nodes/SamplerNode.h"
#include "nodes/SlicerNode.h"
#include "nodes/WaveTerrainNode.h"
#include "nodes/MetallicNode.h"
#include "nodes/OscillatorNode.h"
#include "nodes/WavetableNode.h"

#include <chrono>
#include <fstream>
#include <thread>

namespace app
{
namespace
{
   constexpr int kBlock = 256;
   constexpr int kLog2N = 16;
   constexpr int kN = 1 << kLog2N;
   constexpr int kLobe = 4;

   constexpr double kFlagPitchCents = 1.0;
   constexpr double kFlagSampleCents = 20.0; // the smallest real rate bug (44.1 vs 48 kHz) is 147 c; PaulStretch phase jitter reads ~7 c
   constexpr double kFlagAliasDb = -80.0; // was -60 until F2 (2026-10-13): every synth now reads -82 or lower
   constexpr double kFlagPeakDbfs = 0.0;
   constexpr double kFlagRateDb = 1.0;
   constexpr double kFlagCpuNs = 4000.0;
   constexpr double kFlagClickDb = 12.0;
   constexpr double kFlagTailDbfs = -100.0;

   struct Synth
   {
      const char* name;
      bool pitched;                          // false: inharmonic, PITCH/ALIAS skipped
      std::function<INode*()> create;
      std::function<void(INode*, float)> setFreq;
      std::function<void(INode*)> quiet;     // analog drift / noise off, deterministic
      std::function<void(INode*, int, float)> setUnison; // voices, total detune cents (optional)
      std::function<void(INode*, float)> setVolume;      // optional
      int note = 69;                         // the note that sounds it (Slicer/MPC: slice/pad 0)
      const char* noNotes = nullptr;         // why the note audit does not apply (no note path)
      bool fixture = false;                  // plays FixtureWav: SAMPLE checks playback rate
   };

   // The fixture sample the sample-based synths are loaded with: 2 s of a
   // steady 440 Hz tone (harmonics 1..6 at 1/n) with 5 ms fades at each end,
   // 16-bit mono 48 kHz, decoded through the real file path. Looper records
   // its input and has no load path, so it stays silent here by design.
   const std::string& FixtureWav()
   {
      static const std::string path = [] {
         const std::string p = TmpPath("infinite_synthaudit_fixture.wav");
         const int sr = 48000, n = 2 * sr;
         std::vector<int16_t> pcm((size_t)n);
         for (int i = 0; i < n; i++)
         {
            const double t = (double)i / sr;
            double v = 0.0;
            for (int h = 1; h <= 6; h++)
               v += std::sin(2.0 * M_PI * 440.0 * h * t) / h;
            const double env = std::min({ 1.0, t / 0.005, (2.0 - t) / 0.005 });
            pcm[(size_t)i] = (int16_t)std::lround(0.5 * env * v / 2.45 * 32767.0);
         }
         std::ofstream f(p, std::ios::binary);
         auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
         auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
         const uint32_t bytes = (uint32_t)(pcm.size() * sizeof(int16_t));
         f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
         f.write("fmt ", 4); u32(16); u16(1); u16(1); u32((uint32_t)sr); u32((uint32_t)sr * 2); u16(2); u16(16);
         f.write("data", 4); u32(bytes);
         f.write((const char*)pcm.data(), bytes);
         return p;
      }();
      return path;
   }

   // Molder and Grain Molder render the loaded file on a worker; the result is
   // adopted on the next cook. Bounded wait.
   template <class T> INode* Settled(T* n)
   {
      for (int i = 0; i < 15000 && n->IsRendering(); i++)
         std::this_thread::sleep_for(std::chrono::milliseconds(2));
      return n;
   }

   template <class T> T* Make(INode* (*create)()) { return static_cast<T*>(create()); }

   const std::vector<Synth>& Synths()
   {
      static const std::vector<Synth> s = {
         { "Analog", true, [] { return AnalogNode::Create(); },
           [](INode* n, float f) { static_cast<AnalogNode*>(n)->freq = f; },
           [](INode* n) {
              auto* a = static_cast<AnalogNode*>(n);
              a->analog = false;
              a->filterType = kAFilterOff;
              a->voices = 1.0f;
              a->oscMix = 0.0f; // osc 1 only, as ALIASTEST
              a->sub = 0.0f;
              a->noise = 0.0f;
           },
           [](INode* n, int v, float c) { auto* a = static_cast<AnalogNode*>(n); a->voices = (float)v; a->detune = c; },
           [](INode* n, float v) { static_cast<AnalogNode*>(n)->volume = v; } },
         { "Wavetable", true, [] { return WavetableNode::Create(); },
           [](INode* n, float f) { static_cast<WavetableNode*>(n)->frequency = f; }, [](INode*) {},
           [](INode* n, int v, float c) {
              auto* w = static_cast<WavetableNode*>(n);
              w->engines[1].on = false;
              w->mix = 0.0f;
              w->engines[0].unison = v;
              w->engines[0].detune = c;
           },
           [](INode* n, float v) { static_cast<WavetableNode*>(n)->volume = v; } },
         { "Oscillator", true, [] { return OscillatorNode::Create(); },
           [](INode* n, float f) { static_cast<OscillatorNode*>(n)->frequency = f; }, [](INode*) {} },
         { "Equation Synth", true, [] { return EquationNode::Create(); },
           [](INode* n, float f) { static_cast<EquationNode*>(n)->frequency = f; }, [](INode*) {}, nullptr,
           [](INode* n, float v) { static_cast<EquationNode*>(n)->volume = v; } },
         { "Metallic", false, [] { return MetallicNode::Create(); },
           [](INode* n, float f) { static_cast<MetallicNode*>(n)->frequency = f; }, [](INode*) {} },
         // Not pitch-driven: level, rate, determinism, CPU, notes. The sample synths
         // get the fixture sample (FixtureWav) loaded the way a file pick does.
         { "Field Synth", false, [] { return FieldSynthNode::Create(); }, [](INode*, float) {}, [](INode*) {} },
         { "Wave Terrain", false, [] { return WaveTerrainNode::Create(); }, [](INode*, float) {}, [](INode*) {} },
         { "Spectral Synth", false, [] { return ImageSpectralSynthNode::Create(); }, [](INode*, float) {}, [](INode*) {} },
         { "Grain Molder", false, [] { auto* n = Make<GrainMolderNode>(GrainMolderNode::Create); n->LoadFile(FixtureWav()); return Settled(n); }, [](INode*, float) {}, [](INode*) {}, nullptr, nullptr, 69, nullptr, true },
         { "Molder", false, [] { auto* n = Make<MolderNode>(MolderNode::Create); n->LoadFile(FixtureWav()); return Settled(n); }, [](INode*, float) {}, [](INode*) {}, nullptr, nullptr, 69, nullptr, true },
         { "Granular", false, [] { auto* n = Make<GranularNode>(GranularNode::Create); n->LoadFile(FixtureWav()); return (INode*)n; }, [](INode*, float) {},
           [](INode* n) {
              // A coherent cloud: grains follow a real-time scan, no spray, so the
              // fixture's pitch is measurable (defaults are a random cloud).
              auto* g = static_cast<GranularNode*>(n);
              g->scan = 1.0f;
              g->density = 100.0f;
              g->randomPos = 0.0f;
              g->randomLength = 0.0f;
              g->randomPan = 0.0f;
           }, nullptr, nullptr, 69, nullptr, true },
         { "Sampler", false, [] { auto* n = Make<SamplerNode>(SamplerNode::Create); n->LoadFile(FixtureWav()); return (INode*)n; }, [](INode*, float) {}, [](INode*) {}, nullptr, nullptr, 69, nullptr, true },
         { "Slicer", false, [] { auto* n = Make<SlicerNode>(SlicerNode::Create); n->LoadFile(FixtureWav()); return (INode*)n; }, [](INode*, float) {}, [](INode*) {}, nullptr, nullptr, 36, nullptr, true },
         { "MPC", false, [] { auto* n = Make<MpcNode>(MpcNode::Create); n->LoadPad(0, FixtureWav()); return (INode*)n; }, [](INode*, float) {}, [](INode*) {}, nullptr, nullptr, 36, nullptr, true },
         { "Looper", false, [] { return LooperNode::Create(); }, [](INode*, float) {}, [](INode*) {}, nullptr, nullptr, 69,
           "records its live input, no fixture to load" },
         { "PaulStretch", false, [] { auto* n = Make<PaulStretchNode>(PaulStretchNode::Create); n->LoadFile(FixtureWav()); return (INode*)n; }, [](INode*, float) {}, [](INode*) {}, nullptr, nullptr, 69, nullptr, true },
         { "Drum Sequencer", false, [] { auto* n = Make<DrumSequencerNode>(DrumSequencerNode::Create); n->LoadFileToLane(0, FixtureWav()); return (INode*)n; }, [](INode*, float) {}, [](INode*) {}, nullptr, nullptr, 69,
           "plays its step grid from the transport, no note input (DRUMSEQFIXTURE covers it)", true },
      };
      return s;
   }

   IAudioSource* AsSource(INode* n) { return dynamic_cast<IAudioSource*>(n); }

   // Renders `frames` of the left channel after a 0.3 s warm-up.
   bool Render(const Synth& s, double sr, float freq, int frames, std::vector<float>& out, bool quiet = true)
   {
      std::unique_ptr<INode> node(s.create());
      IAudioSource* src = AsSource(node.get());
      if (src == nullptr)
         return false;
      if (quiet)
         s.quiet(node.get());
      s.setFreq(node.get(), freq);
      AudioNode* an = src->GetAudioNode();
      an->PrepareToPlay(sr, kBlock);
      node->CookIfNeeded(1);
      std::vector<float> l(kBlock), r(kBlock);
      float* ch[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = ch;
      buf.numChannels = 2;
      buf.numFrames = kBlock;
      const int warm = (int)(0.3 * sr);
      out.clear();
      int done = 0;
      while ((int)out.size() < frames)
      {
         node->CookIfNeeded(2 + done / kBlock);
         an->ProcessBlock(nullptr, 0, buf);
         if (done >= warm)
            out.insert(out.end(), l.begin(), l.end());
         done += kBlock;
      }
      out.resize((size_t)frames);
      return true;
   }

   bool AllFinite(const std::vector<float>& x)
   {
      for (float v : x)
         if (!std::isfinite(v))
            return false;
      return true;
   }

   double Db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

   double Rms(const std::vector<float>& x)
   {
      double e = 0.0;
      for (float v : x)
         e += (double)v * v;
      return std::sqrt(e / (double)std::max<size_t>(x.size(), 1));
   }

   double Peak(const std::vector<float>& x)
   {
      double p = 0.0;
      for (float v : x)
         p = std::max(p, (double)std::fabs(v));
      return p;
   }

   std::vector<double> Spectrum(const std::vector<float>& x)
   {
      std::vector<float> w((size_t)kN);
      for (int i = 0; i < kN; i++)
      {
         const double t = 2.0 * M_PI * i / kN;
         w[(size_t)i] = (float)(x[(size_t)i] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t)));
      }
      PortableFft::RealFft fft;
      fft.Prepare(kLog2N);
      std::vector<float> re((size_t)kN / 2), im((size_t)kN / 2);
      fft.Forward(w.data(), kLog2N, re.data(), im.data());
      std::vector<double> p((size_t)kN / 2);
      for (int k = 0; k < kN / 2; k++)
         p[(size_t)k] = (double)re[(size_t)k] * re[(size_t)k] + (double)im[(size_t)k] * im[(size_t)k];
      return p;
   }

   // Parabolic-interpolated peak near `hz`, returned in Hz.
   double PeakHz(const std::vector<double>& p, double hz, double sr)
   {
      const double bin = sr / kN;
      const int c = (int)std::lround(hz / bin);
      int best = c;
      for (int k = std::max(c - 3, 2); k <= std::min(c + 3, kN / 2 - 2); k++)
         if (p[(size_t)k] > p[(size_t)best])
            best = k;
      const double a = std::log(std::max(p[(size_t)best - 1], 1e-30));
      const double b = std::log(std::max(p[(size_t)best], 1e-30));
      const double g = std::log(std::max(p[(size_t)best + 1], 1e-30));
      const double d = 0.5 * (a - g) / (a - 2 * b + g);
      return (best + d) * bin;
   }

   // Non-harmonic energy below 20 kHz re the fundamental (dB).
   double AliasDb(const std::vector<double>& p, double hz, double sr)
   {
      const double bin = sr / kN;
      std::vector<char> harm((size_t)kN / 2, 0);
      for (int k = 0; k <= kLobe + 2; k++)
         harm[(size_t)k] = 1;
      for (int h = 1; h * hz < sr * 0.5 + 10 * bin; h++)
         for (int d = -kLobe - 1; d <= kLobe + 1; d++)
         {
            const int k = (int)std::lround(h * hz / bin) + d;
            if (k > 0 && k < kN / 2)
               harm[(size_t)k] = 1;
         }
      const int hi = std::min((int)(20000.0 / bin), kN / 2 - 1);
      double fund = 0.0, alias = 0.0;
      const int fk = (int)std::lround(hz / bin);
      for (int k = fk - kLobe; k <= fk + kLobe; k++)
         fund += p[(size_t)k];
      for (int k = 1; k <= hi; k++)
         if (!harm[(size_t)k])
            alias += p[(size_t)k];
      return 10.0 * std::log10(std::max(alias, 1e-30) / std::max(fund, 1e-30));
   }

   // One measured value for the quality system (docs/quality/README.md): tools/quality/quality.py
   // reads these rows, keeps the worst per metric and node, and owns the targets and the release
   // gate. The FLAG lines stay as this audit's own pointers to what to look at.
   void Quality(const char* id, const char* node, double v)
   {
      printf("QUALITY id=%s subject=\"node:%s\" value=%.4f\n", id, node, v);
   }

   struct Flags
   {
      int n = 0;
      void Flag(const char* sec, const char* node, const char* what, double v)
      {
         printf("SYNTHAUDIT FLAG %-5s %-15s %s %.2f\n", sec, node, what, v);
         n++;
      }
   };

   // A synth wired to a NoteEventQueue the way the topology builder does.
   struct NoteRig
   {
      std::unique_ptr<INode> node;
      AudioNode* audio = nullptr;
      NoteEventQueue queue;
      double sr = 48000.0;
      int frame = 1;
      std::vector<float> l, r;
      std::vector<int> ids;

      bool Init(const Synth& s, double rate, bool quiet = true)
      {
         sr = rate;
         node.reset(s.create());
         IAudioSource* src = AsSource(node.get());
         if (src == nullptr)
            return false;
         if (quiet)
            s.quiet(node.get());
         audio = src->GetAudioNode();
         audio->PrepareToPlay(sr, kBlock);
         node->CookIfNeeded(frame++);
         const int cursor = queue.RegisterConsumer();
         int slot = 0;
         for (int i = 0; i < 8; i++)
            if (node->NoteInputSlot(i) != nullptr)
            {
               slot = i;
               break;
            }
         audio->SetNoteInbox(slot, &queue, cursor);
         l.assign(kBlock, 0.0f);
         r.assign(kBlock, 0.0f);
         return true;
      }
      int On(int note, float vel)
      {
         NoteEvent e;
         e.note = note;
         e.velocity = vel;
         e.isNoteOn = true;
         e.voiceId = NextVoiceId();
         queue.Push(e);
         return e.voiceId;
      }
      void Off(int note, int id)
      {
         NoteEvent e;
         e.note = note;
         e.isNoteOn = false;
         e.voiceId = id;
         queue.Push(e);
      }
      // Appends `frames` of the left channel (rounded up to whole blocks).
      void Run(int frames, std::vector<float>& out)
      {
         float* ch[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = kBlock;
         for (int done = 0; done < frames; done += kBlock)
         {
            node->CookIfNeeded(frame++);
            audio->ProcessBlock(nullptr, 0, buf);
            out.insert(out.end(), l.begin(), l.end());
         }
      }
   };

   // Peak of |x| over 1 ms windows.
   std::vector<double> Envelope(const std::vector<float>& x, double sr)
   {
      const size_t w = std::max<size_t>(1, (size_t)(sr * 0.001));
      std::vector<double> e;
      for (size_t i = 0; i + w <= x.size(); i += w)
      {
         double p = 0.0;
         for (size_t j = 0; j < w; j++)
            p = std::max(p, (double)std::fabs(x[i + j]));
         e.push_back(p);
      }
      return e;
   }

   double MaxD2(const std::vector<float>& x, size_t a, size_t b)
   {
      double m = 0.0;
      b = std::min(b, x.size());
      for (size_t i = std::max<size_t>(a, 2); i < b; i++)
         m = std::max(m, (double)std::fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2]));
      return m;
   }

   // Note-on click, dB: the worst second difference in the first 5 ms re the
   // larger of the sustain (from hold / 2) and the note's own 5-50 ms body.
   // The body term is for struck or plucked voices (Metallic): they have
   // decayed by hold / 2, so against the sustain alone every strike read as
   // a click (+32 dB, node-audits F7). A real click is a step inside the
   // first 5 ms and stands out against both (ClickMetricSelfCheck).
   double ClickOnDb(const std::vector<float>& x, size_t onAt, size_t hold, double sr)
   {
      const size_t w = (size_t)(0.005 * sr);
      const double steady = MaxD2(x, onAt + hold / 2, onAt + hold / 2 + (size_t)(0.1 * sr));
      const double body = MaxD2(x, onAt + w, onAt + (size_t)(0.05 * sr));
      return Db(MaxD2(x, onAt, onAt + w) / std::max({ steady, body, 1e-12 }));
   }

   // The metric on synthetic notes, so a change to it cannot quietly stop
   // seeing clicks: a 440 Hz sine that starts at its peak (a step) and a
   // strike that decays to -28 dB by hold / 2 (the case the body term is for).
   bool ClickMetricSelfCheck()
   {
      const double sr = 48000.0, w = 2.0 * M_PI * 440.0 / sr;
      const size_t pre = 256, hold = (size_t)(1.6 * sr);
      std::vector<float> step(pre + hold, 0.0f), strike(pre + hold, 0.0f);
      for (size_t i = 0; i < hold; i++)
      {
         step[pre + i] = (float)(0.5 * std::cos(w * (double)i));
         const double a = std::min(1.0, (double)i / (0.002 * sr)); // 2 ms raised-cosine strike
         const double atk = 0.5 - 0.5 * std::cos(M_PI * a);
         strike[pre + i] = (float)(0.5 * atk * std::exp(-(double)i / (0.25 * sr)) * std::sin(w * (double)i));
      }
      const double cStep = ClickOnDb(step, pre, hold, sr), cStrike = ClickOnDb(strike, pre, hold, sr);
      const bool ok = cStep > kFlagClickDb && cStrike < kFlagClickDb;
      printf("SYNTHAUDIT CLICK metric check: step %+.1f dB (must flag), decaying strike %+.1f dB (must not) %s\n",
             cStep, cStrike, ok ? "OK" : "FAIL");
      return ok;
   }

   bool AuditNotes(const Synth& s, Flags& flags)
   {
      if (s.noNotes != nullptr)
      {
         printf("SYNTHAUDIT NOTE  %-15s not note-driven: %s\n", s.name, s.noNotes);
         return true;
      }
      const double sr = 48000.0;
      const size_t hold = (size_t)(1.6 * sr);

      // Held note 69: pitch, envelope, click.
      {
         NoteRig rig;
         if (!rig.Init(s, sr))
            return false;
         std::vector<float> x;
         rig.Run(kBlock * 4, x); // silence before
         const size_t onAt = x.size();
         const int id = rig.On(s.note, 1.0f);
         rig.Run((int)hold, x);
         const size_t offAt = x.size();
         rig.Off(s.note, id);
         rig.Run((int)(10.0 * sr), x);
         if (!AllFinite(x))
         {
            printf("SYNTHAUDIT FAIL %-15s non-finite on a note\n", s.name);
            return false;
         }
         double sustain = 0.0;
         for (size_t i = onAt + hold / 2; i < offAt; i++)
            sustain = std::max(sustain, (double)std::fabs(x[i]));
         if (sustain < 1e-5)
         {
            printf("SYNTHAUDIT NOTE  %-15s SILENT on note %d (note cable has no effect)\n", s.name, s.note);
            flags.Flag("NOTE", s.name, "silent on note-on", 1.0);
            return true;
         }
         if (s.pitched && hold >= (size_t)kN)
         {
            std::vector<float> seg(x.begin() + (long)offAt - kN, x.begin() + (long)offAt);
            const double got = PeakHz(Spectrum(seg), 440.0, sr);
            const double cents = 1200.0 * std::log2(got / 440.0);
            // The same note at true defaults: `quiet` turns off analog drift and noise for the
            // other measurements, but a user's first patch plays with them on. Analog's first voice
            // card was 3.5 c flat there and only the black-box probe saw it.
            double defCents = cents;
            {
               NoteRig def;
               if (def.Init(s, sr, false))
               {
                  std::vector<float> y;
                  def.Run(kBlock * 4, y);
                  def.On(s.note, 1.0f);
                  def.Run((int)hold, y);
                  std::vector<float> dseg(y.end() - kN, y.end());
                  defCents = 1200.0 * std::log2(PeakHz(Spectrum(dseg), 440.0, sr) / 440.0);
               }
            }
            printf("SYNTHAUDIT NOTE  %-15s note 69 pitch %+.2f cents | at defaults %+.2f cents\n", s.name, cents, defCents);
            const double worstCents = std::max(std::fabs(cents), std::fabs(defCents));
            Quality("synth.note_cents", s.name, worstCents);
            if (worstCents > kFlagPitchCents)
               flags.Flag("NOTE", s.name, "cents", std::fabs(defCents) > std::fabs(cents) ? defCents : cents);
         }
         const std::vector<double> env = Envelope(x, sr);
         const size_t eOn = onAt / (size_t)(sr * 0.001), eOff = offAt / (size_t)(sr * 0.001);
         double a10 = -1, a90 = -1, rel60 = -1;
         for (size_t i = eOn; i < eOff; i++)
         {
            if (a10 < 0 && env[i] >= 0.1 * sustain)
               a10 = (double)(i - eOn);
            if (a90 < 0 && env[i] >= 0.9 * sustain)
               a90 = (double)(i - eOn);
         }
         for (size_t i = eOff; i < env.size(); i++)
            if (env[i] < sustain * 0.001)
            {
               rel60 = (double)(i - eOff);
               break;
            }
         printf("SYNTHAUDIT ENV   %-15s attack %.0f ms | release to -60 dB %s%.0f ms\n", s.name,
                a10 >= 0 && a90 >= 0 ? a90 - a10 : -1.0, rel60 < 0 ? ">" : "", rel60 < 0 ? 10000.0 : rel60);
         const double steady = MaxD2(x, onAt + hold / 2, onAt + hold / 2 + (size_t)(0.1 * sr));
         const size_t w = (size_t)(0.005 * sr);
         const double cOn = ClickOnDb(x, onAt, hold, sr);
         const double cOff = Db(MaxD2(x, offAt, offAt + w) / std::max(steady, 1e-12));
         printf("SYNTHAUDIT CLICK %-15s note-on %+.1f dB | note-off %+.1f dB re steady\n", s.name, cOn, cOff);
         Quality("synth.click_on_db", s.name, cOn);
         Quality("synth.click_off_db", s.name, cOff);
         if (cOn > kFlagClickDb)
            flags.Flag("CLICK", s.name, "note-on dB", cOn);
         if (cOff > kFlagClickDb)
            flags.Flag("CLICK", s.name, "note-off dB", cOff);
      }

      // Velocity.
      {
         double rms[3] = {};
         const float vel[3] = { 1.0f, 0.5f, 0.25f };
         for (int k = 0; k < 3; k++)
         {
            NoteRig rig;
            rig.Init(s, sr);
            std::vector<float> x;
            rig.On(s.note, vel[k]);
            rig.Run((int)(0.5 * sr), x);
            const size_t segOff = std::min(x.size(), (size_t)(0.25 * sr));
            std::vector<float> seg;
            seg.assign(x.data() + segOff, x.data() + x.size());
            rms[k] = Rms(seg);
         }
         printf("SYNTHAUDIT VEL   %-15s vel 0.5 %+.1f dB | vel 0.25 %+.1f dB re vel 1\n", s.name,
                Db(rms[1] / std::max(rms[0], 1e-12)), Db(rms[2] / std::max(rms[0], 1e-12)));
      }

      // Polyphony and stuck voices.
      for (int n : { 1, 4, 8, 32 })
      {
         NoteRig rig;
         rig.Init(s, sr);
         std::vector<float> x;
         // What the node plays with no note held: a free-running source (Granular,
         // PaulStretch) keeps sounding after all-off, which is not a stuck voice.
         std::vector<float> idle;
         rig.Run((int)(0.5 * sr), idle);
         const double idleDb = Db(Peak(idle));
         std::vector<int> notes, ids;
         for (int i = 0; i < n; i++)
         {
            notes.push_back(36 + i * 2);
            ids.push_back(rig.On(notes.back(), 0.8f));
         }
         rig.Run((int)(0.3 * sr), x);
         x.clear();
         const int blocks = 400;
         const auto t0 = std::chrono::steady_clock::now();
         rig.Run(blocks * kBlock, x);
         const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() /
                           ((double)blocks * kBlock);
         const double pk = Db(Peak(x));
         for (int i = 0; i < n; i++)
            rig.Off(notes[(size_t)i], ids[(size_t)i]);
         std::vector<float> tail;
         rig.Run((int)(10.0 * sr), tail);
         const bool fin = AllFinite(x) && AllFinite(tail);
         std::vector<float> last(tail.end() - (long)(0.5 * sr), tail.end());
         const double tl = Db(Peak(last));
         printf("SYNTHAUDIT POLY  %-15s %2d notes: peak %6.1f dBFS | %7.1f ns/frame | tail %7.1f dBFS%s%s\n", s.name, n, pk,
                ns, tl, idleDb > kFlagTailDbfs ? " (free-running, idle level)" : "", fin ? "" : " NON-FINITE");
         if (!fin)
            return false;
         if (idleDb <= kFlagTailDbfs) // a free-running node's tail is its idle level, not a stuck voice
            Quality("synth.poly_tail_dbfs", s.name, tl);
         if (n == 4)
            Quality("synth.chord4_peak_dbfs", s.name, pk);
         if (tl > kFlagTailDbfs && tl > idleDb + 1.0)
            flags.Flag("POLY", s.name, "tail dBFS after all-off", tl);
         // A 4-note chord at velocity 0.8 must fit under 0 dBFS at defaults; 8 and
         // 32 notes are reported only (a polysynth sums its voices, see F5).
         if (n == 4 && pk > kFlagPeakDbfs)
            flags.Flag("POLY", s.name, "4-note chord peak dBFS", pk);
      }
      return true;
   }

   // SAMPLE: the fixture through a held note at 44.1 / 48 / 96 kHz. A sample
   // played at the device rate instead of its own shows as a pitch and length
   // that move with the rate. Pitch is the strongest partial near 440 Hz (the
   // fixture's fundamental, or wherever the node's root note maps it);
   // length is note-on to -40 dB re the peak, with the note held 4 s.
   // Drum Sequencer has no note input: one hit of lane 0 from its own step
   // grid (quarter-note steps at 30 bpm, only step 0 on: one hit per 8 s),
   // the transport clock advanced per block the way the engine does.
   bool RenderSteps(const Synth& s, double sr, std::vector<float>& out)
   {
      std::unique_ptr<INode> node(s.create());
      auto* d = dynamic_cast<DrumSequencerNode*>(node.get());
      IAudioSource* src = AsSource(node.get());
      if (d == nullptr || src == nullptr)
         return false;
      d->rate = MusicTime::kQuarter;
      d->numSteps = 4;
      d->stepVel[0][0] = 1.0f;
      Transport& t = Transport::Instance();
      const float bpm = t.Tempo();
      const bool playing = t.IsPlaying();
      t.SetTempo(30.0f);
      t.SetPlaying(true);
      t.Rewind();
      t.NotifyAudioEngineStarted(sr);
      AudioNode* an = src->GetAudioNode();
      an->PrepareToPlay(sr, kBlock);
      std::vector<float> l(kBlock), r(kBlock);
      float* ch[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = ch;
      buf.numChannels = 2;
      buf.numFrames = kBlock;
      out.clear();
      for (int done = 0, fr = 1; done < (int)(4.0 * sr); done += kBlock, fr++)
      {
         node->CookIfNeeded(fr);
         t.AdvanceAudioClock(kBlock);
         an->ProcessBlock(nullptr, 0, buf);
         out.insert(out.end(), l.begin(), l.end());
      }
      t.SetTempo(bpm);
      t.SetPlaying(playing);
      return true;
   }

   void AuditSample(const Synth& s, Flags& flags, bool legacy = false)
   {
      const double rates[3] = { 44100.0, 48000.0, 96000.0 };
      double hz[3] = {}, len[3] = {};
      std::string row;
      for (int r = 0; r < 3; r++)
      {
         const double sr = rates[r];
         std::vector<float> x;
         if (s.noNotes != nullptr)
         {
            if (!RenderSteps(s, sr, x))
               return;
         }
         else
         {
            NoteRig rig;
            if (!rig.Init(s, sr))
               return;
            rig.On(s.note, 1.0f);
            rig.Run((int)(4.0 * sr), x);
         }
         std::vector<float> seg((size_t)kN, 0.0f);
         const size_t from = (size_t)(0.02 * sr);
         for (size_t i = 0; i < (size_t)kN && from + i < x.size(); i++)
            seg[i] = x[from + i];
         const std::vector<double> p = Spectrum(seg);
         size_t best = 1;
         for (size_t k = 1; k + 1 < p.size() && (double)k * sr / kN < 4000.0; k++)
            if (p[k] > p[best])
               best = k;
         hz[r] = PeakHz(p, (double)best * sr / kN, sr);
         const std::vector<double> env = Envelope(x, sr);
         double pk = 0.0;
         for (double e : env)
            pk = std::max(pk, e);
         len[r] = 4.0;
         for (size_t i = env.size(); i-- > 0;)
            if (env[i] >= pk * 0.01)
            {
               len[r] = (double)(i + 1) * 0.001;
               break;
            }
         char b[64];
         snprintf(b, sizeof b, " %3.0fk %7.1f Hz %5.2f s", sr / 1000.0, hz[r], len[r]);
         row += b;
      }
      const double c = 1200.0 * std::log2(std::max({ hz[0], hz[1], hz[2] }) / std::max(1e-9, std::min({ hz[0], hz[1], hz[2] })));
      const double l = std::max({ len[0], len[1], len[2] }) / std::max(1e-9, std::min({ len[0], len[1], len[2] }));
      printf("SYNTHAUDIT SAMPLE %-14s%s | pitch spread %.1f c, length ratio %.3f%s\n", s.name, row.c_str(), c, l,
             legacy ? " (as loaded from a patch saved before rateFix)" : "");
      if (legacy)
         return;
      Quality("synth.sample_pitch_spread_c", s.name, c);
      Quality("synth.sample_length_ratio", s.name, l);
      if (c > kFlagSampleCents)
         flags.Flag("SAMPLE", s.name, "pitch spread over rates, cents", c);
      if (l > 1.02)
         flags.Flag("SAMPLE", s.name, "length ratio over rates", l);
   }
}

bool RunSynthAudit()
{
   const char* only = getenv("INFINITE_SYNTHAUDIT_ONLY");
   auto want = [&](const char* sec) { return only == nullptr || *only == 0 || strstr(only, sec) != nullptr; };
   Flags flags;
   bool ok = true;
   if (want("CLICK"))
      ok = ClickMetricSelfCheck() && ok;
   const double rates[3] = { 44100.0, 48000.0, 96000.0 };
   const float notes[4] = { 110.0f, 440.0f, 1760.0f, 7040.0f };

   for (const Synth& s : Synths())
   {
      if (s.fixture && want("SAMPLE"))
      {
         AuditSample(s, flags);
         Synth old = s; // the old-patch load path: BeforeLoadParams() runs, saved keys never arrive
         old.create = [c = s.create] {
            INode* n = c();
            n->BeforeLoadParams();
            return n;
         };
         AuditSample(old, flags, true);
      }
      std::vector<float> x;
      if (!Render(s, 48000.0, 440.0f, kN, x))
      {
         printf("SYNTHAUDIT %-15s not an IAudioSource\n", s.name);
         ok = false;
         continue;
      }
      if (AllFinite(x) && Peak(x) < 1e-5 && !s.pitched && strcmp(s.name, "Metallic") != 0)
      {
         printf("SYNTHAUDIT SKIP %-15s silent free-running (note-driven): notes only\n", s.name);
         // Field Synth has no frequency param, so its default program is
         // played through the note cable (v0.6 1.11). detune 1 and sub 0 leave
         // one harmonic series through the program's saturator: the detune's
         // beating and the sub octave are the patch, not aliasing.
         if (want("ALIAS") && strcmp(s.name, "Field Synth") == 0)
         {
            for (int note : { 96, 108 })
            {
               NoteRig rig;
               if (!rig.Init(s, 48000.0))
                  continue;
               Field::ParamTable& params = static_cast<FieldSynthNode*>(rig.node.get())->GetParamTable();
               if (Field::ParamEntry* p = params.Find("detune"))
                  p->value = 1.0f;
               if (Field::ParamEntry* p = params.Find("sub"))
                  p->value = 0.0f;
               rig.On(note, 0.8f);
               std::vector<float> settle, y;
               rig.Run(24000, settle);
               rig.Run(kN, y);
               const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
               const double a = AliasDb(Spectrum(y), hz, 48000.0);
               printf("SYNTHAUDIT ALIAS %-15s %5.0f Hz  %7.1f dB (default program, detune 1, sub 0)\n", s.name, hz, a);
               Quality("synth.alias_db", s.name, a);
               if (a > kFlagAliasDb)
                  flags.Flag("ALIAS", s.name, "dB", a);
            }
         }
         if (want("NOTE") || want("POLY"))
            ok = AuditNotes(s, flags) && ok;
         continue;
      }
      if (!AllFinite(x) || Peak(x) < 1e-5)
      {
         printf("SYNTHAUDIT FAIL %-15s %s at defaults\n", s.name, AllFinite(x) ? "silent" : "non-finite");
         ok = false;
         continue;
      }

      if (want("LEVEL"))
      {
         const double pk = Db(Peak(x));
         printf("SYNTHAUDIT LEVEL %-15s rms %6.1f dBFS | peak %6.1f dBFS\n", s.name, Db(Rms(x)), pk);
         Quality("synth.peak_dbfs", s.name, pk);
         if (pk > kFlagPeakDbfs)
            flags.Flag("LEVEL", s.name, "peak dBFS", pk);
      }

      if (want("PITCH") && s.pitched)
      {
         for (double sr : rates)
         {
            std::string row;
            for (float hz : notes)
            {
               std::vector<float> y;
               Render(s, sr, hz, kN, y);
               const double got = PeakHz(Spectrum(y), hz, sr);
               const double cents = 1200.0 * std::log2(got / hz);
               char b[48];
               snprintf(b, sizeof b, " %6.0fHz %+6.2fc", hz, cents);
               row += b;
               Quality("synth.pitch_cents", s.name, std::fabs(cents));
               if (std::fabs(cents) > kFlagPitchCents)
                  flags.Flag("PITCH", s.name, "cents", cents);
            }
            printf("SYNTHAUDIT PITCH %-15s %6.0f Hz |%s\n", s.name, sr, row.c_str());
         }
      }

      if (want("ALIAS") && s.pitched)
      {
         for (float hz : { 1750.0f, 5000.0f })
         {
            std::vector<float> y;
            Render(s, 48000.0, hz, kN, y);
            const double a = AliasDb(Spectrum(y), hz, 48000.0);
            printf("SYNTHAUDIT ALIAS %-15s %5.0f Hz  %7.1f dB\n", s.name, hz, a);
            Quality("synth.alias_db", s.name, a);
            if (a > kFlagAliasDb)
               flags.Flag("ALIAS", s.name, "dB", a);
         }
      }



      if (want("RATE"))
      {
         double lo = 1e9, hi = -1e9;
         for (double sr : rates)
         {
            std::vector<float> y;
            Render(s, sr, 440.0f, (int)(3.0 * sr), y); // same seconds at every rate
            const double d = Db(Rms(y));
            lo = std::min(lo, d);
            hi = std::max(hi, d);
         }
         if (want("RATE") && strcmp(s.name, "Metallic") == 0)
         {
            // The same note-triggered strike at each rate: free-run timing out of the picture.
            for (double sr : rates)
            {
               NoteRig rig;
               rig.Init(s, sr);
               std::vector<float> y;
               rig.On(69, 1.0f);
               rig.Run((int)(0.5 * sr), y);
               printf("SYNTHAUDIT RATE  %-15s note strike %6.0f Hz: rms(0.5 s) %6.1f dBFS peak %6.1f dBFS\n", s.name, sr,
                      Db(Rms(y)), Db(Peak(y)));
            }
         }
         printf("SYNTHAUDIT RATE  %-15s level spread %.2f dB over 44.1/48/96 kHz\n", s.name, hi - lo);
         Quality("synth.rate_spread_db", s.name, hi - lo);
         if (hi - lo > kFlagRateDb)
            flags.Flag("RATE", s.name, "spread dB", hi - lo);
      }

      if (want("DET"))
      {
         std::vector<float> a, b;
         Render(s, 48000.0, 440.0f, 1 << 14, a);
         Render(s, 48000.0, 440.0f, 1 << 14, b);
         const bool same = a == b;
         printf("SYNTHAUDIT DET   %-15s %s\n", s.name, same ? "bit-identical" : "DIFFERS");
         Quality("synth.det_differs", s.name, same ? 0.0 : 1.0);
         if (!same)
            flags.Flag("DET", s.name, "not bit-identical", 1.0);
      }


      if (want("UNISON") && s.setUnison)
      {
         std::unique_ptr<INode> node(s.create());
         s.quiet(node.get());
         s.setUnison(node.get(), 4, 100.0f);
         s.setFreq(node.get(), 440.0f);
         std::vector<float> y;
         {
            IAudioSource* src = AsSource(node.get());
            AudioNode* an = src->GetAudioNode();
            an->PrepareToPlay(48000.0, kBlock);
            node->CookIfNeeded(1);
            std::vector<float> l(kBlock), r(kBlock);
            float* ch[2] = { l.data(), r.data() };
            AudioBuffer buf;
            buf.channels = ch;
            buf.numChannels = 2;
            buf.numFrames = kBlock;
            const int total = (int)(0.3 * 48000.0) + kN;
            for (int done = 0; done < total; done += kBlock)
            {
               node->CookIfNeeded(2 + done / kBlock);
               an->ProcessBlock(nullptr, 0, buf);
               if (done >= (int)(0.3 * 48000.0))
                  y.insert(y.end(), l.begin(), l.end());
            }
            y.resize((size_t)kN);
         }
         const std::vector<double> p = Spectrum(y);
         const double bin = 48000.0 / kN;
         const int lo = (int)(440.0 * std::pow(2.0, -200.0 / 1200.0) / bin), hi = (int)(440.0 * std::pow(2.0, 200.0 / 1200.0) / bin);
         double top = 0.0;
         for (int k = lo; k <= hi; k++)
            top = std::max(top, p[(size_t)k]);
         int count = 0;
         double fmin = 1e9, fmax = 0.0;
         for (int k = lo + 1; k < hi; k++)
            if (p[(size_t)k] > p[(size_t)k - 1] && p[(size_t)k] >= p[(size_t)k + 1] && p[(size_t)k] > top * 0.01)
            {
               const double f = PeakHz(p, k * bin, 48000.0);
               if (count > 0 && f - 0.0 < fmax + 3.0 * bin && f < fmax + 3.0 * bin)
                  continue;
               count++;
               fmin = std::min(fmin, f);
               fmax = std::max(fmax, f);
            }
         const double extent = count > 1 ? 1200.0 * std::log2(fmax / fmin) : 0.0;
         printf("SYNTHAUDIT UNISON %-14s 4 voices, 100 c: %d peaks, outer spread %.1f cents\n", s.name, count, extent);
         if (count != 4)
            flags.Flag("UNISON", s.name, "peak count (expect 4)", (double)count);
         if (std::fabs(extent - 100.0) > 5.0)
            flags.Flag("UNISON", s.name, "outer spread cents (expect 100)", extent);
      }

      if (want("PARAM") && s.setVolume)
      {
         std::unique_ptr<INode> node(s.create());
         s.quiet(node.get());
         s.setFreq(node.get(), 440.0f);
         s.setVolume(node.get(), 0.2f);
         IAudioSource* src = AsSource(node.get());
         AudioNode* an = src->GetAudioNode();
         an->PrepareToPlay(48000.0, kBlock);
         std::vector<float> l(kBlock), r(kBlock), y;
         float* ch[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = kBlock;
         const int pre = 48000 / 2;
         size_t stepAt = 0;
         for (int done = 0, fr = 1; done < pre * 2; done += kBlock, fr++)
         {
            if (done >= pre && stepAt == 0)
            {
               s.setVolume(node.get(), 0.8f);
               stepAt = y.size();
            }
            node->CookIfNeeded(fr);
            an->ProcessBlock(nullptr, 0, buf);
            y.insert(y.end(), l.begin(), l.end());
         }
         const size_t w = (size_t)(0.05 * 48000.0);
         const double steady = MaxD2(y, stepAt - w - 1000, stepAt - 1000);
         const double after = MaxD2(y, stepAt, stepAt + w);
         const double settled = MaxD2(y, stepAt + 4 * w, stepAt + 5 * w);
         const double db = Db(after / std::max(settled, 1e-12));
         printf("SYNTHAUDIT PARAM %-15s volume 0.2 -> 0.8 step: worst d2 %+.1f dB re settled (steady before %.1f dB)\n", s.name, db,
                Db(steady / std::max(settled, 1e-12)));
         if (db > kFlagClickDb)
            flags.Flag("PARAM", s.name, "volume step dB", db);
      }

      if (want("NOTE") || want("ENV") || want("CLICK") || want("VEL") || want("POLY"))
         ok = AuditNotes(s, flags) && ok;

      if (want("CPU"))
      {
         std::unique_ptr<INode> node(s.create());
         IAudioSource* src = AsSource(node.get());
         s.setFreq(node.get(), 440.0f);
         AudioNode* an = src->GetAudioNode();
         an->PrepareToPlay(48000.0, kBlock);
         node->CookIfNeeded(1);
         std::vector<float> l(kBlock), r(kBlock);
         float* ch[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = kBlock;
         for (int i = 0; i < 50; i++)
            an->ProcessBlock(nullptr, 0, buf);
         const int blocks = 2000;
         const auto t0 = std::chrono::steady_clock::now();
         for (int i = 0; i < blocks; i++)
            an->ProcessBlock(nullptr, 0, buf);
         const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() /
                           ((double)blocks * kBlock);
         printf("SYNTHAUDIT CPU   %-15s %7.1f ns/frame\n", s.name, ns);
         Quality("synth.cpu_ns_frame", s.name, ns);
         if (ns > kFlagCpuNs)
            flags.Flag("CPU", s.name, "ns/frame", ns);
      }
   }

   printf("SYNTHAUDIT %s (%d flags)\n", ok ? "PASS" : "FAILED", flags.n);
   return ok;
}
}
