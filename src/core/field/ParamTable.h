#pragma once

#include "INode.h"
#include <map>
#include <string>
#include <vector>

namespace Field
{
   struct DeclaredParam
   {
      std::string name;
      std::string typeName = "float";
      double defaultValue = 0.0;
      double minValue = 0.0;
      double maxValue = 1.0;
   };

   struct ParamEntry
   {
      int id = 0;
      std::string name;
      float value = 0.0f;
      float defaultValue = 0.0f;
      float minValue = 0.0f;
      float maxValue = 1.0f;
      bool isDeclared = false;
   };

   class ParamTable
   {
   public:
      ParamTable() = default;

      void Reconcile(const std::vector<DeclaredParam>& declared, int nodeIndex, std::string& outNotice);

      void VisitParams(ParamVisitor& v);

      const std::vector<ParamEntry>& Params() const { return mParams; }
      std::vector<ParamEntry>& Params() { return mParams; }

      const ParamEntry* Find(const std::string& name) const;
      ParamEntry* Find(const std::string& name);
      const ParamEntry* FindById(int id) const;

      int NextParamId() const { return mNextParamId; }
      void SetNextParamId(int nextId) { mNextParamId = nextId; }

      std::string SerializeParamMap() const;
      void DeserializeParamMap(const std::string& str);

      // Get map of param values for execution
      std::map<std::string, float> ValueMap() const;

   private:
      std::vector<ParamEntry> mParams;
      std::map<std::string, int> mParamIdByName;
      int mNextParamId = 1;
   };
}
