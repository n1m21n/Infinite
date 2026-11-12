#pragma once

// Material programs (OPUS_PLAN rows 1.2 to 1.5): a Field Pixel kernel in the Material domain, compiled once per
// source (row 3.4: knob values are live data, FldMat.prm0..3, except knobs past kMaterialLiveParams, which are baked)
// into one GLSL and one MSL function. The id is process-wide and stable for the run, and it is
// the number the shaders switch on (MatRec.program on Metal, uFldProgram on GL), so no remapping happens per frame.
//
// A host shader gets one library string per language for the set of ids it draws (BuildMaterialLibrary): it defines
// FLD_MAT_ANY 1, the FldMat struct, one function per id and
//   fldMatDispatch(int id, inout FldMat o, P, N, UV, t, W, V)
// The library is empty for an empty set, so a scene without programs compiles exactly the shader it had before.

#include <cstdint>
#include <functional>
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
      bool usesWear = false; // reads curvature or cavity (row 4.4): the renderer bakes them per vertex
      std::string error;     // set when a backend could not compile it (read with MaterialProgramError)
   };

   // Lex, parse, lower (Material kind) and emit both languages. Returns the id (> 0), or 0 with err set (line-numbered
   // for a source error). The same source gives the same id whatever its first 16 param values.
   int RegisterMaterialProgram(const std::string& source, const std::vector<float>& paramValues, std::string& err);
   const MaterialProgramEntry* FindMaterialProgram(int id);
   // True when the program writes opacity or mask: the surface can be cut or see-through.
   bool MaterialProgramSeeThrough(int id);

   enum class MaterialLang { Glsl, Msl };
   // ids: the set a renderer draws (any order, duplicates and unknown ids ignored). key: the sorted set's hash, 0 for
   // an empty set; a renderer relinks when it changes.
   // A failed program (row 1.5) is left out of every library, so its material draws its knobs and the others keep
   // running.
   std::string BuildMaterialLibrary(const std::vector<int>& ids, MaterialLang lang, unsigned long long& key);

   // Failure path (row 1.5). A backend whose library does not compile finds the programs that fail alone and marks
   // them: compile(lib) returns "" when lib builds in that backend, else the compiler's first error line. Returns how
   // many it marked. The first error a program gets is kept; the Field Pixel node shows it.
   int IsolateFailingPrograms(const std::vector<int>& ids, MaterialLang lang,
                              const std::function<std::string(const std::string&)>& compile);
   std::string MaterialProgramError(int id);
   // Test hook: both sources of id stop compiling in any backend.
   void BreakMaterialProgramForTest(int id);
}
