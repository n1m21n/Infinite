#pragma once

// Helpers shared by the PointZ operator files (not part of the public API).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

#include "PopOps.h"

namespace pop
{
   namespace popa
   {
      constexpr float kPi = 3.14159265358979f;

      struct Target
      {
         std::string name;
         int comps = 1;
      };

      // Resolves a text param naming an attribute. comps: the input's, else
      // the name's default, unless forced.
      inline bool ResolveTarget(CookCtx& c, const std::string& key, Target& t, int forceComps = 0)
      {
         t.name = c.S(key);
         if (!ValidAttribName(t.name))
         {
            c.error = "'" + t.name + "' is not a usable attribute name";
            return false;
         }
         const PopData* in = c.Input(0);
         t.comps = forceComps > 0 ? forceComps : (in ? in->CompsOf(t.name) : DefaultComps(t.name));
         return true;
      }

      inline std::string Swz(int comps)
      {
         static const char* k[] = { "x", "x", "xy", "xyz", "xyzw" };
         return k[std::max(1, std::min(4, comps))];
      }

      // "#define A name / TA type / SW swizzle" so one GLSL body serves every
      // component count: `A = TA((vec4expr).SW);`.
      inline std::string Defines(const Target& t)
      {
         return "\n#define A " + t.name + "\n#define TA " + GlslType(t.comps) + "\n#define SW " + Swz(t.comps) + "\n";
      }

      inline void AddRead(KernelSpec& s, const std::string& name, int comps)
      {
         for (const KernelIO& r : s.reads)
            if (r.name == name)
               return;
         s.reads.push_back({ name, comps });
      }

      // Runs a kernel over input 0 with every write blended by the op's mask.
      inline bool Run(CookCtx& c, KernelSpec spec, const std::vector<KernelUniform>& extra = {})
      {
         for (const KernelIO& w : spec.writes)
            AddRead(spec, w.name, w.comps);
         const std::string mask = (c.def->ParamIndex("mask") >= 0) ? c.S("mask") : std::string();
         if (!mask.empty())
         {
            if (!ValidAttribName(mask))
               c.warning = "mask '" + mask + "' is not an attribute name";
            else
            {
               AddRead(spec, mask, 1);
               std::string pre, post;
               for (const KernelIO& w : spec.writes)
                  pre += std::string("   ") + GlslType(w.comps) + " old_" + w.name + " = " + w.name + ";\n";
               post += "   float mask_ = clamp(float(" + mask + "), 0.0, 1.0);\n";
               for (const KernelIO& w : spec.writes)
                  post += "   " + w.name + " = mix(old_" + w.name + ", " + w.name + ", mask_);\n";
               spec.body = pre + "   {\n" + spec.body + "\n   }\n" + post;
            }
         }
         return c.Kernel(spec, *c.Input(0), extra);
      }

      inline KernelUniform Mat(const char* name, const Mat4& m)
      {
         KernelUniform u;
         u.name = name;
         u.type = KernelUniform::kMat4;
         std::memcpy(u.v, m.m, sizeof(float) * 16);
         return u;
      }
      inline KernelUniform Vec(const char* name, float x, float y, float z)
      {
         KernelUniform u;
         u.name = name;
         u.type = KernelUniform::kVec3;
         u.v[0] = x; u.v[1] = y; u.v[2] = z;
         return u;
      }
      inline KernelUniform Flt(const char* name, float x)
      {
         KernelUniform u;
         u.name = name;
         u.type = KernelUniform::kFloat;
         u.v[0] = x;
         return u;
      }
      inline KernelUniform Int(const char* name, int x)
      {
         KernelUniform u;
         u.name = name;
         u.type = KernelUniform::kInt;
         u.i = x;
         return u;
      }

      inline std::vector<std::string> SplitList(const std::string& s)
      {
         std::vector<std::string> out;
         std::string cur;
         for (char ch : s)
         {
            if (ch == ',' || ch == ' ' || ch == ';')
            {
               if (!cur.empty()) out.push_back(cur);
               cur.clear();
            }
            else
               cur += ch;
         }
         if (!cur.empty()) out.push_back(cur);
         return out;
      }

      inline std::vector<std::string> Axes() { return { "X", "Y", "Z" }; }

      inline Mat4 Euler(float rx, float ry, float rz)
      {
         const float d = kPi / 180.0f;
         return Mat4::Multiply(Mat4::RotationZ(rz * d), Mat4::Multiply(Mat4::RotationY(ry * d), Mat4::RotationX(rx * d)));
      }

      inline PopDef Simple(const char* name, const char* td, const char* help, int inputs)
      {
         PopDef d;
         d.name = name;
         d.tdName = td;
         d.help = help;
         d.inputs = inputs;
         d.tier = 'A';
         return d;
      }

      inline ParamSpec Mask() { return T("mask", "mask attribute", ""); }
   }


   void RegisterKernelsA1(std::vector<PopDef>& defs);
   void RegisterKernelsA2(std::vector<PopDef>& defs);
   // "name" or "name:comps" list -> kernel writes (PopOpsA2.cpp).
   bool ParseWritesPublic(CookCtx& c, const std::string& list, std::vector<KernelIO>& w);
}
