// PointZ operators, tier A (second half): mapping, fields, forces, lookups,
// selection, blending, user GLSL and the image conversions.

#include "PopOpsInternal.h"

#include <cstdlib>

namespace pop
{
   using namespace popa;

   namespace
   {
      // Extracts a vec3 uniform pair for an input's bounds (unit box if none).
      void BoundsUniforms(const PopData& d, std::vector<KernelUniform>& u)
      {
         float lo[3] = { -0.5f, -0.5f, -0.5f }, hi[3] = { 0.5f, 0.5f, 0.5f };
         float l[3], h[3];
         if (d.Bounds(l, h))
            for (int k = 0; k < 3; k++)
            {
               lo[k] = l[k];
               hi[k] = h[k] > l[k] ? h[k] : l[k] + 1e-6f;
            }
         u.push_back(Vec("uLo", lo[0], lo[1], lo[2]));
         u.push_back(Vec("uHi", hi[0], hi[1], hi[2]));
      }

      // Every attribute of `d` as kernel reads (at most `limit`, P first).
      std::vector<KernelIO> AllReads(const PopData& d, size_t limit)
      {
         std::vector<KernelIO> r;
         if (d.Has(attr::P))
            r.push_back({ attr::P, d.CompsOf(attr::P) });
         for (const PopAttrib& a : d.attribs)
         {
            if (r.size() >= limit)
               break;
            if (a.name == attr::P || !ValidAttribName(a.name))
               continue;
            r.push_back({ a.name, a.buffer->Comps() });
         }
         return r;
      }

      // "name" or "name:comps" list -> kernel writes (comps from the input when known).
      bool ParseWrites(CookCtx& c, const std::string& list, std::vector<KernelIO>& w)
      {
         for (const std::string& item : SplitList(list))
         {
            std::string name = item;
            int comps = 0;
            const size_t colon = item.find(':');
            if (colon != std::string::npos)
            {
               name = item.substr(0, colon);
               comps = std::atoi(item.c_str() + colon + 1);
            }
            if (!ValidAttribName(name))
            {
               c.error = "'" + name + "' is not a usable attribute name";
               return false;
            }
            const PopData* in = c.Input(0);
            if (comps <= 0)
               comps = in ? in->CompsOf(name) : DefaultComps(name);
            w.push_back({ name, std::max(1, std::min(4, comps)) });
         }
         if (w.empty())
         {
            c.error = "no output attribute named";
            return false;
         }
         if (w.size() > 4)
         {
            c.error = "at most four output attributes per GLSL PointZ";
            return false;
         }
         return true;
      }

      std::shared_ptr<PopData> GridFromImage(int cols, int rows, float width, float height, int kind)
      {
         cols = std::max(2, cols);
         rows = std::max(2, rows);
         auto out = std::make_shared<PopData>();
         const int n = cols * rows;
         out->pointCount = n;
         std::vector<float> P((size_t)n * 3), N((size_t)n * 3), T((size_t)n * 3);
         for (int j = 0; j < rows; j++)
            for (int i = 0; i < cols; i++)
            {
               const size_t k = (size_t)(j * cols + i);
               const float u = (float)i / (cols - 1), v = (float)j / (rows - 1);
               P[k * 3] = (u - 0.5f) * width; P[k * 3 + 1] = (v - 0.5f) * height; P[k * 3 + 2] = 0.0f;
               N[k * 3] = 0.0f; N[k * 3 + 1] = 0.0f; N[k * 3 + 2] = 1.0f;
               T[k * 3] = u; T[k * 3 + 1] = v; T[k * 3 + 2] = 0.0f;
            }
         out->Set(attr::P, MakeBuffer(std::move(P), 3));
         out->Set(attr::N, MakeBuffer(std::move(N), 3));
         out->Set(attr::Tex, MakeBuffer(std::move(T), 3));
         auto topo = NewTopo();
         if (kind == 0)
         {
            for (int j = 0; j + 1 < rows; j++)
               for (int i = 0; i + 1 < cols; i++)
               {
                  const uint32_t a = (uint32_t)(j * cols + i);
                  topo->tris.insert(topo->tris.end(), { a, a + 1, a + (uint32_t)cols + 1, a, a + (uint32_t)cols + 1, a + (uint32_t)cols });
               }
         }
         else if (kind == 1)
         {
            for (int j = 0; j < rows; j++)
            {
               std::vector<uint32_t> s;
               for (int i = 0; i < cols; i++)
                  s.push_back((uint32_t)(j * cols + i));
               topo->AddStrip(s, false);
            }
         }
         else
            topo->pointPrims = true;
         out->topo = topo;
         out->dims[0] = rows;
         out->dims[1] = cols;
         return out;
      }
   }

   void RegisterKernelsA2(std::vector<PopDef>& defs)
   {
      // ---- Texture Map -------------------------------------------------------
      {
         PopDef d = Simple("Texture Map PointZ", "Texture Map PointZ",
                           "Writes texture coordinates (Tex) by projecting the points: planar on XY / XZ / YZ (fitted to the "
                           "input's bounds), spherical, cylindrical or from the normal. 'scale' and 'offset' tile and slide them.", 1);
         d.params = { M("proj", "projection", { "planar XY", "planar XZ", "planar YZ", "spherical", "cylindrical", "from normal" }, 0),
                      V("scale", "scale", 1, 1, 1, -10.0f, 10.0f), V("offset", "offset", 0, 0, 0, -10.0f, 10.0f),
                      T("dest", "attribute", "Tex"), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target t;
            if (!ResolveTarget(c, "dest", t, 3))
               return false;
            KernelSpec k;
            k.reads = { { attr::P, 3 }, { attr::N, 3 } };
            k.writes = { { t.name, 3 } };
            k.body = R"GLSL(
   vec3 u01 = (P - uLo) / max(uHi - uLo, vec3(1e-9));
   vec3 ctr = (uLo + uHi) * 0.5;
   vec3 d = P - ctr;
   vec3 uv;
   if (p_proj == 0) uv = vec3(u01.xy, 0.0);
   else if (p_proj == 1) uv = vec3(u01.x, 1.0 - u01.z, 0.0);
   else if (p_proj == 2) uv = vec3(1.0 - u01.z, u01.y, 0.0);
   else if (p_proj == 3) { vec3 n = normalize(d + vec3(1e-9)); uv = vec3(atan(n.x, n.z) / TAU + 0.5, asin(clamp(n.y, -1.0, 1.0)) / PI + 0.5, 0.0); }
   else if (p_proj == 4) uv = vec3(atan(d.x, d.z) / TAU + 0.5, u01.y, 0.0);
   else uv = vec3(N.xy * 0.5 + 0.5, 0.0);
   uv = uv * p_scale + p_offset;
)GLSL" + std::string("   ") + t.name + " = uv;\n";
            std::vector<KernelUniform> u;
            BoundsUniforms(*c.Input(0), u);
            return Run(c, k, u);
         };
         defs.push_back(d);
      }

