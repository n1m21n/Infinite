// Infinite-Turbo 0.45: RPC tools behind the MCP bridge (Infinite-Turbo.exe
// --mcp), the live validation hints drawn on nodes and the auto layout.
//
// Included by main.cpp inside its anonymous namespace, right before
// HandleRpcCommand, so everything here can use the editor's own helpers
// (SpawnNode, ConnectNodes, BuildPatchData, ApplyPatchData, gLinks, ...).
// Every function runs on the main thread: RPC requests are drained once per
// frame (RemoteControl::DrainPending) before anything is drawn.

// ---------------------------------------------------------------- params ---
// Modulation and expressions address a parameter by its index in the node's
// drawn controls (ParamRef::paramIndex), which only exists once the node has
// drawn. The cache keeps every drawn param per node (off-screen nodes draw
// every 30 frames, so they fill in too) so RPC calls can name a param
// ("amount", "Speed") instead of guessing its index.
struct RpcParamInfo
{
   std::string name;
   int paramIndex = 0;
   float minValue = 0.0f, maxValue = 1.0f;
   float* value = nullptr;
   const INode* owner = nullptr;
};
std::map<int, std::map<int, RpcParamInfo>> gRpcParamCache;

void RpcRefreshParamCache()
{
   // Drop nodes that are gone or were replaced (an index reused after a load,
   // an undo or a batch rollback): their entries point into a freed node.
   for (auto it = gRpcParamCache.begin(); it != gRpcParamCache.end();)
   {
      GraphNode* gn = FindNodeByIndex(it->first);
      bool stale = gn == nullptr;
      for (const auto& kv : it->second)
         stale = stale || kv.second.owner != gn->node.get();
      it = stale ? gRpcParamCache.erase(it) : std::next(it);
   }
   for (const ParamRef& ref : Modulation::Instance().FrameParams())
   {
      GraphNode* gn = FindNodeByIndex(ref.nodeIndex);
      if (gn == nullptr)
         continue;
      RpcParamInfo& p = gRpcParamCache[ref.nodeIndex][ref.paramIndex];
      p.name = ref.name;
      p.paramIndex = ref.paramIndex;
      p.minValue = ref.minValue;
      p.maxValue = ref.maxValue;
      p.value = ref.value;
      p.owner = gn->node.get();
   }
}

std::string RpcNorm(const std::string& s)
{
   // Turbo 0.47: Latin-1 accents fold to the base letter (UTF-8 C3 xx), so
   // "ijexa" finds "Ijexá" and "bjork" finds "Björk".
   static const char kFold[64 + 1] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
   std::string out;
   for (size_t i = 0; i < s.size(); i++)
   {
      unsigned char c = (unsigned char)s[i];
      if (c == 0xC3 && i + 1 < s.size() && (unsigned char)s[i + 1] >= 0x80 && (unsigned char)s[i + 1] <= 0xBF)
      {
         c = (unsigned char)kFold[(unsigned char)s[i + 1] - 0x80];
         i++;
      }
      if (c != ' ' && c != '_' && c != '-' && c != '.')
         out += (char)std::tolower(c);
   }
   return out;
}

// `param` is a name (case, spaces and underscores ignored) or an index.
const RpcParamInfo* RpcFindParam(const GraphNode& gn, const nlohmann::json& param)
{
   auto it = gRpcParamCache.find(gn.index);
   if (it == gRpcParamCache.end() || it->second.empty() || it->second.begin()->second.owner != gn.node.get())
      return nullptr; // not drawn yet, or the cache still describes a replaced node
   if (param.is_number_integer())
   {
      auto p = it->second.find(param.get<int>());
      return p == it->second.end() ? nullptr : &p->second;
   }
   if (!param.is_string())
      return nullptr;
   const std::string want = RpcNorm(param.get<std::string>());
   for (const auto& kv : it->second)
      if (RpcNorm(kv.second.name) == want)
         return &kv.second;
   return nullptr;
}

// Modulation / expression requests for a node that has not drawn yet (made
// in the same batch, or off screen) wait here and land within a few frames.
struct RpcPendingBind
{
   int kind = 0; // 0 modulate, 1 expression, 2 clear
   int dst = -1;
   nlohmann::json param;
   int src = -1, srcOutput = 0, polarity = 0;
   float depth = 1.0f;
   bool hasRange = false;
   float inMin = 0.0f, inMax = 1.0f, outMin = 0.0f, outMax = 1.0f;
   std::string expr;
   uint64_t dstUid = 0; // the node it was meant for (an index can be reused)
   int framesLeft = 240;
};
std::vector<RpcPendingBind> gRpcPendingBinds;

// 1 = applied, 0 = not drawn yet (retry), -1 = error.
int RpcApplyBind(const RpcPendingBind& b, std::string& err)
{
   GraphNode* dst = FindNodeByIndex(b.dst);
   if (dst == nullptr || (b.dstUid != 0 && dst->uid != b.dstUid))
   {
      err = "unknown node index " + std::to_string(b.dst);
      return -1;
   }
   if (b.kind == 0 && FindNodeByIndex(b.src) == nullptr)
   {
      err = "unknown source node index " + std::to_string(b.src);
      return -1;
   }
   const RpcParamInfo* p = RpcFindParam(*dst, b.param);
   if (p == nullptr)
   {
      auto cached = gRpcParamCache.find(b.dst);
      if (cached != gRpcParamCache.end() && !cached->second.empty() &&
          cached->second.begin()->second.owner == dst->node.get())
      {
         err = "node " + std::to_string(b.dst) + " has no param '" +
               (b.param.is_string() ? b.param.get<std::string>() : b.param.dump()) + "' (see explain)";
         return -1;
      }
      return 0;
   }
   PushUndoCheckpoint();
   Modulation& mod = Modulation::Instance();
   if (b.kind == 0)
   {
      Modulation::Source s;
      s.nodeIndex = b.src;
      s.outputIndex = b.srcOutput;
      s.polarity = b.polarity;
      s.depth = b.depth;
      s.centre = p->value != nullptr ? *p->value : 0.0f;
      if (b.hasRange)
      {
         s.inMin = b.inMin; s.inMax = b.inMax; s.outMin = b.outMin; s.outMax = b.outMax;
      }
      mod.RestoreLink(b.dst, p->paramIndex, s);
   }
   else if (b.kind == 1)
   {
      if (b.expr.empty())
         mod.ClearExpression(b.dst, p->paramIndex);
      else
         mod.SetExpression(b.dst, p->paramIndex, b.expr);
   }
   else
   {
      mod.Unbind(b.dst, p->paramIndex);
      mod.ClearExpression(b.dst, p->paramIndex);
   }
   return 1;
}

