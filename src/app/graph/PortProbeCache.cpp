// fork: see PortProbeCache.h
#include "app/graph/PortProbeCache.h"
#include "app/AppShared.h"
#include <bitset>
#include <chrono>
#include <unordered_map>

namespace app
{
namespace
{
   struct Entry
   {
      std::bitset<512> accepts;           // indexed by SrcKind mask
      std::vector<uint16_t> outputKinds;
   };
   std::unordered_map<std::string, Entry>& Cache()
   {
      static std::unordered_map<std::string, Entry> c;
      return c;
   }
   bool sWarmDone = false;

   const Entry* Build(const std::string& name)
   {
      auto& cache = Cache();
      if (auto it = cache.find(name); it != cache.end())
         return &it->second;
      GraphNode probe;
      probe.node.reset(NodeFactory::Instance().MakeNode(name));
      if (!probe.node)
         return nullptr;
      Entry e;
      const int slots = InputCountFor(probe);
      for (uint16_t kind = 0; kind < 512; ++kind)
      {
         if (kind & kSrcPredictor)
            continue;   // IsInputSlotCompatible refuses every slot for a predictor
         for (int s = 0; s < slots; ++s)
            if (SlotAcceptsKind(&probe, s, kind))
            {
               e.accepts.set(kind);
               break;
            }
      }
      const int outs = probe.node->OutputCount();
      for (int o = 0; o < outs; ++o)
         e.outputKinds.push_back(SrcKindOfOutput(&probe, o));
      return &cache.emplace(name, std::move(e)).first->second;
   }
}

bool SlotAcceptsKind(GraphNode* dst, int slot, uint16_t kind)
{
   // IsInputSlotCompatible only compares these pointers with nullptr, so a non-null stand-in is enough.
   auto* mark = reinterpret_cast<void*>(uintptr_t(1));
   return IsInputSlotCompatible(dst, slot, (kind & kSrcModulator) != 0,
                                (kind & kSrcPalette) ? static_cast<IPaletteSource*>(mark) : nullptr,
                                (kind & kSrcGeometry) ? static_cast<IGeometrySource*>(mark) : nullptr,
                                (kind & kSrcCamera) ? static_cast<CameraNode*>(mark) : nullptr,
                                (kind & kSrcLight) ? static_cast<LightNode*>(mark) : nullptr,
                                (kind & kSrcEnvironment) != 0, (kind & kSrcAudio) != 0, (kind & kSrcNote) != 0,
                                (kind & kSrcPredictor) != 0);
}

uint16_t SrcKindOfOutput(GraphNode* src, int out)
{
   INode* n = src->node.get();
   uint16_t k = 0;
   if (dynamic_cast<IModulator*>(n) != nullptr || ModulatorForOutput(n, out) != nullptr)
      k |= kSrcModulator;
   if (dynamic_cast<IPaletteSource*>(n) != nullptr)
      k |= kSrcPalette;
   if (auto* g = dynamic_cast<IGeometrySource*>(n); g != nullptr && g->IsGeometryOutputIndex(out))
      k |= kSrcGeometry;
   if (dynamic_cast<CameraNode*>(n) != nullptr)
      k |= kSrcCamera;
   if (dynamic_cast<LightNode*>(n) != nullptr)
      k |= kSrcLight;
   if (dynamic_cast<EnvironmentNode*>(n) != nullptr)
      k |= kSrcEnvironment;
   if (auto* a = dynamic_cast<IAudioSource*>(n); a != nullptr && a->IsAudioOutputIndex(out))
      k |= kSrcAudio;
   if (dynamic_cast<INoteSource*>(n) != nullptr)
      k |= kSrcNote;
   if (dynamic_cast<IPredictor*>(n) != nullptr)
      k |= kSrcPredictor;
   return k;
}

bool TypeAcceptsKind(const std::string& name, uint16_t kind)
{
   const Entry* e = Build(name);
   return e != nullptr && e->accepts.test(kind & 511u);
}

const std::vector<uint16_t>& TypeOutputKinds(const std::string& name)
{
   static const std::vector<uint16_t> none;
   const Entry* e = Build(name);
   return e != nullptr ? e->outputKinds : none;
}

void WarmPortProbeCache(double budgetMs)
{
   if (sWarmDone)
      return;
   const auto t0 = std::chrono::steady_clock::now();
   for (const std::string& category : NodeFactory::Instance().GetCategories())
      for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
      {
         if (Cache().count(name) != 0 || !IsUserSpawnable(name))
            continue;
         Build(name);
         if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > budgetMs)
            return;
      }
   sWarmDone = true;   // types registered later (extensions) are built on first use
}
}
