#include "GenerativeNodes.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kOpNames = {
      "displace", "jitter", "smooth", "twist",
      "bulge", "extrude faces", "subdivide", "squash"
   };

   const std::vector<std::string> kDepthSourceNames = {
      "Luminance", "Red", "Alpha", "Flat"
   };

   // Deterministic hash in 0..1. The seed is mixed into the *input* rather than
   // used as a multiplier on the output, so two nodes with different seeds
   // produce genuinely uncorrelated sequences instead of scaled copies of one -
   // the bug the Random modulators shipped with.
   float Hash01(float a, float b, float c)
   {
      float v = std::sin(a * 127.1f + b * 311.7f + c * 74.7f) * 43758.5453f;
      return v - std::floor(v);
   }

   // Smooth 3D value noise, so `displace` produces lobes rather than static.
   float ValueNoise(float x, float y, float z, float seed)
   {
      const float ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
      const float fx = x - ix, fy = y - iy, fz = z - iz;
      const float sx = fx * fx * (3.0f - 2.0f * fx);
      const float sy = fy * fy * (3.0f - 2.0f * fy);
      const float sz = fz * fz * (3.0f - 2.0f * fz);

      auto corner = [&](int dx, int dy, int dz)
      {
         return Hash01(ix + (float)dx + seed * 37.0f, iy + (float)dy, iz + (float)dz);
      };
      const float c00 = corner(0, 0, 0) + (corner(1, 0, 0) - corner(0, 0, 0)) * sx;
      const float c10 = corner(0, 1, 0) + (corner(1, 1, 0) - corner(0, 1, 0)) * sx;
      const float c01 = corner(0, 0, 1) + (corner(1, 0, 1) - corner(0, 0, 1)) * sx;
      const float c11 = corner(0, 1, 1) + (corner(1, 1, 1) - corner(0, 1, 1)) * sx;
      const float c0 = c00 + (c10 - c00) * sy;
      const float c1 = c01 + (c11 - c01) * sy;
      return (c0 + (c1 - c0) * sz) * 2.0f - 1.0f;
   }
}

const std::vector<std::string>& MeshResynthNode::OpNames() { return kOpNames; }
const std::vector<std::string>& ImageToPointsNode::DepthSourceNames() { return kDepthSourceNames; }

void MeshResynthNode::Randomise()
{
   // Rolls both the seed and the weights - this is the "surprise me" button.
   // To explore one axis at a time, move the seed slider (same weights, a
   // different evolution) or a single weight (same evolution, different mix).
   seed = Hash01(seed * 17.3f + 1.7f, seed * 5.1f, 3.3f) * 1000.0f;
   for (int i = 0; i < kOpCount; i++)
      weight[i] = Hash01(seed, (float)i * 13.0f, 9.1f);
   // Subdivide is the only operator that costs triangles permanently, so it
   // starts near zero rather than at a uniform random weight.
   weight[kSubdivide] *= 0.15f;
   mNeedsReset = true;
}

