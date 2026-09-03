#include "ParamTable.h"
#include "Modulation.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace Field
{
   const ParamEntry* ParamTable::Find(const std::string& name) const
   {
      for (const auto& p : mParams)
      {
         if (p.name == name)
            return &p;
      }
      return nullptr;
   }

   ParamEntry* ParamTable::Find(const std::string& name)
   {
      for (auto& p : mParams)
      {
         if (p.name == name)
            return &p;
      }
      return nullptr;
   }

   const ParamEntry* ParamTable::FindById(int id) const
   {
      for (const auto& p : mParams)
      {
         if (p.id == id)
            return &p;
      }
      return nullptr;
   }

   void ParamTable::Reconcile(const std::vector<DeclaredParam>& declared, int nodeIndex, std::string& outNotice)
   {
      outNotice.clear();
      std::vector<std::string> droppedModNames;

      std::unordered_map<std::string, const DeclaredParam*> newDeclMap;
      for (const auto& d : declared)
      {
         newDeclMap[d.name] = &d;
      }

      // 1. Mark undeclared params and clean up modulation/expression bindings
      for (auto& p : mParams)
      {
         if (p.isDeclared && newDeclMap.find(p.name) == newDeclMap.end())
         {
            p.isDeclared = false;
            if (nodeIndex >= 0)
            {
               if (Modulation::Instance().IsModulated(nodeIndex, p.id))
               {
                  Modulation::Instance().Unbind(nodeIndex, p.id);
                  droppedModNames.push_back(p.name);
               }
               if (Modulation::Instance().HasExpression(nodeIndex, p.id))
               {
                  Modulation::Instance().ClearExpression(nodeIndex, p.id);
               }
            }
         }
      }

      if (!droppedModNames.empty())
      {
         outNotice = "Dropped modulation on deleted/renamed param";
         if (droppedModNames.size() == 1)
         {
            outNotice += " '" + droppedModNames[0] + "'";
         }
         else
         {
            outNotice += "s: ";
            for (size_t i = 0; i < droppedModNames.size(); ++i)
            {
               if (i > 0) outNotice += ", ";
               outNotice += "'" + droppedModNames[i] + "'";
            }
         }
      }

      // 2. Add or update declared params
      for (const auto& d : declared)
      {
         ParamEntry* existing = Find(d.name);
         if (existing != nullptr)
         {
            existing->isDeclared = true;
            existing->defaultValue = (float)d.defaultValue;
            existing->minValue = (float)d.minValue;
            existing->maxValue = (float)d.maxValue;

            // Clamp modulation range if modulated
            if (nodeIndex >= 0 && Modulation::Instance().IsModulated(nodeIndex, existing->id))
            {
               Modulation::Source src = Modulation::Instance().ModulatorFor(nodeIndex, existing->id);
               if (src.hasRange)
               {
                  float clampedLo = std::max(existing->minValue, std::min(existing->maxValue, src.lo));
                  float clampedHi = std::max(existing->minValue, std::min(existing->maxValue, src.hi));
                  if (clampedLo != src.lo || clampedHi != src.hi)
                  {
                     Modulation::Instance().SetRange(nodeIndex, existing->id, clampedLo, clampedHi);
                  }
               }
            }
         }
         else
         {
            int id = 0;
            auto it = mParamIdByName.find(d.name);
            if (it != mParamIdByName.end())
            {
               id = it->second;
            }
            else
            {
               id = mNextParamId++;
               mParamIdByName[d.name] = id;
            }
            if (id >= mNextParamId)
            {
               mNextParamId = id + 1;
            }

            ParamEntry entry;
            entry.id = id;
            entry.name = d.name;
            entry.value = (float)d.defaultValue;
            entry.defaultValue = (float)d.defaultValue;
            entry.minValue = (float)d.minValue;
            entry.maxValue = (float)d.maxValue;
            entry.isDeclared = true;
            mParams.push_back(entry);
         }
      }
   }

   std::string ParamTable::SerializeParamMap() const
   {
      std::string s;
      bool first = true;
      for (const auto& kv : mParamIdByName)
      {
         if (!first) s += ",";
         first = false;
         s += kv.first + ":" + std::to_string(kv.second);
      }
      return s;
   }

   void ParamTable::DeserializeParamMap(const std::string& str)
   {
      mParamIdByName.clear();
      if (str.empty()) return;
      std::istringstream ss(str);
      std::string token;
      while (std::getline(ss, token, ','))
      {
         size_t colon = token.find(':');
         if (colon != std::string::npos)
         {
            std::string name = token.substr(0, colon);
            int id = std::atoi(token.substr(colon + 1).c_str());
            if (!name.empty() && id > 0)
            {
               mParamIdByName[name] = id;
               if (id >= mNextParamId)
                  mNextParamId = id + 1;
               if (!Find(name))
               {
                  ParamEntry pe;
                  pe.id = id;
                  pe.name = name;
                  pe.isDeclared = false;
                  mParams.push_back(pe);
               }
            }
         }
      }
   }

   void ParamTable::VisitParams(ParamVisitor& v)
   {
      v.Int("field_nextParamId", mNextParamId);
      std::string mapStr = SerializeParamMap();
      v.Text("field_paramMap", mapStr);
      if (!mapStr.empty())
      {
         DeserializeParamMap(mapStr);
      }

      for (auto& p : mParams)
      {
         std::string key = "p." + p.name;
         v.Float(key.c_str(), p.value);
      }
   }

   std::map<std::string, float> ParamTable::ValueMap() const
   {
      std::map<std::string, float> res;
      for (const auto& p : mParams)
      {
         if (p.isDeclared)
         {
            res[p.name] = p.value;
         }
      }
      return res;
   }
}
