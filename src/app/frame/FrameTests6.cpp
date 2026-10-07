// Per-frame self-test blocks moved verbatim out of the main loop in main.cpp.
#include "app/AppShared.h"

namespace app
{

void FrameTest_MIDILEARNTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MIDILEARNTEST") != nullptr)
      {
         // Per-parameter MIDI learn: learn -> bind -> drive, reuse, cancel,
         // clash with the other learners, refusal, undo, save/load.
         static bool ok = true, midiUp = false;
         Modulation& mod = Modulation::Instance();
         auto bad = [&](const char* what)
         {
            printf("MIDILEARNTEST %s FAIL\n", what);
            ok = false;
         };
         MpcNode* mpc = nullptr;
         int mpcIdx = -1, r2rIdx = -1;
         for (GraphNode& gn : gNodes)
         {
            if (auto* m = dynamic_cast<MpcNode*>(gn.node.get())) { mpc = m; mpcIdx = gn.index; }
            if (dynamic_cast<RangeToRangeNode*>(gn.node.get()) != nullptr) r2rIdx = gn.index;
         }
         auto ccCount = [&]() -> int
         {
            int c = 0;
            for (GraphNode& gn : gNodes)
               if (dynamic_cast<MidiCCNode*>(gn.node.get()) != nullptr) c++;
            return c;
         };
         auto firstCC = [&]() -> MidiCCNode*
         {
            for (GraphNode& gn : gNodes)
               if (auto* c = dynamic_cast<MidiCCNode*>(gn.node.get())) return c;
            return nullptr;
         };
         auto inject = [&](int cc, int val)
         {
            const unsigned char msg[3] = { 0xB0, (unsigned char)cc, (unsigned char)val };
            Platform::MidiInjectBytes(msg, 3, 77);
         };
         const int volA = MpcNode::ParamId(2, MpcNode::kVolume);
         const int volB = MpcNode::ParamId(3, MpcNode::kVolume);
         const int volC = MpcNode::ParamId(4, MpcNode::kVolume);
         const int volD = MpcNode::ParamId(5, MpcNode::kVolume);
         const int volE = MpcNode::ParamId(6, MpcNode::kVolume);
         const ParamRef* known = mpc != nullptr ? mod.KnownParam(mpcIdx, volA) : nullptr;
         const float kLo = known != nullptr ? known->minValue : 0.0f;
         const float kHi = known != nullptr ? known->maxValue : 1.0f;
         auto near = [&](float a, float b) { return std::fabs(a - b) < 0.02f * std::max(1.0f, std::fabs(kHi - kLo)); };

         if (frameId == 1 && mpc != nullptr)
         {
            std::string err;
            midiUp = Platform::MidiStart(err);
            if (!midiUp)
               printf("MIDILEARNTEST note: MidiStart unavailable (%s); skipping\n", err.c_str());
            else
            {
               StartParamMidiLearn(mpcIdx, volA);
               if (MidiLearnActiveCount() != 1 || !ParamMidiLearnIsActiveFor(mpcIdx, volA))
                  bad("start: exactly one learn (param) should be live");
               inject(20, 64);
            }
         }
         if (midiUp && mpc != nullptr)
         {
            if (frameId == 3)
            {
               // Captured at the top of the previous frame.
               MidiCCNode* cc = firstCC();
               if (ccCount() != 1 || cc == nullptr) bad("learn did not spawn exactly one MIDI CC node");
               else if (cc->device != 77 || cc->channel != 0 || cc->controller != 20 || cc->isNote)
                  bad("spawned node did not take the captured device/channel/controller");
               if (!mod.IsModulated(mpcIdx, volA)) bad("learned param is not bound");
               else
               {
                  const Modulation::Source src = mod.ModulatorFor(mpcIdx, volA);
                  if (!src.hasRange || !near(src.lo, kLo) || !near(src.hi, kHi)) bad("binding range is not the param's range");
               }
               if (MidiLearnActiveCount() != 0) bad("learn still live after capture");
               inject(20, 127);
            }
            if (frameId == 7)
            {
               if (!near(mpc->padVolume[2], kHi)) bad("CC 127 did not drive the param to its max");
               inject(20, 0);
            }
            if (frameId == 10)
            {
               if (!near(mpc->padVolume[2], kLo)) bad("CC 0 did not drive the param to its min");
               // Same control onto a second param: must reuse the node.
               StartParamMidiLearn(mpcIdx, volB);
               inject(20, 64);
            }
            if (frameId == 12)
            {
               MidiCCNode* cc = firstCC();
               if (ccCount() != 1) bad("second param on the same CC spawned a duplicate node");
               if (!mod.IsModulated(mpcIdx, volB) || mod.ModulatorFor(mpcIdx, volB).nodeIndex != mod.ModulatorFor(mpcIdx, volA).nodeIndex)
                  bad("second param is not bound to the shared node");
               (void)cc;
               // Cancel: nothing binds afterwards.
               StartParamMidiLearn(mpcIdx, volC);
               if (MidiLearnActiveCount() != 1) bad("cancel setup: learn not live");
               MidiLearnCancelAll();
               if (MidiLearnActiveCount() != 0) bad("cancel left a learn live");
               inject(21, 64);
            }
            if (frameId == 14)
            {
               if (mod.IsModulated(mpcIdx, volC) || ccCount() != 1) bad("a cancelled learn still bound");
               // Clash matrix.
               StartParamMidiLearn(mpcIdx, volC);
               MidiLearnCancelAll(); // what every perf start site does first
               gPerfMidiLearnIdx = 0;
               if (MidiLearnActiveCount() != 1 || ParamMidiLearnActive()) bad("perf learn start did not cancel the param learn");
               StartParamMidiLearn(mpcIdx, volC);
               if (gPerfMidiLearnIdx != -1 || MidiLearnActiveCount() != 1) bad("param learn start did not cancel the perf learn");
               MidiCCNode* cc = firstCC();
               if (cc != nullptr)
               {
                  MidiLearnCancelAll();
                  cc->StartLearn();
                  StartParamMidiLearn(mpcIdx, volC);
                  if (cc->IsLearning() || MidiLearnActiveCount() != 1) bad("param learn start did not cancel the node learner");
               }
               MidiLearnCancelAll();
               // Refusal: a param another (non-MIDI) modulator drives.
               if (r2rIdx >= 0)
               {
                  mod.Bind(mpcIdx, volD, r2rIdx);
                  if (ParamMidiLearnable(mpcIdx, volD)) bad("a param driven by a non-MIDI modulator was offered MIDI learn");
                  Platform::MidiCCValue fake;
                  fake.device = 77; fake.channel = 0; fake.controller = 22;
                  if (ParamMidiLearnCommit(mpcIdx, volD, fake)) bad("commit replaced a non-MIDI binding");
                  if (mod.ModulatorFor(mpcIdx, volD).nodeIndex != r2rIdx) bad("non-MIDI binding was disturbed");
                  mod.Unbind(mpcIdx, volD);
               }
               // A different CC spawns a second node; undo must take it and the
               // binding back in one step.
               StartParamMidiLearn(mpcIdx, volE);
               inject(30, 100);
            }
            if (frameId == 16)
            {
               if (ccCount() != 2 || !mod.IsModulated(mpcIdx, volE)) bad("second CC did not spawn a second node and bind");
               Undo();
            }
            if (frameId == 18)
            {
               if (ccCount() != 1 || mod.IsModulated(mpcIdx, volE)) bad("undo did not remove the learned node and binding in one step");
               if (!mod.IsModulated(mpcIdx, volA)) bad("undo removed too much");
               Redo();
            }
            if (frameId == 20)
            {
               if (ccCount() != 2 || !mod.IsModulated(mpcIdx, volE)) bad("redo did not restore the learned node and binding");
               // Undo while listening cancels the listen.
               StartParamMidiLearn(mpcIdx, volC);
               Undo();
            }
            if (frameId == 22)
            {
               if (MidiLearnActiveCount() != 0) bad("undo left a learn live");
               Redo();
            }
            if (frameId == 24)
            {
               inject(20, 127);
               SavePatchTo(TmpPath("infinite_midilearntest.infinite"));
            }
            if (frameId == 26)
            {
               NewPatch();
               LoadPatchFrom(TmpPath("infinite_midilearntest.infinite"));
            }
            if (frameId == 30)
            {
               MidiCCNode* cc = firstCC();
               if (ccCount() != 2 || cc == nullptr) bad("reload lost the learned nodes");
               else if (cc->device != 77 || cc->controller != 20) bad("reload lost the captured mapping");
               if (!mod.IsModulated(mpcIdx, volA) || !mod.IsModulated(mpcIdx, volB) || !mod.IsModulated(mpcIdx, volE))
                  bad("learned bindings did not survive save/load");
               if (mod.IsModulated(mpcIdx, volC)) bad("reload invented a binding");
               if (!near(mpc->padVolume[2], kHi)) bad("reloaded mapping does not drive the param");
               if (MidiLearnActiveCount() != 0) bad("learn live after reload");
            }
         }
         if (frameId == 32)
            printf("%s\n", ok ? "MIDILEARNTEST OK" : "MIDILEARNTEST FAIL");
      }
}

