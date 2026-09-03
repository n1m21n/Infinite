#pragma once

#include "FieldTypes.h"
#include "FieldIR.h"
#include "INode.h"
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

namespace Field
{
   struct StateCell
   {
      std::string name;
      std::string typeName;
      DataType type = DataType::Float;
      int lanes = 1;
      std::vector<float> initialValues; // size == lanes
      Domain domain = Domain::Element;
      int slotOffset = 0; // overall lane offset
   };

   class FieldState
   {
   public:
      FieldState() = default;

      void ClearDeclarations();
      bool DeclareCell(const std::string& name,
                       const std::string& typeName,
                       DataType type,
                       int lanes,
                       const std::vector<float>& initValues,
                       Domain domain);

      const std::vector<StateCell>& Cells() const { return mCells; }
      size_t DeclaredCount() const { return mCells.size(); }
      size_t TotalLanes() const;
      size_t CellCount() const { return TotalLanes(); }

      bool HasCell(const std::string& name) const;
      const StateCell* FindCell(const std::string& name) const;
      int GetLaneIndex(const std::string& name, int comp = 0) const;

      // Compile-time / resolution-change allocation
      void Allocate(Domain domain, size_t elementCount);

      // Real-time safe reset - zero heap allocations
      void ResetAll();

      // Hot reload transplant - transfers values for matching (name, type)
      void Transplant(const FieldState& oldState);

      // Storage access
      std::vector<float>* GetElementLane(size_t laneIdx);
      const std::vector<float>* GetElementLane(size_t laneIdx) const;
      float* GetFrameValue(size_t laneIdx);
      const float* GetFrameValue(size_t laneIdx) const;

      size_t ElementStorageCount() const { return mElementCount; }

      // Cost calculation - single source of truth for arithmetic (§5.6)
      static size_t CostBytes(Domain domain,
                              size_t cellCount,
                              int elementCount = 5000,
                              int width = 1920,
                              int height = 1080,
                              int voiceCount = 8);

      size_t CostBytes(int elementCount = 5000,
                       int width = 1920,
                       int height = 1080,
                       int voiceCount = 8) const;

      static void FormatCost(Domain domain,
                             size_t cellCount,
                             int elementCount,
                             int width,
                             int height,
                             int voiceCount,
                             char* outBuf,
                             size_t bufSize);

      void FormatCost(char* outBuf, size_t bufSize, int elementCount, int width = 1920, int height = 1080, int voiceCount = 8) const;

      // Serialization into patch
      void VisitParams(ParamVisitor& v);
      void RestoreFromMap(const std::unordered_map<std::string, float>& floatMap,
                          const std::unordered_map<std::string, std::string>& typeMap);

   private:
      std::vector<StateCell> mCells;
      Domain mDomain = Domain::Element;
      size_t mElementCount = 0;

      // Parallel storage lanes for element domain
      std::vector<std::vector<float>> mElementLanes;

      // Storage for frame domain
      std::vector<float> mFrameValues;

      // Stored side map from VisitParams load before compilation
      std::unordered_map<std::string, float> mSavedFloats;
      std::unordered_map<std::string, std::string> mSavedTypes;
   };
}
