#include "GeometryOpNodes.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cmath>

#include "GLUtil.h"
#include "Transport.h"

namespace
{
   const std::vector<std::string> kOpNames = {
      "Transform", "Array", "Subdivide", "Solidify", "Extrude",
      "Wireframe", "Triangulate", "Normals", "Explode", "Twist",
      "Smooth", "Mirror", "Screw",
      "Select", "Delete Selected", "Transform Selected", "Extrude Selected"
   };
   const std::vector<std::string> kSourceNames = { "Vertices", "Edges", "Faces" };
   const std::vector<std::string> kWrapModeNames = { "Cylindrical", "Spherical", "Nearest Surface" };
   const Mesh kEmptyMesh;

   float Rand01(float seed, int index)
   {
      const float x = std::sin((seed + 1.0f) * (float)(index + 1) * 12.9898f) * 43758.5453f;
      return x - std::floor(x);
   }

   // FNV-1a over the raw pixel bytes, so DisplacementNode can tell a texture
   // that merely re-read the same as last frame (the common, static case) from
   // one that actually changed (an animated Noise/Voronoi) - see
   // DisplacementNode::CookIfNeeded.
   unsigned long long HashFloats(const std::vector<float>& v)
   {
      unsigned long long h = 1469598103934665603ull;
      const unsigned char* bytes = reinterpret_cast<const unsigned char*>(v.data());
      const size_t n = v.size() * sizeof(float);
      for (size_t i = 0; i < n; i++)
      {
         h ^= bytes[i];
         h *= 1099511628211ull;
      }
      return h;
   }

   // Whether a source is, or sits behind a chain of mesh-transforming wrapper
   // nodes on top of, an InstanceOnPoints - see IGeometrySource::PassthroughSource.
   bool WrapsInstancer(IGeometrySource* s)
   {
      for (; s != nullptr; s = s->PassthroughSource())
      {
         if (dynamic_cast<InstanceOnPointsNode*>(s) != nullptr)
            return true;
      }
      return false;
   }
}

const std::vector<std::string>& GeometryOpNode::OpNames() { return kOpNames; }

Mat4 GeometryOpNode::TransformMatrix() const
{
   const float spinPhase = spin * (float)Transport::Instance().Beats();
   Mat4 m = Mat4::Scale(scaleX, scaleY, scaleZ);
   m = Mat4::Multiply(Mat4::RotationZ(rotZ), m);
   m = Mat4::Multiply(Mat4::RotationY(rotY + spinPhase), m);
   m = Mat4::Multiply(Mat4::RotationX(rotX), m);
   m = Mat4::Multiply(Mat4::Translation(offsetX, offsetY, offsetZ), m);
   return m;
}

Mat4 GeometryOpNode::GetInstanceGroupMatrix() const
{
   const Mat4 upstream = input ? input->GetInstanceGroupMatrix() : Mat4::Identity();
   if (op == kTransform && !bypassed && WrapsInstancer(input))
      return Mat4::Multiply(TransformMatrix(), upstream);
   return upstream;
}

