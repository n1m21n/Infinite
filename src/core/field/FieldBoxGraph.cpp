#include "FieldBoxGraph.h"
#include "FieldAst.h"
#include "FieldLex.h"
#include "FieldParse.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

// How code becomes boxes and back (plan §8.2.1).
//
//   code --Lex/Parse--> AST --GraphBuilder--> BoxGraph --Printer--> code
//
// * Every expression the registry knows becomes a box; anything else becomes a
//   code box holding the exact source text (comments included).
// * A call keeps exactly the arguments the source wrote, and a call stays a call:
//   `pow(a, b)` never comes back as `a ^ b`. Sugar boxes never capture a call.
// * Comments: a code box keeps its own. Whole-line comments before a statement
//   ride on that statement's box (or when-group) and print above it; a comment
//   at the end of a box statement's line moves to the line above it. Comments
//   after the last statement are kept on the graph.
// * The printer works from the wires, so a graph built by hand prints too.

namespace Field
{
   const BoxNode* BoxGraph::FindBox(int id) const
   {
      for (const BoxNode& b : boxes)
      {
         if (b.id == id)
            return &b;
      }
      return nullptr;
   }

   BoxNode* BoxGraph::FindBox(int id)
   {
      for (BoxNode& b : boxes)
      {
         if (b.id == id)
            return &b;
      }
      return nullptr;
   }

   const BoxWire* BoxGraph::FindWireTo(int dstBoxId, int dstPinIdx) const
   {
      for (const BoxWire& w : wires)
      {
         if (w.dstBoxId == dstBoxId && w.dstPinIdx == dstPinIdx)
            return &w;
      }
      return nullptr;
   }

   int BoxGraph::CountCodeBoxes() const
   {
      int count = 0;
      for (const BoxNode& b : boxes)
      {
         if (b.specId == "code")
            ++count;
      }
      return count;
   }

   namespace
   {
      // Binding strength, low to high, matching FieldParse.cpp.
      enum Prec
      {
         kPrecOr = 1,
         kPrecAnd = 2,
         kPrecCompare = 3,
         kPrecAdd = 4,
         kPrecMul = 5,
         kPrecPow = 6,
         kPrecUnary = 7,
         kPrecPostfix = 8, // x.y, X.at(i), reduce.op(...): never the operand of a unary
         kPrecAtom = 10
      };

      std::string Trim(const std::string& s)
      {
         size_t a = 0;
         while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
            ++a;
         size_t b = s.size();
         while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
            --b;
         return s.substr(a, b - a);
      }

      // Shortest text that reads back as exactly the same double.
      std::string FormatDouble(double v)
      {
         if (std::isnan(v) || std::isinf(v) || v == 0.0)
            return "0";
         char buf[64];
         for (int digits = 15; digits <= 17; ++digits)
         {
            std::snprintf(buf, sizeof(buf), "%.*g", digits, v);
            if (std::strtod(buf, nullptr) == v)
               break;
         }
         return std::string(buf);
      }

      int BinaryPrec(const std::string& op)
      {
         if (op == "||") return kPrecOr;
         if (op == "&&") return kPrecAnd;
         if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") return kPrecCompare;
         if (op == "+" || op == "-") return kPrecAdd;
         if (op == "*" || op == "/" || op == "%") return kPrecMul;
         if (op == "^") return kPrecPow;
         return 0;
      }

      std::string Wrap(const std::string& s, int myPrec, int parentPrec)
      {
         return myPrec <= parentPrec ? "(" + s + ")" : s;
      }

      // FNV-1a: the same key on every OS and compiler (std::hash is not).
      std::string StableHash(const std::string& s)
      {
         uint32_t h = 2166136261u;
         for (unsigned char c : s)
         {
            h ^= c;
            h *= 16777619u;
         }
         char buf[16];
         std::snprintf(buf, sizeof(buf), "%08x", h);
         return std::string(buf);
      }

      bool IsOpenBracket(char c) { return c == '(' || c == '[' || c == '{'; }
      bool IsCloseBracket(char c) { return c == ')' || c == ']' || c == '}'; }

      // Index of the bracket that closes src[open], skipping strings and comments.
      size_t MatchClose(const std::string& src, size_t open)
      {
         int depth = 0;
         for (size_t p = open; p < src.size(); ++p)
         {
            char c = src[p];
            if (c == '"')
            {
               for (++p; p < src.size() && src[p] != '"'; ++p)
               {
                  if (src[p] == '\\')
                     ++p;
               }
               continue;
            }
            if (c == '#')
            {
               while (p < src.size() && src[p] != '\n')
                  ++p;
               continue;
            }
            if (IsOpenBracket(c))
               ++depth;
            else if (IsCloseBracket(c) && --depth == 0)
               return p;
         }
         return std::string::npos;
      }

      // Grows [lo, hi) until its brackets balance: `a + b) * c` gains its `(`,
      // `if (x) { y = 1` gains its `}`.
      void BalanceRange(const std::string& src, size_t& lo, size_t& hi)
      {
         for (int guard = 0; guard < 256; ++guard)
         {
            int depth = 0;
            int minDepth = 0;
            for (size_t p = lo; p < hi; ++p)
            {
               char c = src[p];
               if (c == '"')
               {
                  for (++p; p < hi && src[p] != '"'; ++p)
                  {
                     if (src[p] == '\\')
                        ++p;
                  }
                  continue;
               }
               if (c == '#')
               {
                  while (p < hi && src[p] != '\n')
                     ++p;
                  continue;
               }
               if (IsOpenBracket(c))
                  ++depth;
               else if (IsCloseBracket(c))
                  minDepth = std::min(minDepth, --depth);
            }
            if (minDepth < 0)
            {
               int need = -minDepth;
               int inner = 0;
               size_t p = lo;
               while (p > 0 && need > 0)
               {
                  char c = src[--p];
                  if (IsCloseBracket(c))
                     ++inner;
                  else if (IsOpenBracket(c))
                  {
                     if (inner > 0)
                        --inner;
                     else
                        --need;
                  }
               }
               if (need > 0)
                  return;
               lo = p;
               continue;
            }
            if (depth > 0)
            {
               // Close the innermost open bracket left inside the range.
               std::vector<size_t> opens;
               for (size_t p = lo; p < hi; ++p)
               {
                  char c = src[p];
                  if (c == '"')
                  {
                     for (++p; p < hi && src[p] != '"'; ++p)
                     {
                        if (src[p] == '\\')
                           ++p;
                     }
                     continue;
                  }
                  if (c == '#')
                  {
                     while (p < hi && src[p] != '\n')
                        ++p;
                     continue;
                  }
                  if (IsOpenBracket(c))
                     opens.push_back(p);
                  else if (IsCloseBracket(c) && !opens.empty())
                     opens.pop_back();
               }
               if (opens.empty())
                  return;
               size_t close = MatchClose(src, opens.front());
               if (close == std::string::npos)
                  return;
               hi = close + 1;
               continue;
            }
            return;
         }
      }

      void ForEachChild(const AstNodePtr& node, const std::function<void(const AstNodePtr&)>& fn)
      {
         if (!node)
            return;
         switch (node->kind)
         {
            case AstKind::Access: fn(static_cast<const AstAccess*>(node.get())->base); break;
            case AstKind::Index:
               fn(static_cast<const AstIndex*>(node.get())->base);
               fn(static_cast<const AstIndex*>(node.get())->index);
               break;
            case AstKind::Unary: fn(static_cast<const AstUnary*>(node.get())->operand); break;
            case AstKind::Binary:
               fn(static_cast<const AstBinary*>(node.get())->lhs);
               fn(static_cast<const AstBinary*>(node.get())->rhs);
               break;
            case AstKind::Call:
               for (const auto& a : static_cast<const AstCall*>(node.get())->args)
                  fn(a);
               break;
            case AstKind::Assign:
               fn(static_cast<const AstAssign*>(node.get())->lvalue);
               fn(static_cast<const AstAssign*>(node.get())->rvalue);
               break;
            case AstKind::Block:
               for (const auto& s : static_cast<const AstBlock*>(node.get())->statements)
                  fn(s);
               break;
            case AstKind::Program:
               for (const auto& s : static_cast<const AstProgram*>(node.get())->statements)
                  fn(s);
               break;
            case AstKind::If:
               fn(static_cast<const AstIf*>(node.get())->cond);
               fn(static_cast<const AstIf*>(node.get())->thenBlock);
               fn(static_cast<const AstIf*>(node.get())->elseBlock);
               break;
            case AstKind::For:
               fn(static_cast<const AstFor*>(node.get())->init);
               fn(static_cast<const AstFor*>(node.get())->cond);
               fn(static_cast<const AstFor*>(node.get())->step);
               fn(static_cast<const AstFor*>(node.get())->body);
               break;
            case AstKind::Map:
               fn(static_cast<const AstMap*>(node.get())->countExpr);
               fn(static_cast<const AstMap*>(node.get())->body);
               break;
            case AstKind::DeclParam: fn(static_cast<const AstDeclParam*>(node.get())->initExpr); break;
            case AstKind::DeclState: fn(static_cast<const AstDeclState*>(node.get())->initExpr); break;
            case AstKind::DeclAttrib: fn(static_cast<const AstDeclAttrib*>(node.get())->initExpr); break;
            case AstKind::DeclOutput: fn(static_cast<const AstDeclOutput*>(node.get())->initExpr); break;
            default: break;
         }
      }

      // Source range of a node and everything under it. The parser gives most
      // nodes only their first token, so the range is the union of the subtree's
      // tokens, each call stretched to its `)`, then balanced.
      void CollectExtent(const std::string& src, const AstNodePtr& node, size_t& lo, size_t& hi)
      {
         if (!node)
            return;
         const SourceSpan& sp = node->span;
         if (sp.length > 0 && sp.offset + sp.length <= src.size())
         {
            lo = std::min(lo, sp.offset);
            hi = std::max(hi, sp.offset + sp.length);
            if (node->kind == AstKind::Call)
            {
               size_t p = sp.offset + sp.length;
               while (p < src.size() && (src[p] == ' ' || src[p] == '\t'))
                  ++p;
               if (p < src.size() && src[p] == '(')
               {
                  size_t close = MatchClose(src, p);
                  if (close != std::string::npos)
                     hi = std::max(hi, close + 1);
               }
            }
         }
         ForEachChild(node, [&](const AstNodePtr& c) { CollectExtent(src, c, lo, hi); });
      }

      bool NodeExtent(const std::string& src, const AstNodePtr& node, size_t& lo, size_t& hi)
      {
         lo = std::string::npos;
         hi = 0;
         CollectExtent(src, node, lo, hi);
         if (lo == std::string::npos || hi <= lo)
            return false;
         BalanceRange(src, lo, hi);
         return true;
      }

