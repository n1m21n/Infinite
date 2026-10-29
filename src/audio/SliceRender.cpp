#include "SliceRender.h"

#include <algorithm>
#include <cmath>

// Accelerate FFT on macOS, the portable FFT elsewhere (as audio/dsp/ClipTimeStretch.h does).
#if defined(__APPLE__) && !defined(SIGNALSMITH_USE_ACCELERATE) && !defined(INFINITE_TEST_PORTABLE_FFT)
#define SIGNALSMITH_USE_ACCELERATE
#endif
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace
{
   constexpr float kPi = 3.14159265358979f;

   float DbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

   // Linear interpolation read; outside the buffer is silence.
   float ReadLin(const std::vector<float>& x, double pos)
   {
      if (pos < 0.0)
         return 0.0f;
      const int64_t i0 = (int64_t)pos;
      if (i0 >= (int64_t)x.size())
         return 0.0f;
      const float a = x[(size_t)i0];
      const float b = i0 + 1 < (int64_t)x.size() ? x[(size_t)i0 + 1] : 0.0f;
      return a + (b - a) * (float)(pos - (double)i0);
   }

   void Resample(std::vector<float>& x, int64_t outLen, double rate)
   {
      std::vector<float> y((size_t)std::max<int64_t>(0, outLen));
      for (int64_t i = 0; i < outLen; i++)
         y[(size_t)i] = ReadLin(x, (double)i * rate);
      x.swap(y);
   }

   // Signalsmith's offline pass from `in` (any length) to `outLen` frames, transposed. Falls back to resampling when
   // the input is shorter than the stretcher's window.
   void Stretch(std::vector<float>& l, std::vector<float>& r, double sr, int64_t outLen, float semis, bool tonal)
   {
      const int inLen = (int)l.size();
      if (inLen < 2 || outLen < 2)
      {
         l.assign((size_t)std::max<int64_t>(0, outLen), 0.0f);
         r.assign((size_t)std::max<int64_t>(0, outLen), 0.0f);
         return;
      }
      signalsmith::stretch::SignalsmithStretch<float> st(0x5eed);
      if (tonal)
         st.presetDefault(2, (float)sr);
      else
      {
         // Beats: a shorter window keeps the hits tight.
         const int block = std::max(256, (int)(sr * 0.04));
         st.configure(2, block, block / 4);
      }
      st.setTransposeSemitones(semis, tonal ? (float)(8000.0 / sr) : 0.0f);
      std::vector<float> ol((size_t)outLen), orr((size_t)outLen);
      float* in[2] = { l.data(), r.data() };
      float* out[2] = { ol.data(), orr.data() };
      if (!st.exact(in, inLen, out, (int)outLen))
      {
         const double rate = (double)inLen / (double)outLen;
         Resample(l, outLen, rate);
         Resample(r, outLen, rate);
         return;
      }
      l.swap(ol);
      r.swap(orr);
   }

   // Topology-preserving SVF (Zavalishin), one channel.
   struct Svf
   {
      float ic1 = 0.0f, ic2 = 0.0f;
      void Run(float* x, int64_t n, double sr, float cutoff, float res, int kind) // kind 0 lp, 1 hp, 2 bp
      {
         const float g = std::tan(kPi * std::clamp(cutoff, 20.0f, (float)(sr * 0.49)) / (float)sr);
         const float k = 2.0f - 1.96f * std::clamp(res, 0.0f, 1.0f);
         const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
         for (int64_t i = 0; i < n; i++)
         {
            const float v3 = x[i] - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            x[i] = kind == 0 ? v2 : kind == 1 ? x[i] - k * v1 - v2 : v1;
         }
      }
   };

   void Filter(std::vector<float>& l, std::vector<float>& r, double sr, const SliceParams& p)
   {
      // off, lp 12, lp 24, hp 12, hp 24, bp 12 (SliceFilterNames)
      const int m = p.filterMode;
      if (m <= 0 || m > 5)
         return;
      const int kind = m <= 2 ? 0 : m <= 4 ? 1 : 2;
      const int stages = (m == 2 || m == 4) ? 2 : 1;
      for (std::vector<float>* ch : { &l, &r })
         for (int s = 0; s < stages; s++)
         {
            Svf f;
            f.Run(ch->data(), (int64_t)ch->size(), sr, p.cutoffHz, stages == 2 ? p.res * 0.7f : p.res, kind);
         }
   }

   // Two envelope followers on the peak: the fast one ahead of the slow one is the attack (the transient shaper's
   // model, src/audio/dsp/TransientShaperKernel.h), and each part gets up to 12 dB either way.
   void Shape(std::vector<float>& l, std::vector<float>& r, double sr, float transient, float decay)
   {
      if (std::fabs(transient) < 1e-3f && std::fabs(decay) < 1e-3f)
         return;
      const auto coef = [&](float ms) { return std::exp(-1.0f / (float)(ms * 0.001 * sr)); };
      const float fa = coef(0.5f), fr = coef(30.0f), sa = coef(30.0f), sr2 = coef(300.0f);
      float fast = 0.0f, slow = 0.0f;
      const float attackDb = 12.0f * transient, sustainDb = 12.0f * decay;
      for (size_t i = 0; i < l.size(); i++)
      {
         const float x = std::max(std::fabs(l[i]), std::fabs(r[i]));
         fast = x > fast ? fa * fast + (1.0f - fa) * x : fr * fast + (1.0f - fr) * x;
         slow = x > slow ? sa * slow + (1.0f - sa) * x : sr2 * slow + (1.0f - sr2) * x;
         const float t = std::clamp((fast - slow) / (slow + 0.01f), 0.0f, 1.0f);
         const float g = DbToGain(attackDb * t + sustainDb * (1.0f - t));
         l[i] *= g;
         r[i] *= g;
      }
   }

   // Feed-forward peak compressor with a 6 dB soft knee, mixed in parallel by `compAmount`. Makeup is automatic
   // (half the gain reduction at 0 dBFS) plus the slice's own.
   void Compress(std::vector<float>& l, std::vector<float>& r, double sr, const SliceParams& p)
   {
      if (p.compAmount <= 1e-3f)
         return;
      const float thr = p.compThreshDb, ratio = std::max(1.0f, p.compRatio), knee = 6.0f;
      const float ca = std::exp(-1.0f / (float)(std::max(0.1f, p.compAttackMs) * 0.001 * sr));
      const float cr = std::exp(-1.0f / (float)(std::max(1.0f, p.compReleaseMs) * 0.001 * sr));
      const float slope = 1.0f - 1.0f / ratio;
      const float makeup = DbToGain(-thr * slope * 0.5f + p.compMakeupDb);
      const float mix = std::clamp(p.compAmount, 0.0f, 1.0f);
      float gr = 0.0f; // dB of reduction, smoothed
      for (size_t i = 0; i < l.size(); i++)
      {
         const float x = std::max(std::fabs(l[i]), std::fabs(r[i]));
         const float db = 20.0f * std::log10(std::max(x, 1e-6f));
         const float over = db - thr;
         float want = 0.0f;
         if (over >= knee * 0.5f)
            want = over * slope;
         else if (over > -knee * 0.5f)
         {
            const float k = over + knee * 0.5f;
            want = slope * k * k / (2.0f * knee);
         }
         gr = want > gr ? ca * gr + (1.0f - ca) * want : cr * gr + (1.0f - cr) * want;
         const float g = (1.0f - mix) + mix * DbToGain(-gr) * makeup;
         l[i] *= g;
         r[i] *= g;
      }
   }

   void Fade(std::vector<float>& l, std::vector<float>& r, int64_t body, double sr, const SliceParams& p)
   {
      const float e = std::pow(2.0f, 2.0f * std::clamp(p.fadeCurve, -1.0f, 1.0f));
      const int64_t fi = std::min(body, (int64_t)(p.fadeInMs * 0.001 * sr));
      const int64_t fo = std::min(body, (int64_t)(p.fadeOutMs * 0.001 * sr));
      for (int64_t i = 0; i < fi; i++)
      {
         const float g = std::pow((float)i / (float)fi, e);
         l[(size_t)i] *= g;
         r[(size_t)i] *= g;
      }
      if (fo > 0)
      {
         for (int64_t i = 0; i < fo; i++)
         {
            const float g = std::pow((float)i / (float)fo, e);
            l[(size_t)(body - 1 - i)] *= g;
            r[(size_t)(body - 1 - i)] *= g;
         }
         // Faded out: the tail past the body stays silent too.
         std::fill(l.begin() + body, l.end(), 0.0f);
         std::fill(r.begin() + body, r.end(), 0.0f);
      }
   }

   // Fills `len` frames by looping [ls, le) of x (forward or ping-pong).
   void LoopFill(std::vector<float>& x, int64_t len, int64_t ls, int64_t le, bool pingPong)
   {
      const int64_t n = (int64_t)x.size();
      std::vector<float> y((size_t)len);
      ls = std::clamp<int64_t>(ls, 0, std::max<int64_t>(0, n - 1));
      le = std::clamp<int64_t>(le, ls + 1, n);
      const int64_t span = le - ls;
      for (int64_t i = 0; i < len; i++)
      {
         int64_t k = i;
         if (k >= le)
         {
            const int64_t into = k - le;
            if (pingPong)
            {
               const int64_t cyc = into % (2 * span);
               k = cyc < span ? le - 1 - cyc : ls + (cyc - span);
            }
            else
               k = ls + into % span;
         }
         y[(size_t)i] = x[(size_t)std::min(k, n - 1)];
      }
      x.swap(y);
   }

   std::string KeyOf(const SliceSet& set, int i, int64_t a, int64_t b, int64_t tail, double sr)
   {
      // The params' text form is already canonical (defaults left out).
      SliceSet one;
      one.slices.push_back(set.slices[(size_t)i]);
      one.slices.back().name.clear();
      one.slices.back().colour = -1;
      one.slices.back().note = -1;
      one.slices.back().id = 1;
      one.slices.back().start = 0;
      one.endFrame = 1;
      std::string k = one.Serialize();
      k += std::to_string(a) + ":" + std::to_string(b) + ":" + std::to_string(tail) + ":" + std::to_string(sr) + ":" +
           std::to_string(set.sampleBpm);
      return k;
   }

   int64_t TailFrames(const SliceSet& set, double sr) { return (int64_t)std::lround(std::clamp(set.xfadeMs, 0.0f, 20.0f) * 0.001 * sr); }
}