GeometryOpNode::Signature GeometryOpNode::CurrentSignature() const
{
   Signature s;
   s.op = op;
   s.count = count;
   s.levels = levels;
   s.axis = axis;
   s.a = amount;
   s.ox = offsetX; s.oy = offsetY; s.oz = offsetZ;
   s.rs = rotStep; s.ss = scaleStep; s.rad = radius;
   s.sm = smooth; s.th = thickness; s.ins = inset; s.sd = seed;
   s.rx = rotX; s.ry = rotY; s.rz = rotZ;
   s.sx = scaleX; s.sy = scaleY; s.sz = scaleZ;
   // Forces a rebuild every cook while spinning, since the baked-in rotation
   // (TransformMatrix()) would otherwise freeze at whichever beat happened to
   // be live the first time this signature matched.
   s.spinBeats = (spin != 0.0f) ? (float)Transport::Instance().Beats() : 0.0f;
   s.radial = radial; s.keep = keepOriginal; s.flat = flatShade; s.flip = flipNormals;
   s.selectMode = selectMode;
   s.selectA = selectA; s.selectB = selectB; s.selectC = selectC;
   s.selectSeed = selectSeed; s.normalAmount = normalAmount;
   s.selectInvert = selectInvert; s.selectAppend = selectAppend;
   s.keepSelected = keepSelected; s.moveAlongNormals = moveAlongNormals;
   s.iter = iterations;
   s.screwSteps = screwSteps;
   s.mirrorOffset = mirrorOffset;
   s.turns = turns;
   s.rise = rise;
   s.radiusOffset = radiusOffset;
   s.weldSeam = weldSeam;
   s.upstream = input;
   // The upstream's own revision stamp, not its triangle count: Select and
   // other mask-only operators leave the vertex/index count unchanged, so a
   // count was blind to exactly the changes those nodes make.
   s.upstreamRevision = input ? input->MeshRevision() : 0;
   return s;
}

const Mesh& GeometryOpNode::GetMesh()
{
   if (input == nullptr)
      return kEmptyMesh;
   if (bypassed)
      return input->GetMesh();

   const Signature sig = CurrentSignature();
   if (mHasBuilt && sig == mBuilt)
      return mCache;

   const Mesh& src = input->GetMesh();
   switch (op)
   {
      case kTransform:
         // Downstream of an instancer, each "copy" is a stamp placed by its own
         // instance transform, not a mesh of its own - baking the move/rotate/
         // scale into the stamp's vertices here would apply it once per instance
         // in the stamp's *local* frame (inflating/smearing every copy) instead
         // of moving the whole scattered result as one rigid group. Leave the
         // stamp mesh untouched and let GetInstanceGroupMatrix() carry it.
         mCache = WrapsInstancer(input) ? src : MeshOps::Transform(src, TransformMatrix());
         break;
      case kArray:
         mCache = MeshOps::Array(src, count, offsetX, offsetY, offsetZ,
                                 rotStep, scaleStep, radial, radius);
         break;
      case kSubdivide:
         mCache = MeshOps::Subdivide(src, levels, smooth);
         break;
      case kSolidify:
         mCache = MeshOps::Solidify(src, thickness, keepOriginal);
         break;
      case kExtrude:
         mCache = MeshOps::Extrude(src, thickness, inset);
         break;
      case kWireframe:
         mCache = MeshOps::Wireframe(src, thickness);
         break;
      case kTriangulate:
         mCache = MeshOps::Triangulate(src, amount * 0.05f);
         break;
      case kNormals:
         mCache = MeshOps::RecalculateNormals(src, flatShade, flipNormals);
         break;
      case kExplode:
         mCache = MeshOps::Explode(src, amount * 0.3f, seed);
         break;
      case kSmooth:
         // Laplacian relaxation alone barely moves a low-poly primitive like a
         // Cube - it has no extra vertices between the corners to round out.
         // Subdividing first gives it geometry to actually smooth.
         mCache = MeshOps::Smooth(levels > 0 ? MeshOps::Subdivide(src, levels, 1.0f) : src,
                                  iterations, amount);
         break;
      case kMirror:
         mCache = MeshOps::Mirror(src, axis, mirrorOffset, weldSeam, keepOriginal);
         break;
      case kScrew:
         mCache = MeshOps::Screw(src, screwSteps, turns, rise, radiusOffset, axis);
         break;
      case kSelect:
         mCache = MeshOps::Select(src, selectMode, selectA, selectB, selectC, axis,
                                  selectSeed, selectInvert, selectAppend);
         break;
      case kDeleteSelected:
         mCache = MeshOps::DeleteSelected(src, keepSelected);
         break;
      case kTransformSelected:
         mCache = MeshOps::TransformSelected(src, TransformMatrix(), moveAlongNormals, normalAmount);
         break;
      case kExtrudeSelected:
         mCache = MeshOps::ExtrudeSelected(src, thickness, inset);
         break;
      default:
         mCache = MeshOps::Twist(src, amount * 3.0f, axis);
         break;
   }

   mBuilt = sig;
   mHasBuilt = true;
   mMeshRevision = NextMeshRevision();
   return mCache;
}

