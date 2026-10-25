// Circuit Synth presets: textbook schematics with our own part values, tuned
// by ear and by the INFINITE_CIRCUITSYNTHTEST measurements (level, pitch,
// stereo correlation). Each one is a plain netlist: nodes are numbered from 1,
// 0 is ground.
#include "CircuitSynthDsp.h"

#include <cstring>
#include <vector>

namespace CircuitDsp
{
   namespace
   {
      constexpr double kTwoPi = 6.283185307179586;

      struct Build
      {
         Preset p;

         Build(const char* name, double refHz, float gain)
         {
            std::strncpy(p.name, name, sizeof(p.name) - 1);
            p.refHz = refHz;
            p.gain = gain;
         }
         int Node()
         {
            const int n = ++p.numNodes;
            p.nodeX[n] = (float)(n - 1); // a chain left to right unless placed
            return n;
         }
         void At(int node, float x, float y)
         {
            p.nodeX[node] = x;
            p.nodeY[node] = y;
         }
         void Route(int part, uint8_t r) { p.parts[part].route = r; }
         int Knob(const char* label)
         {
            KnobDef& k = p.knobs[p.numKnobs];
            std::strncpy(k.label, label, sizeof(k.label) - 1);
            return p.numKnobs++;
         }
         int Add(PartKind kind, int a, int b, double value, int knob, double p1 = 0.0, double p2 = 0.0, double p3 = 0.0)
         {
            PartDef& d = p.parts[p.numParts];
            d.kind = kind;
            d.a = (int8_t)a;
            d.b = (int8_t)b;
            d.value = value;
            d.p1 = p1;
            d.p2 = p2;
            d.p3 = p3;
            d.knob = (int8_t)knob;
            if (knob >= 0 && p.knobs[knob].part < 0)
               p.knobs[knob].part = (int8_t)p.numParts;
            return p.numParts++;
         }
         int R(int a, int b, double ohm, int knob = -1) { return Add(kResistor, a, b, ohm, knob); }
         int C(int a, int b, double farad, int knob = -1) { return Add(kCapacitor, a, b, farad, knob); }
         int L(int a, int b, double henry, int knob = -1) { return Add(kInductor, a, b, henry, knob); }
         // Anode a, cathode b. 1N4148-like by default.
         int D(int a, int b, int knob = -1, double is = 2.5e-9, double n = 1.75) { return Add(kDiode, a, b, is, knob, n); }
         int Neon(int a, int b, double strike, double maintain, double rlit, int knob = -1)
         {
            return Add(kNeon, a, b, strike, knob, maintain, rlit);
         }
         int Chua(int a, int b, double ga, double gb, double e1, double e2, int knob = -1)
         {
            return Add(kChua, a, b, ga, knob, gb, e1, e2);
         }
         int Neg(int a, int b, double g, double vs, int knob = -1) { return Add(kNegRes, a, b, g, knob, vs); }
         int Op(int plus, int minus, int out, int knob = -1, double gain = 1e5, double rout = 50.0, double rail = 12.0)
         {
            const int k = Add(kOpAmp, plus, minus, gain, knob, rout, rail);
            p.parts[k].c = (int8_t)out;
            return k;
         }
         int Gen(int a, double volts, int knob = -1, double rs = 1.0) { return Add(kGenerator, a, 0, volts, knob, rs); }
         void Fixed(int part) { p.parts[part].tracks = false; }
         // V(a) - V(b).
         void Probe(const char* name, int a, int b = 0, float gain = 1.0f)
         {
            ProbeDef& q = p.probes[p.numProbes++];
            std::strncpy(q.name, name, sizeof(q.name) - 1);
            q.node[0] = (int8_t)a;
            q.weight[0] = 1.0f;
            q.node[1] = (int8_t)b;
            q.weight[1] = b ? -1.0f : 0.0f;
            q.gain = gain;
         }
         // One pickup hearing two points: V(a) + mix V(c).
         void ProbeMix(const char* name, int a, int c, float mix, float gain)
         {
            Probe(name, a, 0, gain);
            p.probes[p.numProbes - 1].node[1] = (int8_t)c;
            p.probes[p.numProbes - 1].weight[1] = mix;
         }
         // Up to four points with their weights.
         void ProbeSum(const char* name, const int (&nodes)[4], const float (&weights)[4], float gain)
         {
            ProbeDef& q = p.probes[p.numProbes++];
            std::strncpy(q.name, name, sizeof(q.name) - 1);
            for (int k = 0; k < 4; k++)
            {
               q.node[k] = (int8_t)nodes[k];
               q.weight[k] = weights[k];
            }
            q.gain = gain;
         }
         void ProbeI(const char* name, int part, float gain)
         {
            ProbeDef& q = p.probes[p.numProbes++];
            std::strncpy(q.name, name, sizeof(q.name) - 1);
            q.current = true;
            q.part = (int8_t)part;
            q.gain = gain;
         }
         Build& Play(int wave, int trigger, float attack, float release, float contactMs = 1.0f)
         {
            p.wave = wave;
            p.trigger = trigger;
            p.attack = attack;
            p.release = release;
            p.contactMs = contactMs;
            return *this;
         }
         Build& Stereo(int probeA, int probeB, float spread)
         {
            p.probeA = probeA;
            p.probeB = probeB;
            p.spread = spread;
            return *this;
         }
         Build& Follow(bool c, bool l)
         {
            p.cFollow = c;
            p.lFollow = l;
            return *this;
         }
         Build& Damp(int knob, float sign)
         {
            p.dampKnob = knob;
            p.dampSign = sign;
            return *this;
         }
      };

