// Sculpt self-test: each brush moves the points it should, the way it
// should, on a UV sphere (whose seam must never open); symmetry mirrors,
// a face selection masks, the saved text round-trips, Amount blends, and a
// changed topology stops the sculpt instead of scrambling it.
//
// INFINITE_SCULPTTEST_MODEL=<glb> also sculpts that model (level 2, a few
// scripted strokes) and writes the node's data text to
// INFINITE_SCULPTTEST_OUT, for before/after renders. With
// INFINITE_SCULPTTEST_STROKES=<file> (one dab per line: brush x y z radius
// strength invert) it runs those dabs on the model's own points instead and
// writes them before and after to OUT, for the Blender sculpt A/B.
#include "app/AppShared.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include "nodes/ModelSourceNode.h"
#include "nodes/SculptNode.h"

namespace app
{
namespace
{
   struct TextIn : ParamVisitor
   {
      std::string data;
      void Float(const char*, float&) override {}
      void Int(const char*, int&) override {}
      void Bool(const char*, bool&) override {}
      void Text(const char* name, std::string& v) override
      {
         if (std::string(name) == "data")
            v = data;
      }
      void Color(const char*, float*) override {}
   };
   struct TextOut : ParamVisitor
   {
      std::string data;
      void Float(const char*, float&) override {}
      void Int(const char*, int&) override {}
      void Bool(const char*, bool&) override {}
      void Text(const char* name, std::string& v) override
      {
         if (std::string(name) == "data")
            data = v;
      }
      void Color(const char*, float*) override {}
   };

