// Per-frame modulation + palette application (runtime code that sat among the fixtures in main.cpp).
#include "app/AppShared.h"

namespace app
{
// --- Who actually authored a modulator write (MovementStats::ClassifyPool) ------------------
// Every performance surface in Infinite - the macro knob, XY pad, number box, trigger button,
// and both MIDI nodes - is an IModulator, so the modulation apply loop used to log all six as
// Source::Modulator: a hand on a hardware controller was recorded as automation at a tenth of
// the weight of the same hand on a mouse. They are hand-operated controls and log as Perf.
//
// The exception is a surface that is itself being driven - an LFO wired into a macro knob. That
// is a machine with a hand-shaped control in front of it, and it can run all night, so it must
// not enter a human pool. gMachineDrivenModNodes records the modulator nodes a machine source
// wrote during the previous frame; one frame of latency is nothing against a 10 Hz grid.
static std::unordered_set<int> gMachineDrivenModNodes;
static std::unordered_set<int> gMachineDrivenModNodesPrev;

static MovementLog::Source ModulatorLogSource(INode* n, int modNodeIndex)
{
   const bool isSurface = dynamic_cast<MidiCCNode*>(n) != nullptr ||
                          dynamic_cast<MidiTriggerNode*>(n) != nullptr ||
                          dynamic_cast<MacroKnobNode*>(n) != nullptr ||
                          dynamic_cast<MacroXYNode*>(n) != nullptr ||
                          dynamic_cast<MacroTriggerNode*>(n) != nullptr ||
                          dynamic_cast<MacroNumBoxNode*>(n) != nullptr;
   if (!isSurface)
      return MovementLog::Source::Modulator;
   return gMachineDrivenModNodesPrev.count(modNodeIndex) > 0 ? MovementLog::Source::Modulator
                                                             : MovementLog::Source::Perf;
}

// ---- per-clip modulation bypass (Arrange::Clip::bypassedModParams) -------
//
// A clip can ask for some of its source node's modulations not to be
// applied while that clip is the one playing, so the same oscillator can
// sound modulated under one clip and static under another. Rebuilt once
// per frame, immediately before the apply loop reads it, because the
// answer changes with the playhead.
//
// Keyed by node index rather than uid so the apply loop's test is a plain
// lookup on the ParamRef it already has.
//
// Only the clip under the playhead counts. Two clips on different lanes
// sharing one source and disagreeing about a param cannot both be honoured
// - there is one param float - so the first lane wins here; giving such a
// clip its own private shadow instance is what actually resolves that, and
// is deliberately a separate step.
std::unordered_map<int, const std::vector<int>*> gArrangeActiveClipModBypass;

void ArrangeRefreshActiveClipModBypass()
{
   gArrangeActiveClipModBypass.clear();
   // Canvas mode: no clip is playing anything, so no clip gets a say over
   // the canvas's own modulation.
   if (!ArrangeTimelineRoutingActive())
      return;

   const double beat = Transport::Instance().Beats();
   for (const Arrange::Lane& lane : gArrange.lanes)
   {
      if (!Arrange::LaneEffectivelyEnabled(gArrange, lane))
         continue;
      for (const Arrange::Clip& c : lane.clips)
      {
         // Clips are sorted by start and never overlap within a lane, so
         // the first one that starts after `beat` ends the search.
         if (Arrange::TicksToBeats(c.start) > beat)
            break;
         if (!(beat < Arrange::TicksToBeats(c.End())))
            continue;
         if (c.enabled && c.srcUid != 0 && !c.bypassedModParams.empty())
         {
            const GraphNode* gn = FindNodeByUid(c.srcUid);
            if (gn != nullptr)
               gArrangeActiveClipModBypass.emplace(gn->index, &c.bypassedModParams);
         }
         break; // this lane's only candidate, usable or not
      }
   }
}

bool ArrangeClipBypassesMod(int nodeIndex, int paramIndex)
{
   if (gArrangeActiveClipModBypass.empty())
      return false;
   auto it = gArrangeActiveClipModBypass.find(nodeIndex);
   if (it == gArrangeActiveClipModBypass.end() || it->second == nullptr)
      return false;
   return std::binary_search(it->second->begin(), it->second->end(), paramIndex);
}

// What a bypassed param sits at. Modulation::Source::centre is the
// destination's value at the instant the binding was made - the knob
// position the modulator took over from - which is precisely "what this
// would read with the modulator unplugged". Clamped to the param's own
// declared range because a binding restored from a patch line that predates
// the centre token decodes it as 0, which is out of range for plenty of
// params (a cutoff in Hz, say) and must not be written raw.
//
// Writing this every frame, rather than skipping the write, is the point:
// leaving the param alone would freeze it at whatever the modulator last
// pushed, so a bypassed LFO would strand the knob mid-sweep instead of
// releasing it.
float ArrangeClipBypassBaseValue(const ParamRef& ref, const Modulation::Source& src)
{
   return ShapeToParam(ref, std::clamp(src.centre, ref.minValue, ref.maxValue));
}

// Which nodes have a param that something other than the hand writes every
// frame - see GraphNode::IsParamDriven. One flag per writer in
// ApplyModulationAndPalette just below; keep the two in step, since off-screen
// culling and the collapsed register-only pass skip any node this misses and
// its driven params then freeze (collapsed) or step every kCullRefresh frames
// (off screen). Runs before the node editor draws.
void RefreshParamDriverFlags()
{
   Modulation& modulation = Modulation::Instance();
   for (GraphNode& gn : gNodes)
   {
      gn.hasModulatedParams = false;
      gn.hasBipolarParams = false;
      gn.hasPaletteColors = false;
      gn.hasExpressionParams = false;
      gn.hasPerfPanelParams = false;
      gn.hasGestureParams = false;
   }
   // Modulators, macros, triggers, predictors, number boxes: all Links().
   for (const auto& link : modulation.Links())
   {
      if (GraphNode* target = FindNodeByIndex(link.first.first))
      {
         target->hasModulatedParams = true;
         if (link.second.polarity == Modulation::Source::kBipolar)
            target->hasBipolarParams = true;
      }
   }
   for (const auto& link : PaletteBinding::Instance().Links())
   {
      if (GraphNode* target = FindNodeByIndex(link.first.first))
         target->hasPaletteColors = true;
   }
   for (const auto& expr : modulation.Expressions())
   {
      if (GraphNode* target = FindNodeByIndex(expr.first.first))
         target->hasExpressionParams = true;
   }
   // gPerfPendingWrites is filled for an element's own destination and for
   // every extra target, and cleared each frame whether or not it landed.
   for (const auto& elem : gPerfElements)
   {
      if (GraphNode* target = FindNodeByIndex(elem.dstIndex))
         target->hasPerfPanelParams = true;
      for (const auto& t : elem.targets)
         if (GraphNode* target = FindNodeByIndex(t.dstIndex))
            target->hasPerfPanelParams = true;
   }
   for (const auto& playback : GestureRecorder::Instance().Playbacks())
   {
      if (GraphNode* target = FindNodeByIndex(playback.first.first))
         target->hasGestureParams = true;
   }
}

void ApplyModulationAndPalette(int frameId, bool isNormalFrame)
{
   UpdatePerformanceMatrixMIDI();
   // Before any binding is read: which clip is under the playhead decides
   // which modulations are bypassed this frame.
   ArrangeRefreshActiveClipModBypass();

   Modulation& modulation = Modulation::Instance();
   // Apply deferred writes from the performance matrix before snapshot and modulators
   for (const ParamRef& ref : modulation.FrameParams())
   {
      if (ref.value == nullptr) continue;
      auto it = gPerfPendingWrites.find({ref.nodeIndex, ref.paramIndex});
      if (it != gPerfPendingWrites.end())
      {
         *ref.value = ShapeToParam(ref, it->second);
         MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex, MovementLog::Source::Perf);
      }
   }
   gPerfPendingWrites.clear();

