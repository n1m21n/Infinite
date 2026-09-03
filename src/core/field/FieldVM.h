#pragma once

#include "FieldBytecode.h"
#include <map>
#include <string>

namespace Field
{
   class FieldState;

   struct ExecutionEnv
   {
      double t = 0.0;
      const std::map<std::string, float>* params = nullptr;
      const std::map<std::string, float>* siblings = nullptr;
      const std::map<std::string, float>* globals = nullptr;
      FieldState* state = nullptr;
   };

   struct VectorResult
   {
      double v[4] = { 0.0, 0.0, 0.0, 0.0 };
      int lanes = 1;
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

      bool ExecuteVector(const BytecodeProgram& prog,
                         const ExecutionEnv& env,
                         VectorResult& outResult,
                         std::string& outError);
   };
}
