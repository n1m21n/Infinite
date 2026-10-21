#include "ProximityLinesNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace
{
   const Mesh kEmptyMesh;

#if defined(__ARM_NEON)
   // For a 4-bit lane mask: the byte shuffle that packs the set lanes to the front, and how
   // many there are.
   struct CompactTable
   {
      unsigned char perm[16][16];
      unsigned char count[16];
      CompactTable()
      {
         for (int m = 0; m < 16; m++)
         {
            int n = 0;
            for (int b = 0; b < 16; b++)
               perm[m][b] = 0xFF;
            for (int lane = 0; lane < 4; lane++)
               if (m & (1 << lane))
               {
                  for (int k = 0; k < 4; k++)
                     perm[m][n * 4 + k] = (unsigned char)(lane * 4 + k);
                  n++;
               }
            count[m] = (unsigned char)n;
         }
      }
   };
   const CompactTable kCompact;
#endif

   struct SortedPoint { float x, y, z; unsigned int id; };

   // Scratch memory kept between calls: a cook of a moving cloud runs every frame, and
   // fresh multi-megabyte allocations (page faults, zeroing) cost a visible slice of it.
   struct Workspace
   {
      std::mutex mutex;
      std::vector<unsigned int> start, fill, cellOf, nbr;
      std::vector<unsigned char> nbrCount;
      std::vector<SortedPoint> sp;
      std::vector<std::vector<std::pair<unsigned int, unsigned int>>> parts;
   };
   Workspace& Scratch()
   {
      static Workspace ws;
      return ws;
   }

   // Worker threads that stay alive between calls, so a cook wakes them instead of creating
   // new ones.
   class WorkerPool
   {
   public:
      explicit WorkerPool(unsigned n)
      {
         for (unsigned i = 0; i < n; i++)
            mThreads.emplace_back([this] { Loop(); });
      }
      ~WorkerPool()
      {
         {
            std::lock_guard<std::mutex> lk(mMutex);
            mQuit = true;
         }
         mWake.notify_all();
         for (std::thread& t : mThreads)
            t.join();
      }
      unsigned Size() const { return (unsigned)mThreads.size() + 1; }
      // Runs fn on every pool thread and on the caller, returns when all are done.
      void Run(const std::function<void()>& fn)
      {
         std::lock_guard<std::mutex> serial(mRunMutex);
         {
            std::lock_guard<std::mutex> lk(mMutex);
            mJob = &fn;
            mPending = (unsigned)mThreads.size();
            mGeneration++;
         }
         mWake.notify_all();
         fn();
         std::unique_lock<std::mutex> lk(mMutex);
         mDone.wait(lk, [this] { return mPending == 0; });
      }

   private:
      void Loop()
      {
         unsigned seen = 0;
         for (;;)
         {
            const std::function<void()>* job = nullptr;
            {
               std::unique_lock<std::mutex> lk(mMutex);
               mWake.wait(lk, [&] { return mQuit || mGeneration != seen; });
               if (mQuit)
                  return;
               seen = mGeneration;
               job = mJob;
            }
            (*job)();
            std::lock_guard<std::mutex> lk(mMutex);
            if (--mPending == 0)
               mDone.notify_one();
         }
      }
      std::vector<std::thread> mThreads;
      std::mutex mMutex, mRunMutex;
      std::condition_variable mWake, mDone;
      const std::function<void()>* mJob = nullptr;
      unsigned mPending = 0, mGeneration = 0;
      bool mQuit = false;
   };

   WorkerPool& Pool()
   {
      static WorkerPool pool(std::max(1u, std::min(8u, std::thread::hardware_concurrency())) - 1);
      return pool;
   }

   inline unsigned int FloatBits(float f) { unsigned int u; std::memcpy(&u, &f, sizeof u); return u; }
}

