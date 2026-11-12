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
// Every mesh in the default scene is read, placed by its node, and split into
// one part per material (part 0 is the package itself, the rest are in
// `parts`). TEXCOORD_0 only. The skin, its animation clips and a lone
// primitive's morph targets are read (Skinning.h poses them); no Draco/meshopt
// compression, no cameras/lights. Row 7.1: KHR_materials_* clearcoat, transmission, ior, specular, sheen,
// emissive_strength, iridescence, anisotropy, dispersion, diffuse_transmission and volume, plus
// KHR_texture_transform (the base colour map's, else the first map that has one), are read into the part.
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
      GltfDecodedImage clearcoatMap;    // KHR_materials_clearcoat texture (R)
      GltfDecodedImage sheenMap;        // KHR_materials_sheen colour texture
      GltfDecodedImage transmissionMap; // KHR_materials_transmission texture (R)

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

      // The part's material: its name and base factors (glTF defaults when it has none).
      std::string materialName;
      float baseColor[4] = { 1, 1, 1, 1 };
      float metallicFactor = 1.0f;
      float roughnessFactor = 1.0f;
      float specular = 0.5f;   // Material's specular (0.5 = F0 0.04); spec-gloss files set it from their specular colour
      float emissiveFactor[3] = { 0, 0, 0 };
      float emissiveStrength = 1.0f;
      float ior = 1.5f;
      float transmission = 0.0f, clearcoat = 0.0f, clearcoatRoughness = 0.0f;
      float sheen = 0.0f, sheenColor[3] = { 1, 1, 1 }, sheenRoughness = 0.0f;
      float iridescence = 0.0f, iridescenceIor = 1.3f, iridescenceThickness = 400.0f;
      float anisotropy = 0.0f, anisotropyRotation = 0.0f; // rotation in radians, as the file has it
      float dispersion = 0.0f;                            // the file's 20 / Abbe
      float translucency = 0.0f;                          // KHR_materials_diffuse_transmission
      float thickness = 0.0f, attenuationColor[3] = { 1, 1, 1 }, attenuationDistance = 0.0f; // volume, 0 = none
      bool hasUvTransform = false;
      float uvOffset[2] = { 0, 0 }, uvRotation = 0.0f, uvScale[2] = { 1, 1 };

      bool hadMultipleUVSets = false;

      // Parts 1.. of the file (one per further material); empty on a part.
      std::vector<GltfDecodePackage> parts;
      int PartCount() const { return 1 + (int)parts.size(); }
      // Part n, or nullptr past the end.
      const GltfDecodePackage* Part(int n) const
      {
         return n == 0 ? this : (n > 0 && n <= (int)parts.size()) ? &parts[n - 1] : nullptr;
      }
   };

   // Parses and decodes `path` (.gltf or .glb), caching the result by path
   // (AssetCache - mtime/size validated) so repeated calls for the same file
   // within a session - e.g. ModelSourceNode::Load followed by main.cpp's
   // drop handler re-deriving textures - are cache hits, not re-parses.
   // Returns nullptr and fills outError on failure; the returned pointer is
   // only valid until the next DecodeCached() call for a different path.
   const GltfDecodePackage* DecodeCached(const std::string& path, std::string& outError);
}
