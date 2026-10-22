#include "PopCpu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace pop
{
   int Arrays::Index(const std::string& name) const
   {
      for (size_t i = 0; i < names.size(); i++)
         if (names[i] == name)
            return (int)i;
      return -1;
   }

   int Arrays::Ensure(const std::string& name, int c)
   {
      const int i = Index(name);
      if (i >= 0)
         return i;
      names.push_back(name);
      comps.push_back(c);
      float d[4];
      DefaultValue(name, d);
      std::vector<float> v((size_t)count * (size_t)c);
      for (size_t k = 0; k < v.size(); k++)
         v[k] = d[k % (size_t)c];
      data.push_back(std::move(v));
      return (int)names.size() - 1;
   }

   Arrays Load(const PopData& d)
   {
      Arrays a;
      a.count = d.pointCount;
      a.names.push_back(attr::P);
      a.comps.push_back(3);
      a.data.push_back(d.Read(attr::P, 3));
      for (const PopAttrib& at : d.attribs)
      {
         if (at.name == attr::P)
            continue;
         a.names.push_back(at.name);
         a.comps.push_back(at.buffer->Comps());
         a.data.push_back(d.Read(at.name));
      }
      return a;
   }

   Arrays LoadRows(const PopData& d, const std::vector<int>& rows)
   {
      Arrays a;
      a.count = (int)rows.size();
      auto add = [&](const std::string& name, const PopAttrib* at, int comps) {
         a.names.push_back(name);
         a.comps.push_back(comps);
         std::vector<float> v((size_t)a.count * comps);
         if (at != nullptr && at->buffer->Comps() == comps && at->buffer->Count() == d.pointCount)
            at->buffer->ReadRows(rows, v.data());
         else
         {
            // Odd shapes (a P with two components...) take the full path.
            const std::vector<float> all = d.Read(name, comps);
            for (size_t r = 0; r < rows.size(); r++)
               std::copy_n(&all[(size_t)rows[r] * comps], comps, &v[r * comps]);
         }
         a.data.push_back(std::move(v));
      };
      add(attr::P, d.Find(attr::P), 3);
      for (const PopAttrib& at : d.attribs)
         if (at.name != attr::P)
            add(at.name, &at, at.buffer->Comps());
      return a;
   }

   std::shared_ptr<PopData> Store(Arrays&& a, std::shared_ptr<PopTopology> topo)
   {
      auto out = std::make_shared<PopData>();
      out->pointCount = a.count;
      for (size_t i = 0; i < a.names.size(); i++)
      {
         a.data[i].resize((size_t)a.count * (size_t)a.comps[i]);
         out->Set(a.names[i], MakeBuffer(std::move(a.data[i]), a.comps[i]));
      }
      out->topo = topo ? topo : std::make_shared<PopTopology>();
      return out;
   }

   PointBuilder::PointBuilder(const Arrays& src) : mSrc(src)
   {
      mOut.names = src.names;
      mOut.comps = src.comps;
      mOut.data.resize(src.data.size());
   }

   int PointBuilder::Begin()
   {
      for (size_t a = 0; a < mOut.data.size(); a++)
      {
         // Attributes added after construction (Set) start at their default.
         float d[4];
         DefaultValue(mOut.names[a], d);
         for (int k = 0; k < mOut.comps[a]; k++)
            mOut.data[a].push_back(a < mSrc.data.size() ? 0.0f : d[k]);
      }
      return mOut.count++;
   }

   int PointBuilder::Copy(int i)
   {
      const int o = Begin();
      for (size_t a = 0; a < mOut.data.size() && a < mSrc.data.size(); a++)
      {
         const int c = mOut.comps[a];
         std::copy(mSrc.data[a].begin() + (size_t)i * c, mSrc.data[a].begin() + (size_t)(i + 1) * c,
                   mOut.data[a].begin() + (size_t)o * c);
      }
      return o;
   }

   int PointBuilder::Weighted(const int* idx, const float* w, int n)
   {
      const int o = Begin();
      for (size_t a = 0; a < mOut.data.size() && a < mSrc.data.size(); a++)
      {
         const int c = mOut.comps[a];
         float* dst = &mOut.data[a][(size_t)o * c];
         for (int k = 0; k < n; k++)
            for (int j = 0; j < c; j++)
               dst[j] += mSrc.data[a][(size_t)idx[k] * c + j] * w[k];
         if (mOut.names[a] == attr::N && c == 3)
         {
            const float l = std::sqrt(dst[0] * dst[0] + dst[1] * dst[1] + dst[2] * dst[2]);
            if (l > 1e-12f) { dst[0] /= l; dst[1] /= l; dst[2] /= l; }
         }
      }
      return o;
   }

   int PointBuilder::Lerp(int i, int j, float t)
   {
      const int idx[2] = { i, j };
      const float w[2] = { 1.0f - t, t };
      return Weighted(idx, w, 2);
   }

   int PointBuilder::Bary(int i, int j, int k, float u, float v)
   {
      const int idx[3] = { i, j, k };
      const float w[3] = { 1.0f - u - v, u, v };
      return Weighted(idx, w, 3);
   }

   void PointBuilder::SetPos(int point, float x, float y, float z)
   {
      float* p = &mOut.data[0][(size_t)point * 3];
      p[0] = x; p[1] = y; p[2] = z;
   }

   void PointBuilder::Set(int point, const std::string& name, const float* v, int comps)
   {
      const int a = mOut.Ensure(name, comps);
      const int c = mOut.comps[(size_t)a];
      for (int k = 0; k < c && k < comps; k++)
         mOut.data[(size_t)a][(size_t)point * c + k] = v[k];
   }

   Arrays Concat(const std::vector<const Arrays*>& parts)
   {
      Arrays out;
      for (const Arrays* p : parts)
         for (size_t i = 0; i < p->names.size(); i++)
            if (out.Index(p->names[i]) < 0)
            {
               out.names.push_back(p->names[i]);
               out.comps.push_back(p->comps[i]);
               out.data.emplace_back();
            }
      for (const Arrays* p : parts)
      {
         for (size_t a = 0; a < out.names.size(); a++)
         {
            const int c = out.comps[a];
            const int src = p->Index(out.names[a]);
            float d[4];
            DefaultValue(out.names[a], d);
            for (int i = 0; i < p->count; i++)
               for (int k = 0; k < c; k++)
               {
                  float v = d[k];
                  if (src >= 0 && k < p->comps[(size_t)src])
                     v = p->data[(size_t)src][(size_t)i * p->comps[(size_t)src] + k];
                  out.data[a].push_back(v);
               }
         }
         out.count += p->count;
      }
      return out;
   }
   void ParallelRanges(int count, int minPerTask, const std::function<void(int, int)>& fn)
   {
      if (count <= 0)
         return;
      const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
      const int tasks = std::clamp(count / std::max(1, minPerTask), 1, std::min(hw, 8));
      if (tasks <= 1)
      {
         fn(0, count);
         return;
      }
      std::vector<std::thread> workers;
      workers.reserve((size_t)tasks - 1);
      const int per = (count + tasks - 1) / tasks;
      for (int t = 1; t < tasks; t++)
      {
         const int a = t * per, b = std::min(count, a + per);
         if (a < b)
            workers.emplace_back([&fn, a, b] { fn(a, b); });
      }
      fn(0, std::min(count, per));
      for (std::thread& w : workers)
         w.join();
   }

   Arrays Replicate(const Arrays& src, int pieces)
   {
      Arrays out;
      pieces = std::max(0, pieces);
      out.count = src.count * pieces;
      out.names = src.names;
      out.comps = src.comps;
      out.data.resize(src.data.size());
      for (size_t a = 0; a < src.data.size(); a++)
      {
         const size_t one = (size_t)src.count * (size_t)src.comps[a];
         std::vector<float>& d = out.data[a];
         if (src.data[a].size() < one)
         {
            d.resize(one * (size_t)pieces);
            continue;
         }
         // reserve + append: no zero fill of the whole array first.
         d.reserve(one * (size_t)pieces);
         for (int p = 0; p < pieces; p++)
            d.insert(d.end(), src.data[a].begin(), src.data[a].begin() + (std::ptrdiff_t)one);
      }
      return out;
   }

   std::shared_ptr<PopTopology> ReplicateTopo(const TopologyPtr& src, int pieces, int per, ReplicaTopoCache* cache)
   {
      if (cache != nullptr && cache->topo && cache->src == src && cache->pieces == pieces && cache->per == per)
         return cache->topo;
      auto t = std::make_shared<PopTopology>();
      if (src)
      {
         const PopTopology& st = *src;
         t->pointPrims = st.pointPrims;
         const size_t nt = st.tris.size();
         t->tris.resize(nt * (size_t)pieces);
         for (int p = 0; p < pieces; p++)
         {
            const uint32_t base = (uint32_t)((size_t)p * (size_t)per);
            uint32_t* dst = t->tris.data() + nt * (size_t)p;
            for (size_t i = 0; i < nt; i++)
               dst[i] = st.tris[i] + base;
         }
         const size_t strips = st.StripCount();
         if (strips > 0)
         {
            t->lineVerts.reserve(st.lineVerts.size() * (size_t)pieces);
            t->lineStart.reserve(strips * (size_t)pieces);
            t->lineCount.reserve(strips * (size_t)pieces);
            t->lineClosed.reserve(strips * (size_t)pieces);
            for (int p = 0; p < pieces; p++)
            {
               const uint32_t base = (uint32_t)((size_t)p * (size_t)per);
               for (size_t s = 0; s < strips; s++)
               {
                  t->lineStart.push_back((int)t->lineVerts.size());
                  t->lineCount.push_back(st.lineCount[s]);
                  t->lineClosed.push_back(st.lineClosed[s]);
                  for (int k = 0; k < st.lineCount[s]; k++)
                     t->lineVerts.push_back(st.lineVerts[(size_t)(st.lineStart[s] + k)] + base);
               }
            }
         }
      }
      if (cache != nullptr)
      {
         cache->src = src;
         cache->pieces = pieces;
         cache->per = per;
         cache->topo = t;
      }
      return t;
   }
}
