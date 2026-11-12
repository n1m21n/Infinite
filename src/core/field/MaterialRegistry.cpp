#include "MaterialRegistry.h"

#include "FieldIR.h"
#include "FieldLex.h"
#include "FieldParse.h"
#include "GlslBackend.h"
#include "MslBackend.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <mutex>

namespace Field
{
   namespace
   {
      std::mutex gMu;
      std::deque<MaterialProgramEntry> gEntries; // id = index + 1; a deque keeps returned pointers valid

      unsigned long long Fnv(const void* p, size_t n, unsigned long long h = 1469598103934665603ull)
      {
         const unsigned char* b = static_cast<const unsigned char*>(p);
         for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
         return h;
      }

      std::string At(const FieldError& e)
      {
         return "line " + std::to_string(e.span.line) + ", col " + std::to_string(e.span.col) + ": " + e.message;
      }
   }

   int RegisterMaterialProgram(const std::string& source, const std::vector<float>& paramValues, std::string& err)
   {
      err.clear();
      unsigned long long h = Fnv(source.data(), source.size());
      // Live knobs (the first kMaterialLiveParams) are data, so only the baked ones are part of the program.
      if (paramValues.size() > (size_t)kMaterialLiveParams)
         h = Fnv(paramValues.data() + kMaterialLiveParams, (paramValues.size() - kMaterialLiveParams) * sizeof(float), h);
      {
         std::lock_guard<std::mutex> lk(gMu);
         for (const MaterialProgramEntry& e : gEntries)
            if (e.hash == h) return e.id;
      }
      std::vector<Token> tokens;
      FieldError fe;
      if (!Lex(source, tokens, fe)) { err = At(fe); return 0; }
      AstNodePtr ast;
      if (!ParseProgram(tokens, ast, fe)) { err = At(fe); return 0; }
      PixelIRProgram ir;
      if (!LowerPixelProgramToIR(ast, ir, fe, PixelProgramKind::Material)) { err = At(fe); return 0; }

      std::lock_guard<std::mutex> lk(gMu);
      for (const MaterialProgramEntry& e : gEntries) // another thread may have registered it meanwhile
         if (e.hash == h) return e.id;
      MaterialProgramEntry e;
      e.id = (int)gEntries.size() + 1;
      e.hash = h;
      e.written = ir.materialWritten;
      const std::string fn = "fldmat_" + std::to_string(e.id);
      MaterialFnResult g = EmitGlslMaterialFn(ir, fn, paramValues);
      if (!g.error.empty()) { err = g.error; return 0; }
      MaterialFnResult m = EmitMslMaterialFn(ir, fn, paramValues);
      if (!m.error.empty()) { err = m.error; return 0; }
      {
         // the signature names fld_t once; any further whole-word use is a read of t
         int uses = 0;
         for (size_t at = g.source.find("fld_t"); at != std::string::npos; at = g.source.find("fld_t", at + 5))
         {
            const char nx = at + 5 < g.source.size() ? g.source[at + 5] : ' ';
            if (!(std::isalnum((unsigned char)nx) || nx == '_')) uses++;
         }
         e.usesTime = uses > 1;
         // every function declares fld_v_curvature / fld_v_cavity from G; a second whole-word use is a read
         auto reads = [&](const std::string& name) {
            int n = 0;
            for (size_t at = g.source.find(name); at != std::string::npos; at = g.source.find(name, at + name.size()))
            {
               const char nx = at + name.size() < g.source.size() ? g.source[at + name.size()] : ' ';
               if (!(std::isalnum((unsigned char)nx) || nx == '_')) n++;
            }
            return n > 1;
         };
         e.usesWear = reads("fld_v_curvature") || reads("fld_v_cavity");
      }
      e.glsl = std::move(g.source);
      e.msl = std::move(m.source);
      gEntries.push_back(std::move(e));
      return gEntries.back().id;
   }

