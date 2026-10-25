#pragma once

#include <cstdint>

#include "audio/DspMath.h"
#include "audio/SynthModes.h"

// Circuit Synth (Turbo 0.73): a synth that makes its sound by simulating small
// electrical circuits in real time. Clean room, MIT: written from textbook
// circuit simulation only - Modified Nodal Analysis (Ho, Ruehli & Brennan
// 1975; any SPICE text, e.g. Vlach & Singhal), trapezoidal companion models
// for C and L, Newton-Raphson for the nonlinear parts (Shockley diode, a
// gas-discharge lamp with strike / maintain hysteresis, Chua's piecewise-
// linear diode (Chua 1983, Kennedy 1992), a cubic negative resistance (van
// der Pol 1927), a saturating op-amp), and the classic schematics the presets
// are drawn from (RC / RLC filters, LC tanks, ladders, lines and meshes,
// Twin-T, the multiple-feedback "bridged-T" drum resonator).
//
// Solver: the linear part of a circuit (R, C, L companions, generator Norton
// equivalents, op-amp output loads) is one fixed matrix, LU-factored only when
// a part value changes (key, glide, knob, LFO - at most once per control
// chunk), never when only the sources move. The nonlinear parts sit on a few
// "ports"; the linear network's response to a unit current at each port is
// precomputed with the same LU, so Newton runs in the port space only (1-2
// unknowns for every preset) - the per-sample cost is one triangular solve.
//
// Stereo is built in, not added on: each voice runs its circuit twice (L and
// R) with small fixed per-part tolerances (SPREAD, like real parts), each side
// listens at its own probe point (PROBE A / B), WIDTH is mid/side on the
// result and POLY voices are panned across the field.
//
// Audio thread: no allocation, no locks. Every array is fixed-size.
namespace CircuitDsp
{
   constexpr int kMaxNodes = 20; // non-ground nodes
   constexpr int kMaxParts = 64;
   constexpr int kMaxPorts = 4; // nonlinear ports
   constexpr int kMaxPortParts = 4;
   constexpr int kMaxProbes = 6;
   constexpr int kMaxKnobs = 8;
   constexpr int kMaxVoices = 8;
   constexpr int kMaxChunk = 64; // frames per Engine::Render call
   constexpr int kMaxOver = 8;

   enum PartKind : uint8_t
   {
      kResistor,
      kCapacitor,
      kInductor,
      kDiode,     // value = Is, p1 = emission coefficient n
      kNeon,      // value = strike V, p1 = maintain V, p2 = lit resistance
      kChua,      // value = Ga (inner slope), p1 = Gb, p2 = E1, p3 = E2 (outer, slope turns positive)
      kNegRes,    // cubic negative resistance i = G (v^3 / Vs^2 - v): value = G, p1 = Vs
      kOpAmp,     // a = +in, b = -in, c = out: value = open-loop gain, p1 = Rout, p2 = rail
      kGenerator, // a = +, b = -: value = volts, p1 = internal resistance
   };

   enum Wave { kSaw, kPulse, kSine, kNoise, kStep, kNumWaves };
   enum LfoShape { kLfoSine, kLfoTriangle, kLfoSquare, kLfoRandom, kNumLfoShapes };
   enum LfoTarget { kToTune, kToPower, kToDamp, kToLevel, kToCutoff, kNumLfoTargets };
   enum Trigger { kTap, kHold };
   // How the schematic draws a two-node part: an L-shaped wire whose
   // horizontal leg runs at the upper node's height (auto) or the lower one's,
   // the symbol forced onto the vertical leg, or a detour over / under.
   enum Route : uint8_t { kRouteAuto, kRouteLow, kRouteVertical, kRouteOver, kRouteUnder };
   enum Mode { kMono, kPoly };

   struct PartDef
   {
      PartKind kind = kResistor;
      int8_t a = 0, b = 0, c = 0; // nodes, 0 = ground
      double value = 0.0;
      double p1 = 0.0, p2 = 0.0, p3 = 0.0;
      int8_t knob = -1;   // the knob that scales this part, -1 = none
      bool tracks = true; // a C or L that follows the key (false: fixed, e.g. a buzzer membrane)
      uint8_t route = kRouteAuto; // schematic only
   };

   struct ProbeDef
   {
      char name[16] = {};
      // A pickup: the weighted sum of up to four node voltages (a single
      // point, a difference, or several points heard at once - it draws no
      // current, so it never loads the circuit), or the current through one
      // part (C, L or R).
      bool current = false;
      int8_t node[4] = {};
      float weight[4] = {};
      int8_t part = -1;
      float gain = 1.0f;
   };

