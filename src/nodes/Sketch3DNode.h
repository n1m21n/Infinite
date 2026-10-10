#pragma once

#include <map>
#include <string>
#include <vector>

#include "Geometry3DNodes.h"
#include "INode.h"
#include "field/ParamTable.h"
#include "sketch/Sketch3DEngine.h"

// Geometry source whose body is a JavaScript sketch: draw(t) builds a triangle mesh with
// a p5-WEBGL-flavoured API (box, sphere, translate, rotateY, beginShape...). Same sandbox
// and param() knobs as Sketch; the mesh feeds Render 3D / the geometry operators like any
// other source. Terminal source: identity model matrix, default material.
class Sketch3DNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new Sketch3DNode(); }
   Sketch3DNode();
   ~Sketch3DNode() override = default;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   INode* BypassSource() override { return nullptr; }
   IGeometrySource** GeometryInputSlot(int) override { return nullptr; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("code", code);
      v.Bool("animate", animate);
      mParamTable.VisitParams(v);
   }

   // IGeometrySource: pure generator, no passthrough.
   const Mesh& GetMesh() override { return bypassed ? EmptyMesh() : mMesh; }
   unsigned long long MeshRevision() override { return bypassed ? 0 : mMeshRevision; }
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override { return Material(); }
   unsigned int GetSurfaceTexture() override { return 0; }
   unsigned int GetMaterialTexture(int) override { return 0; }
   unsigned long long SurfaceTextureRevision() const override { return 0; }
   MappingTransform GetMappingTransform() const override { return MappingTransform(); }
   IGeometrySource* PassthroughSource() const override { return nullptr; }
   Mat4 GetInstanceGroupMatrix() const override { return Mat4::Identity(); }
   const std::vector<unsigned char>* InstanceSelection() const override { return nullptr; }
   unsigned long long InstanceSelectionRevision() const override { return 0; }
   const std::vector<Mat4>* InstanceTransformOverride() const override { return nullptr; }
   const std::vector<Particle>* GetPointCloud() override { return nullptr; }
   unsigned long long PointCloudRevision() override { return 0; }
   float PointBaseSize() const override { return 1.0f; }
   const Polyline* GetCurve() override { return nullptr; }
   unsigned long long CurveStamp() override { return 0; }

   // Compiles `code`. On failure the last working program (and mesh) stay.
   bool Apply();
   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }
   Field::ParamTable& GetParamTable() { return mParamTable; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }
   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   struct Preset { const char* name; const char* code; };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);

   std::string code;
   bool animate = true;
   int presetIndex = 0;

private:
   Sketch3DEngine mEngine;
   Field::ParamTable mParamTable;
   std::string mLastError, mNotice;
   std::string mCompiledCode;
   bool mCompiledOnce = false;
   int mLastCookFrame = -1;
   int mNodeIndex = -1;

   Mesh mMesh;
   unsigned long long mMeshRevision = 0;

   // What the last draw() saw; a cook with identical inputs keeps the mesh and its revision.
   bool mHasRun = false;
   double mRunT = 0;
   std::map<std::string, float> mRunParams;
};
