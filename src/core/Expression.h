#pragma once

#include <map>
#include <string>

// A small algebraic expression evaluator for inline parameter expressions
// (typed directly into a numeric field, prefixed with '='). Deliberately not
// the FormulaNode machinery: FormulaNode hands its text straight to the GLSL
// compiler for a per-pixel shape function, which is far too heavy to run
// once a frame per parameter. This is a plain recursive-descent parser over
// floats, re-parsed on every evaluation - expressions are short, so the cost
// is negligible next to a shader recompile.
namespace Expression
{
   // Evaluates `text` (without the leading '=') to a float.
   //   - `t` is seconds since the transport started playing (matches
   //     FormulaNode's own time uniform), bound to the identifier `t`.
   //   - `siblings`, if non-null, maps a node's other parameter names to
   //     their current values, so an expression can read e.g. "width * 0.5".
   // Supports + - * / % ^ (with unary minus and right-associative ^), the
   // functions sin cos tan abs min max clamp floor mod lerp, and the
   // constant pi. Returns false and fills outError on a parse or evaluation
   // error (unknown identifier, wrong argument count, division by zero from
   // a malformed expression, etc.); outValue is left untouched in that case.
   bool Evaluate(const std::string& text, double t,
                 const std::map<std::string, float>* siblings,
                 float& outValue, std::string& outError);
}
