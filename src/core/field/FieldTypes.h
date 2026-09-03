#pragma once

#include <string>
#include <vector>

namespace Field
{
   // Field DataType lattice:
   //                  ┌─ vec2 ─┐
   // int  ──▶  float ─┼─ vec3 ─┼──  (broadcast, scalar → vector only)
   //                  └─ vec4 ─┘
   //
   // NOTE on 'int' (§5.1):
   // Numeric literals stay 'float' in the frame domain, full stop.
   // 'int' exists for loop counters, indices and mode selectors (steps 4-5).
   // 'int' is reserved here so the lattice is complete, and remains unreachable
   // from step 3's surface syntax.
   //
   // NOTE on 'bool' / 'bvec' (§5.6 Decision 1):
   // bool is the result of a comparison or logical operation.
   // There is no bvec2/3/4 in Field v1; vector comparisons are refused at compile time.
   enum class DataType
   {
      Void,
      Float,
      Int,
      Bool,
      Vec2,
      Vec3,
      Vec4
   };

   struct FieldType
   {
      DataType kind = DataType::Float;
      int lanes = 1;

      FieldType() = default;
      FieldType(DataType k, int l) : kind(k), lanes(l) {}
      explicit FieldType(DataType k) : kind(k), lanes(GetLanesForType(k)) {}

      bool operator==(const FieldType& other) const
      {
         return kind == other.kind && lanes == other.lanes;
      }

      bool operator!=(const FieldType& other) const
      {
         return !(*this == other);
      }

      bool operator==(DataType dt) const
      {
         return kind == dt;
      }

      bool operator!=(DataType dt) const
      {
         return kind != dt;
      }

      static int GetLanesForType(DataType t)
      {
         switch (t)
         {
            case DataType::Float: return 1;
            case DataType::Int: return 1;
            case DataType::Bool: return 1;
            case DataType::Vec2: return 2;
            case DataType::Vec3: return 3;
            case DataType::Vec4: return 4;
            default: return 0;
         }
      }

      static DataType FromLanes(int lanes)
      {
         switch (lanes)
         {
            case 1: return DataType::Float;
            case 2: return DataType::Vec2;
            case 3: return DataType::Vec3;
            case 4: return DataType::Vec4;
            default: return DataType::Void;
         }
      }

      static const char* ToString(DataType t)
      {
         switch (t)
         {
            case DataType::Float: return "float";
            case DataType::Int: return "int";
            case DataType::Bool: return "bool";
            case DataType::Vec2: return "vec2";
            case DataType::Vec3: return "vec3";
            case DataType::Vec4: return "vec4";
            case DataType::Void: return "void";
            default: return "unknown";
         }
      }

      const char* ToString() const
      {
         return ToString(kind);
      }
   };

   // Rank promotion for binary arithmetic operators (+, -, *, /, %, ^):
   // - Scalar (float/int) + Vector (vecN) -> Vector (vecN) (scalar broadcast)
   // - Vector (vecN) + Scalar (float/int) -> Vector (vecN) (scalar broadcast)
   // - Vector (vecN) + Vector (vecN) -> Vector (vecN)
   // - Vector (vecN) + Vector (vecM) where N != M -> ERROR (no truncation, no zero-fill)
   inline bool JoinRank(FieldType lhs, FieldType rhs, FieldType& outResult, std::string& outError)
   {
      outError.clear();

      int lhsLanes = lhs.lanes;
      int rhsLanes = rhs.lanes;

      if (lhsLanes == 1 && rhsLanes == 1)
      {
         outResult = FieldType(DataType::Float, 1);
         return true;
      }
      if (lhsLanes == 1 && rhsLanes > 1)
      {
         outResult = FieldType(FieldType::FromLanes(rhsLanes), rhsLanes);
         return true;
      }
      if (lhsLanes > 1 && rhsLanes == 1)
      {
         outResult = FieldType(FieldType::FromLanes(lhsLanes), lhsLanes);
         return true;
      }
      if (lhsLanes == rhsLanes)
      {
         outResult = FieldType(FieldType::FromLanes(lhsLanes), lhsLanes);
         return true;
      }

      // Incomparable vector ranks
      outError = "cannot combine " + std::string(FieldType::ToString(lhs.kind)) +
                 " and " + std::string(FieldType::ToString(rhs.kind)) +
                 " (hint: broadcast goes scalar to vector only; there is no rank-narrowing rule)";
      return false;
   }
}