void MeshResynthNode::ApplyGeneration(int generation)
{
   if (mMesh.vertices.empty())
      return;

   const float g = (float)generation;
   const float amount = std::max(0.0f, chaos);

   // Bounding radius, so displacement magnitudes stay proportional to the shape
   // rather than to whatever units it happens to be modelled in.
   float radius = 0.0f;
   for (const Vertex& v : mMesh.vertices)
      radius = std::max(radius, std::sqrt(v.px * v.px + v.py * v.py + v.pz * v.pz));
   if (radius < 1e-5f)
      radius = 1.0f;

   if (weight[kSubdivide] > 0.01f &&
       (int)(mMesh.indices.size() / 3) * 4 <= triangleBudget &&
       Hash01(seed, g, 21.0f) < weight[kSubdivide])
   {
      mMesh = MeshOps::Subdivide(mMesh, 1, 0.5f);
   }

   if (weight[kSmooth] > 0.01f)
      mMesh = MeshOps::Smooth(mMesh, 1, weight[kSmooth] * 0.6f);

   if (weight[kExtrudeFaces] > 0.01f && (int)(mMesh.indices.size() / 3) * 3 <= triangleBudget)
   {
      // A random face subset, then extrude just those. Fraction is capped low:
      // extruding most of a mesh every generation turns it to noise in three
      // steps rather than growing structure.
      const float fraction = std::min(0.25f, weight[kExtrudeFaces] * 0.25f);
      Mesh selected = MeshOps::Select(mMesh, MeshOps::kSelectRandom, fraction, 0.0f, 0.0f,
                                      0, seed + g, false, false);
      if (selected.SelectedCount() > 0)
         mMesh = MeshOps::ExtrudeSelected(selected, radius * 0.12f * amount, 0.25f);
      mMesh.faceMask.clear();
   }

   const float noiseScale = 2.0f + Hash01(seed, g, 5.0f) * 4.0f;
   const float twist = (Hash01(seed, g, 7.0f) - 0.5f) * 2.0f * weight[kTwist] * amount * 3.0f;
   const float sqx = 1.0f + (Hash01(seed, g, 11.0f) - 0.5f) * weight[kSquash] * amount;
   const float sqy = 1.0f + (Hash01(seed, g, 12.0f) - 0.5f) * weight[kSquash] * amount;
   const float sqz = 1.0f + (Hash01(seed, g, 13.0f) - 0.5f) * weight[kSquash] * amount;

   for (size_t i = 0; i < mMesh.vertices.size(); i++)
   {
      Vertex& v = mMesh.vertices[i];

      if (weight[kDisplace] > 0.01f)
      {
         const float n = ValueNoise(v.px * noiseScale, v.py * noiseScale, v.pz * noiseScale,
                                    seed + g * 3.7f);
         const float d = n * weight[kDisplace] * amount * radius * 0.15f;
         v.px += v.nx * d; v.py += v.ny * d; v.pz += v.nz * d;
      }

      if (weight[kBulge] > 0.01f)
      {
         const float len = std::sqrt(v.px * v.px + v.py * v.py + v.pz * v.pz);
         if (len > 1e-5f)
         {
            const float n = ValueNoise(v.px * 1.5f, v.py * 1.5f, v.pz * 1.5f, seed + g * 1.3f + 50.0f);
            const float s = 1.0f + n * weight[kBulge] * amount * 0.3f;
            v.px *= s; v.py *= s; v.pz *= s;
         }
      }

      if (weight[kJitter] > 0.01f)
      {
         // Keyed on vertex index, so the same vertex gets the same offset every
         // replay - jitter that changed per frame would just look like noise.
         const float fi = (float)i;
         const float j = weight[kJitter] * amount * radius * 0.04f;
         v.px += (Hash01(seed + g, fi, 1.0f) - 0.5f) * j;
         v.py += (Hash01(seed + g, fi, 2.0f) - 0.5f) * j;
         v.pz += (Hash01(seed + g, fi, 3.0f) - 0.5f) * j;
      }

      if (std::fabs(twist) > 1e-4f)
      {
         const float a = v.py * twist;
         const float c = std::cos(a), s = std::sin(a);
         const float x = v.px * c - v.pz * s;
         const float z = v.px * s + v.pz * c;
         v.px = x; v.pz = z;
      }

      if (weight[kSquash] > 0.01f)
      {
         v.px *= sqx; v.py *= sqy; v.pz *= sqz;
      }
   }

   // Every operator above moves positions without touching normals, so one
   // recalculation at the end is both correct and cheaper than per-operator.
   mMesh = MeshOps::RecalculateNormals(mMesh, false, false);
}

void MeshResynthNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (input == nullptr)
   {
      if (!mMesh.Empty())
      {
         mMesh = Mesh();
         mRevision = NextMeshRevision();
      }
      mBuiltInput = nullptr;
      return;
   }

   if (auto* node = dynamic_cast<INode*>(input))
      node->CookIfNeeded(frameId);

   const unsigned long long inputRevision = input->MeshRevision();
   // A changed source restarts the evolution rather than mutating the old
   // result - otherwise swapping the input would leave forty generations of
   // drift applied to a shape that never went through them.
   if (mBuiltInput != (const void*)input || mBuiltInputRevision != inputRevision)
   {
      mBuiltInput = input;
      mBuiltInputRevision = inputRevision;
      mNeedsReset = true;
   }

   const double beats = Transport::Instance().Beats();
   if (autoStep && stepsPerBeat > 0.0f)
   {
      const double interval = 1.0 / (double)stepsPerBeat;
      if (beats - mLastStepBeat >= interval)
      {
         mPendingSteps++;
         mLastStepBeat = beats;
      }
   }

   bool changed = false;
   if (mNeedsReset)
   {
      mMesh = input->GetMesh();
      mMesh.faceMask.clear();
      mGeneration = 0;
      mNeedsReset = false;
      changed = true;
   }

   // Capped per frame so a step burst can't stall the UI; the rest carry over.
   int steps = std::min(mPendingSteps, 8);
   mPendingSteps -= steps;
   while (steps-- > 0)
   {
      mGeneration++;
      ApplyGeneration(mGeneration);
      changed = true;
   }

   if (changed)
      mRevision = NextMeshRevision();
}

void ImageToPointsNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const unsigned int tex = mInput.Pull(frameId);
   if (tex == 0)
   {
      if (!mPoints.empty())
      {
         mPoints.clear();
         mPointUv.clear();
         mRevision = NextMeshRevision();
      }
      return;
   }

   const int n = std::max(2, std::min(density, 512));
   if (!EnsureDownsampler(n))
      return;

   // The point of the intermediate FBO is the readback size: pulling a 4000px
   // texture straight back to the CPU is a pipeline stall measured in
   // milliseconds, every frame, where an n x n target is ~36 KB. The sampling
   // is still a sparse sample of the source (bilinear only averages four
   // texels), it just happens on the GPU.
   GLUtil::RunShaderPass(mSmall, mProgram, [this, tex]()
   {
      glActiveTexture(GL_TEXTURE0);
      // Deliberately no glTexParameteri here: this texture belongs to the
      // upstream node, and changing its filter would silently turn off the
      // mipmapping that the 3D surface path sets up on the same handle.
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(glGetUniformLocation(mProgram, "uTex"), 0);
   });

   mPixels.resize((size_t)n * (size_t)n * 4);
   glBindTexture(GL_TEXTURE_2D, mSmall.tex);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());
   glBindTexture(GL_TEXTURE_2D, 0);

   mPoints.clear();
   mPoints.reserve((size_t)n * (size_t)n);
   mPointUv.clear();
   mPointUv.reserve((size_t)n * (size_t)n);

   for (int gy = 0; gy < n; gy++)
   {
      for (int gx = 0; gx < n; gx++)
      {
         const float u = ((float)gx + 0.5f) / (float)n;
         const float v = ((float)gy + 0.5f) / (float)n;
         const size_t idx = ((size_t)gy * (size_t)n + (size_t)gx) * 4;

         const float r = (float)mPixels[idx] / 255.0f;
         const float g = (float)mPixels[idx + 1] / 255.0f;
         const float b = (float)mPixels[idx + 2] / 255.0f;
         const float a = (float)mPixels[idx + 3] / 255.0f;
         const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;

         float depth = 0.0f;
         switch (depthSource)
         {
            case kRed: depth = r; break;
            case kAlpha: depth = a; break;
            case kFlat: depth = 0.0f; break;
            default: depth = luma; break;
         }

         // Threshold on alpha as well as brightness, so a transparent PNG drops
         // its background even when that background is white.
         if (luma < threshold || a < threshold)
            continue;

         Particle p;
         p.px = (u - 0.5f) * width;
         // glGetTexImage returns rows bottom-up, matching GL's convention, so
         // row 0 is the bottom of the image and maps to -Y. Flipping here would
         // mirror the cloud against the 2D preview of the same image.
         p.py = (v - 0.5f) * height;
         p.pz = depth * depthScale;
         p.nx = 0.0f; p.ny = 0.0f; p.nz = 1.0f;
         p.scale = pointSize * (1.0f + (luma - 0.5f) * 2.0f * sizeFromLuma);
         p.scale = std::max(0.001f, p.scale);
         p.r = useImageColor ? r * tint[0] : tint[0];
         p.g = useImageColor ? g * tint[1] : tint[1];
         p.b = useImageColor ? b * tint[2] : tint[2];
         p.life = 0.0f;
         p.alive = true;
         mPoints.push_back(p);
         mPointUv.push_back({ u, v });
      }
   }

   mRevision = NextMeshRevision();
}

