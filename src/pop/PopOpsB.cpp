// PointZ operators, tier B (first part): topology operators on CPU copies.
// Purpose follows the TouchDesigner POP of the same name (Derivative's public
// operator descriptions); the algorithms are standard and written here.

#include "PopOpsInternal.h"
#include "PopCpu.h"
#include "GpuAlgo.h"
#include "platform/OpenGLHeaders.h"

#include <map>
#include <cstring>
#include <functional>
#include <numeric>
#include <unordered_map>

#include "Mesh.h"

namespace pop
{
   using namespace popa;

   namespace
   {
      PopDef Cpu(const char* name, const char* help, int inputs)
      {
         PopDef d = Simple(name, name, help, inputs);
         d.tier = 'B';
         return d;
      }

      // New topology after keeping only mapped points (map[old] = new or -1):
      // faces losing a corner go, strips split around removed points.
      std::shared_ptr<PopTopology> RemapTopo(const PopTopology& t, const std::vector<int>& map)
      {
         auto o = NewTopo();
         o->pointPrims = t.pointPrims;
         for (size_t i = 0; i + 2 < t.tris.size(); i += 3)
         {
            const int a = map[t.tris[i]], b = map[t.tris[i + 1]], c = map[t.tris[i + 2]];
            if (a >= 0 && b >= 0 && c >= 0)
               o->tris.insert(o->tris.end(), { (uint32_t)a, (uint32_t)b, (uint32_t)c });
         }
         for (size_t s = 0; s < t.StripCount(); s++)
         {
            std::vector<uint32_t> cur;
            bool broke = false;
            for (int k = 0; k < t.lineCount[s]; k++)
            {
               const int m = map[t.lineVerts[(size_t)(t.lineStart[s] + k)]];
               if (m < 0)
               {
                  if (cur.size() >= 2) o->AddStrip(cur, false);
                  cur.clear();
                  broke = true;
               }
               else
                  cur.push_back((uint32_t)m);
            }
            if (cur.size() >= 2)
               o->AddStrip(cur, t.lineClosed[s] != 0 && !broke);
         }
         return o;
      }

      // Turbo 0.70 (P5): the kept rows gathered on the GPU. The mask is known on
      // the CPU, so the indices and the count are too: no attribute comes down,
      // nothing goes up but the index list. Null when compute is unavailable.
      // in's rows `rows` (indices known on the CPU) as a new PopData, gathered on
      // the GPU; no topology. Null when compute is unavailable or a gather fails.
      std::shared_ptr<PopData> GatherPointsGpu(const PopData& in, const std::vector<uint32_t>& rows)
      {
         if (!gpu::Available())
            return nullptr;
         const int n = in.pointCount;
         auto out = std::make_shared<PopData>();
         const int kept = (int)rows.size();
         out->pointCount = kept;
         if (kept > 0)
         {
            const uint32_t countValue = (uint32_t)kept;
            const unsigned int rowsBuf = AcquireGpuBuffer(rows.size() * 4), countBuf = AcquireGpuBuffer(4);
            glBindBuffer(GL_COPY_WRITE_BUFFER, rowsBuf);
            glBufferSubData(GL_COPY_WRITE_BUFFER, 0, (GLsizeiptr)(rows.size() * 4), rows.data());
            glBindBuffer(GL_COPY_WRITE_BUFFER, countBuf);
            glBufferSubData(GL_COPY_WRITE_BUFFER, 0, 4, &countValue);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            bool ok = true;
            for (const PopAttrib& at : in.attribs)
            {
               if (at.buffer->Count() < n)
                  continue;
               const int comps = at.buffer->Comps();
               const unsigned int dst = AcquireGpuBuffer((size_t)kept * (size_t)comps * sizeof(float));
               ok = ok && gpu::GatherRows(at.buffer->Gpu(), comps, rowsBuf, countBuf, kept, dst);
               out->Set(at.name, std::make_shared<PopBuffer>(dst, kept, comps));
            }
            ReleaseGpuBuffer(rowsBuf, rows.size() * 4);
            ReleaseGpuBuffer(countBuf, 4);
            if (!ok)
               return nullptr;
         }
         return out;
      }

      std::shared_ptr<PopData> KeepPointsGpu(const PopData& in, const std::vector<char>& keep)
      {
         const int n = in.pointCount;
         std::vector<uint32_t> rows;
         std::vector<int> map((size_t)n, -1);
         for (int i = 0; i < n; i++)
            if (keep[(size_t)i])
            {
               map[(size_t)i] = (int)rows.size();
               rows.push_back((uint32_t)i);
            }
         auto out = GatherPointsGpu(in, rows);
         if (out)
            out->topo = RemapTopo(in.Topo(), map);
         return out;
      }

      // Points kept by mask, everything else follows.
      std::shared_ptr<PopData> KeepPoints(const PopData& in, const std::vector<char>& keep)
      {
         // A GPU-made input is gathered on the GPU instead of coming down whole.
         bool onGpu = false;
         for (const PopAttrib& at : in.attribs)
            onGpu = onGpu || !at.buffer->OnCpu();
         if (onGpu)
            if (auto gpuOut = KeepPointsGpu(in, keep))
               return gpuOut;
         Arrays a = Load(in);
         std::vector<int> map((size_t)a.count, -1);
         PointBuilder b(a);
         for (int i = 0; i < a.count; i++)
            if (keep[(size_t)i])
               map[(size_t)i] = b.Copy(i);
         auto out = Store(b.Take(), RemapTopo(in.Topo(), map));
         return out;
      }

      void Cross(const float* a, const float* b, float* o)
      {
         o[0] = a[1] * b[2] - a[2] * b[1];
         o[1] = a[2] * b[0] - a[0] * b[2];
         o[2] = a[0] * b[1] - a[1] * b[0];
      }
      void Normalize3(float* v)
      {
         const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
         if (l > 1e-12f) { v[0] /= l; v[1] /= l; v[2] /= l; }
      }

      // Smooth per-point normals from the faces around each point (area weighted).
      std::vector<float> FaceNormals(const Arrays& a, const PopTopology& t, bool flip)
      {
         std::vector<float> N((size_t)a.count * 3, 0.0f);
         for (size_t i = 0; i + 2 < t.tris.size(); i += 3)
         {
            const float* p0 = a.Pos((int)t.tris[i]);
            const float* p1 = a.Pos((int)t.tris[i + 1]);
            const float* p2 = a.Pos((int)t.tris[i + 2]);
            const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
            const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
            float n[3];
            Cross(e1, e2, n);
            for (int k = 0; k < 3; k++)
               for (int j = 0; j < 3; j++)
                  N[(size_t)t.tris[i + k] * 3 + j] += n[j];
         }
         for (int i = 0; i < a.count; i++)
         {
            float* n = &N[(size_t)i * 3];
            Normalize3(n);
            if (flip) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
            if (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f) n[2] = 1.0f;
         }
         return N;
      }

      // Turbo 0.70 (P5): FaceNormals of a PopData whose P is on the GPU, on the
      // GPU. The faces around each point (CSR, in face order, as FaceNormals
      // sums them) are built once per topology into `cache`; a kernel per point
      // sums the face normals from P read in place - nothing comes down. Same
      // sums (precise); the sqrt and divide may differ by an ulp.
      struct GpuNormalsCache
      {
         TopologyPtr topo; // kept alive: the pointer is the cache key
         int points = -1;
         std::shared_ptr<PopData> adj;
      };
      BufferPtr GpuFaceNormals(const PopData& d, const TopologyPtr& topo, bool flip, const KernelEnv& baseEnv,
                               GpuNormalsCache& cache, std::string& err)
      {
         const PopTopology& t = *topo;
         if (cache.topo != topo || cache.points != d.pointCount)
         {
            const int n = d.pointCount;
            std::vector<float> start((size_t)n, 0.0f), count((size_t)n, 0.0f), tris, faces;
            std::vector<std::vector<uint32_t>> around((size_t)n);
            const size_t faceCount = t.tris.size() / 3;
            for (size_t f = 0; f < faceCount; f++)
               for (int k = 0; k < 3; k++)
                  if (t.tris[f * 3 + (size_t)k] < (uint32_t)n)
                     around[t.tris[f * 3 + (size_t)k]].push_back((uint32_t)f);
            for (int v = 0; v < n; v++)
            {
               start[(size_t)v] = (float)faces.size();
               count[(size_t)v] = (float)around[(size_t)v].size();
               for (uint32_t f : around[(size_t)v])
                  faces.push_back((float)f);
            }
            tris.reserve(faceCount * 3);
            for (size_t i = 0; i < faceCount * 3; i++)
               tris.push_back((float)t.tris[i]);
            cache.adj = std::make_shared<PopData>();
            cache.adj->Set("PzAdjStart", MakeBuffer(std::move(start), 1));
            cache.adj->Set("PzAdjCount", MakeBuffer(std::move(count), 1));
            cache.adj->Set("PzAdjFace", MakeBuffer(faces.empty() ? std::vector<float>(1, 0.0f) : std::move(faces), 1));
            cache.adj->Set("PzTri", MakeBuffer(tris.empty() ? std::vector<float>(1, 0.0f) : std::move(tris), 1));
            cache.topo = topo;
            cache.points = n;
         }
         KernelSpec spec;
         auto lookup = [](const char* name, int comps, int input) {
            KernelIO io;
            io.name = name;
            io.comps = comps;
            io.input = input;
            return io;
         };
         spec.lookups = { lookup(attr::P, 3, 0), lookup("PzAdjStart", 1, 1), lookup("PzAdjCount", 1, 1),
                          lookup("PzAdjFace", 1, 1), lookup("PzTri", 1, 1) };
         KernelIO nOut;
         nOut.name = attr::N;
         nOut.comps = 3;
         spec.writes = { nOut };
         spec.body = "int s = int(fetch_PzAdjStart(id));\n"
                     "int cnt = int(fetch_PzAdjCount(id));\n"
                     "precise vec3 n = vec3(0.0);\n"
                     "for (int k = 0; k < cnt; k++)\n"
                     "{\n"
                     "   int f = int(fetch_PzAdjFace(s + k)) * 3;\n"
                     "   precise vec3 p0 = fetch_P(int(fetch_PzTri(f)));\n"
                     "   precise vec3 e1 = fetch_P(int(fetch_PzTri(f + 1))) - p0;\n"
                     "   precise vec3 e2 = fetch_P(int(fetch_PzTri(f + 2))) - p0;\n"
                     "   n += vec3(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);\n"
                     "}\n"
                     "float l = sqrt(n.x * n.x + n.y * n.y + n.z * n.z);\n"
                     "if (l > 1e-12)\n"
                     "   n = n / l;\n"
                     "if (uFlip != 0)\n"
                     "   n = -n;\n"
                     "if (n.x == 0.0 && n.y == 0.0 && n.z == 0.0)\n"
                     "   n.z = 1.0;\n"
                     "N = n;\n";
         KernelUniform flipU;
         flipU.name = "uFlip";
         flipU.type = KernelUniform::kInt;
         flipU.i = flip ? 1 : 0;
         KernelEnv env = baseEnv;
         env.lookupData[0] = &d;
         env.lookupData[1] = cache.adj.get();
         std::vector<BufferPtr> out;
         if (!RunKernel(spec, d, { flipU }, env, out, err) || out.empty())
            return nullptr;
         return out[0];
      }

