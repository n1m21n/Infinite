#pragma once

#include <string>
#include <vector>

#include "Platform.h"

// Decodes a .gltf/.glb file into geometry + up-to-six derived textures, all
// through cgltf rather than Platform::LoadModel (ModelIO does not read glTF -
// see ModelSourceNode.h). Kept separate from ModelSourceNode/ImageSourceNode
// so both can consume the same decoded package: ModelSourceNode::Load pulls
// the geometry half, and ImageSourceNode::Load (via the "gltf://" pseudo-path
// scheme) pulls one texture slot each.
//
// Scope, deliberately: first mesh, first primitive, first material only.
// TEXCOORD_0 only. The first skin, its animation clips and the primitive's
// morph targets are read (Skinning.h poses them); no Draco/meshopt
// compression, no KHR_materials_* extensions beyond the base PBR
// metallic-roughness set, no cameras/lights. A file
// with more than the first of any of these still loads - see
// hadMultipleMaterials/hadMultipleUVSets - it just doesn't use the rest.
namespace GltfImport
{
   struct GltfDecodedImage
   {
      std::vector<unsigned char> pixels; // RGBA8, empty if this map isn't present
      int width = 0;
      int height = 0;
      std::string label; // for status text / debugging, e.g. "albedo"
   };

   // One joint of the first skin, in parent-first order. `between` is the
   // rest transform of any plain (non-joint) nodes between this joint and
   // its parent joint, or the scene root when it has none.
   struct GltfJoint
   {
      std::string name;
      int parent = -1;
      float between[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
      float t[3] = { 0, 0, 0 };
      float r[4] = { 0, 0, 0, 1 }; // quaternion x, y, z, w
      float s[3] = { 1, 1, 1 };
      float inverseBind[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
   };

   // One animated property. joint -1 with path kWeights drives the morph
   // weights. Cubic spline keys keep only their value (tangents dropped).
   struct GltfChannel
   {
      enum Path { kTranslation = 0, kRotation, kScale, kWeights };
      int joint = -1;
      int path = kTranslation;
      bool step = false;
      int width = 3; // floats per key
      std::vector<float> times;
      std::vector<float> values;
   };

   struct GltfClip
   {
      std::string name;
      float duration = 0.0f;
      std::vector<GltfChannel> channels;
   };

   struct GltfDecodePackage
   {
      std::vector<Platform::ModelVertex> vertices;
      std::vector<unsigned int> indices;

      GltfDecodedImage albedo;
      GltfDecodedImage roughness; // channel-split from metallicRoughness.G, or a dedicated map if the file has one
      GltfDecodedImage metallic;  // channel-split from metallicRoughness.B
      GltfDecodedImage normalMap;
      GltfDecodedImage occlusion;
      GltfDecodedImage emissive;

      // Rig: four joints and weights per vertex, parallel to `vertices`;
      // empty when the file has no skin.
      std::vector<unsigned short> skinJoints;
      std::vector<float> skinWeights;
      std::vector<GltfJoint> joints;
      std::vector<GltfClip> clips;
      // Morph targets: position deltas, three floats per vertex each.
      std::vector<std::string> morphNames;
      std::vector<std::vector<float>> morphDeltas;
      std::vector<float> morphDefaults;

      bool hadMultipleMaterials = false;
      bool hadMultipleUVSets = false;
   };

   // Parses and decodes `path` (.gltf or .glb), caching the result by path
   // (AssetCache - mtime/size validated) so repeated calls for the same file
   // within a session - e.g. ModelSourceNode::Load followed by main.cpp's
   // drop handler re-deriving textures - are cache hits, not re-parses.
   // Returns nullptr and fills outError on failure; the returned pointer is
   // only valid until the next DecodeCached() call for a different path.
   const GltfDecodePackage* DecodeCached(const std::string& path, std::string& outError);
}