      // Layout-free text of a subtree (no spaces, no parentheses, no comments), so
      // a key survives the printer re-spacing a line.
      std::string Canon(const std::string& src, const AstNodePtr& node)
      {
         if (!node)
            return "_";
         std::string s;
         switch (node->kind)
         {
            case AstKind::Literal:
            {
               auto* lit = static_cast<const AstLiteral*>(node.get());
               const SourceSpan& sp = node->span;
               if (sp.length > 0 && sp.offset + sp.length <= src.size())
                  return "L" + src.substr(sp.offset, sp.length);
               return "L" + (lit->isBool ? std::string(lit->boolValue ? "true" : "false") : FormatDouble(lit->numberValue));
            }
            case AstKind::Ident: return "I" + static_cast<const AstIdent*>(node.get())->name;
            case AstKind::Access: s = "." + static_cast<const AstAccess*>(node.get())->field; break;
            case AstKind::Unary: s = "U" + static_cast<const AstUnary*>(node.get())->op; break;
            case AstKind::Binary: s = "B" + static_cast<const AstBinary*>(node.get())->op; break;
            case AstKind::Call: s = "C" + static_cast<const AstCall*>(node.get())->callee; break;
            case AstKind::Assign: s = "A" + static_cast<const AstAssign*>(node.get())->op; break;
            case AstKind::DeclParam: s = "Dp" + static_cast<const AstDeclParam*>(node.get())->name; break;
            case AstKind::DeclState: s = "Ds" + static_cast<const AstDeclState*>(node.get())->name; break;
            case AstKind::DeclAttrib: s = "Da" + static_cast<const AstDeclAttrib*>(node.get())->name; break;
            case AstKind::DeclOutput: s = "Do" + static_cast<const AstDeclOutput*>(node.get())->name; break;
            case AstKind::DeclInput: return "Di" + static_cast<const AstDeclInput*>(node.get())->name;
            default: s = "K" + std::to_string(static_cast<int>(node->kind)); break;
         }
         s += "(";
         ForEachChild(node, [&](const AstNodePtr& c) { s += Canon(src, c) + ","; });
         return s + ")";
      }

      int NodePrec(const AstNodePtr& node)
      {
         if (!node)
            return 0;
         switch (node->kind)
         {
            case AstKind::Literal:
            case AstKind::Ident:
               return kPrecAtom;
            case AstKind::Call:
               return static_cast<const AstCall*>(node.get())->callee.find('.') != std::string::npos ? kPrecPostfix : kPrecAtom;
            case AstKind::Access:
            case AstKind::Index:
               return kPrecPostfix;
            case AstKind::Unary:
               return kPrecUnary;
            case AstKind::Binary:
               return BinaryPrec(static_cast<const AstBinary*>(node.get())->op);
            default:
               return 0;
         }
      }

      bool IsBoolLikeExpr(const AstNodePtr& node)
      {
         if (!node)
            return false;
         if (node->kind == AstKind::Binary)
         {
            const std::string& op = static_cast<const AstBinary*>(node.get())->op;
            return op == "<" || op == "<=" || op == ">" || op == ">="
                || op == "==" || op == "!=" || op == "&&" || op == "||";
         }
         if (node->kind == AstKind::Unary)
            return static_cast<const AstUnary*>(node.get())->op == "!";
         if (node->kind == AstKind::Literal)
            return static_cast<const AstLiteral*>(node.get())->isBool;
         return false;
      }

      struct PreScanInfo
      {
         std::unordered_set<std::string> stateNames;
         std::unordered_map<std::string, int> assignCounts;
      };

      void PreScan(const AstNodePtr& node, PreScanInfo& info)
      {
         if (!node)
            return;
         if (node->kind == AstKind::DeclState)
            info.stateNames.insert(static_cast<const AstDeclState*>(node.get())->name);
         if (node->kind == AstKind::Assign)
         {
            auto* a = static_cast<const AstAssign*>(node.get());
            if (a->lvalue && a->lvalue->kind == AstKind::Ident)
               info.assignCounts[static_cast<const AstIdent*>(a->lvalue.get())->name]++;
            else if (a->lvalue && a->lvalue->kind == AstKind::Access &&
                     static_cast<const AstAccess*>(a->lvalue.get())->base->kind == AstKind::Ident)
               info.assignCounts[static_cast<const AstIdent*>(static_cast<const AstAccess*>(a->lvalue.get())->base.get())->name]++;
         }
         ForEachChild(node, [&](const AstNodePtr& c) { PreScan(c, info); });
      }

      // Expressions that cannot be boxes: state offset reads `A(uv + d)`, table
      // indexing `tbl[i]`, strings, and calls with side effects or special forms.
      bool ExprNeedsCodeBox(const AstNodePtr& node, const PreScanInfo& info)
      {
         if (!node)
            return false;
         if (node->kind == AstKind::Literal)
            return static_cast<const AstLiteral*>(node.get())->isString;
         if (node->kind == AstKind::Index)
            return true;
         if (node->kind == AstKind::Call)
         {
            auto* c = static_cast<const AstCall*>(node.get());
            if (info.stateNames.count(c->callee) > 0)
               return true;
            if (c->callee == "delay" || c->callee == "phase" || c->callee == "vec4" ||
                c->callee == "emit" || c->callee == "connect" || c->callee == "set" || c->callee == "place")
               return true;
         }
         bool needs = false;
         ForEachChild(node, [&](const AstNodePtr& c) { needs = needs || ExprNeedsCodeBox(c, info); });
         return needs;
      }

      // Registry boxes a Field call may open as. Sugar never does (it has no Field
      // word of its own), nor do boxes that are not functions.
      const BoxSpec* SpecForCall(const std::string& callee)
      {
         const BoxSpec* spec = FindBoxSpec(callee);
         if (!spec || spec->isSugar || spec->outlets.empty())
            return nullptr;
         if (spec->family == BoxFamily::Sources || spec->family == BoxFamily::InletsOutlets)
            return nullptr;
         if (spec->id == "previous" || spec->id == "swizzle" || spec->id == "neighbour" || spec->id == "reduce")
            return nullptr;
         if (spec->fieldWord != callee && callee != "mod" && callee != "pow")
            return nullptr;
         return spec;
      }

      bool IsSourceSpecId(const std::string& id)
      {
         if (id == "ident" || id == "notes_in" || id == "geometry_in")
            return true;
         const BoxSpec* spec = FindBoxSpec(id);
         return spec && spec->family == BoxFamily::Sources && spec->inlets.empty() && id != "number" && id != "knob";
      }

      bool IsDeclSpecId(const std::string& id)
      {
         return id == "knob" || id == "previous" || id == "attrib" || id == "input_pin";
      }

      struct OutRef
      {
         int boxId = 0;
         int pinIdx = 0;
         ValueKind kind = ValueKind::Number;
      };

      struct CommentTok
      {
         size_t offset = 0;
         std::string text;
      };

      std::string JoinComments(const std::string& a, const std::string& b)
      {
         if (a.empty())
            return b;
         if (b.empty())
            return a;
         return a + "\n" + b;
      }

      // ---- sugar ---------------------------------------------------------------------
      // A sugar box has no Field word of its own: it prints as an expansion and an
      // expression of exactly that shape reopens as the box. `$k` is inlet k.
      struct SugarTemplate
      {
         std::string id;
         std::string text;
         AstNodePtr ast;          // parsed text, `$k` read as identifier `__sk`
         int prec = 0;            // binding strength of the whole expansion
         std::vector<int> uses;   // how often each inlet appears
         std::vector<int> ctx;    // parent precedence at each `$k`, in text order
      };

      void CollectSlotContexts(const AstNodePtr& node, int parentPrec, std::vector<int>& ctx)
      {
         if (!node)
            return;
         switch (node->kind)
         {
            case AstKind::Ident:
               if (static_cast<const AstIdent*>(node.get())->name.rfind("__s", 0) == 0)
                  ctx.push_back(parentPrec);
               return;
            case AstKind::Binary:
            {
               auto* bin = static_cast<const AstBinary*>(node.get());
               int p = BinaryPrec(bin->op);
               bool rightAssoc = bin->op == "^";
               CollectSlotContexts(bin->lhs, rightAssoc ? p : p - 1, ctx);
               CollectSlotContexts(bin->rhs, rightAssoc ? p - 1 : p, ctx);
               return;
            }
            case AstKind::Unary:
               CollectSlotContexts(static_cast<const AstUnary*>(node.get())->operand, kPrecPostfix, ctx);
               return;
            case AstKind::Access:
               CollectSlotContexts(static_cast<const AstAccess*>(node.get())->base, kPrecPow, ctx);
               return;
            case AstKind::Call:
               for (const AstNodePtr& a : static_cast<const AstCall*>(node.get())->args)
                  CollectSlotContexts(a, 0, ctx);
               return;
            default:
               ForEachChild(node, [&](const AstNodePtr& c) { CollectSlotContexts(c, 0, ctx); });
               return;
         }
      }

      const std::vector<SugarTemplate>& SugarTemplates()
      {
         static const std::vector<SugarTemplate> kTemplates = [] {
            static const std::pair<const char*, const char*> kText[] = {
               { "rescale", "$3 + ($0 - $1) / ($2 - $1) * ($4 - $3)" },
               { "wrap", "$1 + fract(($0 - $1) / ($2 - $1)) * ($2 - $1)" },
               { "rotate", "vec3($0.x * cos($1) - $0.y * sin($1), $0.x * sin($1) + $0.y * cos($1), $0.z)" },
               { "circle", "vec3(cos($0) * $1, sin($0) * $1, 0)" },
               { "spiral", "vec3(cos($0 * $1 * 6.28318531) * $2 * $0, sin($0 * $1 * 6.28318531) * $2 * $0, 0)" },
               { "note_to_hz", "440 * 2 ^ (($0 - 69) / 12)" },
               { "hz_to_note", "69 + 12 * log($0 / 440) / log(2)" },
               { "octave", "$0 + 12 * $1" },
               { "euclid", "$0 * $1 % $2 < $1" },
            };
            std::vector<SugarTemplate> out;
            for (const auto& [id, text] : kText)
            {
               SugarTemplate t;
               t.id = id;
               t.text = text;
               std::string src = "__r = ";
               for (const char* c = text; *c; ++c)
               {
                  if (*c == '$')
                     src += "__s";
                  else
                     src += *c;
               }
               std::vector<Token> toks;
               FieldError err;
               AstNodePtr prog;
               if (!Lex(src, toks, err) || !ParseProgram(toks, prog, err) || !prog)
                  continue;
               auto* p = static_cast<const AstProgram*>(prog.get());
               if (p->statements.empty() || p->statements[0]->kind != AstKind::Assign)
                  continue;
               t.ast = static_cast<const AstAssign*>(p->statements[0].get())->rvalue;
               t.prec = NodePrec(t.ast);
               CollectSlotContexts(t.ast, 0, t.ctx);
               for (const char* c = text; *c; ++c)
               {
                  if (*c == '$')
                  {
                     int k = c[1] - '0';
                     if (k >= static_cast<int>(t.uses.size()))
                        t.uses.resize(k + 1, 0);
                     t.uses[k]++;
                  }
               }
               out.push_back(std::move(t));
            }
            return out;
         }();
         return kTemplates;
      }

