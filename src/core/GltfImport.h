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
// TEXCOORD_0 only. No animation/skinning, no Draco/meshopt compression, no
// KHR_materials_* extensions beyond the base PBR metallic-roughness set, no
// scene graph/cameras/lights, no sparse accessors, no morph targets. A file
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
