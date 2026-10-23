// INFINITE_NOTEAUDIT: a measurement audit of the note nodes.
//
// The note-side twin of FXAUDIT / MODAUDIT / SYNTHAUDIT (docs/plans/node-audits/
// README.md, block 2). Each node runs through its real node -> audio node
// ProcessBlock, wired to real NoteEventQueues the way the topology builder
// does it, with the Transport clock advanced the way the audio callback does
// it (AdvanceAudioClock before the graph runs), and gets:
//
//   PAIR    a scripted overlapping stream goes in (input nodes) or the
//           transport plays 4 s (source nodes); every output note-on must have
//           exactly one note-off by voiceId, never before its on, none stuck
//           after the input has released everything / the transport stops
//   GRID    (arp, sequencer, random generator, chorder) note-on position in
//           samples against the ideal beat grid at 60/90/120/128/174 bpm and
//           block sizes 64/512/800/333/2048 (24/60/144 fps at 48 kHz); flag > 1
//           sample. Also: 1/64 steps at 174 bpm (two per 2048 block), a loop
//           lap mid-block, and stepTiming 0 (saved patches) still block-quantised
//   SWING   random generator groove 0.5 / 1.0: delay of odd steps against
//           groove * 0.5 * rateBeats, flag > 1 %
//   HUMAN   humanizer timing: spread within the knob, and the same stream
//           after PrepareToPlay gives the same output (seeded determinism)
//   LOOP    a transport loop wrap and a seek during playback: pair integrity
//   JITTER  pass-through nodes: note-on sample out against sample in at 24/60/144
//           fps blocks (2000/800/333 frames) and 64/512/2048; flag > 1 sample
//   BYPASS  a node between a source and a consumer is bypassed (consumer rewired
//           to the source's queue, as the topology builder does) or deleted
//           (consumer's inbox cleared) while notes sound: pair integrity at the
//           consumer, and how many voices it holds open afterwards
//   LOAD    out-of-range ints from a patch file stay inside the node's tables
//   CPU     ns per 512-frame block while active
//
// Only hard invariants fail the run (a note-on without its off at the end of
// the stream). Everything else is reported, with FLAG lines past the
// thresholds above.
#include "app/AppShared.h"
#include "audio/AudioBuffer.h"
#include "audio/MusicTime.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"
#include "nodes/NoteNodes.h"
#include "audio/NoteTheory.h"
#include "core/Patch.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

