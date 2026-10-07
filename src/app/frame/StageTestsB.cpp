// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
int DrawTestsB(FrameCtx& fc)
{
   auto& window = fc.window;
   auto& selfTest = fc.selfTest;
   auto& sFirstFrameEndMs = fc.sFirstFrameEndMs;
   auto& frameId = fc.frameId;
   auto& splashEnabled = fc.splashEnabled;
   auto& isBenchB3 = fc.isBenchB3;
   auto& benchB6Stages = fc.benchB6Stages;
   auto& benchStagesSample = fc.benchStagesSample;
   auto& benchStagesCpuSample = fc.benchStagesCpuSample;
   auto& now = fc.now;

      if (getenv("INFINITE_OFFLINECLOCKTEST") != nullptr && frameId >= 1 && !offlineClockDone)
      {
         Modulation& mod = Modulation::Instance();
         GestureRecorder& rec = GestureRecorder::Instance();
         Transport& transport = Transport::Instance();
         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         int sizeXParam = -1, sizeYParam = -1, rotParam = -1;
         for (const ParamRef& ref : mod.FrameParams())
         {
            if (ref.nodeIndex != gNodes[0].index)
               continue;
            if (ref.name == "size x") sizeXParam = ref.paramIndex;
            if (ref.name == "size y") sizeYParam = ref.paramIndex;
            if (ref.name == "rotation") rotParam = ref.paramIndex;
         }
         const bool resolved = sizeXParam >= 0 && sizeYParam >= 0 && rotParam >= 0;
         // The Shape registers its controls only on a frame that draws its
         // params; under load that frame can slip past frame 1 (a full driver
         // run saw "params -1,-1,-1"). Retry each frame up to the last one the
         // driver gives us, and only then report the failure.
         if (!resolved && frameId < 6)
            ;
         else
         {
         offlineClockDone = true;
         if (resolved)
         {
            mod.Bind(gNodes[0].index, sizeXParam, gNodes[2].index);
            mod.SetExpression(gNodes[0].index, sizeYParam, "lerp(lo, hi, mod(t, 4) / 4)");
            // A one-second -90 -> 90 ramp, ended at once so it is already looping.
            const double t0 = rec.ClockNow();
            rec.BeginFrame(/*shiftHeld=*/true, t0);
            rec.NotifyMovement(gNodes[0].index, rotParam, -90.0f, t0, /*isNewGrab=*/true);
            rec.NotifyMovement(gNodes[0].index, rotParam, 0.0f, t0 + 0.5, /*isNewGrab=*/false);
            rec.NotifyMovement(gNodes[0].index, rotParam, 90.0f, t0 + 1.0, /*isNewGrab=*/false);
            rec.BeginFrame(/*shiftHeld=*/false, t0 + 1.0);
         }
         transport.SetPlaying(false); // every run enters offline from the same transport position

         struct Values { float sizeX, sizeY, rot; };
         int sweepFrameId = 1000000; // clear of the main loop's ids, so no modulator memo is reused
         uint32_t lcg = 12345u;
         auto sweep = [&](int fps, double start, int frames, uint32_t wallSeed) {
            std::vector<Values> out;
            lcg = wallSeed;
            transport.SetOfflineMode(true, 48000.0);
            int n = 0;
            while (n < frames)
            {
               lcg = lcg * 1664525u + 1013904223u;
               rec.AdvanceClock((double)(lcg >> 8) / (double)(1u << 24) * 0.2, true); // wall dt in [0, 0.2)
               const int batch = 1 + (int)((lcg >> 4) % 7u);
               for (int b = 0; b < batch && n < frames; ++b, ++n)
               {
                  transport.SetOfflineVideoTime(start + (double)n / (double)fps);
                  ApplyModulationAndPalette(++sweepFrameId);
                  out.push_back({ shape->sizeX, shape->sizeY, shape->rotation });
               }
            }
            transport.SetOfflineMode(false, 0.0);
            return out;
         };
         const std::vector<Values> a = sweep(30, 0.0, 90, 1u);
         const std::vector<Values> b = sweep(30, 0.0, 90, 777u);
         const std::vector<Values> c = sweep(60, 0.0, 180, 4242u);
         const std::vector<Values> d = sweep(30, 2.5, 60, 99u);

         const bool sameRuns = a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Values)) == 0;
         double worstRate = 0.0, worstRamp = 0.0, worstStart = 0.0;
         for (size_t k = 0; k < a.size() && 2 * k < c.size(); ++k)
         {
            worstRate = std::max(worstRate, (double)std::abs(a[k].sizeX - c[2 * k].sizeX));
            worstRate = std::max(worstRate, (double)std::abs(a[k].sizeY - c[2 * k].sizeY));
            worstRate = std::max(worstRate, (double)std::abs(a[k].rot - c[2 * k].rot));
         }
         auto ramp = [](double t) { return -90.0 + 180.0 * std::fmod(t, 1.0); };
         for (size_t k = 0; k < a.size(); ++k)
            worstRamp = std::max(worstRamp, std::abs((double)a[k].rot - ramp((double)k / 30.0)));
         for (size_t k = 0; k < d.size(); ++k)
            worstStart = std::max(worstStart, std::abs((double)d[k].rot - ramp(2.5 + (double)k / 30.0)));
         // The LFO and expression must actually move, or equal runs prove nothing.
         const bool moving = !a.empty() && a.front().sizeX != a[a.size() / 2].sizeX &&
                             a.front().sizeY != a[a.size() / 2].sizeY;

         const bool ok = resolved && moving && sameRuns && worstRate <= 1e-6 && worstRamp <= 1e-3 && worstStart <= 1e-3;
         printf("offline clock: params %d,%d,%d moving=%d 30==30 %s, 30 vs 60 worst %.3g, ramp worst %.3g, start 2.5 worst %.3g\n",
                sizeXParam, sizeYParam, rotParam, (int)moving, sameRuns ? "identical" : "DIFFERENT",
                worstRate, worstRamp, worstStart);
         printf("%s\n", ok ? "OFFLINE CLOCK OK" : "OFFLINE CLOCK FAIL");
         }
      }

      // A gesture loop (Shift-drag recording) on a node whose body is skipped -
      // off screen (culled) or eye closed (register-only pass) - must still
      // move every frame. Both skips are gated on GraphNode::IsParamDriven;
      // before gestures were in it, the culled node stepped once per
      // kCullRefresh frames and the collapsed one froze, in the canvas and in
      // any render/record taken while zoomed in.
      FrameTest_CULLDRIVENTEST(frameId, window);

