#include "PlyPoints.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include "AppPaths.h"

namespace PlyPoints
{
   namespace
   {
      enum class Format { kAscii, kLittle, kBig };

      struct Prop
      {
         std::string name;
         int width = 4;        // bytes in binary form
         bool isFloat = false;
         bool isSigned = false;
         bool isList = false;  // a list property: only legal outside the vertex element here
      };

      struct Element
      {
         std::string name;
         long long count = 0;
         std::vector<Prop> props;
      };

      bool TypeOf(const std::string& type, Prop& p)
      {
         struct T { const char* name; int w; bool f; bool s; };
         static const T kTypes[] = {
            { "char", 1, false, true }, { "int8", 1, false, true },
            { "uchar", 1, false, false }, { "uint8", 1, false, false },
            { "short", 2, false, true }, { "int16", 2, false, true },
            { "ushort", 2, false, false }, { "uint16", 2, false, false },
            { "int", 4, false, true }, { "int32", 4, false, true },
            { "uint", 4, false, false }, { "uint32", 4, false, false },
            { "float", 4, true, true }, { "float32", 4, true, true },
            { "double", 8, true, true }, { "float64", 8, true, true },
         };
         for (const T& t : kTypes)
         {
            if (type == t.name)
            {
               p.width = t.w;
               p.isFloat = t.f;
               p.isSigned = t.s;
               return true;
            }
         }
         return false;
      }

      double ReadBinary(const unsigned char* p, const Prop& prop, Format format)
      {
         unsigned char b[8] = {};
         for (int k = 0; k < prop.width; k++)
            b[k] = format == Format::kBig ? p[prop.width - 1 - k] : p[k];
         uint64_t bits = 0;
         for (int k = prop.width - 1; k >= 0; k--)
            bits = (bits << 8) | b[k];
         if (prop.isFloat)
         {
            if (prop.width == 4)
            {
               const uint32_t u = (uint32_t)bits;
               float f;
               std::memcpy(&f, &u, 4);
               return f;
            }
            double d;
            std::memcpy(&d, &bits, 8);
            return d;
         }
         if (!prop.isSigned)
            return (double)bits;
         const int shift = 64 - prop.width * 8;
         return (double)((int64_t)(bits << shift) >> shift);
      }

      // An integer colour channel spans its type's range; a float one is 0..1.
      float ColourChannel(double v, const Prop& prop)
      {
         if (!prop.isFloat)
         {
            const double top = prop.width == 1 ? 255.0 : prop.width == 2 ? 65535.0 : 4294967295.0;
            v /= top;
         }
         return (float)std::min(1.0, std::max(0.0, v));
      }
   }

