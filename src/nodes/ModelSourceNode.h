#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// Loads a mesh off disk and feeds it into the geometry graph, so an imported
// model can be run through the same operators as a generated primitive.
//
// Decoding goes through ModelIO in the platform shim rather than a bundled
// importer, matching how images and video are handled: OBJ, PLY, STL, USD and
// USDZ come free with the OS. glTF and FBX are not covered.
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

   const Mesh& GetMesh() override { return mMesh; }
   unsigned long long MeshRevision() override { return mMeshRevision; }
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned long long SurfaceTextureRevision() const override { return mTextureInput.Revision(); }

   ImageCable& TextureInput() { return mTextureInput; }
   const char* InputLabel(int) const override { return "texture"; }

   bool Load(const std::string& path);
   const std::string& Path() const { return mPath; }
   const std::string& Status() const { return mStatus; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   // Imported files arrive at wildly different scales - millimetres, metres,
   // whatever the authoring tool used. Fitting to a unit box on load means a
   // model always shows up framed rather than invisible or filling the scene.
   bool normalizeScale = true;
   bool recenter = true;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float uniformScale = 1.0f;
   float spinY = 0.0f;

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

   Mesh mMesh;
   unsigned long long mMeshRevision = 0;
   std::string mPath;
   std::string mStatus = "no model loaded";

   ImageCable mTextureInput;
   GLUtil::Fbo mPreview;
   int mLastCookFrame = -1;
};
