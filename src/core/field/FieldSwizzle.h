#pragma once

#include "FieldTypes.h"
#include <cstdint>
#include <string>

namespace Field
{
   struct SwizzleInfo
   {
      FieldType resultType;
      int numComponents = 0;
      uint8_t indices[4] = { 0, 0, 0, 0 };
   };

   // Parses and validates swizzles like ".xy", ".rgb", ".xxxx", etc.
   // Base must be a vector (vec2, vec3, vec4).
   // Components must come exclusively from {'x','y','z','w'} OR {'r','g','b','a'}.
   // Components must be within range of baseType lanes (e.g. .w is invalid on vec3).
   bool ParseAndValidateSwizzle(const std::string& swizzleStr,
                                FieldType baseType,
                                const std::string& baseNameForError,
                                SwizzleInfo& outInfo,
                                std::string& outError);

   // Helper for step 4 (lvalue swizzles): duplicate components on lvalues are forbidden (e.g. P.xx = ...).
   bool HasDuplicateSwizzleComponents(const std::string& swizzleStr);
}
