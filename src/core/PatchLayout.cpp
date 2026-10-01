#include "PatchLayout.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace PatchLayout
{
   namespace
   {
      constexpr float kGapX = 160.0f;
      constexpr float kGapY = 110.0f;
      constexpr float kBandGap = 260.0f;
      constexpr float kNoteGap = 36.0f;

      enum Band { kPicture = 0, kSound = 1, kModulation = 2, kBandCount = 3 };

      const char* const kBandNames[kBandCount] = { "Picture", "Sound", "Modulation" };

      struct TypeSize
      {
         const char* type;
         float w, h;
      };

      // Only the fallback: a measured size always wins.
      const TypeSize kTypeSizes[] = {
         { "Wavetable", 1000, 1260 }, { "Analog", 700, 900 }, { "Metallic", 460, 560 }, { "Oscillator", 700, 900 },
         { "Mixer", 740, 580 }, { "Reverb", 460, 480 }, { "Delay", 460, 480 }, { "Chorus", 460, 480 },
         { "Limiter", 460, 420 }, { "Drum Sequencer", 900, 900 }, { "MPC", 900, 900 }, { "Sampler", 700, 700 },
         { "Dynamics", 500, 520 }, { "EQ", 620, 560 }, { "Random Note Generator", 320, 300 },
         { "Arpeggiator", 340, 320 }, { "Note Sequencer", 900, 600 }, { "Chorder", 340, 320 },
         { "Audio Analyze", 250, 560 }, { "LFO", 250, 340 }, { "Envelope", 260, 340 }, { "Random", 250, 300 },
         { "Pattern", 420, 420 }, { "FieldPixel", 340, 380 }, { "Output", 340, 340 }, { "Audio Out", 300, 220 },
         { "Render 3D", 360, 420 }, { "Material", 320, 520 }, { "Camera", 300, 320 }, { "Light", 300, 320 },
      };

      const TypeSize kCategorySizes[] = {
         { "Source", 300, 300 }, { "Effects", 280, 240 }, { "Compositing", 280, 240 }, { "3D", 300, 320 },
         { "Utility", 320, 280 }, { "Modulators", 250, 320 }, { "Notes", 330, 300 }, { "Synths", 700, 800 },
         { "AudioEffects", 460, 480 }, { "Macros", 260, 260 }, { "Prediction", 360, 360 },
      };

      Band BandOf(const Patch::NodeRecord& n)
      {
         const std::string& c = n.category;
         if (c == "Notes" || c == "Synths" || c == "AudioEffects")
            return kSound;
         if (c == "Modulators" || c == "Macros" || c == "Prediction")
            return kModulation;
         // Utility holds both audio plumbing (Audio Out, Mixer) and the picture's
         // sinks; only the sinks belong with the picture.
         if (c == "Utility")
         {
            const std::string& t = n.typeName;
            if (t == "Output" || t == "Syphon Out" || t == "Field Graph" || t == "Projection" || t == "Viewport")
               return kPicture;
            return kSound;
         }
         return kPicture;
      }

      bool IsComment(const Patch::NodeRecord& n) { return n.typeName == "Comment"; }

      // The `f width` / `f height` params a Comment carries.
      float ParamFloat(const Patch::NodeRecord& n, const char* key, float fallback)
      {
         const std::string want = std::string("f ") + key;
         for (const auto& p : n.params)
            if (p.first == want)
            {
               char* end = nullptr;
               const float v = std::strtof(p.second.c_str(), &end);
               if (end != p.second.c_str() && v > 0.0f)
                  return v;
            }
         return fallback;
      }

      struct Item
      {
         const Patch::NodeRecord* rec = nullptr;
         Band band = kPicture;
         bool comment = false;
         float w = 0.0f, h = 0.0f;
         int depth = 0;
      };

      bool HintIs(const Patch::NodeRecord& c, const std::string& prefix, const Patch::NodeRecord& target)
      {
         if (c.layoutHint.compare(0, prefix.size(), prefix) != 0)
            return false;
         const std::string who = c.layoutHint.substr(prefix.size());
         return who == target.id || who == std::to_string(target.index);
      }
   }

   bool NeedsLayout(const Patch::Data& data)
   {
      if (data.nodes.empty())
         return false;
      for (const Patch::NodeRecord& n : data.nodes)
         if (n.hasPos)
            return false;
      return true;
   }

   Size EstimateSize(const Patch::NodeRecord& node)
   {
      if (IsComment(node))
         return { ParamFloat(node, "width", 260.0f), ParamFloat(node, "height", 140.0f) };
      for (const TypeSize& t : kTypeSizes)
         if (node.typeName == t.type)
            return { t.w, t.h };
      for (const TypeSize& t : kCategorySizes)
         if (node.category == t.type)
            return { t.w, t.h };
      return { 300.0f, 300.0f };
   }

   std::map<int, Pos> Compute(const Patch::Data& data, const std::map<int, Size>* measured)
   {
      std::vector<Item> items;
      items.reserve(data.nodes.size());
      std::map<int, size_t> byIndex;
      for (const Patch::NodeRecord& n : data.nodes)
      {
         Item it;
         it.rec = &n;
         it.comment = IsComment(n);
         it.band = BandOf(n);
         const Size est = EstimateSize(n);
         it.w = est.w;
         it.h = est.h;
         if (measured != nullptr)
         {
            auto m = measured->find(n.index);
            if (m != measured->end() && m->second.w > 0.0f && m->second.h > 0.0f)
            {
               it.w = m->second.w;
               it.h = m->second.h;
            }
         }
         byIndex[n.index] = items.size();
         items.push_back(it);
      }

      // Depth within a band: longest path over same-band edges. Bounded passes,
      // so a feedback loop settles instead of spinning.
      struct Edge { size_t from, to; };
      std::vector<Edge> edges;
      for (const auto* list : { &data.cables, &data.geometry, &data.audio, &data.notes })
         for (const Patch::CableRecord& c : *list)
         {
            auto s = byIndex.find(c.srcIndex);
            auto d = byIndex.find(c.dstIndex);
            if (s != byIndex.end() && d != byIndex.end() && !items[s->second].comment && !items[d->second].comment)
               edges.push_back({ s->second, d->second });
         }
      for (size_t pass = 0; pass < items.size(); ++pass)
      {
         bool changed = false;
         for (const Edge& e : edges)
            if (items[e.from].band == items[e.to].band && items[e.to].depth < items[e.from].depth + 1 &&
                items[e.from].depth < (int)items.size())
            {
               items[e.to].depth = items[e.from].depth + 1;
               changed = true;
            }
         if (!changed)
            break;
      }

      auto findComment = [&](const std::string& prefix, const Patch::NodeRecord& target) -> const Item*
      {
         for (const Item& c : items)
            if (c.comment && HintIs(*c.rec, prefix, target))
               return &c;
         return nullptr;
      };

      std::map<int, Pos> out;
      float bandTop = 0.0f;
      for (int b = 0; b < kBandCount; ++b)
      {
         std::vector<Item*> members;
         for (Item& it : items)
            if (!it.comment && it.band == b)
               members.push_back(&it);
         if (members.empty())
            continue;

         const Item* header = nullptr;
         for (const Item& c : items)
            if (c.comment && c.rec->layoutHint == std::string("band ") + kBandNames[b])
            {
               header = &c;
               break;
            }
         const float top = bandTop + (header != nullptr ? header->h + kNoteGap : 0.0f);

         // Modulators have no useful wiring depth: one column each, a row.
         std::map<int, std::vector<Item*>> columns;
         for (size_t k = 0; k < members.size(); ++k)
            columns[b == kModulation ? (int)k : members[k]->depth].push_back(members[k]);

         float x = 0.0f, bandHeight = 0.0f;
         for (auto& col : columns)
         {
            float y = top, colW = 0.0f;
            for (Item* it : col.second)
            {
               colW = std::max(colW, it->w);
               if (const Item* note = findComment("near ", *it->rec))
                  colW = std::max(colW, note->w);
            }
            for (Item* it : col.second)
            {
               if (const Item* note = findComment("near ", *it->rec))
               {
                  out[note->rec->index] = { x, y };
                  y += note->h + kNoteGap;
               }
               out[it->rec->index] = { x, y };
               y += it->h + kGapY;
            }
            bandHeight = std::max(bandHeight, y - kGapY - top);
            x += colW + kGapX;
         }
         if (header != nullptr)
            out[header->rec->index] = { 0.0f, bandTop };
         bandTop = top + bandHeight + kBandGap;
      }

      // Comments whose target is missing: park them top-left, clear of the graph.
      for (const Item& it : items)
         if (it.comment && out.find(it.rec->index) == out.end())
            out[it.rec->index] = { -it.w - kGapX, 0.0f };
      return out;
   }

   bool Apply(Patch::Data& data, const std::map<int, Size>* measured)
   {
      if (!NeedsLayout(data))
         return false;
      const std::map<int, Pos> pos = Compute(data, measured);
      for (Patch::NodeRecord& n : data.nodes)
      {
         auto p = pos.find(n.index);
         if (p != pos.end())
         {
            n.x = p->second.x;
            n.y = p->second.y;
         }
         n.hasPos = true;
      }
      return true;
   }
}
