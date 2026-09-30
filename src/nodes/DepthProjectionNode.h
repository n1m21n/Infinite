#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Mesh.h"
#include "Geometry3DNodes.h"

// DepthProjectionNode: Unprojects a 2D depth map (and optional color map) into a
// 3D point cloud or connected triangle mesh.
//
// Supports:
// - Pinhole camera intrinsics unprojection (FOV, aspect ratio, focal scale, optical center)
// - Planar Z-depth heightmap displacement
// - Radial / spherical distance unprojection (LiDAR / Fisheye)
// - Cylindrical / 360 panoramic unprojection
// - Depth remapping, power curves, and near/far clipping
// - Connected triangle mesh generation with depth discontinuity edge tearing
// - Surface normal estimation from depth gradients
// - Depth colormaps (Turbo, Viridis, Thermal, Ocean) or RGB color texture sampling
class DepthProjectionNode : public INode, public IGeometrySource
{
public:
   enum Projection
   {
      kPerspective = 0, // Pinhole camera unprojection (FOV, aspect, focal scale)
      kPlanar,          // Planar Z-depth displacement / heightmap
      kRadial,          // Radial distance from camera center (LiDAR)
      kCylindrical,     // Cylindrical 360 panoramic unprojection
      kProjectionCount
   };
   static const std::vector<std::string>& ProjectionNames();

   enum OutputType
   {
      kPoints = 0,      // 3D Point Cloud (Particle vector)
      kMesh,            // Connected Triangulated Surface Mesh with edge tearing
      kOutputTypeCount
   };
   static const std::vector<std::string>& OutputTypeNames();

   enum DepthSource
   {
      kLuminance = 0,
      kRed,
      kAlpha,
      kGreen,
      kBlue,
      kInverseLuma,     // Inverted luminance (white = near, black = far vs black = near, white = far)
      kDepthSourceCount
   };
   static const std::vector<std::string>& DepthSourceNames();

   enum ColorMode
   {
      kColorInput = 0,  // Sample RGB from color input texture (or white/tint if unplugged)
      kTurbo,           // Colormap: Turbo rainbow gradient
      kViridis,         // Colormap: Viridis purple-teal-yellow
      kThermal,         // Colormap: Thermal / Ironbow
      kOcean,           // Colormap: Deep Ocean cyan-blue-white
      kSolidTint,       // Solid tint only
      kColorModeCount
   };
   static const std::vector<std::string>& ColorModeNames();

   static INode* Create() { return new DepthProjectionNode(); }
   ~DepthProjectionNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   // IGeometrySource: Point cloud
   const std::vector<Particle>& GetPoints() { return mPoints; }
   unsigned long long PointRevision() { return mPointRevision; }
   const std::vector<Particle>* GetPointCloud() override
   {
      if (bypassed || outputType == kMesh)
         return nullptr;
      return &mPoints;
   }
   unsigned long long PointCloudRevision() override
   {
      return (bypassed || outputType == kMesh) ? 0 : mPointRevision;
   }
   float PointBaseSize() const;

   // IGeometrySource: Mesh
   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;

   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned long long MaterialRevision() const;
   unsigned int GetSurfaceTexture() override;
   unsigned long long SurfaceTextureRevision() const override { return mColorInput.Revision(); }
   MappingTransform GetMappingTransform() const override { return MappingTransform(); }

   INode* BypassSource() override { return nullptr; }
   size_t PointCount() const { return mPoints.size(); }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   ImageCable& DepthInput() { return mDepthInput; }
   ImageCable& ColorInput() { return mColorInput; }
   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "depth";
      if (slot == 1) return "color";
      return nullptr;
   }

   // --- Parameters ---
   int projection = kPerspective;
   int outputType = kPoints;
   int depthSource = kLuminance;
   int colorMode = kColorInput;

   // Camera Intrinsics
   float fov = 60.0f;
   float customAspect = 1.77778f;
   bool autoAspect = true;
   float focalScale = 1.0f;
   float principalPointX = 0.0f;
   float principalPointY = 0.0f;

   // Depth Range & Remap
   float nearDepth = 0.1f;
   float farDepth = 10.0f;
   float depthScale = 1.0f;
   float depthCurve = 1.0f;
   float clipNear = 0.0f;
   float clipFar = 1.0f;
   float planarWidth = 4.0f;
   float planarHeight = 3.0f;

   // Sampling & Geometry
   int density = 128;
   float pointSize = 1.0f;
   float edgeTearThreshold = 0.25f; // Max depth delta across edge before splitting triangle
   bool computeNormals = true;
   float tint[3] = { 1.0f, 1.0f, 1.0f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;

   // Transform
   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("projection", projection);
      v.Int("outputType", outputType);
      v.Int("depthSource", depthSource);
      v.Int("colorMode", colorMode);
      v.Float("fov", fov);
      v.Float("aspect", customAspect);
      v.Bool("autoAspect", autoAspect);
      v.Float("focalScale", focalScale);
      v.Float("principalPointX", principalPointX);
      v.Float("principalPointY", principalPointY);
      v.Float("nearDepth", nearDepth);
      v.Float("farDepth", farDepth);
      v.Float("depthScale", depthScale);
      v.Float("depthCurve", depthCurve);
      v.Float("clipNear", clipNear);
      v.Float("clipFar", clipFar);
      v.Float("planarWidth", planarWidth);
      v.Float("planarHeight", planarHeight);
      v.Int("density", density);
      v.Float("pointSize", pointSize);
      v.Float("edgeTear", edgeTearThreshold);
      v.Bool("computeNormals", computeNormals);
      v.Color("tint", tint);
      v.Float("metallic", metallic);
      v.Float("roughness", roughness);
      v.Float("opacity", opacity);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("rotX", rotX); v.Float("rotY", rotY); v.Float("rotZ", rotZ);
      v.Float("scaleX", scaleX); v.Float("scaleY", scaleY); v.Float("scaleZ", scaleZ);
   }

private:
   bool EnsureResources(int n);

   ImageCable mDepthInput;
   ImageCable mColorInput;

   std::vector<Particle> mPoints;
   Mesh mMesh;

   std::vector<unsigned char> mDepthPixels;
   std::vector<unsigned char> mColorPixels;

   unsigned long long mPointRevision = 0;
   unsigned long long mMeshRevision = 0;
   mutable unsigned long long mMaterialRevision = 0;
   mutable size_t mLastMaterialHash = 0;

   GLUtil::Fbo mDepthFbo;
   GLUtil::Fbo mColorFbo;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};
