#include "BakeNode.h"

#include "gl3.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <thread>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kModeNames = { "AO", "Curvature", "Position", "Normal", "Thickness" };
   const std::vector<std::string> kSizeNames = { "256", "512", "1024", "2048" };

   struct V3
   {
      float x = 0, y = 0, z = 0;
   };
   V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
   V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
   V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
   V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
   float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   V3 Norm(V3 a)
   {
      const float l = std::sqrt(Dot(a, a));
      return l > 1e-12f ? a * (1.0f / l) : V3{ 0, 1, 0 };
   }
   V3 Pos(const Vertex& v) { return { v.px, v.py, v.pz }; }
   V3 Nrm(const Vertex& v) { return { v.nx, v.ny, v.nz }; }

   // A plain median-split BVH over triangles: enough for the few thousand
   // to few hundred thousand triangles a bake sees.
   struct Bvh
   {
      struct Node
      {
         V3 lo, hi;
         int left = -1, right = -1, first = 0, count = 0;
      };
      std::vector<Node> nodes;
      std::vector<int> tris;
      std::vector<V3> a, e1, e2;

      void Build(const Mesh& m)
      {
         const int n = (int)m.FaceCount();
         a.resize(n); e1.resize(n); e2.resize(n); tris.resize(n);
         std::vector<V3> centre(n);
         for (int f = 0; f < n; f++)
         {
            const V3 p0 = Pos(m.vertices[m.indices[f * 3]]);
            const V3 p1 = Pos(m.vertices[m.indices[f * 3 + 1]]);
            const V3 p2 = Pos(m.vertices[m.indices[f * 3 + 2]]);
            a[f] = p0; e1[f] = p1 - p0; e2[f] = p2 - p0;
            centre[f] = (p0 + p1 + p2) * (1.0f / 3.0f);
            tris[f] = f;
         }
         nodes.clear();
         nodes.reserve(n * 2 + 1);
         Split(0, n, centre);
      }

      int Split(int first, int count, const std::vector<V3>& centre)
      {
         Node node;
         node.lo = { 1e30f, 1e30f, 1e30f };
         node.hi = { -1e30f, -1e30f, -1e30f };
         for (int i = first; i < first + count; i++)
         {
            const int f = tris[i];
            for (const V3 p : { a[f], a[f] + e1[f], a[f] + e2[f] })
            {
               node.lo = { std::min(node.lo.x, p.x), std::min(node.lo.y, p.y), std::min(node.lo.z, p.z) };
               node.hi = { std::max(node.hi.x, p.x), std::max(node.hi.y, p.y), std::max(node.hi.z, p.z) };
            }
         }
         const int index = (int)nodes.size();
         nodes.push_back(node);
         if (count <= 4)
         {
            nodes[index].first = first;
            nodes[index].count = count;
            return index;
         }
         const V3 ext = node.hi - node.lo;
         const int axis = (ext.x > ext.y && ext.x > ext.z) ? 0 : (ext.y > ext.z ? 1 : 2);
         auto key = [&](int f) { return axis == 0 ? centre[f].x : axis == 1 ? centre[f].y : centre[f].z; };
         const int mid = first + count / 2;
         std::nth_element(tris.begin() + first, tris.begin() + mid, tris.begin() + first + count,
                          [&](int l, int r) { return key(l) < key(r); });
         const int left = Split(first, mid - first, centre);
         const int right = Split(mid, first + count - mid, centre);
         nodes[index].left = left;
         nodes[index].right = right;
         return index;
      }

      static bool HitBox(const Node& n, V3 o, V3 inv, float tMax)
      {
         float t0 = 0.0f, t1 = tMax;
         const float lo[3] = { n.lo.x, n.lo.y, n.lo.z }, hi[3] = { n.hi.x, n.hi.y, n.hi.z };
         const float oo[3] = { o.x, o.y, o.z }, ii[3] = { inv.x, inv.y, inv.z };
         for (int k = 0; k < 3; k++)
         {
            float ta = (lo[k] - oo[k]) * ii[k], tb = (hi[k] - oo[k]) * ii[k];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1)
               return false;
         }
         return true;
      }

      // Nearest hit distance below tMax, or tMax. anyHit stops at the first.
      float Cast(V3 o, V3 d, float tMax, bool anyHit) const
      {
         if (nodes.empty())
            return tMax;
         const V3 inv = { 1.0f / (std::fabs(d.x) > 1e-12f ? d.x : 1e-12f),
                          1.0f / (std::fabs(d.y) > 1e-12f ? d.y : 1e-12f),
                          1.0f / (std::fabs(d.z) > 1e-12f ? d.z : 1e-12f) };
         int stack[64];
         int sp = 0;
         stack[sp++] = 0;
         float best = tMax;
         while (sp > 0)
         {
            const Node& n = nodes[stack[--sp]];
            if (!HitBox(n, o, inv, best))
               continue;
            if (n.left < 0)
            {
               for (int i = n.first; i < n.first + n.count; i++)
               {
                  const int f = tris[i];
                  // Moller-Trumbore, both sides.
                  const V3 p = Cross(d, e2[f]);
                  const float det = Dot(e1[f], p);
                  if (std::fabs(det) < 1e-12f)
                     continue;
                  const float id = 1.0f / det;
                  const V3 s = o - a[f];
                  const float u = Dot(s, p) * id;
                  if (u < 0.0f || u > 1.0f)
                     continue;
                  const V3 q = Cross(s, e1[f]);
                  const float v = Dot(d, q) * id;
                  if (v < 0.0f || u + v > 1.0f)
                     continue;
                  const float t = Dot(e2[f], q) * id;
                  if (t > 0.0f && t < best)
                  {
                     best = t;
                     if (anyHit)
                        return best;
                  }
               }
            }
            else if (sp < 62)
            {
               stack[sp++] = n.left;
               stack[sp++] = n.right;
            }
         }
         return best;
      }
   };

   // Low-discrepancy cosine-weighted directions about n, turned per texel so
   // neighbouring texels do not band.
   V3 HemiDir(V3 n, int i, int count, float turn)
   {
      const float u1 = ((float)i + 0.5f) / (float)count;
      unsigned b = (unsigned)i;
      b = (b << 16u) | (b >> 16u);
      b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
      b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
      b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
      b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
      const float u2 = std::fmod((float)b * 2.3283064e-10f + turn, 1.0f);
      const float r = std::sqrt(u1), phi = 6.2831853f * u2;
      const V3 up = std::fabs(n.y) < 0.99f ? V3{ 0, 1, 0 } : V3{ 1, 0, 0 };
      const V3 t = Norm(Cross(up, n)), bt = Cross(n, t);
      return Norm(t * (r * std::cos(phi)) + bt * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - u1)));
   }

   // Signed mean curvature per vertex from its welded neighbours: positive
   // on ridges, negative in creases, in 1/mesh-size units.
   std::vector<float> VertexCurvature(const Mesh& m, float size)
   {
      std::map<std::array<long long, 3>, int> ids;
      std::vector<int> weld(m.vertices.size());
      for (size_t i = 0; i < m.vertices.size(); i++)
      {
         const Vertex& v = m.vertices[i];
         weld[i] = ids.emplace(std::array<long long, 3>{ std::llround(v.px * 1e5), std::llround(v.py * 1e5),
                                                        std::llround(v.pz * 1e5) }, (int)ids.size()).first->second;
      }
      const size_t w = ids.size();
      std::vector<V3> wp(w), wn(w);
      for (size_t i = 0; i < m.vertices.size(); i++)
      {
         wp[weld[i]] = Pos(m.vertices[i]);
         wn[weld[i]] = wn[weld[i]] + Nrm(m.vertices[i]);
      }
      for (V3& n : wn)
         n = Norm(n);
      std::vector<float> sum(w, 0.0f), cnt(w, 0.0f);
      for (size_t f = 0; f < m.FaceCount(); f++)
         for (int k = 0; k < 3; k++)
         {
            const int i = weld[m.indices[f * 3 + k]], j = weld[m.indices[f * 3 + (k + 1) % 3]];
            const V3 dp = wp[j] - wp[i];
            const float l2 = Dot(dp, dp);
            if (l2 < 1e-14f)
               continue;
            // Normals spreading apart along the edge = convex.
            const float k2 = Dot(wn[j] - wn[i], dp) / l2;
            sum[i] += k2; cnt[i] += 1.0f;
            sum[j] += k2; cnt[j] += 1.0f;
         }
      std::vector<float> out(m.vertices.size());
      for (size_t i = 0; i < m.vertices.size(); i++)
         out[i] = cnt[weld[i]] > 0.0f ? sum[weld[i]] / cnt[weld[i]] * size : 0.0f;
      return out;
   }

   bool HasUVs(const Mesh& m)
   {
      for (const Vertex& v : m.vertices)
         if (v.u != m.vertices[0].u || v.v != m.vertices[0].v)
            return true;
      return false;
   }
}

