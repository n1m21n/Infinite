#include "Transfer.h"
#include "FieldIR.h"
#include <sstream>

namespace Field
{
   const char* TransferKindToString(TransferKind k)
   {
      switch (k)
      {
         case TransferKind::Reduce: return "reduce";
         case TransferKind::Map: return "map";
         case TransferKind::Broadcast: return "broadcast";
         case TransferKind::Resample: return "resample";
         case TransferKind::Downsample: return "downsample";
         case TransferKind::None: return "none";
      }
      return "none";
   }

   TransferKind ParseTransferKind(const std::string& name)
   {
      if (name == "reduce" || name.rfind("reduce.", 0) == 0) return TransferKind::Reduce;
      if (name == "map") return TransferKind::Map;
      if (name == "broadcast") return TransferKind::Broadcast;
      if (name == "resample") return TransferKind::Resample;
      if (name == "downsample") return TransferKind::Downsample;
      return TransferKind::None;
   }

   Domain DomainCoarsen(Domain d)
   {
      switch (d)
      {
         case Domain::Element: return Domain::Frame;
         case Domain::Pixel: return Domain::Frame;
         case Domain::Sample: return Domain::Frame;
         case Domain::Frame: return Domain::Graph;
         case Domain::Graph: return Domain::Graph;
      }
      return Domain::Graph;
   }

   bool DomainFromString(const std::string& str, Domain& outDomain)
   {
      if (str == "graph")   { outDomain = Domain::Graph; return true; }
      if (str == "frame")   { outDomain = Domain::Frame; return true; }
      if (str == "element") { outDomain = Domain::Element; return true; }
      if (str == "pixel")   { outDomain = Domain::Pixel; return true; }
      if (str == "sample")  { outDomain = Domain::Sample; return true; }
      return false;
   }

   const char* DomainToString(Domain d)
   {
      switch (d)
      {
         case Domain::Graph: return "graph";
         case Domain::Frame: return "frame";
         case Domain::Element: return "element";
         case Domain::Pixel: return "pixel";
         case Domain::Sample: return "sample";
      }
      return "unknown";
   }

   FieldError MakeIncomparableDomainError(Domain d1,
                                         SourceSpan span1,
                                         Domain d2,
                                         SourceSpan span2,
                                         const std::string& contextDesc)
   {
      FieldError err;
      err.severity = Severity::Error;
      err.span = span1;

      std::ostringstream msg;
      msg << "incomparable domains: " << DomainToString(d1)
          << " (line " << span1.line << ", col " << span1.col << ") and "
          << DomainToString(d2)
          << " (line " << span2.line << ", col " << span2.col << ")";
      if (!contextDesc.empty())
      {
         msg << " in " << contextDesc;
      }
      err.message = msg.str();

      if ((d1 == Domain::Element && d2 == Domain::Sample) || (d1 == Domain::Sample && d2 == Domain::Element))
      {
         err.hint = "cannot mix element and sample domains directly; wrap in a transfer operator, e.g. reduce.rms(in, 20, 200) or route through frame";
      }
      else if ((d1 == Domain::Element && d2 == Domain::Pixel) || (d1 == Domain::Pixel && d2 == Domain::Element))
      {
         err.hint = "cannot mix element and pixel domains directly; reduce to frame first (e.g. reduce.mean(x)), or render geometry to a texture and sample it";
      }
      else if ((d1 == Domain::Pixel && d2 == Domain::Sample) || (d1 == Domain::Sample && d2 == Domain::Pixel))
      {
         err.hint = "cannot mix pixel and sample domains directly; wrap in a transfer operator or route through frame";
      }
      else
      {
         err.hint = "every crossing between incomparable domains must be explicit; route through frame or use reduce/resample";
      }

      return err;
   }

