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
   };

   double ParseExpr(ParseState& s);

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
      args.push_back(ParseExpr(s));
      while (s.Consume(','))
         args.push_back(ParseExpr(s));
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
      s.Fail("unknown function '" + name + "'");
      return 0.0;
   }

   double ParseAtom(ParseState& s)
   {
      s.SkipSpace();
      if (s.Consume('('))
      {
         double v = ParseExpr(s);
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
}

bool Evaluate(const std::string& text, double t,
              const std::map<std::string, float>* siblings,
              float& outValue, std::string& outError)
{
   ParseState state(text);
   state.t = t;
   state.siblings = siblings;

   if (state.Peek() == '\0')
   {
      outError = "empty expression";
      return false;
   }

   double result = ParseExpr(state);
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
