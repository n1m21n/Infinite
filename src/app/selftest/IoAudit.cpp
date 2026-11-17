// INFINITE_IOAUDIT: a measurement audit of the audio routing nodes.
//
// The routing twin of FXAUDIT / SYNTHAUDIT / NOTEAUDIT (docs/plans/node-audits/
// README.md, block 2). Gain, Mixer, Blend Audio, Splitter, Audio Meter and
// Spatial Mixer run through their real node -> audio node ProcessBlock at
// 512-frame blocks, fed 48 kHz stereo test signals:
//
//   LAW     Gain: measured dB against the knob, -48..+24 dB (flag > 0.01 dB)
//           Mixer: centred channel is unity; equal-power pan sweep of a mono
//           signal (flag > 0.1 dB spread); hard pan leaves the far side
//           silent; mute and solo silence what they should
//           Blend: a/b gain at five positions; total power of two unrelated
//           sines constant (flag > 0.1 dB spread); ends are exactly one side
//           Splitter: bit-identical copy
//   SUM     Mixer with all 12 slots summing 12 different sines against a
//           double-precision reference (flag > 1e-6), and again with the
//           slots in reverse order (flag any difference > 1e-6)
//   METER   BS.1770 loudness (the Spatial Mixer's SpatialMaster::Loudness)
//           against EBU Tech 3341 anchors (997 Hz stereo sine -23 / -33 dBFS
//           reads -23 / -33 LUFS; left only reads 3.01 LU lower) and against
//           the published K-weighting response at 100 Hz / 1 kHz / 10 kHz at
//           48 kHz (flag > 0.1 LU); Audio Meter peak and RMS readout of a
//           0.5-amplitude sine (flag > 0.1 dB)
//   LINK    worst second difference within 5 ms of an instant parameter step
//           (gain, mixer fader, mixer mute, mixer solo, blend), dB re the
//           steady-state worst (flag 12 dB, FXAUDIT's zipper threshold)
//   CPU     ns per stereo frame
//
// Not covered: Audio In / Audio Out (hardware), delay compensation across the
// graph (needs the engine; see audio-pipeline-sweep).
//
// Only hard invariants fail the run (non-finite output, a Splitter that is not
// a copy). Everything else is reported, with FLAG lines past the thresholds.
#include "app/AppShared.h"
#include "audio/AudioBuffer.h"
#include "audio/dsp/SpatialMaster.h"
#include "nodes/AudioMeterNode.h"
#include "nodes/AudioNodes.h"
#include "nodes/SpatialMixerNode.h"

#include <chrono>

namespace app
{
namespace
{
   constexpr double kRate = 48000.0;
   constexpr int kBlock = 512;

   // One measured value for the quality system (docs/quality/README.md): tools/quality/quality.py keeps
   // the worst per metric and subject and owns the target and the release gate.
   void Quality(const char* id, const std::string& node, double v)
   {
      printf("QUALITY id=%s subject=\"node:%s\" value=%.4f\n", id, node.c_str(), v);
   }

   struct Flags
   {
      int n = 0;
      void Flag(const char* sec, const char* node, const char* what, double v)
      {
         printf("IOAUDIT FLAG %-5s %-14s %s %.4f\n", sec, node, what, v);
         n++;
      }
   };

   struct Sig
   {
      std::vector<float> l, r;
      explicit Sig(int n = 0) : l(n, 0.0f), r(n, 0.0f) {}
      int N() const { return (int)l.size(); }
   };

   Sig Sine(double hz, double amp, int n, double phase = 0.0, bool left = true, bool right = true)
   {
      Sig s(n);
      for (int i = 0; i < n; i++)
      {
         const float v = (float)(amp * std::sin(2.0 * M_PI * hz * (double)i / kRate + phase));
         if (left)
            s.l[i] = v;
         if (right)
            s.r[i] = v;
      }
      return s;
   }

   double Rms(const std::vector<float>& x, int from, int to)
   {
      double a = 0.0;
      for (int i = from; i < to; i++)
         a += (double)x[i] * x[i];
      return std::sqrt(a / std::max(1, to - from));
   }
   double Db(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }

