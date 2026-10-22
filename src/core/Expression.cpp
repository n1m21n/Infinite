#include "Expression.h"
#include "field/FieldProgramCache.h"
#include "field/FieldVM.h"

#include <cctype>
#include <cmath>

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

      // pow(-1, 0.5), pow(0, -1) and overflow give NaN/inf without a VM
      // error. Report it like any other runtime error so the caller keeps the
      // param's last good value instead of writing NaN into a kernel.
      if (!std::isfinite((float)result))
      {
         outError = "result is not a finite number";
         return false;
      }
      outValue = (float)result;
      return true;
   }

   bool Check(const std::string& text,
              const std::map<std::string, float>* siblings,
              const std::map<std::string, float>* globals,
              std::string& outError)
   {
      size_t pos = 0;
      while (pos < text.size() && isspace((unsigned char)text[pos]))
         pos++;
      if (pos == text.size())
      {
         outError = "empty expression";
         return false;
      }
      Field::BytecodeProgram prog;
      if (!Field::FieldProgramCache::Instance().GetOrCompile(text, siblings, globals, prog, outError))
         return false;
      // Names resolve at run time (FieldVM OpLoadVar), so the compile accepts any identifier. Resolve them
      // here the same way, without values, so a misspelt name fails the check instead of every frame.
      for (const Field::Instruction& inst : prog.code)
      {
         if (inst.op != Field::Opcode::OpLoadVar)
            continue;
         const std::string& name = inst.stringData;
         if (name == "t" || name == "pi" || (siblings != nullptr && siblings->count(name)) ||
             (globals != nullptr && globals->count(name)))
            continue;
         outError = "unknown identifier '" + name + "'";
         return false;
      }
      return true;
   }
}