      // Turbo 0.70 (P5): P of the rows of `d` (on the GPU) moved by one matrix per
      // group, row r by group rows->PzGroup[r]: the four columns are rows of
      // `mats` (PzM0..PzM3). `translate` adds column 3 only (q + t, as the CPU
      // offsets), else P = M * P in TransformArrays' order; precise, so the same
      // floats. With rows->PzTex, Tex is set from it. In place on d.
      bool GpuMoveRows(PopData& d, const PopData& rows, const PopData& mats, bool translate, const KernelEnv& baseEnv,
                       std::string& err)
      {
         const PopAttrib* group = rows.Find("PzGroup");
         const PopAttrib* tex = rows.Find("PzTex");
         if (group == nullptr)
            return false;
         d.Set("PzGroup", group->buffer);
         if (tex != nullptr)
            d.Set("PzTex", tex->buffer);
         KernelSpec spec;
         auto io = [](const char* name, int comps, int input) {
            KernelIO k;
            k.name = name;
            k.comps = comps;
            k.input = input;
            return k;
         };
         spec.reads = { io(attr::P, 3, 0), io("PzGroup", 1, 0) };
         spec.writes = { io(attr::P, 3, 0) };
         spec.lookups = { io("PzM0", 4, 1), io("PzM1", 4, 1), io("PzM2", 4, 1), io("PzM3", 4, 1) };
         spec.body = "int g = int(PzGroup);\n"
                     "precise vec3 q = P;\n"
                     "vec4 c3 = fetch_PzM3(g);\n";
         if (translate)
            spec.body += "precise vec3 r = q + c3.xyz;\n"
                         "P = r;\n";
         else
            spec.body += "vec4 c0 = fetch_PzM0(g);\n"
                         "vec4 c1 = fetch_PzM1(g);\n"
                         "vec4 c2 = fetch_PzM2(g);\n"
                         "precise float x = c0.x * q.x + c1.x * q.y + c2.x * q.z + c3.x;\n"
                         "precise float y = c0.y * q.x + c1.y * q.y + c2.y * q.z + c3.y;\n"
                         "precise float z = c0.z * q.x + c1.z * q.y + c2.z * q.z + c3.z;\n"
                         "P = vec3(x, y, z);\n";
         if (tex != nullptr)
         {
            spec.reads.push_back(io("PzTex", 3, 0));
            spec.writes.push_back(io(attr::Tex, 3, 0));
            spec.body += "Tex = PzTex;\n";
         }
         KernelEnv env = baseEnv;
         env.lookupData[0] = &d;
         env.lookupData[1] = &mats;
         std::vector<BufferPtr> out;
         const bool ok = RunKernel(spec, d, {}, env, out, err) && out.size() == spec.writes.size();
         d.Remove("PzGroup");
         d.Remove("PzTex");
         if (!ok)
            return false;
         d.Set(attr::P, out[0]);
         if (tex != nullptr)
            d.Set(attr::Tex, out[1]);
         return true;
      }

      // Matrices as the four column rows GpuMoveRows looks up.
      std::shared_ptr<PopData> MatrixTable(const std::vector<Mat4>& ms)
      {
         std::vector<float> col[4];
         for (const Mat4& m : ms)
            for (int c = 0; c < 4; c++)
               col[c].insert(col[c].end(), m.m + c * 4, m.m + c * 4 + 4);
         auto t = std::make_shared<PopData>();
         t->Set("PzM0", MakeBuffer(std::move(col[0]), 4));
         t->Set("PzM1", MakeBuffer(std::move(col[1]), 4));
         t->Set("PzM2", MakeBuffer(std::move(col[2]), 4));
         t->Set("PzM3", MakeBuffer(std::move(col[3]), 4));
         return t;
      }

      bool AnyOnGpu(const PopData& d)
      {
         for (const PopAttrib& at : d.attribs)
            if (!at.buffer->OnCpu())
               return true;
         return false;
      }

      // Spatial hash weld: map each point to a representative within `dist`.
      std::vector<int> WeldMap(const Arrays& a, float dist)
      {
         std::vector<int> rep((size_t)a.count);
         const float cell = std::max(dist, 1e-6f);
         std::unordered_map<long long, std::vector<int>> grid;
         auto key = [&](long long x, long long y, long long z) { return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL); };
         for (int i = 0; i < a.count; i++)
         {
            const float* p = a.Pos(i);
            const long long cx = (long long)std::floor(p[0] / cell), cy = (long long)std::floor(p[1] / cell), cz = (long long)std::floor(p[2] / cell);
            int found = -1;
            for (int dx = -1; dx <= 1 && found < 0; dx++)
               for (int dy = -1; dy <= 1 && found < 0; dy++)
                  for (int dz = -1; dz <= 1 && found < 0; dz++)
                  {
                     auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                     if (it == grid.end()) continue;
                     for (int j : it->second)
                     {
                        const float* q = a.Pos(j);
                        const float ddx = p[0] - q[0], ddy = p[1] - q[1], ddz = p[2] - q[2];
                        if (ddx * ddx + ddy * ddy + ddz * ddz <= dist * dist) { found = j; break; }
                     }
                  }
            if (found >= 0)
               rep[(size_t)i] = found;
            else
            {
               rep[(size_t)i] = i;
               grid[key(cx, cy, cz)].push_back(i);
            }
         }
         return rep;
      }

      Mat4 CopyStep(const float t[3], const float r[3], const float s[3], int n)
      {
         Mat4 one = Mat4::Multiply(Mat4::Translation(t[0], t[1], t[2]),
                                   Mat4::Multiply(Euler(r[0], r[1], r[2]), Mat4::Scale(s[0], s[1], s[2])));
         Mat4 acc;
         for (int i = 0; i < n; i++)
            acc = Mat4::Multiply(one, acc);
         return acc;
      }

