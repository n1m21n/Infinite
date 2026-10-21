// Turbo 0.60: upstream's per-voice modulation fixture (src/app/selftest/DspFixtures2.cpp,
// RunVoiceModFixture), verbatim. INFINITE_VOICEMODTEST runs it headless.

// Per-voice modulation (docs/plans/modulation/per-voice.md): the audio-thread
// kernel against the node it copies, note-on restarts per voice, the route's
// shaping against CookFrame's, and a real synth taking a plan.
bool RunVoiceModFixture()
{
   bool all = true;
   auto check = [&](const char* what, double got, double want, double tol) {
      const bool ok = std::fabs(got - want) <= tol;
      printf("VOICEMODTEST %-58s got %.4f want %.4f  %s\n", what, got, want, ok ? "OK" : "FAIL");
      all &= ok;
   };
   constexpr double kSr = 48000.0;
   using Rt = VoiceMod::Runtime<8>;
   auto box = std::make_unique<VoiceMod::PlanBox>();
   auto plan = std::make_unique<VoiceMod::Plan>();
   auto rt = std::make_unique<Rt>();
   rt->Prepare(kSr);
   auto oneRoute = [&](const VoiceMod::Spec& sp) {
      *plan = VoiceMod::Plan();
      plan->numSources = 1;
      plan->sources[0] = sp;
      plan->numRoutes = 1;
      plan->routes[0] = VoiceMod::Route();
      plan->routes[0].param = 0;
      box->Publish(*plan);
   };
   float last = 0.0f;
   auto run = [&](int voice, int samples) {
      for (int i = 0; i < samples; i++)
         rt->Apply(voice, [&](int, float x) { last = x; });
   };

   Transport& tr = Transport::Instance();
   const float tempoBefore = (float)tr.Tempo();
   tr.SetPlaying(false);
   tr.Rewind();
   tr.SetTempo(120.0f);
   tr.SetPlaying(true);

   // --- (a) the kernel plays the node's shape at the node's rate
   {
      LFONode lfo;
      lfo.voice = true;
      lfo.shape = 1; // triangle
      lfo.rateMode = LFONode::kRateBeats;
      lfo.rateBeats = 2.0f; // 1 cycle a second at 120
      lfo.phase = 0.1f;
      lfo.low = 0.2f;
      lfo.high = 0.9f;
      VoiceMod::Spec sp;
      check("LFO with per voice on gives a spec", lfo.VoiceSpec(sp) ? 1 : 0, 1, 0);
      oneRoute(sp);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(0);
      const double seconds = 0.37;
      run(0, (int)std::lround(seconds * kSr));
      for (int i = 0; i < 74; i++)
         tr.Tick(0.005f);
      check("kernel LFO = node LFO, 0.37 s from start", rt->Peek(0, 0), lfo.Value01(), 2e-4);
      lfo.voice = false;
      check("per voice off gives no spec", lfo.VoiceSpec(sp) ? 1 : 0, 0, 0);
   }

   // --- (b) Random: each note draws its own sequence, inside low..high
   {
      RandomNode rnd;
      rnd.voice = true;
      rnd.low = 0.25f;
      rnd.high = 0.75f;
      VoiceMod::Spec sp;
      rnd.VoiceSpec(sp);
      oneRoute(sp);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(0);
      rt->NoteOn(1);
      bool inside = true, differ = false;
      for (int step = 0; step < 16; step++)
      {
         run(0, 4000);
         run(1, 4000);
         const float a = rt->Peek(0, 0), b = rt->Peek(1, 0);
         inside &= a >= 0.25f && a <= 0.75f && b >= 0.25f && b <= 0.75f;
         differ |= std::fabs(a - b) > 1e-3f;
      }
      check("Random stays inside low..high", inside ? 1 : 0, 1, 0);
      check("Random: two notes draw different values", differ ? 1 : 0, 1, 0);
   }

   // --- (c) MSEG: a ramp per voice, held loops, release plays out
   {
      MSEGNode env;
      env.voice = true;
      env.points = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
      env.rateMode = LFONode::kRateHz;
      env.rateHz = 1.0f; // one shape a second
      env.loop = true;
      env.loopStart = 0.25f;
      env.loopEnd = 0.75f;
      VoiceMod::Spec sp;
      env.VoiceSpec(sp);
      oneRoute(sp);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(0);
      run(0, 12000); // 0.25 s
      rt->NoteOn(1);
      run(0, 12000);
      run(1, 12000);
      check("MSEG voice 0 at 0.5 s", rt->Peek(0, 0), 0.5, 1e-3);
      check("MSEG voice 1, started 0.25 s later, at 0.25", rt->Peek(1, 0), 0.25, 1e-3);
      run(0, 24000); // 1.0 s held: 0.25 + fmod(0.75, 0.5) = 0.5
      check("MSEG held 1 s loops 0.25-0.75", rt->Peek(0, 0), 0.5, 1e-3);
      rt->NoteOff(0);
      run(0, 48000);
      check("MSEG released plays out to the end", rt->Peek(0, 0), 1.0, 1e-6);
      check("MSEG voice 1 is untouched by voice 0's release", rt->Peek(1, 0), 0.25, 1e-3);
   }

   // --- (d) route shaping matches CookFrame: curve, lo/hi, step, clamp
   {
      MSEGNode flat;
      flat.voice = true;
      flat.points = { { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.5f, 0.0f } };
      VoiceMod::Spec sp;
      flat.VoiceSpec(sp);
      oneRoute(sp);
      VoiceMod::Route& r = plan->routes[0];
      r.curve = 1.0f / 3.0f; // pow(0.5, 2) = 0.25
      r.lo = 0.0f;
      r.hi = 1.0f;
      r.minValue = 0.0f;
      r.maxValue = 1.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(2);
      run(2, 1);
      check("curve 1/3 bends 0.5 to 0.25", last, 0.25, 1e-5);
      r.lo = 100.0f;
      r.hi = 1000.0f;
      r.maxValue = 2000.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      run(2, 1);
      check("lo/hi 100..1000 maps 0.25 to 325", last, 325.0, 1e-3);
      r.step = 50.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      run(2, 1);
      check("step 50 snaps 325 to 350", last, 350.0, 1e-3);
      r.maxValue = 300.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      run(2, 1);
      check("the param's max clamps it to 300", last, 300.0, 1e-3);
      r.lo = 0.0f;
      r.maxValue = 2000.0f;
      r.step = 0.0f;
      r.curve = 0.0f;
      r.smoothMs = 10.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(3); // a note starts its smoothing on the value, not from 0
      run(3, 1);
      check("smoothing starts on the value at note-on", last, 500.0, 1e-3);
      plan->sources[0].points[0].y = 1.0f;
      plan->sources[0].points[1].y = 1.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      run(3, 480); // 10 ms = one tau: 1 - 1/e of the way from 500 to 1000
      check("smoothing: one tau covers 1-1/e", last, 1000.0 - 500.0 * std::exp(-1.0), 1.5);
      box->Publish(VoiceMod::Plan());
      rt->BeginBlock(*box, 120.0);
      check("an empty plan turns the runtime off", rt->Active() ? 1 : 0, 0, 0);
   }

   // --- (e) Analog: a real synth runs a plan per voice
   {
      AnalogNode syn;
      check("Analog cutoff is a voice param", syn.VoiceParamFor(&syn.cutoff) >= 0 ? 1 : 0, 1, 0);
      check("Analog volume is not a voice param", syn.VoiceParamFor(&syn.volume), -1, 0);
      const int numFrames = 480;
      auto render = [&](bool withPlan, std::vector<float>& out) {
         AnalogNode n;
         n.attack = 1.0f;
         n.sustain = 1.0f;
         AudioNode* audio = n.GetAudioNode();
         audio->PrepareToPlay(kSr, numFrames);
         n.CookIfNeeded(1);
         if (withPlan)
         {
            LFONode lfo;
            lfo.voice = true;
            lfo.rateMode = LFONode::kRateHz;
            lfo.rateHz = 2.0f;
            VoiceMod::Plan p;
            p.numSources = 1;
            lfo.VoiceSpec(p.sources[0]);
            p.sources[0].id = 77;
            p.numRoutes = 1;
            p.routes[0].param = n.VoiceParamFor(&n.cutoff);
            p.routes[0].lo = 200.0f;
            p.routes[0].hi = 4000.0f;
            p.routes[0].minValue = 20.0f;
            p.routes[0].maxValue = 20000.0f;
            n.PublishVoicePlan(p);
         }
         NoteEventQueue inbox;
         audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
         std::vector<float> c0(numFrames), c1(numFrames);
         float* chans[2] = { c0.data(), c1.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = numFrames;
         auto blocks = [&](int count) {
            for (int b = 0; b < count; b++)
            {
               audio->ProcessBlock(nullptr, 0, buf);
               out.insert(out.end(), c0.begin(), c0.end());
            }
         };
         NoteEvent on;
         on.note = 48;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.voiceId = 1;
         inbox.Push(on);
         blocks(10); // 0.1 s
         on.note = 55;
         on.voiceId = 2;
         inbox.Push(on);
         blocks(20); // voice 0 is 0.3 s in, voice 1 0.2 s
         if (withPlan)
         {
            // sine at 0.6 and 0.4 cycles
            check("Analog voice 0 runs its own LFO (0.6 cycles)", n.DebugVoiceSource(0, 0),
                  0.5 + 0.5 * std::sin(2.0 * M_PI * 0.6), 0.02);
            check("Analog voice 1 runs its own LFO (0.4 cycles)", n.DebugVoiceSource(1, 0),
                  0.5 + 0.5 * std::sin(2.0 * M_PI * 0.4), 0.02);
         }
      };
      std::vector<float> plain, modded;
      render(false, plain);
      render(true, modded);
      double diff = 0.0, energy = 0.0;
      for (size_t i = 0; i < plain.size() && i < modded.size(); i++)
      {
         diff += (plain[i] - modded[i]) * (plain[i] - modded[i]);
         energy += plain[i] * plain[i];
      }
      check("Analog with a plan sounds different (diff/energy > 0.05)", diff / std::max(1e-12, energy) > 0.05 ? 1 : 0, 1, 0);
      bool finite = true;
      for (float x : modded)
         finite &= std::isfinite(x);
      check("Analog with a plan stays finite", finite ? 1 : 0, 1, 0);
   }

   // --- (f) Wavetable: each engine's knobs are distinct voice params
   {
      WavetableNode wt;
      const int e0 = wt.VoiceParamFor(&wt.engines[0].cutoff), e1 = wt.VoiceParamFor(&wt.engines[1].cutoff);
      check("Wavetable engine cutoffs are voice params", e0 >= 0 && e1 >= 0 ? 1 : 0, 1, 0);
      check("Wavetable engine 1's cutoff is its own param", e0 != e1 ? 1 : 0, 1, 0);
      check("Wavetable master volume is not a voice param", wt.VoiceParamFor(&wt.volume), -1, 0);
      // An engine's level driven per voice from a square LFO: the render
      // goes through the per-voice SmoothedBlock copy, so it must differ
      // from the shared render and stay finite.
      auto render = [&](bool withPlan) {
         WavetableNode n;
         n.engines[0].on = true;
         n.engines[0].ampAttack = 1.0f;
         n.engines[0].ampSustain = 1.0f;
         n.engines[1].on = false;
         AudioNode* audio = n.GetAudioNode();
         audio->PrepareToPlay(kSr, 480);
         n.CookIfNeeded(1);
         if (withPlan)
         {
            LFONode lfo;
            lfo.voice = true;
            lfo.shape = 4; // square
            lfo.rateMode = LFONode::kRateHz;
            lfo.rateHz = 5.0f;
            VoiceMod::Plan p;
            p.numSources = 1;
            lfo.VoiceSpec(p.sources[0]);
            p.sources[0].id = 78;
            p.numRoutes = 1;
            p.routes[0].param = n.VoiceParamFor(&n.engines[0].volume);
            p.routes[0].lo = 0.0f;
            p.routes[0].hi = 0.05f;
            p.routes[0].maxValue = 1.0f;
            n.PublishVoicePlan(p);
         }
         NoteEventQueue inbox;
         audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
         std::vector<float> c0(480), c1(480), out;
         float* chans[2] = { c0.data(), c1.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = 480;
         NoteEvent on;
         on.note = 57;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.voiceId = 1;
         inbox.Push(on);
         for (int b = 0; b < 30; b++)
         {
            audio->ProcessBlock(nullptr, 0, buf);
            out.insert(out.end(), c0.begin(), c0.end());
         }
         return out;
      };
      const std::vector<float> plain = render(false), modded = render(true);
      double diff = 0.0, energy = 0.0;
      bool finite = true;
      for (size_t i = 0; i < plain.size() && i < modded.size(); i++)
      {
         diff += (plain[i] - modded[i]) * (plain[i] - modded[i]);
         energy += plain[i] * plain[i];
         finite &= std::isfinite(modded[i]);
      }
      check("Wavetable sounds (shared render energy > 0)", energy > 1e-6 ? 1 : 0, 1, 0);
      check("Wavetable with a plan sounds different", diff / std::max(1e-12, energy) > 0.05 ? 1 : 0, 1, 0);
      check("Wavetable with a plan stays finite", finite ? 1 : 0, 1, 0);
   }

   // --- (h) Voice Spread (2.13): the four spreads, and a chord's notes
   //     each taking their place among the held keys
   {
      using namespace ModShapes;
      check("spread linear, 4: note 2 at 2/3", Spread(kSpreadLinear, 2, 4), 2.0 / 3.0, 1e-6);
      check("spread linear wraps after count (note 5 of 4 = note 1)", Spread(kSpreadLinear, 5, 4), 1.0 / 3.0, 1e-6);
      check("spread golden: note 1 at 0.618", Spread(kSpreadGolden, 1, 8), 0.6180340, 1e-6);
      check("spread golden: note 2 at 0.236", Spread(kSpreadGolden, 2, 8), 0.2360680, 1e-6);
      check("spread primes, 4: note 2 = (5-2)/(7-2)", Spread(kSpreadPrimes, 2, 4), 0.6, 1e-6);
      check("spread primes, 4: last note at 1", Spread(kSpreadPrimes, 3, 4), 1.0, 1e-6);
      check("spread alternate, 4: note 0 left of centre", Spread(kSpreadAlternate, 0, 4), 0.25, 1e-6);
      check("spread alternate, 4: note 1 right of centre", Spread(kSpreadAlternate, 1, 4), 0.75, 1e-6);
      check("spread alternate, 4: note 3 at the right edge", Spread(kSpreadAlternate, 3, 4), 1.0, 1e-6);
      check("spread of one note is the centre", Spread(kSpreadGolden, 3, 1), 0.5, 1e-6);
      bool inside = true;
      for (int m = 0; m < 4; m++)
         for (int c = 1; c <= kSpreadMaxCount; c++)
            for (int i = 0; i < 40; i++)
            {
               const float v = Spread(m, i, c);
               inside &= v >= 0.0f && v <= 1.0f;
            }
      check("every spread stays in 0..1", inside ? 1 : 0, 1, 0);

      VoiceSpreadNode sp;
      sp.mode = kSpreadLinear;
      sp.count = 3;
      sp.low = 0.2f;
      sp.high = 0.8f;
      VoiceMod::Spec spec;
      check("Voice Spread is always per voice", sp.VoiceSpec(spec) ? 1 : 0, 1, 0);
      check("Voice Spread shared value = first note's", sp.Value01(), 0.2, 1e-6);
      oneRoute(spec);
      auto rs = std::make_unique<Rt>();
      rs->Prepare(kSr);
      rs->BeginBlock(*box, 120.0);
      rs->NoteOn(5); // a chord lands on whatever slots are free
      rs->NoteOn(2);
      rs->NoteOn(7);
      check("chord note 1 (slot 5) at low", rs->Peek(5, 0), 0.2, 1e-6);
      check("chord note 2 (slot 2) in the middle", rs->Peek(2, 0), 0.5, 1e-6);
      check("chord note 3 (slot 7) at high", rs->Peek(7, 0), 0.8, 1e-6);
      rs->NoteOff(2);
      rs->NoteOn(0); // two keys held: the new one is the third
      check("a note played over two held keys is third", rs->Peek(0, 0), 0.8, 1e-6);
      check("held notes keep their place", rs->Peek(5, 0), 0.2, 1e-6);
      rs->NoteOn(5); // the same key again, still held by itself only: two others held
      check("a retriggered key counts the others, not itself", rs->Peek(5, 0), 0.8, 1e-6);
   }

   // --- (i) Envelope (2.14): AHDSR with per-stage curves. Hold 0 and
   // curves 0 is the straight ADSR the node had; per voice, each voice is
   // gated by its own note-on and note-off.
   {
      ModShapes::Ahdsr e;
      e.attackMs = 10.0f;
      e.holdMs = 0.0f;
      e.decayMs = 40.0f;
      e.sustain = 0.5f;
      e.releaseMs = 20.0f;
      auto at = [&](const ModShapes::Ahdsr& env, double sec) {
         int stage = ModShapes::kEnvAttack;
         double start = 0.0;
         float from = 0.0f;
         return ModShapes::AhdsrLevel(env, stage, start, from, sec);
      };
      check("straight attack halfway", at(e, 0.005), 0.5, 1e-5);
      check("straight decay halfway", at(e, 0.030), 0.75, 1e-5);
      check("sustain", at(e, 0.5), 0.5, 1e-6);
      e.holdMs = 15.0f;
      check("hold keeps the peak", at(e, 0.020), 1.0, 1e-6);
      check("decay starts after the hold", at(e, 0.045), 0.75, 1e-5);
      e.decayCurve = 0.5f;
      check("curved decay = MsegSegment's bend", at(e, 0.045), ModShapes::MsegSegment(1.0f, 0.5f, 0.5f, 0.5f), 1e-6);
      check("positive decay curve falls fast first", at(e, 0.045) < 0.75 ? 1 : 0, 1, 0);
      {
         int stage = ModShapes::kEnvRelease;
         double start = 0.0;
         float from = 0.8f;
         e.releaseCurve = -0.5f;
         check("release starts from its level, curved",
               ModShapes::AhdsrLevel(e, stage, start, from, 0.010), ModShapes::MsegSegment(0.8f, 0.0f, -0.5f, 0.5f), 1e-6);
      }

      EnvelopeNode env;
      VoiceMod::Spec spec;
      check("Envelope shared by default", env.VoiceSpec(spec) ? 1 : 0, 0, 0);
      env.voice = true;
      env.output = EnvelopeNode::kOutCentred;
      check("centred output stays shared", env.VoiceSpec(spec) ? 1 : 0, 0, 0);
      env.output = EnvelopeNode::kOutAdsr;
      env.attackMs = 10.0f;
      env.holdMs = 5.0f;
      env.decayMs = 50.0f;
      env.sustainLevel = 0.5f;
      env.releaseMs = 20.0f;
      env.attackCurve = env.decayCurve = env.releaseCurve = 0.0f;
      check("Envelope per voice with ADSR output", env.VoiceSpec(spec) ? 1 : 0, 1, 0);
      EnvelopeNode fresh;
      check("a new Envelope's decay falls fast first", fresh.decayCurve > 0.0f ? 1 : 0, 1, 0);
      fresh.BeforeLoadParams();
      check("an older patch loads straight", fresh.decayCurve + fresh.releaseCurve + fresh.holdMs, 0.0, 0.0);

      oneRoute(spec);
      auto re = std::make_unique<Rt>();
      re->Prepare(kSr);
      re->BeginBlock(*box, 120.0);
      auto runOn = [&](Rt& r, int voice, double ms) {
         for (int i = 0, n = (int)std::lround(ms * kSr / 1000.0); i < n; i++)
            r.Apply(voice, [&](int, float x) { last = x; });
      };
      re->NoteOn(3);
      runOn(*re, 3, 5.0);
      check("voice 3 attack halfway at 5 ms", re->Peek(3, 0), 0.5, 2e-3);
      check("voice 0, never played, at rest", re->Peek(0, 0), 0.0, 0.0);
      runOn(*re, 3, 7.0);
      check("voice 3 holds at 12 ms", re->Peek(3, 0), 1.0, 1e-6);
      runOn(*re, 3, 28.0);
      check("voice 3 decay halfway at 40 ms", re->Peek(3, 0), 0.75, 2e-3);
      runOn(*re, 3, 60.0);
      check("voice 3 sustains", re->Peek(3, 0), 0.5, 1e-6);
      re->NoteOn(6);
      runOn(*re, 6, 5.0);
      check("voice 6 starts its own attack", re->Peek(6, 0), 0.5, 2e-3);
      check("voice 3 still sustains", re->Peek(3, 0), 0.5, 1e-6);
      re->NoteOff(3);
      runOn(*re, 3, 10.0);
      check("voice 3 release halfway after its note-off", re->Peek(3, 0), 0.25, 2e-3);
      const float before = re->Peek(3, 0);
      re->NoteOn(3);
      check("a retrigger starts the attack where release was", re->Peek(3, 0), before, 2e-3);
      re->NoteOff(3);
      runOn(*re, 3, 30.0);
      check("released to rest", re->Peek(3, 0), 0.0, 1e-6);
   }

   // --- (j) Note Expression (2.15): each voice follows its own note's MPE
   // pressure or slide, from the note-on and through Express, smoothed.
   {
      NoteExpressionNode ne;
      ne.low = 0.2f;
      ne.high = 0.8f;
      VoiceMod::Spec spec;
      check("Note Expression is always per voice", ne.VoiceSpec(spec) ? 1 : 0, 1, 0);
      oneRoute(spec);
      auto rx = std::make_unique<Rt>();
      rx->Prepare(kSr);
      rx->BeginBlock(*box, 120.0);
      auto runOn = [&](Rt& r, int voice, double ms) {
         for (int i = 0, n = (int)std::lround(ms * kSr / 1000.0); i < n; i++)
            r.Apply(voice, [&](int, float x) { last = x; });
      };
      rx->NoteOn(1, 0.5f, 0.1f);
      rx->NoteOn(4, 1.0f, 0.9f);
      check("voice 1 starts at its note's pressure (no glide in)", rx->Peek(1, 0), 0.2 + 0.6 * 0.5, 1e-6);
      check("voice 4 starts at its own", rx->Peek(4, 0), 0.8, 1e-6);
      runOn(*rx, 1, 1.0);
      rx->Express(1, 0.0f, 0.1f);
      runOn(*rx, 1, 5.0);
      check("a pressure move is smoothed: one time constant in (e^-1 left)", rx->Peek(1, 0),
            0.2 + 0.6 * 0.5 * std::exp(-1.0), 0.01);
      runOn(*rx, 1, 50.0);
      check("and settles", rx->Peek(1, 0), 0.2, 1e-3);
      check("voice 4 does not move with voice 1", rx->Peek(4, 0), 0.8, 1e-6);
      rx->NoteOn(1, 0.9f, 0.0f);
      check("a new note on the slot starts at its own value", rx->Peek(1, 0), 0.2 + 0.6 * 0.9, 1e-6);

      ne.source = VoiceMod::kExprSlide;
      ne.VoiceSpec(spec);
      oneRoute(spec);
      auto rs2 = std::make_unique<Rt>();
      rs2->Prepare(kSr);
      rs2->BeginBlock(*box, 120.0);
      rs2->NoteOn(2, 1.0f, 0.25f);
      check("slide source reads slide, not pressure", rs2->Peek(2, 0), 0.2 + 0.6 * 0.25, 1e-6);

      // Analog end to end: two notes with their own pressure; a Pitch Bend
      // style bendUpdate (no expression) leaves pressure where it was.
      AnalogNode syn;
      syn.attack = 1.0f;
      syn.sustain = 1.0f;
      AudioNode* audio = syn.GetAudioNode();
      audio->PrepareToPlay(kSr, 480);
      syn.CookIfNeeded(1);
      VoiceMod::Plan p;
      p.numSources = 1;
      NoteExpressionNode press;
      press.VoiceSpec(p.sources[0]);
      p.sources[0].id = 91;
      p.numRoutes = 1;
      p.routes[0].param = syn.VoiceParamFor(&syn.cutoff);
      p.routes[0].lo = 200.0f;
      p.routes[0].hi = 4000.0f;
      p.routes[0].minValue = 20.0f;
      p.routes[0].maxValue = 20000.0f;
      syn.PublishVoicePlan(p);
      NoteEventQueue inbox;
      audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
      std::vector<float> c0(480), c1(480);
      float* chans[2] = { c0.data(), c1.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = 480;
      bool finite = true;
      auto blocks = [&](int count) {
         for (int b = 0; b < count; b++)
         {
            audio->ProcessBlock(nullptr, 0, buf);
            for (float x : c0)
               finite &= std::isfinite(x);
         }
      };
      NoteEvent on;
      on.isNoteOn = true;
      on.velocity = 1.0f;
      on.hasExpression = true;
      on.note = 48;
      on.voiceId = 1;
      on.pressure = 0.25f;
      inbox.Push(on);
      on.note = 55;
      on.voiceId = 2;
      on.pressure = 0.75f;
      inbox.Push(on);
      blocks(2);
      check("Analog voice 0 follows its note's pressure", syn.DebugVoiceSource(0, 0), 0.25, 1e-3);
      check("Analog voice 1 follows its own", syn.DebugVoiceSource(1, 0), 0.75, 1e-3);
      NoteEvent ex;
      ex.bendUpdate = true;
      ex.hasExpression = true;
      ex.voiceId = 1;
      ex.note = 48;
      ex.pressure = 1.0f;
      inbox.Push(ex);
      blocks(5); // 50 ms: ten time constants
      check("an expression update moves voice 0 only", syn.DebugVoiceSource(0, 0), 1.0, 1e-3);
      check("voice 1 unchanged", syn.DebugVoiceSource(1, 0), 0.75, 1e-3);
      NoteEvent bend;
      bend.bendUpdate = true;
      bend.voiceId = 2;
      bend.note = 55;
      bend.bendSemitones = 2.0f;
      inbox.Push(bend);
      blocks(2);
      check("a bendUpdate with no expression keeps the note's pressure", syn.DebugVoiceSource(1, 0), 0.75, 1e-3);
      check("Analog with Note Expression stays finite", finite ? 1 : 0, 1, 0);
   }

   // --- (g) the switch saves
   {
      struct BoolGrab : ParamVisitor
      {
         bool* target = nullptr;
         bool set = false;
         bool value = false;
         void Float(const char*, float&) override {}
         void Int(const char*, int&) override {}
         void Text(const char*, std::string&) override {}
         void Color(const char*, float*) override {}
         void Bool(const char* name, bool& v) override
         {
            if (std::strcmp(name, "voice") != 0)
               return;
            if (set)
               v = value;
            else
               value = v;
         }
      };
      LFONode a, b;
      a.voice = true;
      BoolGrab g;
      a.VisitParams(g);
      g.set = true;
      b.VisitParams(g);
      check("LFO per voice survives save/load", b.voice ? 1 : 0, 1, 0);
      MSEGNode m;
      BoolGrab gm;
      m.VisitParams(gm);
      check("MSEG per voice defaults off", gm.value ? 1 : 0, 0, 0);
   }

   tr.SetTempo(tempoBefore);
   printf("VOICEMODTEST %s\n", all ? "OK" : "FAIL");
   return all;
}
