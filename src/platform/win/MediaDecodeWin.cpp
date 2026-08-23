// Windows implementation of the Platform facade's decoding surface:
//
//   - LoadImageRGBA: stb_image (png/jpeg/bmp/gif/tga/webp/pic/pnm), flipped
//     for GL like the macOS ImageIO path was. TIFF/HEIC are not inbox formats
//     here and report a clear error rather than silently failing.
//   - LoadImageFloatRGB: tinyexr for .exr (the format the macOS path existed
//     for), stb_image's float path for everything else including Radiance
//     .hdr. Linear values above 1.0 survive - no sRGB clamp.
//   - LoadModel: hand-rolled OBJ / PLY / STL importers replacing ModelIO,
//     producing the same interleaved vertex layout (Platform::ModelVertex,
//     matching core/Mesh.h's Vertex) with generated normals when the file has
//     none.
//   - DecodeAudioFileToBuffer: dr_wav / dr_mp3 / dr_flac plus a small AIFF
//     parser standing in for AVFoundation's wav/aiff/mp3/flac coverage. The
//     AVFoundation-only containers (m4a/alac/caf) report an explicit error.

#include "../Platform.h"

#include "WinCommon.h"

// Declarations only: STB_IMAGE_IMPLEMENTATION already lives in
// EnvironmentNode.cpp (it owns the HDRI loader there too), so defining it
// again here would duplicate every stbi_* symbol at link time.
#include "stb_image.h"

