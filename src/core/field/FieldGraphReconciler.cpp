#include "FieldGraphReconciler.h"

#include <set>
#include <unordered_map>

namespace Field
{
   bool ReconcileGraphPlan(const GraphPlan& plan,
                            GraphOwnershipMap& ownership,
                            IFieldGraphHost& host,
                            std::vector<ReconcileAction>& outActions,
                            FieldError& outError)
   {
      outError.Clear();
      outActions.clear();

      if (!plan.valid)
      {
         outError.severity = Severity::Error;
         outError.message = "cannot reconcile an invalid GraphPlan";
         return false;
      }

      std::set<std::string> wanted;
      for (const auto& e : plan.emits) wanted.insert(e.key);

      // Mount / Update / Remount pass, in plan (source) order - keeps
      // reconcile output deterministic given a deterministic plan.
      std::unordered_map<std::string, int> liveIndex; // key -> current node id, post this pass
      for (const auto& e : plan.emits)
      {
         if (!host.Spawnable(e.typeName))
         {
            outError.severity = Severity::Error;
            outError.span = e.span;
            outError.message = "emit(\"" + e.typeName + "\", ...) does not name a spawnable node type";
            return false;
         }

         int existing = ownership.Has(e.key) ? ownership.Get(e.key) : -1;
         bool alive = existing >= 0 && host.Alive(existing);

         if (alive && host.TypeNameOf(existing) == e.typeName)
         {
            // Same identity, same type: leave the node mounted as-is (its
            // params/wiring are applied by the Set/Connect passes below).
            liveIndex[e.key] = existing;
            outActions.push_back({ ReconcileActionKind::Update, e.key, e.typeName, existing });
            continue;
         }

         if (alive)
         {
            // Same identity, different type: doc §5.3.3 REMOUNT
            int fresh = host.Remount(existing, e.typeName);
            if (fresh < 0)
            {
               outError.severity = Severity::Error;
               outError.span = e.span;
               outError.message = "failed to remount node of type '" + e.typeName + "'";
               return false;
            }
            ownership.Set(e.key, fresh);
            liveIndex[e.key] = fresh;
            outActions.push_back({ ReconcileActionKind::Remount, e.key, e.typeName, fresh });
            continue;
         }

         int fresh = host.Mount(e.typeName);
         if (fresh < 0)
         {
            outError.severity = Severity::Error;
            outError.span = e.span;
            outError.message = "failed to mount node of type '" + e.typeName + "'";
            return false;
         }
         ownership.Set(e.key, fresh);
         liveIndex[e.key] = fresh;
         outActions.push_back({ ReconcileActionKind::Mount, e.key, e.typeName, fresh });
      }

      // Unmount pass: anything owned but no longer wanted, per doc §5.3.3
      // ("unmount after mount/update" so a key changing target type never
      // transiently drops to zero live nodes for that key mid-reconcile).
      std::vector<std::string> toErase;
      for (const auto& kv : ownership.Entries())
      {
         if (wanted.find(kv.first) != wanted.end()) continue;
         if (host.Alive(kv.second))
         {
            host.Unmount(kv.second);
            outActions.push_back({ ReconcileActionKind::Unmount, kv.first, "", kv.second });
         }
         toErase.push_back(kv.first);
      }
      for (const auto& k : toErase) ownership.Erase(k);

      // Param/place passes - order doesn't matter between them, both must
      // run before Connect (a freshly-mounted node's slots may depend on
      // params already being set, e.g. a mixer's channel count).
      for (const auto& s : plan.sets)
      {
         auto it = liveIndex.find(s.targetKey);
         if (it == liveIndex.end())
         {
            outError.severity = Severity::Error;
            outError.span = s.span;
            outError.message = "set() target handle does not resolve to a mounted node";
            return false;
         }
         host.SetParam(it->second, s.paramName, s.value);
      }
      for (const auto& p : plan.places)
      {
         auto it = liveIndex.find(p.targetKey);
         if (it == liveIndex.end())
         {
            outError.severity = Severity::Error;
            outError.span = p.span;
            outError.message = "place() target handle does not resolve to a mounted node";
            return false;
         }
         host.Place(it->second, p.x, p.y);
      }

      // Reconnect pass last (doc §5.3.3): all nodes for this pass exist by
      // now, so every connect() can resolve both endpoints.
      for (const auto& c : plan.connects)
      {
         auto srcIt = liveIndex.find(c.srcKey);
         auto dstIt = liveIndex.find(c.dstKey);
         if (srcIt == liveIndex.end() || dstIt == liveIndex.end())
         {
            outError.severity = Severity::Error;
            outError.span = c.span;
            outError.message = "connect() endpoint handle does not resolve to a mounted node";
            return false;
         }
         host.Connect(srcIt->second, c.srcSlot, dstIt->second, c.dstSlot);
         outActions.push_back({ ReconcileActionKind::Connect, c.srcKey + "->" + c.dstKey, "", -1 });
      }

      return true;
   }
}