namespace app
{
namespace
{
   constexpr double kRate = 48000.0;
   constexpr double kFlagGridSamples = 1.0;
   constexpr double kFlagSwingPct = 1.0;

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
         printf("NOTEAUDIT FLAG %-5s %-18s %s %.2f\n", sec, node, what, v);
         n++;
      }
   };

   struct Evt
   {
      long long sample; // absolute: block start + frameOffset
      NoteEvent e;
   };

   // One node wired to numIn input queues, with one consumer on its outbox.
   struct Rig
   {
      std::unique_ptr<INode> node;
      AudioNode* audio = nullptr;
      NoteEventQueue in[2];
      NoteEventQueue* out = nullptr;
      int outCursor = -1;
      int block = 512;
      long long pos = 0;
      int frame = 1;
      std::vector<float> l, r;
      std::vector<Evt> got;

      bool Init(INode* n, int blockSize = 512)
      {
         node.reset(n);
         block = blockSize;
         l.assign(block, 0.0f);
         r.assign(block, 0.0f);
         INoteSource* src = dynamic_cast<INoteSource*>(n);
         if (src == nullptr)
            return false;
         n->CookIfNeeded(frame++);
         audio = src->GetAudioNode();
         if (audio == nullptr)
            return false;
         audio->PrepareToPlay(kRate, block);
         for (int s = 0; s < 8; s++)
         {
            if (n->NoteInputSlot(s) == nullptr)
               continue;
            const int q = std::min(s, 1);
            audio->SetNoteInbox(s, &in[q], in[q].RegisterConsumer());
         }
         out = audio->NoteOutbox();
         if (out == nullptr)
            return false;
         outCursor = out->RegisterConsumer();
         return true;
      }

      // Transport must be playing; advances the audio clock first, as the
      // engine does, then runs one block and collects the outbox.
      void Step(bool advanceClock = true)
      {
         if (advanceClock)
            Transport::Instance().AdvanceAudioClock(block);
         node->CookIfNeeded(frame++);
         AudioBuffer buf;
         float* ch[2] = { l.data(), r.data() };
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = block;
         audio->ProcessBlock(nullptr, 0, buf);
         NoteEvent evts[64];
         int n;
         while ((n = out->Pop(outCursor, evts, 64)) > 0)
            for (int i = 0; i < n; i++)
               got.push_back({ pos + std::clamp(evts[i].frameOffset, 0, block - 1), evts[i] });
         pos += block;
      }
      void Run(double seconds)
      {
         const long long end = pos + (long long)(seconds * kRate);
         while (pos < end)
            Step();
      }
   };

   void StartTransport(double bpm)
   {
      Transport& t = Transport::Instance();
      t.SetOfflineMode(false);
      t.SetLoop(false, 0.0, 4.0);
      t.SetPlaying(false);
      t.SetTempo((float)bpm);
      t.Seek(0.0);
      t.NotifyAudioEngineStarted(kRate);
      t.SetPlaying(true);
   }
   void StopTransport()
   {
      Transport& t = Transport::Instance();
      t.SetPlaying(false);
      t.NotifyAudioEngineStopped();
   }

   void Push(NoteEventQueue& q, int note, float vel, bool on, int voice, int offset = 0)
   {
      NoteEvent e;
      e.note = note;
      e.velocity = vel;
      e.isNoteOn = on;
      e.voiceId = voice;
      e.frameOffset = offset;
      q.Push(e);
   }

   // Pair integrity over an output stream. Returns the number of problems.
   struct PairReport
   {
      int ons = 0, offs = 0, stuck = 0, orphanOff = 0, dupOn = 0, offBeforeOn = 0;
      bool Bad() const { return stuck || orphanOff || dupOn || offBeforeOn; }
   };
   PairReport Pairs(const std::vector<Evt>& got)
   {
      PairReport p;
      std::map<int, long long> open;
      std::map<int, bool> closed;
      for (const Evt& g : got)
      {
         if (g.e.bendUpdate)
            continue;
         if (g.e.isNoteOn)
         {
            p.ons++;
            if (open.count(g.e.voiceId) || closed.count(g.e.voiceId))
               p.dupOn++;
            open[g.e.voiceId] = g.sample;
         }
         else
         {
            p.offs++;
            auto it = open.find(g.e.voiceId);
            if (it == open.end())
               p.orphanOff++;
            else
            {
               if (g.sample < it->second)
                  p.offBeforeOn++;
               open.erase(it);
               closed[g.e.voiceId] = true;
            }
         }
      }
      p.stuck = (int)open.size();
      return p;
   }

   struct Case
   {
      const char* name;
      std::function<INode*()> create;
      std::function<void(INode*)> setup; // param tweaks (optional)
      bool source;                        // no note input: transport drives it
   };

   const std::vector<Case>& Cases()
   {
      static const std::vector<Case> c = {
         { "Note Filter", [] { return NoteFilterNode::Create(); }, nullptr, false },
         { "Note Transpose", [] { return NoteTransposeNode::Create(); }, [](INode* n) { static_cast<NoteTransposeNode*>(n)->semitones = 7; }, false },
         { "Pitch Bend", [] { return PitchBendNode::Create(); }, [](INode* n) { static_cast<PitchBendNode*>(n)->bendSemitones = 1.0f; }, false },
         { "Velocity Curve", [] { return VelocityCurveNode::Create(); }, [](INode* n) { static_cast<VelocityCurveNode*>(n)->curve = 2.0f; }, false },
         { "Gate", [] { return GateNode::Create(); }, [](INode* n) { static_cast<GateNode*>(n)->holdMs = 300.0f; }, false },
         { "Humanizer", [] { return HumanizerNode::Create(); }, [](INode* n) { auto* h = static_cast<HumanizerNode*>(n); h->timingMs = 30.0f; h->velocityPct = 30.0f; }, false },
         { "Quantizer", [] { return QuantizerNode::Create(); }, [](INode* n) { static_cast<QuantizerNode*>(n)->div = 3; }, false },
         { "Glide", [] { return GlideNode::Create(); }, [](INode* n) { static_cast<GlideNode*>(n)->glideMs = 100.0f; }, false },
         { "Note Echo", [] { return NoteEchoNode::Create(); }, nullptr, false },
         // Past 15 repeats the repeat-id keys (voiceId * 16 + k) of two voices overlap; set directly,
         // as a retaining param writer would, so only the audio side's clamp stands in the way.
         { "Note Echo x20", [] { return NoteEchoNode::Create(); }, [](INode* n) { static_cast<NoteEchoNode*>(n)->repeats = 20; }, false },
         { "Note Router", [] { return NoteRouterNode::Create(); }, nullptr, false },
         { "Note Merge", [] { return NoteMergeNode::Create(); }, nullptr, false },
         { "Note Switcher", [] { return NoteSwitcherNode::Create(); }, nullptr, false },
         { "Arpeggiator", [] { return ArpeggiatorNode::Create(); }, nullptr, false },
         { "Chorder", [] { return ChorderNode::Create(); }, nullptr, true },
         { "Note Stack", [] { return NoteStackNode::Create(); }, nullptr, false },
         { "Note Strum", [] { return NoteStrumNode::Create(); }, nullptr, false },
         { "Note Capturer", [] { return NoteCapturerNode::Create(); }, nullptr, false },
         { "Note Sequencer", [] { return NoteSequencerNode::Create(); }, nullptr, true },
         { "Random Note Gen", [] { return RandomNoteGeneratorNode::Create(); }, nullptr, true },
         { "Bouncing Balls", [] { return BouncingBallsNode::Create(); }, nullptr, true },
      };
      return c;
   }

   // Four overlapping notes across the first second, all released by 1.5 s.
   void ScriptedStream(Rig& rig, double tSec)
   {
      struct Ev { double t; int note; bool on; int voice; };
      static const Ev kScript[] = {
         { 0.00, 60, true, 9001 }, { 0.10, 64, true, 9002 }, { 0.20, 67, true, 9003 }, { 0.30, 60, true, 9004 },
         { 0.50, 64, false, 9002 }, { 0.70, 60, false, 9001 }, { 1.00, 67, false, 9003 }, { 1.30, 60, false, 9004 },
      };
      static const double kStep = 0.0; (void)kStep;
      for (const Ev& e : kScript)
      {
         const double t0 = tSec, t1 = tSec + (double)rig.block / kRate;
         if (e.t >= t0 && e.t < t1)
            Push(rig.in[0], e.note, 0.8f, e.on, e.voice, (int)((e.t - t0) * kRate));
      }
   }

   void AuditPair(const Case& c, Flags& flags, bool& ok)
   {
      Rig rig;
      if (!rig.Init(c.create()))
      {
         printf("NOTEAUDIT SKIP %-18s no note outbox\n", c.name);
         return;
      }
      if (c.setup)
         c.setup(rig.node.get());
      rig.node->CookIfNeeded(rig.frame++);
      StartTransport(120.0);
      const double total = c.source ? 4.0 : 4.0;
      while (rig.pos < (long long)(total * kRate))
      {
         if (!c.source)
            ScriptedStream(rig, (double)rig.pos / kRate);
         rig.Step();
      }
      if (c.source)
      {
         StopTransport();
         // A stopped transport must close what it opened: run another second
         // with the transport frozen.
         rig.Run(1.0);
      }
      else
      {
         rig.Run(3.0); // tails: echo repeats, gate hold, strum, glide
         StopTransport();
      }
      const PairReport p = Pairs(rig.got);
      printf("NOTEAUDIT PAIR  %-18s on %3d off %3d | stuck %d orphan-off %d dup-on %d off-before-on %d\n", c.name, p.ons,
             p.offs, p.stuck, p.orphanOff, p.dupOn, p.offBeforeOn);
      if (p.ons == 0)
         printf("NOTEAUDIT NOTE  %-18s produced no notes in this scenario\n", c.name);
      Quality("note.pair_unpaired", c.name, p.stuck + p.orphanOff + p.dupOn + p.offBeforeOn);
      if (p.Bad())
      {
         flags.Flag("PAIR", c.name, "unpaired events", p.stuck + p.orphanOff + p.dupOn + p.offBeforeOn);
         if (p.stuck)
            ok = false;
      }
   }

   // ---- GRID / SWING -----------------------------------------------------
   struct GridSrc
   {
      const char* name;
      std::function<INode*(double rateBeats, float groove, int stepTiming)> create;
      bool needsInput; // arp: hold note 60
   };

   const std::vector<GridSrc>& GridSrcs()
   {
      static const std::vector<GridSrc> g = {
         { "Arpeggiator", [](double rb, float, int timing) {
              auto* a = static_cast<ArpeggiatorNode*>(ArpeggiatorNode::Create());
              a->rateBeats = (float)rb;
              a->octaves = 1;
              a->stepTiming = timing;
              return (INode*)a;
           }, true },
         { "Note Sequencer", [](double rb, float, int timing) {
              auto* s = static_cast<NoteSequencerNode*>(NoteSequencerNode::Create());
              s->rateBeats = (float)rb;
              s->stepTiming = timing;
              return (INode*)s;
           }, false },
         { "Random Note Gen", [](double rb, float groove, int timing) {
              auto* g2 = static_cast<RandomNoteGeneratorNode*>(RandomNoteGeneratorNode::Create());
              g2->rateBeats = (float)rb;
              g2->groove = groove;
              g2->useGlobalScale = false;
              g2->stepTiming = timing;
              return (INode*)g2;
           }, false },
         { "Chorder", [](double rb, float, int timing) {
              auto* c = static_cast<ChorderNode*>(ChorderNode::Create());
              c->rateBeats = (float)rb;
              c->strumMs = 0.0f; // the chord's first note lands on the step itself
              c->humanizeTimingMs = 0.0f;
              c->stepTiming = timing;
              return (INode*)c;
           }, false },
      };
      return g;
   }

   // Runs a source and returns its note-on sample positions (one per sample:
   // a chord's notes share their onset). loopEndBeats > 0 loops 0..loopEnd.
   std::vector<long long> OnPositions(const GridSrc& g, double bpm, int block, double rateBeats, float groove, double seconds,
                                      int stepTiming = 1, double loopEndBeats = 0.0)
   {
      Rig rig;
      std::vector<long long> pos;
      if (!rig.Init(g.create(rateBeats, groove, stepTiming), block))
         return pos;
      rig.node->CookIfNeeded(rig.frame++);
      StartTransport(bpm);
      if (loopEndBeats > 0.0)
         Transport::Instance().SetLoop(true, 0.0, loopEndBeats);
      if (g.needsInput)
         Push(rig.in[0], 60, 0.8f, true, 777);
      rig.Run(seconds);
      StopTransport();
      Transport::Instance().SetLoop(false, 0.0, 4.0);
      for (const Evt& e : rig.got)
         if (e.e.isNoteOn && !e.e.bendUpdate && (pos.empty() || pos.back() != e.sample))
            pos.push_back(e.sample);
      return pos;
   }

   // Worst distance of an onset from the step lattice (-1: no notes).
   double LatticeError(const std::vector<long long>& p, double stepSamples)
   {
      double maxErr = p.empty() ? -1.0 : 0.0;
      for (long long s : p)
      {
         const double k = std::round((double)s / stepSamples);
         maxErr = std::max(maxErr, std::fabs((double)s - k * stepSamples));
      }
      return maxErr;
   }

   void AuditGrid(Flags& flags)
   {
      const double tempos[5] = { 60.0, 90.0, 120.0, 128.0, 174.0 };
      const int blocks[5] = { 64, 512, 800, 333, 2048 };
      const double rateBeats = 0.25;
      for (const GridSrc& g : GridSrcs())
      {
         double worst = 0.0;
         double worstBpm = 0, worstBlock = 0;
         int perBlock[5] = {};
         for (double bpm : tempos)
            for (int bi = 0; bi < 5; bi++)
            {
               const double maxErr =
                  LatticeError(OnPositions(g, bpm, blocks[bi], rateBeats, 0.0f, 6.0), rateBeats * 60.0 / bpm * kRate);
               perBlock[bi] = std::max(perBlock[bi], (int)std::lround(std::max(maxErr, 0.0)));
               if (maxErr > worst)
               {
                  worst = maxErr;
                  worstBpm = bpm;
                  worstBlock = blocks[bi];
               }
            }
         printf("NOTEAUDIT GRID  %-18s worst %7.1f samples (%.0f bpm, block %.0f) | by block 64/512/800/333/2048: %d %d %d %d %d\n",
                g.name, worst, worstBpm, worstBlock, perBlock[0], perBlock[1], perBlock[2], perBlock[3], perBlock[4]);
         Quality("note.grid_samples", g.name, worst);
         if (worst > kFlagGridSamples)
            flags.Flag("GRID", g.name, "worst note-on error samples", worst);
      }

      // Several steps inside one block: 1/64 at 174 bpm is 1034 samples a step, block 2048.
      for (const GridSrc& g : GridSrcs())
      {
         const double fastRate = 1.0 / 64.0, bpm = 174.0, seconds = 2.0;
         const double stepSamples = fastRate * 60.0 / bpm * kRate;
         const std::vector<long long> p = OnPositions(g, bpm, 2048, fastRate, 0.0f, seconds);
         const int expected = (int)std::ceil(std::ceil(seconds * kRate / 2048.0) * 2048.0 / stepSamples); // whole blocks run
         const double err = LatticeError(p, stepSamples);
         printf("NOTEAUDIT GRID  %-18s fast 1/64 at 174 bpm, block 2048: %d of %d steps, worst %.1f samples\n", g.name,
                (int)p.size(), expected, err);
         if (err > kFlagGridSamples || std::abs((int)p.size() - expected) > 1)
            flags.Flag("GRID", g.name, "fast-rate missing steps or error samples", std::max(err, (double)std::abs((int)p.size() - expected)));
      }

      // Loop 0..3 beats at 120 bpm with block 333: the wrap falls mid-block, the step at the loop start
      // must land on the wrap sample. 3 beats is a whole number of steps, so the lattice runs on.
      for (const GridSrc& g : GridSrcs())
      {
         const double bpm = 120.0, seconds = 6.0;
         const double stepSamples = rateBeats * 60.0 / bpm * kRate;
         const std::vector<long long> p = OnPositions(g, bpm, 333, rateBeats, 0.0f, seconds, 1, 3.0);
         const int expected = (int)std::ceil(std::ceil(seconds * kRate / 333.0) * 333.0 / stepSamples); // whole blocks run
         const double err = LatticeError(p, stepSamples);
         printf("NOTEAUDIT GRID  %-18s loop lap (0..3 beats, block 333): %d of %d steps, worst %.1f samples\n", g.name,
                (int)p.size(), expected, err);
         if (err > kFlagGridSamples || std::abs((int)p.size() - expected) > 1)
            flags.Flag("GRID", g.name, "loop-lap missing steps or error samples", std::max(err, (double)std::abs((int)p.size() - expected)));
      }

      // Saved patches: stepTiming 0 must keep the old block-start timing (about one block off).
      for (const GridSrc& g : GridSrcs())
      {
         const double bpm = 90.0;
         const double err = LatticeError(OnPositions(g, bpm, 512, rateBeats, 0.0f, 6.0, 0), rateBeats * 60.0 / bpm * kRate);
         printf("NOTEAUDIT GRID  %-18s legacy (stepTiming 0, 90 bpm, block 512): worst %.1f samples, %s\n", g.name, err,
                err > 256.0 ? "old timing kept" : "OLD TIMING LOST");
         if (err <= 256.0)
            flags.Flag("GRID", g.name, "legacy stepTiming 0 no longer block-quantised", err);
      }
   }

   void AuditSwing(Flags& flags)
   {
      const GridSrc& g = GridSrcs()[2];
      const double bpm = 120.0, rateBeats = 0.25;
      const double stepSamples = rateBeats * 60.0 / bpm * kRate;
      for (float groove : { 0.5f, 1.0f })
      {
         const std::vector<long long> p = OnPositions(g, bpm, 512, rateBeats, groove, 6.0);
         double oddSum = 0.0, evenSum = 0.0;
         int oddN = 0, evenN = 0;
         for (long long s : p)
         {
            // Swing only delays (by less than a step), so the step is the one at or before the onset;
            // round() would hand a groove-1.0 note, exactly half a step late, to the next step.
            const long long k = (long long)std::floor(((double)s + 1.0) / stepSamples);
            const double d = (double)s - (double)k * stepSamples;
            if (k & 1) { oddSum += d; oddN++; }
            else { evenSum += d; evenN++; }
         }
         if (oddN == 0)
         {
            printf("NOTEAUDIT SWING %-18s groove %.1f no odd steps\n", g.name, groove);
            continue;
         }
         const double expected = (double)groove * 0.5 * stepSamples;
         const double got = oddSum / oddN - (evenN ? evenSum / evenN : 0.0);
         const double errPct = std::fabs(got - expected) / stepSamples * 100.0;
         printf("NOTEAUDIT SWING %-18s groove %.1f odd-step delay %8.1f samples, expected %8.1f (%.2f %% of a step off)\n", g.name,
                groove, got, expected, errPct);
         if (errPct > kFlagSwingPct)
            flags.Flag("SWING", g.name, "swing error % of step", errPct);
      }
   }

   // ---- HUMAN ------------------------------------------------------------
   std::vector<long long> HumanRun(float timingMs, bool* withinKnob, double* maxDevMs)
   {
      auto* h = static_cast<HumanizerNode*>(HumanizerNode::Create());
      h->timingMs = timingMs;
      Rig rig;
      std::vector<long long> res;
      if (!rig.Init(h))
         return res;
      rig.node->CookIfNeeded(rig.frame++);
      StartTransport(120.0);
      std::vector<long long> inPos;
      for (int i = 0; i < 20; i++)
      {
         const long long at = (long long)(0.1 * kRate) + (long long)i * (long long)(0.2 * kRate);
         inPos.push_back(at);
      }
      int next = 0;
      while (rig.pos < (long long)(6.0 * kRate))
      {
         while (next < (int)inPos.size() && inPos[next] < rig.pos + rig.block)
         {
            Push(rig.in[0], 60 + next, 0.8f, true, 5000 + next, (int)(inPos[next] - rig.pos));
            Push(rig.in[0], 60 + next, 0.0f, false, 5000 + next, (int)(inPos[next] - rig.pos));
            next++;
         }
         rig.Step();
      }
      StopTransport();
      double maxDev = 0.0;
      for (const Evt& e : rig.got)
      {
         if (!e.e.isNoteOn)
            continue;
         res.push_back(e.sample);
         const int idx = e.e.voiceId - 5000;
         if (idx >= 0 && idx < (int)inPos.size())
            maxDev = std::max(maxDev, std::fabs((double)(e.sample - inPos[idx])) / kRate * 1000.0);
      }
      if (maxDevMs)
         *maxDevMs = maxDev;
      if (withinKnob)
         *withinKnob = maxDev <= (double)timingMs * 1.01 + 1.0;
      return res;
   }

   void AuditHuman(Flags& flags)
   {
      bool within = false;
      double dev = 0.0;
      const std::vector<long long> a = HumanRun(30.0f, &within, &dev);
      const std::vector<long long> b = HumanRun(30.0f, nullptr, nullptr);
      printf("NOTEAUDIT HUMAN Humanizer          timing 30 ms: max deviation %.2f ms (%s), %zu notes\n", dev,
             within ? "within knob" : "OUTSIDE knob", a.size());
      if (!within)
         flags.Flag("HUMAN", "Humanizer", "deviation ms beyond knob", dev);
      const bool same = a == b;
      printf("NOTEAUDIT HUMAN Humanizer          two fresh nodes, same stream: %s\n", same ? "identical" : "different");
      if (!same)
         flags.Flag("HUMAN", "Humanizer", "run-to-run difference (unseeded)", 1.0);
   }

   // ---- LOOP -------------------------------------------------------------
   void AuditLoop(Flags& flags, bool& ok)
   {
      for (const GridSrc& g : GridSrcs())
      {
         Rig rig;
         if (!rig.Init(g.create(0.25, 0.0f, 1), 512))
            continue;
         rig.node->CookIfNeeded(rig.frame++);
         StartTransport(120.0);
         if (g.needsInput)
            Push(rig.in[0], 60, 0.8f, true, 777);
         Transport::Instance().SetLoop(true, 0.0, 3.0); // 1.5 s lap
         rig.Run(5.0);
         Transport::Instance().Seek(0.7);
         rig.Run(1.0);
         Transport::Instance().SetLoop(false, 0.0, 3.0);
         StopTransport();
         rig.Run(1.0);
         const PairReport p = Pairs(rig.got);
         printf("NOTEAUDIT LOOP  %-18s on %3d off %3d | stuck %d orphan-off %d dup-on %d off-before-on %d\n", g.name, p.ons,
                p.offs, p.stuck, p.orphanOff, p.dupOn, p.offBeforeOn);
         if (p.Bad())
         {
            flags.Flag("LOOP", g.name, "unpaired events across loop/seek/stop", p.stuck + p.orphanOff + p.dupOn + p.offBeforeOn);
            if (p.stuck)
               ok = false;
         }
      }
   }


   // ---- JITTER -----------------------------------------------------------
   // Pass-through nodes keep an event's frame offset: note-on sample out must be
   // note-on sample in at every block size the app runs (24/60/144 fps at 48 kHz
   // is 2000/800/333 frames).
   void AuditJitter(Flags& flags)
   {
      const int blocks[6] = { 64, 333, 512, 800, 2000, 2048 };
      for (const Case& c : Cases())
      {
         const std::string n = c.name;
         if (n != "Note Filter" && n != "Note Transpose" && n != "Velocity Curve" && n != "Pitch Bend")
            continue;
         double worst = 0.0;
         int missing = 0;
         for (int block : blocks)
         {
            Rig rig;
            if (!rig.Init(c.create(), block))
               continue;
            if (c.setup)
               c.setup(rig.node.get());
            rig.node->CookIfNeeded(rig.frame++);
            StartTransport(120.0);
            std::vector<long long> inPos;
            for (int i = 0; i < 40; i++)
               inPos.push_back(1000 + (long long)i * 7919 + (i % 5) * 13); // off every block lattice
            int next = 0;
            while (rig.pos < (long long)(8.0 * kRate))
            {
               while (next < (int)inPos.size() && inPos[next] < rig.pos + block)
               {
                  Push(rig.in[0], 60 + next % 12, 0.8f, true, 4000 + next, (int)(inPos[next] - rig.pos));
                  Push(rig.in[0], 60 + next % 12, 0.0f, false, 4000 + next, (int)(inPos[next] - rig.pos));
                  next++;
               }
               rig.Step();
            }
            StopTransport();
            int found = 0;
            for (const Evt& e : rig.got)
               if (e.e.isNoteOn && e.e.voiceId >= 4000 && e.e.voiceId < 4040)
               {
                  found++;
                  worst = std::max(worst, std::fabs((double)(e.sample - inPos[e.e.voiceId - 4000])));
               }
            missing += 40 - found;
         }
         printf("NOTEAUDIT JITTER %-17s worst %.1f samples over blocks 64/333/512/800/2000/2048, %d notes missing\n", c.name,
                worst, missing);
         Quality("note.jitter_samples", c.name, std::max(worst, (double)missing));
         if (worst > kFlagGridSamples || missing > 0)
            flags.Flag("JITTER", c.name, "note-on error samples or missing notes", std::max(worst, (double)missing));
      }
   }

   // ---- BYPASS -----------------------------------------------------------
   // source queue -> node -> consumer (a Note Filter that passes everything, whose
   // output is the pair check). Mid-note the node is bypassed: it stops cooking and the
   // consumer reads the source's queue from the tail, as RebuildAudioTopology does.
   // Deleted: the consumer's inbox is cleared and nothing replaces it.
   // A patch file is outside input and Patch::LoadParams hands ints through
   // unchecked. Each note node must leave a damaged key, range or octave inside
   // the tables its body and audio thread index (fuzz: Chorder SIGBUS, body
   // reads past NoteNameList, (baseOctave + 1) * 12 overflow).
   void AuditLoadClamp(bool& ok)
   {
      const char* kExtremes[] = { "2147483647", "-2147483648", "100000", "-100000" };
      int bad = 0;
      auto check = [&](const char* node, const char* what, int got, int lo, int hi) {
         if (got < lo || got > hi)
         {
            printf("  LOAD %s: %s = %d, outside %d..%d\n", node, what, got, lo, hi);
            bad++;
         }
      };
      for (const char* x : kExtremes)
      {
         auto load = [x](INode* n, std::initializer_list<const char*> keys) {
            std::vector<std::pair<std::string, std::string>> in;
            for (const char* k : keys)
               in.push_back({ std::string("i ") + k, x });
            Patch::LoadParams(n, in);
         };
         std::unique_ptr<INode> kb(KeyboardNode::Create());
         load(kb.get(), { "baseOctave", "transpose" });
         check("Keyboard", "baseOctave", static_cast<KeyboardNode*>(kb.get())->baseOctave, -1, 8);
         check("Keyboard", "transpose", static_cast<KeyboardNode*>(kb.get())->transpose, -127, 127);

         std::unique_ptr<INode> nf(NoteFilterNode::Create());
         load(nf.get(), { "scale", "root", "rangeLow", "rangeHigh" });
         auto* f = static_cast<NoteFilterNode*>(nf.get());
         check("Note Filter", "root", f->root, 0, 11);
         check("Note Filter", "scale", f->scale, 0, MusicTime::kNumScaleTypes - 1);
         check("Note Filter", "rangeLow", f->rangeLow, 0, 127);
         check("Note Filter", "rangeHigh", f->rangeHigh, 0, 127);

         std::unique_ptr<INode> rg(RandomNoteGeneratorNode::Create());
         load(rg.get(), { "scale", "root", "rangeLow", "rangeHigh" });
         auto* r = static_cast<RandomNoteGeneratorNode*>(rg.get());
         check("Random Note Gen", "root", r->root, 0, 11);
         check("Random Note Gen", "scale", r->scale, 0, MusicTime::kNumScaleTypes - 1);
         check("Random Note Gen", "rangeLow", r->rangeLow, 0, 127);
         check("Random Note Gen", "rangeHigh", r->rangeHigh, 0, 127);

         std::unique_ptr<INode> ch(ChorderNode::Create());
         load(ch.get(), { "scale", "root", "chordSize" });
         auto* c = static_cast<ChorderNode*>(ch.get());
         check("Chorder", "root", c->root, 0, 11);
         check("Chorder", "scale", c->scale, 0, MusicTime::kNumScaleTypes - 1);
         check("Chorder", "chordSize", c->chordSize, 2, 6);

         std::unique_ptr<INode> ne(NoteEchoNode::Create());
         load(ne.get(), { "repeats", "transposePerRepeat" });
         auto* e = static_cast<NoteEchoNode*>(ne.get());
         check("Note Echo", "repeats", e->repeats, 1, NoteEchoNode::kMaxRepeats);
         check("Note Echo", "transposePerRepeat", e->transposePerRepeat, -NoteEchoNode::kMaxTransposePerRepeat,
               NoteEchoNode::kMaxTransposePerRepeat);
      }
      // In-range values load unchanged; a root past B keeps its pitch class.
      std::unique_ptr<INode> ch(ChorderNode::Create());
      Patch::LoadParams(ch.get(), { { "i root", "5" }, { "i scale", "3" }, { "i chordSize", "4" } });
      auto* c = static_cast<ChorderNode*>(ch.get());
      if (c->root != 5 || c->scale != 3 || c->chordSize != 4)
      {
         printf("  LOAD Chorder: in-range values changed (%d %d %d)\n", c->root, c->scale, c->chordSize);
         bad++;
      }
      Patch::LoadParams(ch.get(), { { "i root", "14" }, { "i scale", "-1" } });
      if (c->root != 2 || c->scale != MusicTime::kMajor)
      {
         printf("  LOAD Chorder: root 14 / scale -1 gave %d / %d, want 2 / major\n", c->root, c->scale);
         bad++;
      }
      printf("LOAD %s (%d out of range)\n", bad == 0 ? "ok" : "FAIL", bad);
      if (bad != 0)
         ok = false;
   }

   void AuditBypass(Flags& flags, bool& ok)
   {
      for (const Case& c : Cases())
      {
         if (c.source)
            continue;
         for (int mode = 0; mode < 2; mode++) // 0 bypass, 1 delete
         {
            Rig a, cons;
            if (!a.Init(c.create()) || !cons.Init(NoteFilterNode::Create()))
               continue;
            if (c.setup)
               c.setup(a.node.get());
            a.node->CookIfNeeded(a.frame++);
            cons.audio->SetNoteInbox(0, a.out, a.out->RegisterConsumer());
            StartTransport(120.0);
            const double bypassAt = 0.55; // 9001, 9003, 9004 are held
            bool cut = false;
            int voicesOnAtCut = 0;
            while (a.pos < (long long)(4.0 * kRate))
            {
               const double t = (double)a.pos / kRate;
               ScriptedStream(a, t);
               if (!cut && t >= bypassAt)
               {
                  cut = true;
                  std::map<int, bool> open;
                  for (const Evt& e : cons.got)
                     if (!e.e.bendUpdate)
                        e.e.isNoteOn ? (void)(open[e.e.voiceId] = true) : (void)open.erase(e.e.voiceId);
                  voicesOnAtCut = (int)open.size();
                  if (mode == 0)
                     cons.audio->SetNoteInbox(0, &a.in[0], a.in[0].RegisterConsumer());
                  else
                     cons.audio->SetNoteInbox(0, nullptr, -1);
               }
               if (!cut)
                  a.Step(true);
               else
               {
                  Transport::Instance().AdvanceAudioClock(a.block);
                  a.pos += a.block;
               }
               cons.Step(false);
               cons.pos = a.pos;
            }
            StopTransport();
            const PairReport p = Pairs(cons.got);
            printf("NOTEAUDIT BYPASS %-17s %-6s %d voices open at the cut | at the consumer: on %3d off %3d stuck %d orphan-off %d\n",
                   c.name, mode == 0 ? "bypass" : "delete", voicesOnAtCut, p.ons, p.offs, p.stuck, p.orphanOff);
            // Delete is reported only: a deleted producer cannot send note-offs, so every node leaves its
            // consumer's open voices open (the consumer drops to its free-running state, nothing sounds).
            if (mode == 0 && (p.stuck || p.orphanOff))
               flags.Flag("BYPASS", c.name, "bypass mid-note: stuck + orphan-off", p.stuck + p.orphanOff);
         }
      }
      (void)ok;
   }

   // ---- CPU --------------------------------------------------------------
   void AuditCpu()
   {
      for (const Case& c : Cases())
      {
         Rig rig;
         if (!rig.Init(c.create()))
            continue;
         if (c.setup)
            c.setup(rig.node.get());
         rig.node->CookIfNeeded(rig.frame++);
         StartTransport(120.0);
         for (int i = 0; i < 4; i++)
            Push(rig.in[0], 60 + i * 4, 0.8f, true, 9100 + i);
         rig.Run(0.5);
         const auto t0 = std::chrono::steady_clock::now();
         const int blocks = 400;
         for (int i = 0; i < blocks; i++)
            rig.Step();
         const auto t1 = std::chrono::steady_clock::now();
         StopTransport();
         const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / blocks;
         printf("NOTEAUDIT CPU   %-18s %8.0f ns per 512-frame block\n", c.name, ns);
      }
   }
} // namespace