   bool ValidateReduce(const std::string& reduceOp,
                       size_t argCount,
                       Domain argDomain,
                       SourceSpan span,
                       FieldError& outError)
   {
      outError.Clear();
      outError.span = span;
      outError.severity = Severity::Error;

      if (reduceOp != "sum" && reduceOp != "rms" && reduceOp != "min" &&
          reduceOp != "max" && reduceOp != "mean")
      {
         outError.message = "unknown reduction operator 'reduce." + reduceOp + "' (supported: sum, rms, min, max, mean)";
         return false;
      }

      if (argCount == 3)
      {
         if (reduceOp != "rms")
         {
            outError.message = "reduce." + reduceOp + "() expects 1 argument";
            return false;
         }
         if (argDomain != Domain::Sample)
         {
            outError.message = "3-argument reduce.rms(in, lo, hi) is valid only for sample domain (got " +
                               std::string(DomainToString(argDomain)) + ")";
            outError.hint = "band-limited RMS requires sample-domain audio input";
            return false;
         }
         return true;
      }

      if (argCount != 1)
      {
         outError.message = "reduce." + reduceOp + "() expects 1 argument" +
                            (reduceOp == "rms" ? " (or 3 for band-limited sample reduce: reduce.rms(in, lo, hi))" : "");
         return false;
      }

      if (argDomain == Domain::Graph || argDomain == Domain::Frame)
      {
         outError.message = "cannot reduce value: value is already " + std::string(DomainToString(argDomain)) + "-domain";
         outError.hint = "reduction aggregates fine-domain collections (element, sample) into frame domain";
         return false;
      }

      if (argDomain == Domain::Pixel)
      {
         outError.message = "pixel->frame reductions are not supported in v1 (open design question on GPU->CPU readback latency; hint: render elements to a texture and sample it, or reduce to frame before passing to pixel)";
         outError.hint = "for pixel-to-frame dataflow, use an image analysis node or pass parameters via uniforms";
         return false;
      }

      return true;
   }

   bool ValidateResample(Domain fromDomain,
                         Domain toDomain,
                         SourceSpan span,
                         FieldError& outError)
   {
      outError.Clear();
      outError.span = span;
      outError.severity = Severity::Error;

      if (fromDomain == toDomain)
         return true;

      // Coarse -> Fine is legal
      if (fromDomain == Domain::Graph || fromDomain == Domain::Frame)
         return true;

      // Fine -> Coarse: Sample -> Frame is legal
      if (fromDomain == Domain::Sample && toDomain == Domain::Frame)
         return true;

      // Element/Pixel -> Frame is refused
      if (fromDomain == Domain::Element && toDomain == Domain::Frame)
      {
         outError.message = "resample is not defined from element to frame; use reduce instead (e.g. reduce.mean(x) or reduce.rms(x))";
         outError.hint = "resample takes a single point sample; to aggregate all elements, use reduce";
         return false;
      }
      if (fromDomain == Domain::Pixel && toDomain == Domain::Frame)
      {
         outError.message = "resample is not defined from pixel to frame; use reduce instead";
         outError.hint = "resample takes a single point sample; to aggregate pixels, use a reduction pass";
         return false;
      }

      // Incomparable domains
      outError = MakeIncomparableDomainError(fromDomain, span, toDomain, span, "resample");
      return false;
   }

   bool ValidateDownsample(int k,
                           SourceSpan span,
                           FieldError& outError)
   {
      outError.Clear();
      outError.span = span;
      outError.severity = Severity::Error;

      if (k < 1)
      {
         outError.message = "downsample factor k must be a compile-time constant integer >= 1 (got " + std::to_string(k) + ")";
         return false;
      }
      return true;
   }

   bool ValidateMap(Domain ambientDomain,
                    Domain bodyDomain,
                    SourceSpan span,
                    FieldError& outError)
   {
      outError.Clear();
      outError.span = span;
      outError.severity = Severity::Error;

      if (ambientDomain == Domain::Element && bodyDomain == Domain::Frame)
      {
         outError.message = "mapped body domain must be finer than or equal to surrounding domain; use reduce for many-to-one aggregation";
         return false;
      }
      return true;
   }

