#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// An animated water surface, built from summed Gerstner waves.
//
// Deliberately not a simulation: every vertex is a closed-form function of its
// position and the transport time, so there is no state to keep, no timestep to
// keep stable, and pausing or rewinding the transport lands on exactly the same
// surface every time. That is what makes it cheap enough to leave running.
class OceanNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new OceanNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned long long SurfaceTextureRevision() const override { return mTextureInput.Revision(); }

   ImageCable& TextureInput() { return mTextureInput; }
   const char* InputLabel(int) const override { return "texture"; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   int resolution = 96;
   float size = 4.0f;
   float amplitude = 0.12f;
   float wavelength = 2.0f;
   float steepness = 1.0f;
   float direction = 0.6f;
   float choppiness = 1.0f;
   int octaves = 4;
   float speed = 1.0f;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float uniformScale = 1.0f;

   float color[3] = { 0.12f, 0.30f, 0.42f };
   float metallic = 0.05f;
   float roughness = 0.12f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 0.2f, 0.5f, 0.7f };
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
      v.Int("resolution", resolution); v.Float("size", size);
      v.Float("amplitude", amplitude); v.Float("wavelength", wavelength);
      v.Float("steepness", steepness); v.Float("direction", direction);
      v.Float("choppiness", choppiness); v.Int("octaves", octaves);
      v.Float("speed", speed);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("scale", uniformScale);
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

private:
   void RebuildIfNeeded();

   Mesh mMesh;
   unsigned long long mMeshRevision = 0;

   // The surface is rebuilt whenever the shape parameters change *or* the
   // transport moves, so unlike the static primitives this legitimately
   // re-uploads while playing. It still costs nothing while paused.
   int mBuiltResolution = -1, mBuiltOctaves = -1;
   float mBuiltSize = -1, mBuiltAmp = -1, mBuiltLength = -1;
   float mBuiltSteep = -1, mBuiltDir = -999, mBuiltChop = -1;
   double mBuiltTime = -1.0;

   ImageCable mTextureInput;
   int mLastCookFrame = -1;
};
