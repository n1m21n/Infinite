#include "plugin/ipc/PluginIpc.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>
#include <thread>

namespace PluginIpc
{
   namespace
   {
      uint64_t Align64(uint64_t v) { return (v + 63) & ~uint64_t(63); }

      uint32_t NextPow2(uint32_t v)
      {
         uint32_t p = 1;
         while (p < v) p <<= 1;
         return p;
      }

      // Copies n frames starting at absolute frame `pos` out of / into a ring of `size` frames.
      void RingRead(const float* ring, uint32_t size, uint64_t pos, float* dst, uint32_t n)
      {
         const uint32_t a = uint32_t(pos & (size - 1));
         const uint32_t first = std::min(n, size - a);
         memcpy(dst, ring + a, first * sizeof(float));
         memcpy(dst + first, ring, (n - first) * sizeof(float));
      }

      void RingWrite(float* ring, uint32_t size, uint64_t pos, const float* src, uint32_t n)
      {
         const uint32_t a = uint32_t(pos & (size - 1));
         const uint32_t first = std::min(n, size - a);
         if (src)
         {
            memcpy(ring + a, src, first * sizeof(float));
            memcpy(ring, src + first, (n - first) * sizeof(float));
         }
         else
         {
            memset(ring + a, 0, first * sizeof(float));
            memset(ring, 0, (n - first) * sizeof(float));
         }
      }

      struct ChunkHeader
      {
         uint32_t frameBytes; // header + payload, before padding
         uint32_t type, id;
         uint32_t total, offset, len;
      };

      constexpr uint64_t kRingMask = kMessageRingBytes - 1;

      void BytesWrite(ByteRing* r, uint64_t pos, const void* src, size_t n)
      {
         const uint64_t a = pos & kRingMask;
         const size_t first = std::min<size_t>(n, kMessageRingBytes - a);
         memcpy(r->data + a, src, first);
         memcpy(r->data, static_cast<const uint8_t*>(src) + first, n - first);
      }

      void BytesRead(const ByteRing* r, uint64_t pos, void* dst, size_t n)
      {
         const uint64_t a = pos & kRingMask;
         const size_t first = std::min<size_t>(n, kMessageRingBytes - a);
         memcpy(dst, r->data + a, first);
         memcpy(static_cast<uint8_t*>(dst) + first, r->data, n - first);
      }

      uint64_t Pad8(uint64_t v) { return (v + 7) & ~uint64_t(7); }
   }

   Layout ComputeLayout(uint32_t maxBlock)
   {
      Layout l{};
      l.ringFrames = std::max<uint32_t>(4096, NextPow2(4 * std::max<uint32_t>(maxBlock, 1)));
      l.slots = Align64(sizeof(Header));
      l.inRing = Align64(l.slots + uint64_t(kSlots) * sizeof(BlockIn));
      l.outRing = Align64(l.inRing + uint64_t(kInChannels) * l.ringFrames * sizeof(float));
      l.toEngine = Align64(l.outRing + uint64_t(kOutChannels) * l.ringFrames * sizeof(float));
      l.toShell = Align64(l.toEngine + sizeof(ByteRing));
      l.total = Align64(l.toShell + sizeof(ByteRing));
      return l;
   }

   // ---- Channel ------------------------------------------------------------------------------

