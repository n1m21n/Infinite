// A private patch rendered to a buffer, away from the live engine. See OfflineGraph.h.
#include "app/graph/OfflineGraph.h"

#include "audio/AudioEngine.h"
#include "nodes/AudioEffectNode.h"
#include "nodes/DrumSequencerNode.h"
#include "nodes/FieldSynthNode.h"
#include "nodes/GranularNode.h"
#include "nodes/PartialsNode.h"
#include "nodes/SamplerNode.h"
#include "core/NodeFactory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <unordered_map>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace app
{
namespace
{
   constexpr int kBlock = 512;
   // Motions move a param a little at a time; once per 4 blocks (43 ms at
   // 48k) is smoother than any LFO a style asks for, and cooks 4x less.
   constexpr int kMotionEveryBlocks = 4;
   // Longest wait for nodes that build in the background (a convolution
   // impulse, a Partials analysis) before rendering anyway.
   constexpr int kPrepareWaitMs = 3000;

   constexpr int kMaxNoteSlots = AudioNode::kMaxNoteSlots;

   // The audio node behind a node that makes or carries audio or notes (the visual-audio nodes never appear in a style's patch).
   AudioNode* AudioNodeOfAny(INode* node)
   {
      if (node == nullptr)
         return nullptr;
      if (auto* src = dynamic_cast<IAudioSource*>(node))
         return src->GetAudioNode();
      if (auto* noteSrc = dynamic_cast<INoteSource*>(node))
         return noteSrc->GetAudioNode();
      return node->AudioNodeForNotePorts();
   }

   // File-backed and compiled params load here, as on patch load, for the node types a style card places.
   void ReloadDerivedState(INode* node)
   {
      if (auto* sampler = dynamic_cast<SamplerNode*>(node))
         sampler->ReloadFromPath();
      if (auto* partials = dynamic_cast<PartialsNode*>(node))
         partials->ReloadFromPath();
      if (auto* gran = dynamic_cast<GranularNode*>(node))
         gran->ReloadFromPath();
      if (auto* drum = dynamic_cast<DrumSequencerNode*>(node))
         drum->ReloadFromPaths();
      if (auto* fsn = dynamic_cast<FieldSynthNode*>(node))
         fsn->Apply();
   }

   // True while a node is still making what it plays from, on its own job:
   // the headless render waits on the same thing (Headless.cpp).
   bool StillPreparing(INode* n)
   {
      if (auto* fx = dynamic_cast<AudioEffectNode*>(n))
         return fx->KernelPreparing();
      if (auto* p = dynamic_cast<PartialsNode*>(n))
         return p->IsAnalyzing();
      return false;
   }

   // The engine's own flush-to-zero setting (AudioEngine::Process): a decay
   // tail left to go denormal costs 100x per sample on the worker too.
   void SetFlushToZero()
   {
#if defined(__x86_64__)
      _mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__)
      uint64_t fpcr;
      __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
      fpcr |= (1ULL << 24);
      __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
   }
}

struct OfflineGraph::Owned
{
   int index = 0;
   std::unique_ptr<INode> node;
};

struct OfflineGraph::Entry
{
   INode* owner = nullptr;
   AudioNode* node = nullptr;
   int numInputs = 0;
   int inputs[kAudioMaxNodeInputs] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }; // buffer pair, -1 = silence
   int numOutputs = 1;
   int firstOutput = 0; // buffer pair of output 0
};

struct OfflineGraph::Wire
{
   INode* owner = nullptr; // the consumer
   AudioNode* consumer = nullptr;
   int slot = 0;
   NoteEventQueue* inbox = nullptr;
   int cursor = 0;
};

OfflineGraph::OfflineGraph() = default;
OfflineGraph::~OfflineGraph() = default;
int OfflineGraph::NodeCount() const { return (int)mNodes.size(); }

INode* OfflineGraph::NodeAt(int patchIndex) const
{
   for (const Owned& o : mNodes)
      if (o.index == patchIndex)
         return o.node.get();
   return nullptr;
}