      // ---- Field -----------------------------------------------------------
      {
         PopDef d = Simple("Field PointZ", "Field PointZ",
                           "A region of space that weights points: 1 inside the shape, 0 outside, with a soft 'transition' "
                           "between. Writes Weight (and signed distance Dist), combined with what is already there. Use the "
                           "weight as another PointZ's mask to limit its effect to the region.", 1);
         d.params = { M("shape", "shape", { "sphere", "box", "torus", "tube", "capsule", "plane" }, 0),
                      V("center", "center", 0, 0, 0, -20.0f, 20.0f), V("size", "size / radius", 0.5f, 0.5f, 0.5f, 0.0f, 20.0f),
                      F("minor", "minor radius", 0.1f, 0.0f, 5.0f, "shape=2|4"), M("axis", "axis", Axes(), 1, "shape=2|3|4|5"),
                      F("range", "transition", 0.2f, 0.0f, 10.0f), M("curve", "transition curve", { "linear", "smooth", "ease in-out" }, 1),
                      B("invert", "invert", false), M("combine", "combine", { "replace", "add", "multiply", "max", "min" }, 0),
                      T("weight", "weight attribute", "Weight"), B("dist", "write Dist", false) };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target w;
            if (!ResolveTarget(c, "weight", w, 1))
               return false;
            KernelSpec k;
            k.reads = { { attr::P, 3 } };
            k.writes = { { w.name, 1 } };
            const bool dist = c.Bl("dist");
            if (dist)
               k.writes.push_back({ attr::Dist, 1 });
            k.body = R"GLSL(
   vec3 ax = p_axis == 0 ? vec3(1, 0, 0) : (p_axis == 1 ? vec3(0, 1, 0) : vec3(0, 0, 1));
   vec3 q = P - p_center;
   float sd;
   if (p_shape == 0) sd = length(q / max(p_size, vec3(1e-6))) * min(p_size.x, min(p_size.y, p_size.z)) - min(p_size.x, min(p_size.y, p_size.z));
   else if (p_shape == 1) { vec3 e = abs(q) - p_size; sd = length(max(e, vec3(0.0))) + min(max(e.x, max(e.y, e.z)), 0.0); }
   else if (p_shape == 2) { float h = dot(q, ax); vec3 r = q - ax * h; sd = length(vec2(length(r) - p_size.x, h)) - p_minor; }
   else if (p_shape == 3) { float h = dot(q, ax); sd = length(q - ax * h) - p_size.x; }
   else if (p_shape == 4) { float h = clamp(dot(q, ax), -p_size.y, p_size.y); sd = length(q - ax * h) - p_minor; }
   else sd = dot(q, ax);
   float t = p_range > 1e-6 ? clamp(0.5 - sd / p_range, 0.0, 1.0) : (sd <= 0.0 ? 1.0 : 0.0);
   if (p_curve == 1) t = t * t * (3.0 - 2.0 * t);
   else if (p_curve == 2) t = t < 0.5 ? 2.0 * t * t : 1.0 - pow(-2.0 * t + 2.0, 2.0) * 0.5;
   if (p_invert != 0) t = 1.0 - t;
)GLSL";
            k.body += "   float old = " + w.name + ";\n";
            k.body += "   " + w.name + " = p_combine == 0 ? t : (p_combine == 1 ? min(1.0, old + t) : (p_combine == 2 ? old * t : (p_combine == 3 ? max(old, t) : min(old, t))));\n";
            if (dist)
               k.body += "   Dist = sd;\n";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Force Radial ----------------------------------------------------
      {
         PopDef d = Simple("Force Radial PointZ", "Force Radial PointZ",
                           "Adds a force to PartForce pulling points toward a centre (or pushing them away), fading out over "
                           "'radius', with an optional swirl around an axis. Put it inside a Particle PointZ loop.", 1);
         d.params = { V("center", "center", 0, 0, 0, -20.0f, 20.0f), F("strength", "strength", 1.0f, -50.0f, 50.0f),
                      F("radius", "radius", 2.0f, 0.0f, 50.0f), M("falloff", "falloff", { "none", "linear", "inverse square" }, 1),
                      F("swirl", "swirl", 0.0f, -50.0f, 50.0f), V("axis", "swirl axis", 0, 1, 0, -1.0f, 1.0f), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            KernelSpec k;
            k.reads = { { attr::P, 3 } };
            k.writes = { { attr::PartForce, 3 } };
            k.body = R"GLSL(
   vec3 d = p_center - P;
   float l = length(d);
   vec3 dir = l > 1e-6 ? d / l : vec3(0.0);
   float f = 1.0;
   if (p_falloff == 1) f = p_radius > 0.0 ? clamp(1.0 - l / p_radius, 0.0, 1.0) : 0.0;
   else if (p_falloff == 2) f = 1.0 / (1.0 + l * l);
   vec3 ax = length(p_axis) > 1e-6 ? normalize(p_axis) : vec3(0, 1, 0);
   PartForce += (dir * p_strength + cross(ax, -dir) * p_swirl) * f;
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Lookup Attribute --------------------------------------------------
      {
         PopDef d = Simple("Lookup Attribute PointZ", "Lookup Attribute PointZ",
                           "Copies attributes from the second input onto the first, point by point: point i reads the "
                           "point given by its 'index attribute' (or the same number when empty), clamped to the range.", 2);
         d.inputLabels = { "points", "source" };
         d.params = { T("attribs", "attributes", "Color"), T("index", "index attribute", ""), I("offset", "index offset", 0, -100000, 100000) };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const PopData* src = c.Input(1);
            if (src == nullptr)
            {
               c.warning = "nothing on the source input";
               return true;
            }
            KernelSpec k;
            k.lookupInput = 1;
            const std::string idx = c.S("index");
            if (!idx.empty())
            {
               if (!ValidAttribName(idx))
               {
                  c.error = "'" + idx + "' is not a usable attribute name";
                  return false;
               }
               k.reads.push_back({ idx, 1 });
            }
            k.body = "   int j = " + (idx.empty() ? std::string("id") : "int(floor(" + idx + " + 0.5))") + " + p_offset;\n";
            for (const std::string& a : SplitList(c.S("attribs")))
            {
               if (!ValidAttribName(a))
                  continue;
               if (k.writes.size() >= 4)
               {
                  c.warning = "only four attributes per lookup";
                  break;
               }
               const int comps = src->CompsOf(a);
               k.writes.push_back({ a, comps });
               k.lookups.push_back({ a, comps });
               k.body += "   " + a + " = fetch_" + a + "(j);\n";
            }
            if (k.writes.empty())
            {
               c.error = "no attribute to look up";
               return false;
            }
            return c.Kernel(k, *c.Input(0));
         };
         defs.push_back(d);
      }

      // ---- Lookup Channel (curve) ---------------------------------------------
      {
         PopDef d = Simple("Lookup Channel PointZ", "Lookup Channel PointZ",
                           "Runs an attribute through a curve: its value (0..1 after the input range) picks a point on a "
                           "five-key curve, smoothly interpolated, and the result is written out. A shaping table you can "
                           "modulate key by key.", 1);
         d.params = { T("attrib", "input attribute", "Tex"), I("comp", "component", 0, 0, 3), F("inLo", "from low", 0.0f, -10.0f, 10.0f),
                      F("inHi", "from high", 1.0f, -10.0f, 10.0f), F("k0", "key 0", 0.0f, -2.0f, 2.0f), F("k1", "key 1", 0.25f, -2.0f, 2.0f),
                      F("k2", "key 2", 0.5f, -2.0f, 2.0f), F("k3", "key 3", 0.75f, -2.0f, 2.0f), F("k4", "key 4", 1.0f, -2.0f, 2.0f),
                      M("interp", "interpolation", { "linear", "smooth" }, 1), T("dest", "output attribute", "Value") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target src, dst;
            if (!ResolveTarget(c, "attrib", src) || !ResolveTarget(c, "dest", dst, 1))
               return false;
            KernelSpec k;
            k.reads = { { src.name, src.comps } };
            k.writes = { { dst.name, 1 } };
            const int comp = std::max(0, std::min(src.comps - 1, c.I("comp")));
            const std::string pick = src.comps == 1 ? src.name : src.name + "[" + std::to_string(comp) + "]";
            k.body = "   float x = (" + pick + " - p_inLo) / ((p_inHi - p_inLo) == 0.0 ? 1e-9 : (p_inHi - p_inLo));\n" + R"GLSL(
   x = clamp(x, 0.0, 1.0) * 4.0;
   float keys[5] = float[5](p_k0, p_k1, p_k2, p_k3, p_k4);
   int i0 = int(min(floor(x), 3.0));
   float f = x - float(i0);
   if (p_interp == 1) f = f * f * (3.0 - 2.0 * f);
   float v = mix(keys[i0], keys[i0 + 1], f);
)GLSL" + "   " + dst.name + " = v;\n";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Lookup Texture ----------------------------------------------------
      {
         PopDef d = Simple("Lookup Texture PointZ", "Lookup Texture PointZ",
                           "Samples an image at each point's texture coordinate (or its position fitted to the bounds) and "
                           "writes the colour - replacing, multiplying or adding - or pushes the points along their normals "
                           "by the image's brightness.", 1);
         d.imageInput = true;
         d.params = { M("uvFrom", "coordinates", { "Tex", "position XY (bounds)", "position XZ (bounds)" }, 0),
                      M("mode", "result", { "colour replace", "colour multiply", "colour add", "displace along N", "write attribute" }, 0),
                      F("amount", "amount", 1.0f, -5.0f, 5.0f), T("dest", "attribute", "Lum", "mode=4"), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            if (c.image == 0)
            {
               c.warning = "no image on the image input";
               return true;
            }
            KernelSpec k;
            k.usesImage = true;
            k.reads = { { attr::P, 3 }, { attr::N, 3 }, { attr::Tex, 3 }, { attr::Color, 4 } };
            const int mode = c.I("mode");
            k.body = R"GLSL(
   vec2 uv = Tex.xy;
   vec3 u01 = (P - uLo) / max(uHi - uLo, vec3(1e-9));
   if (p_uvFrom == 1) uv = u01.xy; else if (p_uvFrom == 2) uv = vec2(u01.x, 1.0 - u01.z);
   vec4 s = sampleImage(uv);
   float lum = dot(s.rgb, vec3(0.2126, 0.7152, 0.0722));
)GLSL";
            if (mode <= 2)
            {
               k.writes = { { attr::Color, 4 } };
               k.body += mode == 0 ? "   Color = mix(Color, s, clamp(p_amount, 0.0, 1.0));\n"
                         : mode == 1 ? "   Color *= mix(vec4(1.0), s, clamp(p_amount, 0.0, 1.0));\n"
                                     : "   Color += s * p_amount;\n";
            }
            else if (mode == 3)
            {
               k.writes = { { attr::P, 3 } };
               k.body += "   P += normalize(N + vec3(1e-9)) * lum * p_amount;\n";
            }
            else
            {
               Target t;
               if (!ResolveTarget(c, "dest", t, 1))
                  return false;
               k.writes = { { t.name, 1 } };
               k.body += "   " + t.name + " = lum * p_amount;\n";
            }
            std::vector<KernelUniform> u;
            BoundsUniforms(*c.Input(0), u);
            return Run(c, k, u);
         };
         defs.push_back(d);
      }

      // ---- Group -------------------------------------------------------------
      {
         PopDef d = Simple("Group PointZ", "Group PointZ",
                           "Marks a set of points in a float attribute (1 in, 0 out): inside a box or sphere, an index range, "
                           "a random fraction, facing a direction, or a GLSL condition. Combine with what is there to build up "
                           "a selection; other PointZ nodes take it as their mask.", 1);
         d.params = { T("name", "group attribute", "Group"),
                      M("by", "select by", { "box", "sphere", "index range", "random fraction", "normal direction", "GLSL condition" }, 0),
                      V("center", "center", 0, 0, 0, -20.0f, 20.0f, "by=0|1"), V("size", "size", 1, 1, 1, 0.0f, 20.0f, "by=0"),
                      F("radius", "radius", 0.5f, 0.0f, 20.0f, "by=1"), I("start", "first", 0, 0, 10000000, "by=2"),
                      I("end", "last", 99, 0, 10000000, "by=2"), I("step", "every", 1, 1, 10000, "by=2"),
                      F("fraction", "fraction", 0.5f, 0.0f, 1.0f, "by=3"), F("seed", "seed", 1.0f, 0.0f, 1000.0f, "by=3"),
                      V("dir", "direction", 0, 1, 0, -1.0f, 1.0f, "by=4"), F("angle", "spread (deg)", 45.0f, 0.0f, 180.0f, "by=4"),
                      T("expr", "condition", "P.y > 0.0", "by=5"), M("combine", "combine", { "replace", "union", "intersect", "subtract" }, 0),
                      B("invert", "invert", false) };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target g;
            if (!ResolveTarget(c, "name", g, 1))
               return false;
            KernelSpec k;
            k.reads = AllReads(*c.Input(0), 11);
            AddRead(k, attr::Color, 4);
            AddRead(k, attr::Tex, 3);
            k.writes = { { g.name, 1 } };
            std::string cond = c.S("expr");
            if (cond.find(';') != std::string::npos || cond.find('{') != std::string::npos)
               cond = "false";
            k.body = R"GLSL(
   bool inside = false;
   if (p_by == 0) inside = all(lessThanEqual(abs(P - p_center), p_size * 0.5));
   else if (p_by == 1) inside = length(P - p_center) <= p_radius;
   else if (p_by == 2) inside = id >= p_start && id <= p_end && ((id - p_start) % max(1, p_step)) == 0;
   else if (p_by == 3) inside = hashi(id, p_seed) < p_fraction;
   else if (p_by == 4) inside = dot(normalize(N + vec3(1e-9)), normalize(p_dir + vec3(1e-9))) >= cos(radians(p_angle));
)GLSL";
            if (c.I("by") == 5)
               k.body += "   else inside = bool(" + (cond.empty() ? std::string("false") : cond) + ");\n";
            k.body += "   float v = (p_invert != 0) != inside ? 1.0 : 0.0;\n   float old = " + g.name + ";\n";
            k.body += "   " + g.name + " = p_combine == 0 ? v : (p_combine == 1 ? max(old, v) : (p_combine == 2 ? min(old, v) : old * (1.0 - v)));\n";
            AddRead(k, attr::N, 3);
            AddRead(k, g.name, 1);
            if (!c.Input(0)->Has(g.name))
               for (KernelIO& r : k.reads)
                  if (r.name == g.name)
                     r.comps = 1;
            return c.Kernel(k, *c.Input(0));
         };
         defs.push_back(d);
      }

      // ---- Blend ---------------------------------------------------------------
      {
         PopDef d = Simple("Blend PointZ", "Blend PointZ",
                           "Blends attributes of the first input toward the second (same point count): P, N and Color by "
                           "default. 'weight' 0 is the first input, 1 the second; a weight attribute blends per point.", 2);
         d.inputLabels = { "from", "to" };
         d.params = { F("weight", "weight", 0.5f, 0.0f, 1.0f), T("attribs", "attributes", "P,N,Color"), T("wattr", "weight attribute", ""), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const PopData* b = c.Input(1);
            if (b == nullptr)
               return true;
            if (b->pointCount != c.Input(0)->pointCount)
               c.warning = "the inputs have different point counts; extra points read the last one";
            KernelSpec k;
            k.lookupInput = 1;
            std::string wexpr = "p_weight";
            const std::string wa = c.S("wattr");
            if (!wa.empty() && ValidAttribName(wa))
            {
               k.reads.push_back({ wa, 1 });
               wexpr = "p_weight * " + wa;
            }
            k.body = "   float w = clamp(" + wexpr + ", 0.0, 1.0);\n";
            for (const std::string& a : SplitList(c.S("attribs")))
            {
               if (!ValidAttribName(a) || (!b->Has(a) && !c.Input(0)->Has(a)))
                  continue;
               if (k.writes.size() >= 4)
                  break;
               const int comps = c.Input(0)->Has(a) ? c.Input(0)->CompsOf(a) : b->CompsOf(a);
               k.writes.push_back({ a, comps });
               k.lookups.push_back({ a, comps });
               k.body += "   " + a + " = mix(" + a + ", fetch_" + a + "(id), w);\n";
               if (a == attr::N)
                  k.body += "   N = normalize(N + vec3(1e-9));\n";
            }
            if (k.writes.empty())
               return true;
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Dimension -----------------------------------------------------------
      {
         PopDef d = Simple("Dimension PointZ", "Dimension PointZ",
                           "Gives the points a grid shape (rows x columns; 'from input' keeps a generator's own) and writes "
                           "each point's normalised column and row (0..1) into an attribute, for grid-aware effects.", 1);
         d.params = { B("auto", "from input", true), I("rows", "rows", 16, 1, 100000, "auto=0"), I("cols", "columns", 16, 1, 100000, "auto=0"),
                      T("dest", "attribute", "Tex") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            int rows = c.out->dims[0], cols = c.out->dims[1];
            if (!c.Bl("auto") || rows <= 0 || cols <= 0)
            {
               rows = std::max(1, c.I("rows"));
               cols = std::max(1, c.I("cols"));
            }
            c.out->dims[0] = rows;
            c.out->dims[1] = cols;
            Target t;
            if (!ResolveTarget(c, "dest", t, 3))
               return false;
            KernelSpec k;
            k.writes = { { t.name, 3 } };
            k.body = "   int col = id % uCols, row = id / uCols;\n   " + t.name +
                     " = vec3(float(col) / max(1.0, float(uCols - 1)), float(row) / max(1.0, float(uRows - 1)), 0.0);\n";
            return c.Kernel(k, *c.Input(0), { Int("uCols", cols), Int("uRows", rows) });
         };
         defs.push_back(d);
      }

      // ---- Phaser ----------------------------------------------------------------
      {
         PopDef d = Simple("Phaser PointZ", "Phaser PointZ",
                           "Staggers an animation across points: as 'phase' runs 0 to 1, each point's own 0..1 (from an "
                           "attribute, or its index) decides when its 'Phase' ramps from 0 to 1, over 'edge'. Drive a Blend, "
                           "a mask or a scale with it.", 1);
         d.params = { F("phase", "phase", 0.5f, 0.0f, 1.0f), F("edge", "edge", 0.2f, 0.001f, 1.0f), F("speed", "speed", 0.0f, -5.0f, 5.0f),
                      T("from", "offset attribute (empty = index)", ""), I("comp", "component", 0, 0, 3),
                      M("curve", "curve", { "linear", "smooth" }, 1), T("dest", "output attribute", "Phase") };
         d.animKey = "speed";
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target dst;
            if (!ResolveTarget(c, "dest", dst, 1))
               return false;
            KernelSpec k;
            k.writes = { { dst.name, 1 } };
            std::string off = "(float(id) / max(1.0, float(uCount - 1)))";
            const std::string from = c.S("from");
            if (!from.empty() && ValidAttribName(from))
            {
               const int comps = c.Input(0)->CompsOf(from);
               k.reads.push_back({ from, comps });
               off = comps == 1 ? from : from + "[" + std::to_string(std::max(0, std::min(comps - 1, c.I("comp")))) + "]";
            }
            k.body = "   float o = clamp(" + off + ", 0.0, 1.0);\n" + R"GLSL(
   float ph = fract(p_phase + uTime * p_speed);
   if (p_speed == 0.0) ph = p_phase;
   float t = clamp((ph * (1.0 + p_edge) - o) / p_edge, 0.0, 1.0);
   if (p_curve == 1) t = t * t * (3.0 - 2.0 * t);
)GLSL" + "   " + dst.name + " = t;\n";
            return c.Kernel(k, *c.Input(0));
         };
         defs.push_back(d);
      }

      // ---- Point (expressions) -----------------------------------------------------
      {
         PopDef d = Simple("Point PointZ", "Point PointZ",
                           "Sets point attributes from GLSL expressions: position, normal, colour and one custom attribute. "
                           "Empty keeps the value. In scope: P, N, Color, Tex, id, uCount, uTime, uBeat, noise and hash helpers.", 1);
         d.params = { T("eP", "position", ""), T("eN", "normal", ""), T("eC", "colour (vec4)", ""), T("custom", "custom attribute", ""),
                      T("eX", "custom value", "", ""), I("comps", "custom components", 1, 1, 4), B("animate", "animate (uses uTime)", false), Mask() };
         d.animKey = "animate";
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            KernelSpec k;
            k.reads = AllReads(*c.Input(0), 11);
            AddRead(k, attr::P, 3);
            AddRead(k, attr::N, 3);
            AddRead(k, attr::Color, 4);
            AddRead(k, attr::Tex, 3);
            auto safe = [](const std::string& e) { return e.find(';') == std::string::npos && e.find('{') == std::string::npos; };
            if (!c.S("eP").empty() && safe(c.S("eP"))) { k.writes.push_back({ attr::P, 3 }); k.body += "   vec3 nP = vec3(" + c.S("eP") + ");\n"; }
            if (!c.S("eN").empty() && safe(c.S("eN"))) { k.writes.push_back({ attr::N, 3 }); k.body += "   vec3 nN = vec3(" + c.S("eN") + ");\n"; }
            if (!c.S("eC").empty() && safe(c.S("eC"))) { k.writes.push_back({ attr::Color, 4 }); k.body += "   vec4 nC = vec4(" + c.S("eC") + ");\n"; }
            const std::string cname = c.S("custom");
            if (!cname.empty() && !c.S("eX").empty() && safe(c.S("eX")))
            {
               if (!ValidAttribName(cname))
               {
                  c.error = "'" + cname + "' is not a usable attribute name";
                  return false;
               }
               const int comps = std::max(1, std::min(4, c.I("comps")));
               k.writes.push_back({ cname, comps });
               AddRead(k, cname, comps);
               k.body += std::string("   ") + GlslType(comps) + " nX = " + GlslType(comps) + "(" + c.S("eX") + ");\n";
            }
            // Every expression sees the inputs, not each other's results.
            for (const KernelIO& w : k.writes)
               k.body += "   " + w.name + " = " + (w.name == attr::P ? "nP" : w.name == attr::N ? "nN" : w.name == attr::Color ? "nC" : "nX") + ";\n";
            if (k.writes.empty())
               return true;
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Attribute -----------------------------------------------------------------
      {
         PopDef d = Simple("Attribute PointZ", "Attribute PointZ",
                           "Creates or overwrites an attribute: a constant, a copy of another attribute, the point index "
                           "(raw or 0..1), or a GLSL expression.", 1);
         d.params = { T("name", "attribute", "Value"), I("comps", "components", 1, 1, 4),
                      M("from", "value", { "constant", "copy attribute", "index", "index 0..1", "expression" }, 0),
                      V("value", "value", 0, 0, 0, -100.0f, 100.0f, "from=0"), F("value4", "value (4th)", 1.0f, -100.0f, 100.0f, "from=0"),
                      T("src", "source attribute", "P", "from=1"), T("expr", "expression", "P.y", "from=4") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target t;
            if (!ResolveTarget(c, "name", t, std::max(1, std::min(4, c.I("comps")))))
               return false;
            KernelSpec k;
            k.writes = { { t.name, t.comps } };
            const int from = c.I("from");
            std::string v;
            if (from == 0)
               v = "vec4(p_value, p_value4)";
            else if (from == 1)
            {
               const std::string src = c.S("src");
               if (!ValidAttribName(src))
               {
                  c.error = "'" + src + "' is not a usable attribute name";
                  return false;
               }
               const int sc = c.Input(0)->CompsOf(src);
               k.reads.push_back({ src, sc });
               v = sc == 4 ? src : (sc == 1 ? "vec4(" + src + ")" : "vec4(" + src + std::string(sc == 2 ? ", 0.0, 0.0)" : ", 0.0)"));
            }
            else if (from == 2)
               v = "vec4(float(id))";
            else if (from == 3)
               v = "vec4(float(id) / max(1.0, float(uCount - 1)))";
            else
            {
               std::string e = c.S("expr");
               if (e.find(';') != std::string::npos || e.find('{') != std::string::npos)
                  e = "0.0";
               k.reads = AllReads(*c.Input(0), 11);
               AddRead(k, attr::N, 3);
               AddRead(k, attr::Color, 4);
               AddRead(k, attr::Tex, 3);
               k.body = std::string("   ") + GlslType(t.comps) + " e_ = " + GlslType(t.comps) + "(" + e + ");\n";
               k.body += "   " + t.name + " = e_;\n";
               return c.Kernel(k, *c.Input(0));
            }
            k.body = Defines(t) + "   A = TA((" + v + ").SW);\n";
            return c.Kernel(k, *c.Input(0));
         };
         defs.push_back(d);
      }

      // ---- Attribute Combine -----------------------------------------------------
      {
         PopDef d = Simple("Attribute Combine PointZ", "Attribute Combine PointZ",
                           "Combines two attributes of the same points into a third: add, subtract, multiply, divide, min, "
                           "max, dot, cross, distance or average.", 1);
         d.params = { T("a", "A", "P"), T("b", "B", "N"), M("op", "operation", { "add", "subtract", "multiply", "divide", "min", "max",
                                                                               "dot", "cross", "distance", "average" }, 0),
                      T("dest", "result", "Result") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target a, b, r;
            if (!ResolveTarget(c, "a", a) || !ResolveTarget(c, "b", b))
               return false;
            const int op = c.I("op");
            const int comps = (op == 6 || op == 8) ? 1 : (op == 7 ? 3 : std::max(a.comps, b.comps));
            if (!ResolveTarget(c, "dest", r, comps))
               return false;
            KernelSpec k;
            k.reads = { { a.name, a.comps } };
            AddRead(k, b.name, b.comps);
            k.writes = { { r.name, comps } };
            auto widen = [&](const Target& t) {
               if (t.comps == comps) return t.name;
               return std::string("vec4(") + (t.comps == 1 ? t.name : t.name + (t.comps == 2 ? ", 0.0, 0.0" : t.comps == 3 ? ", 0.0" : "")) + ")." + Swz(comps);
            };
            const std::string A = (op >= 6 && op <= 8) ? "vec3(vec4(" + (a.comps == 1 ? a.name : a.name + std::string(a.comps == 2 ? ", 0.0, 0.0" : a.comps == 3 ? ", 0.0" : "")) + ").xyz)" : widen(a);
            const std::string Bx = (op >= 6 && op <= 8) ? "vec3(vec4(" + (b.comps == 1 ? b.name : b.name + std::string(b.comps == 2 ? ", 0.0, 0.0" : b.comps == 3 ? ", 0.0" : "")) + ").xyz)" : widen(b);
            static const char* kOps[] = { "(%A + %B)", "(%A - %B)", "(%A * %B)", "(%A / %B)", "min(%A, %B)", "max(%A, %B)",
                                          "dot(%A, %B)", "cross(%A, %B)", "distance(%A, %B)", "((%A + %B) * 0.5)" };
            std::string e = kOps[std::max(0, std::min(9, op))];
            for (size_t p; (p = e.find("%A")) != std::string::npos;) e.replace(p, 2, A);
            for (size_t p; (p = e.find("%B")) != std::string::npos;) e.replace(p, 2, Bx);
            k.body = "   " + r.name + " = " + std::string(GlslType(comps)) + "(" + e + ");\n";
            return c.Kernel(k, *c.Input(0));
         };
         defs.push_back(d);
      }

      // ---- Attribute Convert ------------------------------------------------------
      {
         PopDef d = Simple("Attribute Convert PointZ", "Attribute Convert PointZ",
                           "Copies, renames or deletes an attribute, or changes its component count (a single value can be "
                           "spread to every component).", 1);
         d.params = { M("op", "operation", { "copy / resize", "rename", "delete" }, 0), T("src", "attribute", "P"),
                      T("dest", "new name", "Rest", "op=0|1"), I("comps", "components", 3, 1, 4, "op=0"), B("spread", "spread a single value", true, "op=0") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const std::string src = c.S("src");
            if (!c.out->Has(src))
            {
               c.warning = "no attribute '" + src + "'";
               return true;
            }
            const int op = c.I("op");
            if (op == 2)
            {
               if (src == attr::P)
                  c.warning = "P cannot be deleted";
               else
                  c.out->Remove(src);
               return true;
            }
            const std::string dest = c.S("dest");
            if (!ValidAttribName(dest))
            {
               c.error = "'" + dest + "' is not a usable attribute name";
               return false;
            }
            if (op == 1)
            {
               if (src == attr::P)
               {
                  c.warning = "P cannot be renamed (copy it instead)";
                  return true;
               }
               c.out->Rename(src, dest);
               return true;
            }
            const int sc = c.out->CompsOf(src), dc = std::max(1, std::min(4, c.I("comps")));
            KernelSpec k;
            k.reads = { { src, sc } };
            k.writes = { { dest, dc } };
            std::string v4;
            if (sc == 1)
               v4 = c.Bl("spread") ? "vec4(" + src + ")" : "vec4(" + src + ", 0.0, 0.0, 0.0)";
            else if (sc == 2)
               v4 = "vec4(" + src + ", 0.0, 0.0)";
            else if (sc == 3)
               v4 = "vec4(" + src + ", 0.0)";
            else
               v4 = src;
            k.body = "   " + dest + " = " + std::string(GlslType(dc)) + "((" + v4 + ")." + Swz(dc) + ");\n";
            return c.Kernel(k, *c.Input(0));
         };
         defs.push_back(d);
      }

      // ---- Math Combine / Math Mix ------------------------------------------------
      {
         PopDef d = Simple("Math Combine PointZ", "Math Combine PointZ",
                           "Combines one attribute of the first input with the same-numbered points of the second: add, "
                           "subtract, multiply, divide, min, max, or replace.", 2);
         d.inputLabels = { "A", "B" };
         d.params = { T("attrib", "attribute", "P"), T("battrib", "B attribute (empty = same)", ""),
                      M("op", "operation", { "add", "subtract", "multiply", "divide", "min", "max", "replace" }, 0),
                      F("scale", "B scale", 1.0f, -10.0f, 10.0f), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            const PopData* b = c.Input(1);
            if (b == nullptr)
               return true;
            Target t;
            if (!ResolveTarget(c, "attrib", t))
               return false;
            std::string bn = c.S("battrib");
            if (bn.empty())
               bn = t.name;
            if (!ValidAttribName(bn))
            {
               c.error = "'" + bn + "' is not a usable attribute name";
               return false;
            }
            KernelSpec k;
            k.lookupInput = 1;
            k.reads = { { t.name, t.comps } };
            k.writes = { { t.name, t.comps } };
            k.lookups = { { bn, t.comps } };
            k.body = Defines(t) + "   TA bv = fetch_" + bn + "(id) * p_scale;\n" + R"GLSL(
   if (p_op == 0) A += bv; else if (p_op == 1) A -= bv; else if (p_op == 2) A *= bv;
   else if (p_op == 3) A /= bv; else if (p_op == 4) A = min(A, bv); else if (p_op == 5) A = max(A, bv); else A = bv;
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }
      {
         PopDef d = Simple("Math Mix PointZ", "Math Mix PointZ",
                           "Mixes two attributes of the same points by a weight (a number, or a weight attribute per point) "
                           "into a result attribute.", 1);
         d.params = { T("a", "A", "P"), T("b", "B", "Rest"), F("weight", "weight", 0.5f, 0.0f, 1.0f), T("wattr", "weight attribute", ""),
                      T("dest", "result", "P") };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target a, b, r;
            if (!ResolveTarget(c, "a", a) || !ResolveTarget(c, "b", b, a.comps) || !ResolveTarget(c, "dest", r, a.comps))
               return false;
            KernelSpec k;
            k.reads = { { a.name, a.comps } };
            AddRead(k, b.name, a.comps);
            k.writes = { { r.name, a.comps } };
            std::string w = "p_weight";
            const std::string wa = c.S("wattr");
            if (!wa.empty() && ValidAttribName(wa))
            {
               AddRead(k, wa, 1);
               w = "p_weight * " + wa;
            }
            k.body = "   " + r.name + " = mix(" + a.name + ", " + b.name + ", clamp(" + w + ", 0.0, 1.0));\n";
            return c.Kernel(k, *c.Input(0));
         };
         defs.push_back(d);
      }

      // ---- GLSL / GLSL Select --------------------------------------------------------
      {
         PopDef d = Simple("GLSL PointZ", "GLSL PointZ",
                           "Runs your own GLSL once per point. Every input attribute is a variable of its name (P, N, Color, "
                           "Tex ...); list the ones you change in 'outputs' (name or name:components, up to four). Also: id, "
                           "uCount, uTime, uBeat, uDelta, knobs uA..uD, fetch_P(i) for any point's position, sampleImage(uv) "
                           "from the image pin, and the noise / hash / rotation helpers.", 1);
         d.imageInput = true;
         d.params = { T("outputs", "outputs", "P"),
                      T("code", "code", "P += N * 0.1 * sin(uTime * 2.0 + P.y * 6.0);"),
                      F("a", "uA", 0.5f, 0.0f, 1.0f), F("b", "uB", 0.5f, 0.0f, 1.0f), F("c", "uC", 0.5f, 0.0f, 1.0f),
                      F("d", "uD", 0.5f, 0.0f, 1.0f), B("animate", "animate (cook every frame)", true), Mask() };
         d.animKey = "animate";
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            KernelSpec k;
            if (!ParseWrites(c, c.S("outputs"), k.writes))
               return false;
            k.reads = AllReads(*c.Input(0), 11);
            AddRead(k, attr::N, 3);
            AddRead(k, attr::Color, 4);
            AddRead(k, attr::Tex, 3);
            k.lookups = { { attr::P, 3 } };
            k.usesImage = true;
            k.body = c.S("code");
            return Run(c, k, { Flt("uA", c.F("a")), Flt("uB", c.F("b")), Flt("uC", c.F("c")), Flt("uD", c.F("d")) });
         };
         defs.push_back(d);
      }
      {
         PopDef d = Simple("GLSL Select PointZ", "GLSL Select PointZ",
                           "Marks points with a GLSL condition (true = 1) into a group attribute, like Group PointZ's GLSL mode "
                           "but with the knobs uA..uD and every attribute in scope. Follow with Delete PointZ to keep only them.", 1);
         d.params = { T("name", "group attribute", "Group"), T("cond", "condition", "P.y > uA - 0.5"),
                      F("a", "uA", 0.5f, 0.0f, 1.0f), F("b", "uB", 0.5f, 0.0f, 1.0f), B("animate", "animate", false) };
         d.animKey = "animate";
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target g;
            if (!ResolveTarget(c, "name", g, 1))
               return false;
            std::string cond = c.S("cond");
            if (cond.find(';') != std::string::npos || cond.find('{') != std::string::npos || cond.empty())
               cond = "false";
            KernelSpec k;
            k.reads = AllReads(*c.Input(0), 11);
            AddRead(k, attr::N, 3);
            AddRead(k, attr::Color, 4);
            AddRead(k, attr::Tex, 3);
            k.writes = { { g.name, 1 } };
            k.body = "   " + g.name + " = bool(" + cond + ") ? 1.0 : 0.0;\n";
            return c.Kernel(k, *c.Input(0), { Flt("uA", c.F("a")), Flt("uB", c.F("b")) });
         };
         defs.push_back(d);
      }

      // ---- Image to PointZ / Signal to PointZ ----------------------------------------------------
      {
         PopDef d = Simple("Image to PointZ", "Image to PointZ",
                           "Turns an image into points: a grid of columns x rows over the picture (keeping its aspect), each "
                           "point coloured by the pixel under it and lifted by its brightness. Polygons, row lines or points.", 0);
         d.imageInput = true;
         d.params = { I("cols", "columns", 160, 2, 2048), B("aspect", "rows from aspect", true), I("rows", "rows", 90, 2, 2048, "aspect=0"),
                      F("width", "width", 2.0f, 0.0f, 50.0f), F("height", "height from brightness", 0.0f, -5.0f, 5.0f),
                      M("kind", "output", { "polygons", "lines", "points" }, 2), B("alpha", "drop transparent pixels", false) };
         d.cook = [](CookCtx& c) {
            if (c.image == 0 || c.imageW <= 0 || c.imageH <= 0)
            {
               c.warning = "no image on the image input";
               c.out = std::make_shared<PopData>();
               return true;
            }
            const int cols = std::max(2, c.I("cols"));
            const float aspect = (float)c.imageH / (float)c.imageW;
            const int rows = c.Bl("aspect") ? std::max(2, (int)std::lround(cols * aspect)) : std::max(2, c.I("rows"));
            const float w = c.F("width");
            // Turbo 0.62: the grid only changes with its shape; a live image
            // reruns the kernel over the cached one instead of rebuilding
            // (and re-uploading) every point on the CPU each frame.
            struct GridState : OpState
            {
               int cols = 0, rows = 0, kind = -1;
               float w = 0.0f, h = 0.0f;
               std::shared_ptr<PopData> grid;
            };
            GridState& gs = c.State<GridState>();
            if (!gs.grid || gs.cols != cols || gs.rows != rows || gs.kind != c.I("kind") || gs.w != w || gs.h != w * aspect)
            {
               gs.grid = GridFromImage(cols, rows, w, w * aspect, c.I("kind"));
               gs.cols = cols;
               gs.rows = rows;
               gs.kind = c.I("kind");
               gs.w = w;
               gs.h = w * aspect;
            }
            auto grid = ShallowCopy(*gs.grid);
            KernelSpec k;
            k.usesImage = true;
            k.reads = { { attr::P, 3 }, { attr::Tex, 3 } };
            k.writes = { { attr::Color, 4 }, { attr::P, 3 }, { attr::Active, 1 } };
            k.body = R"GLSL(
   vec4 s = sampleImage(Tex.xy);
   Color = s;
   P.z += dot(s.rgb, vec3(0.2126, 0.7152, 0.0722)) * uHeight;
   Active = (uAlpha != 0 && s.a < 0.01) ? 0.0 : 1.0;
)GLSL";
            c.out = grid;
            return c.Kernel(k, *grid, { Flt("uHeight", c.F("height")), Int("uAlpha", c.Bl("alpha") ? 1 : 0) });
         };
         defs.push_back(d);
      }
      {
         PopDef d = Simple("Signal to PointZ", "Signal to PointZ",
                           "Turns a row of an image into a line of points - one per pixel across - lifted by a channel's "
                           "value. Feed it an Audio Texture for a live waveform or spectrum as geometry.", 0);
         d.imageInput = true;
         d.params = { F("row", "row (0..1)", 0.0f, 0.0f, 1.0f), M("chan", "channel", { "red", "green", "blue", "alpha", "brightness" }, 0),
                      I("points", "points", 256, 2, 8192), F("width", "width", 2.0f, 0.0f, 50.0f), F("amp", "amplitude", 1.0f, -10.0f, 10.0f),
                      F("bias", "centre (value at rest)", 0.5f, -1.0f, 1.0f), T("dest", "value attribute", "Value") };
         d.cook = [](CookCtx& c) {
            const int n = std::max(2, c.I("points"));
            auto line = GridFromImage(n, 2, c.F("width"), 0.0f, 1);
            // Keep only the first row as a single strip.
            auto topo = NewTopo();
            std::vector<uint32_t> s((size_t)n);
            for (int i = 0; i < n; i++)
               s[(size_t)i] = (uint32_t)i;
            topo->AddStrip(s, false);
            line->topo = topo;
            line->dims[0] = 1;
            line->dims[1] = n;
            if (c.image == 0)
            {
               c.warning = "no image on the image input";
               c.out = line;
               return true;
            }
            Target t;
            if (!ResolveTarget(c, "dest", t, 1))
               return false;
            KernelSpec k;
            k.usesImage = true;
            k.reads = { { attr::P, 3 }, { attr::Tex, 3 } };
            k.writes = { { attr::P, 3 }, { t.name, 1 } };
            k.body = R"GLSL(
   vec4 s = sampleImage(vec2(Tex.x, uRow));
   float v = p_chan == 4 ? dot(s.rgb, vec3(0.2126, 0.7152, 0.0722)) : s[p_chan];
   v -= p_bias;
   P.y = v * p_amp;
)GLSL" + std::string("   ") + t.name + " = v;\n";
            c.out = line;
            return c.Kernel(k, *line, { Flt("uRow", c.F("row")) });
         };
         defs.push_back(d);
      }
   }

   bool ParseWritesPublic(CookCtx& c, const std::string& list, std::vector<KernelIO>& w) { return ParseWrites(c, list, w); }

   void RegisterTierA(std::vector<PopDef>& defs)
   {
      RegisterGenerators(defs);
      RegisterKernelsA1(defs);
      RegisterKernelsA2(defs);
   }
}
