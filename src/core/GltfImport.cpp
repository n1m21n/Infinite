#include "GltfImport.h"

#include <algorithm>
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

   // Rotation part of a column-major matrix with no shear, as a unit
   // quaternion (x, y, z, w); scale is divided out first.
   void MatrixToTrs(const float m[16], float t[3], float r[4], float sc[3])
   {
      t[0] = m[12]; t[1] = m[13]; t[2] = m[14];
      for (int c = 0; c < 3; c++)
         sc[c] = std::sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]);
      float a[3][3]; // a[row][col]
      for (int c = 0; c < 3; c++)
         for (int row = 0; row < 3; row++)
            a[row][c] = sc[c] > 1e-12f ? m[c * 4 + row] / sc[c] : 0.0f;
      const float tr = a[0][0] + a[1][1] + a[2][2];
      if (tr > 0.0f)
      {
         const float k = std::sqrt(tr + 1.0f) * 2.0f;
         r[3] = 0.25f * k; r[0] = (a[2][1] - a[1][2]) / k; r[1] = (a[0][2] - a[2][0]) / k; r[2] = (a[1][0] - a[0][1]) / k;
      }
      else if (a[0][0] > a[1][1] && a[0][0] > a[2][2])
      {
         const float k = std::sqrt(1.0f + a[0][0] - a[1][1] - a[2][2]) * 2.0f;
         r[3] = (a[2][1] - a[1][2]) / k; r[0] = 0.25f * k; r[1] = (a[0][1] + a[1][0]) / k; r[2] = (a[0][2] + a[2][0]) / k;
      }
      else if (a[1][1] > a[2][2])
      {
         const float k = std::sqrt(1.0f + a[1][1] - a[0][0] - a[2][2]) * 2.0f;
         r[3] = (a[0][2] - a[2][0]) / k; r[0] = (a[0][1] + a[1][0]) / k; r[1] = 0.25f * k; r[2] = (a[1][2] + a[2][1]) / k;
      }
      else
      {
         const float k = std::sqrt(1.0f + a[2][2] - a[0][0] - a[1][1]) * 2.0f;
         r[3] = (a[1][0] - a[0][1]) / k; r[0] = (a[0][2] + a[2][0]) / k; r[1] = (a[1][2] + a[2][1]) / k; r[2] = 0.25f * k;
      }
   }

   void MulMat(const float a[16], const float b[16], float out[16])
   {
      float o[16];
      for (int c = 0; c < 4; c++)
         for (int row = 0; row < 4; row++)
         {
            float v = 0.0f;
            for (int k = 0; k < 4; k++)
               v += a[k * 4 + row] * b[c * 4 + k];
            o[c * 4 + row] = v;
         }
      std::memcpy(out, o, sizeof(o));
   }

   // The first skin (preferring the one on the node that shows mesh 0), its
   // joints parent-first, and every clip that moves those joints or the
   // mesh's morph weights.
   // One primitive as placed in the scene: its node's world matrix, unless a
   // skin places it (a skinned mesh's node transform is ignored, per the spec).
   struct PrimRef
   {
      const cgltf_primitive* prim = nullptr;
      const cgltf_mesh* mesh = nullptr;
      const cgltf_node* node = nullptr;
      float world[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
      bool skinned = false;
      size_t firstVertex = 0, vertexCount = 0;
   };

   const cgltf_accessor* SkinAccessor(const cgltf_primitive& prim, cgltf_attribute_type type)
   {
      for (cgltf_size i = 0; i < prim.attributes_count; i++)
         if (prim.attributes[i].type == type && prim.attributes[i].index == 0)
            return prim.attributes[i].data;
      return nullptr;
   }

   void ReadRig(const cgltf_data* data, const std::vector<PrimRef>& refs, GltfImport::GltfDecodePackage& pkg)
   {
      const cgltf_mesh* mesh0 = refs[0].mesh;
      const cgltf_primitive& prim = *refs[0].prim;
      const size_t vertexCount = pkg.vertices.size();

      // Morph targets (positions only), when the part is one primitive.
      for (cgltf_size t = 0; refs.size() == 1 && t < prim.targets_count; t++)
      {
         std::vector<float> deltas(vertexCount * 3, 0.0f);
         for (cgltf_size a = 0; a < prim.targets[t].attributes_count; a++)
         {
            const cgltf_attribute& attr = prim.targets[t].attributes[a];
            if (attr.type == cgltf_attribute_type_position && attr.data->count == vertexCount)
               cgltf_accessor_unpack_floats(attr.data, deltas.data(), deltas.size());
         }
         pkg.morphDeltas.push_back(std::move(deltas));
         pkg.morphNames.push_back(t < mesh0->target_names_count && mesh0->target_names[t] != nullptr
                                     ? std::string(mesh0->target_names[t]) : "morph " + std::to_string(t + 1));
         pkg.morphDefaults.push_back(t < mesh0->weights_count ? mesh0->weights[t] : 0.0f);
      }

      const cgltf_skin* skin = refs[0].node != nullptr ? refs[0].node->skin : nullptr;
      if (skin == nullptr && data->skins_count > 0)
         skin = &data->skins[0];

      // Every primitive of the part needs joints and weights, or none is skinned.
      bool weighted = true;
      for (const PrimRef& r : refs)
      {
         const cgltf_accessor* j = SkinAccessor(*r.prim, cgltf_attribute_type_joints);
         const cgltf_accessor* w = SkinAccessor(*r.prim, cgltf_attribute_type_weights);
         weighted = weighted && r.skinned && j != nullptr && w != nullptr && j->count == r.vertexCount && w->count == r.vertexCount;
      }

      std::vector<const cgltf_node*> nodes; // joint index -> node, parent-first
      std::vector<int> fileToOrdered;
      if (skin != nullptr && skin->joints_count > 0 && weighted)
      {
         auto jointOf = [&](const cgltf_node* n) {
            for (cgltf_size j = 0; j < skin->joints_count; j++)
               if (skin->joints[j] == n)
                  return (int)j;
            return -1;
         };
         auto depth = [](const cgltf_node* n) { int d = 0; for (; n->parent != nullptr; n = n->parent) d++; return d; };
         std::vector<int> order(skin->joints_count);
         for (size_t j = 0; j < order.size(); j++)
            order[j] = (int)j;
         std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depth(skin->joints[a]) < depth(skin->joints[b]); });
         fileToOrdered.assign(order.size(), 0);
         for (size_t k = 0; k < order.size(); k++)
            fileToOrdered[order[k]] = (int)k;

         for (size_t k = 0; k < order.size(); k++)
         {
            const cgltf_node* n = skin->joints[order[k]];
            nodes.push_back(n);
            GltfImport::GltfJoint j;
            j.name = n->name != nullptr ? n->name : "joint " + std::to_string(k + 1);
            float local[16];
            cgltf_node_transform_local(n, local);
            MatrixToTrs(local, j.t, j.r, j.s);
            if (!n->has_matrix)
            {
               if (n->has_rotation) std::memcpy(j.r, n->rotation, sizeof(j.r));
               if (n->has_scale) std::memcpy(j.s, n->scale, sizeof(j.s));
            }
            const cgltf_node* p = n->parent;
            while (p != nullptr && jointOf(p) < 0)
            {
               float pl[16];
               cgltf_node_transform_local(p, pl);
               MulMat(pl, j.between, j.between);
               p = p->parent;
            }
            j.parent = p != nullptr ? fileToOrdered[jointOf(p)] : -1;
            if (skin->inverse_bind_matrices != nullptr)
               cgltf_accessor_read_float(skin->inverse_bind_matrices, order[k], j.inverseBind, 16);
            pkg.joints.push_back(j);
         }

         pkg.skinJoints.resize(vertexCount * 4);
         pkg.skinWeights.resize(vertexCount * 4);
         for (const PrimRef& r : refs)
         {
            const cgltf_accessor* jointAcc = SkinAccessor(*r.prim, cgltf_attribute_type_joints);
            const cgltf_accessor* weightAcc = SkinAccessor(*r.prim, cgltf_attribute_type_weights);
            for (size_t i = 0; i < r.vertexCount; i++)
            {
               const size_t v = r.firstVertex + i;
               cgltf_uint ji[4] = { 0, 0, 0, 0 };
               cgltf_accessor_read_uint(jointAcc, i, ji, 4);
               cgltf_accessor_read_float(weightAcc, i, &pkg.skinWeights[v * 4], 4);
               for (int k = 0; k < 4; k++)
                  pkg.skinJoints[v * 4 + k] = (unsigned short)(ji[k] < fileToOrdered.size() ? fileToOrdered[ji[k]] : 0);
            }
         }
      }

      for (cgltf_size a = 0; a < data->animations_count; a++)
      {
         const cgltf_animation& anim = data->animations[a];
         GltfImport::GltfClip clip;
         clip.name = anim.name != nullptr ? anim.name : "clip " + std::to_string(a + 1);
         for (cgltf_size c = 0; c < anim.channels_count; c++)
         {
            const cgltf_animation_channel& ch = anim.channels[c];
            if (ch.target_node == nullptr || ch.sampler == nullptr)
               continue;
            GltfImport::GltfChannel out;
            switch (ch.target_path)
            {
            case cgltf_animation_path_type_translation: out.path = GltfImport::GltfChannel::kTranslation; out.width = 3; break;
            case cgltf_animation_path_type_rotation: out.path = GltfImport::GltfChannel::kRotation; out.width = 4; break;
            case cgltf_animation_path_type_scale: out.path = GltfImport::GltfChannel::kScale; out.width = 3; break;
            case cgltf_animation_path_type_weights: out.path = GltfImport::GltfChannel::kWeights; out.width = (int)pkg.morphDeltas.size(); break;
            default: continue;
            }
            if (out.path == GltfImport::GltfChannel::kWeights)
            {
               if (ch.target_node->mesh != mesh0 || out.width == 0)
                  continue;
            }
            else
            {
               const auto it = std::find(nodes.begin(), nodes.end(), ch.target_node);
               if (it == nodes.end())
                  continue;
               out.joint = (int)(it - nodes.begin());
            }
            const cgltf_animation_sampler& sm = *ch.sampler;
            out.step = sm.interpolation == cgltf_interpolation_type_step;
            const bool cubic = sm.interpolation == cgltf_interpolation_type_cubic_spline;
            const size_t keys = sm.input->count;
            out.times.resize(keys);
            cgltf_accessor_unpack_floats(sm.input, out.times.data(), keys);
            std::vector<float> raw(sm.output->count * cgltf_num_components(sm.output->type));
            cgltf_accessor_unpack_floats(sm.output, raw.data(), raw.size());
            const size_t perKey = (size_t)out.width * (cubic ? 3 : 1);
            if (raw.size() < keys * perKey)
               continue;
            out.values.resize(keys * out.width);
            for (size_t k = 0; k < keys; k++)
               std::memcpy(&out.values[k * out.width], &raw[k * perKey + (cubic ? out.width : 0)], out.width * sizeof(float));
            if (!out.times.empty())
               clip.duration = std::max(clip.duration, out.times.back());
            clip.channels.push_back(std::move(out));
         }
         if (!clip.channels.empty())
            pkg.clips.push_back(std::move(clip));
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

      // A model file is outside input, and cgltf's accessor reads trust it:
      // an accessor past its buffer view reads past the buffer, and an index
      // past the vertex count makes GenerateNormals write out of bounds.
      // cgltf_validate checks both (and the rest of the file's extents).
      if (cgltf_validate(data) != cgltf_result_success)
      {
         cgltf_free(data);
         outError = "glTF file is damaged (an accessor or index points outside its data)";
         return nullptr;
      }

      // Every triangle primitive in the default scene, placed by its node,
      // grouped by material in the order they first appear.
      std::vector<const cgltf_material*> mats;
      std::vector<std::vector<PrimRef>> groups;
      auto addPrims = [&](const cgltf_mesh* mesh, const cgltf_node* node) {
         for (cgltf_size p = 0; p < mesh->primitives_count; p++)
         {
            const cgltf_primitive& prim = mesh->primitives[p];
            if (prim.type != cgltf_primitive_type_triangles)
               continue;
            PrimRef r;
            r.prim = &prim;
            r.mesh = mesh;
            r.node = node;
            r.skinned = (node != nullptr && node->skin != nullptr) ||
                        (data->skins_count > 0 && SkinAccessor(prim, cgltf_attribute_type_joints) != nullptr);
            if (node != nullptr && !r.skinned)
               cgltf_node_transform_world(node, r.world);
            const auto it = std::find(mats.begin(), mats.end(), prim.material);
            if (it == mats.end())
            {
               mats.push_back(prim.material);
               groups.push_back({ r });
            }
            else
               groups[it - mats.begin()].push_back(r);
         }
      };
      std::vector<const cgltf_node*> stack;
      const cgltf_scene* scene = data->scene != nullptr ? data->scene : data->scenes_count > 0 ? &data->scenes[0] : nullptr;
      if (scene != nullptr)
         for (cgltf_size i = scene->nodes_count; i-- > 0;)
            stack.push_back(scene->nodes[i]);
      else
         for (cgltf_size i = data->nodes_count; i-- > 0;)
            if (data->nodes[i].parent == nullptr)
               stack.push_back(&data->nodes[i]);
      while (!stack.empty())
      {
         const cgltf_node* n = stack.back();
         stack.pop_back();
         if (n->mesh != nullptr)
            addPrims(n->mesh, n);
         for (cgltf_size c = n->children_count; c-- > 0;)
            stack.push_back(n->children[c]);
      }
      if (groups.empty())   // meshes no node places
         for (cgltf_size m = 0; m < data->meshes_count; m++)
            addPrims(&data->meshes[m], nullptr);
      if (groups.empty())
      {
         cgltf_free(data);
         outError = "glTF file has no mesh geometry";
         return nullptr;
      }

      const std::string dir = DirOf(path);
      std::vector<GltfDecodePackage> built(groups.size());
      for (size_t g = 0; g < groups.size(); g++)
      {
         GltfDecodePackage& pkg = built[g];
         std::vector<PrimRef>& refs = groups[g];
         int uvSetsSeen = 0;
         bool anyMissingNormals = false;
         for (PrimRef& r : refs)
         {
            const cgltf_primitive& prim = *r.prim;
            const cgltf_accessor* posAcc = nullptr;
            const cgltf_accessor* normAcc = nullptr;
            const cgltf_accessor* uvAcc = nullptr;
            int uvSets = 0;
            for (cgltf_size i = 0; i < prim.attributes_count; i++)
            {
               const cgltf_attribute& attr = prim.attributes[i];
               if (attr.type == cgltf_attribute_type_position && posAcc == nullptr)
                  posAcc = attr.data;
               else if (attr.type == cgltf_attribute_type_normal && normAcc == nullptr)
                  normAcc = attr.data;
               else if (attr.type == cgltf_attribute_type_texcoord)
               {
                  uvSets++;
                  if (attr.index == 0)
                     uvAcc = attr.data;
               }
            }
            uvSetsSeen = std::max(uvSetsSeen, uvSets);
            if (posAcc == nullptr)
               continue;

            const size_t base = pkg.vertices.size();
            const size_t count = posAcc->count;
            r.firstVertex = base;
            r.vertexCount = count;
            pkg.vertices.resize(base + count);
            Platform::ModelVertex* out = &pkg.vertices[base];
            std::vector<float> buf(count * 3);
            cgltf_accessor_unpack_floats(posAcc, buf.data(), buf.size());
            const float* m = r.world;
            for (size_t i = 0; i < count; i++)
            {
               const float x = buf[i * 3], y = buf[i * 3 + 1], z = buf[i * 3 + 2];
               out[i].px = m[0] * x + m[4] * y + m[8] * z + m[12];
               out[i].py = m[1] * x + m[5] * y + m[9] * z + m[13];
               out[i].pz = m[2] * x + m[6] * y + m[10] * z + m[14];
            }
            // Normals turn by the cofactor matrix (the inverse transpose, scaled).
            const float c[9] = { m[5] * m[10] - m[6] * m[9], m[6] * m[8] - m[4] * m[10], m[4] * m[9] - m[5] * m[8],
                                 m[2] * m[9] - m[1] * m[10], m[0] * m[10] - m[2] * m[8], m[1] * m[8] - m[0] * m[9],
                                 m[1] * m[6] - m[2] * m[5], m[2] * m[4] - m[0] * m[6], m[0] * m[5] - m[1] * m[4] };
            const float det = m[0] * c[0] + m[4] * c[1] + m[8] * c[2];
            if (normAcc != nullptr && normAcc->count == count)
            {
               cgltf_accessor_unpack_floats(normAcc, buf.data(), buf.size());
               for (size_t i = 0; i < count; i++)
               {
                  const float x = buf[i * 3], y = buf[i * 3 + 1], z = buf[i * 3 + 2];
                  float n[3] = { c[0] * x + c[3] * y + c[6] * z, c[1] * x + c[4] * y + c[7] * z, c[2] * x + c[5] * y + c[8] * z };
                  const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                  const float k = len > 1e-12f ? (det < 0.0f ? -1.0f : 1.0f) / len : 0.0f;
                  out[i].nx = n[0] * k; out[i].ny = n[1] * k; out[i].nz = n[2] * k;
               }
            }
            else
               anyMissingNormals = true;
            if (uvAcc != nullptr && uvAcc->count == count)
            {
               std::vector<float> uv(count * 2);
               cgltf_accessor_unpack_floats(uvAcc, uv.data(), uv.size());
               for (size_t i = 0; i < count; i++)
               {
                  // glTF puts v=0 at the image's top; our images load bottom-up.
                  out[i].u = uv[i * 2 + 0];
                  out[i].v = 1.0f - uv[i * 2 + 1];
               }
            }

            const size_t firstIndex = pkg.indices.size();
            if (prim.indices != nullptr)
            {
               pkg.indices.resize(firstIndex + prim.indices->count);
               for (size_t i = 0; i < prim.indices->count; i++)
                  pkg.indices[firstIndex + i] = (unsigned int)(base + cgltf_accessor_read_index(prim.indices, i));
            }
            else
            {
               pkg.indices.resize(firstIndex + count);
               for (size_t i = 0; i < count; i++)
                  pkg.indices[firstIndex + i] = (unsigned int)(base + i);
            }
            if (det < 0.0f)   // a mirrored node turns the winding inside out
               for (size_t i = firstIndex; i + 2 < pkg.indices.size(); i += 3)
                  std::swap(pkg.indices[i + 1], pkg.indices[i + 2]);
         }
         refs.erase(std::remove_if(refs.begin(), refs.end(), [](const PrimRef& r) { return r.vertexCount == 0; }), refs.end());
         if (refs.empty())
            continue;
         if (anyMissingNormals)
            GenerateNormals(pkg.vertices, pkg.indices);

         ReadRig(data, refs, pkg);
         pkg.hadMultipleUVSets = uvSetsSeen > 1;

         if (mats[g] == nullptr)
         {
            pkg.materialName = "Material " + std::to_string(g + 1);
            continue;
         }
         const cgltf_material& mat = *mats[g];
         pkg.materialName = mat.name != nullptr && mat.name[0] != '\0' ? mat.name : "Material " + std::to_string(g + 1);
         std::copy(mat.emissive_factor, mat.emissive_factor + 3, pkg.emissiveFactor);
         std::string err;
         if (mat.has_pbr_metallic_roughness)
         {
            const cgltf_pbr_metallic_roughness& pbr = mat.pbr_metallic_roughness;
            std::copy(pbr.base_color_factor, pbr.base_color_factor + 4, pkg.baseColor);
            pkg.metallicFactor = pbr.metallic_factor;
            pkg.roughnessFactor = pbr.roughness_factor;
            if (pbr.base_color_texture.texture != nullptr)
               DecodeCgltfImage(pbr.base_color_texture.texture->image, dir, "albedo", pkg.albedo, err);
            if (pbr.metallic_roughness_texture.texture != nullptr)
            {
               GltfDecodedImage combined;
               if (DecodeCgltfImage(pbr.metallic_roughness_texture.texture->image, dir, "metallicRoughness", combined, err))
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
      built.erase(std::remove_if(built.begin(), built.end(), [](const GltfDecodePackage& p) { return p.vertices.empty(); }), built.end());
      if (built.empty())
      {
         cgltf_free(data);
         outError = "glTF primitive has no POSITION attribute";
         return nullptr;
      }
      GltfDecodePackage pkg = std::move(built[0]);
      for (size_t g = 1; g < built.size(); g++)
         pkg.parts.push_back(std::move(built[g]));

      cgltf_free(data);

      size_t bytes = 0;
      for (int n = 0; n < pkg.PartCount(); n++)
      {
         const GltfDecodePackage& part = *pkg.Part(n);
         bytes += part.vertices.size() * sizeof(Platform::ModelVertex) +
                  part.indices.size() * sizeof(unsigned int) +
                  part.skinJoints.size() * sizeof(unsigned short) + part.skinWeights.size() * sizeof(float) +
                  part.joints.size() * sizeof(GltfJoint);
         for (const auto& d : part.morphDeltas)
            bytes += d.size() * sizeof(float);
         for (const auto& c : part.clips)
            for (const auto& ch : c.channels)
               bytes += (ch.times.size() + ch.values.size()) * sizeof(float);
         for (const GltfDecodedImage* img : { &part.albedo, &part.roughness, &part.metallic, &part.normalMap,
                                              &part.occlusion, &part.emissive })
            bytes += img->pixels.size();
      }

      cache.Put(path, pkg, bytes);
      return cache.Get(path);
   }
}