      const SugarTemplate* FindSugarTemplate(const std::string& id)
      {
         for (const SugarTemplate& t : SugarTemplates())
         {
            if (t.id == id)
               return &t;
         }
         return nullptr;
      }

      bool HasImpureCall(const AstNodePtr& node)
      {
         if (!node)
            return false;
         if (node->kind == AstKind::Call && static_cast<const AstCall*>(node.get())->callee == "rand")
            return true;
         bool impure = false;
         ForEachChild(node, [&](const AstNodePtr& c) { impure = impure || HasImpureCall(c); });
         return impure;
      }

      bool MatchTemplate(const std::string& src, const AstNodePtr& tpl, const AstNodePtr& node, std::vector<AstNodePtr>& binds)
      {
         if (!tpl || !node)
            return tpl == node;
         if (tpl->kind == AstKind::Ident)
         {
            const std::string& name = static_cast<const AstIdent*>(tpl.get())->name;
            if (name.rfind("__s", 0) == 0)
            {
               size_t k = static_cast<size_t>(name[3] - '0');
               if (k >= binds.size())
                  binds.resize(k + 1);
               if (!binds[k])
               {
                  binds[k] = node;
                  return true;
               }
               return Canon(src, binds[k]) == Canon(src, node) && !HasImpureCall(node);
            }
         }
         if (tpl->kind != node->kind)
            return false;
         switch (tpl->kind)
         {
            case AstKind::Literal:
            {
               auto* a = static_cast<const AstLiteral*>(tpl.get());
               auto* b = static_cast<const AstLiteral*>(node.get());
               return !b->isString && !b->isBool && a->numberValue == b->numberValue;
            }
            case AstKind::Ident:
               return static_cast<const AstIdent*>(tpl.get())->name == static_cast<const AstIdent*>(node.get())->name;
            case AstKind::Access:
            {
               auto* a = static_cast<const AstAccess*>(tpl.get());
               auto* b = static_cast<const AstAccess*>(node.get());
               return a->field == b->field && MatchTemplate(src, a->base, b->base, binds);
            }
            case AstKind::Unary:
            {
               auto* a = static_cast<const AstUnary*>(tpl.get());
               auto* b = static_cast<const AstUnary*>(node.get());
               return a->op == b->op && MatchTemplate(src, a->operand, b->operand, binds);
            }
            case AstKind::Binary:
            {
               auto* a = static_cast<const AstBinary*>(tpl.get());
               auto* b = static_cast<const AstBinary*>(node.get());
               return a->op == b->op && MatchTemplate(src, a->lhs, b->lhs, binds) && MatchTemplate(src, a->rhs, b->rhs, binds);
            }
            case AstKind::Call:
            {
               auto* a = static_cast<const AstCall*>(tpl.get());
               auto* b = static_cast<const AstCall*>(node.get());
               if (a->callee != b->callee || a->args.size() != b->args.size())
                  return false;
               for (size_t i = 0; i < a->args.size(); ++i)
               {
                  if (!MatchTemplate(src, a->args[i], b->args[i], binds))
                     return false;
               }
               return true;
            }
            default:
               return false;
         }
      }

      struct GraphBuilder
      {
         BoxGraph& graph;
         const PreScanInfo& info;
         const std::string& src;
         std::vector<CommentTok> comments;
         size_t commentIdx = 0;
         int nextBoxId = 1;
         int nextWireId = 1;
         int nextGroupId = 1;
         int nextStmtOrder = 0;
         std::unordered_map<std::string, OutRef> env;
         std::unordered_map<std::string, OutRef> builtinSources;
         std::unordered_map<std::string, int> keyCount;
         std::string stmtCanon; // layout-free text of the statement being lowered (for keys)
         size_t stmtLo = 0;
         size_t stmtHi = 0;
         std::string pendingComment;

         GraphBuilder(BoxGraph& g, const PreScanInfo& i, const std::string& s) : graph(g), info(i), src(s) {}

         std::string Slice(const AstNodePtr& node) const
         {
            size_t lo = 0, hi = 0;
            if (!NodeExtent(src, node, lo, hi))
               return "";
            return src.substr(lo, hi - lo);
         }

         // Comments not yet claimed that start before `offset`.
         std::string TakeCommentsBefore(size_t offset)
         {
            std::string out;
            while (commentIdx < comments.size() && comments[commentIdx].offset < offset)
               out = JoinComments(out, comments[commentIdx++].text);
            return out;
         }

         size_t LineEnd(size_t p) const
         {
            while (p < src.size() && src[p] != '\n')
               ++p;
            return p;
         }

         // Unique, edit-stable key: same base text -> same key, repeats get ~2, ~3.
         void SetKey(BoxNode& b, const std::string& base)
         {
            int n = ++keyCount[base];
            b.key = n == 1 ? base : base + "~" + std::to_string(n);
         }

         void SetHashKey(BoxNode& b, const AstNodePtr& node)
         {
            SetKey(b, "h" + StableHash(stmtCanon + "\x1f" + Canon(src, node) + "\x1f" + b.specId));
         }

         BoxNode& AddBox(const std::string& specId, const std::string& customLabel = "")
         {
            BoxNode node;
            node.id = nextBoxId++;
            node.specId = specId;
            if (const BoxSpec* spec = FindBoxSpec(specId))
            {
               node.label = customLabel.empty() ? spec->label : customLabel;
               for (const PinSpec& p : spec->inlets)
                  node.inlets.push_back({ p.name, p.kind, p.defaultValue });
               for (const PinSpec& p : spec->outlets)
                  node.outlets.push_back({ p.name, p.kind, p.defaultValue });
            }
            else
            {
               node.label = customLabel.empty() ? specId : customLabel;
            }
            graph.boxes.push_back(std::move(node));
            return graph.boxes.back();
         }

         // Sizes a call box's inlets to the number of arguments the source wrote.
         static void SizeInlets(BoxNode& b, size_t count)
         {
            if (b.inlets.size() > count)
               b.inlets.resize(count);
            while (b.inlets.size() < count)
               b.inlets.push_back({ "arg" + std::to_string(b.inlets.size() + 1), ValueKind::Number, 0.0 });
         }

         void Connect(int srcBoxId, int srcPinIdx, int dstBoxId, int dstPinIdx, ValueKind kind)
         {
            BoxWire w;
            w.id = nextWireId++;
            w.srcBoxId = srcBoxId;
            w.srcPinIdx = srcPinIdx;
            w.dstBoxId = dstBoxId;
            w.dstPinIdx = dstPinIdx;
            w.kind = kind;
            graph.wires.push_back(std::move(w));
         }

         void ConnectArgs(int boxId, const std::vector<AstNodePtr>& args, int firstPin)
         {
            for (size_t i = 0; i < args.size(); ++i)
            {
               OutRef r = LowerExpr(args[i]);
               Connect(r.boxId, r.pinIdx, boxId, firstPin + static_cast<int>(i), r.kind);
            }
         }

         OutRef GetOrCreateBuiltinSource(const std::string& name)
         {
            auto it = builtinSources.find(name);
            if (it != builtinSources.end())
               return it->second;

            static const std::unordered_map<std::string, std::string> kBuiltinSpec = {
               { "t", "time" }, { "dt", "dt" }, { "age", "age" }, { "beat", "beat" },
               { "i", "index" }, { "count", "count" }, { "P", "position" }, { "N", "normal" },
               { "Cd", "color_in" }, { "uv", "uv" }, { "in", "audio_in" }, { "sr", "sr" },
               { "freq", "freq" }, { "gate", "gate" }, { "noteOn", "noteOn" },
               { "noteNum", "noteNum" }, { "noteVel", "noteVel" }
            };

            auto spIt = kBuiltinSpec.find(name);
            BoxNode& b = spIt != kBuiltinSpec.end() ? AddBox(spIt->second) : AddBox("ident", name);
            SetKey(b, "src_" + name);
            b.varName = name;
            if (b.outlets.empty())
               b.outlets.push_back({ name, ValueKind::Number, 0.0 });
            OutRef ref{ b.id, 0, b.outlets[0].kind };
            builtinSources[name] = ref;
            return ref;
         }

         OutRef MakeCodeExpr(const AstNodePtr& node, const std::string& label = "")
         {
            BoxNode& bx = AddBox("code", label);
            bx.rawCode = Slice(node);
            bx.rawPrec = NodePrec(node);
            bx.outlets.push_back({ "out", ValueKind::Number, 0.0 });
            SetKey(bx, "c" + StableHash(stmtCanon + "\x1f" + bx.rawCode));
            return { bx.id, 0, ValueKind::Number };
         }