   struct KnobDef
   {
      char label[12] = {};
      int8_t part = -1; // the part whose value the knob shows
   };

   struct Preset
   {
      char name[24] = {};
      int numNodes = 0, numParts = 0, numProbes = 0, numKnobs = 0;
      PartDef parts[kMaxParts];
      ProbeDef probes[kMaxProbes];
      KnobDef knobs[kMaxKnobs];
      double refHz = 220.0; // the note at which the circuit plays its drawn values
      float gain = 1.0f;    // output level, volts -> full scale
      // What selecting the preset sets on the node.
      int wave = kSaw, trigger = kHold;
      bool cFollow = true, lFollow = true;
      float contactMs = 1.0f, attack = 0.003f, release = 0.4f, spread = 1.5f;
      int probeA = 0, probeB = 1;
      int dampKnob = -1;      // the knob DAMP moves
      float dampSign = 1.0f;  // +1: raising that knob adds loss
      // Schematic: each node's place on a grid (x right, y down; ground is
      // drawn one unit under the node a part goes to ground from).
      float nodeX[kMaxNodes + 1] = {}, nodeY[kMaxNodes + 1] = {};
   };

   int NumPresets();
   const Preset& GetPreset(int index);
   int DefaultPreset();
   int FindPreset(const char* name); // -1 if none

   // The multiplier a knob trim t (-1..1) applies to a part of this kind.
   double KnobScale(PartKind kind, double t);
   // "2.2 kohm", "56 nF", "0.50 H", "2.0 V" ... for the readout strip.
   void FormatPartValue(const PartDef& part, double scale, char* out, int size);

   // Everything the engine reads, constant over one Render call.
   struct Params
   {
      int preset = 0, wave = kSaw, mode = kMono, trigger = kHold;
      bool cFollow = true, lFollow = true;
      float pitch = 0.0f;     // semitones
      float glide = 0.0f;     // seconds
      float contactMs = 1.0f; // source switch-on / off edge
      float attack = 0.003f, release = 0.4f;
      float lfoRate = 1.0f, lfoDepth = 0.0f;
      int lfoShape = kLfoSine, lfoTarget = kToTune;
      int probeA = 0, probeB = 1;
      float width = 1.0f;  // 0 mono, 1 as built, 1.5 wider
      float spread = 1.5f; // percent part tolerance between the L and R circuits
      float level = 0.8f;
      float knob[kMaxKnobs] = {};
      // Output filter, the same types as the other synths (SynthModes):
      // off, lp / hp 12-36, bp / notch 12-24, comb + / -.
      int filterType = SynthModes::kFilterOff;
      float cutoff = 8000.0f; // Hz
      float reso = 0.2f;      // 0..1
      float filterKey = 0.0f; // 0..1: the cutoff follows the last note (1 = fully, from 220 Hz)
   };

   // One circuit instance: values, companion states, the factored matrix.
   struct Circuit
   {
      struct Part
      {
         PartKind kind = kResistor;
         int a = 0, b = 0, c = 0;
         double value = 0.0, p1 = 0.0, p2 = 0.0, p3 = 0.0;
         double v = 0.0, i = 0.0; // companion state (C, L), last voltage / current
         double g = 0.0;          // companion / Norton conductance, set by Factor
         bool lit = false;        // neon
      };
      struct Port
      {
         int ca = 0, cb = 0, ia = 0, ib = 0; // control pair, injection pair
         int count = 0;
         int part[kMaxPortParts] = {};
         double sign[kMaxPortParts] = {};
         bool opamp = false;
         double v = 0.0, vPrev = 0.0; // last two solutions (Newton's predicted start)
      };

      int n = 0; // matrix dimension (nodes)
      int numParts = 0, numPorts = 0;
      Part parts[kMaxParts];
      Port ports[kMaxPorts];
      // Index lists so the per-step loops touch only the parts they need.
      int numReactive = 0, numGens = 0, numNeons = 0;
      int8_t reactive[kMaxParts] = {}, gens[kMaxParts] = {}, neons[kMaxParts] = {};
      double lu[kMaxNodes * kMaxNodes] = {};
      int piv[kMaxNodes] = {};
      double zcol[kMaxPorts][kMaxNodes + 1] = {};
      double Z[kMaxPorts][kMaxPorts] = {};
      double x[kMaxNodes + 1] = {}; // node voltages, x[0] = ground
      double rhs[kMaxNodes + 1] = {};
      double dt = 1.0 / 192000.0;
      double contact = 0.0; // the generators' switch: 0 open .. 1 closed (their resistance / contact)
      bool dirty = true;
      int newtonIters = 0; // last step, for the CPU report