   // Snapshot every registered parameter's current value, keyed by node,
   // before any of this frame's writes land. This is what lets an
   // expression reference a sibling parameter by name ("width * 0.5")
   // without depending on the order params happened to draw in - and it
   // means a cycle between two expressions just settles a frame late
   // rather than reading half-updated state mid-pass.
   std::map<int, std::map<std::string, float>> paramSnapshot;
   for (const ParamRef& ref : modulation.FrameParams())
      if (ref.value != nullptr)
         paramSnapshot[ref.nodeIndex][ref.name] = *ref.value;

   // Fixture only: put one envelope field into the expression state, so
   // the UI fixture also covers the fx badge and the clear button - the
   // layout that spilled out of the panel and over the next column.
   // Parameter indices are positional and only exist once the node has
   // drawn, so this has to run here, where FrameParams is populated,
   // rather than beside the node's spawn.
   if (getenv("INFINITE_AUDIOUITEST") != nullptr && frameId == 2 && gNodes.size() > 1)
   {
      int found = 0, foundDecay = 0;
      for (const ParamRef& ref : modulation.FrameParams())
      {
         if (ref.nodeIndex != gNodes[1].index)
            continue;
         if (ref.name == "release" && ++found == 3) // engine A's filter release
            modulation.SetExpression(ref.nodeIndex, ref.paramIndex, "lerp(lo, hi, 0.5)");
         // ...and, under INFINITE_AUDIOUITEST_TYPING, leave engine A's
         // filter decay open in formula-entry mode - the other layout
         // that spilled, since the typed field used to draw its caption
         // outside the box to the right. Behind its own flag because a
         // fixture that opens with a focused text field swallows the
         // keyboard until you click away.
         if (ref.name == "decay" && ++foundDecay == 3 &&
             getenv("INFINITE_AUDIOUITEST_TYPING") != nullptr)
         {
            const std::pair<int, int> key(ref.nodeIndex, ref.paramIndex);
            gTypedParamText[key] = "=lerp(lo, hi, 0.25)";
            gTypedParam.insert(key);
            gTypedParamJustOpened = key;
         }
      }
   }

