#include "SplatIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

namespace
{
   using SplatIO::Splat;
   using SplatIO::SplatCloud;

   constexpr float kShC0 = 0.2820948f;

   float Sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

   // Sigma = R * diag(sx,sy,sz) * diag(sx,sy,sz)^T * R^T, upper triangle only.
   // R built from a normalized wxyz quaternion.
   void ComputeCovariance(float qw, float qx, float qy, float qz, float sx, float sy, float sz, float outCov[6])
   {
      const float r00 = 1.0f - 2.0f * (qy * qy + qz * qz);
      const float r01 = 2.0f * (qx * qy - qw * qz);
      const float r02 = 2.0f * (qx * qz + qw * qy);
      const float r10 = 2.0f * (qx * qy + qw * qz);
      const float r11 = 1.0f - 2.0f * (qx * qx + qz * qz);
      const float r12 = 2.0f * (qy * qz - qw * qx);
      const float r20 = 2.0f * (qx * qz - qw * qy);
      const float r21 = 2.0f * (qy * qz + qw * qx);
      const float r22 = 1.0f - 2.0f * (qx * qx + qy * qy);

      // M = R * diag(sx,sy,sz) -- scale each column of R.
      const float m00 = r00 * sx, m01 = r01 * sy, m02 = r02 * sz;
      const float m10 = r10 * sx, m11 = r11 * sy, m12 = r12 * sz;
      const float m20 = r20 * sx, m21 = r21 * sy, m22 = r22 * sz;

      // Sigma = M * M^T.
      outCov[0] = m00 * m00 + m01 * m01 + m02 * m02; // xx
      outCov[1] = m00 * m10 + m01 * m11 + m02 * m12; // xy
      outCov[2] = m00 * m20 + m01 * m21 + m02 * m22; // xz
      outCov[3] = m10 * m10 + m11 * m11 + m12 * m12; // yy
      outCov[4] = m10 * m20 + m11 * m21 + m12 * m22; // yz
      outCov[5] = m20 * m20 + m21 * m21 + m22 * m22; // zz
   }

   // `alpha` is the final, already-resolved 0..1 opacity (sigmoid(opacity)
   // for a real trained field, alpha/255 or a flat default for a plain point
   // cloud - see LoadSplatPly) - this function no longer applies sigmoid
   // itself, since that transform is specific to the SH/opacity-logit
   // encoding and wrong for a plain alpha byte.
   void FinishSplat(Splat& s, float alpha, float logScaleX, float logScaleY, float logScaleZ,
                     float rw, float rx, float ry, float rz, bool haveRotation)
   {
      s.a = alpha;
      s.sx = std::exp(logScaleX);
      s.sy = std::exp(logScaleY);
      s.sz = std::exp(logScaleZ);

      if (haveRotation)
      {
         const float len = std::sqrt(rw * rw + rx * rx + ry * ry + rz * rz);
         const float inv = (len > 1e-8f) ? (1.0f / len) : 1.0f;
         s.qw = rw * inv;
         s.qx = rx * inv;
         s.qy = ry * inv;
         s.qz = rz * inv;
      }
      else
      {
         s.qw = 1.0f;
         s.qx = s.qy = s.qz = 0.0f;
      }

      ComputeCovariance(s.qw, s.qx, s.qy, s.qz, s.sx, s.sy, s.sz, s.cov);
   }

   void UpdateBounds(SplatCloud& cloud)
   {
      if (cloud.Empty())
      {
         cloud.boundsMin[0] = cloud.boundsMin[1] = cloud.boundsMin[2] = 0.0f;
         cloud.boundsMax[0] = cloud.boundsMax[1] = cloud.boundsMax[2] = 0.0f;
         return;
      }

      float lo[3] = {cloud.splats[0].px, cloud.splats[0].py, cloud.splats[0].pz};
      float hi[3] = {cloud.splats[0].px, cloud.splats[0].py, cloud.splats[0].pz};
      for (const Splat& s : cloud.splats)
      {
         const float p[3] = {s.px, s.py, s.pz};
         for (int k = 0; k < 3; ++k)
         {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
         }
      }
      std::memcpy(cloud.boundsMin, lo, sizeof(lo));
      std::memcpy(cloud.boundsMax, hi, sizeof(hi));
   }

