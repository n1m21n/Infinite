#include "FieldState.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Field
{
   void FieldState::ClearDeclarations()
   {
      mCells.clear();
   }

   bool FieldState::DeclareCell(const std::string& name,
                                const std::string& typeName,
                                DataType type,
                                int lanes,
                                const std::vector<float>& initValues,
                                Domain domain)
   {
      if (HasCell(name))
         return false;

      StateCell cell;
      cell.name = name;
      cell.typeName = typeName;
      cell.type = type;
      cell.lanes = std::max(1, lanes);
      cell.domain = domain;
      cell.slotOffset = (int)TotalLanes();
      cell.initialValues = initValues;
      if ((int)cell.initialValues.size() < cell.lanes)
      {
         cell.initialValues.resize(cell.lanes, 0.0f);
      }

      mCells.push_back(std::move(cell));
      return true;
   }

   size_t FieldState::TotalLanes() const
   {
      size_t total = 0;
      for (const auto& c : mCells)
      {
         total += (size_t)c.lanes;
      }
      return total;
   }

   bool FieldState::HasCell(const std::string& name) const
   {
      return FindCell(name) != nullptr;
   }

   const StateCell* FieldState::FindCell(const std::string& name) const
   {
      for (const auto& c : mCells)
      {
         if (c.name == name)
            return &c;
      }
      return nullptr;
   }

   int FieldState::GetLaneIndex(const std::string& name, int comp) const
   {
      const auto* cell = FindCell(name);
      if (!cell) return -1;
      if (comp < 0 || comp >= cell->lanes) comp = 0;
      return cell->slotOffset + comp;
   }

   void FieldState::Allocate(Domain domain, size_t elementCount)
   {
      mDomain = domain;
      mElementCount = elementCount;
      size_t totalLanes = TotalLanes();

      if (mFrameValues.size() != totalLanes)
      {
         mFrameValues.resize(totalLanes, 0.0f);
      }

      if (domain == Domain::Element)
      {
         bool sizeChanged = (mElementLanes.size() != totalLanes);
         if (!sizeChanged && !mElementLanes.empty() && mElementLanes[0].size() != elementCount)
         {
            sizeChanged = true;
         }

         if (sizeChanged)
         {
            mElementLanes.resize(totalLanes);
            for (auto& lane : mElementLanes)
            {
               lane.resize(elementCount, 0.0f);
            }
            ResetAll();
         }
      }
      else
      {
         ResetAll();
      }
   }

   void FieldState::ResetAll()
   {
      for (const auto& cell : mCells)
      {
         for (int comp = 0; comp < cell.lanes; ++comp)
         {
            float initVal = (comp < (int)cell.initialValues.size()) ? cell.initialValues[comp] : 0.0f;
            int laneIdx = cell.slotOffset + comp;

            if (laneIdx >= 0 && laneIdx < (int)mFrameValues.size())
            {
               mFrameValues[laneIdx] = initVal;
            }

            if (mDomain == Domain::Element)
            {
               if (laneIdx >= 0 && laneIdx < (int)mElementLanes.size())
               {
                  std::fill(mElementLanes[laneIdx].begin(), mElementLanes[laneIdx].end(), initVal);
               }
            }
         }
      }
   }

   void FieldState::Transplant(const FieldState& oldState)
   {
      Allocate(mDomain, mElementCount);

      for (const auto& cell : mCells)
      {
         const auto* oldCell = oldState.FindCell(cell.name);
         bool matched = (oldCell != nullptr && oldCell->type == cell.type && oldCell->lanes == cell.lanes);

         for (int comp = 0; comp < cell.lanes; ++comp)
         {
            int newLane = cell.slotOffset + comp;
            float initVal = (comp < (int)cell.initialValues.size()) ? cell.initialValues[comp] : 0.0f;

            if (mDomain == Domain::Element)
            {
               if (newLane >= 0 && newLane < (int)mElementLanes.size())
               {
                  if (matched)
                  {
                     int oldLane = oldCell->slotOffset + comp;
                     const auto* src = oldState.GetElementLane((size_t)oldLane);
                     if (src && !src->empty())
                     {
                        size_t copyCount = std::min(src->size(), mElementLanes[newLane].size());
                        std::copy(src->begin(), src->begin() + copyCount, mElementLanes[newLane].begin());
                        if (copyCount < mElementLanes[newLane].size())
                        {
                           std::fill(mElementLanes[newLane].begin() + copyCount, mElementLanes[newLane].end(), initVal);
                        }
                     }
                     else
                     {
                        std::fill(mElementLanes[newLane].begin(), mElementLanes[newLane].end(), initVal);
                     }
                  }
                  else
                  {
                     std::fill(mElementLanes[newLane].begin(), mElementLanes[newLane].end(), initVal);
                  }
               }
            }
            else
            {
               if (newLane >= 0 && newLane < (int)mFrameValues.size())
               {
                  if (matched)
                  {
                     int oldLane = oldCell->slotOffset + comp;
                     const float* srcVal = oldState.GetFrameValue((size_t)oldLane);
                     mFrameValues[newLane] = srcVal ? *srcVal : initVal;
                  }
                  else
                  {
                     mFrameValues[newLane] = initVal;
                  }
               }
            }
         }
      }
   }

   std::vector<float>* FieldState::GetElementLane(size_t laneIdx)
   {
      if (laneIdx < mElementLanes.size())
         return &mElementLanes[laneIdx];
      return nullptr;
   }

   const std::vector<float>* FieldState::GetElementLane(size_t laneIdx) const
   {
      if (laneIdx < mElementLanes.size())
         return &mElementLanes[laneIdx];
      return nullptr;
   }

   float* FieldState::GetFrameValue(size_t laneIdx)
   {
      if (laneIdx < mFrameValues.size())
         return &mFrameValues[laneIdx];
      return nullptr;
   }

   const float* FieldState::GetFrameValue(size_t laneIdx) const
   {
      if (laneIdx < mFrameValues.size())
         return &mFrameValues[laneIdx];
      return nullptr;
   }

   size_t FieldState::CostBytes(Domain domain,
                                size_t cellCount,
                                int elementCount,
                                int width,
                                int height,
                                int voiceCount)
   {
      switch (domain)
      {
         case Domain::Frame:
         case Domain::Graph:
            return cellCount * 4;

         case Domain::Sample:
            return cellCount * (size_t)std::max(1, voiceCount) * 4;

         case Domain::Element:
            return cellCount * (size_t)std::max(1, elementCount) * 4;

         case Domain::Pixel:
            return cellCount * ((size_t)std::max(1, width) * (size_t)std::max(1, height)) * 4;
      }
      return cellCount * 4;
   }

   size_t FieldState::CostBytes(int elementCount,
                                int width,
                                int height,
                                int voiceCount) const
   {
      return CostBytes(mDomain, CellCount(), elementCount, width, height, voiceCount);
   }

   void FieldState::FormatCost(Domain domain,
                               size_t cellCount,
                               int elementCount,
                               int width,
                               int height,
                               int voiceCount,
                               char* outBuf,
                               size_t bufSize)
   {
      if (!outBuf || bufSize == 0) return;
      if (cellCount == 0)
      {
         snprintf(outBuf, bufSize, "state: 0 cells");
         return;
      }

      size_t bytes = CostBytes(domain, cellCount, elementCount, width, height, voiceCount);

      char sizeStr[32];
      if (bytes < 1024)
      {
         snprintf(sizeStr, sizeof(sizeStr), "%zu B", bytes);
      }
      else if (bytes < 1024 * 1024)
      {
         snprintf(sizeStr, sizeof(sizeStr), "%.1f KiB", (double)bytes / 1024.0);
      }
      else
      {
         snprintf(sizeStr, sizeof(sizeStr), "%.1f MiB", (double)bytes / (1024.0 * 1024.0));
      }

      const char* sPlural = (cellCount == 1) ? "" : "s";

      if (domain == Domain::Element)
      {
         snprintf(outBuf, bufSize, "state: %zu cell%s x %d elems = %s",
                  cellCount, sPlural, elementCount, sizeStr);
      }
      else if (domain == Domain::Pixel)
      {
         int px = width * height;
         snprintf(outBuf, bufSize, "state: %zu cell%s x %d px x 2 (ping-pong) = %s",
                  cellCount, sPlural, px, sizeStr);
      }
      else if (domain == Domain::Sample)
      {
         snprintf(outBuf, bufSize, "state: %zu cell%s x %d voices = %s",
                  cellCount, sPlural, voiceCount, sizeStr);
      }
      else
      {
         snprintf(outBuf, bufSize, "state: %zu cell%s = %s",
                  cellCount, sPlural, sizeStr);
      }
   }

   void FieldState::FormatCost(char* outBuf, size_t bufSize, int elementCount, int width, int height, int voiceCount) const
   {
      FormatCost(mDomain, CellCount(), elementCount, width, height, voiceCount, outBuf, bufSize);
   }

   void FieldState::VisitParams(ParamVisitor& v)
   {
      // Frame domain values and types serialized to patch
      for (const auto& cell : mCells)
      {
         std::string typeKey = "stt." + cell.name;
         std::string typeTag = cell.typeName;
         v.Text(typeKey.c_str(), typeTag);

         if (cell.lanes == 1)
         {
            std::string key = "st." + cell.name;
            float val = (mFrameValues.size() > (size_t)cell.slotOffset)
               ? mFrameValues[cell.slotOffset]
               : (cell.initialValues.empty() ? 0.0f : cell.initialValues[0]);
            v.Float(key.c_str(), val);
            if (mFrameValues.size() > (size_t)cell.slotOffset)
            {
               mFrameValues[cell.slotOffset] = val;
            }
         }
         else
         {
            const char* comps[] = { ".x", ".y", ".z", ".w" };
            for (int i = 0; i < cell.lanes && i < 4; ++i)
            {
               std::string key = "st." + cell.name + comps[i];
               size_t lIdx = (size_t)(cell.slotOffset + i);
               float val = (mFrameValues.size() > lIdx)
                  ? mFrameValues[lIdx]
                  : (cell.initialValues.size() > (size_t)i ? cell.initialValues[i] : 0.0f);
               v.Float(key.c_str(), val);
               if (mFrameValues.size() > lIdx)
               {
                  mFrameValues[lIdx] = val;
               }
            }
         }
      }
   }

   void FieldState::RestoreFromMap(const std::unordered_map<std::string, float>& floatMap,
                                   const std::unordered_map<std::string, std::string>& typeMap)
   {
      mSavedFloats = floatMap;
      mSavedTypes = typeMap;
   }
}