bool RunNoteAudit()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   const char* only = getenv("INFINITE_NOTEAUDIT_ONLY");
   auto want = [&](const char* sec) { return only == nullptr || *only == 0 || strstr(only, sec) != nullptr; };
   Flags flags;
   bool ok = true;

   if (want("PAIR"))
      for (const Case& c : Cases())
         AuditPair(c, flags, ok);
   if (want("GRID"))
      AuditGrid(flags);
   if (want("SWING"))
      AuditSwing(flags);
   if (want("HUMAN"))
      AuditHuman(flags);
   if (want("LOOP"))
      AuditLoop(flags, ok);
   if (want("JITTER"))
      AuditJitter(flags);
   if (want("BYPASS"))
      AuditBypass(flags, ok);
   if (want("LOAD"))
      AuditLoadClamp(ok);
   if (want("CPU"))
      AuditCpu();

   printf("NOTEAUDIT %s (%d flags)\n", ok ? "PASS" : "FAIL", flags.n);
   return ok;
}

// INFINITE_RESTARTTEST: quantized restart on Note Sequencer, Arpeggiator and
// MIDI File. A press waits for the Transport launch grid (bar, beat) and the
// pattern plays its first step on that line; with no grid it lands on the
// next step; Play after a stop starts on the transport grid again.
bool RunRestartTest()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   bool ok = true;
   auto check = [&](bool cond, const char* what) {
      printf("RESTARTTEST %s %s\n", what, cond ? "OK" : "FAIL");
      ok = ok && cond;
   };
   const double bpm = 120.0;
   const double samplesPerBeat = kRate * 60.0 / bpm;
   auto runTo = [&](Rig& rig, double beat, long long base = 0) {
      while ((double)(rig.pos - base) < beat * samplesPerBeat)
         rig.Step();
   };
   // The note-on nearest `beat` (within 0.02 beats), -1 if none.
   // `base`: the sample the transport last started from 0 at.
   auto noteAt = [&](const Rig& rig, double beat, long long base = 0) {
      for (const Evt& e : rig.got)
         if (e.e.isNoteOn && e.sample >= base && std::fabs((double)(e.sample - base) / samplesPerBeat - beat) < 0.02)
            return e.e.note;
      return -1;
   };
   // Stop, then a few stopped blocks so every pending note-off flushes.
   auto stopAndFlush = [&](Rig& rig) {
      StopTransport();
      for (int b = 0; b < 8; b++)
         rig.Step();
   };
   auto pairsOk = [&](const Rig& rig) {
      const PairReport p = Pairs(rig.got);
      if (p.Bad())
         printf("RESTARTTEST pairs: on %d off %d stuck %d orphan-off %d dup-on %d off-before-on %d\n", p.ons, p.offs,
                p.stuck, p.orphanOff, p.dupOn, p.offBeforeOn);
      return !p.Bad();
   };
   Transport& tr = Transport::Instance();
   const double bar = MusicTime::LaunchQuantizeBeats(MusicTime::kLaunchBar);

   // ---- Note Sequencer: 5 steps of 1/16 (notes 60..64) ----
   {
      auto* seq = static_cast<NoteSequencerNode*>(NoteSequencerNode::Create());
      seq->steps = 5;
      seq->rateMode = 0;
      seq->rateBeats = 0.25f;
      for (int i = 0; i < 5; i++)
      {
         seq->stepNote[i] = 60 + i;
         seq->stepEnabled[i] = true;
      }
      Rig rig;
      check(rig.Init(seq), "sequencer rig");
      StartTransport(bpm);
      tr.SetLaunchQuantize(MusicTime::kLaunchBar);
      runTo(rig, 1.1);
      seq->RequestRestart();
      rig.Step();
      check(seq->RestartArmed(), "sequencer: a press waits for the bar");
      runTo(rig, bar + 0.6);
      const long long lastStep = (long long)std::lround(bar / 0.25) - 1;
      check(noteAt(rig, bar - 0.25) == 60 + (int)(lastStep % 5), "sequencer: the pattern runs on until the bar");
      check(noteAt(rig, bar) == 60 && noteAt(rig, bar + 0.25) == 61 && noteAt(rig, bar + 0.5) == 62,
            "sequencer: step 1 lands on the bar line");
      check(!seq->RestartArmed(), "sequencer: no longer armed after it lands");
      // No grid: the next step plays step 1.
      tr.SetLaunchQuantize(MusicTime::kLaunchNone);
      runTo(rig, bar + 1.35);
      seq->RequestRestart();
      runTo(rig, bar + 2.0);
      check(noteAt(rig, bar + 1.5) == 60 && noteAt(rig, bar + 1.75) == 61, "sequencer: no grid lands on the next step");
      // Stop and play from 0: the restart's offset is gone.
      stopAndFlush(rig);
      const long long base = rig.pos;
      StartTransport(bpm);
      runTo(rig, 0.6, base);
      check(noteAt(rig, 0.0, base) == 60 && noteAt(rig, 0.25, base) == 61, "sequencer: play starts on the transport grid again");
      stopAndFlush(rig);
      check(pairsOk(rig), "sequencer: every note-on has its off");
   }

   // ---- Arpeggiator: hold C E G, Up, 1/16 ----
   {
      auto* arp = static_cast<ArpeggiatorNode*>(ArpeggiatorNode::Create());
      arp->mode = ArpeggiatorNode::kUp;
      arp->octaves = 1;
      arp->rateMode = 0;
      arp->rateBeats = 0.25f;
      Rig rig;
      check(rig.Init(arp), "arp rig");
      StartTransport(bpm);
      tr.SetLaunchQuantize(MusicTime::kLaunchBar);
      Push(rig.in[0], 60, 0.8f, true, 901);
      Push(rig.in[0], 64, 0.8f, true, 902);
      Push(rig.in[0], 67, 0.8f, true, 903);
      runTo(rig, 1.1);
      arp->RequestRestart();
      runTo(rig, bar + 0.6);
      const int seqNotes[3] = { 60, 64, 67 };
      const long long ticks = (long long)std::lround(bar / 0.25);
      check(noteAt(rig, bar - 0.25) == seqNotes[(ticks - 1) % 3], "arp: the pattern runs on until the bar");
      check(noteAt(rig, bar) == 60 && noteAt(rig, bar + 0.25) == 64 && noteAt(rig, bar + 0.5) == 67,
            "arp: the first note lands on the bar line");
      Push(rig.in[0], 60, 0.0f, false, 901);
      Push(rig.in[0], 64, 0.0f, false, 902);
      Push(rig.in[0], 67, 0.0f, false, 903);
      runTo(rig, bar + 1.0);
      stopAndFlush(rig);
      check(pairsOk(rig), "arp: every note-on has its off");
   }

   // ---- MIDI File: C D E F on beats 0..3, 0.9 beat each, a 4-beat loop ----
   {
      const uint8_t trk[] = {
         0x00, 0x90, 60, 100, 0x83, 0x30, 0x80, 60, 0,
         0x30, 0x90, 62, 100, 0x83, 0x30, 0x80, 62, 0,
         0x30, 0x90, 64, 100, 0x83, 0x30, 0x80, 64, 0,
         0x30, 0x90, 65, 100, 0x83, 0x30, 0x80, 65, 0,
         0x00, 0xFF, 0x2F, 0x00 };
      std::vector<uint8_t> bytes = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k',
                                     0, 0, 0, (uint8_t)sizeof(trk) };
      bytes.insert(bytes.end(), trk, trk + sizeof(trk));
      // The system temp folder: "/tmp" does not exist on Windows, so the file was never written there.
      const std::filesystem::path file = std::filesystem::temp_directory_path() / "infinite_restarttest.mid";
      const std::string path = file.u8string();
      {
         std::ofstream f(file, std::ios::binary);
         f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
      }
      auto* mf = static_cast<MidiFileNode*>(MidiFileNode::Create());
      mf->path = path;
      mf->loop = true;
      Rig rig;
      check(rig.Init(mf), "midi file rig");
      check(mf->GetSong() != nullptr, "midi file loads");
      StartTransport(bpm);
      tr.SetLaunchQuantize(MusicTime::kLaunchBeat);
      runTo(rig, 2.3); // the E is sounding
      mf->RequestRestart();
      runTo(rig, 3.6);
      check(noteAt(rig, 2.0) == 64, "midi file: the file runs on until the beat");
      check(noteAt(rig, 3.0) == 60 && noteAt(rig, 3.0) != 65, "midi file: the file starts over on the beat line");
      runTo(rig, 4.2);
      check(noteAt(rig, 4.0) == 62, "midi file: and plays on from there");
      stopAndFlush(rig);
      check(pairsOk(rig), "midi file: every note-on has its off");
      std::remove(path.c_str());
   }

   tr.SetLaunchQuantize(MusicTime::kLaunchNone);
   printf("RESTARTTEST %s\n", ok ? "ALL OK" : "FAILED");
   return ok;
}