bool OfflineGraph::Build(const Patch::Data& data, int tapIndex)
{
   mTransportRec = data.transport;
   for (const Patch::NodeRecord& rec : data.nodes)
   {
      INode* n = NodeFactory::Instance().MakeNode(rec.typeName);
      if (n == nullptr)
      {
         mError += "unknown type " + rec.typeName + "; ";
         continue;
      }
      Owned o;
      o.index = rec.index;
      o.node.reset(n);
      Patch::LoadParams(n, rec.params);
      ReloadDerivedState(n); // file-backed params (drum lanes, sampler paths) load here, as on patch load
      mNodes.push_back(std::move(o));
   }
   for (const Patch::CableRecord& c : data.audio)
   {
      INode* dst = NodeAt(c.dstIndex);
      INode* src = NodeAt(c.srcIndex);
      AudioCable* cable = dst != nullptr ? dst->AudioInputSlot(c.dstSlot) : nullptr;
      if (cable != nullptr && src != nullptr)
         cable->Connect(src, c.srcOutput);
   }
   for (const Patch::CableRecord& c : data.notes)
   {
      INode* dst = NodeAt(c.dstIndex);
      INode* src = NodeAt(c.srcIndex);
      NoteCable* cable = dst != nullptr ? dst->NoteInputSlot(c.dstSlot) : nullptr;
      if (cable != nullptr && src != nullptr)
         cable->Connect(src, c.srcOutput);
   }
   mTap = NodeAt(tapIndex);
   if (mTap == nullptr)
      mError += "no tap node; ";
   return mTap != nullptr;
}

// Depth-first from the tap, producers first: the order RebuildAudioTopology's
// CollectAudioChain gives the live engine. `visiting` doubles as the visited
// set; the graph has no cycles (WouldCreateAudioCycle never let one be drawn,
// and the compiler never writes one).
void OfflineGraph::Collect(INode* node, std::vector<INode*>& visiting)
{
   if (node == nullptr || std::find(visiting.begin(), visiting.end(), node) != visiting.end())
      return;
   visiting.push_back(node);

   Entry e;
   e.owner = node;
   for (int slot = 0; slot < kAudioMaxNodeInputs; slot++)
   {
      AudioCable* cable = node->AudioInputSlot(slot);
      if (cable == nullptr)
         continue;
      e.numInputs = slot + 1;
      if (cable->IsConnected())
         Collect(cable->GetSource(), visiting);
   }
   for (int slot = 0; slot < kMaxNoteSlots; slot++)
   {
      NoteCable* cable = node->NoteInputSlot(slot);
      if (cable != nullptr && cable->IsConnected())
         Collect(cable->GetSource(), visiting);
   }
   e.node = AudioNodeOfAny(node);
   if (e.node != nullptr)
      mOrder.push_back(e);
}

void OfflineGraph::Cook()
{
   ++mCookFrame;
   for (Entry& e : mOrder)
      e.owner->CookIfNeeded(mCookFrame);
}