void FrameTest_GESTUREUNDOTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_GESTUREUNDOTEST") != nullptr)
      {
         static int sidesParam = -1;
         static bool madeOk = false, undoOk = false, redoOk = false, checked = false;
         GestureRecorder& rec = GestureRecorder::Instance();

         if (frameId == 1)
         {
            for (const ParamRef& ref : Modulation::Instance().FrameParams())
               if (ref.nodeIndex == gNodes[0].index && ref.name == "sides")
                  sidesParam = ref.paramIndex;
            // The checkpoint a knob grab pushes, before the drag records
            // anything - this is the state undo must return to.
            PushUndoCheckpoint();
            const double t = ImGui::GetTime();
            rec.BeginFrame(/*shiftHeld=*/true, t);
            rec.NotifyMovement(gNodes[0].index, sidesParam, 4.0f, t, /*isNewGrab=*/true);
            rec.NotifyMovement(gNodes[0].index, sidesParam, 9.0f, t + 0.1, /*isNewGrab=*/false);
         }
         // The main loop's own BeginFrame(io.KeyShift == false) already ran
         // this frame, ending the session and turning the trace into a loop.
         else if (frameId == 3)
         {
            madeOk = sidesParam >= 0 && rec.IsRecording(gNodes[0].index, sidesParam);
            Undo();
            undoOk = !rec.IsRecording(gNodes[0].index, sidesParam);
         }
         else if (frameId == 5)
         {
            Redo();
            // Note what this does and does not prove: RemapGestures runs, but
            // NewPatch resets gNextIndex to 1 and respawns in order, so on a
            // single-node fixture the remap is the identity and a key that
            // was never rewritten would pass too. The appear/disappear/
            // reappear sequence is the assertion; the non-identity remap
            // needs a fixture that deletes a node between snapshots.
            redoOk = rec.IsRecording(gNodes[0].index, sidesParam);
         }
         else if (frameId == 7 && !checked)
         {
            checked = true;
            const bool ok = madeOk && undoOk && redoOk;
            printf("gesture undo: recorded=%d goneAfterUndo=%d backAfterRedo=%d param=%d node=%d\n",
                   (int)madeOk, (int)undoOk, (int)redoOk, sidesParam, gNodes[0].index);
            printf("%s\n", ok ? "GESTURE UNDO OK" : "GESTURE UNDO FAIL");
         }
      }
}