void RpcPumpPendingBinds()
{
   for (size_t i = 0; i < gRpcPendingBinds.size();)
   {
      RpcPendingBind& b = gRpcPendingBinds[i];
      std::string err;
      const int r = RpcApplyBind(b, err);
      if (r == 0 && --b.framesLeft > 0)
      {
         i++;
         continue;
      }
      if (r != 1)
         RuntimeLog::Write("rpc: pending %s on node %d dropped: %s", b.kind == 1 ? "expression" : "modulation",
                           b.dst, err.empty() ? "node never drew" : err.c_str());
      gRpcPendingBinds.erase(gRpcPendingBinds.begin() + (long)i);
   }
}

// ----------------------------------------------------------- pins/kinds ---
std::string RpcSlotKind(const GraphNode& gn, int slot)
{
   INode* n = gn.node.get();
   if (dynamic_cast<Render3DNode*>(n) != nullptr)
   {
      if (slot < Render3DNode::kSlots) return "geometry";
      if (slot == Render3DNode::kSlots) return "camera";
      if (slot == Render3DNode::kEnvSlot) return "environment";
      return "light";
   }
   if (n->GeometryInputSlot(slot) != nullptr) return "geometry";
   if (dynamic_cast<SetColorNode*>(n) != nullptr)
      return slot == 1 ? "image" : (slot == 2 ? "palette" : "geometry");
   if (dynamic_cast<AudioAnalyzeNode*>(n) != nullptr) return "audio";
   if (n->ModulatorInputSlot(slot) != nullptr) return "modulator";
   if (n->AudioInputSlot(slot) != nullptr) return "audio";
   if (n->NoteInputSlot(slot) != nullptr) return "note";
   return "image";
}

std::string RpcSlotLabel(const GraphNode& gn, int slot)
{
   if (const char* named = gn.node->InputLabel(slot))
      return named;
   const int inputs = InputCountFor(gn);
   if (inputs == 1)
      return "in";
   return std::string(1, (char)('A' + slot));
}

std::string RpcOutputKind(INode* n, int out)
{
   if (ModulatorForOutput(n, out) != nullptr) return "modulator";
   if (IsAudioOutput(n, out)) return "audio";
   if (dynamic_cast<INoteSource*>(n) != nullptr) return "note";
   if (dynamic_cast<IGeometrySource*>(n) != nullptr) return "geometry";
   if (dynamic_cast<CameraNode*>(n) != nullptr) return "camera";
   if (dynamic_cast<LightNode*>(n) != nullptr) return "light";
   if (dynamic_cast<EnvironmentNode*>(n) != nullptr) return "environment";
   if (dynamic_cast<IPaletteSource*>(n) != nullptr) return "palette";
   return "image";
}

// A slot given as a number or as its label ("B", "geo A", "notes").
int RpcResolveSlot(const GraphNode& gn, const nlohmann::json& v)
{
   if (v.is_number_integer())
      return v.get<int>();
   if (!v.is_string())
      return -1;
   const std::string want = RpcNorm(v.get<std::string>());
   const int inputs = InputCountFor(gn);
   for (int s = 0; s < inputs; s++)
      if (RpcNorm(RpcSlotLabel(gn, s)) == want)
         return s;
   return -1;
}

int RpcResolveOutput(GraphNode& gn, const nlohmann::json& v)
{
   if (v.is_number_integer())
      return v.get<int>();
   if (!v.is_string())
      return 0;
   const std::string want = RpcNorm(v.get<std::string>());
   for (int o = 0; o < gn.node->OutputCount(); o++)
      if (const char* l = gn.node->OutputLabel(o); l != nullptr && RpcNorm(l) == want)
         return o;
   return -1;
}

nlohmann::json RpcDescribeNodeShape(GraphNode& gn, bool withParams)
{
   using json = nlohmann::json;
   json out;
   out["type"] = gn.typeName;
   out["category"] = gn.category;
   json inputs = json::array();
   const int count = InputCountFor(gn);
   for (int s = 0; s < count; s++)
      inputs.push_back({ { "slot", s }, { "label", RpcSlotLabel(gn, s) }, { "kind", RpcSlotKind(gn, s) } });
   out["inputs"] = inputs;
   json outputs = json::array();
   const bool isSink = dynamic_cast<OutputNode*>(gn.node.get()) != nullptr;
   if (!isSink)
      for (int o = 0; o < std::max(1, gn.node->OutputCount()); o++)
      {
         const char* l = gn.node->OutputLabel(o);
         outputs.push_back({ { "index", o }, { "label", l ? l : "out" }, { "kind", RpcOutputKind(gn.node.get(), o) } });
      }
   out["outputs"] = outputs;
   if (withParams)
   {
      std::vector<std::pair<std::string, std::string>> raw;
      Patch::SaveParams(gn.node.get(), raw);
      json params = json::array();
      for (const auto& kv : raw)
      {
         const size_t sp = kv.first.find(' ');
         const std::string letter = sp == std::string::npos ? "" : kv.first.substr(0, sp);
         const std::string key = sp == std::string::npos ? kv.first : kv.first.substr(sp + 1);
         const char* type = letter == "f" ? "float" : letter == "i" ? "int" : letter == "b" ? "bool"
                          : letter == "c" ? "color" : letter == "s" ? "text" : "?";
         params.push_back({ { "key", key }, { "type", type }, { "value", kv.second } });
      }
      out["params"] = params;
   }
   return out;
}

