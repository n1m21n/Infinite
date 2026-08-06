#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Mesh.h"

// A node that supplies geometry to a Render node. Geometry travels down its own
// kind of cable - it is neither an image nor a control value - so Render's
// inputs hold IGeometrySource pointers rather than ImageCables, the same
// pattern Math uses for modulators.
class IGeometrySource
{
public:
   virtual ~IGeometrySource() {}

   // Mesh in object space, plus the model matrix that places it in the scene.
   virtual const Mesh& GetMesh() = 0;
   virtual Mat4 GetModelMatrix() const = 0;
   virtual void GetMaterial(float outColor[3], float& outMetallic, float& outRoughness,
                            float& outOpacity, int& outShading) const = 0;
   // Optional texture applied to the surface (0 when none is patched in).
   virtual unsigned int GetSurfaceTexture() { return 0; }
};

// --- Geometry -----------------------------------------------------------
class GeometryNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new GeometryNode(); }
   static const std::vector<std::string>& ShapeNames();
   static const std::vector<std::string>& ShadingNames();

   ~GeometryNode() override;

   // Geometry nodes have no image output; the preview shows a small solo render.
   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mPreview.w; }
   int GetOutputHeight() const override { return mPreview.h; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   Mat4 GetModelMatrix() const override;
   void GetMaterial(float outColor[3], float& outMetallic, float& outRoughness,
                    float& outOpacity, int& outShading) const override;
   unsigned int GetSurfaceTexture() override;

   ImageCable& TextureInput() { return mTextureInput; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   int shape = 1;        // cube by default
   int detail = 24;      // segments / rings
   int sides = 16;
   float tubeRadius = 0.4f;
   int knotP = 2;
   int knotQ = 3;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
   float uniformScale = 1.0f;
   float spinY = 0.0f;   // extra rotation per beat, so shapes can turn on their own

   float color[3] = { 0.85f, 0.86f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;      // lit / normals / uv / wireframe-ish flat

private:
   void RebuildIfNeeded();

   Mesh mMesh;
   int mBuiltShape = -1, mBuiltDetail = -1, mBuiltSides = -1, mBuiltP = -1, mBuiltQ = -1;
   float mBuiltTube = -1.0f;

   ImageCable mTextureInput;
   GLUtil::Fbo mPreview;
   int mLastCookFrame = -1;
   bool mPreviewFailed = false;
};

// --- Render 3D ----------------------------------------------------------
// Rasterises up to four geometry inputs into a texture with a depth buffer,
// a camera and two lights. This is the bridge from 3D back into the 2D graph:
// its output is an ordinary image cable.
class Render3DNode : public INode
{
public:
   static const int kSlots = 4;

   static INode* Create() { return new Render3DNode(); }
   static const std::vector<std::string>& ProjectionNames();

   ~Render3DNode() override;

   unsigned int GetOutputTexture() override { return mColorTex; }
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   void CookIfNeeded(int frameId) override;

   IGeometrySource* geometry[kSlots] = { nullptr, nullptr, nullptr, nullptr };

   float width = 1024.0f;
   float height = 1024.0f;
   int projection = 0;      // perspective / orthographic
   float fov = 45.0f;
   float orthoHeight = 1.5f;
   float camDistance = 3.0f;
   float camAzimuth = 0.6f;
   float camElevation = 0.4f;
   float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
   float nearPlane = 0.05f;
   float farPlane = 100.0f;

   float lightAzimuth = 0.9f;
   float lightElevation = 0.9f;
   float lightColor[3] = { 1.0f, 0.98f, 0.94f };
   float lightIntensity = 1.2f;
   float ambientColor[3] = { 0.28f, 0.32f, 0.42f };
   float rimIntensity = 0.35f;

   float bgColor[3] = { 0.04f, 0.04f, 0.06f };
   float bgOpacity = 1.0f;
   bool depthTest = true;
   bool backfaceCull = true;

private:
   bool EnsureResources(int w, int h);
   bool EnsureShader();

   unsigned int mFbo = 0;
   unsigned int mColorTex = 0;
   unsigned int mDepthBuffer = 0;
   int mWidth = 0, mHeight = 0;

   unsigned int mProgram = 0;
   bool mShaderTried = false;
   unsigned int mVao = 0, mVbo = 0, mIbo = 0;
   int mLastCookFrame = -1;
};
