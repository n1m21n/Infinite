#include "app/AppShared.h"
#include "core/Expression.h"
#include "core/Transport.h"
#include "core/bci/BciDspEngine.h"
#include "core/bci/BciTypes.h"
#include "core/bci/SyntheticCortex.h"
#include "core/field/BackendRegister.h"
#include "core/field/FieldIR.h"
#include "core/field/FieldLex.h"
#include "core/field/FieldParse.h"
#include "core/field/GlslBackend.h"
#include "core/field/SampleRuntime.h"
#include "nodes/BrainSculptNode.h"
#include "nodes/BrainSourceNode.h"
#include "nodes/ModulatorNodes.h"
#include "platform/AppPaths.h"
#include "audio/AudioNode.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace app
{
namespace
{
   struct CheckCounter
   {
      int passed = 0;
      int failed = 0;
      void Expect(bool cond, const char* msg)
      {
         if (cond)
         {
            ++passed;
         }
         else
         {
            ++failed;
            std::printf("[BCI_SELFTEST] FAIL: %s\n", msg);
         }
      }
   };

   struct FixedMod : IModulator
   {
      float v = 0.0f;
      float Value01() override { return v; }
   };
}

bool RunBciSelftest()
{
   CheckCounter ck;
   std::printf("[BCI_SELFTEST] running biophysical DSP, LogicGate, Take, BrainSculpt & Field checks...\n");

   // 1. SyntheticCortex + 1/f^chi + Cortical Band Separation (CalmAlpha vs DeepFocus)
   {
      Bci::SyntheticCortex cortex;
      cortex.SetSpontaneousBlinks(false);
      cortex.SetAutoDemoGestures(false);

      Bci::BciDspEngine dsp;
      dsp.Reset(256.0);
      dsp.SetSmoothing(0.25f);

      Bci::TelemetrySnapshot snapCalm{}, snapFocus{};
      float pinsCalm[Bci::kOutputPinCount] = {};
      float pinsFocus[Bci::kOutputPinCount] = {};
      std::vector<Bci::SampleFrame> batch;

      cortex.SetPreset(Bci::SyntheticPreset::CalmAlpha);
      for (int step = 0; step < 64; ++step) // 4 seconds @ 16 samples/step
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pinsCalm, snapCalm);
      }

      cortex.SetPreset(Bci::SyntheticPreset::DeepFocus);
      for (int step = 0; step < 64; ++step)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pinsFocus, snapFocus);
      }

      std::printf("  [1] CalmAlpha: alpha=%.3f arousal=%.3f slope=%.2f | DeepFocus: alpha=%.3f arousal=%.3f slope=%.2f\n",
                  pinsCalm[BrainSourceNode::kAlpha], pinsCalm[BrainSourceNode::kArousal], snapCalm.aperiodicSlope,
                  pinsFocus[BrainSourceNode::kAlpha], pinsFocus[BrainSourceNode::kArousal], snapFocus.aperiodicSlope);
      ck.Expect(pinsCalm[BrainSourceNode::kAlpha] > pinsFocus[BrainSourceNode::kAlpha] + 0.05f,
                "CalmAlpha has higher 1/f-detrended Alpha than DeepFocus");
      ck.Expect(pinsFocus[BrainSourceNode::kArousal] > pinsCalm[BrainSourceNode::kArousal] + 0.05f,
                "DeepFocus has higher Arousal than CalmAlpha");
      ck.Expect(snapCalm.aperiodicSlope > snapFocus.aperiodicSlope,
                "CalmAlpha has steeper 1/f^chi slope than DeepFocus");
   }

   // 2. Orthogonal Somatic Demixing (JawLeft vs JawRight, WinkLeft, Intentional Blink + Artifact Blanking)
   {
      Bci::SyntheticCortex cortex;
      cortex.SetSpontaneousBlinks(false);
      cortex.SetAutoDemoGestures(false);
      cortex.SetPreset(Bci::SyntheticPreset::CalmAlpha);

      Bci::BciDspEngine dsp;
      dsp.Reset(256.0);
      dsp.SetSmoothing(0.15f);
      dsp.SetStartleGuard(false);

      Bci::TelemetrySnapshot snap{};
      float pins[Bci::kOutputPinCount] = {};
      std::vector<Bci::SampleFrame> batch;

      // Warm up 2.0s CalmAlpha
      for (int s = 0; s < 32; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
      }
      const float alphaBeforeBlink = pins[BrainSourceNode::kAlpha];

      // Left Jaw Clench
      cortex.TriggerGesture(Bci::GestureKind::JawLeft);
      float peakJawL = 0.0f, peakJawR = 0.0f;
      for (int s = 0; s < 5; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         peakJawL = std::max(peakJawL, pins[BrainSourceNode::kJawLeft]);
         peakJawR = std::max(peakJawR, pins[BrainSourceNode::kJawRight]);
      }
      ck.Expect(peakJawL > 0.45f && peakJawL > peakJawR * 2.0f,
                "JawLeft isolates TP9 without spilling into TP10");

      // Settle & Right Jaw Clench
      for (int s = 0; s < 8; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
      }
      cortex.TriggerGesture(Bci::GestureKind::JawRight);
      peakJawL = 0.0f; peakJawR = 0.0f;
      for (int s = 0; s < 5; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         peakJawL = std::max(peakJawL, pins[BrainSourceNode::kJawLeft]);
         peakJawR = std::max(peakJawR, pins[BrainSourceNode::kJawRight]);
      }
      ck.Expect(peakJawR > 0.45f && peakJawR > peakJawL * 2.0f,
                "JawRight isolates TP10 without spilling into TP9");

      // Intentional Blink + Artifact Blanking check
      for (int s = 0; s < 8; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
      }
      cortex.TriggerGesture(Bci::GestureKind::Blink);
      float peakBlink = 0.0f;
      for (int s = 0; s < 8; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         peakBlink = std::max(peakBlink, pins[BrainSourceNode::kBlink]);
      }
      const float alphaDrift = std::fabs(pins[BrainSourceNode::kAlpha] - alphaBeforeBlink);
      std::printf("  [2] Intentional Blink peak=%.2f, Alpha drift during blink=%.3f\n", peakBlink, alphaDrift);
      ck.Expect(peakBlink > 0.9f, "Intentional Blink fires discrete kBlink pulse");
      ck.Expect(alphaDrift < 0.18f, "Artifact Blanking preserves cortical Alpha during EOG blink");
   }

   // 3. Gap 1: Reflex vs Intentional Blink Discrimination + Gap 3: Startle Mask
   {
      Bci::SyntheticCortex cortex;
      cortex.SetSpontaneousBlinks(false);
      cortex.SetAutoDemoGestures(false);
      Bci::BciDspEngine dsp;
      dsp.Reset(256.0);
      dsp.SetBlinkMode(Bci::BlinkMode::IntentionalOrDouble);
      dsp.SetStartleGuard(false);

      Bci::TelemetrySnapshot snap{};
      float pins[Bci::kOutputPinCount] = {};
      std::vector<Bci::SampleFrame> batch;

      for (int s = 0; s < 16; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
      }

      // Single 125 ms vegetative blink must be suppressed
      cortex.TriggerVegetativeBlink();
      float vegPeak = 0.0f;
      for (int s = 0; s < 8; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         vegPeak = std::max(vegPeak, pins[BrainSourceNode::kBlink]);
      }
      ck.Expect(vegPeak < 0.5f, "Gap 1: Single vegetative blink suppressed in IntentionalOrDouble mode");

      // Double vegetative blink within 380 ms must fire
      for (int s = 0; s < 12; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
      }
      cortex.TriggerVegetativeBlink();
      for (int s = 0; s < 3; ++s) // ~187 ms later
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
      }
      cortex.TriggerVegetativeBlink();
      float dblPeak = 0.0f;
      for (int s = 0; s < 8; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         dblPeak = std::max(dblPeak, pins[BrainSourceNode::kBlink]);
      }
      ck.Expect(dblPeak > 0.9f, "Gap 1: Double-blink fires kBlink");

      // Gap 3: Efference-Copy Startle Mask suppresses spikes right after sudden output transient
      dsp.SetStartleGuard(true);
      Bci::NotifyOutputTransient(0.65f, 0.0f);
      cortex.TriggerGesture(Bci::GestureKind::JawLeft);
      float maskedJaw = 0.0f;
      for (int s = 0; s < 3; ++s)
      {
         cortex.Generate(16, 256.0, batch);
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         maskedJaw = std::max(maskedJaw, pins[BrainSourceNode::kJawLeft]);
      }
      ck.Expect(snap.startleMasked && maskedJaw < 0.05f,
                "Gap 3: Startle mask suppresses self-triggered transient echo");
   }

   // 4. LogicGateNode: Chatter Immunity, Toggle, SRLatch, Presence Guard, Quantize & Idempotency
   {
      Transport& tr = Transport::Instance();
      tr.NotifyAudioEngineStopped();
      tr.SetOfflineMode(true, 48000.0);
      tr.SetTempo(120.0f); // 2 beats/sec
      tr.Seek(0.0);
      tr.SetPlaying(true);

      FixedMod inA, inB, pres;
      LogicGateNode gate;
      gate.inputA = &inA;
      gate.inputB = &inB;
      gate.presenceIn = &pres;
      gate.highThresh = 0.65f;
      gate.lowThresh = 0.45f;
      gate.holdMs = 50.0f;
      gate.minPresence = 0.40f;
      pres.v = 0.9f;

      // Chatter immunity inside hysteresis band
      gate.op = LogicGateNode::kOpSchmittGate;
      int transitions = 0;
      float prev = 0.0f;
      for (int f = 0; f < 40; ++f)
      {
         tr.SetOfflineVideoTime(f * 0.016);
         inA.v = (f % 2 == 0) ? 0.62f : 0.48f; // oscillates inside [0.45, 0.65]
         const float out = gate.Value01();
         // Idempotency check: 8 calls on same tick return identical value
         for (int rep = 0; rep < 8; ++rep)
            ck.Expect(gate.Value01() == out, "LogicGateNode Value01() idempotent within frame");
         if (f > 0 && out != prev)
            ++transitions;
         prev = out;
      }
      ck.Expect(transitions == 0, "LogicGateNode immune to boundary chatter inside hysteresis band");

      // Toggle + Presence guard interlock
      gate.op = LogicGateNode::kOpToggle;
      gate.holdMs = 20.0f;
      gate.refractoryMs = 50.0f;
      inA.v = 0.0f;
      tr.SetOfflineVideoTime(1.0);
      ck.Expect(gate.Value01() == 0.0f, "Toggle starts at 0");
      inA.v = 0.85f;
      tr.SetOfflineVideoTime(1.02);
      tr.SetOfflineVideoTime(1.05); // past 20 ms hold
      ck.Expect(gate.Value01() == 1.0f, "Toggle flips 0 -> 1 after holdMs");
      // Drop presence below minPresence -> Toggle must stay frozen at 1 even if inputA drops/spikes
      pres.v = 0.1f;
      inA.v = 0.0f;
      tr.SetOfflineVideoTime(1.10);
      inA.v = 0.90f;
      tr.SetOfflineVideoTime(1.15);
      ck.Expect(gate.Value01() == 1.0f, "Presence guard freezes Toggle state on contact loss");

      // Quantized RisingEdge to 1/4 Beat (at 120 BPM, 1 beat = 0.5 s)
      pres.v = 0.95f;
      gate.op = LogicGateNode::kOpRisingEdge;
      gate.quantize = LogicGateNode::kQuantQuarter;
      gate.holdMs = 0.0f;
      gate.pulseMs = 40.0f;
      inA.v = 0.0f;
      tr.SetOfflineVideoTime(2.05); // beat 4.10
      gate.Value01();
      inA.v = 0.90f;
      tr.SetOfflineVideoTime(2.10); // beat 4.20 (armed mid-beat, must NOT fire yet)
      const float preBeat = gate.Value01();
      tr.SetOfflineVideoTime(2.51); // beat 5.02 (crossed beat 5.0 boundary)
      const float atBeat = gate.Value01();
      ck.Expect(preBeat == 0.0f && atBeat == 1.0f, "Gap 2: Quantized RisingEdge arms mid-beat and fires on beat grid");

      tr.SetOfflineMode(false);
      tr.SetPlaying(false);
      tr.Seek(0.0);
   }

   // 5. Gap 4 (Take Baking), Breakthrough 1 (BrainSculptNode IPredictor), Gap 5 (brain.* Expression/Field), Gap 6 (Profile JSON)
   {
      // Profile JSON round-trip in temp dir
      Bci::BciDspEngine dsp;
      Bci::CalibrationProfile prof;
      prof.valid = true;
      prof.iafHz = 10.75f;
      prof.medianBlinkUv = 148.0f;
      dsp.SetProfile(prof);
      const std::string profPath = AppPaths::TempDir() + "/infinite_bci_selftest_profile.json";
      ck.Expect(dsp.SaveProfile(profPath), "Gap 6: Save calibration profile JSON");
      Bci::BciDspEngine dsp2;
      ck.Expect(dsp2.LoadProfile(profPath) && std::fabs(dsp2.Profile().iafHz - 10.75f) < 0.01f,
                "Gap 6: Load calibration profile JSON preserves IAF");

      // BrainSourceNode Take Baking & VisitParams round-trip
      Transport& tr = Transport::Instance();
      tr.NotifyAudioEngineStopped();
      tr.SetOfflineMode(true, 48000.0);
      tr.SetTempo(120.0f);
      tr.Seek(0.0);
      tr.SetPlaying(true);

      BrainSourceNode recNode;
      recNode.StartRecordingTake();
      for (int step = 0; step < 8; ++step)
      {
         tr.SetOfflineVideoTime(step * 0.05);
         recNode.CookIfNeeded(step + 1);
      }
      recNode.StopRecordingTake();
      ck.Expect(recNode.HasTake() && recNode.TakeFrameCount() > 0, "Gap 4: Take recorder captured beat-locked frames");

      std::vector<std::pair<std::string, std::string>> saved;
      Patch::SaveParams(&recNode, saved);
      BrainSourceNode playNode;
      Patch::LoadParams(&playNode, saved);
      ck.Expect(playNode.HasTake() && playNode.playTake, "Gap 4: Take data round-trips via VisitParams");
      tr.SetOfflineVideoTime(0.15);
      const float expectedA = recNode.Value(BrainSourceNode::kArousal);
      const float replayedA = playNode.Value(BrainSourceNode::kArousal);
      ck.Expect(std::fabs(expectedA - replayedA) < 1e-5f, "Gap 4: Offline take playback is deterministic");

      // Breakthrough 1: BrainSculptNode (IPredictor) Shift-grab anchor interpolation
      BrainSculptNode sculpt;
      const ParamKey pk{ 42ULL, 3 };
      float calmVec[24] = {};
      calmVec[BrainSourceNode::kArousal] = 0.15f;
      calmVec[BrainSourceNode::kValence] = 0.70f;
      calmVec[BrainSourceNode::kEntropy] = 0.20f;
      calmVec[BrainSourceNode::kCoherence] = 0.85f;
      Bci::PublishBrainGlobals(calmVec, true);
      sculpt.OnRelease(pk, 0.15f, 0.0f);

      float focusVec[24] = {};
      focusVec[BrainSourceNode::kArousal] = 0.88f;
      focusVec[BrainSourceNode::kValence] = 0.45f;
      focusVec[BrainSourceNode::kEntropy] = 0.72f;
      focusVec[BrainSourceNode::kCoherence] = 0.35f;
      focusVec[BrainSourceNode::kBrowTension] = 0.60f;
      Bci::PublishBrainGlobals(focusVec, true);
      sculpt.OnRelease(pk, 0.85f, 0.0f);

      Bci::PublishBrainGlobals(calmVec, true);
      sculpt.Tick(1, 0.5);
      const float predCalm = sculpt.ValuePos01For(pk, 0.5f);
      Bci::PublishBrainGlobals(focusVec, true);
      sculpt.Tick(2, 0.5);
      const float predFocus = sculpt.ValuePos01For(pk, 0.5f);
      std::printf("  [5] BrainSculpt RBF prediction: calm=%.3f focus=%.3f\n", predCalm, predFocus);
      ck.Expect(predCalm < 0.30f && predFocus > 0.70f,
                "Breakthrough 1: BrainSculptNode RBF interpolates Shift-grabbed states");

      // Gap 5: brain.* Expression & Field Pixel + Sample compilation
      float exprVal = 0.0f;
      std::string err;
      ck.Expect(Expression::Check("brain.arousal * 2.0 + brain.coherence", nullptr, nullptr, err),
                "Gap 5: Expression::Check accepts brain.*");
      ck.Expect(Expression::Evaluate("brain.arousal * 2.0 + brain.coherence", 0.0, nullptr, nullptr, exprVal, err) &&
                std::fabs(exprVal - (0.88f * 2.0f + 0.35f)) < 1e-3f,
                "Gap 5: Expression::Evaluate resolves brain.* live values");

      std::vector<Field::Token> toks;
      Field::FieldError ferr;
      Field::AstNodePtr ast;
      Field::PixelIRProgram pixIr;
      const char* pixSrc = "col = vec3(brain.arousal, brain.smoothness, brain.blink);";
      ck.Expect(Field::Lex(pixSrc, toks, ferr) && Field::ParseProgram(toks, ast, ferr) &&
                Field::LowerPixelProgramToIR(ast, pixIr, ferr),
                "Gap 5: FieldPixel lowers brain.* symbols cleanly");
      Field::GlslEmitResult glsl = Field::EmitGlsl(pixIr);
      ck.Expect(glsl.error.empty() && glsl.source.find("fld_brain_arousal") != std::string::npos,
                "Gap 5: GlslBackend emits uniform fld_brain_*");

      Field::SampleProgram sProg;
      Field::FieldError sErr;
      const char* sampleSrc = "state float ph = 0\nph = (ph + (110 + 440 * brain.arousal) / sr) % 1\nout = sin(6.28318 * ph) * (0.2 + 0.5 * brain.jaw)";
      ck.Expect(Field::CompileSampleProgram(sampleSrc, nullptr, sProg, sErr),
                "Gap 5: Sample domain compiles brain.arousal and brain.jaw");
      Field::SampleRuntimeInput sIn;
      float stCur[4] = {}, stNext[4] = {};
      sIn.sr = 48000.0f;
      sIn.stateCur = stCur;
      sIn.stateNext = stNext;
      float regs[Field::kSampleMaxRegs] = {};
      const float sampleOut = Field::RunSampleProgram(sProg, sIn, regs);
      ck.Expect(std::isfinite(sampleOut), "Gap 5: SampleRuntime executes LoadBrain cleanly");

      tr.SetOfflineMode(false);
      tr.SetPlaying(false);
      tr.Seek(0.0);
   }

   // 6. Optional Real Human EEG Check (PhysioNet EDF -> 256 Hz CSV via INFINITE_BCI_CSV_OPEN / CLOSED)
   if (const char* openCsv = std::getenv("INFINITE_BCI_CSV_OPEN"))
   {
      const char* closedCsv = std::getenv("INFINITE_BCI_CSV_CLOSED");
      auto evalCsv = [&](const char* path, double& outAlpha, double& outArousal, double& outCoherence,
                         double& outSlope, int& outBlinks)
      {
         Bci::SyntheticCortex replay;
         std::string cerr;
         if (!replay.LoadCsv(path, cerr))
         {
            std::printf("  [6] CSV load error (%s): %s\n", path, cerr.c_str());
            return false;
         }
         Bci::BciDspEngine dsp;
         dsp.Reset(256.0);
         dsp.SetSmoothing(0.35f);
         dsp.SetBlinkMode(Bci::BlinkMode::AllBlinks);
         dsp.SetStartleGuard(false);
         std::vector<Bci::SampleFrame> batch;
         Bci::TelemetrySnapshot snap{};
         float pins[Bci::kOutputPinCount] = {};
         double sumA = 0, sumAr = 0, sumCoh = 0, sumSl = 0;
         int n = 0;
         outBlinks = 0;
         bool prevBlink = false;
         // Run first 20s (320 steps of 16 samples @ 256 Hz)
         for (int s = 0; s < 320; ++s)
         {
            replay.Generate(16, 256.0, batch);
            dsp.Process(batch.data(), (int)batch.size(), pins, snap);
            if (s >= 32) // skip initial 2s filter settle
            {
               sumA += pins[BrainSourceNode::kAlpha];
               sumAr += pins[BrainSourceNode::kArousal];
               sumCoh += pins[BrainSourceNode::kCoherence];
               sumSl += snap.aperiodicSlope;
               ++n;
            }
            const bool bNow = pins[BrainSourceNode::kBlink] > 0.5f;
            if (bNow && !prevBlink) ++outBlinks;
            prevBlink = bNow;
         }
         outAlpha = n > 0 ? sumA / n : 0.0;
         outArousal = n > 0 ? sumAr / n : 0.0;
         outCoherence = n > 0 ? sumCoh / n : 0.0;
         outSlope = n > 0 ? sumSl / n : 0.0;
         return true;
      };

      double oAlpha = 0, oArousal = 0, oCoh = 0, oSlope = 0;
      int oBlinks = 0;
      ck.Expect(evalCsv(openCsv, oAlpha, oArousal, oCoh, oSlope, oBlinks),
                "Real Human EEG Eyes-Open CSV loads and processes");
      std::printf("  [6] Real Human EEG (Eyes Open):   alpha=%.3f arousal=%.3f coherence=%.3f 1/f_slope=%.2f blinks=%d\n",
                  oAlpha, oArousal, oCoh, oSlope, oBlinks);
      if (closedCsv != nullptr)
      {
         double cAlpha = 0, cArousal = 0, cCoh = 0, cSlope = 0;
         int cBlinks = 0;
         ck.Expect(evalCsv(closedCsv, cAlpha, cArousal, cCoh, cSlope, cBlinks),
                   "Real Human EEG Eyes-Closed CSV loads and processes");
         std::printf("  [6] Real Human EEG (Eyes Closed): alpha=%.3f arousal=%.3f coherence=%.3f 1/f_slope=%.2f blinks=%d\n",
                     cAlpha, cArousal, cCoh, cSlope, cBlinks);
         ck.Expect(cAlpha > oAlpha, "Real Human EEG: Berger Effect (Eyes-Closed Alpha > Eyes-Open Alpha)");
      }
   }

   // 7. Calibrated eyes-closed state on real EEG (INFINITE_BCI_STATE_OPEN / _CLOSED, 4-col 256 Hz CSVs).
   //    Same protocol as tools/bci/state_mirror.py: calibrate on the first 30 s of each file, score the
   //    rest after a 6 s settle. Prints STATE_ENGINE acc=... for the C++ <-> Python comparison.
   if (const char* sOpen = std::getenv("INFINITE_BCI_STATE_OPEN"))
   {
      const char* sClosed = std::getenv("INFINITE_BCI_STATE_CLOSED");
      Bci::SyntheticCortex ro, rc;
      std::string e1, e2;
      const bool loaded = sClosed && ro.LoadCsv(sOpen, e1) && rc.LoadCsv(sClosed, e2);
      ck.Expect(loaded, "Calibrated state: open/closed CSVs load");
      if (loaded)
      {
         Bci::BciDspEngine dsp;
         dsp.Reset(256.0);
         dsp.SetStartleGuard(false);
         dsp.SetPersistProfile(false);
         dsp.SetProfile(Bci::CalibrationProfile{});
         std::vector<Bci::SampleFrame> batch;
         Bci::TelemetrySnapshot snap{};
         float pins[Bci::kOutputPinCount] = {};
         auto feed = [&](Bci::SyntheticCortex& src, int frames, int scoreFrom, int wantClosed, int& hits, int& n)
         {
            for (int done = 0; done < frames; done += 12)
            {
               const int k = std::min(12, frames - done);
               src.Generate(k, 256.0, batch);
               dsp.Process(batch.data(), (int)batch.size(), pins, snap);
               if (wantClosed >= 0 && done >= scoreFrom)
               {
                  hits += ((snap.eyesClosedScore > 0.5f) == (wantClosed == 1)) ? 1 : 0;
                  ++n;
               }
            }
         };
         const int cal = 30 * 256, settle = 6 * 256;
         int hits = 0, n = 0;
         dsp.StartCalibration();
         feed(ro, cal, 0, -1, hits, n);
         feed(rc, cal + 64, 0, -1, hits, n); // + a few frames so the closed phase completes
         if (!dsp.Profile().stateValid)
            std::printf("  [7] STATE_ENGINE no fit (calib failed flag=%d): too few clean updates, likely a bad lead\n",
                        snap.stateCalibFailed ? 1 : 0);
         ck.Expect(dsp.Profile().stateValid || snap.stateCalibFailed,
                   "Calibrated state: fits, or reports the failure (never silent)");
         const int restO = (int)ro.CsvFrameCount() - cal, restC = (int)rc.CsvFrameCount() - cal - 64;
         ck.Expect(restO > settle && restC > settle, "Calibrated state: files longer than 36 s");
         if (dsp.Profile().stateValid && restO > settle && restC > settle)
         {
            feed(ro, restO, settle, 0, hits, n);
            feed(rc, restC, settle, 1, hits, n);
            const double acc = n > 0 ? (double)hits / n : 0.0;
            std::printf("  [7] STATE_ENGINE acc=%.3f updates=%d  region alpha AF7 %.3f AF8 %.3f TP9 %.3f TP10 %.3f\n",
                        acc, n, snap.regionAlpha[0], snap.regionAlpha[1], snap.regionAlpha[2], snap.regionAlpha[3]);
            ck.Expect(acc > 0.6, "Calibrated state: held-out eyes-closed accuracy above 60%");
         }
      }
   }

   // 8. Direct pins: a 10 Hz sine on every channel. Audio: the engine's phase step is 2*pi*10/256 per sample and
   //    the audio pin plays it at 32x (320 Hz, counted by zero crossings). Image: columns arrive and change pixels.
   {
      Bci::BciDspEngine dsp;
      dsp.Reset(256.0);
      dsp.SetStartleGuard(false);
      dsp.SetArtifactBlanking(false);
      dsp.SetPersistProfile(false);
      dsp.SetProfile(Bci::CalibrationProfile{});
      BrainSourceNode node;
      std::vector<Bci::SampleFrame> batch(12);
      Bci::TelemetrySnapshot snap{};
      float pins[Bci::kOutputPinCount] = {};
      unsigned lastSeq = 0;
      double dphSum = 0.0;
      int dphN = 0;
      std::vector<uint8_t> px0, px1;
      const double kTwoPi = 6.283185307179586;
      for (int i = 0, t = 0; i < 8 * 256 / 12; ++i)
      {
         for (auto& f : batch)
         {
            const float v = 40.0f * (float)std::sin(kTwoPi * 10.0 * (double)t++ / 256.0);
            for (float& c : f.ch)
               c = v;
         }
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         if (i > 4 * 256 / 12)
            for (const auto& a : dsp.Audify())
            {
               dphSum += a.dphase[0];
               ++dphN;
            }
         node.FeedDirect(snap, lastSeq, dsp.Audify(), 256.0);
         if (i == 4 * 256 / 12)
            node.CopyImagePixels(px0);
      }
      node.CopyImagePixels(px1);
      const double dph = dphN > 0 ? dphSum / dphN : 0.0;
      const double want = kTwoPi * 10.0 / 256.0;

      AudioNode* an = node.GetAudioNode();
      const int sr = 48000, n = sr / 10; // 0.1 s: inside the ~32 frames left after the catch-up skip
      std::vector<float> l(n), r(n);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer out{ chans, 2, n };
      an->PrepareToPlay(sr, n);
      an->ProcessBlock(nullptr, 0, out);
      int zc = 0;
      float peak = 0.0f;
      for (int i = 1; i < n; ++i)
      {
         zc += (l[i - 1] < 0.0f) != (l[i] < 0.0f) ? 1 : 0;
         peak = std::max(peak, std::fabs(l[i]));
      }
      const double hz = zc / 2.0 / ((double)n / sr);
      std::printf("  [8] DIRECT_PINS dphase=%.4f (want %.4f) audio=%.1f Hz peak=%.3f imageSeq=%u px=%zu changed=%d\n",
                  dph, want, hz, peak, snap.imageSeq, px1.size(), px0 != px1 ? 1 : 0);
      ck.Expect(std::fabs(dph - want) < 0.05 * want, "Direct audio: engine phase step matches a 10 Hz input");
      ck.Expect(hz > 300.0 && hz < 340.0 && peak > 0.05f, "Direct audio: 10 Hz EEG plays at ~320 Hz");
      ck.Expect(px1.size() == (size_t)BrainSourceNode::kImgW * BrainSourceNode::kImgH * 4 && px0 != px1,
                "Direct image: columns scroll in and change pixels");
   }

   std::printf("BCI_SELFTEST: passed=%d failed=%d %s\n",
               ck.passed, ck.failed, ck.failed == 0 ? "OK" : "FAIL");
   return ck.failed == 0;
}
}
