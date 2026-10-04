#include "DepthProjectionNode.h"

#include <cmath>
#include <cstring>

static const std::vector<std::string> kProjectionNames = {
   "Perspective", "Planar (Ortho)", "Radial (LiDAR)", "Cylindrical (360)"
};

static const std::vector<std::string> kOutputTypeNames = {
   "Points", "Mesh"
};

static const std::vector<std::string> kDepthSourceNames = {
   "Luminance", "Red", "Alpha", "Green", "Blue", "Inv Luminance"
};

static const std::vector<std::string> kColorModeNames = {
   "Color Input", "Turbo", "Viridis", "Thermal", "Ocean", "Solid Tint"
};

const std::vector<std::string>& DepthProjectionNode::ProjectionNames() { return kProjectionNames; }
const std::vector<std::string>& DepthProjectionNode::OutputTypeNames() { return kOutputTypeNames; }
const std::vector<std::string>& DepthProjectionNode::DepthSourceNames() { return kDepthSourceNames; }
const std::vector<std::string>& DepthProjectionNode::ColorModeNames() { return kColorModeNames; }

static inline float Clamp01(float v)
{
   return std::max(0.0f, std::min(1.0f, v));
}

// Colormap evaluation functions (Turbo, Viridis, Thermal, Ocean)
static void EvaluateColormap(int mode, float t, float outRgb[3])
{
   t = Clamp01(t);
   switch (mode)
   {
      case DepthProjectionNode::kTurbo:
      {
         // Google's Turbo colormap polynomial fit
         const float r = 0.1357f + t * ( 4.5974f + t * (-42.3278f + t * (130.5887f + t * (-150.5668f + t * 58.1375f))));
         const float g = 0.0914f + t * ( 2.1856f + t * (  4.8052f + t * (-14.0195f + t * (   4.2109f + t *  2.7747f))));
         const float b = 0.1067f + t * (12.5833f + t * (-90.1921f + t * (274.6893f + t * (-350.6657f + t * 153.4681f))));
         outRgb[0] = Clamp01(r);
         outRgb[1] = Clamp01(g);
         outRgb[2] = Clamp01(b);
         break;
      }
      case DepthProjectionNode::kViridis:
      {
         // Viridis polynomial approximation
         const float r = 0.267f + t * ( 0.004f + t * ( 0.812f + t * (-0.083f)));
         const float g = 0.004f + t * ( 1.408f + t * (-0.569f + t * ( 0.157f)));
         const float b = 0.329f + t * ( 1.139f + t * (-2.062f + t * ( 0.594f)));
         outRgb[0] = Clamp01(r);
         outRgb[1] = Clamp01(g);
         outRgb[2] = Clamp01(b);
         break;
      }
      case DepthProjectionNode::kThermal:
      {
         // Thermal / Ironbow: black -> deep violet -> orange-red -> yellow -> white
         if (t < 0.25f)
         {
            const float s = t / 0.25f;
            outRgb[0] = s * 0.5f; outRgb[1] = 0.0f; outRgb[2] = s * 0.8f;
         }
         else if (t < 0.5f)
         {
            const float s = (t - 0.25f) / 0.25f;
            outRgb[0] = 0.5f + s * 0.5f; outRgb[1] = s * 0.2f; outRgb[2] = 0.8f * (1.0f - s);
         }
         else if (t < 0.75f)
         {
            const float s = (t - 0.5f) / 0.25f;
            outRgb[0] = 1.0f; outRgb[1] = 0.2f + s * 0.6f; outRgb[2] = 0.0f;
         }
         else
         {
            const float s = (t - 0.75f) / 0.25f;
            outRgb[0] = 1.0f; outRgb[1] = 0.8f + s * 0.2f; outRgb[2] = s;
         }
         break;
      }
      case DepthProjectionNode::kOcean:
      {
         // Ocean: deep midnight blue -> teal -> cyan -> white
         if (t < 0.5f)
         {
            const float s = t / 0.5f;
            outRgb[0] = s * 0.1f; outRgb[1] = 0.15f + s * 0.55f; outRgb[2] = 0.4f + s * 0.5f;
         }
         else
         {
            const float s = (t - 0.5f) / 0.5f;
            outRgb[0] = 0.1f + s * 0.9f; outRgb[1] = 0.7f + s * 0.3f; outRgb[2] = 0.9f + s * 0.1f;
         }
         break;
      }
      default:
         outRgb[0] = 1.0f; outRgb[1] = 1.0f; outRgb[2] = 1.0f;
         break;
   }
}