void ProximityLinesNode::FindLinks(const float* xyz, size_t count, float radius, int maxPerPoint,
                                   size_t maxLinks, std::vector<std::pair<unsigned int, unsigned int>>& out)
{
   out.clear();
   if (count < 2 || radius <= 0.0f || maxLinks == 0)
      return;
   const int K = std::clamp(maxPerPoint, 1, 16);
   Workspace& ws = Scratch();
   std::lock_guard<std::mutex> scratchLock(ws.mutex);
   if (ws.sp.capacity() > 4 * (count + 4) + 4096) // a much smaller cloud: give the memory back
   {
      std::vector<unsigned int>().swap(ws.start); std::vector<unsigned int>().swap(ws.fill);
      std::vector<unsigned int>().swap(ws.cellOf); std::vector<unsigned int>().swap(ws.nbr);
      std::vector<unsigned char>().swap(ws.nbrCount); std::vector<SortedPoint>().swap(ws.sp);
      ws.parts.clear(); ws.parts.shrink_to_fit();
   }

   float lo[3] = { xyz[0], xyz[1], xyz[2] }, hi[3] = { xyz[0], xyz[1], xyz[2] };
   for (size_t i = 1; i < count; i++)
      for (int a = 0; a < 3; a++)
      {
         lo[a] = std::min(lo[a], xyz[i * 3 + a]);
         hi[a] = std::max(hi[a], xyz[i * 3 + a]);
      }

   // A cell is never smaller than the radius, so every neighbour is in the 27 cells around
   // a point; when the radius is tiny against the extent the cells grow to keep the grid
   // under ~4M cells (more candidates per cell, still exact). Half-radius cells were tried
   // and are slower: more, emptier runs cost more than the saved distance tests.
   float cell = radius;
   for (;;)
   {
      double cells = 1.0;
      for (int a = 0; a < 3; a++)
         cells *= std::floor((hi[a] - lo[a]) / cell) + 1.0;
      if (cells <= 4.0e6)
         break;
      cell *= 1.5f;
   }
   int dim[3];
   for (int a = 0; a < 3; a++)
      dim[a] = (int)std::floor((hi[a] - lo[a]) / cell) + 1;
   const int h = 1; // cells to look out in each direction
   const float inv = 1.0f / cell;
   auto coord = [&](float v, int a) { return std::min(dim[a] - 1, (int)((v - lo[a]) * inv)); };

   // Counting sort by cell; the positions are copied in cell order so a cell's points sit
   // together in memory.
   const size_t ncell = (size_t)dim[0] * dim[1] * dim[2];
   std::vector<unsigned int>& start = ws.start;
   start.assign(ncell + 1, 0);
   ws.cellOf.resize(count);
   unsigned int* const cellOf = ws.cellOf.data();
   for (size_t i = 0; i < count; i++)
   {
      const float* p = &xyz[i * 3];
      cellOf[i] = (unsigned int)(((size_t)coord(p[2], 2) * dim[1] + coord(p[1], 1)) * dim[0] + coord(p[0], 0));
      start[cellOf[i] + 1]++;
   }
   for (size_t c = 0; c < ncell; c++)
      start[c + 1] += start[c];
   ws.sp.resize(count + 4); // padded: a four-wide load may run past the end
   SortedPoint* const sp = ws.sp.data();
   {
      std::vector<unsigned int>& fill = ws.fill;
      fill.assign(start.begin(), start.end() - 1);
      for (size_t i = 0; i < count; i++)
      {
         const unsigned int s = fill[cellOf[i]]++;
         sp[s] = { xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2], (unsigned int)i };
      }
   }

   // Every point reaches for its K nearest neighbours inside the radius (ties go to the
   // lower index, so the answer does not depend on thread timing). Cells are handed to the
   // threads in chunks; each point writes only its own row, so there is no locking.
   const float r2 = radius * radius;
   // Only the entries a point wrote are ever read, so stale contents do not matter.
   ws.nbr.resize(count * (size_t)K);
   ws.nbrCount.resize(count);
   unsigned int* const nbr = ws.nbr.data();
   unsigned char* const nbrCount = ws.nbrCount.data();
