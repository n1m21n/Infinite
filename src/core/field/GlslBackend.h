#pragma once

#include "FieldIR.h"
#include <string>
#include <vector>

namespace Field
{
   struct UniformSlot
   {
      std::string name;       // e.g. "fld_h0", "fld_p_speed"
      std::string varName;    // original name if param or hoisted expr
      FieldType type;
      Domain domain = Domain::Frame;
      int hoistedIndex = -1;  // index into hoisted prologue statements
      int paramIndex = -1;    // index in ParamTable
      int location = -1;      // GL uniform location
   };

   struct StateSlot
   {
      std::string name;
      DataType type = DataType::Float;
      int lanes = 1;
      int bankIndex = 0;      // bank 0 in v1
      int channel = 0;        // 0=r, 1=g, 2=b, 3=a
      std::vector<float> initialValues;
   };

   struct GlslEmitResult
   {
      std::string source;
      std::vector<UniformSlot> uniforms;
      std::vector<StateSlot> state;
      std::string error;
      int branchCount = 0;
      std::vector<int> lineToIrNode;
   };

   GlslEmitResult EmitGlsl(const PixelIRProgram& program);
}