   const MaterialProgramEntry* FindMaterialProgram(int id)
   {
      std::lock_guard<std::mutex> lk(gMu);
      return (id >= 1 && id <= (int)gEntries.size()) ? &gEntries[id - 1] : nullptr;
   }

   bool MaterialProgramSeeThrough(int id)
   {
      const MaterialProgramEntry* e = FindMaterialProgram(id);
      if (!e || !MaterialProgramError(id).empty()) return false;
      const uint32_t cut = (1u << MaterialOutputBit("opacity")) | (1u << MaterialOutputBit("mask"));
      return (e->written & cut) != 0;
   }

   std::string BuildMaterialLibrary(const std::vector<int>& idsIn, MaterialLang lang, unsigned long long& key)
   {
      std::vector<int> ids;
      for (int id : idsIn)
         if (FindMaterialProgram(id) && MaterialProgramError(id).empty()) ids.push_back(id);
      std::sort(ids.begin(), ids.end());
      ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
      key = ids.empty() ? 0 : Fnv(ids.data(), ids.size() * sizeof(int), lang == MaterialLang::Glsl ? 7ull : 11ull);
      if (ids.empty()) return std::string();
      const bool g = lang == MaterialLang::Glsl;
      std::string s = "\n#define FLD_MAT_ANY 1\n";
      s += g ? GlslMaterialPrelude() : MslMaterialPrelude();
      s += "\n";
      for (int id : ids)
         s += (g ? FindMaterialProgram(id)->glsl : FindMaterialProgram(id)->msl) + "\n";
      s += g ? "void fldMatDispatch(int id, inout FldMat o, vec3 P, vec3 N, vec2 UV, float t, vec3 W, vec3 V, vec4 G)\n{\n"
             : "static void fldMatDispatch(int id, thread FldMat& o, float3 P, float3 N, float2 UV, float t, float3 W, float3 V, float4 G, "
               "array<texture2d<float>, 16> fm, float4 fi, float flod)\n{\n";
      for (int id : ids)
      {
         const std::string n = std::to_string(id);
         s += "   if (id == " + n + ") fldmat_" + n + (g ? "(o, P, N, UV, t, W, V, G);\n" : "(o, P, N, UV, t, W, V, G, fm, fi, flod);\n");
      }
      s += "}\n";
      // Row 2.6: which programs write height (the tracer takes two more taps for those, to bump the normal).
      s += g ? "bool fldMatHasHeight(int id)\n{\n" : "static bool fldMatHasHeight(int id)\n{\n";
      for (int id : ids)
         if (FindMaterialProgram(id)->written & (1u << MaterialOutputBit("height")))
            s += "   if (id == " + std::to_string(id) + ") return true;\n";
      s += "   return false;\n}\n";
      return s;
   }

   std::string MaterialProgramError(int id)
   {
      std::lock_guard<std::mutex> lk(gMu);
      return (id >= 1 && id <= (int)gEntries.size()) ? gEntries[id - 1].error : std::string();
   }

   int IsolateFailingPrograms(const std::vector<int>& ids, MaterialLang lang,
                              const std::function<std::string(const std::string&)>& compile)
   {
      std::vector<int> u(ids);
      std::sort(u.begin(), u.end());
      u.erase(std::unique(u.begin(), u.end()), u.end());
      int marked = 0;
      for (int id : u)
      {
         unsigned long long key = 0;
         const std::string lib = BuildMaterialLibrary({ id }, lang, key);
         if (lib.empty()) continue; // unknown or already failed
         std::string e = compile(lib);
         if (e.empty()) continue;
         std::lock_guard<std::mutex> lk(gMu);
         if (gEntries[id - 1].error.empty()) gEntries[id - 1].error = std::move(e);
         marked++;
      }
      return marked;
   }

   void BreakMaterialProgramForTest(int id)
   {
      std::lock_guard<std::mutex> lk(gMu);
      if (id < 1 || id > (int)gEntries.size()) return;
      gEntries[id - 1].glsl += "\nthis does not compile;\n";
      gEntries[id - 1].msl += "\nthis does not compile;\n";
   }
}
