// INFINITE_FACTORYPRESETTEST: the factory preset table (core/FactoryPresets).
//
// For every node type in the table:
//   COUNT    at least 8 presets, names unique
//   KEYS     every key a preset sets is a key the node saves
//   APPLY    after Apply the node saves back the preset's values
//   DISTINCT no two presets of a type save the same params
//   AUDIBLE  synths: a held note 60 through a real NoteEventQueue is finite and
//            over -60 dBFS RMS in its first 0.6 s
//            effects: 2 s of noise bursts comes out finite, over -60 dBFS, and
//            not equal to the input
// Headless (runs before glfwInit).
#include <sstream>
#include "app/AppShared.h"
#include "audio/AudioBuffer.h"
#include "audio/NoteEventQueue.h"
#include "core/FactoryPresets.h"
#include "nodes/AudioEffectNode.h"

#include <chrono>
#include <cmath>
#include <set>
#include <thread>

namespace app
{
namespace
{
   constexpr int kBlock = 256;
   constexpr double kRate = 48000.0;
   constexpr double kAudibleDbfs = -60.0;

   using Params = std::vector<std::pair<std::string, std::string>>;

   const std::string* Find(const Params& p, const std::string& key)
   {
      for (const auto& kv : p)
         if (kv.first.size() > 2 && kv.first.compare(2, std::string::npos, key) == 0) // "f cutoff"
            return &kv.second;
      return nullptr;
   }

   bool SameValue(const std::string& a, const std::string& b)
   {
      if (a == b)
         return true;
      // A colour: compare each number.
      if (a.find(' ') != std::string::npos || b.find(' ') != std::string::npos)
      {
         std::istringstream ia(a), ib(b);
         std::string ta, tb;
         while (ia >> ta)
            if (!(ib >> tb) || !SameValue(ta, tb))
               return false;
         return !(ib >> tb);
      }
      char* ea = nullptr;
      char* eb = nullptr;
      const double da = strtod(a.c_str(), &ea);
      const double db = strtod(b.c_str(), &eb);
      if (ea == a.c_str() || eb == b.c_str())
         return false;
      return std::fabs(da - db) <= 1e-4 * std::max(1.0, std::fabs(db));
   }

   double Dbfs(double sumSq, size_t n) { return 10.0 * std::log10(std::max(1e-30, sumSq / (double)std::max<size_t>(1, n))); }

   struct Render
   {
      bool finite = true;
      double db = -300.0;
      bool differs = false;
   };

   Render RenderSynth(INode* node, AudioNode* audio)
   {
      Render out;
      NoteEventQueue queue;
      int frame = 1;
      audio->PrepareToPlay(kRate, kBlock);
      node->CookIfNeeded(frame++);
      const int cursor = queue.RegisterConsumer();
      int slot = 0;
      for (int i = 0; i < 8; i++)
         if (node->NoteInputSlot(i) != nullptr)
         {
            slot = i;
            break;
         }
      audio->SetNoteInbox(slot, &queue, cursor);
      NoteEvent e;
      e.note = 60;
      e.velocity = 1.0f;
      e.isNoteOn = true;
      e.voiceId = NextVoiceId();
      queue.Push(e);
      std::vector<float> l(kBlock), r(kBlock);
      float* ch[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = ch;
      buf.numChannels = 2;
      buf.numFrames = kBlock;
      double sum = 0.0;
      size_t n = 0;
      const int frames = (int)(kRate * 0.6);
      for (int done = 0; done < frames; done += kBlock)
      {
         node->CookIfNeeded(frame++);
         audio->ProcessBlock(nullptr, 0, buf);
         for (int i = 0; i < kBlock; i++)
         {
            if (!std::isfinite(l[(size_t)i]) || !std::isfinite(r[(size_t)i]))
               out.finite = false;
            sum += 0.5 * ((double)l[(size_t)i] * l[(size_t)i] + (double)r[(size_t)i] * r[(size_t)i]);
            n++;
         }
      }
      out.db = Dbfs(sum, n);
      out.differs = true;
      return out;
   }

   Render RenderEffect(INode* node, AudioNode* audio)
   {
      Render out;
      int frame = 1;
      audio->PrepareToPlay(kRate, kBlock);
      node->CookIfNeeded(frame++);
      if (AudioEffectNode* fx = dynamic_cast<AudioEffectNode*>(node))
         for (int t = 0; t < 400 && fx->KernelPreparing(); t++)
         {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            node->CookIfNeeded(frame++);
         }
      float il[kBlock], ir[kBlock], ol[kBlock], orr[kBlock];
      float* iCh[2] = { il, ir };
      float* oCh[2] = { ol, orr };
      uint32_t seed = 0x1234567u;
      double sum = 0.0, diff = 0.0;
      size_t n = 0;
      const int frames = (int)(kRate * 2.0);
      for (int done = 0; done < frames; done += kBlock)
      {
         for (int i = 0; i < kBlock; i++)
         {
            // 125 ms of -6 dBFS noise every 500 ms: level for the dynamics
            // presets, gaps for the delay and reverb tails.
            const int pos = (done + i) % (int)(kRate * 0.5);
            seed = seed * 1664525u + 1013904223u;
            const float noise = ((float)(seed >> 8) / 8388608.0f - 1.0f) * 0.5f;
            il[i] = ir[i] = pos < (int)(kRate * 0.125) ? noise : 0.0f;
         }
         AudioBuffer in { iCh, 2, kBlock };
         AudioBuffer o { oCh, 2, kBlock };
         const AudioBuffer* ins[1] = { &in };
         node->CookIfNeeded(frame++);
         audio->ProcessBlock(ins, 1, o);
         for (int i = 0; i < kBlock; i++)
         {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i]))
               out.finite = false;
            sum += 0.5 * ((double)ol[i] * ol[i] + (double)orr[i] * orr[i]);
            diff += ((double)ol[i] - il[i]) * ((double)ol[i] - il[i]);
            n++;
         }
      }
      out.db = Dbfs(sum, n);
      out.differs = Dbfs(diff, n) > -80.0;
      return out;
   }
}

