#include "app/graph/NodeClipboard.h"
#include <algorithm>
#include <cmath>
#include <map>
#include "app/AppShared.h"
#include "core/Patch.h"

namespace app
{
namespace
{
   const char* kHeader = "infinite-nodes v";

   // The version in the first line, or -1 when it is not ours.
   int HeaderVersion(const std::string& text, size_t* bodyAt)
   {
      const size_t n = std::char_traits<char>::length(kHeader);
      if (text.compare(0, n, kHeader) != 0)
         return -1;
      const size_t eol = text.find('\n');
      if (eol == std::string::npos)
         return -1;
      const std::string num = text.substr(n, eol - n);
      if (num.empty() || num.size() > 6 || !std::all_of(num.begin(), num.end(), [](char c) { return c >= '0' && c <= '9'; }))
         return -1;
      if (bodyAt != nullptr)
         *bodyAt = eol + 1;
      return std::atoi(num.c_str());
   }

   template <class T>
   void KeepIf(std::vector<T>& v, const std::set<int>& keep, int T::*a, int T::*b)
   {
      v.erase(std::remove_if(v.begin(), v.end(),
                             [&](const T& r) { return !keep.count(r.*a) || !keep.count(r.*b); }),
              v.end());
   }
}

std::string NodeClipboardSerialize(const std::set<int>& indices)
{
   Patch::Data all = BuildPatchData();
   Patch::Data d;
   std::set<int> keep;
   for (const Patch::NodeRecord& rec : all.nodes)
      if (indices.count(rec.index))
      {
         d.nodes.push_back(rec);
         d.nodes.back().id.clear(); // a name is unique per patch; the copy would collide with its original
         keep.insert(rec.index);
      }
   if (d.nodes.empty())
      return std::string();
   d.cables = all.cables;
   d.geometry = all.geometry;
   d.audio = all.audio;
   d.notes = all.notes;
   d.modulation = all.modulation;
   d.palette = all.palette;
   KeepIf(d.cables, keep, &Patch::CableRecord::srcIndex, &Patch::CableRecord::dstIndex);
   KeepIf(d.geometry, keep, &Patch::CableRecord::srcIndex, &Patch::CableRecord::dstIndex);
   KeepIf(d.audio, keep, &Patch::CableRecord::srcIndex, &Patch::CableRecord::dstIndex);
   KeepIf(d.notes, keep, &Patch::CableRecord::srcIndex, &Patch::CableRecord::dstIndex);
   KeepIf(d.modulation, keep, &Patch::ModRecord::srcIndex, &Patch::ModRecord::dstIndex);
   KeepIf(d.palette, keep, &Patch::PaletteRecord::srcIndex, &Patch::PaletteRecord::dstIndex);
   for (const Patch::ExprRecord& e : all.expressions)
      if (keep.count(e.dstIndex))
         d.expressions.push_back(e);
   for (const Patch::GestureRecord& g : all.gestures)
      if (keep.count(g.dstIndex))
         d.gestures.push_back(g);

   std::string body;
   if (!Patch::WriteText(d, body))
      return std::string();
   return kHeader + std::to_string(Patch::FormatVersion()) + "\n" + body;
}

bool LooksLikeNodeClipboard(const std::string& text)
{
   return HeaderVersion(text, nullptr) >= 0;
}

NodePasteResult NodeClipboardPaste(const std::string& text, const ImVec2& at)
{
   NodePasteResult res;
   size_t bodyAt = 0;
   const int version = HeaderVersion(text, &bodyAt);
   if (version < 0)
   {
      res.message = T("Nothing to paste");
      return res;
   }
   if (version > Patch::FormatVersion())
   {
      res.message = T("Those nodes were copied from a newer Infinite; update to paste them");
      return res;
   }
   Patch::Data data;
   std::string err;
   if (!Patch::ReadText(text.substr(bodyAt), data, err))
   {
      res.message = std::string(T("Could not paste those nodes: ")) + err;
      return res;
   }
   if (data.nodes.empty())
   {
      res.message = T("Nothing to paste");
      return res;
   }

   // Centre of the copied nodes, to land them on `at`.
   float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
   for (const Patch::NodeRecord& rec : data.nodes)
   {
      minX = std::min(minX, rec.x); maxX = std::max(maxX, rec.x);
      minY = std::min(minY, rec.y); maxY = std::max(maxY, rec.y);
   }
   const float dx = at.x - (minX + maxX) * 0.5f, dy = at.y - (minY + maxY) * 0.5f;

   PushUndoCheckpoint("Paste");
   gSuppressUndoCheckpoints = true;
   std::map<int, int> remap;
   for (const Patch::NodeRecord& rec : data.nodes)
   {
      GraphNode* spawned = SpawnNode(rec.typeName, rec.category, rec.x + dx, rec.y + dy);
      if (spawned == nullptr)
      {
         res.skipped++;
         continue;
      }
      // The uid is not carried: it belongs to the node it was copied from, and SpawnNode minted a fresh one.
      remap[rec.index] = spawned->index;
      res.newIndices.push_back(spawned->index);
      spawned->showParams = rec.showParams;
      spawned->node->bypassed = rec.bypassed;
      spawned->showMiniViewport = rec.showMiniViewport;
      spawned->showAdvancedParams = rec.showAdvancedParams;
      if (auto* fgn = dynamic_cast<FieldGraphNode*>(spawned->node.get()))
         fgn->encapsulated = false;
      Patch::LoadParams(spawned->node.get(), rec.params);
      ReloadDerivedState(spawned->node.get());
      if (auto* geomOp = dynamic_cast<GeometryOpNode*>(spawned->node.get()))
         geomOp->MigrateDeprecatedOp();
      if (auto* rn = dynamic_cast<RandomNode*>(spawned->node.get()))
         rn->seed = RandomNode::NextSeed();
      if (auto* fgn = dynamic_cast<FieldGraphNode*>(spawned->node.get()))
         fgn->SetUid(FieldGraphNode::NewUid());
   }
   res.pasted = (int)res.newIndices.size();
   // Field graph ownership names the copied children by their old index; only the new nodes are rewritten.
   for (int idx : res.newIndices)
      if (GraphNode* gn = FindNodeByIndex(idx))
         if (auto* fgn = dynamic_cast<FieldGraphNode*>(gn->node.get()))
         {
            fgn->Ownership().Remap(remap);
            fgn->ownershipText = fgn->Ownership().ToText();
         }

   auto resolve = [&](int savedIndex) -> GraphNode*
   {
      auto it = remap.find(savedIndex);
      return it == remap.end() ? nullptr : FindNodeByIndex(it->second);
   };
   for (const Patch::CableRecord& c : data.cables)
   {
      GraphNode* dst = resolve(c.dstIndex);
      GraphNode* src = resolve(c.srcIndex);
      if (dst && src)
         if (ImageCable* cable = CableFor(*dst, c.dstSlot))
            cable->Connect(src->node.get(), c.srcOutput);
   }
   for (const Patch::CableRecord& c : data.geometry)
   {
      GraphNode* dst = resolve(c.dstIndex);
      GraphNode* src = resolve(c.srcIndex);
      if (dst && src)
         ConnectGeometrySlot(*dst, c.dstSlot, *src, c.srcOutput);
   }
   for (const Patch::CableRecord& c : data.audio)
   {
      GraphNode* dst = resolve(c.dstIndex);
      GraphNode* src = resolve(c.srcIndex);
      if (dst && src)
         if (AudioCable* cable = dst->node->AudioInputSlot(c.dstSlot))
            cable->Connect(src->node.get(), c.srcOutput);
   }
   for (const Patch::CableRecord& c : data.notes)
   {
      GraphNode* dst = resolve(c.dstIndex);
      GraphNode* src = resolve(c.srcIndex);
      if (dst && src)
         if (NoteCable* cable = dst->node->NoteInputSlot(c.dstSlot))
            cable->Connect(src->node.get(), c.srcOutput);
   }
   for (const Patch::ModRecord& m : data.modulation)
   {
      GraphNode* dst = resolve(m.dstIndex);
      GraphNode* src = resolve(m.srcIndex);
      if (!dst || !src)
         continue;
      Modulation::Source source;
      source.nodeIndex = src->index;
      source.outputIndex = m.srcOutput;
      source.polarity = m.polarity;
      source.depth = m.depth;
      source.centre = m.centre;
      source.lo = m.lo;
      source.hi = m.hi;
      source.hasRange = m.hasRange;
      source.enabled = m.enabled;
      source.curve = m.curve;
      Modulation::Instance().RestoreLink(dst->index, m.dstParam, source);
   }
   for (const Patch::PaletteRecord& c : data.palette)
   {
      GraphNode* dst = resolve(c.dstIndex);
      GraphNode* src = resolve(c.srcIndex);
      if (dst && src)
         PaletteBinding::Instance().Bind(dst->index, c.dstColor, src->index, c.srcSwatch);
   }
   for (const Patch::ExprRecord& e : data.expressions)
   {
      GraphNode* dst = resolve(e.dstIndex);
      if (!dst)
         continue;
      Modulation::Instance().SetExpression(dst->index, e.dstParam, e.text);
      if (std::abs(e.curve) > 0.0001f)
         Modulation::Instance().SetExpressionCurve(dst->index, e.dstParam, e.curve);
   }
   for (const Patch::GestureRecord& g : data.gestures)
   {
      GraphNode* dst = resolve(g.dstIndex);
      if (dst == nullptr || g.samples.size() < 2)
         continue;
      GestureRecorder::Playback pb;
      pb.speed = g.speed;
      pb.hasRangeOverride = g.hasRangeOverride;
      pb.rangeLo = g.rangeLo;
      pb.rangeHi = g.rangeHi;
      pb.curve = g.curve;
      for (const Patch::GestureSample& s : g.samples)
         pb.samples.push_back({ s.value, s.timeSec, s.startsNewGrab });
      pb.recordedMin = pb.recordedMax = pb.samples.front().value;
      for (const GestureRecorder::Sample& s : pb.samples)
      {
         pb.recordedMin = std::min(pb.recordedMin, s.value);
         pb.recordedMax = std::max(pb.recordedMax, s.value);
      }
      pb.startTime = GestureRecorder::Instance().ClockNow();
      GestureRecorder::Instance().SetPlayback(dst->index, g.dstParam, std::move(pb));
   }
   RebuildAudioTopology();
   gSuppressUndoCheckpoints = false;
   gPatchDirty = true;

   res.ok = res.pasted > 0;
   res.message = res.ok ? ("Pasted " + std::to_string(res.pasted) + (res.pasted == 1 ? " node" : " nodes") +
                           (res.skipped ? ", " + std::to_string(res.skipped) + " this build does not have were left out" : ""))
                        : T("None of those nodes exist in this build");
   return res;
}
}