   float Len(const float* p) { return std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]); }

   // One dab through the node, as Sculpt mode does it.
   void Dab(SculptNode& n, int brush, const float c[3], float radius, float strength, bool invert, int symmetry)
   {
      std::vector<float> pos, nrm;
      Sculpt::PointFrame(n.Shaped(), n.Topology(), pos, nrm);
      Sculpt::Dab d;
      d.brush = brush;
      std::copy(c, c + 3, d.center);
      d.radius = radius;
      d.strength = strength;
      d.invert = invert;
      d.symmetry = symmetry;
      Sculpt::Stroke(d, pos, nrm, n.Topology(), std::clamp(n.amount, 0.0f, 2.0f), n.Offsets());
      n.OffsetsChanged();
   }

   // The point nearest p, and its position.
   int Nearest(SculptNode& n, const float p[3], float out[3])
   {
      std::vector<float> pos, nrm;
      Sculpt::PointFrame(n.Shaped(), n.Topology(), pos, nrm);
      int best = -1;
      float bd = 1e30f;
      for (int i = 0; i < n.Topology().points; i++)
      {
         const float* q = &pos[(size_t)i * 3];
         const float d = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);
         if (d < bd)
         {
            bd = d;
            best = i;
         }
      }
      if (best >= 0)
         std::copy(&pos[(size_t)best * 3], &pos[(size_t)best * 3] + 3, out);
      return best;
   }

   void PointAt(SculptNode& n, int i, float out[3])
   {
      std::vector<float> pos, nrm;
      Sculpt::PointFrame(n.Shaped(), n.Topology(), pos, nrm);
      std::copy(&pos[(size_t)i * 3], &pos[(size_t)i * 3] + 3, out);
   }

   // Scripted strokes on a real model, for the before/after renders.
   bool SculptModel(const std::string& path, const char* outPath)
   {
      ModelSourceNode model;
      if (!model.Load(path))
         return false;
      SculptNode n;
      n.input = &model;
      n.level = 2;
      n.CookIfNeeded(1);
      std::vector<float> pos, nrm;
      Sculpt::PointFrame(n.Shaped(), n.Topology(), pos, nrm);
      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      for (size_t i = 0; i < pos.size(); i++)
      {
         lo[i % 3] = std::min(lo[i % 3], pos[i]);
         hi[i % 3] = std::max(hi[i % 3], pos[i]);
      }
      const int along = hi[0] - lo[0] > hi[2] - lo[2] ? 0 : 2, across = 2 - along;
      const float size = std::max({ hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] });
      const float mid = (lo[across] + hi[across]) * 0.5f;
      // The top of the body along its length, at t (0..1).
      auto top = [&](float t, float c[3]) {
         const float x = lo[along] + (hi[along] - lo[along]) * t;
         float best = -1e30f;
         for (int i = 0; i < n.Topology().points; i++)
         {
            const float* p = &pos[(size_t)i * 3];
            if (std::fabs(p[along] - x) < size * 0.03f && std::fabs(p[across] - mid) < size * 0.05f && p[1] > best)
            {
               best = p[1];
               std::copy(p, p + 3, c);
            }
         }
         return best > -1e29f;
      };
      float c[3];
      // A ridge down the back, then a dent across it, then fuller flanks.
      for (int s = 0; s <= 24; s++)
         if (top(0.3f + 0.4f * (float)s / 24.0f, c))
            Dab(n, Sculpt::kDraw, c, size * 0.06f, 0.8f, false, 1);
      for (int s = 0; s <= 8; s++)
         if (top(0.5f, c))
         {
            c[across] += size * (-0.08f + 0.16f * (float)s / 8.0f);
            Dab(n, Sculpt::kCrease, c, size * 0.05f, 0.9f, false, 0);
         }
      for (int s = 0; s <= 10; s++)
         if (top(0.4f + 0.2f * (float)s / 10.0f, c))
         {
            c[1] -= size * 0.08f;
            c[across] += size * 0.1f;
            Dab(n, Sculpt::kInflate, c, size * 0.1f, 0.6f, false, 1 << across);
         }
      TextOut out;
      n.VisitParams(out);
      std::ofstream(outPath) << out.data;
      return !out.data.empty();
   }

   // The dabs in `strokes` on the model's own points (level 0); OUT gets "before" then "after", one point a line.
   bool SculptStrokes(const std::string& path, const char* strokes, const char* outPath)
   {
      ModelSourceNode model;
      model.normalizeScale = false;   // the file's own units, as Blender has them
      model.recenter = false;
      if (!model.Load(path))
         return false;
      SculptNode n;
      n.input = &model;
      n.CookIfNeeded(1);
      std::ofstream out(outPath);
      auto points = [&](const char* tag) {
         std::vector<float> pos, nrm;
         Sculpt::PointFrame(n.Shaped(), n.Topology(), pos, nrm);
         out << tag << " " << n.Topology().points << "\n";
         for (size_t i = 0; i < pos.size(); i += 3)
            out << pos[i] << " " << pos[i + 1] << " " << pos[i + 2] << "\n";
      };
      points("before");
      std::ifstream in(strokes);
      std::string line;
      int dabs = 0, stroke = -1;
      while (std::getline(in, line))
      {
         std::istringstream ls(line);
         int brush = 0, invert = 0;
         float c[3], radius = 0.0f, strength = 0.0f;
         if (!(ls >> brush >> c[0] >> c[1] >> c[2] >> radius >> strength >> invert))
            continue;
         // A view direction and stroke number: the dab lands where the 3D View's mouse ray would, on the
         // surface as its stroke began.
         float v[3], o[3], t = 0.0f;
         int s = 0;
         if (ls >> v[0] >> v[1] >> v[2] >> s)
         {
            if (s != stroke)
            {
               n.BeginStroke();
               stroke = s;
            }
            for (int k = 0; k < 3; k++)
               o[k] = c[k] - v[k] * radius * 4.0f;
            if (n.Raycast(o, v, t))
               for (int k = 0; k < 3; k++)
                  c[k] = o[k] + v[k] * t;
         }
         Dab(n, brush, c, radius, strength, invert != 0, 0);
         n.CookIfNeeded(2 + dabs++);
      }
      n.EndStroke();
      points("after");
      return dabs > 0;
   }
}

