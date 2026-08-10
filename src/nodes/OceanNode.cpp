#include "OceanNode.h"

#include <cmath>

#include "Transport.h"

void OceanNode::RebuildIfNeeded()
{
   // Quantised so a frame that advances the transport by a negligible amount
   // does not rebuild a hundred thousand vertices for a sub-pixel change.
   const double now = Transport::Instance().Beats() * (double)speed;
   const double quantised = std::floor(now * 240.0) / 240.0;

   if (mBuiltResolution == resolution && mBuiltOctaves == octaves &&
       mBuiltSize == size && mBuiltAmp == amplitude && mBuiltLength == wavelength &&
       mBuiltSteep == steepness && mBuiltDir == direction && mBuiltChop == choppiness &&
       mBuiltTime == quantised && !mMesh.Empty())
      return;

   mMesh = MeshOps::Ocean(resolution, size, amplitude, wavelength, steepness,
                          direction * 3.14159265f / 180.0f, choppiness, octaves, (float)quantised);

   mBuiltResolution = resolution;
   mBuiltOctaves = octaves;
   mBuiltSize = size;
   mBuiltAmp = amplitude;
   mBuiltLength = wavelength;
   mBuiltSteep = steepness;
   mBuiltDir = direction;
   mBuiltChop = choppiness;
   mBuiltTime = quantised;
   mMeshRevision = NextMeshRevision();
}

const Mesh& OceanNode::GetMesh()
{
   RebuildIfNeeded();
   return mMesh;
}

unsigned long long OceanNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Mat4 OceanNode::GetModelMatrix() const
{
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   return Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
}

Material OceanNode::GetMaterial() const
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

unsigned int OceanNode::GetSurfaceTexture()
{
   return mTextureInput.IsConnected() && mTextureInput.GetSource()
             ? mTextureInput.GetSource()->GetOutputTexture()
             : 0;
}

void OceanNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   RebuildIfNeeded();
   if (mTextureInput.IsConnected())
      mTextureInput.Pull(frameId);
}
