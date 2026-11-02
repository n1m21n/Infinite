#pragma once

#include <vector>

#include "GltfImport.h"

// Poses a skinned glTF package on the CPU, so every geometry op after Model 3D
// sees the moved mesh. Morph targets apply first, then linear-blend skinning
// with four weights per vertex (the glTF rule, same maths as Blender's
// armature deform: sum of weight * jointMatrix * v, divided by the weight
// total, vertices with no weight left as they are).
namespace Skinning
{
   // `clip` indexes pkg.clips, -1 is the rest pose. `time` is in the clip's
   // seconds and is wrapped into [0, duration). `morphWeights` adds on top of
   // any weights the clip animates. `out` gets one vertex per pkg.vertices,
   // positions and normals posed, UVs copied. When `jointWorld` is given it
   // gets 16 floats per joint: the joint's posed world matrix (column-major).
   // `poseOffsets` (4 floats per joint, a quaternion, or empty) turns each
   // joint after its clip rotation, in the joint's own frame: Blender's pose
   // bone rotation on top of the action.
   void Evaluate(const GltfImport::GltfDecodePackage& pkg, int clip, float time,
                 const std::vector<float>& morphWeights,
                 std::vector<Platform::ModelVertex>& out,
                 std::vector<float>* jointWorld = nullptr,
                 const std::vector<float>* poseOffsets = nullptr);

   // Euler degrees (X, then Y, then Z, about fixed axes: R = Rz * Ry * Rx)
   // to and from a quaternion (x, y, z, w).
   void QuatFromEuler(const float deg[3], float q[4]);
   void EulerFromQuat(const float q[4], float deg[3]);
   void QuatMul(const float a[4], const float b[4], float out[4]);

   bool HasRig(const GltfImport::GltfDecodePackage& pkg);
}
