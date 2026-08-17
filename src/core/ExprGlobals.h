#pragma once

#include <map>
#include <string>
#include <vector>

// Patch-wide named values that every inline parameter expression can read.
//
// The problem they solve: an expression typed into a parameter can already
// reach `t`, its own siblings, and its own range - but nothing outside its
// node. So a patch built around one idea ("everything follows this tempo",
// "hold this section down while the intro plays") has to spell that idea out
// again inside every parameter that depends on it, and changing it means
// editing each one. A global is that idea written once, by name.
//
// A global is itself an expression, not a number, which is what makes the
// "condition" case work: `beat = mod(t * 2, 1) < 0.5` is 1 while the beat is
// in its first half and 0 after, and any parameter can then be written
// `=lerp(lo, hi, beat)`. Expression.cpp's comparison and logical operators
// exist for exactly this.
//
// Evaluation order is the order the list is in, and each global sees `t` plus
// every global *above* it - so they can build on each other, and a cycle is
// structurally impossible rather than something to detect at runtime. A
// global that fails to evaluate keeps its last good value and records the
// error for the editor to show, the same way a failing parameter expression
// does.
namespace ExprGlobals
{
   struct Global
   {
      std::string name;  // identifier as it appears inside an expression
      std::string expr;  // the expression text, without a leading '='
      float value = 0.0f;
      std::string error; // empty when the last evaluation succeeded
   };

   std::vector<Global>& All();

   // Name rules, enforced by the editor rather than at evaluation time so the
   // failure is visible where it is typed: identifier characters only, not
   // starting with a digit, and not one of the names the evaluator already
   // binds (`t`, `pi`, `lo`, `hi`), which would otherwise be shadowed with no
   // way to tell from the expression which one won.
   bool IsValidName(const std::string& name, std::string& outError);

   // Main thread, once per frame, before any parameter expression is
   // evaluated. Fills in `value` and `error` on every entry.
   void EvaluateAll(double t);

   // Name -> last evaluated value, for binding into a parameter expression's
   // identifier lookup. Rebuilt by EvaluateAll.
   const std::map<std::string, float>& Values();

   // Preset definition for quick-adding popular global modulation expressions
   struct Preset
   {
      std::string category;
      std::string name;
      std::string expr;
      std::string description;
   };

   const std::vector<Preset>& Presets();

   // Dropping the whole graph drops the globals with it - they are part of the
   // patch, not of the application.
   void Clear();
}
