#include "MeshModalSolver.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace MeshModalSolver
{
namespace
{
   struct Csr
   {
      int n = 0;
      std::vector<int> rowStart, col;
      std::vector<double> val;
      void Mul(const double* x, double* y) const
      {
         for (int i = 0; i < n; i++)
         {
            double acc = 0.0;
            for (int k = rowStart[i]; k < rowStart[i + 1]; k++)
               acc += val[k] * x[col[k]];
            y[i] = acc;
         }
      }
   };

   struct Mesh3
   {
      std::vector<double> p; // 3 per vertex
      std::vector<int> tri;  // 3 per triangle
      int nv() const { return (int)(p.size() / 3); }
   };

   uint64_t HashCell(int64_t x, int64_t y, int64_t z)
   {
      return (uint64_t)(x * 73856093LL) ^ (uint64_t)(y * 19349663LL) ^ (uint64_t)(z * 83492791LL);
   }

   // Merge vertices that share a cell of size `cell` (average position) and
   // drop triangles that collapse. cell <= 0 merges only exactly-equal points
   // (handled by the caller passing a tiny cell).
   Mesh3 Cluster(const Mesh3& in, double cell, const double lo[3])
   {
      Mesh3 out;
      std::unordered_map<uint64_t, int> cellToNew;
      std::vector<int> remap(in.nv());
      std::vector<int> counts;
      for (int v = 0; v < in.nv(); v++)
      {
         const int64_t cx = (int64_t)std::floor((in.p[3 * v] - lo[0]) / cell);
         const int64_t cy = (int64_t)std::floor((in.p[3 * v + 1] - lo[1]) / cell);
         const int64_t cz = (int64_t)std::floor((in.p[3 * v + 2] - lo[2]) / cell);
         const uint64_t key = HashCell(cx, cy, cz);
         auto it = cellToNew.find(key);
         int id;
         if (it == cellToNew.end())
         {
            id = (int)counts.size();
            cellToNew[key] = id;
            counts.push_back(0);
            out.p.insert(out.p.end(), { 0.0, 0.0, 0.0 });
         }
         else
            id = it->second;
         remap[v] = id;
         counts[id]++;
         for (int a = 0; a < 3; a++)
            out.p[3 * id + a] += in.p[3 * v + a];
      }
      for (size_t i = 0; i < counts.size(); i++)
         for (int a = 0; a < 3; a++)
            out.p[3 * i + a] /= (double)counts[i];
      for (size_t t = 0; t + 2 < in.tri.size(); t += 3)
      {
         const int a = remap[in.tri[t]], b = remap[in.tri[t + 1]], c = remap[in.tri[t + 2]];
         if (a == b || b == c || a == c)
            continue;
         out.tri.insert(out.tri.end(), { a, b, c });
      }
      return out;
   }

   // Dense symmetric eigen-decomposition by cyclic Jacobi. a is n x n
   // row-major (destroyed); eigenvalues to w, eigenvectors as columns of v.
   void JacobiEigen(std::vector<double>& a, int n, std::vector<double>& w, std::vector<double>& v)
   {
      v.assign((size_t)n * n, 0.0);
      for (int i = 0; i < n; i++)
         v[(size_t)i * n + i] = 1.0;
      for (int sweep = 0; sweep < 60; sweep++)
      {
         double off = 0.0;
         for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
               off += a[(size_t)i * n + j] * a[(size_t)i * n + j];
         if (off < 1e-26)
            break;
         for (int p = 0; p < n - 1; p++)
            for (int q = p + 1; q < n; q++)
            {
               const double apq = a[(size_t)p * n + q];
               if (std::fabs(apq) < 1e-300)
                  continue;
               const double theta = (a[(size_t)q * n + q] - a[(size_t)p * n + p]) / (2.0 * apq);
               const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
               const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
               for (int k = 0; k < n; k++)
               {
                  const double akp = a[(size_t)k * n + p], akq = a[(size_t)k * n + q];
                  a[(size_t)k * n + p] = c * akp - s * akq;
                  a[(size_t)k * n + q] = s * akp + c * akq;
               }
               for (int k = 0; k < n; k++)
               {
                  const double apk = a[(size_t)p * n + k], aqk = a[(size_t)q * n + k];
                  a[(size_t)p * n + k] = c * apk - s * aqk;
                  a[(size_t)q * n + k] = s * apk + c * aqk;
               }
               for (int k = 0; k < n; k++)
               {
                  const double vkp = v[(size_t)k * n + p], vkq = v[(size_t)k * n + q];
                  v[(size_t)k * n + p] = c * vkp - s * vkq;
                  v[(size_t)k * n + q] = s * vkp + c * vkq;
               }
            }
      }
      w.resize(n);
      for (int i = 0; i < n; i++)
         w[i] = a[(size_t)i * n + i];
   }

   // Generalised small problem A x = lambda B x (B SPD). Ascending order;
   // x columns are B-orthonormal. Returns false if B is not SPD.
   bool SmallGeneralisedEigen(const std::vector<double>& A, const std::vector<double>& B, int n,
                              std::vector<double>& lam, std::vector<double>& X)
   {
      // B = L L^T.
      std::vector<double> L((size_t)n * n, 0.0);
      for (int i = 0; i < n; i++)
         for (int j = 0; j <= i; j++)
         {
            double s = B[(size_t)i * n + j];
            for (int k = 0; k < j; k++)
               s -= L[(size_t)i * n + k] * L[(size_t)j * n + k];
            if (i == j)
            {
               if (s <= 1e-14)
                  return false;
               L[(size_t)i * n + i] = std::sqrt(s);
            }
            else
               L[(size_t)i * n + j] = s / L[(size_t)j * n + j];
         }
      // C = L^-1 A L^-T.
      std::vector<double> T((size_t)n * n), C((size_t)n * n);
      for (int col = 0; col < n; col++) // T = L^-1 A, column by column
         for (int i = 0; i < n; i++)
         {
            double s = A[(size_t)i * n + col];
            for (int k = 0; k < i; k++)
               s -= L[(size_t)i * n + k] * T[(size_t)k * n + col];
            T[(size_t)i * n + col] = s / L[(size_t)i * n + i];
         }
      for (int row = 0; row < n; row++) // C = T L^-T, row by row
         for (int i = 0; i < n; i++)
         {
            double s = T[(size_t)row * n + i];
            for (int k = 0; k < i; k++)
               s -= L[(size_t)i * n + k] * C[(size_t)row * n + k];
            C[(size_t)row * n + i] = s / L[(size_t)i * n + i];
         }
      for (int i = 0; i < n; i++) // symmetrise
         for (int j = i + 1; j < n; j++)
            C[(size_t)i * n + j] = C[(size_t)j * n + i] = 0.5 * (C[(size_t)i * n + j] + C[(size_t)j * n + i]);
      std::vector<double> w, V;
      JacobiEigen(C, n, w, V);
      std::vector<int> order(n);
      for (int i = 0; i < n; i++)
         order[i] = i;
      std::sort(order.begin(), order.end(), [&](int a, int b) { return w[a] < w[b]; });
      lam.resize(n);
      X.assign((size_t)n * n, 0.0);
      for (int c = 0; c < n; c++)
      {
         const int src = order[c];
         lam[c] = w[src];
         // x = L^-T y
         for (int i = n - 1; i >= 0; i--)
         {
            double s = V[(size_t)i * n + src];
            for (int k = i + 1; k < n; k++)
               s -= L[(size_t)k * n + i] * X[(size_t)k * n + c];
            X[(size_t)i * n + c] = s / L[(size_t)i * n + i];
         }
      }
      return true;
   }

// Eigen-solve of an assembled operator (stiffness weights `w`, lumped mass `mass`) on the welded/size-capped
// vertex set `m`. Shared by the triangle-mesh, point-cloud and curve entry points.
Modes SolveOperator(const Mesh3& m, std::vector<std::unordered_map<int, double>>& w, std::vector<double>& mass,
                    double diam, const double ext[3], int maxModes, Modes result)
{
   const int n = m.nv();
   auto P = [&](int v, int a) { return m.p[3 * v + a]; };
   Csr S, M;
   S.n = M.n = n;
   S.rowStart.assign(n + 1, 0);
   std::vector<double> mDiag(n);
   double totalMass = 0.0;
   for (int i = 0; i < n; i++)
   {
      mDiag[i] = std::max(mass[i], 1e-12);
      totalMass += mDiag[i];
   }
   for (int i = 0; i < n; i++)
   {
      double diag = 0.0;
      std::vector<std::pair<int, double>> row;
      for (const auto& kv : w[i])
      {
         const double wt = std::max(kv.second, 0.0); // keep it an M-matrix on obtuse triangles
         if (wt <= 0.0)
            continue;
         row.emplace_back(kv.first, -wt);
         diag += wt;
      }
      row.emplace_back(i, diag);
      std::sort(row.begin(), row.end());
      for (const auto& e : row)
      {
         S.col.push_back(e.first);
         S.val.push_back(e.second);
      }
      S.rowStart[i + 1] = (int)S.col.size();
   }

   // Connected components -> how many zero modes to expect and skip.
   std::vector<int> comp(n, -1);
   int numComp = 0;
   for (int s0 = 0; s0 < n; s0++)
   {
      if (comp[s0] >= 0)
         continue;
      std::vector<int> stack { s0 };
      comp[s0] = numComp;
      while (!stack.empty())
      {
         const int v = stack.back();
         stack.pop_back();
         for (int k = S.rowStart[v]; k < S.rowStart[v + 1]; k++)
            if (comp[S.col[k]] < 0)
            {
               comp[S.col[k]] = numComp;
               stack.push_back(S.col[k]);
            }
      }
      numComp++;
   }

   const int want = std::min(maxModes + numComp, n - 2);
   const int p = std::min(want + 8, n - 1);
   const double sigma = 0.3 * std::pow(3.14159265358979 / diam, 2.0);
   // Shifted operator K = S + sigma * M (diagonal M), Jacobi preconditioner.
   Csr K = S;
   std::vector<double> kDiag(n, 0.0);
   for (int i = 0; i < n; i++)
      for (int k = K.rowStart[i]; k < K.rowStart[i + 1]; k++)
         if (K.col[k] == i)
         {
            K.val[k] += sigma * mDiag[i];
            kDiag[i] = K.val[k];
         }

   auto applyS = [&](const double* x, double* y) { S.Mul(x, y); };
   auto Mdot = [&](const double* a, const double* b) {
      double s = 0.0;
      for (int i = 0; i < n; i++)
         s += mDiag[i] * a[i] * b[i];
      return s;
   };

   // Starting block: deterministic pseudo-random, M-orthonormalised later by Rayleigh-Ritz.
   std::vector<std::vector<double>> X(p, std::vector<double>(n)), Y(p, std::vector<double>(n));
   uint32_t seed = 12345u;
   for (int c = 0; c < p; c++)
      for (int i = 0; i < n; i++)
      {
         seed = seed * 1664525u + 1013904223u;
         X[c][i] = ((double)(seed >> 8) / 16777216.0) - 0.5;
      }
   std::vector<double> lam(p, 0.0), prevLam(p, 1e30);

   std::vector<double> r(n), z(n), d(n), Kd(n), rhs(n);
   auto solveShifted = [&](const double* b, double* x) {
      // PCG on K x = b, starting from x (previous guess keeps it cheap).
      K.Mul(x, Kd.data());
      double bn = 0.0;
      for (int i = 0; i < n; i++)
      {
         r[i] = b[i] - Kd[i];
         bn += b[i] * b[i];
      }
      for (int i = 0; i < n; i++)
      {
         z[i] = r[i] / kDiag[i];
         d[i] = z[i];
      }
      double rz = 0.0;
      for (int i = 0; i < n; i++)
         rz += r[i] * z[i];
      const double tol = 1e-8 * std::max(bn, 1e-30);
      for (int it = 0; it < 400; it++)
      {
         double rr = 0.0;
         for (int i = 0; i < n; i++)
            rr += r[i] * r[i];
         if (rr < tol)
            break;
         K.Mul(d.data(), Kd.data());
         double dKd = 0.0;
         for (int i = 0; i < n; i++)
            dKd += d[i] * Kd[i];
         if (dKd <= 0.0)
            break;
         const double alpha = rz / dKd;
         for (int i = 0; i < n; i++)
         {
            x[i] += alpha * d[i];
            r[i] -= alpha * Kd[i];
         }
         double rzNew = 0.0;
         for (int i = 0; i < n; i++)
         {
            z[i] = r[i] / kDiag[i];
            rzNew += r[i] * z[i];
         }
         const double beta = rzNew / rz;
         rz = rzNew;
         for (int i = 0; i < n; i++)
            d[i] = z[i] + beta * d[i];
      }
   };

   std::vector<double> Sy(n);
   for (int outer = 0; outer < 14; outer++)
   {
      // Y = K^-1 M X
      for (int c = 0; c < p; c++)
      {
         for (int i = 0; i < n; i++)
            rhs[i] = mDiag[i] * X[c][i];
         std::fill(Y[c].begin(), Y[c].end(), 0.0);
         solveShifted(rhs.data(), Y[c].data());
      }
      // Rayleigh-Ritz on span(Y).
      std::vector<double> A((size_t)p * p), B((size_t)p * p);
      for (int c = 0; c < p; c++)
      {
         applyS(Y[c].data(), Sy.data());
         for (int c2 = 0; c2 <= c; c2++)
         {
            double a = 0.0;
            for (int i = 0; i < n; i++)
               a += Y[c2][i] * Sy[i];
            A[(size_t)c * p + c2] = A[(size_t)c2 * p + c] = a;
            const double b = Mdot(Y[c].data(), Y[c2].data());
            B[(size_t)c * p + c2] = B[(size_t)c2 * p + c] = b;
         }
      }
      std::vector<double> ritzLam, V;
      if (!SmallGeneralisedEigen(A, B, p, ritzLam, V))
         break;
      for (int c = 0; c < p; c++)
      {
         std::fill(X[c].begin(), X[c].end(), 0.0);
         for (int k = 0; k < p; k++)
         {
            const double coef = V[(size_t)k * p + c];
            for (int i = 0; i < n; i++)
               X[c][i] += coef * Y[k][i];
         }
         lam[c] = ritzLam[c];
      }
      double change = 0.0;
      for (int c = 0; c < want; c++)
         change = std::max(change, std::fabs(lam[c] - prevLam[c]) / std::max(std::fabs(lam[c]), 1e-9));
      prevLam = lam;
      if (change < 1e-5)
         break;
   }

   // Drop the zero modes, keep the next maxModes.
   const double zeroTol = 1e-5 * std::pow(3.14159265358979 / diam, 2.0);
   int first = 0;
   while (first < want && lam[first] < zeroTol)
      first++;
   const int count = std::min(maxModes, want - first);
   if (count < 1)
      return result;

   // Order of vertices along the longest bounding-box axis, for the pickup points.
   int axis = 0;
   for (int a = 1; a < 3; a++)
      if (ext[a] > ext[axis])
         axis = a;
   std::vector<int> byAxis(n);
   for (int i = 0; i < n; i++)
      byAxis[i] = i;
   std::sort(byAxis.begin(), byAxis.end(), [&](int a, int b) { return P(a, axis) < P(b, axis); });

   result.count = count;
   for (int c = 0; c < count; c++)
   {
      const std::vector<double>& phi = X[first + c];
      // Unit RMS over the surface: scale so sum(M phi^2) = totalMass.
      const double norm = std::sqrt(Mdot(phi.data(), phi.data()) / totalMass);
      result.lambda[c] = (float)lam[first + c];
      for (int k = 0; k < kPickPoints; k++)
      {
         const int vtx = byAxis[std::min(n - 1, (int)(((double)k + 0.5) / kPickPoints * n))];
         result.shape[c][k] = norm > 0.0 ? (float)(phi[vtx] / norm) : 0.0f;
      }
   }
   result.valid = true;
   return result;
}
}

Modes Solve(const float* positions, int numVertices, const uint32_t* indices, int numIndices, int maxModes, int vertexCap)
{
   Modes result;
   maxModes = std::clamp(maxModes, 1, kMaxModes);
   if (!positions || !indices || numVertices < 4 || numIndices < 3)
      return result;

   Mesh3 raw;
   raw.p.resize((size_t)numVertices * 3);
   double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
   for (int v = 0; v < numVertices; v++)
      for (int a = 0; a < 3; a++)
      {
         const double x = positions[3 * v + a];
         if (!std::isfinite(x))
            return result;
         raw.p[3 * v + a] = x;
         lo[a] = std::min(lo[a], x);
         hi[a] = std::max(hi[a], x);
      }
   for (int t = 0; t + 2 < numIndices; t += 3)
   {
      const uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
      if ((int)a >= numVertices || (int)b >= numVertices || (int)c >= numVertices)
         continue;
      raw.tri.insert(raw.tri.end(), { (int)a, (int)b, (int)c });
   }
   const double ext[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] };
   const double diam = std::sqrt(ext[0] * ext[0] + ext[1] * ext[1] + ext[2] * ext[2]);
   if (!(diam > 1e-9))
      return result;

   // 1. Weld coincident vertices (flat-shaded meshes duplicate them per face).
   Mesh3 m = Cluster(raw, diam * 1e-5, lo);
   result.weldedVertices = m.nv();
   // 2. Size cap: coarsen with a uniform grid until the system fits.
   if (m.nv() > vertexCap)
   {
      double cell = diam / std::cbrt((double)vertexCap) * 0.5;
      for (int it = 0; it < 40 && m.nv() > vertexCap; it++)
      {
         m = Cluster(m, cell, lo);
         cell *= 1.25;
      }
   }
   const int n = m.nv();
   result.solverVertices = n;
   if (n < 8 || m.tri.size() < 3)
      return result;

   // 3. Cotangent stiffness and lumped mass.
   std::vector<std::unordered_map<int, double>> w(n);
   std::vector<double> mass(n, 0.0);
   auto P = [&](int v, int a) { return m.p[3 * v + a]; };
   for (size_t t = 0; t + 2 < m.tri.size(); t += 3)
   {
      const int idx[3] = { m.tri[t], m.tri[t + 1], m.tri[t + 2] };
      double e[3][3]; // edge opposite vertex k
      for (int k = 0; k < 3; k++)
         for (int a = 0; a < 3; a++)
            e[k][a] = P(idx[(k + 2) % 3], a) - P(idx[(k + 1) % 3], a);
      auto cross = [](const double* a, const double* b, double* o) {
         o[0] = a[1] * b[2] - a[2] * b[1];
         o[1] = a[2] * b[0] - a[0] * b[2];
         o[2] = a[0] * b[1] - a[1] * b[0];
      };
      double cr[3];
      const double ab[3] = { P(idx[1], 0) - P(idx[0], 0), P(idx[1], 1) - P(idx[0], 1), P(idx[1], 2) - P(idx[0], 2) };
      const double ac[3] = { P(idx[2], 0) - P(idx[0], 0), P(idx[2], 1) - P(idx[0], 1), P(idx[2], 2) - P(idx[0], 2) };
      cross(ab, ac, cr);
      const double area2 = std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
      if (area2 < 1e-18)
         continue;
      for (int k = 0; k < 3; k++)
      {
         // cot at corner k = dot(u, v) / |u x v|, u,v the two edges meeting at k.
         const int i = idx[(k + 1) % 3], j = idx[(k + 2) % 3];
         double u[3], vv[3];
         for (int a = 0; a < 3; a++)
         {
            u[a] = P(i, a) - P(idx[k], a);
            vv[a] = P(j, a) - P(idx[k], a);
         }
         const double dot = u[0] * vv[0] + u[1] * vv[1] + u[2] * vv[2];
         const double wgt = 0.5 * dot / area2 * 2.0 * 0.5; // 0.5 * cot, cot = dot / area2
         w[i][j] += wgt;
         w[j][i] += wgt;
         mass[idx[k]] += area2 * 0.5 / 3.0;
      }
      (void)e;
   }
   return SolveOperator(m, w, mass, diam, ext, maxModes, result);
}