unsigned long long GeometryOpNode::MeshRevision()
{
   // Both early-outs in GetMesh() hand back somebody else's mesh, so they have
   // to hand back that mesh's stamp too rather than this node's.
   if (input == nullptr)
      return 0;
   if (bypassed)
      return input->MeshRevision();
   GetMesh();
   return mMeshRevision;
}

Material GeometryOpNode::GetMaterial() const
{
   if (inheritMaterial && input != nullptr)
      return input->GetMaterial();

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

unsigned int GeometryOpNode::GetSurfaceTexture()
{
   return input ? input->GetSurfaceTexture() : 0;
}

void GeometryOpNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
}

// ======================================================== Displacement

DisplacementNode::~DisplacementNode()
{
   if (mReadFbo != 0)
      glDeleteFramebuffers(1, &mReadFbo);
}

DisplacementNode::Signature DisplacementNode::CurrentSignature() const
{
   Signature s;
   s.mode = mode;
   s.strength = strength;
   s.midlevel = midlevel;
   s.flat = flatShade;
   s.flip = flipNormals;
   s.upstream = input;
   s.upstreamRevision = input ? input->MeshRevision() : 0;
   s.texGeneration = mTexGeneration;
   return s;
}

const Mesh& DisplacementNode::GetMesh()
{
   if (input == nullptr)
      return kEmptyMesh;
   if (bypassed)
      return input->GetMesh();

   const Signature sig = CurrentSignature();
   if (mHasBuilt && sig == mBuilt)
      return mCache;

   const Mesh& src = input->GetMesh();
   mCache = MeshOps::Displace(src, mTexPixels, mTexW, mTexH, mode, strength, midlevel,
                              flatShade, flipNormals);

   mBuilt = sig;
   mHasBuilt = true;
   mMeshRevision = NextMeshRevision();
   return mCache;
}

unsigned long long DisplacementNode::MeshRevision()
{
   if (input == nullptr)
      return 0;
   if (bypassed)
      return input->MeshRevision();
   GetMesh();
   return mMeshRevision;
}

Material DisplacementNode::GetMaterial() const
{
   if (inheritMaterial && input != nullptr)
      return input->GetMaterial();

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

unsigned int DisplacementNode::GetSurfaceTexture()
{
   return input ? input->GetSurfaceTexture() : 0;
}

void DisplacementNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);

   if (mTextureInput.IsConnected())
   {
      const unsigned int tex = mTextureInput.Pull(frameId);
      const int w = mTextureInput.Width();
      const int h = mTextureInput.Height();
      if (tex != 0 && w > 0 && h > 0 &&
          GLUtil::ReadTexturePixels(mReadFbo, tex, w, h, mTexPixels))
      {
         mTexW = w; mTexH = h;
      }
      else
      {
         mTexPixels.clear();
         mTexW = mTexH = 0;
      }
      // Bumped only when the pixels actually differ from last frame - there is
      // no per-texture revision stamp to compare against instead, so content
      // is hashed directly. A static texture (Clouds with no animated seed,
      // the common case) now leaves the signature alone instead of forcing a
      // mesh rebuild - and resetting any simulation downstream, like Cloth -
      // every single frame. An animated source (Noise with time, Voronoi with
      // a modulated seed) still drives the mesh every frame its pixels change.
      const unsigned long long hash = HashFloats(mTexPixels);
      if (!mHasTexHash || hash != mTexHash)
      {
         mTexGeneration++;
         mTexHash = hash;
         mHasTexHash = true;
      }
   }
   else if (mTexW != 0 || mTexH != 0)
   {
      mTexPixels.clear();
      mTexW = mTexH = 0;
      mTexGeneration++;
      mHasTexHash = false;
   }
}