// ======================================================== INFINITE_SCULPTTEST
bool RunSculptTest()
{
   printf("[SCULPT TEST] Sculpt node and brushes\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };

   GeometryNode sphere;
   sphere.shape = 2;
   sphere.detail = 32;
   SculptNode n;
   n.input = &sphere;
   n.CookIfNeeded(1);
   const Mesh base = sphere.GetMesh();
   const Sculpt::Topology& topo = n.Topology();
   check(topo.points > 0 && topo.points < (int)base.vertices.size(), "the seam welds: fewer points than vertices",
         topo.points, (double)base.vertices.size());
   check(n.Sculptable() && !n.HasSculpt(), "a new node is sculptable and empty", n.Sculptable(), n.HasSculpt());

   // Draw on top: the touched point rises, and every vertex of a point stays together.
   const float up[3] = { 0, 2, 0 };
   float c[3], after[3];
   const int topPt = Nearest(n, up, c);
   const float r0 = Len(c);
   Dab(n, Sculpt::kDraw, c, 0.3f, 0.5f, false, 0);
   PointAt(n, topPt, after);
   check(Len(after) > r0 + 1e-3f, "Draw raises the surface", Len(after), r0);
   float gap = 0.0f;
   {
      const Mesh& m = n.GetMesh();
      std::vector<float> first((size_t)topo.points * 3, 1e30f);
      for (size_t i = 0; i < m.vertices.size(); i++)
      {
         float* f = &first[(size_t)topo.point[i] * 3];
         const float p[3] = { m.vertices[i].px, m.vertices[i].py, m.vertices[i].pz };
         if (f[0] > 1e29f)
            std::copy(p, p + 3, f);
         else
            gap = std::max(gap, std::fabs(f[0] - p[0]) + std::fabs(f[1] - p[1]) + std::fabs(f[2] - p[2]));
      }
   }
   check(gap < 1e-6f, "no crack: split vertices move together", gap, 0);
   Dab(n, Sculpt::kDraw, c, 0.3f, 0.5f, true, 0);
   PointAt(n, topPt, after);
   check(std::fabs(Len(after) - r0) < 0.02f, "Ctrl-Draw takes it back down", Len(after), r0);
   n.Clear();

   // Inflate, Crease (sinks), Pinch (pulls neighbours in), Flatten, Smooth, Grab.
   const float side[3] = { 2, 0, 0 };
   const int sidePt = Nearest(n, side, c);
   const float rs = Len(c);
   Dab(n, Sculpt::kInflate, c, 0.3f, 0.5f, false, 0);
   PointAt(n, sidePt, after);
   check(Len(after) > rs + 1e-3f, "Inflate pushes out along the normal", Len(after), rs);
   n.Clear();
   Dab(n, Sculpt::kCrease, c, 0.3f, 0.5f, false, 0);
   PointAt(n, sidePt, after);
   check(Len(after) < rs - 1e-3f, "Crease cuts in", Len(after), rs);
   n.Clear();
   {
      const float off[3] = { c[0], c[1] + 0.12f, c[2] };
      float q[3];
      const int ring = Nearest(n, off, q);
      const float d0 = std::hypot(q[1] - c[1], q[2] - c[2]);
      Dab(n, Sculpt::kPinch, c, 0.3f, 1.0f, false, 0);
      PointAt(n, ring, q);
      const float d1 = std::hypot(q[1] - c[1], q[2] - c[2]);
      check(d1 < d0 - 1e-4f, "Pinch pulls points toward the brush", d1, d0);
      n.Clear();
      // Flatten: the sphere's bulge under the brush drops toward its plane.
      Dab(n, Sculpt::kFlatten, c, 0.4f, 1.0f, false, 0);
      PointAt(n, sidePt, after);
      check(Len(after) < rs - 1e-4f, "Flatten lowers the bulge", Len(after), rs);
      n.Clear();
   }
   {
      // Roughen, then smooth: the spread of radii under the brush shrinks.
      std::vector<float>& o = n.Offsets();
      for (size_t i = 0; i < o.size(); i++)
         o[i] = 0.03f * std::sin((float)i * 12.9898f);
      n.OffsetsChanged();
      auto spread = [&]() {
         std::vector<float> pos, nrm;
         Sculpt::PointFrame(n.Shaped(), n.Topology(), pos, nrm);
         double s = 0.0, s2 = 0.0;
         int k = 0;
         for (int i = 0; i < topo.points; i++)
         {
            const float* p = &pos[(size_t)i * 3];
            if (std::hypot(p[0] - c[0], std::hypot(p[1] - c[1], p[2] - c[2])) < 0.15f)
            {
               const double l = Len(p);
               s += l;
               s2 += l * l;
               k++;
            }
         }
         return k ? std::sqrt(std::max(0.0, s2 / k - (s / k) * (s / k))) : 0.0;
      };
      const double before = spread();
      for (int i = 0; i < 4; i++)
         Dab(n, Sculpt::kSmooth, c, 0.3f, 1.0f, false, 0);
      check(spread() < before * 0.7, "Smooth evens out a rough patch", spread(), before);
      n.Clear();
   }
   {
      std::vector<float> pos, nrm;
      Sculpt::PointFrame(n.Shaped(), n.Topology(), pos, nrm);
      Sculpt::Dab d;
      std::copy(c, c + 3, d.center);
      d.radius = 0.3f;
      d.strength = 0.5f;
      Sculpt::GrabHold hold;
      Sculpt::GrabBegin(d, pos, n.Topology(), n.Offsets(), hold);
      const float move[3] = { 0, 0.2f, 0 };
      Sculpt::GrabMove(hold, move, 1.0f, n.Offsets());
      n.OffsetsChanged();
      PointAt(n, sidePt, after);
      check(std::fabs(after[1] - c[1] - 0.2f) < 0.03f, "Grab carries the point under the brush", after[1] - c[1], 0.2);
      n.Clear();
   }

   // A dab re-shapes only its neighbourhood; the result matches a full re-shape.
   {
      std::vector<unsigned int> touched;
      for (int i = 0; i < 6; i++)
      {
         const float at[3] = { c[0], c[1] + 0.05f * i, c[2] };
         Sculpt::Dab d;
         d.brush = i % 2 ? Sculpt::kSmooth : Sculpt::kDraw;
         std::copy(at, at + 3, d.center);
         d.radius = 0.2f;
         d.symmetry = 1;
         touched.clear();
         Sculpt::Stroke(d, n.PointPos(), n.PointNrm(), n.Topology(), 1.0f, n.Offsets(), &touched, n.PointGrid(d.radius));
         n.OffsetsChanged(&touched);
      }
      Mesh full;
      Sculpt::Apply(base, n.Topology(), n.Offsets(), 1.0f, full);
      const Mesh& part = n.GetMesh();
      float dp = 0.0f, dn = 0.0f;
      for (size_t i = 0; i < full.vertices.size() && i < part.vertices.size(); i++)
      {
         const Vertex &a = full.vertices[i], &b = part.vertices[i];
         dp = std::max(dp, std::fabs(a.px - b.px) + std::fabs(a.py - b.py) + std::fabs(a.pz - b.pz));
         dn = std::max(dn, std::fabs(a.nx - b.nx) + std::fabs(a.ny - b.ny) + std::fabs(a.nz - b.nz));
      }
      check(dp < 1e-6f && dn < 1e-4f && full.vertices.size() == part.vertices.size(), "a dab's re-shape matches a full one",
            dp, dn);
      std::vector<float> pos, nrm;
      Sculpt::PointFrame(full, n.Topology(), pos, nrm);
      float df = 0.0f;
      for (size_t i = 0; i < pos.size(); i++)
         df = std::max(df, std::fabs(pos[i] - n.PointPos()[i]) + std::fabs(nrm[i] - n.PointNrm()[i]));
      check(df < 1e-4f, "the cached points follow", df, 0);
      // The grid's ray finds the same hit as testing every face.
      const float o[3] = { 0.03f, 2.0f, 0.02f }, dir[3] = { 0, -1, 0 };
      float tg = 0.0f, tb = 0.0f;
      const bool hg = n.Raycast(o, dir, tg), hb = Sculpt::Raycast(n.GetMesh(), o, dir, tb);
      check(hg && hb && std::fabs(tg - tb) < 1e-5f, "the grid's ray hits where a full test does", tg, tb);
      n.Clear();
   }

   // Symmetry X: a dab on +X lands on -X too.
   {
      const float mirror[3] = { -2, 0, 0 };
      float m0[3], m1[3];
      const int mirrorPt = Nearest(n, mirror, m0);
      Dab(n, Sculpt::kDraw, c, 0.3f, 0.5f, false, 1);
      PointAt(n, mirrorPt, m1);
      PointAt(n, sidePt, after);
      check(std::fabs(Len(m1) - Len(after)) < 1e-3f && Len(m1) > Len(m0) + 1e-3f, "X symmetry mirrors the dab", Len(m1),
            Len(after));
   }

   // Saved text: a fresh node loading it shows the same mesh.
   {
      TextOut out;
      n.VisitParams(out);
      SculptNode loaded;
      loaded.input = &sphere;
      TextIn in;
      in.data = out.data;
      loaded.VisitParams(in);
      loaded.CookIfNeeded(1);
      const Mesh &a = n.GetMesh(), &b = loaded.GetMesh();
      float err = a.vertices.size() == b.vertices.size() ? 0.0f : 1.0f;
      for (size_t i = 0; i < a.vertices.size() && i < b.vertices.size(); i++)
         err = std::max(err, std::fabs(a.vertices[i].px - b.vertices[i].px) + std::fabs(a.vertices[i].py - b.vertices[i].py) +
                                std::fabs(a.vertices[i].pz - b.vertices[i].pz));
      check(!out.data.empty() && err < 1e-4f, "saved text loads back to the same shape", err, (double)out.data.size());
      // Undo: an older text replaces newer strokes.
      TextOut before;
      loaded.VisitParams(before);
      Dab(loaded, Sculpt::kInflate, c, 0.3f, 0.5f, false, 0);
      TextIn undo;
      undo.data = before.data;
      loaded.VisitParams(undo);
      loaded.CookIfNeeded(2);
      PointAt(loaded, sidePt, after);
      float ref[3];
      PointAt(n, sidePt, ref);
      check(std::fabs(Len(after) - Len(ref)) < 1e-4f, "loading older text undoes later strokes", Len(after), Len(ref));
   }

   // Amount: 0 is the input, 2 is twice the sculpt.
   {
      float a1[3], a2[3], a0[3];
      PointAt(n, sidePt, a1);
      n.amount = 2.0f;
      n.CookIfNeeded(3);
      PointAt(n, sidePt, a2);
      n.amount = 0.0f;
      n.CookIfNeeded(4);
      PointAt(n, sidePt, a0);
      check(std::fabs((Len(a2) - Len(a0)) - 2.0f * (Len(a1) - Len(a0))) < 2e-3f, "amount 2 doubles the sculpt", Len(a2) - Len(a0),
            Len(a1) - Len(a0));
      check(std::fabs(Len(a0) - rs) < 1e-5f, "amount 0 is the input", Len(a0), rs);
      n.amount = 1.0f;
   }

   // A face selection masks: nothing outside it moves.
   {
      Sculpt::Topology masked;
      Mesh half = base;
      half.faceMask.assign(half.FaceCount(), 0);
      for (size_t f = 0; f < half.FaceCount(); f++)
         half.faceMask[f] = half.vertices[half.indices[f * 3]].py > 0.0f ? 1 : 0;
      Sculpt::Build(half, masked);
      std::vector<float> pos, nrm, off((size_t)masked.points * 3, 0.0f);
      Sculpt::PointFrame(half, masked, pos, nrm);
      Sculpt::Dab d;
      d.center[1] = -0.5f;
      d.radius = 0.4f;
      Sculpt::Stroke(d, pos, nrm, masked, 1.0f, off);
      bool movedBelow = false;
      for (int i = 0; i < masked.points; i++)
         if (masked.mask[i] == 0 && (off[(size_t)i * 3] != 0.0f || off[(size_t)i * 3 + 1] != 0.0f || off[(size_t)i * 3 + 2] != 0.0f))
            movedBelow = true;
      check(!movedBelow, "unselected faces hold still", movedBelow, 0);
   }

   // Topology change: level 1 has other points; the sculpt waits, then clears.
   n.level = 1;
   n.CookIfNeeded(5);
   check(!n.CookWarning().empty() && !n.Sculptable(), "a new topology warns and stops the sculpt", n.CookWarning().size(), 0);
   n.level = 0;
   n.CookIfNeeded(6);
   check(n.CookWarning().empty() && n.HasSculpt(), "the old topology brings it back", n.CookWarning().size(), n.HasSculpt());

   if (const char* model = getenv("INFINITE_SCULPTTEST_MODEL"))
   {
      const char* out = getenv("INFINITE_SCULPTTEST_OUT");
      if (const char* strokes = getenv("INFINITE_SCULPTTEST_STROKES"))
         check(out != nullptr && SculptStrokes(model, strokes, out), "the stroke file's dabs on the model", 1, 1);
      else
         check(out != nullptr && SculptModel(model, out), "scripted strokes on the model", 1, 1);
   }

   printf("SCULPT TEST: %s (%d failure%s)\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
   return failures == 0;
}
}
