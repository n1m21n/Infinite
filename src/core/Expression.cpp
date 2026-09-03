#include "Expression.h"
#include "field/FieldProgramCache.h"
#include "field/FieldVM.h"

#include <cctype>

namespace Expression
{
   bool Evaluate(const std::string& text, double t,
                 const std::map<std::string, float>* siblings,
                 const std::map<std::string, float>* globals,
                 float& outValue, std::string& outError)
   {
      // Check for empty or whitespace-only expression first
      size_t pos = 0;
      while (pos < text.size() && isspace((unsigned char)text[pos]))
         pos++;
      if (pos == text.size())
      {
         outError = "empty expression";
         return false;
      }

      Field::BytecodeProgram prog;
      std::string compileError;
      if (!Field::FieldProgramCache::Instance().GetOrCompile(text, siblings, globals, prog, compileError))
      {
         outError = compileError;
         return false;
      }

      Field::FieldVM vm;
      Field::ExecutionEnv env;
      env.t = t;
      env.siblings = siblings;
      env.globals = globals;

      double result = 0.0;
      std::string runtimeError;
      if (!vm.Execute(prog, env, result, runtimeError))
      {
         outError = runtimeError;
         return false;
      }

      outValue = (float)result;
      return true;
   }
}
