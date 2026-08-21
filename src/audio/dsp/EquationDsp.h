#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace EquationDsp
{
   constexpr int kFrameSize = 1024;
   constexpr int kMipLevels = 10;
   constexpr int kMaxHarmonics = kFrameSize / 2; // 512

   enum DomainMode
   {
      kDomainZeroToOne = 0,     // [0, 1]
      kDomainNegPiToPi = 1,     // [-pi, pi]
      kDomainNegOneToOne = 2,   // [-1, 1]
      kDomainCount
   };

   // Bank holding exact 10-level anti-aliased mip pyramid for one equation cycle
   struct EquationBank
   {
      std::vector<float> data; // kMipLevels * kFrameSize floats
      std::vector<float> previewCurve; // 1024 raw samples for UI graph

      EquationBank()
         : data(kMipLevels * kFrameSize, 0.0f)
         , previewCurve(kFrameSize, 0.0f)
      {
         // Default to sine wave
         for (int L = 0; L < kMipLevels; L++)
         {
            float* dst = data.data() + L * kFrameSize;
            for (int i = 0; i < kFrameSize; i++)
            {
               const float val = sinf(6.28318530717958647692f * (float)i / (float)kFrameSize);
               dst[i] = val;
               if (L == 0)
                  previewCurve[i] = val;
            }
         }
      }

      inline const float* Mip(int level) const
      {
         const int lvl = std::clamp(level, 0, kMipLevels - 1);
         return data.data() + lvl * kFrameSize;
      }
   };

   inline int MipForPhaseInc(double phaseInc)
   {
      if (phaseInc <= 0.0)
         return 0;
      const double maxHarmonic = 0.5 / phaseInc;
      int level = 0;
      int ceiling = kMaxHarmonics;
      while (level < kMipLevels - 1 && (double)ceiling > maxHarmonic)
      {
         ceiling >>= 1;
         level++;
      }
      return level;
   }

   inline float SampleBank(const EquationBank& bank, double phase01, double phaseInc)
   {
      phase01 -= floor(phase01);
      const int mip = MipForPhaseInc(phaseInc);
      const float* frame = bank.Mip(mip);
      const double x = phase01 * (double)kFrameSize;
      const int i0 = (int)x & (kFrameSize - 1);
      const int i1 = (i0 + 1) & (kFrameSize - 1);
      const float fx = (float)(x - (double)i0);
      return frame[i0] + (frame[i1] - frame[i0]) * fx;
   }

   // -------------------------------------------------------------------------
   // Fast AST Expression Evaluator for y = f(x, a, b, c, d, t, pi)
   // -------------------------------------------------------------------------
   enum class TokenType
   {
      End,
      Number,
      Identifier,
      Plus,
      Minus,
      Mul,
      Div,
      Mod,
      Pow,
      LParen,
      RParen,
      Comma,
      Lt,
      Le,
      Gt,
      Ge,
      Eq,
      Ne,
      And,
      Or,
      Not
   };

   struct Token
   {
      TokenType type = TokenType::End;
      double numValue = 0.0;
      std::string strValue;
   };

   struct AstNode;
   using AstNodePtr = std::shared_ptr<AstNode>;

   enum class AstType
   {
      Constant,
      Variable,
      UnaryOp,
      BinaryOp,
      FunctionCall
   };

   enum class VarId
   {
      X, A, B, C, D, T, Pi, E, Phi
   };

   struct AstNode
   {
      AstType type = AstType::Constant;
      double constVal = 0.0;
      VarId varId = VarId::X;
      TokenType op = TokenType::Plus;
      std::string funcName;
      std::vector<AstNodePtr> children;

      double Evaluate(double x, double a, double b, double c, double d, double t) const
      {
         switch (type)
         {
            case AstType::Constant:
               return constVal;

            case AstType::Variable:
               switch (varId)
               {
                  case VarId::X: return x;
                  case VarId::A: return a;
                  case VarId::B: return b;
                  case VarId::C: return c;
                  case VarId::D: return d;
                  case VarId::T: return t;
                  case VarId::Pi: return 3.14159265358979323846;
                  case VarId::E: return 2.71828182845904523536;
                  case VarId::Phi: return 1.61803398874989484820;
               }
               return 0.0;

            case AstType::UnaryOp:
            {
               if (children.empty()) return 0.0;
               const double v = children[0]->Evaluate(x, a, b, c, d, t);
               if (op == TokenType::Minus) return -v;
               if (op == TokenType::Not) return (std::abs(v) < 1e-9) ? 1.0 : 0.0;
               return v;
            }

            case AstType::BinaryOp:
            {
               if (children.size() < 2) return 0.0;
               const double left = children[0]->Evaluate(x, a, b, c, d, t);
               const double right = children[1]->Evaluate(x, a, b, c, d, t);
               switch (op)
               {
                  case TokenType::Plus:  return left + right;
                  case TokenType::Minus: return left - right;
                  case TokenType::Mul:   return left * right;
                  case TokenType::Div:   return std::abs(right) > 1e-12 ? left / right : (left >= 0.0 ? 1000.0 : -1000.0);
                  case TokenType::Mod:   return std::abs(right) > 1e-12 ? fmod(left, right) : 0.0;
                  case TokenType::Pow:   return pow(left, right);
                  case TokenType::Lt:    return left < right ? 1.0 : 0.0;
                  case TokenType::Le:    return left <= right ? 1.0 : 0.0;
                  case TokenType::Gt:    return left > right ? 1.0 : 0.0;
                  case TokenType::Ge:    return left >= right ? 1.0 : 0.0;
                  case TokenType::Eq:    return std::abs(left - right) < 1e-6 ? 1.0 : 0.0;
                  case TokenType::Ne:    return std::abs(left - right) >= 1e-6 ? 1.0 : 0.0;
                  case TokenType::And:   return (std::abs(left) > 1e-9 && std::abs(right) > 1e-9) ? 1.0 : 0.0;
                  case TokenType::Or:    return (std::abs(left) > 1e-9 || std::abs(right) > 1e-9) ? 1.0 : 0.0;
                  default: return 0.0;
               }
            }

            case AstType::FunctionCall:
            {
               const size_t n = children.size();
               if (n == 0) return 0.0;
               const double v0 = children[0]->Evaluate(x, a, b, c, d, t);
               const double v1 = n > 1 ? children[1]->Evaluate(x, a, b, c, d, t) : 0.0;
               const double v2 = n > 2 ? children[2]->Evaluate(x, a, b, c, d, t) : 0.0;

               if (funcName == "sin") return sin(v0);
               if (funcName == "cos") return cos(v0);
               if (funcName == "tan") return tan(v0);
               if (funcName == "asin") return asin(std::clamp(v0, -1.0, 1.0));
               if (funcName == "acos") return acos(std::clamp(v0, -1.0, 1.0));
               if (funcName == "atan") return atan(v0);
               if (funcName == "atan2") return atan2(v0, v1);
               if (funcName == "sinh") return sinh(std::clamp(v0, -20.0, 20.0));
               if (funcName == "cosh") return cosh(std::clamp(v0, -20.0, 20.0));
               if (funcName == "tanh") return tanh(v0);
               if (funcName == "abs") return std::abs(v0);
               if (funcName == "sign" || funcName == "sgn") return v0 > 0.0 ? 1.0 : (v0 < 0.0 ? -1.0 : 0.0);
               if (funcName == "sqrt") return sqrt(std::max(0.0, v0));
               if (funcName == "cbrt") return cbrt(v0);
               if (funcName == "exp") return exp(std::clamp(v0, -40.0, 40.0));
               if (funcName == "log" || funcName == "ln") return log(std::max(1e-12, v0));
               if (funcName == "log2") return log2(std::max(1e-12, v0));
               if (funcName == "log10") return log10(std::max(1e-12, v0));
               if (funcName == "pow") return pow(v0, v1);
               if (funcName == "floor") return floor(v0);
               if (funcName == "ceil") return ceil(v0);
               if (funcName == "round") return round(v0);
               if (funcName == "trunc") return trunc(v0);
               if (funcName == "min") return std::min(v0, v1);
               if (funcName == "max") return std::max(v0, v1);
               if (funcName == "clamp") return std::clamp(v0, v1, v2);
               if (funcName == "lerp") return v0 + (v1 - v0) * v2;
               if (funcName == "step") return v0 <= v1 ? 1.0 : 0.0;
               if (funcName == "smoothstep")
               {
                  const double edge0 = v0, edge1 = v1, inX = v2;
                  const double denom = edge1 - edge0;
                  const double val = std::abs(denom) > 1e-12 ? std::clamp((inX - edge0) / denom, 0.0, 1.0) : 0.0;
                  return val * val * (3.0 - 2.0 * val);
               }
               if (funcName == "sinc")
               {
                  const double px = 3.14159265358979323846 * v0;
                  return std::abs(px) < 1e-7 ? 1.0 : sin(px) / px;
               }
               if (funcName == "mod")
               {
                  return std::abs(v1) > 1e-12 ? fmod(v0, v1) : 0.0;
               }
               if (funcName == "tri")
               {
                  const double wrap = v0 - floor(v0);
                  return wrap < 0.5 ? 4.0 * wrap - 1.0 : 3.0 - 4.0 * wrap;
               }
               if (funcName == "saw")
               {
                  const double wrap = v0 - floor(v0);
                  return 2.0 * wrap - 1.0;
               }
               if (funcName == "sqr")
               {
                  const double wrap = v0 - floor(v0);
                  return wrap < 0.5 ? 1.0 : -1.0;
               }
               if (funcName == "if")
               {
                  return (std::abs(v0) > 1e-9) ? v1 : v2;
               }
               return v0;
            }
         }
         return 0.0;
      }
   };

   class Parser
   {
   public:
      static bool Parse(const std::string& expr, AstNodePtr& outRoot, std::string& outError)
      {
         Parser p(expr);
         outRoot = p.ParseExpression();
         if (!p.mError.empty())
         {
            outError = p.mError;
            outRoot = nullptr;
            return false;
         }
         if (p.mCurToken.type != TokenType::End)
         {
            outError = "unexpected extra tokens after expression";
            outRoot = nullptr;
            return false;
         }
         return true;
      }

   private:
      explicit Parser(const std::string& s) : mSrc(s)
      {
         NextToken();
      }

      void NextToken()
      {
         while (mPos < mSrc.size() && isspace((unsigned char)mSrc[mPos]))
            mPos++;

         if (mPos >= mSrc.size())
         {
            mCurToken.type = TokenType::End;
            return;
         }

         const char c = mSrc[mPos];
         if (isdigit((unsigned char)c) || c == '.')
         {
            const size_t start = mPos;
            bool dotSeen = (c == '.');
            mPos++;
            while (mPos < mSrc.size())
            {
               const char ch = mSrc[mPos];
               if (isdigit((unsigned char)ch))
               {
                  mPos++;
               }
               else if (ch == '.' && !dotSeen)
               {
                  dotSeen = true;
                  mPos++;
               }
               else
               {
                  break;
               }
            }
            mCurToken.type = TokenType::Number;
            mCurToken.numValue = atof(mSrc.substr(start, mPos - start).c_str());
            return;
         }

         if (isalpha((unsigned char)c) || c == '_')
         {
            const size_t start = mPos;
            while (mPos < mSrc.size() && (isalnum((unsigned char)mSrc[mPos]) || mSrc[mPos] == '_'))
               mPos++;
            std::string id = mSrc.substr(start, mPos - start);
            for (char& ch : id)
               ch = (char)tolower((unsigned char)ch);
            mCurToken.type = TokenType::Identifier;
            mCurToken.strValue = id;
            return;
         }

         mPos++;
         switch (c)
         {
            case '+': mCurToken.type = TokenType::Plus; return;
            case '-': mCurToken.type = TokenType::Minus; return;
            case '*': mCurToken.type = TokenType::Mul; return;
            case '/': mCurToken.type = TokenType::Div; return;
            case '%': mCurToken.type = TokenType::Mod; return;
            case '^': mCurToken.type = TokenType::Pow; return;
            case '(': mCurToken.type = TokenType::LParen; return;
            case ')': mCurToken.type = TokenType::RParen; return;
            case ',': mCurToken.type = TokenType::Comma; return;
            case '<':
               if (mPos < mSrc.size() && mSrc[mPos] == '=') { mPos++; mCurToken.type = TokenType::Le; }
               else mCurToken.type = TokenType::Lt;
               return;
            case '>':
               if (mPos < mSrc.size() && mSrc[mPos] == '=') { mPos++; mCurToken.type = TokenType::Ge; }
               else mCurToken.type = TokenType::Gt;
               return;
            case '=':
               if (mPos < mSrc.size() && mSrc[mPos] == '=') { mPos++; }
               mCurToken.type = TokenType::Eq;
               return;
            case '!':
               if (mPos < mSrc.size() && mSrc[mPos] == '=') { mPos++; mCurToken.type = TokenType::Ne; }
               else mCurToken.type = TokenType::Not;
               return;
            case '&':
               if (mPos < mSrc.size() && mSrc[mPos] == '&') { mPos++; }
               mCurToken.type = TokenType::And;
               return;
            case '|':
               if (mPos < mSrc.size() && mSrc[mPos] == '|') { mPos++; }
               mCurToken.type = TokenType::Or;
               return;
            default:
               mError = std::string("unknown character '") + c + "'";
               mCurToken.type = TokenType::End;
               return;
         }
      }

      AstNodePtr ParseExpression()
      {
         return ParseOr();
      }

      AstNodePtr ParseOr()
      {
         AstNodePtr left = ParseAnd();
         while (mCurToken.type == TokenType::Or)
         {
            TokenType op = mCurToken.type;
            NextToken();
            AstNodePtr right = ParseAnd();
            auto node = std::make_shared<AstNode>();
            node->type = AstType::BinaryOp;
            node->op = op;
            node->children = { left, right };
            left = node;
         }
         return left;
      }

      AstNodePtr ParseAnd()
      {
         AstNodePtr left = ParseComparison();
         while (mCurToken.type == TokenType::And)
         {
            TokenType op = mCurToken.type;
            NextToken();
            AstNodePtr right = ParseComparison();
            auto node = std::make_shared<AstNode>();
            node->type = AstType::BinaryOp;
            node->op = op;
            node->children = { left, right };
            left = node;
         }
         return left;
      }

      AstNodePtr ParseComparison()
      {
         AstNodePtr left = ParseAddSub();
         while (mCurToken.type == TokenType::Lt || mCurToken.type == TokenType::Le ||
                mCurToken.type == TokenType::Gt || mCurToken.type == TokenType::Ge ||
                mCurToken.type == TokenType::Eq || mCurToken.type == TokenType::Ne)
         {
            TokenType op = mCurToken.type;
            NextToken();
            AstNodePtr right = ParseAddSub();
            auto node = std::make_shared<AstNode>();
            node->type = AstType::BinaryOp;
            node->op = op;
            node->children = { left, right };
            left = node;
         }
         return left;
      }

      AstNodePtr ParseAddSub()
      {
         AstNodePtr left = ParseMulDivMod();
         while (mCurToken.type == TokenType::Plus || mCurToken.type == TokenType::Minus)
         {
            TokenType op = mCurToken.type;
            NextToken();
            AstNodePtr right = ParseMulDivMod();
            auto node = std::make_shared<AstNode>();
            node->type = AstType::BinaryOp;
            node->op = op;
            node->children = { left, right };
            left = node;
         }
         return left;
      }

      AstNodePtr ParseMulDivMod()
      {
         AstNodePtr left = ParsePower();
         while (mCurToken.type == TokenType::Mul || mCurToken.type == TokenType::Div || mCurToken.type == TokenType::Mod)
         {
            TokenType op = mCurToken.type;
            NextToken();
            AstNodePtr right = ParsePower();
            auto node = std::make_shared<AstNode>();
            node->type = AstType::BinaryOp;
            node->op = op;
            node->children = { left, right };
            left = node;
         }
         return left;
      }

      AstNodePtr ParsePower()
      {
         AstNodePtr left = ParseUnary();
         if (mCurToken.type == TokenType::Pow)
         {
            NextToken();
            AstNodePtr right = ParsePower(); // right-associative
            auto node = std::make_shared<AstNode>();
            node->type = AstType::BinaryOp;
            node->op = TokenType::Pow;
            node->children = { left, right };
            return node;
         }
         return left;
      }

      AstNodePtr ParseUnary()
      {
         if (mCurToken.type == TokenType::Minus || mCurToken.type == TokenType::Plus || mCurToken.type == TokenType::Not)
         {
            TokenType op = mCurToken.type;
            NextToken();
            AstNodePtr operand = ParseUnary();
            if (op == TokenType::Plus) return operand;
            auto node = std::make_shared<AstNode>();
            node->type = AstType::UnaryOp;
            node->op = op;
            node->children = { operand };
            return node;
         }
         return ParsePrimary();
      }

      AstNodePtr ParsePrimary()
      {
         if (mCurToken.type == TokenType::Number)
         {
            auto node = std::make_shared<AstNode>();
            node->type = AstType::Constant;
            node->constVal = mCurToken.numValue;
            NextToken();
            return node;
         }

         if (mCurToken.type == TokenType::LParen)
         {
            NextToken();
            AstNodePtr expr = ParseExpression();
            if (mCurToken.type != TokenType::RParen)
            {
               if (mError.empty()) mError = "expected ')'";
               return nullptr;
            }
            NextToken();
            return expr;
         }

         if (mCurToken.type == TokenType::Identifier)
         {
            std::string id = mCurToken.strValue;
            NextToken();

            // Check if it's a function call (followed by '(')
            if (mCurToken.type == TokenType::LParen)
            {
               NextToken();
               auto node = std::make_shared<AstNode>();
               node->type = AstType::FunctionCall;
               node->funcName = id;
               if (mCurToken.type != TokenType::RParen)
               {
                  while (true)
                  {
                     AstNodePtr arg = ParseExpression();
                     if (!arg) return nullptr;
                     node->children.push_back(arg);
                     if (mCurToken.type == TokenType::Comma)
                     {
                        NextToken();
                     }
                     else
                     {
                        break;
                     }
                  }
               }
               if (mCurToken.type != TokenType::RParen)
               {
                  if (mError.empty()) mError = "expected ')' in function call";
                  return nullptr;
               }
               NextToken();
               return node;
            }

            // Variables and Constants
            auto node = std::make_shared<AstNode>();
            node->type = AstType::Variable;
            if (id == "x" || id == "phase") node->varId = VarId::X;
            else if (id == "a" || id == "k1") node->varId = VarId::A;
            else if (id == "b" || id == "k2") node->varId = VarId::B;
            else if (id == "c" || id == "k3") node->varId = VarId::C;
            else if (id == "d" || id == "k4") node->varId = VarId::D;
            else if (id == "t" || id == "time") node->varId = VarId::T;
            else if (id == "pi") node->varId = VarId::Pi;
            else if (id == "e") node->varId = VarId::E;
            else if (id == "phi") node->varId = VarId::Phi;
            else
            {
               if (mError.empty()) mError = "unknown variable '" + id + "'";
               return nullptr;
            }
            return node;
         }

         if (mError.empty())
            mError = "syntax error, unexpected token";
         return nullptr;
      }

      std::string mSrc;
      size_t mPos = 0;
      Token mCurToken;
      std::string mError;
   };

   // -------------------------------------------------------------------------
   // Fast Cooley-Tukey Radix-2 FFT Engine for N = 1024
   // -------------------------------------------------------------------------
   struct Radix2FFT
   {
      static constexpr int N = 1024;
      float cosTable[N / 2];
      float sinTable[N / 2];
      uint16_t bitRev[N];

      Radix2FFT()
      {
         for (int i = 0; i < N / 2; i++)
         {
            const double angle = -2.0 * 3.14159265358979323846 * (double)i / (double)N;
            cosTable[i] = (float)cos(angle);
            sinTable[i] = (float)sin(angle);
         }
         for (int i = 0; i < N; i++)
         {
            int rev = 0;
            int temp = i;
            for (int j = 0; j < 10; j++)
            {
               rev = (rev << 1) | (temp & 1);
               temp >>= 1;
            }
            bitRev[i] = (uint16_t)rev;
         }
      }

      static const Radix2FFT& Instance()
      {
         static Radix2FFT sInstance;
         return sInstance;
      }

      void Forward(float* re, float* im) const
      {
         for (int i = 0; i < N; i++)
         {
            const int j = bitRev[i];
            if (i < j)
            {
               std::swap(re[i], re[j]);
               std::swap(im[i], im[j]);
            }
         }

         for (int len = 2; len <= N; len <<= 1)
         {
            const int half = len >> 1;
            const int step = N / len;
            for (int i = 0; i < N; i += len)
            {
               for (int k = 0; k < half; k++)
               {
                  const int twiddleIdx = k * step;
                  const float uRe = re[i + k];
                  const float uIm = im[i + k];
                  const float vRe = re[i + k + half] * cosTable[twiddleIdx] - im[i + k + half] * sinTable[twiddleIdx];
                  const float vIm = re[i + k + half] * sinTable[twiddleIdx] + im[i + k + half] * cosTable[twiddleIdx];

                  re[i + k] = uRe + vRe;
                  im[i + k] = uIm + vIm;
                  re[i + k + half] = uRe - vRe;
                  im[i + k + half] = uIm - vIm;
               }
            }
         }
      }

      void Inverse(float* re, float* im) const
      {
         for (int i = 0; i < N; i++)
            im[i] = -im[i];

         Forward(re, im);

         const float invN = 1.0f / (float)N;
         for (int i = 0; i < N; i++)
         {
            re[i] *= invN;
            im[i] = -im[i] * invN;
         }
      }
   };

   // -------------------------------------------------------------------------
   // Build 10-level Anti-Aliased Wavetable from AST Function
   // -------------------------------------------------------------------------
   inline void BuildBankFromAst(EquationBank& bank, const AstNode& ast,
                                int domainMode, float a, float b, float c, float d, float t)
   {
      const int ceilings[kMipLevels] = { 512, 256, 128, 64, 32, 16, 8, 4, 2, 1 };
      const auto& fft = Radix2FFT::Instance();

      float re[kFrameSize];
      float im[kFrameSize];
      float specRe[kFrameSize];
      float specIm[kFrameSize];

      // 1. Sample raw 1024 points over cycle
      double mean = 0.0;
      const double piVal = 3.14159265358979323846;

      for (int i = 0; i < kFrameSize; i++)
      {
         const double normX = (double)i / (double)kFrameSize;
         double evalX = normX;
         switch (domainMode)
         {
            case kDomainZeroToOne:
               evalX = normX;
               break;
            case kDomainNegPiToPi:
               evalX = (normX - 0.5) * 2.0 * piVal;
               break;
            case kDomainNegOneToOne:
               evalX = (normX - 0.5) * 2.0;
               break;
         }

         double val = ast.Evaluate(evalX, (double)a, (double)b, (double)c, (double)d, (double)t);
         if (std::isnan(val) || std::isinf(val))
            val = 0.0;
         val = std::clamp(val, -100.0, 100.0);

         re[i] = (float)val;
         im[i] = 0.0f;
         mean += val;
      }

      // Remove DC bias
      mean /= (double)kFrameSize;
      float maxAmp = 0.0f;
      for (int i = 0; i < kFrameSize; i++)
      {
         re[i] -= (float)mean;
         maxAmp = std::max(maxAmp, std::abs(re[i]));
      }

      // Peak normalize if above 1.0
      if (maxAmp > 1.0f)
      {
         const float normFactor = 1.0f / maxAmp;
         for (int i = 0; i < kFrameSize; i++)
            re[i] *= normFactor;
      }
      else if (maxAmp < 1e-4f)
      {
         // Pure silence/zero: fill gentle fallback sine
         for (int i = 0; i < kFrameSize; i++)
            re[i] = sinf(6.283185307f * (float)i / (float)kFrameSize);
      }

      // Save for UI preview
      for (int i = 0; i < kFrameSize; i++)
         bank.previewCurve[i] = re[i];

      // 2. Forward FFT to frequency domain
      fft.Forward(re, im);
      std::memcpy(specRe, re, sizeof(re));
      std::memcpy(specIm, im, sizeof(im));

      // 3. Synthesize each mip level with harmonic ceiling
      for (int L = 0; L < kMipLevels; L++)
      {
         const int ceiling = ceilings[L];
         std::memcpy(re, specRe, sizeof(re));
         std::memcpy(im, specIm, sizeof(im));

         // Zero harmonics above ceiling
         for (int h = ceiling + 1; h <= kMaxHarmonics; h++)
         {
            re[h] = 0.0f;
            im[h] = 0.0f;
            if (h > 0 && h < kFrameSize)
            {
               re[kFrameSize - h] = 0.0f;
               im[kFrameSize - h] = 0.0f;
            }
         }

         // Inverse FFT back to time domain
         fft.Inverse(re, im);

         float* dst = bank.data.data() + L * kFrameSize;
         for (int i = 0; i < kFrameSize; i++)
            dst[i] = re[i];
      }
   }
}