void FrameTest_CULLDRIVENTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_CULLDRIVENTEST") != nullptr)
      {
         static int rotParam[2] = { -1, -1 };
         static float lastRot[2] = { 0.0f, 0.0f };
         static int changed[2] = { 0, 0 }, sampled = 0;
         static bool checked = false;
         GestureRecorder& rec = GestureRecorder::Instance();
         GraphNode* nodes[2] = { &gNodes[0], &gNodes[2] };
         auto rotOf = [&](int k) { return static_cast<ShapeNode*>(nodes[k]->node.get())->rotation; };

         // Frame 0: this block runs after the next frame's ClearFrameParams,
         // so only the first frame still sees the graph's registrations.
         if (frameId == 0)
         {
            for (int k = 0; k < 2; ++k)
               for (const ParamRef& ref : Modulation::Instance().FrameParams())
                  if (ref.nodeIndex == nodes[k]->index && ref.name == "rotation")
                     rotParam[k] = ref.paramIndex;
            // A one-second 0 -> 180 ramp; the main loop's BeginFrame(shift
            // up) next frame ends the session and starts it looping.
            const double t = rec.ClockNow();
            rec.BeginFrame(/*shiftHeld=*/true, t);
            for (int k = 0; k < 2; ++k)
            {
               rec.NotifyMovement(nodes[k]->index, rotParam[k], 0.0f, t, /*isNewGrab=*/true);
               rec.NotifyMovement(nodes[k]->index, rotParam[k], 90.0f, t + 0.5, /*isNewGrab=*/false);
               rec.NotifyMovement(nodes[k]->index, rotParam[k], 180.0f, t + 1.0, /*isNewGrab=*/false);
            }
            gNodes[0].showParams = false;
            gNodes[2].spawnX = gNodes[2].spawnY = 20000.0f;
            gNodes[2].needsPosition = true;
            Transport::Instance().SetPlaying(true); // the gesture clock only runs while playing
         }
         // 20 frames: past the first-frame layout, and short of the
         // every-kCullRefresh redraw, so a skipped node gets at most one.
         else if (frameId >= 5 && frameId < 25)
         {
            for (int k = 0; k < 2; ++k)
            {
               if (frameId > 5 && rotOf(k) != lastRot[k])
                  ++changed[k];
               lastRot[k] = rotOf(k);
            }
            if (frameId > 5)
               ++sampled;
         }
         else if (frameId >= 25 && !checked)
         {
            checked = true;
            // Wall-clock frames can repeat a clock value; half the frames
            // moving is far above the 1-in-30 a skipped node manages.
            const bool resolved = rotParam[0] >= 0 && rotParam[1] >= 0;
            const bool collapsedOk = resolved && changed[0] * 2 >= sampled;
            const bool culledOk = resolved && changed[1] * 2 >= sampled;
            printf("cull driven: collapsed moved %d/%d, off-screen moved %d/%d (params %d,%d)\n",
                   changed[0], sampled, changed[1], sampled, rotParam[0], rotParam[1]);
            printf("%s\n", (collapsedOk && culledOk) ? "CULL DRIVEN OK" : "CULL DRIVEN FAIL");
         }
      }
}