bool RunFactoryPresetTest()
{
   int failures = 0;
   int total = 0;
   auto fail = [&](const std::string& type, const std::string& preset, const std::string& what) {
      printf("  [FAIL] %s / %s: %s\n", type.c_str(), preset.c_str(), what.c_str());
      failures++;
   };

   for (const std::string& type : FactoryPresets::Types())
   {
      const std::vector<FactoryPresets::Preset>& list = *FactoryPresets::For(type);
      std::unique_ptr<INode> probe(NodeFactory::Instance().MakeNode(type));
      if (!probe)
      {
         fail(type, "-", "no such node type");
         continue;
      }
      const bool isSynth = NodeFactory::Instance().CategoryOf(type) == "Synths";
      Params defaults;
      Patch::SaveParams(probe.get(), defaults);
      if (list.size() < 8)
         fail(type, "-", "fewer than 8 presets");

      std::set<std::string> names;
      std::vector<Params> saved;
      double quietest = 0.0;
      for (int i = 0; i < (int)list.size(); i++)
      {
         const FactoryPresets::Preset& p = list[(size_t)i];
         total++;
         if (!names.insert(p.name).second)
            fail(type, p.name, "duplicate name");
         for (const auto& kv : p.values)
            if (Find(defaults, kv.first) == nullptr)
               fail(type, p.name, "unknown key " + kv.first);

         std::unique_ptr<INode> node(NodeFactory::Instance().MakeNode(type));
         if (!FactoryPresets::Apply(node.get(), type, i))
         {
            fail(type, p.name, "Apply refused");
            continue;
         }
         Params after;
         Patch::SaveParams(node.get(), after);
         for (const auto& kv : p.values)
         {
            const std::string* got = Find(after, kv.first);
            if (got == nullptr || !SameValue(*got, kv.second))
               fail(type, p.name, kv.first + " reads back " + (got ? *got : std::string("(missing)")) + ", want " + kv.second);
         }
         for (size_t k = 0; k < saved.size(); k++)
            if (saved[k] == after)
               fail(type, p.name, "same params as " + list[k].name);
         saved.push_back(after);

         IAudioSource* src = dynamic_cast<IAudioSource*>(node.get());
         AudioNode* audio = src != nullptr ? src->GetAudioNode() : nullptr;
         if (src == nullptr)
            continue;   // a picture preset (Material): the read-back above is the check
         if (audio == nullptr)
         {
            fail(type, p.name, "no audio node");
            continue;
         }
         const Render r = isSynth ? RenderSynth(node.get(), audio) : RenderEffect(node.get(), audio);
         quietest = std::min(quietest, r.db);
         if (!r.finite)
            fail(type, p.name, "non-finite output");
         if (r.db < kAudibleDbfs)
            fail(type, p.name, "silent (" + std::to_string(r.db) + " dBFS)");
         if (!r.differs)
            fail(type, p.name, "output equals input");
      }
      if (dynamic_cast<IAudioSource*>(probe.get()) != nullptr)
         printf("  %-11s %2d presets, quietest %.1f dBFS\n", type.c_str(), (int)list.size(), quietest);
      else
         printf("  %-11s %2d presets\n", type.c_str(), (int)list.size());
   }

   printf("FACTORY PRESET TEST (%d presets, %d types): %s\n", total, (int)FactoryPresets::Types().size(),
          failures == 0 ? "OK" : "FAIL");
   return failures == 0;
}
}