// ------------------------------------------------------------ live hints ---
// The validation pass reruns 300 ms after the last undoable edit (or a patch
// load), so a drag or a burst of edits costs one pass, not one per frame.
// Only hints that say something while building are shown: a half-wired
// merge (Blend, two-input filters), an Output with nothing in it, and an
// image loop with no Feedback node to break it.
struct RpcIssue
{
   std::string code, message, hint;
};
std::unordered_map<int, std::vector<RpcIssue>> gLiveIssues;
unsigned gLiveIssueDoneSerial = 0;

void RefreshLiveIssues(bool force = false)
{
   if (!force && (gLiveIssueDoneSerial == gLiveIssueSerial || glfwGetTime() - gLiveIssueEditTime < 0.3))
      return;
   gLiveIssueDoneSerial = gLiveIssueSerial;
   gLiveIssues.clear();

   // Image cables in the live graph (gLinks is rebuilt every frame).
   std::set<std::pair<int, int>> connected;          // (dst node, slot) - any kind
   std::map<int, std::vector<int>> imageEdges;        // src -> dsts, image cables only
   for (const LinkInfo& link : gLinks)
   {
      if (!GraphNode::IsInputPin(link.dstPin))
         continue;
      const int dstIndex = GraphNode::NodeIndexFromPin(link.dstPin);
      const int slot = GraphNode::InputSlotFromPin(link.dstPin);
      connected.insert({ dstIndex, slot });
      GraphNode* dst = FindNodeByIndex(dstIndex);
      if (dst != nullptr && RpcSlotKind(*dst, slot) == "image" &&
          dynamic_cast<FeedbackNode*>(dst->node.get()) == nullptr)
         imageEdges[GraphNode::NodeIndexFromPin(link.srcPin)].push_back(dstIndex);
   }

   for (GraphNode& gn : gNodes)
   {
      INode* n = gn.node.get();
      if (dynamic_cast<OutputNode*>(n) != nullptr && connected.count({ gn.index, 0 }) == 0)
         gLiveIssues[gn.index].push_back({ "W_OUTPUT_EMPTY", "this Output has nothing connected",
                                           "wire an image into its input, or delete it" });
      int mergeInputs = 0;
      if (dynamic_cast<BlendNode*>(n) != nullptr)
         mergeInputs = 2;
      else if (auto* filter = dynamic_cast<FilterNode*>(n); filter != nullptr && filter->Def().inputs >= 2)
         mergeInputs = filter->Def().inputs;
      if (mergeInputs >= 2)
      {
         int wired = 0;
         std::string open;
         for (int s = 0; s < mergeInputs; s++)
         {
            if (connected.count({ gn.index, s }) != 0)
               wired++;
            else
               open += (open.empty() ? "" : ", ") + RpcSlotLabel(gn, s);
         }
         if (wired > 0 && wired < mergeInputs)
            gLiveIssues[gn.index].push_back({ "W_OPEN_INPUT", "input " + open + " is not connected",
                                              "this node mixes its inputs: wire every one of them" });
      }
   }

   // Image cycles (Feedback inputs excluded: that is what breaks a loop).
   std::map<int, int> state; // 0 new, 1 on stack, 2 done
   std::vector<int> stack;
   std::function<void(int)> visit = [&](int v)
   {
      state[v] = 1;
      stack.push_back(v);
      for (int w : imageEdges[v])
      {
         if (state[w] == 1)
         {
            for (auto it = std::find(stack.begin(), stack.end(), w); it != stack.end(); ++it)
            {
               auto& list = gLiveIssues[*it];
               bool already = false;
               for (const RpcIssue& is : list)
                  already = already || is.code == "W_IMAGE_CYCLE";
               if (!already)
                  list.push_back({ "W_IMAGE_CYCLE", "image loop without a Feedback node",
                                   "insert a Feedback node in the loop (it delays one frame)" });
            }
         }
         else if (state[w] == 0)
            visit(w);
      }
      stack.pop_back();
      state[v] = 2;
   };
   for (GraphNode& gn : gNodes)
      if (state[gn.index] == 0)
         visit(gn.index);
}

std::string LiveIssueTooltip(int nodeIndex)
{
   auto it = gLiveIssues.find(nodeIndex);
   if (it == gLiveIssues.end())
      return std::string();
   std::string tip;
   for (const RpcIssue& w : it->second)
   {
      if (!tip.empty())
         tip += "\n";
      tip += w.message;
      if (!w.hint.empty())
         tip += "\n  -> " + w.hint;
   }
   return tip;
}

// ------------------------------------------------------------ auto layout ---
// Layered left-to-right layout: each node goes in the column of its longest
// cable path from a source, columns as wide as their widest node, rows as
// tall as each node, ordered by where their inputs sit. Sizes come from the
// drawn nodes, so it runs once they have drawn (see gAutoLayoutPending).
// (gAutoLayoutPending / gAutoLayoutWait / gAutoLayoutFit live near the top of main.cpp.)

