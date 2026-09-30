#include "GltfImport.h"

#include <cmath>
#include <cstring>

#include "AssetCache.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace
{
   using GltfImport::GltfDecodedImage;
   using GltfImport::GltfDecodePackage;

   // Shared across every ModelSourceNode/ImageSourceNode respawn in the
   // process, same pattern as GetModelDecodeCache()/GetImageDecodeCache() -
   // see AssetCache.h. One glTF file can be asked for twice in a row (Model
   // 3D's geometry load, then the drop handler's texture spawn), so this
   // cache is what makes the second ask free.
   AssetCache<GltfDecodePackage>& GetGltfDecodeCache()
   {
      static AssetCache<GltfDecodePackage> cache(512ull * 1024 * 1024);
      return cache;
   }

   std::string DirOf(const std::string& path)
   {
      const size_t slash = path.find_last_of("/\\");
      return (slash == std::string::npos) ? std::string() : path.substr(0, slash);
   }

   // Minimal base64 decoder for embedded image data-URIs. cgltf's own
   // cgltf_load_buffer_base64 exists but wants the exact decoded size up
   // front (fine for buffers, whose byteLength is known from the JSON) -
   // images have no such field, so this decodes directly off the string
   // instead of pre-computing a size.
   bool DecodeBase64(const char* data, size_t len, std::vector<unsigned char>& out)
   {
      auto decodeChar = [](char c) -> int {
         if (c >= 'A' && c <= 'Z') return c - 'A';
         if (c >= 'a' && c <= 'z') return c - 'a' + 26;
         if (c >= '0' && c <= '9') return c - '0' + 52;
         if (c == '+') return 62;
         if (c == '/') return 63;
         return -1;
      };

      out.clear();
      out.reserve(len * 3 / 4 + 3);
      unsigned int buffer = 0;
      int bufferBits = 0;
      for (size_t i = 0; i < len; i++)
      {
         const char c = data[i];
         if (c == '=' || c == '\n' || c == '\r' || c == ' ')
            continue;
         const int v = decodeChar(c);
         if (v < 0)
            return false;
         buffer = (buffer << 6) | (unsigned int)v;
         bufferBits += 6;
         if (bufferBits >= 8)
         {
            bufferBits -= 8;
            out.push_back((unsigned char)((buffer >> bufferBits) & 0xFF));
         }
      }
      return true;
   }

   // Resolves and decodes one cgltf_image, whether it's embedded (bufferView,
   // already loaded into memory by cgltf_load_buffers), a base64 data-URI, or
   // an external file relative to the .gltf/.glb's own directory.
   bool DecodeCgltfImage(const cgltf_image* img, const std::string& dir, const char* label,
                        GltfDecodedImage& out, std::string& outError)
   {
      if (img == nullptr)
      {
         outError = "no image";
         return false;
      }

      out.label = label;

      if (img->buffer_view != nullptr)
      {
         const cgltf_buffer_view* bv = img->buffer_view;
         if (bv->buffer == nullptr || bv->buffer->data == nullptr)
         {
            outError = "image bufferView has no data";
            return false;
         }
         const unsigned char* base = (const unsigned char*)bv->buffer->data + bv->offset;
         std::vector<unsigned char> bytes(base, base + bv->size);
         return Platform::LoadImageRGBAFromMemory(bytes, out.pixels, out.width, out.height, outError);
      }

      if (img->uri != nullptr && img->uri[0] != '\0')
      {
         const std::string uri = img->uri;
         const std::string kDataPrefix = "data:";
         if (uri.compare(0, kDataPrefix.size(), kDataPrefix) == 0)
         {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos)
            {
               outError = "malformed data URI";
               return false;
            }
            std::vector<unsigned char> bytes;
            if (!DecodeBase64(uri.data() + comma + 1, uri.size() - comma - 1, bytes))
            {
               outError = "malformed base64 data URI";
               return false;
            }
            return Platform::LoadImageRGBAFromMemory(bytes, out.pixels, out.width, out.height, outError);
         }

         // External file, path relative to the .gltf's own directory. Copy the
         // URI before percent-decoding it in place - cgltf_decode_uri mutates
         // its argument - and unescape %20 etc, which real-world exporters
         // (Sketchfab among them) use for filenames with spaces.
         std::vector<char> mutableUri(uri.begin(), uri.end());
         mutableUri.push_back('\0');
         cgltf_decode_uri(mutableUri.data());
         const std::string relPath = mutableUri.data();
         const std::string fullPath = dir.empty() ? relPath : (dir + "/" + relPath);
         return Platform::LoadImageRGBA(fullPath, out.pixels, out.width, out.height, outError);
      }

      outError = "image has neither bufferView nor uri";
      return false;
   }

   // glTF's metallicRoughnessTexture packs roughness into G and metallic into
   // B of one combined image (KHR spec channel assignment). Infinite's shader
   // has no concept of a combined map - Geometry3DNodes.cpp samples roughness
   // and metallic as two independent single-channel textures - so this splits
   // the decoded RGBA8 buffer into two full RGBA8 images (channel replicated
   // into RGB, alpha opaque) rather than touching the shader.
   void SplitMetallicRoughness(const GltfDecodedImage& src, GltfDecodedImage& outRoughness,
                               GltfDecodedImage& outMetallic)
   {
      if (src.pixels.empty())
         return;

      const int w = src.width, h = src.height;
      outRoughness.width = w; outRoughness.height = h; outRoughness.label = "roughness";
      outMetallic.width = w; outMetallic.height = h; outMetallic.label = "metallic";
      outRoughness.pixels.resize((size_t)w * h * 4);
      outMetallic.pixels.resize((size_t)w * h * 4);

      const size_t count = (size_t)w * h;
      for (size_t i = 0; i < count; i++)
      {
         const unsigned char g = src.pixels[i * 4 + 1];
         const unsigned char b = src.pixels[i * 4 + 2];
         outRoughness.pixels[i * 4 + 0] = g;
         outRoughness.pixels[i * 4 + 1] = g;
         outRoughness.pixels[i * 4 + 2] = g;
         outRoughness.pixels[i * 4 + 3] = 255;
         outMetallic.pixels[i * 4 + 0] = b;
         outMetallic.pixels[i * 4 + 1] = b;
         outMetallic.pixels[i * 4 + 2] = b;
         outMetallic.pixels[i * 4 + 3] = 255;
      }
   }

   // Rebuilds smooth per-vertex normals for a file that has none, by
   // accumulating face normals across shared indices then normalizing -
   // matches the promise in Platform::LoadModel's own doc comment ("Normals
   // are generated when the file has none").
   void GenerateNormals(std::vector<Platform::ModelVertex>& vertices, const std::vector<unsigned int>& indices)
   {
      for (auto& v : vertices) { v.nx = 0; v.ny = 0; v.nz = 0; }

      for (size_t i = 0; i + 2 < indices.size(); i += 3)
      {
         Platform::ModelVertex& a = vertices[indices[i + 0]];
         Platform::ModelVertex& b = vertices[indices[i + 1]];
         Platform::ModelVertex& c = vertices[indices[i + 2]];
         const float e1x = b.px - a.px, e1y = b.py - a.py, e1z = b.pz - a.pz;
         const float e2x = c.px - a.px, e2y = c.py - a.py, e2z = c.pz - a.pz;
         const float nx = e1y * e2z - e1z * e2y;
         const float ny = e1z * e2x - e1x * e2z;
         const float nz = e1x * e2y - e1y * e2x;
         a.nx += nx; a.ny += ny; a.nz += nz;
         b.nx += nx; b.ny += ny; b.nz += nz;
         c.nx += nx; c.ny += ny; c.nz += nz;
      }

      for (auto& v : vertices)
      {
         const float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
         if (len > 1e-8f) { v.nx /= len; v.ny /= len; v.nz /= len; }
         else { v.nx = 0; v.ny = 0; v.nz = 1; }
      }
   }
}

