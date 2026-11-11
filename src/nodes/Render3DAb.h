#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

// Render 3D shader A/B switches, for measuring a shading change without a rebuild per variant.
// INFINITE_R3D_AB is a comma list of names (e.g. "ggx,fresnel"); each name becomes the preprocessor macro
// R3D_AB_<NAME> in every Render 3D shader (GL forward, GL path tracer, Metal). A candidate change sits in the
// shader under #ifdef R3D_AB_<NAME>; tools/blender-ab/sweep.py --ab <names> renders with it on. When the
// numbers pick a winner, the #ifdef is removed and the winning branch stays. Unset = shipped behaviour.
inline std::vector<std::string> R3dAbMacros()
{
   std::vector<std::string> out;
   const char* e = getenv("INFINITE_R3D_AB");
   std::string cur;
   for (const char* p = e ? e : ""; ; ++p)
   {
      if (*p && *p != ',')
      {
         if (isalnum((unsigned char)*p) || *p == '_')
            cur += (char)toupper((unsigned char)*p);
         continue;
      }
      if (!cur.empty())
         out.push_back("R3D_AB_" + cur);
      cur.clear();
      if (!*p)
         break;
   }
   return out;
}

// A switch read from C++ (a change that is not in a shader): name in capitals, e.g. R3dAbOn("MAPMIPS").
inline bool R3dAbOn(const char* name)
{
   for (const std::string& m : R3dAbMacros())
      if (m == std::string("R3D_AB_") + name)
         return true;
   return false;
}

// GLSL source with the active switches defined right after its #version line.
inline std::string R3dAbGlsl(const char* src)
{
   std::string s = src;
   std::string defs;
   for (const std::string& m : R3dAbMacros())
      defs += "#define " + m + " 1\n";
   if (!defs.empty())
      s.insert(s.find('\n') + 1, defs);
   return s;
}
