#pragma once

#include "FieldError.h"
#include "FieldIR.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Field 'graph' domain (build step 10): interprets a compiled GraphIRProgram
// once, at edit time, into a deterministic GraphPlan - a description of
// which node types should exist, how they should be wired, what their
// params should be set to, and where they should sit. This file contains no
// GraphNode/ImGui/gNodes reference; see FieldGraphHost.h for the seam that
// turns a GraphPlan into real graph mutations, and FieldGraphReconciler.h
// for the diff that decides what to mount/update/unmount.
namespace Field
{
   struct EmitSpec
   {
      // Identity key (doc §5.3.1): "<lvalueName>#<k0>.<k1>...". Stable
      // across regenerations as long as the same emit() call site produces
      // the same key arguments - independent of node index or emit order.
      std::string key;
      std::string typeName; // e.g. "Oscillator" - must name a spawnable node type
      SourceSpan span;
   };

   struct ConnectSpec
   {
      std::string srcKey;
      int srcSlot = 0;
      std::string dstKey;
      int dstSlot = 0;
      SourceSpan span;
   };

   struct SetSpec
   {
      std::string targetKey;
      std::string paramName;
      float value = 0.0f;
      // Build step 15 §4.2: non-empty iff this set()'s value expression is
      // exactly a bare Variable node naming one of the program's declared
      // params - i.e. a direct, unconditional pass-through
      // (`set(osc, "rate", amount)`), not a literal or a computed expression
      // (`set(osc, "rate", amount * 2)`). Empty for every other case; the
      // `value` field above is still baked exactly as before in every case,
      // this is additive provenance only, consumed by
      // FieldGraphNode::RebuildLiveForward to build the live-forwarding
      // fast path (main.cpp's PushLiveParams call, no Regenerate() needed).
      std::string sourceParamName;
      SourceSpan span;
   };

   struct PlaceSpec
   {
      std::string targetKey;
      float x = 0.0f;
      float y = 0.0f;
      SourceSpan span;
   };

   // Pure data, deterministic given (program, params, globals). No INode*,
   // no node index - see FieldGraphReconciler for how this becomes actions
   // against real nodes.
   struct GraphPlan
   {
        std::vector<EmitSpec> emits;
        std::vector<ConnectSpec> connects;
        std::vector<SetSpec> sets;
        std::vector<PlaceSpec> places;
        bool valid = false;
   };

   // Interprets `program` with the given param values (name -> value, as
   // already reconciled by the caller's Field::ParamTable) and graph-domain
   // globals (name -> value; see the globals bridge) into a GraphPlan.
   // Returns false and fills outError on interpretation failure (e.g. a
   // duplicate emit key, an emit() type name that reduces to an empty
   // string, or a loop that exceeds the safety iteration cap).
   bool InterpretGraphProgram(const GraphIRProgram& program,
                               const std::map<std::string, float>& params,
                               const std::map<std::string, float>& globals,
                               GraphPlan& outPlan,
                               FieldError& outError);
}