         OutRef LowerExpr(const AstNodePtr& node)
         {
            if (!node)
            {
               BoxNode& b = AddBox("number", "0");
               b.literalText = "0";
               SetHashKey(b, node);
               return { b.id, 0, ValueKind::Number };
            }

            if (node->kind == AstKind::Binary || node->kind == AstKind::Call)
            {
               for (const SugarTemplate& tpl : SugarTemplates())
               {
                  std::vector<AstNodePtr> binds;
                  if (!MatchTemplate(src, tpl.ast, node, binds) || binds.size() != tpl.uses.size())
                     continue;
                  std::vector<OutRef> refs;
                  for (const AstNodePtr& bnd : binds)
                     refs.push_back(LowerExpr(bnd));
                  BoxNode& bx = AddBox(tpl.id);
                  SetHashKey(bx, node);
                  int id = bx.id;
                  ValueKind outKind = bx.outlets.empty() ? ValueKind::Number : bx.outlets[0].kind;
                  for (size_t k = 0; k < refs.size(); ++k)
                     Connect(refs[k].boxId, refs[k].pinIdx, id, static_cast<int>(k), refs[k].kind);
                  return { id, 0, outKind };
               }
            }

            switch (node->kind)
            {
               case AstKind::Literal:
               {
                  auto* lit = static_cast<const AstLiteral*>(node.get());
                  if (lit->isString)
                     return MakeCodeExpr(node);
                  std::string txt = Slice(node);
                  if (txt.empty())
                     txt = lit->isBool ? (lit->boolValue ? "true" : "false") : FormatDouble(lit->numberValue);
                  BoxNode& b = AddBox("number", txt);
                  b.numberValue = lit->isBool ? (lit->boolValue ? 1.0 : 0.0) : lit->numberValue;
                  b.literalText = txt;
                  SetHashKey(b, node);
                  return { b.id, 0, lit->isBool ? ValueKind::Bool : ValueKind::Number };
               }

               case AstKind::Ident:
               {
                  const std::string& name = static_cast<const AstIdent*>(node.get())->name;
                  auto it = env.find(name);
                  if (it != env.end())
                     return it->second;
                  return GetOrCreateBuiltinSource(name);
               }

               case AstKind::Access:
               {
                  auto* acc = static_cast<const AstAccess*>(node.get());
                  OutRef baseRef = LowerExpr(acc->base);
                  BoxNode& sw = AddBox("swizzle", "." + acc->field);
                  sw.swizzleField = acc->field;
                  SetHashKey(sw, node);
                  int swId = sw.id;
                  int outIdx = 0;
                  ValueKind k = ValueKind::Number;
                  if (acc->field == "x" || acc->field == "y" || acc->field == "z")
                     outIdx = acc->field == "x" ? 0 : (acc->field == "y" ? 1 : 2);
                  else
                  {
                     sw.outlets.clear();
                     k = acc->field.size() > 1 ? ValueKind::Vec3 : ValueKind::Number;
                     sw.outlets.push_back({ acc->field, k, 0.0 });
                  }
                  Connect(baseRef.boxId, baseRef.pinIdx, swId, 0, baseRef.kind);
                  return { swId, outIdx, k };
               }

               case AstKind::Unary:
               {
                  auto* un = static_cast<const AstUnary*>(node.get());
                  // !(a && b) -> not both, !(a || b) -> neither
                  if (un->op == "!" && un->operand && un->operand->kind == AstKind::Binary)
                  {
                     auto* inner = static_cast<const AstBinary*>(un->operand.get());
                     if (inner->op == "&&" || inner->op == "||")
                     {
                        OutRef a = LowerExpr(inner->lhs);
                        OutRef b = LowerExpr(inner->rhs);
                        BoxNode& bx = AddBox(inner->op == "&&" ? "nand" : "nor");
                        SetHashKey(bx, node);
                        int id = bx.id;
                        Connect(a.boxId, a.pinIdx, id, 0, ValueKind::Bool);
                        Connect(b.boxId, b.pinIdx, id, 1, ValueKind::Bool);
                        return { id, 0, ValueKind::Bool };
                     }
                  }
                  OutRef opRef = LowerExpr(un->operand);
                  BoxNode& bx = AddBox(un->op == "!" ? "not" : "neg");
                  SetHashKey(bx, node);
                  int id = bx.id;
                  Connect(opRef.boxId, opRef.pinIdx, id, 0, opRef.kind);
                  return { id, 0, (un->op == "!") ? ValueKind::Bool : opRef.kind };
               }

               case AstKind::Binary:
               {
                  auto* bin = static_cast<const AstBinary*>(node.get());
                  OutRef lhs = LowerExpr(bin->lhs);
                  OutRef rhs = LowerExpr(bin->rhs);

                  std::string specId = "add";
                  ValueKind outKind = ValueKind::Number;
                  if (bin->op == "+") specId = "add";
                  else if (bin->op == "-") specId = "sub";
                  else if (bin->op == "*") specId = "mul";
                  else if (bin->op == "/") specId = "div";
                  else if (bin->op == "%") specId = "mod";
                  else if (bin->op == "^") specId = "pow";
                  else if (bin->op == "<") { specId = "lt"; outKind = ValueKind::Bool; }
                  else if (bin->op == "<=") { specId = "le"; outKind = ValueKind::Bool; }
                  else if (bin->op == ">") { specId = "gt"; outKind = ValueKind::Bool; }
                  else if (bin->op == ">=") { specId = "ge"; outKind = ValueKind::Bool; }
                  else if (bin->op == "==")
                  {
                     specId = (IsBoolLikeExpr(bin->lhs) && IsBoolLikeExpr(bin->rhs)) ? "xnor" : "eq";
                     outKind = ValueKind::Bool;
                  }
                  else if (bin->op == "!=")
                  {
                     specId = (IsBoolLikeExpr(bin->lhs) || IsBoolLikeExpr(bin->rhs)) ? "xor" : "ne";
                     outKind = ValueKind::Bool;
                  }
                  else if (bin->op == "&&") { specId = "and"; outKind = ValueKind::Bool; }
                  else if (bin->op == "||") { specId = "or"; outKind = ValueKind::Bool; }
                  else
                     return MakeCodeExpr(node);

                  BoxNode& bx = AddBox(specId);
                  SetHashKey(bx, node);
                  int id = bx.id;
                  Connect(lhs.boxId, lhs.pinIdx, id, 0, lhs.kind);
                  Connect(rhs.boxId, rhs.pinIdx, id, 1, rhs.kind);
                  return { id, 0, outKind };
               }

               case AstKind::Call:
               {
                  auto* call = static_cast<const AstCall*>(node.get());
                  const std::string& callee = call->callee;
                  size_t dot = callee.find('.');

                  // Neighbour read: X.at(idx)
                  if (dot != std::string::npos && callee.compare(dot, std::string::npos, ".at") == 0 && dot > 0)
                  {
                     BoxNode& bx = AddBox("neighbour", callee);
                     bx.varName = callee.substr(0, dot);
                     SetHashKey(bx, node);
                     SizeInlets(bx, call->args.size());
                     int id = bx.id;
                     ConnectArgs(id, call->args, 0);
                     return { id, 0, ValueKind::Vec3 };
                  }

                  // reduce.max(x), reduce.rms(in, lo, hi), ...
                  if (callee.rfind("reduce.", 0) == 0 && callee.size() > 7 && callee.find('.', 7) == std::string::npos)
                  {
                     BoxNode& bx = AddBox("reduce", callee);
                     bx.swizzleField = callee.substr(7);
                     SetHashKey(bx, node);
                     SizeInlets(bx, call->args.size());
                     int id = bx.id;
                     ConnectArgs(id, call->args, 0);
                     return { id, 0, ValueKind::Number };
                  }

                  if (const BoxSpec* spec = SpecForCall(callee))
                  {
                     BoxNode& bx = AddBox(spec->id);
                     bx.callName = callee;
                     SetHashKey(bx, node);
                     SizeInlets(bx, call->args.size());
                     int id = bx.id;
                     ValueKind outKind = bx.outlets[0].kind;
                     ConnectArgs(id, call->args, 0);
                     return { id, 0, outKind };
                  }

                  return MakeCodeExpr(node, callee);
               }

               default:
                  return MakeCodeExpr(node);
            }
         }

         // The statement becomes one code box holding its source text, plus a
         // comment at the end of its last line.
         void AddCodeBoxStmt(int whenGroupId)
         {
            size_t hi = stmtHi;
            size_t p = hi;
            while (p < src.size() && (src[p] == ' ' || src[p] == '\t' || src[p] == ';'))
               ++p;
            if (p < src.size() && src[p] == '#')
               hi = LineEnd(p);
            BoxNode& bx = AddBox("code");
            bx.rawCode = src.substr(stmtLo, hi - stmtLo);
            while (!bx.rawCode.empty() && std::isspace(static_cast<unsigned char>(bx.rawCode.back())))
               bx.rawCode.pop_back();
            bx.stmtOrder = nextStmtOrder++;
            bx.whenGroupId = whenGroupId;
            SetKey(bx, "c" + StableHash(bx.rawCode));
            TakeCommentsBefore(hi); // already inside rawCode
         }

         int MakePlayNote(const AstCall* call, int whenGroupId)
         {
            BoxNode& pn = AddBox("play_note");
            pn.stmtOrder = nextStmtOrder++;
            pn.whenGroupId = whenGroupId;
            SizeInlets(pn, call->args.size() + 1);
            SetHashKey(pn, nullptr);
            int id = pn.id;
            ConnectArgs(id, call->args, 1);
            return id;
         }

         // Lowers one statement, keeping the comments that lead it (and the one
         // at the end of its line) on the box or group it becomes.
         void LowerStmt(const AstNodePtr& stmt, int whenGroupId = -1)
         {
            if (!stmt)
               return;
            size_t lo = 0, hi = 0;
            bool hasRange = NodeExtent(src, stmt, lo, hi);
            const std::string savedCanon = stmtCanon;
            const size_t savedLo = stmtLo, savedHi = stmtHi;
            stmtCanon = Canon(src, stmt);
            stmtLo = lo;
            stmtHi = hi;

            std::string lead = JoinComments(pendingComment, hasRange ? TakeCommentsBefore(lo) : std::string());
            pendingComment.clear();
            const int orderBefore = nextStmtOrder;
            const size_t boxesBefore = graph.boxes.size();
            const size_t groupsBefore = graph.groups.size();

            // A comment inside a statement cannot ride on a box: keep it as code.
            bool commentInside = false;
            for (size_t c = commentIdx; hasRange && c < comments.size() && comments[c].offset < hi; ++c)
               commentInside = commentInside || comments[c].offset > lo;
            if (commentInside)
               AddCodeBoxStmt(whenGroupId);
            else
               LowerStmtBody(stmt, whenGroupId, hasRange);

            if (hasRange)
               lead = JoinComments(lead, TakeCommentsBefore(LineEnd(hi)));
            if (!lead.empty())
            {
               bool attached = false;
               for (size_t gi = groupsBefore; gi < graph.groups.size() && !attached; ++gi)
               {
                  if (graph.groups[gi].stmtOrder == orderBefore)
                  {
                     graph.groups[gi].comment = JoinComments(lead, graph.groups[gi].comment);
                     attached = true;
                  }
               }
               for (size_t bi = boxesBefore; bi < graph.boxes.size() && !attached; ++bi)
               {
                  if (graph.boxes[bi].stmtOrder == orderBefore)
                  {
                     graph.boxes[bi].comment = JoinComments(lead, graph.boxes[bi].comment);
                     attached = true;
                  }
               }
               if (!attached)
                  pendingComment = lead;
            }
            stmtCanon = savedCanon;
            stmtLo = savedLo;
            stmtHi = savedHi;
         }

