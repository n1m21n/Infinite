// PointZ operators, tier B (third part): operators with memory (Particle,
// Feedback, Trail, Cache), GLSL that creates points, file input / output and
// Instance PointZ (TouchDesigner's Geometry COMP instancing, as a node).

#include "PopOpsInternal.h"
#include "PopCpu.h"
#include "PopGpu.h"

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <cstring>
#include <cctype>
#include <deque>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <unordered_map>

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

      std::shared_ptr<PopData> PointsOnly(Arrays&& a)
      {
         auto topo = NewTopo();
         topo->pointPrims = true;
         return Store(std::move(a), topo);
      }

      // ---- Particle state ---------------------------------------------------------
      struct ParticleState : OpState
      {
         Arrays parts;
         double carry = 0.0;
         double time = 0.0; // Turbo 0.61: drives the built-in turbulence
         float nextId = 0.0f;
         uint32_t rng = 0x9e3779b9u;
         bool init = false;
         // Turbo 0.62: last frame's source, read back without stalling.
         std::shared_ptr<AsyncRows> async;
         float Rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(rng & 0xffffff) / 16777215.0f; }
      };

      struct FrameState : OpState
      {
         std::deque<DataPtr> frames; // newest first
         bool lastWrite = false;
         unsigned long long written = 0;
      };

      struct TrailState : OpState
      {
         struct Snap { std::vector<float> P, C, id; };
         std::deque<Snap> snaps; // newest first
         int frame = 0;
         // GPU path (points matched by index): history buffers of L blocks of
         // N points, newest block first, shifted on the GPU; topology and
         // TrailAge stay the same while N and L do.
         BufferPtr histP, histC;
         int histN = -1, histL = -1;
         std::shared_ptr<PopTopology> topo;
         BufferPtr age;
         std::shared_ptr<PopData> last;
      };

      std::string ReadFileText(const std::string& path, std::string& error)
      {
         std::ifstream f(path, std::ios::binary);
         if (!f)
         {
            error = "cannot open " + path;
            return std::string();
         }
         std::stringstream ss;
         ss << f.rdbuf();
         return ss.str();
      }

      std::string Lower(std::string s)
      {
         for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
         return s;
      }

      std::string Ext(const std::string& path)
      {
         const size_t dot = path.find_last_of('.');
         return dot == std::string::npos ? std::string() : Lower(path.substr(dot + 1));
      }

      // PLY (ascii or binary little endian). Vertex x y z, nx ny nz, red green
      // blue alpha (uchar 0..255 or float), s t / u v / texture_u texture_v,
      // scale; any other vertex property becomes an attribute of that name.
      // Faces (vertex_indices / vertex_index) are fanned into triangles.
      bool ReadPly(const std::string& bytes, std::shared_ptr<PopData>& out, bool faces, std::string& error)
      {
         const size_t endHeader = bytes.find("end_header");
         if (bytes.compare(0, 3, "ply") != 0 || endHeader == std::string::npos)
         {
            error = "not a PLY file";
            return false;
         }
         size_t body = bytes.find('\n', endHeader);
         body = body == std::string::npos ? bytes.size() : body + 1;
         std::istringstream hdr(bytes.substr(0, endHeader));
         std::string line;
         bool ascii = true;
         struct Prop { std::string name, type, countType; bool list = false; };
         struct Elem { std::string name; long long count = 0; std::vector<Prop> props; };
         std::vector<Elem> elems;
         while (std::getline(hdr, line))
         {
            std::istringstream ls(line);
            std::string w;
            ls >> w;
            if (w == "format") { std::string f; ls >> f; ascii = f == "ascii"; if (f == "binary_big_endian") { error = "big-endian PLY is not supported"; return false; } }
            else if (w == "element") { Elem e; ls >> e.name >> e.count; elems.push_back(e); }
            else if (w == "property" && !elems.empty())
            {
               Prop p;
               std::string t;
               ls >> t;
               if (t == "list") { p.list = true; ls >> p.countType >> p.type >> p.name; }
               else { p.type = t; ls >> p.name; }
               elems.back().props.push_back(p);
            }
         }
         auto size = [](const std::string& t) -> int {
            if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
            if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
            if (t == "double" || t == "float64") return 8;
            return 4;
         };
         size_t pos = body;
         std::istringstream asc(ascii ? bytes.substr(body) : std::string());
         auto readVal = [&](const std::string& t, double& v) -> bool {
            if (ascii) return (bool)(asc >> v);
            const int s = size(t);
            if (pos + (size_t)s > bytes.size()) return false;
            const char* p = bytes.data() + pos;
            pos += (size_t)s;
            if (t == "char" || t == "int8") v = (double)*(const int8_t*)p;
            else if (t == "uchar" || t == "uint8") v = (double)*(const uint8_t*)p;
            else if (t == "short" || t == "int16") { int16_t x; std::memcpy(&x, p, 2); v = x; }
            else if (t == "ushort" || t == "uint16") { uint16_t x; std::memcpy(&x, p, 2); v = x; }
            else if (t == "int" || t == "int32") { int32_t x; std::memcpy(&x, p, 4); v = x; }
            else if (t == "uint" || t == "uint32") { uint32_t x; std::memcpy(&x, p, 4); v = x; }
            else if (t == "double" || t == "float64") { double x; std::memcpy(&x, p, 8); v = x; }
            else { float x; std::memcpy(&x, p, 4); v = x; }
            return true;
         };
         Arrays a;
         auto topo = NewTopo();
         for (const Elem& e : elems)
         {
            if (e.name == "vertex")
            {
               a.count = (int)e.count;
               std::vector<std::pair<int, int>> slot(e.props.size(), { -1, 0 });
               for (size_t i = 0; i < e.props.size(); i++)
               {
                  const std::string& n = e.props[i].name;
                  if (n == "x" || n == "y" || n == "z") slot[i] = { a.Ensure(attr::P, 3), n[0] - 'x' };
                  else if (n == "nx" || n == "ny" || n == "nz") slot[i] = { a.Ensure(attr::N, 3), n[1] - 'x' };
                  else if (n == "red" || n == "r") slot[i] = { a.Ensure(attr::Color, 4), 0 };
                  else if (n == "green" || n == "g") slot[i] = { a.Ensure(attr::Color, 4), 1 };
                  else if (n == "blue" || n == "b") slot[i] = { a.Ensure(attr::Color, 4), 2 };
                  else if (n == "alpha" || n == "a") slot[i] = { a.Ensure(attr::Color, 4), 3 };
                  else if (n == "s" || n == "u" || n == "texture_u") slot[i] = { a.Ensure(attr::Tex, 3), 0 };
                  else if (n == "t" || n == "v" || n == "texture_v") slot[i] = { a.Ensure(attr::Tex, 3), 1 };
                  else if (n == "scale") slot[i] = { a.Ensure(attr::PScale, 1), 0 };
                  else if (ValidAttribName(n) && !e.props[i].list) slot[i] = { a.Ensure(n, 1), 0 };
               }
               a.Ensure(attr::P, 3);
               for (long long v = 0; v < e.count; v++)
                  for (size_t i = 0; i < e.props.size(); i++)
                  {
                     const Prop& p = e.props[i];
                     double x = 0;
                     if (p.list)
                     {
                        if (!readVal(p.countType, x)) { error = "PLY ends early"; return false; }
                        for (int k = 0; k < (int)x; k++) { double y; readVal(p.type, y); }
                        continue;
                     }
                     if (!readVal(p.type, x)) { error = "PLY ends early"; return false; }
                     if (slot[i].first < 0) continue;
                     const bool col = a.names[(size_t)slot[i].first] == attr::Color;
                     if (col && (p.type == "uchar" || p.type == "uint8")) x /= 255.0;
                     a.At(slot[i].first, (int)v)[slot[i].second] = (float)x;
                  }
            }
            else
            {
               for (long long v = 0; v < e.count; v++)
                  for (const Prop& p : e.props)
                  {
                     double x = 0;
                     if (!p.list) { if (!readVal(p.type, x)) return true; continue; }
                     if (!readVal(p.countType, x)) return true;
                     std::vector<uint32_t> idx;
                     for (int k = 0; k < (int)x; k++) { double y; readVal(p.type, y); idx.push_back((uint32_t)y); }
                     if (faces && e.name == "face" && (p.name == "vertex_indices" || p.name == "vertex_index"))
                        for (size_t k = 1; k + 1 < idx.size(); k++)
                           topo->tris.insert(topo->tris.end(), { idx[0], idx[k], idx[k + 1] });
                  }
            }
         }
         topo->pointPrims = topo->tris.empty();
         for (uint32_t& i : topo->tris) if (i >= (uint32_t)a.count) i = 0;
         out = Store(std::move(a), topo);
         return true;
      }

      // Whitespace / comma separated numbers, one point per line.
      Arrays ReadTable(const std::string& text, const std::string& columns, std::string& warning)
      {
         std::vector<std::pair<std::string, int>> cols;
         for (const std::string& item : SplitList(columns))
         {
            const size_t colon = item.find(':');
            std::string name = colon == std::string::npos ? item : item.substr(0, colon);
            int comps = colon == std::string::npos ? DefaultComps(name) : std::atoi(item.c_str() + colon + 1);
            if (!ValidAttribName(name)) { warning = "skipped column '" + item + "'"; continue; }
            cols.push_back({ name, std::max(1, std::min(4, comps)) });
         }
         if (cols.empty())
            cols.push_back({ attr::P, 3 });
         Arrays a;
         a.Ensure(attr::P, 3);
         std::vector<int> idx;
         for (auto& c : cols) idx.push_back(a.Ensure(c.first, c.second));
         std::istringstream in(text);
         std::string line;
         while (std::getline(in, line))
         {
            for (char& ch : line) if (ch == ',' || ch == ';' || ch == '\t') ch = ' ';
            std::istringstream ls(line);
            std::vector<float> v;
            float x;
            while (ls >> x) v.push_back(x);
            if (v.empty()) continue;
            const int p = a.count++;
            for (size_t at = 0; at < a.data.size(); at++)
            {
               float d[4];
               DefaultValue(a.names[at], d);
               for (int k = 0; k < a.comps[at]; k++) a.data[at].push_back(d[k]);
            }
            size_t col = 0;
            for (size_t c = 0; c < cols.size(); c++)
               for (int k = 0; k < cols[c].second; k++, col++)
                  if (col < v.size()) a.At(idx[c], p)[k] = v[col];
         }
         return a;
      }

      bool ReadObj(const std::string& text, std::shared_ptr<PopData>& out, std::string& error)
      {
         std::vector<float> v, vt, vn;
         Arrays a;
         const int ip = a.Ensure(attr::P, 3), it = a.Ensure(attr::Tex, 3), in = a.Ensure(attr::N, 3);
         std::map<std::tuple<int, int, int>, int> remap;
         auto topo = NewTopo();
         auto vertex = [&](const std::string& tok) -> int {
            int iv = 0, itx = 0, inn = 0;
            std::sscanf(tok.c_str(), "%d/%d/%d", &iv, &itx, &inn);
            if (tok.find("//") != std::string::npos) { std::sscanf(tok.c_str(), "%d//%d", &iv, &inn); itx = 0; }
            const int nv = (int)v.size() / 3, nt = (int)vt.size() / 2, nn = (int)vn.size() / 3;
            if (iv < 0) iv = nv + iv + 1;
            if (itx < 0) itx = nt + itx + 1;
            if (inn < 0) inn = nn + inn + 1;
            auto key = std::make_tuple(iv, itx, inn);
            auto f = remap.find(key);
            if (f != remap.end()) return f->second;
            const int p = a.count++;
            for (size_t at = 0; at < a.data.size(); at++)
            {
               float d[4];
               DefaultValue(a.names[at], d);
               for (int k = 0; k < a.comps[at]; k++) a.data[at].push_back(d[k]);
            }
            if (iv >= 1 && iv <= nv) std::copy(&v[(size_t)(iv - 1) * 3], &v[(size_t)(iv - 1) * 3] + 3, a.At(ip, p));
            if (itx >= 1 && itx <= nt) { a.At(it, p)[0] = vt[(size_t)(itx - 1) * 2]; a.At(it, p)[1] = vt[(size_t)(itx - 1) * 2 + 1]; }
            if (inn >= 1 && inn <= nn) std::copy(&vn[(size_t)(inn - 1) * 3], &vn[(size_t)(inn - 1) * 3] + 3, a.At(in, p));
            remap[key] = p;
            return p;
         };
         std::istringstream ss(text);
         std::string line;
         while (std::getline(ss, line))
         {
            std::istringstream ls(line);
            std::string w;
            ls >> w;
            if (w == "v") { float x = 0, y = 0, z = 0; ls >> x >> y >> z; v.insert(v.end(), { x, y, z }); }
            else if (w == "vt") { float x = 0, y = 0; ls >> x >> y; vt.insert(vt.end(), { x, y }); }
            else if (w == "vn") { float x = 0, y = 0, z = 0; ls >> x >> y >> z; vn.insert(vn.end(), { x, y, z }); }
            else if (w == "f")
            {
               std::vector<int> f;
               std::string tok;
               while (ls >> tok) f.push_back(vertex(tok));
               for (size_t k = 1; k + 1 < f.size(); k++)
                  topo->tris.insert(topo->tris.end(), { (uint32_t)f[0], (uint32_t)f[k], (uint32_t)f[k + 1] });
            }
            else if (w == "l")
            {
               std::vector<uint32_t> s;
               std::string tok;
               while (ls >> tok) s.push_back((uint32_t)vertex(tok));
               if (s.size() >= 2) topo->AddStrip(s, false);
            }
         }
         if (a.count == 0 && !v.empty())
         {
            for (size_t i = 0; i + 2 < v.size(); i += 3) vertex(std::to_string(i / 3 + 1));
            topo->pointPrims = true;
         }
         if (a.count == 0)
         {
            error = "no vertices in the OBJ file";
            return false;
         }
         out = Store(std::move(a), topo);
         return true;
      }

      bool WriteFile(const PopData& d, const std::string& path, int format, std::string& error)
      {
         const std::vector<float> P = d.Read(attr::P, 3), N = d.Read(attr::N, 3), C = d.Read(attr::Color, 4), T = d.Read(attr::Tex, 2);
         const bool hasN = d.Has(attr::N), hasC = d.Has(attr::Color), hasT = d.Has(attr::Tex);
         const PopTopology& t = d.Topo();
         const int n = d.pointCount;
         std::ofstream f(path, std::ios::binary);
         if (!f) { error = "cannot write " + path; return false; }
         if (format == 0 || format == 1)
         {
            const bool bin = format == 0;
            f << "ply\nformat " << (bin ? "binary_little_endian" : "ascii") << " 1.0\ncomment Infinite-Turbo PointZ\nelement vertex " << n
              << "\nproperty float x\nproperty float y\nproperty float z\n";
            if (hasN) f << "property float nx\nproperty float ny\nproperty float nz\n";
            if (hasT) f << "property float s\nproperty float t\n";
            if (hasC) f << "property uchar red\nproperty uchar green\nproperty uchar blue\nproperty uchar alpha\n";
            f << "element face " << t.TriangleCount() << "\nproperty list uchar int vertex_indices\nend_header\n";
            for (int i = 0; i < n; i++)
            {
               float vals[8];
               int k = 0;
               for (int j = 0; j < 3; j++) vals[k++] = P[(size_t)i * 3 + j];
               if (hasN) for (int j = 0; j < 3; j++) vals[k++] = N[(size_t)i * 3 + j];
               if (hasT) for (int j = 0; j < 2; j++) vals[k++] = T[(size_t)i * 2 + j];
               unsigned char col[4];
               for (int j = 0; j < 4; j++) col[j] = (unsigned char)std::max(0.0f, std::min(255.0f, C[(size_t)i * 4 + j] * 255.0f + 0.5f));
               if (bin)
               {
                  f.write((const char*)vals, (std::streamsize)(k * sizeof(float)));
                  if (hasC) f.write((const char*)col, 4);
               }
               else
               {
                  for (int j = 0; j < k; j++) f << vals[j] << ' ';
                  if (hasC) f << (int)col[0] << ' ' << (int)col[1] << ' ' << (int)col[2] << ' ' << (int)col[3];
                  f << '\n';
               }
            }
            for (size_t i = 0; i + 2 < t.tris.size(); i += 3)
            {
               if (bin)
               {
                  const unsigned char three = 3;
                  const int32_t idx[3] = { (int32_t)t.tris[i], (int32_t)t.tris[i + 1], (int32_t)t.tris[i + 2] };
                  f.write((const char*)&three, 1);
                  f.write((const char*)idx, 12);
               }
               else
                  f << "3 " << t.tris[i] << ' ' << t.tris[i + 1] << ' ' << t.tris[i + 2] << '\n';
            }
         }
         else if (format == 2)
         {
            f << "# Infinite-Turbo PointZ\n";
            for (int i = 0; i < n; i++) f << "v " << P[(size_t)i * 3] << ' ' << P[(size_t)i * 3 + 1] << ' ' << P[(size_t)i * 3 + 2] << '\n';
            for (int i = 0; i < n && hasT; i++) f << "vt " << T[(size_t)i * 2] << ' ' << T[(size_t)i * 2 + 1] << '\n';
            for (int i = 0; i < n && hasN; i++) f << "vn " << N[(size_t)i * 3] << ' ' << N[(size_t)i * 3 + 1] << ' ' << N[(size_t)i * 3 + 2] << '\n';
            auto ref = [&](uint32_t i) {
               std::string s = std::to_string(i + 1);
               if (hasT || hasN) s += "/" + (hasT ? std::to_string(i + 1) : std::string()) + (hasN ? "/" + std::to_string(i + 1) : std::string());
               return s;
            };
            for (size_t i = 0; i + 2 < t.tris.size(); i += 3) f << "f " << ref(t.tris[i]) << ' ' << ref(t.tris[i + 1]) << ' ' << ref(t.tris[i + 2]) << '\n';
            for (size_t s = 0; s < t.StripCount(); s++)
            {
               f << 'l';
               for (int k = 0; k < t.lineCount[s]; k++) f << ' ' << t.lineVerts[(size_t)(t.lineStart[s] + k)] + 1;
               if (t.lineClosed[s]) f << ' ' << t.lineVerts[(size_t)t.lineStart[s]] + 1;
               f << '\n';
            }
         }
         else
         {
            for (size_t a = 0; a < d.attribs.size(); a++)
            {
               const int c = d.attribs[a].buffer->Comps();
               for (int k = 0; k < c; k++) f << (a || k ? "," : "") << d.attribs[a].name << (c > 1 ? std::string(".") + "xyzw"[k] : std::string());
            }
            f << '\n';
            std::vector<std::vector<float>> cols;
            for (const PopAttrib& at : d.attribs) cols.push_back(d.Read(at.name));
            for (int i = 0; i < n; i++)
            {
               for (size_t a = 0; a < d.attribs.size(); a++)
               {
                  const int c = d.attribs[a].buffer->Comps();
                  for (int k = 0; k < c; k++) f << (a || k ? "," : "") << cols[a][(size_t)i * c + k];
               }
               f << '\n';
            }
         }
         return (bool)f;
      }
   }

   void RegisterTimeB(std::vector<PopDef>& defs)
   {
      // ---- Particle ----------------------------------------------------------------
      {
         PopDef d = Cpu("Particle PointZ", "A particle system born from the input's points: 'birth rate' per second (or 'per point' "
                                        "every frame), each with a life, starting velocity and the source point's attributes. Writes "
                                        "PartId, PartVel, PartAge, PartLifeSpan, PartForce, PartMass, PartDrag. Forces: gravity here, "
                                        "or any PointZ nodes after it that add to PartForce (Force Radial, Noise on PartForce...) ending in a "
                                        "Null PointZ whose 'tag' you put in 'target' - next frame the particles carry on from there.", 1);
         d.inputLabels = { "source" };
         d.everyFrame = true;
         d.params = { M("birth", "birth", { "rate per second", "per point each frame" }, 0), F("rate", "birth rate", 200.0f, 0.0f, 100000.0f, "birth=0"),
                      F("life", "life (s)", 3.0f, 0.01f, 600.0f), F("lifeVar", "life variance", 0.3f, 0.0f, 1.0f),
                      V("vel", "velocity", 0, 0.5f, 0, -20.0f, 20.0f), F("speedN", "speed along N", 0.0f, -20.0f, 20.0f),
                      F("velRand", "random velocity", 0.2f, 0.0f, 20.0f), F("jitter", "birth jitter", 0.0f, 0.0f, 2.0f),
                      V("gravity", "gravity", 0, -1.0f, 0, -50.0f, 50.0f), F("drag", "drag", 0.0f, 0.0f, 10.0f),
                      F("damp", "damping", 0.0f, 0.0f, 1.0f), F("mass", "mass", 1.0f, 0.001f, 100.0f),
                      I("max", "max particles", 100000, 1, 10000000), F("timeScale", "time scale", 1.0f, 0.0f, 10.0f),
                      B("inherit", "copy source attributes", true), T("target", "target (Null PointZ tag)", ""), B("play", "play", true),
                      F("seed", "seed", 1.0f, 0.0f, 1000.0f),
                      // Turbo 0.61: over life (age / life span) and a built-in turbulence.
                      // Appended, so older patches keep their parameter slots.
                      F("sizeBirth", "size at birth (x PScale)", 1.0f, 0.0f, 10.0f),
                      F("sizeDeath", "size at death (x PScale)", 1.0f, 0.0f, 10.0f),
                      F("sizeCurve", "size curve", 1.0f, 0.1f, 8.0f),
                      B("colorLife", "colour over life", false),
                      C("colorBirth", "colour at birth", 1.0f, 1.0f, 1.0f, "colorLife=1"),
                      C("colorDeath", "colour at death", 1.0f, 0.3f, 0.1f, "colorLife=1"),
                      F("turb", "turbulence", 0.0f, 0.0f, 50.0f), F("turbScale", "turbulence scale", 1.0f, 0.05f, 20.0f),
                      F("turbSpeed", "turbulence speed", 1.0f, 0.0f, 10.0f) };
         d.cook = [](CookCtx& c) {
            ParticleState& s = c.State<ParticleState>();
            const PopData* src = c.Input(0);
            if (c.reset || !s.init)
            {
               s = ParticleState();
               s.init = true;
               s.rng = (uint32_t)(c.F("seed") * 7919.0f) * 2654435761u + 1u;
               s.parts.Ensure(attr::P, 3);
            }
            Arrays& a = s.parts;
            // The loop: adopt what the downstream target made of last frame's particles.
            const std::string target = c.S("target");
            if (!target.empty() && c.findTagged)
            {
               const PopData* back = c.findTagged(target);
               if (back != nullptr && back->pointCount == a.count && back->Has(attr::PartId) && a.count > 0)
               {
                  const std::vector<float> ids = back->Read(attr::PartId, 1);
                  const int ii = a.Index(attr::PartId);
                  if (ii >= 0 && ids[0] == *a.At(ii, 0) && ids.back() == *a.At(ii, a.count - 1))
                     a = Load(*back);
               }
               else if (back == nullptr)
                  c.warning = "no Null PointZ tagged '" + target + "'";
            }
            const int iP = a.Ensure(attr::P, 3), iV = a.Ensure(attr::PartVel, 3), iAge = a.Ensure(attr::PartAge, 1),
                      iLife = a.Ensure(attr::PartLifeSpan, 1), iId = a.Ensure(attr::PartId, 1), iF = a.Ensure(attr::PartForce, 3),
                      iM = a.Ensure(attr::PartMass, 1), iD = a.Ensure(attr::PartDrag, 1);
            const float dt = c.Bl("play") ? std::min(0.1f, c.env.delta * c.F("timeScale")) : 0.0f;
            float g[3];
            c.V3("gravity", g);
            const float damp = c.F("damp");
            // Turbo 0.61: a smooth, divergence-ish swirl from summed sines of
            // the position, moving with time - turbulence without a force chain.
            const float turb = c.F("turb"), turbK = c.F("turbScale");
            s.time += dt * c.F("turbSpeed");
            const float tt = (float)s.time;
            // Integrate and age.
            std::vector<char> keep((size_t)a.count, 1);
            int alive = 0;
            const int iDeath = a.Index(attr::PartDeath);
            for (int i = 0; i < a.count; i++)
            {
               float* p = a.At(iP, i);
               float* v = a.At(iV, i);
               float* f = a.At(iF, i);
               const float m = std::max(1e-4f, *a.At(iM, i));
               const float sp = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
               const float dragK = *a.At(iD, i) * sp;
               float tf[3] = { 0.0f, 0.0f, 0.0f };
               if (turb > 0.0f)
               {
                  const float x = p[0] * turbK, y = p[1] * turbK, z = p[2] * turbK;
                  tf[0] = turb * (std::sin(y + tt * 0.9f) + std::sin(z * 1.7f + tt * 1.3f)) * 0.5f;
                  tf[1] = turb * (std::sin(z + tt * 1.1f) + std::sin(x * 1.3f + tt * 0.7f)) * 0.5f;
                  tf[2] = turb * (std::sin(x + tt * 1.2f) + std::sin(y * 1.5f + tt * 0.8f)) * 0.5f;
               }
               for (int k = 0; k < 3; k++)
               {
                  const float force = f[k] + g[k] * m - dragK * v[k] + tf[k] * m;
                  v[k] = (v[k] + force / m * dt) * (1.0f - damp * std::min(1.0f, dt * 60.0f));
                  p[k] += v[k] * dt;
                  f[k] = 0.0f;
               }
               float* age = a.At(iAge, i);
               *age += dt;
               const float life = *a.At(iLife, i);
               const bool dead = (life > 0.0f && *age >= life) || (iDeath >= 0 && *a.At(iDeath, i) > 0.5f);
               keep[(size_t)i] = !dead;
               alive += !dead;
            }
            if (alive != a.count)
            {
               // Turbo 0.62: compacted in place, attribute by attribute (a
               // PointBuilder copy of every living particle cost ~3 ms a frame
               // at 7k particles).
               for (size_t at = 0; at < a.data.size(); at++)
               {
                  const int cc = a.comps[at];
                  float* d = a.data[at].data();
                  int w = 0;
                  for (int i = 0; i < a.count; i++)
                     if (keep[(size_t)i])
                     {
                        if (w != i)
                           std::memmove(d + (size_t)w * cc, d + (size_t)i * cc, (size_t)cc * sizeof(float));
                        w++;
                     }
                  a.data[at].resize((size_t)w * cc);
               }
               a.count = alive;
            }
            // Births.
            // Turbo 0.62: a GPU-made source is sampled from the copy the GPU
            // finished last frame (AsyncRows) - reading it now would make the
            // CPU wait for every kernel queued before. A CPU source is read directly.
            const bool inheritAll = c.Bl("inherit");
            bool srcOnCpu = src != nullptr;
            if (src != nullptr)
               for (const PopAttrib& at : src->attribs)
                  if ((inheritAll || at.name == attr::P || at.name == attr::N) && !at.buffer->OnCpu())
                     srcOnCpu = false;
            const AsyncRows::Snapshot* snap = nullptr;
            if (src != nullptr && !srcOnCpu)
            {
               if (!s.async)
                  s.async = std::make_shared<AsyncRows>();
               snap = s.async->Latest();
               if (snap != nullptr && snap->count <= 0)
                  snap = nullptr;
            }
            if (src != nullptr && src->pointCount > 0 && c.Bl("play"))
            {
               int births = 0;
               if (c.I("birth") == 0)
               {
                  s.carry += (double)c.F("rate") * dt;
                  births = (int)s.carry;
                  s.carry -= births;
               }
               else
                  births = src->pointCount;
               const int maxP = std::max(1, c.I("max"));
               births = std::max(0, std::min(births, maxP - a.count));
               if (births > 0)
               {
                  // Turbo 0.62: draw every random number first, in the order the
                  // births always used, then read only the source points picked -
                  // not the whole (often GPU-made, every frame new) source.
                  struct Draw
                  {
                     int si;
                     float jit[3], vr[3], lr;
                  };
                  std::vector<Draw> draws((size_t)births);
                  std::vector<int> rows((size_t)births);
                  const int srcCount = snap ? snap->count : src->pointCount;
                  for (int b = 0; b < births; b++)
                  {
                     Draw& dr = draws[(size_t)b];
                     dr.si = c.I("birth") == 1 ? std::min(b, srcCount - 1) : std::min(srcCount - 1, (int)(s.Rnd() * srcCount));
                     for (int k = 0; k < 3; k++) dr.jit[k] = s.Rnd();
                     for (int k = 0; k < 3; k++) dr.vr[k] = s.Rnd();
                     dr.lr = s.Rnd();
                     rows[(size_t)b] = dr.si;
                  }
                  Arrays sa;
                  if (snap != nullptr)
                  {
                     // Load's layout (P first) from the snapshot's rows.
                     sa.count = births;
                     auto take = [&](size_t k) {
                        const int cc = snap->comps[k];
                        std::vector<float> v((size_t)births * cc);
                        for (int b = 0; b < births; b++)
                           std::memcpy(&v[(size_t)b * cc], snap->data[k] + (size_t)rows[(size_t)b] * cc, cc * sizeof(float));
                        sa.names.push_back(snap->names[k]);
                        sa.comps.push_back(cc);
                        sa.data.push_back(std::move(v));
                     };
                     for (size_t k = 0; k < snap->names.size(); k++)
                        if (snap->names[k] == attr::P)
                           take(k);
                     if (sa.names.empty() || sa.comps[0] != 3)
                        sa = LoadRows(*src, rows); // odd P: the plain path
                     else
                        for (size_t k = 0; k < snap->names.size(); k++)
                           if (snap->names[k] != attr::P)
                              take(k);
                  }
                  else
                     sa = LoadRows(*src, rows);
                  const int saN = sa.Index(attr::N);
                  float vel[3];
                  c.V3("vel", vel);
                  const float speedN = c.F("speedN"), velRand = c.F("velRand"), jitter = c.F("jitter");
                  const float life = c.F("life"), var = c.F("lifeVar"), mass = c.F("mass"), drag = c.F("drag");
                  const bool inherit = c.Bl("inherit");
                  if (inherit)
                     for (size_t at = 0; at < sa.names.size(); at++)
                        a.Ensure(sa.names[at], sa.comps[at]);
                  for (int b = 0; b < births; b++)
                  {
                     const Draw& dr = draws[(size_t)b];
                     const int si = b; // row in sa
                     const int p = a.count++;
                     for (size_t at = 0; at < a.data.size(); at++)
                     {
                        float dv[4];
                        DefaultValue(a.names[at], dv);
                        const int cc = a.comps[at];
                        const int sidx = inherit ? sa.Index(a.names[at]) : (a.names[at] == attr::P ? 0 : -1);
                        for (int k = 0; k < cc; k++)
                        {
                           float x = dv[k];
                           if (sidx >= 0 && k < sa.comps[(size_t)sidx]) x = sa.data[(size_t)sidx][(size_t)si * sa.comps[(size_t)sidx] + k];
                           a.data[at].push_back(x);
                        }
                     }
                     float* pp = a.At(a.Index(attr::P), p);
                     const float* sp = sa.Pos(si);
                     for (int k = 0; k < 3; k++) pp[k] = sp[k] + (dr.jit[k] * 2.0f - 1.0f) * jitter;
                     float* vv = a.At(a.Index(attr::PartVel), p);
                     float sn[3] = { 0.0f, 0.0f, 1.0f }; // N's default
                     if (saN >= 0)
                        for (int k = 0; k < 3 && k < sa.comps[(size_t)saN]; k++) sn[k] = sa.At(saN, si)[k];
                     for (int k = 0; k < 3; k++) vv[k] = vel[k] + sn[k] * speedN + (dr.vr[k] * 2.0f - 1.0f) * velRand;
                     *a.At(a.Index(attr::PartAge), p) = 0.0f;
                     *a.At(a.Index(attr::PartLifeSpan), p) = life * (1.0f + (dr.lr * 2.0f - 1.0f) * var);
                     *a.At(a.Index(attr::PartId), p) = s.nextId;
                     s.nextId += 1.0f;
                     *a.At(a.Index(attr::PartMass), p) = mass;
                     *a.At(a.Index(attr::PartDrag), p) = drag;
                     float* ff = a.At(a.Index(attr::PartForce), p);
                     ff[0] = ff[1] = ff[2] = 0.0f;
                  }
               }
            }
            (void)iId; (void)iM; (void)iD;
            // Queue next frame's copy of the source.
            if (src != nullptr && !srcOnCpu && src->pointCount > 0 && c.Bl("play"))
            {
               std::vector<std::string> names = { attr::P, attr::N };
               if (inheritAll)
                  for (const PopAttrib& at : src->attribs)
                     if (at.name != attr::P && at.name != attr::N)
                        names.push_back(at.name);
               s.async->Request(*src, names);
            }

            // Turbo 0.61: size and colour over life (t = age / life span). PScale
            // at birth is kept in PartSize0 so the curve scales what the source
            // gave each particle (1 when it had none); off = left untouched.
            const float sizeBirth = c.F("sizeBirth"), sizeDeath = c.F("sizeDeath"), sizeCurve = c.F("sizeCurve");
            const bool sizeLife = sizeBirth != 1.0f || sizeDeath != 1.0f;
            const bool colorLife = c.Bl("colorLife");
            if ((sizeLife || colorLife) && a.count > 0)
            {
               const int jAge = a.Index(attr::PartAge), jLife = a.Index(attr::PartLifeSpan);
               int jS = -1, jS0 = -1, jC = -1;
               if (sizeLife)
               {
                  const bool hadScale = a.Index(attr::PScale) >= 0;
                  jS = a.Ensure(attr::PScale, 1);
                  const bool hadBase = a.Index("PartSize0") >= 0;
                  jS0 = a.Ensure("PartSize0", 1);
                  if (!hadBase)
                     for (int i = 0; i < a.count; i++)
                        *a.At(jS0, i) = hadScale ? *a.At(jS, i) : 1.0f;
               }
               float cb[3] = { 1, 1, 1 }, cd[3] = { 1, 1, 1 };
               if (colorLife)
               {
                  c.V3("colorBirth", cb);
                  c.V3("colorDeath", cd);
                  jC = a.Ensure(attr::Color, 4);
               }
               for (int i = 0; i < a.count; i++)
               {
                  const float life = *a.At(jLife, i);
                  const float t = life > 0.0f ? std::clamp(*a.At(jAge, i) / life, 0.0f, 1.0f) : 0.0f;
                  if (sizeLife)
                  {
                     // A particle born this frame has no base yet: its PScale is
                     // still what it inherited from the source (or the default).
                     float& base = *a.At(jS0, i);
                     if (base <= 0.0f)
                        base = *a.At(jS, i) > 0.0f ? *a.At(jS, i) : 1.0f;
                     const float k = std::pow(t, sizeCurve);
                     *a.At(jS, i) = base * (sizeBirth + (sizeDeath - sizeBirth) * k);
                  }
                  if (colorLife)
                  {
                     float* col = a.At(jC, i);
                     for (int q = 0; q < 3; q++)
                        col[q] = cb[q] + (cd[q] - cb[q]) * t;
                     col[3] = 1.0f;
                  }
               }
            }
            Arrays out = a;
            c.out = PointsOnly(std::move(out));
            return true;
         };
         defs.push_back(d);
      }

      // ---- Feedback ----------------------------------------------------------------
      {
         PopDef d = Cpu("Feedback PointZ", "Starts a loop: outputs its input once (and on reset), then every frame the output of the "
                                        "Null PointZ tagged 'target' from the frame before - so the PointZ nodes in between act again on "
                                        "their own result (growth, erosion, trails of change).", 1);
         d.everyFrame = true;
         d.params = { T("target", "target (Null PointZ tag)", "loop"), B("play", "play", true) };
         d.cook = [](CookCtx& c) {
            FrameState& s = c.State<FrameState>();
            const PopData* back = (c.findTagged && !c.S("target").empty()) ? c.findTagged(c.S("target")) : nullptr;
            if (c.reset || s.frames.empty() || back == nullptr)
            {
               if (back == nullptr && !c.S("target").empty())
                  c.warning = "no Null PointZ tagged '" + c.S("target") + "' yet: passing the input";
               s.frames.clear();
               if (!PassInput(c))
                  return true;
               s.frames.push_front(c.out);
               return true;
            }
            if (!c.Bl("play"))
            {
               c.out = ShallowCopy(*s.frames.front());
               return true;
            }
            c.out = ShallowCopy(*back);
            s.frames.clear();
            s.frames.push_front(c.out);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Trail -------------------------------------------------------------------
      {
         PopDef d = Cpu("Trail PointZ", "Draws where each point has been: keeps the last 'length' frames and outputs one line per point "
                                     "(matched by PartId, or by index), newest first, with TrailAge 0..1 along it.", 1);
         d.everyFrame = true;
         d.params = { I("length", "length (frames)", 30, 2, 1000), I("every", "sample every", 1, 1, 60),
                      M("match", "match points by", { "PartId (else index)", "index" }, 0), I("maxPoints", "point limit", 2000000, 1000, 20000000) };
         d.cook = [](CookCtx& c) {
            TrailState& s = c.State<TrailState>();
            if (c.reset) s = TrailState();
            const PopData* in = c.Input(0);
            if (in == nullptr) return true;
            const bool sample = (s.frame++ % std::max(1, c.I("every"))) == 0;
            const bool byIndex = c.I("match") == 1 || !in->Has(attr::PartId);
            if (byIndex)
            {
               // GPU path: nothing is downloaded, and the line topology and
               // TrailAge are rebuilt only when the point count or length changes.
               s.snaps.clear();
               const int N = in->pointCount;
               const int L = std::max(2, c.I("length"));
               if ((long long)N * L > (long long)c.I("maxPoints"))
               {
                  c.error = "would make " + std::to_string((long long)N * L) + " points, over the point limit";
                  return false;
               }
               if (N <= 0)
               {
                  c.out = std::make_shared<PopData>();
                  return true;
               }
               const bool shapeChanged = N != s.histN || L != s.histL;
               if (!sample && !shapeChanged && s.last)
               {
                  c.out = ShallowCopy(*s.last);
                  return true;
               }
               if (shapeChanged)
               {
                  s.histP.reset();
                  s.histC.reset();
                  s.histN = N;
                  s.histL = L;
                  auto topo = NewTopo();
                  std::vector<uint32_t> strip((size_t)L);
                  for (int i = 0; i < N; i++)
                  {
                     for (int f = 0; f < L; f++)
                        strip[(size_t)f] = (uint32_t)((size_t)f * (size_t)N + (size_t)i);
                     topo->AddStrip(strip, false);
                  }
                  s.topo = topo;
                  std::vector<float> age((size_t)N * (size_t)L);
                  for (int f = 0; f < L; f++)
                     std::fill_n(age.begin() + (std::ptrdiff_t)((size_t)f * (size_t)N), N, (float)f / (float)(L - 1));
                  s.age = MakeBuffer(std::move(age), 1);
               }
               auto attrib = [&](const char* name, int comps) -> BufferPtr {
                  if (const PopAttrib* a = in->Find(name))
                     if (a->buffer->Comps() == comps && a->buffer->Count() >= N)
                        return a->buffer;
                  return MakeBuffer(in->Read(name, comps), comps);
               };
               s.histP = GpuShiftIn(attrib(attr::P, 3), s.histP, N * 3, L, 3);
               s.histC = GpuShiftIn(attrib(attr::Color, 4), s.histC, N * 4, L, 4);
               auto out = std::make_shared<PopData>();
               out->pointCount = N * L;
               out->Set(attr::P, s.histP);
               out->Set(attr::Color, s.histC);
               out->Set("TrailAge", s.age);
               out->topo = s.topo;
               s.last = out;
               c.out = ShallowCopy(*out);
               return true;
            }
            s.histN = s.histL = -1;
            s.last.reset();
            if (sample)
            {
               TrailState::Snap snap;
               snap.P = in->Read(attr::P, 3);
               snap.C = in->Read(attr::Color, 4);
               if (c.I("match") == 0 && in->Has(attr::PartId)) snap.id = in->Read(attr::PartId, 1);
               else { snap.id.resize((size_t)in->pointCount); std::iota(snap.id.begin(), snap.id.end(), 0.0f); }
               s.snaps.push_front(std::move(snap));
               while ((int)s.snaps.size() > std::max(2, c.I("length"))) s.snaps.pop_back();
            }
            std::vector<std::unordered_map<long long, int>> index(s.snaps.size());
            for (size_t f = 1; f < s.snaps.size(); f++)
               for (size_t i = 0; i < s.snaps[f].id.size(); i++) index[f][(long long)s.snaps[f].id[i]] = (int)i;
            std::vector<float> P, C, age;
            auto topo = NewTopo();
            const TrailState::Snap& now = s.snaps.front();
            const int L = (int)s.snaps.size();
            for (size_t i = 0; i < now.id.size(); i++)
            {
               std::vector<uint32_t> strip;
               const long long id = (long long)now.id[i];
               for (int f = 0; f < L; f++)
               {
                  int j = (int)i;
                  if (f > 0) { auto it = index[(size_t)f].find(id); if (it == index[(size_t)f].end()) break; j = it->second; }
                  const TrailState::Snap& sn = s.snaps[(size_t)f];
                  strip.push_back((uint32_t)(P.size() / 3));
                  P.insert(P.end(), { sn.P[(size_t)j * 3], sn.P[(size_t)j * 3 + 1], sn.P[(size_t)j * 3 + 2] });
                  C.insert(C.end(), { sn.C[(size_t)j * 4], sn.C[(size_t)j * 4 + 1], sn.C[(size_t)j * 4 + 2], sn.C[(size_t)j * 4 + 3] });
                  age.push_back(L > 1 ? (float)f / (L - 1) : 0.0f);
               }
               if (strip.size() >= 2) topo->AddStrip(strip, false);
               if ((int)(P.size() / 3) > c.I("maxPoints")) { c.warning = "point limit reached"; break; }
            }
            c.out = FromPoints(std::move(P), false);
            c.out->Set(attr::Color, MakeBuffer(std::move(C), 4));
            c.out->Set("TrailAge", MakeBuffer(std::move(age), 1));
            c.out->topo = topo;
            return true;
         };
         defs.push_back(d);
      }

      // ---- Cache / Cache Select / Cache Blend ------------------------------------------
      {
         PopDef d = Cpu("Cache PointZ", "Remembers the last 'frames' cooks of its input (on the GPU, no copies) and outputs the one "
                                     "'delay' frames back. Cache Select and Cache Blend read its frames by its 'tag'.", 1);
         d.everyFrame = true;
         d.params = { I("frames", "frames", 30, 1, 1000), I("delay", "output delay", 0, 0, 999), B("active", "record", true), T("tag", "tag", "cache") };
         d.cook = [](CookCtx& c) {
            FrameState& s = c.State<FrameState>();
            if (c.reset) s.frames.clear();
            const PopData* in = c.Input(0);
            if (in != nullptr && c.Bl("active"))
            {
               auto copy = ShallowCopy(*in);
               copy->revision = in->revision;
               if (s.frames.empty() || s.frames.front()->revision != in->revision)
                  s.frames.push_front(copy);
               while ((int)s.frames.size() > std::max(1, c.I("frames"))) s.frames.pop_back();
            }
            if (s.frames.empty())
               return PassInput(c), true;
            const int i = std::max(0, std::min((int)s.frames.size() - 1, c.I("delay")));
            c.out = ShallowCopy(*s.frames[(size_t)i]);
            return true;
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("Cache Select PointZ", "Outputs one frame of the Cache PointZ tagged 'target': 'index' frames back from the newest.", 0);
         d.everyFrame = true;
         d.params = { T("target", "Cache PointZ tag", "cache"), I("index", "index", 0, 0, 999) };
         d.cook = [](CookCtx& c) {
            OpState* st = c.findTaggedState ? c.findTaggedState(c.S("target")) : nullptr;
            auto* fs = dynamic_cast<FrameState*>(st);
            if (fs == nullptr || fs->frames.empty())
            {
               c.warning = "no Cache PointZ tagged '" + c.S("target") + "' (or it is empty)";
               return true;
            }
            const int i = std::max(0, std::min((int)fs->frames.size() - 1, c.I("index")));
            c.out = ShallowCopy(*fs->frames[(size_t)i]);
            return true;
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("Cache Blend PointZ", "Blends between two frames of the Cache PointZ tagged 'target' at a fractional 'position' "
                                           "(frames back): smooth slow motion and time-smear of P and Color when point counts match.", 0);
         d.everyFrame = true;
         d.params = { T("target", "Cache PointZ tag", "cache"), F("pos", "position", 0.0f, 0.0f, 999.0f), T("attribs", "attributes", "P,Color") };
         d.cook = [](CookCtx& c) {
            auto* fs = dynamic_cast<FrameState*>(c.findTaggedState ? c.findTaggedState(c.S("target")) : nullptr);
            if (fs == nullptr || fs->frames.empty())
            {
               c.warning = "no Cache PointZ tagged '" + c.S("target") + "' (or it is empty)";
               return true;
            }
            const float pos = std::max(0.0f, std::min((float)fs->frames.size() - 1.0f, c.F("pos")));
            const int i0 = (int)pos, i1 = std::min((int)fs->frames.size() - 1, i0 + 1);
            const float t = pos - (float)i0;
            const PopData& a = *fs->frames[(size_t)i0];
            const PopData& b = *fs->frames[(size_t)i1];
            c.out = ShallowCopy(a);
            if (a.pointCount != b.pointCount || t <= 0.0f)
               return true;
            for (const std::string& name : SplitList(c.S("attribs")))
            {
               if (!a.Has(name) || !b.Has(name)) continue;
               const int comps = a.CompsOf(name);
               std::vector<float> va = a.Read(name, comps);
               const std::vector<float> vb = b.Read(name, comps);
               for (size_t k = 0; k < va.size(); k++) va[k] += (vb[k] - va[k]) * t;
               c.out->Set(name, MakeBuffer(std::move(va), comps));
            }
            return true;
         };
         defs.push_back(d);
      }

      // ---- GLSL Create / GLSL Copy / GLSL Advanced --------------------------------------
      {
         PopDef d = Cpu("GLSL Create PointZ", "Makes points from nothing with your GLSL: 'points' of them (or a rows x columns grid), "
                                           "as points, one line, or grid faces. Set P, Color and any listed outputs per id.", 0);
         d.params = { M("layout", "layout", { "points", "one line", "grid faces" }, 0), I("count", "points", 1000, 1, 4000000, "layout=0|1"),
                      I("rows", "rows", 64, 2, 4096, "layout=2"), I("cols", "columns", 64, 2, 4096, "layout=2"), T("outputs", "outputs", "P,Color"),
                      T("code", "code", "float u = float(id) / float(uCount);\nP = vec3(cos(u * TAU * 7.0), sin(u * TAU * 5.0), sin(u * TAU * 3.0 + uTime)) * 0.5;\nColor = vec4(u, 1.0 - u, 0.5, 1.0);"),
                      F("a", "uA", 0.5f, 0.0f, 1.0f), F("b", "uB", 0.5f, 0.0f, 1.0f), B("animate", "animate", true) };
         d.animKey = "animate";
         d.cook = [](CookCtx& c) {
            const int layout = c.I("layout");
            const int rows = std::max(2, c.I("rows")), cols = std::max(2, c.I("cols"));
            const int n = layout == 2 ? rows * cols : std::max(1, c.I("count"));
            auto base = FromPoints(std::vector<float>((size_t)n * 3, 0.0f), layout == 0);
            auto topo = NewTopo();
            topo->pointPrims = layout == 0;
            if (layout == 1)
            {
               std::vector<uint32_t> s((size_t)n);
               std::iota(s.begin(), s.end(), 0u);
               if (n >= 2) topo->AddStrip(s, false);
            }
            else if (layout == 2)
            {
               for (int j = 0; j + 1 < rows; j++)
                  for (int i = 0; i + 1 < cols; i++)
                  {
                     const uint32_t a = (uint32_t)(j * cols + i);
                     topo->tris.insert(topo->tris.end(), { a, a + 1, a + (uint32_t)cols + 1, a, a + (uint32_t)cols + 1, a + (uint32_t)cols });
                  }
               base->dims[0] = rows;
               base->dims[1] = cols;
            }
            base->topo = topo;
            c.out = base;
            KernelSpec k;
            CookCtx probe = c;
            probe.inCount = 1;
            probe.in[0] = base;
            if (!ParseWritesPublic(probe, c.S("outputs"), k.writes))
            {
               c.error = probe.error;
               return false;
            }
            k.reads = { { attr::P, 3 } };
            k.body = "   int col = id % " + std::to_string(cols) + ", row = id / " + std::to_string(cols) + ";\n" + c.S("code");
            return c.Kernel(k, *base, { Flt("uA", c.F("a")), Flt("uB", c.F("b")) });
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("GLSL Copy PointZ", "Copies the input 'copies' times and lets your GLSL place each copy: copyId (0..uCopies-1) "
                                         "and the point's own values are in scope; change P, Color and any listed outputs.", 1);
         d.params = { I("copies", "copies", 16, 1, 100000), T("outputs", "outputs", "P"),
                      T("code", "code", "float u = float(copyId) / float(uCopies);\nP = rotY(u * TAU) * (P * 0.3 + vec3(1.0, 0.0, 0.0));"),
                      F("a", "uA", 0.5f, 0.0f, 1.0f), F("b", "uB", 0.5f, 0.0f, 1.0f), B("animate", "animate", false) };
         d.animKey = "animate";
         d.cook = [](CookCtx& c) {
            const PopData* in = c.Input(0);
            if (in == nullptr) return true;
            const int copies = std::max(1, c.I("copies"));
            if ((long long)copies * in->pointCount > 20000000LL)
            {
               c.error = "too many points";
               return false;
            }
            auto topo = ReplicateTopo(in->topo, copies, in->pointCount, &c.State<ReplicaTopoCache>());
            auto big = Store(Replicate(Load(*in), copies), topo);
            c.out = big;
            CookCtx probe = c;
            probe.in[0] = big;
            KernelSpec k;
            if (!ParseWritesPublic(probe, c.S("outputs"), k.writes))
            {
               c.error = probe.error;
               return false;
            }
            for (const PopAttrib& at : big->attribs)
               if (k.reads.size() < 12 && ValidAttribName(at.name)) k.reads.push_back({ at.name, at.buffer->Comps() });
            k.body = "   int copyId = id / uPer;\n" + c.S("code");
            return c.Kernel(k, *big, { Int("uPer", std::max(1, in->pointCount)), Int("uCopies", copies), Flt("uA", c.F("a")), Flt("uB", c.F("b")) });
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("GLSL Advanced PointZ", "GLSL over the first input with random access to every attribute of the second: "
                                             "fetch_<name>(i) reads point i of input B (uCountB points), plus the image pin and "
                                             "knobs uA..uD. For nearest-point tricks, morphs and custom simulations.", 2);
         d.inputLabels = { "A", "B" };
         d.imageInput = true;
         d.params = { T("outputs", "outputs", "P"), T("code", "code", "P = mix(P, fetch_P(id % uCountB), uA);"),
                      F("a", "uA", 0.5f, 0.0f, 1.0f), F("b", "uB", 0.5f, 0.0f, 1.0f), F("c", "uC", 0.5f, 0.0f, 1.0f), F("d", "uD", 0.5f, 0.0f, 1.0f),
                      B("animate", "animate", true) };
         d.animKey = "animate";
         d.cook = [](CookCtx& c) {
            if (!PassInput(c)) return true;
            KernelSpec k;
            if (!ParseWritesPublic(c, c.S("outputs"), k.writes)) return false;
            for (const PopAttrib& at : c.Input(0)->attribs)
               if (k.reads.size() < 11 && ValidAttribName(at.name)) k.reads.push_back({ at.name, at.buffer->Comps() });
            int countB = 0;
            if (const PopData* b = c.Input(1))
            {
               countB = b->pointCount;
               for (const PopAttrib& at : b->attribs)
                  if (k.lookups.size() < 8 && ValidAttribName(at.name)) k.lookups.push_back({ at.name, at.buffer->Comps() });
            }
            if (k.lookups.empty())
               k.lookups.push_back({ attr::P, 3 });
            k.lookupInput = 1;
            k.usesImage = true;
            k.body = c.S("code");
            return c.Kernel(k, *c.Input(0), { Int("uCountB", std::max(1, countB)), Flt("uA", c.F("a")), Flt("uB", c.F("b")),
                                             Flt("uC", c.F("c")), Flt("uD", c.F("d")) });
         };
         defs.push_back(d);
      }

      // ---- Files -----------------------------------------------------------------------
      {
         PopDef d = Cpu("Point File In PointZ", "Reads a point cloud: PLY (ascii or binary, with colours, normals and any extra "
                                             "per-point values), or a text table (.xyz, .pts, .csv, .txt: x y z [r g b]).", 0);
         d.params = { T("path", "file", ""), F("scale", "scale", 1.0f, 0.0001f, 1000.0f), I("every", "keep every", 1, 1, 1000),
                      B("yUp", "Z-up file (swap to Y-up)", false) };
         d.cook = [](CookCtx& c) {
            const std::string path = c.S("path");
            if (path.empty()) { c.warning = "no file"; return true; }
            std::string err;
            const std::string bytes = ReadFileText(path, err);
            if (!err.empty()) { c.error = err; return false; }
            std::shared_ptr<PopData> data;
            if (Ext(path) == "ply")
            {
               if (!ReadPly(bytes, data, false, err)) { c.error = err; return false; }
               auto t = NewTopo();
               t->pointPrims = true;
               data->topo = t;
            }
            else
               data = PointsOnly(ReadTable(bytes, "P:3,Color:3", c.warning));
            Arrays a = Load(*data);
            const int every = std::max(1, c.I("every"));
            const float sc = c.F("scale");
            PointBuilder b(a);
            for (int i = 0; i < a.count; i += every)
            {
               const int p = b.Copy(i);
               const float* q = a.Pos(i);
               if (c.Bl("yUp")) b.SetPos(p, q[0] * sc, q[2] * sc, -q[1] * sc);
               else b.SetPos(p, q[0] * sc, q[1] * sc, q[2] * sc);
            }
            c.out = PointsOnly(b.Take());
            return true;
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("File In PointZ", "Reads geometry with its faces and lines: OBJ (v / vt / vn / f / l) or PLY with faces.", 0);
         d.params = { T("path", "file", ""), F("scale", "scale", 1.0f, 0.0001f, 1000.0f) };
         d.cook = [](CookCtx& c) {
            const std::string path = c.S("path");
            if (path.empty()) { c.warning = "no file"; return true; }
            std::string err;
            const std::string bytes = ReadFileText(path, err);
            if (!err.empty()) { c.error = err; return false; }
            std::shared_ptr<PopData> data;
            const std::string ext = Ext(path);
            const bool ok = ext == "ply" ? ReadPly(bytes, data, true, err) : ReadObj(bytes, data, err);
            if (!ok) { c.error = err; return false; }
            const float sc = c.F("scale");
            if (sc != 1.0f)
            {
               std::vector<float> P = data->Read(attr::P, 3);
               for (float& x : P) x *= sc;
               data->Set(attr::P, MakeBuffer(std::move(P), 3));
            }
            c.out = data;
            return true;
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("Table to PointZ", "Points from a table of numbers typed in (or pasted): one point per line, columns mapped by "
                                      "'columns' (name:components, e.g. P:3,Color:4,PScale:1). Optionally joined into a line.", 0);
         d.params = { T("table", "table", "0 0 0\n1 0 0\n1 1 0\n0 1 0"), T("columns", "columns", "P:3"),
                      M("as", "as", { "points", "line", "closed line" }, 0), T("path", "or read file", "") };
         d.cook = [](CookCtx& c) {
            std::string text = c.S("table");
            if (!c.S("path").empty())
            {
               std::string err;
               text = ReadFileText(c.S("path"), err);
               if (!err.empty()) { c.error = err; return false; }
            }
            Arrays a = ReadTable(text, c.S("columns"), c.warning);
            const int n = a.count;
            auto topo = NewTopo();
            if (c.I("as") == 0) topo->pointPrims = true;
            else if (n >= 2)
            {
               std::vector<uint32_t> s((size_t)n);
               std::iota(s.begin(), s.end(), 0u);
               topo->AddStrip(s, c.I("as") == 2);
            }
            c.out = Store(std::move(a), topo);
            return true;
         };
         defs.push_back(d);
      }
      {
         PopDef d = Cpu("File Out PointZ", "Writes its input to a file - PLY (binary or ascii), OBJ or CSV of every attribute - when "
                                        "'write' is switched on (once per switch), or on every change with 'auto'. Passes the input on.", 1);
         d.everyFrame = true;
         d.params = { T("path", "file", ""), M("format", "format", { "PLY binary", "PLY ascii", "OBJ", "CSV" }, 0), B("write", "write", false),
                      B("auto", "write on every change", false) };
         d.cook = [](CookCtx& c) {
            FrameState& s = c.State<FrameState>();
            if (!PassInput(c)) return true;
            const bool w = c.Bl("write");
            const bool edge = w && !s.lastWrite;
            s.lastWrite = w;
            const bool changed = c.Input(0)->revision != s.written;
            if ((edge || (c.Bl("auto") && changed)) && !c.S("path").empty())
            {
               std::string err;
               if (!WriteFile(*c.Input(0), c.S("path"), c.I("format"), err)) c.warning = err;
               s.written = c.Input(0)->revision;
            }
            return true;
         };
         defs.push_back(d);
      }

      // ---- Instance PointZ -------------------------------------------------------------------
      {
         PopDef d = Cpu("Instance PointZ", "Geometry instancing (TouchDesigner's Geometry COMP idea): draws the shape on the second "
                                        "input once at every point of the first, on the GPU, in one draw call - millions are fine. "
                                        "Each instance reads the point's P (translate), N (aim), PScale (uniform scale), Scale "
                                        "(per-axis scale), Rotate (degrees), Color and Active (0 hides it); rename any of them "
                                        "below. Instance textures: plug up to four images and pick one per instance by its "
                                        "TexIndex, by instance number or at random, or cut one image (or the material's "
                                        "texture) into an atlas grid of cells.", 2);
         d.inputLabels = { "points", "shape" };
         d.imageInput = true;
         d.imageCount = 4;
         d.imageLabels = { "texture 0", "texture 1", "texture 2", "texture 3" };
         d.params = { F("scale", "scale", 0.05f, 0.0f, 10.0f), V("scaleXYZ", "scale X / Y / Z", 1, 1, 1, 0.0f, 10.0f),
                      V("rotOffset", "rotate (added, degrees)", 0, 0, 0, -360.0f, 360.0f),
                      V("offset", "translate (added)", 0, 0, 0, -10.0f, 10.0f),
                      M("align", "aim along N", { "off", "+Z to N", "+Y to N" }, 0),
                      T("posAttr", "translate attribute", "P"),
                      T("scaleAttr", "scale attribute", "PScale"), T("scale3Attr", "scale XYZ attribute", "Scale"),
                      T("rotAttr", "rotate attribute", "Rotate"),
                      T("colorAttr", "colour attribute", "Color"), T("activeAttr", "active attribute", "Active"),
                      T("normalAttr", "aim attribute", "N"), B("useColor", "colour instances", true),
                      M("texMode", "instance texture", { "material texture", "pick image by index", "atlas grid" }, 0),
                      M("texFrom", "texture index from", { "TexIndex attribute", "instance number", "random" }, 0, "texMode=1|2"),
                      T("texAttr", "texture index attribute", "TexIndex", "texMode=1|2"),
                      I("atlasCols", "atlas columns", 4, 1, 64, "texMode=2"), I("atlasRows", "atlas rows", 4, 1, 64, "texMode=2") };
         d.cook = [](CookCtx& c) {
            const PopData* pts = c.Input(0);
            const PopData* shape = c.Input(1);
            if (shape == nullptr)
            {
               c.warning = "needs a shape on the second input";
               return true;
            }
            c.out = ShallowCopy(*shape);
            if (pts == nullptr)
               return true;
            auto inst = std::make_shared<PopData>();
            inst->pointCount = pts->pointCount;
            auto take = [&](const std::string& from, const char* to) {
               if (from.empty())
                  return;
               if (const PopAttrib* a = pts->Find(from)) inst->Set(to, a->buffer);
            };
            take(c.S("posAttr").empty() ? std::string(attr::P) : c.S("posAttr"), attr::P);
            take(c.S("normalAttr"), attr::N);
            take(c.S("scaleAttr"), attr::PScale);
            take(c.S("scale3Attr"), attr::Scale);
            take(c.S("rotAttr"), attr::Rotate);
            take(c.S("activeAttr"), attr::Active);
            if (c.Bl("useColor"))
               take(c.S("colorAttr"), attr::Color);
            PopData::InstanceLook& look = inst->instanceLook;
            c.V3("scaleXYZ", look.scaleXYZ);
            c.V3("rotOffset", look.rotate);
            c.V3("offset", look.offset);
            look.texMode = std::clamp(c.I("texMode"), 0, 2);
            look.texFrom = std::clamp(c.I("texFrom"), 0, 2);
            look.atlasCols = std::max(1, c.I("atlasCols"));
            look.atlasRows = std::max(1, c.I("atlasRows"));
            look.texCount = 0;
            for (int k = 0; k < 4; k++)
               if (c.images[k] != 0)
                  look.tex[look.texCount++] = c.images[k];
            if (look.texMode != 0)
            {
               take(c.S("texAttr"), attr::TexIndex);
               if (look.texMode == 1 && look.texCount == 0)
                  c.warning = "'pick image by index' needs images on the texture pins";
               if (look.texFrom == 0 && !inst->Has(attr::TexIndex))
                  c.warning = "no '" + c.S("texAttr") + "' attribute on the points: every instance uses texture 0";
            }
            inst->topo = NewTopo();
            inst->instanceScale = c.F("scale");
            inst->instanceAlign = c.I("align");
            c.instances = inst;
            return true;
         };
         defs.push_back(d);
      }
   }
}