   // PLY property type sizes, binary_little_endian only.
   int PlyTypeSize(const std::string& type)
   {
      if (type == "float" || type == "float32" || type == "int" || type == "int32" ||
          type == "uint" || type == "uint32")
         return 4;
      if (type == "double" || type == "float64")
         return 8;
      if (type == "short" || type == "int16" || type == "ushort" || type == "uint16")
         return 2;
      if (type == "char" || type == "int8" || type == "uchar" || type == "uint8")
         return 1;
      return 0; // unknown - caller treats as a fatal header error
   }

   // Reads one property's value out of a raw vertex record as a float,
   // regardless of its on-disk type (3DGS ply properties are float32 in
   // every exporter seen so far, but this stays honest about the header).
   float ReadPlyFloat(const unsigned char* record, size_t offset, const std::string& type)
   {
      if (type == "float" || type == "float32")
      {
         float v;
         std::memcpy(&v, record + offset, 4);
         return v;
      }
      if (type == "double" || type == "float64")
      {
         double v;
         std::memcpy(&v, record + offset, 8);
         return static_cast<float>(v);
      }
      if (type == "uchar" || type == "uint8")
         return static_cast<float>(record[offset]);
      if (type == "char" || type == "int8")
         return static_cast<float>(static_cast<int8_t>(record[offset]));
      int32_t iv = 0;
      const int sz = PlyTypeSize(type);
      std::memcpy(&iv, record + offset, sz);
      return static_cast<float>(iv);
   }
}

