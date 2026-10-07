// Headless jobs: ExplainLive, ParamKeyJoiner, HeadlessTick (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
// ---- headless jobs: the per-frame driver for --render / --frame ----
// Runs once per main-loop iteration AFTER the normal frame's ImGui pass. The
// hidden window keeps drawing frames on purpose: a node registers its ParamRefs
// (modulation, expressions, gestures) only by being drawn, so skipping the
// draw would render a patch whose modulation never lands.
std::string ExplainLive(bool json, bool all)
{
   static std::map<std::string, std::map<std::string, std::string>> sDefaults;
   PatchExplain::Env xenv;
   xenv.defaultParams = [&](const std::string& type) -> std::map<std::string, std::string>
   {
      auto it = sDefaults.find(type);
      if (it != sDefaults.end())
         return it->second;
      std::map<std::string, std::string> out;
      if (std::unique_ptr<INode> fresh { NodeFactory::Instance().MakeNode(type) })
      {
         std::vector<std::pair<std::string, std::string>> raw;
         Patch::SaveParams(fresh.get(), raw);
         for (const auto& kv : raw)
            out[kv.first] = kv.second;
      }
      return sDefaults[type] = out;
   };
   xenv.slotName = [&](const std::string& type, int slot, const std::string&) -> std::string
   {
      // Slots are one numbering across kinds (Wavetable: 0 notes, 1 fm in), and
      // SlotNames is parallel to inputs, not indexed by slot.
      const PatchSchema::TypeSchema* ts = SchemaFor(type);
      if (ts == nullptr)
         return std::string();
      const std::vector<std::string> names = PatchSchema::SlotNames(*ts);
      for (size_t i = 0; i < ts->inputs.size() && i < names.size(); i++)
         if (ts->inputs[i].slot == slot)
            return names[i];
      return std::string();
   };
   xenv.paramLabel = [&](int node, int param) -> std::string
   {
      const ParamRef* k = Modulation::Instance().KnownParam(node, param);
      return k != nullptr ? k->name : std::string();
   };
   // The join lives per type (the probe nodes are gone after the load), so a
   // running app that never probed answers "" and the label is shown instead.
   xenv.paramKey = [&](int node, int param) -> std::string
   {
      const GraphNode* gn = FindNodeByIndex(node);
      if (gn == nullptr)
         return std::string();
      auto it = gParamJoin.find(gn->typeName);
      if (it == gParamJoin.end())
         return std::string();
      auto k = it->second.keyOfParam.find(param);
      return k == it->second.keyOfParam.end() ? std::string() : k->second;
   };
   xenv.optionName = [&](const std::string& type, const std::string& key, int value) -> std::string
   {
      auto it = gParamJoin.find(type);
      if (it == gParamJoin.end())
         return std::string();
      auto o = it->second.optionsOfKey.find(key);
      if (o == it->second.optionsOfKey.end() || value < 0 || value >= (int)o->second.size())
         return std::string();
      return o->second[value];
   };
   xenv.resolveMod = [&](const Patch::ModRecord& m, float& lo, float& hi) -> bool
   {
      const ParamRef* k = Modulation::Instance().KnownParam(m.dstIndex, m.dstParam);
      if (k == nullptr)
         return false;
      const Modulation::Source src = Modulation::Instance().ResolvedSourceFor(*k);
      lo = src.lo;
      hi = src.hi;
      return true;
   };
   xenv.idOf = [&](int index) -> std::string
   {
      for (const Patch::NodeRecord& n : gHeadlessPatch.nodes)
         if (n.index == index)
            return n.id;
      return std::string();
   };
   xenv.nodeNotes = [&](int index, bool fileBypass) -> std::vector<std::string>
   {
      std::vector<std::string> out;
      GraphNode* gn = FindNodeByIndex(index);
      if (gn == nullptr || gn->node == nullptr)
         return out;
      auto num = [](float v)
      {
         char b[32];
         std::snprintf(b, sizeof(b), "%.4g", (double)v);
         return std::string(b);
      };
      // The file's flag, not the live one: the frame loop clears it where bypass cannot apply.
      bool fileFlag = fileBypass;
      for (const Patch::NodeRecord& n : gHeadlessPatch.nodes)
         if (n.index == index)
            fileFlag = n.bypassed;
      if (fileFlag)
      {
         if (!CanBypass(*gn))
            out.push_back("bypass ignored: " + std::to_string(InputCountFor(*gn)) + " inputs, nothing to pass through");
         else if (INode* src = gn->node->BypassSource())
         {
            std::string what = "its input";
            for (const GraphNode& o : gNodes)
               if (o.node.get() == src)
                  what = o.typeName + " (" + std::to_string(o.index) + ")";
            out.push_back("bypassed: passes " + what + " straight through");
         }
         else
            out.push_back("bypassed: silent (nothing passes through)");
      }
      if (gn->node->bypassed)
         return out; // a bypassed source hands out empty geometry
      if (auto* geo = dynamic_cast<IGeometrySource*>(gn->node.get()))
      {
         const Mesh& mesh = geo->GetMesh();
         if (mesh.HasGeometry())
         {
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            const Mat4 mm = geo->GetModelMatrix();
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { mm.m[0] * v.px + mm.m[4] * v.py + mm.m[8] * v.pz + mm.m[12],
                                    mm.m[1] * v.px + mm.m[5] * v.py + mm.m[9] * v.pz + mm.m[13],
                                    mm.m[2] * v.px + mm.m[6] * v.py + mm.m[10] * v.pz + mm.m[14] };
               for (int k = 0; k < 3; k++)
               {
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
            }
            out.push_back("geometry: " + std::to_string(mesh.vertices.size()) + " vertices, " +
                          std::to_string(mesh.FaceCount()) + " faces, world bbox (" + num(lo[0]) + " " + num(lo[1]) + " " +
                          num(lo[2]) + ") to (" + num(hi[0]) + " " + num(hi[1]) + " " + num(hi[2]) + ")");
            const Material mat = geo->GetMaterial();
            static const char* kShading[] = { "lit", "normals", "uv", "flat" };
            out.push_back(std::string("material: ") + (mat.shading >= 0 && mat.shading < 4 ? kShading[mat.shading] : "?") +
                          ", colour " + num(mat.color[0]) + " " + num(mat.color[1]) + " " + num(mat.color[2]) + ", metallic " +
                          num(mat.metallic) + ", roughness " + num(mat.roughness) + ", opacity " + num(mat.opacity) +
                          (mat.emission > 0.0f ? ", emission " + num(mat.emission) : std::string()));
         }
         else if (geo->IsGeometryOutputIndex(0))
            out.push_back("geometry: empty (no mesh output)");
      }
      return out;
   };
   const PatchExplain::Explanation ex = PatchExplain::Build(BuildPatchData(), xenv);
   return json ? PatchExplain::ToJson(ex) : PatchExplain::ToText(ex, all);
}

static void HeadlessFinish(GLFWwindow* window, Headless::Status& st, double startWall)
{
   st.elapsedMs = (long long)((glfwGetTime() - startWall) * 1000.0);
   st.ok = st.errors.empty();
   gHeadlessExitCode = Headless::Emit(gHeadlessJob, st);
   glfwSetWindowShouldClose(window, GLFW_TRUE);
}

// A probe (validate / describe) is done once every spawned node has been drawn
// with its parameters open, and at least 3 ticks have passed so the first
// frame's layout has settled. Replaces a fixed 8-tick wait that a slow start
// or the off-screen cull could cut short (false "has no modulatable parameters").
static const int kHeadlessProbeCapTicks = 120;
static bool HeadlessProbeDone(const std::vector<std::pair<std::string, int>>& probed, int ticks)
{
   if (ticks < 3)
      return false;
   for (const auto& pr : probed)
      if (!gHeadlessDrawn.count(pr.second))
         return false;
   return true;
}


// ---- ParamRef.key join (docs/fix-briefs/headless-engine.md 3.1b) ------------
// A control registered by the draw pass knows its label, not the key the file
// stores. Two tiers find the key of each control on a drawn probe node:
//   1. by address: the widget hands over the member it edits (ParamRef::srcAddr)
//      and VisitParams hands out the address of every saved member;
//   2. by perturbation: for the rest (dropdowns take an int by value, some
//      knobs edit a local copy), change one saved value, draw a frame, and see
//      which unmatched control's value followed it.
// The result is remembered per type, so key -> parameter index works for any
// later node of that type.


class AddrCollector : public ParamVisitor
{
public:
   std::map<const void*, std::string> keyOfAddr;
   std::vector<std::pair<std::string, char>> keys; // f/i/b keys in file order
   void Float(const char* n, float& v) override { keyOfAddr[&v] = n; keys.push_back({ n, 'f' }); }
   void Int(const char* n, int& v) override { keyOfAddr[&v] = n; keys.push_back({ n, 'i' }); }
   void Bool(const char* n, bool& v) override { keyOfAddr[&v] = n; keys.push_back({ n, 'b' }); }
   void Text(const char*, std::string&) override {}
   void Color(const char*, float*) override {}
};

// Changes the value of one saved key (mode 0, remembering the old one) or puts it back (mode 1).
class KeyPerturber : public ParamVisitor
{
public:
   std::string key;
   int mode = 0;
   double old = 0.0;
   double now = 0.0;
   bool hit = false;
   char kind = 'f';
   int attempt = 0; // 0,1,2: which of the candidate values to try (a widget may clamp one away)
   void Float(const char* n, float& v) override
   {
      if (key != n) return;
      hit = true; kind = 'f';
      if (mode == 0)
      {
         old = v;
         static const float kCand[] = { 0.5f, 0.25f, 0.9f, 0.1f };
         int seen = 0;
         for (float c : kCand)
            if (std::fabs(c - v) > 1e-3f && seen++ == attempt)
            {
               v = c;
               break;
            }
         now = v;
      }
      else v = (float)old;
   }
   void Int(const char* n, int& v) override
   {
      if (key != n) return;
      hit = true; kind = 'i';
      if (mode == 0)
      {
         old = v;
         static const int kCand[] = { 0, 1, 2, 3, -1 };
         int seen = 0;
         for (int c : kCand)
            if (c != v && seen++ == attempt)
            {
               v = c;
               break;
            }
         now = v;
      }
      else v = (int)old;
   }
   void Bool(const char* n, bool& v) override
   {
      if (key != n) return;
      hit = true; kind = 'b';
      if (mode == 0) { old = v ? 1 : 0; v = !v; now = v ? 1 : 0; }
      else v = old != 0.0;
   }
   void Text(const char*, std::string&) override {}
   void Color(const char*, float*) override {}
};