void RunAutoLayout(const std::vector<int>& which)
{
   if (which.empty() || gEditor == nullptr)
      return;
   std::set<int> place(which.begin(), which.end());
   ed::EditorContext* prev = ed::GetCurrentEditor();
   ed::SetCurrentEditor(gEditor);

   std::map<int, std::vector<int>> preds;
   for (const LinkInfo& link : gLinks)
   {
      const int s = GraphNode::NodeIndexFromPin(link.srcPin), d = GraphNode::NodeIndexFromPin(link.dstPin);
      if (s != d && place.count(s) && place.count(d))
         preds[d].push_back(s);
   }
   for (const auto& kv : Modulation::Instance().Links())
   {
      const int d = kv.first.first, s = kv.second.nodeIndex;
      if (s != d && place.count(s) && place.count(d))
         preds[d].push_back(s);
   }
   // Longest path, bounded (feedback loops would otherwise never settle).
   std::map<int, int> depth;
   for (int v : which)
      depth[v] = 0;
   for (size_t pass = 0; pass < which.size() + 1; pass++)
   {
      bool changed = false;
      for (int v : which)
         for (int p : preds[v])
            if (depth[p] + 1 > depth[v] && depth[p] + 1 <= (int)which.size())
            {
               depth[v] = depth[p] + 1;
               changed = true;
            }
      if (!changed)
         break;
   }

   // Origin: at (0,0) for a whole-graph layout, else right of what stays put.
   float originX = 0.0f, originY = 0.0f;
   bool haveFixed = false;
   for (GraphNode& gn : gNodes)
   {
      if (place.count(gn.index))
         continue;
      const ImVec2 p = ed::GetNodePosition(gn.NodeId());
      const ImVec2 sz = ed::GetNodeSize(gn.NodeId());
      if (!haveFixed)
      {
         originX = p.x + sz.x;
         originY = p.y;
         haveFixed = true;
      }
      originX = std::max(originX, p.x + sz.x);
      originY = std::min(originY, p.y);
   }
   if (haveFixed)
      originX += 160.0f;

   std::map<int, std::vector<int>> columns;
   for (int v : which)
      columns[depth[v]].push_back(v);
   std::map<int, float> rowCentre;
   float x = originX;
   for (auto& col : columns)
   {
      std::vector<int>& nodes = col.second;
      // Barycentre of the inputs already placed, else creation order.
      auto key = [&](int v)
      {
         float sum = 0.0f;
         int n = 0;
         for (int p : preds[v])
            if (rowCentre.count(p))
            {
               sum += rowCentre[p];
               n++;
            }
         return n > 0 ? sum / (float)n : 1e9f + (float)v;
      };
      std::stable_sort(nodes.begin(), nodes.end(), [&](int a, int b) { return key(a) < key(b); });
      float width = 0.0f, y = originY;
      for (int v : nodes)
      {
         GraphNode* gn = FindNodeByIndex(v);
         if (gn == nullptr)
            continue;
         ImVec2 sz = ed::GetNodeSize(gn->NodeId());
         sz.x = sz.x > 1.0f ? sz.x : 240.0f; // canvas units
         sz.y = sz.y > 1.0f ? sz.y : 120.0f;
         ed::SetNodePosition(gn->NodeId(), ImVec2(x, y));
         gn->spawnX = gn->liveX = x; // spawn too, so a later needsPosition re-place keeps it
         gn->spawnY = gn->liveY = y;
         rowCentre[v] = y + sz.y * 0.5f;
         y += sz.y + 48.0f;
         width = std::max(width, sz.x);
      }
      x += width + 110.0f;
   }
   ed::SetCurrentEditor(prev);
}

// Called once per frame at the top of the main loop.
void PumpAutoLayout()
{
   if (gAutoLayoutPending.empty())
      return;
   if (gAutoLayoutWait > 0)
   {
      gAutoLayoutWait--;
      return;
   }
   std::vector<int> nodes;
   for (int v : gAutoLayoutPending)
      if (FindNodeByIndex(v) != nullptr)
         nodes.push_back(v);
   // Turbo 0.48 (after upstream 4c25bfa): a big patch can take more than the
   // first few frames before every node has a measured size; wait for them
   // (bounded), then lay out with what there is (RunAutoLayout falls back to a
   // default size for anything still unmeasured, e.g. a culled node).
   static int sExtraFrames = 0;
   if (gEditor != nullptr && sExtraFrames < 30)
   {
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      bool allMeasured = true;
      for (int v : nodes)
      {
         const ImVec2 sz = ed::GetNodeSize(FindNodeByIndex(v)->NodeId());
         if (!(sz.x > 1.0f && sz.x < 20000.0f && sz.y > 1.0f && sz.y < 20000.0f))
         {
            allMeasured = false;
            break;
         }
      }
      ed::SetCurrentEditor(prev);
      if (!allMeasured)
      {
         sExtraFrames++;
         return;
      }
   }
   sExtraFrames = 0;
   gAutoLayoutPending.clear();
   RunAutoLayout(nodes);
   if (gAutoLayoutFit)
      gRequestFitView = true;
   gAutoLayoutFit = false;
}

