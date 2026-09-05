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
      double dt = 1.0 / 60.0;
      double frame = 0.0;
      // Build steps 22/23: cooks since this node's state bank was cleared.
      double age = 0.0;
      // Step 26 (OPEN-D note history): always 0.0 here - the element domain
      // has no note pipeline (only FieldSynthNode's separate sample-domain
      // kernel does). Kept as real ExecutionEnv fields, defaulted to 0.0,
      // purely so LoadEnvScalar's dispatch mirrors t/dt/frame/age exactly.
      double noteOn = 0.0;
      double notePitch = 0.0;
      double noteVel = 0.0;
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