namespace SplatIO
{
   bool LoadSplatPly(const std::string& path, SplatCloud& out, std::string& outError)
   {
      out = SplatCloud();

      FILE* f = std::fopen(path.c_str(), "rb");
      if (!f)
      {
         outError = "could not open file: " + path;
         return false;
      }

      // --- Header: read line by line up to and including "end_header\n". ---
      std::string magic;
      {
         char c;
         while (std::fread(&c, 1, 1, f) == 1 && c != '\n')
            magic += c;
      }
      if (magic != "ply" && magic != "ply\r")
      {
         outError = "not a PLY file (bad magic): " + path;
         std::fclose(f);
         return false;
      }

      struct Property
      {
         std::string type;
         std::string name;
         size_t offset = 0;
      };
      std::vector<Property> properties;
      long long vertexCount = -1;
      bool formatOk = false;
      bool inVertexElement = false;

      std::string line;
      auto readLine = [&](std::string& l) -> bool {
         l.clear();
         char c;
         while (true)
         {
            if (std::fread(&c, 1, 1, f) != 1)
               return !l.empty();
            if (c == '\n')
               return true;
            if (c != '\r')
               l += c;
         }
      };

      while (readLine(line))
      {
         std::istringstream iss(line);
         std::string tok;
         iss >> tok;

         if (tok == "format")
         {
            std::string fmt;
            iss >> fmt;
            formatOk = (fmt == "binary_little_endian");
         }
         else if (tok == "element")
         {
            std::string elemName;
            long long count = 0;
            iss >> elemName >> count;
            inVertexElement = (elemName == "vertex");
            if (inVertexElement)
               vertexCount = count;
         }
         else if (tok == "property")
         {
            if (inVertexElement)
            {
               Property p;
               iss >> p.type >> p.name;
               if (p.type == "list")
               {
                  outError = "PLY has a list property in the vertex element (unsupported): " + path;
                  std::fclose(f);
                  return false;
               }
               properties.push_back(p);
            }
         }
         else if (tok == "end_header")
         {
            break;
         }
      }

      if (!formatOk)
      {
         outError = "PLY is not binary_little_endian: " + path;
         std::fclose(f);
         return false;
      }
      if (vertexCount < 0)
      {
         outError = "PLY has no vertex element: " + path;
         std::fclose(f);
         return false;
      }

      size_t stride = 0;
      std::map<std::string, size_t> offsetOf;
      std::map<std::string, std::string> typeOf;
      for (Property& p : properties)
      {
         const int sz = PlyTypeSize(p.type);
         if (sz == 0)
         {
            outError = "PLY has an unrecognised property type '" + p.type + "': " + path;
            std::fclose(f);
            return false;
         }
         p.offset = stride;
         offsetOf[p.name] = p.offset;
         typeOf[p.name] = p.type;
         stride += static_cast<size_t>(sz);
      }

      auto hasField = [&](const char* name) { return offsetOf.count(name) != 0; };
      if (!hasField("x") || !hasField("y") || !hasField("z"))
      {
         outError = "PLY vertex element has no x/y/z position: " + path;
         std::fclose(f);
         return false;
      }

      // Presence of the trained-3DGS-specific properties, decided once from
      // the header rather than per vertex. When any of these is absent this
      // is (almost certainly) a plain colored point cloud, not real trained
      // splat data - see the graceful-degradation comment in SplatIO.h.
      const bool haveDc = hasField("f_dc_0");
      const bool haveRgbColor = hasField("red") && hasField("green") && hasField("blue");
      const bool haveOpacity = hasField("opacity");
      const bool haveAlpha = hasField("alpha");
      const bool haveScale = hasField("scale_0") && hasField("scale_1") && hasField("scale_2");
      out.hadTrainedFields = haveDc && haveOpacity && haveScale;

      // --- Body: one vertex record at a time. ---
      out.splats.reserve(static_cast<size_t>(vertexCount));
      std::vector<unsigned char> record(stride);

      for (long long i = 0; i < vertexCount; ++i)
      {
         if (std::fread(record.data(), 1, stride, f) != stride)
         {
            outError = "PLY body truncated (expected " + std::to_string(vertexCount) + " vertices): " + path;
            std::fclose(f);
            return false;
         }

         auto readOr = [&](const char* name, float fallback) -> float {
            auto it = offsetOf.find(name);
            if (it == offsetOf.end())
               return fallback;
            return ReadPlyFloat(record.data(), it->second, typeOf[name]);
         };

         Splat s;
         s.px = readOr("x", 0.0f);
         s.py = readOr("y", 0.0f);
         s.pz = readOr("z", 0.0f);

         if (haveDc)
         {
            const float dc0 = readOr("f_dc_0", 0.0f);
            const float dc1 = readOr("f_dc_1", 0.0f);
            const float dc2 = readOr("f_dc_2", 0.0f);
            s.r = 0.5f + kShC0 * dc0;
            s.g = 0.5f + kShC0 * dc1;
            s.b = 0.5f + kShC0 * dc2;
         }
         else if (haveRgbColor)
         {
            // Plain PLY color bytes, not SH-DC - no SH_C0 conversion, just
            // 0..255 -> 0..1.
            s.r = readOr("red", 0.0f) / 255.0f;
            s.g = readOr("green", 0.0f) / 255.0f;
            s.b = readOr("blue", 0.0f) / 255.0f;
         }
         else
         {
            s.r = s.g = s.b = 0.5f; // genuinely no color data available
         }

         float alpha;
         if (haveOpacity)
         {
            alpha = Sigmoid(readOr("opacity", 0.0f)); // sigmoid(0) = 0.5, a sane default alpha
         }
         else if (haveAlpha)
         {
            alpha = readOr("alpha", 255.0f) / 255.0f; // plain alpha byte, not a logit - no sigmoid
         }
         else
         {
            alpha = 1.0f; // no opacity concept at all - render solid, not semi-transparent
         }

         const float logSx = readOr("scale_0", 0.0f);
         const float logSy = readOr("scale_1", 0.0f);
         const float logSz = readOr("scale_2", 0.0f);
         const bool haveRot = hasField("rot_0") && hasField("rot_1") && hasField("rot_2") && hasField("rot_3");
         const float rw = readOr("rot_0", 1.0f); // INRIA convention: rot_0..3 is wxyz, not xyzw
         const float rx = readOr("rot_1", 0.0f);
         const float ry = readOr("rot_2", 0.0f);
         const float rz = readOr("rot_3", 0.0f);

         FinishSplat(s, alpha, logSx, logSy, logSz, rw, rx, ry, rz, haveRot);
         out.splats.push_back(s);
      }

      std::fclose(f);
      UpdateBounds(out);

      // Scale fallback: scale_0/1/2 absent means FinishSplat exp()'d a
      // default log-scale of 0 into a 1.0-world-unit radius for every splat
      // above, which is nonsense for a typical (much smaller than 1 world
      // unit) point-cloud scan - overwrite with a radius estimated from the
      // cloud's own point density instead. Needs UpdateBounds()'s bounding
      // box, hence the second pass rather than folding into the main loop.
      if (!haveScale && !out.splats.empty())
      {
         const float dx = out.boundsMax[0] - out.boundsMin[0];
         const float dy = out.boundsMax[1] - out.boundsMin[1];
         const float dz = out.boundsMax[2] - out.boundsMin[2];
         const float diag = std::sqrt(dx * dx + dy * dy + dz * dz);
         // Average nearest-neighbor spacing for N points ~uniformly filling
         // that bounding volume; guarded against splatCount == 0 above.
         const float spacing = diag / std::cbrt(static_cast<float>(out.splats.size()));
         const float defaultRadius = std::max(spacing * 0.5f, 1e-4f);
         for (Splat& s : out.splats)
         {
            s.sx = s.sy = s.sz = defaultRadius;
            ComputeCovariance(s.qw, s.qx, s.qy, s.qz, s.sx, s.sy, s.sz, s.cov);
         }
      }

      return true;
   }