      // ------------------------------------------------ clippers

      // The owner's photo: generator 2 V saw -> R 2.2k -> antiparallel
      // diodes + C to ground, and a second RC after it for a darker side.
      Preset DiodeClipper()
      {
         Build b("Diode Clipper", 46.2, 0.75f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("R"), kC = b.Knob("C"), kD = b.Knob("D"), kR2 = b.Knob("R2"), kC2 = b.Knob("C2");
         b.Gen(n1, 2.0, kG);
         const int r1 = b.R(n1, n2, 2200.0, kR);
         b.D(n2, 0, kD);
         b.D(0, n2, kD);
         b.C(n2, 0, 56e-9, kC);
         b.R(n2, n3, 22000.0, kR2);
         b.C(n3, 0, 33e-9, kC2);
         b.Probe("clip", n2);
         b.Probe("tone", n3, 0, 1.4f);
         b.ProbeI("R current", r1, 2200.0f);
         b.Play(kSaw, kHold, 0.004f, 0.47f).Stereo(0, 1, 4.0f).Damp(kR, 1.0f);
         return b.p;
      }

      // Two stages: a non-inverting op-amp with antiparallel diodes in its
      // feedback (soft clip, the gain leg's RC gives the mid hump), then a
      // germanium / silicon pair to ground (hard, asymmetric) and a tone RC.
      Preset Fuzz()
      {
         Build b("Fuzz", 82.4, 0.6f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node(), n4 = b.Node(), n5 = b.Node(), n6 = b.Node(), n7 = b.Node();
         const int kG = b.Knob("drive"), kGain = b.Knob("gain R"), kD = b.Knob("soft D"), kR = b.Knob("R"), kH = b.Knob("hard D"),
                   kR2 = b.Knob("tone R"), kC2 = b.Knob("tone C"), kMid = b.Knob("mid C");
         b.At(n3, 2.0f, -1.0f);
         b.At(n4, 3.0f, 0.0f);
         b.At(n5, 1.4f, -2.0f);
         b.At(n6, 4.0f, 0.0f);
         b.At(n7, 5.0f, 0.0f);
         b.Gen(n1, 0.5, kG);
         b.R(n1, n2, 1000.0);
         b.R(n2, 0, 510000.0);
         b.Op(n2, n3, n4);
         b.R(n4, n3, 51000.0, kGain);
         b.D(n4, n3, kD);
         b.D(n3, n4, kD);
         b.R(n3, n5, 4700.0);
         b.C(n5, 0, 47e-9, kMid);
         b.R(n4, n6, 1000.0, kR);
         b.D(n6, 0, kH, 2e-7, 1.3);  // germanium-ish
         b.D(0, n6, kH, 2.5e-9, 1.9); // silicon
         b.C(n6, 0, 10e-9);
         b.R(n6, n7, 4700.0, kR2);
         b.C(n7, 0, 47e-9, kC2);
         b.Probe("soft", n4, 0, 0.6f);
         b.Probe("hard", n6);
         b.Probe("tone", n7, 0, 1.5f);
         b.Play(kSaw, kHold, 0.003f, 0.3f).Stereo(2, 1, 1.5f).Damp(kR2, 1.0f);
         return b.p;
      }

      // ------------------------------------------------ filters

      Preset RcLowpass()
      {
         Build b("RC Lowpass", 220.0, 0.55f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("G"), kR1 = b.Knob("R1"), kC1 = b.Knob("C1"), kR2 = b.Knob("R2"), kC2 = b.Knob("C2");
         b.Gen(n1, 2.0, kG);
         const int r1 = b.R(n1, n2, 10000.0, kR1);
         b.C(n2, 0, 22e-9, kC1);
         b.R(n2, n3, 47000.0, kR2);
         b.C(n3, 0, 10e-9, kC2);
         b.Probe("C1", n2);
         b.Probe("C2", n3);
         b.ProbeI("R1 current", r1, 10000.0f);
         b.Play(kSaw, kHold, 0.005f, 0.25f).Stereo(0, 1, 1.5f).Damp(kR1, 1.0f);
         return b.p;
      }

      // Series R-L into a C to ground: a resonant 2-pole lowpass.
      Preset RlcLowpass()
      {
         Build b("RLC Lowpass", 110.0, 0.35f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("R"), kL = b.Knob("L"), kC = b.Knob("C");
         b.Gen(n1, 2.0, kG);
         b.R(n1, n2, 100.0, kR);
         const int l = b.L(n2, n3, 0.1, kL);
         b.C(n3, 0, 1e-6, kC);
         b.Probe("C", n3);
         b.Probe("L in", n2);
         b.ProbeI("coil I", l, 316.0f);
         b.Play(kSaw, kHold, 0.005f, 0.3f).Stereo(0, 1, 1.5f).Damp(kR, 1.0f);
         return b.p;
      }

      // L and C in series to a load R: a bandpass, the load hears it.
      Preset SeriesLc()
      {
         Build b("Series LC", 160.0, 0.5f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("G"), kL = b.Knob("L"), kC = b.Knob("C"), kR = b.Knob("R");
         b.Gen(n1, 2.0, kG);
         const int l = b.L(n1, n2, 0.05, kL);
         b.C(n2, n3, 2.2e-6, kC);
         b.R(n3, 0, 120.0, kR);
         b.Probe("R", n3, 0, 1.0f);
         b.Probe("LC mid", n2, 0, 0.7f);
         b.ProbeI("coil I", l, 150.0f);
         b.Play(kSaw, kHold, 0.005f, 0.3f).Stereo(0, 1, 1.5f).Damp(kR, 1.0f);
         return b.p;
      }

      // A high-Q tank fed through 10k: it rings at its own pitch, a formant
      // riding on the saw (3rd harmonic of the note).
      Preset TankFilter()
      {
         Build b("Tank Filter", 110.0, 0.9f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("R"), kC = b.Knob("C"), kL = b.Knob("L"), kLoss = b.Knob("loss");
         b.Gen(n1, 3.0, kG);
         const int r = b.R(n1, n2, 10000.0, kR);
         b.C(n2, 0, 1e-6, kC);
         b.L(n2, n3, 0.233, kL);
         b.R(n3, 0, 2.0, kLoss);
         b.At(n3, 1.6f, 1.0f);
         b.Probe("tank", n2);
         b.Probe("coil", n3, 0, 120.0f);
         b.ProbeI("R current", r, 10000.0f);
         b.Play(kSaw, kHold, 0.01f, 0.3f).Stereo(0, 0, 2.0f).Damp(kLoss, 1.0f);
         return b.p;
      }

      Preset TwinTNotch()
      {
         Build b("Twin-T Notch", 220.0, 0.6f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node(), n4 = b.Node(), n5 = b.Node();
         const int kG = b.Knob("G"), kRin = b.Knob("R in"), kR = b.Knob("R tee"), kC = b.Knob("C tee"), kLoad = b.Knob("R load");
         b.Gen(n1, 2.0, kG);
         b.R(n1, n2, 1000.0, kRin);
         b.At(n3, 2.0f, -1.0f);
         b.At(n4, 3.0f, 0.0f);
         b.At(n5, 2.0f, 1.0f);
         b.R(n2, n3, 10000.0, kR); // R - R with 2C to ground
         b.R(n3, n4, 10000.0, kR);
         b.C(n3, 0, 32e-9, kC);
         b.Route(b.C(n2, n5, 16e-9, kC), kRouteLow); // C - C with R/2 to ground
         b.Route(b.C(n5, n4, 16e-9, kC), kRouteLow);
         b.R(n5, 0, 5000.0, kR);
         b.R(n4, 0, 100000.0, kLoad);
         b.Probe("out", n4);
         b.Probe("R tee", n3);
         b.Probe("C tee", n5);
         b.Play(kSaw, kHold, 0.005f, 0.3f).Stereo(0, 1, 1.5f).Damp(kLoad, -1.0f);
         return b.p;
      }

      Preset LcLadder()
      {
         Build b("LC Ladder", 220.0, 0.5f);
         const int n1 = b.Node();
         int nodes[6];
         for (int& n : nodes)
            n = b.Node();
         const int kG = b.Knob("G"), kRs = b.Knob("R src"), kL = b.Knob("L line"), kC = b.Knob("C line"), kLoad = b.Knob("R load");
         b.Gen(n1, 2.0, kG);
         b.R(n1, nodes[0], 600.0, kRs);
         for (int s = 0; s < 5; s++)
         {
            b.L(nodes[s], nodes[s + 1], 0.095, kL);
            b.C(nodes[s + 1], 0, 265e-9, kC);
         }
         b.R(nodes[5], 0, 600.0, kLoad);
         b.Probe("tap 1", nodes[1]);
         b.Probe("tap 3", nodes[3]);
         b.Probe("out", nodes[5]);
         b.Play(kSaw, kHold, 0.005f, 0.3f).Stereo(1, 2, 1.5f).Damp(kLoad, -1.0f);
         return b.p;
      }

      // ------------------------------------------------ struck circuits

      // A contact closes for a moment on a battery; the tank rings on its own.
      Preset LcTank()
      {
         Build b("LC Tank", 318.3, 0.9f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("R in"), kC = b.Knob("C"), kL = b.Knob("L"), kLoss = b.Knob("loss");
         b.Gen(n1, 5.0, kG);
         const int r = b.R(n1, n2, 4700.0, kR);
         b.C(n2, 0, 1e-6, kC);
         b.L(n2, n3, 0.25, kL);
         b.R(n3, 0, 4.0, kLoss);
         b.At(n3, 1.6f, 1.0f);
         b.Probe("tank", n2);
         b.Probe("coil", n3, 0, 120.0f);
         b.ProbeI("R in current", r, 4700.0f);
         b.Play(kStep, kTap, 0.001f, 1.5f).Stereo(0, 0, 0.5f).Damp(kLoss, 1.0f);
         return b.p;
      }

      // The proof's Thump: a 6 V step through 10 ohm into a tank whose coil is
      // lossy - about two cycles and it is over.
      Preset Thump()
      {
         Build b("Thump", 55.0, 0.2f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("R in"), kC = b.Knob("C"), kL = b.Knob("L"), kLoss = b.Knob("loss");
         b.Gen(n1, 6.0, kG);
         b.R(n1, n2, 10.0, kR);
         b.C(n2, 0, 16.7e-6, kC);
         b.L(n2, n3, 0.5, kL);
         b.R(n3, 0, 60.0, kLoss);
         b.At(n3, 1.6f, 1.0f);
         b.Probe("tank", n2);
         b.Probe("coil", n3, 0, 2.0f);
         b.Play(kStep, kTap, 0.0003f, 0.6f, 0.5f).Stereo(0, 0, 3.0f).Damp(kLoss, 1.0f);
         return b.p;
      }

      // Two tanks a little apart, coupled by a small C: the energy swings
      // between them (the two modes beat) - tank 1 left, tank 2 right.
      Preset CoupledTanks()
      {
         Build b("Coupled Tanks", 318.3, 0.8f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node(), n4 = b.Node(), n5 = b.Node();
         const int kG = b.Knob("G"), kK = b.Knob("couple"), kC = b.Knob("C"), kL1 = b.Knob("L1"), kL2 = b.Knob("L2"), kLoss = b.Knob("loss");
         b.Gen(n1, 5.0, kG);
         b.At(n3, 1.6f, 1.0f);
         b.At(n4, 2.8f, 0.0f);
         b.At(n5, 3.4f, 1.0f);
         b.R(n1, n2, 2200.0);
         b.C(n2, 0, 1e-6, kC);
         b.L(n2, n3, 0.25, kL1);
         b.R(n3, 0, 3.0, kLoss);
         b.C(n2, n4, 0.08e-6, kK);
         b.C(n4, 0, 1e-6, kC);
         b.L(n4, n5, 0.22, kL2);
         b.R(n5, 0, 3.0, kLoss);
         b.ProbeMix("near 1", n2, n4, 0.45f, 1.0f);
         b.ProbeMix("near 2", n4, n2, 0.45f, 1.0f);
         b.Probe("tank 1", n2);
         b.Probe("tank 2", n4);
         b.Play(kStep, kTap, 0.001f, 2.0f).Stereo(0, 1, 0.5f).Damp(kLoss, 1.0f);
         return b.p;
      }

      // Five tanks at bell partial ratios (hum, minor third, fifth, octave,
      // upper), struck by one hammer through small capacitors (a resistor
      // there would tie the tanks together once the contact lets go); two
      // pickups hear them with different weights, like two microphones.
      Preset Bell()
      {
         Build b("Bell", 400.0, 0.5f);
         const int n1 = b.Node();
         const int kG = b.Knob("G"), kS = b.Knob("hammer C"), kC = b.Knob("C"), kL = b.Knob("L"), kLoss = b.Knob("loss");
         b.At(n1, 0.0f, -1.0f);
         b.Gen(n1, 50.0, kG);
         b.R(n1, 0, 10000.0);
         static const double kRatio[5] = { 1.0, 1.183, 1.506, 2.0, 2.66 };
         static const double kDecay[5] = { 2.4, 1.8, 1.3, 0.9, 0.6 }; // seconds
         const double lh = 0.1;
         int tank[5];
         for (int k = 0; k < 5; k++)
         {
            const int t = b.Node(), m = b.Node();
            tank[k] = t;
            b.At(t, 1.0f + 1.3f * k, 0.0f);
            b.At(m, 1.6f + 1.3f * k, 1.0f);
            const double w = kTwoPi * 400.0 * kRatio[k];
            b.Route(b.C(n1, t, 10e-9, kS), kRouteVertical);
            b.C(t, 0, 1.0 / (w * w * lh), kC);
            b.L(t, m, lh, kL);
            b.R(m, 0, 2.0 * lh / kDecay[k], kLoss);
         }
         b.ProbeSum("pickup A", { tank[0], tank[2], tank[4], tank[3] }, { 1.0f, 0.8f, 0.6f, 0.3f }, 1.0f);
         b.ProbeSum("pickup B", { tank[0], tank[1], tank[3], tank[2] }, { 1.0f, 0.8f, 0.6f, 0.3f }, 1.6f);
         b.Probe("hum", tank[0]);
         b.Play(kStep, kTap, 0.0005f, 3.0f).Stereo(0, 1, 0.2f).Damp(kLoss, 1.0f);
         return b.p;
      }

      // A lumped line, open at both ends once the contact lets go: half-wave
      // modes, slightly inharmonic (a lumped line is dispersive) - a pluck.
      Preset LcLine()
      {
         Build b("LC Line", 110.0, 0.25f);
         const int n1 = b.Node();
         int nodes[12];
         for (int& n : nodes)
            n = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("R in"), kL = b.Knob("L line"), kC = b.Knob("C line"), kEnd = b.Knob("end R");
         b.Gen(n1, 5.0, kG);
         b.R(n1, nodes[0], 200.0, kR);
         for (int s = 0; s < 12; s++)
         {
            b.C(nodes[s], 0, 379e-9, kC);
            if (s < 11)
               b.L(nodes[s], nodes[s + 1], 0.379, kL);
         }
         b.R(nodes[11], 0, 47000.0, kEnd);
         // Differential pickups: the charge the touch leaves on the line (its
         // DC mode, the same at every node) cancels out.
         b.Probe("ends", nodes[0], nodes[11]);
         b.Probe("half", nodes[0], nodes[5]);
         b.Probe("inner", nodes[3], nodes[8]);
         b.Play(kStep, kTap, 0.001f, 1.5f).Stereo(0, 1, 0.3f).Damp(kEnd, -1.0f);
         return b.p;
      }

      // A 3 x 4 grid of caps joined by coils: a little membrane. Struck at a
      // corner, heard at two inner points.
      Preset LcMesh()
      {
         Build b("LC Mesh", 140.0, 0.3f);
         const int n1 = b.Node();
         int m[3][4];
         for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++)
            {
               m[i][j] = b.Node();
               b.At(m[i][j], 1.0f + 1.5f * j, 1.6f * i);
            }
         const int kG = b.Knob("G"), kR = b.Knob("R in"), kL = b.Knob("L mesh"), kC = b.Knob("C mesh"), kLoss = b.Knob("loss");
         b.Gen(n1, 5.0, kG);
         b.R(n1, m[0][0], 220.0, kR);
         for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++)
            {
               b.C(m[i][j], 0, 1e-6, kC);
               b.R(m[i][j], 0, 60000.0, kLoss);
               if (j < 3)
                  b.L(m[i][j], m[i][j + 1], 0.1, kL);
               if (i < 2)
                  b.L(m[i][j], m[i + 1][j], 0.1, kL);
            }
         // Differential pickups, as on the line: the common charge cancels.
         b.Probe("diagonal", m[0][0], m[2][3]);
         b.Probe("row", m[0][0], m[0][3]);
         b.Probe("column", m[0][0], m[2][0]);
         b.Play(kStep, kTap, 0.0005f, 1.8f).Stereo(0, 1, 0.3f).Damp(kLoss, -1.0f);
         return b.p;
      }

      // ------------------------------------------------ oscillators

      // An LC tank with a cubic negative resistance across it (van der Pol
      // 1927): it starts by itself and settles on a limit cycle.
      Preset VanDerPol()
      {
         Build b("Van der Pol", 283.5, 0.55f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("kick"), kPush = b.Knob("push"), kC = b.Knob("C"), kL = b.Knob("L"), kDamp = b.Knob("R par");
         b.Gen(n3, 1.0, kG);
         b.R(n3, n1, 47000.0);
         b.C(n1, 0, 1e-6, kC);
         b.L(n1, n2, 0.25, kL);
         b.R(n2, 0, 2.0);
         b.R(n1, 0, 10000.0, kDamp);
         b.At(n3, 0.0f, 0.0f);
         b.At(n1, 1.0f, 0.0f);
         b.At(n2, 1.7f, 1.0f);
         b.Neg(n1, 0, 3e-3, 1.0, kPush);
         // Tank voltage and coil current are 90 degrees apart; this pickup
         // hears both, half way between.
         b.Probe("tank", n1);
         b.ProbeMix("tank + coil", n1, n2, 250.0f, 0.7f);
         b.Probe("coil", n2, 0, 250.0f);
         b.Play(kStep, kHold, 0.005f, 0.3f).Stereo(0, 1, 0.0f).Damp(kDamp, -1.0f);
         return b.p;
      }

      // The same oscillator pushed by the note's saw: it locks, splits and
      // wanders around the forcing (its own pitch sits near 1.5x the note).
      Preset PushedVdp()
      {
         Build b("Pushed VdP", 110.0, 0.55f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("push R"), kPush = b.Knob("push"), kC = b.Knob("C"), kL = b.Knob("L"),
                   kDamp = b.Knob("R par");
         b.Gen(n3, 2.0, kG);
         b.R(n3, n1, 4700.0, kR);
         b.C(n1, 0, 1e-6, kC);
         const double natural = 1.5 * 110.0;
         b.L(n1, n2, 0.25 * (290.0 / natural) * (290.0 / natural), kL);
         b.R(n2, 0, 2.0);
         b.R(n1, 0, 10000.0, kDamp);
         b.At(n3, 0.0f, 0.0f);
         b.At(n1, 1.0f, 0.0f);
         b.At(n2, 1.7f, 1.0f);
         b.Neg(n1, 0, 3e-3, 1.0, kPush);
         // Tank voltage and coil current are 90 degrees apart; this pickup
         // hears both, half way between.
         b.Probe("tank", n1);
         b.ProbeMix("tank + coil", n1, n2, 250.0f, 0.7f);
         b.Probe("coil", n2, 0, 250.0f);
         b.Play(kSaw, kHold, 0.005f, 0.3f).Stereo(0, 1, 0.0f).Damp(kDamp, -1.0f);
         return b.p;
      }

      // Pearson-Anson relaxation oscillator: a supply charges C through R
      // until the lamp strikes, the lamp dumps C down to its maintaining
      // voltage and goes out. Heard through an output coupling capacitor.
      Preset NeonRelax()
      {
         Build b("Neon Relax", 108.8, 0.06f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node(), n4 = b.Node(), n5 = b.Node();
         const int kG = b.Knob("supply"), kR = b.Knob("R"), kC = b.Knob("C"), kS = b.Knob("strike");
         b.Gen(n1, 90.0, kG);
         // R must keep the supply current under the lamp's holding current, or
         // the lamp never goes out (as with a real one).
         b.R(n1, n2, 1e6, kR);
         b.C(n2, 0, 8.2e-9, kC);
         b.At(n3, 1.6f, 1.0f);
         b.At(n4, 2.4f, 0.0f);
         b.At(n5, 3.2f, -1.0f);
         b.Neon(n2, n3, 70.0, 55.0, 10000.0, kS);
         b.R(n3, 0, 100.0);
         b.C(n2, n4, 1e-9);
         b.R(n4, 0, 10e6);
         b.C(n2, n5, 100e-12); // a second, brighter coupling: high-passed near the note
         b.R(n5, 0, 4.7e6);
         // A relaxation oscillator twin with any tolerance runs at its own
         // rate, so the twins stay identical here (spread 0) and the sides
         // differ by what they hear: the coupled saw, and a brighter coupling.
         b.Probe("out", n4);
         b.Probe("bright", n5, 0, 2.0f);
         b.Probe("lamp I", n3, 0, 60.0f);
         b.Probe("cap", n2);
         b.Play(kStep, kHold, 0.005f, 0.2f).Stereo(0, 1, 0.0f).Follow(true, false).Damp(kR, 1.0f);
         return b.p;
      }

      // Chua's circuit with Kennedy's values: R sets the route from a
      // limit cycle through period doubling to the double scroll.
      Preset Chua()
      {
         Build b("Chua", 2200.0, 0.3f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node(), n4 = b.Node();
         const int kR = b.Knob("R"), kC1 = b.Knob("C1"), kC2 = b.Knob("C2"), kL = b.Knob("L"), kD = b.Knob("diode");
         b.At(n4, 0.0f, 0.0f);
         b.At(n1, 1.0f, 0.0f);
         b.At(n2, 2.2f, 0.0f);
         b.At(n3, 2.8f, 1.0f);
         b.Gen(n4, 0.5);
         b.R(n4, n1, 100000.0);
         b.C(n1, 0, 10e-9, kC1);
         b.C(n2, 0, 100e-9, kC2);
         b.R(n1, n2, 1800.0, kR);
         const int l = b.L(n2, n3, 18e-3, kL);
         b.R(n3, 0, 12.5);
         b.Chua(n1, 0, -0.757e-3, -0.409e-3, 1.08, 11.0, kD);
         // The attractor's own variables are nearly uncorrelated (and twins
         // with any tolerance part ways at once - it is chaos): each side
         // hears both capacitors, weighted its own way.
         b.ProbeMix("C1 side", n1, n2, 1.2f, 1.0f);
         b.ProbeMix("C2 side", n2, n1, 0.05f, 5.0f);
         b.Probe("C1", n1);
         b.ProbeI("coil I", l, 400.0f);
         b.Play(kStep, kHold, 0.01f, 0.3f).Stereo(0, 1, 0.0f).Damp(kR, 1.0f);
         return b.p;
      }

      // ------------------------------------------------ drums

      // Multiple-feedback bandpass around an op-amp (R1 in, C and C bridged
      // by R2 from the output, R3 to ground) - the 808-style resonator - hit
      // by a short contact on a battery. f0 and Q from the textbook MFB
      // design equations with C1 = C2.
      Preset BridgedT(const char* name, double f0, double q, double c, float gain, float release)
      {
         Build b(name, f0, gain);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node(), n4 = b.Node();
         const int kG = b.Knob("G"), kR1 = b.Knob("R in"), kR3 = b.Knob("R tune"), kR2 = b.Knob("R decay"), kC = b.Knob("C");
         const double r1 = 10000.0;
         const double r2 = q / (3.14159265358979 * f0 * c);
         const double rp = r2 / (4.0 * q * q);
         const double r3 = 1.0 / (1.0 / rp - 1.0 / r1);
         b.Gen(n1, 1.0, kG);
         b.R(n1, n2, r1, kR1);
         b.R(n2, 0, r3, kR3);
         b.C(n2, n3, c, kC);
         b.Route(b.C(n2, n4, c, kC), kRouteOver);
         b.Route(b.R(n3, n4, r2, kR2), kRouteUnder);
         b.Op(0, n3, n4);
         b.Probe("out", n4);
         b.Probe("bridge", n2, 0, 6.0f);
         b.Play(kStep, kTap, 0.0005f, release, 0.3f).Stereo(0, 0, 1.5f).Follow(true, false).Damp(kR2, -1.0f);
         return b.p;
      }

      // ------------------------------------------------ buzzers

      // A coil hammered by a pulse with a flyback diode across it, coupled
      // into a fixed membrane tank.
      Preset Buzzer()
      {
         Build b("Buzzer", 110.0, 0.2f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node(), n4 = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("R"), kL = b.Knob("coil"), kK = b.Knob("couple"), kM = b.Knob("membrane");
         b.Gen(n1, 12.0, kG);
         b.R(n1, n2, 47.0, kR);
         b.L(n2, 0, 0.5, kL);
         b.D(0, n2);
         b.C(n2, n3, 100e-9, kK);
         b.Fixed(b.C(n3, 0, 22e-9, kM));
         b.Fixed(b.L(n3, n4, 0.2));
         b.R(n4, 0, 30.0);
         b.At(n4, 2.6f, 1.0f);
         b.ProbeMix("coil side", n2, n3, 1.0f, 0.5f);
         b.ProbeMix("membrane side", n3, n2, 0.3f, 0.5f);
         b.Probe("coil", n2, 0, 0.5f);
         b.Probe("membrane", n3, 0, 0.5f);
         b.Play(kPulse, kHold, 0.002f, 0.15f).Stereo(0, 2, 1.5f).Damp(kR, 1.0f);
         return b.p;
      }

      // A telephone-style ringer: the ring pulse drives the coil, the
      // hammer couples into two gongs a little apart - one each side.
      Preset Ringer()
      {
         Build b("Ringer", 25.0, 0.04f);
         const int n1 = b.Node(), n2 = b.Node(), n3 = b.Node(), n4 = b.Node(), n5 = b.Node(), n6 = b.Node();
         const int kG = b.Knob("G"), kR = b.Knob("R"), kL = b.Knob("coil"), kK = b.Knob("hammer"), kLoss = b.Knob("gong loss");
         b.At(n5, 2.6f, 1.0f);
         b.At(n4, 3.2f, -1.0f);
         b.At(n6, 3.8f, 0.0f);
         b.Gen(n1, 40.0, kG);
         b.R(n1, n2, 1000.0, kR);
         b.L(n2, 0, 2.0, kL);
         b.C(n2, n3, 47e-9, kK);
         b.Fixed(b.C(n3, 0, 47e-9));
         b.Fixed(b.L(n3, n5, 0.12));
         b.R(n5, 0, 2.0, kLoss);
         b.C(n2, n4, 47e-9, kK);
         b.Fixed(b.C(n4, 0, 39e-9));
         b.Fixed(b.L(n4, n6, 0.12));
         b.R(n6, 0, 2.0, kLoss);
         b.ProbeMix("near 1", n3, n4, 0.5f, 1.0f);
         b.ProbeMix("near 2", n4, n3, 0.5f, 1.0f);
         b.Probe("coil", n2, 0, 0.05f);
         b.Play(kPulse, kHold, 0.002f, 0.8f).Stereo(0, 1, 0.3f).Follow(true, false).Damp(kLoss, 1.0f);
         return b.p;
      }

      const std::vector<Preset>& Table()
      {
         static const std::vector<Preset> table = []
         {
            std::vector<Preset> t;
            t.push_back(DiodeClipper());
            t.push_back(Fuzz());
            t.push_back(RcLowpass());
            t.push_back(RlcLowpass());
            t.push_back(SeriesLc());
            t.push_back(TankFilter());
            t.push_back(TwinTNotch());
            t.push_back(LcLadder());
            t.push_back(LcTank());
            t.push_back(Thump());
            t.push_back(CoupledTanks());
            t.push_back(Bell());
            t.push_back(LcLine());
            t.push_back(LcMesh());
            t.push_back(VanDerPol());
            t.push_back(PushedVdp());
            t.push_back(NeonRelax());
            t.push_back(Chua());
            t.push_back(BridgedT("BT Kick", 50.0, 30.0, 100e-9, 0.25f, 0.9f));
            t.push_back(BridgedT("BT Tom", 140.0, 18.0, 33e-9, 0.25f, 0.7f));
            t.push_back(BridgedT("Rim", 500.0, 5.0, 10e-9, 0.25f, 0.25f));
            t.push_back(Buzzer());
            t.push_back(Ringer());
            return t;
         }();
         return table;
      }
   }

   int NumPresets() { return (int)Table().size(); }

   const Preset& GetPreset(int index)
   {
      const auto& t = Table();
      if (index < 0 || index >= (int)t.size())
         index = 0;
      return t[(size_t)index];
   }

   int DefaultPreset() { return 0; }

   int FindPreset(const char* name)
   {
      const auto& t = Table();
      for (size_t i = 0; i < t.size(); i++)
         if (std::strcmp(t[i].name, name) == 0)
            return (int)i;
      return -1;
   }
}
