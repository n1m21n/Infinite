   // Turbo 0.60: upstream's hand-written patch format - nodes named with an
   // `id` line, cables and bindings that refer to them, to their slots and
   // outputs by word, `mod`/`expr` lines that name the control by its saved
   // key, and `mod` lines that carry upstream's lo/hi range. Upstream resolves
   // the words with PatchSchema (core/PatchSchema.h, verbatim); this file is
   // the Turbo side of it: the schema environment built from probe nodes, the
   // pass ApplyPatchData runs first, and the per-frame step that attaches what
   // can only be attached once the nodes have drawn.

   // A free-standing instance (never on the canvas) used to answer schema
   // questions about a node type. Cached for the process, like upstream's.
   GraphNode* SchemaProbe(const std::string& typeName)
   {
      static std::map<std::string, std::unique_ptr<GraphNode>> sProbes;
      auto it = sProbes.find(typeName);
      if (it != sProbes.end())
         return it->second.get();
      std::unique_ptr<GraphNode> gn;
      if (INode* made = NodeFactory::Instance().MakeNode(typeName))
      {
         gn = std::make_unique<GraphNode>();
         gn->node.reset(made);
         gn->typeName = typeName;
         gn->category = NodeFactory::Instance().CategoryOf(typeName);
      }
      GraphNode* raw = gn.get();
      sProbes[typeName] = std::move(gn);
      return raw;
   }

   // Saved keys of a node, for the schema's param list.
   class SchemaParamRecorder : public ParamVisitor
   {
   public:
      explicit SchemaParamRecorder(std::vector<PatchSchema::ParamInfo>& out) : mOut(out) {}
      void Float(const char* n, float& v) override { Add(n, 'f', std::to_string(v)); }
      void Int(const char* n, int& v) override { Add(n, 'i', std::to_string(v)); }
      void Bool(const char* n, bool& v) override { Add(n, 'b', v ? "1" : "0"); }
      void Text(const char* n, std::string& v) override { Add(n, 's', v); }
      void Color(const char* n, float* rgb) override
      {
         Add(n, 'c', std::to_string(rgb[0]) + " " + std::to_string(rgb[1]) + " " + std::to_string(rgb[2]));
      }

   private:
      void Add(const char* n, char kind, const std::string& def) { mOut.push_back({ n, kind, def }); }
      std::vector<PatchSchema::ParamInfo>& mOut;
   };

   bool SchemaSlotKind(GraphNode& gn, int slot, std::string& kind)
   {
      INode* n = gn.node.get();
      if (n->AudioInputSlot(slot) != nullptr) kind = "audio";
      else if (n->NoteInputSlot(slot) != nullptr) kind = "note";
      else if (n->GeometryInputSlot(slot) != nullptr) kind = "geometry";
      else if (n->ModulatorInputSlot(slot) != nullptr) kind = "modulator";
      else if (slot < InputCountFor(gn)) kind = "image";
      else return false;
      return true;
   }

   std::unique_ptr<PatchSchema::TypeSchema> BuildSchema(GraphNode* gn, const std::string& typeName)
   {
      std::unique_ptr<PatchSchema::TypeSchema> t;
      if (gn == nullptr)
         return t;
      t = std::make_unique<PatchSchema::TypeSchema>();
      t->name = typeName;
      t->category = gn->category;
      SchemaParamRecorder rec(t->params);
      gn->node->VisitParams(rec);
      for (int slot = 0; slot < 64; slot++)
      {
         std::string kind;
         if (!SchemaSlotKind(*gn, slot, kind))
            continue;
         const char* label = gn->node->InputLabel(slot);
         t->inputs.push_back({ slot, kind, label != nullptr ? label : "" });
      }
      const int outs = std::max(1, gn->node->OutputCount());
      for (int o = 0; o < outs; o++)
      {
         const char* label = gn->node->OutputLabel(o);
         t->outputs.push_back({ label != nullptr ? label : "out", "image",
                                ModulatorForOutput(gn->node.get(), o) != nullptr });
      }
      t->hardwareDriven = gn->node->IsHardwareDriven();
      return t;
   }

   const PatchSchema::TypeSchema* SchemaFor(const std::string& typeName)
   {
      static std::map<std::string, std::unique_ptr<PatchSchema::TypeSchema>> sSchemas;
      auto it = sSchemas.find(typeName);
      if (it != sSchemas.end())
         return it->second.get();
      std::unique_ptr<PatchSchema::TypeSchema> t = BuildSchema(SchemaProbe(typeName), typeName);
      const PatchSchema::TypeSchema* raw = t.get();
      sSchemas[typeName] = std::move(t);
      return raw;
   }

   // The schema of one record's node with its saved params applied, so pins
   // that grow from state (a Mixer's channel count, a Plugin's note pin) show.
   const PatchSchema::TypeSchema* InstanceSchemaFor(const Patch::NodeRecord& rec)
   {
      struct Instance
      {
         std::unique_ptr<GraphNode> node;
         std::unique_ptr<PatchSchema::TypeSchema> schema;
      };
      static std::map<std::string, Instance> sInstances;
      std::string key = rec.typeName;
      for (const auto& kv : rec.params)
         key += '\n' + kv.first + ' ' + kv.second;
      auto it = sInstances.find(key);
      if (it != sInstances.end())
         return it->second.schema.get();
      Instance inst;
      if (INode* made = NodeFactory::Instance().MakeNode(rec.typeName))
      {
         inst.node = std::make_unique<GraphNode>();
         inst.node->node.reset(made);
         inst.node->typeName = rec.typeName;
         inst.node->category = NodeFactory::Instance().CategoryOf(rec.typeName);
         Patch::LoadParams(made, rec.params);
         inst.schema = BuildSchema(inst.node.get(), rec.typeName);
      }
      const PatchSchema::TypeSchema* raw = inst.schema.get();
      sInstances[key] = std::move(inst);
      return raw;
   }

   PatchSchema::Env MakeSchemaEnv()
   {
      PatchSchema::Env env;
      env.schema = [](const std::string& n) { return SchemaFor(n); };
      for (const std::string& cat : NodeFactory::Instance().GetCategories())
         for (const std::string& n : NodeFactory::Instance().GetNodesInCategory(cat))
            env.allTypes.push_back(n);
      env.link = [](const std::string&, int, const std::string&, int) { return PatchSchema::Link::Ok; };
      env.instanceSchema = [](const Patch::NodeRecord& rec) { return InstanceSchemaFor(rec); };
      return env;
   }

   // What a load could not attach yet. Keyed bindings wait for their nodes to
   // draw (a control only registers its ParamRef by drawing); lo/hi ranges
   // wait for the destination's range, which also comes from the draw.
   struct PendingNamed
   {
      std::vector<Patch::ModRecord> mods; // saved indices, dstKey set
      std::vector<Patch::ExprRecord> exprs;
      struct Range
      {
         int dst = 0, param = 0; // live indices
         float lo = 0.0f, hi = 0.0f;
      };
      std::vector<Patch::ModRecord> rangedMods; // saved indices, lo/hi set
      std::vector<Range> ranges;
      std::map<int, int> remap; // saved index -> live index
      int waited = 0;
   };
   PendingNamed gPendingNamed;
   std::map<int, int> gLastApplyRemap; // set by ApplyPatchData

   // Turbo 0.60: the Mixer took upstream's 12 channels, whose body counts
   // the channel stepper as parameter 0 - faders and pans moved up by one.
   // A Mixer saved before (no `channels` key) gets its bindings shifted.
   bool IsLegacyMixer(const Patch::NodeRecord& rec)
   {
      if (rec.typeName != "Mixer")
         return false;
      for (const auto& kv : rec.params)
         if (kv.first == "i channels")
            return false;
      return true;
   }

   void ShiftLegacyMixerOrdinals(Patch::Data& data)
   {
      std::set<int> legacy;
      for (const Patch::NodeRecord& rec : data.nodes)
         if (IsLegacyMixer(rec))
            legacy.insert(rec.index);
      if (legacy.empty())
         return;
      auto shift = [&](int dstIndex, int& param) {
         if (legacy.count(dstIndex) != 0 && param >= 0)
            param++;
      };
      for (Patch::ModRecord& m : data.modulation)
         if (m.dstKey.empty())
            shift(m.dstIndex, m.dstParam);
      for (Patch::ExprRecord& e : data.expressions)
         if (e.dstKey.empty())
            shift(e.dstIndex, e.dstParam);
      for (Patch::MidiMapRecord& m : data.midi)
         shift(m.dstIndex, m.dstParam);
      for (Patch::GestureRecord& g : data.gestures)
         shift(g.dstIndex, g.dstParam);
      for (Patch::PerfRecord& pr : data.performance)
      {
         shift(pr.dstIndex, pr.dstParam);
         shift(pr.dstIndex, pr.dstParam2);
         for (Patch::PerfTarget& t : pr.targets)
            shift(t.dstIndex, t.dstParam);
         for (Patch::PerfTarget& t : pr.targetsY)
            shift(t.dstIndex, t.dstParam);
      }
   }

   bool NeedsNamedPrep(const Patch::Data& data)
   {
      if (data.hasNamedRefs || data.hasKeyRefs)
         return true;
      for (const Patch::NodeRecord& rec : data.nodes)
         if (IsLegacyMixer(rec))
            return true;
      for (const Patch::ModRecord& m : data.modulation)
         if (m.hasRange || !m.enabled)
            return true;
      return false;
   }

   // Runs on a copy of the patch before ApplyPatchData spawns anything.
   void PrepareNamedPatch(Patch::Data& data)
   {
      gPendingNamed = PendingNamed();
      ShiftLegacyMixerOrdinals(data);
      if (data.hasNamedRefs)
      {
         std::vector<Headless::Issue> errors;
         PatchSchema::Resolve(data, MakeSchemaEnv(), errors);
         for (const Headless::Issue& e : errors)
            fprintf(stderr, "patch: line %d: %s\n", e.line, e.message.c_str());
         if (!errors.empty())
            gPatchStatus = "Opened with " + std::to_string(errors.size()) + " unresolved name(s); line " +
                           std::to_string(errors.front().line) + ": " + errors.front().message;
      }
      // Keyed bindings wait for the nodes to draw.
      for (const Patch::ModRecord& m : data.modulation)
         if (!m.dstKey.empty() && m.enabled)
            gPendingNamed.mods.push_back(m);
      for (const Patch::ExprRecord& e : data.expressions)
         if (!e.dstKey.empty())
            gPendingNamed.exprs.push_back(e);
      // A disabled upstream binding has no Turbo form: it is not loaded.
      data.modulation.erase(std::remove_if(data.modulation.begin(), data.modulation.end(),
                                           [](const Patch::ModRecord& m) { return !m.dstKey.empty() || !m.enabled; }),
                            data.modulation.end());
      data.expressions.erase(std::remove_if(data.expressions.begin(), data.expressions.end(),
                                            [](const Patch::ExprRecord& e) { return !e.dstKey.empty(); }),
                             data.expressions.end());
      for (const Patch::ModRecord& m : data.modulation)
         if (m.hasRange)
            gPendingNamed.rangedMods.push_back(m);
      // A dropdown given its option name cannot be matched here (upstream's
      // window app skips it too): the node keeps its default.
      for (Patch::NodeRecord& n : data.nodes)
         n.params.erase(std::remove_if(n.params.begin(), n.params.end(),
                                       [](const std::pair<std::string, std::string>& p) {
                                          char* end = nullptr;
                                          std::strtod(p.second.c_str(), &end);
                                          return p.first[0] == 'i' && !p.second.empty() &&
                                                 (end == p.second.c_str() || *end != '\0');
                                       }),
                        n.params.end());
   }

   // After ApplyPatchData: the saved -> live index map is known.
   void FinishNamedPatch()
   {
      PendingNamed& pn = gPendingNamed;
      pn.remap = gLastApplyRemap;
      for (const Patch::ModRecord& m : pn.rangedMods)
      {
         auto d = pn.remap.find(m.dstIndex);
         if (d != pn.remap.end())
            pn.ranges.push_back({ d->second, m.dstParam, m.lo, m.hi });
      }
      pn.rangedMods.clear();
   }

   class KeyAddrFinder : public ParamVisitor
   {
   public:
      explicit KeyAddrFinder(const std::string& k) : key(k) {}
      const void* addr = nullptr;
      void Float(const char* n, float& v) override { if (key == n) addr = &v; }
      void Int(const char* n, int& v) override { if (key == n) addr = &v; }
      void Bool(const char* n, bool& v) override { if (key == n) addr = &v; }
      void Text(const char*, std::string&) override {}
      void Color(const char*, float*) override {}

   private:
      std::string key;
   };

   // The ParamRef index of a saved key on a drawn node: the registered
   // control whose value is the member VisitParams hands out for the key.
   int ParamIndexOfKeyOnNode(const GraphNode& gn, const std::string& key)
   {
      if (gn.node == nullptr)
         return -1;
      KeyAddrFinder f(key);
      gn.node->VisitParams(f);
      if (f.addr == nullptr)
         return -1;
      for (const ParamRef& r : Modulation::Instance().FrameParams())
         if (r.nodeIndex == gn.index && (const void*)r.value == f.addr)
            return r.paramIndex;
      return -1;
   }

   // Whether a collapsed node has to register its controls this frame
   // (Nodes.inl's register-only pass): a cable, expression, MIDI mapping or
   // gesture drives it, or a load is still waiting to attach a binding by name.
   bool NodeIsParamDriven(const GraphNode& gn)
   {
      if (gn.hasModulatedParams)
         return true;
      for (const MidiMap::Mapping& m : MidiMap::All())
         if (m.nodeIndex == gn.index)
            return true;
      for (const auto& pb : GestureRecorder::Instance().Playbacks())
         if (pb.first.first == gn.index)
            return true;
      const PendingNamed& pn = gPendingNamed;
      auto pendingOn = [&](int saved) {
         auto it = pn.remap.find(saved);
         return it != pn.remap.end() && it->second == gn.index;
      };
      for (const Patch::ModRecord& m : pn.mods)
         if (pendingOn(m.dstIndex))
            return true;
      for (const Patch::ExprRecord& e : pn.exprs)
         if (pendingOn(e.dstIndex))
            return true;
      for (const PendingNamed::Range& r : pn.ranges)
         if (r.dst == gn.index)
            return true;
      return false;
   }

   // Saved key of each bound control, refreshed once a frame while the
   // controls are registered (RefreshParamKeyCache), so a save from the menu
   // - before the nodes draw - still has them. Only floats: a key's address
   // has to be the ParamRef's own value (discrete controls write a proxy).
   std::map<std::pair<int, int>, std::string> gParamKeyCache;

   class KeyOfAddr : public ParamVisitor
   {
   public:
      std::map<const void*, std::string> keys;
      std::set<std::string> seen, dup;
      void Float(const char* n, float& v) override
      {
         if (!seen.insert(n).second)
            dup.insert(n);
         keys[&v] = n;
      }
      void Int(const char*, int&) override {}
      void Bool(const char*, bool&) override {}
      void Text(const char*, std::string&) override {}
      void Color(const char*, float*) override {}
   };

   void RefreshParamKeyCache()
   {
      const Modulation& mod = Modulation::Instance();
      std::set<std::pair<int, int>> bound;
      for (const auto& link : mod.Links())
         bound.insert(link.first);
      for (const auto& expr : mod.Expressions())
         bound.insert(expr.first);
      for (auto it = gParamKeyCache.begin(); it != gParamKeyCache.end();)
         it = bound.count(it->first) ? std::next(it) : gParamKeyCache.erase(it);
      if (bound.empty())
         return;
      std::map<int, KeyOfAddr> perNode;
      for (const ParamRef& r : mod.FrameParams())
      {
         const std::pair<int, int> k(r.nodeIndex, r.paramIndex);
         if (bound.count(k) == 0 || gParamKeyCache.count(k) != 0)
            continue;
         auto pn = perNode.find(r.nodeIndex);
         if (pn == perNode.end())
         {
            GraphNode* gn = FindNodeByIndex(r.nodeIndex);
            if (gn == nullptr || gn->node == nullptr)
               continue;
            pn = perNode.emplace(r.nodeIndex, KeyOfAddr()).first;
            gn->node->VisitParams(pn->second);
         }
         auto a = pn->second.keys.find((const void*)r.value);
         if (a != pn->second.keys.end() && pn->second.dup.count(a->second) == 0)
            gParamKeyCache[k] = a->second;
      }
   }

   std::string SavedKeyOfParam(int nodeIndex, int paramIndex)
   {
      auto it = gParamKeyCache.find({ nodeIndex, paramIndex });
      return it == gParamKeyCache.end() ? std::string() : it->second;
   }

   // Once a frame, before ClearFrameParams (last frame's controls are still
   // registered): attaches keyed bindings once their nodes have drawn, and
   // turns upstream lo/hi into the range mapper once the range is known.
   void PollPendingNamed()
   {
      PendingNamed& pn = gPendingNamed;
      if (pn.mods.empty() && pn.exprs.empty() && pn.ranges.empty())
         return;
      const std::vector<ParamRef>& refs = Modulation::Instance().FrameParams();
      auto liveOf = [&](int saved) -> GraphNode* {
         auto it = pn.remap.find(saved);
         return it == pn.remap.end() ? nullptr : FindNodeByIndex(it->second);
      };
      auto drawn = [&](int liveIndex) {
         for (const ParamRef& r : refs)
            if (r.nodeIndex == liveIndex)
               return true;
         return false;
      };
      pn.waited++;
      if (!pn.mods.empty() || !pn.exprs.empty())
      {
         bool ready = true;
         for (const Patch::ModRecord& m : pn.mods)
            if (GraphNode* d = liveOf(m.dstIndex))
               ready = ready && drawn(d->index);
         for (const Patch::ExprRecord& e : pn.exprs)
            if (GraphNode* d = liveOf(e.dstIndex))
               ready = ready && drawn(d->index);
         if (ready || pn.waited >= 60)
         {
            int applied = 0;
            std::string missed;
            for (const Patch::ModRecord& m : pn.mods)
            {
               GraphNode* dst = liveOf(m.dstIndex);
               GraphNode* src = liveOf(m.srcIndex);
               if (dst == nullptr || src == nullptr)
                  continue;
               const int p = ParamIndexOfKeyOnNode(*dst, m.dstKey);
               if (p < 0)
               {
                  missed += (missed.empty() ? "" : ", ") + dst->typeName + "." + m.dstKey;
                  continue;
               }
               Modulation::Source source;
               source.nodeIndex = src->index;
               source.outputIndex = m.srcOutput;
               source.polarity = m.polarity;
               source.depth = m.depth;
               source.centre = m.centre;
               if (!m.hasRange)
               {
                  source.inMin = m.inMin;
                  source.inMax = m.inMax;
                  source.outMin = m.outMin;
                  source.outMax = m.outMax;
               }
               Modulation::Instance().RestoreLink(dst->index, p, source);
               if (m.hasRange)
                  pn.ranges.push_back({ dst->index, p, m.lo, m.hi });
               applied++;
            }
            for (const Patch::ExprRecord& e : pn.exprs)
            {
               GraphNode* dst = liveOf(e.dstIndex);
               if (dst == nullptr)
                  continue;
               const int p = ParamIndexOfKeyOnNode(*dst, e.dstKey);
               if (p < 0)
               {
                  missed += (missed.empty() ? "" : ", ") + dst->typeName + "." + e.dstKey;
                  continue;
               }
               Modulation::Instance().SetExpression(dst->index, p, e.text);
               applied++;
            }
            if (!missed.empty())
               fprintf(stderr, "patch: controls not found: %s\n", missed.c_str());
            gPatchStatus = std::to_string(applied) + " binding(s) by control name attached" +
                           (missed.empty() ? std::string() : "; not found: " + missed);
            pn.mods.clear();
            pn.exprs.clear();
         }
      }
      // Upstream's lo/hi is the destination-units range the binding writes:
      // value = lo + (hi - lo) * v. Turbo's absolute binding with the output
      // window writes min + (max - min) * (outMin + (outMax - outMin) * v),
      // the same line when outMin/outMax are lo/hi in the param's 0..1.
      for (auto it = pn.ranges.begin(); it != pn.ranges.end();)
      {
         const ParamRef* ref = nullptr;
         for (const ParamRef& r : refs)
            if (r.nodeIndex == it->dst && r.paramIndex == it->param)
               ref = &r;
         if (ref == nullptr)
         {
            it = pn.waited >= 120 ? pn.ranges.erase(it) : std::next(it);
            continue;
         }
         Modulation::Source s = Modulation::Instance().ModulatorFor(it->dst, it->param);
         const float range = ref->maxValue - ref->minValue;
         if (s.nodeIndex >= 0 && std::fabs(range) > 1e-9f)
         {
            s.polarity = Modulation::Source::kAbsolute;
            s.inMin = 0.0f;
            s.inMax = 1.0f;
            s.outMin = (it->lo - ref->minValue) / range;
            s.outMax = (it->hi - ref->minValue) / range;
            Modulation::Instance().RestoreLink(it->dst, it->param, s);
         }
         it = pn.ranges.erase(it);
      }
   }