// EXR's ZIP blocks decompress via miniz (vendored alongside tinyexr and
// compiled from external/miniz.c) - keeps us off zlib/nanozlib downloads.
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace
{
   constexpr int kMaxDecodeChannels = 2; // SampleBuffer is stereo by contract

   // Split an interleaved decode buffer into SampleBuffer's planar layout
   // (channel 0's frames, then channel 1's).
   void Deinterleave(const std::vector<float>& interleaved, uint64_t frames, int channels,
                     std::vector<float>& outChannelData)
   {
      outChannelData.assign((size_t)frames * channels, 0.0f);
      for (int c = 0; c < channels; c++)
      {
         float* dst = outChannelData.data() + (size_t)c * frames;
         for (uint64_t i = 0; i < frames; i++)
            dst[i] = interleaved[(size_t)i * channels + c];
      }
   }

   std::string LowerExtension(const std::string& path)
   {
      const size_t dot = path.find_last_of('.');
      if (dot == std::string::npos || dot == path.size() - 1)
         return {};
      std::string ext = path.substr(dot + 1);
      for (char& c : ext)
         c = (char)tolower((unsigned char)c);
      return ext;
   }

// Path conventions:
//   - dr_libs' plain `*_init_file` APIs go through raw fopen, which mangles
//     non-ASCII paths on Windows; use their explicit `_w` variants.
//   - stb_image converts its UTF-8 char* paths to _wfopen internally when
//     built with MSVC, so pass paths.c_str() straight through.
//   - tinyexr only offers fopen-based loading; hand it the file bytes and use
//     the from-memory entry point instead.
#define WPATH(path) WinCommon::Utf8ToWide(path).c_str()

   // ---- shared geometry helpers ---------------------------------------------

   void GenerateNormals(std::vector<Platform::ModelVertex>& vertices,
                        const std::vector<unsigned int>& indices)
   {
      for (size_t i = 0; i + 2 < indices.size(); i += 3)
      {
         Platform::ModelVertex& a = vertices[indices[i]];
         Platform::ModelVertex& b = vertices[indices[i + 1]];
         Platform::ModelVertex& c = vertices[indices[i + 2]];
         const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
         const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
         float nx = uy * vz - uz * vy;
         float ny = uz * vx - ux * vz;
         float nz = ux * vy - uy * vx;
         const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
         if (len > 1e-12f)
         {
            nx /= len; ny /= len; nz /= len;
         }
         a.nx += nx; a.ny += ny; a.nz += nz;
         b.nx += nx; b.ny += ny; b.nz += nz;
         c.nx += nx; c.ny += ny; c.nz += nz;
      }
      for (Platform::ModelVertex& vtx : vertices)
      {
         const float len = std::sqrt(vtx.nx * vtx.nx + vtx.ny * vtx.ny + vtx.nz * vtx.nz);
         if (len > 1e-12f)
         {
            vtx.nx /= len; vtx.ny /= len; vtx.nz /= len;
         }
         else
         {
            vtx.nx = 0.0f; vtx.ny = 0.0f; vtx.nz = 1.0f;
         }
      }
   }

   bool FinishModel(std::vector<Platform::ModelVertex>& vertices,
                    std::vector<unsigned int>& indices, bool fileHadNormals,
                    std::vector<Platform::ModelVertex>& outVertices,
                    std::vector<unsigned int>& outIndices, std::string& outError)
   {
      if (!fileHadNormals && !vertices.empty())
         GenerateNormals(vertices, indices);
      if (vertices.empty() || indices.empty())
      {
         outError = "no geometry";
         return false;
      }
      outVertices = std::move(vertices);
      outIndices = std::move(indices);
      return true;
   }

   // ---- OBJ ---------------------------------------------------------------

   // Splits "v/vt/vn" into three index strings; any component may be empty.
   // Handles all OBJ forms: v, v/vt, v//vn, v/vt/vn.
   void SplitFaceToken(const char* tok, char outV[32], char outVt[32], char outVn[32])
   {
      outV[0] = outVt[0] = outVn[0] = '\0';
      if (tok == nullptr)
         return;
      const char* slash1 = strchr(tok, '/');
      if (slash1 == nullptr)
      {
         snprintf(outV, 32, "%s", tok);
         return;
      }
      const size_t vLen = (size_t)(slash1 - tok);
      memcpy(outV, tok, std::min(vLen, (size_t)31));
      outV[std::min(vLen, (size_t)31)] = '\0';

      const char* slash2 = strchr(slash1 + 1, '/');
      if (slash2 == nullptr)
      {
         snprintf(outVt, 32, "%s", slash1 + 1);
         return;
      }
      const size_t vtLen = (size_t)(slash2 - slash1 - 1);
      if (vtLen > 0)
      {
         memcpy(outVt, slash1 + 1, std::min(vtLen, (size_t)31));
         outVt[std::min(vtLen, (size_t)31)] = '\0';
      }
      snprintf(outVn, 32, "%s", slash2 + 1);
   }

   int ParseObjIndex(const char* token, size_t count)
   {
      if (token == nullptr || token[0] == '\0')
         return -1;
      // OBJ is 1-based; negatives are relative to the current end.
      const long idx = strtol(token, nullptr, 10);
      if (idx == 0)
         return -1;
      if (idx > 0)
         return (int)std::min<long>(idx - 1, (long)count - 1);
      return (int)std::max<long>((long)count + idx, 0);
   }

   bool LoadModelObj(const std::string& path, std::vector<Platform::ModelVertex>& outVertices,
                     std::vector<unsigned int>& outIndices, std::string& outError)
   {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
         outError = "cannot open file";
         return false;
      }

      std::vector<float> positions, uvs, normals;
      std::vector<Platform::ModelVertex> vertices;
      std::vector<unsigned int> indices;
      bool hasNormals = false;

      // Dedup map: combined "v/vt/vn" triple -> output vertex index. Keeps
      // shared-corner models from exploding into unindexed soup.
      std::map<std::pair<int64_t, int64_t>, unsigned> dedup;

      std::string line;
      while (std::getline(file, line))
      {
         if (line.size() < 2)
            continue;
         if (line.compare(0, 2, "v ") == 0)
         {
            float x, y, z;
            if (sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z) == 3)
            {
               positions.push_back(x);
               positions.push_back(y);
               positions.push_back(z);
            }
         }
         else if (line.compare(0, 3, "vt ") == 0)
         {
            float u, v;
            if (sscanf(line.c_str() + 3, "%f %f", &u, &v) == 2)
            {
               uvs.push_back(u);
               uvs.push_back(v);
            }
         }
         else if (line.compare(0, 3, "vn ") == 0)
         {
            float x, y, z;
            if (sscanf(line.c_str() + 3, "%f %f %f", &x, &y, &z) == 3)
            {
               normals.push_back(x);
               normals.push_back(y);
               normals.push_back(z);
               hasNormals = true;
            }
         }
         else if (line.compare(0, 2, "f ") == 0)
         {
            // Collect polygon corner slots first, then fan-triangulate.
            unsigned corners[64];
            int cornerCount = 0;

            std::vector<char> buf(line.begin(), line.end());
            buf.push_back('\0');
            char* saveptr = nullptr;
            strtok_s(buf.data(), " \t\r\n", &saveptr); // skip the "f"
            for (char* tok = strtok_s(nullptr, " \t\r\n", &saveptr);
                 tok != nullptr && cornerCount < 64;
                 tok = strtok_s(nullptr, " \t\r\n", &saveptr))
            {
               char sv[32], svt[32], svn[32];
               SplitFaceToken(tok, sv, svt, svn);
               const int vi = ParseObjIndex(sv, positions.size() / 3);
               if (vi < 0)
                  continue;
               const int ti = ParseObjIndex(svt, uvs.size() / 2);
               const int ni = ParseObjIndex(svn, normals.size() / 3);

               const auto key = std::make_pair((int64_t)vi << 20 | (int64_t)(ti + 1),
                                               (int64_t)ni + 1);
               auto found = dedup.find(key);
               unsigned slot;
               if (found == dedup.end())
               {
                  Platform::ModelVertex vtx;
                  vtx.px = positions[vi * 3];
                  vtx.py = positions[vi * 3 + 1];
                  vtx.pz = positions[vi * 3 + 2];
                  if (ti >= 0)
                  {
                     vtx.u = uvs[ti * 2];
                     vtx.v = uvs[ti * 2 + 1];
                  }
                  if (ni >= 0)
                  {
                     vtx.nx = normals[ni * 3];
                     vtx.ny = normals[ni * 3 + 1];
                     vtx.nz = normals[ni * 3 + 2];
                  }
                  slot = (unsigned)vertices.size();
                  dedup[key] = slot;
                  vertices.push_back(vtx);
               }
               else
               {
                  slot = found->second;
               }
               corners[cornerCount++] = slot;
            }

            for (int i = 2; i < cornerCount; i++)
            {
               indices.push_back(corners[0]);
               indices.push_back(corners[i - 1]);
               indices.push_back(corners[i]);
            }
         }
      }

      return FinishModel(vertices, indices, hasNormals, outVertices, outIndices, outError);
   }

   // ---- STL ---------------------------------------------------------------

   bool LoadModelStl(const std::string& path, std::vector<Platform::ModelVertex>& outVertices,
                     std::vector<unsigned int>& outIndices, std::string& outError)
   {
      std::ifstream file(path, std::ios::binary | std::ios::ate);
      if (!file)
      {
         outError = "cannot open file";
         return false;
      }
      const std::streamoff size = file.tellg();
      file.seekg(0);

      uint32_t triCount = 0;
      bool binary = false;
      if (size >= 84)
      {
         file.seekg(80);
         file.read(reinterpret_cast<char*>(&triCount), 4);
         // Binary STL: 84-byte header/count + exactly 50 bytes per triangle
         // (some writers leave one trailing byte).
         if ((std::streamoff)(84 + (long long)triCount * 50) <= size &&
             (std::streamoff)(84 + (long long)triCount * 50 + 4096) > size)
            binary = true;
      }
      file.seekg(0);

      std::vector<Platform::ModelVertex> vertices;
      std::vector<unsigned int> indices;
      vertices.reserve(binary ? (size_t)triCount * 3 : 1024);

      if (binary)
      {
         file.seekg(84);
         std::vector<char> rec(50);
         for (uint32_t t = 0; t < triCount && file.read(rec.data(), 50); t++)
         {
            const float* floats = reinterpret_cast<const float*>(rec.data() + 12);
            for (int i = 0; i < 3; i++)
            {
               Platform::ModelVertex v;
               v.px = floats[i * 3 + 0];
               v.py = floats[i * 3 + 1];
               v.pz = floats[i * 3 + 2];
               indices.push_back((unsigned)vertices.size());
               vertices.push_back(v);
            }
         }
      }
      else
      {
         // ASCII: every third "vertex x y z" line completes one triangle.
         std::string line;
         while (std::getline(file, line))
         {
            if (line.compare(0, 6, "vertex") == 0)
            {
               Platform::ModelVertex v;
               if (sscanf(line.c_str() + 6, "%f %f %f", &v.px, &v.py, &v.pz) == 3)
               {
                  indices.push_back((unsigned)vertices.size());
                  vertices.push_back(v);
               }
            }
         }
      }

      return FinishModel(vertices, indices, false, outVertices, outIndices, outError);
   }

   // ---- PLY ---------------------------------------------------------------

   struct PlyProp
   {
      std::string name;
      size_t width = 4;    // bytes in binary form
      bool isFloat = true; // false => signed integer, read directly as float
   };

   bool PlyPropWidth(const char* type, size_t& outWidth, bool& outIsFloat)
   {
      struct { const char* name; size_t w; bool f; } kTypes[] = {
         { "char", 1, false }, { "int8", 1, false },
         { "uchar", 1, false }, { "uint8", 1, false },
         { "short", 2, false }, { "int16", 2, false },
         { "ushort", 2, false }, { "uint16", 2, false },
         { "int", 4, false }, { "int32", 4, false },
         { "uint", 4, false }, { "uint32", 4, false },
         { "float", 4, true }, { "float32", 4, true },
         { "double", 8, true }, { "float64", 8, true },
      };
      for (const auto& t : kTypes)
      {
         if (strcmp(type, t.name) == 0)
         {
            outWidth = t.w;
            outIsFloat = t.f;
            return true;
         }
      }
      return false;
   }

   template <typename AtFn>
   float ReadPlyProp(const std::vector<PlyProp>& props, int index, AtFn at)
   {
      if (index < 0 || index >= (int)props.size())
         return 0.0f;
      size_t off = 0;
      for (int q = 0; q < index; q++)
         off += props[q].width;
      const unsigned char* p = at(off);
      if (p == nullptr)
         return 0.0f;
      const PlyProp& prop = props[index];
      if (prop.isFloat)
      {
         if (prop.width == 4)
            return *(const float*)p;
         if (prop.width == 8)
            return (float)*(const double*)p;
         return 0.0f;
      }
      switch (prop.width)
      {
         case 1: return (float)*(const int8_t*)p;
         case 2: return (float)*(const int16_t*)p;
         case 4: return (float)*(const int32_t*)p;
         default: return 0.0f;
      }
   }

   bool LoadModelPly(const std::string& path, std::vector<Platform::ModelVertex>& outVertices,
                     std::vector<unsigned int>& outIndices, std::string& outError)
   {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
         outError = "cannot open file";
         return false;
      }

      std::string line;
      std::getline(file, line);
      if (line.compare(0, 3, "ply") != 0)
      {
         outError = "not a PLY file";
         return false;
      }

      long long vertexCount = 0, faceCount = 0;
      bool ascii = true;
      bool inVertexElement = false;
      std::vector<PlyProp> props;

      while (std::getline(file, line))
      {
         // Header lines are plain ASCII regardless of body format.
         if (line.compare(0, 7, "format ") == 0)
            ascii = line.find("ascii") != std::string::npos;
         else if (line.compare(0, 8, "element ") == 0)
         {
            char kind[64] = "";
            long long count = 0;
            if (sscanf(line.c_str(), "element %63s %lld", kind, &count) == 2)
            {
               inVertexElement = strcmp(kind, "vertex") == 0;
               if (inVertexElement)
               {
                  vertexCount = count;
                  props.clear();
               }
               else if (strcmp(kind, "face") == 0)
                  faceCount = count;
            }
         }
         else if (line.compare(0, 9, "property ") == 0 && inVertexElement &&
                  props.size() < 32)
         {
            char type[32] = "", name[64] = "";
            if (sscanf(line.c_str(), "property %31s %63s", type, name) == 2)
            {
               PlyProp prop;
               prop.name = name;
               if (!PlyPropWidth(type, prop.width, prop.isFloat))
                  break; // list property inside vertex element: unsupported
               props.push_back(prop);
            }
         }
         else if (line.compare(0, 10, "end_header") == 0)
            break;
      }

      auto propIndex = [&](const char* name) -> int {
         for (size_t i = 0; i < props.size(); i++)
            if (props[i].name == name)
               return (int)i;
         return -1;
      };
      const int ix = propIndex("x"), iy = propIndex("y"), iz = propIndex("z");
      const int inx = propIndex("nx"), iny = propIndex("ny"), inz = propIndex("nz");
      const int iu = propIndex("s") >= 0 ? propIndex("s") : propIndex("u");
      const int iv = propIndex("t") >= 0 ? propIndex("t") : propIndex("v");
      if (ix < 0 || iy < 0 || iz < 0)
      {
         outError = "PLY has no x/y/z properties";
         return false;
      }

      std::vector<Platform::ModelVertex> vertices;
      std::vector<unsigned int> indices;
      vertices.reserve((size_t)std::max(0LL, vertexCount));

      if (ascii)
      {
         for (long long v = 0; v < vertexCount && std::getline(file, line); )
         {
            std::vector<float> vals(props.size(), 0.0f);
            std::vector<char> buf(line.begin(), line.end());
            buf.push_back('\0');
            char* saveptr = nullptr;
            int nvals = 0;
            for (char* tok = strtok_s(buf.data(), " \t\r\n", &saveptr);
                 tok != nullptr && nvals < (int)props.size();
                 tok = strtok_s(nullptr, " \t\r\n", &saveptr))
               vals[nvals++] = (float)strtod(tok, nullptr);
            if (nvals < (int)props.size())
               continue; // malformed row; skip rather than misread the stream
            v++;

            Platform::ModelVertex mv;
            mv.px = vals[ix]; mv.py = vals[iy]; mv.pz = vals[iz];
            if (inx >= 0) { mv.nx = vals[inx]; mv.ny = vals[iny]; mv.nz = vals[inz]; }
            if (iu >= 0) mv.u = vals[iu];
            if (iv >= 0) mv.v = vals[iv];
            vertices.push_back(mv);
         }

         for (long long f = 0; f < faceCount && std::getline(file, line); f++)
         {
            std::vector<char> buf(line.begin(), line.end());
            buf.push_back('\0');
            char* saveptr = nullptr;
            char* countTok = strtok_s(buf.data(), " \t\r\n", &saveptr);
            if (countTok == nullptr)
               continue;
            const int cornerCount = atoi(countTok);
            unsigned first = 0, prev = 0;
            for (int c = 0; c < cornerCount; c++)
            {
               char* tok = strtok_s(nullptr, " \t\r\n", &saveptr);
               if (tok == nullptr)
                  break;
               const unsigned cur = (unsigned)strtoul(tok, nullptr, 10);
               if (c == 0)
                  first = cur;
               else if (c >= 2)
               {
                  indices.push_back(first);
                  indices.push_back(prev);
                  indices.push_back(cur);
               }
               prev = cur;
            }
         }
      }
      else
      {
         // Little-endian binary body. Vertex rows are fixed-width per the
         // declared property types; faces are uchar-count + int32 indices
         // (the only form exporters emit).
         size_t rowBytes = 0;
         for (const PlyProp& p : props)
            rowBytes += p.width;
         std::vector<unsigned char> row(std::max(rowBytes, (size_t)1), 0);
         for (long long v = 0; v < vertexCount; v++)
         {
            if (!file.read(reinterpret_cast<char*>(row.data()), (std::streamsize)rowBytes))
                break;
            auto at = [&](size_t off) -> const unsigned char* {
               return off < rowBytes ? row.data() + off : nullptr;
            };
            auto val = [&](int index) -> float {
               return ReadPlyProp(props, index, at);
            };

            Platform::ModelVertex mv;
            mv.px = val(ix); mv.py = val(iy); mv.pz = val(iz);
            if (inx >= 0) { mv.nx = val(inx); mv.ny = val(iny); mv.nz = val(inz); }
            if (iu >= 0) mv.u = val(iu);
            if (iv >= 0) mv.v = val(iv);
            vertices.push_back(mv);
         }

         for (long long f = 0; f < faceCount; f++)
         {
            unsigned char n = 0;
            if (!file.read(reinterpret_cast<char*>(&n), 1))
               break;
            unsigned first = 0, prev = 0;
            for (int c = 0; c < n; c++)
            {
               int32_t idx = 0;
               if (!file.read(reinterpret_cast<char*>(&idx), 4))
                  break;
               const unsigned cur = (unsigned)idx;
               if (c == 0)
                  first = cur;
               else if (c >= 2)
               {
                  indices.push_back(first);
                  indices.push_back(prev);
                  indices.push_back(cur);
               }
               prev = cur;
            }
         }
      }

      return FinishModel(vertices, indices, inx >= 0, outVertices, outIndices, outError);
   }

   // ---- AIFF ---------------------------------------------------------------

   double ReadIeeeExtended80(const unsigned char* p)
   {
      // Standard 80-bit Motorola extended float used in AIFF COMM chunks.
      const int exponent = ((p[0] & 0x7F) << 8) | p[1];
      double mantissa = 0.0;
      for (int i = 2; i < 10; i++)
         mantissa = mantissa * 256.0 + (double)p[i];
      mantissa /= 4503599627370496.0; // 2^52: shift past the integer bit
      const int sign = (p[0] & 0x80) ? -1 : 1;
      if (mantissa == 0.0 && exponent == 0)
         return 0.0;
      return sign * std::ldexp(mantissa, exponent - 16383);
   }

   uint32_t ReadU32BE(const unsigned char* p)
   {
      return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
   }

   uint16_t ReadU16BE(const unsigned char* p)
   {
      return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
   }

   bool DecodeAiff(const std::string& path, Platform::SampleBuffer& out, std::string& outError)
   {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
         outError = "cannot open file";
         return false;
      }

      unsigned char header[12];
      if (!file.read(reinterpret_cast<char*>(header), 12) ||
          memcmp(header, "FORM", 4) != 0 ||
          (memcmp(header + 8, "AIFF", 4) != 0 && memcmp(header + 8, "AIFC", 4) != 0))
      {
         outError = "not an AIFF file";
         return false;
      }

      int channels = 0, bits = 0;
      uint32_t frames = 0;
      double rate = 0.0;
      bool haveComm = false;
      bool sampleFormatFloat = false; // AIFC 'FL32'
      std::streamoff ssndDataPos = -1;

      // Walk IFF chunks until we've seen COMM and can jump to SSND's data.
      for (;;)
      {
         unsigned char chunkHeader[8];
         if (!file.read(reinterpret_cast<char*>(chunkHeader), 8))
            break;
         const uint32_t chunkSize = ReadU32BE(chunkHeader + 4);
         const std::streamoff here = (std::streamoff)file.tellg();
         const std::streamoff nextPos = here + (std::streamoff)((chunkSize + 1) & ~1);

         if (memcmp(chunkHeader, "COMM", 4) == 0 && chunkSize >= 18)
         {
            unsigned char comm[24] = {};
            file.read(reinterpret_cast<char*>(comm),
                      (std::streamsize)std::min<uint32_t>(chunkSize, 24));
            channels = ReadU16BE(comm);
            frames = ReadU32BE(comm + 2);
            bits = ReadU16BE(comm + 6);
            rate = ReadIeeeExtended80(comm + 8);
            haveComm = true;
            if (chunkSize >= 22 &&
                (memcmp(comm + 18, "FL32", 4) == 0 || memcmp(comm + 18, "fl32", 4) == 0))
               sampleFormatFloat = true;
         }
         else if (memcmp(chunkHeader, "SSND", 4) == 0 && chunkSize >= 8)
         {
            unsigned char sndInfo[8];
            file.read(reinterpret_cast<char*>(sndInfo), 8);
            const uint32_t dataOffset = ReadU32BE(sndInfo);
            ssndDataPos = here + 8 + (std::streamoff)dataOffset;
         }

         if (nextPos <= here)
            break; // corrupt size; don't loop forever
         file.seekg(nextPos);
      }

      if (!haveComm || ssndDataPos < 0)
      {
         outError = "AIFF missing COMM/SSND chunks";
         return false;
      }
      if (channels <= 0 || channels > kMaxDecodeChannels || frames == 0 || rate <= 0.0)
      {
         outError = "unsupported AIFF channel count or length";
         return false;
      }
      if (!sampleFormatFloat && bits != 8 && bits != 16 && bits != 24 && bits != 32)
      {
         outError = "unsupported AIFF bit depth";
         return false;
      }

      file.clear();
      file.seekg(ssndDataPos);

      const size_t totalSamples = (size_t)frames * channels;
      std::vector<float> interleaved(totalSamples, 0.0f);

      const int bytesPerSample = sampleFormatFloat ? 4 : (bits + 7) / 8;
      const size_t frameBytes = (size_t)channels * bytesPerSample;
      std::vector<unsigned char> buf(totalSamples * bytesPerSample);
      file.read(reinterpret_cast<char*>(buf.data()),
                (std::streamsize)(totalSamples * bytesPerSample));
      const size_t gotFrames = (size_t)((uint64_t)file.gcount()) / frameBytes;

      for (size_t f = 0; f < gotFrames; f++)
         for (int c = 0; c < channels; c++)
         {
            const unsigned char* s = buf.data() + f * frameBytes + (size_t)c * bytesPerSample;
            float value = 0.0f;
            if (sampleFormatFloat)
            {
               const uint32_t asInt = ReadU32BE(s);
               memcpy(&value, &asInt, 4); // big-endian bit pattern reinterpreted
            }
            else if (bytesPerSample == 1)
               value = (float)s[0] / 128.0f - 1.0f; // 8-bit AIFF is unsigned
            else if (bytesPerSample == 2)
               value = (float)(int16_t)ReadU16BE(s) / 32768.0f;
            else if (bytesPerSample == 3)
            {
               const int32_t v = ((int32_t)s[0] << 24) | ((int32_t)s[1] << 16) |
                                 ((int32_t)s[2] << 8);
               value = (float)v / 2147483648.0f;
            }
            else
            {
               value = (float)(int32_t)ReadU32BE(s) / 2147483648.0f;
            }
            interleaved[f * channels + c] = value;
         }

      out.channels = channels;
      out.numFrames = (int)gotFrames;
      out.sampleRate = rate;
      Deinterleave(interleaved, gotFrames, channels, out.channelData);
      return true;
   }

   // Whole-file read through wide-char streams so UTF-8 paths survive
   // conversion to the system codepage. Image decoding goes through stb's
   // *_from_memory entry points because the one STB_IMAGE_IMPLEMENTATION in
   // this target (EnvironmentNode.cpp) is compiled with STBI_NO_STDIO - there
   // are no FILE*-based loaders to link against.
   bool ReadFileBytes(const std::string& path, std::vector<unsigned char>& outBytes,
                      std::string& outError)
   {
      std::ifstream file(WinCommon::Utf8ToWide(path), std::ios::binary | std::ios::ate);
      if (!file)
      {
         outError = "cannot open file";
         return false;
      }
      const std::streamoff size64 = file.tellg();
      if (size64 <= 0)
      {
         outError = size64 == 0 ? "empty file" : "cannot stat file";
         return false;
      }
      outBytes.resize((size_t)size64);
      file.seekg(0);
      if (!file.read(reinterpret_cast<char*>(outBytes.data()), (std::streamsize)size64))
      {
         outError = "cannot read file";
         return false;
      }
      return true;
   }
}