         void LowerStmtBody(const AstNodePtr& stmt, int whenGroupId, bool hasRange)
         {
            if (!hasRange)
               return; // nothing in the source to keep

            switch (stmt->kind)
            {
               case AstKind::DeclParam:
               {
                  auto* p = static_cast<const AstDeclParam*>(stmt.get());
                  const AstNode* init = p->initExpr.get();
                  if (init && init->kind == AstKind::Unary)
                     init = static_cast<const AstUnary*>(init)->operand.get();
                  if (init && (init->kind != AstKind::Literal || static_cast<const AstLiteral*>(init)->isString))
                  {
                     AddCodeBoxStmt(whenGroupId);
                     break;
                  }
                  BoxNode& bx = AddBox("knob", p->name);
                  SetKey(bx, "k_" + p->name);
                  bx.varName = p->name;
                  bx.varType = p->typeName;
                  bx.numberValue = p->defaultValue;
                  bx.minVal = p->minVal;
                  bx.maxVal = p->maxVal;
                  bx.stmtOrder = nextStmtOrder++;
                  bx.whenGroupId = whenGroupId;
                  env[p->name] = { bx.id, 0, ValueKind::Number };
                  break;
               }

               case AstKind::DeclState:
               {
                  auto* st = static_cast<const AstDeclState*>(stmt.get());
                  if (st->tableSize > 0 || ExprNeedsCodeBox(st->initExpr, info))
                  {
                     AddCodeBoxStmt(whenGroupId);
                     break;
                  }
                  ValueKind k = (st->typeName == "vec3") ? ValueKind::Vec3 : ValueKind::Number;
                  BoxNode& bx = AddBox("previous", st->name);
                  int bxId = bx.id;
                  SetKey(bx, "s_" + st->name);
                  bx.varName = st->name;
                  bx.varType = st->typeName;
                  bx.boundary = st->boundary;
                  bx.stmtOrder = nextStmtOrder++;
                  bx.whenGroupId = whenGroupId;
                  bx.outlets[0].kind = k;
                  if (st->initExpr)
                  {
                     OutRef initRef = LowerExpr(st->initExpr);
                     Connect(initRef.boxId, initRef.pinIdx, bxId, 1, initRef.kind);
                  }
                  env[st->name] = { bxId, 0, k };
                  break;
               }

               case AstKind::DeclAttrib:
               {
                  auto* a = static_cast<const AstDeclAttrib*>(stmt.get());
                  if (ExprNeedsCodeBox(a->initExpr, info))
                  {
                     AddCodeBoxStmt(whenGroupId);
                     break;
                  }
                  BoxNode& bx = AddBox("attrib", a->name);
                  int bxId = bx.id;
                  SetKey(bx, "a_" + a->name);
                  bx.varName = a->name;
                  bx.varType = a->typeName;
                  bx.stmtOrder = nextStmtOrder++;
                  bx.whenGroupId = whenGroupId;
                  if (a->initExpr)
                  {
                     OutRef initRef = LowerExpr(a->initExpr);
                     Connect(initRef.boxId, initRef.pinIdx, bxId, 0, initRef.kind);
                  }
                  env[a->name] = { bxId, 0, ValueKind::PerPoint };
                  break;
               }

               case AstKind::DeclOutput:
               {
                  auto* o = static_cast<const AstDeclOutput*>(stmt.get());
                  if (!o->initExpr || ExprNeedsCodeBox(o->initExpr, info))
                  {
                     AddCodeBoxStmt(whenGroupId);
                     break;
                  }
                  BoxNode& bx = AddBox("output_pin", o->name);
                  int bxId = bx.id;
                  SetKey(bx, "out_" + o->name);
                  bx.varName = o->name;
                  bx.varType = o->typeName;
                  bx.domainName = o->domainName;
                  bx.stmtOrder = nextStmtOrder++;
                  bx.whenGroupId = whenGroupId;
                  bx.inlets.push_back({ o->name, ValueKind::Number, 0.0 });
                  OutRef valRef = LowerExpr(o->initExpr);
                  Connect(valRef.boxId, valRef.pinIdx, bxId, 0, valRef.kind);
                  break;
               }

               case AstKind::DeclInput:
               {
                  auto* in = static_cast<const AstDeclInput*>(stmt.get());
                  BoxNode& bx = AddBox("input_pin", in->name);
                  SetKey(bx, "in_" + in->name);
                  bx.varName = in->name;
                  bx.varType = in->typeName;
                  bx.domainName = in->domainName;
                  bx.stmtOrder = nextStmtOrder++;
                  bx.whenGroupId = whenGroupId;
                  bx.outlets.push_back({ in->name, ValueKind::Any, 0.0 });
                  env[in->name] = { bx.id, 0, ValueKind::Any };
                  break;
               }

               case AstKind::Assign:
                  LowerAssign(static_cast<const AstAssign*>(stmt.get()), whenGroupId);
                  break;

               case AstKind::If:
                  LowerIf(static_cast<const AstIf*>(stmt.get()), whenGroupId);
                  break;

               case AstKind::Call:
               {
                  auto* call = static_cast<const AstCall*>(stmt.get());
                  if (call->callee == "note" && !ExprNeedsCodeBox(stmt, info))
                  {
                     MakePlayNote(call, whenGroupId);
                     break;
                  }
                  AddCodeBoxStmt(whenGroupId);
                  break;
               }

               default:
                  AddCodeBoxStmt(whenGroupId);
                  break;
            }
         }

         void LowerAssign(const AstAssign* a, int whenGroupId)
         {
            if (ExprNeedsCodeBox(a->rvalue, info) || !a->lvalue || a->lvalue->kind == AstKind::Index)
            {
               AddCodeBoxStmt(whenGroupId);
               return;
            }

            std::string targetBase;
            std::string targetField;
            if (a->lvalue->kind == AstKind::Ident)
               targetBase = static_cast<const AstIdent*>(a->lvalue.get())->name;
            else if (a->lvalue->kind == AstKind::Access)
            {
               auto* acc = static_cast<const AstAccess*>(a->lvalue.get());
               if (acc->base && acc->base->kind == AstKind::Ident)
               {
                  targetBase = static_cast<const AstIdent*>(acc->base.get())->name;
                  targetField = acc->field;
               }
            }
            const bool multiSwizzleP = targetBase == "P" && !targetField.empty() &&
                                       targetField != "x" && targetField != "y" && targetField != "z";
            if (targetBase.empty() || multiSwizzleP)
            {
               AddCodeBoxStmt(whenGroupId);
               return;
            }

            OutRef rhsRef = LowerExpr(a->rvalue);

            auto sink = [&](const std::string& specId, const std::string& varName, int pin) {
               BoxNode& bx = AddBox(specId);
               bx.varName = varName;
               bx.assignOp = a->op;
               bx.swizzleField = targetField;
               bx.stmtOrder = nextStmtOrder++;
               bx.whenGroupId = whenGroupId;
               SetKey(bx, varName + (targetField.empty() ? "" : "." + targetField) + "_out");
               Connect(rhsRef.boxId, rhsRef.pinIdx, bx.id, pin, rhsRef.kind);
            };

            if (targetBase == "P")
            {
               int pin = targetField == "x" ? 0 : (targetField == "y" ? 1 : (targetField == "z" ? 2 : 3));
               sink("position_out", "P", pin);
               return;
            }
            if (targetField.empty() && a->op == "=")
            {
               if (targetBase == "N") { sink("normal_out", "N", 0); return; }
               if (targetBase == "Cd") { sink("color_out", "Cd", 0); return; }
               if (targetBase == "publish") { sink(rhsRef.kind == ValueKind::Pulse ? "pulse_out" : "value_out", "publish", 0); return; }
            }

            const bool isState = info.stateNames.count(targetBase) > 0;
            auto cntIt = info.assignCounts.find(targetBase);
            const bool isReassigned = cntIt != info.assignCounts.end() && cntIt->second > 1;
            const bool explicitAssign = isState || !targetField.empty() || a->op != "=" || isReassigned || whenGroupId >= 0;

            // A single `name = expr` names the box that computes expr, unless that
            // box is a source, a declaration or already named (`x = t`, `y = x`):
            // those get an assign box, so a shared source is never renamed.
            if (!explicitAssign)
            {
               BoxNode* prod = graph.FindBox(rhsRef.boxId);
               if (prod && prod->wireName.empty() && prod->stmtOrder < 0 &&
                   !IsSourceSpecId(prod->specId) && !IsDeclSpecId(prod->specId) && prod->specId != "assign_var" &&
                   (prod->outlets.size() <= 1 || prod->specId == "swizzle"))
               {
                  prod->wireName = targetBase;
                  prod->stmtOrder = nextStmtOrder++;
                  SetKey(*prod, "w_" + targetBase);
                  env[targetBase] = rhsRef;
                  return;
               }
            }

            BoxNode& bx = AddBox("assign_var", targetBase + (targetField.empty() ? "" : "." + targetField) + " " + a->op);
            bx.varName = targetBase;
            bx.swizzleField = targetField;
            bx.assignOp = a->op;
            bx.stmtOrder = nextStmtOrder++;
            bx.whenGroupId = whenGroupId;
            bx.inlets.push_back({ "val", rhsRef.kind, 0.0 });
            bx.outlets.push_back({ targetBase, rhsRef.kind, 0.0 });
            SetKey(bx, "v_" + targetBase + (targetField.empty() ? "" : "." + targetField));
            int id = bx.id;
            Connect(rhsRef.boxId, rhsRef.pinIdx, id, 0, rhsRef.kind);
            if (!isState && targetField.empty() && a->op == "=")
               env[targetBase] = { id, 0, rhsRef.kind };
         }

         void LowerIf(const AstIf* iff, int whenGroupId)
         {
            if (iff->elseBlock || whenGroupId >= 0 || ExprNeedsCodeBox(iff->cond, info))
            {
               AddCodeBoxStmt(whenGroupId);
               return;
            }

            std::vector<AstNodePtr> thenStmts;
            if (iff->thenBlock && iff->thenBlock->kind == AstKind::Block)
               thenStmts = static_cast<const AstBlock*>(iff->thenBlock.get())->statements;
            else if (iff->thenBlock)
               thenStmts.push_back(iff->thenBlock);

            bool onlyNotes = !thenStmts.empty();
            for (const auto& s : thenStmts)
            {
               if (!s)
                  continue;
               if (s->kind == AstKind::If || s->kind == AstKind::For || s->kind == AstKind::Map)
               {
                  AddCodeBoxStmt(whenGroupId);
                  return;
               }
               if (s->kind != AstKind::Call || static_cast<const AstCall*>(s.get())->callee != "note" ||
                   ExprNeedsCodeBox(s, info))
                  onlyNotes = false;
            }

            OutRef condRef = LowerExpr(iff->cond);

            if (onlyNotes)
            {
               for (const auto& s : thenStmts)
               {
                  int pnId = MakePlayNote(static_cast<const AstCall*>(s.get()), -1);
                  Connect(condRef.boxId, condRef.pinIdx, pnId, 0, ValueKind::Pulse);
               }
               return;
            }

            BoxGroup grp;
            grp.id = nextGroupId++;
            grp.name = "when";
            grp.isWhenGroup = true;
            grp.condBoxId = condRef.boxId;
            grp.condPinIdx = condRef.pinIdx;
            grp.stmtOrder = nextStmtOrder++;
            graph.groups.push_back(grp);
            const size_t grpIdx = graph.groups.size() - 1;
            const int gid = grp.id;

            const size_t boxStart = graph.boxes.size();
            for (const auto& s : thenStmts)
               LowerStmt(s, gid);
            for (size_t bi = boxStart; bi < graph.boxes.size(); ++bi)
            {
               if (graph.boxes[bi].whenGroupId == gid)
                  graph.groups[grpIdx].boxIds.push_back(graph.boxes[bi].id);
            }
         }
      };
   }