bool SliceRenderCache::Find(uint16_t id, const std::string& key, Entry& out)
{
   std::lock_guard<std::mutex> lock(mMutex);
   const auto it = mEntries.find(id);
   if (it == mEntries.end() || it->second.key != key)
      return false;
   out = it->second;
   return true;
}

void SliceRenderCache::Store(uint16_t id, Entry e)
{
   std::lock_guard<std::mutex> lock(mMutex);
   mEntries[id] = std::move(e);
}

void SliceRenderCache::Retain(const SliceSet& set)
{
   std::lock_guard<std::mutex> lock(mMutex);
   for (auto it = mEntries.begin(); it != mEntries.end();)
      it = set.IndexOfId(it->first) < 0 ? mEntries.erase(it) : std::next(it);
}

void RenderSlice(const SourceAudio& src, const SliceSet& set, int index, bool withTail, std::vector<float>& l,
                 std::vector<float>& r, int64_t& body)
{
   const Slice& s = set.slices[(size_t)index];
   const SliceParams& p = s.p;
   const double sr = src.sr;
   const int64_t frames = src.Frames();
   const int64_t a = std::clamp<int64_t>(s.start, 0, frames), b = std::clamp<int64_t>(set.EndOf(index), a, frames);
   const int64_t X = withTail ? TailFrames(set, sr) : 0;
   // The read: forward takes the tail from the audio after the slice; reversed plays [a - X, b) backwards, so its
   // tail is the audio before the slice, heard last.
   l.clear();
   r.clear();
   if (!p.reverse)
   {
      const int64_t e = std::min(frames, b + X);
      l.assign(src.l.begin() + a, src.l.begin() + e);
      r.assign(src.r.begin() + a, src.r.begin() + e);
   }
   else
   {
      const int64_t s0 = std::max<int64_t>(0, a - X);
      l.assign(src.l.begin() + s0, src.l.begin() + b);
      r.assign(src.r.begin() + s0, src.r.begin() + b);
      std::reverse(l.begin(), l.end());
      std::reverse(r.begin(), r.end());
   }
   const int64_t srcBody = b - a;
   const int64_t srcTail = (int64_t)l.size() - srcBody;

   // Length and pitch.
   double ratio = std::clamp(p.lengthRatio, 0.25f, 4.0f);
   if (p.fitToGrid && set.sampleBpm > 0.0f && srcBody > 0)
   {
      const double six = sr * 60.0 / (double)set.sampleBpm / 4.0;
      const double steps = std::max(1.0, std::round((double)srcBody * ratio / six));
      ratio = steps * six / (double)srcBody;
   }
   const float semis = p.pitchSt + p.fineCt / 100.0f;
   if (p.stretchMode == 0)
   {
      // Off: pitch resamples, as a sampler plays it; the length then fills by looping or silence.
      const double rate = std::pow(2.0, (double)semis / 12.0);
      const int64_t pitched = (int64_t)std::lround((double)l.size() / rate);
      if (std::fabs(rate - 1.0) > 1e-6)
      {
         Resample(l, pitched, rate);
         Resample(r, pitched, rate);
      }
      const int64_t pBody = std::min<int64_t>((int64_t)l.size(), (int64_t)std::lround((double)srcBody / rate));
      body = (int64_t)std::lround((double)pBody * ratio);
      if (std::fabs(ratio - 1.0) > 1e-6)
      {
         const int64_t want = body + X;
         if (body > pBody && p.loopMode > 0)
         {
            const int64_t ls = (int64_t)(p.loopStart * pBody), le = (int64_t)(p.loopEnd * pBody);
            l.resize((size_t)pBody);
            r.resize((size_t)pBody);
            LoopFill(l, want, ls, le, p.loopMode == 2);
            LoopFill(r, want, ls, le, p.loopMode == 2);
         }
         else if (body > pBody)
         {
            // Longer with no loop: silence after the slice.
            l.resize((size_t)pBody);
            r.resize((size_t)pBody);
         }
         // Shorter: the slice's own continuation is the tail, so the cut fades rather than clicks.
         l.resize((size_t)want, 0.0f);
         r.resize((size_t)want, 0.0f);
      }
   }
   else if (p.stretchMode == 1)
   {
      // Repitch: the length sets the rate, the pitch follows (a turntable).
      const double rate = 1.0 / ratio;
      body = (int64_t)std::lround((double)srcBody * ratio);
      const int64_t total = (int64_t)std::lround((double)l.size() * ratio);
      Resample(l, total, rate);
      Resample(r, total, rate);
   }
   else
   {
      // Beats / tonal: length and pitch apart.
      body = (int64_t)std::lround((double)srcBody * ratio);
      const int64_t total = body + (int64_t)std::lround((double)srcTail * ratio);
      Stretch(l, r, sr, total, semis, p.stretchMode == 3);
   }
   body = std::clamp<int64_t>(body, 0, (int64_t)l.size());
   if (!withTail)
   {
      l.resize((size_t)body);
      r.resize((size_t)body);
   }
   else
   {
      // The tail is always X frames, whatever the chain did to its length.
      l.resize((size_t)(body + X), 0.0f);
      r.resize((size_t)(body + X), 0.0f);
   }

   Fade(l, r, body, sr, p);
   Shape(l, r, sr, p.transient, p.decay);
   Compress(l, r, sr, p);
   Filter(l, r, sr, p);

   const float g = p.gainDb <= -60.0f ? 0.0f : DbToGain(p.gainDb);
   const float ang = (std::clamp(p.pan, -1.0f, 1.0f) + 1.0f) * kPi * 0.25f;
   // Constant power, normalised so centre is unity.
   const float gl = g * std::cos(ang) * 1.41421356f, gr = g * std::sin(ang) * 1.41421356f;
   const bool silent = p.mute || (set.AnySolo() && !p.solo);
   for (size_t i = 0; i < l.size(); i++)
   {
      l[i] = silent ? 0.0f : l[i] * gl;
      r[i] = silent ? 0.0f : r[i] * gr;
   }
}