   [[maybe_unused]] bool Finite(const Sig& s)
   {
      for (int i = 0; i < s.N(); i++)
         if (!std::isfinite(s.l[i]) || !std::isfinite(s.r[i]))
            return false;
      return true;
   }

   // Runs `node` over the inputs block by block. `between(blockStartSample)` is
   // called before each block, so a test can change a parameter and re-cook.
   Sig Run(INode* node, AudioNode* audio, const std::vector<const Sig*>& ins, int total,
           const std::function<void(int)>& between = nullptr)
   {
      static int sFrame = 1;
      Sig out(total);
      std::vector<float> l(kBlock), r(kBlock);
      std::vector<std::vector<float>> inL(ins.size(), std::vector<float>(kBlock)), inR = inL;
      std::vector<AudioBuffer> inBufs(ins.size());
      std::vector<float*> chans[16];
      std::vector<const AudioBuffer*> ptrs(ins.size());
      for (int pos = 0; pos < total; pos += kBlock)
      {
         if (between)
            between(pos);
         node->CookIfNeeded(sFrame++);
         const int n = std::min(kBlock, total - pos);
         for (size_t k = 0; k < ins.size(); k++)
         {
            for (int i = 0; i < kBlock; i++)
            {
               const bool have = ins[k] != nullptr && pos + i < ins[k]->N();
               inL[k][i] = have ? ins[k]->l[pos + i] : 0.0f;
               inR[k][i] = have ? ins[k]->r[pos + i] : 0.0f;
            }
            chans[k] = { inL[k].data(), inR[k].data() };
            inBufs[k].channels = chans[k].data();
            inBufs[k].numChannels = 2;
            inBufs[k].numFrames = kBlock;
            ptrs[k] = ins[k] != nullptr ? &inBufs[k] : nullptr;
         }
         float* oc[2] = { l.data(), r.data() };
         AudioBuffer ob;
         ob.channels = oc;
         ob.numChannels = 2;
         ob.numFrames = kBlock;
         audio->ProcessBlock(ptrs.data(), (int)ptrs.size(), ob);
         for (int i = 0; i < n; i++)
         {
            out.l[pos + i] = l[i];
            out.r[pos + i] = r[i];
         }
      }
      return out;
   }

   AudioNode* Prep(INode* n)
   {
      n->CookIfNeeded(0);
      AudioNode* a = dynamic_cast<IAudioSource*>(n)->GetAudioNode();
      a->PrepareToPlay(kRate, kBlock);
      return a;
   }

   // ------------------------------------------------------------------ LAW
   void AuditGain(Flags& flags)
   {
      const Sig in = Sine(1000.0, 0.25, 16384);
      double worst = 0.0;
      for (float db : { -48.0f, -24.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 24.0f })
      {
         std::unique_ptr<GainNode> g(static_cast<GainNode*>(GainNode::Create()));
         g->gainDb = db;
         AudioNode* a = Prep(g.get());
         const Sig out = Run(g.get(), a, { &in }, 16384);
         const double got = Db(Rms(out.l, 8192, 16384)) - Db(Rms(in.l, 8192, 16384));
         worst = std::max(worst, std::fabs(got - db));
      }
      printf("IOAUDIT LAW   Gain           worst error over -48..+24 dB: %.4f dB\n", worst);
      Quality("io.gain_knob_db", "Gain", worst);
      if (worst > 0.01)
         flags.Flag("LAW", "Gain", "knob error dB", worst);
   }

   struct MixerRig
   {
      std::unique_ptr<MixerNode> node;
      AudioNode* audio = nullptr;
      MixerRig()
      {
         node.reset(static_cast<MixerNode*>(MixerNode::Create()));
         node->numChannels = 12;
         audio = Prep(node.get());
      }
   };