class ParamKeyJoiner
{
public:
   // probed: (type, node index) of drawn probe nodes, one per type.
   void Begin(const std::vector<std::pair<std::string, int>>& probed)
   {
      mNodes.clear();
      for (const auto& pr : probed)
      {
         if (gParamJoin[pr.first].done)
            continue;
         Node n;
         n.type = pr.first;
         n.index = pr.second;
         mNodes.push_back(std::move(n));
      }
   }

   // Live use: match controls to keys by address only. Never perturbs a value, so it is safe on
   // nodes the user is looking at; controls the address match misses stay unkeyed.
   void SetTier1Only(bool on) { mTier1Only = on; }

   // Call once per tick after the frame was drawn. True when every node is finished.
   bool Step()
   {
      bool pending = false;
      for (Node& n : mNodes)
      {
         if (n.finished)
            continue;
         GraphNode* gn = FindNodeByIndex(n.index);
         if (gn == nullptr)
         {
            Finish(n);
            continue;
         }
         if (n.stage == 0)
         {
            // A node registers its controls only on a frame that draws its body; give a
            // slow first frame a few ticks before deciding it has none.
            bool any = false;
            for (const ParamRef& r : Modulation::Instance().FrameParams())
               any = any || r.nodeIndex == n.index;
            if (!any && n.wait++ < 30)
            {
               pending = true;
               continue;
            }
            n.stage = 1;
            Tier1(n, *gn);
         }
         else
            Tier2(n, *gn);
         if (!n.finished)
            pending = true;
      }
      return !pending;
   }

private:
   struct Node
   {
      std::string type;
      int index = 0;
      bool finished = false;
      ParamJoinType join;
      std::vector<std::pair<std::string, char>> keys;
      std::map<int, float> baseline;      // unmatched paramIndex -> value before perturbing
      std::map<int, std::string> label;   // unmatched paramIndex -> label
      std::map<int, std::pair<float, float>> range;
      std::map<std::string, int> attempt;
      std::vector<std::string> queue;     // candidate keys still to try
      size_t next = 0;
      std::string trying;
      KeyPerturber perturb;
      bool applied = false;
      int wait = 0;
      int stage = 0;
   };
   std::vector<Node> mNodes;
   bool mTier1Only = false;

   void Finish(Node& n)
   {
      n.finished = true;
      ParamJoinType& out = gParamJoin[n.type];
      out = n.join;
      out.done = true;
      out.buttons = gProbeButtons[n.index];
      for (const auto& kv : n.label)
         if (!out.keyOfParam.count(kv.first))
         {
            out.unkeyed.insert(kv.first);
            out.unkeyedName[kv.first] = kv.second;
         }
      std::set<std::string> used;
      for (const auto& kv : out.keyOfParam)
         used.insert(kv.second);
      for (const auto& k : n.keys)
         if (!used.count(k.first))
            out.plainKeys.insert(k.first);
      // Remember the key on the probe's sticky record too.
      for (const auto& kv : out.keyOfParam)
      {
         Modulation::Instance().SetKnownKey(n.index, kv.first, kv.second);
         if (const ParamRef* k = Modulation::Instance().KnownParam(n.index, kv.first))
            if (!k->enumOptions.empty())
               out.optionsOfKey[kv.second] = k->enumOptions;
      }
   }

   void Tier1(Node& n, GraphNode& gn)
   {
      AddrCollector ac;
      gn.node->VisitParams(ac);
      n.keys = ac.keys;
      std::set<std::string> taken;
      for (const ParamRef& r : Modulation::Instance().FrameParams())
      {
         if (r.nodeIndex != n.index)
            continue;
         n.join.registered++;
         auto it = r.srcAddr != nullptr ? ac.keyOfAddr.find(r.srcAddr) : ac.keyOfAddr.end();
         if (it != ac.keyOfAddr.end() && !taken.count(it->second))
         {
            taken.insert(it->second);
            n.join.keyOfParam[r.paramIndex] = it->second;
            n.join.paramOfKey[it->second] = r.paramIndex;
         }
         else if (!n.join.keyOfParam.count(r.paramIndex))
         {
            n.label[r.paramIndex] = r.name;
            n.range[r.paramIndex] = { r.minValue, r.maxValue };
            if (r.value != nullptr)
               n.baseline[r.paramIndex] = *r.value;
         }
      }
      // A param that ended up matched on a later ref is not unmatched.
      for (auto it = n.label.begin(); it != n.label.end();)
         it = n.join.keyOfParam.count(it->first) ? n.label.erase(it) : std::next(it);
      if (n.label.empty() || mTier1Only)
      {
         Finish(n);
         return;
      }
      for (const auto& k : n.keys)
         if (!taken.count(k.first))
            n.queue.push_back(k.first);
      Apply(n, gn);
   }

   // Perturb the next candidate key, or finish.
   void Apply(Node& n, GraphNode& gn)
   {
      if (n.next >= n.queue.size())
      {
         Finish(n);
         return;
      }
      n.trying = n.queue[n.next++];
      n.perturb = KeyPerturber();
      n.perturb.key = n.trying;
      n.perturb.attempt = n.attempt[n.trying];
      gn.node->VisitParams(n.perturb);
      n.applied = n.perturb.hit;
   }

   void Tier2(Node& n, GraphNode& gn)
   {
      if (n.applied)
      {
         // Which unmatched control followed the change?
         int found = -1, count = 0;
         for (const ParamRef& r : Modulation::Instance().FrameParams())
         {
            if (r.nodeIndex != n.index || !n.label.count(r.paramIndex) || r.value == nullptr)
               continue;
            const float base = n.baseline[r.paramIndex];
            const float v = *r.value;
            const bool moved = std::fabs(v - base) > 1e-4f;
            const bool followed = n.perturb.kind == 'f' ? std::fabs(v - (float)n.perturb.now) < 1e-3f
                                                        : std::lround(v) == std::lround(n.perturb.now);
            if (moved && followed)
            {
               found = r.paramIndex;
               count++;
            }
         }
         n.perturb.mode = 1;
         gn.node->VisitParams(n.perturb); // put it back
         if (count == 1)
         {
            n.join.keyOfParam[found] = n.trying;
            n.join.paramOfKey[n.trying] = found;
            n.label.erase(found);
            n.baseline.erase(found);
            n.range.erase(found);
         }
         else if (count == 0 && n.attempt[n.trying] < 2)
         {
            // The widget may have clamped the value away; try the next candidate.
            n.attempt[n.trying]++;
            n.queue.push_back(n.trying);
         }
      }
      if (n.label.empty())
      {
         Finish(n);
         return;
      }
      Apply(n, gn);
   }
};

// Fills a type's per-key control table from the join and the sticky ParamRef records of
// the drawn probe node `nodeIndex`. Options come from KnownParam (FrameParams drops them).
static void AttachControls(PatchSchema::TypeSchema& ts, int nodeIndex)
{
   auto it = gParamJoin.find(ts.name);
   ts.controls.clear();
   ts.controlsKnown = it != gParamJoin.end() && it->second.done;
   if (!ts.controlsKnown)
      return;
   for (const auto& kv : it->second.keyOfParam)
   {
      const ParamRef* k = Modulation::Instance().KnownParam(nodeIndex, kv.first);
      if (k == nullptr)
         continue;
      PatchSchema::ControlInfo c;
      c.key = kv.second;
      c.label = k->name.empty() ? kv.second : k->name;
      c.index = kv.first;
      c.hasRange = true;
      c.minValue = k->minValue;
      c.maxValue = k->maxValue;
      c.step = k->step;
      c.isEnum = k->isEnum;
      c.isBool = k->isBool;
      c.options = k->enumOptions;
      ts.controls.push_back(std::move(c));
   }
   ts.actions.clear();
   for (const std::string& b : it->second.buttons)
      ts.actions.push_back({ b, PatchSchema::ClassifyAction(b) });
}

// The running app never probes node types, so its key join is empty and `explain` could not name
// a control or a dropdown option. Fill it from the nodes already on the canvas, address match only.
void JoinLiveTier1()
{
   std::vector<std::pair<std::string, int>> live;
   std::set<std::string> seen;
   for (GraphNode& gn : gNodes)
      if (!gParamJoin[gn.typeName].done && seen.insert(gn.typeName).second)
         live.push_back({ gn.typeName, gn.index });
   if (live.empty())
      return;
   ParamKeyJoiner joiner;
   joiner.SetTier1Only(true);
   joiner.Begin(live);
   joiner.Step();
}

// A take's first audio blocks start every node from its last pushed parameters. Those came from
// the wall-clock frame that cooked before the take (a modulator read at an arbitrary transport time),
// so two renders began from slightly different values and the smoothers carried the difference for
// a while. Cook one frame at the take's own start time, then re-prepare, so the starting state
// depends on the patch and the start time only.
static void PrimeOfflineStart(int& frameId, double startSeconds)
{
   ++frameId;
   Transport::Instance().SetOfflineVideoTime(startSeconds);
   ApplyModulationAndPalette(frameId);
   ArrangeSeekVideoSampleSources(Transport::Instance().Beats());
   for (GraphNode& gn : gNodes)
      if (!gn.node->bypassed)
         gn.node->CookIfNeeded(frameId);
   ForceAudioRepare();
   RebuildAudioTopology();
}