   namespace
   {
      // Prints a graph from its wires. A statement box (declaration, sink,
      // assignment, named box, code statement, note) is one line; everything it
      // reads is inlined, except a box read more than once, which is named first.
      struct Printer
      {
         const BoxGraph& graph;
         std::unordered_map<int, const BoxNode*> byId;
         std::map<std::pair<int, int>, const BoxWire*> wireTo;
         std::unordered_map<int, std::vector<const BoxWire*>> wiresFrom;
         std::unordered_map<int, int> groupOf; // box id -> when-group id
         std::unordered_map<int, int> readerMemo;
         std::unordered_map<int, std::string> tempNames;
         std::unordered_set<std::string> usedNames;
         int tempCounter = 0;

         explicit Printer(const BoxGraph& g) : graph(g)
         {
            for (const BoxNode& b : graph.boxes)
            {
               byId[b.id] = &b;
               for (const std::string* n : { &b.wireName, &b.varName })
               {
                  if (!n->empty())
                     usedNames.insert(*n);
               }
               if (b.whenGroupId >= 0)
                  groupOf[b.id] = b.whenGroupId;
            }
            for (const BoxWire& w : graph.wires)
            {
               wireTo[{ w.dstBoxId, w.dstPinIdx }] = &w;
               wiresFrom[w.srcBoxId].push_back(&w);
            }
            for (const BoxGroup& grp : graph.groups)
            {
               if (!grp.isWhenGroup)
                  continue;
               for (int id : grp.boxIds)
                  groupOf[id] = grp.id;
            }
         }

         const BoxNode* Box(int id) const
         {
            auto it = byId.find(id);
            return it == byId.end() ? nullptr : it->second;
         }

         const BoxWire* WireTo(int boxId, int pin) const
         {
            auto it = wireTo.find({ boxId, pin });
            return it == wireTo.end() ? nullptr : it->second;
         }

         static bool IsSink(const std::string& id)
         {
            return id == "position_out" || id == "normal_out" || id == "color_out" ||
                   id == "value_out" || id == "pulse_out" || id == "output_pin";
         }

         bool IsCodeStmt(const BoxNode& b) const { return b.specId == "code" && b.outlets.empty(); }

         // Boxes that are printed as their own statement.
         bool IsStatementBox(const BoxNode& b) const
         {
            return IsDeclSpecId(b.specId) || IsSink(b.specId) || b.specId == "assign_var" ||
                   b.specId == "play_note" || IsCodeStmt(b) || !b.wireName.empty();
         }

         // Distinct reads of a box's outputs. Reads through an unnamed swizzle
         // count as reads of the box; notes sharing one trigger count once.
         int Readers(int boxId)
         {
            auto memo = readerMemo.find(boxId);
            if (memo != readerMemo.end())
               return memo->second;
            readerMemo[boxId] = 0; // cycle guard
            std::set<std::pair<int, int>> keys;
            int extra = 0;
            auto it = wiresFrom.find(boxId);
            if (it != wiresFrom.end())
            {
               for (const BoxWire* w : it->second)
               {
                  const BoxNode* dst = Box(w->dstBoxId);
                  if (!dst)
                     continue;
                  if (const SugarTemplate* tpl = FindSugarTemplate(dst->specId))
                  {
                     if (w->dstPinIdx < static_cast<int>(tpl->uses.size()) && tpl->uses[w->dstPinIdx] > 1 && IsImpure(boxId))
                        ++extra;
                  }
                  if (dst->specId == "swizzle" && dst->wireName.empty())
                     extra += std::max(1, Readers(dst->id));
                  else if (dst->specId == "play_note" && w->dstPinIdx == 0 && groupOf.count(dst->id) == 0)
                     keys.insert({ -1, w->srcPinIdx });
                  else
                     keys.insert({ w->dstBoxId, w->dstPinIdx });
               }
            }
            for (const BoxGroup& grp : graph.groups)
            {
               if (grp.isWhenGroup && grp.condBoxId == boxId)
                  keys.insert({ -2 - grp.id, 0 });
            }
            int n = static_cast<int>(keys.size()) + extra;
            readerMemo[boxId] = n;
            return n;
         }

         // A box whose inlined value changes per read (rand), so it must not be duplicated.
         bool IsImpure(int boxId, int depth = 0)
         {
            const BoxNode* b = Box(boxId);
            if (!b || depth > 64)
               return false;
            if (b->specId == "rand" || (b->specId == "code" && b->rawCode.find("rand(") != std::string::npos))
               return true;
            if (!b->wireName.empty() || IsDeclSpecId(b->specId) || b->specId == "assign_var")
               return false;
            for (int pin = 0; pin < static_cast<int>(b->inlets.size()); ++pin)
            {
               if (const BoxWire* w = WireTo(boxId, pin))
               {
                  if (IsImpure(w->srcBoxId, depth + 1))
                     return true;
               }
            }
            return false;
         }

         // A box nothing reads yet (just placed, pasted or unwired) keeps its own named line, so it survives the
         // round trip through the code instead of vanishing until it is wired.
         bool IsDangling(const BoxNode& b)
         {
            if (IsStatementBox(b) || b.specId == "code" || b.specId == "ident" || b.outlets.empty())
               return false;
            return Readers(b.id) == 0;
         }

         bool IsTemp(const BoxNode& b)
         {
            if (IsStatementBox(b) || b.specId == "number" || b.specId == "swizzle" || IsSourceSpecId(b.specId))
               return false;
            if (b.outlets.size() != 1)
               return false;
            return Readers(b.id) > 1;
         }

         const std::string& TempName(int boxId)
         {
            auto it = tempNames.find(boxId);
            if (it != tempNames.end())
               return it->second;
            std::string name;
            do
               name = "v" + std::to_string(++tempCounter);
            while (usedNames.count(name) > 0);
            usedNames.insert(name);
            return tempNames[boxId] = name;
         }

         // The name a box's output is read by, or "" when it is inlined.
         std::string NameOf(const BoxNode& b)
         {
            if (!b.wireName.empty())
               return b.wireName;
            if (IsDeclSpecId(b.specId) || b.specId == "assign_var")
               return b.varName;
            if (IsTemp(b))
               return TempName(b.id);
            return "";
         }

         static std::string Number(double v, int parentPrec)
         {
            std::string s = FormatDouble(v);
            return (v < 0.0 && parentPrec > 0) ? "(" + s + ")" : s;
         }

         std::string Inlet(const BoxNode& b, int pin, int parentPrec)
         {
            if (const BoxWire* w = WireTo(b.id, pin))
               return Ref(w->srcBoxId, w->srcPinIdx, parentPrec);
            double def = pin >= 0 && pin < static_cast<int>(b.inlets.size()) ? b.inlets[pin].defaultValue : 0.0;
            return Number(def, parentPrec);
         }

         std::string Args(const BoxNode& b, int first = 0)
         {
            std::string s;
            for (int i = first; i < static_cast<int>(b.inlets.size()); ++i)
            {
               if (i > first)
                  s += ", ";
               s += Inlet(b, i, 0);
            }
            return s;
         }

         std::string Ref(int boxId, int pin, int parentPrec)
         {
            const BoxNode* b = Box(boxId);
            if (!b)
               return "0";
            std::string name = NameOf(*b);
            if (!name.empty())
               return name;
            return Body(*b, pin, parentPrec);
         }

         static std::string OperatorOf(const BoxNode& b, const BoxSpec* spec)
         {
            if (!b.callName.empty())
               return "";
            if (b.specId == "pow") return "^";
            if (b.specId == "mod") return "%";
            if (b.specId == "xor") return "!=";
            if (b.specId == "xnor") return "==";
            if (spec && b.inlets.size() == 2 && BinaryPrec(spec->fieldWord) > 0)
               return spec->fieldWord;
            return "";
         }

         std::string Binary(const BoxNode& b, const std::string& op, int parentPrec)
         {
            int p = BinaryPrec(op);
            bool rightAssoc = op == "^";
            std::string s = Inlet(b, 0, rightAssoc ? p : p - 1) + " " + op + " " + Inlet(b, 1, rightAssoc ? p - 1 : p);
            return Wrap(s, p, parentPrec);
         }

         // The expression a box computes (ignoring any name it has).
         std::string Body(const BoxNode& b, int pin, int parentPrec)
         {
            const std::string& id = b.specId;
            const BoxSpec* spec = FindBoxSpec(id);

            if (id == "number")
            {
               if (!b.literalText.empty())
                  return (b.literalText[0] == '-' && parentPrec > 0) ? "(" + b.literalText + ")" : b.literalText;
               return Number(b.numberValue, parentPrec);
            }
            if (id == "code")
               return b.rawPrec > parentPrec ? b.rawCode : "(" + b.rawCode + ")";
            if (id == "ident")
               return b.varName.empty() ? b.label : b.varName;
            if (id == "notes_in")
            {
               static const char* kNames[] = { "noteNum", "noteVel", "noteOn" };
               return kNames[std::clamp(pin, 0, 2)];
            }
            if (id == "geometry_in")
            {
               static const char* kNames[] = { "P", "N", "Cd" };
               return kNames[std::clamp(pin, 0, 2)];
            }
            if (IsSourceSpecId(id) && spec)
               return b.varName.empty() ? spec->fieldWord : b.varName;
            if (IsDeclSpecId(id) || id == "assign_var")
               return b.varName;

            if (id == "swizzle")
            {
               std::string field = b.swizzleField;
               if (field.empty() && pin >= 0 && pin < static_cast<int>(b.outlets.size()))
                  field = b.outlets[pin].name;
               return Wrap(Inlet(b, 0, kPrecPow) + "." + field, kPrecPostfix, parentPrec);
            }
            if (id == "neighbour")
               return Wrap(b.varName + ".at(" + Args(b) + ")", kPrecPostfix, parentPrec);
            if (id == "reduce")
               return Wrap("reduce." + b.swizzleField + "(" + Args(b) + ")", kPrecPostfix, parentPrec);

            if (id == "neg" || id == "not")
               return Wrap(std::string(id == "neg" ? "-" : "!") + Inlet(b, 0, kPrecPostfix), kPrecUnary, parentPrec);
            if (id == "nand" || id == "nor")
            {
               std::string op = id == "nand" ? "&&" : "||";
               int p = BinaryPrec(op);
               return Wrap("!(" + Inlet(b, 0, p - 1) + " " + op + " " + Inlet(b, 1, p) + ")", kPrecUnary, parentPrec);
            }
            if (const SugarTemplate* tpl = FindSugarTemplate(id))
            {
               std::string out;
               size_t slot = 0;
               for (size_t c = 0; c < tpl->text.size(); ++c)
               {
                  if (tpl->text[c] != '$')
                  {
                     out += tpl->text[c];
                     continue;
                  }
                  int k = tpl->text[c + 1] - '0';
                  ++c;
                  const int ctxPrec = slot < tpl->ctx.size() ? tpl->ctx[slot] : kPrecPostfix;
                  ++slot;
                  std::string arg = Inlet(b, k, ctxPrec);
                  if (!WireTo(b.id, k) && k < static_cast<int>(b.inlets.size()) && b.inlets[k].kind == ValueKind::Vec3)
                     arg = "vec3(" + Inlet(b, k, 0) + ")";
                  const bool dotAfter = c + 1 < tpl->text.size() && tpl->text[c + 1] == '.';
                  if (dotAfter && !arg.empty() && (std::isdigit(static_cast<unsigned char>(arg[0])) || arg[0] == '.'))
                     arg = "(" + arg + ")";
                  out += arg;
               }
               return Wrap(out, tpl->prec, parentPrec);
            }

            std::string op = OperatorOf(b, spec);
            if (!op.empty())
               return Binary(b, op, parentPrec);

            std::string fn = !b.callName.empty() ? b.callName : (spec ? spec->fieldWord : id);
            return fn + "(" + Args(b) + ")";
         }