void ImageToPointsNode::RebuildMeshIfNeeded()
{
   if (mBuiltMeshRevision == mRevision)
      return;
   mBuiltMeshRevision = mRevision;

   mCookedMesh = Mesh();
   mCookedMesh.vertices.reserve(mPoints.size() * 4);
   mCookedMesh.indices.reserve(mPoints.size() * 6);

   // p.scale is a relative multiplier meant to be combined with an external
   // instance size (see InstanceOnPointsNode::Rebuild, which multiplies it by
   // its own instanceScale) - on its own it is not a world-space size. For a
   // standalone swatch quad here, anchor the *base* size to the grid spacing
   // so points read as a cloud of distinct dice instead of one overlapping
   // slab, and let p.scale still scale that up/down around the base.
   const int n = std::max(2, std::min(density, 512));
   const float cell = std::min(width, height) / (float)n;
   const float baseHalf = cell * 0.45f;

   for (size_t i = 0; i < mPoints.size(); i++)
   {
      const Particle& p = mPoints[i];
      const float u = mPointUv[i].first;
      const float v = mPointUv[i].second;

      // Billboard-ish quad oriented to the point normal, same construction as
      // MeshOps::PointsToFaces (Mesh.cpp) - but every corner gets this point's
      // own single (u, v) instead of a 0..1 spread, so the quad samples one
      // texel of the source image rather than the whole thing.
      float n[3] = { p.nx, p.ny, p.nz };
      const float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
      if (len > 1e-6f) { n[0] /= len; n[1] /= len; n[2] /= len; }
      float up[3] = { 0, 1, 0 };
      if (std::fabs(n[1]) > 0.9f) { up[0] = 1; up[1] = 0; }
      float sx[3] = { n[1]*up[2] - n[2]*up[1], n[2]*up[0] - n[0]*up[2], n[0]*up[1] - n[1]*up[0] };
      const float sl = std::sqrt(sx[0]*sx[0] + sx[1]*sx[1] + sx[2]*sx[2]);
      if (sl < 1e-6f) continue;
      sx[0] /= sl; sx[1] /= sl; sx[2] /= sl;
      const float sy[3] = { n[1]*sx[2] - n[2]*sx[1], n[2]*sx[0] - n[0]*sx[2], n[0]*sx[1] - n[1]*sx[0] };

      const float h = baseHalf * p.scale;
      const unsigned int base = (unsigned int)mCookedMesh.vertices.size();
      const float corners[4][2] = { { -h, -h }, { h, -h }, { h, h }, { -h, h } };
      for (int c = 0; c < 4; c++)
      {
         Vertex vtx;
         vtx.px = p.px + sx[0]*corners[c][0] + sy[0]*corners[c][1];
         vtx.py = p.py + sx[1]*corners[c][0] + sy[1]*corners[c][1];
         vtx.pz = p.pz + sx[2]*corners[c][0] + sy[2]*corners[c][1];
         vtx.nx = n[0]; vtx.ny = n[1]; vtx.nz = n[2];
         vtx.u = u; vtx.v = v;
         mCookedMesh.vertices.push_back(vtx);
      }
      mCookedMesh.indices.push_back(base); mCookedMesh.indices.push_back(base + 1); mCookedMesh.indices.push_back(base + 2);
      mCookedMesh.indices.push_back(base); mCookedMesh.indices.push_back(base + 2); mCookedMesh.indices.push_back(base + 3);
   }

   mCookedMeshRevision = NextMeshRevision();
}

const Mesh& ImageToPointsNode::GetMesh()
{
   RebuildMeshIfNeeded();
   return mCookedMesh;
}

unsigned long long ImageToPointsNode::MeshRevision()
{
   RebuildMeshIfNeeded();
   return mCookedMeshRevision;
}

Material ImageToPointsNode::GetMaterial() const
{
   // Matches CookIfNeeded's p.r/g/b: tint always multiplies, the raw texture
   // (see GetSurfaceTexture) only enters the mix when useImageColor is on.
   Material m;
   m.color[0] = tint[0]; m.color[1] = tint[1]; m.color[2] = tint[2];
   return m;
}

ImageToPointsNode::~ImageToPointsNode()
{
   GLUtil::DestroyFbo(mSmall);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool ImageToPointsNode::EnsureDownsampler(int n)
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
   return mProgram != 0 && GLUtil::EnsureFbo(mSmall, n, n);
}