// ------------------------------------------------------------- validation ---
nlohmann::json ValidatePatchData(const Patch::Data& data)
{
   using json = nlohmann::json;
   json errors = json::array(), warnings = json::array();
   std::map<int, std::unique_ptr<GraphNode>> probes; // saved index -> throwaway node
   std::set<int> seen;
   for (const Patch::NodeRecord& rec : data.nodes)
   {
      const std::string where = "node " + std::to_string(rec.index) + " (" + rec.typeName + ")";
      if (!seen.insert(rec.index).second)
         errors.push_back(where + ": duplicate node index");
      const std::string canonical = NodeFactory::CanonicalName(rec.typeName);
      INode* made = NodeFactory::Instance().MakeNode(canonical);
      if (made == nullptr)
      {
         errors.push_back(where + ": unknown node type");
         continue;
      }
      auto probe = std::make_unique<GraphNode>();
      probe->node.reset(made);
      probe->typeName = canonical;
      probe->index = rec.index;
      std::vector<std::pair<std::string, std::string>> raw;
      Patch::SaveParams(made, raw);
      std::set<std::string> keys;
      for (const auto& kv : raw)
         keys.insert(kv.first);
      for (const auto& kv : rec.params)
         if (!keys.count(kv.first))
            warnings.push_back(where + ": unknown param '" + kv.first + "' (ignored on load)");
      probes[rec.index] = std::move(probe);
   }
   auto checkCables = [&](const std::vector<Patch::CableRecord>& list, const char* tag, const char* kind)
   {
      for (const Patch::CableRecord& c : list)
      {
         const std::string where = std::string(tag) + " " + std::to_string(c.dstIndex) + " " +
                                   std::to_string(c.dstSlot) + " " + std::to_string(c.srcIndex);
         auto dst = probes.find(c.dstIndex);
         auto src = probes.find(c.srcIndex);
         if (dst == probes.end() || src == probes.end())
         {
            errors.push_back(where + ": names a node that is not in the patch");
            continue;
         }
         const int slots = InputCountFor(*dst->second);
         if (c.dstSlot < 0 || c.dstSlot >= slots)
         {
            errors.push_back(where + ": " + dst->second->typeName + " has " + std::to_string(slots) + " input(s)");
            continue;
         }
         const std::string slotKind = RpcSlotKind(*dst->second, c.dstSlot);
         const std::string want = kind;
         const bool ok = want == "image" ? (slotKind == "image")
                       : want == "geometry" ? (slotKind == "geometry" || slotKind == "camera" || slotKind == "light" ||
                                               slotKind == "environment" || slotKind == "palette")
                       : want == "audio" ? (slotKind == "audio" || slotKind == "note")
                       : (slotKind == "note" || slotKind == "audio");
         if (!ok)
            warnings.push_back(where + ": slot " + std::to_string(c.dstSlot) + " of " + dst->second->typeName +
                               " is a " + slotKind + " input, not " + want);
         if (c.srcOutput < 0 || c.srcOutput >= std::max(1, src->second->node->OutputCount()))
            warnings.push_back(where + ": " + src->second->typeName + " has no output " + std::to_string(c.srcOutput));
      }
   };
   checkCables(data.cables, "cable", "image");
   checkCables(data.geometry, "geo", "geometry");
   checkCables(data.audio, "aud", "audio");
   checkCables(data.notes, "note", "note");
   for (const Patch::ModRecord& m : data.modulation)
      if (!probes.count(m.dstIndex) || !probes.count(m.srcIndex))
         errors.push_back("mod " + std::to_string(m.dstIndex) + " " + std::to_string(m.dstParam) + " " +
                          std::to_string(m.srcIndex) + ": names a node that is not in the patch");
   int noPos = 0;
   for (const Patch::NodeRecord& rec : data.nodes)
      if (!rec.hasPos)
         noPos++;
   json out;
   out["ok"] = errors.empty();
   out["errors"] = errors;
   out["warnings"] = warnings;
   out["nodes"] = (int)data.nodes.size();
   out["cables"] = (int)(data.cables.size() + data.geometry.size() + data.audio.size() + data.notes.size());
   out["auto_layout_nodes"] = noPos;
   return out;
}

// ------------------------------------------------------------------- explain
nlohmann::json ExplainNode(GraphNode& gn)
{
   using json = nlohmann::json;
   json out = RpcDescribeNodeShape(gn, false);
   out["index"] = gn.index;
   out["title"] = NodeTitle(gn);
   out["bypassed"] = gn.node->bypassed;
   out["x"] = gn.liveX;
   out["y"] = gn.liveY;
   // What feeds each input (from the cables drawn last frame).
   for (const LinkInfo& link : gLinks)
   {
      if (GraphNode::NodeIndexFromPin(link.dstPin) != gn.index || !GraphNode::IsInputPin(link.dstPin))
         continue;
      const int slot = GraphNode::InputSlotFromPin(link.dstPin);
      if (slot >= 0 && slot < (int)out["inputs"].size())
         out["inputs"][(size_t)slot]["from"] = { { "node", GraphNode::NodeIndexFromPin(link.srcPin) },
                                                 { "output", GraphNode::IsOutputPin(link.srcPin)
                                                                ? GraphNode::OutputIndexFromPin(link.srcPin) : 0 } };
   }
   // Drawn (modulatable) params: name, range, value, what drives them.
   json params = json::array();
   Modulation& mod = Modulation::Instance();
   auto it = gRpcParamCache.find(gn.index);
   if (it != gRpcParamCache.end() && !it->second.empty() && it->second.begin()->second.owner == gn.node.get())
      for (const auto& kv : it->second)
      {
         const RpcParamInfo& p = kv.second;
         json e = { { "param", p.paramIndex }, { "name", p.name }, { "min", p.minValue }, { "max", p.maxValue } };
         if (p.value != nullptr)
            e["value"] = *p.value;
         const Modulation::Source s = mod.ModulatorFor(gn.index, p.paramIndex);
         if (s.nodeIndex >= 0)
            e["modulated_by"] = { { "node", s.nodeIndex }, { "output", s.outputIndex },
                                  { "polarity", s.polarity == 0 ? "absolute" : "bipolar" }, { "depth", s.depth } };
         if (const std::string* ex = mod.ExpressionFor(gn.index, p.paramIndex))
            e["expression"] = *ex;
         params.push_back(e);
      }
   out["params"] = params;
   // Every saved setting, by key (what set_param takes).
   std::vector<std::pair<std::string, std::string>> raw;
   Patch::SaveParams(gn.node.get(), raw);
   json settings = json::object();
   for (const auto& kv : raw)
   {
      const size_t sp = kv.first.find(' ');
      // Blobs (plugin state, gesture data) would drown the reply: size only.
      settings[sp == std::string::npos ? kv.first : kv.first.substr(sp + 1)] =
         kv.second.size() > 240 ? "<" + std::to_string(kv.second.size()) + " chars>" : kv.second;
   }
   out["settings"] = settings;
   auto issues = gLiveIssues.find(gn.index);
   if (issues != gLiveIssues.end())
   {
      json w = json::array();
      for (const RpcIssue& is : issues->second)
         w.push_back({ { "code", is.code }, { "message", is.message }, { "hint", is.hint } });
      out["warnings"] = w;
   }
   return out;
}

