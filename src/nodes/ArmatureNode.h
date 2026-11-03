#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "Geometry3DNodes.h"
#include "GltfImport.h"
#include "RigPose.h"

// A chain of bones through any geometry (Blender's armature with envelope
// weights, written fresh): tentacles, plants, cables, tails. The bones run
// along the path curve if one is patched, else along an axis through the
// input's bounds. Each point takes up to four bones, weighted by a smooth
// falloff with its distance to each bone. Curl, twist and a travelling wave
// turn the bones; the 3D View's Pose mode and IK (RigPose) add on top, all as
// params. Skinning reuses core/Skinning, so it poses like a glTF rig.
//
// Whatever comes in bends: mesh vertices (with normals), point clouds and CPU
// particles (normals and velocities turn too), and curves (so a Path follows
// the bend). Downstream of Instance on Points each copy rides its bone and
// turns with it but keeps its shape (InstanceTransformOverride). GPU-only
// particles never reach the CPU, so they pass through unbent with a warning.
class ArmatureNode : public INode, public IGeometrySource, public IRigged
{
public:
   static INode* Create() { return new ArmatureNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override { return input ? input->GetModelMatrix() : Mat4::Identity(); }
   Material GetMaterial() const override { return input ? input->GetMaterial() : Material(); }
   unsigned long long MaterialRevision() const override { return input ? input->MaterialRevision() : 0; }
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override { return input ? input->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override { return input ? input->SurfaceTextureRevision() : 0; }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   unsigned long long MappingRevision() const override { return input ? input->MappingRevision() : 0; }
   IGeometrySource* PassthroughSource() const override { return input; }
   Mat4 GetInstanceGroupMatrix() const override { return input ? input->GetInstanceGroupMatrix() : Mat4::Identity(); }
   const std::vector<Mat4>* InstanceTransformOverride() const override
   {
      return mHasInstances ? &mInstances : (input ? input->InstanceTransformOverride() : nullptr);
   }
   const std::vector<unsigned char>* InstanceSelection() const override
   {
      return input ? input->InstanceSelection() : nullptr;
   }
   unsigned long long InstanceSelectionRevision() const override
   {
      return input ? input->InstanceSelectionRevision() : 0;
   }
   const std::vector<Particle>* GetPointCloud() override;
   unsigned long long PointCloudRevision() override;
   float PointBaseSize() const override { return input ? input->PointBaseSize() : 1.0f; }
   const GpuPoints* GetGpuPoints() override;
   const Polyline* GetCurve() override;
   unsigned long long CurveStamp() override;

   IGeometrySource* input = nullptr;
   IGeometrySource* path = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return slot == 0 ? &input : slot == 1 ? &path : nullptr;
   }
   const char* InputLabel(int slot) const override { return slot == 0 ? "in" : "path"; }
   const std::string& CookWarning() const override { return mCookWarning; }
   const std::string& Status() const { return mStatus; }

   // IRigged: the bones are joints 0..bones-1, the chain's end is one more
   // joint ("tip") with no weight, so IK can reach with it.
   bool Poseable() const override { return mPkg.joints.size() >= 2; }
   int JointCount() const override { return (int)mPkg.joints.size(); }
   const std::string& JointName(int j) const override { return mPkg.joints[j].name; }
   int JointParent(int j) const override { return mPkg.joints[j].parent; }
   bool JointMatrix(int j, float out[16]) override;
   RigPose& Pose() override { return pose; }

   int axis = 1;              // 0 X, 1 Y, 2 Z (no path patched)
   int bones = 8;             // 2..32
   float envelope = 1.0f;     // falloff radius, in bone lengths
   float curl = 0.0f;         // degrees over the whole chain
   float curlDirection = 0.0f;// degrees about the chain: which way it curls
   float twist = 0.0f;        // degrees over the whole chain
   float waveAmount = 0.0f;   // degrees per bone
   float waveLength = 1.0f;   // chains per wave
   float waveSpeed = 1.0f;    // waves per second, with the transport
   float wavePhase = 0.0f;    // 0-1
   RigPose pose;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("axis", axis); v.Int("bones", bones); v.Float("envelope", envelope);
      v.Float("curl", curl); v.Float("curlDirection", curlDirection); v.Float("twist", twist);
      v.Float("waveAmount", waveAmount); v.Float("waveLength", waveLength);
      v.Float("waveSpeed", waveSpeed); v.Float("wavePhase", wavePhase);
      pose.Visit(v);
      // Encoded only when something reads or writes it, as Sculpt's data.
      if (!mPaintEncoded)
      {
         mPaintData = EncodePaint();
         mPaintDecodedFrom = mPaintData;
         mPaintEncoded = true;
      }
      v.Text("weights", mPaintData);
   }

   // Weight paint (the 3D View's Pose mode, W): painted amounts add to the
   // envelope's, per point and bone, before the four strongest are kept. Made
   // for one point and bone count; a change warns and paints nothing until
   // it comes back or the paint is cleared.
   int paintBone = -1;   // not saved: tint the mesh by this bone's weight
   size_t PointCount() { Update(); return mEnv.size() / (size_t)std::max(1, mEnvBones); }
   // The posed points, mesh space, and their normals (zero for curve points
   // and instance origins).
   const std::vector<float>& PosedPoints() { Update(); return mPosed; }
   const std::vector<float>& PosedNormals() { Update(); return mPosedN; }
   bool Paintable();
   // Moves each point's weight for the bone toward target (0 or 1) by its amount.
   void Paint(const std::vector<std::pair<unsigned int, float>>& hits, int bone, float target);
   bool HasPaint() const;
   void ClearPaint();

private:
   // Rebuilds the bones and weights when the input or the chain changed, then
   // re-poses when any pose input moved. Cheap when nothing did.
   void Update();
   void BuildRest();
   void ApplySkin(const std::vector<float>& skin);
   void Reweight();
   std::string EncodePaint() const;
   void DecodePaint();

   std::string mCookWarning, mStatus;
   int mLastCookFrame = -1;

   // The chain as joints (Skinning's format; no vertices, weights kept here).
   GltfImport::GltfDecodePackage mPkg;
   std::vector<double> mRestKey, mPoseKey;
   std::vector<float> mJointWorld;

   // What bends, as one list of points with four bones each: the mesh's
   // vertices, then the cloud's points, then the curve's, then each
   // instance's origin.
   std::vector<unsigned short> mJoints;
   std::vector<float> mWeights;
   // The envelope's weight per point and bone (before the paint), each
   // point's nearest bone, and where the points are after the pose.
   std::vector<float> mEnv;
   std::vector<unsigned short> mNearest;
   int mEnvBones = 0;
   std::vector<float> mPosed, mPosedN;
   // The paint, per point and bone, for mPaintPoints x mPaintBones (0: none).
   std::vector<float> mPaint;
   int mPaintPoints = 0, mPaintBones = 0;
   unsigned long long mPaintRevision = 0, mWeightedRevision = 0;
   std::string mPaintData, mPaintDecodedFrom;
   bool mPaintEncoded = true;
   size_t mMeshCount = 0, mCloudCount = 0, mCurveCount = 0, mInstanceCount = 0;

   Mesh mMesh;
   std::vector<Particle> mCloud;
   Polyline mCurve;
   std::vector<Mat4> mInstances;
   bool mHasInstances = false;
   unsigned long long mMeshRevision = 0, mCloudRevision = 0, mCurveRevision = 0;
};
