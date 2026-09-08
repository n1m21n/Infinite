// Standalone Field compiler front-end, for CI/dev use only: runs a Field
// program (pixel or element domain) through Lex -> Parse -> LowerToIR ->
// EmitGlsl and reports the first error, exactly what FieldPixelNode::Apply()
// and FieldElementNode::Apply() do at load time. There is no way to catch a
// broken preset except running it inside the app - this closes that gap for
// new presets added to Presets() tables, so a broken one never reaches
// main before a human loads it once.
//
// Usage: field-preset-check pixel|element <path-to-field-source>
// Exit 0 and prints the emitted GLSL/bytecode summary on success; exit 1 and
// prints "line N: message" on any lex/parse/IR/emit error.
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "field/FieldIR.h"
#include "field/GlslBackend.h"

#include <fstream>
#include <iostream>
#include <sstream>

// Field's own ExprGlobals module (patch-wide named values referenced from
// ordinary Formula/parameter expressions) is irrelevant to compiling a
// pixel/element kernel - LowerAstExpr only calls ExprGlobals::All() to seed
// name lookups, so an empty table here behaves identically to a patch with
// no globals defined, which is the common case for a preset under test.
namespace ExprGlobals
{
   struct Global { std::string name; std::string expr; float value = 0; std::string error; };
   std::vector<Global>& All() { static std::vector<Global> g; return g; }
}

namespace
{
   bool ReadFile(const std::string& path, std::string& out)
   {
      std::ifstream f(path);
      if (!f) return false;
      std::ostringstream ss;
      ss << f.rdbuf();
      out = ss.str();
      return true;
   }
}

int main(int argc, char** argv)
{
   if (argc < 3)
   {
      std::cerr << "usage: field-preset-check pixel|element <path-to-field-source>\n";
      return 2;
   }

   std::string domainArg = argv[1];
   std::string path = argv[2];
   std::string code;
   if (!ReadFile(path, code))
   {
      std::cerr << "could not read " << path << "\n";
      return 2;
   }

   std::vector<Field::Token> tokens;
   Field::FieldError err;
   if (!Field::Lex(code, tokens, err))
   {
      std::cerr << "line " << err.span.line << ", col " << err.span.col << ": " << err.message << "\n";
      return 1;
   }

   Field::AstNodePtr ast;
   if (!Field::ParseProgram(tokens, ast, err))
   {
      std::cerr << "line " << err.span.line << ", col " << err.span.col << ": " << err.message << "\n";
      return 1;
   }

   if (domainArg == "pixel")
   {
      Field::PixelIRProgram ir;
      if (!Field::LowerPixelProgramToIR(ast, ir, err))
      {
         std::cerr << "line " << err.span.line << ", col " << err.span.col << ": " << err.message << "\n";
         return 1;
      }

      Field::GlslEmitResult emit = Field::EmitGlsl(ir);
      if (!emit.error.empty())
      {
         std::cerr << "emit error: " << emit.error << "\n";
         return 1;
      }

      std::cout << emit.source << std::endl;
      return 0;
   }

   std::cerr << "unsupported domain '" << domainArg << "' (only 'pixel' is wired up so far)\n";
   return 2;
}