// ----------------------------------------------------------- image capture ---
// Turbo 0.46 (MCP phase 3): a node's image as a small JPEG / PNG, so an MCP
// client can look at what it built. Read back from the node's current output
// texture (main thread, GL current), downscaled to fit `maxSize`.
bool RpcCaptureTexture(unsigned int tex, int w, int h, int maxSize, bool png, std::string& outBase64,
                       int& outW, int& outH, std::string& err)
{
   if (tex == 0 || w <= 0 || h <= 0)
   {
      err = "this node has no image output right now (is something connected and playing?)";
      return false;
   }
   std::vector<unsigned char> pixels((size_t)w * (size_t)h * 4);
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   GLuint fbo = 0;
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
   const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
   if (complete)
   {
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   }
   glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
   glDeleteFramebuffers(1, &fbo);
   if (!complete)
   {
      err = "could not read this node's texture";
      return false;
   }
   // Box-filter down to maxSize on the long side, flipping to top-down rows.
   const float scale = std::min(1.0f, (float)std::max(16, maxSize) / (float)std::max(w, h));
   outW = std::max(1, (int)std::lround(w * scale));
   outH = std::max(1, (int)std::lround(h * scale));
   std::vector<unsigned char> small((size_t)outW * (size_t)outH * 4);
   for (int y = 0; y < outH; y++)
   {
      const int sy0 = (int)((float)y / outH * h), sy1 = std::max(sy0 + 1, (int)((float)(y + 1) / outH * h));
      for (int x = 0; x < outW; x++)
      {
         const int sx0 = (int)((float)x / outW * w), sx1 = std::max(sx0 + 1, (int)((float)(x + 1) / outW * w));
         unsigned int acc[4] = { 0, 0, 0, 0 };
         int n = 0;
         for (int sy = sy0; sy < sy1 && sy < h; sy++)
            for (int sx = sx0; sx < sx1 && sx < w; sx++, n++)
               for (int c = 0; c < 4; c++)
                  acc[c] += pixels[((size_t)sy * w + sx) * 4 + c];
         unsigned char* d = &small[((size_t)(outH - 1 - y) * outW + x) * 4];
         for (int c = 0; c < 4; c++)
            d[c] = (unsigned char)(n > 0 ? acc[c] / n : 0);
      }
   }
   std::vector<unsigned char> encoded;
   auto sink = [](void* ctx, void* data, int size)
   {
      auto* out = static_cast<std::vector<unsigned char>*>(ctx);
      out->insert(out->end(), (unsigned char*)data, (unsigned char*)data + size);
   };
   stbi_flip_vertically_on_write(0);
   if (png)
      stbi_write_png_to_func(sink, &encoded, outW, outH, 4, small.data(), outW * 4);
   else
   {
      std::vector<unsigned char> rgb((size_t)outW * outH * 3);
      for (size_t i = 0; i < (size_t)outW * outH; i++)
      {
         // Over black, so a transparent area reads as black, as on an output.
         const unsigned a = small[i * 4 + 3];
         for (int c = 0; c < 3; c++)
            rgb[i * 3 + c] = (unsigned char)(small[i * 4 + c] * a / 255);
      }
      stbi_write_jpg_to_func(sink, &encoded, outW, outH, 3, rgb.data(), 85);
   }
   if (encoded.empty())
   {
      err = "image encoding failed";
      return false;
   }
   outBase64 = Base64::Encode(encoded.data(), encoded.size());
   return true;
}

bool RpcCaptureNode(GraphNode& gn, int output, const nlohmann::json& params, nlohmann::json& outResult,
                    std::string& outError)
{
   INode* n = gn.node.get();
   const int maxSize = std::clamp(params.value("max_size", 768), 64, 2048);
   const bool png = params.value("format", std::string("jpeg")) == "png";
   std::string b64;
   int w = 0, h = 0;
   const unsigned int tex = n->GetOutputTextureAt(output);
   if (!RpcCaptureTexture(tex, n->GetOutputWidthAt(output), n->GetOutputHeightAt(output), maxSize, png, b64, w, h, outError))
      return false;
   outResult = { { "image_base64", b64 }, { "mime", png ? "image/png" : "image/jpeg" },
                 { "index", gn.index }, { "type", gn.typeName }, { "output", output },
                 { "source_size", { n->GetOutputWidthAt(output), n->GetOutputHeightAt(output) } },
                 { "image_size", { w, h } } };
   return true;
}

// --------------------------------------------------------------- dispatch ---
bool HandleRpcCommand(const std::string& method, const nlohmann::json& params,
                      nlohmann::json& outResult, std::string& outError);

// Replaces "$N" / "$N.field" strings with the result of batch command N
// ("$N" alone = its "index").
nlohmann::json RpcSubstituteRefs(const nlohmann::json& v, const std::vector<nlohmann::json>& results, std::string& err)
{
   using json = nlohmann::json;
   if (v.is_string())
   {
      const std::string s = v.get<std::string>();
      if (s.size() >= 2 && s[0] == '$' && std::isdigit((unsigned char)s[1]))
      {
         const size_t dot = s.find('.');
         const int n = std::atoi(s.substr(1, dot == std::string::npos ? std::string::npos : dot - 1).c_str());
         const std::string field = dot == std::string::npos ? "index" : s.substr(dot + 1);
         if (n < 0 || n >= (int)results.size() || !results[(size_t)n].is_object() || !results[(size_t)n].contains(field))
         {
            err = "reference " + s + " does not resolve";
            return v;
         }
         return results[(size_t)n][field];
      }
      return v;
   }
   if (v.is_object())
   {
      json o = json::object();
      for (auto it = v.begin(); it != v.end(); ++it)
         o[it.key()] = RpcSubstituteRefs(it.value(), results, err);
      return o;
   }
   if (v.is_array())
   {
      json a = json::array();
      for (const json& e : v)
         a.push_back(RpcSubstituteRefs(e, results, err));
      return a;
   }
   return v;
}