Modes SolveCloud(const float* positions, int numVertices, int maxModes, int vertexCap)
{
   Modes result;
   maxModes = std::clamp(maxModes, 1, kMaxModes);
   if (!positions || numVertices < 8)
      return result;
   Mesh3 m;
   m.p.resize((size_t)numVertices * 3);
   double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
   for (int v = 0; v < numVertices; v++)
      for (int a = 0; a < 3; a++)
      {
         const double x = positions[3 * v + a];
         if (!std::isfinite(x))
            return result;
         m.p[3 * v + a] = x;
         lo[a] = std::min(lo[a], x);
         hi[a] = std::max(hi[a], x);
      }
   const double ext[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] };
   const double diam = std::sqrt(ext[0] * ext[0] + ext[1] * ext[1] + ext[2] * ext[2]);
   if (!(diam > 1e-9))
      return result;
   m = Cluster(m, diam * 1e-5, lo);
   result.weldedVertices = m.nv();
   if (m.nv() > vertexCap)
   {
      double cell = diam / std::cbrt((double)vertexCap) * 0.5;
      for (int it = 0; it < 40 && m.nv() > vertexCap; it++)
      {
         m = Cluster(m, cell, lo);
         cell *= 1.25;
      }
   }
   const int n = m.nv();
   result.solverVertices = n;
   if (n < 8)
      return result;

   // Graph Laplacian over each point's nearest neighbours, edge weight 1/d^2 and unit vertex mass: the
   // same length-unit scaling a surface Laplacian has, so the shared solver's shift and zero-mode
   // tolerance mean the same thing here. Brute-force kNN: n is capped at vertexCap, and this runs on a worker.
   constexpr int kNeighbours = 6;
   std::vector<std::unordered_map<int, double>> w(n);
   std::vector<double> mass(n, 1.0);
   std::vector<std::pair<double, int>> dist;
   for (int i = 0; i < n; i++)
   {
      dist.clear();
      for (int j = 0; j < n; j++)
      {
         if (j == i)
            continue;
         double d2 = 0.0;
         for (int a = 0; a < 3; a++)
         {
            const double d = m.p[3 * i + a] - m.p[3 * j + a];
            d2 += d * d;
         }
         dist.emplace_back(d2, j);
      }
      const int k = std::min<int>(kNeighbours, (int)dist.size());
      std::partial_sort(dist.begin(), dist.begin() + k, dist.end());
      for (int q = 0; q < k; q++)
      {
         const double wt = 1.0 / std::max(dist[q].first, diam * diam * 1e-12);
         const int j = dist[q].second;
         w[i][j] = std::max(w[i][j], wt);
         w[j][i] = std::max(w[j][i], wt);
      }
   }
   return SolveOperator(m, w, mass, diam, ext, maxModes, result);
}