   void AuditMixerLaw(Flags& flags)
   {
      const Sig in = Sine(1000.0, 0.25, 16384);
      {
         MixerRig m;
         const Sig out = Run(m.node.get(), m.audio, { &in }, 16384);
         const double d = Db(Rms(out.l, 8192, 16384)) - Db(Rms(in.l, 8192, 16384));
         printf("IOAUDIT LAW   Mixer          centred unity channel: %.4f dB\n", d);
         if (std::fabs(d) > 0.01)
            flags.Flag("LAW", "Mixer", "centred channel dB", d);
      }
      double lo = 1e9, hi = -1e9;
      double hardFar = -1e9;
      for (float pan : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
      {
         MixerRig m;
         m.node->pan[0] = pan;
         const Sig out = Run(m.node.get(), m.audio, { &in }, 16384);
         const double p = Rms(out.l, 8192, 16384) * Rms(out.l, 8192, 16384) + Rms(out.r, 8192, 16384) * Rms(out.r, 8192, 16384);
         const double pdb = 10.0 * std::log10(std::max(p, 1e-18));
         lo = std::min(lo, pdb);
         hi = std::max(hi, pdb);
         if (pan == -1.0f)
            hardFar = std::max(hardFar, Db(Rms(out.r, 8192, 16384)));
         if (pan == 1.0f)
            hardFar = std::max(hardFar, Db(Rms(out.l, 8192, 16384)));
      }
      printf("IOAUDIT LAW   Mixer          pan sweep total power spread %.4f dB, far side at hard pan %.1f dBFS\n", hi - lo, hardFar);
      if (hi - lo > 0.1)
         flags.Flag("LAW", "Mixer", "pan power spread dB", hi - lo);
      if (hardFar > -120.0)
         flags.Flag("LAW", "Mixer", "far side at hard pan dBFS", hardFar);
      {
         MixerRig m;
         m.node->mute[0] = true;
         const Sig out = Run(m.node.get(), m.audio, { &in }, 16384);
         const double mdb = Db(Rms(out.l, 8192, 16384));
         MixerRig s;
         s.node->solo[1] = true;
         const Sig out2 = Run(s.node.get(), s.audio, { &in, &in }, 16384);
         const double sdb = Db(Rms(out2.l, 8192, 16384));
         const double expect = Db(Rms(in.l, 8192, 16384));
         printf("IOAUDIT LAW   Mixer          mute %.1f dBFS | solo ch2 of 2: %.2f dB re one channel\n", mdb, sdb - expect);
         if (mdb > -120.0)
            flags.Flag("LAW", "Mixer", "muted channel dBFS", mdb);
         if (std::fabs(sdb - expect) > 0.01)
            flags.Flag("LAW", "Mixer", "solo level error dB", sdb - expect);
      }
      {
         MixerRig m;
         m.node->gainDb[0] = -60.0f;
         const Sig out = Run(m.node.get(), m.audio, { &in }, 16384);
         const double d = Db(Rms(out.l, 8192, 16384)) - Db(Rms(in.l, 8192, 16384));
         printf("IOAUDIT LAW   Mixer          fader -60 dB reads %.3f dB\n", d);
         if (std::fabs(d + 60.0) > 0.05)
            flags.Flag("LAW", "Mixer", "fader -60 dB error", d + 60.0);
      }
   }

   void AuditBlendSplitter(Flags& flags, bool& ok)
   {
      const Sig a = Sine(700.0, 0.3, 16384), b = Sine(1900.0, 0.3, 16384, 1.0);
      double lo = 1e9, hi = -1e9;
      double endA = 0, endB = 0;
      for (float bl : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
      {
         std::unique_ptr<BlendAudioNode> n(static_cast<BlendAudioNode*>(BlendAudioNode::Create()));
         n->blend = bl;
         AudioNode* au = Prep(n.get());
         const Sig out = Run(n.get(), au, { &a, &b }, 16384);
         const double rms = Rms(out.l, 8192, 16384);
         const double rmsA = Rms(a.l, 8192, 16384);
         const double pdb = 20.0 * std::log10(rms / rmsA);
         lo = std::min(lo, pdb);
         hi = std::max(hi, pdb);
         if (bl == 0.0f)
         {
            double e = 0;
            for (int i = 8192; i < 16384; i++)
               e = std::max(e, (double)std::fabs(out.l[i] - a.l[i]));
            endA = e;
         }
         if (bl == 1.0f)
         {
            double e = 0;
            for (int i = 8192; i < 16384; i++)
               e = std::max(e, (double)std::fabs(out.l[i] - b.l[i]));
            endB = e;
         }
      }
      printf("IOAUDIT LAW   Blend Audio    power spread %.4f dB over 5 positions | blend 0 differs from A by %.2e, blend 1 from B by %.2e\n", hi - lo,
             endA, endB);
      if (hi - lo > 0.1)
         flags.Flag("LAW", "Blend Audio", "power spread dB", hi - lo);
      if (endA > 1e-5 || endB > 1e-5)
         flags.Flag("LAW", "Blend Audio", "end position not exact", std::max(endA, endB));

      std::unique_ptr<SplitterNode> sp(static_cast<SplitterNode*>(SplitterNode::Create()));
      AudioNode* au = Prep(sp.get());
      const Sig out = Run(sp.get(), au, { &a }, 16384);
      bool same = true;
      for (int i = 0; i < 16384; i++)
         same = same && out.l[i] == a.l[i] && out.r[i] == a.r[i];
      printf("IOAUDIT LAW   Splitter       bit-identical copy: %s\n", same ? "yes" : "NO");
      if (!same)
         ok = false;
   }

   // ------------------------------------------------------------------ SUM
   void AuditSum(Flags& flags)
   {
      const int N = 8192;
      std::vector<Sig> sigs;
      for (int k = 0; k < 12; k++)
         sigs.push_back(Sine(220.0 * (k + 1), 0.05, N, 0.3 * k));
      std::vector<double> ref(N, 0.0);
      for (int k = 0; k < 12; k++)
         for (int i = 0; i < N; i++)
            ref[i] += (double)sigs[k].l[i];
      std::vector<const Sig*> fwd, rev;
      for (int k = 0; k < 12; k++)
      {
         fwd.push_back(&sigs[k]);
         rev.push_back(&sigs[11 - k]);
      }
      MixerRig m1, m2;
      const Sig o1 = Run(m1.node.get(), m1.audio, fwd, N);
      const Sig o2 = Run(m2.node.get(), m2.audio, rev, N);
      double err = 0.0, ord = 0.0;
      for (int i = 0; i < N; i++)
      {
         err = std::max(err, std::fabs((double)o1.l[i] - ref[i]));
         ord = std::max(ord, (double)std::fabs(o1.l[i] - o2.l[i]));
      }
      printf("IOAUDIT SUM   Mixer          12 slots: max error vs double reference %.2e | slot order reversed: max difference %.2e\n", err, ord);
      Quality("io.sum_error", "Mixer", err);
      if (err > 1e-6)
         flags.Flag("SUM", "Mixer", "sum error", err);
      if (ord > 1e-6)
         flags.Flag("SUM", "Mixer", "slot-order difference", ord);
   }

   // ---------------------------------------------------------------- METER
   // Published BS.1770 48 kHz K-weighting coefficients (ITU-R BS.1770-4 tables
   // 1 and 2), evaluated analytically - independent of SpatialMaster's own
   // coefficient derivation.
   double KWeightDb(double hz)
   {
      using C = std::complex<double>;
      const double w = 2.0 * M_PI * hz / kRate;
      const C z1 = std::exp(C(0.0, -w)), z2 = z1 * z1;
      const double b1[3] = { 1.53512485958697, -2.69169618940638, 1.19839281085285 };
      const double a1[3] = { 1.0, -1.69065929318241, 0.73248077421585 };
      const double b2[3] = { 1.0, -2.0, 1.0 };
      const double a2[3] = { 1.0, -1.99004745483398, 0.99007225036621 };
      const C h1 = (b1[0] + b1[1] * z1 + b1[2] * z2) / (a1[0] + a1[1] * z1 + a1[2] * z2);
      const C h2 = (b2[0] + b2[1] * z1 + b2[2] * z2) / (a2[0] + a2[1] * z1 + a2[2] * z2);
      return 20.0 * std::log10(std::abs(h1 * h2));
   }

   double LoudnessOf(const Sig& s, double* shortTerm = nullptr)
   {
      SpatialMaster::Loudness m;
      m.Prepare(kRate);
      for (int i = 0; i < s.N(); i++)
         m.Process(s.l[i], s.r[i]);
      if (shortTerm)
         *shortTerm = m.ShortTerm();
      return m.Momentary();
   }

   void AuditMeter(Flags& flags)
   {
      const int N = (int)(4.0 * kRate);
      auto check = [&](const char* what, double got, double want)
      {
         const double e = got - want;
         printf("IOAUDIT METER Loudness       %-34s %8.3f LUFS (want %8.3f, error %+.3f)\n", what, got, want, e);
         if (std::fabs(e) > 0.1)
            flags.Flag("METER", "Loudness", what, e);
      };
      double st = 0.0;
      check("997 Hz stereo -23 dBFS momentary", LoudnessOf(Sine(997.0, std::pow(10.0, -23.0 / 20.0), N), &st), -23.0);
      check("997 Hz stereo -23 dBFS short-term", st, -23.0);
      check("997 Hz stereo -33 dBFS momentary", LoudnessOf(Sine(997.0, std::pow(10.0, -33.0 / 20.0), N)), -33.0);
      check("997 Hz left only -23 dBFS", LoudnessOf(Sine(997.0, std::pow(10.0, -23.0 / 20.0), N, 0.0, true, false)), -23.0 - 3.0103);
      for (double hz : { 100.0, 1000.0, 10000.0 })
      {
         const double amp = 0.1;
         const double want = 20.0 * std::log10(amp) + KWeightDb(hz) - 0.691; // stereo +3.01, sine mean square -3.01
         char what[48];
         snprintf(what, sizeof(what), "%.0f Hz K-weighting", hz);
         check(what, LoudnessOf(Sine(hz, amp, N)), want);
      }

      // Audio Meter node: peak and RMS readout of a 0.5 sine, after ballistics.
      std::unique_ptr<AudioMeterNode> mn(static_cast<AudioMeterNode*>(AudioMeterNode::Create()));
      AudioNode* a = dynamic_cast<IAudioSource*>(mn.get())->GetAudioNode();
      mn->CookIfNeeded(0);
      a->PrepareToPlay(kRate, kBlock);
      const Sig in = Sine(1000.0, 0.5, 143360); // 280 whole blocks: a short last block would be zero-padded
      Run(mn.get(), a, { &in }, 143360);
      // The display ballistics step by wall-clock dt (>= 1 ms per cook), so a
      // few hundred back-to-back cooks settle the smoothed RMS without waiting.
      for (int k = 0; k < 3000; k++)
      {
         static int f = 100000;
         mn->CookIfNeeded(f++);
      }
      const double peakDb = Db(mn->MaxPeakL()), rmsDb = Db(mn->RmsL());
      printf("IOAUDIT METER Audio Meter    0.5 sine: peak %.3f dBFS (want -6.021), rms %.3f dBFS (want -9.031)\n", peakDb, rmsDb);
      if (std::fabs(peakDb + 6.0206) > 0.1)
         flags.Flag("METER", "Audio Meter", "peak error dB", peakDb + 6.0206);
      if (std::fabs(rmsDb + 9.0309) > 0.1)
         flags.Flag("METER", "Audio Meter", "rms error dB", rmsDb + 9.0309);
   }

   // ----------------------------------------------------------------- LINK
   double MaxD2(const std::vector<float>& x, int from, int to)
   {
      double m = 0.0;
      for (int i = std::max(from, 2); i < to; i++)
         m = std::max(m, (double)std::fabs(x[i] - 2.0f * x[i - 1] + x[i - 2]));
      return m;
   }

   void Link(Flags& flags, const char* name, INode* node, AudioNode* audio, const std::vector<const Sig*>& ins,
             const std::function<void()>& step)
   {
      const int total = 16384, stepAt = 8192;
      bool done = false;
      const Sig out = Run(node, audio, ins, total, [&](int pos)
      {
         if (pos >= stepAt && !done)
         {
            step();
            done = true;
         }
      });
      const double steady = MaxD2(out.l, 4096, stepAt - 512);
      const double around = MaxD2(out.l, stepAt - 64, stepAt + (int)(0.005 * kRate) + kBlock);
      const double d = Db(around) - Db(steady);
      printf("IOAUDIT LINK  %-22s step click %6.1f dB re steady\n", name, d);
      if (d > 12.0)
         flags.Flag("LINK", name, "step click dB", d);
   }

   void AuditLink(Flags& flags)
   {
      const Sig in = Sine(440.0, 0.3, 16384);
      {
         std::unique_ptr<GainNode> g(static_cast<GainNode*>(GainNode::Create()));
         AudioNode* a = Prep(g.get());
         Link(flags, "Gain 0 -> -24 dB", g.get(), a, { &in }, [&] { g->gainDb = -24.0f; });
      }
      {
         MixerRig m;
         Link(flags, "Mixer fader 0 -> -24", m.node.get(), m.audio, { &in }, [&] { m.node->gainDb[0] = -24.0f; });
      }
      {
         MixerRig m;
         Link(flags, "Mixer mute", m.node.get(), m.audio, { &in }, [&] { m.node->mute[0] = true; });
      }
      {
         MixerRig m;
         Link(flags, "Mixer solo other", m.node.get(), m.audio, { &in, &in }, [&] { m.node->solo[1] = true; });
      }
      {
         MixerRig m;
         Link(flags, "Mixer pan 0 -> hard L", m.node.get(), m.audio, { &in }, [&] { m.node->pan[0] = -1.0f; });
      }
      {
         const Sig b = Sine(440.0, 0.3, 16384, 1.0); // same pitch: the step, not a tone change
         std::unique_ptr<BlendAudioNode> n(static_cast<BlendAudioNode*>(BlendAudioNode::Create()));
         n->blend = 0.0f;
         AudioNode* a = Prep(n.get());
         Link(flags, "Blend 0 -> 1", n.get(), a, { &in, &b }, [&] { n->blend = 1.0f; });
      }
   }

   // ------------------------------------------------------------------ CPU
   void CpuOne(const char* name, INode* node, AudioNode* audio, int inputs)
   {
      const Sig in = Sine(440.0, 0.3, 16384);
      std::vector<const Sig*> ins(inputs, &in);
      Run(node, audio, ins, 16384);
      const auto t0 = std::chrono::steady_clock::now();
      const int total = 16384 * 8;
      Run(node, audio, ins, total);
      const auto t1 = std::chrono::steady_clock::now();
      printf("IOAUDIT CPU   %-14s %7.1f ns per stereo frame\n", name, std::chrono::duration<double, std::nano>(t1 - t0).count() / total);
   }

   void AuditCpu()
   {
      {
         std::unique_ptr<GainNode> g(static_cast<GainNode*>(GainNode::Create()));
         CpuOne("Gain", g.get(), Prep(g.get()), 1);
      }
      {
         MixerRig m;
         CpuOne("Mixer 12", m.node.get(), m.audio, 12);
      }
      {
         std::unique_ptr<BlendAudioNode> n(static_cast<BlendAudioNode*>(BlendAudioNode::Create()));
         CpuOne("Blend Audio", n.get(), Prep(n.get()), 2);
      }
      {
         std::unique_ptr<SplitterNode> n(static_cast<SplitterNode*>(SplitterNode::Create()));
         CpuOne("Splitter", n.get(), Prep(n.get()), 1);
      }
      {
         std::unique_ptr<AudioMeterNode> n(static_cast<AudioMeterNode*>(AudioMeterNode::Create()));
         CpuOne("Audio Meter", n.get(), Prep(n.get()), 1);
      }
      {
         std::unique_ptr<SpatialMixerNode> n(static_cast<SpatialMixerNode*>(SpatialMixerNode::Create()));
         n->CookIfNeeded(0);
         AudioNode* a = n->GetAudioNode();
         a->PrepareToPlay(kRate, kBlock);
         CpuOne("Spatial Mixer", n.get(), a, 4);
      }
   }
} // namespace

bool RunIoAudit()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   const char* only = getenv("INFINITE_IOAUDIT_ONLY");
   auto want = [&](const char* sec) { return only == nullptr || *only == 0 || strstr(only, sec) != nullptr; };
   Flags flags;
   bool ok = true;
   if (want("LAW"))
   {
      AuditGain(flags);
      AuditMixerLaw(flags);
      AuditBlendSplitter(flags, ok);
   }
   if (want("SUM"))
      AuditSum(flags);
   if (want("METER"))
      AuditMeter(flags);
   if (want("LINK"))
      AuditLink(flags);
   if (want("CPU"))
      AuditCpu();
   printf("IOAUDIT %s (%d flags)\n", ok ? "PASS" : "FAIL", flags.n);
   return ok;
}

} // namespace app
