#pragma once

// PointZ (Point Operator) data: geometry as named per-point attributes that can
// live on the CPU, on the GPU, or both, plus a shared topology (triangles,
// line strips, point primitives). Modelled on TouchDesigner's POP family as
// documented publicly by Derivative (points with attributes, primitives that
// reference points). Portable OpenGL 3.2 core: no compute shaders, so the same
// code runs on macOS (GL 4.1) and Windows.
//
// Ownership rules, which keep chains cheap:
//  - A PopBuffer is immutable once made. An operator that changes `P` makes a
//    new buffer for `P` and hands every other attribute through by pointer.
//  - Topology is a shared const object; operators that only move points reuse
//    their input's topology without copying.
//  - A buffer made on the GPU (by a kernel) is downloaded only when something
//    on the CPU asks for it, and a CPU-made buffer is uploaded only when a
//    kernel or Render 3D asks for it.
//
// Every GL call happens on the thread that owns the main context (cook and
// render run there), so destructors may delete GL objects directly.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Mesh.h"

namespace pop
{
   struct AsyncBounds; // PopGpu.h

   // Standard attribute names (TouchDesigner's, where it has one).
   namespace attr
   {
      inline const char* P = "P";               // vec3 position
      inline const char* N = "N";               // vec3 normal
      inline const char* Color = "Color";       // vec4 rgba
      inline const char* Tex = "Tex";           // vec3 texture coordinate
      inline const char* PScale = "PScale";     // float point / instance scale
      inline const char* Rotate = "Rotate";     // vec3 euler degrees (instances)
      inline const char* Up = "Up";             // vec3 up vector (instances)
      inline const char* Active = "Active";     // float, 0 hides an instance
      inline const char* TexIndex = "TexIndex"; // float, instance texture index
      inline const char* Scale = "Scale";       // vec3 per-axis instance scale
      inline const char* Weight = "Weight";     // float field weight
      inline const char* Dist = "Dist";         // float signed distance
      inline const char* PartId = "PartId";
      inline const char* PartVel = "PartVel";
      inline const char* PartAge = "PartAge";
      inline const char* PartLifeSpan = "PartLifeSpan";
      inline const char* PartForce = "PartForce";
      inline const char* PartMass = "PartMass";
      inline const char* PartDrag = "PartDrag";
      inline const char* PartDeath = "PartDeath";
   }

   // Default value a missing attribute reads as (4 components, unused ones
   // ignored). N defaults to +Z like Mesh's Vertex, Color to opaque white.
   void DefaultValue(const std::string& name, float out[4]);
   // Component count a new attribute of this name gets when an operator
   // creates it without saying (P/N/Tex/Rotate/Up/PartVel/PartForce 3,
   // Color 4, everything else 1).
   int DefaultComps(const std::string& name);

   class PopBuffer
   {
   public:
      PopBuffer(std::vector<float>&& cpu, int comps);
      // Takes ownership of an existing GL buffer holding count * comps floats.
      PopBuffer(unsigned int glBuffer, int count, int comps);
      ~PopBuffer();
      PopBuffer(const PopBuffer&) = delete;
      PopBuffer& operator=(const PopBuffer&) = delete;

      int Count() const { return mCount; }
      int Comps() const { return mComps; }
      // Downloads from the GPU on first call when the data was made there.
      const std::vector<float>& Cpu() const;
      // Uploads on first call when the data was made on the CPU.
      unsigned int Gpu() const;
      bool OnGpu() const { return mGpu != 0; }
      bool OnCpu() const { return mCpuValid; }

   private:
      mutable std::vector<float> mCpu;
      mutable bool mCpuValid = false;
      mutable unsigned int mGpu = 0;
      int mCount = 0;
      int mComps = 1;
   };
   using BufferPtr = std::shared_ptr<const PopBuffer>;

   // Pooled GL buffer stores (see PopData.cpp): a store of exactly `bytes`
   // (at least 16), reused when one of that size was freed. PopBuffer gives
   // its store back when it dies.
   unsigned int AcquireGpuBuffer(size_t bytes);
   void ReleaseGpuBuffer(unsigned int id, size_t bytes);

   BufferPtr MakeBuffer(std::vector<float>&& values, int comps);
   // n copies of one value.
   BufferPtr MakeFilled(int count, int comps, const float value[4]);

   // Line strips: strip s covers lineVerts[lineStart[s] .. lineStart[s] + lineCount[s]).
   class PopTopology
   {
   public:
      std::vector<uint32_t> tris;      // 3 point indices per triangle
      std::vector<uint32_t> lineVerts; // point indices, strip after strip
      std::vector<int> lineStart;
      std::vector<int> lineCount;
      std::vector<unsigned char> lineClosed;
      // Draw every point as a point primitive (a cloud, or vertices only).
      bool pointPrims = false;