Modes SolveCurve(const float* positions, int numVertices, bool closed, int maxModes)
{
   Modes result;
   maxModes = std::clamp(maxModes, 1, kMaxModes);
   if (!positions || numVertices < 2)
      return result;
   const int segs = closed ? numVertices : numVertices - 1;
   std::vector<double> cum(segs + 1, 0.0);
   for (int s = 0; s < segs; s++)
   {
      const float* a = positions + 3 * s;
      const float* b = positions + 3 * ((s + 1) % numVertices);
      double d2 = 0.0;
      for (int k = 0; k < 3; k++)
      {
         if (!std::isfinite(a[k]) || !std::isfinite(b[k]))
            return result;
         const double d = (double)b[k] - (double)a[k];
         d2 += d * d;
      }
      cum[s + 1] = cum[s] + std::sqrt(d2);
   }
   const double total = cum[segs];
   if (!(total > 1e-9))
      return result;

   // Resample to evenly spaced points along the arc length, so the string's modes do not depend on how
   // the curve happened to be sampled (a two-point line is a perfectly good string).
   constexpr int kSamples = 200;
   const int n = kSamples;
   Mesh3 m;
   m.p.resize((size_t)n * 3);
   int seg = 0;
   for (int i = 0; i < n; i++)
   {
      const double t = closed ? total * i / n : total * i / (n - 1);
      while (seg < segs - 1 && cum[seg + 1] < t)
         seg++;
      const double len = std::max(cum[seg + 1] - cum[seg], 1e-12);
      const double u = std::clamp((t - cum[seg]) / len, 0.0, 1.0);
      const float* a = positions + 3 * seg;
      const float* b = positions + 3 * ((seg + 1) % numVertices);
      for (int k = 0; k < 3; k++)
         m.p[3 * i + k] = (double)a[k] + ((double)b[k] - (double)a[k]) * u;
   }
   double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
   for (int i = 0; i < n; i++)
      for (int a = 0; a < 3; a++)
      {
         lo[a] = std::min(lo[a], m.p[3 * i + a]);
         hi[a] = std::max(hi[a], m.p[3 * i + a]);
      }
   const double ext[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] };
   // A string's first mode has wavelength 2 * its length, so use the arc length as the length scale
   // (a bounding box of a coiled curve would put the shift and zero-mode tolerance in the wrong place).
   const double diam = total;
   result.weldedVertices = n;
   result.solverVertices = n;

   // 1D finite elements: stiffness 1/h per segment, mass h/2 to each end. Free ends (Neumann), like the
   // free edge of a plate; a closed curve is a ring and its modes come in pairs.
   const double h = total / (closed ? n : n - 1);
   std::vector<std::unordered_map<int, double>> w(n);
   std::vector<double> mass(n, 0.0);
   const int links = closed ? n : n - 1;
   for (int i = 0; i < links; i++)
   {
      const int j = (i + 1) % n;
      w[i][j] += 1.0 / h;
      w[j][i] += 1.0 / h;
      mass[i] += 0.5 * h;
      mass[j] += 0.5 * h;
   }
   return SolveOperator(m, w, mass, diam, ext, maxModes, result);
}
} // namespace MeshModalSolver
