#include "PopData.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "platform/OpenGLHeaders.h"
#include "PopGpu.h"

namespace pop
{
   // --- PopBuffer -------------------------------------------------------
   PopBuffer::PopBuffer(std::vector<float>&& cpu, int comps)
      : mCpu(std::move(cpu)), mCpuValid(true), mComps(std::max(1, std::min(4, comps)))
   {
      mCount = (int)(mCpu.size() / (size_t)mComps);
      mCpu.resize((size_t)mCount * (size_t)mComps);
   }

   PopBuffer::PopBuffer(unsigned int glBuffer, int count, int comps)
      : mCpuValid(false), mGpu(glBuffer), mCount(count), mComps(std::max(1, std::min(4, comps)))
   {
   }

   // --- GPU buffer pool --------------------------------------------------
   // Kernels make new output buffers every cook; an animated chain used to
   // allocate and free several buffers per node per frame. Freed stores go
   // back to a pool keyed by size and kernel outputs take them from there.
   // Only GPU-side writes (transform feedback) reuse a store, so reuse never
   // makes the CPU wait for a draw that still reads it.
   // Turbo 0.70 (F5): a freed store waits two frames before it is handed out
   // again. Reused at once, a kernel wrote a store that this frame's draws
   // (or an async copy) still read, and the GPU had to finish those first.
   namespace
   {
      struct PendingStore
      {
         unsigned int id;
         size_t bytes;
         unsigned long long frame;
      };
      struct GpuPool
      {
         std::unordered_map<size_t, std::vector<unsigned int>> free;
         std::vector<PendingStore> pending;
         unsigned long long frame = 0;
         size_t bytes = 0;
         int entries = 0;
      };
      GpuPool& Pool()
      {
         static GpuPool p;
         return p;
      }
      constexpr size_t kPoolMaxBytes = 512ull * 1024 * 1024;
      constexpr int kPoolMaxEntries = 256;
   }

