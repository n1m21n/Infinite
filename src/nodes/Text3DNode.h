#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// Extruded text as real geometry, so a title can be lit, reflected and run
// through the geometry operators rather than composited as a flat image.
//
// The 2D Text node stays as it is: rendering a string to a texture is the right
// answer for overlays, and this is the right answer for type in a scene.
class Text3DNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new Text3DNode(); }

   ~Text3DNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;

   ImageCable& TextureInput() { return mTextureInput; }
   const char* InputLabel(int) const override { return "texture"; }

   size_t TriangleCount() const { return mMesh.indices.size() / 3; }
   const std::string& Status() const { return mStatus; }

   std::string text = "Infinite";
   std::string fontName = "Helvetica";
   float depth = 0.2f;
   float bevel = 0.0f;
   float letterSpacing = 0.0f;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float uniformScale = 1.0f;
   float spinY = 0.0f;

   float color[3] = { 0.9f, 0.9f, 0.93f };
   float metallic = 0.2f;
   float roughness = 0.35f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("text", text); v.Text("font", fontName);
      v.Float("depth", depth); v.Float("bevel", bevel);
      v.Float("tracking", letterSpacing);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("rotX", rotX); v.Float("rotY", rotY); v.Float("rotZ", rotZ);
      v.Float("scale", uniformScale); v.Float("spin", spinY);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   void RebuildIfNeeded();

   Mesh mMesh;
   unsigned long long mMeshRevision = 0;
   std::string mStatus = "no text";

   // Rebuilding runs CoreText and an ear-clip pass, which is far too expensive
   // to repeat every frame; it only reruns when one of these actually changes.
   std::string mBuiltText;
   std::string mBuiltFont;
   float mBuiltDepth = -1.0f, mBuiltBevel = -1.0f, mBuiltSpacing = -999.0f;

   ImageCable mTextureInput;
   GLUtil::Fbo mPreview;
   int mLastCookFrame = -1;
};
