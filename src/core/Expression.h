#pragma once

#include <map>
#include <string>

// A small algebraic expression evaluator for inline parameter expressions
// (typed directly into a numeric field, prefixed with '='). Powered by the
// Field pipeline (lexer -> AST -> typed IR -> register bytecode VM) with an
// LRU compiled program cache for low per-frame evaluation overhead.
namespace Expression
{
   // Evaluates `text` (without the leading '=') to a float.
   //   - `globals`, if non-null, maps patch-wide named values (see
   //     core/ExprGlobals.h) to their current values. Looked up *after*
   //     `siblings`, so a node's own parameter always wins over a global of
   //     the same name - the local meaning is the one visible where the
   //     expression is typed.
   //   - `t` is seconds since the transport started playing (matches
   //     FormulaNode's own time uniform), bound to the identifier `t`.
   //   - `siblings`, if non-null, maps a node's other parameter names to
   //     their current values, so an expression can read e.g. "width * 0.5".
   //     The caller also binds `lo` and `hi` to the target parameter's own
   //     minimum and maximum, so an expression can be written in normalised
   //     terms - "lerp(lo, hi, sin(t) * 0.5 + 0.5)" sweeps the full range the
   //     way a patched modulator does, rather than writing raw units that
   //     clamp to nothing on a parameter measured in milliseconds.
   // Supports + - * / % ^ (with unary minus and right-associative ^), the
   // comparisons < <= > >= == !=, the logical operators && || !, the
   // functions sin cos tan abs min max clamp floor ceil round mod lerp mix sqrt
   // exp log pow sign step smoothstep rand noise sh (with optional seed 4th arg) if,
   // the vector constructors vec2 vec3 vec4, component swizzles (.xy, .rgb, etc.),
   // and the constant pi. Returns false
   // and fills outError on a parse or evaluation error (unknown identifier,
   // wrong argument count, division by zero from a malformed expression,
   // etc.); outValue is left untouched in that case.
   //
   // Comparisons and logical operators yield 1 or 0 and treat any non-zero
   // input as true, so a condition composes with arithmetic directly -
   // "lerp(lo, hi, x > 0.5)" is a valid gate and needs no separate boolean
   // type. `if(cond, a, b)` evaluates both branches (there is no short
   // circuit); with no side effects in the language, the only cost is the
   // arithmetic, and it keeps the parser a plain expression grammar.
   bool Evaluate(const std::string& text, double t,
                 const std::map<std::string, float>* siblings,
                 const std::map<std::string, float>* globals,
                 float& outValue, std::string& outError);
}