bool OfflineGraph::Prepare(double sampleRate)
{
   mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
   ScopedTransport clock(mClock);
   mClock.SetTempo(mTransportRec.bpm);
   mClock.SetTimeSignature(mTransportRec.timeSigNum, mTransportRec.timeSigDen);
   mClock.SetKey(mTransportRec.key);
   mClock.SetScale(mTransportRec.scale);
   mClock.NotifyAudioEngineStarted(mSampleRate);
   mClock.SetPlaying(true);

   std::vector<INode*> visiting;
   Collect(mTap, visiting);
   if (mOrder.empty() || mOrder.back().owner != mTap)
   {
      mError += "tap is not an audio source; ";
      return false;
   }

   // Output buffers, and each input's source buffer.
   std::unordered_map<AudioNode*, int> firstOutput;
   int pairs = 0;
   for (Entry& e : mOrder)
   {
      e.numOutputs = std::clamp(e.node->AudioOutputCount(), 1, kAudioMaxNodeOutputs);
      e.firstOutput = pairs;
      firstOutput[e.node] = pairs;
      pairs += e.numOutputs;
   }
   mBuffers.assign((size_t)pairs * 2, std::vector<float>(kBlock, 0.0f));
   for (Entry& e : mOrder)
      for (int slot = 0; slot < e.numInputs; slot++)
      {
         AudioCable* cable = e.owner->AudioInputSlot(slot);
         if (cable == nullptr || !cable->IsConnected())
            continue;
         auto it = firstOutput.find(AudioNodeOfAny(cable->GetSource()));
         if (it != firstOutput.end())
            e.inputs[slot] = it->second + cable->GetOutputSlot();
      }

   // Cook, prepare, cook: PrepareToPlay seeds each node's smoothers from
   // what the first cook pushed (the DSP fixtures' order, DspFixtures3.cpp).
   Cook();
   for (Entry& e : mOrder)
   {
      e.node->PrepareToPlay(mSampleRate, kBlock);
      e.node->preparedForSampleRate = mSampleRate;
   }
   Cook();

   // Note wiring, applied here rather than described: nothing else owns
   // these queues. Cursor ids 0..n-1 per outbox, every consumer starting at
   // the tail, as AudioEngine::ApplyNoteWiringIfNew does for a fresh wire.
   std::unordered_map<NoteEventQueue*, int> consumers;
   std::vector<Wire>& wires = mWires;
   wires.clear();
   for (Entry& e : mOrder)
      for (int slot = 0; slot < kMaxNoteSlots; slot++)
      {
         NoteCable* cable = e.owner->NoteInputSlot(slot);
         if (cable == nullptr || !cable->IsConnected())
            continue;
         AudioNode* producer = AudioNodeOfAny(cable->GetSource());
         NoteEventQueue* inbox = producer != nullptr ? producer->NoteOutbox(cable->GetOutputSlot()) : nullptr;
         if (inbox == nullptr)
            continue;
         int& count = consumers[inbox];
         if (count >= NoteEventQueue::kMaxConsumers)
            continue;
         wires.push_back({ e.owner, e.node, slot, inbox, count++ });
      }
   for (const auto& [queue, count] : consumers)
   {
      size_t heads[NoteEventQueue::kMaxConsumers];
      for (int i = 0; i < count; i++)
         heads[i] = queue->Tail();
      queue->AdoptConsumers(count, heads);
   }
   for (const Wire& w : wires)
      w.consumer->ApplyNoteInbox(w.slot, w.inbox, w.cursor);
   return true;
}

void OfflineGraph::ApplyMotions(double beats)
{
   for (const Motion& m : mMotions)
   {
      INode* n = NodeAt(m.node);
      if (n == nullptr || m.periodBeats <= 0.0)
         continue;
      const double s = 0.5 + 0.5 * std::sin(2.0 * M_PI * (beats / m.periodBeats + m.phase));
      const float v = m.lo + (m.hi - m.lo) * (float)s;
      char text[32];
      snprintf(text, sizeof(text), "%g", v);
      Patch::LoadParams(n, { { m.key, text } });
   }
}

void OfflineGraph::ProcessEntry(Entry& e, int n)
{
   float* chans[kAudioMaxNodeInputs][2];
   AudioBuffer views[kAudioMaxNodeInputs];
   const AudioBuffer* ins[kAudioMaxNodeInputs];
   for (int i = 0; i < e.numInputs; i++)
   {
      const int p = e.inputs[i];
      if (p < 0)
      {
         ins[i] = nullptr;
         continue;
      }
      chans[i][0] = mBuffers[(size_t)p * 2].data();
      chans[i][1] = mBuffers[(size_t)p * 2 + 1].data();
      views[i] = AudioBuffer{ chans[i], 2, n };
      ins[i] = &views[i];
   }
   float* outChans[kAudioMaxNodeOutputs][2];
   AudioBuffer outViews[kAudioMaxNodeOutputs];
   AudioBuffer* outs[kAudioMaxNodeOutputs];
   for (int o = 0; o < e.numOutputs; o++)
   {
      const int p = e.firstOutput + o;
      outChans[o][0] = mBuffers[(size_t)p * 2].data();
      outChans[o][1] = mBuffers[(size_t)p * 2 + 1].data();
      std::fill(outChans[o][0], outChans[o][0] + n, 0.0f);
      std::fill(outChans[o][1], outChans[o][1] + n, 0.0f);
      outViews[o] = AudioBuffer{ outChans[o], 2, n };
      outs[o] = &outViews[o];
   }
   e.node->ProcessBlockMulti(ins, e.numInputs, outs, e.numOutputs);
   e.node->ApplyBlockGain(outs, e.numOutputs, n, mSampleRate);
}