#if defined(__ARM_NEON)
   const float32x4_t r2v = vdupq_n_f32(r2);
   const uint32x4_t kLane = { 0, 1, 2, 3 }, kBit = { 1, 2, 4, 8 };
#endif
   std::atomic<size_t> nextCell{ 0 };
   const unsigned long long kLimit = ((unsigned long long)FloatBits(r2) << 32) | 0xFFFFFFFFull;
   auto reach = [&]()
   {
      unsigned long long best[16];
      std::vector<unsigned long long> stage(256);
      const size_t kChunk = 256;
      for (;;)
      {
         const size_t c0 = nextCell.fetch_add(kChunk);
         if (c0 >= ncell)
            break;
         const size_t c1 = std::min(ncell, c0 + kChunk);
         for (size_t c = c0; c < c1; c++)
         {
            const unsigned int s0 = start[c], s1 = start[c + 1];
            if (s0 == s1)
               continue;
            const int cx = (int)(c % dim[0]), cy = (int)((c / dim[0]) % dim[1]), cz = (int)(c / ((size_t)dim[0] * dim[1]));
            const int x0 = std::max(0, cx - h), x1 = std::min(dim[0] - 1, cx + h);
            unsigned int rt0[9], rt1[9];
            int nrun = 0;
            size_t cand = 0;
            for (int z = std::max(0, cz - h); z <= std::min(dim[2] - 1, cz + h); z++)
               for (int y = std::max(0, cy - h); y <= std::min(dim[1] - 1, cy + h); y++)
               {
                  const size_t row = ((size_t)z * dim[1] + y) * dim[0];
                  rt0[nrun] = start[row + x0];
                  rt1[nrun] = start[row + x1 + 1];
                  cand += rt1[nrun] - rt0[nrun];
                  nrun++;
               }
            if (stage.size() < cand + 4) // + 4: a four-wide store may run past the last survivor
               stage.resize(cand * 2 + 4);
            unsigned long long* S = stage.data();
            for (unsigned int s = s0; s < s1; s++)
            {
               const float px = sp[s].x, py = sp[s].y, pz = sp[s].z;
               const unsigned int self = sp[s].id;
               // Candidates inside the radius are packed with no branch (the test is the store
               // index) as (distance bits << 32 | index): d2 >= 0, so the float bits order like
               // the number, and one integer compare is the (distance, index) order.
               size_t ns = 0;
#if defined(__ARM_NEON)
               // Four candidates at a time: vld4 splits x, y, z and index apart, the survivors of
               // the four lanes are packed to the front with a table shuffle.
               const float32x4_t pxv = vdupq_n_f32(px), pyv = vdupq_n_f32(py), pzv = vdupq_n_f32(pz);
               for (int r = 0; r < nrun; r++)
                  for (unsigned int t = rt0[r], t1 = rt1[r]; t < t1; t += 4)
                  {
                     const float32x4x4_t v = vld4q_f32(&sp[t].x);
                     const float32x4_t dx = vsubq_f32(v.val[0], pxv), dy = vsubq_f32(v.val[1], pyv), dz = vsubq_f32(v.val[2], pzv);
                     float32x4_t d2 = vmulq_f32(dx, dx);
                     d2 = vfmaq_f32(d2, dy, dy);
                     d2 = vfmaq_f32(d2, dz, dz);
                     const uint32x4_t in = vandq_u32(vcleq_f32(d2, r2v), vcltq_u32(kLane, vdupq_n_u32(t1 - t)));
                     const unsigned int bits = vaddvq_u32(vandq_u32(in, kBit));
                     const uint8x16_t shuf = vld1q_u8(kCompact.perm[bits]);
                     const uint32x4_t dd = vreinterpretq_u32_u8(vqtbl1q_u8(vreinterpretq_u8_f32(d2), shuf));
                     const uint32x4_t ii = vreinterpretq_u32_u8(vqtbl1q_u8(vreinterpretq_u8_f32(v.val[3]), shuf));
                     vst1q_u32(reinterpret_cast<uint32_t*>(&S[ns]), vzip1q_u32(ii, dd));
                     vst1q_u32(reinterpret_cast<uint32_t*>(&S[ns + 2]), vzip2q_u32(ii, dd));
                     ns += kCompact.count[bits];
                  }
#else
               for (int r = 0; r < nrun; r++)
                  for (unsigned int t = rt0[r], t1 = rt1[r]; t < t1; t++)
                  {
                     const float dx = sp[t].x - px, dy = sp[t].y - py, dz = sp[t].z - pz;
                     const float d2 = dx * dx + dy * dy + dz * dz;
                     S[ns] = ((unsigned long long)FloatBits(d2) << 32) | sp[t].id;
                     ns += (d2 <= r2);
                  }
#endif
               for (int q = 0; q < K; q++)
                  best[q] = kLimit;
               if (K <= 8)
               {
                  // Few neighbours kept: a branch-free insert (K min/max steps) beats the
                  // unpredictable shifting loop.
                  for (size_t j = 0; j < ns; j++)
                  {
                     unsigned long long cur = S[j];
                     cur = (unsigned int)cur == self ? ~0ull : cur;
                     if (K == 4)
                     {
                        unsigned long long lo = std::min(best[0], cur); cur = std::max(best[0], cur); best[0] = lo;
                        lo = std::min(best[1], cur); cur = std::max(best[1], cur); best[1] = lo;
                        lo = std::min(best[2], cur); cur = std::max(best[2], cur); best[2] = lo;
                        lo = std::min(best[3], cur); best[3] = lo;
                        continue;
                     }
                     for (int q = 0; q < K; q++)
                     {
                        const unsigned long long lo = std::min(best[q], cur);
                        cur = std::max(best[q], cur);
                        best[q] = lo;
                     }
                  }
               }
               else
               {
                  for (size_t j = 0; j < ns; j++)
                  {
                     const unsigned long long kj = S[j];
                     if (kj >= best[K - 1] || (unsigned int)kj == self)
                        continue;
                     int q = K - 1;
                     while (q > 0 && best[q - 1] > kj)
                     {
                        best[q] = best[q - 1];
                        q--;
                     }
                     best[q] = kj;
                  }
               }
               int n = 0;
               while (n < K && best[n] != kLimit)
               {
                  nbr[(size_t)self * K + n] = (unsigned int)best[n];
                  n++;
               }
               nbrCount[self] = (unsigned char)n;
            }
         }
      }
   };
   const unsigned int hw = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
   const unsigned int workers = count >= 20000 ? hw : 1u;
   {
      std::vector<std::thread> pool;
      const size_t chunk = (count + workers - 1) / workers;
      for (unsigned int w = 1; w < workers; w++)
         pool.emplace_back(reach);
      reach();
      for (std::thread& t : pool)
         t.join();
   }

   // A pair reached from either end is drawn once: from the lower index when both reach,
   // from whichever end reaches otherwise. Chunks are joined in index order, so the link
   // order is fixed.
   auto has = [&](unsigned int row, unsigned int id)
   {
      const unsigned int* r = &nbr[(size_t)row * K];
      for (int m = 0; m < nbrCount[row]; m++)
         if (r[m] == id)
            return true;
      return false;
   };
   // Fixed-size chunks of points, handed out in order; each chunk's links land in its own
   // list and the lists are joined in chunk order, so the link order never depends on
   // which thread ran what. Once the finished leading chunks already hold maxLinks links
   // the later chunks are not needed and are skipped.
   const size_t kEmitChunk = 512;
   const size_t nchunk = (count + kEmitChunk - 1) / kEmitChunk;
   auto& parts = ws.parts;
   if (parts.size() < nchunk)
      parts.resize(nchunk);
   for (auto& v : parts)
      v.clear();
   std::vector<char> finished(nchunk, 0);
   std::atomic<size_t> nextChunk{ 0 }, stopAt{ nchunk };
   std::mutex doneMutex;
   std::atomic<size_t> joinNext{ 0 };
   size_t prefix = 0, prefixLinks = 0;
   auto emit = [&]()
   {
      for (;;)
      {
         const size_t k = nextChunk.fetch_add(1);
         if (k >= nchunk || k >= stopAt.load())
            break;
         auto& v = parts[k];
         v.reserve(kEmitChunk * 3);
         const size_t to = std::min(count, (k + 1) * kEmitChunk);
         for (size_t i = k * kEmitChunk; i < to; i++)
            for (int m = 0; m < nbrCount[i]; m++)
            {
               const unsigned int j = nbr[i * K + m];
               if (i < j || !has(j, (unsigned int)i))
                  v.push_back({ (unsigned int)std::min<size_t>(i, j), (unsigned int)std::max<size_t>(i, j) });
            }
         std::lock_guard<std::mutex> lk(doneMutex);
         finished[k] = 1;
         while (prefix < nchunk && finished[prefix])
         {
            prefixLinks += parts[prefix].size();
            prefix++;
            if (prefixLinks >= maxLinks && stopAt.load() > prefix)
               stopAt.store(prefix);
         }
      }
   };
   if (workers > 1)
      Pool().Run(emit);
   else
      emit();

   std::vector<size_t> offset(nchunk + 1, 0);
   for (size_t k = 0; k < nchunk; k++)
      offset[k + 1] = std::min(maxLinks, offset[k] + parts[k].size());
   out.resize(offset[nchunk]);
   auto join = [&]()
   {
      for (size_t k = joinNext.fetch_add(1); k < nchunk; k = joinNext.fetch_add(1))
         std::copy_n(parts[k].begin(), offset[k + 1] - offset[k], out.begin() + offset[k]);
   };
   if (workers > 1)
      Pool().Run(join);
   else
      join();
}