bool RenderSliceSet(const SourceAudio& src, const SliceSet& set, SliceRenderCache* cache, const std::atomic<bool>* cancel,
                    SliceRenderResult& out)
{
   out = SliceRenderResult();
   if (set.Empty() || src.Frames() < 2)
      return true;
   const double sr = src.sr;
   const int64_t X = TailFrames(set, sr);
   const int n = set.Count();
   std::vector<SliceRenderCache::Entry> parts((size_t)n);
   for (int i = 0; i < n; i++)
   {
      if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
         return false;
      const bool tail = i + 1 < n; // the last slice ends at the end marker
      const int64_t a = set.slices[(size_t)i].start, b = set.EndOf(i);
      const std::string key = KeyOf(set, i, a, b, tail ? X : 0, sr) + (set.AnySolo() ? "S" : "");
      SliceRenderCache::Entry& e = parts[(size_t)i];
      if (cache == nullptr || !cache->Find(set.slices[(size_t)i].id, key, e))
      {
         RenderSlice(src, set, i, tail, e.l, e.r, e.body);
         e.key = key;
         if (cache != nullptr)
            cache->Store(set.slices[(size_t)i].id, e);
      }
   }
   int64_t total = 0;
   for (const auto& e : parts)
      total += e.body;
   out.l.assign((size_t)total + (size_t)X, 0.0f);
   out.r.assign(out.l.size(), 0.0f);
   int64_t pos = 0;
   for (int i = 0; i < n; i++)
   {
      const auto& e = parts[(size_t)i];
      out.outStart.push_back(pos);
      const int64_t len = (int64_t)e.l.size();
      for (int64_t k = 0; k < len; k++)
      {
         float g = 1.0f;
         if (i > 0 && k < X)
            g = (float)k / (float)X; // head: fades in under the previous slice's tail
         if (k >= e.body && X > 0)
            g = 1.0f - (float)(k - e.body) / (float)X; // tail: fades out over the next head
         out.l[(size_t)(pos + k)] += e.l[(size_t)k] * g;
         out.r[(size_t)(pos + k)] += e.r[(size_t)k] * g;
      }
      pos += e.body;
   }
   out.outStart.push_back(pos);
   out.l.resize((size_t)pos);
   out.r.resize((size_t)pos);
   out.laidFrames = pos;

   // The sample-wide pass.
   const SampleWide& w = set.wide;
   if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
      return false;
   PitchShiftBuffer(out.l, out.r, sr, w.pitch + w.fine / 100.0f);
   if (std::fabs(w.speed - 1.0f) > 1e-4f && out.l.size() > 1)
   {
      const int64_t m = std::max<int64_t>(2, (int64_t)std::lround((double)out.l.size() / w.speed));
      const double rate = (double)(out.l.size() - 1) / (double)(m - 1);
      Resample(out.l, m, rate);
      Resample(out.r, m, rate);
   }
   if (w.reverse)
   {
      std::reverse(out.l.begin(), out.l.end());
      std::reverse(out.r.begin(), out.r.end());
   }
   if (w.loop && w.pingPong && !out.l.empty())
   {
      out.l.insert(out.l.end(), out.l.rbegin(), out.l.rend());
      out.r.insert(out.r.end(), out.r.rbegin(), out.r.rend());
      out.bounced = true;
   }
   const auto ramp = [&](float ms, bool in)
   {
      const int64_t f = std::min<int64_t>((int64_t)out.l.size() / 2, (int64_t)(ms * 0.001 * sr));
      for (int64_t i = 0; i < f; i++)
      {
         const float g = (float)i / (float)f;
         const size_t k = in ? (size_t)i : out.l.size() - 1 - (size_t)i;
         out.l[k] *= g;
         out.r[k] *= g;
      }
   };
   ramp(w.fadeInMs, true);
   ramp(w.fadeOutMs, false);
   if (std::fabs(w.gainDb) > 1e-4f)
   {
      const float g = DbToGain(w.gainDb);
      for (size_t i = 0; i < out.l.size(); i++)
      {
         out.l[i] *= g;
         out.r[i] *= g;
      }
   }
   return true;
}

