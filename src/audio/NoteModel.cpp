// Ported from upstream Infinite (github.com/n1m21n/Infinite) into Infinite-Turbo.
#include "audio/NoteModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace NoteModel
{
   namespace
   {
      constexpr int kAlphabet[kNumViewpoints] = { 128, kMaxAlphabet, kDurBins, kVelBins, kRelPitchAlphabet };

      uint64_t Mix(uint64_t h, uint64_t v)
      {
         h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
         h *= 0xBF58476D1CE4E5B9ull;
         h ^= h >> 31;
         return h;
      }
      constexpr uint64_t kTop = 1ull << 63;

      // hist[0] is the most recent symbol. Order 0 is keyed by the viewpoint's metric context.
      uint64_t CtxKey(int v, int order, const uint16_t* hist, int metricCtx)
      {
         uint64_t h = Mix(0x1234567ull + (uint64_t)v, (uint64_t)order);
         if (order == 0)
            return Mix(h, (uint64_t)metricCtx + 0x77) | kTop;
         for (int i = 0; i < order; i++)
            h = Mix(h, (uint64_t)hist[i] + 1);
         return h | kTop;
      }
      uint64_t PairKey(uint64_t ck, int sym) { return Mix(ck, (uint64_t)sym + 7) | kTop; }

      uint64_t Slot(uint64_t key, uint64_t mask) { return (key ^ (key >> 29)) & mask; }

      const CtxSlot* FindCtx(const ViewModel& m, uint64_t key)
      {
         if (m.ctx.empty())
            return nullptr;
         for (uint64_t i = Slot(key, m.mask);; i = (i + 1) & m.mask)
         {
            const CtxSlot& s = m.ctx[i];
            if (s.key == key)
               return &s;
            if (s.key == 0)
               return nullptr;
         }
      }
      uint32_t PairCount(const ViewModel& m, uint64_t pk)
      {
         if (m.pair.empty())
            return 0;
         for (uint64_t i = Slot(pk, m.mask);; i = (i + 1) & m.mask)
         {
            const PairSlot& s = m.pair[i];
            if (s.key == pk)
               return s.count;
            if (s.key == 0)
               return 0;
         }
      }

      void Add(ViewModel& m, uint64_t ck, int sym)
      {
         CtxSlot* c = nullptr;
         for (uint64_t i = Slot(ck, m.mask);; i = (i + 1) & m.mask)
            if (m.ctx[i].key == ck || m.ctx[i].key == 0)
            {
               c = &m.ctx[i];
               break;
            }
         c->key = ck;
         const uint64_t pk = PairKey(ck, sym);
         PairSlot* p = nullptr;
         for (uint64_t i = Slot(pk, m.mask);; i = (i + 1) & m.mask)
            if (m.pair[i].key == pk || m.pair[i].key == 0)
            {
               p = &m.pair[i];
               break;
            }
         p->key = pk;
         if (p->count == 0)
            c->distinct++;
         p->count++;
         c->total++;
         c->last = (uint16_t)sym;
      }

      // Symbols of one viewpoint for the whole event list, plus each position's order-0 metric context.
      struct Seq
      {
         std::vector<uint16_t> sym[kNumViewpoints];
         std::vector<uint8_t> metricCtx[kNumViewpoints];
         int first[kNumViewpoints] = { 0, 1, 0, 0, 0 }; // spacing of event 0 is undefined
      };
      Seq MakeSeq(const std::vector<Event>& ev)
      {
         Seq q;
         const int n = (int)ev.size();
         for (int v = 0; v < kNumViewpoints; v++)
         {
            q.sym[v].resize(n);
            q.metricCtx[v].resize(n);
         }
         for (int t = 0; t < n; t++)
         {
            q.sym[kPitch][t] = ev[t].note;
            q.sym[kIoi][t] = (uint16_t)std::min<int>(ev[t].ioiTicks, kMaxIoiTicks);
            q.sym[kDur][t] = (uint16_t)DurBin(ev[t].durTicks);
            q.sym[kVel][t] = (uint16_t)VelBin(ev[t].vel);
            q.sym[kPitchRel][t] = (uint16_t)RelPitchSymbol(ev[t].relPitch);
            q.metricCtx[kPitch][t] = 0;
            q.metricCtx[kIoi][t] = t > 0 ? ev[t - 1].metric : 0;
            q.metricCtx[kDur][t] = ev[t].metric;
            q.metricCtx[kVel][t] = ev[t].metric;
            q.metricCtx[kPitchRel][t] = 0;
         }
         return q;
      }

      // Keys for predicting position t of viewpoint v; returns the highest usable order.
      int KeysAt(const Seq& q, int v, int t, int maxOrder, uint64_t* keys)
      {
         const int avail = t - q.first[v];
         const int top = std::max(0, std::min(maxOrder, avail));
         uint16_t h[kMaxOrder];
         for (int i = 0; i < top; i++)
            h[i] = q.sym[v][t - 1 - i];
         for (int k = 0; k <= top; k++)
            keys[k] = CtxKey(v, k, h, q.metricCtx[v][t]);
         return top;
      }

      // PPM-C blend from the highest order down. No order -1 (uniform) term: symbols never seen in any
      // context get probability zero, which is what keeps output inside the learned pitch set.
      bool Blend(const ViewModel& m, const uint64_t* keys, int top, float* p)
      {
         const int A = m.alphabet;
         std::memset(p, 0, sizeof(float) * (size_t)A);
         float remaining = 1.0f;
         bool any = false;
         for (int k = top; k >= 0; k--)
         {
            const CtxSlot* c = FindCtx(m, keys[k]);
            if (c == nullptr || c->total == 0)
               continue;
            const float n = (float)c->total, d = (float)c->distinct;
            const float esc = d / (n + d);
            const float scale = remaining * (1.0f - esc) / n;
            for (int s = 0; s < A; s++)
            {
               const uint32_t cnt = PairCount(m, PairKey(keys[k], s));
               if (cnt)
                  p[s] += scale * (float)cnt;
            }
            remaining *= esc;
            any = true;
         }
         if (!any)
            return false;
         float sum = 0.0f;
         for (int s = 0; s < A; s++)
            sum += p[s];
         if (sum <= 0.0f)
            return false;
         const float inv = 1.0f / sum;
         for (int s = 0; s < A; s++)
            p[s] *= inv;
         return true;
      }

      int FoldPitch(int s, int lo, int hi)
      {
         if (s >= lo && s <= hi)
            return s;
         int t = s;
         while (t < lo)
            t += 12;
         while (t > hi)
            t -= 12;
         return (t >= lo && t <= hi) ? t : std::clamp(s, lo, hi);
      }

      const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string B64Encode(const uint8_t* d, size_t n)
      {
         std::string out;
         out.reserve((n + 2) / 3 * 4);
         for (size_t i = 0; i < n; i += 3)
         {
            const uint32_t b = ((uint32_t)d[i] << 16) | ((i + 1 < n ? d[i + 1] : 0) << 8) | (i + 2 < n ? d[i + 2] : 0);
            out += kB64[(b >> 18) & 63];
            out += kB64[(b >> 12) & 63];
            out += i + 1 < n ? kB64[(b >> 6) & 63] : '=';
            out += i + 2 < n ? kB64[b & 63] : '=';
         }
         return out;
      }
      bool B64Decode(const std::string& s, std::vector<uint8_t>& out)
      {
         auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
         };
         out.clear();
         uint32_t acc = 0;
         int bits = 0;
         for (char c : s)
         {
            if (c == '=')
               break;
            const int v = val(c);
            if (v < 0)
               return false;
            acc = (acc << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8)
            {
               bits -= 8;
               out.push_back((uint8_t)((acc >> bits) & 0xFF));
            }
         }
         return true;
      }
   }

   int DurBin(int ticks)
   {
      const double t = std::max(1, ticks);
      return std::clamp((int)std::lround(2.0 * std::log2(t / 2.0)), 0, kDurBins - 1);
   }
   int VelBin(int vel127) { return std::clamp(vel127 * kVelBins / 128, 0, kVelBins - 1); }
   int RelPitchSymbol(int semitones) { return std::clamp(semitones, -kRelPitchRange, kRelPitchRange) + kRelPitchRange; }
   int SymbolToRelPitch(int symbol) { return std::clamp(symbol, 0, kRelPitchAlphabet - 1) - kRelPitchRange; }
   int MetricOf(double beatInBar) { return std::clamp((int)std::floor(beatInBar * 4.0 + 1e-6), 0, 15); }
   int SnapTicks(double beats) { return (int)std::lround(beats * (double)kTicksPerBeat); }

   Tables* Build(const std::vector<Event>& eventsIn)
   {
      auto* T = new Tables();
      T->events = eventsIn;
      if ((int)T->events.size() > kMaxEvents)
         T->events.resize(kMaxEvents);
      const std::vector<Event>& ev = T->events;
      const int n = (int)ev.size();
      T->nEvents = n;
      T->maxOrder = kMaxOrder;
      const Seq q = MakeSeq(ev);

      uint64_t cap = 64;
      const uint64_t need = (uint64_t)std::max(1, n) * (uint64_t)(kMaxOrder + 1) * 2;
      while (cap < need)
         cap <<= 1;
      for (int v = 0; v < kNumViewpoints; v++)
      {
         ViewModel& m = T->vp[v];
         m.alphabet = kAlphabet[v];
         m.mask = cap - 1;
         m.ctx.assign(cap, CtxSlot());
         m.pair.assign(cap, PairSlot());
         for (int t = q.first[v]; t < n; t++)
         {
            uint64_t keys[kMaxOrder + 1];
            const int top = KeysAt(q, v, t, kMaxOrder, keys);
            for (int k = 0; k <= top; k++)
               Add(m, keys[k], q.sym[v][t]);
         }
         for (int i = 0; i < kMaxOrder; i++)
         {
            const int idx = n - 1 - i;
            T->tail[v][i] = idx >= 0 ? q.sym[v][idx] : 0;
         }
      }

      double dSum[kDurBins] = {}, vSum[kVelBins] = {};
      int dCnt[kDurBins] = {}, vCnt[kVelBins] = {};
      int lo = 127, hi = 0;
      for (const Event& e : ev)
      {
         const int db = DurBin(e.durTicks), vb = VelBin(e.vel);
         dSum[db] += e.durTicks; dCnt[db]++;
         vSum[vb] += e.vel; vCnt[vb]++;
         lo = std::min<int>(lo, e.note);
         hi = std::max<int>(hi, e.note);
      }
      for (int b = 0; b < kDurBins; b++)
         T->durMeanTicks[b] = dCnt[b] ? (float)(dSum[b] / dCnt[b]) : 2.0f * std::pow(2.0f, (float)b * 0.5f);
      for (int b = 0; b < kVelBins; b++)
         T->velMean[b] = vCnt[b] ? (float)(vSum[b] / vCnt[b]) : ((float)b + 0.5f) * 128.0f / (float)kVelBins;
      T->lowNote = n ? lo : 0;
      T->highNote = n ? hi : 127;
      return T;
   }

   std::string EncodeEvents(const std::vector<Event>& events)
   {
      const int n = std::min<int>((int)events.size(), kMaxEvents);
      std::vector<uint8_t> b;
      b.reserve(5 + (size_t)n * 8);
      b.push_back('P');
      b.push_back('M');
      b.push_back(1);
      b.push_back((uint8_t)(n & 0xFF));
      b.push_back((uint8_t)(n >> 8));
      for (int i = 0; i < n; i++)
      {
         const Event& e = events[i];
         b.push_back(e.note);
         b.push_back(e.vel);
         b.push_back(e.metric);
         b.push_back((uint8_t)e.relPitch); // was always-zero pad; old files decode this as relPitch 0
         b.push_back((uint8_t)(e.ioiTicks & 0xFF));
         b.push_back((uint8_t)(e.ioiTicks >> 8));
         b.push_back((uint8_t)(e.durTicks & 0xFF));
         b.push_back((uint8_t)(e.durTicks >> 8));
      }
      return B64Encode(b.data(), b.size());
   }

   bool DecodeEvents(const std::string& text, std::vector<Event>& out)
   {
      out.clear();
      if (text.empty())
         return true;
      std::vector<uint8_t> b;
      if (!B64Decode(text, b) || b.size() < 5 || b[0] != 'P' || b[1] != 'M' || b[2] != 1)
         return false;
      const int n = b[3] | (b[4] << 8);
      if (n > kMaxEvents || b.size() < 5 + (size_t)n * 8)
         return false;
      out.resize(n);
      for (int i = 0; i < n; i++)
      {
         const uint8_t* p = &b[5 + (size_t)i * 8];
         Event& e = out[i];
         e.note = std::min<uint8_t>(p[0], 127);
         e.vel = std::min<uint8_t>(p[1], 127);
         e.metric = std::min<uint8_t>(p[2], 15);
         e.relPitch = (int8_t)p[3];
         e.ioiTicks = (uint16_t)std::min<int>(p[4] | (p[5] << 8), kMaxIoiTicks);
         e.durTicks = (uint16_t)std::min<int>(p[6] | (p[7] << 8), kMaxIoiTicks);
      }
      return true;
   }

   bool HeldOutCrossEntropy(const std::vector<Event>& events, int maxOrder, float& ceModel, float& ceBaseline)
   {
      const int n = (int)events.size();
      if (n < 10)
         return false;
      const int split = n * 8 / 10;
      std::vector<Event> train(events.begin(), events.begin() + split);
      std::unique_ptr<Tables> T(Build(train));
      const Seq q = MakeSeq(events);
      double sumModel = 0.0, sumBase = 0.0;
      int count = 0;
      float p[kMaxAlphabet + 4];
      for (int t = split; t < n; t++)
      {
         double m = 0.0, b = 0.0;
         for (int v : { (int)kPitch, (int)kIoi })
         {
            uint64_t keys[kMaxOrder + 1];
            const int s = q.sym[v][t];
            const int top = KeysAt(q, v, t, std::min(maxOrder, kMaxOrder), keys);
            const bool okM = Blend(T->vp[v], keys, top, p);
            m += -std::log2(std::max(okM ? p[s] : 0.0f, 1e-3f));
            const bool okB = Blend(T->vp[v], keys, 0, p);
            b += -std::log2(std::max(okB ? p[s] : 0.0f, 1e-3f));
         }
         sumModel += m;
         sumBase += b;
         count++;
      }
      if (count == 0)
         return false;
      ceModel = (float)(sumModel / count);
      ceBaseline = (float)(sumBase / count);
      return true;
   }

   // ------------------------------------------------------------------ Player

   float Player::Rand01()
   {
      mRng ^= mRng << 13;
      mRng ^= mRng >> 17;
      mRng ^= mRng << 5;
      return (float)(mRng >> 8) * (1.0f / 16777216.0f);
   }

   void Player::Reset(const Tables* t, uint32_t seed)
   {
      mBound = t;
      mRng = seed ? seed * 2654435761u | 1u : 0x9E3779B9u;
      for (int i = 0; i < 4; i++)
         Rand01();
      for (int v = 0; v < kNumViewpoints; v++)
      {
         for (int i = 0; i < kMaxOrder; i++)
            mHist[v][i] = t ? t->tail[v][i] : 0;
         const int have = t ? t->nEvents - (v == kIoi ? 1 : 0) : 0;
         mHistLen[v] = std::clamp(have, 0, kMaxOrder);
      }
   }

   int Player::SampleView(const Tables& t, int v, int maxOrder, int metricCtx, float stray, bool replay, const Params& p)
   {
      const ViewModel& m = t.vp[v];
      const int top = std::clamp(std::min(maxOrder, mHistLen[v]), 0, t.maxOrder);
      uint64_t keys[kMaxOrder + 1];
      for (int k = 0; k <= top; k++)
         keys[k] = CtxKey(v, k, mHist[v], metricCtx);

      if (replay)
      {
         // Longest exact context, most recent continuation. Not argmax: greedy decoding falls into the
         // likeliest short cycle, this reproduces what actually followed the longest matching history.
         for (int k = top; k >= 0; k--)
            if (const CtxSlot* c = FindCtx(m, keys[k]))
               return v == kPitch ? FoldPitch(c->last, p.lowNote, p.highNote) : (int)c->last;
         return -1;
      }

      float* pr = mScratch;
      if (!Blend(m, keys, top, pr))
         return -1;
      const int A = m.alphabet;

      if (v == kPitch)
      {
         const int lo = std::clamp(p.lowNote, 0, 127), hi = std::clamp(std::max(p.lowNote, p.highNote), 0, 127);
         for (int s = 0; s < A; s++)
            if ((s < lo || s > hi) && pr[s] > 0.0f)
            {
               const int f = FoldPitch(s, lo, hi);
               if (f >= lo && f <= hi && f != s)
                  pr[f] += pr[s];
               pr[s] = 0.0f;
            }
         float sum = 0.0f;
         for (int s = 0; s < A; s++)
            sum += pr[s];
         if (sum <= 0.0f)
            for (int s = lo; s <= hi; s++)
               pr[s] = 1.0f, sum += 1.0f;
         for (int s = 0; s < A; s++)
            pr[s] /= sum;
      }

      // Stray: temperature on the blend, p_i ~ p_i^(1/T); above the faithful midpoint it also mixes
      // toward uniform (pitch: over the range).
      float T = 1.0f, uni = 0.0f;
      if (stray <= 0.5f)
         T = 0.1f * std::pow(10.0f, (stray - 0.05f) / 0.45f);
      else
      {
         T = std::pow(10.0f, (stray - 0.5f) * 2.0f);
         uni = (stray - 0.5f) * 2.0f;
      }
      if (std::abs(T - 1.0f) > 1e-4f)
      {
         const float e = 1.0f / T;
         float sum = 0.0f;
         for (int s = 0; s < A; s++)
            if (pr[s] > 0.0f)
            {
               pr[s] = std::pow(pr[s], e);
               sum += pr[s];
            }
         if (sum > 0.0f)
            for (int s = 0; s < A; s++)
               pr[s] /= sum;
      }
      if (v == kPitch && uni > 0.0f)
      {
         const int lo = std::clamp(p.lowNote, 0, 127), hi = std::clamp(std::max(p.lowNote, p.highNote), 0, 127);
         const float u = uni / (float)(hi - lo + 1);
         for (int s = 0; s < A; s++)
            pr[s] = (1.0f - uni) * pr[s] + ((s >= lo && s <= hi) ? u : 0.0f);
      }

      float sum = 0.0f;
      for (int s = 0; s < A; s++)
         sum += pr[s];
      float r = Rand01() * sum;
      int pick = -1;
      for (int s = 0; s < A; s++)
      {
         if (pr[s] <= 0.0f)
            continue;
         pick = s;
         r -= pr[s];
         if (r < 0.0f)
            break;
      }
      return pick;
   }

   void Player::Next(const Tables& t, const Params& p, double prevOnsetBeats, double beatsPerBar, Out& o)
   {
      const float stray = std::clamp(p.stray, 0.0f, 1.0f);
      const bool replay = stray < 0.05f;
      const int order = std::clamp(p.memory, 0, kMaxOrder);
      const double bpb = std::max(1.0, beatsPerBar);
      auto metricAt = [bpb](double beats) {
         double b = std::fmod(beats, bpb);
         if (b < 0.0)
            b += bpb;
         return MetricOf(b);
      };

      int ioi = SampleView(t, kIoi, order, metricAt(prevOnsetBeats), stray, replay, p);
      if (ioi < 0)
         ioi = kTicksPerBeat / 4;
      o.ioiTicks = ioi;
      o.onsetBeats = prevOnsetBeats + (double)ioi / (double)kTicksPerBeat;
      const int metric = metricAt(o.onsetBeats);

      int pitch = SampleView(t, kPitch, order, 0, stray, replay, p);
      int dur = SampleView(t, kDur, order, metric, stray, replay, p);
      int vel = SampleView(t, kVel, order, metric, stray, replay, p);
      if (pitch < 0)
         pitch = std::clamp((p.lowNote + p.highNote) / 2, 0, 127);
      if (dur < 0)
         dur = 4;
      if (vel < 0)
         vel = kVelBins / 2;

      const int sy[kNumViewpoints] = { pitch, ioi, dur, vel };
      for (int v = 0; v < kNumViewpoints; v++)
      {
         for (int i = kMaxOrder - 1; i > 0; i--)
            mHist[v][i] = mHist[v][i - 1];
         mHist[v][0] = (uint16_t)sy[v];
         mHistLen[v] = std::min(kMaxOrder, mHistLen[v] + 1);
      }

      const float uLen = Rand01(), uVel = Rand01();
      o.note = pitch;
      double ticks = t.durMeanTicks[dur];
      ticks *= std::exp((double)p.lengthSpread * (2.0 * uLen - 1.0) * 0.9);
      o.durBeats = std::clamp(ticks / (double)kTicksPerBeat, 1.0 / 48.0, 8.0);
      float v01 = t.velMean[vel] / 127.0f + (2.0f * uVel - 1.0f) * p.velSpread * 0.3f;
      o.velocity = std::clamp(v01, 0.05f, 1.0f);
   }

   void Player::NextRhythm(const Tables& t, const Params& p, double prevOnsetBeats, double beatsPerBar,
                            int rootNote, Out& o)
   {
      const float stray = std::clamp(p.stray, 0.0f, 1.0f);
      const bool replay = stray < 0.05f;
      const int order = std::clamp(p.memory, 0, kMaxOrder);
      const double bpb = std::max(1.0, beatsPerBar);
      auto metricAt = [bpb](double beats) {
         double b = std::fmod(beats, bpb);
         if (b < 0.0)
            b += bpb;
         return MetricOf(b);
      };

      int ioi = SampleView(t, kIoi, order, metricAt(prevOnsetBeats), stray, replay, p);
      if (ioi < 0)
         ioi = kTicksPerBeat / 4;
      o.ioiTicks = ioi;
      o.onsetBeats = prevOnsetBeats + (double)ioi / (double)kTicksPerBeat;
      const int metric = metricAt(o.onsetBeats);

      int relSym = SampleView(t, kPitchRel, order, 0, stray, replay, p);
      int dur = SampleView(t, kDur, order, metric, stray, replay, p);
      int vel = SampleView(t, kVel, order, metric, stray, replay, p);
      if (relSym < 0)
         relSym = RelPitchSymbol(0);
      if (dur < 0)
         dur = 4;
      if (vel < 0)
         vel = kVelBins / 2;

      const int viewpoints[4] = { (int)kIoi, (int)kDur, (int)kVel, (int)kPitchRel };
      const int symbols[4] = { ioi, dur, vel, relSym };
      for (int i = 0; i < 4; i++)
      {
         const int v = viewpoints[i];
         for (int k = kMaxOrder - 1; k > 0; k--)
            mHist[v][k] = mHist[v][k - 1];
         mHist[v][0] = (uint16_t)symbols[i];
         mHistLen[v] = std::min(kMaxOrder, mHistLen[v] + 1);
      }

      const float uLen = Rand01(), uVel = Rand01();
      o.note = std::clamp(rootNote + SymbolToRelPitch(relSym), 0, 127);
      double ticks = t.durMeanTicks[dur];
      ticks *= std::exp((double)p.lengthSpread * (2.0 * uLen - 1.0) * 0.9);
      o.durBeats = std::clamp(ticks / (double)kTicksPerBeat, 1.0 / 48.0, 8.0);
      float v01 = t.velMean[vel] / 127.0f + (2.0f * uVel - 1.0f) * p.velSpread * 0.3f;
      o.velocity = std::clamp(v01, 0.05f, 1.0f);
   }
}