      void Load(const Preset& p);
      void Reset();
      // Sets a part's value; marks the matrix dirty when the change affects it.
      void SetValue(int part, double value);
      // Closes / opens every generator's contact (quantized: each new level
      // refactors the matrix, so an edge costs a few dozen factorizations).
      void SetContact(double c);
      // One time step. `source` (-1..1, times each generator's volts) drives
      // every generator. False when the state blew up (it was reset).
      bool Step(double source);
      double Probe(const ProbeDef& probe) const;

   private:
      void Factor();
      void SolveLu(double* b) const; // in place, b[1..n]
      void PortEval(const Port& port, double v, double& inj, double& dinj) const;
   };

   class Engine
   {
   public:
      void Prepare(double sampleRate);
      void Reset();
      void NoteOn(int note, float velocity, int voiceId);
      void NoteOff(int note, int voiceId);
      void AllNotesOff();
      // Takes the preset, mode and play settings: call before the note
      // events of a chunk (Render calls it too), so a preset change never
      // swallows the notes that arrive with it.
      void Apply(const Params& p);
      // frames <= kMaxChunk. Writes (not adds) L and R.
      void Render(float* outL, float* outR, int frames, const Params& p);

      int ActiveVoices() const;
      int MaxVoices() const { return mMaxVoices; }
      int Oversampling() const { return mOver; }
      int LoadedPreset() const { return mPreset; }
      double InternalRate() const { return mRate * mOver; }
      // Test access: a voice's circuits (twin 0 = L, 1 = R).
      const Circuit& VoiceCircuit(int voice, int twin) const { return mVoices[voice].tw[twin]; }

   private:
      struct Voice
      {
         bool active = false, gate = false;
         int note = -1, voiceId = 0;
         float velocity = 0.0f;
         uint64_t age = 0;
         double hz = 110.0, targetHz = 110.0;
         float pan = 0.0f;
         double phase = 0.0, contact = 0.0, env = 0.0;
         int tapRemaining = 0;
         uint32_t noise = 22222u;
         bool twinLive = false; // twin 1 is being simulated (spread > 0)
         Circuit tw[2];
         float offset[2][kMaxParts] = {}; // fixed per-part tolerance, -1..1
      };

      void LoadPreset(int index);
      void DesignDecimator();
      void UpdateValues(Voice& v, const Params& p, double tuneScale, double dampAdd);
      int AllocateVoice();
      void Trigger(Voice& v, int note, float velocity, int voiceId, bool fresh);
      double NoteHz(int note, float pitch) const;
      int TapSamples(double hz) const;
      float LfoValue(int shape);

      double mRate = 48000.0;
      int mOver = 4;
      int mPreset = -1;
      int mMaxVoices = kMaxVoices;
      int mMode = kMono;
      int mTrigger = kHold;
      float mPitch = 0.0f, mGlide = 0.0f, mContactMs = 1.0f;
      Voice mVoices[kMaxVoices];
      uint64_t mNextAge = 1;
      int mPanIndex = 0;
      double mLastHz = 110.0;
      // Mono note stack (last-note priority).
      int mStack[16] = {};
      int mStackIds[16] = {};
      float mStackVel[16] = {};
      int mStackSize = 0;

      double mTime = 0.0; // seconds rendered, for the twins' drift
      double mLfoPhase = 0.0;
      float mLfoHeld = 0.0f;
      uint32_t mLfoSeed = 12345u;

      // Decimator: 63-tap Blackman-windowed sinc at the internal rate.
      static constexpr int kTaps = 63;
      float mFir[kTaps] = {};
      float mHistL[kTaps * 2] = {}, mHistR[kTaps * 2] = {};
      int mHistPos = 0;
      double mDcXL = 0.0, mDcYL = 0.0, mDcXR = 0.0, mDcYR = 0.0, mDcK = 0.999;
      float mBufL[kMaxChunk * kMaxOver] = {}, mBufR[kMaxChunk * kMaxOver] = {};
      DspMath::TptSvf mFilter[2][SynthModes::kMaxFilterStages];
      DspMath::CombFilter mComb[2];
   };
}
