#pragma once

#include "FieldError.h"
#include <string>
#include <vector>

namespace Field
{
   enum class Domain;

   enum class TransferKind
   {
      None,
      Reduce,
      Map,
      Broadcast,
      Resample,
      Downsample
   };

   const char* TransferKindToString(TransferKind k);
   TransferKind ParseTransferKind(const std::string& name);

   // Domain utilities
   Domain DomainCoarsen(Domain d);
   bool DomainFromString(const std::string& str, Domain& outDomain);
   const char* DomainToString(Domain d);

   // Error formatting helper for incomparable domains (names both domains and line:col spans + hint)
   FieldError MakeIncomparableDomainError(Domain d1,
                                         SourceSpan span1,
                                         Domain d2,
                                         SourceSpan span2,
                                         const std::string& contextDesc = "");

   // Transfer operator legality validators
   bool ValidateReduce(const std::string& reduceOp,
                       size_t argCount,
                       Domain argDomain,
                       SourceSpan span,
                       FieldError& outError);

   bool ValidateResample(Domain fromDomain,
                         Domain toDomain,
                         SourceSpan span,
                         FieldError& outError);

   bool ValidateDownsample(int k,
                           SourceSpan span,
                           FieldError& outError);

   bool ValidateMap(Domain ambientDomain,
                    Domain bodyDomain,
                    SourceSpan span,
                    FieldError& outError);

   // Cost Table Entry (§5.6 of Step 8 plan / §7 of field-domains skill)
   struct TransferCostEntry
   {
      Domain fromDomain;
      Domain toDomain;
      TransferKind opKind;
      const char* name;
      const char* costFormula;
      const char* notes;
      bool isLegal;
   };

   // Access the canonical cost table (defined once as static data)
   const std::vector<TransferCostEntry>& GetTransferCostTable();
   const TransferCostEntry* FindTransferCost(Domain from, Domain to, TransferKind op);
   std::string FormatTransferCostReadout(Domain from, Domain to, TransferKind op, size_t N = 0);
}