         static std::string Indent(const std::string& text, const std::string& indent)
         {
            std::string out;
            std::istringstream iss(text);
            std::string line;
            while (std::getline(iss, line))
               out += indent + line + "\n";
            return out;
         }

         // One statement box as a line (no trailing newline).
         std::string Statement(const BoxNode& b)
         {
            const std::string type = b.varType.empty() ? "float" : b.varType;
            const std::string& id = b.specId;
            if (id == "code")
               return b.wireName.empty() ? b.rawCode : b.wireName + " = " + b.rawCode;
            if (id == "knob")
               return "param " + type + " " + b.varName + " = " + FormatDouble(b.numberValue) +
                      " [" + FormatDouble(b.minVal) + ", " + FormatDouble(b.maxVal) + "]";
            if (id == "previous")
            {
               std::string s = "state " + type + " " + b.varName + " = " + Inlet(b, 1, 0);
               if (!b.boundary.empty())
                  s += " [" + b.boundary + "]";
               return s;
            }
            if (id == "attrib")
            {
               std::string s = "attrib " + type + " " + b.varName;
               if (WireTo(b.id, 0))
                  s += " = " + Inlet(b, 0, 0);
               return s;
            }
            if (id == "input_pin" || id == "output_pin")
            {
               std::string s = id == "input_pin" ? "input" : "output";
               if (!b.domainName.empty())
                  s += " " + b.domainName;
               s += " " + type + " " + b.varName;
               if (id == "output_pin")
                  s += " = " + Inlet(b, 0, 0);
               return s;
            }
            if (id == "position_out")
            {
               int pin = b.swizzleField == "x" ? 0 : (b.swizzleField == "y" ? 1 : (b.swizzleField == "z" ? 2 : 3));
               if (b.swizzleField.empty() && !WireTo(b.id, 3))
               {
                  // A hand-built box wired per axis.
                  std::string s;
                  for (int i = 0; i < 3; ++i)
                  {
                     if (WireTo(b.id, i))
                        s += std::string(s.empty() ? "" : "\n") + "P." + "xyz"[i] + " = " + Inlet(b, i, 0);
                  }
                  return s;
               }
               return (b.swizzleField.empty() ? "P" : "P." + b.swizzleField) + " " + b.assignOp + " " + Inlet(b, pin, 0);
            }
            if (id == "normal_out" || id == "color_out")
            {
               std::string def = id == "normal_out" ? "vec3(0, 1, 0)" : "vec3(1, 1, 1)";
               return std::string(id == "normal_out" ? "N" : "Cd") + " = " + (WireTo(b.id, 0) ? Inlet(b, 0, 0) : def);
            }
            if (id == "value_out" || id == "pulse_out")
               return "publish = " + Inlet(b, 0, 0);
            if (id == "assign_var")
               return b.varName + (b.swizzleField.empty() ? "" : "." + b.swizzleField) + " " + b.assignOp + " " + Inlet(b, 0, 0);
            if (id == "play_note")
               return "note(" + Args(b, 1) + ")";
            if (!b.wireName.empty())
               return b.wireName + " = " + Body(b, 0, 0);
            return "";
         }

         struct Unit
         {
            enum Kind { BoxStmt, Temp, StateWrite, Group, Notes } kind = BoxStmt;
            int id = 0;            // box id, or group id
            std::vector<int> notes; // Notes: play_note box ids
            int order = INT_MAX;
            float x = 0.0f;
            int tie = 0;
         };

         std::string Print()
         {
            std::vector<Unit> units;
            std::unordered_map<int, int> unitOfBox;
            std::map<std::pair<int, int>, int> notesUnitBySource;

            auto addUnit = [&](Unit u, const BoxNode* b) {
               if (b)
               {
                  u.order = b->stmtOrder >= 0 ? b->stmtOrder : (u.kind == Unit::StateWrite ? INT_MAX : -1);
                  u.x = b->x;
                  u.tie = b->id;
               }
               units.push_back(u);
               return static_cast<int>(units.size()) - 1;
            };

            for (const BoxNode& b : graph.boxes)
            {
               if (groupOf.count(b.id) > 0)
                  continue;
               if (b.specId == "play_note")
               {
                  const BoxWire* w = WireTo(b.id, 0);
                  if (w)
                  {
                     auto key = std::make_pair(w->srcBoxId, w->srcPinIdx);
                     auto it = notesUnitBySource.find(key);
                     if (it != notesUnitBySource.end())
                     {
                        units[it->second].notes.push_back(b.id);
                        unitOfBox[b.id] = it->second;
                        continue;
                     }
                     Unit u;
                     u.kind = Unit::Notes;
                     u.notes.push_back(b.id);
                     int ui = addUnit(u, &b);
                     notesUnitBySource[key] = ui;
                     unitOfBox[b.id] = ui;
                     continue;
                  }
               }
               if (IsStatementBox(b))
               {
                  Unit u;
                  u.id = b.id;
                  unitOfBox[b.id] = addUnit(u, &b);
               }
               else if (IsTemp(b) || IsDangling(b))
               {
                  Unit u;
                  u.kind = Unit::Temp;
                  u.id = b.id;
                  unitOfBox[b.id] = addUnit(u, &b);
               }
            }
            std::vector<int> stateWriteUnits;
            for (const BoxNode& b : graph.boxes)
            {
               if (b.specId == "previous" && WireTo(b.id, 0))
               {
                  Unit u;
                  u.kind = Unit::StateWrite;
                  u.id = b.id;
                  stateWriteUnits.push_back(addUnit(u, &b));
               }
            }
            for (const BoxGroup& grp : graph.groups)
            {
               if (!grp.isWhenGroup)
                  continue;
               Unit u;
               u.kind = Unit::Group;
               u.id = grp.id;
               u.order = grp.stmtOrder >= 0 ? grp.stmtOrder : -1;
               u.tie = INT_MAX / 2 + grp.id;
               float minX = 1e30f;
               for (const BoxNode& b : graph.boxes)
               {
                  auto g = groupOf.find(b.id);
                  if (g != groupOf.end() && g->second == grp.id)
                  {
                     minX = std::min(minX, b.x);
                     if (grp.stmtOrder < 0 && b.stmtOrder >= 0)
                        u.order = u.order < 0 ? b.stmtOrder : std::min(u.order, b.stmtOrder);
                  }
               }
               u.x = minX < 1e29f ? minX : 0.0f;
               int ui = static_cast<int>(units.size());
               units.push_back(u);
               for (const BoxNode& b : graph.boxes)
               {
                  auto g = groupOf.find(b.id);
                  if (g != groupOf.end() && g->second == grp.id)
                     unitOfBox[b.id] = ui;
               }
            }

            // Dependencies: walk each unit's inlined inputs until a named box.
            std::vector<std::set<int>> deps(units.size());
            for (size_t ui = 0; ui < units.size(); ++ui)
            {
               std::unordered_set<int> seen;
               std::function<void(int, int)> walkInlets;
               std::function<void(int)> walkSource = [&](int srcId) {
                  auto u = unitOfBox.find(srcId);
                  if (u != unitOfBox.end() && u->second != static_cast<int>(ui))
                  {
                     deps[ui].insert(u->second);
                     return;
                  }
                  if (u != unitOfBox.end())
                     return; // own box
                  walkInlets(srcId, -1);
               };
               walkInlets = [&](int boxId, int onlyPin) {
                  if (!seen.insert(boxId * 64 + (onlyPin + 1)).second)
                     return;
                  const BoxNode* b = Box(boxId);
                  if (!b)
                     return;
                  for (int pin = 0; pin < static_cast<int>(b->inlets.size()); ++pin)
                  {
                     if (onlyPin >= 0 && pin != onlyPin)
                        continue;
                     if (b->specId == "previous" && onlyPin < 0 && pin == 0)
                        continue; // the next value is written later, not read now
                     if (const BoxWire* w = WireTo(boxId, pin))
                        walkSource(w->srcBoxId);
                  }
               };
               const Unit& u = units[ui];
               switch (u.kind)
               {
                  case Unit::BoxStmt:
                  case Unit::Temp: walkInlets(u.id, -1); break;
                  case Unit::StateWrite:
                     walkInlets(u.id, 0);
                     deps[ui].insert(unitOfBox[u.id]);
                     break;
                  case Unit::Notes:
                     for (int nid : u.notes)
                        walkInlets(nid, -1);
                     break;
                  case Unit::Group:
                  {
                     for (const BoxGroup& grp : graph.groups)
                     {
                        if (grp.id == u.id)
                           walkSource(grp.condBoxId);
                     }
                     for (const BoxNode& b : graph.boxes)
                     {
                        auto g = groupOf.find(b.id);
                        if (g != groupOf.end() && g->second == u.id)
                           walkInlets(b.id, -1);
                     }
                     break;
                  }
               }
               deps[ui].erase(static_cast<int>(ui));
            }

            // Kahn, ties in source order then left to right.
            auto before = [&](int a, int b) {
               const Unit& ua = units[a];
               const Unit& ub = units[b];
               return std::tie(ua.order, ua.x, ua.tie) < std::tie(ub.order, ub.x, ub.tie);
            };
            std::vector<bool> done(units.size(), false);
            std::vector<int> sequence;
            for (size_t n = 0; n < units.size(); ++n)
            {
               int pick = -1;
               for (size_t ui = 0; ui < units.size(); ++ui)
               {
                  if (done[ui])
                     continue;
                  bool ready = true;
                  for (int d : deps[ui])
                     ready = ready && done[d];
                  if (ready && (pick < 0 || before(static_cast<int>(ui), pick)))
                     pick = static_cast<int>(ui);
               }
               if (pick < 0) // a cycle: take the earliest remaining
               {
                  for (size_t ui = 0; ui < units.size(); ++ui)
                  {
                     if (!done[ui] && (pick < 0 || before(static_cast<int>(ui), pick)))
                        pick = static_cast<int>(ui);
                  }
               }
               done[pick] = true;
               sequence.push_back(pick);
            }

            std::string out;
            for (int ui : sequence)
            {
               const Unit& u = units[ui];
               switch (u.kind)
               {
                  case Unit::BoxStmt:
                  {
                     const BoxNode& b = *Box(u.id);
                     out += Indent(b.comment, "");
                     std::string s = Statement(b);
                     if (!s.empty())
                        out += s + "\n";
                     break;
                  }
                  case Unit::Temp:
                  {
                     const BoxNode& b = *Box(u.id);
                     out += Indent(b.comment, "");
                     out += TempName(b.id) + " = " + Body(b, 0, 0) + "\n";
                     break;
                  }
                  case Unit::StateWrite:
                  {
                     const BoxNode& b = *Box(u.id);
                     out += b.varName + " = " + Inlet(b, 0, 0) + "\n";
                     break;
                  }
                  case Unit::Notes:
                  {
                     const BoxNode& first = *Box(u.notes.front());
                     for (int nid : u.notes)
                        out += Indent(Box(nid)->comment, "");
                     out += "if (" + Inlet(first, 0, 0) + ") {\n";
                     for (int nid : u.notes)
                        out += "   " + Statement(*Box(nid)) + "\n";
                     out += "}\n";
                     break;
                  }
                  case Unit::Group:
                  {
                     const BoxGroup* grp = nullptr;
                     for (const BoxGroup& g : graph.groups)
                     {
                        if (g.id == u.id)
                           grp = &g;
                     }
                     out += Indent(grp->comment, "");
                     out += "if (" + Ref(grp->condBoxId, grp->condPinIdx, 0) + ") {\n";
                     std::vector<const BoxNode*> members;
                     for (const BoxNode& b : graph.boxes)
                     {
                        auto g = groupOf.find(b.id);
                        if (g != groupOf.end() && g->second == grp->id && IsStatementBox(b))
                           members.push_back(&b);
                     }
                     std::sort(members.begin(), members.end(), [](const BoxNode* a, const BoxNode* b) {
                        return std::tie(a->stmtOrder, a->x, a->id) < std::tie(b->stmtOrder, b->x, b->id);
                     });
                     for (const BoxNode* m : members)
                     {
                        out += Indent(m->comment, "   ");
                        std::string s = Statement(*m);
                        if (!s.empty())
                           out += Indent(s, "   ");
                     }
                     out += "}\n";
                     break;
                  }
               }
            }
            if (!graph.trailingComment.empty())
               out += Indent(graph.trailingComment, "");
            return out;
         }
      };
   }