   // Advance every predictor exactly once, before any binding reads it (idempotency: a predictor
   // driving N params must not run N times as fast). A bypassed predictor is not ticked, so it
   // freezes rather than advancing unseen and jumping on un-bypass.
   // Live, dt is wall-clock time between calls. Offline (Render Now, headless --render/--frame),
   // it is the step in Transport's video seconds instead: the offline pump calls this many times per
   // wall-clock frame, so a wall dt would be a few random milliseconds per rendered frame. The video
   // step also makes a repeat call at the same T (the normal-frame call during a take, a re-exported
   // --frame) tick by 0 rather than a second time.
   {
      static double sLastTickTime = -1.0;
      static double sLastOfflineT = 0.0;
      static bool sWasOfflineTick = false;
      Transport& transport = Transport::Instance();
      double tickDt = 0.0;
      if (transport.IsOfflineMode())
      {
         const double videoT = transport.Seconds();
         tickDt = sWasOfflineTick ? std::clamp(videoT - sLastOfflineT, 0.0, 0.25) : 0.0;
         sLastOfflineT = videoT;
         sWasOfflineTick = true;
         sLastTickTime = -1.0; // first live tick after the take gets dt 0, not a stale gap
      }
      else
      {
         sWasOfflineTick = false;
         const double nowWall = glfwGetTime();
         tickDt = sLastTickTime < 0.0 ? 0.0 : std::clamp(nowWall - sLastTickTime, 0.0, 0.25);
         sLastTickTime = nowWall;
      }
      for (GraphNode& gn : gNodes)
         if (gn.node != nullptr && !gn.node->bypassed)
            if (auto* pred = dynamic_cast<IPredictor*>(gn.node.get()))
               pred->Tick(frameId, tickDt);
   }

