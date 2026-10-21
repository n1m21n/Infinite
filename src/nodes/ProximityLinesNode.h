#pragma once

#include <utility>
#include <vector>

#include "GeometryOpNodes.h"
#include "Geometry3DNodes.h"

// --- Proximity Lines ----------------------------------------------------
// Joins points that are closer than `radius` with thin lines (plexus, constellation
// looks). Point cloud in (Particle System, a Distribute node, Image to Points); a plain
// mesh falls back to its vertices. The lines come out as a mesh of thin three-sided
// prisms, one per link, the same way Wireframe draws edges, so they render and take
// materials like any other mesh.
//
// Neighbours are found with a uniform grid (counting sort, cell at least `radius`), split
// across threads. Every point reaches for its `maxPerPoint` nearest neighbours inside the
// radius, nearest first, and the whole node keeps at most `maxLinks`; both caps keep a
// dense cloud from turning into a solid. A pair that is reached from either end is drawn
// once (so a busy point can end up with more than `maxPerPoint` lines, from the points
// that reach for it). The same input and settings always give the same lines.
class ProximityLinesNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new ProximityLinesNode(); }

   // The link search alone, so it can be timed and tested without building the mesh.
   // xyz is three floats per point; links come back as index pairs (a < b).
   static void FindLinks(const float* xyz, size_t count, float radius, int maxPerPoint,
                         size_t maxLinks, std::vector<std::pair<unsigned int, unsigned int>>& out);

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override
   {
      if (input == nullptr) return Mat4::Identity();
      if (FindInstancer(input) != nullptr) return Mat4::Identity();
      return input->GetModelMatrix();
   }
   Material GetMaterial() const override;
   unsigned long long MaterialRevision() const override;
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override { return input ? input->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override { return input ? input->SurfaceTextureRevision() : 0; }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   unsigned long long MappingRevision() const override
   {
      return ComputeContentRevision(GetMappingTransform(), mMappingRevision, mLastMappingHash);
   }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "points"; }
   size_t LinkCount() const { return mLinks; }

   float radius = 0.5f;        // join points closer than this
   int maxPerPoint = 4;        // nearest neighbours each point reaches for, 1..16
   int maxLinks = 20000;       // links kept in total, 1..200000
   float thickness = 0.01f;    // prism radius
   bool aliveOnly = true;      // drop dead particles
   bool inheritMaterial = true;

   float color[3] = { 0.9f, 0.9f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("radius", radius); v.Int("maxPerPoint", maxPerPoint); v.Int("maxLinks", maxLinks);
      v.Float("thickness", thickness); v.Bool("aliveOnly", aliveOnly); v.Bool("inheritMaterial", inheritMaterial);
      v.Color("color", color); v.Float("metallic", metallic); v.Float("roughness", roughness);
      v.Float("opacity", opacity); v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   void RebuildIfNeeded();

   Mesh mCache;
   size_t mLinks = 0;
   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltUpstream = 0;
   float mBuiltRadius = -1.0f, mBuiltThickness = -1.0f;
   int mBuiltMaxPerPoint = 0, mBuiltMaxLinks = 0;
   bool mBuiltAliveOnly = true;
   unsigned long long mMeshRevision = 0;
   mutable unsigned long long mMaterialRevision = 0;
   mutable size_t mLastMaterialHash = 0;
   mutable unsigned long long mMappingRevision = 0;
   mutable size_t mLastMappingHash = 0;
   int mLastCookFrame = -1;
};