void HeadlessTick(int& frameId, GLFWwindow* window)
{
   enum class Phase { Warm, Running, Frames, NodeRender, Audio, Done };
   static Platform::RecorderHandle* sNodeRec = nullptr; // --render --node: the encoder, opened on the first frame
   static int sNodeRecW = 0, sNodeRecH = 0;
   static int sNodeStartFrame = 0, sNodeEndFrame = 0;
   static Phase sPhase = Phase::Warm;
   static int sTicks = 0;
   static int sPass = 0;                    // --frame --repeat: which pass is running (0-based)
   static double sWall = -1.0;
   static Headless::Status sStatus;
   static OutputNode* sOut = nullptr;
   static INode* sTap = nullptr;            // the node whose image --frame / --frames-dir reads (an Output, or --node)
   static int sTapOutput = 0;
   static std::vector<double> sTimes;       // --frame: the requested times; --frames-dir: start + i/fps
   static int sOutIndex = -1;
   static size_t sNextTime = 0;
   static int sNextFrame = 0;
   static std::string sFrameStats;          // --frame: one JSON object per exported frame
   static ContactSheet::Builder sSheet;     // --frame --contact-sheet
   static std::unique_ptr<PngSequenceWriter> sPngWriter; // --frames-dir: zlib off the GL thread
   // --audio-summary
   static std::unique_ptr<AudioSummary::Analyzer> sAnalyzer;
   static AudioFileWriter sSummaryWav;
   static AudioCaptureRing* sSinkRing = nullptr; // one sink picked with --output; null = the device mix
   static std::string sAudioSource;              // JSON describing what was measured
   static double sAudioSeconds = 0.0;
   static long long sAudioTotal = 0, sAudioDone = 0, sAudioStep = 0;
   // --stems / --notes (R471 7.5)
   struct StemTap
   {
      std::string file;
      int nodeIndex = 0;
   };
   struct SchedEvent
   {
      long long sample = 0;
      NoteEvent ev;
      NoteEventQueue* queue = nullptr;
   };
   static std::vector<AudioEngine::OfflineTap> sTaps;
   static std::vector<StemTap> sStemInfo;
   static std::vector<SchedEvent> sSched;
   static size_t sSchedNext = 0;
   static int sNoteTag = 0; // NoteEvent::source for injected notes: any stable address
   static std::string sNotesJson;

   const Headless::Job& job = gHeadlessJob;
   if (sPhase == Phase::Done || !HeadlessJobActive() || job.mode == Headless::Mode::Version ||
       glfwWindowShouldClose(window))
      return;
   if (sWall < 0.0)
   {
      sWall = glfwGetTime();
      sStatus.mode = job.mode == Headless::Mode::Render     ? "render"
                     : job.mode == Headless::Mode::Describe ? "describe"
                     : job.mode == Headless::Mode::Validate ? "validate"
                     : job.mode == Headless::Mode::Canonicalize ? "canonicalize"
                     : job.mode == Headless::Mode::Explain ? "explain"
                     : job.mode == Headless::Mode::AudioSummary ? "audio-summary"
                     : job.mode == Headless::Mode::Frames ? "frames"
                                                            : "frame";
      sStatus.warnings = gHeadlessPreWarnings;
      sStatus.patch = job.patch;
      sStatus.out = job.out;
      sStatus.fps = job.fps;
   }
   auto fail = [&](const char* code, const std::string& msg, int node = -1)
   {
      sStatus.errors.push_back({ code, msg, 0, node });
      sPhase = Phase::Done;
      HeadlessFinish(window, sStatus, sWall);
   };

   if (glfwGetTime() - sWall > job.timeoutSec)
   {
      if (gOfflineRender.active && gOfflineRender.node != nullptr)
         gOfflineRender.node->RequestFinishOfflineRender(true);
      fail("E_TIMEOUT", "job exceeded --timeout " + std::to_string((int)job.timeoutSec) + " s");
      return;
   }

   if (gHeadlessNeedProbe)
   {
      // The file names controls (`mod 5 radius ...`) or dropdown options (`i shapeType Star`).
      // Both only exist once a node of each type has been drawn, so draw one of each, join
      // them to their saved keys, resolve the patch, and only then load it.
      static std::vector<std::pair<std::string, int>> sPr;
      static ParamKeyJoiner sPrJoin;
      static bool sPrBegun = false;
      static int sPrTicks = 0;
      const int t = ++sPrTicks;
      if (t == 1)
      {
         std::set<std::string> types;
         for (const Patch::NodeRecord& n : gHeadlessPatch.nodes)
            types.insert(n.typeName);
         int i = 0;
         for (const std::string& type : types)
         {
            const PatchSchema::TypeSchema* ts = SchemaFor(type);
            if (ts == nullptr || ts->hardwareDriven)
               continue;
            GraphNode* gn = SpawnNode(type, ts->category, (float)(i % 16) * 420.0f, (float)(i / 16) * 700.0f);
            if (gn == nullptr)
               continue;
            gn->showParams = true;
            sPr.push_back({ type, gn->index });
            i++;
         }
         gHeadlessProbeAll = true;
         gHeadlessDrawn.clear();
         return;
      }
      if (!HeadlessProbeDone(sPr, t) && t < kHeadlessProbeCapTicks)
         return;
      if (!sPrBegun)
      {
         std::vector<std::pair<std::string, int>> drawn;
         for (const auto& pr : sPr)
            if (gHeadlessDrawn.count(pr.second))
               drawn.push_back(pr);
         sPrJoin.Begin(drawn);
         sPrBegun = true;
      }
      if (!sPrJoin.Step() && t < 4 * kHeadlessProbeCapTicks)
         return;
      gHeadlessProbeAll = false;
      gHeadlessNeedProbe = false;
      std::vector<Headless::Issue> keyErrors;
      PatchSchema::ResolveKeys(gHeadlessPatch, MakeSchemaEnv(true), keyErrors);
      if (!keyErrors.empty())
      {
         for (const Headless::Issue& is : gHeadlessPromoted)
            sStatus.errors.push_back(is);
         for (const Headless::Issue& is : keyErrors)
            sStatus.errors.push_back(is);
         std::stable_sort(sStatus.errors.begin(), sStatus.errors.end(),
                          [](const Headless::Issue& a, const Headless::Issue& b)
                          { return (a.line > 0 ? a.line : 1 << 30) < (b.line > 0 ? b.line : 1 << 30); });
         sPhase = Phase::Done;
         HeadlessFinish(window, sStatus, sWall);
         return;
      }
      if (job.mode == Headless::Mode::Canonicalize)
      {
         FinishCanonicalize(gHeadlessPatch, job, sStatus);
         sPhase = Phase::Done;
         HeadlessFinish(window, sStatus, sWall);
         return;
      }
      LoadPatchFrom(job.patch); // starts from an empty canvas, so the probes are gone
      return;
   }

   if (job.mode == Headless::Mode::Validate)
   {
      static Patch::Data sData;
      static std::vector<std::pair<std::string, int>> sProbed; // type, node index
      static std::vector<std::pair<std::string, int>> sRequired; // the ones a mod/expr line drives
      static ParamKeyJoiner sJoin;
      static bool sJoinBegun = false;
      const int t = ++sTicks;
      if (t == 1)
      {
         std::string err;
         if (!Patch::Read(job.patch, sData, err))
            return fail("E_LOAD", err);
         PatchSchema::Resolve(sData, MakeSchemaEnv(job.forRender), sStatus.errors);
         if (!sStatus.errors.empty())
         {
            sPhase = Phase::Done;
            HeadlessFinish(window, sStatus, sWall);
            return;
         }
         // The parameter indices a `mod`/`expr` line may use only exist once a
         // node has been drawn, so draw one of each type the file drives.
         std::set<std::string> driven;
         std::map<int, std::string> typeOfIndex;
         for (const Patch::NodeRecord& n : sData.nodes)
            typeOfIndex[n.index] = n.typeName;
         for (const Patch::ModRecord& m : sData.modulation)
            if (typeOfIndex.count(m.dstIndex))
               driven.insert(typeOfIndex[m.dstIndex]);
         for (const Patch::ExprRecord& e : sData.expressions)
            if (typeOfIndex.count(e.dstIndex))
               driven.insert(typeOfIndex[e.dstIndex]);
         // Every other type in the file is probed too, for its control ranges
         // (W_OUT_OF_RANGE / W_INTERNAL_PARAM), but a node that never draws
         // (a Group) is only an error when a mod/expr line needs it.
         std::set<std::string> all(driven);
         for (const Patch::NodeRecord& n : sData.nodes)
            all.insert(n.typeName);
         int i = 0;
         for (const std::string& type : all)
         {
            const PatchSchema::TypeSchema* ts = SchemaFor(type);
            if (ts == nullptr || ts->hardwareDriven)
               continue;
            GraphNode* gn = SpawnNode(type, ts->category, (float)(i % 16) * 420.0f, (float)(i / 16) * 700.0f);
            if (gn == nullptr)
               continue;
            gn->showParams = true;
            sProbed.push_back({ type, gn->index });
            if (driven.count(type))
               sRequired.push_back({ type, gn->index });
            i++;
         }
         if (!sProbed.empty())
         {
            gHeadlessProbeAll = true;
            gHeadlessDrawn.clear();
            return;
         }
      }
      else if (!sProbed.empty() && !HeadlessProbeDone(sProbed, t))
      {
         if (t < kHeadlessProbeCapTicks)
            return;
         if (!HeadlessProbeDone(sRequired, t))
            return fail("E_PROBE_TIMEOUT", "a node the patch drives never drew its parameters within " +
                                              std::to_string(kHeadlessProbeCapTicks) + " frames, so mod/expr lines cannot be checked");
      }
      if (!sProbed.empty())
      {
         if (!sJoinBegun)
         {
            std::vector<std::pair<std::string, int>> drawn;
            for (const auto& pr : sProbed)
               if (gHeadlessDrawn.count(pr.second))
                  drawn.push_back(pr);
            sJoin.Begin(drawn);
            sJoinBegun = true;
         }
         if (!sJoin.Step() && t < 4 * kHeadlessProbeCapTicks)
            return;
         for (const auto& pr : sProbed)
            if (gHeadlessDrawn.count(pr.second))
               if (PatchSchema::TypeSchema* mts = const_cast<PatchSchema::TypeSchema*>(SchemaFor(pr.first)))
                  AttachControls(*mts, pr.second);
      }
      gHeadlessProbeAll = false;

      for (const auto& pr : sProbed)
      {
         int max = -1;
         for (const ParamRef& r : Modulation::Instance().FrameParams())
            if (r.nodeIndex == pr.second)
               max = std::max(max, r.paramIndex);
         gModulatableMax[pr.first] = max;
      }
      const PatchSchema::Env env = MakeSchemaEnv(job.forRender);
      PatchSchema::ResolveKeys(sData, env, sStatus.errors);
      if (sStatus.errors.empty())
         PatchSchema::Validate(sData, env, sStatus.errors, sStatus.warnings);
      if (!job.lenient)
         Headless::PromoteWarnings(sStatus.warnings, sStatus.errors);
      sStatus.extraJson.push_back("\"nodes\":" + std::to_string(sData.nodes.size()));
      sPhase = Phase::Done;
      HeadlessFinish(window, sStatus, sWall);
      return;
   }

   if (job.mode == Headless::Mode::Describe)
   {
      static std::vector<std::pair<std::string, int>> sSpawned; // type, node index
      const int t = ++sTicks;
      std::vector<std::string> types;
      for (const std::string& cat : NodeFactory::Instance().GetCategories())
         for (const std::string& n : NodeFactory::Instance().GetNodesInCategory(cat))
            if (job.describeType.empty() || n == job.describeType)
               types.push_back(n);
      if (t == 1)
      {
         if (types.empty())
         {
            std::vector<std::string> all;
            for (const std::string& cat : NodeFactory::Instance().GetCategories())
               for (const std::string& n : NodeFactory::Instance().GetNodesInCategory(cat))
                  all.push_back(n);
            Headless::Issue is;
            is.code = "E_UNKNOWN_TYPE";
            is.message = "unknown node type '" + job.describeType + "'";
            const std::vector<std::string> near = PatchSchema::Nearest(job.describeType, all, 3);
            for (size_t i = 0; i < near.size(); i++)
               is.hint += (i ? ", " : "did you mean: ") + near[i];
            sStatus.errors.push_back(is);
            sPhase = Phase::Done;
            HeadlessFinish(window, sStatus, sWall);
            return;
         }
         // Spawn one expanded instance of each type on the canvas so the draw
         // pass registers its modulatable parameters (ParamRef ranges, labels).
         // A node that opens a device on spawn is described from its schema alone.
         int i = 0;
         for (const std::string& n : types)
         {
            const PatchSchema::TypeSchema* ts = SchemaFor(n);
            if (ts == nullptr || ts->hardwareDriven)
               continue;
            GraphNode* gn = SpawnNode(n, ts->category, (float)(i % 16) * 420.0f, (float)(i / 16) * 700.0f);
            if (gn == nullptr)
               continue;
            gn->showParams = true;
            sSpawned.push_back({ n, gn->index });
            i++;
         }
         gHeadlessProbeAll = true;
         gHeadlessDrawn.clear();
         return;
      }
      if (!HeadlessProbeDone(sSpawned, t) && t < kHeadlessProbeCapTicks)
         return;
      // Which saved key is each control? Needs the nodes still drawing.
      static ParamKeyJoiner sJoiner;
      static bool sJoinBegun = false;
      if (!sJoinBegun)
      {
         std::vector<std::pair<std::string, int>> drawn;
         for (const auto& pr : sSpawned)
            if (gHeadlessDrawn.count(pr.second))
               drawn.push_back(pr);
         sJoiner.Begin(drawn);
         sJoinBegun = true;
      }
      if (!sJoiner.Step() && t < 4 * kHeadlessProbeCapTicks)
         return;
      gHeadlessProbeAll = false;
      // A node that never draws (a Group, a Comment) registers nothing: it is
      // described from its schema alone and listed so that is visible.
      {
         std::string undrawn;
         for (const auto& pr : sSpawned)
            if (!gHeadlessDrawn.count(pr.second))
               undrawn += (undrawn.empty() ? "\"" : ",\"") + Headless::JsonEscape(pr.first) + "\"";
         if (!undrawn.empty())
            fprintf(stderr, "describe: never drawn: %s\n", undrawn.c_str());
      }

      std::map<std::string, int> indexOf(sSpawned.begin(), sSpawned.end());
      std::string typesJson = "\"types\":[";
      std::string unkeyedList;
      int joinRegistered = 0, joinKeyed = 0;
      bool first = true;
      for (const std::string& n : types)
      {
         PatchSchema::TypeSchema ts = *SchemaFor(n);
         auto it = indexOf.find(n);
         const ParamJoinType* join = gParamJoin.count(n) ? &gParamJoin[n] : nullptr;
         if (it != indexOf.end())
            for (const ParamRef& r : Modulation::Instance().FrameParams())
               if (r.nodeIndex == it->second)
               {
                  std::string key;
                  if (join != nullptr && join->keyOfParam.count(r.paramIndex))
                     key = join->keyOfParam.at(r.paramIndex);
                  const ParamRef* known = Modulation::Instance().KnownParam(r.nodeIndex, r.paramIndex);
                  ts.modulatable.push_back({ r.paramIndex, r.name, r.minValue, r.maxValue, r.step, r.isEnum, r.isBool,
                                             known != nullptr ? known->enumOptions : r.enumOptions, key });
                  ts.joinRegistered++;
                  if (!key.empty())
                     ts.joinKeyed++;
                  else
                     unkeyedList += (unkeyedList.empty() ? "" : ",") + std::string("{\"type\":\"") + Headless::JsonEscape(n) +
                                    "\",\"index\":" + std::to_string(r.paramIndex) + ",\"label\":\"" + Headless::JsonEscape(r.name) + "\"}";
               }
         if (it != indexOf.end())
            AttachControls(ts, it->second);
         joinRegistered += ts.joinRegistered;
         joinKeyed += ts.joinKeyed;
         std::sort(ts.modulatable.begin(), ts.modulatable.end(),
                   [](const auto& a, const auto& b) { return a.index < b.index; });
         typesJson += (first ? "" : ",") + PatchSchema::ToJson(ts);
         first = false;
      }
      typesJson += "]";
      sStatus.extraJson.push_back("\"join_stats\":{\"registered\":" + std::to_string(joinRegistered) + ",\"keyed\":" +
                                  std::to_string(joinKeyed) + ",\"unkeyed\":" + std::to_string(joinRegistered - joinKeyed) + "}");
      sStatus.extraJson.push_back("\"unkeyed\":[" + unkeyedList + "]");
      sStatus.extraJson.push_back("\"count\":" + std::to_string(types.size()));
      sStatus.extraJson.push_back(typesJson);
      sPhase = Phase::Done;
      HeadlessFinish(window, sStatus, sWall);
      return;
   }

   if (sPhase == Phase::Warm)
   {
      if (sTicks == 0)
      {
         gHeadlessProbeAll = true;
         gHeadlessDrawn.clear();
      }
      if (++sTicks < 3)
         return;
      // The parameter-index check below needs every node a mod/expr line
      // drives to have drawn (it registers its parameters by drawing). They
      // are param-driven, so the cull never skips them; this only guards a
      // slow first frame. Past the cap the check runs on what registered.
      if (sTicks < kHeadlessProbeCapTicks)
      {
         std::map<int, std::string> typeOfFileIndex;
         for (const Patch::NodeRecord& n : gHeadlessPatch.nodes)
            typeOfFileIndex[n.index] = n.typeName;
         std::set<std::string> drivenTypes;
         for (const Patch::ModRecord& m : gHeadlessPatch.modulation)
            if (typeOfFileIndex.count(m.dstIndex))
               drivenTypes.insert(typeOfFileIndex[m.dstIndex]);
         for (const Patch::ExprRecord& e : gHeadlessPatch.expressions)
            if (typeOfFileIndex.count(e.dstIndex))
               drivenTypes.insert(typeOfFileIndex[e.dstIndex]);
         for (const std::string& type : drivenTypes)
         {
            bool drew = false;
            for (GraphNode& g : gNodes)
               if (g.typeName == type && gHeadlessDrawn.count(g.index))
                  drew = true;
            if (!drew)
               return;
         }
      }
      gHeadlessProbeAll = false;

      // E_BAD_PARAM needs drawn nodes, so it is checked here on the loaded
      // graph rather than in the strict pass before load.
      {
         for (const ParamRef& r : Modulation::Instance().FrameParams())
            if (GraphNode* g = FindNodeByIndex(r.nodeIndex))
            {
               auto it = gModulatableMax.find(g->typeName);
               gModulatableMax[g->typeName] = std::max(it == gModulatableMax.end() ? -1 : it->second, r.paramIndex);
            }
         for (GraphNode& g : gNodes)
            if (!gModulatableMax.count(g.typeName))
               gModulatableMax[g.typeName] = -1;
         std::vector<Headless::Issue> bad;
         PatchSchema::CheckParamIndices(gHeadlessPatch, MakeSchemaEnv(true), bad);
         if (!bad.empty() || !gHeadlessPromoted.empty())
         {
            // Everything strict mode found, in file order, so one fix round clears it.
            for (const Headless::Issue& is : gHeadlessPromoted)
               sStatus.errors.push_back(is);
            for (const Headless::Issue& is : bad)
               sStatus.errors.push_back(is);
            std::stable_sort(sStatus.errors.begin(), sStatus.errors.end(),
                             [](const Headless::Issue& a, const Headless::Issue& b)
                             { return (a.line > 0 ? a.line : 1 << 30) < (b.line > 0 ? b.line : 1 << 30); });
            sPhase = Phase::Done;
            HeadlessFinish(window, sStatus, sWall);
            return;
         }
      }

      if (job.mode == Headless::Mode::Explain)
      {
         // The live graph, read back after the load: what ApplyPatchData built, not the file.
         sStatus.stdoutText = ExplainLive(job.explainJson, job.explainAll);
         sStatus.warnings = gHeadlessPreWarnings;
         sStatus.ok = true;
         sPhase = Phase::Done;
         HeadlessFinish(window, sStatus, sWall);
         return;
      }

      if (job.mode == Headless::Mode::AudioSummary)
      {
         // Offline audio only: nothing is encoded. The graph is stepped the
         // way --render steps it, so the numbers describe the audio a render
         // of the same range would carry.
         if (INode* hw = FindHardwareDrivenNode())
         {
            int hwIndex = -1;
            for (GraphNode& g : gNodes)
               if (g.node.get() == hw)
                  hwIndex = g.index;
            return fail("E_HARDWARE_SOURCE", "the patch has a live source (camera/MIDI/Syphon In) "
                                             "that can't be pre-synthesized offline", hwIndex);
         }

         // Every place audio leaves the graph: an Audio Out, or an Output with its audio input wired.
         struct Sink
         {
            int index;
            std::string type;
            AudioCaptureRing* ring;
         };
         std::vector<Sink> sinks;
         std::vector<OutputNode*> videoOuts;
         for (GraphNode& g : gNodes)
         {
            auto* audioOut = dynamic_cast<AudioOutputNode*>(g.node.get());
            auto* outNode = dynamic_cast<OutputNode*>(g.node.get());
            if (outNode != nullptr)
               videoOuts.push_back(outNode);
            if (audioOut == nullptr && outNode == nullptr)
               continue;
            bool wired = false;
            for (int slot = 0; slot < kMaxAudioSlots; slot++)
               if (AudioCable* cable = g.node->AudioInputSlot(slot))
                  wired = wired || cable->IsConnected();
            if (wired)
               sinks.push_back({ g.index, g.typeName, audioOut ? &audioOut->CaptureRing() : &outNode->CaptureRing() });
         }
         if (sinks.empty())
            return fail("E_NO_AUDIO", "no audio reaches an Audio Out or an Output's audio input, so there is nothing to measure");

         sSinkRing = nullptr;
         sAudioSource = "\"source\":\"mix\"";
         if (!job.output.empty())
         {
            char* end = nullptr;
            const long want = std::strtol(job.output.c_str(), &end, 10);
            const bool numeric = end != nullptr && *end == '\0';
            for (const Sink& sk : sinks)
               if (numeric && sk.index == (int)want)
               {
                  sSinkRing = sk.ring;
                  sAudioSource = "\"source\":\"node\",\"source_node\":" + std::to_string(sk.index);
               }
            if (sSinkRing == nullptr)
               return fail("E_NO_OUTPUT", "--output " + job.output + " is not an Audio Out or an Output with audio wired in");
         }
         sAudioSource += ",\"sinks\":[";
         for (size_t i = 0; i < sinks.size(); i++)
            sAudioSource += std::string(i ? "," : "") + "{\"node\":" + std::to_string(sinks[i].index) + ",\"type\":\"" +
                            Headless::JsonEscape(sinks[i].type) + "\"}";
         sAudioSource += "]";

         // Same default range as --render: the one Output's own duration.
         sAudioSeconds = job.duration > 0.0 ? job.duration
                         : videoOuts.size() == 1 ? (double)std::max(1, videoOuts.front()->offlineDurationSeconds)
                                                 : 10.0;
         sAudioTotal = (long long)std::llround(sAudioSeconds * gHeadlessAudioRate);
         sAudioDone = 0;
         sAudioStep = 0;

         std::error_code ec;
         for (const std::string* path : { &job.out, &job.wavPath })
            if (!path->empty() && !std::filesystem::path(*path).parent_path().empty())
               std::filesystem::create_directories(std::filesystem::path(*path).parent_path(), ec);
         if (!job.wavPath.empty() &&
             !sSummaryWav.Open(job.wavPath, gHeadlessAudioRate, 2, AudioFileWriter::Format::Wav))
            return fail("E_RENDER", "could not create " + job.wavPath);

         // Node refs in --stems / --note-map: an index, or the patch's `id <word>`.
         auto resolveRef = [&](const std::string& ref) -> GraphNode*
         {
            char* e = nullptr;
            const long idx = std::strtol(ref.c_str(), &e, 10);
            if (e != ref.c_str() && *e == '\0')
               return FindNodeByIndex((int)idx);
            for (const Patch::NodeRecord& n : gHeadlessPatch.nodes)
               if (!n.id.empty() && n.id == ref)
                  return FindNodeByIndex(n.index);
            return nullptr;
         };

         sTaps.clear();
         sStemInfo.clear();
         sSched.clear();
         sSchedNext = 0;
         sNotesJson.clear();
         AudioEngine::Instance().SetOfflineTaps(nullptr);
         for (const std::string& ref : job.stems)
         {
            GraphNode* g = resolveRef(ref);
            if (g == nullptr)
               return fail("E_BAD_REF", "--stems " + ref + ": the patch has no such node");
            auto* as = dynamic_cast<IAudioSource*>(g->node.get());
            if (as == nullptr || as->GetAudioNode() == nullptr)
               return fail("E_BAD_REF", "--stems " + ref + ": " + g->typeName + " has no audio output", g->index);
            std::string name;
            for (char c : ref)
               name += (std::isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_';
            if (std::isdigit((unsigned char)name[0]) || name.empty())
               name = "node" + std::to_string(g->index);
            AudioEngine::OfflineTap tap;
            tap.node = as->GetAudioNode();
            tap.left.reserve((size_t)sAudioTotal);
            tap.right.reserve((size_t)sAudioTotal);
            sTaps.push_back(std::move(tap));
            sStemInfo.push_back({ (std::filesystem::path(job.stemsDir) / (name + ".wav")).string(), g->index });
         }
         if (!job.stemsDir.empty())
            std::filesystem::create_directories(job.stemsDir, ec);

         if (!job.notes.empty())
         {
            Headless::NoteSchedule plan;
            std::string notesError;
            if (!Headless::LoadNoteSchedule(job.notes, job.noteMap, plan, notesError))
               return fail("E_USAGE", notesError);
            std::map<std::string, NoteEventQueue*> queueOf;
            for (const std::string& ref : plan.nodes)
            {
               GraphNode* g = resolveRef(ref);
               if (g == nullptr)
                  return fail("E_BAD_REF", "--note-map: the patch has no node '" + ref + "'");
               AudioNode* an = nullptr;
               if (auto* as = dynamic_cast<IAudioSource*>(g->node.get()))
                  an = as->GetAudioNode();
               else if (auto* ns = dynamic_cast<INoteSource*>(g->node.get()))
                  an = ns->GetAudioNode();
               NoteEventQueue* q = an != nullptr ? an->NoteOutbox() : nullptr;
               for (int slot = 0; an != nullptr && q == nullptr && slot < AudioNode::kMaxNoteSlots; slot++)
                  q = an->appliedInbox[slot];
               if (q == nullptr)
                  return fail("E_BAD_REF", "--note-map: " + g->typeName + " '" + ref + "' has no note source wired in; "
                                           "patch a Keyboard or other note node into it", g->index);
               queueOf[ref] = q;
            }
            int injected = 0;
            for (const Headless::NoteHit& h : plan.hits)
            {
               const long long on = (long long)std::llround((h.t - job.start) * gHeadlessAudioRate);
               if (on < 0 || on >= sAudioTotal)
                  continue;
               NoteEvent ev;
               ev.note = h.pitch;
               ev.velocity = h.velocity;
               ev.source = &sNoteTag;
               ev.voiceId = NextVoiceId();
               ev.isNoteOn = true;
               sSched.push_back({ on, ev, queueOf[h.node] });
               ev.isNoteOn = false;
               sSched.push_back({ on + std::max<long long>(1, (long long)std::llround(h.length * gHeadlessAudioRate)), ev, queueOf[h.node] });
               injected++;
            }
            std::stable_sort(sSched.begin(), sSched.end(),
                             [](const SchedEvent& x, const SchedEvent& y) { return x.sample < y.sample; });
            std::string unmapped = "[";
            for (size_t i = 0; i < plan.unmappedTypes.size(); i++)
               unmapped += std::string(i ? "," : "") + "\"" + Headless::JsonEscape(plan.unmappedTypes[i]) + "\"";
            unmapped += "]";
            sNotesJson = "\"notes\":{\"events\":" + std::to_string(plan.hits.size()) + ",\"injected\":" + std::to_string(injected) +
                         ",\"unmapped_types\":" + unmapped + "}";
            if (!plan.unmappedTypes.empty())
               sStatus.warnings.push_back({ "W_UNMAPPED_EVENT", "no map entry for " + std::to_string(plan.unmappedTypes.size()) +
                                                                 " event type(s), ignored", 0, -1, "add them to --note-map to hear them" });
            if (!plan.panIgnoredNodes.empty())
               sStatus.warnings.push_back({ "W_NOTE_PAN_IGNORED", "pan_from is not applied: notes carry no per-note pan", 0, -1,
                                            "pan the node's own output instead" });
         }
         if (!sTaps.empty())
            AudioEngine::Instance().SetOfflineTaps(&sTaps);

         sAnalyzer = std::make_unique<AudioSummary::Analyzer>(gHeadlessAudioRate);
         Transport::Instance().Seek(job.start);
         Transport::Instance().SetOfflineMode(true, gHeadlessAudioRate);
         Transport::Instance().SetPlaying(true);
         PrimeOfflineStart(frameId, job.start);
         if (sSinkRing != nullptr)
         {
            float discard[4096];
            while (sSinkRing->Read(discard, 4096) > 0)
               ;
            sSinkRing->enabled.store(true, std::memory_order_relaxed);
         }
         sPhase = Phase::Audio;
         return;
      }

      sTap = nullptr;
      sTapOutput = 0;
      if (!job.node.empty())
      {
         // --node <index[:output]>: tap that node's image; no Output is needed.
         char* end = nullptr;
         const long idx = std::strtol(job.node.c_str(), &end, 10);
         long outSlot = 0;
         if (end == job.node.c_str() || (*end != '\0' && *end != ':') ||
             (*end == ':' && (outSlot = std::strtol(end + 1, &end, 10), *end != '\0')))
            return fail("E_USAGE", "--node needs a node index or index:output, got '" + job.node + "'");
         GraphNode* tg = FindNodeByIndex((int)idx);
         if (tg == nullptr)
            return fail("E_BAD_REF", "--node " + job.node + ": the patch has no node with index " + std::to_string(idx));
         if (outSlot < 0 || outSlot >= tg->node->OutputCount())
            return fail("E_BAD_SLOT", "--node " + job.node + ": " + tg->typeName + " has " +
                                         std::to_string(tg->node->OutputCount()) + " output(s)", (int)idx);
         sTap = tg->node.get();
         sTapOutput = (int)outSlot;
      }
      else
      {
         // Resolve the Output. --output takes a node index or a display name.
         std::vector<int> outs;
         for (GraphNode& gn : gNodes)
            if (gn.typeName == "Output")
               outs.push_back(gn.index);
         if (outs.empty())
            return fail("E_NO_OUTPUT", "the patch has no Output node");
         if (!job.output.empty())
         {
            char* end = nullptr;
            const long want = std::strtol(job.output.c_str(), &end, 10);
            const bool numeric = end != nullptr && *end == '\0';
            sOutIndex = -1;
            for (int idx : outs)
               if (numeric && idx == (int)want)
                  sOutIndex = idx;
            if (sOutIndex < 0)
               return fail("E_NO_OUTPUT", "--output " + job.output + " is not an Output node index");
         }
         else if (outs.size() > 1)
            return fail("E_AMBIGUOUS_OUTPUT", "the patch has " + std::to_string(outs.size()) +
                                                 " Output nodes; pick one with --output <index>");
         else
            sOutIndex = outs.front();
         GraphNode* gn = FindNodeByIndex(sOutIndex);
         sOut = gn != nullptr ? static_cast<OutputNode*>(gn->node.get()) : nullptr;
         if (sOut == nullptr)
            return fail("E_NO_OUTPUT", "Output node vanished");
         sTap = sOut;
      }

      if (job.mode == Headless::Mode::Render)
      {
         std::string ext = std::filesystem::path(job.out).extension().string();
         for (char& c : ext)
            c = (char)tolower((unsigned char)c);
         if (ext != ".mp4" && ext != ".mov")
            return fail("E_UNSUPPORTED_CONTAINER", "--render writes .mp4 or .mov, got '" + ext + "'");
         if (job.codec == "prores4444")
         {
#ifndef __APPLE__
            return fail("E_UNSUPPORTED_CODEC", "--codec prores4444 is only available on macOS", -1);
#endif
            if (ext != ".mov")
               return fail("E_UNSUPPORTED_CONTAINER", "--codec prores4444 writes .mov, got '" + ext + "'");
            if (sOut != nullptr)
               sOut->recordProRes4444 = true;
         }
         if (sOut == nullptr)
         {
            // --render --node: no Output to record from. Step the patch the way
            // --frames-dir does and hand each tapped frame to the recorder.
            // Video only: a tapped node's image has no audio of its own.
            std::error_code ec;
            const std::filesystem::path parent = std::filesystem::path(job.out).parent_path();
            if (!parent.empty())
               std::filesystem::create_directories(parent, ec);
            std::filesystem::remove(job.out, ec);
            if (!job.noAudio)
               sStatus.warnings.push_back({ "W_NODE_NO_AUDIO", "--render --node writes video only", 0, -1,
                                            "pass --no-audio to silence this, or render without --node for the Output's audio" });
            sNodeStartFrame = (int)std::llround(job.start * (double)job.fps);
            sNodeEndFrame = sNodeStartFrame + std::max(1, (int)std::llround(job.duration * (double)job.fps));
            Transport::Instance().SetOfflineMode(true, gHeadlessAudioRate);
            Transport::Instance().SetPlaying(true);
            sNextFrame = 0;
            sPhase = Phase::NodeRender;
            return;
         }
         std::error_code ec;
         const std::filesystem::path parent = std::filesystem::path(job.out).parent_path();
         if (!parent.empty())
            std::filesystem::create_directories(parent, ec);
         std::filesystem::remove(job.out, ec);

         sOut->recordVideoPath = job.out;
         sOut->offlineFps = job.fps;
         if (job.duration > 0.0)
         {
            const int secs = std::max(1, (int)std::ceil(job.duration - 1e-9));
            if ((double)secs != job.duration)
               sStatus.warnings.push_back({ "W_DURATION_ROUNDED",
                                            "--duration rounds up to whole seconds (" + std::to_string(secs) + ")", 0, -1 });
            sOut->offlineDurationSeconds = secs;
         }
         sOut->includeAudio = !job.noAudio;

         // Seek before the session starts: StartOfflineRenderSession captures
         // the transport position for SetOfflineMode and the audio clock, so
         // seeking after it left picture at S and audio at 0.
         Transport::Instance().Seek(job.start);
         StartOfflineRenderSession(sOut);
         if (!gOfflineRender.active)
         {
            const std::string why = sOut->RecordStatus();
            return fail(why.rfind("refused:", 0) == 0 ? "E_HARDWARE_SOURCE" : "E_RENDER",
                        why.empty() ? "offline render did not start" : why);
         }
         gOfflineRender.startSeconds = job.start;
         PrimeOfflineStart(frameId, job.start);
         sStatus.frames = sOut->OfflineFramesTotal();
         sStatus.audioSampleRate = sOut->OfflineNeedsGraphAudio() ? gHeadlessAudioRate : 0.0;
         sStatus.audioFrames = sOut->OfflineNeedsGraphAudio()
                                  ? (long long)(gHeadlessAudioRate * (double)sStatus.frames / (double)job.fps)
                                  : 0;
         sPhase = Phase::Running;
         return;
      }

      // --frames-dir: every frame of start .. start+duration at fps, as a numbered PNG sequence.
      if (job.mode == Headless::Mode::Frames)
      {
         std::error_code ec;
         std::filesystem::create_directories(job.out, ec);
         const long long count = std::max(1LL, (long long)std::llround(job.duration * (double)job.fps));
         sTimes.clear();
         for (long long i = 0; i < count; i++)
            sTimes.push_back(job.start + (double)i / (double)job.fps);
         // Fast zlib by default: a sequence is a hand-off to the film engine, not an archive.
         sPngWriter = std::make_unique<PngSequenceWriter>(job.pngLevel >= 0 ? job.pngLevel : 1);
         Transport::Instance().SetOfflineMode(true, gHeadlessAudioRate);
         Transport::Instance().SetPlaying(true);
         sNextTime = 0;
         sNextFrame = 0;
         sPhase = Phase::Frames;
         return;
      }
      sTimes = job.times;
      if (job.pngLevel >= 0)
         stbi_write_png_compression_level = job.pngLevel;

      // --repeat N: every pass writes into its own pass<k>/ under the requested directory.
      if (job.repeat > 1)
      {
         static std::string sBaseOut;
         if (sPass == 0)
         {
            sBaseOut = job.out;
            std::string e = std::filesystem::path(sBaseOut).extension().string();
            for (char& c : e)
               c = (char)tolower((unsigned char)c);
            if (e == ".png")
               return fail("E_USAGE", "--repeat needs an output directory, not a .png file");
         }
         gHeadlessJob.out = sBaseOut + (sBaseOut.back() == '/' ? "" : "/") + "pass" + std::to_string(sPass + 1) + "/";
      }

      // --frame: a single .png, or a directory for one or several times.
      std::string ext = std::filesystem::path(job.out).extension().string();
      for (char& c : ext)
         c = (char)tolower((unsigned char)c);
      if (ext == ".png" && job.times.size() > 1)
         return fail("E_USAGE", "several --frame times need an output directory, not a .png file");
      if (ext != ".png")
      {
         std::error_code ec;
         std::filesystem::create_directories(job.out, ec);
      }
      else if (!std::filesystem::path(job.out).parent_path().empty())
      {
         std::error_code ec;
         std::filesystem::create_directories(std::filesystem::path(job.out).parent_path(), ec);
      }
      Transport::Instance().SetOfflineMode(true, gHeadlessAudioRate);
      Transport::Instance().SetPlaying(true);
      sNextTime = 0;
      sNextFrame = 0;
      sPhase = Phase::Frames;
      return;
   }

   if (sPhase == Phase::Running)
   {
      if (gOfflineRender.active || sOut->IsOfflineFinalizing())
         return;
      std::error_code ec;
      const auto size = std::filesystem::exists(job.out, ec) ? std::filesystem::file_size(job.out, ec) : 0;
      sStatus.statusText = sOut->RecordStatus();
      sStatus.width = sOut->GetOutputWidth();
      sStatus.height = sOut->GetOutputHeight();
      if (size == 0)
         return fail("E_RENDER", sStatus.statusText.empty() ? "no output file was written" : sStatus.statusText);
      if (!sOut->LastOfflineRenderOk())
         return fail("E_RENDER", sStatus.statusText.empty() ? "the encoder did not finish the file" : sStatus.statusText);
      // A size > 0 file can still have no index (the pre-fix finishWriting
      // race); never report ok for a file no player can open.
      if (!Headless::MovieHasIndex(job.out))
         return fail("E_RENDER", "the file was written but has no index (moov), so it cannot be played");
      sStatus.files.push_back(job.out);
      sPhase = Phase::Done;
      HeadlessFinish(window, sStatus, sWall);
      return;
   }

   if (sPhase == Phase::NodeRender)
   {
      const double budgetStart = glfwGetTime();
      while (sNextFrame < sNodeEndFrame)
      {
         // RecorderAppend drops a frame when its queue is full; a headless render
         // must not, so wait for the encoder to drain instead.
         if (sNodeRec != nullptr && Platform::RecorderPendingFrameCount(sNodeRec) > 6)
            return;
         ++frameId;
         Transport::Instance().SetOfflineVideoTime((double)sNextFrame / (double)job.fps);
         ApplyModulationAndPalette(frameId);
         ArrangeSeekVideoSampleSources(Transport::Instance().Beats());
         for (GraphNode& gn : gNodes)
            if (!gn.node->bypassed)
               gn.node->CookIfNeeded(frameId);
         if (sNextFrame >= sNodeStartFrame)
         {
            if (sTap->GetOutputWidth() <= 0 || sTap->GetOutputHeight() <= 0)
               return fail("E_RENDER", "the --node " + job.node + " produced no image");
            int rw = 0, rh = 0;
            std::vector<unsigned char> rgba;
            if (!ReadNodeImageRgba8(sTap, sTapOutput, job.codec != "prores4444", rw, rh, rgba))
               return fail("E_RENDER", "could not read the --node " + job.node + " image");
            if (job.sizeW > 0 && (rw != job.sizeW || rh != job.sizeH))
            {
               rgba = ResizeRgba8(rgba.data(), rw, rh, job.sizeW, job.sizeH);
               rw = job.sizeW;
               rh = job.sizeH;
            }
            // The encoder wants even sizes; crop the odd row/column.
            const int cw = rw & ~1, ch = rh & ~1;
            if (cw <= 0 || ch <= 0)
               return fail("E_RENDER", "the --node image is too small to encode");
            if (sNodeRec == nullptr)
            {
               std::string err;
               sNodeRec = Platform::RecorderStart(job.out, cw, ch, job.fps, err, std::string(), true, 0.0, 2,
                                                  job.codec == "prores4444");
               if (sNodeRec == nullptr)
                  return fail("E_RENDER", err.empty() ? "could not start the encoder" : err);
               Platform::RecorderSetInputIsBgra(sNodeRec, false);
               sNodeRecW = cw;
               sNodeRecH = ch;
            }
            else if (cw != sNodeRecW || ch != sNodeRecH)
               return fail("E_RENDER", "the --node image changed size mid-render (" + std::to_string(sNodeRecW) + "x" +
                                          std::to_string(sNodeRecH) + " -> " + std::to_string(cw) + "x" + std::to_string(ch) +
                                          "); pass --size to hold it fixed");
            // The recorder takes bottom-up rows; rgba is top-down.
            std::vector<unsigned char> frame = Platform::RecorderAcquireFrameBuffer(sNodeRec);
            frame.resize((size_t)cw * (size_t)ch * 4);
            for (int i = 0; i < ch; i++)
               std::memcpy(frame.data() + (size_t)i * cw * 4, rgba.data() + (size_t)(ch - 1 - i) * rw * 4, (size_t)cw * 4);
            if (!Platform::RecorderAppend(sNodeRec, std::move(frame), 1))
               return fail("E_RENDER", "the encoder refused a frame");
         }
         sNextFrame++;
         if (glfwGetTime() - budgetStart > 0.1)
            return;
      }
      Transport::Instance().SetOfflineMode(false);
      std::string err;
      int written = 0, dropped = 0;
      const bool stopped = Platform::RecorderStop(sNodeRec, err, &written, &dropped);
      sNodeRec = nullptr;
      std::error_code ec;
      const auto size = std::filesystem::exists(job.out, ec) ? std::filesystem::file_size(job.out, ec) : 0;
      if (!stopped || size == 0)
         return fail("E_RENDER", err.empty() ? "the encoder did not finish the file" : err);
      if (dropped > 0)
         return fail("E_RENDER", std::to_string(dropped) + " frame(s) were dropped by the encoder");
      if (!Headless::MovieHasIndex(job.out))
         return fail("E_RENDER", "the file was written but has no index (moov), so it cannot be played");
      sStatus.width = sNodeRecW;
      sStatus.height = sNodeRecH;
      sStatus.frames = written;
      sStatus.files.push_back(job.out);
      sPhase = Phase::Done;
      HeadlessFinish(window, sStatus, sWall);
      return;
   }

   if (sPhase == Phase::Frames)
   {
      const double budgetStart = glfwGetTime();
      while (sNextTime < sTimes.size())
      {
         const int target = (int)std::llround(sTimes[sNextTime] * (double)job.fps);
         if (sNextFrame > target)
            sNextFrame = target; // duplicate time: re-export what the last step cooked
         const bool needStep = sNextFrame <= target;
         if (needStep)
         {
            ++frameId;
            Transport::Instance().SetOfflineVideoTime((double)sNextFrame / (double)job.fps);
            ApplyModulationAndPalette(frameId);
            ArrangeSeekVideoSampleSources(Transport::Instance().Beats());
            for (GraphNode& gn : gNodes)
               if (!gn.node->bypassed)
                  gn.node->CookIfNeeded(frameId);
         }
         if (sNextFrame == target)
         {
            int w = sTap->GetOutputWidth(), h = sTap->GetOutputHeight();
            if (w <= 0 || h <= 0)
               return fail("E_RENDER", sOut != nullptr ? "the Output produced no image (is its input connected?)"
                                                       : "the --node " + job.node + " produced no image",
                           sOut != nullptr ? sOutIndex : -1);
            std::string path = job.out;
            std::vector<unsigned char> pixels;
            const bool sequence = job.mode == Headless::Mode::Frames;
            if (sequence || job.sizeW > 0)
            {
               if (sequence)
               {
                  char name[32];
                  std::snprintf(name, sizeof(name), "%06zu.png", sNextTime);
                  path = job.out + (job.out.back() == '/' ? "" : "/") + name;
               }
               else if (std::filesystem::path(job.out).extension() != ".png" && std::filesystem::path(job.out).extension() != ".PNG")
               {
                  char name[96];
                  std::snprintf(name, sizeof(name), "/frame_%05d.png", target);
                  path = job.out + (job.out.back() == '/' ? std::string(name + 1) : std::string(name));
               }
               int rw = 0, rh = 0;
               std::vector<unsigned char> rgba;
               if (!ReadNodeImageRgba8(sTap, sTapOutput, !job.alpha, rw, rh, rgba))
                  return fail("E_RENDER", "could not read " + path);
               std::vector<uint16_t> deep;
               if (sequence && job.depth == 16)
               {
                  if (!NodeImageIsFloat(sTap, sTapOutput))
                     return fail("E_UNSUPPORTED_DEPTH",
                                 "--depth 16 needs a float source: this image is 8 bits per channel, so a 16-bit PNG would only pad it; drop --depth or tap a node with a 16-bit float output",
                                 sOut != nullptr ? sOutIndex : -1);
                  int dw = 0, dh = 0;
                  if (!ReadNodeImageRgba16(sTap, sTapOutput, !job.alpha, dw, dh, deep))
                     return fail("E_RENDER", "could not read " + path);
                  if (job.sizeW > 0 && (dw != job.sizeW || dh != job.sizeH))
                     return fail("E_UNSUPPORTED_DEPTH", "--size with --depth 16 is not supported: render at the native size, or use --depth 8",
                                 sOut != nullptr ? sOutIndex : -1);
               }
               else if (job.sizeW > 0 && (rw != job.sizeW || rh != job.sizeH))
               {
                  rgba = ResizeRgba8(rgba.data(), rw, rh, job.sizeW, job.sizeH);
                  rw = job.sizeW;
                  rh = job.sizeH;
               }
               w = rw;
               h = rh;
               if (sequence)
               {
                  sPngWriter->Submit(path, rw, rh, rgba, std::move(deep), job.pngLevel >= 0 ? job.pngLevel : 1);
                  pixels = std::move(rgba);
               }
               else
               {
                  if (!WriteSrgbPng(path, rw, rh, rgba.data()))
                     return fail("E_RENDER", "could not write " + path);
                  pixels = std::move(rgba);
               }
            }
            else
            {
               if (std::filesystem::path(job.out).extension() != ".png" && std::filesystem::path(job.out).extension() != ".PNG")
               {
                  char name[96];
                  std::snprintf(name, sizeof(name), "/frame_%05d.png", target);
                  path = job.out + (job.out.back() == '/' ? std::string(name + 1) : std::string(name));
               }
               ExportImage(sTap, path, 90, &pixels, sTapOutput);
            }
            std::error_code ec;
            if (!sequence && !std::filesystem::exists(path, ec))
               return fail("E_RENDER", "could not write " + path);
            sStatus.files.push_back(path);
            {
               // What the frame looks like, in numbers, so a caller that
               // cannot open the PNG still knows a black or blown-out one.
               const ColorStats::FrameSummary fs = ColorStats::SummarizeRgba8(pixels.data(), w, h);
               char head[96];
               std::snprintf(head, sizeof(head), "{\"time\":%.6g,\"frame\":%d,\"file\":\"", sTimes[sNextTime], target);
               sFrameStats += std::string(sFrameStats.empty() ? "" : ",") + head + Headless::JsonEscape(path) + "\"," +
                              ColorStats::FrameSummaryJsonFields(fs) + "}";
               char when[32];
               std::snprintf(when, sizeof(when), "%.6g", sTimes[sNextTime]);
               if (fs.blackPercent >= 100.0)
                  sStatus.warnings.push_back({ "W_BLACK_FRAME", std::string("the frame at ") + when + " s is entirely black",
                                               0, sOutIndex, "check that something visible reaches the Output at that time" });
               if (!job.contactSheet.empty())
               {
                  char label[32];
                  std::snprintf(label, sizeof(label), "%.2fs", sTimes[sNextTime]);
                  sSheet.Add(pixels.data(), w, h, job.mode != Headless::Mode::Frames, label);
               }
            }
            sStatus.width = w;
            sStatus.height = h;
            sStatus.frames = (long long)sStatus.files.size();
            sNextTime++;
         }
         else
            sNextFrame++;
         if (glfwGetTime() - budgetStart > 0.1)
            return;
      }
      Transport::Instance().SetOfflineMode(false);
      if (job.mode == Headless::Mode::Frame && sPass + 1 < job.repeat)
      {
         // Another pass: reload the patch in this same process, so static state the first
         // load or render left behind is still alive when the second one runs.
         sPass++;
         sTicks = 0;
         sNextTime = 0;
         sNextFrame = 0;
         sPhase = Phase::Warm;
         LoadPatchFrom(job.patch);
         return;
      }
      if (sPngWriter != nullptr)
      {
         const std::vector<std::string> failed = sPngWriter->Finish();
         sPngWriter.reset();
         if (!failed.empty())
            return fail("E_RENDER", "could not write " + failed.front() + (failed.size() > 1 ? " (and " + std::to_string(failed.size() - 1) + " more)" : std::string()));
      }
      if (job.mode == Headless::Mode::Frames)
      {
         // What a film engine needs to read the sequence back: straight alpha, sRGB, fps, count.
         char meta[320];
         std::snprintf(meta, sizeof(meta),
                       "{\"fps\":%d,\"size\":[%d,%d],\"count\":%zu,\"start\":%.6g,\"alpha\":%s,"
                       "\"premultiplied\":false,\"color_space\":\"srgb\",\"depth\":%d,\"pattern\":\"%%06d.png\"}\n",
                       job.fps, sStatus.width, sStatus.height, sTimes.size(), job.start, job.alpha ? "true" : "false", job.depth);
         const std::string metaPath = job.out + (job.out.back() == '/' ? "" : "/") + "frames.json";
         if (FILE* f = std::fopen(metaPath.c_str(), "wb"))
         {
            std::fwrite(meta, 1, std::strlen(meta), f);
            std::fclose(f);
            sStatus.files.push_back(metaPath);
         }
      }
      if (job.repeat > 1)
         sStatus.extraJson.push_back("\"passes\":" + std::to_string(job.repeat));
      sStatus.extraJson.push_back("\"frame_stats\":[" + sFrameStats + "]");
      if (!job.contactSheet.empty())
      {
         const ContactSheet::Image sheet = sSheet.Build();
         std::error_code ec;
         if (!std::filesystem::path(job.contactSheet).parent_path().empty())
            std::filesystem::create_directories(std::filesystem::path(job.contactSheet).parent_path(), ec);
         if (sheet.width <= 0 || !WriteSrgbPng(job.contactSheet, sheet.width, sheet.height, sheet.rgba.data()))
            return fail("E_RENDER", "could not write " + job.contactSheet);
         sStatus.files.push_back(job.contactSheet);
         sStatus.extraJson.push_back("\"contact_sheet\":{\"file\":\"" + Headless::JsonEscape(job.contactSheet) +
                                     "\",\"size\":[" + std::to_string(sheet.width) + "," + std::to_string(sheet.height) +
                                     "],\"cells\":" + std::to_string(sSheet.Count()) +
                                     ",\"cell_width\":" + std::to_string(ContactSheet::kCellWidth) + "}");
      }
      sPhase = Phase::Done;
      HeadlessFinish(window, sStatus, sWall);
      return;
   }

   if (sPhase == Phase::Audio)
   {
      static float sL[kAudioMaxBlockFrames];
      static float sR[kAudioMaxBlockFrames];
      static float* sChannels[2] = { sL, sR };
      static std::vector<float> sInterleaved;

      const double rate = gHeadlessAudioRate;
      const double budgetStart = glfwGetTime();
      const int blockCap = OfflineAudioBlockFrames();
      while (sAudioDone < sAudioTotal)
      {
         // One video frame of graph time, then the audio that belongs to it:
         // the order the offline render pump runs in, so modulators and
         // anything else cooked per frame move exactly as they do in a take.
         ++frameId;
         Transport::Instance().SetOfflineVideoTime(job.start + (double)sAudioStep / (double)job.fps);
         ApplyModulationAndPalette(frameId);
         ArrangeSeekVideoSampleSources(Transport::Instance().Beats());
         for (GraphNode& gn : gNodes)
            if (!gn.node->bypassed)
               gn.node->CookIfNeeded(frameId);
         sAudioStep++;

         const long long target = std::min(sAudioTotal, (long long)std::llround((double)sAudioStep * rate / (double)job.fps));
         while (sAudioDone < target)
         {
            const int n = (int)std::min<long long>(blockCap, target - sAudioDone);
            AudioBuffer buf;
            buf.channels = sChannels;
            buf.numChannels = 2;
            buf.numFrames = n;
            // Film events land at their own sample, inside this block, not on a video frame.
            while (sSchedNext < sSched.size() && sSched[sSchedNext].sample < sAudioDone + n)
            {
               const SchedEvent& se = sSched[sSchedNext++];
               NoteEvent e = se.ev;
               e.frameOffset = (int)std::max<long long>(0, se.sample - sAudioDone);
               se.queue->Push(e);
            }
            AudioEngine::Instance().ProcessOffline(buf);

            sInterleaved.assign((size_t)n * 2, 0.0f);
            if (sSinkRing != nullptr)
            {
               // One sink: what RunTopology just wrote into its capture ring,
               // not the sum of every sink that the device buffer holds.
               sSinkRing->Read(sInterleaved.data(), n * 2);
               for (int i = 0; i < n; i++)
               {
                  sL[i] = sInterleaved[(size_t)i * 2 + 0];
                  sR[i] = sInterleaved[(size_t)i * 2 + 1];
               }
            }
            else
               for (int i = 0; i < n; i++)
               {
                  sInterleaved[(size_t)i * 2 + 0] = sL[i];
                  sInterleaved[(size_t)i * 2 + 1] = sR[i];
               }
            sAnalyzer->Push(sL, sR, n);
            if (sSummaryWav.IsOpen())
               sSummaryWav.Append(sInterleaved.data(), n);
            sAudioDone += n;
         }
         if (glfwGetTime() - budgetStart > 0.1)
            return;
      }

      if (sSinkRing != nullptr)
         sSinkRing->enabled.store(false, std::memory_order_relaxed);
      Transport::Instance().SetOfflineMode(false);
      Transport::Instance().SetPlaying(false);
      AudioEngine::Instance().SetOfflineTaps(nullptr);
      for (size_t i = 0; i < sTaps.size(); i++)
      {
         AudioFileWriter w;
         if (!w.Open(sStemInfo[i].file, gHeadlessAudioRate, 2, AudioFileWriter::Format::Wav))
            return fail("E_RENDER", "could not create " + sStemInfo[i].file);
         const std::vector<float>& L = sTaps[i].left;
         const std::vector<float>& R = sTaps[i].right;
         std::vector<float> inter(L.size() * 2);
         for (size_t k = 0; k < L.size(); k++)
         {
            inter[k * 2] = L[k];
            inter[k * 2 + 1] = R[k];
         }
         w.Append(inter.data(), (int)L.size());
         w.Close();
         sStatus.files.push_back(sStemInfo[i].file);
      }
      if (!sNotesJson.empty())
         sStatus.extraJson.push_back(sNotesJson);

      const AudioSummary::Result res = sAnalyzer->Finish();
      char range[96];
      std::snprintf(range, sizeof(range), "\"start\":%.6g,\"duration\":%.6g,", job.start, sAudioSeconds);
      const std::string context = std::string(range) + sAudioSource;
      {
         // The file carries everything; the status line repeats the headline
         // numbers so one read of stdout is enough to judge the take.
         std::ofstream f(job.out, std::ios::binary | std::ios::trunc);
         f << "{\"patch\":\"" << Headless::JsonEscape(job.patch) << "\"," << context << ","
           << AudioSummary::ToJson(res, true).substr(1) << "\n";
         f.close();
         if (!f)
            return fail("E_RENDER", "could not write " + job.out);
      }
      sStatus.files.push_back(job.out);
      if (sSummaryWav.IsOpen())
      {
         sSummaryWav.Close();
         sStatus.files.push_back(job.wavPath);
      }
      sStatus.audioSampleRate = rate;
      sStatus.audioFrames = sAudioTotal;
      sStatus.extraJson.push_back(context);
      sStatus.extraJson.push_back("\"audio_summary\":" + AudioSummary::ToJson(res, false));

      char num[64];
      if (res.clippedFrames > 0)
      {
         std::snprintf(num, sizeof(num), "%+.2f dBFS, %.3f%% of frames", AudioSummary::Db(res.samplePeak), res.clippedPercent);
         sStatus.warnings.push_back({ "W_CLIPPING", std::string("the signal reaches or passes full scale (sample peak ") + num +
                                                       "); a file written from it will distort",
                                      0, -1, "lower the level feeding the Audio Out / Output" });
      }
      if (!std::isfinite(res.integratedLufs))
         sStatus.warnings.push_back({ "W_SILENT", "the whole range is silent (nothing above -70 LUFS)", 0, -1,
                                      "check that a note or audio source is playing in this range" });
      sPhase = Phase::Done;
      HeadlessFinish(window, sStatus, sWall);
   }
}
}