const std::vector<std::string>& BakeNode::ModeNames() { return kModeNames; }
const std::vector<std::string>& BakeNode::SizeNames() { return kSizeNames; }

BakeNode::~BakeNode()
{
   if (mJob.valid())
      mJob.wait();
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

std::vector<float> BakeNode::Bake(const Request& r)
{
   const int res = r.res;
   std::vector<float> px((size_t)res * res * 4, 0.0f);
   const Mesh& m = r.mesh;
   if (m.Empty())
      return px;

   V3 lo = Pos(m.vertices[0]), hi = lo;
   for (const Vertex& v : m.vertices)
   {
      lo = { std::min(lo.x, v.px), std::min(lo.y, v.py), std::min(lo.z, v.pz) };
      hi = { std::max(hi.x, v.px), std::max(hi.y, v.py), std::max(hi.z, v.pz) };
   }
   const float size = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-6f });
   const float reach = std::max(r.distance, 0.001f) * size;
   const float eps = size * 1e-4f;
   const int rays = std::clamp(r.samples, 1, 256);

   Bvh bvh;
   if (r.mode == kAO || r.mode == kThickness)
      bvh.Build(m);
   std::vector<float> curv;
   if (r.mode == kCurvature)
      curv = VertexCurvature(m, size);

   // Pass 1 (cheap): which triangle and weights each texel takes. Weights
   // are clamped into the triangle, so a texel past an edge reads the
   // nearest point on the surface, not a point floating off it.
   struct Texel
   {
      unsigned f = 0;
      float w[3] = { 0, 0, 0 };
   };
   std::vector<Texel> texel((size_t)res * res);
   std::vector<unsigned char> filled((size_t)res * res, 0);
   for (size_t f = 0; f < m.FaceCount(); f++)
   {
      const Vertex* v[3] = { &m.vertices[m.indices[f * 3]], &m.vertices[m.indices[f * 3 + 1]],
                             &m.vertices[m.indices[f * 3 + 2]] };
      // Texel-space corners; UVs outside 0-1 wrap, as a texture would.
      float tx[3], ty[3];
      const float wrapU = std::floor(v[0]->u), wrapV = std::floor(v[0]->v);
      for (int k = 0; k < 3; k++)
      {
         tx[k] = (v[k]->u - wrapU) * res;
         ty[k] = (v[k]->v - wrapV) * res;
      }
      const float area = (tx[1] - tx[0]) * (ty[2] - ty[0]) - (tx[2] - tx[0]) * (ty[1] - ty[0]);
      if (std::fabs(area) < 1e-9f)
         continue;
      const int x0 = std::max(0, (int)std::floor(std::min({ tx[0], tx[1], tx[2] })));
      const int x1 = std::min(res - 1, (int)std::ceil(std::max({ tx[0], tx[1], tx[2] })));
      const int y0 = std::max(0, (int)std::floor(std::min({ ty[0], ty[1], ty[2] })));
      const int y1 = std::min(res - 1, (int)std::ceil(std::max({ ty[0], ty[1], ty[2] })));
      // About half a texel of slack past each edge, so texels a seam only
      // grazes still get this triangle's value. Weight k falls to 0 on the
      // edge opposite corner k; one texel off that edge is |edge| / |area|.
      float slack[3];
      for (int k = 0; k < 3; k++)
      {
         const int i = (k + 1) % 3, j = (k + 2) % 3;
         const float len = std::sqrt((tx[j] - tx[i]) * (tx[j] - tx[i]) + (ty[j] - ty[i]) * (ty[j] - ty[i]));
         slack[k] = -0.7f * len / std::fabs(area);
      }
      for (int y = y0; y <= y1; y++)
         for (int x = x0; x <= x1; x++)
         {
            const float sx = x + 0.5f, sy = y + 0.5f;
            float w[3];
            w[0] = ((tx[1] - sx) * (ty[2] - sy) - (tx[2] - sx) * (ty[1] - sy)) / area;
            w[1] = ((tx[2] - sx) * (ty[0] - sy) - (tx[0] - sx) * (ty[2] - sy)) / area;
            w[2] = 1.0f - w[0] - w[1];
            if (w[0] < slack[0] || w[1] < slack[1] || w[2] < slack[2])
               continue;
            const size_t i = (size_t)y * res + x;
            // A texel inside this triangle wins over one only touched by slack.
            const bool inside = w[0] >= 0.0f && w[1] >= 0.0f && w[2] >= 0.0f;
            if (filled[i] == 2 || (filled[i] == 1 && !inside))
               continue;
            float sum = 0.0f;
            for (float& c : w)
               sum += (c = std::max(c, 0.0f));
            texel[i].f = (unsigned)f;
            for (int k = 0; k < 3; k++)
               texel[i].w[k] = w[k] / sum;
            filled[i] = inside ? 2 : 1;
         }
   }

   // Curvature: spread so the 90th percentile of |c| lands near the ends,
   // whatever the mesh's scale or density.
   float curvGain = 1.0f;
   if (!curv.empty())
   {
      std::vector<float> mag(curv.size());
      for (size_t i = 0; i < curv.size(); i++)
         mag[i] = std::fabs(curv[i]);
      std::nth_element(mag.begin(), mag.begin() + mag.size() * 9 / 10, mag.end());
      curvGain = 0.45f / std::max(mag[mag.size() * 9 / 10], 1e-6f);
   }

   // Pass 2 (the cost): each covered texel's value, rows split over threads.
   auto shade = [&](int yBegin, int yEnd)
   {
      for (int y = yBegin; y < yEnd; y++)
         for (int x = 0; x < res; x++)
         {
            const size_t i = (size_t)y * res + x;
            if (!filled[i])
               continue;
            const Texel& t = texel[i];
            const unsigned* c = &m.indices[t.f * 3];
            const V3 p = Pos(m.vertices[c[0]]) * t.w[0] + Pos(m.vertices[c[1]]) * t.w[1] + Pos(m.vertices[c[2]]) * t.w[2];
            const V3 n = Norm(Nrm(m.vertices[c[0]]) * t.w[0] + Nrm(m.vertices[c[1]]) * t.w[1] +
                              Nrm(m.vertices[c[2]]) * t.w[2]);
            float rgb[3] = { 0, 0, 0 };
            // Each texel turns its rays by its own amount, so neighbours dither
            // instead of banding. Hashed in integers: in float, x * 7919 +
            // y * 104729 is past 2^24, fmod(.., 1) read 0, and every texel
            // shot the same 16 rays.
            const float turn = (float)(((unsigned)x * 7919u + (unsigned)y * 104729u) * 2654435769u >> 8) / 16777216.0f;
            switch (r.mode)
            {
               case kAO:
               {
                  int open = 0;
                  for (int s = 0; s < rays; s++)
                     if (bvh.Cast(p + n * eps, HemiDir(n, s, rays, turn), reach, true) >= reach)
                        open++;
                  rgb[0] = rgb[1] = rgb[2] = (float)open / rays;
                  break;
               }
               case kThickness:
               {
                  float sum = 0.0f;
                  const V3 in = n * -1.0f;
                  for (int s = 0; s < rays; s++)
                     sum += bvh.Cast(p + in * eps, HemiDir(in, s, rays, turn), reach, false);
                  rgb[0] = rgb[1] = rgb[2] = sum / (rays * reach);
                  break;
               }
               case kCurvature:
               {
                  const float k = curv[c[0]] * t.w[0] + curv[c[1]] * t.w[1] + curv[c[2]] * t.w[2];
                  rgb[0] = rgb[1] = rgb[2] = std::clamp(0.5f + k * curvGain, 0.0f, 1.0f);
                  break;
               }
               case kPosition:
                  rgb[0] = (p.x - lo.x) / size; rgb[1] = (p.y - lo.y) / size; rgb[2] = (p.z - lo.z) / size;
                  break;
               default:
                  rgb[0] = n.x * 0.5f + 0.5f; rgb[1] = n.y * 0.5f + 0.5f; rgb[2] = n.z * 0.5f + 0.5f;
                  break;
            }
            px[i * 4] = rgb[0]; px[i * 4 + 1] = rgb[1]; px[i * 4 + 2] = rgb[2]; px[i * 4 + 3] = 1.0f;
         }
   };
   const int workers = (r.mode == kAO || r.mode == kThickness)
                          ? std::clamp((int)std::thread::hardware_concurrency(), 1, 8) : 1;
   std::vector<std::thread> pool;
   for (int w = 1; w < workers; w++)
      pool.emplace_back(shade, res * w / workers, res * (w + 1) / workers);
   shade(0, res / workers);
   for (std::thread& th : pool)
      th.join();

   // Grow every island outward a few texels so filtering and mips at a seam
   // read the island's own colour, not the background.
   for (int pass = 0; pass < 4; pass++)
   {
      std::vector<unsigned char> next = filled;
      std::vector<float> out = px;
      for (int y = 0; y < res; y++)
         for (int x = 0; x < res; x++)
         {
            const size_t i = (size_t)y * res + x;
            if (filled[i])
               continue;
            float acc[3] = { 0, 0, 0 };
            int n = 0;
            for (int dy = -1; dy <= 1; dy++)
               for (int dx = -1; dx <= 1; dx++)
               {
                  const int nx = x + dx, ny = y + dy;
                  if (nx < 0 || ny < 0 || nx >= res || ny >= res)
                     continue;
                  const size_t j = (size_t)ny * res + nx;
                  if (!filled[j])
                     continue;
                  for (int k = 0; k < 3; k++)
                     acc[k] += px[j * 4 + k];
                  n++;
               }
            if (n == 0)
               continue;
            for (int k = 0; k < 3; k++)
               out[i * 4 + k] = acc[k] / n;
            out[i * 4 + 3] = 1.0f;
            next[i] = 1;
         }
      px.swap(out);
      filled.swap(next);
   }
   return px;
}