   bool Channel::WriteChunk(const void* hdr, size_t hl, const void* data, size_t len, int timeoutMs)
   {
      const uint64_t need = Pad8(hl + len);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
      for (;;)
      {
         const uint64_t head = out_->head.load(std::memory_order_relaxed);
         const uint64_t tail = out_->tail.load(std::memory_order_acquire);
         if (kMessageRingBytes - (head - tail) >= need)
         {
            BytesWrite(out_, head, hdr, hl);
            if (len) BytesWrite(out_, head + hl, data, len);
            out_->head.store(head + need, std::memory_order_release);
            out_->wake.fetch_add(1, std::memory_order_release);
            Ipc::SharedWake(&out_->wake);
            return true;
         }
         if (std::chrono::steady_clock::now() >= deadline) return false;
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
   }

   bool Channel::Send(uint32_t type, uint32_t id, const void* data, size_t len, int timeoutMs)
   {
      if (!out_ || len > 0xffffffffu) return false;
      const auto* p = static_cast<const uint8_t*>(data);
      uint32_t off = 0;
      do
      {
         const uint32_t n = uint32_t(std::min<size_t>(kMessageChunk, len - off));
         ChunkHeader h{uint32_t(sizeof(ChunkHeader) + n), type, id, uint32_t(len), off, n};
         if (!WriteChunk(&h, sizeof h, p ? p + off : nullptr, n, timeoutMs)) return false;
         off += n;
      } while (off < len);
      return true;
   }

   bool Channel::Receive(Message* msg, int timeoutMs)
   {
      if (!in_) return false;
      const uint64_t start = Ipc::MonotonicNs();
      const uint64_t budget = uint64_t(timeoutMs < 0 ? 0 : timeoutMs) * 1000000ull;
      for (;;)
      {
         const uint32_t w = in_->wake.load(std::memory_order_acquire);
         const uint64_t tail = in_->tail.load(std::memory_order_relaxed);
         const uint64_t head = in_->head.load(std::memory_order_acquire);
         if (tail != head)
         {
            ChunkHeader h;
            BytesRead(in_, tail, &h, sizeof h);
            if (h.offset == 0)
            {
               partial_.type = h.type;
               partial_.id = h.id;
               partial_.data.resize(h.total);
               inPartial_ = true;
            }
            const bool fits = inPartial_ && h.type == partial_.type && h.id == partial_.id &&
                              uint64_t(h.offset) + h.len <= partial_.data.size();
            if (fits && h.len) BytesRead(in_, tail + sizeof h, partial_.data.data() + h.offset, h.len);
            in_->tail.store(tail + Pad8(h.frameBytes), std::memory_order_release);
            if (fits && h.offset + h.len == h.total)
            {
               inPartial_ = false;
               *msg = std::move(partial_);
               partial_ = Message{};
               return true;
            }
            if (!fits) inPartial_ = false; // a torn message (sender timed out): drop it
            continue;
         }
         const uint64_t el = Ipc::MonotonicNs() - start;
         if (el >= budget) return false;
         Ipc::SharedWait(&in_->wake, w, budget - el);
      }
   }

   // ---- ShellSide ----------------------------------------------------------------------------

   bool ShellSide::Create(double sampleRate, uint32_t maxBlock)
   {
      Close();
      if (maxBlock == 0 || maxBlock > kMaxBlockLimit || sampleRate <= 0.0) return false;
      const Layout l = ComputeLayout(maxBlock);
      if (!Ipc::SharedMemoryCreate(Ipc::SharedMemoryUniqueName("infp"), size_t(l.total), &shm_)) return false;
      auto* base = static_cast<uint8_t*>(shm_.data);
      h_ = new (base) Header();
      h_->magic = kMagic;
      h_->version = kVersion;
      h_->totalBytes = l.total;
      h_->sampleRate = sampleRate;
      h_->maxBlock = maxBlock;
      h_->ringFrames = l.ringFrames;
      h_->slotsOffset = l.slots;
      h_->inRingOffset = l.inRing;
      h_->outRingOffset = l.outRing;
      h_->toEngineOffset = l.toEngine;
      h_->toShellOffset = l.toShell;
      slots_ = reinterpret_cast<BlockIn*>(base + l.slots);
      inRing_ = reinterpret_cast<float*>(base + l.inRing);
      outRing_ = reinterpret_cast<float*>(base + l.outRing);
      auto* toEngine = new (base + l.toEngine) ByteRing();
      auto* toShell = new (base + l.toShell) ByteRing();
      chan_.Attach(toEngine, toShell);
      posted_ = seq_ = 0;
      lastBeat_ = 0;
      lastBeatNs_ = Ipc::MonotonicNs();
      return true;
   }

   void ShellSide::Close()
   {
      Ipc::SharedMemoryClose(&shm_);
      h_ = nullptr;
      lateMark_ = ~0ull;
      slots_ = nullptr;
      inRing_ = outRing_ = nullptr;
      chan_.Attach(nullptr, nullptr);
   }

   bool ShellSide::EngineAttached() const
   {
      return h_ && h_->attached.load(std::memory_order_acquire) != 0;
   }

   bool ShellSide::WaitAttached(int timeoutMs, const Ipc::Child* engine)
   {
      for (int i = 0; i < timeoutMs; ++i)
      {
         if (EngineAttached()) return true;
         if (engine && !Ipc::ChildAlive(*engine)) return false;
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      return EngineAttached();
   }

   uint64_t ShellSide::HeartbeatAgeMs()
   {
      if (!h_) return 0;
      const uint64_t now = Ipc::MonotonicNs();
      const uint64_t beat = h_->heartbeat.load(std::memory_order_relaxed);
      if (beat != lastBeat_)
      {
         lastBeat_ = beat;
         lastBeatNs_ = now;
      }
      return (now - lastBeatNs_) / 1000000ull;
   }

   bool ShellSide::WaitDone(uint64_t timeoutNs)
   {
      if (!h_) return false;
      const uint64_t t0 = Ipc::MonotonicNs();
      for (;;)
      {
         const uint32_t w = h_->doneWake.load(std::memory_order_acquire);
         if (h_->consumedSeq.load(std::memory_order_acquire) >= seq_) return true;
         const uint64_t el = Ipc::MonotonicNs() - t0;
         if (el >= timeoutNs) return false;
         Ipc::SharedWait(&h_->doneWake, w, timeoutNs - el);
      }
   }

   void ShellSide::RequestQuit()
   {
      if (!h_) return;
      h_->quit.store(1, std::memory_order_release);
      h_->postedWake.fetch_add(1, std::memory_order_release);
      Ipc::SharedWake(&h_->postedWake);
   }

   Result ShellSide::Process(const ProcessArgs& a)
   {
      const uint32_t n = a.frames;
      auto silence = [&] {
         for (uint32_t c = 0; c < a.numOut; ++c)
            if (a.out[c]) memset(a.out[c], 0, n * sizeof(float));
      };
      if (!h_ || !EngineAttached() || n == 0 || n > h_->maxBlock)
      {
         silence();
         return Result::NoEngine;
      }
      const uint32_t R = h_->ringFrames;
      const int64_t L = h_->maxBlock;
      const bool offline = (a.flags & kOffline) != 0;
      const uint64_t P = posted_;

      // 1. post block [P, P+n), unless a stalled engine means the slots or input ring would overrun.
      const uint64_t consumed = h_->consumedSeq.load(std::memory_order_acquire);
      const uint64_t doneF = h_->doneFrames.load(std::memory_order_acquire);
      // Nothing in flight (engine caught up after a skip): always post; the engine zero-fills the gap.
      const bool idle = consumed == seq_;
      const bool canPost = idle || (seq_ - consumed < kSlots && P + n - std::min(doneF, P) <= uint64_t(R - L));
      posted_ = P + n;
      if (!canPost)
      {
         h_->xruns.fetch_add(1, std::memory_order_relaxed);
         silence();
         return Result::Skipped;
      }
      for (uint32_t c = 0; c < kInChannels; ++c)
      {
         const float* src = c < a.numIn ? a.in[c] : nullptr;
         if (!src && c == 1 && a.numIn >= 1) src = a.in[0]; // mono main input feeds both sides
         RingWrite(inRing_ + size_t(c) * R, R, P, src, n);
      }
      BlockIn& s = slots_[seq_ & (kSlots - 1)];
      s.startFrame = P;
      s.frames = n;
      s.flags = a.flags;
      s.transport = a.transport;
      s.numEvents = std::min(a.numEvents, kMaxEvents);
      if (s.numEvents) memcpy(s.events, a.events, s.numEvents * sizeof(Event));
      s.numParams = std::min(a.numParams, kMaxParamPoints);
      if (s.numParams) memcpy(s.params, a.params, s.numParams * sizeof(ParamPoint));
      ++seq_;
      h_->postedSeq.store(seq_, std::memory_order_release);
      h_->postedWake.fetch_add(1, std::memory_order_release);
      Ipc::SharedWake(&h_->postedWake);

      // 2. read output [P-L, P-L+n); wait only when the engine is behind.
      const int64_t from = int64_t(P) - L;
      const int64_t need = from + int64_t(n);
      const uint64_t doneNow = h_->doneFrames.load(std::memory_order_acquire);
      bool ready = need <= 0 || int64_t(doneNow) >= need;
      // Realtime: after a miss, don't wait again until the engine shows progress, so a dead or
      // hung engine costs the host thread nothing per block.
      const bool stuck = !offline && lateMark_ != ~0ull && doneNow == lateMark_;
      if (!ready && !stuck)
      {
         const uint64_t budget = offline ? 0 : uint64_t(0.6 * 1e9 * n / h_->sampleRate);
         const uint64_t t0 = Ipc::MonotonicNs();
         while (!ready)
         {
            const uint32_t w = h_->doneWake.load(std::memory_order_acquire);
            ready = int64_t(h_->doneFrames.load(std::memory_order_acquire)) >= need;
            if (ready) break;
            uint64_t left;
            if (budget)
            {
               const uint64_t el = Ipc::MonotonicNs() - t0;
               if (el >= budget) break;
               left = budget - el;
            }
            else
            {
               // Offline: no deadline, but stop if the engine has stopped beating for 3 s.
               if (HeartbeatAgeMs() > 3000) break;
               left = 200000000ull;
            }
            Ipc::SharedWait(&h_->doneWake, w, left);
         }
      }
      if (!ready)
      {
         lateMark_ = h_->doneFrames.load(std::memory_order_acquire);
         h_->xruns.fetch_add(1, std::memory_order_relaxed);
         silence();
         return Result::Late;
      }
      lateMark_ = ~0ull;
      for (uint32_t c = 0; c < a.numOut; ++c)
      {
         if (!a.out[c]) continue;
         const float* ring = outRing_ + size_t(std::min(c, kOutChannels - 1)) * R;
         uint32_t lead = 0;
         if (from < 0)
         {
            lead = uint32_t(std::min<int64_t>(-from, n));
            memset(a.out[c], 0, lead * sizeof(float));
         }
         if (lead < n) RingRead(ring, R, uint64_t(from + lead), a.out[c] + lead, n - lead);
      }
      return Result::Ok;
   }

   // ---- EngineSide ---------------------------------------------------------------------------

   bool EngineSide::Open(const std::string& name)
   {
      Close();
      // Map the header first to learn the size, then the whole region.
      Ipc::SharedMemory probe;
      if (!Ipc::SharedMemoryOpen(name, sizeof(Header), &probe)) return false;
      const auto* ph = static_cast<const Header*>(probe.data);
      const bool ok = ph->magic == kMagic && ph->version == kVersion;
      const uint64_t total = ph->totalBytes;
      Ipc::SharedMemoryClose(&probe);
      if (!ok || !Ipc::SharedMemoryOpen(name, size_t(total), &shm_)) return false;
      auto* base = static_cast<uint8_t*>(shm_.data);
      h_ = reinterpret_cast<Header*>(base);
      slots_ = reinterpret_cast<BlockIn*>(base + h_->slotsOffset);
      inRing_ = reinterpret_cast<float*>(base + h_->inRingOffset);
      outRing_ = reinterpret_cast<float*>(base + h_->outRingOffset);
      chan_.Attach(reinterpret_cast<ByteRing*>(base + h_->toShellOffset),
                   reinterpret_cast<ByteRing*>(base + h_->toEngineOffset));
      scratch_.assign(size_t(kInChannels + kOutChannels) * h_->maxBlock, 0.f);
      read_ = h_->consumedSeq.load(std::memory_order_acquire);
      done_ = h_->doneFrames.load(std::memory_order_acquire);
      seenWake_ = h_->postedWake.load(std::memory_order_acquire);
      return true;
   }

   void EngineSide::Close()
   {
      Ipc::SharedMemoryClose(&shm_);
      h_ = nullptr;
      chan_.Attach(nullptr, nullptr);
   }

   void EngineSide::MarkAttached()
   {
      if (h_) h_->attached.store(1, std::memory_order_release);
   }

   bool EngineSide::NextBlock(BlockView* v, uint64_t timeoutNs)
   {
      if (!h_) return false;
      const uint64_t start = Ipc::MonotonicNs();
      for (;;)
      {
         Beat();
         if (QuitRequested()) return false;
         const uint32_t w = h_->postedWake.load(std::memory_order_acquire);
         if (h_->postedSeq.load(std::memory_order_acquire) > read_) break;
         const uint64_t el = Ipc::MonotonicNs() - start;
         if (timeoutNs && el >= timeoutNs) return false;
         uint64_t left = timeoutNs ? timeoutNs - el : 100000000ull;
         left = std::min<uint64_t>(left, 100000000ull); // keep the heartbeat going
         Ipc::SharedWait(&h_->postedWake, w, left);
      }
      const BlockIn& s = slots_[read_ & (kSlots - 1)];
      const uint32_t R = h_->ringFrames;
      const uint32_t n = std::min(s.frames, h_->maxBlock);
      v->in = &s;
      v->frames = n;
      v->gapBefore = false;
      if (s.startFrame > done_)
      {
         // Resync: the shell skipped blocks while we were stalled. Zero what it never posted so
         // that old audio in the ring is never played again.
         const uint64_t gap = std::min<uint64_t>(s.startFrame - done_, R);
         for (uint32_t c = 0; c < kOutChannels; ++c)
         {
            uint64_t pos = s.startFrame - gap;
            uint64_t left = gap;
            while (left)
            {
               const uint32_t k = uint32_t(std::min<uint64_t>(left, h_->maxBlock));
               RingWrite(outRing_ + size_t(c) * R, R, pos, nullptr, k);
               pos += k;
               left -= k;
            }
         }
         h_->gaps.fetch_add(1, std::memory_order_relaxed);
         v->gapBefore = true;
      }
      const uint32_t mb = h_->maxBlock;
      for (uint32_t c = 0; c < kInChannels; ++c)
      {
         v->input[c] = scratch_.data() + size_t(c) * mb;
         RingRead(inRing_ + size_t(c) * R, R, s.startFrame, v->input[c], n);
      }
      for (uint32_t c = 0; c < kOutChannels; ++c)
      {
         v->output[c] = scratch_.data() + size_t(kInChannels + c) * mb;
         memset(v->output[c], 0, n * sizeof(float));
      }
      return true;
   }

   void EngineSide::Complete(const BlockView& v)
   {
      if (!h_ || !v.in) return;
      const uint32_t R = h_->ringFrames;
      for (uint32_t c = 0; c < kOutChannels; ++c)
         RingWrite(outRing_ + size_t(c) * R, R, v.in->startFrame, v.output[c], v.frames);
      done_ = v.in->startFrame + v.frames;
      ++read_;
      h_->doneFrames.store(done_, std::memory_order_release);
      h_->consumedSeq.store(read_, std::memory_order_release);
      h_->doneWake.fetch_add(1, std::memory_order_release);
      Ipc::SharedWake(&h_->doneWake);
   }
}
