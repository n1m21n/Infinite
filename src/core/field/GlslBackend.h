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

      // Build step 22 (OPEN-C): how many offset reads of a state cell the
      // kernel performs, and whether it performs any at all. A kernel that
      // reads its neighbours is a simulation - it integrates for minutes, so
      // its cells need 32-bit storage, where a plain trails kernel does not.
      // The count is the number the node face shows next to the byte count,
      // because at 1080p fetch bandwidth is the ceiling, not ALU.
      int offsetReadCount = 0;
      bool usesOffsetReads = false;
   };

   GlslEmitResult EmitGlsl(const PixelIRProgram& program);
}
