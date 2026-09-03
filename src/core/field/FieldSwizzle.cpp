#include "FieldSwizzle.h"
#include <unordered_set>

namespace Field
{
   bool HasDuplicateSwizzleComponents(const std::string& swizzleStr)
   {
      std::unordered_set<char> seen;
      for (char c : swizzleStr)
      {
         if (seen.find(c) != seen.end())
            return true;
         seen.insert(c);
      }
      return false;
   }

   bool ParseAndValidateSwizzle(const std::string& swizzleStr,
                                FieldType baseType,
                                const std::string& baseNameForError,
                                SwizzleInfo& outInfo,
                                std::string& outError)
   {
      outError.clear();
      outInfo = SwizzleInfo{};

      // Base must be a vector type
      if (baseType.lanes <= 1)
      {
         std::string name = baseNameForError.empty() ? "expression" : ("'" + baseNameForError + "'");
         outError = name + " is a float; '." + swizzleStr + "' needs a vec2, vec3 or vec4";
         return false;
      }

      if (swizzleStr.empty() || swizzleStr.size() > 4)
      {
         outError = "swizzle length must be between 1 and 4 components (got '" + swizzleStr + "')";
         return false;
      }

      bool hasXyzw = false;
      bool hasRgba = false;

      outInfo.numComponents = (int)swizzleStr.size();
      outInfo.resultType = FieldType(FieldType::FromLanes(outInfo.numComponents), outInfo.numComponents);

      for (size_t i = 0; i < swizzleStr.size(); ++i)
      {
         char c = swizzleStr[i];
         int compIdx = -1;

         if (c == 'x') { compIdx = 0; hasXyzw = true; }
         else if (c == 'y') { compIdx = 1; hasXyzw = true; }
         else if (c == 'z') { compIdx = 2; hasXyzw = true; }
         else if (c == 'w') { compIdx = 3; hasXyzw = true; }
         else if (c == 'r') { compIdx = 0; hasRgba = true; }
         else if (c == 'g') { compIdx = 1; hasRgba = true; }
         else if (c == 'b') { compIdx = 2; hasRgba = true; }
         else if (c == 'a') { compIdx = 3; hasRgba = true; }
         else
         {
            outError = "invalid swizzle component '" + std::string(1, c) + "'";
            return false;
         }

         if (hasXyzw && hasRgba)
         {
            outError = "mixed swizzle sets: '." + swizzleStr + "' mixes xyzw and rgba components";
            return false;
         }

         if (compIdx >= baseType.lanes)
         {
            outError = "component '" + std::string(1, c) + "' is out of range for " +
                       std::string(FieldType::ToString(baseType.kind)) + " (arity " +
                       std::to_string(baseType.lanes) + ")";
            return false;
         }

         outInfo.indices[i] = (uint8_t)compIdx;
      }

      return true;
   }
}