   Kind Parse(const unsigned char* data, size_t size, Cloud& out, std::string& error)
   {
      out = Cloud();
      size_t pos = 0;
      auto nextLine = [&](std::string& line) -> bool {
         if (pos >= size)
            return false;
         const size_t start = pos;
         while (pos < size && data[pos] != '\n')
            pos++;
         size_t end = pos;
         if (pos < size)
            pos++; // past '\n'
         if (end > start && data[end - 1] == '\r')
            end--;
         line.assign(reinterpret_cast<const char*>(data) + start, end - start);
         return true;
      };

      std::string line;
      if (!nextLine(line) || line.compare(0, 3, "ply") != 0)
      {
         error = "not a PLY file";
         return Kind::kError;
      }

      Format format = Format::kAscii;
      std::vector<Element> elements;
      bool ended = false;
      while (nextLine(line))
      {
         std::istringstream in(line);
         std::string word;
         in >> word;
         if (word == "format")
         {
            std::string f;
            in >> f;
            if (f == "ascii") format = Format::kAscii;
            else if (f == "binary_little_endian") format = Format::kLittle;
            else if (f == "binary_big_endian") format = Format::kBig;
            else
            {
               error = "unknown PLY format " + f;
               return Kind::kError;
            }
         }
         else if (word == "element")
         {
            Element e;
            in >> e.name >> e.count;
            elements.push_back(e);
         }
         else if (word == "property" && !elements.empty())
         {
            std::string type;
            in >> type;
            Prop p;
            if (type == "list")
            {
               std::string countType, itemType;
               in >> countType >> itemType >> p.name;
               p.isList = true;
            }
            else
            {
               in >> p.name;
               if (!TypeOf(type, p))
               {
                  error = "unknown PLY property type " + type;
                  return Kind::kError;
               }
            }
            elements.back().props.push_back(p);
         }
         else if (word == "end_header")
         {
            ended = true;
            break;
         }
      }
      if (!ended)
      {
         error = "PLY header has no end_header";
         return Kind::kError;
      }

      int vertexElement = -1;
      for (size_t e = 0; e < elements.size(); e++)
      {
         if (elements[e].name == "vertex" && vertexElement < 0)
            vertexElement = (int)e;
         if (elements[e].name == "face" && elements[e].count > 0)
            return Kind::kMesh;
      }
      if (vertexElement < 0 || elements[vertexElement].count <= 0)
      {
         error = "PLY has no vertices";
         return Kind::kError;
      }

      const std::vector<Prop>& props = elements[vertexElement].props;
      auto find = [&](const char* name) -> int {
         for (size_t i = 0; i < props.size(); i++)
            if (props[i].name == name)
               return (int)i;
         return -1;
      };
      const int ix = find("x"), iy = find("y"), iz = find("z");
      if (ix < 0 || iy < 0 || iz < 0)
      {
         error = "PLY has no x/y/z properties";
         return Kind::kError;
      }
      for (const Prop& p : props)
      {
         if (p.isList)
         {
            error = "PLY vertex element has a list property";
            return Kind::kError;
         }
      }
      int ir = find("red"), ig = find("green"), ib = find("blue");
      if (ir < 0 || ig < 0 || ib < 0)
      {
         ir = find("r"); ig = find("g"); ib = find("b");
      }
      const bool rgb = ir >= 0 && ig >= 0 && ib >= 0;
      const int d0 = find("f_dc_0"), d1 = find("f_dc_1"), d2 = find("f_dc_2");
      const bool splat = !rgb && d0 >= 0 && d1 >= 0 && d2 >= 0;
      const int inx = find("nx"), iny = find("ny"), inz = find("nz");
      const bool normals = inx >= 0 && iny >= 0 && inz >= 0;

      // Elements before the vertex element are skipped whole; that is only
      // possible when their rows have a fixed size (no lists) in binary.
      if (format == Format::kAscii)
      {
         for (int e = 0; e < vertexElement; e++)
            for (long long k = 0; k < elements[e].count; k++)
               if (!nextLine(line))
               {
                  error = "PLY body is truncated";
                  return Kind::kError;
               }
      }
      else
      {
         for (int e = 0; e < vertexElement; e++)
         {
            size_t row = 0;
            for (const Prop& p : elements[e].props)
            {
               if (p.isList)
               {
                  error = "PLY element before the vertices has a list property";
                  return Kind::kError;
               }
               row += (size_t)p.width;
            }
            pos += row * (size_t)elements[e].count;
         }
      }

      std::vector<size_t> offsets(props.size(), 0);
      size_t rowBytes = 0;
      for (size_t i = 0; i < props.size(); i++)
      {
         offsets[i] = rowBytes;
         rowBytes += (size_t)props[i].width;
      }

      const long long count = elements[vertexElement].count;
      if (format != Format::kAscii && (pos > size || (size - pos) / std::max<size_t>(rowBytes, 1) < (size_t)count))
      {
         error = "PLY body is truncated";
         return Kind::kError;
      }
      out.points.reserve((size_t)count);
      std::vector<double> vals(props.size(), 0.0);
      for (long long v = 0; v < count; v++)
      {
         if (format == Format::kAscii)
         {
            if (!nextLine(line))
            {
               error = "PLY body is truncated";
               return Kind::kError;
            }
            const char* c = line.c_str();
            for (size_t i = 0; i < props.size(); i++)
            {
               char* endp = nullptr;
               vals[i] = std::strtod(c, &endp);
               if (endp == c)
               {
                  error = "PLY has a short vertex row";
                  return Kind::kError;
               }
               c = endp;
            }
         }
         else
         {
            const unsigned char* row = data + pos + (size_t)v * rowBytes;
            for (size_t i = 0; i < props.size(); i++)
               vals[i] = ReadBinary(row + offsets[i], props[i], format);
         }

         Particle p;
         p.px = (float)vals[ix]; p.py = (float)vals[iy]; p.pz = (float)vals[iz];
         if (normals)
         {
            p.nx = (float)vals[inx]; p.ny = (float)vals[iny]; p.nz = (float)vals[inz];
         }
         if (rgb)
         {
            p.r = ColourChannel(vals[ir], props[ir]);
            p.g = ColourChannel(vals[ig], props[ig]);
            p.b = ColourChannel(vals[ib], props[ib]);
            p.hasColor = true;
         }
         else if (splat)
         {
            const double c0 = 0.28209479177387814; // Y_0^0
            p.r = (float)std::min(1.0, std::max(0.0, 0.5 + c0 * vals[d0]));
            p.g = (float)std::min(1.0, std::max(0.0, 0.5 + c0 * vals[d1]));
            p.b = (float)std::min(1.0, std::max(0.0, 0.5 + c0 * vals[d2]));
            p.hasColor = true;
         }
         out.points.push_back(p);
      }
      out.hasColor = rgb || splat;
      out.hasNormals = normals;
      return Kind::kPoints;
   }

   Kind Read(const std::string& path, Cloud& out, std::string& error)
   {
      std::ifstream file(AppPaths::FsPath(path), std::ios::binary);
      if (!file)
      {
         error = "cannot open file";
         return Kind::kError;
      }
      // The header alone says whether the file has faces, so a mesh .ply
      // (which the caller then reads through the model loader anyway) costs
      // one small read here, not the whole file.
      std::vector<unsigned char> head(64 * 1024);
      file.read(reinterpret_cast<char*>(head.data()), (std::streamsize)head.size());
      head.resize((size_t)file.gcount());
      if (Parse(head.data(), head.size(), out, error) == Kind::kMesh)
      {
         out = Cloud();
         return Kind::kMesh;
      }
      if (!file.eof())
         head.insert(head.end(), std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      error.clear();
      return Parse(head.data(), head.size(), out, error);
   }
}
