#pragma once

// Material programs (OPUS_PLAN rows 1.2 to 1.5): a Field Pixel kernel in the Material domain, compiled once per
// (source, param values) into one GLSL and one MSL function. The id is process-wide and stable for the run, and it is
// the number the shaders switch on (MatRec.program on Metal, uFldProgram on GL), so no remapping happens per frame.
//
// A host shader gets one library string per language for the set of ids it draws (BuildMaterialLibrary): it defines
// FLD_MAT_ANY 1, the FldMat struct, one function per id and
//   fldMatDispatch(int id, inout FldMat o, P, N, UV, t)
// The library is empty for an empty set, so a scene without programs compiles exactly the shader it had before.

#include <cstdint>
#include <string>
#include <vector>

namespace Field
{
   struct MaterialProgramEntry
   {
      int id = 0;
      unsigned long long hash = 0;
      std::string glsl, msl; // one function each, named fldmat_<id>
      uint32_t written = 0;  // MaterialOutputBit mask of the outputs the program writes
      bool usesTime = false; // reads t: the picture changes every frame
   };

   // Lex, parse, lower (Material kind) and emit both languages. Returns the id (> 0), or 0 with err set (line-numbered
   // for a source error). The same source and param values give the same id.
   int RegisterMaterialProgram(const std::string& source, const std::vector<float>& paramValues, std::string& err);
   const MaterialProgramEntry* FindMaterialProgram(int id);
   // True when the program writes opacity or mask: the surface can be cut or see-through.
   bool MaterialProgramSeeThrough(int id);

   enum class MaterialLang { Glsl, Msl };
   // ids: the set a renderer draws (any order, duplicates and unknown ids ignored). key: the sorted set's hash, 0 for
   // an empty set; a renderer relinks when it changes.
   std::string BuildMaterialLibrary(const std::vector<int>& ids, MaterialLang lang, unsigned long long& key);
}