   unsigned int AcquireGpuBuffer(size_t bytes)
   {
      bytes = std::max<size_t>(bytes, 16);
      GpuPool& p = Pool();
      auto it = p.free.find(bytes);
      if (it != p.free.end() && !it->second.empty())
      {
         const unsigned int id = it->second.back();
         it->second.pop_back();
         p.bytes -= bytes;
         p.entries--;
         return id;
      }
      GLint prev = 0;
      glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev);
      GLuint b = 0;
      glGenBuffers(1, &b);
      glBindBuffer(GL_ARRAY_BUFFER, b);
      glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, nullptr, GL_DYNAMIC_COPY);
      glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev);
      return b;
   }

   void ReleaseGpuBuffer(unsigned int id, size_t bytes)
   {
      if (id == 0)
         return;
      bytes = std::max<size_t>(bytes, 16);
      GpuPool& p = Pool();
      if (p.entries >= kPoolMaxEntries || p.bytes + bytes > kPoolMaxBytes)
      {
         glDeleteBuffers(1, &id);
         return;
      }
      p.pending.push_back({ id, bytes, p.frame });
      p.bytes += bytes;
      p.entries++;
   }

   void EndFrameGpuPool()
   {
      GpuPool& p = Pool();
      p.frame++;
      size_t keep = 0;
      for (size_t i = 0; i < p.pending.size(); i++)
      {
         const PendingStore& s = p.pending[i];
         if (s.frame + 2 <= p.frame)
            p.free[s.bytes].push_back(s.id);
         else
            p.pending[keep++] = s;
      }
      p.pending.resize(keep);
   }

   PopBuffer::~PopBuffer()
   {
      if (mGpu != 0)
         ReleaseGpuBuffer(mGpu, (size_t)mCount * (size_t)mComps * sizeof(float));
   }

   const std::vector<float>& PopBuffer::Cpu() const
   {
      if (!mCpuValid)
      {
         mCpu.assign((size_t)mCount * (size_t)mComps, 0.0f);
         if (mGpu != 0 && !mCpu.empty())
         {
            GLint prev = 0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev);
            glBindBuffer(GL_ARRAY_BUFFER, mGpu);
            glGetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(mCpu.size() * sizeof(float)), mCpu.data());
            glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev);
         }
         mCpuValid = true;
      }
      return mCpu;
   }

   void PopBuffer::ReadRows(const std::vector<int>& rows, float* out) const
   {
      const size_t stride = (size_t)mComps;
      if (mCpuValid || mGpu == 0)
      {
         const std::vector<float>& all = Cpu();
         for (size_t r = 0; r < rows.size(); r++)
            std::copy_n(&all[(size_t)rows[r] * stride], stride, out + r * stride);
         return;
      }
      // A few small reads: the first waits for the GPU, the rest are copies.
      GLint prev = 0;
      glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev);
      glBindBuffer(GL_ARRAY_BUFFER, mGpu);
      for (size_t r = 0; r < rows.size(); r++)
         glGetBufferSubData(GL_ARRAY_BUFFER, (GLintptr)((size_t)rows[r] * stride * sizeof(float)),
                            (GLsizeiptr)(stride * sizeof(float)), out + r * stride);
      glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev);
   }

   unsigned int PopBuffer::Gpu() const
   {
      if (mGpu == 0)
      {
         GLint prev = 0;
         glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev);
         glGenBuffers(1, &mGpu);
         glBindBuffer(GL_ARRAY_BUFFER, mGpu);
         // Never zero-sized: some drivers reject binding an empty store.
         const size_t bytes = std::max<size_t>(mCpu.size() * sizeof(float), 16);
         glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STATIC_DRAW);
         if (!mCpu.empty())
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(mCpu.size() * sizeof(float)), mCpu.data());
         glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev);
      }
      return mGpu;
   }

   BufferPtr MakeBuffer(std::vector<float>&& values, int comps)
   {
      return std::make_shared<PopBuffer>(std::move(values), comps);
   }

   BufferPtr MakeFilled(int count, int comps, const float value[4])
   {
      std::vector<float> v((size_t)std::max(0, count) * (size_t)comps);
      for (size_t i = 0; i < v.size(); i++)
         v[i] = value[i % (size_t)comps];
      return MakeBuffer(std::move(v), comps);
   }

   // --- PopTopology -----------------------------------------------------
   PopTopology::~PopTopology()
   {
      if (mTriIbo != 0)
         glDeleteBuffers(1, &mTriIbo);
      if (mLineIbo != 0)
         glDeleteBuffers(1, &mLineIbo);
   }

   void PopTopology::AddStrip(const std::vector<uint32_t>& pts, bool closed)
   {
      lineStart.push_back((int)lineVerts.size());
      lineCount.push_back((int)pts.size());
      lineClosed.push_back(closed ? 1 : 0);
      lineVerts.insert(lineVerts.end(), pts.begin(), pts.end());
   }

   unsigned int PopTopology::TriIbo() const
   {
      if (mTriIbo == 0 && !tris.empty())
      {
         glGenBuffers(1, &mTriIbo);
         GLint prev = 0;
         glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev);
         // Uploaded through ARRAY_BUFFER so no VAO's element binding is touched.
         glBindBuffer(GL_ARRAY_BUFFER, mTriIbo);
         glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(tris.size() * sizeof(uint32_t)), tris.data(), GL_STATIC_DRAW);
         glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev);
      }
      return mTriIbo;
   }

   int PopTopology::LineIndexCount() const
   {
      if (mLineIndexCount < 0)
      {
         int n = 0;
         for (size_t s = 0; s < lineStart.size(); s++)
         {
            const int c = lineCount[s];
            if (c >= 2)
               n += 2 * (c - 1) + ((lineClosed[s] && c > 2) ? 2 : 0);
         }
         mLineIndexCount = n;
      }
      return mLineIndexCount;
   }

   unsigned int PopTopology::LineIbo() const
   {
      if (mLineIbo == 0 && LineIndexCount() > 0)
      {
         std::vector<uint32_t> idx;
         idx.reserve((size_t)LineIndexCount());
         for (size_t s = 0; s < lineStart.size(); s++)
         {
            const int start = lineStart[s], c = lineCount[s];
            if (c < 2)
               continue;
            for (int k = 0; k + 1 < c; k++)
            {
               idx.push_back(lineVerts[(size_t)(start + k)]);
               idx.push_back(lineVerts[(size_t)(start + k + 1)]);
            }
            if (lineClosed[s] && c > 2)
            {
               idx.push_back(lineVerts[(size_t)(start + c - 1)]);
               idx.push_back(lineVerts[(size_t)start]);
            }
         }
         glGenBuffers(1, &mLineIbo);
         GLint prev = 0;
         glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev);
         glBindBuffer(GL_ARRAY_BUFFER, mLineIbo);
         glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * sizeof(uint32_t)), idx.data(), GL_STATIC_DRAW);
         glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev);
      }
      return mLineIbo;
   }

   // --- PopData -----------------------------------------------------------
   const PopAttrib* PopData::Find(const std::string& name) const
   {
      for (const PopAttrib& a : attribs)
         if (a.name == name)
            return &a;
      return nullptr;
   }

   int PopData::CompsOf(const std::string& name) const
   {
      const PopAttrib* a = Find(name);
      return a ? a->buffer->Comps() : DefaultComps(name);
   }

   void PopData::Set(const std::string& name, BufferPtr buffer)
   {
      for (PopAttrib& a : attribs)
         if (a.name == name)
         {
            a.buffer = std::move(buffer);
            return;
         }
      attribs.push_back({ name, std::move(buffer) });
   }

   void PopData::Remove(const std::string& name)
   {
      attribs.erase(std::remove_if(attribs.begin(), attribs.end(),
                                   [&](const PopAttrib& a) { return a.name == name; }),
                    attribs.end());
   }

   void PopData::Rename(const std::string& from, const std::string& to)
   {
      if (from == to)
         return;
      const PopAttrib* a = Find(from);
      if (a == nullptr)
         return;
      BufferPtr b = a->buffer;
      Remove(from);
      Set(to, b);
   }

   std::vector<float> PopData::Read(const std::string& name, int comps) const
   {
      const PopAttrib* a = Find(name);
      const int own = a ? a->buffer->Comps() : DefaultComps(name);
      const int want = comps > 0 ? comps : own;
      std::vector<float> out((size_t)pointCount * (size_t)want);
      if (a != nullptr && own == want && a->buffer->Count() == pointCount)
         return a->buffer->Cpu();
      float def[4];
      DefaultValue(name, def);
      const std::vector<float>* src = a ? &a->buffer->Cpu() : nullptr;
      const int srcCount = a ? a->buffer->Count() : 0;
      for (int i = 0; i < pointCount; i++)
         for (int c = 0; c < want; c++)
            out[(size_t)i * want + c] = (src && i < srcCount && c < own) ? (*src)[(size_t)i * own + c] : def[c];
      return out;
   }

   const PopTopology& PopData::Topo() const
   {
      static const PopTopology kEmpty;
      return topo ? *topo : kEmpty;
   }

   bool PopData::Bounds(float lo[3], float hi[3]) const
   {
      if (mBoundsRevision != revision)
      {
         mBoundsRevision = revision;
         mHasBounds = false;
         const PopAttrib* p = Find(attr::P);
         if (p != nullptr && pointCount > 0)
         {
            if (p->buffer->OnCpu() || !p->buffer->OnGpu())
            {
               const std::vector<float>& v = p->buffer->Cpu();
               const int c = p->buffer->Comps();
               mLo[0] = mLo[1] = mLo[2] = 1e30f;
               mHi[0] = mHi[1] = mHi[2] = -1e30f;
               for (int i = 0; i < pointCount; i++)
                  for (int k = 0; k < 3 && k < c; k++)
                  {
                     const float x = v[(size_t)i * c + k];
                     if (!std::isfinite(x))
                        continue;
                     mLo[k] = std::min(mLo[k], x);
                     mHi[k] = std::max(mHi[k], x);
                  }
               for (int k = c; k < 3; k++)
                  mLo[k] = mHi[k] = 0.0f;
               mHasBounds = mLo[0] <= mHi[0];
            }
            else
            {
               mHasBounds = GpuBounds(p->buffer->Gpu(), pointCount, p->buffer->Comps(), mLo, mHi);
            }
         }
      }
      for (int k = 0; k < 3; k++)
      {
         lo[k] = mLo[k];
         hi[k] = mHi[k];
      }
      return mHasBounds;
   }

   bool PopData::BoundsAsync(AsyncBounds& state, float lo[3], float hi[3]) const
   {
      const PopAttrib* p = Find(attr::P);
      if (p == nullptr || pointCount <= 0)
         return false;
      if (mBoundsRevision == revision || p->buffer->OnCpu() || !p->buffer->OnGpu())
         return Bounds(lo, hi);
      return GpuBoundsAsync(state, p->buffer->Gpu(), pointCount, p->buffer->Comps(), lo, hi);
   }

   std::shared_ptr<PopData> ShallowCopy(const PopData& in)
   {
      auto out = std::make_shared<PopData>();
      out->pointCount = in.pointCount;
      out->attribs = in.attribs;
      out->topo = in.topo;
      out->dims[0] = in.dims[0];
      out->dims[1] = in.dims[1];
      out->instanceScale = in.instanceScale;
      out->instanceAlign = in.instanceAlign;
      out->instanceLook = in.instanceLook;
      return out;
   }

   // --- conversions -------------------------------------------------------
   std::shared_ptr<PopData> FromMesh(const Mesh& mesh)
   {
      auto out = std::make_shared<PopData>();
      const int n = (int)mesh.vertices.size();
      out->pointCount = n;
      std::vector<float> P((size_t)n * 3), N((size_t)n * 3), T((size_t)n * 3);
      for (int i = 0; i < n; i++)
      {
         const Vertex& v = mesh.vertices[(size_t)i];
         P[(size_t)i * 3] = v.px; P[(size_t)i * 3 + 1] = v.py; P[(size_t)i * 3 + 2] = v.pz;
         N[(size_t)i * 3] = v.nx; N[(size_t)i * 3 + 1] = v.ny; N[(size_t)i * 3 + 2] = v.nz;
         T[(size_t)i * 3] = v.u; T[(size_t)i * 3 + 1] = v.v; T[(size_t)i * 3 + 2] = 0.0f;
      }
      out->Set(attr::P, MakeBuffer(std::move(P), 3));
      out->Set(attr::N, MakeBuffer(std::move(N), 3));
      out->Set(attr::Tex, MakeBuffer(std::move(T), 3));
      if (mesh.HasVertexColor())
      {
         std::vector<float> C((size_t)n * 4);
         for (int i = 0; i < n; i++)
         {
            C[(size_t)i * 4] = mesh.vertexColor[(size_t)i * 3];
            C[(size_t)i * 4 + 1] = mesh.vertexColor[(size_t)i * 3 + 1];
            C[(size_t)i * 4 + 2] = mesh.vertexColor[(size_t)i * 3 + 2];
            C[(size_t)i * 4 + 3] = 1.0f;
         }
         out->Set(attr::Color, MakeBuffer(std::move(C), 4));
      }
      auto topo = std::make_shared<PopTopology>();
      topo->tris.assign(mesh.indices.begin(), mesh.indices.end());
      topo->pointPrims = mesh.indices.empty() && n > 0;
      out->topo = topo;
      return out;
   }

   std::shared_ptr<PopData> FromCloud(const std::vector<Particle>& cloud)
   {
      std::vector<const Particle*> alive;
      alive.reserve(cloud.size());
      for (const Particle& p : cloud)
         if (p.alive)
            alive.push_back(&p);
      const int n = (int)alive.size();
      auto out = std::make_shared<PopData>();
      out->pointCount = n;
      std::vector<float> P((size_t)n * 3), N((size_t)n * 3), V((size_t)n * 3), C((size_t)n * 4),
         S((size_t)n), A((size_t)n), L((size_t)n), I((size_t)n);
      for (int i = 0; i < n; i++)
      {
         const Particle& p = *alive[(size_t)i];
         P[(size_t)i * 3] = p.px; P[(size_t)i * 3 + 1] = p.py; P[(size_t)i * 3 + 2] = p.pz;
         N[(size_t)i * 3] = p.nx; N[(size_t)i * 3 + 1] = p.ny; N[(size_t)i * 3 + 2] = p.nz;
         V[(size_t)i * 3] = p.vx; V[(size_t)i * 3 + 1] = p.vy; V[(size_t)i * 3 + 2] = p.vz;
         C[(size_t)i * 4] = p.r; C[(size_t)i * 4 + 1] = p.g; C[(size_t)i * 4 + 2] = p.b; C[(size_t)i * 4 + 3] = 1.0f;
         S[(size_t)i] = p.scale;
         A[(size_t)i] = p.age;
         L[(size_t)i] = p.life;
         I[(size_t)i] = (float)i;
      }
      out->Set(attr::P, MakeBuffer(std::move(P), 3));
      out->Set(attr::N, MakeBuffer(std::move(N), 3));
      out->Set(attr::PartVel, MakeBuffer(std::move(V), 3));
      out->Set(attr::Color, MakeBuffer(std::move(C), 4));
      out->Set(attr::PScale, MakeBuffer(std::move(S), 1));
      out->Set(attr::PartAge, MakeBuffer(std::move(A), 1));
      out->Set(attr::PartLifeSpan, MakeBuffer(std::move(L), 1));
      out->Set(attr::PartId, MakeBuffer(std::move(I), 1));
      auto topo = std::make_shared<PopTopology>();
      topo->pointPrims = true;
      out->topo = topo;
      return out;
   }

   std::shared_ptr<PopData> FromCurve(const Polyline& line)
   {
      auto out = std::make_shared<PopData>();
      const int n = (int)line.Count();
      out->pointCount = n;
      std::vector<float> P(line.points.begin(), line.points.begin() + (size_t)n * 3);
      out->Set(attr::P, MakeBuffer(std::move(P), 3));
      auto topo = std::make_shared<PopTopology>();
      std::vector<uint32_t> strip((size_t)n);
      for (int i = 0; i < n; i++)
         strip[(size_t)i] = (uint32_t)i;
      if (n >= 2)
         topo->AddStrip(strip, line.closed);
      out->topo = topo;
      return out;
   }

   Mesh ToMesh(const PopData& data)
   {
      Mesh mesh;
      const int n = data.pointCount;
      const std::vector<float> P = data.Read(attr::P, 3);
      const std::vector<float> N = data.Read(attr::N, 3);
      const std::vector<float> T = data.Read(attr::Tex, 2);
      mesh.vertices.resize((size_t)n);
      for (int i = 0; i < n; i++)
      {
         Vertex& v = mesh.vertices[(size_t)i];
         v.px = P[(size_t)i * 3]; v.py = P[(size_t)i * 3 + 1]; v.pz = P[(size_t)i * 3 + 2];
         v.nx = N[(size_t)i * 3]; v.ny = N[(size_t)i * 3 + 1]; v.nz = N[(size_t)i * 3 + 2];
         v.u = T[(size_t)i * 2]; v.v = T[(size_t)i * 2 + 1];
      }
      if (data.Has(attr::Color))
      {
         const std::vector<float> C = data.Read(attr::Color, 3);
         mesh.vertexColor = C;
      }
      const PopTopology& topo = data.Topo();
      mesh.indices.reserve(topo.tris.size());
      for (uint32_t idx : topo.tris)
         mesh.indices.push_back(idx < (uint32_t)n ? idx : 0u);
      return mesh;
   }

   std::vector<Particle> ToCloud(const PopData& data)
   {
      const int n = data.pointCount;
      std::vector<Particle> out((size_t)n);
      const std::vector<float> P = data.Read(attr::P, 3);
      const std::vector<float> N = data.Read(attr::N, 3);
      const std::vector<float> V = data.Read(attr::PartVel, 3);
      const std::vector<float> C = data.Read(attr::Color, 3);
      const std::vector<float> S = data.Read(attr::PScale, 1);
      const std::vector<float> A = data.Read(attr::PartAge, 1);
      const std::vector<float> L = data.Read(attr::PartLifeSpan, 1);
      for (int i = 0; i < n; i++)
      {
         Particle& p = out[(size_t)i];
         p.px = P[(size_t)i * 3]; p.py = P[(size_t)i * 3 + 1]; p.pz = P[(size_t)i * 3 + 2];
         p.nx = N[(size_t)i * 3]; p.ny = N[(size_t)i * 3 + 1]; p.nz = N[(size_t)i * 3 + 2];
         p.vx = V[(size_t)i * 3]; p.vy = V[(size_t)i * 3 + 1]; p.vz = V[(size_t)i * 3 + 2];
         p.r = C[(size_t)i * 3]; p.g = C[(size_t)i * 3 + 1]; p.b = C[(size_t)i * 3 + 2];
         p.scale = S[(size_t)i];
         p.age = A[(size_t)i];
         p.life = L[(size_t)i];
         p.alive = true;
      }
      return out;
   }

   Polyline ToCurve(const PopData& data)
   {
      Polyline line;
      const PopTopology& topo = data.Topo();
      if (!topo.HasLines())
         return line;
      const std::vector<float> P = data.Read(attr::P, 3);
      const int start = topo.lineStart[0], c = topo.lineCount[0];
      for (int k = 0; k < c; k++)
      {
         const uint32_t idx = topo.lineVerts[(size_t)(start + k)];
         if (idx >= (uint32_t)data.pointCount)
            continue;
         line.points.push_back(P[(size_t)idx * 3]);
         line.points.push_back(P[(size_t)idx * 3 + 1]);
         line.points.push_back(P[(size_t)idx * 3 + 2]);
      }
      line.closed = topo.lineClosed[0] != 0;
      return line;
   }
}
