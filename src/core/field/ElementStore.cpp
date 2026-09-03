#include "ElementStore.h"

#include <algorithm>

namespace Field
{
   void ElementStore::Resize(size_t elementCount)
   {
      if (mCount == elementCount)
         return;

      mCount = elementCount;

      mPx.resize(mCount);
      mPy.resize(mCount);
      mPz.resize(mCount);

      mNx.resize(mCount);
      mNy.resize(mCount);
      mNz.resize(mCount);

      mU.resize(mCount);
      mV.resize(mCount);

      mCr.resize(mCount);
      mCg.resize(mCount);
      mCb.resize(mCount);

      for (auto& pair : mUserAttribs)
      {
         UserAttrib& ua = pair.second;
         for (int l = 0; l < ua.lanes; ++l)
         {
            ua.data[l].resize(mCount);
            float initVal = (l < (int)ua.initValues.size()) ? ua.initValues[l] : 0.0f;
            std::fill(ua.data[l].begin(), ua.data[l].end(), initVal);
         }
      }
   }

   void ElementStore::FromMesh(const Mesh& mesh, size_t maxCount)
   {
      size_t n = mesh.vertices.size();
      if (maxCount > 0 && maxCount < n)
         n = maxCount;

      Resize(n);

      for (size_t i = 0; i < n; ++i)
      {
         const Vertex& v = mesh.vertices[i];
         mPx[i] = v.px;
         mPy[i] = v.py;
         mPz[i] = v.pz;

         mNx[i] = v.nx;
         mNy[i] = v.ny;
         mNz[i] = v.nz;

         mU[i] = v.u;
         mV[i] = v.v;
      }

      mHasInputVertexColor = mesh.HasVertexColor();
      if (mHasInputVertexColor)
      {
         for (size_t i = 0; i < n; ++i)
         {
            mCr[i] = mesh.vertexColor[3 * i + 0];
            mCg[i] = mesh.vertexColor[3 * i + 1];
            mCb[i] = mesh.vertexColor[3 * i + 2];
         }
      }
      else
      {
         std::fill(mCr.begin(), mCr.end(), 1.0f);
         std::fill(mCg.begin(), mCg.end(), 1.0f);
         std::fill(mCb.begin(), mCb.end(), 1.0f);
      }

      // Re-initialize user attribs to their initial values
      for (auto& pair : mUserAttribs)
      {
         UserAttrib& ua = pair.second;
         for (int l = 0; l < ua.lanes; ++l)
         {
            float initVal = (l < (int)ua.initValues.size()) ? ua.initValues[l] : 0.0f;
            std::fill(ua.data[l].begin(), ua.data[l].end(), initVal);
         }
      }
   }

   void ElementStore::ToMesh(Mesh& outMesh, const MeshWriteMask& mask, const Mesh& sourceMesh, size_t count) const
   {
      size_t n = (count > 0 && count <= mCount) ? count : mCount;
      outMesh.vertices.resize(n);

      for (size_t i = 0; i < n; ++i)
      {
         Vertex& v = outMesh.vertices[i];
         if (i < sourceMesh.vertices.size())
         {
            v = sourceMesh.vertices[i];
         }
         else
         {
            v = Vertex{};
         }

         if (mask.wroteP)
         {
            v.px = mPx[i];
            v.py = mPy[i];
            v.pz = mPz[i];
         }
         if (mask.wroteN)
         {
            v.nx = mNx[i];
            v.ny = mNy[i];
            v.nz = mNz[i];
         }
         if (mask.wroteUv)
         {
            v.u = mU[i];
            v.v = mV[i];
         }
      }

      // Handle vertexColor
      if (mask.wroteCd)
      {
         outMesh.vertexColor.resize(n * 3);
         for (size_t i = 0; i < n; ++i)
         {
            outMesh.vertexColor[3 * i + 0] = mCr[i];
            outMesh.vertexColor[3 * i + 1] = mCg[i];
            outMesh.vertexColor[3 * i + 2] = mCb[i];
         }
      }
      else
      {
         if (mHasInputVertexColor && sourceMesh.HasVertexColor())
         {
            outMesh.vertexColor = sourceMesh.vertexColor;
            if (outMesh.vertexColor.size() > n * 3)
               outMesh.vertexColor.resize(n * 3);
         }
         else
         {
            outMesh.vertexColor.clear(); // Empty stays empty
         }
      }

      // Preserve indices and metadata
      if (n == sourceMesh.vertices.size())
      {
         outMesh.indices = sourceMesh.indices;
         outMesh.faceMask = sourceMesh.faceMask;
         outMesh.selectionGroup = sourceMesh.selectionGroup;
      }
      else
      {
         // Truncated: keep only triangles with all vertices < n
         outMesh.indices.clear();
         outMesh.faceMask.clear();
         outMesh.selectionGroup.clear();

         size_t numFaces = sourceMesh.indices.size() / 3;
         for (size_t f = 0; f < numFaces; ++f)
         {
            unsigned int i0 = sourceMesh.indices[3 * f + 0];
            unsigned int i1 = sourceMesh.indices[3 * f + 1];
            unsigned int i2 = sourceMesh.indices[3 * f + 2];

            if (i0 < n && i1 < n && i2 < n)
            {
               outMesh.indices.push_back(i0);
               outMesh.indices.push_back(i1);
               outMesh.indices.push_back(i2);

               if (f < sourceMesh.faceMask.size())
                  outMesh.faceMask.push_back(sourceMesh.faceMask[f]);
               if (f < sourceMesh.selectionGroup.size())
                  outMesh.selectionGroup.push_back(sourceMesh.selectionGroup[f]);
            }
         }
      }
   }

