#include "Expression.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace Expression
{
namespace
{
   struct ParseState
   {
      const std::string& src;
      size_t pos = 0;
      double t = 0.0;
      const std::map<std::string, float>* siblings = nullptr;
      const std::map<std::string, float>* globals = nullptr;
      std::string error;

      explicit ParseState(const std::string& s) : src(s) {}

      bool Failed() const { return !error.empty(); }

      void Fail(const std::string& msg)
      {
         if (error.empty())
            error = msg;
      }

      void SkipSpace()
      {
         while (pos < src.size() && isspace((unsigned char)src[pos]))
            pos++;
      }

      char Peek()
      {
         SkipSpace();
         return pos < src.size() ? src[pos] : '\0';
      }

      bool Consume(char c)
      {
         SkipSpace();
         if (pos < src.size() && src[pos] == c)
         {
            pos++;
            return true;
         }
         return false;
      }

      // Multi-character operators. Callers must try the longer spelling first
      // ("<=" before "<", "==" before "="), or the shorter one matches its
      // prefix and leaves the tail to be misread as the start of an operand.
      bool ConsumeStr(const char* tok)
      {
         SkipSpace();
         size_t n = 0;
         while (tok[n] != '\0')
            n++;
         if (pos + n > src.size())
            return false;
         for (size_t i = 0; i < n; i++)
            if (src[pos + i] != tok[i])
               return false;
         pos += n;
         return true;
      }
   };

   // The whole grammar's entry point, used by parenthesised sub-expressions
   // and function arguments as well as by Evaluate.
   double ParseOr(ParseState& s);

   double ParseNumber(ParseState& s)
   {
      s.SkipSpace();
      size_t start = s.pos;
      while (s.pos < s.src.size() &&
             (isdigit((unsigned char)s.src[s.pos]) || s.src[s.pos] == '.'))
         s.pos++;
      if (s.pos == start)
      {
         s.Fail("expected a number");
         return 0.0;
      }
      return atof(s.src.substr(start, s.pos - start).c_str());
   }

   std::string ParseIdentifier(ParseState& s)
   {
      s.SkipSpace();
      size_t start = s.pos;
      while (s.pos < s.src.size() &&
             (isalnum((unsigned char)s.src[s.pos]) || s.src[s.pos] == '_'))
         s.pos++;
      return s.src.substr(start, s.pos - start);
   }

   // Comma-separated argument list for a function call; caller already
   // consumed the opening '('.
   std::vector<double> ParseArgs(ParseState& s)
   {
      std::vector<double> args;
      if (s.Consume(')'))
         return args;
      args.push_back(ParseOr(s));
      while (s.Consume(','))
         args.push_back(ParseOr(s));
      if (!s.Consume(')'))
         s.Fail("expected ')'");
      return args;
   }

   double CallFunction(ParseState& s, const std::string& name, const std::vector<double>& a)
   {
      auto need = [&](size_t n) -> bool
      {
         if (a.size() != n)
         {
            s.Fail(name + "() expects " + std::to_string(n) + " argument(s)");
            return false;
         }
         return true;
      };
      if (name == "sin") return need(1) ? sin(a[0]) : 0.0;
      if (name == "cos") return need(1) ? cos(a[0]) : 0.0;
      if (name == "tan") return need(1) ? tan(a[0]) : 0.0;
      if (name == "abs") return need(1) ? fabs(a[0]) : 0.0;
      if (name == "floor") return need(1) ? floor(a[0]) : 0.0;
      if (name == "min") return need(2) ? std::min(a[0], a[1]) : 0.0;
      if (name == "max") return need(2) ? std::max(a[0], a[1]) : 0.0;
      if (name == "mod") return need(2) ? fmod(a[0], a[1]) : 0.0;
      if (name == "clamp") return need(3) ? std::min(std::max(a[0], a[1]), a[2]) : 0.0;
      if (name == "lerp") return need(3) ? a[0] + (a[1] - a[0]) * a[2] : 0.0;
      if (name == "ceil") return need(1) ? ceil(a[0]) : 0.0;
      if (name == "round") return need(1) ? floor(a[0] + 0.5) : 0.0;
      if (name == "sign") return need(1) ? (a[0] > 0.0 ? 1.0 : (a[0] < 0.0 ? -1.0 : 0.0)) : 0.0;
      if (name == "exp") return need(1) ? exp(a[0]) : 0.0;
      if (name == "pow") return need(2) ? pow(a[0], a[1]) : 0.0;
      if (name == "sqrt")
      {
         if (!need(1)) return 0.0;
         if (a[0] < 0.0) { s.Fail("sqrt() of a negative number"); return 0.0; }
         return sqrt(a[0]);
      }
      if (name == "log")
      {
         if (!need(1)) return 0.0;
         if (a[0] <= 0.0) { s.Fail("log() needs a positive argument"); return 0.0; }
         return log(a[0]);
      }
      // step/smoothstep take their edges first, matching GLSL - the same
      // spelling FormulaNode's shader code uses, so the two languages in this
      // app don't disagree about argument order.
      if (name == "step") return need(2) ? (a[1] < a[0] ? 0.0 : 1.0) : 0.0;
      if (name == "smoothstep")
      {
         if (!need(3)) return 0.0;
         if (a[1] == a[0]) return a[2] < a[0] ? 0.0 : 1.0;
         const double x = std::min(std::max((a[2] - a[0]) / (a[1] - a[0]), 0.0), 1.0);
         return x * x * (3.0 - 2.0 * x);
      }
      // Both branches are already evaluated by the time we get here - see the
      // note in Expression.h on why there is no short circuit.
      if (name == "if") return need(3) ? (a[0] != 0.0 ? a[1] : a[2]) : 0.0;
      s.Fail("unknown function '" + name + "'");
      return 0.0;
   }

   double ParseAtom(ParseState& s)
   {
      s.SkipSpace();
      if (s.Consume('('))
      {
         double v = ParseOr(s);
         if (!s.Consume(')'))
            s.Fail("expected ')'");
         return v;
      }
      char c = s.Peek();
      if (c == '-')
      {
         s.pos++;
         return -ParseAtom(s);
      }
      if (c == '+')
      {
         s.pos++;
         return ParseAtom(s);
      }
      if (c == '!')
      {
         // Only ever unary here: "!=" can only follow an operand, and an
         // operand is never what ParseAtom is called on.
         s.pos++;
         return ParseAtom(s) != 0.0 ? 0.0 : 1.0;
      }
      if (isdigit((unsigned char)c) || c == '.')
         return ParseNumber(s);
      if (isalpha((unsigned char)c) || c == '_')
      {
         std::string name = ParseIdentifier(s);
         if (s.Consume('('))
            return CallFunction(s, name, ParseArgs(s));
         if (name == "t")
            return s.t;
         if (name == "pi")
            return M_PI;
         if (s.siblings != nullptr)
         {
            auto it = s.siblings->find(name);
            if (it != s.siblings->end())
               return (double)it->second;
         }
         // Globals are looked up last, so a node's own parameter of the same
         // name always shadows one - see Expression.h.
         if (s.globals != nullptr)
         {
            auto it = s.globals->find(name);
            if (it != s.globals->end())
               return (double)it->second;
         }
         s.Fail("unknown identifier '" + name + "'");
         return 0.0;
      }
      s.Fail("unexpected character");
      return 0.0;
   }

   // Right-associative, so 2^3^2 == 2^(3^2).
   double ParsePower(ParseState& s)
   {
      double base = ParseAtom(s);
      if (s.Failed())
         return base;
      s.SkipSpace();
      if (s.Consume('^'))
      {
         double exp = ParsePower(s);
         return pow(base, exp);
      }
      return base;
   }

   double ParseTerm(ParseState& s)
   {
      double v = ParsePower(s);
      for (;;)
      {
         if (s.Failed()) return v;
         s.SkipSpace();
         char c = s.Peek();
         if (c == '*') { s.pos++; v *= ParsePower(s); }
         else if (c == '/')
         {
            s.pos++;
            double rhs = ParsePower(s);
            if (rhs == 0.0) { s.Fail("division by zero"); return v; }
            v /= rhs;
         }
         else if (c == '%')
         {
            s.pos++;
            double rhs = ParsePower(s);
            if (rhs == 0.0) { s.Fail("division by zero"); return v; }
            v = fmod(v, rhs);
         }
         else break;
      }
      return v;
   }

   double ParseExpr(ParseState& s)
   {
      double v = ParseTerm(s);
      for (;;)
      {
         if (s.Failed()) return v;
         s.SkipSpace();
         char c = s.Peek();
         if (c == '+') { s.pos++; v += ParseTerm(s); }
         else if (c == '-') { s.pos++; v -= ParseTerm(s); }
         else break;
      }
      return v;
   }

   // Comparisons bind looser than arithmetic, so "a + 1 > b" compares the sum
   // rather than adding 1 to a boolean. Non-associative in practice: "a < b <
   // c" parses left to right and compares c against a 0/1, which is what it
   // does in C too - spelling it "a < b && b < c" is the readable form and now
   // parses.
   double ParseCompare(ParseState& s)
   {
      double v = ParseExpr(s);
      for (;;)
      {
         if (s.Failed()) return v;
         s.SkipSpace();
         // Longer spellings first, or "<" swallows the "<" of "<=" and leaves
         // "=" to be misread as the start of an operand.
         if (s.ConsumeStr("<=")) v = (v <= ParseExpr(s)) ? 1.0 : 0.0;
         else if (s.ConsumeStr(">=")) v = (v >= ParseExpr(s)) ? 1.0 : 0.0;
         else if (s.ConsumeStr("==")) v = (v == ParseExpr(s)) ? 1.0 : 0.0;
         else if (s.ConsumeStr("!=")) v = (v != ParseExpr(s)) ? 1.0 : 0.0;
         else if (s.ConsumeStr("<")) v = (v < ParseExpr(s)) ? 1.0 : 0.0;
         else if (s.ConsumeStr(">")) v = (v > ParseExpr(s)) ? 1.0 : 0.0;
         else break;
      }
      return v;
   }

   double ParseAnd(ParseState& s)
   {
      double v = ParseCompare(s);
      while (!s.Failed() && s.ConsumeStr("&&"))
      {
         const double rhs = ParseCompare(s);
         v = (v != 0.0 && rhs != 0.0) ? 1.0 : 0.0;
      }
      return v;
   }

   double ParseOr(ParseState& s)
   {
      double v = ParseAnd(s);
      while (!s.Failed() && s.ConsumeStr("||"))
      {
         const double rhs = ParseAnd(s);
         v = (v != 0.0 || rhs != 0.0) ? 1.0 : 0.0;
      }
      return v;
   }
}

bool Evaluate(const std::string& text, double t,
              const std::map<std::string, float>* siblings,
              const std::map<std::string, float>* globals,
              float& outValue, std::string& outError)
{
   ParseState state(text);
   state.t = t;
   state.siblings = siblings;
   state.globals = globals;

   if (state.Peek() == '\0')
   {
      outError = "empty expression";
      return false;
   }

   double result = ParseOr(state);
   if (!state.Failed())
   {
      state.SkipSpace();
      if (state.pos != text.size())
         state.Fail("unexpected trailing text");
   }
   if (state.Failed())
   {
      outError = state.error;
      return false;
   }
   outValue = (float)result;
   return true;
}
}
