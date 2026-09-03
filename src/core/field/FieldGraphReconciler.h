#pragma once

#include "FieldGraphHost.h"
#include "FieldGraphKernel.h"
#include "FieldGraphOwnership.h"
#include "FieldError.h"

#include <string>
#include <vector>

// Field 'graph' domain (build step 10): diffs a GraphPlan against a
// persisted ownership map and drives an IFieldGraphHost to match - a
// React-like reconcile, not a delete-and-respawn (doc §5.3.3). Independently
// unit-testable against a fake host (no ImGui/GL/audio dependency).
namespace Field
{
   enum class ReconcileActionKind
   {
      Mount,
      Update,
      Remount,
      Unmount,
      Connect
   };

   struct ReconcileAction
   {
      ReconcileActionKind kind;
      std::string key;
      std::string typeName;
      int nodeId = -1;
   };

   // Reconciles `plan` against `ownership` (mutated in place: new keys are
   // added, keys no longer wanted are erased, remounted keys get their new
   // index) by calling `host`. `outActions` receives a trace of what
   // happened, in order, for tests/diagnostics. Returns false only on a
   // hard failure (an emit() type name that isn't spawnable); a stale
   // ownership entry is not an error (doc §5.7) - it's silently remounted.
   bool ReconcileGraphPlan(const GraphPlan& plan,
                            GraphOwnershipMap& ownership,
                            IFieldGraphHost& host,
                            std::vector<ReconcileAction>& outActions,
                            FieldError& outError);
}