DepthProjectionNode::~DepthProjectionNode()
{
   GLUtil::DestroyFbo(mDepthFbo);
   GLUtil::DestroyFbo(mColorFbo);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool DepthProjectionNode::EnsureResources(int n)
{
   if (mProgram == 0 && !mShaderTried)
   {
      mShaderTried = true;
      mProgram = GLUtil::CompileProgram(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n"
         "void main() { fragColor = texture(uTex, vUv); }\n");
   }
   if (mProgram == 0)
      return false;

   const bool depthOk = GLUtil::EnsureFbo(mDepthFbo, n, n);
   const bool colorOk = GLUtil::EnsureFbo(mColorFbo, n, n);
   return depthOk && colorOk;
}

float DepthProjectionNode::PointBaseSize() const
{
   if (!relativePointSize)
      return 1.0f;
   const int n = std::max(2, std::min(density, 512));
   const float dim = std::min(planarWidth, planarHeight);
   return (dim / (float)n) * 0.5f;
}

const Mesh& DepthProjectionNode::GetMesh()
{
   if (bypassed || outputType == kPoints)
   {
      static const Mesh kEmpty;
      return kEmpty;
   }
   return mMesh;
}

unsigned long long DepthProjectionNode::MeshRevision()
{
   if (bypassed || outputType == kPoints)
      return 0;
   return mMeshRevision;
}

Mat4 DepthProjectionNode::GetModelMatrix() const
{
   Mat4 m = Mat4::Scale(scaleX, scaleY, scaleZ);
   m = Mat4::Multiply(Mat4::RotationZ(rotZ), m);
   m = Mat4::Multiply(Mat4::RotationY(rotY), m);
   m = Mat4::Multiply(Mat4::RotationX(rotX), m);
   m = Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
   return m;
}

Material DepthProjectionNode::GetMaterial() const
{
   // Turbo 0.45 (upstream R482): tint is already baked into every sample's
   // colour and the shader multiplies albedo by it, so report a neutral albedo.
   Material m;
   m.color[0] = 1.0f; m.color[1] = 1.0f; m.color[2] = 1.0f;
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   return m;
}

unsigned long long DepthProjectionNode::MaterialRevision() const
{
   return mMaterialRevision; // Turbo: our Render3D reads the material directly each frame
}

unsigned int DepthProjectionNode::GetSurfaceTexture()
{
   // Turbo 0.45 (upstream R482): the colour input is already baked into the
   // vertex / particle colours; exposing it as a texture applied it twice.
   return 0;
}

void DepthProjectionNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (bypassed || !mDepthInput.IsConnected())
   {
      if (!mPoints.empty() || !mMesh.Empty())
      {
         mPoints.clear();
         mMesh = Mesh();
         mPointRevision = NextMeshRevision();
         mMeshRevision = NextMeshRevision();
      }
      return;
   }

   const unsigned int depthTex = mDepthInput.Pull(frameId);
   if (depthTex == 0)
   {
      if (!mPoints.empty() || !mMesh.Empty())
      {
         mPoints.clear();
         mMesh = Mesh();
         mPointRevision = NextMeshRevision();
         mMeshRevision = NextMeshRevision();
      }
      return;
   }

   const unsigned int colorTex = mColorInput.IsConnected() ? mColorInput.Pull(frameId) : 0;

   const int n = std::max(8, std::min(density, 512));
   if (!EnsureResources(n))
      return;

   // 1. Resample Depth Texture on GPU to n x n
   GLUtil::RunShaderPass(mDepthFbo, mProgram, [this, depthTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, depthTex);
      glUniform1i(glGetUniformLocation(mProgram, "uTex"), 0);
   });

   mDepthPixels.resize((size_t)n * (size_t)n * 4);
   glBindTexture(GL_TEXTURE_2D, mDepthFbo.tex);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, mDepthPixels.data());
   glBindTexture(GL_TEXTURE_2D, 0);

   // 2. Resample Color Texture if present
   const bool hasColorTex = (colorTex != 0);
   if (hasColorTex)
   {
      GLUtil::RunShaderPass(mColorFbo, mProgram, [this, colorTex]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, colorTex);
         glUniform1i(glGetUniformLocation(mProgram, "uTex"), 0);
      });

      mColorPixels.resize((size_t)n * (size_t)n * 4);
      glBindTexture(GL_TEXTURE_2D, mColorFbo.tex);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, mColorPixels.data());
      glBindTexture(GL_TEXTURE_2D, 0);
   }

   // 3. Compute Aspect & Intrinsics
   float aspect = 1.0f;
   if (autoAspect)
   {
      const int inW = mDepthInput.Width();
      const int inH = mDepthInput.Height();
      if (inW > 0 && inH > 0)
         aspect = (float)inW / (float)inH;
      else
         aspect = customAspect;
   }
   else
   {
      aspect = customAspect;
   }
   aspect = std::max(0.01f, aspect);

   const float kDeg2Rad = 3.14159265358979323846f / 180.0f;
   const float tanHalfFov = std::tan(std::max(1.0f, std::min(170.0f, fov)) * 0.5f * kDeg2Rad);

   // Raw normalized depth values and 3D position grid
   struct GridSample
   {
      float px = 0.0f, py = 0.0f, pz = 0.0f;
      float rawDepth = 0.0f;
      float curvedDepth = 0.0f;
      float r = 1.0f, g = 1.0f, b = 1.0f;
      bool valid = false;
      float u = 0.0f, v = 0.0f;
   };

   std::vector<GridSample> grid((size_t)n * (size_t)n);

   for (int gy = 0; gy < n; gy++)
   {
      for (int gx = 0; gx < n; gx++)
      {
         const size_t gIdx = (size_t)gy * (size_t)n + (size_t)gx;
         const size_t pIdx = gIdx * 4;

         const float u = ((float)gx + 0.5f) / (float)n;
         const float v = ((float)gy + 0.5f) / (float)n;

         const float dr = (float)mDepthPixels[pIdx] / 255.0f;
         const float dg = (float)mDepthPixels[pIdx + 1] / 255.0f;
         const float db = (float)mDepthPixels[pIdx + 2] / 255.0f;
         const float da = (float)mDepthPixels[pIdx + 3] / 255.0f;
         const float dluma = 0.2126f * dr + 0.7152f * dg + 0.0722f * db;

         float rawD = 0.0f;
         switch (depthSource)
         {
            case kRed: rawD = dr; break;
            case kAlpha: rawD = da; break;
            case kGreen: rawD = dg; break;
            case kBlue: rawD = db; break;
            case kInverseLuma: rawD = 1.0f - dluma; break;
            default: rawD = dluma; break;
         }

         // Clipping check
         if (rawD < clipNear || rawD > clipFar || da < 0.01f)
         {
            grid[gIdx].valid = false;
            grid[gIdx].rawDepth = rawD;
            grid[gIdx].u = u;
            grid[gIdx].v = v;
            continue;
         }

         float curvedD = rawD;
         if (depthCurve != 1.0f && curvedD > 0.0f)
            curvedD = std::pow(curvedD, std::max(0.01f, depthCurve));

         // Remap normalized curved depth into camera distance / depth range
         const float distanceZ = nearDepth + curvedD * (farDepth - nearDepth);

         float px = 0.0f, py = 0.0f, pz = 0.0f;

         switch (projection)
         {
            case kPerspective:
            {
               const float focal = std::max(0.001f, focalScale);
               const float normX = (u - 0.5f - principalPointX) * 2.0f * aspect * tanHalfFov / focal;
               const float normY = (v - 0.5f - principalPointY) * 2.0f * tanHalfFov / focal;
               pz = distanceZ * depthScale;
               px = normX * pz;
               py = normY * pz;
               break;
            }
            case kPlanar:
            {
               px = (u - 0.5f - principalPointX) * planarWidth;
               py = (v - 0.5f - principalPointY) * planarHeight;
               pz = (curvedD - 0.5f) * depthScale * (farDepth - nearDepth);
               break;
            }
            case kRadial:
            {
               const float focal = std::max(0.001f, focalScale);
               const float rx = (u - 0.5f - principalPointX) * 2.0f * aspect * tanHalfFov / focal;
               const float ry = (v - 0.5f - principalPointY) * 2.0f * tanHalfFov / focal;
               const float rz = 1.0f;
               const float len = std::sqrt(rx * rx + ry * ry + rz * rz);
               const float r = distanceZ * depthScale;
               px = (rx / len) * r;
               py = (ry / len) * r;
               pz = (rz / len) * r;
               break;
            }
            case kCylindrical:
            {
               const float theta = (u - 0.5f - principalPointX) * 2.0f * 3.14159265358979323846f;
               const float r = distanceZ * depthScale;
               px = r * std::sin(theta);
               py = (v - 0.5f - principalPointY) * planarHeight;
               pz = r * std::cos(theta);
               break;
            }
         }

         float colR = 1.0f, colG = 1.0f, colB = 1.0f;
         if (colorMode == kColorInput)
         {
            if (hasColorTex && !mColorPixels.empty())
            {
               colR = (float)mColorPixels[pIdx] / 255.0f;
               colG = (float)mColorPixels[pIdx + 1] / 255.0f;
               colB = (float)mColorPixels[pIdx + 2] / 255.0f;
            }
            else
            {
               colR = dr; colG = dg; colB = db;
            }
         }
         else if (colorMode == kSolidTint)
         {
            colR = 1.0f; colG = 1.0f; colB = 1.0f;
         }
         else
         {
            float rgb[3];
            EvaluateColormap(colorMode, curvedD, rgb);
            colR = rgb[0]; colG = rgb[1]; colB = rgb[2];
         }

         colR *= tint[0];
         colG *= tint[1];
         colB *= tint[2];

         grid[gIdx].px = px;
         grid[gIdx].py = py;
         grid[gIdx].pz = pz;
         grid[gIdx].rawDepth = rawD;
         grid[gIdx].curvedDepth = curvedD;
         grid[gIdx].r = colR;
         grid[gIdx].g = colG;
         grid[gIdx].b = colB;
         grid[gIdx].valid = true;
         grid[gIdx].u = u;
         grid[gIdx].v = v;
      }
   }

   // 4. Compute Surface Normals from 3D Position Gradients
   std::vector<float> normals((size_t)n * (size_t)n * 3, 0.0f);
   if (computeNormals)
   {
      for (int gy = 0; gy < n; gy++)
      {
         for (int gx = 0; gx < n; gx++)
         {
            const size_t gIdx = (size_t)gy * (size_t)n + (size_t)gx;
            if (!grid[gIdx].valid)
               continue;

            const int gx0 = std::max(0, gx - 1);
            const int gx1 = std::min(n - 1, gx + 1);
            const int gy0 = std::max(0, gy - 1);
            const int gy1 = std::min(n - 1, gy + 1);

            const auto& pC = grid[gIdx];
            const auto& pL = grid[(size_t)gy * (size_t)n + (size_t)gx0];
            const auto& pR = grid[(size_t)gy * (size_t)n + (size_t)gx1];
            const auto& pD = grid[(size_t)gy0 * (size_t)n + (size_t)gx];
            const auto& pU = grid[(size_t)gy1 * (size_t)n + (size_t)gx];

            const float dxX = (pR.valid ? pR.px : pC.px) - (pL.valid ? pL.px : pC.px);
            const float dxY = (pR.valid ? pR.py : pC.py) - (pL.valid ? pL.py : pC.py);
            const float dxZ = (pR.valid ? pR.pz : pC.pz) - (pL.valid ? pL.pz : pC.pz);

            const float dyX = (pU.valid ? pU.px : pC.px) - (pD.valid ? pD.px : pC.px);
            const float dyY = (pU.valid ? pU.py : pC.py) - (pD.valid ? pD.py : pC.py);
            const float dyZ = (pU.valid ? pU.pz : pC.pz) - (pD.valid ? pD.pz : pC.pz);

            // Cross product: dX x dY
            float nx = dxY * dyZ - dxZ * dyY;
            float ny = dxZ * dyX - dxX * dyZ;
            float nz = dxX * dyY - dxY * dyX;

            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f)
            {
               nx /= len; ny /= len; nz /= len;
            }
            else
            {
               nx = 0.0f; ny = 0.0f; nz = -1.0f;
            }

            normals[gIdx * 3 + 0] = nx;
            normals[gIdx * 3 + 1] = ny;
            normals[gIdx * 3 + 2] = nz;
         }
      }
   }

   // 5. Output Construction
   if (outputType == kPoints)
   {
      mPoints.clear();
      mPoints.reserve((size_t)n * (size_t)n);

      for (int gy = 0; gy < n; gy++)
      {
         for (int gx = 0; gx < n; gx++)
         {
            const size_t gIdx = (size_t)gy * (size_t)n + (size_t)gx;
            const auto& sample = grid[gIdx];
            if (!sample.valid)
               continue;

            Particle p;
            p.px = sample.px;
            p.py = sample.py;
            p.pz = sample.pz;
            p.nx = normals[gIdx * 3 + 0];
            p.ny = normals[gIdx * 3 + 1];
            p.nz = normals[gIdx * 3 + 2];
            p.scale = std::max(0.001f, pointSize);
            p.r = sample.r;
            p.g = sample.g;
            p.b = sample.b;
            p.life = 0.0f;
            p.alive = true;
            mPoints.push_back(p);
         }
      }
      mMesh = Mesh();
      mPointRevision = NextMeshRevision();
   }
   else // kMesh
   {
      mMesh = Mesh();
      std::vector<int> vertMap((size_t)n * (size_t)n, -1);

      // Collect vertices
      for (int gy = 0; gy < n; gy++)
      {
         for (int gx = 0; gx < n; gx++)
         {
            const size_t gIdx = (size_t)gy * (size_t)n + (size_t)gx;
            const auto& sample = grid[gIdx];
            if (!sample.valid)
               continue;

            const int vIdx = (int)mMesh.vertices.size();
            vertMap[gIdx] = vIdx;

            Vertex v;
            v.px = sample.px;
            v.py = sample.py;
            v.pz = sample.pz;
            v.nx = normals[gIdx * 3 + 0];
            v.ny = normals[gIdx * 3 + 1];
            v.nz = normals[gIdx * 3 + 2];
            v.u = sample.u;
            v.v = sample.v;
            mMesh.vertices.push_back(v);

            mMesh.vertexColor.push_back(sample.r);
            mMesh.vertexColor.push_back(sample.g);
            mMesh.vertexColor.push_back(sample.b);
         }
      }

      // Triangulate grid cells with depth edge tearing
      const float tearThreshold = std::max(0.001f, edgeTearThreshold);

      for (int gy = 0; gy < n - 1; gy++)
      {
         for (int gx = 0; gx < n - 1; gx++)
         {
            const size_t idx00 = (size_t)gy * (size_t)n + (size_t)gx;
            const size_t idx10 = (size_t)gy * (size_t)n + (size_t)(gx + 1);
            const size_t idx01 = (size_t)(gy + 1) * (size_t)n + (size_t)gx;
            const size_t idx11 = (size_t)(gy + 1) * (size_t)n + (size_t)(gx + 1);

            const int v00 = vertMap[idx00];
            const int v10 = vertMap[idx10];
            const int v01 = vertMap[idx01];
            const int v11 = vertMap[idx11];

            const float d00 = grid[idx00].curvedDepth;
            const float d10 = grid[idx10].curvedDepth;
            const float d01 = grid[idx01].curvedDepth;
            const float d11 = grid[idx11].curvedDepth;

            // Triangle 1: (v00, v10, v01)
            if (v00 >= 0 && v10 >= 0 && v01 >= 0)
            {
               const bool tear = (std::fabs(d00 - d10) > tearThreshold ||
                                  std::fabs(d10 - d01) > tearThreshold ||
                                  std::fabs(d01 - d00) > tearThreshold);
               if (!tear)
               {
                  mMesh.indices.push_back((unsigned int)v00);
                  mMesh.indices.push_back((unsigned int)v10);
                  mMesh.indices.push_back((unsigned int)v01);
               }
            }

            // Triangle 2: (v10, v11, v01)
            if (v10 >= 0 && v11 >= 0 && v01 >= 0)
            {
               const bool tear = (std::fabs(d10 - d11) > tearThreshold ||
                                  std::fabs(d11 - d01) > tearThreshold ||
                                  std::fabs(d01 - d10) > tearThreshold);
               if (!tear)
               {
                  mMesh.indices.push_back((unsigned int)v10);
                  mMesh.indices.push_back((unsigned int)v11);
                  mMesh.indices.push_back((unsigned int)v01);
               }
            }
         }
      }

      mPoints.clear();
      mMeshRevision = NextMeshRevision();
   }
}