namespace Platform
{
   bool LoadImageRGBA(const std::string& path, std::vector<unsigned char>& outPixels,
                      int& outWidth, int& outHeight, std::string& outError)
   {
      outError.clear();
      const std::string ext = LowerExtension(path);
      if (ext == "tif" || ext == "tiff")
      {
         outError = "TIFF images are not supported on this platform; convert to PNG/JPEG";
         return false;
      }
      if (ext == "heic" || ext == "heif")
      {
         outError = "HEIC images are not supported on this platform; convert to PNG/JPEG";
         return false;
      }

      stbi_set_flip_vertically_on_load(1);
      std::vector<unsigned char> bytes;
      if (!ReadFileBytes(path, bytes, outError))
         return false;
      int w = 0, h = 0, comp = 0;
      stbi_uc* data = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
      if (data == nullptr)
      {
         outError = stbi_failure_reason() ? stbi_failure_reason() : "image decode failed";
         return false;
      }

      outWidth = w;
      outHeight = h;
      outPixels.assign(data, data + (size_t)w * h * 4);
      stbi_image_free(data);
      return true;
   }

   bool LoadImageFloatRGB(const std::string& path, std::vector<float>& outPixels,
                          int& outWidth, int& outHeight, std::string& outError)
   {
      outError.clear();
      const std::string ext = LowerExtension(path);

      if (ext == "exr")
      {
         std::vector<unsigned char> bytes;
         if (!ReadFileBytes(path, bytes, outError))
            return false;

         const char* err = nullptr;
         float* rgba = nullptr; // tinyexr always returns RGBA
         int w = 0, h = 0;
         const int ret =
            LoadEXRFromMemory(&rgba, &w, &h, bytes.data(), bytes.size(), &err);
         if (ret != TINYEXR_SUCCESS || rgba == nullptr)
         {
            outError = err ? err : "EXR decode failed";
            if (rgba != nullptr)
               free(rgba);
            return false;
         }

         outWidth = w;
         outHeight = h;
         outPixels.resize((size_t)w * h * 3);
         for (size_t px = 0; px < (size_t)w * h; px++)
         {
            outPixels[px * 3 + 0] = rgba[px * 4 + 0];
            outPixels[px * 3 + 1] = rgba[px * 4 + 1];
            outPixels[px * 3 + 2] = rgba[px * 4 + 2];
         }
         free(rgba);
         return true;
      }

      // Everything else through stb_image's float loader (Radiance .hdr,
      // half-float PNG...). Row-flipped for GL like the RGBA path.
      stbi_set_flip_vertically_on_load(1);
      std::vector<unsigned char> bytes;
      if (!ReadFileBytes(path, bytes, outError))
         return false;
      int w = 0, h = 0, comp = 0;
      float* data = stbi_loadf_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 3);
      if (data == nullptr)
      {
         outError = stbi_failure_reason() ? stbi_failure_reason() : "image decode failed";
         return false;
      }

