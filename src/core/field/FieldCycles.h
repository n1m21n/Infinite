#pragma once

#include "FieldError.h"
#include "FieldIR.h"
#include <string>
#include <vector>

namespace Field
{
   // Performs dataflow cycle legality check on the typed IR.
   // Returns true if all cycles pass through at least one state delay.
   // On failure, populates outError with the exact cycle trace and hint.
   bool CheckDataflowCycles(const std::vector<IRStmtPtr>& stmts,
                            const std::vector<DeclaredState>& states,
                            FieldError& outError);

   // Constant folding pass on IR tree
   IRNodePtr FoldConstants(const IRNodePtr& node);
   void FoldConstantsInStmts(std::vector<IRStmtPtr>& stmts);
}
