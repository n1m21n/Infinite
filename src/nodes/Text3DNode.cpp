#include "Text3DNode.h"

#include <OpenGL/gl3.h>

#include "Platform.h"
#include "Transport.h"

Text3DNode::~Text3DNode()
{
   GLUtil::DestroyFbo(mPreview);
}

void Text3DNode::RebuildIfNeeded()
{
   if (text == mBuiltText && fontName == mBuiltFont && depth == mBuiltDepth &&
       bevel == mBuiltBevel && letterSpacing == mBuiltSpacing)
      return;

   mBuiltText = text;
   mBuiltFont = fontName;
   mBuiltDepth = depth;
   mBuiltBevel = bevel;
   mBuiltSpacing = letterSpacing;

   mMesh.vertices.clear();
   mMesh.indices.clear();

   std::vector<Platform::TextContour> contours;
   std::string error;
   if (!Platform::GetTextOutlines(text, fontName, letterSpacing, contours, error))
   {
      mStatus = error.empty() ? "could not build outlines" : error;
      mMeshRevision = NextMeshRevision();
      return;
   }

   std::vector<MeshOps::Contour2D> converted;
   converted.reserve(contours.size());
   for (const Platform::TextContour& c : contours)
   {
      MeshOps::Contour2D out;
      out.points = c.points;
      converted.push_back(std::move(out));
   }

   mMesh = MeshOps::ExtrudeContours(converted, depth, bevel);
   mMeshRevision = NextMeshRevision();
   mStatus = std::to_string(contours.size()) + " contours, " +
             std::to_string(mMesh.indices.size() / 3) + " triangles";
}

const Mesh& Text3DNode::GetMesh()
{
   RebuildIfNeeded();
   return mMesh;
}

unsigned long long Text3DNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Mat4 Text3DNode::GetModelMatrix() const
{
   const float spin = spinY * (float)Transport::Instance().Beats();
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   m = Mat4::Multiply(Mat4::RotationZ(rotZ), m);
   m = Mat4::Multiply(Mat4::RotationY(rotY + spin), m);
   m = Mat4::Multiply(Mat4::RotationX(rotX), m);
   m = Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
   return m;
}

Material Text3DNode::GetMaterial() const
{
   Material m;
   m.color[0] = color[0]; m.color[1] = color[1]; m.color[2] = color[2];
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   m.shading = shading;
   m.emissionColor[0] = emissionColor[0];
   m.emissionColor[1] = emissionColor[1];
   m.emissionColor[2] = emissionColor[2];
   m.emission = emission;
   m.ior = ior;
   m.transmission = transmission;
   m.transmissionRoughness = transmissionRoughness;
   m.specular = specular;
   m.clearcoat = clearcoat;
   m.clearcoatRoughness = clearcoatRoughness;
   m.subsurface = subsurface;
   m.subsurfaceColor[0] = subsurfaceColor[0];
   m.subsurfaceColor[1] = subsurfaceColor[1];
   m.subsurfaceColor[2] = subsurfaceColor[2];
   m.subsurfaceRadius = subsurfaceRadius;
   return m;
}

unsigned int Text3DNode::GetSurfaceTexture()
{
   return mTextureInput.IsConnected() && mTextureInput.GetSource()
             ? mTextureInput.GetSource()->GetOutputTexture()
             : 0;
}

unsigned int Text3DNode::GetOutputTexture()
{
   return GLUtil::FboTexture(mPreview);
}

void Text3DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   RebuildIfNeeded();
   if (mTextureInput.IsConnected())
      mTextureInput.Pull(frameId);
}