#ifndef NDEBUG
      if (getenv("INFINITE_PREDBINDTEST") != nullptr)
      {
         static int sizeYParam = -1, rotParam = -1, freqParam = -1, discParam = -1;
         static int filterIdx = -1;
         static ParamRef sizeXRef, freqRef;
         static bool ok[8] = {};
         static uint64_t stubUid = 0;
         static int lastTicks = 0, tickCalls = 0, tickBad = 0;
         static float posAtHold = -1.0f, discBefore = 0.0f, holdBefore = 0.0f;
         static int ticksAtBypass = 0;
         Modulation& mod = Modulation::Instance();
         auto stubNode = [&]() -> StubPredictorNode* {
            for (GraphNode& gn : gNodes)
               if (auto* st = dynamic_cast<StubPredictorNode*>(gn.node.get())) return st;
            return nullptr;
         };
         auto frameRefFor = [&](int nodeIdx, int param) -> const ParamRef* {
            for (const ParamRef& r : mod.FrameParams())
               if (r.nodeIndex == nodeIdx && r.paramIndex == param) return &r;
            return nullptr;
         };
         auto sizeXPos = [&]() -> float {
            const ParamRef* r = frameRefFor(gNodes[0].index, gPredTestSizeXParam);
            return r != nullptr ? ParamToPos(*r, *r->value) : -1.0f;
         };
         StubPredictorNode* stub = stubNode();

         if (frameId == 1)
         {
            for (const ParamRef& r : mod.FrameParams())
            {
               if (r.nodeIndex == gNodes[0].index)
               {
                  if (r.name == "size x") { gPredTestSizeXParam = r.paramIndex; gPredTestNodeIndex = gNodes[0].index; sizeXRef = r; }
                  if (r.name == "size y") sizeYParam = r.paramIndex;
                  if (r.name == "rotation") rotParam = r.paramIndex;
                  if ((r.isEnum || r.isBool) && discParam < 0) discParam = r.paramIndex;
               }
               if (r.nodeIndex == gNodes[3].index && r.posToValue != nullptr && freqParam < 0)
               {
                  freqParam = r.paramIndex; filterIdx = r.nodeIndex; freqRef = r;
               }
            }
            printf("predbind: sizeX=%d sizeY=%d rot=%d discrete=%d filterFreq=%d\n", gPredTestSizeXParam, sizeYParam,
                   rotParam, discParam, freqParam);
            if (stub == nullptr || gPredTestSizeXParam < 0 || sizeYParam < 0 || rotParam < 0 || discParam < 0 || freqParam < 0)
            {
               printf("PREDBINDTEST FAIL (fixture: could not resolve params)\n");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
               return 1;
            }
            stubUid = UidForIndex(gNodes[2].index);
            stub->pos = 0.5f;
            mod.Bind(gNodes[0].index, gPredTestSizeXParam, gNodes[2].index);
            mod.Bind(gNodes[0].index, sizeYParam, gNodes[2].index);
            mod.Bind(gNodes[0].index, rotParam, gNodes[2].index);
            mod.Bind(filterIdx, freqParam, gNodes[2].index);
         }
         // 1. one stub drives four params: Tick runs once per frame
         if (frameId >= 3 && frameId <= 8 && stub != nullptr)
         {
            if (frameId > 3) { ++tickCalls; if (stub->tickCount - lastTicks != 1) ++tickBad; }
            lastTicks = stub->tickCount;
            if (frameId == 8)
            {
               ok[1] = tickCalls == 5 && tickBad == 0;
               printf("test1 (idempotent Tick, 4 bindings) frames=%d bad=%d  %s\n", tickCalls, tickBad, ok[1] ? "OK" : "- BUG");
               // 2. fader space: p = 0.5 on a log-tapered param lands at posToValue(0.5), not the linear midpoint
               const ParamRef* fr = frameRefFor(filterIdx, freqParam);
               if (fr != nullptr)
               {
                  const float expect = std::clamp(fr->posToValue(0.5f, fr->minValue, fr->maxValue), fr->minValue, fr->maxValue);
                  const float linMid = 0.5f * (fr->minValue + fr->maxValue);
                  ok[2] = std::fabs(*fr->value - expect) <= 1e-3f * std::max(1.0f, std::fabs(expect)) &&
                          std::fabs(expect - linMid) > 0.01f * (fr->maxValue - fr->minValue);
                  printf("test2 (fader-space map) value=%.3f expect=%.3f linearMid=%.3f  %s\n", *fr->value, expect, linMid,
                         ok[2] ? "OK" : "- BUG");
               }
            }
         }
         // 3a. plain grab: no Shift, so nothing changes
         if (frameId == 69 && stub != nullptr)
         {
            const float pos = sizeXPos();
            ok[3] = stub->grabCount == 0 && stub->releaseCount == 0 && std::fabs(pos - 0.5f) < 0.02f;
            printf("test3a (plain grab ignored) grabs=%d pos=%.3f  %s\n", stub->grabCount, pos, ok[3] ? "OK" : "- BUG");
         }
         // 3b. Shift-grab: held by hand mid-drag, OnRelease gets the new pos, then the predictor resumes
         if (frameId == 79 && stub != nullptr)
         {
            posAtHold = sizeXPos();
            holdBefore = posAtHold;
            ok[4] = stub->grabCount == 1 && posAtHold > 0.6f;
            printf("test3b (shift-grab holds) grabs=%d pos=%.3f  %s\n", stub->grabCount, posAtHold, ok[4] ? "OK" : "- BUG");
         }
         if (frameId == 85 && stub != nullptr)
         {
            const float pos = sizeXPos();
            ok[5] = stub->releaseCount == 1 && stub->releasePos > 0.6f && std::fabs(stub->releasePos - holdBefore) < 0.05f &&
                    stub->lastRelease.uid == UidForIndex(gNodes[0].index) && std::fabs(pos - 0.5f) < 0.02f;
            printf("test3c (release + resume) releases=%d releasePos=%.3f vel=%.2f resumedPos=%.3f  %s\n",
                   stub->releaseCount, stub->releasePos, stub->releaseVel, pos, ok[5] ? "OK" : "- BUG");
         }
         if (frameId == 90)
         {
            const bool clear = gPredictorGrabs.empty() && gPredictorGrabsPrev.empty();
            printf("test3d (grab set cleared) %s\n", clear ? "OK" : "- BUG");
            ok[5] = ok[5] && clear;
            // 4. undo keeps the slot key: unbind, undo, the binding is back and keyed by the same uid
            PushUndoCheckpoint();
            mod.Unbind(gNodes[0].index, sizeYParam);
            Undo();
         }
         if (frameId == 91)
         {
            stub = stubNode();
            if (stub != nullptr)
               stub->pos = 0.25f;
         }
         if (frameId == 94)
         {
            const Modulation::Source s = mod.ModulatorFor(gNodes[0].index, sizeYParam);
            const ParamRef* yr = frameRefFor(gNodes[0].index, sizeYParam);
            const float yPos = yr != nullptr ? ParamToPos(*yr, *yr->value) : -1.0f;
            ok[6] = stub != nullptr && s.nodeIndex >= 0 && UidForIndex(s.nodeIndex) == stubUid && std::fabs(yPos - 0.25f) < 0.02f &&
                    stub->readKeys.count(ParamKey{ UidForIndex(gNodes[0].index), sizeYParam }) > 0;
            printf("test4 (undo keeps uid key) bound=%d uidSame=%d yPos=%.3f  %s\n", (int)(s.nodeIndex >= 0),
                   (int)(s.nodeIndex >= 0 && UidForIndex(s.nodeIndex) == stubUid), yPos, ok[6] ? "OK" : "- BUG");
         }
         // 5. green never binds to a discrete param: refused at the cable drop, inert when it arrives via a patch
         if (frameId == 96 && stub != nullptr)
         {
            const ParamRef* dr = frameRefFor(gNodes[0].index, discParam);
            discBefore = dr != nullptr ? *dr->value : 0.0f;
            const bool refused = PredictorBindRefusal(stub, gNodes[0].index, discParam) != nullptr;
            const bool contRefused = PredictorBindRefusal(stub, gNodes[0].index, sizeYParam) != nullptr;
            Modulation::Source src;
            src.nodeIndex = gNodes[2].index; src.lo = 0.0f; src.hi = 1.0f; src.hasRange = true;
            mod.RestoreLink(gNodes[0].index, discParam, src); // what a hand-edited patch file would do
            ok[7] = refused && !contRefused;
            printf("test5a (cable drop refuses discrete) refused=%d continuousRefused=%d  %s\n", (int)refused, (int)contRefused,
                   ok[7] ? "OK" : "- BUG");
            stub->pos = 0.9f;
            stub->readKeys.clear();
         }
         if (frameId == 100 && stub != nullptr)
         {
            const ParamRef* dr = frameRefFor(gNodes[0].index, discParam);
            const bool held = dr != nullptr && *dr->value == discBefore;
            const bool neverRead = stub->readKeys.count(ParamKey{ UidForIndex(gNodes[0].index), discParam }) == 0;
            const bool inert = IsInertPredictorBinding(gNodes[0].index, discParam);
            const bool good = held && neverRead && inert;
            printf("test5b (loaded discrete binding inert) held=%d neverRead=%d matrixInert=%d  %s\n", (int)held, (int)neverRead,
                   (int)inert, good ? "OK" : "- BUG");
            ok[7] = ok[7] && good;
            mod.Unbind(gNodes[0].index, discParam);
         }
         // 6. bypass: Tick stops and the param holds; un-bypass resumes
         static float xAtBypass = -1.0f;
         if (frameId == 102 && stub != nullptr)
         {
            xAtBypass = sizeXPos();
            ticksAtBypass = stub->tickCount;
            stub->pos = 0.1f;
            gNodes[2].node->bypassed = true;
         }
         if (frameId == 106 && stub != nullptr)
         {
            const bool frozen = stub->tickCount == ticksAtBypass && std::fabs(sizeXPos() - xAtBypass) < 0.02f;
            gNodes[2].node->bypassed = false;
            ok[0] = frozen;
            printf("test6a (bypass freezes) ticks=%d/%d pos=%.3f held=%.3f  %s\n", stub->tickCount, ticksAtBypass, sizeXPos(),
                   xAtBypass, frozen ? "OK" : "- BUG");
         }
         if (frameId == 110 && stub != nullptr)
         {
            const bool resumed = stub->tickCount > ticksAtBypass && std::fabs(sizeXPos() - 0.1f) < 0.02f;
            ok[0] = ok[0] && resumed;
            printf("test6b (un-bypass resumes) ticks=%d pos=%.3f  %s\n", stub->tickCount, sizeXPos(), resumed ? "OK" : "- BUG");
            const bool all = ok[0] && ok[1] && ok[2] && ok[3] && ok[4] && ok[5] && ok[6] && ok[7];
            printf("%s\n", all ? "PREDBINDTEST OK" : "PREDBINDTEST FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
#endif

      if (getenv("INFINITE_MODMATRIXTEST") != nullptr)
      {
         static int sidesParam = -1;
         static bool test1Ok = false, test2Ok = false, test3Ok = false, test4Ok = false;
         static int sidesAtDisable = 0;
         static bool movedWhileEnabled = false;
         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         auto* r2r = static_cast<RangeToRangeNode*>(gNodes[2].node.get());
         Modulation& mod = Modulation::Instance();

         if (frameId == 1)
         {
            for (const ParamRef& ref : mod.FrameParams())
               if (ref.nodeIndex == gNodes[0].index && ref.name == "sides")
                  sidesParam = ref.paramIndex;
            printf("sides param=%d\n", sidesParam);
            r2r->outLow = 3.0f;
            r2r->outHigh = 20.0f;
            r2r->clampOutput = true;
            r2r->constantIn = 0.0f; // -> sides = 3
            mod.Bind(gNodes[0].index, sidesParam, gNodes[2].index);
         }
         if (frameId == 3)
         {
            // Test 1: the binding exists and Links() reports it, matching
            // what the matrix table iterates.
            test1Ok = mod.Links().size() == 1 &&
                      mod.ModulatorFor(gNodes[0].index, sidesParam).nodeIndex == gNodes[2].index;
            printf("test1 (link exists) links=%zu  %s\n", mod.Links().size(),
                   test1Ok ? "OK" : "- BUG");
            gNodes[0].showParams = false; // collapse the modulated node
         }
         if (frameId == 5)
         {
            // Test 2: the matrix can still resolve a collapsed destination's
            // name/range. Two things have to hold at once: a collapsed node
            // that something is patched into keeps re-registering its params
            // (otherwise the apply pass has no pointer to write through and
            // the modulation silently freezes - see gParamRegisterOnly), and
            // KnownParam stays sticky for the cases that genuinely register
            // nothing (a docked matrix panel drawing before the canvas has,
            // a param behind a mode switch).
            bool registeredThisFrame = false;
            for (const ParamRef& ref : mod.FrameParams())
               if (ref.nodeIndex == gNodes[0].index && ref.paramIndex == sidesParam)
                  registeredThisFrame = true;
            const ParamRef* known = mod.KnownParam(gNodes[0].index, sidesParam);
            test2Ok = registeredThisFrame && known != nullptr && known->name == "sides";
            printf("test2 (collapsed node still registers, KnownParam sticky) registered=%d known=%p name=%s  %s\n",
                   registeredThisFrame, (const void*)known, known ? known->name.c_str() : "(null)",
                   test2Ok ? "OK" : "- BUG");
            gNodes[0].showParams = true; // reopen
            r2r->constantIn = 1.0f; // -> sides should move towards 20 while still enabled
         }
         if (frameId == 8)
         {
            // Positive control: prove the binding actually drives the
            // value while enabled, so freezing it after disable (below)
            // means something.
            movedWhileEnabled = shape->sides != 3;
            printf("moved while enabled: sides=%d %s\n", shape->sides,
                   movedWhileEnabled ? "MOVED" : "DID NOT MOVE");
            mod.SetEnabled(gNodes[0].index, sidesParam, false);
            sidesAtDisable = shape->sides;
            r2r->constantIn = 0.0f; // try to pull it back to 3 - must have no effect once disabled
         }
         if (frameId == 13)
         {
            // Test 3: a disabled binding stops being written - sides must
            // still be exactly what it was the frame it was disabled, even
            // though the modulator driving it (now aimed back at the other
            // end of its range) keeps changing.
            test3Ok = movedWhileEnabled && shape->sides == sidesAtDisable;
            printf("test3 (disabled freezes value) at=%d now=%d  %s\n",
                   sidesAtDisable, shape->sides, test3Ok ? "OK" : "- BUG");
            SavePatchTo(TmpPath("infinite_modmatrixtest.infinite"));
            // Also check the RemoteControl JSON export carries enabled=false
            // through PatchJson::ToJson - the text patch and the JSON view
            // must agree.
            nlohmann::json out = PatchJson::ToJson(BuildPatchData());
            bool jsonEnabledFalse = false;
            for (const auto& m : out["modulation"])
            {
               if (m["dstIndex"] == gNodes[0].index && m["dstParam"] == sidesParam)
                  jsonEnabledFalse = (m["enabled"] == false);
            }
            printf("test4a (json enabled=false) %s\n", jsonEnabledFalse ? "OK" : "- BUG");
            test4Ok = jsonEnabledFalse;
         }
         if (frameId == 15)
         {
            NewPatch();
            LoadPatchFrom(TmpPath("infinite_modmatrixtest.infinite"));
         }
         if (frameId == 17)
         {
            // Test 4 (continued): the text patch round-trip must reproduce
            // the same disabled binding.
            int reloadedNodeIdx = -1, reloadedSidesParam = -1;
            for (GraphNode& gn : gNodes)
               if (dynamic_cast<ShapeNode*>(gn.node.get()) != nullptr)
                  reloadedNodeIdx = gn.index;
            for (const ParamRef& ref : mod.FrameParams())
               if (ref.nodeIndex == reloadedNodeIdx && ref.name == "sides")
                  reloadedSidesParam = ref.paramIndex;
            const Modulation::Source reloaded = mod.ModulatorFor(reloadedNodeIdx, reloadedSidesParam);
            const bool textEnabledFalse = !reloaded.enabled;
            printf("test4b (text patch enabled=false) %s\n", textEnabledFalse ? "OK" : "- BUG");
            test4Ok = test4Ok && textEnabledFalse;

            // No self-close here: INFINITE_EXITAFTER owns closing the window
            // and flushing stdout - see the identical comment on
            // INFINITE_MODBOUNDSTEST above.
            printf("%s\n", (test1Ok && test2Ok && test3Ok && test4Ok) ? "MOD MATRIX TEST OK" : "SUSPECT");
         }
      }

      FrameTest_MODCURVETEST(frameId, window);

      if (getenv("INFINITE_OSCTEST") != nullptr)
      {
         if (gNodes.size() < 5)
         {
            printf("OSCTEST fixture missing (%zu nodes)\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return 1;
         }
         auto* constNode = static_cast<ConstantNode*>(gNodes[2].node.get());
         auto* send = static_cast<OscSendNode*>(gNodes[3].node.get());
         auto* recv = static_cast<OscReceiveNode*>(gNodes[4].node.get());
         if (frameId == 1)
         {
            // A dedicated port, not the 9000 default both nodes spawn with -
            // proves the port field itself (and the receive listener restart
            // it triggers) actually takes effect, not just the wiring.
            const int testPort = 9737;
            constNode->value = 0.73f;
            send->host = "127.0.0.1";
            send->port = testPort;
            send->address = "/infinite/osctest";
            recv->port = testPort;
            recv->address = "/infinite/osctest";
         }
         if (frameId == 60)
         {
            const float got = recv->Value01();
            const bool ok = std::fabs(got - constNode->value) < 0.02f;
            printf("osc sent=%.3f received=%.3f  %s\n", constNode->value, got,
                   ok ? "ROUND TRIP OK" : "BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // Must run before the cook loop just below - see
      // ArrangeSeekVideoSampleSources's own comment for why.
      ArrangeSeekVideoSampleSources(Transport::Instance().Beats());

      // A bypassed node is out of the chain, so it does no work at all: a
      // sender (Syphon/Spout Out, OSC Send) stops sending, a receiver
      // (camera, Syphon In, video) stops pulling frames into the graph, and
      // a GPU effect stops rendering. Nothing reads its texture - every
      // image read resolves past it (ImageCable::Resolved).
      {
         // B6 only: the whole-graph cook is outside every other stage, so
         // without this the canvas bench cannot tell cook time from UI time.
         ConditionalStageTimer timerCookAll((benchB6Stages && benchStagesCpuSample) ? &sStageCookAll : nullptr, Bench::FrameTail::kCookAll);
         for (GraphNode& gn : gNodes)
            if (!gn.node->bypassed)
               gn.node->CookIfNeeded(frameId);
      }

      // Arrangement monitor (overhaul WP4): after the cook, so the clips it
      // selects and the textures it reads belong to the same frame.
      CompositeArrangeMonitorIfRequested();
      ReapArrangeGeomViewports();
      // Unconditional, panel open or not: the audio thread's peak ring has
      // to be drained every frame or it fills and starts dropping buckets
      // the waveform would never get back (WP8).
      ArrangeSyncClipVisuals();

      // The node cook loop above is the longest single stretch of the frame,
      // and it runs after glfwPollEvents() with the run loop otherwise
      // unserviced - see local-prompts/02-plugin-editor-lag.md. Pump it here
      // so a hosted plugin's editor window (the app's only real NSWindow)
      // doesn't sit starved for the length of a heavy cook. Called
      // unconditionally, not gated on AnyPluginEditorOpen(): on Linux (task
      // 4.3) a plugin can register IRunLoop timers through the
      // factory-context host object with no editor open at all, and those
      // still need servicing every frame. Cheap when idle on every
      // platform - macOS's CFRunLoopRunInMode(..., 0.0, ...) and Windows'
      // PeekMessageW(nullptr, ...) are both no-ops with nothing queued.
      Platform::PumpPluginEditorEvents();

      // Top-level idle gate: NodeWorkCounter() only advances when some node
      // actually redid real work this frame (FilterNode's RunShaderPass,
      // Render3DNode's draw passes, ...) - a cache hit leaves it alone. A
      // patch with nothing left to compute settles into idle==true every
      // frame within a couple frames of its last real edit, the same way a
      // static Blender viewport stops re-rendering. This only observes that;
      // it deliberately does not skip glfwSwapBuffers/ImGui's own redraw,
      // since those still have to run for UI responsiveness (hover states,
      // cursor, animated widgets) regardless of whether the node graph is idle.
      FrameTest_CACHETEST(frameId, window);

      if (getenv("INFINITE_TEXTFIT") != nullptr && frameId == 4 && !gNodes.empty())
      {
         if (auto* t = dynamic_cast<TextNode*>(gNodes[0].node.get()))
            printf("requested %.0f pt -> fitted %.1f pt (box %.0fx%.0f of %dx%d)\n",
                   t->fontSize, t->FittedSize(),
                   t->width * t->wrapWidth, t->height * t->wrapHeight,
                   t->GetOutputWidth(), t->GetOutputHeight());
      }

      if (selfTest && frameId >= 1)
      {
         int failures = 0;
         for (GraphNode& gn : gNodes)
         {
            // modulators emit a value, not a texture, so they are checked
            // differently - including nodes that expose taps rather than being
            // modulators themselves (Image/Audio Analyze).
            if (dynamic_cast<CameraNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<LightNode*>(gn.node.get()) != nullptr)
            {
               printf("%-22s [%-12s] scene node     OK\n", gn.typeName.c_str(), gn.category.c_str());
               continue;
            }
            if (auto* op = dynamic_cast<GeometryOpNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), op->TriangleCount());
               continue;
            }
            if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu instances  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), inst->InstanceCount());
               continue;
            }
            if (auto* ps = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu particles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), ps->AliveCount());
               continue;
            }
            if (dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MappingNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<ClothNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<WrapNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<Group3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<CurveNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<ImageToPointsNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<DepthProjectionNode*>(gn.node.get()) != nullptr)
            {
               // Pass-throughs and samplers with nothing patched in are empty
               // by definition, so that is not a failure.
               printf("%-22s [%-12s] geometry pass  OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }
            if (auto* oc = dynamic_cast<OceanNode*>(gn.node.get()))
            {
               const bool ok = oc->TriangleCount() > 0;
               if (!ok) ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), oc->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }
            if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), t3d->TriangleCount());
               continue;
            }
            if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
            {
               // A freshly spawned Model 3D has no file yet, so an empty mesh
               // here is correct rather than a failure - same as a Geometry Op
               // with nothing patched into it.
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), model->TriangleCount());
               continue;
            }
            if (auto* geo = dynamic_cast<GeometryNode*>(gn.node.get()))
            {
               // geometry emits a mesh, not a texture
               const bool ok = geo->TriangleCount() > 0;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), geo->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }

            if (dynamic_cast<IAudioSource*>(gn.node.get()) != nullptr ||
                dynamic_cast<AudioOutputNode*>(gn.node.get()) != nullptr)
            {
               // P2's audio-graph nodes (AudioFileNode included, now that
               // it's a real IAudioSource) emit real-time samples, not a
               // texture or a 0-1 modulator value.
               printf("%-22s [%-12s] audio node     OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }

            IModulator* mod = dynamic_cast<IModulator*>(gn.node.get());
            if (mod == nullptr)
               mod = gn.node->ModulatorOutput(0);
            if (mod != nullptr)
            {
               const float v = mod->Value01();
               const bool ok = v >= 0.0f && v <= 1.0f;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] value=%.3f      %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), v, ok ? "OK" : "FAIL");
               continue;
            }

            bool ok = gn.node->GetOutputWidth() > 0 && gn.node->GetOutputTexture() != 0;
            if (!ok)
               ++failures;
            printf("%-22s [%-12s] %dx%d tex=%-3u %s\n",
                   gn.typeName.c_str(), gn.category.c_str(),
                   gn.node->GetOutputWidth(), gn.node->GetOutputHeight(),
                   gn.node->GetOutputTexture(), ok ? "OK" : "FAIL");
         }
         printf("\n%zu node types, %d failures\n", gNodes.size(), failures);
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (gOfflineRender.active)
         DrawOfflineRenderProgressWindow();
      DrawArrangeWavRenderProgressWindow();
      DrawArrangeRenderFailNotice();
      if (splashEnabled)
         Splash::Draw();

      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      {
         ConditionalStageTimer timerImGuiRender(benchStagesCpuSample ? &sStageImGuiRender : nullptr, Bench::FrameTail::kImGuiRender);
         Bench::ConditionalGpuStageTimer timerImGuiRenderGpu(benchStagesSample ? &sGpuTimerRing : nullptr, "imgui_render", frameId);
         ImGui::Render();
         glViewport(0, 0, fbW, fbH);
         // Backs every transparent ImGui child/window (ChildBg/WindowBg alpha 0
         // by default, see ApplyTheme) - any panel that skips
         // PushElevatedPanelStyle shows this colour through, so it has to track
         // the theme rather than stay a fixed dark constant or a light theme
         // renders that gap as near-black.
         const CategoryColors::UiTheme& clearTheme = CategoryColors::CurrentUiTheme();
         glClearColor(clearTheme.windowBg.r, clearTheme.windowBg.g, clearTheme.windowBg.b, 1.0f);
         glClear(GL_COLOR_BUFFER_BIT);
         ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      }

      if (const char* shotPath = getenv("IMAGERESYNTH_SCREENSHOT"))
      {
         const int targetFrame = getenv("INFINITE_SCREENSHOT_FRAME") ? atoi(getenv("INFINITE_SCREENSHOT_FRAME")) : 12;
         if (frameId == targetFrame)
         {
            std::vector<unsigned char> px(fbW * fbH * 4);
            glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            stbi_flip_vertically_on_write(1);
            // Report the real result. This printed "wrote ..." unconditionally,
            // so a failed write announced success - and on macOS it fails
            // easily, because Cocoa chdir's a bundled app to
            // Contents/Resources and a relative shotPath then names a
            // directory that does not exist there. A screenshot harness whose
            // only signal is this line was reading a lie.
            if (stbi_write_png(shotPath, fbW, fbH, 4, px.data(), fbW * 4))
               printf("wrote %s (%dx%d)\n", shotPath, fbW, fbH);
            else
               printf("FAILED to write %s (%dx%d)\n", shotPath, fbW, fbH);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (Bench::Tail().active)
      {
         PollTailFences();
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkCanvasSwap, Bench::ScopedStageTimer::NowMs());
      }
      {
         ConditionalStageTimer timerSwap(benchStagesCpuSample ? &sStageSwap : nullptr, Bench::FrameTail::kSwap);
         glfwSwapBuffers(window);
      }
      if (Bench::Tail().active)
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkSwapped, Bench::ScopedStageTimer::NowMs());
      gTailCanvasFence.Place(true);
      if (frameId == 0)
         sFirstFrameEndMs = Bench::ScopedStageTimer::NowMs();

      // glfwSwapBuffers blocks on vsync - dead time for AppKit to service a
      // hosted plugin's editor window. See local-prompts/02-plugin-editor-lag.md.
      // Unconditional for the same reason as the call above (Linux
      // factory-context IRunLoop timers with no editor open).
      Platform::PumpPluginEditorEvents();
      if (Bench::Tail().active)
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkPumped, Bench::ScopedStageTimer::NowMs());

      // Projector output: blit this frame's cooked result of each open
      // window's node into that window. Runs after the editor's own swap so
      // it never shows last frame's texture a frame behind the graph.
      // Iterate backwards since a dead source erases its window mid-loop.
      //
      // Windows will demote a HWND_TOPMOST window in real situations
      // (another app going topmost, a UAC prompt, display hot-plug, a
      // resolution change), so a fullscreen projector's topmost state is
      // reasserted on a throttle - cheap and imperceptible at 0.5s, wasteful
      // to do every frame.
      static double sLastTopmostRefresh = 0.0;
      
      now = glfwGetTime();
      const bool refreshTopmost = now - sLastTopmostRefresh >= 0.5;
      // Projectors present in two passes around the frame clock's wait (Block
      // 2 step 3a): every window renders into its back buffer and flushes
      // first, so the GPU works through the blits while the loop waits; only
      // the swaps run after the wait returns, so the presents land as soon
      // after the refresh as possible. A dead source's window is closed in the
      // first pass, so the second only sees windows that rendered.
      // `projectors` (stage timer and Bench::Tail) is both passes; the wait
      // between them is idle time, not work.
      double projWorkMs = 0.0;
      const bool timeProjectors = benchStagesCpuSample || Bench::Tail().active;
      {
         const double projRenderStartMs = timeProjectors ? Bench::ScopedStageTimer::NowMs() : 0.0;
         for (size_t i = gProjectorWindows.size(); i-- > 0; )
         {
            GraphNode* src = FindNodeByIndex(gProjectorWindows[i].nodeIndex);
            if (src == nullptr)
            {
               // Its node was deleted out from under it - close rather than sit
               // there showing a permanently blank window.
               CloseProjectorWindow(i);
               continue;
            }

            GLFWwindow* projWindow = gProjectorWindows[i].window;
#if defined(_WIN32)
            if (gProjectorWindows[i].fullscreen && refreshTopmost)
               Platform::ReassertOutputWindowTopmost(projWindow);
#endif
            glfwMakeContextCurrent(projWindow);
            int pw, ph;
            glfwGetFramebufferSize(projWindow, &pw, &ph);

            // A geometry node's own GetOutputTexture() isn't a real preview (it
            // produces a mesh, not pixels) - render it the same way its
            // mini-viewport/viewport-panel card does instead, sharing that
            // node's own orbit camera so all three agree on framing.
            unsigned int tex = 0;
            int texW = 0, texH = 0;
            if (auto* geo = dynamic_cast<IGeometrySource*>(src->node.get()))
            {
               NodeViewport& viewport = gProjectorViewports[src->index];
               SharedViewportCamera& cam = gNodeCameras[src->index];
               tex = viewport.Render(dynamic_cast<IGeometrySource*>(DisplayNode(src->node.get())), cam, pw, ph);
            }
            else if (INode* shown = DisplayNode(src->node.get()))
            {
               // A bypassed node projects what passes through it, and a bypassed
               // source projects nothing (the clear below), never a frozen frame.
               tex = shown->GetOutputTexture();
               texW = shown->GetOutputWidth();
               texH = shown->GetOutputHeight();
            }

            if (tex != 0)
            {
               if (dynamic_cast<ProjectionNode*>(DisplayNode(src->node.get())) != nullptr)
                  GLUtil::DrawTextureToScreen(tex, pw, ph, 0, 0, /*checkerBg=*/false);
               else
                  GLUtil::DrawTextureToScreen(tex, pw, ph, texW, texH, /*checkerBg=*/true,
                                              /*transparentWindow=*/!gProjectorWindows[i].fullscreen);
            }
            else
            {
               glViewport(0, 0, pw, ph);
               glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
               glClear(GL_COLOR_BUFFER_BIT);
            }
            glFlush();
         }
         if (timeProjectors)
            projWorkMs += Bench::ScopedStageTimer::NowMs() - projRenderStartMs;
      }
      // The frame clock's wait: the primary Output display's refresh with a
      // projector open, else the canvas's when Vsync is on, so the presents
      // below land on its grid whether or not the canvas (or the projector)
      // is covered or hidden.
      PaceProjectorPresent(window);
      {
         const double projSwapStartMs = timeProjectors ? Bench::ScopedStageTimer::NowMs() : 0.0;
         for (size_t i = gProjectorWindows.size(); i-- > 0; )
         {
            GLFWwindow* projWindow = gProjectorWindows[i].window;
            glfwMakeContextCurrent(projWindow);
            // B8: present cost and swap-to-swap interval per window. CPU only -
            // a GL timer query must never span this context switch.
            const double benchB8SwapStartMs = (sBenchB8Sampling && i < sBenchB8Win.size())
                                                 ? Bench::ScopedStageTimer::NowMs() : -1.0;
            glfwSwapBuffers(projWindow);
            if (i == 0 && Bench::Tail().active)
            {
               // Window 0 is the last to present (backwards loop): its fence
               // covers every projector's work this frame.
               static double sTailLastProjSwapMs = -1.0;
               const double swapMs = Bench::ScopedStageTimer::NowMs();
               sTailProjIntervalMs = sTailLastProjSwapMs >= 0.0 ? swapMs - sTailLastProjSwapMs : -1.0;
               sTailLastProjSwapMs = swapMs;
               gTailProjFence.Place(false);
            }
            if (benchB8SwapStartMs >= 0.0)
            {
               const double endMs = Bench::ScopedStageTimer::NowMs();
               BenchB8Window& bw = sBenchB8Win[i];
               bw.presentMs.Push(endMs - benchB8SwapStartMs);
               if (bw.lastSwapMs >= 0.0)
               {
                  const double iv = endMs - bw.lastSwapMs;
                  bw.intervalMs.Push(iv);
                  const double refreshMs = bw.refreshHz > 0 ? 1000.0 / (double)bw.refreshHz : sBenchB8RefreshMs;
                  if (std::fabs(iv - std::max(1.0, std::round(iv / refreshMs)) * refreshMs) <= 1.5)
                     bw.onVsync++;
               }
               bw.lastSwapMs = endMs;
            }
            if (isBenchB3 && frameId >= 32)
            {
               const double nowSwapMs = Bench::ScopedStageTimer::NowMs();
               if (sBenchB3LastProjSwapMs > 0.0)
               {
                  const double intervalMs = nowSwapMs - sBenchB3LastProjSwapMs;
                  sBenchB3ProjIntervalRing.Push(intervalMs);
                  const double expectedIntervalMs = 1000.0 / (sBenchB3MonitorRefreshHz > 0 ? (double)sBenchB3MonitorRefreshHz : 60.0);
                  if (intervalMs > 1.5 * expectedIntervalMs)
                     sBenchB3MissedVsyncCount++;
                  sBenchB3TotalVsyncCount++;
               }
               sBenchB3LastProjSwapMs = nowSwapMs;
            }
         }
         if (timeProjectors)
         {
            projWorkMs += Bench::ScopedStageTimer::NowMs() - projSwapStartMs;
            if (benchStagesCpuSample)
               sStageProjectors.Push(projWorkMs);
            Bench::Tail().AddStage(Bench::FrameTail::kProjectors, projWorkMs);
         }
         if (refreshTopmost && !gProjectorWindows.empty())
            sLastTopmostRefresh = now;
         if (!gProjectorWindows.empty())
            glfwMakeContextCurrent(window);
      }   return -1;
}
}