   bool LoadSplatFile(const std::string& path, SplatCloud& out, std::string& outError)
   {
      out = SplatCloud();

      FILE* f = std::fopen(path.c_str(), "rb");
      if (!f)
      {
         outError = "could not open file: " + path;
         return false;
      }

      std::fseek(f, 0, SEEK_END);
      const long sizeBytes = std::ftell(f);
      std::fseek(f, 0, SEEK_SET);

      constexpr long kRecordSize = 32;
      if (sizeBytes < 0 || (sizeBytes % kRecordSize) != 0)
      {
         outError = ".splat file size is not a multiple of 32 bytes: " + path;
         std::fclose(f);
         return false;
      }

      const long count = sizeBytes / kRecordSize;
      out.splats.reserve(static_cast<size_t>(count));

      unsigned char record[kRecordSize];
      for (long i = 0; i < count; ++i)
      {
         if (std::fread(record, 1, kRecordSize, f) != static_cast<size_t>(kRecordSize))
         {
            outError = ".splat body truncated: " + path;
            std::fclose(f);
            return false;
         }

         float pos[3], scale[3];
         std::memcpy(pos, record + 0, 12);
         std::memcpy(scale, record + 12, 12);
         const unsigned char* color = record + 24; // r,g,b,a, 0..255
         const unsigned char* rot = record + 28;   // w,x,y,z quantized, wxyz order

         Splat s;
         s.px = pos[0];
         s.py = pos[1];
         s.pz = pos[2];
         s.r = color[0] / 255.0f;
         s.g = color[1] / 255.0f;
         s.b = color[2] / 255.0f;
         s.a = color[3] / 255.0f;
         s.sx = scale[0]; // .splat stores linear scale directly, not log
         s.sy = scale[1];
         s.sz = scale[2];

         const float rw = (static_cast<float>(rot[0]) - 128.0f) / 128.0f;
         const float rx = (static_cast<float>(rot[1]) - 128.0f) / 128.0f;
         const float ry = (static_cast<float>(rot[2]) - 128.0f) / 128.0f;
         const float rz = (static_cast<float>(rot[3]) - 128.0f) / 128.0f;
         const float len = std::sqrt(rw * rw + rx * rx + ry * ry + rz * rz);
         const float inv = (len > 1e-8f) ? (1.0f / len) : 1.0f;
         s.qw = rw * inv;
         s.qx = rx * inv;
         s.qy = ry * inv;
         s.qz = rz * inv;

         ComputeCovariance(s.qw, s.qx, s.qy, s.qz, s.sx, s.sy, s.sz, s.cov);
         out.splats.push_back(s);
      }

      std::fclose(f);
      UpdateBounds(out);
      return true;
   }
}