bool OfflineGraph::Render(int frames, std::vector<float>& left, std::vector<float>& right,
                          const std::atomic<bool>* cancel, std::atomic<float>* progress)
{
   left.assign((size_t)std::max(0, frames), 0.0f);
   right.assign((size_t)std::max(0, frames), 0.0f);
   if (mOrder.empty())
      return true;
   SetFlushToZero();
   ScopedTransport clock(mClock);

   // An offline render outruns real time: wait for background builds, or
   // the start of the loop (or all of it) renders through a missing impulse.
   const auto waitUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(kPrepareWaitMs);
   for (;;)
   {
      bool preparing = false;
      for (Entry& e : mOrder)
         preparing = preparing || StillPreparing(e.owner);
      if (!preparing || std::chrono::steady_clock::now() > waitUntil)
         break;
      if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
         return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      Cook();
   }

   const Entry& tapEntry = mOrder.back();
   int done = 0, block = 0;
   while (done < frames)
   {
      if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
         return false;
      const int n = std::min(kBlock, frames - done);
      mClock.AdvanceAudioClock(n);
      if (!mMotions.empty() && block % kMotionEveryBlocks == 0)
      {
         ApplyMotions(mClock.BlockStartBeats());
         Cook();
      }
      for (Entry& e : mOrder)
         ProcessEntry(e, n);
      const float* l = mBuffers[(size_t)tapEntry.firstOutput * 2].data();
      const float* r = mBuffers[(size_t)tapEntry.firstOutput * 2 + 1].data();
      std::copy(l, l + n, left.begin() + done);
      std::copy(r, r + n, right.begin() + done);
      done += n;
      block++;
      if (progress != nullptr)
         progress->store((float)done / (float)frames, std::memory_order_relaxed);
   }
   return true;
}

void OfflineGraph::RenderNotes(double beats, std::vector<CapturedNote>& out)
{
   out.clear();
   if (mOrder.empty() || beats <= 0.0)
      return;
   ScopedTransport clock(mClock);
   const double beatsPerFrame = mClock.Tempo() / (60.0 * mSampleRate);
   const int frames = (int)std::ceil(beats / beatsPerFrame);

   // Only the voices' wires are read here: a note effect's own input is
   // drained by the effect, which runs. Each voice's cursor is ours, since
   // the voice never does.
   std::vector<const Wire*> voices;
   std::unordered_map<const INode*, int> indexOf;
   for (const Owned& o : mNodes)
      indexOf[o.node.get()] = o.index;
   for (const Wire& w : mWires)
      if (dynamic_cast<INoteSource*>(w.owner) == nullptr)
         voices.push_back(&w);

   std::unordered_map<int64_t, size_t> open; // (wire, voiceId) -> index in out
   NoteEvent events[NoteEventQueue::kCapacity];
   int done = 0;
   while (done < frames)
   {
      const int n = std::min(kBlock, frames - done);
      mClock.AdvanceAudioClock(n);
      const double b0 = mClock.BlockStartBeats();
      for (Entry& e : mOrder)
         if (dynamic_cast<INoteSource*>(e.owner) != nullptr)
            ProcessEntry(e, n);
      for (size_t w = 0; w < voices.size(); w++)
      {
         const int count = voices[w]->inbox->Pop(voices[w]->cursor, events, NoteEventQueue::kCapacity);
         for (int i = 0; i < count; i++)
         {
            const NoteEvent& ev = events[i];
            if (ev.bendUpdate)
               continue;
            const double at = b0 + ev.frameOffset * beatsPerFrame;
            const int64_t key = ((int64_t)w << 32) ^ (int64_t)(uint32_t)ev.voiceId;
            auto it = open.find(key);
            if (it != open.end())
            {
               // A note-off, or a note-on reusing a held id: either way the held note ends here.
               CapturedNote& c = out[it->second];
               c.lengthBeats = std::max(0.0, at - c.startBeats);
               open.erase(it);
            }
            if (ev.isNoteOn && at < beats)
            {
               CapturedNote c;
               c.startBeats = at;
               c.pitch = ev.note;
               c.velocity = ev.velocity;
               auto idx = indexOf.find(voices[w]->owner);
               c.voice = idx != indexOf.end() ? idx->second : -1;
               open[key] = out.size();
               out.push_back(c);
            }
         }
      }
      done += n;
   }
   for (const auto& [key, i] : open)
      out[i].lengthBeats = std::max(0.0, beats - out[i].startBeats);
   std::stable_sort(out.begin(), out.end(), [](const CapturedNote& a, const CapturedNote& b) {
      return a.startBeats != b.startBeats ? a.startBeats < b.startBeats : a.pitch < b.pitch;
   });
}

} // namespace app