void ProximityLinesNode::RebuildIfNeeded()
{
   if (input == nullptr)
   {
      if (!mCache.vertices.empty())
      {
         mCache = Mesh();
         mLinks = 0;
         mMeshRevision = NextMeshRevision();
      }
      mBuiltInput = nullptr;
      return;
   }

   const std::vector<Particle>* cloud = input->GetPointCloud();
   const unsigned long long upstream = cloud ? input->PointCloudRevision() : input->MeshRevision();
   if (mBuiltInput == input && mBuiltUpstream == upstream && mBuiltRadius == radius &&
       mBuiltThickness == thickness && mBuiltMaxPerPoint == maxPerPoint && mBuiltMaxLinks == maxLinks &&
       mBuiltAliveOnly == aliveOnly)
      return;

   std::vector<float> pos, col;
   bool anyColor = false;
   if (cloud != nullptr)
   {
      pos.reserve(cloud->size() * 3);
      for (const Particle& p : *cloud)
         if (!(aliveOnly && !p.alive))
            anyColor = anyColor || p.hasColor;
      if (anyColor)
         col.reserve(cloud->size() * 3);
      for (const Particle& p : *cloud)
      {
         if (aliveOnly && !p.alive)
            continue;
         pos.push_back(p.px); pos.push_back(p.py); pos.push_back(p.pz);
         if (anyColor)
         {
            col.push_back(p.r); col.push_back(p.g); col.push_back(p.b);
         }
      }
   }
   else
   {
      const Mesh& src = input->GetMesh();
      pos.reserve(src.vertices.size() * 3);
      for (const Vertex& v : src.vertices)
      {
         pos.push_back(v.px); pos.push_back(v.py); pos.push_back(v.pz);
      }
      if (src.vertexColor.size() == src.vertices.size() * 3)
      {
         anyColor = true;
         col = src.vertexColor;
      }
   }

   std::vector<std::pair<unsigned int, unsigned int>> links;
   FindLinks(pos.data(), pos.size() / 3, radius, maxPerPoint, (size_t)std::clamp(maxLinks, 1, 200000), links);

   // One three-sided prism per link: six vertices, six triangles. Vertex colour (when the
   // points had real colour) rides alongside, one triple per vertex, so the line fades
   // between its two ends.
   Mesh out;
   out.vertices.reserve(links.size() * 6);
   out.indices.reserve(links.size() * 18);
   if (anyColor)
      out.vertexColor.reserve(links.size() * 18);
   const float t = std::max(0.0005f, thickness);
   for (const auto& l : links)
   {
      const float* a = &pos[l.first * 3];
      const float* b = &pos[l.second * 3];
      float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
      const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (len < 1e-8f)
         continue;
      for (float& c : d) c /= len;
      float up[3] = { 0, 1, 0 };
      if (std::fabs(d[1]) > 0.9f) { up[0] = 1; up[1] = 0; }
      float s[3] = { d[1] * up[2] - d[2] * up[1], d[2] * up[0] - d[0] * up[2], d[0] * up[1] - d[1] * up[0] };
      const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
      for (float& c : s) c /= sl;
      const float o[3] = { d[1] * s[2] - d[2] * s[1], d[2] * s[0] - d[0] * s[2], d[0] * s[1] - d[1] * s[0] };
      const unsigned int base = (unsigned int)out.vertices.size();
      for (int end = 0; end < 2; end++)
      {
         const float* p = end == 0 ? a : b;
         const unsigned int idx = end == 0 ? l.first : l.second;
         for (int k = 0; k < 3; k++)
         {
            const float ang = 6.2831853f * (float)k / 3.0f;
            const float cs = std::cos(ang), sn = std::sin(ang);
            Vertex v;
            v.nx = s[0] * cs + o[0] * sn; v.ny = s[1] * cs + o[1] * sn; v.nz = s[2] * cs + o[2] * sn;
            v.px = p[0] + v.nx * t; v.py = p[1] + v.ny * t; v.pz = p[2] + v.nz * t;
            v.u = (float)end; v.v = (float)k / 3.0f;
            out.vertices.push_back(v);
            if (anyColor)
            {
               out.vertexColor.push_back(col[idx * 3]);
               out.vertexColor.push_back(col[idx * 3 + 1]);
               out.vertexColor.push_back(col[idx * 3 + 2]);
            }
         }
      }
      for (unsigned int k = 0; k < 3; k++)
      {
         const unsigned int k1 = (k + 1) % 3;
         out.indices.insert(out.indices.end(), { base + k, base + k1, base + 3 + k1,
                                                 base + k, base + 3 + k1, base + 3 + k });
      }
   }

   mLinks = out.vertices.size() / 6;
   mCache = std::move(out);
   mBuiltInput = input;
   mBuiltUpstream = upstream;
   mBuiltRadius = radius;
   mBuiltThickness = thickness;
   mBuiltMaxPerPoint = maxPerPoint;
   mBuiltMaxLinks = maxLinks;
   mBuiltAliveOnly = aliveOnly;
   mMeshRevision = NextMeshRevision();
}

const Mesh& ProximityLinesNode::GetMesh()
{
   if (bypassed)
      return input ? input->GetMesh() : kEmptyMesh;
   RebuildIfNeeded();
   return mCache;
}

unsigned long long ProximityLinesNode::MeshRevision()
{
   if (bypassed)
      return input ? input->MeshRevision() : 0;
   RebuildIfNeeded();
   return mMeshRevision;
}

Material ProximityLinesNode::GetMaterial() const
{
   if (inheritMaterial && input != nullptr)
      return input->GetMaterial();
   Material m;
   m.color[0] = color[0]; m.color[1] = color[1]; m.color[2] = color[2];
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   m.emissionColor[0] = emissionColor[0];
   m.emissionColor[1] = emissionColor[1];
   m.emissionColor[2] = emissionColor[2];
   m.emission = emission;
   return m;
}

unsigned long long ProximityLinesNode::MaterialRevision() const
{
   return ComputeContentRevision(GetMaterial(), mMaterialRevision, mLastMaterialHash);
}

void ProximityLinesNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}
