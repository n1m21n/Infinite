#include "ExprGlobals.h"

#include <cctype>

#include "Expression.h"

namespace ExprGlobals
{
namespace
{
   std::vector<Global> sGlobals;
   std::map<std::string, float> sValues;
}

std::vector<Global>& All() { return sGlobals; }

bool IsValidName(const std::string& name, std::string& outError)
{
   if (name.empty())
   {
      outError = "name is empty";
      return false;
   }
   if (isdigit((unsigned char)name[0]) != 0)
   {
      outError = "name cannot start with a digit";
      return false;
   }
   for (char c : name)
   {
      if (isalnum((unsigned char)c) == 0 && c != '_')
      {
         outError = "name may only contain letters, digits and _";
         return false;
      }
   }
   if (name == "t" || name == "pi" || name == "lo" || name == "hi")
   {
      outError = "'" + name + "' is already bound by the evaluator";
      return false;
   }
   return true;
}

void EvaluateAll(double t)
{
   sValues.clear();
   for (Global& g : sGlobals)
   {
      std::string nameError;
      if (g.name.empty() || !IsValidName(g.name, nameError))
      {
         g.error = nameError.empty() ? "name is empty" : nameError;
         continue;
      }
      if (g.expr.empty())
      {
         // An empty expression is a blank row mid-edit, not a failure - it
         // still publishes its last value so downstream parameters don't jump
         // while the field is being retyped.
         g.error.clear();
         sValues[g.name] = g.value;
         continue;
      }

      float result = 0.0f;
      std::string error;
      // `sValues` at this point holds exactly the globals declared above this
      // one, which is what makes the ordering rule in the header hold: a
      // global referring to one below it fails with "unknown identifier"
      // rather than silently reading a stale value from the previous frame.
      if (Expression::Evaluate(g.expr, t, &sValues, nullptr, result, error))
      {
         g.value = result;
         g.error.clear();
      }
      else
      {
         g.error = error;
      }
      sValues[g.name] = g.value;
   }
}

const std::map<std::string, float>& Values() { return sValues; }

void Clear()
{
   sGlobals.clear();
   sValues.clear();
}
}