void BakeNode::Upload(const std::vector<float>& pixels, int res)
{
   if (mTex == 0)
      glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, res, res, 0, GL_RGBA, GL_FLOAT, pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glBindTexture(GL_TEXTURE_2D, 0);
   mTexSize = res;
   mTexRevision = NextTextureRevision();
   NodeWorkCounter()++;
}

void BakeNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   // A finished job lands first, so a change during it bakes from fresh state.
   if (mJob.valid() && mJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
   {
      const std::vector<float> px = mJob.get();
      Upload(px, mJobRes);
   }

   if (input == nullptr)
   {
      mCookWarning.clear();
      return;
   }
   const unsigned long long rev = input->MeshRevision();
   const int res = 256 << std::clamp(size, 0, 3);
   const bool changed = rev != mKeyMesh || mode != mKeyMode || size != mKeySize || samples != mKeySamples ||
                        distance != mKeyDistance || input != mKeyInput;
   if (!changed && !mDirty)
      return;
   if (mJob.valid())
   {
      mDirty = true;   // bake again once the running job lands
      return;
   }

   const Mesh& mesh = input->GetMesh();
   mCookWarning = (!mesh.Empty() && !HasUVs(mesh)) ? "No UVs: add a UV Project before Bake" : "";
   mKeyMesh = rev; mKeyMode = mode; mKeySize = size; mKeySamples = samples;
   mKeyDistance = distance; mKeyInput = input;
   mDirty = false;

   Request req;
   req.mesh = mesh;
   req.mode = mode; req.res = res; req.samples = samples; req.distance = distance;
   // The first image, and every image of an offline render, is baked here so
   // the frame that asks for it has it; live edits bake off the main thread.
   if (mTex == 0 || Transport::Instance().IsOfflineMode())
   {
      Upload(Bake(req), res);
      return;
   }
   mJobRes = res;
   mJob = std::async(std::launch::async, [req = std::move(req)]() { return Bake(req); });
}