// ===================================================== Instance on Points

const std::vector<std::string>& InstanceOnPointsNode::SourceNames() { return kSourceNames; }

const Mesh& InstanceOnPointsNode::GetMesh()
{
   // The shape drawn per instance; the transforms come from InstanceTransforms.
   return instanceShape ? instanceShape->GetMesh() : mEmpty;
}

unsigned long long InstanceOnPointsNode::MeshRevision()
{
   // The uploaded mesh is the shape's, so the stamp has to be the shape's too.
   return instanceShape ? instanceShape->MeshRevision() : 0;
}

size_t InstanceOnPointsNode::TriangleCount() const
{
   if (instanceShape == nullptr)
      return 0;
   return (const_cast<IGeometrySource*>(instanceShape)->GetMesh().indices.size() / 3) * mTransforms.size();
}

Material InstanceOnPointsNode::GetMaterial() const
{
   if (inheritMaterial && instanceShape != nullptr)
      return instanceShape->GetMaterial();

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

unsigned int InstanceOnPointsNode::GetSurfaceTexture()
{
   return instanceShape ? instanceShape->GetSurfaceTexture() : 0;
}

void InstanceOnPointsNode::Rebuild()
{
   mTransforms.clear();
   mColors.clear();

   // The stamp's own transform is baked into every instance as a baseline -
   // move/scale/rotate the shape source and all copies follow, same as
   // dragging its object-level sliders would in a single-instance graph.
   const Mat4 shapeModel = instanceShape ? instanceShape->GetModelMatrix() : Mat4::Identity();

   // A patched point cloud replaces mesh sampling: the positions already exist,
   // so there is nothing to sample.
   if (cloudSource != nullptr)
   {
      const std::vector<Particle>& cloud = cloudSource->GetPoints();
      mTransforms.reserve(cloud.size());
      mColors.reserve(cloud.size() * 3);
      for (const Particle& p : cloud)
      {
         if (!p.alive)
            continue;
         const float s = instanceScale * p.scale;
         Mat4 m = Mat4::Scale(s, s, s);
         m = Mat4::Multiply(Mat4::Translation(p.px, p.py, p.pz), m);
         mTransforms.push_back(Mat4::Multiply(m, shapeModel));
         mColors.push_back(p.r);
         mColors.push_back(p.g);
         mColors.push_back(p.b);
      }
      return;
   }

   if (pointSource == nullptr)
      return;

   const Mesh& srcLocal = pointSource->GetMesh();
   if (srcLocal.Empty())
      return;

   // Points are sampled from the source's own space, so its object-level
   // transform (move/rotate/scale the Helix, say) has to be applied before
   // sampling or it never reaches the instances scattered on it.
   const Mat4 pointModel = pointSource->GetModelMatrix();
   const Mesh src = MeshOps::Transform(srcLocal, pointModel);

   const std::vector<MeshPoint> points = MeshOps::ToPoints(src, pointMode, maxPoints);
   mTransforms.reserve(points.size());

   for (size_t i = 0; i < points.size(); i++)
   {
      const MeshPoint& p = points[i];
      const float r0 = Rand01(seed, (int)i * 3 + 0);
      const float r1 = Rand01(seed, (int)i * 3 + 1);
      const float r2 = Rand01(seed, (int)i * 3 + 2);

      const float s = instanceScale * (1.0f + (r0 - 0.5f) * 2.0f * scaleRandom);
      Mat4 m = Mat4::Scale(s, s, s);

      if (rotationRandom > 0.0f)
      {
         m = Mat4::Multiply(Mat4::RotationY(r1 * 6.28318530f * rotationRandom), m);
         m = Mat4::Multiply(Mat4::RotationX(r2 * 6.28318530f * rotationRandom), m);
      }

      if (alignToNormal)
      {
         // Build a basis whose Y axis is the point normal, so instances stand
         // up off the surface instead of all facing the same way.
         float n[3] = { p.nx, p.ny, p.nz };
         const float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
         if (len > 1e-6f) { n[0] /= len; n[1] /= len; n[2] /= len; }
         float up[3] = { 0, 1, 0 };
         if (std::fabs(n[1]) > 0.99f) { up[0] = 1; up[1] = 0; }
         float t[3] = { up[1]*n[2] - up[2]*n[1], up[2]*n[0] - up[0]*n[2], up[0]*n[1] - up[1]*n[0] };
         const float tl = std::sqrt(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
         if (tl > 1e-6f) { t[0] /= tl; t[1] /= tl; t[2] /= tl; }
         const float b[3] = { n[1]*t[2] - n[2]*t[1], n[2]*t[0] - n[0]*t[2], n[0]*t[1] - n[1]*t[0] };

         Mat4 basis;
         basis.m[0] = t[0]; basis.m[1] = t[1]; basis.m[2] = t[2];
         basis.m[4] = n[0]; basis.m[5] = n[1]; basis.m[6] = n[2];
         basis.m[8] = b[0]; basis.m[9] = b[1]; basis.m[10] = b[2];
         m = Mat4::Multiply(basis, m);
      }

      m = Mat4::Multiply(Mat4::Translation(p.px + p.nx * normalOffset,
                                           p.py + p.ny * normalOffset,
                                           p.pz + p.nz * normalOffset), m);
      mTransforms.push_back(Mat4::Multiply(m, shapeModel));
   }
}

void InstanceOnPointsNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (auto* p = dynamic_cast<INode*>(pointSource))
      p->CookIfNeeded(frameId);
   if (auto* s = dynamic_cast<INode*>(instanceShape))
      s->CookIfNeeded(frameId);
   if (auto* c = dynamic_cast<INode*>(cloudSource))
      c->CookIfNeeded(frameId);

   // A simulated cloud changes every frame while it is running, so its own
   // revision stamp is what decides dirtiness rather than any parameter here.
   // pointSource and instanceShape get the same treatment for the same
   // reason a triangle count was tried and dropped in GeometryOpNode: a
   // Select or Transform Selected upstream can change which vertices move,
   // or where they end up, without changing how many there are.
   const unsigned long long cloudRevision = cloudSource ? cloudSource->PointRevision() : 0;
   const unsigned long long pointRevision = pointSource ? pointSource->MeshRevision() : 0;
   const unsigned long long shapeRevision = instanceShape ? instanceShape->MeshRevision() : 0;
   // Neither source bumps a revision for a pure transform edit (GetModelMatrix
   // is evaluated live, including per-frame animation like spin/beat), so the
   // matrices baked into Rebuild() have to be compared directly to catch that.
   const Mat4 pointModel = pointSource ? pointSource->GetModelMatrix() : Mat4::Identity();
   const Mat4 shapeModel = instanceShape ? instanceShape->GetModelMatrix() : Mat4::Identity();
   const bool dirty =
      mBuiltPointSource != pointSource || mBuiltShape != instanceShape ||
      mBuiltCloud != (const void*)cloudSource || mBuiltCloudRevision != cloudRevision ||
      mBuiltPointRevision != pointRevision || mBuiltShapeRevision != shapeRevision ||
      !(mBuiltPointModel == pointModel) || !(mBuiltShapeModel == shapeModel) ||
      mBuiltMode != pointMode || mBuiltMax != maxPoints ||
      mBuiltScale != instanceScale || mBuiltScaleRand != scaleRandom ||
      mBuiltRotRand != rotationRandom || mBuiltSeed != seed ||
      mBuiltOffset != normalOffset || mBuiltAlign != alignToNormal;

   if (!dirty)
      return;

   Rebuild();
   mInstanceRevision = NextMeshRevision();

   mBuiltPointSource = pointSource;
   mBuiltShape = instanceShape;
   mBuiltCloud = cloudSource;
   mBuiltCloudRevision = cloudRevision;
   mBuiltPointRevision = pointRevision;
   mBuiltShapeRevision = shapeRevision;
   mBuiltPointModel = pointModel;
   mBuiltShapeModel = shapeModel;
   mBuiltMode = pointMode;
   mBuiltMax = maxPoints;
   mBuiltScale = instanceScale;
   mBuiltScaleRand = scaleRandom;
   mBuiltRotRand = rotationRandom;
   mBuiltSeed = seed;
   mBuiltOffset = normalOffset;
   mBuiltAlign = alignToNormal;
}

// ============================================================== Wrap

const std::vector<std::string>& WrapNode::ModeNames() { return kWrapModeNames; }

WrapNode::Signature WrapNode::CurrentSignature() const
{
   Signature s;
   s.mode = mode;
   s.axis = axis;
   s.radiusOverride = radiusOverride;
   s.radiusScale = radiusScale;
   s.fitAround = fitAround;
   s.offset = offset;
   s.blend = blend;
   s.flat = flatShade;
   s.flip = flipNormals;
   s.source = sourceInput;
   s.target = targetInput;
   s.sourceRevision = sourceInput ? sourceInput->MeshRevision() : 0;
   s.targetRevision = targetInput ? targetInput->MeshRevision() : 0;
   s.sourceModel = sourceInput ? sourceInput->GetModelMatrix() : Mat4::Identity();
   s.targetModel = targetInput ? targetInput->GetModelMatrix() : Mat4::Identity();
   return s;
}

float WrapNode::ResolvedRadius() const
{
   if (targetInput == nullptr)
      return radiusOverride;
   return MeshOps::WrapRadius(targetInput->GetMesh(), targetInput->GetModelMatrix(), axis) *
          radiusScale;
}

const Mesh& WrapNode::GetMesh()
{
   if (sourceInput == nullptr)
      return kEmptyMesh;
   if (bypassed)
      return sourceInput->GetMesh();

   const Signature sig = CurrentSignature();
   if (mHasBuilt && sig == mBuilt)
      return mCache;

   const Mesh& src = sourceInput->GetMesh();
   const Mat4 srcModel = sourceInput->GetModelMatrix();
   // The target is optional in the bend modes - a radius override alone is
   // enough to bend around nothing - so an empty target is passed straight
   // through to Wrap, which decides whether it can proceed.
   const Mesh& tgt = targetInput ? targetInput->GetMesh() : kEmptyMesh;
   const Mat4 tgtModel = targetInput ? targetInput->GetModelMatrix() : Mat4::Identity();
   mCache = MeshOps::Wrap(src, srcModel, tgt, tgtModel, mode, offset, blend,
                          radiusOverride, radiusScale, axis, fitAround, flatShade, flipNormals);

   mBuilt = sig;
   mHasBuilt = true;
   mMeshRevision = NextMeshRevision();
   return mCache;
}

unsigned long long WrapNode::MeshRevision()
{
   if (sourceInput == nullptr)
      return 0;
   if (bypassed)
      return sourceInput->MeshRevision();
   GetMesh();
   return mMeshRevision;
}

Material WrapNode::GetMaterial() const
{
   if (inheritMaterial && sourceInput != nullptr)
      return sourceInput->GetMaterial();

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

unsigned int WrapNode::GetSurfaceTexture()
{
   return sourceInput ? sourceInput->GetSurfaceTexture() : 0;
}

void WrapNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(sourceInput))
      upstream->CookIfNeeded(frameId);
   if (auto* upstream = dynamic_cast<INode*>(targetInput))
      upstream->CookIfNeeded(frameId);
}