bool HandleRpcCommandExtra(const std::string& method, const nlohmann::json& params,
                           nlohmann::json& outResult, std::string& outError, bool& handled)
{
   using json = nlohmann::json;
   handled = true;

   if (method == "describe")
   {
      const std::string type = params.value("type", std::string());
      if (type.empty())
      {
         json list = json::array();
         for (const std::string& cat : NodeFactory::Instance().GetCategories())
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(cat))
            {
               if (!IsUserSpawnable(name))
                  continue;
               GraphNode probe;
               probe.typeName = name;
               probe.category = cat;
               const char* helpText = NodeHelpText(probe);
               std::string help = helpText ? helpText : "";
               const size_t stop = help.find(". ");
               if (stop != std::string::npos && stop < 200)
                  help = help.substr(0, stop + 1);
               else if (help.size() > 200)
                  help = help.substr(0, 197) + "...";
               list.push_back({ { "type", name }, { "category", cat }, { "summary", help } });
            }
         outResult = { { "node_types", list },
                       { "note", "call describe with {\"type\": name} for inputs, outputs and settings" } };
         return true;
      }
      const std::string canonical = NodeFactory::CanonicalName(type);
      GraphNode probe;
      probe.node.reset(NodeFactory::Instance().MakeNode(canonical));
      if (!probe.node)
      {
         outError = "unknown node type '" + type + "' (describe without arguments lists them)";
         return false;
      }
      probe.typeName = canonical;
      probe.category = NodeFactory::Instance().CategoryOf(canonical);
      outResult = RpcDescribeNodeShape(probe, true);
      const char* helpText = NodeHelpText(probe);
      outResult["help"] = helpText ? helpText : "";
      outResult["note"] = "settings are the patch keys set_param takes; modulate / set_expression take the "
                          "drawn param names that explain lists once the node exists";
      return true;
   }
   if (method == "explain")
   {
      if (params.contains("index"))
      {
         GraphNode* gn = FindNodeByIndex(params.value("index", -1));
         if (gn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         outResult = ExplainNode(*gn);
         return true;
      }
      json nodes = json::array();
      for (GraphNode& gn : gNodes)
         nodes.push_back(ExplainNode(gn));
      Transport& t = Transport::Instance();
      outResult = { { "nodes", nodes },
                    { "transport", { { "playing", t.IsPlaying() }, { "bpm", t.Tempo() }, { "beat", t.Beats() } } },
                    { "audio_running", AudioEngine::Instance().SampleRate() > 0.0 },
                    { "patch_path", gPatchPath },
                    { "unsaved", gPatchDirty } };
      return true;
   }
   if (method == "get_patch_text")
   {
      std::string text, err;
      if (!Patch::WriteText(BuildPatchData(), text, err))
      {
         outError = err;
         return false;
      }
      outResult = { { "text", text } };
      return true;
   }
   if (method == "validate_patch_text" || method == "load_patch_text")
   {
      const std::string text = params.value("text", std::string());
      Patch::Data data;
      std::string err;
      if (!Patch::ReadText(text, data, err))
      {
         if (method == "validate_patch_text")
         {
            outResult = { { "ok", false }, { "errors", json::array({ "parse: " + err }) }, { "warnings", json::array() } };
            return true;
         }
         outError = "parse: " + err;
         return false;
      }
      json report = ValidatePatchData(data);
      if (method == "validate_patch_text")
      {
         outResult = report;
         return true;
      }
      if (!report["ok"].get<bool>() && !params.value("force", false))
      {
         outError = "patch has errors (pass force=true to load anyway): " + report["errors"].dump();
         return false;
      }
      PushUndoCheckpoint(); // one undo step back to the graph before the load
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      ApplyPatchData(data, false);
      gSuppressUndoCheckpoints = wasSuppressed;
      gPatchDirty = true;
      gPatchStatus = "Loaded from text (RPC)";
      gRequestFitView = true;
      outResult = report;
      outResult["note"] = "the patch's node numbers are kept when they are unique and ascending; explain shows the live ones";
      return true;
   }
   if (method == "batch")
   {
      if (!params.contains("commands") || !params["commands"].is_array())
      {
         outError = "batch needs commands: [{method, params}, ...]";
         return false;
      }
      // All or nothing, one undo step. The snapshot becomes the undo step
      // only on success; on failure the graph goes back to it and the undo /
      // redo stacks are untouched. Commands that manage documents or the
      // stacks themselves are not allowed inside.
      static const std::set<std::string> kNotInBatch = { "batch", "undo", "redo", "load_patch", "load_patch_text",
                                                         "new_patch", "save_patch" };
      Patch::Data before = BuildPatchData();
      const size_t pendingBefore = gRpcPendingBinds.size();
      const bool dirtyBefore = gPatchDirty;
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      std::vector<json> results;
      std::string failure;
      int failedAt = -1;
      for (const json& cmd : params["commands"])
      {
         gSuppressUndoCheckpoints = true; // re-armed every step: some calls reset it
         const std::string m = cmd.is_object() ? cmd.value("method", std::string()) : std::string();
         std::string e;
         json r;
         if (m.empty())
            e = "missing method";
         else if (kNotInBatch.count(m))
            e = m + " is not allowed inside a batch";
         else
         {
            const json p = RpcSubstituteRefs(cmd.value("params", json::object()), results, e);
            if (e.empty() && HandleRpcCommand(m, p, r, e))
            {
               results.push_back(r);
               continue;
            }
         }
         failure = m + ": " + e;
         failedAt = (int)results.size();
         break;
      }
      if (failedAt >= 0)
      {
         gSuppressUndoCheckpoints = true;
         ApplyPatchData(before, false);
         gRpcPendingBinds.resize(pendingBefore);
         gSuppressUndoCheckpoints = wasSuppressed;
         gPatchDirty = dirtyBefore;
         outError = "command " + std::to_string(failedAt) + " failed (" + failure + "); nothing was changed";
         return false;
      }
      gSuppressUndoCheckpoints = wasSuppressed;
      PushUndoSnapshot(std::move(before));
      outResult = { { "results", results } };
      return true;
   }
   if (method == "modulate" || method == "set_expression" || method == "unmodulate")
   {
      RpcPendingBind b;
      b.kind = method == "modulate" ? 0 : method == "set_expression" ? 1 : 2;
      b.dst = params.value("index", -1);
      if (GraphNode* d = FindNodeByIndex(b.dst))
         b.dstUid = d->uid;
      b.param = params.contains("param") ? params["param"] : json();
      if (b.param.is_null())
      {
         outError = "param (name or index) is required";
         return false;
      }
      if (b.kind == 0)
      {
         b.src = params.value("srcIndex", -1);
         GraphNode* src = FindNodeByIndex(b.src);
         if (src == nullptr)
         {
            outError = "unknown srcIndex";
            return false;
         }
         b.srcOutput = params.contains("srcOutput") ? RpcResolveOutput(*src, params["srcOutput"]) : 0;
         if (b.srcOutput < 0)
         {
            outError = "unknown srcOutput";
            return false;
         }
         b.polarity = params.value("polarity", std::string("absolute")) == "bipolar" ? 1 : 0;
         b.depth = params.value("depth", 1.0f);
         if (params.contains("range") && params["range"].is_array() && params["range"].size() == 4)
         {
            b.hasRange = true;
            b.inMin = params["range"][0].get<float>(); b.inMax = params["range"][1].get<float>();
            b.outMin = params["range"][2].get<float>(); b.outMax = params["range"][3].get<float>();
         }
      }
      else if (b.kind == 1)
         b.expr = params.value("expression", std::string());
      std::string err;
      const int r = RpcApplyBind(b, err);
      if (r < 0)
      {
         outError = err;
         return false;
      }
      if (r == 0)
      {
         gRpcPendingBinds.push_back(b);
         outResult = { { "pending", true },
                       { "note", "the node has not drawn yet: this lands within a few frames (explain shows it)" } };
         return true;
      }
      outResult = json::object();
      return true;
   }
   if (method == "auto_layout")
   {
      std::vector<int> which;
      if (params.contains("nodes") && params["nodes"].is_array())
         for (const json& v : params["nodes"])
            which.push_back(v.get<int>());
      else
         for (GraphNode& gn : gNodes)
            which.push_back(gn.index);
      PushUndoCheckpoint();
      RunAutoLayout(which);
      if (params.value("fit", true))
         gRequestFitView = true;
      outResult = json::object();
      return true;
   }
   if (method == "transport")
   {
      Transport& t = Transport::Instance();
      if (params.contains("audio") && params["audio"].is_boolean())
      {
         if (params["audio"].get<bool>() && AudioEngine::Instance().SampleRate() <= 0.0)
         {
            gAudioStartError.clear();
            if (!StartAudioEngine(gAudioStartError))
            {
               outError = "audio: " + gAudioStartError;
               return false;
            }
         }
         else if (!params["audio"].get<bool>())
            AudioEngine::Instance().Stop();
      }
      if (params.contains("bpm") && params["bpm"].is_number())
         t.SetTempo(std::clamp(params["bpm"].get<float>(), 20.0f, 300.0f));
      if (params.contains("seek_beats") && params["seek_beats"].is_number())
         t.SeekBeats(std::max(0.0, params["seek_beats"].get<double>()));
      if (params.contains("play") && params["play"].is_boolean())
         t.SetPlaying(params["play"].get<bool>());
      outResult = { { "playing", t.IsPlaying() }, { "bpm", t.Tempo() }, { "beat", t.Beats() },
                    { "audio_running", AudioEngine::Instance().SampleRate() > 0.0 } };
      return true;
   }
   if (method == "screenshot_node")
   {
      GraphNode* gn = FindNodeByIndex(params.value("index", -1));
      if (gn == nullptr)
      {
         outError = "unknown node index";
         return false;
      }
      const int output = params.contains("output") ? RpcResolveOutput(*gn, params["output"]) : 0;
      if (output < 0)
      {
         outError = "unknown output";
         return false;
      }
      return RpcCaptureNode(*gn, output, params, outResult, outError);
   }
   if (method == "render_frame")
   {
      // The live Output (the first one, or the one given), as the projector sees it.
      GraphNode* target = params.contains("index") ? FindNodeByIndex(params.value("index", -1)) : nullptr;
      if (target == nullptr)
         for (GraphNode& g : gNodes)
            if (dynamic_cast<OutputNode*>(g.node.get()) != nullptr)
            {
               target = &g;
               break;
            }
      if (target == nullptr)
      {
         outError = "no Output node in the patch (add one, or use screenshot_node on any image node)";
         return false;
      }
      if (!RpcCaptureNode(*target, 0, params, outResult, outError))
         return false;
      // Optionally also written to disk at full size (PNG or JPEG by extension).
      const std::string path = params.value("path", std::string());
      if (!path.empty())
      {
         if (auto* out = dynamic_cast<OutputNode*>(target->node.get()))
         {
            ExportImage(out, path);
            outResult["saved"] = path;
         }
      }
      return true;
   }
   if (method == "ping")
   {
      outResult = { { "app", "Infinite-Turbo" }, { "nodes", (int)gNodes.size() } };
      return true;
   }
   handled = false;
   return false;
}
