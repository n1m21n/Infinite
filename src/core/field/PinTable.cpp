#include "PinTable.h"

#include <sstream>
#include <unordered_map>

namespace Field
{
   const PinEntry* PinTable::Find(const std::string& name) const
   {
      const PinEntry* declaredMatch = nullptr;
      const PinEntry* anyMatch = nullptr;
      for (const auto& p : mPins)
      {
         if (p.name == name)
         {
            anyMatch = &p;
            if (p.isDeclared) declaredMatch = &p;
         }
      }
      return declaredMatch ? declaredMatch : anyMatch;
   }

   PinEntry* PinTable::Find(const std::string& name)
   {
      PinEntry* declaredMatch = nullptr;
      PinEntry* anyMatch = nullptr;
      for (auto& p : mPins)
      {
         if (p.name == name)
         {
            anyMatch = &p;
            if (p.isDeclared) declaredMatch = &p;
         }
      }
      return declaredMatch ? declaredMatch : anyMatch;
   }

   const PinEntry* PinTable::FindById(int id) const
   {
      for (const auto& p : mPins)
      {
         if (p.id == id)
            return &p;
      }
      return nullptr;
   }

   void PinTable::Reconcile(const std::vector<DeclaredPin>& declared, int /*nodeIndex*/, std::string& outNotice)
   {
      outNotice.clear();
      std::vector<std::string> retiredNames;

      std::unordered_map<std::string, const DeclaredPin*> newDeclMap;
      for (const auto& d : declared)
         newDeclMap[d.name] = &d;

      // 1. Any pin currently declared but absent from the new declared list
      //    is retired. Discovery only - see the header comment on Reconcile.
      for (auto& p : mPins)
      {
         if (p.isDeclared && newDeclMap.find(p.name) == newDeclMap.end())
         {
            p.isDeclared = false;
            retiredNames.push_back(p.name);
         }
      }

      // 2. Add, update-in-place, or retire-and-remint each declared pin.
      for (const auto& d : declared)
      {
         PinEntry* existingDeclared = nullptr;
         PinEntry* existingAny = nullptr;
         for (auto& p : mPins)
         {
            if (p.name == d.name)
            {
               existingAny = &p;
               if (p.isDeclared) existingDeclared = &p;
            }
         }
         PinEntry* existing = existingDeclared ? existingDeclared : existingAny;

         if (existing != nullptr &&
             existing->typeName == d.typeName && existing->domainName == d.domainName &&
             existing->isOutput == d.isOutput)
         {
            // Same identity, same shape - reactivate in place, same id
            // preserved. This covers both "still declared, no change" and
            // "was retired by an earlier Reconcile, now redeclared with an
            // unchanged shape": ParamTable's append-only identity scheme
            // (S4) reuses the id in both cases - only an actual shape
            // change (below) mints a fresh one.
            existing->isDeclared = true;
            continue;
         }

         // Either no prior entry, or the prior entry's shape changed
         // (S5.4: type/domain/direction change retires the old identity
         // and mints a fresh one - never reuse an id across a shape change).
         if (existing != nullptr && existing->isDeclared)
         {
            existing->isDeclared = false;
            retiredNames.push_back(existing->name);
         }

         int id = mNextPinId++;
         mPinIdByName[d.name] = id;

         PinEntry entry;
         entry.id = id;
         entry.name = d.name;
         entry.typeName = d.typeName;
         entry.domainName = d.domainName;
         entry.isOutput = d.isOutput;
         entry.isDeclared = true;
         mPins.push_back(entry);
      }

      if (!retiredNames.empty())
      {
         outNotice = "Retired pin";
         if (retiredNames.size() > 1) outNotice += "s";
         outNotice += ": ";
         for (size_t i = 0; i < retiredNames.size(); ++i)
         {
            if (i > 0) outNotice += ", ";
            outNotice += "'" + retiredNames[i] + "'";
         }
      }
   }

   std::string PinTable::SerializePinMap() const
   {
      std::string s;
      bool first = true;
      for (const auto& kv : mPinIdByName)
      {
         if (!first) s += ",";
         first = false;
         s += kv.first + ":" + std::to_string(kv.second);
      }
      return s;
   }

   void PinTable::DeserializePinMap(const std::string& str)
   {
      mPinIdByName.clear();
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
               mPinIdByName[name] = id;
               if (id >= mNextPinId)
                  mNextPinId = id + 1;
               if (!Find(name))
               {
                  PinEntry pe;
                  pe.id = id;
                  pe.name = name;
                  pe.isDeclared = false;
                  mPins.push_back(pe);
               }
            }
         }
      }
   }
}