void PitchShiftBuffer(std::vector<float>& l, std::vector<float>& r, double sr, float semis)
{
   if (std::fabs(semis) < 0.005f || l.size() < 64)
      return;
   Stretch(l, r, sr, (int64_t)l.size(), semis, true);
}

double SourceToLaid(const SliceSet& set, const SliceRenderResult& res, int64_t frame)
{
   if (set.Empty() || res.outStart.size() < (size_t)set.Count() + 1)
      return 0.0;
   const int i = set.IndexAt(frame);
   const double a = (double)set.slices[(size_t)i].start, b = (double)set.EndOf(i);
   const double u = b > a ? std::clamp(((double)frame - a) / (b - a), 0.0, 1.0) : 0.0;
   const double o0 = (double)res.outStart[(size_t)i], o1 = (double)res.outStart[(size_t)i + 1];
   return o0 + (o1 - o0) * (set.slices[(size_t)i].p.reverse ? 1.0 - u : u);
}

double LaidToSource(const SliceSet& set, const SliceRenderResult& res, double laid)
{
   if (set.Empty() || res.outStart.size() < (size_t)set.Count() + 1)
      return 0.0;
   const auto it = std::upper_bound(res.outStart.begin(), res.outStart.end() - 1, (int64_t)laid);
   const int i = std::clamp((int)(it - res.outStart.begin()) - 1, 0, set.Count() - 1);
   const double o0 = (double)res.outStart[(size_t)i], o1 = (double)res.outStart[(size_t)i + 1];
   double u = o1 > o0 ? std::clamp((laid - o0) / (o1 - o0), 0.0, 1.0) : 0.0;
   if (set.slices[(size_t)i].p.reverse)
      u = 1.0 - u;
   // A slice made longer by looping or silence shows its playhead at the slice's end while it holds.
   const SliceParams& p = set.slices[(size_t)i].p;
   if (p.stretchMode == 0 && p.lengthRatio > 1.0f)
      u = std::min(1.0, u * p.lengthRatio);
   const double a = (double)set.slices[(size_t)i].start, b = (double)set.EndOf(i);
   return a + (b - a) * u;
}

float PeakOf(const std::vector<float>& l, const std::vector<float>& r)
{
   float p = 0.0f;
   for (size_t i = 0; i < l.size(); i++)
      p = std::max(p, std::max(std::fabs(l[i]), i < r.size() ? std::fabs(r[i]) : 0.0f));
   return p;
}
