#pragma once

#include "Mesh.h"
#include "FieldTypes.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Field
{
   struct MeshWriteMask
   {
      bool wroteP = false;
      bool wroteN = false;
      bool wroteUv = false;
      bool wroteCd = false;
      std::vector<std::string> wroteAttribs;

      bool WroteAttrib(const std::string& name) const
      {
         for (const auto& a : wroteAttribs)
         {
            if (a == name) return true;
         }
         return false;
      }
   };

   // Structure-of-Arrays attribute store for the Field element domain.
   // Allocates once and reuses memory across cooks.
   class ElementStore
   {
   public:
      ElementStore() = default;

      size_t Count() const { return mCount; }
      bool Empty() const { return mCount == 0; }

      // Allocates/resizes parallel arrays. Only reallocates when count changes.
      void Resize(size_t elementCount);

      // Gather from Mesh (AoS -> SoA)
      void FromMesh(const Mesh& mesh, size_t maxCount = 0);

      // Scatter to Mesh (SoA -> AoS), respecting write mask and preserving other mesh data
      void ToMesh(Mesh& outMesh, const MeshWriteMask& mask, const Mesh& sourceMesh, size_t count = 0) const;

      // Exact round-trip helper for testing: Mesh -> Store -> Mesh
      void ToMeshExact(Mesh& outMesh, const MeshWriteMask& mask) const;

      // Built-in attribute lanes
      std::vector<float>& Px() { return mPx; }
      std::vector<float>& Py() { return mPy; }
      std::vector<float>& Pz() { return mPz; }

      std::vector<float>& Nx() { return mNx; }
      std::vector<float>& Ny() { return mNy; }
      std::vector<float>& Nz() { return mNz; }

      std::vector<float>& U() { return mU; }
      std::vector<float>& V() { return mV; }

      std::vector<float>& Cr() { return mCr; }
      std::vector<float>& Cg() { return mCg; }
      std::vector<float>& Cb() { return mCb; }

      const std::vector<float>& Px() const { return mPx; }
      const std::vector<float>& Py() const { return mPy; }
      const std::vector<float>& Pz() const { return mPz; }

      const std::vector<float>& Nx() const { return mNx; }
      const std::vector<float>& Ny() const { return mNy; }
      const std::vector<float>& Nz() const { return mNz; }

      const std::vector<float>& U() const { return mU; }
      const std::vector<float>& V() const { return mV; }

      const std::vector<float>& Cr() const { return mCr; }
      const std::vector<float>& Cg() const { return mCg; }
      const std::vector<float>& Cb() const { return mCb; }

      bool HasInputVertexColor() const { return mHasInputVertexColor; }

      // Dynamic / user-declared attributes
      bool DeclareAttrib(const std::string& name, DataType type, const std::vector<float>& initValues = {});
      bool HasAttrib(const std::string& name) const;
      DataType GetAttribType(const std::string& name) const;
      std::vector<float>* GetAttribLane(const std::string& name, int component = 0);
      const std::vector<float>* GetAttribLane(const std::string& name, int component = 0) const;

      void Clear();

   private:
      size_t mCount = 0;
      bool mHasInputVertexColor = false;

      // Built-in lanes (P, N, uv, Cd)
      std::vector<float> mPx, mPy, mPz;
      std::vector<float> mNx, mNy, mNz;
      std::vector<float> mU, mV;
      std::vector<float> mCr, mCg, mCb;

      struct UserAttrib
      {
         DataType type = DataType::Float;
         int lanes = 1;
         std::vector<float> initValues;
         std::vector<std::vector<float>> data; // data[lane][i]
      };
      std::unordered_map<std::string, UserAttrib> mUserAttribs;
   };
}