   std::string PrintBoxGraphToCode(const BoxGraph& graph, bool includeHeader)
   {
      Printer printer(graph);
      std::string body = printer.Print();
      return includeHeader ? FormatBoxesHeader(graph) + "\n" + body : body;
   }

   void ParseBoxesHeader(const std::string& code, BoxHeaderData& outHeader)
   {
      outHeader = BoxHeaderData{};
      std::istringstream iss(code);
      std::string line;
      while (std::getline(iss, line))
      {
         std::string t = Trim(line);
         size_t prefixLen = 0;
         if (t.rfind("# @boxes v1", 0) == 0)
            prefixLen = 11;
         else if (t.rfind("// @boxes v1", 0) == 0)
            prefixLen = 12;
         else
            continue;
         std::string rest = Trim(t.substr(prefixLen));
         std::stringstream ss(rest);
         std::string part;
         while (std::getline(ss, part, '|'))
         {
            std::string p = Trim(part);
            if (p.empty())
               continue;
            if (p.rfind("view ", 0) == 0)
            {
               float vx = 0.0f, vy = 0.0f, vz = 1.0f;
               if (std::sscanf(p.c_str() + 5, "%f,%f,%f", &vx, &vy, &vz) == 3)
               {
                  outHeader.view.panX = vx;
                  outHeader.view.panY = vy;
                  outHeader.view.zoom = vz > 0.05f ? vz : 1.0f;
                  outHeader.view.valid = true;
               }
            }
            else if (p.rfind("group ", 0) == 0)
            {
               size_t q1 = p.find('"');
               size_t q2 = (q1 != std::string::npos) ? p.find('"', q1 + 1) : std::string::npos;
               if (q1 != std::string::npos && q2 != std::string::npos)
               {
                  BoxHeaderGroup g;
                  g.name = p.substr(q1 + 1, q2 - q1 - 1);
                  std::istringstream gs(p.substr(q2 + 1));
                  std::string tok;
                  while (gs >> tok)
                  {
                     if (tok == "[folded]")
                        g.folded = true;
                     else
                        g.keys.push_back(tok);
                  }
                  outHeader.groups.push_back(std::move(g));
               }
            }
            else
            {
               size_t sp = p.find(' ');
               if (sp != std::string::npos)
               {
                  BoxHeaderEntry e;
                  e.key = p.substr(0, sp);
                  if (std::sscanf(p.c_str() + sp + 1, "%f,%f", &e.x, &e.y) == 2)
                     outHeader.positions.push_back(std::move(e));
               }
            }
         }
         break;
      }
   }

   std::string FormatBoxesHeader(const BoxGraph& graph)
   {
      std::ostringstream oss;
      oss << "# @boxes v1 view " << FormatDouble(graph.view.panX) << ","
          << FormatDouble(graph.view.panY) << "," << FormatDouble(graph.view.zoom);
      for (const BoxNode& b : graph.boxes)
      {
         if (!b.key.empty())
            oss << " | " << b.key << " " << static_cast<int>(std::round(b.x))
                << "," << static_cast<int>(std::round(b.y));
      }
      for (const BoxGroup& g : graph.groups)
      {
         if (g.isWhenGroup)
            continue;
         oss << " | group \"" << g.name << "\"";
         for (int bid : g.boxIds)
         {
            if (const BoxNode* b = graph.FindBox(bid))
               oss << " " << b->key;
         }
         if (g.folded)
            oss << " [folded]";
      }
      return oss.str();
   }

   bool ParseCodeToBoxGraph(const std::string& code, uint32_t hostMask, BoxGraph& outGraph, FieldError& outError)
   {
      // `//` is not a Field comment: blank an old-style header line (same length,
      // so every offset still points at the same text).
      std::string src = code;
      {
         size_t lineStart = 0;
         while (lineStart < src.size())
         {
            size_t lineEnd = src.find('\n', lineStart);
            if (lineEnd == std::string::npos)
               lineEnd = src.size();
            size_t p = lineStart;
            while (p < lineEnd && (src[p] == ' ' || src[p] == '\t'))
               ++p;
            if (src.compare(p, 9, "// @boxes") == 0)
               std::fill(src.begin() + static_cast<std::ptrdiff_t>(p), src.begin() + static_cast<std::ptrdiff_t>(lineEnd), ' ');
            lineStart = lineEnd + 1;
         }
      }

      std::vector<Token> tokens;
      if (!Lex(src, tokens, outError))
         return false;

      AstNodePtr ast;
      if (!ParseProgram(tokens, ast, outError))
         return false;

      outGraph = BoxGraph{};
      outGraph.hostMask = hostMask;

      BoxHeaderData header;
      ParseBoxesHeader(code, header);
      if (header.view.valid)
         outGraph.view = header.view;

      PreScanInfo preScan;
      PreScan(ast, preScan);

      GraphBuilder builder{ outGraph, preScan, src };
      for (const Token& t : tokens)
      {
         if (t.kind == TokenKind::Comment && t.text.rfind("# @boxes", 0) != 0)
            builder.comments.push_back({ t.span.offset, t.text });
      }
      if (ast && ast->kind == AstKind::Program)
      {
         const auto* prog = static_cast<const AstProgram*>(ast.get());
         for (const AstNodePtr& stmt : prog->statements)
            builder.LowerStmt(stmt);
      }
      outGraph.trailingComment = JoinComments(builder.pendingComment, builder.TakeCommentsBefore(std::string::npos));

      // Apply header positions if present
      std::unordered_map<std::string, BoxHeaderEntry> posByKey;
      for (const auto& e : header.positions)
         posByKey[e.key] = e;
      for (BoxNode& b : outGraph.boxes)
      {
         auto it = posByKey.find(b.key);
         if (it != posByKey.end())
         {
            b.x = it->second.x;
            b.y = it->second.y;
            b.hasExplicitPos = true;
         }
      }

      // Apply header groups
      std::unordered_map<std::string, int> idByKey;
      for (const BoxNode& b : outGraph.boxes)
         idByKey[b.key] = b.id;
      for (const auto& hg : header.groups)
      {
         BoxGroup g;
         g.id = builder.nextGroupId++;
         g.name = hg.name;
         g.folded = hg.folded;
         for (const std::string& k : hg.keys)
         {
            auto it = idByKey.find(k);
            if (it != idByKey.end())
               g.boxIds.push_back(it->second);
         }
         if (!g.boxIds.empty())
            outGraph.groups.push_back(std::move(g));
      }

      AutoLayoutBoxGraph(outGraph);
      return true;
   }

   void AutoLayoutBoxGraph(BoxGraph& graph)
   {
      if (graph.boxes.empty())
         return;

      std::unordered_map<int, int> depth;
      for (const BoxNode& b : graph.boxes)
         depth[b.id] = 0;

      // Longest-path column assignment (bounded iterations for safety)
      for (size_t iter = 0; iter < graph.boxes.size(); ++iter)
      {
         bool changed = false;
         for (const BoxWire& w : graph.wires)
         {
            int dSrc = depth[w.srcBoxId];
            if (depth[w.dstBoxId] < dSrc + 1)
            {
               depth[w.dstBoxId] = dSrc + 1;
               changed = true;
            }
         }
         if (!changed)
            break;
      }

      std::unordered_map<int, int> colRowCount;
      for (BoxNode& b : graph.boxes)
      {
         if (b.hasExplicitPos)
            continue;
         int col = depth[b.id];
         int row = colRowCount[col]++;
         b.x = 40.0f + static_cast<float>(col) * 210.0f;
         b.y = 40.0f + static_cast<float>(row) * 96.0f;
      }
   }
}
