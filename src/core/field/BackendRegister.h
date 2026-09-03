#pragma once

#include "FieldError.h"
#include "SampleProgram.h"
#include <string>

// Main-thread-only compiler for the Field 'sample' domain: lex -> parse ->
// direct AST-to-register-bytecode emission (no shared typed-IR pass with the
// Element/Pixel backends - see BackendRegister.cpp's header comment for why).
// Never reachable from ProcessBlock or any other audio-thread path.
namespace Field
{
   // `previous` (may be null) is the currently-active compiled program, used
   // only to resolve (name,type) state-cell transplant indices (§5.9) - its
   // bytecode is never reused. On failure, outProgram is left default
   // (valid = false) and outError describes the first error encountered.
   bool CompileSampleProgram(const std::string& code,
                              const SampleProgram* previous,
                              SampleProgram& outProgram,
                              FieldError& outError);
}
