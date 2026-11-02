#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "Geometry3DNodes.h"
#include "GltfImport.h"

// Loads a mesh off disk and feeds it into the geometry graph, so an imported
// model can be run through the same operators as a generated primitive.
//
// Decoding goes through ModelIO in the platform shim rather than a bundled
// importer, matching how images and video are handled: OBJ, PLY, STL, USD and
// USDZ come free with the OS. glTF goes through core/GltfImport; FBX is not
// covered.
//
// A skinned or morphing glTF poses on the CPU (core/Skinning.h): Clip picks an
// animation, Speed / Phase / Sync drive its time from the transport, and each
// morph target gets a weight. All of them are params, so modulation can scrub
// a clip or push a shape key. The rest pose sets the unit-box fit, and every
// posed frame reuses it so the model doesn't breathe as it moves.
//
// A .ply with no faces (a scan, a Gaussian-splat export) loads as a point
// cloud instead (core/PlyPoints.h, read the same on every platform): the mesh
// is empty, GetPointCloud() carries the points with the file's colours, and
// GetGpuPoints() uploads them once per load or point-size change.
class ModelSourceNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new ModelSourceNode(); }

   ~ModelSourceNode() override;

   // Like GeometryNode, this emits no image; the preview is a text summary.
   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override
   {
      if (bypassed)
      {
         static const Mesh kEmptyMesh;
         return kEmptyMesh;
      }
      UpdatePose();
      return mMesh;
   }
   unsigned long long MeshRevision() override
   {
      if (bypassed)
         return 0;
      UpdatePose();
      return mMeshRevision;
   }
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned long long MaterialRevision() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned long long SurfaceTextureRevision() const override { return mTextureInput.Revision(); }

   const std::vector<Particle>* GetPointCloud() override;
   unsigned long long PointCloudRevision() override;
   const GpuPoints* GetGpuPoints() override;

   ImageCable& TextureInput() { return mTextureInput; }
   const char* InputLabel(int) const override { return "texture"; }

   bool Load(const std::string& path);
   const std::string& Path() const { return mPath; }
   const std::string& Status() const { return mStatus; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }
   bool IsPointCloud() const { return mIsPoints; }
   size_t PointCount() const { return mIsPoints ? mPoints.size() : 0; }

   static constexpr int kMaxMorphs = 8;
   bool HasRig() const { return !mRig.joints.empty() || !mRig.morphDeltas.empty(); }
   bool HasSkin() const { return !mRig.joints.empty(); }
   // "rest pose" followed by the file's clip names.
   std::vector<std::string> ClipNames() const;
   int MorphCount() const { return (int)std::min<size_t>(mRig.morphNames.size(), kMaxMorphs); }
   const std::string& MorphName(int i) const { return mRig.morphNames[i]; }
   int JointCount() const { return (int)mRig.joints.size(); }
   // Seconds into the current clip as last posed (0 in the rest pose).
   float ClipTime() const { return mPosedTime; }

   // Imported files arrive at wildly different scales - millimetres, metres,
   // whatever the authoring tool used. Fitting to a unit box on load means a
   // model always shows up framed rather than invisible or filling the scene.
   bool normalizeScale = true;
   bool recenter = true;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float uniformScale = 1.0f;
   float spinY = 0.0f;
   // Point clouds only: each point's sprite half-size, in the fitted model's
   // units (a unit box when "fit to unit size" is on).
   float pointSize = 0.005f;

   // Rig (skinned / morphing glTF only). clip 0 is the rest pose, 1..n the
   // file's animations. Sync 0 runs the clip in transport seconds times
   // speed; sync 1 fits one loop of the clip to clipBeats beats. Phase (0-1
   // of the clip) is added on top, so modulating it scrubs.
   int clip = 0;
   float clipSpeed = 1.0f;
   float clipPhase = 0.0f;
   int clipSync = 0;
   float clipBeats = 4.0f;
   float morph[kMaxMorphs] = {};

   float color[3] = { 0.82f, 0.83f, 0.86f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;
   // Fresnel/dielectric response, independent of metallic/roughness - 1.5
   // matches glass and Blender's Principled BSDF default.
   float ior = 1.5f;
   // See-through / refractive path. Distinct from `opacity`, which stays a
   // cutout/dither alpha - transmission is the actual glass-like blend.
   // Screen-space approximated (see Render3D's transmission pass), not
   // raytraced: it will not look correct through thick or overlapping
   // transmissive geometry.
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   // Dielectric specular reflectance at normal incidence, independent of
   // metallic - Blender's "Specular" / "Specular IOR Level" slider.
   float specular = 0.5f;
   // Second, untinted Fresnel-weighted specular lobe layered on top - car
   // paint / lacquer.
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   // Fake subsurface scattering via wrap lighting (N.L extended past the
   // terminator, tinted by subsurfaceColor) - not true multi-scatter.
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 1.0f, 0.2f, 0.1f };
   float subsurfaceRadius = 0.5f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mPath);
      v.Bool("normalize", normalizeScale); v.Bool("recenter", recenter);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("rotX", rotX); v.Float("rotY", rotY); v.Float("rotZ", rotZ);
      v.Float("scale", uniformScale); v.Float("spin", spinY);
      v.Float("pointSize", pointSize);
      v.Int("clip", clip); v.Float("clipSpeed", clipSpeed); v.Float("clipPhase", clipPhase);
      v.Int("clipSync", clipSync); v.Float("clipBeats", clipBeats);
      static const char* const kMorphKeys[kMaxMorphs] = { "morph0", "morph1", "morph2", "morph3",
                                                          "morph4", "morph5", "morph6", "morph7" };
      for (int i = 0; i < kMaxMorphs; i++)
         v.Float(kMorphKeys[i], morph[i]);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
   }

   // Reloads from whatever path a patch restored. Called after loading.
   void ReloadFromPath()
   {
      if (!mPath.empty())
      {
         const std::string p = mPath;
         Load(p);
      }
   }

private:
   void Normalize();

   // Re-poses the mesh when the clip, its sampled time or a morph weight
   // moved since the last pose; cheap when nothing did.
   void UpdatePose();
   void ApplyFit(std::vector<Vertex>& out, const std::vector<Platform::ModelVertex>& src) const;

   // Writes pointSize into every point's scale when it changed since the
   // last call, moving the point revision.
   void ApplyPointSize();

   Mesh mMesh;
   // Rest vertices plus joints, weights, clips and morphs; empty for a
   // static model. Images are not kept here.
   GltfImport::GltfDecodePackage mRig;
   float mFitCentre[3] = { 0, 0, 0 };
   float mFitScale = 1.0f;
   std::vector<float> mPoseKey;
   float mPosedTime = 0.0f;
   unsigned long long mMeshRevision = 0;
   bool mIsPoints = false;
   std::vector<Particle> mPoints;
   unsigned long long mPointRevision = 0;
   float mAppliedPointSize = -1.0f;
   GpuPointsGL::Target mGpuTarget;
   GpuPoints mGpuPoints;
   unsigned long long mGpuUploadedRevision = 0;
   bool mGpuValid = false;
   mutable unsigned long long mMaterialRevision = 0;
   mutable size_t mLastMaterialHash = 0;
   std::string mPath;
   std::string mStatus = "no model loaded";

   ImageCable mTextureInput;
   GLUtil::Fbo mPreview;
   int mLastCookFrame = -1;
};