   void ElementStore::ToMeshExact(Mesh& outMesh, const MeshWriteMask& mask) const
   {
      outMesh.vertices.resize(mCount);
      for (size_t i = 0; i < mCount; ++i)
      {
         Vertex& v = outMesh.vertices[i];
         v.px = mPx[i];
         v.py = mPy[i];
         v.pz = mPz[i];

         v.nx = mNx[i];
         v.ny = mNy[i];
         v.nz = mNz[i];

         v.u = mU[i];
         v.v = mV[i];
      }

      if (mHasInputVertexColor || mask.wroteCd)
      {
         outMesh.vertexColor.resize(mCount * 3);
         for (size_t i = 0; i < mCount; ++i)
         {
            outMesh.vertexColor[3 * i + 0] = mCr[i];
            outMesh.vertexColor[3 * i + 1] = mCg[i];
            outMesh.vertexColor[3 * i + 2] = mCb[i];
         }
      }
      else
      {
         outMesh.vertexColor.clear();
      }
   }

   bool ElementStore::DeclareAttrib(const std::string& name, DataType type, const std::vector<float>& initValues)
   {
      int lanes = FieldType::GetLanesForType(type);
      if (lanes <= 0) return false;

      UserAttrib ua;
      ua.type = type;
      ua.lanes = lanes;
      ua.initValues = initValues;
      ua.data.resize(lanes);
      for (int l = 0; l < lanes; ++l)
      {
         ua.data[l].resize(mCount);
         float initVal = (l < (int)initValues.size()) ? initValues[l] : 0.0f;
         std::fill(ua.data[l].begin(), ua.data[l].end(), initVal);
      }
      mUserAttribs[name] = std::move(ua);
      return true;
   }

   bool ElementStore::HasAttrib(const std::string& name) const
   {
      return mUserAttribs.find(name) != mUserAttribs.end();
   }

   DataType ElementStore::GetAttribType(const std::string& name) const
   {
      auto it = mUserAttribs.find(name);
      if (it != mUserAttribs.end()) return it->second.type;
      return DataType::Void;
   }

   std::vector<float>* ElementStore::GetAttribLane(const std::string& name, int component)
   {
      auto it = mUserAttribs.find(name);
      if (it != mUserAttribs.end() && component >= 0 && component < it->second.lanes)
      {
         return &it->second.data[component];
      }
      return nullptr;
   }

   const std::vector<float>* ElementStore::GetAttribLane(const std::string& name, int component) const
   {
      auto it = mUserAttribs.find(name);
      if (it != mUserAttribs.end() && component >= 0 && component < it->second.lanes)
      {
         return &it->second.data[component];
      }
      return nullptr;
   }

   void ElementStore::Clear()
   {
      mCount = 0;
      mHasInputVertexColor = false;
      mPx.clear(); mPy.clear(); mPz.clear();
      mNx.clear(); mNy.clear(); mNz.clear();
      mU.clear(); mV.clear();
      mCr.clear(); mCg.clear(); mCb.clear();
      mUserAttribs.clear();
   }
}
