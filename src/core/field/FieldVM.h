#pragma once

#include "FieldBytecode.h"
#include <map>
#include <string>

namespace Field
{
   struct ExecutionEnv
   {
      double t = 0.0;
      const std::map<std::string, float>* siblings = nullptr;
      const std::map<std::string, float>* globals = nullptr;
   };

   class FieldVM
   {
   public:
      FieldVM() = default;

      // Executes bytecode in double precision
      bool Execute(const BytecodeProgram& prog,
                   const ExecutionEnv& env,
                   double& outResult,
                   std::string& outError);
   };
}
