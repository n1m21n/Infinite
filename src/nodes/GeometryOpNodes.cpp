#include "GeometryOpNodes.h"

#include "gl3.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>

#include "GLUtil.h"
#include "Transport.h"

namespace
{
   const std::vector<std::string> kOpNames = {
      "Transform", "Array", "Subdivide", "Solidify", "Extrude",
      "Wireframe", "Triangulate", "Normals", "Explode", "Twist",
      "Smooth", "Mirror", "Screw",
      // The last three are deprecated (GeometryOpNode::kOpCount comment) -
      // still named here so a saved index still has a label, just not spawned
      // or offered in the operation dropdown (GeometryOpNode::IsSpawnable).
      "Select", "Delete Selected", "Transform Selected", "Extrude Selected",
      "Delete"
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

   // Walks the same PassthroughSource() chain as WrapsInstancer below, but
   // returns the instancer itself so a caller can also read its instance
   // count - see GeometryOpNode::UpstreamInstanceCount.
   InstanceOnPointsNode* FindInstancer(IGeometrySource* s)
   {
      for (; s != nullptr; s = s->PassthroughSource())
      {
         if (auto* instancer = dynamic_cast<InstanceOnPointsNode*>(s))
            return instancer;
      }
      return nullptr;
   }

   // Whether a source is, or sits behind a chain of mesh-transforming wrapper
   // nodes on top of, an InstanceOnPoints - see IGeometrySource::PassthroughSource.
   bool WrapsInstancer(IGeometrySource* s)
   {
      return FindInstancer(s) != nullptr;
   }

   // Same hash MeshOps::Select uses internally (Mesh.cpp's anonymous-namespace
   // SelectHash) - kept identical so kSelectRandom means the same threshold in
   // the instance domain as it does on faces.
   float InstanceSelectHash(float seed, size_t index)
   {
      const float x = std::sin((seed + 1.0f) * (float)(index + 1) * 12.9898f) * 43758.5453f;
      return x - std::floor(x);
   }
}

const std::vector<std::string>& GeometryOpNode::OpNames() { return kOpNames; }

bool GeometryOpNode::IsSpawnable(int op)
{
   return op != kDeleteSelected && op != kTransformSelected && op != kExtrudeSelected;
}

void GeometryOpNode::MigrateDeprecatedOp()
{
   switch (op)
   {
      case kDeleteSelected:    op = kDelete;    selectionOnly = true; break;
      case kTransformSelected: op = kTransform; selectionOnly = true; break;
      case kExtrudeSelected:   op = kExtrude;   selectionOnly = true; break;
      default: break;
   }
}

Mat4 GeometryOpNode::TransformMatrix() const
{
   const float spinPhase = spin * (float)Transport::Instance().Beats();
   const float d2r = 3.14159265f / 180.0f;
   Mat4 m = Mat4::Scale(scaleX, scaleY, scaleZ);
   m = Mat4::Multiply(Mat4::RotationZ(rotZ * d2r), m);
   m = Mat4::Multiply(Mat4::RotationY(rotY * d2r + spinPhase), m);
   m = Mat4::Multiply(Mat4::RotationX(rotX * d2r), m);
   m = Mat4::Multiply(Mat4::Translation(offsetX, offsetY, offsetZ), m);
   return m;
}

Mat4 GeometryOpNode::GetInstanceGroupMatrix() const
{
   const Mat4 upstream = input ? input->GetInstanceGroupMatrix() : Mat4::Identity();
   // selectionOnly moves only the masked instances, published as a per-instance
   // InstanceTransformOverride() in GetMesh() instead - folding the same move
   // into the whole-group matrix here as well would apply it twice (once per
   // instance, once again to the group). Without selectionOnly there is no
   // per-instance override, so the whole-group matrix is the only place the
   // move happens, same as before this feature existed.
   if (op == kTransform && !bypassed && WrapsInstancer(input) && !selectionOnly)
      return Mat4::Multiply(TransformMatrix(), upstream);
   return upstream;
}

bool GeometryOpNode::ActsOnInstanceStamp() const
{
   return !bypassed && WrapsInstancer(input);
}

size_t GeometryOpNode::UpstreamInstanceCount() const
{
   InstanceOnPointsNode* instancer = FindInstancer(input);
   return instancer ? instancer->InstanceCount() : 0;
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
   s.selectionOnly = selectionOnly;
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
   if (InstanceOnPointsNode* instancer = FindInstancer(input))
      s.upstreamInstanceRevision = instancer->InstanceRevision();
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
   // Per-face-independent operators (Explode, Triangulate, Normals, Solidify,
   // Wireframe) get `selectionOnly` by running on the selected faces alone
   // and stitching the untouched rest back on - see MeshOps::SplitBySelection.
   // Delete/Transform/Extrude instead reuse the dedicated *Selected functions
   // (or, for Extrude, its own built-in FaceSelected filter) since those
   // already implement face-accurate restriction without a split/rejoin seam.
   auto restricted = [&](const std::function<Mesh(const Mesh&)>& fn) -> Mesh
   {
      if (!selectionOnly || src.faceMask.empty())
         return fn(src);
      MeshOps::SelectionSplit split = MeshOps::SplitBySelection(src);
      Mesh result = fn(split.selected);
      MeshOps::AppendMesh(result, split.unselected);
      return result;
   };
   switch (op)
   {
      case kTransform:
         // Downstream of an instancer, each "copy" is a stamp placed by its own
         // instance transform, not a mesh of its own - baking the move/rotate/
         // scale into the stamp's vertices here would apply it once per instance
         // in the stamp's *local* frame (inflating/smearing every copy) instead
         // of moving the whole scattered result as one rigid group. Leave the
         // stamp mesh untouched and let GetInstanceGroupMatrix() (whole group)
         // or the per-instance override below (selectionOnly) carry it.
         if (WrapsInstancer(input))
         {
            mCache = src;
            if (selectionOnly)
            {
               // Move only the masked instances - GetInstanceGroupMatrix() backs
               // off to the upstream-unchanged matrix in this case (see its
               // comment) so the move isn't applied a second time to everyone.
               InstanceOnPointsNode* instancer = FindInstancer(input);
               const std::vector<Mat4>* upstreamOverride = input->InstanceTransformOverride();
               const std::vector<Mat4>& base =
                  upstreamOverride ? *upstreamOverride : instancer->InstanceTransforms();
               const std::vector<unsigned char>* mask = input->InstanceSelection();
               const Mat4 xform = TransformMatrix();
               mInstanceTransformOverride = base;
               if (mask)
               {
                  for (size_t i = 0; i < mInstanceTransformOverride.size(); i++)
                     if (i < mask->size() && (*mask)[i])
                        mInstanceTransformOverride[i] = Mat4::Multiply(xform, mInstanceTransformOverride[i]);
               }
               mHasInstanceTransformOverride = true;
            }
            else
            {
               mHasInstanceTransformOverride = false;
            }
         }
         else if (selectionOnly)
            mCache = MeshOps::TransformSelected(src, TransformMatrix(), moveAlongNormals, normalAmount);
         else
            mCache = MeshOps::Transform(src, TransformMatrix());
         break;
      case kArray:
         mCache = MeshOps::Array(src, count, offsetX, offsetY, offsetZ,
                                 rotStep * 3.14159265f / 180.0f, scaleStep, radial, radius);
         break;
      case kSubdivide:
         // Connectivity-dependent - partially subdividing needs crease
         // handling at the selection boundary to avoid cracks. Out of scope
         // for this phase; selectionOnly is hidden for this op in the UI.
         mCache = MeshOps::Subdivide(src, levels, smooth);
         break;
      case kSolidify:
         mCache = restricted([&](const Mesh& m) { return MeshOps::Solidify(m, thickness, keepOriginal); });
         break;
      case kExtrude:
         // MeshOps::Extrude already filters on Mesh::FaceSelected internally
         // (region-merged, handles n-gons) - selectionOnly just decides
         // whether the incoming mask reaches it or gets cleared first.
         mCache = MeshOps::Extrude(selectionOnly ? src : MeshOps::ClearSelection(src), thickness, inset);
         break;
      case kWireframe:
         mCache = restricted([&](const Mesh& m) { return MeshOps::Wireframe(m, thickness); });
         break;
      case kTriangulate:
         mCache = restricted([&](const Mesh& m) { return MeshOps::Triangulate(m, amount * 0.05f); });
         break;
      case kNormals:
         mCache = restricted([&](const Mesh& m) { return MeshOps::RecalculateNormals(m, flatShade, flipNormals); });
         break;
      case kExplode:
         mCache = restricted([&](const Mesh& m) { return MeshOps::Explode(m, amount * 0.3f, seed); });
         break;
      case kSmooth:
         // Connectivity-dependent - see kSubdivide. selectionOnly hidden.
         // Laplacian relaxation alone barely moves a low-poly primitive like a
         // Cube - it has no extra vertices between the corners to round out.
         // Subdividing first gives it geometry to actually smooth.
         mCache = MeshOps::Smooth(levels > 0 ? MeshOps::Subdivide(src, levels, 1.0f) : src,
                                  iterations, amount);
         break;
      case kMirror:
         // Duplicates the whole mesh - "only these faces" isn't meaningful.
         // selectionOnly hidden.
         mCache = MeshOps::Mirror(src, axis, mirrorOffset, weldSeam, keepOriginal);
         break;
      case kScrew:
         // Connectivity-dependent - see kSubdivide. selectionOnly hidden.
         mCache = MeshOps::Screw(src, screwSteps, turns, rise, radiusOffset, axis);
         break;
      case kDelete:
         if (selectionOnly && WrapsInstancer(input))
         {
            // Instance-domain delete: drop (or, with keepSelected, keep only)
            // the masked instances from the transform list instead of touching
            // the shared stamp mesh - see InstanceTransformOverride().
            mCache = src;
            InstanceOnPointsNode* instancer = FindInstancer(input);
            const std::vector<Mat4>* upstreamOverride = input->InstanceTransformOverride();
            const std::vector<Mat4>& base =
               upstreamOverride ? *upstreamOverride : instancer->InstanceTransforms();
            const std::vector<unsigned char>* mask = input->InstanceSelection();
            mInstanceTransformOverride.clear();
            mInstanceTransformOverride.reserve(base.size());
            for (size_t i = 0; i < base.size(); i++)
            {
               const bool selected = mask && i < mask->size() && (*mask)[i];
               // Named for what is kept, not what is dropped - same convention
               // as MeshOps::DeleteSelected's keepSelected.
               if (selected == keepSelected)
                  mInstanceTransformOverride.push_back(base[i]);
            }
            mHasInstanceTransformOverride = true;
         }
         else
         {
            mCache = MeshOps::DeleteSelected(selectionOnly ? src : MeshOps::ClearSelection(src), keepSelected);
            mHasInstanceTransformOverride = false;
         }
         break;
      case kSelect:
         if (WrapsInstancer(input))
         {
            // Instance-domain select: leave the shared stamp mesh alone and
            // mask instances instead - see InstanceSelection(). Mirrors
            // MeshOps::Select's per-mode math (Mesh.cpp), substituting each
            // instance's translation/local-Y for a face's centre/normal.
            mCache = src;
            InstanceOnPointsNode* instancer = FindInstancer(input);
            const std::vector<Mat4>* upstreamOverride = input->InstanceTransformOverride();
            const std::vector<Mat4>& transforms =
               upstreamOverride ? *upstreamOverride : instancer->InstanceTransforms();
            const std::vector<unsigned char>* previous = input->InstanceSelection();
            const size_t n = transforms.size();
            const int k = std::max(0, std::min(axis, 2));
            mInstanceSelection.assign(n, 0);
            for (size_t i = 0; i < n; i++)
            {
               bool hit;
               switch (selectMode)
               {
                  case MeshOps::kSelectIndex:
                  {
                     const long long start = (long long)selectA;
                     const long long cnt = (long long)selectB;
                     const long long stride = std::max(1LL, (long long)selectC);
                     const long long rel = (long long)i - start;
                     hit = rel >= 0 && (cnt <= 0 || rel < cnt * stride) && (rel % stride) == 0;
                     break;
                  }
                  case MeshOps::kSelectAxis:
                     hit = transforms[i].m[12 + k] >= selectA && transforms[i].m[12 + k] <= selectB;
                     break;
                  case MeshOps::kSelectNormal:
                  {
                     // Local +Y axis (column 1) - the instance's surface normal
                     // when alignToNormal is on (the default). With it off every
                     // instance shares one orientation and this mode degenerates
                     // to all-or-nothing, same as the tooltip should say.
                     const float normal[3] = { transforms[i].m[4], transforms[i].m[5], transforms[i].m[6] };
                     const float sign = (selectC >= 0.0f) ? 1.0f : -1.0f;
                     hit = (normal[k] * sign) >= selectA;
                     break;
                  }
                  case MeshOps::kSelectRandom:
                     hit = InstanceSelectHash(selectSeed, i) < selectA;
                     break;
                  case MeshOps::kSelectRadius:
                  {
                     const float dx = transforms[i].m[12] - selectA;
                     const float dy = transforms[i].m[13] - selectB;
                     const float dz = transforms[i].m[14] - selectC;
                     hit = std::sqrt(dx * dx + dy * dy + dz * dz) <= selectSeed;
                     break;
                  }
                  case MeshOps::kSelectAll:
                  default:
                     hit = true;
                     break;
               }
               if (selectInvert)
                  hit = !hit;
               if (selectAppend && previous && i < previous->size() && (*previous)[i])
                  hit = true;
               mInstanceSelection[i] = hit ? 1 : 0;
            }
            mInstanceSelectionRevision = NextMeshRevision();
         }
         else
         {
            mCache = MeshOps::Select(src, selectMode, selectA, selectB, selectC, axis,
                                     selectSeed, selectInvert, selectAppend);
         }
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
      {
         // kTwist - per-vertex, so selectionOnly derives a vertex mask from
         // the incoming face selection (a vertex counts as selected if any
         // touching face does - MeshOps::VertexSelectionFromFaces), rather
         // than the split/rejoin restricted() uses for per-face operators.
         const std::vector<unsigned char> vertexMask = MeshOps::VertexSelectionFromFaces(src);
         mCache = MeshOps::Twist(src, amount * 3.14159265f / 180.0f * 3.0f, axis,
                                 (selectionOnly && !src.faceMask.empty()) ? &vertexMask : nullptr);
         break;
      }
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

const std::vector<unsigned char>* GeometryOpNode::InstanceSelection() const
{
   if (op == kSelect && !bypassed && WrapsInstancer(input))
      return &mInstanceSelection;
   return input ? input->InstanceSelection() : nullptr;
}

unsigned long long GeometryOpNode::InstanceSelectionRevision() const
{
   if (op == kSelect && !bypassed && WrapsInstancer(input))
      return mInstanceSelectionRevision;
   return input ? input->InstanceSelectionRevision() : 0;
}

const std::vector<Mat4>* GeometryOpNode::InstanceTransformOverride() const
{
   if ((op == kDelete || op == kTransform) && !bypassed && selectionOnly &&
       WrapsInstancer(input) && mHasInstanceTransformOverride)
      return &mInstanceTransformOverride;
   return input ? input->InstanceTransformOverride() : nullptr;
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
   s.selectionOnly = selectionOnly;
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
   const std::vector<unsigned char> vertexMask = MeshOps::VertexSelectionFromFaces(src);
   mCache = MeshOps::Displace(src, mTexPixels, mTexW, mTexH, mode, strength, midlevel,
                              flatShade, flipNormals,
                              (selectionOnly && !src.faceMask.empty()) ? &vertexMask : nullptr);

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
   const std::vector<Particle>* cloud = cloudSource ? cloudSource->GetPointCloud() : nullptr;
   if (cloud != nullptr)
   {
      mTransforms.reserve(cloud->size());
      mColors.reserve(cloud->size() * 3);
      for (const Particle& p : *cloud)
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
   // HasGeometry(), not Empty(): a vertices-only mesh (Points to Vertices'
   // output) has no faces/edges to sample in modes 1/2, but pointMode 0
   // (vertices) works directly off vertex positions and doesn't touch
   // indices at all - see MeshOps::ToPoints.
   if (!srcLocal.HasGeometry())
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
   const unsigned long long cloudRevision = cloudSource ? cloudSource->PointCloudRevision() : 0;
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

// ======================================================== Set Color

namespace
{
   const std::vector<std::string> kSetColorSourceNames = {
      "Flat", "Position", "Normal", "Index", "Random", "Palette", "Texture"
   };

   // Hashes the swatches a palette source currently offers, so SetColorNode
   // can tell "the palette actually changed" from "nothing changed" the same
   // way DisplacementNode hashes texture pixels - IPaletteSource has no
   // revision stamp of its own to compare against instead.
   unsigned long long HashPalette(IPaletteSource* palette)
   {
      if (palette == nullptr)
         return 0;
      unsigned long long h = 1469598103934665603ull;
      const int count = palette->SwatchCount();
      for (int i = 0; i < count; i++)
      {
         float rgb[3];
         palette->GetSwatch(i, rgb);
         const unsigned char* bytes = reinterpret_cast<const unsigned char*>(rgb);
         for (size_t b = 0; b < sizeof(rgb); b++)
         {
            h ^= bytes[b];
            h *= 1099511628211ull;
         }
      }
      return h;
   }

   void SetColorElement(int source, size_t index, size_t count,
                        const float pos[3], const float normal[3], float u, float v, bool hasUV,
                        const float bboxMin[3], const float bboxMax[3],
                        const float flatColor[3], const float rampA[3], const float rampB[3],
                        float seed, IPaletteSource* palette, int paletteOffset,
                        const std::vector<float>& texPixels, int texW, int texH,
                        float outRgb[3])
   {
      switch (source)
      {
      case SetColorNode::kPosition:
         for (int c = 0; c < 3; c++)
         {
            const float span = bboxMax[c] - bboxMin[c];
            outRgb[c] = (span > 1e-8f) ? (pos[c] - bboxMin[c]) / span : 0.5f;
         }
         break;
      case SetColorNode::kNormal:
         outRgb[0] = normal[0] * 0.5f + 0.5f;
         outRgb[1] = normal[1] * 0.5f + 0.5f;
         outRgb[2] = normal[2] * 0.5f + 0.5f;
         break;
      case SetColorNode::kIndex:
      {
         const float t = (count > 1) ? (float)index / (float)(count - 1) : 0.0f;
         for (int c = 0; c < 3; c++)
            outRgb[c] = rampA[c] + (rampB[c] - rampA[c]) * t;
         break;
      }
      case SetColorNode::kRandom:
         outRgb[0] = Rand01(seed, (int)index * 3 + 0);
         outRgb[1] = Rand01(seed, (int)index * 3 + 1);
         outRgb[2] = Rand01(seed, (int)index * 3 + 2);
         break;
      case SetColorNode::kPalette:
      {
         const int swatchCount = palette ? palette->SwatchCount() : 0;
         if (swatchCount <= 0)
         {
            outRgb[0] = flatColor[0]; outRgb[1] = flatColor[1]; outRgb[2] = flatColor[2];
         }
         else
         {
            int swatch = ((int)index + paletteOffset) % swatchCount;
            if (swatch < 0)
               swatch += swatchCount;
            palette->GetSwatch(swatch, outRgb);
         }
         break;
      }
      case SetColorNode::kTexture:
         if (hasUV && texW > 0 && texH > 0 && (size_t)texW * texH * 4 <= texPixels.size())
         {
            int ix = (int)(u * (float)texW);
            int iy = (int)(v * (float)texH); // row 0 = bottom, matches Displace's texRGBA
            ix = std::max(0, std::min(ix, texW - 1));
            iy = std::max(0, std::min(iy, texH - 1));
            const size_t idx = ((size_t)iy * texW + ix) * 4;
            outRgb[0] = texPixels[idx + 0]; outRgb[1] = texPixels[idx + 1]; outRgb[2] = texPixels[idx + 2];
         }
         else
         {
            outRgb[0] = flatColor[0]; outRgb[1] = flatColor[1]; outRgb[2] = flatColor[2];
         }
         break;
      case SetColorNode::kFlat:
      default:
         outRgb[0] = flatColor[0]; outRgb[1] = flatColor[1]; outRgb[2] = flatColor[2];
         break;
      }
   }
}

const std::vector<std::string>& SetColorNode::SourceNames() { return kSetColorSourceNames; }

SetColorNode::~SetColorNode()
{
   if (mReadFbo != 0)
      glDeleteFramebuffers(1, &mReadFbo);
}

SetColorNode::Signature SetColorNode::CurrentSignature() const
{
   Signature s;
   s.source = source;
   s.flatColor[0] = flatColor[0]; s.flatColor[1] = flatColor[1]; s.flatColor[2] = flatColor[2];
   s.rampA[0] = rampA[0]; s.rampA[1] = rampA[1]; s.rampA[2] = rampA[2];
   s.rampB[0] = rampB[0]; s.rampB[1] = rampB[1]; s.rampB[2] = rampB[2];
   s.seed = seed;
   s.paletteOffset = paletteOffset;
   s.upstream = input;
   s.upstreamMeshRevision = input ? input->MeshRevision() : 0;
   s.upstreamCloudRevision = input ? input->PointCloudRevision() : 0;
   s.texGeneration = mTexGeneration;
   s.paletteHash = (source == kPalette) ? HashPalette(paletteInput) : 0;
   return s;
}

void SetColorNode::Rebuild()
{
   mCache = input ? input->GetMesh() : Mesh();
   const std::vector<Particle>* cloud = input ? input->GetPointCloud() : nullptr;
   mHasPointCache = cloud != nullptr;
   mPointCache = mHasPointCache ? *cloud : std::vector<Particle>();

   if (!mCache.vertices.empty())
   {
      float bboxMin[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
      float bboxMax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
      if (source == kPosition)
      {
         for (const Vertex& vert : mCache.vertices)
         {
            bboxMin[0] = std::min(bboxMin[0], vert.px); bboxMax[0] = std::max(bboxMax[0], vert.px);
            bboxMin[1] = std::min(bboxMin[1], vert.py); bboxMax[1] = std::max(bboxMax[1], vert.py);
            bboxMin[2] = std::min(bboxMin[2], vert.pz); bboxMax[2] = std::max(bboxMax[2], vert.pz);
         }
      }
      mCache.vertexColor.assign(mCache.vertices.size() * 3, 1.0f);
      const size_t n = mCache.vertices.size();
      for (size_t i = 0; i < n; i++)
      {
         const Vertex& vert = mCache.vertices[i];
         const float pos[3] = { vert.px, vert.py, vert.pz };
         const float normal[3] = { vert.nx, vert.ny, vert.nz };
         float rgb[3];
         SetColorElement(source, i, n, pos, normal, vert.u, vert.v, true, bboxMin, bboxMax,
                         flatColor, rampA, rampB, seed, paletteInput, paletteOffset,
                         mTexPixels, mTexW, mTexH, rgb);
         mCache.vertexColor[i * 3 + 0] = rgb[0];
         mCache.vertexColor[i * 3 + 1] = rgb[1];
         mCache.vertexColor[i * 3 + 2] = rgb[2];
      }
   }
   else
   {
      mCache.vertexColor.clear();
   }

   if (mHasPointCache && !mPointCache.empty())
   {
      float bboxMin[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
      float bboxMax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
      if (source == kPosition)
      {
         for (const Particle& p : mPointCache)
         {
            bboxMin[0] = std::min(bboxMin[0], p.px); bboxMax[0] = std::max(bboxMax[0], p.px);
            bboxMin[1] = std::min(bboxMin[1], p.py); bboxMax[1] = std::max(bboxMax[1], p.py);
            bboxMin[2] = std::min(bboxMin[2], p.pz); bboxMax[2] = std::max(bboxMax[2], p.pz);
         }
      }
      const size_t n = mPointCache.size();
      for (size_t i = 0; i < n; i++)
      {
         Particle& p = mPointCache[i];
         const float pos[3] = { p.px, p.py, p.pz };
         const float normal[3] = { p.nx, p.ny, p.nz };
         float rgb[3];
         // Particles carry no UV, so Texture has nothing to sample and falls
         // back to the flat colour - the hasUV=false branch in SetColorElement.
         SetColorElement(source, i, n, pos, normal, 0.0f, 0.0f, false, bboxMin, bboxMax,
                         flatColor, rampA, rampB, seed, paletteInput, paletteOffset,
                         mTexPixels, mTexW, mTexH, rgb);
         p.r = rgb[0]; p.g = rgb[1]; p.b = rgb[2];
      }
   }
}

const Mesh& SetColorNode::GetMesh()
{
   static const Mesh kEmptySetColorMesh;
   if (input == nullptr)
      return kEmptySetColorMesh;
   if (bypassed)
      return input->GetMesh();

   const Signature sig = CurrentSignature();
   if (mHasBuilt && sig == mBuilt)
      return mCache;

   Rebuild();
   mBuilt = sig;
   mHasBuilt = true;
   mMeshRevision = NextMeshRevision();
   mCloudRevision = NextMeshRevision();
   return mCache;
}

unsigned long long SetColorNode::MeshRevision()
{
   if (input == nullptr)
      return 0;
   if (bypassed)
      return input->MeshRevision();
   GetMesh();
   return mMeshRevision;
}

const std::vector<Particle>* SetColorNode::GetPointCloud()
{
   if (input == nullptr)
      return nullptr;
   if (bypassed)
      return input->GetPointCloud();
   GetMesh(); // shared rebuild path keeps mesh and point cache in sync
   return mHasPointCache ? &mPointCache : nullptr;
}

unsigned long long SetColorNode::PointCloudRevision()
{
   if (input == nullptr)
      return 0;
   if (bypassed)
      return input->PointCloudRevision();
   GetMesh();
   return mHasPointCache ? mCloudRevision : 0;
}

void SetColorNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);

   if (source == kTexture && mTextureInput.IsConnected())
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