      ~PopTopology();
      size_t TriangleCount() const { return tris.size() / 3; }
      size_t StripCount() const { return lineStart.size(); }
      bool HasFaces() const { return !tris.empty(); }
      bool HasLines() const { return !lineStart.empty(); }
      void AddStrip(const std::vector<uint32_t>& pts, bool closed);
      // Element buffers, built and uploaded on first use.
      unsigned int TriIbo() const;
      // GL_LINES pairs for every strip segment (closed strips close up).
      unsigned int LineIbo() const;
      int LineIndexCount() const;

   private:
      mutable unsigned int mTriIbo = 0;
      mutable unsigned int mLineIbo = 0;
      mutable int mLineIndexCount = -1;
   };
   using TopologyPtr = std::shared_ptr<const PopTopology>;

   struct PopAttrib
   {
      std::string name;
      BufferPtr buffer;
   };

   class PopData
   {
   public:
      int pointCount = 0;
      std::vector<PopAttrib> attribs;
      TopologyPtr topo;
      // Grid shape of a generator's points (rows x cols), 0 when unknown.
      // TouchDesigner calls this the PointZ's dimension; grid-aware operators use it.
      int dims[2] = { 0, 0 };
      unsigned long long revision = 0;
      // Instance PointZ: how Render 3D builds each instance's transform from
      // these points (see pop::InstanceMatrices).
      float instanceScale = 1.0f;
      int instanceAlign = 0;
      // Instance PointZ, Geometry COMP style: extra transform and per-instance
      // texture choice, read by pop::InstanceMatrices and Render 3D.
      struct InstanceLook
      {
         float scaleXYZ[3] = { 1, 1, 1 }; // multiplies PScale * Scale per axis
         float rotate[3] = { 0, 0, 0 };   // degrees added to Rotate
         float offset[3] = { 0, 0, 0 };   // added to the position
         int texMode = 0;                 // 0 material texture, 1 pick image by index, 2 atlas grid
         int texFrom = 0;                 // 0 TexIndex attribute, 1 instance number, 2 random
         int atlasCols = 1, atlasRows = 1;
         unsigned int tex[4] = { 0, 0, 0, 0 };
         int texCount = 0;                // connected images, packed from tex[0]
      } instanceLook;

      const PopAttrib* Find(const std::string& name) const;
      bool Has(const std::string& name) const { return Find(name) != nullptr; }
      int CompsOf(const std::string& name) const;
      // Replaces or adds an attribute (its count must equal pointCount).
      void Set(const std::string& name, BufferPtr buffer);
      void Remove(const std::string& name);
      void Rename(const std::string& from, const std::string& to);
      // CPU copy of an attribute, missing ones filled with their default,
      // resized to `comps` components (0 = the attribute's own count).
      std::vector<float> Read(const std::string& name, int comps = 0) const;
      const PopTopology& Topo() const;

      // Object-space bounds, cached per revision. Uses the CPU copy of P if
      // there is one, else a GPU reduction (one 1x1 render, no full download).
      bool Bounds(float lo[3], float hi[3]) const;
      // For per-frame consumers: CPU data is measured right away, GPU data
      // through `state` without waiting (see GpuBoundsAsync).
      bool BoundsAsync(AsyncBounds& state, float lo[3], float hi[3]) const;

   private:
      mutable unsigned long long mBoundsRevision = ~0ull;
      mutable bool mHasBounds = false;
      mutable float mLo[3] = { 0, 0, 0 };
      mutable float mHi[3] = { 0, 0, 0 };
   };
   using DataPtr = std::shared_ptr<const PopData>;

   // Conversions to and from the rest of Infinite's 3D. Mesh vertices map one
   // to one onto points (Infinite meshes are already split at UV/normal
   // seams); Particles keep their simulation state as Part* attributes.
   std::shared_ptr<PopData> FromMesh(const Mesh& mesh);
   std::shared_ptr<PopData> FromCloud(const std::vector<Particle>& cloud);
   std::shared_ptr<PopData> FromCurve(const Polyline& line);
   Mesh ToMesh(const PopData& data);
   std::vector<Particle> ToCloud(const PopData& data);
   // First line strip as a Polyline (empty when there is none).
   Polyline ToCurve(const PopData& data);

   // A fresh PopData sharing `in`'s attributes and topology (no copies).
   std::shared_ptr<PopData> ShallowCopy(const PopData& in);
}