      outWidth = w;
      outHeight = h;
      outPixels.assign(data, data + (size_t)w * h * 3);
      stbi_image_free(data);
      return true;
   }

   bool LoadModel(const std::string& path, std::vector<ModelVertex>& outVertices,
                  std::vector<unsigned int>& outIndices, std::string& outError)
   {
      outVertices.clear();
      outIndices.clear();
      outError.clear();

      const std::string ext = LowerExtension(path);
      if (ext == "obj")
         return LoadModelObj(path, outVertices, outIndices, outError);
      if (ext == "stl")
         return LoadModelStl(path, outVertices, outIndices, outError);
      if (ext == "ply")
         return LoadModelPly(path, outVertices, outIndices, outError);
      if (ext == "usd" || ext == "usdz" || ext == "usda" || ext == "usdc")
      {
         outError = "USD models are not supported on this platform; use OBJ/PLY/STL";
         return false;
      }
      outError = "unsupported model format";
      return false;
   }

   bool DecodeAudioFileToBuffer(const std::string& path, SampleBuffer& outBuffer,
                                std::string& outError)
   {
      outError.clear();
      outBuffer = SampleBuffer();

      const std::string ext = LowerExtension(path);

      auto finishBuffer = [&](const std::vector<float>& interleaved, uint64_t gotFrames,
                              int channels, int rate) -> bool {
         if (gotFrames == 0)
         {
            outError = "file contains no audio";
            return false;
         }
         outBuffer.channels = channels;
         outBuffer.numFrames = (int)gotFrames;
         outBuffer.sampleRate = (double)rate;
         Deinterleave(interleaved, gotFrames, channels, outBuffer.channelData);
         return true;
      };

      if (ext == "wav")
      {
         drwav wav;
         if (!drwav_init_file_w(&wav, WPATH(path), nullptr))
         {
            outError = "not a readable WAV file";
            return false;
         }

         const bool stereoOrMono = wav.channels <= (unsigned)kMaxDecodeChannels;
         const int channels = (int)std::min<unsigned>(wav.channels, (unsigned)kMaxDecodeChannels);
         const uint64_t total = wav.totalPCMFrameCount;
         std::vector<float> interleaved((size_t)total * channels, 0.0f);
         uint64_t got = 0;
         if (stereoOrMono)
            got = drwav_read_pcm_frames_f32(&wav, total, interleaved.data());
         const int rate = wav.sampleRate;
         drwav_uninit(&wav);
         if (!stereoOrMono)
         {
            outError = "WAV has more than two channels";
            return false;
         }
         return finishBuffer(interleaved, got, channels, rate);
      }

      if (ext == "mp3")
      {
         drmp3 mp3;
         if (!drmp3_init_file_w(&mp3, WPATH(path), nullptr))
         {
            outError = "not a readable MP3 file";
            return false;
         }

         const int channels =
            mp3.channels > 1 ? 2 : 1;
         std::vector<float> interleaved;
         // totalPCMFrameCount is a bitrate estimate that can overshoot or
         // undershoot; decode frame-wise so the buffer holds exactly the
         // samples in the file.
         float chunk[4096 * 2];
         uint64_t got = 0;
         for (;;)
         {
            const uint64_t decoded = drmp3_read_pcm_frames_f32(&mp3, 4096, chunk);
            if (decoded == 0)
               break;
            interleaved.insert(interleaved.end(), chunk, chunk + decoded * channels);
            got += decoded;
         }
         const int rate = mp3.sampleRate;
         drmp3_uninit(&mp3);
         return finishBuffer(interleaved, got, channels, rate);
      }

      if (ext == "flac")
      {
         drflac* flac = drflac_open_file_w(WPATH(path), nullptr);
         if (flac == nullptr)
         {
            outError = "not a readable FLAC file";
            return false;
         }

         const bool stereoOrMono = flac->channels <= (unsigned)kMaxDecodeChannels;
         const int channels = (int)std::min<unsigned>(flac->channels, (unsigned)kMaxDecodeChannels);
         const uint64_t total = flac->totalPCMFrameCount;
         std::vector<float> interleaved((size_t)total * channels, 0.0f);
         uint64_t got = 0;
         if (stereoOrMono)
            got = drflac_read_pcm_frames_f32(flac, total, interleaved.data());
         const int rate = flac->sampleRate;
         drflac_close(flac);
         if (!stereoOrMono)
         {
            outError = "FLAC has more than two channels";
            return false;
         }
         return finishBuffer(interleaved, got, channels, rate);
      }

      if (ext == "aiff" || ext == "aif")
         return DecodeAiff(path, outBuffer, outError);

      if (ext == "m4a" || ext == "m4b" || ext == "caf" || ext == "ogg")
      {
         outError = ext + " decoding requires AVFoundation and is not supported on this platform";
         return false;
      }

      outError = "unsupported audio format";
      return false;
   }
}