void FrameTest_MODCURVETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MODCURVETEST") != nullptr)
      {
         static int sidesParam = -1;
         static int rotParam = -1;
         static int radiusParam = -1;
         static bool testMathOk = false;
         static bool testModWarpOk = false;
         static bool testExprOk = false;
         static bool testGestureOk = false;
         static bool testRoundTripOk = false;
         static bool testJsonOk = false;

         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         auto* r2r = static_cast<RangeToRangeNode*>(gNodes[2].node.get());
         Modulation& mod = Modulation::Instance();
         GestureRecorder& rec = GestureRecorder::Instance();

         if (frameId == 1)
         {
            // Test 1: Math properties of ApplyModulationCurve
            const float v0 = ApplyModulationCurve(0.0f, 0.5f);
            const float v1 = ApplyModulationCurve(1.0f, -0.5f);
            const float midLin = ApplyModulationCurve(0.5f, 0.0f);
            const float midExp = ApplyModulationCurve(0.5f, 0.5f);
            const float midLog = ApplyModulationCurve(0.5f, -0.5f);
            testMathOk = (std::fabs(v0 - 0.0f) < 1e-5f) &&
                         (std::fabs(v1 - 1.0f) < 1e-5f) &&
                         (std::fabs(midLin - 0.5f) < 1e-5f) &&
                         (midExp < 0.5f) && (midLog > 0.5f);
            printf("testMath (ApplyModulationCurve unit check) %s (midLin=%.3f midExp=%.3f midLog=%.3f)\n",
                   testMathOk ? "OK" : "- BUG", midLin, midExp, midLog);

            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index)
               {
                  if (ref.name == "sides") sidesParam = ref.paramIndex;
                  else if (ref.name == "rotation") rotParam = ref.paramIndex;
                  else if (ref.name == "radius" || ref.name == "size x") radiusParam = ref.paramIndex;
               }
            }

            r2r->outLow = 0.0f;
            r2r->outHigh = 1.0f;
            r2r->clampOutput = true;
            r2r->constantIn = 0.5f; // input is 0.5
            mod.Bind(gNodes[0].index, sidesParam, gNodes[2].index);
            mod.SetRange(gNodes[0].index, sidesParam, 0.0f, 100.0f);
            mod.SetCurve(gNodes[0].index, sidesParam, 0.5f);

            if (rotParam >= 0)
            {
               mod.SetExpression(gNodes[0].index, rotParam, "0.5");
               mod.SetExpressionCurve(gNodes[0].index, rotParam, -0.6f);
            }
         }
         if (frameId == 3)
         {
            const float curve = mod.CurveFor(gNodes[0].index, sidesParam);
            const bool curveStored = (std::fabs(curve - 0.5f) < 1e-4f);
            const float expectedVal = ApplyModulationCurve(0.5f, 0.5f) * 100.0f;
            const bool valueWarped = (shape->sides < 30 && shape->sides > 5);
            testModWarpOk = curveStored && valueWarped;
            printf("testModWarp (modulator curve applied) stored=%.2f sides=%d expected=%.1f %s\n",
                   curve, shape->sides, expectedVal, testModWarpOk ? "OK" : "- BUG");

            if (rotParam >= 0)
            {
               const float exprCurve = mod.ExpressionCurveFor(gNodes[0].index, rotParam);
               testExprOk = (std::fabs(exprCurve - (-0.6f)) < 1e-4f);
               printf("testExpr (expression curve stored) curve=%.2f %s\n",
                      exprCurve, testExprOk ? "OK" : "- BUG");
            }
            else
            {
               testExprOk = true;
            }

            // Setup gesture recorder loop on radiusParam
            if (radiusParam >= 0)
            {
               rec.BeginFrame(/*shiftHeld=*/true, ImGui::GetTime());
               rec.NotifyMovement(gNodes[0].index, radiusParam, 0.1f, ImGui::GetTime(), true);
               rec.NotifyMovement(gNodes[0].index, radiusParam, 0.9f, ImGui::GetTime() + 0.1, false);
            }
         }
         if (frameId == 5)
         {
            if (radiusParam >= 0)
            {
               rec.SetPlaybackCurve(gNodes[0].index, radiusParam, 0.75f);
               const float gestCurve = rec.PlaybackCurveFor(gNodes[0].index, radiusParam);
               testGestureOk = (std::fabs(gestCurve - 0.75f) < 1e-4f);
               printf("testGesture (gesture curve stored) curve=%.2f %s\n",
                      gestCurve, testGestureOk ? "OK" : "- BUG");
            }
            else
            {
               testGestureOk = true;
            }

            // Check JSON serialization
            nlohmann::json out = PatchJson::ToJson(BuildPatchData());
            bool jsonModCurveOk = false;
            for (const auto& m : out["modulation"])
            {
               if (m["dstIndex"] == gNodes[0].index && m["dstParam"] == sidesParam)
                  jsonModCurveOk = (std::fabs(m["curve"].get<float>() - 0.5f) < 1e-4f);
            }
            testJsonOk = jsonModCurveOk;
            printf("testJson (JSON export carries curve) %s\n", testJsonOk ? "OK" : "- BUG");

            // Save patch
            SavePatchTo(TmpPath("infinite_modcurvetest.infinite"));
         }
         if (frameId == 7)
         {
            NewPatch();
            LoadPatchFrom(TmpPath("infinite_modcurvetest.infinite"));
         }
         if (frameId == 9)
         {
            int reloadedNodeIdx = -1;
            int reloadedSidesParam = -1;
            int reloadedRotParam = -1;
            int reloadedRadiusParam = -1;
            for (GraphNode& gn : gNodes)
               if (dynamic_cast<ShapeNode*>(gn.node.get()) != nullptr)
                  reloadedNodeIdx = gn.index;
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == reloadedNodeIdx)
               {
                  if (ref.name == "sides") reloadedSidesParam = ref.paramIndex;
                  else if (ref.name == "rotation") reloadedRotParam = ref.paramIndex;
                  else if (ref.name == "radius" || ref.name == "size x") reloadedRadiusParam = ref.paramIndex;
               }
            }

            const float reloadedModCurve = mod.CurveFor(reloadedNodeIdx, reloadedSidesParam);
            const float reloadedExprCurve = (reloadedRotParam >= 0) ? mod.ExpressionCurveFor(reloadedNodeIdx, reloadedRotParam) : -0.6f;
            const float reloadedGestCurve = (reloadedRadiusParam >= 0) ? rec.PlaybackCurveFor(reloadedNodeIdx, reloadedRadiusParam) : 0.75f;

            const bool modOk = (std::fabs(reloadedModCurve - 0.5f) < 1e-3f);
            const bool exprOk = (std::fabs(reloadedExprCurve - (-0.6f)) < 1e-3f);
            const bool gestOk = (std::fabs(reloadedGestCurve - 0.75f) < 1e-3f);
            testRoundTripOk = modOk && exprOk && gestOk;

            printf("testRoundTrip (patch reload) modCurve=%.2f exprCurve=%.2f gestCurve=%.2f %s\n",
                   reloadedModCurve, reloadedExprCurve, reloadedGestCurve,
                   testRoundTripOk ? "OK" : "- BUG");

            const bool allOk = testMathOk && testModWarpOk && testExprOk && testGestureOk && testRoundTripOk && testJsonOk;
            printf("%s\n", allOk ? "MOD CURVE TEST OK" : "MOD CURVE TEST FAIL");
         }
      }
}

void FrameTest_CACHETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_CACHETEST") != nullptr)
      {
         static unsigned long long sPrevWork = 0;
         static int sIdleStreak = 0;
         const unsigned long long work = NodeWorkCounter();
         const bool idle = (work == sPrevWork);
         sIdleStreak = idle ? sIdleStreak + 1 : 0;
         sPrevWork = work;
         printf("CACHETEST frame=%d work=%llu idle=%d idleStreak=%d\n",
                frameId, work, idle ? 1 : 0, sIdleStreak);
      }
}
}