   const double t = Transport::Instance().Seconds();
   // Patch-wide named values, evaluated once before any parameter reads
   // them so every expression in the frame sees the same globals - see
   // core/ExprGlobals.h.
   // Which modulator nodes were themselves written by a machine last frame (see
   // gMachineDrivenModNodes below): a macro knob with an LFO wired into it is not a hand.
   gMachineDrivenModNodesPrev.swap(gMachineDrivenModNodes);
   gMachineDrivenModNodes.clear();
   ExprGlobals::EvaluateAll(t);
   const std::map<std::string, float>& globals = ExprGlobals::Values();
   for (const ParamRef& ref : modulation.FrameParams())
   {
      if (ref.value == nullptr)
         continue;
      const Modulation::Source src = modulation.ResolvedSourceFor(ref);
      if (src.nodeIndex >= 0)
      {
         if (!src.enabled)
            continue;   // binding intact, just not written this frame
         // The clip currently playing this node asked for this particular
         // modulation not to be applied. Sits above every modulator kind
         // below (predictor, macro box, trigger, ordinary) deliberately -
         // one gate, so no modulator type can quietly escape it.
         if (ArrangeClipBypassesMod(ref.nodeIndex, ref.paramIndex))
         {
            *ref.value = ArrangeClipBypassBaseValue(ref, src);
            continue;
         }
         // A wired modulator always wins over a typed expression - see
         // Modulation::SetExpression.
         GraphNode* modNode = FindNodeByIndex(src.nodeIndex);
         if (modNode == nullptr)
            continue;
         if (modNode->node != nullptr && modNode->node->bypassed && modNode->node->BypassSource() == nullptr)
            continue;
         auto* modulator = ModulatorForOutput(modNode->node.get(), src.outputIndex);
         if (modulator == nullptr)
            continue;
         // Value01() is documented to return 0..1, but InvertNode and
         // RangeToRangeNode (clampOutput=false) deliberately violate
         // that on purpose as a signal-shaping tool - see
         // docs/plans/modulators/00-modulation-polarity.md §4. That's
         // fine as long as it stays a signal between modulator nodes:
         // clamping v01 here means it can no longer reach past
         // src.lo/src.hi into a destination param, which is a hard
         // contract (see ShapeToParam).
         // A predictor (green) writes in the destination's fader space, per destination, so a log
         // knob is not crowded at its top end. It never drives a discrete param, and a Shift-grab
         // suspends it for as long as the hand holds the control.
         if (auto* pred = dynamic_cast<IPredictor*>(modNode->node.get()))
         {
            if (ref.isEnum || ref.isBool)
               continue;
            const ParamKey pk{ UidForIndex(ref.nodeIndex), ref.paramIndex };
            if (gPredictorGrabs.count(pk) > 0)
            {
               MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex, MovementLog::Source::Hand);
               continue;
            }
            const float cur = ParamToPos(ref, *ref.value);
            const float p = ApplyModulationCurve(std::clamp(pred->ValuePos01For(pk, cur), 0.0f, 1.0f), src.curve);
            const float posLo = ParamToPos(ref, src.lo), posHi = ParamToPos(ref, src.hi);
            *ref.value = ShapeToParam(ref, PosToParam(ref, posLo + (posHi - posLo) * p));
            gMachineDrivenModNodes.insert(ref.nodeIndex);
            MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex, MovementLog::Source::Prediction);
            continue;
         }
         // A macro number box is the one modulator that speaks in the
         // destination's own units rather than 0..1: the point of
         // typing "440" into it is that the destination becomes 440,
         // with each destination clamping to its own declared range on
         // the way in. Normalising it would have forced the box to
         // carry a min/max of its own, which is exactly the pair of
         // params that made it fiddly.
         if (auto* numBox = dynamic_cast<MacroNumBoxNode*>(modNode->node.get()))
         {
            *ref.value = ShapeToParam(ref, numBox->value);
            MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex,
                                    ModulatorLogSource(modNode->node.get(), src.nodeIndex));
            continue;
         }
         if (auto* trigNode = dynamic_cast<MacroTriggerNode*>(modNode->node.get()))
         {
            // A momentary gate button (Looper transport, MPC pad) acts on a rising edge,
            // so the trigger follows the pad's own level - high while held, and for at
            // least this one apply on a tap - instead of flipping the bool per trigger,
            // where every second trigger would land on the falling edge and do nothing.
            if (ref.momentary)
            {
               const float level = (trigNode->pressed || trigNode->justTriggered) ? ref.maxValue : ref.minValue;
               if (*ref.value != level)
               {
                  *ref.value = level;
                  MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex,
                                          ModulatorLogSource(modNode->node.get(), src.nodeIndex));
               }
               continue;
            }
            const int span = (int)std::lround(ref.maxValue - ref.minValue);
            const bool isStepping = ref.isEnum || ref.isBool || (ref.step == 1.0f && span >= 1);
            if (isStepping)
            {
               if (trigNode->justTriggered)
               {
                  if (ref.isBool || span == 1)
                  {
                     *ref.value = (*ref.value > ref.minValue + 0.5f) ? ref.minValue : ref.maxValue;
                     MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex,
                                             ModulatorLogSource(modNode->node.get(), src.nodeIndex));
                  }
                  else if (span >= 1)
                  {
                     const int curIdx = std::clamp((int)std::lround(*ref.value - ref.minValue), 0, span);
                     *ref.value = ref.minValue + (float)((curIdx + 1) % (span + 1));
                     MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex,
                                             ModulatorLogSource(modNode->node.get(), src.nodeIndex));
                  }
               }
               continue;
            }
         }
         const float rawV01 = std::clamp(modulator->Value01(), 0.0f, 1.0f);
         const float v01 = ApplyModulationCurve(rawV01, src.curve);
         *ref.value = ShapeToParam(ref, src.lo + (src.hi - src.lo) * v01);
         const MovementLog::Source modSrc = ModulatorLogSource(modNode->node.get(), src.nodeIndex);
         if (modSrc == MovementLog::Source::Modulator)
            gMachineDrivenModNodes.insert(ref.nodeIndex);
         MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex, modSrc);
         continue;
      }
      const std::string* expr = modulation.ExpressionFor(ref.nodeIndex, ref.paramIndex);
      if (expr == nullptr)
         continue;
      float result = 0.0f;
      std::string error;
      // `lo`/`hi` bind this param's own range, so an expression can be
      // written in normalised terms the way a wired modulator already
      // works: `=lerp(lo, hi, sin(t) * 0.5 + 0.5)` sweeps the full range,
      // where the bare `=sin(t)` people reach for first lands in raw
      // units and clamps to nothing on a param measured in milliseconds.
      // Both spellings stay available - raw units are what you want for
      // `=250` or `=width * 0.5`.
      std::map<std::string, float>& siblings = paramSnapshot[ref.nodeIndex];
      const auto savedLo = siblings.find("lo");
      const auto savedHi = siblings.find("hi");
      const bool hadLo = savedLo != siblings.end(), hadHi = savedHi != siblings.end();
      const float prevLo = hadLo ? savedLo->second : 0.0f;
      const float prevHi = hadHi ? savedHi->second : 0.0f;
      // "Range" from the param's right-click menu (see modulation.
      // ExpressionRangeFor) overrides what lo/hi resolve to here; falls back
      // to the param's own declared span when no override is set.
      float boundLo = ref.minValue, boundHi = ref.maxValue;
      modulation.ExpressionRangeFor(ref.nodeIndex, ref.paramIndex, boundLo, boundHi);
      siblings["lo"] = boundLo;
      siblings["hi"] = boundHi;
      // Whether the formula text itself names the lo/hi bind variables (as a
      // whole identifier, not e.g. the "lo" inside "log") - if it does, the
      // author is already hand-placing the result inside Range via those
      // variables (`lerp(lo, hi, ...)`), so the blanket remap below must not
      // also run or it would double-apply Range on top of an already-ranged
      // result.
      const auto namesIdentifier = [](const std::string& s, const char* word) {
         const size_t len = strlen(word);
         size_t pos = 0;
         while ((pos = s.find(word, pos)) != std::string::npos)
         {
            const bool leftOk = pos == 0 || !(isalnum((unsigned char)s[pos - 1]) || s[pos - 1] == '_');
            const size_t after = pos + len;
            const bool rightOk = after >= s.size() || !(isalnum((unsigned char)s[after]) || s[after] == '_');
            if (leftOk && rightOk)
               return true;
            pos += len;
         }
         return false;
      };
      const bool formulaOwnsRange = namesIdentifier(*expr, "lo") || namesIdentifier(*expr, "hi");
      const bool evaluated = Expression::Evaluate(*expr, t, &siblings, &globals, result, error);
      if (hadLo) siblings["lo"] = prevLo; else siblings.erase("lo");
      if (hadHi) siblings["hi"] = prevHi; else siblings.erase("hi");
      if (evaluated)
      {
         // A Range override also remaps the formula's own raw output, not just
         // the lo/hi bind variables above - otherwise "Range" would silently do
         // nothing for the common case of a formula that never references
         // lo/hi (e.g. `=sin(t)*0.5+0.5`). This is an identity when no override
         // is set, since boundLo/boundHi then equal ref.minValue/maxValue, and
         // it's skipped entirely when the formula already used lo/hi itself
         // (see formulaOwnsRange above).
         float mapped = result;
         const float exprCurve = modulation.ExpressionCurveFor(ref.nodeIndex, ref.paramIndex);
         if (!formulaOwnsRange && ref.maxValue > ref.minValue)
         {
            float norm = std::clamp((result - ref.minValue) / (ref.maxValue - ref.minValue), 0.0f, 1.0f);
            norm = ApplyModulationCurve(norm, exprCurve);
            mapped = boundLo + norm * (boundHi - boundLo);
         }
         else if (std::abs(exprCurve) > 0.0001f && boundHi != boundLo)
         {
            float norm = std::clamp((result - boundLo) / (boundHi - boundLo), 0.0f, 1.0f);
            norm = ApplyModulationCurve(norm, exprCurve);
            mapped = boundLo + norm * (boundHi - boundLo);
         }
         *ref.value = ShapeToParam(ref, mapped);
         gMachineDrivenModNodes.insert(ref.nodeIndex);
         MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex, MovementLog::Source::Expression);
         modulation.SetExpressionError(ref.nodeIndex, ref.paramIndex, std::string());
      }
      else
      {
         // Leave the last good value in place rather than snapping to 0 -
         // a typo mid-edit should not blank out the render.
         modulation.SetExpressionError(ref.nodeIndex, ref.paramIndex, error);
      }
   }

   // Shift-drag recordings (see GestureRecorder) loop back into their param
   // once their session ends - same precedence as above: a wired modulator
   // or a typed expression already owns the field, so a recording only
   // plays back once neither is in the way.
   GestureSyncClockAxis();
   const double gestureNow = GesturePlaybackClock();
   for (const ParamRef& ref : modulation.FrameParams())
   {
      if (ref.value == nullptr)
         continue;
      if (modulation.IsModulated(ref.nodeIndex, ref.paramIndex) ||
          modulation.HasExpression(ref.nodeIndex, ref.paramIndex))
         continue;
      float playbackValue = 0.0f;
      // Live: GestureRecorder's own clock, not `t` above - samples were
      // timestamped with GestureRecorder::ClockNow() when recorded (see
      // ModSlider/ModKnob/VFaderFloat/BipolarKnobFloat), and that clock only
      // advances while Transport plays, so pausing freezes a looping
      // recording in place. Offline: Transport's video seconds, one step per
      // rendered frame - see GesturePlaybackClock.
      if (GestureRecorder::Instance().GetPlaybackValue(ref.nodeIndex, ref.paramIndex, gestureNow, playbackValue))
      {
         *ref.value = ShapeToParam(ref, playbackValue);
         MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex, MovementLog::Source::Gesture);
      }
   }

   for (GraphNode& gn : gNodes)
   {
      if (auto* trigNode = dynamic_cast<MacroTriggerNode*>(gn.node.get()))
         trigNode->justTriggered = false;
   }

   // A Palette is not an Output, so nothing downstream pulls it, and both its
   // preview and its bindings need this frame's swatches. Cooking here - after
   // modulation has been applied - is what lets a modulator drive the shaping
   // controls and have it land the same frame.
   for (GraphNode& gn : gNodes)
   {
      if (dynamic_cast<IPaletteSource*>(gn.node.get()) != nullptr && !gn.node->bypassed)
         gn.node->CookIfNeeded(frameId);
   }

   // Same reasoning as the Palette loop just above: standalone FieldPixel,
   // FieldPrimitive, or FieldElement nodes with nothing wired downstream are
   // never reached by the sink-driven cook loops, so their preview thumbnails
   // or mini-viewports stayed static or un-cooked - CookIfNeeded was simply
   // never being called on them.
   for (GraphNode& gn : gNodes)
   {
      if (!gn.node->bypassed &&
          (dynamic_cast<FieldPixelNode*>(gn.node.get()) != nullptr ||
           dynamic_cast<FieldPrimitiveNode*>(gn.node.get()) != nullptr ||
           dynamic_cast<FieldElementNode*>(gn.node.get()) != nullptr))
      {
         gn.node->CookIfNeeded(frameId);
      }
   }

   // Build step 15 ("Instrument Mode"): a FieldGraphNode's mounted children
   // can be hidden from the canvas (encapsulated==true), so - same reasoning
   // as the FieldPixel loop just above - they are never reached by the
   // sink-driven cook loop unless something outside the FieldGraphNode
   // happens to consume its boundary output. Cook every mounted child
   // unconditionally, every frame, whether hidden or not - encapsulation is
   // a canvas-presentation choice, not a cook-skipping one (doc trap 2).
   //
   // Also syncs each mounted child's GraphNode::hiddenFromCanvas to the
   // owning FieldGraphNode's current `encapsulated` value every frame,
   // rather than only at Mount() time - so toggling `encapsulated` (a plain
   // checkbox, no Regenerate() involved) takes effect on the very next
   // frame's node-editor draw pass without re-mounting anything (doc §10
   // FIELDGRAPHENCAPTEST assertion 4).
   for (GraphNode& gn : gNodes)
   {
      auto* fgn = dynamic_cast<FieldGraphNode*>(gn.node.get());
      if (fgn == nullptr)
         continue;
      for (int idx : fgn->MountedIndices())
      {
         GraphNode* child = FindNodeByIndex(idx);
         if (child == nullptr)
            continue;
         child->hiddenFromCanvas = fgn->encapsulated;
         child->node->CookIfNeeded(frameId);
      }
   }

   // Colours, the same way and for the same reason: the registry was rebuilt
   // while the nodes drew, so every pointer here belongs to a node that
   // still exists. A palette has to cook before it can be read, and it is
   // not an Output so nothing else would pull it.
   PaletteBinding& palette = PaletteBinding::Instance();
   for (const ColorRef& ref : palette.FrameColors())
   {
      const PaletteBinding::Source src = palette.SourceFor(ref.nodeIndex, ref.colorIndex);
      if (src.nodeIndex < 0 || ref.value == nullptr)
         continue;
      GraphNode* palNode = FindNodeByIndex(src.nodeIndex);
      if (palNode == nullptr)
         continue;
      auto* source = dynamic_cast<IPaletteSource*>(palNode->node.get());
      if (source == nullptr)
         continue;
      source->GetSwatch(src.swatchIndex, ref.value);
   }

   // Wall clock, not the transport: the transport freezes while paused, which
   // would stamp every hand move made with playback stopped at one instant.
   MovementLog::Capture(ImGui::GetTime(), isNormalFrame);
}
}
