// PointZ operators, tier B (second part): line operators and analysis.

#include "PopOpsInternal.h"
#include "PopCpu.h"
#include "GpuAlgo.h"
#include "PopGpu.h"

#include <numeric>

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

      struct Strip
      {
         std::vector<int> pts;
         bool closed = false;
      };

      std::vector<Strip> Strips(const PopTopology& t)
      {
         std::vector<Strip> out;
         for (size_t s = 0; s < t.StripCount(); s++)
         {
            Strip st;
            for (int k = 0; k < t.lineCount[s]; k++)
               st.pts.push_back((int)t.lineVerts[(size_t)(t.lineStart[s] + k)]);
            st.closed = t.lineClosed[s] != 0 && st.pts.size() > 2;
            out.push_back(st);
         }
         return out;
      }

      float Dist(const Arrays& a, int i, int j)
      {
         const float* p = a.Pos(i);
         const float* q = a.Pos(j);
         const float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
         return std::sqrt(dx * dx + dy * dy + dz * dz);
      }

      bool NeedLines(CookCtx& c)
      {
         if (!PassInput(c))
            return false;
         if (!c.Input(0)->Topo().HasLines())
         {
            c.warning = "needs lines (Convert PointZ turns points or edges into lines)";
            return false;
         }
         return true;
      }
   }

   void RegisterLinesB(std::vector<PopDef>& defs)
   {
      // ---- Line Resample ---------------------------------------------------------
      {
         PopDef d = Cpu("Line Resample PointZ", "Re-spaces the points of every line evenly along its length: a fixed number per "
                                             "line, or one every 'length'. Every attribute is interpolated.", 1);
         d.params = { M("by", "by", { "count", "segment length" }, 0), I("count", "points", 64, 2, 100000, "by=0"),
                      F("length", "segment length", 0.05f, 0.0001f, 10.0f, "by=1") };
         d.cook = [](CookCtx& c) {
            if (!NeedLines(c))
               return true;
            // Turbo 0.70 (P6): a GPU-made line resampled by count on the GPU. The
            // layout (which line, which row) comes from the topology, once per
            // topology and count; the segments and lerp factors from the lengths
            // summed on the GPU (gpu::ResampleStencil), every attribute through
            // ApplyStencil - nothing comes down. By segment length the number of
            // rows depends on the length itself, so that mode stays on the CPU.
            {
               const PopData& in = *c.Input(0);
               bool onGpu = false;
               for (const PopAttrib& at : in.attribs)
                  onGpu = onGpu || !at.buffer->OnCpu();
               if (c.I("by") == 0 && onGpu && gpu::Available())
               {
                  struct ResampleState : OpState
                  {
                     TopologyPtr src;
                     int points = -1, count = 0;
                     gpu::ResampleLayout layout;
                     std::shared_ptr<PopTopology> topo;
                  };
                  ResampleState& s = c.State<ResampleState>();
                  const int want = std::max(2, c.I("count"));
                  if (s.src != in.topo || s.points != in.pointCount || s.count != want)
                  {
                     std::vector<uint32_t> pts, start, len, rowStrip, rowJ, offs = { 0 };
                     std::vector<float> den;
                     auto o = NewTopo();
                     int strips = 0;
                     for (const Strip& st : Strips(in.Topo()))
                     {
                        const int n = (int)st.pts.size() + (st.closed ? 1 : 0);
                        if (n < 2)
                           continue;
                        const int count = st.closed ? std::max(3, want) : want;
                        const int rows = st.closed ? count - 1 : count;
                        start.push_back((uint32_t)pts.size());
                        len.push_back((uint32_t)n);
                        den.push_back((float)(count - 1));
                        for (int p : st.pts)
                           pts.push_back((uint32_t)p);
                        if (st.closed)
                           pts.push_back((uint32_t)st.pts.front());
                        std::vector<uint32_t> strip;
                        for (int j = 0; j < rows; j++)
                        {
                           strip.push_back((uint32_t)rowJ.size());
                           rowStrip.push_back((uint32_t)strips);
                           rowJ.push_back((uint32_t)j);
                           offs.push_back(offs.back() + 2);
                        }
                        o->AddStrip(strip, st.closed);
                        strips++;
                     }
                     gpu::ResampleLayout& l = s.layout;
                     l.strips = strips;
                     l.rows = (int)rowJ.size();
                     l.ptsCount = (int)pts.size();
                     l.pts = gpu::UintBuffer(pts);
                     l.start = gpu::UintBuffer(start);
                     l.len = gpu::UintBuffer(len);
                     l.den = MakeBuffer(den.empty() ? std::vector<float>(1, 1.0f) : std::move(den), 1);
                     l.rowStrip = gpu::UintBuffer(rowStrip);
                     l.rowJ = gpu::UintBuffer(rowJ);
                     l.offs = gpu::UintBuffer(offs);
                     s.topo = o;
                     s.src = in.topo;
                     s.points = in.pointCount;
                     s.count = want;
                  }
                  gpu::GpuStencil stencil;
                  if (gpu::ResampleStencil(in, s.layout, stencil))
                     if (auto out = gpu::ApplyStencil(in, stencil))
                     {
                        out->topo = s.topo;
                        c.out = out;
                        return true;
                     }
               }
            }
            Arrays a = Load(*c.Input(0));
            PointBuilder b(a);
            auto o = NewTopo();
            for (const Strip& s : Strips(c.Input(0)->Topo()))
            {
               std::vector<int> pts = s.pts;
               if (s.closed) pts.push_back(pts.front());
               const int n = (int)pts.size();
               if (n < 2) continue;
               std::vector<float> cum((size_t)n, 0.0f);
               for (int k = 1; k < n; k++) cum[(size_t)k] = cum[(size_t)k - 1] + Dist(a, pts[(size_t)k - 1], pts[(size_t)k]);
               const float L = cum.back();
               int count = c.I("by") == 0 ? std::max(2, c.I("count")) : std::max(2, (int)std::ceil(L / std::max(1e-6f, c.F("length"))) + 1);
               if (s.closed) count = std::max(3, count);
               const int emit = s.closed ? count - 1 : count;
               std::vector<uint32_t> strip;
               int seg = 0;
               for (int j = 0; j < (s.closed ? count - 1 : count); j++)
               {
                  const float target = L * (float)j / (float)(count - 1);
                  while (seg < n - 2 && cum[(size_t)seg + 1] < target) seg++;
                  const float span = cum[(size_t)seg + 1] - cum[(size_t)seg];
                  const float t = span > 1e-12f ? (target - cum[(size_t)seg]) / span : 0.0f;
                  strip.push_back((uint32_t)b.Lerp(pts[(size_t)seg], pts[(size_t)seg + 1], std::max(0.0f, std::min(1.0f, t))));
               }
               (void)emit;
               o->AddStrip(strip, s.closed);
            }
            c.out = Store(b.Take(), o);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Line Divide -----------------------------------------------------------
      {
         PopDef d = Cpu("Line Divide PointZ", "Adds points inside every segment of every line: straight (linear) or along a smooth "
                                           "curve through the points (Catmull-Rom). Unlike Line Resample the original points stay.", 1);
         d.params = { I("div", "divisions per segment", 4, 1, 256), M("interp", "interpolation", { "linear", "smooth" }, 1) };
         d.cook = [](CookCtx& c) {
            if (!NeedLines(c))
               return true;
            const int div = std::max(1, c.I("div"));
            const bool smooth = c.I("interp") == 1;
            // Turbo 0.70 (P6): a GPU-made line divided on the GPU - the same
            // Copy / Lerp / Catmull-Rom rows as below as a stencil, built once per
            // topology and settings, applied by gpu::ApplyStencil (same numbers).
            {
               const PopData& in = *c.Input(0);
               bool onGpu = false;
               for (const PopAttrib& at : in.attribs)
                  onGpu = onGpu || !at.buffer->OnCpu();
               if (onGpu && gpu::Available())
               {
                  struct DivideState : OpState
                  {
                     TopologyPtr src;
                     int points = -1, div = 0;
                     bool smooth = false;
                     gpu::GpuStencil stencil;
                     std::shared_ptr<PopTopology> topo;
                  };
                  DivideState& s = c.State<DivideState>();
                  if (s.src != in.topo || s.points != in.pointCount || s.div != div || s.smooth != smooth)
                  {
                     gpu::RowStencil b;
                     auto o = NewTopo();
                     for (const Strip& st : Strips(in.Topo()))
                     {
                        const int n = (int)st.pts.size();
                        if (n < 2)
                           continue;
                        auto at = [&](int i) {
                           return st.closed ? st.pts[(size_t)((i % n + n) % n)] : st.pts[(size_t)std::max(0, std::min(n - 1, i))];
                        };
                        std::vector<uint32_t> strip;
                        const int segs = st.closed ? n : n - 1;
                        for (int k = 0; k < segs; k++)
                           for (int j = 0; j < div; j++)
                           {
                              const float u = (float)j / div;
                              if (!smooth || j == 0)
                              {
                                 strip.push_back(j == 0 ? b.Copy((uint32_t)at(k)) : b.Lerp((uint32_t)at(k), (uint32_t)at(k + 1), u));
                                 continue;
                              }
                              const float u2 = u * u, u3 = u2 * u;
                              const float w[4] = { 0.5f * (-u3 + 2 * u2 - u), 0.5f * (3 * u3 - 5 * u2 + 2),
                                                   0.5f * (-3 * u3 + 4 * u2 + u), 0.5f * (u3 - u2) };
                              const int idx[4] = { at(k - 1), at(k), at(k + 1), at(k + 2) };
                              strip.push_back(b.Weighted(idx, w, 4));
                           }
                        if (!st.closed)
                           strip.push_back(b.Copy((uint32_t)st.pts.back()));
                        o->AddStrip(strip, st.closed);
                     }
                     s.stencil = gpu::Upload(b);
                     s.topo = o;
                     s.src = in.topo;
                     s.points = in.pointCount;
                     s.div = div;
                     s.smooth = smooth;
                  }
                  if (auto out = gpu::ApplyStencil(in, s.stencil))
                  {
                     out->topo = s.topo;
                     c.out = out;
                     return true;
                  }
               }
            }
            Arrays a = Load(*c.Input(0));
            PointBuilder b(a);
            auto o = NewTopo();
            for (const Strip& s : Strips(c.Input(0)->Topo()))
            {
               const int n = (int)s.pts.size();
               if (n < 2) continue;
               auto at = [&](int i) { return s.closed ? s.pts[(size_t)((i % n + n) % n)] : s.pts[(size_t)std::max(0, std::min(n - 1, i))]; };
               std::vector<uint32_t> strip;
               const int segs = s.closed ? n : n - 1;
               for (int k = 0; k < segs; k++)
                  for (int j = 0; j < div; j++)
                  {
                     const float u = (float)j / div;
                     if (!smooth || j == 0)
                     {
                        strip.push_back((uint32_t)(j == 0 ? b.Copy(at(k)) : b.Lerp(at(k), at(k + 1), u)));
                        continue;
                     }
                     const float u2 = u * u, u3 = u2 * u;
                     const float w[4] = { 0.5f * (-u3 + 2 * u2 - u), 0.5f * (3 * u3 - 5 * u2 + 2), 0.5f * (-3 * u3 + 4 * u2 + u), 0.5f * (u3 - u2) };
                     const int idx[4] = { at(k - 1), at(k), at(k + 1), at(k + 2) };
                     strip.push_back((uint32_t)b.Weighted(idx, w, 4));
                  }
               if (!s.closed)
                  strip.push_back((uint32_t)b.Copy(s.pts.back()));
               o->AddStrip(strip, s.closed);
            }
            c.out = Store(b.Take(), o);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Line Break ------------------------------------------------------------
      {
         PopDef d = Cpu("Line Break PointZ", "Cuts lines into pieces: every N points, at corners sharper than 'angle', or where an "
                                          "attribute is over 'threshold'.", 1);
         d.params = { M("by", "break", { "every N points", "at sharp corners", "where attribute > threshold" }, 0),
                      I("every", "every", 8, 2, 100000, "by=0"), F("angle", "angle", 45.0f, 1.0f, 179.0f, "by=1"),
                      T("attrib", "attribute", "Group", "by=2"), F("threshold", "threshold", 0.5f, -10.0f, 10.0f, "by=2") };
         d.cook = [](CookCtx& c) {
            if (!NeedLines(c))
               return true;
            const PopData& in = *c.Input(0);
            Arrays a = Load(in);
            const int by = c.I("by");
            const std::vector<float> attrv = by == 2 ? in.Read(c.S("attrib"), 1) : std::vector<float>();
            const float cosLim = std::cos(c.F("angle") * kPi / 180.0f);
            auto o = NewTopo();
            for (const Strip& s : Strips(in.Topo()))
            {
               std::vector<uint32_t> cur;
               bool brokeHere = false;
               const int n = (int)s.pts.size();
               for (int k = 0; k < n; k++)
               {
                  const int p = s.pts[(size_t)k];
                  cur.push_back((uint32_t)p);
                  bool cut = false;
                  if (k > 0 && k + 1 < n)
                  {
                     if (by == 0) cut = (int)cur.size() >= std::max(2, c.I("every"));
                     else if (by == 1)
                     {
                        const float* p0 = a.Pos(s.pts[(size_t)k - 1]);
                        const float* p1 = a.Pos(p);
                        const float* p2 = a.Pos(s.pts[(size_t)k + 1]);
                        float d1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] }, d2[3] = { p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] };
                        const float l1 = std::sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]), l2 = std::sqrt(d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2]);
                        if (l1 > 1e-9f && l2 > 1e-9f)
                           cut = (d1[0] * d2[0] + d1[1] * d2[1] + d1[2] * d2[2]) / (l1 * l2) < cosLim;
                     }
                     else cut = attrv[(size_t)p] > c.F("threshold");
                  }
                  if (cut)
                  {
                     brokeHere = true;
                     if (cur.size() >= 2) o->AddStrip(cur, false);
                     cur.assign(1, (uint32_t)p); // the break point starts the next piece
                  }
               }
               if (cur.size() >= 2) o->AddStrip(cur, s.closed && !brokeHere);
            }
            c.out->topo = o;
            return true;
         };
         defs.push_back(d);
      }

      // ---- Line Metrics ----------------------------------------------------------
      {
         PopDef d = Cpu("Line Metrics PointZ", "Measures every line and writes per point: Arc (distance along it), ArcU (0..1 along "
                                            "it), LineId, LineLen and the tangent T. Use ArcU to colour, grow or animate along lines.", 1);
         d.cook = [](CookCtx& c) {
            if (!NeedLines(c))
               return true;
            const PopData& in = *c.Input(0);
            Arrays a = Load(in);
            const int n = in.pointCount;
            std::vector<float> arc((size_t)n, 0.0f), u((size_t)n, 0.0f), id((size_t)n, 0.0f), len((size_t)n, 0.0f), T((size_t)n * 3, 0.0f);
            int sid = 0;
            for (const Strip& s : Strips(in.Topo()))
            {
               float cum = 0.0f;
               std::vector<float> at(s.pts.size(), 0.0f);
               for (size_t k = 1; k < s.pts.size(); k++) { cum += Dist(a, s.pts[k - 1], s.pts[k]); at[k] = cum; }
               const float total = cum + (s.closed ? Dist(a, s.pts.back(), s.pts.front()) : 0.0f);
               for (size_t k = 0; k < s.pts.size(); k++)
               {
                  const int p = s.pts[k];
                  arc[(size_t)p] = at[k];
                  u[(size_t)p] = total > 1e-12f ? at[k] / total : 0.0f;
                  id[(size_t)p] = (float)sid;
                  len[(size_t)p] = total;
                  const int i0 = s.pts[k > 0 ? k - 1 : (s.closed ? s.pts.size() - 1 : 0)];
                  const int i1 = s.pts[k + 1 < s.pts.size() ? k + 1 : (s.closed ? 0 : s.pts.size() - 1)];
                  float d3[3] = { a.Pos(i1)[0] - a.Pos(i0)[0], a.Pos(i1)[1] - a.Pos(i0)[1], a.Pos(i1)[2] - a.Pos(i0)[2] };
                  const float l = std::sqrt(d3[0] * d3[0] + d3[1] * d3[1] + d3[2] * d3[2]);
                  for (int j = 0; j < 3; j++) T[(size_t)p * 3 + j] = l > 1e-12f ? d3[j] / l : 0.0f;
               }
               sid++;
            }
            c.out->Set("Arc", MakeBuffer(std::move(arc), 1));
            c.out->Set("ArcU", MakeBuffer(std::move(u), 1));
            c.out->Set("LineId", MakeBuffer(std::move(id), 1));
            c.out->Set("LineLen", MakeBuffer(std::move(len), 1));
            c.out->Set("T", MakeBuffer(std::move(T), 3));
            return true;
         };
         defs.push_back(d);
      }

      // ---- Line Smooth -----------------------------------------------------------
      {
         PopDef d = Cpu("Line Smooth PointZ", "Relaxes lines: each pass pulls every point toward the middle of its neighbours by "
                                           "'strength'. Ends stay put unless 'move ends' is on.", 1);
         d.params = { I("iter", "passes", 4, 0, 200), F("strength", "strength", 0.5f, 0.0f, 1.0f), B("ends", "move ends", false) };
         d.cook = [](CookCtx& c) {
            if (!NeedLines(c))
               return true;
            const PopData& in = *c.Input(0);
            const float k = c.F("strength");
            // Turbo 0.70 (P6): a GPU-made line relaxed on the GPU. Each point's
            // two neighbours and whether it moves are built once per topology;
            // every pass is a kernel with the CPU's formula (`precise`: no fused
            // multiply-add on compute, so the same numbers).
            const PopAttrib* pAttr = in.Find(attr::P);
            if (pAttr != nullptr && !pAttr->buffer->OnCpu() && gpu::Available() && c.I("iter") > 0)
            {
               struct SmoothState : OpState
               {
                  TopologyPtr src; // kept alive: the pointer is the cache key
                  int points = -1;
                  bool ends = false;
                  BufferPtr a0, a1, move;
               };
               SmoothState& s = c.State<SmoothState>();
               const bool ends = c.Bl("ends");
               if (s.src != in.topo || s.points != in.pointCount || s.ends != ends)
               {
                  const size_t n = (size_t)in.pointCount;
                  std::vector<float> a0(n, 0.0f), a1(n, 0.0f), mv(n, 0.0f);
                  for (const Strip& st : Strips(in.Topo()))
                  {
                     const int sn = (int)st.pts.size();
                     for (int j = 0; j < sn; j++)
                     {
                        const bool end = !st.closed && (j == 0 || j == sn - 1);
                        if (end && !ends)
                           continue;
                        const int p = st.pts[(size_t)j];
                        if (p < 0 || (size_t)p >= n)
                           continue;
                        a0[(size_t)p] = (float)st.pts[(size_t)(j > 0 ? j - 1 : (st.closed ? sn - 1 : 0))];
                        a1[(size_t)p] = (float)st.pts[(size_t)(j + 1 < sn ? j + 1 : (st.closed ? 0 : sn - 1))];
                        mv[(size_t)p] = 1.0f;
                     }
                  }
                  s.a0 = MakeBuffer(std::move(a0), 1);
                  s.a1 = MakeBuffer(std::move(a1), 1);
                  s.move = MakeBuffer(std::move(mv), 1);
                  s.src = in.topo;
                  s.points = in.pointCount;
                  s.ends = ends;
               }
               auto io = [](const char* name, int comps) {
                  KernelIO x;
                  x.name = name;
                  x.comps = comps;
                  x.input = 0;
                  return x;
               };
               KernelSpec spec;
               spec.reads = { io(attr::P, 3), io("PzA0", 1), io("PzA1", 1), io("PzMove", 1) };
               spec.lookups = { io(attr::P, 3) };
               spec.writes = { io(attr::P, 3) };
               spec.body = "if (PzMove > 0.5)\n"
                           "{\n"
                           "   vec3 a = fetch_P(int(PzA0));\n"
                           "   vec3 b = fetch_P(int(PzA1));\n"
                           "   precise vec3 mid = 0.5 * (a + b);\n"
                           "   precise vec3 moved = P + (mid - P) * uK;\n"
                           "   P = moved;\n"
                           "}\n";
               KernelUniform uK;
               uK.name = "uK";
               uK.type = KernelUniform::kFloat;
               uK.v[0] = k;
               auto cur = ShallowCopy(in);
               cur->Set("PzA0", s.a0);
               cur->Set("PzA1", s.a1);
               cur->Set("PzMove", s.move);
               bool ok = true;
               for (int it = 0; it < c.I("iter") && ok; it++)
               {
                  KernelEnv env = c.env;
                  env.lookupData[0] = cur.get();
                  std::vector<BufferPtr> out;
                  std::string err;
                  ok = RunKernel(spec, *cur, { uK }, env, out, err);
                  if (ok)
                  {
                     auto next = ShallowCopy(*cur);
                     next->Set(attr::P, out[0]);
                     cur = next;
                  }
               }
               if (ok)
               {
                  c.out->Set(attr::P, cur->Find(attr::P)->buffer);
                  return true;
               }
            }
            std::vector<float> P = in.Read(attr::P, 3);
            const auto strips = Strips(in.Topo());
            for (int it = 0; it < c.I("iter"); it++)
               for (const Strip& s : strips)
               {
                  const int n = (int)s.pts.size();
                  std::vector<float> next(P.begin(), P.end());
                  for (int j = 0; j < n; j++)
                  {
                     const bool end = !s.closed && (j == 0 || j == n - 1);
                     if (end && !c.Bl("ends")) continue;
                     const int a0 = s.pts[(size_t)(j > 0 ? j - 1 : (s.closed ? n - 1 : 0))];
                     const int a1 = s.pts[(size_t)(j + 1 < n ? j + 1 : (s.closed ? 0 : n - 1))];
                     const int p = s.pts[(size_t)j];
                     for (int q = 0; q < 3; q++)
                     {
                        const float mid = 0.5f * (P[(size_t)a0 * 3 + q] + P[(size_t)a1 * 3 + q]);
                        next[(size_t)p * 3 + q] = P[(size_t)p * 3 + q] + (mid - P[(size_t)p * 3 + q]) * k;
                     }
                  }
                  P.swap(next);
               }
            c.out->Set(attr::P, MakeBuffer(std::move(P), 3));
            return true;
         };
         defs.push_back(d);
      }

      // ---- Line Thick ------------------------------------------------------------
      {
         PopDef d = Cpu("Line Thick PointZ", "Gives lines a body: a flat ribbon (facing 'up', or each point's N) or a round tube with "
                                          "'sides'. Width can follow an attribute (e.g. PScale or ArcU).", 1);
         d.params = { M("shape", "shape", { "ribbon", "tube" }, 1), F("width", "width", 0.05f, 0.0f, 5.0f), I("sides", "sides", 8, 3, 64, "shape=1"),
                      V("up", "ribbon up", 0, 1, 0, -1.0f, 1.0f, "shape=0"), B("useN", "ribbon faces N", false, "shape=0"),
                      T("wattr", "width attribute", "") };
         d.cook = [](CookCtx& c) {
            if (!NeedLines(c))
               return true;
            const PopData& in = *c.Input(0);
            Arrays a = Load(in);
            const std::string wa = c.S("wattr");
            const std::vector<float> wv = (!wa.empty() && in.Has(wa)) ? in.Read(wa, 1) : std::vector<float>();
            const std::vector<float> Nin = in.Read(attr::N, 3);
            const int tube = c.I("shape") == 1, sides = std::max(3, c.I("sides"));
            float up[3];
            c.V3("up", up);
            PointBuilder b(a);
            auto o = NewTopo();
            for (const Strip& s : Strips(in.Topo()))
            {
               const int n = (int)s.pts.size();
               if (n < 2) continue;
               const int ring = tube ? sides : 2;
               const int base = b.Count();
               float prevSide[3] = { 0, 0, 0 };
               for (int k = 0; k < n; k++)
               {
                  const int p = s.pts[(size_t)k];
                  const int i0 = s.pts[(size_t)(k > 0 ? k - 1 : (s.closed ? n - 1 : 0))];
                  const int i1 = s.pts[(size_t)(k + 1 < n ? k + 1 : (s.closed ? 0 : n - 1))];
                  float t[3] = { a.Pos(i1)[0] - a.Pos(i0)[0], a.Pos(i1)[1] - a.Pos(i0)[1], a.Pos(i1)[2] - a.Pos(i0)[2] };
                  float tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
                  if (tl < 1e-12f) { t[0] = 1; t[1] = 0; t[2] = 0; tl = 1; }
                  for (float& x : t) x /= tl;
                  float ref[3] = { up[0], up[1], up[2] };
                  if (!tube && c.Bl("useN")) { ref[0] = Nin[(size_t)p * 3]; ref[1] = Nin[(size_t)p * 3 + 1]; ref[2] = Nin[(size_t)p * 3 + 2]; }
                  float side[3] = { t[1] * ref[2] - t[2] * ref[1], t[2] * ref[0] - t[0] * ref[2], t[0] * ref[1] - t[1] * ref[0] };
                  float sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
                  if (sl < 1e-6f)
                  {
                     if (k > 0) { std::copy(prevSide, prevSide + 3, side); sl = 1.0f; }
                     else { const float alt[3] = { 0, 0, 1 }; side[0] = t[1] * alt[2] - t[2] * alt[1]; side[1] = t[2] * alt[0] - t[0] * alt[2]; side[2] = t[0] * alt[1] - t[1] * alt[0]; sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]) + 1e-12f; }
                  }
                  for (float& x : side) x /= sl;
                  std::copy(side, side + 3, prevSide);
                  const float bn[3] = { t[1] * side[2] - t[2] * side[1], t[2] * side[0] - t[0] * side[2], t[0] * side[1] - t[1] * side[0] };
                  const float w = c.F("width") * 0.5f * (wv.empty() ? 1.0f : wv[(size_t)p]);
                  const float* q = a.Pos(p);
                  for (int r = 0; r < ring; r++)
                  {
                     const int np = b.Copy(p);
                     float dir[3];
                     if (tube)
                     {
                        const float ang = 2.0f * kPi * (float)r / sides;
                        for (int j = 0; j < 3; j++) dir[j] = side[j] * std::cos(ang) + bn[j] * std::sin(ang);
                     }
                     else
                        for (int j = 0; j < 3; j++) dir[j] = side[j] * (r == 0 ? -1.0f : 1.0f);
                     b.SetPos(np, q[0] + dir[0] * w, q[1] + dir[1] * w, q[2] + dir[2] * w);
                     const float* nn = tube ? dir : bn;
                     b.Set(np, attr::N, nn, 3);
                     const float tex[3] = { (float)k / (n - 1), tube ? (float)r / sides : (float)r, 0.0f };
                     b.Set(np, attr::Tex, tex, 3);
                  }
               }
               const int segs = s.closed ? n : n - 1;
               const int around = tube ? ring : 1;
               for (int k = 0; k < segs; k++)
                  for (int r = 0; r < around; r++)
                  {
                     const uint32_t a0 = (uint32_t)(base + k * ring + r), a1 = (uint32_t)(base + k * ring + (r + 1) % ring);
                     const uint32_t b0 = (uint32_t)(base + ((k + 1) % n) * ring + r), b1 = (uint32_t)(base + ((k + 1) % n) * ring + (r + 1) % ring);
                     o->tris.insert(o->tris.end(), { a0, b0, b1, a0, b1, a1 });
                  }
            }
            c.out = Store(b.Take(), o);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Accumulate -------------------------------------------------------------
      {
         PopDef d = Cpu("Accumulate PointZ", "Running total of an attribute in point order - over all points, or restarting on every "
                                          "line - written to a new attribute. 'normalize' divides by the final total (a 0..1 ramp).", 1);
         d.params = { T("attrib", "attribute", "Value"), I("comp", "component", 0, 0, 3), M("scope", "scope", { "all points", "per line" }, 0),
                      B("norm", "normalize", false), T("dest", "result", "Accum") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const PopData& in = *c.Input(0);
            const std::string src = c.S("attrib"), dst = c.S("dest");
            if (!ValidAttribName(dst))
            {
               c.error = "'" + dst + "' is not a usable attribute name";
               return false;
            }
            const int comps = in.CompsOf(src);
            const std::vector<float> v = in.Read(src);
            const int k = std::max(0, std::min(comps - 1, c.I("comp")));
            std::vector<float> out((size_t)in.pointCount, 0.0f);
            auto run = [&](const std::vector<int>& order) {
               float sum = 0.0f;
               for (int p : order) { sum += v[(size_t)p * comps + k]; out[(size_t)p] = sum; }
               if (c.Bl("norm") && std::fabs(sum) > 1e-12f)
                  for (int p : order) out[(size_t)p] /= sum;
            };
            if (c.I("scope") == 1 && in.Topo().HasLines())
               for (const Strip& s : Strips(in.Topo())) run(s.pts);
            else
            {
               std::vector<int> all((size_t)in.pointCount);
               std::iota(all.begin(), all.end(), 0);
               run(all);
            }
            c.out->Set(dst, MakeBuffer(std::move(out), 1));
            return true;
         };
         defs.push_back(d);
      }

      // ---- Analyze ----------------------------------------------------------------
      {
         PopDef d = Cpu("Analyze PointZ", "Summarises an attribute over all points into one point: Min, Max, Avg, Sum (per component) "
                                       "and Count, with P at the centroid. Read it with Geometry Table, or Lookup Attribute.", 1);
         d.params = { T("attrib", "attribute", "P") };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const std::string name = c.S("attrib");
            const int comps = in->CompsOf(name);
            const std::vector<float> v = in->Read(name);
            const std::vector<float> P = in->Read(attr::P, 3);
            const int n = in->pointCount;
            std::vector<float> mn((size_t)comps, 1e30f), mx((size_t)comps, -1e30f), sum((size_t)comps, 0.0f);
            double cx = 0, cy = 0, cz = 0;
            for (int i = 0; i < n; i++)
            {
               for (int k = 0; k < comps; k++)
               {
                  const float x = v[(size_t)i * comps + k];
                  mn[(size_t)k] = std::min(mn[(size_t)k], x);
                  mx[(size_t)k] = std::max(mx[(size_t)k], x);
                  sum[(size_t)k] += x;
               }
               cx += P[(size_t)i * 3]; cy += P[(size_t)i * 3 + 1]; cz += P[(size_t)i * 3 + 2];
            }
            std::vector<float> avg((size_t)comps, 0.0f);
            for (int k = 0; k < comps; k++)
            {
               avg[(size_t)k] = n ? sum[(size_t)k] / n : 0.0f;
               if (!n) mn[(size_t)k] = mx[(size_t)k] = 0.0f;
            }
            std::vector<float> c0 = { n ? (float)(cx / n) : 0.0f, n ? (float)(cy / n) : 0.0f, n ? (float)(cz / n) : 0.0f };
            c.out = FromPoints(std::move(c0), true);
            c.out->Set("Min", MakeBuffer(std::move(mn), comps));
            c.out->Set("Max", MakeBuffer(std::move(mx), comps));
            c.out->Set("Avg", MakeBuffer(std::move(avg), comps));
            c.out->Set("Sum", MakeBuffer(std::move(sum), comps));
            c.out->Set("Count", MakeBuffer(std::vector<float>{ (float)n }, 1));
            return true;
         };
         defs.push_back(d);
      }

      // ---- Histogram --------------------------------------------------------------
      {
         PopDef d = Cpu("Histogram PointZ", "Counts an attribute's values into bins and outputs one point per bin along X (height = "
                                         "count, normalised to the fullest bin) as a line, with Count and BinValue attributes.", 1);
         d.params = { T("attrib", "attribute", "P"), I("comp", "component", 1, 0, 3), I("bins", "bins", 32, 2, 4096),
                      B("auto", "range from data", true), F("lo", "low", 0.0f, -100.0f, 100.0f, "auto=0"), F("hi", "high", 1.0f, -100.0f, 100.0f, "auto=0"),
                      F("width", "width", 2.0f, 0.0f, 50.0f), F("height", "height", 1.0f, 0.0f, 50.0f) };
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr)
               return true;
            const std::string name = c.S("attrib");
            const int comps = in->CompsOf(name);
            const std::vector<float> v = in->Read(name);
            const int k = std::max(0, std::min(comps - 1, c.I("comp")));
            const int bins = std::max(2, c.I("bins"));
            float lo = c.F("lo"), hi = c.F("hi");
            if (c.Bl("auto"))
            {
               lo = 1e30f; hi = -1e30f;
               for (int i = 0; i < in->pointCount; i++) { const float x = v[(size_t)i * comps + k]; lo = std::min(lo, x); hi = std::max(hi, x); }
               if (!(lo < hi)) { lo = 0.0f; hi = 1.0f; }
            }
            std::vector<float> cnt((size_t)bins, 0.0f);
            for (int i = 0; i < in->pointCount; i++)
            {
               const float x = (v[(size_t)i * comps + k] - lo) / (hi - lo);
               if (x < 0.0f || x > 1.0f) continue;
               cnt[(size_t)std::min(bins - 1, (int)(x * bins))] += 1.0f;
            }
            const float peak = std::max(1.0f, *std::max_element(cnt.begin(), cnt.end()));
            std::vector<float> P((size_t)bins * 3), val((size_t)bins);
            std::vector<uint32_t> strip((size_t)bins);
            for (int b = 0; b < bins; b++)
            {
               P[(size_t)b * 3] = ((float)b / (bins - 1) - 0.5f) * c.F("width");
               P[(size_t)b * 3 + 1] = cnt[(size_t)b] / peak * c.F("height");
               P[(size_t)b * 3 + 2] = 0.0f;
               val[(size_t)b] = lo + (hi - lo) * ((float)b + 0.5f) / bins;
               strip[(size_t)b] = (uint32_t)b;
            }
            c.out = FromPoints(std::move(P), false);
            c.out->Set("Count", MakeBuffer(std::move(cnt), 1));
            c.out->Set("BinValue", MakeBuffer(std::move(val), 1));
            auto topo = NewTopo();
            topo->AddStrip(strip, false);
            c.out->topo = topo;
            return true;
         };
         defs.push_back(d);
      }
   }
}