      // Turbo 0.62: Copy PointZ on the GPU - the same points the CPU loop below
      // makes (Load's layout, then CopyId / TemplateId / stamped Color), as
      // kernels that look the geometry and the template up directly, so an
      // animated template costs no download and no per-point CPU work. Up to
      // 16 copies per site (the step matrices go in as uniforms); returns
      // false to leave the job to the CPU path.
      bool CopyOnGpu(CookCtx& c, const PopData& in, const PopData* tmpl, int copies, int siteCount, const float t[3],
                     const float r[3], const float s[3])
      {
         constexpr int kMaxSteps = 16;
         if (copies > kMaxSteps || (tmpl != nullptr && tmpl->pointCount <= 0))
            return false;
         const int n = in.pointCount;
         const long long total = (long long)n * copies * siteCount;
         if (n <= 0 || total <= 0 || total > 0x7fffffffLL)
            return false;

         std::vector<KernelIO> outAttrs;
         auto indexOf = [&](const std::string& name) {
            for (size_t i = 0; i < outAttrs.size(); i++)
               if (outAttrs[i].name == name)
                  return (int)i;
            return -1;
         };
         outAttrs.push_back({ attr::P, 3 });
         for (const PopAttrib& at : in.attribs)
            if (at.name != attr::P)
            {
               if (!ValidAttribName(at.name))
                  return false;
               outAttrs.push_back({ at.name, at.buffer->Comps() });
            }
         const size_t srcAttrs = outAttrs.size();
         const bool stamp = tmpl != nullptr && c.Bl("color");
         if (indexOf("CopyId") < 0)
            outAttrs.push_back({ "CopyId", 1 });
         if (indexOf("TemplateId") < 0)
            outAttrs.push_back({ "TemplateId", 1 });
         const bool srcHasColor = indexOf(attr::Color) >= 0 && indexOf(attr::Color) < (int)srcAttrs;
         if (stamp && indexOf(attr::Color) < 0)
            outAttrs.push_back({ attr::Color, 4 });

         std::vector<KernelUniform> u;
         u.push_back(Int("uN", n));
         u.push_back(Int("uCopies", copies));
         u.push_back(Int("uHasTmpl", tmpl != nullptr ? 1 : 0));
         u.push_back(Int("uAlign", c.I("align")));
         u.push_back(Flt("uTScale", c.F("scale")));
         {
            const Mat4 one = CopyStep(t, r, s, 1);
            Mat4 acc;
            for (int k = 0; k < kMaxSteps; k++) // unused ones stay identity
            {
               KernelUniform m;
               m.name = "uStep" + std::to_string(k);
               m.type = KernelUniform::kMat4;
               std::memcpy(m.v, (k < copies ? acc : Mat4()).m, sizeof(m.v));
               u.push_back(m);
               if (k < copies)
                  acc = Mat4::Multiply(one, acc);
            }
         }

         // (The uniforms above are declared by the kernel builder.)
         const std::string decls = R"GLSL(
// Copy's rotation taking `from` onto N (the CPU path's formula).
mat3 copyAlign(vec3 from, vec3 to)
{
   if (dot(to, to) > 1e-20) to = normalize(to);
   vec3 v = cross(from, to);
   float cs = dot(from, to);
   if (cs < -0.9999) return rotX(PI);
   float k = 1.0 / (1.0 + cs);
   return mat3(v.x * v.x * k + cs, v.y * v.x * k + v.z, v.z * v.x * k - v.y,
               v.x * v.y * k - v.z, v.y * v.y * k + cs, v.z * v.y * k + v.x,
               v.x * v.z * k + v.y, v.y * v.z * k - v.x, v.z * v.z * k + cs);
}
)GLSL";
         std::string head = R"GLSL(
   int piece = id / uN;
   int pi = id - piece * uN;
   int site = piece / uCopies;
   int ck = piece - site * uCopies;
   mat4 steps[16] = mat4[16](uStep0, uStep1, uStep2, uStep3, uStep4, uStep5, uStep6, uStep7,
                             uStep8, uStep9, uStep10, uStep11, uStep12, uStep13, uStep14, uStep15);
   mat4 cm = steps[ck];
)GLSL";
         if (tmpl != nullptr)
            head += R"GLSL(
   {
      mat3 R = eulerDeg(fetch_tR(site));
      if (uAlign != 0) R = copyAlign(uAlign == 2 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0), fetch_tN(site)) * R;
      float sc = fetch_tS(site) * uTScale;
      cm = mat4(vec4(R[0] * sc, 0.0), vec4(R[1] * sc, 0.0), vec4(R[2] * sc, 0.0), vec4(fetch_tP(site), 1.0)) * cm;
   }
)GLSL";

         auto swz = [](int comps) {
            static const char* k[] = { "", ".x", ".xy", ".xyz", "" };
            return std::string(k[std::clamp(comps, 1, 4)]);
         };
         float colDefault[4] = { 1, 1, 1, 1 };
         DefaultValue(attr::Color, colDefault);

         // At most four writes per kernel: one pass per group.
         std::vector<BufferPtr> results(outAttrs.size());
         for (size_t first = 0; first < outAttrs.size(); first += 4)
         {
            KernelSpec k;
            k.decls = decls;
            k.body = head;
            std::vector<std::string> geomLookups;
            auto needGeom = [&](const KernelIO& a) {
               for (const std::string& g : geomLookups)
                  if (g == a.name)
                     return;
               geomLookups.push_back(a.name);
               KernelIO l = a;
               l.input = 0;
               l.as = "g" + a.name;
               k.lookups.push_back(l);
            };
            if (tmpl != nullptr)
            {
               const KernelIO tl[] = { { attr::P, 3 }, { attr::N, 3 }, { attr::PScale, 1 }, { attr::Rotate, 3 }, { attr::Color, 4 } };
               const char* as[] = { "tP", "tN", "tS", "tR", "tC" };
               for (int i = 0; i < 5; i++)
               {
                  KernelIO l = tl[i];
                  l.input = 1;
                  l.as = as[i];
                  k.lookups.push_back(l);
               }
            }
            for (size_t w = first; w < outAttrs.size() && w < first + 4; w++)
            {
               const KernelIO& a = outAttrs[w];
               k.writes.push_back(a);
               const std::string ty = GlslType(a.comps);
               if (a.name == "CopyId")
                  k.body += "   CopyId = float(ck);\n";
               else if (a.name == "TemplateId")
                  k.body += "   TemplateId = float(site);\n";
               else if (a.name == attr::P)
               {
                  needGeom(a);
                  k.body += "   P = (cm * vec4(fetch_gP(pi), 1.0)).xyz;\n";
               }
               else if (a.name == attr::N && a.comps >= 3)
               {
                  needGeom(a);
                  k.body += "   {\n      " + ty + " nv = fetch_gN(pi);\n";
                  k.body += "      vec3 nn = transpose(inverse(mat3(cm))) * nv.xyz;\n";
                  k.body += "      if (dot(nn, nn) > 1e-20) nn = normalize(nn);\n";
                  k.body += std::string("      N = ") + (a.comps == 4 ? "vec4(nn, nv.w)" : "nn") + ";\n   }\n";
               }
               else if (a.name == attr::Color && stamp)
               {
                  if (srcHasColor)
                  {
                     needGeom(a);
                     k.body += "   Color = fetch_gColor(pi) * fetch_tC(site)" + swz(a.comps) + ";\n";
                  }
                  else
                  {
                     char lit[160];
                     snprintf(lit, sizeof(lit), "vec4(%.9g, %.9g, %.9g, %.9g)", colDefault[0], colDefault[1], colDefault[2],
                              colDefault[3]);
                     k.body += "   Color = (" + std::string(lit) + " * fetch_tC(site))" + swz(a.comps) + ";\n";
                  }
               }
               else
               {
                  needGeom(a);
                  k.body += "   " + a.name + " = fetch_g" + a.name + "(pi);\n";
               }
            }
            PopData dummy;
            dummy.pointCount = (int)total;
            std::vector<BufferPtr> outBufs;
            if (!RunKernel(k, dummy, u, c.env, outBufs, c.error))
            {
               c.error.clear();
               return false; // the CPU path will do it
            }
            for (size_t w = 0; w < outBufs.size(); w++)
               results[first + w] = outBufs[w];
         }

         auto out = std::make_shared<PopData>();
         out->pointCount = (int)total;
         for (size_t i = 0; i < outAttrs.size(); i++)
            out->Set(outAttrs[i].name, results[i]);
         out->topo = ReplicateTopo(in.topo, copies * siteCount, n, &c.State<ReplicaTopoCache>());
         c.out = out;
         return true;
      }

      void TransformArrays(Arrays& a, int first, int last, const Mat4& m)
      {
         float nm[9];
         m.NormalMatrix(nm);
         const int ni = a.Index(attr::N);
         for (int i = first; i < last; i++)
         {
            float* p = a.Pos(i);
            const float x = p[0], y = p[1], z = p[2];
            p[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
            p[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
            p[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
            if (ni >= 0 && a.comps[(size_t)ni] >= 3)
            {
               float* n = a.At(ni, i);
               const float nx = n[0], ny = n[1], nz = n[2];
               n[0] = nm[0] * nx + nm[3] * ny + nm[6] * nz;
               n[1] = nm[1] * nx + nm[4] * ny + nm[7] * nz;
               n[2] = nm[2] * nx + nm[5] * ny + nm[8] * nz;
               Normalize3(n);
            }
         }
      }

      // Round trip through Infinite's Mesh for the MeshOps operators. Keeps
      // P, N, Tex and Color; other attributes do not survive a remesh.
      std::shared_ptr<PopData> ViaMesh(const PopData& in, const std::function<Mesh(const Mesh&)>& fn, std::string& warning)
      {
         const Mesh m = ToMesh(in);
         if (m.indices.empty())
         {
            warning = "needs faces (got points or lines only)";
            return ShallowCopy(in);
         }
         auto out = FromMesh(fn(m));
         if (!in.Has(attr::Color))
            out->Remove(attr::Color);
         for (const PopAttrib& a : in.attribs)
            if (a.name != attr::P && a.name != attr::N && a.name != attr::Tex && a.name != attr::Color)
            {
               warning = "rebuilds the surface: only P, N, Tex and Color carry through";
               break;
            }
         return out;
      }
   }

   void RegisterTopologyB(std::vector<PopDef>& defs)
   {
      // ---- Merge ---------------------------------------------------------------
      {
         PopDef d = Cpu("Merge PointZ", "Joins up to four PointZ nodes into one: points, faces and lines appended; an attribute any of them "
                                     "has is given to all (defaults where missing).", 4);
         d.inputLabels = { "A", "B", "C", "D" };
         d.cook = [](CookCtx& c) {
            std::vector<const PopData*> datas;
            for (int i = 0; i < 4; i++)
               if (const PopData* in = c.Input(i))
                  datas.push_back(in);
            if (datas.empty())
               return true;
            // Turbo 0.70 (P5): a GPU-made input is joined by GPU copies (same
            // rules as the CPU Concat); the CPU path stays for CPU-only inputs and
            // for an attribute shaped differently in two inputs.
            bool onGpu = false;
            for (const PopData* in : datas)
               for (const PopAttrib& at : in->attribs)
                  onGpu = onGpu || !at.buffer->OnCpu();
            std::shared_ptr<PopData> gpuMerged = onGpu ? gpu::ConcatRows(datas) : nullptr;
            std::vector<Arrays> parts;
            if (!gpuMerged)
               for (const PopData* in : datas)
                  parts.push_back(Load(*in));
            auto topo = NewTopo();
            uint32_t base = 0;
            bool allPoints = true;
            for (size_t p = 0; p < datas.size(); p++)
            {
               const PopTopology& t = datas[p]->Topo();
               for (uint32_t i : t.tris)
                  topo->tris.push_back(i + base);
               for (size_t s = 0; s < t.StripCount(); s++)
               {
                  std::vector<uint32_t> strip;
                  for (int k = 0; k < t.lineCount[s]; k++)
                     strip.push_back(t.lineVerts[(size_t)(t.lineStart[s] + k)] + base);
                  topo->AddStrip(strip, t.lineClosed[s] != 0);
               }
               allPoints = allPoints && t.pointPrims;
               base += (uint32_t)datas[p]->pointCount;
            }
            topo->pointPrims = allPoints;
            if (gpuMerged)
            {
               gpuMerged->topo = topo;
               c.out = gpuMerged;
               return true;
            }
            std::vector<const Arrays*> ptrs;
            for (const Arrays& a : parts)
               ptrs.push_back(&a);
            c.out = Store(Concat(ptrs), topo);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Delete ----------------------------------------------------------------
      {
         PopDef d = Cpu("Delete PointZ", "Removes points (and the faces and line pieces that used them): those whose attribute is over "
                                      "'threshold' (a Group or Weight), an index range, every Nth, a random fraction, or the "
                                      "points no face or line uses. 'invert' keeps them instead.", 1);
         d.params = { M("by", "delete by", { "attribute", "index range", "every Nth", "random fraction", "unused points" }, 0),
                      T("attrib", "attribute", "Group", "by=0"), F("threshold", "threshold", 0.5f, -10.0f, 10.0f, "by=0"),
                      I("start", "first", 0, 0, 10000000, "by=1"), I("end", "last", 99, 0, 10000000, "by=1"),
                      I("nth", "every", 2, 1, 10000, "by=2"), F("fraction", "fraction", 0.5f, 0.0f, 1.0f, "by=3"),
                      F("seed", "seed", 1.0f, 0.0f, 1000.0f, "by=3"), B("invert", "invert (keep them)", false) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const int n = in->pointCount, by = c.I("by");
            std::vector<char> del((size_t)n, 0);
            if (by == 0)
            {
               const std::vector<float> v = in->Read(c.S("attrib"), 1);
               const float th = c.F("threshold");
               for (int i = 0; i < n; i++) del[(size_t)i] = v[(size_t)i] > th;
            }
            else if (by == 1)
            {
               for (int i = std::max(0, c.I("start")); i <= c.I("end") && i < n; i++) del[(size_t)i] = 1;
            }
            else if (by == 2)
            {
               const int k = std::max(1, c.I("nth"));
               for (int i = 0; i < n; i++) del[(size_t)i] = (i % k) == 0;
            }
            else if (by == 3)
            {
               uint32_t st = (uint32_t)(c.F("seed") * 7919.0f) * 2654435761u + 1u;
               const float f = c.F("fraction");
               for (int i = 0; i < n; i++)
               {
                  st ^= st << 13; st ^= st >> 17; st ^= st << 5;
                  del[(size_t)i] = ((float)(st & 0xffffff) / 16777215.0f) < f;
               }
            }
            else
            {
               std::vector<char> used((size_t)n, 0);
               const PopTopology& t = in->Topo();
               for (uint32_t i : t.tris) if (i < (uint32_t)n) used[i] = 1;
               for (uint32_t i : t.lineVerts) if (i < (uint32_t)n) used[i] = 1;
               for (int i = 0; i < n; i++) del[(size_t)i] = !used[(size_t)i];
            }
            std::vector<char> keep((size_t)n);
            const bool inv = c.Bl("invert");
            for (int i = 0; i < n; i++) keep[(size_t)i] = inv ? del[(size_t)i] : !del[(size_t)i];
            c.out = KeepPoints(*in, keep);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Convert ---------------------------------------------------------------
      {
         PopDef d = Cpu("Convert PointZ", "Changes what the points make: faces to their edges as lines, anything to bare points, lines "
                                       "to filled faces (fan from the first point), or points to one line through them in order.", 1);
         d.params = { M("to", "convert to", { "points", "edges (lines)", "faces from closed lines", "one line through the points" }, 0),
                      B("close", "close the line", false, "to=3") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const PopTopology& t = c.Input(0)->Topo();
            auto o = NewTopo();
            const int to = c.I("to");
            if (to == 0)
               o->pointPrims = true;
            else if (to == 1)
            {
               std::map<std::pair<uint32_t, uint32_t>, char> seen;
               for (size_t i = 0; i + 2 < t.tris.size(); i += 3)
                  for (int e = 0; e < 3; e++)
                  {
                     uint32_t a = t.tris[i + (size_t)e], b = t.tris[i + (size_t)((e + 1) % 3)];
                     if (a > b) std::swap(a, b);
                     if (seen.emplace(std::make_pair(a, b), 1).second)
                        o->AddStrip({ a, b }, false);
                  }
               for (size_t s = 0; s < t.StripCount(); s++)
               {
                  std::vector<uint32_t> strip(t.lineVerts.begin() + t.lineStart[s], t.lineVerts.begin() + t.lineStart[s] + t.lineCount[s]);
                  o->AddStrip(strip, t.lineClosed[s] != 0);
               }
            }
            else if (to == 2)
            {
               o->tris = t.tris;
               for (size_t s = 0; s < t.StripCount(); s++)
               {
                  const int st = t.lineStart[s], n = t.lineCount[s];
                  for (int k = 1; k + 1 < n; k++)
                     o->tris.insert(o->tris.end(), { t.lineVerts[(size_t)st], t.lineVerts[(size_t)(st + k)], t.lineVerts[(size_t)(st + k + 1)] });
               }
            }
            else
            {
               std::vector<uint32_t> strip((size_t)c.Input(0)->pointCount);
               std::iota(strip.begin(), strip.end(), 0u);
               if (strip.size() >= 2)
                  o->AddStrip(strip, c.Bl("close"));
            }
            c.out->topo = o;
            return true;
         };
         defs.push_back(d);
      }

      // ---- Normal ----------------------------------------------------------------
      {
         PopDef d = Cpu("Normal PointZ", "Computes normals: smooth (averaged over the faces around each point), or each line's "
                                      "tangent direction. 'flip' turns them around.", 1);
         d.params = { M("mode", "mode", { "smooth from faces", "line tangent" }, 0), B("flip", "flip", false) };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const PopData& in = *c.Input(0);
            const PopTopology& t = in.Topo();
            // Turbo 0.70 (P5): smooth normals of a GPU-made input on the GPU
            // (GpuFaceNormals) - nothing comes down.
            const PopAttrib* pAttr = in.Find(attr::P);
            if (c.I("mode") == 0 && t.HasFaces() && pAttr != nullptr && !pAttr->buffer->OnCpu() && gpu::Available())
            {
               struct NormalState : OpState
               {
                  GpuNormalsCache normals;
               };
               GpuNormalsCache& cache = c.State<NormalState>().normals;
               std::string err;
               if (BufferPtr n = GpuFaceNormals(in, in.topo, c.Bl("flip"), c.env, cache, err))
               {
                  c.out->Set(attr::N, n);
                  return true;
               }
               c.warning = err; // falls through to the CPU path
            }
            Arrays a = Load(in);
            std::vector<float> N;
            if (c.I("mode") == 0)
            {
               if (!t.HasFaces())
               {
                  c.warning = "no faces to take normals from";
                  return true;
               }
               N = FaceNormals(a, t, c.Bl("flip"));
            }
            else
            {
               N = in.Read(attr::N, 3);
               for (size_t s = 0; s < t.StripCount(); s++)
               {
                  const int st = t.lineStart[s], n = t.lineCount[s];
                  for (int k = 0; k < n; k++)
                  {
                     const int i0 = (int)t.lineVerts[(size_t)(st + std::max(0, k - 1))];
                     const int i1 = (int)t.lineVerts[(size_t)(st + std::min(n - 1, k + 1))];
                     float d3[3] = { a.Pos(i1)[0] - a.Pos(i0)[0], a.Pos(i1)[1] - a.Pos(i0)[1], a.Pos(i1)[2] - a.Pos(i0)[2] };
                     Normalize3(d3);
                     const uint32_t p = t.lineVerts[(size_t)(st + k)];
                     const float sgn = c.Bl("flip") ? -1.0f : 1.0f;
                     N[(size_t)p * 3] = d3[0] * sgn; N[(size_t)p * 3 + 1] = d3[1] * sgn; N[(size_t)p * 3 + 2] = d3[2] * sgn;
                  }
               }
            }
            c.out->Set(attr::N, MakeBuffer(std::move(N), 3));
            return true;
         };
         defs.push_back(d);
      }

      // ---- Facet -----------------------------------------------------------------
      {
         PopDef d = Cpu("Facet PointZ", "Unique points: every face gets its own corners (hard, faceted shading with flat normals). "
                                     "Consolidate: merges points closer than 'distance' (welds seams), averaging their attributes, "
                                     "with smooth normals after.", 1);
         d.params = { M("mode", "mode", { "unique points (flat)", "consolidate (weld)" }, 0),
                      F("dist", "distance", 0.0001f, 0.0f, 1.0f, "mode=1"), B("normals", "compute normals", true) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const PopTopology& t = in->Topo();
            // Turbo 0.70 (P5): unique points of a GPU-made input on the GPU - a
            // gather by the face indices, sequential faces (kept per topology),
            // flat normals by a kernel reading each face's corners.
            const PopAttrib* pAttr = in->Find(attr::P);
            if (c.I("mode") == 0 && pAttr != nullptr && !pAttr->buffer->OnCpu() && gpu::Available())
            {
               struct FacetState : OpState
               {
                  TopologyPtr src; // kept alive: the pointer is the cache key
                  size_t corners = 0;
                  std::shared_ptr<PopTopology> seq;
               };
               FacetState& s = c.State<FacetState>();
               const size_t corners = t.tris.size() / 3 * 3;
               if (s.src != in->topo || s.corners != corners)
               {
                  s.seq = NewTopo();
                  s.seq->tris.resize(corners);
                  for (size_t i = 0; i < corners; i++)
                     s.seq->tris[i] = (uint32_t)i;
                  s.src = in->topo;
                  s.corners = corners;
               }
               std::vector<uint32_t> rows(t.tris.begin(), t.tris.begin() + (std::ptrdiff_t)corners);
               if (auto out = GatherPointsGpu(*in, rows))
               {
                  out->topo = s.seq;
                  bool ok = true;
                  if (c.Bl("normals") && corners > 0)
                  {
                     KernelSpec spec;
                     KernelIO p;
                     p.name = attr::P;
                     p.comps = 3;
                     p.input = 0;
                     spec.lookups = { p };
                     KernelIO n;
                     n.name = attr::N;
                     n.comps = 3;
                     spec.writes = { n };
                     spec.body = "int f = (id / 3) * 3;\n"
                                 "vec3 p0 = fetch_P(f);\n"
                                 "vec3 e1 = fetch_P(f + 1) - p0;\n"
                                 "vec3 e2 = fetch_P(f + 2) - p0;\n"
                                 "vec3 nn = vec3(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);\n"
                                 "float l = sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);\n"
                                 "if (l > 1e-12)\n"
                                 "   nn = nn / l;\n"
                                 "N = nn;\n";
                     KernelEnv env = c.env;
                     env.lookupData[0] = out.get();
                     std::vector<BufferPtr> res;
                     std::string err;
                     ok = RunKernel(spec, *out, {}, env, res, err);
                     if (ok)
                        out->Set(attr::N, res[0]);
                  }
                  if (ok)
                  {
                     c.out = out;
                     return true;
                  }
               }
            }
            Arrays a = Load(*in);
            if (c.I("mode") == 0)
            {
               PointBuilder b(a);
               auto o = NewTopo();
               for (size_t i = 0; i + 2 < t.tris.size(); i += 3)
               {
                  const int p0 = b.Copy((int)t.tris[i]), p1 = b.Copy((int)t.tris[i + 1]), p2 = b.Copy((int)t.tris[i + 2]);
                  o->tris.insert(o->tris.end(), { (uint32_t)p0, (uint32_t)p1, (uint32_t)p2 });
               }
               Arrays out = b.Take();
               if (c.Bl("normals"))
               {
                  const int ni = out.Ensure(attr::N, 3);
                  for (size_t f = 0; f + 2 < o->tris.size(); f += 3)
                  {
                     const float* p0 = out.Pos((int)o->tris[f]);
                     const float* p1 = out.Pos((int)o->tris[f + 1]);
                     const float* p2 = out.Pos((int)o->tris[f + 2]);
                     const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
                     const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
                     float n[3];
                     Cross(e1, e2, n);
                     Normalize3(n);
                     for (int k = 0; k < 3; k++)
                        std::copy(n, n + 3, out.At(ni, (int)o->tris[f + (size_t)k]));
                  }
               }
               c.out = Store(std::move(out), o);
               return true;
            }
            const std::vector<int> rep = WeldMap(a, c.F("dist"));
            std::vector<int> map((size_t)a.count, -1), cnt;
            PointBuilder b(a);
            for (int i = 0; i < a.count; i++)
               if (rep[(size_t)i] == i)
               {
                  map[(size_t)i] = b.Copy(i);
                  cnt.push_back(1);
               }
            Arrays& o = b.Out();
            for (int i = 0; i < a.count; i++)
            {
               if (rep[(size_t)i] == i) continue;
               const int dst = map[(size_t)rep[(size_t)i]];
               map[(size_t)i] = dst;
               for (size_t at = 0; at < o.names.size(); at++)
               {
                  const int cc = o.comps[at];
                  for (int k = 0; k < cc; k++)
                     o.data[at][(size_t)dst * cc + k] += a.data[at][(size_t)i * cc + k];
               }
               cnt[(size_t)dst]++;
            }
            for (int p = 0; p < o.count; p++)
               for (size_t at = 0; at < o.names.size(); at++)
               {
                  const int cc = o.comps[at];
                  for (int k = 0; k < cc; k++)
                     o.data[at][(size_t)p * cc + k] /= (float)cnt[(size_t)p];
               }
            auto topo = NewTopo();
            topo->pointPrims = t.pointPrims;
            for (size_t i = 0; i + 2 < t.tris.size(); i += 3)
            {
               const int x = map[t.tris[i]], y = map[t.tris[i + 1]], z = map[t.tris[i + 2]];
               if (x != y && y != z && x != z)
                  topo->tris.insert(topo->tris.end(), { (uint32_t)x, (uint32_t)y, (uint32_t)z });
            }
            for (size_t s = 0; s < t.StripCount(); s++)
            {
               std::vector<uint32_t> strip;
               for (int k = 0; k < t.lineCount[s]; k++)
               {
                  const uint32_t m = (uint32_t)map[t.lineVerts[(size_t)(t.lineStart[s] + k)]];
                  if (strip.empty() || strip.back() != m) strip.push_back(m);
               }
               if (strip.size() >= 2) topo->AddStrip(strip, t.lineClosed[s] != 0);
            }
            Arrays out = b.Take();
            if (c.Bl("normals") && topo->HasFaces())
            {
               std::vector<float> N = FaceNormals(out, *topo, false);
               const int ni = out.Ensure(attr::N, 3);
               out.data[(size_t)ni] = std::move(N);
            }
            c.out = Store(std::move(out), topo);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Primitive -------------------------------------------------------------
      {
         PopDef d = Cpu("Primitive PointZ", "Works on whole primitives: reverses faces (flips winding) and line direction, and writes "
                                         "each point's primitive number into an attribute (PrimId) for per-face effects.", 1);
         d.params = { B("reverse", "reverse", false), B("primId", "write PrimId", true), T("name", "attribute", "PrimId", "primId=1") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const PopTopology& t = c.Input(0)->Topo();
            if (c.Bl("reverse"))
            {
               auto o = std::make_shared<PopTopology>();
               o->pointPrims = t.pointPrims;
               o->tris = t.tris;
               for (size_t i = 0; i + 2 < o->tris.size(); i += 3)
                  std::swap(o->tris[i + 1], o->tris[i + 2]);
               for (size_t s = 0; s < t.StripCount(); s++)
               {
                  std::vector<uint32_t> strip(t.lineVerts.begin() + t.lineStart[s], t.lineVerts.begin() + t.lineStart[s] + t.lineCount[s]);
                  std::reverse(strip.begin(), strip.end());
                  o->AddStrip(strip, t.lineClosed[s] != 0);
               }
               c.out->topo = o;
            }
            if (c.Bl("primId"))
            {
               const std::string name = c.S("name");
               if (!ValidAttribName(name))
               {
                  c.error = "'" + name + "' is not a usable attribute name";
                  return false;
               }
               const int n = c.Input(0)->pointCount;
               std::vector<float> id((size_t)n, -1.0f);
               for (size_t i = 0; i < t.tris.size(); i++)
                  if (t.tris[i] < (uint32_t)n && id[t.tris[i]] < 0.0f) id[t.tris[i]] = (float)(i / 3);
               const float base = (float)t.TriangleCount();
               for (size_t s = 0; s < t.StripCount(); s++)
                  for (int k = 0; k < t.lineCount[s]; k++)
                  {
                     const uint32_t p = t.lineVerts[(size_t)(t.lineStart[s] + k)];
                     if (p < (uint32_t)n && id[p] < 0.0f) id[p] = base + (float)s;
                  }
               for (float& v : id)
                  if (v < 0.0f) v = 0.0f;
               c.out->Set(name, MakeBuffer(std::move(id), 1));
            }
            return true;
         };
         defs.push_back(d);
      }

      // ---- Select ----------------------------------------------------------------
      {
         PopDef d = Cpu("Select PointZ", "Keeps only the listed attributes (P always stays) and renames with 'old:new' pairs - "
                                      "to slim a PointZ down before caching, instancing or writing a file.", 1);
         d.params = { T("keep", "keep attributes (empty = all)", ""), T("rename", "rename (old:new, ...)", "") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const std::vector<std::string> keep = SplitList(c.S("keep"));
            if (!keep.empty())
            {
               std::vector<PopAttrib> kept;
               for (const PopAttrib& a : c.out->attribs)
                  if (a.name == attr::P || std::find(keep.begin(), keep.end(), a.name) != keep.end())
                     kept.push_back(a);
               c.out->attribs = kept;
            }
            for (const std::string& pair : SplitList(c.S("rename")))
            {
               const size_t colon = pair.find(':');
               if (colon == std::string::npos)
                  continue;
               const std::string from = pair.substr(0, colon), to = pair.substr(colon + 1);
               if (from == attr::P || !ValidAttribName(to))
               {
                  c.warning = "skipped rename '" + pair + "'";
                  continue;
               }
               c.out->Rename(from, to);
            }
            return true;
         };
         defs.push_back(d);
      }

      // ---- Sort ------------------------------------------------------------------
      {
         PopDef d = Cpu("Sort PointZ", "Reorders the points (faces and lines follow): by an attribute's component, by distance from a "
                                    "point, randomly, or reversed. Order matters to Trail, Line and index-based PointZ nodes.", 1);
         d.params = { M("by", "sort by", { "attribute", "distance from point", "random", "reverse" }, 0),
                      T("attrib", "attribute", "P", "by=0"), I("comp", "component", 0, 0, 3, "by=0"),
                      V("from", "point", 0, 0, 0, -20.0f, 20.0f, "by=1"), F("seed", "seed", 1.0f, 0.0f, 1000.0f, "by=2"),
                      B("desc", "descending", false) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const int n = in->pointCount, by = c.I("by");
            std::vector<float> key((size_t)n);
            if (by == 0)
            {
               const int comps = in->CompsOf(c.S("attrib"));
               const std::vector<float> v = in->Read(c.S("attrib"));
               const int k = std::max(0, std::min(comps - 1, c.I("comp")));
               for (int i = 0; i < n; i++) key[(size_t)i] = v[(size_t)i * comps + k];
            }
            else if (by == 1)
            {
               float f[3];
               c.V3("from", f);
               const std::vector<float> P = in->Read(attr::P, 3);
               for (int i = 0; i < n; i++)
               {
                  const float dx = P[(size_t)i * 3] - f[0], dy = P[(size_t)i * 3 + 1] - f[1], dz = P[(size_t)i * 3 + 2] - f[2];
                  key[(size_t)i] = dx * dx + dy * dy + dz * dz;
               }
            }
            else if (by == 2)
            {
               uint32_t st = (uint32_t)(c.F("seed") * 7919.0f) * 2654435761u + 1u;
               for (int i = 0; i < n; i++) { st ^= st << 13; st ^= st >> 17; st ^= st << 5; key[(size_t)i] = (float)st; }
            }
            else
               for (int i = 0; i < n; i++) key[(size_t)i] = (float)-i;
            std::vector<int> order((size_t)n);
            std::iota(order.begin(), order.end(), 0);
            const bool desc = c.Bl("desc");
            std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
               return desc ? key[(size_t)x] > key[(size_t)y] : key[(size_t)x] < key[(size_t)y];
            });
            Arrays a = Load(*in);
            PointBuilder b(a);
            std::vector<int> map((size_t)n);
            for (int i = 0; i < n; i++)
               map[(size_t)order[(size_t)i]] = b.Copy(order[(size_t)i]);
            c.out = Store(b.Take(), RemapTopo(in->Topo(), map));
            c.out->dims[0] = in->dims[0];
            c.out->dims[1] = in->dims[1];
            return true;
         };
         defs.push_back(d);
      }

      // ---- Topology --------------------------------------------------------------
      {
         PopDef d = Cpu("Topology PointZ", "Writes how each point sits in the mesh: Valence (how many points it connects to), "
                                        "Boundary (1 on an open edge) and PrimCount (faces using it). Optionally outputs the "
                                        "boundary edges alone as lines.", 1);
         d.params = { B("edges", "output boundary lines only", false) };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const PopData& in = *c.Input(0);
            const int n = in.pointCount;
            const PopTopology& t = in.Topo();
            std::map<std::pair<uint32_t, uint32_t>, int> edges;
            std::vector<float> prims((size_t)n, 0.0f), valence((size_t)n, 0.0f), boundary((size_t)n, 0.0f);
            for (size_t i = 0; i + 2 < t.tris.size(); i += 3)
               for (int e = 0; e < 3; e++)
               {
                  uint32_t a = t.tris[i + (size_t)e], b = t.tris[i + (size_t)((e + 1) % 3)];
                  prims[a] += 1.0f;
                  if (a > b) std::swap(a, b);
                  edges[{ a, b }]++;
               }
            for (size_t s = 0; s < t.StripCount(); s++)
               for (int k = 0; k + 1 < t.lineCount[s]; k++)
               {
                  uint32_t a = t.lineVerts[(size_t)(t.lineStart[s] + k)], b = t.lineVerts[(size_t)(t.lineStart[s] + k + 1)];
                  if (a > b) std::swap(a, b);
                  edges[{ a, b }] += 2;
               }
            auto o = NewTopo();
            for (const auto& e : edges)
            {
               valence[e.first.first] += 1.0f;
               valence[e.first.second] += 1.0f;
               if (e.second == 1)
               {
                  boundary[e.first.first] = boundary[e.first.second] = 1.0f;
                  o->AddStrip({ e.first.first, e.first.second }, false);
               }
            }
            c.out->Set("Valence", MakeBuffer(std::move(valence), 1));
            c.out->Set("Boundary", MakeBuffer(std::move(boundary), 1));
            c.out->Set("PrimCount", MakeBuffer(std::move(prims), 1));
            if (c.Bl("edges"))
               c.out->topo = o;
            return true;
         };
         defs.push_back(d);
      }

      // ---- Copy ------------------------------------------------------------------
      {
         PopDef d = Cpu("Copy PointZ", "Real copies of the geometry (unlike Instance PointZ, which draws one mesh many times): 'copies' "
                                    "stacked with a cumulative step transform, and with a template on the second input, a set "
                                    "at every template point (moved to its P, turned to its N and Rotate, scaled by PScale, "
                                    "coloured by its Color). Writes CopyId and TemplateId.", 2);
         d.inputLabels = { "geometry", "template" };
         d.params = { I("copies", "copies", 1, 1, 10000), V("t", "step translate", 0, 0, 0, -10.0f, 10.0f),
                      V("r", "step rotate", 0, 0, 0, -360.0f, 360.0f), V("s", "step scale", 1, 1, 1, 0.0f, 5.0f),
                      M("align", "align to template N", { "off", "+Z to N", "+Y to N" }, 0), F("scale", "template scale", 1.0f, 0.0f, 10.0f),
                      B("color", "stamp template Color", true), I("maxPoints", "point limit", 4000000, 1000, 50000000) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const PopData* tmpl = c.Input(1);
            const int copies = std::max(1, c.I("copies"));
            const int sites = tmpl ? std::max(0, tmpl->pointCount) : 1;
            const long long total = (long long)in->pointCount * copies * std::max(1, sites);
            if (total > (long long)c.I("maxPoints"))
            {
               c.error = "would make " + std::to_string(total) + " points, over the point limit";
               return false;
            }
            float t[3], r[3], s[3];
            c.V3("t", t); c.V3("r", r); c.V3("s", s);
            const int siteCount = std::max(1, sites);
            const int n = in->pointCount;
            if (n <= 0)
            {
               c.out = ShallowCopy(*in);
               return true;
            }
            const int pieces = copies * siteCount;
            if (CopyOnGpu(c, *in, tmpl, copies, siteCount, t, r, s))
               return true;

            // One allocation per attribute for the whole result, every piece
            // written in place in one pass (copy, transform, ids, colour),
            // split across threads. A vector of per-piece Arrays plus a Concat
            // cost ~30 ms for 7k boxes.
            const Arrays src = Load(*in);
            const bool stampColor = tmpl != nullptr && c.Bl("color");
            Arrays out;
            out.count = n * pieces;
            out.names = src.names;
            out.comps = src.comps;
            auto addAttrib = [&](const std::string& name, int comps) {
               const int i = out.Index(name);
               if (i >= 0)
                  return i;
               out.names.push_back(name);
               out.comps.push_back(comps);
               return (int)out.names.size() - 1;
            };
            const int ci = addAttrib("CopyId", 1);
            const int ti = addAttrib("TemplateId", 1);
            const int col = stampColor ? addAttrib(attr::Color, 4) : -1;
            const bool srcHasColor = col >= 0 && col < (int)src.names.size();
            const int colComps = col >= 0 ? std::min(4, out.comps[(size_t)col]) : 0;
            float colDefault[4] = { 1, 1, 1, 1 };
            DefaultValue(attr::Color, colDefault);
            out.data.resize(out.names.size());
            for (size_t a = 0; a < out.names.size(); a++)
               out.data[a].resize((size_t)out.count * (size_t)out.comps[a]);
            const int srcN = src.Index(attr::N);
            const bool hasN = srcN >= 0 && src.comps[(size_t)srcN] >= 3;

            // Cumulative step transforms, step^k, built once.
            std::vector<Mat4> steps((size_t)copies);
            {
               const Mat4 one = CopyStep(t, r, s, 1);
               Mat4 acc;
               for (int k = 0; k < copies; k++)
               {
                  steps[(size_t)k] = acc;
                  acc = Mat4::Multiply(one, acc);
               }
            }

            std::vector<float> TP, TN, TS, TR, TC;
            if (tmpl)
            {
               TP = tmpl->Read(attr::P, 3); TN = tmpl->Read(attr::N, 3); TS = tmpl->Read(attr::PScale, 1);
               TR = tmpl->Read(attr::Rotate, 3); TC = tmpl->Read(attr::Color, 4);
            }
            const int align = c.I("align");
            const float tscale = c.F("scale");
            std::vector<Mat4> places((size_t)siteCount);
            for (int site = 0; site < siteCount && tmpl; site++)
            {
               const float* p = &TP[(size_t)site * 3];
               Mat4 R = Euler(TR[(size_t)site * 3], TR[(size_t)site * 3 + 1], TR[(size_t)site * 3 + 2]);
               if (align != 0)
               {
                  // Rotation taking +Z (or +Y) onto N.
                  float from[3] = { 0, align == 2 ? 1.0f : 0.0f, align == 1 ? 1.0f : 0.0f };
                  float to[3] = { TN[(size_t)site * 3], TN[(size_t)site * 3 + 1], TN[(size_t)site * 3 + 2] };
                  Normalize3(to);
                  float v[3];
                  Cross(from, to, v);
                  const float cs = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
                  Mat4 A;
                  if (cs < -0.9999f)
                     A = Mat4::RotationX(kPi);
                  else
                  {
                     const float k = 1.0f / (1.0f + cs);
                     A.m[0] = v[0] * v[0] * k + cs; A.m[4] = v[0] * v[1] * k - v[2]; A.m[8] = v[0] * v[2] * k + v[1];
                     A.m[1] = v[1] * v[0] * k + v[2]; A.m[5] = v[1] * v[1] * k + cs; A.m[9] = v[1] * v[2] * k - v[0];
                     A.m[2] = v[2] * v[0] * k - v[1]; A.m[6] = v[2] * v[1] * k + v[0]; A.m[10] = v[2] * v[2] * k + cs;
                  }
                  R = Mat4::Multiply(A, R);
               }
               const float sc = TS[(size_t)site] * tscale;
               places[(size_t)site] = Mat4::Multiply(Mat4::Translation(p[0], p[1], p[2]), Mat4::Multiply(R, Mat4::Scale(sc, sc, sc)));
            }

            ParallelRanges(pieces, 512, [&](int firstPiece, int lastPiece) {
               for (int piece = firstPiece; piece < lastPiece; piece++)
               {
                  const int site = piece / copies, k = piece % copies;
                  const size_t first = (size_t)piece * (size_t)n;
                  for (size_t a = 0; a < src.names.size(); a++)
                  {
                     const size_t one = (size_t)n * (size_t)src.comps[a];
                     if (src.data[a].size() >= one)
                        std::memcpy(out.data[a].data() + first * (size_t)src.comps[a], src.data[a].data(), one * sizeof(float));
                  }
                  const Mat4 m = Mat4::Multiply(places[(size_t)site], steps[(size_t)k]);
                  float nm[9];
                  m.NormalMatrix(nm);
                  float* P = out.data[0].data() + first * 3;
                  for (int i = 0; i < n; i++, P += 3)
                  {
                     const float x = P[0], y = P[1], z = P[2];
                     P[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
                     P[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
                     P[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
                  }
                  if (hasN)
                  {
                     const int nc = src.comps[(size_t)srcN];
                     float* N = out.data[(size_t)srcN].data() + first * (size_t)nc;
                     for (int i = 0; i < n; i++, N += nc)
                     {
                        const float nx = N[0], ny = N[1], nz = N[2];
                        N[0] = nm[0] * nx + nm[3] * ny + nm[6] * nz;
                        N[1] = nm[1] * nx + nm[4] * ny + nm[7] * nz;
                        N[2] = nm[2] * nx + nm[5] * ny + nm[8] * nz;
                        Normalize3(N);
                     }
                  }
                  std::fill_n(out.data[(size_t)ci].data() + first, n, (float)k);
                  std::fill_n(out.data[(size_t)ti].data() + first, n, (float)site);
                  if (colComps > 0)
                  {
                     const float* tc = &TC[(size_t)site * 4];
                     const int stride = out.comps[(size_t)col];
                     float* cp = out.data[(size_t)col].data() + first * (size_t)stride;
                     for (int i = 0; i < n; i++, cp += stride)
                        for (int j = 0; j < colComps; j++)
                           cp[j] = (srcHasColor ? cp[j] : colDefault[j]) * tc[j];
                  }
               }
            });
            auto topo = ReplicateTopo(in->topo, pieces, n, &c.State<ReplicaTopoCache>());
            c.out = Store(std::move(out), topo);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Extrude / Subdivide (faces through MeshOps, lines native) ---------------
      {
         PopDef d = Cpu("Extrude PointZ", "Faces: pushes them out along their normals by 'distance' with an 'inset', building the "
                                       "side walls. Lines: sweeps each line along a direction into a ribbon of faces.", 1);
         d.params = { F("dist", "distance", 0.2f, -5.0f, 5.0f), F("inset", "inset", 0.0f, 0.0f, 1.0f),
                      V("dir", "line direction", 0, 0, 1, -5.0f, 5.0f), I("steps", "line steps", 1, 1, 256) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const PopTopology& t = in->Topo();
            if (t.HasFaces())
            {
               const float dist = c.F("dist"), inset = c.F("inset");
               c.out = ViaMesh(*in, [&](const Mesh& m) { return MeshOps::Extrude(m, dist, inset); }, c.warning);
               return true;
            }
            if (!t.HasLines())
            {
               c.warning = "needs faces or lines";
               PassInput(c);
               return true;
            }
            float dir[3];
            c.V3("dir", dir);
            const int steps = std::max(1, c.I("steps"));
            // Turbo 0.70 (P5): GPU-made lines extruded on the GPU. Each step's
            // copies are a stencil, the step's offset (dir * f, the CPU's floats)
            // is added in a kernel and N comes from GpuFaceNormals. Stencil and
            // topology are built once per topology and steps; the offsets again
            // when the direction moves. Faces still go through MeshOps (CPU).
            if (AnyOnGpu(*in) && gpu::Available())
            {
               struct ExtrudeState : OpState
               {
                  TopologyPtr src;
                  int points = -1, steps = 0;
                  gpu::GpuStencil stencil;
                  std::shared_ptr<PopData> rows, mats;
                  std::shared_ptr<PopTopology> topo;
                  float dir[3] = {};
                  GpuNormalsCache normals;
               };
               ExtrudeState& s = c.State<ExtrudeState>();
               if (s.src != in->topo || s.points != in->pointCount || s.steps != steps)
               {
                  gpu::RowStencil b;
                  std::vector<float> group;
                  auto o = NewTopo();
                  for (size_t si = 0; si < t.StripCount(); si++)
                  {
                     const int st = t.lineStart[si], n = t.lineCount[si];
                     const uint32_t base = (uint32_t)b.Rows();
                     for (int j = 0; j <= steps; j++)
                        for (int k = 0; k < n; k++)
                        {
                           b.Copy(t.lineVerts[(size_t)(st + k)]);
                           group.push_back((float)j);
                        }
                     const int segs = (t.lineClosed[si] && n > 2) ? n : n - 1;
                     for (int j = 0; j < steps; j++)
                        for (int k = 0; k < segs; k++)
                        {
                           const uint32_t a0 = base + (uint32_t)(j * n + k), a1 = base + (uint32_t)(j * n + (k + 1) % n);
                           const uint32_t b0 = a0 + (uint32_t)n, b1 = a1 + (uint32_t)n;
                           o->tris.insert(o->tris.end(), { a0, a1, b1, a0, b1, b0 });
                        }
                  }
                  s.stencil = gpu::Upload(b);
                  s.rows = std::make_shared<PopData>();
                  s.rows->Set("PzGroup", MakeBuffer(std::move(group), 1));
                  s.topo = o;
                  s.src = in->topo;
                  s.points = in->pointCount;
                  s.steps = steps;
                  s.mats.reset();
               }
               if (!s.mats || !std::equal(dir, dir + 3, s.dir))
               {
                  std::vector<Mat4> ms;
                  for (int j = 0; j <= steps; j++)
                  {
                     const float f = (float)j / steps;
                     ms.push_back(Mat4::Translation(dir[0] * f, dir[1] * f, dir[2] * f));
                  }
                  s.mats = MatrixTable(ms);
                  std::copy(dir, dir + 3, s.dir);
               }
               std::string err;
               if (s.stencil.rows > 0)
                  if (auto out = gpu::ApplyStencil(*in, s.stencil))
                  {
                     out->topo = s.topo;
                     if (GpuMoveRows(*out, *s.rows, *s.mats, true, c.env, err))
                        if (BufferPtr nb = GpuFaceNormals(*out, out->topo, false, c.env, s.normals, err))
                        {
                           out->Set(attr::N, nb);
                           c.out = out;
                           return true;
                        }
                  }
               c.warning = err; // falls through to the CPU path
            }
            Arrays a = Load(*in);
            PointBuilder b(a);
            auto o = NewTopo();
            for (size_t s = 0; s < t.StripCount(); s++)
            {
               const int st = t.lineStart[s], n = t.lineCount[s];
               const int base = b.Count();
               for (int j = 0; j <= steps; j++)
                  for (int k = 0; k < n; k++)
                  {
                     const int src = (int)t.lineVerts[(size_t)(st + k)];
                     const int p = b.Copy(src);
                     const float f = (float)j / steps;
                     const float* q = a.Pos(src);
                     b.SetPos(p, q[0] + dir[0] * f, q[1] + dir[1] * f, q[2] + dir[2] * f);
                  }
               const int segs = (t.lineClosed[s] && n > 2) ? n : n - 1;
               for (int j = 0; j < steps; j++)
                  for (int k = 0; k < segs; k++)
                  {
                     const uint32_t a0 = (uint32_t)(base + j * n + k), a1 = (uint32_t)(base + j * n + (k + 1) % n);
                     const uint32_t b0 = a0 + (uint32_t)n, b1 = a1 + (uint32_t)n;
                     o->tris.insert(o->tris.end(), { a0, a1, b1, a0, b1, b0 });
                  }
            }
            Arrays out = b.Take();
            std::vector<float> N = FaceNormals(out, *o, false);
            const int ni = out.Ensure(attr::N, 3);
            out.data[(size_t)ni] = std::move(N);
            c.out = Store(std::move(out), o);
            return true;
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("Subdivide PointZ", "Faces: splits each face into four per level, optionally smoothing toward a rounded surface. "
                                         "Lines: inserts points between, corner-cutting (Chaikin) when 'smooth' is on.", 1);
         d.params = { I("levels", "levels", 1, 1, 5), F("smooth", "smooth", 1.0f, 0.0f, 1.0f) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const PopTopology& t = in->Topo();
            const int levels = std::max(1, std::min(5, c.I("levels")));
            const float smooth = c.F("smooth");
            if (t.HasFaces())
            {
               if ((long long)t.TriangleCount() << (2 * levels) > 8000000LL)
               {
                  c.error = "too many faces for that many levels";
                  return false;
               }
               c.out = ViaMesh(*in, [&](const Mesh& m) { return MeshOps::Subdivide(m, levels, smooth); }, c.warning);
               return true;
            }
            if (!t.HasLines())
            {
               PassInput(c);
               c.warning = "needs faces or lines";
               return true;
            }
            // Turbo 0.70 (P5): lines of a GPU-made input subdivided on the GPU.
            // Each level's stencil (copies and Chaikin lerps, in the CPU loop's
            // order) and topology are built once per input topology / levels /
            // smooth; gpu::WeightedRows applies them, bit for bit the CPU sums.
            bool onGpu = false;
            for (const PopAttrib& at : in->attribs)
               onGpu = onGpu || !at.buffer->OnCpu();
            if (onGpu && gpu::Available())
            {
               struct Level
               {
                  gpu::GpuStencil stencil;
                  std::shared_ptr<PopTopology> topo;
               };
               struct SubdivState : OpState
               {
                  TopologyPtr src;
                  int points = -1, levels = 0;
                  float smooth = -1.0f;
                  std::vector<Level> steps;
               };
               SubdivState& s = c.State<SubdivState>();
               if (s.src != in->topo || s.points != in->pointCount || s.levels != levels || s.smooth != smooth)
               {
                  s.steps.clear();
                  const PopTopology* ct = &t;
                  std::shared_ptr<PopTopology> keepAlive;
                  for (int lv = 0; lv < levels; lv++)
                  {
                     gpu::RowStencil b;
                     auto o = NewTopo();
                     for (size_t si = 0; si < ct->StripCount(); si++)
                     {
                        const int st = ct->lineStart[si], n = ct->lineCount[si];
                        const bool closed = ct->lineClosed[si] && n > 2;
                        std::vector<uint32_t> strip;
                        const int segs = closed ? n : n - 1;
                        if (!closed)
                           strip.push_back(b.Copy(ct->lineVerts[(size_t)st]));
                        for (int k = 0; k < segs; k++)
                        {
                           const uint32_t i0 = ct->lineVerts[(size_t)(st + k)], i1 = ct->lineVerts[(size_t)(st + (k + 1) % n)];
                           const float q = 0.25f * smooth;
                           strip.push_back(b.Lerp(i0, i1, q));
                           strip.push_back(b.Lerp(i0, i1, 1.0f - q));
                        }
                        if (!closed)
                           strip.push_back(b.Copy(ct->lineVerts[(size_t)(st + n - 1)]));
                        o->AddStrip(strip, closed);
                     }
                     Level L;
                     L.stencil = gpu::Upload(b);
                     L.topo = o;
                     s.steps.push_back(L);
                     keepAlive = o;
                     ct = keepAlive.get();
                  }
                  s.src = in->topo;
                  s.points = in->pointCount;
                  s.levels = levels;
                  s.smooth = smooth;
               }
               std::shared_ptr<PopData> cur = ShallowCopy(*in);
               bool ok = true;
               for (const Level& L : s.steps)
               {
                  auto next = gpu::ApplyStencil(*cur, L.stencil);
                  ok = next != nullptr;
                  if (!ok)
                     break;
                  next->topo = L.topo;
                  cur = next;
               }
               if (ok)
               {
                  c.out = cur;
                  return true;
               }
            }
            std::shared_ptr<PopData> cur = ShallowCopy(*in);
            for (int lv = 0; lv < levels; lv++)
            {
               Arrays a = Load(*cur);
               const PopTopology& ct = cur->Topo();
               PointBuilder b(a);
               auto o = NewTopo();
               for (size_t s = 0; s < ct.StripCount(); s++)
               {
                  const int st = ct.lineStart[s], n = ct.lineCount[s];
                  const bool closed = ct.lineClosed[s] && n > 2;
                  std::vector<uint32_t> strip;
                  const int segs = closed ? n : n - 1;
                  if (!closed)
                     strip.push_back((uint32_t)b.Copy((int)ct.lineVerts[(size_t)st]));
                  for (int k = 0; k < segs; k++)
                  {
                     const int i0 = (int)ct.lineVerts[(size_t)(st + k)], i1 = (int)ct.lineVerts[(size_t)(st + (k + 1) % n)];
                     // Chaikin: 1/4 and 3/4 when smooth, midpoint + original otherwise.
                     const float q = 0.25f * smooth;
                     strip.push_back((uint32_t)b.Lerp(i0, i1, q));
                     strip.push_back((uint32_t)b.Lerp(i0, i1, 1.0f - q));
                  }
                  if (!closed)
                     strip.push_back((uint32_t)b.Copy((int)ct.lineVerts[(size_t)(st + n - 1)]));
                  o->AddStrip(strip, closed);
               }
               cur = Store(b.Take(), o);
            }
            c.out = cur;
            return true;
         };
         defs.push_back(d);
      }

      // ---- Sprinkle --------------------------------------------------------------
      {
         PopDef d = Cpu("Sprinkle PointZ", "Scatters points over the input's surface, evenly by area (or one count per face), each "
                                        "carrying the attributes interpolated from where it landed. Output: point primitives.", 1);
         d.params = { I("count", "points", 2000, 1, 4000000), M("method", "method", { "by area", "per face" }, 0),
                      F("seed", "seed", 1.0f, 0.0f, 1000.0f) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const PopTopology& t = in->Topo();
            if (!t.HasFaces())
            {
               c.warning = "needs faces to sprinkle on";
               c.out = std::make_shared<PopData>();
               return true;
            }
            Arrays a = Load(*in);
            const size_t faces = t.TriangleCount();
            std::vector<double> cdf(faces);
            double sum = 0.0;
            for (size_t f = 0; f < faces; f++)
            {
               const float* p0 = a.Pos((int)t.tris[f * 3]);
               const float* p1 = a.Pos((int)t.tris[f * 3 + 1]);
               const float* p2 = a.Pos((int)t.tris[f * 3 + 2]);
               const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
               const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
               float n[3];
               Cross(e1, e2, n);
               sum += 0.5 * std::sqrt((double)n[0] * n[0] + (double)n[1] * n[1] + (double)n[2] * n[2]);
               cdf[f] = sum;
            }
            uint32_t st = (uint32_t)(c.F("seed") * 7919.0f) * 2654435761u + 1u;
            auto rnd = [&]() { st ^= st << 13; st ^= st >> 17; st ^= st << 5; return (float)(st & 0xffffff) / 16777216.0f; };
            PointBuilder b(a);
            const int count = std::max(1, c.I("count"));
            const bool perFace = c.I("method") == 1;
            const size_t total = perFace ? faces * (size_t)count : (size_t)count;
            if (total > 8000000)
            {
               c.error = "too many points";
               return false;
            }
            for (size_t k = 0; k < total; k++)
            {
               size_t f;
               if (perFace)
                  f = k / (size_t)count;
               else
               {
                  const double r = rnd() * sum;
                  f = (size_t)(std::lower_bound(cdf.begin(), cdf.end(), r) - cdf.begin());
                  if (f >= faces) f = faces - 1;
               }
               float u = rnd(), v = rnd();
               if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
               b.Bary((int)t.tris[f * 3], (int)t.tris[f * 3 + 1], (int)t.tris[f * 3 + 2], u, v);
            }
            auto topo = NewTopo();
            topo->pointPrims = true;
            c.out = Store(b.Take(), topo);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Revolve / Curve -------------------------------------------------------
      {
         PopDef d = Cpu("Revolve PointZ", "Spins a profile line around an axis into a surface (a lathe): 'divisions' around, 'angle' "
                                       "for a partial sweep. Every attribute of the profile carries round.", 1);
         d.params = { M("axis", "axis", Axes(), 1), V("origin", "axis origin", 0, 0, 0, -20.0f, 20.0f), F("angle", "angle", 360.0f, 0.0f, 360.0f),
                      I("div", "divisions", 32, 3, 1024) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const PopTopology& t = in->Topo();
            // Turbo 0.70 (P5): a GPU-made profile revolved on the GPU. The rings'
            // copies are a stencil, each ring's matrix (the CPU's floats) moves P
            // in a kernel, Tex comes from a table and N from GpuFaceNormals.
            // Stencil, tables and topology are built once per topology and
            // divisions; the matrices again when the angle, axis or origin move.
            const PopAttrib* inTex = in->Find(attr::Tex);
            if (AnyOnGpu(*in) && gpu::Available() && (inTex == nullptr || inTex->buffer->Comps() == 3))
            {
               struct RevolveState : OpState
               {
                  TopologyPtr src;
                  int points = -1, div = 0, rings = 0, n = 0;
                  gpu::GpuStencil stencil;
                  std::shared_ptr<PopData> rows, mats;
                  std::shared_ptr<PopTopology> topo;
                  float key[6] = {};
                  GpuNormalsCache normals;
               };
               RevolveState& s = c.State<RevolveState>();
               const int div = std::max(3, c.I("div"));
               const bool full = c.F("angle") >= 359.999f;
               const int rings = full ? div : div + 1;
               if (s.src != in->topo || s.points != in->pointCount || s.div != div || s.rings != rings)
               {
                  std::vector<uint32_t> profile;
                  if (t.HasLines())
                     for (int k = 0; k < t.lineCount[0]; k++) profile.push_back(t.lineVerts[(size_t)(t.lineStart[0] + k)]);
                  else
                     for (int i = 0; i < in->pointCount; i++) profile.push_back((uint32_t)i);
                  const int n = (int)profile.size();
                  s.stencil = gpu::GpuStencil();
                  s.n = n;
                  if (n >= 2)
                  {
                     gpu::RowStencil b;
                     std::vector<float> group, tex;
                     for (int j = 0; j < rings; j++)
                        for (int k = 0; k < n; k++)
                        {
                           b.Copy(profile[(size_t)k]);
                           group.push_back((float)j);
                           tex.insert(tex.end(), { (float)j / div, (float)k / (n - 1), 0.0f });
                        }
                     auto topo = NewTopo();
                     for (int j = 0; j < div; j++)
                        for (int k = 0; k + 1 < n; k++)
                        {
                           const uint32_t a0 = (uint32_t)(j * n + k), a1 = a0 + 1;
                           const uint32_t b0 = (uint32_t)(((j + 1) % rings) * n + k), b1 = b0 + 1;
                           topo->tris.insert(topo->tris.end(), { a0, b0, b1, a0, b1, a1 });
                        }
                     s.stencil = gpu::Upload(b);
                     s.rows = std::make_shared<PopData>();
                     s.rows->Set("PzGroup", MakeBuffer(std::move(group), 1));
                     s.rows->Set("PzTex", MakeBuffer(std::move(tex), 3));
                     s.topo = topo;
                  }
                  s.src = in->topo;
                  s.points = in->pointCount;
                  s.div = div;
                  s.rings = rings;
                  s.mats.reset();
               }
               if (s.n < 2)
               {
                  c.warning = "needs a line (or points in order) as the profile";
                  return true;
               }
               float o[3];
               c.V3("origin", o);
               const float key[6] = { c.F("angle"), (float)c.I("axis"), o[0], o[1], o[2], (float)div };
               if (!s.mats || !std::equal(key, key + 6, s.key))
               {
                  const float ang = c.F("angle") * kPi / 180.0f;
                  const int ax = c.I("axis");
                  std::vector<Mat4> ms;
                  for (int j = 0; j < rings; j++)
                  {
                     const float th = ang * (float)j / div;
                     const Mat4 R = ax == 0 ? Mat4::RotationX(th) : (ax == 1 ? Mat4::RotationY(th) : Mat4::RotationZ(th));
                     ms.push_back(Mat4::Multiply(Mat4::Translation(o[0], o[1], o[2]),
                                                 Mat4::Multiply(R, Mat4::Translation(-o[0], -o[1], -o[2]))));
                  }
                  s.mats = MatrixTable(ms);
                  std::copy(key, key + 6, s.key);
               }
               std::string err;
               if (auto out = gpu::ApplyStencil(*in, s.stencil))
               {
                  out->topo = s.topo;
                  if (GpuMoveRows(*out, *s.rows, *s.mats, false, c.env, err))
                     if (BufferPtr nb = GpuFaceNormals(*out, out->topo, false, c.env, s.normals, err))
                     {
                        out->Set(attr::N, nb);
                        out->dims[0] = rings;
                        out->dims[1] = s.n;
                        c.out = out;
                        return true;
                     }
               }
               c.warning = err; // falls through to the CPU path
            }
            Arrays a = Load(*in);
            std::vector<int> profile;
            if (t.HasLines())
               for (int k = 0; k < t.lineCount[0]; k++) profile.push_back((int)t.lineVerts[(size_t)(t.lineStart[0] + k)]);
            else
               for (int i = 0; i < a.count; i++) profile.push_back(i);
            if (profile.size() < 2)
            {
               c.warning = "needs a line (or points in order) as the profile";
               return true;
            }
            const int div = std::max(3, c.I("div"));
            const float ang = c.F("angle") * kPi / 180.0f;
            const bool full = c.F("angle") >= 359.999f;
            const int rings = full ? div : div + 1;
            float o[3];
            c.V3("origin", o);
            const int ax = c.I("axis");
            PointBuilder b(a);
            const int n = (int)profile.size();
            for (int j = 0; j < rings; j++)
            {
               const float th = ang * (float)j / div;
               const Mat4 R = ax == 0 ? Mat4::RotationX(th) : (ax == 1 ? Mat4::RotationY(th) : Mat4::RotationZ(th));
               const Mat4 M = Mat4::Multiply(Mat4::Translation(o[0], o[1], o[2]), Mat4::Multiply(R, Mat4::Translation(-o[0], -o[1], -o[2])));
               const int first = b.Count();
               for (int k = 0; k < n; k++)
               {
                  const int p = b.Copy(profile[(size_t)k]);
                  const float tex[3] = { (float)j / div, (float)k / (n - 1), 0.0f };
                  b.Set(p, attr::Tex, tex, 3);
               }
               TransformArrays(b.Out(), first, b.Count(), M);
            }
            auto topo = NewTopo();
            for (int j = 0; j < div; j++)
               for (int k = 0; k + 1 < n; k++)
               {
                  const uint32_t a0 = (uint32_t)(j * n + k), a1 = a0 + 1;
                  const uint32_t b0 = (uint32_t)(((j + 1) % rings) * n + k), b1 = b0 + 1;
                  topo->tris.insert(topo->tris.end(), { a0, b0, b1, a0, b1, a1 });
               }
            Arrays out = b.Take();
            std::vector<float> N = FaceNormals(out, *topo, false);
            const int ni = out.Ensure(attr::N, 3);
            out.data[(size_t)ni] = std::move(N);
            c.out = Store(std::move(out), topo);
            c.out->dims[0] = rings;
            c.out->dims[1] = n;
            return true;
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("Curve PointZ", "Turns lines (or points in order) into smooth curves through them: Catmull-Rom through every "
                                     "point, or B-spline near them, with 'divisions' points per segment.", 1);
         d.params = { M("type", "type", { "Catmull-Rom (through points)", "B-spline (smoother)" }, 0), I("div", "divisions", 8, 1, 256),
                      B("close", "close open lines", false) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const PopTopology& t = in->Topo();
            Arrays a = Load(*in);
            std::vector<std::vector<int>> strips;
            std::vector<bool> closed;
            if (t.HasLines())
               for (size_t s = 0; s < t.StripCount(); s++)
               {
                  std::vector<int> st;
                  for (int k = 0; k < t.lineCount[s]; k++) st.push_back((int)t.lineVerts[(size_t)(t.lineStart[s] + k)]);
                  strips.push_back(st);
                  closed.push_back(t.lineClosed[s] != 0 || c.Bl("close"));
               }
            else
            {
               std::vector<int> st((size_t)a.count);
               std::iota(st.begin(), st.end(), 0);
               strips.push_back(st);
               closed.push_back(c.Bl("close"));
            }
            const int div = std::max(1, c.I("div"));
            const bool bspline = c.I("type") == 1;
            PointBuilder b(a);
            auto o = NewTopo();
            for (size_t s = 0; s < strips.size(); s++)
            {
               const std::vector<int>& cp = strips[s];
               const int n = (int)cp.size();
               if (n < 2) continue;
               const bool cl = closed[s] && n > 2;
               auto at = [&](int i) { return cl ? cp[(size_t)((i % n + n) % n)] : cp[(size_t)std::max(0, std::min(n - 1, i))]; };
               std::vector<uint32_t> strip;
               const int segs = cl ? n : n - 1;
               for (int k = 0; k < segs; k++)
                  for (int j = 0; j < div; j++)
                  {
                     const float u = (float)j / div, u2 = u * u, u3 = u2 * u;
                     float w[4];
                     if (bspline)
                     {
                        w[0] = (1 - u) * (1 - u) * (1 - u) / 6.0f;
                        w[1] = (3 * u3 - 6 * u2 + 4) / 6.0f;
                        w[2] = (-3 * u3 + 3 * u2 + 3 * u + 1) / 6.0f;
                        w[3] = u3 / 6.0f;
                     }
                     else
                     {
                        w[0] = 0.5f * (-u3 + 2 * u2 - u);
                        w[1] = 0.5f * (3 * u3 - 5 * u2 + 2);
                        w[2] = 0.5f * (-3 * u3 + 4 * u2 + u);
                        w[3] = 0.5f * (u3 - u2);
                     }
                     const int idx[4] = { at(k - 1), at(k), at(k + 1), at(k + 2) };
                     strip.push_back((uint32_t)b.Weighted(idx, w, 4));
                  }
               if (!cl)
                  strip.push_back((uint32_t)b.Copy(cp.back()));
               o->AddStrip(strip, cl);
            }
            c.out = Store(b.Take(), o);
            return true;
         };
         defs.push_back(d);
      }
   }

   void RegisterLinesB(std::vector<PopDef>& defs);
   void RegisterTimeB(std::vector<PopDef>& defs);

   void RegisterTierB(std::vector<PopDef>& defs)
   {
      RegisterTopologyB(defs);
      RegisterLinesB(defs);
      RegisterTimeB(defs);
   }
}
