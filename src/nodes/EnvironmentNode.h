#pragma once

#include <string>

#include "INode.h"

// Loads an equirectangular HDR image (a "chrome ball unwrapped onto a
// rectangle") and hands it to Render 3D as a real, image-based environment -
// in place of, or alongside, Render 3D's own fixed sky/horizon/ground
// gradient.
//
// Two formats, two decoders: Radiance .hdr goes through stb_image's float
// loader (ImageIO does not read it at all), and .exr goes through ImageIO
// itself via Platform::LoadImageFloatRGB, which decodes into an
// extended-range linear colour space rather than clamping to 8-bit sRGB the
// way ImageSourceNode's ordinary image loader does - that clamp is exactly
// why an HDRI needs its own node instead of just plugging an Image Source
// into Render 3D's env pin.
//
// The texture is uploaded once as a mipmapped float sampler. Render 3D reads
// mip 0 straight for the background and for sharp reflections, and reads
// higher mips (roughness-scaled) as a cheap stand-in for both blurred
// reflections and diffuse irradiance. That is a real simplification: it is
// an averaged-mip approximation, not prefiltered split-sum IBL, so specular
// reflections on very glossy metal will look softer than a real-time
// renderer with a proper GGX prefilter. Noted as a follow-up in the README.
class EnvironmentNode : public INode
{
public:
   static INode* Create() { return new EnvironmentNode(); }
   ~EnvironmentNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   void CookIfNeeded(int frameId) override;
   // Only changes when Upload()/EnsurePlaceholder() actually re-upload -
   // lets Render3DNode's scene cache treat a static HDRI as stable rather
   // than assuming it moved every frame. See ImageSourceNode for the same pattern.
   unsigned long long TextureRevision() const override { return mRevision; }

   // Loads `path` (.hdr) into the texture. Returns false and sets
   // LastError() on failure.
   bool Load(const std::string& path);
   bool LoadViaDialog();
   const std::string& LastError() const { return mLastError; }
   const std::string& LoadedPath() const { return mLoadedPath; }

   void ReloadFromPath()
   {
      if (!mLoadedPath.empty())
      {
         const std::string p = mLoadedPath;
         Load(p);
      }
   }

   // The equirectangular texture itself, for Render 3D to sample directly
   // (background and reflections both read this rather than GetOutputTexture,
   // whose only job is the node's own preview thumbnail).
   unsigned int GetEnvironmentTexture();
   // Highest valid mip level of that texture - Render 3D scales this by
   // roughness to pick a blur amount.
   float MaxLod() const { return mMaxLod; }
   bool HasImage() const { return mHasImage; }

   float intensity = 1.0f;
   float rotation = 0.0f; // degrees, about the world Y axis

   // Sun extraction: the image's light above `sunThreshold` is taken out of the sky and handed to
   // Render 3D as one sun light, so a bright HDRI sun casts a real shadow. 0 = off, so a scene
   // made before this lights the same; Blender's own default is 10.
   float sunThreshold = 0.0f;
   float sunAngle = 0.526f;      // degrees across the sun disc; a wider sun softens its shadow
   bool sunShadow = true;
   int sunShadowRes = 0;         // 0 = the look's quality, 1 / 2 / 3 = 1024 / 2048 / 4096
   float sunShadowFilter = 1.0f;
   // The extracted sun, before `rotation`: direction toward it, and its light as a linear colour.
   // False when nothing in the image crosses the threshold.
   bool Sun(float dir[3], float light[3]) const;

   std::string pathInput; // bound to the ImGui text field

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mLoadedPath);
      v.Float("intensity", intensity);
      v.Float("rotation", rotation);
      v.Float("sunThreshold", sunThreshold); v.Float("sunAngle", sunAngle);
      v.Bool("sunShadow", sunShadow); v.Int("sunShadowRes", sunShadowRes);
      v.Float("sunShadowFilter", sunShadowFilter);
   }

private:
   void EnsurePlaceholder();
   void Upload(const float* pixels, int w, int h);

   unsigned int mTex = 0;
   int mWidth = 0;
   int mHeight = 0;
   float mMaxLod = 0.0f;
   bool mHasImage = false;
   std::string mLoadedPath;
   std::string mLastError;
   unsigned long long mRevision = 0;
   float mUploadedThreshold = -1.0f;   // the threshold the texture was cut at
   bool mHasSun = false;
   float mSunDir[3] = { 0, 1, 0 };
   float mSunLight[3] = { 0, 0, 0 };
};