   static const std::vector<TransferCostEntry> sTransferCostTable = {
      { Domain::Graph,   Domain::Frame,   TransferKind::Broadcast,  "graph -> frame",   "0",                      "uniform / mailbox slot",                     true },
      { Domain::Graph,   Domain::Element, TransferKind::Broadcast,  "graph -> element", "0",                      "uniform / mailbox slot",                     true },
      { Domain::Graph,   Domain::Pixel,   TransferKind::Broadcast,  "graph -> pixel",   "0",                      "uniform",                                    true },
      { Domain::Graph,   Domain::Sample,  TransferKind::Broadcast,  "graph -> sample",  "0",                      "mailbox slot",                               true },
      { Domain::Frame,   Domain::Element, TransferKind::Broadcast,  "frame -> element", "0",                      "a hoist out of the per-element loop",        true },
      { Domain::Frame,   Domain::Pixel,   TransferKind::Broadcast,  "frame -> pixel",   "0",                      "becomes a uniform",                          true },
      { Domain::Frame,   Domain::Sample,  TransferKind::Broadcast,  "frame -> sample",  "~0",                     "through ParamMailbox::SmoothedValue",        true },
      { Domain::Element, Domain::Frame,   TransferKind::Reduce,     "element -> frame", "O(N) CPU, once/frame",   "N ≈ 5000; executed in prologue",             true },
      { Domain::Sample,  Domain::Frame,   TransferKind::Reduce,     "sample -> frame",  "O(block) audio thread",  "via existing MeterRing",                     true },
      { Domain::Pixel,   Domain::Frame,   TransferKind::Reduce,     "pixel -> frame",   "O(w*h) + GPU-CPU sync",  "forbid in v1 per latency design question",   false },
      { Domain::Element, Domain::Pixel,   TransferKind::None,       "element <-> pixel","—",                      "error; route through frame or render/sample",false },
      { Domain::Element, Domain::Sample,  TransferKind::None,       "element <-> sample","—",                     "error; route through frame",                 false },
      { Domain::Pixel,   Domain::Sample,  TransferKind::None,       "pixel <-> sample", "—",                      "error; route through frame",                 false },
      { Domain::Frame,   Domain::Frame,   TransferKind::Downsample, "downsample(x, k)", "body/k + 1 hold cell",   "saturates near k=32",                        true },
      { Domain::Element, Domain::Element, TransferKind::Downsample, "downsample(x, k)", "body/k + 1 hold cell",   "saturates near k=32",                        true },
      { Domain::Sample,  Domain::Sample,  TransferKind::Downsample, "downsample(x, k)", "body/k + 1 hold cell",   "saturates near k=32",                        true }
   };

   const std::vector<TransferCostEntry>& GetTransferCostTable()
   {
      return sTransferCostTable;
   }

   const TransferCostEntry* FindTransferCost(Domain from, Domain to, TransferKind op)
   {
      for (const auto& entry : sTransferCostTable)
      {
         if (entry.fromDomain == from && entry.toDomain == to && (op == TransferKind::None || entry.opKind == op))
            return &entry;
      }
      return nullptr;
   }

   std::string FormatTransferCostReadout(Domain from, Domain to, TransferKind op, size_t N)
   {
      const auto* entry = FindTransferCost(from, to, op);
      if (!entry) return "unknown cost";

      std::ostringstream ss;
      ss << entry->name << " [" << TransferKindToString(entry->opKind) << "]: " << entry->costFormula;
      if (N > 0 && op == TransferKind::Map)
      {
         ss << " (N = " << N << ")";
      }
      return ss.str();
   }

   BoundaryMode BoundaryModeFromString(const std::string& str)
   {
      if (str == "wrap") return BoundaryMode::Wrap;
      if (str == "border") return BoundaryMode::Border;
      return BoundaryMode::Clamp;
   }

   const char* BoundaryModeToString(BoundaryMode m)
   {
      switch (m)
      {
         case BoundaryMode::Wrap: return "wrap";
         case BoundaryMode::Border: return "border";
         default: return "clamp";
      }
   }
}