namespace GltfImport
{
   const GltfDecodePackage* DecodeCached(const std::string& path, std::string& outError)
   {
      auto& cache = GetGltfDecodeCache();
      if (const GltfDecodePackage* hit = cache.Get(path))
         return hit;

      cgltf_options options;
      std::memset(&options, 0, sizeof(options));
      cgltf_data* data = nullptr;
      cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
      if (result != cgltf_result_success)
      {
         outError = "could not parse glTF file";
         return nullptr;
      }

      result = cgltf_load_buffers(&options, data, path.c_str());
      if (result != cgltf_result_success)
      {
         cgltf_free(data);
         outError = "could not load glTF buffers (missing .bin or texture file?)";
         return nullptr;
      }

      if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0)
      {
         cgltf_free(data);
         outError = "glTF file has no mesh geometry";
         return nullptr;
      }

      const cgltf_mesh& mesh = data->meshes[0];
      const cgltf_primitive& prim = mesh.primitives[0];

      const cgltf_accessor* posAcc = nullptr;
      const cgltf_accessor* normAcc = nullptr;
      const cgltf_accessor* uvAcc = nullptr;
      int uvSetsSeen = 0;
      for (cgltf_size i = 0; i < prim.attributes_count; i++)
      {
         const cgltf_attribute& attr = prim.attributes[i];
         if (attr.type == cgltf_attribute_type_position && posAcc == nullptr)
            posAcc = attr.data;
         else if (attr.type == cgltf_attribute_type_normal && normAcc == nullptr)
            normAcc = attr.data;
         else if (attr.type == cgltf_attribute_type_texcoord)
         {
            uvSetsSeen++;
            if (attr.index == 0)
               uvAcc = attr.data;
         }
      }