// INFINITE_CHORDPROGTEST: Chorder progression mode against its spec (plan E6):
// each cell's chord on its step, held cells, seek into a hold, chord types,
// random mode untouched, note pairing through edits, save/load and presets.
bool RunChordProgTest()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   bool ok = true;
   auto check = [&](bool cond, const char* what) {
      printf("CHORDPROGTEST %s %s\n", what, cond ? "OK" : "FAIL");
      ok = ok && cond;
   };
   const double bpm = 120.0;
   const double samplesPerBeat = kRate * 60.0 / bpm;
   Transport& tr = Transport::Instance();
   const int savedKey = tr.Key(), savedScale = tr.Scale();
   tr.SetKey(0);
   tr.SetScale(MusicTime::kMajor);

   auto makeChorder = []() {
      auto* c = static_cast<ChorderNode*>(ChorderNode::Create());
      c->rateBeats = 1.0f;
      c->strumMs = 0.0f;
      c->humanizeTimingMs = 0.0f;
      c->humanizeVelocity = 0.0f;
      c->upperHarmonics = 0.0f;
      c->useGlobalScale = true;
      c->chordSize = 3;
      c->mode = ChorderNode::kProgression;
      c->progSteps = 4;
      const int deg[4] = { 1, 4, -1, 0 };
      const int type[4] = { NoteTheory::kChordSeventh, NoteTheory::kChordAuto, NoteTheory::kChordAuto, NoteTheory::kChordSus4 };
      for (int i = 0; i < 4; i++)
      {
         c->progDegree[i] = deg[i];
         c->progType[i] = type[i];
      }
      return c;
   };
   auto runTo = [&](Rig& rig, double beat) {
      while ((double)rig.pos < beat * samplesPerBeat)
         rig.Step();
   };
   // Pitch-class bitmask of the note-ons within 0.02 beats of `beat` (0 = none).
   auto chordAt = [&](const Rig& rig, double beat, long long base = 0) {
      int mask = 0;
      for (const Evt& e : rig.got)
         if (e.e.isNoteOn && std::fabs((double)(e.sample - base) / samplesPerBeat - beat) < 0.02)
            mask |= 1 << (e.e.note % 12);
      return mask;
   };
   auto pcs = [](std::initializer_list<int> l) {
      int m = 0;
      for (int p : l)
         m |= 1 << p;
      return m;
   };
   // Stop, then long enough stopped for a held chord's offs (up to 4 beats here) to flush.
   auto stopAndFlush = [&](Rig& rig) {
      StopTransport();
      for (int b = 0; b < 240; b++)
         rig.Step();
   };

   // 1-3: ii7, V held through the next cell, Isus4, then around again.
   {
      ChorderNode* c = makeChorder();
      Rig rig;
      check(rig.Init(c), "rig");
      StartTransport(bpm);
      runTo(rig, 4.5);
      check(chordAt(rig, 0.0) == pcs({ 2, 5, 9, 0 }), "cell 1: ii7 (D F A C)");
      check(chordAt(rig, 1.0) == pcs({ 7, 11, 2 }), "cell 2: V (G B D)");
      check(chordAt(rig, 2.0) == 0, "cell 3: a hold plays nothing new");
      check(chordAt(rig, 3.0) == pcs({ 0, 5, 7 }), "cell 4: Isus4 (C F G)");
      check(chordAt(rig, 4.0) == pcs({ 2, 5, 9, 0 }), "wraps to cell 1");
      // The V's offs land after the held cell has begun (held through it), before cell 4.
      bool heldOk = false;
      for (const Evt& e : rig.got)
         if (!e.e.isNoteOn && e.e.note % 12 == 11)
         {
            const double b = (double)e.sample / samplesPerBeat;
            heldOk = b > 2.5 && b < 3.0;
         }
      check(heldOk, "V is held through the hold cell");
      check(c->CurrentProgCell() == 0, "playing cell readout");

      // Seek into the hold cell: the chord it holds plays for what is left.
      tr.SeekBeats(6.0);
      const long long seekPos = rig.pos;
      for (int b = 0; b < 30; b++)
         rig.Step();
      int maskAfterSeek = 0;
      for (const Evt& e : rig.got)
         if (e.e.isNoteOn && e.sample >= seekPos && e.sample < seekPos + 2 * rig.block)
            maskAfterSeek |= 1 << (e.e.note % 12);
      if (maskAfterSeek != pcs({ 7, 11, 2 }))
         for (const Evt& e : rig.got)
            if (e.e.isNoteOn && e.sample >= seekPos)
               printf("CHORDPROGTEST after seek: note %d at +%lld (beat %.3f)\n", e.e.note, e.sample - seekPos, tr.Beats());
      check(maskAfterSeek == pcs({ 7, 11, 2 }), "seek into a hold plays the held chord");

      // Edits mid-chord (cells, steps, mode, key) never strand a note.
      c->progDegree[0] = 5;
      c->progSteps = 3;
      for (int b = 0; b < 40; b++)
         rig.Step();
      c->mode = ChorderNode::kRandom;
      tr.SetKey(2);
      for (int b = 0; b < 40; b++)
         rig.Step();
      tr.SetKey(0);
      stopAndFlush(rig);
      const PairReport p = Pairs(rig.got);
      if (p.Bad())
         printf("CHORDPROGTEST pairs: on %d off %d stuck %d orphan-off %d dup-on %d off-before-on %d\n", p.ons, p.offs,
                p.stuck, p.orphanOff, p.dupOn, p.offBeforeOn);
      check(!p.Bad(), "every note-on has its off through edits");
   }

   // Own key when Global Scale is off: ii in D major is E minor.
   {
      ChorderNode* c = makeChorder();
      c->useGlobalScale = false;
      c->root = 2;
      c->scale = MusicTime::kMajor;
      c->progType[0] = NoteTheory::kChordAuto;
      Rig rig;
      rig.Init(c);
      StartTransport(bpm);
      runTo(rig, 0.5);
      check(chordAt(rig, 0.0) == pcs({ 4, 7, 11 }), "node key: ii in D is E G B");
      stopAndFlush(rig);
   }

   // Random mode is unchanged: one 3-note diatonic chord per step.
   {
      ChorderNode* c = makeChorder();
      c->mode = ChorderNode::kRandom;
      Rig rig;
      rig.Init(c);
      StartTransport(bpm);
      runTo(rig, 4.5);
      bool randOk = c->CurrentProgCell() == -1;
      const int cMajor = pcs({ 0, 2, 4, 5, 7, 9, 11 });
      for (int b = 0; b < 4; b++)
      {
         const int m = chordAt(rig, (double)b);
         int count = 0;
         for (int k = 0; k < 12; k++)
            count += (m >> k) & 1;
         randOk = randOk && count == 3 && (m & ~cMajor) == 0;
      }
      check(randOk, "random mode: a 3-note C-major chord every step");
      stopAndFlush(rig);
   }

   // 5: save/load, old patches, presets, numerals.
   {
      std::unique_ptr<ChorderNode> src(makeChorder());
      src->progSteps = 6;
      src->progDegree[5] = 3;
      src->progType[5] = NoteTheory::kChordSus2;
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(src.get(), params);
      std::unique_ptr<ChorderNode> dst(static_cast<ChorderNode*>(ChorderNode::Create()));
      Patch::LoadParams(dst.get(), params);
      bool rt = dst->mode == ChorderNode::kProgression && dst->progSteps == 6 && dst->progDegree[2] == -1 &&
                dst->progDegree[5] == 3 && dst->progType[5] == NoteTheory::kChordSus2 && dst->progType[0] == NoteTheory::kChordSeventh;
      check(rt, "cells, types, steps and mode round trip");

      std::vector<std::pair<std::string, std::string>> old;
      for (const auto& kv : params)
         if (kv.first.find("mode") == std::string::npos && kv.first.find("prog") == std::string::npos)
            old.push_back(kv);
      std::unique_ptr<ChorderNode> legacy(static_cast<ChorderNode*>(ChorderNode::Create()));
      Patch::LoadParams(legacy.get(), old);
      check(legacy->mode == ChorderNode::kRandom, "a patch from before loads as random");

      int count = 0;
      const NoteTheory::Progression* list = NoteTheory::Progressions(count);
      std::unique_ptr<ChorderNode> pre(static_cast<ChorderNode*>(ChorderNode::Create()));
      pre->ApplyProgression(0);
      bool presetOk = count > 0 && pre->mode == ChorderNode::kProgression && pre->progSteps == list[0].steps &&
                      pre->progDegree[0] == list[0].degrees[0] && pre->MatchingProgression() == 0 &&
                      std::fabs(pre->rateBeats - (float)MusicTime::BeatsFor(MusicTime::k1Bar)) < 1e-4f;
      pre->progDegree[0] = 6;
      presetOk = presetOk && pre->MatchingProgression() == -1;
      for (int i = 0; i < count; i++)
         presetOk = presetOk && list[i].steps >= 1 && list[i].steps <= ChorderNode::kProgCells && list[i].degrees[0] >= 0;
      check(presetOk, "presets apply, match, and start on a chord");

      char a[16], b[16], d[16];
      NoteTheory::RomanNumeral(6, MusicTime::kMajor, a, sizeof(a));
      NoteTheory::RomanNumeral(0, MusicTime::kNaturalMinor, b, sizeof(b));
      NoteTheory::RomanNumeral(4, MusicTime::kMajor, d, sizeof(d));
      check(std::string(a) == "vii\xC2\xB0" && std::string(b) == "i" && std::string(d) == "V", "roman numerals");
   }

   tr.SetKey(savedKey);
   tr.SetScale(savedScale);
   printf("CHORDPROGTEST %s\n", ok ? "ALL OK" : "FAILED");
   return ok;
}

} // namespace app