      if (posAcc == nullptr)
      {
         cgltf_free(data);
         outError = "glTF primitive has no POSITION attribute";
         return nullptr;
      }

      GltfDecodePackage pkg;
      const size_t vertexCount = posAcc->count;
      pkg.vertices.resize(vertexCount);

      {
         std::vector<float> buf(vertexCount * 3);
         cgltf_accessor_unpack_floats(posAcc, buf.data(), buf.size());
         for (size_t i = 0; i < vertexCount; i++)
         {
            pkg.vertices[i].px = buf[i * 3 + 0];
            pkg.vertices[i].py = buf[i * 3 + 1];
            pkg.vertices[i].pz = buf[i * 3 + 2];
         }
      }

      if (normAcc != nullptr && normAcc->count == vertexCount)
      {
         std::vector<float> buf(vertexCount * 3);
         cgltf_accessor_unpack_floats(normAcc, buf.data(), buf.size());
         for (size_t i = 0; i < vertexCount; i++)
         {
            pkg.vertices[i].nx = buf[i * 3 + 0];
            pkg.vertices[i].ny = buf[i * 3 + 1];
            pkg.vertices[i].nz = buf[i * 3 + 2];
         }
      }

      if (uvAcc != nullptr && uvAcc->count == vertexCount)
      {
         std::vector<float> buf(vertexCount * 2);
         cgltf_accessor_unpack_floats(uvAcc, buf.data(), buf.size());
         for (size_t i = 0; i < vertexCount; i++)
         {
            pkg.vertices[i].u = buf[i * 2 + 0];
            pkg.vertices[i].v = buf[i * 2 + 1];
         }
      }

      if (prim.indices != nullptr)
      {
         const size_t indexCount = prim.indices->count;
         pkg.indices.resize(indexCount);
         for (size_t i = 0; i < indexCount; i++)
            pkg.indices[i] = (unsigned int)cgltf_accessor_read_index(prim.indices, i);
      }
      else
      {
         pkg.indices.resize(vertexCount);
         for (size_t i = 0; i < vertexCount; i++)
            pkg.indices[i] = (unsigned int)i;
      }

      if (normAcc == nullptr)
         GenerateNormals(pkg.vertices, pkg.indices);

      pkg.hadMultipleMaterials = data->materials_count > 1 || mesh.primitives_count > 1 || data->meshes_count > 1;
      pkg.hadMultipleUVSets = uvSetsSeen > 1;

      // First material's texture set, if any.
      if (prim.material != nullptr)
      {
         const cgltf_material& mat = *prim.material;
         std::string err;
         const std::string dir = DirOf(path);

         if (mat.has_pbr_metallic_roughness)
         {
            if (mat.pbr_metallic_roughness.base_color_texture.texture != nullptr)
               DecodeCgltfImage(mat.pbr_metallic_roughness.base_color_texture.texture->image, dir, "albedo",
                                pkg.albedo, err);

            if (mat.pbr_metallic_roughness.metallic_roughness_texture.texture != nullptr)
            {
               GltfDecodedImage combined;
               if (DecodeCgltfImage(mat.pbr_metallic_roughness.metallic_roughness_texture.texture->image, dir,
                                    "metallicRoughness", combined, err))
                  SplitMetallicRoughness(combined, pkg.roughness, pkg.metallic);
            }
         }

         if (mat.normal_texture.texture != nullptr)
            DecodeCgltfImage(mat.normal_texture.texture->image, dir, "normal", pkg.normalMap, err);
         if (mat.occlusion_texture.texture != nullptr)
            DecodeCgltfImage(mat.occlusion_texture.texture->image, dir, "occlusion", pkg.occlusion, err);
         if (mat.emissive_texture.texture != nullptr)
            DecodeCgltfImage(mat.emissive_texture.texture->image, dir, "emissive", pkg.emissive, err);
      }

      cgltf_free(data);

      size_t bytes = pkg.vertices.size() * sizeof(Platform::ModelVertex) +
                     pkg.indices.size() * sizeof(unsigned int);
      for (const GltfDecodedImage* img : { &pkg.albedo, &pkg.roughness, &pkg.metallic, &pkg.normalMap,
                                           &pkg.occlusion, &pkg.emissive })
         bytes += img->pixels.size();

      cache.Put(path, pkg, bytes);
      return cache.Get(path);
   }
}
